// Prométhée : fenêtre principale à onglets. Licence GPL-3.0-only.
#include "Coquille.hpp"

#include <algorithm>

#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QFileInfo>
#include <QShortcut>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>

#include "BarreTitre.hpp"
#include "Document.hpp"
#include "Journal.hpp"
#include "Recuperation.hpp"
#include "promethee/fichiers.hpp"
#include "FenetrePrincipale.hpp"
#include "Icones.hpp"
#include "RegistreActions.hpp"

namespace {
QList<Coquille*>& registre() {
  static QList<Coquille*> r;
  return r;
}
}  // namespace

QList<Coquille*> Coquille::fenetres() { return registre(); }

Coquille::~Coquille() { registre().removeAll(this); }

Coquille::Coquille(bool accueil) {
  registre().append(this);
  m_cadreIntegre = qEnvironmentVariableIsEmpty("PROMETHEE_CADRE_SYSTEME");
  setWindowFlag(Qt::FramelessWindowHint, m_cadreIntegre);
  setWindowIcon(iconeMarque());
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(0, 0, 0, 0);
  col->setSpacing(0);
  m_barre = new BarreTitre(this);
  m_barre->setBoutonsFenetre(m_cadreIntegre);
  col->addWidget(m_barre);
  m_pile = new QStackedWidget;
  col->addWidget(m_pile, 1);
  // Barre d'onglets, en bas
  auto* bas = new QWidget;
  bas->setObjectName(QStringLiteral("barreOnglets"));
  bas->setAttribute(Qt::WA_StyledBackground);
  bas->setStyleSheet(QStringLiteral(
      "#barreOnglets { background: #ECEFF8; border-top: 1px solid #D7DCEA; }"
      "QTabBar::tab { background: transparent; color: #4A5270; padding: 6px 12px; margin: 3px 2px; border-radius: 7px; min-width: 120px; max-width: 260px; }"
      "QTabBar::tab:selected { background: white; color: #10142A; font-weight: 600; border: 1px solid #D7DCEA; }"
      "QTabBar::tab:hover:!selected { background: #E3E6F1; }"
      "QToolButton#nouvelOnglet { border: none; border-radius: 7px; padding: 4px 8px; font-size: 16px; color: #232A45; }"
      "QToolButton#nouvelOnglet:hover { background: #E3E6F1; }"));
  auto* lb = new QHBoxLayout(bas);
  lb->setContentsMargins(6, 0, 6, 0);
  lb->setSpacing(2);
  m_onglets = new QTabBar;
  m_onglets->setShape(QTabBar::RoundedSouth);
  m_onglets->setTabsClosable(false);  // croix ajoutées à chaque onglet (plus discrètes que celles du style)
  m_onglets->setMovable(true);
  m_onglets->setDocumentMode(true);
  m_onglets->setExpanding(false);
  m_onglets->setElideMode(Qt::ElideRight);
  lb->addWidget(m_onglets);
  auto* plus = new QToolButton;
  plus->setObjectName(QStringLiteral("nouvelOnglet"));
  marquerAction(plus, "onglet.nouveau");
  plus->setText(QStringLiteral("+"));
  plus->setToolTip(QStringLiteral("Nouvel onglet (Ctrl+T)"));
  connect(plus, &QToolButton::clicked, this, [this] { nouvelOnglet(); });
  lb->addWidget(plus);
  m_etat = new QLabel;
  m_etat->setTextFormat(Qt::RichText);
  m_etat->setStyleSheet(QStringLiteral("color: #4A5270; padding-right: 8px;"));
  // Le message prend la place qui reste, sans jamais élargir la fenêtre ; trop long, il est coupé (bulle : en entier)
  m_etat->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_etat->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  connect(m_etat, &QLabel::linkActivated, this, [this](const QString& lien) { if (active()) active()->lienEtat(lien); });
  lb->addWidget(m_etat, 1);
  col->addWidget(bas);
  connect(m_onglets, &QTabBar::currentChanged, this, [this](int i) { activer(i); });
  connect(m_onglets, &QTabBar::tabCloseRequested, this, [this](int i) { fermerOnglet(i); });
  // Menu de l'onglet : le détacher dans sa fenêtre, le fermer
  m_onglets->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_onglets, &QTabBar::customContextMenuRequested, this, [this](const QPoint& p) {
    const int i = m_onglets->tabAt(p);
    if (i < 0) return;
    QMenu m(this);
    QAction* d = m.addAction(QStringLiteral("Détacher dans une fenêtre"), this, [this, i] { detacher(i); });
    d->setObjectName(QStringLiteral("detacherOnglet"));
    d->setEnabled(m_onglets->count() > 1);
    m.addAction(QStringLiteral("Fermer l’onglet"), this, [this, i] { fermerOnglet(i); });
    m.exec(m_onglets->mapToGlobal(p));
  });
  connect(m_onglets, &QTabBar::tabMoved, this, [this](int de, int vers) {
    QWidget* w = m_pile->widget(de);
    m_pile->removeWidget(w);
    m_pile->insertWidget(vers, w);
    m_pile->setCurrentIndex(m_onglets->currentIndex());
  });
  connect(m_barre, &BarreTitre::rechercheDemandee, this, [this] { if (active()) active()->paletteCommandesPublique(); });
  auto raccourci = [this](const QKeySequence& k, std::function<void()> f) {
    auto* s = new QShortcut(k, this);
    connect(s, &QShortcut::activated, this, [f] { f(); });
  };
  raccourci(QKeySequence(Qt::CTRL | Qt::Key_T), [this] { nouvelOnglet(); });
  raccourci(QKeySequence(Qt::CTRL | Qt::Key_W), [this] { fermerOnglet(m_onglets->currentIndex()); });
  raccourci(QKeySequence(Qt::CTRL | Qt::Key_Tab), [this] { if (m_onglets->count() > 1) m_onglets->setCurrentIndex((m_onglets->currentIndex() + 1) % m_onglets->count()); });
  qApp->installEventFilter(this);
  if (accueil) nouvelOnglet();
  m_barre->setGestionAgrandi([this] { return m_agrandi; }, [this] { basculerAgrandi(); });
  if (QScreen* ecran = QGuiApplication::primaryScreen()) {
    m_geometrieNormale = QRect(ecran->availableGeometry().center() - QPoint(750, 460), QSize(1500, 920)).intersected(ecran->availableGeometry());
    setGeometry(ecran->availableGeometry());
    m_agrandi = true;
  } else resize(1500, 920);
}

