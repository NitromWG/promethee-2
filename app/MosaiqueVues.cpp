// Prométhée : vues de l'atelier en mosaïque (lot L3, tâche 3.21). Voir MosaiqueVues.hpp.
// Licence GPL-3.0-only.
#include "MosaiqueVues.hpp"

#include <algorithm>

#include <QApplication>
#include <QEvent>
#include <QPainter>
#include <QSplitter>
#include <QVBoxLayout>

#include "InspecteurElectronique.hpp"

// Cadre d'une vue : sans bord dans la pile (la vue garde exactement sa taille d'avant), un liseré dans la mosaïque,
// de la couleur d'accent autour de la vue active
class MosaiqueVues::Cadre : public QWidget {
 public:
  Cadre(QWidget* vue, bool mode) : m_vue(vue), m_mode(mode) {
    setObjectName(QStringLiteral("cadreVue"));
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(vue);
  }
  QWidget* vue() const { return m_vue; }
  bool mode() const { return m_mode; }
  void setBord(bool mosaique, bool active) {
    m_mosaique = mosaique;
    m_activeBord = active;
    const int m = mosaique ? 2 : 0;
    layout()->setContentsMargins(m, m, m, m);
    update();
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    if (!m_mosaique) return;
    QPainter p(this);
    p.setPen(Qt::NoPen);
    p.setBrush(m_activeBord ? QColor(teintes::lien()) : palette().color(QPalette::Mid));
    p.drawRect(rect());
  }

 private:
  QWidget* m_vue;
  bool m_mode;
  bool m_mosaique = false, m_activeBord = false;
};

MosaiqueVues::MosaiqueVues(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("mosaiqueVues"));
  auto* l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  m_partage = new QSplitter(Qt::Horizontal);
  m_partage->setObjectName(QStringLiteral("partageVues"));
  m_partage->setChildrenCollapsible(false);
  m_partage->setHandleWidth(4);
  l->addWidget(m_partage);
}

MosaiqueVues::Cadre* MosaiqueVues::cadreDe(QWidget* vue) const {
  for (Cadre* c : m_cadres)
    if (c->vue() == vue) return c;
  return nullptr;
}

void MosaiqueVues::ajouter(QWidget* vue, bool mode) {
  if (!vue || cadreDe(vue)) return;
  auto* c = new Cadre(vue, mode);
  m_cadres.push_back(c);
  m_partage->addWidget(c);
  vue->installEventFilter(this);
  if (!m_active) m_active = vue;
  disposer();
}

void MosaiqueVues::setActive(QWidget* vue) {
  if (!cadreDe(vue) || vue == m_active) return;
  // Comme la pile d'avant : le focus qui était dans la vue quittée passe à la vue nouvelle
  QWidget* focus = QApplication::focusWidget();
  const bool suivre = m_active && focus && (focus == m_active || m_active->isAncestorOf(focus));
  m_active = vue;
  disposer();
  if (suivre) vue->setFocus(Qt::OtherFocusReason);
}

void MosaiqueVues::setMosaique(bool oui) {
  if (oui == m_mosaique) return;
  m_mosaique = oui;
  // Première mosaïque : le schéma et le circuit, les deux premières vues de mode
  if (oui && m_montrees.empty())
    for (Cadre* c : m_cadres)
      if (c->mode() && m_montrees.size() < 2) m_montrees.push_back(c->vue());
  disposer();
}

void MosaiqueVues::montrer(QWidget* vue, bool oui) {
  if (!cadreDe(vue)) return;
  const auto it = std::find(m_montrees.begin(), m_montrees.end(), vue);
  if (oui && it == m_montrees.end()) m_montrees.push_back(vue);
  if (!oui && it != m_montrees.end()) m_montrees.erase(it);
  disposer();
}

bool MosaiqueVues::montree(QWidget* vue) const { return std::find(m_montrees.begin(), m_montrees.end(), vue) != m_montrees.end(); }

bool MosaiqueVues::visibleDansMosaique(QWidget* vue) const {
  const Cadre* c = cadreDe(vue);
  return c && !c->isHidden();
}

void MosaiqueVues::disposer() {
  // L'ordre du partage : celui des vues montrées, les autres ensuite
  std::vector<Cadre*> ordre;
  for (QWidget* v : m_montrees)
    if (Cadre* c = cadreDe(v)) ordre.push_back(c);
  for (Cadre* c : m_cadres)
    if (std::find(ordre.begin(), ordre.end(), c) == ordre.end()) ordre.push_back(c);
  for (int i = 0; i < static_cast<int>(ordre.size()); ++i)
    if (m_partage->indexOf(ordre[static_cast<std::size_t>(i)]) != i) m_partage->insertWidget(i, ordre[static_cast<std::size_t>(i)]);
  int montrees = 0;
  for (Cadre* c : m_cadres) {
    const bool visible = c->vue() == m_active || (m_mosaique && montree(c->vue()));
    c->setVisible(visible);
    montrees += visible;
    c->setBord(m_mosaique && montrees > 0, c->vue() == m_active);
  }
  // Une seule vue montrée : sans bord, comme la pile
  if (montrees <= 1)
    for (Cadre* c : m_cadres) c->setBord(false, false);
  else {
    // Parts égales à la première mosaïque
    QList<int> tailles;
    int visibles = 0;
    for (int i = 0; i < m_partage->count(); ++i) visibles += !m_partage->widget(i)->isHidden();
    const QList<int> actuelles = m_partage->sizes();
    bool egales = false;
    for (int i = 0; i < m_partage->count(); ++i)
      if (!m_partage->widget(i)->isHidden() && actuelles.value(i) == 0) egales = true;
    if (egales) {
      for (int i = 0; i < m_partage->count(); ++i) tailles << (m_partage->widget(i)->isHidden() ? 0 : std::max(1, width() / std::max(1, visibles)));
      m_partage->setSizes(tailles);
    }
  }
  emit dispositionChangee();
}

bool MosaiqueVues::eventFilter(QObject* o, QEvent* e) {
  if (m_mosaique && (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::FocusIn))
    if (auto* v = qobject_cast<QWidget*>(o); v && v != m_active)
      if (const Cadre* c = cadreDe(v); c && c->mode()) emit activeDemandee(v);
  return QWidget::eventFilter(o, e);
}
