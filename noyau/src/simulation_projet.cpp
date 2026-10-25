// Prométhée : la simulation dans le projet (lot L11 de la 0.34). Voir simulation_projet.hpp. Licence GPL-3.0-only.
#include "promethee/simulation_projet.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numbers>
#include <regex>
#include <sstream>
#include <stdexcept>

#include "promethee/csv.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
using J = nlohmann::ordered_json;
using Cx = std::complex<double>;

std::string minuscules(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
std::string nb(double v) {
  char t[48];
  std::snprintf(t, sizeof t, "%.9g", v);
  return t;
}
std::string nbFr(double v, int chiffres = 3) {
  char t[48];
  std::snprintf(t, sizeof t, "%.*g", chiffres, v);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
// Durée lisible : « 5,28 ms »
std::string duree(double t) {
  const double a = std::abs(t);
  if (a >= 1) return nbFr(t) + " s";
  if (a >= 1e-3) return nbFr(t * 1e3) + " ms";
  if (a >= 1e-6) return nbFr(t * 1e6) + " µs";
  return nbFr(t * 1e9) + " ns";
}
std::optional<double> tensionNommee(const std::string& nom) {
  static const std::regex motif(R"(^([+-]?)(\d+)V(\d*)$)", std::regex::icase);
  std::smatch m;
  if (std::regex_match(nom, m, motif)) {
    double v = std::stod(m[2].str());
    if (!m[3].str().empty()) v += std::stod("0." + m[3].str());
    return m[1].str() == "-" ? -v : v;
  }
  const std::string n = minuscules(nom);
  if (n == "vcc" || n == "vdd" || n == "v+") return 5.0;
  return std::nullopt;
}
// Réseau du schéma d'un nœud SPICE
std::string reseauDuNoeud(const NetlistSpice& n, const std::string& noeud) {
  for (const auto& [r, nd] : n.noeuds)
    if (minuscules(nd) == minuscules(noeud)) return r;
  return {};
}
const ComposantElec* parRef(const ConceptionElec& c, const std::string& ref) { return c.parReference(ref); }
// Symboles que la netlist sait écrire sans modèle propre
bool symboleSimule(const std::string& s) {
  static const std::set<std::string> v = {"R", "C", "C_Pol", "L", "Fusible", "Liaison_reseaux", "Buzzer", "Moteur", "Bouton", "Interrupteur", "Potentiometre", "D", "LED", "D_Zener",
                                          "D_Schottky", "Q_NPN", "Q_PNP", "NE555", "AOP", "LM358_unites", "Regulateur", "LM7805", "Pile", "Source_tension", "Source_courant"};
  return v.count(s) > 0;
}
bool symboleSansEffet(const std::string& s) {
  return s.rfind("Conn_", 0) == 0 || s.rfind("Bornier_", 0) == 0 || s == "Trou_fixation" || s == "Trou_fixation_masse" || s == "Entretoise" || s == "Vis" || s == "Dissipateur" ||
         s == "Point_test" || s == "Quartz";
}
}  // namespace

// ---------- Partie du schéma ----------
std::set<std::string> composantsDeFeuille(const ConceptionElec& c, std::size_t feuille) {
  std::set<std::string> r;
  if (feuille >= c.feuilles.size()) return r;
  for (const auto& s : c.feuilles[feuille].symboles)
    if (!s.composant.empty()) r.insert(s.composant);
  return r;
}

std::vector<LiaisonCoupee> liaisonsDeLaPartie(const ConceptionElec& c, const std::set<std::string>& composants) {
  OptionsSimulation o;
  o.composants = composants;
  const NetlistSpice n = netlistSpice(c, AnalyseSpice{}, o);
  std::vector<LiaisonCoupee> r;
  for (const auto& nom : n.coupees) {
    LiaisonCoupee l;
    l.reseau = nom;
    if (const auto v = tensionNommee(nom)) {
      l.bouclage = BouclageLiaison::Source;
      l.valeur = *v;
    }
    r.push_back(l);
  }
  return r;
}

std::vector<ParametreGlobal> ordonnerParametres(const std::vector<ParametreGlobal>& p, std::string* erreur) {
  // La netlist ordonne les paramètres ; on la sollicite sur une conception vide pour garder un seul tri
  OptionsSimulation o;
  o.parametres = p;
  const NetlistSpice n = netlistSpice(nouvelleConception("parametres"), AnalyseSpice{}, o);
  for (const auto& e : n.erreurs)
    if (e.rfind("paramètres : ", 0) == 0) {
      if (erreur) *erreur = e.substr(std::string("paramètres : ").size());
      return {};
    }
  std::vector<ParametreGlobal> r;
  std::istringstream in(n.texte);
  for (std::string l; std::getline(in, l);)
    if (l.rfind(".param ", 0) == 0) {
      const std::string nom = l.substr(7, l.find('=') - 7);
      for (const auto& x : p)
        if (x.nom == nom) r.push_back(x);
    }
  return r;
}

// ---------- Préparation ----------
std::vector<LignePreparation> preparerSimulation(const ConceptionElec& c, const AnalyseSpice& a, const OptionsSimulation& o) {
  std::vector<LignePreparation> r;
  const NetlistSpice n = netlistSpice(c, a, o);
  auto uuidDe = [&](const std::string& ref) {
    const ComposantElec* x = parRef(c, ref);
    return x ? x->uuid : std::string();
  };
  // Masse
  bool masse = false;
  for (const auto& [res, nd] : n.noeuds) masse |= nd == "0";
  if (!masse) r.push_back({"masse", "erreur", "Pas de masse : le calcul a besoin d’un réseau de référence.", {}, {}, {"Poser un symbole de masse (GND)"}});
  // Modèles : composants que la netlist ne sait pas écrire
  for (const auto& x : c.composants) {
    if (x.reference.empty() || x.reference[0] == '#') continue;
    if (!o.composants.empty() && !o.composants.count(x.uuid)) continue;
    if (x.champs.count("Sim.Exclure") || x.champs.count("Sim.Modele")) continue;
    if (const auto ia = c.articles.find(x.article); ia != c.articles.end() && !ia->second.modeleSpice.vide()) continue;
    if (symboleSimule(x.symbole) || symboleSansEffet(x.symbole)) continue;
    LignePreparation l{"modèle", "avertissement", x.reference + " (" + x.symbole + ") n’a pas de modèle de simulation : il est laissé hors du calcul.", x.uuid, {}, {}};
    const char p = x.reference.empty() ? ' ' : static_cast<char>(std::toupper(static_cast<unsigned char>(x.reference[0])));
    if (p == 'R' || p == 'C' || p == 'L' || p == 'D') l.remedes.push_back("Déduire le modèle du repère");
    l.remedes.push_back("Choisir un article avec son modèle");
    l.remedes.push_back("Exclure de la simulation");
    r.push_back(l);
  }
  // Nœuds flottants, erreurs de la netlist
  for (const auto& e : n.erreurs) {
    if (e.find("nœud sans chemin vers la masse") != std::string::npos) {
      const auto a0 = e.find("« "), b0 = e.find(" »");
      const std::string res = a0 != std::string::npos && b0 != std::string::npos ? e.substr(a0 + 3, b0 - a0 - 3) : std::string();
      r.push_back({"flottant", "erreur", "Le réseau « " + res + " » n’a pas de chemin vers la masse en continu.", {}, res, {"Ajouter 1 GΩ vers la masse sur chaque nœud (rshunt)", "Relier le réseau par une résistance"}});
    } else if (e.find("pas de masse") == std::string::npos)
      r.push_back({"modèle", "erreur", e, {}, {}, {}});
  }
  // Boucles de sources de tension et d'inductances idéales (partition en ensembles disjoints)
  {
    std::map<std::string, std::string> parent;
    std::function<std::string(const std::string&)> racine = [&](const std::string& x) -> std::string {
      auto it = parent.find(x);
      if (it == parent.end()) return parent[x] = x;
      if (it->second == x) return x;
      return it->second = racine(it->second);
    };
    std::istringstream in(n.texte);
    std::map<std::string, std::vector<std::string>> chemin;  // racine → éléments
    for (std::string l; std::getline(in, l);) {
      if (l.empty()) continue;
      const char e = static_cast<char>(std::toupper(static_cast<unsigned char>(l[0])));
      if (e != 'V' && e != 'L') continue;
      std::istringstream mots(l);
      std::string nom, p, q;
      mots >> nom >> p >> q;
      if (q.empty()) continue;
      const std::string ra = racine(p), rb = racine(q);
      const std::string ref = n.instances.count(minuscules(nom)) ? n.instances.at(minuscules(nom)) : nom;
      if (ra == rb) {
        std::vector<std::string> v = chemin[ra];
        v.push_back(ref);
        std::string liste;
        for (std::size_t i = 0; i < v.size(); ++i) liste += (i ? (i + 1 == v.size() ? " et " : ", ") : "") + v[i];
        r.push_back({"boucle", "erreur", liste + " forment une boucle de sources idéales : le courant y est indéfini.", uuidDe(ref), {}, {"Insérer 1 mΩ en série"}});
      } else {
        auto& v = chemin[rb];
        v.insert(v.end(), chemin[ra].begin(), chemin[ra].end());
        v.push_back(ref);
        parent[ra] = rb;
      }
    }
  }
  // Sources ; une analyse alternative sans amplitude AC ne montre rien
  bool ac = false;
  {
    std::istringstream in(n.texte);
    for (std::string l; std::getline(in, l);) {
      if (l.empty() || (l[0] != 'V' && l[0] != 'I' && l[0] != 'v' && l[0] != 'i')) continue;
      const std::string nom = l.substr(0, l.find(' '));
      const std::string ref = n.instances.count(minuscules(nom)) ? n.instances.at(minuscules(nom)) : nom;
      std::istringstream mots(l);
      std::string a0, p, q;
      mots >> a0 >> p >> q;
      std::string valeur;
      std::getline(mots, valeur);
      if (minuscules(l).find(" ac ") != std::string::npos || minuscules(l).find(" ac=") != std::string::npos) ac = true;
      r.push_back({"source", "information", ref + " : " + valeur.substr(valeur.find_first_not_of(' ') == std::string::npos ? 0 : valeur.find_first_not_of(' ')), uuidDe(ref), {}, {}});
    }
  }
  if (a.type == AnalyseSpice::Type::Alternatif && !ac)
    r.push_back({"source", "erreur", "Aucune source n’a d’amplitude alternative : l’analyse en fréquence ne montrerait rien.", {}, {}, {"Donner « AC 1 » à la source d’entrée"}});
  // Partie simulée seule
  for (const auto& cp : n.coupees) {
    const auto it = std::find_if(o.liaisons.begin(), o.liaisons.end(), [&](const LiaisonCoupee& l) { return l.reseau == cp; });
    std::string b = "laissé en l’air";
    if (it != o.liaisons.end() && it->bouclage == BouclageLiaison::Source) b = "tenu par une source de " + nbFr(it->valeur) + " V";
    else if (it != o.liaisons.end() && it->bouclage == BouclageLiaison::Charge) b = "chargé par " + nbFr(it->valeur) + " Ω vers la masse";
    else if (const auto v = tensionNommee(cp)) b = "alimenté à " + nbFr(*v) + " V d’après son nom";
    r.push_back({"partie", "information", "Réseau coupé « " + cp + " » : " + b + ".", {}, cp, {}});
  }
  return r;
}

int deduireModeles(ConceptionElec& c, const std::vector<std::string>& uuids, std::vector<std::string>* aSaisir) {
  int n = 0;
  for (const auto& u : uuids) {
    ComposantElec* x = c.composant(u);
    if (!x || x->reference.empty() || x->champs.count("Sim.Modele")) continue;
    if (const auto ia = c.articles.find(x->article); ia != c.articles.end() && !ia->second.modeleSpice.vide()) continue;
    if (symboleSimule(x->symbole)) continue;
    const char p = static_cast<char>(std::toupper(static_cast<unsigned char>(x->reference[0])));
    const DefSymbole* d = c.symbole(x->symbole);
    std::vector<std::string> nums;
    if (d)
      for (const auto& b : d->broches) nums.push_back(b.numero);
    std::sort(nums.begin(), nums.end(), [](const std::string& a, const std::string& b) { return std::atoi(a.c_str()) != std::atoi(b.c_str()) ? std::atoi(a.c_str()) < std::atoi(b.c_str()) : a < b; });
    nums.erase(std::unique(nums.begin(), nums.end()), nums.end());
    if (nums.size() < 2) continue;
    if (p == 'R' || p == 'C' || p == 'L') {
      const FamilleValeur f = p == 'R' ? FamilleValeur::Resistance : p == 'C' ? FamilleValeur::Condensateur : FamilleValeur::Inductance;
      const ValeurComprise v = lireValeurComposant(x->valeur, f);
      if (!v.comprise() || v.nombre <= 0) {
        if (aSaisir) aSaisir->push_back(x->reference);
        continue;
      }
      x->champs["Sim.Modele"] = std::string(1, p) + "{ref} {" + nums[0] + "} {" + nums[1] + "} " + nb(v.nombre);
      ++n;
    } else if (p == 'D') {
      std::string anode, cathode;
      if (d)
        for (const auto& b : d->broches) {
          const std::string m = minuscules(b.nom);
          if (m == "a" || m == "anode" || m == "+") anode = b.numero;
          if (m == "k" || m == "c" || m == "cathode" || m == "-") cathode = b.numero;
        }
      if (anode.empty() || cathode.empty()) anode = nums[1], cathode = nums[0];  // broche 1 : cathode
      x->champs["Sim.Modele"] = "D{ref} {" + anode + "} {" + cathode + "} D1N4148";
      x->champs["Sim.Bibliotheque"] = modelesFournis().at("D1N4148");
      ++n;
    }
  }
  return n;
}

int retirerModeles(ConceptionElec& c, const std::vector<std::string>& uuids) {
  int n = 0;
  for (const auto& u : uuids)
    if (ComposantElec* x = c.composant(u)) n += static_cast<int>(x->champs.erase("Sim.Modele") + x->champs.erase("Sim.Bibliotheque") > 0);
  return n;
}

// ---------- Échecs et remèdes ----------
DiagnosticEchec analyserEchec(const ResultatSimulation& r, const NetlistSpice& n) {
  DiagnosticEchec d;
  std::string tout = r.erreur + "\n";
  for (const auto& l : r.console) tout += l + "\n";
  const std::string m = minuscules(tout);
  static const std::regex noeud(R"(trouble with node \"([^\"]+)\")", std::regex::icase);
  static const std::regex instance(R"(trouble with ([A-Za-z0-9_:.]+)-instance ([A-Za-z0-9_:.]+))", std::regex::icase);
  static const std::regex instant(R"(time = ([-+0-9.eE]+))");
  static const std::regex singulier(R"(check node ([A-Za-z0-9_:.#]+))", std::regex::icase);
  static const std::regex modele(R"(unable to find definition of model ([A-Za-z0-9_:.]+))", std::regex::icase);
  std::smatch s;
  if (std::regex_search(tout, s, noeud)) d.noeud = s[1].str();
  if (std::regex_search(tout, s, instance)) {
    const std::string inst = minuscules(s[2].str());
    const std::string base = inst.substr(inst.rfind('.') == std::string::npos ? 0 : inst.rfind('.') + 1);
    d.composant = n.instances.count(base) ? n.instances.at(base) : n.instances.count(inst) ? n.instances.at(inst) : s[2].str();
  }
  if (std::regex_search(tout, s, instant)) d.instant = std::strtod(s[1].str().c_str(), nullptr);
  if (d.noeud.empty() && std::regex_search(tout, s, singulier)) d.noeud = s[1].str();
  if (!d.noeud.empty()) d.reseau = reseauDuNoeud(n, d.noeud);
  const std::string ou = std::string(d.reseau.empty() ? (d.noeud.empty() ? std::string() : ", autour du nœud « " + d.noeud + " »") : ", autour du réseau « " + d.reseau + " »") +
                         (d.composant.empty() ? std::string() : ", sur " + d.composant);
  if (m.find("timestep too small") != std::string::npos) {
    d.genre = "pas";
    const bool depart = m.find("initial timepoint") != std::string::npos || m.find("transient op failed") != std::string::npos;
    d.cause = depart ? "Le point de départ du transitoire n’a pas pu être calculé : le pas de calcul s’est effondré (« timestep too small »)" + ou + "."
                     : "Le pas de calcul s’est effondré (« timestep too small »)" + (d.instant >= 0 ? " à t = " + duree(d.instant) : std::string()) + ou +
                           " : le circuit change plus vite que le moteur ne sait le suivre.";
    d.remedes = {"gminsteps", "srcsteps", "rshunt", "gear", "pas", "reltol"};
  } else if (m.find("singular matrix") != std::string::npos) {
    d.genre = "singulier";
    d.cause = "Matrice singulière" + ou + " : un nœud n’a pas de tension définie (nœud flottant, boucle de sources idéales, condensateurs seuls).";
    d.remedes = {"rshunt", "gminsteps", "srcsteps"};
  } else if (m.find("no convergence") != std::string::npos || m.find("gmin stepping failed") != std::string::npos || m.find("source stepping failed") != std::string::npos ||
             m.find("operating point") != std::string::npos) {
    d.genre = "repos";
    d.cause = "Le point de repos est introuvable" + ou + " : le moteur ne converge pas vers une solution continue.";
    d.remedes = {"gminsteps", "srcsteps", "rshunt", "uic", "reltol"};
  } else if (std::regex_search(tout, s, modele)) {
    d.genre = "modèle";
    d.cause = "Le modèle « " + s[1].str() + " » est introuvable" + ou + " : son texte manque à l’article ou au champ Sim.Bibliotheque.";
  } else if (m.find("error on line") != std::string::npos || m.find("unknown") != std::string::npos) {
    d.genre = "syntaxe";
    // La ligne fautive suit « Error on line » : son premier mot désigne l'élément
    for (std::size_t i = 0; i + 1 < r.console.size(); ++i)
      if (minuscules(r.console[i]).find("error on line") != std::string::npos) {
        std::istringstream mots(r.console[i + 1]);
        std::string w;
        mots >> w;
        if (minuscules(w) == "stderr" || minuscules(w) == "stdout") mots >> w;
        const std::string base = minuscules(w.substr(w.rfind('.') == std::string::npos ? 0 : w.rfind('.') + 1));
        if (n.instances.count(base)) d.composant = n.instances.at(base);
        break;
      }
    d.cause = "Ligne refusée par ngspice" + std::string(d.composant.empty() ? std::string() : " (écrite pour " + d.composant + ")") + " : " + r.erreur;
  } else {
    d.genre = "autre";
    d.cause = r.erreur.empty() ? std::string("ngspice n’a rendu aucun résultat") : r.erreur;
  }
  return d;
}

std::string libelleRemede(const std::string& x) {
  if (x == "gminsteps") return "Pas de gmin progressifs (gminsteps)";
  if (x == "srcsteps") return "Montée progressive des sources";
  if (x == "rshunt") return "1 GΩ vers la masse sur chaque nœud (rshunt)";
  if (x == "gear") return "Méthode d’intégration de Gear";
  if (x == "pas") return "Pas maximal divisé par dix";
  if (x == "reltol") return "Tolérance relative relâchée à 1 %";
  if (x == "uic") return "Départ sans point de repos (uic)";
  return x;
}

bool appliquerRemede(OptionsSimulation& o, const std::string& x, const AnalyseSpice& a) {
  auto mettre = [&](const char* cle, const std::string& v) {
    const auto it = o.moteur.find(cle);
    if (it != o.moteur.end() && it->second == v) return false;
    o.moteur[cle] = v;
    return true;
  };
  if (x == "gminsteps") return mettre("gminsteps", "100");
  if (x == "srcsteps") {
    const bool b = mettre("srcsteps", "100");
    const bool r = !o.rampeSources;
    o.rampeSources = true;
    return b || r;
  }
  if (x == "rshunt") return mettre("rshunt", "1e9");
  if (x == "gear") return mettre("method", "gear");
  if (x == "reltol") return mettre("reltol", "0.01");
  if (x == "uic") {
    const bool r = !o.sansPointRepos;
    o.sansPointRepos = true;
    return r;
  }
  if (x == "pas") {
    const double base = o.pasMax > 0 ? o.pasMax : a.pas;
    o.pasMax = base / 10;
    return true;
  }
  return false;
}

EssaiRemedes simulerAvecRemedes(const ConceptionElec& c, const AnalyseSpice& a, OptionsSimulation& o, const std::atomic<bool>* arret) {
  EssaiRemedes e;
  e.netlist = netlistSpice(c, a, o);
  if (!e.netlist.valide()) {
    e.resultat.erreur = e.netlist.erreurs.front();
    return e;
  }
  e.resultat = simuler(e.netlist, arret);
  if (e.resultat.reussi() || e.resultat.interrompu) return e;
  e.diagnostic = analyserEchec(e.resultat, e.netlist);
  for (const auto& x : e.diagnostic.remedes) {
    if (arret && arret->load()) break;
    OptionsSimulation essai = o;
    if (!appliquerRemede(essai, x, a)) continue;
    e.essayes.push_back(x);
    const NetlistSpice n = netlistSpice(c, a, essai);
    if (!n.valide()) continue;
    ResultatSimulation r = simuler(n, arret);
    if (r.reussi()) {
      essai.remedes.push_back(libelleRemede(x) + " — après « " + e.diagnostic.cause + " »");
      o = essai;
      e.retenu = x;
      e.netlist = n;
      e.resultat = std::move(r);
      return e;
    }
  }
  return e;
}

// ---------- Courbes calculées ----------
namespace {
struct Valeur {
  std::vector<Cx> v;  // une seule valeur : un scalaire
};
class Evaluateur {
 public:
  Evaluateur(const std::string& t, const ResultatSimulation& r, const NetlistSpice& n) : m_t(t), m_r(r), m_n(n) {
    if (!r.echelle.empty())
      if (const auto* e = r.reel(r.echelle)) m_x = *e;
  }
  Valeur evaluer() {
    Valeur v = expression();
    espaces();
    if (m_i < m_t.size()) erreur("caractère inattendu « " + std::string(1, m_t[m_i]) + " »");
    return v;
  }
  const std::vector<double>& echelle() const { return m_x; }

 private:
  [[noreturn]] void erreur(const std::string& e) { throw std::runtime_error(e); }
  void espaces() {
    while (m_i < m_t.size() && std::isspace(static_cast<unsigned char>(m_t[m_i]))) ++m_i;
  }
  bool lire(char c) {
    espaces();
    if (m_i < m_t.size() && m_t[m_i] == c) {
      ++m_i;
      return true;
    }
    return false;
  }
  static Valeur binaire(const Valeur& a, const Valeur& b, const std::function<Cx(Cx, Cx)>& f) {
    Valeur r;
    const std::size_t n = std::max(a.v.size(), b.v.size());
    if (a.v.size() > 1 && b.v.size() > 1 && a.v.size() != b.v.size()) throw std::runtime_error("vecteurs de longueurs différentes");
    r.v.resize(n);
    for (std::size_t i = 0; i < n; ++i) r.v[i] = f(a.v[a.v.size() == 1 ? 0 : i], b.v[b.v.size() == 1 ? 0 : i]);
    return r;
  }
  static Valeur unaire(const Valeur& a, const std::function<Cx(Cx)>& f) {
    Valeur r;
    r.v.reserve(a.v.size());
    for (const auto& x : a.v) r.v.push_back(f(x));
    return r;
  }
  Valeur expression() {
    Valeur a = terme();
    for (;;) {
      if (lire('+')) a = binaire(a, terme(), [](Cx x, Cx y) { return x + y; });
      else if (lire('-')) a = binaire(a, terme(), [](Cx x, Cx y) { return x - y; });
      else return a;
    }
  }
  Valeur terme() {
    Valeur a = facteur();
    for (;;) {
      if (lire('*')) a = binaire(a, facteur(), [](Cx x, Cx y) { return x * y; });
      else if (lire('/')) a = binaire(a, facteur(), [](Cx x, Cx y) { return std::abs(y) == 0 ? Cx(std::numeric_limits<double>::quiet_NaN(), 0) : x / y; });
      else return a;
    }
  }
  Valeur facteur() {
    if (lire('-')) return unaire(facteur(), [](Cx x) { return -x; });
    if (lire('+')) return facteur();
    Valeur a = primaire();
    if (lire('^')) {
      const Valeur b = facteur();
      a = binaire(a, b, [](Cx x, Cx y) { return y.imag() == 0 && x.imag() == 0 && x.real() >= 0 ? Cx(std::pow(x.real(), y.real()), 0) : std::pow(x, y); });
    }
    return a;
  }
  std::string identifiant() {
    espaces();
    std::string s;
    while (m_i < m_t.size() && (std::isalnum(static_cast<unsigned char>(m_t[m_i])) || m_t[m_i] == '_' || m_t[m_i] == '#' || m_t[m_i] == '@' || m_t[m_i] == '[' || m_t[m_i] == ']' ||
                                m_t[m_i] == '.'))
      s += m_t[m_i++];
    return s;
  }
  // Contenu brut entre parenthèses (V(a,b), I(R1))
  std::string brut() {
    if (!lire('(')) erreur("parenthèse attendue");
    std::string s;
    int p = 1;
    while (m_i < m_t.size()) {
      const char ch = m_t[m_i++];
      if (ch == '(') ++p;
      if (ch == ')' && --p == 0) return s;
      s += ch;
    }
    erreur("parenthèse non fermée");
  }
  Valeur vecteur(const std::string& nom) {
    Valeur r;
    if (const auto* x = m_r.reel(nom)) {
      for (double v : *x) r.v.emplace_back(v, 0);
      return r;
    }
    if (const auto* z = m_r.complexe(nom)) {
      r.v = *z;
      return r;
    }
    erreur("vecteur « " + nom + " » absent du résultat");
  }
  Valeur tension(const std::string& reseau) {
    std::string nom = reseau;
    while (!nom.empty() && std::isspace(static_cast<unsigned char>(nom.back()))) nom.pop_back();
    while (!nom.empty() && std::isspace(static_cast<unsigned char>(nom.front()))) nom.erase(nom.begin());
    std::string nd;
    for (const auto& [r, x] : m_n.noeuds)
      if (minuscules(r) == minuscules(nom)) nd = x;
    if (nd == "0") {
      Valeur z;
      z.v = {Cx(0, 0)};
      return z;
    }
    if (nd.empty()) nd = nom;
    return vecteur("v(" + nd + ")");
  }
  Valeur courant(const std::string& ref) {
    std::string nom = ref;
    while (!nom.empty() && std::isspace(static_cast<unsigned char>(nom.back()))) nom.pop_back();
    std::vector<std::string> essais;
    for (const auto& [inst, r] : m_n.instances)
      if (minuscules(r) == minuscules(nom)) essais.push_back(inst);
    essais.push_back(minuscules(nom));
    for (const auto& inst : essais)
      for (const std::string& v : {inst + "#branch", "@" + inst + "[i]", "i(" + inst + ")"})
        if (m_r.reel(v) || m_r.complexe(v)) return vecteur(v);
    erreur("courant de « " + nom + " » non enregistré (options savecurrents)");
  }
  std::vector<Valeur> args() {
    if (!lire('(')) erreur("parenthèse attendue");
    std::vector<Valeur> v;
    if (lire(')')) return v;
    for (;;) {
      v.push_back(expression());
      if (lire(';') || lire(',')) continue;
      if (lire(')')) return v;
      erreur("« ; » ou « ) » attendu");
    }
  }
  Valeur primaire() {
    espaces();
    if (lire('(')) {
      Valeur v = expression();
      if (!lire(')')) erreur("« ) » attendu");
      return v;
    }
    if (m_i < m_t.size() && (std::isdigit(static_cast<unsigned char>(m_t[m_i])) || m_t[m_i] == '.')) {
      const char* debut = m_t.c_str() + m_i;
      char* fin = nullptr;
      double x = std::strtod(debut, &fin);
      m_i += static_cast<std::size_t>(fin - debut);
      // Suffixes SPICE
      if (m_i < m_t.size()) {
        const char s = static_cast<char>(std::tolower(static_cast<unsigned char>(m_t[m_i])));
        const std::string reste = minuscules(m_t.substr(m_i, 3));
        if (reste == "meg") x *= 1e6, m_i += 3;
        else if (s == 'k') x *= 1e3, ++m_i;
        else if (s == 'm') x *= 1e-3, ++m_i;
        else if (s == 'u') x *= 1e-6, ++m_i;
        else if (s == 'n') x *= 1e-9, ++m_i;
        else if (s == 'p') x *= 1e-12, ++m_i;
        else if (s == 'g') x *= 1e9, ++m_i;
      }
      Valeur v;
      v.v = {Cx(x, 0)};
      return v;
    }
    const std::string id = identifiant();
    if (id.empty()) erreur("expression incomplète");
    const std::string f = minuscules(id);
    espaces();
    const bool appel = m_i < m_t.size() && m_t[m_i] == '(';
    if (f == "v" && appel) {
      const std::string b = brut();
      const auto virgule = b.find(',');
      if (virgule == std::string::npos) return tension(b);
      return binaire(tension(b.substr(0, virgule)), tension(b.substr(virgule + 1)), [](Cx x, Cx y) { return x - y; });
    }
    if (f == "i" && appel) return courant(brut());
    if (!appel) {
      if (f == "pi") {
        Valeur v;
        v.v = {Cx(std::numbers::pi, 0)};
        return v;
      }
      if (f == "j") {
        Valeur v;
        v.v = {Cx(0, 1)};
        return v;
      }
      return vecteur(id);
    }
    const std::vector<Valeur> a = args();
    auto un = [&]() -> const Valeur& {
      if (a.empty()) erreur(id + "() attend un argument");
      return a[0];
    };
    if (f == "db") return unaire(un(), [](Cx x) { return Cx(20 * std::log10(std::max(std::abs(x), 1e-300)), 0); });
    if (f == "mag" || f == "abs") return unaire(un(), [](Cx x) { return Cx(std::abs(x), 0); });
    if (f == "ph" || f == "phase") return unaire(un(), [](Cx x) { return Cx(std::arg(x) * 180 / std::numbers::pi, 0); });
    if (f == "re" || f == "real") return unaire(un(), [](Cx x) { return Cx(x.real(), 0); });
    if (f == "im" || f == "imag") return unaire(un(), [](Cx x) { return Cx(x.imag(), 0); });
    if (f == "sqrt") return unaire(un(), [](Cx x) { return x.imag() == 0 && x.real() >= 0 ? Cx(std::sqrt(x.real()), 0) : std::sqrt(x); });
    if (f == "log10") return unaire(un(), [](Cx x) { return Cx(std::log10(std::abs(x)), 0); });
    if (f == "ln" || f == "log") return unaire(un(), [](Cx x) { return x.imag() == 0 && x.real() > 0 ? Cx(std::log(x.real()), 0) : std::log(x); });
    if (f == "exp") return unaire(un(), [](Cx x) { return std::exp(x); });
    if (f == "max" || f == "min") {
      if (a.size() == 2) return binaire(a[0], a[1], [f](Cx x, Cx y) { return (f == "max") == (x.real() >= y.real()) ? x : y; });
      Valeur v;
      double m = f == "max" ? -1e300 : 1e300;
      for (const auto& x : un().v) m = f == "max" ? std::max(m, x.real()) : std::min(m, x.real());
      v.v = {Cx(m, 0)};
      return v;
    }
    if (f == "deriv" || f == "integ" || f == "moy") {
      const Valeur& y = un();
      if (m_x.size() != y.v.size()) erreur(id + "() a besoin de l’échelle du résultat");
      Valeur r;
      r.v.resize(y.v.size());
      const std::size_t n = y.v.size();
      if (f == "deriv")
        for (std::size_t i = 0; i < n; ++i) {
          const std::size_t i0 = i == 0 ? 0 : i - 1, i1 = i + 1 < n ? i + 1 : n - 1;
          const double dx = m_x[i1] - m_x[i0];
          r.v[i] = dx == 0 ? Cx(0, 0) : (y.v[i1] - y.v[i0]) / dx;
        }
      else if (f == "integ")
        for (std::size_t i = 1; i < n; ++i) r.v[i] = r.v[i - 1] + (y.v[i] + y.v[i - 1]) * 0.5 * (m_x[i] - m_x[i - 1]);
      else {
        // Moyenne glissante sur la durée donnée (tout l'enregistrement sans durée)
        const double d = a.size() > 1 && !a[1].v.empty() ? a[1].v[0].real() : (n ? m_x.back() - m_x.front() : 0);
        std::vector<Cx> integrale(n);
        for (std::size_t i = 1; i < n; ++i) integrale[i] = integrale[i - 1] + (y.v[i] + y.v[i - 1]) * 0.5 * (m_x[i] - m_x[i - 1]);
        std::size_t debut = 0;
        for (std::size_t i = 0; i < n; ++i) {
          while (debut < i && m_x[i] - m_x[debut] > d) ++debut;
          const double dx = m_x[i] - m_x[debut];
          r.v[i] = dx > 0 ? (integrale[i] - integrale[debut]) / dx : y.v[i];
        }
      }
      return r;
    }
    erreur("fonction « " + id + " » inconnue");
  }
  const std::string& m_t;
  const ResultatSimulation& m_r;
  const NetlistSpice& m_n;
  std::vector<double> m_x;
  std::size_t m_i = 0;
};
}  // namespace

std::vector<double> CourbeCalculee::reels() const {
  std::vector<double> r;
  r.reserve(y.size());
  for (const auto& z : y) r.push_back(z.real());
  return r;
}
std::vector<double> CourbeCalculee::modules() const {
  std::vector<double> r;
  r.reserve(y.size());
  for (const auto& z : y) r.push_back(std::abs(z));
  return r;
}

CourbeCalculee evaluerCourbe(const std::string& expression, const ResultatSimulation& r, const NetlistSpice& n) {
  CourbeCalculee c;
  try {
    Evaluateur e(expression, r, n);
    const Valeur v = e.evaluer();
    c.x = e.echelle();
    c.y = v.v;
    if (c.y.size() == 1 && c.x.size() > 1) c.y.assign(c.x.size(), c.y[0]);
    if (c.x.size() != c.y.size()) {
      c.x.clear();
      for (std::size_t i = 0; i < c.y.size(); ++i) c.x.push_back(static_cast<double>(i));
    }
    for (const auto& z : c.y) c.complexe |= z.imag() != 0;
  } catch (const std::exception& ex) {
    c.erreur = std::string("« ") + expression + " » : " + ex.what();
  }
  return c;
}

// ---------- Mesures automatiques ----------
std::string nomMesure(GenreMesure g) {
  switch (g) {
    case GenreMesure::Coupure: return "coupure";
    case GenreMesure::Bande: return "bande";
    case GenreMesure::Q: return "Q";
    case GenreMesure::Periode: return "période";
    case GenreMesure::Frequence: return "fréquence";
    case GenreMesure::Montee: return "montée";
    case GenreMesure::Descente: return "descente";
    case GenreMesure::Depassement: return "dépassement";
    case GenreMesure::Maximum: return "maximum";
    case GenreMesure::Minimum: return "minimum";
    case GenreMesure::Moyenne: return "moyenne";
    case GenreMesure::Efficace: return "efficace";
    case GenreMesure::ValeurA: return "valeur à";
  }
  return {};
}
std::optional<GenreMesure> mesureDepuisNom(const std::string& nom) {
  for (int i = 0; i <= static_cast<int>(GenreMesure::ValeurA); ++i)
    if (nomMesure(static_cast<GenreMesure>(i)) == nom) return static_cast<GenreMesure>(i);
  return std::nullopt;
}

namespace {
// Abscisse où y franchit « niveau », interpolée (en logarithme de x si « logX »), depuis l'indice i (montant ou non)
double franchissement(const std::vector<double>& x, const std::vector<double>& y, std::size_t i, double niveau, bool logX) {
  const double a = y[i - 1], b = y[i];
  const double t = b == a ? 0 : (niveau - a) / (b - a);
  if (logX && x[i - 1] > 0 && x[i] > 0) return std::pow(10.0, std::log10(x[i - 1]) + t * (std::log10(x[i]) - std::log10(x[i - 1])));
  return x[i - 1] + t * (x[i] - x[i - 1]);
}
// Instants des passages montants au niveau médian, avec une hystérésis de 10 % de la plage
std::vector<double> frontsMontants(const std::vector<double>& x, const std::vector<double>& y) {
  std::vector<double> r;
  if (y.size() < 3) return r;
  const auto [mn, mx] = std::minmax_element(y.begin(), y.end());
  const double plage = *mx - *mn, milieu = (*mn + *mx) / 2, h = 0.1 * plage;
  if (plage <= 0) return r;
  bool bas = y[0] < milieu - h;
  for (std::size_t i = 1; i < y.size(); ++i) {
    if (y[i] < milieu - h) bas = true;
    if (bas && y[i - 1] < milieu && y[i] >= milieu) {
      r.push_back(franchissement(x, y, i, milieu, false));
      bas = false;
    }
  }
  return r;
}
}  // namespace

ResultatMesure mesurer(const MesureAuto& m, const ResultatSimulation& r, const NetlistSpice& n) {
  ResultatMesure res;
  res.nom = nomMesure(m.genre) + " de " + m.courbe;
  std::string expr = m.courbe;
  if (expr.find('(') == std::string::npos) expr = "V(" + expr + ")";
  const CourbeCalculee c = evaluerCourbe(expr, r, n);
  if (!c.valide() || c.x.size() != c.y.size() || c.y.size() < 2) return res;
  const bool frequentiel = r.echelle == "frequency";
  const std::vector<double>& x = c.x;
  // En fréquence : le module ; sinon la partie réelle
  const std::vector<double> y = frequentiel || c.complexe ? c.modules() : c.reels();
  auto unite = [&]() { return frequentiel ? std::string("Hz") : r.echelle == "time" ? std::string("s") : std::string(); };
  switch (m.genre) {
    case GenreMesure::Coupure:
    case GenreMesure::Bande:
    case GenreMesure::Q: {
      // −3 dB sous le maximum, de part et d'autre, interpolé en logarithme de la fréquence
      const auto im = static_cast<std::size_t>(std::max_element(y.begin(), y.end()) - y.begin());
      const double seuil = y[im] / std::sqrt(2.0);
      double haute = 0, basse = 0;
      for (std::size_t i = im + 1; i < y.size(); ++i)
        if (y[i - 1] >= seuil && y[i] < seuil) {
          haute = franchissement(x, y, i, seuil, frequentiel);
          break;
        }
      for (std::size_t i = im; i > 0; --i)
        if (y[i] >= seuil && y[i - 1] < seuil) {
          basse = franchissement(x, y, i, seuil, frequentiel);
          break;
        }
      if (m.genre == GenreMesure::Coupure) {
        res.valeur = haute > 0 ? haute : basse;
        res.valide = res.valeur > 0;
        res.unite = unite();
      } else if (m.genre == GenreMesure::Bande) {
        res.valeur = haute > 0 ? haute - basse : 0;
        res.valide = haute > 0;
        res.unite = unite();
      } else {
        const double bande = haute > 0 && basse > 0 ? haute - basse : 0;
        res.valeur = bande > 0 ? x[im] / bande : 0;
        res.valide = bande > 0;
      }
      break;
    }
    case GenreMesure::Periode:
    case GenreMesure::Frequence: {
      const std::vector<double> f = frontsMontants(x, y);
      if (f.size() < 2) break;
      const double p = (f.back() - f.front()) / static_cast<double>(f.size() - 1);
      res.valeur = m.genre == GenreMesure::Periode ? p : 1 / p;
      res.unite = m.genre == GenreMesure::Periode ? "s" : "Hz";
      res.valide = p > 0;
      break;
    }
    case GenreMesure::Montee:
    case GenreMesure::Descente: {
      const auto [mn, mx] = std::minmax_element(y.begin(), y.end());
      const double b = *mn + 0.1 * (*mx - *mn), h = *mn + 0.9 * (*mx - *mn);
      const bool montee = m.genre == GenreMesure::Montee;
      double t10 = -1, t90 = -1;
      for (std::size_t i = 1; i < y.size() && t90 < 0; ++i) {
        if (montee) {
          if (t10 < 0 && y[i - 1] < b && y[i] >= b) t10 = franchissement(x, y, i, b, false);
          if (t10 >= 0 && y[i - 1] < h && y[i] >= h) t90 = franchissement(x, y, i, h, false);
        } else {
          if (t10 < 0 && y[i - 1] > h && y[i] <= h) t10 = franchissement(x, y, i, h, false);
          if (t10 >= 0 && y[i - 1] > b && y[i] <= b) t90 = franchissement(x, y, i, b, false);
        }
      }
      if (t10 >= 0 && t90 >= 0) res.valeur = t90 - t10, res.valide = true, res.unite = "s";
      break;
    }
    case GenreMesure::Depassement: {
      // Rapporté au palier final (moyenne des derniers 10 %) depuis la valeur de départ, en %
      const std::size_t d = y.size() - std::max<std::size_t>(1, y.size() / 10);
      double fin = 0;
      for (std::size_t i = d; i < y.size(); ++i) fin += y[i];
      fin /= static_cast<double>(y.size() - d);
      const double debut = y.front(), saut = fin - debut;
      if (std::abs(saut) < 1e-30) break;
      const double extreme = saut > 0 ? *std::max_element(y.begin(), y.end()) : *std::min_element(y.begin(), y.end());
      res.valeur = 100 * (extreme - fin) / saut;
      res.valide = true;
      res.unite = "%";
      break;
    }
    case GenreMesure::Maximum: res.valeur = *std::max_element(y.begin(), y.end()), res.valide = true; break;
    case GenreMesure::Minimum: res.valeur = *std::min_element(y.begin(), y.end()), res.valide = true; break;
    case GenreMesure::Moyenne:
    case GenreMesure::Efficace: {
      // Sur des périodes entières s'il y en a, sinon sur tout l'enregistrement
      double a = x.front(), b = x.back();
      if (const std::vector<double> f = frontsMontants(x, y); f.size() >= 2) a = f.front(), b = f.back();
      double s = 0, s2 = 0, duree0 = 0;
      for (std::size_t i = 1; i < y.size(); ++i) {
        const double x0 = std::max(a, x[i - 1]), x1 = std::min(b, x[i]);
        if (x1 <= x0) continue;
        const double ya = valeurA(x, y, x0), yb = valeurA(x, y, x1);
        s += (ya + yb) / 2 * (x1 - x0);
        s2 += (ya * ya + ya * yb + yb * yb) / 3 * (x1 - x0);
        duree0 += x1 - x0;
      }
      if (duree0 <= 0) break;
      res.valeur = m.genre == GenreMesure::Moyenne ? s / duree0 : std::sqrt(s2 / duree0);
      res.valide = true;
      break;
    }
    case GenreMesure::ValeurA: res.valeur = valeurA(x, y, m.parametre), res.valide = true; break;
  }
  return res;
}

// ---------- Comparaison ----------
std::string Comparaison::verdict() const { return reussie ? "réussi" : "échoué"; }

Comparaison comparerCourbes(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& xr, const std::vector<double>& yr, double bande, GenreBande g,
                            const std::vector<double>* haut) {
  Comparaison c;
  if (x.size() != y.size() || xr.size() != yr.size() || xr.size() < 2 || x.empty()) return c;
  const double lo = std::min(xr.front(), xr.back()), hi = std::max(xr.front(), xr.back());
  double echelle = 0;
  for (double v : yr) echelle = std::max(echelle, std::abs(v));
  bool dedans = false;
  std::size_t debut = 0;
  bool compare = false;
  for (std::size_t i = 0; i < x.size(); ++i) {
    if (x[i] < lo || x[i] > hi) continue;
    compare = true;
    const double ref = valeurA(xr, yr, x[i]);
    double ecart = 0;
    bool hors = false;
    if (g == GenreBande::Enveloppe && haut && haut->size() == xr.size()) {
      const double h = valeurA(xr, *haut, x[i]);
      ecart = y[i] < ref ? ref - y[i] : y[i] > h ? y[i] - h : 0;
      hors = ecart > 0;
    } else if (g == GenreBande::Absolue) {
      ecart = std::abs(y[i] - ref);
      hors = ecart > bande;
    } else {
      // Relative, avec un plancher d'un millième de l'échelle de la référence pour les passages par zéro
      const double base = std::max(std::abs(ref), 1e-3 * echelle);
      ecart = base > 0 ? std::abs(y[i] - ref) / base : 0;
      hors = ecart > bande;
    }
    if (ecart > c.ecartMax) c.ecartMax = ecart, c.xPire = x[i];
    if (hors && !dedans) dedans = true, debut = i;
    if (!hors && dedans) {
      c.horsBande.push_back({debut, i - 1});
      dedans = false;
    }
  }
  if (dedans) c.horsBande.push_back({debut, x.size() - 1});
  c.reussie = compare && c.horsBande.empty();
  return c;
}

// ---------- Classeur ----------
const ResultatFige* ClasseurSimulation::fige(const std::string& nom) const {
  for (const auto& f : figes)
    if (f.nom == nom) return &f;
  return nullptr;
}

namespace {
std::string txt(const J& o, const char* k) { return o.is_object() && o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); }
double reelJ(const J& o, const char* k, double d) { return o.is_object() && o.contains(k) && o[k].is_number() ? o[k].get<double>() : d; }
bool boolJ(const J& o, const char* k) { return o.is_object() && o.contains(k) && o[k].is_boolean() && o[k].get<bool>(); }
const char* nomType(AnalyseSpice::Type t) {
  switch (t) {
    case AnalyseSpice::Type::PointDeRepos: return "repos";
    case AnalyseSpice::Type::Continu: return "continu";
    case AnalyseSpice::Type::Alternatif: return "alternatif";
    case AnalyseSpice::Type::Transitoire: return "transitoire";
  }
  return "transitoire";
}
J analyseVersJson(const AnalyseSpice& a) {
  return {{"type", nomType(a.type)}, {"pas", a.pas}, {"duree", a.duree}, {"debut", a.debut}, {"source", a.source}, {"dcDebut", a.dcDebut}, {"dcFin", a.dcFin},
          {"dcPas", a.dcPas}, {"pointsParDecade", a.pointsParDecade}, {"fDebut", a.fDebut}, {"fFin", a.fFin}};
}
AnalyseSpice analyseDepuisJson(const J& o) {
  AnalyseSpice a;
  const std::string t = txt(o, "type");
  a.type = t == "repos" ? AnalyseSpice::Type::PointDeRepos : t == "continu" ? AnalyseSpice::Type::Continu : t == "alternatif" ? AnalyseSpice::Type::Alternatif : AnalyseSpice::Type::Transitoire;
  a.pas = reelJ(o, "pas", a.pas);
  a.duree = reelJ(o, "duree", a.duree);
  a.debut = reelJ(o, "debut", a.debut);
  a.source = txt(o, "source");
  a.dcDebut = reelJ(o, "dcDebut", a.dcDebut);
  a.dcFin = reelJ(o, "dcFin", a.dcFin);
  a.dcPas = reelJ(o, "dcPas", a.dcPas);
  a.pointsParDecade = static_cast<int>(reelJ(o, "pointsParDecade", a.pointsParDecade));
  a.fDebut = reelJ(o, "fDebut", a.fDebut);
  a.fFin = reelJ(o, "fFin", a.fFin);
  return a;
}
J tensionsVersJson(const std::map<std::string, double>& m) {
  J o = J::object();
  for (const auto& [k, v] : m) o[k] = v;
  return o;
}
std::map<std::string, double> tensionsDepuisJson(const J& o) {
  std::map<std::string, double> m;
  if (o.is_object())
    for (auto it = o.begin(); it != o.end(); ++it)
      if (it.value().is_number()) m[it.key()] = it.value().get<double>();
  return m;
}
J parametresVersJson(const std::vector<ParametreGlobal>& p) {
  J t = J::array();
  for (const auto& x : p) t.push_back({{"nom", x.nom}, {"expression", x.expression}, {"unite", x.unite}});
  return t;
}
std::vector<ParametreGlobal> parametresDepuisJson(const J& t) {
  std::vector<ParametreGlobal> p;
  if (t.is_array())
    for (const auto& x : t) p.push_back({txt(x, "nom"), txt(x, "expression"), txt(x, "unite")});
  return p;
}
J optionsVersJson(const OptionsSimulation& o) {
  J x = J::object();
  if (!o.compatibilite.empty()) x["compatibilite"] = o.compatibilite;
  if (!o.conditionsInitiales.empty()) x["conditionsInitiales"] = tensionsVersJson(o.conditionsInitiales);
  if (!o.pointDepart.empty()) x["pointDepart"] = tensionsVersJson(o.pointDepart);
  if (o.sansPointRepos) x["sansPointRepos"] = true;
  if (!o.moteur.empty()) x["moteur"] = o.moteur;
  if (o.pasMax > 0) x["pasMax"] = o.pasMax;
  if (o.rampeSources) x["rampeSources"] = true;
  if (!o.remedes.empty()) x["remedes"] = o.remedes;
  if (!o.composants.empty()) x["composants"] = std::vector<std::string>(o.composants.begin(), o.composants.end());
  if (!o.liaisons.empty()) {
    J t = J::array();
    for (const auto& l : o.liaisons)
      t.push_back({{"reseau", l.reseau}, {"bouclage", l.bouclage == BouclageLiaison::Source ? "source" : l.bouclage == BouclageLiaison::Charge ? "charge" : "rien"}, {"valeur", l.valeur}, {"ac", l.ac}});
    x["liaisons"] = t;
  }
  if (o.autoriserControle) x["autoriserControle"] = true;
  if (!o.parametres.empty()) x["parametres"] = parametresVersJson(o.parametres);
  return x;
}
OptionsSimulation optionsDepuisJson(const J& x) {
  OptionsSimulation o;
  if (!x.is_object()) return o;
  o.compatibilite = txt(x, "compatibilite");
  if (x.contains("conditionsInitiales")) o.conditionsInitiales = tensionsDepuisJson(x["conditionsInitiales"]);
  if (x.contains("pointDepart")) o.pointDepart = tensionsDepuisJson(x["pointDepart"]);
  o.sansPointRepos = boolJ(x, "sansPointRepos");
  if (x.contains("moteur") && x["moteur"].is_object())
    for (auto it = x["moteur"].begin(); it != x["moteur"].end(); ++it)
      if (it.value().is_string()) o.moteur[it.key()] = it.value().get<std::string>();
  o.pasMax = std::max(0.0, reelJ(x, "pasMax", 0));
  o.rampeSources = boolJ(x, "rampeSources");
  if (x.contains("remedes") && x["remedes"].is_array())
    for (const auto& r : x["remedes"])
      if (r.is_string()) o.remedes.push_back(r.get<std::string>());
  if (x.contains("composants") && x["composants"].is_array())
    for (const auto& r : x["composants"])
      if (r.is_string()) o.composants.insert(r.get<std::string>());
  if (x.contains("liaisons") && x["liaisons"].is_array())
    for (const auto& l : x["liaisons"]) {
      LiaisonCoupee c;
      c.reseau = txt(l, "reseau");
      const std::string b = txt(l, "bouclage");
      c.bouclage = b == "source" ? BouclageLiaison::Source : b == "charge" ? BouclageLiaison::Charge : BouclageLiaison::Rien;
      c.valeur = reelJ(l, "valeur", 0);
      c.ac = reelJ(l, "ac", 0);
      o.liaisons.push_back(c);
    }
  o.autoriserControle = boolJ(x, "autoriserControle");
  if (x.contains("parametres")) o.parametres = parametresDepuisJson(x["parametres"]);
  return o;
}
J vecteurVersJson(const std::vector<double>& v) {
  J t = J::array();
  for (double x : v) t.push_back(x);
  return t;
}
std::vector<double> vecteurDepuisJson(const J& t) {
  std::vector<double> v;
  if (t.is_array())
    for (const auto& x : t) v.push_back(x.is_number() ? x.get<double>() : 0.0);
  return v;
}
}  // namespace

