// Prométhée : icônes vectorielles de l'interface. Licence GPL-3.0-only.
// Chaque icône est dessinée dans une grille de 24 × 24 unités, puis mise à l'échelle.
#include "Icones.hpp"

#include "Theme.hpp"

#include <cmath>
#include <functional>
#include <map>
#include <numbers>

#include <QHash>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {
// Couleurs du logo : encre marine, accent violet, bleu, fonds bleutés ; vert et rouge gardent leur sens (ajouter, ôter).
const QColor ENCRE(0x1A, 0x20, 0x3C), CUIVRE(0x5B, 0x4B, 0xF5), BLEU(0x18, 0x98, 0xF8), FOND(0xDD, 0xE5, 0xFF), VERT(0x2E, 0x8B, 0x57), ROUGE(0xC2, 0x40, 0x2E),
    PALE(0xE9, 0xEE, 0xFF);
const QColor CU_DESSUS(0xE8, 0x61, 0x3C), CU_DESSOUS(0x4F, 0x7B, 0xFF), ETAIN(0xF2, 0xC1, 0x4E), CYAN(0x0E, 0x94, 0xB8);
constexpr double PI = std::numbers::pi;

using Dessin = std::function<void(QPainter&)>;

void trait(QPainter& g, const QColor& c = ENCRE, double l = 1.6, Qt::PenStyle s = Qt::SolidLine) {
  QPen p(c, l, s, Qt::RoundCap, Qt::RoundJoin);
  g.setPen(p);
  g.setBrush(Qt::NoBrush);
}
void point(QPainter& g, double x, double y, const QColor& c = CUIVRE, double r = 1.7) {
  g.setPen(Qt::NoPen);
  g.setBrush(c);
  g.drawEllipse(QPointF(x, y), r, r);
}
void fleche(QPainter& g, QPointF a, QPointF b, const QColor& c = CUIVRE) {
  trait(g, c, 1.6);
  g.drawLine(a, b);
  const double ang = std::atan2(b.y() - a.y(), b.x() - a.x());
  QPolygonF t;
  t << b << b - QPointF(std::cos(ang - 0.45) * 4, std::sin(ang - 0.45) * 4) << b - QPointF(std::cos(ang + 0.45) * 4, std::sin(ang + 0.45) * 4);
  g.setPen(Qt::NoPen);
  g.setBrush(c);
  g.drawPolygon(t);
}
// Boîte en perspective : base (x, y) de largeur w, profondeur d (vers le haut à droite), hauteur h.
void boite(QPainter& g, double x, double y, double w, double h, double d, const QColor& face = FOND) {
  const QPointF a(x, y), b(x + w, y), c(x + w + d, y - d * 0.6), e(x + d, y - d * 0.6);
  QPolygonF dessus;
  dessus << a - QPointF(0, h) << b - QPointF(0, h) << c - QPointF(0, h) << e - QPointF(0, h);
  QPolygonF devant;
  devant << a << b << b - QPointF(0, h) << a - QPointF(0, h);
  QPolygonF cote;
  cote << b << c << c - QPointF(0, h) << b - QPointF(0, h);
  trait(g, ENCRE, 1.3);
  g.setBrush(face);
  g.drawPolygon(devant);
  g.setBrush(face.darker(112));
  g.drawPolygon(cote);
  g.setBrush(face.lighter(106));
  g.drawPolygon(dessus);
}
void arcDeg(QPainter& g, QPointF c, double r, double deb, double bal) {
  g.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), static_cast<int>(deb * 16), static_cast<int>(bal * 16));
}

