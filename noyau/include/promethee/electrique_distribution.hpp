// Prométhée : calcul d'une installation basse tension, à la manière des logiciels de bureau d'études (méthodes du
// guide UTE C 15-105 et règles de la NF C 15-100). De la source au dernier récepteur : bilan des puissances avec
// facteurs d'utilisation et de simultanéité, courant d'emploi, protection (calibre, réglage, pouvoir de coupure),
// canalisation (courant admissible corrigé, sections de phase, de neutre et de protection, conducteurs en
// parallèle), chutes de tension cumulées et au démarrage des moteurs, courants de court-circuit maximal et minimal
// par la méthode des impédances, protection des personnes selon le régime de neutre (TT, TN, IT), contraintes
// thermiques, sélectivité avec l'amont, compensation de l'énergie réactive. Chaque résultat se justifie ; chaque
// écart à une règle devient un avis (information, alerte ou erreur) qui cite son article.
// Le calcul aide à concevoir ; il ne remplace ni la note de calcul de l'installateur ni les tables des
// constructeurs (sélectivité énergétique, filiation). Aucune dépendance à l'interface. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electrique.hpp"

namespace prom::elec {

enum class NiveauAvis : std::uint8_t { Info, Alerte, Erreur };
struct AvisCalcul {
  NiveauAvis niveau = NiveauAvis::Info;
  std::string regle;    // « NF C 15-100 433.1 », « UTE C 15-105 »…
  std::string message;  // ce qui se passe et quoi faire, en clair
};

struct ResultatDepartBT {
  std::string uuid;
  int profondeur = 0;             // 0 : alimenté par la source
  // Puissances appelées (après facteurs d'utilisation et de simultanéité) et courant d'emploi
  double p = 0, q = 0, s = 0;     // kW, kvar, kVA
  double cosPhi = 1;
  double ib = 0;                  // A, phase la plus chargée
  double iNeutre = 0;             // A, courant du neutre dû à l'harmonique 3
  // Protection
  TypeProtection protection = TypeProtection::Modulaire;  // type retenu (jamais Auto)
  double in = 0, ir = 0;          // calibre et réglage (A)
  double imBas = 0, imHaut = 0;   // déclenchement instantané (ou court retard) : bornes basse et haute (A)
  double pdcRequis = 0, pdc = 0;  // kA
  std::string designationProtection;
  // Canalisation
  double k = 1;                   // facteur de correction global (température, groupement, harmoniques, âme)
  double courantDimensionnement = 0, iz = 0;  // A
  double section = 0, sectionNeutre = 0, sectionPE = 0;  // mm² (neutre 0 : non distribué)
  int parallele = 1;
  std::string designationCable;
  // Chutes de tension (%)
  double chute = 0, chuteCumulee = 0, chuteLimite = 5, chuteDemarrage = 0;
  // Courants de court-circuit au bout de la canalisation (kA)
  double ik3max = 0;              // triphasé maximal
  double ik1max = 0;              // phase-neutre maximal (0 sans neutre)
  double ikMin = 0;               // minimal (phase-neutre, sinon biphasé)
  double ief = 0;                 // défaut phase-masse minimal (TN, IT double défaut)
  // Protection des personnes (contacts indirects)
  double ia = 0;                  // courant assurant la coupure dans le temps prescrit (A)
  double longueurMax = 0;         // m, méthode conventionnelle (0 : sans objet)
  bool personnesProtegees = true;
  std::string protectionPersonnes;
  // Contraintes thermiques (A²s)
  double i2tLaisse = 0, i2tAdmissible = 0;
  // Sélectivité avec la protection du tableau amont
  std::string selectivite;
  std::vector<AvisCalcul> avis;
  int erreurs() const;
  int alertes() const;
};

struct ResultatReseauBT {
  std::vector<ResultatDepartBT> departs;  // même ordre que les départs du réseau
  double p = 0, q = 0, s = 0, cosPhi = 1, ib = 0;  // appel total (kW, kvar, kVA, A)
  double ik3Source = 0;           // kA aux bornes de la source (point de livraison)
  double rSource = 0, xSource = 0;  // mΩ, par phase
  double inSource = 0;            // A : courant assigné de la source
  double tauxCharge = 0;          // % de la puissance de la source
  double compensation = 0;        // kvar pour atteindre le cos φ visé
  double transformateurConseille = 0;  // kVA (appel + 25 % de réserve)
  std::vector<AvisCalcul> avis;   // avis sur la source et l'ensemble
  int erreurs = 0, alertes = 0;   // tous départs compris
  const ResultatDepartBT* depart(const std::string& uuid) const;
};

// Calcule toute l'installation. Ce qui vaut 0 dans les données (calibre, section, pouvoir de coupure…) est choisi,
// ce qui est donné est vérifié.
ResultatReseauBT calculerReseau(const ReseauBT& r);

// ---------- Grandeurs des appareils ----------
// Fusible gG (CEI 60269-1) : courant qui le fait fondre à coup sûr en 5 s (« temps » ≥ 1) ou en 0,1 s ; 0 hors de
// la gamme 16–630 A
double courantFusionGG(double calibre, double temps);

// ---------- Aides à la saisie ----------
const char* nomTypeSource(TypeSourceBT t);
const char* nomTypeCharge(TypeCharge t);
const char* nomPolarite(Polarite p);
const char* nomTypeProtection(TypeProtection t);
const char* nomAme(Ame a);
const char* nomRegime(RegimeNeutre r);
// Départ neuf d'un type, aux valeurs usuelles (puissance, cos φ, polarité, différentiel des prises…), repéré à la
// suite dans le réseau
DepartBT nouveauDepart(const ReseauBT& r, TypeCharge t, const std::string& parent);
std::string prochainRepere(const ReseauBT& r, TypeCharge t);
// Ordre d'affichage en arbre (profondeur d'abord) : indices des départs et leur profondeur
std::vector<std::pair<std::size_t, int>> ordreArbre(const ReseauBT& r);
// Supprime un départ et tout ce qu'il alimente ; rend le nombre de départs retirés
int supprimerDepart(ReseauBT& r, const std::string& uuid);
// Installations prêtes à ajuster
enum class ModeleReseau : std::uint8_t { Vide, Tertiaire, Atelier, ParkingIRVE, Maison };
const char* nomModeleReseau(ModeleReseau m);
ReseauBT reseauModele(ModeleReseau m);

// ---------- Documents ----------
// Note de calcul : un départ par ligne, toutes les grandeurs et les avis (CSV au point-virgule, UTF-8 avec BOM)
std::string csvNoteDeCalcul(const ReseauBT& r, const ResultatReseauBT& x);
// Note de calcul complète en HTML (source, bilan, tableau des départs, justifications, avis, hypothèses)
std::string htmlNoteDeCalcul(const ReseauBT& r, const ResultatReseauBT& x, const std::string& projet);
// Schéma unifilaire de l'installation : un folio par tableau (suites au besoin), protections, câbles et récepteurs
// annotés des résultats ; remplace les folios déjà tirés de la distribution. Rend l'indice du premier folio.
std::size_t genererUnifilaire(ProjetElectrique& p);

}  // namespace prom::elec
