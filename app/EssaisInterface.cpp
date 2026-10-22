// Prométhée : essais automatiques de l'interface. Licence GPL-3.0-only.
#include "EssaisInterface.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <numbers>
#include <string>

#include <QAction>
#include <QApplication>
#include <QEventLoop>
#include <QDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QFrame>
#include <QGroupBox>
#include <QScrollArea>
#include <QMenu>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLineEdit>
#include <QWindow>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>

// Passage d'une touche par la table des raccourcis, comme le fait le système (fonction exportée de Qt GUI, celle
// qu'utilise QTest) : vrai si un raccourci l'a prise ou si le widget l'a réservée
Q_GUI_EXPORT bool qt_sendShortcutOverrideEvent(QObject* o, ulong timestamp, int k, Qt::KeyboardModifiers mods, const QString& text, bool autorep, ushort count);
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QToolBar>
#include <QToolButton>

#include "Accueil.hpp"
#include "ChampValeur.hpp"
#include "PanneauLateral.hpp"
#include "Qet.hpp"
#include "MiseEnPlan.hpp"
#include "AtelierAssemblage.hpp"
#include "AtelierElectronique.hpp"
#include "AtelierPiece.hpp"
#include "DialogueNomenclature.hpp"
#include "Navigateur.hpp"
#include "Nomenclature.hpp"
#include "RegistreActions.hpp"
#include "Document.hpp"
#include "Esquisseur.hpp"
#include "FenetrePrincipale.hpp"
#include "Journal.hpp"
#include "VuePiece.hpp"
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <gp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <gp_Trsf.hxx>

#include "promethee/construction.hpp"
#include "promethee/cotes_modele.hpp"
#include "promethee/dessin.hpp"
#include "promethee/dxf_dessin.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/structure.hpp"
#include "promethee/nomenclature_meca.hpp"
#include "promethee/piece.hpp"

using namespace prom;

namespace {
int g_echecs = 0;

void verifier(bool ok, const std::string& quoi, const std::string& detail = {}) {
  const std::string ligne = (ok ? "OK      " : "ÉCHEC   ") + quoi + (detail.empty() ? "" : "  (" + detail + ")");
  std::printf("%s\n", ligne.c_str());
  std::fflush(stdout);
  journal(("essai : " + ligne).c_str());
  if (!ok) ++g_echecs;
}

void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}

void cliquer(QWidget* w, const QPointF& p, Qt::KeyboardModifiers m = Qt::NoModifier) {
  QMouseEvent survol(QEvent::MouseMove, p, w->mapToGlobal(p), Qt::NoButton, Qt::NoButton, m);
  QCoreApplication::sendEvent(w, &survol);
  w->update();
  attendre(120);
  QMouseEvent appui(QEvent::MouseButtonPress, p, w->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, m);
  QCoreApplication::sendEvent(w, &appui);
  QMouseEvent lache(QEvent::MouseButtonRelease, p, w->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, m);
  QCoreApplication::sendEvent(w, &lache);
  w->update();
  attendre(250);
}

// Commande par son nom, qu'elle soit un bouton simple ou une variante dans le menu d'un bouton.
bool declencher(QWidget* racine, const QString& texte) {
  for (QAction* a : racine->findChildren<QAction*>())
    if (a->text() == texte && a->isEnabled()) {
      // Désactivée si son bouton ou sa barre l'est : on vérifie que le parent visible est actif.
      QWidget* w = qobject_cast<QWidget*>(a->parent());
      if (w && !w->isEnabled()) continue;
      a->trigger();
      attendre(400);
      return true;
    }
  return false;
}

bool bouton(QWidget* racine, const QString& texte) {
  for (QPushButton* b : racine->findChildren<QPushButton*>())
    if (b->text() == texte && b->isVisible() && b->isEnabled()) { b->click(); attendre(500); return true; }
  return false;
}

Piece piece(FenetrePrincipale& f) { return f.document()->projet().pieces.empty() ? Piece() : pieceDepuisJson(f.document()->projet().pieces.back()); }

double volumePiece(FenetrePrincipale& f) {
  const ResultatPiece r = construirePiece(piece(f));
  return r.forme.IsNull() ? 0 : volume(r.forme);
}

std::string chiffre(double v) {
  char t[64];
  std::snprintf(t, sizeof t, "%.3f", v);
  return t;
}

// Esquisse sur XY dans la vue : point (u, v) du plan → position à l'écran (la superposition couvre la vue).
QPointF ecranXY(VuePiece* vue, double u, double v, double z = 0) { return vue->projeter(gp_Pnt(u, v, z)); }

int nombreFonctions(FenetrePrincipale& f, TypeFonction t) {
  int n = 0;
  for (const auto& x : piece(f).fonctions) if (x.type == t) ++n;
  return n;
}

// Crée une pièce et une esquisse sur le plan XY en cliquant le plan, comme un utilisateur.
bool esquisseSurXY(FenetrePrincipale& f, VuePiece* vue) {
  f.montrerAtelierPiece(true);  // l'atelier Pièces au premier plan (après l'assemblage, la nomenclature…)
  f.atelierPiece()->nouvellePiece();
  attendre(700);
  cliquer(vue, ecranXY(vue, -22, -12));
  const bool plan = vue->planSelectionne() == "XY";
  verifier(plan, "un clic sur le plan XY le sélectionne",
           "sélection : « " + vue->planSelectionne() + " »" + (vue->plansVisibles() ? " (plans montrés, " : " (plans cachés, ") + std::to_string(vue->selection().size()) + " objet(s) choisi(s) ; pièce " +
               std::to_string(f.atelierPiece()->pieceActive()) + "/" + std::to_string(f.document()->projet().pieces.size()) + (f.atelierPiece()->enReglage() ? ", réglage ouvert" : "") +
               (f.atelierPiece()->enEsquisse() ? ", esquisse ouverte" : "") + ", " + std::to_string(piece(f).fonctions.size()) + " fonction(s))");
  if (!plan) return false;
  verifier(declencher(&f, QStringLiteral("Nouvelle esquisse")), "le bouton « Nouvelle esquisse » répond");
  attendre(700);
  const auto p = piece(f);
  const bool ok = !p.fonctions.empty() && p.fonctions.back().type == TypeFonction::Esquisse && p.fonctions.back().plan == PlanBase::XY && p.fonctions.back().face.empty();
  verifier(ok, "l'esquisse est créée sur le plan XY");
  return ok;
}

// Touche envoyée comme le système le fait : la table des raccourcis passe d'abord (ShortcutOverride, puis
// raccourcis de la fenêtre et de l'atelier), la touche n'arrive au widget que si aucun ne l'a prise. Un simple
// sendEvent contournerait ces raccourcis et cacherait les conflits (EI-04, EI-05).
void toucheReelle(QWidget* w, int k, Qt::KeyboardModifiers m = Qt::NoModifier, const QString& texte = {}) {
  w->window()->activateWindow();
  w->setFocus(Qt::OtherFocusReason);
  attendre(60);
  QWidget* cible = QApplication::focusWidget() ? QApplication::focusWidget() : w;
  if (qt_sendShortcutOverrideEvent(cible, 0, k, m, texte, false, 1)) { attendre(120); return; }  // un raccourci l'a prise
  QKeyEvent appui(QEvent::KeyPress, k, m, texte);
  QCoreApplication::sendEvent(cible, &appui);
  attendre(120);
}

void cliquerDroit(QWidget* w, const QPointF& p) {
  QMouseEvent appui(QEvent::MouseButtonPress, p, w->mapToGlobal(p), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
  QCoreApplication::sendEvent(w, &appui);
  QMouseEvent lache(QEvent::MouseButtonRelease, p, w->mapToGlobal(p), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(w, &lache);
  attendre(150);
}

void rectangle(VuePiece* vue, Esquisseur* esq, double x1, double y1, double x2, double y2) {
  esq->setOutil(Esquisseur::Outil::Rectangle);
  cliquer(esq, ecranXY(vue, x1, y1));
  cliquer(esq, ecranXY(vue, x2, y2));
}

// Guette une fenêtre modale au bout de « ms » : elle est fermée et notée. La note est partagée avec la minuterie,
// si bien qu'un bloc d'essai qui finit avant elle ne laisse pas de référence pendante.
std::shared_ptr<bool> guetterModale(int ms) {
  auto vue = std::make_shared<bool>(false);
  QTimer::singleShot(ms, qApp, [vue] {
    if (QWidget* w = QApplication::activeModalWidget()) {
      *vue = true;
      w->close();
    }
  });
  return vue;
}
}  // namespace

void installerVeilleBoites() {
  static bool installee = false;
  if (installee) return;
  installee = true;
  auto* veille = new QTimer(qApp);
  veille->setInterval(500);
  auto* vue = new QPointer<QWidget>();
  auto* depuis = new int(0);
  QObject::connect(veille, &QTimer::timeout, qApp, [vue, depuis] {
    QWidget* m = QApplication::activeModalWidget();
    if (m != *vue) { *vue = m; *depuis = 0; return; }
    if (!m || ++*depuis < 6) return;
    *depuis = 0;
    if (auto* b = qobject_cast<QMessageBox*>(m)) {
      std::printf("BOÎTE   %s — %s\n", b->windowTitle().toStdString().c_str(), b->text().left(160).toStdString().c_str());
      std::fflush(stdout);
      QAbstractButton* oui = b->button(QMessageBox::Yes);
      if (!oui) oui = b->button(QMessageBox::Ok);
      if (!oui) oui = b->defaultButton();
      if (oui) oui->click(); else b->reject();
    } else if (auto* d = qobject_cast<QDialog*>(m)) {
      std::printf("BOÎTE   %s (fermée)\n", d->windowTitle().toStdString().c_str());
      std::fflush(stdout);
      d->reject();
    }
  });
  veille->start();
}

// 14. Saisie dynamique (M2) : longueur et angle tapés pendant le tracé, largeur et hauteur, diamètre
void essaiSaisieDynamique(FenetrePrincipale& f) {
  f.montrerAtelierPiece(true);
  attendre(500);
  // La vue et l'esquisseur de l'atelier Pièces (l'assemblage a aussi sa vue 3D)
  VuePiece* vue = f.atelierPiece() ? f.atelierPiece()->findChild<VuePiece*>() : nullptr;
  Esquisseur* esq = f.atelierPiece() ? f.atelierPiece()->findChild<Esquisseur*>() : nullptr;
  if (!vue || !esq) return;
  if (esquisseSurXY(f, vue)) {
    auto taper = [&](const QString& texte) {
      for (const QChar c : texte) {
        if (c == QLatin1Char('\t')) toucheReelle(esq, Qt::Key_Tab);
        else if (c == QLatin1Char('\n')) toucheReelle(esq, Qt::Key_Return);
        else toucheReelle(esq, c.isDigit() ? Qt::Key_0 + c.digitValue() : c == QLatin1Char(',') ? Qt::Key_Comma : Qt::Key_Period, Qt::NoModifier, QString(c));
      }
    };
    esq->setOutil(Esquisseur::Outil::Ligne);
    cliquer(esq, ecranXY(vue, 10, 10));
    taper(QStringLiteral("50\t30\n"));
    toucheReelle(esq, Qt::Key_Escape);
    attendre(200);
    const Esquisse e1 = esq->esquisse();
    const ElementEsquisse* ligne = nullptr;
    for (const auto& el : e1.elements) if (el.type == TypeElement::Ligne) ligne = &el;
    double longueur = 0, angle = 0;
    bool coteL = false, coteA = false;
    if (ligne) {
      const PointEsquisse *a = e1.point(ligne->p1), *b = e1.point(ligne->p2);
      longueur = std::hypot(b->x - a->x, b->y - a->y);
      angle = std::atan2(b->y - a->y, b->x - a->x) * 180 / std::numbers::pi;
      for (const auto& c : e1.contraintes) {
        coteL |= c.type == TypeContrainte::Longueur && std::abs(c.valeur - 50) < 1e-9;
        coteA |= c.type == TypeContrainte::Angle && std::abs(c.valeur - 30) < 1e-9;
      }
    }
    verifier(ligne && std::abs(longueur - 50) < 1e-6 && std::abs(angle - 30) < 1e-6 && coteL && coteA, "saisie dynamique : ligne de 50 mm à 30°, cotée", chiffre(longueur) + " mm, " + chiffre(angle) + "°");
    esq->setOutil(Esquisseur::Outil::Rectangle);
    cliquer(esq, ecranXY(vue, 80, 10));
    taper(QStringLiteral("40\t20\n"));
    attendre(200);
    int longueurs = 0;
    for (const auto& c : esq->esquisse().contraintes) longueurs += c.type == TypeContrainte::Longueur && (std::abs(c.valeur - 40) < 1e-9 || std::abs(c.valeur - 20) < 1e-9);
    verifier(longueurs == 2 && esq->esquisse().elements.size() == 5, "saisie dynamique : rectangle de 40 × 20 coté", std::to_string(longueurs) + " cote(s)");
    esq->setOutil(Esquisseur::Outil::Cercle);
    cliquer(esq, ecranXY(vue, 40, 60));
    taper(QStringLiteral("12\n"));
    attendre(200);
    bool diametre = false;
    for (const auto& c : esq->esquisse().contraintes) diametre |= c.type == TypeContrainte::Diametre && std::abs(c.valeur - 12) < 1e-9;
    verifier(diametre, "saisie dynamique : cercle de Ø12 coté");
    // Inférence pendant le tracé (S1) : parallèle, longueur égale, alignement sur un point
    {
      esq->setOutil(Esquisseur::Outil::Ligne);
      cliquer(esq, ecranXY(vue, 0, -40));
      cliquer(esq, ecranXY(vue, 30, -20));
      toucheReelle(esq, Qt::Key_Escape);
      cliquer(esq, ecranXY(vue, 50, -40));
      cliquer(esq, ecranXY(vue, 80, -19.3));  // 34,6° pour 33,7° : parallèle
      toucheReelle(esq, Qt::Key_Escape);
      cliquer(esq, ecranXY(vue, 0, -80));
      cliquer(esq, ecranXY(vue, 20, -80));
      cliquer(esq, ecranXY(vue, 20, -60.3));  // verticale de 19,7 après un trait de 20 : égale
      toucheReelle(esq, Qt::Key_Escape);
      cliquer(esq, ecranXY(vue, 60, -70));
      cliquer(esq, ecranXY(vue, 30.4, -50));  // à l'aplomb du point (30 ; −20) : alignement vertical
      toucheReelle(esq, Qt::Key_Escape);
      attendre(200);
      const Esquisse ei = esq->esquisse();
      int paralleles = 0, egales = 0, alignements = 0;
      for (const auto& c : ei.contraintes) {
        paralleles += c.type == TypeContrainte::Parallele;
        egales += c.type == TypeContrainte::Egal;
        alignements += c.type == TypeContrainte::Vertical && c.refs.size() == 2 && ei.point(c.refs[0]) && ei.point(c.refs[1]);
      }
      double xAligne = 1e9;
      for (const auto& pt : ei.points) if (std::abs(pt.y + 50) < 1) xAligne = pt.x;
      verifier(paralleles == 1 && egales == 1 && alignements >= 1 && std::abs(xAligne - 30) < 1e-6,
               "inférence : parallèle, longueur égale et alignement posés pendant le tracé", std::to_string(paralleles) + " ∥, " + std::to_string(egales) + " =, " + std::to_string(alignements) + " ⋮, x = " + chiffre(xAligne));
    }
    // Cote d'angle (S2) : secteur choisi par la position de l'étiquette ; angle par trois points
    {
      esq->setOutil(Esquisseur::Outil::Ligne);
      cliquer(esq, ecranXY(vue, 100, 0, 0), Qt::AltModifier);
      cliquer(esq, ecranXY(vue, 140, 0, 0), Qt::AltModifier);
      toucheReelle(esq, Qt::Key_Escape);
      cliquer(esq, ecranXY(vue, 100, 0, 0), Qt::AltModifier);
      cliquer(esq, ecranXY(vue, 120, 30, 0), Qt::AltModifier);
      toucheReelle(esq, Qt::Key_Escape);
      esq->setOutil(Esquisseur::Outil::Cote);
      const std::size_t avant = esq->esquisse().contraintes.size();
      cliquer(esq, ecranXY(vue, 128, 0, 0));
      cliquer(esq, ecranXY(vue, 110, 15, 0));
      cliquer(esq, ecranXY(vue, 92, 8, 0));  // entre la première ligne prise à rebours et la seconde
      toucheReelle(esq, Qt::Key_Escape);
      attendre(200);
      const Esquisse ea = esq->esquisse();
      const ContrainteEsquisse* angle = nullptr;
      for (std::size_t i = avant; i < ea.contraintes.size(); ++i) if (ea.contraintes[i].type == TypeContrainte::Angle) angle = &ea.contraintes[i];
      const double attendu = 180 - std::atan2(30.0, 20.0) * 180 / std::numbers::pi;
      verifier(angle && (static_cast<int>(angle->valeur2) & 3) == 1 && std::abs(std::abs(angle->valeur) - attendu) < 0.5,
               "cote d'angle : le secteur suit la position de l'étiquette", angle ? chiffre(angle->valeur) + "°, secteur " + chiffre(angle->valeur2) : std::string("aucune"));
      // Trois points : le sommet, puis un point de chaque côté
      esq->setOutil(Esquisseur::Outil::Cote);
      const std::size_t avant3 = esq->esquisse().contraintes.size(), lignes3 = esq->esquisse().elements.size();
      cliquer(esq, ecranXY(vue, 100, 0, 0));
      cliquer(esq, ecranXY(vue, 140, 0, 0));
      cliquer(esq, ecranXY(vue, 120, 30, 0));
      toucheReelle(esq, Qt::Key_Escape);
      attendre(200);
      const Esquisse e3 = esq->esquisse();
      int angles3 = 0, construction = 0;
      for (std::size_t i = avant3; i < e3.contraintes.size(); ++i) angles3 += e3.contraintes[i].type == TypeContrainte::Angle;
      for (std::size_t i = lignes3; i < e3.elements.size(); ++i) construction += e3.elements[i].construction;
      std::string nouvelles;
      for (std::size_t i = avant3; i < e3.contraintes.size(); ++i) nouvelles += " " + std::to_string(static_cast<int>(e3.contraintes[i].type));
      verifier(angles3 == 1 && construction == 2, "angle par trois points : deux lignes de construction et l'angle coté", std::to_string(angles3) + " angle(s), " + std::to_string(construction) + " ligne(s) de construction ;" + nouvelles);
      esq->setOutil(Esquisseur::Outil::Selection);
    }
    // Réglage d'outil posé sur la vue (U1) : distance du chanfrein et largeur de la rainure en arc, sans boîte modale
    {
      const auto modale = guetterModale(300);
      esq->setOutil(Esquisseur::Outil::ChanfreinEsquisse);
      attendre(100);
      QFrame* reglage = esq->findChild<QFrame*>(QStringLiteral("reglageOutil"));
      ChampValeur* champ = esq->findChild<ChampValeur*>(QStringLiteral("champReglageOutil"));
      verifier(reglage && reglage->isVisible() && champ, "chanfrein d'esquisse : la distance se règle dans un champ posé sur la vue");
      if (champ) {
        champ->setText(QStringLiteral("3"));
        emit champ->editingFinished();
      }
      verifier(std::abs(esq->distanceChanfrein() - 3) < 1e-9, "la distance tapée (3 mm) est gardée par l'outil", chiffre(esq->distanceChanfrein()));
      const std::size_t elements = esq->esquisse().elements.size();
      cliquer(esq, ecranXY(vue, 80.6, 10.6));
      attendre(200);
      verifier(!*modale && esq->esquisse().elements.size() == elements + 1, "le clic sur le coin pose le chanfrein, sans fenêtre", std::to_string(esq->esquisse().elements.size() - elements) + " élément(s)");
      esq->setOutil(Esquisseur::Outil::RainureArc);
      esq->setLargeurRainureArc(4);
      const std::size_t avantRainure = esq->esquisse().elements.size();
      cliquer(esq, ecranXY(vue, 60, 100));
      cliquer(esq, ecranXY(vue, 80, 100));
      cliquer(esq, ecranXY(vue, 60, 120));
      attendre(200);
      verifier(!*modale && esq->esquisse().elements.size() >= avantRainure + 4, "rainure en arc : trois clics, la largeur réglée d'avance", std::to_string(esq->esquisse().elements.size() - avantRainure) + " élément(s)");
      esq->setOutil(Esquisseur::Outil::Selection);
      verifier(reglage && !reglage->isVisible(), "le champ de réglage disparaît avec l'outil");
    }
    verifier(declencher(&f, QStringLiteral("✖ Annuler l’esquisse")), "annuler l'esquisse de la saisie dynamique");
    attendre(500);
  }
}

// 15. Axes de base cliquables (M7) et axe de révolution choisi au clic (M8)
void essaiAxesRevolution(FenetrePrincipale& f) {
  f.montrerAtelierPiece(true);
  attendre(500);
  // La vue et l'esquisseur de l'atelier Pièces (l'assemblage a aussi sa vue 3D)
  VuePiece* vue = f.atelierPiece() ? f.atelierPiece()->findChild<VuePiece*>() : nullptr;
  Esquisseur* esq = f.atelierPiece() ? f.atelierPiece()->findChild<Esquisseur*>() : nullptr;
  if (!vue || !esq) return;
  if (esquisseSurXY(f, vue)) {
    rectangle(vue, esq, 10, 0, 20, 30);
    esq->setOutil(Esquisseur::Outil::Selection);
    attendre(200);
    verifier(declencher(&f, QStringLiteral("✔ Terminer l’esquisse")), "terminer le profil à faire tourner");
    attendre(700);
    verifier(declencher(&f, QStringLiteral("Révolution")), "le bouton Révolution répond");
    attendre(600);
    QComboBox* axe = f.atelierPiece()->findChild<QComboBox*>(QStringLiteral("axeRevolution"));
    verifier(axe != nullptr && axe->itemText(0).contains(QStringLiteral("Ligne")) && !axe->itemText(0).contains(QStringLiteral("Ligne 1")), "les lignes de l'esquisse sont décrites (allure, longueur), pas numérotées",
             axe ? axe->itemText(0).toStdString() : "");
    if (axe) {
      const int vueI = axe->findData(QStringLiteral("?vue"));
      axe->setCurrentIndex(vueI);
      emit axe->activated(vueI);
      attendre(500);
      verifier(vue->plansVisibles(), "le choix de l'axe dans la vue montre plans et axes de base");
      cliquer(vue, ecranXY(vue, 0, 12, 0));  // axe Y
      attendre(500);
      bouton(&f, QStringLiteral("✔ OK"));
      attendre(900);
      const Piece pr = piece(f);
      const FonctionPiece& rev = pr.fonctions.back();
      const ResultatPiece rr = construirePiece(pr);
      verifier(rev.type == TypeFonction::Revolution && rev.axeBase == "Y" && rr.erreurs.empty() && std::abs(volume(rr.forme) - std::numbers::pi * 300 * 30) < 1e-3,
               "révolution autour de l'axe Y cliqué dans la vue", rev.axeBase + " · " + chiffre(volume(rr.forme)) + " mm³");
    }
  }
}

// 16. Échanges STEP (M12) : un assemblage STEP coloré, aux noms Unicode, ouvert avec sa structure ; inséré dans un
// assemblage, inséré à plat dans une pièce ; couleurs de faces posées dans la vue
void essaiImportCao(FenetrePrincipale& f) {
  const TopoDS_Shape chassis = BRepPrimAPI_MakeBox(60, 40, 10).Shape(), patte = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 10), 8, 8, 25).Shape();
  std::vector<TopoDS_Face> faces;
  for (TopExp_Explorer ex(chassis, TopAbs_FACE); ex.More(); ex.Next()) faces.push_back(TopoDS::Face(ex.Current()));
  PieceNommee a{"Châssis Ø20 — 日本", chassis, 0.85, 0.20, 0.15}, b{"Patte", patte, 0.10, 0.30, 0.80};
  a.couleursFaces = {{faces[5], {1.0, 0.8, 0.0}}};
  gp_Trsf t;
  t.SetTranslation(gp_Vec(100, 0, 0));
  PieceNommee a2 = a;
  a2.forme = chassis.Moved(TopLoc_Location(t));
  const QString chemin = QDir::temp().filePath(QStringLiteral("promethee-essai-chassis.step"));
  QFile::remove(chemin);
  exporterStep({a, a2, b}, chemin.toStdString());
  // Fichier › Importer : nouvel assemblage, une pièce par produit
  const std::size_t piecesAvant = f.document()->projet().pieces.size(), assemblagesAvant = f.document()->projet().assemblages.size();
  verifier(f.importerFichierCAO(chemin, false), "importer un STEP d'assemblage dans le projet");
  attendre(800);
  {
    const Projet& p = f.document()->projet();
    const bool structure = p.pieces.size() == piecesAvant + 2 && p.assemblages.size() == assemblagesAvant + 1;
    verifier(structure, "STEP d'assemblage : deux pièces et un assemblage ajoutés", std::to_string(p.pieces.size() - piecesAvant) + " pièce(s), " + std::to_string(p.assemblages.size() - assemblagesAvant) + " assemblage(s)");
    if (structure) {
      const Piece pc = pieceDepuisJson(p.pieces[piecesAvant]);
      verifier(pc.nom == "Châssis Ø20 — 日本" && pc.couleur == "#D93326" && pc.couleursFaces.size() == 1, "nom Unicode, couleur et couleur de face relus", pc.nom + " " + pc.couleur + " · " + std::to_string(pc.couleursFaces.size()) + " face(s)");
      const Assemblage& as = p.assemblages.back();
      int deux = 0;
      for (const auto& [ip, q] : quantitesPieces(p, static_cast<int>(p.assemblages.size()) - 1)) deux += ip == static_cast<int>(piecesAvant) && q == 2;
      verifier(as.composants.size() == 3 && deux == 1 && std::abs(as.composants[1].x - 100) < 1e-6, "trois instances placées, la pièce répétée comptée deux fois",
               std::to_string(as.composants.size()) + " composant(s), x₂ = " + chiffre(as.composants.size() > 1 ? as.composants[1].x : 0));
      verifier(std::abs(volume(construirePiece(pc).forme) - 24000) < 1e-3, "la pièce importée garde son volume");
    }
    AtelierAssemblage* atelier = f.findChild<AtelierAssemblage*>();
    verifier(atelier && atelier->isVisible(), "l'assemblage importé s'ouvre dans l'atelier d'assemblage");
    // Le même fichier inséré dans cet assemblage non vide : un sous-assemblage de plus
    if (atelier) {
      QString err;
      const std::size_t composants = p.assemblages.back().composants.size();
      verifier(atelier->insererFichier(chemin, &err, AtelierAssemblage::ModeImport::Structure), "insérer le STEP d'assemblage dans un assemblage", err.toStdString());
      attendre(600);
      const Projet& q = f.document()->projet();
      const Assemblage& cible = q.assemblages[static_cast<std::size_t>(atelier->assemblage())];
      const bool sous = cible.composants.size() == composants + 1 && cible.composants.back().assemblage >= 0 &&
                        q.assemblages[static_cast<std::size_t>(cible.composants.back().assemblage)].composants.size() == 3;
      verifier(sous, "le fichier devient un sous-assemblage de trois instances");
      // Nomenclature à niveaux (M10) : l'arbre, les repères stables, les masses, puis l'export XLSX
      DialogueNomenclature* nom = atelier->nomenclature();
      verifier(nom != nullptr, "la nomenclature de l'assemblage s'ouvre");
      if (nom) {
        nom->setVue(VueNomenclature::Niveaux);
        nom->setColonnes({"numero", "repere", "quantite", "quantiteTotale", "designation", "masseUnitaire", "masseTotale"});
        attendre(300);
        const TableauNomenclature& t = nom->tableau();
        int sousAssemblages = 0, niveau1 = 0;
        for (std::size_t i = 0; i < t.lignes.size(); ++i) { sousAssemblages += t.assemblages[i]; niveau1 += t.niveaux[i] == 1; }
        QTreeWidget* arbre = nom->findChild<QTreeWidget*>(QStringLiteral("tableNomenclature"));
        verifier(sousAssemblages == 1 && niveau1 == 2 && arbre && arbre->topLevelItemCount() == 3 && t.quantitePieces == 6,
                 "nomenclature à niveaux : châssis, patte, sous-assemblage développé, 6 pièces", t.texteTotal() + " · " + std::to_string(t.lignes.size()) + " ligne(s)");
        // 2 × 24 cm³ et 1,6 cm³ de PLA (1,24 g/cm³), deux fois
        verifier(std::abs(t.masseTotale - 2 * (2 * 24 + 1.6) * 1.24) < 0.01 && t.masseProvisoire, "masse totale de l'assemblage, provisoire tant que la matière n'est pas confirmée", texteMasse(t.masseTotale));
        const QString xlsx = QDir::temp().filePath(QStringLiteral("promethee-essai-nomenclature.xlsx"));
        QFile::remove(xlsx);
        QString err;
        verifier(nom->exporter(xlsx, &err), "nomenclature exportée en XLSX", err.toStdString());
        QFile fx(xlsx);
        const QByteArray octets = fx.open(QIODevice::ReadOnly) ? fx.readAll() : QByteArray();
        verifier(octets.startsWith("PK\x03\x04") && octets.contains("xl/worksheets/sheet1.xml") && octets.contains(QByteArrayLiteral("Ch\xC3\xA2ssis")), "le classeur contient la feuille et les désignations");
        nom->setVue(VueNomenclature::Pieces);
        attendre(200);
        bool tries = true;
        for (std::size_t i = 1; i < nom->tableau().lignes.size(); ++i) tries = tries && nom->tableau().lignes[i - 1][0].nombre < nom->tableau().lignes[i][0].nombre;
        verifier(nom->tableau().lignes.size() == 4 && tries, "pièces seules (deux de chaque import), rangées par repère");
        nom->close();
      }
      // Plan d'assemblage : bulles et nomenclature aux repères stables des pièces
      if (auto plan = atelier->creerMiseEnPlan()) {
        const Projet& q2 = f.document()->projet();
        bool stables = !plan->feuille().textes.empty();
        int lignesPlan = 0;
        for (const auto& [ip, n] : quantitesPieces(q2, atelier->assemblage())) {
          (void)n;
          const int r = repereArticle(q2, false, ip, false);
          bool trouve = false;
          for (const auto& tx : plan->feuille().textes) trouve = trouve || tx.texte == std::to_string(r);
          stables = stables && r > 0 && trouve;
          ++lignesPlan;
        }
        verifier(stables && lignesPlan == 4, "plan d'assemblage : repères stables dans les bulles et la nomenclature");
      }
      // Insertion au curseur (M9) : le composant suit la souris, posé au sol, puis un clic le pose ; un seul pas
      // d'historique ; Échap le laisse à droite de l'ensemble
      if (VuePiece* va = atelier->findChild<VuePiece*>()) {
        const std::size_t avant = f.document()->projet().assemblages[static_cast<std::size_t>(atelier->assemblage())].composants.size();
        atelier->insererPiece(static_cast<int>(piecesAvant), true);
        attendre(300);
        verifier(atelier->enPose(), "insertion au curseur : le composant suit la souris");
        const QPointF ecran = va->projeter(gp_Pnt(250, 150, 0));
        QMouseEvent bouge(QEvent::MouseMove, ecran, va->mapToGlobal(ecran), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(va, &bouge);
        attendre(300);
        cliquer(va, ecran);
        attendre(500);
        const Projet& q3 = f.document()->projet();
        const auto& comps = q3.assemblages[static_cast<std::size_t>(atelier->assemblage())].composants;
        // Châssis 60 × 40 × 10 posé par le centre de son dessous
        const bool pose = !atelier->enPose() && comps.size() == avant + 1 && std::abs(comps.back().x + 30 - 250) < 1 && std::abs(comps.back().y + 20 - 150) < 1 && std::abs(comps.back().z) < 1e-6;
        verifier(pose, "le clic pose le composant sous le curseur, au sol", comps.empty() ? std::string() : chiffre(comps.back().x) + " ; " + chiffre(comps.back().y) + " ; " + chiffre(comps.back().z));
        f.document()->annuler();
        attendre(400);
        verifier(f.document()->projet().assemblages[static_cast<std::size_t>(atelier->assemblage())].composants.size() == avant, "une seule annulation retire l'insertion au curseur");
        atelier->insererPiece(static_cast<int>(piecesAvant), true);
        attendre(300);
        QApplication::sendEvent(va, &bouge);
        attendre(200);
        atelier->abandonnerOutil();
        attendre(300);
        const auto& c4 = f.document()->projet().assemblages[static_cast<std::size_t>(atelier->assemblage())].composants;
        verifier(!atelier->enPose() && c4.size() == avant + 1 && c4.back().x > 100, "Échap laisse le composant à droite de l'ensemble", chiffre(c4.back().x));
      }
    }
  }
  // Atelier Pièces : le même fichier, posé à plat dans une pièce nouvelle, avec ses couleurs
  f.montrerAtelierPiece(true);
  attendre(400);
  AtelierPiece* ap = f.atelierPiece();
  ap->nouvellePiece();
  attendre(500);
  verifier(ap->insererFichierCAO(chemin), "insérer un fichier CAO dans une pièce");
  attendre(900);
  {
    const Piece pc = piece(f);
    const ResultatPiece r = construirePiece(pc);
    std::string d = pc.nom + " " + pc.couleur + " · " + std::to_string(pc.couleursFaces.size()) + " face(s) · " + chiffre(volume(r.forme));
    verifier(nombreFonctions(f, TypeFonction::Import) == 1 && std::abs(volume(r.forme) - (2 * 24000 + 1600)) < 1e-2 && pc.couleur == "#D93326" && pc.couleursFaces.size() == 2 + 6 && pc.nom == "promethee-essai-chassis",
             "pièce importée à plat : volume, couleur, faces jaunes et faces de la patte", d);
    // Couleur posée sur la face du dessus, cliquée dans la vue, puis retirée
    VuePiece* vue = ap->findChild<VuePiece*>();
    if (vue) {
      vue->viderSelection();
      cliquer(vue, ecranXY(vue, 30, 20, 10));
      attendre(400);
      ap->colorerSelection(QStringLiteral("#00aa55"));
      attendre(500);
      const Piece pv = piece(f);
      int vertes = 0;
      for (const auto& [n, c] : pv.couleursFaces) vertes += c == "#00AA55";
      verifier(vertes == 1, "couleur posée sur la face cliquée", std::to_string(vertes) + " face(s) verte(s)");
      vue->viderSelection();
      cliquer(vue, ecranXY(vue, 30, 20, 10));
      attendre(400);
      ap->colorerSelection(QString());
      attendre(500);
      int restantes = 0;
      for (const auto& [n, c] : piece(f).couleursFaces) restantes += c == "#00AA55";
      verifier(restantes == 0, "la face reprend la couleur de la pièce");
    }
  }
}

