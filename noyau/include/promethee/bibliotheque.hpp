// Prométhée : bibliothèque personnelle de symboles électroniques (fichier de l'utilisateur). Licence GPL-3.0-only.
//
// Le fichier ne se perd jamais en silence (EL-08) : une lecture qui échoue le dit, récupère les composants
// encore entiers d'un fichier coupé, et l'appelant met le fichier abîmé de côté avant d'en réécrire un ;
// l'écriture passe par un fichier temporaire et un renommage, et garde la version précédente en « .bak ».
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "promethee/circuit.hpp"
#include "promethee/schema.hpp"

namespace prom::ecad {

struct LectureBibliotheque {
  std::vector<DefSymbole> symboles;
  std::vector<DefEmpreinte> empreintes;  // empreintes créées ou importées par l'utilisateur
  bool abimee = false;   // le fichier n'a pas pu être lu en entier
  std::string erreur;    // cause, pour le message
};
// Lecture d'un contenu de bibliothèque ; un JSON coupé ou abîmé rend les composants encore entiers.
LectureBibliotheque lireBibliotheque(const std::string& contenu);
std::string texteBibliotheque(const std::vector<DefSymbole>& symboles, const std::vector<DefEmpreinte>& empreintes = {});

// Fichier : absent → bibliothèque vide, non abîmée. Lève prom::ErreurFichier si le fichier existe mais ne se lit pas.
LectureBibliotheque lireFichierBibliotheque(const std::filesystem::path& chemin);
// Met de côté un fichier abîmé sous « <nom>.abime-<n>.json » (jamais écrasé) ; rend le nouveau chemin.
std::filesystem::path mettreDeCoteBibliotheque(const std::filesystem::path& chemin);
// Écriture atomique ; la version précédente, si elle se lisait, est gardée en « <nom>.bak ».
void ecrireFichierBibliotheque(const std::filesystem::path& chemin, const std::vector<DefSymbole>& symboles, const std::vector<DefEmpreinte>& empreintes = {});

}  // namespace prom::ecad
