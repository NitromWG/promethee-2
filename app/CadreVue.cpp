// Prométhée : cadre d'une vue 2D (lot L2, tâche 2.12). Licence GPL-3.0-only.
#include "CadreVue.hpp"

#include <algorithm>
#include <cmath>

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QToolButton>
#include <QWidget>

#include "Icones.hpp"
#include "RegistreActions.hpp"
#include "Theme.hpp"

namespace {
constexpr double MARGE = 20;  // millimètres autour du dessin, où l'on peut encore défiler
constexpr int LARGEUR_ENSEMBLE = 200, HAUTEUR_ENSEMBLE = 140, ECART = 6;
}  // namespace

// La vignette : le dessin entier, gardé en image tant qu'il ne change pas, et le cadre de ce qui est affiché
class VueEnsemble : public QWidget {
 public:
  VueEnsemble(CadreVue& c, QWidget* parent) : QWidget(parent), m_cadre(c) {
    setObjectName(QStringLiteral("vueEnsemble"));
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Vue d’ensemble : un clic ou un glisser déplace la vue"));
    setAttribute(Qt::WA_OpaquePaintEvent);
  }
  // Échelle et origine de la vignette : le dessin entier, centré
  double echelle() const {
    const QRectF d = m_cadre.m_a.contenu().adjusted(-MARGE, -MARGE, MARGE, MARGE);
    return std::min((width() - 4) / std::max(d.width(), 1.0), (height() - 4) / std::max(d.height(), 1.0));
  }
  QPointF origine() const {
    const QRectF d = m_cadre.m_a.contenu().adjusted(-MARGE, -MARGE, MARGE, MARGE);
    const double e = echelle();
    return QPointF(width() / 2.0 - d.center().x() * e, height() / 2.0 - d.center().y() * e);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    const std::uint64_t g = m_cadre.m_a.generation();
    if (m_image.isNull() || m_generation != g || m_image.size() != size() * devicePixelRatioF()) {
      m_image = QPixmap(size() * devicePixelRatioF());
      m_image.setDevicePixelRatio(devicePixelRatioF());
      m_image.fill(theme().c().surface);
      QPainter p(&m_image);
      p.setRenderHint(QPainter::Antialiasing);
      m_cadre.m_a.peindre(p, origine(), echelle());
      m_generation = g;
    }
    QPainter p(this);
    p.drawPixmap(0, 0, m_image);
    // Ce que la vue montre
    const QRectF z = m_cadre.zoneAffichee();
    const double e = echelle();
    const QPointF o = origine();
    const QRectF r(o + z.topLeft() * e, z.size() * e);
    QColor fond = theme().c().accentTexte;
    fond.setAlphaF(0.12);
    p.setPen(QPen(theme().c().accentTexte, 1.5));
    p.setBrush(fond);
    p.drawRect(r.intersected(QRectF(rect()).adjusted(1, 1, -1, -1)));
    p.setPen(QPen(theme().c().bordure, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
  }
  void mousePressEvent(QMouseEvent* e) override { aller(e->position()); }
  void mouseMoveEvent(QMouseEvent* e) override {
    if (e->buttons() & Qt::LeftButton) aller(e->position());
  }

 private:
  // Le point visé devient le centre de la vue
  void aller(QPointF q) {
    const QPointF d = m_cadre.versDessin(q);
    const double e = m_cadre.m_a.echelle();
    const QWidget* v = m_cadre.m_vue;
    m_cadre.m_a.deplacer(QPointF(v->width() / 2.0 - d.x() * e, v->height() / 2.0 - d.y() * e));
  }
  CadreVue& m_cadre;
  QPixmap m_image;
  std::uint64_t m_generation = ~std::uint64_t{0};
};

CadreVue::CadreVue(QWidget* vue, Acces a, const QString& cle, int haut) : QObject(vue), m_vue(vue), m_a(std::move(a)), m_cle(cle), m_haut(haut) {
  m_h = new QScrollBar(Qt::Horizontal, vue);
  m_h->setObjectName(QStringLiteral("barreHorizontale"));
  m_v = new QScrollBar(Qt::Vertical, vue);
  m_v->setObjectName(QStringLiteral("barreVerticale"));
  for (QScrollBar* b : {m_h, m_v}) {
    b->setCursor(Qt::ArrowCursor);
    connect(b, &QScrollBar::valueChanged, this, [this] { defiler(); });
  }
  m_ensemble = new VueEnsemble(*this, vue);
  m_bouton = new QToolButton(vue);
  m_bouton->setObjectName(QStringLiteral("boutonVueEnsemble"));
  m_bouton->setIcon(icone(QStringLiteral("plan")));
  m_bouton->setAutoRaise(true);
  m_bouton->setCheckable(true);
  m_bouton->setCursor(Qt::ArrowCursor);
  m_bouton->setToolTip(QStringLiteral("Vue d’ensemble du dessin, en bas à droite"));
  marquerAction(m_bouton, "vue.vueEnsemble");
  connect(m_bouton, &QToolButton::clicked, this, [this](bool oui) { setEnsembleVisible(oui); });
  vue->installEventFilter(this);
  // Créés après que la vue s'est montrée : ils se montrent d'eux-mêmes
  for (QWidget* w : {static_cast<QWidget*>(m_h), static_cast<QWidget*>(m_v), static_cast<QWidget*>(m_bouton)}) w->show();
  setEnsembleVisible(QSettings().value(m_cle, false).toBool());
}

bool CadreVue::ensembleVisible() const { return !m_ensemble->isHidden(); }

void CadreVue::setEnsembleVisible(bool oui) {
  m_ensemble->setVisible(oui);
  m_bouton->setChecked(oui);
  QSettings().setValue(m_cle, oui);
  placer();
}

bool CadreVue::eventFilter(QObject* o, QEvent* e) {
  if (o == m_vue && (e->type() == QEvent::Resize || e->type() == QEvent::Show)) placer();
  return QObject::eventFilter(o, e);
}

void CadreVue::placer() {
  const int w = m_vue->width(), h = m_vue->height(), t = EPAISSEUR;
  m_v->setGeometry(w - t, m_haut, t, std::max(0, h - t - m_haut));
  m_h->setGeometry(0, h - t, std::max(0, w - t), t);
  const int b = 22;
  m_bouton->setGeometry(w - t - b - ECART, h - t - b - ECART, b, b);
  // La vignette au-dessus du bouton, à la forme du dessin
  const QRectF d = m_a.contenu().adjusted(-MARGE, -MARGE, MARGE, MARGE);
  const double rapport = d.height() > 0 ? d.width() / d.height() : 1.4;
  int lw = LARGEUR_ENSEMBLE, lh = static_cast<int>(std::lround(LARGEUR_ENSEMBLE / rapport));
  if (lh > HAUTEUR_ENSEMBLE) {
    lh = HAUTEUR_ENSEMBLE;
    lw = static_cast<int>(std::lround(HAUTEUR_ENSEMBLE * rapport));
  }
  m_ensemble->setGeometry(w - t - ECART - lw, h - t - b - 2 * ECART - lh, lw, lh);
  for (QWidget* x : {static_cast<QWidget*>(m_h), static_cast<QWidget*>(m_v), m_ensemble, static_cast<QWidget*>(m_bouton)}) x->raise();
  maj();
}

QRectF CadreVue::zoneAffichee() const {
  const double e = std::max(m_a.echelle(), 1e-9);
  const QPointF o = m_a.origine();
  return QRectF(QPointF(-o.x() / e, (m_haut - o.y()) / e), QSizeF((m_vue->width() - EPAISSEUR) / e, (m_vue->height() - EPAISSEUR - m_haut) / e));
}

QRectF CadreVue::etendue() const { return m_a.contenu().adjusted(-MARGE, -MARGE, MARGE, MARGE).united(zoneAffichee()); }

QPointF CadreVue::versDessin(QPointF q) const {
  const auto* v = static_cast<const VueEnsemble*>(m_ensemble);
  const double e = std::max(v->echelle(), 1e-9);
  return (q - v->origine()) / e;
}

void CadreVue::maj() {
  // Pendant qu'une barre est tenue, son étendue ne bouge pas sous la souris
  if (m_h->isSliderDown() || m_v->isSliderDown()) {
    m_ensemble->update();
    return;
  }
  const double e = m_a.echelle();
  const QRectF tout = etendue(), vu = zoneAffichee();
  m_maj = true;
  {
    const QSignalBlocker bh(m_h), bv(m_v);
    const int pageH = static_cast<int>(std::lround(vu.width() * e)), pageV = static_cast<int>(std::lround(vu.height() * e));
    m_h->setRange(0, std::max(0, static_cast<int>(std::lround(tout.width() * e)) - pageH));
    m_h->setPageStep(std::max(1, pageH));
    m_h->setSingleStep(std::max(1, pageH / 20));
    m_h->setValue(static_cast<int>(std::lround((vu.left() - tout.left()) * e)));
    m_v->setRange(0, std::max(0, static_cast<int>(std::lround(tout.height() * e)) - pageV));
    m_v->setPageStep(std::max(1, pageV));
    m_v->setSingleStep(std::max(1, pageV / 20));
    m_v->setValue(static_cast<int>(std::lround((vu.top() - tout.top()) * e)));
  }
  m_maj = false;
  m_h->setProperty("etendueX", tout.left());
  m_v->setProperty("etendueY", tout.top());
  m_ensemble->update();
}

void CadreVue::defiler() {
  if (m_maj) return;
  const double e = m_a.echelle();
  // L'étendue retenue au dernier réglage des barres : la valeur se lit dans le même repère
  const double x0 = m_h->property("etendueX").toDouble(), y0 = m_v->property("etendueY").toDouble();
  const double gauche = x0 + m_h->value() / e, haut = y0 + m_v->value() / e;
  m_a.deplacer(QPointF(-gauche * e, m_haut - haut * e));
}
