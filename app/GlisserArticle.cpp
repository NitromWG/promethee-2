// Prométhée : glisser-déposer d'un article de bibliothèque (lot L2, tâche 2.2). Licence GPL-3.0-only.
#include "GlisserArticle.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QDrag>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>

#include "RegistreActions.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"
#include "promethee/electronique.hpp"

using namespace prom::ecad;

QMimeData* GlisserArticle::mime(const DefSymbole& d) {
  auto* m = new QMimeData;
  m->setData(QString::fromLatin1(MIME), QByteArray::fromStdString(symboleVersJson(d).dump()));
  // Le texte brut, pour les autres programmes : « Résistance R_0603 »
  m->setText(QString::fromStdString(d.empreinte.empty() ? d.nom : d.nom + " " + d.empreinte));
  return m;
}

std::optional<DefSymbole> GlisserArticle::article(const QMimeData* m) {
  if (!m || !m->hasFormat(QString::fromLatin1(MIME))) return std::nullopt;
  try {
    DefSymbole d = symboleDepuisJson(nlohmann::ordered_json::parse(m->data(QString::fromLatin1(MIME)).toStdString()));
    if (d.id.empty()) return std::nullopt;
    return d;
  } catch (...) {
    return std::nullopt;
  }
}

GlisserArticle::GlisserArticle(QAbstractItemView* source, std::function<std::optional<DefSymbole>(const QModelIndex&)> articleDe, QObject* parent)
    : QObject(parent), m_source(source), m_articleDe(std::move(articleDe)) {
  m_source->viewport()->installEventFilter(this);
}

GlisserArticle::~GlisserArticle() {
  if (m_actif) {
    qApp->removeEventFilter(this);
    QApplication::restoreOverrideCursor();
  }
  delete m_vignette;
}

bool GlisserArticle::eventFilter(QObject* o, QEvent* e) {
  // Pendant le glisser, les touches sont au geste : R, X, Y pour la pose, Échap pour renoncer ; aucun raccourci
  if (m_actif) {
    if (e->type() == QEvent::ShortcutOverride) {
      e->accept();
      return true;
    }
    if (e->type() == QEvent::KeyPress) return touche(static_cast<QKeyEvent*>(e));
    if (e->type() == QEvent::KeyRelease) return true;
  }
  if (o != m_source->viewport()) return false;
  switch (e->type()) {
    case QEvent::MouseButtonPress: {
      auto* m = static_cast<QMouseEvent*>(e);
      m_appuye = false;
      if (m->button() != Qt::LeftButton) return false;
      m_article = m_articleDe(m_source->indexAt(m->position().toPoint()));
      m_appuye = m_article.has_value();
      m_appui = m->position().toPoint();
      return false;
    }
    case QEvent::MouseMove: {
      auto* m = static_cast<QMouseEvent*>(e);
      const QPoint global = m->globalPosition().toPoint();
      if (m_actif) {
        suivre(global);
        return true;
      }
      if (m_appuye && (m->buttons() & Qt::LeftButton) && (m->position().toPoint() - m_appui).manhattanLength() >= QApplication::startDragDistance()) {
        commencer();
        suivre(global);
        return true;
      }
      return false;
    }
    case QEvent::MouseButtonRelease: {
      auto* m = static_cast<QMouseEvent*>(e);
      m_appuye = false;
      if (!m_actif || m->button() != Qt::LeftButton) return false;
      lacher(m->globalPosition().toPoint());
      return true;
    }
    default: return false;
  }
}

