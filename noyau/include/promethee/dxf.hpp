// Prométhée : lecture d'un contour de carte dans un fichier DXF (EL-44). Lignes, arcs, cercles et polylignes
// (LWPOLYLINE, POLYLINE R12, avec renflements) sont enchaînés en boucles fermées, à 0,01 mm près ; la plus grande
// boucle est le contour, celles qu'elle contient sont des découpes. L'axe Y du DXF monte, celui de la carte
// descend : il est retourné. L'unité vient de $INSUNITS (mm par défaut).
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/ecad.hpp"

namespace prom::ecad {

struct ContourDxf {
  Polygone exterieur;            // vide si aucune boucle fermée
  Polygones decoupes;            // boucles contenues dans le contour
  std::vector<std::string> alertes;  // entités ignorées, boucles ouvertes, boucles hors du contour
};
ContourDxf lireContourDxf(const std::string& texte);

}  // namespace prom::ecad
