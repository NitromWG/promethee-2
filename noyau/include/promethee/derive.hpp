// Prométhée : grandeurs dérivées. Tout le boîtier découle de la carte et de ses composants.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/modele.hpp"

namespace prom {

struct RectArrondi { double hx = 0, hy = 0, r = 0; };

struct DecoupeMur {           // découpe dans une paroi, liée à un connecteur de bord
  Bord mur = Bord::W;
  double u = 0;               // position le long du mur (coordonnée monde)
  double zc = 0;              // hauteur du centre (coordonnée monde)
  double w = 0, h = 0, r = 0;
};

struct Percage { double x = 0, y = 0, d = 0; };   // perçage du couvercle (LED, bouton)

struct GeoComp {
  std::string id, ref, type;
  double h = 0, top = 0, w = 0, d = 0;
  std::optional<Bord> bord;
  double face = 0;            // connecteur de bord : coordonnée de sa face avant
  double x = 0, y = 0, hx = 0, hy = 0;
  int rot = 0;
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  std::optional<DecoupeMur> decoupe;
  std::optional<Percage> trouCouvercle;
  bool dessous = false;       // face arrière : occupe la hauteur sous la carte
  double bas = 0;             // face arrière : point le plus bas
};

// Forme réelle d'une carte libre, discrétisée pour les calculs de distance.
struct FormeCarte {
  std::vector<std::vector<Point>> polygones;  // [0] extérieur, puis découpes
  double sd(double x, double y) const;        // distance signée au bord (négative sur la carte)
  // Coordonnée du bord extrême rencontré par la droite x = u (bords N et S) ou y = u (bords E et W).
  double bordSelon(Bord b, double u, double defaut) const;
};

struct TrouPlace {
  std::string id, ref;
  double x = 0, y = 0;
  // Fixation propre à ce trou (vis du projet par défaut).
  std::string nomVis;
  Vis vis{};
  bool insert = false;
  double Rb = 0, rp = 0, rTete = 0, longInsert = 0;
  int longVis = 0;
};

struct Derive {
  double cx = 0, cy = 0;
  double Li = 0, Wi = 0, ri = 0;        // intérieur du boîtier
  double Lo = 0, Wo = 0, ro = 0;        // extérieur du boîtier
  double zf = 0, zpb = 0, zpt = 0;      // dessus du fond, dessous et dessus de la carte
  Vis vis{};
  double Rb = 0, rp = 0, rTete = 0;     // rayon des piliers, des avant-trous, garde de tête de vis
  RectArrondi lo, li;                   // contours extérieur et intérieur de la lèvre du couvercle
  double sb = 0, sa = 0;                // demi-longueurs droites des parois (sans les arrondis)
  std::vector<TrouPlace> trous;
  std::vector<GeoComp> comps;
  int longVis = 0;
  double H = 0, zt = 0, ztop = 0, zLevre = 0;
  // Forme de la carte : rectangle arrondi, ou contour quelconque (libre).
  bool libre = false;
  double jeu = 0;
  RectArrondi carteRR;
  FormeCarte forme;
  // Composants qui débordent d'une carte libre : le boîtier s'élargit autour d'eux.
  struct RectDebord { double x1, y1, x2, y2; };
  std::vector<RectDebord> debords;
  double sdEnveloppe(double x, double y) const;  // distance à la carte élargie de ses débords
  double sdCarte(double x, double y) const;     // distance signée au bord de la carte
  double sdCavite(double x, double y) const;    // à la face intérieure des parois
  double sdLevreInt(double x, double y) const;  // au bord intérieur de la lèvre du couvercle
  double bordCarte(Bord b, double u) const;     // position du bord de la carte au droit de u
};
// Une découpe de paroi doit tomber sur une partie droite du bord, loin des angles.
bool decoupeSurPartieDroite(const Derive& d, const DecoupeMur& o);

Derive deriver(const Projet& p);
double hauteurIdeale(const Projet& p, const Derive& d);
bool dansLevre(const Derive& d, const GeoComp& g);

// Éléments réellement construits (les cas en erreur sont écartés pour garder des pièces saines).
struct DecoupeValide { double s = 0, t = 0, w = 0, h = 0, r = 0; std::string id; };
struct PilierValide { double x = 0, y = 0; std::string id; double Rb = 0, rp = 0; };
struct PercageValide { double x = 0, y = 0, d = 0; std::string id; };
std::map<Bord, std::vector<DecoupeValide>> decoupesValides(const Derive& d);
std::vector<PilierValide> piliersValides(const Derive& d);
std::vector<PercageValide> percagesValides(const Derive& d);

}  // namespace prom
