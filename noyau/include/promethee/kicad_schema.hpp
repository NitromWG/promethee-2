// Prométhée : schémas KiCad (.kicad_sch, KiCad 6 à 9) en entrée et en sortie (lot L10 de la 0.34 ; 10.3 ; X6,
// ECH-03). À l'import : symboles de bibliothèque embarqués (lib_symbols) changés en définitions (broches, unités,
// symboles dérivés, dessin), symboles posés (unités, miroir, rotation, champs, repères par instance), alimentations,
// fils, jonctions, marques de non-connexion, étiquettes locales, globales et hiérarchiques, textes, et feuilles
// hiérarchiques changées en blocs de feuille (7.2) dont les ports rejoignent les étiquettes hiérarchiques de la
// feuille fille. À l'export : la feuille racine et un fichier par feuille fille, avec leurs symboles de bibliothèque.
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct RapportImportSchemaKicad {
  int feuilles = 0, symboles = 0, composants = 0, fils = 0, etiquettes = 0, blocs = 0;
  std::vector<std::string> alertes;  // ce qui n'a pas pu être repris tel quel
};
// « racine » : contenu du fichier racine ; « lire » rend le contenu d'une sous-feuille d'après son nom de fichier
// (relatif au dossier du projet), ou rien si elle manque
ConceptionElec conceptionDepuisSchemaKicad(const std::string& racine, const std::string& nom, const std::function<std::optional<std::string>(const std::string&)>& lire,
                                           RapportImportSchemaKicad* rapport = nullptr);
// Fichiers du schéma KiCad : « <nom>.kicad_sch » pour la racine, puis un fichier par feuille fille
std::map<std::string, std::string> schemaKicadDepuisConception(const ConceptionElec& c, const std::string& nomProjet);

}  // namespace prom::ecad
