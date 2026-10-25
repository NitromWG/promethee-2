// Prométhée : scripts Python sur les commandes annulables (lot L13 de la 0.34 ; 13.3, 13.9, 13.11 ; AUT-03, AUT-09,
// AUT-14, SIM-36). Le script tourne dans un processus Python du poste (« python3 -I », mode isolé), jamais dans
// Prométhée : chaque appel du module « promethee » est une requête JSON-RPC envoyée sur sa sortie et traitée par
// api_commandes.cpp sur le projet confié, la réponse revenant sur son entrée. Le projet confié est une copie : à la
// fin, l'interface le reprend en un seul pas d'annulation, si bien qu'un Ctrl+Z défait tout le script. Points d'arrêt
// et pas à pas par le module bdb ; courbes tracées par le script (fréquence de coupure en fonction de R2…) ; contrôles
// écrits en Python rendus comme des violations ordinaires. Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom {
struct Projet;

struct CourbeScript {
  std::string nom, axeX, axeY;
  std::vector<double> x, y;
};

struct OptionsScript {
  std::string python;               // programme Python (vide : python3, sinon python)
  double delai = 600;               // secondes
  std::set<int> pointsArret;        // lignes du script où s'arrêter
  bool pasAPas = false;             // s'arrêter à chaque ligne
  // Arrêt (point d'arrêt ou pas) : ligne et variables du script ; rend « continuer », « pas » ou « arreter »
  std::function<std::string(int ligne, const nlohmann::ordered_json& variables)> pause;
  std::function<void(const std::string&)> sortie;  // lignes imprimées par le script, au fil de l'eau
  const std::atomic<bool>* arret = nullptr;
  std::string nom = "script";       // nom du fichier dans les messages
  int conception = -1;              // conception visée par les requêtes qui n'en nomment pas (−1 : la première)
  // Hôte du script (14.5, instruments) : il répond aux méthodes qu'il connaît (« instrument.echantillons »…) avant
  // l'API du projet ; rien rendu : la méthode passe à l'API
  std::function<std::optional<nlohmann::ordered_json>(const std::string& methode, const nlohmann::ordered_json& params)> hote;
};

struct ViolationScript {
  std::string code, message, objet;
  double x = 0, y = 0;  // mm
  bool erreur = true;
};

struct ResultatScript {
  bool reussi = false;
  bool pythonAbsent = false;
  std::string sortie, erreurs;
  int ligneErreur = 0;
  int requetes = 0, modifications = 0;
  std::vector<std::string> pas;       // noms des modifications, dans l'ordre (le pas unique les résume)
  std::vector<CourbeScript> courbes;
  std::vector<ViolationScript> violations;
  std::vector<int> arrets;            // lignes où le script s'est arrêté
  std::string nomPas() const;         // « Script ma_macro.py : 10 modifications »
};

// Python du poste et sa version (« Python 3.12.3 »)
bool pythonDisponible(std::string* version = nullptr, const std::string& programme = {});
// Texte du module « promethee » donné aux scripts (aide, et copie pour un éditeur externe)
const std::string& modulePython();
// Exécute « code » sur « p » (une copie à reprendre en un pas, voir en tête)
ResultatScript executerScript(Projet& p, const std::string& code, const OptionsScript& o = {});

// Contrôles écrits en Python (13.11) : chaque script appelle promethee.signaler(...) ; rendus en violations de la
// DRC (« PY-… ») cliquables et excluables comme les autres ; le projet n'est pas modifié
std::vector<ecad::Violation> executerControlesPython(const Projet& p, const std::vector<std::pair<std::string, std::string>>& scripts, std::vector<std::string>* erreurs = nullptr);

}  // namespace prom