const std::map<QString, Dessin>& dessins() {
  static const std::map<QString, Dessin> d = {
      // ---------- Esquisse ----------
      {"esquisse", [](QPainter& g) {
         trait(g, ENCRE, 1.3);
         g.setBrush(FOND);
         QPolygonF plan;
         plan << QPointF(2, 16) << QPointF(14, 20) << QPointF(22, 12) << QPointF(10, 8);
         g.drawPolygon(plan);
         trait(g, CUIVRE, 2);
         g.drawLine(QPointF(15, 3), QPointF(9, 13));
         point(g, 9, 13, CUIVRE, 1.4);
       }},
      {"selection", [](QPainter& g) {
         QPolygonF f;
         f << QPointF(6, 3) << QPointF(18, 13) << QPointF(12.5, 13.5) << QPointF(15, 20) << QPointF(12.5, 21) << QPointF(10, 14.8) << QPointF(6, 18);
         trait(g, ENCRE, 1.3);
         g.setBrush(Qt::white);
         g.drawPolygon(f);
       }},
      {"ligne", [](QPainter& g) { trait(g); g.drawLine(QPointF(4, 19), QPointF(20, 5)); point(g, 4, 19); point(g, 20, 5); }},
      {"ligne-construction", [](QPainter& g) { trait(g, ENCRE, 1.6, Qt::DashLine); g.drawLine(QPointF(4, 19), QPointF(20, 5)); point(g, 4, 19, ENCRE); point(g, 20, 5, ENCRE); }},
      {"ligne-milieu", [](QPainter& g) { trait(g); g.drawLine(QPointF(3, 18), QPointF(21, 6)); point(g, 12, 12, CUIVRE, 2.2); point(g, 3, 18, ENCRE, 1.3); point(g, 21, 6, ENCRE, 1.3); }},
      {"rectangle", [](QPainter& g) { trait(g); g.drawRect(QRectF(4, 6, 16, 12)); point(g, 4, 18); point(g, 20, 6); }},
      {"rectangle-centre", [](QPainter& g) {
         trait(g); g.drawRect(QRectF(4, 6, 16, 12));
         trait(g, ENCRE, 1, Qt::DashLine); g.drawLine(QPointF(4, 18), QPointF(20, 6));
         point(g, 12, 12); point(g, 20, 6, ENCRE, 1.3);
       }},
      {"rectangle-3points", [](QPainter& g) {
         trait(g);
         QPolygonF r;
         r << QPointF(3, 15) << QPointF(14, 4) << QPointF(21, 11) << QPointF(10, 22);
         g.drawPolygon(r);
         point(g, 3, 15); point(g, 14, 4); point(g, 21, 11);
       }},
      {"parallelogramme", [](QPainter& g) {
         trait(g);
         QPolygonF r;
         r << QPointF(2, 18) << QPointF(16, 18) << QPointF(22, 6) << QPointF(8, 6);
         g.drawPolygon(r);
         point(g, 2, 18); point(g, 16, 18); point(g, 22, 6);
       }},
      {"cercle", [](QPainter& g) { trait(g); g.drawEllipse(QPointF(12, 12), 8, 8); point(g, 12, 12); trait(g, ENCRE, 1, Qt::DashLine); g.drawLine(QPointF(12, 12), QPointF(19.5, 9)); }},
      {"cercle-3points", [](QPainter& g) { trait(g); g.drawEllipse(QPointF(12, 12), 8, 8); point(g, 4, 12); point(g, 16, 5.1); point(g, 17.6, 17.6); }},
      {"arc-centre", [](QPainter& g) { trait(g); arcDeg(g, QPointF(12, 15), 8, 0, 160); point(g, 12, 15); trait(g, ENCRE, 1, Qt::DashLine); g.drawLine(QPointF(12, 15), QPointF(20, 15)); }},
      {"arc-3points", [](QPainter& g) { trait(g); arcDeg(g, QPointF(12, 15), 8, 0, 180); point(g, 20, 15); point(g, 4, 15); point(g, 12, 7); }},
      {"arc-tangent", [](QPainter& g) { trait(g); g.drawLine(QPointF(3, 18), QPointF(12, 18)); trait(g, CUIVRE, 1.8); arcDeg(g, QPointF(12, 11), 7, 270, 140); point(g, 12, 18, ENCRE); }},
      {"rainure", [](QPainter& g) { trait(g); g.drawRoundedRect(QRectF(3, 8, 18, 8), 4, 4); point(g, 7, 12); point(g, 17, 12); }},
      {"polygone", [](QPainter& g) {
         trait(g);
         QPolygonF h;
         for (int i = 0; i < 6; ++i) h << QPointF(12 + 8.5 * std::cos(PI / 6 + i * PI / 3), 12 + 8.5 * std::sin(PI / 6 + i * PI / 3));
         g.drawPolygon(h);
         point(g, 12, 12);
       }},
      {"point", [](QPainter& g) { trait(g, ENCRE, 1.2); g.drawLine(QPointF(6, 12), QPointF(18, 12)); g.drawLine(QPointF(12, 6), QPointF(12, 18)); point(g, 12, 12, CUIVRE, 2.6); }},
      {"conge-esquisse", [](QPainter& g) {
         trait(g, ENCRE, 1, Qt::DashLine); g.drawLine(QPointF(4, 20), QPointF(4, 4)); g.drawLine(QPointF(4, 4), QPointF(20, 4));
         trait(g); g.drawLine(QPointF(4, 20), QPointF(4, 12)); g.drawLine(QPointF(12, 4), QPointF(20, 4));
         trait(g, CUIVRE, 2); arcDeg(g, QPointF(12, 12), 8, 90, 90);
       }},
      {"cote", [](QPainter& g) {
         trait(g, ENCRE, 1.1); g.drawLine(QPointF(4, 6), QPointF(4, 18)); g.drawLine(QPointF(20, 6), QPointF(20, 18));
         fleche(g, QPointF(12, 15), QPointF(5, 15), BLEU); fleche(g, QPointF(12, 15), QPointF(19, 15), BLEU);
         trait(g, BLEU, 1.6); g.drawLine(QPointF(9.5, 10), QPointF(14.5, 10)); g.drawLine(QPointF(12, 7.5), QPointF(12, 10));
       }},
      {"horizontal", [](QPainter& g) { trait(g, VERT, 2); g.drawLine(QPointF(3, 15), QPointF(21, 15)); trait(g, ENCRE, 1.6); g.drawLine(QPointF(8, 4), QPointF(8, 11)); g.drawLine(QPointF(15, 4), QPointF(15, 11)); g.drawLine(QPointF(8, 7.5), QPointF(15, 7.5)); }},
      {"vertical", [](QPainter& g) { trait(g, VERT, 2); g.drawLine(QPointF(17, 3), QPointF(17, 21)); trait(g, ENCRE, 1.6); g.drawLine(QPointF(4, 6), QPointF(8, 16)); g.drawLine(QPointF(12, 6), QPointF(8, 16)); }},
      {"coincident", [](QPainter& g) { trait(g); g.drawLine(QPointF(3, 18), QPointF(12, 12)); g.drawLine(QPointF(12, 12), QPointF(21, 16)); point(g, 12, 12, VERT, 3); }},
      {"contraintes", [](QPainter& g) {
         trait(g); g.drawLine(QPointF(4, 8), QPointF(20, 8)); g.drawLine(QPointF(4, 16), QPointF(20, 16));
         trait(g, VERT, 1.6); g.drawLine(QPointF(10, 10.5), QPointF(12, 13.5)); g.drawLine(QPointF(12, 10.5), QPointF(14, 13.5));
       }},
      {"construction", [](QPainter& g) { trait(g, ENCRE, 1.6, Qt::DashLine); g.drawEllipse(QPointF(12, 12), 8, 8); g.drawLine(QPointF(3, 21), QPointF(21, 3)); }},
      {"reference", [](QPainter& g) { trait(g, ENCRE, 1.1); g.drawLine(QPointF(4, 6), QPointF(4, 18)); g.drawLine(QPointF(20, 6), QPointF(20, 18)); trait(g, QColor(0x7C, 0x84, 0xA0), 1.6); g.drawLine(QPointF(5, 13), QPointF(19, 13)); g.drawArc(QRectF(7, 7, 4, 10), 90 * 16, 180 * 16); g.drawArc(QRectF(13, 7, 4, 10), -90 * 16, 180 * 16); }},
      {"supprimer", [](QPainter& g) {
         // Ciseaux : deux lames croisées et deux anneaux
         trait(g, ENCRE, 1.8);
         g.drawLine(QPointF(6, 3), QPointF(16.5, 15.5));
         g.drawLine(QPointF(18, 3), QPointF(7.5, 15.5));
         g.setBrush(Qt::white);
         trait(g, ROUGE, 1.8);
         g.drawEllipse(QPointF(6, 18.5), 3.2, 3.2);
         g.drawEllipse(QPointF(18, 18.5), 3.2, 3.2);
         point(g, 12, 9.2, ENCRE, 1.2);
       }},
      {"recadrer", [](QPainter& g) {
         trait(g, ENCRE, 1.8);
         for (const auto& [x, y, sx, sy] : {std::tuple{4, 4, 1, 1}, std::tuple{20, 4, -1, 1}, std::tuple{4, 20, 1, -1}, std::tuple{20, 20, -1, -1}}) {
           g.drawLine(QPointF(x, y), QPointF(x + 5 * sx, y)); g.drawLine(QPointF(x, y), QPointF(x, y + 5 * sy));
         }
         g.setBrush(FOND); trait(g, CUIVRE, 1.4); g.drawRect(QRectF(9, 9, 6, 6));
       }},
      // Loupe, avec + ou − : zoomer, dézoomer
      {"zoom-plus", [](QPainter& g) {
         trait(g, ENCRE, 1.8); g.setBrush(QColor(0xE9, 0xEE, 0xFF)); g.drawEllipse(QPointF(10, 10), 6.5, 6.5);
         trait(g, ENCRE, 2.6); g.drawLine(QPointF(15, 15), QPointF(20.5, 20.5));
         trait(g, CUIVRE, 1.8); g.drawLine(QPointF(7, 10), QPointF(13, 10)); g.drawLine(QPointF(10, 7), QPointF(10, 13));
       }},
      {"zoom-moins", [](QPainter& g) {
         trait(g, ENCRE, 1.8); g.setBrush(QColor(0xE9, 0xEE, 0xFF)); g.drawEllipse(QPointF(10, 10), 6.5, 6.5);
         trait(g, ENCRE, 2.6); g.drawLine(QPointF(15, 15), QPointF(20.5, 20.5));
         trait(g, CUIVRE, 1.8); g.drawLine(QPointF(7, 10), QPointF(13, 10));
       }},
      {"terminer", [](QPainter& g) { trait(g, Qt::white, 2.6); g.drawLine(QPointF(5, 13), QPointF(10, 18)); g.drawLine(QPointF(10, 18), QPointF(19, 6)); }},
      {"annuler", [](QPainter& g) { trait(g, ROUGE, 2.4); g.drawLine(QPointF(7, 7), QPointF(17, 17)); g.drawLine(QPointF(17, 7), QPointF(7, 17)); }},
      {"annuler-fleche", [](QPainter& g) {
         // Flèche courbe vers la gauche : arc de 200°, pointe pleine dans l'axe de l'arc
         QPainterPath arc;
         arc.moveTo(18.5, 17);
         arc.cubicTo(20.5, 11.5, 17, 6.5, 11.5, 6.5);
         arc.lineTo(7.5, 6.5);
         g.setPen(QPen(QColor(0x23, 0x2A, 0x45), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
         g.setBrush(Qt::NoBrush);
         g.drawPath(arc);
         QPolygonF t;
         t << QPointF(3, 6.5) << QPointF(8.5, 2.5) << QPointF(8.5, 10.5);
         g.setPen(QPen(QColor(0x23, 0x2A, 0x45), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
         g.setBrush(QColor(0x23, 0x2A, 0x45));
         g.drawPolygon(t);
       }},
      {"retablir-fleche", [](QPainter& g) {
         QPainterPath arc;
         arc.moveTo(5.5, 17);
         arc.cubicTo(3.5, 11.5, 7, 6.5, 12.5, 6.5);
         arc.lineTo(16.5, 6.5);
         g.setPen(QPen(QColor(0x23, 0x2A, 0x45), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
         g.setBrush(Qt::NoBrush);
         g.drawPath(arc);
         QPolygonF t;
         t << QPointF(21, 6.5) << QPointF(15.5, 2.5) << QPointF(15.5, 10.5);
         g.setPen(QPen(QColor(0x23, 0x2A, 0x45), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
         g.setBrush(QColor(0x23, 0x2A, 0x45));
         g.drawPolygon(t);
       }},
      {"valider", [](QPainter& g) {
         g.setPen(Qt::NoPen); g.setBrush(QColor(0x2E, 0x9E, 0x5B)); g.drawEllipse(QPointF(12, 12), 11, 11);
         trait(g, Qt::white, 2.8); g.drawLine(QPointF(6.5, 12.5), QPointF(10.5, 16.5)); g.drawLine(QPointF(10.5, 16.5), QPointF(17.5, 8));
       }},
      {"abandonner", [](QPainter& g) {
         g.setPen(QPen(QColor(0xE3, 0xC4, 0xBE), 1)); g.setBrush(Qt::white); g.drawEllipse(QPointF(12, 12), 11, 11);
         trait(g, ROUGE, 2.8); g.drawLine(QPointF(7.5, 7.5), QPointF(16.5, 16.5)); g.drawLine(QPointF(16.5, 7.5), QPointF(7.5, 16.5));
       }},
      // ---------- Métiers et documents ----------
      {"metier-meca", [](QPainter& g) {
         const QColor c(0x27, 0x60, 0xF1);
         trait(g, c, 2);
         g.drawEllipse(QPointF(12, 12), 4, 4);
         for (int k = 0; k < 8; ++k) {
           const double a = k * PI / 4;
           g.drawLine(QPointF(12 + 6.3 * std::cos(a), 12 + 6.3 * std::sin(a)), QPointF(12 + 9.6 * std::cos(a), 12 + 9.6 * std::sin(a)));
         }
         g.drawEllipse(QPointF(12, 12), 6.6, 6.6);
       }},
      {"metier-electronique", [](QPainter& g) {
         const QColor c(0x0E, 0x94, 0xB8);
         trait(g, c, 1.8);
         g.setBrush(QColor(0xDD, 0xF4, 0xFA));
         g.drawRoundedRect(QRectF(6.5, 6.5, 11, 11), 1.5, 1.5);
         for (int k = 0; k < 3; ++k) {
           const double p = 8.5 + k * 3.5;
           g.drawLine(QPointF(p, 2.5), QPointF(p, 6.5)); g.drawLine(QPointF(p, 17.5), QPointF(p, 21.5));
           g.drawLine(QPointF(2.5, p), QPointF(6.5, p)); g.drawLine(QPointF(17.5, p), QPointF(21.5, p));
         }
       }},
      {"metier-electrique", [](QPainter& g) {
         QPolygonF e;
         e << QPointF(13.5, 2) << QPointF(5, 13.5) << QPointF(11, 13.5) << QPointF(9.5, 22) << QPointF(19, 9.5) << QPointF(13, 9.5);
         g.setPen(QPen(QColor(0x9A, 0x5B, 0x00), 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
         g.setBrush(QColor(0xF2, 0xA9, 0x00));
         g.drawPolygon(e);
       }},
      {"metier-mixte", [](QPainter& g) {
         trait(g, CUIVRE, 1.6);
         g.setBrush(PALE);
         QPolygonF h; h << QPointF(12, 3) << QPointF(21, 7.5) << QPointF(12, 12) << QPointF(3, 7.5);
         g.drawPolygon(h);
         QPainterPath m; m.moveTo(3, 12); m.lineTo(12, 16.5); m.lineTo(21, 12); g.drawPath(m);
         QPainterPath b; b.moveTo(3, 16.5); b.lineTo(12, 21); b.lineTo(21, 16.5); g.drawPath(b);
       }},
      {"piece", [](QPainter& g) { boite(g, 4, 20, 11, 10, 6, QColor(0xDC, 0xE5, 0xFF)); }},
      {"assemblage", [](QPainter& g) { boite(g, 2, 21, 8, 7, 4, QColor(0xDC, 0xE5, 0xFF)); boite(g, 11, 15, 8, 8, 4, QColor(0xC4, 0xD8, 0xF0)); }},
      {"plan", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(Qt::white); g.drawRect(QRectF(4, 3, 16, 18));
         trait(g, QColor(0x2F, 0x5F, 0xD0), 1.2); g.drawRect(QRectF(7, 7, 7, 6)); g.drawLine(QPointF(7, 16), QPointF(14, 16));
         g.drawLine(QPointF(7, 15), QPointF(7, 17)); g.drawLine(QPointF(14, 15), QPointF(14, 17));
         trait(g, ENCRE, 1); g.drawRect(QRectF(15, 17, 5, 4));
       }},
      {"tolerie", [](QPainter& g) {
         trait(g, QColor(0x2F, 0x5F, 0xD0), 1.6); g.setBrush(QColor(0xDC, 0xE5, 0xFF));
         QPolygonF p; p << QPointF(3, 19) << QPointF(3, 9) << QPointF(6, 6) << QPointF(18, 6) << QPointF(21, 9) << QPointF(21, 19) << QPointF(19, 19) << QPointF(19, 9.8) << QPointF(17.2, 8) << QPointF(6.8, 8) << QPointF(5, 9.8) << QPointF(5, 19);
         g.drawPolygon(p);
       }},
      {"bord-tombe", [](QPainter& g) {
         // Tôle plate et son bord relevé, l'arête de pliage en couleur
         trait(g, QColor(0x2F, 0x5F, 0xD0), 1.4); g.setBrush(QColor(0xDC, 0xE5, 0xFF));
         QPolygonF p; p << QPointF(2, 19) << QPointF(14, 19) << QPointF(17, 16) << QPointF(5, 16); g.drawPolygon(p);
         QPolygonF q; q << QPointF(14, 19) << QPointF(17, 16) << QPointF(17, 5) << QPointF(14, 8); g.setBrush(QColor(0xB9, 0xCB, 0xFA)); g.drawPolygon(q);
         trait(g, CUIVRE, 1.8); g.drawLine(QPointF(14, 19), QPointF(17, 16));
         trait(g, ENCRE, 1.2); g.drawLine(QPointF(20, 6), QPointF(20, 12)); g.drawLine(QPointF(18.5, 7.5), QPointF(20, 6)); g.drawLine(QPointF(21.5, 7.5), QPointF(20, 6));
       }},
      {"nervure", [](QPainter& g) {
         // Équerre et son raidisseur triangulaire
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xC9, 0xCF, 0xD6));
         QPolygonF l; l << QPointF(3, 3) << QPointF(7, 3) << QPointF(7, 17) << QPointF(21, 17) << QPointF(21, 21) << QPointF(3, 21); g.drawPolygon(l);
         g.setBrush(CUIVRE); QPolygonF t; t << QPointF(7, 7) << QPointF(7, 17) << QPointF(17, 17); g.drawPolygon(t);
       }},
      {"filetage", [](QPainter& g) {
         // Vis : tige et filets en biais
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xC9, 0xCF, 0xD6)); g.drawRect(QRectF(8, 2, 8, 4));
         g.drawRect(QRectF(9.5, 6, 5, 16));
         trait(g, CUIVRE, 1.3); for (int k = 0; k < 5; ++k) g.drawLine(QPointF(8.5, 8 + k * 3), QPointF(15.5, 10 + k * 3));
       }},
      {"deplie", [](QPainter& g) {
         // Développé : une croix de tôle à plat et ses lignes de pliage en tirets
         trait(g, QColor(0x2F, 0x5F, 0xD0), 1.3); g.setBrush(QColor(0xDC, 0xE5, 0xFF));
         QPolygonF p; p << QPointF(8, 2) << QPointF(16, 2) << QPointF(16, 8) << QPointF(22, 8) << QPointF(22, 16) << QPointF(16, 16) << QPointF(16, 22) << QPointF(8, 22)
                        << QPointF(8, 16) << QPointF(2, 16) << QPointF(2, 8) << QPointF(8, 8);
         g.drawPolygon(p);
         QPen tirets(QColor(0xC2, 0x40, 0x2E), 1.1, Qt::DashLine); g.setPen(tirets);
         g.drawLine(QPointF(8, 8), QPointF(16, 8)); g.drawLine(QPointF(8, 16), QPointF(16, 16)); g.drawLine(QPointF(8, 8), QPointF(8, 16)); g.drawLine(QPointF(16, 8), QPointF(16, 16));
       }},
      {"pcb", [](QPainter& g) {
         g.setPen(QPen(QColor(0x14, 0x6B, 0x45), 1.2)); g.setBrush(QColor(0x2E, 0xA8, 0x6E)); g.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
         g.setBrush(QColor(0x1C, 0x22, 0x38)); g.setPen(Qt::NoPen); g.drawRect(QRectF(7, 9, 6, 6));
         trait(g, QColor(0xF2, 0xC2, 0x6B), 1.2); g.drawLine(QPointF(13, 12), QPointF(18, 12)); g.drawLine(QPointF(18, 12), QPointF(18, 8));
         g.setPen(Qt::NoPen); g.setBrush(QColor(0xF2, 0xC2, 0x6B)); g.drawEllipse(QPointF(18, 8), 1.4, 1.4); g.drawEllipse(QPointF(6, 16.5), 1.2, 1.2);
       }},
      {"schema", [](QPainter& g) {
         trait(g, ENCRE, 1.2); g.setBrush(Qt::white); g.drawRect(QRectF(3, 4, 18, 16));
         trait(g, QColor(0x0E, 0x94, 0xB8), 1.4);
         g.drawLine(QPointF(6, 12), QPointF(9, 12)); g.drawRect(QRectF(9, 10, 5, 4)); g.drawLine(QPointF(14, 12), QPointF(18, 12)); g.drawLine(QPointF(18, 12), QPointF(18, 16));
         g.drawLine(QPointF(16, 16), QPointF(20, 16)); g.drawLine(QPointF(17, 17.5), QPointF(19, 17.5));
       }},
      {"cablage", [](QPainter& g) {
         trait(g, QColor(0x9A, 0x5B, 0x00), 1.8); QPainterPath p; p.moveTo(3, 7); p.cubicTo(10, 7, 10, 17, 21, 17); g.drawPath(p);
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xF2, 0xA9, 0x00)); g.drawRect(QRectF(2, 5, 4, 4)); g.drawRect(QRectF(18, 15, 4, 4));
       }},
      {"armoire", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(QColor(0xF6, 0xE7, 0xC6)); g.drawRect(QRectF(5, 2, 14, 20));
         trait(g, QColor(0x9A, 0x5B, 0x00), 1.3); for (int k = 0; k < 3; ++k) g.drawLine(QPointF(7, 6 + k * 5), QPointF(17, 6 + k * 5));
       }},
      {"contacteur", [](QPainter& g) {
         // Bobine (rectangle) et contact à fermeture relié par la liaison mécanique
         trait(g, ENCRE, 1.4); g.setBrush(QColor(0xF6, 0xE7, 0xC6)); g.drawRect(QRectF(2.5, 9, 7, 6)); g.drawLine(QPointF(6, 4), QPointF(6, 9)); g.drawLine(QPointF(6, 15), QPointF(6, 20));
         trait(g, QColor(0x9A, 0x5B, 0x00), 1.6); g.drawLine(QPointF(17, 4), QPointF(17, 8)); g.drawLine(QPointF(17, 16), QPointF(17, 20)); g.drawLine(QPointF(17, 16), QPointF(13.5, 8.5));
         trait(g, ENCRE, 1.0, Qt::DashLine); g.drawLine(QPointF(9.5, 12), QPointF(15, 12));
       }},
      {"disjoncteur", [](QPainter& g) {
         trait(g, ENCRE, 1.6); g.drawLine(QPointF(12, 2), QPointF(12, 7)); g.drawLine(QPointF(12, 17), QPointF(12, 22)); g.drawLine(QPointF(12, 17), QPointF(7.5, 8));
         trait(g, ROUGE, 1.6); g.drawLine(QPointF(10, 5), QPointF(14, 9)); g.drawLine(QPointF(10, 9), QPointF(14, 5));
       }},
      {"borne", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(QColor(0xE4, 0xE8, 0xEE)); g.drawRect(QRectF(5, 4, 14, 16));
         for (int k = 0; k < 3; ++k) { g.setBrush(Qt::white); g.drawEllipse(QPointF(8.5 + k * 3.5, 9), 1.3, 1.3); g.drawLine(QPointF(8.5 + k * 3.5, 12), QPointF(8.5 + k * 3.5, 17)); }
         trait(g, QColor(0x9A, 0x5B, 0x00), 1.6); g.drawLine(QPointF(8.5, 20), QPointF(8.5, 23)); g.drawLine(QPointF(15.5, 1), QPointF(15.5, 4));
       }},
      {"potentiel", [](QPainter& g) {
         trait(g, CYAN, 2.0); g.drawLine(QPointF(2, 12), QPointF(9, 12));
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xFF, 0xF1, 0xD6)); QPolygonF p; p << QPointF(9, 12) << QPointF(13, 7.5) << QPointF(22, 7.5) << QPointF(22, 16.5) << QPointF(13, 16.5); g.drawPolygon(p);
         QFont f; f.setPixelSize(7); f.setBold(true); g.setFont(f); g.setPen(QColor(0x9A, 0x5B, 0x00)); g.drawText(QRectF(12, 7.5, 10, 9), Qt::AlignCenter, QStringLiteral("L1"));
       }},
      {"numeroter", [](QPainter& g) {
         trait(g, CYAN, 2.0); g.drawLine(QPointF(2, 16), QPointF(22, 16));
         g.setPen(Qt::NoPen); g.setBrush(CUIVRE); g.drawRoundedRect(QRectF(6, 4, 12, 8), 2, 2);
         QFont f; f.setPixelSize(7); f.setBold(true); g.setFont(f); g.setPen(Qt::white); g.drawText(QRectF(6, 4, 12, 8), Qt::AlignCenter, QStringLiteral("12"));
       }},
      {"moteur", [](QPainter& g) {
         trait(g, ENCRE, 1.5); g.setBrush(Qt::white); g.drawEllipse(QPointF(12, 13), 8, 8);
         QFont f; f.setPixelSize(9); f.setBold(true); g.setFont(f); g.setPen(ENCRE); g.drawText(QRectF(4, 7, 16, 9), Qt::AlignCenter, QStringLiteral("M"));
         trait(g, ENCRE, 1.3); g.drawLine(QPointF(12, 1), QPointF(12, 5));
       }},
      {"faisceau", [](QPainter& g) {
         trait(g, QColor(0x9A, 0x5B, 0x00), 2.2); g.drawLine(QPointF(3, 12), QPointF(13, 12)); g.drawLine(QPointF(13, 12), QPointF(21, 5)); g.drawLine(QPointF(13, 12), QPointF(21, 19));
         trait(g, ENCRE, 1.2); g.setBrush(QColor(0xF2, 0xA9, 0x00)); g.drawRect(QRectF(1, 9.5, 4, 5)); g.drawRect(QRectF(19.5, 2.5, 4, 5)); g.drawRect(QRectF(19.5, 16.5, 4, 5));
         point(g, 13, 12, ENCRE, 1.6);
       }},
      {"logement", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(QColor(0xFF, 0xF1, 0xD6)); QPolygonF m; m << QPointF(3, 11) << QPointF(12, 3) << QPointF(21, 11) << QPointF(19, 11) << QPointF(19, 21) << QPointF(5, 21) << QPointF(5, 11); g.drawPolygon(m);
         trait(g, QColor(0xC2, 0x6E, 0x00), 1.8); QPolygonF e; e << QPointF(13, 10) << QPointF(10, 15) << QPointF(13, 15) << QPointF(11, 19); g.drawPolyline(e);
       }},
      {"transformateur", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(Qt::NoBrush); g.drawEllipse(QPointF(12, 9), 5.2, 5.2); trait(g, CUIVRE, 1.4); g.drawEllipse(QPointF(12, 15), 5.2, 5.2);
         trait(g, ENCRE, 1.4); g.drawLine(QPointF(12, 1.5), QPointF(12, 3.8)); trait(g, CUIVRE, 1.4); g.drawLine(QPointF(12, 20.2), QPointF(12, 22.5));
       }},
      {"distribution", [](QPainter& g) {
         trait(g, ENCRE, 1.6); g.drawLine(QPointF(12, 2), QPointF(12, 8)); g.drawLine(QPointF(4, 8), QPointF(20, 8));
         for (double x : {4.0, 12.0, 20.0}) g.drawLine(QPointF(x, 8), QPointF(x, 14));
         g.setBrush(QColor(0xFF, 0xF1, 0xD6)); for (double x : {4.0, 12.0, 20.0}) g.drawRect(QRectF(x - 2.5, 14, 5, 6));
         point(g, 12, 8, CUIVRE, 1.6);
       }},
      {"eclairage", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xFF, 0xF1, 0xD6)); g.drawEllipse(QPointF(12, 10), 6, 6);
         trait(g, QColor(0xC2, 0x6E, 0x00), 1.4); g.drawLine(QPointF(8, 6), QPointF(16, 14)); g.drawLine(QPointF(16, 6), QPointF(8, 14));
         trait(g, ENCRE, 1.3); g.drawLine(QPointF(9.5, 18), QPointF(14.5, 18)); g.drawLine(QPointF(10, 20.5), QPointF(14, 20.5));
       }},
      {"prise", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(Qt::white); g.drawEllipse(QPointF(12, 12), 8.5, 8.5);
         g.setPen(Qt::NoPen); g.setBrush(ENCRE); g.drawEllipse(QPointF(8.5, 12), 1.6, 1.6); g.drawEllipse(QPointF(15.5, 12), 1.6, 1.6);
         g.setBrush(VERT); g.drawRect(QRectF(11, 3.5, 2, 3));
       }},
      {"chauffage", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xFF, 0xE4, 0xDA)); g.drawRoundedRect(QRectF(3, 6, 18, 13), 2, 2);
         trait(g, ROUGE, 1.4); for (int k = 0; k < 4; ++k) g.drawLine(QPointF(6.5 + k * 3.7, 8.5), QPointF(6.5 + k * 3.7, 16.5));
         trait(g, ENCRE, 1.3); g.drawLine(QPointF(6, 19), QPointF(6, 21)); g.drawLine(QPointF(18, 19), QPointF(18, 21));
       }},
      {"irve", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xE9, 0xEE, 0xFF)); g.drawRoundedRect(QRectF(5, 2.5, 10, 18), 2, 2);
         g.setBrush(Qt::white); g.drawRect(QRectF(7, 5, 6, 4));
         trait(g, VERT, 1.8); QPolygonF e; e << QPointF(11, 11) << QPointF(8.5, 15) << QPointF(11, 15) << QPointF(9, 19); g.drawPolyline(e);
         trait(g, ENCRE, 1.4); QPainterPath c; c.moveTo(15, 9); c.cubicTo(21, 9, 21, 17, 18, 21); g.drawPath(c);
       }},
      {"condensateur", [](QPainter& g) {
         trait(g, ENCRE, 1.6); g.drawLine(QPointF(12, 2), QPointF(12, 9.5)); g.drawLine(QPointF(12, 14.5), QPointF(12, 22));
         trait(g, CUIVRE, 2.2); g.drawLine(QPointF(5, 9.5), QPointF(19, 9.5)); g.drawLine(QPointF(5, 14.5), QPointF(19, 14.5));
       }},
      {"photovoltaique", [](QPainter& g) {
         g.setPen(Qt::NoPen); g.setBrush(ETAIN); g.drawEllipse(QPointF(18, 5.5), 3.2, 3.2);
         trait(g, ENCRE, 1.2); g.setBrush(QColor(0x2A, 0x4C, 0x9A)); QPolygonF m; m << QPointF(2, 20) << QPointF(6, 9) << QPointF(18, 9) << QPointF(16, 20); g.drawPolygon(m);
         trait(g, QColor(0xDD, 0xE5, 0xFF), 0.8); g.drawLine(QPointF(4, 14.5), QPointF(17, 14.5)); g.drawLine(QPointF(8.5, 9), QPointF(7, 20)); g.drawLine(QPointF(13, 9), QPointF(11.5, 20));
       }},
      {"aide", [](QPainter& g) {
         trait(g, CUIVRE, 1.6); g.setBrush(QColor(0xE9, 0xEE, 0xFF)); g.drawEllipse(QPointF(12, 12), 9, 9);
         trait(g, ENCRE, 1.8); QPainterPath c; c.moveTo(9, 9.5); c.cubicTo(9, 5.5, 15.5, 5.5, 15, 9.5); c.cubicTo(14.7, 11.5, 12, 12, 12, 14.5); g.drawPath(c);
         point(g, 12, 17.5, ENCRE, 1.2);
       }},
      {"grafcet", [](QPainter& g) {
         // Étape initiale (double carré), transition, étape suivante avec son action
         trait(g, ENCRE, 1.2); g.setBrush(Qt::white); g.drawRect(QRectF(5, 1.5, 9, 7)); g.drawRect(QRectF(6.4, 2.9, 6.2, 4.2));
         g.drawLine(QPointF(9.5, 8.5), QPointF(9.5, 15.5));
         trait(g, CUIVRE, 2.4); g.drawLine(QPointF(6, 12), QPointF(13, 12));
         trait(g, ENCRE, 1.2); g.setBrush(QColor(0xE9, 0xEE, 0xFF)); g.drawRect(QRectF(5, 15.5, 9, 7));
         g.drawLine(QPointF(14, 19), QPointF(16.5, 19)); g.setBrush(Qt::white); g.drawRect(QRectF(16.5, 16.5, 6, 5));
       }},
      {"simuler", [](QPainter& g) {
         // Étape active (jeton) et triangle de lecture
         trait(g, ENCRE, 1.2); g.setBrush(Qt::white); g.drawRect(QRectF(2.5, 3, 10, 10));
         g.setPen(Qt::NoPen); g.setBrush(QColor(0xC2, 0x40, 0x2E)); g.drawEllipse(QPointF(7.5, 10), 1.8, 1.8);
         g.setBrush(QColor(0x2E, 0x8B, 0x57)); QPolygonF t; t << QPointF(12, 11) << QPointF(22, 16.5) << QPointF(12, 22); g.drawPolygon(t);
       }},
      {"appareil-perso", [](QPainter& g) {
         // Appareil dessiné d'après ses bornes : cadre, bornes en haut et en bas, pastille « + »
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xE9, 0xEE, 0xFF)); g.drawRect(QRectF(3, 7, 13, 10));
         for (int k = 0; k < 3; ++k) { g.drawLine(QPointF(5.5 + k * 4, 3), QPointF(5.5 + k * 4, 7)); g.drawLine(QPointF(5.5 + k * 4, 17), QPointF(5.5 + k * 4, 21)); }
         g.setPen(Qt::NoPen); g.setBrush(QColor(0x2E, 0x8B, 0x57)); g.drawEllipse(QPointF(18.5, 16.5), 4.5, 4.5);
         trait(g, Qt::white, 1.6); g.drawLine(QPointF(16.2, 16.5), QPointF(20.8, 16.5)); g.drawLine(QPointF(18.5, 14.2), QPointF(18.5, 18.8));
       }},
      {"code", [](QPainter& g) {
         // Programme d'automate : feuille de texte structuré
         trait(g, ENCRE, 1.2); g.setBrush(QColor(0xFA, 0xFA, 0xF7)); g.drawRoundedRect(QRectF(3, 2.5, 18, 19), 2, 2);
         trait(g, QColor(0x1E, 0x7B, 0xE0), 1.6); g.drawLine(QPointF(6, 7), QPointF(12, 7)); g.drawLine(QPointF(8, 11), QPointF(17, 11));
         trait(g, CUIVRE, 1.6); g.drawLine(QPointF(8, 15), QPointF(14, 15)); trait(g, ENCRE, 1.4); g.drawLine(QPointF(6, 19), QPointF(11, 19));
       }},
      {"apprendre", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0x1A, 0x20, 0x3C)); QPolygonF t; t << QPointF(2, 9) << QPointF(12, 4) << QPointF(22, 9) << QPointF(12, 14); g.drawPolygon(t);
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xE9, 0xEE, 0xFF)); QPainterPath b; b.moveTo(6, 11); b.lineTo(6, 16); b.cubicTo(9, 19, 15, 19, 18, 16); b.lineTo(18, 11); g.drawPath(b);
         trait(g, CUIVRE, 1.4); g.drawLine(QPointF(21, 9.5), QPointF(21, 16)); point(g, 21, 17, CUIVRE, 1.3);
       }},
      {"calcul", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xE9, 0xEE, 0xFF)); g.drawRoundedRect(QRectF(5, 2, 14, 20), 2, 2);
         g.setBrush(Qt::white); g.drawRect(QRectF(7.5, 4.5, 9, 4));
         for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) point(g, 8.5 + c * 3.5, 12 + r * 3.3, r == 2 && c == 2 ? CUIVRE : ENCRE, 1.0);
       }},
      {"rapport", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(Qt::white); g.drawRect(QRectF(4, 3, 16, 18));
         trait(g, ENCRE, 1.0); for (int k = 0; k < 4; ++k) g.drawLine(QPointF(4, 7.5 + k * 3.5), QPointF(20, 7.5 + k * 3.5)); g.drawLine(QPointF(10, 3), QPointF(10, 21));
         g.setPen(Qt::NoPen); g.setBrush(QColor(0xC2, 0x6E, 0x00)); g.drawRect(QRectF(4.5, 3.5, 15, 3.5));
       }},
      {"composants", [](QPainter& g) {
         g.setPen(QPen(ENCRE, 1.2)); g.setBrush(QColor(0x1C, 0x22, 0x38)); g.drawRect(QRectF(4, 7, 9, 10));
         trait(g, ENCRE, 1.2); for (int k = 0; k < 3; ++k) { g.drawLine(QPointF(2, 9 + k * 3), QPointF(4, 9 + k * 3)); g.drawLine(QPointF(13, 9 + k * 3), QPointF(15, 9 + k * 3)); }
         g.setBrush(QColor(0xC9, 0x8A, 0x4B)); g.drawRect(QRectF(17, 10, 5, 3)); g.drawLine(QPointF(16, 11.5), QPointF(17, 11.5)); g.drawLine(QPointF(22, 11.5), QPointF(23, 11.5));
       }},
      {"dxf", [](QPainter& g) {
         // Feuille de dessin : un profil (ligne, arc) et une flèche d'entrée
         trait(g, ENCRE, 1.2); g.setBrush(QColor(0xFA, 0xFA, 0xF7)); QPolygonF f; f << QPointF(5, 2.5) << QPointF(15, 2.5) << QPointF(19.5, 7) << QPointF(19.5, 21.5) << QPointF(5, 21.5); g.drawPolygon(f);
         g.setBrush(Qt::NoBrush); trait(g, QColor(0x1E, 0x7B, 0xE0), 1.6); g.drawLine(QPointF(8, 17.5), QPointF(16, 17.5)); g.drawArc(QRectF(8, 9.5, 8, 8), 0, 180 * 16);
         trait(g, CUIVRE, 1.6); g.drawLine(QPointF(1.5, 12), QPointF(7, 12)); g.drawLine(QPointF(5, 10), QPointF(7, 12)); g.drawLine(QPointF(5, 14), QPointF(7, 12));
       }},
      {"couleur-face", [](QPainter& g) {
         // Cube dont la face de droite est peinte, et une goutte de couleur
         trait(g, ENCRE, 1.2); g.setBrush(QColor(0xE4, 0xE8, 0xEE));
         QPolygonF dessus; dessus << QPointF(3, 8) << QPointF(10, 4) << QPointF(17, 8) << QPointF(10, 12); g.drawPolygon(dessus);
         QPolygonF gauche; gauche << QPointF(3, 8) << QPointF(10, 12) << QPointF(10, 21) << QPointF(3, 17); g.drawPolygon(gauche);
         g.setBrush(QColor(0xE0, 0x6A, 0x3B)); QPolygonF droite; droite << QPointF(10, 12) << QPointF(17, 8) << QPointF(17, 17) << QPointF(10, 21); g.drawPolygon(droite);
         g.setBrush(QColor(0x1E, 0x7B, 0xE0)); trait(g, QColor(0x14, 0x55, 0x9E), 1.0);
         QPainterPath goutte; goutte.moveTo(20, 9); goutte.cubicTo(22.5, 13, 23, 15, 20, 15.5); goutte.cubicTo(17, 15, 17.5, 13, 20, 9); g.drawPath(goutte);
       }},
      {"importer-cao", [](QPainter& g) {
         // Boîte de pièces (assemblage) et flèche d'entrée
         trait(g, ENCRE, 1.2); g.setBrush(QColor(0xC9, 0xCF, 0xD6)); g.drawRect(QRectF(9, 12, 7, 9));
         g.setBrush(QColor(0x2E, 0x6B, 0x4F)); g.drawRect(QRectF(15, 6, 7, 15));
         g.setBrush(QColor(0xC2, 0x40, 0x2E)); g.drawEllipse(QPointF(12.5, 7.5), 3, 3);
         trait(g, CUIVRE, 1.6); g.drawLine(QPointF(1.5, 15), QPointF(7.5, 15)); g.drawLine(QPointF(5.5, 13), QPointF(7.5, 15)); g.drawLine(QPointF(5.5, 17), QPointF(7.5, 15));
       }},
      {"dossier", [](QPainter& g) { trait(g, ENCRE, 1.3); g.setBrush(QColor(0xF1, 0xE3, 0xC4)); QPolygonF p; p << QPointF(2, 7) << QPointF(9, 7) << QPointF(11, 9) << QPointF(22, 9) << QPointF(22, 20) << QPointF(2, 20); g.drawPolygon(p); }},
      {"accueil", [](QPainter& g) {
         trait(g, ENCRE, 1.8); g.setBrush(Qt::NoBrush);
         QPolygonF t; t << QPointF(3, 11.5) << QPointF(12, 3.5) << QPointF(21, 11.5); g.drawPolyline(t);
         g.drawRect(QRectF(6, 11, 12, 10)); g.setBrush(CUIVRE); g.setPen(Qt::NoPen); g.drawRect(QRectF(10.5, 15, 3.5, 6));
       }},
      {"ouvrir", [](QPainter& g) { trait(g, ENCRE, 1.3); g.setBrush(QColor(0xF1, 0xE3, 0xC4)); QPolygonF p; p << QPointF(2, 7) << QPointF(9, 7) << QPointF(11, 9) << QPointF(20, 9) << QPointF(20, 12) << QPointF(6, 12) << QPointF(3, 20) << QPointF(2, 20); g.drawPolygon(p); g.setBrush(QColor(0xE6, 0xC9, 0x8F)); QPolygonF q; q << QPointF(6, 12) << QPointF(22, 12) << QPointF(19, 20) << QPointF(3, 20); g.drawPolygon(q); }},
      {"kicad", [](QPainter& g) { g.setPen(Qt::NoPen); g.setBrush(QColor(0x31, 0x4C, 0xB0)); g.drawRoundedRect(QRectF(3, 3, 18, 18), 4, 4); trait(g, Qt::white, 2.2); g.drawLine(QPointF(9, 7), QPointF(9, 17)); g.drawLine(QPointF(9, 12), QPointF(15, 7)); g.drawLine(QPointF(10.5, 11), QPointF(15.5, 17)); }},
      {"symetrie", [](QPainter& g) {
         trait(g, QColor(0x6E, 0x9B, 0xFF), 1.4, Qt::DashLine); g.drawLine(QPointF(12, 2), QPointF(12, 22));
         trait(g, ENCRE, 1.6); QPolygonF a; a << QPointF(10, 6) << QPointF(3, 18) << QPointF(10, 18); g.drawPolygon(a);
         trait(g, CUIVRE, 1.6); QPolygonF b; b << QPointF(14, 6) << QPointF(21, 18) << QPointF(14, 18); g.drawPolygon(b);
       }},
      {"repetition-lineaire", [](QPainter& g) {
         trait(g, ENCRE, 1.5); g.drawRect(QRectF(2, 9, 5, 5));
         trait(g, CUIVRE, 1.5); g.drawRect(QRectF(9.5, 9, 5, 5)); g.drawRect(QRectF(17, 9, 5, 5));
         trait(g, ENCRE, 1, Qt::DotLine); g.drawLine(QPointF(2, 19), QPointF(22, 19));
       }},
      {"repetition-circulaire", [](QPainter& g) {
         trait(g, ENCRE, 1, Qt::DotLine); g.drawEllipse(QPointF(12, 12), 7.5, 7.5);
         for (int k = 0; k < 6; ++k) {
           const double a = k * PI / 3;
           trait(g, k == 0 ? ENCRE : CUIVRE, 1.4);
           g.setBrush(Qt::white);
           g.drawEllipse(QPointF(12 + 7.5 * std::cos(a), 12 + 7.5 * std::sin(a)), 2.4, 2.4);
         }
         point(g, 12, 12, ENCRE, 1.2);
       }},
      {"convertir", [](QPainter& g) {
         boite(g, 9, 11, 9, 6, 4, QColor(0xDC, 0xE5, 0xFF));
         trait(g, CUIVRE, 2); g.drawLine(QPointF(3, 21), QPointF(21, 21));
         fleche(g, QPointF(13.5, 12.5), QPointF(13.5, 19.5));
       }},
      {"calques", [](QPainter& g) {
         for (int k = 2; k >= 0; --k) {
           QPolygonF p; p << QPointF(12, 4 + k * 4.5) << QPointF(21, 8.5 + k * 4.5) << QPointF(12, 13 + k * 4.5) << QPointF(3, 8.5 + k * 4.5);
           trait(g, ENCRE, 1.2);
           g.setBrush(k == 0 ? QColor(0xEA, 0xE6, 0xFF) : k == 1 ? QColor(0xDC, 0xE5, 0xFF) : QColor(0xDF, 0xF3, 0xE9));
           g.drawPolygon(p);
         }
       }},
      {"filtre-auto", [](QPainter& g) {
         boite(g, 3, 20, 10, 9, 5, QColor(0xDC, 0xE5, 0xFF));
         QPolygonF f; f << QPointF(13, 8) << QPointF(21, 15) << QPointF(17.5, 15.3) << QPointF(19.5, 20) << QPointF(17.8, 20.7) << QPointF(15.8, 16.3) << QPointF(13, 19);
         trait(g, ENCRE, 1.1); g.setBrush(Qt::white); g.drawPolygon(f);
       }},
      {"filtre-faces", [](QPainter& g) {
         boite(g, 4, 20, 12, 10, 6, QColor(0xE8, 0xEB, 0xF4));
         QPolygonF dessus; dessus << QPointF(4, 10) << QPointF(16, 10) << QPointF(22, 6.4) << QPointF(10, 6.4);
         g.setPen(QPen(QColor(0x2F, 0x6B, 0xF5), 1.3)); g.setBrush(QColor(0x6E, 0x9B, 0xFF)); g.drawPolygon(dessus);
       }},
      {"filtre-aretes", [](QPainter& g) {
         boite(g, 4, 20, 12, 10, 6, QColor(0xE8, 0xEB, 0xF4));
         trait(g, QColor(0x2F, 0x6B, 0xF5), 3); g.drawLine(QPointF(16, 20), QPointF(16, 10));
       }},
      {"filtre-sommets", [](QPainter& g) {
         boite(g, 4, 20, 12, 10, 6, QColor(0xE8, 0xEB, 0xF4));
         point(g, 16, 10, QColor(0x2F, 0x6B, 0xF5), 3);
       }},
      {"afficher", [](QPainter& g) {
         QPainterPath oeil; oeil.moveTo(2, 12); oeil.cubicTo(7, 4, 17, 4, 22, 12); oeil.cubicTo(17, 20, 7, 20, 2, 12);
         trait(g, ENCRE, 1.6); g.setBrush(Qt::white); g.drawPath(oeil);
         g.setPen(Qt::NoPen); g.setBrush(QColor(0x2F, 0x6B, 0xF5)); g.drawEllipse(QPointF(12, 12), 3.6, 3.6);
         g.setBrush(ENCRE); g.drawEllipse(QPointF(12, 12), 1.6, 1.6);
       }},
      {"axe", [](QPainter& g) {
         trait(g, QColor(0x6A, 0x4C, 0xF5), 2, Qt::DashDotLine); g.drawLine(QPointF(4, 20), QPointF(20, 4));
         point(g, 4, 20, QColor(0x6A, 0x4C, 0xF5), 1.8); point(g, 20, 4, QColor(0x6A, 0x4C, 0xF5), 1.8);
       }},
      {"percage", [](QPainter& g) {
         boite(g, 2, 21, 14, 9, 6, QColor(0xDC, 0xE5, 0xFF));
         trait(g, ENCRE, 1.2); g.setBrush(Qt::white); g.drawEllipse(QPointF(10, 9.3), 3.2, 1.8);
         trait(g, CUIVRE, 1.6); g.drawLine(QPointF(10, 1), QPointF(10, 7)); g.drawLine(QPointF(8, 3), QPointF(12, 5)); g.drawLine(QPointF(8, 5), QPointF(12, 7));
       }},
      {"depouille", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xDC, 0xE5, 0xFF));
         QPolygonF p; p << QPointF(4, 20) << QPointF(20, 20) << QPointF(17, 5) << QPointF(7, 5); g.drawPolygon(p);
         trait(g, CUIVRE, 1.2, Qt::DashLine); g.drawLine(QPointF(4, 20), QPointF(4, 5)); g.drawLine(QPointF(20, 20), QPointF(20, 5));
         trait(g, QColor(0x6A, 0x4C, 0xF5), 1.6); g.drawLine(QPointF(1, 20), QPointF(23, 20));
       }},
      {"ajuster", [](QPainter& g) {
         // Ajuster : une ligne coupée net au croisement, le bout retiré en pointillé gris
         trait(g, ENCRE, 1.6); g.drawLine(QPointF(12, 3), QPointF(12, 21)); g.drawLine(QPointF(3, 12), QPointF(12, 12));
         trait(g, QColor(0xA0, 0xA8, 0xA4), 1.4, Qt::DotLine); g.drawLine(QPointF(13, 12), QPointF(21, 12));
         point(g, 12, 12, CUIVRE, 2);
       }},
      {"prolonger", [](QPainter& g) {
         trait(g, ENCRE, 1.6); g.drawLine(QPointF(19, 3), QPointF(19, 21)); g.drawLine(QPointF(3, 12), QPointF(10, 12));
         trait(g, CUIVRE, 1.6, Qt::DashLine); g.drawLine(QPointF(10, 12), QPointF(19, 12));
         fleche(g, QPointF(10, 12), QPointF(17, 12));
       }},
      {"decaler", [](QPainter& g) {
         trait(g, ENCRE, 1.6); g.drawRect(QRectF(7, 7, 10, 10));
         trait(g, CUIVRE, 1.6); g.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
       }},
      {"deplacer", [](QPainter& g) {
         fleche(g, QPointF(12, 12), QPointF(21, 12), ENCRE); fleche(g, QPointF(12, 12), QPointF(3, 12), ENCRE);
         fleche(g, QPointF(12, 12), QPointF(12, 3), ENCRE); fleche(g, QPointF(12, 12), QPointF(12, 21), ENCRE);
       }},
      {"pivoter", [](QPainter& g) {
         trait(g, ENCRE, 1.8); arcDeg(g, QPointF(12, 12), 8, 30, 280);
         QPolygonF t; t << QPointF(19.5, 5) << QPointF(21, 11.5) << QPointF(15, 9.5);
         g.setPen(Qt::NoPen); g.setBrush(ENCRE); g.drawPolygon(t); point(g, 12, 12, CUIVRE, 1.6);
       }},
      {"echelle", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.drawRect(QRectF(3, 12, 8, 8));
         trait(g, CUIVRE, 1.6); g.drawRect(QRectF(3, 3, 17, 17));
         fleche(g, QPointF(9, 14), QPointF(18, 5));
       }},
      {"balayage", [](QPainter& g) {
         trait(g, ENCRE, 1, Qt::DashLine); QPainterPath ch; ch.moveTo(4, 20); ch.cubicTo(8, 4, 16, 20, 20, 4); g.drawPath(ch);
         trait(g, CUIVRE, 1.6); g.setBrush(QColor(0xEA, 0xE6, 0xFF)); g.drawEllipse(QPointF(5, 19), 3, 2); g.drawEllipse(QPointF(19.5, 5), 3, 2);
       }},
      {"lissage", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(QColor(0xDC, 0xE5, 0xFF));
         QPolygonF p; p << QPointF(3, 20) << QPointF(21, 20) << QPointF(16, 5) << QPointF(8, 5); g.drawPolygon(p);
         trait(g, CUIVRE, 1.8); g.drawLine(QPointF(3, 20), QPointF(21, 20)); g.drawLine(QPointF(8, 5), QPointF(16, 5));
       }},
      {"coupe", [](QPainter& g) {
         boite(g, 3, 21, 12, 10, 6, QColor(0xDC, 0xE5, 0xFF));
         g.setPen(Qt::NoPen); g.setBrush(QColor(0xE0, 0x7B, 0x1E)); QPolygonF c; c << QPointF(9, 21) << QPointF(9, 11) << QPointF(15, 7.4) << QPointF(15, 17.4); g.drawPolygon(c);
         trait(g, QColor(0x6A, 0x4C, 0xF5), 1.4, Qt::DashLine); g.drawLine(QPointF(6, 23), QPointF(18, 4));
       }},
      {"parallele", [](QPainter& g) { trait(g, ENCRE, 2); g.drawLine(QPointF(4, 19), QPointF(13, 4)); g.drawLine(QPointF(11, 20), QPointF(20, 5)); }},
      {"perpendiculaire", [](QPainter& g) { trait(g, ENCRE, 2); g.drawLine(QPointF(12, 4), QPointF(12, 19)); g.drawLine(QPointF(4, 19), QPointF(20, 19)); trait(g, CUIVRE, 1.3); g.drawRect(QRectF(12, 15, 4, 4)); }},
      {"egal", [](QPainter& g) { trait(g, ENCRE, 2.2); g.drawLine(QPointF(5, 9.5), QPointF(19, 9.5)); g.drawLine(QPointF(5, 14.5), QPointF(19, 14.5)); }},
      {"concentrique", [](QPainter& g) { trait(g, ENCRE, 1.8); g.drawEllipse(QPointF(12, 12), 8.5, 8.5); trait(g, CUIVRE, 1.8); g.drawEllipse(QPointF(12, 12), 4.5, 4.5); point(g, 12, 12, ENCRE, 1.3); }},
      {"colineaire", [](QPainter& g) { trait(g, ENCRE, 1, Qt::DashLine); g.drawLine(QPointF(2, 17), QPointF(22, 7)); trait(g, ENCRE, 2.4); g.drawLine(QPointF(3, 16.5), QPointF(9, 13.5)); trait(g, CUIVRE, 2.4); g.drawLine(QPointF(14, 11), QPointF(21, 7.5)); }},
      {"fixe", [](QPainter& g) {
         trait(g, ENCRE, 1.6); g.drawLine(QPointF(12, 4), QPointF(12, 14));
         g.setBrush(CUIVRE); g.drawEllipse(QPointF(12, 5), 3, 3);
         trait(g, ENCRE, 1.6); g.drawLine(QPointF(5, 19), QPointF(19, 19));
         for (int k = 0; k < 4; ++k) g.drawLine(QPointF(6 + k * 4, 19), QPointF(4 + k * 4, 22));
         point(g, 12, 14.5, ENCRE, 1.6);
       }},
      {"symetrique", [](QPainter& g) {
         trait(g, QColor(0x6E, 0x9B, 0xFF), 1.3, Qt::DashLine); g.drawLine(QPointF(12, 2), QPointF(12, 22));
         point(g, 6, 12, ENCRE, 2.2); point(g, 18, 12, CUIVRE, 2.2);
         trait(g, ENCRE, 1); g.drawLine(QPointF(8, 12), QPointF(10.5, 12)); g.drawLine(QPointF(13.5, 12), QPointF(16, 12));
       }},
      {"gerer-relations", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.drawRoundedRect(QRectF(3, 4, 18, 16), 3, 3);
         trait(g, ENCRE, 1.6); g.drawLine(QPointF(7, 9), QPointF(17, 9)); g.drawLine(QPointF(7, 13), QPointF(14, 13)); g.drawLine(QPointF(7, 17), QPointF(12, 17));
         point(g, 18, 16, CUIVRE, 2.2);
       }},
      {"esquisse3d", [](QPainter& g) {
         trait(g, QColor(0x9A, 0xA0, 0xB8), 1, Qt::DotLine); g.drawLine(QPointF(4, 19), QPointF(20, 19)); g.drawLine(QPointF(4, 19), QPointF(4, 4)); g.drawLine(QPointF(4, 19), QPointF(13, 13));
         trait(g, CUIVRE, 2); QPainterPath c; c.moveTo(6, 17); c.cubicTo(9, 5, 15, 21, 20, 6); g.drawPath(c);
         point(g, 6, 17, ENCRE, 1.8); point(g, 20, 6, ENCRE, 1.8);
       }},
      // ---------- Fonctions ----------
      {"extrusion", [](QPainter& g) { boite(g, 3, 21, 12, 9, 6); fleche(g, QPointF(9, 10), QPointF(9, 2)); }},
      {"extrusion-milieu", [](QPainter& g) {
         boite(g, 3, 21, 12, 12, 6);
         trait(g, BLEU, 1.4, Qt::DashLine);
         QPolygonF p; p << QPointF(1, 15) << QPointF(17, 15) << QPointF(23, 11.4) << QPointF(7, 11.4);
         g.drawPolygon(p);
       }},
      {"extrusion-traversant", [](QPainter& g) { boite(g, 3, 21, 12, 9, 6); fleche(g, QPointF(9, 23), QPointF(9, 1)); }},
      {"extrusion-face", [](QPainter& g) {
         trait(g, BLEU, 1.6); g.drawLine(QPointF(1, 5), QPointF(23, 5));
         boite(g, 3, 21, 12, 13, 5); fleche(g, QPointF(9, 13), QPointF(9, 6.5));
       }},
      {"extrusion-mince", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(FOND);
         g.drawRect(QRectF(3, 6, 15, 15));
         g.setBrush(Qt::white); g.drawRect(QRectF(6, 9, 9, 12));
         fleche(g, QPointF(20.5, 18), QPointF(20.5, 4));
       }},
      {"enlevement", [](QPainter& g) {
         boite(g, 2, 21, 14, 9, 6);
         trait(g, ENCRE, 1.2); g.setBrush(QColor(0x9A, 0xA3, 0xBF)); g.drawEllipse(QPointF(12, 9.3), 4, 2.2);
         fleche(g, QPointF(12, 1.5), QPointF(12, 8));
       }},
      {"enlevement-traversant", [](QPainter& g) {
         boite(g, 2, 21, 14, 9, 6);
         trait(g, ENCRE, 1.2); g.setBrush(Qt::white); g.drawEllipse(QPointF(12, 9.3), 4, 2.2);
         fleche(g, QPointF(12, 1.5), QPointF(12, 23));
       }},
      {"enlevement-face", [](QPainter& g) {
         boite(g, 2, 21, 14, 11, 6);
         trait(g, ENCRE, 1.2); g.setBrush(QColor(0x9A, 0xA3, 0xBF)); g.drawEllipse(QPointF(12, 7.3), 4, 2.2);
         trait(g, BLEU, 1.6); g.drawLine(QPointF(1, 16), QPointF(23, 16));
         fleche(g, QPointF(12, 1), QPointF(12, 15));
       }},
      {"revolution", [](QPainter& g) {
         trait(g, ENCRE, 1, Qt::DashDotLine); g.drawLine(QPointF(12, 2), QPointF(12, 22));
         trait(g, ENCRE, 1.4); g.setBrush(FOND); g.drawRect(QRectF(14, 7, 5, 10));
         trait(g, CUIVRE, 1.8); arcDeg(g, QPointF(12, 12), 9, 200, 140);
         fleche(g, QPointF(3.8, 16.5), QPointF(4.5, 18.5));
       }},
      {"revolution-enlevement", [](QPainter& g) {
         trait(g, ENCRE, 1, Qt::DashDotLine); g.drawLine(QPointF(12, 2), QPointF(12, 22));
         trait(g, ENCRE, 1.4); g.setBrush(Qt::white); g.drawRect(QRectF(14, 7, 5, 10));
         trait(g, ROUGE, 1.8); arcDeg(g, QPointF(12, 12), 9, 200, 140);
       }},
      {"conge", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(FOND);
         QPainterPath p; p.moveTo(3, 21); p.lineTo(3, 9); p.cubicTo(3, 4, 6, 3, 11, 3); p.lineTo(21, 3); p.lineTo(21, 21); p.closeSubpath();
         g.drawPath(p);
         trait(g, CUIVRE, 2.2); QPainterPath a; a.moveTo(3, 10); a.cubicTo(3, 4.5, 6, 3, 10.5, 3); g.drawPath(a);
       }},
      {"chanfrein", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(FOND);
         QPolygonF p; p << QPointF(3, 21) << QPointF(3, 10) << QPointF(10, 3) << QPointF(21, 3) << QPointF(21, 21);
         g.drawPolygon(p);
         trait(g, CUIVRE, 2.2); g.drawLine(QPointF(3, 10), QPointF(10, 3));
       }},
      {"coque", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(FOND);
         QPolygonF ext; ext << QPointF(3, 7) << QPointF(3, 21) << QPointF(21, 21) << QPointF(21, 7) << QPointF(18, 7) << QPointF(18, 18) << QPointF(6, 18) << QPointF(6, 7);
         g.drawPolygon(ext);
         trait(g, CUIVRE, 1.4, Qt::DashLine); g.drawLine(QPointF(6, 7), QPointF(18, 7));
       }},
      {"plans", [](QPainter& g) {
         trait(g, BLEU, 1.2);
         g.setBrush(QColor(0x6F, 0x9F, 0xE8, 90)); QPolygonF a; a << QPointF(2, 16) << QPointF(12, 21) << QPointF(22, 16) << QPointF(12, 11); g.drawPolygon(a);
         g.setBrush(QColor(0x7C, 0xC4, 0x8E, 90)); QPolygonF b; b << QPointF(2, 16) << QPointF(2, 6) << QPointF(12, 1) << QPointF(12, 11); g.drawPolygon(b);
         g.setBrush(QColor(0xE8, 0x8F, 0x86, 90)); QPolygonF c; c << QPointF(12, 11) << QPointF(12, 1) << QPointF(22, 6) << QPointF(22, 16); g.drawPolygon(c);
       }},
      {"masse", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(FOND);
         QPolygonF p; p << QPointF(5, 21) << QPointF(19, 21) << QPointF(16, 9) << QPointF(8, 9);
         g.drawPolygon(p);
         g.drawEllipse(QPointF(12, 6), 2.6, 2.6);
         trait(g, CUIVRE, 1.6); g.drawLine(QPointF(10, 16), QPointF(14, 16));
       }},
      {"exporter", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(FOND); g.drawRect(QRectF(3, 10, 13, 11));
         fleche(g, QPointF(9.5, 15), QPointF(21, 3.5));
       }},
      {"nouvelle-piece", [](QPainter& g) { boite(g, 3, 21, 11, 10, 6, PALE); trait(g, VERT, 2.2); g.drawLine(QPointF(18, 3), QPointF(18, 11)); g.drawLine(QPointF(14, 7), QPointF(22, 7)); }},
      {"proprietes", [](QPainter& g) {
         trait(g, ENCRE, 1.5); g.drawEllipse(QPointF(12, 12), 3.4, 3.4);
         for (int i = 0; i < 8; ++i) { const double a = i * PI / 4; g.drawLine(QPointF(12 + 6 * std::cos(a), 12 + 6 * std::sin(a)), QPointF(12 + 9 * std::cos(a), 12 + 9 * std::sin(a))); }
         g.drawEllipse(QPointF(12, 12), 6.3, 6.3);
       }},
      // ---------- Électronique ----------
      {"fil", [](QPainter& g) {
         trait(g, CYAN, 2.0);
         QPolygonF p; p << QPointF(4, 18) << QPointF(12, 18) << QPointF(12, 6) << QPointF(20, 6); g.drawPolyline(p);
         g.setPen(QPen(ENCRE, 1.3)); g.setBrush(Qt::white); g.drawEllipse(QPointF(4, 18), 2.2, 2.2); g.drawEllipse(QPointF(20, 6), 2.2, 2.2);
       }},
      {"etiquette", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(QColor(0xF1, 0xEE, 0xFF));
         QPolygonF p; p << QPointF(2, 12) << QPointF(7, 7) << QPointF(21, 7) << QPointF(21, 17) << QPointF(7, 17); g.drawPolygon(p);
         trait(g, CUIVRE, 1.6); g.drawLine(QPointF(10, 10.5), QPointF(18, 10.5)); g.drawLine(QPointF(10, 13.5), QPointF(15, 13.5));
       }},
      {"annoter", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(Qt::white); g.drawRect(QRectF(4, 7, 9, 6)); g.drawLine(QPointF(1.5, 10), QPointF(4, 10)); g.drawLine(QPointF(13, 10), QPointF(15.5, 10));
         g.setPen(Qt::NoPen); g.setBrush(CUIVRE); g.drawEllipse(QPointF(17, 16), 5.2, 5.2);
         QFont f; f.setPixelSize(8); f.setBold(true); g.setFont(f); g.setPen(Qt::white); g.drawText(QRectF(11.8, 10.8, 10.4, 10.4), Qt::AlignCenter, QStringLiteral("1"));
       }},
      {"piste", [](QPainter& g) {
         trait(g, CU_DESSUS, 3.0); QPolygonF p; p << QPointF(5, 18) << QPointF(10, 18) << QPointF(16, 12) << QPointF(16, 6); g.drawPolyline(p);
         g.setPen(QPen(ENCRE, 1.2)); g.setBrush(ETAIN); g.drawEllipse(QPointF(5, 18), 2.8, 2.8); g.drawRect(QRectF(13.2, 2.4, 5.6, 5.6));
       }},
      {"via", [](QPainter& g) {
         trait(g, CU_DESSUS, 2.6); g.drawLine(QPointF(2, 12), QPointF(6, 12));
         trait(g, CU_DESSOUS, 2.6); g.drawLine(QPointF(18, 12), QPointF(22, 12));
         g.setPen(QPen(ENCRE, 1.2)); g.setBrush(ETAIN); g.drawEllipse(QPointF(12, 12), 7, 7);
         g.setBrush(Qt::white); g.drawEllipse(QPointF(12, 12), 3, 3);
       }},
      {"zone", [](QPainter& g) {
         QPolygonF p; p << QPointF(3, 5) << QPointF(21, 5) << QPointF(21, 19) << QPointF(9, 19) << QPointF(3, 13);
         g.setPen(QPen(CU_DESSOUS, 1.4)); g.setBrush(QColor(0x4F, 0x7B, 0xFF, 90)); g.drawPolygon(p);
         g.setPen(Qt::NoPen); g.setBrush(Qt::white); g.drawEllipse(QPointF(14, 12), 3.8, 3.8);
         g.setBrush(ETAIN); g.drawEllipse(QPointF(14, 12), 2.1, 2.1);
       }},
      {"croix", [](QPainter& g) {
         trait(g, QColor(0x6B, 0x73, 0x93), 2.2);
         g.drawLine(QPointF(7, 7), QPointF(17, 17));
         g.drawLine(QPointF(17, 7), QPointF(7, 17));
       }},
      // Un pictogramme par action de l'atelier électronique (2.15, S12) : copier, coller, dupliquer, couper et supprimer
      // ne se ressemblent plus ; une alimentation n'est pas une masse ; le texte n'est pas une cote ; ERC, DRC et
      // IPC-D-356 ont chacune le leur
      {"copier", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(PALE); g.drawRoundedRect(QRectF(3, 3, 11, 13), 1.5, 1.5);
         g.setBrush(Qt::white); g.drawRoundedRect(QRectF(9, 8, 11, 13), 1.5, 1.5);
         trait(g, CUIVRE, 1.2); g.drawLine(QPointF(12, 12), QPointF(17, 12)); g.drawLine(QPointF(12, 15), QPointF(17, 15)); g.drawLine(QPointF(12, 18), QPointF(15, 18));
       }},
      {"coller", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(FOND); g.drawRoundedRect(QRectF(4, 4, 14, 17), 1.5, 1.5);
         g.setBrush(ENCRE); g.drawRoundedRect(QRectF(8, 2.5, 6, 3.5), 1, 1);
         g.setBrush(Qt::white); g.drawRect(QRectF(10, 9, 11, 12));
         trait(g, CUIVRE, 1.2); g.drawLine(QPointF(12.5, 13), QPointF(18.5, 13)); g.drawLine(QPointF(12.5, 16), QPointF(18.5, 16));
       }},
      {"dupliquer", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(Qt::white); g.drawRoundedRect(QRectF(3, 6, 11, 12), 1.5, 1.5);
         trait(g, CUIVRE, 1.3, Qt::DashLine); g.drawRoundedRect(QRectF(9, 3, 11, 12), 1.5, 1.5);
         trait(g, VERT, 2.2); g.drawLine(QPointF(18, 15), QPointF(18, 22)); g.drawLine(QPointF(14.5, 18.5), QPointF(21.5, 18.5));
       }},
      {"couper", [](QPainter& g) {
         trait(g, ENCRE, 1.8);
         g.drawLine(QPointF(6, 3), QPointF(16.5, 15.5));
         g.drawLine(QPointF(18, 3), QPointF(7.5, 15.5));
         g.setBrush(Qt::white);
         trait(g, CUIVRE, 1.8);
         g.drawEllipse(QPointF(6.5, 18), 3.2, 3.2);
         g.drawEllipse(QPointF(17.5, 18), 3.2, 3.2);
       }},
      {"corbeille", [](QPainter& g) {
         trait(g, ENCRE, 1.4); g.setBrush(PALE);
         QPolygonF b; b << QPointF(6, 7) << QPointF(18, 7) << QPointF(16.6, 21) << QPointF(7.4, 21); g.drawPolygon(b);
         g.drawLine(QPointF(4, 7), QPointF(20, 7)); g.drawLine(QPointF(10, 4), QPointF(14, 4)); g.drawLine(QPointF(10, 4), QPointF(10, 7)); g.drawLine(QPointF(14, 4), QPointF(14, 7));
         trait(g, ROUGE, 1.3); g.drawLine(QPointF(10, 10), QPointF(10.4, 18)); g.drawLine(QPointF(14, 10), QPointF(13.6, 18));
       }},
      {"gomme", [](QPainter& g) {
         // Une gomme penchée, sa pointe rose sur un trait effacé
         trait(g, ENCRE, 1.3); g.setBrush(PALE);
         QPolygonF corps; corps << QPointF(9, 4) << QPointF(20, 15) << QPointF(14, 21) << QPointF(3, 10); g.drawPolygon(corps);
         g.setBrush(QColor(0xF4, 0xA6, 0xB8)); QPolygonF bout; bout << QPointF(6.5, 13.5) << QPointF(11.5, 18.5) << QPointF(14, 21) << QPointF(3, 10) << QPointF(3, 10); g.drawPolygon(bout);
         trait(g, ROUGE, 1.2, Qt::DashLine); g.drawLine(QPointF(15, 22), QPointF(22, 22));
       }},
      {"alimentation", [](QPainter& g) {
         trait(g, ROUGE, 2.0); g.drawLine(QPointF(12, 22), QPointF(12, 8)); g.drawLine(QPointF(5, 8), QPointF(19, 8));
         QFont f; f.setPixelSize(8); f.setBold(true); g.setFont(f); g.setPen(ROUGE); g.drawText(QRectF(13, 9.5, 10, 9), Qt::AlignCenter, QStringLiteral("+"));
       }},
      {"masse-terre", [](QPainter& g) {
         trait(g, ENCRE, 2.0); g.drawLine(QPointF(12, 3), QPointF(12, 12));
         g.drawLine(QPointF(4, 12), QPointF(20, 12)); g.drawLine(QPointF(7, 16), QPointF(17, 16)); g.drawLine(QPointF(10, 20), QPointF(14, 20));
       }},
      {"drapeau", [](QPainter& g) {
         trait(g, ENCRE, 1.6); g.drawLine(QPointF(6, 22), QPointF(6, 3));
         g.setPen(QPen(ROUGE, 1.2)); g.setBrush(QColor(0xFF, 0xD9, 0xD2)); QPolygonF d; d << QPointF(6, 4) << QPointF(19, 7.5) << QPointF(6, 11); g.drawPolygon(d);
         trait(g, CUIVRE, 1.6); g.drawLine(QPointF(3, 22), QPointF(10, 22));
       }},
      {"texte", [](QPainter& g) {
         trait(g, ENCRE, 2.0); g.drawLine(QPointF(5, 5), QPointF(19, 5)); g.drawLine(QPointF(12, 5), QPointF(12, 20));
         trait(g, CUIVRE, 1.4); g.drawLine(QPointF(9, 20), QPointF(15, 20));
       }},
      {"creer-composant", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(FOND); g.drawRect(QRectF(5, 7, 10, 13));
         for (int k = 0; k < 3; ++k) { g.drawLine(QPointF(2, 10 + 4 * k), QPointF(5, 10 + 4 * k)); g.drawLine(QPointF(15, 10 + 4 * k), QPointF(18, 10 + 4 * k)); }
         trait(g, VERT, 2.2); g.drawLine(QPointF(19, 2), QPointF(19, 9)); g.drawLine(QPointF(15.5, 5.5), QPointF(22.5, 5.5));
       }},
      {"erc", [](QPainter& g) {
         trait(g, ENCRE, 1.4); QPolygonF z; z << QPointF(2, 9) << QPointF(5, 9) << QPointF(6.5, 6) << QPointF(9.5, 12) << QPointF(12.5, 6) << QPointF(14, 9) << QPointF(17, 9); g.drawPolyline(z);
         g.setPen(Qt::NoPen); g.setBrush(VERT); g.drawEllipse(QPointF(16, 16), 6, 6);
         trait(g, Qt::white, 1.8); QPolygonF v; v << QPointF(13, 16) << QPointF(15.2, 18.2) << QPointF(19, 13.8); g.drawPolyline(v);
       }},
      {"drc", [](QPainter& g) {
         trait(g, CU_DESSUS, 2.6); g.drawLine(QPointF(2, 5), QPointF(14, 5)); g.drawLine(QPointF(2, 10), QPointF(14, 10));
         trait(g, ROUGE, 1.1); g.drawLine(QPointF(8, 6.5), QPointF(8, 8.5));
         g.setPen(Qt::NoPen); g.setBrush(VERT); g.drawEllipse(QPointF(16, 16), 6, 6);
         trait(g, Qt::white, 1.8); QPolygonF v; v << QPointF(13, 16) << QPointF(15.2, 18.2) << QPointF(19, 13.8); g.drawPolyline(v);
       }},
      {"ipc", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(PALE); g.drawRect(QRectF(2, 16, 20, 5));
         trait(g, QColor(0x6B, 0x73, 0x93), 1.4);
         for (double x : {6.0, 12.0, 18.0}) { g.drawLine(QPointF(x, 2), QPointF(x, 13)); }
         g.setPen(Qt::NoPen); g.setBrush(ETAIN); for (double x : {6.0, 12.0, 18.0}) g.drawEllipse(QPointF(x, 15), 1.8, 1.8);
       }},
      {"poser-copie", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(Qt::white); g.drawRect(QRectF(3, 8, 7, 9)); g.drawLine(QPointF(6.5, 5), QPointF(6.5, 8)); g.drawLine(QPointF(6.5, 17), QPointF(6.5, 20));
         trait(g, CUIVRE, 1.3, Qt::DashLine); g.drawRect(QRectF(14, 8, 7, 9));
         fleche(g, QPointF(10.5, 12.5), QPointF(13.5, 12.5), VERT);
       }},
      {"publier", [](QPainter& g) {
         trait(g, ENCRE, 1.3); g.setBrush(Qt::white);
         QPolygonF d; d << QPointF(4, 2.5) << QPointF(14, 2.5) << QPointF(18, 6.5) << QPointF(18, 21.5) << QPointF(4, 21.5); g.drawPolygon(d);
         trait(g, QColor(0x8A, 0x91, 0xAC), 1.1); for (int k = 0; k < 3; ++k) g.drawLine(QPointF(7, 8 + k * 3), QPointF(14, 8 + k * 3));
         g.setPen(Qt::NoPen); g.setBrush(CUIVRE); g.drawEllipse(QPointF(16.5, 17), 5.2, 5.2);
         trait(g, Qt::white, 1.7); QPolygonF v; v << QPointF(14.1, 17.1) << QPointF(16, 19) << QPointF(19, 15.3); g.drawPolyline(v);
       }},
  };
  return d;
}

