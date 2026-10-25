// Prométhée : lecture commune des cartes d'autres logiciels (PADS, P-CAD, Altium, Autotrax, gEDA, Sprint-Layout…),
// en-tête interne au noyau : la carte lue (netlist des pastilles, empreintes par nom, poses, cuivre, contour, dessins)
// puis assemblée en conception, schéma de liaison compris ; réseaux tirés du cuivre pour les formats sans netlist.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "promethee/echanges_electronique.hpp"
#include "promethee/formats_cao.hpp"

namespace prom::ecad::cao_interne {

using Broche = std::pair<std::string, std::string>;  // (repère, numéro de pastille)

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
  std::vector<Polygone> decoupes;
  int cuivres = 2;
  // Posées sur la carte seule (pastilles libres, trous, logos) : sans symbole ; leur réseau vient du cuivre
  std::set<std::string> carteSeule;
  std::map<Broche, std::string> reseauxCarteSeule;
  std::vector<RegionCuivre> regions;  // cuivre plein dessiné (polygones de cuivre libres)
  std::vector<Zone> zones;
  std::vector<ZoneRegle> zonesRegles;
  std::vector<DessinCircuit> dessins;
  std::vector<TexteCircuit> textes;
};
// Empreintes rangées sous « prefixe » + nom, composants de la netlist reliés à leur pose, pièces sans réseau ou de la
// carte seule posées sur la carte, pistes, vias, régions, zones, dessins et textes, contour, puis réseaux
// synchronisés
ConceptionElec assemblerCarte(CarteLue& l, const std::string& nom, const std::string& prefixe, RapportLectureCao& r);

// Réseaux tirés du cuivre (cartes sans netlist : Autotrax, Sprint-Layout, gEDA sans liste) : îlots de cuivre
// connecté d'une même couche, pastilles, pistes, vias et régions ; un réseau « N$1 »… par îlot portant une pastille.
// « imposees » nomme le réseau de certains objets (plan de masse : objets posés sans isolement) et relie des
// pastilles sans cuivre entre elles (chevelu gardé par le fichier).
struct LiaisonsCuivre {
  std::map<std::string, std::vector<Broche>> pastilles;
  std::map<std::string, std::vector<std::size_t>> pistes, regions;
  std::vector<std::pair<Broche, Broche>> chevelu;
};
void reseauxDepuisCuivre(CarteLue& l, const LiaisonsCuivre& imposees = {});

// Contour de carte tiré de traits donnés dans le désordre (couche de contour, arcs déjà découpés) : le plus grand
// contour fermé en contour, les autres fermés en découpes ; faux si aucun ne se ferme
bool contourDepuisTraits(CarteLue& l, const std::vector<std::pair<Pt, Pt>>& traits);

// Texte d'un format Windows ancien en UTF-8 : gardé s'il est déjà en UTF-8, sinon lu en Windows-1252 (Europe de
// l'Ouest) ou Windows-1251 (cyrillique) selon ses caractères, comme le fait KiCad pour Sprint-Layout
std::string texteWindows(const std::string& octets);

}  // namespace prom::ecad::cao_interne
