// Prométhée : dessins sur la carte et opérations sur les formes (lot L7, tâches 7.22 et 7.23 ; CAR-26, CAR-27, CAR-31).
// Congé et chanfrein au coin de deux traits ; contour décalé autour de pastilles réunies (union puis décalage par
// Clipper2) : des pastilles proches donnent un seul polygone.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Coin commun à deux traits (un bout partagé), à « tolerance » près ; faux s'ils ne se touchent pas
bool coinCommun(const DessinCircuit& a, const DessinCircuit& b, Pt& coin, Nm tolerance = 1'000);
// Congé de rayon « rayon » au coin des deux traits : chacun raccourci à son point de tangence, l'arc posé entre eux
// (même couche, même épaisseur) ; rend l'UUID de l'arc, vide et la raison sinon
std::string poserConge(Circuit& k, const std::string& trait1, const std::string& trait2, Nm rayon, std::string* raison = nullptr);
// Chanfrein de longueur « d » sur chaque trait
std::string poserChanfrein(Circuit& k, const std::string& trait1, const std::string& trait2, Nm d, std::string* raison = nullptr);
// Contour décalé de « d » autour des formes réunies : un polygone par îlot, sur la couche donnée
std::vector<DessinCircuit> contourDecale(const Polygones& formes, Nm d, Couche couche);
// Pastilles d'une empreinte posée, dans le repère de la carte
Polygones pastillesDe(const ConceptionElec& c, const std::string& composant);
// Points d'un arc (début, milieu, fin) discrétisé pour le dessin et les sorties
std::vector<Pt> pointsArc(Pt debut, Pt milieu, Pt fin, int segments = 32);

}  // namespace prom::ecad
