// Prométhée : lecture des cartes KiCad (.kicad_pcb). Licence GPL-3.0-only.
// Coordonnées converties dans le repère de Prométhée : millimètres, axe Y vers le haut.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "promethee/derive.hpp"
#include "promethee/modele.hpp"

namespace prom {

struct EmpreinteKicad {
  std::string ref, valeur, nom;          // nom : bibliothèque:empreinte
  Point position{};
  double rotation = 0;                   // degrés, sens direct
  bool dessous = false;                  // posée sur la face arrière
  double xmin = 0, ymin = 0, xmax = 0, ymax = 0;  // encombrement (zone de placement, sinon pastilles)
  bool fixation = false;                 // empreinte de trou de fixation
  double percage = 0;                    // diamètre de perçage principal, s'il y en a un
};

struct CarteKicad {
  std::string titre;
  double epaisseur = 1.6;
  std::vector<Contour> contours;         // contours Edge.Cuts reconstitués ; le premier est l'extérieur
  std::vector<EmpreinteKicad> empreintes;
  double xmin = 0, ymin = 0, xmax = 0, ymax = 0;  // encombrement du contour extérieur
};

struct RapportImport {
  int composants = 0, trous = 0, dessous = 0, connecteursBord = 0;
};
// Projet Prométhée tiré d'une carte KiCad : contour réel, trous de fixation, composants de la face avant
// (hauteurs estimées d'après le nom de l'empreinte, à vérifier), le tout recentré sur l'origine.
Projet projetDepuisKicad(const CarteKicad& carte, const std::string& nom, RapportImport* rapport = nullptr);
double hauteurEstimee(const std::string& nomEmpreinte);

CarteKicad lireKicad(const std::string& texte);
// Nouvelle carte KiCad (format de KiCad 7, lisible par KiCad 7, 8 et 9) : contour Edge.Cuts, trous de fixation,
// emplacements des composants sur User.1 (dessus) et User.2 (dessous).
std::string kicadDepuisProjet(const Projet& p, const Derive& d);
// Aller-retour : la carte KiCad d'origine, avec le contour et la position des trous de fixation du projet.
std::string kicadMisAJour(const std::string& texteOriginal, const Projet& p, const Derive& d);          // contenu d'un fichier .kicad_pcb
CarteKicad lireFichierKicad(const std::string& chemin);  // lève std::runtime_error

}  // namespace prom