// Agrandir : toute la surface utile de l'écran où se trouve la fenêtre ; rétablir : la taille d'avant
void Coquille::basculerAgrandi() {
  QScreen* ecran = screen() ? screen() : QGuiApplication::primaryScreen();
  if (!ecran) return;
  if (m_agrandi) {
    setGeometry(m_geometrieNormale.isValid() ? m_geometrieNormale : QRect(ecran->availableGeometry().center() - QPoint(700, 430), QSize(1400, 860)));
    m_agrandi = false;
  } else {
    m_geometrieNormale = geometry();
    setGeometry(ecran->availableGeometry());
    m_agrandi = true;
  }
  m_barre->majTitre();
}

FenetrePrincipale* Coquille::active() const { return qobject_cast<FenetrePrincipale*>(m_pile->currentWidget()); }
FenetrePrincipale* Coquille::onglet(int i) const { return qobject_cast<FenetrePrincipale*>(m_pile->widget(i)); }
int Coquille::nombreOnglets() const { return m_onglets->count(); }
int Coquille::indexActif() const { return m_onglets->currentIndex(); }

void Coquille::brancher(FenetrePrincipale* f) {
  connect(f, &FenetrePrincipale::titreChange, this, [this, f] { majOnglet(f); });
  connect(f, &FenetrePrincipale::etatTexteChange, this, [this, f] {
    if (f != active()) return;
    m_etat->setText(f->texteEtat());
    m_etat->setToolTip(f->texteEtat());
  });
  // Depuis l'accueil d'un onglet déjà occupé : l'action part dans un nouvel onglet, sans rien demander
  connect(f, &FenetrePrincipale::demandeNouvelOnglet, this, [this](const QString& code) {
    FenetrePrincipale* g = nouvelOnglet();
    g->choixAccueilPublic(code);
  });
  connect(f, &FenetrePrincipale::demandeOuvrirFichier, this, [this](const QString& chemin) { ouvrirFichier(chemin); });
  // Enregistré : sa copie de récupération ne sert plus
  connect(f->document(), &Document::etatChange, this, [f] {
    if (!f->document()->modifie()) Recuperation::instance().oublier(*f->document());
  });
}

