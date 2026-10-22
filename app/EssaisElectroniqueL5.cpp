// Prométhée : essais d'interface du lot L5 de l'audit (« Circuit imprimé »), par de vrais gestes : piste qui contourne
// sans Ctrl, auréole et cartouche du tracé, liaison routée par son menu, composants « À placer » glissés sur la carte,
// repère de sérigraphie déplacé à la souris, vingt-quatre LED en couronne, rapport de vérification ouvert dans
// Prométhée. Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L5.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

#include <QApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QEventLoop>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>

#include "AtelierElectronique.hpp"
#include "CartoucheRoutage.hpp"
#include "DialogueCercle.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "FenetreRapport.hpp"
#include "PanneauAPlacer.hpp"
#include "SelectionElectronique.hpp"
#include "VueCircuit.hpp"
#include "promethee/modele.hpp"
#include "promethee/placement_circuit.hpp"
#include "promethee/routeur.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/verification.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
Pt broche(const ConceptionElec& c, const std::string& sym, const std::string& numero) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == sym)
        for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
          if (b.broche->numero == numero) return b.p;
  return {};
}
void fil(ConceptionElec& c, Pt a, Pt b) {
  if (a.x != b.x && a.y != b.y) {
    c.feuilles[0].fils.push_back({nouvelUuid(), a, {b.x, a.y}});
    c.feuilles[0].fils.push_back({nouvelUuid(), {b.x, a.y}, b});
  } else
    c.feuilles[0].fils.push_back({nouvelUuid(), a, b});
}
EmpreintePlacee* empreinteDe(ConceptionElec& c, const std::string& ref) {
  for (auto& e : c.circuit.empreintes)
    if (const ComposantElec* k = c.composant(e.composant); k && k->reference == ref) return &e;
  return nullptr;
}
const EmpreintePlacee* empreinteDe(const ConceptionElec& c, const std::string& ref) {
  for (const auto& e : c.circuit.empreintes)
    if (const ComposantElec* k = c.composant(e.composant); k && k->reference == ref) return &e;
  return nullptr;
}
// R1 et R2 reliées (R1.2 — R2.1) sur une carte de 50 × 30, un mur de cuivre étranger entre elles, ouvert en bas
ConceptionElec deuxResistances(bool mur) {
  ConceptionElec c = nouvelleConception("Essais L5");
  const std::string r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8), 1), r2 = poserSymbole(c, 0, "R", ptMm(101.6, 50.8), 1);
  // En U par-dessous : le fil ne touche aucune autre broche
  const Pt p1 = broche(c, r1, "2"), p2 = broche(c, r2, "1");
  const Nm bas = std::max(p1.y, p2.y) + nm(10.16);
  fil(c, p1, {p1.x, bas});
  fil(c, {p1.x, bas}, {p2.x, bas});
  fil(c, {p2.x, bas}, p2);
  annoter(c, false);
  mettreAJourCircuit(c);
  c.circuit.contour.largeur = nm(50);
  c.circuit.contour.hauteur = nm(30);
  empreinteDe(c, "R1")->position = ptMm(8, 15);
  empreinteDe(c, "R2")->position = ptMm(42, 15);
  if (mur) c.circuit.pistes.push_back({"mur", "AUTRE", Couche::CuivreDessus, ptMm(25, 1.5), ptMm(25, 23), nm(0.3)});
  return c;
}
AtelierElectronique* ouvrirCircuit(FenetrePrincipale& f, ConceptionElec c) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = c.nom;
  p.electronique.push_back(std::move(c));
  f.document()->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Circuit);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return nullptr;
  // Les panneaux fermés : la vue a la même taille quels que soient les lots passés avant
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur,
                       AtelierElectronique::Panneau::Verifications, AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre,
                       AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts, AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);
  a->recalculer();
  attendre(200);
  a->vueCircuit()->recadrer();
  attendre(150);
  return a;
}
// Centre d'une pastille d'un composant (sur la carte)
Pt pastille(const ConceptionElec& c, const std::string& ref, const std::string& numero) {
  for (const auto& p : pastilles(c, connectivite(c)))
    if (p.reference == ref && p.numero == numero) return p.pastille.position;
  return {};
}
}  // namespace

