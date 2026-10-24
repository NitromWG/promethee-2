// Prométhée : structure des grands schémas (lot L7, tâches 7.2 à 7.5, 7.12, 7.13, 7.15 ; STR-02 à STR-06, STR-15,
// STR-17, STR-22, S10).
//
// Bus : une étiquette « D[0..7] » posée sur un fil en fait un bus ; ses fils (reliés bout à bout) ne relient rien
// électriquement, et les fils d'entrée qui le touchent portent les membres D0 à D7, qui se rejoignent par leur nom.
// Une entrée qui porte un nom hors du bus (D8) est signalée (ERC-32).
// Feuille de canal : « 4 × canal » fait trois copies liées de la feuille, composants copiés avec des repères par canal
// (R5 → R105, R205, R305), étiquettes locales propres à chaque copie, globales et alimentations partagées ; le
// placement et le routage du premier canal se reproduisent sur les autres.
// Déplacer vers une feuille : les symboles gardent UUID, repères et empreintes (donc leurs pistes) ; chaque liaison
// coupée devient une paire d'étiquettes globales au nom du réseau.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct NomBus {
  std::string prefixe;
  int debut = 0, fin = 0;
};
std::optional<NomBus> lireNomBus(const std::string& texte);  // « D[0..7] »
// Membres d'un bus ou d'un faisceau (7.13, STR-17) : « D[0..7] » → D0 … D7 ; « {SDA SCL INT} » → SDA, SCL, INT ; un
// alias de la conception (« MEM » → « A[0..15] D[0..7] RD WR ») développé, récursivement ; vide pour un nom simple
std::vector<std::string> membresFaisceau(const std::string& nom, const std::map<std::string, std::vector<std::string>>& alias = {});
struct BusFeuille {
  NomBus nom;
  std::string etiquette;          // UUID de l'étiquette qui le nomme
  std::set<std::string> fils;     // UUID des fils du bus
  std::vector<std::pair<std::string, std::string>> entrees;  // UUID du fil d'entrée, nom porté (« D3 »)
  std::vector<std::string> membres;  // noms de ses membres, développés (bus, faisceau, alias)
};
// Bus et faisceaux d'une feuille ; « alias » : ceux de la conception (aliasBus), qui font aussi des faisceaux
std::vector<BusFeuille> busDeFeuille(const Feuille& f, const std::map<std::string, std::vector<std::string>>* alias = nullptr);

// Feuille de canal ; rend le nombre de copies faites
int repeterFeuille(ConceptionElec& c, std::size_t feuille, int exemplaires);
// Placement et routage du canal source reproduits sur ses copies, chacune décalée de « ecart » de plus que la
// précédente ; rend le nombre d'empreintes placées
int reproduireCanal(ConceptionElec& c, std::size_t feuilleSource, Pt ecart);

struct BilanDeplacement {
  int symboles = 0, fils = 0, etiquettes = 0;
};
BilanDeplacement deplacerVersFeuille(ConceptionElec& c, std::size_t source, const std::set<std::string>& symboles, std::size_t cible);

// Tension d'un réseau d'après ses alimentations (« +12V » : 12, « GND » : 0, « +3V3 » : 3,3), vide si inconnue
std::optional<double> tensionReseau(const Reseau& r);

}  // namespace prom::ecad