FenetrePrincipale* Coquille::nouvelOnglet() {
  auto* f = new FenetrePrincipale(true);
  ajouterOnglet(f);
  f->montrerAccueil();
  return f;
}

void Coquille::adopter(FenetrePrincipale* f) {
  if (!f) return;
  ajouterOnglet(f);
}

Coquille* Coquille::detacher(int index) {
  FenetrePrincipale* f = onglet(index);
  if (!f || m_onglets->count() < 2) return nullptr;
  // L'onglet quitte cette fenêtre sans rien demander : son projet, son historique et sa copie de récupération le
  // suivent
  disconnect(f, nullptr, this, nullptr);
  disconnect(f->document(), nullptr, this, nullptr);
  m_onglets->removeTab(index);
  m_pile->removeWidget(f);
  activer(m_onglets->currentIndex());
  auto* autre = new Coquille(false);
  autre->setAttribute(Qt::WA_DeleteOnClose);
  autre->adopter(f);
  // À l'échelle de son écran : un autre écran s'il y en a un, sinon les deux fenêtres partagent celui-ci
  QScreen* ici = screen() ? screen() : QGuiApplication::primaryScreen();
  QScreen* la = nullptr;
  for (QScreen* e : QGuiApplication::screens())
    if (e != ici && !la) la = e;
  if (la) {
    autre->setGeometry(la->availableGeometry());
    autre->m_agrandi = true;
  } else if (ici) {
    const QRect r = ici->availableGeometry();
    const int minimum = std::max(minimumSizeHint().width(), autre->minimumSizeHint().width());
    if (r.width() >= 2 * minimum) {
      // Assez large pour deux : chacune sa moitié
      const QRect gauche(r.topLeft(), QSize(r.width() / 2, r.height())), droite(QPoint(r.left() + r.width() / 2, r.top()), QSize(r.width() - r.width() / 2, r.height()));
      m_geometrieNormale = gauche;
      m_agrandi = false;
      setGeometry(gauche);
      autre->m_geometrieNormale = droite;
      autre->setGeometry(droite);
    } else {
      // Trop étroit pour deux fenêtres utilisables : la nouvelle en cascade, dans l'écran
      const QRect decalee = r.adjusted(std::min(64, r.width() / 10), std::min(48, r.height() / 10), 0, 0);
      autre->m_geometrieNormale = decalee;
      autre->setGeometry(decalee);
    }
    autre->m_agrandi = false;
    m_barre->majTitre();
  }
  autre->show();
  autre->raise();
  autre->activateWindow();
  autre->m_barre->majTitre();
  return autre;
}

void Coquille::ajouterOnglet(FenetrePrincipale* f) {
  brancher(f);
  m_pile->addWidget(f);
  const int i = m_onglets->addTab(icone(QStringLiteral("accueil")), QStringLiteral("Accueil"));
  auto* croix = new QToolButton;
  croix->setObjectName(QStringLiteral("fermerOnglet"));
  marquerAction(croix, "onglet.fermer");
  croix->setIcon(icone(QStringLiteral("croix")));
  croix->setIconSize(QSize(14, 14));
  croix->setAutoRaise(true);
  croix->setToolTip(QStringLiteral("Fermer l’onglet (Ctrl+W)"));
  croix->setStyleSheet(QStringLiteral("QToolButton { border: none; border-radius: 4px; padding: 1px; } QToolButton:hover { background: #F3D3CE; }"));
  connect(croix, &QToolButton::clicked, this, [this, f] { fermerOnglet(m_pile->indexOf(f)); });
  m_onglets->setTabButton(i, QTabBar::RightSide, croix);
  m_onglets->setCurrentIndex(i);
  activer(i);
  majOnglet(f);
}

