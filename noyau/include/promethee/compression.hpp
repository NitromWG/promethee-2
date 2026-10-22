// Prométhée : compression DEFLATE (RFC 1951) sans bibliothèque extérieure, et archive zip compressée. Recherche des
// répétitions par chaînes de hachage, codes de Huffman fixes : un projet JSON tombe à environ un cinquième de sa
// taille, relu par inflerBrut (archive.hpp) et par tous les outils d'archive. Sert à l'historique local (12.9) et à
// l'archive du projet (12.4). Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace prom {

std::string deflerBrut(const std::string& donnees);  // flux DEFLATE brut, sans en-tête zlib
// Archive zip dont chaque fichier est compressé (méthode 8) quand il y gagne, stocké sinon ; noms en UTF-8
std::string zipComprime(const std::vector<std::pair<std::string, std::string>>& fichiers);

}  // namespace prom
