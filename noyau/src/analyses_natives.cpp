// Prométhée : analyses natives de ngspice et analyse symbolique (lot L14 de la 0.34 ; 14.7). Voir analyses_natives.hpp.
// Licence GPL-3.0-only.
#include "promethee/analyses_natives.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <sstream>

#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string nb(double v) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(12);
  o << v;
  return o.str();
}
// La carte d'analyse est la dernière ligne avant « .end »
std::string remplacerAnalyse(const std::string& texte, const std::string& carte) {
  const auto fin = texte.rfind("\n.end");
  if (fin == std::string::npos) return texte;
  const auto debut = texte.rfind('\n', fin - 1);
  if (debut == std::string::npos) return texte;
  return texte.substr(0, debut + 1) + carte + texte.substr(fin);
}
const std::vector<double>* reelContenant(const ResultatSimulation& r, const std::string& morceau) {
  for (const auto& [k, v] : r.reels)
    if (minuscules(k).find(morceau) != std::string::npos && !v.empty()) return &v;
  return nullptr;
}
std::string noeud(const NetlistSpice& n, const std::string& reseau) {
  const auto it = n.noeuds.find(reseau);
  return it == n.noeuds.end() ? std::string() : it->second;
}
}  // namespace

// ======================================================================================================================
// Analyses natives
// ======================================================================================================================
ResultatNatif analyseNative(const ConceptionElec& c, const AnalyseNative& a) {
  ResultatNatif r;
  if (!ngspiceDisponible()) {
    r.erreur = "ngspice n'est pas installé";
    return r;
  }
  AnalyseSpice base;
  base.type = AnalyseSpice::Type::PointDeRepos;
  OptionsSimulation o;
  if (a.genre == AnalyseNative::Genre::Fourier) {
    base.type = AnalyseSpice::Type::Transitoire;
    base.pas = 1 / (a.fondamentale * 400);
    base.duree = a.periodes / a.fondamentale;
    o.pasMax = base.pas;
  }
  const NetlistSpice n = netlistSpice(c, base, o);
  if (!n.valide()) {
    r.erreur = n.erreurs.front();
    return r;
  }
  const std::string sortie = noeud(n, a.sortie), ref = a.reference.empty() ? std::string("0") : noeud(n, a.reference);
  if (sortie.empty()) {
    r.erreur = "réseau de sortie « " + a.sortie + " » absent de la netlist";
    return r;
  }
  const std::string v = "v(" + sortie + (ref.empty() || ref == "0" ? std::string() : "," + ref) + ")";
  const std::string source = "v" + minuscules(a.entree);
  std::string carte;
  switch (a.genre) {
    case AnalyseNative::Genre::FonctionTransfert: carte = ".tf " + v + " " + source; break;
    case AnalyseNative::Genre::Bruit:
      carte = ".noise " + v + " " + source + " dec " + std::to_string(std::max(1, a.pointsParDecade)) + " " + nb(a.fDebut) + " " + nb(a.fFin);
      break;
    case AnalyseNative::Genre::PolesZeros: {
      const ComposantElec* x = c.parReference(a.entree);
      std::string entree = x ? noeud(n, connectivite(c).nom(x->uuid, "1")) : std::string();
      if (entree.empty()) entree = noeud(n, a.entree);
      if (entree.empty()) {
        r.erreur = "entrée « " + a.entree + " » introuvable";
        return r;
      }
      carte = ".pz " + entree + " 0 " + sortie + " " + (ref.empty() ? std::string("0") : ref) + " vol " + (a.polesSeulement ? "pol" : "pz");
      break;
    }
    case AnalyseNative::Genre::Fourier: carte = base.carte() + " " + nb(base.pas) + "\n.four " + nb(a.fondamentale) + " " + v; break;
  }
  const ResultatSimulation res = simuler(remplacerAnalyse(n.texte, carte));
  r.console = res.console;
  if (!res.reussi()) {
    r.erreur = res.erreur;
    return r;
  }
  switch (a.genre) {
    case AnalyseNative::Genre::FonctionTransfert:
      if (const auto* g = reelContenant(res, "transfer_function")) r.gain = g->front();
      if (const auto* z = reelContenant(res, "input_impedance")) r.impedanceEntree = z->front();
      if (const auto* z = reelContenant(res, "output_impedance")) r.impedanceSortie = z->front();
      break;
    case AnalyseNative::Genre::PolesZeros:
      for (const auto& [k, z] : res.complexes) {
        const std::string m = minuscules(k);
        if (m.find("pole(") != std::string::npos)
          for (const auto& x : z) r.poles.push_back(x);
        else if (m.find("zero(") != std::string::npos)
          for (const auto& x : z) r.zeros.push_back(x);
      }
      for (const auto& [k, z] : res.reels) {
        const std::string m = minuscules(k);
        if (m.find("pole(") != std::string::npos)
          for (const double x : z) r.poles.emplace_back(x, 0);
        else if (m.find("zero(") != std::string::npos)
          for (const double x : z) r.zeros.emplace_back(x, 0);
      }
      break;
    case AnalyseNative::Genre::Bruit:
      if (const auto* f = reelContenant(res, "noise1.frequency")) r.frequences = *f;
      if (const auto* s = reelContenant(res, "onoise_spectrum")) r.bruitSortie = *s;
      if (const auto* s = reelContenant(res, "inoise_spectrum")) r.bruitEntree = *s;
      if (const auto* t = reelContenant(res, "onoise_total")) r.bruitTotalSortie = t->front();
      if (const auto* t = reelContenant(res, "inoise_total")) r.bruitTotalEntree = t->front();
      break;
    case AnalyseNative::Genre::Fourier: {
      // Taux lu dans la sortie de ngspice (« THD: 0.01 % »), et harmoniques recalculées sur le dernier cycle
      for (const auto& l : res.console) {
        const std::string m = minuscules(l);
        if (const auto p = m.find("thd"); p != std::string::npos) {
          const auto d = m.find_first_of("0123456789.", p + 3);
          if (d != std::string::npos) r.distorsion = std::atof(m.c_str() + d);
        }
      }
      const auto* t = res.reel("time");
      const auto* y = res.reel(v);
      if (!y) y = res.reel(sortie);
      if (t && y) {
        Releve s;
        for (std::size_t i = 0; i < t->size() && i < y->size(); ++i) s.push_back({(*t)[i], (*y)[i]});
        const double thd = tauxDistorsion(s, a.fondamentale, a.harmoniques, &r.amplitudes);
        if (r.distorsion == 0) r.distorsion = thd;
      }
      break;
    }
  }
  r.ok = true;
  return r;
}

