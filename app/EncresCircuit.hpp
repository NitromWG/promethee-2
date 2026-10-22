// Prométhée : encres du canevas du circuit imprimé, communes à la vue et à ses outils (aperçus de piste, de zone,
// de contour). Licence GPL-3.0-only.
#pragma once

#include <QColor>
#include <QLocale>
#include <QString>

#include "Theme.hpp"
#include "promethee/circuit.hpp"

namespace encres_circuit {
// Les encres du thème (2.15, G9) : reprises par Theme à chaque application ; ces valeurs servent avant
inline QColor FOND(0x0B, 0x0F, 0x24), GRILLE(0x26, 0x2F, 0x5A), CARTE(0x12, 0x19, 0x3D), BORD(0xF2, 0xC1, 0x4E), TRAVERSANT(0xD9, 0xB6, 0x4A), VIA(0xC9, 0xCE, 0xD9),
    SERIG_DESSUS(0xF2, 0xF2, 0xF2), SERIG_DESSOUS(0xEC, 0xE6, 0xFF), CHEVELU(0xE8, 0xEC, 0xFF, 170), CHOIX(0xFF, 0xFF, 0xFF), ROUGE(0xFF, 0x4D, 0x4D);
inline QColor CUIVRE_DESSUS(0xE8, 0x61, 0x3C), CUIVRE_DESSOUS(0x4F, 0x7B, 0xFF), CUIVRE_INT1(0x1F, 0x9A, 0x68), CUIVRE_INT2(0xA9, 0x80, 0x1C);
inline void prendreEncres(const EncresCircuit& e) {
  FOND = e.fond, GRILLE = e.grille, CARTE = e.carte, BORD = e.bord, TRAVERSANT = e.traversant, VIA = e.via;
  SERIG_DESSUS = e.serigDessus, SERIG_DESSOUS = e.serigDessous, CHEVELU = e.chevelu, CHOIX = e.choix, ROUGE = e.rouge;
  CUIVRE_DESSUS = e.cuivreDessus, CUIVRE_DESSOUS = e.cuivreDessous, CUIVRE_INT1 = e.cuivreInt1, CUIVRE_INT2 = e.cuivreInt2;
}
inline QColor couleurCuivre(prom::ecad::Couche c) {
  using prom::ecad::Couche;
  switch (c) {
    case Couche::CuivreDessus: return CUIVRE_DESSUS;
    case Couche::CuivreDessous: return CUIVRE_DESSOUS;
    case Couche::CuivreInt1: return CUIVRE_INT1;
    case Couche::CuivreInt2: return CUIVRE_INT2;
    default: return VIA;
  }
}
inline QString mmTexte(double nmv) { return QLocale().toString(nmv * 1e-6, 'f', 3) + QStringLiteral(" mm"); }
}  // namespace encres_circuit
