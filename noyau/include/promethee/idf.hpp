// Prométhée : IDF 3.0 en lecture et en écriture (lot L6, tâche 6.17 ; MEC-30). Le fichier de carte (.emn) porte le
// contour, l'épaisseur, les découpes, les trous percés et le placement ; le fichier de bibliothèque (.emp) le contour
// et la hauteur de chaque corps. Unités au choix (MM ou THOU), origine au coin bas gauche de la carte ou à l'origine
// de travail, y vers le haut comme la CAO mécanique ; les composants non montés sont exclus.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct OptionsIdf {
  bool milliemesPouce = false;   // THOU plutôt que MM
  bool origineTravail = false;   // l'origine de travail plutôt que le coin bas gauche
  std::string auteur = "Prométhée";
  std::string date;              // « 2026/10/24.12:00:00 » ; vide : date du cartouche
};
struct FichiersIdf {
  std::string emn, emp;
};
FichiersIdf ecrireIdf(const ConceptionElec& c, const OptionsIdf& o = {});

// Contenu relu, en mm, repère du fichier (y vers le haut)
struct TrouIdf {
  double diametre = 0, x = 0, y = 0;
  bool metallise = false;
  std::string reference, type;  // « BOARD », « J1 » ; « MTG », « PIN », « VIA », « TOOL », « OTHER »
};
struct ComposantIdf {
  std::string geometrie, numeroPiece, reference;
  double x = 0, y = 0, z = 0, rotation = 0;
  bool dessous = false;
  // de la bibliothèque : contour (repère du composant) et hauteur
  std::vector<std::pair<double, double>> contour;
  double hauteur = 0;
};
struct DonneesIdf {
  std::string nom, unite = "MM";
  double epaisseur = 1.6;
  std::vector<std::vector<std::pair<double, double>>> boucles;  // la première : le contour, les suivantes : découpes
  std::vector<TrouIdf> trous;
  std::vector<ComposantIdf> composants;
  std::vector<std::string> alertes;
};
// Lit le fichier de carte et sa bibliothèque ; un arc (angle non nul) est rendu en segments ; les erreurs vont aux
// alertes sans interrompre la lecture
DonneesIdf lireIdf(const std::string& emn, const std::string& emp);
// Carte sans schéma tirée d'un IDF : contour et découpes, trous de fixation non métallisés, chaque composant posé
// avec une empreinte « idf:<géométrie> » de son contour et de sa hauteur
ConceptionElec conceptionDepuisIdf(const DonneesIdf& d);

}  // namespace prom::ecad