double tauxDistorsion(const Releve& s, double f0, int harmoniques, std::vector<double>* amplitudes, int periodes) {
  if (s.size() < 4 || f0 <= 0) return 0;
  const double T = periodes / f0;
  const double t1 = s.back().t, t0 = t1 - T;
  if (t0 < s.front().t) return 0;
  // Transformée discrète sur une grille régulière fine de la fenêtre (périodes entières)
  const int n = 4096;
  std::vector<double> x(n);
  for (int i = 0; i < n; ++i) x[static_cast<std::size_t>(i)] = valeurA(s, t0 + T * i / n);
  std::vector<double> a;
  for (int h = 1; h <= std::max(1, harmoniques); ++h) {
    std::complex<double> z = 0;
    for (int i = 0; i < n; ++i) z += x[static_cast<std::size_t>(i)] * std::exp(std::complex<double>(0, -2 * std::numbers::pi * h * periodes * i / n));
    a.push_back(2 * std::abs(z) / n);
  }
  if (amplitudes) *amplitudes = a;
  if (a.empty() || a[0] <= 0) return 0;
  double somme = 0;
  for (std::size_t i = 1; i < a.size(); ++i) somme += a[i] * a[i];
  return 100 * std::sqrt(somme) / a[0];
}

Spectre fft(const Releve& s, bool hann) {
  Spectre r;
  if (s.size() < 4) return r;
  std::size_t n = 1;
  while (n < s.size()) n <<= 1;
  n = std::max<std::size_t>(n, 64);
  const double t0 = s.front().t, duree = s.back().t - t0;
  if (duree <= 0) return r;
  std::vector<std::complex<double>> x(n);
  double somme = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double w = hann ? 0.5 - 0.5 * std::cos(2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n - 1)) : 1.0;
    somme += w;
    x[i] = w * valeurA(s, t0 + duree * static_cast<double>(i) / static_cast<double>(n));
  }
  // Radix 2, en place
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(x[i], x[j]);
  }
  for (std::size_t l = 2; l <= n; l <<= 1) {
    const std::complex<double> w = std::polar(1.0, -2 * std::numbers::pi / static_cast<double>(l));
    for (std::size_t i = 0; i < n; i += l) {
      std::complex<double> u = 1;
      for (std::size_t k = 0; k < l / 2; ++k) {
        const std::complex<double> a = x[i + k], b = u * x[i + k + l / 2];
        x[i + k] = a + b;
        x[i + k + l / 2] = a - b;
        u *= w;
      }
    }
  }
  const double fe = static_cast<double>(n) / duree;
  for (std::size_t k = 0; k <= n / 2; ++k) {
    r.frequences.push_back(fe * static_cast<double>(k) / static_cast<double>(n));
    r.amplitudes.push_back((k == 0 ? 1.0 : 2.0) * std::abs(x[k]) / somme);
  }
  return r;
}

