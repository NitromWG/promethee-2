// Prométhée : images et vidéos de la carte (lot L6, tâche 6.18 ; MEC-14, MEC-31). Rendu par lancer de rayons sur le
// maillage OpenCascade de la carte équipée (plaque, cuivre, corps, pièces montées, boîtier importé) : matières
// (vernis mat, étain et cuivre brillants, corps), ombres, reflets, huit lumières au plus, fond transparent ; calculé
// hors écran, sur ses propres fils, interruptible à tout moment, avec sa progression. Images clés qui animent la
// caméra, la levée du boîtier et les pièces ; les images d'une animation sont rendues une à une.
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom {

struct LumiereRendu {
  double direction[3] = {-0.4, 0.5, -1};  // d'où vient la lumière vers la scène (repère 3D de la carte)
  double couleur[3] = {1, 1, 1};
  double intensite = 0.9;
};
struct OptionsRendu {
  int largeur = 1920, hauteur = 1080;
  ecad::PointDeVue vue;               // caméra : cible, azimut, élévation, distance, projection
  std::vector<LumiereRendu> lumieres;  // huit au plus ; vide : un éclairage de studio à trois lumières
  bool fondTransparent = true;
  std::uint32_t fond = 0xFFFFFF;       // fond opaque (RGB) quand il n'est pas transparent
  bool ombres = true, reflets = true;
  bool boitier = true;                 // le boîtier importé, s'il y en a un
  int echantillons = 1;                // par côté de pixel (2 : quatre rayons par pixel)
  int fils = 0;                        // 0 : autant que de cœurs
  double precision = 0.1;              // flèche du maillage (mm)
  // Animation : levée du boîtier importé (mm) et déplacement des pièces montées (uuid → décalage z en mm)
  double leveeBoitier = 0;
  std::map<std::string, double> leveePieces;
};
struct ImageRendu {
  int largeur = 0, hauteur = 0;
  std::vector<std::uint8_t> rgba;  // ligne par ligne, du haut vers le bas
  bool interrompu = false;
  int triangles = 0;
  double secondes = 0;
  bool alphaPresent() const;       // au moins un pixel transparent et un opaque
};
// « arret » : posé à vrai depuis un autre fil, le rendu s'arrête au plus tôt (image partielle, interrompu) ;
// « progres » reçoit la part faite (0 à 1), appelé depuis les fils du rendu
ImageRendu rendreCarte(const ecad::ConceptionElec& c, const OptionsRendu& o, const std::atomic<bool>* arret = nullptr, const std::function<void(double)>& progres = {});
// Point de vue qui cadre toute la carte, vue de trois quarts
ecad::PointDeVue vueDeTroisQuarts(const ecad::ConceptionElec& c);

// Images clés (6.18) : à l'instant t (secondes), la caméra, la levée du boîtier et celles des pièces ; l'animation
// interpole entre deux clés (angles par le plus court chemin)
struct ImageCle {
  double t = 0;
  ecad::PointDeVue vue;
  double leveeBoitier = 0;
  std::map<std::string, double> leveePieces;
};
struct EtatAnimation {
  ecad::PointDeVue vue;
  double leveeBoitier = 0;
  std::map<std::string, double> leveePieces;
};
EtatAnimation etatAnimation(const std::vector<ImageCle>& cles, double t);
double dureeAnimation(const std::vector<ImageCle>& cles);

}  // namespace prom
