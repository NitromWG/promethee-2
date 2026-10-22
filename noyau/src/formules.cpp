// Prométhée : formules des valeurs électroniques (lot L3, tâche 3.14). Voir formules.hpp.
// Licence GPL-3.0-only.
#include "promethee/formules.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "promethee/electronique.hpp"

namespace prom::ecad {

namespace {
using Complexe = std::complex<double>;
const std::string FINE = " ";
const char OHM = '\x01';
const char PI = '\x02';

constexpr Dimension dim(int kg, int m, int s, int a) {
  return Dimension{{static_cast<std::int8_t>(kg), static_cast<std::int8_t>(m), static_cast<std::int8_t>(s), static_cast<std::int8_t>(a)}};
}
constexpr Dimension OHMS = dim(1, 2, -3, -2), FARADS = dim(-1, -2, 4, 2), HENRYS = dim(1, 2, -2, -2), VOLTS = dim(1, 2, -3, -1),
                    AMPERES = dim(0, 0, 0, 1), WATTS = dim(1, 2, -3, 0), HERTZ = dim(0, 0, -1, 0), SECONDES = dim(0, 0, 1, 0);

struct UniteNommee {
  const char* symbole;
  const char* nom;  // au pluriel, pour les messages
  Dimension d;
};
const UniteNommee UNITES[] = {{"Ω", "ohms", OHMS},       {"F", "farads", FARADS},       {"H", "henrys", HENRYS},
                              {"V", "volts", VOLTS},     {"A", "ampères", AMPERES},     {"W", "watts", WATTS},
                              {"Hz", "hertz", HERTZ},    {"s", "secondes", SECONDES},   {"C", "coulombs", dim(0, 0, 1, 1)},
                              {"J", "joules", dim(1, 2, -2, 0)}, {"S", "siemens", dim(-1, -2, 3, 2)}};

Dimension plus(const Dimension& a, const Dimension& b) {
  Dimension r;
  for (std::size_t i = 0; i < 4; ++i) r.e[i] = static_cast<std::int8_t>(a.e[i] + b.e[i]);
  return r;
}
Dimension fois(const Dimension& a, int n) {
  Dimension r;
  for (std::size_t i = 0; i < 4; ++i) r.e[i] = static_cast<std::int8_t>(a.e[i] * n);
  return r;
}
std::string nomDimension(const Dimension& d) {
  if (d.sans()) return "un nombre sans unité";
  for (const auto& u : UNITES)
    if (u.d == d) return std::string("des ") + u.nom;
  return "des " + symboleDimension(d);
}

struct ErreurCalcul {
  std::string message;
};
[[noreturn]] void echec(const std::string& m) { throw ErreurCalcul{m}; }

// ---------- Texte ----------
void remplacer(std::string& s, const std::string& de, const std::string& par) {
  for (std::size_t i = s.find(de); i != std::string::npos; i = s.find(de, i + par.size())) s.replace(i, de.size(), par);
}
std::string normaliserFormule(std::string s) {
  for (const auto& [de, par] : std::vector<std::pair<std::string, std::string>>{{"µ", "u"}, {"μ", "u"}, {"Ω", std::string(1, OHM)}, {"Ω", std::string(1, OHM)},
                                                                                 {"π", std::string(1, PI)}, {"·", "*"}, {"⋅", "*"}, {"×", "*"}, {"∥", "|"},
                                                                                 {"÷", "/"}, {"−", "-"}, {" ", " "}, {" ", " "}, {" ", " "}, {"\t", " "}})
    remplacer(s, de, par);
  return s;
}
bool chiffre(char c) { return c >= '0' && c <= '9'; }
bool debutNom(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == OHM || c == '_' || static_cast<unsigned char>(c) >= 0x80; }
bool suiteNom(char c) { return debutNom(c) || chiffre(c); }

std::optional<double> facteurPrefixe(char c) {
  switch (c) {
    case 'f': return 1e-15;
    case 'p': return 1e-12;
    case 'n': return 1e-9;
    case 'u': return 1e-6;
    case 'm': return 1e-3;
    case 'k':
    case 'K': return 1e3;
    case 'M': return 1e6;
    case 'G': return 1e9;
    case 'T': return 1e12;
    default: return std::nullopt;
  }
}
std::optional<Dimension> uniteFormule(const std::string& u, bool colle) {
  if (u == std::string(1, OHM) || u == "ohm" || u == "ohms" || (colle && u == "R")) return OHMS;
  if (u == "F") return FARADS;
  if (u == "H") return HENRYS;
  if (u == "Hz" || u == "hz") return HERTZ;
  if (u == "V") return VOLTS;
  if (u == "A") return AMPERES;
  if (u == "W") return WATTS;
  if (u == "s") return SECONDES;
  return std::nullopt;
}
struct SuffixeFormule {
  bool ok = false, unite = false, imaginaire = false;
  double facteur = 1;
  Dimension d{};
};
// Ce qui suit un nombre : « j », une unité, un préfixe et une unité, un préfixe seul (collé seulement)
SuffixeFormule lireSuffixe(const std::string& id, bool colle) {
  SuffixeFormule r;
  if (colle && id == "j") {
    r.ok = r.imaginaire = true;
    return r;
  }
  if (const auto u = uniteFormule(id, colle)) {
    r.ok = r.unite = true;
    r.d = *u;
    return r;
  }
  if (id.size() >= 2)
    if (const auto f = facteurPrefixe(id[0]))
      if (const auto u = uniteFormule(id.substr(1), false)) {
        r.ok = r.unite = true;
        r.facteur = *f;
        r.d = *u;
        return r;
      }
  if (colle && id.size() == 1)
    if (const auto f = facteurPrefixe(id[0])) {
      r.ok = true;
      r.facteur = *f;
    }
  return r;
}

// ---------- Découpage ----------
enum class Genre : std::uint8_t { Nombre, Nom, Op, Fin };
struct Jeton {
  Genre genre = Genre::Fin;
  Complexe valeur{};
  Dimension d{};
  std::string texte;  // nom, ou opérateur
};

std::vector<Jeton> decouper(const std::string& s) {
  std::vector<Jeton> r;
  std::size_t i = 0;
  auto lireNom = [&](std::size_t k) {
    std::size_t l = k;
    if (l < s.size() && debutNom(s[l]))
      while (l < s.size() && suiteNom(s[l])) ++l;
    return s.substr(k, l - k);
  };
  while (i < s.size()) {
    const char c = s[i];
    if (c == ' ') {
      ++i;
      continue;
    }
    if (chiffre(c) || (c == '.' && i + 1 < s.size() && chiffre(s[i + 1]))) {
      Jeton j;
      j.genre = Genre::Nombre;
      std::size_t k = i;
      while (k < s.size() && chiffre(s[k])) ++k;
      const auto rkm = [&]() -> std::optional<double> {
        if (k == i || k + 1 >= s.size() || !chiffre(s[k + 1])) return std::nullopt;
        if (s[k] == 'R') return 1.0;
        return facteurPrefixe(s[k]);
      }();
      if (rkm) {
        // Notation RKM : 4k7, 2u2, 4R7
        const bool ohms = s[k] == 'R';
        std::size_t l = k + 1;
        while (l < s.size() && chiffre(s[l])) ++l;
        j.valeur = std::strtod((s.substr(i, k - i) + "." + s.substr(k + 1, l - k - 1)).c_str(), nullptr) * *rkm;
        if (ohms) j.d = OHMS;
        i = l;
        const std::string id = lireNom(i);
        if (!id.empty())
          if (const auto u = uniteFormule(id, false); u && (!ohms || *u == OHMS)) {
            j.d = *u;
            i += id.size();
          }
      } else {
        if (k < s.size() && (s[k] == '.' || s[k] == ',') && k + 1 < s.size() && chiffre(s[k + 1])) {
          ++k;
          while (k < s.size() && chiffre(s[k])) ++k;
        }
        if (k < s.size() && (s[k] == 'e' || s[k] == 'E')) {
          std::size_t l = k + 1;
          if (l < s.size() && (s[l] == '+' || s[l] == '-')) ++l;
          if (l < s.size() && chiffre(s[l])) {
            while (l < s.size() && chiffre(s[l])) ++l;
            k = l;
          }
        }
        std::string n = s.substr(i, k - i);
        std::replace(n.begin(), n.end(), ',', '.');
        double x = std::strtod(n.c_str(), nullptr);
        i = k;
        auto appliquer = [&](const SuffixeFormule& x2, std::size_t fin) {
          x *= x2.facteur;
          j.d = x2.d;
          i = fin;
          return x2.imaginaire;
        };
        bool imaginaire = false;
        if (const std::string id = lireNom(i); !id.empty()) {
          if (const SuffixeFormule x2 = lireSuffixe(id, true); x2.ok) imaginaire = appliquer(x2, i + id.size());
        } else if (i < s.size() && s[i] == ' ') {
          std::size_t l = i;
          while (l < s.size() && s[l] == ' ') ++l;
          if (const std::string id2 = lireNom(l); !id2.empty())
            if (const SuffixeFormule x2 = lireSuffixe(id2, false); x2.ok && x2.unite) appliquer(x2, l + id2.size());
        }
        j.valeur = imaginaire ? Complexe(0, x) : Complexe(x, 0);
      }
      r.push_back(j);
      continue;
    }
    if (c == PI) {
      r.push_back(Jeton{Genre::Nom, {}, {}, "pi"});
      ++i;
      continue;
    }
    if (debutNom(c)) {
      const std::string id = lireNom(i);
      r.push_back(Jeton{Genre::Nom, {}, {}, id});
      i += id.size();
      continue;
    }
    if (std::string("+-*/^()[],;|").find(c) != std::string::npos) {
      r.push_back(Jeton{Genre::Op, {}, {}, std::string(1, c)});
      ++i;
      continue;
    }
    echec(std::string("caractère inattendu « ") + c + " »");
  }
  r.push_back(Jeton{});
  return r;
}

const std::set<std::string>& fonctions() {
  static const std::set<std::string> f{"sqrt", "abs", "arg", "re", "im", "conj", "exp", "ln", "log", "log10", "sin", "cos", "tan", "asin", "acos", "atan",
                                       "min", "max", "round", "floor", "ceil", "par", "rled", "eserie", "det", "inv", "transp"};
  return f;
}
bool constante(const std::string& n) { return n == "pi" || n == "j"; }

// ---------- Opérations ----------
Quantite scalaire(Complexe z, Dimension d = {}) {
  Quantite q;
  q.v = {z};
  q.dim = d;
  return q;
}
std::string taille(const Quantite& q) { return std::to_string(q.lignes) + "×" + std::to_string(q.colonnes); }
bool estReel(Complexe z) { return z.imag() == 0 || std::abs(z.imag()) <= 1e-12 * std::abs(z); }

Quantite somme(const Quantite& a, const Quantite& b, double signe) {
  if (!(a.dim == b.dim)) echec("unités incompatibles : " + nomDimension(a.dim) + " et " + nomDimension(b.dim));
  if (!a.scalaire() && !b.scalaire() && (a.lignes != b.lignes || a.colonnes != b.colonnes))
    echec("matrices de tailles différentes : " + taille(a) + " et " + taille(b));
  Quantite r = a.scalaire() ? b : a;
  for (std::size_t i = 0; i < r.v.size(); ++i) {
    const Complexe x = a.scalaire() ? a.v[0] : a.v[i], y = b.scalaire() ? b.v[0] : b.v[i];
    r.v[i] = x + signe * y;
  }
  r.dim = a.dim;
  return r;
}

Quantite produit(const Quantite& a, const Quantite& b) {
  Quantite r;
  r.dim = plus(a.dim, b.dim);
  if (a.scalaire() || b.scalaire()) {
    const Quantite& m = a.scalaire() ? b : a;
    const Complexe k = a.scalaire() ? a.v[0] : b.v[0];
    r.lignes = m.lignes;
    r.colonnes = m.colonnes;
    r.v = m.v;
    for (auto& z : r.v) z *= k;
    return r;
  }
  if (a.colonnes != b.lignes) echec("produit impossible : " + taille(a) + " par " + taille(b));
  r.lignes = a.lignes;
  r.colonnes = b.colonnes;
  r.v.assign(static_cast<std::size_t>(r.lignes * r.colonnes), Complexe{});
  for (int i = 0; i < a.lignes; ++i)
    for (int j = 0; j < b.colonnes; ++j)
      for (int k = 0; k < a.colonnes; ++k)
        r.v[static_cast<std::size_t>(i * r.colonnes + j)] += a.v[static_cast<std::size_t>(i * a.colonnes + k)] * b.v[static_cast<std::size_t>(k * b.colonnes + j)];
  return r;
}

// Inverse par Gauss-Jordan avec pivot partiel ; déterminant au passage
Quantite inverse(const Quantite& a, Complexe* determinant) {
  if (a.lignes != a.colonnes) echec("matrice non carrée : " + taille(a));
  const int n = a.lignes;
  std::vector<Complexe> m = a.v, inv(static_cast<std::size_t>(n * n), Complexe{});
  for (int i = 0; i < n; ++i) inv[static_cast<std::size_t>(i * n + i)] = 1;
  Complexe det = 1;
  double echelle = 0;
  for (const auto& z : m) echelle = std::max(echelle, std::abs(z));
  auto at = [n](std::vector<Complexe>& v, int i, int j) -> Complexe& { return v[static_cast<std::size_t>(i * n + j)]; };
  for (int c = 0; c < n; ++c) {
    int p = c;
    for (int i = c + 1; i < n; ++i)
      if (std::abs(at(m, i, c)) > std::abs(at(m, p, c))) p = i;
    if (std::abs(at(m, p, c)) <= 1e-14 * std::max(echelle, 1e-300)) {
      if (determinant) {
        *determinant = 0;
        return a;
      }
      echec("matrice non inversible");
    }
    if (p != c) {
      for (int j = 0; j < n; ++j) {
        std::swap(at(m, p, j), at(m, c, j));
        std::swap(at(inv, p, j), at(inv, c, j));
      }
      det = -det;
    }
    const Complexe piv = at(m, c, c);
    det *= piv;
    for (int j = 0; j < n; ++j) {
      at(m, c, j) /= piv;
      at(inv, c, j) /= piv;
    }
    for (int i = 0; i < n; ++i)
      if (i != c) {
        const Complexe f = at(m, i, c);
        if (f == Complexe{}) continue;
        for (int j = 0; j < n; ++j) {
          at(m, i, j) -= f * at(m, c, j);
          at(inv, i, j) -= f * at(inv, c, j);
        }
      }
  }
  if (determinant) *determinant = det;
  Quantite r = a;
  r.v = inv;
  r.dim = fois(a.dim, -1);
  return r;
}

Quantite quotient(const Quantite& a, const Quantite& b) {
  if (!b.scalaire()) echec("division par une matrice : employer inv()");
  if (b.v[0] == Complexe{}) echec("division par zéro");
  Quantite r = a;
  for (auto& z : r.v) z /= b.v[0];
  r.dim = plus(a.dim, fois(b.dim, -1));
  return r;
}

Quantite puissance(const Quantite& a, const Quantite& b) {
  if (!b.scalaire() || !b.dim.sans()) echec("un exposant est un nombre sans unité");
  const Complexe e = b.v[0];
  const bool entier = estReel(e) && std::abs(e.real() - std::round(e.real())) < 1e-12 && std::abs(e.real()) <= 64;
  if (!a.scalaire()) {
    if (!entier) echec("une matrice ne s'élève qu'à une puissance entière");
    if (a.lignes != a.colonnes) echec("matrice non carrée : " + taille(a));
    int n = static_cast<int>(std::round(e.real()));
    Quantite base = n < 0 ? inverse(a, nullptr) : a;
    n = std::abs(n);
    Quantite r = base;
    r.dim = {};
    for (int i = 0; i < r.lignes; ++i)
      for (int j = 0; j < r.colonnes; ++j) r.v[static_cast<std::size_t>(i * r.colonnes + j)] = i == j ? 1 : 0;
    for (int i = 0; i < n; ++i) r = produit(r, base);
    return r;
  }
  if (!a.dim.sans() && !entier) echec("une grandeur avec unité ne s'élève qu'à une puissance entière");
  const Complexe x = a.v[0];
  Quantite r = scalaire({}, a.dim.sans() ? Dimension{} : fois(a.dim, static_cast<int>(std::round(e.real()))));
  if (estReel(x) && estReel(e) && (x.real() >= 0 || entier)) r.v[0] = std::pow(x.real(), e.real());
  else {
    if (x == Complexe{}) r.v[0] = 0;
    else r.v[0] = std::pow(x, e);
  }
  return r;
}

Quantite parallele(const Quantite& a, const Quantite& b) {
  if (!a.scalaire() || !b.scalaire()) echec("mise en parallèle de deux grandeurs simples seulement");
  if (!(a.dim == b.dim)) echec("unités incompatibles : " + nomDimension(a.dim) + " et " + nomDimension(b.dim));
  const Complexe s = a.v[0] + b.v[0];
  if (s == Complexe{}) echec("mise en parallèle de somme nulle");
  return scalaire(a.v[0] * b.v[0] / s, a.dim);
}

void sansUnite(const Quantite& q, const std::string& f) {
  if (!q.dim.sans()) echec(f + "() attend un nombre sans unité, pas " + nomDimension(q.dim));
}
void unScalaire(const Quantite& q, const std::string& f) {
  if (!q.scalaire()) echec(f + "() attend une grandeur simple, pas une matrice");
}
double reelDe(const Quantite& q, const std::string& f) {
  unScalaire(q, f);
  if (!estReel(q.v[0])) echec(f + "() attend un nombre réel");
  return q.v[0].real();
}
template <class Fn>
Quantite chaque(Quantite q, Fn fn) {
  for (auto& z : q.v) z = fn(z);
  return q;
}

Quantite appeler(const std::string& f, std::vector<Quantite> a) {
  auto arite = [&](std::size_t n) {
    if (a.size() != n) echec(f + "() attend " + std::to_string(n) + " argument" + (n > 1 ? "s" : ""));
  };
  auto auMoins = [&](std::size_t n) {
    if (a.size() < n) echec(f + "() attend au moins " + std::to_string(n) + " arguments");
  };
  if (f == "sqrt") {
    arite(1);
    for (auto x : a[0].dim.e)
      if (x % 2 != 0) echec("racine carrée de " + nomDimension(a[0].dim) + " : cette unité n'a pas de racine");
    Quantite r = chaque(a[0], [](Complexe z) { return estReel(z) && z.real() >= 0 ? Complexe(std::sqrt(z.real()), 0) : std::sqrt(z); });
    for (auto& x : r.dim.e) x = static_cast<std::int8_t>(x / 2);
    return r;
  }
  if (f == "abs") {
    arite(1);
    return chaque(a[0], [](Complexe z) { return Complexe(std::abs(z), 0); });
  }
  if (f == "re" || f == "im" || f == "conj") {
    arite(1);
    return chaque(a[0], [&f](Complexe z) { return f == "re" ? Complexe(z.real(), 0) : f == "im" ? Complexe(z.imag(), 0) : std::conj(z); });
  }
  if (f == "arg") {
    arite(1);
    Quantite r = chaque(a[0], [](Complexe z) { return Complexe(std::arg(z), 0); });
    r.dim = {};
    return r;
  }
  if (f == "exp" || f == "ln" || f == "log" || f == "log10" || f == "sin" || f == "cos" || f == "tan" || f == "asin" || f == "acos" || f == "atan") {
    arite(1);
    sansUnite(a[0], f);
    return chaque(a[0], [&f](Complexe z) -> Complexe {
      const bool r = estReel(z);
      const double x = z.real();
      if (f == "exp") return r ? Complexe(std::exp(x), 0) : std::exp(z);
      if (f == "ln" || f == "log10" || f == "log") {
        if (z == Complexe{}) echec(f + "(0) n'est pas défini");
        const Complexe l = r && x > 0 ? Complexe(std::log(x), 0) : std::log(z);
        return f == "ln" ? l : l / std::log(10.0);
      }
      if (f == "sin") return r ? Complexe(std::sin(x), 0) : std::sin(z);
      if (f == "cos") return r ? Complexe(std::cos(x), 0) : std::cos(z);
      if (f == "tan") return r ? Complexe(std::tan(x), 0) : std::tan(z);
      if (f == "asin") return r && std::abs(x) <= 1 ? Complexe(std::asin(x), 0) : std::asin(z);
      if (f == "acos") return r && std::abs(x) <= 1 ? Complexe(std::acos(x), 0) : std::acos(z);
      return r ? Complexe(std::atan(x), 0) : std::atan(z);
    });
  }
  if (f == "min" || f == "max") {
    auMoins(1);
    Quantite r = a[0];
    double m = reelDe(a[0], f);
    for (const auto& x : a) {
      if (!(x.dim == a[0].dim)) echec("unités incompatibles : " + nomDimension(a[0].dim) + " et " + nomDimension(x.dim));
      const double y = reelDe(x, f);
      if (f == "min" ? y < m : y > m) {
        m = y;
        r = x;
      }
    }
    return r;
  }
  if (f == "round" || f == "floor" || f == "ceil") {
    arite(1);
    reelDe(a[0], f);
    return chaque(a[0], [&f](Complexe z) { return Complexe(f == "round" ? std::round(z.real()) : f == "floor" ? std::floor(z.real()) : std::ceil(z.real()), 0); });
  }
  if (f == "par") {
    auMoins(2);
    Quantite r = a[0];
    for (std::size_t i = 1; i < a.size(); ++i) r = parallele(r, a[i]);
    return r;
  }
  if (f == "rled") {
    arite(3);  // tension d'alimentation, tension de la LED, courant
    return quotient(somme(a[0], a[1], -1), a[2]);
  }
  if (f == "eserie") {
    arite(2);
    const double x = reelDe(a[0], f), n = reelDe(a[1], f);
    sansUnite(a[1], f);
    const int k = static_cast<int>(std::round(n));
    if (serieE(k).empty() || std::abs(n - k) > 1e-9) echec("eserie() : série 3, 6, 12, 24, 48, 96 ou 192");
    if (x <= 0) echec("eserie() : valeur positive attendue");
    return scalaire(plusProcheDansSerie(x, k), a[0].dim);
  }
  if (f == "det") {
    arite(1);
    Complexe d;
    inverse(a[0], &d);
    return scalaire(d, fois(a[0].dim, a[0].lignes));
  }
  if (f == "inv") {
    arite(1);
    return inverse(a[0], nullptr);
  }
  if (f == "transp") {
    arite(1);
    Quantite r = a[0];
    std::swap(r.lignes, r.colonnes);
    for (int i = 0; i < a[0].lignes; ++i)
      for (int j = 0; j < a[0].colonnes; ++j) r.v[static_cast<std::size_t>(j * r.colonnes + i)] = a[0].v[static_cast<std::size_t>(i * a[0].colonnes + j)];
    return r;
  }
  echec("fonction inconnue : " + f);
}

// ---------- Analyse ----------
class Analyseur {
 public:
  Analyseur(const std::vector<Jeton>& j, const ContexteFormule& c) : jetons_(j), contexte_(c) {}

