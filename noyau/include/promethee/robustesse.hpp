// Prométhée : lecture tolérante bornée (lot L9, tâche 9.7 ; A14, ERG-11). Un fichier abîmé ou écrit par un outil
// fautif s'ouvre quand même, avec un rapport : identifiants en double renouvelés, composant dédoublé retrouvant son
// symbole et son empreinte, empreinte posée deux fois pour un même composant retirée.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Répare les doublons de la conception ; rend une phrase par réparation (vide : rien à reprendre, rien changé)
std::vector<std::string> reparerDoublons(ConceptionElec& c);

}  // namespace prom::ecad
