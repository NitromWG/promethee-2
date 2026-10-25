// Prométhée : essais de l'atelier électronique, dans la vraie fenêtre, par de vrais gestes de souris et de clavier
// (lot L1, tâche 1.1, Q1) : rien n'y émet de signal ni ne déclenche d'action à la main. Un parcours complet : nouvelle
// carte depuis l'accueil, composants pris dans la bibliothèque, fils tirés d'une broche à l'autre, annotation,
// vérification électrique, valeur changée dans le panneau, annuler et rétablir, mise à jour du circuit, empreinte
// glissée à la souris, routage, plan de masse dessiné, DRC, composant créé par l'utilisateur, dessin PDF, dossier
// de fabrication, publication verrouillée puis nouvelle révision, arbre du projet, 3D, enregistrement et
// réouverture, exemple livré, suppression. Licence GPL-3.0-only. Lancés par « Promethee --essais-electronique » ;
// le code de sortie est le nombre d'échecs. Avec --onglets, le même parcours tourne dans la fenêtre à onglets.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <string>

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QTableWidget>
#include <memory>

#include <QInputDialog>
#include <QMenu>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidgetItemIterator>

#include "AtelierElectronique.hpp"
#include "ChronoComposition.hpp"
#include "DialogueComposant.hpp"
#include "Document.hpp"
#include "EssaisInterface.hpp"
#include "FenetrePrincipale.hpp"
#include "InspecteurElectronique.hpp"
#include "TableauChamps.hpp"
#include "DialogueEmpreinte.hpp"
#include "EvenementsReels.hpp"
#include "Journal.hpp"
#include "Navigateur.hpp"
#include "Nomenclature.hpp"
#include "RegistreActions.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"
#include "promethee/carte3d.hpp"
#include "promethee/modele.hpp"
#include "promethee/piece.hpp"

using namespace prom::ecad;

namespace {
int g_echecs = 0;

// Accepte la prochaine boîte de saisie (choix d'un réseau…) dès qu'elle s'affiche ; son texte est rendu
void accepterSaisie(QString* texte = nullptr) {
  auto* t = new QTimer(qApp);
  t->setInterval(100);
  auto essais = std::make_shared<int>(0);
  QObject::connect(t, &QTimer::timeout, qApp, [t, essais, texte] {
    for (QWidget* w : QApplication::topLevelWidgets())
      if (auto* d = qobject_cast<QInputDialog*>(w); d && d->isVisible()) {
        if (texte) *texte = d->textValue();
        reel::touche(d, Qt::Key_Return);  // OK, le bouton par défaut
        t->stop();
        t->deleteLater();
        return;
      }
    if (++*essais > 40) { t->stop(); t->deleteLater(); }
  });
  t->start();
}
// Tape « texte » dans la prochaine boîte de saisie, puis Entrée
void taperDansSaisie(const QString& texte) {
  QTimer::singleShot(300, [texte] {
    for (QWidget* w : QApplication::topLevelWidgets())
      if (auto* d = qobject_cast<QInputDialog*>(w); d && d->isVisible())
        if (auto* l = d->findChild<QLineEdit*>()) {
          l->selectAll();
          reel::taper(l, texte);
          reel::touche(d, Qt::Key_Return);
          return;
        }
  });
}
void verifier(bool ok, const std::string& quoi, const std::string& detail = {}) {
  const std::string ligne = (ok ? "OK      " : "ÉCHEC   ") + quoi + (detail.empty() ? "" : "  (" + detail + ")");
  std::printf("%s\n", ligne.c_str());
  std::fflush(stdout);
  journal(("essai électronique : " + ligne).c_str());
  if (!ok) ++g_echecs;
}

void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}

QPoint ecran(VueSchema* v, Pt p) { return v->versEcran(p).toPoint(); }
QPoint ecran(VueCircuit* v, Pt p) { return v->versEcran(p).toPoint(); }
void cliquer(QWidget* w, QPoint p) {
  reel::cliquer(w, p);
  attendre(60);
}
// Une touche va à la fenêtre active : comme un utilisateur qui revient d'une boîte de dialogue, l'essai rend d'abord
// la main à la fenêtre de « w » si elle l'a perdue
void touche(QWidget* w, int k, const QString& texte = {}, Qt::KeyboardModifiers mods = Qt::NoModifier) {
  if (QApplication::activeWindow() != w->window() && !QApplication::activeModalWidget()) reel::activer(w);
  if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL") && (mods & Qt::ControlModifier))
    std::printf("  (Ctrl+%c : focus %s %s)\n", static_cast<char>(k), QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "-",
                QApplication::focusWidget() ? QApplication::focusWidget()->objectName().toStdString().c_str() : "");
  reel::touche(w, k, mods, texte);
  attendre(20);
}

// Clic sur l'action « id » du ruban : son bouton, sinon la ligne du menu d'un bouton, ouvert par sa flèche ; un ruban
// trop étroit range le reste dans son prolongement
bool cliquerRuban(AtelierElectronique* a, const char* id) {
  QTabWidget* ruban = nullptr;
  for (QTabWidget* t : a->findChildren<QTabWidget*>())
    if (t->count() == 4 && t->tabText(0) == QStringLiteral("Schéma")) ruban = t;
  auto* barre = ruban ? qobject_cast<QToolBar*>(ruban->currentWidget()) : nullptr;
  if (!barre) return false;
  for (int essai = 0; essai < 2; ++essai) {
    for (QToolButton* b : barre->findChildren<QToolButton*>()) {
      if (!b->isVisible()) continue;
      if (b->defaultAction() && b->defaultAction()->isEnabled() && actionsMarquees(b->defaultAction()).contains(QLatin1String(id))) {
        reel::cliquer(b, QPoint(b->width() / 3, b->height() / 2));
        attendre(250);
        return true;
      }
      if (QMenu* m = b->menu())
        for (QAction* x : m->actions())
          if (x->isEnabled() && actionsMarquees(x).contains(QLatin1String(id))) {
            // Le menu s'ouvre dans sa propre boucle : la ligne est cliquée pendant qu'il est ouvert
            bool fait = false;
            QTimer::singleShot(250, [m, x, &fait] {
              if (!m->isVisible()) return;
              reel::cliquer(m, m->actionGeometry(x).center());
              fait = true;
            });
            reel::cliquer(b, QPoint(b->width() - 5, b->height() / 2));
            attendre(300);
            if (m->isVisible()) m->close();
            return fait;
          }
    }
    auto* suite = barre->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button"));
    if (!suite || !suite->isVisible()) break;
    reel::cliquer(suite, suite->rect().center());
    attendre(150);
  }
  return false;
}

// Clic sur un élément d'un arbre ou d'une liste, amené à l'écran d'abord
void cliquerElement(QTreeWidget* t, QTreeWidgetItem* it, Qt::MouseButton bouton = Qt::LeftButton) {
  // Sa branche dépliée, comme d'un clic sur la flèche de chaque parent
  bool deplie = false;
  for (QTreeWidgetItem* p = it->parent(); p; p = p->parent())
    if (!p->isExpanded()) {
      p->setExpanded(true);
      deplie = true;
    }
  if (deplie) attendre(400);  // l'arbre est animé : un clic pendant le dépliage serait perdu
  // Amené dans la vue (une mise en page en attente peut encore faire défiler l'arbre)
  for (int i = 0; i < 4; ++i) {
    t->scrollToItem(it);
    attendre(40);
    if (t->viewport()->rect().contains(t->visualItemRect(it).center())) break;
  }
  if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL")) {
    const QRect r = t->visualItemRect(it);
    QWidget* haut = t->window();
    const QPoint dansHaut = t->viewport()->mapTo(haut, r.center());
    QWidget* sous = haut->childAt(dansHaut);
    std::printf("  (capture %s, menu %s, modal %s, fenêtre active %s)\n", QWidget::mouseGrabber() ? QWidget::mouseGrabber()->metaObject()->className() : "-",
                QApplication::activePopupWidget() ? QApplication::activePopupWidget()->metaObject()->className() : "-",
                QApplication::activeModalWidget() ? QApplication::activeModalWidget()->metaObject()->className() : "-",
                QApplication::activeWindow() ? QApplication::activeWindow()->metaObject()->className() : "-");
    std::printf("  (clic sur « %s » : %d,%d %dx%d dans %dx%d ; sous le point : %s %s, visible %d)\n", it->text(0).toStdString().c_str(), r.x(), r.y(), r.width(), r.height(),
                t->viewport()->width(), t->viewport()->height(), sous ? sous->metaObject()->className() : "-", sous ? (sous == t->viewport() ? "(celle de l'arbre)" : "(une autre)") : "", t->isVisible());
  }
  reel::cliquer(t->viewport(), t->visualItemRect(it).center(), bouton);
  attendre(60);
}
// Clic droit sur un élément de l'arbre, puis clic sur la ligne « ligne » du menu qui s'ouvre
bool menuElement(QTreeWidget* t, QTreeWidgetItem* it, const QString& ligne) {
  bool fait = false;
  QTimer::singleShot(250, [&fait, ligne] {
    for (QWidget* w : QApplication::topLevelWidgets())
      if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible()) {
        for (QAction* x : m->actions())
          if (x->text() == ligne && x->isEnabled()) {
            reel::cliquer(m, m->actionGeometry(x).center());
            fait = true;
            return;
          }
        m->close();
      }
  });
  cliquerElement(t, it, Qt::RightButton);
  attendre(300);
  return fait;
}
// Saisie dans un champ comme au clavier : clic dedans, texte choisi, frappe, Entrée ; puis Échap rend la main au
// canevas (charte), pour que Ctrl+Z aille à l'historique et non au champ
void saisir(QWidget* champ, const QString& texte) {
  QPointer<QWidget> garde(champ);
  QWidget* haut = champ->window();
  reel::cliquer(champ, champ->rect().center());
  if (auto* l = qobject_cast<QLineEdit*>(garde.data())) l->selectAll();
  if (auto* b = qobject_cast<QAbstractSpinBox*>(garde.data())) b->selectAll();
  if (garde) reel::taper(garde, texte);
  if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL"))
    std::printf("  (saisie « %s » : champ %s, focus %s, texte « %s »)\n", texte.toStdString().c_str(), garde ? "présent" : "détruit",
                QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "-",
                garde && qobject_cast<QLineEdit*>(garde.data()) ? qobject_cast<QLineEdit*>(garde.data())->text().toStdString().c_str() : "?");
  reel::touche(garde ? garde.data() : haut, Qt::Key_Return);
  attendre(150);
  reel::touche(garde ? garde.data() : haut, Qt::Key_Escape);
  attendre(80);
}
// Point de la feuille sous le point « q » de la vue, sur la grille de 2,54 mm : là où poser quelque chose de visible
Pt sousEcran(VueSchema* v, QPointF q) {
  const QPointF o = v->versEcran(Pt{0, 0}), u = v->versEcran(ptMm(10, 10));
  const double sx = (u.x() - o.x()) / 10, sy = (u.y() - o.y()) / 10;
  auto grille = [](double x) { return std::round(x / 2.54) * 2.54; };
  return ptMm(grille((q.x() - o.x()) / sx), grille((q.y() - o.y()) / sy));
}
// Dans la fenêtre à onglets, un projet créé depuis l'accueil part dans un onglet neuf quand celui-ci en porte déjà
// un (ce que --essais-onglets essaie) : le parcours libère d'abord son onglet, pour y rester
void libererOnglet(FenetrePrincipale& f) {
  if (f.isWindow()) return;
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  f.document()->creer(p);
  attendre(200);
}

