// Audit indépendant de l'atelier électronique (dans la vraie fenêtre). Chaque essai imprime « DÉFAUT » ou « OK ».
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <memory>

#include <QInputDialog>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EssaisInterface.hpp"
#include "FenetrePrincipale.hpp"
#include "Navigateur.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;

namespace {
int g_defauts = 0;
void noter(bool defaut, const std::string& id, const std::string& quoi) {
  std::printf("[%s] %s — %s\n", id.c_str(), defaut ? "DÉFAUT CONFIRMÉ" : "conforme", quoi.c_str());
  std::fflush(stdout);
  if (defaut) ++g_defauts;
}
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
void souris(QWidget* w, QEvent::Type t, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m = Qt::NoModifier) {
  QMouseEvent e(t, p, w->mapToGlobal(p), b, bs, m);
  QCoreApplication::sendEvent(w, &e);
}
// Accepte la prochaine boîte de saisie (choix d'un réseau…) dès qu'elle s'affiche ; son texte est rendu
void accepterSaisie(QString* texte = nullptr) {
  auto* t = new QTimer(qApp);
  t->setInterval(100);
  auto essais = std::make_shared<int>(0);
  QObject::connect(t, &QTimer::timeout, qApp, [t, essais, texte] {
    for (QWidget* w : QApplication::topLevelWidgets())
      if (auto* d = qobject_cast<QInputDialog*>(w); d && d->isVisible()) {
        if (texte) *texte = d->textValue();
        d->accept();
        t->stop();
        t->deleteLater();
        return;
      }
    if (++*essais > 40) { t->stop(); t->deleteLater(); }
  });
  t->start();
}
void cliquer(QWidget* w, QPointF p, Qt::KeyboardModifiers m = Qt::NoModifier) {
  souris(w, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton, m);
  souris(w, QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, m);
  souris(w, QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, m);
  attendre(60);
}
void glisser(QWidget* w, QPointF a, QPointF b) {
  souris(w, QEvent::MouseMove, a, Qt::NoButton, Qt::NoButton);
  souris(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
  for (int i = 1; i <= 8; ++i) souris(w, QEvent::MouseMove, a + (b - a) * (i / 8.0), Qt::NoButton, Qt::LeftButton);
  souris(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
  attendre(150);
}
void touche(QWidget* w, int k, Qt::KeyboardModifiers m = Qt::NoModifier, const QString& texte = {}) {
  QKeyEvent e(QEvent::KeyPress, k, m, texte);
  QCoreApplication::sendEvent(w, &e);
  attendre(80);
}
const ConceptionElec* conception(FenetrePrincipale& f, int i = 0) {
  const auto& e = f.document()->projet().electronique;
  return i >= 0 && i < static_cast<int>(e.size()) ? &e[static_cast<std::size_t>(i)] : nullptr;
}
Pt broche(const ConceptionElec& c, const std::string& symbole, const std::string& numero) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == symbole)
        if (const DefSymbole* d = c.symbole(s.symbole))
          for (const auto& b : brochesPlacees(s, *d))
            if (b.broche->numero == numero) return b.p;
  return {};
}
int compter(const std::vector<Violation>& v, const char* code) {
  int n = 0;
  for (const auto& x : v) n += x.code == code;
  return n;
}
std::string codes(const std::vector<Violation>& v) {
  std::map<std::string, int> m;
  for (const auto& x : v) m[x.code]++;
  std::string r;
  for (const auto& [k, n] : m) r += (r.empty() ? "" : ", ") + k + "×" + std::to_string(n);
  return r.empty() ? "aucune" : r;
}
}  // namespace

