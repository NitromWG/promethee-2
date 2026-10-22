// Prométhée : bandeau d'outil des canevas. Licence GPL-3.0-only.
#include "BandeauOutil.hpp"

#include <QAccessible>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QMenu>
#include <QToolButton>

#include "Theme.hpp"

BandeauOutil::BandeauOutil(QWidget* canevas) : QWidget(canevas) {
  setObjectName(QStringLiteral("bandeauOutil"));
  setProperty("themeFixe", true);  // couleurs prises dans le thème, pas traduites après coup
  setAccessibleName(QStringLiteral("Bandeau d’outil"));
  setAttribute(Qt::WA_StyledBackground, false);
  m_ligne = new QHBoxLayout(this);
  m_ligne->setContentsMargins(10, 2, 4, 2);
  m_ligne->setSpacing(4);
  m_texte = new QLabel;
  m_texte->setObjectName(QStringLiteral("texteBandeau"));
  m_texte->setTextFormat(Qt::PlainText);
  m_texte->setProperty("themeFixe", true);
  m_ligne->addWidget(m_texte, 1);
  canevas->installEventFilter(this);
  connect(&Theme::instance(), &Theme::change, this, [this] { appliquerTheme(); });
  appliquerTheme();
  hide();
}

void BandeauOutil::appliquerTheme() {
  const CouleursInterface& c = theme().c();
  QFont f = font();
  f.setPixelSize(13);
  setFont(f);
  m_texte->setStyleSheet(QStringLiteral("color: %1;").arg(c.texte.name()));
  const QString style = QStringLiteral(
                            "QToolButton { color: %1; background: transparent; border: 1px solid transparent; border-radius: 5px; padding: 1px 7px; }"
                            "QToolButton:hover { background: %2; border-color: %3; }"
                            "QToolButton:checked { background: %4; color: %5; }")
                            .arg(c.accentTexte.name(), c.survol.name(), c.bordure.name(), c.accent.name(), c.surAccent.name());
  for (QToolButton* b : m_boutons) b->setStyleSheet(style);
  setProperty("styleBoutons", style);
  update();
}

void BandeauOutil::placer() {
  if (QWidget* p = parentWidget()) setGeometry(0, 0, p->width(), HAUTEUR);
  raise();
}

bool BandeauOutil::eventFilter(QObject* o, QEvent* e) {
  if (o == parentWidget() && e->type() == QEvent::Resize) placer();
  return QWidget::eventFilter(o, e);
}

void BandeauOutil::paintEvent(QPaintEvent*) {
  QPainter g(this);
  QColor fond = theme().c().surface;
  fond.setAlphaF(0.92);
  g.fillRect(rect(), fond);
  g.setPen(theme().c().bordure);
  g.drawLine(rect().bottomLeft(), rect().bottomRight());
}

QString BandeauOutil::texte() const { return m_texte->text(); }

QToolButton* BandeauOutil::bouton(const QString& id) const {
  for (QToolButton* b : m_boutons)
    if (b->objectName() == QStringLiteral("bandeau:") + id && b->isVisible()) return b;
  return nullptr;
}

namespace {
// Ce qui distingue deux listes de choix : libellés, coches et lignes grisées
QString signature(const ActionBandeau& a) {
  QString s;
  for (const ChoixBandeau& x : a.choix) s += x.libelle + QLatin1Char(x.coche ? '1' : '0') + QLatin1Char(x.actif ? '1' : '0') + QLatin1Char('|');
  return s;
}
}  // namespace