void Coquille::ouvrirFichier(const QString& chemin) {
  // Déjà ouvert dans un onglet (quel que soit le chemin qui y mène) : on y va
  const QString vise = QFileInfo(chemin).canonicalFilePath();
  for (int i = 0; i < nombreOnglets(); ++i) {
    const QString c = onglet(i) ? onglet(i)->document()->chemin() : QString();
    if (!c.isEmpty() && (c == chemin || (!vise.isEmpty() && QFileInfo(c).canonicalFilePath() == vise))) {
      m_onglets->setCurrentIndex(i);
      return;
    }
  }
  FenetrePrincipale* f = active() && active()->document()->estVierge() ? active() : nouvelOnglet();
  f->ouvrirFichier(chemin);
  f->montrerProjet();
  majOnglet(f);
}

bool Coquille::fermerOnglet(int index) {
  FenetrePrincipale* f = onglet(index);
  if (!f) return false;
  m_onglets->setCurrentIndex(index);
  if (!f->confirmerAbandonPublic()) return false;
  Recuperation::instance().oublier(*f->document());
  m_onglets->removeTab(index);
  m_pile->removeWidget(f);
  f->deleteLater();
  if (m_onglets->count() == 0) nouvelOnglet();  // toujours au moins l'accueil
  else activer(m_onglets->currentIndex());
  return true;
}

void Coquille::activer(int index) {
  FenetrePrincipale* f = onglet(index);
  if (!f) return;
  m_pile->setCurrentWidget(f);
  // Les menus et les accès rapides de la barre de titre sont ceux de l'onglet actif
  QMenuBar* mb = m_barre->menus();
  mb->clear();
  for (QMenu* m : f->menusPrincipaux()) mb->addMenu(m);
  m_barre->definirActions(f->actionsRapides());
  setWindowTitle(f->windowTitle());
  setWindowModified(f->isWindowModified());
  m_barre->majTitre();
  m_etat->setText(f->texteEtat());
  m_etat->setToolTip(f->texteEtat());
}

void Coquille::majOnglet(FenetrePrincipale* f) {
  const int i = m_pile->indexOf(f);
  if (i < 0) return;
  const Document* d = f->document();
  QString t = d->estVierge() ? QStringLiteral("Accueil") : !d->chemin().isEmpty() ? QFileInfo(d->chemin()).completeBaseName() : QString::fromStdString(d->projet().nom);
  if (d->modifie() && !d->estVierge()) t += QStringLiteral(" •");
  m_onglets->setTabText(i, t);
  // Icône du métier : accueil, électronique seule, mécanique seule, ou plusieurs métiers
  const auto& p = d->projet();
  const bool meca = !p.pieces.empty() || !p.assemblages.empty(), elec = !p.electronique.empty() || !p.sansCarte;
  const QString ic = d->estVierge() ? QStringLiteral("accueil") : (meca && elec) || !p.sansCarte ? QStringLiteral("metier-mixte") : elec ? QStringLiteral("pcb") : QStringLiteral("piece");
  m_onglets->setTabIcon(i, icone(ic));
  m_onglets->setTabToolTip(i, d->chemin().isEmpty() ? QStringLiteral("Pas encore enregistré") : d->chemin());
  if (f == active()) {
    setWindowTitle(f->windowTitle());
    setWindowModified(f->isWindowModified());
    m_barre->majTitre();
  }
}

void Coquille::closeEvent(QCloseEvent* e) {
  // Chaque onglet modifié demande s'il faut l'enregistrer ; un refus annule la fermeture
  for (int i = nombreOnglets() - 1; i >= 0; --i) {
    m_onglets->setCurrentIndex(i);
    if (onglet(i) && !onglet(i)->confirmerAbandonPublic()) { e->ignore(); return; }
  }
  // Fermeture normale de la dernière fenêtre : plus rien à récupérer
  bool autreOuverte = false;
  for (Coquille* k : registre()) autreOuverte |= k != this && k->isVisible();
  if (!autreOuverte) Recuperation::instance().terminer();
  e->accept();
}

