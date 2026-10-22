// Prométhée : aperçu d'un article de bibliothèque (lot L1, tâche 1.21). Voir ApercuArticle.hpp.
// Licence GPL-3.0-only.
#include "ApercuArticle.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVBoxLayout>

#include "EncresCircuit.hpp"
#include "VueSchema.hpp"
#include "promethee/electronique.hpp"

using namespace prom::ecad;

namespace {
double enMm(Nm v) { return static_cast<double>(v) * 1e-6; }

Boite bornesEmpreinte(const DefEmpreinte& e) {
  Boite b = e.encombrement;
  for (const Pastille& p : e.pastilles)
    for (const Pt& q : formePastille(p)) b.etendre(q);
  for (const TraitEmpreinte& t : e.traits) {
    if (t.rayon > 0) b.etendre(Boite{t.a.x - t.rayon, t.a.y - t.rayon, t.a.x + t.rayon, t.a.y + t.rayon});
    else {
      b.etendre(t.a);
      b.etendre(t.b);
    }
  }
  if (b.vide()) b = {-1'000'000, -1'000'000, 1'000'000, 1'000'000};
  return b;
}

// Enveloppe convexe (chaîne monotone), pour la silhouette d'un corps rond en perspective
std::vector<QPointF> enveloppe(std::vector<QPointF> p) {
  std::sort(p.begin(), p.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); });
  if (p.size() < 3) return p;
  auto tourne = [](const QPointF& o, const QPointF& a, const QPointF& b) { return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x()); };
  std::vector<QPointF> h(2 * p.size());
  std::size_t k = 0;
  for (const QPointF& q : p) {
    while (k >= 2 && tourne(h[k - 2], h[k - 1], q) <= 0) --k;
    h[k++] = q;
  }
  for (std::size_t i = p.size() - 1, t = k + 1; i-- > 0;) {
    while (k >= t && tourne(h[k - 2], h[k - 1], p[i]) <= 0) --k;
    h[k++] = p[i];
  }
  h.resize(k - 1);
  return h;
}

QPolygonF polygone(const std::vector<QPointF>& p) { return QPolygonF(QList<QPointF>(p.begin(), p.end())); }

const char* const FACES[] = {"Symbole", "Empreinte", "3D", "Photo"};
const char* const NOMS_FACES[] = {"faceSymbole", "faceEmpreinte", "face3D", "facePhoto"};
}  // namespace

ApercuArticle::ApercuArticle(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("apercuArticle"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(0, 0, 0, 0);
  col->setSpacing(4);
  auto* rangee = new QHBoxLayout;
  rangee->setSpacing(2);
  for (int i = 0; i < NOMBRE_FACES; ++i) {
    auto* b = new QToolButton;
    b->setObjectName(QString::fromLatin1(NOMS_FACES[i]));
    b->setText(QString::fromUtf8(FACES[i]));
    b->setCheckable(true);
    b->setAutoExclusive(true);
    b->setAutoRaise(true);
    b->setChecked(i == 0);
    connect(b, &QToolButton::clicked, this, [this, i] { setFace(static_cast<Face>(i)); });
    m_boutons[static_cast<std::size_t>(i)] = b;
    rangee->addWidget(b);
  }
  rangee->addStretch(1);
  col->addLayout(rangee);
  setStyleSheet(QStringLiteral("QToolButton { padding: 1px 4px; }"));
  // Le bouton coché a un cadre : son libellé ne doit pas être rogné
  for (QToolButton* b : m_boutons) b->setMinimumWidth(b->fontMetrics().horizontalAdvance(b->text()) + 14);
  m_image = new QLabel;
  m_image->setObjectName(QStringLiteral("imageApercu"));
  m_image->setAlignment(Qt::AlignCenter);
  m_image->setWordWrap(true);
  m_image->setMinimumSize(160, 120);
  m_image->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  m_image->setProperty("themeFixe", true);
  m_image->setStyleSheet(QStringLiteral("background: white; color: #5A6275; border: 1px solid #E1E5F0; border-radius: 8px; padding: 2px;"));
  col->addWidget(m_image, 1);
}

void ApercuArticle::montrer(const DefSymbole& d, const DefEmpreinte* empreinte) {
  m_symbole = d;
  if (empreinte) m_empreinte = *empreinte;
  else m_empreinte.reset();
  dessiner();
}

void ApercuArticle::vider() {
  m_symbole.reset();
  m_empreinte.reset();
  dessiner();
}

void ApercuArticle::setFace(Face f) {
  m_face = f;
  m_boutons[static_cast<std::size_t>(f)]->setChecked(true);
  dessiner();
  emit faceChangee(f);
}

QPixmap ApercuArticle::image() const { return m_image->pixmap(); }
QString ApercuArticle::mention() const { return m_image->text(); }

void ApercuArticle::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  dessiner();
}

