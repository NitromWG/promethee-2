// Prométhée : vue d'un grafcet. Voir VueGrafcet.hpp. Licence GPL-3.0-only.
#include "VueGrafcet.hpp"

#include <algorithm>
#include <cmath>

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace prom::elec;

namespace {
const QColor JETON(0xC2, 0x40, 0x2E);
const QColor FRANCHISSABLE(0x2E, 0x8B, 0x57);
const QColor CHOIX(0x5B, 0x4B, 0xF5);
const QColor PAPIER(0xFF, 0xFF, 0xFF);
const QColor ENCRE_VUE(0x10, 0x14, 0x2A);
const QColor DISCRET_VUE(0x6B, 0x73, 0x93);
}  // namespace

VueGrafcet::VueGrafcet(QWidget* parent) : QWidget(parent) {
  setMinimumSize(200, 200);
  setMouseTracking(false);
  setFocusPolicy(Qt::ClickFocus);
}

void VueGrafcet::setGrafcet(const Grafcet* g) {
  m_vide = !g || g->etapes.empty();
  m_d = g ? disposerGrafcet(*g) : DispositionGrafcet{};
  cadrer();
  update();
}

void VueGrafcet::setChoix(int etape, int transition) {
  m_etape = etape;
  m_transition = transition;
  update();
}

void VueGrafcet::setSimulation(bool active, const std::set<int>& actives, const std::set<std::size_t>& franchissables) {
  m_marques.simulation = active;
  m_marques.actives = active ? actives : std::set<int>{};
  m_marques.franchissables = active ? franchissables : std::set<std::size_t>{};
  update();
}

void VueGrafcet::cadrer() {
  if (m_d.largeur <= 0 || m_d.hauteur <= 0) return;
  const double l = std::max(50.0, static_cast<double>(width()) - 48), h = std::max(50.0, static_cast<double>(height()) - 48);
  m_echelle = std::clamp(std::min(l / m_d.largeur, h / m_d.hauteur), 1.2, 7.0);
  m_origine = QPointF(std::max(24.0, (width() - m_d.largeur * m_echelle) / 2), 24);
}

void VueGrafcet::resizeEvent(QResizeEvent*) { cadrer(); }

QPointF VueGrafcet::versEcran(double xMm, double yMm) const { return m_origine + QPointF(xMm, yMm) * m_echelle; }

std::pair<int, int> VueGrafcet::sous(double x, double y) const {
  // La plus petite zone sous le point : une action plutôt que l'étape qui la porte n'a pas de sens, l'étape suffit
  for (const auto& z : m_d.zones)
    if (x >= z.x0 - 1 && x <= z.x1 + 1 && y >= z.y0 - 1 && y <= z.y1 + 1) return {z.etape, z.transition};
  return {-1, -1};
}

void VueGrafcet::cliquerEn(double xMm, double yMm, bool double_) {
  const auto [e, t] = sous(xMm, yMm);
  m_etape = e;
  m_transition = t;
  update();
  if (double_) emit doubleClic(e, t);
  else emit choisi(e, t);
}

void VueGrafcet::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const QPointF p = (e->position() - m_origine) / m_echelle;
  cliquerEn(p.x(), p.y());
}

void VueGrafcet::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const QPointF p = (e->position() - m_origine) / m_echelle;
  cliquerEn(p.x(), p.y(), true);
}

void VueGrafcet::keyPressEvent(QKeyEvent* e) {
  // Suppr : l'élément choisi ; Échap : plus rien de choisi
  if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && (m_etape >= 0 || m_transition >= 0)) {
    emit supprimerDemande();
    return;
  }
  if (e->key() == Qt::Key_Escape) {
    cliquerEn(-1000, -1000);
    return;
  }
  QWidget::keyPressEvent(e);
}

void VueGrafcet::paintEvent(QPaintEvent*) {
  QPainter g(this);
  g.setRenderHint(QPainter::Antialiasing);
  g.fillRect(rect(), palette().window());
  if (m_vide) {
    g.setPen(palette().text().color());
    g.drawText(rect().adjusted(30, 30, -30, -30), Qt::AlignCenter | Qt::TextWordWrap,
               QStringLiteral("Pas encore de grafcet.\nCrée-en un depuis le ruban (vierge ou un exemple de cours) : l’étape initiale 0 apparaît ici,\n"
                              "puis « Étape suivante » ajoute l’étape et sa transition."));
    return;
  }
  // Feuille blanche sous le dessin
  const QRectF feuille(m_origine - QPointF(10, 10), QSizeF(m_d.largeur * m_echelle + 20, m_d.hauteur * m_echelle + 20));
  g.setPen(Qt::NoPen);
  g.setBrush(PAPIER);
  g.drawRoundedRect(feuille, 8, 8);
  Marques m = m_marques;
  m.etape = m_etape;
  m.transition = m_transition;
  peindre(g, m_d, m_origine, m_echelle, ENCRE_VUE, DISCRET_VUE, m);
}