void Coquille::demarrerRecuperation(bool proposer) {
  Recuperation::instance().demarrer();
  if (!m_minuterieRecuperation) {
    m_minuterieRecuperation = new QTimer(this);
    bool ok = false;
    const int secondes = qEnvironmentVariableIntValue("PROMETHEE_INTERVALLE_RECUPERATION", &ok);
    m_minuterieRecuperation->setInterval((ok && secondes > 0 ? secondes : 120) * 1000);
    connect(m_minuterieRecuperation, &QTimer::timeout, this, &Coquille::sauvegarderRecuperation);
    m_minuterieRecuperation->start();
  }
  if (proposer) QTimer::singleShot(300, this, [this] { proposerRecuperation(); });
}

void Coquille::sauvegarderRecuperation() {
  int copies = 0;
  // Les onglets de toutes les fenêtres, détachées comprises
  for (Coquille* k : registre())
    for (int i = 0; i < k->nombreOnglets(); ++i)
      if (FenetrePrincipale* f = k->onglet(i)) copies += Recuperation::instance().sauvegarder(*f->document()) ? 1 : 0;
  if (copies) journal("récupération : " + std::to_string(copies) + " projet(s) modifié(s) copié(s)");
}

int Coquille::proposerRecuperation() {
  const auto copies = Recuperation::instance().orphelines();
  if (copies.empty()) return 0;
  QDialog d(this);
  d.setWindowTitle(QStringLiteral("Récupération de projets"));
  auto* col = new QVBoxLayout(&d);
  auto* texte = new QLabel(QStringLiteral("<b>Prométhée ne s’est pas fermé normalement</b> (plantage, arrêt forcé ou coupure).<br>"
                                          "Ces projets avaient des modifications non enregistrées ; leur dernière copie peut être rouverte. "
                                          "Le fichier d’origine n’est pas modifié tant que tu n’enregistres pas."));
  texte->setWordWrap(true);
  col->addWidget(texte);
  auto* liste = new QListWidget;
  for (const auto& c : copies) {
    const QString origine = c.cheminOrigine.isEmpty() ? QStringLiteral("jamais enregistré") : QDir::toNativeSeparators(c.cheminOrigine);
    auto* el = new QListWidgetItem(QStringLiteral("%1 — copie du %2\n%3").arg(c.nom.isEmpty() ? QStringLiteral("Projet sans nom") : c.nom,
                                                                            QLocale().toString(c.date, QLocale::ShortFormat), origine));
    el->setFlags(el->flags() | Qt::ItemIsUserCheckable);
    el->setCheckState(Qt::Checked);
    liste->addItem(el);
  }
  col->addWidget(liste);
  auto* boutons = new QDialogButtonBox;
  QPushButton* recuperer = boutons->addButton(QStringLiteral("Rouvrir les projets cochés"), QDialogButtonBox::AcceptRole);
  QPushButton* plusTard = boutons->addButton(QStringLiteral("Plus tard"), QDialogButtonBox::RejectRole);
  QPushButton* abandonner = boutons->addButton(QStringLiteral("Supprimer ces copies"), QDialogButtonBox::DestructiveRole);
  recuperer->setObjectName(QStringLiteral("principal"));
  QAbstractButton* clic = nullptr;
  connect(boutons, &QDialogButtonBox::clicked, &d, [&](QAbstractButton* b) { clic = b; d.accept(); });
  col->addWidget(boutons);
  d.resize(620, 360);
  d.exec();
  if (clic == plusTard || !clic) return 0;
  int rouverts = 0;
  for (int i = 0; i < liste->count(); ++i) {
    const auto& c = copies[static_cast<size_t>(i)];
    if (clic == abandonner) {
      Recuperation::instance().abandonner(c);
      continue;
    }
    if (liste->item(i)->checkState() != Qt::Checked) continue;
    try {
      const std::string contenu = prom::lireFichier(prom::cheminFichier(c.fichier.toStdString()));
      FenetrePrincipale* f = active() && active()->document()->estVierge() ? active() : nouvelOnglet();
      f->restaurerCopie(contenu, c.cheminOrigine);
      majOnglet(f);
      Recuperation::instance().abandonner(c);
      Recuperation::instance().sauvegarder(*f->document());  // la copie passe dans la session en cours
      ++rouverts;
    } catch (const std::exception& e) {
      journal(std::string("récupération impossible : ") + e.what());
    }
  }
  journal("récupération : " + std::to_string(rouverts) + " projet(s) rouvert(s)");
  return rouverts;
}

