// Prométhée : fabrication détaillée (0.34). Perçage complet (4.13, 4.27 : fentes en Excellon détourées ou en G85,
// options Excellon et EIA, perçage en Gerber X2, fichiers par paire de couches, contre-perçage, diamètres ramenés aux
// forets du profil, tableau et plan de perçage), Gerber X3 des composants (8.7), remplissage des vias IPC-4761
// (8.22), usinages de la carte nue (8.12 : demi-trous, bord métallisé, biseau, lamages) et rapport des paires de
// perçage. Tout part du modèle de fabrication indépendant des formats (electronique.hpp). Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Options d'écriture Excellon (4.27, FAB-03)
struct OptionsExcellon {
  bool g85 = false;          // fentes en G85 (une ligne) plutôt que détourées (G00, M15, G01, M16)
  bool pouces = false;       // INCH plutôt que METRIC
  int decimales = 4;         // chiffres après la virgule (format décimal explicite)
  bool zerosEntiers = false; // coordonnées entières à zéros de tête gardés (« 3.3 », LZ) plutôt que décimales
  bool eia = false;          // ruban EIA RS-358 : pas d'en-tête M48 détaillé, outils seuls (anciennes perceuses)
  bool attributs = true;     // commentaires X2 (TF.FileFunction, TA.AperFunction)
  bool xnc = false;          // XNC (Ucamco) : en-tête minimal, fentes détourées, pas de G85
};
// Paire de couches d'un perçage (4.27, 8.22) : un fichier par paire, comme le demandent les fabricants
struct PairePercage {
  Couche de = Couche::CuivreDessus, a = Couche::CuivreDessous;
  bool metallise = true, contrePercage = false;
  int trous = 0;
  std::string nomFichier, fonction;  // « 1-2 », « Plated,1,2,Blind »
};
std::vector<PairePercage> pairesPercage(const ModeleFabrication& m);
// Fichier Excellon d'une paire (toutes les paires traversantes si « paire » est nulle)
std::string excellonPaire(const ModeleFabrication& m, const PairePercage& paire, const OptionsExcellon& o, const std::string& version);
// Perçage en Gerber X2 (4.27) : flash par trou, tracé par fente, %TF.FileFunction,Plated,1,2,PTH
std::string gerberPercage(const ModeleFabrication& m, const PairePercage& paire, const std::string& version);
// Tableau des perçages (4.27) : outil, diamètre, tolérance, nombre, métallisé, fentes, paire ; en CSV et en JSON
struct LignePercage {
  int outil = 0;
  Nm diametre = 0, longueur = 0, tolerance = 0;
  bool metallise = true;
  int nombre = 0;
  std::string paire, fonction;
};
std::vector<LignePercage> tableauPercages(const ModeleFabrication& m);
std::string tableauPercagesCsv(const ModeleFabrication& m);
std::string tableauPercagesJson(const ModeleFabrication& m);
// Plan de perçage (4.27, 8.7) : contour, chaque trou marqué du symbole de son outil, tableau des outils ; en SVG
std::string planPercageSvg(const ModeleFabrication& m, const std::string& titre);
// Rapport des paires de perçage (4.27) : couches reliées, trous, fichiers
std::string rapportPairesPercage(const ModeleFabrication& m);
// Diamètres voisins ramenés aux forets du profil (4.27) : chaque trou prend le foret le plus proche, s'il est à moins
// de « tolerance » ; rend le nombre de trous changés et ce qui n'a pas de foret
int ramenerAuxForets(ModeleFabrication& m, const std::vector<Nm>& forets, Nm tolerance, std::vector<std::string>* sansForet = nullptr);
// Forets usuels des fabricants (0,2 à 6,3 mm)
const std::vector<Nm>& foretsUsuels();

// Gerber X3 (8.7, FAB-02, FAB-04) : couche des composants d'une face, un point par composant avec ses attributs
// .CRot, .CMfr, .CMPN, .CVal, .CMnt, .CFtp, .CHgt et ses broches (.P)
std::string gerberComposants(const ConceptionElec& c, bool dessous, const std::string& version);
// Vias à remplir (8.22, IPC-4761) : Gerber « Viafill », un flash par via remplie
std::string gerberViaFill(const ModeleFabrication& m, const ConceptionElec& c, const std::string& version);
bool aDesViasRemplies(const ConceptionElec& c);
// Usinages de la carte nue (8.12) : ce que le gbrjob doit déclarer (Castellated, EdgePlating, EdgeConnector,
// EdgeConnectorBevelled), d'après les pastilles crénelées et les traitements du contour
struct UsinagesCarte {
  bool castellee = false, bordMetallise = false, connecteurBord = false, biseau = false, lamages = false;
  int demiTrous = 0;
};
UsinagesCarte usinagesCarte(const ConceptionElec& c);

}  // namespace prom::ecad