void lancerEssaisAudit(FenetrePrincipale& f) {
  installerVeilleBoites();
  Document* doc = f.document();
  // Répondeur automatique : toute boîte modale reçoit « Ne pas enregistrer » (ou Non, ou Annuler)
  auto* repondeur = new QTimer(&f);
  QObject::connect(repondeur, &QTimer::timeout, [] {
    QWidget* m = QApplication::activeModalWidget();
    if (auto* b = qobject_cast<QMessageBox*>(m)) {
      std::printf("  (boîte « %s » : %s)\n", b->windowTitle().toStdString().c_str(), b->text().left(120).toStdString().c_str());
      std::fflush(stdout);
      for (QMessageBox::ButtonRole r : {QMessageBox::DestructiveRole, QMessageBox::NoRole, QMessageBox::RejectRole, QMessageBox::AcceptRole})
        for (QAbstractButton* x : b->buttons())
          if (b->buttonRole(x) == r) { x->click(); return; }
    } else if (m) {
      std::printf("  (fenêtre modale « %s » fermée)\n", m->windowTitle().toStdString().c_str());
      std::fflush(stdout);
      m->close();
    }
  });
  repondeur->start(250);
  const QString dossier = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-audit"));
  QDir(dossier).removeRecursively();
  QDir().mkpath(dossier);
  qputenv("PROMETHEE_BIBLIOTHEQUE", QDir(dossier).filePath(QStringLiteral("bibliotheque.json")).toUtf8());

  // ---------- Carte neuve : deux résistances câblées ----------
  f.choixAccueilPublic(QStringLiteral("carte-nouvelle"));
  attendre(600);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) {
    std::printf("atelier introuvable\n");
    QApplication::exit(99);
    return;
  }
  VueSchema* vs = a->vueSchema();
  VueCircuit* vc = a->vueCircuit();
  doc->modifier([](prom::Projet& p) {
    auto& c = p.electronique[0];
    poserSymbole(c, 0, "R", ptMm(50.8, 50.8), 1);
    poserSymbole(c, 0, "R", ptMm(76.2, 50.8), 1);
    annoter(c, false);
  });
  attendre(200);
  const ConceptionElec* c = conception(f);
  const std::string s1 = c->feuilles[0].symboles[0].uuid, s2 = c->feuilles[0].symboles[1].uuid;
  vs->setOutil(VueSchema::Outil::Fil);
  vs->cliquerEn(broche(*c, s1, "2"));
  vs->cliquerEn(broche(*c, s2, "1"));
  touche(vs, Qt::Key_Escape);
  vs->setOutil(VueSchema::Outil::Selection);
  attendre(200);
  a->recalculer();
  c = conception(f);
  const std::string reseauAvant = a->calculs().k.nom(c->feuilles[0].symboles[0].composant, "2");
  const int libresAvant = compter(a->erc(), "ERC-10");
  const std::string membresAvant = std::to_string(a->calculs().k.reseaux[static_cast<std::size_t>(a->calculs().k.reseau(c->feuilles[0].symboles[0].composant, "2"))].broches.size());

  // U01 : tourner un symbole câblé décroche ses fils
  vs->selectionner({s1});
  touche(vs, Qt::Key_R, Qt::NoModifier, QStringLiteral("r"));
  attendre(200);
  a->recalculer();
  c = conception(f);
  const std::string reseauApres = a->calculs().k.nom(c->feuilles[0].symboles[0].composant, "2");
  const int r12 = a->calculs().k.reseau(c->feuilles[0].symboles[0].composant, "2");
  const std::size_t membresApres = r12 >= 0 ? a->calculs().k.reseaux[static_cast<std::size_t>(r12)].broches.size() : 0;
  noter(compter(a->erc(), "ERC-10") > libresAvant, "U01",
        "R1 câblé à R2 puis tourné (R) : le réseau de R1.2 passe de " + membresAvant + " à " + std::to_string(membresApres) + " broche(s) ; broches libres (ERC-10) " + std::to_string(libresAvant) + " → " +
            std::to_string(compter(a->erc(), "ERC-10")) + " ; ERC : " + codes(a->erc()) + " (le fil reste où il était)");
  doc->annuler();
  attendre(200);

  // U02 : copier-coller
  {
    vs->selectionner({s1});
    const std::size_t avant = conception(f)->feuilles[0].symboles.size();
    touche(vs, Qt::Key_C, Qt::ControlModifier);
    touche(vs, Qt::Key_V, Qt::ControlModifier);
    touche(vs, Qt::Key_D, Qt::ControlModifier);
    attendre(200);
    const std::size_t apres = conception(f)->feuilles[0].symboles.size();
    bool action = false;
    for (QAction* x : f.findChildren<QAction*>()) {
      const QString t = x->text().remove(QLatin1Char('&'));
      if (t.startsWith(QStringLiteral("Copier")) || t.startsWith(QStringLiteral("Coller")) || t.startsWith(QStringLiteral("Dupliquer"))) action = true;
    }
    noter(avant == apres && !action, "U02", "Ctrl+C, Ctrl+V, Ctrl+D sur un symbole choisi : " + std::to_string(avant) + " → " + std::to_string(apres) + " symboles ; action Copier/Coller dans les menus : " + (action ? "oui" : "non"));
  }

  // U03 : texte de schéma non modifiable
  {
    vs->echap();  // le collage de U02 suit la souris jusqu'au clic (2.13) : il est retiré d'abord
    attendre(50);
    const std::string id = nouvelUuid();
    doc->modifier([&](prom::Projet& p) { p.electronique[0].feuilles[0].textes.push_back({id, "Note de conception", ptMm(60, 80), 1'800'000}); });
    attendre(200);
    const std::string vise = vs->objetVise(vs->versEcran(ptMm(60, 80)));
    vs->cliquerEn(ptMm(60, 80));
    attendre(300);
    const bool choisi = vs->selection().count(id) > 0;
    bool champ = false;
    for (QLineEdit* e : a->findChildren<QLineEdit*>())
      if (e->isVisible() && e->text() == QStringLiteral("Note de conception")) champ = true;
    noter(!champ, "U03", std::string("texte « Note de conception » cliqué : choisi = ") + (choisi ? "oui" : "non") + " ; champ pour le modifier dans le panneau : " + (champ ? "oui" : "non") + (vise == id ? "" : " ; objet visé : " + (vise.empty() ? std::string("aucun") : vise)) + " ; " + std::to_string(vs->selection().size()) + " objet(s) choisi(s), outil " + std::to_string(static_cast<int>(vs->outil())));
  }

  // U04 : touche Y : même miroir que X
  {
    // Mesure par les broches : un miroir haut-bas garde leurs abscisses et retourne leurs ordonnées autour de leur
    // milieu ; un miroir gauche-droite fait l'inverse
    const Pt a1 = broche(*conception(f), s2, "1"), a2 = broche(*conception(f), s2, "2");
    vs->selectionner({s2});
    touche(vs, Qt::Key_Y, Qt::NoModifier, QStringLiteral("y"));
    attendre(150);
    const Pt b1 = broche(*conception(f), s2, "1"), b2 = broche(*conception(f), s2, "2");
    const Nm cya = (a1.y + a2.y) / 2, cyb = (b1.y + b2.y) / 2, cxa = (a1.x + a2.x) / 2, cxb = (b1.x + b2.x) / 2;
    const bool hautBas = b1.x - cxb == a1.x - cxa && b1.y - cyb == -(a1.y - cya) && b2.x - cxb == a2.x - cxa && b2.y - cyb == -(a2.y - cya);
    const bool gaucheDroite = a1.x != a2.x && b1.x - cxb == -(a1.x - cxa) && b1.y - cyb == a1.y - cya;
    noter(!hautBas || (gaucheDroite && a1.y != a2.y), "U04", std::string("Y sur R2 : broches retournées de haut en bas = ") + (hautBas ? "oui" : "non") + " ; de gauche à droite = " + (gaucheDroite ? "oui" : "non"));
    doc->annuler();
    attendre(150);
  }

  // ---------- Circuit imprimé ----------
  a->mettreAJourCircuit();
  attendre(400);
  doc->modifier([](prom::Projet& p) {
    auto& ce = p.electronique[0];
    ce.circuit.contour.largeur = nm(40);
    ce.circuit.contour.hauteur = nm(30);
    // Rangées par repère, pas par ordre d'arrivée : R1 et R2, câblés en U01, sont sur la carte quel que soit l'endroit
    // où le collage de U02 a posé les copies (il suit la souris, donc la taille du canevas)
    std::vector<EmpreintePlacee*> rangees;
    for (auto& e : ce.circuit.empreintes) rangees.push_back(&e);
    auto repere = [&ce](const EmpreintePlacee* e) {
      const ComposantElec* k = ce.composant(e->composant);
      return k ? k->reference : std::string();
    };
    std::stable_sort(rangees.begin(), rangees.end(), [&](const EmpreintePlacee* x, const EmpreintePlacee* y) { return repere(x) < repere(y); });
    int i = 0;
    for (EmpreintePlacee* e : rangees) e->position = ptMm(12 + 14 * (i++), 15);
  });
  attendre(300);
  vc->recadrer();
  attendre(200);

  // U05 : outil Zone : réseau GND imposé même s'il n'existe pas
  {
    const std::size_t zonesAvant = conception(f)->circuit.zones.size();
    vc->setCoucheActive(Couche::CuivreDessous);
    vc->setOutil(VueCircuit::Outil::Zone);
    for (Pt q : {ptMm(1, 1), ptMm(39, 1), ptMm(39, 29), ptMm(1, 29)}) cliquer(vc, vc->versEcran(q));
    accepterSaisie();  // proposition acceptée
    QMouseEvent dbl(QEvent::MouseButtonDblClick, vc->versEcran(ptMm(1, 29)), vc->mapToGlobal(vc->versEcran(ptMm(1, 29))), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(vc, &dbl);
    attendre(400);
    a->recalculer();
    c = conception(f);
    const bool gndExiste = a->calculs().k.indice("GND") >= 0;
    noter(!c->circuit.zones.empty() && c->circuit.zones.back().reseau == "GND" && !gndExiste, "U05",
          "zone dessinée sur une carte sans réseau GND : réseau de la zone « " + (c->circuit.zones.empty() ? std::string("?") : c->circuit.zones.back().reseau) + " » ; DRC : " + codes(a->calculs().drc));
    if (c->circuit.zones.size() > zonesAvant) doc->annuler();
    attendre(200);
  }

  // U06 : outil Via cliqué dans le vide : via sans réseau
  {
    vc->setOutil(VueCircuit::Outil::Via);
    cliquer(vc, vc->versEcran(ptMm(20, 25)));
    attendre(300);
    c = conception(f);
    // Le panneau de la via offre-t-il le choix de son réseau ?
    bool choixReseau = false;
    vc->setOutil(VueCircuit::Outil::Selection);
    if (!c->circuit.vias.empty()) {
      vc->choisir(VueCircuit::Genre::Via, c->circuit.vias.back().uuid);
      attendre(300);
      for (QComboBox* b : a->findChildren<QComboBox*>())
        if (b->isVisible() && b->findText(QStringLiteral("— aucun —")) >= 0) choixReseau = true;
    }
    noter(!c->circuit.vias.empty() && c->circuit.vias.back().reseau.empty() && !choixReseau, "U06",
          "via posée dans le vide : réseau « " + (c->circuit.vias.empty() ? std::string("?") : c->circuit.vias.back().reseau) + " » ; choix du réseau dans le panneau : " + (choixReseau ? "oui" : "non"));
    doc->annuler();
    attendre(200);
  }

  // U07 : routage interactif : départ au milieu d'une piste refusé
  {
    a->routerAutomatiquement();
    attendre(500);
    QWidget* dlg = QApplication::activeModalWidget();
    if (dlg) dlg->close();
    for (QWidget* w : QApplication::topLevelWidgets())
      if (w->windowTitle() == QStringLiteral("Routage incomplet")) w->close();
    c = conception(f);
    if (!c->circuit.pistes.empty()) {
      const Piste p = c->circuit.pistes.front();
      const Pt milieu{(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2};
      const std::size_t avant = c->circuit.pistes.size();
      vc->setCoucheActive(p.couche);
      const bool ok = vc->router({milieu, {milieu.x, milieu.y + nm(5)}});
      noter(!ok, "U07", std::string("piste démarrée au milieu d'une piste existante : ") + (ok ? "acceptée" : "refusée") + " (" + std::to_string(avant) + " → " + std::to_string(conception(f)->circuit.pistes.size()) + " pistes)");
      if (ok) {
        doc->annuler();  // la piste d'essai finit dans le vide : on la retire avant U08
        attendre(200);
      }
    } else
      noter(true, "U07", "aucune piste posée par le routeur");
  }

  // U08 : déplacer une empreinte routée : les pistes ne suivent pas
  {
    vc->setOutil(VueCircuit::Outil::Selection);
    vc->choisir(VueCircuit::Genre::Rien, {});
    a->recalculer();
    const int avant = static_cast<int>(a->calculs().drc.size());
    const int coupuresAvant = compter(a->calculs().drc, "DRC-22") + compter(a->calculs().drc, "DRC-20");
    c = conception(f);
    const EmpreintePlacee e = c->circuit.empreintes.front();
    std::printf("  (U08) %zu pistes, empreinte en (%.2f ; %.2f)\n", c->circuit.pistes.size(), mm(e.position.x), mm(e.position.y));
    glisser(vc, vc->versEcran(e.position), vc->versEcran(e.position + ptMm(0, 6)));
    attendre(300);
    a->recalculer();
    for (const auto& x : conception(f)->circuit.empreintes)
      if (x.composant == e.composant) std::printf("  (U08) après glissement : (%.2f ; %.2f)\n", mm(x.position.x), mm(x.position.y));
    noter(compter(a->calculs().drc, "DRC-22") + compter(a->calculs().drc, "DRC-20") > coupuresAvant, "U08",
          "empreinte routée glissée de 6 mm : DRC " + std::to_string(avant) + " → " + codes(a->calculs().drc) + " (pas de glisser avec pistes attachées)");
    doc->annuler();
    attendre(200);
  }

  // U09 : 4 couches → 2 couches : objets internes invisibles mais gardés, et sélectionnables
  {
    doc->modifier([](prom::Projet& p) {
      auto& ce = p.electronique[0];
      ce.circuit.empilage.couchesCuivre = 4;
      ce.circuit.pistes.push_back({nouvelUuid(), "", Couche::CuivreInt1, ptMm(5, 27), ptMm(35, 27), nm(0.3)});
    });
    attendre(200);
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(200);
    vc->choisir(VueCircuit::Genre::Rien, {});
    attendre(200);
    QComboBox* couches = nullptr;
    for (QComboBox* cb : a->findChildren<QComboBox*>())
      if (cb->isVisible() && cb->findText(QStringLiteral("2 couches")) >= 0) couches = cb;
    if (couches) couches->setCurrentIndex(couches->findText(QStringLiteral("2 couches")));
    attendre(300);
    c = conception(f);
    int internes = 0;
    for (const auto& p : c->circuit.pistes) internes += p.couche == Couche::CuivreInt1;
    vc->setOutil(VueCircuit::Outil::Selection);
    cliquer(vc, vc->versEcran(ptMm(20, 27)));
    attendre(200);
    noter(c->circuit.empilage.couchesCuivre == 2 && internes > 0, "U09",
          "passage à 2 couches par le panneau : " + std::to_string(internes) + " piste(s) sur In1.Cu gardée(s) sans avertissement ; un clic à son emplacement choisit : " +
              (vc->genreChoisi() == VueCircuit::Genre::Piste ? "la piste invisible" : "autre chose"));
    doc->annuler();
    doc->annuler();
    attendre(200);
  }

  // U10 : menu « Couches visibles »
  {
    QStringList l;
    for (QToolButton* t : a->findChildren<QToolButton*>())
      if (t->text() == QStringLiteral("Couches visibles") && t->menu())
        for (QAction* x : t->menu()->actions()) l << x->text();
    const bool manque = !l.join(QLatin1Char(',')).contains(QStringLiteral("Vernis")) && !l.join(QLatin1Char(',')).contains(QStringLiteral("Encombrement"));
    noter(manque, "U10", "couches proposées à l'affichage : " + l.join(QStringLiteral(", ")).toStdString() + " (ni vernis, ni pâte, ni encombrement, ni fabrication, ni contour)");
  }

  // U11 : panneau du composant plus large que sa colonne (champs coupés)
  {
    f.choixAccueilPublic(QStringLiteral("exemple-elec"));
    attendre(800);
    a = f.atelierElectronique();
    a->ouvrir(static_cast<int>(doc->projet().electronique.size()) - 1, AtelierElectronique::Schema);
    attendre(300);
    const ConceptionElec* ex = &doc->projet().electronique.back();
    std::string u1;
    for (const auto& s : ex->feuilles[0].symboles)
      if (const auto* x = ex->composant(s.composant); x && x->reference == "U1") u1 = s.uuid;
    a->montrerComposant(ex->parReference("U1")->uuid);
    attendre(400);
    QWidget* page = nullptr;
    for (QWidget* w : a->findChildren<QWidget*>())
      if (w->objectName() == QStringLiteral("panneauElectronique") && w->isVisible()) page = w;
    QScrollArea* zone = nullptr;
    for (QScrollArea* s : a->findChildren<QScrollArea*>())
      if (s->isVisible() && s->widget() == page) zone = s;
    if (page && zone)
      noter(page->minimumSizeHint().width() > zone->viewport()->width(), "U11",
            "panneau de U1 : largeur minimale du contenu " + std::to_string(page->minimumSizeHint().width()) + " px pour " + std::to_string(zone->viewport()->width()) +
                " px visibles (défilement horizontal désactivé : le bord droit est coupé)");
    else
      noter(true, "U11", "panneau introuvable");
  }

  // U12 : temps de réaction : une modification de valeur sur l'exemple, puis sur une grande carte
  {
    using H = std::chrono::steady_clock;
    auto t0 = H::now();
    a->recalculer();
    const double tEx = std::chrono::duration<double, std::milli>(H::now() - t0).count();
    std::string tGrand = "non mesuré";
    const char* chemin = std::getenv("AUDIT_GRANDE_CARTE");
    if (chemin) {
      std::ifstream in(chemin);
      std::string js((std::istreambuf_iterator<char>(in)), {});
      if (!js.empty()) {
        const ConceptionElec g = conceptionDepuisJson(nlohmann::ordered_json::parse(js));
        doc->modifier([&](prom::Projet& p) { p.electronique.push_back(g); });
        attendre(200);
        a->ouvrir(static_cast<int>(doc->projet().electronique.size()) - 1, AtelierElectronique::Circuit);
        attendre(200);
        const std::string id = doc->projet().electronique.back().composants.front().uuid;
        t0 = H::now();
        doc->modifier([&](prom::Projet& p) {
          for (auto& x : p.electronique.back().composants)
            if (x.uuid == id) x.valeur = "22k";
        });
        const double tDoc = std::chrono::duration<double, std::milli>(H::now() - t0).count();
        t0 = H::now();
        a->recalculer();
        const double tAt = std::chrono::duration<double, std::milli>(H::now() - t0).count();
        char t[200];
        std::snprintf(t, sizeof t, "grande carte (%zu composants) : document %.0f ms + atelier %.0f ms", doc->projet().electronique.back().composants.size(), tDoc, tAt);
        tGrand = t;
      }
    }
    char t[300];
    std::snprintf(t, sizeof t, "recalcul de l'atelier sur l'exemple (13 composants) : %.0f ms ; %s", tEx, tGrand.c_str());
    noter(false, "U12", t);
  }

  // U13 : publier efface tout l'historique d'annulation, pas seulement la publication
  {
    int i = 0;
    for (std::size_t n = 0; n < doc->projet().electronique.size(); ++n)
      if (doc->projet().electronique[n].nom == "Clignotant 555") i = static_cast<int>(n);
    a->ouvrir(i, AtelierElectronique::Fabrication);
    attendre(300);
    doc->modifier([&](prom::Projet& p) { p.electronique[static_cast<std::size_t>(i)].cartouche.commentaire = "modif avant publication"; });
    attendre(200);
    const bool avant = doc->peutAnnuler();
    QString err;
    const bool ok = a->publier(dossier, QStringLiteral("audit"), QString(), &err);
    attendre(300);
    noter(avant && ok && !doc->peutAnnuler(), "U13", std::string("annulation possible avant publication : ") + (avant ? "oui" : "non") + " ; publication : " + (ok ? "faite" : err.toStdString()) +
                                                     " ; annulation possible après : " + (doc->peutAnnuler() ? "oui" : "non (tout l'historique du projet est effacé)"));
  }

  // U14 : bibliothèque personnelle illisible puis écrasée par le composant suivant
  {
    const QString chemin = QDir(dossier).filePath(QStringLiteral("perso-abimee.json"));
    qputenv("PROMETHEE_BIBLIOTHEQUE", chemin.toUtf8());
    // Trois composants personnels, puis le fichier coupé net (copie interrompue, synchronisation de dossier…)
    std::vector<DefSymbole> trois;
    for (const char* n : {"Capteur A", "Capteur B", "Capteur C"}) {
      SpecSymbole sp;
      sp.nom = n;
      sp.broches = lireListeBroches("1 VCC\n2 GND\n3 OUT out");
      trois.push_back(genererSymbole(sp));
    }
    nlohmann::ordered_json j = {{"format", "promethee-bibliotheque-electronique"}, {"version", 1}, {"symboles", nlohmann::ordered_json::array()}};
    for (const auto& d : trois) j["symboles"].push_back(symboleVersJson(d));
    std::string texte = j.dump(1);
    texte.resize(texte.size() - 40);
    {
      std::ofstream o(chemin.toStdString(), std::ios::binary);
      o << texte;
    }
    const std::size_t lus = AtelierElectronique::bibliothequePerso().size();
    SpecSymbole sp;
    sp.nom = "Capteur D";
    sp.broches = lireListeBroches("1 VCC\n2 GND");
    a->ajouterSymbolePerso(genererSymbole(sp));
    attendre(200);
    std::ifstream in(chemin.toStdString());
    const std::string apres((std::istreambuf_iterator<char>(in)), {});
    int presents = 0;
    for (const char* n : {"Capteur A", "Capteur B", "Capteur C", "Capteur D"}) presents += apres.find(n) != std::string::npos;
    noter(lus == 0 && presents == 1, "U14", "bibliothèque personnelle de 3 composants coupée net : lus " + std::to_string(lus) + " ; après la création d'un 4e composant, le fichier contient " +
                                              std::to_string(presents) + " composant(s)" + (presents <= 1 ? " : les anciens sont perdus" : " : ce qui était lisible est gardé"));
  }

  std::printf("%d défaut(s) d'interface confirmé(s)\n", g_defauts);
  std::fflush(stdout);
  QApplication::exit(0);
}
