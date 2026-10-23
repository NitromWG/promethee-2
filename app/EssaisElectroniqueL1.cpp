// Prométhée : essais d'interface du lot L1 de l'audit du 15 octobre 2026 (« Fondations »), sur l'exemple livré et
// avec de vrais événements de souris et de clavier (EvenementsReels) : sélection partagée entre le schéma et le
// circuit, état neutre, cadres dans les deux sens, Ctrl+A, Ctrl+clic, flèches, sélection rendue après une
// annulation (1.5, S5, S23, S24). Appelés à la fin de « Promethee --essais-electronique ».
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <tuple>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QEventLoop>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPointingDevice>
#include <QPushButton>
#include <QFormLayout>
#include <QLabel>
#include <QScrollBar>
#include <QSettings>
#include <QTabBar>
#include <QToolBar>
#include <QTabWidget>
#include <QSet>
#include <QWheelEvent>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include "ApercuArticle.hpp"
#include "AtelierElectronique.hpp"
#include "DialogueVersionPrecedente.hpp"
#include "DispositionsAtelier.hpp"
#include "BandeauOutil.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "DescriptionRuban.hpp"
#include "EditeurRuban.hpp"
#include "FenetreReglages.hpp"
#include "FiltreSelection.hpp"
#include "GestesSelection.hpp"
#include "ChampValeur.hpp"
#include "FenetrePrincipale.hpp"
#include "InspecteurElectronique.hpp"
#include "ListeRaccourcis.hpp"
#include "NavigationVue.hpp"
#include "Navigateur.hpp"
#include "PanneauBibliotheque.hpp"
#include "RegistreActions.hpp"
#include "SelectionElectronique.hpp"
#include "Theme.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"
#include "ZoneMessages.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
// Répond à la prochaine boîte de message par le bouton de ce rôle
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
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
QPoint ecran(VueSchema* v, Pt p) { return v->versEcran(p).toPoint(); }
QPoint ecran(VueCircuit* v, Pt p) { return v->versEcran(p).toPoint(); }
const SymbolePlace* symboleDe(const ConceptionElec& c, const std::string& reference) {
  const ComposantElec* k = c.parReference(reference);
  if (!k) return nullptr;
  for (const auto& s : c.feuilles[0].symboles)
    if (s.composant == k->uuid) return &s;
  return nullptr;
}
Boite boiteDe(const ConceptionElec& c, const SymbolePlace& s) {
  const DefSymbole* d = c.symbole(s.symbole);
  return d ? boiteSymbole(s, *d) : Boite{s.position.x, s.position.y, s.position.x, s.position.y};
}
// Un fil dont le milieu est loin des symboles, des étiquettes et des autres fils : un clic y vise ce fil seul
const Fil* filIsole(const ConceptionElec& c) {
  const Feuille& f = c.feuilles[0];
  for (const auto& w : f.fils) {
    const Pt m{(w.a.x + w.b.x) / 2, (w.a.y + w.b.y) / 2};
    if (distance(w.a, w.b) < 6e6) continue;
    bool libre = true;
    for (const auto& s : f.symboles) libre &= !boiteDe(c, s).gonflee(1'500'000).contient(m);
    for (const auto& e : f.etiquettes) libre &= distance(e.p, m) > 6e6;
    for (const auto& t : f.textes) libre &= distance(t.p, m) > 15e6;
    for (const auto& j : f.jonctions) libre &= distance(j.p, m) > 2e6;
    if (libre) return &w;
  }
  return nullptr;
}
// Article de la bibliothèque du panneau, trouvé par son nom : son arbre et son élément
std::pair<QTreeWidget*, QTreeWidgetItem*> article(AtelierElectronique* a, const QString& nom) {
  for (QTreeWidget* t : a->findChildren<QTreeWidget*>())
    if (t->isVisible() && !qobject_cast<Navigateur*>(t))
      for (QTreeWidgetItemIterator it(t); *it; ++it)
        if ((*it)->text(0) == nom) return {t, *it};
  return {nullptr, nullptr};
}
// Clic sur un bouton du bandeau d'outil de la vue
bool bouton(BandeauOutil* b, const QString& id) {
  QToolButton* x = b ? b->bouton(id) : nullptr;
  if (!x) return false;
  reel::cliquer(x, x->rect().center());
  return true;
}
// Clic sur une ligne d'un menu ouvert (au curseur, par popup) : la ligne est choisie, le menu se referme
bool cliquerLigne(QMenu* m, QAction* x) {
  if (!m || !x || !m->isVisible()) return false;
  reel::cliquer(m, m->actionGeometry(x).center());
  attendre(60);
  return true;
}
// Une ligne du menu d'un bouton : un clic sur sa flèche ouvre le menu (dans sa propre boucle), la ligne y est cliquée
bool choisirDansMenuBouton(QToolButton* b, QAction* x) {
  if (!b || !b->menu() || !x) return false;
  QMenu* m = b->menu();
  bool fait = false;
  QTimer::singleShot(250, [garde = QPointer<QMenu>(m), x, &fait] { fait = garde && cliquerLigne(garde, x); });
  const bool fleche = b->popupMode() == QToolButton::MenuButtonPopup;
  const QPointer<QMenu> garde(m);
  reel::cliquer(b, fleche ? QPoint(b->width() - 5, b->height() / 2) : b->rect().center());
  attendre(300);
  if (garde && garde->isVisible()) garde->close();
  return fait;
}
// Clic sur un élément de l'arbre : sa branche dépliée (l'arbre est animé : on attend la fin du dépliage), amené dans
// la vue, puis cliqué
void cliquerElementArbre(QTreeWidget* t, QTreeWidgetItem* it, Qt::MouseButton bouton = Qt::LeftButton) {
  bool deplie = false;
  for (QTreeWidgetItem* p = it->parent(); p; p = p->parent())
    if (!p->isExpanded()) {
      p->setExpanded(true);
      deplie = true;
    }
  if (deplie) attendre(400);
  for (int i = 0; i < 4; ++i) {
    t->scrollToItem(it);
    attendre(40);
    if (t->viewport()->rect().contains(t->visualItemRect(it).center())) break;
  }
  reel::cliquer(t->viewport(), t->visualItemRect(it).center(), bouton);
  attendre(60);
}
// Clic droit sur un élément de l'arbre, puis la ligne « ligne » du menu qui s'ouvre
bool menuElementArbre(QTreeWidget* t, QTreeWidgetItem* it, const QString& ligne) {
  bool fait = false;
  QTimer::singleShot(250, [&fait, ligne] {
    for (QWidget* w : QApplication::topLevelWidgets())
      if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible()) {
        for (QAction* x : m->actions())
          if (x->text() == ligne && x->isEnabled()) {
            fait = cliquerLigne(m, x);
            return;
          }
        m->close();
      }
  });
  cliquerElementArbre(t, it, Qt::RightButton);
  attendre(300);
  return fait;
}
// Un point de la feuille sans rien autour (à gauche du dessin)
Pt pointVide(const ConceptionElec& c) {
  Boite b;
  for (const auto& s : c.feuilles[0].symboles) b.etendre(boiteDe(c, s));
  for (const auto& w : c.feuilles[0].fils) {
    b.etendre(w.a);
    b.etendre(w.b);
  }
  return {b.x0 - 6'000'000, b.centre().y};
}
}  // namespace