std::vector<ResultatSimulation> enchainer(const ConceptionElec& c, const std::vector<AnalyseSpice>& analyses, const OptionsSimulation& o) {
  std::vector<ResultatSimulation> v;
  for (const auto& a : analyses) v.push_back(simuler(netlistSpice(c, a, o)));
  return v;
}

// ======================================================================================================================
// Analyse symbolique (analyse nodale modifiée)
// ======================================================================================================================
namespace {
// Polynôme de Laurent en s et en grandeurs : exposants (s d'abord) → coefficient
using Exposants = std::vector<int>;
using Poly = std::map<Exposants, double>;
Poly produit(const Poly& a, const Poly& b) {
  Poly r;
  for (const auto& [ea, ca] : a)
    for (const auto& [eb, cb] : b) {
      Exposants e(ea.size());
      for (std::size_t i = 0; i < e.size(); ++i) e[i] = ea[i] + eb[i];
      r[e] += ca * cb;
    }
  for (auto it = r.begin(); it != r.end();) it = std::abs(it->second) < 1e-300 ? r.erase(it) : std::next(it);
  return r;
}
void ajouter(Poly& a, const Poly& b, double signe = 1) {
  for (const auto& [e, c] : b) a[e] += signe * c;
  for (auto it = a.begin(); it != a.end();) it = it->second == 0 ? a.erase(it) : std::next(it);
}
struct Element {
  char genre;         // R, C, L, V
  std::string a, b;   // nœuds
  int symbole = -1;   // −1 : valeur numérique
  double valeur = 0;
  bool entree = false;
};
}  // namespace

std::map<std::string, double> valeursSymboles(const ConceptionElec& c) {
  std::map<std::string, double> v;
  for (const auto& x : c.composants) {
    FamilleValeur f = x.symbole == "R" ? FamilleValeur::Resistance : (x.symbole == "C" || x.symbole == "C_Pol") ? FamilleValeur::Condensateur : x.symbole == "L" ? FamilleValeur::Inductance : FamilleValeur::Aucune;
    if (f == FamilleValeur::Aucune) continue;
    const ValeurComprise w = lireValeurComposant(x.valeur, f);
    if (w.etat == EtatValeur::Comprise) v[x.reference] = w.nombre;
  }
  return v;
}

