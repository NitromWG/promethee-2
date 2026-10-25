// Prométhée : schéma déduit d'une carte et calque des retouches (lot L17 de la 0.34 ; 17.6 ; TST-12, TST-14).
//
// Le schéma de liaison de l'import KiCad devient une commande valable pour toute carte : depuis un circuit sans
// schéma (composants posés sur la carte seule), retracé sur une photo ou redessiné, les réseaux se tirent du cuivre
// (pastilles, pistes, vias et zones qui se touchent, par ensembles disjoints, chaque îlot nommé par le réseau que
// porte son cuivre), puis un symbole se pose par composant, une étiquette au nom du réseau au bout de chaque broche,
// à ranger ensuite à la main.
//
// Les retouches d'une révision fabriquée (fil de reprise, piste coupée, composant changé) se notent sur un calque lié
// à la révision et aux numéros de série ; la même commande met le schéma à jour, la liste des retouches sort par
// carte, et la révision suivante les reçoit en modifications proposées, acceptées une à une.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Réseau de chaque pastille numérotée, d'après le cuivre seul : (UUID du composant, numéro) → nom du réseau. Un îlot
// prend le nom le plus porté par ses pistes, vias et zones ; sans nom, « N-<repère>-<pastille> »
std::map<std::pair<std::string, std::string>, std::string> reseauxDuCuivre(const ConceptionElec& c);

struct BilanSchemaDeduit {
  int symboles = 0, etiquettes = 0, reseaux = 0, symbolesCrees = 0;
  std::vector<std::string> alertes;
};
// Pose au schéma (feuille « feuille ») un symbole par composant de la carte qui n'en a pas, une étiquette globale au
// nom du réseau du cuivre au bout de chaque broche reliée, une marque de non-connexion sur les autres ; les
// composants quittent la carte seule. Un composant sans symbole connu (ou dont les broches ne couvrent pas les
// pastilles) reçoit un symbole dessiné d'après son empreinte.
BilanSchemaDeduit schemaDepuisCircuit(ConceptionElec& c, std::size_t feuille = 0);

// ---------- Retouches d'une révision fabriquée (TST-14) ----------
enum class GenreRetouche : std::uint8_t { FilDeReprise, PisteCoupee, ComposantChange };
struct Retouche {
  std::string uuid;
  GenreRetouche genre = GenreRetouche::FilDeReprise;
  std::string revision;                  // révision scellée où elle a été faite (« A »)
  std::vector<std::string> numerosSerie;  // cartes retouchées ; vide : toutes
  std::string a, b;                      // fil : deux broches « R1.2 », « U1.3 »
  std::string piste;                     // piste coupée : UUID
  std::string composant, valeur;         // composant changé : repère, nouvelle valeur
  std::string description;
  bool appliquee = false;                // portée au schéma
  friend bool operator==(const Retouche&, const Retouche&) = default;
};
std::vector<Retouche> retouchesDe(const ConceptionElec& c);  // c.extensions["retouches"]
void ecrireRetouches(ConceptionElec& c, const std::vector<Retouche>& v);
std::string ajouterRetouche(ConceptionElec& c, Retouche r);  // rend son UUID
// Retouches d'une carte (numéro de série), dans l'ordre où elles ont été notées
std::vector<Retouche> retouchesDeCarte(const ConceptionElec& c, const std::string& numeroSerie);
std::string listeRetouches(const ConceptionElec& c, const std::string& numeroSerie = {});  // CSV : carte, genre, description
// Le schéma suit les retouches : fil de reprise (les deux réseaux réunis), composant changé (sa valeur), piste
// coupée (la piste retirée, les broches séparées du cuivre prennent un réseau à elles) ; rend le nombre appliqué
int appliquerRetouches(ConceptionElec& c);
// Modifications proposées à la révision suivante : les retouches de « fabriquee » qu'elle n'a pas encore reçues ;
// chacune s'accepte seule
std::vector<Retouche> retouchesProposees(const ConceptionElec& fabriquee, const ConceptionElec& suivante);
bool accepterRetouche(ConceptionElec& suivante, const Retouche& r, std::string* erreur = nullptr);

}  // namespace prom::ecad