// Pictogramme dessiné à la demande, dans le jeu clair d'origine. Avec un thème sombre, l'image dessinée est
// traduite pixel à pixel (Theme::pixel : traits d'encre éclaircis, remplissages pâles assombris, teintes
// gardées) et gardée en cache jusqu'au prochain changement de thème.
class MoteurIcone : public QIconEngine {
public:
  MoteurIcone(QString nom, Dessin d) : m_nom(std::move(nom)), m_dessin(std::move(d)) {}
  void paint(QPainter* g, const QRect& r, QIcon::Mode mode, QIcon::State) override {
    if (!theme().traduit()) {
      dessiner(*g, r, mode);
      return;
    }
    const qreal dpr = g->device() ? g->device()->devicePixelRatioF() : 1.0;
    g->drawImage(QRectF(r), imageTraduite((QSizeF(r.size()) * dpr).toSize(), mode));
  }
  QPixmap pixmap(const QSize& taille, QIcon::Mode mode, QIcon::State etat) override {
    QPixmap p(taille);
    p.fill(Qt::transparent);
    QPainter g(&p);
    paint(&g, QRect(QPoint(0, 0), taille), mode, etat);
    return p;
  }
  QIconEngine* clone() const override { return new MoteurIcone(m_nom, m_dessin); }

private:
  void dessiner(QPainter& g, const QRect& r, QIcon::Mode mode) const {
    g.save();
    g.setRenderHint(QPainter::Antialiasing);
    if (mode == QIcon::Disabled) g.setOpacity(0.35);
    const double s = std::min(r.width(), r.height()) / 24.0;
    g.translate(r.x() + (r.width() - 24 * s) / 2, r.y() + (r.height() - 24 * s) / 2);
    g.scale(s, s);
    m_dessin(g);
    g.restore();
  }
  QImage imageTraduite(QSize taille, QIcon::Mode mode) const {
    static QHash<QString, QImage> cache;
    static int generation = -1;
    if (generation != theme().generation()) {
      cache.clear();
      generation = theme().generation();
    }
    const QString cle = QStringLiteral("%1|%2|%3|%4").arg(m_nom).arg(taille.width()).arg(taille.height()).arg(static_cast<int>(mode));
    if (const auto it = cache.constFind(cle); it != cache.constEnd()) return *it;
    QImage image(taille.expandedTo(QSize(1, 1)), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    {
      QPainter g(&image);
      dessiner(g, QRect(QPoint(0, 0), image.size()), mode);
    }
    for (int y = 0; y < image.height(); ++y) {
      auto* ligne = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) ligne[x] = theme().pixel(ligne[x]);
    }
    if (cache.size() > 4000) cache.clear();
    cache.insert(cle, image);
    return image;
  }
  QString m_nom;
  Dessin m_dessin;
};
}  // namespace

QIcon icone(const QString& nom) {
  const auto& d = dessins();
  const auto it = d.find(nom);
  if (it == d.end()) return QIcon();
  return QIcon(new MoteurIcone(nom, it->second));
}

QIcon iconeMarque() {
  QIcon i;
  i.addFile(QStringLiteral(":/marque/icone-petite.png"), QSize(64, 64));
  i.addFile(QStringLiteral(":/marque/icone.png"), QSize(256, 256));
  return i;
}

QPixmap logoComplet(int largeur) {
  const QPixmap p(QStringLiteral(":/marque/logo-complet.png"));
  return p.isNull() ? p : p.scaledToWidth(largeur, Qt::SmoothTransformation);
}
