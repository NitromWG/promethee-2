// Prométhée : modèles 3D exacts des composants (lot L6, tâches 6.2 et 6.10 ; MEC-03, EMP-10, EMP-29). Ce qui ne
// dépend pas d'OpenCascade : calage d'un modèle sur son empreinte (quart de tour qui recouvre le mieux l'encombrement,
// boîte centrée, posée sur la carte), rapprochement d'un dossier de fichiers avec les empreintes, les articles et les
// références, association rangée une seule fois par fichier.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Quart de tour du calage : celui qui fait le mieux coïncider la boîte du modèle et l'encombrement de l'empreinte
Modele3D calerModele(const DefEmpreinte& d, Modele3D m);
// Clé de rapprochement d'un nom : minuscules, sans extension ni suffixe « _3d », séparateurs ôtés
std::string cleModele(const std::string& nom);
// Composant → fichier : l'identifiant d'empreinte, celui de l'article, la référence fabricant, la valeur
std::map<std::string, std::string> rapprocherModeles(const ConceptionElec& c, const std::vector<std::string>& fichiers);
// Range le modèle dans la conception (un même fichier une seule fois), le cale sur l'empreinte du composant et le lui
// donne ; rend l'identifiant du modèle
std::string associerModele(ConceptionElec& c, const std::string& composant, Modele3D m);

}  // namespace prom::ecad
