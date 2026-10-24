// Prométhée : archive zip (3MF, classeurs XLSX), flux zlib, fichiers composés CFB. Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace prom {

// Archive zip « stockée » (sans compression), noms en UTF-8 (drapeau 11) : lue par tous les outils d'archive,
// les tableurs (XLSX) et les logiciels d'impression 3D (3MF)
std::string zipStocke(const std::vector<std::pair<std::string, std::string>>& fichiers);
uint32_t crc32(const std::string& octets);
// Lecture (4.17) : un flux DEFLATE brut, et les fichiers d'une archive zip (stockés ou compressés par DEFLATE), dans
// l'ordre du répertoire central ; lève std::runtime_error sur une archive abîmée
std::string inflerBrut(const std::string& donnees);
std::vector<std::pair<std::string, std::string>> lireZip(const std::string& octets);
// Flux zlib (en-tête de deux octets, DEFLATE, somme Adler-32 non vérifiée)
std::string inflerZlib(const std::string& donnees);

// Fichier composé de Microsoft (CFB, « OLE2 » : documents Altium, anciens fichiers d'Office ; 10.15) : les flux, par
// chemin (« Pads6/Data »), dans l'ordre du répertoire ; lève std::runtime_error sur un fichier abîmé. L'écrivain
// (secteurs de 512 octets, petits flux dans le mini-flux) sert aux essais et aux échanges
bool estCfb(const std::string& octets);
std::vector<std::pair<std::string, std::string>> lireCfb(const std::string& octets);
std::string ecrireCfb(const std::vector<std::pair<std::string, std::string>>& flux);

}  // namespace prom
