// Prométhée : catalogue d'appareils électriques génériques (dimensions et caractéristiques typiques du marché,
// sans marque) et correspondance des lettres de repère. Le fabricant et la référence se renseignent dans
// l'appareil posé ; les dimensions servent à l'implantation de l'armoire. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "promethee/electrique.hpp"

namespace prom::elec {

namespace {

EntreeCatalogue E(const std::string& id, const std::string& famille, const std::string& designation, const std::string& symbole, const std::string& prefixe) {
  EntreeCatalogue e;
  e.id = id;
  e.famille = famille;
  e.designation = designation;
  e.symbole = symbole;
  e.prefixe = prefixe;
  return e;
}

std::string nombre(double v) {
  char b[32];
  if (std::abs(v - std::round(v)) < 1e-9) std::snprintf(b, sizeof b, "%.0f", v);
  else std::snprintf(b, sizeof b, "%.2g", v);
  std::string s = b;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}

// Prix public indicatif HT, ordre de grandeur du marché sans remise (appareillage générique de milieu de gamme) :
// il chiffre un premier devis tant qu'on n'a pas saisi ses tarifs dans la nomenclature. Les appareils qui varient
// avec le calibre ou la puissance suivent une droite tirée des tarifs publics ; les autres ont un prix fixe.
double prixIndicatif(const EntreeCatalogue& e) {
  static const std::map<std::string, double> fixes = {
      {"sf-3p-32", 45},       {"dd-2p-16", 120},       {"pf-2p", 80},          {"ka-4rt-24dc", 28},        {"kt-travail", 45},     {"kt-repos", 45},
      {"tl-16", 25},          {"bp-vert", 12},         {"bp-rouge", 12},       {"au-40", 30},              {"com-2", 15},          {"sonore-24", 35},
      {"b-ind-m18", 30},      {"s-fdc", 35},           {"b-thermostat", 25},   {"b-pressostat", 60},       {"m1-0,37", 150},       {"e-chauffe-2000", 60},
      {"y-ev-24", 45},        {"m-ventil", 80},        {"x-2,5", 0.9},         {"x-4", 1.1},               {"x-6", 1.5},           {"x-pe-2,5", 2.5},
      {"x-pe-4", 3},          {"x-connecteur", 25},    {"a-8e8s", 250},        {"u-demarreur-4", 180},     {"p-compteur-1", 45},   {"xd-prise", 6},
      {"xd-prise-mod", 15},   {"s-inter", 6},          {"s-vv", 7},            {"s-poussoir", 7},          {"e-luminaire", 4},     {"s-inter-double", 9},
      {"s-inter-volet", 18},  {"m-volet", 110},        {"e-chauffe-eau-200", 450}, {"m-vmc", 120},         {"xd-sortie-cable", 6}, {"xd-prise-32", 14},
      {"e-baes-45", 45},      {"e-baes-ambiance", 75}, {"b-detecteur-fumee", 55}, {"s-dm", 35},            {"xd-rj45", 12},        {"xd-tv", 8},
      {"b-presence", 85},     {"b-ils", 12},           {"b-transmetteur", 180}, {"b-pt100", 45},           {"b-tc-k", 35},         {"b-codeur", 220},
      {"g-module-425", 110},  {"xd-irve-7", 750},      {"xd-irve-22", 1400},   {"ka-minuterie", 35},       {"ka-horloge", 60},     {"km-hc-20", 30},
      {"ka-phases", 75},      {"p-cpi", 650},          {"p-centrale", 350},    {"p-horaire", 35},          {"t-tc-100", 35},       {"t-tc-250", 45},
      {"u-onduleur-pv-3,6", 900}, {"u-asi-1500", 650}, {"q-inverseur-63", 1500}, {"c-batterie-25", 1400}, {"g-groupe-60", 18000}, {"f-sepam", 2500}};
  if (const auto it = fixes.find(e.id); it != fixes.end()) return it->second;
  const std::string& s = e.symbole;
  const double in = e.calibre, kw = e.puissance;
  double p = 0;
  if (s == "iec:disj-1pn") p = 14 + 0.35 * in;
  else if (s == "iec:disj-2p") p = 30 + 0.8 * in;
  else if (s == "iec:disj-3p") p = 45 + 1.2 * in;
  else if (s == "iec:disj-4p") p = 60 + 1.5 * in;
  else if (s == "iec:disj-moteur-3p") p = 55 + 2 * in;
  else if (s == "iec:inter-sect-3p") p = 35 + 0.8 * in;
  else if (s == "iec:inter-sect-4p") p = 50 + 0.8 * in;
  else if (s == "iec:fusible") p = 9;
  else if (s == "iec:relais-thermique-3p") p = 45;
  else if (s == "iec:diff-2p") {
    const std::string& t = e.typeDifferentiel;
    p = (40 + 0.3 * in) * (t == "B" ? 5.0 : t == "F" ? 2.0 : t == "A" ? 1.6 : 1.0);
  } else if (s == "iec:diff-4p") p = (40 + 0.3 * in) * 2.2 * (e.typeDifferentiel == "A" ? 1.6 : 1.0);
  else if (s == "iec:agcp") p = 120;
  else if (s == "iec:bobine-km") p = 25 + 2.5 * in;
  else if (s == "iec:bobine-ka") p = 15;
  else if (s == "iec:voyant") p = 10;
  else if (s == "iec:moteur-3") p = 120 + 70 * kw;
  else if (s == "iec:alim-dc") p = 35 + 10.5 * in;
  else if (s == "iec:transfo-1") p = 20 + 0.24 * kw * 1000;
  else if (s == "iec:variateur") p = 200 + 85 * kw;
  if (p <= 0) return 0;
  return p < 10 ? std::round(p * 10) / 10 : std::round(p);
}

std::vector<EntreeCatalogue> construire() {
  std::vector<EntreeCatalogue> v;
  const std::string PROT = "Protection et sectionnement", COMM = "Commande et relayage", CAPT = "Capteurs et dialogue", SIGN = "Signalisation",
                    PUIS = "Puissance et récepteurs", ALIM = "Alimentations et transformateurs", BORN = "Bornes et connecteurs", AUTO = "Automates et variateurs",
                    BAT = "Bâtiment";
  auto modulaire = [](EntreeCatalogue& e, int modules) {
    e.modules = modules;
    e.largeur = 18.0 * modules;
    e.hauteur = 85;
    e.profondeur = 70;
  };
  // Disjoncteurs modulaires (courbe C, 6 kA ; 1P+N en 1 module de 18 mm)
  for (double in : {2.0, 6.0, 10.0, 16.0, 20.0, 25.0, 32.0, 40.0}) {
    EntreeCatalogue e = E("dj-1pn-c" + nombre(in), PROT, "Disjoncteur 1P+N " + nombre(in) + " A courbe C 4,5 kA", "iec:disj-1pn", "Q");
    e.poles = 2;
    e.calibre = in;
    e.courbe = "C";
    e.pouvoirCoupure = 4.5;
    e.contactsNO = 1;
    e.contactsNF = 1;  // bloc de contacts auxiliaires adaptable
    modulaire(e, 1);
    v.push_back(e);
  }
  for (double in : {10.0, 16.0, 20.0, 25.0, 32.0, 40.0, 50.0, 63.0}) {
    EntreeCatalogue e = E("dj-3p-c" + nombre(in), PROT, "Disjoncteur 3P " + nombre(in) + " A courbe C 6 kA", "iec:disj-3p", "Q");
    e.poles = 3;
    e.calibre = in;
    e.courbe = "C";
    e.pouvoirCoupure = 6;
    e.contactsNO = 1;
    e.contactsNF = 1;
    modulaire(e, 3);
    v.push_back(e);
  }
  for (double in : {25.0, 40.0, 63.0}) {
    EntreeCatalogue e = E("dj-4p-c" + nombre(in), PROT, "Disjoncteur 3P+N " + nombre(in) + " A courbe C 6 kA", "iec:disj-4p", "Q");
    e.poles = 4;
    e.calibre = in;
    e.courbe = "C";
    e.pouvoirCoupure = 6;
    modulaire(e, 4);
    v.push_back(e);
  }
  for (double in : {16.0, 32.0}) {
    EntreeCatalogue e = E("dj-2p-c" + nombre(in), PROT, "Disjoncteur 2P " + nombre(in) + " A courbe C", "iec:disj-2p", "Q");
    e.poles = 2;
    e.calibre = in;
    e.courbe = "C";
    e.pouvoirCoupure = 6;
    modulaire(e, 2);
    v.push_back(e);
  }
  // Disjoncteurs moteurs (plage de réglage du thermique, 45 mm)
  for (auto [bas, haut] : {std::pair{0.4, 0.63}, {0.63, 1.0}, {1.0, 1.6}, {1.6, 2.5}, {2.5, 4.0}, {4.0, 6.3}, {6.0, 10.0}, {9.0, 14.0}, {13.0, 18.0}, {17.0, 23.0}}) {
    EntreeCatalogue e = E("dm-" + nombre(haut), PROT, "Disjoncteur moteur " + nombre(bas) + "–" + nombre(haut) + " A", "iec:disj-moteur-3p", "Q");
    e.poles = 3;
    e.calibre = haut;
    e.pouvoirCoupure = 50;
    e.contactsNO = 1;
    e.contactsNF = 1;
    e.largeur = 45;
    e.hauteur = 89;
    e.profondeur = 97;
    v.push_back(e);
  }
  for (double in : {25.0, 40.0, 63.0}) {
    EntreeCatalogue e = E("is-3p-" + nombre(in), PROT, "Interrupteur-sectionneur 3P " + nombre(in) + " A", "iec:inter-sect-3p", "Q");
    e.poles = 3;
    e.calibre = in;
    modulaire(e, 3);
    v.push_back(e);
    EntreeCatalogue f = E("is-4p-" + nombre(in), PROT, "Interrupteur-sectionneur 3P+N " + nombre(in) + " A", "iec:inter-sect-4p", "Q");
    f.poles = 4;
    f.calibre = in;
    modulaire(f, 4);
    v.push_back(f);
  }
  {
    EntreeCatalogue e = E("sf-3p-32", PROT, "Sectionneur porte-fusibles 3P 32 A (10×38)", "iec:sect-fusible-3p", "Q");
    e.poles = 3;
    e.calibre = 32;
    modulaire(e, 3);
    v.push_back(e);
  }
  for (double in : {2.0, 4.0, 6.0, 10.0, 16.0}) {
    EntreeCatalogue e = E("fu-10x38-" + nombre(in), PROT, "Porte-fusible 1P et fusible 10×38 gG " + nombre(in) + " A", "iec:fusible", "F");
    e.poles = 1;
    e.calibre = in;
    modulaire(e, 1);
    v.push_back(e);
  }
  for (auto [bas, haut] : {std::pair{1.0, 1.6}, {1.6, 2.5}, {2.5, 4.0}, {4.0, 6.0}, {5.5, 8.0}, {7.0, 10.0}, {9.0, 13.0}, {12.0, 18.0}, {16.0, 24.0}}) {
    EntreeCatalogue e = E("rt-" + nombre(haut), PROT, "Relais thermique " + nombre(bas) + "–" + nombre(haut) + " A (95-96, 97-98)", "iec:relais-thermique-3p", "F");
    e.poles = 3;
    e.calibre = haut;
    e.contactsNO = 1;
    e.contactsNF = 1;
    e.largeur = 45;
    e.hauteur = 66;
    e.profondeur = 70;
    v.push_back(e);
  }
  for (auto [type, sym] : {std::pair{"AC", "iec:diff-2p"}, {"A", "iec:diff-2p"}, {"F", "iec:diff-2p"}}) {
    for (double in : {25.0, 40.0, 63.0}) {
      EntreeCatalogue e = E(std::string("id-2p-") + nombre(in) + "-" + type, PROT, "Interrupteur différentiel 2P " + nombre(in) + " A 30 mA type " + type, sym, "Q");
      e.poles = 2;
      e.calibre = in;
      e.sensibilite = 30;
      e.typeDifferentiel = type;
      modulaire(e, 2);
      v.push_back(e);
    }
  }
  {
    EntreeCatalogue e = E("id-4p-40-A", PROT, "Interrupteur différentiel 4P 40 A 30 mA type A", "iec:diff-4p", "Q");
    e.poles = 4;
    e.calibre = 40;
    e.sensibilite = 30;
    e.typeDifferentiel = "A";
    modulaire(e, 4);
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("dd-2p-16", PROT, "Disjoncteur différentiel 1P+N 16 A 30 mA type A", "iec:disj-diff-2p", "Q");
    e.poles = 2;
    e.calibre = 16;
    e.courbe = "C";
    e.sensibilite = 30;
    e.typeDifferentiel = "A";
    modulaire(e, 2);
    v.push_back(e);
  }
  for (double in : {15.0, 30.0, 45.0, 60.0}) {
    EntreeCatalogue e = E("agcp-" + nombre(in), PROT, "Disjoncteur de branchement 2P 15/" + nombre(in) + " A 500 mA sélectif", "iec:agcp", "Q");
    e.poles = 2;
    e.calibre = in;
    e.sensibilite = 500;
    e.typeDifferentiel = "S";
    e.largeur = 90;
    e.hauteur = 120;
    e.profondeur = 75;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("pf-2p", PROT, "Parafoudre 1P+N type 2, 20 kA", "iec:parafoudre", "F");
    e.poles = 2;
    modulaire(e, 2);
    v.push_back(e);
  }

  // Contacteurs (bobine 24 V continu ou 230 V alternatif ; 1 NO + 1 NF incorporés)
  for (double in : {9.0, 12.0, 18.0, 25.0, 32.0}) {
    for (bool continu : {true, false}) {
      EntreeCatalogue e = E("km-" + nombre(in) + (continu ? "-24dc" : "-230ac"), COMM,
                            "Contacteur 3P " + nombre(in) + " A (AC-3), bobine " + (continu ? "24 V continu" : "230 V alternatif") + ", 1 NO + 1 NF", "iec:bobine-km", "KM");
      e.poles = 3;
      e.calibre = in;
      e.tensionBobine = continu ? 24 : 230;
      e.bobineContinue = continu;
      e.contactsNO = 1 + 2;  // incorporés + bloc additif à 2 contacts
      e.contactsNF = 1 + 2;
      e.largeur = in <= 18 ? 45 : 55;
      e.hauteur = 85;
      e.profondeur = in <= 18 ? 95 : 105;
      v.push_back(e);
    }
  }
  for (bool continu : {true, false}) {
    EntreeCatalogue e = E(std::string("ka-2rt-") + (continu ? "24dc" : "230ac"), COMM, std::string("Relais à embrocher 2 inverseurs 8 A, bobine ") + (continu ? "24 V continu" : "230 V alternatif"),
                          "iec:bobine-ka", "KA");
    e.tensionBobine = continu ? 24 : 230;
    e.bobineContinue = continu;
    e.inverseurs = 2;
    e.largeur = 27;
    e.hauteur = 80;
    e.profondeur = 75;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("ka-4rt-24dc", COMM, "Relais à embrocher 4 inverseurs 5 A, bobine 24 V continu", "iec:bobine-ka", "KA");
    e.tensionBobine = 24;
    e.bobineContinue = true;
    e.inverseurs = 4;
    e.largeur = 27;
    e.hauteur = 80;
    e.profondeur = 75;
    v.push_back(e);
  }
  for (auto [sym, nom] : {std::pair{"iec:bobine-tempo-travail", "Relais temporisé multifonction (travail), 1 inverseur, 24–240 V"},
                          {"iec:bobine-tempo-repos", "Relais temporisé multifonction (repos), 1 inverseur, 24–240 V"}}) {
    EntreeCatalogue e = E(std::string(sym) == "iec:bobine-tempo-travail" ? "kt-travail" : "kt-repos", COMM, nom, sym, "KA");
    e.inverseurs = 1;
    e.tensionBobine = 24;
    modulaire(e, 1);
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("tl-16", BAT, "Télérupteur 16 A, bobine 230 V", "iec:telerupteur", "KA");
    e.calibre = 16;
    e.tensionBobine = 230;
    modulaire(e, 1);
    v.push_back(e);
  }

  // Dialogue en façade (Ø 22 mm) et capteurs
  auto facade = [&](const std::string& id, const std::string& designation, const std::string& sym, const std::string& prefixe, int no, int nf) {
    EntreeCatalogue e = E(id, CAPT, designation, sym, prefixe);
    e.contactsNO = no;
    e.contactsNF = nf;
    e.largeur = 30;
    e.hauteur = 30;
    e.profondeur = 45;
    e.rail = false;
    e.porte = true;
    v.push_back(e);
  };
  facade("bp-vert", "Bouton poussoir Ø 22 vert, 1 NO (+ 1 bloc libre)", "iec:bp-no", "S", 2, 1);
  facade("bp-rouge", "Bouton poussoir Ø 22 rouge, 1 NF (+ 1 bloc libre)", "iec:bp-nf", "S", 1, 2);
  facade("au-40", "Arrêt d'urgence Ø 40 coup de poing, déverrouillage par rotation, 1 NF", "iec:arret-urgence", "S", 0, 2);
  facade("com-2", "Commutateur Ø 22 deux positions fixes, 1 NO", "iec:commutateur", "S", 2, 1);
  for (auto [id, couleur] : {std::pair{"v-vert", "vert"}, {"v-rouge", "rouge"}, {"v-orange", "orange"}, {"v-blanc", "blanc"}, {"v-bleu", "bleu"}}) {
    EntreeCatalogue e = E(id, SIGN, std::string("Voyant Ø 22 ") + couleur + " à DEL 24 V", "iec:voyant", "H");
    e.tensionBobine = 24;
    e.largeur = 30;
    e.hauteur = 30;
    e.profondeur = 40;
    e.rail = false;
    e.porte = true;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("sonore-24", SIGN, "Avertisseur sonore 24 V, 90 dB", "iec:sonore", "H");
    e.largeur = 30;
    e.hauteur = 30;
    e.profondeur = 50;
    e.rail = false;
    e.porte = true;
    v.push_back(e);
  }
  auto terrain = [&](const std::string& id, const std::string& famille, const std::string& designation, const std::string& sym, const std::string& prefixe) {
    EntreeCatalogue e = E(id, famille, designation, sym, prefixe);
    e.rail = false;
    e.horsArmoire = true;
    v.push_back(e);
    return &v.back();
  };
  terrain("b-ind-m18", CAPT, "Détecteur inductif M18 PNP NO, portée 8 mm, 3 fils", "iec:capteur-pnp", "B");
  terrain("s-fdc", CAPT, "Interrupteur de position à galet, 1 NO + 1 NF", "iec:fdc-no", "S")->contactsNF = 1;
  terrain("b-thermostat", CAPT, "Thermostat d'armoire 0–60 °C, 1 NF", "iec:thermostat", "B");
  terrain("b-pressostat", CAPT, "Pressostat 1–10 bar, 1 inverseur", "iec:pressostat", "B");
  // Moteurs asynchrones triphasés 400 V (courant nominal typique)
  for (auto [kw, in] : {std::pair{0.37, 1.0}, {0.55, 1.4}, {0.75, 1.9}, {1.1, 2.6}, {1.5, 3.4}, {2.2, 4.8}, {3.0, 6.4}, {4.0, 8.2}, {5.5, 11.0}, {7.5, 14.8}, {11.0, 21.0}}) {
    EntreeCatalogue* e = terrain("m3-" + nombre(kw), PUIS, "Moteur asynchrone triphasé " + nombre(kw) + " kW 400 V, In ≈ " + nombre(in) + " A", "iec:moteur-3", "M");
    e->puissance = kw;
    e->calibre = in;
  }
  {
    EntreeCatalogue* e = terrain("m1-0,37", PUIS, "Moteur monophasé 0,37 kW 230 V", "iec:moteur-1", "M");
    e->puissance = 0.37;
    e->calibre = 2.8;
  }
  {
    EntreeCatalogue* e = terrain("e-chauffe-2000", PUIS, "Résistance chauffante 2 kW 230 V", "iec:resistance-chauffante", "E");
    e->puissance = 2;
  }
  {
    EntreeCatalogue* e = terrain("y-ev-24", PUIS, "Électrovanne 24 V continu, 4 W", "iec:electrovanne", "Y");
    e->tensionBobine = 24;
    e->bobineContinue = true;
  }
  {
    EntreeCatalogue e = E("m-ventil", PUIS, "Ventilateur filtrant d'armoire 230 V, 55 m³/h", "iec:ventilateur", "M");
    e.largeur = 150;
    e.hauteur = 150;
    e.profondeur = 70;
    e.rail = false;
    e.porte = true;
    e.puissance = 0.015;
    v.push_back(e);
  }

  // Alimentations
  for (auto [a, mod] : {std::pair{2.5, 2}, {5.0, 3}, {10.0, 4}}) {
    EntreeCatalogue e = E("g-24-" + nombre(a), ALIM, "Alimentation à découpage 24 V continu " + nombre(a) + " A", "iec:alim-dc", "G");
    e.calibre = a;
    e.tensionBobine = 24;
    modulaire(e, mod);
    e.hauteur = 90;
    e.profondeur = 60;
    v.push_back(e);
  }
  for (double va : {63.0, 100.0, 250.0}) {
    EntreeCatalogue e = E("t-230-24-" + nombre(va), ALIM, "Transformateur de commande 230/24 V " + nombre(va) + " VA", "iec:transfo-1", "T");
    e.puissance = va / 1000;
    e.largeur = va <= 100 ? 70 : 85;
    e.hauteur = 65;
    e.profondeur = va <= 100 ? 70 : 85;
    e.rail = false;
    v.push_back(e);
  }

  // Bornes (largeur au pas du bornier)
  for (auto [id, designation, sym, l] : {std::tuple{"x-2,5", "Borne à vis 2,5 mm² gris", "iec:borne", 5.2}, std::tuple{"x-4", "Borne à vis 4 mm² gris", "iec:borne", 6.2},
                                         std::tuple{"x-6", "Borne à vis 6 mm² gris", "iec:borne", 8.2}, std::tuple{"x-pe-2,5", "Borne de terre 2,5 mm² vert-jaune", "iec:borne-pe", 5.2},
                                         std::tuple{"x-pe-4", "Borne de terre 4 mm² vert-jaune", "iec:borne-pe", 6.2}}) {
    EntreeCatalogue e = E(id, BORN, designation, sym, "X");
    e.largeur = l;
    e.hauteur = 47;
    e.profondeur = 42;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("x-connecteur", BORN, "Connecteur industriel (fiche et embase)", "iec:connecteur", "X");
    e.rail = false;
    e.horsArmoire = true;
    v.push_back(e);
  }

  // Automates et variateurs
  {
    EntreeCatalogue e = E("a-8e8s", AUTO, "Automate compact 24 V continu, 8 entrées, 8 sorties à relais", "iec:automate-8e8s", "A");
    e.largeur = 90;
    e.hauteur = 90;
    e.profondeur = 70;
    v.push_back(e);
  }
  for (auto [kw, l, h, p] : {std::tuple{0.75, 72.0, 145.0, 140.0}, std::tuple{2.2, 107.0, 145.0, 160.0}, std::tuple{4.0, 107.0, 230.0, 175.0}, std::tuple{7.5, 140.0, 280.0, 185.0}}) {
    EntreeCatalogue e = E("u-var-" + nombre(kw), AUTO, "Variateur de fréquence 400 V " + nombre(kw) + " kW", "iec:variateur", "U");
    e.puissance = kw;
    e.largeur = l;
    e.hauteur = h;
    e.profondeur = p;
    e.rail = false;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("u-demarreur-4", AUTO, "Démarreur progressif 400 V 4 kW", "iec:demarreur", "U");
    e.puissance = 4;
    e.largeur = 45;
    e.hauteur = 125;
    e.profondeur = 130;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("p-compteur-1", AUTO, "Compteur d'énergie monophasé modulaire 40 A", "iec:compteur", "P");
    modulaire(e, 1);
    v.push_back(e);
  }

  // Bâtiment
  {
    EntreeCatalogue e = E("xd-prise", BAT, "Socle de prise de courant 2P+T 16 A", "iec:prise", "XD");
    e.rail = false;
    e.horsArmoire = true;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("xd-prise-mod", BAT, "Prise de courant modulaire 2P+T 16 A (tableau)", "iec:prise", "XD");
    modulaire(e, 3);
    v.push_back(e);
  }
  for (auto [id, designation, sym] : {std::tuple{"s-inter", "Interrupteur simple allumage 10 A", "iec:inter-simple"}, std::tuple{"s-vv", "Interrupteur va-et-vient 10 A", "iec:va-et-vient"},
                                      std::tuple{"s-poussoir", "Bouton poussoir 6 A", "iec:poussoir"}}) {
    EntreeCatalogue e = E(id, BAT, designation, sym, "S");
    e.rail = false;
    e.horsArmoire = true;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("e-luminaire", BAT, "Point lumineux (DCL)", "iec:lampe", "E");
    e.rail = false;
    e.horsArmoire = true;
    v.push_back(e);
  }
  // Compléments du bâtiment : appareils posés dans les pièces, ou modulaires au tableau
  const std::string SEC = "Sécurité et courants faibles", MES = "Mesure et instrumentation", PROD = "Production et stockage", HTA = "HTA et postes";
  for (auto [id, famille, designation, sym, prefixe] :
       {std::tuple{"s-inter-double", BAT, "Interrupteur double allumage 10 A", "iec:inter-double", "S"}, std::tuple{"s-inter-volet", BAT, "Interrupteur de volet roulant", "iec:inter-volet", "S"},
        std::tuple{"m-volet", BAT, "Moteur de volet roulant 230 V", "iec:moteur-volet", "M"}, std::tuple{"e-chauffe-eau-200", BAT, "Chauffe-eau électrique 200 L, 2 200 W", "iec:chauffe-eau", "E"},
        std::tuple{"m-vmc", BAT, "Groupe de VMC simple flux deux vitesses", "iec:vmc", "M"}, std::tuple{"xd-sortie-cable", BAT, "Sortie de câble 20 A avec bornier", "iec:sortie-cable", "XD"},
        std::tuple{"xd-prise-32", BAT, "Socle de prise 32 A 2P+T (cuisson)", "iec:prise-32a", "XD"}, std::tuple{"e-baes-45", SEC, "BAES d'évacuation 45 lm, 1 h, SATI", "iec:baes", "E"},
        std::tuple{"e-baes-ambiance", SEC, "BAES d'ambiance 400 lm, 1 h, SATI", "iec:baes", "E"}, std::tuple{"b-detecteur-fumee", SEC, "Détecteur optique de fumée adressable", "iec:detecteur-incendie", "B"},
        std::tuple{"s-dm", SEC, "Déclencheur manuel rouge à membrane réarmable", "iec:declencheur-manuel", "S"}, std::tuple{"xd-rj45", SEC, "Prise RJ45 catégorie 6, grade 3", "iec:prise-rj45", "XD"},
        std::tuple{"xd-tv", SEC, "Prise télévision coaxiale", "iec:prise-tv", "XD"}, std::tuple{"b-presence", SEC, "Détecteur de présence plafond 360°, 230 V", "iec:detecteur-presence", "B"},
        std::tuple{"b-ils", SEC, "Contact magnétique d'ouverture (ILS) en saillie", "iec:contact-ouverture", "B"}, std::tuple{"b-transmetteur", MES, "Transmetteur de pression 0–10 bar, 4-20 mA deux fils", "iec:transmetteur", "B"},
        std::tuple{"b-pt100", MES, "Sonde Pt100 classe A, trois fils, gaine Ø 6 mm", "iec:pt100", "B"}, std::tuple{"b-tc-k", MES, "Thermocouple type K, gaine inox", "iec:thermocouple", "B"},
        std::tuple{"b-codeur", MES, "Codeur incrémental 1 024 points, push-pull 10–30 V", "iec:codeur", "B"}, std::tuple{"g-module-425", PROD, "Module photovoltaïque 425 Wc", "iec:module-pv", "G"},
        std::tuple{"xd-irve-7", PUIS, "Borne de recharge 7,4 kW monophasée, mode 3, type 2", "iec:borne-irve", "XD"},
        std::tuple{"xd-irve-22", PUIS, "Borne de recharge 22 kW triphasée, mode 3, type 2", "iec:borne-irve", "XD"}}) {
    EntreeCatalogue e = E(id, famille, designation, sym, prefixe);
    e.rail = false;
    e.horsArmoire = true;
    v.push_back(e);
  }
  for (auto [id, famille, designation, sym, prefixe, modules] :
       {std::tuple{"ka-minuterie", BAT, "Minuterie modulaire 0,5 à 20 min, préavis d'extinction", "iec:minuterie", "KA", 1},
        std::tuple{"ka-horloge", COMM, "Interrupteur horaire hebdomadaire numérique, 1 inverseur 16 A", "iec:horloge", "KA", 2},
        std::tuple{"km-hc-20", BAT, "Contacteur heures creuses 20 A, 1 F, commande 230 V", "iec:contacteur-hc", "KM", 1},
        std::tuple{"ka-phases", MES, "Relais de contrôle de phases 400 V, ordre, absence, sous-tension", "iec:relais-phases", "KA", 1},
        std::tuple{"p-cpi", MES, "Contrôleur permanent d'isolement pour réseau IT 400 V", "iec:controle-isolement", "P", 4},
        std::tuple{"p-centrale", MES, "Centrale de mesure triphasée, entrées TC 5 A, Modbus", "iec:centrale-mesure", "P", 4},
        std::tuple{"p-horaire", MES, "Compteur horaire modulaire 230 V", "iec:compteur-horaire", "P", 1}}) {
    EntreeCatalogue e = E(id, famille, designation, sym, prefixe);
    modulaire(e, modules);
    v.push_back(e);
  }
  for (auto [id, designation, ratio] : {std::tuple{"t-tc-100", "Transformateur de courant 100/5 A, classe 0,5", 100.0}, std::tuple{"t-tc-250", "Transformateur de courant 250/5 A, classe 0,5", 250.0}}) {
    EntreeCatalogue e = E(id, MES, designation, "iec:tc", "T");
    e.calibre = ratio;
    e.largeur = 45;
    e.hauteur = 60;
    e.profondeur = 40;
    e.rail = false;
    v.push_back(e);
  }
  for (auto [id, designation, sym, prefixe, l, h, p] :
       {std::tuple{"u-onduleur-pv-3,6", "Onduleur photovoltaïque monophasé 3,6 kVA, 2 MPPT", "iec:onduleur-pv", "U", 350.0, 340.0, 150.0},
        std::tuple{"u-asi-1500", "Alimentation sans interruption 1 500 VA, 230 V, en ligne", "iec:asi", "U", 150.0, 230.0, 400.0},
        std::tuple{"q-inverseur-63", "Inverseur de sources motorisé 4P 63 A", "iec:inverseur-sources", "Q", 180.0, 140.0, 120.0},
        std::tuple{"c-batterie-25", "Batterie de condensateurs 25 kvar 400 V", "iec:condensateurs", "C", 300.0, 400.0, 200.0}}) {
    EntreeCatalogue e = E(id, id[0] == 'c' ? PUIS : PROD, designation, sym, prefixe);
    if (std::string(sym) == "iec:onduleur-pv") e.puissance = 3.6;  // kVA
    if (std::string(sym) == "iec:asi") e.puissance = 1.5;
    e.largeur = l;
    e.hauteur = h;
    e.profondeur = p;
    e.rail = false;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("g-groupe-60", PROD, "Groupe électrogène diesel 60 kVA 400 V", "iec:groupe-electrogene", "G");
    e.puissance = 48;
    e.rail = false;
    e.horsArmoire = true;
    v.push_back(e);
  }
  {
    EntreeCatalogue e = E("f-sepam", HTA, "Relais de protection numérique 50/51, 50N/51N", "iec:relais-protection", "F");
    e.largeur = 176;
    e.hauteur = 222;
    e.profondeur = 100;
    e.rail = false;
    e.porte = true;
    v.push_back(e);
  }
  for (auto& e : v) e.prix = prixIndicatif(e);
  return v;
}

}  // namespace

const std::vector<EntreeCatalogue>& catalogueElectrique() {
  static const std::vector<EntreeCatalogue> v = construire();
  return v;
}

const EntreeCatalogue* entreeCatalogue(const std::string& id) {
  static const std::map<std::string, std::size_t> index = [] {
    std::map<std::string, std::size_t> m;
    const auto& v = catalogueElectrique();
    for (std::size_t i = 0; i < v.size(); ++i) m[v[i].id] = i;
    return m;
  }();
  const auto it = index.find(id);
  return it == index.end() ? nullptr : &catalogueElectrique()[it->second];
}

std::vector<const EntreeCatalogue*> entreesPourSymbole(const std::string& symbole) {
  std::vector<const EntreeCatalogue*> r;
  for (const auto& e : catalogueElectrique())
    if (e.symbole == symbole) r.push_back(&e);
  // Les relais temporisés et télérupteurs acceptent aussi les bobines génériques
  return r;
}

std::string prefixeRepere(const std::string& symbole, NormeRepere norme) {
  const ecad::DefSymbole* d = symboleElectrique(symbole);
  const std::string classique = d ? d->prefixe : std::string("A");
  if (norme == NormeRepere::Classique) return classique.empty() ? "A" : classique;
  // CEI 81346-2:2019, codes courants à deux lettres
  static const std::map<std::string, std::string> parSymbole = {
      {"iec:disj-1p", "QA"},           {"iec:disj-1pn", "QA"},          {"iec:disj-2p", "QA"},         {"iec:disj-3p", "QA"},
      {"iec:disj-4p", "QA"},           {"iec:disj-moteur-3p", "QA"},    {"iec:sect-3p", "QB"},         {"iec:inter-sect-3p", "QB"},
      {"iec:inter-sect-4p", "QB"},     {"iec:fusible", "FA"},           {"iec:sect-fusible-3p", "QB"}, {"iec:relais-thermique-3p", "BB"},
      {"iec:diff-2p", "FB"},           {"iec:diff-4p", "FB"},           {"iec:disj-diff-2p", "QA"},    {"iec:agcp", "QA"},
      {"iec:parafoudre", "FC"},        {"iec:bobine-km", "QA"},         {"iec:bobine-ka", "KF"},       {"iec:bobine-tempo-travail", "KF"},
      {"iec:bobine-tempo-repos", "KF"},{"iec:bp-no", "SF"},             {"iec:bp-nf", "SF"},           {"iec:arret-urgence", "SF"},
      {"iec:commutateur", "SF"},       {"iec:interrupteur-levier", "SF"}, {"iec:fdc-no", "BG"},        {"iec:fdc-nf", "BG"},
      {"iec:thermostat", "BT"},        {"iec:pressostat", "BP"},        {"iec:flotteur", "BL"},        {"iec:capteur-pnp", "BG"},
      {"iec:voyant", "PF"},            {"iec:sonore", "PB"},            {"iec:gyrophare", "PF"},       {"iec:moteur-3", "MA"},
      {"iec:moteur-3-yd", "MA"},       {"iec:moteur-1", "MA"},          {"iec:moteur-cc", "MA"},       {"iec:resistance-chauffante", "EB"},
      {"iec:ventilateur", "GQ"},       {"iec:electrovanne", "MB"},      {"iec:lampe", "EA"},           {"iec:transfo-1", "TA"},
      {"iec:transfo-3", "TA"},         {"iec:alim-dc", "TB"},           {"iec:batterie", "GB"},        {"iec:borne", "XD"},
      {"iec:borne-pe", "XD"},          {"iec:connecteur", "XD"},        {"iec:automate-8e8s", "KF"},   {"iec:variateur", "TA"},
      {"iec:demarreur", "QA"},         {"iec:compteur", "PG"},          {"iec:amperemetre", "PG"},     {"iec:voltmetre", "PG"},
      {"iec:inter-simple", "SF"},      {"iec:va-et-vient", "SF"},       {"iec:poussoir", "SF"},        {"iec:telerupteur", "KF"},
      {"iec:prise", "XD"},             {"iec:u-disj", "QA"},            {"iec:u-diff", "FB"},          {"iec:u-agcp", "QA"},
      {"iec:u-dd", "QA"},              {"iec:u-inter", "QB"},           {"iec:u-fusible", "FA"},       {"iec:u-onduleur", "TA"},
      {"iec:u-onduleur-2", "TA"},      {"iec:u-onduleur-3", "TA"},      {"iec:u-onduleur-4", "TA"},      {"iec:u-contacteur", "QA"},
      {"iec:hta-interrupteur", "QB"},  {"iec:hta-disjoncteur", "QA"},   {"iec:hta-sectionneur", "QB"},   {"iec:hta-combine", "QB"},
      {"iec:hta-terre", "QC"},         {"iec:tt", "BE"},                {"iec:tc", "BE"},                {"iec:onduleur-pv", "TA"},
      {"iec:groupe-electrogene", "GA"}, {"iec:inverseur-sources", "QA"}, {"iec:centrale-mesure", "PG"},  {"iec:wattmetre", "PG"},
      {"iec:frequencemetre", "PG"},    {"iec:compteur-horaire", "PG"},  {"iec:pt100", "BT"},             {"iec:thermocouple", "BT"},
      {"iec:codeur", "BG"},            {"iec:baes", "EA"},              {"iec:inter-double", "SF"},      {"iec:inter-volet", "SF"},
      {"iec:minuterie", "KF"},         {"iec:horloge", "KF"},           {"iec:contacteur-hc", "QA"},     {"iec:moteur-volet", "MA"},
      {"iec:chauffe-eau", "EB"},       {"iec:vmc", "GQ"},               {"iec:sortie-cable", "XD"},      {"iec:prise-32a", "XD"},
      {"iec:condensateurs", "CA"},     {"iec:relais-phases", "KF"},     {"iec:declencheur-manuel", "SF"},
  };
  const auto it = parSymbole.find(symbole);
  return it != parSymbole.end() ? it->second : (classique.empty() ? "A" : classique);
}

Encombrement encombrement(const ProjetElectrique& p, const std::string& composant) {
  Encombrement r;
  const ecad::ComposantElec* c = p.schema.composant(composant);
  if (!c) return r;
  const EntreeCatalogue* e = nullptr;
  if (const auto it = c->champs.find("catalogue"); it != c->champs.end()) e = entreeCatalogue(it->second);
  if (e && (e->largeur > 0 || e->modules > 0)) {
    r.largeur = e->largeur > 0 ? e->largeur : 18.0 * e->modules;
    r.hauteur = e->hauteur > 0 ? e->hauteur : 85;
    r.profondeur = e->profondeur > 0 ? e->profondeur : 70;
    r.rail = e->rail;
    r.porte = e->porte;
    r.horsArmoire = e->horsArmoire;
  } else {
    // Gabarit selon le symbole (appareil sans référence de catalogue)
    const std::string& s = c->symbole;
    const RoleSymbole role = roleSymbole(s);
    const ecad::DefSymbole* perso = estAppareilPersonnel(s) ? detail::definitionDe(p, s) : nullptr;
    if (perso) {
      // Appareil personnel : ses dimensions et son montage, sinon le gabarit d'un module (ou d'un appareil de porte)
      const SpecAppareil a = specAppareil(*perso);
      r.porte = a.montage == MontageAppareil::Porte;
      r.rail = a.montage == MontageAppareil::Rail;
      r.horsArmoire = a.montage == MontageAppareil::HorsArmoire;
      if (!r.horsArmoire) {
        r.largeur = a.largeur > 0 ? a.largeur : r.porte ? 30 : 45;
        r.hauteur = a.hauteur > 0 ? a.hauteur : r.porte ? 30 : 85;
        r.profondeur = a.profondeur > 0 ? a.profondeur : r.porte ? 45 : 75;
      }
    } else if (role == RoleSymbole::Borne && s == "iec:connecteur") {
      r.horsArmoire = true;
      r.rail = false;
    } else if (role == RoleSymbole::Borne) {
      r.largeur = 5.2;
      r.hauteur = 47;
      r.profondeur = 42;
    } else if (s.find("moteur") != std::string::npos || s == "iec:capteur-pnp" || s.find("fdc") != std::string::npos || s == "iec:lampe" || s == "iec:prise" ||
               s == "iec:inter-simple" || s == "iec:va-et-vient" || s == "iec:poussoir" || s == "iec:resistance-chauffante" || s == "iec:electrovanne") {
      r.horsArmoire = true;
      r.rail = false;
    } else if (s == "iec:voyant" || s.find("bp-") != std::string::npos || s == "iec:arret-urgence" || s == "iec:commutateur" || s == "iec:sonore") {
      r.largeur = r.hauteur = 30;
      r.profondeur = 45;
      r.rail = false;
      r.porte = true;
    } else if (s.find("disj-1p") != std::string::npos || s == "iec:fusible") {
      r.largeur = 18;
      r.hauteur = 85;
      r.profondeur = 70;
    } else if (s.find("-4p") != std::string::npos) {
      r.largeur = 72;
      r.hauteur = 85;
      r.profondeur = 70;
    } else if (s.find("-3p") != std::string::npos || s == "iec:bobine-km") {
      r.largeur = 54;
      r.hauteur = 85;
      r.profondeur = 80;
    } else if (s.find("-2p") != std::string::npos) {
      r.largeur = 36;
      r.hauteur = 85;
      r.profondeur = 70;
    } else {
      r.largeur = 45;
      r.hauteur = 85;
      r.profondeur = 75;
    }
  }
  // Un bornier occupe la largeur de toutes ses bornes, plus deux butées
  if (roleSymbole(c->symbole) == RoleSymbole::Borne && c->symbole != "iec:connecteur") {
    std::set<std::string> numeros;
    for (const auto& f : p.schema.feuilles)
      for (const auto& s : f.symboles)
        if (s.composant == c->uuid) {
          const ecad::DefSymbole* d = p.schema.symbole(s.symbole);
          if (!d) d = symboleElectrique(s.symbole);
          if (d && !d->broches.empty()) numeros.insert(ecad::numeroBroche(s, d->broches.front().numero));
        }
    const double pas = r.largeur > 0 ? r.largeur : 5.2;
    r.largeur = pas * static_cast<double>(std::max<std::size_t>(1, numeros.size())) + 2 * 9.0;
    r.rail = true;
    r.horsArmoire = false;
  }
  return r;
}

}  // namespace prom::elec
