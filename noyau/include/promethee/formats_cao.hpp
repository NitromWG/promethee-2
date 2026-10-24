// Prométhée : échanges avec les autres outils de CAO électronique (lot L10 de la 0.34 ; 10.17, 10.18, 10.22 ;
// ECH-21, ECH-22, ECH-30). Écrivains : carte et schéma en PADS ASCII (Layout et Logic), carte en P-CAD ASCII
// (ACCEL_ASCII), schéma en EDIF 2 0 0 (netlists_tierces.hpp), et pour Altium une archive aux formats KiCad et PADS
// que son assistant d'import lit. Chaque écrivain inscrit à son rapport ce qui ne passe pas (zones de cuivre, règles,
// empilage…). Lecteurs : PADS ASCII (Logic et Layout) et P-CAD ASCII, carte et réseaux reliés. Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct RapportEcritureCao {
  std::vector<std::string> nonEcrits;  // ce que le format ne porte pas, objet par objet ou par genre
  int composants = 0, reseaux = 0, pistes = 0, vias = 0;
};
std::string padsAsciiCarte(const ConceptionElec& c, RapportEcritureCao* rapport = nullptr);
std::string padsAsciiSchema(const ConceptionElec& c, RapportEcritureCao* rapport = nullptr);  // PADS Logic : pièces et signaux
std::string pcadAsciiCarte(const ConceptionElec& c, RapportEcritureCao* rapport = nullptr);
// Fichiers de l'archive pour Altium (nom → contenu) : carte et projet KiCad, carte PADS, netlist, et un mode d'emploi
std::vector<std::pair<std::string, std::string>> archivePourAltium(const ConceptionElec& c, RapportEcritureCao* rapport = nullptr);

struct RapportLectureCao {
  int composants = 0, reseaux = 0, pistes = 0, vias = 0;
  std::vector<std::string> alertes;
};
// PADS : la carte (Layout) porte placement, cuivre et réseaux ; le schéma (Logic), s'il est donné, ses valeurs
ConceptionElec conceptionDepuisPads(const std::optional<std::string>& logic, const std::optional<std::string>& layout, const std::string& nom, RapportLectureCao* rapport = nullptr);
ConceptionElec conceptionDepuisPcad(const std::string& texte, const std::string& nom, RapportLectureCao* rapport = nullptr);
bool estPadsAscii(const std::string& texte);
bool estPcadAscii(const std::string& texte);

}  // namespace prom::ecad