void essaisElectroniqueL1(FenetrePrincipale& f, const Verifier& verifier) {
  Document* doc = f.document();
  // Dans la fenêtre à onglets, l'exemple ouvert depuis l'accueil partirait dans un onglet neuf : l'onglet est libéré
  // d'abord, pour que le parcours y reste (le comportement des onglets est essayé par --essais-onglets)
  auto libererOnglet = [&f, doc] {
    if (f.isWindow()) return;
    prom::Projet vide = prom::projetVide();
    vide.sansCarte = true;
    doc->creer(vide);
    attendre(200);
  };
  libererOnglet();
  repondre(QMessageBox::DestructiveRole);
  f.choixAccueilPublic(QStringLiteral("exemple-elec"));
  attendre(900);
  AtelierElectronique* a = f.atelierElectronique();
  const ConceptionElec* c = conception(doc);
  if (!a || !c) {
    verifier(false, "L1 : l'exemple livré s'ouvre pour les essais du lot", {});
    return;
  }
  verifier(reel::activer(&f), "L1 : la fenêtre est active (les raccourcis lui parviennent)", {});
  a->ouvrir(0, AtelierElectronique::Schema);
  attendre(300);
  a->recalculer();
  VueSchema* vs = a->vueSchema();
  VueCircuit* vc = a->vueCircuit();
  SelectionElectronique* sel = a->selection();
  vs->recadrer();
  attendre(200);
  const QPoint pointeurAvant = reel::pointeur();

  // ---------- 1.5 · Sélection partagée, état neutre (S5, S23, S24) ----------
  const SymbolePlace* r1 = symboleDe(*c, "R1");
  const SymbolePlace* c1 = symboleDe(*c, "C1");
  const Fil* fil = filIsole(*c);
  if (!r1 || !c1 || !fil) {
    verifier(false, "L1 : l'exemple a R1, C1 et un fil isolé", {});
    return;
  }
  const std::string idR1 = r1->uuid, idC1 = c1->uuid, idFil = fil->uuid, compR1 = r1->composant;
  const Pt centreR1 = boiteDe(*c, *r1).centre(), centreC1 = boiteDe(*c, *c1).centre();
  const Pt milieuFil{(fil->a.x + fil->b.x) / 2, (fil->a.y + fil->b.y) / 2};
  const Pt vide = pointVide(*c);
  verifier(vs->rect().contains(ecran(vs, vide)) && vs->rect().contains(ecran(vs, centreR1)), "L1 : les points visés sont à l'écran", {});

  reel::cliquer(vs, ecran(vs, centreR1));
  verifier(vs->selection().size() == 1 && vs->selection().count(idR1) && sel->composantChoisi(compR1), "un clic sur R1 le choisit au schéma",
           std::to_string(vs->selection().size()) + " objet(s)");
  bool auCircuit = false;
  for (const auto& o : vc->selection()) auCircuit |= o.genre == VueCircuit::Genre::Empreinte && o.id == compR1;
  verifier(auCircuit, "R1 choisi au schéma l'est aussi sur le circuit (1.5)", {});

  reel::cliquer(vs, ecran(vs, milieuFil));
  verifier(vs->selection().size() == 1 && vs->selection().count(idFil) && !sel->reseau().empty(), "un clic sur un fil le choisit et surligne son réseau", sel->reseau());
  const std::string reseauFil = sel->reseau();
  reel::cliquer(vs, ecran(vs, vide));
  verifier(sel->vide() && sel->reseau().empty() && vs->selection().empty() && vc->selection().empty(), "S5 : un clic dans le vide ne laisse rien de choisi ni de surligné",
           std::to_string(sel->taille()) + " objet(s), réseau « " + sel->reseau() + " »");

  reel::cliquer(vs, ecran(vs, milieuFil));
  reel::touche(vs, Qt::Key_Escape);
  verifier(sel->vide() && sel->reseau().empty(), "S5 : Échap au schéma efface aussi le surlignage", sel->reseau());

  reel::touche(vs, Qt::Key_A, Qt::ControlModifier);
  const std::size_t total = c->feuilles[0].symboles.size() + c->feuilles[0].fils.size() + c->feuilles[0].etiquettes.size() + c->feuilles[0].textes.size() +
                            c->feuilles[0].jonctions.size() + c->feuilles[0].nonConnexions.size();
  verifier(vs->selection().size() == total, "S23 : Ctrl+A choisit tout ce qui est sur la feuille", std::to_string(vs->selection().size()) + " / " + std::to_string(total));
  reel::touche(vs, Qt::Key_Escape);

  // Ctrl+clic bascule, Maj+clic ajoute, Ctrl+Maj+clic retire, comme au circuit (S23)
  reel::cliquer(vs, ecran(vs, centreR1));
  reel::cliquer(vs, ecran(vs, centreC1), Qt::LeftButton, Qt::ControlModifier);
  const bool deux = vs->selection().count(idR1) && vs->selection().count(idC1);
  reel::cliquer(vs, ecran(vs, centreR1), Qt::LeftButton, Qt::ControlModifier);
  const bool bascule = !vs->selection().count(idR1) && vs->selection().count(idC1);
  reel::cliquer(vs, ecran(vs, centreR1), Qt::LeftButton, Qt::ShiftModifier);
  const bool ajoute = vs->selection().count(idR1) && vs->selection().count(idC1);
  reel::cliquer(vs, ecran(vs, centreC1), Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier);
  const bool retire = vs->selection().count(idR1) && !vs->selection().count(idC1);
  verifier(deux && bascule && ajoute && retire, "S23 : Ctrl+clic bascule, Maj+clic ajoute, Ctrl+Maj+clic retire, au schéma", {});

  // Cadres : de gauche à droite, les objets entièrement dedans ; de droite à gauche, ceux qu'il touche
  {
    reel::touche(vs, Qt::Key_Escape);
    const Boite b = boiteDe(*c, *r1).gonflee(800'000);
    const QPoint hautGauche = ecran(vs, {b.x0, b.y0}), basDroite = ecran(vs, {b.x1, b.y1});
    reel::glisser(vs, hautGauche, basDroite);
    const std::set<std::string> dedans = vs->selection();
    reel::touche(vs, Qt::Key_Escape);
    reel::glisser(vs, basDroite, hautGauche);
    const std::set<std::string> touches = vs->selection();
    reel::touche(vs, Qt::Key_Escape);
    std::size_t filsDedans = 0, filsTouches = 0;
    for (const auto& w : c->feuilles[0].fils) {
      filsDedans += dedans.count(w.uuid);
      filsTouches += touches.count(w.uuid);
    }
    verifier(dedans.count(idR1) && filsDedans == 0 && touches.count(idR1) && filsTouches > 0,
             "S23 : cadre de gauche à droite, objets contenus ; de droite à gauche, objets touchés (fils de R1 compris)",
             std::to_string(filsDedans) + " fil(s) dedans, " + std::to_string(filsTouches) + " touché(s)");
  }

  // Flèches : la sélection avance d'un pas de grille ; trois appuis rapprochés font un seul pas d'annulation
  {
    reel::cliquer(vs, ecran(vs, centreR1));
    const int pas = static_cast<int>(doc->historique().size());
    const Pt avant = symboleDe(*conception(doc), "R1")->position;
    for (int i = 0; i < 3; ++i) reel::touche(vs, Qt::Key_Right);
    attendre(100);
    const Pt apres = symboleDe(*conception(doc), "R1")->position;
    verifier(apres.x - avant.x == 3 * GRILLE_SCHEMA && apres.y == avant.y && static_cast<int>(doc->historique().size()) == pas + 1,
             "S23, C18 : trois flèches déplacent R1 de trois pas de grille, en un seul pas d'annulation",
             std::to_string(mm(apres.x - avant.x)) + " mm, " + std::to_string(doc->historique().size() - static_cast<std::size_t>(pas)) + " pas");
    reel::touche(vs, Qt::Key_Escape);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(100);
    verifier(symboleDe(*conception(doc), "R1")->position == avant && vs->selection().count(idR1),
             "S24 : Ctrl+Z ramène R1 et la sélection d'avant la commande", {});
  }

  // Suppression puis annulation : R1 revient, choisi ; rien d'autre ne reste choisi
  {
    reel::cliquer(vs, ecran(vs, centreR1));
    reel::touche(vs, Qt::Key_Delete);
    attendre(100);
    // R1 est routé : son symbole part, son empreinte attend au panneau des écarts (3.6), le composant reste derrière elle
    const bool parti = symboleDe(*conception(doc), "R1") == nullptr && sel->vide();
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(150);
    verifier(parti && symboleDe(*conception(doc), "R1") && vs->selection().size() == 1 && vs->selection().count(idR1),
             "S24 : R1 supprimé puis rendu par Ctrl+Z revient choisi, sans sélection fantôme", {});
  }

  // Du circuit vers le schéma : l'empreinte choisie choisit ses symboles
  {
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(300);
    vc->recadrer();
    attendre(150);
    reel::touche(vc, Qt::Key_Escape);
    Pt pos{};
    for (const auto& e : conception(doc)->circuit.empreintes)
      if (e.composant == compR1) pos = e.position;
    reel::cliquer(vc, ecran(vc, pos));
    verifier(vc->genreChoisi() == VueCircuit::Genre::Empreinte && vc->choisi() == compR1 && vs->selection().count(idR1),
             "1.5 : R1 choisi sur le circuit est choisi au schéma", vc->choisi());
    reel::touche(vc, Qt::Key_Escape);
    verifier(sel->vide() && sel->reseau().empty(), "S5 : Échap au circuit, état neutre", {});
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(200);
  }
  // ---------- 1.6 · Outils : pose à un coup, fantôme tourné à la souris, pose en série (S2, S4, S11, SAI-02, SAI-04) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
    reel::touche(vs, Qt::Key_Escape);
    const std::size_t avant = conception(doc)->feuilles[0].symboles.size();
    a->montrerBibliotheque();
    attendre(200);
    auto [arbre, resistance] = article(a, QStringLiteral("Résistance"));
    if (arbre && resistance) {
      arbre->scrollToItem(resistance);
      reel::doubleCliquer(arbre->viewport(), arbre->visualItemRect(resistance).center());
    }
    attendre(100);
    const bool arme = vs->outil() == VueSchema::Outil::Placer && vs->bandeau()->isVisible() && vs->bandeau()->texte().startsWith(QStringLiteral("Pose"));
    // Tourner à la souris, par le bouton du bandeau, puis poser d'un clic : aucune touche
    const bool tourne = bouton(vs->bandeau(), QStringLiteral("tourner"));
    const Pt ici = vide + Pt{0, -12'700'000};
    reel::deplacer(vs, ecran(vs, ici));
    reel::cliquer(vs, ecran(vs, ici));
    attendre(150);
    const ConceptionElec* c2 = conception(doc);
    const SymbolePlace* pose = c2->feuilles[0].symboles.size() == avant + 1 ? &c2->feuilles[0].symboles.back() : nullptr;
    std::string idPose = pose ? pose->uuid : std::string();
    for (const auto& x : c2->feuilles[0].symboles)
      if (x.position == surGrille(ici)) idPose = x.uuid;
    const SymbolePlace* pose2 = nullptr;
    for (const auto& x : c2->feuilles[0].symboles)
      if (x.uuid == idPose) pose2 = &x;
    verifier(arme && tourne && pose2 && pose2->rotation == 1, "1.6 : un composant armé dans la bibliothèque, tourné par le bandeau et posé d'un clic, sans aucune touche",
             pose2 ? "rotation " + std::to_string(pose2->rotation) : "rien de posé");
    verifier(vs->outil() == VueSchema::Outil::Selection && vs->selection().size() == 1 && vs->selection().count(idPose),
             "S2, S4 : après la pose, l'outil Sélection reprend la main, le composant posé choisi", std::to_string(vs->selection().size()) + " objet(s)");
    bool valeurMontree = false;
    for (QLineEdit* e : a->findChildren<QLineEdit*>()) valeurMontree |= e->isVisible() && e->text() == QStringLiteral("10k");
    verifier(valeurMontree, "S4 : le panneau montre aussitôt le composant posé (valeur 10k)", {});
    reel::cliquer(vs, ecran(vs, vide));
    attendre(100);
    verifier(conception(doc)->feuilles[0].symboles.size() == avant + 1 && sel->vide(), "S2 : un clic dans le vide après la pose ne pose rien et ne laisse rien de choisi",
             std::to_string(conception(doc)->feuilles[0].symboles.size() - avant) + " posé(s)");

    // S11 : pendant une pose, X retourne le fantôme, pas l'ancienne sélection
    reel::cliquer(vs, ecran(vs, centreR1));
    const bool miroirAvant = symboleDe(*conception(doc), "R1")->miroir;
    // La bibliothèque est restée ouverte à gauche pendant que l'inspecteur montrait R1 (B5, 1.10)
    a->montrerBibliotheque();
    attendre(200);
    std::tie(arbre, resistance) = article(a, QStringLiteral("Résistance"));
    if (arbre && resistance) reel::doubleCliquer(arbre->viewport(), arbre->visualItemRect(resistance).center());
    attendre(100);
    reel::deplacer(vs, ecran(vs, vide));
    reel::touche(vs, Qt::Key_X);
    verifier(symboleDe(*conception(doc), "R1")->miroir == miroirAvant && sel->vide(), "S11 : armer la pose vide la sélection ; X retourne le fantôme, pas R1", {});

    // Pose en série par le bouton du bandeau : trois clics, trois composants, « Terminer » rend la main
    const bool serie = bouton(vs->bandeau(), QStringLiteral("serie"));
    const std::size_t avantSerie = conception(doc)->feuilles[0].symboles.size();
    std::string sousSerie;
    // Trois places hors des messages empilés en bas à gauche du canevas (un clic sur une bulle ne pose rien)
    Pt base = vide;
    auto sousMessages = [&](Pt b0) {
      for (int i = 0; i < 3; ++i)
        if (QWidget* hote = a->pileMessages()->parentWidget()) {
          // Le bas gauche que la pile prend, visible ou non : trois bulles au plus, 460 pixels de large au plus
          const int largeur = std::max(160, std::min(460, hote->width() - 24)) + 24;
          const QRect zone(hote->mapToGlobal(QPoint(0, hote->height() - 34 - 3 * 80)), QSize(largeur, 34 + 3 * 80));
          if (zone.contains(vs->mapToGlobal(ecran(vs, b0 + Pt{0, 12'700'000 + i * 10'160'000})))) return true;
        }
      return false;
    };
    for (int k = 0; k < 12 && sousMessages(base); ++k) base = base + Pt{25'400'000, 0};
    for (int i = 0; i < 3; ++i) {
      const QPoint p = ecran(vs, base + Pt{0, 12'700'000 + i * 10'160'000});
      reel::deplacer(vs, p);
      QWidget* w = QApplication::widgetAt(vs->mapToGlobal(p));
      sousSerie += " " + std::to_string(p.x()) + "," + std::to_string(p.y()) + ":" + (w ? w->metaObject()->className() : "rien");
      reel::cliquer(vs, p);
      sousSerie += "→" + std::to_string(conception(doc)->feuilles[0].symboles.size() - avantSerie) + (vs->outil() == VueSchema::Outil::Placer ? "P" : "S");
    }
    attendre(100);
    const std::size_t poses = conception(doc)->feuilles[0].symboles.size() - avantSerie;
    const bool encoreArme = vs->outil() == VueSchema::Outil::Placer;
    const bool fini = bouton(vs->bandeau(), QStringLiteral("terminer")) && vs->outil() == VueSchema::Outil::Selection;
    verifier(serie && poses == 3 && encoreArme && fini && !vs->bandeau()->isVisible(), "1.6 : « Poser en série » garde l'outil, un clic par exemplaire, « Terminer » le rend",
             std::to_string(poses) + " posé(s) ·" + sousSerie);
  }
  // ---------- 1.6 · Outils du circuit : Échap garde ce qui est fixé, Ctrl+Z retire le tracé entier (C14) ----------
  {
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(300);
    a->recalculer();
    vc->recadrer();
    attendre(150);
    reel::touche(vc, Qt::Key_Escape);
    Pt pastille{};
    bool trouvee = false;
    for (const auto& p : a->calculs().pastilles)
      if (!trouvee && p.reference == "R1" && p.numero == "1") {
        pastille = p.pastille.position;
        trouvee = true;
      }
    // Deux points de passage loin de tout cuivre : la piste ne s'y termine pas sur une pastille du même réseau
    auto libre = [&](Pt p) {
      for (const auto& x : a->calculs().pastilles)
        if (dedans(x.forme, p) || distance(x.pastille.position, p) < 2'000'000) return false;
      for (const auto& t : conception(doc)->circuit.pistes)
        if (distancePointSegment(p, t.a, t.b) < 1'200'000) return false;
      for (const auto& v : conception(doc)->circuit.vias)
        if (distance(v.p, p) < 1'500'000) return false;
      return dedans(conception(doc)->circuit.contour.polygone(), p);
    };
    Pt etape1 = pastille, etape2 = pastille;
    bool passages = false;
    for (int n = 1; n <= 6 && !passages; ++n)
      for (const Pt d : {Pt{1, 0}, Pt{-1, 0}, Pt{0, 1}, Pt{0, -1}})
        for (const Pt e : {Pt{d.y, d.x}, Pt{-d.y, -d.x}}) {
          const Pt a1 = pastille + Pt{d.x * n * 1'270'000, d.y * n * 1'270'000};
          const Pt a2 = a1 + Pt{e.x * 2'540'000, e.y * 2'540'000};
          if (!passages && libre(a1) && libre(a2)) {
            etape1 = a1;
            etape2 = a2;
            passages = true;
          }
        }
    trouvee &= passages;
    auto tracer = [&] {
      reel::cliquer(vc, ecran(vc, pastille));
      reel::cliquer(vc, ecran(vc, etape1), Qt::LeftButton, Qt::ControlModifier);  // Ctrl + clic : segment forcé
      reel::cliquer(vc, ecran(vc, etape2), Qt::LeftButton, Qt::ControlModifier);
      reel::deplacer(vc, ecran(vc, etape2 + Pt{2'540'000, 0}));
    };
    reel::touche(vc, Qt::Key_X);  // outil Routage
    const std::size_t pistesAvant = conception(doc)->circuit.pistes.size();
    const int pasAvant = static_cast<int>(doc->historique().size());
    tracer();
    const bool enCours = vc->outil() == VueCircuit::Outil::Router && vc->bandeau()->bouton(QStringLiteral("via")) != nullptr;
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    attendre(80);
    verifier(trouvee && enCours && conception(doc)->circuit.pistes.size() == pistesAvant && static_cast<int>(doc->historique().size()) == pasAvant &&
                 vc->outil() == VueCircuit::Outil::Router,
             "C14 : Ctrl+Z pendant une piste la retire en entier, sans toucher à l'historique", vc->bandeau()->texte().toStdString());
    tracer();
    reel::touche(vc, Qt::Key_Escape);
    attendre(80);
    const std::size_t posees = conception(doc)->circuit.pistes.size() - pistesAvant;
    const bool toujoursArme = vc->outil() == VueCircuit::Outil::Router;
    reel::touche(vc, Qt::Key_Escape);
    verifier(posees >= 2 && static_cast<int>(doc->historique().size()) == pasAvant + 1 && toujoursArme && vc->outil() == VueCircuit::Outil::Selection,
             "C14 : Échap pose les segments fixés et retire celui qui suit la souris ; un second Échap rend l'état neutre",
             std::to_string(posees) + " segment(s)");
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    attendre(150);
    verifier(conception(doc)->circuit.pistes.size() == pistesAvant, "la piste posée s'annule ensuite en un pas", {});

    // C11, ROU-05 : pendant une piste, changer de couche au clavier pose une via au dernier point fixé, et la suite
    // part sur l'autre face ; hors tracé, la même touche ne fait que changer la couche active
    {
      reel::touche(vc, Qt::Key_X);
      const std::size_t viasAvant = conception(doc)->circuit.vias.size(), pistesAvantVia = conception(doc)->circuit.pistes.size();
      reel::cliquer(vc, ecran(vc, pastille));
      reel::cliquer(vc, ecran(vc, etape1), Qt::LeftButton, Qt::ControlModifier);
      const Couche depart = vc->coucheActive();
      reel::touche(vc, depart == Couche::CuivreDessus ? Qt::Key_PageDown : Qt::Key_PageUp);
      const Couche suite = vc->coucheActive();
      reel::cliquer(vc, ecran(vc, etape2), Qt::LeftButton, Qt::ControlModifier);
      reel::touche(vc, Qt::Key_Escape);  // les segments fixés sont posés
      attendre(100);
      const auto& cc = conception(doc)->circuit;
      bool viaAuCoude = false, surLautreFace = false;
      for (const auto& v : cc.vias) viaAuCoude |= distance(v.p, etape1) < 700'000;
      for (std::size_t i = pistesAvantVia; i < cc.pistes.size(); ++i) surLautreFace |= cc.pistes[i].couche == suite && cc.pistes[i].couche != depart;
      verifier(suite != depart && cc.vias.size() == viasAvant + 1 && viaAuCoude && surLautreFace,
               "C11 : Page suiv. pendant le routage pose une via au dernier coude ; la piste continue sur l'autre face",
               std::to_string(cc.vias.size() - viasAvant) + " via(s), couche " + std::to_string(static_cast<int>(suite)));
      reel::touche(vc, Qt::Key_Escape);
      reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
      attendre(150);
      vc->setCoucheActive(Couche::CuivreDessus);
      const std::size_t vias = conception(doc)->circuit.vias.size();
      reel::touche(vc, Qt::Key_PageDown);
      verifier(vc->coucheActive() == Couche::CuivreDessous && conception(doc)->circuit.vias.size() == vias,
               "C11 : hors tracé, Page suiv. change la couche active sans rien poser", {});
      vc->setCoucheActive(Couche::CuivreDessus);
    }

    // Découpe : trois sommets puis Échap la ferme (ce qui est fixé reste) ; Ctrl+Z pendant le tracé le retire
    const Boite carte = boite(conception(doc)->circuit.contour.polygone());
    const Pt c0 = carte.centre();
    const Pt s1 = c0 + Pt{-1'000'000, -1'000'000}, s2 = c0 + Pt{1'000'000, -1'000'000}, s3 = c0 + Pt{0, 1'000'000};
    const std::size_t decoupesAvant = conception(doc)->circuit.contour.decoupes.size();
    vc->setOutil(VueCircuit::Outil::Decoupe);
    for (const Pt& p : {s1, s2, s3}) reel::cliquer(vc, ecran(vc, p));
    const QStringList historique = doc->historique();
    const int position = doc->positionHistorique();
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    const bool retire = conception(doc)->circuit.contour.decoupes.size() == decoupesAvant && doc->historique() == historique && doc->positionHistorique() == position;
    for (const Pt& p : {s1, s2, s3}) reel::cliquer(vc, ecran(vc, p));
    reel::touche(vc, Qt::Key_Escape);
    attendre(80);
    verifier(retire && conception(doc)->circuit.contour.decoupes.size() == decoupesAvant + 1, "C14 : Ctrl+Z retire un contour en cours ; Échap ferme un contour de trois sommets",
             std::to_string(conception(doc)->circuit.contour.decoupes.size() - decoupesAvant) + " découpe(s)");
    reel::touche(vc, Qt::Key_Escape);
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    attendre(150);
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.7 · Échap unique : l'état neutre depuis chaque outil, le focus où qu'il soit (ERG-02, S5, S21) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
    // Un widget de l'atelier hors du canevas, qui prend le focus : une liste du ruban (cherchée à nouveau à chaque
    // vue, le ruban et le panneau se reconstruisant)
    auto trouverAilleurs = [&]() -> QPointer<QWidget> {
      for (QComboBox* x : a->findChildren<QComboBox*>())
        if (x->isVisible() && x->isEnabled() && x->focusPolicy() != Qt::NoFocus) return x;
      return {};
    };
    QPointer<QWidget> ailleurs = trouverAilleurs();
    const std::string reseauExemple = a->calculs().k.reseaux.empty() ? std::string("GND") : a->calculs().k.reseaux.front().nom;
    auto neutre = [&](QWidget* vue, bool schema) {
      const bool outilSelection = schema ? vs->outil() == VueSchema::Outil::Selection : vc->outil() == VueCircuit::Outil::Selection;
      const bool bandeauMasque = schema ? !vs->bandeau()->isVisible() : !vc->bandeau()->isVisible();
      (void)vue;
      return outilSelection && sel->vide() && sel->reseau().empty() && bandeauMasque;
    };
    int neutresSchema = 0, outilsSchema = 0;
    using OS = VueSchema::Outil;
    for (const OS o : {OS::Fil, OS::Etiquette, OS::EtiquetteGlobale, OS::Alimentation, OS::Jonction, OS::NonConnexion, OS::Texte, OS::Placer}) {
      vs->setOutil(o, o == OS::Alimentation ? "GND" : o == OS::Placer ? "R" : "");
      sel->surligner(reseauExemple);
      ailleurs = trouverAilleurs();
      if (ailleurs) ailleurs->setFocus(Qt::OtherFocusReason);
      reel::touche(ailleurs ? ailleurs.data() : static_cast<QWidget*>(vs), Qt::Key_Escape);
      ++outilsSchema;
      neutresSchema += neutre(vs, true);
    }
    verifier(ailleurs && neutresSchema == outilsSchema, "1.7 : au schéma, Échap rend l'état neutre depuis chaque outil, le focus hors du canevas",
             std::to_string(neutresSchema) + " / " + std::to_string(outilsSchema));

    // Un fil commencé : le premier Échap retire ce qui suit la souris, l'outil reste ; le second rend l'état neutre
    vs->setFocus();
    reel::touche(vs, Qt::Key_W);
    reel::cliquer(vs, ecran(vs, vide));
    reel::deplacer(vs, ecran(vs, vide + Pt{5'080'000, 0}));
    reel::touche(vs, Qt::Key_Escape);
    const bool filArme = vs->outil() == OS::Fil;
    reel::touche(vs, Qt::Key_Escape);
    verifier(filArme && neutre(vs, true), "1.7 : un fil commencé, Échap l'arrête, l'outil reste ; Échap encore, l'état neutre", {});

    // Un champ du panneau en cours de saisie : Échap rend d'abord la main au canevas, sans rien changer
    reel::cliquer(vs, ecran(vs, centreR1));
    attendre(100);
    QLineEdit* valeur = nullptr;
    for (QLineEdit* e : a->findChildren<QLineEdit*>())
      if (e->isVisible() && e->text() == QStringLiteral("1k")) valeur = e;
    if (valeur) {
      valeur->setFocus(Qt::OtherFocusReason);
      reel::touche(valeur, Qt::Key_Escape);
    }
    const bool rendu = valeur && vs->hasFocus() && vs->selection().count(idR1);
    reel::touche(vs, Qt::Key_Escape);
    verifier(rendu && neutre(vs, true), "S21 : Échap dans un champ rend la main au canevas, sans perdre la sélection ; le suivant rend l'état neutre", {});

    // Au circuit, chaque outil, le focus hors du canevas
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(200);
    int neutresCircuit = 0, outilsCircuit = 0;
    using OC = VueCircuit::Outil;
    for (const OC o : {OC::Router, OC::Via, OC::Zone, OC::Mesure, OC::Contour, OC::Decoupe, OC::Texte}) {
      vc->setOutil(o);
      sel->surligner(reseauExemple);
      ailleurs = trouverAilleurs();
      if (ailleurs) ailleurs->setFocus(Qt::OtherFocusReason);
      reel::touche(ailleurs ? ailleurs.data() : static_cast<QWidget*>(vc), Qt::Key_Escape);
      ++outilsCircuit;
      neutresCircuit += neutre(vc, false);
    }
    verifier(neutresCircuit == outilsCircuit, "1.7 : au circuit, Échap rend l'état neutre depuis chaque outil", std::to_string(neutresCircuit) + " / " + std::to_string(outilsCircuit));
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
  }

  // ---------- 1.7 · Signes distincts (S5) : la sélection a un fond et des poignées de sa couleur, le surlignage un halo de
  // la sienne, sans cadre ni poignée ; compté dans l'image du canevas, sous le bandeau, la souris hors des objets ----------
  {
    const SignesCanevas& sg = Theme::instance().signesSchema();
    auto compter = [](const QImage& im, const QColor& ref, double ecartTeinte) {
      int n = 0;
      const double t0 = ref.hsvHueF() * 360;
      for (int y = 32; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
          const QColor c = im.pixelColor(x, y);
          if (c.hsvSaturationF() < 0.08 || c.valueF() < 0.3) continue;
          double d = std::abs(c.hsvHueF() * 360 - t0);
          d = std::min(d, 360 - d);
          n += d <= ecartTeinte;
        }
      return n;
    };
    auto image = [&]() {
      reel::deplacer(vs, ecran(vs, vide));
      attendre(80);
      return vs->grab().toImage();
    };
    vs->setFocus();
    reel::touche(vs, Qt::Key_Escape);
    const QImage neutre = image();
    reel::cliquer(vs, ecran(vs, centreR1));
    const QImage choisi = image();
    reel::touche(vs, Qt::Key_Escape);
    sel->surligner(reseauFil);
    const QImage surligne = image();
    reel::touche(vs, Qt::Key_Escape);
    const int s0 = compter(neutre, sg.sel, 12), h0 = compter(neutre, sg.surl, 20);
    const int s1 = compter(choisi, sg.sel, 12), h1 = compter(choisi, sg.surl, 20);
    const int s2 = compter(surligne, sg.sel, 12), h2 = compter(surligne, sg.surl, 20);
    verifier(s1 > s0 + 30 && h1 <= h0 + 5, "S5 : R1 choisi se voit à la couleur de sélection, sans halo de surlignage",
             std::to_string(s0) + " → " + std::to_string(s1) + " px, halo " + std::to_string(h0) + " → " + std::to_string(h1));
    verifier(h2 > h0 + 100 && s2 <= s0 + 5, "S5 : un réseau surligné se voit à son halo, sans cadre ni poignée de sélection",
             std::to_string(h0) + " → " + std::to_string(h2) + " px, sélection " + std::to_string(s0) + " → " + std::to_string(s2));
  }

  // ---------- 1.8 · Le canevas ne garde que ses touches : F8 et F1 vont à l'atelier ; focus visible et pris à
  // l'entrée de la souris ; Suppr et R hors du canevas ; F au circuit seulement ; flèches fusionnées (S21, S22, C13,
  // C18, ERG-03) ----------
  {
    const RegistreActions& registre = RegistreActions::atelierElectronique();
    verifier(registre.conflits().isEmpty(), "ERG-03 : le registre des actions n'a aucune touche en conflit", registre.conflits().join(QStringLiteral(" ; ")).toStdString());
    // Messages de l'atelier depuis le dernier effacement (le bilan du calcul suit celui de l'action)
    QString dernierMessage;
    const auto lien = QObject::connect(a, &AtelierElectronique::messageChange, [&dernierMessage](const QString& t) { dernierMessage += t + QChar('\n'); });
    // Un widget de l'atelier, hors du canevas, qui prend le focus sans être un champ de saisie : la barre d'onglets
    // du ruban
    auto trouverAilleurs = [&]() -> QPointer<QWidget> {
      for (QTabBar* x : a->findChildren<QTabBar*>())
        if (x->isVisible() && x->isEnabled() && x->focusPolicy() != Qt::NoFocus) return x;
      return {};
    };

    // S22 : F8 marche quand le schéma, puis le circuit, a le focus
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
    const int positionAvantF8 = doc->positionHistorique();
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_F8);
    attendre(200);
    verifier(a->mode() == AtelierElectronique::Circuit && dernierMessage.contains(QStringLiteral("Circuit mis à jour")),
             "S22 : F8 marche quand le schéma a le focus (le circuit est mis à jour)", dernierMessage.left(60).toStdString());
    dernierMessage.clear();
    vc->setFocus(Qt::OtherFocusReason);
    reel::touche(vc, Qt::Key_F8);
    attendre(200);
    verifier(dernierMessage.contains(QStringLiteral("Circuit mis à jour")), "S22 : F8 marche aussi quand le circuit a le focus", {});
    // Ce que F8 a fait (annotation, empreintes des composants posés plus haut) est défait
    for (int i = 0; i < 4 && doc->positionHistorique() > positionAvantF8; ++i) {
      reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
      attendre(100);
    }

    // F1 : la liste des raccourcis, l'outil en cours en tête
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_W);
    reel::touche(vs, Qt::Key_F1);
    attendre(150);
    ListeRaccourcis* liste = a->listeRaccourcis();
    const QStringList outilFil = liste ? liste->lignes(QStringLiteral("Outil en cours · Fil")) : QStringList();
    const QStringList atelierListe = liste ? liste->lignes(QStringLiteral("Atelier")) : QStringList();
    verifier(liste && liste->isVisible() && outilFil.contains(QStringLiteral("Inverser le coude du fil ou de la piste · /, Espace")) &&
                 atelierListe.join(QChar('\n')).contains(QStringLiteral("F8")),
             "ERG-03 : F1 montre la liste, l'outil en cours (Fil) en tête et F8 dans celle de l'atelier", outilFil.join(QStringLiteral(" | ")).toStdString());
    if (liste) liste->hide();
    reel::activer(&f);
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Escape);
    reel::touche(vs, Qt::Key_Escape);

    // Focus visible : le canevas qui a le focus porte le lisere d'accent, plus quand il l'a perdu
    QPointer<QWidget> ailleurs = trouverAilleurs();
    auto lisere = [&]() {
      const QImage im = vs->grab().toImage();
      const QColor c = im.pixelColor(1, im.height() / 2), attendu = Theme::instance().c().accentTexte;
      return std::abs(c.red() - attendu.red()) + std::abs(c.green() - attendu.green()) + std::abs(c.blue() - attendu.blue()) < 40;
    };
    vs->setFocus(Qt::OtherFocusReason);
    attendre(50);
    const bool avecFocus = lisere();
    if (ailleurs) ailleurs->setFocus(Qt::OtherFocusReason);
    attendre(50);
    verifier(ailleurs && avecFocus && !lisere(), "S21 : le canevas qui a le focus porte un liseré d'accent, perdu avec le focus", {});

    // La souris qui entre dans le canevas lui donne le focus, sauf pendant une saisie
    if (ailleurs) reel::deplacer(ailleurs, ailleurs->rect().center());
    const QWidget* focusAvant = QApplication::focusWidget();
    // Un point du canevas lui-même : une bulle de message posée sur le canevas n'est pas le canevas
    QPoint pointVide = ecran(vs, vide);
    if (QApplication::widgetAt(vs->mapToGlobal(pointVide)) != vs)
      for (const QPoint p : {QPoint(vs->width() - 40, vs->height() / 2), QPoint(vs->width() / 2, vs->height() / 3), QPoint(60, vs->height() / 2)})
        if (QApplication::widgetAt(vs->mapToGlobal(p)) == vs) {
          pointVide = p;
          break;
        }
    reel::deplacer(vs, pointVide);
    const bool prisALEntree = vs->hasFocus();
    const QWidget* sous = QApplication::widgetAt(vs->mapToGlobal(pointVide));
    const std::string entree = std::string(" (avant : ") + (focusAvant ? focusAvant->metaObject()->className() : "—") + ", sous la souris : " + (sous ? sous->metaObject()->className() + std::string(" ") + sous->objectName().toStdString() : std::string("—")) + ")";
    reel::cliquer(vs, ecran(vs, centreR1));
    attendre(100);
    QLineEdit* valeur = nullptr;
    for (QLineEdit* e : a->findChildren<QLineEdit*>())
      if (e->isVisible() && e->text() == QStringLiteral("1k")) valeur = e;
    bool champGarde = false;
    if (valeur) {
      valeur->setFocus(Qt::OtherFocusReason);
      reel::deplacer(valeur, valeur->rect().center());
      reel::deplacer(vs, ecran(vs, vide));
      champGarde = valeur->hasFocus();
    }
    const QWidget* fo = QApplication::focusWidget();
    verifier(prisALEntree && champGarde, "S21 : la souris qui entre dans le canevas lui donne le focus, sauf pendant une saisie",
             std::string("entrée ") + (prisALEntree ? "1" : "0") + entree + ", champ " + (champGarde ? "1" : "0") + (valeur ? "" : " (champ « 1k » introuvable)") + ", ailleurs " +
                 (ailleurs ? ailleurs->metaObject()->className() + std::string(" ") + ailleurs->objectName().toStdString() : std::string("—")) + ", fenêtre active " +
                 (vs->isActiveWindow() ? "oui" : "non") + ", focus " + (fo ? fo->metaObject()->className() + std::string(" ") + fo->objectName().toStdString() : std::string("—")));

    // Suppr et R, le focus sur les onglets du ruban : ils agissent sur la sélection du canevas ; R tapé dans un champ
    // reste une lettre
    reel::touche(vs, Qt::Key_Escape);
    vs->setFocus(Qt::OtherFocusReason);
    reel::cliquer(vs, ecran(vs, centreR1));
    const int rotationAvant = symboleDe(*conception(doc), "R1")->rotation;
    ailleurs = trouverAilleurs();
    if (ailleurs) ailleurs->setFocus(Qt::OtherFocusReason);
    reel::touche(ailleurs ? ailleurs.data() : static_cast<QWidget*>(vs), Qt::Key_R, Qt::NoModifier, QStringLiteral("r"));
    attendre(80);
    const bool tourneAilleurs = symboleDe(*conception(doc), "R1")->rotation == (rotationAvant + 1) % 4;
    reel::touche(ailleurs ? ailleurs.data() : static_cast<QWidget*>(vs), Qt::Key_R, Qt::ShiftModifier, QStringLiteral("R"));
    attendre(80);
    const bool revenu = symboleDe(*conception(doc), "R1")->rotation == rotationAvant;
    verifier(ailleurs && tourneAilleurs && revenu, "S21 : R puis Maj+R tournent R1 dans un sens puis dans l'autre, le focus sur les onglets du ruban", {});
    reel::touche(ailleurs ? ailleurs.data() : static_cast<QWidget*>(vs), Qt::Key_Delete);
    attendre(100);
    const bool supprime = symboleDe(*conception(doc), "R1") == nullptr;  // routé : l'empreinte attend (3.6)
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(150);
    verifier(supprime && symboleDe(*conception(doc), "R1"), "S21 : Suppr supprime R1 le focus hors du canevas ; Ctrl+Z le rend", {});
    reel::cliquer(vs, ecran(vs, centreR1));
    attendre(100);
    valeur = nullptr;
    for (QLineEdit* e : a->findChildren<QLineEdit*>())
      if (e->isVisible() && e->text() == QStringLiteral("1k")) valeur = e;
    const int rotationChamp = symboleDe(*conception(doc), "R1")->rotation;
    if (valeur) {
      valeur->setFocus(Qt::OtherFocusReason);
      valeur->setCursorPosition(static_cast<int>(valeur->text().size()));
      reel::touche(valeur, Qt::Key_R, Qt::NoModifier, QStringLiteral("r"));
    }
    const bool lettre = valeur && valeur->text() == QStringLiteral("1kr") && symboleDe(*conception(doc), "R1")->rotation == rotationChamp;
    if (valeur) reel::touche(valeur, Qt::Key_Backspace);
    verifier(lettre, "S21 : R tapé dans le champ Valeur reste une lettre, R1 ne tourne pas", valeur ? valeur->text().toStdString() : std::string("pas de champ"));
    reel::touche(valeur ? static_cast<QWidget*>(valeur) : vs, Qt::Key_Escape);
    reel::touche(vs, Qt::Key_Escape);

    // C13 : F ne recadre plus au schéma (il dit une fois où est « Tout voir ») ; au circuit, il change de face
    vs->setFocus(Qt::OtherFocusReason);
    const double echelle = vs->echelle();
    const QPointF ou = vs->versEcran(centreR1);
    dernierMessage.clear();
    reel::touche(vs, Qt::Key_F);
    verifier(vs->echelle() == echelle && vs->versEcran(centreR1) == ou && dernierMessage.contains(QStringLiteral("Origine")),
             "C13 : F ne recadre plus le schéma et dit que Tout voir est sur Origine", dernierMessage.left(60).toStdString());
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(300);
    vc->recadrer();
    attendre(100);
    Pt pos{};
    bool dessous = false;
    for (const auto& e : conception(doc)->circuit.empreintes)
      if (e.composant == compR1) {
        pos = e.position;
        dessous = e.dessous;
      }
    vc->setFocus(Qt::OtherFocusReason);
    reel::cliquer(vc, ecran(vc, pos));
    reel::touche(vc, Qt::Key_F);
    attendre(100);
    bool retourne = false;
    for (const auto& e : conception(doc)->circuit.empreintes)
      if (e.composant == compR1) retourne = e.dessous != dessous;
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    attendre(150);
    verifier(retourne, "C13, PLA-09 : F change l'empreinte de face au circuit", {});

    // C18 : trois flèches espacées de 120 ms font un seul pas d'annulation et un seul calcul, 300 ms après la dernière
    reel::cliquer(vc, ecran(vc, pos));
    attendre(600);
    const std::uint64_t calculs = a->generationCalcul();
    const int pas = doc->positionHistorique();
    Pt avant{};
    for (const auto& e : conception(doc)->circuit.empreintes)
      if (e.composant == compR1) avant = e.position;
    for (int i = 0; i < 3; ++i) {
      reel::touche(vc, Qt::Key_Right);
      attendre(120);
    }
    attendre(600);
    Pt apres{};
    for (const auto& e : conception(doc)->circuit.empreintes)
      if (e.composant == compR1) apres = e.position;
    verifier(apres.x - avant.x == 3 * vc->pasPlacement() && doc->positionHistorique() == pas + 1 && a->generationCalcul() == calculs + 1,
             "C18 : trois flèches au circuit, un pas d'annulation et un seul calcul après la dernière",
             std::to_string(doc->positionHistorique() - pas) + " pas, " + std::to_string(a->generationCalcul() - calculs) + " calcul(s), " + std::to_string(mm(apres.x - avant.x)) + " mm");
    reel::touche(vc, Qt::Key_Escape);
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    attendre(150);
    QObject::disconnect(lien);
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
  }

  // ---------- 1.9 · Inspecteur stable : pages créées une fois, frappe appliquée sans perdre focus ni défilement
  // (ERG-01, P1, P2, P16, P21, P25) ----------
  {
    InspecteurElectronique* ins = a->inspecteur();
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Escape);
    reel::cliquer(vs, ecran(vs, centreR1));
    attendre(100);
    PageInspecteur* pageR1 = ins->page();
    const int creees = ins->pagesCreees();
    reel::cliquer(vs, ecran(vs, centreC1));
    attendre(100);
    reel::cliquer(vs, ecran(vs, centreR1));
    attendre(100);
    verifier(ins->clePage() == QStringLiteral("schema.composant") && ins->page() == pageR1 && ins->pagesCreees() == creees,
             "ERG-01 : passer de R1 à C1 puis à R1 garde la même page de l'inspecteur, sans en créer", ins->clePage().toStdString());

    // P1, P2 : taper « 4k7 » dans Valeur, lettre à lettre : le focus et le défilement restent, la feuille et l'en-tête
    // suivent la frappe, un seul pas d'annulation ; Tab passe au champ Empreinte
    auto* valeur = pageR1 ? pageR1->findChild<ChampTexteLie*>(QStringLiteral("champ:valeur")) : nullptr;
    auto* entete = pageR1 ? pageR1->findChild<QLabel*>(QStringLiteral("enteteInspecteur")) : nullptr;
    const std::string valeurAvant = conception(doc)->parReference("R1") ? conception(doc)->parReference("R1")->valeur : std::string();
    bool focusGarde = true, defilementGarde = true, feuilleSuit = true;
    int pas = doc->positionHistorique();
    if (valeur) {
      ins->setMaximumHeight(300);  // assez court pour défiler
      attendre(80);
      ins->verticalScrollBar()->setValue(40);
      ins->ensureWidgetVisible(valeur);
      attendre(50);
      const int defilement = ins->verticalScrollBar()->value();
      reel::cliquer(valeur, valeur->rect().center());
      valeur->selectAll();
      QString tape;
      for (const QChar ch : QStringLiteral("4k7")) {
        tape += ch;
        reel::taper(valeur, QString(ch));
        attendre(120);  // le calcul de fond repasse entre deux frappes
        focusGarde &= valeur->hasFocus();
        defilementGarde &= ins->verticalScrollBar()->value() == defilement;
        const ComposantElec* r = conception(doc)->parReference("R1");
        feuilleSuit &= r && r->valeur == tape.toStdString();
      }
      const bool enteteSuit = entete && entete->text().contains(QStringLiteral("4k7"));
      reel::touche(valeur, Qt::Key_Tab);
      attendre(80);
      const QWidget* apres = QApplication::focusWidget();
      verifier(focusGarde && defilementGarde, "P1 : taper dans Valeur ne perd ni le focus ni le défilement du panneau", {});
      verifier(feuilleSuit && enteteSuit, "P2 : la feuille et l'en-tête de l'inspecteur suivent la frappe", entete ? entete->text().left(80).toStdString() : std::string());
      verifier(apres && apres->objectName() == QStringLiteral("empreinteComposant") && doc->positionHistorique() == pas + 1 &&
                   doc->historique().value(doc->positionHistorique() - 1).contains(QStringLiteral("4k7")),
               "P1, P2 : Tab passe au champ Empreinte ; la saisie fait un seul pas d'annulation, nommé", doc->historique().value(doc->positionHistorique() - 1).toStdString());
      reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
      attendre(150);
      verifier(conception(doc)->parReference("R1")->valeur == valeurAvant && valeur->text() == QString::fromStdString(valeurAvant),
               "P2 : Ctrl+Z rend la valeur d'avant la saisie, au modèle et au champ", valeur->text().toStdString());
      // Échap dans le champ : la valeur d'avant revient et aucun pas ne reste
      pas = doc->positionHistorique();
      reel::cliquer(valeur, valeur->rect().center());
      valeur->end(false);
      // Une frappe qui laisse une valeur comprise (« 1k » → « 1k0 ») : elle s'écrit aussitôt (une valeur refusée ne
      // s'écrirait pas, 3.2)
      reel::taper(valeur, QStringLiteral("0"));
      attendre(80);
      const bool ecrit = conception(doc)->parReference("R1")->valeur == valeurAvant + "0";
      reel::touche(valeur, Qt::Key_Escape);
      attendre(80);
      verifier(ecrit && conception(doc)->parReference("R1")->valeur == valeurAvant && doc->positionHistorique() == pas && vs->hasFocus(),
               "P2 : Échap dans le champ reprend la valeur d'avant, sans laisser de pas, et rend le focus au canevas", conception(doc)->parReference("R1")->valeur);
    } else
      verifier(false, "P1 : le champ Valeur de l'inspecteur existe", {});

    // P16 : la molette sur la liste des empreintes sans focus fait défiler le panneau, sans changer l'empreinte
    if (auto* emp = pageR1 ? pageR1->findChild<QComboBox*>(QStringLiteral("empreinteComposant")) : nullptr) {
      vs->setFocus(Qt::OtherFocusReason);
      ins->ensureWidgetVisible(emp);
      const int indice = emp->currentIndex();
      const std::string empreinte = conception(doc)->parReference("R1")->empreinte;
      const QPointF centre = QRectF(emp->rect()).center();
      QWheelEvent molette(centre, emp->mapToGlobal(centre), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
      QCoreApplication::sendEvent(emp, &molette);
      attendre(50);
      verifier(emp->currentIndex() == indice && conception(doc)->parReference("R1")->empreinte == empreinte, "P16 : la molette sur une liste sans focus ne change pas l'empreinte", {});
    }

    // P21 : une révision publiée met l'inspecteur en lecture seule, sous un bandeau qui propose la suivante
    // (la publication ne s'annule pas : la révision suivante, par le bouton du bandeau, rend la conception modifiable)
    doc->modifier([](prom::Projet& p) { p.electronique[0].verrouillee = true; });
    attendre(150);
    QWidget* bandeau = ins->findChild<QWidget*>(QStringLiteral("verrou"));
    auto* revision = ins->findChild<QPushButton*>(QStringLiteral("nouvelleRevision"));
    const bool lecture = valeur && valeur->isReadOnly() && bandeau && bandeau->isVisible() && revision && revision->isVisible();
    const std::string etatVerrou = std::string(valeur && valeur->isReadOnly() ? "champ en lecture seule" : "champ modifiable") + (bandeau && bandeau->isVisible() ? ", bandeau montré" : ", bandeau caché") +
                                   (revision && revision->isVisible() ? ", bouton montré" : ", bouton caché") + ", verrou " + (conception(doc)->verrouillee ? "oui" : "non");
    if (revision) {
      ins->ensureWidgetVisible(revision);
      attendre(50);
      reel::cliquer(revision, revision->rect().center());
    }
    attendre(150);
    verifier(lecture && valeur && !valeur->isReadOnly() && !bandeau->isVisible(), "P21 : publiée, l'inspecteur est en lecture seule sous le bandeau « Nouvelle révision » ; rendue modifiable, il l'est aussi",
             std::string(lecture ? "lecture seule" : "modifiable") + " puis " + (valeur && valeur->isReadOnly() ? "lecture seule" : "modifiable") + ", bandeau " +
                 (bandeau && bandeau->isVisible() ? "visible" : "masqué") + ", page " + ins->clePage().toStdString() + ", verrou " + (conception(doc)->verrouillee ? "oui" : "non") +
                 " ; publiée : " + etatVerrou);

    // P15 : le panneau prend ses couleurs dans le thème, sans traduction après coup
    const QString feuille = ins->widget() ? ins->widget()->styleSheet() : QString();
    verifier(feuille.contains(Theme::instance().c().surface.name()) && feuille.contains(Theme::instance().c().bordure.name()) && ins->widget()->property("themeFixe").toBool(),
             "P15 : les couleurs de l'inspecteur viennent du thème", {});

    // P25 : la largeur choisie reste en passant à la fabrication et en revenant
    const int largeur = ins->width();
    a->setMode(AtelierElectronique::Fabrication);
    attendre(250);
    const int enFabrication = ins->width();
    a->setMode(AtelierElectronique::Schema);
    attendre(150);
    verifier(enFabrication == largeur && ins->width() == largeur, "P25 : l'onglet Fabrication n'élargit plus l'inspecteur", std::to_string(largeur) + " → " + std::to_string(enFabrication) + " px");

    // P1 au circuit : sur la page Carte, une règle changée au bas du panneau ne le ramène pas en haut
    a->setMode(AtelierElectronique::Circuit);
    attendre(250);
    vc->setFocus(Qt::OtherFocusReason);
    reel::touche(vc, Qt::Key_Escape);
    attendre(100);
    ChampValeur* isolement = nullptr;
    if (PageInspecteur* carte = ins->page(); carte && ins->clePage() == QStringLiteral("circuit.carte"))
      for (QFormLayout* fl : carte->findChildren<QFormLayout*>())
        for (int i = 0; i < fl->rowCount(); ++i)
          if (auto* l = qobject_cast<QLabel*>(fl->itemAt(i, QFormLayout::LabelRole) ? fl->itemAt(i, QFormLayout::LabelRole)->widget() : nullptr);
              l && l->text() == QStringLiteral("Distance au bord"))
            isolement = qobject_cast<ChampValeur*>(fl->itemAt(i, QFormLayout::FieldRole)->widget());
    if (isolement) {
      ins->ensureWidgetVisible(isolement);
      attendre(50);
      const int defilement = ins->verticalScrollBar()->value();
      const Nm avantBord = conception(doc)->circuit.regles.bord;
      reel::cliquer(isolement, isolement->rect().center());
      const bool focusPris = isolement->hasFocus();
      isolement->selectAll();
      reel::taper(isolement, QStringLiteral("0,7"));
      const QString tape = isolement->text();
      reel::touche(isolement, Qt::Key_Return);
      attendre(250);
      verifier(conception(doc)->circuit.regles.bord == 700'000 && ins->verticalScrollBar()->value() == defilement && isolement->hasFocus() && defilement > 0,
               "P1 : une règle changée au bas de la page Carte garde le défilement et le focus",
               std::to_string(defilement) + " → " + std::to_string(ins->verticalScrollBar()->value()) + ", bord " + std::to_string(conception(doc)->circuit.regles.bord) + " nm, tapé « " +
                   tape.toStdString() + " », focus pris " + (focusPris ? "oui" : "non") + ", champ « " +
                   isolement->text().toStdString() + " », focus " + (QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "-"));
      if (conception(doc)->circuit.regles.bord != avantBord) {
        reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
        attendre(150);
      }
    } else
      verifier(false, "P1 : la page Carte a son champ « Distance au bord »", ins->clePage().toStdString());
    ins->setMaximumHeight(QWIDGETSIZE_MAX);
    a->setMode(AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.10 · Bibliothèque ancrée à gauche, distincte de l'inspecteur (BIB-04, B5, S3) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema);
    a->montrerBibliotheque();
    attendre(150);
    PanneauBibliotheque* bib = a->bibliotheque();
    InspecteurElectronique* ins = a->inspecteur();
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Escape);
    const bool gauche = bib && bib->isVisible() && bib->mapTo(a, QPoint(0, 0)).x() < vs->mapTo(a, QPoint(0, 0)).x() &&
                        ins->mapTo(a, QPoint(0, 0)).x() > vs->mapTo(a, QPoint(0, 0)).x() && !ins->isAncestorOf(bib);
    verifier(gauche, "B5 : la bibliothèque est à gauche du canevas, l'inspecteur seul à droite", {});
    // Choisir un article ne change pas le panneau de droite
    reel::cliquer(vs, ecran(vs, centreR1));
    attendre(100);
    const QString pageAvant = ins->clePage();
    auto* entete = ins->page() ? ins->page()->findChild<QLabel*>(QStringLiteral("enteteInspecteur")) : nullptr;
    const QString enteteAvant = entete ? entete->text() : QString();
    QTreeWidgetItem* condensateur = nullptr;
    if (bib)
      for (QTreeWidgetItemIterator it(bib->arbre()); *it; ++it)
        if ((*it)->text(0) == QStringLiteral("Condensateur")) condensateur = *it;
    if (condensateur) {
      bib->arbre()->scrollToItem(condensateur);
      reel::cliquer(bib->arbre()->viewport(), bib->arbre()->visualItemRect(condensateur).center());
    }
    attendre(100);
    // Depuis le lot L2 (B7), ce clic arme la pose : la sélection se vide (S11) et l'inspecteur montre la feuille,
    // mais la bibliothèque reste ouverte à gauche, avec son encart « À poser », et l'inspecteur reste à droite
    QWidget* encartPose = bib ? bib->findChild<QWidget*>(QStringLiteral("aPoser")) : nullptr;
    verifier(condensateur && bib->articleChoisi() != std::string() && bib->isVisible() && encartPose && encartPose->isVisible() && vs->outil() == VueSchema::Outil::Placer &&
                 !ins->isAncestorOf(bib) && !bib->isAncestorOf(ins),
             "S3, B5, B7 : un clic sur un article arme sa pose ; la bibliothèque reste ouverte à gauche, l'inspecteur à droite", pageAvant.toStdString() + " → " + ins->clePage().toStdString());
    (void)enteteAvant;
    reel::touche(vs, Qt::Key_Escape);  // la pose rendue : le clic suivant ne pose rien
    attendre(60);
    // Un clic dans le vide de la feuille ne la referme plus
    reel::cliquer(vs, ecran(vs, vide));
    attendre(80);
    verifier(bib && bib->isVisible() && sel->vide(), "B5 : un clic dans le vide de la feuille laisse la bibliothèque ouverte", {});
    // Armer la pose : la bibliothèque reste, l'encart « À poser » nomme l'article ; « Annuler » rend la main
    if (condensateur) reel::doubleCliquer(bib->arbre()->viewport(), bib->arbre()->visualItemRect(condensateur).center());
    attendre(100);
    QWidget* aPoser = bib ? bib->findChild<QWidget*>(QStringLiteral("aPoser")) : nullptr;
    const bool encart = aPoser && aPoser->isVisible() && vs->outil() == VueSchema::Outil::Placer && bib->isVisible();
    auto* annuler = bib ? bib->findChild<QPushButton*>(QStringLiteral("annulerPose")) : nullptr;
    if (annuler) reel::cliquer(annuler, annuler->rect().center());
    attendre(80);
    verifier(encart && vs->outil() == VueSchema::Outil::Selection && !aPoser->isVisible(),
             "S3 : pendant la pose, la bibliothèque reste ouverte avec l'encart « À poser » ; « Annuler » rend la main", {});
    // Repliée, elle le reste en passant au circuit et en revenant ; elle se rouvre par le ruban, A y met le curseur
    auto* replier = bib ? bib->findChild<QToolButton*>(QStringLiteral("replierBibliotheque")) : nullptr;
    if (replier) reel::cliquer(replier, replier->rect().center());
    attendre(80);
    const bool repliee = bib && !bib->isVisible();
    a->setMode(AtelierElectronique::Circuit);
    attendre(150);
    const bool cacheeAuCircuit = bib && !bib->isVisible();
    a->setMode(AtelierElectronique::Schema);
    attendre(150);
    const bool resteRepliee = bib && !bib->isVisible() && !QSettings().value(QStringLiteral("electronique/bibliothequeOuverte"), true).toBool();
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_A);
    attendre(100);
    verifier(repliee && cacheeAuCircuit && resteRepliee && bib->isVisible() && bib->recherche()->hasFocus(),
             "B5 : repliée, la bibliothèque le reste (réglage gardé) ; A la rouvre, le curseur dans sa recherche", {});
    reel::touche(bib->recherche(), Qt::Key_Escape);
    vs->setFocus(Qt::OtherFocusReason);
  }
  // ---------- 1.11 · Bilan permanent, messages passagers empilés, panneau Messages (ERG-07, S29) ----------
  {
    BarreEtatAtelier* barre = a->barreEtat();
    PileMessages* pile = a->pileMessages();
    // Le circuit mis à jour : son message reste lisible après le recalcul, et le bilan garde ses compteurs
    a->setMode(AtelierElectronique::Circuit);
    attendre(200);
    const int position = doc->positionHistorique();
    a->mettreAJourCircuit();
    attendre(700);  // recalcul et DRC de fond passés
    bool vu = false;
    for (int i = 0; i < pile->nombre(); ++i) vu |= pile->texte(i).contains(QStringLiteral("Circuit mis à jour"));
    verifier(vu && barre->bilan().contains(QStringLiteral("DRC")) && barre->bilan().contains(QStringLiteral("Routage")),
             "S29 : « Circuit mis à jour » reste affiché après le recalcul, et le bilan garde ses compteurs", barre->bilan().left(80).toStdString());
    while (doc->positionHistorique() > position) {
      doc->annuler();
      attendre(80);
    }
    // Trois messages au plus ; leur durée suit leur longueur
    for (int i = 1; i <= 5; ++i) a->afficherMessage(QStringLiteral("Message d’essai %1").arg(i));
    attendre(50);
    QString mots;
    for (int i = 0; i < 30; ++i) mots += QStringLiteral("mot ");
    QString beaucoup;
    for (int i = 0; i < 300; ++i) beaucoup += QStringLiteral("mot ");
    verifier(pile->nombre() == 3 && pile->texte(2).contains(QStringLiteral("essai 5")) && pile->texte(0).contains(QStringLiteral("essai 3")) && BulleMessage::duree(QStringLiteral("Via posée.")) == 4000 &&
                 BulleMessage::duree(mots) == 5200 && BulleMessage::duree(beaucoup) == 12000,
             "ERG-07 : trois messages passagers au plus, les plus récents ; 4 s, plus 60 ms par mot au-delà de dix, 12 s au plus", std::to_string(pile->nombre()) + " message(s)");
    // Un clic sur un message mène à l'objet
    bool mene = false;
    a->afficherMessage(QStringLiteral("R1 : cliquer pour y aller"), NiveauMessage::Avertissement, [&mene] { mene = true; });
    attendre(50);
    BulleMessage* derniere = nullptr;
    for (BulleMessage* b : pile->findChildren<BulleMessage*>())
      if (b->isVisible() && b->text().contains(QStringLiteral("cliquer pour y aller"))) derniere = b;
    if (derniere) reel::cliquer(derniere, derniere->rect().center());
    attendre(50);
    verifier(mene, "ERG-07 : un clic sur un message mène à l'objet concerné", {});
    // Le panneau Messages garde la session, filtre par niveau
    a->afficherMessage(QStringLiteral("Erreur d’essai"), NiveauMessage::Erreur);
    barre->ouvrirMessages();
    attendre(100);
    PanneauMessages* panneau = barre->panneau();
    const int tous = panneau ? panneau->lignes() : 0;
    QComboBox* niveau = panneau ? panneau->findChild<QComboBox*>(QStringLiteral("niveauMessages")) : nullptr;
    if (niveau) niveau->setCurrentIndex(niveau->findData(static_cast<int>(NiveauMessage::Erreur)));
    const int erreurs = panneau ? panneau->lignes() : 0;
    if (niveau) niveau->setCurrentIndex(0);
    verifier(panneau && panneau->isVisible() && tous >= 8 && erreurs >= 1 && erreurs < tous && a->journal()->messages().size() <= JournalMessages::MAXIMUM,
             "ERG-07 : le panneau Messages garde la session et filtre par niveau", std::to_string(tous) + " message(s), " + std::to_string(erreurs) + " erreur(s)");
    if (QDockWidget* d = a->panneau(AtelierElectronique::Panneau::Messages)) d->hide();
    reel::activer(&f);
    a->setMode(AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.12 · Une zone se choisit par son bord ; Alt+clic liste les objets sous le curseur (C10) ----------
  {
    a->setMode(AtelierElectronique::Circuit);
    attendre(250);
    vc->recadrer();
    attendre(100);
    vc->setFocus(Qt::OtherFocusReason);
    reel::touche(vc, Qt::Key_Escape);
    // Un plan de masse au dos, sur toute la carte ; la couche active reste le dessus
    const Polygone contour = conception(doc)->circuit.contour.polygone();
    const std::string reseauPlan = a->calculs().k.reseaux.empty() ? std::string("GND") : a->calculs().k.reseaux.front().nom;
    const std::string idPlan = nouvelUuid();
    doc->modifierConception(0, [&](ConceptionElec& c2) {
      Zone z;
      z.uuid = idPlan;
      z.reseau = reseauPlan;
      z.couche = Couche::CuivreDessous;
      z.contour = contour;
      c2.circuit.zones.push_back(z);
    });
    attendre(300);
    vc->setCoucheActive(Couche::CuivreDessus);
    // Un point de la carte loin de tout objet : au milieu du plan
    auto loin = [&](Pt p) {
      const ConceptionElec* cc = conception(doc);
      for (const auto& x : a->calculs().pastilles)
        if (distance(x.pastille.position, p) < 5'000'000) return false;
      for (const auto& t : cc->circuit.pistes)
        if (distancePointSegment(p, t.a, t.b) < 1'500'000) return false;
      for (const auto& v : cc->circuit.vias)
        if (distance(v.p, p) < 2'000'000) return false;
      for (const auto& t : cc->circuit.textes)
        if (distance(t.p, p) < 6'000'000) return false;
      for (std::size_t i = 0; i < contour.size(); ++i)
        if (distancePointSegment(p, contour[i], contour[(i + 1) % contour.size()]) < 3'000'000) return false;
      return dedans(contour, p);
    };
    const Boite bc = boite(contour);
    Pt milieu{};
    bool trouve = false;
    for (Nm y = bc.y0; y <= bc.y1 && !trouve; y += 1'270'000)
      for (Nm x = bc.x0; x <= bc.x1 && !trouve; x += 1'270'000)
        if (loin({x, y})) {
          milieu = {x, y};
          trouve = true;
        }
    reel::cliquer(vc, ecran(vc, milieu));
    attendre(80);
    verifier(trouve && sel->vide() && vc->genreChoisi() == VueCircuit::Genre::Rien, "C10 : un clic au milieu d'un plan de masse ne choisit rien", {});
    // Un cadre qui part de l'intérieur du plan choisit, il ne déplace pas le plan
    const Polygone avant = conception(doc)->circuit.zones.back().contour;
    reel::glisser(vc, ecran(vc, milieu), ecran(vc, {bc.x1 - 1'000'000, bc.y1 - 1'000'000}));
    attendre(100);
    verifier(conception(doc)->circuit.zones.back().contour == avant && !vc->estChoisi(VueCircuit::Genre::Zone, idPlan),
             "C10 : un cadre tiré depuis l'intérieur du plan ne le déplace pas", std::to_string(sel->taille()) + " objet(s) choisi(s)");
    reel::touche(vc, Qt::Key_Escape);
    // Le bord du plan le choisit
    const Pt surBord{(contour[0].x + contour[1].x) / 2, (contour[0].y + contour[1].y) / 2};
    reel::cliquer(vc, ecran(vc, surBord));
    attendre(80);
    verifier(vc->genreChoisi() == VueCircuit::Genre::Zone && vc->choisi() == idPlan, "C10 : un clic sur le bord du plan le choisit",
             "genre " + std::to_string(static_cast<int>(vc->genreChoisi())) + " « " + vc->choisi() + " », plan « " + idPlan + " », " + std::to_string(sel->taille()) + " choisi(s), bandeau « " +
                 vc->bandeau()->texte().toStdString() + " »");
    reel::touche(vc, Qt::Key_Escape);
    // Alt+clic au milieu : la liste des objets dessous, le plan compris, et on y choisit
    reel::cliquer(vc, ecran(vc, milieu), Qt::LeftButton, Qt::AltModifier);
    attendre(150);
    QMenu* liste = nullptr;
    for (QMenu* m : vc->findChildren<QMenu*>(QStringLiteral("objetsSous")))
      if (m->isVisible()) liste = m;
    QAction* entree = nullptr;
    if (liste)
      for (QAction* x : liste->actions())
        if (x->text().startsWith(QStringLiteral("Zone ") + QString::fromStdString(reseauPlan))) entree = x;
    const QString texteEntree = entree ? entree->text() : QString();  // le menu et ses lignes disparaissent au clic
    {
      const QPointer<QMenu> garde(liste);
      cliquerLigne(liste, entree);
      if (garde && garde->isVisible()) garde->close();
    }
    attendre(80);
    verifier(!texteEntree.isEmpty() && vc->choisi() == idPlan, "C10 : Alt+clic dans le plan le propose dans la liste des objets dessous, avec sa couche",
             texteEntree.isEmpty() ? std::string("aucune entrée") : texteEntree.toStdString());
    reel::touche(vc, Qt::Key_Escape);
    // Un clic répété au même endroit passe à l'objet suivant dessous : la piste, puis l'empreinte de sa pastille
    const auto& pistes = conception(doc)->circuit.pistes;
    Pt bout{};
    bool surPastille = false;
    // Une pastille où finit une seule piste : dessous, la piste puis l'empreinte, rien d'autre
    auto pistesEn = [&](Pt p) {
      int n = 0;
      for (const auto& t : pistes) n += (t.a == p) + (t.b == p);
      return n;
    };
    for (const auto& t : pistes)
      if (!surPastille && t.couche == Couche::CuivreDessus && pistesEn(t.a) == 1 && ecran(vc, t.a).y() > BandeauOutil::HAUTEUR + 8 &&
          vc->rect().adjusted(8, 8, -8, -8).contains(ecran(vc, t.a)))  // hors du bandeau, dans la vue
        for (const auto& x : a->calculs().pastilles)
          if (!surPastille && x.pastille.position == t.a) {
            bout = t.a;
            surPastille = true;
          }
    // Le pointeur posé d'abord, les événements en attente passés : la vue ne bouge plus quand le clic arrive
    reel::deplacer(vc, ecran(vc, bout));
    attendre(150);
    reel::cliquer(vc, ecran(vc, bout));
    attendre(60);
    // Le recalcul de fond lancé par le plan ajouté peut rendre la sélection d'avant pendant ce premier clic : il se
    // refait alors, l'essai porte sur le second
    if (vc->genreChoisi() == VueCircuit::Genre::Rien) {
      attendre(400);
      reel::cliquer(vc, ecran(vc, bout));
      attendre(60);
    }
    const VueCircuit::Genre premier = vc->genreChoisi();
    reel::cliquer(vc, ecran(vc, bout));
    attendre(60);
    verifier(surPastille && premier == VueCircuit::Genre::Piste && vc->genreChoisi() == VueCircuit::Genre::Empreinte,
             "C10 : un second clic au même endroit passe de la piste à l'empreinte dessous",
             "genres " + std::to_string(static_cast<int>(premier)) + " puis " + std::to_string(static_cast<int>(vc->genreChoisi())) + ", " + std::to_string(vc->selection().size()) + " choisi(s)");
    reel::touche(vc, Qt::Key_Escape);
    doc->modifierConception(0, [&](ConceptionElec& c2) {
      auto& z = c2.circuit.zones;
      z.erase(std::remove_if(z.begin(), z.end(), [&](const Zone& x) { return x.uuid == idPlan; }), z.end());
    });
    attendre(200);
    a->setMode(AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.16 · La souris suffit (ERG-17) : dans chaque état, chaque action possible du registre a un bouton du
  // ruban ou du bandeau, une ligne de menu ou un geste déclaré ; une piste se route d'une face à l'autre, via posée et
  // dernier coude retiré, puis la carte se cadre, sans une touche ; le pointeur est relevé autour de chaque geste
  // (ERG-18) ----------
  {
    const RegistreActions& registre = RegistreActions::atelierElectronique();
    QTabWidget* ruban = nullptr;
    for (QTabWidget* t : a->findChildren<QTabWidget*>())
      if (t->count() == 4 && t->tabText(0) == QStringLiteral("Schéma")) ruban = t;
    // Ce que la souris atteint maintenant : l'onglet montré du ruban, le bandeau de la vue, les menus de la fenêtre,
    // la barre de titre. « actifs » : visibles et actifs ; « montres » : visibles, actifs ou non
    struct Chemins {
      QSet<QString> actifs, montres, existants;  // existants : sur un onglet du ruban ou dans un menu, montrés ou non
    };
    auto chemins = [&](QWidget* vue) {
      Chemins r;
      auto prendre = [&r](const QObject* o, bool actif) {
        for (const QString& id : actionsMarquees(o)) {
          r.montres.insert(id);
          if (actif) r.actifs.insert(id);
        }
      };
      if (auto* barre = ruban ? qobject_cast<QToolBar*>(ruban->currentWidget()) : nullptr)
        for (QAction* x : barre->actions()) {
          QWidget* w = x->isVisible() ? barre->widgetForAction(x) : nullptr;
          if (!w) continue;
          QList<QWidget*> ws = w->findChildren<QWidget*>();
          ws << w;
          for (QWidget* y : ws) {
            if (auto* b = qobject_cast<QToolButton*>(y)) {
              prendre(b, b->isEnabled());
              if (b->defaultAction()) prendre(b->defaultAction(), b->isEnabled() && b->defaultAction()->isEnabled());
              if (b->menu())
                for (QAction* m : b->menu()->actions())
                  if (m->isVisible()) prendre(m, b->isEnabled() && m->isEnabled());
            } else if (qobject_cast<QComboBox*>(y)) {
              prendre(y, y->isEnabled());
            }
          }
        }
      if (auto* b = vue->findChild<BandeauOutil*>(); b && b->isVisible())
        for (QToolButton* x : b->findChildren<QToolButton*>())
          if (x->isVisible()) {
            prendre(x, x->isEnabled());
            if (x->menu())
              for (QAction* m : x->menu()->actions())
                if (m->isVisible()) prendre(m, x->isEnabled() && m->isEnabled());
          }
      // Toutes les entrées du ruban, sur chaque onglet, et celles des menus, montrées ou non
      if (ruban)
        for (int i = 0; i < ruban->count(); ++i)
          for (QObject* o : ruban->widget(i)->findChildren<QObject*>())
            for (const QString& id : actionsMarquees(o)) r.existants.insert(id);
      // Toute la fenêtre : dans la fenêtre à onglets, la palette et l'historique sont dans sa barre de titre
      for (QAction* x : f.window()->findChildren<QAction*>())
        for (const QString& id : actionsMarquees(x)) r.existants.insert(id);
      for (QWidget* w : f.window()->findChildren<QWidget*>())
        for (const QString& id : actionsMarquees(w)) r.existants.insert(id);
      std::function<void(QMenu*)> menu = [&](QMenu* m) {
        emit m->aboutToShow();  // les menus règlent leurs lignes à l'ouverture
        for (QAction* x : m->actions()) {
          if (!x->isVisible()) continue;
          if (x->menu()) menu(x->menu());
          else prendre(x, x->isEnabled());
        }
      };
      for (QMenu* m : f.menusPrincipaux())
        if (m->menuAction()->isVisible()) menu(m);
      for (QPushButton* p : f.findChildren<QPushButton*>())
        if (p->isVisible()) prendre(p, p->isEnabled());
      return r;
    };
    QStringList manques;
    int etats = 0, verifiees = 0;
    auto releve = [&](const QString& etat, bool schema) {
      QWidget* vue = schema ? static_cast<QWidget*>(vs) : static_cast<QWidget*>(vc);
      const Chemins r = chemins(vue);
      ++etats;
      for (const ActionAtelier& x : registre.actions()) {
        if (x.cachee || !x.souris.isEmpty()) continue;  // un geste déclaré : glisser, molette, clic droit, arbre
        const QString id = QString::fromStdString(x.identifiant);
        bool trouve = true;
        if (x.portee == PorteeAction::Partout || x.portee == PorteeAction::Atelier) {
          trouve = r.existants.contains(id);  // fenêtre et atelier : l'entrée existe, sur un onglet du ruban ou dans un menu
        } else if (x.portee == PorteeAction::Canevas || x.portee == (schema ? PorteeAction::Schema : PorteeAction::Circuit)) {
          const bool possible = schema ? vs->actionPossible(x.identifiant) : vc->actionPossible(x.identifiant);
          trouve = !possible || r.actifs.contains(id);
        }
        ++verifiees;
        if (!trouve) manques << QStringLiteral("%1 (%2) : aucun chemin à la souris dans l’état « %3 »").arg(id, registre.touches(x.identifiant), etat);
      }
    };
    using OS = VueSchema::Outil;
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
    vs->setOutil(OS::Selection);
    sel->vider();
    attendre(50);
    releve(QStringLiteral("schéma neutre"), true);
    reel::cliquer(vs, ecran(vs, centreR1));
    attendre(80);
    releve(QStringLiteral("schéma, R1 choisi"), true);
    vs->setOutil(OS::Placer, "R");
    releve(QStringLiteral("pose d’une résistance"), true);
    vs->setOutil(OS::Fil);
    reel::cliquer(vs, ecran(vs, vide));
    reel::deplacer(vs, ecran(vs, vide + Pt{5'080'000, 0}));
    releve(QStringLiteral("fil commencé"), true);
    vs->echap();
    vs->echap();
    vs->setOutil(OS::Etiquette);
    releve(QStringLiteral("étiquette"), true);
    vs->echap();
    vs->setOutil(OS::Alimentation, "GND");
    releve(QStringLiteral("alimentation"), true);
    vs->echap();

    // Circuit : neutre, empreinte choisie, routage commencé, zone commencée
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(250);
    a->recalculer();
    vc->recadrer();
    attendre(150);
    vc->setOutil(VueCircuit::Outil::Selection);
    sel->vider();
    releve(QStringLiteral("circuit neutre"), false);
    Pt pastille{};
    bool trouvee = false;
    for (const auto& p : a->calculs().pastilles)
      if (!trouvee && p.reference == "R1" && p.numero == "1") {
        pastille = p.pastille.position;
        trouvee = true;
      }
    reel::cliquer(vc, ecran(vc, pastille));
    attendre(80);
    releve(QStringLiteral("circuit, objet choisi"), false);
    vc->setOutil(VueCircuit::Outil::Selection);
    sel->vider();
    // Trois points de passage loin de tout cuivre, en équerre : le tracé ne bute sur rien
    auto libre = [&](Pt p) {
      for (const auto& x : a->calculs().pastilles)
        if (dedans(x.forme, p) || distance(x.pastille.position, p) < 2'000'000) return false;
      for (const auto& t : conception(doc)->circuit.pistes)
        if (distancePointSegment(p, t.a, t.b) < 1'500'000) return false;
      for (const auto& v : conception(doc)->circuit.vias)
        if (distance(v.p, p) < 1'800'000) return false;
      return dedans(conception(doc)->circuit.contour.polygone(), p);
    };
    auto segmentLibre = [&](Pt p, Pt q) {
      for (int i = 0; i <= 8; ++i)
        if (!libre(p + Pt{(q.x - p.x) * i / 8, (q.y - p.y) * i / 8})) return false;
      return true;
    };
    Pt e1 = pastille, e2 = pastille, e3 = pastille;
    bool passages = false;
    for (int n = 2; n <= 8 && !passages; ++n)
      for (const Pt d : {Pt{1, 0}, Pt{-1, 0}, Pt{0, 1}, Pt{0, -1}})
        for (const Pt e : {Pt{d.y, d.x}, Pt{-d.y, -d.x}}) {
          const Pt a1 = pastille + Pt{d.x * n * 1'270'000, d.y * n * 1'270'000};
          const Pt a2 = a1 + Pt{e.x * 3'810'000, e.y * 3'810'000};
          const Pt a3 = a2 + Pt{d.x * 3'810'000, d.y * 3'810'000};
          if (!passages && libre(a1) && segmentLibre(a1, a2) && segmentLibre(a2, a3)) {
            e1 = a1;
            e2 = a2;
            e3 = a3;
            passages = true;
          }
        }
    vc->setOutil(VueCircuit::Outil::Router);
    reel::cliquer(vc, ecran(vc, pastille));
    reel::cliquer(vc, ecran(vc, e1));
    reel::deplacer(vc, ecran(vc, e2));
    releve(QStringLiteral("piste commencée"), false);
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);  // le tracé d'essai retiré
    vc->setOutil(VueCircuit::Outil::Zone);
    const Boite carte = boite(conception(doc)->circuit.contour.polygone());
    reel::cliquer(vc, ecran(vc, carte.centre()));
    reel::cliquer(vc, ecran(vc, carte.centre() + Pt{2'000'000, 0}));
    releve(QStringLiteral("zone commencée"), false);
    vc->echap();
    vc->echap();
    verifier(ruban && etats == 10 && manques.isEmpty(),
             "ERG-17 : dans chaque état, chaque action possible a un bouton, une ligne de menu ou un geste déclaré",
             manques.isEmpty() ? std::to_string(verifiees) + " vérifications" : manques.mid(0, 4).join(QStringLiteral(" ; ")).toStdString());

    // Le parcours sans une touche : Router au ruban, la pastille, un coude, Dessous au bandeau (via), deux coudes,
    // Retirer le coude, Terminer, puis Tout voir au ruban
    const int touchesAvant = reel::touchesEnvoyees();
    auto boutonRuban = [&](const char* id) -> QToolButton* {
      auto* barre = ruban ? qobject_cast<QToolBar*>(ruban->currentWidget()) : nullptr;
      if (!barre) return nullptr;
      auto cherche = [&]() -> QToolButton* {
        for (QToolButton* b : barre->findChildren<QToolButton*>())
          if (b->isVisible() && b->defaultAction() && actionsMarquees(b->defaultAction()).contains(QLatin1String(id))) return b;
        return nullptr;
      };
      if (QToolButton* b = cherche()) return b;
      // Ruban trop étroit : l'entrée est dans le prolongement, ouvert d'un clic sur sa flèche
      if (auto* suite = barre->findChild<QToolButton*>(QStringLiteral("qt_toolbar_ext_button")); suite && suite->isVisible()) {
        reel::cliquer(suite, suite->rect().center());
        attendre(150);
      }
      return cherche();
    };
    sel->vider();
    vc->setOutil(VueCircuit::Outil::Selection);
    vc->setCoucheActive(Couche::CuivreDessus);
    const std::size_t pistesAvant = conception(doc)->circuit.pistes.size(), viasAvant = conception(doc)->circuit.vias.size();
    const int pasAvant = doc->positionHistorique();
    QToolButton* router = boutonRuban("circuit.router");
    if (router) reel::cliquer(router, QPoint(router->width() / 3, router->height() / 2));
    const bool arme = vc->outil() == VueCircuit::Outil::Router;
    reel::cliquer(vc, ecran(vc, pastille));
    reel::cliquer(vc, ecran(vc, e1));
    const bool via = bouton(vc->bandeau(), QStringLiteral("via"));  // vers l'autre couche de la paire
    reel::cliquer(vc, ecran(vc, e2));
    reel::cliquer(vc, ecran(vc, e3));
    reel::deplacer(vc, ecran(vc, e3 + Pt{1'270'000, 0}));
    const bool retire = bouton(vc->bandeau(), QStringLiteral("retirer-coude"));
    const bool fini = bouton(vc->bandeau(), QStringLiteral("terminer"));
    attendre(150);
    const auto& cc = conception(doc)->circuit;
    bool dessus = false, dessous = false, jusquE3 = false, viaE1 = false;
    for (std::size_t i = pistesAvant; i < cc.pistes.size(); ++i) {
      dessus |= cc.pistes[i].couche == Couche::CuivreDessus;
      dessous |= cc.pistes[i].couche == Couche::CuivreDessous;
      jusquE3 |= distance(cc.pistes[i].a, e3) < 300'000 || distance(cc.pistes[i].b, e3) < 300'000;
    }
    for (const auto& v : cc.vias) viaE1 |= distance(v.p, e1) < 700'000;
    // Tout voir, au ruban : le contour entier dans la vue
    vc->zoomer(4);
    QToolButton* toutVoir = boutonRuban("vue.toutVoir");
    if (toutVoir) reel::cliquer(toutVoir, toutVoir->rect().center());
    attendre(100);
    const Boite b = boite(conception(doc)->circuit.contour.polygone());
    const bool cadree = vc->rect().contains(ecran(vc, Pt{b.x0, b.y0})) && vc->rect().contains(ecran(vc, Pt{b.x1, b.y1}));
    verifier(trouvee && passages && arme && via && retire && fini && dessus && dessous && !jusquE3 && viaE1 && cc.vias.size() == viasAvant + 1 &&
                 doc->positionHistorique() == pasAvant + 1 && toutVoir && cadree && reel::touchesEnvoyees() == touchesAvant,
             "ERG-17 : piste routée d'une face à l'autre, via posée et dernier coude retiré, puis carte cadrée, sans une touche",
             std::to_string(cc.pistes.size() - pistesAvant) + " segment(s), " + std::to_string(cc.vias.size() - viasAvant) + " via(s)");
    while (doc->positionHistorique() > pasAvant) {
      doc->annuler();
      attendre(60);
    }
    vc->echap();
    verifier(reel::pointeurDeplace().isEmpty() && reel::gestesReleves() > 300, "ERG-18 : le pointeur, relevé autour de chaque geste du parcours, n'a jamais été déplacé",
             std::to_string(reel::gestesReleves()) + " gestes" + (reel::pointeurDeplace().isEmpty() ? std::string() : " ; " + reel::pointeurDeplace().front().toStdString()));
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.17 · Commandes imbriquées et barre du routeur (ERG-21, ROU-05) ----------
  {
    // Au schéma : pendant un fil, une jonction posée le suspend, puis il reprend au dernier point fixé
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
    using OS = VueSchema::Outil;
    vs->setOutil(OS::Selection);
    sel->vider();
    const Pt depart = surGrille(vide + Pt{0, -25'400'000}), point = surGrille(vide + Pt{0, -35'560'000}), arrivee = surGrille(vide + Pt{10'160'000, -25'400'000});
    const int pas0 = doc->positionHistorique();
    const std::size_t jonctions0 = conception(doc)->feuilles[0].jonctions.size(), fils0 = conception(doc)->feuilles[0].fils.size();
    vs->setOutil(OS::Fil);
    reel::cliquer(vs, ecran(vs, depart));
    vs->executerAction("schema.jonction");
    const bool suspendu = vs->traceSuspendu() && vs->outil() == OS::Jonction && vs->bandeau()->texte().contains(QStringLiteral("fil suspendu"));
    reel::cliquer(vs, ecran(vs, point));
    attendre(80);
    const bool repris = !vs->traceSuspendu() && vs->outil() == OS::Fil;
    reel::cliquer(vs, ecran(vs, arrivee));
    attendre(80);
    // Le fil se pose en un pas quand il est fini (2.7) : Échap pose les segments fixés
    const bool reprisFini = vs->outil() == OS::Fil;
    reel::touche(vs, Qt::Key_Escape);
    attendre(80);
    bool filDepuisDepart = false;
    for (std::size_t i = fils0; i < conception(doc)->feuilles[0].fils.size(); ++i) {
      const auto& w = conception(doc)->feuilles[0].fils[i];
      filDepuisDepart |= w.a == depart || w.b == depart;
    }
    verifier(suspendu && repris && reprisFini && conception(doc)->feuilles[0].jonctions.size() == jonctions0 + 1 && filDepuisDepart,
             "ERG-21 : pendant un fil, une jonction posée le suspend, puis il reprend au dernier point fixé",
             vs->bandeau()->texte().toStdString() + " · " + std::to_string(suspendu) + std::to_string(repris) + std::to_string(reprisFini) + " jonctions +" +
                 std::to_string(conception(doc)->feuilles[0].jonctions.size() - jonctions0) + ", fils +" + std::to_string(conception(doc)->feuilles[0].fils.size() - fils0) +
                 ", depuis le départ " + std::to_string(filDepuisDepart));
    vs->echap();
    vs->echap();
    while (doc->positionHistorique() > pas0) {
      doc->annuler();
      attendre(50);
    }

    // Au circuit : la barre du routeur (paire, prochaine via, largeur) ; une piste gênante supprimée au menu du clic
    // droit pendant la piste, qui reprend au dernier coude, sur la paire choisie
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(250);
    a->recalculer();
    vc->recadrer();
    attendre(150);
    vc->setOutil(VueCircuit::Outil::Selection);
    sel->vider();
    vc->setCoucheActive(Couche::CuivreDessus);
    Pt pastille{};
    std::string reseauR1;
    for (const auto& p : a->calculs().pastilles)
      if (p.reference == "R1" && p.numero == "1") {
        pastille = p.pastille.position;
        reseauR1 = p.reseau;
      }
    auto libre = [&](Pt p) {
      for (const auto& x : a->calculs().pastilles)
        if (dedans(x.forme, p) || distance(x.pastille.position, p) < 2'000'000) return false;
      for (const auto& t : conception(doc)->circuit.pistes)
        if (distancePointSegment(p, t.a, t.b) < 1'500'000) return false;
      for (const auto& v : conception(doc)->circuit.vias)
        if (distance(v.p, p) < 1'800'000) return false;
      return dedans(conception(doc)->circuit.contour.polygone(), p);
    };
    auto segmentLibre = [&](Pt p, Pt q) {
      for (int i = 0; i <= 8; ++i)
        if (!libre(p + Pt{(q.x - p.x) * i / 8, (q.y - p.y) * i / 8})) return false;
      return true;
    };
    Pt e1 = pastille, e2 = pastille, v1 = pastille;
    bool passages = false;
    for (int n = 2; n <= 8 && !passages; ++n)
      for (const Pt d : {Pt{1, 0}, Pt{-1, 0}, Pt{0, 1}, Pt{0, -1}})
        for (const Pt e : {Pt{d.y, d.x}, Pt{-d.y, -d.x}}) {
          const Pt a1 = pastille + Pt{d.x * n * 1'270'000, d.y * n * 1'270'000};
          const Pt a2 = a1 + Pt{e.x * 3'810'000, e.y * 3'810'000};
          const Pt a3 = a2 + Pt{d.x * 5'080'000, d.y * 5'080'000};
          if (!passages && libre(a1) && segmentLibre(a1, a2) && libre(a3)) {
            e1 = a1;
            e2 = a2;
            v1 = a3;
            passages = true;
          }
        }
    // Une piste gênante, d'un autre réseau, dont le milieu est libre de pastilles
    std::string gene;
    Pt milieu{};
    for (const auto& t : conception(doc)->circuit.pistes) {
      const Pt m{(t.a.x + t.b.x) / 2, (t.a.y + t.b.y) / 2};
      bool loin = gene.empty() && t.reseau != reseauR1 && distance(t.a, t.b) > 4'000'000;
      for (const auto& x : a->calculs().pastilles) loin &= !dedans(x.forme, m) && distance(x.pastille.position, m) > 2'000'000;
      for (const auto& v : conception(doc)->circuit.vias) loin &= distance(v.p, m) > 1'500'000;
      if (loin) {
        gene = t.uuid;
        milieu = m;
      }
    }
    const int pas1 = doc->positionHistorique();
    const std::size_t vias1 = conception(doc)->circuit.vias.size();
    vc->setOutil(VueCircuit::Outil::Router);
    // La paire, choisie dans la barre avant de commencer
    QToolButton* paire = vc->bandeau()->bouton(QStringLiteral("paire"));
    QAction* dessusDessous = nullptr;
    if (paire && paire->menu())
      for (QAction* x : paire->menu()->actions())
        if (x->text() == QStringLiteral("Dessus ↔ Dessous")) dessusDessous = x;
    choisirDansMenuBouton(paire, dessusDessous);
    reel::cliquer(vc, ecran(vc, pastille));
    reel::cliquer(vc, ecran(vc, e1));
    // La liste de la prochaine via : traversante seule active
    QToolButton* via = vc->bandeau()->bouton(QStringLiteral("via"));
    bool traversante = false, autresGrisees = true;
    if (via && via->menu())
      for (QAction* x : via->menu()->actions()) {
        if (x->text() == QStringLiteral("Traversante")) traversante = x->isEnabled() && x->isChecked();
        if (x->text() == QStringLiteral("Borgne") || x->text() == QStringLiteral("Enterrée") || x->text() == QStringLiteral("Micro-via")) autresGrisees &= !x->isEnabled();
      }
    // Clic droit court sur la piste gênante : son menu, « Supprimer la piste … sous le curseur »
    // Les bulles de message empilées sur le canevas (plus longues à s'effacer sur une machine lente) ne doivent pas
    // recevoir le clic : la pile est cachée le temps du geste
    const bool pileMontree = a->pileMessages()->isVisible();
    a->pileMessages()->hide();
    // Un point de la piste gênante que rien ne couvre : le cartouche du tracé, gardé dans la vue, peut se poser sur
    // son milieu quand la fenêtre est petite
    vc->centrerSur(milieu);  // loin des bords de la vue, où le cartouche, gardé dans la vue, se replie
    attendre(50);
    QPoint clic = ecran(vc, milieu);
    for (const auto& t : conception(doc)->circuit.pistes)
      if (t.uuid == gene)
        for (const double f : {0.5, 0.35, 0.65, 0.25, 0.75, 0.15, 0.85}) {
          const QPoint q = ecran(vc, Pt{t.a.x + static_cast<Nm>((t.b.x - t.a.x) * f), t.a.y + static_cast<Nm>((t.b.y - t.a.y) * f)});
          reel::deplacer(vc, q);
          if (QApplication::widgetAt(vc->mapToGlobal(q)) == vc) {
            clic = q;
            break;
          }
        }
    const QWidget* sousClic = QApplication::widgetAt(vc->mapToGlobal(clic));
    std::string sousClicNom = sousClic ? sousClic->metaObject()->className() : "—";
    for (const QWidget* x = sousClic ? sousClic->parentWidget() : nullptr; x && x != vc && sousClicNom.size() < 200; x = x->parentWidget())
      sousClicNom += std::string(" < ") + x->metaObject()->className() + (x->objectName().isEmpty() ? std::string() : ":" + x->objectName().toStdString());
    reel::cliquer(vc, clic, Qt::RightButton);
    // Le menu attendu jusqu'à une seconde : sur une machine chargée, il paraît après le traitement du relâché
    QMenu* menu = nullptr;
    for (int essai = 0; essai < 50 && !menu; ++essai) {
      attendre(20);
      for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible() && m->objectName() == QStringLiteral("menuRoutage")) menu = m;
    }
    QAction* supprimer = menu ? menu->findChild<QAction*>(QStringLiteral("supprimerPisteSous")) : nullptr;
    if (!supprimer && menu)
      for (QAction* x : menu->actions())
        if (x->objectName() == QStringLiteral("supprimerPisteSous")) supprimer = x;
    {
      const QPointer<QMenu> garde(menu);
      cliquerLigne(menu, supprimer);
      if (garde && garde->isVisible()) garde->close();
    }
    attendre(150);
    if (pileMontree) a->pileMessages()->show();
    bool geneSupprimee = true;
    for (const auto& t : conception(doc)->circuit.pistes) geneSupprimee &= t.uuid != gene;
    const bool toujours = vc->outil() == VueCircuit::Outil::Router && vc->bandeau()->bouton(QStringLiteral("via")) != nullptr;
    // La largeur des segments suivants, choisie dans la barre
    QToolButton* largeur = vc->bandeau()->bouton(QStringLiteral("largeur"));
    // La ligne trouvée d'abord : la choisir reconstruit le bandeau, ses boutons et leurs menus
    QAction* demiMm = nullptr;
    if (largeur && largeur->menu())
      for (QAction* x : largeur->menu()->actions())
        if (x->text() == QStringLiteral("0,500 mm")) demiMm = x;
    choisirDansMenuBouton(largeur, demiMm);
    const bool viaCliquee = bouton(vc->bandeau(), QStringLiteral("via"));  // vers l'autre couche de la paire
    const Couche apresVia = vc->coucheActive();
    reel::cliquer(vc, ecran(vc, e2));
    // Un autre outil pendant la piste : la via posée à part la suspend, puis elle reprend
    vc->setOutil(VueCircuit::Outil::Via);
    const bool viaSuspend = vc->traceSuspendu() && vc->bandeau()->texte().contains(QStringLiteral("suspendu"));
    reel::cliquer(vc, ecran(vc, v1));
    attendre(80);
    const bool reprise = !vc->traceSuspendu() && vc->outil() == VueCircuit::Outil::Router && vc->bandeau()->bouton(QStringLiteral("via")) != nullptr;
    bouton(vc->bandeau(), QStringLiteral("terminer"));
    attendre(150);
    const auto& cc = conception(doc)->circuit;
    bool dessus = false, dessous05 = false;
    for (std::size_t i = 0; i < cc.pistes.size(); ++i) {
      const auto& t = cc.pistes[i];
      // Sur la grille du routage, à une demi-maille près
      dessus |= t.couche == Couche::CuivreDessus && (distance(t.a, e1) < 400'000 || distance(t.b, e1) < 400'000);
      dessous05 |= t.couche == Couche::CuivreDessous && t.largeur == 500'000 && (distance(t.a, e1) < 400'000 || distance(t.b, e1) < 400'000);
    }
    verifier(passages && !gene.empty() && dessusDessous && traversante && autresGrisees,
             "ROU-05 : la barre du routeur choisit la paire de couches ; la prochaine via est traversante, les autres types grisés", {});
    verifier(geneSupprimee && toujours && dessus && dessous05 && cc.vias.size() == vias1 + 2 && vc->paireCouches() == std::pair{Couche::CuivreDessus, Couche::CuivreDessous},
             "ERG-21 : pendant une piste, une piste gênante supprimée au menu du clic droit ; le tracé reprend au dernier coude, sur la paire choisie, à la largeur choisie",
             std::to_string(cc.vias.size() - vias1) + " via(s), menu " + std::to_string(menu != nullptr && supprimer != nullptr) + ", via au bandeau " + std::to_string(viaCliquee) +
                 ", couche " + std::to_string(static_cast<int>(apresVia)) + ", sous le clic droit " + sousClicNom + ", gêne supprimée " + std::to_string(geneSupprimee));
    verifier(viaSuspend && reprise, "ERG-21 : un autre outil pendant la piste la suspend ; sa commande faite, la piste reprend", {});
    vc->echap();
    while (doc->positionHistorique() > pas1) {
      doc->annuler();
      attendre(50);
    }
    // À quatre couches : quatre paires proposées, et la via mène à l'autre couche de la paire choisie (C16)
    const auto paires = VueCircuit::pairesPossibles(4);
    vc->setPaireCouches(Couche::CuivreDessus, Couche::CuivreInt1);
    vc->setCoucheActive(Couche::CuivreDessus);
    const Couche apres = vc->coucheApresVia();
    vc->setPaireCouches(Couche::CuivreDessus, Couche::CuivreDessous);
    verifier(paires.size() == 4 && paires.front() == std::pair{Couche::CuivreDessus, Couche::CuivreDessous} && apres == Couche::CuivreInt1,
             "ROU-05 : à quatre couches, la paire extérieure puis les paires voisines ; la via suit la paire choisie, plus la couche suivante", {});
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.18 · Bandeau de sélection, appui long, filtre qui clignote (SAI-18, ERG-17) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
    vs->setOutil(VueSchema::Outil::Selection);
    sel->neutre();
    vs->recadrer();
    attendre(100);
    // Au schéma : le mode « Ajouter » du bandeau complète la sélection, « Retirer » en ôte, sans une touche
    reel::cliquer(vs, ecran(vs, centreR1));
    attendre(60);
    const bool bandeauVu = vs->bandeau()->isVisible() && vs->bandeau()->texte().startsWith(QStringLiteral("Sélection")) && vs->bandeau()->bouton(QStringLiteral("mode-ajouter"));
    bouton(vs->bandeau(), QStringLiteral("mode-ajouter"));
    reel::cliquer(vs, ecran(vs, centreC1));
    attendre(60);
    const bool deux = vs->selection().count(idR1) && vs->selection().count(idC1) && sel->taille() == 2;
    bouton(vs->bandeau(), QStringLiteral("mode-retirer"));
    reel::cliquer(vs, ecran(vs, centreR1));
    attendre(60);
    const bool retire = !vs->selection().count(idR1) && vs->selection().count(idC1);
    bouton(vs->bandeau(), QStringLiteral("terminer"));
    attendre(60);
    verifier(bandeauVu && deux && retire && sel->vide() && sel->modeBandeau() == SelectionElectronique::Mode::Remplacer && !vs->bandeau()->isVisible(),
             "SAI-18 : au schéma, le bandeau de sélection donne Ajouter et Retirer à la souris ; « Tout désélectionner » rend l'état neutre", {});

    // Au circuit : un appui long sur une pastille couverte par un plan ouvre la liste, qui propose les deux ; le mode
    // « Ajouter » du bandeau y complète la sélection
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(250);
    a->recalculer();
    vc->recadrer();
    attendre(150);
    vc->setOutil(VueCircuit::Outil::Selection);
    sel->neutre();
    const int pas0 = doc->positionHistorique();
    const std::string plan = nouvelUuid();
    doc->modifierConception(0, [&](ConceptionElec& c2) {
      Zone z;
      z.uuid = plan;
      z.reseau = "GND";
      z.couche = Couche::CuivreDessous;
      z.contour = c2.circuit.contour.polygone();
      c2.circuit.zones.push_back(z);
    });
    attendre(300);
    Pt pastille{};
    for (const auto& p : a->calculs().pastilles)
      if (p.reference == "R1" && p.numero == "1") pastille = p.pastille.position;
    auto listeOuverte = [] {
      for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible() && m->objectName() == QStringLiteral("objetsSous")) return m;
      return static_cast<QMenu*>(nullptr);
    };
    auto appuiLong = [&](Pt p, const QString& debut) {
      const QPoint e = ecran(vc, p);
      reel::deplacer(vc, e);
      reel::appuyer(vc, e);
      attendre(AppuiLong::DUREE + 250);
      QMenu* liste = listeOuverte();
      QStringList lignes;
      QAction* voulue = nullptr;
      if (liste)
        for (QAction* x : liste->actions())
          if (!x->isSeparator() && !x->text().isEmpty()) {
            lignes << x->text();
            if (!voulue && x->text().contains(debut)) voulue = x;
          }
      {
        const QPointer<QMenu> garde(liste);
        cliquerLigne(liste, voulue);
        if (garde && garde->isVisible()) garde->close();
      }
      attendre(60);
      reel::relacher(vc, e);
      attendre(60);
      return lignes;
    };
    const QStringList premiere = appuiLong(pastille, QStringLiteral("(empreinte)"));
    bool zoneProposee = false, empreinteProposee = false;
    for (const QString& l : premiere) {
      zoneProposee |= l.startsWith(QStringLiteral("Zone"));
      empreinteProposee |= l.contains(QStringLiteral("(empreinte)"));
    }
    const bool empreinte = vc->genreChoisi() == VueCircuit::Genre::Empreinte;
    const bool ajouter = bouton(vc->bandeau(), QStringLiteral("mode-ajouter"));
    appuiLong(pastille, QStringLiteral("Zone"));
    bool avecZone = false, avecEmpreinte = false;
    for (const auto& o : vc->selection()) {
      avecZone |= o.genre == VueCircuit::Genre::Zone;
      avecEmpreinte |= o.genre == VueCircuit::Genre::Empreinte;
    }
    verifier(zoneProposee && empreinteProposee && empreinte && ajouter && avecZone && avecEmpreinte,
             "SAI-18 : un appui long sur une pastille couverte par un plan les propose tous deux ; « Ajouter » du bandeau complète la sélection, sans une touche",
             premiere.join(QStringLiteral(" | ")).toStdString());
    bouton(vc->bandeau(), QStringLiteral("terminer"));

    // Le filtre de sélection : les pistes exclues, un clic sur une piste ne prend rien et le bouton clignote
    auto* filtre = a->findChild<BoutonFiltre*>(QStringLiteral("filtreSelection"));
    QAction* pistes = nullptr;
    if (filtre && filtre->menu())
      for (QAction* x : filtre->menu()->actions())
        if (x->isCheckable() && x->data().toInt() == static_cast<int>(SelectionElectronique::Genre::Piste)) pistes = x;
    if (pistes) pistes->setChecked(false);
    Pt milieu{};
    bool trouve = false;
    for (const auto& t : conception(doc)->circuit.pistes) {
      const Pt m{(t.a.x + t.b.x) / 2, (t.a.y + t.b.y) / 2};
      bool seule = !trouve && distance(t.a, t.b) > 2'500'000 && ecran(vc, m).y() > BandeauOutil::HAUTEUR + 8 && vc->rect().adjusted(8, 8, -8, -8).contains(ecran(vc, m));
      for (const auto& x : a->calculs().pastilles) seule &= !dedans(x.forme, m) && distance(x.pastille.position, m) > 1'500'000;
      for (const auto& v : conception(doc)->circuit.vias) seule &= distance(v.p, m) > 1'500'000;
      for (const auto& u : conception(doc)->circuit.pistes) seule &= u.uuid == t.uuid || distancePointSegment(m, u.a, u.b) > 800'000;
      // Hors de l'encombrement des empreintes, qui se prennent d'un clic dedans
      for (const auto& em : conception(doc)->circuit.empreintes)
        if (const DefEmpreinte* d = conception(doc)->empreinte(em.empreinte)) seule &= !dedans(encombrementSurCarte(em, *d), m);
      // Loin du bord d'une zone (qui se choisit par son bord) et hors des messages empilés sur le canevas
      for (const auto& z : conception(doc)->circuit.zones)
        for (std::size_t i = 0; i < z.contour.size(); ++i) seule &= distancePointSegment(m, z.contour[i], z.contour[(i + 1) % z.contour.size()]) > 1'500'000;
      seule &= !(a->pileMessages()->isVisible() && a->pileMessages()->geometry().adjusted(-8, -8, 8, 8).contains(ecran(vc, m)));
      // Hors des repères de sérigraphie, qui se prennent d'un clic pour être glissés (5.5)
      seule &= vc->repereSous(ecran(vc, m)).empty();
      if (seule) {
        milieu = m;
        trouve = true;
      }
    }
    const int avant = filtre ? filtre->clignotements() : 0;
    sel->neutre();
    reel::cliquer(vc, ecran(vc, milieu));
    attendre(80);
    const int clignote = filtre ? filtre->clignotements() - avant : -1;
    const std::size_t pris = sel->taille();
    const std::string quoi = pris ? std::to_string(static_cast<int>(sel->tenu()->genre)) + " " + sel->tenu()->id : std::string();
    const bool bloque = filtre && clignote == 1 && sel->vide();
    if (pistes) pistes->setChecked(true);
    reel::cliquer(vc, ecran(vc, milieu));
    attendre(80);
    verifier(trouve && pistes && filtre && filtre->text() == QStringLiteral("Filtre") && bloque && vc->genreChoisi() == VueCircuit::Genre::Piste,
             "SAI-18 : un clic que le filtre de sélection bloque ne prend rien, et le bouton du filtre clignote ; filtre rouvert, le clic prend la piste",
             "clignotements " + std::to_string(clignote) + ", " + std::to_string(pris) + " objet(s) pris (" + quoi + ") · " +
             std::to_string(trouve) + std::to_string(pistes != nullptr) + std::to_string(filtre != nullptr) + " « " + (filtre ? filtre->text().toStdString() : std::string()) + " » bloqué " +
                 std::to_string(bloque) + ", genre " + std::to_string(static_cast<int>(vc->genreChoisi())));
    vc->echap();
    while (doc->positionHistorique() > pas0) {
      doc->annuler();
      attendre(50);
    }
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.19 · Navigation commune : pavé tactile, menu à marques, défilement au bord, réglages (ERG-19, ERG-20) ----------
  {
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(250);
    vc->recadrer();
    attendre(100);
    vc->setOutil(VueCircuit::Outil::Selection);
    sel->neutre();
    const ReglagesNavigation initiaux = Navigation::instance().reglages();
    // La fenêtre des réglages, ouverte par son menu ; deux doigts réglés sur « faire défiler »
    QAction* menuReglages = f.findChild<QAction*>(QStringLiteral("menu:reglages"));
    if (menuReglages) reel::touche(&f, Qt::Key_Comma, Qt::ControlModifier, QStringLiteral(","));  // Ctrl+, : Fichier › Réglages…
    attendre(150);
    auto* fenetre = f.findChild<FenetreReglages*>(QStringLiteral("fenetreReglages"));
    auto* doigts = fenetre ? fenetre->findChild<QComboBox*>(QStringLiteral("deuxDoigts")) : nullptr;
    if (doigts) doigts->setCurrentIndex(static_cast<int>(ReglagesNavigation::DeuxDoigts::Defiler));
    const bool regle = Navigation::instance().reglages().deuxDoigts == ReglagesNavigation::DeuxDoigts::Defiler;
    if (fenetre) fenetre->close();
    reel::activer(&f);
    // Un glisser à deux doigts sur un pavé tactile, reconnu à son type d'appareil
    QPointingDevice pave(QStringLiteral("Pavé d’essai"), 4242, QInputDevice::DeviceType::TouchPad, QPointingDevice::PointerType::Finger,
                         QInputDevice::Capability::Position | QInputDevice::Capability::Scroll | QInputDevice::Capability::PixelScroll, 2, 0);
    auto deuxDoigts = [&](QPoint pixels) {
      const QPointF p(vc->width() / 2.0, vc->height() / 2.0);
      QWheelEvent ev(p, vc->mapToGlobal(p), pixels, pixels * 2, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false, Qt::MouseEventNotSynthesized, &pave);
      QApplication::sendEvent(vc, &ev);
    };
    const Pt p0{0, 0}, p1{10'000'000, 0};
    auto pixelsPour10mm = [&] { return ecran(vc, p1).x() - ecran(vc, p0).x(); };
    const int e0 = pixelsPour10mm();
    const QPoint o0 = ecran(vc, p0);
    deuxDoigts(QPoint(0, 40));
    const bool defile = pixelsPour10mm() == e0 && ecran(vc, p0) == o0 + QPoint(0, 40);
    ReglagesNavigation zoomer = Navigation::instance().reglages();
    zoomer.deuxDoigts = ReglagesNavigation::DeuxDoigts::Zoomer;
    Navigation::instance().setReglages(zoomer);
    deuxDoigts(QPoint(0, 40));
    const bool zoome = pixelsPour10mm() != e0;
    verifier(menuReglages && fenetre && doigts && regle && defile && zoome,
             "ERG-19 : glisser à deux doigts réglé sur « faire défiler » dans les réglages : l'échelle ne change pas ; sur « zoomer », elle change", {});

    // Un tracé rapide vers la gauche avec le bouton des gestes (droit avec Ctrl, par défaut) : un pas annulé
    vc->recadrer();
    attendre(80);
    const Boite carte = boite(conception(doc)->circuit.contour.polygone());
    const int pas0 = doc->positionHistorique();
    doc->modifierConception(0, [&](ConceptionElec& c2) { c2.circuit.vias.push_back({nouvelUuid(), "", carte.centre(), 600'000, 300'000}); });
    attendre(100);
    const int pas1 = doc->positionHistorique();
    const QPoint centre = vc->rect().center();
    reel::deplacer(vc, centre, Qt::ControlModifier);
    reel::appuyer(vc, centre, Qt::RightButton, Qt::ControlModifier);
    reel::deplacer(vc, centre - QPoint(60, 0), Qt::ControlModifier);
    reel::relacher(vc, centre - QPoint(60, 0), Qt::RightButton, Qt::ControlModifier);
    attendre(100);
    verifier(pas1 == pas0 + 1 && doc->positionHistorique() == pas0 && !vc->menuMarquesVisible(),
             "ERG-20 : un tracé rapide vers la gauche avec le bouton des gestes annule un pas, sans attendre le menu", {});

    // Le bouton droit tenu immobile montre le menu à marques ; relâché vers le bas : Tout voir
    vc->zoomer(4);
    attendre(50);
    reel::deplacer(vc, centre);
    reel::appuyer(vc, centre, Qt::RightButton);
    attendre(NavigationVue::ATTENTE_MENU + 150);
    const bool menu = vc->menuMarquesVisible();
    reel::deplacer(vc, centre + QPoint(0, 60));
    reel::relacher(vc, centre + QPoint(0, 60), Qt::RightButton);
    attendre(100);
    const bool cadree = vc->rect().contains(ecran(vc, Pt{carte.x0, carte.y0})) && vc->rect().contains(ecran(vc, Pt{carte.x1, carte.y1}));
    verifier(menu && !vc->menuMarquesVisible() && cadree, "ERG-20 : le bouton droit tenu immobile montre le menu à marques ; relâché vers le bas, Tout voir", {});

    // Pendant un cadre, le pointeur dans la bande du bord fait défiler la vue
    const QPoint departCadre(std::max(30, ecran(vc, Pt{carte.x0, carte.y0}).x() - 10), vc->height() / 2);
    const QPoint avantBord = ecran(vc, p0);
    reel::deplacer(vc, departCadre);
    reel::appuyer(vc, departCadre);
    reel::deplacer(vc, departCadre + QPoint(-6, 0));
    reel::deplacer(vc, QPoint(3, vc->height() / 2));
    attendre(250);
    const bool auBord = ecran(vc, p0).x() > avantBord.x();
    reel::relacher(vc, QPoint(3, vc->height() / 2));
    attendre(50);
    vc->echap();
    verifier(auBord, "ERG-19 : pendant un cadre, le pointeur au bord fait défiler la vue", {});

    // F1 donne la carte du menu à marques
    a->montrerRaccourcis();
    attendre(100);
    const bool carteF1 = a->listeRaccourcis() && a->listeRaccourcis()->note().contains(QStringLiteral("Menu à marques")) &&
                         a->listeRaccourcis()->note().contains(QStringLiteral("← Annuler"));
    if (a->listeRaccourcis()) a->listeRaccourcis()->hide();
    verifier(carteF1, "ERG-20 : F1 donne la carte du menu à marques", {});
    Navigation::instance().setReglages(initiaux);
    reel::activer(&f);
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.20 · Ruban décrit par un fichier, édité par glisser ; suites de touches, menus au curseur, premier
  // appui, bascules annoncées (ERG-22, ERG-03) ----------
  {
    const RegistreActions& registre = RegistreActions::atelierElectronique();
    QTabWidget* ruban = nullptr;
    for (QTabWidget* t : a->findChildren<QTabWidget*>())
      if (t->count() == 4 && t->tabText(0) == QStringLiteral("Schéma")) ruban = t;
    auto entreesRuban = [&](int onglet, const QString& id) {
      int n = 0;
      if (ruban && onglet < ruban->count())
        for (QAction* x : ruban->widget(onglet)->findChildren<QAction*>())
          n += actionsMarquees(x).contains(id);
      return n;
    };
    // Le fichier du ruban ne cite que des identifiants du registre, et chacun a son entrée dans le ruban construit
    const DescriptionRuban original = RubanEnCours::original();
    int citees = 0, construites = 0;
    for (std::size_t i = 0; i < original.onglets.size(); ++i)
      for (const auto& g : original.onglets[i].groupes)
        for (const std::string& id : g.actions) {
          ++citees;
          const ActionAtelier* x = registre.action(id);
          if (x && x->controle) {
            for (QObject* o : ruban ? ruban->widget(static_cast<int>(i))->findChildren<QObject*>() : QList<QObject*>()) construites += actionsMarquees(o).contains(QString::fromStdString(id)) ? 1 : 0;
          } else {
            construites += entreesRuban(static_cast<int>(i), QString::fromStdString(id)) > 0;
          }
        }
    verifier(original.onglets.size() == 4 && original.inconnues(registre).isEmpty() && citees > 60 && construites >= citees && registre.conflits().isEmpty(),
             "ERG-22 : le ruban est construit d'après un fichier qui ne cite que des identifiants du registre ; aucune touche en conflit",
             std::to_string(citees) + " entrées citées, " + std::to_string(construites) + " construites");

    // Dans les réglages, « Poser une via » glissé dans le groupe Modifier du circuit : gardé au redémarrage
    QAction* menuReglages = f.findChild<QAction*>(QStringLiteral("menu:reglages"));
    if (menuReglages) reel::touche(&f, Qt::Key_Comma, Qt::ControlModifier, QStringLiteral(","));  // Ctrl+, : Fichier › Réglages…
    attendre(150);
    auto* fenetre = f.findChild<FenetreReglages*>(QStringLiteral("fenetreReglages"));
    if (fenetre) fenetre->montrerPage(QStringLiteral("page:ruban"));
    auto* editeur = fenetre ? fenetre->findChild<EditeurRuban*>() : nullptr;
    ArbreRuban* arbre = editeur ? editeur->arbre() : nullptr;
    auto* disponibles = editeur ? editeur->findChild<QListWidget*>(QStringLiteral("actionsDisponibles")) : nullptr;
    const bool glissable = arbre && disponibles && disponibles->dragEnabled() && arbre->acceptDrops() && arbre->dragDropMode() == QAbstractItemView::DragDrop;
    const int viasAvant = entreesRuban(AtelierElectronique::Circuit, QStringLiteral("circuit.via"));
    const bool depose = arbre && arbre->deposer(QStringLiteral("circuit.via"), arbre->groupe(QStringLiteral("Circuit imprimé"), QStringLiteral("Modifier")));
    attendre(150);
    const int viasApres = entreesRuban(AtelierElectronique::Circuit, QStringLiteral("circuit.via"));
    const bool garde = QSettings().contains(QStringLiteral("ruban/electronique"));
    RubanEnCours::instance().relire();  // le redémarrage relit les réglages
    bool apresRedemarrage = false;
    for (const auto& o : RubanEnCours::instance().description().onglets)
      for (const auto& g : o.groupes)
        if (o.id == "circuit" && g.titre == QStringLiteral("Modifier"))
          for (const std::string& id : g.actions) apresRedemarrage |= id == "circuit.via";
    verifier(glissable && depose && viasApres == viasAvant + 1 && garde && apresRedemarrage,
             "ERG-22 : le bouton « Via » glissé dans le ruban y paraît aussitôt, et reste au redémarrage",
             std::to_string(viasAvant) + " → " + std::to_string(viasApres) + " entrées Via");
    auto* original2 = editeur ? editeur->findChild<QPushButton*>(QStringLiteral("rubanOriginal")) : nullptr;
    if (original2) original2->click();
    attendre(150);
    verifier(original2 && RubanEnCours::instance().description() == original && !QSettings().contains(QStringLiteral("ruban/electronique")) &&
                 entreesRuban(AtelierElectronique::Circuit, QStringLiteral("circuit.via")) == viasAvant,
             "ERG-22 : « Rétablir l'original » rend le ruban livré", {});

    // P puis V au circuit : la première touche attend, ses suites annoncées près du curseur ; V arme la via
    if (fenetre) fenetre->close();
    reel::activer(&f);
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(250);
    vc->recadrer();
    attendre(100);
    vc->setOutil(VueCircuit::Outil::Selection);
    sel->neutre();
    const Boite carte = boite(conception(doc)->circuit.contour.polygone());
    // Un point de la carte loin de tout cuivre, pour une via posée au curseur
    Pt libre = carte.centre();
    bool trouve = false;
    for (Nm dx = -20'000'000; dx <= 20'000'000 && !trouve; dx += 2'540'000)
      for (Nm dy = -15'000'000; dy <= 15'000'000 && !trouve; dy += 2'540'000) {
        const Pt p = carte.centre() + Pt{dx, dy};
        bool ok = dedans(conception(doc)->circuit.contour.polygone(), p) && vc->rect().adjusted(40, 40, -40, -40).contains(ecran(vc, p));
        for (const auto& x : a->calculs().pastilles) ok &= distance(x.pastille.position, p) > 3'000'000;
        for (const auto& t : conception(doc)->circuit.pistes) ok &= distancePointSegment(p, t.a, t.b) > 2'000'000;
        for (const auto& v : conception(doc)->circuit.vias) ok &= distance(v.p, p) > 2'500'000;
        if (ok) {
          libre = p;
          trouve = true;
        }
      }
    reel::deplacer(vc, ecran(vc, libre));
    vc->setFocus();
    reel::touche(vc, Qt::Key_P, Qt::NoModifier, QStringLiteral("p"));
    const QString attente = vc->annonceClavier();
    reel::touche(vc, Qt::Key_V, Qt::NoModifier, QStringLiteral("v"));
    const bool arme = vc->outil() == VueCircuit::Outil::Via && vc->annonceClavier().isEmpty();
    vc->echap();
    verifier(attente.startsWith(QStringLiteral("P… ")) && attente.contains(QStringLiteral("V : Poser une via")) && arme,
             "ERG-03 : P puis V au circuit : la première touche s'annonce en attente, la seconde arme la via", attente.toStdString());
    // Premier appui « agir au curseur », choisi dans les réglages : P puis V pose la via là où est le curseur
    reel::activer(&f);
    if (menuReglages) reel::touche(&f, Qt::Key_Comma, Qt::ControlModifier, QStringLiteral(","));
    attendre(100);
    if (fenetre) fenetre->montrerPage(QStringLiteral("page:clavier"));
    auto* premier = fenetre ? fenetre->findChild<QComboBox*>(QStringLiteral("premierAppui")) : nullptr;
    if (premier) premier->setCurrentIndex(static_cast<int>(PremierAppui::AgirAuCurseur));
    if (fenetre) fenetre->close();
    reel::activer(&f);
    vc->setFocus();
    reel::deplacer(vc, ecran(vc, libre));
    const int pas0 = doc->positionHistorique();
    const std::size_t vias0 = conception(doc)->circuit.vias.size();
    reel::touche(vc, Qt::Key_P, Qt::NoModifier, QStringLiteral("p"));
    reel::touche(vc, Qt::Key_V, Qt::NoModifier, QStringLiteral("v"));
    attendre(100);
    bool viaAuCurseur = false;
    for (const auto& v : conception(doc)->circuit.vias) viaAuCurseur |= distance(v.p, libre) < 1'500'000;
    verifier(trouve && premier && conception(doc)->circuit.vias.size() == vias0 + 1 && viaAuCurseur && vc->outil() == VueCircuit::Outil::Selection,
             "ERG-03 : premier appui réglé sur « agir au curseur » : P puis V pose une via au circuit, là où est le curseur", {});
    while (doc->positionHistorique() > pas0) {
      doc->annuler();
      attendre(50);
    }
    setPremierAppui(PremierAppui::ArmerOutil);
    // Maj+A ouvre au curseur le menu Poser ; une ligne arme l'outil
    reel::touche(vc, Qt::Key_A, Qt::ShiftModifier, QStringLiteral("A"));
    attendre(80);
    QMenu* menuPoser = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
      if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible() && m->objectName() == QStringLiteral("menuTouche")) menuPoser = m;
    QAction* zone = nullptr;
    if (menuPoser)
      for (QAction* x : menuPoser->actions())
        if (actionsMarquees(x).contains(QStringLiteral("circuit.zone"))) zone = x;
    {
      const QPointer<QMenu> garde(menuPoser);
      cliquerLigne(menuPoser, zone);
      if (garde && garde->isVisible()) garde->close();
    }
    attendre(80);
    verifier(menuPoser && zone && vc->outil() == VueCircuit::Outil::Zone, "ERG-03 : Maj+A ouvre au curseur le menu Poser, dont une ligne arme l'outil", {});
    vc->echap();
    // Une bascule faite au clavier s'annonce près du curseur : / pendant le routage
    vc->setOutil(VueCircuit::Outil::Router);
    reel::touche(vc, Qt::Key_Slash, Qt::NoModifier, QStringLiteral("/"));
    const QString bascule = vc->annonceClavier();
    vc->echap();
    verifier(bascule.startsWith(QStringLiteral("Coude")), "ERG-03 : une bascule faite au clavier s'annonce près du curseur", bascule.toStdString());
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.21 · Panneaux ancrables et dispositions nommées ; bibliothèque à colonnes, triée, avec ses aperçus
  // (ERG-23, BIB-04) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema);
    a->montrerBibliotheque();
    attendre(150);
    using Lieu = DispositionsAtelier::Lieu;
    DispositionsAtelier* disp = a->dispositions();
    QMainWindow* fp = a->fenetrePanneaux();
    QDockWidget* dProjet = a->panneau(AtelierElectronique::Panneau::Projet);
    QDockWidget* dBib = a->panneau(AtelierElectronique::Panneau::Bibliotheque);
    QDockWidget* dIns = a->panneau(AtelierElectronique::Panneau::Inspecteur);
    QDockWidget* dMsg = a->panneau(AtelierElectronique::Panneau::Messages);
    disp->origine();
    attendre(150);
    // L'arbre du projet vient dans le panneau Projet de l'atelier
    auto* nav = f.findChild<Navigateur*>();
    const bool arbreIci = nav && dProjet->widget() == nav && nav->isVisible();
    const bool places = fp->dockWidgetArea(dProjet) == Qt::LeftDockWidgetArea && fp->dockWidgetArea(dBib) == Qt::LeftDockWidgetArea &&
                        fp->dockWidgetArea(dIns) == Qt::RightDockWidgetArea && !dIns->isFloating() && dBib->isVisible() && dIns->isVisible() && !dMsg->isVisible();
    verifier(arbreIci && places, "ERG-23 : panneaux sur une fenêtre imbriquée : projet (l'arbre de la fenêtre) et bibliothèque à gauche, inspecteur à droite, messages fermés",
             std::string(arbreIci ? "arbre dans le panneau" : "arbre ailleurs") + ", " + (places ? "places d'origine" : "places inattendues"));
    // Disposition « Routage » : au circuit, l'inspecteur détaché (un second écran), les messages ancrés à droite
    a->setMode(AtelierElectronique::Circuit);
    attendre(200);
    dIns->setFloating(true);
    attendre(80);
    dIns->setGeometry(70, 90, 380, 520);
    fp->addDockWidget(Qt::RightDockWidgetArea, dMsg);
    a->ouvrirMessages();
    attendre(250);
    const QRect geoIns = dIns->geometry();
    disp->enregistrer(QStringLiteral("Routage"), Lieu::PourMoi);
    disp->origine();
    attendre(200);
    const bool rendu = !dIns->isFloating() && fp->dockWidgetArea(dIns) == Qt::RightDockWidgetArea && !dMsg->isVisible();
    const bool rappele = disp->rappeler(QStringLiteral("Routage"), Lieu::PourMoi);
    attendre(250);
    const QRect geoApres = dIns->geometry();
    const bool revenu = rappele && dIns->isFloating() && dIns->isVisible() && (geoApres.topLeft() - geoIns.topLeft()).manhattanLength() <= 8 &&
                        std::abs(geoApres.width() - geoIns.width()) <= 8 && dMsg->isVisible() && fp->dockWidgetArea(dMsg) == Qt::RightDockWidgetArea && !dBib->isVisible() &&
                        disp->noms(Lieu::PourMoi).contains(QStringLiteral("Routage"));
    auto texteRect = [](const QRect& r) { return std::to_string(r.x()) + "," + std::to_string(r.y()) + " " + std::to_string(r.width()) + "×" + std::to_string(r.height()); };
    verifier(rendu && revenu, "ERG-23 : disposition « Routage » enregistrée (inspecteur détaché, messages à droite), l'origine rendue, puis rappelée : chaque panneau revient à sa place",
             "inspecteur " + texteRect(geoIns) + " → " + texteRect(geoApres) + (rendu ? "" : ", origine non rendue"));
    // Un panneau détaché suit l'atelier ; hors de l'atelier, l'arbre revient à gauche de la fenêtre
    f.montrerAtelierPiece(false);
    attendre(250);
    const bool insCache = !dIns->isVisible();
    const bool arbreRendu = nav && !a->isAncestorOf(nav) && nav->isVisible();
    f.montrerElectronique(0, AtelierElectronique::Circuit);
    attendre(300);
    const bool insRevenu = dIns->isVisible() && dIns->isFloating();
    const bool arbreRevenu = nav && dProjet->widget() == nav && nav->isVisible();
    verifier(insCache && arbreRendu && insRevenu && arbreRevenu, "ERG-23 : un panneau détaché est caché avec l'atelier et revient avec lui ; l'arbre retourne à gauche de la fenêtre hors de l'atelier",
             std::string(insCache ? "" : "inspecteur resté visible ; ") + (arbreRendu ? "" : "arbre non rendu ; ") + (insRevenu ? "" : "inspecteur non revenu ; ") +
                 (arbreRevenu ? "" : "arbre non revenu"));
    // Dans le projet (sans pas d'annulation, relue à l'ouverture) et dans un fichier qui se partage
    const int pas = doc->positionHistorique();
    disp->enregistrer(QStringLiteral("Routage"), Lieu::Projet);
    const bool dansProjet = doc->projet().dispositions.count("Routage") == 1 && doc->positionHistorique() == pas && disp->noms(Lieu::Projet).contains(QStringLiteral("Routage"));
    const bool relue = prom::normaliser(prom::versJson(doc->projet(), false)).dispositions == doc->projet().dispositions;
    const QString chemin = QDir::temp().filePath(QStringLiteral("promethee-disposition-essai.json"));
    const bool exportee = disp->exporter(chemin, QStringLiteral("Routage"));
    disp->origine();
    attendre(150);
    QString nomImporte;
    const bool importee = disp->importer(chemin, &nomImporte) && nomImporte == QStringLiteral("Routage");
    attendre(250);
    QFile fichier(chemin);
    const QJsonObject j = fichier.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(fichier.readAll()).object() : QJsonObject();
    fichier.close();
    verifier(dansProjet && relue && exportee && importee && dIns->isFloating() && j.value(QStringLiteral("format")).toString() == QStringLiteral("promethee-disposition"),
             "ERG-23 : une disposition se garde dans le projet (sans pas d'annulation, relue à l'ouverture) et dans un fichier .json qui la rend",
             std::string(dansProjet ? "" : "absente du projet ; ") + (relue ? "" : "perdue à la relecture ; ") + (exportee ? "" : "non exportée ; ") + (importee ? "" : "non importée"));
    disp->supprimer(QStringLiteral("Routage"), Lieu::Projet);
    disp->supprimer(QStringLiteral("Routage"), Lieu::PourMoi);
    QFile::remove(chemin);
    disp->origine();
    a->setMode(AtelierElectronique::Schema);
    attendre(200);
    // La croix du panneau de la bibliothèque la replie, comme son bouton
    dBib->close();
    attendre(80);
    const bool fermee = !dBib->isVisible() && !QSettings().value(QStringLiteral("electronique/bibliothequeOuverte"), true).toBool();
    a->montrerBibliotheque();
    attendre(100);
    verifier(fermee && dBib->isVisible() && disp->noms(Lieu::PourMoi).isEmpty() && disp->noms(Lieu::Projet).isEmpty(),
             "B5 : la croix du panneau replie la bibliothèque, qui se rouvre comme avant", {});

    // BIB-04 : tri alphabétique, une liste dans l'ordre des noms
    PanneauBibliotheque* bib = a->bibliotheque();
    QTreeWidget* arbre = bib->arbre();
    bib->recherche()->clear();
    bib->setTri(PanneauBibliotheque::Tri::Alphabetique);
    attendre(50);
    QStringList noms;
    bool liste = true;
    for (int i = 0; i < arbre->topLevelItemCount(); ++i) {
      const QTreeWidgetItem* it = arbre->topLevelItem(i);
      liste &= it->childCount() == 0 && it->data(0, Qt::UserRole).isValid();
      noms << it->text(0);
    }
    bool ordonne = true;
    for (int i = 1; i < noms.size(); ++i) ordonne &= QString::localeAwareCompare(noms[i - 1], noms[i]) <= 0;
    const QComboBox* tri = bib->findChild<QComboBox*>(QStringLiteral("triBibliotheque"));
    verifier(liste && ordonne && noms.size() > 20 && tri && tri->currentIndex() == static_cast<int>(PanneauBibliotheque::Tri::Alphabetique),
             "BIB-04 : tri alphabétique : une liste sans catégories, dans l'ordre des noms", std::to_string(noms.size()) + " article(s)");
    // Par pertinence : le nom exact en tête, accents et casse ignorés
    bib->setTri(PanneauBibliotheque::Tri::Pertinence);
    auto premierVisible = [arbre]() -> QString {
      for (int i = 0; i < arbre->topLevelItemCount(); ++i)
        if (!arbre->topLevelItem(i)->isHidden()) return arbre->topLevelItem(i)->text(0);
      return {};
    };
    bib->recherche()->setText(QStringLiteral("condensateur"));
    attendre(30);
    const QString tete1 = premierVisible();
    bib->recherche()->setText(QStringLiteral("RESISTANCE"));
    attendre(30);
    const QString tete2 = premierVisible();
    verifier(tete1 == QStringLiteral("Condensateur") && tete2 == QStringLiteral("Résistance") && QSettings().value(QStringLiteral("bibliotheque/tri")).toInt() == 1,
             "BIB-04 : tri par pertinence : « condensateur » met le Condensateur en tête, « RESISTANCE » trouve la Résistance", (tete1 + QStringLiteral(" · ") + tete2).toStdString());
    bib->recherche()->clear();
    bib->setTri(PanneauBibliotheque::Tri::Categorie);
    // Colonnes de champs choisies, gardées
    bib->setColonnes({QStringLiteral("empreinte"), QStringLiteral("broches")});
    attendre(30);
    QTreeWidgetItem* condensateur = nullptr;
    for (QTreeWidgetItemIterator it(arbre); *it; ++it)
      if ((*it)->text(0) == QStringLiteral("Condensateur")) condensateur = *it;
    const bool colonnes = arbre->columnCount() == 3 && arbre->headerItem()->text(1) == QStringLiteral("Empreinte") && arbre->headerItem()->text(2) == QStringLiteral("Broches") &&
                          condensateur && condensateur->text(1) == QStringLiteral("C_0603") && condensateur->text(2) == QStringLiteral("2") &&
                          QSettings().value(QStringLiteral("bibliotheque/colonnes")).toStringList() == QStringList({QStringLiteral("empreinte"), QStringLiteral("broches")});
    verifier(colonnes, "BIB-04 : colonnes de champs choisies (empreinte, broches), gardées d'une session à l'autre",
             condensateur ? (condensateur->text(1) + QStringLiteral(" · ") + condensateur->text(2)).toStdString() : std::string("Condensateur introuvable"));
    // Aperçus : symbole, empreinte, 3D, photo
    if (condensateur) arbre->setCurrentItem(condensateur);
    attendre(30);
    ApercuArticle* ap = bib->apercu();
    ap->setFace(ApercuArticle::Face::Symbole);
    const QImage symbole = ap->image().toImage();
    ap->setFace(ApercuArticle::Face::Empreinte);
    const QImage empreinte = ap->image().toImage();
    ap->setFace(ApercuArticle::Face::Vue3D);
    const QImage boitier = ap->image().toImage();
    ap->setFace(ApercuArticle::Face::Photo);
    const bool sansPhoto = ap->image().isNull() && ap->mention().contains(QStringLiteral("Pas de photo"));
    // Une photo donnée par la propriété « photo » ; l'aperçu se réutilise seul (fenêtre de choix, 4.5)
    const QString cheminPhoto = QDir::temp().filePath(QStringLiteral("promethee-photo-essai.png"));
    QImage image(120, 80, QImage::Format_RGB32);
    image.fill(QColor(0x33, 0x99, 0x66));
    image.save(cheminPhoto);
    ApercuArticle seul;
    DefSymbole avecPhoto = *symboleBibliotheque("C");
    avecPhoto.proprietes["photo"] = cheminPhoto.toStdString();
    seul.setFace(ApercuArticle::Face::Photo);
    seul.montrer(avecPhoto, nullptr);
    const bool photo = !seul.image().isNull();
    seul.setFace(ApercuArticle::Face::Empreinte);
    const bool sansEmpreinte = seul.image().isNull() && !seul.mention().isEmpty();
    QFile::remove(cheminPhoto);
    verifier(!symbole.isNull() && !empreinte.isNull() && !boitier.isNull() && symbole != empreinte && empreinte != boitier && sansPhoto && photo && sansEmpreinte &&
                 QSettings().value(QStringLiteral("bibliotheque/face")).toInt() == static_cast<int>(ApercuArticle::Face::Photo),
             "BIB-04 : l'aperçu montre le symbole, l'empreinte, la 3D et la photo (ou dit qu'il n'y en a pas) ; la face choisie est gardée", {});
    // Captures pour la relecture à l'œil (PROMETHEE_CAPTURES_ELECTRONIQUE : un dossier)
    if (const QByteArray d = qgetenv("PROMETHEE_CAPTURES_ELECTRONIQUE"); !d.isEmpty()) {
      const QDir dossier(QString::fromLocal8Bit(d));
      empreinte.save(dossier.filePath(QStringLiteral("apercu-empreinte.png")));
      boitier.save(dossier.filePath(QStringLiteral("apercu-3d.png")));
      ap->setFace(ApercuArticle::Face::Empreinte);
      f.grab().save(dossier.filePath(QStringLiteral("atelier-panneaux.png")));
    }
    ap->setFace(ApercuArticle::Face::Symbole);
    bib->setColonnes({});
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
  }
  // ---------- 1.22 · Enregistrer pour une version précédente : ce qui se perd d'abord, puis un fichier neuf (ECH-11) ----------
  {
    auto* menu = f.findChild<QAction*>(QStringLiteral("menu:pourVersionPrecedente"));
    const QString cheminAvant = doc->chemin();
    const bool modifieAvant = doc->modifie();
    const int pas = doc->positionHistorique();
    prom::Projet copie = doc->projet();
    copie.dispositions["Essai"] = "{}";
    DialogueVersionPrecedente d(copie, QDir::temp().filePath(QStringLiteral("carte.prom")));
    auto* versions = d.findChild<QComboBox*>(QStringLiteral("versionPrecedente"));
    // Proposée d'abord : la version juste avant la courante
    const bool parDefaut = versions && versions->currentData().toString() == QString::fromStdString(prom::versionsAnterieures().front());
    d.setVersion(QStringLiteral("0.29"));
    const QStringList pertes29 = d.pertes();
    d.setVersion(QStringLiteral("0.28"));
    const QStringList pertes28 = d.pertes();
    d.setVersion(QStringLiteral("0.29"));
    const QString chemin = QDir::temp().filePath(QStringLiteral("promethee-essai.pour-0-29.prom"));
    const bool ecrit = d.ecrire(chemin);
    QFile fichier(chemin);
    const QJsonObject j = fichier.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(fichier.readAll()).object() : QJsonObject();
    fichier.close();
    QFile::remove(chemin);
    const QJsonObject carte = j.value(QStringLiteral("electronique")).toArray().first().toObject();
    const bool pour029 = !j.contains(QStringLiteral("dispositions")) && carte.value(QStringLiteral("formatElectronique")).toInt() == 2 && !carte.contains(QStringLiteral("format"));
    const bool dites = std::any_of(pertes29.begin(), pertes29.end(), [](const QString& x) { return x.contains(QStringLiteral("disposition")); }) && pertes28.size() >= pertes29.size();
    verifier(menu && versions && versions->count() == static_cast<int>(prom::versionsAnterieures().size()) && parDefaut && dites && ecrit && pour029 &&
                 d.cheminPropose().endsWith(QStringLiteral("carte.pour-0-29.prom")) && doc->chemin() == cheminAvant && doc->modifie() == modifieAvant && doc->positionHistorique() == pas,
             "ECH-11 : « Enregistrer pour une version précédente… » dit ce qui se perd (la 0.29, puis la 0.28), écrit un fichier neuf pour elle, le projet ouvert inchangé",
             pertes29.join(QStringLiteral(" | ")).toStdString());
  }
  // ---------- 1.14 · Menus selon l'atelier, import KiCad de l'arbre, schéma seul, Ctrl+3, renommer et supprimer sur
  // place, dernier atelier rouvert (G1, G4, G6, G7, STR-13) ----------
  {
    auto actionNommee = [&f](const char* nom) { return f.findChild<QAction*>(QString::fromLatin1(nom)); };
    // Les touches des menus, sans doublon parmi les entrées montrées
    auto doublons = [&f] {
      QStringList vues, deux;
      std::function<void(QMenu*)> parcourir = [&](QMenu* m) {
        for (QAction* x : m->actions()) {
          if (!x->isVisible()) continue;
          if (x->menu()) parcourir(x->menu());
          for (const QKeySequence& k : x->shortcuts()) {
            const QString t = k.toString(QKeySequence::PortableText);
            if (vues.contains(t)) deux << t;
            vues << t;
          }
        }
      };
      for (QMenu* m : f.menusPrincipaux())
        if (m->menuAction()->isVisible()) parcourir(m);
      return deux;
    };
    QAction* importMeca = actionNommee("menu:importKicadMeca");
    QAction* importElec = actionNommee("menu:importKicadElec");
    QAction* dossierMeca = actionNommee("menu:dossierMeca");
    QAction* dossierElec = actionNommee("menu:dossierElec");
    QAction* toutChoisir = actionNommee("menu:selection.tout");
    QMenu* ajouterElec = f.findChild<QMenu*>(QStringLiteral("menuAjouterElectronique"));
    const QKeySequence ctrlI(Qt::CTRL | Qt::Key_I), ctrlE(Qt::CTRL | Qt::Key_E);
    a->setMode(AtelierElectronique::Schema);
    attendre(100);
    verifier(importMeca && importElec && dossierMeca && dossierElec && toutChoisir && ajouterElec && !importMeca->isVisible() && importElec->isVisible() &&
                 importElec->shortcut() == ctrlI && !dossierMeca->isVisible() && dossierElec->isVisible() && dossierElec->shortcut() == ctrlE &&
                 ajouterElec->menuAction()->isVisible() && toutChoisir->isVisible() && doublons().isEmpty(),
             "G1 : dans l'atelier électronique, Fichier, Édition, Ajouter et Affichage sont les siens ; Ctrl+I et Ctrl+E y importent et exportent l'électronique, sans doublon",
             doublons().join(QLatin1Char(' ')).toStdString());
    sel->vider();
    vs->setFocus(Qt::OtherFocusReason);
    if (toutChoisir) reel::touche(vs, Qt::Key_A, Qt::ControlModifier, QStringLiteral("a"));  // Édition › Tout choisir
    attendre(80);
    verifier(sel->taille() > 3, "G1 : « Édition › Tout choisir » agit sur le schéma ouvert", std::to_string(sel->taille()) + " objet(s)");
    sel->vider();
    f.montrerAtelierPiece(false);
    attendre(150);
    verifier(importMeca && importMeca->isVisible() && importMeca->shortcut() == ctrlI && importElec->shortcut().isEmpty() && dossierMeca->isVisible() &&
                 dossierMeca->shortcut() == ctrlE && !dossierElec->isVisible() && !ajouterElec->menuAction()->isVisible() && doublons().isEmpty(),
             "G1 : hors de l'atelier électronique, les menus reviennent à la mécanique, Ctrl+I et Ctrl+E avec eux", doublons().join(QLatin1Char(' ')).toStdString());
    f.montrerElectronique(0, AtelierElectronique::Schema);
    attendre(200);

    // G4 : « Ajouter une carte KiCad… » de l'arbre lance l'import électronique
    auto* nav = f.findChild<Navigateur*>();
    QTreeWidgetItem* kicad = nullptr;
    if (nav)
      for (QTreeWidgetItemIterator it(nav); *it; ++it)
        if ((*it)->data(0, Navigateur::ROLE_CLE).toString() == QLatin1String("ajouter:kicad")) kicad = *it;
    int elec = 0, meca = 0;
    if (nav) {
      const auto c1 = QObject::connect(nav, &Navigateur::importerKicadElectronique, nav, [&elec] { ++elec; });
      const auto c2 = QObject::connect(nav, &Navigateur::importerKicad, nav, [&meca] { ++meca; });
      // La fenêtre de choix du fichier, refermée sans rien choisir
      QTimer::singleShot(400, [] {
        if (QWidget* m = QApplication::activeModalWidget()) m->close();
      });
      if (kicad) cliquerElementArbre(nav, kicad);
      attendre(150);
      QObject::disconnect(c1);
      QObject::disconnect(c2);
    }
    verifier(kicad && elec == 1 && meca == 0 && conception(doc) && doc->projet().electronique.size() == 1,
             "G4 : « Ajouter une carte KiCad… » de l'arbre importe vers l'électronique ; l'import pour le boîtier reste au menu Fichier", {});

    // G7 : renommer sur place, supprimer sans fenêtre ; l'un et l'autre s'annulent
    const int position = doc->positionHistorique();
    for (QTreeWidgetItemIterator it(nav); nav && *it; ++it)
      if ((*it)->data(0, Navigateur::ROLE_CLE).toString() == QLatin1String("ajouter:elec")) {
        cliquerElementArbre(nav, *it);
        break;
      }
    attendre(300);
    const bool ajoutee = doc->projet().electronique.size() == 2;
    reel::activer(&f);
    const bool edite = nav && nav->editerNomConception(1);
    attendre(100);
    QLineEdit* champNom = nav ? nav->findChild<QLineEdit*>() : nullptr;
    if (champNom) {
      champNom->selectAll();
      reel::taper(champNom, QStringLiteral("Alimentation"));
      reel::touche(champNom, Qt::Key_Return);
    }
    attendre(200);
    verifier(ajoutee && edite && champNom && QApplication::activeModalWidget() == nullptr && doc->projet().electronique.size() == 2 &&
                 doc->projet().electronique[1].nom == "Alimentation",
             "G7 : une carte se renomme sur place dans l'arbre, sans fenêtre", doc->projet().electronique.size() == 2 ? doc->projet().electronique[1].nom : std::string());
    QTreeWidgetItem* carte2 = nullptr;
    for (QTreeWidgetItemIterator it(nav); nav && *it; ++it)
      if ((*it)->data(0, Navigateur::ROLE_CLE).toString() == QLatin1String("elec:1")) carte2 = *it;
    if (carte2) menuElementArbre(nav, carte2, QStringLiteral("Supprimer la carte"));
    attendre(200);
    const bool supprimee = doc->projet().electronique.size() == 1 && QApplication::activeModalWidget() == nullptr && a->conception() == 0;
    doc->annuler();
    attendre(150);
    verifier(supprimee && doc->projet().electronique.size() == 2 && doc->projet().electronique[1].nom == "Alimentation",
             "G7 : une carte se supprime sans fenêtre modale, et Ctrl+Z la rend", {});
    while (doc->positionHistorique() > position) {
      doc->annuler();
      attendre(60);
    }
    f.montrerElectronique(0, AtelierElectronique::Schema);
    attendre(150);

    // G7 : un fichier se rouvre sur le dernier atelier utilisé, ici le circuit imprimé
    const QString chemin = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-dernier-atelier-%1.prom").arg(QCoreApplication::applicationPid()));
    doc->enregistrer(chemin, true);
    f.montrerElectronique(0, AtelierElectronique::Circuit);
    attendre(200);
    doc->creer(prom::projetVide());
    f.montrerAtelierPiece(false);
    attendre(150);
    f.ouvrirFichier(chemin);
    attendre(500);
    a = f.atelierElectronique();
    verifier(a && a->isVisible() && a->mode() == AtelierElectronique::Circuit && doc->chemin() == chemin,
             "G7 : le projet se rouvre sur le dernier atelier utilisé (l'électronique, au circuit imprimé), pas sur la mécanique", {});

    // G6, STR-13 : « Schéma électronique » donne un schéma seul ; le circuit se crée au premier besoin
    libererOnglet();
    repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("schema-elec"));
    attendre(700);
    a = f.atelierElectronique();
    QTabBar* onglets = nullptr;
    if (a)
      for (QTabBar* t : a->findChildren<QTabBar*>())
        if (t->count() == 4) onglets = t;
    auto* creer = a ? a->findChild<QToolButton*>(QStringLiteral("creerCircuit")) : nullptr;
    bool arbreSansCircuit = nav != nullptr;
    for (QTreeWidgetItemIterator it(nav); nav && *it; ++it) arbreSansCircuit &= (*it)->data(0, Navigateur::ROLE_CLE).toString() != QLatin1String("elec:0:circuit");
    if (a) a->setMode(AtelierElectronique::Circuit);
    attendre(100);
    verifier(a && conception(doc) && conception(doc)->schemaSeul && onglets && !onglets->isTabVisible(AtelierElectronique::Circuit) &&
                 !onglets->isTabVisible(AtelierElectronique::Vue3D) && creer && creer->isVisible() && a->mode() == AtelierElectronique::Schema && arbreSansCircuit,
             "STR-13 : « Schéma électronique » ouvre un schéma seul, sans circuit, ni 3D, ni fabrication",
             std::to_string(a != nullptr) + std::to_string(conception(doc) && conception(doc)->schemaSeul) + std::to_string(onglets != nullptr) +
                 std::to_string(onglets && !onglets->isTabVisible(AtelierElectronique::Circuit)) + std::to_string(creer != nullptr) + std::to_string(creer && creer->isVisible()) +
                 std::to_string(a && a->mode() == AtelierElectronique::Schema) + std::to_string(arbreSansCircuit));
    if (creer) reel::cliquer(creer, creer->rect().center());
    attendre(600);
    if (a) a->setMode(AtelierElectronique::Circuit);
    attendre(100);
    verifier(a && conception(doc) && !conception(doc)->schemaSeul && onglets && onglets->isTabVisible(AtelierElectronique::Circuit) && a->mode() == AtelierElectronique::Circuit &&
                 creer && !creer->isVisible(),
             "STR-13 : le bouton « Circuit » crée le circuit imprimé, et ses onglets paraissent", {});

    // G7 : Ctrl+3 dans un projet sans carte ouvre l'accueil électronique, sans rien créer ; le choix s'y ajoute
    libererOnglet();
    repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("piece"));
    attendre(800);
    QAction* ctrl3 = nullptr;
    for (QAction* x : f.actions())
      if (x->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_3)) ctrl3 = x;
    const std::size_t pieces = doc->projet().pieces.size();
    const int pas = doc->positionHistorique();
    reel::activer(&f);
    if (ctrl3) reel::touche(&f, Qt::Key_3, Qt::ControlModifier, QStringLiteral("3"));
    attendre(200);
    QWidget* accueil = nullptr;
    for (QWidget* w : f.findChildren<QWidget*>())
      if (QString::fromLatin1(w->metaObject()->className()) == QLatin1String("Accueil")) accueil = w;
    verifier(ctrl3 && pieces > 0 && doc->projet().electronique.empty() && doc->positionHistorique() == pas && accueil && accueil->isVisible(),
             "G7 : Ctrl+3 dans un projet sans carte ouvre l'accueil électronique, sans rien créer ni empiler", {});
    f.choixAccueilPublic(QStringLiteral("carte-nouvelle"));
    attendre(600);
    verifier(QApplication::activeModalWidget() == nullptr && doc->projet().pieces.size() == pieces && doc->projet().electronique.size() == 1 && f.atelierElectronique() &&
                 f.atelierElectronique()->isVisible(),
             "G7 : la carte choisie depuis cet accueil s'ajoute au projet ouvert, ses pièces gardées", {});
    QFile::remove(chemin);
  }
  verifier(reel::pointeur() == pointeurAvant, "ERG-18 : aucun geste n'a déplacé le pointeur", {});
}
