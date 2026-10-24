// Prométhée : dessins 2D échangés avec la carte (lot L10 de la 0.34 ; 10.21 ; ECH-09, ECH-16, ECH-17). Un DXF ou un
// SVG (logo, marquage, découpe) se pose sur la couche choisie, à l'échelle voulue ou à la largeur voulue, centré où
// l'on veut, retourné pour le dessous ; ses discontinuités sont réparées (extrémités réunies à la tolérance) et ses
// contours fermés deviennent des régions pleines, trous compris (le « O » d'un logo reste ouvert), découpées en un
// seul contour par îlot pour le Gerber. Le même dessin peut devenir la forme libre d'une pastille. En sortie, un DXF
// par jeu de couches enregistré (un calque par couche, cotes comprises, miroir pour une vue de dessous). Licence
// GPL-3.0-only.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct OptionsDessinCarte {
  Couche couche = Couche::SerigraphieDessus;
  double echelle = 1;               // en plus de l'unité du fichier
  double largeurVoulue = 0;         // mm ; > 0 : le dessin est mis à cette largeur, proportions gardées
  Pt centre;                        // où poser le centre de l'encombrement (repère de la carte)
  bool remplir = true;              // contours fermés en régions pleines ; sinon en traits
  Nm epaisseur = 150'000;           // traits ouverts et contours non remplis
  double tolerance = 0.01;          // mm : extrémités plus proches réunies (réparation des discontinuités)
  std::set<std::string> calques;    // calques ou groupes repris ; vide : tous
  bool miroir = false;              // retourné (dessin destiné à la face de dessous, vu de dessus)
};
struct RapportDessinCarte {
  int contours = 0, traits = 0, reunis = 0, ilots = 0;
  double largeur = 0, hauteur = 0;  // mm, après mise à l'échelle
  std::vector<std::string> alertes;
};
std::vector<DessinCircuit> dessinsDepuisSvg(const std::string& svg, const OptionsDessinCarte& o, RapportDessinCarte* rapport = nullptr);
std::vector<DessinCircuit> dessinsDepuisDxf(const std::string& dxf, const OptionsDessinCarte& o, RapportDessinCarte* rapport = nullptr);
// Forme libre d'une pastille (éditeur d'empreinte) : le plus grand îlot des contours pleins, centré sur son
// encombrement ; rien si le dessin n'a pas de contour fermé
std::optional<Pastille> pastilleDepuisDessins(const std::vector<DessinCircuit>& d, const std::string& numero);
// Contour quelconque à trous en contours simples, chaque trou relié au bord par une coupure (régions Gerber)
Polygones decouperEnContoursSimples(const Polygones& p);

// ---------- DXF par couches ----------
struct JeuDxf {
  std::string nom = "Toutes les couches";
  std::vector<Couche> couches;      // vide : toutes celles qui portent quelque chose
  bool miroir = false;              // vue de dessous
  bool cotes = true;
  bool contour = true;
};
std::string dxfCouches(const ConceptionElec& c, const JeuDxf& j);
std::vector<JeuDxf> jeuxDxf(const ConceptionElec& c);  // jeux enregistrés dans la conception
void enregistrerJeuDxf(ConceptionElec& c, const JeuDxf& j);

}  // namespace prom::ecad
