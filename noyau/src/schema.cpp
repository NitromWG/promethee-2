// Prométhée : schéma électronique — bibliothèque de symboles intégrée et géométrie des symboles placés.
// Licence GPL-3.0-only.
#include "promethee/schema.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

namespace prom::ecad {

const char* codeTypeBroche(TypeBroche t) {
  switch (t) {
    case TypeBroche::Entree: return "entree";
    case TypeBroche::Sortie: return "sortie";
    case TypeBroche::Bidirectionnelle: return "bidirectionnelle";
    case TypeBroche::TroisEtats: return "trois-etats";
    case TypeBroche::Passive: return "passive";
    case TypeBroche::AlimEntree: return "alim-entree";
    case TypeBroche::AlimSortie: return "alim-sortie";
    case TypeBroche::CollecteurOuvert: return "collecteur-ouvert";
    case TypeBroche::NonConnectee: return "non-connectee";
    case TypeBroche::NonSpecifiee: return "non-specifiee";
  }
  return "passive";
}

const char* codeSensSignal(SensSignal s) {
  switch (s) {
    case SensSignal::Aucun: return "";
    case SensSignal::Entree: return "entree";
    case SensSignal::Sortie: return "sortie";
    case SensSignal::Bidirectionnel: return "bidirectionnel";
    case SensSignal::TroisEtats: return "trois-etats";
    case SensSignal::Passif: return "passif";
  }
  return "";
}

SensSignal sensSignalDepuis(const std::string& code) {
  for (int i = 1; i <= static_cast<int>(SensSignal::Passif); ++i)
    if (code == codeSensSignal(static_cast<SensSignal>(i))) return static_cast<SensSignal>(i);
  return SensSignal::Aucun;
}

const char* nomTypeBroche(TypeBroche t) {
  switch (t) {
    case TypeBroche::Entree: return "Entrée";
    case TypeBroche::Sortie: return "Sortie";
    case TypeBroche::Bidirectionnelle: return "Bidirectionnelle";
    case TypeBroche::TroisEtats: return "Trois états";
    case TypeBroche::Passive: return "Passive";
    case TypeBroche::AlimEntree: return "Entrée d’alimentation";
    case TypeBroche::AlimSortie: return "Sortie d’alimentation";
    case TypeBroche::CollecteurOuvert: return "Collecteur ouvert";
    case TypeBroche::NonConnectee: return "Non connectée";
    case TypeBroche::NonSpecifiee: return "Non spécifiée";
  }
  return "Passive";
}

TypeBroche typeBrocheDepuis(const std::string& code) {
  for (int i = 0; i <= static_cast<int>(TypeBroche::NonSpecifiee); ++i)
    if (code == codeTypeBroche(static_cast<TypeBroche>(i))) return static_cast<TypeBroche>(i);
  return TypeBroche::Passive;
}

namespace {
using T = TypeBroche;
Broche B(const char* num, const char* nom, TypeBroche t, double x, double y, int dir, double l = 2.54, bool nomVisible = true, bool numeroVisible = true) {
  Broche b;
  b.numero = num;
  b.nom = nom;
  b.type = t;
  b.extremite = ptMm(x, y);
  b.direction = dir;
  b.longueur = nm(l);
  b.nomVisible = nomVisible;
  b.numeroVisible = numeroVisible;
  return b;
}
Broche Bp(const char* num, double x, double y, int dir, double l) { return B(num, "~", T::Passive, x, y, dir, l, false, false); }
Trace Ligne(std::initializer_list<std::pair<double, double>> pts, bool ferme = false, bool rempli = false) {
  Trace t;
  t.forme = Trace::Forme::Polyligne;
  for (const auto& [x, y] : pts) t.points.push_back(ptMm(x, y));
  t.ferme = ferme || rempli;
  t.rempli = rempli;
  return t;
}
Trace Rect(double x0, double y0, double x1, double y1, bool fond = false) {
  Trace t;
  t.forme = Trace::Forme::Rectangle;
  t.points = {ptMm(x0, y0), ptMm(x1, y1)};
  t.fond = fond;
  return t;
}
Trace Rond(double cx, double cy, double r, bool rempli = false) {
  Trace t;
  t.forme = Trace::Forme::Cercle;
  t.points = {ptMm(cx, cy)};
  t.rayon = nm(r);
  t.rempli = rempli;
  return t;
}
Trace Arc(double cx, double cy, double r, double a0, double a1) {
  Trace t;
  t.forme = Trace::Forme::Arc;
  t.points = {ptMm(cx, cy)};
  t.rayon = nm(r);
  t.debut = a0;
  t.fin = a1;
  return t;
}
Trace Txt(double x, double y, const char* texte, double taille = 1.27) {
  Trace t;
  t.forme = Trace::Forme::Texte;
  t.points = {ptMm(x, y)};
  t.texte = texte;
  t.taille = nm(taille);
  return t;
}
DefSymbole S(const char* id, const char* nom, const char* categorie, const char* prefixe, const char* empreinte, const char* description, const char* motsCles = "") {
  DefSymbole d;
  d.id = id;
  d.nom = nom;
  d.categorie = categorie;
  d.prefixe = prefixe;
  d.empreinte = empreinte;
  d.description = description;
  d.motsCles = motsCles;
  return d;
}

std::vector<DefSymbole> construire() {
  std::vector<DefSymbole> v;
  // ----- Passifs -----
  {
    DefSymbole s = S("R", "Résistance", "Passifs", "R", "R_0603", "Résistance", "resistance ohm");
    s.broches = {Bp("1", 0, -3.81, 1, 1.27), Bp("2", 0, 3.81, 3, 1.27)};
    s.traces = {Rect(-1.016, -2.54, 1.016, 2.54)};
    v.push_back(s);
  }
  {
    DefSymbole s = S("C", "Condensateur", "Passifs", "C", "C_0603", "Condensateur non polarisé", "condensateur capacite ceramique");
    s.broches = {Bp("1", 0, -3.81, 1, 3.048), Bp("2", 0, 3.81, 3, 3.048)};
    s.traces = {Ligne({{-2.032, -0.762}, {2.032, -0.762}}), Ligne({{-2.032, 0.762}, {2.032, 0.762}})};
    v.push_back(s);
  }
  {
    DefSymbole s = S("C_Pol", "Condensateur polarisé", "Passifs", "C", "CP_Radial_D6.3_P2.50", "Condensateur électrolytique (broche 1 : +)", "electrolytique chimique tantale");
    s.broches = {Bp("1", 0, -3.81, 1, 3.048), Bp("2", 0, 3.81, 3, 2.794)};
    s.traces = {Ligne({{-2.032, -0.762}, {2.032, -0.762}}), Rect(-2.032, 0.508, 2.032, 1.016), Txt(-1.6, -1.8, "+", 1.27)};
    s.traces[1].rempli = true;
    v.push_back(s);
  }
  {
    DefSymbole s = S("L", "Inductance", "Passifs", "L", "L_0805", "Inductance, self", "bobine self ferrite");
    s.broches = {Bp("1", 0, -3.81, 1, 1.27), Bp("2", 0, 3.81, 3, 1.27)};
    for (double y : {-1.905, -0.635, 0.635, 1.905}) s.traces.push_back(Arc(0, y, 0.635, -90, 90));
    v.push_back(s);
  }
  {
    DefSymbole s = S("Fusible", "Fusible", "Passifs", "F", "Fuse_1206", "Fusible", "fuse protection");
    s.broches = {Bp("1", 0, -3.81, 1, 1.27), Bp("2", 0, 3.81, 3, 1.27)};
    s.traces = {Rect(-0.762, -2.54, 0.762, 2.54), Ligne({{0, -2.54}, {0, 2.54}})};
    v.push_back(s);
  }
  {
    DefSymbole s = S("Quartz", "Quartz", "Passifs", "Y", "Crystal_HC49-4H", "Quartz, résonateur", "crystal oscillateur horloge");
    s.broches = {Bp("1", -3.81, 0, 0, 2.286), Bp("2", 3.81, 0, 2, 2.286)};
    s.traces = {Rect(-0.762, -1.524, 0.762, 1.524), Ligne({{-1.524, -1.016}, {-1.524, 1.016}}), Ligne({{1.524, -1.016}, {1.524, 1.016}})};
    s.posReference = ptMm(0, -2.54);
    s.posValeur = ptMm(0, 2.54);
    v.push_back(s);
  }
  // ----- Semi-conducteurs (broche 1 : cathode des diodes) -----
  auto diode = [&](const char* id, const char* nom, const char* fp, const char* desc, int barre, bool led) {
    DefSymbole s = S(id, nom, led ? "Optoélectronique" : "Semi-conducteurs", "D", fp, desc, led ? "led diode lumiere" : "diode redressement");
    s.broches = {B("1", "K", T::Passive, 3.81, 0, 2, 2.54, false, false), B("2", "A", T::Passive, -3.81, 0, 0, 2.54, false, false)};
    s.traces = {Ligne({{-1.27, -1.27}, {-1.27, 1.27}, {1.27, 0}}, true)};
    if (barre == 0) s.traces.push_back(Ligne({{1.27, -1.27}, {1.27, 1.27}}));
    if (barre == 1) s.traces.push_back(Ligne({{0.762, -1.27}, {1.27, -1.27}, {1.27, 1.27}, {1.778, 1.27}}));
    if (barre == 2) s.traces.push_back(Ligne({{0.762, -0.762}, {0.762, -1.27}, {1.27, -1.27}, {1.27, 1.27}, {1.778, 1.27}, {1.778, 0.762}}));
    if (led) {
      s.traces.push_back(Ligne({{-0.3, -1.6}, {0.7, -2.6}}));
      s.traces.push_back(Ligne({{0.2, -2.6}, {0.7, -2.6}, {0.7, -2.1}}));
      s.traces.push_back(Ligne({{0.7, -1.6}, {1.7, -2.6}}));
      s.traces.push_back(Ligne({{1.2, -2.6}, {1.7, -2.6}, {1.7, -2.1}}));
      s.posReference = ptMm(0, 2.54);
      s.posValeur = ptMm(0, 3.937);
    } else {
      s.posReference = ptMm(0, -2.54);
      s.posValeur = ptMm(0, 2.54);
    }
    v.push_back(s);
  };
  diode("D", "Diode", "D_SOD-123", "Diode (broche 1 : cathode)", 0, false);
  diode("D_Zener", "Diode Zener", "D_SOD-123", "Diode Zener (broche 1 : cathode)", 1, false);
  diode("D_Schottky", "Diode Schottky", "D_SMA", "Diode Schottky (broche 1 : cathode)", 2, false);
  diode("LED", "LED", "LED_0805", "Diode électroluminescente (broche 1 : cathode)", 0, true);
  auto bipolaire = [&](const char* id, const char* nom, bool npn) {
    DefSymbole s = S(id, nom, "Semi-conducteurs", "Q", "SOT-23", npn ? "Transistor NPN (SOT-23 : 1 B, 2 E, 3 C)" : "Transistor PNP (SOT-23 : 1 B, 2 E, 3 C)", "transistor bipolaire");
    s.broches = {B("1", "B", T::Passive, -5.08, 0, 0, 3.175, false), B("3", "C", T::Passive, 0, -5.08, 1, 2.54, false), B("2", "E", T::Passive, 0, 5.08, 3, 2.54, false)};
    s.traces = {Rect(-2.032, -1.905, -1.778, 1.905), Ligne({{-1.905, -0.635}, {0, -2.54}}), Ligne({{-1.905, 0.635}, {0, 2.54}}), Rond(-0.635, 0, 2.794)};
    s.traces[0].rempli = true;
    if (npn) s.traces.push_back(Ligne({{0, 2.54}, {-0.95, 2.35}, {-0.4, 1.75}}, true, true));
    else s.traces.push_back(Ligne({{-1.905, 0.635}, {-0.95, 0.83}, {-1.5, 1.43}}, true, true));
    s.posReference = ptMm(3.048, -1.27);
    s.posValeur = ptMm(3.048, 1.27);
    v.push_back(s);
  };
  bipolaire("Q_NPN", "Transistor NPN", true);
  bipolaire("Q_PNP", "Transistor PNP", false);
  auto mos = [&](const char* id, const char* nom, bool canalN) {
    DefSymbole s = S(id, nom, "Semi-conducteurs", "Q", "SOT-23", canalN ? "MOSFET canal N (SOT-23 : 1 G, 2 S, 3 D)" : "MOSFET canal P (SOT-23 : 1 G, 2 S, 3 D)", "mosfet transistor");
    s.broches = {B("1", "G", T::Passive, -5.08, 0, 0, 3.302, false), B("3", "D", T::Passive, 0, -5.08, 1, 2.54, false), B("2", "S", T::Passive, 0, 5.08, 3, 2.54, false)};
    s.traces = {Ligne({{-1.778, -1.524}, {-1.778, 1.524}}), Ligne({{-1.016, -1.905}, {-1.016, -0.889}}), Ligne({{-1.016, -0.508}, {-1.016, 0.508}}),
                Ligne({{-1.016, 0.889}, {-1.016, 1.905}}), Ligne({{-1.016, -1.397}, {0, -1.397}, {0, -2.54}}), Ligne({{-1.016, 1.397}, {0, 1.397}, {0, 2.54}}),
                Ligne({{-1.016, 0}, {0, 0}, {0, 1.397}}), Rond(-0.762, 0, 2.794)};
    if (canalN) s.traces.push_back(Ligne({{-1.016, 0}, {-0.381, -0.381}, {-0.381, 0.381}}, true, true));
    else s.traces.push_back(Ligne({{-0.2, 0}, {-0.85, -0.381}, {-0.85, 0.381}}, true, true));
    s.posReference = ptMm(3.048, -1.27);
    s.posValeur = ptMm(3.048, 1.27);
    v.push_back(s);
  };
  mos("Q_NMOS", "MOSFET canal N", true);
  mos("Q_PMOS", "MOSFET canal P", false);
  // ----- Circuits intégrés -----
  {
    DefSymbole s = S("AOP", "Amplificateur opérationnel", "Circuits intégrés", "U", "SOT-23-5", "Amplificateur opérationnel simple (brochage SOT-23-5 : 1 +, 2 V−, 3 −, 4 sortie, 5 V+)", "aop ampli lmv321");
    s.broches = {B("3", "-", T::Entree, -7.62, -2.54, 0, 3.81, false), B("1", "+", T::Entree, -7.62, 2.54, 0, 3.81, false), B("4", "~", T::Sortie, 7.62, 0, 2, 3.81, false),
                 B("5", "V+", T::AlimEntree, -1.27, -5.08, 1, 2.54, false), B("2", "V-", T::AlimEntree, -1.27, 5.08, 3, 2.54, false)};
    s.traces = {Ligne({{-3.81, -3.81}, {-3.81, 3.81}, {3.81, 0}}, true), Txt(-2.9, -2.54, "−"), Txt(-2.9, 2.54, "+")};
    s.traces[0].fond = true;
    s.posReference = ptMm(1.27, -3.81);
    s.posValeur = ptMm(1.27, 3.81);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Regulateur", "Régulateur de tension", "Alimentation", "U", "SOT-223", "Régulateur linéaire à trois broches (SOT-223 : 1 GND, 2 sortie, 3 entrée)", "ldo ams1117 regulateur");
    s.broches = {B("3", "VI", T::AlimEntree, -7.62, 0, 0, 2.54), B("2", "VO", T::AlimSortie, 7.62, 0, 2, 2.54), B("1", "GND", T::AlimEntree, 0, 5.08, 3, 2.54)};
    s.traces = {Rect(-5.08, -2.54, 5.08, 2.54, true)};
    s.posReference = ptMm(0, -5.334);
    s.posValeur = ptMm(0, -3.81);
    v.push_back(s);
  }
  {
    DefSymbole s = S("NE555", "NE555", "Circuits intégrés", "U", "SOIC-8", "Temporisateur 555", "timer 555 oscillateur");
    s.broches = {B("8", "VCC", T::AlimEntree, 0, -10.16, 1), B("1", "GND", T::AlimEntree, 0, 10.16, 3), B("4", "R", T::Entree, -10.16, -5.08, 0), B("2", "TR", T::Entree, -10.16, -2.54, 0),
                 B("6", "THR", T::Entree, -10.16, 2.54, 0), B("5", "CV", T::Entree, -10.16, 5.08, 0), B("3", "Q", T::Sortie, 10.16, -2.54, 2), B("7", "DIS", T::CollecteurOuvert, 10.16, 2.54, 2)};
    s.traces = {Rect(-7.62, -7.62, 7.62, 7.62, true)};
    s.posReference = ptMm(2.54, -8.89);
    s.posValeur = ptMm(2.54, 8.89);
    v.push_back(s);
  }
  {
    DefSymbole s = S("ATtiny85", "ATtiny85", "Microcontrôleurs", "U", "SOIC-8", "Microcontrôleur AVR 8 bits, 8 broches", "mcu avr microcontroleur attiny");
    s.broches = {B("8", "VCC", T::AlimEntree, 0, -10.16, 1), B("4", "GND", T::AlimEntree, 0, 12.7, 3), B("5", "PB0", T::Bidirectionnelle, 10.16, -5.08, 2),
                 B("6", "PB1", T::Bidirectionnelle, 10.16, -2.54, 2), B("7", "PB2", T::Bidirectionnelle, 10.16, 0, 2), B("2", "PB3", T::Bidirectionnelle, 10.16, 2.54, 2),
                 B("3", "PB4", T::Bidirectionnelle, 10.16, 5.08, 2), B("1", "PB5/RESET", T::Bidirectionnelle, 10.16, 7.62, 2)};
    s.traces = {Rect(-7.62, -7.62, 7.62, 10.16, true)};
    s.posReference = ptMm(2.54, -8.89);
    s.posValeur = ptMm(2.54, 11.43);
    v.push_back(s);
  }
  // ----- Électromécanique -----
  {
    DefSymbole s = S("Bouton", "Bouton poussoir", "Électromécanique", "SW", "SW_PUSH_6mm", "Bouton poussoir (contact à fermeture)", "interrupteur bouton tactile");
    s.broches = {Bp("1", -5.08, 0, 0, 2.54), Bp("2", 5.08, 0, 2, 2.54)};
    s.traces = {Rond(-2.032, 0, 0.508), Rond(2.032, 0, 0.508), Ligne({{-2.54, -1.27}, {2.54, -1.27}}), Ligne({{0, -1.27}, {0, -2.54}})};
    s.posReference = ptMm(0, -3.81);
    s.posValeur = ptMm(0, 1.905);
    v.push_back(s);
  }
  auto connecteur = [&](int n, bool bornier) {
    char id[40], nom[60], fp[48];
    std::snprintf(id, sizeof id, bornier ? "Bornier_1x%02d" : "Conn_01x%02d", n);
    std::snprintf(nom, sizeof nom, bornier ? "Bornier à vis, %d points" : "Connecteur, %d broches", n);
    std::snprintf(fp, sizeof fp, bornier ? "TerminalBlock_1x%02d_P5.08" : "PinHeader_1x%02d_P2.54", n);
    DefSymbole s = S(id, nom, "Connecteurs", "J", fp, bornier ? "Bornier à vis au pas de 5,08 mm" : "Connecteur une rangée au pas de 2,54 mm", "connecteur barrette");
    const double y0 = -(n - 1) * 1.27;
    for (int i = 0; i < n; ++i) {
      const std::string num = std::to_string(i + 1);
      s.broches.push_back(B(num.c_str(), ("Pin_" + num).c_str(), T::Passive, -5.08, y0 + 2.54 * i, 0, 3.81, false, true));
    }
    s.traces = {Rect(-1.27, y0 - 1.27, 1.27, -y0 + 1.27, true)};
    s.posReference = ptMm(0, y0 - 2.54);
    s.posValeur = ptMm(0, -y0 + 2.54);
    v.push_back(s);
  };
  for (int n : {2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 40}) connecteur(n, false);
  for (int n : {2, 3, 4}) connecteur(n, true);
  {
    DefSymbole s = S("Point_test", "Point de test", "Électromécanique", "TP", "TestPoint_Pad_1.5", "Point de test (pastille de mesure)", "test mesure sonde");
    s.broches = {Bp("1", 0, 2.54, 3, 1.27)};
    s.traces = {Rond(0, 0, 1.27)};
    s.posReference = ptMm(2.032, -0.635);
    s.posValeur = ptMm(2.032, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Trou_fixation", "Trou de fixation M3", "Mécanique", "H", "MountingHole_3.2", "Trou de fixation non métallisé (vis M3)", "fixation vis trou");
    s.mecanique = true;
    s.traces = {Rond(0, 0, 2.54), Rond(0, 0, 1.27)};
    s.posReference = ptMm(3.556, -0.635);
    s.posValeur = ptMm(3.556, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Trou_fixation_masse", "Trou de fixation M3 relié", "Mécanique", "H", "MountingHole_3.2_Pad", "Trou de fixation métallisé, relié à un réseau (masse)", "fixation vis trou masse");
    s.mecanique = true;
    s.broches = {Bp("1", 0, 5.08, 3, 2.54)};
    s.traces = {Rond(0, 0, 2.54), Rond(0, 0, 1.27)};
    s.posReference = ptMm(3.556, -0.635);
    s.posValeur = ptMm(3.556, 1.27);
    v.push_back(s);
  }
  // Symboles sans broche (3.17, SAI-45) : un repère, une ligne de nomenclature, aucune ligne au fichier de placement.
  // L'entretoise marque son trou sur la carte (le boîtier y met un pilier) ; la vis et le dissipateur ne vont qu'à la
  // nomenclature.
  {
    DefSymbole s = S("Entretoise", "Entretoise M3", "Mécanique", "MP", "MountingHole_3.2", "Entretoise de carte M3, posée dans son trou de fixation", "entretoise pilier standoff");
    s.genre = GenreComposant::Mecanique;
    s.traces = {Rect(-1.27, -2.54, 1.27, 2.54, false), Rond(0, 0, 0.635)};
    s.posReference = ptMm(2.286, -0.635);
    s.posValeur = ptMm(2.286, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Vis", "Vis", "Mécanique", "MP", "", "Vis de fixation (nomenclature seule)", "vis ecrou fixation");
    s.genre = GenreComposant::Mecanique;
    s.traces = {Rond(0, 0, 1.27), Ligne({{-0.889, -0.889}, {0.889, 0.889}}), Ligne({{-0.889, 0.889}, {0.889, -0.889}})};
    s.posReference = ptMm(2.032, -0.635);
    s.posValeur = ptMm(2.032, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Dissipateur", "Dissipateur", "Mécanique", "HS", "", "Dissipateur thermique (nomenclature seule)", "dissipateur radiateur heatsink");
    s.genre = GenreComposant::Mecanique;
    s.traces = {Rect(-2.54, -1.27, 2.54, 1.27, false), Ligne({{-1.524, -1.27}, {-1.524, -2.54}}), Ligne({{0, -1.27}, {0, -2.54}}), Ligne({{1.524, -1.27}, {1.524, -2.54}})};
    s.posReference = ptMm(3.302, -0.635);
    s.posValeur = ptMm(3.302, 1.27);
    v.push_back(s);
  }
  // ----- Composants courants -----
  {
    DefSymbole s = S("Potentiometre", "Potentiomètre", "Passifs", "RV", "Trimmer_3296W", "Potentiomètre ou ajustable (2 : curseur)", "potentiometre ajustable trimmer resistance variable");
    s.broches = {Bp("1", 0, -3.81, 1, 1.27), Bp("3", 0, 3.81, 3, 1.27), B("2", "~", T::Passive, 3.81, 0, 2, 1.778, false, true)};
    s.traces = {Rect(-1.016, -2.54, 1.016, 2.54), Ligne({{2.032, 0}, {1.143, 0}}), Ligne({{1.143, 0}, {1.905, -0.508}, {1.905, 0.508}}, true, true)};
    s.posReference = ptMm(-2.032, -1.27);
    s.posValeur = ptMm(-2.032, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Interrupteur", "Interrupteur", "Électromécanique", "SW", "PinHeader_1x02_P2.54", "Interrupteur simple (marche-arrêt), relié par deux fils", "interrupteur marche arret spst");
    s.broches = {Bp("1", -5.08, 0, 0, 2.54), Bp("2", 5.08, 0, 2, 2.54)};
    s.traces = {Rond(-2.032, 0, 0.508), Rond(2.032, 0, 0.508), Ligne({{-1.6, -0.3}, {2.1, -1.65}})};
    s.posReference = ptMm(0, -2.794);
    s.posValeur = ptMm(0, 1.905);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Inverseur", "Interrupteur inverseur", "Électromécanique", "SW", "SW_Slide_SPDT_P2.54", "Inverseur à glissière (2 : commun)", "inverseur glissiere spdt commutateur");
    s.broches = {Bp("2", -5.08, 0, 0, 2.54), Bp("1", 5.08, -2.54, 2, 2.54), Bp("3", 5.08, 2.54, 2, 2.54)};
    s.traces = {Rond(-2.032, 0, 0.508), Rond(2.032, -2.54, 0.508), Rond(2.032, 2.54, 0.508), Ligne({{-1.6, -0.25}, {1.75, -2.0}})};
    s.posReference = ptMm(0, -4.064);
    s.posValeur = ptMm(0, 4.064);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Pile", "Pile ou batterie", "Alimentation", "BT", "PinHeader_1x02_P2.54", "Pile, accumulateur (1 : +), reliée par deux fils", "pile batterie accumulateur lipo");
    s.broches = {B("1", "+", T::AlimSortie, 0, -5.08, 1, 2.54, false, true), B("2", "-", T::Passive, 0, 5.08, 3, 2.54, false, true)};
    s.traces = {Ligne({{-2.032, -2.54}, {2.032, -2.54}}), Ligne({{-1.016, -1.524}, {1.016, -1.524}}), Ligne({{-2.032, 0.508}, {2.032, 0.508}}), Ligne({{-1.016, 1.524}, {1.016, 1.524}}),
                Ligne({{0, -1.524}, {0, -0.889}}), Ligne({{0, -0.254}, {0, 0.508}}), Txt(-2.6, -3.4, "+", 1.27)};
    s.traces[1].rempli = s.traces[3].rempli = true;
    s.posReference = ptMm(2.794, -1.27);
    s.posValeur = ptMm(2.794, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Buzzer", "Buzzer", "Électromécanique", "BZ", "Buzzer_D12_P7.6", "Buzzer (1 : +)", "buzzer sonore bip piezo");
    s.broches = {Bp("1", 0, -5.08, 1, 2.54), Bp("2", 0, 5.08, 3, 2.54)};
    s.traces = {Rond(0, 0, 2.54), Txt(0, 0, "BZ", 1.27), Txt(-1.6, -3.3, "+", 1.0)};
    s.posReference = ptMm(3.302, -1.27);
    s.posValeur = ptMm(3.302, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Moteur", "Moteur à courant continu", "Électromécanique", "M", "PinHeader_1x02_P2.54", "Moteur à courant continu, relié par deux fils", "moteur cc dc");
    s.broches = {Bp("1", 0, -5.08, 1, 2.54), Bp("2", 0, 5.08, 3, 2.54)};
    s.traces = {Rond(0, 0, 2.54), Txt(0, 0, "M", 1.6)};
    s.posReference = ptMm(3.302, -1.27);
    s.posValeur = ptMm(3.302, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("LM7805", "Régulateur 7805", "Alimentation", "U", "TO-220-3", "Régulateur linéaire 5 V, boîtier TO-220 (1 entrée, 2 masse, 3 sortie)", "regulateur 7805 5v to220");
    s.broches = {B("1", "VI", T::AlimEntree, -7.62, 0, 0, 2.54), B("3", "VO", T::AlimSortie, 7.62, 0, 2, 2.54), B("2", "GND", T::AlimEntree, 0, 5.08, 3, 2.54)};
    s.traces = {Rect(-5.08, -2.54, 5.08, 2.54, true)};
    s.posReference = ptMm(0, -5.334);
    s.posValeur = ptMm(0, -3.81);
    v.push_back(s);
  }
  auto genere = [&](const char* id, const char* nom, const char* categorie, const char* empreinte, const char* description, const char* motsCles, const char* broches) {
    SpecSymbole sp;
    sp.id = id;
    sp.nom = nom;
    sp.categorie = categorie;
    sp.prefixe = "U";
    sp.empreinte = empreinte;
    sp.description = description;
    sp.motsCles = motsCles;
    sp.broches = lireListeBroches(broches);
    v.push_back(genererSymbole(sp));
  };
  genere("LM358", "LM358", "Circuits intégrés", "SOIC-8", "Double amplificateur opérationnel", "aop ampli double lm358",
         "1 OUTA out\n2 -INA in\n3 +INA in\n4 V- alim\n5 +INB in\n6 -INB in\n7 OUTB out\n8 V+ alim");
  genere("PC817", "PC817", "Optoélectronique", "DIP-4", "Optocoupleur (1 anode, 2 cathode, 3 émetteur, 4 collecteur)", "optocoupleur isolation pc817",
         "1 A passive gauche\n2 K passive gauche\n4 C passive droite\n3 E passive droite");
  genere("74HC00", "74HC00", "Logique", "SOIC-14", "Quatre portes NON-ET à deux entrées", "logique nand porte 74hc",
         "1 1A in\n2 1B in\n3 1Y out\n4 2A in\n5 2B in\n6 2Y out\n7 GND alim\n8 3Y out\n9 3A in\n10 3B in\n11 4Y out\n12 4A in\n13 4B in\n14 VCC alim");
  genere("ATmega328P", "ATmega328P", "Microcontrôleurs", "DIP-28", "Microcontrôleur AVR 8 bits, 28 broches (celui de l’Arduino Uno)", "mcu avr arduino atmega microcontroleur",
         "1 PC6/RESET in\n2 PD0/RXD io\n3 PD1/TXD io\n4 PD2 io\n5 PD3 io\n6 PD4 io\n7 VCC alim\n8 GND alim\n9 PB6/XTAL1 io\n10 PB7/XTAL2 io\n11 PD5 io\n12 PD6 io\n13 PD7 io\n"
         "14 PB0 io\n15 PB1 io\n16 PB2 io\n17 PB3/MOSI io\n18 PB4/MISO io\n19 PB5/SCK io\n20 AVCC alim\n21 AREF passive\n22 GND alim\n23 PC0 io\n24 PC1 io\n25 PC2 io\n"
         "26 PC3 io\n27 PC4/SDA io\n28 PC5/SCL io");
  // ----- Symboles à plusieurs unités (E10) : chaque porte ou ampli se pose à part, l'alimentation à part -----
  auto deUnite = [](std::vector<Trace> traces, int u) {
    for (auto& t : traces) t.unite = u;
    return traces;
  };
  auto brocheUnite = [](Broche b, int u) {
    b.unite = u;
    return b;
  };
  {
    DefSymbole s = S("LM358_unites", "LM358 (par unité)", "Circuits intégrés", "U", "SOIC-8",
                     "Double amplificateur opérationnel, posé ampli par ampli (unités A et B) et alimentation à part (unité C)", "aop ampli double lm358 unites");
    s.unites = 3;
    const char* broches[2][3] = {{"3", "2", "1"}, {"5", "6", "7"}};
    for (int u = 1; u <= 2; ++u) {
      s.broches.push_back(brocheUnite(B(broches[u - 1][0], "+", T::Entree, -7.62, 2.54, 0, 3.81, false), u));
      s.broches.push_back(brocheUnite(B(broches[u - 1][1], "-", T::Entree, -7.62, -2.54, 0, 3.81, false), u));
      s.broches.push_back(brocheUnite(B(broches[u - 1][2], "~", T::Sortie, 7.62, 0, 2, 3.81, false), u));
      std::vector<Trace> corps = {Ligne({{-3.81, -3.81}, {-3.81, 3.81}, {3.81, 0}}, true), Txt(-2.9, -2.54, "−"), Txt(-2.9, 2.54, "+")};
      corps[0].fond = true;
      for (auto& t : deUnite(corps, u)) s.traces.push_back(t);
    }
    s.broches.push_back(brocheUnite(B("8", "V+", T::AlimEntree, 0, -5.08, 1, 2.54), 3));
    s.broches.push_back(brocheUnite(B("4", "V-", T::AlimEntree, 0, 5.08, 3, 2.54), 3));
    for (auto& t : deUnite({Rect(-1.27, -2.54, 1.27, 2.54, true)}, 3)) s.traces.push_back(t);
    s.posReference = ptMm(1.27, -4.318);
    s.posValeur = ptMm(1.27, 4.318);
    v.push_back(s);
  }
  {
    DefSymbole s = S("74HC00_unites", "74HC00 (par porte)", "Logique", "U", "SOIC-14",
                     "Quatre portes NON-ET à deux entrées, posées porte par porte (unités A à D) et alimentation à part (unité E)", "logique nand porte 74hc unites");
    s.unites = 5;
    const char* broches[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"9", "10", "8"}, {"12", "13", "11"}};
    for (int u = 1; u <= 4; ++u) {
      s.broches.push_back(brocheUnite(B(broches[u - 1][0], "A", T::Entree, -7.62, -2.54, 0, 3.81, false), u));
      s.broches.push_back(brocheUnite(B(broches[u - 1][1], "B", T::Entree, -7.62, 2.54, 0, 3.81, false), u));
      s.broches.push_back(brocheUnite(B(broches[u - 1][2], "Y", T::Sortie, 7.62, 0, 2, 2.54, false), u));
      std::vector<Trace> corps = {Ligne({{0, -3.81}, {-3.81, -3.81}, {-3.81, 3.81}, {0, 3.81}}), Arc(0, 0, 3.81, -90, 90), Rond(4.445, 0, 0.635)};
      for (auto& t : deUnite(corps, u)) s.traces.push_back(t);
    }
    s.broches.push_back(brocheUnite(B("14", "VCC", T::AlimEntree, 0, -5.08, 1, 2.54), 5));
    s.broches.push_back(brocheUnite(B("7", "GND", T::AlimEntree, 0, 5.08, 3, 2.54), 5));
    for (auto& t : deUnite({Rect(-1.27, -2.54, 1.27, 2.54, true)}, 5)) s.traces.push_back(t);
    s.posReference = ptMm(1.27, -4.318);
    s.posValeur = ptMm(1.27, 4.318);
    v.push_back(s);
  }
  // ----- Alimentations (symboles de réseau) -----
  {
    DefSymbole s = S("GND", "GND", "Alimentations", "#PWR", "", "Masse", "masse 0v");
    s.alimentation = true;
    s.broches = {B("1", "GND", T::AlimEntree, 0, 0, 1, 0, false, false)};
    s.traces = {Ligne({{0, 0}, {0, 1.27}}), Ligne({{-1.27, 1.27}, {1.27, 1.27}, {0, 2.54}}, true)};
    s.posValeur = ptMm(0, 3.556);
    v.push_back(s);
  }
  for (const char* nom : {"+5V", "+3V3", "+12V", "VCC"}) {
    DefSymbole s = S(nom, nom, "Alimentations", "#PWR", "", "Alimentation", "alimentation tension");
    s.alimentation = true;
    s.broches = {B("1", nom, T::AlimEntree, 0, 0, 3, 0, false, false)};
    s.traces = {Ligne({{0, 0}, {0, -1.27}}), Ligne({{-1.016, -1.27}, {1.016, -1.27}})};
    s.posValeur = ptMm(0, -2.286);
    v.push_back(s);
  }
  // Styles d'alimentation à nom libre (SAI-12, S13) : le nom se donne à la pose (−12V, VBAT, GNDA…)
  {
    DefSymbole s = S("Alim_barre", "Alimentation (barre)", "Alimentations", "#PWR", "", "Alimentation au nom libre, dessinée d’une barre", "alimentation tension barre");
    s.alimentation = true;
    s.broches = {B("1", "VCC", T::AlimEntree, 0, 0, 3, 0, false, false)};
    s.traces = {Ligne({{0, 0}, {0, -1.27}}), Ligne({{-1.016, -1.27}, {1.016, -1.27}})};
    s.posValeur = ptMm(0, -2.286);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Alim_fleche", "Alimentation (flèche)", "Alimentations", "#PWR", "", "Alimentation au nom libre, dessinée d’une flèche", "alimentation tension fleche");
    s.alimentation = true;
    s.broches = {B("1", "VCC", T::AlimEntree, 0, 0, 3, 0, false, false)};
    s.traces = {Ligne({{0, 0}, {0, -1.27}}), Ligne({{-0.762, -1.27}, {0.762, -1.27}, {0, -2.54}}, true)};
    s.posValeur = ptMm(0, -3.302);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Masse_chassis", "Masse de châssis", "Alimentations", "#PWR", "", "Masse reliée au châssis, au nom libre", "masse chassis terre");
    s.alimentation = true;
    s.broches = {B("1", "GNDPWR", T::AlimEntree, 0, 0, 1, 0, false, false)};
    s.traces = {Ligne({{0, 0}, {0, 1.27}}), Ligne({{-1.27, 1.27}, {1.27, 1.27}}), Ligne({{-1.27, 1.27}, {-1.905, 2.159}}), Ligne({{0, 1.27}, {-0.635, 2.159}}),
                Ligne({{1.27, 1.27}, {0.635, 2.159}})};
    s.posValeur = ptMm(0, 3.302);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Masse_analogique", "Masse analogique", "Alimentations", "#PWR", "", "Masse analogique, au nom libre (GNDA)", "masse analogique gnda");
    s.alimentation = true;
    s.broches = {B("1", "GNDA", T::AlimEntree, 0, 0, 1, 0, false, false)};
    s.traces = {Ligne({{0, 0}, {0, 1.27}}), Ligne({{-1.27, 1.27}, {1.27, 1.27}, {0, 2.54}, {-1.27, 1.27}})};
    s.posValeur = ptMm(0, 3.556);
    v.push_back(s);
  }
  {
    // Liaison de réseaux (net-tie, 2.11) : deux réseaux qui se touchent exprès en un seul point (masse en étoile,
    // GNDA et GND) ; chacun garde son nom, l'alimentation passe de l'un à l'autre
    DefSymbole s = S("Liaison_reseaux", "Liaison de réseaux", "Alimentations", "NT", "NetTie-2_SMD",
                     "Relie deux réseaux en un seul point voulu (masse en étoile, GNDA et GND) : chacun garde son nom", "net tie liaison reseaux masse etoile");
    s.liaisonReseaux = true;
    s.broches = {Bp("1", -2.54, 0, 0, 1.27), Bp("2", 2.54, 0, 2, 1.27)};
    s.traces = {Ligne({{-1.27, 0}, {1.27, 0}}), Rond(0, 0, 0.381, true)};
    s.posReference = ptMm(0, -1.524);
    s.posValeur = ptMm(0, 1.524);
    v.push_back(s);
  }
  // ----- Simulation (lot L11) : sources sans empreinte, hors nomenclature ; la valeur dit la source -----
  {
    DefSymbole s = S("Source_tension", "Source de tension", "Simulation", "V", "",
                     "Source de tension pour la simulation (1 : +) ; valeur « 5 », « DC 5 AC 1 », « SIN(0 1 1k) », « PULSE(0 5 0 1n 1n 1m 2m) » "
                     "ou « 0 V pendant 1 ms, puis rampe à 5 V en 10 µs »",
                     "source tension simulation sinus impulsion generateur");
    s.genre = GenreComposant::HorsNomenclature;
    s.broches = {B("1", "+", T::AlimSortie, 0, -5.08, 1, 2.54, false, true), B("2", "-", T::Passive, 0, 5.08, 3, 2.54, false, true)};
    s.traces = {Arc(0, 0, 2.54, 0, 360), Txt(0, -0.9, "+", 1.27), Txt(0, 1.6, "−", 1.27)};
    s.posReference = ptMm(3.302, -1.27);
    s.posValeur = ptMm(3.302, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Source_courant", "Source de courant", "Simulation", "I", "",
                     "Source de courant pour la simulation : le courant sort de la broche 2 ; valeur « 10m », « SIN(0 1m 1k) »", "source courant simulation");
    s.genre = GenreComposant::HorsNomenclature;
    s.broches = {Bp("1", 0, -5.08, 1, 2.54), Bp("2", 0, 5.08, 3, 2.54)};
    s.traces = {Arc(0, 0, 2.54, 0, 360), Ligne({{0, -1.524}, {0, 1.524}}), Ligne({{-0.762, 0.508}, {0, 1.524}, {0.762, 0.508}})};
    s.posReference = ptMm(3.302, -1.27);
    s.posValeur = ptMm(3.302, 1.27);
    v.push_back(s);
  }
  {
    DefSymbole s = S("Drapeau_alim", "Drapeau d’alimentation", "Alimentations", "#FLG", "", "Déclare le réseau alimenté (vérification électrique)", "pwr_flag drapeau");
    s.drapeau = true;
    s.broches = {B("1", "ALIM", T::AlimSortie, 0, 0, 3, 0, false, false)};
    s.traces = {Ligne({{0, 0}, {0, -1.27}}), Ligne({{0, -1.27}, {-1.016, -1.905}, {0, -2.54}, {1.016, -1.905}}, true), Txt(0, -3.302, "ALIM", 1.0)};
    s.posValeur = ptMm(0, -4.2);
    v.push_back(s);
  }
  return v;
}
}  // namespace

const std::vector<DefSymbole>& bibliothequeSymboles() {
  static const std::vector<DefSymbole> lib = construire();
  return lib;
}

const DefSymbole* symboleBibliotheque(const std::string& id) {
  for (const auto& s : bibliothequeSymboles())
    if (s.id == id) return &s;
  return nullptr;
}

// ---------- Composants créés par l'utilisateur ----------
namespace {
std::string minuscules(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}
std::size_t longueurUtf8(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char ch : s) n += (ch & 0xC0) != 0x80;
  return n;
}
bool estMasse(const std::string& nom) {
  const std::string n = minuscules(nom);
  return n.rfind("gnd", 0) == 0 || n.rfind("vss", 0) == 0 || n == "agnd" || n == "dgnd" || n == "pgnd" || n == "0v" || n == "vee" || n == "v-";
}
bool estAlimentation(const std::string& nom) {
  const std::string n = minuscules(nom);
  if (estMasse(nom) || n.rfind("vcc", 0) == 0 || n.rfind("vdd", 0) == 0 || n == "v+" || n == "vin" || n == "vbat" || n == "vbus" || n == "avcc" || n == "avdd" || n == "dvdd" || n == "vio")
    return true;
  if (!n.empty() && n[0] == '+') return true;
  // « 5V », « 3V3 », « 12V »
  std::size_t i = 0;
  while (i < n.size() && std::isdigit(static_cast<unsigned char>(n[i]))) ++i;
  if (i == 0 || i >= n.size() || n[i] != 'v') return false;
  for (std::size_t j = i + 1; j < n.size(); ++j)
    if (!std::isdigit(static_cast<unsigned char>(n[j]))) return false;
  return true;
}
// Type d'une broche deviné d'après son nom (EL-33) : sorties d'alimentation (VOUT, VO, VREF…), alimentations et
// masses, entrées usuelles (EN, RESET, NRST, CS, SCK…), sorties usuelles (TX, DO, Q…), broches libres (NC).
// Les signes ¯ « N » ou « / » en tête (actif à l'état bas) sont ignorés. Le reste est bidirectionnel.
TypeBroche typeDevine(const std::string& nom) {
  std::string n = minuscules(nom);
  n.erase(std::remove(n.begin(), n.end(), '~'), n.end());
  if (!n.empty() && (n[0] == '/' || n[0] == '!')) n.erase(0, 1);
  auto un = [&](std::initializer_list<const char*> l) { return std::any_of(l.begin(), l.end(), [&](const char* y) { return n == y; }); };
  if (n.empty()) return TypeBroche::Bidirectionnelle;
  if (un({"nc", "n.c.", "dnc"})) return TypeBroche::NonConnectee;
  if (un({"vout", "vo", "vref", "vreg", "vout1", "vout2", "3v3out", "5vout"})) return TypeBroche::AlimSortie;
  if (estAlimentation(nom)) return TypeBroche::AlimEntree;
  if (un({"en", "nen", "ce", "nce", "cs", "ncs", "ss", "nss", "sck", "sclk", "clk", "mosi", "sdi", "din", "rx", "rxd", "reset", "nreset", "rst", "nrst", "mclr", "nmclr", "shdn",
          "nshdn", "oe", "noe", "we", "nwe", "rd", "nrd", "wr", "nwr", "a0", "a1", "a2", "boot0", "boot", "trig", "thr", "dis", "ctrl", "adj", "fb", "sense", "inh", "in", "in+", "in-",
          "ain", "vin+", "vin-", "set", "mode", "sel", "pgm", "prog"}))
    return TypeBroche::Entree;
  if (un({"tx", "txd", "miso", "sdo", "dout", "do", "q", "nq", "int", "nint", "irq", "nirq", "pg", "pgood", "flt", "nflt", "fault", "nfault", "rdy", "ready", "busy", "out", "out-",
          "alert", "stat"}))
    return TypeBroche::Sortie;
  return TypeBroche::Bidirectionnelle;
}

// Arrondi au multiple supérieur de la grille (1,27 mm), en millimètres
double grilleSup(double v) { return std::ceil(v / 1.27 - 1e-9) * 1.27; }
}  // namespace

std::string identifiantSymbole(const std::string& nom) {
  std::string s;
  for (unsigned char ch : nom) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') s += static_cast<char>(ch);
    else if (ch == ' ' || ch == '/') s += '-';
  }
  while (!s.empty() && s.back() == '-') s.pop_back();
  return "perso:" + (s.empty() ? std::string("composant") : s);
}

namespace {
DefSymbole genererCorps(const SpecSymbole& s) {
  constexpr double P = 2.54, L = 2.54;
  DefSymbole d;
  d.id = s.id.empty() ? identifiantSymbole(s.nom) : s.id;
  d.nom = s.nom.empty() ? std::string("Composant") : s.nom;
  d.description = s.description;
  d.categorie = s.categorie.empty() ? std::string("Mes composants") : s.categorie;
  d.prefixe = s.prefixe.empty() ? std::string("U") : s.prefixe;
  d.empreinte = s.empreinte;
  d.motsCles = s.motsCles;
  d.fabricant = s.fabricant;
  d.mpn = s.mpn;
  d.fiche = s.fiche;
  d.version = std::max(1, s.version);
  // Répartition sur les quatre côtés, dans l'ordre de la liste
  std::vector<std::size_t> cotes[4], libres;
  for (std::size_t i = 0; i < s.broches.size(); ++i) {
    const SpecBroche& b = s.broches[i];
    int c = b.cote;
    if (c < 0 || c > 3) {
      if (b.type == TypeBroche::AlimEntree || b.type == TypeBroche::AlimSortie) c = estMasse(b.nom) ? 3 : 2;
      else if (b.type == TypeBroche::Entree) c = 0;
      else if (b.type == TypeBroche::Sortie || b.type == TypeBroche::TroisEtats || b.type == TypeBroche::CollecteurOuvert) c = 1;
      else {
        libres.push_back(i);
        continue;
      }
    }
    cotes[c].push_back(i);
  }
  const std::size_t total = cotes[0].size() + cotes[1].size() + libres.size(), cibleGauche = (total + 1) / 2;
  const std::size_t aGauche = cibleGauche > cotes[0].size() ? std::min(libres.size(), cibleGauche - cotes[0].size()) : 0;
  for (std::size_t k = 0; k < libres.size(); ++k) cotes[k < aGauche ? 0 : 1].push_back(libres[k]);
  for (auto& c : cotes) std::sort(c.begin(), c.end());
  // Encombrement des noms (texte de 1,15 mm, environ 0,8 de chasse)
  auto largeurNom = [&](std::size_t i) {
    const std::string& n = s.broches[i].nom;
    return n.empty() || n == "~" ? 0.0 : 0.92 * static_cast<double>(longueurUtf8(n));
  };
  double maxNom[4] = {0, 0, 0, 0};
  for (int c = 0; c < 4; ++c)
    for (std::size_t i : cotes[c]) maxNom[c] = std::max(maxNom[c], largeurNom(i));
  double pasHorizontal = P;
  while (pasHorizontal < std::max(maxNom[2], maxNom[3]) + 0.8) pasHorizontal += P;
  const std::size_t nV = std::max(cotes[0].size(), cotes[1].size()), nH = std::max(cotes[2].size(), cotes[3].size());
  double w = std::max({2 * P, (0.6 + maxNom[0] + 1.27 + maxNom[1] + 0.6) / 2, nH ? (static_cast<double>(nH) - 1) / 2 * pasHorizontal + P : 0.0});
  double h = std::max(P, nV ? (static_cast<double>(nV) - 1) / 2 * P + P : P);
  if (!cotes[2].empty() || !cotes[3].empty()) h += P;
  w = grilleSup(w);
  h = grilleSup(h);
  auto ajouter = [&](std::size_t i, double x, double y, int dir) {
    const SpecBroche& b = s.broches[i];
    Broche x2;
    x2.numero = b.numero;
    x2.nom = b.nom.empty() ? std::string("~") : b.nom;
    x2.type = b.type;
    x2.extremite = ptMm(x, y);
    x2.direction = dir;
    x2.longueur = nm(L);
    x2.nomVisible = !b.nom.empty() && b.nom != "~";
    d.broches.push_back(x2);
  };
  for (int c = 0; c < 2; ++c) {
    const double n = static_cast<double>(cotes[c].size());
    for (std::size_t k = 0; k < cotes[c].size(); ++k) ajouter(cotes[c][k], c == 0 ? -w - L : w + L, (static_cast<double>(k) - (n - 1) / 2) * P, c == 0 ? 0 : 2);
  }
  for (int c = 2; c < 4; ++c) {
    const double n = static_cast<double>(cotes[c].size());
    for (std::size_t k = 0; k < cotes[c].size(); ++k) ajouter(cotes[c][k], (static_cast<double>(k) - (n - 1) / 2) * pasHorizontal, c == 2 ? -h - L : h + L, c == 2 ? 1 : 3);
  }
  // Les broches sont triées par numéro dans la définition (ordre stable pour les fichiers)
  std::stable_sort(d.broches.begin(), d.broches.end(), [](const Broche& a, const Broche& b) {
    const bool na = !a.numero.empty() && std::all_of(a.numero.begin(), a.numero.end(), [](unsigned char ch) { return std::isdigit(ch); });
    const bool nb = !b.numero.empty() && std::all_of(b.numero.begin(), b.numero.end(), [](unsigned char ch) { return std::isdigit(ch); });
    if (na && nb && a.numero.size() != b.numero.size()) return a.numero.size() < b.numero.size();
    if (na != nb) return na;
    return a.numero < b.numero;
  });
  Trace corps;
  corps.forme = Trace::Forme::Rectangle;
  corps.points = {ptMm(-w, -h), ptMm(w, h)};
  corps.fond = true;
  d.traces.push_back(corps);
  d.posReference = cotes[2].empty() ? ptMm(0, -h - 1.27) : ptMm(w + 0.635, -h - 1.27);
  d.posValeur = cotes[3].empty() ? ptMm(0, h + 1.905) : ptMm(w + 0.635, h + 1.905);
  return d;
}
}  // namespace

DefSymbole genererSymbole(const SpecSymbole& s) {
  // Unités (E10) : une seule (ou aucune donnée) → un corps ; plusieurs → un corps par unité, numérotées 1…N dans
  // l'ordre de leurs numéros, les broches communes (unité 0) répétées sur chacune (même numéro : la connectivité
  // les réunit)
  std::set<int> unites;
  for (const auto& b : s.broches)
    if (b.unite > 0) unites.insert(b.unite);
  if (unites.size() <= 1) {
    SpecSymbole s1 = s;
    s1.broches.clear();
    std::set<std::string> vus;
    for (const auto& b : s.broches)
      if (vus.insert(b.numero).second) s1.broches.push_back(b);
    return genererCorps(s1);
  }
  DefSymbole d;
  int rang = 0;
  for (const int u : unites) {
    ++rang;
    SpecSymbole su = s;
    su.broches.clear();
    std::set<std::string> vus;
    for (const auto& b : s.broches)
      if ((b.unite == u || b.unite == 0) && vus.insert(b.numero).second) su.broches.push_back(b);
    DefSymbole du = genererCorps(su);
    for (auto& b : du.broches) b.unite = rang;
    for (auto& t : du.traces) t.unite = rang;
    if (rang == 1) {
      d = du;
      d.broches.clear();
      d.traces.clear();
    } else {
      // Repère et valeur au-dessus et au-dessous de l'unité la plus haute
      if (du.posReference.y < d.posReference.y) d.posReference = du.posReference;
      if (du.posValeur.y > d.posValeur.y) d.posValeur = du.posValeur;
    }
    d.broches.insert(d.broches.end(), du.broches.begin(), du.broches.end());
    d.traces.insert(d.traces.end(), du.traces.begin(), du.traces.end());
  }
  d.unites = rang;
  d.nomsUnites = s.nomsUnites;
  if (d.nomsUnites.size() > static_cast<std::size_t>(rang)) d.nomsUnites.resize(static_cast<std::size_t>(rang));
  return d;
}

SpecSymbole specDepuisSymbole(const DefSymbole& d) {
  SpecSymbole s;
  s.id = d.id;
  s.nom = d.nom;
  s.prefixe = d.prefixe;
  s.categorie = d.categorie;
  s.description = d.description;
  s.empreinte = d.empreinte;
  s.motsCles = d.motsCles;
  s.fabricant = d.fabricant;
  s.mpn = d.mpn;
  s.fiche = d.fiche;
  s.version = d.version;
  s.nomsUnites = d.nomsUnites;
  // Ordre de lecture : chaque côté de haut en bas (ou de gauche à droite), côté par côté
  std::vector<const Broche*> v;
  for (const auto& b : d.broches) v.push_back(&b);
  auto cote = [](int dir) { return dir == 0 ? 0 : dir == 2 ? 1 : dir == 1 ? 2 : 3; };
  std::stable_sort(v.begin(), v.end(), [&](const Broche* a, const Broche* b) {
    const int ca = cote(a->direction), cb = cote(b->direction);
    if (ca != cb) return ca < cb;
    return ca < 2 ? a->extremite.y < b->extremite.y : a->extremite.x < b->extremite.x;
  });
  // Plusieurs unités : une broche présente sur plusieurs unités est commune (unité 0), listée une fois
  std::map<std::string, std::set<int>> unitesDe;
  for (const auto& b : d.broches) unitesDe[b.numero].insert(b.unite);
  std::set<std::string> vus;
  for (const Broche* b : v) {
    if (!vus.insert(b->numero).second) continue;
    SpecBroche sb{b->numero, b->nom == "~" ? std::string() : b->nom, b->type, cote(b->direction)};
    if (d.unites > 1) sb.unite = unitesDe[b->numero].size() > 1 ? 0 : b->unite;
    s.broches.push_back(sb);
  }
  return s;
}

namespace {
// « u2 », « u=2 », « unite=B », « unité:2 », « commune » → numéro d'unité (0 : commune) ; -1 si le mot n'en est pas un
int uniteDepuisMot(const std::string& x) {
  if (x == "commune" || x == "common") return 0;
  std::string v;
  for (const char* p : {"unité", "unite", "unit", "u"})
    if (x.rfind(p, 0) == 0) {
      v = x.substr(std::string(p).size());
      break;
    }
  if (!v.empty() && (v[0] == '=' || v[0] == ':')) v.erase(0, 1);
  if (v.empty() || v.size() > 3) return -1;
  if (std::all_of(v.begin(), v.end(), [](unsigned char ch) { return std::isdigit(ch); })) return std::clamp(std::stoi(v), 0, 64);
  if (v.size() == 1 && v[0] >= 'a' && v[0] <= 'z') return v[0] - 'a' + 1;
  return -1;
}
}  // namespace

std::vector<SpecBroche> lireListeBroches(const std::string& texte) {
  std::vector<SpecBroche> r;
  std::istringstream lignes(texte);
  std::string ligne;
  while (std::getline(lignes, ligne)) {
    for (char& ch : ligne)
      if (ch == '\t' || ch == ';' || ch == ',' || ch == '|' || ch == '\r') ch = ' ';
    std::istringstream mots(ligne);
    std::vector<std::string> m;
    for (std::string x; mots >> x;) m.push_back(x);
    if (m.empty() || m[0][0] == '#') continue;
    SpecBroche b;
    b.numero = m[0];
    if (m.size() > 1) b.nom = m[1];
    bool typeDonne = false;
    for (std::size_t i = 2; i < m.size(); ++i) {
      const std::string x = minuscules(m[i]);
      auto est = [&](std::initializer_list<const char*> l) { return std::any_of(l.begin(), l.end(), [&](const char* y) { return x == y; }); };
      if (est({"in", "input", "entree", "entrée", "e"})) { b.type = TypeBroche::Entree; typeDonne = true; }
      else if (est({"out", "output", "sortie", "s"})) { b.type = TypeBroche::Sortie; typeDonne = true; }
      else if (est({"io", "bidir", "bidirectionnelle", "bidirectional", "inout"})) { b.type = TypeBroche::Bidirectionnelle; typeDonne = true; }
      else if (est({"tri", "tristate", "3etats", "trois-etats", "3-etats"})) { b.type = TypeBroche::TroisEtats; typeDonne = true; }
      else if (est({"passive", "passif", "p"})) { b.type = TypeBroche::Passive; typeDonne = true; }
      else if (est({"alim", "power", "pwr", "alimentation", "power_in", "alim-entree"})) { b.type = TypeBroche::AlimEntree; typeDonne = true; }
      else if (est({"alim-sortie", "power_out", "pwr_out", "source"})) { b.type = TypeBroche::AlimSortie; typeDonne = true; }
      else if (est({"oc", "od", "collecteur", "drain", "open", "collecteur-ouvert"})) { b.type = TypeBroche::CollecteurOuvert; typeDonne = true; }
      else if (est({"nc", "non-connectee", "libre"})) { b.type = TypeBroche::NonConnectee; typeDonne = true; }
      else if (est({"gauche", "left", "g", "l"})) b.cote = 0;
      else if (est({"droite", "right", "d", "r"})) b.cote = 1;
      else if (est({"haut", "top", "h", "t", "up"})) b.cote = 2;
      else if (est({"bas", "bottom", "b", "down"})) b.cote = 3;
      else if (const int u = uniteDepuisMot(x); u >= 0) b.unite = u;
    }
    if (!typeDonne) b.type = typeDevine(b.nom);
    r.push_back(b);
  }
  return r;
}

Pt transformer(const SymbolePlace& s, Pt q) {
  if (s.miroir) q.x = -q.x;
  return tournerQuarts(q, s.rotation) + s.position;
}

int transformerDirection(const SymbolePlace& s, int d) {
  if (s.miroir && (d == 0 || d == 2)) d = 2 - d;
  const int q = ((s.rotation % 4) + 4) % 4;
  return (d + 3 * q) % 4;
}

std::string lettreUnite(int unite) {
  if (unite < 1) return {};
  std::string r;
  for (int u = unite; u > 0; u = (u - 1) / 26) r.insert(r.begin(), static_cast<char>('A' + (u - 1) % 26));
  return r;
}

std::string numeroBroche(const SymbolePlace& s, const std::string& numeroDefinition) {
  const auto it = s.numeros.find(numeroDefinition);
  return it != s.numeros.end() && !it->second.empty() ? it->second : numeroDefinition;
}

std::vector<BrochePlacee> brochesPlacees(const SymbolePlace& s, const DefSymbole& d) {
  std::vector<BrochePlacee> r;
  r.reserve(d.broches.size());
  for (const auto& b : d.broches)
    if (dansUnite(b.unite, s, d)) r.push_back({&b, transformer(s, b.extremite), transformerDirection(s, b.direction), numeroBroche(s, b.numero)});
  return r;
}

Boite boiteSymbole(const SymbolePlace& s, const DefSymbole& d) {
  Boite b;
  for (const auto& t : d.traces) {
    if (t.forme == Trace::Forme::Texte || !dansUnite(t.unite, s, d)) continue;
    if (t.forme == Trace::Forme::Cercle || t.forme == Trace::Forme::Arc) {
      const Pt c = t.points.empty() ? Pt{} : t.points.front();
      b.etendre(transformer(s, c + Pt{t.rayon, t.rayon}));
      b.etendre(transformer(s, c - Pt{t.rayon, t.rayon}));
    } else
      for (const Pt& p : t.points) b.etendre(transformer(s, p));
  }
  for (const auto& br : d.broches)
    if (dansUnite(br.unite, s, d)) b.etendre(transformer(s, br.extremite));
  if (b.vide()) b.etendre(s.position);
  return b;
}

Pt ancrageChamp(const SymbolePlace& s, const DefSymbole& d, bool valeur) {
  const std::optional<ChampPlace>& propre = valeur ? s.champValeur : s.champReference;
  if (propre) return s.position + propre->decalage;
  return transformer(s, valeur ? d.posValeur : d.posReference);
}

Boite cadreFeuille(const std::string& format) {
  if (format == "A3") return {0, 0, 420 * NM_PAR_MM, 297 * NM_PAR_MM};
  if (format == "A2") return {0, 0, 594 * NM_PAR_MM, 420 * NM_PAR_MM};
  return {0, 0, 297 * NM_PAR_MM, 210 * NM_PAR_MM};
}

Pt surGrille(Pt p, Nm pas) {
  auto arrondi = [pas](Nm v) { return static_cast<Nm>(std::llround(static_cast<double>(v) / static_cast<double>(pas))) * pas; };
  return {arrondi(p.x), arrondi(p.y)};
}

const char* nomGenreComposant(GenreComposant g) {
  switch (g) {
    case GenreComposant::Standard: return "standard";
    case GenreComposant::Mecanique: return "mécanique";
    case GenreComposant::Graphique: return "graphique";
    case GenreComposant::LiaisonReseaux: return "liaison de réseaux";
    case GenreComposant::HorsNomenclature: return "hors nomenclature";
    case GenreComposant::Cavalier: return "cavalier";
  }
  return "standard";
}

const char* codeGenreComposant(GenreComposant g) {
  switch (g) {
    case GenreComposant::Standard: return "standard";
    case GenreComposant::Mecanique: return "mecanique";
    case GenreComposant::Graphique: return "graphique";
    case GenreComposant::LiaisonReseaux: return "liaisonReseaux";
    case GenreComposant::HorsNomenclature: return "horsNomenclature";
    case GenreComposant::Cavalier: return "cavalier";
  }
  return "standard";
}

GenreComposant genreComposantDepuisCode(const std::string& code) {
  for (auto g : {GenreComposant::Mecanique, GenreComposant::Graphique, GenreComposant::LiaisonReseaux, GenreComposant::HorsNomenclature, GenreComposant::Cavalier})
    if (code == codeGenreComposant(g)) return g;
  return GenreComposant::Standard;
}

}  // namespace prom::ecad
