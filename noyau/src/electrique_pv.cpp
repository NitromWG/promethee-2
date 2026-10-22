// Prométhée : installation photovoltaïque raccordée au réseau. Règles du guide UTE C 15-712-1 (reprises par la
// NF C 15-100, partie 7-712) telles que les détaille le guide SER/ADEME « protection des personnes et des biens »
// (2012) : tension à vide au froid (coefficient de température du module, à défaut tableau par température
// minimale), composants dimensionnés sur 1,25 Isc STC, protection des chaînes quand leur nombre dépasse
// 1 + IRM / Isc, fusibles gPV entre 1,4 Isc et IRM, câbles des chaînes et câble principal, interrupteur-sectionneur
// continu, courant de sortie de l'onduleur pris à 1,1 fois le nominal sans donnée du fabricant ; longueur
// critique des parafoudres du guide UTE C 61-740-52. Câbles solaires H1Z2Z2-K (EN 50618). Licence GPL-3.0-only.
#include "promethee/electrique_pv.hpp"

#include <algorithm>
#include <cmath>

#include "electrique_interne.hpp"

namespace prom::elec {

using detail::nombreFr;

namespace {

const double RACINE3 = std::sqrt(3.0);

// Câble solaire H1Z2Z2-K (EN 50618, annexe A, tableau A.1) : deux câbles jointifs sur une paroi (le + et le − d'une
// chaîne cheminent ensemble), 60 °C ambiants, âme à 120 °C
struct IzSolaire {
  double s, iz;
};
const std::vector<IzSolaire> IZ_SOLAIRE = {{1.5, 24}, {2.5, 33}, {4, 44}, {6, 57}, {10, 79}, {16, 107}, {25, 142}, {35, 176}, {50, 221}, {70, 278}, {95, 333}, {120, 390}};
// Facteur de température (tableau A.2) : 1 jusqu'à 60 °C, 0,91 à 70 °C, 0,82 à 80 °C, 0,71 à 90 °C…
double facteurSolaire(double t) { return t <= 60 ? 1.0 : t < 120 ? std::sqrt((120 - t) / 60.0) : 0.0; }
double izSolaire(double s, double temperature) {
  for (const auto& x : IZ_SOLAIRE)
    if (std::abs(x.s - s) < 1e-9) return x.iz * facteurSolaire(temperature);
  return 0;
}
const std::vector<double> SECTIONS_DC = {2.5, 4, 6, 10, 16, 25, 35, 50, 70, 95, 120};
const std::vector<double> FUSIBLES_GPV = {1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 16, 20, 25, 30, 32, 35, 40, 50, 63};
const std::vector<double> CALIBRES = {6, 10, 13, 16, 20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 250, 320, 400, 500, 630};

// Coefficient de la tension à vide selon la température minimale (cristallin, sans donnée du fabricant)
double kTableau(double t) {
  struct Ligne {
    double t, k;
  };
  static const Ligne lignes[] = {{20, 1.02}, {15, 1.04}, {10, 1.06}, {5, 1.08}, {0, 1.10}, {-5, 1.12}, {-10, 1.14}, {-15, 1.16}, {-20, 1.18}, {-25, 1.20}, {-30, 1.21}, {-35, 1.23}};
  for (const auto& l : lignes)
    if (t >= l.t - 1e-9) return l.k;
  return 1.25;
}
double calibreAuDessus(double courant) {
  for (double c : CALIBRES)
    if (c + 1e-9 >= courant) return c;
  return 0;
}
std::string n(double v, int d = 2) { return nombreFr(v, d); }
std::string pluriel(int k, const std::string& mot) { return std::to_string(k) + " " + mot + (k > 1 ? "s" : ""); }

}  // namespace

int ResultatPV::erreurs() const {
  return static_cast<int>(std::count_if(avis.begin(), avis.end(), [](const AvisCalcul& a) { return a.niveau == NiveauAvis::Erreur; }));
}
int ResultatPV::alertes() const {
  return static_cast<int>(std::count_if(avis.begin(), avis.end(), [](const AvisCalcul& a) { return a.niveau == NiveauAvis::Alerte; }));
}

double coefficientOrientation(double orientation, double inclinaison) {
  // Lignes : inclinaison 0°, 30°, 60°, 90° ; colonnes : écart au sud 0° (sud), 45° (sud-est, sud-ouest), 90° (est, ouest)
  static const double t[4][3] = {{0.93, 0.93, 0.93}, {1.00, 0.96, 0.90}, {0.91, 0.88, 0.78}, {0.68, 0.66, 0.55}};
  double o = std::isfinite(orientation) ? std::fmod(orientation, 360.0) : 0;
  if (o > 180) o -= 360;
  if (o < -180) o += 360;
  const double a = std::clamp(std::abs(o), 0.0, 90.0) / 45.0;
  const double i = std::clamp(std::isfinite(inclinaison) ? inclinaison : 30.0, 0.0, 90.0) / 30.0;
  const int ia = std::min(1, static_cast<int>(a)), ii = std::min(2, static_cast<int>(i));
  const double fa = a - ia, fi = i - ii;
  return (1 - fi) * ((1 - fa) * t[ii][ia] + fa * t[ii][ia + 1]) + fi * ((1 - fa) * t[ii + 1][ia] + fa * t[ii + 1][ia + 1]);
}

const std::vector<std::pair<std::string, double>>& productiblesIndicatifs() {
  static const std::vector<std::pair<std::string, double>> v = {{"Nord, Normandie, Hauts-de-France (≈ 950)", 950},
                                                                {"Bretagne, Île-de-France, Centre, Est (≈ 1 100)", 1100},
                                                                {"Pays de la Loire, Bourgogne, Rhône-Alpes, Aquitaine (≈ 1 200)", 1200},
                                                                {"Occitanie, vallée du Rhône (≈ 1 300)", 1300},
                                                                {"Provence, Côte d'Azur, Corse (≈ 1 400)", 1400}};
  return v;
}

const char* nomBatimentPV(BatimentPV b) {
  switch (b) {
    case BatimentPV::Habitation: return "Maison ou immeuble d'habitation";
    case BatimentPV::Tertiaire: return "Bâtiment tertiaire, industriel ou agricole";
    case BatimentPV::CentraleSol: return "Centrale au sol";
  }
  return "Maison ou immeuble d'habitation";
}

ResultatPV calculerPV(const InstallationPV& in) {
  ResultatPV x;
  const ModulePV& m = in.module;
  const OnduleurPV& o = in.onduleur;
  auto avis = [&x](NiveauAvis niveau, const std::string& regle, const std::string& message) { x.avis.push_back({niveau, regle, message}); };
  const double voc = std::max(1.0, m.voc), vmp = std::max(1.0, m.vmp), isc = std::max(0.1, m.isc), imp = std::max(0.1, m.imp);
  const int onduleurs = std::max(1, in.onduleurs);
  // ----- Températures extrêmes -----
  x.kTemperature = std::max(1.0, m.coefVoc != 0 ? 1 + m.coefVoc / 100 * (in.tMin - 25) : kTableau(in.tMin));
  x.uocModuleMax = voc * x.kTemperature;
  x.vmpModuleMin = vmp * (1 + (m.coefVoc != 0 ? m.coefVoc : -0.35) / 100 * (in.tMax - 25));
  x.vmpModuleMax = vmp * x.kTemperature;
  const double tensionAdmise = std::min(o.tensionDCMax, m.tensionMax);
  x.seriesMax = static_cast<int>(std::floor(tensionAdmise / x.uocModuleMax + 1e-9));
  x.seriesMin = std::max(1, static_cast<int>(std::ceil(o.mpptMin / std::max(1.0, x.vmpModuleMin) - 1e-9)));
  // ----- Calepinage : des chaînes égales, réparties sur les entrées (MPPT) des onduleurs -----
  const int entreesParOnduleur = std::max(1, o.mppt);
  const int entrees = entreesParOnduleur * onduleurs;
  const int parEntreeMax = std::max(1, static_cast<int>(std::floor(o.iscMppt / isc + 1e-9)));
  const int total = std::max(0, in.nombreModules);
  int s = 0;
  if (in.modulesParChaine > 0) s = in.modulesParChaine;
  else if (x.seriesMin <= x.seriesMax) {
    int meilleur = 0, poses = -1;
    for (int k = std::min(x.seriesMax, total); k >= x.seriesMin; --k) {
      const int c = total / k;
      if (c == 0 || c > entrees * parEntreeMax) continue;
      if (c * k > poses) {
        poses = c * k;
        meilleur = k;
      }
      if (c * k == total) break;  // la plus longue chaîne qui pose tous les modules
    }
    s = meilleur;
  }
  if (x.seriesMin > x.seriesMax)
    avis(NiveauAvis::Erreur, "UTE C 15-712-1",
         "Module et onduleur incompatibles : au moins " + std::to_string(x.seriesMin) + " modules en série pour entrer dans la plage MPPT, mais " + std::to_string(x.seriesMax) +
             " au plus pour rester sous " + n(tensionAdmise, 0) + " V à vide par grand froid.");
  if (s <= 0) {
    if (x.seriesMin <= x.seriesMax) avis(NiveauAvis::Erreur, "Calepinage", "Pas assez de modules (ou trop pour cet onduleur) pour former des chaînes valables.");
    return x;
  }
  x.modulesParChaine = s;
  x.chaines = total / s;
  x.modulesPoses = x.chaines * s;
  // Une chaîne par onduleur, puis la suivante sur l'entrée suivante : les onduleurs restent équilibrés
  x.repartition.assign(static_cast<std::size_t>(entrees), 0);
  for (int c = 0; c < x.chaines; ++c) {
    const int ond = c % onduleurs, entree = (c / onduleurs) % entreesParOnduleur;
    ++x.repartition[static_cast<std::size_t>(ond * entreesParOnduleur + entree)];
  }
  x.chainesParMppt = std::max(1, *std::max_element(x.repartition.begin(), x.repartition.end()));
  int chainesOnduleurMax = 0;
  for (int k = 0; k < onduleurs; ++k) {
    int c = 0;
    for (int e = 0; e < entreesParOnduleur; ++e) c += x.repartition[static_cast<std::size_t>(k * entreesParOnduleur + e)];
    chainesOnduleurMax = std::max(chainesOnduleurMax, c);
  }
  x.puissanceCrete = x.modulesPoses * m.puissance / 1000;
  const double creteOnduleurMax = chainesOnduleurMax * s * m.puissance;  // W
  x.ratioDCAC = o.puissanceAC > 0 ? creteOnduleurMax / o.puissanceAC : 0;
  x.uocChaine = s * x.uocModuleMax;
  x.vmpChaineMin = s * x.vmpModuleMin;
  x.vmpChaineMax = s * x.vmpModuleMax;
  x.iscChaine = 1.25 * isc;
  x.iscMppt = x.chainesParMppt * x.iscChaine;
  if (x.modulesPoses < total)
    avis(NiveauAvis::Alerte, "Calepinage",
         std::to_string(total - x.modulesPoses) + " module(s) ne trouvent pas place dans des chaînes égales : ajoute ou retire des modules, ou change de chaîne.");
  if (x.chaines < entrees && x.chaines > 0)
    avis(NiveauAvis::Info, "Calepinage", pluriel(entrees - x.chaines, "entrée") + " d'onduleur sans chaîne : de la place pour agrandir plus tard.");
  if (x.uocChaine > tensionAdmise + 1e-9)
    avis(NiveauAvis::Erreur, "UTE C 15-712-1",
         "Par " + n(in.tMin, 0) + " °C, la tension à vide de la chaîne atteint " + n(x.uocChaine, 0) + " V, au-delà des " + n(tensionAdmise, 0) +
             " V admis : moins de modules en série.");
  if (x.vmpChaineMin < o.mpptMin - 1e-9)
    avis(NiveauAvis::Alerte, "Onduleur",
         "Cellules à " + n(in.tMax, 0) + " °C : la chaîne descend à " + n(x.vmpChaineMin, 0) + " V, sous la plage MPPT (" + n(o.mpptMin, 0) + " V) : l'onduleur décroche par forte chaleur.");
  if (x.vmpChaineMax > o.mpptMax + 1e-9)
    avis(NiveauAvis::Info, "Onduleur", "Par grand froid, la chaîne dépasse la plage MPPT (" + n(x.vmpChaineMax, 0) + " V) : l'onduleur écrête un peu.");
  if (x.chainesParMppt * isc > o.iscMppt + 1e-9)
    avis(NiveauAvis::Erreur, "Onduleur",
         std::to_string(x.chainesParMppt) + " chaîne(s) sur une entrée : " + n(x.chainesParMppt * isc, 1) + " A de court-circuit pour " + n(o.iscMppt, 0) +
             " A admis par l'onduleur. Ajoute un onduleur ou une entrée.");
  else if (x.chainesParMppt * imp > o.courantMppt + 1e-9)
    avis(NiveauAvis::Alerte, "Onduleur", "Le courant d'une entrée (" + n(x.chainesParMppt * imp, 1) + " A) dépasse ce que l'onduleur exploite (" + n(o.courantMppt, 0) + " A) : écrêtage.");
  if (creteOnduleurMax > o.puissanceDCMax + 1e-9)
    avis(NiveauAvis::Erreur, "Onduleur",
         "Puissance crête par onduleur (" + n(creteOnduleurMax / 1000, 2) + " kWc) au-delà de la puissance continue qu'il admet (" + n(o.puissanceDCMax / 1000, 1) + " kW).");
  if (x.ratioDCAC > 1.3 + 1e-9)
    avis(NiveauAvis::Alerte, "Onduleur", "Rapport puissance crête / puissance de l'onduleur de " + n(x.ratioDCAC, 2) + " : écrêtage fréquent aux heures de plein soleil.");
  else if (x.ratioDCAC < 0.8 && x.ratioDCAC > 0)
    avis(NiveauAvis::Info, "Onduleur", "Onduleur surdimensionné (rapport " + n(x.ratioDCAC, 2) + ") : un modèle plus petit suffirait.");

  // ----- Protection des chaînes -----
  const int nc = x.chainesParMppt;  // chaînes en parallèle sur une même entrée
  x.chainesMaxSansProtection = static_cast<int>(std::floor(1 + m.courantInverse / isc + 1e-9));
  x.fusibles = nc > x.chainesMaxSansProtection;
  if (x.fusibles) {
    for (double f : FUSIBLES_GPV)
      if (f + 1e-9 >= 1.4 * isc) {
        x.calibreFusible = f;
        break;
      }
    if (x.calibreFusible <= 0 || x.calibreFusible > m.courantInverse + 1e-9)
      avis(NiveauAvis::Erreur, "UTE C 15-712-1", "Aucun fusible gPV entre 1,4 Isc (" + n(1.4 * isc, 1) + " A) et le courant inverse admissible du module (" + n(m.courantInverse, 0) + " A).");
    else
      avis(NiveauAvis::Info, "UTE C 15-712-1",
           std::to_string(nc) + " chaînes en parallèle : chacune est protégée par des fusibles gPV de " + n(x.calibreFusible, 0) + " A sur ses deux polarités.");
  }
  // ----- Câbles des chaînes -----
  // Sans protection, un câble de chaîne porte au plus le courant de défaut des autres chaînes de l'entrée
  x.izRequis = x.fusibles ? 1.45 * x.calibreFusible : (nc <= 2 ? 1.25 * isc : (nc - 1) * 1.25 * isc);
  const double rho1 = 0.023;  // Ω·mm²/m, cuivre chaud
  const double vmpStc = s * vmp;
  auto chuteDC = [&](double sec, double courant, double longueur) { return 100.0 * 2 * rho1 * longueur * courant / (sec * vmpStc); };
  // Passes : chute de 1 % (conseillée), puis de 3 % (maximum) ; un câble de chaîne reste d'abord dans les sections
  // que prennent les connecteurs courants (10 mm² au plus) ; 4 mm² au moins quand la section est calculée
  auto choisir = [&](double izMin, double courant, double longueur, double imposee, bool chaine, double* iz, double* chute) {
    double retenue = 0;
    for (int passe = 0; passe < 3 && retenue <= 0; ++passe)
      for (double sec : SECTIONS_DC) {
        if (imposee > 0 && std::abs(sec - imposee) > 1e-9) continue;
        if (imposee <= 0) {
          if (sec < 4 - 1e-9 || (chaine && passe < 2 && sec > 10 + 1e-9)) continue;
          if (izSolaire(sec, in.temperatureCables) + 1e-9 < izMin) continue;
          if (chuteDC(sec, courant, longueur) > (passe == 0 ? 1.0 : 3.0) + 1e-9) continue;
        }
        retenue = sec;
        break;
      }
    if (retenue <= 0) retenue = imposee > 0 ? imposee : SECTIONS_DC.back();
    *iz = izSolaire(retenue, in.temperatureCables);
    *chute = chuteDC(retenue, courant, longueur);
    return retenue;
  };
  x.sectionDC = choisir(x.izRequis, imp, in.longueurDC, in.sectionDC, true, &x.izDC, &x.chuteDC);
  if (in.sectionDC > 0 && x.izDC <= 0)
    avis(NiveauAvis::Erreur, "EN 50618", "Section continue de " + n(in.sectionDC, -1) + " mm² hors des sections de câble solaire (4 à 120 mm²).");
  else if (x.izDC + 1e-9 < x.izRequis)
    avis(NiveauAvis::Erreur, "UTE C 15-712-1", "Câble de chaîne de " + n(x.sectionDC, -1) + " mm² : " + n(x.izDC, 0) + " A admissibles pour " + n(x.izRequis, 1) + " A requis.");
  if (x.sectionDC > 10 + 1e-9)
    avis(NiveauAvis::Alerte, "Connecteurs", "Câble de chaîne de " + n(x.sectionDC, -1) +
                                                " mm² : au-delà de ce que prennent les connecteurs courants (4 à 6 mm², 10 mm² en version spéciale). Rapproche l'onduleur des modules.");
  if (x.chuteDC > 3 + 1e-9)
    avis(NiveauAvis::Erreur, "UTE C 15-712-1", "Chute de tension continue de " + n(x.chuteDC) + " % au-delà de 3 % : section plus forte ou onduleur plus près des modules.");
  else if (x.chuteDC > 1 + 1e-9)
    avis(NiveauAvis::Alerte, "Guide UTE C 15-712-1", "Chute de tension continue de " + n(x.chuteDC) + " % : 1 % au plus est conseillé (perte de production).");
  // Plus de chaînes par entrée que l'onduleur n'a de connecteurs : boîte de jonction (posée près de l'onduleur)
  x.boiteJonction = nc > std::max(1, o.entreesParMppt);
  if (x.boiteJonction) {
    double chute = 0;
    x.sectionPrincipal = choisir(1.25 * isc * nc, imp * nc, 2, 0, false, &x.izPrincipal, &chute);
    avis(NiveauAvis::Info, "UTE C 15-712-1",
         "Boîte de jonction (IP 44, IK 07 au moins) : " + std::to_string(nc) + " chaînes réunies par entrée, câble principal de " + n(x.sectionPrincipal, -1) +
             " mm² (Iz ≥ 1,25 × Isc × " + std::to_string(nc) + ").");
  }
  // ----- Sectionnement continu -----
  x.sectionneurTension = x.uocChaine;
  x.sectionneurCourant = 1.25 * isc * nc;
  avis(NiveauAvis::Info, "UTE C 15-712-1",
       "Interrupteur-sectionneur continu bipolaire près de l'onduleur, catégorie DC21B au moins : " + n(x.sectionneurTension, 0) + " V et " + n(x.sectionneurCourant, 1) +
           " A au moins par entrée (il peut être intégré à l'onduleur s'il remplit ces conditions).");
  // ----- Parafoudres -----
  const double base = in.batiment == BatimentPV::Habitation ? 115 : in.batiment == BatimentPV::Tertiaire ? 450 : 200;
  x.longueurCritique = base / std::max(0.01, in.ng);
  x.parafoudreDC = in.paratonnerre || in.longueurDC + 1e-9 >= x.longueurCritique;
  const bool parafoudreTete = in.paratonnerre || in.ng > 2.5 + 1e-9;  // NF C 15-100 443 : obligatoire en tête
  x.parafoudreAC = parafoudreTete || in.longueurAC > 10 + 1e-9;
  x.typeParafoudreAC = in.paratonnerre ? "type 1" : "type 2";
  avis(x.parafoudreDC ? NiveauAvis::Alerte : NiveauAvis::Info, "Guide UTE C 61-740-52",
       x.parafoudreDC ? "Parafoudre du côté continu obligatoire (" + std::string(in.paratonnerre ? "bâtiment équipé d'un paratonnerre" : "longueur " + n(in.longueurDC, 0) + " m ≥ Lcrit " + n(x.longueurCritique, 0) + " m") +
                            ") : type 2, tension assignée au moins " + n(x.uocChaine, 0) + " V, près de l'onduleur (et de la boîte de jonction)."
                      : "Parafoudre du côté continu non obligatoire (" + n(in.longueurDC, 0) + " m < Lcrit " + n(x.longueurCritique, 0) + " m).");
  if (parafoudreTete)
    avis(NiveauAvis::Alerte, "NF C 15-100 443 et 534",
         std::string(in.paratonnerre ? "Paratonnerre : parafoudre de type 1 en tête du tableau" : "Foudroiement élevé (Ng > 2,5) : parafoudre de type 2 en tête du tableau") +
             (in.longueurAC > 10 + 1e-9 ? ", et un second près de l'onduleur (à plus de 10 m du tableau)." : "."));
  else if (x.parafoudreAC)
    avis(NiveauAvis::Info, "Guide UTE C 15-712-1", "Si le tableau a un parafoudre, l'onduleur à plus de 10 m en demande un second près de lui (type 2).");
  // ----- Côté alternatif -----
  const double u = o.triphase ? RACINE3 * 400 : 230;
  x.courantAC = o.courantACMax > 0 ? o.courantACMax : 1.1 * o.puissanceAC / u;
  x.calibreAC = calibreAuDessus(x.courantAC);
  x.puissanceAC = o.puissanceAC * onduleurs;
  x.courantACTotal = x.courantAC * onduleurs;
  x.calibreGeneral = onduleurs > 1 ? calibreAuDessus(x.courantACTotal) : x.calibreAC;
  x.pdcAC = x.puissanceAC <= 36000 + 1e-9 ? 3 : 6;
  const int charges = o.triphase ? 3 : 2;
  const double b = o.triphase ? 1 : 2, u0 = o.triphase ? 400 / RACINE3 : 230;
  auto chuteAC = [&](double sec) { return 100.0 * b * 0.0225 * in.longueurAC / sec * x.courantAC / u0; };
  for (int passe = 0; passe < 2 && x.sectionAC <= 0; ++passe)
    for (double sec : sectionsNormalisees()) {
      if (sec + 1e-9 < std::max(2.5, in.sectionAC)) continue;
      if (in.sectionAC > 0 && std::abs(sec - in.sectionAC) > 1e-9) continue;
      if (in.sectionAC <= 0 && courantAdmissible(sec, MethodePose::C, Isolant::PR, charges) + 1e-9 < x.calibreAC) continue;
      if (in.sectionAC <= 0 && chuteAC(sec) > (passe == 0 ? 1.0 : 3.0) + 1e-9) continue;
      x.sectionAC = sec;
      break;
    }
  if (x.sectionAC <= 0) x.sectionAC = in.sectionAC > 0 ? in.sectionAC : 240;
  x.chuteAC = chuteAC(x.sectionAC);
  if (in.sectionAC > 0 && courantAdmissible(x.sectionAC, MethodePose::C, Isolant::PR, charges) + 1e-9 < x.calibreAC)
    avis(NiveauAvis::Erreur, "NF C 15-100 433", "Câble alternatif de " + n(x.sectionAC, -1) + " mm² trop faible pour une protection de " + n(x.calibreAC, 0) + " A.");
  if (x.chuteAC > 3 + 1e-9) avis(NiveauAvis::Erreur, "NF C 15-100 712", "Chute de tension alternative de " + n(x.chuteAC) + " % au-delà de 3 %.");
  else if (x.chuteAC > 1 + 1e-9)
    avis(NiveauAvis::Alerte, "Guide UTE C 15-712-1", "Chute de tension alternative de " + n(x.chuteAC) + " % : 1 % au plus est conseillé (l'onduleur décroche si la tension monte trop).");
  x.differentielAC = in.batiment == BatimentPV::Habitation ? "30 mA type A à immunité renforcée" : "300 mA type A (ou la sensibilité de la notice de l'onduleur)";
  avis(NiveauAvis::Info, "NF C 15-100 712",
       "Côté alternatif, par onduleur : disjoncteur " + n(x.calibreAC, 0) + " A, pouvoir de coupure " + n(x.pdcAC, 0) + " kA au moins, différentiel " + x.differentielAC +
           " ; type B si la notice de l'onduleur l'exige (pas de séparation entre continu et alternatif).");
  if (!o.triphase && x.puissanceAC > 6000 + 1e-9)
    avis(NiveauAvis::Erreur, "Enedis", "Injection en monophasé limitée à 6 kVA : " + n(x.puissanceAC / 1000, 1) + " kVA demandent un raccordement triphasé.");
  if (x.puissanceAC > 250000 + 1e-9)
    avis(NiveauAvis::Alerte, "Enedis", "Au-delà de 250 kVA, la production se raccorde en HTA (poste de livraison) : ce calcul ne couvre que la basse tension.");
  else if (x.puissanceAC > 36000 + 1e-9)
    avis(NiveauAvis::Info, "Enedis", "Plus de 36 kVA : branchement à puissance surveillée, disjoncteur de branchement réglable, étude de raccordement du distributeur.");
  if (in.venteTotale && x.puissanceAC <= 36000 + 1e-9) {
    static const double mono[] = {15, 30, 45, 60, 75, 90}, tri[] = {10, 20, 30, 40, 50, 60};
    for (double c : o.triphase ? std::vector<double>(std::begin(tri), std::end(tri)) : std::vector<double>(std::begin(mono), std::end(mono)))
      if (c + 1e-9 >= x.courantACTotal) {
        x.reglageBranchement = c;
        break;
      }
    avis(NiveauAvis::Info, "Guide SER/ADEME",
         "Vente totale : branchement propre, disjoncteur de branchement différentiel 500 mA type S réglé à " + n(x.reglageBranchement, 0) + " A au moins, compteur de production.");
  }
  avis(NiveauAvis::Info, "DIN VDE V 0126-1-1, EN 50549-1",
       "Découplage : la protection intégrée à l'onduleur suffit en basse tension ; garde son certificat de conformité pour le dossier de raccordement.");
  // ----- Masses, étiquetage, intervention des secours -----
  avis(NiveauAvis::Info, "UTE C 15-712-1",
       "Cadres des modules et structures reliés à la terre par un conducteur vert-jaune de 6 mm² cuivre au moins ; câbles + et − côte à côte pour réduire les boucles "
       "(surtensions induites par la foudre).");
  avis(NiveauAvis::Info, "UTE C 15-712-1",
       "Étiquettes : « Coupure d'urgence entrée onduleur » sur le sectionneur continu et « Coupure d'urgence sortie onduleur » sur la protection alternative (lettres "
       "rouges sur fond jaune), « Ne pas déconnecter en charge » près des connecteurs, danger sur les boîtes de jonction, présence de deux sources au tableau.");
  if (in.batiment != BatimentPV::Habitation)
    avis(NiveauAvis::Info, "Sécurité incendie",
         "Bâtiment recevant du public ou de grande taille : les pompiers demandent une coupure du côté continu commandée à distance au plus près des modules, ou des "
         "câbles continus à l'abri dans un cheminement protégé. Vois-le avec le bureau de contrôle.");
  // ----- Production -----
  x.coefOrientation = coefficientOrientation(in.orientation, in.inclinaison);
  if (std::abs(in.orientation) > 90 + 1e-9)
    avis(NiveauAvis::Alerte, "Production", "Pan tourné vers le nord : la production sera nettement plus faible que l'estimation (le tableau s'arrête à l'est et à l'ouest).");
  x.production = x.puissanceCrete * std::max(0.0, in.productible) * x.coefOrientation;
  return x;
}

const std::vector<ModulePV>& modulesPV() {
  static const std::vector<ModulePV> v = [] {
    std::vector<ModulePV> r;
    auto mod = [&r](const std::string& nom, double p, double voc, double vmp, double isc, double imp, double coef, double irm, double l, double h) {
      ModulePV m;
      m.nom = nom;
      m.puissance = p;
      m.voc = voc;
      m.vmp = vmp;
      m.isc = isc;
      m.imp = imp;
      m.coefVoc = coef;
      m.courantInverse = irm;
      m.largeur = l;
      m.hauteur = h;
      r.push_back(m);
    };
    mod("Module 375 Wc (monocristallin, 120 demi-cellules)", 375, 41.0, 34.4, 11.5, 10.9, -0.28, 20, 1038, 1755);
    mod("Module 425 Wc (TOPCon, 108 demi-cellules)", 425, 38.5, 32.2, 13.9, 13.2, -0.27, 25, 1134, 1722);
    mod("Module 500 Wc (monocristallin, 132 demi-cellules)", 500, 45.3, 37.9, 14.0, 13.2, -0.27, 25, 1134, 1960);
    mod("Module 550 Wc (monocristallin, 144 demi-cellules)", 550, 49.6, 41.6, 14.0, 13.2, -0.28, 25, 1134, 2279);
    return r;
  }();
  return v;
}

const std::vector<OnduleurPV>& onduleursPV() {
  static const std::vector<OnduleurPV> v = [] {
    std::vector<OnduleurPV> r;
    auto ond = [&r](const std::string& nom, double ac, double dc, double vmax, double mmin, double mmax, double imppt, double isc, int mppt, int entrees, bool tri) {
      OnduleurPV o;
      o.nom = nom;
      o.puissanceAC = ac;
      o.puissanceDCMax = dc;
      o.tensionDCMax = vmax;
      o.mpptMin = mmin;
      o.mpptMax = mmax;
      o.courantMppt = imppt;
      o.iscMppt = isc;
      o.mppt = mppt;
      o.entreesParMppt = entrees;
      o.triphase = tri;
      r.push_back(o);
    };
    ond("Onduleur monophasé 3,6 kVA (2 MPPT)", 3680, 5500, 600, 90, 520, 16, 20, 2, 1, false);
    ond("Onduleur monophasé 6 kVA (2 MPPT)", 6000, 9000, 600, 90, 520, 16, 20, 2, 1, false);
    ond("Onduleur triphasé 10 kVA (2 MPPT)", 10000, 15000, 1100, 160, 950, 16, 20, 2, 2, true);
    ond("Onduleur triphasé 30 kVA (3 MPPT)", 30000, 45000, 1100, 200, 1000, 32, 40, 3, 2, true);
    ond("Onduleur triphasé 100 kVA (10 MPPT)", 100000, 150000, 1100, 200, 1000, 32, 40, 10, 2, true);
    return r;
  }();
  return v;
}

InstallationPV installationPVType(double kWc) {
  InstallationPV i;
  i.module = modulesPV()[1];
  const double p = std::max(0.5, kWc);
  i.nombreModules = std::max(1, static_cast<int>(std::lround(p * 1000 / i.module.puissance)));
  const auto& o = onduleursPV();
  if (p <= 4.5) i.onduleur = o[0];
  else if (p <= 7.5) i.onduleur = o[1];
  else if (p <= 13) i.onduleur = o[2];
  else if (p <= 39) i.onduleur = o[3];
  else {
    i.onduleur = o[4];
    i.onduleurs = static_cast<int>(std::ceil(p / 130));
  }
  i.batiment = p <= 36 ? BatimentPV::Habitation : BatimentPV::Tertiaire;
  i.nom = "Installation photovoltaïque de " + nombreFr(p, 1) + " kWc";
  return i;
}

}  // namespace prom::elec