void ApercuArticle::dessiner() {
  const QSize taille = m_image->contentsRect().size().expandedTo(QSize(156, 116));
  auto dire = [this](const QString& texte) {
    m_image->setPixmap(QPixmap());
    m_image->setText(texte);
  };
  if (!m_symbole) return dire(QStringLiteral("Choisis un article pour le voir ici."));
  switch (m_face) {
    case Face::Symbole: m_image->setPixmap(VueSchema::apercu(*m_symbole, taille)); return;
    case Face::Empreinte:
      if (!m_empreinte) return dire(m_symbole->empreinte.empty() ? QStringLiteral("Pas d’empreinte associée à cet article.") : QStringLiteral("Empreinte « %1 » introuvable.").arg(QString::fromStdString(m_symbole->empreinte)));
      m_image->setPixmap(dessinEmpreinte(*m_empreinte, taille));
      return;
    case Face::Vue3D:
      if (!m_empreinte) return dire(QStringLiteral("Pas de boîtier à montrer : l’article n’a pas d’empreinte."));
      m_image->setPixmap(dessin3D(*m_empreinte, taille));
      return;
    case Face::Photo:
      if (const auto p = photo(*m_symbole, taille)) m_image->setPixmap(*p);
      else dire(QStringLiteral("Pas de photo. Ajoute au composant une propriété « photo » qui donne le chemin d’une image."));
      return;
  }
}

// Empreinte vue de dessus, sur le fond du canevas du circuit : encombrement en tirets, sérigraphie, pastilles et
// perçages, à l'échelle de l'image
QPixmap ApercuArticle::dessinEmpreinte(const DefEmpreinte& e, QSize taille) {
  using namespace encres_circuit;
  QPixmap pm(taille);
  pm.fill(FOND);
  QPainter g(&pm);
  g.setRenderHint(QPainter::Antialiasing);
  const Boite b = bornesEmpreinte(e);
  const double l = std::max(enMm(b.largeur()), 0.5), h = std::max(enMm(b.hauteur()), 0.5);
  const double s = std::min((taille.width() - 20) / (l * 1.1), (taille.height() - 20) / (h * 1.1));
  const QPointF centre(enMm(b.x0 + b.x1) / 2, enMm(b.y0 + b.y1) / 2);
  auto ecran = [&](Pt p) { return QPointF(taille.width() / 2.0 + (enMm(p.x) - centre.x()) * s, taille.height() / 2.0 + (enMm(p.y) - centre.y()) * s); };
  if (!e.encombrement.vide()) {
    g.setPen(QPen(QColor(0x6B, 0x73, 0x94), 1, Qt::DashLine));
    g.setBrush(Qt::NoBrush);
    g.drawRect(QRectF(ecran({e.encombrement.x0, e.encombrement.y0}), ecran({e.encombrement.x1, e.encombrement.y1})));
  }
  for (const TraitEmpreinte& t : e.traits) {
    const bool serig = t.couche == Couche::SerigraphieDessus;
    g.setPen(QPen(serig ? SERIG_DESSUS : QColor(0x8A, 0x92, 0xB2), std::max(1.0, enMm(t.epaisseur) * s), Qt::SolidLine, Qt::RoundCap));
    g.setBrush(Qt::NoBrush);
    if (t.rayon > 0) g.drawEllipse(ecran(t.a), enMm(t.rayon) * s, enMm(t.rayon) * s);
    else g.drawLine(ecran(t.a), ecran(t.b));
  }
  for (const Pastille& p : e.pastilles) {
    if (p.type != TypePastille::NonMetallisee) {
      QPolygonF poly;
      for (const Pt& q : formePastille(p)) poly << ecran(q);
      g.setPen(Qt::NoPen);
      g.setBrush(p.type == TypePastille::Traversante ? TRAVERSANT : couleurCuivre(Couche::CuivreDessus));
      g.drawPolygon(poly);
    }
    if (p.percage > 0) {
      g.setPen(p.type == TypePastille::NonMetallisee ? QPen(QColor(0x8A, 0x92, 0xB2), 1) : Qt::NoPen);
      g.setBrush(FOND);
      const double r = enMm(p.percage) * s / 2;
      g.drawEllipse(ecran(p.position), r, r);
    }
  }
  return pm;
}