void BandeauOutil::montrer(const ContenuBandeau& c) {
  if (c.vide()) {
    m_contenu = {};
    hide();
    return;
  }
  const QString t = c.detail.isEmpty() ? c.titre : c.titre + QStringLiteral(" · ") + c.detail;
  // Mêmes boutons qu'avant : ils restent (le clic qui a mené ici n'est pas détruit sous la souris)
  bool memes = c.actions.size() == m_contenu.actions.size();
  for (std::size_t i = 0; memes && i < c.actions.size(); ++i)
    memes = c.actions[i].id == m_contenu.actions[i].id && c.actions[i].libelle == m_contenu.actions[i].libelle && c.actions[i].touche == m_contenu.actions[i].touche &&
            c.actions[i].action == m_contenu.actions[i].action && signature(c.actions[i]) == signature(m_contenu.actions[i]);
  m_contenu = c;
  if (!memes) {
    for (QToolButton* b : m_boutons) {
      b->hide();
      b->deleteLater();
    }
    m_boutons.clear();
    std::vector<const ActionBandeau*> ordre;
    for (const auto& a : c.actions)
      if (!a.sortie) ordre.push_back(&a);
    for (const auto& a : c.actions)
      if (a.sortie) ordre.push_back(&a);  // la sortie toujours à droite
    for (const ActionBandeau* a : ordre) {
      auto* b = new QToolButton(this);
      b->setObjectName(QStringLiteral("bandeau:") + a->id);
      b->setText(a->touche.isEmpty() ? a->libelle : a->libelle + QStringLiteral(" · ") + a->touche);
      b->setToolTip(a->touche.isEmpty() ? a->libelle : QStringLiteral("%1 (%2)").arg(a->libelle, a->touche));
      b->setFocusPolicy(Qt::NoFocus);  // le canevas garde le clavier
      b->setCursor(Qt::PointingHandCursor);
      b->setCheckable(a->cochable);
      b->setStyleSheet(property("styleBoutons").toString());
      if (!a->action.empty()) marquerAction(b, a->action);
      const QString id = a->id;
      if (!a->choix.empty()) {
        // La liste du bouton : flèche à côté s'il agit aussi lui-même, bouton entier sinon
        auto* menu = new QMenu(b);
        menu->setObjectName(QStringLiteral("liste:") + a->id);
        for (std::size_t k = 0; k < a->choix.size(); ++k) {
          const ChoixBandeau& x = a->choix[k];
          if (x.section) {
            menu->addSection(x.libelle);
            continue;
          }
          QAction* ligne = menu->addAction(x.libelle);
          ligne->setCheckable(true);
          ligne->setChecked(x.coche);
          ligne->setEnabled(x.actif);
          ligne->setToolTip(x.bulle);
          if (!x.action.empty()) marquerAction(ligne, x.action);
          connect(ligne, &QAction::triggered, this, [this, id, k] {
            for (const auto& y : m_contenu.actions)
              if (y.id == id && k < y.choix.size() && y.choix[k].faire) {
                const auto faire = y.choix[k].faire;
                faire();
                emit actionFaite();
                return;
              }
          });
        }
        menu->setToolTipsVisible(true);
        b->setMenu(menu);
        b->setPopupMode(a->faire ? QToolButton::MenuButtonPopup : QToolButton::InstantPopup);
      }
      connect(b, &QToolButton::clicked, this, [this, id] {
        for (const auto& x : m_contenu.actions)
          if (x.id == id && x.faire) {
            const auto faire = x.faire;  // l'action peut changer le contenu du bandeau
            faire();
            emit actionFaite();
            return;
          }
      });
      m_ligne->addWidget(b);
      b->show();  // visible tout de suite, sans attendre que la boucle d'événements le montre
      m_boutons.push_back(b);
    }
  }
  for (QToolButton* b : m_boutons)
    for (const auto& a : c.actions)
      if (b->objectName() == QStringLiteral("bandeau:") + a.id && a.cochable) b->setChecked(a.coche);
  if (m_texte->text() != t) {
    m_texte->setText(t);
    setAccessibleDescription(t);
    QAccessibleEvent ev(this, QAccessible::DescriptionChanged);
    QAccessible::updateAccessibility(&ev);
  }
  placer();
  show();
  if (!memes && layout()) layout()->activate();  // les nouveaux boutons sont à leur place avant le geste suivant
}