  Quantite tout() {
    if (courant().genre == Genre::Fin) echec("formule vide");
    Quantite q = expression();
    if (courant().genre != Genre::Fin) echec("« " + courant().texte + " » inattendu");
    return q;
  }

 private:
  const Jeton& courant() const { return jetons_[i_]; }
  bool estOp(const char* o) const { return courant().genre == Genre::Op && courant().texte == o; }
  void attendre(const char* o, const std::string& message) {
    if (!estOp(o)) echec(message);
    ++i_;
  }

  Quantite expression() {
    Quantite q = paralleles();
    while (estOp("+") || estOp("-")) {
      const double s = courant().texte == "+" ? 1 : -1;
      ++i_;
      q = somme(q, paralleles(), s);
    }
    return q;
  }
  Quantite paralleles() {
    Quantite q = produits();
    while (estOp("|")) {
      ++i_;
      q = parallele(q, produits());
    }
    return q;
  }
  bool debutFacteur() const {
    const Jeton& j = courant();
    return j.genre == Genre::Nombre || j.genre == Genre::Nom || (j.genre == Genre::Op && (j.texte == "(" || j.texte == "["));
  }
  Quantite produits() {
    Quantite q = unaire();
    while (true) {
      if (estOp("*")) {
        ++i_;
        q = produit(q, unaire());
      } else if (estOp("/")) {
        ++i_;
        q = quotient(q, unaire());
      } else if (debutFacteur()) {
        q = produit(q, unaire());  // multiplication implicite : 2π, 2(R1+R2)
      } else
        break;
    }
    return q;
  }
  Quantite unaire() {
    if (estOp("-")) {
      ++i_;
      Quantite q = unaire();
      for (auto& z : q.v) z = -z;
      return q;
    }
    if (estOp("+")) {
      ++i_;
      return unaire();
    }
    return puissances();
  }
  Quantite puissances() {
    Quantite q = primaire();
    if (estOp("^")) {
      ++i_;
      q = puissance(q, unaire());
    }
    return q;
  }
  Quantite primaire() {
    const Jeton j = courant();
    switch (j.genre) {
      case Genre::Fin: echec("expression incomplète");
      case Genre::Nombre: ++i_; return scalaire(j.valeur, j.d);
      case Genre::Nom: {
        ++i_;
        if (estOp("(") && fonctions().count(j.texte)) {
          ++i_;
          std::vector<Quantite> args;
          if (!estOp(")")) {
            args.push_back(expression());
            while (estOp(",")) {
              ++i_;
              args.push_back(expression());
            }
          }
          attendre(")", "parenthèse non fermée après les arguments de " + j.texte + "()");
          return appeler(j.texte, std::move(args));
        }
        if (contexte_)
          if (auto q = contexte_(j.texte)) return *q;
        if (j.texte == "pi") return scalaire(3.14159265358979323846);
        if (j.texte == "j") return scalaire(Complexe(0, 1));
        echec("nom inconnu ou sans valeur : " + j.texte);
      }
      case Genre::Op: break;
    }
    if (j.texte == "(") {
      ++i_;
      Quantite q = expression();
      attendre(")", "parenthèse non fermée");
      return q;
    }
    if (j.texte == "[") {
      ++i_;
      std::vector<std::vector<Quantite>> lignes(1);
      if (estOp("]")) echec("matrice vide");
      while (true) {
        Quantite e = expression();
        if (!e.scalaire()) echec("une matrice s'écrit avec des nombres");
        lignes.back().push_back(e);
        if (estOp(",")) ++i_;
        else if (estOp(";")) {
          ++i_;
          lignes.emplace_back();
        } else
          break;
      }
      attendre("]", "crochet non fermé");
      Quantite m;
      m.lignes = static_cast<int>(lignes.size());
      m.colonnes = static_cast<int>(lignes.front().size());
      m.dim = lignes.front().front().dim;
      m.v.clear();
      for (const auto& l : lignes) {
        if (static_cast<int>(l.size()) != m.colonnes) echec("lignes de longueurs différentes dans la matrice");
        for (const auto& e : l) {
          if (!(e.dim == m.dim)) echec("unités différentes dans la matrice");
          m.v.push_back(e.v[0]);
        }
      }
      return m;
    }
    echec("« " + j.texte + " » inattendu");
  }