J classeurVersJson(const ClasseurSimulation& k) {
  J o = {{"format", FORMAT_CLASSEUR}};
  if (!k.parametres.empty()) o["parametres"] = parametresVersJson(k.parametres);
  J on = J::array();
  for (const auto& x : k.onglets) {
    J y = {{"nom", x.nom}, {"analyse", analyseVersJson(x.analyse)}};
    if (const J op = optionsVersJson(x.options); !op.empty()) y["options"] = op;
    if (!x.sondes.empty()) y["sondes"] = x.sondes;
    if (!x.expressions.empty()) y["expressions"] = x.expressions;
    if (!x.mesures.empty()) {
      J t = J::array();
      for (const auto& m : x.mesures) t.push_back({{"genre", nomMesure(m.genre)}, {"courbe", m.courbe}, {"parametre", m.parametre}});
      y["mesures"] = t;
    }
    if (!x.reference.empty()) {
      y["reference"] = x.reference;
      y["courbeReference"] = x.courbeReference;
      y["bande"] = x.bande;
      y["genreBande"] = x.genreBande == GenreBande::Absolue ? "absolue" : x.genreBande == GenreBande::Enveloppe ? "enveloppe" : "relative";
    }
    if (!x.verdict.empty()) y["verdict"] = x.verdict;
    on.push_back(y);
  }
  if (!on.empty()) o["onglets"] = on;
  J fi = J::array();
  for (const auto& f : k.figes) {
    J y = {{"nom", f.nom}, {"onglet", f.onglet}, {"date", f.date}, {"echelle", f.echelle}};
    J re = J::object();
    for (const auto& [n, v] : f.reels) re[n] = vecteurVersJson(v);
    y["reels"] = re;
    J co = J::object();
    for (const auto& [n, v] : f.complexes) {
      std::vector<double> a, b;
      for (const auto& z : v) a.push_back(z.real()), b.push_back(z.imag());
      co[n] = {{"re", vecteurVersJson(a)}, {"im", vecteurVersJson(b)}};
    }
    if (!co.empty()) y["complexes"] = co;
    if (!f.noeuds.empty()) y["noeuds"] = f.noeuds;
    fi.push_back(y);
  }
  if (!fi.empty()) o["figes"] = fi;
  return o;
}

