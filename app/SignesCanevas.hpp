// Prométhée : signes des canevas de l'atelier électronique (charte d'interaction, lot L1, tâche 1.7, S5). Sélection,
// surlignage d'un réseau et survol ont chacun leur forme, pas seulement leur couleur, pour rester lisibles par un
// daltonien et en noir et blanc : la sélection a un fond, un contour plein et des poignées ; le surlignage est un
// halo le long du cuivre ou des fils, sans cadre ni poignée, le reste de la vue s'éclaircissant ; le survol est un
// contour fin. Les marques de vérification ont aussi leur forme : disque (erreur), triangle (avertissement), cercle
// creux (information). Couleurs prises dans le thème (Theme::signesSchema, Theme::signesCircuit).
// Licence GPL-3.0-only.
#pragma once

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

#include "Theme.hpp"

namespace signes {

inline QColor avecAlpha(QColor c, double a) {
  c.setAlphaF(a);
  return c;
}

// Poignées carrées de 7 px, à l'échelle des signes du canevas (2.31)
inline void poignee(QPainter& g, QPointF p, const QColor& sel) {
  const double d = 3.5 * Theme::echelleCanevas();
  g.setPen(QPen(Qt::white, 1));
  g.setBrush(sel);
  g.drawRect(QRectF(p - QPointF(d, d), QSizeF(2 * d, 2 * d)));
}

// Objet choisi, vu par sa boîte : fond à 18 %, contour plein de 1,5 px, poignées aux coins
inline void selectionBoite(QPainter& g, const QRectF& r, const QColor& sel) {
  g.save();
  const QRectF b = r.normalized();
  g.setPen(QPen(sel, 1.5));
  g.setBrush(avecAlpha(sel, 0.18));
  g.drawRect(b);
  for (const QPointF& p : {b.topLeft(), b.topRight(), b.bottomLeft(), b.bottomRight()}) poignee(g, p, sel);
  g.restore();
}

// Objet choisi, vu par son contour (empreinte, zone, piste) : même fond, même contour, poignées sur les sommets
inline void selectionChemin(QPainter& g, const QPainterPath& chemin, const QColor& sel, bool poignees = true) {
  g.save();
  g.setPen(QPen(sel, 1.5));
  g.setBrush(avecAlpha(sel, 0.18));
  g.drawPath(chemin);
  if (poignees) {
    const QRectF b = chemin.boundingRect();
    for (const QPointF& p : {b.topLeft(), b.topRight(), b.bottomLeft(), b.bottomRight()}) poignee(g, p, sel);
  }
  g.restore();
}

// Trait choisi (fil, segment) : bande translucide, contour, poignées aux deux bouts
inline void selectionTrait(QPainter& g, QPointF a, QPointF b, double largeur, const QColor& sel) {
  g.save();
  g.setPen(QPen(avecAlpha(sel, 0.30), largeur + 6, Qt::SolidLine, Qt::RoundCap));
  g.drawLine(a, b);
  g.setPen(QPen(sel, 1.5, Qt::SolidLine, Qt::RoundCap));
  g.drawLine(a, b);
  poignee(g, a, sel);
  poignee(g, b, sel);
  g.restore();
}

// Surlignage d'un réseau : halo de 4 px de part et d'autre du trait ou du point
inline void haloTrait(QPainter& g, QPointF a, QPointF b, double largeur, const QColor& surl) {
  g.save();
  g.setPen(QPen(avecAlpha(surl, 0.55), largeur + 8, Qt::SolidLine, Qt::RoundCap));
  g.drawLine(a, b);
  g.restore();
}
inline void haloChemin(QPainter& g, const QPainterPath& chemin, const QColor& surl) {
  g.save();
  g.setPen(QPen(avecAlpha(surl, 0.55), 8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  g.setBrush(Qt::NoBrush);
  g.drawPath(chemin);
  g.restore();
}
inline void haloPoint(QPainter& g, QPointF p, double rayon, const QColor& surl) {
  g.save();
  g.setPen(Qt::NoPen);
  g.setBrush(avecAlpha(surl, 0.55));
  g.drawEllipse(p, rayon + 4, rayon + 4);
  g.restore();
}

// Survol : contour de 1 px, sans fond ni poignée
inline void survolBoite(QPainter& g, const QRectF& r, const QColor& survol) {
  g.save();
  g.setPen(QPen(survol, 1));
  g.setBrush(Qt::NoBrush);
  g.drawRect(r.normalized());
  g.restore();
}

// Marques de vérification : disque de 9 px (erreur), triangle de 10 px (avertissement), cercle creux de 9 px
// (information)
enum class Marque { Erreur, Avertissement, Information };
inline void marque(QPainter& g, QPointF p, Marque m, const QColor& c) {
  g.save();
  switch (m) {
    case Marque::Erreur:
      g.setPen(QPen(Qt::white, 1));
      g.setBrush(c);
      g.drawEllipse(p, 4.5, 4.5);
      break;
    case Marque::Avertissement: {
      QPolygonF t;
      t << p + QPointF(0, -5.5) << p + QPointF(5, 4) << p + QPointF(-5, 4);
      g.setPen(QPen(Qt::white, 1));
      g.setBrush(c);
      g.drawPolygon(t);
      break;
    }
    case Marque::Information:
      g.setPen(QPen(c, 1.6));
      g.setBrush(Qt::NoBrush);
      g.drawEllipse(p, 4.5, 4.5);
      break;
  }
  g.restore();
}

// Résultat à refaire ou désynchronisé : hachures grises à 45°, pas de 6 px, dans la zone
inline void hachures(QPainter& g, const QPainterPath& zone, const QColor& perime) {
  g.save();
  g.setClipPath(zone, Qt::IntersectClip);
  g.setPen(QPen(avecAlpha(perime, 0.8), 1));
  const QRectF b = zone.boundingRect();
  for (double x = b.left() - b.height(); x < b.right(); x += 6) g.drawLine(QPointF(x, b.bottom()), QPointF(x + b.height(), b.top()));
  g.restore();
}

}  // namespace signes
