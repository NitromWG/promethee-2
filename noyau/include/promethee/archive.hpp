// Prométhée : archive zip sans compression (3MF, classeurs XLSX). Licence GPL-3.0-only.
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

}  // namespace prom
