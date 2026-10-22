// Prométhée : unités et saisie des valeurs. Licence GPL-3.0-only.
#include "promethee/unites.hpp"

#include <cctype>
#include <cmath>
#include <map>
#include <cstdio>
#include <numbers>

namespace prom {

const char* symbole(UniteLongueur u) {
  switch (u) {
    case UniteLongueur::Centimetre: return "cm";
    case UniteLongueur::Metre: return "m";
    case UniteLongueur::Pouce: return "in";
    case UniteLongueur::Pied: return "ft";
    case UniteLongueur::Mil: return "mil";
    case UniteLongueur::Millimetre: break;
  }
  return "mm";
}
const char* symbole(UniteAngle u) { return u == UniteAngle::Radian ? "rad" : "°"; }

namespace {
double facteur(UniteLongueur u) {
  switch (u) {
    case UniteLongueur::Centimetre: return 10;
    case UniteLongueur::Metre: return 1000;
    case UniteLongueur::Pouce: return 25.4;
    case UniteLongueur::Pied: return 304.8;
    case UniteLongueur::Mil: return 0.0254;
    case UniteLongueur::Millimetre: break;
  }
  return 1;
}

// Analyse descendante : expr := terme (('+'|'-') terme)* ; terme := facteur (('*'|'/') facteur)* ;
// facteur := ['-'] (nombre [unité] | variable | '(' expr ')' [unité]). Chaque valeur porte sa dimension :
// 0 nombre pur, 1 longueur (mm), 2 angle (degrés). Une unité ne s'applique qu'à une valeur sans dimension :
// « (10 mm + 5) cm » est refusé (ES-32). Une fraction suivie d'une unité est une fraction de cette unité :
// « 1/4 in » vaut 6,35 mm, « 40/2 mm » vaut 20 mm quel que soit le document (ES-14).
struct Lecteur {
  const std::string& s;
  size_t i = 0;
  Unites doc;
  std::string erreur;
  const std::map<std::string, Grandeur>* variables = nullptr;
  bool souples = false;  // variables sans dimension connue
  struct V {
    double v = 0;
    int dim = 0;
    double brut = 0;      // valeur avant l'unité qui la suit (pour « 1/4 in »)
    double unite = 1;     // facteur de cette unité
    bool uniteSeule = false;  // la dimension vient d'une unité écrite juste après un nombre
    bool souple = false;      // variable donnée sans sa dimension (ancienne interface) : elle s'adapte
  };
  void blancs() {
    for (;;) {
      if (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) { ++i; continue; }
      // Espaces insécables (U+00A0, U+202F) que collent les claviers français (ES-31)
      if (s.compare(i, 2, "\xC2\xA0") == 0) { i += 2; continue; }
      if (s.compare(i, 3, "\xE2\x80\xAF") == 0) { i += 3; continue; }
      return;
    }
  }
  bool unite(V& x) {
    blancs();
    auto mot = [&](const char* m) {
      const size_t n = std::char_traits<char>::length(m);
      if (s.compare(i, n, m) != 0) return false;
      if (i + n < s.size() && std::isalpha(static_cast<unsigned char>(s[i + n]))) return false;
      i += n;
      return true;
    };
    double f = 0;
    int dim = 0;
    if (mot("mm")) { f = 1; dim = 1; }
    else if (mot("mil") || mot("thou")) { f = 0.0254; dim = 1; }
    else if (mot("cm")) { f = 10; dim = 1; }
    else if (mot("m")) { f = 1000; dim = 1; }
    else if (mot("in") || mot("\"")) { f = 25.4; dim = 1; }
    else if (mot("ft") || mot("'")) { f = 304.8; dim = 1; }
    else if (mot("deg") || mot("°")) { f = 1; dim = 2; }
    else if (mot("rad")) { f = 180 / std::numbers::pi; dim = 2; }
    else return true;  // pas d'unité
    if (x.dim != 0) { erreur = "Unité appliquée à une valeur qui en a déjà une."; return false; }
    x.brut = x.v;
    x.unite = f;
    x.v *= f;
    x.dim = dim;
    x.uniteSeule = true;
    return true;
  }
  bool facteurExpr(V& x) {
    blancs();
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
      const bool moins = s[i] == '-';
      ++i;
      if (!facteurExpr(x)) return false;
      if (moins) { x.v = -x.v; x.brut = -x.brut; }
      return true;
    }
    if (i < s.size() && s[i] == '(') {
      ++i;
      if (!expr(x)) return false;
      blancs();
      if (i >= s.size() || s[i] != ')') { erreur = "Parenthèse fermante manquante."; return false; }
      ++i;
      x.uniteSeule = false;
      return unite(x);
    }
    // Variable de la pièce, avec sa dimension
    if (i < s.size() && (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
      const size_t d0 = i;
      while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
      const std::string nom = s.substr(d0, i - d0);
      if (!variables || !variables->count(nom)) { erreur = "Variable inconnue : « " + nom + " »."; return false; }
      const Grandeur& g = variables->at(nom);
      x = V{g.valeur, g.dimension, g.valeur, 1, false, souples};
      return true;
    }
    const size_t debut = i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == ',')) ++i;
    if (i == debut) { erreur = "Nombre attendu."; return false; }
    std::string n = s.substr(debut, i - debut);
    for (char& c : n) if (c == ',') c = '.';
    char* fin = nullptr;
    x = V{};
    x.v = std::strtod(n.c_str(), &fin);
    if (!fin || *fin != '\0') { erreur = "Nombre illisible : " + n; return false; }
    return unite(x);
  }
  bool terme(V& x) {
    if (!facteurExpr(x)) return false;
    for (;;) {
      blancs();
      if (i >= s.size() || (s[i] != '*' && s[i] != '/')) return true;
      const char op = s[i++];
      V y{};
      if (!facteurExpr(y)) return false;
      if (op == '*') {
        if (x.dim && y.dim && (x.souple || y.souple)) {
          if (y.souple) y.dim = 0;
          else x.dim = 0;
        }
        if (x.dim && y.dim) { erreur = "On ne peut pas multiplier deux grandeurs qui ont une unité."; return false; }
        x.v *= y.v;
        x.dim = x.dim ? x.dim : y.dim;
        x.uniteSeule = false;
      } else {
        if (std::abs(y.v) < 1e-300) { erreur = "Division par zéro."; return false; }
        if (x.dim == 0 && y.dim != 0 && y.uniteSeule) {
          // « 1/4 in » : la fraction d'une unité
          x.v = x.v / y.brut * y.unite;
          x.dim = y.dim;
        } else if (y.dim == 0) {
          x.v /= y.v;
        } else if (x.dim == y.dim) {
          x.v /= y.v;
          x.dim = 0;  // rapport de deux longueurs : un nombre
        } else {
          erreur = "Division sans dimension cohérente.";
          return false;
        }
        x.uniteSeule = false;
      }
    }
  }
  bool expr(V& x) {
    if (!terme(x)) return false;
    for (;;) {
      blancs();
      if (i >= s.size() || (s[i] != '+' && s[i] != '-')) return true;
      const char op = s[i++];
      V y{};
      if (!terme(y)) return false;
      if (x.dim && y.dim && x.dim != y.dim) { erreur = "On ne peut pas additionner une longueur et un angle."; return false; }
      // Un terme sans unité ajouté à une grandeur prend l'unité du document
      auto convertir = [&](V& z, int dim) {
        if (z.dim) return;
        z.v = dim == 2 ? versDegres(z.v, doc.angle) : versMillimetres(z.v, doc.longueur);
        z.dim = dim;
      };
      if (x.dim != y.dim) {
        const int dim = x.dim ? x.dim : y.dim;
        convertir(x, dim);
        convertir(y, dim);
      }
      x.v = op == '+' ? x.v + y.v : x.v - y.v;
      x.uniteSeule = false;
    }
  }
};
}  // namespace

