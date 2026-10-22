// Prométhée : comparaison et fusion de conceptions (lot L12 ; 12.2, 12.8 ; PRJ-03, STR-11, PRJ-20). Les objets
// s'apparient par leur UUID ; deux fichiers sans ancêtre commun s'alignent d'abord (repère, puis position à une
// tolérance près). Les différences se rangent par catégorie (composant, symbole posé, fil, piste…) ; la fusion à
// trois voies garde ce qui n'a changé que d'un côté et ne demande que pour ce qui a changé des deux côtés
// autrement. Le travail se fait sur la forme enregistrée (JSON) : tout ce que le fichier porte est comparé et fusionné,
// y compris ce qu'ajouteront les versions suivantes. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct DifferenceObjet {
  std::string categorie;  // « composant », « symbole posé », « fil », « étiquette », « empreinte », « piste », « via »…
  std::string genre;      // « ajouté », « retiré », « modifié »
  std::string chemin;     // chemin dans le fichier : « composants[uuid=…].valeur »
  std::string repere;     // ce que l'utilisateur reconnaît : « R3 », « GND », « feuille Alimentation »
  std::string detail;     // « valeur : 10k → 4k7 »
};
struct ComparaisonConceptions {
  std::vector<DifferenceObjet> differences;
  std::map<std::string, int> parCategorie;
  bool identiques() const { return differences.empty(); }
};
ComparaisonConceptions comparerConceptions(const ConceptionElec& a, const ConceptionElec& b);

// Deux fichiers sans ancêtre commun (12.8) : « autre » reçoit les UUID des objets de « reference » qu'il apparie
// (feuilles par nom, composants par repère, symboles par composant et unité, fils, pistes et vias par position à
// « tolerance » près, étiquettes par texte et position) ; rend aussi le nombre d'objets appariés
ConceptionElec aligner(const ConceptionElec& reference, const ConceptionElec& autre, Nm tolerance = 10'000, int* apparies = nullptr);

// Réseaux renommés d'une conception à l'autre, reconnus au recouvrement de leurs broches (au moins 60 %)
std::vector<std::pair<std::string, std::string>> reseauxRenommes(const ConceptionElec& a, const ConceptionElec& b);

// ---------- Fusion à trois voies (12.8) ----------
struct ConflitFusion {
  std::string chemin, categorie, repere, propriete;
  std::string ancetre, nous, eux;  // valeurs en texte (JSON compact)
};
enum class ChoixFusion { Nous, Eux };
struct ResultatFusion {
  ConceptionElec conception;
  std::vector<ConflitFusion> conflits;  // tranchés par « choix », sinon « nous »
  int deNous = 0, dEux = 0;             // changements repris de chaque côté
};
// « choix » : chemin d'un conflit → côté gardé
ResultatFusion fusionner(const ConceptionElec& ancetre, const ConceptionElec& nous, const ConceptionElec& eux, const std::map<std::string, ChoixFusion>& choix = {});

}  // namespace prom::ecad
