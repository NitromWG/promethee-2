// Prométhée : sorties pour le fabricant et l'assembleur (lot L8 de la 0.34 ; 8.5, 8.6, 8.15 ; FAB-07, FAB-08, FAB-13,
// FAB-15, FAB-18, FAB-20, F5). Nomenclature configurable (colonnes, regroupement sur la valeur numérique, formats
// JLCPCB et PCBWay) ; corrections de rotation par motif d'empreinte selon le profil de l'assembleur ; modèle commun
// des positions (composants, mires, points de test, face, point de référence), écrit par des gabarits de colonnes
// en fichiers de données (OpenPnP, Neoden, Charmhigh, MacroFab, JLCPCB), copies d'un panneau et origine posée ;
// statistiques et récapitulatif de fabrication ; jeu « archive pour le fabricant » (« projet_revB_JLCPCB.zip »).
// Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/fabrication_avancee.hpp"

namespace prom::ecad {

// ---------- Nomenclature configurable (8.5) ----------
// Colonnes connues : « quantite », « reperes », « valeur », « empreinte », « fabricant », « mpn », « fournisseur »,
// « lcsc », « description », « genre » (CMS, traversant), « face » ; un en-tête par colonne (vide : le nom français)
struct OptionsNomenclature {
  std::string format = "standard";  // « standard », « jlcpcb », « pcbway »
  std::vector<std::string> colonnes{"quantite", "reperes", "valeur", "empreinte", "fabricant", "mpn", "fournisseur", "description"};
  std::vector<std::string> entetes;  // remplacent les noms français, dans l'ordre des colonnes
  bool regrouperValeur = true;       // 4k7, 4,7 kΩ et 4700 sur une seule ligne
  bool nonMontes = false;            // les « ne pas monter » listés à part, à la fin
  std::string separateur = ",";
  std::string separateurReperes = ",";
};
OptionsNomenclature optionsNomenclature(const std::string& format);  // « jlcpcb », « pcbway », « standard »
// Tableau : la première ligne est l'en-tête
std::vector<std::vector<std::string>> tableauNomenclature(const ConceptionElec& c, const OptionsNomenclature& o);
std::string csvNomenclatureFormat(const ConceptionElec& c, const OptionsNomenclature& o);

// ---------- Corrections de rotation (8.5, F5) ----------
// Le zéro de l'assembleur diffère de celui de la bibliothèque pour certaines familles (SOT-23, SOIC, QFN…) : l'angle
// ajouté au motif d'empreinte qui correspond (« SOT-23* », « SOIC-* », « *QFN* ») ; le premier motif l'emporte
struct CorrectionRotation {
  std::string motif;
  double angle = 0;
  bool dessousSeulement = false;
};
std::vector<CorrectionRotation> correctionsRotation(const std::string& profil);  // « JLCPCB », « PCBWay » ; vide : aucune
double rotationCorrigee(const std::string& empreinte, double rotation, bool dessous, const std::vector<CorrectionRotation>& corrections);

// ---------- Positions (8.15) ----------
struct Position {
  std::string repere, valeur, empreinte, genre = "composant";  // « composant », « mire », « test »
  double x = 0, y = 0;     // mm, repère de fabrication (coin bas gauche, Y vers le haut) ou origine posée
  double rotation = 0;     // degrés, sens direct vu de dessus, de 0 à 360, corrections du profil comprises
  bool dessous = false;
  int copie = 0;           // panneau : rang de la copie (1…), 0 pour la carte seule ou le cadre
};
struct OptionsPositions {
  bool composants = true, mires = true, pointsTest = false;
  bool dessus = true, dessous = true;
  std::optional<Pt> origine;   // origine posée à la souris (repère de la carte) ; absente : coin bas gauche
  std::string profil;          // corrections de rotation (« JLCPCB ») ; vide : aucune
  std::optional<OptionsPanneau> panneau;  // copies du panneau, et ses trois mires sur le cadre
};
std::vector<Position> positions(const ConceptionElec& c, const OptionsPositions& o);
// Trois mires du cadre d'un panneau (coins bas gauche, bas droit, haut gauche, au milieu du rail), repère du panneau
std::vector<Position> miresPanneau(const ConceptionElec& c, const OptionsPanneau& p);
// Origine de la copie (ligne, colonne) dans le repère de fabrication du panneau, et taille du panneau (mm)
std::pair<double, double> origineCopie(const ConceptionElec& c, const OptionsPanneau& p, int ligne, int colonne);
std::pair<double, double> taillePanneau(const ConceptionElec& c, const OptionsPanneau& p);

// Gabarit d'écriture des positions, en fichier de données (JSON) : en-tête, modèle de ligne, séparateur. Champs :
// {repere} {valeur} {empreinte} {genre} {x} {y} {rotation} {face} {face:T/B} {face:Top/Bottom} {copie} ; un nombre
// prend ses décimales après « : » ({x:3})
struct GabaritPositions {
  std::string nom, extension = "csv", entete, ligne;
  bool rotationSigneeDessous = false;  // dessous : angle négatif (Neoden)
  bool miroirDessous = false;          // dessous : x pris en miroir (vu de dessous)
};
const std::vector<GabaritPositions>& gabaritsPositions();  // OpenPnP, Neoden, Charmhigh, MacroFab, JLCPCB, Prométhée
std::optional<GabaritPositions> gabaritPositions(const std::string& nom);
std::optional<GabaritPositions> lireGabaritPositions(const std::string& json, std::string* erreur = nullptr);
std::string texteGabaritPositions(const GabaritPositions& g);
std::string ecrirePositions(const std::vector<Position>& p, const GabaritPositions& g, double largeurCarteMm = 0);

// ---------- Statistiques et récapitulatif (8.6, FAB-15, FAB-18) ----------
struct StatistiquesFabrication {
  double largeur = 0, hauteur = 0, surface = 0;  // mm, mm²
  int couchesCuivre = 2;
  double epaisseur = 1.6;                        // mm
  int composants = 0, cms = 0, traversants = 0, dessus = 0, dessous = 0, nonMontes = 0, lignesNomenclature = 0;
  int pastilles = 0, trous = 0, trousMetallises = 0, trousNonMetallises = 0, diametresDifferents = 0, vias = 0;
  double plusPetitTrou = 0, plusPetiteVia = 0, plusFinePiste = 0, plusPetitIsolement = 0;  // mm
  double longueurPistes = 0;  // mm
  int reseaux = 0, reseauxNonRoutes = 0;
  std::string finition, couleurVernis, couleurSerigraphie;
};
StatistiquesFabrication statistiquesFabrication(const ConceptionElec& c);
std::string statistiquesJson(const StatistiquesFabrication& s);
std::string recapitulatifFabrication(const ConceptionElec& c, const StatistiquesFabrication& s, const std::string& fabricant, const std::vector<std::string>& fichiers);
// Jeu « archive pour le fabricant » : Gerber, perçages, nomenclature et placement au format du fabricant,
// récapitulatif et statistiques, en un zip « {projet}_rev{revision}_{FABRICANT} »
JeuSorties jeuArchiveFabricant(const std::string& fabricant);

}  // namespace prom::ecad
