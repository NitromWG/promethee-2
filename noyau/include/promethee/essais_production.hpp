// Prométhée : essais, mise en service et fiabilité (lot L17 ; 17.2 à 17.5, 17.7 à 17.10 ; TST-01 à TST-10,
// FIA-01 à FIA-06). Plan de mise sous tension tiré des rails et des régulateurs, entrée avant sortie ; valeurs
// attendues portées par les points de test ; couverture des accès de test ; procès-verbal par numéro de série, comparé
// aux valeurs attendues ; contraintes d'emploi face aux valeurs nominales des articles ; durée de vie à la chaleur
// (loi des 10 °C) ; taux de défaillance, MTBF et modes de défaillance par famille ; conformité RoHS et REACH.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Mise sous tension (17.2) ----------
struct EtapeMiseSousTension {
  int numero = 0;
  std::string action;   // « Ohmmètre entre +5V et GND », « Alimenter VIN à 9 V, limite 120 mA », « Mesurer +3V3 »
  std::string reseau;
  std::string attendu;  // « > 100 Ω », « 3,30 V ± 5 % »
  double valeur = 0, min = 0, max = 0;  // attendu chiffré quand il l'est
};
struct PlanMiseSousTension {
  std::vector<std::string> entrees;  // rails qui arrivent de l'extérieur (connecteur, pile, source)
  std::vector<std::string> rails;    // dans l'ordre : entrée avant sortie de chaque régulateur
  std::vector<EtapeMiseSousTension> etapes;
  std::vector<std::string> alertes;
};
PlanMiseSousTension planMiseSousTension(const ConceptionElec& c);

// ---------- Points de test et couverture (17.3, 17.4) ----------
struct PointDeTest {
  std::string reference, reseau;
  std::string grandeur = "tension";  // « tension », « courant », « fréquence », « résistance »
  double attendue = 0, tolerance = 0.05;  // tolérance relative
  bool chiffree = false;
  std::string forme;  // « continu », « carré 1 kHz »…
};
// Points de test (symbole « Point_test ») avec leurs champs « Valeur attendue » (« 3,3 V », « 1 kHz »), « Tolérance »
// (« 5 % ») et « Forme »
std::vector<PointDeTest> pointsDeTest(const ConceptionElec& c);
struct CouvertureTest {
  int reseaux = 0, accessibles = 0;
  double taux = 0;  // part des réseaux accessibles
  std::vector<std::string> sansAcces;
};
// Un réseau est accessible par un point de test ou une pastille traversante (pointe de lit à clous, 8.8)
CouvertureTest couvertureTest(const ConceptionElec& c);

// ---------- Procès-verbal (17.5) ----------
struct MesurePv {
  std::string point;  // repère du point de test ou réseau
  double valeur = 0;
};
struct LignePv {
  std::string point, reseau, attendu;
  double mesure = 0;
  bool mesuree = false, conforme = false;
};
struct ProcesVerbal {
  std::string numeroSerie, date, operateur;
  std::vector<LignePv> lignes;
  bool conforme = false;  // toutes les mesures attendues faites et dans leur tolérance
  std::string csv() const;
};
ProcesVerbal procesVerbal(const ConceptionElec& c, const std::string& numeroSerie, const std::vector<MesurePv>& mesures, const std::string& operateur = {},
                          const std::string& date = {});

// ---------- Contraintes d'emploi (17.7) ----------
struct ContrainteEmploi {
  std::string reference, grandeur;   // « tension », « puissance »
  double emploi = 0, nominale = 0, taux = 0;  // taux = emploi / nominale
  bool depasse = false;              // au-delà du taux admis (« derating »)
  std::string message;
};
// Tension de service des condensateurs (rails qu'ils voient), puissance des résistances entre deux rails connus ;
// « admis » : part de la valeur nominale permise (0,8 pour la tension, 0,5 pour la puissance par défaut)
std::vector<ContrainteEmploi> contraintesEmploi(const ConceptionElec& c, double admisTension = 0.8, double admisPuissance = 0.5);

// ---------- Durée de vie et fiabilité (17.8, 17.9) ----------
// Loi des 10 °C : la durée de vie double à chaque 10 °C sous la température nominale
double dureeVie(double heuresNominales, double temperatureNominale, double temperatureService);
struct LigneFiabilite {
  std::string reference, famille;
  double fit = 0;  // défaillances par 10⁹ heures
  std::map<std::string, double> modes;  // « ouvert » → 0,6…
};
struct Fiabilite {
  std::vector<LigneFiabilite> lignes;
  double fitTotal = 0, mtbfHeures = 0;
  std::map<std::string, double> partParFamille;
};
// Table par famille (modifiable par les paramètres « fit » des composants), à la température de service
Fiabilite fiabilite(const ConceptionElec& c, double temperature = 40);

// ---------- Conformité (17.10) ----------
struct Conformite {
  std::vector<std::string> nonConformes;  // « R3 : RoHS non »
  std::vector<std::string> inconnus;      // états non renseignés
  int conformes = 0;
  bool complete() const { return nonConformes.empty() && inconnus.empty(); }
};
// Paramètres « rohs », « reach », « sans_plomb », « sans_halogene » du composant ou de son article (« oui », « non »)
Conformite conformite(const ConceptionElec& c, const std::vector<std::string>& exigences = {"rohs", "reach"});

}  // namespace prom::ecad
