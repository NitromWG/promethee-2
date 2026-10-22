// Prométhée : simulation du schéma (lot L11) : netlist SPICE, contrôles, mesures, boîte à calculs. Le moteur ngspice
// est dans moteur_ngspice.cpp. Licence GPL-3.0-only.
#include "promethee/simulation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>

#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
std::string nb(double v) {
  char t[48];
  std::snprintf(t, sizeof t, "%.9g", v);
  return t;
}
std::string minuscules(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
bool estMasse(const std::string& n) {
  const std::string m = minuscules(n);
  return m == "gnd" || m == "0" || m == "agnd" || m == "dgnd" || m == "pgnd" || m == "masse" || m == "gnda" || m == "vss" || m == "0v";
}
// Tension d'un réseau d'alimentation d'après son nom : « +5V » → 5, « +3V3 » → 3,3, « -12V » → −12, « VCC » → 5
std::optional<double> tensionDeNom(const std::string& nom) {
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
// Nombre lu dans un texte (« 9V », « 3,3 V », « AMS1117-3.3 » → 3,3)
std::optional<double> nombreDans(const std::string& t, bool dernier = false) {
  static const std::regex motif(R"((\d+(?:[.,]\d+)?))");
  std::optional<double> r;
  for (std::sregex_iterator it(t.begin(), t.end(), motif), fin; it != fin; ++it) {
    std::string x = (*it)[1].str();
    std::replace(x.begin(), x.end(), ',', '.');
    r = std::stod(x);
    if (!dernier) break;
  }
  return r;
}

const char* MODELE_555 = R"(.subckt NE555 gnd trig out reset ctrl thres disch vcc
* Temporisateur 555 comportemental (Prométhée) : diviseur 5k/5k/5k, comparateurs, bascule, sortie, décharge
R1 vcc ctrl 5k
R2 ctrl lo 5k
R3 lo gnd 5k
Bs s gnd V = (V(lo) - V(trig)) > 0 ? 1 : 0
Br r gnd V = (V(thres) - V(ctrl)) > 0 ? 1 : 0
Bq qn gnd V = (V(reset, gnd) < 0.7) ? 0 : ((V(s) > 0.5) ? 1 : ((V(r) > 0.5) ? 0 : ((V(q) > 0.5) ? 1 : 0)))
Rq qn q 1k
Cq q gnd 10p
Bqb qb gnd V = 1 - V(q)
Bo outi gnd V = V(q) > 0.5 ? V(vcc, gnd) - 1.7 : 0.1
Ro outi out 10
Sd disch gnd qb gnd swdecharge
.model swdecharge sw(vt=0.5 vh=0.1 ron=10 roff=1e9)
.ends NE555
)";
const char* MODELE_AOP = R"(.subckt AOP inp inm vcc vee out
* Amplificateur opérationnel comportemental (Prométhée) : gain 1e5, pôle à 10 Hz, sortie bornée aux rails
Rin inp inm 10meg
Bg n1 0 V = 1e5 * V(inp, inm)
Rp n1 n2 1k
Cp n2 0 15.9u
Bo o1 0 V = max(min(V(n2), V(vcc) - 0.1), V(vee) + 0.1)
Ro o1 out 50
.ends AOP
)";
const char* MODELE_REGULATEUR = R"(.subckt REGULATEUR vi vo gnd params: vout=5 chute=1.5
* Régulateur linéaire comportemental (Prométhée) : sortie bornée par l'entrée moins la chute
Rq vi gnd 100k
Bo o1 gnd V = max(min(V(vi, gnd) - chute, vout), 0)
Ro o1 vo 0.1
.ends REGULATEUR
)";
}  // namespace

const std::map<std::string, std::string>& modelesFournis() {
  static const std::map<std::string, std::string> m = {
      {"NE555", MODELE_555},
      {"AOP", MODELE_AOP},
      {"REGULATEUR", MODELE_REGULATEUR},
      {"DLED", ".model DLED D(IS=1e-22 N=1.5 RS=2 BV=5 IBV=10u CJO=10p)\n"},
      {"D1N4148", ".model D1N4148 D(IS=2.52n RS=0.568 N=1.752 CJO=4p M=0.4 TT=20n BV=100 IBV=100u)\n"},
      {"DZENER", ".model DZENER D(IS=1e-14 RS=5 BV=5.1 IBV=5m)\n"},
      {"DSCHOTTKY", ".model DSCHOTTKY D(IS=1e-5 RS=0.05 N=1.05 CJO=100p BV=40)\n"},
      {"QNPN", ".model QNPN NPN(IS=1e-14 BF=200 VAF=100 CJE=8p CJC=3p TF=0.4n TR=50n)\n"},
      {"QPNP", ".model QPNP PNP(IS=1e-14 BF=150 VAF=100 CJE=8p CJC=3p TF=0.4n TR=50n)\n"},
  };
  return m;
}

