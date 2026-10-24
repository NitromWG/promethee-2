// Prométhée : contrôles de la DRC ajoutés par la 0.34 (zones de règles, contraintes, quinze contrôles nouveaux,
// paires et accords, proximité, contre-perçage, familles de 8.25). Appelés à la fin de verifierDRC. Licence
// GPL-3.0-only.
#pragma once

#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

void verifierDRCAvancee(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis, const std::vector<PastillePlacee>& pp, std::vector<Violation>& v);

}  // namespace prom::ecad
