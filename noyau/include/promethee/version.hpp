// Prométhée : version et identifiant de build. Licence GPL-3.0-only.
// Les valeurs viennent de cmake/version.cmake, relancé à chaque compilation : chaque binaire porte la
// révision exacte de ses sources (commit git), affichée dans « À propos », le journal, les publications
// de fabrication et le paquet Windows.
#pragma once

#include <string>

namespace prom {

struct InfosVersion {
  const char* version;       // « 0.25.0 »
  const char* revision;      // commit (12 caractères), suffixé « -modifiee » si les sources différaient du commit
  const char* dateRevision;  // date ISO 8601 du commit, vide si inconnue
  const char* identifiant;   // « 0.25.0+a1b2c3d4e5f6 » (+ « .numéro » en intégration continue)
  int numeroBuild;           // numéro de l'intégration continue, 0 en compilation locale
};

const InfosVersion& infosVersion();
// « 0.25 » : la version telle qu'on la nomme dans l'interface.
std::string versionCourte();
// « Prométhée 0.25.0 (révision a1b2c3d4e5f6, 2026-10-09) » : une ligne pour les journaux et les fiches.
std::string ligneVersion();

}  // namespace prom
