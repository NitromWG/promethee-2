// Prométhée : revue de conception automatique (lot L13, 13.13). Licence GPL-3.0-only.
#include "promethee/revue_conception.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
std::string majuscules(std::string s) {
  for (auto& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}
const DefSymbole* symboleDe(const ConceptionElec& c, const ComposantElec& k) { return c.symbole(k.symbole); }
std::string prefixe(const ComposantElec& k) {
  std::string p;
  for (char ch : k.reference) {
    if (!std::isalpha(static_cast<unsigned char>(ch))) break;
    p += ch;
  }
  return p;
}

}  // namespace

// Tension lue dans un nom de réseau ou un paramètre : « +5V », « 3V3 », « 25 V », « 6,3V » ; 0 sinon
double tensionDe(const std::string& t0) {
  std::string t;
  for (char ch : t0)
    if (ch != ' ' && ch != '+') t += ch == ',' ? '.' : ch;
  const std::size_t v = t.find_first_of("vV");
  if (v == std::string::npos || v == 0) return 0;
  const std::string avant = t.substr(0, v), apres = t.substr(v + 1);
  if (avant.find_first_not_of("0123456789.") != std::string::npos) return 0;
  double x = std::atof(avant.c_str());
  if (!apres.empty() && apres.find_first_not_of("0123456789") == std::string::npos) x += std::atof(("0." + apres).c_str());
  else if (!apres.empty()) return 0;
  return x;
}

namespace {
struct Vue {
  const ConceptionElec& c;
  Connectivite k;
  std::map<std::string, std::set<std::string>> composantsDuReseau;  // réseau → UUID des composants
  explicit Vue(const ConceptionElec& cc) : c(cc), k(connectivite(cc)) {
    for (const auto& r : k.reseaux)
      for (const auto& b : r.broches) composantsDuReseau[r.nom].insert(b.composant);
  }
  const Reseau* reseau(const std::string& nom) const {
    const int i = k.indice(nom);
    return i < 0 ? nullptr : &k.reseaux[static_cast<std::size_t>(i)];
  }
  std::string reseauDe(const ComposantElec& x, const std::string& broche) const { return k.nom(x.uuid, broche); }
  std::vector<std::string> reseauxDe(const ComposantElec& x) const {
    std::vector<std::string> r;
    if (const DefSymbole* d = symboleDe(c, x))
      for (const auto& b : d->broches)
        if (const std::string n = reseauDe(x, b.numero); !n.empty() && std::find(r.begin(), r.end(), n) == r.end()) r.push_back(n);
    return r;
  }
  // Un composant du genre voulu relie-t-il ce réseau à un autre réseau qui vérifie « autre » ?
  bool relie(const std::string& reseau, bool (*genre)(const ConceptionElec&, const ComposantElec&), const std::function<bool(const std::string&)>& autre) const {
    const auto it = composantsDuReseau.find(reseau);
    if (it == composantsDuReseau.end()) return false;
    for (const auto& u : it->second) {
      const ComposantElec* x = c.composant(u);
      if (!x || !genre(c, *x)) continue;
      for (const auto& n : reseauxDe(*x))
        if (n != reseau && autre(n)) return true;
    }
    return false;
  }
  bool alimente(const std::string& n) const {
    const Reseau* r = reseau(n);
    return r && estAlimentation(*r);
  }
};
}  // namespace

