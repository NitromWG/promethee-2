// Prométhée : imports côté schéma (lot L10 de la 0.34 ; 10.14 ; ECH-03, ECH-19, ECH-29). Schémas KiCad 5 (.sch),
// symboles tirés du cache du projet (« <projet>-cache.lib ») ou des bibliothèques données, feuilles hiérarchiques ;
// schémas LTspice (.asc), symboles lus dans les .asy fournis, sinon les symboles de base d'LTspice (résistance,
// condensateur, bobine, diode, sources, transistors), directives gardées dans un texte SPICE pour la simulation ;
// synoptiques Visio (.vsdx, XML ouvert lu directement), formes changées en blocs et connecteurs en liaisons d'une
// feuille de schéma fonctionnel. Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct RapportImportSchema {
  int feuilles = 0, composants = 0, fils = 0, etiquettes = 0, blocs = 0, liaisons = 0;
  std::vector<std::string> directives;  // LTspice : directives SPICE gardées
  std::vector<std::string> alertes;
};
using LecteurFichier = std::function<std::optional<std::string>(const std::string&)>;

// « lire » rend le contenu d'un fichier voisin (sous-feuilles, « <projet>-cache.lib », bibliothèques .lib)
ConceptionElec conceptionDepuisKicad5(const std::string& racine, const std::string& nom, const LecteurFichier& lire, RapportImportSchema* rapport = nullptr);
// « lire » rend le contenu d'un symbole .asy voisin (« res.asy »), s'il est fourni
ConceptionElec conceptionDepuisLtspice(const std::string& asc, const std::string& nom, const LecteurFichier& lire, RapportImportSchema* rapport = nullptr);
// Fichier .vsdx (octets de l'archive) : une feuille de schéma fonctionnel par page
ConceptionElec conceptionDepuisVisio(const std::string& vsdx, const std::string& nom, RapportImportSchema* rapport = nullptr);
bool estSchemaKicad5(const std::string& texte);
bool estSchemaLtspice(const std::string& texte);

}  // namespace prom::ecad