Qt::Edges Coquille::bordsSous(const QPoint& global) const {
  Qt::Edges b;
  if (!m_cadreIntegre || isMaximized() || m_agrandi || !isVisible()) return b;
  const QRect r = frameGeometry();
  const int m = 6;
  if (!r.adjusted(-1, -1, 1, 1).contains(global)) return b;
  if (global.x() <= r.left() + m) b |= Qt::LeftEdge;
  if (global.x() >= r.right() - m) b |= Qt::RightEdge;
  if (global.y() <= r.top() + m) b |= Qt::TopEdge;
  if (global.y() >= r.bottom() - m) b |= Qt::BottomEdge;
  return b;
}

// Redimensionnement par les bords d'une fenêtre sans cadre : on surveille la souris dans toute l'application.
bool Coquille::eventFilter(QObject* o, QEvent* e) {
  // Un onglet glissé loin de la barre (plus haut que la moitié de la fenêtre) se détache
  if (o == m_onglets) {
    if (e->type() == QEvent::MouseButtonPress) {
      auto* me = static_cast<QMouseEvent*>(e);
      m_ongletPresse = me->button() == Qt::LeftButton ? m_onglets->tabAt(me->position().toPoint()) : -1;
    } else if (e->type() == QEvent::MouseButtonRelease && m_ongletPresse >= 0) {
      auto* me = static_cast<QMouseEvent*>(e);
      const int i = m_onglets->currentIndex();  // l'onglet saisi, même s'il a glissé de place dans la barre
      m_ongletPresse = -1;
      if (me->position().y() < -height() / 2 && m_onglets->count() > 1) {
        QTimer::singleShot(0, this, [this, i] { detacher(std::min(i, m_onglets->count() - 1)); });
      }
    }
  }
  if (m_cadreIntegre && (e->type() == QEvent::MouseMove || e->type() == QEvent::MouseButtonPress)) {
    auto* w = qobject_cast<QWidget*>(o);
    if (w && w->window() == this) {
      auto* me = static_cast<QMouseEvent*>(e);
      const Qt::Edges b = bordsSous(me->globalPosition().toPoint());
      if (e->type() == QEvent::MouseMove && me->buttons() == Qt::NoButton) {
        static bool forme = false;
        if (b) {
          const Qt::CursorShape c = (b == (Qt::LeftEdge | Qt::TopEdge) || b == (Qt::RightEdge | Qt::BottomEdge)) ? Qt::SizeFDiagCursor
                                  : (b == (Qt::RightEdge | Qt::TopEdge) || b == (Qt::LeftEdge | Qt::BottomEdge)) ? Qt::SizeBDiagCursor
                                  : (b & (Qt::LeftEdge | Qt::RightEdge)) ? Qt::SizeHorCursor : Qt::SizeVerCursor;
          if (forme) QApplication::changeOverrideCursor(c); else QApplication::setOverrideCursor(c);
          forme = true;
        } else if (forme) {
          QApplication::restoreOverrideCursor();
          forme = false;
        }
      }
      if (e->type() == QEvent::MouseButtonPress && me->button() == Qt::LeftButton && b && windowHandle()) {
        windowHandle()->startSystemResize(b);
        return true;
      }
    }
  }
  return QWidget::eventFilter(o, e);
}
