// Prométhée : requêtes sur les objets de la carte (lot L13 de la 0.34 ; 13.11 ; AUT-16, DRC-04, DRC-39). Le langage
// d'expressions unique (expressions.hpp) compte et choisit les objets visés — pistes, vias, pastilles, zones,
// composants — pour l'éditeur de requêtes, qui marque une requête invalide avec sa raison ; une règle (contrainte du
// gestionnaire) se crée depuis la requête courante. Historique, favoris et exemples se gardent par poste.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "promethee/contraintes.hpp"
#include "promethee/electronique.hpp"

namespace prom::ecad {

struct ObjetsVises {
  int pistes = 0, vias = 0, pastilles = 0, zones = 0, composants = 0;
  std::vector<std::string> uuids;  // pistes, vias, zones ; composants (pour leurs pastilles et eux-mêmes)
  int total() const { return pistes + vias + pastilles + zones + composants; }
  std::string texte() const;       // « 3 pistes, 2 pastilles »
};
// Objets visés par la requête ; requête invalide : rien, et la raison (avec sa position) dans « erreur »
ObjetsVises objetsVises(const ConceptionElec& c, const std::string& requete, std::string* erreur = nullptr);

// Règle d'isolement, de largeur… créée depuis des requêtes : « couches » 0 toutes ; « externes » les deux faces
Contrainte regleDepuisRequetes(const std::string& nom, GenreContrainte genre, const std::string& requeteA, const std::string& requeteB, Nm minimum, std::uint64_t couches = 0);
std::uint64_t couchesExternes();

struct HistoriqueRequetes {
  std::vector<std::string> recentes, favoris;
  void noter(const std::string& requete, std::size_t garder = 30);
  void basculerFavori(const std::string& requete);
  std::string texte() const;
  void lire(const std::string& texte);
};
// Exemples fournis (requête, ce qu'elle vise)
const std::vector<std::pair<std::string, std::string>>& exemplesRequetes();

}  // namespace prom::ecad