double versMillimetres(double v, UniteLongueur u) { return v * facteur(u); }
double depuisMillimetres(double mm, UniteLongueur u) { return mm / facteur(u); }
double versDegres(double v, UniteAngle u) { return u == UniteAngle::Radian ? v * 180 / std::numbers::pi : v; }
double depuisDegres(double deg, UniteAngle u) { return u == UniteAngle::Radian ? deg * std::numbers::pi / 180 : deg; }

UniteLongueur uniteLongueurDepuis(const std::string& s) {
  if (s == "cm") return UniteLongueur::Centimetre;
  if (s == "m") return UniteLongueur::Metre;
  if (s == "in") return UniteLongueur::Pouce;
  if (s == "ft") return UniteLongueur::Pied;
  if (s == "mil") return UniteLongueur::Mil;
  return UniteLongueur::Millimetre;
}
UniteAngle uniteAngleDepuis(const std::string& s) { return s == "rad" ? UniteAngle::Radian : UniteAngle::Degre; }

bool lireGrandeur(const std::string& texte, const Unites& document, Grandeur& resultat, std::string* erreur, const std::map<std::string, Grandeur>* variables) {
  Lecteur l{texte, 0, document, {}};
  l.variables = variables;
  Lecteur::V x{};
  if (!l.expr(x)) { if (erreur) *erreur = l.erreur; return false; }
  l.blancs();
  if (l.i < texte.size()) { if (erreur) *erreur = "Caractère inattendu : « " + texte.substr(l.i) + " »."; return false; }
  if (!std::isfinite(x.v)) { if (erreur) *erreur = "Valeur hors limites."; return false; }
  resultat = {x.v, x.dim};
  return true;
}

