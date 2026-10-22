// Prométhée : calcul d'une installation basse tension (méthode des impédances du guide UTE C 15-105, règles de la
// NF C 15-100). Conventions : impédances en milliohms, courants en ampères (kA pour les courts-circuits),
// sections en mm², longueurs en mètres. Facteurs de tension c = 1,05 (maximal) et 0,95 (minimal), facteur de charge
// à vide m = 1,05 ; résistivités ρ0 = 18,51 mΩ·mm²/m (cuivre) et 29,41 (aluminium) à 20 °C, ρ1 = 1,25 ρ0 (défauts
// coupés par disjoncteur), ρ2 = 1,5 ρ0 (par fusible), 22,5 et 36 en service normal (chutes de tension, annexe G) ;
// réactance 0,08 mΩ/m (multiconducteur), 0,09 (unipolaires jointifs). Licence GPL-3.0-only.
#include "promethee/electrique_distribution.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <set>

#include "electrique_interne.hpp"

namespace prom::elec {

using detail::nombreFr;

namespace {

constexpr double C_MAX = 1.05, C_MIN = 0.95, M_CHARGE = 1.05;
constexpr double COS_DEMARRAGE = 0.35;
const double RACINE3 = std::sqrt(3.0);

double rho0(Ame a) { return a == Ame::Cuivre ? 0.01851 : 0.02941; }
double rhoService(Ame a) { return a == Ame::Cuivre ? 0.0225 : 0.036; }
double lambdaCable(bool unipolaire) { return unipolaire ? 0.09 : 0.08; }

struct Z {
  double r = 0, x = 0;  // mΩ
  double module() const { return std::hypot(r, x); }
  Z operator+(const Z& o) const { return {r + o.r, x + o.x}; }
  Z operator*(double k) const { return {r * k, x * k}; }
  Z resistanceFois(double k) const { return {r * k, x}; }
};

Z reseauHta(double sccMVA, double u) {
  if (sccMVA <= 0) return {};
  const double zq = (M_CHARGE * u) * (M_CHARGE * u) / (sccMVA * 1e6) * 1000.0;
  const double xq = 0.995 * zq;
  return {0.1 * xq, xq};
}
Z impedanceTransformateur(double kva, double ucc, double pcu, double u) {
  const double sn = std::max(1.0, kva) * 1000.0;
  const double zt = (M_CHARGE * u) * (M_CHARGE * u) / sn * std::max(0.5, ucc) / 100.0 * 1000.0;
  if (pcu > 0) {
    const double rt = std::min(0.99 * zt, pcu * (M_CHARGE * u) * (M_CHARGE * u) / (sn * sn) * 1000.0);
    return {rt, std::sqrt(zt * zt - rt * rt)};
  }
  return {0.31 * zt, 0.95 * zt};
}
// Conducteur de « longueur » m et de section « s » (n en parallèle), résistance à ρ0 × facteur
Z conducteur(double longueur, double s, int n, Ame a, bool unipolaire, double facteurRho = 1.0) {
  if (s <= 0 || n <= 0 || longueur <= 0) return {};
  return {facteurRho * rho0(a) * longueur / (s * n) * 1000.0, lambdaCable(unipolaire) * longueur / n};
}

struct ImpedancesSource {
  Z max, min;              // par phase
  Z boucleMax, boucleMin;  // contribution de la source à la boucle d'un défaut monophasé
  double in = 0;           // A
  bool mono = false;
};

ImpedancesSource impedancesSource(const SourceBT& s) {
  ImpedancesSource z;
  const double u = s.tension > 0 ? s.tension : 400;
  const double u0 = u / RACINE3;
  switch (s.type) {
    case TypeSourceBT::Transformateur: {
      const Z t = reseauHta(s.sccAmont, u) + impedanceTransformateur(s.puissance, s.ucc, s.pertesCuivre, u);
      z.max = z.min = z.boucleMax = z.boucleMin = t;
      z.in = s.puissance * 1000.0 / (RACINE3 * u);
      break;
    }
    case TypeSourceBT::ReseauPublic: {
      // Réseau conventionnel des prescriptions du distributeur (NF C 14-100) : transformateur 1 000 kVA, ucc 6 % ;
      // liaison L1 de 6 m en aluminium (4 × 240 mm² par phase), liaison L2 de 15 m en aluminium 240 mm²
      const Z t = reseauHta(500, u) + impedanceTransformateur(1000, 6, 0, u);
      const Z l0 = conducteur(6, 240, 4, Ame::Aluminium, true) + conducteur(15, 240, 1, Ame::Aluminium, false);
      const Z l1 = conducteur(6, 240, 4, Ame::Aluminium, true, 1.25) + conducteur(15, 240, 1, Ame::Aluminium, false, 1.25);
      z.max = t + l0;
      z.min = t + l1;
      z.boucleMax = t + l0 * 2.0;
      z.boucleMin = t + l1 * 2.0;
      z.in = s.puissance * 1000.0 / (RACINE3 * u);
      break;
    }
    case TypeSourceBT::BranchementLimite: {
      // Courant de court-circuit donné par le distributeur au point de livraison ; R = X faute de mieux
      const double icc = std::max(0.1, s.icc) * 1000.0;
      const double zs = C_MAX * M_CHARGE * u0 / icc * 1000.0;
      const Z zr{zs / std::sqrt(2.0), zs / std::sqrt(2.0)};
      if (s.triphase) {
        z.max = z.min = zr;
        z.boucleMax = z.boucleMin = zr * 2.0;
        z.in = s.puissance * 1000.0 / (RACINE3 * u);
      } else {
        z.boucleMax = z.boucleMin = zr;  // l'Icc d'un branchement monophasé est un courant phase-neutre
        z.max = z.min = zr * 0.5;
        z.in = s.puissance * 1000.0 / u0;
        z.mono = true;
      }
      break;
    }
    case TypeSourceBT::Groupe: {
      const double base = (M_CHARGE * u) * (M_CHARGE * u) / (std::max(1.0, s.puissance) * 1000.0) * 1000.0;
      const double xs = base * s.xdSub / 100.0, xt = base * s.xdTrans / 100.0, x0 = base * s.x0 / 100.0;
      z.max = {0.1 * xs, xs};
      z.min = {0.1 * xt, xt};
      // Défaut monophasé : (Z1 + Z2 + Z0) / 3, la réactance inverse prise égale à la subtransitoire
      z.boucleMax = {0.1 * xs, (2 * xs + x0) / 3.0};
      z.boucleMin = {0.1 * xt, (xt + xs + x0) / 3.0};
      z.in = s.puissance * 1000.0 / (RACINE3 * u);
      break;
    }
  }
  return z;
}

// ---------- Gammes normalisées ----------
const std::vector<double> CALIBRES_MODULAIRES = {1, 2, 3, 4, 6, 10, 13, 16, 20, 25, 32, 40, 50, 63, 80, 100, 125};
const std::vector<double> CALIBRES_REGLABLES = {100, 160, 250, 400, 630, 800, 1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000, 6300};
const std::vector<double> PDC_MODULAIRES = {4.5, 6, 10, 15, 25, 36, 50};
const std::vector<double> PDC_REGLABLES = {25, 36, 50, 70, 100, 150};
const std::vector<double> PDC_MOTEUR = {15, 25, 50, 100};
const std::vector<double> SECTIONS = {1.5, 2.5, 4, 6, 10, 16, 25, 35, 50, 70, 95, 120, 150, 185, 240};
struct Plage {
  double min, max;
};
const std::vector<Plage> PLAGES_MOTEUR = {{0.1, 0.16}, {0.16, 0.25}, {0.25, 0.4}, {0.4, 0.63}, {0.63, 1}, {1, 1.6}, {1.6, 2.5}, {2.5, 4}, {4, 6.3},
                                          {6, 10},     {9, 14},      {13, 18},     {17, 23},     {20, 25},  {24, 32}, {30, 40},  {37, 50},   {48, 65}};
// Fusibles gG : portes de la CEI 60269-1 (A) — Imin 10 s, Imax 5 s, Imin 0,1 s, Imax 0,1 s
struct PorteFusible {
  double in, imin10, imax5, imin01, imax01;
};
const std::vector<PorteFusible> PORTES_GG = {{16, 33, 65, 85, 150},        {20, 42, 85, 110, 200},       {25, 52, 110, 150, 260},     {32, 75, 150, 200, 350},
                                             {40, 95, 190, 260, 450},       {50, 125, 250, 350, 610},     {63, 160, 320, 450, 820},    {80, 215, 425, 610, 1100},
                                             {100, 290, 580, 820, 1450},    {125, 355, 715, 1100, 1910},  {160, 465, 950, 1450, 2590}, {200, 610, 1250, 1910, 3420},
                                             {250, 735, 1650, 2590, 4500},  {315, 1050, 2200, 3420, 6000}, {400, 1150, 2840, 4500, 8060}, {500, 1650, 3800, 6000, 10600},
                                             {630, 2200, 5100, 8060, 14140}};
const PorteFusible* porteGG(double in) {
  for (const auto& p : PORTES_GG)
    if (std::abs(p.in - in) < 1e-6) return &p;
  return nullptr;
}
double superieur(const std::vector<double>& gamme, double v) {
  for (double x : gamme)
    if (x + 1e-9 >= v) return x;
  return 0;
}
double sectionSuperieure(double s) {
  for (double x : SECTIONS)
    if (x + 1e-9 >= s) return x;
  return SECTIONS.back();
}

// Coefficient k des conducteurs (NF C 15-100, tableau 43A ; conducteurs de protection incorporés au câble)
double coefficientK(Ame a, Isolant i) {
  if (a == Ame::Cuivre) return i == Isolant::PVC ? 115 : 143;
  return i == Isolant::PVC ? 76 : 94;
}
// Section du conducteur de protection (NF C 15-100, tableau 54F) : même métal que les phases
double sectionProtection(double sph) {
  if (sph <= 16 + 1e-9) return sph;
  if (sph <= 35 + 1e-9) return 16;
  return sectionSuperieure(sph / 2.0);
}

// I²t maximal laissé passer (A²s) par un disjoncteur modulaire de classe de limitation 3 (NF EN 60898-1, annexe ZA) ;
// −1 hors du champ de la classe (calibre au-delà de 32 A, courbe D)
double i2tClasse3(double in, const std::string& courbe, double ik) {
  if (in > 32 + 1e-9 || courbe == "D") return -1;
  static const std::array<double, 4> I = {3000, 4500, 6000, 10000};
  static const std::array<double, 4> B16 = {15000, 25000, 35000, 70000}, C16 = {18000, 30000, 42000, 84000};
  static const std::array<double, 4> B32 = {18000, 32000, 45000, 90000}, C32 = {22000, 39000, 55000, 110000};
  const auto& t = in <= 16 + 1e-9 ? (courbe == "B" ? B16 : C16) : (courbe == "B" ? B32 : C32);
  if (ik <= I[0]) return t[0] * (ik / I[0]) * (ik / I[0]);
  // Au-delà de 10 kA (hors du champ de la NF EN 60898-1) : un appareil limiteur laisse passer une énergie qui croît
  // comme le courant présumé, pas comme son carré
  if (ik >= I[3]) return t[3] * (ik / I[3]);
  for (std::size_t k = 1; k < I.size(); ++k)
    if (ik <= I[k]) {
      const double f = (ik - I[k - 1]) / (I[k] - I[k - 1]);
      return t[k - 1] + f * (t[k] - t[k - 1]);
    }
  return t[3];
}

std::string kA(double v) { return nombreFr(v, v < 10 ? 2 : 1) + " kA"; }
std::string amperes(double v) { return nombreFr(v, v < 10 ? 2 : v < 100 ? 1 : 0) + " A"; }
std::string pourcent(double v) { return nombreFr(v, 2) + " %"; }

bool polariteMono(Polarite p) { return p == Polarite::Monophase; }
bool avecNeutre(Polarite p) { return p != Polarite::Triphase; }

// Ce qu'il faut savoir d'une protection pour la sélectivité
struct ProtectionVue {
  TypeProtection type = TypeProtection::Modulaire;
  double in = 0, ir = 0, imBas = 0, imHaut = 0;
  bool retarde = false;
  double differentiel = 0;
  bool selectifDiff = false;
  std::string repere;
};

// Contexte transmis d'un tableau à ses départs
struct Amont {
  Z phase0, neutre0, pe0;     // sommes des canalisations amont à ρ0 (mΩ)
  double chute = 0;           // % cumulée jusqu'au jeu de barres
  double rChute = 0, xChute = 0;  // Ω : b·R (service) et b·X des canalisations amont, pour le démarrage des moteurs
  double ik3Bus = 0, ik1Bus = 0;  // kA au jeu de barres
  int protection = -1;        // départ (tableau) dont la protection est en amont
  double differentiel = 0;    // mA du différentiel le plus proche en amont
  int profondeur = 0;
  bool mono = false;          // jeu de barres monophasé
};

// Grandeurs d'une canalisation essayée (courants de court-circuit en kA : volts sur milliohms)
struct Evaluation {
  double s = 0, sn = 0, spe = 0;
  int n = 1;
  double k = 1, iz = 0;
  double chute = 0, chuteCumulee = 0;
  double ik3max = 0, ik1max = 0, ikMin = 0, ief = 0, iefIT = 0;
  Z phase0, neutre0, pe0;  // de cette canalisation, ρ0
  double rService = 0, x = 0;  // Ω, une phase
  bool okIz = false, okChute = false, okCourt = false, okPersonnes = false, okThermique = false, okThermiquePE = false;
  bool thermiqueEstimee = false;  // contrainte vérifiée par hypothèse non limitante (sans courbe constructeur)
  double i2t = 0, i2tAdm = 0, i2tAdmPE = 0;
};

class Calcul {
 public:
  explicit Calcul(const ReseauBT& r) : m_r(r) {}

