// Prométhée : nomenclature unique (électronique, pièces fabriquées, visserie). Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/derive.hpp"
#include "promethee/modele.hpp"

namespace prom {

struct LigneNomenclature {
  std::string groupe, ref, designation, appro;
  int qte = 1;
};

std::vector<LigneNomenclature> nomenclature(const Projet& p, const Derive& d);
// CSV au séparateur point-virgule, en UTF-8 avec marque d'ordre : s'ouvre tel quel dans Excel en français.
std::string versCsv(const std::vector<LigneNomenclature>& lignes);
// Champ CSV protégé (guillemets doublés) s'il contient ; " retour à la ligne ou retour chariot (EX-10, EX-14)
std::string champCsv(const std::string& s);
// Tri naturel des repères (D2 avant D10), sans conversion numérique : aucune suite de chiffres ne déborde (EX-14)
bool avantNaturelRepere(const std::string& a, const std::string& b);

}  // namespace prom
