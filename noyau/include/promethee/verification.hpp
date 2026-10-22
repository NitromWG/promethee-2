// Prométhée : exclusions qui reviennent « à revoir » et rapport de vérification (lot L5, tâches 5.13 et 5.29 ;
// DRC-09, DRC-10, DRC-18, R8, R9).
//
// Une exclusion de DRC garde la signature géométrique de ses objets (pistes, vias, empreintes, zones) : tant que rien
// ne bouge elle tient, et si l'un d'eux bouge la violation revient, marquée « à revoir ». Le rapport HTML est
// autonome : groupé par règle, exclusions à part, chaque ligne porte un lien « promethee:violation/<n> » que
// Prométhée suit jusqu'à l'objet.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

std::string signatureObjets(const ConceptionElec& c, const std::vector<std::string>& objets);
// Exclusion d'une violation, une par une, avec sa raison et la signature de ses objets
ExclusionErc exclusionPour(const ConceptionElec& c, const Violation& v, const std::string& raison, const std::string& auteur = {}, const std::string& date = {});

struct BilanVerification {
  std::vector<Violation> actives;                              // à corriger (les « à revoir » comprises)
  std::vector<std::pair<Violation, ExclusionErc>> exclues;     // exclues, objets inchangés
};
BilanVerification bilanDRC(const ConceptionElec& c, const Connectivite& k, const Remplissages& r);
// Rapport HTML autonome : ERC puis DRC, groupés par règle, exclusions à part ; « liens » : l'indice de chaque
// violation dans la liste rendue (actives de l'ERC, puis de la DRC), cible des liens promethee:violation/<n>
std::string rapportVerificationHtml(const ConceptionElec& c, const std::vector<Violation>& erc, const BilanVerification& drc);
std::string rapportVerificationTexte(const ConceptionElec& c, const std::vector<Violation>& erc, const BilanVerification& drc);

}  // namespace prom::ecad
