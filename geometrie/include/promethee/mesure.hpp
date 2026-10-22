// Prométhée : mesure (M11) — chaque élément décrit (aire, périmètre, longueur, rayon, Ø, angle d'arc, coordonnées),
// puis entre deux éléments la distance minimale et ses points, ΔX ΔY ΔZ, la distance de centre à centre, l'entraxe
// de deux axes parallèles, l'écart de deux plans parallèles et l'angle vrai (0 à 180°) ; au-delà, les totaux
// (longueurs, aires). Utilisée par l'atelier Pièces et par l'atelier d'assemblage. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include "promethee/unites.hpp"

namespace prom {

struct ElementMesure {
  enum class Genre { Forme, Plan, Axe, Point } genre = Genre::Forme;
  TopoDS_Shape forme;  // face, arête, sommet ou corps (genre Forme)
  gp_Pln plan;         // plan infini (plan de base, plan de référence)
  gp_Ax1 axe;          // droite infinie (axe de base, axe de référence)
  gp_Pnt point;
  std::string nom;     // nom à montrer (« Plan de base XY »), vide : décrit d'après la géométrie
  TopoDS_Shape solide; // solide qui porte la face, pour en prendre la normale sortante (facultatif)
};

struct GrandeurMesure {
  enum class Nature { Longueur, Aire, Angle, Volume, Nombre } nature = Nature::Longueur;
  std::string nom;
  double valeur = 0;
};

struct DescriptionMesure {
  std::string type;                      // « Face plane », « Face cylindrique », « Arête circulaire »…
  std::vector<GrandeurMesure> grandeurs; // aire, périmètre, Ø, rayon, longueur, angle de l'arc, X, Y, Z…
  bool aCentre = false;                  // centre d'un cercle, d'une sphère, milieu d'un axe de cylindre, point
  gp_Pnt centre;
  bool aAxe = false;                     // axe d'un cylindre, d'un cône, d'un cercle ; droite d'une arête
  gp_Ax1 axe;
  bool aNormale = false;                 // normale sortante d'une face plane, normale d'un plan
  gp_Dir normale;
  bool aDirection = false;               // direction d'une arête droite (du premier au dernier sommet)
  gp_Dir direction;
  double longueur = 0, aire = 0;
};

struct ResultatMesure {
  std::vector<DescriptionMesure> elements;
  // Deux éléments
  bool distance = false;
  double distanceMin = 0;
  gp_Pnt pa, pb;                 // points de la distance minimale (sur le premier, sur le second)
  gp_Vec delta;                  // pb − pa : ΔX ΔY ΔZ
  bool centres = false;          // les deux ont un centre
  double distanceCentres = 0;
  gp_Pnt ca, cb;
  gp_Vec deltaCentres;
  bool entraxe = false;          // deux axes parallèles (trous, arbres, cercles)
  double valeurEntraxe = 0;
  bool ecartPlans = false;       // deux faces planes ou plans parallèles
  double valeurEcartPlans = 0;
  bool angle = false;            // entre normales, directions et axes ; 0 à 180°
  double angleDeg = 0;
  // Nature de l'angle : entre deux normales sortantes (0 à 180° ; l'angle dièdre intérieur en est le supplément),
  // entre deux directions d'arêtes (0 à 180°), entre axes non orientés (0 à 90°), entre une droite et un plan
  enum class Angle { Normales, Directions, Axes, DroitePlan } natureAngle = Angle::Directions;
  // Plus de deux éléments : totaux
  int faces = 0, aretes = 0, sommets = 0;
  double longueurTotale = 0, aireTotale = 0;
};

DescriptionMesure decrireElement(const ElementMesure& e);
ResultatMesure mesurer(const std::vector<ElementMesure>& elements);
// Lignes lisibles (unités du document) : une par élément, puis les mesures entre eux
std::vector<std::string> lignesMesure(const ResultatMesure& r, const Unites& u);
std::string texteGrandeur(const GrandeurMesure& g, const Unites& u);

}  // namespace prom