std::optional<FonctionSymbolique> transfertSymbolique(const ConceptionElec& c, const std::string& entree, const std::string& sortie, std::string* erreur,
                                                      const std::set<std::string>& numeriques) {
  auto refuser = [&](const std::string& e) -> std::optional<FonctionSymbolique> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  AnalyseSpice op;
  op.type = AnalyseSpice::Type::PointDeRepos;
  const NetlistSpice n = netlistSpice(c, op);
  if (!n.valide()) return refuser(n.erreurs.front());
  const std::string ns = noeud(n, sortie);
  if (ns.empty()) return refuser("réseau de sortie « " + sortie + " » absent");
  // Éléments du premier niveau de la netlist
  FonctionSymbolique f;
  std::vector<Element> elements;
  std::istringstream in(n.texte);
  bool sousCircuit = false, entreeVue = false;
  const std::map<std::string, double> valeurs = valeursSymboles(c);
  for (std::string l; std::getline(in, l);) {
    const std::string m = minuscules(l);
    if (m.rfind(".subckt", 0) == 0) sousCircuit = true;
    if (m.rfind(".ends", 0) == 0) {
      sousCircuit = false;
      continue;
    }
    if (sousCircuit || m.empty() || !std::isalpha(static_cast<unsigned char>(m[0]))) continue;
    std::istringstream mots(l);
    std::string inst, a, b, val;
    mots >> inst >> a >> b >> val;
    const char g = static_cast<char>(std::toupper(static_cast<unsigned char>(inst[0])));
    const auto it = n.instances.find(minuscules(inst));
    const std::string ref = it == n.instances.end() ? inst.substr(1) : it->second;
    Element e;
    e.genre = g;
    e.a = minuscules(a);
    e.b = minuscules(b);
    if (g == 'R' || g == 'C' || g == 'L') {
      // Grandeur nommée par le repère ; un composant fait de plusieurs éléments (potentiomètre) les nomme chacun
      const std::string base = inst.substr(1);
      const bool seul = minuscules(base) == minuscules(ref);
      const std::string nom = seul ? ref : base;
      const auto v = valeurs.find(ref);
      e.valeur = seul && v != valeurs.end() ? v->second : std::atof(val.c_str());
      if (!numeriques.count(ref)) {
        const auto p = std::find(f.symboles.begin(), f.symboles.end(), nom);
        e.symbole = static_cast<int>(p - f.symboles.begin());
        if (p == f.symboles.end()) f.symboles.push_back(nom);
      }
    } else if (g == 'V') {
      e.entree = minuscules(ref) == minuscules(entree);
      entreeVue |= e.entree;
    } else if (g == 'I')
      continue;  // source de courant éteinte
    else
      return refuser("élément « " + inst + " » hors de l'analyse symbolique (R, C, L et sources seulement)");
    elements.push_back(e);
  }
  if (!entreeVue) return refuser("source d'entrée « " + entree + " » introuvable");
  // Inconnues : tensions des nœuds (hors masse), puis courants des sources de tension
  std::vector<std::string> noeuds;
  auto indice = [&](const std::string& x) -> int {
    if (x == "0") return -1;
    const auto p = std::find(noeuds.begin(), noeuds.end(), x);
    if (p != noeuds.end()) return static_cast<int>(p - noeuds.begin());
    noeuds.push_back(x);
    return static_cast<int>(noeuds.size()) - 1;
  };
  for (const auto& e : elements) indice(e.a), indice(e.b);
  int sources = 0;
  for (const auto& e : elements) sources += e.genre == 'V';
  const int nn = static_cast<int>(noeuds.size()), taille = nn + sources;
  if (taille > 14) return refuser("circuit trop grand pour l'analyse symbolique (" + std::to_string(taille) + " inconnues, 14 au plus)");
  const std::size_t dim = f.symboles.size() + 1;  // s, puis les grandeurs
  std::vector<std::vector<Poly>> A(static_cast<std::size_t>(taille), std::vector<Poly>(static_cast<std::size_t>(taille)));
  std::vector<Poly> b(static_cast<std::size_t>(taille));
  auto monome = [&](int puissanceS, int symbole, int puissance, double coefficient) {
    Exposants e(dim, 0);
    e[0] = puissanceS;
    if (symbole >= 0) e[static_cast<std::size_t>(symbole) + 1] = puissance;
    return Poly{{e, coefficient}};
  };
  auto stamper = [&](int i, int j, const Poly& y) {
    if (i >= 0) ajouter(A[static_cast<std::size_t>(i)][static_cast<std::size_t>(i)], y);
    if (j >= 0) ajouter(A[static_cast<std::size_t>(j)][static_cast<std::size_t>(j)], y);
    if (i >= 0 && j >= 0) {
      ajouter(A[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)], y, -1);
      ajouter(A[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)], y, -1);
    }
  };
  int k = nn;
  for (const auto& e : elements) {
    const int i = indice(e.a), j = indice(e.b);
    if (e.genre == 'R') stamper(i, j, e.symbole >= 0 ? monome(0, e.symbole, -1, 1) : monome(0, -1, 0, 1 / e.valeur));
    else if (e.genre == 'C') stamper(i, j, e.symbole >= 0 ? monome(1, e.symbole, 1, 1) : monome(1, -1, 0, e.valeur));
    else if (e.genre == 'L') stamper(i, j, e.symbole >= 0 ? monome(-1, e.symbole, -1, 1) : monome(-1, -1, 0, 1 / e.valeur));
    else if (e.genre == 'V') {
      const Poly un = monome(0, -1, 0, 1), moinsUn = monome(0, -1, 0, -1);
      if (i >= 0) ajouter(A[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)], un), ajouter(A[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)], un);
      if (j >= 0) ajouter(A[static_cast<std::size_t>(j)][static_cast<std::size_t>(k)], moinsUn), ajouter(A[static_cast<std::size_t>(k)][static_cast<std::size_t>(j)], moinsUn);
      if (e.entree) b[static_cast<std::size_t>(k)] = un;
      ++k;
    }
  }
  // Déterminant par développement sur les lignes, mémorisé par colonnes restantes
  auto determinant = [&](const std::vector<std::vector<Poly>>& M) {
    std::map<std::uint32_t, Poly> memo;
    const int t = static_cast<int>(M.size());
    std::function<Poly(int, std::uint32_t)> det = [&](int ligne, std::uint32_t libres) -> Poly {
      if (ligne == t) return monome(0, -1, 0, 1);
      if (const auto it = memo.find(libres); it != memo.end()) return it->second;
      Poly r;
      int signe = 1;
      for (int col = 0; col < t; ++col) {
        if (!(libres & (1u << col))) continue;
        const Poly& x = M[static_cast<std::size_t>(ligne)][static_cast<std::size_t>(col)];
        if (!x.empty()) ajouter(r, produit(x, det(ligne + 1, libres & ~(1u << col))), signe);
        signe = -signe;
      }
      memo[libres] = r;
      return r;
    };
    return det(0, (1u << t) - 1);
  };
  const Poly D = determinant(A);
  auto avecColonne = [&](int col) {
    auto M = A;
    for (int i = 0; i < taille; ++i) M[static_cast<std::size_t>(i)][static_cast<std::size_t>(col)] = b[static_cast<std::size_t>(i)];
    return determinant(M);
  };
  Poly N = avecColonne(indice(ns));
  if (D.empty()) return refuser("circuit singulier : le déterminant est nul");
  // Normalisation : puissances de s ramenées à 0, terme constant du dénominateur ramené à 1
  int minS = 1 << 20;
  for (const auto& [e, c2] : D) minS = std::min(minS, e[0]);
  for (const auto& [e, c2] : N) minS = std::min(minS, e[0]);
  auto decaler = [&](const Poly& p, const Exposants& par, double diviseur) {
    Poly r;
    for (const auto& [e, c2] : p) {
      Exposants x = e;
      for (std::size_t i = 0; i < x.size(); ++i) x[i] -= par[i];
      r[x] = c2 / diviseur;
    }
    return r;
  };
  Exposants decalage(dim, 0);
  decalage[0] = minS;
  Poly Dn = decaler(D, decalage, 1), Nn = decaler(N, decalage, 1);
  // Le plus petit degré en s du dénominateur : son premier monôme devient 1
  int degre0 = 1 << 20;
  for (const auto& [e, c2] : Dn) degre0 = std::min(degre0, e[0]);
  const auto premier = std::find_if(Dn.begin(), Dn.end(), [&](const auto& x) { return x.first[0] == degre0; });
  const Exposants m0 = premier->first;
  const double c0 = premier->second;
  Dn = decaler(Dn, m0, c0);
  Nn = decaler(Nn, m0, c0);
  auto versS = [&](const Poly& p) {
    PolynomeS r;
    for (const auto& [e, c2] : p) {
      Monome m;
      m.coefficient = c2;
      m.exposants.assign(e.begin() + 1, e.end());
      r[e[0]].push_back(m);
    }
    return r;
  };
  f.numerateur = versS(Nn);
  f.denominateur = versS(Dn);
  return f;
}