ClasseurSimulation classeurDepuisJson(const J& o, std::vector<std::string>* alertes) {
  ClasseurSimulation k;
  if (!o.is_object()) return k;
  // Format 0 (essais de la 0.33) : un seul onglet en tête, sans « format »
  const int format = o.contains("format") && o["format"].is_number_integer() ? o["format"].get<int>() : 0;
  if (format > FORMAT_CLASSEUR && alertes) alertes->push_back("classeur de simulation d’un format plus récent (" + std::to_string(format) + ") : lu au mieux");
  if (o.contains("parametres")) k.parametres = parametresDepuisJson(o["parametres"]);
  J onglets = o.contains("onglets") ? o["onglets"] : J::array();
  if (format == 0 && o.contains("analyse")) onglets = J::array({o});
  if (onglets.is_array())
    for (const auto& y : onglets) {
      OngletClasseur x;
      x.nom = txt(y, "nom");
      if (x.nom.empty()) x.nom = "Analyse " + std::to_string(k.onglets.size() + 1);
      if (y.contains("analyse")) x.analyse = analyseDepuisJson(y["analyse"]);
      if (y.contains("options")) x.options = optionsDepuisJson(y["options"]);
      for (const char* cle : {"sondes", "expressions"})
        if (y.contains(cle) && y[cle].is_array())
          for (const auto& s : y[cle])
            if (s.is_string()) (std::string(cle) == "sondes" ? x.sondes : x.expressions).push_back(s.get<std::string>());
      if (y.contains("mesures") && y["mesures"].is_array())
        for (const auto& m : y["mesures"])
          if (const auto g = mesureDepuisNom(txt(m, "genre"))) x.mesures.push_back({*g, txt(m, "courbe"), reelJ(m, "parametre", 0)});
      x.reference = txt(y, "reference");
      x.courbeReference = txt(y, "courbeReference");
      x.bande = reelJ(y, "bande", 0.01);
      const std::string g = txt(y, "genreBande");
      x.genreBande = g == "absolue" ? GenreBande::Absolue : g == "enveloppe" ? GenreBande::Enveloppe : GenreBande::Relative;
      x.verdict = txt(y, "verdict");
      k.onglets.push_back(x);
    }
  if (o.contains("figes") && o["figes"].is_array())
    for (const auto& y : o["figes"]) {
      ResultatFige f;
      f.nom = txt(y, "nom");
      f.onglet = txt(y, "onglet");
      f.date = txt(y, "date");
      f.echelle = txt(y, "echelle");
      if (y.contains("reels") && y["reels"].is_object())
        for (auto it = y["reels"].begin(); it != y["reels"].end(); ++it) f.reels[it.key()] = vecteurDepuisJson(it.value());
      if (y.contains("complexes") && y["complexes"].is_object())
        for (auto it = y["complexes"].begin(); it != y["complexes"].end(); ++it) {
          const auto a = vecteurDepuisJson(it.value().contains("re") ? it.value()["re"] : J::array());
          const auto b = vecteurDepuisJson(it.value().contains("im") ? it.value()["im"] : J::array());
          auto& z = f.complexes[it.key()];
          for (std::size_t i = 0; i < a.size(); ++i) z.emplace_back(a[i], i < b.size() ? b[i] : 0.0);
        }
      if (y.contains("noeuds") && y["noeuds"].is_object())
        for (auto it = y["noeuds"].begin(); it != y["noeuds"].end(); ++it)
          if (it.value().is_string()) f.noeuds[it.key()] = it.value().get<std::string>();
      k.figes.push_back(f);
    }
  return k;
}

