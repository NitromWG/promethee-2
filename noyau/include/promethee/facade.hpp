// Prométhée : façade calée sur la carte (lot L6, tâches 6.14 et 6.15 ; MEC-24 à MEC-27). Découpes, légendes et
// échelles graduées posées sur une face du boîtier, chacune liée à son composant : déplacer le potentiomètre de
// 10 mm déplace sa découpe, sa légende et son échelle de 10 mm. Contrôle avant la sortie (écart entre découpes, marge
// au bord), puis SVG et DXF, à l'échelle 1, en millimètres.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Centre d'un élément sur la façade (mm, repère de la carte, y vers le bas)
struct PositionFacade {
  double x = 0, y = 0;
};
PositionFacade positionElement(const ConceptionElec& c, const ElementFacade& e);
// Éléments par défaut pour un composant : découpe ronde à son diamètre, légende au-dessus, et pour un potentiomètre
// une échelle graduée autour
std::vector<ElementFacade> elementsPour(const ConceptionElec& c, const std::string& composant, bool echelle);

struct DefautFacade {
  std::string message;
  std::vector<std::string> elements;  // UUID des éléments en cause
  double x = 0, y = 0;                // lieu du défaut
};
// Contrôle avant la sortie : deux découpes plus proches que l'écart minimal, une découpe trop près du bord de la face
// ou hors d'elle, un élément lié à un composant absent ; vide si la façade sort telle quelle
std::vector<DefautFacade> controlerFacade(const ConceptionElec& c, const Facade& f);
std::string svgFacade(const ConceptionElec& c, const Facade& f);
std::string dxfFacade(const ConceptionElec& c, const Facade& f);  // calques DECOUPES, GRAVURE (légendes, échelles), CONTOUR

}  // namespace prom::ecad