// Boîtier en perspective isométrique : un morceau de carte verte, les pastilles dorées, le corps (pavé ou cylindre)
// aux dimensions de l'empreinte, trois faces ombrées
QPixmap ApercuArticle::dessin3D(const DefEmpreinte& e, QSize taille) {
  const Boite carte = bornesEmpreinte(e).gonflee(800'000);
  const Boite corps = empriseCorps(e);
  const double hauteur = std::max(e.corps.hauteur > 0 ? enMm(e.corps.hauteur) : 1.0, 0.3), epaisseur = 1.6;
  // x à droite, y vers l'observateur, z vers le haut
  auto iso = [](double x, double y, double z) { return QPointF((x - y) * 0.866, (x + y) * 0.5 - z); };
  const double cx0 = enMm(carte.x0), cx1 = enMm(carte.x1), cy0 = enMm(carte.y0), cy1 = enMm(carte.y1);
  const double bx0 = enMm(corps.x0), bx1 = enMm(corps.x1), by0 = enMm(corps.y0), by1 = enMm(corps.y1);
  // Cadrage : tous les coins projetés tiennent dans l'image
  std::vector<QPointF> coins;
  for (double x : {cx0, cx1})
    for (double y : {cy0, cy1})
      for (double z : {-epaisseur, 0.0}) coins.push_back(iso(x, y, z));
  for (double x : {bx0, bx1})
    for (double y : {by0, by1}) coins.push_back(iso(x, y, hauteur));
  QRectF bornes(coins.front(), QSizeF(0, 0));
  for (const QPointF& p : coins) bornes |= QRectF(p, QSizeF(0.001, 0.001));
  const double s = std::min((taille.width() - 16) / bornes.width(), (taille.height() - 16) / bornes.height());
  const QPointF decalage = QPointF(taille.width() / 2.0, taille.height() / 2.0) - bornes.center() * s;
  auto ecran = [&](double x, double y, double z) { return iso(x, y, z) * s + decalage; };
  auto face = [&](std::initializer_list<std::array<double, 3>> pts) {
    QPolygonF p;
    for (const auto& [x, y, z] : pts) p << ecran(x, y, z);
    return p;
  };

  QPixmap pm(taille);
  QPainter g(&pm);
  g.setRenderHint(QPainter::Antialiasing);
  QLinearGradient fond(0, 0, 0, taille.height());
  fond.setColorAt(0, QColor(0xFB, 0xFC, 0xFF));
  fond.setColorAt(1, QColor(0xE3, 0xE8, 0xF2));
  g.fillRect(pm.rect(), fond);
  // Carte : dessus et deux tranches vues
  const QColor vert(0x1F, 0x6B, 0x45);
  g.setPen(QPen(vert.darker(160), 1));
  g.setBrush(vert.darker(140));
  g.drawPolygon(face({{cx0, cy1, 0}, {cx1, cy1, 0}, {cx1, cy1, -epaisseur}, {cx0, cy1, -epaisseur}}));
  g.setBrush(vert.darker(120));
  g.drawPolygon(face({{cx1, cy0, 0}, {cx1, cy1, 0}, {cx1, cy1, -epaisseur}, {cx1, cy0, -epaisseur}}));
  g.setBrush(vert);
  g.drawPolygon(face({{cx0, cy0, 0}, {cx1, cy0, 0}, {cx1, cy1, 0}, {cx0, cy1, 0}}));
  // Pastilles dorées sur le dessus
  g.setPen(Qt::NoPen);
  for (const Pastille& p : e.pastilles) {
    if (p.type == TypePastille::NonMetallisee) continue;
    QPolygonF poly;
    for (const Pt& q : formePastille(p)) poly << ecran(enMm(q.x), enMm(q.y), 0);
    g.setBrush(QColor(0xD9, 0xB6, 0x4A));
    g.drawPolygon(poly);
  }
  // Corps
  const QColor teinte(QString::fromStdString(e.corps.couleur));
  const QColor base = teinte.isValid() ? teinte : QColor(0x2A, 0x2D, 0x34);
  g.setPen(QPen(base.darker(180), 1));
  if (e.corps.rond) {
    const double rx = (bx1 - bx0) / 2, ry = (by1 - by0) / 2, mx = (bx0 + bx1) / 2, my = (by0 + by1) / 2;
    std::vector<QPointF> tour, haut;
    for (int i = 0; i < 48; ++i) {
      const double a = 2 * std::numbers::pi * i / 48;
      const double x = mx + rx * std::cos(a), y = my + ry * std::sin(a);
      tour.push_back(ecran(x, y, 0));
      tour.push_back(ecran(x, y, hauteur));
      haut.push_back(ecran(x, y, hauteur));
    }
    g.setBrush(base.darker(130));
    g.drawPolygon(polygone(enveloppe(tour)));
    g.setBrush(base.lighter(150));
    g.drawPolygon(polygone(haut));
  } else {
    g.setBrush(base.darker(140));
    g.drawPolygon(face({{bx0, by1, 0}, {bx1, by1, 0}, {bx1, by1, hauteur}, {bx0, by1, hauteur}}));
    g.setBrush(base.darker(115));
    g.drawPolygon(face({{bx1, by0, 0}, {bx1, by1, 0}, {bx1, by1, hauteur}, {bx1, by0, hauteur}}));
    g.setBrush(base.lighter(150));
    g.drawPolygon(face({{bx0, by0, hauteur}, {bx1, by0, hauteur}, {bx1, by1, hauteur}, {bx0, by1, hauteur}}));
  }
  return pm;
}

// Photo du composant réel : la propriété « photo » du symbole donne le chemin d'une image lisible
std::optional<QPixmap> ApercuArticle::photo(const DefSymbole& d, QSize taille) {
  const auto it = d.proprietes.find("photo");
  if (it == d.proprietes.end() || it->second.empty()) return std::nullopt;
  const QString chemin = QString::fromStdString(it->second);
  if (!QFileInfo(chemin).isFile()) return std::nullopt;
  QPixmap p(chemin);
  if (p.isNull()) return std::nullopt;
  return p.scaled(taille, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
