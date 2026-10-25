// Prométhée : serveur MCP (lot L13 de la 0.34 ; 13.4 ; AUT-05). Un assistant (Claude Desktop, un modèle local…)
// lance « promethee mcp <projet> » et dialogue en JSON-RPC 2.0 sur l'entrée et la sortie standard, au protocole MCP :
// il lit le projet (résumé, composants, réseaux, ERC, DRC, revue) et propose des commandes de l'interface JSON-RPC
// (api_commandes.hpp). Le serveur ne modifie jamais le projet : chaque proposition, essayée sur une copie, s'inscrit
// dans un fichier à côté du projet ; l'atelier la montre, et l'utilisateur l'accepte (un pas d'annulation) ou la
// refuse (rien ne change). Licence GPL-3.0-only.
#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/api_commandes.hpp"
#include "promethee/modele.hpp"

namespace prom {

struct PropositionAssistant {
  int numero = 0;
  std::string methode, raison, auteur = "assistant", date;
  nlohmann::ordered_json params = nlohmann::ordered_json::object();
  std::string etat = "en attente";  // « en attente », « acceptée », « refusée »
  std::string resultat;              // ce qu'a donné l'acceptation, ou la raison du refus
  std::string essai;                 // ce qu'a donné l'essai sur une copie au moment de la proposition
  friend bool operator==(const PropositionAssistant& a, const PropositionAssistant& b) { return a.numero == b.numero && a.etat == b.etat && a.methode == b.methode && a.params == b.params; }
};
nlohmann::ordered_json propositionVersJson(const PropositionAssistant& p);
PropositionAssistant propositionDepuisJson(const nlohmann::ordered_json& j);
std::string cheminPropositions(const std::string& projet);  // « <projet>.propositions.json » à côté du projet
std::vector<PropositionAssistant> lirePropositions(const std::string& texte);
std::string textePropositions(const std::vector<PropositionAssistant>& v);

// Acceptation : la requête appliquée au projet (la réponse dit le nom du pas) ; refus : rien ne change
ReponseApi accepterProposition(Projet& p, PropositionAssistant& x);
void refuserProposition(PropositionAssistant& x, const std::string& raison = {});

// Outils offerts à l'assistant (nom, description), pour l'aide
const std::vector<std::pair<std::string, std::string>>& outilsMcp();

class ServeurMcp {
 public:
  explicit ServeurMcp(Projet p);
  // Un message de l'assistant (une ligne JSON) ; rend la réponse (rien pour une notification)
  std::optional<std::string> traiter(const std::string& message);
  const std::vector<PropositionAssistant>& propositions() const { return m_propositions; }
  void reprendre(Projet p) { m_projet = std::move(p); }  // projet relu (après une acceptation dans l'atelier)
  void reprendrePropositions(std::vector<PropositionAssistant> v) { m_propositions = std::move(v); }
  bool initialise() const { return m_initialise; }

 private:
  nlohmann::ordered_json appelerOutil(const std::string& nom, const nlohmann::ordered_json& arguments, bool& erreur);
  Projet m_projet;
  std::vector<PropositionAssistant> m_propositions;
  bool m_initialise = false;
};

// Boucle du serveur : projet lu depuis « cheminProjet », propositions écrites à côté de lui ; rend le code de sortie
int servirMcp(const std::string& cheminProjet, std::istream& entree, std::ostream& sortie);

}  // namespace prom
