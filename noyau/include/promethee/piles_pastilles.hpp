// Prométhée : piles de pastilles et de vias (4.24, 8.22, 8.23). Gabarits nommés selon l'IPC-7351, qu'une pastille
// cite et suit ; via choisie par le tracé d'après ses couches ; contre-perçage (profondeur, talon, couches coupées).
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Gabarit tiré d'une pastille : sa forme, ses cotes, son perçage, son raccord ; nommé selon l'IPC-7351
GabaritPastille gabaritDepuisPastille(const Pastille& p, Nm margeVernis = 50'000);
// Bibliothèque intégrée de gabarits courants (passifs 0402 à 1206, SOIC, QFP, traversants de 0,8 à 1,3 mm)
const std::vector<GabaritPastille>& gabaritsIntegres();
// Relie chaque pastille d'une empreinte au gabarit de mêmes cotes (créé dans « gabarits » s'il n'existe pas) ; rend le
// nombre de pastilles reliées
int lierAuxGabarits(DefEmpreinte& d, std::map<std::string, GabaritPastille>& gabarits);
// Un gabarit changé (4.24) : chaque pastille des empreintes du projet qui le cite le suit, en une commande ; le nom du
// gabarit (« ancien ») peut changer avec ses cotes. Rend le nombre de pastilles changées.
int appliquerGabaritPartout(ConceptionElec& c, const std::string& ancien, const GabaritPastille& g);

// Contre-perçage d'une via (8.23) : profondeur depuis la face percée, talon laissé sous la dernière couche gardée,
// couches coupées ; « possible » faux si le talon dépasse son maximum ou si une couche à couper le serait à moitié
struct BilanContrePercage {
  double profondeur = 0, talon = 0;  // mm
  std::vector<Couche> coupees;
  bool possible = true;
  std::string raison;
};
BilanContrePercage bilanContrePercage(const Via& v, const Empilage& e);
// Pistes reliées à une couche que le contre-perçage d'une via coupe (8.23) : leur UUID
std::vector<std::string> pistesSurCouchesCoupees(const ConceptionElec& c, const Via& v);

}  // namespace prom::ecad
