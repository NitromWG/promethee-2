// Prométhée : migrations du fichier de projet (lot L1, tâche 1.22, ECH-10, ECH-11). Chaque étape dit ce qui change
// entre le fichier d'une version de Prométhée et celui de la suivante, dans les deux sens : la montée, faite en
// mémoire à la lecture d'un fichier ancien (le fichier n'est réécrit qu'à l'enregistrement), et la descente, qui
// écrit le fichier pour la version d'avant en disant, une ligne chacune, ce qui s'y perd (« Enregistrer pour une
// version précédente… »). Les étapes s'enchaînent : un projet de la 0.30 descend à la 0.29, puis à la 0.28. Un essai
// refuse toute étape qui n'a pas sa descente. Les schémas JSON de chaque version sont dans docs/schemas.
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "promethee/modele.hpp"

namespace prom {

struct EtapeMigration {
  std::string depuis, vers;  // versions de Prométhée : le fichier de « depuis » devient celui de « vers »
  std::string objet;         // « projet » (premier niveau du fichier) ou « conception » (une carte électronique)
  std::string resume;        // ce que l'étape change dans le fichier
  // Montée, à la lecture d'un fichier de « depuis » ; « notes » reçoit ce qu'elle a changé
  std::function<void(Json& o, std::vector<std::string>& notes)> monter;
  // Descente, pour écrire le fichier de « depuis » ; « pertes » reçoit ce qui ne s'y garde pas
  std::function<void(Json& o, std::vector<std::string>& pertes)> descendre;
};

// Les étapes, de la plus ancienne à la plus récente
const std::vector<EtapeMigration>& etapesMigration();
// Version du fichier écrit par ce logiciel (« 0.30 ») et celles pour lesquelles il sait écrire, la plus récente
// d'abord (« 0.29 », « 0.28 »)
std::string versionFichierCourante();
std::vector<std::string> versionsAnterieures();
// Ordre de deux versions « 0.29 » < « 0.30 » : négatif, nul ou positif
int comparerVersions(const std::string& a, const std::string& b);

// Version qui a écrit une conception : « 0.28 » sans clé de format, « 0.29 » avec « formatElectronique » seule,
// la courante avec « format » (ou une plus récente : rien n'y monte, la lecture tolérante garde ce qu'elle ignore)
std::string versionConception(const Json& o);
// Monte une conception au format courant, étape par étape
void monterConception(Json& o, std::vector<std::string>* notes = nullptr);

// Projet écrit pour une version antérieure : le JSON du fichier, et ce qui s'y perd (« Carte « X » : … » pour
// une carte électronique)
struct FichierAnterieur {
  std::string version;
  Json projet;
  std::vector<std::string> pertes;
};
// Lève std::invalid_argument pour une version qui n'est pas dans versionsAnterieures()
FichierAnterieur pourVersion(const Projet& p, const std::string& version);
void ecrirePourVersion(const FichierAnterieur& f, const std::string& chemin);  // écriture atomique
// « carte.prom » → « carte.pour-0-29.prom »
std::string nomPourVersion(const std::string& chemin, const std::string& version);

}  // namespace prom
