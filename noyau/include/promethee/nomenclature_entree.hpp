// Prométhée : nomenclature en entrée (lot L10 de la 0.34 ; 10.6 ; ECH-13). Une nomenclature CSV ou XLSX venue d'un
// autre outil, d'un acheteur ou d'un assembleur met à jour les composants de la conception : chaque ligne désigne
// ses composants par leurs repères (« R1, R2 », « R1-R4 », « R1 R2 ») et écrit les colonnes reconnues (valeur,
// empreinte connue, fabricant, MPN, code LCSC, description, fiche, non monté) ; les autres colonnes vont dans les
// champs du composant. Le rapport dit ce qui change, composant par composant, avant que la commande l'applique.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// « R1-R3, C5 » → R1 R2 R3 C5 (séparateurs : virgule, point-virgule, espace ; plages d'un même préfixe)
std::vector<std::string> developperReperes(const std::string& texte);

struct ChangementNomenclature {
  std::string reference, champ, avant, apres;
};
struct RapportNomenclatureEntree {
  int lignes = 0, composants = 0;
  std::vector<ChangementNomenclature> changements;
  std::vector<std::string> inconnus;   // repères absents de la conception
  std::vector<std::string> colonnes;   // en-tête → champ retenu, pour l'affichage
  std::vector<std::string> alertes;    // quantité qui ne compte pas les repères, empreinte inconnue…
};
// La première ligne porte les en-têtes (français ou anglais, sans casse ni accents). « appliquer » faux : rien n'est
// écrit, le rapport dit ce qui changerait.
RapportNomenclatureEntree appliquerNomenclature(ConceptionElec& c, const std::vector<std::vector<std::string>>& tableau, bool appliquer = true);

}  // namespace prom::ecad
