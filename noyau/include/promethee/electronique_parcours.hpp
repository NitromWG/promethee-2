// Prométhée : parcours « Apprendre » de l'atelier électronique (lot L2, tâche 2.17 ; AUT-11). La première carte,
// de la pose à la publication : chaque étape dit ce qu'on y apprend et pourquoi, où cliquer, renvoie à un mot du
// lexique, et se vérifie toute seule sur la conception, l'ERC et la DRC servant de correcteur. Aucune dépendance à
// l'interface.
// Licence GPL-3.0-only.
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct EtapeApprendre {
  std::string titre;     // « Pose une LED »
  std::string pourquoi;  // ce qu'on apprend, en mots simples
  std::string comment;   // où cliquer, quoi régler
  std::string mot;       // mot du lexique de l'aide (vide : aucun)
  int mode = 0;          // onglet de l'atelier : 0 schéma, 1 circuit, 3 fabrication
  std::function<bool(const ConceptionElec&)> faite;
};
struct ParcoursApprendre {
  std::string id, nom, description;
  std::vector<EtapeApprendre> etapes;
};
const std::vector<ParcoursApprendre>& parcoursElectroniques();
const ParcoursApprendre* parcoursElectronique(const std::string& id);
// La première étape qui n'est pas encore faite ; etapes.size() quand toutes le sont
std::size_t etapeEnCours(const ParcoursApprendre& p, const ConceptionElec& c);

}  // namespace prom::ecad