std::string AnalyseSpice::carte() const {
  switch (type) {
    case Type::PointDeRepos: return ".op";
    case Type::Continu: return ".dc " + (source.empty() ? std::string("V1") : source) + " " + nb(dcDebut) + " " + nb(dcFin) + " " + nb(dcPas);
    case Type::Alternatif: return ".ac dec " + std::to_string(std::max(1, pointsParDecade)) + " " + nb(fDebut) + " " + nb(fFin);
    case Type::Transitoire: return ".tran " + nb(pas) + " " + nb(duree) + " " + nb(debut);
  }
  return {};
}

NetlistSpice netlistSpice(const ConceptionElec& c, const AnalyseSpice& a) {
  NetlistSpice n;
  const Connectivite k = connectivite(c);
  // Nœuds : la masse en 0, les autres d'après le nom du réseau
  std::set<std::string> pris{"0"};
  auto noeud = [&](const std::string& reseau) -> std::string {
    if (reseau.empty()) return {};
    if (const auto it = n.noeuds.find(reseau); it != n.noeuds.end()) return it->second;
    if (estMasse(reseau)) return n.noeuds[reseau] = "0";
    std::string x = "n_";
    for (unsigned char ch : reseau) x += std::isalnum(ch) ? static_cast<char>(std::tolower(ch)) : '_';
    std::string y = x;
    for (int i = 2; pris.count(y); ++i) y = x + "_" + std::to_string(i);
    pris.insert(y);
    return n.noeuds[reseau] = y;
  };
  // Une broche sans liaison reçoit un nœud propre (elle ne touche rien)
  int libres = 0;
  auto noeudBroche = [&](const ComposantElec& x, const std::string& broche) {
    const std::string r = k.nom(x.uuid, broche);
    return r.empty() ? "nc_" + std::to_string(++libres) : noeud(r);
  };
  std::ostringstream el;
  std::set<std::string> modeles;
  std::set<std::string> sourcesPositives;  // nœuds tenus par une source posée
  // Liaisons en continu, pour le contrôle des nœuds flottants
  std::vector<std::pair<std::string, std::string>> liaisonsContinu;
  auto valeurDe = [&](const ComposantElec& x, double defaut) {
    const ValeurComprise v = lireValeurComposant(x.valeur, familleDe(c, x));
    if (v.comprise() && v.nombre > 0) return v.nombre;
    if (const auto d = nombreDans(x.valeur)) return *d;
    return defaut;
  };
  std::set<std::string> poses;
  for (const auto& x : c.composants) {
    if (x.reference.empty() || x.reference[0] == '#') continue;
    const std::string ref = x.reference;
    if (const auto it = x.champs.find("Sim.Exclure"); it != x.champs.end() && (it->second == "1" || minuscules(it->second) == "oui")) {
      n.avertissements.push_back(ref + " écarté de la simulation (champ Sim.Exclure)");
      continue;
    }
    // Élément écrit à la main : « XU1 {1} {2} MONMODELE », les numéros de broches remplacés par leurs nœuds
    if (const auto it = x.champs.find("Sim.Modele"); it != x.champs.end() && !it->second.empty()) {
      std::string t = it->second;
      static const std::regex marque(R"(\{([^}]+)\})");
      std::string sortie;
      std::size_t debut = 0;
      for (std::sregex_iterator m(t.begin(), t.end(), marque), fin; m != fin; ++m) {
        sortie += t.substr(debut, static_cast<std::size_t>(m->position()) - debut);
        const std::string cle = (*m)[1].str();
        sortie += cle == "ref" ? ref : noeudBroche(x, cle);
        debut = static_cast<std::size_t>(m->position() + m->length());
      }
      sortie += t.substr(debut);
      el << sortie << "\n";
      if (const auto b = x.champs.find("Sim.Bibliotheque"); b != x.champs.end()) el << b->second << "\n";
      continue;
    }
    const std::string s = x.symbole;
    auto N = [&](const char* broche) { return noeudBroche(x, broche); };
    auto deux = [&](const char* p1, const char* p2) {
      liaisonsContinu.push_back({N(p1), N(p2)});
      return N(p1) + " " + N(p2);
    };
    if (s == "R") el << "R" << ref << " " << deux("1", "2") << " " << nb(valeurDe(x, 1000)) << "\n";
    else if (s == "C" || s == "C_Pol") el << "C" << ref << " " << N("1") << " " << N("2") << " " << nb(valeurDe(x, 1e-6)) << "\n";
    else if (s == "L") el << "L" << ref << " " << deux("1", "2") << " " << nb(valeurDe(x, 1e-6)) << "\n";
    else if (s == "Fusible" || s == "Liaison_reseaux") el << "R" << ref << " " << deux("1", "2") << " 1m\n";
    else if (s == "Buzzer" || s == "Moteur") el << "R" << ref << " " << deux("1", "2") << " 50\n";
    else if (s == "Bouton" || s == "Interrupteur") {
      const bool ferme = x.champs.count("Sim.Ferme") && x.champs.at("Sim.Ferme") == "1";
      el << "R" << ref << " " << deux("1", "2") << " " << (ferme ? "1m" : "1e9") << "\n";
    } else if (s == "Potentiometre") {
      const double r = valeurDe(x, 10'000);
      double pos = 0.5;
      if (const auto it = x.champs.find("Sim.Position"); it != x.champs.end())
        if (const auto v = nombreDans(it->second)) pos = std::clamp(*v > 1 ? *v / 100 : *v, 0.001, 0.999);
      el << "R" << ref << "a " << deux("1", "2") << " " << nb(r * pos) << "\n";
      el << "R" << ref << "b " << deux("2", "3") << " " << nb(r * (1 - pos)) << "\n";
    } else if (s == "D" || s == "LED" || s == "D_Zener" || s == "D_Schottky") {
      const char* m = s == "LED" ? "DLED" : s == "D_Zener" ? "DZENER" : s == "D_Schottky" ? "DSCHOTTKY" : "D1N4148";
      el << "D" << ref << " " << N("2") << " " << N("1") << " " << m << "\n";  // anode, cathode (broche 1 : cathode)
      liaisonsContinu.push_back({N("1"), N("2")});
      modeles.insert(m);
    } else if (s == "Q_NPN" || s == "Q_PNP") {
      const char* m = s == "Q_NPN" ? "QNPN" : "QPNP";
      el << "Q" << ref << " " << N("3") << " " << N("1") << " " << N("2") << " " << m << "\n";  // C B E (SOT-23 : 1 B, 2 E, 3 C)
      liaisonsContinu.push_back({N("1"), N("2")});
      liaisonsContinu.push_back({N("3"), N("2")});
      modeles.insert(m);
    } else if (s == "NE555") {
      el << "X" << ref << " " << N("1") << " " << N("2") << " " << N("3") << " " << N("4") << " " << N("5") << " " << N("6") << " " << N("7") << " " << N("8") << " NE555\n";
      for (const char* p : {"2", "3", "4", "5", "6", "7", "8"}) liaisonsContinu.push_back({N("1"), N(p)});
      modeles.insert("NE555");
    } else if (s == "AOP") {
      el << "X" << ref << " " << N("1") << " " << N("3") << " " << N("5") << " " << N("2") << " " << N("4") << " AOP\n";
      for (const char* p : {"1", "3", "4", "5"}) liaisonsContinu.push_back({N("2"), N(p)});
      modeles.insert("AOP");
    } else if (s == "LM358_unites") {
      // Une pose par unité ; l'alimentation (8 V+, 4 V−) commune
      const std::string vcc = N("8"), vee = N("4");
      el << "X" << ref << "A " << N("3") << " " << N("2") << " " << vcc << " " << vee << " " << N("1") << " AOP\n";
      el << "X" << ref << "B " << N("5") << " " << N("6") << " " << vcc << " " << vee << " " << N("7") << " AOP\n";
      for (const char* p : {"1", "2", "3", "5", "6", "7", "8"}) liaisonsContinu.push_back({vee, N(p)});
      modeles.insert("AOP");
    } else if (s == "Regulateur" || s == "LM7805") {
      const bool l7805 = s == "LM7805";
      const std::string vi = N(l7805 ? "1" : "3"), vo = N(l7805 ? "3" : "2"), gnd = N(l7805 ? "2" : "1");
      const double vout = l7805 ? 5.0 : nombreDans(x.valeur, true).value_or(3.3);
      el << "X" << ref << " " << vi << " " << vo << " " << gnd << " REGULATEUR params: vout=" << nb(vout) << " chute=" << (l7805 ? "2" : "1.1") << "\n";
      liaisonsContinu.push_back({vi, gnd});
      liaisonsContinu.push_back({vo, gnd});
      modeles.insert("REGULATEUR");
    } else if (s == "Pile") {
      el << "V" << ref << " " << deux("1", "2") << " DC " << nb(nombreDans(x.valeur).value_or(9)) << "\n";
      sourcesPositives.insert(N("1"));
    } else if (s == "Source_tension") {
      // La valeur dit la source : « 5 », « DC 5 AC 1 », « SIN(0 1 1k) », « PULSE(…) », ou une phrase PWL en français
      std::string v = x.valeur.empty() ? std::string("0") : x.valeur;
      std::string err;
      const std::string vm = minuscules(v);
      if (vm.find("pendant") != std::string::npos || vm.find("rampe") != std::string::npos || vm.find("puis") != std::string::npos) {
        if (const std::string p = sourcePwl(v, &err); !p.empty()) v = p;
        else n.erreurs.push_back(ref + " : source non comprise (" + err + ")");
      }
      if (!std::isalpha(static_cast<unsigned char>(v[0])) && v.find('(') == std::string::npos) v = "DC " + nb(nombreDans(v).value_or(0));
      el << "V" << ref << " " << deux("1", "2") << " " << v << "\n";
      sourcesPositives.insert(N("1"));
    } else if (s == "Source_courant") {
      std::string v = x.valeur.empty() ? std::string("0") : x.valeur;
      if (!std::isalpha(static_cast<unsigned char>(v[0])) && v.find('(') == std::string::npos) {
        const ValeurComprise vc = lireValeurComposant(v, FamilleValeur::Aucune);
        v = "DC " + nb(vc.comprise() ? vc.nombre : nombreDans(v).value_or(0) * (v.find("mA") != std::string::npos ? 1e-3 : 1));
      }
      el << "I" << ref << " " << N("1") << " " << N("2") << " " << v << "\n";
    } else {
      // Mécanique, connecteurs, points de test : sans effet sur le calcul ; un circuit intégré sans modèle est signalé
      const bool silencieux = s.rfind("Conn_", 0) == 0 || s.rfind("Bornier_", 0) == 0 || s == "Trou_fixation" || s == "Trou_fixation_masse" || s == "Entretoise" || s == "Vis" ||
                              s == "Dissipateur" || s == "Point_test" || s == "Quartz";
      if (!silencieux) n.avertissements.push_back(ref + " (" + s + ") écarté : pas de modèle de simulation ; un champ Sim.Modele le donne");
      continue;
    }
    poses.insert(ref);
  }
  // Réseaux d'alimentation nommés sans source : une source continue implicite
  std::ostringstream implicites;
  for (const auto& r : k.reseaux) {
    if (r.broches.empty()) continue;
    const auto v = tensionDeNom(r.nom);
    if (!v) continue;
    const std::string nd = noeud(r.nom);
    if (nd == "0" || sourcesPositives.count(nd)) continue;
    bool utilise = false;
    for (const auto& b : r.broches) utilise |= c.composant(b.composant) && poses.count(c.composant(b.composant)->reference);
    if (!utilise) continue;
    implicites << "V_" << nd << " " << nd << " 0 DC " << nb(*v) << "\n";
    liaisonsContinu.push_back({nd, "0"});
    n.avertissements.push_back("réseau « " + r.nom + " » alimenté par une source implicite de " + nb(*v) + " V");
  }
  // Contrôles avant le calcul : masse, nœuds sans chemin vers elle en continu
  std::set<std::string> tous;
  for (const auto& [r, nd] : n.noeuds) tous.insert(nd);
  if (!tous.count("0")) n.erreurs.push_back("pas de masse : nomme « GND » le réseau de référence (symbole de masse)");
  std::map<std::string, std::string> parent;
  std::function<std::string(const std::string&)> racine = [&](const std::string& x) -> std::string {
    auto it = parent.find(x);
    if (it == parent.end() || it->second == x) return x;
    return it->second = racine(it->second);
  };
  for (const auto& nd : tous) parent[nd] = nd;
  for (const auto& [u, v] : liaisonsContinu) {
    if (!parent.count(u)) parent[u] = u;
    if (!parent.count(v)) parent[v] = v;
    parent[racine(u)] = racine(v);
  }
  if (tous.count("0"))
    for (const auto& [reseau, nd] : n.noeuds)
      if (nd != "0" && racine(nd) != racine("0")) n.erreurs.push_back("nœud sans chemin vers la masse en continu : « " + reseau + " » (un condensateur seul ne suffit pas)");
  // Texte
  std::ostringstream t;
  t << "* " << (c.nom.empty() ? std::string("Conception") : c.nom) << " — netlist écrite par Prométhée\n";
  t << el.str() << implicites.str();
  for (const auto& m : modeles) t << modelesFournis().at(m);
  t << a.carte() << "\n.end\n";
  n.texte = t.str();
  return n;
}

std::string vecteurTension(const NetlistSpice& n, const std::string& reseau) {
  const auto it = n.noeuds.find(reseau);
  if (it == n.noeuds.end() || it->second == "0") return {};
  return "v(" + it->second + ")";
}

namespace {
// « v(n_sortie) » : ngspice range le vecteur sous le nom du nœud seul
bool memeVecteur(const std::string& k, const std::string& nom) {
  const std::string a = minuscules(k), b = minuscules(nom);
  if (a == b) return true;
  if (b.size() > 3 && b.rfind("v(", 0) == 0 && b.back() == ')') return a == b.substr(2, b.size() - 3);
  return false;
}
}  // namespace

const std::vector<double>* ResultatSimulation::reel(const std::string& nom) const {
  for (const auto& [k, v] : reels)
    if (memeVecteur(k, nom)) return &v;
  return nullptr;
}
const std::vector<std::complex<double>>* ResultatSimulation::complexe(const std::string& nom) const {
  for (const auto& [k, v] : complexes)
    if (memeVecteur(k, nom)) return &v;
  return nullptr;
}

// ---------- Mesures ----------
double frequenceMesuree(const std::vector<double>& t, const std::vector<double>& v) {
  if (t.size() < 4 || v.size() != t.size()) return 0;
  const std::size_t debut = t.size() / 2;
  const auto [mn, mx] = std::minmax_element(v.begin() + static_cast<long>(debut), v.end());
  const double milieu = (*mn + *mx) / 2;
  if (*mx - *mn < 1e-9) return 0;
  std::vector<double> fronts;
  for (std::size_t i = debut + 1; i < t.size(); ++i)
    if (v[i - 1] < milieu && v[i] >= milieu) fronts.push_back(t[i - 1] + (milieu - v[i - 1]) / (v[i] - v[i - 1]) * (t[i] - t[i - 1]));
  if (fronts.size() < 2) return 0;
  return static_cast<double>(fronts.size() - 1) / (fronts.back() - fronts.front());
}

double frequenceCoupure(const std::vector<double>& f, const std::vector<std::complex<double>>& h) {
  if (f.size() < 2 || h.size() != f.size()) return 0;
  const double ref = std::abs(h.front()) / std::sqrt(2.0);
  for (std::size_t i = 1; i < f.size(); ++i)
    if (std::abs(h[i - 1]) >= ref && std::abs(h[i]) < ref) {
      // Interpolation en décibels sur le logarithme de la fréquence
      const double a = 20 * std::log10(std::abs(h[i - 1])), b = 20 * std::log10(std::abs(h[i])), cible = 20 * std::log10(ref);
      const double la = std::log10(f[i - 1]), lb = std::log10(f[i]);
      return std::pow(10.0, la + (cible - a) / (b - a) * (lb - la));
    }
  return 0;
}

double valeurA(const std::vector<double>& x, const std::vector<double>& y, double x0) {
  if (x.empty() || y.size() != x.size()) return 0;
  if (x0 <= x.front()) return y.front();
  if (x0 >= x.back()) return y.back();
  const auto it = std::upper_bound(x.begin(), x.end(), x0);
  const std::size_t i = static_cast<std::size_t>(it - x.begin());
  return y[i - 1] + (x0 - x[i - 1]) / (x[i] - x[i - 1]) * (y[i] - y[i - 1]);
}

std::vector<double> gainDb(const std::vector<std::complex<double>>& h) {
  std::vector<double> r;
  r.reserve(h.size());
  for (const auto& x : h) r.push_back(20 * std::log10(std::max(std::abs(x), 1e-30)));
  return r;
}

// ---------- Boîte à calculs ----------
CombinaisonE combinaisonE(double cible, int serie) {
  CombinaisonE meilleur;
  if (cible <= 0) return meilleur;
  std::vector<double> valeurs;
  for (int decade = -1; decade <= 7; ++decade)
    for (double m : serieE(serie)) valeurs.push_back(m * std::pow(10.0, decade));
  std::sort(valeurs.begin(), valeurs.end());
  valeurs.erase(std::unique(valeurs.begin(), valeurs.end()), valeurs.end());
  auto essayer = [&](double a, double b, bool par) {
    const double v = par ? a * b / (a + b) : a + b;
    const double e = std::abs(v - cible) / cible;
    if (meilleur.valeur == 0 || e < meilleur.ecart - 1e-12) meilleur = {a, b, par, v, e};
  };
  auto voisins = [&](double cherche, const std::function<void(double)>& f) {
    const auto it = std::lower_bound(valeurs.begin(), valeurs.end(), cherche);
    if (it != valeurs.end()) f(*it);
    if (it != valeurs.begin()) f(*std::prev(it));
  };
  for (double a : valeurs) {
    if (a <= cible) voisins(cible - a, [&](double b) { essayer(a, b, false); });
    if (a > cible) voisins(1 / (1 / cible - 1 / a), [&](double b) { essayer(a, b, true); });
  }
  if (meilleur.a < meilleur.b) std::swap(meilleur.a, meilleur.b);
  return meilleur;
}

// ---------- Sources en français ----------
std::string sourcePwl(const std::string& description, std::string* erreur) {
  // Valeur avec unité : « 5 V », « 500 mV », « 1 ms », « 10 µs »
  static const std::regex tension(R"(([-+]?\d+(?:[.,]\d+)?)\s*(mV|V))");
  static const std::regex duree(R"((\d+(?:[.,]\d+)?)\s*(ns|µs|us|ms|s)\b)");
  auto lire = [](const std::smatch& m) {
    std::string x = m[1].str();
    std::replace(x.begin(), x.end(), ',', '.');
    return std::stod(x);
  };
  auto facteur = [](const std::string& u) { return u == "ns" ? 1e-9 : (u == "µs" || u == "us") ? 1e-6 : u == "ms" ? 1e-3 : u == "mV" ? 1e-3 : 1.0; };
  // Morceaux séparés par « puis » ou une virgule
  std::vector<std::string> morceaux;
  std::string reste = description;
  for (;;) {
    std::size_t p = reste.find(", puis");
    std::size_t l = 6;
    if (p == std::string::npos) {
      p = reste.find(" puis ");
      l = 6;
    }
    if (p == std::string::npos) {
      p = reste.find(',');
      l = 1;
    }
    if (p == std::string::npos) break;
    morceaux.push_back(reste.substr(0, p));
    reste = reste.substr(p + l);
  }
  morceaux.push_back(reste);
  double t = 0, v = 0;
  std::vector<std::pair<double, double>> points;
  bool premier = true;
  for (const auto& m0 : morceaux) {
    const std::string m = minuscules(m0);
    std::smatch mt, md;
    const bool aT = std::regex_search(m0, mt, tension), aD = std::regex_search(m0, md, duree);
    if (m.find("rampe") != std::string::npos && aT && aD) {
      if (premier) points.push_back({t, v});
      t += lire(md) * facteur(md[2].str());
      v = lire(mt) * facteur(mt[2].str());
      points.push_back({t, v});
    } else if (m.find("pendant") != std::string::npos && aT && aD) {
      const double nv = lire(mt) * facteur(mt[2].str());
      if (premier || nv != v) {
        if (!premier) t += 1e-9;
        v = nv;
        points.push_back({t, v});
      }
      t += lire(md) * facteur(md[2].str());
      points.push_back({t, v});
    } else if ((m.find("saut") != std::string::npos || premier) && aT) {
      if (!premier) t += 1e-9;
      v = lire(mt) * facteur(mt[2].str());
      points.push_back({t, v});
    } else {
      if (erreur) *erreur = "morceau non compris : « " + m0 + " »";
      return {};
    }
    premier = false;
  }
  std::string r = "PWL(";
  for (std::size_t i = 0; i < points.size(); ++i) r += (i ? " " : "") + nb(points[i].first) + " " + nb(points[i].second);
  return r + ")";
}

}  // namespace prom::ecad
