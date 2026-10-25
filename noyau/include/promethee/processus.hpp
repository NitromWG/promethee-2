// Prométhée : programmes du poste lancés par le noyau (Git, compilateurs, émulateurs, simulateurs de logique). La
// ligne de commande est un tableau, jamais passé à un interpréteur de commandes ; le dossier, l'entrée standard, le
// délai et des variables d'environnement se choisissent ; les sorties sont lues en entier. Prométhée ne donne
// l'accès au réseau à aucun de ces programmes : l'appelant refuse ce qui en demanderait (git_projet.cpp), et le
// journal des lancements permet aux essais de le vérifier. Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace prom {

struct ResultatProcessus {
  bool lance = false;          // le programme a démarré
  int code = -1;               // code de sortie
  bool delaiDepasse = false;   // arrêté au bout du délai
  std::string sortie, erreurs;
  bool reussi() const { return lance && !delaiDepasse && code == 0; }
};

struct OptionsProcessus {
  std::string dossier;                               // vide : dossier courant
  std::string entree;                                // envoyé sur l'entrée standard, qui est ensuite fermée
  double delai = 120;                                // secondes
  std::map<std::string, std::string> environnement;  // variables ajoutées ou remplacées
};

// Lance « argv[0] » (cherché dans le PATH s'il n'a pas de chemin) et attend sa fin ; un programme introuvable rend
// « lance » faux et la raison dans « erreurs »
ResultatProcessus executerProcessus(const std::vector<std::string>& argv, const OptionsProcessus& o = {});

// Dialogue avec un programme (scripts, serveur MCP, moteur de l'assistant) : chaque ligne de sa sortie standard
// passe par « repondre », appelé sur le fil de l'appelant ; une réponse lui est écrite sur son entrée standard, suivie
// d'une fin de ligne ; std::nullopt : la ligne va dans « sortie ». « arret » mis à vrai arrête le programme.
ResultatProcessus dialoguerProcessus(const std::vector<std::string>& argv, const OptionsProcessus& o,
                                     const std::function<std::optional<std::string>(const std::string& ligne)>& repondre, const std::atomic<bool>* arret = nullptr);

// Programme qui reste lancé et dialogue à la demande (simulateur de logique en pas à pas, GDB en GDB/MI, 15.5 et
// 15.7) : l'appelant écrit quand il veut et attend la ligne suivante ; la sortie est lue par un fil à part, les
// erreurs gardées. Arrêté à la destruction.
class ProcessusInteractif {
 public:
  ProcessusInteractif();
  ~ProcessusInteractif();
  ProcessusInteractif(const ProcessusInteractif&) = delete;
  ProcessusInteractif& operator=(const ProcessusInteractif&) = delete;
  bool demarrer(const std::vector<std::string>& argv, const OptionsProcessus& o = {}, std::string* erreur = nullptr);
  bool enCours() const;
  bool ecrire(const std::string& texte);  // tel quel (ajouter la fin de ligne)
  // Ligne suivante de la sortie standard (sans sa fin de ligne), au plus « delai » secondes ; nullopt : rien venu
  // ou programme terminé
  std::optional<std::string> lireLigne(double delai = 10);
  std::string erreurs() const;
  void fermerEntree();
  int arreter();  // tue le programme s'il tourne encore ; rend son code de sortie
  struct Etat;

 private:
  std::shared_ptr<Etat> m_etat;
};

// Chemin complet d'un programme du PATH (« git », « ghdl », « avr-gcc »…), vide s'il n'y est pas
std::string programmeDuPoste(const std::string& nom);

// Lignes de commande lancées depuis le début (ou le dernier effacement), dans l'ordre
std::vector<std::vector<std::string>> programmesLances();
void effacerJournalProgrammes();

}  // namespace prom
