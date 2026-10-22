// Prométhée : essais d'interface du lot L2 de l'audit du 15 octobre 2026 (« Schéma interactif »), par de vrais gestes
// de souris et de clavier (EvenementsReels), sur une feuille d'essai ajoutée à l'exemple livré : glisser-déposer
// depuis la bibliothèque, édition sur place, fil tracé autour d'un symbole, symbole relié déplacé (2.1, Q2), puis
// un bloc par tâche du lot. Appelés à la fin de « Promethee --essais-electronique », ou seuls avec
// PROMETHEE_ESSAIS_LOTS=L2.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>

#include <QAbstractButton>
#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QSpinBox>
#include <QEventLoop>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QSet>
#include <QToolBar>
#include <QLabel>
#include <QTabWidget>
#include <QTableWidget>
#include <QPointer>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QSettings>
#include <QListWidget>
#include <QDialog>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QPainter>
#include <QMouseEvent>
#include <QImage>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QWheelEvent>

#include "AtelierElectronique.hpp"
#include "CadreVue.hpp"
#include "MemoiresSelection.hpp"
#include "PeripheriquesEntree.hpp"
#include "Vignettes.hpp"
#include "PanneauRecherche.hpp"
#include "ParcoursElectronique.hpp"
#include "BandeauOutil.hpp"
#include "DialogueComposant.hpp"
#include "DescriptionRuban.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "Navigateur.hpp"
#include "PanneauBibliotheque.hpp"
#include "PanneauVerifications.hpp"
#include "SelectionElectronique.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"
#include "RegistreActions.hpp"
#include "RegistreReglages.hpp"
#include "FenetreReglages.hpp"
#include "Theme.hpp"
#include "Icones.hpp"
#include "EncresCircuit.hpp"
#include "Theme.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
void repondre(QMessageBox::ButtonRole role) {
  QTimer::singleShot(300, [role] {
    if (auto* m = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
      for (QAbstractButton* b : m->buttons())
        if (m->buttonRole(b) == role) {
          b->click();
          return;
        }
  });
}
QPoint ecran(VueSchema* v, Pt p) { return v->versEcran(p).toPoint(); }
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
// La feuille d'essai du lot (la dernière de l'exemple)
const Feuille* feuilleEssai(Document* doc) {
  const ConceptionElec* c = conception(doc);
  return c && !c->feuilles.empty() ? &c->feuilles.back() : nullptr;
}
// Article de la bibliothèque du panneau, trouvé par son nom
std::pair<QTreeWidget*, QTreeWidgetItem*> article(AtelierElectronique* a, const QString& nom) {
  // L'arbre de la bibliothèque, pas un autre arbre qui citerait le même nom (inspecteur)
  if (QTreeWidget* t = a->findChild<QTreeWidget*>(QStringLiteral("arbreBibliotheque")); t && t->isVisible())
    for (QTreeWidgetItemIterator it(t); *it; ++it)
      if ((*it)->text(0) == nom && (*it)->data(0, Qt::UserRole).isValid()) return {t, *it};
  return {nullptr, nullptr};
}
// L'article amené dans la vue de son arbre ; son centre, en coordonnées de la zone d'affichage
QPoint amenerArticle(QTreeWidget* t, QTreeWidgetItem* it) {
  for (int i = 0; i < 4; ++i) {
    t->scrollToItem(it);
    attendre(40);
    if (t->viewport()->rect().contains(t->visualItemRect(it).center())) break;
  }
  return t->visualItemRect(it).center();
}
// Clic sur une ligne d'un menu ouvert : la ligne est choisie, le menu se referme
bool cliquerLigne(QMenu* m, QAction* x) {
  if (!m || !x || !m->isVisible()) return false;
  reel::cliquer(m, m->actionGeometry(x).center());
  attendre(60);
  return true;
}
// Le menu contextuel ouvert, s'il y en a un
QMenu* menuOuvert(const QString& nom) {
  for (QWidget* w : QApplication::topLevelWidgets())
    if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible() && m->objectName() == nom) return m;
  return nullptr;
}
QAction* ligne(QMenu* m, const QString& debut) {
  if (m)
    for (QAction* a : m->actions())
      if (a->text().startsWith(debut)) return a;
  return nullptr;
}
// Une ligne d'un sous-menu : la ligne parente ouvre le sous-menu, puis l'entrée y est cliquée ; le menu se referme
bool cliquerSousLigne(QMenu* m, const QString& parent, const QString& entree) {
  QAction* x = ligne(m, parent);
  if (!m || !x || !x->menu() || !m->isVisible()) {
    if (m) m->close();
    return false;
  }
  const QPoint ou = m->actionGeometry(x).center();
  reel::deplacer(m, ou);
  reel::cliquer(m, ou);
  attendre(200);
  // Le menu se détruit de lui-même une fois fermé (WA_DeleteOnClose) : ses pointeurs sont gardés
  QPointer<QMenu> s = x->menu(), garde = m;
  QAction* y = s ? ligne(s, entree) : nullptr;
  const bool ok = s && s->isVisible() && y && cliquerLigne(s, y);
  for (const QPointer<QMenu>& z : {s, garde})
    if (z && z->isVisible()) z->close();
  return ok;
}
// Le bouton du bandeau de l'outil, par l'identifiant de son action
// Le bouton montré du bandeau (les boutons d'un bandeau refait attendent d'être détruits, cachés, sous le même nom)
QToolButton* boutonBandeau(VueSchema* v, const QString& id) { return v->bandeau()->bouton(id); }
// Le seul symbole de la feuille d'essai posé avec ce symbole de bibliothèque, le dernier s'il y en a plusieurs
const SymbolePlace* dernierSymbole(const Feuille& f, const std::string& symbole) {
  const SymbolePlace* r = nullptr;
  for (const auto& s : f.symboles)
    if (s.symbole == symbole) r = &s;
  return r;
}
// Un segment traverse-t-il le corps d'un symbole (sa boîte réduite d'un demi-pas, bouts de broches exclus) ?
bool traverse(const Fil& w, const Boite& corps) {
  const Boite b = corps.gonflee(-GRILLE_SCHEMA / 2);
  if (w.a.y == w.b.y) return w.a.y > b.y0 && w.a.y < b.y1 && std::max(w.a.x, w.b.x) > b.x0 && std::min(w.a.x, w.b.x) < b.x1;
  if (w.a.x == w.b.x) return w.a.x > b.x0 && w.a.x < b.x1 && std::max(w.a.y, w.b.y) > b.y0 && std::min(w.a.y, w.b.y) < b.y1;
  return true;  // un fil en biais compte comme traversant : le schéma veut l'équerre
}
// Les fils de la feuille mènent-ils de a à b (bouts communs ou point sur un segment) ?
bool relies(const Feuille& f, Pt a, Pt b) {
  std::vector<Pt> atteints{a};
  std::vector<bool> pris(f.fils.size(), false);
  for (bool encore = true; encore;) {
    encore = false;
    for (std::size_t i = 0; i < f.fils.size(); ++i) {
      if (pris[i]) continue;
      const Fil& w = f.fils[i];
      for (const Pt& p : atteints)
        if (p == w.a || p == w.b || surSegment(p, w.a, w.b)) {
          pris[i] = encore = true;
          atteints.push_back(w.a);
          atteints.push_back(w.b);
          break;
        }
    }
  }
  return std::find(atteints.begin(), atteints.end(), b) != atteints.end();
}
}  // namespace

