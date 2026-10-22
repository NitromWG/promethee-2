// Prométhée : revue de conception automatique (lot L13, 13.13 ; AUT-08, ERC-25 du catalogue). Règles lisibles sur la
// netlist et les composants : LED sans résistance, régulateur sans condensateur de sortie ou d'entrée, bus I2C sans
// tirage, circuit intégré sans découplage, entrée laissée en l'air, électrolytique sous une tension trop proche de
// la sienne. Chaque point porte sa règle, sa gravité et la correction proposée. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct PointRevue {
  std::string regle;     // « LED-R », « REG-COUT », « REG-CIN », « I2C-TIRAGE », « DECOUPLAGE », « ENTREE-LIBRE », « CAPA-TENSION »
  std::string gravite;   // « erreur », « avertissement »
  std::string objet;     // repère ou réseau concerné
  std::string message, correction;
};
struct RegleRevue {
  std::string id, nom, explication;
};
const std::vector<RegleRevue>& reglesRevue();
std::vector<PointRevue> revueConception(const ConceptionElec& c);

// Classement des composants par leur rôle, partagé avec les assistants et les essais de production
bool estResistance(const ConceptionElec& c, const ComposantElec& k);
bool estCondensateur(const ConceptionElec& c, const ComposantElec& k);
bool estLed(const ConceptionElec& c, const ComposantElec& k);
bool estRegulateur(const ConceptionElec& c, const ComposantElec& k);
bool estMasse(const std::string& reseau);
bool estAlimentation(const Reseau& r);
double tensionDe(const std::string& texte);  // « +5V », « 3V3 », « 25 V » → volts ; 0 sinon

}  // namespace prom::ecad