void essaisElectroniqueL5(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();

  // ---------- 5.7, 5.20 · Piste vers une pastille cachée derrière une piste : contournée sans Ctrl ; auréole, cartouche ----------
  {
    AtelierElectronique* a = ouvrirCircuit(f, deuxResistances(true));
    if (!a) {
      verifier(false, "L5 : la carte d'essai s'ouvre au circuit", {});
      return;
    }
    verifier(reel::activer(&f), "L5 : la fenêtre est active", {});
    VueCircuit* vc = a->vueCircuit();
    const ConceptionElec* c = conception(doc);
    const Pt depart = pastille(*c, "R1", "2"), arrivee = pastille(*c, "R2", "1");
    vc->setOutil(VueCircuit::Outil::Router);
    attendre(80);
    reel::cliquer(vc, vc->versEcran(depart).toPoint());
    attendre(120);
    if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL"))
      std::printf("  (départ %.3f,%.3f → écran %d,%d ; vue %dx%d ; outil %d ; largeur %.3f ; chevelu %zu ; pastilles %zu)\n", mm(depart.x), mm(depart.y), vc->versEcran(depart).toPoint().x(),
                  vc->versEcran(depart).toPoint().y(), vc->width(), vc->height(), static_cast<int>(vc->outil()), mm(vc->largeurTrace()), a->calculs().etat.chevelu.size(), a->calculs().pastilles.size());
    // Le cartouche près de la tête, et l'auréole autour des objets proches (R1.1, sans réseau)
    CartoucheRoutage* ca = vc->cartouche();
    const bool cartouche = ca && ca->isVisible() && !ca->texteReseau()->text().isEmpty();
    bool aureoleExacte = false;
    std::string detailAureole = "aucune";
    for (const auto& x : vc->aureolesRoutage()) {
      const Boite bf = boite(x.forme), bc = boite(x.contour);
      const double ecart = mm(bc.largeur() - bf.largeur()) / 2;
      detailAureole = x.objet + " : " + std::to_string(ecart) + " mm pour " + std::to_string(mm(x.isolement));
      if (x.objet == "R1.1") aureoleExacte = std::abs(ecart - mm(x.isolement)) < 0.01;
    }
    verifier(cartouche && aureoleExacte, "5.20 : pendant le tracé, le cartouche est près de la tête et l'auréole entoure R1.1 à l'isolement exact", detailAureole);
    // Un clic sur la largeur, dans le cartouche, la change
    const Nm avant = vc->largeurTrace();
    if (ca) {
      reel::cliquer(ca->boutonLargeur(), QPoint(6, ca->boutonLargeur()->height() / 2));
      attendre(100);
    }
    const Nm apres = vc->largeurTrace();
    verifier(avant > 0 && apres > 0 && apres != avant && ca && ca->boutonLargeur()->text().contains(QLocale(QLocale::French).toString(mm(apres), 'g', 3)),
             "5.20 : un clic sur la largeur, dans le cartouche, la change pour la suivante", std::to_string(mm(avant)) + " → " + std::to_string(mm(apres)));
    if (ca) {
      reel::cliquer(ca->boutonLargeur(), QPoint(6, ca->boutonLargeur()->height() / 2));
      // Retour à la largeur de la classe : la suite de la liste, puis le premier
      for (int i = 0; i < 12 && vc->largeurTrace() != avant; ++i) reel::cliquer(ca->boutonLargeur(), QPoint(6, ca->boutonLargeur()->height() / 2));
      attendre(60);
    }
    // Vers R2.1, derrière le mur : un seul clic, sans Ctrl
    reel::deplacer(vc, vc->versEcran(arrivee).toPoint());
    attendre(150);
    reel::cliquer(vc, vc->versEcran(arrivee).toPoint());
    attendre(300);
    c = conception(doc);
    int segments = 0;
    bool libres = true, quaranteCinq = true;
    for (const auto& p : c->circuit.pistes) {
      if (p.reseau == "AUTRE") continue;
      ++segments;
      OptionsTrace o;
      o.reseau = p.reseau;
      o.couche = p.couche;
      o.largeur = p.largeur;
      libres &= segmentLibre(*c, p.a, p.b, o);
      const Nm dx = std::abs(p.b.x - p.a.x), dy = std::abs(p.b.y - p.a.y);
      quaranteCinq &= dx == 0 || dy == 0 || dx == dy;
    }
    const Connectivite k = connectivite(*c);
    const bool relie = etatRoutage(*c, k, remplirZones(*c, k)).chevelu.empty();
    verifier(segments >= 2 && libres && quaranteCinq && relie, "5.7 : la piste tracée vers R2.1, cachée derrière un mur de cuivre, le contourne à l'isolement, à 45°, sans Ctrl",
             std::to_string(segments) + " segment(s)");
    vc->setOutil(VueCircuit::Outil::Selection);
  }

  // ---------- 5.21 · Le menu d'une liaison la route d'office ; ce qui ne passe pas reste au chevelu, avec sa raison ----------
  {
    AtelierElectronique* a = ouvrirCircuit(f, deuxResistances(true));
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool route = false, menu = false;
    if (vc) {
      const ConceptionElec* c = conception(doc);
      const Pt milieu{(pastille(*c, "R1", "2").x + pastille(*c, "R2", "1").x) / 2, pastille(*c, "R1", "2").y};
      vc->setOutil(VueCircuit::Outil::Selection);
      reel::cliquer(vc, vc->versEcran(milieu + ptMm(4, 0)).toPoint(), Qt::RightButton);
      attendre(250);
      for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible() && m->objectName() == QStringLiteral("menuLiaison"))
          for (QAction* x : m->actions())
            if (x->objectName() == QStringLiteral("routerLiaison")) {
              menu = true;
              x->trigger();
              m->close();
            }
      attendre(300);
      c = conception(doc);
      const Connectivite k = connectivite(*c);
      route = etatRoutage(*c, k, remplirZones(*c, k)).chevelu.empty();
    }
    verifier(menu && route, "5.21 : clic droit sur la liaison, « Router cette liaison » : elle est routée d'office, autour du mur", {});
    // Une liaison sans passage (R2 enfermée dans un anneau, carte simple face) : elle reste, avec sa raison
    ConceptionElec enfermee = deuxResistances(false);
    const std::vector<Pt> anneau{ptMm(36, 9), ptMm(48, 9), ptMm(48, 21), ptMm(36, 21), ptMm(36, 9)};
    for (std::size_t i = 1; i < anneau.size(); ++i) enfermee.circuit.pistes.push_back({nouvelUuid(), "AUTRE", Couche::CuivreDessus, anneau[i - 1], anneau[i], nm(0.3)});
    a = ouvrirCircuit(f, enfermee);
    vc = a ? a->vueCircuit() : nullptr;
    std::string raison;
    bool reste = false;
    if (vc) {
      const ConceptionElec* c = conception(doc);
      const Connectivite k = connectivite(*c);
      const auto ch = etatRoutage(*c, k, remplirZones(*c, k)).chevelu;
      if (!ch.empty()) raison = vc->routerLiaisonChoisie(ch.front());
      const ConceptionElec* c2 = conception(doc);
      const Connectivite k2 = connectivite(*c2);
      reste = etatRoutage(*c2, k2, remplirZones(*c2, k2)).chevelu.size() == 1;
    }
    verifier(reste && !raison.empty(), "5.21 : la liaison qui ne passe pas reste au chevelu, avec sa raison", raison);
  }

  // ---------- 5.5 · Un repère de sérigraphie se déplace à la souris et garde sa place ----------
  {
    AtelierElectronique* a = ouvrirCircuit(f, deuxResistances(false));
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool garde = false;
    std::string detail;
    if (vc) {
      const ConceptionElec* c = conception(doc);
      const EmpreintePlacee* e = empreinteDe(*c, "R1");
      const Pt avant = positionRepere(*e, *c->empreinte(e->empreinte), c->circuit.serigraphie.hauteur);
      const QPoint de = vc->versEcran(avant).toPoint(), vers = vc->versEcran(avant + ptMm(0, 5)).toPoint();
      vc->setOutil(VueCircuit::Outil::Selection);
      reel::glisser(vc, de, vers);
      attendre(200);
      c = conception(doc);
      e = empreinteDe(*c, "R1");
      const Pt apres = positionRepere(*e, *c->empreinte(e->empreinte), c->circuit.serigraphie.hauteur);
      const ConceptionElec relue = conceptionDepuisJson(versJson(*c));
      const EmpreintePlacee* er = empreinteDe(relue, "R1");
      const bool bouge = std::abs(mm(apres.y - avant.y) - 5) < 0.3 && std::abs(mm(apres.x - avant.x)) < 0.3;
      // Le corps n'a pas bougé ; le repère est gardé à l'enregistrement
      garde = bouge && e->position == ptMm(8, 15) && er && er->repere == e->repere;
      detail = std::to_string(mm(apres.x - avant.x)) + " ; " + std::to_string(mm(apres.y - avant.y)) + " mm";
    }
    verifier(garde, "5.5 : le repère R1 glissé de 5 mm à la souris garde sa place, le corps reste, et le fichier la garde", detail);
  }

  // ---------- 5.2 · Composants ajoutés au schéma : dans « À placer », puis glissés sur la carte, par groupe ----------
  {
    AtelierElectronique* a = ouvrirCircuit(f, deuxResistances(false));
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool liste = false, poses = false;
    std::string detail;
    if (vc) {
      a->modifierConception([](ConceptionElec& c) {
        for (int i = 0; i < 3; ++i) poserSymbole(c, 0, "C", ptMm(50.8 + 12.7 * i, 101.6));
      }, QStringLiteral("Trois condensateurs"));
      a->mettreAJourCircuit();
      attendre(300);
      a->executerAction("circuit.aPlacer");
      attendre(200);
      PanneauAPlacer* p = a->aPlacer();
      QTreeWidgetItem* groupe = p && p->arbre()->topLevelItemCount() == 1 ? p->arbre()->topLevelItem(0) : nullptr;
      liste = a->panneau(AtelierElectronique::Panneau::APlacer)->isVisible() && groupe && groupe->childCount() == 3;
      if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL"))
        std::printf("  (à placer : panneau %d, groupes %d, enfants %d)\n", a->panneau(AtelierElectronique::Panneau::APlacer)->isVisible(), p ? p->arbre()->topLevelItemCount() : -1,
                    groupe ? groupe->childCount() : -1);
      if (groupe) {
        // Le groupe glissé sur la carte, lâché au coin haut gauche
        const std::vector<std::string> ids = p->composantsDe(groupe);
        QMimeData* m = PanneauAPlacer::donnees(ids);
        const QPointF ou = vc->versEcran(ptMm(14, 4));
        QDragEnterEvent entree(ou.toPoint(), Qt::CopyAction, m, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(vc, &entree);
        QDropEvent depot(ou, Qt::CopyAction, m, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(vc, &depot);
        delete m;
        attendre(300);
      }
      const ConceptionElec* c = conception(doc);
      int surCarte = 0;
      for (const auto& e : c->circuit.empreintes)
        if (const ComposantElec* k = c->composant(e.composant); k && k->reference.rfind("C", 0) == 0 && !aPlacer(*c, e)) ++surCarte;
      poses = surCarte == 3 && p && p->nombre() == 0;
      detail = std::to_string(surCarte) + " sur la carte, " + std::to_string(p ? p->nombre() : -1) + " dans la liste";
    }
    verifier(liste && poses, "5.2 : trois condensateurs ajoutés au schéma attendent dans « À placer », puis se posent sur la carte d'un glisser du groupe", detail);
  }

  // ---------- 5.18 · Vingt-quatre LED en couronne de 20 mm, tournées vers le centre, D1 à D24, en un pas ----------
  {
    ConceptionElec c0 = nouvelleConception("Couronne");
    for (int i = 0; i < 24; ++i) poserSymbole(c0, 0, "LED", ptMm(25.4 + 12.7 * (i % 8), 25.4 + 25.4 * (i / 8)));
    annoter(c0, false);
    mettreAJourCircuit(c0);
    c0.circuit.contour.largeur = nm(60);
    c0.circuit.contour.hauteur = nm(60);
    for (std::size_t i = 0; i < c0.circuit.empreintes.size(); ++i) c0.circuit.empreintes[i].position = ptMm(5 + 6.5 * static_cast<double>(i % 8), 10 + 15 * static_cast<double>(i / 8));
    AtelierElectronique* a = ouvrirCircuit(f, c0);
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool couronne = false;
    std::string detail;
    if (vc) {
      vc->toutChoisir();
      attendre(100);
      const int pas = doc->positionHistorique();
      QTimer::singleShot(300, [] {
        if (auto* d = qobject_cast<DialogueCercle*>(QApplication::activeModalWidget())) {
          d->centreX->setValue(30);
          d->centreY->setValue(30);
          d->rayon->setValue(20);
          d->accept();
        }
      });
      a->executerAction("circuit.disposerCercle");
      attendre(300);
      const ConceptionElec* c = conception(doc);
      OptionsCercle o;
      o.centre = ptMm(30, 30);
      o.rayon = nm(20);
      int justes = 0;
      for (int i = 0; i < 24; ++i)
        if (const EmpreintePlacee* e = empreinteDe(*c, "D" + std::to_string(i + 1))) {
          const Pt attendu = placeSurCercle(o, static_cast<std::size_t>(i), 24);
          const bool place = distance(e->position, attendu) <= 1000;
          const bool tournee = std::abs(e->rotation - rotationSurCercle(o, static_cast<std::size_t>(i), 24)) < 1e-6;
          justes += place && tournee;
        }
      couronne = justes == 24 && doc->positionHistorique() == pas + 1;
      detail = std::to_string(justes) + " / 24, " + std::to_string(doc->positionHistorique() - pas) + " pas";
    }
    verifier(couronne, "5.18 : vingt-quatre LED choisies se rangent en couronne de 20 mm, tournées vers le centre, D1 à D24, en un pas d'annulation", detail);
  }

  // ---------- 5.29 · Le rapport de vérification s'ouvre dans Prométhée, groupé par règle, exclusions à part ----------
  {
    ConceptionElec c0 = deuxResistances(false);
    c0.circuit.pistes.push_back({"proche", "AUTRE", Couche::CuivreDessus, ptMm(15, 15.35), ptMm(35, 15.35), nm(0.25)});
    c0.circuit.pistes.push_back({"proche2", "AUTRE2", Couche::CuivreDessus, ptMm(15, 18), ptMm(35, 18), nm(0.25)});
    c0.circuit.pistes.push_back({"proche3", "AUTRE3", Couche::CuivreDessus, ptMm(15, 18.3), ptMm(35, 18.3), nm(0.25)});
    {
      const Connectivite k = connectivite(c0);
      for (const auto& v : verifierDRC(c0, k, remplirZones(c0, k)))
        if (std::find(v.objets.begin(), v.objets.end(), "proche3") != v.objets.end()) {
          c0.erc.exclusions.push_back(exclusionPour(c0, v, "écart voulu, vu avec le fabricant", "essai"));
          break;
        }
    }
    AtelierElectronique* a = ouvrirCircuit(f, c0);
    bool ouvert = false, mene = false;
    std::string detail;
    if (a) {
      a->executerAction("verification.rapport");
      attendre(300);
      FenetreRapport* r = a->rapport();
      ouvert = r && r->isVisible() && r->html().contains(QStringLiteral("<h2>Exclusions</h2>")) && r->html().contains(QStringLiteral("écart voulu")) &&
               r->html().contains(QStringLiteral("<h3 id=\"")) && !r->drc().actives.empty();
      // La première ligne de la DRC, cliquée : son objet est choisi au circuit
      if (r && !r->drc().actives.empty()) {
        // La première violation de la DRC qui vise des objets (l'isolement entre AUTRE2 et AUTRE3)
        std::size_t i = 0;
        while (i + 1 < r->drc().actives.size() && r->drc().actives[i].objets.empty()) ++i;
        const std::size_t n = r->erc().size() + i;
        const Violation v = r->drc().actives[i];
        emit r->navigateur()->anchorClicked(QUrl(QStringLiteral("promethee:violation/%1").arg(n)));
        attendre(250);
        bool choisi = false;
        for (const auto& id : v.objets) choisi |= a->vueCircuit()->estChoisi(VueCircuit::Genre::Piste, id) || a->vueCircuit()->estChoisi(VueCircuit::Genre::Empreinte, id);
        mene = choisi && a->mode() == AtelierElectronique::Circuit;
        if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL")) {
          std::printf("  (rapport : %zu objets, mode %d, choisi %d :", v.objets.size(), a->mode(), choisi);
          for (const auto& id : v.objets) std::printf(" %s", id.c_str());
          std::printf(")\n");
        }
        detail = v.code + " " + v.message;
        r->close();
      }
    }
    verifier(ouvert && mene, "5.29 : le rapport HTML s'ouvre dans Prométhée, groupé par règle, exclusions à part, et sa ligne mène à son objet d'un clic", detail);
  }
}
