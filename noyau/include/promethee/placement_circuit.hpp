// Prométhée : placement au circuit (lot L5, tâches 5.2, 5.3, 5.18 ; PLA-01, PLA-02, PLA-05, PLA-21, C1, C8).
//
// « À placer » : les empreintes que la synchronisation a rangées hors de la carte, groupées par feuille du schéma ;
// un groupe se pose sur la carte en rangées serrées à partir d'un coin, sans recouvrement d'encombrement. Disposition
// en cercle ou en arc : chaque composant à sa place calculée sur le cercle, tourné vers le centre au besoin, et les
// repères renumérotés dans l'ordre du cercle (D1 à D24), en un seul changement.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct GroupeAPlacer {
  std::string nom;                       // nom de la feuille
  std::vector<std::string> composants;   // UUID, dans l'ordre des repères
};
std::vector<GroupeAPlacer> groupesAPlacer(const ConceptionElec& c);
// Pose les composants en rangées à partir du coin (haut gauche), à l'intérieur de la carte ; rend le nombre posé
int placerGroupe(ConceptionElec& c, const std::vector<std::string>& composants, Pt coin, Nm ecart = NM_PAR_MM);

struct OptionsCercle {
  Pt centre;
  Nm rayon = 20 * NM_PAR_MM;
  double angleDepart = -90;   // degrés à l'écran (y vers le bas) : −90 en haut
  double balayage = 360;      // un arc s'il est plus petit
  bool versCentre = true;     // le haut de chaque empreinte tourné vers le centre
  double rotationPropre = 0;  // ajoutée à chacune
  bool renumeroter = true;
  std::string prefixe;        // vide : celui du premier composant
};
Pt placeSurCercle(const OptionsCercle& o, std::size_t i, std::size_t n);
double rotationSurCercle(const OptionsCercle& o, std::size_t i, std::size_t n);
// Rend le nombre de composants disposés ; l'ordre est celui de la liste (la sélection)
int disposerEnCercle(ConceptionElec& c, const std::vector<std::string>& composants, const OptionsCercle& o);

}  // namespace prom::ecad