bool lireValeur(const std::string& texte, bool angle, const Unites& document, double& resultat, std::string* erreur, const std::map<std::string, Grandeur>* variables) {
  Grandeur g;
  if (!lireGrandeur(texte, document, g, erreur, variables)) return false;
  if (g.dimension == 2 && !angle) { if (erreur) *erreur = "Une longueur est attendue, pas un angle."; return false; }
  if (g.dimension == 1 && angle) { if (erreur) *erreur = "Un angle est attendu, pas une longueur."; return false; }
  if (g.dimension == 0) g.valeur = angle ? versDegres(g.valeur, document.angle) : versMillimetres(g.valeur, document.longueur);
  if (!std::isfinite(g.valeur)) { if (erreur) *erreur = "Valeur hors limites."; return false; }
  resultat = g.valeur;
  return true;
}

bool lireValeur(const std::string& texte, bool angle, const Unites& document, double& resultat, std::string* erreur, const std::map<std::string, double>* variables) {
  if (!variables) return lireValeur(texte, angle, document, resultat, erreur, static_cast<const std::map<std::string, Grandeur>*>(nullptr));
  // Variables données en nombres seuls (mm ou degrés) : elles prennent la dimension du champ
  std::map<std::string, Grandeur> g;
  for (const auto& [n, v] : *variables) g[n] = {v, angle ? 2 : 1};
  Lecteur l{texte, 0, document, {}};
  l.variables = &g;
  l.souples = true;
  Lecteur::V x{};
  if (!l.expr(x)) { if (erreur) *erreur = l.erreur; return false; }
  l.blancs();
  if (l.i < texte.size()) { if (erreur) *erreur = "Caractère inattendu : « " + texte.substr(l.i) + " »."; return false; }
  if (x.dim == 2 && !angle) { if (erreur) *erreur = "Une longueur est attendue, pas un angle."; return false; }
  if (x.dim == 1 && angle) { if (erreur) *erreur = "Un angle est attendu, pas une longueur."; return false; }
  if (x.dim == 0) x.v = angle ? versDegres(x.v, document.angle) : versMillimetres(x.v, document.longueur);
  if (!std::isfinite(x.v)) { if (erreur) *erreur = "Valeur hors limites."; return false; }
  resultat = x.v;
  return true;
}

std::string ecrireValeur(double interne, bool angle, const Unites& document, bool avecSymbole) {
  const double v = angle ? depuisDegres(interne, document.angle) : depuisMillimetres(interne, document.longueur);
  char t[64];
  // Assez de décimales pour être exact, sans zéros inutiles.
  std::snprintf(t, sizeof t, "%.6f", v);
  std::string s = t;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s == "-0") s = "0";
  for (char& c : s) if (c == '.') c = ',';
  if (avecSymbole) s += angle ? std::string(document.angle == UniteAngle::Radian ? " rad" : "°") : std::string(" ") + symbole(document.longueur);
  return s;
}

std::string ecrireAire(double mm2, const Unites& document) {
  // Une aire se convertit par le carré du facteur de longueur (1 cm² = 100 mm², AP-16)
  const double k = depuisMillimetres(1, document.longueur);
  const double v = mm2 * k * k;
  char t[64];
  std::snprintf(t, sizeof t, "%.6f", v);
  std::string s = t;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  if (s == "-0") s = "0";
  for (char& c : s) if (c == '.') c = ',';
  return s + " " + symbole(document.longueur) + "²";
}

}  // namespace prom