void essaisElectroniqueL2(FenetrePrincipale& f, const Verifier& verifier0) {
  // PROMETHEE_TRACE_FENETRE : la fenêtre active et le widget qui a le focus, à chaque vérification (diagnostic)
  const bool trace = qEnvironmentVariableIsSet("PROMETHEE_TRACE_FENETRE");
  const auto verifier = [&verifier0, &f, trace](bool ok, const std::string& quoi, const std::string& detail) {
    if (trace) {
      QWidget* active = QApplication::activeWindow();
      QWidget* focus = QApplication::focusWidget();
      std::printf("· fenêtre active %s, focus %s\n", active ? active->metaObject()->className() : "aucune", focus ? focus->metaObject()->className() : "aucun");
    }
    verifier0(ok, quoi, detail);
    // Sous un serveur graphique sans gestionnaire de fenêtres, une fenêtre modale refermée ne rend la main à
    // personne : la fenêtre est réactivée comme d'un clic de l'utilisateur
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  if (!f.isWindow()) {
    prom::Projet vide = prom::projetVide();
    vide.sansCarte = true;
    doc->creer(vide);
    attendre(200);
  }
  repondre(QMessageBox::DestructiveRole);
  f.choixAccueilPublic(QStringLiteral("exemple-elec"));
  attendre(900);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a || !conception(doc)) {
    verifier(false, "L2 : l'exemple livré s'ouvre pour les essais du lot", {});
    return;
  }
  verifier(reel::activer(&f), "L2 : la fenêtre est active", {});
  // Une feuille d'essai vide, au format A3, ajoutée à l'exemple : les gestes y ont la place
  a->modifierConception([](ConceptionElec& c) {
    Feuille fe;
    fe.uuid = nouvelUuid();
    fe.nom = "Essais L2";
    fe.format = "A3";
    c.feuilles.push_back(fe);
  }, QStringLiteral("Feuille d’essai"));
  const int fi = static_cast<int>(conception(doc)->feuilles.size()) - 1;
  a->ouvrir(0, AtelierElectronique::Schema, fi);
  attendre(300);
  a->recalculer();
  VueSchema* vs = a->vueSchema();
  vs->centrerSur(ptMm(120, 90));
  attendre(150);
  a->montrerBibliotheque();
  attendre(200);

  // ---------- 2.2 · Glisser-déposer depuis la bibliothèque, rotation choisie pendant le glisser (SAI-01, S1) ----------
  {
    const Pt cible = ptMm(101.6, 76.2);
    vs->centrerSur(cible);
    attendre(100);
    const auto [arbre, item] = article(a, QStringLiteral("Condensateur"));
    bool pose = false;
    int rotation = -1;
    bool choisi = false;
    if (arbre && item) {
      const QPoint depart = amenerArticle(arbre, item);
      reel::deplacer(arbre->viewport(), depart);
      reel::appuyer(arbre->viewport(), depart);
      for (int i = 1; i <= 3; ++i) reel::deplacer(arbre->viewport(), depart + QPoint(6 * i, 2 * i));
      // Le pointeur passe sur la feuille, le bouton toujours tenu ; R tourne le condensateur pendant le glisser
      const QPoint arrivee = ecran(vs, cible);
      const QPoint debut = vs->mapFromGlobal(arbre->viewport()->mapToGlobal(depart + QPoint(18, 6)));
      for (int i = 1; i <= 8; ++i) reel::deplacer(vs, debut + (arrivee - debut) * i / 8);
      reel::touche(vs, Qt::Key_R);
      reel::deplacer(vs, arrivee + QPoint(1, 0));
      reel::deplacer(vs, arrivee);
      reel::relacher(vs, arrivee);
      attendre(150);
      if (const Feuille* fe = feuilleEssai(doc))
        if (const SymbolePlace* s = dernierSymbole(*fe, "C")) {
          pose = s->position == cible;
          rotation = s->rotation;
          choisi = vs->selection().count(s->uuid) > 0;
        }
    }
    verifier(pose && rotation == 1 && choisi && vs->outil() == VueSchema::Outil::Selection,
             "SAI-01, S1 : un condensateur glissé de la bibliothèque se pose au lâcher, tourné par R pendant le glisser, et reste choisi",
             "rotation " + std::to_string(rotation));
    reel::touche(vs, Qt::Key_Escape);
  }

  // ---------- 2.3 · Pose en série, Inser, copie, unités enchaînées (SAI-03, SAI-05, SYM-04, S15) ----------
  {
    const Pt depart = ptMm(50.8, 165.1);
    const Nm pas = 7'620'000;
    vs->centrerSur(depart + Pt{5 * pas, 0});
    attendre(100);
    const auto [arbre, item] = article(a, QStringLiteral("Condensateur"));
    if (arbre && item) {
      const QPoint ou = amenerArticle(arbre, item);
      attendre(QApplication::doubleClickInterval() + 50);  // l'appui du glisser précédent, au même endroit, ne fait pas un double-clic
      reel::cliquer(arbre->viewport(), ou);
      attendre(50);
      QTreeWidgetItem* sous = arbre->itemAt(ou);
      verifier(vs->outil() == VueSchema::Outil::Placer && vs->symboleArme() == "C", "B7 : un clic sur un article de la bibliothèque arme sa pose",
               vs->symboleArme() + " / outil " + std::to_string(static_cast<int>(vs->outil())) + " / sous le clic : " + (sous ? sous->text(0).toStdString() : std::string("rien")) +
                   " / courant : " + (arbre->currentItem() ? arbre->currentItem()->text(0).toStdString() : std::string("rien")));
      if (auto* serie = a->findChild<QAbstractButton*>(QStringLiteral("poserEnSerie"))) reel::cliquer(serie, serie->rect().center());
    }
    for (int i = 0; i < 10; ++i) reel::cliquer(vs, ecran(vs, depart + Pt{i * pas, 0}));
    reel::touche(vs, Qt::Key_Escape);
    for (int i = 0; i < 10; ++i) reel::touche(vs, Qt::Key_Insert);
    attendre(100);
    // Les vingt condensateurs de la rangée, de gauche à droite : au même pas, repères qui se suivent
    std::vector<std::pair<Nm, std::string>> rangee;
    const ConceptionElec* c = conception(doc);
    for (const auto& s : feuilleEssai(doc)->symboles)
      if (s.symbole == "C" && s.position.y == depart.y)
        if (const ComposantElec* k = c->composant(s.composant)) rangee.push_back({s.position.x, k->reference});
    std::sort(rangee.begin(), rangee.end());
    bool auPas = rangee.size() == 20, suivis = rangee.size() == 20;
    int premier = -1;
    for (std::size_t i = 0; i < rangee.size(); ++i) {
      auPas &= rangee[i].first == depart.x + static_cast<Nm>(i) * pas;
      const std::string& r = rangee[i].second;
      const int n = r.size() > 1 && r[0] == 'C' && std::all_of(r.begin() + 1, r.end(), [](char x) { return x >= '0' && x <= '9'; }) ? std::stoi(r.substr(1)) : -1;
      if (i == 0) premier = n;
      suivis &= n >= 1 && n == premier + static_cast<int>(i);
    }
    verifier(auPas && suivis, "SAI-03, SAI-05 : dix poses en série à la souris puis dix par Inser : vingt condensateurs au même pas, repères donnés à la pose et qui se suivent",
             std::to_string(rangee.size()) + " posé(s), de " + (rangee.empty() ? std::string("—") : rangee.front().second) + " à " + (rangee.empty() ? std::string("—") : rangee.back().second));
    // Ctrl + glisser le dernier : une copie se dépose, le modèle reste ; un seul pas d'annulation
    if (rangee.size() == 20) {
      reel::touche(vs, Qt::Key_Escape);
      const Pt dernier{rangee.back().first, depart.y};
      vs->centrerSur(dernier);
      attendre(100);
      const std::size_t avant = feuilleEssai(doc)->symboles.size();
      const int pasHistorique = doc->positionHistorique();
      reel::glisser(vs, ecran(vs, dernier), ecran(vs, dernier + ptMm(0, 10.16)), Qt::ControlModifier);
      attendre(100);
      bool copie = false, modele = false;
      for (const auto& s : feuilleEssai(doc)->symboles) {
        copie |= s.symbole == "C" && s.position == dernier + ptMm(0, 10.16);
        modele |= s.symbole == "C" && s.position == dernier;
      }
      verifier(copie && modele && feuilleEssai(doc)->symboles.size() == avant + 1 && doc->positionHistorique() == pasHistorique + 1,
               "SAI-03 : Ctrl + glisser un composant posé en dépose une copie, en un pas, le modèle reste en place",
               std::string(copie ? "copie" : "pas de copie") + (modele ? ", modèle en place" : ", modèle déplacé") + ", " + std::to_string(feuilleEssai(doc)->symboles.size() - avant) + " symbole(s) de plus, " +
                   std::to_string(doc->positionHistorique() - pasHistorique) + " pas");
      reel::touche(vs, Qt::Key_Escape);
    }
    // Unités enchaînées, en série : cinq clics posent les cinq unités d'un 74HC00 par porte, du même composant ; le
    // sixième commence un composant neuf
    const auto [arbre2, item2] = article(a, QStringLiteral("74HC00 (par porte)"));
    const Pt ligne = ptMm(50.8, 215.9);
    vs->centrerSur(ligne + ptMm(50.8, 0));
    attendre(100);
    std::map<std::string, std::set<int>> unites;
    if (arbre2 && item2) {
      reel::cliquer(arbre2->viewport(), amenerArticle(arbre2, item2));
      if (auto* serie = a->findChild<QAbstractButton*>(QStringLiteral("poserEnSerie"))) reel::cliquer(serie, serie->rect().center());
      for (int i = 0; i < 6; ++i) reel::cliquer(vs, ecran(vs, ligne + ptMm(17.78 * i, 0)));
      for (const auto& s : feuilleEssai(doc)->symboles)
        if (s.symbole == "74HC00_unites" && s.position.y == ligne.y) unites[s.composant].insert(s.unite);
    }
    std::size_t complets = 0, neufs = 0;
    for (const auto& [k, u] : unites) {
      complets += u.size() == 5;
      neufs += u == std::set<int>{1};
    }
    verifier(unites.size() == 2 && complets == 1 && neufs == 1, "SYM-04 : en série, les unités s'enchaînent, A à E du même composant, puis un composant neuf",
             std::to_string(unites.size()) + " composant(s)");
    reel::touche(vs, Qt::Key_Escape);
  }

  // ---------- 2.6 · Valeur éditée sur place (SAI-14, S8) ----------
  {
    bool change = false;
    std::string valeur;
    if (const Feuille* fe = feuilleEssai(doc))
      if (const SymbolePlace* s = dernierSymbole(*fe, "C")) {
        const ConceptionElec* c = conception(doc);
        const DefSymbole* d = c->symbole(s->symbole);
        const ComposantElec* k = c->composant(s->composant);
        if (d && k) {
          vs->centrerSur(s->position);
          attendre(100);
          // Le texte de la valeur, là où la feuille le dessine
          const QRectF z = vs->zoneChamp(s->uuid, true);
          reel::doubleCliquer(vs, z.isEmpty() ? ecran(vs, transformer(*s, d->posValeur)) : z.center().toPoint());
          attendre(100);
          if (QLineEdit* e = vs->saisieEnCours()) {
            reel::touche(e, Qt::Key_A, Qt::ControlModifier);
            reel::taper(e, QStringLiteral("4k7"));
            reel::touche(e, Qt::Key_Return);
            attendre(100);
          }
          if (const ComposantElec* k2 = conception(doc)->composant(s->composant)) valeur = k2->valeur;
          change = valeur == "4k7";
        }
      }
    verifier(change, "SAI-14, S8 : double-clic sur la valeur, « 4k7 », Entrée : la feuille montre la nouvelle valeur", valeur);
    reel::touche(vs, Qt::Key_Escape);
    // Tab passe de la valeur au repère ; un champ pris à la souris garde sa nouvelle place
    if (const Feuille* fe = feuilleEssai(doc))
      if (const SymbolePlace* s = dernierSymbole(*fe, "C")) {
        const std::string id = s->uuid, comp = s->composant;
        reel::doubleCliquer(vs, vs->zoneChamp(id, true).center().toPoint());
        attendre(80);
        if (QLineEdit* e = vs->saisieEnCours()) reel::touche(e, Qt::Key_Tab);
        attendre(80);
        if (QLineEdit* e = vs->saisieEnCours()) {
          reel::touche(e, Qt::Key_A, Qt::ControlModifier);
          reel::taper(e, QStringLiteral("C99"));
          reel::touche(e, Qt::Key_Return);
          attendre(80);
        }
        const ComposantElec* k = conception(doc)->composant(comp);
        verifier(k && k->reference == "C99", "SAI-14 : Tab passe de la valeur au repère, sur place", k ? k->reference : std::string("?"));
        const QRectF avant = vs->zoneChamp(id, true);
        reel::glisser(vs, avant.center().toPoint(), avant.center().toPoint() + QPoint(0, static_cast<int>(vs->echelle() * 5.08)));
        attendre(80);
        const QRectF apres = vs->zoneChamp(id, true);
        bool garde = false;
        for (const auto& x : feuilleEssai(doc)->symboles)
          if (x.uuid == id) garde = x.champValeur && x.champValeur->bloque;
        verifier(garde && apres.center().y() > avant.center().y() + 2 * vs->echelle(), "SAI-14 : la valeur se déplace à la souris et garde sa place", {});
        reel::touche(vs, Qt::Key_Escape);
      }
  }

  // ---------- 2.5 · Menu contextuel : clic droit court sur l'objet, glissé pour la vue (SAI-20, S7) ----------
  {
    // Une résistance à elle, à l'écart des autres essais
    const Pt ou = ptMm(203.2, 76.2);
    a->modifierConception([&](ConceptionElec& c) { poserSymbole(c, c.feuilles.size() - 1, "R", ou); }, QStringLiteral("Résistance du menu"));
    const SymbolePlace* r = nullptr;
    for (const auto& x : feuilleEssai(doc)->symboles)
      if (x.symbole == "R" && x.position == ou) r = &x;
    bool menu = false, tourne = false, vue = false, sansMenu = false;
    if (r) {
      const std::string id = r->uuid;
      const int rotation = r->rotation;
      vs->centrerSur(r->position);
      attendre(100);
      const QPoint p = ecran(vs, r->position);
      reel::cliquer(vs, p, Qt::RightButton);
      attendre(150);
      QMenu* m = menuOuvert(QStringLiteral("menuContextuel"));
      menu = m != nullptr;
      if (QAction* t = ligne(m, QStringLiteral("Tourner\t")); t ? cliquerLigne(m, t) : false) {
        for (const auto& x : feuilleEssai(doc)->symboles)
          if (x.uuid == id) tourne = x.rotation == (rotation + 1) % 4;
      } else if (m)
        m->close();
      // Clic droit glissé : la vue se déplace, aucun menu
      const QPointF avant = vs->versEcran(ptMm(0, 0));
      reel::appuyer(vs, p + QPoint(30, 30), Qt::RightButton);
      for (int i = 1; i <= 6; ++i) reel::deplacer(vs, p + QPoint(30 + 10 * i, 30 + 6 * i));
      reel::relacher(vs, p + QPoint(90, 66), Qt::RightButton);
      attendre(150);
      vue = vs->versEcran(ptMm(0, 0)) != avant;
      sansMenu = menuOuvert(QStringLiteral("menuContextuel")) == nullptr;
    }
    verifier(menu && tourne, "SAI-20, S7 : clic droit court sur un symbole : son menu ; « Tourner » le tourne", {});
    verifier(vue && sansMenu, "SAI-20 : clic droit glissé : la vue se déplace, sans menu", {});
    reel::touche(vs, Qt::Key_Escape);
  }

  // ---------- 2.7 · Fil tracé autour d'un symbole, sans le traverser (SAI-06, S10) ----------
  {
    const Pt centreR = ptMm(139.7, 76.2);
    std::string idR;
    a->modifierConception([&](ConceptionElec& c) {
      // Une résistance couchée : son corps barre la ligne droite entre les deux points du fil
      idR = poserSymbole(c, static_cast<std::size_t>(fi), "R", centreR, 1);
    }, QStringLiteral("Résistance d’essai"));
    a->recalculer();
    const Pt depart = ptMm(127, 76.2), arrivee = ptMm(152.4, 76.2);
    vs->centrerSur(centreR);
    attendre(100);
    const std::size_t filsAvant = feuilleEssai(doc)->fils.size();
    reel::touche(vs, Qt::Key_W);
    reel::cliquer(vs, ecran(vs, depart));
    reel::deplacer(vs, ecran(vs, arrivee) + QPoint(0, 1));
    reel::deplacer(vs, ecran(vs, arrivee));
    reel::doubleCliquer(vs, ecran(vs, arrivee));
    attendre(100);
    reel::touche(vs, Qt::Key_Escape);
    reel::touche(vs, Qt::Key_Escape);
    const Feuille* fe = feuilleEssai(doc);
    const ConceptionElec* c = conception(doc);
    Boite corps;
    for (const auto& s : fe->symboles)
      if (s.uuid == idR)
        if (const DefSymbole* d = c->symbole(s.symbole)) corps = boiteSymbole(s, *d);
    int traversants = 0;
    for (std::size_t i = filsAvant; i < fe->fils.size(); ++i) traversants += traverse(fe->fils[i], corps);
    verifier(fe->fils.size() > filsAvant && traversants == 0 && relies(*fe, depart, arrivee),
             "SAI-06, S10 : un fil tracé d'un côté à l'autre d'une résistance la contourne, à l'équerre, sans traverser son corps",
             std::to_string(fe->fils.size() - filsAvant) + " segment(s), " + std::to_string(traversants) + " traversant(s)");
  }

  // ---------- 2.8 · Symbole relié déplacé : fils à l'équerre, marques suivies (SAI-08, S16, S17) ----------
  {
    const Pt centre = ptMm(114.3, 114.3);
    std::string idS;
    Pt bout{};
    a->modifierConception([&](ConceptionElec& c) {
      Feuille& fe = c.feuilles[static_cast<std::size_t>(fi)];
      idS = poserSymbole(c, static_cast<std::size_t>(fi), "R", centre, 0);
      // Un fil part de la broche du haut, monte de 5,08 mm puis va à gauche de 10,16 mm ; la broche du bas porte une
      // marque de non-connexion
      const Pt haut = centre + ptMm(0, -3.81), coin = haut + ptMm(0, -5.08);
      bout = coin + ptMm(-10.16, 0);
      fe.fils.push_back({nouvelUuid(), haut, coin});
      fe.fils.push_back({nouvelUuid(), coin, bout});
      fe.nonConnexions.push_back({centre + ptMm(0, 3.81), nouvelUuid()});
    }, QStringLiteral("Symbole relié d’essai"));
    a->recalculer();
    vs->centrerSur(centre);
    attendre(100);
    reel::touche(vs, Qt::Key_Escape);
    const Pt corps = centre + ptMm(0, 1.27);
    reel::glisser(vs, ecran(vs, corps), ecran(vs, corps + ptMm(7.62, 5.08)));
    attendre(150);
    const Feuille* fe = feuilleEssai(doc);
    Pt broche{}, basBroche{};
    for (const auto& s : fe->symboles)
      if (s.uuid == idS) {
        broche = s.position + ptMm(0, -3.81);
        basBroche = s.position + ptMm(0, 3.81);
      }
    int biais = 0;
    for (const auto& w : fe->fils) biais += w.a.x != w.b.x && w.a.y != w.b.y;
    bool marque = false;
    for (const auto& n : fe->nonConnexions) marque |= n.p == basBroche;
    const bool deplace = broche == centre + ptMm(7.62, 5.08 - 3.81);
    verifier(deplace && biais == 0 && relies(*fe, bout, broche), "SAI-08, S16 : un symbole relié glissé garde tous ses fils à l'équerre, et toujours reliés",
             std::string(deplace ? "déplacé" : "pas déplacé") + ", " + std::to_string(biais) + " fil(s) en biais, " + (relies(*fe, bout, broche) ? "relié" : "décroché"));
    verifier(deplace && marque, "S17 : la marque de non-connexion suit la broche du symbole glissé", {});
    reel::touche(vs, Qt::Key_Escape);
  }

  // ---------- 2.10 · Étiquettes à incrément, alimentation au nom libre (SAI-11, SAI-12, S13, S20) ----------
  {
    const Pt e0 = ptMm(203.2, 50.8);
    vs->centrerSur(e0 + ptMm(0, 5.08));
    attendre(100);
    reel::touche(vs, Qt::Key_Escape);
    reel::touche(vs, Qt::Key_L);
    std::vector<std::string> noms;
    for (int i = 0; i < 3; ++i) {
      reel::cliquer(vs, ecran(vs, e0 + ptMm(0, 5.08 * i)));
      attendre(60);
      if (QLineEdit* e = vs->saisieEnCours()) {
        if (i == 0) {
          reel::touche(e, Qt::Key_A, Qt::ControlModifier);
          reel::taper(e, QStringLiteral("D0"));
        }
        reel::touche(e, Qt::Key_Return);
        attendre(60);
      }
    }
    for (const auto& x : feuilleEssai(doc)->etiquettes)
      if (x.p.x == e0.x && x.p.y >= e0.y && x.p.y <= e0.y + 5'080'000 * 2) noms.push_back(x.texte);
    std::sort(noms.begin(), noms.end());
    verifier(noms == std::vector<std::string>{"D0", "D1", "D2"}, "SAI-11 : L, clic, « D0 », Entrée, clic, Entrée, clic, Entrée : D0, D1, D2",
             noms.empty() ? std::string("aucune") : noms.front() + "…" + noms.back());
    // Un clic ailleurs, sur un champ prérempli non retouché : rien n'est posé (S20)
    const std::size_t avant = feuilleEssai(doc)->etiquettes.size();
    reel::cliquer(vs, ecran(vs, e0 + ptMm(0, 20.32)));
    attendre(60);
    reel::cliquer(vs, ecran(vs, e0 + ptMm(25.4, 20.32)));
    attendre(60);
    if (QLineEdit* e = vs->saisieEnCours()) reel::touche(e, Qt::Key_Escape);
    verifier(feuilleEssai(doc)->etiquettes.size() == avant, "S20 : un clic ailleurs sur un nom proposé non retouché ne pose rien", {});
    reel::touche(vs, Qt::Key_Escape);
    reel::touche(vs, Qt::Key_Escape);
    // P : alimentation ; le nom « -12V » pris dans la liste du bandeau, puis un clic
    reel::touche(vs, Qt::Key_P);
    bool nomLibre = false;
    if (QToolButton* b = vs->bandeau()->bouton(QStringLiteral("nom-alim")); b && b->menu()) {
      QMenu* m = b->menu();
      QTimer::singleShot(250, [m] {
        for (QAction* x : m->actions())
          if (x->text() == QStringLiteral("-12V")) {
            reel::cliquer(m, m->actionGeometry(x).center());
            return;
          }
        m->close();
      });
      reel::cliquer(b, b->rect().center());
      attendre(400);
      const Pt p = e0 + ptMm(25.4, 0);
      reel::cliquer(vs, ecran(vs, p));
      attendre(60);
      for (const auto& x : feuilleEssai(doc)->symboles) nomLibre |= x.position == p && x.nomAlim == "-12V";
    }
    verifier(nomLibre, "S13, SAI-12 : une alimentation au nom libre (-12V) se pose depuis le bandeau, sans renommer", {});
    reel::touche(vs, Qt::Key_Escape);
  }

  // ---------- 2.11 · Vérification à trois niveaux : marques de dessin, F7, problème cliqué, F4, à refaire (S9, S28, P29) ----------
  {
    a->modifierConception([](ConceptionElec& c) {
      Feuille fe;
      fe.uuid = nouvelUuid();
      fe.nom = "Vingt";
      fe.format = "A3";
      c.feuilles.push_back(fe);
      const std::size_t n = c.feuilles.size() - 1;
      for (int i = 0; i < 20; ++i) {
        const std::string s = poserSymbole(c, n, "R", ptMm(40.64 + 15.24 * (i % 10), 60.96 + 30.48 * (i / 10)));
        for (const auto& x : c.feuilles[n].symboles)
          if (x.uuid == s) annoterALaPose(c, x.composant);
      }
      c.composants.back().valeur.clear();  // ERC-26, du niveau complet : rien avant « Vérifier »
    }, QStringLiteral("Vingt résistances"));
    const int fv = static_cast<int>(conception(doc)->feuilles.size()) - 1;
    a->ouvrir(0, AtelierElectronique::Schema, fv);
    attendre(200);
    a->recalculer();
    vs->recadrer();
    attendre(150);
    auto compter = [vs](VueSchema::GenreMarque g, bool perimees = false) {
      int n = 0;
      for (const auto& m : vs->marquesErc()) n += m.genre == g && (!perimees || m.perimee);
      return n;
    };
    const int rouges = compter(VueSchema::GenreMarque::Erreur), orangees = compter(VueSchema::GenreMarque::Avertissement), grises = compter(VueSchema::GenreMarque::Dessin);
    verifier(rouges == 0 && orangees == 0 && grises == 40, "S9 : vingt composants non reliés : aucune marque rouge ni orangée, un petit cercle gris au bout de chaque broche",
             std::to_string(rouges) + " rouge(s), " + std::to_string(orangees) + " orangée(s), " + std::to_string(grises) + " grise(s)");
    QDockWidget* dock = a->panneau(AtelierElectronique::Panneau::Verifications);
    vs->setFocus();
    reel::touche(vs, Qt::Key_F7);
    attendre(250);
    QTreeWidget* liste = a->verifications() ? a->verifications()->liste() : nullptr;
    verifier(dock && dock->isVisible() && liste && liste->topLevelItemCount() > 0 && compter(VueSchema::GenreMarque::Avertissement) >= 1,
             "S9 : F7 lance la vérification complète, ouvre le panneau Vérifications et montre la valeur manquante", {});
    // Un clic sur le problème : sa feuille, son composant choisi (P29)
    QTreeWidgetItem* valeur = nullptr;
    if (liste)
      for (int i = 0; i < liste->topLevelItemCount() && !valeur; ++i)
        if (liste->topLevelItem(i)->data(0, Qt::UserRole).toString() == QLatin1String("ERC-26") && liste->topLevelItem(i)->childCount() > 0) valeur = liste->topLevelItem(i)->child(0);
    std::string attendu;
    if (const ConceptionElec* c = conception(doc))
      for (const auto& s : c->feuilles[static_cast<std::size_t>(fv)].symboles)
        if (s.composant == c->composants.back().uuid) attendu = s.uuid;
    const std::string texteValeur = valeur ? valeur->text(0).toStdString() : std::string("ERC-26 absente de la liste");
    if (valeur) {
      valeur->parent()->setExpanded(true);
      liste->scrollToItem(valeur);
      attendre(60);
      vs->selectionner({});
      reel::cliquer(liste->viewport(), liste->visualItemRect(valeur).center());  // la liste se refait ensuite
      attendre(150);
    }
    verifier(valeur && !attendu.empty() && vs->selection().count(attendu) == 1 && vs->feuille() == fv, "P29 : un clic sur le problème mène à sa feuille et choisit son composant", texteValeur);
    // F4 : le problème suivant
    auto courant = [liste]() -> QString {
      QTreeWidgetItem* x = liste ? liste->currentItem() : nullptr;
      return x ? x->text(0) + QLatin1Char('|') + x->text(1) : QString();
    };
    const QString avant = courant();
    vs->setFocus();
    reel::touche(vs, Qt::Key_F4);
    attendre(150);
    verifier(!courant().isEmpty() && courant() != avant, "F4 mène au problème suivant de la liste", courant().toStdString());
    // Le schéma change : les résultats complets restent, hachurés, « à refaire »
    a->modifierConception([fv](ConceptionElec& c) { c.feuilles[static_cast<std::size_t>(fv)].symboles.front().position.x += 2 * GRILLE_SCHEMA; }, QStringLiteral("Déplacer"));
    attendre(150);
    verifier(a->verificationPerimee() && compter(VueSchema::GenreMarque::Avertissement, true) >= 1, "S9 : après une modification, la vérification complète passe « à refaire », ses marques hachurées", {});
    // Exclusion justifiée, au clic droit, sans fenêtre modale (E5)
    bool exclue = false;
    reel::touche(vs, Qt::Key_F7);
    attendre(200);
    if (liste)
      for (int i = 0; i < liste->topLevelItemCount(); ++i)
        if (QTreeWidgetItem* g = liste->topLevelItem(i); g->data(0, Qt::UserRole).toString() == QLatin1String("ERC-26") && g->childCount() > 0) {
          g->setExpanded(true);
          liste->scrollToItem(g->child(0));
          attendre(60);
          reel::cliquer(liste->viewport(), liste->visualItemRect(g->child(0)).center(), Qt::RightButton);
          attendre(200);
          if (cliquerLigne(menuOuvert(QStringLiteral("menuVerification")), ligne(menuOuvert(QStringLiteral("menuVerification")), QStringLiteral("Exclure")))) {
            if (auto* raison = a->verifications()->findChild<QLineEdit*>(QStringLiteral("raisonExclusion")); raison && raison->isVisible()) {
              reel::taper(raison, QStringLiteral("valeur donnée au montage"));
              reel::touche(raison, Qt::Key_Return);
              attendre(250);
              exclue = true;
            }
          }
          break;
        }
    int restantes = 0;
    for (const auto& v : a->ercMontree()) restantes += v.code == "ERC-26";
    const ConceptionElec* cx = conception(doc);
    verifier(exclue && restantes == 0 && cx && !cx->erc.exclusions.empty() && cx->erc.exclusions.back().raison == "valeur donnée au montage",
             "E5 : une alerte s'exclut au clic droit, avec sa raison, gardée dans le projet", std::to_string(restantes) + " ERC-26 restante(s)");
    // Exclus et ignorés (2.33, ERC-15) : l'alerte exclue et le contrôle de DRC ignoré sont listés, et se rétablissent
    {
      a->modifierConception([](ConceptionElec& c) { c.erc.gravites["DRC-43"] = GraviteReglee::Ignorer; }, QStringLiteral("Ignorer DRC-43"));
      auto* filtre = a->verifications() ? a->verifications()->findChild<QComboBox*>(QStringLiteral("filtreVerifications")) : nullptr;
      if (filtre) filtre->setCurrentIndex(3);
      attendre(150);
      auto trouver = [liste](const QString& code, bool ignore) -> QTreeWidgetItem* {
        for (int i = 0; liste && i < liste->topLevelItemCount(); ++i)
          if (QTreeWidgetItem* g = liste->topLevelItem(i); g->data(0, Qt::UserRole).toString() == code) {
            if (ignore && g->childCount() == 0) return g;
            if (!ignore && g->childCount() > 0) {
              g->setExpanded(true);
              return g->child(0);
            }
          }
        return nullptr;
      };
      auto retablir = [&](QTreeWidgetItem* x, const QString& entree) {
        if (!x) return false;
        liste->scrollToItem(x);
        attendre(60);
        reel::cliquer(liste->viewport(), liste->visualItemRect(x).center(), Qt::RightButton);
        attendre(200);
        QMenu* m = menuOuvert(QStringLiteral("menuVerification"));
        const bool ok = cliquerLigne(m, ligne(m, entree));
        attendre(250);
        return ok;
      };
      const bool vuIgnore = trouver(QStringLiteral("DRC-43"), true) != nullptr, vuExclue = trouver(QStringLiteral("ERC-26"), false) != nullptr;
      const bool controle = retablir(trouver(QStringLiteral("DRC-43"), true), QStringLiteral("Rétablir ce contrôle")) && !conception(doc)->erc.gravites.count("DRC-43");
      const bool alerte = retablir(trouver(QStringLiteral("ERC-26"), false), QStringLiteral("Rétablir")) &&
                          std::none_of(conception(doc)->erc.exclusions.begin(), conception(doc)->erc.exclusions.end(), [](const ExclusionErc& e) { return e.code == "ERC-26"; });
      if (filtre) filtre->setCurrentIndex(0);
      verifier(vuIgnore && vuExclue && controle && alerte, "ERC-15 : « Exclus et ignorés » liste l'alerte exclue et le contrôle de DRC ignoré ; chacun se rétablit au clic droit",
               std::string(vuIgnore ? "ignoré listé" : "ignoré absent") + (vuExclue ? ", exclue listée" : ", exclue absente") + (controle ? ", contrôle rétabli" : "") + (alerte ? ", alerte rétablie" : ""));
    }
    if (QWidget* d2 = a->panneau(AtelierElectronique::Panneau::Verifications)) d2->hide();
  }

  // ---------- 2.12 · Navigation : barres, vue d'ensemble, grille, Z, Maj + molette ; Ctrl+F et remplacer (SAI-17, SAI-25, STR-14, S14, S27) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema, 0);
    attendre(200);
    vs->setFocus();
    // Ctrl+F « R12 », Entrée : R12 sur sa feuille, choisi et centré
    reel::touche(vs, Qt::Key_F, Qt::ControlModifier);
    attendre(150);
    PanneauRecherche* pr = a->recherche();
    QDockWidget* dockR = a->panneau(AtelierElectronique::Panneau::Recherche);
    const bool ouvert = dockR && dockR->isVisible() && pr && pr->champ()->hasFocus();
    if (pr) {
      reel::taper(pr->champ(), QStringLiteral("R12"));
      reel::touche(pr->champ(), Qt::Key_Return);
      attendre(200);
    }
    std::string idR12;
    int feuilleR12 = -1;
    if (const ConceptionElec* c = conception(doc))
      for (const auto& k : c->composants)
        if (k.reference == "R12")
          for (std::size_t i = 0; i < c->feuilles.size(); ++i)
            for (const auto& sp : c->feuilles[i].symboles)
              if (sp.composant == k.uuid) {
                idR12 = sp.uuid;
                feuilleR12 = static_cast<int>(i);
              }
    bool centre = false;
    if (const Feuille* f = feuilleR12 >= 0 ? &conception(doc)->feuilles[static_cast<std::size_t>(feuilleR12)] : nullptr)
      for (const auto& sp : f->symboles)
        if (sp.uuid == idR12) centre = vs->rect().contains(vs->versEcran(sp.position).toPoint());
    verifier(ouvert && !idR12.empty() && vs->feuille() == feuilleR12 && vs->selection().count(idR12) == 1 && centre,
             "SAI-17, STR-14 : Ctrl+F « R12 », Entrée : R12 sur sa feuille, choisi et dans la vue", pr ? std::to_string(pr->resultats().size()) + " résultat(s)" : std::string());
    // Remplacer tout : « 10k » devient « 22k » dans toutes les valeurs, en un pas ; Ctrl+Z rend tout
    int changes = 0, restants = 0, avant10k = 0;
    const int pas0 = doc->positionHistorique();
    for (const auto& k : conception(doc)->composants) avant10k += k.valeur == "10k";
    if (pr) {
      pr->chercher(QStringLiteral("10k"));
      changes = pr->remplacerTout(QStringLiteral("22k"));
      attendre(100);
    }
    for (const auto& k : conception(doc)->composants) restants += k.valeur == "10k";
    const bool unPas = doc->positionHistorique() == pas0 + 1;
    doc->annuler();
    attendre(150);
    int rendus = 0;
    for (const auto& k : conception(doc)->composants) rendus += k.valeur == "10k";
    verifier(avant10k > 0 && changes == avant10k && restants == 0 && unPas && rendus == avant10k, "SAI-17 : « Remplacer tout » 10k par 22k change toutes les valeurs en un pas, que Ctrl+Z rend",
             std::to_string(changes) + " / " + std::to_string(avant10k));
    if (dockR) dockR->hide();
    // La grille suit l'accroche (S27)
    vs->setFocus();
    vs->centrerSur(ptMm(100, 80));
    auto aEchelle = [vs](double e) { vs->zoomer(e / vs->echelle()); return vs->pasGrille(); };
    const double fine = aEchelle(10), large = aEchelle(4), aucune = aEchelle(1.5);
    verifier(std::abs(fine - 1.27) < 1e-9 && std::abs(large - 2.54) < 1e-9 && aucune == 0, "S27 : la grille montre l'accroche de 1,27 mm de près, 2,54 mm de plus loin, rien en dessous de 2 px/mm",
             std::to_string(fine) + " / " + std::to_string(large) + " / " + std::to_string(aucune));
    // Barres de défilement et indicateur de zoom (S14)
    aEchelle(6);
    vs->repaint();
    attendre(100);
    CadreVue* cadre = vs->cadre();
    QScrollBar* barre = cadre ? cadre->barreH() : nullptr;
    QToolButton* zoom = a->indicateurZoom();
    const QPointF origine0 = vs->versEcran(ptMm(0, 0));
    const int valeur0 = barre ? barre->value() : -1;
    if (barre && barre->isVisible()) {
      reel::cliquer(barre, QPoint(barre->width() - 30, barre->height() / 2));  // une page vers la droite
      attendre(100);
    }
    const QPointF origine1 = vs->versEcran(ptMm(0, 0));
    const std::string detailBarre = barre ? "barre " + std::to_string(barre->isVisible()) + " " + std::to_string(barre->geometry().x()) + "," + std::to_string(barre->geometry().y()) + " " +
                                                std::to_string(barre->width()) + "×" + std::to_string(barre->height()) + ", valeur " + std::to_string(valeur0) + " → " + std::to_string(barre->value()) + " sur " +
                                                std::to_string(barre->maximum()) + ", origine " + std::to_string(origine0.x()) + " → " + std::to_string(origine1.x())
                                          : std::string("pas de barre");
    verifier(barre && barre->isVisible() && cadre->barreV()->isVisible() && origine1.x() < origine0.x() - 20 && std::abs(origine1.y() - origine0.y()) < 1 && zoom && zoom->isVisible() &&
                 zoom->text().endsWith(QLatin1Char('%')),
             "S14 : un clic dans la barre horizontale fait défiler la vue d'une page ; l'indicateur de zoom dit le taux", (zoom ? zoom->text().toStdString() : std::string()) + " ; " + detailBarre);
    // Maj + molette fait défiler verticalement, sans zoomer
    const double e0 = vs->echelle();
    const QPointF ici(vs->width() / 2.0, vs->height() / 2.0);
    QWheelEvent molette(ici, vs->mapToGlobal(ici), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(vs, &molette);
    attendre(60);
    const QPointF origine2 = vs->versEcran(ptMm(0, 0));
    verifier(vs->echelle() == e0 && std::abs(origine2.y() - origine1.y()) > 10 && std::abs(origine2.x() - origine1.x()) < 1, "SAI-25 : Maj + molette fait défiler la vue verticalement, sans zoomer", {});
    // Vue d'ensemble : un clic sur son centre amène le centre du dessin au centre de la vue
    bool ensemble = false;
    std::string detailEnsemble;
    if (cadre) {
      if (!cadre->ensembleVisible()) reel::cliquer(cadre->boutonEnsemble(), cadre->boutonEnsemble()->rect().center());
      attendre(100);
      QWidget* vig = cadre->ensemble();
      if (vig && vig->isVisible()) {
        reel::cliquer(vig, vig->rect().center());
        attendre(100);
        const Boite b = cadreFeuille(conception(doc)->feuilles[static_cast<std::size_t>(vs->feuille())].format);
        const QPointF milieu = vs->versEcran(b.centre());
        ensemble = std::abs(milieu.x() - vs->width() / 2.0) < 40 && std::abs(milieu.y() - vs->height() / 2.0) < 40;
        detailEnsemble = "milieu de la feuille à " + std::to_string(milieu.x()) + "," + std::to_string(milieu.y()) + " ; vue " + std::to_string(vs->width()) + "×" + std::to_string(vs->height());
      } else
        detailEnsemble = vig ? "vignette cachée" : "pas de vignette";
      if (cadre->ensembleVisible()) reel::cliquer(cadre->boutonEnsemble(), cadre->boutonEnsemble()->rect().center());
    }
    verifier(ensemble, "SAI-25 : la vue d'ensemble s'ouvre d'un clic, et un clic en son centre y amène la vue", detailEnsemble);
    // Z : la sélection cadrée
    if (!idR12.empty()) {
      a->ouvrir(0, AtelierElectronique::Schema, feuilleR12);
      attendre(100);
      vs->selectionner({idR12});
      aEchelle(2);
      vs->setFocus();
      reel::touche(vs, Qt::Key_Z);
      attendre(80);
    }
    verifier(!idR12.empty() && vs->echelle() > 6, "SAI-25 : Z cadre la sélection", std::to_string(vs->echelle()) + " px/mm");
    reel::touche(vs, Qt::Key_Escape);
  }

  // ---------- 2.13 · Copier-coller d'un projet à l'autre par le presse-papiers du système, collage qui suit la souris (SAI-27, S32) ----------
  {
    // Dans l'exemple : un symbole personnel, posé sur la feuille d'essai, copié
    DefSymbole perso = *symboleBibliotheque("R");
    perso.id = "perso:Shunt";
    perso.nom = "Shunt";
    perso.traces.push_back(perso.traces.front());
    std::string idShunt;
    const int fe = static_cast<int>(conception(doc)->feuilles.size()) - 1;
    a->modifierConception([&](ConceptionElec& c) {
      c.symboles[perso.id] = perso;
      idShunt = poserSymbole(c, c.feuilles.size() - 1, perso.id, ptMm(254, 101.6));
    }, QStringLiteral("Shunt"));
    a->ouvrir(0, AtelierElectronique::Schema, fe);
    attendre(150);
    vs->selectionner({idShunt});
    vs->setFocus();
    reel::touche(vs, Qt::Key_C, Qt::ControlModifier);
    attendre(80);
    // Un autre projet, neuf
    prom::Projet autre = prom::projetVide();
    autre.sansCarte = true;
    autre.electronique.push_back(nouvelleConception("Projet B"));
    autre.nom = "Projet B";
    doc->creer(autre);
    f.montrerElectronique(0, AtelierElectronique::Schema);
    attendre(300);
    AtelierElectronique* b = f.atelierElectronique();
    VueSchema* vb = b ? b->vueSchema() : nullptr;
    bool suit = false, pose = false, fige = false, repere = false, unPas = false;
    if (vb) {
      vb->recadrer();
      attendre(100);
      const Pt la = ptMm(101.6, 76.2), ici = ptMm(127, 101.6);
      reel::deplacer(vb, ecran(vb, la));
      vb->setFocus();
      const int pas0 = doc->positionHistorique();
      reel::touche(vb, Qt::Key_V, Qt::ControlModifier);
      attendre(80);
      // Le bloc naît sous la souris, puis la suit : un clic le pose
      Pt naissance{};
      bool ne = false;
      for (const auto& x : doc->projet().electronique.front().feuilles[0].symboles)
        if (x.symbole.rfind("perso:Shunt", 0) == 0) {
          naissance = x.position;
          ne = true;
        }
      const Pt attendu = naissance + (ici - la);
      reel::deplacer(vb, ecran(vb, ici));
      attendre(60);
      const ConceptionElec& cb = doc->projet().electronique.front();
      for (const auto& x : cb.feuilles[0].symboles) suit |= ne && x.symbole.rfind("perso:Shunt", 0) == 0 && x.position == attendu;
      reel::cliquer(vb, ecran(vb, ici));
      attendre(100);
      const ConceptionElec& cb2 = doc->projet().electronique.front();
      for (const auto& x : cb2.feuilles[0].symboles)
        if (x.symbole == "perso:Shunt") {
          pose = x.position == attendu;
          if (const ComposantElec* k = cb2.composant(x.composant)) repere = k->reference == "R1";
        }
      if (const auto it = cb2.symboles.find("perso:Shunt"); it != cb2.symboles.end()) fige = it->second == perso;
      unPas = doc->positionHistorique() == pas0 + 1;
    }
    verifier(suit && pose && fige && repere && unPas, "SAI-27, S32 : un bloc copié dans un projet se colle dans un autre, sa définition personnelle comprise ; il suit la souris jusqu'au clic, repère libre, en un pas",
             std::string(suit ? "suit" : "ne suit pas") + (pose ? ", posé" : ", pas posé") + (fige ? ", définition figée" : ", définition perdue") + (repere ? ", R1" : "") + (unPas ? ", un pas" : ""));
  }

  // ---------- 2.14 · Palette Ctrl+K : toutes les actions de l'atelier, sans accents, mots dans le désordre (SAI-21) ----------
  {
    AtelierElectronique* b = f.atelierElectronique();
    VueSchema* vb = b ? b->vueSchema() : nullptr;
    std::string id;
    int rotation = -1;
    if (const ConceptionElec* c = conception(doc); c && vb)
      for (const auto& x : c->feuilles[0].symboles)
        if (!x.composant.empty()) {
          id = x.uuid;
          rotation = x.rotation;
        }
    QStringList premiers;
    if (vb && !id.empty()) {
      vb->selectionner({id});
      vb->setFocus();
      QTimer::singleShot(400, [&premiers] {
        QDialog* d = nullptr;
        for (QWidget* w : QApplication::topLevelWidgets())
          if (w->isVisible() && w->objectName() == QLatin1String("palette")) d = qobject_cast<QDialog*>(w);
        auto* champ = d ? d->findChild<QLineEdit*>() : nullptr;
        auto* liste = d ? d->findChild<QListWidget*>() : nullptr;
        if (!champ || !liste) {
          if (d) d->reject();
          return;
        }
        for (const QString& mot : {QStringLiteral("tourner"), QStringLiteral("annoter"), QStringLiteral("changer d'empreinte"), QStringLiteral("verifier")}) {
          champ->setText(mot);
          premiers << (liste->count() > 0 ? liste->item(0)->text() : QStringLiteral("—"));
        }
        champ->setText(QStringLiteral("tourner"));
        reel::touche(champ, Qt::Key_Return);
      });
      reel::touche(vb, Qt::Key_K, Qt::ControlModifier);
      attendre(150);
    }
    int apres = -1;
    if (const ConceptionElec* c = conception(doc))
      for (const auto& x : c->feuilles[0].symboles)
        if (x.uuid == id) apres = x.rotation;
    const bool trouves = premiers.size() == 4 && premiers[0].contains(QStringLiteral("Tourner")) && premiers[1].contains(QStringLiteral("Annoter")) &&
                         premiers[2].contains(QStringLiteral("empreinte")) && premiers[3].contains(QStringLiteral("Vérifier"));
    verifier(trouves && apres == (rotation + 1) % 4, "SAI-21 : Ctrl+K « tourner », « annoter », « changer d'empreinte », « verifier » trouvent leur action ; Entrée la lance",
             premiers.join(QStringLiteral(" | ")).toStdString());
  }

  // ---------- 2.16 · Texte tourné à 90° choisi d'un clic sur ses lettres (S25) ----------
  {
    AtelierElectronique* b = f.atelierElectronique();
    VueSchema* vb = b ? b->vueSchema() : nullptr;
    const std::string idT = nouvelUuid();
    const Pt ancre = ptMm(152.4, 101.6);
    bool pris = false;
    if (vb) {
      b->modifierConception([&](ConceptionElec& c) {
        TexteSchema t;
        t.uuid = idT;
        t.texte = "Alimentation 12 V";
        t.p = ancre;
        t.taille = 1'800'000;
        t.rotation = 1;
        c.feuilles[0].textes.push_back(t);
      }, QStringLiteral("Texte tourné"));
      vb->centrerSur(ancre);
      attendre(100);
      vb->selectionner({});
      reel::cliquer(vb, ecran(vb, ancre + ptMm(0, -6)));  // sur les lettres, 6 mm au-dessus du point d'ancrage
      attendre(80);
      pris = vb->selection().count(idT) == 1;
      reel::touche(vb, Qt::Key_Escape);
    }
    verifier(pris, "S25 : un texte tourné de 90° se choisit d'un clic sur ses lettres", {});
  }

  // ---------- 2.17 · Apprendre : parcours de la première carte, lexique, premier lancement (AUT-11, ERG-13, ERG-16) ----------
  {
    AtelierElectronique* b = f.atelierElectronique();
    PanneauApprendre* ap = b ? b->apprendre() : nullptr;
    bool montre = false, suit = false, mot = false, gardees = false;
    QString t1, t2;
    if (ap) {
      b->executerAction("atelier.apprendre");
      attendre(100);
      QDockWidget* dockA = b->panneau(AtelierElectronique::Panneau::Apprendre);
      montre = dockA && dockA->isVisible();
      ap->onglets()->setCurrentIndex(0);
      if (auto* commencer = ap->findChild<QPushButton*>(QStringLiteral("commencerParcours")); commencer && commencer->isVisible()) {
        reel::cliquer(commencer, commencer->rect().center());
        attendre(250);
      }
      t1 = ap->titreEtape()->text();
      const int avant = ap->etape();
      // La LED posée : l'étape passe d'elle-même à la suivante
      b->modifierConception([](ConceptionElec& c) { poserSymbole(c, 0, "LED", ptMm(76.2, 76.2)); }, QStringLiteral("LED"));
      attendre(200);
      t2 = ap->titreEtape()->text();
      suit = avant == 0 && ap->etape() == 1 && conception(doc) && doc->projet().electronique.back().nom == "Première carte";
      // Le lexique, cherché pendant la frappe
      ap->onglets()->setCurrentIndex(1);
      attendre(60);
      reel::cliquer(ap->rechercheLexique(), ap->rechercheLexique()->rect().center());
      reel::taper(ap->rechercheLexique(), QStringLiteral("via"));
      attendre(60);
      for (int i = 0; i < ap->lexique()->count(); ++i) mot |= ap->lexique()->item(i)->text().startsWith(QStringLiteral("via — "));
      // Premier lancement : mil et un fabricant gardés ; une carte neuve les prend
      QSettings s;
      const QVariant u0 = s.value(QStringLiteral("electronique/uniteCartesNeuves")), p0 = s.value(QStringLiteral("electronique/profilCartesNeuves")),
                     l0 = s.value(QStringLiteral("electronique/premierLancement"));
      ap->onglets()->setCurrentIndex(0);
      ap->montrerPremierLancement();
      attendre(60);
      auto* unite = ap->findChild<QComboBox*>(QStringLiteral("uniteCartesNeuves"));
      auto* profil = ap->findChild<QComboBox*>(QStringLiteral("profilCartesNeuves"));
      auto* valider = ap->findChild<QPushButton*>(QStringLiteral("validerPremierLancement"));
      std::string nomProfil;
      if (unite && profil && valider && profil->count() > 1) {
        unite->setCurrentIndex(unite->findData(QStringLiteral("mil")));
        profil->setCurrentIndex(1);
        nomProfil = profil->currentData().toString().toStdString();
        reel::cliquer(valider, valider->rect().center());
        attendre(60);
        ConceptionElec neuve = nouvelleConception("Préférences");
        PanneauApprendre::appliquerPreferences(neuve);
        gardees = PanneauApprendre::premierLancementFait() && neuve.circuit.affichage.unite == "mil" && neuve.circuit.regles.profil == nomProfil && !valider->isVisible();
      }
      // Les réglages d'avant sont rendus : les essais suivants partent du même état
      for (const auto& [cle, v] : {std::pair{QStringLiteral("electronique/uniteCartesNeuves"), u0}, std::pair{QStringLiteral("electronique/profilCartesNeuves"), p0},
                                   std::pair{QStringLiteral("electronique/premierLancement"), l0}})
        v.isValid() ? s.setValue(cle, v) : s.remove(cle);
      ap->quitter();
      if (dockA) dockA->hide();
    }
    verifier(montre && t1.contains(QStringLiteral("Pose une LED")) && suit && t2.contains(QStringLiteral("résistance")),
             "AUT-11 : « Apprendre » ouvre le parcours sur une carte neuve ; la LED posée, l'étape passe d'elle-même à la suivante", (t1 + QStringLiteral(" → ") + t2).toStdString());
    verifier(mot, "ERG-13 : le lexique trouve « via » pendant la frappe", {});
    verifier(gardees, "ERG-16 : au premier lancement, l'unité et le fabricant choisis valent pour les cartes neuves", {});
  }

  // ---------- 2.15 · Thèmes du circuit, contrastes, icônes propres, clavier seul, ruban à 1 024 px (ERG-04, ERG-05, G3, G9 à G12, S12) ----------
  {
    // Contrastes mesurés (formule du WCAG), dans chaque thème
    const IdTheme themeAvant = Theme::instance().choix();
    QStringList faibles;
    for (IdTheme id : {IdTheme::Promethee, IdTheme::Clair, IdTheme::Sombre, IdTheme::ContrasteEleve}) {
      Theme::instance().choisir(id);
      const EncresCircuit& e = theme().circuit();
      const EncresSchema& sc = theme().schema();
      auto paire = [&](const QString& nom, const QColor& x, const QColor& y) {
        if (const double r = contraste(x, y); r < 3.0) faibles << QStringLiteral("%1, %2 : %3:1").arg(Theme::cle(id), nom).arg(r, 0, 'f', 2);
      };
      paire(QStringLiteral("sélection sur cuivre dessus"), e.choix, e.cuivreDessus);
      paire(QStringLiteral("sélection sur cuivre dessous"), e.choix, e.cuivreDessous);
      paire(QStringLiteral("sélection sur cuivre interne 1"), e.choix, e.cuivreInt1);
      paire(QStringLiteral("sélection sur cuivre interne 2"), e.choix, e.cuivreInt2);
      paire(QStringLiteral("sélection sur la carte"), e.choix, e.carte);
      paire(QStringLiteral("sérigraphie dessous sur cuivre dessous"), e.serigDessous, e.cuivreDessous);
      paire(QStringLiteral("aperçu du schéma sur le papier"), sc.apercu, sc.papier);
      if (encres_circuit::CHOIX != e.choix || encres_circuit::couleurCuivre(Couche::CuivreDessous) != e.cuivreDessous) faibles << QStringLiteral("%1 : encres du circuit hors du thème").arg(Theme::cle(id));
    }
    Theme::instance().choisir(themeAvant);
    verifier(faibles.isEmpty(), "G3, G9, ERG-04 : dans chaque thème, la sélection tient 3:1 sur chaque cuivre et sur la carte, la sérigraphie du dessous sur son cuivre, l'aperçu sur le papier",
             faibles.join(QStringLiteral(" ; ")).toStdString());
    // Une icône par sens (S12)
    const RegistreActions& r = RegistreActions::atelierElectronique();
    auto ic = [&r](const char* id) {
      const ActionAtelier* x = r.action(id);
      return x ? x->icone : QString();
    };
    const QStringList groupes[] = {{ic("edition.copier"), ic("edition.coller"), ic("edition.dupliquer"), ic("edition.couper"), ic("selection.supprimer")},
                                   {ic("schema.alimentation.5v"), ic("schema.alimentation.gnd"), ic("schema.drapeauAlimentation")},
                                   {ic("schema.texte"), ic("circuit.mesurer")},
                                   {ic("schema.erc"), ic("circuit.drc"), ic("fabrication.valider"), ic("fabrication.ipc")}};
    QStringList doubles;
    for (const QStringList& g : groupes) {
      if (QSet<QString>(g.begin(), g.end()).size() != g.size()) doubles << g.join(QLatin1Char('/'));
      for (const QString& n : g)
        if (n.isEmpty() || icone(n).isNull()) doubles << QStringLiteral("« %1 » sans dessin").arg(n);
    }
    if (ic("composant.creer") == QLatin1String("nouvelle-piece")) doubles << QStringLiteral("Créer un composant : icône de pièce");
    verifier(doubles.isEmpty(), "S12 : copier, coller, dupliquer, couper, supprimer, alimentation et masse, texte et mesure, ERC, DRC, validation et IPC ont chacun leur icône",
             doubles.join(QStringLiteral(" ; ")).toStdString());
    // Au clavier seul : la recherche, Entrée arme la pose, trois flèches à droite et deux en bas, Entrée pose
    AtelierElectronique* a3 = f.atelierElectronique();
    VueSchema* v3 = a3 ? a3->vueSchema() : nullptr;
    bool clavier = false;
    std::string detailClavier;
    if (v3) {
      a3->setMode(AtelierElectronique::Schema);
      a3->montrerBibliotheque();
      attendre(100);
      PanneauBibliotheque* bib = a3->bibliotheque();
      const std::size_t avant = conception(doc) ? doc->projet().electronique[static_cast<std::size_t>(a3->conception())].feuilles[static_cast<std::size_t>(v3->feuille())].symboles.size() : 0;
      bib->recherche()->setFocus();
      bib->recherche()->selectAll();
      reel::taper(bib->recherche(), QStringLiteral("Condensateur"));
      reel::touche(bib->recherche(), Qt::Key_Return);
      attendre(100);
      const bool arme = v3->outil() == VueSchema::Outil::Placer && v3->hasFocus();
      const QPointF o = v3->versEcran(Pt{0, 0});
      const QPointF centre(v3->width() / 2.0, v3->height() / 2.0);
      const Pt depart = surGrille(ptMm((centre.x() - o.x()) / v3->echelle(), (centre.y() - o.y()) / v3->echelle()));
      for (int i = 0; i < 3; ++i) reel::touche(v3, Qt::Key_Right);
      for (int i = 0; i < 2; ++i) reel::touche(v3, Qt::Key_Down);
      reel::touche(v3, Qt::Key_Return);
      attendre(100);
      const Feuille& fe = doc->projet().electronique[static_cast<std::size_t>(a3->conception())].feuilles[static_cast<std::size_t>(v3->feuille())];
      const Pt attendu = depart + Pt{3 * GRILLE_SCHEMA, 2 * GRILLE_SCHEMA};
      bool pose = false;
      for (const auto& x : fe.symboles) pose |= x.symbole == "C" && x.position == attendu;
      clavier = arme && fe.symboles.size() == avant + 1 && pose && !v3->accessibleName().isEmpty() && !a3->vueCircuit()->accessibleName().isEmpty();
      detailClavier = std::string(arme ? "armé" : "pas armé") + ", " + std::to_string(fe.symboles.size() - avant) + " posé(s)" + (pose ? " au bon endroit" : "");
      reel::touche(v3, Qt::Key_Escape);
    }
    verifier(clavier, "G11, ERG-05 : au clavier seul, la recherche et Entrée arment la pose, les flèches mènent le fantôme, Entrée le pose ; les canevas ont un nom accessible", detailClavier);
    // À 1 024 × 768, aucun onglet du ruban ne cache de commande derrière le débordement (G12)
    QWidget* haut = f.window();
    const QSize taille = haut->size();
    haut->resize(1024, 768);
    attendre(300);
    QTabWidget* ruban = nullptr;
    if (a3)
      for (QTabWidget* t : a3->findChildren<QTabWidget*>())
        if (t->count() == 4 && t->tabText(0) == QStringLiteral("Schéma")) ruban = t;
    QStringList deborde;
    if (ruban) {
      const int courant = ruban->currentIndex();
      for (int i = 0; i < ruban->count(); ++i) {
        if (!ruban->isTabVisible(i)) continue;
        ruban->setCurrentIndex(i);
        attendre(120);
        if (auto* b = qobject_cast<QToolBar*>(ruban->widget(i)))
          if (auto* ext = b->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button")); ext && ext->isVisible()) deborde << ruban->tabText(i);
      }
      ruban->setCurrentIndex(courant);
    }
    haut->resize(taille);
    attendre(200);
    verifier(ruban && deborde.isEmpty(), "G12 : à 1 024 × 768, chaque onglet du ruban tient dans sa largeur, sans débordement", deborde.join(QStringLiteral(", ")).toStdString());
  }

  // ---------- 2.18 · Fil depuis le bout d'une broche libre, avec Sélection ; « Relier à… » au menu d'une broche (SAI-29, SAI-30) ----------
  {
    AtelierElectronique* a4 = f.atelierElectronique();
    VueSchema* v4 = a4 ? a4->vueSchema() : nullptr;
    bool filLibre = false, relie = false;
    std::string detail;
    if (v4) {
      a4->setMode(AtelierElectronique::Schema);
      // Deux résistances à elles, à l'écart, sur la feuille de la carte du parcours
      std::string r1, r2;
      a4->modifierConception([&](ConceptionElec& c) {
        r1 = poserSymbole(c, 0, "R", ptMm(203.2, 50.8));
        r2 = poserSymbole(c, 0, "R", ptMm(228.6, 76.2));
        for (auto& k : c.composants) annoterALaPose(c, k.uuid);
      }, QStringLiteral("Deux résistances"));
      v4->setOutil(VueSchema::Outil::Selection);
      v4->centrerSur(ptMm(215.9, 63.5));
      attendre(150);
      const ConceptionElec* c = &doc->projet().electronique[static_cast<std::size_t>(a4->conception())];
      auto broche = [&](const std::string& s, const std::string& n) {
        for (const auto& sp : c->feuilles[0].symboles)
          if (sp.uuid == s)
            for (const auto& b : brochesPlacees(sp, *c->symbole(sp.symbole)))
              if (b.broche->numero == n) return b.p;
        return Pt{};
      };
      // L'appui sur le bout libre de R1.1 commence un fil ; un clic fixe un coude, un double-clic le termine
      const Pt depart = broche(r1, "1"), coude = depart + ptMm(0, -7.62), fin = coude + ptMm(10.16, 0);
      const std::size_t fils0 = c->feuilles[0].fils.size();
      reel::cliquer(v4, ecran(v4, depart));
      attendre(60);
      const bool enFil = v4->outil() == VueSchema::Outil::Fil;
      reel::cliquer(v4, ecran(v4, coude));
      reel::doubleCliquer(v4, ecran(v4, fin));
      attendre(150);
      c = &doc->projet().electronique[static_cast<std::size_t>(a4->conception())];
      bool depuis = false;
      for (std::size_t i = fils0; i < c->feuilles[0].fils.size(); ++i) depuis |= c->feuilles[0].fils[i].a == depart || c->feuilles[0].fils[i].b == depart;
      filLibre = enFil && depuis && v4->outil() == VueSchema::Outil::Selection;
      detail = std::string(enFil ? "fil commencé" : "pas de fil") + (depuis ? ", posé depuis la broche" : "") + (v4->outil() == VueSchema::Outil::Selection ? ", Sélection rendue" : "");
      // Clic droit sur R1.2 › « Relier à… », puis clic sur R2.1 : un fil les relie
      reel::cliquer(v4, ecran(v4, broche(r1, "2")), Qt::RightButton);
      attendre(150);
      QMenu* m = menuOuvert(QStringLiteral("menuContextuel"));
      if (QAction* x = ligne(m, QStringLiteral("Relier à")); x && cliquerLigne(m, x)) {
        reel::cliquer(v4, ecran(v4, broche(r2, "1")));
        attendre(150);
      } else if (m)
        m->close();
      a4->recalculer();
      c = &doc->projet().electronique[static_cast<std::size_t>(a4->conception())];
      const Connectivite k = connectivite(*c);
      std::string k1, k2;
      for (const auto& sp : c->feuilles[0].symboles) {
        if (sp.uuid == r1) k1 = sp.composant;
        if (sp.uuid == r2) k2 = sp.composant;
      }
      relie = !k1.empty() && k.reseau(k1, "2") >= 0 && k.reseau(k1, "2") == k.reseau(k2, "1");
      reel::touche(v4, Qt::Key_Escape);
    }
    verifier(filLibre, "SAI-29 : avec Sélection, l'appui sur le bout d'une broche libre commence un fil ; fini, la main revient à Sélection", detail);
    verifier(relie, "SAI-30 : clic droit sur une broche › « Relier à… », clic sur l'autre : un fil les relie", {});
  }

  // ---------- 2.19 à 2.25 : sur la carte du parcours, à l'écart de ce qui y est déjà ----------
  if (AtelierElectronique* a4 = f.atelierElectronique(); a4 && a4->vueSchema()) {
    VueSchema* v4 = a4->vueSchema();
    // Les panneaux fermés : la vue a la même taille quels que soient les lots passés avant (ceux qui ouvrent, déplacent
    // ou élargissent des panneaux), et les gestes de ces essais tombent aux mêmes endroits ; chaque essai qui a besoin
    // d'un panneau l'ouvre
    for (const auto p : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur,
                         AtelierElectronique::Panneau::Verifications, AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre,
                         AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts, AtelierElectronique::Panneau::Champs})
      if (QDockWidget* d = a4->panneau(p)) d->hide();
    attendre(200);
    if (trace) std::printf("· vue du schéma : %d × %d, %.3f px/mm\n", v4->width(), v4->height(), v4->echelle());
    a4->setMode(AtelierElectronique::Schema);
    v4->setOutil(VueSchema::Outil::Selection);
    auto conc = [&] { return &doc->projet().electronique[static_cast<std::size_t>(a4->conception())]; };
    auto montrer = [&](Pt p, int feuille = 0) {
      if (v4->feuille() != feuille) v4->setConception(a4->conception(), feuille);
      v4->centrerSur(p);
      attendre(120);
    };
    auto clicDroit = [&](Pt p, QPoint decalage = {}) {
      reel::cliquer(v4, ecran(v4, p) + decalage, Qt::RightButton);
      attendre(150);
      return menuOuvert(QStringLiteral("menuContextuel"));
    };

    // ---------- 2.19 · Fil courbe au bandeau ; « Décaler les croisements » sur la feuille (SAI-06, SAI-09) ----------
    {
      // Mode courbe : le bouton de mode du bandeau, puis un fil à un coude
      const Pt a = ptMm(304.8, 25.4), b = a + ptMm(12.7, 0), fin = b + ptMm(0, 10.16);
      montrer(a + ptMm(6.35, 5.08));
      v4->setOutil(VueSchema::Outil::Fil);
      attendre(60);
      for (int i = 0; i < 4 && !v4->bandeau()->texte().contains(QStringLiteral("courbe")); ++i)
        if (QToolButton* m = boutonBandeau(v4, QStringLiteral("mode"))) {
          reel::cliquer(m, m->rect().center());
          attendre(60);
        }
      const bool rayonMontre = boutonBandeau(v4, QStringLiteral("rayon-fil")) != nullptr;
      const std::size_t fils0 = conc()->feuilles[0].fils.size();
      reel::cliquer(v4, ecran(v4, a));
      reel::cliquer(v4, ecran(v4, b));
      reel::doubleCliquer(v4, ecran(v4, fin));
      attendre(120);
      bool arrondi = false;
      for (std::size_t i = fils0; i < conc()->feuilles[0].fils.size(); ++i) arrondi |= conc()->feuilles[0].fils[i].rayon > 0;
      // Le mode revient à l'équerre pour la suite
      for (int i = 0; i < 4 && !v4->bandeau()->texte().contains(QStringLiteral("équerre")); ++i)
        if (QToolButton* m = boutonBandeau(v4, QStringLiteral("mode"))) {
          reel::cliquer(m, m->rect().center());
          attendre(60);
        }
      v4->setOutil(VueSchema::Outil::Selection);
      verifier(rayonMontre && arrondi, "SAI-06 : le bouton de mode du bandeau passe le fil en courbe, au rayon montré ; le fil posé a ses coins arrondis", {});

      // Six croisements à quatre branches, jonction posée
      const Pt base = ptMm(304.8, 50.8);
      a4->modifierConception([&](ConceptionElec& c) {
        for (int i = 0; i < 6; ++i) {
          const Pt p = base + ptMm(15.24 * i, 0);
          c.feuilles[0].fils.push_back({nouvelUuid(), p - ptMm(5.08, 0), p + ptMm(5.08, 0)});
          c.feuilles[0].fils.push_back({nouvelUuid(), p - ptMm(0, 5.08), p + ptMm(0, 5.08)});
          c.feuilles[0].jonctions.push_back({p, nouvelUuid()});
        }
      }, QStringLiteral("Six croisements"));
      // Branches en chaque point de la zone : bouts de fils, et deux par fil qui le traverse
      auto branches = [&] {
        std::map<Pt, int> n;
        const Feuille& fe = conc()->feuilles[0];
        auto dedans = [&](Pt p) { return p.x >= base.x - ptMm(10, 0).x && p.x <= base.x + ptMm(90, 0).x && std::abs(p.y - base.y) <= ptMm(0, 10).y; };
        std::set<Pt> points;
        for (const auto& w : fe.fils)
          for (const Pt p : {w.a, w.b})
            if (dedans(p)) points.insert(p);
        for (const auto& j : fe.jonctions)
          if (dedans(j.p)) points.insert(j.p);
        for (const Pt p : points)
          for (const auto& w : fe.fils) n[p] += (w.a == p) + (w.b == p) + (p != w.a && p != w.b && surSegment(p, w.a, w.b) ? 2 : 0);
        return n;
      };
      auto compter = [&](int k) {
        int x = 0;
        for (const auto& [p, n] : branches()) x += n == k;
        return x;
      };
      const int quatre0 = compter(4), pas0 = doc->positionHistorique();
      montrer(base + ptMm(38.1, 0));
      bool menu = false;
      if (QMenu* m = clicDroit(base + ptMm(2.54, 0))) menu = cliquerSousLigne(m, QStringLiteral("Décaler les croisements"), QStringLiteral("Sur cette feuille"));
      attendre(120);
      const int quatre1 = compter(4), trois1 = compter(3), pas1 = doc->positionHistorique();
      doc->annuler();
      attendre(120);
      const int quatre2 = compter(4);
      verifier(menu && quatre0 == 6 && quatre1 == 0 && trois1 == 12 && pas1 == pas0 + 1 && quatre2 == 6,
               "SAI-09 : « Décaler les croisements » sur la feuille : six croisements à quatre branches deviennent douze T, en un pas d'annulation",
               std::to_string(quatre0) + " croisements → " + std::to_string(quatre1) + " croisements, " + std::to_string(trois1) + " T ; pas +" + std::to_string(pas1 - pas0) +
                   " ; annulé : " + std::to_string(quatre2));
    }

    // ---------- 2.20 · « Renommer le réseau… » sur toutes les feuilles ; genre changé au menu ; renvois (SAI-31, SAI-32, SAI-11) ----------
    {
      const Pt e0 = ptMm(304.8, 88.9);
      std::size_t f1 = 0, f2 = 0;
      a4->modifierConception([&](ConceptionElec& c) {
        for (int i = 0; i < 2; ++i) {
          Feuille fe;
          fe.uuid = nouvelUuid();
          fe.nom = "I2C " + std::to_string(i + 1);
          c.feuilles.push_back(fe);
        }
        f1 = c.feuilles.size() - 2;
        f2 = c.feuilles.size() - 1;
        for (const std::size_t i : {std::size_t{0}, f1, f2})
          for (int j = 0; j < 2; ++j) {
            const Pt p = e0 + ptMm(0, 10.16 * j);
            // Le fil part vers la gauche : à droite de l'ancre, le clic tombe sur le texte seul
            c.feuilles[i].fils.push_back({nouvelUuid(), p, p - ptMm(10.16, 0)});
            c.feuilles[i].etiquettes.push_back({nouvelUuid(), "SDA", p, 0, TypeEtiquette::Globale});
          }
      }, QStringLiteral("Bus I2C sur trois feuilles"));
      auto noms = [&](const std::string& t) {
        int n = 0;
        for (const auto& fe : conc()->feuilles)
          for (const auto& e : fe.etiquettes) n += e.texte == t;
        return n;
      };
      montrer(e0);
      const int pas0 = doc->positionHistorique();
      bool champ = false;
      if (QMenu* m = clicDroit(e0, QPoint(12, 0)))
        if (QAction* x = ligne(m, QStringLiteral("Renommer le réseau sur toutes les feuilles")); x && cliquerLigne(m, x)) {
          attendre(80);
          if (QLineEdit* e = v4->saisieEnCours()) {
            champ = e->text() == QStringLiteral("SDA");
            reel::touche(e, Qt::Key_A, Qt::ControlModifier);
            reel::taper(e, QStringLiteral("I2C_SDA"));
            reel::touche(e, Qt::Key_Return);
            attendre(120);
          }
        } else if (m)
          m->close();
      const int renommees = noms("I2C_SDA"), restantes = noms("SDA"), pas1 = doc->positionHistorique();
      reel::touche(v4, Qt::Key_Z, Qt::ControlModifier);
      attendre(120);
      verifier(champ && renommees == 6 && restantes == 0 && pas1 == pas0 + 1 && noms("SDA") == 6,
               "SAI-32 : « Renommer le réseau… » SDA en I2C_SDA sur toutes les feuilles : six étiquettes changées, un seul Ctrl+Z les rend",
               std::to_string(renommees) + " renommée(s), " + std::to_string(restantes) + " restante(s), pas +" + std::to_string(pas1 - pas0) + ", après Ctrl+Z " + std::to_string(noms("SDA")));
      // Genre changé au menu : l'étiquette reste la même, devenue connecteur hors feuille ; son renvoi mène aux autres feuilles
      std::string id;
      for (const auto& e : conc()->feuilles[0].etiquettes)
        if (e.texte == "SDA" && e.p == e0) id = e.uuid;
      bool genre = false;
      if (QMenu* m = clicDroit(e0, QPoint(12, 0))) genre = cliquerSousLigne(m, QStringLiteral("Genre"), QStringLiteral("Connecteur hors feuille"));
      attendre(80);
      bool horsFeuille = false;
      for (const auto& e : conc()->feuilles[0].etiquettes)
        if (e.uuid == id) horsFeuille = e.type == TypeEtiquette::HorsFeuille;
      v4->repaint();
      const auto zones = v4->zonesRenvoi();
      bool ouvert = false;
      if (!zones.empty()) {
        reel::cliquer(v4, zones.front().first.center().toPoint());
        attendre(200);
        ouvert = v4->feuille() == zones.front().second && v4->feuille() != 0 && v4->selection().size() == 1;
      }
      verifier(genre && horsFeuille && ouvert, "SAI-11, SAI-31 : genre changé au menu sans redessiner ; le connecteur hors feuille montre ses renvois, un clic ouvre la feuille partenaire",
               std::string(genre ? "menu" : "pas de menu") + (horsFeuille ? ", hors feuille" : "") + ", " + std::to_string(zones.size()) + " renvoi(s)" + (ouvert ? ", ouvert" : ""));
      montrer(e0, 0);
    }

    // ---------- 2.21 · « Terminer les broches libres » d'un connecteur ; étiquette au bout d'un fil ouvert (SAI-13) ----------
    {
      const Pt ou = ptMm(355.6, 127);
      std::string j;
      a4->modifierConception([&](ConceptionElec& c) {
        j = poserSymbole(c, 0, "Conn_01x20", ou);
        for (const auto& s : c.feuilles[0].symboles)
          if (s.uuid == j) annoterALaPose(c, s.composant);
        for (const auto& s : c.feuilles[0].symboles)
          if (s.uuid == j) {
            const auto br = brochesPlacees(s, *c.symbole(s.symbole));
            for (std::size_t i = 0; i < 8 && i < br.size(); ++i) c.feuilles[0].fils.push_back({nouvelUuid(), br[i].p, br[i].p + ptMm(-5.08, 0)});
          }
      }, QStringLiteral("Connecteur de vingt broches"));
      Pt centre{};
      for (const auto& s : conc()->feuilles[0].symboles)
        if (s.uuid == j) centre = boiteSymbole(s, *conc()->symbole(s.symbole)).centre();
      montrer(centre);
      const std::size_t etiquettes0 = conc()->feuilles[0].etiquettes.size();
      const int pas0 = doc->positionHistorique();
      bool menu = false;
      if (QMenu* m = clicDroit(centre)) menu = cliquerSousLigne(m, QStringLiteral("Terminer les broches libres"), QStringLiteral("Par une étiquette"));
      attendre(120);
      const std::size_t ajoutees = conc()->feuilles[0].etiquettes.size() - etiquettes0;
      verifier(menu && ajoutees == 12 && doc->positionHistorique() == pas0 + 1,
               "SAI-13 : « Terminer les broches libres » sur un connecteur de vingt broches dont huit reliées : douze étiquettes à leur nom, en un pas",
               std::to_string(ajoutees) + " étiquette(s), pas +" + std::to_string(doc->positionHistorique() - pas0));
      // Réglage du fil : une étiquette posée d'office au bout laissé ouvert, son nom demandé sur place
      const bool avant = VueSchema::etiquetteAuBoutOuvert();
      const Pt a = ptMm(304.8, 152.4), b = a + ptMm(10.16, 0);
      montrer(a);
      v4->setOutil(VueSchema::Outil::Fil);
      attendre(60);
      if (!VueSchema::etiquetteAuBoutOuvert())
        if (QToolButton* x = boutonBandeau(v4, QStringLiteral("etiquette-bout"))) {
          reel::cliquer(x, x->rect().center());
          attendre(60);
        }
      reel::cliquer(v4, ecran(v4, a));
      reel::doubleCliquer(v4, ecran(v4, b));
      attendre(120);
      bool demande = false;
      if (QLineEdit* e = v4->saisieEnCours()) {
        demande = true;
        reel::taper(e, QStringLiteral("TX_OUT"));
        reel::touche(e, Qt::Key_Return);
        attendre(120);
      }
      bool posee = false;
      std::string ailleurs;
      for (const auto& e : conc()->feuilles[0].etiquettes) {
        posee |= e.texte == "TX_OUT" && e.p == b;
        if (e.texte == "TX_OUT" && e.p != b) ailleurs = " · TX_OUT posée en " + std::to_string(mm(e.p.x)) + "," + std::to_string(mm(e.p.y));
      }
      const bool regle = VueSchema::etiquetteAuBoutOuvert();
      VueSchema::setEtiquetteAuBoutOuvert(avant);
      v4->setOutil(VueSchema::Outil::Selection);
      verifier(demande && posee, "SAI-13 : avec le réglage du bandeau, le bout d'un fil laissé ouvert reçoit une étiquette, nommée sur place",
               std::string(demande ? "nom demandé" : "nom non demandé") + (regle ? ", réglage mis" : ", réglage absent") + ailleurs + " · vue " + std::to_string(v4->width()) + "×" +
                   std::to_string(v4->height()) + ", a en " + std::to_string(ecran(v4, a).x()) + "," + std::to_string(ecran(v4, a).y()));
    }

    // ---------- 2.22 · Réglages de l'étiquette avant la pose : nom et pas ; liste de noms collée (SAI-33, SAI-11) ----------
    {
      const Pt a = ptMm(330.2, 177.8);
      montrer(a + ptMm(10.16, 0));
      v4->setOutil(VueSchema::Outil::Etiquette);
      attendre(60);
      reel::deplacer(v4, ecran(v4, a));
      reel::touche(v4, Qt::Key_Tab);
      attendre(80);
      bool champ = false;
      if (QLineEdit* e = v4->saisieEnCours()) {
        champ = true;
        reel::touche(e, Qt::Key_A, Qt::ControlModifier);
        reel::taper(e, QStringLiteral("D0"));
        reel::touche(e, Qt::Key_Return);
        attendre(80);
      }
      if (QToolButton* x = boutonBandeau(v4, QStringLiteral("pas-etiquette")); x && x->menu())
        for (QAction* y : x->menu()->actions())
          if (y->text() == QStringLiteral("+2")) y->trigger();
      attendre(60);
      for (int i = 0; i < 3; ++i) {
        reel::cliquer(v4, ecran(v4, a + ptMm(7.62 * i, 0)));
        attendre(80);
      }
      std::vector<std::string> poses;
      for (const auto& e : conc()->feuilles[0].etiquettes)
        for (int i = 0; i < 3; ++i)
          if (e.p == a + ptMm(7.62 * i, 0)) poses.push_back(e.texte);
      verifier(champ && poses == std::vector<std::string>{"D0", "D2", "D4"}, "SAI-33 : étiquette réglée à « D0 », pas 2, avant la pose : trois clics posent D0, D2 et D4",
               [&] {
                 std::string t;
                 for (const auto& x : poses) t += x + " ";
                 return t;
               }());
      // Une liste de noms collée : un clic par nom
      QGuiApplication::clipboard()->setText(QStringLiteral("SCK\nMOSI\nMISO\n"));
      const Pt b = a + ptMm(0, 10.16);
      reel::deplacer(v4, ecran(v4, b));
      reel::touche(v4, Qt::Key_V, Qt::ControlModifier);
      attendre(60);
      for (int i = 0; i < 3; ++i) {
        reel::cliquer(v4, ecran(v4, b + ptMm(10.16 * i, 0)));
        attendre(80);
      }
      std::vector<std::string> colles;
      for (const auto& e : conc()->feuilles[0].etiquettes)
        for (int i = 0; i < 3; ++i)
          if (e.p == b + ptMm(10.16 * i, 0)) colles.push_back(e.texte);
      reel::touche(v4, Qt::Key_Escape);
      v4->setOutil(VueSchema::Outil::Selection);
      verifier(colles == std::vector<std::string>{"SCK", "MOSI", "MISO"}, "SAI-11 : une liste de noms collée pose une étiquette par nom, d'un clic chacune", {});
    }

    // ---------- 2.23 · Champ bloqué au menu, champs replacés après une rotation (SAI-34) ----------
    {
      const Pt ou = ptMm(304.8, 203.2);
      std::string r;
      a4->modifierConception([&](ConceptionElec& c) {
        r = poserSymbole(c, 0, "R", ou);
        for (const auto& s : c.feuilles[0].symboles)
          if (s.uuid == r) annoterALaPose(c, s.composant);
      }, QStringLiteral("Résistance des champs"));
      montrer(ou);
      v4->repaint();
      bool bloque = false, garde = false, replace = false;
      const QRectF zv = v4->zoneChamp(r, true);
      reel::cliquer(v4, zv.center().toPoint(), Qt::RightButton);
      attendre(150);
      if (QMenu* m = menuOuvert(QStringLiteral("menuContextuel")))
        if (QAction* x = ligne(m, QStringLiteral("Bloquer ce champ")); x && cliquerLigne(m, x)) attendre(80);
      Pt valeur0{};
      for (const auto& s : conc()->feuilles[0].symboles)
        if (s.uuid == r && s.champValeur) {
          bloque = s.champValeur->bloque;
          valeur0 = s.champValeur->decalage;
        }
      v4->selectionner({r});
      reel::touche(v4, Qt::Key_R);
      attendre(120);
      for (const auto& s : conc()->feuilles[0].symboles)
        if (s.uuid == r) {
          garde = s.champValeur && s.champValeur->bloque && s.champValeur->decalage == valeur0;
          const Pt ref = ancrageChamp(s, *conc()->symbole(s.symbole), false);
          replace = s.champReference && ref.x % GRILLE_SCHEMA == 0 && ref.y % GRILLE_SCHEMA == 0 && !boiteSymbole(s, *conc()->symbole(s.symbole)).gonflee(-GRILLE_SCHEMA / 2).contient(ref);
        }
      verifier(bloque && garde && replace, "SAI-34 : la valeur bloquée au menu garde sa place quand le symbole tourne ; le repère se replace à côté du corps, sur la grille",
               std::string(bloque ? "bloquée" : "non bloquée") + (garde ? ", gardée" : "") + (replace ? ", repère replacé" : ""));
    }

    // ---------- 2.23 · « Créer un composant » : broche propagée aux autres unités, nom d'unité montré (SYM-04) ----------
    {
      DialogueComposant dlg(a4);
      SpecSymbole sp;
      sp.nom = "Double ampli";
      sp.prefixe = "U";
      sp.nomsUnites = {"Ampli 1", "Ampli 2"};
      for (const auto& [n, u] : std::vector<std::pair<std::string, int>>{{"1", 1}, {"2", 1}, {"3", 1}, {"7", 2}, {"6", 2}, {"5", 2}, {"8", 0}, {"4", 0}}) {
        SpecBroche b;
        b.numero = n;
        b.nom = "P" + n;
        b.unite = u;
        sp.broches.push_back(b);
      }
      dlg.setSpec(sp);
      auto* table = dlg.findChild<QTableWidget*>();
      bool propage = false, numero = false, nomVu = false;
      if (table && table->rowCount() == 8) {
        table->item(1, 1)->setText(QStringLiteral("IN-"));  // deuxième broche de l'unité A
        auto* type = qobject_cast<QComboBox*>(table->cellWidget(1, 2));
        if (type) type->setCurrentIndex(static_cast<int>(TypeBroche::Entree));
        auto* t4 = qobject_cast<QComboBox*>(table->cellWidget(4, 2));
        propage = table->item(4, 1)->text() == QStringLiteral("IN-") && t4 && t4->currentIndex() == static_cast<int>(TypeBroche::Entree);
        numero = table->item(4, 0)->text() == QStringLiteral("6");
        if (auto* u = qobject_cast<QComboBox*>(table->cellWidget(0, 4))) nomVu = u->currentText() == QStringLiteral("A · Ampli 1");
      }
      const SpecSymbole relu = dlg.spec();
      verifier(propage && numero && nomVu && relu.nomsUnites == sp.nomsUnites,
               "SYM-04 : dans « Créer un composant », une broche changée l'est à la même place dans l'autre unité, numéro gardé, et l'unité porte son nom",
               std::string(propage ? "propagée" : "non propagée") + (numero ? ", numéro gardé" : "") + (nomVu ? ", nom vu" : ""));
    }

    // ---------- 2.24 · Coller en matrice, collage spécial, copier comme texte, tampon (SAI-03, SAI-27) ----------
    {
      const Pt led = ptMm(381, 25.4);
      std::string dl, rl;
      a4->modifierConception([&](ConceptionElec& c) {
        dl = poserSymbole(c, 0, "LED", led);
        rl = poserSymbole(c, 0, "R", led + ptMm(0, 10.16));
        for (const auto& s : c.feuilles[0].symboles)
          if (s.uuid == dl || s.uuid == rl) annoterALaPose(c, s.composant);
      }, QStringLiteral("LED et résistance"));
      montrer(led + ptMm(40, 40));
      v4->selectionner({dl, rl});
      reel::touche(v4, Qt::Key_C, Qt::ControlModifier);
      attendre(60);
      // Copier comme texte : un tableau des repères et valeurs
      v4->copierCommeTexte();
      const QString texte = QGuiApplication::clipboard()->text();
      std::string refLed, refR;
      for (const auto& s : conc()->feuilles[0].symboles)
        if (const ComposantElec* k = conc()->composant(s.composant)) {
          if (s.uuid == dl) refLed = k->reference;
          if (s.uuid == rl) refR = k->reference;
        }
      verifier(texte.contains(QString::fromStdString(refLed) + QLatin1Char('\t')) && texte.contains(QString::fromStdString(refR) + QLatin1Char('\t')),
               "SAI-27 : « Copier comme texte » met au presse-papiers un tableau des repères, valeurs et empreintes", texte.left(80).toStdString());
      v4->selectionner({dl, rl});
      reel::touche(v4, Qt::Key_C, Qt::ControlModifier);
      // La matrice, par sa fenêtre : 4 lignes, 8 colonnes, d'abord en ligne
      const std::size_t composants0 = conc()->composants.size();
      reel::deplacer(v4, ecran(v4, led + ptMm(20.32, 30.48)));
      QTimer::singleShot(300, [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget()); d && d->objectName() == QStringLiteral("dialogueMatrice")) {
          if (auto* x = d->findChild<QSpinBox*>(QStringLiteral("lignes"))) x->setValue(4);
          if (auto* x = d->findChild<QSpinBox*>(QStringLiteral("colonnes"))) x->setValue(8);
          if (auto* x = d->findChild<QDoubleSpinBox*>(QStringLiteral("pasX"))) x->setValue(10.16);
          if (auto* x = d->findChild<QDoubleSpinBox*>(QStringLiteral("pasY"))) x->setValue(20.32);
          d->accept();
        }
      });
      a4->executerAction("edition.collerMatrice");
      attendre(200);
      std::map<int, Pt> leds;
      for (const auto& s : conc()->feuilles[0].symboles)
        if (const ComposantElec* k = conc()->composant(s.composant); k && s.symbole == "LED" && k->reference.size() > 1 && s.uuid != dl)
          if (std::all_of(k->reference.begin() + 1, k->reference.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }))
            leds[std::stoi(k->reference.substr(1))] = s.position;
      const std::size_t nouveaux = conc()->composants.size() - composants0;
      bool enLigne = leds.size() >= 32;
      if (enLigne) {
        auto it = leds.end();
        std::advance(it, -32);
        std::vector<Pt> p;
        for (; it != leds.end(); ++it) p.push_back(it->second);
        for (int i = 1; i < 8; ++i) enLigne &= p[static_cast<std::size_t>(i)].y == p[0].y && p[static_cast<std::size_t>(i)].x > p[static_cast<std::size_t>(i - 1)].x;
        enLigne &= p[8].y > p[0].y;
      }
      verifier(nouveaux == 64 && enLigne, "SAI-03 : « Coller en matrice » 4 × 8 d'une LED et de sa résistance : 64 composants, numérotés d'abord en ligne",
               std::to_string(nouveaux) + " composant(s), " + std::to_string(leds.size()) + " LED numérotées");
      doc->annuler();
      attendre(120);
      // Tampon : le collage qui suit la souris reste armé ; trois clics, trois copies, Échap finit
      const std::size_t avant = conc()->composants.size();
      const Pt t0 = led + ptMm(50.8, 0);
      reel::deplacer(v4, ecran(v4, t0));
      reel::touche(v4, Qt::Key_V, Qt::ControlModifier);
      attendre(80);
      if (QToolButton* x = boutonBandeau(v4, QStringLiteral("tampon"))) {
        reel::cliquer(x, x->rect().center());
        attendre(60);
      }
      std::string suite;
      for (int i = 0; i < 3; ++i) {
        reel::deplacer(v4, ecran(v4, t0 + ptMm(10.16 * i, 0)));
        reel::cliquer(v4, ecran(v4, t0 + ptMm(10.16 * i, 0)));
        attendre(80);
        suite += " " + std::to_string(conc()->composants.size() - avant);
      }
      reel::touche(v4, Qt::Key_Escape);
      attendre(80);
      const std::size_t tampon = conc()->composants.size() - avant;
      suite += " puis Échap " + std::to_string(tampon) + (v4->tampon() ? ", tampon armé" : "");
      v4->setTampon(false);
      verifier(tampon == 6, "SAI-03 : en tampon, chaque clic pose une copie du bloc ; Échap retire celle qui suit la souris", std::to_string(tampon) + " composant(s) collé(s) ·" + suite);
      // Collage spécial, repères gardés : la copie porte le même repère
      v4->selectionner({rl});
      reel::touche(v4, Qt::Key_C, Qt::ControlModifier);
      // Comme depuis le menu au curseur : le bloc suit la souris, un clic le pose
      const Pt libre = led + ptMm(-25.4, 30.48);
      reel::deplacer(v4, ecran(v4, libre));
      const bool special = v4->collageSpecial(ReperesCollage::Gardes);
      attendre(60);
      reel::cliquer(v4, ecran(v4, libre));
      attendre(80);
      int memes = 0;
      for (const auto& k : conc()->composants) memes += k.reference == refR;
      verifier(special && memes == 2, "SAI-27 : « Collage spécial », repères gardés : la copie garde le repère de l'original", std::to_string(memes) + " × " + refR);
      doc->annuler();
      attendre(80);
    }

    // ---------- 2.25 · Aligner, répartir, échanger au menu d'une sélection ; Gomme (SAI-20, SAI-35, SAI-36, SAI-37) ----------
    {
      const double xs[] = {30.48, 38.1, 33.02, 45.72, 35.56}, ys[] = {20.32, 33.02, 50.8, 60.96, 91.44};
      const Pt o = ptMm(406.4, 101.6);
      std::set<std::string> sel;
      a4->modifierConception([&](ConceptionElec& c) {
        for (int i = 0; i < 5; ++i) sel.insert(poserSymbole(c, 0, "R", o + ptMm(xs[i], ys[i]), 1));
        for (const auto& s : c.feuilles[0].symboles)
          if (sel.count(s.uuid)) annoterALaPose(c, s.composant);
      }, QStringLiteral("Cinq résistances"));
      montrer(o + ptMm(38.1, 55.88));
      v4->selectionner(sel);
      const int pas0 = doc->positionHistorique();
      Pt premier{};
      for (const auto& s : conc()->feuilles[0].symboles)
        if (s.uuid == *sel.begin()) premier = boiteSymbole(s, *conc()->symbole(s.symbole)).centre();
      bool gauche = false, repartir = false;
      if (QMenu* m = clicDroit(premier)) gauche = cliquerSousLigne(m, QStringLiteral("Aligner"), QStringLiteral("À gauche"));
      attendre(80);
      v4->selectionner(sel);
      if (QMenu* m = clicDroit(premier)) repartir = cliquerSousLigne(m, QStringLiteral("Aligner"), QStringLiteral("Répartir à la verticale"));
      attendre(80);
      std::vector<Pt> p;
      for (const auto& s : conc()->feuilles[0].symboles)
        if (sel.count(s.uuid)) p.push_back(s.position);
      std::sort(p.begin(), p.end(), [](Pt a, Pt b) { return a.y < b.y; });
      bool alignes = p.size() == 5;
      for (std::size_t i = 1; alignes && i < p.size(); ++i) alignes = p[i].x == p[0].x && p[i].y - p[i - 1].y == p[1].y - p[0].y && p[i].y % GRILLE_SCHEMA == 0;
      verifier(gauche && repartir && alignes && doc->positionHistorique() == pas0 + 2,
               "SAI-35 : cinq résistances alignées à gauche puis réparties, au menu : même abscisse, pas égal sur la grille, un pas chacune", {});
      // Échanger deux résistances
      const std::string x1 = *sel.begin(), x2 = *std::next(sel.begin());
      Pt p1{}, p2{};
      for (const auto& s : conc()->feuilles[0].symboles) {
        if (s.uuid == x1) p1 = s.position;
        if (s.uuid == x2) p2 = s.position;
      }
      v4->selectionner({x1, x2});
      bool echange = false;
      Pt c1{};
      for (const auto& s : conc()->feuilles[0].symboles)
        if (s.uuid == x1) c1 = boiteSymbole(s, *conc()->symbole(s.symbole)).centre();
      if (QMenu* m = clicDroit(c1))
        if (QAction* x = ligne(m, QStringLiteral("Échanger")); x && cliquerLigne(m, x)) echange = true;
        else if (m)
          m->close();
      attendre(80);
      Pt q1{}, q2{};
      for (const auto& s : conc()->feuilles[0].symboles) {
        if (s.uuid == x1) q1 = s.position;
        if (s.uuid == x2) q2 = s.position;
      }
      verifier(echange && q1 == p2 && q2 == p1, "SAI-36 : « Échanger leurs places » au menu de deux symboles choisis", {});
      // Gomme : chaque clic supprime l'objet visé, jusqu'à « Terminer »
      a4->executerAction("schema.gomme");
      attendre(60);
      const bool armee = v4->outil() == VueSchema::Outil::Gomme;
      const std::size_t n0 = conc()->feuilles[0].symboles.size();
      int clics = 0;
      for (const auto& id : {x1, x2}) {
        Pt c{};
        for (const auto& s : conc()->feuilles[0].symboles)
          if (s.uuid == id) c = boiteSymbole(s, *conc()->symbole(s.symbole)).centre();
        reel::deplacer(v4, ecran(v4, c));
        reel::cliquer(v4, ecran(v4, c));
        attendre(80);
        ++clics;
      }
      const std::size_t effaces = n0 - conc()->feuilles[0].symboles.size();
      std::string sousTerminer = " · bouton Terminer absent";
      if (QToolButton* x = boutonBandeau(v4, QStringLiteral("terminer"))) {
        QWidget* w = QApplication::widgetAt(x->mapToGlobal(x->rect().center()));
        sousTerminer = " · sous Terminer : " + (w ? w->metaObject()->className() + std::string(" ") + w->objectName().toStdString() : std::string("rien")) + (x->isVisible() ? "" : ", bouton caché");
        reel::cliquer(x, x->rect().center());
      }
      attendre(60);
      verifier(armee && effaces == 2 && v4->outil() == VueSchema::Outil::Selection, "SAI-37 : la Gomme supprime chaque objet cliqué, jusqu'à « Terminer »",
               std::to_string(effaces) + " supprimé(s) en " + std::to_string(clics) + " clic(s)" + sousTerminer);
    }

    // ---------- 2.26 · Semblables, estompage, polygone, trait, Étendre et Réduire, mémoires (SAI-38, SAI-39, ERG-28, ERG-29) ----------
    {
      // Quatorze condensateurs de 100 nF sur trois feuilles, et trois de 10 µF
      const std::size_t nf = conc()->feuilles.size();
      const Pt rangee = ptMm(431.8, 152.4);
      std::vector<std::string> cent0;  // les cinq de la feuille 0
      std::string dix0;
      a4->modifierConception([&](ConceptionElec& c) {
        auto poser = [&](std::size_t f, Pt p, const char* valeur) {
          const std::string s = poserSymbole(c, f, "C", p);
          for (const auto& x : c.feuilles[f].symboles)
            if (x.uuid == s) {
              annoterALaPose(c, x.composant);
              if (ComposantElec* k = c.composant(x.composant)) k->valeur = valeur;
            }
          return s;
        };
        for (int i = 0; i < 5; ++i) cent0.push_back(poser(0, rangee + ptMm(10.16 * i, 0), "100n"));
        for (int i = 0; i < 5; ++i) poser(nf - 2, ptMm(50.8 + 10.16 * i, 152.4), "100n");
        for (int i = 0; i < 4; ++i) poser(nf - 1, ptMm(50.8 + 10.16 * i, 152.4), "100n");
        for (int i = 0; i < 3; ++i) {
          const std::string s = poser(0, rangee + ptMm(10.16 * i, 25.4), "10u");
          if (i == 0) dix0 = s;
        }
      }, QStringLiteral("Condensateurs de découplage"));
      montrer(rangee + ptMm(20.32, 12.7));
      v4->selectionner({cent0.front()});
      Pt c0{};
      for (const auto& s : conc()->feuilles[0].symboles)
        if (s.uuid == cent0.front()) c0 = boiteSymbole(s, *conc()->symbole(s.symbole)).centre();
      QTimer::singleShot(400, [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget()); d && d->objectName() == QStringLiteral("dialogueSemblables")) d->accept();
      });
      bool menu = false;
      if (QMenu* m = clicDroit(c0))
        if (QAction* x = ligne(m, QStringLiteral("Choisir les semblables")); x && cliquerLigne(m, x)) menu = true;
        else if (m)
          m->close();
      attendre(200);
      SelectionElectronique* sel = a4->selection();
      const std::size_t semblables = sel->composants().size();
      const bool estompe = sel->estompage() == SelectionElectronique::Estompage::Estomper && v4->opaciteObjet(dix0) < 0.9 && v4->opaciteObjet(cent0.back()) == 1.0;
      verifier(menu && semblables == 14 && estompe,
               "SAI-39, ERG-28 : « Choisir les semblables » sur un 100 nF, même valeur, portée projet : les quatorze condensateurs de 100 nF choisis sur trois feuilles, le reste estompé",
               std::to_string(semblables) + " choisi(s)" + (estompe ? ", reste estompé" : ", pas d'estompage"));
      sel->setEstompage(SelectionElectronique::Estompage::Aucun);
      sel->vider();
      // Polygone : quatre sommets autour des cinq 100 nF de la feuille, fermé d'un double-clic
      sel->setForme(SelectionElectronique::Forme::Polygone);
      const Pt p0 = rangee + ptMm(-6.35, -7.62), p1 = rangee + ptMm(46.99, -7.62), p2 = rangee + ptMm(46.99, 7.62), p3 = rangee + ptMm(-6.35, 7.62);
      for (const Pt p : {p0, p1, p2}) {
        reel::cliquer(v4, ecran(v4, p));
        attendre(60);
      }
      reel::doubleCliquer(v4, ecran(v4, p3));
      attendre(120);
      auto caps = [&] {
        int n = 0;
        for (const auto& id : cent0) n += v4->selection().count(id) > 0;
        return n;
      };
      const int parPolygone = caps();
      const std::size_t total = v4->selection().size();
      sel->vider();
      // Trait : une ligne qui traverse les cinq corps
      sel->setForme(SelectionElectronique::Forme::Trait);
      reel::glisser(v4, ecran(v4, rangee + ptMm(-6.35, 0)), ecran(v4, rangee + ptMm(46.99, 0)));
      attendre(120);
      const int parTrait = caps();
      sel->setForme(SelectionElectronique::Forme::Cadre);
      verifier(parPolygone == 5 && parTrait == 5, "SAI-38 : sélection par polygone fermé d'un double-clic, puis par un trait qui touche les objets",
               "polygone " + std::to_string(parPolygone) + " (" + std::to_string(total) + " objets), trait " + std::to_string(parTrait));
      // Étendre et Réduire : une résistance reliée à une étiquette par un fil
      std::string r, fil, etiquette;
      const Pt rp = rangee + ptMm(0, 50.8);
      a4->modifierConception([&](ConceptionElec& c) {
        r = poserSymbole(c, 0, "R", rp);
        for (const auto& s : c.feuilles[0].symboles)
          if (s.uuid == r) {
            annoterALaPose(c, s.composant);
            const auto br = brochesPlacees(s, *c.symbole(s.symbole));
            const auto droite = std::max_element(br.begin(), br.end(), [](const BrochePlacee& a, const BrochePlacee& b) { return a.p.x < b.p.x; });
            fil = nouvelUuid();
            etiquette = nouvelUuid();
            c.feuilles[0].fils.push_back({fil, droite->p, droite->p + ptMm(7.62, 0)});
            c.feuilles[0].etiquettes.push_back({etiquette, "NET_E", droite->p + ptMm(7.62, 0), 0, TypeEtiquette::Locale});
          }
      }, QStringLiteral("Résistance et son étiquette"));
      a4->recalculer();
      v4->selectionner({r});
      const bool etendu = v4->executerAction("selection.etendre") && v4->selection().count(fil) && v4->selection().count(etiquette);
      const bool reduit = v4->executerAction("selection.reduire") && v4->selection() == std::set<std::string>{r};
      verifier(etendu && reduit, "SAI-39 : « Étendre » prend le fil et l'étiquette de la résistance ; « Réduire » revient au palier d'avant", {});
      // Mémoires : la sélection gardée dans la première, rappelée d'un clic
      v4->basculerMemoires();
      attendre(80);
      PanneauMemoires* mem = v4->memoires();
      bool rappel = false;
      if (mem && mem->isVisible()) {
        reel::cliquer(mem->bouton(0), mem->bouton(0)->rect().center());  // vide : garde {R}
        sel->vider();
        reel::cliquer(mem->bouton(0), mem->bouton(0)->rect().center());  // rappelle
        attendre(60);
        rappel = v4->selection() == std::set<std::string>{r};
        v4->basculerMemoires();
      }
      verifier(rappel, "ERG-29 : une mémoire de sélection garde la sélection d'un clic et la rappelle d'un autre", {});
      sel->vider();
      // Au circuit aussi (2.26) : une piste choisie, l'autre voilée par le masque ne se prend plus d'un clic
      {
        const std::string p1 = nouvelUuid(), p2 = nouvelUuid();
        const Pt o = conc()->circuit.contour.origine + ptMm(conc()->circuit.contour.largeur / 2.0e6, conc()->circuit.contour.hauteur / 2.0e6);
        a4->modifierConception([&](ConceptionElec& c) {
          Piste t1;
          t1.uuid = p1;
          t1.reseau = "MASQUE_A";
          t1.a = o + ptMm(-5, -2);
          t1.b = o + ptMm(5, -2);
          Piste t2 = t1;
          t2.uuid = p2;
          t2.reseau = "MASQUE_B";
          t2.a = o + ptMm(-5, 2);
          t2.b = o + ptMm(5, 2);
          c.circuit.pistes.push_back(t1);
          c.circuit.pistes.push_back(t2);
        }, QStringLiteral("Deux pistes"));
        const int ci = a4->conception(), fi0 = a4->vueSchema()->feuille();
        a4->ouvrir(ci, AtelierElectronique::Circuit);
        attendre(250);
        a4->recalculer();
        VueCircuit* vc = a4->vueCircuit();
        vc->setOutil(VueCircuit::Outil::Selection);
        vc->recadrer();
        attendre(120);
        sel->vider();
        const QPoint sur2 = vc->versEcran(o + ptMm(-2.5, 2)).toPoint();  // entre les marques de la DRC (bouts et milieu)
        vc->repaint();
        const QImage imageAvant = vc->grab().toImage();
        const QColor avant = imageAvant.pixelColor(sur2);
        vc->choisir(VueCircuit::Genre::Piste, p1);
        sel->setEstompage(SelectionElectronique::Estompage::Masquer, 0.6);
        vc->repaint();
        const QImage imageApres = vc->grab().toImage();
        const QColor apres = imageApres.pixelColor(sur2);
        if (const QByteArray d = qgetenv("PROMETHEE_CAPTURES_ELECTRONIQUE"); !d.isEmpty()) {
          imageAvant.save(QDir(QString::fromLocal8Bit(d)).filePath(QStringLiteral("estompage-avant.png")));
          imageApres.save(QDir(QString::fromLocal8Bit(d)).filePath(QStringLiteral("estompage-apres.png")));
        }
        const std::string etatMasque = std::string(sel->estompageActif() ? "estompage actif" : "estompage inactif") + ", " + std::to_string(vc->selection().size()) + " choisi(s) dans la vue, piste choisie " +
                                       imageApres.pixelColor(vc->versEcran(o + ptMm(-2.5, -2)).toPoint()).name().toStdString() + ", carte " + imageApres.pixelColor(vc->versEcran(o + ptMm(0, 6)).toPoint()).name().toStdString();
        QWidget* dessous = QApplication::widgetAt(vc->mapToGlobal(sur2));
        const bool masqueAvant = sel->intouchable();
        reel::cliquer(vc, sur2);
        attendre(80);
        const bool garde = vc->selection().size() == 1 && vc->selection().front().id == p1;
        std::string vu = std::to_string(vc->selection().size()) + " choisi(s)";
        for (const auto& x : vc->selection()) vu += " " + std::to_string(static_cast<int>(x.genre)) + (x.id == p1 ? ":p1" : x.id == p2 ? ":p2" : ":autre");
        vu += std::string(masqueAvant ? ", masque" : ", sans masque") + ", sous le clic : " + (dessous ? dessous->metaObject()->className() + std::string(" ") + dessous->objectName().toStdString() : std::string("rien"));
        sel->setEstompage(SelectionElectronique::Estompage::Aucun);
        sel->vider();
        verifier(apres != avant && garde, "ERG-28 : au circuit aussi, ce qui n'est pas choisi s'estompe ; masqué, il ne se prend plus d'un clic",
                 std::string(apres != avant ? "estompé" : "pas estompé") + " (" + avant.name().toStdString() + " → " + apres.name().toStdString() + ")" +
                     (garde ? ", sélection gardée" : ", sélection changée : ") + vu + " · " + etatMasque);
        a4->modifierConception([&](ConceptionElec& c) {
          std::erase_if(c.circuit.pistes, [&](const Piste& t) { return t.uuid == p1 || t.uuid == p2; });
        }, QStringLiteral("Deux pistes retirées"));
        a4->ouvrir(ci, AtelierElectronique::Schema, fi0);
        attendre(150);
      }
    }

    // ---------- 2.27 · Chercher « GND » : le réseau sur trois feuilles, la zone et les vias du circuit (SAI-40, SAI-17) ----------
    {
      a4->ouvrir(0, AtelierElectronique::Schema, 0);
      attendre(200);
      a4->modifierConception([&](ConceptionElec& c) {
        Feuille fe;
        fe.uuid = nouvelUuid();
        fe.nom = "Alimentation";
        c.feuilles.push_back(fe);
        while (c.feuilles.size() < 3) {  // trois feuilles au moins
          Feuille autre;
          autre.uuid = nouvelUuid();
          autre.nom = "Capteurs " + std::to_string(c.feuilles.size());
          c.feuilles.insert(c.feuilles.end() - 1, autre);
        }
        // GND sur la première feuille, l'avant-dernière et la nouvelle : trois feuilles
        for (const std::size_t i : std::set<std::size_t>{0, c.feuilles.size() - 2, c.feuilles.size() - 1}) {
          const std::string g = poserSymbole(c, i, "GND", ptMm(50.8, 50.8));
          for (const auto& s : c.feuilles[i].symboles)
            if (s.uuid == g)
              if (const auto br = brochesPlacees(s, *c.symbole(s.symbole)); !br.empty()) c.feuilles[i].fils.push_back({nouvelUuid(), br[0].p, br[0].p + ptMm(0, -5.08)});
        }
        const Pt o = c.circuit.contour.origine + ptMm(6, 6);
        for (int i = 0; i < 2; ++i) c.circuit.vias.push_back({nouvelUuid(), "GND", o + ptMm(3.0 * i, 0)});
        if (std::none_of(c.circuit.zones.begin(), c.circuit.zones.end(), [](const Zone& z) { return z.reseau == "GND"; })) {
          Zone z;
          z.uuid = nouvelUuid();
          z.reseau = "GND";
          z.contour = {o + ptMm(-4, -4), o + ptMm(14, -4), o + ptMm(14, 10), o + ptMm(-4, 10)};
          c.circuit.zones.push_back(z);
        }
      }, QStringLiteral("GND sur trois feuilles"));
      a4->recalculer();
      a4->executerAction("edition.chercher");
      attendre(150);
      auto* pr = a4->findChild<PanneauRecherche*>();
      std::set<int> feuilles;
      int zones = 0, vias = 0, menes = 0, clics = 0;
      if (pr) {
        if (auto* x = pr->findChild<QCheckBox*>(QStringLiteral("rechercheMotEntier"))) x->setChecked(true);
        pr->setPortee(PanneauRecherche::Portee::Tout);
        pr->champ()->clear();
        reel::taper(pr->champ(), QStringLiteral("GND"));
        attendre(150);
        const std::vector<PanneauRecherche::Trouve> r = pr->resultats();
        for (std::size_t i = 0; i < r.size(); ++i) {
          const auto& x = r[i];
          if (x.genre == PanneauRecherche::Genre::Reseau) feuilles.insert(x.feuille);
          zones += x.genre == PanneauRecherche::Genre::Zone;
          vias += x.genre == PanneauRecherche::Genre::Vias;
          if (x.genre != PanneauRecherche::Genre::Reseau && x.genre != PanneauRecherche::Genre::Zone && x.genre != PanneauRecherche::Genre::Vias) continue;
          // Un clic mène à chacun : la feuille et le réseau surligné, ou le circuit et l'objet choisi
          QTreeWidgetItem* it = pr->liste()->topLevelItem(static_cast<int>(i));
          if (!it) continue;
          pr->liste()->scrollToItem(it);
          attendre(30);
          reel::cliquer(pr->liste()->viewport(), pr->liste()->visualItemRect(it).center());
          attendre(150);
          ++clics;
          if (x.genre == PanneauRecherche::Genre::Reseau) menes += a4->vueSchema()->feuille() == x.feuille && a4->vueSchema()->reseauSurligne() == "GND";
          else if (x.genre == PanneauRecherche::Genre::Zone) menes += a4->selection()->contient(SelectionElectronique::Genre::Zone, x.objet);
          else menes += !a4->selection()->ids(SelectionElectronique::Genre::Via).empty();
        }
      }
      verifier(pr && feuilles.size() == 3 && zones >= 1 && vias >= 1 && menes == clics,
               "SAI-40 : « GND » trouve le réseau sur trois feuilles, la zone et les vias du circuit, et un clic mène à chacun",
               std::to_string(feuilles.size()) + " feuille(s), " + std::to_string(zones) + " zone(s), " + std::to_string(vias) + " groupe(s) de vias ; " + std::to_string(menes) + "/" +
                   std::to_string(clics) + " clic(s) menés");
      // Recherche approchée, puis tous les résultats choisis ensemble
      bool approche = false, boutonVu = false;
      std::size_t choisis = 0, trouvesGnd = 0;
      if (pr) {
        pr->setApproche(true);
        pr->champ()->clear();
        reel::cliquer(pr->champ(), pr->champ()->rect().center());  // le champ repris d'un clic, après les résultats
        reel::taper(pr->champ(), QStringLiteral("GDN"));
        attendre(100);
        for (const auto& x : pr->resultats()) approche |= x.texte == "GND";
        pr->setApproche(false);
        pr->champ()->clear();
        reel::taper(pr->champ(), QStringLiteral("GND"));
        attendre(100);
        trouvesGnd = pr->resultats().size();
        if (auto* b = pr->findChild<QPushButton*>(QStringLiteral("choisirResultats"))) {
          boutonVu = b->isVisible() && b->isEnabled();
          reel::cliquer(b, b->rect().center());
        }
        attendre(100);
        choisis = a4->selection()->taille();
        if (auto* x = pr->findChild<QCheckBox*>(QStringLiteral("rechercheMotEntier"))) x->setChecked(false);
      }
      verifier(approche && choisis > 3, "SAI-40 : la recherche approchée trouve GND pour « GDN » ; « Tout choisir » prend tous les objets trouvés pour l'inspecteur",
               std::to_string(choisis) + " objet(s) choisi(s), " + std::to_string(trouvesGnd) + " résultat(s)" + (boutonVu ? "" : ", bouton caché") + (approche ? "" : ", GDN non trouvé"));
      a4->selection()->vider();
      a4->setMode(AtelierElectronique::Schema);
    }

    if (QDockWidget* dr = a4->panneau(AtelierElectronique::Panneau::Recherche)) dr->hide();  // la vue retrouve sa place
    // ---------- 2.28 · Longueur et angle tapés pendant un fil ; coordonnées relatives ; grilles rapides (ERG-24 à ERG-26) ----------
    {
      VueSchema* v = a4->vueSchema();
      a4->setMode(AtelierElectronique::Schema);  // la recherche a pu mener au circuit
      attendre(100);
      const Pt depart = ptMm(381, 254);
      v->centrerSur(depart + ptMm(0, -6.35));
      attendre(120);
      v->setOutil(VueSchema::Outil::Fil);
      attendre(40);
      // Le bouton de grille du bandeau bascule entre les deux grilles rapides
      const QString grille0 = v->grilleActive().nom;
      QString grille1;
      if (QToolButton* g = boutonBandeau(v, QStringLiteral("grille"))) {
        reel::cliquer(g, g->rect().center());
        attendre(40);
        grille1 = v->grilleActive().nom;
        if (QToolButton* g2 = boutonBandeau(v, QStringLiteral("grille"))) reel::cliquer(g2, g2->rect().center());
        attendre(40);
      }
      verifier(!grille1.isEmpty() && grille1 != grille0 && v->grilleActive().nom == grille0, "ERG-25 : le bouton Grille du bandeau bascule entre les deux grilles rapides",
               grille0.toStdString() + " → " + grille1.toStdString());
      const QPoint ouDepart = ecran(v, depart);
      // Une bulle de message passe peut-être sur ce point : elle s'efface d'elle-même
      for (int i = 0; i < 80 && QApplication::widgetAt(v->mapToGlobal(ouDepart)) != v; ++i) attendre(100);
      const QPointF origineAvant = v->origineVue();
      reel::deplacer(v, ouDepart);
      reel::cliquer(v, ouDepart);
      attendre(60);
      const std::optional<Pt> commence = v->dernierPointTrace();
      QWidget* sousDepart = QApplication::widgetAt(v->mapToGlobal(ouDepart));
      const std::string ouClic = " · clic en " + std::to_string(ouDepart.x()) + "," + std::to_string(ouDepart.y()) + " d'une vue de " + std::to_string(v->width()) + "×" +
                                 std::to_string(v->height()) + (v->isVisible() ? "" : " cachée") + ", origine " + std::to_string(origineAvant.x()) + "," + std::to_string(origineAvant.y()) + " → " +
                                 std::to_string(v->origineVue().x()) + "," + std::to_string(v->origineVue().y()) + ", sous le clic " +
                                 (sousDepart ? sousDepart->metaObject()->className() + std::string(" ") + sousDepart->objectName().toStdString() : std::string("rien"));
      reel::deplacer(v, ecran(v, depart + ptMm(0, -5.08)));  // le curseur au-dessus : l'angle proposé est 90°
      QWidget* popup = QApplication::activePopupWidget();
      QWidget* focus0 = QApplication::focusWidget();
      const std::string avantChiffre = std::string(" · avant le chiffre : outil ") + std::to_string(static_cast<int>(v->outil())) + (commence ? ", fil commencé" : ", fil non commencé") +
                                       ", popup " + (popup ? popup->metaObject()->className() + std::string(" ") + popup->objectName().toStdString() : std::string("aucun")) + ", focus " +
                                       (focus0 ? focus0->metaObject()->className() + std::string(" ") + focus0->objectName().toStdString() : std::string("aucun")) + ", focus de la fenêtre " +
                                       (v->window()->focusWidget() ? v->window()->focusWidget()->metaObject()->className() : "aucun");
      reel::touche(v, Qt::Key_1, Qt::NoModifier, QStringLiteral("1"));
      attendre(80);
      bool champs = false, tab = false;
      std::string apresTab;
      if (QFrame* s = v->saisieTrace()) {
        auto* l = s->findChild<QLineEdit*>(QStringLiteral("saisieLongueur"));
        auto* an = s->findChild<QLineEdit*>(QStringLiteral("saisieAngle"));
        if (l && an) {
          champs = true;
          reel::taper(l, QStringLiteral("2,7"));
          reel::touche(l, Qt::Key_Tab);
          attendre(40);
          tab = an->hasFocus();
          QWidget* fw = QApplication::focusWidget();
          QWidget* active = QApplication::activeWindow();
          apresTab = " · après Tab : longueur « " + l->text().toStdString() + " », focus " + (fw ? fw->metaObject()->className() + std::string(" ") + fw->objectName().toStdString() : std::string("aucun")) +
                     ", fenêtre active " + (active ? active->metaObject()->className() + std::string(" ") + active->objectName().toStdString() : std::string("aucune")) +
                     ", focus de la fenêtre " + (v->window()->focusWidget() ? v->window()->focusWidget()->objectName().toStdString() : std::string("aucun"));
          reel::touche(an, Qt::Key_A, Qt::ControlModifier);
          reel::taper(an, QStringLiteral("90"));
          reel::touche(an, Qt::Key_Return);
          attendre(100);
        }
      }
      const QString etat = a4->boutonCoordonnees() ? a4->boutonCoordonnees()->text() : QString();
      const std::optional<Pt> coude = v->dernierPointTrace();
      reel::touche(v, Qt::Key_Escape);
      attendre(80);
      v->setOutil(VueSchema::Outil::Selection);
      verifier(champs && tab && coude && *coude == depart + ptMm(0, -12.7) && etat.contains(QStringLiteral("12,70 mm à 90°")),
               "ERG-26, ERG-24 : fil commencé, « 12,7 », Tab, « 90 », Entrée : coude posé à 12,7 mm au-dessus du départ, la barre d'état affiche 12,7 mm à 90°",
               etat.toStdString() + (tab ? "" : " · Tab n'a pas mené à l'angle" + apresTab + avantChiffre + ouClic) +
                   (coude ? " · coude " + std::to_string(mm(coude->x)) + "," + std::to_string(mm(coude->y)) : std::string(" · pas de coude")) +
                   (commence ? " · départ " + std::to_string(mm(commence->x)) + "," + std::to_string(mm(commence->y)) : std::string(" · pas de départ")));
      // L'origine relative posée d'un clic : la barre d'état mesure depuis elle
      const bool polaire = VueSchema::coordonneesPolaires();
      VueSchema::setCoordonneesPolaires(true);
      const Pt o = depart + ptMm(25.4, 0);
      v->armerOrigineRelative();
      reel::cliquer(v, ecran(v, o));
      attendre(60);
      reel::deplacer(v, ecran(v, o + ptMm(10.16, 0)));
      attendre(60);
      const QString relatif = a4->boutonCoordonnees() ? a4->boutonCoordonnees()->text() : QString();
      v->effacerOrigineRelative();
      VueSchema::setCoordonneesPolaires(polaire);
      verifier(v->origineRelative() == std::nullopt && relatif.contains(QStringLiteral("10,16 mm à 0°")), "ERG-24 : l'origine relative posée d'un clic ; la barre d'état mesure depuis elle",
               relatif.toStdString());
    }

    // ---------- 2.29 · Cadrages nommés, souris 3D, consultation, plein écran (SAI-25, ERG-30 à ERG-33) ----------
    {
      VueSchema* v = a4->vueSchema();
      const QPointF o0 = v->origineVue();
      const double e0 = v->echelle();
      v->enregistrerCadrage(QStringLiteral("Essai 2.29"));
      v->deplacerVue(o0 + QPointF(300, -200), e0 * 2);
      const bool rappel = v->cadrages().contains(QStringLiteral("Essai 2.29")) && v->rappelerCadrage(QStringLiteral("Essai 2.29")) && std::abs(v->echelle() - e0) < 1e-9 &&
                          (v->origineVue() - o0).manhattanLength() < 1.0;
      verifier(rappel, "SAI-25 : un cadrage enregistré sous un nom se rappelle : même feuille, même centre, même échelle", {});
      // Souris 3D : un mouvement de l'appareil fait défiler la vue montrée
      const QPointF avant = v->origineVue();
      SourisEspace::instance().simuler(1.0, 0, 0, 0, 0, 0);
      attendre(30);
      QWidget* active = QApplication::activeWindow();
      verifier((v->origineVue() - avant).manhattanLength() > 1.0, "ERG-31 : la souris 3D fait défiler la vue montrée",
               SourisEspace::instance().description().toStdString() + " · fenêtre active : " +
                   (active ? active->metaObject()->className() + std::string(" ") + active->objectName().toStdString() : std::string("aucune")));
      // Consultation : rien ne se modifie, jusqu'à ce que le mode soit quitté
      a4->executerAction("vue.consultation");
      const std::string nom0 = doc->projet().electronique[static_cast<std::size_t>(a4->conception())].nom;
      const bool refuse = doc->consultation() && !a4->modifierConception([](ConceptionElec& c) { c.nom += " (modifié)"; }, QStringLiteral("Essai de consultation")) &&
                          doc->projet().electronique[static_cast<std::size_t>(a4->conception())].nom == nom0;
      a4->executerAction("vue.consultation");
      verifier(refuse && !doc->consultation(), "ERG-33 : en consultation, une modification est refusée ; le mode se quitte", {});
      // Plein écran, puis retour
      a4->executerAction("vue.pleinEcran");
      attendre(150);
      const bool plein = (a4->window()->windowState() & Qt::WindowFullScreen) != 0;
      a4->executerAction("vue.pleinEcran");
      attendre(150);
      verifier(plein && !(a4->window()->windowState() & Qt::WindowFullScreen), "ERG-32 : F11 met la fenêtre en plein écran et l'en sort", {});
    }

    // ---------- 2.30 · Description écrite une fois, accessibilité du canevas, vignettes et mosaïque (SAI-19, ERG-34, ERG-37) ----------
    {
      VueSchema* v = a4->vueSchema();
      a4->setMode(AtelierElectronique::Schema);
      const int fi = v->feuille();
      const Pt rp = ptMm(406.4, 254);
      std::string r, ref;
      a4->modifierConception([&](ConceptionElec& c) {
        r = poserSymbole(c, static_cast<std::size_t>(fi), "R", rp);
        const std::string p = poserSymbole(c, static_cast<std::size_t>(fi), "+3V3", rp + ptMm(15.24, -7.62));
        Feuille& fe = c.feuilles[static_cast<std::size_t>(fi)];
        Pt b1{}, b2{}, alim{};
        for (const auto& s : fe.symboles) {
          if (s.uuid == r) {
            annoterALaPose(c, s.composant);
            if (ComposantElec* k = c.composant(s.composant)) {
              k->valeur = "4k7";
              ref = k->reference;
            }
            for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole))) (b.broche->numero == "1" ? b1 : b2) = b.p;
          }
          if (s.uuid == p) alim = brochesPlacees(s, *c.symbole(s.symbole)).front().p;
        }
        fe.fils.push_back({nouvelUuid(), b1, b1 - ptMm(7.62, 0)});
        fe.etiquettes.push_back({nouvelUuid(), "SDA", b1 - ptMm(7.62, 0), 2, TypeEtiquette::Globale});
        fe.fils.push_back({nouvelUuid(), b2, Pt{alim.x, b2.y}});
        fe.fils.push_back({nouvelUuid(), Pt{alim.x, b2.y}, alim});
      }, QStringLiteral("R3 de l'accessibilité"));
      a4->recalculer();
      v->centrerSur(rp);
      v->selectionner({r});
      attendre(60);
      QAccessibleInterface* acc = QAccessible::queryAccessibleInterface(v);
      const QString lu = acc ? acc->text(QAccessible::Description) : QString();
      const QString attendu = QString::fromStdString(ref) + QStringLiteral(", 4,7 kΩ, réseaux SDA et +3V3");
      verifier(lu == attendu, "ERG-34 : la résistance choisie, l'interface d'accessibilité du canevas rend « R3, 4,7 kΩ, réseaux SDA et +3V3 »", lu.toStdString());
      const ConceptionElec& cc = doc->projet().electronique[static_cast<std::size_t>(a4->conception())];
      Pt centreR{};
      for (const auto& s2 : cc.feuilles[static_cast<std::size_t>(fi)].symboles)
        if (s2.uuid == r) centreR = boiteSymbole(s2, *cc.symbole(s2.symbole)).centre();
      const QString bulle = v->texteSurvol(v->versEcran(centreR));
      // Alt+→ : l'objet relié suivant, décrit à son tour
      v->setFocus();
      reel::touche(v, Qt::Key_Right, Qt::AltModifier);
      attendre(60);
      const QString suivant = acc ? acc->text(QAccessible::Description) : QString();
      verifier(bulle.contains(QStringLiteral("4,7 kΩ")) && !suivant.isEmpty() && suivant != lu && v->selection().size() == 1 && !v->selection().count(r),
               "SAI-19 : l'infobulle lit la même description ; Alt+→ passe à l'objet relié suivant, décrit à son tour", suivant.toStdString());
      // Vignettes : dessinée une fois, gardée, refaite quand la feuille change
      Vignettes& vg = Vignettes::instance();
      const QPixmap p1 = vg.vignette(doc, a4->conception(), fi), p2 = vg.vignette(doc, a4->conception(), fi);
      a4->modifierConception([&](ConceptionElec& c) { c.feuilles[static_cast<std::size_t>(fi)].textes.push_back({nouvelUuid(), "Vignette", rp + ptMm(0, 20.32), 1'800'000}); },
                             QStringLiteral("Texte de vignette"));
      const QPixmap p3 = vg.vignette(doc, a4->conception(), fi);
      verifier(!p1.isNull() && p1.cacheKey() == p2.cacheKey() && p3.cacheKey() != p1.cacheKey(), "ERG-37 : la vignette d'une feuille est gardée, puis refaite quand la feuille change", {});
      // La mosaïque montre chaque feuille ; un clic ouvre la sienne
      a4->executerAction("atelier.mosaique");
      attendre(200);
      int vignettes = 0;
      bool ouverte = false;
      for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* d0 = qobject_cast<QDialog*>(w); d0 && d0->objectName() == QStringLiteral("mosaique") && d0->isVisible())
          if (auto* l = d0->findChild<QListWidget*>(QStringLiteral("feuillesMosaique"))) {
            QPointer<QDialog> d(d0);  // la mosaïque se ferme d'elle-même quand une feuille est ouverte
            vignettes = l->count();
            if (vignettes > 1) {
              QListWidgetItem* it = l->item(vignettes - 1);
              reel::cliquer(l->viewport(), l->visualItemRect(it).center());
              attendre(150);
              ouverte = v->feuille() == vignettes - 1;
            }
            if (d && d->isVisible()) d->close();
            break;
          }
      verifier(vignettes == static_cast<int>(doc->projet().electronique[static_cast<std::size_t>(a4->conception())].feuilles.size()) && ouverte,
               "ERG-37 : la mosaïque du projet montre chaque feuille en vignette ; un clic ouvre la sienne", std::to_string(vignettes) + " vignette(s)");
      a4->ouvrir(a4->conception(), AtelierElectronique::Schema, fi);
    }

    // ---------- 2.31 · Palettes vérifiées par simulation des daltonismes, thème en fichier, échelle du canevas (ERG-04, ERG-36) ----------
    {
      Theme& th = Theme::instance();
      const int avant = th.palette();
      const QStringList ps = Theme::palettes();
      int dalto = -1;
      for (int i = 0; i < ps.size(); ++i)
        if (ps[i].contains(QStringLiteral("daltoniens"))) dalto = i;
      double pire = 0;
      if (dalto >= 0) {
        th.choisirPalette(dalto);
        pire = 21;
        const EncresCircuit& e = theme().circuit();
        for (int d = 0; d < 3; ++d) {
          const auto t = static_cast<Theme::Daltonisme>(d);
          for (const QColor& c : {e.cuivreDessus, e.cuivreDessous, e.cuivreInt1, e.cuivreInt2, e.carte}) pire = std::min(pire, contraste(Theme::simuler(e.choix, t), Theme::simuler(c, t)));
        }
      }
      // Thème en fichier : exporté, un jeton changé, relu, le jeton revient
      const QJsonObject o = th.exporterTheme();
      const QString cle = Theme::jetons().front().cle;
      const QColor c0 = th.jeton(cle);
      th.setJeton(cle, QColor(1, 2, 3));
      const bool change = th.jeton(cle) == QColor(1, 2, 3);
      const bool importe = th.importerTheme(o) && th.jeton(cle) == c0;
      th.choisirPalette(avant);
      char d[64];
      std::snprintf(d, sizeof d, "pire contraste %.2f:1", pire);
      verifier(dalto >= 0 && pire >= 3 && change && importe,
               "ERG-36 : la palette pour daltoniens garde la sélection à 3:1 au moins sur chaque cuivre, vue par les trois daltonismes simulés ; un thème exporté se relit à l'identique", d);
      // Échelle des signes du canevas, réglée dans Prométhée : elle vaut tout de suite
      RegistreReglages::instance().ecrire(QStringLiteral("apparence/echelleCanevas"), 1.5);
      const bool echelle = std::abs(Theme::echelleCanevas() - 1.5) < 1e-9;
      RegistreReglages::instance().ecrire(QStringLiteral("apparence/echelleCanevas"), 1.0);
      verifier(echelle && std::abs(Theme::echelleCanevas() - 1.0) < 1e-9, "ERG-04 : l'échelle des signes du canevas se règle dans Prométhée et vaut sans redémarrer", {});
    }

    // ---------- 2.32 · Réglages déclarés : recherche, réglage surligné, palette, export, remise à zéro, relecture, reprise (ERG-38, ERG-39, ERG-16) ----------
    {
      RegistreReglages& rr = RegistreReglages::instance();
      const auto trouves = rr.chercher(QStringLiteral("numerotation"));
      const bool cherche = !trouves.empty() && trouves.front()->cle == QLatin1String("schema/annotationALaPose");
      f.ouvrirReglage(QStringLiteral("schema/annotationALaPose"));
      attendre(200);
      FenetreReglages* fr = f.findChild<FenetreReglages*>();
      QWidget* w = fr ? fr->surligne() : nullptr;
      const bool surligne = w && w->isVisible() && w->property("cleReglage").toString() == QLatin1String("schema/annotationALaPose");
      if (fr) fr->close();
      reel::activer(&f);  // la main revient à la fenêtre, comme d'un clic
      verifier(cherche && surligne, "ERG-38 : « numérotation » trouve « Repère donné à la pose » ; le réglage s'ouvre sur sa page, surligné", {});
      // Ctrl+K : le réglage a sa place dans la palette
      QStringList lignes;
      QTimer::singleShot(400, [&lignes] {
        QDialog* d = nullptr;
        for (QWidget* x : QApplication::topLevelWidgets())
          if (x->isVisible() && x->objectName() == QLatin1String("palette")) d = qobject_cast<QDialog*>(x);
        auto* champ = d ? d->findChild<QLineEdit*>() : nullptr;
        auto* liste = d ? d->findChild<QListWidget*>() : nullptr;
        if (champ && liste) {
          champ->setText(QStringLiteral("enjambement"));
          for (int i = 0; i < liste->count(); ++i) lignes << liste->item(i)->text();
        }
        if (d) d->reject();
      });
      VueSchema* vk = a4->vueSchema();
      vk->setFocus();
      reel::touche(vk, Qt::Key_K, Qt::ControlModifier);
      attendre(150);
      verifier(!lignes.isEmpty() && lignes.front().startsWith(QStringLiteral("Réglage : Saut aux croisements")),
               "ERG-38 : Ctrl+K « enjambement » propose « Réglage : Saut aux croisements de fils »", lignes.join(QStringLiteral(" | ")).toStdString());
      // Un raccourci, le ruban et un jeton du thème changés ; exportés, tout remis à zéro, puis relus : identiques
      RegistreActions& ra = RegistreActions::atelierElectronique();
      ra.setRaccourcis("schema.gomme", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G));
      DescriptionRuban ruban = RubanEnCours::instance().description();
      if (!ruban.onglets.empty() && !ruban.onglets[0].groupes.empty()) ruban.onglets[0].groupes[0].titre += QStringLiteral(" (à moi)");
      RubanEnCours::instance().remplacer(ruban);
      Theme& th = Theme::instance();
      const QString jeton = Theme::jetons().front().cle;
      th.setJeton(jeton, QColor(12, 34, 56));
      auto touches = [&ra] { return ra.action("schema.gomme")->raccourcis; };
      const auto k1 = touches();
      const DescriptionRuban r1 = RubanEnCours::instance().description();
      const QColor c1 = th.jeton(jeton);
      const QString fichier = QDir::temp().filePath(QStringLiteral("promethee-reglages-%1.json").arg(QCoreApplication::applicationPid()));
      const bool exporte = rr.exporterFichier(fichier);
      rr.remettreAZero();
      attendre(60);
      const bool remis = touches() != k1 && !(RubanEnCours::instance().description() == r1) && th.jeton(jeton) != c1;
      const bool relu = rr.relireFichier(fichier);
      attendre(60);
      const bool identiques = touches() == k1 && RubanEnCours::instance().description() == r1 && th.jeton(jeton) == c1;
      ra.setRaccourcis("schema.gomme", ra.action("schema.gomme")->raccourcisOrigine[0], ra.action("schema.gomme")->raccourcisOrigine[1]);
      RubanEnCours::instance().retablir();
      th.setJeton(jeton, QColor());
      QFile::remove(fichier);
      verifier(exporte && remis && relu && identiques, "ERG-39 : réglages exportés, tout remis à zéro, puis relus : raccourcis, ruban et thème identiques",
               std::string(exporte ? "exportés" : "non exportés") + (remis ? ", remis à zéro" : ", pas remis") + (relu ? ", relus" : ", non relus") + (identiques ? ", identiques" : ", différents"));
      // Reprise de la version précédente : une clé renommée suit
      {
        QSettings st;
        const QVariant version = st.value(QStringLiteral("reglages/version"));
        st.setValue(QStringLiteral("reglages/version"), QStringLiteral("0.30"));
        st.setValue(QStringLiteral("schema/annotation"), false);
        st.remove(QStringLiteral("schema/annotationALaPose"));
        st.sync();
        const QString reprise = rr.reprendreVersionPrecedente(QStringLiteral("0.31"));
        QSettings lu;
        const bool suivie = reprise == QLatin1String("0.30") && lu.contains(QStringLiteral("schema/annotationALaPose")) && !lu.value(QStringLiteral("schema/annotationALaPose")).toBool() &&
                            !lu.contains(QStringLiteral("schema/annotation"));
        lu.remove(QStringLiteral("schema/annotationALaPose"));
        if (version.isValid()) lu.setValue(QStringLiteral("reglages/version"), version);
        else lu.remove(QStringLiteral("reglages/version"));
        verifier(suivie, "ERG-16 : au premier lancement d'une version, les réglages de la précédente sont repris, clés renommées comprises", reprise.toStdString());
      }
    }
  }

  // ---------- 2.4 · Survol : contour, infobulle, et une image en moins de 16 ms sur 300 composants (SAI-19, A10) ----------
  {
    prom::Projet p = prom::projetVide();
    p.sansCarte = true;
    p.electronique.push_back(carteDeMesure(300));
    p.nom = p.electronique.front().nom;
    if (!f.isWindow()) {
      prom::Projet vide = prom::projetVide();
      vide.sansCarte = true;
      doc->creer(vide);
      attendre(200);
    }
    doc->creer(p);
    f.montrerElectronique(0, AtelierElectronique::Schema);
    attendre(300);
    AtelierElectronique* a2 = f.atelierElectronique();
    VueSchema* v2 = a2 ? a2->vueSchema() : nullptr;
    if (!v2) {
      verifier(false, "L2 : la carte de mesure s'ouvre", {});
      return;
    }
    a2->recalculer();
    v2->recadrer();
    attendre(150);
    const ConceptionElec& c = doc->projet().electronique.front();
    const SymbolePlace *r1 = nullptr, *r2 = nullptr;
    for (const auto& x : c.feuilles[0].symboles)
      if (x.symbole == "R") {
        if (!r1) r1 = &x;
        else if (!r2) r2 = &x;
      }
    std::vector<double> temps;
    std::string survole;
    QString bulle;
    if (r1 && r2) {
      const Pt c1 = boiteSymbole(*r1, *c.symbole("R")).centre(), c2 = boiteSymbole(*r2, *c.symbole("R")).centre();
      v2->repaint();  // l'image du dessin fixe est faite une fois
      // Le coût d'une image au survol : le déplacement reçu (objet visé, contour mis à jour), puis l'image de la vue
      // peinte. La composition de la fenêtre par le serveur graphique n'est pas comptée : sous Xvfb, sans
      // carte graphique, elle coûte à elle seule une quinzaine de millisecondes, quelle que soit la zone
      QImage image(v2->size() * v2->devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
      image.setDevicePixelRatio(v2->devicePixelRatioF());
      for (int i = 0; i < 12; ++i) {
        reel::deplacer(v2, ecran(v2, i % 2 ? c2 : c1));
        QElapsedTimer t;
        t.start();
        const QPointF ou = v2->versEcran(i % 2 ? c1 : c2);  // l'autre résistance : le survol change
        QMouseEvent mouvement(QEvent::MouseMove, ou, v2->mapToGlobal(ou), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(v2, &mouvement);
        QPainter p(&image);
        v2->render(&p);
        p.end();
        temps.push_back(static_cast<double>(t.nsecsElapsed()) / 1e6);
      }
      survole = v2->objetSurvole();
      bulle = v2->texteSurvol(v2->versEcran(c2));
    }
    std::sort(temps.begin(), temps.end());
    const double median = temps.empty() ? 1e9 : temps[temps.size() / 2];
    const ComposantElec* k2 = r2 ? c.composant(r2->composant) : nullptr;
    verifier(!survole.empty() && k2 && bulle.contains(QString::fromStdString(k2->reference)), "SAI-19, S6 : l'objet survolé prend un contour, l'infobulle dit son repère et sa valeur",
             bulle.toStdString());
    char d[64];
    std::snprintf(d, sizeof d, "%.1f ms par image (médiane)", median);
    verifier(median < 16, "A10 : au survol, une image coûte moins de 16 ms sur une carte de 300 composants", d);
  }

  // ---------- 2.32 · Session rouverte : le projet, l'atelier, la feuille et le cadrage (ERG-35) ----------
  {
    const QString chemin = QDir::temp().filePath(QStringLiteral("promethee-session-%1.prom").arg(QCoreApplication::applicationPid()));
    bool rouverte = false;
    std::string detail;
    try {
      doc->enregistrer(chemin);
      AtelierElectronique* a5 = f.atelierElectronique();
      if (a5) {
        a5->ouvrir(0, AtelierElectronique::Schema, 0);
        attendre(150);
        VueSchema* v5 = a5->vueSchema();
        v5->deplacerVue(v5->origineVue() + QPointF(40, 25), v5->echelle() * 1.5);
        const double e0 = v5->echelle();
        const QPointF o0 = v5->origineVue();
        f.retenirSession();
        prom::Projet vide = prom::projetVide();
        vide.sansCarte = true;
        doc->creer(vide);
        attendre(200);
        const bool ok = f.rouvrirSession();
        attendre(800);
        AtelierElectronique* a6 = f.atelierElectronique();
        VueSchema* v6 = a6 ? a6->vueSchema() : nullptr;
        rouverte = ok && doc->chemin() == chemin && v6 && v6->feuille() == 0 && std::abs(v6->echelle() - e0) < 1e-6 && (v6->origineVue() - o0).manhattanLength() < 1;
        if (v6) detail = "échelle " + std::to_string(v6->echelle()) + " / " + std::to_string(e0);
      }
    } catch (const std::exception& e) {
      detail = e.what();
    }
    verifier(rouverte, "ERG-35 : la session se rouvre : le projet, l'atelier électronique, la feuille et le cadrage", detail);
    QSettings().remove(QStringLiteral("session"));
  }
}
