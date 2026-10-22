// Prométhée : ajout d'éléments à une place libre de la carte. Licence GPL-3.0-only.
#pragma once

#include <string>

#include "promethee/modele.hpp"

namespace prom {

// Ajoute un composant du type donné à la première place sans conflit (recherche en spirale,
// ou le long des bords pour un connecteur de bord) et renvoie son identifiant.
std::string ajouterComposant(Projet& p, const std::string& type);
// Ajoute un trou de fixation dans le premier coin libre et renvoie son identifiant.
std::string ajouterTrou(Projet& p);

}  // namespace prom