namespace {
std::string nombreCourt(double v) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(6);
  o << v;
  return o.str();
}
// Monôme écrit : « R1·C1 », « R2/R1 », « 2·R1 » ; « latex » : « R_{1} C_{1} », « \frac{R_{2}}{R_{1}} »
std::string ecrireMonome(const Monome& m, const std::vector<std::string>& symboles, bool latex, bool* moins) {
  auto nom = [&](const std::string& s) {
    if (!latex) return s;
    std::size_t i = 0;
    while (i < s.size() && !std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    return i < s.size() ? s.substr(0, i) + "_{" + s.substr(i) + "}" : s;
  };
  std::vector<std::string> haut, bas;
  for (std::size_t i = 0; i < m.exposants.size() && i < symboles.size(); ++i) {
    const int e = m.exposants[i];
    if (e == 0) continue;
    std::string x = nom(symboles[i]);
    if (std::abs(e) > 1) x += (latex ? "^{" : "^") + std::to_string(std::abs(e)) + (latex ? "}" : "");
    (e > 0 ? haut : bas).push_back(x);
  }
  const double c = std::abs(m.coefficient);
  *moins = m.coefficient < 0;
  const std::string sep = latex ? " " : "·";
  std::string h;
  if (std::abs(c - 1) > 1e-12 || haut.empty()) h = nombreCourt(c);
  for (const auto& x : haut) h += (h.empty() ? "" : sep) + x;
  if (bas.empty()) return h;
  std::string b;
  for (const auto& x : bas) b += (b.empty() ? "" : sep) + x;
  if (latex) return "\\frac{" + h + "}{" + b + "}";
  return h + "/" + (bas.size() > 1 ? "(" + b + ")" : b);
}
std::string ecrirePolynome(const PolynomeS& p, const std::vector<std::string>& symboles, bool latex) {
  std::string t;
  for (const auto& [puissance, monomes] : p) {
    std::string groupe;
    bool premierMoins = false;
    for (std::size_t i = 0; i < monomes.size(); ++i) {
      bool moins = false;
      const std::string x = ecrireMonome(monomes[i], symboles, latex, &moins);
      if (i == 0) {
        groupe = x;
        premierMoins = moins;
      } else
        groupe += (moins ? " - " : " + ") + x;
    }
    std::string sPuissance;
    if (puissance == 1) sPuissance = "s";
    else if (puissance != 0) sPuissance = latex ? "s^{" + std::to_string(puissance) + "}" : "s^" + std::to_string(puissance);
    std::string terme;
    if (sPuissance.empty()) terme = monomes.size() > 1 ? "(" + groupe + ")" : groupe;
    else if (monomes.size() == 1 && groupe == "1") terme = sPuissance;
    else terme = sPuissance + (latex ? " " : "·") + (monomes.size() > 1 ? "(" + groupe + ")" : groupe);
    if (t.empty()) t = (premierMoins && monomes.size() == 1 ? "-" : "") + terme;
    else t += (premierMoins && monomes.size() == 1 ? " - " : " + ") + terme;
  }
  return t.empty() ? std::string("0") : t;
}
}  // namespace

