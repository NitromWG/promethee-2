// Prométhée : ligne de commande sous le canevas et macros (lot L13 de la 0.34 ; 13.8 ; AUT-10, AUT-13, AUT-20). Chaque
// commande de l'interface JSON-RPC (api_commandes.hpp) a sa forme écrite : « carte.contour largeur=100 hauteur=80
// rayon=3 », ou en mots, sans accents si l'on veut (« carte contour largeur=100mm »). Valeurs : nombres avec unité
// (mm, cm, mil, in, µm, deg), textes entre guillemets ou mots, listes [x, y], oui/non. Une macro est une suite de ces
// lignes, enregistrée en direct ou écrite à la main dans un fichier texte, rejouée en un lot : un seul pas
// d'annulation. Les commandes probables se proposent d'après les successions observées sur le poste.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/api_commandes.hpp"

namespace prom {

// Requête JSON-RPC d'une ligne ; rien pour une ligne vide ou un commentaire (« # … ») ; erreur : la raison
std::optional<nlohmann::ordered_json> requeteDeLigne(const std::string& ligne, std::string* erreur = nullptr);
// Forme écrite d'une requête (enregistrement d'une macro)
std::string ligneDeRequete(const nlohmann::ordered_json& requete);
// Méthode reconnue d'un mot ou de deux (« carte contour », « sérigraphie » sans accent), vide sinon
std::string methodeDeMots(const std::string& mots);
// Paramètres connus d'une méthode (tirés de sa description : « {x, y, reseau?} »)
std::vector<std::string> parametresDe(const std::string& methode);
// Complétion de ce qui est tapé : méthodes, puis clés de la méthode
std::vector<std::string> completer(const std::string& debut, std::size_t n = 12);

struct Macro {
  std::string nom, description;
  std::vector<std::string> lignes;  // commandes, sans les commentaires
};
// Texte d'une macro : « # nom : … », « # … » (description), puis une commande par ligne
Macro lireMacro(const std::string& texte);
std::string texteMacro(const Macro& m);
// Toutes les lignes en un lot (un seul pas, tout ou rien) ; « erreurs » : « ligne n : … »
ReponseApi executerMacro(Projet& p, const Macro& m, std::vector<std::string>* erreurs = nullptr);

// Commandes probables d'après les successions observées (chaîne de Markov d'ordre 1, gardée par le poste)
class PredicteurCommandes {
 public:
  void observer(const std::string& methode);
  std::vector<std::string> proposer(std::size_t n = 3) const;  // après la dernière observée
  std::string texte() const;
  void lire(const std::string& texte);

 private:
  std::map<std::string, std::map<std::string, int>> m_suites;
  std::string m_derniere;
};

}  // namespace prom
