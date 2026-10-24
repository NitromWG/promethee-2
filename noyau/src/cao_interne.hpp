// Prométhée : lecture commune des cartes d'autres logiciels (PADS, P-CAD, Altium…), en-tête interne au noyau : la
// carte lue (netlist des pastilles, empreintes par nom, poses, cuivre, contour) puis assemblée en conception, schéma
// de liaison compris. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "promethee/echanges_electronique.hpp"
#include "promethee/formats_cao.hpp"

namespace prom::ecad::cao_interne {

struct CarteLue {
  NetlistLue netlist;
  std::map<std::string, DefEmpreinte> empreintes;  // nom du decal → définition
  struct Pose {
    std::string ref, decal;
    Pt p;
    double rotation = 0;
    bool dessous = false;
  };
  std::vector<Pose> poses;
  std::vector<Piste> pistes;
  std::vector<Via> vias;
  Polygone contour;
  int cuivres = 2;
};
// Empreintes rangées sous « prefixe » + nom, composants de la netlist reliés à leur pose, pièces sans réseau posées
// sur la carte seule, pistes et vias, contour, puis réseaux synchronisés
ConceptionElec assemblerCarte(CarteLue& l, const std::string& nom, const std::string& prefixe, RapportLectureCao& r);

}  // namespace prom::ecad::cao_interne
