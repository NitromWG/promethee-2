// Prométhée : fabrication à l'atelier (lot L16 ; 16.7, 16.8, 16.9 ; FAB-21, FAB-22, FAB-23). Gravure à l'anglaise
// par fraiseuse : sillons d'isolement autour du cuivre de chaque réseau, par décalages successifs, en G-code ;
// perçages et découpe du contour avec ses attaches. Pochoir de pâte à braser tiré des pastilles CMS, ouvertures
// réduites ou agrandies, en SVG ou en DXF pour la découpe laser ou vinyle. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct OptionsUsinage {
  double diametreOutil = 0.2;   // mm, largeur de coupe utile (fraise en V à la profondeur choisie)
  int passes = 2;               // sillons concentriques autour du cuivre
  double recouvrement = 0.4;    // part de la largeur reprise d'une passe à la suivante
  double profondeur = 0.08;     // mm sous la surface du cuivre
  double avance = 300, plongee = 100;  // mm/min
  double securite = 2;          // mm au-dessus de la carte entre deux sillons
  int broche = 12000;           // tr/min
  bool miroir = false;          // couche du dessous vue de dessous : X retourné autour du centre de la carte
  // Perçages et découpe
  double profondeurPercage = 1.8, profondeurDecoupe = 1.8, diametreDecoupe = 1.0;
  int attaches = 4;             // attaches laissées sur le contour
  double largeurAttache = 1.5;  // mm
};

struct Sillon {
  std::vector<Pt> points;  // fermé : le premier point revient à la fin
  int passe = 0;
  std::string reseau;
};
struct ResultatIsolement {
  std::vector<Sillon> sillons;
  std::vector<std::string> alertes;  // « isolement impossible entre GND et +5V : 0,15 mm pour un outil de 0,20 mm »
  double longueur = 0;               // mm de sillons
};
// Sillons d'isolement de la couche de cuivre « couche » (CuivreDessus ou CuivreDessous)
ResultatIsolement sillonsIsolement(const ConceptionElec& c, Couche couche, const OptionsUsinage& o = {});
// Cuivre de chaque réseau de la couche (pastilles, pistes, zones remplies), réuni ; « » pour le cuivre isolé
std::map<std::string, Polygones> cuivreParReseau(const ConceptionElec& c, Couche couche);

std::string gcodeIsolement(const ConceptionElec& c, const ResultatIsolement& r, const OptionsUsinage& o = {});
std::string gcodePercages(const ConceptionElec& c, const OptionsUsinage& o = {});
std::string gcodeDecoupe(const ConceptionElec& c, const OptionsUsinage& o = {});
// Temps d'usinage estimé (minutes) d'un G-code écrit par Prométhée : longueurs aux avances
double dureeGcode(const std::string& gcode);

// ---------- Pochoir de pâte (16.9) ----------
// Ouvertures du pochoir : pastilles CMS de la face, réduites (« reduction » > 0, mm par côté) ou agrandies (< 0)
Polygones ouverturesPochoir(const ConceptionElec& c, bool dessous, double reduction = 0.05);
std::string pochoirSvg(const ConceptionElec& c, bool dessous, double reduction = 0.05);
std::string pochoirDxf(const ConceptionElec& c, bool dessous, double reduction = 0.05);

}  // namespace prom::ecad
