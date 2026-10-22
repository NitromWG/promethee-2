// Prométhée : tôlerie (M5). Une tôle de base (profil fermé d'une esquisse, épaisseur constante) reçoit des bords
// tombés sur ses arêtes droites, et ceux-ci d'autres bords tombés. Chaque paroi est connue par son repère, ses côtés
// et sa place dans le déplié : la pièce pliée et son état déplié se construisent ensemble, exactement.
//
// Longueur développée d'un pli d'angle θ (radians), de rayon intérieur r, dans une tôle d'épaisseur e :
//   BA = θ · (r + K · e)
// où K situe la fibre neutre dans l'épaisseur. Sans valeur donnée, K suit la norme DIN 6935 :
//   k = 0,65 + 0,5 · log10(r / e) (borné à [0,5 ; 1]), K = k / 2 ; r / e > 5 : K = 0,5.
//
// Coins : quand un bord tombé arrive sur une arête voisine déjà pliée, sa largeur est réduite pour laisser le pli
// voisin libre (jeu de 0,2 mm) et le coin est dégagé ; un bord tombé plus court que son arête (retraits) reçoit des
// dégagements de pli rectangulaires ou oblongs, larges de l'épaisseur. Le déplié est rendu en DXF : contour,
// lignes de pliage vers le haut et vers le bas sur deux calques, et une note par pli (sens, angle, rayon).
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include "promethee/piece.hpp"

namespace prom {

double facteurKDin6935(double rayon, double epaisseur);
// Facteur K retenu pour une tôle de base (le sien s'il est donné, sinon DIN 6935)
double facteurKTole(const FonctionPiece& tole);
// Longueur développée d'un pli (mm), angle en degrés
double longueurDeveloppee(double angleDegres, double rayon, double epaisseur, double facteurK);

struct LignePli {
  int fonction = 0;            // bord tombé
  gp_Pnt a, b;                 // ligne médiane du pli dans le plan du déplié
  gp_Pnt debutA, debutB;       // début et fin de la zone pliée (lignes de tangence)
  gp_Pnt finA, finB;
  double angle = 90, rayon = 1, facteurK = 0.5, developpee = 0;
  bool versLeHaut = true;      // vu de dessus du déplié (face « dessus » de la tôle de base vers soi)
  std::string note;            // « HAUT 90° R1,5 »
};

struct DeplieTole {
  bool present = false;
  int base = 0;                // fonction tôle de base
  double epaisseur = 0, rayon = 0, facteurK = 0;
  gp_Ax3 repere;               // plan du déplié : celui de la tôle de base, face « dessous » à z = 0
  TopoDS_Shape contour;        // face plane du déplié (trous compris)
  std::vector<LignePli> plis;
  std::vector<std::string> alertes;  // fonctions non reprises dans le déplié, etc.
  double xmin = 0, ymin = 0, xmax = 0, ymax = 0;  // encombrement dans le repère du déplié
};

// Solide du déplié (contour épaissi), placé sur la tôle de base : à montrer à la place de la pièce pliée
TopoDS_Shape solideDeplie(const DeplieTole& d);
// DXF R12 du déplié, en mm, coin bas-gauche à l'origine : CONTOUR, PLIS-HAUT, PLIS-BAS (tirets), NOTES
std::string dxfDeplie(const DeplieTole& d, const std::string& titre = {});
// SVG du déplié à l'échelle 1 (aperçu, impression)
std::string svgDeplie(const DeplieTole& d, const std::string& titre = {});

}  // namespace prom
