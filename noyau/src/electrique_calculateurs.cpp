// Prométhée : boîte à calculs de l'électricien. Formules et règles : NF C 15-100 (CEI 60364), guide UTE C 15-105
// (courts-circuits, longueurs protégées), CEI 60269 (fusibles gG), CEI/TR 60890 (échauffement des enveloppes), EN
// 12464-1 (éclairements usuels), règlement de sécurité ERP (éclairage de sécurité). Licence GPL-3.0-only.
#include "promethee/lignes_transmission.hpp"
#include "promethee/electrique_calculateurs.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "electrique_interne.hpp"
#include "promethee/electrique_fabrication.hpp"

namespace prom::elec {

using detail::nombreFr;

namespace {

using V = std::vector<double>;
const double RACINE3 = std::sqrt(3.0);
const double PI = 3.14159265358979323846;

std::string n(double v, int d = 2) { return nombreFr(v, d); }
ChampCalcul nombre(const std::string& id, const std::string& nom, const std::string& unite, double valeur, double min, double max, int decimales, const std::string& aide = {}) {
  ChampCalcul c;
  c.id = id;
  c.nom = nom;
  c.unite = unite;
  c.valeur = valeur;
  c.min = min;
  c.max = max;
  c.decimales = decimales;
  c.aide = aide;
  return c;
}
ChampCalcul liste(const std::string& id, const std::string& nom, std::vector<std::string> choix, int defaut, const std::string& aide = {}) {
  ChampCalcul c;
  c.id = id;
  c.nom = nom;
  c.choix = std::move(choix);
  c.valeur = defaut;
  c.min = 0;
  c.max = static_cast<double>(c.choix.size()) - 1;
  c.decimales = 0;
  c.aide = aide;
  return c;
}
Calculateur fiche(const std::string& id, const std::string& nom, const std::string& famille, const std::string& description, const std::string& regle) {
  Calculateur c;
  c.id = id;
  c.nom = nom;
  c.famille = famille;
  c.description = description;
  c.regle = regle;
  return c;
}
std::vector<std::string> nomsSections() {
  std::vector<std::string> r;
  for (double s : sectionsNormalisees()) r.push_back(nombreFr(s) + " mm²");
  return r;
}
int indiceSection(double s) {
  const auto& t = sectionsNormalisees();
  for (std::size_t i = 0; i < t.size(); ++i)
    if (std::abs(t[i] - s) < 1e-9) return static_cast<int>(i);
  return 0;
}
double section(double indice) {
  const auto& t = sectionsNormalisees();
  return t[std::clamp<std::size_t>(static_cast<std::size_t>(std::lround(indice)), 0, t.size() - 1)];
}
double sectionAuDessus(double s) {
  for (double x : sectionsNormalisees())
    if (x + 1e-9 >= s) return x;
  return 0;
}
int choixDe(double v) { return static_cast<int>(std::lround(v)); }
AvisCalcul avis(NiveauAvis niveau, const std::string& regle, const std::string& message) { return {niveau, regle, message}; }
const std::vector<std::string> AMES = {"Cuivre", "Aluminium"};
double rho0(int ame) { return ame == 0 ? 0.01851 : 0.02941; }        // Ω·mm²/m à 20 °C
double rhoService(int ame) { return ame == 0 ? 0.0225 : 0.036; }     // en service (NF C 15-100, annexe G)
double coefK(int ame, bool pr) { return ame == 0 ? (pr ? 143 : 115) : (pr ? 94 : 76); }
const std::vector<double> KVA = {100, 160, 250, 400, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150};

std::vector<Calculateur> construire() {
  std::vector<Calculateur> v;
  const std::string CABLES = "Câbles et protections", RESEAU = "Puissance et réseau", TERRE = "Terre et sécurité", ECLAIRAGE = "Éclairage", BASES = "Bases et conversions";

  // ----- Section d'un câble -----
  {
    Calculateur c = fiche("section", "Section d’un câble", CABLES,
                  "La plus petite section qui porte le courant sans chauffer et sans trop de chute de tension, et le calibre qui la protège.",
                  "NF C 15-100, parties 4-43 et 5-52 (CEI 60364-5-52, annexe B)");
    std::vector<std::string> poses;
    for (MethodePose m : {MethodePose::A1, MethodePose::A2, MethodePose::B1, MethodePose::B2, MethodePose::C, MethodePose::E, MethodePose::D1}) poses.push_back(nomMethodePose(m));
    c.champs = {nombre("ib", "Courant d’emploi Ib", "A", 16, 0.1, 250, 1, "le courant du circuit en service normal"),
                nombre("longueur", "Longueur du câble", "m", 25, 0.1, 2000, 1),
                liste("reseau", "Réseau", {"Monophasé 230 V", "Triphasé 400 V"}, 0),
                nombre("cosphi", "cos φ", "", 0.9, 0.1, 1, 2),
                liste("pose", "Mode de pose", poses, 2),
                liste("isolant", "Isolant", {"PVC (70 °C)", "PR, U1000 R2V (90 °C)"}, 1),
                nombre("temperature", "Température ambiante (sol pour D1)", "°C", 30, 10, 60, 0),
                nombre("groupes", "Circuits groupés", "", 1, 1, 20, 0, "circuits posés côte à côte"),
                liste("usage", "Usage", {"Éclairage (chute ≤ 3 %)", "Autres usages (chute ≤ 5 %)"}, 1)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const bool tri = choixDe(x[2]) == 1, eclairage = choixDe(x[8]) == 0;
      const ChoixSection s = choisirSection(x[0], x[1], tri, x[3], static_cast<MethodePose>(choixDe(x[4])), choixDe(x[5]) == 1 ? Isolant::PR : Isolant::PVC, x[6],
                                            choixDe(x[7]), eclairage, tri ? 400 : 230);
      r.formule = "In ≥ Ib ; Iz = I₀ × kθ × kgroupement ≥ In ; ΔU = b (ρ L / S × cos φ + λ L × sin φ) × Ib ≤ " + std::string(eclairage ? "3" : "5") + " %";
      if (s.section <= 0) {
        r.avis.push_back(avis(NiveauAvis::Erreur, "NF C 15-100 433.1", s.justification));
        r.explication = "Aucune section courante ne convient : partage le circuit ou rapproche le tableau.";
        return r;
      }
      r.lignes = {{"Section", n(s.section, -1) + " mm²", true},
                  {"Protection", n(s.calibre, -1) + " A"},
                  {"Courant admissible Iz", n(s.courantAdmissible, 1) + " A"},
                  {"Chute de tension", n(s.chute.pourcent) + " % (" + n(s.chute.volts) + " V)"}};
      r.explication = "Un câble de " + n(s.section, -1) + " mm², protégé par un disjoncteur de " + n(s.calibre, -1) + " A, porte " + n(x[0], 1) +
                      " A sans chauffer et sans trop de chute de tension. " + s.justification + ".";
      r.avis.push_back(avis(NiveauAvis::Info, "NF C 15-100", "Vérifie aussi le pouvoir de coupure et la protection des personnes au point de pose (onglet Distribution)."));
      return r;
    };
    v.push_back(c);
  }
  // ----- Chute de tension -----
  {
    Calculateur c = fiche("chute", "Chute de tension", CABLES, "La tension perdue dans un câble, et la longueur au-delà de laquelle elle devient trop forte.",
                  "NF C 15-100, 525 et annexe G");
    c.champs = {nombre("courant", "Courant", "A", 20, 0.1, 5000, 1),
                nombre("longueur", "Longueur", "m", 30, 0.1, 10000, 1),
                liste("section", "Section", nomsSections(), indiceSection(2.5)),
                liste("ame", "Âme", AMES, 0),
                liste("reseau", "Réseau", {"Monophasé 230 V", "Triphasé 400 V"}, 0),
                nombre("cosphi", "cos φ", "", 0.9, 0.1, 1, 2),
                liste("limite", "Limite", {"3 % (éclairage, réseau public)", "5 % (autres usages, réseau public)", "6 % (éclairage, poste privé)", "8 % (autres usages, poste privé)"}, 1)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double I = x[0], L = x[1], S = section(x[2]), cphi = std::clamp(x[5], 0.0, 1.0), sphi = std::sqrt(1 - cphi * cphi);
      const bool mono = choixDe(x[4]) == 0;
      const double b = mono ? 2 : 1, u0 = 230.94, rho = rhoService(choixDe(x[3])), lambda = 0.00008;
      const double parMetre = b * (rho / S * cphi + lambda * sphi) * I;
      const double du = parMetre * L, pct = 100 * du / u0;
      const double limite = std::array<double, 4>{3, 5, 6, 8}[static_cast<std::size_t>(std::clamp(choixDe(x[6]), 0, 3))];
      const double lmax = parMetre > 0 ? limite / 100 * u0 / parMetre : 0;
      r.lignes = {{"Chute de tension", n(pct) + " %", true}, {"En volts", n(du) + " V"}, {"Longueur maximale à " + n(limite, 0) + " %", n(lmax, 0) + " m"}};
      r.formule = "ΔU = b (ρ L / S × cos φ + λ L × sin φ) × I ; b = " + std::string(mono ? "2 (monophasé)" : "1 (triphasé)") + ", ρ = " + n(rho, 4) + " Ω·mm²/m, λ = 0,08 mΩ/m ; ΔU % = 100 ΔU / U0";
      r.explication = pct <= limite + 1e-9 ? "La chute reste sous " + n(limite, 0) + " % : les appareils reçoivent une tension correcte."
                                           : "Au-delà de " + n(limite, 0) + " % : passe à " + n(sectionAuDessus(S * pct / limite), -1) +
                                                 " mm² (la chute baisse comme la section augmente) ou raccourcis la ligne.";
      r.avis.push_back(pct <= limite + 1e-9 ? avis(NiveauAvis::Info, "NF C 15-100 525", "Chute de tension admissible.")
                                            : avis(NiveauAvis::Erreur, "NF C 15-100 525", "Chute de tension de " + n(pct) + " % au-delà de " + n(limite, 0) + " %."));
      return r;
    };
    v.push_back(c);
  }
  // ----- Courant d'emploi et puissances -----
  {
    Calculateur c = fiche("courant", "Courant et puissances", RESEAU, "Le courant qu’appelle un récepteur, et ses puissances active, réactive et apparente.",
                  "Bilan de puissance");
    c.champs = {nombre("puissance", "Puissance utile", "kW", 10, 0.001, 100000, 3, "moteur : la puissance sur l’arbre (plaque)"),
                liste("reseau", "Réseau", {"Monophasé 230 V", "Triphasé 400 V", "Triphasé 230 V"}, 1),
                nombre("cosphi", "cos φ", "", 0.85, 0.1, 1, 2, "résistances 1 ; moteurs 0,8 à 0,85 ; LED 0,9 à 0,95"),
                nombre("rendement", "Rendement", "", 1, 0.1, 1, 2, "moteur : 0,85 à 0,95 ; 1 pour une résistance")};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const int reseau = choixDe(x[1]);
      const double cphi = x[2], pa = x[0] / x[3], s = pa / cphi, q = s * std::sqrt(std::max(0.0, 1 - cphi * cphi));
      const double u = reseau == 1 ? 400 : 230;
      const double i = reseau == 0 ? s * 1000 / u : s * 1000 / (RACINE3 * u);
      r.lignes = {{"Courant d’emploi Ib", n(i, 1) + " A", true},
                  {"Puissance absorbée", n(pa, 2) + " kW"},
                  {"Puissance apparente", n(s, 2) + " kVA"},
                  {"Puissance réactive", n(q, 2) + " kvar"},
                  {"En chevaux", n(x[0] / 0.736, 1) + " ch"}};
      r.formule = reseau == 0 ? "I = P / (η × U × cos φ)" : "I = P / (η × √3 × U × cos φ)";
      r.explication = "Ce récepteur appelle " + n(i, 1) + " A : c’est ce courant qui choisit le calibre de sa protection et la section de son câble.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Court-circuit en bout de ligne -----
  {
    Calculateur c = fiche("icc", "Court-circuit en bout de ligne", CABLES,
                  "Les courants de court-circuit maximal et minimal au bout d’un câble, à partir de celui du tableau ; la protection déclenche-t-elle ?",
                  "UTE C 15-105 (méthode des impédances), NF C 15-100 434");
    c.champs = {nombre("icc", "Icc au départ du câble", "kA", 10, 0.1, 200, 1, "courant de court-circuit au tableau qui alimente le câble"),
                nombre("longueur", "Longueur", "m", 50, 0.1, 5000, 1),
                liste("section", "Section", nomsSections(), indiceSection(2.5)),
                liste("ame", "Âme", AMES, 0),
                liste("reseau", "Circuit", {"Triphasé + neutre", "Triphasé sans neutre", "Monophasé"}, 0),
                liste("protection", "Protection", {"Disjoncteur courbe B", "Disjoncteur courbe C", "Disjoncteur courbe D", "Fusible gG"}, 1),
                nombre("calibre", "Calibre", "A", 16, 1, 630, 0)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double u0 = 230.94, cm = 1.05 * 1.05, cmin = 0.95 * 1.05;
      const double L = x[1], S = section(x[2]);
      const int ame = choixDe(x[3]), circuit = choixDe(x[4]), prot = choixDe(x[5]);
      const double in = x[6];
      const bool fusible = prot == 3;
      // Amont : impédance tirée de l'Icc donné, partagée comme celle d'un transformateur (R = 0,31 Z, X = 0,95 Z)
      const double za = cm * u0 / (x[0] * 1000), ra = 0.31 * za, xa = 0.95 * za;
      const double r0 = rho0(ame) * L / S, xc = 0.00008 * L, f = fusible ? 1.5 : 1.25;
      double ikMax = 0, ikMin = 0;
      if (circuit == 2) {
        // Monophasé : l'Icc donné est entre phase et neutre ; phase et neutre du câble en série
        ikMax = cm * u0 / std::hypot(ra + 2 * r0, xa + 2 * xc) / 1000;
        ikMin = cmin * u0 / std::hypot(ra + 2 * f * r0, xa + 2 * xc) / 1000;
      } else {
        ikMax = cm * u0 / std::hypot(ra + r0, xa + xc) / 1000;
        ikMin = circuit == 0 ? cmin * u0 / std::hypot(ra + 2 * f * r0, xa + 2 * xc) / 1000 : RACINE3 / 2 * cmin * u0 / std::hypot(ra + f * r0, xa + xc) / 1000;
      }
      double seuil = 0;
      if (fusible) seuil = courantFusionGG(in, 5);
      else seuil = std::array<double, 3>{5, 10, 20}[static_cast<std::size_t>(prot)] * in;
      r.lignes = {{"Court-circuit maximal en bout", n(ikMax, ikMax < 10 ? 2 : 1) + " kA", true},
                  {"Court-circuit minimal en bout", n(ikMin, ikMin < 10 ? 2 : 1) + " kA", true},
                  {fusible ? "Fusion en 5 s à partir de" : "Déclenchement instantané à partir de", seuil > 0 ? n(seuil, 0) + " A" : std::string("hors gamme gG")}};
      r.formule = "Ik = c × m × U0 / √(ΣR² + ΣX²) ; c = 1,05 (maximal) ou 0,95 (minimal), m = 1,05 ; R = ρ L / S, ρ0 = " + n(rho0(ame), 5) +
                  " Ω·mm²/m (× " + n(f, 2) + " pour le minimal), X = 0,08 mΩ/m";
      const bool ok = seuil > 0 && ikMin * 1000 >= seuil;
      r.explication = ok ? "Même au bout de la ligne, le court-circuit fait déclencher la protection : la ligne est protégée jusqu’au bout."
                         : "Au bout de la ligne, le court-circuit est trop faible pour faire déclencher la protection rapidement : raccourcis la ligne, augmente la section ou prends une courbe plus basse.";
      r.avis.push_back(ok ? avis(NiveauAvis::Info, "NF C 15-100 434.2", "Le court-circuit minimal fait déclencher la protection.")
                          : avis(NiveauAvis::Erreur, "NF C 15-100 434.2", "Court-circuit minimal (" + n(ikMin * 1000, 0) + " A) sous le seuil de la protection (" + n(seuil, 0) + " A)."));
      r.avis.push_back(avis(NiveauAvis::Info, "NF C 15-100 434.5.1", "Le pouvoir de coupure des appareils posés au bout de ce câble doit dépasser " + n(ikMax, ikMax < 10 ? 2 : 1) + " kA."));
      return r;
    };
    v.push_back(c);
  }
  // ----- Longueur maximale protégée -----
  {
    Calculateur c = fiche("lmax", "Longueur maximale protégée", CABLES,
                  "La plus grande longueur de câble pour laquelle un défaut à la masse fait encore déclencher la protection (schémas TN et IT).",
                  "UTE C 15-105, méthode conventionnelle ; NF C 15-100 411.4 et 411.6");
    std::vector<std::string> pe = {"Même section que la phase"};
    for (const auto& t : nomsSections()) pe.push_back(t);
    c.champs = {liste("section", "Section de phase", nomsSections(), indiceSection(2.5)),
                liste("pe", "Section du conducteur de protection", pe, 0),
                liste("ame", "Âme", AMES, 0),
                liste("protection", "Protection",
                      {"Disjoncteur courbe B", "Disjoncteur courbe C", "Disjoncteur courbe D", "Fusible gG, circuit terminal (0,4 s)", "Fusible gG, distribution (5 s)"}, 1),
                nombre("calibre", "Calibre", "A", 16, 1, 630, 0),
                liste("regime", "Schéma", {"TN", "IT, neutre distribué", "IT, sans neutre"}, 0)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double S = section(x[0]), spe = choixDe(x[1]) == 0 ? S : section(x[1] - 1);
      const int ame = choixDe(x[2]), prot = choixDe(x[3]), regime = choixDe(x[5]);
      const double in = x[4];
      double ia = 0;
      if (prot <= 2) ia = std::array<double, 3>{5, 10, 20}[static_cast<std::size_t>(prot)] * in;
      else ia = courantFusionGG(in, prot == 3 ? 0.1 : 5);
      double corr = 1;
      if (S >= 300) corr = 1.30;
      else if (S >= 240) corr = 1.25;
      else if (S >= 185) corr = 1.20;
      else if (S >= 150) corr = 1.15;
      const double rho1 = (prot >= 3 ? 1.5 : 1.25) * rho0(ame) * corr, m = S / spe;
      double lmax = ia > 0 ? 0.8 * 230.94 * S / (rho1 * (1 + m) * ia) : 0;
      if (regime == 1) lmax *= 0.5;
      if (regime == 2) lmax *= RACINE3 / 2;
      r.lignes = {{"Longueur maximale", n(lmax, 0) + " m", true}, {"Courant de déclenchement Ia", ia > 0 ? n(ia, 0) + " A" : std::string("hors gamme gG")}, {"Rapport m = Sph / SPE", n(m)}};
      r.formule = "Lmax = 0,8 × U0 × Sph / (ρ1 × (1 + m) × Ia)" + std::string(regime == 1 ? " × ½ (IT, neutre distribué)" : regime == 2 ? " × √3/2 (IT, sans neutre)" : "") +
                  " ; ρ1 = " + n(rho1, 4) + " Ω·mm²/m" + (corr > 1 ? " (réactance des grosses sections comprise)" : "");
      r.explication = "Au-delà de " + n(lmax, 0) +
                      " m, un défaut à la masse ne fait plus déclencher la protection assez vite : prends une section plus forte (ou un PE plus gros), une courbe plus basse, ou un différentiel.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Contrainte thermique -----
  {
    Calculateur c = fiche("thermique", "Contrainte thermique", CABLES, "Le câble supporte-t-il l’énergie d’un court-circuit jusqu’à ce que la protection coupe ?", "NF C 15-100 434.5.2");
    c.champs = {liste("section", "Section", nomsSections(), indiceSection(2.5)),
                liste("ame", "Âme", AMES, 0),
                liste("isolant", "Isolant", {"PVC", "PR"}, 1),
                nombre("icc", "Courant de court-circuit", "kA", 6, 0.01, 200, 2),
                nombre("duree", "Durée jusqu’à la coupure", "s", 0.01, 0.001, 5, 3, "0,01 s pour un disjoncteur à déclenchement instantané")};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double S = section(x[0]), k = coefK(choixDe(x[1]), choixDe(x[2]) == 1), I = x[3] * 1000, t = x[4];
      const double i2t = I * I * t, adm = k * k * S * S, tadm = std::pow(k * S / I, 2), smin = I * std::sqrt(t) / k;
      r.lignes = {{"Énergie du court-circuit I²t", n(i2t / 1e6, 4) + " MA²s"},
                  {"Ce que supporte le câble k²S²", n(adm / 1e6, 4) + " MA²s"},
                  {"Durée admissible", n(tadm, 3) + " s", true},
                  {"Section minimale", n(sectionAuDessus(smin), -1) + " mm² (" + n(smin, 2) + " mm² calculés)"}};
      r.formule = "I²t ≤ k² S² ; t admissible = (k S / I)² ; k = " + n(k, 0);
      const bool ok = i2t <= adm;
      r.explication = ok ? "Le câble supporte ce court-circuit : son isolant ne se dégrade pas." : "Le câble chauffe trop avant la coupure : section plus forte ou protection plus rapide (ou limiteuse).";
      r.avis.push_back(ok ? avis(NiveauAvis::Info, "NF C 15-100 434.5.2", "Contrainte thermique vérifiée.")
                          : avis(NiveauAvis::Erreur, "NF C 15-100 434.5.2", "Contrainte thermique dépassée."));
      return r;
    };
    v.push_back(c);
  }
  // ----- Conducteur de protection -----
  {
    Calculateur c = fiche("pe", "Conducteur de protection (PE)", CABLES, "La section du conducteur vert-jaune : par le tableau simplifié, et par le calcul de la contrainte thermique.",
                  "NF C 15-100 543.1 (tableau 54F)");
    c.champs = {liste("section", "Section des phases", nomsSections(), indiceSection(16)),
                liste("ame", "Âme", AMES, 0),
                liste("isolant", "Isolant", {"PVC", "PR"}, 1),
                nombre("ief", "Courant de défaut à la masse", "kA", 2, 0.01, 100, 2),
                nombre("duree", "Temps de coupure", "s", 0.2, 0.001, 5, 3)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double S = section(x[0]), k = coefK(choixDe(x[1]), choixDe(x[2]) == 1), I = x[3] * 1000, t = x[4];
      const double tableau = S <= 16 ? S : S <= 35 ? 16 : sectionAuDessus(S / 2);
      const double adia = I * std::sqrt(t) / k, retenue = std::max(tableau, sectionAuDessus(adia));
      r.lignes = {{"Section du PE", n(retenue, -1) + " mm²", true}, {"Tableau 54F", n(tableau, -1) + " mm²"}, {"Contrainte thermique", n(adia, 2) + " mm² au moins"}};
      r.formule = "Tableau 54F : S ≤ 16 → S ; 16 < S ≤ 35 → 16 ; S > 35 → S/2.   Calcul : SPE ≥ √(I² t) / k, k = " + n(k, 0);
      r.explication = "Le conducteur vert-jaune fait " + n(retenue, -1) + " mm² : assez pour écouler le défaut jusqu’à la coupure.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Compensation -----
  {
    Calculateur c = fiche("compensation", "Compensation de l’énergie réactive", RESEAU, "La batterie de condensateurs qui relève le cos φ, et le courant qu’elle fait gagner.",
                  "Compensation (tan φ)");
    c.champs = {nombre("puissance", "Puissance active", "kW", 100, 0.1, 100000, 1), nombre("cosphi", "cos φ actuel", "", 0.8, 0.1, 1, 2),
                nombre("cible", "cos φ visé", "", 0.93, 0.5, 1, 2, "0,93 évite les pénalités d’énergie réactive (tan φ 0,4)"), nombre("tension", "Tension", "V", 400, 100, 1000, 0)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double p = x[0], c1 = x[1], c2 = x[2], u = x[3];
      const double t1 = std::sqrt(1 - c1 * c1) / c1, t2 = std::sqrt(1 - c2 * c2) / c2;
      const double qc = std::max(0.0, p * (t1 - t2)), qcArrondi = std::ceil(qc / 5) * 5;
      const double i1 = p * 1000 / (RACINE3 * u * c1), i2 = p * 1000 / (RACINE3 * u * c2);
      r.lignes = {{"Batterie de condensateurs", n(qcArrondi, 0) + " kvar", true}, {"Calculé", n(qc, 1) + " kvar"}, {"Courant avant", n(i1, 1) + " A"},
                  {"Courant après", n(i2, 1) + " A"}, {"Gain sur le courant", n(100 * (1 - i2 / i1), 0) + " %"}};
      r.formule = "Qc = P × (tan φ1 − tan φ2) ; tan φ1 = " + n(t1, 3) + ", tan φ2 = " + n(t2, 3);
      r.explication = qc > 0 ? "Les condensateurs fournissent sur place l’énergie réactive des moteurs : le courant baisse, les câbles et le transformateur respirent."
                             : "Le cos φ atteint déjà la cible : pas de compensation nécessaire.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Transformateur -----
  {
    Calculateur c = fiche("transformateur", "Choix du transformateur", RESEAU, "La puissance normalisée du transformateur HTA/BT, son courant et son courant de court-circuit.",
                  "UTE C 15-105 (impédances), NF EN 60076");
    c.champs = {nombre("appel", "Puissance appelée", "kVA", 300, 1, 10000, 0), nombre("reserve", "Réserve", "%", 25, 0, 200, 0), nombre("tension", "Tension secondaire", "V", 400, 200, 1000, 0)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double besoin = x[0] * (1 + x[1] / 100), u = x[2];
      double sn = 0;
      for (double k : KVA)
        if (k + 1e-9 >= besoin) {
          sn = k;
          break;
        }
      if (sn <= 0) {
        r.avis.push_back(avis(NiveauAvis::Erreur, "Installation", "Plus de 3 150 kVA : plusieurs transformateurs."));
        return r;
      }
      const double ucc = sn <= 630 ? 4 : 6, m = 1.05;
      const double zq = (m * u) * (m * u) / 500e6, xq = 0.995 * zq, rq = 0.1 * xq;
      const double zt = (m * u) * (m * u) / (sn * 1000) * ucc / 100, rt = 0.31 * zt, xt = 0.95 * zt;
      const double ik = 1.05 * m * u / RACINE3 / std::hypot(rq + rt, xq + xt) / 1000;
      r.lignes = {{"Transformateur", n(sn, 0) + " kVA", true}, {"Tension de court-circuit", n(ucc, 0) + " %"}, {"Courant assigné au secondaire", n(sn * 1000 / (RACINE3 * u), 0) + " A"},
                  {"Courant de court-circuit aux bornes", n(ik, 1) + " kA"}, {"Taux de charge", n(100 * x[0] / sn, 0) + " %"}};
      r.formule = "Sn ≥ S × (1 + réserve) ; In = Sn / (√3 U) ; Ik3 = 1,05 × 1,05 × U0 / |Za + ZT| (réseau amont 500 MVA)";
      r.explication = "Un transformateur de " + n(sn, 0) + " kVA couvre l’appel avec " + n(x[1], 0) +
                      " % de réserve. Son courant de court-circuit fixe le pouvoir de coupure des appareils du tableau général.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Démarrage d'un moteur -----
  {
    Calculateur c = fiche("demarrage", "Démarrage d’un moteur", RESEAU, "Le courant de démarrage, la chute de tension qu’il provoque et la protection du moteur.", "UTE C 15-105, NF EN 60947-4-1");
    c.champs = {nombre("puissance", "Puissance utile", "kW", 15, 0.06, 1000, 2),
                nombre("tension", "Tension", "V", 400, 200, 1000, 0),
                nombre("cosphi", "cos φ", "", 0.85, 0.3, 1, 2),
                nombre("rendement", "Rendement", "", 0.9, 0.3, 1, 2),
                liste("mode", "Démarrage", {"Direct (Id ≈ 7 In)", "Étoile-triangle (≈ 2,5 In)", "Démarreur progressif (≈ 3 In)", "Variateur de vitesse (≈ 1,5 In)"}, 0),
                nombre("longueur", "Longueur du câble", "m", 30, 0.1, 5000, 1),
                liste("section", "Section", nomsSections(), indiceSection(4)),
                nombre("icc", "Icc au tableau", "kA", 10, 0.1, 200, 1)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double u = x[1], u0 = u / RACINE3, in = x[0] * 1000 / (x[3] * RACINE3 * u * x[2]);
      const double k = std::array<double, 4>{7, 2.5, 3, 1.5}[static_cast<std::size_t>(std::clamp(choixDe(x[4]), 0, 3))], id = k * in;
      const double R = rhoService(0) * x[5] / section(x[6]), X = 0.00008 * x[5];
      const double sphi = std::sqrt(1 - x[2] * x[2]), sd = std::sqrt(1 - 0.35 * 0.35);
      const double duN = 100 * (R * x[2] + X * sphi) * in / u0;
      const double duD = 100 * (R * 0.35 + X * sd) * id / u0 + 100 * id / (x[7] * 1000);  // câble + source (borne haute)
      std::string plage;
      for (auto [a, b] : std::vector<std::pair<double, double>>{{0.1, 0.16}, {0.16, 0.25}, {0.25, 0.4}, {0.4, 0.63}, {0.63, 1}, {1, 1.6}, {1.6, 2.5}, {2.5, 4}, {4, 6.3},
                                                               {6, 10}, {9, 14}, {13, 18}, {17, 23}, {20, 25}, {24, 32}, {30, 40}, {37, 50}, {48, 65}})
        if (in <= b + 1e-9) {
          plage = n(a, -1) + " à " + n(b, -1) + " A";
          break;
        }
      r.lignes = {{"Courant nominal In", n(in, 1) + " A", true},
                  {"Courant de démarrage", n(id, 0) + " A"},
                  {"Chute de tension en marche", n(duN) + " %"},
                  {"Chute de tension au démarrage", n(duD, 1) + " %", true},
                  {"Disjoncteur moteur", plage.empty() ? std::string("réglable (au-delà de 65 A)") : "plage " + plage + ", réglé à " + n(in, 1) + " A"}};
      r.formule = "In = P / (η √3 U cos φ) ; Id = k In ; ΔU démarrage ≈ (R × 0,35 + X × 0,94) Id / U0 + Id / Icc";
      const bool ok = duD <= 15;
      r.explication = ok ? "Le moteur démarre sans faire trop chuter la tension du tableau."
                         : "Au démarrage la tension chute de plus de 15 % : le moteur peine et les autres appareils en souffrent. Démarreur progressif, variateur, ou câble plus gros.";
      r.avis.push_back(ok ? avis(NiveauAvis::Info, "UTE C 15-105", "Chute au démarrage sous 15 %.")
                          : avis(NiveauAvis::Alerte, "UTE C 15-105", "Chute au démarrage de " + n(duD, 1) + " % au-delà de 15 %."));
      if (choixDe(x[4]) == 0) r.avis.push_back(avis(NiveauAvis::Info, "NF EN 60947-4-1", "Contacteur en catégorie AC-3, de courant assigné au moins " + n(in, 1) + " A."));
      return r;
    };
    v.push_back(c);
  }
  // ----- Pertes d'un câble -----
  {
    Calculateur c = fiche("pertes", "Pertes d’un câble et leur coût", RESEAU, "L’énergie perdue en chaleur dans un câble chaque année, et ce que rapporte la section supérieure.",
                  "Efficacité énergétique (NF C 15-100, partie 8-1)");
    c.champs = {nombre("courant", "Courant moyen", "A", 50, 0.1, 10000, 1),
                nombre("longueur", "Longueur", "m", 50, 0.1, 10000, 1),
                liste("section", "Section", nomsSections(), indiceSection(16)),
                liste("ame", "Âme", AMES, 0),
                liste("reseau", "Réseau", {"Monophasé (2 conducteurs)", "Triphasé (3 conducteurs)"}, 1),
                nombre("heures", "Heures de fonctionnement par an", "h", 3000, 1, 8760, 0),
                nombre("prix", "Prix du kWh", "€", 0.2, 0, 10, 3)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double S = section(x[2]), rho = rhoService(choixDe(x[3])), nc = choixDe(x[4]) == 0 ? 2 : 3;
      auto pertes = [&](double s) { return nc * rho * x[1] / s * x[0] * x[0]; };
      const double p = pertes(S), e = p * x[5] / 1000, cout = e * x[6];
      double sup = 0;
      for (double s2 : sectionsNormalisees())
        if (s2 > S + 1e-9) {
          sup = s2;
          break;
        }
      r.lignes = {{"Pertes", n(p, 0) + " W", true}, {"Énergie perdue par an", n(e, 0) + " kWh"}, {"Coût par an", n(cout, 0) + " €", true}};
      if (sup > 0) {
        const double gain = (p - pertes(sup)) * x[5] / 1000 * x[6];
        r.lignes.push_back({"Gain avec " + n(sup, -1) + " mm²", n(gain, 0) + " € par an"});
      }
      r.formule = "P = n × ρ × L / S × I² ; E = P × heures";
      r.explication = "Un câble chauffe : c’est de l’énergie payée pour rien. Sur les lignes longues et chargées, la section supérieure se rembourse souvent en quelques années.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Batterie -----
  {
    Calculateur c = fiche("batterie", "Autonomie d’une batterie (ASI)", RESEAU, "Combien de temps une batterie alimente une charge à travers un onduleur.", "Règle de l’art");
    c.champs = {nombre("puissance", "Puissance de la charge", "W", 1000, 1, 1e6, 0),
                nombre("rendement", "Rendement de l’onduleur", "", 0.9, 0.3, 1, 2),
                nombre("tension", "Tension de la batterie", "V", 24, 2, 1000, 0),
                nombre("capacite", "Capacité", "Ah", 100, 0.1, 1e5, 1),
                nombre("profondeur", "Profondeur de décharge", "%", 80, 10, 100, 0, "plomb : 50 % ; lithium : 80 à 90 %")};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double i = x[0] / (x[1] * x[2]), h = x[3] * x[4] / 100 / i;
      r.lignes = {{"Autonomie", n(h, 2) + " h (" + n(h * 60, 0) + " min)", true}, {"Courant de la batterie", n(i, 1) + " A"}, {"Énergie utile", n(x[3] * x[2] * x[4] / 100, 0) + " Wh"}};
      r.formule = "I = P / (η × U) ; autonomie = C × profondeur / I";
      r.explication = "À fort courant, une batterie au plomb rend moins que sa capacité nominale (loi de Peukert) : garde une marge de 20 à 30 %.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Prise de terre -----
  {
    Calculateur c = fiche("terre", "Prise de terre (schéma TT)", TERRE, "La résistance d’une prise de terre et le différentiel qui protège les personnes.",
                  "NF C 15-100 411.5 et partie 5-54 (valeurs approchées)");
    c.champs = {nombre("resistivite", "Résistivité du sol", "Ω·m", 100, 1, 10000, 0, "humus 50, argile 100, sable 500, roche 3 000"),
                liste("electrode", "Électrode", {"Piquet vertical", "Boucle à fond de fouille", "Conducteur enfoui horizontal", "Plaque verticale"}, 0),
                nombre("longueur", "Longueur (périmètre pour une boucle ou une plaque)", "m", 2, 0.1, 1000, 1),
                liste("differentiel", "Différentiel", {"30 mA", "100 mA", "300 mA", "500 mA", "1 A"}, 3),
                liste("ul", "Locaux", {"Secs (50 V)", "Mouillés ou chantiers (25 V)"}, 0)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const int e = choixDe(x[1]);
      const double coef = e == 0 ? 1.0 : e == 3 ? 0.8 : 2.0;
      const double ra = coef * x[0] / x[2];
      const double idn = std::array<double, 5>{0.03, 0.1, 0.3, 0.5, 1}[static_cast<std::size_t>(std::clamp(choixDe(x[3]), 0, 4))], ul = choixDe(x[4]) == 0 ? 50 : 25;
      const double raMax = ul / idn, uc = ra * idn;
      r.lignes = {{"Résistance de la prise de terre", n(ra, 1) + " Ω", true}, {"Résistance maximale admise", n(raMax, 0) + " Ω"}, {"Tension de contact en défaut", n(uc, 1) + " V"}};
      r.formule = std::string(e == 0 ? "Piquet : R = ρ / L" : e == 3 ? "Plaque : R = 0,8 ρ / L" : "R = 2 ρ / L") + " ; RA × IΔn ≤ UL (" + n(ul, 0) + " V)";
      const bool ok = uc <= ul + 1e-9;
      r.explication = ok ? "La tension qui apparaît sur une masse en défaut reste sous " + n(ul, 0) + " V : le différentiel protège les personnes."
                         : "Prise de terre trop résistante pour ce différentiel : améliore-la (piquets en parallèle, boucle) ou prends un différentiel plus sensible.";
      r.avis.push_back(ok ? avis(NiveauAvis::Info, "NF C 15-100 411.5.3", "RA × IΔn ≤ UL.")
                          : avis(NiveauAvis::Erreur, "NF C 15-100 411.5.3", "RA × IΔn = " + n(uc, 1) + " V au-delà de " + n(ul, 0) + " V."));
      return r;
    };
    v.push_back(c);
  }
  // ----- Éclairage intérieur -----
  {
    struct Activite {
      const char* nom;
      double lux;
    };
    static const std::vector<Activite> activites = {{"Bureau, écriture et lecture (500 lx)", 500}, {"Salle de réunion (500 lx)", 500}, {"Salle de classe (500 lx)", 500},
                                                    {"Couloir, circulation (100 lx)", 100},     {"Escalier (150 lx)", 150},         {"Sanitaires (200 lx)", 200},
                                                    {"Atelier, travail moyen (300 lx)", 300},   {"Atelier, travail fin (750 lx)", 750}, {"Entrepôt (150 lx)", 150},
                                                    {"Commerce, vente (300 lx)", 300},          {"Cuisine professionnelle (500 lx)", 500}, {"Parking couvert (75 lx)", 75}};
    std::vector<std::string> noms;
    for (const auto& a : activites) noms.push_back(a.nom);
    Calculateur c = fiche("eclairage", "Éclairage intérieur", ECLAIRAGE, "Le nombre de luminaires pour éclairer une pièce comme il faut (méthode des flux).",
                  "EN 12464-1 (éclairements usuels)");
    c.champs = {nombre("longueur", "Longueur de la pièce", "m", 10, 0.5, 500, 1),
                nombre("largeur", "Largeur", "m", 8, 0.5, 500, 1),
                liste("activite", "Activité", noms, 0),
                nombre("flux", "Flux d’un luminaire", "lm", 3600, 10, 200000, 0, "dalle LED 600 × 600 : 3 000 à 4 000 lm"),
                nombre("puissance", "Puissance d’un luminaire", "W", 30, 0.1, 5000, 1),
                nombre("utilisation", "Facteur d’utilisation", "", 0.6, 0.1, 1, 2, "pièce claire et basse : 0,7 ; sombre ou haute : 0,4"),
                nombre("maintenance", "Facteur de maintenance", "", 0.8, 0.3, 1, 2)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double s = x[0] * x[1], e = activites[static_cast<std::size_t>(std::clamp(choixDe(x[2]), 0, static_cast<int>(activites.size()) - 1))].lux;
      const double nb = std::ceil(e * s / (x[3] * x[5] * x[6]));
      const double obtenu = nb * x[3] * x[5] * x[6] / s;
      r.lignes = {{"Luminaires", n(nb, 0), true}, {"Éclairement obtenu", n(obtenu, 0) + " lx"}, {"Puissance installée", n(nb * x[4], 0) + " W (" + n(nb * x[4] / s, 1) + " W/m²)"},
                  {"Efficacité", n(x[3] / x[4], 0) + " lm/W"}};
      r.formule = "N = E × S / (Φ × U × M) ; E = " + n(e, 0) + " lx, S = " + n(s, 1) + " m²";
      r.explication = "Répartis les " + n(nb, 0) + " luminaires en quadrillage régulier, espacés d’au plus 1,5 fois leur hauteur au-dessus du plan de travail.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Éclairage de sécurité -----
  {
    Calculateur c = fiche("baes", "Éclairage de sécurité (BAES)", ECLAIRAGE, "Le nombre de blocs autonomes pour l’évacuation et pour l’ambiance (anti-panique).",
                  "Règlement de sécurité ERP (EC 8 à EC 15), NF C 71-800");
    c.champs = {nombre("circulation", "Longueur des circulations", "m", 60, 0, 10000, 0),
                nombre("issues", "Issues et dégagements à signaler", "", 2, 0, 500, 0),
                nombre("changements", "Changements de direction", "", 2, 0, 500, 0),
                nombre("surface", "Surface des locaux à éclairer en ambiance", "m²", 0, 0, 100000, 0, "locaux de plus de 300 m² ou recevant 100 personnes ; 0 si aucun"),
                nombre("flux", "Flux d’un bloc d’ambiance", "lm", 360, 10, 5000, 0),
                nombre("hauteur", "Hauteur de pose des blocs d’ambiance", "m", 3, 1, 30, 1)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double evac = std::max(2.0, std::ceil(x[0] / 15) + x[1] + x[2]);
      const double amb = x[3] > 0 ? std::max(2.0, std::ceil(5 * x[3] / x[4])) : 0;
      r.lignes = {{"Blocs d’évacuation", n(evac, 0), true}, {"Blocs d’ambiance", n(amb, 0), true}, {"Espacement maximal (ambiance)", n(4 * x[5], 1) + " m"}};
      r.formule = "Évacuation : un bloc tous les 15 m de circulation, au-dessus de chaque issue et à chaque changement de direction (2 au moins). Ambiance : 5 lm par m², 2 blocs au moins, espacés de 4 fois leur hauteur au plus.";
      r.explication = "Les blocs d’évacuation montrent le chemin de sortie ; ceux d’ambiance évitent la panique dans les grands locaux quand l’éclairage normal s’éteint.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Loi d'Ohm -----
  {
    Calculateur c = fiche("ohm", "Loi d’Ohm et puissance", BASES, "Donne deux grandeurs, laisse les autres à 0 : les autres se calculent.", "U = R × I ; P = U × I");
    c.champs = {nombre("tension", "Tension U", "V", 230, 0, 1e6, 3), nombre("courant", "Courant I", "A", 0, 0, 1e6, 3), nombre("resistance", "Résistance R", "Ω", 0, 0, 1e9, 3),
                nombre("puissance", "Puissance P", "W", 2000, 0, 1e9, 3)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      double u = x[0], i = x[1], R = x[2], p = x[3];
      const int connus = (u > 0) + (i > 0) + (R > 0) + (p > 0);
      if (connus < 2) {
        r.explication = "Donne deux grandeurs (les autres à 0) : par exemple la tension et la puissance d’un radiateur.";
        return r;
      }
      if (u > 0 && i > 0) {
        R = u / i;
        p = u * i;
      } else if (u > 0 && R > 0) {
        i = u / R;
        p = u * i;
      } else if (i > 0 && R > 0) {
        u = R * i;
        p = u * i;
      } else if (p > 0 && u > 0) {
        i = p / u;
        R = u / i;
      } else if (p > 0 && i > 0) {
        u = p / i;
        R = u / i;
      } else if (p > 0 && R > 0) {
        i = std::sqrt(p / R);
        u = R * i;
      }
      r.lignes = {{"Tension U", n(u, 2) + " V", true}, {"Courant I", n(i, 3) + " A", true}, {"Résistance R", n(R, 3) + " Ω", true}, {"Puissance P", n(p, 1) + " W", true}};
      r.formule = "U = R × I ; P = U × I = R × I² = U² / R";
      r.explication = connus > 2 ? "Plus de deux grandeurs données : les deux premières (dans l’ordre U, I, R, P) font foi." : "En courant alternatif, c’est vrai pour une résistance (cos φ = 1).";
      return r;
    };
    v.push_back(c);
  }
  // ----- Jauge AWG -----
  {
    Calculateur c = fiche("awg", "Section en mm² et jauge AWG", BASES, "Passe d’une section métrique à la jauge américaine (AWG), et inversement.", "ASTM B258");
    c.champs = {liste("sens", "Conversion", {"mm² vers AWG", "AWG vers mm²"}, 0), nombre("valeur", "Valeur", "", 2.5, 0, 1000, 3, "AWG : 0 pour 1/0, -1 pour 2/0…")};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      auto diametre = [](double awg) { return 0.127 * std::pow(92.0, (36 - awg) / 39); };
      auto nomAwg = [](int a) { return a >= 1 ? std::to_string(a) : std::to_string(1 - a) + "/0"; };
      if (choixDe(x[0]) == 0) {
        const double s = std::max(1e-6, x[1]), d = std::sqrt(4 * s / PI);
        const double awg = 36 - 39 * std::log(d / 0.127) / std::log(92.0);
        const int proche = static_cast<int>(std::lround(awg));
        const double sp = PI * std::pow(diametre(proche), 2) / 4;
        r.lignes = {{"Jauge la plus proche", "AWG " + nomAwg(proche), true}, {"Section de cette jauge", n(sp, 3) + " mm²"}, {"Diamètre du conducteur", n(d, 3) + " mm"}};
      } else {
        const double awg = std::round(x[1]), d = diametre(awg), s = PI * d * d / 4;
        r.lignes = {{"Section", n(s, 3) + " mm²", true}, {"Section métrique au-dessus", n(sectionAuDessus(s) > 0 ? sectionAuDessus(s) : s, -1) + " mm²"}, {"Diamètre", n(d, 3) + " mm"}};
      }
      r.formule = "d = 0,127 mm × 92^((36 − AWG) / 39) ; S = π d² / 4";
      r.explication = "Les jauges AWG ne tombent pas sur les sections métriques : prends toujours la section égale ou supérieure.";
      return r;
    };
    v.push_back(c);
  }
  // ----- Échauffement d'une armoire -----
  {
    Calculateur c = fiche("armoire", "Échauffement d’une armoire", TERRE, "La température dans une armoire fermée, et la ventilation ou la climatisation qu’il lui faut.",
                  "CEI/TR 60890 (surfaces efficaces), k = 5,5 W/(m²·K) pour la tôle peinte");
    c.champs = {nombre("largeur", "Largeur", "mm", 600, 100, 10000, 0),
                nombre("hauteur", "Hauteur", "mm", 800, 100, 5000, 0),
                nombre("profondeur", "Profondeur", "mm", 250, 50, 2000, 0),
                liste("pose", "Pose", posesArmoire(), 1),
                nombre("pertes", "Puissance dissipée à l’intérieur", "W", 150, 0, 100000, 0, "somme des pertes des appareils (disjoncteurs, contacteurs, variateurs, alimentations)"),
                nombre("exterieur", "Température extérieure maximale", "°C", 35, -20, 60, 0),
                nombre("maximum", "Température intérieure admise", "°C", 45, 0, 70, 0, "appareils courants : 40 à 55 °C ; électronique : 35 à 40 °C")};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double a = surfaceEchangeArmoire(x[0], x[1], x[2], choixDe(x[3]));
      const double k = 5.5, dt = x[4] / (k * a), ti = x[5] + dt;
      r.lignes = {{"Température intérieure", n(ti, 1) + " °C", true}, {"Échauffement", n(dt, 1) + " K"}, {"Surface d’échange efficace", n(a, 2) + " m²"}};
      r.formule = "ΔT = P / (k × A) ; ventilation : V = 3,1 × (P − k A ΔTmax) / ΔTmax m³/h";
      if (ti <= x[6] + 1e-9) {
        r.explication = "Les parois suffisent à évacuer la chaleur : pas de ventilation nécessaire.";
        r.avis.push_back(avis(NiveauAvis::Info, "CEI/TR 60890", "Température intérieure admise."));
      } else if (x[6] > x[5]) {
        const double pa = x[4] - k * a * (x[6] - x[5]), debit = 3.1 * pa / (x[6] - x[5]);
        r.lignes.push_back({"Ventilation nécessaire", n(debit, 0) + " m³/h", true});
        r.explication = "Les parois ne suffisent pas : un ventilateur filtrant de " + n(std::ceil(debit / 10) * 10, 0) + " m³/h au moins (sortie en haut, entrée en bas).";
        r.avis.push_back(avis(NiveauAvis::Alerte, "CEI/TR 60890", "Sans ventilation, " + n(ti, 0) + " °C à l’intérieur."));
      } else {
        const double froid = x[4] + k * a * (x[5] - x[6]);
        r.lignes.push_back({"Climatiseur nécessaire", n(froid, 0) + " W de froid", true});
        r.explication = "Il fait plus chaud dehors que ce qu’admettent les appareils : seul un climatiseur d’armoire convient.";
        r.avis.push_back(avis(NiveauAvis::Alerte, "CEI/TR 60890", "Ventilation impossible : climatisation."));
      }
      return r;
    };
    v.push_back(c);
  }
  // ----- Lignes de transmission (8.21) : analyse et synthèse, moteur de lignes_transmission.cpp -----
  {
    const std::string LIGNES = "Lignes de transmission";
    const std::vector<std::string> genres = {"Microruban", "Ruban (entre deux plans)", "Paire en microruban", "Paire en ruban"};
    auto champsCommuns = [&](bool synthese) {
      std::vector<ChampCalcul> ch = {liste("genre", "Ligne", genres, 0),
                                     nombre("er", "Permittivité εr", "", 4.3, 1, 15, 2, "à la fréquence de référence"),
                                     nombre("tand", "Pertes tan δ", "", 0.02, 0, 0.1, 4),
                                     nombre("h", "Diélectrique (microruban) ou écart entre plans (ruban)", "mm", 0.2, 0.01, 10, 3),
                                     nombre("t", "Épaisseur du cuivre", "mm", 0.035, 0.005, 0.2, 3),
                                     nombre(synthese ? "z" : "w", synthese ? "Impédance visée (Zdiff pour une paire)" : "Largeur", synthese ? "Ω" : "mm", synthese ? 50 : 0.35, synthese ? 10 : 0.01,
                                            synthese ? 300 : 20, synthese ? 1 : 3),
                                     nombre("s", "Écart de la paire", "mm", 0.15, 0.01, 10, 3),
                                     nombre("f", "Fréquence", "GHz", 1, 0.001, 100, 3),
                                     nombre("rq", "Rugosité (Hammerstad, RMS)", "µm", 0, 0, 10, 2),
                                     liste("ds", "Diélectrique large bande (Djordjevic et Sarkar)", {"Non", "Oui"}, 1)};
      return ch;
    };
    auto ligne = [](const V& x) {
      return prom::ecad::LigneTransmission(static_cast<prom::ecad::GenreLigne>(choixDe(x[0])), {x[1], x[2], 1e9, 1e3, 1e12, choixDe(x[9]) == 1},
                                           {0, 0, 0, x[8] * 1e-6});
    };
    auto lignes = [](const prom::ecad::ResultatLigne& R, bool paire, ResultatCalcul& r) {
      if (paire) {
        r.lignes.push_back({"Impédance différentielle", n(R.zDiff, 1) + " Ω", true});
        r.lignes.push_back({"Mode impair / mode pair", n(R.zImpair, 1) + " / " + n(R.zPair, 1) + " Ω"});
        r.lignes.push_back({"Mode commun", n(R.zCommun, 1) + " Ω"});
      } else
        r.lignes.push_back({"Impédance Z0", n(R.z0, 1) + " Ω", true});
      r.lignes.push_back({"Permittivité effective", n(R.erEff, 3)});
      r.lignes.push_back({"Retard", n(R.retard, 3) + " ps/mm"});
      r.lignes.push_back({"Pertes du conducteur", n(R.pertesConducteur / 100, 4) + " dB/cm"});
      r.lignes.push_back({"Pertes du diélectrique", n(R.pertesDielectrique / 100, 4) + " dB/cm"});
    };
    {
      Calculateur c = fiche("ligne", "Impédance d’une ligne", LIGNES, "Impédance, retard et pertes d’une piste ou d’une paire au-dessus d’un plan ou entre deux plans.",
                            "Hammerstad et Jensen, Kirschning et Jansen, Wadell, Cohn ; Djordjevic et Sarkar ; Hammerstad (rugosité)");
      c.champs = champsCommuns(false);
      c.calculer = [=](const V& x) {
        ResultatCalcul r;
        const bool paire = choixDe(x[0]) >= 2;
        const prom::ecad::ResultatLigne R = ligne(x).analyser({x[5] * 1e-3, paire ? x[6] * 1e-3 : 0, x[3] * 1e-3, 0, x[4] * 1e-3, 0}, x[7] * 1e9);
        lignes(R, paire, r);
        r.formule = paire ? "Kirschning et Jansen (microruban) ou Cohn (ruban), modes pair et impair, dispersion comprise" : "Hammerstad et Jensen (microruban, dispersion de Kirschning et Jansen) ou Wadell (ruban)";
        r.explication = "Les mêmes formules que le calculateur de KiCad, à moins de 2 % près ; le retard donne la vitesse de la couche aux règles d’accord.";
        return r;
      };
      v.push_back(c);
    }
    {
      Calculateur c = fiche("ligne-synthese", "Largeur pour une impédance", LIGNES, "La largeur de piste qui donne l’impédance visée (Zdiff pour une paire, à écart fixé).",
                            "Synthèse par dichotomie sur le moteur d’analyse");
      c.champs = champsCommuns(true);
      c.calculer = [=](const V& x) {
        ResultatCalcul r;
        const bool paire = choixDe(x[0]) >= 2;
        const prom::ecad::GeometrieLigne g{0, paire ? x[6] * 1e-3 : 0, x[3] * 1e-3, 0, x[4] * 1e-3, 0};
        const auto w = ligne(x).largeurPour(x[5], g, x[7] * 1e9);
        if (!w) {
          r.lignes.push_back({"Largeur", "hors d’atteinte", true});
          r.explication = "Aucune largeur raisonnable ne donne cette impédance sur ce diélectrique : change l’épaisseur ou l’écart.";
          return r;
        }
        r.lignes.push_back({"Largeur", n(*w * 1e3, 3) + " mm", true});
        prom::ecad::GeometrieLigne gw = g;
        gw.largeur = *w;
        lignes(ligne(x).analyser(gw, x[7] * 1e9), paire, r);
        r.formule = "Z(w) décroissante : dichotomie sur w jusqu’à Z(w) = Z visée";
        r.explication = "Arrondis la largeur au pas de ton fabricant puis reviens à l’analyse pour lire l’écart.";
        return r;
      };
      v.push_back(c);
    }
  }
  return v;
}

}  // namespace

const std::vector<Calculateur>& calculateursElectriques() {
  static const std::vector<Calculateur> v = construire();
  return v;
}

const Calculateur* calculateur(const std::string& id) {
  for (const auto& c : calculateursElectriques())
    if (c.id == id) return &c;
  return nullptr;
}

const std::vector<std::string>& famillesCalculateurs() {
  static const std::vector<std::string> f = {"Câbles et protections", "Puissance et réseau", "Terre et sécurité", "Éclairage", "Bases et conversions", "Lignes de transmission"};
  return f;
}

ResultatCalcul executer(const Calculateur& c, std::vector<double> valeurs) {
  const std::size_t donnees = valeurs.size();
  valeurs.resize(c.champs.size());
  for (std::size_t i = 0; i < c.champs.size(); ++i) {
    const ChampCalcul& ch = c.champs[i];
    double& v = valeurs[i];
    if (i >= donnees || !std::isfinite(v)) v = ch.valeur;
    v = std::clamp(v, ch.min, ch.max);
    if (!ch.choix.empty()) v = std::round(v);
  }
  return c.calculer ? c.calculer(valeurs) : ResultatCalcul{};
}

}  // namespace prom::elec