bool estResistance(const ConceptionElec& c, const ComposantElec& k) {
  const DefSymbole* d = symboleDe(c, k);
  return (d && (d->id == "R" || d->id == "R_Small" || d->prefixe == "R")) || prefixe(k) == "R";
}
bool estCondensateur(const ConceptionElec& c, const ComposantElec& k) {
  const DefSymbole* d = symboleDe(c, k);
  return (d && (d->id == "C" || d->id == "C_Pol" || d->prefixe == "C")) || prefixe(k) == "C";
}
bool estLed(const ConceptionElec& c, const ComposantElec& k) {
  const DefSymbole* d = symboleDe(c, k);
  return (d && majuscules(d->id).find("LED") != std::string::npos) || majuscules(k.valeur).find("LED") != std::string::npos;
}
bool estRegulateur(const ConceptionElec& c, const ComposantElec& k) {
  const DefSymbole* d = symboleDe(c, k);
  if (!d) return false;
  const std::string id = majuscules(d->id), cat = majuscules(d->categorie);
  return id.find("REGULATEUR") != std::string::npos || id.find("78") == 2 || id.find("LDO") != std::string::npos || id.find("1117") != std::string::npos ||
         (cat.find("ALIMENTATION") != std::string::npos && std::any_of(d->broches.begin(), d->broches.end(), [](const Broche& b) { return b.type == TypeBroche::AlimSortie; }));
}
bool estMasse(const std::string& n) {
  const std::string m = majuscules(n);
  return m == "GND" || m == "0V" || m == "VSS" || m == "AGND" || m == "DGND" || m == "GNDA" || m == "GNDD" || m == "PGND" || m == "MASSE" || m.rfind("GND_", 0) == 0;
}
bool estAlimentation(const Reseau& r) {
  if (estMasse(r.nom)) return false;
  if (!r.alimentations.empty()) return true;
  const std::string m = majuscules(r.nom);
  return !m.empty() && (m[0] == '+' || m.rfind("VCC", 0) == 0 || m.rfind("VDD", 0) == 0 || m.rfind("VBAT", 0) == 0 || m.rfind("VIN", 0) == 0 || m == "3V3" || m == "5V" ||
                        m == "12V" || m.rfind("V+", 0) == 0);
}

const std::vector<RegleRevue>& reglesRevue() {
  static const std::vector<RegleRevue> r = {
      {"LED-R", "LED sans résistance", "Une LED doit avoir une résistance (ou un pilote de courant) en série : sans elle, le courant n'est limité que par la source."},
      {"REG-COUT", "Régulateur sans condensateur de sortie", "Un régulateur linéaire oscille ou répond mal sans condensateur entre sa sortie et la masse (souvent 10 µF)."},
      {"REG-CIN", "Régulateur sans condensateur d'entrée", "Un condensateur près de l'entrée du régulateur le garde stable quand l'alimentation est loin."},
      {"I2C-TIRAGE", "Bus I2C sans tirage", "SDA et SCL sont à drain ouvert : chacune a besoin d'une résistance de tirage vers l'alimentation (2,2 à 10 kΩ)."},
      {"DECOUPLAGE", "Circuit intégré sans découplage", "Chaque broche d'alimentation d'un circuit intégré veut un condensateur de 100 nF vers la masse, au plus près."},
      {"ENTREE-LIBRE", "Entrée laissée en l'air", "Une entrée qui ne voit aucune autre broche flotte : elle consomme et bascule au hasard."},
      {"CAPA-TENSION", "Condensateur proche de sa tension", "Un condensateur dont la tension nominale est à moins de 20 % de celle qu'il voit vieillit vite (électrolytique) ou perd sa capacité (céramique)."},
  };
  return r;
}

