// Prométhée : échanges de la conception électronique avec les autres logiciels (lot L10 ; ECH-08, ECH-09, ROU-16,
// FAB-06, ECH-31, ECH-32 à ECH-34, TST-13).
//
// Netlists en entrée : KiCad (.net) ou liste de Prométhée ; une conception en sort (un symbole par composant, une
// étiquette au bout de chaque broche reliée), ou une conception existante en est mise à jour avec son bilan.
// DXF de la mécanique de la carte. Specctra : carte écrite en DSN pour un routeur externe (Freerouting), session SES
// relue et posée. BoardView pour la réparation, IPC-2581 pour la fabrication. Carte relue des Gerber et perçages
// écrits : pastilles, pistes, vias et réseaux tirés du cuivre connecté, groupés en composants par les attributs X2.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Netlists (10.7, 10.20) ----------
struct ComposantNetlist {
  std::string reference, valeur, empreinte;
};
struct NetlistLue {
  std::vector<ComposantNetlist> composants;
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> reseaux;  // nom → (repère, broche)
  std::vector<std::string> erreurs;
};
NetlistLue lireNetlist(const std::string& texte);  // KiCad (« (export … ») ou liste de Prométhée (netlistTexte)
NetlistLue netlistDe(const ConceptionElec& c);      // la netlist de la conception (comparaisons, essais)
// Empreinte du catalogue la plus proche d'un nom venu d'ailleurs (« Resistor_SMD:R_0603_1608Metric » → « R_0603 »)
std::string empreinteConnue(const std::string& nom);
ConceptionElec conceptionDepuisNetlist(const NetlistLue& n, const std::string& nom);
struct BilanNetlist {
  int ajoutes = 0, valeurs = 0, empreintes = 0, liaisons = 0, nonChangees = 0;
  std::vector<std::string> lignes;  // une ligne par changement ou par écart laissé, pour le rapport CSV
};
BilanNetlist mettreAJourDepuisNetlist(ConceptionElec& c, const NetlistLue& n);
std::string rapportNetlistCsv(const BilanNetlist& b);

// ---------- DXF de la mécanique de la carte (10.8) ----------
// Calques CONTOUR, DECOUPES, PERCAGES (trous non métallisés et de fixation), ENCOMBREMENTS ; millimètres, repère
// mécanique (Y vers le haut, origine au coin bas gauche de la carte)
std::string dxfMecaniqueCarte(const ConceptionElec& c);

// ---------- Specctra (10.11, ROU-16) ----------
std::string specctraDsn(const ConceptionElec& c);
struct SessionSpecctra {
  std::vector<Piste> pistes;
  std::vector<Via> vias;
  std::vector<std::string> erreurs;
};
SessionSpecctra lireSpecctraSes(const std::string& texte);
std::string specctraSes(const ConceptionElec& c);  // la session du routage courant, comme l'écrit Freerouting
// Pose le routage d'une session : pistes et vias des réseaux qu'elle route remplacés ; rend le nombre de pistes
int appliquerSessionSpecctra(ConceptionElec& c, const SessionSpecctra& s);

// ---------- Réparation et fabrication (10.9, 10.23) ----------
std::string boardView(const ConceptionElec& c);  // format BRDOUT, lu par OpenBoardView ; millièmes de pouce
std::string ipc2581(const ConceptionElec& c, const std::string& version = {});

// ---------- Carte relue des fichiers de fabrication (10.13) ----------
struct PastilleRelue {
  Pt position;
  Polygone forme;
  Couches couches = 0;
  std::string reference, numero;  // %TO.P quand le fichier les porte
  int reseau = -1;
};
struct CarteRelue {
  std::vector<PastilleRelue> pastilles;
  std::vector<Piste> pistes;
  std::vector<Via> vias;
  int reseaux = 0;
  std::vector<std::string> erreurs;
};
CarteRelue relireCarte(const std::map<Couche, std::string>& gerbers, const std::string& percagesMetallises);
// Netlist tirée de la carte relue : pastilles groupées en composants par leur repère X2, sinon une par composant
NetlistLue netlistDepuisCarteRelue(const CarteRelue& c);
// Carte modifiable reconstruite (10.13) : une empreinte par composant, tirée de ses pastilles relues (rectangles à
// leur mesure, traversantes là où passe un perçage), posée à sa place ; pistes et vias reposées ; schéma de liaison
// par étiquettes ; contour lu dans le fichier Profile s'il est donné, sinon la boîte du cuivre élargie de 2 mm
ConceptionElec conceptionDepuisCarteRelue(const CarteRelue& r, const std::string& nom, const std::string& contourGerber = {}, const std::string& percagesMetallises = {});

}  // namespace prom::ecad
