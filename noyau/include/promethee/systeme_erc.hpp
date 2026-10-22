// Prométhée : netlist et vérification du système (lot L12, 12.19 ; STR-12, ERC-24 du catalogue). Les liaisons du
// projet (systeme.hpp) relient les cartes connecteur à connecteur ; la netlist globale réunit les réseaux qu'elles
// joignent, et l'ERC du système y cherche deux sorties reliées, deux alimentations différentes jointes, un connecteur
// introuvable ou des brochages de longueurs différentes. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

namespace prom {
struct Projet;

struct ReseauSysteme {
  std::string nom;                               // « carte:réseau » du premier membre, ou son nom s'il est partagé
  std::vector<std::pair<std::string, std::string>> membres;  // (nom de la carte, réseau)
  std::vector<std::string> liaisons;             // câbles qui le portent
};
struct NetlistSysteme {
  std::vector<ReseauSysteme> reseaux;            // seuls les réseaux qui traversent au moins une liaison
  std::vector<std::string> alertes;              // connecteur ou carte introuvable…
};
NetlistSysteme netlistSysteme(const Projet& p);

struct ViolationSysteme {
  bool erreur = true;
  std::string liaison, reseau, message;
};
std::vector<ViolationSysteme> verifierSysteme(const Projet& p);

}  // namespace prom
