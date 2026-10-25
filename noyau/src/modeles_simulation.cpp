// Prométhée : modèles de simulation (lot L11 de la 0.34). Voir modeles_simulation.hpp. Licence GPL-3.0-only.
#include "promethee/modeles_simulation.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <regex>
#include <sstream>

#include "promethee/moindres_carres.hpp"

namespace prom::ecad {

namespace {
using prom::elec::ChampCalcul;

std::string nb(double v) {
  char t[48];
  std::snprintf(t, sizeof t, "%.6g", v);
  return t;
}
std::string minuscules(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
ChampCalcul champ(const char* id, const char* nom, const char* unite, double valeur, double min, double max, int decimales = 3, const char* aide = "") {
  ChampCalcul c;
  c.id = id, c.nom = nom, c.unite = unite, c.valeur = valeur, c.min = min, c.max = max, c.decimales = decimales, c.aide = aide;
  return c;
}
// Paramètres « CLÉ=valeur » d'une carte .model, dans l'ordre des champs ; ceux à zéro marqués « facultatif » omis
std::string parametres(const FicheModele& f, const std::vector<double>& v, const std::set<std::string>& omisAZero = {}) {
  std::string r;
  for (std::size_t i = 0; i < f.champs.size(); ++i) {
    if (omisAZero.count(f.champs[i].id) && v[i] == 0) continue;
    r += (r.empty() ? "" : " ") + f.champs[i].id + "=" + nb(v[i]);
  }
  return r;
}
}  // namespace

// ---------- Fiches de modèles intégrés ----------
const std::vector<FicheModele>& fichesModeles() {
  static const std::vector<FicheModele> v = [] {
    std::vector<FicheModele> f;
    f.push_back({"diode", "Diode", "diode", "Modèle D de SPICE : saturation, émission, série, jonction, transit, claquage", {
                     champ("IS", "Courant de saturation", "A", 1e-14, 1e-20, 1e-3, 3, "IS"), champ("N", "Coefficient d’émission", "", 1, 0.5, 5),
                     champ("RS", "Résistance série", "Ω", 0.1, 0, 1e3), champ("CJO", "Capacité de jonction à 0 V", "F", 2e-12, 0, 1e-6),
                     champ("M", "Exposant de jonction", "", 0.5, 0.1, 0.9), champ("VJ", "Potentiel de jonction", "V", 1, 0.2, 2), champ("TT", "Temps de transit", "s", 5e-9, 0, 1e-3),
                     champ("BV", "Tension de claquage (0 : aucune)", "V", 0, 0, 1e4), champ("IBV", "Courant au claquage", "A", 1e-4, 0, 1)}, "A K"});
    for (const char* t : {"npn", "pnp"})
      f.push_back({t, std::string("Bipolaire ") + (t[0] == 'n' ? "NPN" : "PNP"), "bipolaire", "Gummel-Poon de SPICE (niveau 1)", {
                       champ("IS", "Courant de saturation", "A", 1e-14, 1e-20, 1e-6), champ("BF", "Gain direct", "", 100, 1, 1e4), champ("BR", "Gain inverse", "", 1, 0.01, 1e3),
                       champ("VAF", "Tension d’Early (0 : infinie)", "V", 100, 0, 1e4), champ("NF", "Émission directe", "", 1, 0.5, 3), champ("CJE", "Capacité base-émetteur", "F", 1e-12, 0, 1e-6),
                       champ("CJC", "Capacité base-collecteur", "F", 1e-12, 0, 1e-6), champ("TF", "Transit direct", "s", 1e-10, 0, 1e-3), champ("TR", "Transit inverse", "s", 1e-8, 0, 1e-3),
                       champ("RB", "Résistance de base", "Ω", 10, 0, 1e5), champ("RC", "Résistance de collecteur", "Ω", 1, 0, 1e5), champ("RE", "Résistance d’émetteur", "Ω", 0.1, 0, 1e5)}, "C B E"});
    for (const char* t : {"nmos", "pmos"})
      f.push_back({t, std::string("MOSFET ") + (t[0] == 'n' ? "canal N" : "canal P") + " (niveau 1)", "mos", "Shichman-Hodges", {
                       champ("VTO", "Tension de seuil", "V", t[0] == 'n' ? 1 : -1, -50, 50), champ("KP", "Transconductance", "A/V²", 2e-5, 1e-9, 1e3),
                       champ("LAMBDA", "Modulation de canal", "1/V", 0.01, 0, 1), champ("GAMMA", "Effet de substrat", "√V", 0, 0, 10), champ("PHI", "Potentiel de surface", "V", 0.6, 0.1, 2),
                       champ("CGSO", "Recouvrement grille-source", "F/m", 0, 0, 1e-6), champ("CGDO", "Recouvrement grille-drain", "F/m", 0, 0, 1e-6)}, "D G S"});
    for (const char* t : {"vdmos-n", "vdmos-p"})
      f.push_back({t, std::string("MOSFET de puissance ") + (t[6] == 'n' ? "canal N" : "canal P") + " (VDMOS)", "mos", "Modèle VDMOS de ngspice", {
                       champ("VTO", "Tension de seuil", "V", t[6] == 'n' ? 2 : -2, -50, 50), champ("KP", "Transconductance", "A/V²", 1, 1e-6, 1e4),
                       champ("RD", "Résistance de drain", "Ω", 0.01, 0, 1e3), champ("RS", "Résistance de source", "Ω", 0.01, 0, 1e3), champ("RG", "Résistance de grille", "Ω", 1, 0, 1e3),
                       champ("LAMBDA", "Modulation de canal", "1/V", 0.001, 0, 1), champ("CGDMAX", "Capacité grille-drain maximale", "F", 1e-9, 0, 1e-6),
                       champ("CGDMIN", "Capacité grille-drain minimale", "F", 1e-10, 0, 1e-6), champ("CGS", "Capacité grille-source", "F", 1e-9, 0, 1e-6),
                       champ("CJO", "Capacité de la diode de structure", "F", 1e-9, 0, 1e-6), champ("IS", "Saturation de la diode", "A", 1e-12, 1e-20, 1e-3)}, "D G S"});
    for (const char* t : {"njf", "pjf"})
      f.push_back({t, std::string("JFET ") + (t[0] == 'n' ? "canal N" : "canal P"), "jfet", "JFET de SPICE", {
                       champ("VTO", "Tension de pincement", "V", t[0] == 'n' ? -2 : 2, -50, 50), champ("BETA", "Transconductance", "A/V²", 1e-3, 1e-9, 10),
                       champ("LAMBDA", "Modulation de canal", "1/V", 0.01, 0, 1), champ("RD", "Résistance de drain", "Ω", 0, 0, 1e4), champ("RS", "Résistance de source", "Ω", 0, 0, 1e4),
                       champ("CGS", "Capacité grille-source", "F", 1e-12, 0, 1e-6), champ("CGD", "Capacité grille-drain", "F", 1e-12, 0, 1e-6)}, "D G S"});
    f.push_back({"sw", "Interrupteur commandé en tension", "sous-circuit", "Élément S : seuil et hystérésis", {
                     champ("VT", "Seuil", "V", 0.5, -1e3, 1e3), champ("VH", "Hystérésis", "V", 0.1, 0, 1e3), champ("RON", "Résistance fermé", "Ω", 1, 1e-6, 1e6),
                     champ("ROFF", "Résistance ouvert", "Ω", 1e9, 1, 1e15)}, "1 2 3 4"});
    f.push_back({"csw", "Interrupteur commandé en courant", "sous-circuit", "Élément W : seuil et hystérésis du courant", {
                     champ("IT", "Seuil", "A", 1e-3, -1e3, 1e3), champ("IH", "Hystérésis", "A", 1e-4, 0, 1e3), champ("RON", "Résistance fermé", "Ω", 1, 1e-6, 1e6),
                     champ("ROFF", "Résistance ouvert", "Ω", 1e9, 1, 1e15)}, "1 2"});
    f.push_back({"mutuelle", "Inductances couplées", "sous-circuit", "Deux inductances et leur couplage K", {
                     champ("L1", "Inductance du primaire", "H", 1e-3, 1e-12, 1e3), champ("L2", "Inductance du secondaire", "H", 1e-3, 1e-12, 1e3),
                     champ("K", "Couplage", "", 0.99, 0.01, 0.999999, 6)}, "1 2 3 4"});
    f.push_back({"ligne-t", "Ligne de transmission sans pertes", "sous-circuit", "Élément T : impédance et retard", {
                     champ("Z0", "Impédance caractéristique", "Ω", 50, 1, 1e4), champ("TD", "Retard", "s", 1e-9, 1e-15, 1)}, "1 2 3 4"});
    f.push_back({"potentiometre", "Potentiomètre", "sous-circuit", "Deux résistances, curseur borné", {
                     champ("R", "Résistance totale", "Ω", 10e3, 1e-3, 1e9), champ("POS", "Position du curseur", "", 0.5, 0.0001, 0.9999, 4)}, "1 2 3"});
    f.push_back({"gain", "Bloc de gain (XSPICE)", "sous-circuit", "Bloc gain : décalages d’entrée et de sortie", {
                     champ("GAIN", "Gain", "", 1, -1e9, 1e9), champ("IN_OFFSET", "Décalage d’entrée", "V", 0, -1e3, 1e3), champ("OUT_OFFSET", "Décalage de sortie", "V", 0, -1e3, 1e3)}, "1 2"});
    return f;
  }();
  return v;
}

const FicheModele* ficheModele(const std::string& id) {
  for (const auto& f : fichesModeles())
    if (f.id == id) return &f;
  return nullptr;
}

std::optional<std::string> ecrireModele(const FicheModele& f, const std::string& nom, const std::vector<double>& valeurs0, std::string* refus) {
  std::vector<double> v = valeurs0;
  for (std::size_t i = v.size(); i < f.champs.size(); ++i) v.push_back(f.champs[i].valeur);
  for (std::size_t i = 0; i < f.champs.size(); ++i)
    if (!(v[i] >= f.champs[i].min && v[i] <= f.champs[i].max)) {
      if (refus) *refus = "« " + f.champs[i].nom + " » hors bornes : de " + nb(f.champs[i].min) + " à " + nb(f.champs[i].max) + (f.champs[i].unite.empty() ? "" : " " + f.champs[i].unite);
      return std::nullopt;
    }
  if (f.id == "diode") return ".model " + nom + " D(" + parametres(f, v, {"BV"}) + ")\n";
  if (f.id == "npn" || f.id == "pnp") return ".model " + nom + " " + minuscules(f.id) + "(" + parametres(f, v, {"VAF"}) + ")\n";
  if (f.id == "nmos" || f.id == "pmos") return ".model " + nom + " " + f.id + "(LEVEL=1 " + parametres(f, v) + ")\n";
  if (f.id.rfind("vdmos", 0) == 0) return ".model " + nom + " VDMOS(" + std::string(f.id == "vdmos-p" ? "pchan " : "") + parametres(f, v) + ")\n";
  if (f.id == "njf" || f.id == "pjf") return ".model " + nom + " " + f.id + "(" + parametres(f, v) + ")\n";
  if (f.id == "sw") return ".subckt " + nom + " 1 2 3 4\nS1 1 2 3 4 " + nom + "_sw\n.model " + nom + "_sw sw(" + parametres(f, v) + ")\n.ends " + nom + "\n";
  if (f.id == "csw") return ".subckt " + nom + " 1 2\nVcapteur 1 1c 0\nW1 1c 2 Vcapteur " + nom + "_csw\n.model " + nom + "_csw csw(" + parametres(f, v) + ")\n.ends " + nom + "\n";
  if (f.id == "mutuelle") return ".subckt " + nom + " 1 2 3 4\nL1 1 2 " + nb(v[0]) + "\nL2 3 4 " + nb(v[1]) + "\nK1 L1 L2 " + nb(v[2]) + "\n.ends " + nom + "\n";
  if (f.id == "ligne-t") return ".subckt " + nom + " 1 2 3 4\nT1 1 2 3 4 Z0=" + nb(v[0]) + " TD=" + nb(v[1]) + "\n.ends " + nom + "\n";
  if (f.id == "potentiometre") return ".subckt " + nom + " 1 2 3\nRa 1 2 " + nb(v[0] * v[1]) + "\nRb 2 3 " + nb(v[0] * (1 - v[1])) + "\n.ends " + nom + "\n";
  if (f.id == "gain")
    return ".subckt " + nom + " 1 2\nA1 1 2 " + nom + "_gain\n.model " + nom + "_gain gain(in_offset=" + nb(v[1]) + " gain=" + nb(v[0]) + " out_offset=" + nb(v[2]) + ")\n.ends " + nom + "\n";
  if (refus) *refus = "fiche « " + f.id + " » sans écriture";
  return std::nullopt;
}

// ---------- Modèles tirés de la fiche ----------
namespace {
// Problème de moindres carrés à jacobienne numérique, dense
class ProblemeDense : public prom::numerique::ProblemeMoindresCarres {
 public:
  ProblemeDense(int n, std::function<void(const std::vector<double>&, std::vector<double>&)> f, int m) : m_n(n), m_m(m), m_f(std::move(f)) {}
  int dimension() const override { return m_n; }
  void residus(const std::vector<double>& x, std::vector<double>& F) override {
    F.assign(static_cast<std::size_t>(m_m), 0);
    m_f(x, F);
  }
  void jacobienne(const std::vector<double>& x, prom::numerique::MatriceCreuse& J) override {
    std::vector<double> f0, f1;
    residus(x, f0);
    J.assign(static_cast<std::size_t>(m_m), {});
    for (int j = 0; j < m_n; ++j) {
      std::vector<double> y = x;
      const double h = 1e-7 * std::max(1.0, std::abs(x[static_cast<std::size_t>(j)]));
      y[static_cast<std::size_t>(j)] += h;
      residus(y, f1);
      for (int i = 0; i < m_m; ++i) J[static_cast<std::size_t>(i)].push_back({j, (f1[static_cast<std::size_t>(i)] - f0[static_cast<std::size_t>(i)]) / h});
    }
  }

