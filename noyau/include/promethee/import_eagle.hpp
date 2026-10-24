// Prométhée : import EAGLE (lot L10 de la 0.34 ; 10.5, 10.18 ; ECH-05, ECH-23). Fichiers XML d'EAGLE 6 et suivants
// (et de Fusion Electronics) : schéma (.sch) — bibliothèques embarquées changées en symboles (une unité par porte,
// broches numérotées par les pastilles de la variante choisie), parties, instances, fils, étiquettes, jonctions,
// alimentations (portes à broche « sup »), réseaux réunis par nom d'une feuille à l'autre comme dans EAGLE ; carte
// (.brd) — contour (Dimension), empreintes des bibliothèques embarquées, éléments posés (face et rotation), pistes,
// vias, polygones de cuivre, trous ; bibliothèque (.lbr) — symboles, empreintes et articles, avec un rapport par
// objet. Un projet (schéma et carte de même nom) arrive relié : chaque élément de la carte est la partie du schéma
// qui porte son nom. Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/gestion_bibliotheque.hpp"

namespace prom::ecad {

struct RapportImportEagle {
  int feuilles = 0, parties = 0, reseaux = 0, elements = 0, pistes = 0, vias = 0, polygones = 0;
  std::vector<std::string> alertes;
};
// Schéma seul, carte seule, ou les deux reliés (le schéma donne les composants et leurs réseaux)
ConceptionElec conceptionDepuisEagle(const std::optional<std::string>& schema, const std::optional<std::string>& carte, const std::string& nom, RapportImportEagle* rapport = nullptr);

// Bibliothèque EAGLE (.lbr) en bibliothèque de Prométhée : un article par variante d'un ensemble (deviceset ×
// device), son symbole et son empreinte ; le rapport dit, objet par objet, ce qui a été fait (10.4)
RapportBibliotheque importerBibliothequeEagle(const std::string& lbr, ContenuBibliotheque& destination, bool remplacer = false);

}  // namespace prom::ecad
