// Prométhée : valeurs des composants électroniques (lot L3, tâches 3.2 et 3.3). Voir valeurs.hpp.
// Licence GPL-3.0-only.
#include "promethee/valeurs.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>

#include "promethee/electronique.hpp"

namespace prom::ecad {

namespace {
const std::string FINE = " ";
const char OHM = '\x01';  // Ω, après normalisation

// ---------- Texte ----------
void remplacer(std::string& s, const std::string& de, const std::string& par) {
  for (std::size_t i = s.find(de); i != std::string::npos; i = s.find(de, i + par.size())) s.replace(i, de.size(), par);
}
// µ, μ → u ; Ω (deux formes) → OHM ; espaces insécables → espace ; signe moins → « - » ; espaces en trop retirés
std::string normaliser(std::string s) {
  remplacer(s, "µ", "u");
  remplacer(s, "μ", "u");
  remplacer(s, "Ω", std::string(1, OHM));
  remplacer(s, "Ω", std::string(1, OHM));
  remplacer(s, " ", " ");
  remplacer(s, " ", " ");
  remplacer(s, " ", " ");
  remplacer(s, "−", "-");
  remplacer(s, "±", "~");  // ± : un seul octet pour la lecture
  std::string r;
  for (char c : s) {
    if (c == '\t') c = ' ';
    if (c == ' ' && (r.empty() || r.back() == ' ')) continue;
    r += c;
  }
  while (!r.empty() && r.back() == ' ') r.pop_back();
  return r;
}
bool chiffre(char c) { return c >= '0' && c <= '9'; }
bool lettre(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == OHM; }
std::string minuscules(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
// Sans accents ni casse ni espaces aux bords, pour comparer des noms de champs
std::string simple(std::string s) {
  for (const auto& [a, b] : std::vector<std::pair<std::string, std::string>>{{"é", "e"}, {"è", "e"}, {"ê", "e"}, {"É", "e"}, {"à", "a"}, {"â", "a"}, {"ô", "o"}, {"î", "i"}, {"ï", "i"}, {"û", "u"}, {"ç", "c"}})
    remplacer(s, a, b);
  s = minuscules(s);
  while (!s.empty() && (s.back() == ' ' || s.back() == '.' || s.back() == ':')) s.pop_back();
  while (!s.empty() && s.front() == ' ') s.erase(s.begin());
  return s;
}
// Nombre écrit à la française, au plus « chiffres » chiffres significatifs, sans zéros inutiles
std::string nombreFrancais(double v, int chiffres) {
  if (v == 0) return "0";
  char t[64];
  const int ent = static_cast<int>(std::floor(std::log10(std::abs(v)))) + 1;
  const int dec = std::max(0, chiffres - ent);
  std::snprintf(t, sizeof t, "%.*f", dec, v);
  std::string s = t;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  for (char& c : s)
    if (c == '.') c = ',';
  return s;
}
// Arrondi à « chiffres » chiffres significatifs
double arrondiSignificatif(double v, int chiffres) {
  if (v == 0) return 0;
  const double e = std::floor(std::log10(std::abs(v))) - (chiffres - 1);
  const double p = std::pow(10.0, e);
  return std::round(v / p) * p;
}

// ---------- Familles ----------
struct Plage {
  double min, max;
};
Plage plage(FamilleValeur f) {
  switch (f) {
    case FamilleValeur::Resistance: return {1e-3, 1e12};
    case FamilleValeur::Condensateur: return {0.1e-12, 10};
    case FamilleValeur::Inductance: return {1e-9, 10};
    case FamilleValeur::Ferrite: return {1, 10e3};
    case FamilleValeur::Fusible: return {1e-3, 100};
    case FamilleValeur::Quartz: return {1e3, 1e9};
    case FamilleValeur::Zener: return {1.8, 200};
    case FamilleValeur::Pile: return {1, 60};
    case FamilleValeur::Aucune: break;
  }
  return {0, 0};
}
std::string seMesure(FamilleValeur f) {
  switch (f) {
    case FamilleValeur::Resistance: return "une résistance se mesure en ohms";
    case FamilleValeur::Condensateur: return "un condensateur se mesure en farads";
    case FamilleValeur::Inductance: return "une inductance se mesure en henrys";
    case FamilleValeur::Ferrite: return "une perle ferrite se mesure en ohms (à 100 MHz)";
    case FamilleValeur::Fusible: return "un fusible se mesure en ampères";
    case FamilleValeur::Quartz: return "un quartz se mesure en hertz";
    case FamilleValeur::Zener: return "une diode Zener se mesure en volts";
    case FamilleValeur::Pile: return "une pile se mesure en volts";
    case FamilleValeur::Aucune: break;
  }
  return "cette famille n'a pas d'unité";
}

// ---------- Unités et préfixes ----------
enum class Unite : std::uint8_t { Aucune, Ohm, Farad, Henry, Hertz, Volt, Ampere, Watt };
Unite uniteDe(FamilleValeur f) {
  switch (f) {
    case FamilleValeur::Resistance:
    case FamilleValeur::Ferrite: return Unite::Ohm;
    case FamilleValeur::Condensateur: return Unite::Farad;
    case FamilleValeur::Inductance: return Unite::Henry;
    case FamilleValeur::Fusible: return Unite::Ampere;
    case FamilleValeur::Quartz: return Unite::Hertz;
    case FamilleValeur::Zener:
    case FamilleValeur::Pile: return Unite::Volt;
    case FamilleValeur::Aucune: break;
  }
  return Unite::Aucune;
}
// Une suite de lettres : préfixe éventuel puis unité éventuelle (« kΩ », « nF », « MHz », « mA », « k », « F »)
struct Suffixe {
  bool valide = false;
  double facteur = 1;
  bool prefixe = false;
  Unite unite = Unite::Aucune;
};
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
std::optional<Unite> lireUnite(const std::string& u) {
  if (u == std::string(1, OHM) || u == "ohm" || u == "ohms" || u == "Ohm" || u == "Ohms" || u == "R") return Unite::Ohm;
  if (u == "F") return Unite::Farad;
  if (u == "H") return Unite::Henry;
  if (u == "Hz" || u == "hz" || u == "HZ") return Unite::Hertz;
  if (u == "V") return Unite::Volt;
  if (u == "A") return Unite::Ampere;
  if (u == "W") return Unite::Watt;
  return std::nullopt;
}
Suffixe lireSuffixe(const std::string& m) {
  Suffixe r;
  if (m.empty()) {
    r.valide = true;
    return r;
  }
  if (const auto u = lireUnite(m); u && m != "R") {
    r.valide = true;
    r.unite = *u;
    return r;
  }
  if (const auto f = facteurPrefixe(m[0])) {
    if (m.size() == 1) {
      r.valide = true;
      r.facteur = *f;
      r.prefixe = true;
      return r;
    }
    if (const auto u = lireUnite(m.substr(1)); u && m.substr(1) != "R") {
      r.valide = true;
      r.facteur = *f;
      r.prefixe = true;
      r.unite = *u;
    }
  }
  return r;
}
// Lettres RKM (CEI 60062) qui tiennent lieu de virgule
std::optional<double> facteurRkm(char c, FamilleValeur f, bool* microhenry) {
  switch (c) {
    case 'R': return 1.0;
    case 'L': return 1e-3;
    case 'V': return (f == FamilleValeur::Zener || f == FamilleValeur::Pile) ? std::optional<double>(1.0) : std::nullopt;
    default: break;
  }
  if (const auto p = facteurPrefixe(c); p && c != 'f') return p;
  (void)microhenry;
  return std::nullopt;
}

// ---------- Lecture ----------
struct Principal {
  bool ok = false;
  double nombre = 0;
  bool multiplicateur = false;  // un préfixe ou une lettre RKM
  Unite unite = Unite::Aucune;
  bool rkmR = false;            // R comme virgule (« 4R7 »)
  bool zeroFinal = false;
  bool nu = false;              // nombre sans préfixe ni unité
  std::string chiffres;         // le nombre nu tel qu'écrit, pour le code EIA
  std::string erreur;
};

// Lit un nombre décimal à virgule ou point, avec exposant : « 4,7 », « 1e3 », « 2.2E+3 »
bool lireDecimal(const std::string& s, std::size_t& i, double& v, std::string& ecrit) {
  const std::size_t d = i;
  while (i < s.size() && chiffre(s[i])) ++i;
  if (i < s.size() && (s[i] == ',' || s[i] == '.') && i + 1 < s.size() && chiffre(s[i + 1])) {
    ++i;
    while (i < s.size() && chiffre(s[i])) ++i;
  }
  if (i == d) return false;
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    std::size_t j = i + 1;
    if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
    if (j < s.size() && chiffre(s[j])) {
      while (j < s.size() && chiffre(s[j])) ++j;
      i = j;
    }
  }
  ecrit = s.substr(d, i - d);
  std::string n = ecrit;
  for (char& c : n)
    if (c == ',') c = '.';
  v = std::strtod(n.c_str(), nullptr);
  return true;
}

bool lettreDeTolerance(const std::string& t) { return t.size() == 1 && std::string("ELPWBCDFGHJKMNQSTZ").find(t[0]) != std::string::npos; }

Principal lirePrincipal(const std::string& s, std::size_t& i, FamilleValeur f) {
  Principal r;
  if (i < s.size() && s[i] == '-') {
    r.erreur = "une valeur ne peut pas être négative";
    return r;
  }
  // R47, L47 : la lettre d'abord
  if (i + 1 < s.size() && (s[i] == 'R' || s[i] == 'L') && chiffre(s[i + 1])) {
    const double fac = s[i] == 'R' ? 1.0 : 1e-3;
    if (s[i] == 'R') r.rkmR = true;
    ++i;
    const std::size_t d = i;
    while (i < s.size() && chiffre(s[i])) ++i;
    r.nombre = std::strtod(("0." + s.substr(d, i - d)).c_str(), nullptr) * fac;
    r.multiplicateur = true;
    r.ok = true;
  } else if (i < s.size() && chiffre(s[i])) {
    // Chiffres puis une lettre RKM suivie de chiffres : 4k7, 2u2, 4R7, 15M0, 6p8, 5V1
    std::size_t j = i;
    while (j < s.size() && chiffre(s[j])) ++j;
    bool micro = false;
    if (j + 1 < s.size() && chiffre(s[j + 1]) && facteurRkm(s[j], f, &micro)) {
      const double fac = *facteurRkm(s[j], f, &micro);
      if (s[j] == 'R') r.rkmR = true;
      const std::string avant = s.substr(i, j - i);
      std::size_t k = j + 1;
      while (k < s.size() && chiffre(s[k])) ++k;
      const std::string apres = s.substr(j + 1, k - j - 1);
      r.nombre = std::strtod((avant + "." + apres).c_str(), nullptr) * fac;
      r.zeroFinal = !apres.empty() && apres.back() == '0' && apres.find_first_not_of('0') != std::string::npos ? false : (!apres.empty() && apres == std::string(apres.size(), '0'));
      r.multiplicateur = s[j] != 'V';
      if (s[j] == 'V') r.unite = Unite::Volt;
      i = k;
      // Unité collée : « 4k7Ω », « 2u2F »
      std::size_t l = i;
      while (l < s.size() && lettre(s[l])) ++l;
      if (l > i) {
        const std::string m = s.substr(i, l - i);
        const auto u = lireUnite(m);
        if (!u || m == "R") {
          r.erreur = "« " + m + " » n'est pas une unité";
          return r;
        }
        r.unite = *u;
        i = l;
      }
      r.ok = true;
    } else {
      // Nombre décimal, puis préfixe et unité, collés ou après une espace
      std::string ecrit;
      lireDecimal(s, i, r.nombre, ecrit);
      std::size_t j2 = i;
      const bool espace = j2 < s.size() && s[j2] == ' ';
      if (espace) ++j2;
      std::size_t l = j2;
      while (l < s.size() && lettre(s[l])) ++l;
      const std::string m = s.substr(j2, l - j2);
      const bool finMot = l >= s.size() || s[l] == ' ';
      Suffixe x = finMot ? lireSuffixe(m) : Suffixe{};
      // « R » collé (470R, 0R) : des ohms
      if (!espace && m == "R" && finMot) {
        x.valide = true;
        x.unite = Unite::Ohm;
        r.rkmR = true;
      }
      bool prendre = !m.empty() && x.valide;
      if (espace && prendre && m.size() == 1 && lettreDeTolerance(m) && std::string("KMGTF").find(m[0]) == std::string::npos) prendre = false;
      if (!espace && !m.empty() && !x.valide) {
        r.erreur = "« " + m + " » n'est ni un préfixe ni une unité";
        return r;
      }
      if (prendre) {
        r.nombre *= x.facteur;
        r.multiplicateur = x.prefixe || r.rkmR;
        r.unite = x.unite;
        i = l;
      } else {
        r.nu = true;
        r.chiffres = ecrit;
      }
      r.ok = true;
    }
  }
  return r;
}

// Un complément : tension, tolérance, diélectrique, puissance, courant ; faux s'il n'est pas compris
bool lireComplement(const std::string& t, ValeurComprise& v) {
  if (t.empty()) return true;
  // Diélectriques
  const std::string m = minuscules(t);
  static const std::vector<std::pair<std::string, std::string>> dielectriques{{"c0g", "C0G"}, {"cog", "C0G"}, {"np0", "NP0"}, {"npo", "NP0"}, {"x5r", "X5R"},
                                                                             {"x6s", "X6S"}, {"x7r", "X7R"}, {"x7s", "X7S"}, {"y5v", "Y5V"}, {"z5u", "Z5U"}};
  for (const auto& [a, b] : dielectriques)
    if (m == a) {
      v.dielectrique = b;
      return true;
    }
  // Lettre de tolérance
  if (lettreDeTolerance(t)) {
    const char c = t[0];
    if (std::string("QSTZ").find(c) != std::string::npos) {
      v.toleranceAsymetrique = c == 'Q' ? "−10 % +30 %" : c == 'S' ? "−20 % +50 %" : c == 'T' ? "−10 % +50 %" : "−20 % +80 %";
      return true;
    }
    if (v.famille == FamilleValeur::Condensateur && v.nombre < 10e-12 && std::string("CDFG").find(c) != std::string::npos) {
      v.toleranceAbsolue = (c == 'C' ? 0.25 : c == 'D' ? 0.5 : c == 'F' ? 1.0 : 2.0) * 1e-12;
      return true;
    }
    if (const auto p = toleranceDeLettre(c)) {
      v.tolerance = *p;
      return true;
    }
  }
  std::size_t i = 0;
  if (t[0] == '~') ++i;
  if (i < t.size() && t[i] == ' ') ++i;
  // Fraction de watt : 1/4W
  if (const std::size_t barre = t.find('/'); barre != std::string::npos && i == 0) {
    double a = 0, b = 0;
    std::string e1, e2;
    std::size_t k = 0;
    if (lireDecimal(t, k, a, e1) && k == barre) {
      std::size_t k2 = barre + 1;
      if (lireDecimal(t, k2, b, e2) && b != 0) {
        const std::string u = t.substr(k2);
        if (u == "W" || u == " W") {
          v.puissance = a / b;
          return true;
        }
      }
    }
    return false;
  }
  // Tension RKM : 3V3, 6V3
  {
    std::size_t k = i;
    while (k < t.size() && chiffre(t[k])) ++k;
    if (k > i && k + 1 < t.size() && t[k] == 'V' && chiffre(t[k + 1])) {
      std::size_t l = k + 1;
      while (l < t.size() && chiffre(t[l])) ++l;
      if (l == t.size()) {
        v.tension = std::strtod((t.substr(i, k - i) + "." + t.substr(k + 1)).c_str(), nullptr);
        return true;
      }
    }
  }
  double x = 0;
  std::string ecrit;
  std::size_t k = i;
  if (!lireDecimal(t, k, x, ecrit)) return false;
  std::string u = t.substr(k);
  if (!u.empty() && u[0] == ' ') u.erase(0, 1);
  if (u == "%") {
    v.tolerance = x;
    return true;
  }
  double fac = 1;
  if (u.size() >= 2 && facteurPrefixe(u[0]) && lireUnite(u.substr(1))) {
    fac = *facteurPrefixe(u[0]);
    u = u.substr(1);
  }
  if (u == "V") v.tension = x * fac;
  else if (u == "W") v.puissance = x * fac;
  else if (u == "A") v.courant = x * fac;
  else return false;
  return true;
}

std::string propositionEia(const std::string& chiffres, FamilleValeur f) {
  // Trois chiffres : deux significatifs et un exposant, en pF (condensateur) ou en µH (inductance)
  if (chiffres.size() != 3 || chiffres.find_first_not_of("0123456789") != std::string::npos || chiffres[2] > '9') return {};
  const double sig = std::stod(chiffres.substr(0, 2));
  const int ex = chiffres[2] - '0';
  if (ex > 6) return {};
  const double base = f == FamilleValeur::Condensateur ? sig * std::pow(10.0, ex) * 1e-12 : sig * std::pow(10.0, ex) * 1e-7;
  return ecrireSI(base, uniteDeFamille(f)) + " (code " + chiffres + ")";
}
}  // namespace

std::string phraseUnite(FamilleValeur f) { return seMesure(f); }

const char* nomFamille(FamilleValeur f) {
  switch (f) {
    case FamilleValeur::Resistance: return "résistance";
    case FamilleValeur::Condensateur: return "condensateur";
    case FamilleValeur::Inductance: return "inductance";
    case FamilleValeur::Ferrite: return "perle ferrite";
    case FamilleValeur::Fusible: return "fusible";
    case FamilleValeur::Quartz: return "quartz";
    case FamilleValeur::Zener: return "diode Zener";
    case FamilleValeur::Pile: return "pile";
    case FamilleValeur::Aucune: break;
  }
  return "sans unité";
}

const char* uniteDeFamille(FamilleValeur f) {
  switch (f) {
    case FamilleValeur::Resistance:
    case FamilleValeur::Ferrite: return "Ω";
    case FamilleValeur::Condensateur: return "F";
    case FamilleValeur::Inductance: return "H";
    case FamilleValeur::Fusible: return "A";
    case FamilleValeur::Quartz: return "Hz";
    case FamilleValeur::Zener:
    case FamilleValeur::Pile: return "V";
    case FamilleValeur::Aucune: break;
  }
  return "";
}

FamilleValeur familleDepuisPrefixe(const std::string& prefixe, const std::string& symbole) {
  if (minuscules(symbole).find("zener") != std::string::npos) return FamilleValeur::Zener;
  if (prefixe == "R" || prefixe == "RN" || prefixe == "RV" || prefixe == "RT" || prefixe == "VR") return FamilleValeur::Resistance;
  if (prefixe == "C" || prefixe == "CP") return FamilleValeur::Condensateur;
  if (prefixe == "L") return FamilleValeur::Inductance;
  if (prefixe == "FB") return FamilleValeur::Ferrite;
  if (prefixe == "F") return FamilleValeur::Fusible;
  if (prefixe == "Y") return FamilleValeur::Quartz;
  if (prefixe == "BT") return FamilleValeur::Pile;
  return FamilleValeur::Aucune;
}

FamilleValeur familleDe(const ConceptionElec& c, const ComposantElec& k) {
  const DefSymbole* d = c.symbole(k.symbole);
  std::string prefixe = d ? d->prefixe : std::string();
  if (prefixe.empty())
    for (char x : k.reference) {
      if (!std::isalpha(static_cast<unsigned char>(x))) break;
      prefixe += x;
    }
  return familleDepuisPrefixe(prefixe, k.symbole + " " + (d ? d->nom : std::string()));
}

ValeurComprise lireValeurComposant(const std::string& texte, FamilleValeur famille) {
  ValeurComprise v;
  v.famille = famille;
  const std::string s = normaliser(texte);
  if (s.empty()) return v;
  if (famille == FamilleValeur::Aucune) {
    v.etat = EtatValeur::TexteLibre;
    return v;
  }
  auto refus = [&](const std::string& r) {
    v.etat = EtatValeur::Refusee;
    v.raison = r;
    return v;
  };
  std::size_t i = 0;
  const Principal p = lirePrincipal(s, i, famille);
  if (!p.ok) return refus(p.erreur.empty() ? "valeur illisible : " + seMesure(famille) + " (par exemple " + (famille == FamilleValeur::Condensateur ? "100n" : famille == FamilleValeur::Inductance ? "10u" : "4k7") + ")" : p.erreur);
  v.nombre = p.nombre;
  v.zeroFinal = p.zeroFinal;
  const Unite attendue = uniteDe(famille);
  if (p.unite != Unite::Aucune && p.unite != attendue) return refus(seMesure(famille));
  if (p.rkmR && famille != FamilleValeur::Resistance && famille != FamilleValeur::Ferrite && famille != FamilleValeur::Inductance)
    return refus("la lettre R ne s'emploie que pour une résistance ou une inductance");
  if (p.rkmR && famille == FamilleValeur::Inductance) {
    // R sur une inductance : des microhenrys, par usage des fabricants
    v.nombre *= 1e-6;
    v.note = "= " + ecrireSI(v.nombre, "H");
  }
  // Nombre nu sur un condensateur ou une inductance : ambigu
  if (p.nu && (famille == FamilleValeur::Condensateur || famille == FamilleValeur::Inductance)) {
    const std::string eia = propositionEia(p.chiffres, famille);
    if (!eia.empty()) v.propositions.push_back(eia);
    const std::vector<double> facteurs = famille == FamilleValeur::Condensateur ? std::vector<double>{1e-12, 1e-9, 1e-6} : std::vector<double>{1e-9, 1e-6, 1e-3};
    for (double fac : facteurs) {
      const double x = p.nombre * fac;
      const Plage pl = plage(famille);
      if (x >= pl.min && x <= pl.max) v.propositions.push_back(ecrireSI(x, uniteDeFamille(famille)));
    }
    std::string liste;
    for (const auto& x : v.propositions) liste += (liste.empty() ? "" : " ? ") + x;
    return refus("nombre sans unité, ambigu pour " + std::string(famille == FamilleValeur::Condensateur ? "un condensateur" : "une inductance") + (liste.empty() ? "" : " : " + liste + " ?"));
  }
  // Compléments
  std::vector<std::string> mots;
  {
    std::string reste = i < s.size() ? s.substr(i) : std::string();
    std::string cour;
    for (char c : reste) {
      if (c == ' ') {
        if (!cour.empty()) mots.push_back(cour);
        cour.clear();
      } else
        cour += c;
    }
    if (!cour.empty()) mots.push_back(cour);
    // « 50 V », « ± 5 % » : le nombre et son unité réunis
    std::vector<std::string> r;
    for (std::size_t k = 0; k < mots.size(); ++k) {
      std::string m = mots[k];
      if (m == "~" && k + 1 < mots.size()) m += mots[++k];
      const bool nombreSeul = !m.empty() && m.find_first_not_of("0123456789,.~") == std::string::npos;
      if (nombreSeul && k + 1 < mots.size()) {
        const std::string& u = mots[k + 1];
        if (u == "%" || u == "V" || u == "W" || u == "A" || u == "mA" || u == "mW" || u == "kV") {
          m += " " + u;
          ++k;
        }
      }
      r.push_back(m);
    }
    mots = r;
  }
  for (const std::string& m : mots)
    if (!lireComplement(m, v)) {
      if (lettreDeTolerance(m) || std::isalpha(static_cast<unsigned char>(m[0])) || chiffre(m[0]) || m[0] == '~')
        v.raison = (v.raison.empty() ? "" : v.raison + " ; ") + "complément non compris : « " + m + " »";
    }
  // Plage
  const Plage pl = plage(famille);
  const bool strap = famille == FamilleValeur::Resistance && v.nombre == 0;
  if (!strap && (v.nombre < pl.min * (1 - 1e-9) || v.nombre > pl.max * (1 + 1e-9)))
    return refus("hors de la plage d'" + std::string(famille == FamilleValeur::Resistance || famille == FamilleValeur::Inductance ? "une " : "un ") + nomFamille(famille) + " : de " +
                 ecrireSI(pl.min, uniteDeFamille(famille)) + " à " + ecrireSI(pl.max, uniteDeFamille(famille)));
  v.etat = EtatValeur::Comprise;
  if (v.zeroFinal) v.note = (v.note.empty() ? "" : v.note + " ; ") + "zéro final : tolérance serrée (CEI 60062)";
  // Série E
  v.serieReference = v.tolerance ? serieDeTolerance(*v.tolerance) : serieDeFamille(famille);
  if (v.serieReference && v.nombre > 0) {
    v.serie = dansSerie(v.nombre, v.serieReference) ? v.serieReference : 0;
    v.normalisee = plusProcheDansSerie(v.nombre, v.serieReference);
  }
  return v;
}

// ---------- Écriture ----------
std::string ecrireSI(double base, const std::string& unite, int chiffres) {
  if (base == 0) return "0" + FINE + unite;
  const double a = std::abs(base);
  int e3 = static_cast<int>(std::floor(std::log10(a) / 3.0)) * 3;
  e3 = std::clamp(e3, -15, 12);
  double m = arrondiSignificatif(base / std::pow(10.0, e3), chiffres);
  if (std::abs(m) >= 1000 && e3 < 12) {
    e3 += 3;
    m = arrondiSignificatif(base / std::pow(10.0, e3), chiffres);
  }
  static const std::map<int, std::string> prefixes{{-15, "f"}, {-12, "p"}, {-9, "n"}, {-6, "µ"}, {-3, "m"}, {0, ""}, {3, "k"}, {6, "M"}, {9, "G"}, {12, "T"}};
  return nombreFrancais(m, chiffres) + FINE + prefixes.at(e3) + unite;
}

std::string ecrireCompact(double base, FamilleValeur f) {
  auto rkm = [](double m, const std::string& lettre) {
    std::string t = nombreFrancais(m, 3);
    const std::size_t v = t.find(',');
    if (v == std::string::npos) return t + lettre;
    return t.substr(0, v) + lettre + t.substr(v + 1);
  };
  switch (f) {
    case FamilleValeur::Resistance:
    case FamilleValeur::Ferrite: {
      if (base == 0) return "0R";
      if (base < 1) {
        if (base >= 0.01) {
          std::string t = nombreFrancais(arrondiSignificatif(base, 3), 3);  // « 0,47 »
          std::string frac = t.substr(t.find(',') + 1);
          if (frac.size() < 2) frac += "0";
          return "R" + frac;
        }
        return rkm(arrondiSignificatif(base * 1e3, 3), "L");
      }
      int e3 = std::clamp(static_cast<int>(std::floor(std::log10(base) / 3.0)) * 3, 0, 12);
      double m = arrondiSignificatif(base / std::pow(10.0, e3), 3);
      if (m >= 1000 && e3 < 12) {
        e3 += 3;
        m /= 1000;
      }
      static const std::map<int, std::string> l{{0, "R"}, {3, "k"}, {6, "M"}, {9, "G"}, {12, "T"}};
      return rkm(m, l.at(e3));
    }
    case FamilleValeur::Condensateur:
    case FamilleValeur::Inductance: {
      if (base == 0) return "0";
      int e3 = std::clamp(static_cast<int>(std::floor(std::log10(base) / 3.0)) * 3, -12, 0);
      double m = arrondiSignificatif(base / std::pow(10.0, e3), 3);
      if (m >= 1000 && e3 < 0) {
        e3 += 3;
        m /= 1000;
      }
      static const std::map<int, std::string> l{{-12, "p"}, {-9, "n"}, {-6, "u"}, {-3, "m"}};
      if (e3 == 0) return nombreFrancais(m, 3) + (f == FamilleValeur::Condensateur ? "F" : "H");
      return rkm(m, l.at(e3));
    }
    case FamilleValeur::Zener:
    case FamilleValeur::Pile: return rkm(arrondiSignificatif(base, 3), "V");
    case FamilleValeur::Fusible:
    case FamilleValeur::Quartz: {
      std::string t = ecrireSI(base, uniteDeFamille(f));
      remplacer(t, FINE, "");
      return t;
    }
    case FamilleValeur::Aucune: break;
  }
  return {};
}

std::string ecrireValeurComposant(const ValeurComprise& v, AffichageValeur mode, const std::string& tape) {
  if (mode == AffichageValeur::CommeTapee || !v.comprise()) return tape;
  const bool si = mode == AffichageValeur::SystemeInternational;
  std::string r = si ? ecrireSI(v.nombre, uniteDeFamille(v.famille)) : ecrireCompact(v.nombre, v.famille);
  auto colle = [&](double x, const std::string& u) {
    std::string t = ecrireSI(x, u);
    if (!si) remplacer(t, FINE, "");
    return t;
  };
  if (v.tension) r += " " + (si ? colle(*v.tension, "V") : ecrireCompact(*v.tension, FamilleValeur::Pile) == nombreFrancais(*v.tension, 3) + "V" ? colle(*v.tension, "V") : ecrireCompact(*v.tension, FamilleValeur::Pile));
  if (v.tolerance) r += " " + std::string(si ? "±" : "") + nombreFrancais(*v.tolerance, 3) + (si ? FINE : "") + "%";
  if (!v.toleranceAsymetrique.empty()) r += " " + v.toleranceAsymetrique;
  if (!v.dielectrique.empty()) r += " " + v.dielectrique;
  if (v.puissance) r += " " + colle(*v.puissance, "W");
  if (v.courant) r += " " + colle(*v.courant, "A");
  return r;
}

std::string resumeValeur(const ValeurComprise& v) {
  switch (v.etat) {
    case EtatValeur::Vide: return {};
    case EtatValeur::TexteLibre: return "texte libre";
    case EtatValeur::Refusee: return v.raison;
    case EtatValeur::Comprise: break;
  }
  std::string r = "= " + ecrireSI(v.nombre, uniteDeFamille(v.famille));
  if (v.serieReference && v.nombre > 0) {
    if (v.serie) r += " · E" + std::to_string(v.serie);
    else r += " · hors E" + std::to_string(v.serieReference) + " (" + ecrireSI(v.normalisee, uniteDeFamille(v.famille)) + ")";
  }
  if (!v.note.empty() && v.note.rfind("= ", 0) != 0) r += " · " + v.note;
  if (!v.raison.empty()) r += " · " + v.raison;
  return r;
}

// ---------- Séries E ----------
const std::vector<double>& serieE(int n) {
  static const std::vector<double> e3{1.0, 2.2, 4.7};
  static const std::vector<double> e6{1.0, 1.5, 2.2, 3.3, 4.7, 6.8};
  static const std::vector<double> e12{1.0, 1.2, 1.5, 1.8, 2.2, 2.7, 3.3, 3.9, 4.7, 5.6, 6.8, 8.2};
  static const std::vector<double> e24{1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0, 3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1};
  static const std::vector<double> e96{1.00, 1.02, 1.05, 1.07, 1.10, 1.13, 1.15, 1.18, 1.21, 1.24, 1.27, 1.30, 1.33, 1.37, 1.40, 1.43, 1.47, 1.50, 1.54, 1.58,
                                       1.62, 1.65, 1.69, 1.74, 1.78, 1.82, 1.87, 1.91, 1.96, 2.00, 2.05, 2.10, 2.15, 2.21, 2.26, 2.32, 2.37, 2.43, 2.49, 2.55,
                                       2.61, 2.67, 2.74, 2.80, 2.87, 2.94, 3.01, 3.09, 3.16, 3.24, 3.32, 3.40, 3.48, 3.57, 3.65, 3.74, 3.83, 3.92, 4.02, 4.12,
                                       4.22, 4.32, 4.42, 4.53, 4.64, 4.75, 4.87, 4.99, 5.11, 5.23, 5.36, 5.49, 5.62, 5.76, 5.90, 6.04, 6.19, 6.34, 6.49, 6.65,
                                       6.81, 6.98, 7.15, 7.32, 7.50, 7.68, 7.87, 8.06, 8.25, 8.45, 8.66, 8.87, 9.09, 9.31, 9.53, 9.76};
  static const std::vector<double> e48 = [] {
    std::vector<double> r;
    for (std::size_t i = 0; i < e96.size(); i += 2) r.push_back(e96[i]);
    return r;
  }();
  static const std::vector<double> e192 = [] {
    // Calculée à trois chiffres ; ses valeurs de rang pair sont celles de E96 ; 9,20 et non 9,19
    std::vector<double> r;
    for (int i = 0; i < 192; ++i) {
      double x = std::round(std::pow(10.0, i / 192.0) * 100.0) / 100.0;
      if (i % 2 == 0) x = e96[static_cast<std::size_t>(i / 2)];
      if (std::abs(x - 9.19) < 1e-9) x = 9.20;
      r.push_back(x);
    }
    return r;
  }();
  switch (n) {
    case 3: return e3;
    case 6: return e6;
    case 12: return e12;
    case 24: return e24;
    case 48: return e48;
    case 96: return e96;
    case 192: return e192;
    default: break;
  }
  static const std::vector<double> vide;
  return vide;
}

int serieDeTolerance(double pourcent) {
  if (pourcent <= 0.5) return 192;
  if (pourcent <= 1) return 96;
  if (pourcent <= 2) return 48;
  if (pourcent <= 5) return 24;
  if (pourcent <= 10) return 12;
  if (pourcent <= 20) return 6;
  return 3;
}

int serieDeFamille(FamilleValeur f) {
  switch (f) {
    case FamilleValeur::Resistance:
    case FamilleValeur::Zener: return 24;
    case FamilleValeur::Condensateur:
    case FamilleValeur::Inductance: return 12;
    default: return 0;
  }
}

namespace {
// Les candidats d'une série autour d'une valeur : la décade de la valeur, la précédente et la suivante
std::vector<double> candidats(double valeur, int n) {
  std::vector<double> r;
  const auto& s = serieE(n);
  if (s.empty() || valeur <= 0) return r;
  const int e = static_cast<int>(std::floor(std::log10(valeur)));
  for (int d = e - 1; d <= e + 1; ++d)
    for (double x : s) r.push_back(x * std::pow(10.0, d));
  return r;
}
}  // namespace

bool dansSerie(double valeur, int n) {
  for (double c : candidats(valeur, n))
    if (std::abs(c - valeur) <= c * 0.0005) return true;
  return false;
}

double plusProcheDansSerie(double valeur, int n) {
  double meilleur = valeur, ecart = 1e300;
  for (double c : candidats(valeur, n)) {
    const double d = std::abs(std::log(c / valeur));
    if (d < ecart - 1e-12) {
      ecart = d;
      meilleur = c;
    }
  }
  return meilleur;
}

double voisineDansSerie(double valeur, int n, int pas) {
  std::vector<double> c = candidats(valeur, n);
  if (c.empty()) return valeur;
  std::sort(c.begin(), c.end());
  // La position de la valeur (ou de sa voisine dans la série)
  if (dansSerie(valeur, n)) {
    const double p = plusProcheDansSerie(valeur, n);
    auto it = std::min_element(c.begin(), c.end(), [p](double a, double b) { return std::abs(a - p) < std::abs(b - p); });
    const auto i = static_cast<long>(it - c.begin()) + pas;
    if (i >= 0 && i < static_cast<long>(c.size())) return c[static_cast<std::size_t>(i)];
    return valeur;
  }
  if (pas > 0) {
    for (double x : c)
      if (x > valeur) return x;
  } else {
    for (auto it = c.rbegin(); it != c.rend(); ++it)
      if (*it < valeur) return *it;
  }
  return valeur;
}

std::optional<double> toleranceDeLettre(char c) {
  switch (c) {
    case 'E': return 0.005;
    case 'L': return 0.01;
    case 'P': return 0.02;
    case 'W': return 0.05;
    case 'B': return 0.1;
    case 'C': return 0.25;
    case 'D': return 0.5;
    case 'F': return 1;
    case 'G': return 2;
    case 'H': return 3;
    case 'J': return 5;
    case 'K': return 10;
    case 'M': return 20;
    case 'N': return 30;
    default: return std::nullopt;
  }
}

std::string cleValeur(const ValeurComprise& v, const std::string& texte) {
  if (v.comprise()) {
    char t[160];
    std::snprintf(t, sizeof t, "%d:%.6g|%.4g|%.4g|%s", static_cast<int>(v.famille), v.nombre, v.tension.value_or(0), v.tolerance.value_or(0), v.dielectrique.c_str());
    return t;
  }
  std::string r;
  for (char c : normaliser(texte))
    if (c != ' ') r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return "texte:" + r;
}

// ---------- Paramètres typés ----------
const std::vector<DefParametre>& parametresDeFamille(FamilleValeur f) {
  using G = DefParametre::Genre;
  static const std::vector<DefParametre> resistance{
      {"tolerance", "Tolérance", "%", G::Nombre, {}, {"tolerance", "tol", "precision"}},
      {"puissance", "Puissance", "W", G::Nombre, {}, {"puissance", "power", "wattage"}},
      {"tensionMax", "Tension maximale", "V", G::Nombre, {}, {"tension maximale", "tension max", "voltage"}},
      {"coefTemperature", "Coefficient de température", "ppm/K", G::Nombre, {}, {"coefficient de temperature", "tcr", "ct"}},
      {"technologie", "Technologie", "", G::Choix, {"couche épaisse", "couche mince", "bobinée"}, {"technologie"}},
  };
  static const std::vector<DefParametre> condensateur{
      {"tension", "Tension", "V", G::Nombre, {}, {"tension", "voltage", "tension max", "tension de service"}},
      {"dielectrique", "Diélectrique", "", G::Choix, {"C0G", "X7R", "X5R", "Y5V", "électrolytique", "tantale", "polymère", "film"}, {"dielectrique", "dielectric"}},
      {"tolerance", "Tolérance", "%", G::Nombre, {}, {"tolerance", "tol"}},
      {"esr", "Résistance série", "mΩ", G::Nombre, {}, {"resistance serie", "esr"}},
      {"ondulation", "Courant d'ondulation", "A", G::Nombre, {}, {"courant d'ondulation", "ripple"}},
  };
  static const std::vector<DefParametre> inductance{
      {"courant", "Courant nominal", "A", G::Nombre, {}, {"courant nominal", "courant", "irms"}},
      {"saturation", "Courant de saturation", "A", G::Nombre, {}, {"courant de saturation", "isat"}},
      {"resistanceSerie", "Résistance série", "mΩ", G::Nombre, {}, {"resistance serie", "dcr"}},
      {"blindee", "Blindée", "", G::Booleen, {}, {"blindee", "shielded"}},
      {"tolerance", "Tolérance", "%", G::Nombre, {}, {"tolerance", "tol"}},
  };
  static const std::vector<DefParametre> ferrite{
      {"courant", "Courant nominal", "A", G::Nombre, {}, {"courant nominal", "courant"}},
      {"resistanceSerie", "Résistance série", "mΩ", G::Nombre, {}, {"resistance serie", "dcr"}},
  };
  static const std::vector<DefParametre> fusible{
      {"tension", "Tension", "V", G::Nombre, {}, {"tension", "voltage"}},
      {"caracteristique", "Caractéristique", "", G::Choix, {"rapide", "temporisé"}, {"caracteristique", "type"}},
  };
  static const std::vector<DefParametre> quartz{
      {"chargeCapacitive", "Capacité de charge", "pF", G::Nombre, {}, {"capacite de charge", "charge", "cl"}},
      {"tolerancePpm", "Tolérance", "ppm", G::Nombre, {}, {"tolerance"}},
  };
  static const std::vector<DefParametre> zener{
      {"puissance", "Puissance", "W", G::Nombre, {}, {"puissance", "power"}},
      {"tolerance", "Tolérance", "%", G::Nombre, {}, {"tolerance", "tol"}},
  };
  static const std::vector<DefParametre> pile{
      {"capacite", "Capacité", "mAh", G::Nombre, {}, {"capacite"}},
  };
  static const std::vector<DefParametre> autre{
      {"tensionAlimentation", "Tension d'alimentation", "V", G::Nombre, {}, {"tension d'alimentation", "alimentation", "vcc", "vdd"}},
  };
  switch (f) {
    case FamilleValeur::Resistance: return resistance;
    case FamilleValeur::Condensateur: return condensateur;
    case FamilleValeur::Inductance: return inductance;
    case FamilleValeur::Ferrite: return ferrite;
    case FamilleValeur::Fusible: return fusible;
    case FamilleValeur::Quartz: return quartz;
    case FamilleValeur::Zener: return zener;
    case FamilleValeur::Pile: return pile;
    case FamilleValeur::Aucune: break;
  }
  return autre;
}

const DefParametre* parametre(FamilleValeur f, const std::string& cle) {
  for (const auto& p : parametresDeFamille(f))
    if (p.cle == cle) return &p;
  return nullptr;
}

std::optional<double> lireParametre(const DefParametre& p, const std::string& texte, std::string* erreur) {
  auto non = [&](const std::string& e) -> std::optional<double> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  const std::string s = normaliser(texte);
  if (s.empty()) return non("valeur vide");
  if (p.genre == DefParametre::Genre::Choix) {
    for (std::size_t i = 0; i < p.choix.size(); ++i)
      if (simple(p.choix[i]) == simple(s)) return static_cast<double>(i);
    std::string liste;
    for (const auto& c : p.choix) liste += (liste.empty() ? "" : ", ") + c;
    return non("choisir parmi : " + liste);
  }
  if (p.genre == DefParametre::Genre::Booleen) {
    const std::string m = simple(s);
    if (m == "oui" || m == "vrai" || m == "1" || m == "yes") return 1.0;
    if (m == "non" || m == "faux" || m == "0" || m == "no") return 0.0;
    return non("oui ou non");
  }
  if (p.genre == DefParametre::Genre::Texte) return non("texte");
  // Nombre : RKM en volts (6V3), fraction (1/4 W), nombre et préfixe et unité
  std::size_t i = 0;
  double x = 0;
  std::string ecrit;
  {
    std::size_t k = 0;
    while (k < s.size() && chiffre(s[k])) ++k;
    if (p.unite == "V" && k > 0 && k + 1 < s.size() && s[k] == 'V' && chiffre(s[k + 1])) {
      std::size_t l = k + 1;
      while (l < s.size() && chiffre(s[l])) ++l;
      if (l == s.size()) return std::strtod((s.substr(0, k) + "." + s.substr(k + 1)).c_str(), nullptr);
    }
  }
  if (s[0] == '~') ++i;
  if (!lireDecimal(s, i, x, ecrit)) return non("nombre attendu");
  if (i < s.size() && s[i] == '/') {
    ++i;
    double b = 0;
    std::string e2;
    if (!lireDecimal(s, i, b, e2) || b == 0) return non("fraction illisible");
    x /= b;
  }
  std::string u = s.substr(i);
  if (!u.empty() && u[0] == ' ') u.erase(0, 1);
  // L'unité du paramètre peut avoir un préfixe (mΩ, pF, mAh) : la valeur s'exprime dans cette unité
  std::string uniteBase = p.unite;
  double facteurParam = 1;
  remplacer(uniteBase, "Ω", std::string(1, OHM));
  if (uniteBase.size() >= 2 && facteurPrefixe(uniteBase[0]) && uniteBase != "ppm" && uniteBase != "ppm/K") {
    facteurParam = *facteurPrefixe(uniteBase[0]);
    uniteBase = uniteBase.substr(1);
  }
  if (u.empty()) return x;
  if (u == uniteBase) return x / facteurParam;
  if (u.size() > uniteBase.size() && u.substr(1) == uniteBase && facteurPrefixe(u[0])) return x * *facteurPrefixe(u[0]) / facteurParam;
  return non("unité attendue : " + p.unite);
}

int migrerChampsEnParametres(ConceptionElec& c) {
  int n = 0;
  for (auto& k : c.composants) {
    const FamilleValeur f = familleDe(c, k);
    for (auto it = k.champs.begin(); it != k.champs.end();) {
      const std::string nom = simple(it->first);
      const DefParametre* trouve = nullptr;
      for (const auto& p : parametresDeFamille(f)) {
        if (simple(p.libelle) == nom) trouve = &p;
        for (const auto& s : p.synonymes)
          if (simple(s) == nom) trouve = &p;
        if (trouve) break;
      }
      if (trouve && !k.parametres.count(trouve->cle)) {
        k.parametres[trouve->cle] = it->second;
        it = k.champs.erase(it);
        ++n;
      } else
        ++it;
    }
  }
  return n;
}

}  // namespace prom::ecad
