// Prométhée : installation photovoltaïque raccordée au réseau (guide UTE C 15-712-1, NF C 15-100 partie 7-712).
// Calepinage (modules en série selon les températures extrêmes du site, chaînes réparties sur les entrées MPPT),
// protection des chaînes (fusibles quand le courant inverse peut dépasser ce que supporte un module), câbles
// continus et alternatifs (courant admissible, chute de tension), interrupteur-sectionneur continu, parafoudres
// (longueur critique), protections du côté alternatif, branchement, production annuelle estimée. Les avis citent
// leur règle. Le calcul aide à concevoir : la notice de l'onduleur et la visite du Consuel font foi. Aucune
// dépendance à l'interface. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "promethee/electrique_distribution.hpp"

namespace prom::elec {

struct ResultatPV {
  // Calepinage
  double kTemperature = 1;          // coefficient de la tension à vide au froid
  double uocModuleMax = 0;          // V, au froid
  double vmpModuleMin = 0, vmpModuleMax = 0;  // V, au chaud et au froid
  int seriesMin = 0, seriesMax = 0; // modules en série admissibles
  int modulesParChaine = 0, chaines = 0, chainesParMppt = 0, modulesPoses = 0;
  std::vector<int> repartition;     // chaînes sur chaque entrée : onduleur 1 (entrée 1, 2…), puis onduleur 2…
  double puissanceCrete = 0;        // kWc
  double ratioDCAC = 0;             // onduleur le plus chargé
  double uocChaine = 0, vmpChaineMin = 0, vmpChaineMax = 0;  // V
  double iscChaine = 0, iscMppt = 0;  // A (1,25 Isc par chaîne)
  // Protection des chaînes
  int chainesMaxSansProtection = 0;
  bool fusibles = false;
  double calibreFusible = 0;        // A (gPV)
  // Câbles continus
  double izRequis = 0, sectionDC = 0, izDC = 0, chuteDC = 0;  // A, mm², A, %
  bool boiteJonction = false;       // plus de chaînes par entrée que de connecteurs : boîte de jonction et câble principal
  double sectionPrincipal = 0, izPrincipal = 0;
  // Sectionnement continu
  double sectionneurTension = 0, sectionneurCourant = 0;  // V, A
  // Parafoudres
  double longueurCritique = 0;      // m
  bool parafoudreDC = false, parafoudreAC = false;
  std::string typeParafoudreAC;     // « type 1 », « type 2 »
  // Côté alternatif (par onduleur, puis pour l'ensemble)
  double courantAC = 0, calibreAC = 0, sectionAC = 0, chuteAC = 0, pdcAC = 0;  // A, A, mm², %, kA
  std::string differentielAC;
  double puissanceAC = 0, courantACTotal = 0, calibreGeneral = 0;  // VA, A, A
  double reglageBranchement = 0;    // A : disjoncteur de branchement de la vente totale (≤ 36 kVA)
  // Production
  double coefOrientation = 1;
  double production = 0;            // kWh par an
  std::vector<AvisCalcul> avis;
  int erreurs() const;
  int alertes() const;
};
ResultatPV calculerPV(const InstallationPV& i);

// Coefficient de production selon l'orientation (écart au sud) et l'inclinaison, interpolé dans le tableau usuel
// en France (1 plein sud à 30°, 0,93 à plat, 0,55 en façade est ou ouest)
double coefficientOrientation(double orientation, double inclinaison);
// Productibles indicatifs (kWh/kWc/an, plein sud à 30°, pertes comprises) par grande région : un ordre de grandeur,
// PVGIS donne celui de l'adresse
const std::vector<std::pair<std::string, double>>& productiblesIndicatifs();

// Matériel générique pour commencer (les valeurs se reprennent de la fiche du fabricant)
const std::vector<ModulePV>& modulesPV();
const std::vector<OnduleurPV>& onduleursPV();
// Installation de départ : un module, un onduleur et un nombre de modules qui conviennent ensemble
InstallationPV installationPVType(double kWc);
const char* nomBatimentPV(BatimentPV b);

// Note de calcul (HTML) et schéma unifilaire (un folio, remplacé quand on le redessine) ; rend l'indice du folio
std::string htmlNotePV(const InstallationPV& i, const ResultatPV& x, const std::string& projet);
std::size_t genererUnifilairePV(ProjetElectrique& p);

}  // namespace prom::elec
