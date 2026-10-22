// Prométhée : lecture générale d'un dessin DXF (M1) et insertion dans une esquisse.
// Entités lues : LINE, ARC, CIRCLE, LWPOLYLINE et POLYLINE (renflements), ELLIPSE, SPLINE (NURBS, points de
// contrôle ou points de passage), POINT, INSERT (blocs imbriqués, échelle, rotation, miroir, réseaux de colonnes
// et de rangées). Les entités dessinées dans un repère retourné (extrusion 0 0 -1) sont remises à l'endroit.
// L'unité vient de $INSUNITS (mm si absente) ; tout est rendu en millimètres. Une ellipse, une spline ou un arc
// déformé par une échelle non uniforme devient une suite d'arcs et de lignes qui la suit à 0,005 mm près.
// Textes, cotes, hachures et solides ne sont pas de la géométrie d'esquisse : ils sont comptés et signalés.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "promethee/esquisse.hpp"

namespace prom::dxf {

struct Pt2 {
  double x = 0, y = 0;
};

// Trait lu, en millimètres, dans le repère du dessin (Y vers le haut)
struct TraitDxf {
  enum class Genre { Ligne, Arc, Cercle, Point };
  Genre genre = Genre::Ligne;
  std::string calque;
  Pt2 a, b;             // ligne : extrémités ; arc : début et fin (sens direct) ; point : a
  Pt2 centre;           // arc et cercle
  double rayon = 0;
  bool approche = false;  // morceau d'une ellipse, d'une spline ou d'un arc déformé
};

struct CalqueDxf {
  std::string nom;
  int couleur = 7;        // indice de couleur AutoCAD (ACI)
  bool visible = true;    // ni éteint (couleur négative) ni gelé
  int traits = 0;         // traits lus sur ce calque
};

struct DessinDxf {
  std::vector<TraitDxf> traits;
  std::vector<CalqueDxf> calques;          // dans l'ordre du fichier, puis ceux rencontrés sans définition
  double facteurMm = 1;                    // facteur de l'unité du fichier vers le mm
  std::string unite = "mm";                // unité déclarée ($INSUNITS), en clair
  bool uniteDeclaree = false;
  std::map<std::string, int> ignorees;     // type d'entité → nombre (textes, cotes, hachures…)
  int courbesApprochees = 0;               // ellipses, splines et arcs déformés remplacés par des arcs
  std::vector<std::string> alertes;
  const CalqueDxf* calque(const std::string& nom) const;
};

DessinDxf lireDessinDxf(const std::string& texte);
// Couleur d'un indice ACI (1 rouge, 2 jaune, 3 vert, 4 cyan, 5 bleu, 6 magenta, 7 noir ou blanc, 8 et 9 gris,
// puis la roue de 10 à 249, et les gris de 250 à 255), en « #RRGGBB »
std::string couleurAci(int indice);

enum class PlacementDxf {
  CommeLeFichier,   // l'origine du DXF sur l'origine de l'esquisse
  Centre,           // le centre de l'encombrement sur l'origine
  CoinBasGauche     // le coin bas-gauche de l'encombrement sur l'origine
};

struct OptionsImportDxf {
  double echelle = 1;                      // en plus de l'unité du fichier (2 : deux fois plus grand)
  double facteurMm = 0;                    // > 0 : remplace l'unité du fichier (1 mm, 25,4 pouce…)
  PlacementDxf placement = PlacementDxf::CommeLeFichier;
  double dx = 0, dy = 0;                   // décalage après placement, mm
  std::set<std::string> calques;           // calques repris (vide, sans « calquesChoisis » : tous les calques visibles)
  bool calquesChoisis = false;             // la liste est celle de l'utilisateur, même vide
  bool calquesEsquisse = true;             // chaque calque DXF devient un calque de l'esquisse, à sa couleur
  bool horizontalesVerticales = true;      // relations sur les lignes horizontales et verticales
  bool fixer = false;                      // points fixés et rayons cotés : géométrie entièrement contrainte
  bool construction = false;               // tout en géométrie de construction (gabarit à décalquer)
  double tolerance = 0.001;                // mm : extrémités réunies en un seul point
};

struct RapportImportDxf {
  int lignes = 0, arcs = 0, cercles = 0, points = 0, approches = 0, reunis = 0, relations = 0;
  std::vector<int> elements;               // éléments créés dans l'esquisse
  double xmin = 0, ymin = 0, xmax = 0, ymax = 0;  // encombrement inséré, mm, dans l'esquisse
  std::vector<std::string> alertes;
  int total() const { return lignes + arcs + cercles + points; }
};

// Insère le dessin dans l'esquisse (rien n'est enlevé). Les extrémités confondues à la tolérance près ne font
// qu'un point (les contours restent fermés) ; un point à l'origine rejoint l'origine de l'esquisse.
RapportImportDxf importerDxf(Esquisse& e, const DessinDxf& d, const OptionsImportDxf& o = {});

}  // namespace prom::dxf
