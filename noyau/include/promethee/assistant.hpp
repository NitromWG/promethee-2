// Prométhée : assistant de conception (lot L13 de la 0.34 ; 13.15, 13.16, 13.17 ; AUT-18 à AUT-25). Le moteur se
// choisit : modèle local servi par llama.cpp (programme llama-cli et modèle GGUF), Ollama sur le poste, ou tout
// programme qui lit la question sur son entrée et écrit la réponse ; Prométhée ne l'appelle que comme un programme du
// poste (processus.hpp) et n'ouvre lui-même aucune connexion ; un service en ligne ne passe que par le programme
// d'accès en ligne de la décision D13, si l'utilisateur l'active. « Expliquer ce bloc » et « Demander à l'assistant »
// sur une violation se rédigent ici, avec la mémoire des règles maison, montrée et suspendable ; toute correction
// reste une proposition (assistant_mcp.hpp). Fiches techniques interrogées : texte des pages, passages retrouvés par
// recherche plein texte (BM25), réponse tirée de ces seuls passages avec la page citée. Simulation confiée : la
// question (« fréquence de coupure de ce filtre ? ») devient une analyse lancée par ngspice, remèdes de convergence
// compris, et une mesure, avec la courbe jointe. Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom {

// ---------- Moteur (13.15) ----------
struct MoteurAssistant {
  std::string genre = "aucun";        // « llama.cpp », « ollama », « commande », « aucun »
  std::string programme, modele;      // programme (« llama-cli », chemin) ; modèle (.gguf, ou nom pour Ollama)
  std::vector<std::string> arguments; // arguments de plus
  int jetons = 512;                   // longueur maximale de la réponse
  double delai = 180;
  bool configure() const { return genre != "aucun" && (!programme.empty() || genre == "ollama"); }
};
nlohmann::ordered_json moteurVersJson(const MoteurAssistant& m);
MoteurAssistant moteurDepuisJson(const nlohmann::ordered_json& j);
// Ligne de commande qui pose la question (la question va sur l'entrée standard pour « commande » et « ollama »)
std::vector<std::string> commandeMoteur(const MoteurAssistant& m, const std::string& invite);

struct ReponseAssistant {
  bool ok = false;
  std::string texte, erreur;
  std::vector<std::string> commande;  // ce qui a été lancé
};
ReponseAssistant demander(const MoteurAssistant& m, const std::string& systeme, const std::string& question);

// Mémoire des règles maison : montrée, modifiable, suspendable
struct MemoireAssistant {
  std::vector<std::string> regles;
  bool suspendue = false;
  std::string texte() const;
  void lire(const std::string& texte);
};
// Ce qu'est un bloc : ses composants (repère, valeur, article, référence), les réseaux qui les relient, ceux qui le
// relient au reste du schéma
std::string contexteBloc(const ecad::ConceptionElec& c, const std::set<std::string>& composants);
std::string inviteExpliquerBloc(const ecad::ConceptionElec& c, const std::set<std::string>& composants, const MemoireAssistant& m);
std::string inviteViolation(const ecad::ConceptionElec& c, const ecad::Violation& v, const MemoireAssistant& m);
std::string systemeAssistant();  // consignes communes (français, propositions acceptées une à une, aucune invention de valeur)

// ---------- Fiches techniques (13.16) ----------
struct PageFiche {
  int numero = 0;
  std::string texte;
};
struct FicheTechnique {
  std::string nom;
  std::vector<PageFiche> pages;
};
// Texte page par page : pdftotext du poste s'il est installé, sinon le lecteur de Prométhée (PDF à flux en clair)
std::optional<FicheTechnique> lireFicheTechnique(const std::string& pdf, const std::string& nom, std::string* erreur = nullptr);
struct ExtraitFiche {
  int page = 0;
  std::string passage;
  double score = 0;
};
std::vector<ExtraitFiche> chercherDansFiche(const FicheTechnique& f, const std::string& question, std::size_t n = 3);
struct ReponseFiche {
  bool trouvee = false;
  std::string valeur, unite, texte;  // « 1,5 », « A », « Courant de sortie maximal : 1,5 A (page 1 : « … ») »
  double nombre = 0;
  ExtraitFiche extrait;
};
// Réponse tirée des seuls passages retrouvés ; « moteur » configuré : il rédige à partir d'eux, sinon une valeur
// avec son unité est relevée dans le meilleur passage
ReponseFiche repondreSurFiche(const FicheTechnique& f, const std::string& question, const MoteurAssistant* moteur = nullptr);

// ---------- Simulation confiée (13.17) ----------
struct ReponseSimulation {
  bool ok = false;
  std::string texte, erreur;
  double valeur = 0;
  std::string unite, reseau, analyse;
  std::vector<double> x, y;  // courbe jointe (fréquence, gain en dB ; ou temps, tension)
  std::string axeX, axeY;
  std::vector<std::string> journal;
};
ReponseSimulation repondreSimulation(const ecad::ConceptionElec& c, const std::string& question);

}  // namespace prom
