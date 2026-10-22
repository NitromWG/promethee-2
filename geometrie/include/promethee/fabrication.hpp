// Prométhée : vérification de fabrication d'une pièce. Repère ce qui sera difficile ou impossible à produire :
// parois trop minces, petits rayons, trous trop profonds pour leur diamètre, angles intérieurs vifs (une fraise
// laisse toujours un rayon). Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace prom {

struct AvisFabrication {
  int niveau = 1;                    // 0 : à savoir, 1 : à corriger
  std::string message;
  std::vector<TopoDS_Shape> formes;  // faces ou arêtes concernées (pour les montrer)
};

struct SeuilsFabrication {
  double paroiMin = 1.0;      // mm
  double rayonMin = 0.5;      // mm
  double profondeurMax = 8;   // longueur d'un trou / son diamètre
};

std::vector<AvisFabrication> verifierFabrication(const TopoDS_Shape& piece, const SeuilsFabrication& s = {});

}  // namespace prom