std::string FonctionSymbolique::texte() const {
  const std::string n = ecrirePolynome(numerateur, symboles, false), d = ecrirePolynome(denominateur, symboles, false);
  if (d == "1") return n;
  const bool plusieursN = numerateur.size() > 1 || (numerateur.size() == 1 && numerateur.begin()->second.size() > 1);
  return (plusieursN ? "(" + n + ")" : n) + " / (" + d + ")";
}

std::string FonctionSymbolique::latex() const {
  const std::string n = ecrirePolynome(numerateur, symboles, true), d = ecrirePolynome(denominateur, symboles, true);
  return d == "1" ? n : "\\frac{" + n + "}{" + d + "}";
}

std::vector<double> FonctionSymbolique::coefficients(bool num, const std::map<std::string, double>& valeurs) const {
  const PolynomeS& p = num ? numerateur : denominateur;
  int degre = 0;
  for (const auto& [k, m] : p) degre = std::max(degre, k);
  std::vector<double> c(static_cast<std::size_t>(degre) + 1, 0);
  for (const auto& [k, monomes] : p)
    for (const auto& m : monomes) {
      double v = m.coefficient;
      for (std::size_t i = 0; i < m.exposants.size() && i < symboles.size(); ++i)
        if (m.exposants[i]) {
          const auto it = valeurs.find(symboles[i]);
          v *= std::pow(it == valeurs.end() ? 1.0 : it->second, m.exposants[i]);
        }
      if (k >= 0) c[static_cast<std::size_t>(k)] += v;
    }
  return c;
}