 private:
  int m_n, m_m;
  std::function<void(const std::vector<double>&, std::vector<double>&)> m_f;
};
double tensionThermique(double temperature) { return 8.617333262e-5 * (temperature + 273.15); }
}  // namespace

AjustementModele ajusterDiode(const std::vector<PointFiche>& points, const std::string& nom, double temperature, double cjo, double tt, double bv) {
  AjustementModele r;
  r.nom = nom;
  r.genre = "diode";
  r.broches = "A K";
  if (points.size() < 2) {
    r.erreur = "il faut au moins deux points (courant, tension) relevés sur la fiche";
    return r;
  }
  for (const auto& p : points)
    if (!(p.x > 0 && p.y > 0)) {
      r.erreur = "un point a un courant ou une tension nulle ou négative";
      return r;
    }
  std::vector<PointFiche> pts = points;
  std::sort(pts.begin(), pts.end(), [](const PointFiche& a, const PointFiche& b) { return a.x < b.x; });
  const double vt = tensionThermique(temperature);
  // Départ : N tiré des deux plus faibles courants, IS du plus faible, RS du plus fort
  double n0 = std::clamp((pts[1].y - pts[0].y) / (vt * std::log(pts[1].x / pts[0].x)), 0.8, 3.0);
  double is0 = pts[0].x / std::expm1(pts[0].y / (n0 * vt));
  const double vdiode = n0 * vt * std::log(pts.back().x / is0 + 1);
  double rs0 = std::max(1e-3, (pts.back().y - vdiode) / pts.back().x);
  auto tension = [vt](double i, double is, double n, double rs) { return n * vt * std::log(i / is + 1) + i * rs; };
  ProblemeDense p(3, [&](const std::vector<double>& x, std::vector<double>& F) {
    const double is = std::exp(x[0]), n = x[1], rs = std::exp(x[2]);
    for (std::size_t i = 0; i < pts.size(); ++i) F[i] = (tension(pts[i].x, is, n, rs) - pts[i].y) / pts[i].y;
  }, static_cast<int>(pts.size()));
  std::vector<double> x{std::log(std::max(is0, 1e-30)), n0, std::log(rs0)};
  prom::numerique::OptionsLM o;
  o.iterations = 400;
  o.coutCible = 1e-20;
  prom::numerique::levenbergMarquardt(p, x, o);
  const double is = std::exp(x[0]), n = x[1], rs = std::exp(x[2]);
  if (!(n > 0.3 && n < 10 && is > 0 && std::isfinite(rs))) {
    r.erreur = "l’ajustement n’a pas convergé vers des valeurs physiques";
    return r;
  }
  r.parametres = {{"IS", is}, {"N", n}, {"RS", rs}};
  for (const auto& q : points) {
    const double e = std::abs(tension(q.x, is, n, rs) - q.y) / q.y;
    r.ecarts.push_back(e);
    r.ecartMax = std::max(r.ecartMax, e);
  }
  r.texte = ".model " + nom + " D(IS=" + nb(is) + " N=" + nb(n) + " RS=" + nb(rs) + (cjo > 0 ? " CJO=" + nb(cjo) : "") + (tt > 0 ? " TT=" + nb(tt) : "") + (bv > 0 ? " BV=" + nb(bv) : "") + ")\n";
  r.source = "ajusté sur " + std::to_string(points.size()) + " point(s) de la fiche, écart maximal " + nb(100 * r.ecartMax) + " %";
  r.reussi = true;
  return r;
}

AjustementModele ajusterVdmos(double vth, double idSeuil, double rdsOn, double vgsOn, double qg, double vgsQg, double qgd, double vdsQ, bool canalP, const std::string& nom) {
  AjustementModele r;
  r.nom = nom;
  r.genre = "mos";
  r.broches = "D G S";
  vth = std::abs(vth), vgsOn = std::abs(vgsOn), vgsQg = std::abs(vgsQg), vdsQ = std::abs(vdsQ);
  if (!(vth > 0 && idSeuil > 0 && rdsOn > 0 && vgsOn > vth)) {
    r.erreur = "seuil, courant au seuil et résistance passante à une tension de grille au-dessus du seuil sont attendus";
    return r;
  }
  // Id = KP/2 (Vgs − VTO)² au seuil ; Rds = 1 / (KP (Vgs − VTO)) à faible Vds
  ProblemeDense p(2, [&](const std::vector<double>& x, std::vector<double>& F) {
    const double vto = x[0], kp = std::exp(x[1]);
    const double ov = std::max(vth - vto, 1e-6), on = std::max(vgsOn - vto, 1e-6);
    F[0] = (kp / 2 * ov * ov - idSeuil) / idSeuil;
    F[1] = (1 / (kp * on) - rdsOn) / rdsOn;
  }, 2);
  std::vector<double> x{vth - 0.2, std::log(1 / (rdsOn * (vgsOn - vth + 0.2)))};
  prom::numerique::OptionsLM o;
  o.iterations = 400;
  prom::numerique::levenbergMarquardt(p, x, o);
  const double vto = x[0], kp = std::exp(x[1]);
  if (!(vto < vth && kp > 0 && std::isfinite(kp))) {
    r.erreur = "l’ajustement n’a pas convergé";
    return r;
  }
  const double cgs = qg > qgd && vgsQg > 0 ? (qg - qgd) / vgsQg : 1e-9;
  const double cgd = qgd > 0 && vdsQ > 0 ? qgd / vdsQ : 1e-10;
  r.parametres = {{"VTO", canalP ? -vto : vto}, {"KP", kp}, {"CGS", cgs}, {"CGDMAX", cgd * 3}, {"CGDMIN", cgd / 3}};
  const double ov = vth - vto, on = vgsOn - vto;
  r.ecarts = {std::abs(kp / 2 * ov * ov - idSeuil) / idSeuil, std::abs(1 / (kp * on) - rdsOn) / rdsOn};
  r.ecartMax = std::max(r.ecarts[0], r.ecarts[1]);
  r.texte = ".model " + nom + " VDMOS(" + std::string(canalP ? "pchan " : "") + "VTO=" + nb(canalP ? -vto : vto) + " KP=" + nb(kp) + " CGS=" + nb(cgs) + " CGDMAX=" + nb(cgd * 3) +
            " CGDMIN=" + nb(cgd / 3) + " A=1)\n";
  r.source = "ajusté sur le seuil, la résistance passante et les charges de grille de la fiche";
  r.reussi = true;
  return r;
}

AjustementModele modeleInterrupteur(double ron, double roff, double seuil, double hysteresis, const std::string& nom) {
  AjustementModele r;
  r.nom = nom;
  r.genre = "sous-circuit";
  r.broches = "1 2 3 4";
  if (!(ron > 0 && roff > ron)) {
    r.erreur = "la résistance ouverte doit dépasser la résistance fermée";
    return r;
  }
  r.parametres = {{"RON", ron}, {"ROFF", roff}, {"VT", seuil}, {"VH", std::abs(hysteresis)}};
  r.texte = ".subckt " + nom + " 1 2 3 4\nS1 1 2 3 4 " + nom + "_sw\n.model " + nom + "_sw sw(vt=" + nb(seuil) + " vh=" + nb(std::abs(hysteresis)) + " ron=" + nb(ron) + " roff=" + nb(roff) +
            ")\n.ends " + nom + "\n";
  r.source = "résistances fermée et ouverte, seuil et hystérésis de la fiche";
  r.reussi = true;
  return r;
}

AjustementModele modeleTransformateur(double lPrimaire, double rapport, double lFuite, const std::string& nom) {
  AjustementModele r;
  r.nom = nom;
  r.genre = "sous-circuit";
  r.broches = "1 2 3 4";
  if (!(lPrimaire > 0 && rapport > 0 && lFuite >= 0 && lFuite < lPrimaire)) {
    r.erreur = "inductance du primaire, rapport et inductance de fuite (sous celle du primaire) sont attendus";
    return r;
  }
  const double k = std::sqrt(1 - lFuite / lPrimaire), l2 = lPrimaire * rapport * rapport;
  r.parametres = {{"L1", lPrimaire}, {"L2", l2}, {"K", k}};
  r.texte = ".subckt " + nom + " 1 2 3 4\nL1 1 2 " + nb(lPrimaire) + "\nL2 3 4 " + nb(l2) + "\nK1 L1 L2 " + nb(k) + "\n.ends " + nom + "\n";
  r.source = "k = √(1 − Lfuite / Lmagnétisante)";
  r.reussi = true;
  return r;
}

void rattacherModele(Article& a, const AjustementModele& m) {
  ModeleSpice s;
  s.texte = m.texte;
  s.nom = m.nom;
  s.genre = m.genre;
  s.source = m.source;
  a.modeleSpice = s;
}

// ---------- IBIS ----------
namespace {
double nombreIbis(const std::string& t0, bool* lu = nullptr) {
  std::string t = t0;
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
  if (lu) *lu = false;
  if (t.empty() || minuscules(t) == "na") return std::numeric_limits<double>::quiet_NaN();
  // « 3.0/30n » : une pente
  if (const auto b = t.find('/'); b != std::string::npos) {
    bool a1 = false, a2 = false;
    const double x = nombreIbis(t.substr(0, b), &a1), y = nombreIbis(t.substr(b + 1), &a2);
    if (lu) *lu = a1 && a2 && y != 0;
    return y != 0 ? x / y : std::numeric_limits<double>::quiet_NaN();
  }
  char* fin = nullptr;
  double v = std::strtod(t.c_str(), &fin);
  if (fin == t.c_str()) return std::numeric_limits<double>::quiet_NaN();
  if (lu) *lu = true;
  if (*fin) switch (*fin) {
      case 'T': v *= 1e12; break;
      case 'G': v *= 1e9; break;
      case 'M': v *= 1e6; break;
      case 'k': case 'K': v *= 1e3; break;
      case 'm': v *= 1e-3; break;
      case 'u': v *= 1e-6; break;
      case 'n': v *= 1e-9; break;
      case 'p': v *= 1e-12; break;
      case 'f': v *= 1e-15; break;
      default: break;
    }
  return v;
}
std::string motCle(const std::string& l) {
  std::string m;
  for (const char c : l.substr(1, l.find(']') - 1)) m += c == '_' ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return m;
}
std::vector<std::string> mots(const std::string& l) {
  std::istringstream in(l);
  std::vector<std::string> v;
  for (std::string w; in >> w;) v.push_back(w);
  return v;
}
void triple(const std::vector<std::string>& v, std::size_t debut, double (&d)[3]) {
  for (int i = 0; i < 3; ++i) {
    const double x = debut + static_cast<std::size_t>(i) < v.size() ? nombreIbis(v[debut + static_cast<std::size_t>(i)]) : std::numeric_limits<double>::quiet_NaN();
    d[i] = std::isnan(x) ? (i == 0 ? 0 : d[0]) : x;
  }
}
double interp(const std::vector<double>& x, const std::vector<double>& y, double x0) {
  if (x.empty()) return 0;
  if (x.size() == 1) return y[0];
  if (x0 <= x.front()) return y[0] + (x0 - x[0]) * (y[1] - y[0]) / (x[1] - x[0]);
  if (x0 >= x.back()) {
    const std::size_t n = x.size();
    return y[n - 1] + (x0 - x[n - 1]) * (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]);
  }
  const auto it = std::upper_bound(x.begin(), x.end(), x0);
  const std::size_t i = static_cast<std::size_t>(it - x.begin());
  return y[i - 1] + (x0 - x[i - 1]) / (x[i] - x[i - 1]) * (y[i] - y[i - 1]);
}
}  // namespace

const ModeleIbis* FichierIbis::modele(const std::string& nom) const {
  for (const auto& m : modeles)
    if (minuscules(m.nom) == minuscules(nom)) return &m;
  for (const auto& b : broches)
    if (b.numero == nom && minuscules(b.modele) != minuscules(nom)) return modele(b.modele);
  if (const auto it = selecteurs.find(nom); it != selecteurs.end() && !it->second.empty()) return modele(it->second.front());
  return nullptr;
}

std::optional<FichierIbis> lireIbis(const std::string& texte, std::string* erreur) {
  FichierIbis f;
  char commentaire = '|';
  std::istringstream in(texte);
  std::string section;
  ModeleIbis* m = nullptr;
  TableIbis* table = nullptr;
  FormeOndeIbis* onde = nullptr;
  std::string selecteur;
  bool ignorer = false;
  int numero = 0;
  for (std::string l; std::getline(in, l);) {
    ++numero;
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (const auto p = l.find(commentaire); p != std::string::npos) l = l.substr(0, p);
    const auto debut = l.find_first_not_of(" \t");
    if (debut == std::string::npos) continue;
    l = l.substr(debut);
    if (l[0] == '[') {
      section = motCle(l);
      const std::string reste = l.find(']') == std::string::npos ? std::string() : l.substr(l.find(']') + 1);
      const auto v = mots(reste);
      table = nullptr;
      if (ignorer) {
        if (section.rfind("end ", 0) == 0) ignorer = false;
        continue;
      }
      if (section == "end") break;
      if (section == "ibis ver") f.version = v.empty() ? std::string() : v[0];
      else if (section == "comment char" && !v.empty()) commentaire = v[0][0];
      else if (section == "component") f.composant = reste.substr(reste.find_first_not_of(" \t") == std::string::npos ? 0 : reste.find_first_not_of(" \t")), m = nullptr;
      else if (section == "manufacturer") f.fabricant = reste.substr(reste.find_first_not_of(" \t") == std::string::npos ? 0 : reste.find_first_not_of(" \t"));
      else if (section == "model") {
        f.modeles.push_back({});
        m = &f.modeles.back();
        m->nom = v.empty() ? std::string() : v[0];
        onde = nullptr;
      } else if (section == "model selector") selecteur = v.empty() ? std::string() : v[0];
      else if (section == "voltage range" && m) triple(v, 0, m->tension);
      else if (m && (section == "pullup" || section == "pulldown" || section == "gnd clamp" || section == "power clamp"))
        table = section == "pullup" ? &m->pullup : section == "pulldown" ? &m->pulldown : section == "gnd clamp" ? &m->masse : &m->alimentation;
      else if (m && (section == "rising waveform" || section == "falling waveform")) {
        auto& liste = section == "rising waveform" ? m->montees : m->descentes;
        liste.push_back({});
        onde = &liste.back();
        table = &onde->table;
      } else if (section.find("algorithmic model") != std::string::npos || section == "test data" || section == "test load")
        ignorer = true;
      continue;
    }
    if (ignorer) continue;
    // Lignes « clé = valeur » ou « clé valeurs »
    std::string ligne = l;
    for (char& c : ligne)
      if (c == '=') c = ' ';
    const auto v = mots(ligne);
    if (v.empty()) continue;
    const std::string cle = minuscules(v[0]);
    if (section == "package") {
      if (cle == "r_pkg") triple(v, 1, f.rPkg);
      else if (cle == "l_pkg") triple(v, 1, f.lPkg);
      else if (cle == "c_pkg") triple(v, 1, f.cPkg);
      continue;
    }
    if (section == "pin") {
      if (v.size() >= 3 && cle != "signal_name") {
        BrocheIbis b;
        b.numero = v[0], b.signal = v[1], b.modele = v[2];
        if (v.size() >= 6) b.r = nombreIbis(v[3]), b.l = nombreIbis(v[4]), b.c = nombreIbis(v[5]);
        for (double* x : {&b.r, &b.l, &b.c})
          if (std::isnan(*x)) *x = 0;
        f.broches.push_back(b);
      }
      continue;
    }
    if (section == "model selector") {
      f.selecteurs[selecteur].push_back(v[0]);
      continue;
    }
    if (!m) continue;
    if (section == "model") {
      if (cle == "model_type" && v.size() > 1) m->genre = v[1];
      else if (cle == "polarity" && v.size() > 1) m->inverse = minuscules(v[1]) == "inverting";
      else if (cle == "c_comp") triple(v, 1, m->cComp);
      else if (cle == "c_comp_pullup" || cle == "c_comp_pulldown" || cle == "c_comp_gnd_clamp" || cle == "c_comp_power_clamp") {
        double d[3];
        triple(v, 1, d);
        for (int i = 0; i < 3; ++i) m->cComp[i] += d[i];
      } else if (cle == "vinl" && v.size() > 1) m->vinl = nombreIbis(v[1]);
      else if (cle == "vinh" && v.size() > 1) m->vinh = nombreIbis(v[1]);
      continue;
    }
    if (section == "ramp") {
      if (cle == "dv/dt_r") triple(v, 1, m->rampeMontee);
      else if (cle == "dv/dt_f") triple(v, 1, m->rampeDescente);
      else if (cle == "r_load" && v.size() > 1) m->rLoad = nombreIbis(v[1]);
      continue;
    }
    if (onde && table == &onde->table) {
      if (cle == "r_fixture" && v.size() > 1) onde->rFixture = nombreIbis(v[1]);
      else if (cle == "v_fixture" && v.size() > 1) onde->vFixture = nombreIbis(v[1]);
      else if (cle == "c_fixture" && v.size() > 1) onde->cFixture = nombreIbis(v[1]);
      else if (cle == "l_fixture" && v.size() > 1) onde->lFixture = nombreIbis(v[1]);
      else if (cle.rfind("v_fixture_", 0) == 0 || cle.rfind("r_dut", 0) == 0 || cle.rfind("l_dut", 0) == 0 || cle.rfind("c_dut", 0) == 0) {
      } else if (bool lu = false; (nombreIbis(v[0], &lu), lu)) {
        double d[3];
        triple(v, 1, d);
        table->x.push_back(nombreIbis(v[0]));
        table->typ.push_back(d[0]), table->min.push_back(d[1]), table->max.push_back(d[2]);
      }
      continue;
    }
    if (table) {
      bool lu = false;
      nombreIbis(v[0], &lu);
      if (!lu) continue;
      double d[3];
      triple(v, 1, d);
      table->x.push_back(nombreIbis(v[0]));
      table->typ.push_back(d[0]), table->min.push_back(d[1]), table->max.push_back(d[2]);
    }
  }
  if (f.modeles.empty()) {
    if (erreur) *erreur = "aucun [Model] : pas un fichier IBIS lisible";
    return std::nullopt;
  }
  // Tables rangées par abscisse croissante
  for (auto& x : f.modeles)
    for (TableIbis* t : {&x.pullup, &x.pulldown, &x.masse, &x.alimentation}) {
      std::vector<std::size_t> ordre(t->x.size());
      for (std::size_t i = 0; i < ordre.size(); ++i) ordre[i] = i;
      std::sort(ordre.begin(), ordre.end(), [&](std::size_t a, std::size_t b) { return t->x[a] < t->x[b]; });
      TableIbis u;
      for (std::size_t i : ordre) u.x.push_back(t->x[i]), u.typ.push_back(t->typ[i]), u.min.push_back(t->min[i]), u.max.push_back(t->max[i]);
      *t = u;
    }
  return f;
}

namespace {
// « pwl(x, x1, y1, …) » d'une table I-V au coin voulu ; vide sans table
std::string tableSpice(const TableIbis& t, int coin, const std::string& x) {
  if (t.x.size() < 2) return {};
  std::string r = "pwl(" + x;
  for (std::size_t i = 0; i < t.x.size(); ++i) r += ", " + nb(t.x[i]) + ", " + nb(t.coin(coin)[i]);
  return r + ")";
}
// Coefficients de tirage du haut et du bas, Ku(t) et Kd(t), d'après les formes d'onde d'un même front : avec deux
// charges d'essai différentes, le système de deux équations est résolu à chaque instant ; avec une seule, Kd = 1 − Ku.
// À chaque charge : courant des tables pondéré = courant de la charge, de la capacité de la puce et des écrêteurs.
struct Coefficient {
  double t, ku, kd;
};
std::vector<Coefficient> coefficients(const ModeleIbis& m, const std::vector<FormeOndeIbis>& ondes, int coin) {
  std::vector<Coefficient> r;
  if (ondes.empty() || ondes[0].table.x.size() < 2) return r;
  const double vcc = m.tension[coin] != 0 ? m.tension[coin] : m.tension[0];
  const FormeOndeIbis& a = ondes[0];
  const FormeOndeIbis* b = nullptr;
  for (std::size_t i = 1; i < ondes.size(); ++i)
    if (ondes[i].table.x.size() >= 2 && std::abs(ondes[i].vFixture - a.vFixture) > 1e-6) {
      b = &ondes[i];
      break;
    }
  auto courant = [&](const TableIbis& tb, double x) { return tb.x.size() >= 2 ? interp(tb.x, tb.coin(coin), x) : 0.0; };
  // Termes d'une charge à l'instant t : (Ipu, Ipd, second membre)
  auto termes = [&](const FormeOndeIbis& w, double t, double h, double& ipu, double& ipd, double& droite) {
    const std::vector<double>& v = w.table.coin(coin);
    const double vi = interp(w.table.x, v, t);
    const double dv = (interp(w.table.x, v, t + h / 2) - interp(w.table.x, v, t - h / 2)) / h;
    const double charge = (vi - w.vFixture) / std::max(w.rFixture, 1e-9) + (m.cComp[coin] + w.cFixture) * dv;
    ipu = courant(m.pullup, vcc - vi);
    ipd = courant(m.pulldown, vi);
    droite = -charge - courant(m.masse, vi) - courant(m.alimentation, vcc - vi);
  };
  const double t0 = a.table.x.front(), t1 = b ? std::min(a.table.x.back(), b->table.x.back()) : a.table.x.back();
  const int n = 400;
  const double h = (t1 - t0) / n;
  Coefficient precedent{0, 0, 1};
  for (int k = 0; k <= n; ++k) {
    const double t = t0 + h * k;
    double pa, da, sa;
    termes(a, t, h, pa, da, sa);
    Coefficient c{t - t0, precedent.ku, precedent.kd};
    if (b) {
      double pb, db, sb;
      termes(*b, t, h, pb, db, sb);
      const double det = pa * db - pb * da;
      if (std::abs(det) > 1e-18) {
        c.ku = (sa * db - sb * da) / det;
        c.kd = (pa * sb - pb * sa) / det;
      }
    } else if (std::abs(pa - da) > 1e-12) {
      c.ku = (sa - da) / (pa - da);
      c.kd = 1 - c.ku;
    }
    c.ku = std::clamp(c.ku, -0.5, 3.0);
    c.kd = std::clamp(c.kd, -0.5, 3.0);
    precedent = c;
    r.push_back(c);
  }
  return r;
}
}  // namespace

AjustementModele tamponIbis(const FichierIbis& f, const OptionsTamponIbis& o, const std::string& nom) {
  AjustementModele r;
  r.nom = nom;
  r.genre = "ibis";
  const ModeleIbis* m = f.modele(o.modele);
  if (!m) {
    r.erreur = "modèle IBIS « " + o.modele + " » absent du fichier";
    return r;
  }
  const int coin = std::clamp(o.coin, 0, 2);
  const double vcc = m->tension[coin] != 0 ? m->tension[coin] : m->tension[0];
  if (vcc == 0) {
    r.erreur = "le modèle n’a pas de plage de tension ([Voltage Range])";
    return r;
  }
  // Coefficients de montée et de descente : formes d'onde, sinon la rampe (20 à 80 % de l'excursion)
  std::vector<Coefficient> montee = coefficients(*m, m->montees, coin), descente = coefficients(*m, m->descentes, coin);
  auto rampe = [&](double pente, bool monte) {
    const double p = pente > 0 ? pente : 1e9;
    const double tr = vcc / p;  // excursion entière à la pente donnée (20 à 80 % de la rampe)
    return std::vector<Coefficient>{{0, monte ? 0.0 : 1.0, monte ? 1.0 : 0.0}, {tr, monte ? 1.0 : 0.0, monte ? 0.0 : 1.0}};
  };
  if (montee.empty()) montee = rampe(m->rampeMontee[coin] != 0 ? m->rampeMontee[coin] : m->rampeMontee[0], true);
  if (descente.empty()) descente = rampe(m->rampeDescente[coin] != 0 ? m->rampeDescente[coin] : m->rampeDescente[0], false);
  // Ku(t) et Kd(t) pour le stimulus : fronts alternés, le premier montant (descendant si la polarité est inverse)
  auto pwlK = [&](bool inverse, bool haut0) {
    std::vector<std::pair<double, double>> p{{0, (inverse ? descente.back() : descente.front()).ku}};
    if (!haut0) p[0].second = inverse ? montee.back().kd : montee.front().kd;
    if (inverse && haut0) p[0].second = montee.back().ku;
    if (!inverse && haut0) p[0].second = descente.back().ku;
    bool haut = inverse;
    for (std::size_t i = 0; i < o.fronts.size(); ++i) {
      const double t = o.fronts[i];
      const auto& c = !haut ? montee : descente;
      const double fin = i + 1 < o.fronts.size() ? o.fronts[i + 1] : std::numeric_limits<double>::infinity();
      for (const auto& k : c)
        if (t + k.t > p.back().first && t + k.t < fin) p.push_back({t + k.t, haut0 ? k.ku : k.kd});
      haut = !haut;
    }
    std::string s = "PWL(";
    for (std::size_t i = 0; i < p.size(); ++i) s += (i ? " " : "") + nb(p[i].first) + " " + nb(p[i].second);
    return s + ")";
  };
  std::string corps;
  auto tampon = [&](const std::string& suffixe, const std::string& sortie, bool inverse) {
    const std::string die = o.boitier ? "die" + suffixe : sortie;
    corps += "VKU" + suffixe + " ku" + suffixe + " 0 " + pwlK(inverse != m->inverse, true) + "\n";
    corps += "VKD" + suffixe + " kd" + suffixe + " 0 " + pwlK(inverse != m->inverse, false) + "\n";
    std::string i;
    const std::string vd = "V(" + die + ")";
    if (const std::string pu = tableSpice(m->pullup, coin, nb(vcc) + " - " + vd); !pu.empty()) i += "V(ku" + suffixe + ")*" + pu;
    if (const std::string pd = tableSpice(m->pulldown, coin, vd); !pd.empty()) i += std::string(i.empty() ? "" : " + ") + "V(kd" + suffixe + ")*" + pd;
    if (const std::string gc = tableSpice(m->masse, coin, vd); !gc.empty()) i += std::string(i.empty() ? "" : " + ") + gc;
    if (const std::string pc = tableSpice(m->alimentation, coin, nb(vcc) + " - " + vd); !pc.empty()) i += std::string(i.empty() ? "" : " + ") + pc;
    if (i.empty()) i = "0";
    corps += "B" + suffixe + " " + die + " 0 I = " + i + "\n";
    if (m->cComp[coin] > 0) corps += "CCOMP" + suffixe + " " + die + " 0 " + nb(m->cComp[coin]) + "\n";
    if (o.boitier) {
      corps += "RPKG" + suffixe + " " + die + " m" + suffixe + " " + nb(std::max(f.rPkg[coin], 1e-6)) + "\n";
      corps += "LPKG" + suffixe + " m" + suffixe + " " + sortie + " " + nb(std::max(f.lPkg[coin], 1e-15)) + "\n";
      if (f.cPkg[coin] > 0) corps += "CPKG" + suffixe + " " + sortie + " 0 " + nb(f.cPkg[coin]) + "\n";
    }
  };
  if (o.differentiel) {
    tampon("P", "outp", false);
    tampon("N", "outn", true);
    r.broches = "outp outn";
  } else {
    tampon("", "out", false);
    r.broches = "out";
  }
  r.texte = "* Tampon IBIS « " + m->nom + " » de " + (f.composant.empty() ? std::string("composant") : f.composant) + ", coin " + (coin == 0 ? "typique" : coin == 1 ? "minimal" : "maximal") +
            "\n.subckt " + nom + " " + (o.differentiel ? "outp outn" : "out") + "\n" + corps + ".ends " + nom + "\n";
  r.source = "IBIS " + f.version + " : " + f.composant + (f.fabricant.empty() ? std::string() : " (" + f.fabricant + ")") + ", modèle " + m->nom;
  r.reussi = true;
  return r;
}

// ---------- Verilog-A ----------
std::optional<ModuleVerilogA> lireVerilogA(const std::string& source, std::string* erreur) {
  // Commentaires retirés
  std::string s = std::regex_replace(source, std::regex(R"(//[^\n]*)"), "");
  s = std::regex_replace(s, std::regex(R"(/\*[\s\S]*?\*/)"), "");
  static const std::regex module(R"(\bmodule\s+([A-Za-z_][A-Za-z0-9_$]*)\s*\(([^)]*)\)\s*;)");
  std::smatch m;
  if (!std::regex_search(s, m, module)) {
    if (erreur) *erreur = "aucun « module nom(ports); » dans le fichier Verilog-A";
    return std::nullopt;
  }
  ModuleVerilogA v;
  v.nom = m[1].str();
  std::istringstream ports(m[2].str());
  for (std::string p; std::getline(ports, p, ',');) {
    p.erase(std::remove_if(p.begin(), p.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), p.end());
    if (!p.empty()) v.ports.push_back(p);
  }
  static const std::regex parametre(R"(\bparameter\s+(?:real|integer)?\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^;]+?)\s*(?:from[^;]*)?;)");
  for (std::sregex_iterator it(s.begin(), s.end(), parametre), fin; it != fin; ++it) v.parametres[(*it)[1].str()] = (*it)[2].str();
  return v;
}

bool openvafDisponible(std::string* raison) {
  const char* chemin = std::getenv("PATH");
  if (chemin) {
#ifdef _WIN32
    const char sep = ';';
    const std::vector<std::string> noms = {"openvaf.exe", "openvaf-r.exe"};
#else
    const char sep = ':';
    const std::vector<std::string> noms = {"openvaf", "openvaf-r"};
#endif
    std::istringstream in(chemin);
    for (std::string d; std::getline(in, d, sep);)
      for (const auto& n : noms) {
        std::error_code ec;
        if (!d.empty() && std::filesystem::is_regular_file(std::filesystem::path(d) / n, ec)) return true;
      }
  }
  if (raison) *raison = "OpenVAF n’est pas installé (programme « openvaf » introuvable dans le PATH) : les modèles Verilog-A ne se compilent pas sur ce poste";
  return false;
}

std::vector<std::string> commandeOpenvaf(const std::string& source, const std::string& osdi) { return {"openvaf", source, "-o", osdi}; }

ModeleSpice modeleVerilogA(const ModuleVerilogA& m, const std::string& source, const std::string& osdi) {
  ModeleSpice s;
  s.nom = m.nom + "_va";
  s.texte = ".model " + s.nom + " " + m.nom + "\n";
  s.genre = "verilog-a";
  s.fichier = osdi;
  s.source = "module Verilog-A « " + m.nom + " » compilé par OpenVAF";
  (void)source;
  return s;
}

}  // namespace prom::ecad