  ResultatReseauBT executer();

 private:
  const ReseauBT& m_r;
  ResultatReseauBT m_x;
  ImpedancesSource m_z;
  double m_u = 400, m_u0 = 400 / RACINE3;
  std::map<std::string, std::size_t> m_indice;
  std::vector<std::vector<std::size_t>> m_enfants;
  std::vector<std::size_t> m_racines;
  // Puissances par phase (kW, kvar) de chaque départ ; part qui ne foisonne pas (bornes de recharge, 722.311)
  std::vector<std::array<double, 3>> m_pPhase, m_qPhase, m_pFixe, m_qFixe;
  std::vector<ProtectionVue> m_prot;

  bool sourcePublique() const { return m_r.source.type == TypeSourceBT::BranchementLimite || m_r.source.type == TypeSourceBT::ReseauPublic; }
  bool monoDepart(const DepartBT& d) const { return m_z.mono || polariteMono(d.polarite); }
  void avis(std::size_t i, NiveauAvis n, const std::string& regle, const std::string& message) { m_x.departs[i].avis.push_back({n, regle, message}); }
  void avisReseau(NiveauAvis n, const std::string& regle, const std::string& message) { m_x.avis.push_back({n, regle, message}); }

  void construireArbre();
  void calculerCharges(std::size_t i, std::set<std::size_t>& pile);
  void choisirProtection(std::size_t i);
  void choisirProtections(std::size_t i);
  Evaluation evaluer(std::size_t i, const Amont& a, double s, int n, double spe) const;
  void dimensionner(std::size_t i, const Amont& a);
  std::string selectivite(std::size_t i, const Amont& a);
  void bilanSource();
};

void Calcul::construireArbre() {
  const auto& d = m_r.departs;
  m_x.departs.resize(d.size());
  m_enfants.assign(d.size(), {});
  for (std::size_t i = 0; i < d.size(); ++i) {
    m_x.departs[i].uuid = d[i].uuid;
    m_indice[d[i].uuid] = i;
  }
  for (std::size_t i = 0; i < d.size(); ++i) {
    const auto it = d[i].parent.empty() ? m_indice.end() : m_indice.find(d[i].parent);
    bool racine = it == m_indice.end();
    if (!racine && d[it->second].charge != TypeCharge::Tableau) {
      avis(i, NiveauAvis::Erreur, "Installation", "Le départ amont « " + d[it->second].repere + " » n'est pas un tableau : ce départ est rattaché à la source.");
      racine = true;
    }
    if (!racine) {
      // Boucle (un tableau qui s'alimente lui-même) : coupée ici
      std::set<std::size_t> vus{i};
      std::size_t k = it->second;
      while (true) {
        if (vus.count(k)) {
          racine = true;
          avis(i, NiveauAvis::Erreur, "Installation", "Ce départ s'alimente lui-même par ses tableaux : il est rattaché à la source.");
          break;
        }
        vus.insert(k);
        const auto jt = d[k].parent.empty() ? m_indice.end() : m_indice.find(d[k].parent);
        if (jt == m_indice.end()) break;
        k = jt->second;
      }
    } else if (!d[i].parent.empty() && it == m_indice.end()) {
      avis(i, NiveauAvis::Alerte, "Installation", "Son tableau amont n'existe plus : ce départ est rattaché à la source.");
    }
    if (racine) m_racines.push_back(i);
    else m_enfants[it->second].push_back(i);
  }
}

void Calcul::calculerCharges(std::size_t i, std::set<std::size_t>& pile) {
  const DepartBT& d = m_r.departs[i];
  ResultatDepartBT& x = m_x.departs[i];
  std::array<double, 3>& pp = m_pPhase[i];
  std::array<double, 3>& qq = m_qPhase[i];
  std::array<double, 3>& pf = m_pFixe[i];
  std::array<double, 3>& qf = m_qFixe[i];
  pp = qq = pf = qf = {0, 0, 0};
  if (pile.count(i)) return;
  pile.insert(i);
  const bool mono = monoDepart(d);
  auto repartir = [&](double p, double q) {
    if (mono && d.phase >= 1 && d.phase <= 3) {
      pp[static_cast<std::size_t>(d.phase - 1)] += p;
      qq[static_cast<std::size_t>(d.phase - 1)] += q;
    } else
      for (std::size_t k = 0; k < 3; ++k) {
        pp[k] += p / 3.0;
        qq[k] += q / 3.0;
      }
  };
  if (d.charge == TypeCharge::Tableau) {
    // Bornes de recharge : simultanéité 1 jusqu'à la source, sauf simultanéité donnée (gestion de la charge)
    std::array<double, 3> pDiv{}, qDiv{}, pIrve{}, qIrve{};
    for (std::size_t e : m_enfants[i]) {
      calculerCharges(e, pile);
      for (std::size_t k = 0; k < 3; ++k) {
        pDiv[k] += m_pPhase[e][k] - m_pFixe[e][k];
        qDiv[k] += m_qPhase[e][k] - m_qFixe[e][k];
        pIrve[k] += m_pFixe[e][k];
        qIrve[k] += m_qFixe[e][k];
      }
    }
    const std::size_t n = m_enfants[i].size();
    const double ks = d.ks > 0 ? d.ks : n <= 1 ? 1.0 : n <= 3 ? 0.9 : n <= 5 ? 0.8 : n <= 9 ? 0.7 : 0.6;
    const bool fixe = d.ks <= 0;
    for (std::size_t k = 0; k < 3; ++k) {
      pp[k] = ks * pDiv[k] + (fixe ? pIrve[k] : ks * pIrve[k]);
      qq[k] = ks * qDiv[k] + (fixe ? qIrve[k] : ks * qIrve[k]);
      pf[k] = fixe ? pIrve[k] : 0;
      qf[k] = fixe ? qIrve[k] : 0;
    }
    if (mono) {
      // Tableau monophasé : tout sur une phase
      const double p = pp[0] + pp[1] + pp[2], q = qq[0] + qq[1] + qq[2], p2 = pf[0] + pf[1] + pf[2], q2 = qf[0] + qf[1] + qf[2];
      pp = qq = pf = qf = {0, 0, 0};
      repartir(p, q);
      const auto pTot = pp, qTot = qq;
      pp = qq = {0, 0, 0};
      repartir(p2, q2);
      pf = pp;
      qf = qq;
      pp = pTot;
      qq = qTot;
    }
    if (n == 0) avis(i, NiveauAvis::Alerte, "Installation", "Ce tableau n'alimente encore rien : ajoute-lui des départs.");
    if (fixe && pIrve[0] + pIrve[1] + pIrve[2] > 0 && n > 1)
      avis(i, NiveauAvis::Info, "NF C 15-100 722.311",
           "Les bornes de recharge comptent sans foisonnement (simultanéité 1) ; avec une gestion de la charge, donne la simultanéité du tableau.");
  } else {
    double p = 0, q = 0;
    const double cphi = std::clamp(d.cosPhi, 0.05, 1.0), tphi = std::sqrt(1.0 - cphi * cphi) / cphi;
    if (d.charge == TypeCharge::Condensateurs) {
      q = -std::max(0.0, d.puissance);
    } else if (d.courantImpose > 0) {
      const double s = mono ? d.courantImpose * m_u0 / 1000.0 : RACINE3 * m_u * d.courantImpose / 1000.0;
      p = s * cphi * d.ku;
      q = p * tphi;
    } else {
      p = std::max(0.0, d.puissance) / std::clamp(d.rendement, 0.1, 1.0) * std::max(0.0, d.ku);
      q = p * tphi;
    }
    if (d.puissance <= 0 && d.courantImpose <= 0)
      avis(i, NiveauAvis::Alerte, "Installation", "Puissance nulle : donne la puissance du récepteur (ou son courant d'emploi).");
    repartir(p, q);
    if (d.charge == TypeCharge::IRVE) {
      pf = pp;
      qf = qq;
    }
  }
  // Courant d'emploi : phase la plus chargée
  double p = 0, q = 0, ib = 0;
  for (std::size_t k = 0; k < 3; ++k) {
    p += pp[k];
    q += qq[k];
    ib = std::max(ib, std::hypot(pp[k], qq[k]) * 1000.0 / m_u0);
  }
  x.p = p;
  x.q = q;
  x.s = std::hypot(p, q);
  x.cosPhi = x.s > 1e-9 ? std::abs(p) / x.s : 1.0;
  if (d.charge == TypeCharge::Condensateurs) {
    // Condensateurs : courant assigné × 1,36 (harmoniques et tolérance de capacité)
    ib = std::abs(q) * 1000.0 / (mono ? m_u0 : RACINE3 * m_u) * 1.36;
    x.cosPhi = 0;
  } else if (d.courantImpose > 0)
    ib = d.courantImpose;
  else if (mono)
    ib = x.s * 1000.0 / m_u0;
  else if (d.charge != TypeCharge::Tableau)
    ib = x.s * 1000.0 / (RACINE3 * m_u);
  x.ib = ib;
  // Harmonique 3 : courant du neutre (CEI 60364-5-52, annexe E)
  if (d.polarite == Polarite::TriphaseNeutre && !m_z.mono && d.tauxH3 > 15) x.iNeutre = 3.0 * d.tauxH3 / 100.0 * ib;
  pile.erase(i);
}

void Calcul::choisirProtections(std::size_t i) {
  for (std::size_t e : m_enfants[i]) choisirProtections(e);
  choisirProtection(i);
}

void Calcul::choisirProtection(std::size_t i) {
  const DepartBT& d = m_r.departs[i];
  ResultatDepartBT& x = m_x.departs[i];
  double ib = std::max(x.ib, x.iNeutre > x.ib && d.tauxH3 > 33 ? x.iNeutre : 0.0);
  // Calibre plancher : 10 A pour un circuit terminal courant, 16 A pour un tableau ; sélectivité recherchée : la
  // protection d'un tableau vaut au moins 1,6 fois le plus fort réglage de ses départs
  if (d.calibreImpose <= 0) {
    double plancher = d.charge == TypeCharge::Tableau ? 16 : d.charge == TypeCharge::Moteur || d.charge == TypeCharge::Condensateurs ? 0 : 10;
    if (d.charge == TypeCharge::Tableau && m_r.selectivite)
      for (std::size_t e : m_enfants[i]) plancher = std::max(plancher, 1.6 * m_x.departs[e].ir);
    ib = std::max(ib, plancher);
  }
  TypeProtection t = d.protection;
  if (t == TypeProtection::Auto) {
    const double base = d.calibreImpose > 0 ? d.calibreImpose : ib;
    if (d.charge == TypeCharge::Moteur && base <= 65) t = TypeProtection::Moteur;
    else t = base <= 63 ? TypeProtection::Modulaire : TypeProtection::Reglable;
  }
  x.protection = t;
  ProtectionVue& v = m_prot[i];
  v.type = t;
  v.repere = d.repere;
  v.differentiel = d.differentiel;
  v.selectifDiff = d.differentielSelectif;
  switch (t) {
    case TypeProtection::Modulaire: {
      x.in = d.calibreImpose > 0 ? d.calibreImpose : superieur(CALIBRES_MODULAIRES, ib);
      if (x.in <= 0) {
        x.in = 125;
        avis(i, NiveauAvis::Erreur, "NF C 15-100 433.1", "Au-delà de 125 A, un disjoncteur modulaire ne convient plus : choisis un disjoncteur réglable.");
      }
      x.ir = x.in;
      const std::string c = d.courbe == "B" || d.courbe == "D" ? d.courbe : "C";
      const double bas = c == "B" ? 3 : c == "D" ? 10 : 5, haut = c == "B" ? 5 : c == "D" ? 20 : 10;
      x.imBas = bas * x.in;
      x.imHaut = haut * x.in;
      x.ia = x.imHaut;
      break;
    }
    case TypeProtection::Reglable: {
      x.in = d.calibreImpose > 0 ? d.calibreImpose : superieur(CALIBRES_REGLABLES, ib);
      if (x.in <= 0) {
        x.in = CALIBRES_REGLABLES.back();
        avis(i, NiveauAvis::Erreur, "Installation", "Courant d'emploi au-delà de 6 300 A : partage ce départ.");
      }
      x.ir = d.reglageIr > 0 ? std::min(d.reglageIr, x.in) : std::clamp(std::ceil(ib), 0.4 * x.in, x.in);
      const double isd = std::clamp(d.isd > 0 ? d.isd : 10.0, 1.5, 15.0) * x.ir;
      // Tolérance de ±20 % sur le seuil : Ia = 1,2 Isd (UTE C 15-105)
      x.imBas = 0.8 * isd;
      x.imHaut = 1.2 * isd;
      x.ia = x.imHaut;
      v.retarde = d.retarde;
      break;
    }
    case TypeProtection::Moteur: {
      const Plage* pl = nullptr;
      const double reglage = d.reglageIr > 0 ? d.reglageIr : ib;
      for (const auto& p : PLAGES_MOTEUR)
        if (reglage <= p.max + 1e-9) {
          pl = &p;
          break;
        }
      if (!pl) {
        pl = &PLAGES_MOTEUR.back();
        avis(i, NiveauAvis::Erreur, "Installation", "Au-delà de 65 A, un disjoncteur moteur ne convient plus : choisis un disjoncteur réglable.");
      }
      x.in = d.calibreImpose > 0 ? d.calibreImpose : pl->max;
      x.ir = std::clamp(reglage, pl->min, pl->max);
      // Déclencheur magnétique fixe à 13 fois le haut de la plage
      x.imBas = 0.8 * 13 * pl->max;
      x.imHaut = 1.2 * 13 * pl->max;
      x.ia = x.imHaut;
      break;
    }
    case TypeProtection::Fusible: {
      x.in = d.calibreImpose > 0 ? d.calibreImpose : superieur({16, 20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630}, ib);
      if (x.in <= 0) {
        x.in = 630;
        avis(i, NiveauAvis::Erreur, "Installation", "Au-delà de 630 A, choisis un disjoncteur réglable.");
      }
      x.ir = x.in;
      if (const PorteFusible* g = porteGG(x.in)) {
        // Circuit terminal de 32 A au plus : coupure en 0,4 s, vérifiée avec la porte de 0,1 s (prudent) ; sinon 5 s
        const bool terminal = d.charge != TypeCharge::Tableau && x.in <= 32;
        x.ia = terminal ? g->imax01 : g->imax5;
        x.imBas = g->imin01;
        x.imHaut = g->imax5;
      } else {
        x.ia = 0;
        avis(i, NiveauAvis::Info, "UTE C 15-105", "Calibre de fusible hors de la gamme gG 16–630 A : courants de fusion non vérifiés.");
      }
      break;
    }
    case TypeProtection::Auto: break;
  }
  v.in = x.in;
  v.ir = x.ir;
  v.imBas = x.imBas;
  v.imHaut = x.imHaut;
  if (x.ir + 1e-9 < x.ib)
    avis(i, NiveauAvis::Erreur, "NF C 15-100 433.1",
         "Le calibre (ou réglage) " + amperes(x.ir) + " est inférieur au courant d'emploi " + amperes(x.ib) + " : la protection déclencherait en service normal.");
}

Evaluation Calcul::evaluer(std::size_t i, const Amont& a, double s, int n, double spe) const {
  const DepartBT& d = m_r.departs[i];
  const ResultatDepartBT& x = m_x.departs[i];
  Evaluation e;
  e.s = s;
  e.n = n;
  const bool mono = monoDepart(d), neutre = mono || (avecNeutre(d.polarite) && !m_z.mono);
  // Neutre : plein, sauf demi-section permise au-delà de 16 mm² (25 en aluminium) sans harmoniques (524.2)
  e.sn = 0;
  if (neutre) {
    e.sn = s;
    const double seuil = d.ame == Ame::Cuivre ? 16 : 25;
    if (!mono && d.neutreReduit && d.tauxH3 <= 15 && s > seuil + 1e-9) e.sn = std::max(seuil, sectionSuperieure(s / 2.0));
  }
  e.spe = spe > 0 ? spe : sectionProtection(s);
  // Courant admissible : table des conducteurs en cuivre, corrigée (température, groupement, câbles en parallèle,
  // harmoniques, aluminium)
  const int charges = mono ? 2 : 3;
  const int groupes = std::max(1, d.circuitsJointifs) + (n - 1);
  double fh = 1.0, idim = x.ir;
  if (x.protection == TypeProtection::Fusible) idim = x.in * (x.in < 10 ? 1.31 : x.in <= 25 ? 1.21 : 1.10);
  if (neutre && !mono && d.tauxH3 > 15) {
    fh = d.tauxH3 > 45 ? 1.0 : 0.86;  // tableau E.52.1 : quatre conducteurs chargés
    if (d.tauxH3 > 33) idim = std::max(idim, x.iNeutre);
  }
  const double kAme = d.ame == Ame::Cuivre ? 1.0 : 0.78;
  const double base = courantAdmissible(s, d.pose, d.isolant, charges, d.pose == MethodePose::D1 ? 20 : 30, 1);
  const double izCorrige = courantAdmissible(s, d.pose, d.isolant, charges, d.temperature, groupes);
  e.k = base > 0 ? izCorrige / base * fh * kAme : 0;
  e.iz = izCorrige * fh * kAme * n;
  e.okIz = e.iz + 1e-6 >= idim && e.iz > 0;
  // Impédances de cette canalisation à ρ0
  e.phase0 = conducteur(d.longueur, s, n, d.ame, d.unipolaire);
  e.neutre0 = neutre ? conducteur(d.longueur, e.sn, n, d.ame, d.unipolaire) : Z{};
  e.pe0 = conducteur(d.longueur, e.spe, n, d.ame, d.unipolaire);
  // Chute de tension : b (ρ L / S cos φ + λ L sin φ) Ib, rapportée à la tension simple
  const double b = mono ? 2.0 : 1.0;
  e.rService = rhoService(d.ame) * d.longueur / (s * n);
  e.x = lambdaCable(d.unipolaire) / 1000.0 * d.longueur / n;
  const double cphi = std::clamp(x.cosPhi, 0.0, 1.0), sphi = std::sqrt(1.0 - cphi * cphi);
  e.chute = 100.0 * b * (e.rService * cphi + e.x * sphi) * x.ib / m_u0;
  e.chuteCumulee = a.chute + e.chute;
  e.okChute = e.chuteCumulee <= x.chuteLimite + 1e-9;
  // Courts-circuits au bout de la canalisation
  const double fRho = x.protection == TypeProtection::Fusible ? 1.5 : 1.25;
  const double u = M_CHARGE * m_u0;
  const Z zMax = m_z.max + a.phase0 + e.phase0;
  e.ik3max = zMax.module() > 0 ? C_MAX * u / zMax.module() : 0;
  const Z boucleNMax = m_z.boucleMax + (a.phase0 + a.neutre0 + e.phase0 + e.neutre0);
  e.ik1max = neutre && boucleNMax.module() > 0 ? C_MAX * u / boucleNMax.module() : 0;
  if (mono) e.ik3max = e.ik1max;  // circuit monophasé : le court-circuit maximal est entre phase et neutre
  const Z boucleNMin = m_z.boucleMin + (a.phase0 + a.neutre0 + e.phase0 + e.neutre0).resistanceFois(fRho);
  const double ik1min = neutre && boucleNMin.module() > 0 ? C_MIN * u / boucleNMin.module() : 0;
  const Z zMin = m_z.min + (a.phase0 + e.phase0).resistanceFois(fRho);
  const double ik2min = zMin.module() > 0 ? RACINE3 / 2.0 * C_MIN * u / zMin.module() : 0;
  e.ikMin = neutre ? ik1min : ik2min;
  const Z boucleT = m_z.boucleMin + (a.phase0 + a.pe0 + e.phase0 + e.pe0).resistanceFois(fRho);
  e.ief = boucleT.module() > 0 ? C_MIN * u / boucleT.module() : 0;
  // IT, double défaut : boucle des seules canalisations, comptée deux fois ; tension composée sans neutre
  const Z boucleIT = (a.phase0 + a.pe0 + e.phase0 + e.pe0).resistanceFois(fRho) * 2.0;
  const double uIT = neutre ? u : u * RACINE3;
  e.iefIT = boucleIT.module() > 0 ? C_MIN * uIT / boucleIT.module() : 0;
  // Protection contre les courts-circuits : le minimal doit faire déclencher l'instantané (fusible : en 5 s)
  e.okCourt = x.imHaut <= 0 || e.ikMin * 1000.0 + 1e-6 >= x.imHaut;
  // Personnes (contacts indirects)
  const double diff = d.differentiel > 0 ? d.differentiel : a.differentiel;
  switch (m_r.source.regime) {
    case RegimeNeutre::TT: e.okPersonnes = diff > 0 && m_r.source.resistanceTerre * diff / 1000.0 <= 50 + 1e-9; break;
    case RegimeNeutre::TNS:
    case RegimeNeutre::TNC: e.okPersonnes = diff > 0 || (x.ia > 0 && e.ief * 1000.0 + 1e-6 >= x.ia); break;
    case RegimeNeutre::IT: e.okPersonnes = diff > 0 || (x.ia > 0 && e.iefIT * 1000.0 + 1e-6 >= x.ia); break;
  }
  // Contraintes thermiques : énergie laissée passer au point de pose de la protection
  double ikOrigine = (mono ? (a.ik1Bus > 0 ? a.ik1Bus : a.ik3Bus) : a.ik3Bus) * 1000.0;
  // Installation domestique derrière un branchement à puissance limitée : 3 kA, comme pour le pouvoir de coupure
  if (m_r.source.type == TypeSourceBT::BranchementLimite && m_r.source.domestique) ikOrigine = std::min(ikOrigine, 3000.0);
  const double kph = coefficientK(d.ame, d.isolant);
  e.i2tAdm = kph * kph * (s * n) * (s * n);
  e.i2tAdmPE = kph * kph * (e.spe * n) * (e.spe * n);
  e.okThermique = e.okThermiquePE = true;
  if (x.protection == TypeProtection::Fusible) {
    e.i2t = 0;  // fusible limiteur : vérifié par sa coupure en moins de 5 s
  } else {
    double i2t = x.protection == TypeProtection::Modulaire ? i2tClasse3(x.in, d.courbe, ikOrigine) : -1;
    if (i2t < 0 && (x.protection == TypeProtection::Moteur || (x.protection == TypeProtection::Modulaire && x.in <= 125 + 1e-9 && d.courbe != "D"))) {
      // Disjoncteur moteur, ou modulaire au-delà de 32 A : limiteurs, estimés d'après la classe 3 de 32 A
      // (proportionnelle au calibre) ; à confirmer par la courbe du constructeur
      i2t = i2tClasse3(32, "C", ikOrigine) * std::max(1.0, x.protection == TypeProtection::Moteur ? 1.0 : x.in / 32.0);
      e.thermiqueEstimee = true;
    }
    if (i2t < 0) {
      // Sans courbe du constructeur : coupure en une demi-période (10 ms), ou au bout du court retard
      const double t = x.protection == TypeProtection::Reglable && d.retarde ? 0.12 : 0.01;
      i2t = ikOrigine * ikOrigine * t;
      e.thermiqueEstimee = true;
    }
    e.i2t = i2t;
    e.okThermique = e.i2tAdm + 1e-6 >= i2t;
    e.okThermiquePE = e.i2tAdmPE + 1e-6 >= i2t;
  }
  return e;
}

void Calcul::dimensionner(std::size_t i, const Amont& a) {
  const DepartBT& d = m_r.departs[i];
  ResultatDepartBT& x = m_x.departs[i];
  x.profondeur = a.profondeur;
  // Chute de tension admissible : 3 % (éclairage) et 5 % sur le réseau public, 6 et 8 % depuis un poste privé
  const bool eclairage = d.charge == TypeCharge::Eclairage;
  const double limEcl = m_r.chuteEclairage > 0 ? m_r.chuteEclairage : sourcePublique() ? 3.0 : 6.0;
  const double limAut = m_r.chuteAutres > 0 ? m_r.chuteAutres : sourcePublique() ? 5.0 : 8.0;
  x.chuteLimite = eclairage ? limEcl : limAut;
  // Pouvoir de coupure au point de pose (jeu de barres amont)
  const bool mono = monoDepart(d);
  x.pdcRequis = mono && a.ik1Bus > 0 ? a.ik1Bus : a.ik3Bus;
  const bool domestique = m_r.source.type == TypeSourceBT::BranchementLimite && m_r.source.domestique;
  const double exige = domestique ? std::min(x.pdcRequis, 3.0) : x.pdcRequis;
  if (d.pdcImpose > 0) x.pdc = d.pdcImpose;
  else if (x.protection == TypeProtection::Fusible) x.pdc = 100;
  else {
    std::vector<double> gamme = x.protection == TypeProtection::Reglable ? PDC_REGLABLES : x.protection == TypeProtection::Moteur ? PDC_MOTEUR : PDC_MODULAIRES;
    if (domestique && x.protection == TypeProtection::Modulaire) gamme.insert(gamme.begin(), 3.0);
    x.pdc = superieur(gamme, exige);
    if (x.pdc <= 0) x.pdc = gamme.back();
  }
  if (x.pdc + 1e-9 < exige)
    avis(i, NiveauAvis::Erreur, "NF C 15-100 434.5.1",
         "Pouvoir de coupure " + kA(x.pdc) + " inférieur au courant de court-circuit maximal au point de pose (" + kA(exige) +
             ") : prends un appareil plus performant, ou justifie une association (filiation) avec la protection amont d'après les tables du constructeur.");
  else if (domestique && x.pdcRequis > 3.0 + 1e-9 && a.profondeur == 0)
    avis(i, NiveauAvis::Info, "NF C 15-100",
         "Installation domestique derrière un branchement à puissance limitée : un pouvoir de coupure de 3 kA suffit pour tous ses disjoncteurs.");

  // ----- Choix de la canalisation -----
  const double smin = std::max({d.ame == Ame::Cuivre ? 1.5 : 16.0, d.sectionMini, 0.0});
  Evaluation e;
  bool trouve = false;
  auto essayer = [&](double s, int n) -> bool {
    Evaluation t = evaluer(i, a, s, n, 0);
    if (!t.okIz || !t.okChute || !t.okCourt) return false;
    if (!t.okPersonnes) {
      const Evaluation u = evaluer(i, a, s, n, s);  // conducteur de protection de même section
      if (!u.okPersonnes) return false;
      t = u;
    }
    if (!t.okThermiquePE && t.okThermique) {
      const Evaluation u = evaluer(i, a, s, n, s);
      if (u.okThermiquePE) t = u;
    }
    // Contrainte thermique, même estimée (hypothèse prudente) : la section augmente plutôt que de laisser un doute
    if (!t.okThermique || !t.okThermiquePE) return false;
    e = t;
    return true;
  };
  if (d.sectionImposee > 0) {
    const int n = std::max(1, d.parallele);
    e = evaluer(i, a, d.sectionImposee, n, 0);
    if (!e.okPersonnes) {
      const Evaluation u = evaluer(i, a, d.sectionImposee, n, d.sectionImposee);
      if (u.okPersonnes) e = u;
    }
    trouve = true;
  } else {
    std::vector<int> nombres;
    if (d.parallele > 0) nombres = {d.parallele};
    else nombres = {1, 2, 3, 4, 5, 6};
    for (int n : nombres) {
      for (double s : SECTIONS) {
        if (s + 1e-9 < smin) continue;
        if (n > 1 && s < 70) continue;  // conducteurs en parallèle : grosses sections seulement
        if (essayer(s, n)) {
          trouve = true;
          break;
        }
      }
      if (trouve) break;
    }
    if (!trouve) {
      // Rien ne satisfait toutes les règles : la plus petite section qui porte le courant (et la chute si possible),
      // les règles non satisfaites deviennent des avis
      bool pris = false;
      for (int passe = 0; passe < 2 && !pris; ++passe)
        for (int n : nombres) {
          for (double s : SECTIONS) {
            if (s + 1e-9 < smin || (n > 1 && s < 70)) continue;
            const Evaluation t = evaluer(i, a, s, n, 0);
            if (t.okIz && (passe == 1 || t.okChute)) {
              e = t;
              pris = true;
              break;
            }
          }
          if (pris) break;
        }
      if (!pris) e = evaluer(i, a, SECTIONS.back(), nombres.back(), 0);
    }
  }
  x.section = e.s;
  x.sectionNeutre = e.sn;
  x.sectionPE = e.spe;
  x.parallele = e.n;
  x.k = e.k;
  x.iz = e.iz;
  x.courantDimensionnement = x.ir;
  if (x.protection == TypeProtection::Fusible) x.courantDimensionnement = x.in * (x.in < 10 ? 1.31 : x.in <= 25 ? 1.21 : 1.10);
  if (d.tauxH3 > 33 && x.iNeutre > x.courantDimensionnement) x.courantDimensionnement = x.iNeutre;
  x.chute = e.chute;
  x.chuteCumulee = e.chuteCumulee;
  x.ik3max = e.ik3max;
  x.ik1max = e.ik1max;
  x.ikMin = e.ikMin;
  x.ief = m_r.source.regime == RegimeNeutre::IT ? e.iefIT : e.ief;
  x.i2tLaisse = e.i2t;
  x.i2tAdmissible = e.i2tAdm;

  // ----- Avis -----
  if (!e.okIz)
    avis(i, NiveauAvis::Erreur, "NF C 15-100 433.1",
         "Courant admissible Iz " + amperes(e.iz) + " inférieur au courant de la protection " + amperes(x.courantDimensionnement) +
             " : la canalisation n'est pas protégée contre les surcharges. Augmente la section.");
  if (!e.okChute) {
    const NiveauAvis n = sourcePublique() ? NiveauAvis::Erreur : NiveauAvis::Alerte;
    avis(i, n, "NF C 15-100 525",
         "Chute de tension cumulée " + pourcent(e.chuteCumulee) + " au-delà de " + pourcent(x.chuteLimite) +
             " : augmente la section, raccourcis la ligne ou rapproche le tableau.");
  }
  if (!e.okCourt) {
    if (x.protection == TypeProtection::Fusible)
      avis(i, NiveauAvis::Erreur, "NF C 15-100 434.2",
           "Court-circuit minimal en bout de ligne " + kA(x.ikMin) + " : le fusible ne fond pas en moins de 5 s (" + amperes(x.imHaut) +
               "). Augmente la section ou réduis le calibre.");
    else
      avis(i, NiveauAvis::Erreur, "NF C 15-100 434.2",
           "Court-circuit minimal en bout de ligne " + kA(x.ikMin) + " inférieur au déclenchement instantané " + amperes(x.imHaut) +
               " : la ligne n'est pas protégée jusqu'au bout. Augmente la section, raccourcis la ligne ou prends une courbe plus basse (B).");
  }
  if (d.ame == Ame::Aluminium && x.section < 16 - 1e-9)
    avis(i, NiveauAvis::Erreur, "NF C 15-100 524.1", "Une canalisation en aluminium a au moins 16 mm².");
  if (e.okThermique && e.okThermiquePE) {
    // contrainte vérifiée (fusible : par sa coupure en moins de 5 s)
  } else if (e.thermiqueEstimee)
    avis(i, NiveauAvis::Alerte, "NF C 15-100 434.5.2",
         "Contrainte thermique non démontrée sans la courbe I²t du constructeur (hypothèse d'une coupure en " +
             std::string(x.protection == TypeProtection::Reglable && d.retarde ? "120 ms" : "10 ms") + " : " + nombreFr(e.i2t / 1e6, 2) +
             " MA²s pour " + nombreFr((e.okThermique ? e.i2tAdmPE : e.i2tAdm) / 1e6, 2) + " MA²s admissibles). Vérifie la limitation du disjoncteur.");
  else
    avis(i, NiveauAvis::Erreur, "NF C 15-100 434.5.2",
         "Contrainte thermique : l'énergie laissée passer (" + nombreFr(e.i2t / 1e6, 3) + " MA²s) dépasse ce que supporte " +
             (e.okThermique ? std::string("le conducteur de protection") : std::string("la canalisation")) + " (k²S² = " +
             nombreFr((e.okThermique ? e.i2tAdmPE : e.i2tAdm) / 1e6, 3) + " MA²s). Augmente la section.");
  if (d.charge == TypeCharge::Tableau && !m_enfants[i].empty() && e.okChute && e.chuteCumulee > 0.5 * x.chuteLimite)
    avis(i, NiveauAvis::Info, "NF C 15-100 525",
         "La ligne de ce tableau consomme déjà " + pourcent(e.chuteCumulee) + " sur " + pourcent(x.chuteLimite) +
             " : une section plus forte (section imposée) laisserait de la marge à ses départs.");
  if (m_r.source.regime == RegimeNeutre::TNC && x.section < (d.ame == Ame::Cuivre ? 10 : 16) - 1e-9)
    avis(i, NiveauAvis::Info, "NF C 15-100 543.4",
         "En TN-C, le PEN demande 10 mm² en cuivre (16 en aluminium) : ce circuit est en TN-S, neutre et conducteur de protection séparés.");
  if (d.tauxH3 > 15 && !mono && avecNeutre(d.polarite))
    avis(i, NiveauAvis::Info, "CEI 60364-5-52 annexe E",
         "Harmonique 3 à " + nombreFr(d.tauxH3) + " % : neutre chargé (" + amperes(x.iNeutre) + "), " +
             (d.tauxH3 > 33 ? std::string("section choisie sur le courant du neutre") : std::string("courant admissible réduit de 0,86")) + ".");
  if (d.neutreReduit && d.tauxH3 > 15)
    avis(i, NiveauAvis::Erreur, "NF C 15-100 524.2", "Pas de neutre réduit avec plus de 15 % d'harmonique 3 : le neutre garde la section des phases.");

  // ----- Protection des personnes -----
  const double diff = d.differentiel > 0 ? d.differentiel : a.differentiel;
  const double ra = m_r.source.resistanceTerre;
  // Longueur maximale par la méthode conventionnelle (information, hors TT)
  auto longueurMaxi = [&](double ia) {
    if (ia <= 0 || x.section <= 0 || x.sectionPE <= 0) return 0.0;
    double corr = 1.0;
    if (x.section >= 300 - 1e-9) corr = 1.30;
    else if (x.section >= 240 - 1e-9) corr = 1.25;
    else if (x.section >= 185 - 1e-9) corr = 1.20;
    else if (x.section >= 150 - 1e-9) corr = 1.15;
    const double rho1 = (x.protection == TypeProtection::Fusible ? 1.5 : 1.25) * rho0(d.ame) * corr;
    const double m = x.section / x.sectionPE;
    const double sph = x.section * x.parallele;
    double l = 0.8 * m_u0 * sph / (rho1 * (1 + m) * ia);
    if (m_r.source.regime == RegimeNeutre::IT) l *= (avecNeutre(d.polarite) || mono) ? 0.5 : RACINE3 / 2.0;
    return l;
  };
  x.personnesProtegees = e.okPersonnes;
  switch (m_r.source.regime) {
    case RegimeNeutre::TT:
      if (diff <= 0) {
        x.protectionPersonnes = "aucune : pas de différentiel";
        avis(i, NiveauAvis::Erreur, "NF C 15-100 411.5",
             "En schéma TT, chaque circuit est protégé par un dispositif différentiel : donne-lui un différentiel (ou mets-en un en tête de son tableau).");
      } else {
        const double ul = ra * diff / 1000.0;
        x.protectionPersonnes = "différentiel " + nombreFr(diff) + " mA (RA × IΔn = " + nombreFr(ul, 1) + " V)";
        if (ul > 50 + 1e-9)
          avis(i, NiveauAvis::Erreur, "NF C 15-100 411.5.3",
               "RA × IΔn = " + nombreFr(ul, 1) + " V dépasse 50 V : prends un différentiel plus sensible ou améliore la prise de terre (" + nombreFr(ra) + " Ω).");
      }
      break;
    case RegimeNeutre::TNS:
    case RegimeNeutre::TNC: {
      x.longueurMax = longueurMaxi(x.ia);
      if (x.ia > 0 && e.ief * 1000.0 + 1e-6 >= x.ia)
        x.protectionPersonnes = "coupure automatique (Ief " + kA(x.ief) + " ≥ Ia " + kA(x.ia / 1000.0) + ")";
      else if (diff > 0)
        x.protectionPersonnes = "différentiel " + nombreFr(diff) + " mA (Ief " + kA(x.ief) + " < Ia " + kA(x.ia / 1000.0) + ")";
      else {
        x.protectionPersonnes = "non assurée";
        avis(i, NiveauAvis::Erreur, "NF C 15-100 411.4",
             "Contacts indirects : le défaut en bout de ligne (" + kA(x.ief) + ") ne fait pas déclencher la protection (Ia " + kA(x.ia / 1000.0) +
                 ") ; longueur maximale " + nombreFr(x.longueurMax, 0) + " m. Raccourcis la ligne, augmente la section du conducteur de protection ou ajoute un différentiel.");
      }
      break;
    }
    case RegimeNeutre::IT: {
      x.longueurMax = longueurMaxi(x.ia);
      if (x.ia > 0 && e.iefIT * 1000.0 + 1e-6 >= x.ia)
        x.protectionPersonnes = "coupure au double défaut (Ief " + kA(x.ief) + " ≥ Ia " + kA(x.ia / 1000.0) + ")";
      else if (diff > 0)
        x.protectionPersonnes = "différentiel " + nombreFr(diff) + " mA au double défaut";
      else {
        x.protectionPersonnes = "non assurée au double défaut";
        avis(i, NiveauAvis::Erreur, "NF C 15-100 411.6.4",
             "Double défaut : le courant (" + kA(x.ief) + ") ne fait pas déclencher la protection (Ia " + kA(x.ia / 1000.0) + ") ; longueur maximale " +
                 nombreFr(x.longueurMax, 0) + " m.");
      }
      break;
    }
  }
  // Prises et bornes de recharge : différentiel 30 mA au plus
  if ((d.charge == TypeCharge::Prises || d.charge == TypeCharge::IRVE) && (diff <= 0 || diff > 30 + 1e-9))
    avis(i, NiveauAvis::Erreur, d.charge == TypeCharge::IRVE ? "NF C 15-100 722.531" : "NF C 15-100 411.3.3",
         "Un circuit de " + std::string(d.charge == TypeCharge::IRVE ? "borne de recharge" : "prises de courant") + " est protégé par un différentiel de 30 mA au plus.");
  if (d.charge == TypeCharge::IRVE && d.differentiel > 0 && d.typeDifferentiel == "AC")
    avis(i, NiveauAvis::Erreur, "NF C 15-100 722.531.3",
         "Borne de recharge : différentiel de type A (si la borne détecte les défauts continus de 6 mA) ou de type B, jamais AC.");
  if (d.charge == TypeCharge::Moteur && x.protection == TypeProtection::Modulaire && d.courbe == "B" && d.demarrage >= 4)
    avis(i, NiveauAvis::Alerte, "UTE C 15-105", "Un moteur à démarrage direct fait déclencher une courbe B : prends une courbe D (ou C).");

  // ----- Démarrage des moteurs -----
  if (d.charge == TypeCharge::Moteur && d.demarrage > 1) {
    const double id = d.demarrage * x.ib, sd = std::sqrt(1.0 - COS_DEMARRAGE * COS_DEMARRAGE);
    const double b = mono ? 2.0 : 1.0;
    const double propre = 100.0 * b * (e.rService * COS_DEMARRAGE + e.x * sd) * id / m_u0;
    const double amont = 100.0 * (a.rChute * COS_DEMARRAGE + a.xChute * sd) * (id - x.ib) / m_u0;
    x.chuteDemarrage = a.chute + amont + propre;
    if (x.chuteDemarrage > 15 + 1e-9)
      avis(i, NiveauAvis::Alerte, "UTE C 15-105",
           "Au démarrage (" + nombreFr(d.demarrage) + " × In), la chute de tension atteint " + pourcent(x.chuteDemarrage) +
               " : au-delà de 15 % le moteur peine à démarrer. Démarreur progressif, variateur, ou section plus forte.");
  }

  // ----- Désignations -----
  {
    const std::string poles = mono ? "1P+N" : avecNeutre(d.polarite) && m_r.source.regime != RegimeNeutre::TNC ? "4P" : "3P";
    std::string t;
    switch (x.protection) {
      case TypeProtection::Modulaire:
        t = "Disjoncteur " + poles + " courbe " + (d.courbe == "B" || d.courbe == "D" ? d.courbe : std::string("C")) + " " + nombreFr(x.in) + " A " + kA(x.pdc);
        break;
      case TypeProtection::Reglable:
        t = "Disjoncteur " + poles + " " + nombreFr(x.in) + " A, Ir " + nombreFr(x.ir, 0) + " A, " + (d.retarde ? "court retard " : "Isd ") +
            nombreFr(std::clamp(d.isd > 0 ? d.isd : 10.0, 1.5, 15.0)) + " Ir, " + kA(x.pdc);
        break;
      case TypeProtection::Moteur: t = "Disjoncteur moteur " + nombreFr(x.in) + " A réglé à " + nombreFr(x.ir, 2) + " A, " + kA(x.pdc); break;
      case TypeProtection::Fusible: t = "Fusibles gG " + nombreFr(x.in) + " A (" + poles + ")"; break;
      case TypeProtection::Auto: break;
    }
    if (d.differentiel > 0)
      t += " + différentiel " + nombreFr(d.differentiel) + " mA type " + d.typeDifferentiel + (d.differentielSelectif ? " S" : "");
    x.designationProtection = t;
    const std::string matiere = std::string(d.ame == Ame::Cuivre ? "Cu" : "Al") + (d.isolant == Isolant::PR ? " PR" : " PVC");
    const int phases = mono ? 1 : 3;
    std::string c;
    if (!d.unipolaire && (x.sectionNeutre <= 0 || x.sectionNeutre == x.section) && x.sectionPE == x.section) {
      const int conducteurs = phases + (x.sectionNeutre > 0 ? 1 : 0) + 1;
      c = std::to_string(conducteurs) + "G" + nombreFr(x.section);
    } else {
      c = std::to_string(phases) + "×" + nombreFr(x.section);
      if (x.sectionNeutre > 0) c += " + N " + nombreFr(x.sectionNeutre);
      c += " + PE " + nombreFr(x.sectionPE);
    }
    if (x.parallele > 1) c = std::to_string(x.parallele) + " × (" + c + ")";
    x.designationCable = c + " " + matiere + (d.unipolaire ? " unipolaires" : "");
  }

  // ----- Sélectivité avec l'amont -----
  x.selectivite = selectivite(i, a);

  // ----- Départs alimentés -----
  if (d.charge == TypeCharge::Tableau) {
    Amont b = a;
    b.phase0 = a.phase0 + e.phase0;
    b.neutre0 = a.neutre0 + e.neutre0;
    b.pe0 = a.pe0 + e.pe0;
    b.chute = e.chuteCumulee;
    const double bb = mono ? 2.0 : 1.0;
    b.rChute = a.rChute + bb * e.rService;
    b.xChute = a.xChute + bb * e.x;
    b.ik3Bus = x.ik3max;
    b.ik1Bus = x.ik1max;
    b.protection = static_cast<int>(i);
    b.differentiel = diff;
    b.profondeur = a.profondeur + 1;
    b.mono = mono;
    for (std::size_t k : m_enfants[i]) dimensionner(k, b);
  }
}

std::string Calcul::selectivite(std::size_t i, const Amont& a) {
  if (a.protection < 0) return "—";
  const ProtectionVue& am = m_prot[static_cast<std::size_t>(a.protection)];
  const ProtectionVue& av = m_prot[i];
  const ResultatDepartBT& x = m_x.departs[i];
  std::string r;
  // Surcharge : rapport des réglages au moins 1,6
  const double rapport = av.ir > 0 ? am.ir / av.ir : 0;
  if (rapport + 1e-9 < 1.6) {
    avis(i, NiveauAvis::Alerte, "Sélectivité",
         "Pas de sélectivité en surcharge avec " + am.repere + " (rapport des réglages " + nombreFr(rapport, 2) + " < 1,6) : un défaut ici peut couper tout le tableau.");
    r = "aucune en surcharge";
  } else if (am.type == TypeProtection::Fusible && av.type == TypeProtection::Fusible) {
    r = "totale (fusibles gG, rapport " + nombreFr(rapport, 2) + ")";
  } else if (am.retarde) {
    r = "chronométrique (court retard de " + am.repere + ")";
  } else {
    const double ik = a.ik3Bus * 1000.0;
    if (am.imBas >= ik - 1e-6) r = "totale";
    else if (am.imBas > av.imHaut + 1e-6) {
      // Partielle : la limite figure au tableau des résultats ; au-delà, tables de sélectivité du constructeur
      r = "partielle jusqu'à " + kA(am.imBas / 1000.0);
    } else {
      r = "aucune en court-circuit";
      avis(i, NiveauAvis::Alerte, "Sélectivité",
           "Les seuils instantanés de " + am.repere + " (" + amperes(am.imBas) + ") et de ce départ (" + amperes(av.imHaut) +
               ") se chevauchent : un court-circuit ici peut couper le tableau amont. Court retard en amont ou tables du constructeur.");
    }
  }
  (void)x;
  // Différentiels en cascade : amont retardé et au moins deux fois moins sensible
  double diffAmont = 0;
  bool selectifAmont = false;
  for (int k = a.protection; k >= 0;) {
    const ProtectionVue& p = m_prot[static_cast<std::size_t>(k)];
    if (p.differentiel > 0) {
      diffAmont = p.differentiel;
      selectifAmont = p.selectifDiff;
      break;
    }
    const auto it = m_indice.find(m_r.departs[static_cast<std::size_t>(k)].parent);
    k = it == m_indice.end() ? -1 : static_cast<int>(it->second);
  }
  if (av.differentiel > 0 && diffAmont > 0) {
    if (!selectifAmont || diffAmont + 1e-9 < 2 * av.differentiel)
      avis(i, NiveauAvis::Alerte, "NF C 15-100 535.4",
           "Différentiels en cascade : celui d'amont (" + nombreFr(diffAmont) + " mA) doit être sélectif (type S ou retardé) et au moins deux fois moins sensible.");
  }
  return r;
}

void Calcul::bilanSource() {
  const SourceBT& s = m_r.source;
  double p = 0, q = 0, ib = 0;
  std::array<double, 3> pp{}, qq{};
  for (std::size_t r : m_racines)
    for (std::size_t k = 0; k < 3; ++k) {
      pp[k] += m_pPhase[r][k];
      qq[k] += m_qPhase[r][k];
    }
  for (std::size_t k = 0; k < 3; ++k) {
    p += pp[k];
    q += qq[k];
    ib = std::max(ib, std::hypot(pp[k], qq[k]) * 1000.0 / m_u0);
  }
  m_x.p = p;
  m_x.q = q;
  m_x.s = std::hypot(p, q);
  m_x.cosPhi = m_x.s > 1e-9 ? p / m_x.s : 1.0;
  m_x.ib = m_z.mono ? m_x.s * 1000.0 / m_u0 : ib;
  m_x.inSource = m_z.in;
  m_x.tauxCharge = s.puissance > 0 ? 100.0 * m_x.s / s.puissance : 0;
  const Z zs = m_z.max;
  m_x.rSource = zs.r;
  m_x.xSource = zs.x;
  m_x.ik3Source = zs.module() > 0 ? C_MAX * M_CHARGE * m_u0 / zs.module() : 0;
  if (m_z.mono) {
    const Z zb = m_z.boucleMax;
    m_x.ik3Source = zb.module() > 0 ? C_MAX * M_CHARGE * m_u0 / zb.module() : 0;
  }
  // Compensation : Qc = P (tan φ − tan φ visé), arrondie aux 5 kvar
  const double cible = std::clamp(m_r.cosPhiCible, 0.5, 1.0);
  if (p > 0 && m_x.cosPhi + 1e-9 < cible) {
    const double qc = p * (q / p - std::sqrt(1.0 - cible * cible) / cible);
    m_x.compensation = std::ceil(qc / 5.0) * 5.0;
  }
  for (double kva : {100.0, 160.0, 250.0, 400.0, 630.0, 800.0, 1000.0, 1250.0, 1600.0, 2000.0, 2500.0, 3150.0})
    if (kva + 1e-9 >= m_x.s * 1.25) {
      m_x.transformateurConseille = kva;
      break;
    }
  // Avis sur la source
  const std::string appel = nombreFr(m_x.s, 1) + " kVA";
  switch (s.type) {
    case TypeSourceBT::Transformateur:
    case TypeSourceBT::Groupe:
      if (m_x.tauxCharge > 100 + 1e-9)
        avisReseau(NiveauAvis::Erreur, "Installation",
                   "La source est surchargée : " + appel + " appelés pour " + nombreFr(s.puissance) + " kVA. Prends au moins " + nombreFr(m_x.transformateurConseille) + " kVA.");
      else if (m_x.tauxCharge > 80 + 1e-9)
        avisReseau(NiveauAvis::Alerte, "Installation", "La source est chargée à " + nombreFr(m_x.tauxCharge, 0) + " % : peu de réserve pour l'avenir.");
      break;
    case TypeSourceBT::BranchementLimite:
    case TypeSourceBT::ReseauPublic:
      if (m_x.s > s.puissance + 1e-9)
        avisReseau(NiveauAvis::Alerte, "Installation",
                   "La puissance appelée (" + appel + ") dépasse la puissance " + std::string(s.type == TypeSourceBT::BranchementLimite ? "souscrite" : "de raccordement") + " (" +
                       nombreFr(s.puissance) + " kVA).");
      if (s.regime != RegimeNeutre::TT)
        avisReseau(NiveauAvis::Alerte, "NF C 15-100 312.2",
                   "Une installation alimentée par le réseau public basse tension est en schéma TT : choisis TT (ou un transformateur privé).");
      if (s.type == TypeSourceBT::BranchementLimite && s.puissance > 36 + 1e-9)
        avisReseau(NiveauAvis::Alerte, "NF C 14-100", "Un branchement à puissance limitée ne dépasse pas 36 kVA : passe au branchement > 36 kVA.");
      if (s.type == TypeSourceBT::ReseauPublic && (s.puissance < 36 - 1e-9 || s.puissance > 250 + 1e-9))
        avisReseau(NiveauAvis::Info, "NF C 14-100", "Les branchements basse tension > 36 kVA vont jusqu'à 250 kVA ; au-delà, poste de livraison HTA.");
      break;
  }
  if (s.regime == RegimeNeutre::IT && !s.controleurIsolement)
    avisReseau(NiveauAvis::Erreur, "NF C 15-100 411.6.3", "En schéma IT, un contrôleur permanent d'isolement signale le premier défaut : pose-le.");
  if (m_x.compensation > 0)
    avisReseau(NiveauAvis::Info, "Compensation",
               "cos φ " + nombreFr(m_x.cosPhi, 2) + " : une batterie de " + nombreFr(m_x.compensation) + " kvar le relève à " + nombreFr(cible, 2) + " (" +
                   std::string(s.type == TypeSourceBT::Transformateur && m_x.compensation <= 0.15 * s.puissance ? "fixe" : "automatique, à gradins") + ").");
  if (m_r.departs.empty()) avisReseau(NiveauAvis::Info, "Installation", "Ajoute le tableau général, puis ses départs.");
}

ResultatReseauBT Calcul::executer() {
  m_u = m_r.source.tension > 0 ? m_r.source.tension : 400;
  m_u0 = m_u / RACINE3;
  m_z = impedancesSource(m_r.source);
  construireArbre();
  const std::size_t n = m_r.departs.size();
  m_pPhase.assign(n, {0, 0, 0});
  m_qPhase.assign(n, {0, 0, 0});
  m_pFixe.assign(n, {0, 0, 0});
  m_qFixe.assign(n, {0, 0, 0});
  m_prot.assign(n, {});
  {
    std::set<std::size_t> pile;
    for (std::size_t r : m_racines) calculerCharges(r, pile);
  }
  bilanSource();
  for (std::size_t r : m_racines) choisirProtections(r);
  Amont a;
  a.ik3Bus = m_x.ik3Source;
  {
    const Z zb = m_z.boucleMax;
    a.ik1Bus = zb.module() > 0 ? C_MAX * M_CHARGE * m_u0 / zb.module() : 0;
  }
  a.mono = m_z.mono;
  for (std::size_t r : m_racines) dimensionner(r, a);
  for (const auto& d : m_x.departs) {
    m_x.erreurs += d.erreurs();
    m_x.alertes += d.alertes();
  }
  for (const auto& v : m_x.avis) {
    if (v.niveau == NiveauAvis::Erreur) ++m_x.erreurs;
    if (v.niveau == NiveauAvis::Alerte) ++m_x.alertes;
  }
  return m_x;
}

}  // namespace

double courantFusionGG(double calibre, double temps) {
  const PorteFusible* g = porteGG(calibre);
  if (!g) return 0;
  return temps >= 1 ? g->imax5 : g->imax01;
}

int ResultatDepartBT::erreurs() const {
  return static_cast<int>(std::count_if(avis.begin(), avis.end(), [](const AvisCalcul& a) { return a.niveau == NiveauAvis::Erreur; }));
}
int ResultatDepartBT::alertes() const {
  return static_cast<int>(std::count_if(avis.begin(), avis.end(), [](const AvisCalcul& a) { return a.niveau == NiveauAvis::Alerte; }));
}
const ResultatDepartBT* ResultatReseauBT::depart(const std::string& uuid) const {
  for (const auto& d : departs)
    if (d.uuid == uuid) return &d;
  return nullptr;
}

ResultatReseauBT calculerReseau(const ReseauBT& r) {
  Calcul c(r);
  return c.executer();
}

// ---------- Aides à la saisie ----------
const char* nomTypeSource(TypeSourceBT t) {
  switch (t) {
    case TypeSourceBT::BranchementLimite: return "Réseau public, branchement ≤ 36 kVA";
    case TypeSourceBT::ReseauPublic: return "Réseau public, branchement > 36 kVA";
    case TypeSourceBT::Transformateur: return "Transformateur HTA/BT (poste privé)";
    case TypeSourceBT::Groupe: return "Groupe électrogène";
  }
  return "";
}
const char* nomTypeCharge(TypeCharge t) {
  switch (t) {
    case TypeCharge::Tableau: return "Tableau";
    case TypeCharge::Eclairage: return "Éclairage";
    case TypeCharge::Prises: return "Prises de courant";
    case TypeCharge::Chauffage: return "Chauffage";
    case TypeCharge::Moteur: return "Moteur";
    case TypeCharge::IRVE: return "Borne de recharge (IRVE)";
    case TypeCharge::Condensateurs: return "Condensateurs";
    case TypeCharge::Autre: return "Autre récepteur";
  }
  return "";
}
const char* nomPolarite(Polarite p) {
  switch (p) {
    case Polarite::TriphaseNeutre: return "Triphasé + neutre";
    case Polarite::Triphase: return "Triphasé sans neutre";
    case Polarite::Monophase: return "Monophasé";
  }
  return "";
}
const char* nomTypeProtection(TypeProtection t) {
  switch (t) {
    case TypeProtection::Auto: return "Au mieux";
    case TypeProtection::Modulaire: return "Disjoncteur modulaire";
    case TypeProtection::Reglable: return "Disjoncteur réglable";
    case TypeProtection::Fusible: return "Fusibles gG";
    case TypeProtection::Moteur: return "Disjoncteur moteur";
  }
  return "";
}
const char* nomAme(Ame a) { return a == Ame::Cuivre ? "Cuivre" : "Aluminium"; }
const char* nomRegime(RegimeNeutre r) {
  switch (r) {
    case RegimeNeutre::TT: return "TT";
    case RegimeNeutre::TNS: return "TN-S";
    case RegimeNeutre::TNC: return "TN-C";
    case RegimeNeutre::IT: return "IT";
  }
  return "";
}

std::string prochainRepere(const ReseauBT& r, TypeCharge t) {
  if (t == TypeCharge::Tableau) {
    bool tgbt = false;
    int n = 0;
    for (const auto& d : r.departs) {
      if (d.repere == "TGBT") tgbt = true;
      if (d.repere.rfind("TD", 0) == 0) n = std::max(n, static_cast<int>(detail::lireNombre(d.repere)));
    }
    return tgbt ? "TD" + std::to_string(n + 1) : "TGBT";
  }
  int n = 0;
  for (const auto& d : r.departs)
    if (d.repere.size() > 1 && d.repere[0] == 'Q' && std::isdigit(static_cast<unsigned char>(d.repere[1]))) n = std::max(n, static_cast<int>(detail::lireNombre(d.repere)));
  return "Q" + std::to_string(n + 1);
}

DepartBT nouveauDepart(const ReseauBT& r, TypeCharge t, const std::string& parent) {
  DepartBT d;
  d.uuid = ecad::nouvelUuid();
  d.parent = parent;
  d.charge = t;
  d.repere = prochainRepere(r, t);
  bool mono = r.source.type == TypeSourceBT::BranchementLimite && !r.source.triphase;
  for (const auto& x : r.departs)
    if (x.uuid == parent && x.polarite == Polarite::Monophase) mono = true;
  switch (t) {
    case TypeCharge::Tableau:
      d.nom = parent.empty() ? "Tableau général" : "Tableau divisionnaire";
      d.puissance = 0;
      d.polarite = Polarite::TriphaseNeutre;
      d.longueur = parent.empty() ? 10 : 30;
      break;
    case TypeCharge::Eclairage:
      d.nom = "Éclairage";
      d.puissance = 1.5;
      d.cosPhi = 0.95;
      d.polarite = Polarite::Monophase;
      d.longueur = 30;
      break;
    case TypeCharge::Prises:
      d.nom = "Prises de courant";
      d.puissance = 2;
      d.cosPhi = 0.9;
      d.polarite = Polarite::Monophase;
      d.longueur = 25;
      d.calibreImpose = 20;
      d.sectionMini = 2.5;
      d.differentiel = 30;
      d.typeDifferentiel = "A";
      break;
    case TypeCharge::Chauffage:
      d.nom = "Chauffage";
      d.puissance = 6;
      d.cosPhi = 1;
      d.polarite = Polarite::Triphase;
      d.longueur = 25;
      break;
    case TypeCharge::Moteur:
      d.nom = "Moteur";
      d.puissance = 7.5;
      d.cosPhi = 0.85;
      d.rendement = 0.88;
      d.ku = 1;
      d.demarrage = 7;
      d.polarite = Polarite::Triphase;
      d.longueur = 30;
      d.courbe = "D";
      break;
    case TypeCharge::IRVE:
      d.nom = "Borne de recharge";
      d.puissance = 7.4;
      d.cosPhi = 1;
      d.polarite = Polarite::Monophase;
      d.longueur = 30;
      d.differentiel = 30;
      d.typeDifferentiel = "A";
      break;
    case TypeCharge::Condensateurs:
      d.nom = "Batterie de condensateurs";
      d.puissance = 50;
      d.cosPhi = 0;
      d.polarite = Polarite::Triphase;
      d.longueur = 5;
      break;
    case TypeCharge::Autre:
      d.nom = "Départ";
      d.puissance = 5;
      d.polarite = Polarite::TriphaseNeutre;
      break;
  }
  if (mono) d.polarite = Polarite::Monophase;
  return d;
}

std::vector<std::pair<std::size_t, int>> ordreArbre(const ReseauBT& r) {
  std::vector<std::pair<std::size_t, int>> v;
  std::map<std::string, std::size_t> indice;
  for (std::size_t i = 0; i < r.departs.size(); ++i) indice[r.departs[i].uuid] = i;
  std::vector<bool> vu(r.departs.size(), false);
  std::function<void(std::size_t, int)> parcourir = [&](std::size_t i, int profondeur) {
    if (vu[i]) return;
    vu[i] = true;
    v.push_back({i, profondeur});
    for (std::size_t k = 0; k < r.departs.size(); ++k)
      if (!vu[k] && r.departs[k].parent == r.departs[i].uuid) parcourir(k, profondeur + 1);
  };
  for (std::size_t i = 0; i < r.departs.size(); ++i) {
    const std::string& p = r.departs[i].parent;
    const auto it = p.empty() ? indice.end() : indice.find(p);
    if (it == indice.end() || r.departs[it->second].charge != TypeCharge::Tableau) parcourir(i, 0);
  }
  // Ce qui reste (boucles) : à la racine
  for (std::size_t i = 0; i < r.departs.size(); ++i)
    if (!vu[i]) parcourir(i, 0);
  return v;
}

int supprimerDepart(ReseauBT& r, const std::string& uuid) {
  std::set<std::string> aRetirer{uuid};
  bool ajoute = true;
  while (ajoute) {
    ajoute = false;
    for (const auto& d : r.departs)
      if (aRetirer.count(d.parent) && !aRetirer.count(d.uuid)) {
        aRetirer.insert(d.uuid);
        ajoute = true;
      }
  }
  const auto avant = r.departs.size();
  r.departs.erase(std::remove_if(r.departs.begin(), r.departs.end(), [&](const DepartBT& d) { return aRetirer.count(d.uuid) > 0; }), r.departs.end());
  return static_cast<int>(avant - r.departs.size());
}

const char* nomModeleReseau(ModeleReseau m) {
  switch (m) {
    case ModeleReseau::Vide: return "Installation vide";
    case ModeleReseau::Tertiaire: return "Bâtiment tertiaire (transformateur 250 kVA)";
    case ModeleReseau::Atelier: return "Atelier (tarif jaune, moteurs)";
    case ModeleReseau::ParkingIRVE: return "Parking avec bornes de recharge";
    case ModeleReseau::Maison: return "Maison individuelle (12 kVA)";
  }
  return "";
}

ReseauBT reseauModele(ModeleReseau m) {
  ReseauBT r;
  auto ajouter = [&r](TypeCharge t, const std::string& parent, const std::string& nom, double p, double l) -> DepartBT& {
    DepartBT d = nouveauDepart(r, t, parent);
    d.nom = nom;
    if (t != TypeCharge::Tableau) d.puissance = p;
    d.longueur = l;
    r.departs.push_back(d);
    return r.departs.back();
  };
  switch (m) {
    case ModeleReseau::Vide: {
      r.nom = "Installation";
      r.source.type = TypeSourceBT::ReseauPublic;
      r.source.puissance = 100;
      r.source.regime = RegimeNeutre::TT;
      r.source.resistanceTerre = 30;
      DepartBT& t = ajouter(TypeCharge::Tableau, "", "Tableau général", 0, 10);
      t.differentiel = 500;
      t.differentielSelectif = true;
      break;
    }
    case ModeleReseau::Tertiaire: {
      r.nom = "Bâtiment de bureaux";
      r.source.type = TypeSourceBT::Transformateur;
      r.source.puissance = 250;
      r.source.ucc = 4;
      r.source.regime = RegimeNeutre::TNS;
      DepartBT& g = ajouter(TypeCharge::Tableau, "", "Tableau général basse tension", 0, 8);
      g.unipolaire = true;
      const std::string tgbt = g.uuid;
      const std::string rdc = ajouter(TypeCharge::Tableau, tgbt, "Tableau du rez-de-chaussée", 0, 35).uuid;
      ajouter(TypeCharge::Eclairage, rdc, "Éclairage du hall", 1.2, 30);
      ajouter(TypeCharge::Eclairage, rdc, "Éclairage des bureaux", 2.4, 40);
      ajouter(TypeCharge::Prises, rdc, "Prises des bureaux est", 2, 35);
      ajouter(TypeCharge::Prises, rdc, "Prises des bureaux ouest", 2, 45);
      const std::string etage = ajouter(TypeCharge::Tableau, tgbt, "Tableau de l'étage", 0, 50).uuid;
      ajouter(TypeCharge::Eclairage, etage, "Éclairage de l'étage", 2.4, 40);
      ajouter(TypeCharge::Prises, etage, "Prises de l'étage", 2, 40);
      ajouter(TypeCharge::Chauffage, etage, "Chauffage de l'étage", 9, 30);
      DepartBT& cta = ajouter(TypeCharge::Moteur, tgbt, "Centrale de traitement d'air", 15, 40);
      cta.demarrage = 2.5;  // variateur
      DepartBT& asc = ajouter(TypeCharge::Moteur, tgbt, "Ascenseur", 9, 30);
      asc.demarrage = 2.5;
      DepartBT& froid = ajouter(TypeCharge::Autre, tgbt, "Groupe froid", 60, 25);
      froid.polarite = Polarite::Triphase;
      froid.cosPhi = 0.85;
      ajouter(TypeCharge::Condensateurs, tgbt, "Batterie de condensateurs", 30, 5);
      break;
    }
    case ModeleReseau::Atelier: {
      r.nom = "Atelier de mécanique";
      r.source.type = TypeSourceBT::ReseauPublic;
      r.source.puissance = 120;
      r.source.regime = RegimeNeutre::TT;
      r.source.resistanceTerre = 30;
      DepartBT& g = ajouter(TypeCharge::Tableau, "", "Tableau général", 0, 15);
      g.differentiel = 500;
      g.differentielSelectif = true;
      const std::string tgbt = g.uuid;
      ajouter(TypeCharge::Moteur, tgbt, "Compresseur", 15, 25);
      ajouter(TypeCharge::Moteur, tgbt, "Tour", 7.5, 20);
      ajouter(TypeCharge::Moteur, tgbt, "Fraiseuse", 5.5, 22);
      DepartBT& pont = ajouter(TypeCharge::Moteur, tgbt, "Pont roulant", 4, 30);
      pont.ku = 0.5;
      DepartBT& ecl = ajouter(TypeCharge::Eclairage, tgbt, "Éclairage de l'atelier", 3, 40);
      ecl.differentiel = 30;
      ajouter(TypeCharge::Prises, tgbt, "Prises de l'atelier", 3, 35);
      const std::string bureaux = ajouter(TypeCharge::Tableau, tgbt, "Tableau des bureaux", 0, 30).uuid;
      ajouter(TypeCharge::Eclairage, bureaux, "Éclairage des bureaux", 1, 20);
      ajouter(TypeCharge::Prises, bureaux, "Prises des bureaux", 2, 20);
      ajouter(TypeCharge::Chauffage, bureaux, "Chauffage des bureaux", 4, 15);
      break;
    }
    case ModeleReseau::ParkingIRVE: {
      r.nom = "Parking de 8 places équipées";
      r.source.type = TypeSourceBT::ReseauPublic;
      r.source.puissance = 100;
      r.source.regime = RegimeNeutre::TT;
      r.source.resistanceTerre = 30;
      DepartBT& g = ajouter(TypeCharge::Tableau, "", "Tableau général", 0, 10);
      g.differentiel = 500;
      g.differentielSelectif = true;
      const std::string tgbt = g.uuid;
      DepartBT& td = ajouter(TypeCharge::Tableau, tgbt, "Tableau des bornes", 0, 40);
      const std::string irve = td.uuid;
      for (int k = 1; k <= 8; ++k) {
        DepartBT& b = ajouter(TypeCharge::IRVE, irve, "Borne " + std::to_string(k), 7.4, 10 + 4.0 * k);
        b.phase = (k - 1) % 3 + 1;
        b.pose = MethodePose::D1;
        b.temperature = 20;
      }
      DepartBT& ecl = ajouter(TypeCharge::Eclairage, tgbt, "Éclairage du parking", 1, 50);
      ecl.differentiel = 30;
      break;
    }
    case ModeleReseau::Maison: {
      r.nom = "Maison";
      r.source.type = TypeSourceBT::BranchementLimite;
      r.source.puissance = 12;
      r.source.triphase = false;
      r.source.icc = 6;
      r.source.regime = RegimeNeutre::TT;
      r.source.resistanceTerre = 50;
      r.source.domestique = true;
      DepartBT& t = ajouter(TypeCharge::Tableau, "", "Tableau de répartition", 0, 2);
      t.calibreImpose = 60;
      t.differentiel = 500;
      t.differentielSelectif = true;
      t.pose = MethodePose::B1;
      t.sectionMini = 10;
      t.ks = 0.4;
      t.courbe = "D";  // disjoncteur de branchement : déclenchement instantané haut
      const std::string tab = t.uuid;
      auto final = [&](TypeCharge c, const std::string& nom, double p, double l, double calibre, double smin) {
        DepartBT& d = ajouter(c, tab, nom, p, l);
        d.pose = MethodePose::B1;  // conducteurs sous conduits encastrés dans la maçonnerie
        d.isolant = Isolant::PVC;
        d.differentiel = 30;
        d.typeDifferentiel = c == TypeCharge::Eclairage ? "AC" : "A";
        if (calibre > 0) d.calibreImpose = calibre;
        if (smin > 0) d.sectionMini = smin;
      };
      final(TypeCharge::Eclairage, "Éclairage du séjour", 0.8, 15, 16, 1.5);
      final(TypeCharge::Eclairage, "Éclairage des chambres", 0.6, 20, 16, 1.5);
      final(TypeCharge::Prises, "Prises du séjour", 2, 15, 20, 2.5);
      final(TypeCharge::Prises, "Prises des chambres", 2, 22, 20, 2.5);
      final(TypeCharge::Chauffage, "Plaque de cuisson", 7.2, 10, 32, 6);
      final(TypeCharge::Autre, "Four", 3.5, 10, 20, 2.5);
      final(TypeCharge::Autre, "Lave-linge", 2.5, 12, 20, 2.5);
      final(TypeCharge::Chauffage, "Chauffe-eau", 3, 14, 20, 2.5);
      final(TypeCharge::IRVE, "Borne de recharge (pilotée)", 3.7, 18, 20, 2.5);
      break;
    }
  }
  return r;
}

}  // namespace prom::elec
