// Prométhée : schéma de système (lot L12, 12.19, STR-12) : les cartes du projet reliées par des câbles, connecteur à
// connecteur. La netlist globale raccorde celles des cartes par les broches de leurs connecteurs ; l'ERC du système
// tourne sur ce graphe. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace prom {

struct LiaisonSysteme {
  std::string uuid, nom;               // « W1 », « Câble capteur »
  std::string genre = "cable";         // « cable », « directe » (connecteurs enfichés), « faisceau »
  std::string carteA, connecteurA;     // UUID de la conception, repère du connecteur
  std::string carteB, connecteurB;
  // Broche de A → broche de B ; vide : broche à broche, du même numéro
  std::vector<std::pair<std::string, std::string>> broches;
  friend bool operator==(const LiaisonSysteme&, const LiaisonSysteme&) = default;
};

nlohmann::ordered_json liaisonVersJson(const LiaisonSysteme& l);
LiaisonSysteme liaisonDepuisJson(const nlohmann::ordered_json& o);

}  // namespace prom
