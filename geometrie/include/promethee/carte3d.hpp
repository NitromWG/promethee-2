// Prométhée : vue 3D d'une carte électronique conçue dans Prométhée — plaque à la couleur de son vernis, cuivre des
// deux faces (pastilles étamées, pistes et zones sous le vernis), corps des composants à leur hauteur réelle.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <TopoDS_Shape.hxx>

#include "promethee/electronique.hpp"

namespace prom {

struct Carte3D {
  TopoDS_Shape forme;
  std::vector<std::pair<TopoDS_Shape, std::string>> couleurs;  // sous-formes et couleur « #RRGGBB »
};
Carte3D modele3DCarte(const ecad::ConceptionElec& c);

}  // namespace prom
