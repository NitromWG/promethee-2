// Prométhée : police vectorielle à trait simple, commune à l'écran, aux Gerber, au PDF et à la 3D (EL-06).
// Capitales, chiffres et ponctuation courante, dessinés sur une grille de 4 × 6 ; les minuscules s'écrivent en
// petites capitales et les lettres accentuées sans leur accent (lisible en sérigraphie à 0,8 mm). Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "promethee/ecad.hpp"

namespace prom::ecad {

enum class AncrageTexte { Gauche, Centre, Droite };
struct TraitTexte {
  Pt a, b;
};
// Traits d'un texte : « position » est le point d'ancrage sur la ligne de base (au milieu de la hauteur si
// « centreVertical »), « hauteur » celle des capitales, rotation en degrés (sens direct, axe Y vers le bas comme
// la carte), « miroir » pour une face dessous lue par transparence.
std::vector<TraitTexte> traitsTexte(const std::string& texte, Pt position, Nm hauteur, double rotation = 0, bool miroir = false,
                                    AncrageTexte ancrage = AncrageTexte::Centre, bool centreVertical = true);
Nm largeurTexte(const std::string& texte, Nm hauteur);
// Épaisseur de trait conseillée pour une hauteur : 15 % de la hauteur, au moins 0,12 mm
Nm epaisseurTexte(Nm hauteur);

}  // namespace prom::ecad
