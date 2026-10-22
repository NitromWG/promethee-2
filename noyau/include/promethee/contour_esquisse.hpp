// Prométhée : contour de la carte piloté par une esquisse (lot L6, tâche 6.13 ; MEC-29). L'esquisse, gardée dans la
// conception, porte la boucle extérieure de la carte, ses découpes et un cercle par trou de fixation, cotés depuis les
// bords : une cote changée (80 → 90 mm) redimensionne la carte, et les trous de fixation suivent leur bord.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/esquisse.hpp"

namespace prom {

// Esquisse de départ tirée de la carte : rectangle (coin haut gauche à l'origine, longueur et largeur cotées), un
// cercle par trou de fixation, au diamètre de son perçage, coté depuis le coin le plus proche
Esquisse esquisseDeCarte(const ecad::ConceptionElec& c);
// Résout l'esquisse de la conception et l'applique : boucle extérieure → contour, autres boucles → découpes, cercles →
// trous de fixation (chaque trou rejoint le cercle le plus proche) ; rend les remarques
std::vector<std::string> appliquerEsquisseContour(ecad::ConceptionElec& c);
struct CoteContour {
  int contrainte = 0;
  std::string nom;  // « Longueur 80 mm », « Écart X du trou H1 »
  double valeur = 0;
};
std::vector<CoteContour> cotesContour(const ecad::ConceptionElec& c);
bool changerCoteContour(ecad::ConceptionElec& c, int contrainte, double valeur, std::vector<std::string>* remarques = nullptr);

}  // namespace prom
