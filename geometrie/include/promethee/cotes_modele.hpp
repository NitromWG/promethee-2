// Prométhée : cotes du modèle reprises en mise en plan (D3). Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/construction.hpp"
#include "promethee/dessin.hpp"
#include "promethee/piece.hpp"

namespace prom {

// Cotes des esquisses employées par les fonctions actives de la pièce, en 3D d'après le repère de chaque esquisse
// (longueurs, distances, écarts horizontaux et verticaux, rayons, diamètres, angles). Les cotes d'une esquisse en
// échec, ou dont un élément a disparu, sont rendues dans « orphelines » au lieu d'être cotées.
std::vector<OptionsMiseEnPlan::CoteModele> cotesDuModele(const Piece& p, const ResultatPiece& r, std::vector<std::string>* orphelines = nullptr);

}  // namespace prom
