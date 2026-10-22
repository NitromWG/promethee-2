// Prométhée : placement automatique (lot L13, 13.12 ; PLA-13, PLA-35). Première passe par groupes d'affinité (les
// composants les plus liés côte à côte, en partant du plus relié), puis recuit simulé sur la longueur du chevelu
// (demi-périmètre de chaque réseau) et des pénalités de recouvrement et de sortie de carte ; une dernière passe
// range sans recouvrement ce qui en garde. Interruptible, calculé sur le poste ; les empreintes verrouillées ne
// bougent pas. Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <set>
#include <string>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct OptionsPlacementAuto {
  int iterations = 60'000;
  unsigned graine = 1;
  Nm ecart = 300'000;     // entre deux encombrements
  Nm marge = 1'000'000;   // au bord de la carte
  Nm pas = 250'000;       // grille des positions
  bool tourner = true;    // quarts de tour permis
  std::set<std::string> fixes;  // composants qui ne bougent pas (en plus des verrouillés)
  const std::atomic<bool>* arret = nullptr;
};
struct ResultatPlacementAuto {
  double chevelu = 0;   // mm, arbre couvrant des pastilles de chaque réseau, après placement
  int collisions = 0;   // encombrements qui se touchent ou sortent de la carte, après placement
  int deplaces = 0;
  bool interrompu = false;
};
ResultatPlacementAuto placerAutomatiquement(ConceptionElec& c, const OptionsPlacementAuto& o = {});

// Longueur du chevelu (mm) : arbre couvrant minimal des pastilles de chaque réseau, cuivre ignoré
double longueurChevelu(const ConceptionElec& c);
// Encombrements qui se touchent (écart compris) ou sortent du contour de la carte
int collisionsPlacement(const ConceptionElec& c, Nm ecart = 0);

}  // namespace prom::ecad