void GlisserArticle::commencer() {
  // La liste ne verra pas la fin de son appui (le glisser la prend) : un relâcher hors de tout article la rend à son
  // état neutre, sans clic ; sinon le clic suivant sur un article n'armerait pas sa pose (B7)
  const QPointF dehors(-1, -1);
  QMouseEvent fin(QEvent::MouseButtonRelease, dehors, m_source->viewport()->mapToGlobal(dehors), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(m_source->viewport(), &fin);
  m_actif = true;
  m_enAttente.clear();
  qApp->installEventFilter(this);
  QApplication::setOverrideCursor(Qt::ForbiddenCursor);
  if (!m_vignette) {
    m_vignette = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
    m_vignette->setObjectName(QStringLiteral("vignetteGlisser"));
    m_vignette->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_vignette->setAttribute(Qt::WA_ShowWithoutActivating);
  }
  m_vignette->setPixmap(VueSchema::apercu(*m_article, QSize(72, 48)));
  m_vignette->adjustSize();
}

void GlisserArticle::suivre(QPoint global) {
  QWidget* sous = QApplication::widgetAt(global);
  VueSchema* vs = nullptr;
  VueCircuit* vc = nullptr;
  m_refus.clear();
  for (QWidget* x = sous; x && !vs && !vc && !m_refus; x = x->parentWidget()) {
    if (auto* s = qobject_cast<VueSchema*>(x); s && s->domaine() == VueSchema::Domaine::Electronique) vs = s;
    else if (auto* c = qobject_cast<VueCircuit*>(x)) {
      if (m_article->mecanique) vc = c;
      else m_refus = c;  // le circuit ne reçoit que les articles mécaniques
    }
  }
  if (vs != m_schema || vc != m_circuit) quitterCible();
  if (vs) {
    const bool arrivee = !m_schema;
    m_schema = vs;
    vs->glisserArticle(*m_article, vs->mapFromGlobal(QPointF(global)));
    if (arrivee) {
      for (const std::string& a : m_enAttente) vs->executerAction(a);
      if (!m_enAttente.empty()) vs->glisserArticle(*m_article, vs->mapFromGlobal(QPointF(global)));
    }
  } else if (vc) {
    const bool arrivee = !m_circuit;
    m_circuit = vc;
    vc->suivreArticle(*m_article, vc->mapFromGlobal(QPointF(global)));
    if (arrivee)
      for (const std::string& a : m_enAttente)
        if (a == "objet.tourner" || a == "objet.tournerInverse") vc->tournerArticle(a == "objet.tournerInverse");
  }
  const bool cible = vs || vc;
  QApplication::changeOverrideCursor(cible ? Qt::DragCopyCursor : Qt::ForbiddenCursor);
  if (m_vignette) {
    m_vignette->move(global + QPoint(16, 16));
    m_vignette->setVisible(!cible);
  }
  // Hors des fenêtres de Prométhée : le glisser-déposer du système prend le relais (une autre instance le reçoit)
  if (!sous && !QApplication::topLevelAt(global)) versSysteme();
}

void GlisserArticle::lacher(QPoint global) {
  suivre(global);
  if (!m_actif) return;
  const std::string id = m_article->id;
  bool pose = false;
  if (m_schema) {
    VueSchema* vs = m_schema;
    m_schema.clear();  // la pose rend la main à Sélection : rien à rendre ensuite
    pose = vs->deposerArticle(vs->mapFromGlobal(QPointF(global)));
  } else if (m_circuit) {
    VueCircuit* vc = m_circuit;
    m_circuit.clear();
    pose = vc->poserArticle(vc->mapFromGlobal(QPointF(global)));
  } else if (m_refus) {
    m_refus->refuserArticle(*m_article);
  }
  quitterCible();
  finir();
  if (pose) emit depose(QString::fromStdString(id));
}

void GlisserArticle::annuler() {
  quitterCible();
  finir();
}

void GlisserArticle::quitterCible() {
  if (m_schema) m_schema->abandonnerArticle();
  if (m_circuit) m_circuit->abandonnerArticle();
  m_schema.clear();
  m_circuit.clear();
}

void GlisserArticle::finir() {
  if (!m_actif) return;
  m_actif = m_appuye = false;
  m_enAttente.clear();
  qApp->removeEventFilter(this);
  QApplication::restoreOverrideCursor();
  if (m_vignette) m_vignette->hide();
}

bool GlisserArticle::touche(QKeyEvent* k) {
  for (const ActionAtelier* a : RegistreActions::atelierElectronique().pour(PorteeAction::Canevas, *k))
    if (a->identifiant == "commun.echap") {
      annuler();
      return true;
    }
  for (const ActionAtelier* a : RegistreActions::atelierElectronique().pour(PorteeAction::Schema, *k)) {
    const std::string& id = a->identifiant;
    if (id != "objet.tourner" && id != "objet.tournerInverse" && id != "objet.miroirX" && id != "objet.miroirY") continue;
    if (m_schema) m_schema->executerAction(id);
    else if (m_circuit) {
      if (id == "objet.tourner" || id == "objet.tournerInverse") m_circuit->tournerArticle(id == "objet.tournerInverse");
    } else
      m_enAttente.push_back(id);
    break;
  }
  return true;  // toute autre touche attend la fin du geste
}

void GlisserArticle::versSysteme() {
  const DefSymbole d = *m_article;
  quitterCible();
  finir();
  auto* drag = new QDrag(m_source);
  drag->setMimeData(mime(d));
  drag->setPixmap(VueSchema::apercu(d, QSize(72, 48)));
  drag->exec(Qt::CopyAction);
}