ClasseurSimulation classeurDe(const ConceptionElec& c, std::vector<std::string>* alertes) {
  const auto it = c.extensions.find("simulation");
  return it == c.extensions.end() ? ClasseurSimulation{} : classeurDepuisJson(it->second, alertes);
}

void ecrireClasseur(ConceptionElec& c, const ClasseurSimulation& k) {
  if (k.vide()) c.extensions.erase("simulation");
  else c.extensions["simulation"] = classeurVersJson(k);
}

ResultatFige figer(const ResultatSimulation& r, const NetlistSpice& n, const std::string& nom, const std::string& onglet, const std::string& date) {
  ResultatFige f;
  f.nom = nom;
  f.onglet = onglet;
  f.date = date;
  f.echelle = r.echelle;
  f.reels = r.reels;
  f.complexes = r.complexes;
  f.noeuds = n.noeuds;
  return f;
}

ResultatSimulation resultatDe(const ResultatFige& f) {
  ResultatSimulation r;
  r.echelle = f.echelle;
  r.reels = f.reels;
  r.complexes = f.complexes;
  return r;
}

NetlistSpice noeudsDe(const ResultatFige& f) {
  NetlistSpice n;
  n.noeuds = f.noeuds;
  return n;
}

std::string csvResultat(const ResultatSimulation& r, const std::vector<std::string>& vecteurs) {
  const ReglagesCsv reg = reglagesCsv();
  auto nombre = [&](double v) {
    std::string s = nb(v);
    if (reg.decimale != '.') std::replace(s.begin(), s.end(), '.', reg.decimale);
    return s;
  };
  std::vector<std::string> noms = vecteurs;
  if (noms.empty()) {
    for (const auto& [k, v] : r.reels)
      if (k != r.echelle) noms.push_back(k);
    for (const auto& [k, v] : r.complexes)
      if (k != r.echelle) noms.push_back(k);
  }
  const std::vector<double>* x = r.reel(r.echelle);
  EcrivainCsv w(reg);
  std::vector<std::string> tete;
  if (x) tete.push_back(r.echelle);
  for (const auto& n : noms) {
    if (r.complexe(n)) {
      tete.push_back("mag(" + n + ")");
      tete.push_back("ph(" + n + ")");
    } else
      tete.push_back(n);
  }
  w.ligne(tete);
  std::size_t lignes = x ? x->size() : 0;
  for (const auto& n : noms) {
    if (const auto* v = r.reel(n)) lignes = std::max(lignes, v->size());
    if (const auto* z = r.complexe(n)) lignes = std::max(lignes, z->size());
  }
  for (std::size_t i = 0; i < lignes; ++i) {
    std::vector<std::string> l;
    if (x) l.push_back(i < x->size() ? nombre((*x)[i]) : std::string());
    for (const auto& n : noms) {
      if (const auto* z = r.complexe(n)) {
        l.push_back(i < z->size() ? nombre(std::abs((*z)[i])) : std::string());
        l.push_back(i < z->size() ? nombre(std::arg((*z)[i]) * 180 / std::numbers::pi) : std::string());
      } else if (const auto* v = r.reel(n))
        l.push_back(i < v->size() ? nombre((*v)[i]) : std::string());
    }
    w.ligne(l);
  }
  return w.texte();
}

