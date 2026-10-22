// Prométhée : barre de titre intégrée. Licence GPL-3.0-only.
#include "BarreTitre.hpp"

#include "Theme.hpp"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QWindow>

#include "Icones.hpp"
#include "RegistreActions.hpp"

namespace {
// Icônes des boutons de fenêtre, dessinées au trait fin
QIcon iconeFenetre(int quoi, const QColor& c) {
  QPixmap p(32, 32);
  p.fill(Qt::transparent);
  QPainter g(&p);
  g.setRenderHint(QPainter::Antialiasing);
  g.setPen(QPen(c, 1.6));
  if (quoi == 0) g.drawLine(QPointF(10, 16.5), QPointF(22, 16.5));
  else if (quoi == 1) g.drawRect(QRectF(10.5, 10.5, 11, 11));
  else if (quoi == 2) { g.drawRect(QRectF(10.5, 13.5, 9, 9)); g.drawLine(QPointF(13.5, 10.5), QPointF(22.5, 10.5)); g.drawLine(QPointF(22.5, 10.5), QPointF(22.5, 19.5)); }
  else { g.drawLine(QPointF(10.5, 10.5), QPointF(21.5, 21.5)); g.drawLine(QPointF(21.5, 10.5), QPointF(10.5, 21.5)); }
  return QIcon(p);
}
}  // namespace

BarreTitre::BarreTitre(QWidget* fenetre) : QWidget(fenetre), m_fenetre(fenetre) {
  setObjectName(QStringLiteral("barreTitre"));
  setAttribute(Qt::WA_StyledBackground);
  setFixedHeight(40);
  setProperty("themeFixe", true);  // couleurs réglées ici pour chaque thème (appliquerTheme)
  auto* l = new QHBoxLayout(this);
  l->setContentsMargins(10, 0, 0, 0);
  l->setSpacing(4);
  auto* logo = new QLabel;
  logo->setPixmap(QIcon(QStringLiteral(":/marque/marque.png")).pixmap(28, 28));
  logo->setToolTip(QStringLiteral("Prométhée"));
  l->addWidget(logo);
  m_menus = new QMenuBar;
  m_menus->setNativeMenuBar(false);
  l->addWidget(m_menus);
  auto* sep = new QFrame;
  sep->setFrameShape(QFrame::VLine);
  sep->setProperty("themeFixe", true);
  m_separateur = sep;
  l->addWidget(sep);
  m_rapides = new QHBoxLayout;
  m_rapides->setSpacing(2);
  l->addLayout(m_rapides);
  l->addStretch(1);
  m_titre = new QLabel;
  m_titre->setProperty("themeFixe", true);
  m_titre->setAttribute(Qt::WA_TransparentForMouseEvents);
  l->addWidget(m_titre);
  l->addStretch(1);
  auto* recherche = new QPushButton(icone(QStringLiteral("filtre-auto")), QStringLiteral("Rechercher une commande   Ctrl+K"));
  recherche->setObjectName(QStringLiteral("recherche"));
  marquerAction(recherche, "palette");
  recherche->setMinimumWidth(230);
  connect(recherche, &QPushButton::clicked, this, &BarreTitre::rechercheDemandee);
  l->addWidget(recherche);
  l->addSpacing(8);
  auto bouton = [&](int quoi, const char* nom, const QString& bulle) {
    auto* b = new QToolButton;
    b->setObjectName(QString::fromLatin1(nom));
    b->setIcon(iconeFenetre(quoi, QColor(0xC9, 0xD0, 0xEE)));
    b->setIconSize(QSize(32, 32));
    b->setFixedSize(46, 40);
    b->setToolTip(bulle);
    b->setStyleSheet(QStringLiteral("QToolButton { border-radius: 0; }"));
    l->addWidget(b);
    return b;
  };
  QToolButton* reduire = bouton(0, "reduire", QStringLiteral("Réduire"));
  m_agrandir = bouton(1, "agrandir", QStringLiteral("Agrandir"));
  QToolButton* fermer = bouton(3, "fermer", QStringLiteral("Fermer"));
  m_boutonsFenetre = {reduire, m_agrandir, fermer};
  connect(reduire, &QToolButton::clicked, m_fenetre, &QWidget::showMinimized);
  connect(m_agrandir, &QToolButton::clicked, this, [this] { basculer(); });
  connect(fermer, &QToolButton::clicked, m_fenetre, &QWidget::close);
  fermer->installEventFilter(this);
  m_fenetre->installEventFilter(this);
  connect(m_fenetre, &QWidget::windowTitleChanged, this, [this] { majTitre(); });
  majTitre();
  appliquerTheme();
  connect(&Theme::instance(), &Theme::change, this, &BarreTitre::appliquerTheme);
}