  const std::vector<Jeton>& jetons_;
  const ContexteFormule& contexte_;
  std::size_t i_ = 0;
};

// ---------- Écriture ----------
std::string exposant(int n) {
  static const char* chiffres[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
  std::string r = n < 0 ? "⁻" : "";
  for (char c : std::to_string(std::abs(n))) r += chiffres[c - '0'];
  return r;
}
// Nombre sans unité : six chiffres significatifs au plus, virgule décimale
std::string nombreLibre(double x) {
  if (x == 0) return "0";
  char t[64];
  std::snprintf(t, sizeof t, "%.6g", x);
  std::string s = t;
  const std::size_t e = s.find('e');
  if (e != std::string::npos) {
    std::string m = s.substr(0, e), ex = s.substr(e + 1);
    const bool negatif = ex[0] == '-';
    ex = ex.substr(ex[0] == '-' || ex[0] == '+' ? 1 : 0);
    while (ex.size() > 1 && ex[0] == '0') ex.erase(0, 1);
    s = m + "e" + (negatif ? "-" : "") + ex;
  }
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
std::string complexeLibre(Complexe z) {
  const double seuil = 1e-12 * std::abs(z);
  const bool reel = std::abs(z.imag()) <= seuil, imaginaire = std::abs(z.real()) <= seuil && !reel;
  if (reel) return nombreLibre(z.real());
  if (imaginaire) return nombreLibre(z.imag()) + "j";
  return nombreLibre(z.real()) + (z.imag() < 0 ? " - " : " + ") + nombreLibre(std::abs(z.imag())) + "j";
}

std::optional<Quantite> grandeurProfonde(const ConceptionElec& c, const std::string& nom, int profondeur) {
  for (const auto& k : c.composants)
    if (k.reference == nom) {
      const FamilleValeur f = familleDe(c, k);
      const ValeurComprise v = lireValeurComposant(k.valeur, f);
      if (!v.comprise()) return std::nullopt;
      return Quantite::nombre(v.nombre, dimensionDeFamille(f));
    }
  if (const auto it = c.variables.find(nom); it != c.variables.end() && profondeur < 16) {
    const ResultatFormule r = evaluerFormule(it->second, [&](const std::string& n) { return grandeurProfonde(c, n, profondeur + 1); });
    if (r.ok) return r.q;
  }
  return std::nullopt;
}
}  // namespace

bool Quantite::reelle() const {
  return std::all_of(v.begin(), v.end(), [](Complexe z) { return estReel(z); });
}

Dimension dimensionDeFamille(FamilleValeur f) {
  switch (f) {
    case FamilleValeur::Resistance:
    case FamilleValeur::Ferrite: return OHMS;
    case FamilleValeur::Condensateur: return FARADS;
    case FamilleValeur::Inductance: return HENRYS;
    case FamilleValeur::Fusible: return AMPERES;
    case FamilleValeur::Quartz: return HERTZ;
    case FamilleValeur::Zener:
    case FamilleValeur::Pile: return VOLTS;
    case FamilleValeur::Aucune: break;
  }
  return {};
}

std::string symboleDimension(const Dimension& d) {
  if (d.sans()) return {};
  for (const auto& u : UNITES)
    if (u.d == d) return u.symbole;
  static const char* base[] = {"kg", "m", "s", "A"};
  std::string r;
  for (std::size_t i = 0; i < 4; ++i)
    if (d.e[i] != 0) r += (r.empty() ? "" : "·") + std::string(base[i]) + (d.e[i] == 1 ? "" : exposant(d.e[i]));
  return r;
}

ResultatFormule evaluerFormule(const std::string& texte, const ContexteFormule& contexte) {
  ResultatFormule r;
  try {
    const std::vector<Jeton> j = decouper(normaliserFormule(texte));
    for (std::size_t i = 0; i < j.size(); ++i)
      if (j[i].genre == Genre::Nom && !constante(j[i].texte) && !(fonctions().count(j[i].texte) && j[i + 1].genre == Genre::Op && j[i + 1].texte == "(") &&
          std::find(r.noms.begin(), r.noms.end(), j[i].texte) == r.noms.end())
        r.noms.push_back(j[i].texte);
    Analyseur a(j, contexte);
    r.q = a.tout();
    for (const auto& z : r.q.v)
      if (!std::isfinite(z.real()) || !std::isfinite(z.imag())) echec("résultat infini ou indéfini");
    r.ok = true;
  } catch (const ErreurCalcul& e) {
    r.erreur = e.message;
  }
  return r;
}

std::vector<std::string> nomsDeFormule(const std::string& texte) {
  std::vector<std::string> r;
  try {
    const std::vector<Jeton> j = decouper(normaliserFormule(texte));
    for (std::size_t i = 0; i < j.size(); ++i)
      if (j[i].genre == Genre::Nom && !constante(j[i].texte) && !(fonctions().count(j[i].texte) && j[i + 1].genre == Genre::Op && j[i + 1].texte == "(") &&
          std::find(r.begin(), r.end(), j[i].texte) == r.end())
        r.push_back(j[i].texte);
  } catch (const ErreurCalcul&) {
  }
  return r;
}

std::string ecrireQuantite(const Quantite& q, int chiffres) {
  const std::string u = symboleDimension(q.dim);
  bool simple = false;
  for (const auto& x : UNITES) simple |= u == x.symbole;
  if (q.scalaire()) {
    const Complexe z = q.v[0];
    if (estReel(z)) {
      if (u.empty()) return nombreLibre(z.real());
      if (simple) return ecrireSI(z.real(), u, chiffres);
      return nombreLibre(z.real()) + FINE + u;
    }
    return complexeLibre(z) + (u.empty() ? "" : FINE + u);
  }
  std::string r = "[";
  for (int i = 0; i < q.lignes; ++i) {
    if (i) r += "; ";
    for (int j = 0; j < q.colonnes; ++j) r += (j ? ", " : "") + complexeLibre(q.v[static_cast<std::size_t>(i * q.colonnes + j)]);
  }
  return r + "]" + (u.empty() ? "" : FINE + u);
}

std::optional<Quantite> grandeurDe(const ConceptionElec& c, const std::string& nom) { return grandeurProfonde(c, nom, 0); }

std::vector<ErreurFormule> recalculerFormules(ConceptionElec& c) {
  std::vector<ErreurFormule> erreurs;
  std::map<std::string, std::size_t> parRepere;
  std::vector<std::size_t> aFormule;
  for (std::size_t i = 0; i < c.composants.size(); ++i)
    if (!c.composants[i].formule.empty()) {
      aFormule.push_back(i);
      parRepere[c.composants[i].reference] = i;
    }
  if (aFormule.empty()) return erreurs;
  std::set<std::string> references;
  for (const auto& k : c.composants) references.insert(k.reference);
  // Dépendances, à travers les variables de la conception
  std::map<std::size_t, std::set<std::size_t>> dependances;
  for (std::size_t i : aFormule) {
    std::set<std::string> vus;
    std::vector<std::string> pile = nomsDeFormule(c.composants[i].formule);
    while (!pile.empty()) {
      const std::string n = pile.back();
      pile.pop_back();
      if (!vus.insert(n).second) continue;
      if (const auto it = parRepere.find(n); it != parRepere.end()) dependances[i].insert(it->second);
      if (!references.count(n))
        if (const auto v = c.variables.find(n); v != c.variables.end())
          for (const auto& m : nomsDeFormule(v->second)) pile.push_back(m);
    }
  }
  // Ordre des dépendances ; ce qui reste est en cycle ou dépend d'un cycle
  std::vector<std::size_t> ordre;
  std::set<std::size_t> faits;
  for (bool progres = true; progres;) {
    progres = false;
    for (std::size_t i : aFormule)
      if (!faits.count(i) && std::all_of(dependances[i].begin(), dependances[i].end(), [&](std::size_t d) { return faits.count(d) > 0; })) {
        ordre.push_back(i);
        faits.insert(i);
        progres = true;
      }
  }
  for (std::size_t i : ordre) {
    ComposantElec& k = c.composants[i];
    const FamilleValeur f = familleDe(c, k);
    const ResultatFormule r = evaluerFormule(k.formule, [&](const std::string& n) { return grandeurDe(c, n); });
    auto erreur = [&](const std::string& m) { erreurs.push_back({k.uuid, k.reference, m}); };
    if (!r.ok) {
      erreur(r.erreur);
      continue;
    }
    if (!r.q.scalaire() || !r.q.reelle()) {
      erreur("la formule doit donner un nombre réel, pas " + ecrireQuantite(r.q));
      continue;
    }
    if (f != FamilleValeur::Aucune && !r.q.dim.sans() && !(r.q.dim == dimensionDeFamille(f))) {
      erreur("la formule donne " + nomDimension(r.q.dim) + " ; " + phraseUnite(f));
      continue;
    }
    const std::string texte = f == FamilleValeur::Aucune ? ecrireQuantite(r.q) : ecrireSI(r.q.reel(), uniteDeFamille(f), 3);
    if (f != FamilleValeur::Aucune)
      if (const ValeurComprise v = lireValeurComposant(texte, f); !v.comprise()) {
        erreur(texte + " : " + v.raison);
        continue;
      }
    k.valeur = texte;
  }
  for (std::size_t i : aFormule)
    if (!faits.count(i)) {
      std::string cycle;
      for (std::size_t d : dependances[i])
        if (!faits.count(d)) cycle += (cycle.empty() ? "" : ", ") + c.composants[d].reference;
      erreurs.push_back({c.composants[i].uuid, c.composants[i].reference, "formule en cycle avec " + cycle});
    }
  return erreurs;
}

std::string texteCalcule(const ConceptionElec& c, const std::string& texte) {
  std::string r;
  std::size_t i = 0;
  while (true) {
    const std::size_t d = texte.find("{=", i);
    const std::size_t f = d == std::string::npos ? std::string::npos : texte.find('}', d + 2);
    if (f == std::string::npos) {
      r += texte.substr(i);
      return r;
    }
    r += texte.substr(i, d - i);
    const std::string formule = texte.substr(d + 2, f - d - 2);
    const ResultatFormule x = evaluerFormule(formule, [&](const std::string& n) { return grandeurDe(c, n); });
    r += x.ok ? ecrireQuantite(x.q) : "{?" + formule + "}";
    i = f + 1;
  }
}

}  // namespace prom::ecad
