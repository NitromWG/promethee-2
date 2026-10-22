// Prométhée : calculs de la CAO électrique — courants admissibles des conducteurs en cuivre (CEI 60364-5-52,
// annexe B, tableaux B.52.2 à B.52.5 et B.52.10, B.52.12 pour la pose E), facteurs de correction de température
// (B.52.14) et de groupement (B.52.17), chute de tension, choix de la section, bilan de puissance.
// Licence GPL-3.0-only.
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"

namespace prom::elec {

namespace {

// Sections du tableau, mm²
constexpr std::array<double, 15> SECTIONS = {1.5, 2.5, 4, 6, 10, 16, 25, 35, 50, 70, 95, 120, 150, 185, 240};
// Courants admissibles (A), cuivre, 30 °C dans l'air (20 °C dans le sol pour D1), par méthode de pose A1, A2, B1, B2,
// C, E, D1 (ligne = section)
using Table = std::array<std::array<double, 7>, 15>;
// PVC, 2 conducteurs chargés (B.52.2 ; E : B.52.10 ; D1 : enterré sous conduit, sol à 2,5 K·m/W)
constexpr Table PVC2 = {{{14.5, 14, 17.5, 16.5, 19.5, 22, 22},
                         {19.5, 18.5, 24, 23, 27, 30, 29},
                         {26, 25, 32, 30, 36, 40, 37},
                         {34, 32, 41, 38, 46, 51, 46},
                         {46, 43, 57, 52, 63, 70, 60},
                         {61, 57, 76, 69, 85, 94, 78},
                         {80, 75, 101, 90, 112, 119, 99},
                         {99, 92, 125, 111, 138, 148, 119},
                         {119, 110, 151, 133, 168, 180, 140},
                         {151, 139, 192, 168, 213, 232, 173},
                         {182, 167, 232, 201, 258, 282, 204},
                         {210, 192, 269, 232, 299, 328, 231},
                         {240, 219, 300, 258, 344, 379, 261},
                         {273, 248, 341, 294, 392, 434, 292},
                         {321, 291, 400, 344, 461, 514, 336}}};
// PVC, 3 conducteurs chargés (B.52.4 ; E : B.52.10)
constexpr Table PVC3 = {{{13.5, 13, 15.5, 15, 17.5, 18.5, 18},
                         {18, 17.5, 21, 20, 24, 25, 24},
                         {24, 23, 28, 27, 32, 34, 30},
                         {31, 29, 36, 34, 41, 43, 38},
                         {42, 39, 50, 46, 57, 60, 50},
                         {56, 52, 68, 62, 76, 80, 64},
                         {73, 68, 89, 80, 96, 101, 82},
                         {89, 83, 110, 99, 119, 126, 98},
                         {108, 99, 134, 118, 144, 153, 116},
                         {136, 125, 171, 149, 184, 196, 143},
                         {164, 150, 207, 179, 223, 238, 169},
                         {188, 172, 239, 206, 259, 276, 192},
                         {216, 196, 262, 225, 299, 319, 217},
                         {245, 223, 296, 255, 341, 364, 243},
                         {286, 261, 346, 297, 403, 430, 280}}};
// PR (XLPE, EPR), 2 conducteurs chargés (B.52.3 ; E : B.52.12)
constexpr Table PR2 = {{{19, 18.5, 23, 22, 24, 26, 25},
                        {26, 25, 31, 30, 33, 36, 33},
                        {35, 33, 42, 40, 45, 49, 43},
                        {45, 42, 54, 51, 58, 63, 53},
                        {61, 57, 75, 69, 80, 86, 71},
                        {81, 76, 100, 91, 107, 115, 91},
                        {106, 99, 133, 119, 138, 149, 116},
                        {131, 121, 164, 146, 171, 185, 139},
                        {158, 145, 198, 175, 209, 225, 164},
                        {200, 183, 253, 221, 269, 289, 203},
                        {241, 220, 306, 265, 328, 352, 239},
                        {278, 253, 354, 305, 382, 410, 271},
                        {318, 290, 407, 349, 441, 473, 306},
                        {362, 329, 464, 395, 506, 542, 343},
                        {424, 386, 546, 462, 599, 641, 395}}};
// PR, 3 conducteurs chargés (B.52.5 ; E : B.52.12)
constexpr Table PR3 = {{{17, 16.5, 20, 19.5, 22, 23, 21},
                        {23, 22, 28, 26, 30, 32, 28},
                        {31, 30, 37, 35, 40, 42, 36},
                        {40, 38, 48, 44, 52, 54, 44},
                        {54, 51, 66, 60, 71, 75, 58},
                        {73, 68, 88, 80, 96, 100, 75},
                        {95, 89, 117, 105, 119, 127, 96},
                        {117, 109, 144, 128, 147, 158, 115},
                        {141, 130, 175, 154, 179, 192, 135},
                        {179, 164, 222, 194, 229, 246, 167},
                        {216, 197, 269, 233, 278, 298, 197},
                        {249, 227, 312, 268, 322, 346, 223},
                        {285, 259, 342, 300, 371, 399, 251},
                        {324, 295, 384, 340, 424, 456, 281},
                        {380, 346, 450, 398, 500, 538, 324}}};

int indiceSection(double s) {
  for (std::size_t i = 0; i < SECTIONS.size(); ++i)
    if (std::abs(SECTIONS[i] - s) < 1e-6) return static_cast<int>(i);
  return -1;
}

}  // namespace

const char* nomMethodePose(MethodePose m) {
  switch (m) {
    case MethodePose::A1: return "A1 — conducteurs isolés sous conduit dans une paroi isolante";
    case MethodePose::A2: return "A2 — câble multiconducteur sous conduit dans une paroi isolante";
    case MethodePose::B1: return "B1 — conducteurs isolés sous conduit ou goulotte sur paroi";
    case MethodePose::B2: return "B2 — câble multiconducteur sous conduit ou goulotte sur paroi";
    case MethodePose::C: return "C — câble fixé sur une paroi";
    case MethodePose::E: return "E — câble multiconducteur à l'air libre (chemin de câbles perforé)";
    case MethodePose::D1: return "D1 — câble enterré sous conduit (sol à 20 °C)";
  }
  return "";
}

const std::vector<double>& sectionsNormalisees() {
  static const std::vector<double> v(SECTIONS.begin(), SECTIONS.end());
  return v;
}

const std::vector<double>& calibresNormalises() {
  static const std::vector<double> v = {2, 3, 6, 10, 13, 16, 20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 250};
  return v;
}

double facteurTemperature(Isolant i, double temperature) {
  // k = √((θmax − θa) / (θmax − 30)) : la formule dont sont tirées les valeurs du tableau B.52.14
  const double tmax = i == Isolant::PVC ? 70.0 : 90.0;
  if (!std::isfinite(temperature) || temperature >= tmax) return 0;
  const double k = std::sqrt((tmax - temperature) / (tmax - 30.0));
  return std::round(k * 100.0) / 100.0;
}

double facteurGroupement(int circuits) {
  // Tableau B.52.17, cas 1 (circuits jointifs, en faisceau sur une paroi, encastrés ou sous conduit)
  static const std::vector<std::pair<int, double>> t = {{1, 1.0}, {2, 0.80}, {3, 0.70}, {4, 0.65}, {5, 0.60}, {6, 0.57}, {7, 0.54},
                                                        {8, 0.52}, {9, 0.50}, {12, 0.45}, {16, 0.41}, {20, 0.38}};
  if (circuits <= 1) return 1.0;
  for (const auto& [n, f] : t)
    if (circuits <= n) return f;
  return 0.38;
}

double courantAdmissible(double section, MethodePose m, Isolant i, int conducteursCharges, double temperature, int circuitsGroupes) {
  const int l = indiceSection(section);
  if (l < 0) return 0;
  const bool trois = conducteursCharges >= 3;
  const Table& t = i == Isolant::PVC ? (trois ? PVC3 : PVC2) : (trois ? PR3 : PR2);
  const double base = t[static_cast<std::size_t>(l)][static_cast<std::size_t>(m)];
  // Pose enterrée : température du sol, référence 20 °C (B.52.15) ; sinon air, référence 30 °C (B.52.14)
  double kt = facteurTemperature(i, temperature);
  if (m == MethodePose::D1) {
    const double tmax = i == Isolant::PVC ? 70.0 : 90.0;
    kt = std::isfinite(temperature) && temperature < tmax ? std::round(std::sqrt((tmax - temperature) / (tmax - 20.0)) * 100.0) / 100.0 : 0.0;
  }
  const double iz = base * kt * facteurGroupement(circuitsGroupes);
  return std::round(iz * 10.0) / 10.0;
}

ChuteTension chuteTension(double courant, double longueur, double section, bool triphase, double cosPhi, double tension, bool eclairage) {
  ChuteTension r;
  if (section <= 0 || tension <= 0) return r;
  constexpr double rho = 0.0225;     // Ω·mm²/m, cuivre en service normal (NF C 15-100, annexe G)
  constexpr double lambda = 0.00008; // Ω/m (0,08 mΩ/m)
  const double c = std::clamp(cosPhi, 0.0, 1.0), s = std::sqrt(1.0 - c * c);
  const double b = triphase ? 1.0 : 2.0;
  r.volts = b * (rho * longueur / section * c + lambda * longueur * s) * courant;
  const double u0 = triphase ? tension / std::sqrt(3.0) : tension;
  r.pourcent = 100.0 * r.volts / u0;
  r.admissible = r.pourcent <= (eclairage ? 3.0 : 5.0) + 1e-9;
  return r;
}

ChoixSection choisirSection(double courantEmploi, double longueur, bool triphase, double cosPhi, MethodePose m, Isolant i, double temperature, int circuitsGroupes,
                            bool eclairage, double tension) {
  ChoixSection r;
  for (double in : calibresNormalises())
    if (in + 1e-9 >= courantEmploi) {
      r.calibre = in;
      break;
    }
  if (r.calibre <= 0) {
    r.justification = "Courant d'emploi au-delà de 250 A : hors du champ de ce calcul.";
    return r;
  }
  const double limite = eclairage ? 3.0 : 5.0;
  for (double s : SECTIONS) {
    const double iz = courantAdmissible(s, m, i, triphase ? 3 : 2, temperature, circuitsGroupes);
    if (iz + 1e-9 < r.calibre) continue;
    const ChuteTension ct = chuteTension(courantEmploi, longueur, s, triphase, cosPhi, tension, eclairage);
    if (!ct.admissible) continue;
    r.section = s;
    r.courantAdmissible = iz;
    r.chute = ct;
    r.justification = "In " + detail::nombreFr(r.calibre) + " A ≥ Ib " + detail::nombreFr(courantEmploi, 1) + " A ; Iz " + detail::nombreFr(iz, 1) + " A ≥ In (pose " +
                      std::string(nomMethodePose(m)).substr(0, 2) + (i == Isolant::PVC ? ", PVC" : ", PR") + ", " + detail::nombreFr(temperature) + " °C, " +
                      std::to_string(std::max(1, circuitsGroupes)) + " circuit(s)) ; chute de tension " + detail::nombreFr(ct.pourcent, 2) + " % ≤ " + detail::nombreFr(limite) +
                      " %";
    return r;
  }
  r.justification = "Aucune section jusqu'à 240 mm² ne convient (courant ou longueur trop grands) : fractionne le circuit ou rapproche la source.";
  return r;
}

BilanPuissance bilanPuissance(const ProjetElectrique& p, double cosPhi) {
  BilanPuissance b;
  for (const auto& c : p.schema.composants) {
    const double kw = detail::puissanceDe(c);
    if (kw <= 0) continue;
    // Transformateurs et alimentations ne consomment pas : ils transmettent (leur charge est déjà comptée)
    if (c.symbole == "iec:transfo-1" || c.symbole == "iec:transfo-3" || c.symbole == "iec:alim-dc" || c.symbole == "iec:variateur" || c.symbole == "iec:demarreur") continue;
    LigneBilan l;
    l.repere = repereAffiche(p, c);
    l.designation = c.description.empty() ? c.valeur : c.description;
    l.puissance = kw;
    // Facteur d'utilisation usuel : 0,75 pour un moteur, 1 pour l'éclairage et le chauffage
    l.ku = c.symbole.find("moteur") != std::string::npos || c.symbole == "iec:ventilateur" ? 0.75 : 1.0;
    if (const double ku = detail::lireNombre(detail::champ(c, "ku")); ku > 0) l.ku = ku;
    b.lignes.push_back(l);
  }
  // Facteur de simultanéité selon le nombre de départs (CEI 61439, facteur de diversité assigné)
  const std::size_t n = b.lignes.size();
  const double ks = n <= 1 ? 1.0 : n <= 3 ? 0.9 : n <= 5 ? 0.8 : n <= 9 ? 0.7 : 0.6;
  for (auto& l : b.lignes) {
    l.ks = ks;
    b.installee += l.puissance;
    b.absorbee += l.absorbee();
  }
  std::stable_sort(b.lignes.begin(), b.lignes.end(), [](const LigneBilan& x, const LigneBilan& y) { return ecad::ordreNaturel(x.repere, y.repere); });
  const double c = std::clamp(cosPhi, 0.1, 1.0);
  const bool triphase = p.reglages.tension > 300;
  const double u = p.reglages.tension > 0 ? p.reglages.tension : 400;
  b.courant = triphase ? b.absorbee * 1000.0 / (std::sqrt(3.0) * u * c) : b.absorbee * 1000.0 / (u * c);
  return b;
}

}  // namespace prom::elec