void VueGrafcet::peindre(QPainter& g, const DispositionGrafcet& d, QPointF o, double k, const QColor& encre, const QColor& discret, const Marques& m) {
  g.save();
  auto P = [&](double x, double y) { return o + QPointF(x, y) * k; };
  const double fin = std::max(1.0, 0.25 * k);
  // Choix : un halo sous l'élément désigné
  for (const auto& z : d.zones) {
    const bool choisi = (m.etape >= 0 && z.etape == m.etape && z.transition < 0) || (m.transition >= 0 && z.transition == m.transition);
    if (!choisi) continue;
    g.setPen(QPen(CHOIX, std::max(1.0, 0.3 * k), Qt::DashLine));
    QColor c = CHOIX;
    c.setAlpha(28);
    g.setBrush(c);
    g.drawRoundedRect(QRectF(P(z.x0 - 1.2, z.y0 - 1.2), P(z.x1 + 1.2, z.y1 + 1.2)), 3, 3);
  }
  // Liaisons et transitions
  for (std::size_t i = 0; i < d.traits.size(); ++i) {
    const TraitGrafcet& t = d.traits[i];
    QColor c = encre;
    double epaisseur = t.epais ? std::max(2.0, 1.0 * k) : fin;
    if (t.epais && m.simulation && t.transition >= 0 && m.franchissables.count(static_cast<std::size_t>(t.transition))) {
      // Transition franchissable : sa barre en vert
      c = FRANCHISSABLE;
      epaisseur *= 1.4;
    }
    g.setPen(QPen(c, epaisseur, Qt::SolidLine, t.epais ? Qt::FlatCap : Qt::SquareCap));
    g.drawLine(P(t.x0, t.y0), P(t.x1, t.y1));
    if (t.fleche) {
      const QPointF a = P(t.x0, t.y0), b = P(t.x1, t.y1), mid = (a + b) / 2;
      const double ang = std::atan2(b.y() - a.y(), b.x() - a.x()), s = std::max(4.0, 1.6 * k);
      QPolygonF tri;
      tri << mid + QPointF(std::cos(ang), std::sin(ang)) * s << mid + QPointF(std::cos(ang + 2.6), std::sin(ang + 2.6)) * s * 0.8
          << mid + QPointF(std::cos(ang - 2.6), std::sin(ang - 2.6)) * s * 0.8;
      g.setPen(Qt::NoPen);
      g.setBrush(encre);
      g.drawPolygon(tri);
    }
  }
  // Étapes et actions
  for (const auto& b : d.boites) {
    const bool active = b.etape >= 0 && m.actives.count(b.etape) > 0;
    g.setPen(QPen(encre, fin));
    QColor fond = Qt::white;
    if (active) fond = QColor(0xFF, 0xE8, 0xE3);
    g.setBrush(fond);
    g.drawRect(QRectF(P(b.x, b.y), P(b.x + b.l, b.y + b.h)));
    if (b.double_) {
      g.setBrush(Qt::NoBrush);
      g.drawRect(QRectF(P(b.x + 1, b.y + 1), P(b.x + b.l - 1, b.y + b.h - 1)));
    }
    if (active) {
      // Jeton de l'étape active, sous son numéro
      g.setPen(Qt::NoPen);
      g.setBrush(JETON);
      g.drawEllipse(P(b.x + b.l / 2, b.y + b.h * 0.78), 1.1 * k, 1.1 * k);
    }
  }
  // Textes
  for (const auto& t : d.textes) {
    QFont f = g.font();
    f.setPixelSize(std::max(1, static_cast<int>(std::lround(t.taille * k))));
    f.setBold(false);
    g.setFont(f);
    g.setPen(t.discret ? discret : encre);
    const QFontMetricsF fm(f);
    const QString s = QString::fromStdString(t.texte);
    const double w = fm.horizontalAdvance(s);
    const QPointF p = P(t.x, t.y);
    const double x = t.alignement < 0 ? p.x() : t.alignement > 0 ? p.x() - w : p.x() - w / 2;
    // Le numéro de l'étape active remonte un peu pour laisser voir le jeton
    double y = p.y() + fm.ascent() / 2 - fm.descent() / 2;
    for (const auto& b : d.boites)
      if (b.etape >= 0 && m.actives.count(b.etape) && t.alignement == 0 && std::abs(t.x - (b.x + b.l / 2)) < 0.01 && std::abs(t.y - (b.y + b.h / 2)) < 0.01) y -= 1.6 * k;
    if (t.taille * k < 3) continue;
    g.drawText(QPointF(x, y), s);
  }
  g.restore();
}
