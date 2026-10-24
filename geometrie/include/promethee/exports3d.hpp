// Prométhée : exports 3D de la carte équipée pour les visualiseurs et les autres logiciels (lot L10 de la 0.34 ;
// 10.10, 10.24 ; MEC-08). Les pièces nommées (carte, cuivre, composants, modèles exacts) sont maillées une fois, avec
// leurs couleurs par pièce et par face, puis écrites en GLB (glTF 2.0 binaire, lu par les navigateurs, Blender,
// Windows), VRML 2.0, STL binaire, COLLADA, AMF, script OpenSCAD ; XAO (géométrie BREP et groupe des faces de
// pastilles, pour la simulation) et STEP compressé (.stpZ, gzip) gardent la géométrie exacte. OBJ, PLY, 3MF et BREP
// restent dans echanges.hpp. Licence GPL-3.0-only.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "promethee/geometrie.hpp"

namespace prom {

enum class Format3D { GLB, VRML, STL, COLLADA, AMF, OpenSCAD, XAO, STPZ };
struct InfoFormat3D {
  Format3D format;
  const char* extension;  // « glb »
  const char* nom;        // « glTF binaire »
  const char* usage;      // une ligne pour le dialogue
};
const std::vector<InfoFormat3D>& formats3D();
// Contenu du fichier (octets) ; « fleche » : écart maximal entre la surface et son maillage (mm)
std::string texte3D(const std::vector<PieceNommee>& pieces, Format3D f, double fleche = 0.02);
void exporter3D(const std::vector<PieceNommee>& pieces, Format3D f, const std::string& chemin, double fleche = 0.02);

// Lecture de contrôle d'un GLB, indépendante de l'écrivain : en-tête, morceaux JSON et binaire, vues et accesseurs
// vérifiés contre la taille du tampon, boîte des positions
struct ResumeGlb {
  bool ok = false;
  std::string erreur;
  int maillages = 0, primitives = 0, materiaux = 0, noeuds = 0;
  std::size_t sommets = 0, triangles = 0;
  std::array<double, 3> min{0, 0, 0}, max{0, 0, 0};  // mètres, comme le veut glTF
  std::vector<std::string> nomsNoeuds;
};
ResumeGlb lireGlb(const std::string& octets);

}  // namespace prom