std::optional<ResultatFige> resultatDepuisCsv(const std::string& texte0, const std::string& nom, std::string* erreur) {
  std::string texte = texte0;
  if (texte.size() >= 3 && static_cast<unsigned char>(texte[0]) == 0xEF && static_cast<unsigned char>(texte[1]) == 0xBB && static_cast<unsigned char>(texte[2]) == 0xBF) texte = texte.substr(3);
  std::istringstream in(texte);
  std::vector<std::string> lignes;
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (!l.empty()) lignes.push_back(l);
  }
  if (lignes.size() < 2) {
    if (erreur) *erreur = "CSV sans données";
    return std::nullopt;
  }
  // Séparateur : celui qui revient le plus dans l'en-tête ; virgule décimale si le séparateur n'est pas la virgule
  char sep = ';';
  std::size_t meilleur = 0;
  for (const char s : {';', ',', '\t'})
    if (const auto n = static_cast<std::size_t>(std::count(lignes[0].begin(), lignes[0].end(), s)); n > meilleur) meilleur = n, sep = s;
  auto couper = [&](const std::string& l) {
    std::vector<std::string> v;
    std::string c;
    bool guillemets = false;
    for (const char ch : l) {
      if (ch == '"') guillemets = !guillemets;
      else if (ch == sep && !guillemets) {
        v.push_back(c);
        c.clear();
      } else
        c += ch;
    }
    v.push_back(c);
    return v;
  };
  const std::vector<std::string> tete = couper(lignes[0]);
  if (tete.size() < 2) {
    if (erreur) *erreur = "CSV d’une seule colonne : l’échelle et au moins un vecteur sont attendus";
    return std::nullopt;
  }
  std::vector<std::vector<double>> colonnes(tete.size());
  for (std::size_t i = 1; i < lignes.size(); ++i) {
    const auto v = couper(lignes[i]);
    for (std::size_t j = 0; j < tete.size(); ++j) {
      std::string s = j < v.size() ? v[j] : std::string();
      if (sep != ',') std::replace(s.begin(), s.end(), ',', '.');
      char* fin = nullptr;
      const double x = std::strtod(s.c_str(), &fin);
      if (fin == s.c_str()) {
        if (erreur) *erreur = "ligne " + std::to_string(i + 1) + ", colonne « " + tete[j] + " » : nombre illisible";
        return std::nullopt;
      }
      colonnes[j].push_back(x);
    }
  }
  ResultatFige f;
  f.nom = nom;
  f.echelle = tete[0];
  f.reels[tete[0]] = colonnes[0];
  // « mag(x) » et « ph(x) » côte à côte redonnent le vecteur complexe x
  for (std::size_t j = 1; j < tete.size(); ++j) {
    const std::string t = tete[j];
    if (t.rfind("mag(", 0) == 0 && j + 1 < tete.size() && tete[j + 1] == "ph(" + t.substr(4)) {
      auto& z = f.complexes[t.substr(4, t.size() - 5)];
      for (std::size_t i = 0; i < colonnes[j].size(); ++i) z.push_back(std::polar(colonnes[j][i], colonnes[j + 1][i] * std::numbers::pi / 180));
      ++j;
    } else
      f.reels[t] = colonnes[j];
  }
  return f;
}

}  // namespace prom::ecad
