// Prométhée : distances exactes de la carte équipée (lot L6, tâches 6.7 et 6.12 ; MEC-10, MEC-16, MEC-21). Les corps
// des composants (modèle exact ou boîte), les pièces montées et le boîtier du commerce importé en STEP, dans le
// repère 3D de la carte ; écarts mesurés sur les formes exactes par BRepExtrema d'OpenCascade, d'abord bornés par les
// boîtes englobantes. Deux contrôles de la DRC : DRC-47, un corps qui heurte le boîtier importé ou s'en approche sous
// son jeu ; DRC-75, deux corps plus proches qu'une contrainte « Écart 3D » (portées A et B sur les composants).
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include "promethee/electronique.hpp"
#include "promethee/mesure.hpp"

namespace prom {

struct CorpsCarte {
  enum class Genre : std::uint8_t { Composant, Piece, Boitier } genre = Genre::Composant;
  std::string id;   // UUID du composant, de la pièce ; « boitier » pour le boîtier importé
  std::string nom;  // repère du composant ou de la pièce, nom du boîtier
  TopoDS_Shape forme;
};
// Pièce montée (6.8) et boîtier importé (6.7) dans le repère 3D de la carte ; forme nulle si le modèle manque
TopoDS_Shape formePieceMontee(const ecad::ConceptionElec& c, const ecad::PieceMontee& m);
TopoDS_Shape formeBoitierImporte(const ecad::ConceptionElec& c);
// Tous les corps de la carte équipée : composants montés (hors « ne pas monter »), pièces, boîtier importé
std::vector<CorpsCarte> corpsCarte(const ecad::ConceptionElec& c);

// Distance exacte entre deux formes (0 si elles se touchent ou se recoupent) et ses deux points ; −1 si le calcul
// échoue. « borne » : au-delà, le calcul exact est sauté (boîtes trop loin) et la distance des boîtes rendue
double distanceExacte(const TopoDS_Shape& a, const TopoDS_Shape& b, gp_Pnt* pa = nullptr, gp_Pnt* pb = nullptr, double borne = -1);

// Mesure entre deux corps de la carte (6.12) : celle de l'atelier Pièces (distance minimale et ses points, ΔX, ΔY,
// ΔZ, centres), plus la distance de chacun à la carte (hauteur de son point le plus bas au-dessus de sa face)
struct MesureCorps {
  ResultatMesure mesure;
  double distanceCarteA = 0, distanceCarteB = 0;
};
MesureCorps mesurerCorps(const ecad::ConceptionElec& c, const std::string& idA, const std::string& idB);

// Contrôles exacts de la DRC (DRC-47, DRC-75), lancés sur le fil du calcul de fond
std::vector<ecad::Violation> verifierDistances3D(const ecad::ConceptionElec& c);

}  // namespace prom