std::vector<PointRevue> revueConception(const ConceptionElec& c) {
  std::vector<PointRevue> r;
  const Vue v(c);
  std::set<std::string> poses;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) poses.insert(s.composant);
  for (const auto& x : c.composants) {
    if (!poses.count(x.uuid) || x.nePasMonter) continue;
    const DefSymbole* d = symboleDe(c, x);
    if (!d) continue;
    // LED-R : aucun de ses deux réseaux ne passe par une résistance
    if (estLed(c, x)) {
      bool resistance = false;
      for (const auto& n : v.reseauxDe(x)) {
        const auto it = v.composantsDuReseau.find(n);
        if (it == v.composantsDuReseau.end()) continue;
        for (const auto& u : it->second)
          if (const ComposantElec* y = c.composant(u); y && y->uuid != x.uuid && estResistance(c, *y)) resistance = true;
      }
      if (!resistance) r.push_back({"LED-R", "erreur", x.reference, x.reference + " : LED sans résistance en série.", "Ajouter une résistance en série (R = (V − Vf) / I, 1 kΩ sous 5 V pour 3 mA)."});
    }
    // REG-COUT et REG-CIN
    if (estRegulateur(c, x)) {
      for (const auto& b : d->broches) {
        const std::string n = v.reseauDe(x, b.numero);
        if (n.empty() || estMasse(n)) continue;
        const bool sortie = b.type == TypeBroche::AlimSortie;
        const bool entree = b.type == TypeBroche::AlimEntree;
        if (!sortie && !entree) continue;
        if (!v.relie(n, &estCondensateur, [](const std::string& m) { return estMasse(m); })) {
          if (sortie)
            r.push_back({"REG-COUT", "erreur", x.reference, x.reference + " : sortie « " + n + " » sans condensateur vers la masse.", "Poser un condensateur de 10 µF entre " + n + " et la masse, près de la broche."});
          else
            r.push_back({"REG-CIN", "avertissement", x.reference, x.reference + " : entrée « " + n + " » sans condensateur vers la masse.", "Poser un condensateur de 1 à 10 µF entre " + n + " et la masse."});
        }
      }
    }
    // DECOUPLAGE : broche d'alimentation d'un circuit intégré (U) dont le réseau ne voit aucun condensateur vers la masse
    if (prefixe(x) == "U" && !estRegulateur(c, x)) {
      std::set<std::string> vus;
      for (const auto& b : d->broches) {
        if (b.type != TypeBroche::AlimEntree) continue;
        const std::string n = v.reseauDe(x, b.numero);
        if (n.empty() || estMasse(n) || !vus.insert(n).second) continue;
        if (!v.relie(n, &estCondensateur, [](const std::string& m) { return estMasse(m); }))
          r.push_back({"DECOUPLAGE", "avertissement", x.reference, x.reference + " : alimentation « " + n + " » sans condensateur de découplage.", "Poser 100 nF entre " + n + " et la masse, au plus près de " + x.reference + "."});
      }
    }
    // ENTREE-LIBRE : broche d'entrée seule sur son réseau
    for (const auto& b : d->broches) {
      if (b.type != TypeBroche::Entree) continue;
      const std::string n = v.reseauDe(x, b.numero);
      const Reseau* rs = n.empty() ? nullptr : v.reseau(n);
      if (rs && rs->broches.size() == 1 && !rs->nomme)
        r.push_back({"ENTREE-LIBRE", "avertissement", x.reference + "." + b.numero, x.reference + "." + b.numero + " (" + b.nom + ") : entrée qui ne voit aucune autre broche.",
                     "La relier à la masse ou à l'alimentation, directement ou par une résistance."});
    }
    // CAPA-TENSION : tension nominale (paramètre « tension ») face au nom du réseau d'alimentation qu'il voit
    if (estCondensateur(c, x))
      if (const auto it = x.parametres.find("tension"); it != x.parametres.end()) {
        const double nominale = tensionDe(it->second);
        double vue = 0;
        for (const auto& n : v.reseauxDe(x)) vue = std::max(vue, tensionDe(n));
        if (nominale > 0 && vue > 0 && nominale < vue * 1.2)
          r.push_back({"CAPA-TENSION", "avertissement", x.reference, x.reference + " : " + it->second + " nominaux sous " + std::to_string(static_cast<int>(vue + 0.5)) + " V.", "Prendre une tension nominale d'au moins 1,5 fois la tension de service."});
      }
  }
  // I2C-TIRAGE : réseaux nommés SDA ou SCL sans résistance vers une alimentation
  for (const auto& rs : v.k.reseaux) {
    const std::string m = majuscules(rs.nom);
    const bool i2c = m == "SDA" || m == "SCL" || m.find("I2C_SDA") != std::string::npos || m.find("I2C_SCL") != std::string::npos || (m.size() > 3 && (m.substr(m.size() - 4) == "_SDA" || m.substr(m.size() - 4) == "_SCL"));
    if (!i2c || rs.broches.empty()) continue;
    if (!v.relie(rs.nom, &estResistance, [&v](const std::string& n) { return v.alimente(n); }))
      r.push_back({"I2C-TIRAGE", "erreur", rs.nom, "Bus I2C : « " + rs.nom + " » sans résistance de tirage vers l'alimentation.", "Poser 4,7 kΩ entre " + rs.nom + " et l'alimentation du bus."});
  }
  return r;
}

}  // namespace prom::ecad
