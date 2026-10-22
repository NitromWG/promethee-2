// Prométhée : bande d'une fonction mince dans le plan de l'esquisse (interne à la bibliothèque de géométrie).
// Licence GPL-3.0-only.
//
// Région fermée (avec ses trous) : la paroi longe TOUS ses contours (FO-10), vers la matière (« interieur »),
// vers l'extérieur ou à cheval ; elle s'obtient par différence de la région et de sa version rétrécie ou élargie,
// ce qui garde chaque morceau quand un décalage se scinde (FO-21). Coins vifs par défaut (FO-24).
// Contour ouvert (chaîne de lignes et d'arcs) : décalage calculé en 2D, élément par élément, raccordé par
// intersection, fermé par deux segments droits ; « exterieur » est à gauche du sens de tracé (celui du premier
// élément dessiné, de son premier point vers le second), « interieur » à droite, « milieu » à cheval (FO-11).
#pragma once

#include <string>
#include <vector>

#include <TopoDS_Face.hxx>
#include <gp_Ax3.hxx>

#include "promethee/esquisse.hpp"

namespace prom::mince {

// Bande d'une région fermée (face plane de l'esquisse) ; une ou plusieurs faces.
bool bandeRegion(const TopoDS_Face& region, const gp_Ax3& ax, double epaisseur, const std::string& sens, bool arrondi, std::vector<TopoDS_Face>& faces,
                 std::string& erreur);

// Bande d'un contour ouvert : les éléments hors construction de l'esquisse doivent former une seule chaîne.
bool bandeOuverte(const Esquisse& e, const gp_Ax3& ax, double epaisseur, const std::string& sens, TopoDS_Face& face, std::string& erreur);

}  // namespace prom::mince
