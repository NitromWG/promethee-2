// Prométhée : import électrique d'une carte KiCad (.kicad_pcb), EL-36. Contour et découpes (Edge.Cuts), empreintes
// avec leurs pastilles, leur sérigraphie et leur encombrement (figées dans le projet), pistes (arcs compris),
// vias, zones de cuivre, textes de sérigraphie et réseaux. Le fichier de carte ne contient pas le schéma : un
// schéma de liaison est créé (un symbole par composant, broches reliées par des étiquettes globales au nom du
// réseau KiCad), pour que connectivité, ERC, DRC et nomenclature fonctionnent comme sur une carte dessinée ici.
// Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct RapportImportKicad {
  int empreintes = 0, pastilles = 0, pistes = 0, vias = 0, zones = 0, textes = 0, reseaux = 0, couchesCuivre = 2;
  std::vector<std::string> alertes;  // ce qui a été approché ou laissé de côté
};
// « projetKicad » : contenu du fichier .kicad_pro voisin (règles, classes de réseaux et leurs motifs). Sans lui,
// les règles sont relevées sur la carte telle qu'elle est routée (largeurs, vias, isolement, bord) et le rapport
// le dit : une carte importée ne doit pas arriver couverte d'erreurs qui ne viennent que de règles par défaut.
ConceptionElec conceptionDepuisKicad(const std::string& texte, const std::string& nom, RapportImportKicad* rapport = nullptr, const std::string& projetKicad = {});

// Export d'une carte vers KiCad (.kicad_pcb, format de KiCad 7, lisible par KiCad 7, 8 et 9) : couches, réseaux,
// empreintes avec leurs pastilles (réseau de chaque pastille), sérigraphie, encombrement et repères, pistes,
// vias, zones (réglages thermiques compris), contour et découpes, textes de la carte, cartouche.
std::string kicadDepuisConception(const ConceptionElec& c, const Connectivite& k);

// Empreinte d'une bibliothèque KiCad (fichier .kicad_mod) : identifiant « kicad:<nom> », pastilles, sérigraphie,
// encombrement et corps comme à l'import d'une carte. Rien si le texte n'est pas une empreinte ou n'a pas de pastille.
std::optional<DefEmpreinte> empreinteDepuisKicadMod(const std::string& texte, std::vector<std::string>* alertes = nullptr);
// Symboles d'une bibliothèque KiCad (.kicad_sym) : broches (numéro, nom, type électrique, côté et ordre), préfixe,
// empreinte, fiche et description ; le corps est redessiné comme celui d'un composant créé ici. Un symbole à
// plusieurs unités garde ses unités (posées une à une, U1A, U1B…), les variantes De Morgan sont laissées ; les symboles dérivés (« extends ») prennent les broches de
// leur parent ; les symboles d'alimentation sont laissés (ceux de Prométhée les remplacent).
std::vector<DefSymbole> symbolesDepuisKicadSym(const std::string& texte, std::vector<std::string>* alertes = nullptr);

}  // namespace prom::ecad