// Répond à la prochaine boîte de message par le bouton de ce rôle (Oui, Ne pas enregistrer…)
void repondre(QMessageBox::ButtonRole role) {
  QTimer::singleShot(300, [role] {
    if (auto* m = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
      for (QAbstractButton* b : m->buttons())
        if (m->buttonRole(b) == role) {
          reel::cliquer(b, b->rect().center());
          return;
        }
  });
}

const ConceptionElec* conception(FenetrePrincipale& f, int i = 0) {
  const auto& e = f.document()->projet().electronique;
  return i >= 0 && i < static_cast<int>(e.size()) ? &e[static_cast<std::size_t>(i)] : nullptr;
}

std::string symboleDe(const ConceptionElec& c, const std::string& idSymbole) {
  for (const auto& s : c.feuilles[0].symboles)
    if (s.symbole == idSymbole) return s.uuid;
  return {};
}

Pt broche(const ConceptionElec& c, const std::string& symbole, const std::string& numero) {
  for (const auto& s : c.feuilles[0].symboles)
    if (s.uuid == symbole)
      if (const DefSymbole* d = c.symbole(s.symbole))
        for (const auto& b : brochesPlacees(s, *d))
          if (b.broche->numero == numero) return b.p;
  return {};
}

// Pose un symbole comme l'utilisateur : A ouvre la bibliothèque, double-clic sur l'article, R pour tourner, clic sur
// la feuille, Échap
bool poserDepuisBibliotheque(AtelierElectronique* a, const QString& nom, Pt ou, int quarts = 0) {
  VueSchema* vs = a->vueSchema();
  vs->setFocus(Qt::OtherFocusReason);
  touche(vs, Qt::Key_A, QStringLiteral("a"));
  attendre(150);
  QTreeWidget* arbre = nullptr;
  QTreeWidgetItem* cible = nullptr;
  for (QTreeWidget* t : a->findChildren<QTreeWidget*>())
    if (t->isVisible() && t->topLevelItemCount() > 0 && !qobject_cast<Navigateur*>(t))
      for (QTreeWidgetItemIterator it(t); *it; ++it)
        if ((*it)->text(0) == nom) {
          arbre = t;
          cible = *it;
        }
  if (!cible) {
    std::printf("  (pose : « %s » introuvable dans la bibliothèque)\n", nom.toStdString().c_str());
    return false;
  }
  // L'article amené dans la vue de l'arbre (une mise en page en attente peut encore la faire défiler)
  auto amener = [arbre, cible] {
    for (int i = 0; i < 4; ++i) {
      arbre->scrollToItem(cible);
      attendre(40);
      if (arbre->viewport()->rect().contains(arbre->visualItemRect(cible).center())) return;
    }
  };
  // Un clic choisit l'article (l'aperçu se met à jour, le panneau peut bouger), puis le double-clic arme la pose
  amener();
  cliquer(arbre->viewport(), arbre->visualItemRect(cible).center());
  amener();
  const bool courant = arbre->currentItem() == cible;
  const QRect r = arbre->visualItemRect(cible);
  reel::doubleCliquer(arbre->viewport(), r.center());
  attendre(120);
  VueSchema::Outil outil = vs->outil();
  if (outil == VueSchema::Outil::Selection) {
    // Rare sous Xvfb : le double-clic n'a pas pris ; on le dit, puis on double-clique de nouveau, comme on le ferait
    std::printf("  (double-clic sur « %s » sans effet : courant %d, rectangle %d,%d %dx%d, vue %dx%d, courant après %s ; repris)\n", nom.toStdString().c_str(), courant, r.x(), r.y(),
                r.width(), r.height(), arbre->viewport()->width(), arbre->viewport()->height(), arbre->currentItem() ? arbre->currentItem()->text(0).toStdString().c_str() : "-");
    amener();
    reel::doubleCliquer(arbre->viewport(), arbre->visualItemRect(cible).center());
    attendre(120);
    outil = vs->outil();
  }
  for (int i = 0; i < quarts; ++i) touche(vs, Qt::Key_R, QStringLiteral("r"));
  const QPoint q = ecran(vs, ou);
  const bool dedans = vs->rect().contains(q);
  cliquer(vs, q);
  attendre(60);
  touche(vs, Qt::Key_Escape);
  if (outil == VueSchema::Outil::Selection || !dedans)
    std::printf("  (pose de « %s » : outil %d, point %d,%d %s de la vue, focus %s)\n", nom.toStdString().c_str(), static_cast<int>(outil), q.x(), q.y(), dedans ? "dans" : "hors",
                QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "-");
  return true;
}
}  // namespace

