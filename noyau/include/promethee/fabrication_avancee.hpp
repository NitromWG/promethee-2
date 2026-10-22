// Prométhée : fabrication et cartes complexes (lot L8 ; FAB-xx, CAR-xx, ROU-xx du catalogue).
//
// Panneau : la carte répétée en lignes et colonnes dans un cadre, reliée par des languettes perforées (« mouse
// bites ») ; un modèle de fabrication complet (cuivre, vernis, sérigraphie, perçages, contour fraisé), contrôlé avant
// la sortie. Relecture des Gerber et des perçages écrits : le cuivre reconstruit, la connectivité relue comparée à
// celle de la conception, une piste coupée à la main dans un fichier signalée avant l'envoi. Impédance des pistes et
// des paires différentielles (IPC-2141), largeur et écart proposés pour l'empilage. Devis hors ligne d'après les
// grilles datées des profils de fabricants.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Panneau ----------
struct OptionsPanneau {
  int colonnes = 3, lignes = 2;
  double ecart = 2.0;           // mm fraisés entre deux cartes
  double rail = 5.0;            // mm de cadre autour
  double largeurLanguette = 3.0;  // mm
  int languettesParBord = 2;    // sur les bords haut et bas de chaque carte
  double diametreTrou = 0.5, pasTrous = 0.8;  // perforations (mm)
};
struct PanneauFabrication {
  ModeleFabrication modele;
  int languettes = 0, perforations = 0;
  std::vector<std::string> defauts;  // contrôles du panneau ; vide : il sort
};
PanneauFabrication panneauFabrication(const ConceptionElec& c, const OptionsPanneau& o, const std::string& version = {});

// ---------- Relecture des fichiers de fabrication ----------
struct ObjetGerber {
  enum class Genre : std::uint8_t { Trait, Flash, Region } genre = Genre::Trait;
  Polygone forme;                   // cuivre de l'objet (repère de fabrication, nm)
  std::string reference, numero;    // %TO.P en vigueur (pastille)
  std::string reseau;               // %TO.N en vigueur
  Pt a, b;                          // trait : extrémités ; flash : centre (dans a)
  Nm largeur = 0;                   // trait : largeur de l'ouverture
};
struct LectureGerber {
  std::vector<ObjetGerber> objets;
  std::vector<std::string> erreurs;
};
LectureGerber lireGerber(const std::string& texte);
std::vector<std::pair<Pt, Nm>> lirePercages(const std::string& texte);  // centre (repère de fabrication), diamètre
// Groupes de pastilles reliées par le cuivre (« R1.2 » …), relus des fichiers : cuivres (par couche) et perçages
// métallisés qui relient les couches
std::vector<std::vector<std::string>> groupesRelus(const std::vector<LectureGerber>& cuivres, const std::vector<std::pair<Pt, Nm>>& percages);
// Les mêmes groupes, calculés sur la conception (modèle de fabrication en mémoire)
std::vector<std::vector<std::string>> groupesConception(const ConceptionElec& c);
// Comparaison avant l'envoi : pastilles reliées dans la conception et séparées dans les fichiers (coupure), ou
// l'inverse (court-circuit) ; vide s'ils disent la même chose
std::vector<std::string> comparerFabrication(const ConceptionElec& c, const std::map<Couche, std::string>& gerbers, const std::string& percagesMetallises);

// ---------- Impédance (IPC-2141) ----------
struct Microruban {
  double h = 1.6;     // mm de diélectrique jusqu'au plan
  double t = 0.035;   // mm de cuivre
  double er = 4.5;    // permittivité relative
};
Microruban microrubanDe(const Empilage& e);  // couche externe, plan le plus proche
double impedanceMicroruban(double largeur, const Microruban& m);
double impedanceDifferentielle(double largeur, double ecart, const Microruban& m);
struct ProposPaire {
  double largeur = 0, ecart = 0, z0 = 0, zdiff = 0;  // mm, ohms
};
// Largeur et écart pour une paire de « zdiff » ohms sur l'empilage (écart égal à la largeur, au moins 0,15 mm)
ProposPaire proposerPaire(double zdiff, const Empilage& e);

// ---------- Devis hors ligne ----------
struct OffreFabrication {
  std::string fabricant, date;  // date de la grille de prix
  int quantite = 0;
  double prix = 0, delaiJours = 0;
  std::string devise = "EUR";
  std::vector<std::string> details;
};
std::vector<OffreFabrication> devis(const ConceptionElec& c, int quantite);

// ---------- Routage automatique complet ----------
// Le routeur d'appoint, puis le contournement pour chaque liaison restante, couche par couche ; rend le bilan
BilanRoutage routerComplet(ConceptionElec& c);

// ---------- Visionneuse HTML ----------
// Page autonome, sans réseau : carte en SVG (pastilles, contour, repères), nomenclature ; un repère cliqué s'allume
std::string visionneuseHtml(const ConceptionElec& c);

}  // namespace prom::ecad
