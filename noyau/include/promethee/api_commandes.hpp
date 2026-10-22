// Prométhée : interface de commandes en JSON-RPC 2.0 (lot L13 ; 13.3, 13.4, 13.10 ; AUT-03 à AUT-05, AUT-15). Un
// programme de n'importe quel langage lit le projet et le modifie par des requêtes ; l'application les reçoit sur
// un canal local du poste (jamais un port réseau) et chaque requête, ou chaque « lot » de requêtes, forme un seul pas
// d'annulation. Un assistant passe par les mêmes méthodes et ses propositions s'acceptent une à une.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace prom {
struct Projet;

struct ReponseApi {
  nlohmann::ordered_json reponse;  // objet JSON-RPC : « result » ou « error »
  bool modifie = false;            // le projet a changé
  std::string nomPas;              // nom du pas d'annulation (« API : 10 vias posés »)
};
// Méthodes et leur description, pour l'aide et la découverte (« methodes »)
const std::vector<std::pair<std::string, std::string>>& methodesApi();
ReponseApi traiterRequete(Projet& p, const nlohmann::ordered_json& requete);
// Texte d'une requête : lu, traité, et la réponse rendue en texte (une ligne JSON)
ReponseApi traiterTexte(Projet& p, const std::string& texte);

}  // namespace prom
