// Prométhée : rendu de la carte en image, sans écran (lot L13 de la 0.34 ; 13.6 ; AUT-01, FAB-18). Le modèle de
// fabrication (cuivre, vernis, sérigraphie, perçages, contour) est tramé par le noyau, sur-échantillonné trois fois,
// aux couleurs d'une carte réelle (vernis, finition, sérigraphie) ; l'image s'écrit en PNG par le compresseur DEFLATE
// du noyau. La ligne de commande s'en sert sur une machine sans écran ni carte graphique. Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct ImageRgba {
  int largeur = 0, hauteur = 0;
  std::vector<std::uint8_t> pixels;  // RGBA, ligne par ligne depuis le haut
  std::uint32_t pixel(int x, int y) const;  // 0xRRGGBBAA
};
std::string encoderPng(const ImageRgba& i);
std::optional<ImageRgba> lirePng(const std::string& octets);  // 8 bits, RVB ou RVBA, sans entrelacement

struct OptionsRendu {
  double pxParMm = 12;
  bool dessous = false;               // vue de dessous (miroir)
  std::string finition = "ENIG";      // « ENIG » (doré), « HASL » (étain), « OSP » (cuivre)
  std::string vernis = "vert";        // « vert », « bleu », « rouge », « noir », « blanc », « violet »
  int maxPixels = 4000;               // plus grand côté
};
ImageRgba rendreCarte(const ConceptionElec& c, const OptionsRendu& o = {});
std::string pngCarte(const ConceptionElec& c, const OptionsRendu& o = {});

}  // namespace prom::ecad
