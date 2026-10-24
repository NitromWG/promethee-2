// Prométhée : langage d'expressions sur les propriétés des objets (13.11) — analyse descendante récursive en arbre,
// évaluation sans allocation. Licence GPL-3.0-only.
#include "promethee/expressions.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <set>

#include "promethee/circuit.hpp"

namespace prom::ecad {

struct Expression::Noeud {
  enum class Genre { Vrai, Ou, Et, Non, Comparaison, Fonction, Identifiant, Texte, Nombre } genre = Genre::Vrai;
  std::string op;    // comparaison : « == », « != », « < », « <= », « > », « >= », « ~ » ; fonction : son nom
  std::string nom;   // identifiant ; texte
  double nombre = 0;
  std::vector<std::shared_ptr<const Noeud>> enfants;
};

namespace {
using N = Expression::Noeud;
using PN = std::shared_ptr<const N>;

std::string minuscules(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

class Analyseur {
 public:
  explicit Analyseur(const std::string& t) : m_t(t) {}
  PN analyser() {
    espaces();
    if (m_i >= m_t.size()) {
      auto n = std::make_shared<N>();
      n->genre = N::Genre::Vrai;
      return n;
    }
    PN r = ou();
    espaces();
    if (m_i < m_t.size()) erreur("texte en trop après l'expression");
    return r;
  }
  std::string m_erreur;

 private:
  const std::string& m_t;
  std::size_t m_i = 0;
  void erreur(const std::string& e) {
    if (m_erreur.empty()) m_erreur = e + " (position " + std::to_string(m_i + 1) + ")";
    m_i = m_t.size();
  }
  void espaces() {
    while (m_i < m_t.size() && std::isspace(static_cast<unsigned char>(m_t[m_i]))) ++m_i;
  }
  bool prendre(const std::string& s) {
    espaces();
    if (m_t.compare(m_i, s.size(), s) == 0) {
      m_i += s.size();
      return true;
    }
    return false;
  }
  bool motCle(const char* m) {
    espaces();
    const std::size_t n = std::char_traits<char>::length(m);
    if (m_i + n <= m_t.size() && minuscules(m_t.substr(m_i, n)) == m && (m_i + n == m_t.size() || !std::isalnum(static_cast<unsigned char>(m_t[m_i + n])))) {
      m_i += n;
      return true;
    }
    return false;
  }
  PN binaire(N::Genre g, PN a, PN b) {
    auto n = std::make_shared<N>();
    n->genre = g;
    n->enfants = {std::move(a), std::move(b)};
    return n;
  }
  PN ou() {
    PN a = et();
    while (prendre("||") || motCle("ou") || motCle("or")) a = binaire(N::Genre::Ou, a, et());
    return a;
  }
  PN et() {
    PN a = non();
    while (prendre("&&") || motCle("et") || motCle("and")) a = binaire(N::Genre::Et, a, non());
    return a;
  }
  PN non() {
    espaces();
    if ((m_i < m_t.size() && m_t[m_i] == '!' && (m_i + 1 >= m_t.size() || m_t[m_i + 1] != '=')) || motCle("non") || motCle("not")) {
      if (m_t[m_i] == '!') ++m_i;
      auto n = std::make_shared<N>();
      n->genre = N::Genre::Non;
      n->enfants = {non()};
      return n;
    }
    return comparaison();
  }
  PN comparaison() {
    PN a = terme();
    espaces();
    for (const char* op : {"==", "!=", "<=", ">=", "<", ">", "~", "="}) {
      if (prendre(op)) {
        auto n = std::make_shared<N>();
        n->genre = N::Genre::Comparaison;
        n->op = std::string(op) == "=" ? "==" : op;
        n->enfants = {a, terme()};
        return n;
      }
    }
    return a;
  }
  PN terme() {
    espaces();
    if (m_i >= m_t.size()) {
      erreur("expression incomplète");
      return std::make_shared<N>();
    }
    const char c = m_t[m_i];
    if (c == '(') {
      ++m_i;
      PN r = ou();
      if (!prendre(")")) erreur("parenthèse fermante attendue");
      return r;
    }
    if (c == '\'' || c == '"') {
      const char q = c;
      ++m_i;
      std::string s;
      while (m_i < m_t.size() && m_t[m_i] != q) s += m_t[m_i++];
      if (m_i >= m_t.size()) {
        erreur("guillemet fermant attendu");
        return std::make_shared<N>();
      }
      ++m_i;
      auto n = std::make_shared<N>();
      n->genre = N::Genre::Texte;
      n->nom = s;
      return n;
    }
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.') {
      std::size_t j = m_i + 1;
      while (j < m_t.size() && (std::isalnum(static_cast<unsigned char>(m_t[j])) || m_t[j] == '.' || m_t[j] == ',' || m_t[j] == '%' || static_cast<unsigned char>(m_t[j]) >= 0x80)) ++j;
      // Unité séparée du nombre par une espace (« 0,2 mm ») : rattachée au nombre quand elle est connue
      if (std::none_of(m_t.begin() + static_cast<std::ptrdiff_t>(m_i), m_t.begin() + static_cast<std::ptrdiff_t>(j), [](char x) { return std::isalpha(static_cast<unsigned char>(x)) || static_cast<unsigned char>(x) >= 0x80 || x == '%'; })) {
        std::size_t k = j;
        while (k < m_t.size() && m_t[k] == ' ') ++k;
        std::size_t f = k;
        while (f < m_t.size() && (std::isalpha(static_cast<unsigned char>(m_t[f])) || static_cast<unsigned char>(m_t[f]) >= 0x80 || m_t[f] == '%')) ++f;
        double essai = 0;
        if (f > k && k > j && (f == m_t.size() || !std::isalnum(static_cast<unsigned char>(m_t[f]))) && lireNombreUnite(m_t.substr(m_i, j - m_i) + m_t.substr(k, f - k), essai) &&
            m_t.substr(k, f - k) != "et" && m_t.substr(k, f - k) != "ou")
          j = f;
      }
      double v = 0;
      if (!lireNombreUnite(m_t.substr(m_i, j - m_i), v)) {
        erreur("nombre illisible « " + m_t.substr(m_i, j - m_i) + " »");
        return std::make_shared<N>();
      }
      m_i = j;
      auto n = std::make_shared<N>();
      n->genre = N::Genre::Nombre;
      n->nombre = v;
      return n;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80) {
      std::size_t j = m_i;
      while (j < m_t.size() && (std::isalnum(static_cast<unsigned char>(m_t[j])) || m_t[j] == '_' || m_t[j] == '.' || static_cast<unsigned char>(m_t[j]) >= 0x80)) ++j;
      std::string nom = m_t.substr(m_i, j - m_i);
      m_i = j;
      espaces();
      if (m_i < m_t.size() && m_t[m_i] == '(') {
        ++m_i;
        auto n = std::make_shared<N>();
        n->genre = N::Genre::Fonction;
        n->op = minuscules(nom);
        espaces();
        if (!prendre(")")) {
          do n->enfants.push_back(terme());
          while (prendre(","));
          if (!prendre(")")) erreur("parenthèse fermante attendue après les arguments de " + nom);
        }
        static const std::set<std::string> connues{"dans", "commence", "finit", "contient", "existe"};
        if (!connues.count(n->op)) erreur("fonction inconnue « " + nom + " »");
        return n;
      }
      auto n = std::make_shared<N>();
      if (minuscules(nom) == "vrai" || minuscules(nom) == "true") {
        n->genre = N::Genre::Vrai;
        return n;
      }
      n->genre = N::Genre::Identifiant;
      n->nom = minuscules(nom);
      return n;
    }
    erreur(std::string("caractère inattendu « ") + c + " »");
    return std::make_shared<N>();
  }
};

std::optional<ValeurExpression> valeur(const N& n, const Proprietes& p) {
  switch (n.genre) {
    case N::Genre::Texte: return ValeurExpression{n.nom};
    case N::Genre::Nombre: return ValeurExpression{n.nombre};
    case N::Genre::Identifiant: return p(n.nom);
    default: return std::nullopt;
  }
}
bool enNombre(const ValeurExpression& v, double& x) {
  if (const double* d = std::get_if<double>(&v)) {
    x = *d;
    return true;
  }
  return lireNombreUnite(std::get<std::string>(v), x);
}
std::string enTexte(const ValeurExpression& v) {
  if (const std::string* s = std::get_if<std::string>(&v)) return *s;
  const double d = std::get<double>(v);
  char t[48];
  std::snprintf(t, sizeof t, "%g", d);
  return t;
}
bool evaluer(const N& n, const Proprietes& p) {
  switch (n.genre) {
    case N::Genre::Vrai: return true;
    case N::Genre::Ou: return evaluer(*n.enfants[0], p) || evaluer(*n.enfants[1], p);
    case N::Genre::Et: return evaluer(*n.enfants[0], p) && evaluer(*n.enfants[1], p);
    case N::Genre::Non: return !evaluer(*n.enfants[0], p);
    case N::Genre::Comparaison: {
      const auto a = valeur(*n.enfants[0], p), b = valeur(*n.enfants[1], p);
      if (!a || !b) return n.op == "!=" && (a.has_value() != b.has_value());
      if (n.op == "~") return correspond(enTexte(*b), enTexte(*a));
      double x = 0, y = 0;
      const bool nombres = enNombre(*a, x) && enNombre(*b, y);
      if (n.op == "==") return nombres ? std::abs(x - y) <= 1e-9 * std::max(1.0, std::abs(y)) : enTexte(*a) == enTexte(*b);
      if (n.op == "!=") return nombres ? std::abs(x - y) > 1e-9 * std::max(1.0, std::abs(y)) : enTexte(*a) != enTexte(*b);
      if (!nombres) {
        const std::string s = enTexte(*a), t = enTexte(*b);
        return n.op == "<" ? s < t : n.op == "<=" ? s <= t : n.op == ">" ? s > t : s >= t;
      }
      return n.op == "<" ? x < y : n.op == "<=" ? x <= y : n.op == ">" ? x > y : x >= y;
    }
    case N::Genre::Fonction: {
      if (n.enfants.empty()) return false;
      const auto a = valeur(*n.enfants[0], p);
      if (n.op == "existe") return a.has_value();
      if (!a) return false;
      const std::string s = enTexte(*a);
      for (std::size_t i = 1; i < n.enfants.size(); ++i) {
        const auto b = valeur(*n.enfants[i], p);
        if (!b) continue;
        const std::string t = enTexte(*b);
        if (n.op == "dans" && (s == t || correspond(t, s))) return true;
        if (n.op == "commence" && s.rfind(t, 0) == 0) return true;
        if (n.op == "finit" && s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0) return true;
        if (n.op == "contient" && s.find(t) != std::string::npos) return true;
      }
      return false;
    }
    case N::Genre::Identifiant: {
      // Un identifiant seul : vrai s'il existe et n'est ni vide, ni nul, ni « non »
      const auto a = p(n.nom);
      if (!a) return false;
      if (const double* d = std::get_if<double>(&*a)) return *d != 0;
      const std::string s = minuscules(std::get<std::string>(*a));
      return !s.empty() && s != "0" && s != "non" && s != "faux" && s != "false";
    }
    case N::Genre::Texte: return !n.nom.empty();
    case N::Genre::Nombre: return n.nombre != 0;
  }
  return false;
}
void collecter(const N& n, std::set<std::string>& r) {
  if (n.genre == N::Genre::Identifiant) r.insert(n.nom);
  for (const auto& e : n.enfants) collecter(*e, r);
}
}  // namespace

bool lireNombreUnite(const std::string& brut, double& v) {
  std::string t;
  for (char ch : brut)
    if (!std::isspace(static_cast<unsigned char>(ch))) t += ch;
  std::replace(t.begin(), t.end(), ',', '.');
  if (t.empty()) return false;
  char* fin = nullptr;
  const double x = std::strtod(t.c_str(), &fin);
  if (fin == t.c_str() || !std::isfinite(x)) return false;
  const std::string u(fin);
  static const std::pair<const char*, double> unites[] = {{"", 1},       {"mm", 1e6},   {"um", 1e3}, {"µm", 1e3},     {"nm", 1},     {"mil", 25'400}, {"in", 25.4e6}, {"cm", 1e7},
                                                          {"m", 1e9},    {"V", 1},      {"mV", 1e-3}, {"kV", 1e3},     {"A", 1},      {"mA", 1e-3},    {"ps", 1},      {"ns", 1e3},
                                                          {"deg", 1},    {"°", 1},      {"%", 0.01}, {"ohm", 1},      {"Ω", 1},      {"k", 1e3},      {"M", 1e6},     {"pF", 1e-12},
                                                          {"nF", 1e-9},  {"uF", 1e-6},  {"µF", 1e-6}, {"W", 1},        {"Hz", 1},     {"kHz", 1e3},    {"MHz", 1e6}};
  for (const auto& [nom, f] : unites)
    if (u == nom) {
      v = x * f;
      return true;
    }
  return false;
}

Expression::Expression() = default;
Expression::~Expression() = default;
Expression::Expression(const Expression&) = default;
Expression& Expression::operator=(const Expression&) = default;
Expression::Expression(Expression&&) noexcept = default;
Expression& Expression::operator=(Expression&&) noexcept = default;

Expression Expression::compiler(const std::string& texte) {
  Expression e;
  e.m_texte = texte;
  Analyseur a(texte);
  e.m_racine = a.analyser();
  e.m_erreur = a.m_erreur;
  return e;
}

std::vector<std::string> Expression::identifiants() const {
  std::set<std::string> r;
  if (m_racine) collecter(*m_racine, r);
  return {r.begin(), r.end()};
}

bool Expression::evaluer(const Proprietes& p) const {
  if (!valide() || !m_racine) return false;
  return ::prom::ecad::evaluer(*m_racine, p);
}

}  // namespace prom::ecad
