// Prométhée : lecture d'un dessin SVG en formes (lot L7, tâche 7.19 ; SAI-55). Éléments lus : line, rect (coins
// arrondis), circle, ellipse, polyline, polygon, path (M L H V C S Q T A Z, absolus et relatifs) ; groupes imbriqués
// et transformations (translate, scale, rotate, skewX, skewY, matrix) ; trait, fond et épaisseur, en attributs ou en
// style. Les arcs et les quadratiques deviennent des courbes de Bézier cubiques (exactes à la transformation près),
// cercles et ellipses restent tels quels tant que la transformation ne les déforme pas. L'unité vient de width,
// height et viewBox (mm, cm, in, pt, px à 96 par pouce) ; tout est rendu en millimètres, Y vers le bas comme le SVG.
// defs, symbol, use, text et image ne sont pas de la géométrie : ils sont comptés et signalés.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace prom::svg {

struct Pt2 {
  double x = 0, y = 0;
};
struct FormeSvg {
  enum class Genre : std::uint8_t { Polyligne, Bezier, Cercle, Ellipse, Rectangle };
  Genre genre = Genre::Polyligne;
  // Polyligne : sommets ; Bézier : départ, deux points de contrôle, arrivée ; cercle et ellipse : centre ; rectangle :
  // deux coins
  std::vector<Pt2> points;
  double rx = 0, ry = 0;  // cercle, ellipse ; rectangle : rayon des coins
  bool ferme = false;
  std::string trait, fond;  // « #RRGGBB », vide : aucun
  std::string fondChemin;   // fond du chemin dont la forme est un morceau (un chemin est coupé à chaque courbe, 10.21)
  double epaisseur = 0;     // mm
  std::string calque;       // identifiant ou étiquette du groupe parent le plus proche
};
struct DessinSvg {
  std::vector<FormeSvg> formes;
  double largeur = 0, hauteur = 0;  // mm
  std::map<std::string, int> ignores;  // élément → nombre
  std::vector<std::string> alertes;
};
DessinSvg lireSvg(const std::string& texte);

}  // namespace prom::svg