void essaisElectroniqueL1(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL2(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL3(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL4(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL5(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectronique034(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectronique034L4(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectronique034L5(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectronique034L6(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectronique034L7(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectronique034L8(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectronique034L10(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectronique034L11(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL6(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL7(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL8(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL9(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL10(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL11(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void essaisElectroniqueL12(FenetrePrincipale& f, const std::function<void(bool, const std::string&, const std::string&)>& verifier);
void profilerClicsL9(FenetrePrincipale& f);
void profilerGlisseL9(FenetrePrincipale& f);

namespace {
// Objectifs de la charte d'interaction : 16 ms pour un retour de souris, 50 ms pour un clic, 100 ms pour une
// synchronisation, 30 ms pour une valeur affichée, à 300 composants (ERG-10)
void mesure(const char* quoi, double ms, double objectif, double pose = 0) {
  // Le travail de Prométhée (événements traités, widgets peints) est comparé à l'objectif ; la pose des images par
  // la pile graphique est dite à part (rendu logiciel sans carte graphique : plus de 16 ms à elle seule)
  const double travail = ms - pose;
  const QString detail = pose > 0.05 ? QStringLiteral(", travail %1 ms + pose de l'image %2 ms").arg(travail, 0, 'f', 1).arg(pose, 0, 'f', 1) : QString();
  std::printf("MESURE  %-62s %8.1f ms%s%s\n", quoi, ms, detail.toUtf8().constData(),
              objectif > 0 ? (travail > objectif ? QStringLiteral("  (objectif %1 ms : dépassé)").arg(objectif).toUtf8().constData() : QStringLiteral("  (objectif %1 ms)").arg(objectif).toUtf8().constData()) : "");
  std::fflush(stdout);
  // Lot L9 (9.1, AUT-12) : un travail qui dépasse son objectif de plus de 20 % fait échouer les essais, donc la
  // construction ; PROMETHEE_MESURES_INDICATIVES=1 les garde indicatifs (machine lente)
  if (objectif > 0 && travail > 1.2 * objectif && qEnvironmentVariableIsEmpty("PROMETHEE_MESURES_INDICATIVES"))
    verifier(false, std::string("mesure dans son objectif (+20 %) : ") + quoi,
             QStringLiteral("travail %1 ms pour %2 ms (pose de l'image %3 ms en plus)").arg(travail, 0, 'f', 1).arg(objectif).arg(pose, 0, 'f', 1).toStdString());
}

void mesurerAtelier(FenetrePrincipale& f) {
  using H = std::chrono::steady_clock;
  auto depuis = [](H::time_point t0) { return std::chrono::duration<double, std::milli>(H::now() - t0).count(); };
  composition::activer(true);
  double c0 = 0;
  auto pose = [&c0] { return composition::cumul() - c0; };
  libererOnglet(f);
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.electronique.push_back(carteDeMesure(300));
  p.nom = p.electronique.front().nom;
  auto t0 = H::now();
  c0 = composition::cumul();
  f.document()->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Schema);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return;
  a->recalculer();
  VueSchema* vs = a->vueSchema();
  vs->repaint();
  mesure("ouverture de la carte (schéma, calcul complet, première image)", depuis(t0), 0);
  reel::activer(&f);
  vs->setFocus(Qt::OtherFocusReason);
  touche(vs, Qt::Key_Home);
  attendre(100);
  t0 = H::now();
  c0 = composition::cumul();
  for (int i = 0; i < 10; ++i) vs->repaint();
  mesure("image du schéma (une feuille de 100 composants)", depuis(t0) / 10, 16, pose() / 10);
  // Survol (2.4) : la souris passe d'une résistance à l'autre, une image par mouvement
  {
    const ConceptionElec& cs = f.document()->projet().electronique.front();
    std::vector<Pt> centres;
    for (const auto& sp : cs.feuilles[0].symboles)
      if (sp.symbole == "R" && centres.size() < 2)
        if (const DefSymbole* d = cs.symbole(sp.symbole)) centres.push_back(boiteSymbole(sp, *d).centre());
    if (centres.size() == 2) {
      t0 = H::now();
      c0 = composition::cumul();
      for (int i = 0; i < 10; ++i) {
        reel::deplacer(vs, ecran(vs, centres[static_cast<std::size_t>(i % 2)]));
        vs->repaint();
      }
      mesure("survol : une image par mouvement de la souris", depuis(t0) / 10, 16, pose() / 10);
    }
  }
  // Clic sur une résistance : choisie, inspecteur et image à jour
  const ConceptionElec& c = f.document()->projet().electronique.front();
  const SymbolePlace* r = nullptr;
  for (const auto& sp : c.feuilles[0].symboles)
    if (!r && sp.symbole == "R") r = &sp;
  // Le premier clic construit la page du composant dans l'inspecteur (une fois) : il est dit à part ; l'objectif porte
  // sur la médiane de cinq clics d'une résistance à l'autre
  using Temps = std::pair<double, double>;  // total, pose de l'image
  auto mediane = [](std::vector<Temps> t) {
    std::sort(t.begin(), t.end(), [](const Temps& x, const Temps& y) { return x.first - x.second < y.first - y.second; });
    return t[t.size() / 2];
  };
  std::vector<QPoint> resistances;
  for (const auto& sp : c.feuilles[0].symboles)
    if (sp.symbole == "R" && resistances.size() < 2)
      if (const DefSymbole* d = c.symbole(sp.symbole)) resistances.push_back(ecran(vs, boiteSymbole(sp, *d).centre()));
  if (r && resistances.size() == 2) {
    const DefSymbole* d = c.symbole(r->symbole);
    const QPoint q = ecran(vs, d ? boiteSymbole(*r, *d).centre() : r->position);
    t0 = H::now();
    c0 = composition::cumul();
    reel::cliquer(vs, q);
    vs->repaint();
    mesure("premier clic sur une résistance : page de l'inspecteur construite", depuis(t0), 0, pose());
    std::vector<Temps> clics;
    for (int i = 1; i <= 5; ++i) {
      t0 = H::now();
      c0 = composition::cumul();
      reel::cliquer(vs, resistances[static_cast<std::size_t>(i % 2)]);
      vs->repaint();
      clics.push_back({depuis(t0), pose()});
    }
    const Temps m = mediane(clics);
    mesure("clic sur une résistance : choisie, inspecteur et image à jour", m.first, 50, m.second);
    reel::cliquer(vs, q);
  }
  // Valeur changée dans l'inspecteur : modèle, ERC seule (A12), feuille redessinée
  if (QLineEdit* v = a->findChild<QLineEdit*>(QStringLiteral("champ:valeur")); v && v->isVisible()) {
    reel::cliquer(v, v->rect().center());
    v->selectAll();
    t0 = H::now();
    c0 = composition::cumul();
    reel::taper(v, QStringLiteral("4"));
    a->recalculerApresModification();
    vs->repaint();
    mesure("frappe dans Valeur : modèle, vérification et feuille à jour", depuis(t0), 30, pose());
    reel::touche(v, Qt::Key_Escape);
    attendre(80);
  }
  // Fantôme d'une pose qui suit la souris
  touche(vs, Qt::Key_Escape);
  touche(vs, Qt::Key_Escape);
  if (poserDepuisBibliotheque(a, QStringLiteral("Résistance"), sousEcran(vs, QPointF(vs->width() * 0.5, vs->height() * 0.5)))) {
    touche(vs, Qt::Key_Z, {}, Qt::ControlModifier);
    attendre(100);
  }
  // F8 sans rien à changer, trois fois depuis le schéma (médiane) : le circuit se montre aussitôt, son calcul
  // complet suit en fond
  {
    std::vector<Temps> f8;
    for (int i = 0; i < 3; ++i) {
      a->setMode(AtelierElectronique::Schema);
      for (const auto attente = H::now(); a->calculEnCours() && depuis(attente) < 60000;) attendre(20);
      attendre(100);
      vs->setFocus(Qt::OtherFocusReason);
      t0 = H::now();
      c0 = composition::cumul();
      touche(vs, Qt::Key_F8);
      f8.push_back({depuis(t0), pose()});
    }
    const Temps m = mediane(f8);
    mesure("F8 : synchronisation sans rien à changer, circuit montré", m.first, 100, m.second);
  }
  attendre(200);
  // Circuit : calcul complet (plan de masse, DRC), image, glissement d'une empreinte
  VueCircuit* vc = a->vueCircuit();
  a->setMode(AtelierElectronique::Circuit);
  t0 = H::now();
  c0 = composition::cumul();
  a->recalculer();
  mesure("circuit : calcul complet (connectivité, plan de masse, DRC)", depuis(t0), 0);
  vc->setFocus(Qt::OtherFocusReason);
  touche(vc, Qt::Key_Home);
  attendre(100);
  t0 = H::now();
  c0 = composition::cumul();
  for (int i = 0; i < 10; ++i) vc->repaint();
  mesure("image du circuit (300 empreintes, plan de masse rempli)", depuis(t0) / 10, 16, pose() / 10);
  if (!c.circuit.empreintes.empty()) {
    const Pt e = c.circuit.empreintes[c.circuit.empreintes.size() / 2].position;
    const QPoint depart = ecran(vc, e), arrivee = ecran(vc, e + ptMm(3, 0));
    reel::deplacer(vc, depart);
    reel::appuyer(vc, depart);
    t0 = H::now();
    c0 = composition::cumul();
    for (int i = 1; i <= 10; ++i) {
      reel::deplacer(vc, depart + (arrivee - depart) * i / 10);
      vc->repaint();
    }
    mesure("glissement d'une empreinte : une image par mouvement", depuis(t0) / 10, 16, pose() / 10);
    reel::relacher(vc, arrivee);
    t0 = H::now();
    c0 = composition::cumul();
    while (a->calculEnCours() && depuis(t0) < 60000) attendre(20);
    mesure("fin du glissement : plan de masse et DRC de fond refaits", depuis(t0), 0);
    touche(vc, Qt::Key_Z, {}, Qt::ControlModifier);
    attendre(200);
  }
  // A2 (1.2) : une valeur changée sur la carte coûte autant avec un assemblage de 50 pièces dans le projet : seule la
  // conception touchée est photographiée, et la mécanique n'est pas recalculée (médiane de neuf modifications)
  Document* doc = f.document();
  auto coutValeur = [&]() {
    std::vector<double> t;
    for (int i = 0; i < 9; ++i) {
      const auto debut = H::now();
      doc->modifierConception(0, [i](ConceptionElec& x) { x.composants[1].valeur = std::to_string(10 + i) + "k"; });
      t.push_back(depuis(debut));
      attendre(80);  // le calcul différé passe
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
  };
  const double seule = coutValeur();
  doc->modifier([](prom::Projet& pr) {
    prom::Piece plaque;
    prom::FonctionPiece e;
    e.type = prom::TypeFonction::Esquisse;
    e.esquisse.ajouterRectangle(0, 0, 40, 30);
    const int ie = plaque.ajouter(e);
    prom::FonctionPiece x;
    x.type = prom::TypeFonction::Extrusion;
    x.esquisseId = ie;
    x.longueur = 5;
    plaque.ajouter(x);
    prom::Assemblage as;
    as.nom = "Pile de 50 plaques";
    for (int i = 0; i < 50; ++i) {
      plaque.nom = "Plaque " + std::to_string(i + 1);
      pr.pieces.push_back(prom::pieceVersJson(plaque));
      prom::ComposantAssemblage k;
      k.piece = static_cast<int>(pr.pieces.size()) - 1;
      k.z = 5.0 * i;
      k.fixe = i == 0;
      as.composants.push_back(k);
    }
    pr.assemblages.push_back(as);
  });
  attendre(500);
  const double avecAssemblage = coutValeur();
  mesure("valeur changée, carte seule (document)", seule, 0);
  mesure("valeur changée, avec un assemblage de 50 pièces (document)", avecAssemblage, 0);
  verifier(avecAssemblage <= seule * 1.5 + 2, "A2 : sur une carte de 300 composants, une valeur changée ne coûte pas plus avec un assemblage de 50 pièces dans le projet",
           std::to_string(seule) + " ms puis " + std::to_string(avecAssemblage) + " ms");
  composition::activer(false);
}
}  // namespace

// Profilage (PROMETHEE_ESSAIS_LOTS=PROFIL300) : clics sur des résistances et F8 répétés sur la carte de 300
// composants, chaque temps imprimé, pour échantillonner la pile pendant ce temps
static void profilerAtelier300(FenetrePrincipale& f) {
  using H = std::chrono::steady_clock;
  auto depuis = [](H::time_point t0) { return std::chrono::duration<double, std::milli>(H::now() - t0).count(); };
  libererOnglet(f);
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.electronique.push_back(carteDeMesure(300));
  f.document()->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Schema);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return;
  a->recalculer();
  VueSchema* vs = a->vueSchema();
  reel::activer(&f);
  vs->setFocus(Qt::OtherFocusReason);
  touche(vs, Qt::Key_Home);
  attendre(500);
  const ConceptionElec& c = f.document()->projet().electronique.front();
  std::vector<QPoint> r;
  for (const auto& sp : c.feuilles[0].symboles)
    if (sp.symbole == "R" && r.size() < 2)
      if (const DefSymbole* d = c.symbole(sp.symbole)) r.push_back(ecran(vs, boiteSymbole(sp, *d).centre()));
  std::printf("PROFIL clics\n");
  std::fflush(stdout);
  for (int i = 0; i < 60 && r.size() == 2; ++i) {
    const auto t0 = H::now();
    reel::cliquer(vs, r[static_cast<std::size_t>(i % 2)]);
    vs->repaint();
    std::printf("clic %d : %.1f ms  (%lld objets)\n", i, depuis(t0), static_cast<long long>(f.findChildren<QObject*>().size()));
    // Objets qui s'accumulent d'un clic à l'autre : comptés par classe et par parent au premier et au dixième clic
    if (i == 1 || i == 11) {
      static std::map<std::string, int> avant;
      std::map<std::string, int> n;
      for (QObject* o : f.findChildren<QObject*>())
        ++n[std::string(o->metaObject()->className()) + " < " + (o->parent() ? o->parent()->metaObject()->className() + std::string(" ") + o->parent()->objectName().toStdString() : std::string("-"))];
      if (i == 11)
        for (const auto& [k, v] : n)
          if (v != avant[k]) std::printf("  croissance %+d  %s\n", v - avant[k], k.c_str());
      avant = n;
    }
    std::fflush(stdout);
  }
  std::printf("PROFIL F8\n");
  std::fflush(stdout);
  for (int i = 0; i < 15; ++i) {
    a->setMode(AtelierElectronique::Schema);
    vs->setFocus(Qt::OtherFocusReason);
    // Une valeur changée au schéma : le circuit est à recalculer quand F8 le montre
    f.document()->modifierConception(0, [i](ConceptionElec& x) { x.composants[1].valeur = std::to_string(10 + i) + "k"; });
    attendre(300);
    const auto t0 = H::now();
    touche(vs, Qt::Key_F8);
    std::printf("F8 %d : %.1f ms\n", i, depuis(t0));
    std::fflush(stdout);
  }
  std::printf("PROFIL fin\n");
  std::fflush(stdout);
}

void lancerEssaisElectronique(FenetrePrincipale& f) {
  installerVeilleBoites();
  Document* doc = f.document();
  const QString dossier = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-electronique"));
  QDir(dossier).removeRecursively();
  QDir().mkpath(dossier);
  qputenv("PROMETHEE_BIBLIOTHEQUE", QDir(dossier).filePath(QStringLiteral("bibliotheque.json")).toUtf8());
  // PROMETHEE_ESSAIS_LOTS=L1,L2 : les seuls essais de ces lots, sans le parcours ni les mesures
  if (const QStringList lots = qEnvironmentVariable("PROMETHEE_ESSAIS_LOTS").split(QLatin1Char(','), Qt::SkipEmptyParts); !lots.isEmpty()) {
    verifier(reel::activer(&f), "la fenêtre est active : les touches et les raccourcis lui parviennent");
    const auto v = [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); };
    if (lots.contains(QStringLiteral("L1"))) essaisElectroniqueL1(f, v);
    if (lots.contains(QStringLiteral("L2"))) essaisElectroniqueL2(f, v);
    if (lots.contains(QStringLiteral("L3"))) essaisElectroniqueL3(f, v);
    if (lots.contains(QStringLiteral("L4"))) essaisElectroniqueL4(f, v);
    if (lots.contains(QStringLiteral("L5"))) essaisElectroniqueL5(f, v);
    if (lots.contains(QStringLiteral("034"))) essaisElectronique034(f, v);
    if (lots.contains(QStringLiteral("034L4"))) essaisElectronique034L4(f, v);
    if (lots.contains(QStringLiteral("034L5"))) essaisElectronique034L5(f, v);
    if (lots.contains(QStringLiteral("034L6"))) essaisElectronique034L6(f, v);
    if (lots.contains(QStringLiteral("034L7"))) essaisElectronique034L7(f, v);
    if (lots.contains(QStringLiteral("034L8"))) essaisElectronique034L8(f, v);
    if (lots.contains(QStringLiteral("034L10"))) essaisElectronique034L10(f, v);
    if (lots.contains(QStringLiteral("034L11"))) essaisElectronique034L11(f, v);
    if (lots.contains(QStringLiteral("L6"))) essaisElectroniqueL6(f, v);
    if (lots.contains(QStringLiteral("L7"))) essaisElectroniqueL7(f, v);
    if (lots.contains(QStringLiteral("L8"))) essaisElectroniqueL8(f, v);
    if (lots.contains(QStringLiteral("L9"))) essaisElectroniqueL9(f, v);
    if (lots.contains(QStringLiteral("L10"))) essaisElectroniqueL10(f, v);
    if (lots.contains(QStringLiteral("L11"))) essaisElectroniqueL11(f, v);
    if (lots.contains(QStringLiteral("L12"))) essaisElectroniqueL12(f, v);
    if (lots.contains(QStringLiteral("CLICS"))) profilerClicsL9(f);
    if (lots.contains(QStringLiteral("GLISSE"))) profilerGlisseL9(f);
    if (lots.contains(QStringLiteral("MESURES"))) mesurerAtelier(f);
    if (lots.contains(QStringLiteral("PROFIL300"))) profilerAtelier300(f);
    std::printf("%d échec(s)\n", g_echecs);
    std::fflush(stdout);
    QDir(dossier).removeRecursively();
    QApplication::exit(g_echecs);
    return;
  }
  int refus = 0;
  QObject::connect(doc, &Document::refus, &f, [&refus](const QString&) { ++refus; });

  // ---------- 1. Nouvelle carte depuis l'accueil ----------
  f.choixAccueilPublic(QStringLiteral("carte-nouvelle"));
  attendre(600);
  AtelierElectronique* a = f.atelierElectronique();
  verifier(a && a->isVisible(), "l'accueil « Nouvelle carte électronique » ouvre l'atelier électronique");
  if (!a) {
    std::printf("%d échec(s)\n", ++g_echecs);
    QApplication::exit(g_echecs);
    return;
  }
  verifier(reel::activer(&f), "la fenêtre est active : les touches et les raccourcis lui parviennent");
  verifier(conception(f) && conception(f)->feuilles.size() == 1, "une conception neuve, avec sa feuille");
  verifier(a->mode() == AtelierElectronique::Schema, "l'atelier s'ouvre sur le schéma");
  verifier(!doc->peutAnnuler(), "la création ne laisse pas de pas d'historique vide à annuler");

  // ---------- 2. Composants pris dans la bibliothèque ----------
  // Le dessin est posé au milieu de la feuille entière (Origine : tout voir), loin des bords de la vue
  const auto P = [](double x, double y) { return ptMm(x + 50.8, y + 25.4); };
  a->vueSchema()->setFocus(Qt::OtherFocusReason);
  touche(a->vueSchema(), Qt::Key_Home);
  attendre(150);
  bool poses = poserDepuisBibliotheque(a, QStringLiteral("Connecteur, 2 broches"), P(20.32, 50.8));
  poses &= poserDepuisBibliotheque(a, QStringLiteral("Résistance"), P(50.8, 38.1));
  poses &= poserDepuisBibliotheque(a, QStringLiteral("LED"), P(63.5, 50.8), 3);
  poses &= poserDepuisBibliotheque(a, QStringLiteral("+5V"), P(30.48, 30.48));
  poses &= poserDepuisBibliotheque(a, QStringLiteral("GND"), P(30.48, 66.04));
  const ConceptionElec* c = conception(f);
  verifier(poses && c && c->feuilles[0].symboles.size() == 5 && c->composants.size() == 3, "cinq symboles posés depuis la bibliothèque, trois composants",
           c ? std::to_string(c->feuilles[0].symboles.size()) + " symboles" : "");
  std::string led;
  for (const auto& s : c->feuilles[0].symboles) if (s.symbole == "LED") led = s.uuid;
  bool tournee = false;
  for (const auto& s : c->feuilles[0].symboles) if (s.uuid == led) tournee = s.rotation == 3;
  verifier(tournee, "R tourne le symbole avant de le poser");

  // ---------- 3. Fils tirés d'une broche à l'autre ----------
  const std::string j = symboleDe(*c, "Conn_01x02"), r = symboleDe(*c, "R"), p5 = symboleDe(*c, "+5V"), gnd = symboleDe(*c, "GND");
  VueSchema* vs = a->vueSchema();
  // W arme le fil ; / change l'équerre ; un clic sur chaque broche
  auto filEntre = [&](Pt x, Pt y, bool verticalDabord = false) {
    vs->setFocus(Qt::OtherFocusReason);
    touche(vs, Qt::Key_W, QStringLiteral("w"));
    if (verticalDabord) touche(vs, Qt::Key_Slash, QStringLiteral("/"));
    cliquer(vs, ecran(vs, x));
    cliquer(vs, ecran(vs, y));
    if (verticalDabord) touche(vs, Qt::Key_Slash, QStringLiteral("/"));
    touche(vs, Qt::Key_Escape);
    touche(vs, Qt::Key_Escape);
  };
  c = conception(f);
  filEntre(broche(*c, j, "1"), broche(*c, p5, "1"));
  filEntre(broche(*c, p5, "1"), broche(*c, r, "1"));
  filEntre(broche(*c, r, "2"), broche(*c, led, "2"));
  filEntre(broche(*c, led, "1"), broche(*c, gnd, "1"), true);
  filEntre(broche(*c, j, "2"), broche(*c, gnd, "1"));
  attendre(200);
  c = conception(f);
  verifier(c->feuilles[0].fils.size() >= 8, "les fils suivent l'équerre d'une broche à l'autre", std::to_string(c->feuilles[0].fils.size()) + " segments");

  // ---------- 4. Annotation et vérification électrique ----------
  verifier(cliquerRuban(a, "schema.annoter"), "le bouton « Annoter » répond");
  c = conception(f);
  verifier(c->parReference("J1") && c->parReference("R1") && c->parReference("D1"), "annotation : J1, R1, D1");
  a->recalculer();
  int erreursErc = 0;
  for (const auto& v : a->erc()) erreursErc += v.gravite == Gravite::Erreur;
  std::string detailErc;
  for (const auto& v : a->erc()) detailErc += v.code + " " + v.message + " ; ";
  verifier(erreursErc == 0, "vérification électrique sans erreur", detailErc);
  const Connectivite k = connectivite(*c);
  verifier(k.nom(c->parReference("R1")->uuid, "1") == "+5V" && k.nom(c->parReference("D1")->uuid, "1") == "GND", "réseaux nommés par les alimentations : +5V et GND");

  // ---------- 5. Valeur changée dans le panneau, annuler, rétablir ----------
  cliquer(vs, ecran(vs, P(50.8, 38.1)));
  attendre(300);
  verifier(vs->selection().size() == 1, "un clic sur la résistance la sélectionne");
  QLineEdit* valeur = nullptr;
  for (QLineEdit* e : a->findChildren<QLineEdit*>(QStringLiteral("champ:valeur")))
    if (e->isVisible() && e->text() == QStringLiteral("10k")) valeur = e;
  verifier(valeur != nullptr, "le panneau montre la valeur de la résistance");
  const std::uint64_t calculsAvant = a->generationCalcul();
  if (valeur) {
    saisir(valeur, QStringLiteral("220"));
    attendre(150);
  }
  verifier(conception(f)->parReference("R1")->valeur == "220", "la valeur saisie dans le panneau arrive dans le modèle");
  // L1 : le pas porte son nom, le menu le dit, et changer une valeur ne relance ni la connectivité ni le cuivre
  QAction* actionAnnuler = f.findChild<QAction*>(QStringLiteral("actionAnnuler"));
  verifier(doc->nomAnnuler() == QStringLiteral("Valeur de R1 : 10k → 220") && actionAnnuler && actionAnnuler->text() == QStringLiteral("&Annuler : Valeur de R1 : 10k → 220"),
           "le pas d'historique est nommé « Valeur de R1 : 10k → 220 », et le menu Édition le dit", (doc->nomAnnuler() + QStringLiteral(" | ") + (actionAnnuler ? actionAnnuler->text() : QString())).toStdString());
  verifier(a->generationCalcul() == calculsAvant, "changer une valeur ne relance ni la connectivité, ni les zones, ni la DRC (A12)",
           std::to_string(a->generationCalcul() - calculsAvant) + " calcul(s)");
  touche(vs, Qt::Key_Z, {}, Qt::ControlModifier);
  attendre(200);
  verifier(conception(f)->parReference("R1")->valeur == "10k", "annuler rend l'ancienne valeur");
  verifier(doc->nomRetablir() == QStringLiteral("Valeur de R1 : 10k → 220"), "Rétablir porte le nom du pas défait");
  touche(vs, Qt::Key_Y, {}, Qt::ControlModifier);
  attendre(200);
  verifier(conception(f)->parReference("R1")->valeur == "220", "rétablir redonne la nouvelle");

  // ---------- 6. Circuit imprimé : mise à jour, placement, routage, zone, DRC ----------
  vs->setFocus(Qt::OtherFocusReason);
  touche(vs, Qt::Key_F8);
  attendre(400);
  c = conception(f);
  verifier(c->circuit.empreintes.size() == 3 && a->mode() == AtelierElectronique::Circuit, "« Mettre à jour depuis le schéma » (F8) pose trois empreintes et montre le circuit");
  doc->modifier([](prom::Projet& p) {
    auto& ce = p.electronique[0];
    ce.circuit.contour.largeur = nm(40);
    ce.circuit.contour.hauteur = nm(30);
    for (auto& e : ce.circuit.empreintes) {
      const std::string ref = ce.composant(e.composant)->reference;
      if (ref == "R1") e.position = ptMm(20, 10);
      if (ref == "D1") e.position = ptMm(30, 20);
      if (ref == "J1") e.position = ptMm(12, 15);
    }
  });
  attendre(300);
  VueCircuit* vc = a->vueCircuit();
  vc->setFocus(Qt::OtherFocusReason);
  touche(vc, Qt::Key_Home);
  attendre(200);
  {
    // Ctrl+Z pendant un glissement : l'empreinte revient à sa place, l'historique ne bouge pas, et la suite du geste
    // ne déplace plus rien (A15)
    const QStringList historiqueAvant = doc->historique();
    const QPoint depart = ecran(vc, ptMm(12, 15)), arrivee = ecran(vc, ptMm(6, 15));
    reel::deplacer(vc, depart);
    reel::appuyer(vc, depart);
    for (int i = 1; i <= 4; ++i) reel::deplacer(vc, depart + (arrivee - depart) * i / 8);
    Pt pendant{};
    for (const auto& e : conception(f)->circuit.empreintes) if (conception(f)->composant(e.composant)->reference == "J1") pendant = e.position;
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);  // le bouton gauche toujours tenu
    for (int i = 5; i <= 8; ++i) reel::deplacer(vc, depart + (arrivee - depart) * i / 8);
    reel::relacher(vc, arrivee);
    attendre(150);
    Pt apres{};
    for (const auto& e : conception(f)->circuit.empreintes) if (conception(f)->composant(e.composant)->reference == "J1") apres = e.position;
    verifier(std::abs(mm(pendant.x) - 12) > 1 && std::abs(mm(apres.x) - 12) < 0.01 && std::abs(mm(apres.y) - 15) < 0.01 && doc->historique() == historiqueAvant && !doc->enGeste(),
             "Ctrl+Z pendant le glissement remet l'empreinte en place sans toucher à l'historique",
             std::to_string(mm(pendant.x)) + " → " + std::to_string(mm(apres.x)) + ", " + std::to_string(doc->historique().size()) + " pas");
  }
  // Glisser le connecteur à la souris
  reel::glisser(vc, ecran(vc, ptMm(12, 15)), ecran(vc, ptMm(6, 15)));
  attendre(150);
  c = conception(f);
  Pt posJ1{};
  for (const auto& e : c->circuit.empreintes) if (c->composant(e.composant)->reference == "J1") posJ1 = e.position;
  verifier(std::abs(mm(posJ1.x) - 6) < 0.25 && std::abs(mm(posJ1.y) - 15) < 0.25, "l'empreinte se glisse à la souris", std::to_string(mm(posJ1.x)) + ", " + std::to_string(mm(posJ1.y)));
  const bool historique = doc->peutAnnuler();
  touche(vc, Qt::Key_Z, {}, Qt::ControlModifier);
  attendre(200);
  c = conception(f);
  for (const auto& e : c->circuit.empreintes) if (c->composant(e.composant)->reference == "J1") posJ1 = e.position;
  verifier(historique && std::abs(mm(posJ1.x) - 12) < 0.01, "le glissement s'annule en un seul pas");
  touche(vc, Qt::Key_Y, {}, Qt::ControlModifier);
  attendre(200);
  // Routage automatique, puis plan de masse dessiné au dos
  cliquerRuban(a, "circuit.routerAuto");
  attendre(400);
  a->recalculer();
  verifier(a->calculs().etat.chevelu.empty() && a->calculs().etat.reseauxComplets == a->calculs().etat.reseauxTotal, "le routage automatique relie tout",
           std::to_string(a->calculs().etat.chevelu.size()) + " liaison(s) restantes");
  // Page suivante : couche active dessous ; Zone au ruban ; trois clics et un double-clic qui ferme
  vc->setFocus(Qt::OtherFocusReason);
  touche(vc, Qt::Key_PageDown);
  cliquerRuban(a, "circuit.zone");
  // Les bulles de message empilées en bas à gauche du canevas s'effacent d'abord : un coin de la zone y tombe
  for (int i = 0; i < 100 && a->pileMessages()->isVisible() && a->pileMessages()->nombre() > 0; ++i) attendre(100);
  for (Pt q : {ptMm(1, 1), ptMm(39, 1), ptMm(39, 29)}) cliquer(vc, ecran(vc, q));
  QString propose;
  // Le réseau est demandé à la fermeture (EL-20) : la masse de la carte est proposée d'abord
  accepterSaisie(&propose);
  reel::doubleCliquer(vc, ecran(vc, ptMm(1, 29)));
  attendre(400);
  touche(vc, Qt::Key_Escape);
  c = conception(f);
  a->recalculer();
  const bool zone = c->circuit.zones.size() == 1 && c->circuit.zones[0].reseau == "GND" && c->circuit.zones[0].couche == Couche::CuivreDessous;
  verifier(zone && propose == QStringLiteral("GND"), "la zone fermée demande son réseau, la masse de la carte proposée d'abord", propose.toStdString());
  verifier(zone && a->calculs().zones.count(c->circuit.zones[0].uuid) && aireMm2(a->calculs().zones.at(c->circuit.zones[0].uuid)) > 300, "plan de masse dessiné au dos, rempli");
  int erreursDrc = 0;
  std::string detailDrc;
  for (const auto& v : a->calculs().drc) {
    erreursDrc += v.gravite == Gravite::Erreur;
    detailDrc += v.code + " " + v.message + " ; ";
  }
  verifier(erreursDrc == 0, "DRC sans erreur après routage et zone", detailDrc);
  // Classe de réseaux créée dans le panneau de la carte : le +5V y reçoit une piste plus large
  vc->setFocus(Qt::OtherFocusReason);
  touche(vc, Qt::Key_Escape);
  attendre(300);
  bool classe = false;
  for (QPushButton* b : a->findChildren<QPushButton*>())
    if (b->isVisible() && b->text() == QStringLiteral("＋ Nouvelle classe")) {
      a->inspecteur()->ensureWidgetVisible(b);
      attendre(50);
      reel::cliquer(b, b->rect().center());
      classe = true;
      break;
    }
  attendre(400);
  // Le +5V se coche dans la liste des réseaux de la classe (EL-39 : plus de nom tapé à la main)
  // Clic sur la ligne, puis Espace qui coche (comme au clavier dans toute liste à cases)
  QListWidgetItem* reseaux = nullptr;
  QListWidget* liste = nullptr;
  for (QListWidget* l : a->findChildren<QListWidget*>())
    if (l->isVisible())
      for (int i = 0; i < l->count(); ++i)
        if (l->item(i)->text() == QStringLiteral("+5V") && (l->item(i)->flags() & Qt::ItemIsUserCheckable)) {
          reseaux = l->item(i);
          liste = l;
        }
  if (reseaux) {
    a->inspecteur()->ensureWidgetVisible(liste);
    liste->scrollToItem(reseaux);
    attendre(50);
    reel::cliquer(liste->viewport(), liste->visualItemRect(reseaux).center());
    reel::touche(liste, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    attendre(400);
  }
  c = conception(f);
  verifier(classe && reseaux && c->circuit.regles.classes.size() == 1 && c->circuit.regles.classeReseau.count("+5V") &&
               largeurPiste(c->circuit.regles, "+5V") == nm(0.5) && largeurPiste(c->circuit.regles, "GND") == c->circuit.regles.largeur,
           "une classe de réseaux créée dans le panneau élargit les pistes du +5V, et seulement elles");
  vc->setFocus(Qt::OtherFocusReason);
  touche(vc, Qt::Key_Z, {}, Qt::ControlModifier);
  touche(vc, Qt::Key_Z, {}, Qt::ControlModifier);
  attendre(300);

  // ---------- 7. Composant créé par l'utilisateur ----------
  {
    DialogueComposant dlg(a);
    SpecSymbole sp;
    sp.nom = "Capteur I2C";
    sp.empreinte = "SOIC-8";
    sp.fabricant = "Exemple";
    sp.mpn = "CAPT-8";
    dlg.setSpec(sp);
    dlg.collerBroches(QStringLiteral("1 VCC\n2 GND\n3 SDA io\n4 SCL in\n5 INT out\n6 NC\n7 EN in\n8 ADDR in"));
    verifier(dlg.valide(), "la fenêtre de création accepte un composant à huit broches collées");
    const DefSymbole d = dlg.symbole();
    a->ajouterSymbolePerso(d);
    attendre(200);
    c = conception(f);
    verifier(c->symboles.count(d.id) == 1, "le composant créé entre dans le projet (définition verrouillée)");
    bool dansBibliotheque = false;
    for (const auto& x : AtelierElectronique::bibliothequePerso()) dansBibliotheque |= x.id == d.id;
    verifier(dansBibliotheque, "et dans la bibliothèque personnelle, pour les prochains projets");
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(200);
    const std::size_t avant = c->composants.size();
    poserDepuisBibliotheque(a, QStringLiteral("Capteur I2C"), sousEcran(vs, QPointF(vs->width() * 0.86, vs->height() * 0.3)));
    attendre(200);
    c = conception(f);
    const ComposantElec* x = c->composants.size() > avant ? &c->composants.back() : nullptr;
    verifier(x && x->mpn == "CAPT-8" && x->empreinte == "SOIC-8", "le composant créé se pose, avec son empreinte et sa référence fabricant");
    // On le retire pour garder un schéma propre
    touche(vs, Qt::Key_Z, {}, Qt::ControlModifier);
    attendre(200);
  }

  // Symbole à plusieurs unités (E10) : la porte A se pose, le panneau pose la suivante, le repère porte la lettre
  {
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(200);
    const std::size_t avant = conception(f)->feuilles[0].symboles.size();
    poserDepuisBibliotheque(a, QStringLiteral("74HC00 (par porte)"), sousEcran(vs, QPointF(vs->width() * 0.86, vs->height() * 0.55)));
    attendre(200);
    c = conception(f);
    const SymbolePlace* porte = c->feuilles[0].symboles.size() > avant ? &c->feuilles[0].symboles.back() : nullptr;
    verifier(porte && porte->unite == 1, "un 74HC00 par porte se pose unité A d'abord");
    if (porte) {
      const std::string uuid = porte->uuid, comp = porte->composant;
      const DefSymbole* defPorte = c->symbole(porte->symbole);
      cliquer(vs, ecran(vs, defPorte ? boiteSymbole(*porte, *defPorte).centre() : porte->position));
      attendre(300);
      QPushButton* suivante = a->findChild<QPushButton*>(QStringLiteral("poserUniteSuivante"));
      QComboBox* choix = a->findChild<QComboBox*>(QStringLiteral("uniteSymbole"));
      verifier(suivante && suivante->isEnabled() && choix && choix->count() == 5, "le panneau montre les cinq unités et propose la suivante");
      if (suivante) {
        a->inspecteur()->ensureWidgetVisible(suivante);
        attendre(50);
        reel::cliquer(suivante, suivante->rect().center());
      }
      attendre(300);
      c = conception(f);
      verifier(unitesPosees(*c, comp) == std::set<int>{1, 2} && vs->selection().size() == 1, "« Poser l’unité suivante » pose la porte B à côté, sélectionnée");
      const auto v = verifierERC(*c, connectivite(*c));
      int manquantes = 0;
      for (const auto& x : v) manquantes += x.code == "ERC-40" && std::find(x.objets.begin(), x.objets.end(), comp) != x.objets.end();
      verifier(manquantes == 3, "la vérification électrique signale les trois unités non posées (C, D, E)", std::to_string(manquantes));
      vs->setFocus(Qt::OtherFocusReason);
      touche(vs, Qt::Key_Z, {}, Qt::ControlModifier);
      touche(vs, Qt::Key_Z, {}, Qt::ControlModifier);
      attendre(300);
      verifier(conception(f)->feuilles[0].symboles.size() == avant, "deux annulations retirent les deux unités");
    }
  }

  // ---------- 8. Dessin PDF, dossier de fabrication ----------
  const QString pdf = QDir(dossier).filePath(QStringLiteral("schema.pdf"));
  verifier(a->exporterDessin(pdf) && QFileInfo(pdf).size() > 2000, "dessin du schéma en PDF");
  const QStringList fichiers = a->exporterFabrication(QDir(dossier).filePath(QStringLiteral("brouillon")));
  verifier(fichiers.size() >= 16 && QFileInfo::exists(QDir(dossier).filePath(QStringLiteral("brouillon/LISEZMOI.txt"))), "dossier de fabrication (Gerber, perçage, placement, nomenclature)",
           std::to_string(fichiers.size()) + " fichiers");
  c = conception(f);
  verifier(fichiers.contains(QStringLiteral("Assemblage/") + QString::fromStdString(c->nom).replace(QLatin1Char(' '), QLatin1Char('_')) + QStringLiteral("-plan-de-montage.pdf")) &&
               fichiers.filter(QStringLiteral("-schema.pdf")).size() == 1,
           "le dossier contient aussi le schéma et le plan de montage en PDF");
  const QString plan = QDir(dossier).filePath(QStringLiteral("plan.pdf"));
  verifier(a->exporterPlanMontage(plan) && QFileInfo(plan).size() > 2000, "plan de montage en PDF");
  {
    OptionsTypon o;
    const QString typon = QDir(dossier).filePath(QStringLiteral("typon.pdf")), typonSvg = QDir(dossier).filePath(QStringLiteral("typon.svg"));
    verifier(a->exporterTypon(typon, o) && QFileInfo(typon).size() > 2000 && a->exporterTypon(typonSvg, o) && QFileInfo(typonSvg).size() > 2000,
             "typon de gravure en PDF et en SVG, à l'échelle 1");
    std::printf("  (typon) %s\n", typon.toStdString().c_str());
  }

  // ---------- 9. Validation, publication, verrou, nouvelle révision ----------
  a->ouvrir(0, AtelierElectronique::Fabrication);
  attendre(400);
  const Validation v = a->validation();
  std::string bloquants;
  for (const auto& e : v.etapes)
    for (const auto& d : e.details)
      if (d.gravite == Gravite::Erreur) bloquants += e.nom + " : " + d.message + " ; ";
  verifier(v.etapes.size() == 7 && v.publiable(), "validation globale : sept étapes, rien de bloquant", bloquants);
  QString erreur;
  const bool publiee = a->publier(dossier, QStringLiteral("Essai"), QString(), &erreur);
  c = conception(f);
  verifier(publiee && c->verrouillee && c->publications.size() == 1 && c->publications[0].id == "R001", "publication R001 : conception verrouillée", erreur.toStdString());
  verifier(publiee && QFileInfo::exists(QDir(dossier).filePath(QString::fromStdString(c->publications[0].dossier) + QStringLiteral("/PUBLICATION.txt"))), "la fiche de publication est écrite");
  {
    // EL-18 : l'historique du projet est gardé, mais Annuler laisse la révision publiée telle quelle
    const bool historique = doc->peutAnnuler();
    const std::string avantAnnuler = prom::ecad::versJson(*c).dump();
    doc->annuler();
    attendre(200);
    c = conception(f);
    verifier(historique && c->verrouillee && c->publications.size() == 1 && prom::ecad::versJson(*c).dump() == avantAnnuler,
             "la publication ne s'annule pas, et l'historique du reste du projet est gardé");
    doc->retablir();
    attendre(200);
    c = conception(f);
  }
  a->ouvrir(0, AtelierElectronique::Schema);
  attendre(200);
  const std::size_t nSymboles = c->feuilles[0].symboles.size();
  {
    const SymbolePlace& premier = c->feuilles[0].symboles.front();
    const DefSymbole* d = c->symbole(premier.symbole);
    vs->setFocus(Qt::OtherFocusReason);
    touche(vs, Qt::Key_Home);
    attendre(100);
    cliquer(vs, ecran(vs, d ? boiteSymbole(premier, *d).centre() : premier.position));
    touche(vs, Qt::Key_Delete);
    attendre(100);
  }
  doc->modifier([](prom::Projet& p) { p.electronique[0].composants.front().valeur = "modifiée en douce"; });
  attendre(200);
  c = conception(f);
  verifier(c->feuilles[0].symboles.size() == nSymboles && c->composants.front().valeur != "modifiée en douce" && refus >= 1, "une conception publiée refuse toute modification, avec un message",
           std::to_string(refus) + " refus");
  a->nouvelleRevision();
  attendre(200);
  c = conception(f);
  verifier(!c->verrouillee && c->cartouche.revision == "B" && c->publications.size() == 1, "nouvelle révision B : la conception se modifie de nouveau, l'historique reste");

  // ---------- 10. Arbre du projet ----------
  auto* nav = f.findChild<Navigateur*>();
  // L'arbre est reconstruit à chaque changement : on cherche l'élément juste avant de cliquer dessus
  auto element = [nav](const QString& cle) -> QTreeWidgetItem* {
    for (QTreeWidgetItemIterator it(nav); *it; ++it)
      if ((*it)->data(0, Navigateur::ROLE_CLE).toString() == cle) return *it;
    return nullptr;
  };
  const QString cleR1 = QStringLiteral("elec:0:comp:") + QString::fromStdString(conception(f)->parReference("R1")->uuid);
  verifier(element(QStringLiteral("elec:0:circuit")) && element(cleR1) && element(QStringLiteral("elec:0:feuille:0")) && element(QStringLiteral("elec:0:fab")),
           "l'arbre range la carte : schéma, composants, circuit imprimé, 3D, fabrication");
  if (QTreeWidgetItem* it = element(QStringLiteral("elec:0:circuit"))) {
    cliquerElement(nav, it);
    attendre(300);
    verifier(a->mode() == AtelierElectronique::Circuit, "un clic sur « Circuit imprimé » dans l'arbre ouvre le circuit");
  }
  if (QTreeWidgetItem* it = element(cleR1)) {
    int presses = 0, clics = 0;
    const auto c1 = QObject::connect(nav, &QTreeWidget::itemPressed, nav, [&presses] { ++presses; });
    const auto c2 = QObject::connect(nav, &QTreeWidget::itemClicked, nav, [&clics] { ++clics; });
    cliquerElement(nav, it);
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL")) std::printf("  (arbre : %d appui(s), %d clic(s))\n", presses, clics);
    attendre(400);
    verifier(a->mode() == AtelierElectronique::Schema && vs->selection().size() == 1, "un clic sur R1 dans l'arbre le montre et le sélectionne dans le schéma",
             "mode " + std::to_string(a->mode()) + ", " + std::to_string(vs->selection().size()) + " choisi(s)");
  }
  // Menu contextuel propre à l'élément (G2) : celui d'un composant ne propose jamais de supprimer la carte
  {
    QMenu menuR1, menuCarte;
    nav->remplirMenu(menuR1, cleR1);
    nav->remplirMenu(menuCarte, QStringLiteral("elec:0"));
    auto textes = [](const QMenu& m) {
      QStringList t;
      for (QAction* x : m.actions()) t << x->text();
      return t;
    };
    verifier(!textes(menuR1).contains(QStringLiteral("Supprimer la carte")) && textes(menuR1).contains(QStringLiteral("Supprimer le composant")) &&
                 textes(menuCarte).contains(QStringLiteral("Supprimer la carte")),
             "clic droit sur R1 dans l'arbre : menu du composant, sans « Supprimer la carte » ; la carte garde le sien");
  }

  // ---------- 11. 3D ----------
  a->ouvrir(0, AtelierElectronique::Vue3D);
  attendre(1200);
  verifier(a->mode() == AtelierElectronique::Vue3D, "la vue 3D de la carte s'ouvre");

  // ---------- 11 bis. Hauteur saisie dans le panneau : le corps suit en 3D (P4, T2) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(300);
    const ComposantElec* r1 = conception(f)->parReference("R1");
    const std::string idR1 = r1 ? r1->uuid : std::string();
    vs->setFocus(Qt::OtherFocusReason);
    touche(vs, Qt::Key_Home);
    attendre(100);
    for (const auto& s : conception(f)->feuilles[0].symboles)
      if (s.composant == idR1)
        if (const DefSymbole* d = conception(f)->symbole(s.symbole)) cliquer(vs, ecran(vs, boiteSymbole(s, *d).centre()));
    attendre(300);
    auto* h = a->findChild<QDoubleSpinBox*>(QStringLiteral("hauteurComposant"));
    if (h) {
      a->inspecteur()->ensureWidgetVisible(h);
      attendre(50);
      saisir(h, QStringLiteral("12"));
      attendre(150);
      // Douze fois la hauteur générique d'une résistance CMS : le panneau demande confirmation, sans fenêtre (3.4, P5)
      if (auto* confirmer = a->findChild<QPushButton*>(QStringLiteral("confirmerHauteur")); confirmer && confirmer->isVisible()) {
        cliquer(confirmer, confirmer->rect().center());
        attendre(150);
      }
    }
    // Le corps de R1 : la forme posée sur la carte (pas le cuivre, qui part de z = 0) au droit de R1
    const ConceptionElec* c2 = conception(f);
    Pt pos{};
    for (const auto& e : c2->circuit.empreintes)
      if (e.composant == idR1) pos = e.position;
    double hauteur = 0;
    for (const auto& [forme, couleur] : prom::modele3DCarte(*c2).couleurs) {
      Bnd_Box b;
      BRepBndLib::Add(forme, b);
      if (b.IsVoid()) continue;
      double x0, y0, z0, x1, y1, z1;
      b.Get(x0, y0, z0, x1, y1, z1);
      if (std::hypot((x0 + x1) / 2 - mm(pos.x), (y0 + y1) / 2 + mm(pos.y)) < 1.5 && z0 > 0.01 && z0 < 0.2) hauteur = std::max(hauteur, z1 - z0);
    }
    verifier(h && c2->parReference("R1") && c2->parReference("R1")->hauteur == 12 && std::abs(hauteur - 12) < 0.01,
             "12 mm saisis dans le panneau donnent un corps de 12 mm en 3D", std::to_string(hauteur) + " mm");
    touche(vs, Qt::Key_Z, {}, Qt::ControlModifier);
    attendre(200);
  }

  // ---------- 12. Enregistrer et rouvrir ----------
  const QString fichier = QDir(dossier).filePath(QStringLiteral("carte.prom"));
  doc->enregistrer(fichier);
  const std::string avant = versJson(*conception(f)).dump();
  Document relu;
  bool signale = false;
  try {
    relu.ouvrir(fichier);
  } catch (const FichierDejaOuvert&) {
    signale = true;
  }
  verifier(signale, "un fichier déjà ouvert dans une autre fenêtre est signalé à l'ouverture");
  relu.ouvrir(fichier, true);
  verifier(relu.projet().electronique.size() == 1 && versJson(relu.projet().electronique[0]).dump() == avant, "enregistrer puis rouvrir rend exactement la même carte");
  verifier(!relu.modifie(), "le fichier rouvert n'est pas marqué modifié");

  // ---------- 13. Exemple livré ----------
  libererOnglet(f);
  repondre(QMessageBox::DestructiveRole);
  f.choixAccueilPublic(QStringLiteral("exemple-elec"));
  attendre(900);
  c = conception(f);
  a->recalculer();
  int erreursExemple = 0;
  for (const auto& x : a->erc()) erreursExemple += x.gravite == Gravite::Erreur;
  for (const auto& x : a->calculs().drc) erreursExemple += x.gravite == Gravite::Erreur;
  verifier(c && c->nom == "Clignotant 555" && erreursExemple == 0 && a->validation().publiable(), "l'exemple « clignotant 555 » s'ouvre, sans erreur, prêt à publier");

  // ---------- 13 bis. Nomenclature commune du projet ----------
  if (QTreeWidgetItem* it = element(QStringLiteral("nomenclature"))) cliquerElement(nav, it);
  attendre(600);
  int lignesElec = 0;
  bool u1 = false, pcb = false;
  if (auto* page = f.findChild<PageNomenclature*>())
    if (auto* t = page->findChild<QTableWidget*>())
      for (int r = 0; r < t->rowCount(); ++r) {
        if (t->item(r, 0) && t->item(r, 0)->text() == QStringLiteral("Électronique")) ++lignesElec;
        if (t->item(r, 1) && t->item(r, 1)->text() == QStringLiteral("U1")) u1 = true;
        if (t->item(r, 1) && t->item(r, 1)->text() == QStringLiteral("PCB")) pcb = true;
      }
  verifier(lignesElec == 10 && u1 && pcb, "la nomenclature commune reprend la carte conçue : le circuit nu et ses composants regroupés", std::to_string(lignesElec) + " lignes");

  // ---------- 13 ter. Ctrl+3 ramène à l'atelier électronique ----------
  f.montrerAtelierPiece(true);
  attendre(300);
  touche(&f, Qt::Key_3, QStringLiteral("3"), Qt::ControlModifier);
  attendre(300);
  verifier(a->isVisible() && a->conception() == 0, "Ctrl+3 (Atelier électronique) ramène à la carte depuis un autre atelier");

  // ---------- 14. Ajouter une carte au projet, puis la supprimer ----------
  if (QTreeWidgetItem* it = element(QStringLiteral("ajouter:elec"))) cliquerElement(nav, it);
  attendre(500);
  verifier(doc->projet().electronique.size() == 2 && a->conception() == 1, "« Ajouter une carte électronique » en ajoute une seconde et l'ouvre");
  const bool menuSupprimer = element(QStringLiteral("elec:1")) && menuElement(nav, element(QStringLiteral("elec:1")), QStringLiteral("Supprimer la carte"));
  attendre(500);
  verifier(menuSupprimer && doc->projet().electronique.size() == 1 && a->conception() == 0, "supprimer la carte ouverte (clic droit dans l'arbre) ramène sur celle qui reste");
  a->vueSchema()->setFocus(Qt::OtherFocusReason);
  touche(a->vueSchema(), Qt::Key_Z, {}, Qt::ControlModifier);
  attendre(400);
  verifier(doc->projet().electronique.size() == 2, "la suppression s'annule");

  // ---------- 14 bis. Tableau des champs (fonction KiCad) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(200);
    const ConceptionElec* c0 = conception(f);
    TableauChamps t(*c0, a);
    std::vector<std::string> refs;
    for (const auto& k : c0->composants) if (k.reference.size() > 1 && k.reference[0] == 'R') refs.push_back(k.reference);
    std::sort(refs.begin(), refs.end());
    bool ok = refs.size() >= 2 && t.ecrire(refs[0], QStringLiteral("Fabricant"), QStringLiteral("Yageo"));
    // Bloc collé sur deux lignes, deux colonnes (référence fabricant, fournisseur)
    const int l0 = t.ligne(refs.size() >= 2 ? refs[0] : std::string()), c1 = t.colonne(QStringLiteral("Réf. fabricant"));
    if (ok && l0 >= 0 && c1 >= 0) t.collerTexte(l0, c1, QStringLiteral("RC0603FR-0710KL\tDigikey\nRC0603FR-07330RL\tMouser\n"));
    t.ajouterChamp(QStringLiteral("Tolérance"));
    ok = ok && t.ecrire(refs[0], QStringLiteral("Tolérance"), QStringLiteral("1 %"));
    const int n = a->appliquerChamps(t.composants());
    attendre(200);
    const ComposantElec* r0 = conception(f)->parReference(refs.empty() ? std::string() : refs[0]);
    verifier(ok && n >= 2 && r0 && r0->fabricant == "Yageo" && r0->mpn == "RC0603FR-0710KL" && r0->fournisseur == "Digikey" && r0->champs.count("Tolérance") &&
                 doc->peutAnnuler(),
             "le tableau des champs modifie plusieurs composants en un pas (cellule, bloc collé, champ libre ajouté)", std::to_string(n) + " composant(s)");
    vs->setFocus(Qt::OtherFocusReason);
    touche(vs, Qt::Key_Z, {}, Qt::ControlModifier);
    attendre(200);
  }

  // ---------- 14 ter. Empreinte créée par tableau de pastilles, rangée dans la bibliothèque et le projet ----------
  {
    const ConceptionElec* c0 = conception(f);
    std::string jId;
    for (const auto& k : c0->composants) if (k.reference == "J1") jId = k.uuid;
    DialogueEmpreinte dlg(2, a);
    SpecEmpreinte sp;
    sp.nom = "Bornier essai";
    sp.colonnes = 2;
    sp.pasX = 5.08;
    sp.largeur = 2.4;
    sp.hauteur = 2.4;
    sp.percage = 1.3;
    sp.hauteurCorps = 10;
    dlg.setSpec(sp);
    const DefEmpreinte d = dlg.empreinte();
    a->ajouterEmpreintePerso(d, jId);
    attendre(200);
    const ConceptionElec* c1 = conception(f);
    const ComposantElec* j1 = c1->composant(jId);
    bool dansBibliotheque = false;
    for (const auto& e : AtelierElectronique::empreintesPerso()) dansBibliotheque |= e.id == d.id;
    verifier(!jId.empty() && j1 && j1->empreinte == "perso:Bornier_essai" && c1->empreintes.count("perso:Bornier_essai") && dansBibliotheque && d.pastilles.size() == 2 &&
                 d.pastilles[1].position == ptMm(2.54, 0),
             "une empreinte créée par tableau de pastilles va dans la bibliothèque personnelle, le projet et le composant");
    vs->setFocus(Qt::OtherFocusReason);
    touche(vs, Qt::Key_Z, {}, Qt::ControlModifier);
    attendre(200);
  }

  // ---------- 14 ter bis. Homonyme d'empreinte : nom libre proposé, remplacement jamais silencieux (B10) ----------
  {
    DialogueEmpreinte libre(2, a);
    libre.setContexte({identifiantEmpreintePerso("Connecteur")}, 0);
    verifier(libre.spec().nom == "Connecteur-2", "le nom proposé à une empreinte neuve est libre : « Connecteur-2 » quand « Connecteur » existe");
    // « perso:Bornier_essai » est resté dans la bibliothèque personnelle : une autre géométrie du même nom pose la
    // question, et « Annuler » ne change ni la bibliothèque ni le projet
    std::string jId;
    for (const auto& k : conception(f)->composants)
      if (k.reference == "J1") jId = k.uuid;
    const std::string empreinteJ1 = conception(f)->composant(jId) ? conception(f)->composant(jId)->empreinte : std::string();
    DialogueEmpreinte dlg(2, a);
    SpecEmpreinte sp;
    sp.nom = "Bornier essai";
    sp.colonnes = 4;
    sp.pasX = 3.5;
    dlg.setSpec(sp);
    bool question = false;
    QTimer::singleShot(400, [&question] {
      for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* b = qobject_cast<QMessageBox*>(w); b && b->isVisible() && b->windowTitle() == QStringLiteral("Empreinte déjà existante")) {
          question = true;
          for (QAbstractButton* x : b->buttons())
            if (b->buttonRole(x) == QMessageBox::RejectRole) {
              reel::cliquer(x, x->rect().center());
              break;
            }
        }
    });
    a->ajouterEmpreintePerso(dlg.empreinte(), jId);
    attendre(200);
    std::size_t pastillesBibliotheque = 0;
    for (const auto& e : AtelierElectronique::empreintesPerso())
      if (e.id == "perso:Bornier_essai") pastillesBibliotheque = e.pastilles.size();
    const ComposantElec* j1 = conception(f)->composant(jId);
    verifier(question && pastillesBibliotheque == 2 && j1 && j1->empreinte == empreinteJ1 && !conception(f)->empreintes.count("perso:Bornier_essai"),
             "une empreinte du nom d'une existante pose la question ; « Annuler » ne change ni la bibliothèque ni le composant");
  }

  // ---------- 14 quater. Texte posé sur la carte, choisi, déplacé, dans les Gerber ----------
  {
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(300);
    VueCircuit* vc2 = a->vueCircuit();
    const std::size_t avant = conception(f)->circuit.textes.size();
    // Page précédente : couche active dessus ; Texte au ruban ; un clic, le texte tapé ; Maj+→ jusqu'à 2 mm
    vc2->setFocus(Qt::OtherFocusReason);
    touche(vc2, Qt::Key_Home);
    touche(vc2, Qt::Key_PageUp);
    cliquerRuban(a, "circuit.texte");
    taperDansSaisie(QStringLiteral("REV A"));
    cliquer(vc2, ecran(vc2, ptMm(5, 5)));
    attendre(500);
    std::string id;
    for (const auto& t : conception(f)->circuit.textes)
      if (t.texte == "REV A") id = t.uuid;
    const ConceptionElec* c1 = conception(f);
    Pt p0{};
    for (const auto& t : c1->circuit.textes) if (t.uuid == id) p0 = t.p;
    vc2->setFocus(Qt::OtherFocusReason);
    auto xTexte = [&]() -> double {
      for (const auto& t : conception(f)->circuit.textes)
        if (t.uuid == id) return mm(t.p.x);
      return 0;
    };
    // Maj+→ : dix pas de la grille ; autant d'appuis qu'il en faut pour 2 mm
    const double x0 = xTexte();
    touche(vc2, Qt::Key_Right, {}, Qt::ShiftModifier);
    const double pas = xTexte() - x0;
    for (int i = 1; pas > 0 && i < static_cast<int>(std::lround(2.0 / pas)); ++i) touche(vc2, Qt::Key_Right, {}, Qt::ShiftModifier);
    attendre(450);
    Pt p1{};
    bool serig = false;
    for (const auto& t : conception(f)->circuit.textes) if (t.uuid == id) { p1 = t.p; serig = t.couche == Couche::SerigraphieDessus; }
    bool gerber = false;
    for (const auto& t : textesSerigraphie(*conception(f))) gerber |= t.texte == "REV A";
    verifier(!id.empty() && conception(f)->circuit.textes.size() == avant + 1 && vc2->genreChoisi() == VueCircuit::Genre::Texte && p1.x - p0.x == nm(2) && serig && gerber,
             "un texte de sérigraphie se pose sur la carte, se choisit, se déplace et part dans les Gerber",
             std::string(id.empty() ? "texte absent" : "texte posé") + ", genre " + std::to_string(static_cast<int>(vc2->genreChoisi())) + ", déplacé de " + std::to_string(mm(p1.x - p0.x)) + " mm" +
                 (serig ? "" : ", pas en sérigraphie") + (gerber ? "" : ", absent des Gerber"));
    touche(vc2, Qt::Key_Escape);
    touche(vc2, Qt::Key_Z, {}, Qt::ControlModifier);
    touche(vc2, Qt::Key_Z, {}, Qt::ControlModifier);
    attendre(200);
  }

  // ---------- 15. Carte KiCad importée avec son cuivre et ses réseaux (EL-36) ----------
  // Données d'essai trouvées depuis le dossier de l'exécutable (build/app → tests/donnees) ou la variable
  // PROMETHEE_DONNEES
  {
    QString dossier = qEnvironmentVariable("PROMETHEE_DONNEES");
    for (const QString& essai : {QCoreApplication::applicationDirPath() + QStringLiteral("/../../tests/donnees"), QStringLiteral("tests/donnees")})
      if (dossier.isEmpty() && QFileInfo::exists(essai + QStringLiteral("/kicad/ecc83-pp.kicad_pcb"))) dossier = essai;
    const QString carte = dossier + QStringLiteral("/kicad/ecc83-pp.kicad_pcb");
    if (QFileInfo::exists(carte)) {
      const std::size_t avant = doc->projet().electronique.size();
      const bool ok = f.importerKicadElectronique(carte, false);
      attendre(800);
      for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* b = qobject_cast<QMessageBox*>(w); b && b->isVisible() && b->windowTitle() == QStringLiteral("Carte KiCad importée")) reel::touche(b, Qt::Key_Escape);
      a->recalculer();
      int erreurs = 0;
      for (const auto& v : a->calculs().drc) erreurs += v.gravite == Gravite::Erreur;
      verifier(ok && doc->projet().electronique.size() == avant + 1 && a->conception() == static_cast<int>(avant) && a->mode() == AtelierElectronique::Circuit &&
                   a->calculs().etat.chevelu.empty() && erreurs == 0,
               "une carte KiCad s'importe avec son cuivre et ses réseaux : tout routé, DRC sans erreur", std::to_string(erreurs) + " erreur(s)");
    } else
      std::printf("(carte KiCad d'essai introuvable : import non essayé)\n");
  }

  // ---------- Lot L1 : fondations, par de vrais gestes (EssaisElectroniqueL1.cpp) ----------
  essaisElectroniqueL1(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L2 : schéma interactif, par de vrais gestes (EssaisElectroniqueL2.cpp) ----------
  essaisElectroniqueL2(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L3 : vérité du composant (EssaisElectroniqueL3.cpp) ----------
  essaisElectroniqueL3(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L4 : empreintes et bibliothèque (EssaisElectroniqueL4.cpp) ----------
  essaisElectroniqueL4(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L5 : circuit imprimé (EssaisElectroniqueL5.cpp) ----------
  essaisElectroniqueL5(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });
  essaisElectronique034(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L6 : mécatronique (EssaisElectroniqueL6.cpp) ----------
  essaisElectroniqueL6(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L7 : schéma avancé (EssaisElectroniqueL7.cpp) ----------
  essaisElectroniqueL7(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L8 : fabrication et cartes complexes (EssaisElectroniqueL8.cpp) ----------
  essaisElectroniqueL8(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L9 : performances (EssaisElectroniqueL9.cpp) ----------
  essaisElectroniqueL9(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L10 : échanges (EssaisElectroniqueL10.cpp) ----------
  essaisElectroniqueL10(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lot L11 : simulation (EssaisElectroniqueL11.cpp) ----------
  essaisElectroniqueL11(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Lots L12 à L19 : outils de conception, interface JSON-RPC, verrou, historique (EssaisElectroniqueL12.cpp) ----------
  essaisElectroniqueL12(f, [](bool ok, const std::string& quoi, const std::string& detail) { verifier(ok, quoi, detail); });

  // ---------- Mesures (1.1, Q5, ERG-10) : la carte de mesure de 300 composants dans l'atelier, par de vrais gestes ;
  // temps relevés à chaque construction (lignes « MESURE »), sans seuil avant le lot L9 ----------
  mesurerAtelier(f);

  std::printf("%d échec(s)\n", g_echecs);
  std::fflush(stdout);
  QDir(dossier).removeRecursively();
  QApplication::exit(g_echecs);
}
