// Prométhée : dépôt des géométries importées (EX-06). Le texte BRep d'un import (plusieurs mégaoctets pour un
// STEP) n'est gardé qu'une fois en mémoire, adressé par son SHA-256 : la pièce, le projet et chaque état
// d'annulation ne portent que la référence « @sha256:<hex> ». Le fichier enregistré range les textes à part,
// sous « geometries ». Le dépôt ne fait que grandir pendant la session : une référence reste toujours valable.
// Licence GPL-3.0-only.
#pragma once

#include <memory>
#include <string>

namespace prom {

inline constexpr std::size_t TAILLE_DEPOT_MIN = 1024;  // en dessous, le texte reste dans la pièce
bool estReferenceGeometrie(const std::string& s);
// Range le texte et rend sa référence ; un texte court est rendu tel quel
std::string deposerGeometrie(const std::string& texte);
// Texte d'une référence (nullptr si inconnue) ; un texte qui n'est pas une référence est rendu lui-même
std::shared_ptr<const std::string> geometrieDeposee(const std::string& referenceOuTexte);

}  // namespace prom