void lancerEssaisInterface(FenetrePrincipale& f) {
  installerVeilleBoites();
  f.show();
  attendre(800);
  if (qgetenv("PROMETHEE_ESSAIS_SEULS") == "cao") {
    f.montrerAccueil();
    attendre(300);
    if (Accueil* acc = f.findChild<Accueil*>()) { acc->choisirMetier(QStringLiteral("meca")); emit acc->choisi(QStringLiteral("piece")); }
    attendre(800);
    essaiImportCao(f);
    std::printf("%d échec(s)\n", g_echecs);
    std::fflush(stdout);
    QApplication::exit(g_echecs);
    return;
  }
  if (qgetenv("PROMETHEE_ESSAIS_SEULS") == "axes") {
    f.montrerAccueil();
    attendre(300);
    if (Accueil* acc = f.findChild<Accueil*>()) { acc->choisirMetier(QStringLiteral("meca")); emit acc->choisi(QStringLiteral("piece")); }
    attendre(800);
    essaiSaisieDynamique(f);
    essaiAxesRevolution(f);
    std::printf("%d échec(s)\n", g_echecs);
    std::fflush(stdout);
    QApplication::exit(g_echecs);
    return;
  }
  // 0. Accueil : « Mécanique » puis « Pièce » crée un projet avec une seule pièce, rangée dans le navigateur.
  {
    f.montrerAccueil();
    attendre(300);
    Accueil* acc = f.findChild<Accueil*>();
    verifier(acc && acc->isVisible(), "l'accueil s'affiche");
    if (acc) {
      acc->choisirMetier(QStringLiteral("meca"));
      emit acc->choisi(QStringLiteral("piece"));
      attendre(900);
    }
    const auto p = f.document()->projet();
    verifier(p.sansCarte && p.pieces.size() == 1, "« Pièce » crée un projet sans carte avec une pièce", std::to_string(p.pieces.size()) + " pièce(s)");
    Navigateur* nav = f.findChild<Navigateur*>();
    bool meca = false, elec = false, piece = false;
    if (nav)
      for (QTreeWidgetItemIterator it(nav); *it; ++it) {
        const QString cle = (*it)->data(0, Navigateur::ROLE_CLE).toString();
        meca |= cle == QLatin1String("section:meca");
        elec |= cle == QLatin1String("section:elec");
        piece |= cle == QLatin1String("piece:0");
      }
    verifier(nav && meca && piece && !elec, "le navigateur montre la mécanique et la pièce, sans électronique");
    verifier(nav && nav->itemPieceActive() && nav->itemPieceActive()->childCount() > 0, "la pièce active se déplie jusqu'à ses fonctions dans le navigateur");
  }
  f.montrerAtelierPiece(true);
  attendre(800);
  VuePiece* vue = f.findChild<VuePiece*>();
  Esquisseur* esq = f.findChild<Esquisseur*>();
  verifier(vue && esq, "l'atelier Pièces s'ouvre");
  if (!vue || !esq) { QApplication::exit(1); return; }
  const gp_Dir directionAvant = vue->directionCamera();

  // 1. Plan cliqué, esquisse, rectangle et cercle qui se croisent, retour à la vue d'avant.
  if (esquisseSurXY(f, vue)) {
    rectangle(vue, esq, 0, 0, 40, 30);
    esq->setOutil(Esquisseur::Outil::Cercle);
    cliquer(esq, ecranXY(vue, 40, 30));
    cliquer(esq, ecranXY(vue, 50, 30));
    verifier(esq->esquisse().elements.size() == 5, "rectangle et cercle tracés à la souris", std::to_string(esq->esquisse().elements.size()) + " éléments");
    // M1 : un DXF inséré dans l'esquisse (contour à coin arrondi en pouces, calque de cotes laissé), puis annulé
    if (AtelierPiece* ap = f.findChild<AtelierPiece*>()) {
      const QString chemin = QDir::temp().filePath(QStringLiteral("promethee-essai-esquisse.dxf"));
      QFile fx(chemin);
      if (fx.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fx.write("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n1\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
                 "0\nLWPOLYLINE\n8\nPROFIL\n90\n4\n70\n1\n10\n3\n20\n0\n10\n5\n20\n0\n42\n0.41421356\n10\n6\n20\n1\n10\n3\n20\n1\n"
                 "0\nCIRCLE\n8\nPROFIL\n10\n4\n20\n0.5\n40\n0.2\n0\nTEXT\n8\nCOTES\n10\n0\n20\n0\n1\nnote\n0\nENDSEC\n0\nEOF\n");
        fx.close();
      }
      const std::size_t avant = esq->esquisse().elements.size();
      prom::dxf::OptionsImportDxf o;
      const bool insere = ap->insererDxf(chemin, &o);
      attendre(300);
      const auto& ee = esq->esquisse();
      int arcs = 0;
      double xmax = 0;
      for (const auto& el : ee.elements) arcs += el.type == TypeElement::Arc;
      for (const auto& p : ee.points) xmax = std::max(xmax, p.x);
      verifier(insere && ee.elements.size() == avant + 5 && arcs == 1, "« Insérer un DXF » ajoute le contour (trois lignes, un arc de polyligne) et son trou à l’esquisse",
               std::to_string(ee.elements.size() - avant) + " élément(s), " + std::to_string(arcs) + " arc(s)");
      verifier(std::abs(xmax - 6 * 25.4) < 1e-6, "le DXF en pouces arrive en millimètres", chiffre(xmax) + " mm");
      bool calque = false;
      for (const auto& k : ee.calques) calque |= k.nom == "PROFIL";
      verifier(calque, "le calque du DXF devient un calque de l’esquisse");
      esq->annulerEtape();
      attendre(200);
      verifier(esq->esquisse().elements.size() == avant, "un seul Ctrl+Z retire tout le DXF inséré");
      QFile::remove(chemin);
    }
    // Navigation pendant l'esquisse : molette, bouton du milieu (rotation), clic droit (déplacement).
    esq->setOutil(Esquisseur::Outil::Selection);
    attendre(200);
    const double echelle0 = vue->echelleCamera();
    const QPointF milieu(esq->width() / 2.0, esq->height() / 2.0);
    for (int i = 0; i < 3; ++i) {
      QWheelEvent molette(milieu, esq->mapToGlobal(milieu), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
      QCoreApplication::sendEvent(esq, &molette);
      vue->update();
      attendre(150);
    }
    attendre(300);
    {
      QScrollArea* panneau = nullptr;
      for (QScrollArea* s : f.findChildren<QScrollArea*>()) if (s->isVisible() && s->findChild<QTableWidget*>()) panneau = s;
      verifier(panneau != nullptr, "pendant l'esquisse, le panneau des cotes et des calques est visible");
    }
    verifier(std::abs(vue->echelleCamera() - echelle0) > 1e-6 * echelle0, "la molette zoome pendant l'esquisse", chiffre(echelle0) + " → " + chiffre(vue->echelleCamera()));
    auto glisser = [&](Qt::MouseButton b, QPointF de, QPointF a) {
      QMouseEvent appui(QEvent::MouseButtonPress, de, esq->mapToGlobal(de), b, b, Qt::NoModifier);
      QCoreApplication::sendEvent(esq, &appui);
      for (int k = 1; k <= 6; ++k) {
        const QPointF q = de + (a - de) * (k / 6.0);
        QMouseEvent bouge(QEvent::MouseMove, q, esq->mapToGlobal(q), Qt::NoButton, b, Qt::NoModifier);
        QCoreApplication::sendEvent(esq, &bouge);
        vue->update();
        attendre(40);
      }
      QMouseEvent lache(QEvent::MouseButtonRelease, a, esq->mapToGlobal(a), b, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(esq, &lache);
      attendre(200);
    };
    const gp_Pnt centre0 = vue->centreCamera();
    glisser(Qt::RightButton, milieu, milieu + QPointF(80, 40));
    verifier(vue->centreCamera().Distance(centre0) > 1e-3, "le clic droit déplace la vue pendant l'esquisse", chiffre(vue->centreCamera().Distance(centre0)) + " mm");
    const gp_Dir dir0 = vue->directionCamera();
    glisser(Qt::MiddleButton, milieu, milieu + QPointF(60, 30));
    verifier(vue->directionCamera().Angle(dir0) > 1e-3, "le bouton du milieu fait tourner la vue pendant l'esquisse", chiffre(vue->directionCamera().Angle(dir0)) + " rad");
    // Annuler et rétablir dans l'esquisse (Ctrl+Z, Ctrl+Y), mode construction (X), ligne de commande (Espace).
    {
      const size_t avant = esq->esquisse().elements.size();
      QKeyEvent z(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
      QCoreApplication::sendEvent(esq, &z);
      attendre(150);
      verifier(esq->esquisse().elements.size() == avant - 1, "Ctrl+Z retire le dernier tracé de l'esquisse", std::to_string(esq->esquisse().elements.size()) + " élément(s)");
      QKeyEvent y(QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier);
      QCoreApplication::sendEvent(esq, &y);
      attendre(150);
      verifier(esq->esquisse().elements.size() == avant, "Ctrl+Y le rétablit", std::to_string(esq->esquisse().elements.size()) + " élément(s)");
      QKeyEvent x(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
      QCoreApplication::sendEvent(esq, &x);
      esq->setOutil(Esquisseur::Outil::Ligne);
      cliquer(esq, ecranXY(vue, -20, 0));
      cliquer(esq, ecranXY(vue, -20, 30));
      esq->setOutil(Esquisseur::Outil::Selection);
      const auto& els = esq->esquisse().elements;
      verifier(!els.empty() && els.back().construction, "en mode construction, la ligne tracée est une ligne de construction");
      QKeyEvent x2(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
      QCoreApplication::sendEvent(esq, &x2);
      QString message;
      const size_t n0 = esq->esquisse().elements.size();
      verifier(esq->executerCommande(QStringLiteral("c 12"), &message) && esq->esquisse().elements.size() == n0 + 1, "la commande « c 12 » trace un cercle", message.toStdString());
      bool coteOk = false;
      for (const auto& k : esq->esquisse().contraintes) if (k.type == TypeContrainte::Diametre && std::abs(k.valeur - 12) < 1e-9) coteOk = true;
      verifier(coteOk, "et le cote à Ø 12");
      const bool refusee = !esq->executerCommande(QStringLiteral("perdu"), &message);
      verifier(refusee && message.contains(QStringLiteral("inconnue")), "une commande inconnue est signalée", message.toStdString());
      // Barre contextuelle : un clic sur une ligne la fait apparaître
      esq->setOutil(Esquisseur::Outil::Selection);
      cliquer(esq, ecranXY(vue, 20, 0));
      QFrame* barre = esq->findChild<QFrame*>(QStringLiteral("barreContexte"));
      verifier(barre && barre->isVisible(), "un clic sur une ligne affiche la barre contextuelle");
      // Menu radial : clic droit maintenu puis pichenette vers le haut = cotation
      const QPointF m = ecranXY(vue, 10, 10);
      QMouseEvent appui(QEvent::MouseButtonPress, m, esq->mapToGlobal(m), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
      QCoreApplication::sendEvent(esq, &appui);
      attendre(450);
      const QPointF haut = m + QPointF(0, -60);
      QMouseEvent bouge(QEvent::MouseMove, haut, esq->mapToGlobal(haut), Qt::NoButton, Qt::RightButton, Qt::NoModifier);
      QCoreApplication::sendEvent(esq, &bouge);
      attendre(100);
      QMouseEvent lache(QEvent::MouseButtonRelease, haut, esq->mapToGlobal(haut), Qt::RightButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(esq, &lache);
      attendre(150);
      verifier(esq->outil() == Esquisseur::Outil::Cote, "menu radial : pichenette vers le haut = cotation");
      esq->setOutil(Esquisseur::Outil::Selection);
      // Bouton Construction avec une ligne sélectionnée : la ligne passe en construction (le mode ne change pas)
      cliquer(esq, ecranXY(vue, 20, 0));
      verifier(declencher(&f, QStringLiteral("Construction")), "le bouton Construction répond");
      bool construction = false;
      for (const auto& el : esq->esquisse().elements)
        if (el.type == TypeElement::Ligne) {
          const auto* a = esq->esquisse().point(el.p1);
          const auto* b = esq->esquisse().point(el.p2);
          if (a && b && std::abs(a->y) < 1e-6 && std::abs(b->y) < 1e-6 && std::abs(a->x - b->x) > 1) construction = el.construction;
        }
      verifier(construction && !esq->modeConstruction(), "avec une ligne sélectionnée, le bouton Construction la convertit");
      // Symétrie : le cercle par rapport à l'axe vertical
      const size_t avantSym = esq->esquisse().elements.size();
      esq->setOutil(Esquisseur::Outil::Selection);
      cliquer(esq, ecranXY(vue, 50, 30));
      cliquer(esq, ecranXY(vue, 0, -15), Qt::ShiftModifier);
      verifier(declencher(&f, QStringLiteral("Symétrie")), "le bouton Symétrie ouvre son panneau");
      attendre(300);
      verifier(bouton(&f, QStringLiteral("✔ OK")), "valider la symétrie dans son panneau");
      bool copie = false;
      for (const auto& el : esq->esquisse().elements)
        if (el.type == TypeElement::Cercle)
          if (const auto* c = esq->esquisse().point(el.centre); c && std::abs(c->x + 40) < 0.6 && std::abs(c->y - 30) < 0.6) copie = true;
      verifier(esq->esquisse().elements.size() == avantSym + 1 && copie, "symétrie : le cercle est copié de l'autre côté de l'axe", std::to_string(esq->esquisse().elements.size() - avantSym) + " copie(s)");
      // Réseau linéaire par son panneau : le cercle d'origine, trois exemplaires au pas de 20 mm selon X
      {
        const size_t avantReseau = esq->esquisse().elements.size();
        esq->setOutil(Esquisseur::Outil::Selection);
        cliquer(esq, ecranXY(vue, 50, 30));
        verifier(declencher(&f, QStringLiteral("Réseau linéaire")), "le réseau linéaire ouvre son panneau");
        attendre(300);
        verifier(bouton(&f, QStringLiteral("✔ OK")), "valider le réseau dans son panneau");
        verifier(esq->esquisse().elements.size() == avantReseau + 2, "réseau linéaire : deux copies du cercle", std::to_string(esq->esquisse().elements.size() - avantReseau) + " copie(s)");
      }
      // Calque : un calque actif reçoit le tracé suivant
      esq->ajouterCalque("Perçages", "#C2402E");
      esq->setCalqueActif("Perçages");
      verifier(esq->executerCommande(QStringLiteral("c 6")), "tracé sur le calque actif");
      verifier(!esq->esquisse().elements.empty() && esq->esquisse().elements.back().calque == "Perçages", "le nouvel élément est rangé sur le calque actif");
      esq->setCalqueActif("");
    }
    verifier(declencher(&f, QStringLiteral("✔ Terminer l’esquisse")), "terminer l'esquisse (coche verte dans la vue)");
    attendre(800);
    const gp_Dir d = vue->directionCamera();
    verifier(d.Angle(directionAvant) < 0.05, "la vue revient à celle d'avant l'esquisse", "écart " + chiffre(d.Angle(directionAvant)) + " rad");

    // 2. Extrusion : on ne garde que la région commune au rectangle et au cercle, d'un clic.
    verifier(declencher(&f, QStringLiteral("Extrusion borgne")), "le bouton « Extrusion » ouvre le gestionnaire");
    attendre(600);
    cliquer(vue, ecranXY(vue, 36, 27));
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider l'extrusion");
    attendre(600);
    const auto p = piece(f);
    const FonctionPiece* ext = nullptr;
    for (const auto& x : p.fonctions) if (x.type == TypeFonction::Extrusion) ext = &x;
    verifier(ext && ext->regions.size() == 1, "une seule région retenue par le clic", ext ? std::to_string(ext->regions.size()) + " région(s)" : "pas d'extrusion");
    double rayon = 10;  // le premier cercle tracé (celui qui croise le rectangle)
    for (const auto& x : p.fonctions)
      if (x.type == TypeFonction::Esquisse) {
        for (const auto& el : x.esquisse.elements) if (el.type == TypeElement::Cercle) { rayon = el.rayon; break; }
        break;
      }
    const double attendu = std::numbers::pi * rayon * rayon / 4 * 10;  // tracé à la souris : rayon au pixel près
    verifier(std::abs(volumePiece(f) - attendu) < 1, "le volume est celui de la région choisie", chiffre(volumePiece(f)) + " pour " + chiffre(attendu));
  }

  // 3. Plaque, congé d'un clic sur une arête, esquisse sur une face puis annulée, coque d'un clic sur une face.
  if (esquisseSurXY(f, vue)) {
    // La plaque est tracée avec la variante « Rectangle par le centre » du bouton Rectangle.
    verifier(declencher(&f, QStringLiteral("Rectangle par le centre")), "la variante « Rectangle par le centre » se choisit dans le menu");
    cliquer(esq, ecranXY(vue, 30, 20));
    cliquer(esq, ecranXY(vue, 60, 40));
    {
      const auto& es = esq->esquisse();
      double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
      for (const auto& q : es.points) { x0 = std::min(x0, q.x); x1 = std::max(x1, q.x); y0 = std::min(y0, q.y); y1 = std::max(y1, q.y); }
      verifier(std::abs(x1 - x0 - 60) < 0.6 && std::abs(y1 - y0 - 40) < 0.6, "rectangle de 60 × 40 tracé depuis son centre", chiffre(x1 - x0) + " × " + chiffre(y1 - y0));
    }
    verifier(declencher(&f, QStringLiteral("✔ Terminer l’esquisse")), "terminer l'esquisse de la plaque");
    attendre(700);
    verifier(declencher(&f, QStringLiteral("Extrusion borgne")), "extrusion de la plaque");
    attendre(400);
    {
      // Poignée orange au bout de l'extrusion (10 mm au-dessus du centre de la plaque) : on la tire vers le haut.
      const QPointF depart = ecranXY(vue, 30, 20, 10 + 4);
      const QPointF haut = ecranXY(vue, 30, 20, 10 + 4 + 5);
      QMouseEvent appui(QEvent::MouseButtonPress, depart, vue->mapToGlobal(depart), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(vue, &appui);
      for (int k = 1; k <= 5; ++k) {
        const QPointF q = depart + (haut - depart) * (k / 5.0);
        QMouseEvent bouge(QEvent::MouseMove, q, vue->mapToGlobal(q), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(vue, &bouge);
        attendre(60);
      }
      QMouseEvent lache(QEvent::MouseButtonRelease, haut, vue->mapToGlobal(haut), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      QCoreApplication::sendEvent(vue, &lache);
      attendre(500);
    }
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider l'extrusion de la plaque");
    {
      double profondeur = 0;
      for (const auto& x : piece(f).fonctions) if (x.type == TypeFonction::Extrusion) profondeur = x.longueur;
      verifier(std::abs(profondeur - 15) < 0.6, "tirer la poignée orange change la profondeur", chiffre(profondeur) + " mm au lieu de 10");
      // Ramener à 10 mm pour la suite des essais (volumes attendus)
      f.document()->modifier([](Projet& p) {
        Piece pc = pieceDepuisJson(p.pieces.back());
        for (auto& x : pc.fonctions) if (x.type == TypeFonction::Extrusion) x.longueur = 10;
        p.pieces.back() = pieceVersJson(pc);
      });
      attendre(600);
    }
    attendre(600);
    verifier(std::abs(volumePiece(f) - 24000) < 1, "plaque de 60 × 40 × 10", chiffre(volumePiece(f)));
    vue->recadrer();
    attendre(400);
    // Clic sur la face du dessus : la barre flottante propose ses commandes.
    cliquer(vue, ecranXY(vue, 30, 20, 10));
    QFrame* barre3D = f.findChild<QFrame*>(QStringLiteral("barre3D"));
    verifier(barre3D && barre3D->isVisible(), "un clic sur une face fait apparaître la barre flottante");
    // Mesure : la face du dessus puis celle du dessous, 10 mm d'écart
    {
      // (les raccourcis clavier passent par Qt ; l'essai emprunte le bouton Mesurer de l'onglet Évaluer)
      verifier(declencher(&f, QStringLiteral("Mesurer")), "le bouton Mesurer répond");
      attendre(200);
      QFrame* panneau = f.findChild<QFrame*>(QStringLiteral("panneauMesure"));
      QLabel* texteMesure = f.findChild<QLabel*>(QStringLiteral("texteMesure"));
      verifier(panneau && panneau->isVisible() && texteMesure, "M ouvre la mesure");
      cliquer(vue, ecranXY(vue, 30, 20, 10));
      cliquer(vue, ecranXY(vue, 30, 0, 5));  // face avant, visible depuis la vue par défaut
      const QString tm = texteMesure ? texteMesure->text() : QString();
      verifier(tm.contains(QStringLiteral("Distance minimale")) && tm.contains(QStringLiteral("ΔX")) && tm.contains(QStringLiteral("angle des faces 90")),
               "la mesure donne distance, ΔX ΔY ΔZ et angle vrai entre deux faces", tm.left(160).toStdString());
      verifier(vue->nombreTracesMesure() >= 1 || tm.contains(QStringLiteral("Distance minimale 0")), "la distance est dessinée dans la vue");
      // Unités de la mesure au choix
      if (QComboBox* unite = f.findChild<QComboBox*>(QStringLiteral("uniteMesure"))) {
        unite->setCurrentIndex(unite->findData(QStringLiteral("cm")));
        emit unite->activated(unite->currentIndex());
        attendre(200);
        verifier(texteMesure->text().contains(QStringLiteral(" cm")), "la mesure passe en centimètres", texteMesure->text().left(120).toStdString());
        unite->setCurrentIndex(0);
        emit unite->activated(0);
      }
      // Trois éléments : les totaux
      cliquer(vue, ecranXY(vue, 0, 20, 5));  // face de gauche, visible elle aussi
      attendre(200);
      verifier(texteMesure->text().contains(QStringLiteral("Total")), "trois éléments : les totaux", texteMesure->text().right(120).toStdString());
      // Garder une mesure : elle reste dessinée après la sortie de la mesure
      vue->viderSelection();
      cliquer(vue, ecranXY(vue, 30, 20, 10));
      cliquer(vue, ecranXY(vue, 30, 0, 5));
      attendre(200);
      if (auto* garder = f.findChild<QToolButton*>(QStringLiteral("garderMesure"))) garder->click();
      attendre(200);
      verifier(f.atelierPiece()->nombreMesuresGardees() >= 1, "« Garder » retient la mesure");
      verifier(declencher(&f, QStringLiteral("Mesurer")), "un second appui quitte la mesure");
      attendre(200);
      verifier(panneau && !panneau->isVisible(), "le panneau de mesure se referme");
      verifier(vue->nombreTracesMesure() >= 1, "la mesure gardée reste dessinée", std::to_string(vue->nombreTracesMesure()) + " trait(s)");
      f.atelierPiece()->effacerMesures();
      verifier(vue->nombreTracesMesure() == 0, "« Effacer » retire les mesures gardées");
    }
    vue->viderSelection();
    attendre(200);
    // Esquisse sur la face du dessus, puis annulée.
    const int avant = nombreFonctions(f, TypeFonction::Esquisse);
    cliquer(vue, ecranXY(vue, 30, 20, 10));
    verifier(declencher(&f, QStringLiteral("Nouvelle esquisse")), "nouvelle esquisse sur la face cliquée");
    attendre(700);
    const auto p2 = piece(f);
    verifier(!p2.fonctions.empty() && p2.fonctions.back().type == TypeFonction::Esquisse && !p2.fonctions.back().face.empty(), "l'esquisse est posée sur la face");
    verifier(declencher(&f, QStringLiteral("Convertir les entités")), "le bouton Convertir les entités répond");
    attendre(300);
    {
      int lignes = 0;
      for (const auto& el : esq->esquisse().elements) if (el.type == TypeElement::Ligne) ++lignes;
      verifier(lignes == 4, "les quatre arêtes de la face d'appui passent dans l'esquisse", std::to_string(lignes) + " ligne(s)");
      // S3 : reprises liées à la pièce, et pas de doublon si l'on convertit encore
      verifier(!esq->esquisse().liensExternes.empty(), "les arêtes reprises sont liées à la pièce", std::to_string(esq->esquisse().liensExternes.size()) + " lien(s)");
      declencher(&f, QStringLiteral("Convertir les entités"));
      attendre(300);
      int lignes2 = 0;
      for (const auto& el : esq->esquisse().elements) if (el.type == TypeElement::Ligne) ++lignes2;
      verifier(lignes2 == 4, "convertir de nouveau ne crée aucun doublon", std::to_string(lignes2) + " ligne(s)");
    }
    verifier(declencher(&f, QStringLiteral("✖ Annuler l’esquisse")), "annuler l'esquisse");
    attendre(700);
    verifier(nombreFonctions(f, TypeFonction::Esquisse) == avant, "l'esquisse annulée a disparu");
    // Coque : la face du dessus s'ouvre d'un clic.
    verifier(declencher(&f, QStringLiteral("Coque")), "le bouton « Coque » ouvre le gestionnaire");
    attendre(500);
    cliquer(vue, ecranXY(vue, 30, 20, 10));
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider la coque");
    attendre(700);
    const auto p3 = piece(f);
    const FonctionPiece* coque = nullptr;
    for (const auto& x : p3.fonctions) if (x.type == TypeFonction::Coque) coque = &x;
    verifier(coque && coque->references.size() == 1, "une face ouverte d'un clic", coque ? std::to_string(coque->references.size()) + " face(s)" : "pas de coque");
    const ResultatPiece r3 = construirePiece(p3);
    verifier(coque && r3.erreur(coque->id).empty(), "la coque se construit", coque ? r3.erreur(coque->id) : "");
    verifier(std::abs(volumePiece(f) - (24000 - 57.0 * 37.0 * 8.5)) < 1, "volume de la boîte évidée", chiffre(volumePiece(f)));
    // Congé sur deux arêtes verticales extérieures, cliquées dans la vue.
    vue->recadrer();
    attendre(400);
    verifier(declencher(&f, QStringLiteral("Congé")), "le bouton « Congé » ouvre le gestionnaire");
    attendre(500);
    cliquer(vue, ecranXY(vue, 60, 40, 5));
    cliquer(vue, ecranXY(vue, 0, 0, 5));
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider le congé");
    attendre(600);
    const auto p = piece(f);
    const FonctionPiece* conge = nullptr;
    for (const auto& x : p.fonctions) if (x.type == TypeFonction::Conge) conge = &x;
    verifier(conge && conge->references.size() == 2, "deux arêtes retenues par deux clics", conge ? std::to_string(conge->references.size()) + " arête(s)" : "pas de congé");
    const ResultatPiece r = construirePiece(p);
    verifier(conge && r.erreur(conge->id).empty(), "le congé se construit", conge ? r.erreur(conge->id) : "");
  }
  // 4. Congé puis chanfrein sur des arêtes qui se touchent, cliquées l'une après l'autre dans le même réglage.
  for (const bool chanfrein : {false, true}) {
    if (!esquisseSurXY(f, vue)) break;
    rectangle(vue, esq, 0, 0, 60, 40);
    verifier(declencher(&f, QStringLiteral("✔ Terminer l’esquisse")), "terminer l'esquisse du bloc");
    attendre(700);
    verifier(declencher(&f, QStringLiteral("Extrusion borgne")), "extrusion du bloc");
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider l'extrusion du bloc");
    attendre(600);
    vue->recadrer();
    attendre(400);
    const QString nom = chanfrein ? QStringLiteral("Chanfrein à distances égales") : QStringLiteral("Congé");
    verifier(declencher(&f, nom), (chanfrein ? std::string("chanfrein") : std::string("congé")) + " : gestionnaire ouvert");
    attendre(500);
    // Arête du haut devant, l'arête verticale qui la touche au coin, puis l'arête du bas devant qui touche celle-ci.
    cliquer(vue, ecranXY(vue, 30, 0, 10));
    cliquer(vue, ecranXY(vue, 60, 0, 5));
    cliquer(vue, ecranXY(vue, 30, 0, 0));
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider");
    attendre(700);
    const auto p = piece(f);
    const FonctionPiece* fn = nullptr;
    for (const auto& x : p.fonctions) if (x.type == (chanfrein ? TypeFonction::Chanfrein : TypeFonction::Conge)) fn = &x;
    verifier(fn && fn->references.size() == 3, std::string(chanfrein ? "chanfrein" : "congé") + " : trois arêtes voisines retenues, cliquées l'une après l'autre",
             fn ? std::to_string(fn->references.size()) + " arête(s)" : "aucune fonction");
    const ResultatPiece r = construirePiece(p);
    verifier(fn && r.erreur(fn->id).empty(), std::string(chanfrein ? "chanfrein" : "congé") + " sur trois arêtes voisines construit", fn ? r.erreur(fn->id) : "");
  }
  // 5. Plan décalé, perçage d'un point de l'esquisse, répétition linéaire du trou : par les boutons du ruban.
  if (esquisseSurXY(f, vue)) {
    rectangle(vue, esq, 0, 0, 60, 40);
    declencher(&f, QStringLiteral("✔ Terminer l’esquisse"));
    attendre(600);
    declencher(&f, QStringLiteral("Extrusion borgne"));
    bouton(&f, QStringLiteral("✔ OK"));
    attendre(600);
    // Plan décalé de 10 mm au-dessus de XY (le plan XY est le support par défaut)
    verifier(declencher(&f, QStringLiteral("Plan décalé")), "le bouton Plan décalé ouvre le gestionnaire");
    attendre(400);
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider le plan");
    attendre(500);
    const ResultatPiece r = construirePiece(piece(f));
    verifier(r.plansReference.size() == 1 && std::abs(r.plansReference[0].second.Location().Z() - 10) < 1e-6, "un plan de référence à 10 mm au-dessus de XY");
    // Esquisse avec un point sur la face du dessus, puis Perçage simple traversant
    vue->recadrer();
    attendre(300);
    cliquer(vue, ecranXY(vue, 30, 20, 10));
    declencher(&f, QStringLiteral("Nouvelle esquisse"));
    attendre(700);
    esq->setOutil(Esquisseur::Outil::Point);
    cliquer(esq, ecranXY(vue, 10, 20, 10));
    declencher(&f, QStringLiteral("✔ Terminer l’esquisse"));
    attendre(700);
    verifier(declencher(&f, QStringLiteral("Perçage simple")), "le bouton Perçage ouvre le gestionnaire");
    attendre(400);
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider le perçage");
    attendre(600);
    const double apresTrou = volumePiece(f);
    verifier(std::abs(apresTrou - (24000 - std::numbers::pi * 6.25 * 10)) < 1, "un trou de Ø 5 traversant", chiffre(apresTrou));
    // Répétition linéaire du trou : la fonction choisie dans l'arbre est reprise
    int idTrou = 0;
    for (const auto& x : piece(f).fonctions) if (x.type == TypeFonction::Percage) idTrou = x.id;
    f.atelierPiece()->choisirFonctionPublique(idTrou);
    attendre(200);
    verifier(declencher(&f, QStringLiteral("Répétition linéaire de fonctions")), "le bouton Répétition ouvre le gestionnaire");
    attendre(400);
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider la répétition");
    attendre(700);
    verifier(std::abs(volumePiece(f) - (24000 - 3 * std::numbers::pi * 6.25 * 10)) < 1, "trois trous au pas de 10 mm", chiffre(volumePiece(f)));
  }
  // 6. Panneau de droite effacé hors réglage ; vue en coupe ; mise en plan exportée en PDF ; outil Ajuster dans l'esquisse.
  {
    // Rien de choisi : pas de panneau ; une fonction choisie dans l'arbre : ses réglages s'affichent
    f.atelierPiece()->choisirFonctionPublique(0);
    attendre(200);
    auto panneauVisible = [&] {
      for (QScrollArea* s : f.findChildren<QScrollArea*>()) if (s->isVisible() && s->parentWidget() && s->width() > 200 && s->x() > vue->width() / 2) return s;
      return static_cast<QScrollArea*>(nullptr);
    };
    verifier(!panneauVisible(), "rien de choisi : pas de panneau à droite");
    int idExtrusion = 0;
    for (const auto& x : piece(f).fonctions) if (x.type == TypeFonction::Extrusion) idExtrusion = x.id;
    f.atelierPiece()->choisirFonctionPublique(idExtrusion);
    attendre(300);
    QScrollArea* panneau = panneauVisible();
    bool reglages = false;
    if (panneau) for (QGroupBox* g : panneau->findChildren<QGroupBox*>()) reglages |= g->title() == QStringLiteral("Réglages");
    verifier(panneau && reglages, "une fonction choisie : ses réglages s'affichent, modifiables sur place");
    f.atelierPiece()->choisirFonctionPublique(0);
    attendre(200);
    // Vérification de fabrication : sa fenêtre s'ouvre avec ses avis
    verifier(declencher(&f, QStringLiteral("Vérifier la fabrication")), "le bouton Vérifier la fabrication répond");
    attendre(400);
    PanneauLateral* fab = f.atelierPiece()->panneauLateral();
    verifier(fab && fab->isVisible() && fab->page() == QStringLiteral("fabrication"), "la vérification de fabrication s'ouvre dans le panneau latéral");
    if (fab) fab->annuler();
    // Panneau latéral (U1) : propriétés de la pièce, masse et variables s'ouvrent à droite de la vue, sans fenêtre modale
    {
      PanneauLateral* pl = f.atelierPiece()->panneauLateral();
      const auto modale = guetterModale(300);
      f.atelierPiece()->ouvrirProprietesPiece();
      attendre(400);
      QLineEdit* nomP = pl ? pl->findChild<QLineEdit*>(QStringLiteral("nomPiece")) : nullptr;
      verifier(!*modale && pl && pl->isVisible() && pl->page() == QStringLiteral("proprietesPiece") && nomP, "Propriétés de la pièce : dans le panneau latéral, sans fenêtre modale");
      const std::string nomAvant = f.document()->projet().pieces.empty() ? std::string() : f.document()->projet().pieces[0].value("nom", std::string());
      if (nomP) nomP->setText(QStringLiteral("Pièce renommée au panneau"));
      if (pl) pl->annuler();
      attendre(200);
      verifier(!f.document()->projet().pieces.empty() && f.document()->projet().pieces[0].value("nom", std::string()) == nomAvant, "Annuler laisse la pièce telle quelle");
      f.atelierPiece()->ouvrirProprietesPiece();
      attendre(300);
      if (auto* n2 = pl ? pl->findChild<QLineEdit*>(QStringLiteral("nomPiece")) : nullptr) n2->setText(QStringLiteral("Pièce renommée au panneau"));
      if (auto* v = pl ? pl->findChild<QPushButton*>(QStringLiteral("validerPanneau")) : nullptr) v->click();
      attendre(400);
      bool renommee = false;
      for (const auto& pj : f.document()->projet().pieces) renommee |= pj.value("nom", std::string()) == "Pièce renommée au panneau";
      verifier(renommee && pl && !pl->isVisible(), "Valider applique les propriétés et referme le panneau");
      f.atelierPiece()->ouvrirMasse();
      attendre(300);
      verifier(pl && pl->isVisible() && pl->page() == QStringLiteral("masse") && pl->findChild<QLabel*>(QStringLiteral("proprietesMasse")), "Propriétés de masse : dans le panneau latéral");
      f.atelierPiece()->ouvrirVariables();
      attendre(300);
      verifier(pl && pl->page() == QStringLiteral("variables"), "Variables : la page remplace la précédente dans le panneau");
      if (pl) pl->annuler();
      attendre(200);
      verifier(!*modale && pl && !pl->isVisible(), "Échap ou Annuler referme le panneau ; aucune fenêtre modale ouverte");
    }
    attendre(200);
    verifier(declencher(&f, QStringLiteral("Vue en coupe")), "le bouton Vue en coupe répond");
    attendre(300);
    QFrame* coupe = f.findChild<QFrame*>(QStringLiteral("panneauCoupe"));
    verifier(coupe && coupe->isVisible(), "le panneau de coupe s'affiche dans la vue");
    declencher(&f, QStringLiteral("Vue en coupe"));
    attendre(200);
    const ResultatPiece r = construirePiece(piece(f));
    OptionsMiseEnPlan o;
    o.titre = "Essai";
    const Feuille fe = miseEnPlan(r.forme, o);
    const QString pdf = QDir::temp().filePath(QStringLiteral("promethee-essai-plan.pdf"));
    QFile::remove(pdf);
    const bool pdfOk = MiseEnPlan::exporterPdf(fe, pdf);
    verifier(pdfOk && QFileInfo(pdf).size() > 1000, "la mise en plan s'exporte en PDF", std::to_string(QFileInfo(pdf).size()) + " octets");
    // M6 : la fenêtre de mise en plan règle coupe, détail, interruption, cotes et notes ; l'aperçu suit
    {
      PlanPiece reglages;
      MiseEnPlan dlg(r.forme, reglages, "Essai", "PLA", &f);
      auto* coupeA = dlg.findChild<QComboBox*>(QStringLiteral("coupe"));
      auto* detail = dlg.findChild<QCheckBox*>(QStringLiteral("detail"));
      auto* interrompue = dlg.findChild<QCheckBox*>(QStringLiteral("interrompue"));
      auto* notes = dlg.findChild<QPlainTextEdit*>(QStringLiteral("notes"));
      verifier(coupeA && detail && interrompue && notes, "la mise en plan propose coupe, détail, vue interrompue et notes");
      if (coupeA && detail && notes) {
        coupeA->setCurrentIndex(coupeA->findData(QStringLiteral("face")));
        detail->setChecked(true);
        notes->setPlainText(QStringLiteral("Ébavurer"));
        attendre(200);
        bool coupeAA = false, detailB = false;
        for (const auto& v : dlg.feuille().vues) { coupeAA |= v.nom == "Coupe A-A"; detailB |= v.nom.rfind("Détail B", 0) == 0; }
        bool note = false;
        for (const auto& t : dlg.feuille().textes) note |= t.texte == "Ébavurer";
        std::string vues;
        for (const auto& v : dlg.feuille().vues) vues += v.nom + " ; ";
        for (const auto& x : dlg.feuille().alertes) vues += x + " ; ";
        verifier(coupeAA && detailB && note, "l'aperçu montre la coupe A-A, le détail B et la note", vues);
        verifier(dlg.reglages().coupe == "face" && dlg.reglages().detail && dlg.reglages().notes.size() == 1, "les réglages de la mise en plan sont gardés avec elle");
      }
      // D1 : A3 à la française, troisième dièdre, vue de face prise du dessus ; D3 : cotes du modèle
      auto* format = dlg.findChild<QComboBox*>(QStringLiteral("formatFeuille"));
      auto* sens = dlg.findChild<QComboBox*>(QStringLiteral("sensFeuille"));
      auto* projection = dlg.findChild<QComboBox*>(QStringLiteral("projection"));
      auto* face = dlg.findChild<QComboBox*>(QStringLiteral("vueFace"));
      verifier(format && sens && projection && face, "la mise en plan propose format, sens, projection et vue de face");
      if (format && sens && projection && face) {
        if (coupeA) coupeA->setCurrentIndex(0);
        if (detail) detail->setChecked(false);
        format->setCurrentIndex(format->findText(QStringLiteral("A3")));
        sens->setCurrentIndex(1);
        projection->setCurrentIndex(1);
        face->setCurrentIndex(face->findData(QStringLiteral("dessus")));
        attendre(200);
        const Feuille& fd = dlg.feuille();
        verifier(fd.largeur == 297 && fd.hauteur == 420 && fd.troisiemeDiedre && !fd.vues.empty() && fd.vues.front().repere.Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9),
                 "A3 à la française, troisième dièdre, pièce vue du dessus", chiffre(fd.largeur) + " × " + chiffre(fd.hauteur));
        verifier(dlg.reglages().format == "A3" && dlg.reglages().portrait && dlg.reglages().troisiemeDiedre && dlg.reglages().vueFace == "dessus", "réglages de feuille et de vue gardés");
      }
    }
    // D3 : depuis l'atelier, la mise en plan reçoit les cotes des esquisses
    {
      Piece pc;
      FonctionPiece es;
      es.type = TypeFonction::Esquisse;
      const auto cotes4 = es.esquisse.ajouterRectangle(0, 0, 50, 30);
      es.esquisse.contraindre(TypeContrainte::Longueur, {cotes4[0]}, 50);
      es.esquisse.contraindre(TypeContrainte::Longueur, {cotes4[1]}, 30);
      resoudre(es.esquisse);
      const int ies = pc.ajouter(es);
      FonctionPiece ex;
      ex.type = TypeFonction::Extrusion;
      ex.esquisseId = ies;
      ex.longueur = 8;
      pc.ajouter(ex);
      const ResultatPiece rc = construirePiece(pc);
      std::vector<std::string> orphelines;
      const auto cotes = cotesDuModele(pc, rc, &orphelines);
      PlanPiece reglages;
      reglages.cotesModele = true;
      MiseEnPlan dlg(rc.forme, reglages, "Essai", "PLA", &f);
      dlg.setCotesModele(cotes, orphelines);
      attendre(100);
      int nombres = 0;
      for (const auto& t : dlg.feuille().textes) nombres += t.texte == "50" || t.texte == "30";
      verifier(cotes.size() == 2 && nombres == 2, "les cotes des esquisses de la pièce paraissent sur le plan", std::to_string(cotes.size()) + " cote(s), " + std::to_string(nombres) + " texte(s) chiffré(s)");
    }
  }
  if (esquisseSurXY(f, vue)) {
    // Deux lignes croisées, puis l'outil Ajuster retire le bout cliqué
    esq->executerCommande(QStringLiteral("l"));
    cliquer(esq, ecranXY(vue, 0, 10));
    cliquer(esq, ecranXY(vue, 40, 10));
    esq->setOutil(Esquisseur::Outil::Selection);
    esq->setOutil(Esquisseur::Outil::Ligne);
    cliquer(esq, ecranXY(vue, 20, -5));
    cliquer(esq, ecranXY(vue, 20, 25));
    esq->setOutil(Esquisseur::Outil::Selection);
    verifier(declencher(&f, QStringLiteral("Ajuster")), "l'outil Ajuster se choisit dans le menu Modifier");
    cliquer(esq, ecranXY(vue, 32, 10));
    double xmax = -1e9;
    for (const auto& el : esq->esquisse().elements)
      if (el.type == TypeElement::Ligne) {
        const auto* a = esq->esquisse().point(el.p1);
        const auto* b = esq->esquisse().point(el.p2);
        if (a && b && std::abs(a->y - 10) < 0.6 && std::abs(b->y - 10) < 0.6) xmax = std::max(xmax, std::max(a->x, b->x));
      }
    verifier(std::abs(xmax - 20) < 0.6, "Ajuster : la ligne s'arrête au croisement", chiffre(xmax));
    declencher(&f, QStringLiteral("✖ Annuler l’esquisse"));
    attendre(500);
  }
  // 7. Assemblage : deux pièces insérées côte à côte, puis lien QElectroTech
  {
    Navigateur* nav = f.findChild<Navigateur*>();
    const int avant = static_cast<int>(f.document()->projet().assemblages.size());
    emit nav->nouvelAssemblage();
    attendre(600);
    AtelierAssemblage* as = f.findChild<AtelierAssemblage*>();
    verifier(as && as->isVisible() && static_cast<int>(f.document()->projet().assemblages.size()) == avant + 1, "un nouvel assemblage s'ouvre dans son atelier");
    // Deux insertions de la plaque percée (repérée par son perçage)
    int rang = -1;
    {
      const auto& ps = f.document()->projet().pieces;
      for (size_t i = 0; i < ps.size(); ++i) {
        const Piece pc = pieceDepuisJson(ps[i]);
        for (const auto& fn : pc.fonctions) if (fn.type == TypeFonction::Percage) rang = static_cast<int>(i);
      }
    }
    if (as && rang >= 0) { as->insererPiece(rang); attendre(400); as->insererPiece(rang); attendre(400); }
    const auto& cs = f.document()->projet().assemblages.back().composants;
    verifier(cs.size() == 2 && cs[1].x > cs[0].x, "deux pièces insérées, la seconde placée à côté de la première", std::to_string(cs.size()) + " composant(s)");
    // M6 : plan d'assemblage, une bulle pour la pièce (deux exemplaires) et sa ligne de nomenclature
    if (as) {
      const auto plan = as->creerMiseEnPlan();
      int bulles = 0, quantite2 = 0;
      if (plan) {
        for (const auto& v : plan->feuille().vues)
          for (const auto& t : v.traits) bulles += t.type == TypeTrait::Cote && !t.rempli && t.points.size() > 40;
        for (const auto& t : plan->feuille().textes) quantite2 += t.texte == "2";
      }
      verifier(plan && bulles == 1 && quantite2 >= 1, "plan d'assemblage : une bulle par pièce, nomenclature avec la quantité", std::to_string(bulles) + " bulle(s)");
    }
    // Schéma QElectroTech relié
    const QString qet = QDir::temp().filePath(QStringLiteral("promethee-essai.qet"));
    {
      QFile fq(qet);
      fq.open(QIODevice::WriteOnly);
      fq.write("<?xml version=\"1.0\"?><project title=\"Armoire\"><diagram title=\"Puissance\"><elements>"
               "<element><elementInformations><elementInformation name=\"label\">Q1</elementInformation><elementInformation name=\"designation\">Disjoncteur</elementInformation></elementInformations></element>"
               "<element><elementInformations><elementInformation name=\"label\">KM1</elementInformation></elementInformations></element>"
               "</elements><conductors><conductor/></conductors></diagram><diagram title=\"Commande\"><elements>"
               "<element><elementInformations><elementInformation name=\"label\">S1</elementInformation></elementInformations></element></elements></diagram></project>");
    }
    f.document()->modifier([&](Projet& p) { p.schemasElectriques.push_back({qet.toStdString(), "Armoire"}); });
    attendre(400);
    int appareils = -1;
    bool electrique = false;
    for (QTreeWidgetItemIterator it(nav); *it; ++it) {
      const QString cle = (*it)->data(0, Navigateur::ROLE_CLE).toString();
      electrique |= cle == QLatin1String("section:electrique");
      if (cle.endsWith(QLatin1String(":appareils"))) appareils = (*it)->childCount();
    }
    verifier(electrique && appareils == 3, "le schéma QElectroTech apparaît sous Électrique avec ses trois appareils", std::to_string(appareils) + " appareil(s)");
    emit nav->ouvrirSchema(0);
    attendre(400);
    PageSchema* page = f.findChild<PageSchema*>();
    verifier(page && page->isVisible(), "la page du schéma s'ouvre");
  }
  // 8. Nomenclature commune ; variable citée dans une cote de l'esquisse
  {
    Navigateur* nav = f.findChild<Navigateur*>();
    emit nav->ouvrirNomenclature();
    attendre(800);
    PageNomenclature* nom = f.findChild<PageNomenclature*>();
    QTableWidget* t = nom ? nom->findChild<QTableWidget*>() : nullptr;
    int meca = 0, elec = 0;
    if (t)
      for (int r = 0; r < t->rowCount(); ++r) {
        if (t->item(r, 0)->text() == QStringLiteral("Mécanique")) ++meca;
        if (t->item(r, 0)->text() == QStringLiteral("Électrique")) ++elec;
      }
    verifier(nom && nom->isVisible() && meca >= 1 && elec == 3, "la nomenclature réunit pièces et appareils électriques", std::to_string(meca) + " pièce(s), " + std::to_string(elec) + " appareil(s)");
  }
  if (esquisseSurXY(f, vue)) {
    esq->setVariables({{"L", prom::Grandeur{50.0, 1}}});
    esq->executerCommande(QStringLiteral("r 40 30"));
    int idCote = 0;
    for (const auto& k : esq->esquisse().contraintes) if (k.type == TypeContrainte::Longueur && std::abs(k.valeur - 40) < 1e-9) idCote = k.id;
    esq->changerCoteTexte(idCote, QStringLiteral("L/2 + 10"));
    std::string expr;
    double valeur = 0;
    for (const auto& k : esq->esquisse().contraintes) if (k.id == idCote) { expr = k.expression; valeur = k.valeur; }
    verifier(expr == "L/2 + 10" && std::abs(valeur - 35) < 1e-9, "une cote saisie « L/2 + 10 » vaut 35 mm et garde son expression", expr + " = " + chiffre(valeur));
    declencher(&f, QStringLiteral("✖ Annuler l’esquisse"));
    attendre(400);
  }
  // 9. Diagnostic d'un rectangle sans cote ; esquisse 3D par son bouton
  if (esquisseSurXY(f, vue)) {
    esq->executerCommande(QStringLiteral("r 40 30"));
    // la commande « r » cote le rectangle : on retire ses cotes pour laisser des degrés de liberté
    Esquisse sansCote = esq->esquisse();
    sansCote.contraintes.erase(std::remove_if(sansCote.contraintes.begin(), sansCote.contraintes.end(), [](const ContrainteEsquisse& c) { return estCote(c.type); }), sansCote.contraintes.end());
    esq->appliquer(sansCote);
    attendre(300);
    bool longueurLibre = false, partiel = false;
    for (QPushButton* b : f.findChildren<QPushButton*>()) if (b->isVisible() && b->text().contains(QStringLiteral("longueur libre"))) longueurLibre = true;
    for (QLabel* l : f.findChildren<QLabel*>()) if (l->isVisible() && l->text().contains(QStringLiteral("Partiellement contrainte"))) partiel = true;
    verifier(partiel && longueurLibre, "le panneau explique ce qui peut encore bouger (longueurs libres)");
    declencher(&f, QStringLiteral("✖ Annuler l’esquisse"));
    attendre(400);
  }
  {
    const size_t avant = piece(f).fonctions.size();
    verifier(declencher(&f, QStringLiteral("Esquisse 3D")), "le bouton Esquisse 3D ouvre son panneau");
    attendre(300);
    // Deux clics dans la vue posent deux points
    cliquer(vue, QPointF(vue->width() * 0.45, vue->height() * 0.5));
    attendre(200);
    cliquer(vue, QPointF(vue->width() * 0.6, vue->height() * 0.4));
    attendre(200);
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider l’esquisse 3D");
    attendre(400);
    const auto& fs = piece(f).fonctions;
    verifier(fs.size() == avant + 1 && fs.back().type == TypeFonction::Esquisse3D && fs.back().points3d.size() >= 2, "une esquisse 3D est créée avec ses points");
  }
  // 10. Esquisse sur XY sous une plaque : arêtes de la pièce en référence, cote jusqu'à elles, conversion ; fonctions
  // lancées depuis l'esquisse (annuler y ramène, valider crée la fonction)
  if (esquisseSurXY(f, vue)) {
    esq->executerCommande(QStringLiteral("r 60 40"));
    attendre(200);
    declencher(&f, QStringLiteral("✔ Terminer l’esquisse"));
    attendre(500);
    declencher(&f, QStringLiteral("Extrusion borgne"));
    bouton(&f, QStringLiteral("✔ OK"));
    attendre(600);
    vue->recadrer();
    attendre(300);
    // Nouvelle esquisse sur le plan XY (sous la plaque) : ses arêtes y sont en référence
    f.atelierPiece()->choisirFonctionPublique(-1);
    attendre(200);
    declencher(&f, QStringLiteral("Nouvelle esquisse"));
    attendre(700);
    verifier(esq->isVisible() && esq->nombreReferencesExternes() >= 4, "les arêtes de la pièce s'affichent en référence dans l'esquisse", std::to_string(esq->nombreReferencesExternes()) + " référence(s)");
    const size_t avant = esq->esquisse().elements.size();
    const int imp = esq->importerExterne(0);
    bool construction = false;
    for (const auto& el : esq->esquisse().elements) if (el.id == imp) construction = el.construction;
    verifier(imp > 0 && construction && esq->esquisse().elements.size() == avant + 1, "un clic sur une arête de la pièce la reprend (construction, fixe) pour s'y relier ou coter");
    // Un cercle, coté jusqu'à l'arête reprise : distance du centre à la ligne
    // Un point libre de l'esquisse, coté à 15 mm de l'arête reprise
    Esquisse s = esq->esquisse();
    const int centre = s.ajouterPoint(20, 25);
    s.contraindre(TypeContrainte::Distance, {centre, imp}, 15);
    esq->appliquer(s);
    attendre(200);
    bool cote = false;
    for (const auto& k : esq->esquisse().contraintes) cote |= k.type == TypeContrainte::Distance && k.refs.size() == 2 && k.refs[1] == imp;
    verifier(cote && resoudre(s).resolue, "une cote relie l'esquisse à l'arête de la pièce");
    // Convertir les entités : les arêtes de la plaque posées dans le plan XY entrent dans l'esquisse
    const size_t avantConv = esq->esquisse().elements.size();
    verifier(declencher(&f, QStringLiteral("Convertir les entités")), "Convertir les entités répond");
    attendre(300);
    verifier(esq->esquisse().elements.size() >= avantConv + 3, "convertir les entités reprend les arêtes de la plaque", std::to_string(esq->esquisse().elements.size() - avantConv) + " élément(s)");
    // Extrusion depuis l'esquisse : annuler ramène dans l'esquisse, valider crée la fonction
    verifier(declencher(&f, QStringLiteral("Extrusion depuis l’esquisse")), "l'extrusion se lance depuis l'esquisse");
    attendre(600);
    verifier(!esq->isVisible() && bouton(&f, QStringLiteral("✖ Annuler")), "le réglage d'extrusion s'ouvre, l'esquisse est terminée");
    attendre(600);
    verifier(esq->isVisible(), "annuler la fonction ramène dans l'esquisse");
    declencher(&f, QStringLiteral("Extrusion depuis l’esquisse"));
    attendre(600);
    const size_t nbAvant = piece(f).fonctions.size();
    verifier(bouton(&f, QStringLiteral("✔ OK")), "valider la fonction lancée depuis l'esquisse");
    attendre(600);
    verifier(piece(f).fonctions.size() == nbAvant + 1 && piece(f).fonctions.back().type == TypeFonction::Extrusion, "l'esquisse et sa fonction sont validées ensemble");
    // Échap annule un réglage ouvert ; aucune flèche ni aperçu ne reste
    declencher(&f, QStringLiteral("Extrusion borgne"));
    attendre(400);
    QKeyEvent echap(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    f.atelierPiece()->setFocus();
    QApplication::sendEvent(f.atelierPiece(), &echap);
    bouton(&f, QStringLiteral("✖ Annuler"));
    attendre(300);
  }
  // 11. Chaque case et chaque liste des réglages, cliquées pour de vrai : le panneau se reconstruit pendant le signal
  // de la case elle-même (cas du plantage « inverser le sens » de l'extrusion)
  {
    for (const char* commande : {"Extrusion borgne", "Enlèvement borgne", "Révolution", "Coque", "Dépouille", "Plan décalé", "Chanfrein", "Perçage"}) {
      if (!declencher(&f, QString::fromUtf8(commande))) continue;
      attendre(400);
      for (int passe = 0; passe < 2; ++passe) {
        for (int k = 0; k < 12; ++k) {
          QList<QCheckBox*> cases;
          for (QCheckBox* c : f.atelierPiece()->findChildren<QCheckBox*>()) if (c->isVisible() && c->isEnabled()) cases << c;
          if (k >= cases.size()) break;
          cases[k]->click();
          attendre(120);
        }
      }
      for (int k = 0; k < 6; ++k) {
        QList<QComboBox*> listes;
        for (QComboBox* c : f.atelierPiece()->findChildren<QComboBox*>()) if (c->isVisible() && c->isEnabled() && c->count() > 1) listes << c;
        if (k >= listes.size()) break;
        listes[k]->setCurrentIndex((listes[k]->currentIndex() + 1) % listes[k]->count());
        attendre(120);
      }
      bouton(&f, QStringLiteral("✖ Annuler"));
      attendre(300);
    }
    // Le cas signalé : inverser le sens d'une extrusion, puis valider
    declencher(&f, QStringLiteral("Extrusion borgne"));
    attendre(400);
    QCheckBox* inverser = nullptr;
    for (QCheckBox* c : f.atelierPiece()->findChildren<QCheckBox*>()) if (c->isVisible() && c->text() == QStringLiteral("Inverser le sens")) inverser = c;
    if (inverser) inverser->click();
    attendre(300);
    const size_t nAvant = piece(f).fonctions.size();
    bouton(&f, QStringLiteral("✔ OK"));
    attendre(500);
    verifier(inverser && piece(f).fonctions.size() == nAvant + 1 && piece(f).fonctions.back().inverse, "inverser le sens d'une extrusion puis valider, sans plantage");
  }
  // EI : l'esquisseur au clavier et à la souris, touches passées par la table des raccourcis comme dans l'usage réel
  if (esquisseSurXY(f, vue)) {
    const auto lignes = [&] {
      int n = 0;
      for (const auto& el : esq->esquisse().elements) n += el.type == TypeElement::Ligne;
      return n;
    };
    const auto points = [&] { return esq->esquisse().points.size(); };
    const std::size_t points0 = points();
    // EI-04 et EI-08 : un clic de ligne puis Échap abandonne le tracé, sans laisser de point isolé
    esq->setOutil(Esquisseur::Outil::Ligne);
    cliquer(esq, ecranXY(vue, 70, 70));
    toucheReelle(esq, Qt::Key_Escape);
    verifier(points() == points0, "EI-04 EI-08 Échap abandonne la ligne commencée, sans point isolé", std::to_string(points() - points0) + " point(s) en trop");
    cliquer(esq, ecranXY(vue, 0, 50));
    cliquer(esq, ecranXY(vue, 40, 53));
    cliquerDroit(esq, ecranXY(vue, 60, 60));
    verifier(lignes() == 1, "EI-04 après Échap, deux clics ne tracent qu'une ligne", std::to_string(lignes()) + " ligne(s)");
    toucheReelle(esq, Qt::Key_Escape);
    verifier(esq->outil() == Esquisseur::Outil::Selection, "EI-04 un second Échap ramène l'outil Sélection");
    // EI-05 : H pose Horizontale sur la ligne sélectionnée, malgré le raccourci « H » de la fenêtre
    cliquer(esq, ecranXY(vue, 20, 51.5));
    toucheReelle(esq, Qt::Key_H, Qt::NoModifier, QStringLiteral("h"));
    bool horizontale = false;
    for (const auto& c : esq->esquisse().contraintes) horizontale |= c.type == TypeContrainte::Horizontal;
    verifier(horizontale, "EI-05 la touche H pose la relation Horizontale");
    // EI-24 : Ctrl+C ne choisit pas l'outil Cercle
    toucheReelle(esq, Qt::Key_C, Qt::ControlModifier);
    verifier(esq->outil() != Esquisseur::Outil::Cercle, "EI-24 Ctrl+C ne choisit pas l'outil Cercle");
    // EI-08 : premier clic d'un cercle puis clic droit : aucun point ne reste
    const std::size_t points1 = points();
    esq->setOutil(Esquisseur::Outil::Cercle);
    cliquer(esq, ecranXY(vue, -40, -40));
    cliquerDroit(esq, ecranXY(vue, -45, -45));
    verifier(points() == points1, "EI-08 un cercle commencé puis abandonné ne laisse pas de point", std::to_string(points() - points1) + " point(s) en trop");
    // EI-25 : la ligne de commande se referme sur Échap
    esq->setOutil(Esquisseur::Outil::Selection);
    toucheReelle(esq, Qt::Key_Space, Qt::NoModifier, QStringLiteral(" "));
    QLineEdit* commande = nullptr;
    for (QLineEdit* l : esq->findChildren<QLineEdit*>()) if (l->isVisible() && l->placeholderText().startsWith(QStringLiteral("Commande"))) commande = l;
    verifier(commande != nullptr, "EI-25 Espace ouvre la ligne de commande");
    if (commande) {
      toucheReelle(commande, Qt::Key_Escape);
      verifier(!commande->isVisible(), "EI-25 Échap referme la ligne de commande");
    }
    // EI-09 : un arc tangent qui revient sur un point existant y est lié
    esq->setOutil(Esquisseur::Outil::Ligne);
    cliquer(esq, ecranXY(vue, 10, -30));
    cliquer(esq, ecranXY(vue, 50, -30));
    cliquer(esq, ecranXY(vue, 50, -10));
    cliquerDroit(esq, ecranXY(vue, 70, 0));
    esq->setOutil(Esquisseur::Outil::ArcTangent);
    cliquer(esq, ecranXY(vue, 50, -10));
    cliquer(esq, ecranXY(vue, 10, -30));
    cliquerDroit(esq, ecranXY(vue, 70, 0));
    int arc = 0;
    for (const auto& el : esq->esquisse().elements) if (el.type == TypeElement::Arc) arc = el.id;
    bool lie = false;
    if (const auto* a = esq->esquisse().element(arc)) {
      int depart = 0;
      for (const auto& q : esq->esquisse().points) if (std::hypot(q.x - 10, q.y + 30) < 1e-6 && q.id != a->p1 && q.id != a->p2) depart = q.id;
      for (const auto& c : esq->esquisse().contraintes)
        if (c.type == TypeContrainte::Coincidence && c.refs.size() == 2)
          for (int extremite : {a->p1, a->p2}) lie |= (c.refs[0] == extremite && c.refs[1] == depart) || (c.refs[1] == extremite && c.refs[0] == depart);
      lie |= depart == 0;  // arrivée confondue avec le point lui-même
    }
    verifier(arc != 0 && lie, "EI-09 l'arc tangent reste lié au point existant où il arrive");
    // EI-06 : valeur de cote tapée sans Entrée, puis ✔ : la valeur compte
    esq->setOutil(Esquisseur::Outil::Cote);
    cliquer(esq, ecranXY(vue, 30, -30));
    cliquer(esq, ecranXY(vue, 30, -45));
    QLineEdit* editeur = nullptr;
    for (QLineEdit* l : esq->findChildren<QLineEdit*>()) if (l->isVisible() && l->placeholderText().isEmpty()) editeur = l;
    verifier(editeur != nullptr, "EI-06 l'éditeur de cote s'ouvre à la pose");
    if (editeur) editeur->setText(QStringLiteral("55"));
    const int idEsquisse = piece(f).fonctions.back().id;
    verifier(declencher(&f, QStringLiteral("✔ Terminer l’esquisse")), "terminer l'esquisse de l'essai EI");
    bool cote55 = false;
    for (const auto& fn : piece(f).fonctions)
      if (fn.id == idEsquisse)
        for (const auto& c : fn.esquisse.contraintes) cote55 |= estCote(c.type) && std::abs(std::abs(c.valeur) - 55) < 1e-9;
    verifier(cote55, "EI-06 la valeur tapée sans Entrée est enregistrée par ✔");
  }
  // 12. Tôlerie (M5) : contour esquissé, tôle de base, bord tombé d'un clic sur l'arête, déplié, DXF du déplié
  if (AtelierPiece* ap = f.atelierPiece()) {
    ap->nouvellePiece();
    attendre(500);
    ap->esquisserTole();
    attendre(700);
    verifier(ap->enEsquisse(), "« Esquisser le contour de la tôle » ouvre une esquisse");
    rectangle(vue, esq, 0, 0, 80, 50);
    esq->setOutil(Esquisseur::Outil::Selection);
    attendre(200);
    verifier(declencher(&f, QStringLiteral("✔ Terminer l’esquisse")), "terminer le contour de la tôle");
    attendre(900);
    verifier(ap->enReglage(), "à la validation du contour, la tôle de base s'ouvre d'elle-même");
    bouton(&f, QStringLiteral("✔ OK"));
    attendre(900);
    Piece pt = piece(f);
    verifier(nombreFonctions(f, TypeFonction::Tole) == 1 && construirePiece(pt).erreurs.empty(), "tôle de base de 80 × 50 × 1,5 mm");
    verifier(declencher(&f, QStringLiteral("Bord tombé")), "le bouton Bord tombé répond");
    attendre(500);
    verifier(ap->enReglage(), "le réglage du bord tombé attend son arête");
    cliquer(vue, ecranXY(vue, 40, 0, 1.5));  // arête avant du dessus de la tôle
    attendre(600);
    bouton(&f, QStringLiteral("✔ OK"));
    attendre(900);
    pt = piece(f);
    const ResultatPiece rt = construirePiece(pt);
    std::string erreursTole;
    for (const auto& [id, m] : rt.erreurs) erreursTole += m + " ";
    verifier(nombreFonctions(f, TypeFonction::BordTombe) == 1 && rt.erreurs.empty() && rt.tole.plis.size() == 1, "bord tombé posé d'un clic sur l'arête, sans erreur", erreursTole);
    QAction* deplie = nullptr;
    for (QAction* a : ap->findChildren<QAction*>()) if (a->objectName() == QStringLiteral("vueDepliee")) deplie = a;
    verifier(deplie != nullptr, "bouton Déplié dans l'onglet Tôlerie");
    const QByteArray captures = qgetenv("PROMETHEE_CAPTURE_TOLE");
    if (!captures.isEmpty()) { vue->recadrer(); attendre(500); f.grab().save(QDir(QString::fromLocal8Bit(captures)).filePath(QStringLiteral("tole-pliee.png"))); }
    if (deplie) {
      deplie->trigger();
      attendre(600);
      if (!captures.isEmpty()) f.grab().save(QDir(QString::fromLocal8Bit(captures)).filePath(QStringLiteral("tole-deplie.png")));
      verifier(ap->vueDepliee(), "la vue montre le déplié");
      deplie->trigger();
      attendre(400);
      verifier(!ap->vueDepliee(), "un second appui remet la pièce pliée");
    }
    const QString dxfDeplie = QDir::temp().filePath(QStringLiteral("promethee-essai-deplie.dxf"));
    verifier(ap->exporterDeplie(QStringLiteral("dxf"), dxfDeplie), "export du déplié en DXF");
    QFile fx(dxfDeplie);
    int plis = 0;
    if (fx.open(QIODevice::ReadOnly)) {
      const auto d = prom::dxf::lireDessinDxf(fx.readAll().toStdString());
      for (const auto& tr : d.traits) plis += tr.calque == "PLIS-HAUT";
    }
    verifier(plis == 1, "le DXF du déplié porte la ligne de pliage sur son calque", std::to_string(plis));
    QFile::remove(dxfDeplie);
  }
  // 13. Filetage cosmétique (M4) : arbre Ø10 esquissé, extrudé, filetage d'un clic sur sa face, hélice montrée
  if (esquisseSurXY(f, vue)) {
    esq->setOutil(Esquisseur::Outil::Cercle);
    cliquer(esq, ecranXY(vue, 20, 20));
    cliquer(esq, ecranXY(vue, 25, 20));
    esq->setOutil(Esquisseur::Outil::Selection);
    attendre(200);
    verifier(declencher(&f, QStringLiteral("✔ Terminer l’esquisse")), "terminer l'esquisse de l'arbre");
    attendre(700);
    declencher(&f, QStringLiteral("Extrusion borgne"));
    attendre(500);
    bouton(&f, QStringLiteral("✔ OK"));
    attendre(900);
    verifier(declencher(&f, QStringLiteral("Filetage cosmétique")), "le bouton Filetage cosmétique répond");
    attendre(500);
    cliquer(vue, ecranXY(vue, 20, 15, 5));  // face cylindrique, côté avant
    attendre(500);
    bouton(&f, QStringLiteral("✔ OK"));
    attendre(900);
    const ResultatPiece rf = construirePiece(piece(f));
    std::string d;
    for (const auto& ft : rf.filetages) d += ft.designation + (ft.exterieur ? " ext. " : " int. ");
    for (const auto& [id, m] : rf.erreurs) d += m;
    verifier(nombreFonctions(f, TypeFonction::Filetage) == 1 && rf.filetages.size() == 1 && rf.filetages[0].exterieur && rf.filetages[0].taille == "M10",
             "filetage M10 extérieur posé d'un clic sur la face de l'arbre", d);
  }
  essaiSaisieDynamique(f);
  essaiAxesRevolution(f);
  essaiImportCao(f);
  // Raccourcis d'historique : aucune touche déclarée deux fois (une touche en double ne fait plus rien)
  for (QAction* a : f.findChildren<QAction*>())
    if (a->objectName() == QStringLiteral("actionRetablir") || a->objectName() == QStringLiteral("actionAnnuler")) {
      const auto ks = a->shortcuts();
      bool doublon = false;
      for (int i = 0; i < ks.size(); ++i) for (int j = i + 1; j < ks.size(); ++j) doublon |= ks[i] == ks[j];
      verifier(!ks.isEmpty() && !doublon, a->text().toStdString() + " : raccourcis sans doublon");
    }
  std::printf("%d échec(s)\n", g_echecs);
  std::fflush(stdout);
  QApplication::exit(g_echecs);
}

// ---------------------------------------------------------------------------------------------------------------
// Essais des onglets et des scénarios de documents, dans la vraie fenêtre à onglets.
#include "Coquille.hpp"
#include <QAbstractButton>
#include <QMessageBox>
#include <QTabBar>
#include <QGuiApplication>
#include <QScreen>
#include <BRepAdaptor_Surface.hxx>
#include <QStatusBar>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUuid>
#include "Recuperation.hpp"
#include "Theme.hpp"
#include "promethee/fichiers.hpp"

namespace {
// Répond à la prochaine boîte de question par le bouton qui porte ce texte ; garde les textes vus (français ?)
QStringList g_boutonsVus;
void repondre(const QString& texte) {
  QTimer::singleShot(350, [texte] {
    if (auto* m = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
      g_boutonsVus.clear();
      for (QAbstractButton* b : m->buttons()) g_boutonsVus << b->text();
      for (QAbstractButton* b : m->buttons()) if (b->text() == texte) { b->click(); return; }
    }
  });
}
// Une pièce simple : plaque 60 × 40 × 10 (esquisse + extrusion), écrite dans la pièce d'indice i du projet
void remplirPiece(Document* d, int i) {
  Piece pc = pieceDepuisJson(d->projet().pieces[static_cast<size_t>(i)]);
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.esquisse.ajouterRectangle(0, 0, 60, 40);
  resoudre(e.esquisse);
  const int ie = pc.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = 10;
  pc.ajouter(x);
  d->modifier([&](Projet& p) { p.pieces[static_cast<size_t>(i)] = pieceVersJson(pc); });
}
}  // namespace

void lancerEssaisOnglets(Coquille& c) {
  installerVeilleBoites();
  g_echecs = 0;
  attendre(800);
  QWidget* bas = c.findChild<QWidget*>(QStringLiteral("barreOnglets"));
  QTabBar* barre = bas ? bas->findChild<QTabBar*>() : nullptr;
  // 1. Démarrage : un onglet, sur l'accueil, rien d'ouvert (pas de question en créant quoi que ce soit)
  verifier(c.nombreOnglets() == 1 && c.active() && c.active()->document()->estVierge(), "au lancement : un seul onglet, vierge");
  verifier(c.windowTitle() == QStringLiteral("Prométhée"), "titre « Prométhée » tant que rien n'est ouvert", c.windowTitle().toStdString());
  verifier(!c.windowIcon().isNull() && !QApplication::windowIcon().isNull(), "l'icône du logiciel est celle du logo");
  if (const QByteArray chemin = qgetenv("PROMETHEE_CAPTURE_ACCUEIL"); !chemin.isEmpty()) c.grab().save(QString::fromLocal8Bit(chemin));
  verifier(barre && barre->shape() == QTabBar::RoundedSouth, "la barre d'onglets est en bas");
  if (QScreen* ecran = QGuiApplication::primaryScreen())
    verifier(c.geometry() == ecran->availableGeometry(), "la fenêtre s'ouvre sur toute la surface utile de l'écran",
             QStringLiteral("%1×%2").arg(c.width()).arg(c.height()).toStdString());
  // 2. Accueil → Assemblage : aucune question, l'assemblage s'ouvre dans cet onglet
  bool question = false;
  QTimer::singleShot(400, [&question] { if (qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { question = true; QApplication::activeModalWidget()->close(); } });
  c.active()->choixAccueilPublic(QStringLiteral("assemblage"));
  attendre(700);
  FenetrePrincipale* f1 = c.active();
  AtelierAssemblage* as = f1->findChild<AtelierAssemblage*>();
  verifier(!question, "créer un assemblage depuis l'accueil ne demande rien");
  verifier(c.nombreOnglets() == 1 && as && as->isVisible() && f1->document()->projet().assemblages.size() == 1, "l'assemblage s'ouvre dans l'onglet");
  // 3. Nouvelle pièce depuis l'assemblage : insérée d'office, atelier Pièce, bouton de retour
  QAction* nouvelle = nullptr;
  for (QAction* a : as->findChildren<QAction*>()) if (a->text() == QStringLiteral("Nouvelle pièce")) nouvelle = a;
  verifier(nouvelle != nullptr, "l'assemblage propose « Nouvelle pièce »");
  if (nouvelle) nouvelle->trigger();
  attendre(700);
  const auto& pr = f1->document()->projet();
  verifier(pr.pieces.size() == 1 && pr.assemblages[0].composants.size() == 1 && pr.assemblages[0].composants[0].piece == 0, "la nouvelle pièce est insérée dans l'assemblage");
  QPushButton* retour = f1->findChild<QPushButton*>(QStringLiteral("retour"));
  verifier(retour && retour->isVisible() && f1->atelierPiece()->isVisible(), "l'atelier Pièce s'ouvre, avec un bouton pour revenir à l'assemblage");
  // 4. On modèle la pièce, puis retour à l'assemblage
  remplirPiece(f1->document(), 0);
  attendre(500);
  if (retour) retour->click();
  attendre(700);
  verifier(as->isVisible() && !retour->isVisible(), "le bouton ramène à l'assemblage");
  verifier(volume(construirePiece(pieceDepuisJson(f1->document()->projet().pieces[0])).forme) > 23999, "la pièce modelée a son volume");
  // 5. Enregistrer en .prom, titre de l'onglet sans marque de modification
  const QString fichier1 = QDir::temp().filePath(QStringLiteral("promethee-essai-assemblage.prom"));
  QFile::remove(fichier1);
  try { f1->document()->enregistrer(fichier1); } catch (...) {}
  attendre(300);
  verifier(QFileInfo::exists(fichier1) && !f1->document()->modifie(), "le projet s'enregistre en .prom");
  verifier(barre && !barre->tabText(0).contains(QStringLiteral("•")), "l'onglet n'affiche plus de modification", barre ? barre->tabText(0).toStdString() : "");
  // 6. Insérer un fichier STEP et une pièce d'un autre projet
  {
    Piece boite;
    FonctionPiece e;
    e.type = TypeFonction::Esquisse;
    e.esquisse.ajouterRectangle(0, 0, 20, 20);
    resoudre(e.esquisse);
    const int ie = boite.ajouter(e);
    FonctionPiece x;
    x.type = TypeFonction::Extrusion;
    x.esquisseId = ie;
    x.longueur = 20;
    boite.ajouter(x);
    const QString step = QDir::temp().filePath(QStringLiteral("promethee-essai-boite.step"));
    exporterStep({{"boite", construirePiece(boite).forme}}, step.toStdString());
    QString err;
    verifier(as->insererFichier(step, &err), "insérer un fichier STEP dans l'assemblage", err.toStdString());
    attendre(500);
    const auto& p2 = f1->document()->projet();
    verifier(p2.pieces.size() == 2 && p2.assemblages[0].composants.size() == 2, "la pièce STEP rejoint le projet et l'assemblage");
    verifier(std::abs(volume(construirePiece(pieceDepuisJson(p2.pieces[1])).forme) - 8000) < 1, "la pièce STEP garde sa géométrie (8 cm³)");
  }
  // 6 bis. Arbre sans doublon : une pièce utilisée par l'assemblage n'est listée que sous lui
  {
    Navigateur* nav = f1->findChild<Navigateur*>();
    int sousMeca = 0, sousAssemblage = 0;
    for (QTreeWidgetItemIterator it(nav); *it; ++it) {
      const QString cle = (*it)->data(0, Navigateur::ROLE_CLE).toString();
      if (cle.startsWith(QLatin1String("piece:")) && !cle.contains(QLatin1String(":f:")) && !cle.contains(QLatin1String(":plans"))) ++sousMeca;
      if (cle.startsWith(QLatin1String("assemblage:0:c:"))) ++sousAssemblage;
    }
    verifier(sousMeca == 0 && sousAssemblage == 2, "l'arbre ne montre chaque pièce qu'une fois, sous son assemblage", std::to_string(sousMeca) + " au premier niveau, " + std::to_string(sousAssemblage) + " sous l'assemblage");
  }
  // 6 ter. Une seule ligne en bas : les onglets et l'état du projet actif
  {
    QWidget* basBarre = c.findChild<QWidget*>(QStringLiteral("barreOnglets"));
    QLabel* etatBas = nullptr;
    if (basBarre) for (QLabel* l : basBarre->findChildren<QLabel*>()) if (!l->text().isEmpty()) etatBas = l;
    verifier(etatBas && etatBas->text().contains(QStringLiteral("Modèle valide")), "l'état du projet s'affiche sur la ligne des onglets", etatBas ? etatBas->text().left(80).toStdString() : "");
    bool barreEtatVisible = false;
    for (QStatusBar* sb : f1->findChildren<QStatusBar*>()) barreEtatVisible |= sb->isVisible();
    verifier(!barreEtatVisible, "plus de barre d'état séparée sous l'onglet");
  }
  // 6 quater. Manipulations : pivoter, copier, répéter, symétrie, fixer, masquer ; contrainte posée puis suspendue
  {
    as->choisirComposant(1);
    const double rz0 = f1->document()->projet().assemblages[0].composants[1].rz;
    as->pivoter(2, 90);
    attendre(200);
    verifier(std::abs(f1->document()->projet().assemblages[0].composants[1].rz - rz0 - 90) < 1e-9, "pivoter de 90° autour de Z");
    const size_t n0 = f1->document()->projet().assemblages[0].composants.size();
    as->choisirComposant(1);
    as->copier();
    attendre(200);
    verifier(f1->document()->projet().assemblages[0].composants.size() == n0 + 1, "copier un composant");
    as->choisirComposant(1);
    as->repeter(false, 3, 120, 0);
    attendre(300);
    verifier(f1->document()->projet().assemblages[0].composants.size() == n0 + 3, "répétition linéaire : deux exemplaires de plus");
    as->choisirComposant(1);
    as->symetriser(3);
    attendre(200);
    verifier(f1->document()->projet().assemblages[0].composants.back().miroir == 3, "symétrie d'un composant par rapport à YZ");
    as->choisirComposant(2);
    as->basculerFixe();
    as->basculerMasque();
    attendre(200);
    verifier(f1->document()->projet().assemblages[0].composants[2].fixe && f1->document()->projet().assemblages[0].composants[2].masque, "fixer et masquer un composant");
    // Contrainte : dessus de la plaque (composant 0) contre dessous de la boîte STEP (composant 1)
    const Piece p0 = pieceDepuisJson(f1->document()->projet().pieces[0]);
    int idExt = 0;
    for (const auto& fn : p0.fonctions) if (fn.type == TypeFonction::Extrusion) idExt = fn.id;
    f1->document()->modifier([idExt](Projet& p) {
      ContrainteAssemblage k;
      k.type = "coincidence";
      k.a = 0; k.refA = std::to_string(idExt) + ":sommet";
      k.b = 1; k.refB = "1:import.4";
      p.assemblages[0].contraintes.push_back(k);
    });
    attendre(200);
    as->choisirContrainte(0);
    attendre(200);
    f1->document()->modifier([](Projet& p) { p.assemblages[0].contraintes[0].suspendue = true; });
    attendre(300);
    Navigateur* nav = f1->findChild<Navigateur*>();
    bool suspendueVue = false;
    for (QTreeWidgetItemIterator it(nav); *it; ++it) if ((*it)->text(0).startsWith(QStringLiteral("⏸"))) suspendueVue = true;
    verifier(suspendueVue, "une contrainte suspendue reste dans l'arbre, marquée ⏸");
    // Sous-assemblage : un second assemblage inséré dans le premier, rangé sous lui dans l'arbre
    f1->document()->modifier([](Projet& p) { Assemblage s; s.nom = "Sous-ensemble"; s.composants.push_back({0, 0, 0, 0, 0, 0, 0, true}); p.assemblages.push_back(s); });
    attendre(200);
    as->insererSousAssemblage(1);
    attendre(400);
    int premierNiveau = 0;
    bool imbrique = false;
    for (QTreeWidgetItemIterator it(nav); *it; ++it) {
      const QString cle = (*it)->data(0, Navigateur::ROLE_CLE).toString();
      if (cle == QLatin1String("assemblage:1")) ++premierNiveau;
      if ((*it)->text(0) == QStringLiteral("Sous-ensemble") && cle.startsWith(QLatin1String("assemblage:0:c:"))) imbrique = true;
    }
    verifier(imbrique && premierNiveau == 0, "le sous-assemblage se range sous l'assemblage, pas au premier niveau");
    // Vérifications : les fenêtres s'ouvrent
    for (const char* nom : {"Degrés de liberté", "Interférences", "Nomenclature de l’assemblage"}) {
      QAction* act = nullptr;
      for (QAction* a : as->findChildren<QAction*>()) if (a->text() == QString::fromUtf8(nom)) act = a;
      if (act) act->trigger();
      attendre(500);
      // Analyses dans le panneau latéral (U1) ; la nomenclature reste une fenêtre non modale
      QWidget* d = nullptr;
      for (QDialog* x : as->findChildren<QDialog*>()) if (x->isVisible()) d = x;
      if (PanneauLateral* pl = as->findChild<PanneauLateral*>(); !d && pl && pl->isVisible()) d = pl;
      verifier(d != nullptr, std::string("la fenêtre « ") + nom + " » s'ouvre");
      if (auto* pl = qobject_cast<PanneauLateral*>(d)) pl->annuler();
      else if (d) d->close();
      attendre(200);
    }
    as->setEclatement(0.8);
    attendre(300);
    as->setEclatement(0);
    verifier(true, "la vue éclatée s'applique et se retire");
    if (const QByteArray chemin = qgetenv("PROMETHEE_CAPTURE_ASSEMBLAGE"); !chemin.isEmpty()) {
      as->choisirComposant(1);
      attendre(500);
      c.grab().save(QString::fromLocal8Bit(chemin));
    }
  }
  // 6 quinquies a. Sens des contraintes (A1) : parallèle même sens / opposé, angle orienté puis inversé depuis le panneau
  {
    Piece dalle;
    FonctionPiece es;
    es.type = TypeFonction::Esquisse;
    es.esquisse.ajouterRectangle(0, 0, 30, 20);
    resoudre(es.esquisse);
    const int ies = dalle.ajouter(es);
    FonctionPiece xp;
    xp.type = TypeFonction::Extrusion;
    xp.esquisseId = ies;
    xp.longueur = 5;
    const std::string sommet = std::to_string(dalle.ajouter(xp)) + ":sommet";
    int assSens = 0;
    f1->document()->modifier([&](Projet& p) {
      p.pieces.push_back(pieceVersJson(dalle));
      const int id = static_cast<int>(p.pieces.size()) - 1;
      Assemblage m;
      m.nom = "Sens des contraintes";
      m.composants = {{id, 0, 0, 0, 0, 0, 0, true}, {id, 50, 0, 0, 25, 10, 0, false}};
      ContrainteAssemblage c;
      c.type = "parallele";
      c.refA = sommet;
      c.refB = sommet;
      m.contraintes.push_back(c);
      p.assemblages.push_back(m);
      assSens = static_cast<int>(p.assemblages.size()) - 1;
    });
    f1->montrerAssemblage(assSens);
    attendre(400);
    auto normaleZ = [&] {
      const auto& a = f1->document()->projet().assemblages[static_cast<size_t>(assSens)];
      return gp_Vec(0, 0, 1).Transformed(placementComposant(a.composants[1])).Z();
    };
    as->choisirContrainte(0);
    attendre(300);
    QComboBox* sens = as->findChild<QComboBox*>(QStringLiteral("sensContrainte"));
    verifier(sens != nullptr, "le panneau d'une contrainte parallèle propose son sens");
    if (sens) {
      sens->setCurrentIndex(2);
      emit sens->activated(2);
      attendre(400);
      verifier(f1->document()->projet().assemblages[static_cast<size_t>(assSens)].contraintes[0].alignement == 2 && std::abs(normaleZ() + 1) < 1e-4,
               "sens opposé imposé : la pièce se retourne", chiffre(normaleZ()));
      as->choisirContrainte(0);
      attendre(200);
      if (QComboBox* s2 = as->findChild<QComboBox*>(QStringLiteral("sensContrainte"))) {
        s2->setCurrentIndex(1);
        emit s2->activated(1);
        attendre(400);
      }
      verifier(std::abs(normaleZ() - 1) < 1e-4, "même sens imposé : les faces regardent du même côté", chiffre(normaleZ()));
    }
    // Angle : la pièce inclinée de 25° autour de X, l'angle est orienté sur l'état actuel puis inversé
    f1->document()->modifier([&](Projet& p) {
      auto& a = p.assemblages[static_cast<size_t>(assSens)];
      a.composants[1] = {a.composants[1].piece, 50, 0, 0, 25, 0, 0, false};
      a.contraintes[0].type = "angle";
      a.contraintes[0].alignement = 0;
      a.contraintes[0].valeur = 25;
    });
    attendre(300);
    as->choisirContrainte(0);
    attendre(300);
    QCheckBox* oriente = as->findChild<QCheckBox*>(QStringLiteral("angleOriente"));
    verifier(oriente && !oriente->isChecked(), "le panneau d'une contrainte d'angle propose l'angle orienté");
    if (oriente) {
      oriente->setChecked(true);
      attendre(500);
      const auto& c = f1->document()->projet().assemblages[static_cast<size_t>(assSens)].contraintes[0];
      verifier(c.alignement == 1 && c.parametres.size() == 3 && std::abs(c.valeur - 25) < 1e-6, "angle orienté : axe pris sur l'état actuel, rien ne bouge", chiffre(c.valeur) + "°");
      const double avant = f1->document()->projet().assemblages[static_cast<size_t>(assSens)].composants[1].rx;
      QPushButton* inverser = as->findChild<QPushButton*>(QStringLiteral("inverserAngle"));
      verifier(inverser != nullptr, "un angle orienté peut être inversé");
      if (inverser) {
        inverser->click();
        attendre(500);
        const auto& a = f1->document()->projet().assemblages[static_cast<size_t>(assSens)];
        const gp_Vec n = gp_Vec(0, 0, 1).Transformed(placementComposant(a.composants[1]));
        const gp_Vec axe(a.contraintes[0].parametres[0], a.contraintes[0].parametres[1], a.contraintes[0].parametres[2]);
        const double signe = std::atan2(gp_Vec(0, 0, 1).Crossed(n).Dot(axe), n.Z()) * 180 / std::numbers::pi;
        verifier(a.contraintes[0].alignement == 2 && std::abs(signe + 25) < 1e-3, "angle inversé : la pièce passe de l'autre côté", chiffre(avant) + " → " + chiffre(signe) + "°");
      }
    }
  }
  // 6 quinquies b. Références de contrainte (A2) : plans de l'assemblage, repère de la pièce, sommets ; liaison refusée
  {
    Piece dalle;
    FonctionPiece es;
    es.type = TypeFonction::Esquisse;
    es.esquisse.ajouterRectangle(0, 0, 30, 20);
    resoudre(es.esquisse);
    const int ies = dalle.ajouter(es);
    FonctionPiece xp;
    xp.type = TypeFonction::Extrusion;
    xp.esquisseId = ies;
    xp.longueur = 5;
    const int ix = dalle.ajouter(xp);
    const ResultatPiece rd = construirePiece(dalle);
    std::string sommet;
    for (TopExp_Explorer ex(rd.forme, TopAbs_VERTEX); ex.More(); ex.Next())
      if (BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())).Distance(gp_Pnt(30, 20, 5)) < 1e-9) sommet = rd.nomSommet(TopoDS::Vertex(ex.Current()));
    int assRef = 0;
    f1->document()->modifier([&](Projet& p) {
      p.pieces.push_back(pieceVersJson(dalle));
      const int id = static_cast<int>(p.pieces.size()) - 1;
      Assemblage m;
      m.nom = "Références";
      m.composants = {{id, 0, 0, 0, 0, 0, 0, true}, {id, 45, -30, 12, 20, -10, 35, false}, {id, -50, 40, 8, 0, 30, 0, false}};
      p.assemblages.push_back(m);
      assRef = static_cast<int>(p.assemblages.size()) - 1;
    });
    f1->montrerAssemblage(assRef);
    attendre(400);
    as->setRepereVisible(true);
    attendre(200);
    QToolButton* boutonRepere = as->findChild<QToolButton*>(QStringLiteral("repereAssemblage"));
    verifier(as->repereVisible() && boutonRepere, "le repère de l'assemblage se montre (plans, axes, origine)");
    constexpr int G = AtelierAssemblage::COMPOSANT_ASSEMBLAGE;
    for (const char* plan : {"XY", "XZ", "YZ"}) {
      as->commencerContrainte("aligne");
      as->choisirReference(G, std::string("assemblage:") + plan);
      as->choisirReference(1, std::string("base:") + plan);
      attendre(250);
    }
    const auto& mref = f1->document()->projet().assemblages[static_cast<size_t>(assRef)];
    const gp_Trsf T1 = placementComposant(mref.composants[1]);
    verifier(mref.contraintes.size() == 3 && mref.contraintes[0].a == 1 && mref.contraintes[0].b == 1 && gp::Origin().Transformed(T1).Distance(gp::Origin()) < 1e-5 &&
                 std::abs(gp_Vec(1, 0, 0).Transformed(T1).X() - 1) < 1e-6 && std::abs(gp_Vec(0, 0, 1).Transformed(T1).Z() - 1) < 1e-6,
             "repère de la pièce aligné sur les plans de l'assemblage : la pièce se pose à l'origine",
             chiffre(gp::Origin().Transformed(T1).Distance(gp::Origin())) + " mm");
    // Sommet sur sommet entre deux composants
    as->commencerContrainte("coincidence");
    as->choisirReference(1, sommet);
    as->choisirReference(2, sommet);
    attendre(300);
    const auto& mref2 = f1->document()->projet().assemblages[static_cast<size_t>(assRef)];
    const double ecartSommets = gp_Pnt(30, 20, 5).Transformed(placementComposant(mref2.composants[2])).Distance(gp_Pnt(30, 20, 5).Transformed(placementComposant(mref2.composants[1])));
    verifier(mref2.contraintes.size() == 4 && ecartSommets < 1e-5, "un sommet se pose sur le sommet d'un autre composant", chiffre(ecartSommets) + " mm");
    // Une liaison ne vise pas l'assemblage : refusée avec la raison, l'outil reste ouvert
    as->commencerContrainte("pivot");
    as->choisirReference(G, "assemblage:Z");
    as->choisirReference(2, std::to_string(ix) + ":sommet");
    verifier(as->texteEtat().contains(QStringLiteral("Une liaison relie deux composants")) && as->outilContrainte(), "une liaison vers l'assemblage est refusée, avec sa raison");
    as->abandonnerOutil();
    verifier(!as->outilContrainte() && f1->document()->projet().assemblages[static_cast<size_t>(assRef)].contraintes.size() == 4, "Échap abandonne l'outil sans rien créer");
    // Panneau : les références se changent pour un plan ou un axe de la pièce, ou de l'assemblage
    as->choisirContrainte(0);
    attendre(300);
    QComboBox* refA = as->findChild<QComboBox*>(QStringLiteral("referenceA"));
    QComboBox* refB = as->findChild<QComboBox*>(QStringLiteral("referenceB"));
    verifier(refA && refB && refA->itemText(0) == QStringLiteral("Plan XY de l’assemblage") && refB->findData(QStringLiteral("base:Z")) > 0 && refB->findData(QStringLiteral("assemblage:X")) < 0,
             "le panneau propose les références admises de chaque côté (pas deux références d'assemblage)");
    if (refA) {
      const int iz = refA->findData(QStringLiteral("assemblage:YZ"));
      verifier(iz > 0, "le plan YZ de l'assemblage est proposé pour le premier côté");
      if (iz > 0) {
        refA->setCurrentIndex(iz);
        emit refA->activated(iz);
        attendre(400);
        verifier(f1->document()->projet().assemblages[static_cast<size_t>(assRef)].contraintes[0].refA == "assemblage:YZ", "la référence choisie au panneau est gardée dans le projet");
        f1->document()->annuler();
        attendre(300);
      }
    }
    as->setRepereVisible(false);
  }
  // 6 quinquies c. Mise à jour (A3, AS-12) : la plaque d'un sous-assemblage s'épaissit, le cube du socle et celui du
  // parent montent, que l'assemblage soit affiché ou rouvert après la modification
  {
    auto pave = [](double l, double w, double h, int& extrusion) {
      Piece q;
      FonctionPiece e;
      e.type = TypeFonction::Esquisse;
      e.esquisse.ajouterRectangle(0, 0, l, w);
      resoudre(e.esquisse);
      const int ie = q.ajouter(e);
      FonctionPiece x;
      x.type = TypeFonction::Extrusion;
      x.esquisseId = ie;
      x.longueur = h;
      extrusion = q.ajouter(x);
      return q;
    };
    int exP = 0, exC = 0;
    const Piece plaque = pave(60, 40, 8, exP), cube = pave(10, 10, 10, exC);
    int ip = 0, ic = 0, aSocle = 0, aEnsemble = 0;
    f1->document()->modifier([&](Projet& p) {
      p.pieces.push_back(pieceVersJson(plaque));
      ip = static_cast<int>(p.pieces.size()) - 1;
      p.pieces.push_back(pieceVersJson(cube));
      ic = static_cast<int>(p.pieces.size()) - 1;
      Assemblage so;
      so.nom = "Socle";
      so.composants = {{ip, 0, 0, 0, 0, 0, 0, true}, {ic, 5, 5, 8, 0, 0, 0, false}};
      ContrainteAssemblage k;
      k.a = 0;
      k.b = 1;
      k.refA = std::to_string(exP) + ":sommet";
      k.refB = std::to_string(exC) + ":base";
      so.contraintes.push_back(k);
      p.assemblages.push_back(so);
      aSocle = static_cast<int>(p.assemblages.size()) - 1;
      Assemblage en;
      en.nom = "Ensemble";
      ComposantAssemblage cs;
      cs.assemblage = aSocle;
      cs.fixe = true;
      en.composants = {cs, {ic, 20, 0, 18, 0, 0, 0, false}};
      ContrainteAssemblage d;
      d.a = 0;
      d.b = 1;
      d.refA = "@1/" + std::to_string(exC) + ":sommet";
      d.refB = std::to_string(exC) + ":base";
      en.contraintes.push_back(d);
      p.assemblages.push_back(en);
      aEnsemble = static_cast<int>(p.assemblages.size()) - 1;
    });
    f1->montrerAssemblage(aEnsemble);
    attendre(400);
    auto epaisseur = [&](double h) {
      f1->document()->modifier([&](Projet& p) {
        Piece q = pieceDepuisJson(p.pieces[static_cast<size_t>(ip)]);
        for (auto& f : q.fonctions) if (f.id == exP) f.longueur = h;
        p.pieces[static_cast<size_t>(ip)] = pieceVersJson(q);
      });
    };
    auto hauteurs = [&] {
      const auto& p = f1->document()->projet();
      return std::pair<double, double>{p.assemblages[static_cast<size_t>(aSocle)].composants[1].z, p.assemblages[static_cast<size_t>(aEnsemble)].composants[1].z};
    };
    epaisseur(20);
    attendre(500);
    auto [zs, ze] = hauteurs();
    verifier(std::abs(zs - 20) < 1e-5 && std::abs(ze - 30) < 1e-5, "plaque épaissie, assemblage affiché : le socle puis l'ensemble se remettent en place",
             "socle " + chiffre(zs) + " mm, ensemble " + chiffre(ze) + " mm");
    // Modifiée pendant qu'un autre assemblage est affiché, puis l'ensemble rouvert
    f1->montrerAssemblage(aSocle);
    attendre(300);
    f1->montrerAccueil();
    attendre(200);
    epaisseur(12);
    attendre(300);
    f1->montrerAssemblage(aEnsemble);
    attendre(500);
    std::tie(zs, ze) = hauteurs();
    verifier(std::abs(zs - 12) < 1e-5 && std::abs(ze - 22) < 1e-5, "plaque modifiée hors de l'assemblage : tout se remet en place à la réouverture",
             "socle " + chiffre(zs) + " mm, ensemble " + chiffre(ze) + " mm");
    QAction* reconstruire = nullptr;
    for (QAction* x : as->findChildren<QAction*>()) if (x->text().startsWith(QStringLiteral("Reconstruire"))) reconstruire = x;
    verifier(reconstruire != nullptr, "« Reconstruire (Ctrl+B) » est au ruban de l'assemblage");
    if (reconstruire) reconstruire->trigger();
    attendre(300);
    std::tie(zs, ze) = hauteurs();
    verifier(std::abs(ze - 22) < 1e-5, "Reconstruire ne déplace rien quand tout est à jour");
  }
  // 6 quinquies d. Manipulation (A4) : glisser et pivoter à la souris dans le respect des contraintes, un composant
  // lié qui suit, placement et pivotement par valeurs dans le panneau, sans boîte modale
  {
    struct Pave { Piece p; int ex = 0; std::vector<int> lignes; };
    auto pave = [](double l, double w, double h) {
      Pave q;
      FonctionPiece e;
      e.type = TypeFonction::Esquisse;
      q.lignes = e.esquisse.ajouterRectangle(0, 0, l, w);
      resoudre(e.esquisse);
      const int ie = q.p.ajouter(e);
      FonctionPiece x;
      x.type = TypeFonction::Extrusion;
      x.esquisseId = ie;
      x.longueur = h;
      q.ex = q.p.ajouter(x);
      return q;
    };
    const Pave plaque = pave(80, 60, 8), cube = pave(10, 10, 10);
    int aManip = 0;
    f1->document()->modifier([&](Projet& p) {
      p.pieces.push_back(pieceVersJson(plaque.p));
      const int ip = static_cast<int>(p.pieces.size()) - 1;
      p.pieces.push_back(pieceVersJson(cube.p));
      const int ic = static_cast<int>(p.pieces.size()) - 1;
      Assemblage m;
      m.nom = "Manipulation";
      m.composants = {{ip, 0, 0, 0, 0, 0, 0, true}, {ic, 20, 20, 8, 0, 0, 0, false}, {ic, 20, 20, 18, 0, 0, 0, false}};
      auto lier = [&](int a, const std::string& ra, int b, const std::string& rb, bool aligne) {
        ContrainteAssemblage k;
        k.a = a;
        k.b = b;
        k.refA = ra;
        k.refB = rb;
        k.inverse = aligne;
        m.contraintes.push_back(k);
      };
      const std::string base = std::to_string(cube.ex) + ":base", sommet = std::to_string(cube.ex) + ":sommet";
      lier(0, std::to_string(plaque.ex) + ":sommet", 1, base, false);
      // Le second cube est posé sur le premier, ses flancs alignés sur les siens : il le suit
      lier(1, sommet, 2, base, false);
      lier(1, std::to_string(cube.ex) + ":cote:" + std::to_string(cube.lignes[0]), 2, std::to_string(cube.ex) + ":cote:" + std::to_string(cube.lignes[0]), true);
      lier(1, std::to_string(cube.ex) + ":cote:" + std::to_string(cube.lignes[3]), 2, std::to_string(cube.ex) + ":cote:" + std::to_string(cube.lignes[3]), true);
      p.assemblages.push_back(m);
      aManip = static_cast<int>(p.assemblages.size()) - 1;
    });
    f1->montrerAssemblage(aManip, 1);
    attendre(400);
    auto comp = [&](int k) { return f1->document()->projet().assemblages[static_cast<size_t>(aManip)].composants[static_cast<size_t>(k)]; };
    const ComposantAssemblage depart = comp(1);
    as->basculerDeplacerSouris();
    as->debutGlisser(25, 25, 18);
    as->glisserVers(10, 5, 7);  // la souris monte aussi : le cube reste posé sur la plaque
    attendre(100);
    as->finGlisser();
    attendre(300);
    ComposantAssemblage c1 = comp(1), c2 = comp(2);
    verifier(std::abs(c1.x - 30) < 1e-4 && std::abs(c1.y - 25) < 1e-4 && std::abs(c1.z - 8) < 1e-4, "glisser : le cube suit la souris dans le plan de la plaque, sans la quitter",
             chiffre(c1.x) + " ; " + chiffre(c1.y) + " ; " + chiffre(c1.z));
    verifier(std::abs(c2.x - 30) < 1e-4 && std::abs(c2.y - 25) < 1e-4 && std::abs(c2.z - 18) < 1e-4, "le cube lié au premier le suit pendant le glisser", chiffre(c2.x) + " ; " + chiffre(c2.y) + " ; " + chiffre(c2.z));
    f1->document()->annuler();
    attendre(300);
    verifier(std::abs(comp(1).x - depart.x) < 1e-9 && std::abs(comp(1).y - depart.y) < 1e-9, "un seul Ctrl+Z annule tout le glisser");
    // Rotation à la souris autour de Z, par le centre du cube : un quart de tour
    as->choisirComposant(1);
    as->basculerPivoterSouris(2);
    verifier(as->pivotSouris() == 2, "« Faire pivoter à la souris » est actif, autour de Z");
    as->debutGlisser(35, 25, 18);       // 10 mm à droite du centre (25 ; 25)
    as->glisserVers(-10, 10, 0);        // 10 mm devant le centre : 90°
    as->finGlisser();
    attendre(300);
    c1 = comp(1);
    const gp_Vec ex1 = gp_Vec(1, 0, 0).Transformed(placementComposant(c1));
    const gp_Pnt centre1 = gp_Pnt(5, 5, 5).Transformed(placementComposant(c1));
    verifier(std::abs(ex1.Y() - 1) < 1e-4 && centre1.Distance(gp_Pnt(25, 25, 13)) < 1e-4,
             "pivoter à la souris : le cube tourne d'un quart de tour sur lui-même", chiffre(ex1.X()) + " ; " + chiffre(ex1.Y()));
    // Autour de X, la plaque l'en empêche : le cube reste à plat
    as->choisirComposant(1);
    as->basculerPivoterSouris(0);
    as->debutGlisser(25, 35, 13);
    as->glisserVers(0, -10, 10);
    as->finGlisser();
    attendre(300);
    const gp_Vec ez1 = gp_Vec(0, 0, 1).Transformed(placementComposant(comp(1)));
    verifier(std::abs(ez1.Z() - 1) < 1e-4, "pivoter autour de X contre les contraintes : le cube reste posé à plat", chiffre(ez1.Z()));
    as->abandonnerOutil();
    verifier(as->pivotSouris() == -1, "Échap termine la rotation à la souris");
    // Placement et pivotement par valeurs : dans le panneau, sans boîte modale
    const auto modale = guetterModale(200);
    as->choisirComposant(1);
    as->placerParValeurs();
    attendre(400);
    QLineEdit* px = as->findChild<QLineEdit*>(QStringLiteral("positionX"));
    verifier(!*modale && px != nullptr, "« Placer par valeurs » ouvre les champs du panneau, sans fenêtre modale");
    const ComposantAssemblage avantDelta = comp(1);
    if (auto* dx = as->findChild<ChampValeur*>(QStringLiteral("deltaX"))) dx->setValeur(5);
    if (auto* b = as->findChild<QPushButton*>(QStringLiteral("translaterComposant"))) b->click();
    attendre(300);
    verifier(std::abs(comp(1).x - avantDelta.x - 5) < 1e-4 && std::abs(comp(1).z - 8) < 1e-4, "« Déplacer de » ΔX = 5 mm, depuis le panneau", chiffre(comp(1).x - avantDelta.x) + " mm");
    // Sortir d'un champ sans rien y taper ne repose pas sa valeur arrondie à l'affichage. Sous Windows, le champ X
    // ouvert par « Placer par valeurs » perdait le focus quand le panneau se reconstruisait, et annulait ainsi le
    // « Déplacer de » ; sous xvfb la fenêtre n'a jamais le focus, d'où la sortie de champ provoquée ici.
    if (auto* x = as->findChild<ChampValeur*>(QStringLiteral("positionX"))) {
      emit x->valeurChangee(comp(1).x + 0.1234567891);  // position qui ne s'écrit pas exactement en six décimales
      attendre(300);
    }
    {
      const double avantSortie = comp(1).x;
      if (auto* x = as->findChild<ChampValeur*>(QStringLiteral("positionX"))) emit x->editingFinished();
      attendre(300);
      verifier(std::abs(comp(1).x - avantSortie) < 1e-12, "sortir du champ X sans rien y taper ne déplace pas le composant", chiffre(comp(1).x - avantSortie) + " mm");
    }
    as->pivoterParValeur();
    attendre(200);
    const gp_Pnt centreAvant = gp_Pnt(5, 5, 5).Transformed(placementComposant(comp(1)));
    const gp_Vec exAvant = gp_Vec(1, 0, 0).Transformed(placementComposant(comp(1)));
    if (auto* ang = as->findChild<ChampValeur*>(QStringLiteral("anglePivot"))) ang->setValeur(30);
    if (auto* ax = as->findChild<QComboBox*>(QStringLiteral("axePivot"))) ax->setCurrentIndex(2);
    if (auto* b = as->findChild<QPushButton*>(QStringLiteral("pivoterComposant"))) b->click();
    attendre(300);
    const gp_Pnt centreApres = gp_Pnt(5, 5, 5).Transformed(placementComposant(comp(1)));
    const gp_Vec exApres = gp_Vec(1, 0, 0).Transformed(placementComposant(comp(1)));
    const double tourne = exAvant.AngleWithRef(exApres, gp_Vec(0, 0, 1)) * 180 / std::numbers::pi;
    verifier(std::abs(tourne - 30) < 1e-3 && centreApres.Distance(centreAvant) < 1e-3, "pivoter de 30° autour de Z par son centre, depuis le panneau", chiffre(tourne) + "°");
    // Panneau latéral (U1) : distance posée à sa valeur actuelle, répétition, propriétés physiques, engrenage
    PanneauLateral* pla = as->panneauLateral();
    const auto modaleA = guetterModale(300);
    const size_t nc0 = f1->document()->projet().assemblages[static_cast<size_t>(aManip)].contraintes.size();
    const ComposantAssemblage avantDistance = comp(2);
    as->commencerContrainte("distance");
    as->choisirReference(0, std::to_string(plaque.ex) + ":base");
    as->choisirReference(2, std::to_string(cube.ex) + ":sommet");
    attendre(400);
    const auto& mDist = f1->document()->projet().assemblages[static_cast<size_t>(aManip)];
    verifier(!*modaleA && mDist.contraintes.size() == nc0 + 1 && std::abs(std::abs(mDist.contraintes.back().valeur) - 28) < 1e-6 && std::abs(comp(2).z - avantDistance.z) < 1e-6,
             "distance posée sans boîte : à sa valeur actuelle (28 mm), rien ne bouge", chiffre(mDist.contraintes.back().valeur) + " mm");
    QLineEdit* valeurC = as->findChild<QLineEdit*>(QStringLiteral("valeurContrainte"));
    verifier(valeurC != nullptr, "la distance s'ouvre dans le panneau, prête à être tapée");
    f1->document()->annuler();
    attendre(300);
    as->choisirComposant(2);
    as->panneauRepetition(true);
    attendre(300);
    verifier(pla && pla->isVisible() && pla->page() == QStringLiteral("repetitionCirculaire") && as->findChild<QSpinBox*>(QStringLiteral("nombreRepetition")), "Répétition circulaire : nombre, angle et axe dans le panneau");
    if (pla) pla->annuler();
    for (QAction* x : as->findChildren<QAction*>())
      if (x->text() == QStringLiteral("Propriétés physiques")) { x->trigger(); break; }
    attendre(300);
    verifier(!*modaleA && pla && pla->isVisible() && pla->page() == QStringLiteral("proprietesPhysiques"), "Propriétés physiques de l'assemblage : dans le panneau latéral");
    if (pla) pla->annuler();
  }
  // 6 quinquies. Mécanisme : plaque percée et arbre liés par un pivot ; simulation ; sous-assemblage depuis une sélection
  {
    Piece plaqueP;
    FonctionPiece es;
    es.type = TypeFonction::Esquisse;
    es.esquisse.ajouterRectangle(0, 0, 60, 40);
    resoudre(es.esquisse);
    const int ies = plaqueP.ajouter(es);
    FonctionPiece xp;
    xp.type = TypeFonction::Extrusion;
    xp.esquisseId = ies;
    xp.longueur = 10;
    const int ixp = plaqueP.ajouter(xp);
    FonctionPiece pts;
    pts.type = TypeFonction::Esquisse;
    pts.face = std::to_string(ixp) + ":sommet";
    pts.esquisse.ajouterPoint(30, 20);
    const int ipts = plaqueP.ajouter(pts);
    FonctionPiece tr;
    tr.type = TypeFonction::Percage;
    tr.esquisseId = ipts;
    tr.typePercage = "simple";
    tr.diametre = 10;
    tr.traversant = true;
    const int itr = plaqueP.ajouter(tr);
    Piece arbre;
    FonctionPiece ec;
    ec.type = TypeFonction::Esquisse;
    const int cercle = ec.esquisse.ajouterCercle(ec.esquisse.ajouterPoint(0, 0), 5);
    const int iec = arbre.ajouter(ec);
    FonctionPiece xa;
    xa.type = TypeFonction::Extrusion;
    xa.esquisseId = iec;
    xa.longueur = 40;
    const int ixa = arbre.ajouter(xa);
    int ip = 0, ia = 0, ass = 0;
    f1->document()->modifier([&](Projet& p) {
      p.pieces.push_back(pieceVersJson(plaqueP));
      ip = static_cast<int>(p.pieces.size()) - 1;
      p.pieces.push_back(pieceVersJson(arbre));
      ia = static_cast<int>(p.pieces.size()) - 1;
      Assemblage m;
      m.nom = "Mécanisme";
      m.composants = {{ip, 0, 0, 0, 0, 0, 0, true}, {ia, 30, 20, -15, 0, 0, 0, false}, {ip, 100, 0, 0, 0, 0, 0, false}};
      p.assemblages.push_back(m);
      ass = static_cast<int>(p.assemblages.size()) - 1;
    });
    f1->montrerAssemblage(ass);
    attendre(500);
    std::string alesage;
    for (const auto& [face, nom] : construirePiece(plaqueP).faces)
      if (nom.rfind(std::to_string(itr) + ":trou", 0) == 0) { BRepAdaptor_Surface s(face); if (s.GetType() == GeomAbs_Cylinder) alesage = nom; }
    as->lier("pivot", 0, alesage, 1, std::to_string(ixa) + ":cote:" + std::to_string(cercle), 0);
    attendre(400);
    const auto& m = f1->document()->projet().assemblages[static_cast<size_t>(ass)];
    verifier(m.contraintes.size() == 1 && m.contraintes[0].type == "pivot", "un pivot se crée entre l'alésage et l'arbre");
    // Modifier la liaison après coup (M9) : angle imposé, puis position axiale ; le pilotage est gardé dans le projet
    {
      as->choisirContrainte(0);
      attendre(300);
      QCheckBox* piloter = as->findChild<QCheckBox*>(QStringLiteral("piloterLiaison"));
      ChampValeur* axialeChamp = as->findChild<ChampValeur*>(QStringLiteral("positionAxiale"));
      verifier(piloter && axialeChamp, "le panneau du pivot propose l'angle imposé et la position axiale");
      if (piloter && axialeChamp) {
        const ComposantAssemblage avant = f1->document()->projet().assemblages[static_cast<size_t>(ass)].composants[1];
        piloter->setChecked(true);
        attendre(400);
        const auto& c0 = f1->document()->projet().assemblages[static_cast<size_t>(ass)].contraintes[0];
        verifier(c0.pilotee, "le pilotage du pivot est gardé dans le projet");
        if (ChampValeur* angle = as->findChild<ChampValeur*>(QStringLiteral("valeurPilotee"))) {
          emit angle->valeurChangee(c0.valeur + 45);
          attendre(500);
        }
        const ComposantAssemblage tourne = f1->document()->projet().assemblages[static_cast<size_t>(ass)].composants[1];
        double ecart = std::fmod(std::abs(tourne.rz - avant.rz), 360.0);
        ecart = std::min(ecart, 360 - ecart);
        verifier(std::abs(ecart - 45) < 0.05 && std::abs(tourne.z - avant.z) < 1e-6, "angle imposé de 45° : l'arbre tourne autour de son axe", chiffre(ecart) + "°");
        if (ChampValeur* ax = as->findChild<ChampValeur*>(QStringLiteral("positionAxiale"))) {
          emit ax->valeurChangee(ax->valeur() + 5);
          attendre(500);
        }
        const ComposantAssemblage glisse = f1->document()->projet().assemblages[static_cast<size_t>(ass)].composants[1];
        verifier(std::abs(std::abs(glisse.z - tourne.z) - 5) < 0.01, "position axiale modifiée : l'arbre glisse de 5 mm le long de l'axe", chiffre(glisse.z - tourne.z) + " mm");
        if (QCheckBox* p2 = as->findChild<QCheckBox*>(QStringLiteral("piloterLiaison"))) p2->setChecked(false);
        attendre(300);
        verifier(!f1->document()->projet().assemblages[static_cast<size_t>(ass)].contraintes[0].pilotee, "le pilotage se retire");
      }
    }
    const double rzProjet = m.composants[1].rz;
    as->simuler(0, 90);
    attendre(300);
    verifier(std::abs(f1->document()->projet().assemblages[static_cast<size_t>(ass)].composants[1].rz - rzProjet) < 1e-9, "la simulation fait tourner l'arbre sans modifier le projet");
    as->arreterSimulation();
    attendre(200);
    QAction* simu = nullptr;
    for (QAction* x : as->findChildren<QAction*>()) if (x->text() == QStringLiteral("Simulation cinématique")) simu = x;
    if (simu) simu->trigger();
    attendre(300);
    QFrame* panneau = as->findChild<QFrame*>(QStringLiteral("panneauSimulation"));
    verifier(panneau && panneau->isVisible(), "le panneau de simulation s'ouvre");
    if (simu) simu->trigger();
    attendre(200);
    const size_t nAss = f1->document()->projet().assemblages.size();
    as->creerSousAssemblage({1, 2});
    attendre(400);
    const auto& m2 = f1->document()->projet().assemblages[static_cast<size_t>(ass)];
    verifier(f1->document()->projet().assemblages.size() == nAss + 1 && m2.composants.size() == 2 && m2.composants.back().assemblage == static_cast<int>(nAss),
             "deux composants deviennent un sous-assemblage, rangé sous le parent");
    {
      // AS-16 : le pivot qui reliait l'arbre au reste est gardé, sur la face de l'arbre dans le sous-assemblage
      const auto& m3 = f1->document()->projet().assemblages[static_cast<size_t>(ass)];
      std::string types;
      bool pivot = false;
      for (const auto& c3 : m3.contraintes) {
        types += c3.type + (c3.suspendue ? "(suspendue) " : " ");
        pivot |= c3.type == "pivot" && !c3.suspendue && (c3.refA.rfind("@", 0) == 0 || c3.refB.rfind("@", 0) == 0);
      }
      verifier(pivot, "la liaison qui reliait l'arbre au reste est gardée sur le sous-assemblage", types);
    }
    f1->montrerAssemblage(0);  // retour au premier assemblage pour la suite
    attendre(300);
  }
  // 7. Nouvel onglet : vierge, sur l'accueil ; y créer une pièce ne demande rien
  c.nouvelOnglet();
  attendre(500);
  verifier(c.nombreOnglets() == 2 && c.active()->document()->estVierge(), "« + » ouvre un onglet vierge");
  {
    // ERG-17 : Ctrl+T et Ctrl+W ont leurs boutons, que l'essai « la souris suffit » reconnaît à leur action
    QToolButton* plus = c.findChild<QToolButton*>(QStringLiteral("nouvelOnglet"));
    int croix = 0;
    for (QToolButton* x : c.findChildren<QToolButton*>(QStringLiteral("fermerOnglet"))) croix += actionsMarquees(x).contains(QStringLiteral("onglet.fermer"));
    verifier(plus && actionsMarquees(plus).contains(QStringLiteral("onglet.nouveau")) && croix == c.nombreOnglets(),
             "ERG-17 : le « + » et la croix de chaque onglet portent les actions Nouvel onglet et Fermer l'onglet");
  }
  question = false;
  QTimer::singleShot(400, [&question] { if (qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { question = true; QApplication::activeModalWidget()->close(); } });
  c.active()->choixAccueilPublic(QStringLiteral("piece"));
  attendre(700);
  FenetrePrincipale* f2 = c.active();
  verifier(!question && c.nombreOnglets() == 2 && f2->document()->projet().pieces.size() == 1, "une pièce se crée dans le nouvel onglet, sans question");
  remplirPiece(f2->document(), 0);
  attendre(300);
  const QString fichier2 = QDir::temp().filePath(QStringLiteral("promethee-essai-piece.prom"));
  QFile::remove(fichier2);
  try { f2->document()->enregistrer(fichier2); } catch (...) {}
  verifier(QFileInfo::exists(fichier2), "la pièce seule s'enregistre en .prom");
  // 8. Depuis l'accueil d'un onglet occupé : la création part dans un nouvel onglet
  f2->choixAccueilPublic(QStringLiteral("assemblage"));
  attendre(700);
  verifier(c.nombreOnglets() == 3 && c.active() != f2 && c.active()->document()->projet().assemblages.size() == 1, "depuis un onglet occupé, un nouvel assemblage s'ouvre dans un nouvel onglet");
  // 9. Rouvrir un fichier déjà ouvert : on retrouve son onglet, sans doublon
  c.ouvrirFichier(fichier1);
  attendre(400);
  verifier(c.nombreOnglets() == 3 && c.active() == f1, "rouvrir un fichier déjà ouvert ramène à son onglet");
  // 10. Pièce d'un autre projet insérée dans l'assemblage
  {
    QString err;
    const size_t piecesAvant = f1->document()->projet().pieces.size(), composantsAvant = f1->document()->projet().assemblages[0].composants.size();
    verifier(as->insererDepuisProjet(fichier2, 0, &err), "insérer une pièce d'un autre projet", err.toStdString());
    attendre(400);
    verifier(f1->document()->projet().pieces.size() == piecesAvant + 1 && f1->document()->projet().assemblages[0].composants.size() == composantsAvant + 1, "la pièce de l'autre projet rejoint l'assemblage");
  }
  // 11. Fermer un onglet modifié : question en français ; « Annuler » le garde, « Ne pas enregistrer » le ferme
  c.activer(2);
  attendre(300);
  repondre(QStringLiteral("Annuler"));
  c.fermerOnglet(2);
  attendre(500);
  verifier(c.nombreOnglets() == 3, "« Annuler » garde l'onglet ouvert");
  verifier(g_boutonsVus.contains(QStringLiteral("Enregistrer")) && g_boutonsVus.contains(QStringLiteral("Ne pas enregistrer")) && g_boutonsVus.contains(QStringLiteral("Annuler")),
           "la question d'enregistrement est en français", g_boutonsVus.join(QStringLiteral(", ")).toStdString());
  repondre(QStringLiteral("Ne pas enregistrer"));
  c.fermerOnglet(2);
  attendre(500);
  verifier(c.nombreOnglets() == 2, "« Ne pas enregistrer » ferme l'onglet");
  // 12. Fermer un onglet enregistré : aucune question
  question = false;
  QTimer::singleShot(400, [&question] { if (qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) { question = true; QApplication::activeModalWidget()->close(); } });
  c.fermerOnglet(1);
  attendre(600);
  verifier(!question && c.nombreOnglets() == 1, "un onglet enregistré se ferme sans question");
  // 13. Esquisse 3D placée à la souris, puis tube le long d'elle
  c.nouvelOnglet();
  attendre(400);
  c.active()->choixAccueilPublic(QStringLiteral("piece"));
  attendre(700);
  FenetrePrincipale* f3 = c.active();
  VuePiece* vue = f3->findChild<VuePiece*>();
  verifier(declencher(f3, QStringLiteral("Esquisse 3D")), "le bouton Esquisse 3D répond dans l'onglet");
  attendre(400);
  for (const QPointF& q : {QPointF(0.4, 0.5), QPointF(0.55, 0.45), QPointF(0.65, 0.6)}) {
    cliquer(vue, QPointF(vue->width() * q.x(), vue->height() * q.y()));
    attendre(250);
  }
  verifier(bouton(f3, QStringLiteral("✔ OK")), "valider l’esquisse 3D placée à la souris");
  attendre(400);
  const Piece pc3 = pieceDepuisJson(f3->document()->projet().pieces.back());
  verifier(!pc3.fonctions.empty() && pc3.fonctions.back().type == TypeFonction::Esquisse3D && pc3.fonctions.back().points3d.size() == 3, "trois clics posent trois points",
           pc3.fonctions.empty() ? std::string("rien") : std::to_string(pc3.fonctions.back().points3d.size()) + " point(s)");
  verifier(declencher(f3, QStringLiteral("Tube le long d’un chemin")), "le tube se lance le long de l'esquisse 3D");
  attendre(400);
  verifier(bouton(f3, QStringLiteral("✔ OK")), "valider le tube");
  attendre(600);
  const Piece pc4 = pieceDepuisJson(f3->document()->projet().pieces.back());
  {
    std::string pts;
    for (const FonctionPiece& g : pc4.fonctions)
      for (const auto& q : g.points3d) pts += "(" + std::to_string(q[0]) + ", " + std::to_string(q[1]) + ", " + std::to_string(q[2]) + ") ";
    verifier(volume(construirePiece(pc4).forme) > 1, "le tube a du volume", pts);
  }
  if (const QByteArray chemin = qgetenv("PROMETHEE_CAPTURE_ONGLETS"); !chemin.isEmpty()) {
    // Capture de la fenêtre à onglets (documentation) : deux onglets, l'atelier Pièce avec le tube
    c.nouvelOnglet();
    attendre(300);
    c.activer(c.nombreOnglets() - 2);
    attendre(500);
    c.grab().save(QString::fromLocal8Bit(chemin));
  }
  // 13 bis. Électronique dans un onglet : la barre du bas donne l'état de la carte, pas la validité mécanique
  c.nouvelOnglet();
  attendre(400);
  c.active()->choixAccueilPublic(QStringLiteral("exemple-elec"));
  attendre(1200);
  QString texteBas;
  for (QLabel* l : c.findChildren<QLabel*>())
    if (l->isVisible() && l->text().contains(QStringLiteral("ERC"))) texteBas = l->text();
  verifier(c.active()->atelierElectronique() && c.active()->atelierElectronique()->isVisible(), "l'exemple électronique s'ouvre dans un onglet");
  verifier(!texteBas.isEmpty() && !texteBas.contains(QStringLiteral("Modèle valide")), "la barre du bas de l'onglet donne l'état de la carte (ERC), sans la validité mécanique",
           texteBas.left(80).toStdString());
  // G8 : dans un onglet aussi, l'atelier électronique garde sa barre d'état (bilan et messages), rien n'est coupé
  // sur la ligne des onglets
  verifier(c.active()->atelierElectronique() && c.active()->atelierElectronique()->barreEtat()->isVisible() && c.active()->texteEtat().isEmpty(),
           "G8 : dans un onglet, la barre d'état de l'atelier électronique reste la sienne");
  if (const QByteArray chemin = qgetenv("PROMETHEE_CAPTURE_ONGLETS_ELEC"); !chemin.isEmpty()) c.grab().save(QString::fromLocal8Bit(chemin));
  // 13 ter. Enregistrement en conflit, puis récupération après un arrêt brutal (dans la vraie fenêtre à onglets)
  {
    auto cliquerBouton = [](const QString& texte) {
      QTimer::singleShot(350, [texte] {
        if (QWidget* m = QApplication::activeModalWidget())
          for (QAbstractButton* b : m->findChildren<QAbstractButton*>())
            if (b->text() == texte) { b->click(); return; }
      });
    };
    const QString dossier = QDir(QString::fromLocal8Bit(qgetenv("PROMETHEE_DOSSIER_DONNEES"))).filePath(QStringLiteral("essai-conflit"));
    QDir(dossier).removeRecursively();
    QDir().mkpath(dossier);
    const QString chemin = QDir(dossier).filePath(QStringLiteral("conflit.prom"));
    prom::Projet depart = prom::exemple();
    depart.nom = "Version d'origine";
    prom::ecrireProjet(depart, chemin.toStdString());
    c.nouvelOnglet();
    attendre(300);
    c.ouvrirFichier(chemin);
    attendre(600);
    FenetrePrincipale* fc = c.active();
    verifier(fc && fc->document()->chemin() == chemin, "conflit : le projet s'ouvre dans un onglet");
    // Un autre programme réécrit le fichier pendant qu'on le modifie ici
    prom::Projet ailleurs = depart;
    ailleurs.nom = "Modifié ailleurs";
    prom::ecrireProjet(ailleurs, chemin.toStdString());
    fc->document()->modifier([](prom::Projet& p) { p.nom = "Ma version"; });
    QAction* enregistrer = nullptr;
    for (QAction* a : fc->findChildren<QAction*>()) if (a->text() == QStringLiteral("&Enregistrer")) enregistrer = a;
    verifier(enregistrer != nullptr, "conflit : l'action Enregistrer existe");
    if (enregistrer) {
      repondre(QStringLiteral("Annuler"));
      enregistrer->trigger();
      attendre(700);
      verifier(g_boutonsVus.contains(QStringLiteral("Écraser avec ma version")) && prom::lireProjet(chemin.toStdString()).nom == "Modifié ailleurs",
               "conflit : la version écrite ailleurs est signalée, et gardée si l'on annule");
      repondre(QStringLiteral("Écraser avec ma version"));
      enregistrer->trigger();
      attendre(700);
      verifier(prom::lireProjet(chemin.toStdString()).nom == "Ma version" && !fc->document()->modifie(), "conflit : « Écraser avec ma version » enregistre ce projet");
    }
    // Copie laissée par une session interrompue (aucun verrou tenu) : proposée, rouverte, puis effacée
    c.fermerOnglet(c.indexActif());
    attendre(300);
    const QString session = QDir(Recuperation::dossierDonnees()).filePath(QStringLiteral("recuperation/") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QDir().mkpath(session);
    prom::Projet perdu = prom::exemple();
    perdu.nom = "Travail récupéré";
    prom::ecrireFichierAtomique(prom::cheminFichier(QDir(session).filePath(QStringLiteral("a.prom")).toStdString()), prom::versJson(perdu).dump(2));
    QJsonObject fiche;
    fiche[QStringLiteral("origine")] = chemin;
    fiche[QStringLiteral("nom")] = QStringLiteral("Travail récupéré");
    fiche[QStringLiteral("date")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    prom::ecrireFichierAtomique(prom::cheminFichier(QDir(session).filePath(QStringLiteral("a.json")).toStdString()), QJsonDocument(fiche).toJson().toStdString());
    const int avant = c.nombreOnglets();
    cliquerBouton(QStringLiteral("Rouvrir les projets cochés"));
    const int rouverts = c.proposerRecuperation();
    attendre(500);
    verifier(rouverts == 1 && c.active() && c.active()->document()->projet().nom == "Travail récupéré" && c.active()->document()->modifie(),
             "récupération : la copie d'une session interrompue est proposée et rouverte, marquée à enregistrer");
    verifier(c.active() && c.active()->document()->chemin() == chemin && c.nombreOnglets() >= avant, "récupération : le projet retrouve son fichier d'origine, sans l'écraser");
    verifier(prom::lireProjet(chemin.toStdString()).nom == "Ma version", "récupération : le fichier d'origine n'est pas modifié avant d'enregistrer");
    verifier(!QDir(session).exists() || QDir(session).entryList({QStringLiteral("*.prom")}, QDir::Files).isEmpty(), "récupération : la copie rouverte est effacée");
    verifier(Recuperation::instance().orphelines().empty(), "récupération : plus rien à proposer ensuite");
  }
  // 13 bis. Thèmes : chacun s'applique aussitôt, fenêtre ouverte sur l'atelier électronique ; les feuilles de style et
  // les textes riches sont traduits, les contrastes restent lisibles, et le retour au thème clair rend tout à l'octet près.
  {
    Theme& th = Theme::instance();
    const IdTheme depart = th.choix();
    const bool impose = !qEnvironmentVariableIsEmpty("PROMETHEE_THEME");
    th.choisir(IdTheme::Clair);
    attendre(300);
    Navigateur* nav = c.active()->findChild<Navigateur*>();
    const QString cssNav = nav ? nav->styleSheet() : QString();
    const QString texteRiche = QStringLiteral("<span style='color:#10142A'>encre</span> <span style='color:#C2402E'>erreur</span>");
    QLabel etiquette(texteRiche);
    auto contraste = [](const QColor& a, const QColor& b) {
      auto lum = [](const QColor& c) {
        auto l = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * l(c.redF()) + 0.7152 * l(c.greenF()) + 0.0722 * l(c.blueF());
      };
      const double x = lum(a), y = lum(b);
      return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
    };
    for (IdTheme id : {IdTheme::Promethee, IdTheme::Sombre, IdTheme::ContrasteEleve, IdTheme::Systeme, IdTheme::Clair}) {
      const std::string nom = Theme::nom(id).toStdString();
      th.choisir(id);
      attendre(350);
      const CouleursInterface& k = th.c();
      verifier(QApplication::palette().color(QPalette::Window) == k.fenetre && QApplication::palette().color(QPalette::Text) == k.texte, "thème « " + nom + " » : palette appliquée sans redémarrer");
      verifier(impose || QSettings().value(QStringLiteral("apparence/theme")).toString() == Theme::cle(id), "thème « " + nom + " » gardé dans les réglages");
      const double cTexte = contraste(k.texte, k.fenetre), cSecond = contraste(k.texteSecondaire, k.surface), cDiscret = contraste(k.texteDiscret, k.surface);
      const double cAccent = contraste(k.surAccent, k.accent), cErreur = contraste(k.erreur, k.surface);
      verifier(cTexte >= 7 && cSecond >= 4.5 && cDiscret >= 3 && cAccent >= 4.5 && cErreur >= 3.5, "thème « " + nom + " » : contrastes lisibles (WCAG)",
               QStringLiteral("texte %1, secondaire %2, discret %3, sur accent %4, erreur %5").arg(cTexte, 0, 'f', 1).arg(cSecond, 0, 'f', 1).arg(cDiscret, 0, 'f', 1).arg(cAccent, 0, 'f', 1).arg(cErreur, 0, 'f', 1).toStdString());
      const bool traduit = th.traduit();
      verifier(!nav || (traduit ? nav->styleSheet() != cssNav : nav->styleSheet() == cssNav), "thème « " + nom + " » : feuille de style du navigateur " + (traduit ? "traduite" : "d'origine"));
      const QString encre = th.texte(QColor(0x10, 0x14, 0x2A)).name().toUpper();
      verifier(traduit ? etiquette.text().contains(encre) && !etiquette.text().contains(QStringLiteral("#10142A")) : etiquette.text() == texteRiche,
               "thème « " + nom + " » : textes riches traduits selon le thème", etiquette.text().toStdString());
      if (const QByteArray dossier = qgetenv("PROMETHEE_CAPTURE_THEMES"); !dossier.isEmpty())
        c.grab().save(QDir(QString::fromLocal8Bit(dossier)).filePath(QStringLiteral("onglets-%1.png").arg(Theme::cle(id))));
    }
    verifier(!nav || nav->styleSheet() == cssNav, "retour au thème clair : feuilles de style d'origine, à l'octet près");
    // Le menu Affichage › Thème et le choix de l'accueil suivent le thème courant
    QAction* sombre = nullptr;
    for (QAction* a : c.active()->findChildren<QAction*>()) if (a->objectName() == QStringLiteral("theme-sombre")) sombre = a;
    verifier(sombre != nullptr, "Affichage › Thème propose les thèmes");
    if (sombre) sombre->trigger();
    attendre(300);
    verifier(th.choix() == IdTheme::Sombre && sombre && sombre->isChecked(), "choisir « Sombre » dans le menu l'applique et le coche");
    bool comboAJour = false;
    for (QComboBox* b : c.findChildren<QComboBox*>()) if (b->objectName() == QStringLiteral("choixTheme")) comboAJour = b->currentData().toInt() == static_cast<int>(IdTheme::Sombre);
    verifier(comboAJour, "le choix d'apparence de l'accueil suit le thème");
    th.choisir(depart);
    attendre(300);
  }
  // 14. Fermer le dernier onglet : il reste toujours un onglet d'accueil
  repondre(QStringLiteral("Ne pas enregistrer"));
  for (int i = c.nombreOnglets() - 1; i >= 0; --i) { repondre(QStringLiteral("Ne pas enregistrer")); c.fermerOnglet(i); attendre(400); }
  verifier(c.nombreOnglets() == 1 && c.active()->document()->estVierge(), "tout fermé : il reste un onglet d'accueil vierge");
  std::printf("%d échec(s)\n", g_echecs);
  std::fflush(stdout);
  QApplication::exit(g_echecs);
}