// La barre reste sombre dans tous les thèmes (zone de marque) : bleu nuit du logo, gris neutre du thème Sombre,
// noir et blanc en contraste élevé.
void BarreTitre::appliquerTheme() {
  const CouleursInterface& c = theme().c();
  struct Teintes {
    QString survol, filet, champ, texteChamp, survolChamp, titre;
  };
  Teintes t{QStringLiteral("#1F2750"), QStringLiteral("#2A3463"), QStringLiteral("#121838"), QStringLiteral("#9AA3C8"), QStringLiteral("#6E9BFF"), QStringLiteral("#E8ECFF")};
  if (theme().effectif() == IdTheme::Sombre)
    t = {QStringLiteral("#2A2C33"), QStringLiteral("#34363D"), QStringLiteral("#1C1D21"), QStringLiteral("#A6AAB3"), QStringLiteral("#8AB0FF"), QStringLiteral("#ECEDF0")};
  else if (theme().effectif() == IdTheme::ContrasteEleve)
    t = {QStringLiteral("#333333"), QStringLiteral("#FFFFFF"), QStringLiteral("#000000"), QStringLiteral("#FFFFFF"), QStringLiteral("#FFD400"), QStringLiteral("#FFFFFF")};
  const QString haut = c.titreHaut.name(), bas = c.titreBas.name(), texte = c.titreTexte.name(), bord = c.titreBordure.name();
  setStyleSheet(QStringLiteral(
      "#barreTitre { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:1 %2); border-bottom: 1px solid %3; }"
      "#barreTitre QMenuBar { background: transparent; border: none; font-size: 13px; color: %4; }"
      "#barreTitre QMenuBar::item { padding: 6px 9px; border-radius: 6px; background: transparent; color: %4; }"
      "#barreTitre QMenuBar::item:selected { background: %5; color: white; }"
      "#barreTitre QToolButton { border: none; border-radius: 6px; padding: 4px; }"
      "#barreTitre QToolButton:hover { background: %5; }"
      "#barreTitre QToolButton:disabled { background: transparent; }"
      "#barreTitre QToolButton#fermer:hover { background: #D9453A; }"
      "#barreTitre QPushButton#recherche { border: 1px solid %6; border-radius: 8px; background: %7; color: %8; padding: 4px 12px; text-align: left; }"
      "#barreTitre QPushButton#recherche:hover { border-color: %9; color: %4; }")
                    .arg(haut, bas, bord, texte, t.survol, t.filet, t.champ, t.texteChamp, t.survolChamp));
  m_separateur->setStyleSheet(QStringLiteral("color: %1;").arg(t.filet));
  m_titre->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(t.titre));
}

namespace {
// Icône redessinée dans une couleur unie (les icônes du logiciel sont foncées, la barre de titre est sombre)
QIcon teinter(const QIcon& source, const QColor& actif, const QColor& grise) {
  QIcon r;
  for (const auto& [mode, couleur] : {std::pair{QIcon::Normal, actif}, std::pair{QIcon::Disabled, grise}}) {
    QPixmap p = source.pixmap(40, 40);
    QPainter g(&p);
    g.setCompositionMode(QPainter::CompositionMode_SourceIn);
    g.fillRect(p.rect(), couleur);
    g.end();
    r.addPixmap(p, mode);
  }
  return r;
}
}  // namespace

void BarreTitre::ajouterAction(QAction* action) {
  auto* b = new QToolButton;
  b->setDefaultAction(action);
  const QIcon claire = teinter(action->icon(), QColor(0xDC, 0xE2, 0xFA), QColor(0x4A, 0x52, 0x80));
  b->setIcon(claire);
  connect(action, &QAction::changed, b, [b, claire] { b->setIcon(claire); });
  b->setToolButtonStyle(Qt::ToolButtonIconOnly);
  b->setIconSize(QSize(20, 20));
  m_rapides->addWidget(b);
}

void BarreTitre::ajouterSeparateur() { m_rapides->addSpacing(6); }

void BarreTitre::definirActions(const QList<QAction*>& actions) {
  while (QLayoutItem* it = m_rapides->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
  for (int i = 0; i < actions.size(); ++i) {
    if (i == 1) ajouterSeparateur();
    ajouterAction(actions[i]);
  }
}

void BarreTitre::setBoutonsFenetre(bool visibles) {
  for (QWidget* w : m_boutonsFenetre) w->setVisible(visibles);
}

void BarreTitre::majTitre() {
  QString t = m_fenetre->windowTitle();
  t.replace(QStringLiteral("[*]"), m_fenetre->isWindowModified() ? QStringLiteral("*") : QString());
  m_titre->setText(t);
  m_agrandir->setIcon(iconeFenetre(agrandie() ? 2 : 1, QColor(0xC9, 0xD0, 0xEE)));
}

void BarreTitre::mousePressEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton && m_fenetre->windowHandle() && (m_fenetre->windowFlags() & Qt::FramelessWindowHint) && !agrandie()) m_fenetre->windowHandle()->startSystemMove();
  QWidget::mousePressEvent(e);
}

void BarreTitre::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton) basculer();
}

bool BarreTitre::eventFilter(QObject* o, QEvent* e) {
  if (o == m_fenetre && (e->type() == QEvent::WindowStateChange || e->type() == QEvent::ModifiedChange)) majTitre();
  if (auto* b = qobject_cast<QToolButton*>(o); b && b->objectName() == QLatin1String("fermer")) {
    if (e->type() == QEvent::Enter) b->setIcon(iconeFenetre(3, Qt::white));
    if (e->type() == QEvent::Leave) b->setIcon(iconeFenetre(3, QColor(0xC9, 0xD0, 0xEE)));
  }
  return QWidget::eventFilter(o, e);
}
