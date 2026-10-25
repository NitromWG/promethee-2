// Prométhée : simulation nourrie par le projet (lot L14 de la 0.34 ; 14.11). Voir simulation_nourrie.hpp.
// Licence GPL-3.0-only.
#include "promethee/simulation_nourrie.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

#include "promethee/fabrication_avancee.hpp"
#include "promethee/lignes_transmission.hpp"

namespace prom::ecad {

namespace {
constexpr double C0 = 299'792'458.0;
constexpr double RHO_CUIVRE = 1.72e-8;  // Ω·m à 20 °C
std::string minuscules(std::string s) {
  for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
std::string nb(double v) {
  std::ostringstream o;
  o.imbue(std::locale::classic());
  o.precision(10);
  o << v;
  return o.str();
}
// Nombre avec suffixe SPICE (« 1m », « 2.2u », « 47k »)
double nombreSi(const std::string& t, double defaut) {
  std::string x = minuscules(t);
  for (auto& c : x)
    if (c == ',') c = '.';
  char* fin = nullptr;
  double v = std::strtod(x.c_str(), &fin);
  if (fin == x.c_str()) return defaut;
  const std::string s = fin;
  if (s.rfind("meg", 0) == 0) v *= 1e6;
  else if (!s.empty()) switch (s[0]) {
      case 'g': v *= 1e9; break;
      case 'k': v *= 1e3; break;
      case 'm': v *= 1e-3; break;
      case 'u': v *= 1e-6; break;
      case 'n': v *= 1e-9; break;
      case 'p': v *= 1e-12; break;
      default: break;
    }
  return v;
}
}  // namespace

double permittiviteEffective(double largeur, double hauteur, double er) {
  return LigneTransmission(GenreLigne::Microruban, {er, 0.02, 1e9, 1e3, 1e12, false}).analyser({largeur * 1e-3, 0, hauteur * 1e-3, 0, 35e-6, 0}, 1e9).erEff;
}

std::vector<ParasitesReseau> parasitesPistes(const ConceptionElec& c) {
  std::map<std::string, ParasitesReseau> m;
  const Microruban mr = microrubanDe(c.circuit.empilage);
  const double t = std::max(1e-6, c.circuit.empilage.cuivre * 1e-3);  // m
  for (const auto& p : c.circuit.pistes) {
    if (p.reseau.empty()) continue;
    const double l = std::hypot(static_cast<double>(p.b.x - p.a.x), static_cast<double>(p.b.y - p.a.y)) * 1e-9;  // m
    const double w = std::max(1.0, static_cast<double>(p.largeur)) * 1e-9;                                        // m
    const ResultatLigne lt = LigneTransmission(GenreLigne::Microruban, {mr.er, 0.02, 1e9, 1e3, 1e12, false}).analyser({w, 0, mr.h * 1e-3, 0, t, 0}, 1e9);
    const double z0 = lt.z0 > 0 ? lt.z0 : 50, ee = lt.erEff > 0 ? lt.erEff : (mr.er + 1) / 2;
    ParasitesReseau& x = m[p.reseau];
    x.reseau = p.reseau;
    ++x.pistes;
    x.longueur += l;
    x.resistance += RHO_CUIVRE * l / (w * t);
    x.inductance += z0 * std::sqrt(ee) / C0 * l;
    x.capacite += std::sqrt(ee) / (z0 * C0) * l;
  }
  const Connectivite k = connectivite(c);
  std::vector<ParasitesReseau> v;
  for (auto& [n, x] : m) {
    for (const auto& r : k.reseaux)
      if (r.nom == n) x.broches = static_cast<int>(r.broches.size());
    v.push_back(x);
  }
  return v;
}

NetlistSpice netlistAvecPistes(const ConceptionElec& c, const AnalyseSpice& a, OptionsSimulation o, std::vector<ParasitesReseau>* sortie) {
  const NetlistSpice avant = netlistSpice(c, a, o);
  const std::vector<ParasitesReseau> p = parasitesPistes(c);
  if (sortie) *sortie = p;
  const Connectivite k = connectivite(c);
  int i = 0;
  for (const auto& x : p) {
    const auto it = avant.noeuds.find(x.reseau);
    if (it == avant.noeuds.end() || it->second == "0") continue;
    const std::string nd = it->second;
    ++i;
    const ecad::Reseau* res = nullptr;
    for (const auto& r : k.reseaux)
      if (r.nom == x.reseau) res = &r;
    if (res && res->broches.size() == 2) {
      // Cellule en pi entre les deux broches : la seconde reçoit son propre nœud
      const std::string loin = nd + "_p";
      o.noeudsBroches[{res->broches[1].composant, res->broches[1].broche}] = loin;
      o.elementsEnPlus.push_back("C_pis" + std::to_string(i) + "a " + nd + " 0 " + nb(x.capacite / 2));
      o.elementsEnPlus.push_back("R_pis" + std::to_string(i) + " " + nd + " " + nd + "_m " + nb(std::max(1e-6, x.resistance)));
      o.elementsEnPlus.push_back("L_pis" + std::to_string(i) + " " + nd + "_m " + loin + " " + nb(std::max(1e-15, x.inductance)));
      o.elementsEnPlus.push_back("C_pis" + std::to_string(i) + "b " + loin + " 0 " + nb(x.capacite / 2));
    } else
      o.elementsEnPlus.push_back("C_pis" + std::to_string(i) + " " + nd + " 0 " + nb(x.capacite));
  }
  return netlistSpice(c, a, o);
}

ParametresMoteur parametresMoteur(const ComposantElec& k) {
  ParametresMoteur p;
  auto lire = [&](const char* cle, double& v) {
    if (const auto it = k.champs.find(cle); it != k.champs.end()) v = nombreSi(it->second, v);
  };
  lire("Sim.R", p.r);
  lire("Sim.L", p.l);
  lire("Sim.Ke", p.ke);
  lire("Sim.Kt", p.kt);
  lire("Sim.J", p.j);
  lire("Sim.B", p.b);
  lire("Sim.Couple", p.couple);
  return p;
}

void reglerMoteur(ComposantElec& k, const ParametresMoteur& p) {
  k.champs["Sim.R"] = nb(p.r);
  k.champs["Sim.L"] = nb(p.l);
  k.champs["Sim.Ke"] = nb(p.ke);
  k.champs["Sim.Kt"] = nb(p.kt);
  k.champs["Sim.J"] = nb(p.j);
  k.champs["Sim.B"] = nb(p.b);
  k.champs["Sim.Couple"] = nb(p.couple);
}

void appliquerCharge(ComposantElec& moteur, double inertiePiece, double coupleResistant, double inertieRotor) {
  ParametresMoteur p = parametresMoteur(moteur);
  if (inertieRotor <= 0) inertieRotor = moteur.champs.count("Sim.Jrotor") ? nombreSi(moteur.champs["Sim.Jrotor"], 0) : 0;
  p.j = inertieRotor + inertiePiece;
  p.couple = coupleResistant;
  reglerMoteur(moteur, p);
}

double constanteTempsMecanique(const ParametresMoteur& p) { return p.j * p.r / (p.ke * p.kt); }
std::string vecteurVitesse(const std::string& reference) { return "v(w_" + minuscules(reference) + ")"; }

double constanteDeTemps(const Releve& r, double finale, double depart) {
  const double cible = 0.632120558828558 * finale;
  for (std::size_t i = 1; i < r.size(); ++i) {
    if (r[i].t < depart) continue;
    if ((r[i - 1].v - cible) * (r[i].v - cible) <= 0 && r[i].v != r[i - 1].v)
      return r[i - 1].t + (r[i].t - r[i - 1].t) * (cible - r[i - 1].v) / (r[i].v - r[i - 1].v) - depart;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace prom::ecad
