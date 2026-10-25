// Prométhée : calculateurs de l'électronique (lot L11 de la 0.34). Voir calculateurs_electronique.hpp.
// Sources : IPC-2221B (§ 6.2 largeur de piste, table 6-1 isolement), I. M. Onderdonk (courant de fusion, adiabatique),
// H. Johnson et M. Graham, « High-Speed Digital Design » (1993 : vias, diaphonie, longueur critique), CEI 60664-1
// table F.4 (lignes de fuite), R. Lundin (1985, coefficient de Nagaoka), H. A. Wheeler (1928, bobines multicouches),
// CEI 60062 (code des couleurs), CEI 60063 (séries E), CEI 60028 (cuivre recuit), A. Bar-Cohen et W. M. Rohsenow
// (1984, ailettes en convection naturelle), MIL-STD-889 (indice anodique). Licence GPL-3.0-only.
#include "promethee/calculateurs_electronique.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>

#include "promethee/circuit.hpp"
#include "promethee/regles_carte.hpp"
#include "promethee/simulation.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

using prom::elec::AvisCalcul;
using prom::elec::Calculateur;
using prom::elec::ChampCalcul;
using prom::elec::LigneResultat;
using prom::elec::NiveauAvis;
using prom::elec::ResultatCalcul;

namespace {
using V = std::vector<double>;
constexpr double PI = std::numbers::pi;
constexpr double MU0 = 4e-7 * PI;
constexpr double C0 = 299'792'458.0;
constexpr double RHO_CU = 1.0 / 58.0 * 1e-6;  // Ω·m à 20 °C (CEI 60028)
constexpr double ALPHA_CU = 0.00393;          // /K
constexpr double K_CU = 385;                  // W/(m·K)

const std::string COURANTS = "Courants", CARTE = "Carte", ETABLI = "Établi", ENERGIE = "Énergie et chaleur";

// « 4,7 kΩ », « 2,5 mm »
std::string si(double v, const std::string& unite, int chiffres = 3) { return ecrireSI(v, unite, chiffres); }
std::string fr(double v, int decimales = 2) {
  char t[64];
  std::snprintf(t, sizeof t, "%.*f", decimales, v);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
ChampCalcul nombre(const std::string& id, const std::string& nom, const std::string& unite, double valeur, double min, double max, int decimales, const std::string& aide = {}) {
  ChampCalcul c;
  c.id = id, c.nom = nom, c.unite = unite, c.valeur = valeur, c.min = min, c.max = max, c.decimales = decimales, c.aide = aide;
  return c;
}
ChampCalcul liste(const std::string& id, const std::string& nom, std::vector<std::string> choix, int defaut, const std::string& aide = {}) {
  ChampCalcul c;
  c.id = id, c.nom = nom, c.choix = std::move(choix), c.valeur = defaut, c.min = 0, c.max = static_cast<double>(c.choix.size()) - 1, c.decimales = 0, c.aide = aide;
  return c;
}
Calculateur fiche(const std::string& id, const std::string& nom, const std::string& famille, const std::string& description, const std::string& regle) {
  Calculateur c;
  c.id = id, c.nom = nom, c.famille = famille, c.description = description, c.regle = regle;
  return c;
}
int choix(double v) { return static_cast<int>(std::lround(v)); }
AvisCalcul avis(NiveauAvis n, const std::string& regle, const std::string& message) { return {n, regle, message}; }
// Valeurs normalisées voisines (au-dessous et au-dessus) d'une valeur dans une série E
std::string voisines(double v, int serie, const std::string& unite) {
  if (!(v > 0)) return "—";
  const double p = plusProcheDansSerie(v, serie);
  const double bas = p <= v ? p : voisineDansSerie(p, serie, -1), haut = p >= v ? p : voisineDansSerie(p, serie, +1);
  if (std::abs(bas - haut) < 1e-12 * v) return si(p, unite) + " (E" + std::to_string(serie) + ")";
  return si(bas, unite) + " ou " + si(haut, unite) + " (E" + std::to_string(serie) + ")";
}
// IPC-2221 : I = k ΔT^0,44 A^0,725 (A en mil², k = 0,048 dehors, 0,024 dedans)
double courantIPC2221(double largeurMm, double epaisseurMm, double echauffement, bool externe) {
  const double aMil2 = (largeurMm / 0.0254) * (epaisseurMm / 0.0254);
  return (externe ? 0.048 : 0.024) * std::pow(std::max(echauffement, 0.0), 0.44) * std::pow(aMil2, 0.725);
}
// Onderdonk : I = A √(log10(1 + (1083 − Ta) / (234 + Ta)) / (33 t)), A en mils circulaires
double courantFusion(double sectionMm2, double tAmbiante, double t) {
  const double cmil = sectionMm2 / (0.0254 * 0.0254) * 4 / PI;
  return cmil * std::sqrt(std::log10(1 + (1083 - tAmbiante) / (234 + tAmbiante)) / (33 * std::max(t, 1e-9)));
}
const std::vector<std::string> CUIVRES = {"½ oz (17,5 µm)", "1 oz (35 µm)", "2 oz (70 µm)", "3 oz (105 µm)"};
double epaisseurCuivre(double i) { return std::array<double, 4>{0.0175, 0.035, 0.070, 0.105}[static_cast<std::size_t>(std::clamp(choix(i), 0, 3))]; }
const std::vector<std::string> SERIES = {"E12", "E24", "E48", "E96"};
int serieDe(double i) { return std::array<int, 4>{12, 24, 48, 96}[static_cast<std::size_t>(std::clamp(choix(i), 0, 3))]; }

// ---------- Code des couleurs ----------
const std::vector<std::string>& couleurs() {
  static const std::vector<std::string> c = {"noir", "marron", "rouge", "orange", "jaune", "vert", "bleu", "violet", "gris", "blanc", "or", "argent", "aucun"};
  return c;
}
int indiceCouleur(const std::string& c) {
  const auto& v = couleurs();
  const auto it = std::find(v.begin(), v.end(), c);
  return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}
const std::map<std::string, double>& tolerancesCouleur() {
  static const std::map<std::string, double> t = {{"marron", 0.01}, {"rouge", 0.02}, {"vert", 0.005}, {"bleu", 0.0025}, {"violet", 0.001}, {"gris", 0.0005},
                                                  {"or", 0.05},     {"argent", 0.10}, {"aucun", 0.20}};
  return t;
}

// ---------- Bobines ----------
// Coefficient de Nagaoka par les formules de Lundin (1985), x = diamètre / longueur
double nagaoka(double x) {
  auto f1 = [](double u) { return (1 + 0.383901 * u + 0.017108 * u * u) / (1 + 0.258952 * u); };
  auto f2 = [](double u) { return 0.093842 * u + 0.002029 * u * u - 0.000801 * u * u * u; };
  if (x <= 1) return f1(x * x) - 4 * x / (3 * PI);
  const double u = 1 / (x * x);
  return 2 / (PI * x) * ((std::log(4 * x) - 0.5) * f1(u) + f2(u));
}
double bobineAir(double diametreMm, double longueurMm, double spires) {
  const double a = diametreMm / 2000, b = longueurMm / 1000;
  return MU0 * PI * a * a * spires * spires / b * nagaoka(diametreMm / longueurMm);
}

// ---------- Fonctions de Kelvin (séries) : ber, bei et dérivées ----------
void kelvin(double x, double& ber, double& bei, double& dber, double& dbei) {
  ber = bei = dber = dbei = 0;
  // ber(x) + i bei(x) = J0(x e^{3iπ/4}) = Σ (−1)^k (x/2)^{2k} i^k... évalué par la série complexe
  std::complex<double> z = std::polar(x, 3 * PI / 4), terme(1, 0), somme(0, 0), derive(0, 0);
  const std::complex<double> z2 = z * z / 4.0;
  for (int k = 0; k < 200; ++k) {
    if (k > 0) terme *= -z2 / static_cast<double>(k * k);
    somme += terme;
    if (k > 0) derive += terme * (2.0 * k) / z;  // d/dz de (z/2)^{2k}
    if (std::abs(terme) < 1e-18 * std::max(1.0, std::abs(somme)) && k > 5) break;
  }
  // d/dx = d/dz · e^{3iπ/4}
  const std::complex<double> dx = derive * std::polar(1.0, 3 * PI / 4);
  ber = somme.real(), bei = somme.imag(), dber = dx.real(), dbei = dx.imag();
}
// Rca / Rcc d'un fil rond de rayon r à la profondeur de peau δ
double rapportPeau(double rayon, double delta) {
  const double x = std::sqrt(2.0) * rayon / delta;
  if (x > 30) return rayon / (2 * delta) + 0.25;
  double ber, bei, dber, dbei;
  kelvin(x, ber, bei, dber, dbei);
  const double den = dber * dber + dbei * dbei;
  if (den <= 0) return 1;
  return x / 2 * (ber * dbei - bei * dber) / den;
}
double diametreAwg(double n) { return 0.127 * std::pow(92.0, (36 - n) / 39); }

std::vector<Calculateur> construire() {
  std::vector<Calculateur> v;
  // ======================== Courants (11.7) ========================
  {
    Calculateur c = fiche("piste", "Largeur de piste et courant de fusion", COURANTS, "Largeur d’une piste pour un courant et un échauffement donnés ; sa résistance, sa chute et le courant qui la fond en un temps donné.",
                          "IPC-2221B § 6.2 ; Onderdonk");
    c.champs = {nombre("courant", "Courant", "A", 1, 0.001, 100, 3), nombre("echauffement", "Échauffement admis", "°C", 10, 1, 100, 1), liste("cuivre", "Cuivre", CUIVRES, 1),
                liste("couche", "Couche", {"Externe", "Interne"}, 0), nombre("longueur", "Longueur", "mm", 50, 0.1, 10000, 1), nombre("ambiante", "Température ambiante", "°C", 25, -55, 150, 0),
                nombre("duree", "Durée du défaut (fusion)", "s", 1, 0.001, 100, 3)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double i = x[0], dt = x[1], e = epaisseurCuivre(x[2]), l = x[4], ta = x[5], t = x[6];
      const bool externe = choix(x[3]) == 0;
      const double w = mm(largeurIPC2221(i, dt, e, externe));
      const double rPiste = RHO_CU * (1 + ALPHA_CU * (ta + dt - 20)) * (l / 1000) / (w / 1000 * e / 1000);
      r.lignes = {{"Largeur", fr(w, 3) + " mm (" + fr(w / 0.0254, 1) + " mil)", true},
                  {"Résistance", si(rPiste, "Ω")},
                  {"Chute de tension", si(rPiste * i, "V")},
                  {"Pertes", si(rPiste * i * i, "W")},
                  {"Courant de fusion en " + fr(t, 3) + " s", fr(courantFusion(w * e, ta, t), 1) + " A"}};
      r.formule = "I = k × ΔT^0,44 × A^0,725 (A en mil², k = 0,048 dehors, 0,024 dedans) ; fusion : I = A √(log10(1 + (1083 − Ta)/(234 + Ta)) / (33 t)), A en mils circulaires";
      r.explication = "L’abaque de l’IPC-2221 vaut pour une carte de 1,6 mm sans plan ; une piste sur un plan chauffe moins. Le courant de fusion est celui d’un défaut bref : la piste ne doit jamais le voir.";
      if (w < 0.1) r.avis.push_back(avis(NiveauAvis::Alerte, "Capacités des fabricants", "Sous 0,1 mm, peu de fabricants gravent à coût courant : élargis la piste."));
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("diviseur", "Diviseur de tension", COURANTS, "Deux résistances de série E qui donnent au plus près la tension voulue.", "Vs = Ve × R2 / (R1 + R2) ; CEI 60063");
    c.champs = {nombre("ve", "Tension d’entrée", "V", 12, 0.001, 1e5, 3), nombre("vs", "Tension voulue", "V", 3.3, 0.001, 1e5, 3), nombre("rtotal", "Résistance totale environ", "kΩ", 10, 0.01, 1e5, 2),
                liste("serie", "Série", SERIES, 1)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double ve = x[0], vs = x[1], rt = x[2] * 1e3;
      const int serie = serieDe(x[3]);
      if (vs >= ve) {
        r.explication = "La tension voulue doit être sous celle d’entrée.";
        return r;
      }
      double meilleur = 1e300, r1b = 0, r2b = 0;
      // R2 parcourt la série autour de sa valeur idéale, R1 suit au plus près
      const double r2i = rt * vs / ve;
      for (int d = -12; d <= 12; ++d) {
        double r2 = plusProcheDansSerie(r2i, serie);
        for (int k = 0; k < std::abs(d); ++k) r2 = voisineDansSerie(r2, serie, d > 0 ? 1 : -1);
        const double r1 = plusProcheDansSerie(r2 * (ve / vs - 1), serie);
        const double e = std::abs(ve * r2 / (r1 + r2) - vs) / vs + 1e-6 * std::abs(std::log((r1 + r2) / rt));
        if (e < meilleur) meilleur = e, r1b = r1, r2b = r2;
      }
      const double obtenue = ve * r2b / (r1b + r2b);
      r.lignes = {{"R1 (haut)", si(r1b, "Ω"), true}, {"R2 (bas)", si(r2b, "Ω"), true}, {"Tension obtenue", fr(obtenue, 4) + " V"}, {"Écart", fr(100 * (obtenue - vs) / vs, 2) + " %"},
                  {"Courant", si(ve / (r1b + r2b), "A")}};
      r.formule = "Vs = Ve × R2 / (R1 + R2)";
      r.explication = "Le diviseur se charge : la résistance de ce qui suit doit être grande devant R2 (dix fois au moins), sinon la tension baisse.";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("led", "Résistance d’une LED", COURANTS, "Résistance série d’une LED, sa puissance et le boîtier qui la tient.", "R = (Va − Vf) / I");
    c.champs = {nombre("va", "Tension d’alimentation", "V", 5, 0.5, 1000, 2), nombre("vf", "Tension directe de la LED", "V", 2, 0.5, 50, 2), nombre("courant", "Courant", "mA", 10, 0.01, 2000, 2),
                liste("serie", "Série", SERIES, 1)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double va = x[0], vf = x[1], i = x[2] / 1000;
      if (va <= vf) {
        r.explication = "L’alimentation doit dépasser la tension directe de la LED.";
        return r;
      }
      const double rx = (va - vf) / i;
      // La valeur normalisée au-dessus : le courant ne dépasse pas celui voulu
      double rn = plusProcheDansSerie(rx, serieDe(x[3]));
      if (rn < rx) rn = voisineDansSerie(rn, serieDe(x[3]), 1);
      const double p = (va - vf) * (va - vf) / rn;
      const char* boitier = p <= 0.05 ? "0402 ou plus" : p <= 0.1 ? "0603 ou plus" : p <= 0.125 ? "0805 ou plus" : p <= 0.25 ? "1206 ou plus" : p <= 0.5 ? "2010 ou plus" : p <= 1 ? "2512" : "résistance de puissance";
      r.lignes = {{"Résistance normalisée", si(rn, "Ω"), true}, {"Résistance exacte", si(rx, "Ω")}, {"Courant obtenu", fr((va - vf) / rn * 1000, 2) + " mA"},
                  {"Puissance dans la résistance", si(p, "W")}, {"Boîtier conseillé", std::string(boitier) + " (puissance doublée par prudence)"}};
      r.formule = "R = (Va − Vf) / I ; P = (Va − Vf)² / R";
      r.explication = "La valeur normalisée est prise au-dessus de l’exacte : la LED reçoit au plus le courant voulu.";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("regulateur", "Régulateur réglable", COURANTS, "Résistances d’un régulateur réglable et plage de sortie aux huit coins de Vref, R1 et R2.",
                          "Vs = Vref × (1 + R2/R1) + Iadj × R2 ; fiches LM317, LM1117, LM2596, TL431");
    c.champs = {liste("modele", "Régulateur", {"LM317 (1,25 V)", "LM1117 / AMS1117 réglable (1,25 V)", "LM2596-ADJ (1,23 V)", "TL431 (2,495 V)", "LM337 (−1,25 V)"}, 0),
                nombre("vs", "Tension voulue", "V", 5, 1.25, 60, 3), nombre("r1", "R1 (entre sortie et réglage)", "Ω", 240, 1, 1e6, 0), nombre("tvref", "Tolérance de Vref", "%", 4, 0, 10, 2),
                nombre("tr", "Tolérance des résistances", "%", 1, 0, 10, 2), liste("serie", "Série", SERIES, 3)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      static const double vrefs[] = {1.25, 1.25, 1.23, 2.495, 1.25}, iadj[] = {50e-6, 60e-6, 0, 2e-6, 65e-6};
      const int m = std::clamp(choix(x[0]), 0, 4);
      const double vref = vrefs[m], ia = iadj[m], vs = x[1], r1 = x[2], tv = x[3] / 100, tr = x[4] / 100;
      if (vs <= vref) {
        r.explication = "La tension voulue doit dépasser la référence du régulateur.";
        return r;
      }
      const double r2x = (vs - vref) / (vref / r1 + ia);
      const double r2 = plusProcheDansSerie(r2x, serieDe(x[5]));
      auto sortie = [&](double vr, double a, double b) { return vr * (1 + b / a) + ia * b; };
      double mn = 1e300, mx = -1e300;
      for (int k = 0; k < 8; ++k) {
        const double s = sortie(vref * (1 + ((k & 1) ? tv : -tv)), r1 * (1 + ((k & 2) ? tr : -tr)), r2 * (1 + ((k & 4) ? tr : -tr)));
        mn = std::min(mn, s), mx = std::max(mx, s);
      }
      r.lignes = {{"R2 normalisée", si(r2, "Ω"), true}, {"R2 exacte", si(r2x, "Ω")}, {"Sortie nominale", fr(sortie(vref, r1, r2), 3) + " V", true},
                  {"Plage aux huit coins", fr(mn, 3) + " à " + fr(mx, 3) + " V"}};
      r.formule = "Vs = Vref × (1 + R2/R1) + Iadj × R2";
      r.explication = m == 4 ? "Le LM337 régule une tension négative : les valeurs sont celles de son module." : "La plage aux huit coins prend chaque grandeur à ses deux bornes de tolérance.";
      if (m == 0 && r1 > 240 * 1.1) r.avis.push_back(avis(NiveauAvis::Alerte, "Fiche LM317", "R1 de plus de 240 Ω : le courant minimal de charge (3,5 à 10 mA) n’est peut-être plus tenu à vide."));
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("filtre-rc", "Filtre RC", COURANTS, "Fréquence de coupure d’un filtre RC, ou la résistance pour une coupure voulue.", "fc = 1 / (2π R C)");
    c.champs = {liste("mode", "Calcul", {"Coupure depuis R et C", "R depuis la coupure et C"}, 0), nombre("r", "Résistance", "kΩ", 1, 1e-3, 1e6, 3), nombre("c", "Capacité", "nF", 100, 1e-3, 1e9, 3),
                nombre("fc", "Coupure voulue", "Hz", 1591.55, 1e-3, 1e10, 2)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double cap = x[2] * 1e-9;
      if (choix(x[0]) == 0) {
        const double fc = 1 / (2 * PI * x[1] * 1e3 * cap);
        r.lignes = {{"Fréquence de coupure", si(fc, "Hz"), true}, {"Constante de temps", si(x[1] * 1e3 * cap, "s")}};
      } else {
        const double rr = 1 / (2 * PI * x[3] * cap);
        r.lignes = {{"Résistance", si(rr, "Ω"), true}, {"Valeurs normalisées", voisines(rr, 24, "Ω")}};
      }
      r.formule = "fc = 1 / (2π R C) : à cette fréquence le gain vaut −3 dB, la phase −45°";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("normalisee", "Valeur normalisée", COURANTS, "La valeur de série E la plus proche d’une cible, et deux valeurs en série ou en parallèle qui l’approchent.", "CEI 60063");
    c.champs = {nombre("valeur", "Valeur voulue", "", 12340, 1e-12, 1e12, 4), liste("serie", "Série", SERIES, 1)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const int serie = serieDe(x[1]);
      const double p = plusProcheDansSerie(x[0], serie);
      const CombinaisonE k = combinaisonE(x[0], serie);
      r.lignes = {{"La plus proche", si(p, "") + " (écart " + fr(100 * (p - x[0]) / x[0], 2) + " %)", true}, {"Voisines", voisines(x[0], serie, "")},
                  {"Couple", si(k.a, "") + (k.parallele ? " ∥ " : " + ") + si(k.b, "") + " = " + si(k.valeur, "") + " (écart " + fr(100 * k.ecart, 3) + " %)"}};
      r.formule = "Séries E : 3 à 192 valeurs par décade, à pas logarithmique égal";
      return r;
    };
    v.push_back(c);
  }
  // ======================== Carte (11.17) ========================
  {
    Calculateur c = fiche("via", "Via", CARTE, "Résistance, chute, pertes, résistance thermique, capacité, inductance, résonance et courant admissible d’une via.",
                          "Johnson et Graham (1993) ; IPC-2221 (courant, estimé)");
    c.champs = {nombre("percage", "Perçage", "mm", 0.3, 0.05, 5, 3), nombre("paroi", "Cuivre de paroi", "µm", 25, 5, 100, 1), nombre("longueur", "Longueur (épaisseur traversée)", "mm", 1.6, 0.05, 10, 3),
                nombre("pastille", "Diamètre de la pastille", "mm", 0.6, 0.1, 10, 3), nombre("antipastille", "Diamètre du dégagement dans le plan", "mm", 1.0, 0.2, 20, 3),
                nombre("er", "Permittivité", "", 4.4, 1, 15, 2), nombre("courant", "Courant", "A", 1, 0, 100, 3), nombre("echauffement", "Échauffement admis", "°C", 10, 1, 100, 1),
                nombre("montee", "Temps de montée du signal", "ns", 1, 0.001, 1000, 3)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double d = x[0] / 1000, t = x[1] * 1e-6, h = x[2] / 1000, i = x[6];
      const double section = PI * t * (d + t);
      const double rv = RHO_CU * h / section, rth = h / (K_CU * section);
      const double hi = x[2] / 25.4, d1 = x[3] / 25.4, d2 = x[4] / 25.4, di = x[0] / 25.4;
      const double cap = d2 > d1 ? 1.41 * x[5] * hi * d1 / (d2 - d1) * 1e-12 : 0;
      const double ind = 5.08 * hi * (std::log(4 * hi / di) + 1) * 1e-9;
      const double f0 = cap > 0 && ind > 0 ? 1 / (2 * PI * std::sqrt(ind * cap)) : 0;
      const double admis = courantIPC2221(PI * (x[0] + x[1] / 1000), x[1] / 1000, x[7], true);
      r.lignes = {{"Résistance", si(rv, "Ω"), true}, {"Chute de tension", si(rv * i, "V")}, {"Pertes", si(rv * i * i, "W")}, {"Résistance thermique", fr(rth, 1) + " K/W"},
                  {"Capacité", si(cap, "F")}, {"Inductance", si(ind, "H")}, {"Résonance propre", si(f0, "Hz")}, {"Montée dégradée de", si(2.2 * cap * 50 / 2, "s") + " (sous 50 Ω)"},
                  {"Courant admissible (estimé)", fr(admis, 2) + " A pour " + fr(x[7], 0) + " °C"}};
      r.formule = "R = ρ h / (π t (d + t)) ; C = 1,41 εr T D1 / (D2 − D1) pF, L = 5,08 h (ln(4h/d) + 1) nH, cotes en pouces";
      r.explication = "Le courant admissible est estimé par l’IPC-2221 sur la section de la paroi ; l’IPC-2152 (abaques mesurés) donne en général un peu plus.";
      if (i > admis) r.avis.push_back(avis(NiveauAvis::Alerte, "IPC-2221", "Le courant dépasse ce qu’une via tient pour cet échauffement : mets-en plusieurs en parallèle."));
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("fusion", "Courant de fusion d’une piste", CARTE, "Courant qui fond une piste en un temps donné, sans échange de chaleur (défaut bref).", "Onderdonk");
    c.champs = {nombre("largeur", "Largeur", "mm", 0.25, 0.01, 100, 3), liste("cuivre", "Cuivre", CUIVRES, 1), nombre("ambiante", "Température ambiante", "°C", 25, -55, 150, 0),
                nombre("duree", "Durée", "s", 1, 1e-4, 100, 4)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double s = x[0] * epaisseurCuivre(x[1]);
      r.lignes = {{"Courant de fusion", fr(courantFusion(s, x[2], x[3]), 2) + " A", true}, {"Section", fr(s * 1e3, 2) + " × 10⁻³ mm²"}};
      r.formule = "I = A √(log10(1 + (1083 − Ta) / (234 + Ta)) / (33 t)), A en mils circulaires";
      r.explication = "Le calcul suppose toute la chaleur gardée dans le cuivre : il vaut pour des défauts courts (moins de quelques secondes).";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("diaphonie", "Diaphonie et longueur critique", CARTE, "Diaphonie entre deux pistes voisines au-dessus d’un plan ; bande du signal et longueur au-delà de laquelle la piste est une ligne.",
                          "Johnson et Graham (1993)");
    c.champs = {nombre("ecart", "Écart entre les pistes (axe à axe)", "mm", 0.5, 0.01, 100, 3), nombre("hauteur", "Hauteur au-dessus du plan", "mm", 0.2, 0.01, 10, 3),
                nombre("k", "Coefficient K (≤ 1, selon montée et longueur couplée)", "", 1, 0.01, 1, 2), nombre("montee", "Temps de montée", "ns", 1, 0.001, 1000, 3),
                nombre("er", "Permittivité effective", "", 3.3, 1, 15, 2)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double xt = x[2] / (1 + std::pow(x[0] / x[1], 2));
      const double tr = x[3] * 1e-9, vitesse = C0 / std::sqrt(x[4]);
      r.lignes = {{"Diaphonie", fr(100 * xt, 2) + " % (" + fr(20 * std::log10(std::max(xt, 1e-12)), 1) + " dB)", true}, {"Bande du signal", si(0.35 / tr, "Hz")},
                  {"Longueur critique", fr(tr / 6 * vitesse * 1000, 1) + " mm (délai d’un sixième de la montée)"}, {"Délai par centimètre", si(0.01 / vitesse, "s")}};
      r.formule = "diaphonie ≈ K / (1 + (s/h)²) ; bande = 0,35 / tr ; longueur critique : délai tr / 6";
      r.explication = "Doubler l’écart divise la diaphonie par quatre environ ; au-delà de la longueur critique, adapte la ligne.";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("impedance-cible", "Impédance cible de l’alimentation", CARTE, "Impédance que le réseau d’alimentation ne doit pas dépasser, et capacité de deux plans en regard.",
                          "Z = V × ondulation / ΔI ; C = ε0 εr S / e");
    c.champs = {nombre("tension", "Tension", "V", 3.3, 0.1, 100, 3), nombre("ondulation", "Ondulation permise", "%", 3, 0.1, 20, 2), nombre("di", "Saut de courant", "A", 1, 0.001, 1000, 3),
                nombre("surface", "Surface des plans en regard", "cm²", 50, 0.01, 1e5, 2), nombre("epaisseur", "Diélectrique entre les plans", "mm", 0.2, 0.01, 5, 3),
                nombre("er", "Permittivité", "", 4.4, 1, 15, 2)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double z = x[0] * x[1] / 100 / x[2];
      const double cap = 8.8541878128e-12 * x[5] * x[3] * 1e-4 / (x[4] / 1000);
      r.lignes = {{"Impédance cible", si(z, "Ω"), true}, {"Capacité des plans", si(cap, "F")}};
      r.formule = "Z = V × ondulation / ΔI";
      r.explication = "Les condensateurs de découplage et les plans doivent tenir l’impédance sous cette cible jusqu’à la fréquence du saut de courant.";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("isolement", "Isolement", CARTE, "Distance entre conducteurs selon l’IPC-2221 (table 6-1), et ligne de fuite selon la CEI 60664-1.", "IPC-2221B table 6-1 ; CEI 60664-1 table F.4");
    c.champs = {nombre("tension", "Tension (continue ou crête ; efficace pour la CEI)", "V", 600, 0, 5000, 0),
                liste("categorie", "Catégorie IPC-2221", {"B1 couche interne", "B2 externe nue (< 3 050 m)", "B3 externe nue (> 3 050 m)", "B4 externe sous vernis permanent", "A5 externe sous vernis de tropicalisation",
                                                        "A6 pattes de composants nues", "A7 pattes de composants vernies"}, 0),
                liste("pollution", "Degré de pollution (CEI)", {"1", "2", "3"}, 1), liste("groupe", "Groupe de matériau (CEI)", {"I", "II", "IIIa", "IIIb"}, 2)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      static const char* cat[] = {"B1", "B2", "B3", "B4", "A5", "A6", "A7"};
      const double d = isolementIPC2221(x[0], cat[std::clamp(choix(x[1]), 0, 6)]);
      const double f = ligneFuiteCEI60664(x[0], choix(x[2]) + 1, choix(x[3]) + 1, true);
      r.lignes = {{"Isolement IPC-2221", fr(d, 2) + " mm", true}, {"Ligne de fuite CEI 60664-1", fr(f, 2) + " mm"}};
      r.formule = "Table 6-1 ; au-delà de 500 V, la valeur par volt s’ajoute à celle de 500 V";
      r.explication = "La règle d’isolement de la classe de réseaux peut être prise de cette table, dans le gestionnaire de contraintes.";
      r.avis.push_back(avis(NiveauAvis::Info, sourceCEI60664(), "Valeurs de la CEI à confirmer sur le texte de la norme."));
      return r;
    };
    v.push_back(c);
  }
  // ======================== Établi (11.18) ========================
  {
    Calculateur c = fiche("attenuateur", "Atténuateur résistif", ETABLI, "Résistances d’un atténuateur en pi, en T ou en T ponté, pour un affaiblissement et une impédance.", "K = 10^(A/20)");
    c.champs = {nombre("db", "Affaiblissement", "dB", 10, 0.1, 100, 2), nombre("z", "Impédance", "Ω", 50, 1, 1e6, 1), liste("forme", "Forme", {"Pi", "T", "T ponté"}, 0)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double k = std::pow(10.0, x[0] / 20), z = x[1];
      const int f = choix(x[2]);
      if (f == 0) {
        const Attenuateur a = attenuateurPi(x[0], z);
        r.lignes = {{"Résistances aux deux bouts (vers la masse)", si(a.parallele, "Ω"), true}, {"Résistance série", si(a.serie, "Ω"), true},
                    {"Normalisées", voisines(a.parallele, 96, "Ω") + " ; " + voisines(a.serie, 96, "Ω")}};
      } else if (f == 1) {
        const Attenuateur a = attenuateurT(x[0], z);
        r.lignes = {{"Résistances série aux deux bouts", si(a.serie, "Ω"), true}, {"Résistance vers la masse", si(a.parallele, "Ω"), true},
                    {"Normalisées", voisines(a.serie, 96, "Ω") + " ; " + voisines(a.parallele, 96, "Ω")}};
      } else {
        r.lignes = {{"Pont (entre entrée et sortie)", si(z * (k - 1), "Ω"), true}, {"Vers la masse", si(z / (k - 1), "Ω"), true}, {"Deux résistances égales", si(z, "Ω")}};
      }
      r.formule = "pi : Rparallèle = Z (K + 1)/(K − 1), Rsérie = Z (K² − 1)/(2K) ; T : Rsérie = Z (K − 1)/(K + 1), Rparallèle = 2 Z K/(K² − 1)";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("bobine", "Bobine", ETABLI, "Inductance d’une bobine à air (monocouche, multicouche) ou sur noyau (tore, ferrite, noyau E, poudre de fer), et spires pour une inductance voulue.",
                          "Lundin (1985), Wheeler (1928), L = AL N²");
    c.champs = {liste("genre", "Bobine", {"À air, une couche", "À air, plusieurs couches", "Sur noyau (AL)"}, 0), nombre("spires", "Spires", "", 20, 1, 1e5, 0),
                nombre("diametre", "Diamètre moyen", "mm", 10, 0.1, 1000, 2), nombre("longueur", "Longueur du bobinage", "mm", 20, 0.1, 1000, 2),
                nombre("profondeur", "Épaisseur du bobinage (plusieurs couches)", "mm", 3, 0.01, 500, 2), nombre("al", "AL du noyau", "nH/spire²", 100, 0.01, 1e6, 2),
                nombre("voulue", "Inductance voulue (0 : aucune)", "µH", 0, 0, 1e6, 3)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const int g = choix(x[0]);
      const double n = x[1];
      auto inductance = [&](double spires) {
        if (g == 0) return bobineAir(x[2], x[3], spires);
        if (g == 1) {
          const double a = x[2] / 2 / 25.4, b = x[3] / 25.4, cc = x[4] / 25.4;
          return 0.8 * a * a * spires * spires / (6 * a + 9 * b + 10 * cc) * 1e-6;
        }
        return x[5] * 1e-9 * spires * spires;
      };
      const double l = inductance(n);
      r.lignes = {{"Inductance", si(l, "H"), true}};
      if (x[6] > 0) {
        // Spires pour l'inductance voulue : L croît comme N² (à air, presque)
        double s = n * std::sqrt(x[6] * 1e-6 / l);
        for (int k = 0; k < 20; ++k) s *= std::sqrt(x[6] * 1e-6 / inductance(s));
        r.lignes.push_back({"Spires pour " + si(x[6] * 1e-6, "H"), fr(s, 1) + " (arrondi : " + std::to_string(static_cast<long>(std::lround(s))) + ")", true});
      }
      if (g == 0) r.lignes.push_back({"Coefficient de Nagaoka", fr(nagaoka(x[2] / x[3]), 4)});
      r.formule = g == 0 ? "L = μ0 π a² N² K / b, K de Nagaoka par Lundin" : g == 1 ? "L (µH) = 0,8 a² N² / (6a + 9b + 10c), a, b, c en pouces" : "L = AL × N²";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("resonance", "Résonance et adaptation en pi", ETABLI, "Fréquence de résonance LC, et réseau en pi qui adapte deux résistances à une fréquence.", "f0 = 1/(2π√(LC))");
    c.champs = {nombre("l", "Inductance", "µH", 10, 1e-6, 1e6, 4), nombre("c", "Capacité", "pF", 100, 1e-3, 1e9, 3), nombre("rs", "Résistance de la source", "Ω", 50, 0.1, 1e6, 2),
                nombre("rl", "Résistance de la charge", "Ω", 10, 0.1, 1e6, 2), nombre("q", "Facteur de qualité voulu", "", 5, 0.5, 100, 2), nombre("f", "Fréquence d’adaptation", "MHz", 10, 1e-6, 1e5, 4)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double l = x[0] * 1e-6, cap = x[1] * 1e-12, f = x[5] * 1e6, w = 2 * PI * f;
      r.lignes = {{"Résonance", si(1 / (2 * PI * std::sqrt(l * cap)), "Hz"), true}};
      const double rh = std::max(x[2], x[3]), rv = rh / (x[4] * x[4] + 1);
      if (rv < std::min(x[2], x[3])) {
        const double q1 = std::sqrt(x[2] / rv - 1), q2 = std::sqrt(x[3] / rv - 1);
        r.lignes.push_back({"C côté source", si(q1 / (w * x[2]), "F")});
        r.lignes.push_back({"L série", si((q1 + q2) * rv / w, "H")});
        r.lignes.push_back({"C côté charge", si(q2 / (w * x[3]), "F")});
      } else
        r.lignes.push_back({"Adaptation en pi", "Q trop faible pour ces résistances : augmente-le"});
      r.formule = "pi passe-bas : Rv = Rmax/(Q² + 1), Q1 = √(Rs/Rv − 1), Q2 = √(Rl/Rv − 1), C1 = Q1/(ω Rs), L = (Q1 + Q2) Rv/ω, C2 = Q2/(ω Rl)";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("petits-calculs", "Petits calculs", ETABLI, "Réactances, charge d’un condensateur, gains d’amplificateur, quartz, décibels et longueur d’onde.", "");
    c.champs = {nombre("f", "Fréquence", "Hz", 1000, 1e-3, 1e12, 3), nombre("l", "Inductance", "µH", 100, 1e-6, 1e9, 3), nombre("c", "Capacité", "nF", 100, 1e-6, 1e9, 3),
                nombre("r", "Résistance (charge RC)", "kΩ", 10, 1e-3, 1e6, 3), nombre("r1", "R1 de l’ampli (entrée)", "kΩ", 1, 1e-3, 1e6, 3), nombre("r2", "R2 de l’ampli (contre-réaction)", "kΩ", 10, 1e-3, 1e6, 3),
                nombre("cl", "Capacité de charge du quartz", "pF", 18, 1, 100, 2), nombre("cpar", "Capacités parasites", "pF", 5, 0, 50, 2), nombre("ratio", "Rapport de tensions", "", 2, 1e-9, 1e9, 4),
                nombre("er", "Permittivité (longueur d’onde)", "", 1, 1, 100, 2)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double f = x[0], w = 2 * PI * f, l = x[1] * 1e-6, cap = x[2] * 1e-9, rr = x[3] * 1e3;
      r.lignes = {{"XL", si(w * l, "Ω")}, {"XC", si(1 / (w * cap), "Ω")}, {"Résonance de L et C", si(1 / (2 * PI * std::sqrt(l * cap)), "Hz")},
                  {"Constante RC", si(rr * cap, "s") + " (63 % ; 99 % en 5 RC)"}, {"Gain inverseur", fr(-x[5] / x[4], 3) + " (" + fr(20 * std::log10(x[5] / x[4]), 2) + " dB)"},
                  {"Gain non inverseur", fr(1 + x[5] / x[4], 3) + " (" + fr(20 * std::log10(1 + x[5] / x[4]), 2) + " dB)"},
                  {"Condensateurs du quartz", si(std::max(0.0, 2 * (x[6] - x[7])) * 1e-12, "F") + " chacun"},
                  {"Rapport en décibels", fr(20 * std::log10(x[8]), 3) + " dB (tension), " + fr(10 * std::log10(x[8]), 3) + " dB (puissance)"},
                  {"Longueur d’onde", si(C0 / (f * std::sqrt(x[9])), "m")}};
      r.formule = "XL = 2πfL ; XC = 1/(2πfC) ; quartz : C = 2 (CL − Cparasites)";
      return r;
    };
    v.push_back(c);
  }
  {
    std::vector<std::string> cl = couleurs();
    Calculateur c = fiche("couleurs", "Code des couleurs des résistances", ETABLI, "Valeur et tolérance lues sur les anneaux (4 ou 5).", "CEI 60062");
    c.champs = {liste("nombre", "Anneaux", {"4 anneaux", "5 anneaux"}, 0), liste("a1", "1er anneau", cl, 4), liste("a2", "2e anneau", cl, 7), liste("a3", "3e anneau", cl, 2), liste("a4", "4e anneau", cl, 10),
                liste("a5", "5e anneau (5 anneaux)", cl, 1)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const auto& cl = couleurs();
      std::vector<std::string> a;
      const int n = choix(x[0]) == 0 ? 4 : 5;
      for (int i = 0; i < n; ++i) a.push_back(cl[static_cast<std::size_t>(std::clamp(choix(x[static_cast<std::size_t>(i) + 1]), 0, static_cast<int>(cl.size()) - 1))]);
      double ohms = 0, tol = 0;
      if (!valeurDesAnneaux(a, ohms, tol)) {
        r.explication = "Une couleur n’est pas permise à sa place (chiffre : noir à blanc ; multiplicateur : jusqu’à argent ; tolérance : marron, rouge, vert, bleu, violet, gris, or, argent).";
        return r;
      }
      r.lignes = {{"Valeur", si(ohms, "Ω") + " à " + fr(tol * 100, tol < 0.01 ? 2 : 0) + " %", true}, {"Série E conseillée", "E" + std::to_string(serieDeTolerance(tol * 100))}};
      r.formule = "chiffres, puis multiplicateur, puis tolérance";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("couleurs-inverse", "Anneaux d’une valeur", ETABLI, "Couleurs des anneaux d’une résistance de valeur et de tolérance données.", "CEI 60062");
    c.champs = {nombre("valeur", "Valeur", "Ω", 4700, 0.01, 1e10, 3), liste("tolerance", "Tolérance", {"5 %", "1 %", "2 %", "10 %", "0,5 %"}, 0), liste("nombre", "Anneaux", {"4 anneaux", "5 anneaux"}, 0)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      static const double t[] = {0.05, 0.01, 0.02, 0.10, 0.005};
      const double tol = t[std::clamp(choix(x[1]), 0, 4)];
      const auto a = anneauxDe(x[0], tol, choix(x[2]) == 0 ? 4 : 5);
      std::string s;
      for (std::size_t i = 0; i < a.size(); ++i) s += (i ? ", " : "") + a[i];
      r.lignes = {{"Anneaux", s, true}, {"Normalisées voisines", voisines(x[0], serieDeTolerance(tol * 100), "Ω")}};
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("forets", "Forets et filetages", ETABLI, "Perçage d’une patte de composant, et cotes d’un filetage métrique (avant-trou, passage).", "IPC-2222 ; ISO 261, ISO 273");
    c.champs = {nombre("patte", "Diamètre de la patte", "mm", 0.8, 0.1, 10, 2), liste("filetage", "Filetage", {"M1,6", "M2", "M2,5", "M3", "M4", "M5", "M6", "M8"}, 3)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      static const double forets[] = {0.3, 0.35, 0.4, 0.45, 0.5, 0.55, 0.6, 0.65, 0.7, 0.75, 0.8, 0.85, 0.9, 0.95, 1.0, 1.05, 1.1, 1.15, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0, 2.2, 2.4, 2.5, 2.8, 3.0, 3.2, 3.5, 4.0};
      const double fini = x[0] + 0.25, foret = fini + 0.1;
      double choisi = forets[std::size(forets) - 1];
      for (double f : forets)
        if (f + 1e-9 >= foret) {
          choisi = f;
          break;
        }
      struct Filetage {
        double pas, avantTrou, passage;
      };
      static const Filetage fil[] = {{0.35, 1.25, 1.8}, {0.4, 1.6, 2.4}, {0.45, 2.05, 2.9}, {0.5, 2.5, 3.4}, {0.7, 3.3, 4.5}, {0.8, 4.2, 5.5}, {1.0, 5.0, 6.6}, {1.25, 6.8, 9.0}};
      const Filetage& f = fil[std::clamp(choix(x[1]), 0, 7)];
      r.lignes = {{"Trou fini", fr(fini, 2) + " mm", true}, {"Foret (métallisation comprise)", fr(choisi, 2) + " mm", true}, {"Pas du filetage", fr(f.pas, 2) + " mm"},
                  {"Avant-trou de taraudage", fr(f.avantTrou, 2) + " mm"}, {"Trou de passage (moyen)", fr(f.passage, 2) + " mm"}};
      r.formule = "trou fini = patte + 0,25 mm (IPC-2222, niveau B) ; foret = trou fini + 0,1 mm de métallisation";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("aide-memoire-ipc", "Aide-mémoire IPC-6011 et IPC-6012", ETABLI, "Classes de performance et types de cartes.", "IPC-6011, IPC-6012");
    c.champs = {liste("classe", "Classe", {"Classe 1", "Classe 2", "Classe 3"}, 1), liste("type", "Type de carte", {"Type 1", "Type 2", "Type 3", "Type 4", "Type 5", "Type 6"}, 2)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      static const char* classes[] = {"produits électroniques courants : la fonction compte, l’aspect peut avoir des défauts",
                                      "produits de service dédié : fonctionnement prolongé, défauts d’aspect tolérés s’ils ne gênent pas",
                                      "haute fiabilité : fonctionnement sans interruption exigé (médical, aéronautique, sécurité)"};
      static const char* types[] = {"simple face", "double face", "multicouche sans via borgne ni enterrée", "multicouche avec vias borgnes ou enterrées",
                                    "multicouche à âme métallique, sans via borgne ni enterrée", "multicouche à âme métallique avec vias borgnes ou enterrées"};
      r.lignes = {{"Classe " + std::to_string(choix(x[0]) + 1), classes[std::clamp(choix(x[0]), 0, 2)], true}, {"Type " + std::to_string(choix(x[1]) + 1), types[std::clamp(choix(x[1]), 0, 5)], true}};
      r.explication = "La classe se donne au fabricant avec le dossier ; elle fixe les tolérances d’anneau, de vide de métallisation et d’aspect.";
      return r;
    };
    v.push_back(c);
  }
  {
    static const std::vector<std::pair<std::string, double>> metaux = {
        {"Or", 0.00},      {"Argent", 0.15},          {"Nickel, titane", 0.30}, {"Cuivre", 0.35},      {"Laiton, bronze", 0.40}, {"Acier inoxydable 18 % Cr", 0.50},
        {"Chrome, étain (dépôt)", 0.60}, {"Étain-plomb (soudure)", 0.65}, {"Plomb", 0.70}, {"Aluminium série 2000", 0.75}, {"Fer, acier au carbone", 0.85},
        {"Aluminium (autres)", 0.90}, {"Cadmium", 0.95}, {"Acier galvanisé", 1.20}, {"Zinc", 1.25}, {"Magnésium", 1.75}};
    std::vector<std::string> noms;
    for (const auto& m : metaux) noms.push_back(m.first);
    Calculateur c = fiche("potentiels", "Couple galvanique", ETABLI, "Écart d’indice anodique entre deux métaux en contact, avec l’alerte au-delà d’un écart réglable.", "MIL-STD-889 (indice anodique)");
    c.champs = {liste("m1", "Premier métal", noms, 3), liste("m2", "Second métal", noms, 9), nombre("seuil", "Écart permis", "V", 0.25, 0.05, 1, 2, "0,15 V en milieu sévère, 0,25 V en milieu normal, 0,5 V en milieu contrôlé")};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const auto& a = metaux[static_cast<std::size_t>(std::clamp(choix(x[0]), 0, static_cast<int>(metaux.size()) - 1))];
      const auto& b = metaux[static_cast<std::size_t>(std::clamp(choix(x[1]), 0, static_cast<int>(metaux.size()) - 1))];
      const double e = std::abs(a.second - b.second);
      r.lignes = {{"Écart d’indice anodique", fr(e, 2) + " V", true}, {"Métal qui se corrode", a.second > b.second ? a.first : b.second > a.second ? b.first : std::string("aucun")}};
      if (e > x[2]) r.avis.push_back(avis(NiveauAvis::Alerte, "MIL-STD-889", "Écart au-delà du seuil : isole les deux métaux ou protège le plus anodique."));
      else r.avis.push_back(avis(NiveauAvis::Info, "MIL-STD-889", "Couple acceptable pour ce seuil."));
      r.explication = "Valeurs à confirmer sur le texte de la norme ; le milieu (humidité, sel) décide du seuil.";
      return r;
    };
    v.push_back(c);
  }
  // ======================== Énergie et chaleur (11.19) ========================
  {
    Calculateur c = fiche("fil", "Fil et nappe", ENERGIE, "Résistance, effet de peau, chute et pertes d’un fil de cuivre (jauge AWG), ou d’un conducteur de nappe.", "CEI 60028 ; ASTM B258");
    c.champs = {nombre("awg", "Jauge AWG", "", 24, -3, 40, 0), nombre("longueur", "Longueur", "m", 1, 0.001, 1e4, 3), nombre("courant", "Courant", "A", 1, 0, 1000, 3),
                nombre("temperature", "Température du conducteur", "°C", 20, -55, 200, 0), nombre("f", "Fréquence", "Hz", 0, 0, 1e9, 0), nombre("conducteurs", "Conducteurs de la nappe", "", 1, 1, 100, 0)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      const double d = diametreAwg(x[0]), s = PI * d * d / 4;
      const double rho = RHO_CU * (1 + ALPHA_CU * (x[3] - 20));
      const double rcc = rho * x[1] / (s * 1e-6);
      double rapport = 1, delta = 0;
      if (x[4] > 0) {
        delta = std::sqrt(rho / (PI * x[4] * MU0));
        rapport = rapportPeau(d / 2000, delta);
      }
      const double rca = rcc * rapport;
      r.lignes = {{"Diamètre", fr(d, 3) + " mm (" + fr(s, 3) + " mm²)"}, {"Résistance continue", si(rcc, "Ω"), true}, {"Résistance par mètre", si(rcc / x[1], "Ω/m")},
                  {"Chute de tension (aller)", si(rca * x[2], "V")}, {"Pertes", si(rca * x[2] * x[2], "W")}};
      if (x[4] > 0) {
        r.lignes.push_back({"Profondeur de peau", si(delta, "m")});
        r.lignes.push_back({"Rca / Rcc", fr(rapport, 3)});
      }
      if (x[5] > 1) r.lignes.push_back({"Nappe : aller et retour sur deux conducteurs", si(2 * rca, "Ω")});
      // Courant que le fil tient à l'air libre (convection naturelle, h ≈ 10 W/(m²·K), 30 K d'échauffement)
      const double imax = std::sqrt(10 * PI * d / 1000 * 30 / (rho / (s * 1e-6)));
      r.lignes.push_back({"Courant à l’air libre (30 K, estimé)", fr(imax, 2) + " A"});
      r.formule = "d = 0,127 mm × 92^((36 − n)/39) ; ρ = 1/58 Ω·mm²/m à 20 °C, + 0,393 %/K ; Rca/Rcc par les fonctions de Kelvin";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("autonomie", "Autonomie sur pile ou batterie", ENERGIE, "Courant moyen de modes qui se répètent, part de chaque mode, autonomie selon la chimie, l’autodécharge et le rendement.",
                          "moyenne pondérée des modes");
    c.champs = {nombre("i1", "Mode 1 : courant", "mA", 0.02, 0, 1e5, 4), nombre("t1", "Mode 1 : durée", "s", 59, 0, 1e7, 3), nombre("i2", "Mode 2 : courant", "mA", 15, 0, 1e5, 4),
                nombre("t2", "Mode 2 : durée", "s", 1, 0, 1e7, 3), nombre("i3", "Mode 3 : courant", "mA", 0, 0, 1e5, 4), nombre("t3", "Mode 3 : durée", "s", 0, 0, 1e7, 3),
                nombre("capacite", "Capacité", "mAh", 2000, 1, 1e7, 0),
                liste("chimie", "Chimie", {"Lithium-ion (2 %/mois)", "LiFePO4 (3 %/mois)", "NiMH (20 %/mois)", "NiMH faible autodécharge (2 %/mois)", "Alcaline (0,2 %/mois)", "Lithium primaire (0,1 %/mois)", "Plomb (4 %/mois)"}, 0),
                nombre("rendement", "Rendement du régulateur", "%", 100, 10, 100, 1)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      static const double auto_[] = {0.02, 0.03, 0.20, 0.02, 0.002, 0.001, 0.04};
      std::vector<ModeConsommation> m;
      for (int k = 0; k < 3; ++k)
        if (x[static_cast<std::size_t>(2 * k + 1)] > 0) m.push_back({x[static_cast<std::size_t>(2 * k)] / 1000, x[static_cast<std::size_t>(2 * k + 1)]});
      const Autonomie a = autonomie(m, x[6], auto_[std::clamp(choix(x[7]), 0, 6)], x[8] / 100);
      if (a.courantMoyen <= 0) {
        r.explication = "Donne au moins un mode, avec son courant et sa durée.";
        return r;
      }
      // Heures et jours entiers, arrondis au-dessous : l'autonomie n'est jamais surestimée
      r.lignes = {{"Courant moyen", si(a.courantMoyen, "A"), true},
                  {"Autonomie hors autodécharge et rendement", fr(std::floor(a.heures), 0) + " h (" + fr(std::floor(a.heures / 24), 0) + " jours)", true},
                  {"Autonomie réelle", fr(std::floor(a.heuresReelles), 0) + " h (" + fr(std::floor(a.heuresReelles / 24), 0) + " jours)"}};
      for (std::size_t k = 0; k < a.parts.size(); ++k) r.lignes.push_back({"Part du mode " + std::to_string(k + 1), fr(100 * a.parts[k], 1) + " % de la charge"});
      r.formule = "I moyen = Σ Ii ti / Σ ti ; autonomie = capacité / I moyen";
      r.explication = "L’autonomie réelle retire l’autodécharge de la chimie et divise par le rendement du régulateur ; le froid réduit encore la capacité.";
      return r;
    };
    v.push_back(c);
  }
  {
    Calculateur c = fiche("thermique", "Chaîne thermique et radiateur", ENERGIE, "Température de jonction d’une pièce et de son radiateur, résistance maximale du radiateur, et celle d’un radiateur à ailettes.",
                          "Tj = Ta + P × ΣRth ; Bar-Cohen et Rohsenow (1984)");
    c.champs = {nombre("p", "Puissance dissipée", "W", 5, 0.001, 1e4, 3), nombre("ta", "Température ambiante", "°C", 40, -55, 150, 1), nombre("rjc", "Rth jonction-boîtier", "K/W", 1.5, 0, 100, 2),
                nombre("rcs", "Rth boîtier-radiateur (pâte, isolant)", "K/W", 0.5, 0, 100, 2), nombre("rsa", "Rth radiateur-air (0 : sans radiateur)", "K/W", 8, 0, 200, 2),
                nombre("tjmax", "Tj maximale", "°C", 150, 50, 250, 0), nombre("ailettes", "Ailettes (0 : pas de calcul)", "", 0, 0, 100, 0), nombre("hauteur", "Hauteur des ailettes", "mm", 20, 1, 500, 1),
                nombre("longueur", "Longueur des ailettes (verticale)", "mm", 50, 1, 1000, 1), nombre("largeur", "Largeur de la semelle", "mm", 40, 1, 1000, 1),
                nombre("epaisseur", "Épaisseur d’une ailette", "mm", 1.5, 0.1, 20, 2)};
    c.calculer = [](const V& x) {
      ResultatCalcul r;
      double rsa = x[4];
      // Radiateur à ailettes verticales en convection naturelle : coefficient de Bar-Cohen et Rohsenow, efficacité d'ailette
      if (x[6] >= 2) {
        const double n = x[6], h = x[7] / 1000, l = x[8] / 1000, w = x[9] / 1000, t = x[10] / 1000;
        const double s = (w - n * t) / (n - 1);
        if (s > 0) {
          double dt = 40, r0 = 10;
          for (int k = 0; k < 30; ++k) {
            const double tf = x[1] + dt / 2 + 273.15, beta = 1 / tf;
            const double nu = 1.6e-5 * std::pow(tf / 300, 1.75), alpha = nu / 0.71, kAir = 0.0263 * std::pow(tf / 300, 0.82);
            const double el = 9.81 * beta * dt * std::pow(s, 4) / (nu * alpha * l);
            const double nus = std::pow(576 / (el * el) + 2.873 / std::sqrt(el), -0.5);
            const double hc = nus * kAir / s;
            const double m = std::sqrt(2 * hc / (200 * t)), eta = std::tanh(m * h) / (m * h);
            const double surface = n * 2 * h * l * eta + (w - n * t) * l;
            r0 = 1 / (hc * surface);
            dt = std::max(1.0, x[0] * r0);
          }
          rsa = r0;
          r.lignes.push_back({"Radiateur à ailettes", fr(r0, 2) + " K/W (écart entre ailettes " + fr(s * 1000, 1) + " mm)"});
        }
      }
      const double total = x[2] + x[3] + rsa;
      const double tj = x[1] + x[0] * (rsa > 0 ? total : x[2] + 40);
      const double rmax = (x[5] - x[1]) / x[0] - x[2] - x[3];
      r.lignes.insert(r.lignes.begin(), {{"Température de jonction", fr(tj, 1) + " °C", true}, {"Rth radiateur maximale", rmax > 0 ? fr(rmax, 2) + " K/W" : std::string("impossible : la jonction dépasse sans radiateur"), true}});
      r.formule = "Tj = Ta + P × (Rjc + Rcs + Rsa) ; ailettes : Nu = (576/El² + 2,873/√El)^(−1/2)";
      r.explication = "La température ambiante est celle de l’air autour de la pièce, dans le boîtier : saisis-la tant que le boîtier ne l’estime pas.";
      if (tj > x[5]) r.avis.push_back(avis(NiveauAvis::Erreur, "Fiche de la pièce", "La jonction dépasse sa température maximale : prends un radiateur plus grand ou réduis la puissance."));
      else if (tj > x[5] - 25) r.avis.push_back(avis(NiveauAvis::Alerte, "Règle de déclassement", "Moins de 25 °C de marge sous Tj max : la durée de vie baisse."));
      return r;
    };
    v.push_back(c);
  }
  return v;
}
}  // namespace

const std::vector<Calculateur>& calculateursElectroniques() {
  static const std::vector<Calculateur> v = construire();
  return v;
}
const Calculateur* calculateurElectronique(const std::string& id) {
  for (const auto& c : calculateursElectroniques())
    if (c.id == id) return &c;
  return nullptr;
}
const std::vector<std::string>& famillesCalculateursElectroniques() {
  static const std::vector<std::string> v = {COURANTS, CARTE, ETABLI, ENERGIE};
  return v;
}

std::vector<std::string> anneauxDe(double ohms, double tolerance, int anneaux) {
  const int chiffres = anneaux >= 5 ? 3 : 2;
  if (!(ohms > 0)) return {};
  int expo = static_cast<int>(std::floor(std::log10(ohms))) - (chiffres - 1);
  long mantisse = std::lround(ohms / std::pow(10.0, expo));
  if (mantisse >= static_cast<long>(std::pow(10, chiffres))) mantisse /= 10, ++expo;
  std::vector<std::string> r;
  const std::string m = std::to_string(mantisse);
  for (const char ch : m) r.push_back(couleurs()[static_cast<std::size_t>(ch - '0')]);
  r.push_back(expo == -1 ? "or" : expo == -2 ? "argent" : expo >= 0 && expo <= 9 ? couleurs()[static_cast<std::size_t>(expo)] : "?");
  std::string t = "or";
  double meilleur = 1e9;
  for (const auto& [c, v] : tolerancesCouleur())
    if (std::abs(v - tolerance) < meilleur) meilleur = std::abs(v - tolerance), t = c;
  r.push_back(t);
  return r;
}

bool valeurDesAnneaux(const std::vector<std::string>& c, double& ohms, double& tolerance) {
  if (c.size() != 4 && c.size() != 5) return false;
  const std::size_t chiffres = c.size() == 5 ? 3 : 2;
  long m = 0;
  for (std::size_t i = 0; i < chiffres; ++i) {
    const int k = indiceCouleur(c[i]);
    if (k < 0 || k > 9) return false;
    m = m * 10 + k;
  }
  const int mult = indiceCouleur(c[chiffres]);
  double f = 0;
  if (mult >= 0 && mult <= 9) f = std::pow(10.0, mult);
  else if (c[chiffres] == "or") f = 0.1;
  else if (c[chiffres] == "argent") f = 0.01;
  else return false;
  const auto t = tolerancesCouleur().find(c[chiffres + 1]);
  if (t == tolerancesCouleur().end()) return false;
  ohms = static_cast<double>(m) * f;
  tolerance = t->second;
  return true;
}

Attenuateur attenuateurPi(double db, double z) {
  const double k = std::pow(10.0, db / 20);
  return {z * (k * k - 1) / (2 * k), z * (k + 1) / (k - 1)};
}
Attenuateur attenuateurT(double db, double z) {
  const double k = std::pow(10.0, db / 20);
  return {z * (k - 1) / (k + 1), z * 2 * k / (k * k - 1)};
}

Autonomie autonomie(const std::vector<ModeConsommation>& modes, double capaciteMah, double autodechargeMois, double rendement) {
  Autonomie a;
  double q = 0, t = 0;
  for (const auto& m : modes) q += m.courant * m.duree, t += m.duree;
  if (t <= 0 || q <= 0) return a;
  a.courantMoyen = q / t;
  for (const auto& m : modes) a.parts.push_back(m.courant * m.duree / q);
  const double capacite = capaciteMah / 1000;  // Ah
  a.heures = capacite / a.courantMoyen;
  const double fuite = capacite * autodechargeMois / (30.4375 * 24);  // A équivalents
  a.heuresReelles = capacite / (a.courantMoyen / std::max(rendement, 0.01) + fuite);
  return a;
}

// ---------- Piste et via choisies ----------
std::string ProprietesCuivre::resume() const {
  std::string s = fr(longueur, 2) + " mm";
  if (delai > 0) s += " · " + si(delai, "s");
  s += " · " + si(resistance, "Ω");
  if (courantAdmissible > 0) s += " · " + fr(courantAdmissible, 2) + " A";
  return s;
}

namespace {
// Épaisseur du cuivre, permittivité et hauteur du diélectrique voisin d'un cuivre de l'empilage
void voisinage(const ConceptionElec& c, Couche couche, double& cuivre, double& er, double& h, bool& externe) {
  const Empilage& e = c.circuit.empilage;
  cuivre = e.cuivre, er = 4.4, h = std::max(0.05, e.epaisseur / 2);
  const int n = std::max(1, e.couchesCuivre);
  const int rang = rangCuivre(couche, n);
  externe = rang <= 0 || rang >= n - 1;
  const auto pile = pileEmpilage(e);
  int k = -1;
  for (std::size_t i = 0; i < pile.size(); ++i) {
    if (pile[i].genre != GenreCoucheEmpilage::Cuivre) continue;
    if (++k == rang) {
      cuivre = pile[i].epaisseur > 0 ? pile[i].epaisseur : cuivre;
      const std::size_t j = rang == n - 1 && i > 0 ? i - 1 : i + 1;
      if (j < pile.size() && pile[j].genre == GenreCoucheEmpilage::Dielectrique) er = pile[j].permittivite, h = pile[j].epaisseur;
      break;
    }
  }
}
}  // namespace

ProprietesCuivre proprietesPiste(const ConceptionElec& c, const Piste& p, double echauffement) {
  ProprietesCuivre r;
  double er = 4.4, h = 0.2;
  voisinage(c, p.couche, r.epaisseur, er, h, r.externe);
  r.largeur = mm(p.largeurFin > 0 ? (p.largeur + p.largeurFin) / 2 : p.largeur);
  r.longueur = mm(static_cast<Nm>(std::llround(distance(p.a, p.b))));
  if (p.milieu) r.longueur = mm(static_cast<Nm>(std::llround(distance(p.a, *p.milieu) + distance(*p.milieu, p.b))));
  r.resistance = RHO_CU * (r.longueur / 1000) / (std::max(r.largeur, 1e-6) / 1000 * r.epaisseur / 1000);
  // Délai : microruban dehors (εeff de Hammerstad), triplaque dedans (εr)
  const double eeff = r.externe ? (er + 1) / 2 + (er - 1) / 2 / std::sqrt(1 + 12 * h / std::max(r.largeur, 1e-6)) : er;
  r.delai = r.longueur / 1000 * std::sqrt(eeff) / C0;
  r.courantAdmissible = courantIPC2221(r.largeur, r.epaisseur, echauffement, r.externe);
  return r;
}

ProprietesCuivre proprietesVia(const ConceptionElec& c, const Via& v, double echauffement) {
  ProprietesCuivre r;
  const double paroi = 0.025;  // mm de cuivre déposé
  const Empilage& e = c.circuit.empilage;
  const int n = std::max(1, e.couchesCuivre);
  const int a = rangCuivre(v.de, n), b = rangCuivre(v.vers, n);
  const double fraction = n > 1 && a >= 0 && b >= 0 ? static_cast<double>(std::abs(b - a)) / (n - 1) : 1;
  r.longueur = e.epaisseur * fraction;
  r.largeur = mm(v.percage);
  r.epaisseur = paroi;
  const double section = PI * (r.largeur + paroi) * paroi;  // mm²
  r.resistance = RHO_CU * (r.longueur / 1000) / (section * 1e-6);
  r.courantAdmissible = courantIPC2221(PI * (r.largeur + paroi), paroi, echauffement, true);
  const double hi = r.longueur / 25.4, di = std::max(r.largeur, 0.01) / 25.4, d1 = mm(v.diametre) / 25.4, d2 = d1 + 0.5 / 25.4;
  r.inductance = 5.08 * hi * (std::log(4 * hi / di) + 1) * 1e-9;
  r.capacite = 1.41 * 4.4 * hi * d1 / (d2 - d1) * 1e-12;
  r.delai = r.longueur / 1000 * std::sqrt(4.4) / C0;
  return r;
}

}  // namespace prom::ecad
