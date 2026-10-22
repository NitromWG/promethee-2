// Prométhée : encres et petits outils de dessin du canevas de schéma, communs au socle (VueSchema) et aux classes
// de domaine (VueSchemaElectrique). Licence GPL-3.0-only.
#pragma once

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPointF>
#include <QString>

#include "Theme.hpp"

namespace dessin_schema {
// Encres du dessin en cours : celles du thème à l'écran, le jeu clair à l'impression (prendreEncres)
inline QColor PAPIER, HORS, CADRE, ENCRE, FOND_CORPS, FIL, ALIM, CHOIX, RESEAU, GLOBALE, APERCU, GRILLE, CARTOUCHE, MARQUE, BROCHE, FOND_ETIQUETTE;
inline void prendreEncres(bool impression) {
  const EncresSchema& e = theme().schema(impression);
  PAPIER = e.papier, HORS = e.hors, CADRE = e.cadre, ENCRE = e.encre, FOND_CORPS = e.fondCorps, FIL = e.fil, ALIM = e.alim, CHOIX = e.choix;
  RESEAU = e.reseau, GLOBALE = e.globale, APERCU = e.apercu, GRILLE = e.grille, CARTOUCHE = e.cartouche, MARQUE = e.marque, BROCHE = e.broche;
  FOND_ETIQUETTE = e.fondEtiquette;
}
inline QColor alpha(QColor c, int a) {
  c.setAlpha(a);
  return c;
}
inline QPointF direction(int d) { return d == 0 ? QPointF(1, 0) : d == 1 ? QPointF(0, 1) : d == 2 ? QPointF(-1, 0) : QPointF(0, -1); }
// Texte posé sur un point d'ancrage : à gauche, à droite ou centré
inline void texte(QPainter& g, QPointF p, const QString& t, double taille, int alignement, const QColor& c) {
  if (t.isEmpty() || taille < 3) return;
  QFont f = g.font();
  f.setPixelSize(std::max(1, static_cast<int>(taille)));
  g.setFont(f);
  g.setPen(c);
  const QFontMetricsF m(f);
  const double l = m.horizontalAdvance(t);
  const double x = alignement < 0 ? p.x() : alignement > 0 ? p.x() - l : p.x() - l / 2;
  g.drawText(QPointF(x, p.y() + m.ascent() / 2 - m.descent() / 2), t);
}
}  // namespace dessin_schema