std::vector<std::complex<double>> FonctionSymbolique::poles(const std::map<std::string, double>& valeurs) const { return racines(coefficients(false, valeurs)); }
std::vector<std::complex<double>> FonctionSymbolique::zeros(const std::map<std::string, double>& valeurs) const { return racines(coefficients(true, valeurs)); }

std::complex<double> FonctionSymbolique::evaluer(std::complex<double> s, const std::map<std::string, double>& valeurs) const {
  auto poly = [&](const std::vector<double>& c) {
    std::complex<double> v = 0;
    for (std::size_t i = c.size(); i-- > 0;) v = v * s + c[i];
    return v;
  };
  return poly(coefficients(true, valeurs)) / poly(coefficients(false, valeurs));
}

std::vector<std::complex<double>> racines(const std::vector<double>& c0) {
  std::vector<double> c = c0;
  while (!c.empty() && std::abs(c.back()) < 1e-300) c.pop_back();
  const int n = static_cast<int>(c.size()) - 1;
  if (n < 1) return {};
  // Polynôme unitaire, racines de départ sur un cercle de rayon adapté (borne de Cauchy)
  std::vector<std::complex<double>> a(static_cast<std::size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) a[static_cast<std::size_t>(i)] = c[static_cast<std::size_t>(i)] / c[static_cast<std::size_t>(n)];
  double rayon = 0;
  for (int i = 0; i < n; ++i) rayon = std::max(rayon, std::abs(a[static_cast<std::size_t>(i)]));
  rayon = 1 + rayon;
  // Petits coefficients : un rayon d'après les racines probables (|a0|^(1/n))
  const double probable = std::pow(std::abs(a[0]), 1.0 / n);
  if (probable > 0 && probable < rayon) rayon = probable;
  std::vector<std::complex<double>> z(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) z[static_cast<std::size_t>(i)] = std::polar(rayon, 2 * std::numbers::pi * (i + 0.25) / n);
  auto valeur = [&](std::complex<double> x) {
    std::complex<double> v = 1;
    for (int i = n - 1; i >= 0; --i) v = v * x + a[static_cast<std::size_t>(i)];
    return v;
  };
  for (int it = 0; it < 2000; ++it) {
    double change = 0;
    for (int i = 0; i < n; ++i) {
      std::complex<double> d = 1;
      for (int j = 0; j < n; ++j)
        if (j != i) d *= z[static_cast<std::size_t>(i)] - z[static_cast<std::size_t>(j)];
      if (std::abs(d) == 0) d = 1e-12;
      const std::complex<double> pas = valeur(z[static_cast<std::size_t>(i)]) / d;
      z[static_cast<std::size_t>(i)] -= pas;
      change = std::max(change, std::abs(pas) / std::max(1e-300, std::abs(z[static_cast<std::size_t>(i)])));
    }
    if (change < 1e-15) break;
  }
  // Affinage par Newton, et parties imaginaires infimes ramenées à 0
  for (auto& x : z) {
    for (int it = 0; it < 20; ++it) {
      std::complex<double> v = 0, d = 0;
      for (int i = n; i >= 0; --i) {
        d = d * x + v;
        v = v * x + a[static_cast<std::size_t>(i)];
      }
      if (std::abs(d) == 0) break;
      x -= v / d;
    }
    if (std::abs(x.imag()) < 1e-9 * std::max(1.0, std::abs(x.real()))) x = {x.real(), 0};
  }
  std::sort(z.begin(), z.end(), [](const auto& p, const auto& q) { return std::abs(p) < std::abs(q); });
  return z;
}

}  // namespace prom::ecad
