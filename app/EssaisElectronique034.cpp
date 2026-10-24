// Prométhée : essais d'interface de la 0.34, lot L5 — gestes du circuit : collisions en direct et sommets du contour
// (5.4), chevelu mesuré en glissant (5.6), largeur et via pendant le tracé (5.8), segment glissé (5.9), navigateur de
// réseaux (5.15), presse-papiers partagé et liaisons restantes (5.16), carte sans schéma (5.17), commandes du ruban
// en un pas (5.19, 5.22, 5.24), origine de travail (5.31), réglages repris (5.32).
// Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=034.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <QApplication>
#include <QElapsedTimer>
#include <QClipboard>
#include <QDialog>
#include <QDockWidget>
#include <QEventLoop>
#include <QImage>
#include <QInputDialog>
#include <QListWidget>
#include <QRegularExpression>
#include <QMenu>
#include <QMimeData>
#include <QTimer>
#include <QTreeWidget>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QTableWidget>
#include <map>

#include "AtelierElectronique.hpp"
#include "BandeauOutil.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "ZoneMessages.hpp"
#include "FenetrePrincipale.hpp"
#include "PanneauReseaux.hpp"
#include "DialogueRemplacer.hpp"
#include "FenetresBibliotheque.hpp"
#include "FenetresMeca034.hpp"
#include "FenetresSchema034.hpp"
#include "VueSchema.hpp"
#include "promethee/reutilisation.hpp"
#include "promethee/schema_avance.hpp"
#include "VuePiece.hpp"
#include "promethee/derive.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/nomenclature.hpp"
#include "SelectionElectronique.hpp"
#include "VueCircuit.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
ConceptionElec carteSeule(const std::string& nom) {
  ConceptionElec c = nouvelleConception(nom);
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(50);
  c.circuit.contour.hauteur = nm(30);
  return c;
}
AtelierElectronique* ouvrir(FenetrePrincipale& f, ConceptionElec c) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = c.nom;
  p.electronique.push_back(std::move(c));
  f.document()->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Circuit);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return nullptr;
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur,
                       AtelierElectronique::Panneau::Verifications, AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre,
                       AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts, AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer,
                       AtelierElectronique::Panneau::Reseaux})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);
  a->recalculer();
  attendre(250);
  a->vueCircuit()->recadrer();
  attendre(150);
  return a;
}
Pt pastilleDe(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  for (const auto& p : pastilles(c, connectivite(c)))
    if (p.composant == composant && p.numero == numero) return p.pastille.position;
  return {};
}
// Les derniers messages de l'atelier, sans balises (détail des essais)
std::string derniersMessages(AtelierElectronique* a, int n = 3) {
  std::string r;
  if (!a || !a->journal()) return r;
  const auto& m = a->journal()->messages();
  for (int i = std::max(0, static_cast<int>(m.size()) - n); i < static_cast<int>(m.size()); ++i) {
    QString t = m[static_cast<std::size_t>(i)].html;
    t.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    r += " | " + t.toStdString();
  }
  return r;
}
// Boîte de dialogue de saisie ouverte par une commande : remplie puis acceptée
void repondre(int delai, const std::function<void(QInputDialog*)>& f) {
  QTimer::singleShot(delai, [f] {
    if (auto* d = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
      f(d);
      d->accept();
    }
  });
}
}  // namespace

void essaisElectronique034L4(FenetrePrincipale& f, const Verifier& verifier);
void essaisElectronique034L6(FenetrePrincipale& f, const Verifier& verifier);
void essaisElectronique034L7(FenetrePrincipale& f, const Verifier& verifier);
void essaisElectronique034L8(FenetrePrincipale& f, const Verifier& verifier);

void essaisElectronique034L5(FenetrePrincipale& f, const Verifier& verifier);

// Tous les lots de la 0.34 ; « PROMETHEE_ESSAIS_LOTS=034L5 » (L4, L5, L6, L7…) n'en mène qu'un
void essaisElectronique034(FenetrePrincipale& f, const Verifier& verifier) {
  essaisElectronique034L4(f, verifier);
  essaisElectronique034L6(f, verifier);
  essaisElectronique034L5(f, verifier);
  essaisElectronique034L7(f, verifier);
  essaisElectronique034L8(f, verifier);
}

void essaisElectronique034L5(FenetrePrincipale& f, const Verifier& verifier) {
  Document* doc = f.document();

  // ---------- 5.4 · Encombrement rouge pendant le glisser ; sommet du contour déplacé à la souris ----------
  {
    ConceptionElec c0 = carteSeule("Collisions");
    const std::string r1 = poserEmpreinteSeule(c0, "R_0603", ptMm(12, 15));
    poserEmpreinteSeule(c0, "R_0603", ptMm(30, 15));
    AtelierElectronique* a = ouvrir(f, c0);
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool rouge = false, contour = false;
    std::string detail;
    if (vc) {
      vc->choisir(VueCircuit::Genre::Empreinte, r1);
      attendre(100);
      const QPoint depart = vc->versEcran(ptMm(12, 15)).toPoint(), arrivee = vc->versEcran(ptMm(29.6, 15)).toPoint();
      reel::appuyer(vc, depart);
      for (int i = 1; i <= 10; ++i) {
        reel::deplacer(vc, depart + (arrivee - depart) * i / 10);
        attendre(30);
      }
      attendre(150);
      const QImage img = vc->grab().toImage();
      int n = 0;
      for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
          const QColor p = img.pixelColor(x, y);
          if (std::abs(p.red() - 0xC2) < 12 && std::abs(p.green() - 0x40) < 12 && std::abs(p.blue() - 0x2E) < 12) ++n;
        }
      reel::relacher(vc, arrivee);
      attendre(300);
      rouge = n > 20;
      detail = std::to_string(n) + " pixel(s) rouges";
      // Sommet du contour (coin bas droit) glissé de 5 mm vers la droite
      const int pas = doc->positionHistorique();
      vc->choisir(VueCircuit::Genre::Rien, {});
      attendre(100);
      const QPoint coin = vc->versEcran(ptMm(50, 30)).toPoint(), but = vc->versEcran(ptMm(55, 30)).toPoint();
      reel::glisser(vc, coin, but);
      attendre(300);
      const ConceptionElec* c = conception(doc);
      if (c) {
        const auto s = sommetsContour(*c, -1);
        contour = std::any_of(s.begin(), s.end(), [](Pt p) { return std::abs(p.x - nm(55)) <= nm(0.2) && std::abs(p.y - nm(30)) <= nm(0.2); }) && doc->positionHistorique() == pas + 1;
        detail += ", contour de " + std::to_string(s.size()) + " sommets, pas +" + std::to_string(doc->positionHistorique() - pas);
      }
    }
    verifier(rouge && contour, "5.4 : encombrement rouge pendant le glisser sur une voisine ; sommet du contour déplacé à la souris, en un pas", detail);
  }

  // ---------- 5.6 · Longueur totale du chevelu affichée et mise à jour en glissant ----------
  {
    ConceptionElec c0 = carteSeule("Chevelu");
    const std::string r1 = poserEmpreinteSeule(c0, "R_0603", ptMm(10, 15));
    const std::string r2 = poserEmpreinteSeule(c0, "R_0603", ptMm(40, 15));
    reseauParPiste(c0, r1, "2", r2, "1");
    AtelierElectronique* a = ouvrir(f, c0);
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool affiche = false, suit = false;
    std::string detail;
    if (vc && vc->bandeau()) {
      vc->choisir(VueCircuit::Genre::Empreinte, r1);
      attendre(100);
      const QPoint depart = vc->versEcran(ptMm(10, 15)).toPoint();
      reel::appuyer(vc, depart);
      reel::deplacer(vc, depart + QPoint(8, 0));
      attendre(400);
      const QString t1 = vc->bandeau()->texte();
      reel::deplacer(vc, vc->versEcran(ptMm(30, 15)).toPoint());
      attendre(600);
      const QString t2 = vc->bandeau()->texte();
      reel::relacher(vc, vc->versEcran(ptMm(30, 15)).toPoint());
      attendre(200);
      affiche = t1.contains(QStringLiteral("chevelu"));
      suit = affiche && t1 != t2 && t2.contains(QStringLiteral("chevelu"));
      detail = t1.toStdString() + " → " + t2.toStdString();
    }
    verifier(affiche && suit, "5.6 : longueur totale du chevelu affichée pendant le glisser, mise à jour avec lui", detail);
  }

  // ---------- 5.8 · W change la largeur pendant le tracé ; V passe à la face opposée en quatre couches ----------
  {
    ConceptionElec c0 = carteSeule("Largeur et via");
    c0.circuit.empilage.couchesCuivre = 4;
    const std::string r1 = poserEmpreinteSeule(c0, "R_0603", ptMm(10, 15));
    AtelierElectronique* a = ouvrir(f, c0);
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool largeur = false, via = false;
    std::string detail;
    if (vc) {
      const Pt p = pastilleDe(*conception(doc), r1, "2");
      a->executerAction("circuit.router");
      attendre(100);
      reel::cliquer(vc, vc->versEcran(p).toPoint());
      attendre(100);
      reel::deplacer(vc, vc->versEcran(ptMm(20, 15)).toPoint());
      attendre(100);
      const Nm avant = vc->largeurTrace();
      reel::touche(vc, Qt::Key_W);
      attendre(100);
      const Nm apres = vc->largeurTrace();
      largeur = avant > 0 && apres > avant;
      reel::cliquer(vc, vc->versEcran(ptMm(20, 15)).toPoint());
      attendre(100);
      reel::touche(vc, Qt::Key_V);
      attendre(150);
      via = vc->coucheActive() == Couche::CuivreDessous;
      reel::touche(vc, Qt::Key_Escape);
      attendre(150);
      detail = "largeur " + std::to_string(avant) + " → " + std::to_string(apres) + ", couche après V : " + codeCouche(vc->coucheActive());
    }
    verifier(largeur && via, "5.8 : W élargit le tracé en cours ; V mène à la face opposée sur une carte de quatre couches", detail);
  }

  // ---------- 5.9 · Une piste posée glisse parallèlement à elle-même, ses voisines la suivent ----------
  {
    ConceptionElec c0 = carteSeule("Glisser");
    c0.circuit.pistes = {{"s1", "N", Couche::CuivreDessus, ptMm(10, 10), ptMm(20, 10), nm(0.25)},
                         {"s2", "N", Couche::CuivreDessus, ptMm(20, 10), ptMm(20, 20), nm(0.25)},
                         {"s3", "N", Couche::CuivreDessus, ptMm(20, 20), ptMm(30, 20), nm(0.25)}};
    AtelierElectronique* a = ouvrir(f, c0);
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool glisse = false;
    std::string detail;
    if (vc) {
      vc->choisir(VueCircuit::Genre::Piste, "s2");
      attendre(100);
      const int pas = doc->positionHistorique();
      reel::glisser(vc, vc->versEcran(ptMm(20, 15)).toPoint(), vc->versEcran(ptMm(23, 15)).toPoint());
      attendre(300);
      const ConceptionElec* c = conception(doc);
      auto piste = [&](const std::string& id) {
        for (const auto& t : c->circuit.pistes)
          if (t.uuid == id) return t;
        return Piste{};
      };
      const Piste s1 = piste("s1"), s2 = piste("s2"), s3 = piste("s3");
      glisse = std::abs(s2.a.x - nm(23)) <= nm(0.06) && s2.a.x == s2.b.x && s1.b == s2.a && s3.a == s2.b && s1.a == ptMm(10, 10) && s3.b == ptMm(30, 20) && doc->positionHistorique() == pas + 1;
      detail = "s2 à x = " + std::to_string(mm(s2.a.x)) + " mm, pas +" + std::to_string(doc->positionHistorique() - pas);
    }
    verifier(glisse, "5.9 : une piste choisie glisse parallèlement à elle-même, ses voisines la suivent, en un pas", detail);
  }

  // ---------- 5.15 · Réseau cliqué dans le navigateur : surligné partout ----------
  {
    ConceptionElec c0 = carteSeule("Navigateur");
    const std::string r1 = poserEmpreinteSeule(c0, "R_0603", ptMm(10, 15));
    const std::string r2 = poserEmpreinteSeule(c0, "R_0603", ptMm(40, 15));
    const std::string n = reseauParPiste(c0, r1, "2", r2, "1");
    AtelierElectronique* a = ouvrir(f, c0);
    bool surligne = false;
    std::string detail;
    if (a) {
      a->executerAction("vue.reseaux");
      attendre(300);
      auto* p = a->findChild<PanneauReseaux*>();
      QTreeWidgetItem* it = p ? p->ligne(n) : nullptr;
      if (it) {
        p->arbre()->scrollToItem(it);
        attendre(50);
        const QRect r = p->arbre()->visualItemRect(it);
        reel::cliquer(p->arbre()->viewport(), r.center());
        attendre(200);
        surligne = a->vueCircuit()->selectionPartagee() && a->vueCircuit()->selectionPartagee()->reseau() == n;
      }
      detail = p ? std::to_string(p->nombre()) + " réseau(x) listé(s)" : "navigateur absent";
      if (QDockWidget* d = a->panneau(AtelierElectronique::Panneau::Reseaux)) d->hide();
    }
    verifier(surligne, "5.15 : un réseau cliqué dans le navigateur est surligné au schéma et au circuit", detail);
  }

  // ---------- 5.16 · Presse-papiers du circuit partagé ; 5.17 réseau neuf par la piste, gardé ----------
  {
    ConceptionElec c0 = carteSeule("Carte seule");
    AtelierElectronique* a = ouvrir(f, c0);
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool partage = false, reseau = false, garde = false;
    std::string detail;
    if (vc) {
      // Deux empreintes posées sans schéma depuis le ruban
      for (int i = 0; i < 2; ++i) {
        repondre(200, [](QInputDialog* d) { d->setTextValue(QStringLiteral("R_0603")); });
        reel::deplacer(vc, vc->versEcran(ptMm(10 + 25 * i, 15)).toPoint());
        a->executerAction("circuit.poserEmpreinte");
        attendre(400);
      }
      const ConceptionElec* c = conception(doc);
      if (c && c->composants.size() == 2) {
        const std::string ra = c->composants[0].uuid, rb = c->composants[1].uuid;
        const Pt pa = pastilleDe(*c, ra, "2"), pb = pastilleDe(*c, rb, "1");
        a->recalculer();
        attendre(300);
        vc->router({pa, {pb.x, pa.y}, pb});
        attendre(400);
        c = conception(doc);
        const auto ita = c->nomsReseaux.find(ra + ":2"), itb = c->nomsReseaux.find(rb + ":1");
        const std::string na = ita != c->nomsReseaux.end() ? ita->second : std::string("?"), nb = itb != c->nomsReseaux.end() ? itb->second : std::string("?");
        reseau = ita != c->nomsReseaux.end() && itb != c->nomsReseaux.end() && na == nb && !na.empty() &&
                 std::all_of(c->circuit.pistes.begin(), c->circuit.pistes.end(), [&](const Piste& t) { return t.reseau == na; });
        detail = "réseau « " + na + " » / « " + nb + " », " + std::to_string(c->circuit.pistes.size()) + " piste(s) sur « " + (c->circuit.pistes.empty() ? std::string() : c->circuit.pistes.front().reseau) + " », " +
                 std::to_string(c->nomsReseaux.size()) + " nom(s) figé(s)";
        // Synchronisation puis relecture du fichier : le réseau reste
        a->mettreAJourCircuit();
        attendre(300);
        const ConceptionElec relu = conceptionDepuisJson(versJson(*conception(doc)));
        const auto itr = relu.nomsReseaux.find(ra + ":2");
        garde = reseau && itr != relu.nomsReseaux.end() && itr->second == na && relu.composants.size() == 2;
        detail += ", relu « " + (itr != relu.nomsReseaux.end() ? itr->second : std::string("?")) + " »" + derniersMessages(a);
        // Copier les deux empreintes et la piste : le presse-papiers du système porte le circuit, le collage double tout
        vc->toutChoisir();
        attendre(100);
        vc->copier();
        attendre(100);
        const QMimeData* m = QApplication::clipboard()->mimeData();
        const bool mime = m && m->hasFormat(QStringLiteral("application/x-promethee-circuit"));
        const std::size_t avant = conception(doc)->circuit.empreintes.size();
        reel::deplacer(vc, vc->versEcran(ptMm(10, 25)).toPoint());
        vc->coller();
        attendre(300);
        partage = mime && conception(doc)->circuit.empreintes.size() == 2 * avant;
        detail += mime ? ", presse-papiers du système" : ", presse-papiers vide";
      }
    }
    verifier(reseau && garde, "5.17 : carte sans schéma, deux empreintes reliées par une piste partagent un réseau neuf, gardé après synchronisation et relecture", detail);
    verifier(partage, "5.16 : le presse-papiers du circuit passe par celui du système et colle empreintes et cuivre", detail);
  }

  // ---------- 5.16 · Une liaison restante cliquée est montrée ----------
  {
    ConceptionElec c0 = carteSeule("Restantes");
    const std::string r1 = poserEmpreinteSeule(c0, "R_0603", ptMm(8, 15));
    const std::string r2 = poserEmpreinteSeule(c0, "R_0603", ptMm(42, 15));
    const std::string n = reseauParPiste(c0, r1, "2", r2, "1");
    // Un mur de cuivre étranger sur toute la hauteur : le routage direct échoue
    c0.circuit.pistes.push_back({"mur", "AUTRE", Couche::CuivreDessus, ptMm(25, 0.5), ptMm(25, 29.5), nm(0.3)});
    c0.circuit.pistes.push_back({"mur2", "AUTRE", Couche::CuivreDessous, ptMm(25, 0.5), ptMm(25, 29.5), nm(0.3)});
    AtelierElectronique* a = ouvrir(f, c0);
    bool montree = false;
    std::string detail;
    if (a) {
      a->executerAction("circuit.routerAuto");
      attendre(500);
      QListWidget* l = nullptr;
      for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* x = w->findChild<QListWidget*>(QStringLiteral("liaisonsRestantes"))) l = x;
      if (l && l->count() > 0) {
        l->setCurrentRow(0);
        attendre(200);
        montree = a->vueCircuit()->selectionPartagee() && a->vueCircuit()->selectionPartagee()->reseau() == n;
        if (QWidget* d = l->window()) d->close();
      }
      detail = (l ? std::to_string(l->count()) + " liaison(s) restante(s)" : std::string("liste absente")) + derniersMessages(a);
      if (!l)
        for (QWidget* w : QApplication::topLevelWidgets())
          if (w->isVisible()) detail += " · " + w->objectName().toStdString() + "/" + std::string(w->metaObject()->className());
    }
    verifier(montree, "5.16 : clic sur une liaison restante du routage : elle est centrée et son réseau surligné", detail);
  }

  // ---------- 5.19, 5.22, 5.24 · Commandes du ruban, chacune en un pas d'annulation ----------
  {
    ConceptionElec c0 = carteSeule("Commandes");
    const std::string u1 = poserEmpreinteSeule(c0, "SOIC-8", ptMm(25, 15));
    const std::string c1 = poserEmpreinteSeule(c0, "C_0603", ptMm(5, 5));
    c0.nomsReseaux[u1 + ":8"] = "VCC";
    c0.nomsReseaux[u1 + ":4"] = "GND";
    c0.nomsReseaux[c1 + ":1"] = "VCC";
    c0.nomsReseaux[c1 + ":2"] = "GND";
    AtelierElectronique* a = ouvrir(f, c0);
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool decouplage = false, gouttes = false, renum = false;
    std::string detail, detailGouttes;
    if (vc) {
      vc->choisir(VueCircuit::Genre::Empreinte, u1);
      attendre(100);
      int pas = doc->positionHistorique();
      a->executerAction("circuit.decouplages");
      attendre(400);
      const ConceptionElec* c = conception(doc);
      decouplage = distance(pastilleDe(*c, c1, "1"), pastilleDe(*c, u1, "8")) < nm(4.5) && doc->positionHistorique() == pas + 1;
      pas = doc->positionHistorique();
      a->executerAction("circuit.gouttes");
      attendre(400);
      c = conception(doc);
      gouttes = !c->circuit.regions.empty() && doc->positionHistorique() == pas + 1;
      detailGouttes = "gouttes : pas " + std::to_string(doc->positionHistorique() - pas) + ", " + std::to_string(c->circuit.regions.size()) + " région(s)";
      // Renumérotation : la boîte de dialogue acceptée telle quelle
      QTimer::singleShot(250, [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget()); d && d->objectName() == QLatin1String("dialogueRenumeroter")) d->accept();
      });
      pas = doc->positionHistorique();
      vc->choisir(VueCircuit::Genre::Rien, {});
      a->executerAction("circuit.renumeroter");
      attendre(400);
      renum = doc->positionHistorique() <= pas + 1;
      detail = "pas " + std::to_string(doc->positionHistorique() - pas) + ", " + std::to_string(c->circuit.regions.size()) + " goutte(s)";
    }
    verifier(decouplage, "5.24 : « Découplages » range le condensateur au plus près de la broche d'alimentation, en un pas", detail);
    verifier(gouttes, "5.22 : « Gouttes » les pose sur les pastilles reliées, en un pas", detailGouttes);
    verifier(renum, "5.19 : « Renuméroter… » en un pas au plus, son journal en message", detail);
  }

  // ---------- 5.31 · Origine prise au centre d'un trou : le trou lit 0 ; 0 dans le fichier de placement ----------
  {
    ConceptionElec c0 = carteSeule("Origine");
    // Le trou en bas à droite : la pile des messages, en bas à gauche du canevas, ne le couvre pas
    const std::string h1 = poserEmpreinteSeule(c0, "MountingHole_3.2", ptMm(45, 25));
    const std::string r1 = poserEmpreinteSeule(c0, "R_0603", ptMm(30, 15));
    AtelierElectronique* a = ouvrir(f, c0);
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    bool zero = false, fichier = false;
    std::string detail;
    if (vc) {
      // Clic droit sur le trou, « Origine de travail ici »
      QTimer::singleShot(250, [] {
        for (QWidget* w : QApplication::topLevelWidgets())
          if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible() && m->objectName() == QLatin1String("menuEmpreinte"))
            for (QAction* x : m->actions())
              if (x->objectName() == QLatin1String("origineIci")) {
                x->trigger();
                m->close();
              }
      });
      vc->setOutil(VueCircuit::Outil::Selection);
      reel::cliquer(vc, vc->versEcran(ptMm(45, 25)).toPoint(), Qt::RightButton);
      attendre(500);
      const ConceptionElec* c = conception(doc);
      if (c && !c->circuit.affichage.origineTravail) detail = "origine non posée par le menu" + derniersMessages(a);
      if (c && c->circuit.affichage.origineTravail) {
        const Pt t = versRepereTravail(c->circuit.affichage, ptMm(45, 25));
        zero = t == Pt{};
        a->executerAction("circuit.origineFichiers");
        attendre(200);
        c = conception(doc);
        // Le fichier de placement : le trou n'y est pas (pas à monter) ; R1 à (−15 ; 10) depuis le trou, y vers le haut
        const std::string csv = csvPlacement(*c, true);
        // Ligne de R1 (Designator, Val, Package, Mid X, Mid Y…) : Mid X et Mid Y à −15 et 10 mm de l'origine
        for (const auto& l : lireTableau(csv))
          if (l.size() >= 5 && l[0] == "R1") {
            const double x = QString::fromStdString(l[3]).replace(',', '.').toDouble(), y = QString::fromStdString(l[4]).replace(',', '.').toDouble();
            fichier = c->circuit.affichage.origineFabrication && std::abs(x + 15) < 0.01 && std::abs(y - 10) < 0.01;
          }
        detail = csv.substr(0, 200);
      }
      (void)h1;
      (void)r1;
    }
    verifier(zero && fichier, "5.31 : origine de travail au centre d'un trou de fixation : le trou lit 0 ; 0, le fichier de placement s'y rapporte", detail);
  }

  // ---------- 5.32 · Réglages repris d'une autre carte, règles et classes seules ----------
  {
    prom::Projet p = prom::projetVide();
    p.sansCarte = true;
    ConceptionElec a0 = carteSeule("Cible");
    ConceptionElec b0 = carteSeule("Référence");
    b0.circuit.regles.isolement = nm(0.3);
    ClasseReseau k;
    k.nom = "Puissance";
    k.largeur = nm(1);
    b0.circuit.regles.classes.push_back(k);
    b0.circuit.empilage.couchesCuivre = 4;
    b0.circuit.affichage.grille = nm(0.5);
    p.electronique = {a0, b0};
    f.document()->creer(p);
    f.montrerElectronique(0, AtelierElectronique::Circuit);
    attendre(400);
    AtelierElectronique* a = f.atelierElectronique();
    bool repris = false;
    std::string detail;
    if (a) {
      QTimer::singleShot(250, [] {
        if (auto* d = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) d->accept();  // la carte de référence
      });
      QTimer::singleShot(700, [] {
        if (auto* d = qobject_cast<QDialog*>(QApplication::activeModalWidget()); d && d->objectName() == QLatin1String("listeACocher"))
          if (auto* l = d->findChild<QListWidget*>(QStringLiteral("lignes"))) {
            for (int i = 0; i < l->count(); ++i) l->item(i)->setCheckState(i <= 1 ? Qt::Checked : Qt::Unchecked);
            d->accept();
          }
      });
      a->executerAction("circuit.reprendreReglages");
      attendre(1200);
      const ConceptionElec& c = doc->projet().electronique[0];
      repris = c.circuit.regles.isolement == nm(0.3) && c.circuit.regles.classes.size() == b0.circuit.regles.classes.size() && c.circuit.empilage.couchesCuivre == 2 &&
               c.circuit.affichage.grille == a0.circuit.affichage.grille;
      detail = "isolement " + std::to_string(mm(c.circuit.regles.isolement)) + " mm, " + std::to_string(c.circuit.empilage.couchesCuivre) + " couches";
    }
    verifier(repris, "5.32 : « Reprendre les réglages… » règles et classes seules cochées : règles et classes identiques, empilage et vues inchangés", detail);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Lot L4 de la 0.34 : approvisionnement (4.18), échange en masse (4.20), bibliothèque en tableau (4.23), empreintes
// par noms (4.26), affectation non modale (4.28)
namespace {
// Conception de schéma : n résistances de la valeur donnée, posées et synchronisées sur la carte
ConceptionElec schemaResistances(const std::string& nom, int dixK, int autres, bool empreintes = true) {
  ConceptionElec c = carteSeule(nom);
  auto poser = [&](int n, const std::string& valeur, double y) {
    for (int i = 0; i < n; ++i) {
      const std::string s = poserSymbole(c, 0, "R", ptMm(20 + 7.62 * (i % 16), y + 10.16 * (i / 16)));
      for (const auto& sp : c.feuilles[0].symboles)
        if (sp.uuid == s) {
          ComposantElec* k = c.composant(sp.composant);
          k->valeur = valeur;
          if (!empreintes) {
            k->empreinte.clear();
            k->champs["Boîtier"] = "0603";
          }
        }
    }
  };
  poser(dixK, "10k", 30);
  poser(autres, "4k7", 100);
  annoter(c, true);
  if (empreintes) appliquerEcarts(c, ecartsSynchronisation(c));
  return c;
}
}  // namespace

void essaisElectronique034L4(FenetrePrincipale& f, const Verifier& verifier) {
  Document* doc = f.document();

  // ---------- 4.18 · Huit résistances de 10 kΩ par carte et 50 en tiroir : 30 à acheter pour dix cartes ----------
  {
    AtelierElectronique* a = ouvrir(f, schemaResistances("Achats", 8, 2));
    bool trente = false;
    std::string detail;
    if (a) {
      const QString csv = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-electronique/tiroirs.csv"));
      prom::ecrireFichierAtomique(prom::cheminFichier(csv.toStdString()), "Valeur;Boîtier;Quantité;Emplacement\n10k;0603;50;Tiroir A3\n");
      a->executerAction("bibliotheque.achats");
      attendre(200);
      if (DialogueAchats* d = a->achats()) {
        d->ajouterSource(csv, true);
        d->reglerCartes(10);
        attendre(100);
        for (const auto& b : d->besoins())
          if (b.ligne.quantite == 8) {
            trente = b.besoin == 80 && b.enStock == 50 && b.aAcheter == 30;
            detail = std::to_string(b.besoin) + " requises, " + std::to_string(b.enStock) + " en stock, " + std::to_string(b.aAcheter) + " à acheter";
          }
        trente = trente && d->table()->rowCount() == 2 && d->csv().contains(QStringLiteral("30"));
        d->close();
      }
    }
    verifier(trente, "4.18 : « Reste à acheter… », huit 10 kΩ par carte et 50 en tiroir : 30 à acheter pour dix cartes", detail);
  }

  // ---------- 4.20 · R* de valeur 10k : douze symboles échangés en une commande, repères et valeurs gardés ----------
  {
    ConceptionElec c0 = schemaResistances("Masse", 12, 3);
    DefSymbole us = *c0.symbole("R");
    us.id = "R_US";
    us.nom = "Résistance (dessin américain)";
    // Dessin différent : un zigzag à la place du rectangle, pour la comparaison A/B
    us.traces.clear();
    Trace zigzag;
    zigzag.forme = Trace::Forme::Polyligne;
    for (int i = 0; i <= 6; ++i) zigzag.points.push_back(ptMm(i == 0 || i == 6 ? 0.0 : (i % 2 ? 1.0 : -1.0), -3.0 + i));
    us.traces.push_back(zigzag);
    c0.symboles["R_US"] = us;
    AtelierElectronique* a = ouvrir(f, c0);
    bool douze = false, gardes = false, unPas = false, compare = false, empreintes = false;
    std::string detail;
    if (a) {
      const ConceptionElec* c = conception(doc);
      std::vector<std::string> un{c->composants.front().uuid};
      DialogueRemplacer d(*a, un, a);
      d.show();
      const int vises = d.appliquerPortee(QStringLiteral("R*"), QStringLiteral("10 kΩ"));
      d.choisirArticle("R_US");
      attendre(150);
      d.reglerTransparence(0);
      attendre(50);
      const QImage ia = d.superposition()->image();
      d.basculerAB();
      attendre(50);
      const QImage ib = d.superposition()->image();
      compare = d.transparence() == 100 && ia != ib;
      std::map<std::string, std::pair<std::string, std::string>> avant;
      for (const auto& id : d.composants()) avant[id] = {c->composant(id)->reference, c->composant(id)->valeur};
      const int pas = doc->positionHistorique();
      // Repère et valeur gardés (cases de la fenêtre)
      for (QCheckBox* k : d.findChildren<QCheckBox*>())
        if (k->text() == QStringLiteral("Valeur") || k->text() == QStringLiteral("Repère")) k->setChecked(true);
      const int n = d.appliquer();
      c = conception(doc);
      douze = vises == 12 && n == 12;
      unPas = doc->positionHistorique() == pas + 1;
      gardes = std::all_of(avant.begin(), avant.end(), [&](const auto& x) {
        const ComposantElec* k = c->composant(x.first);
        return k && k->symbole == "R_US" && k->reference == x.second.first && k->valeur == x.second.second;
      });
      d.close();
      // Empreintes échangées en masse sur la carte, en place
      DialogueEchangeEmpreintes e(*a, {}, a);
      const int ve = e.viser(QStringLiteral("R*"), QStringLiteral("10k"));
      e.reglerEmpreinte(QStringLiteral("R_0805"));
      const int ne = e.appliquer();
      c = conception(doc);
      empreintes = ve == 12 && ne == 12 && std::count_if(c->circuit.empreintes.begin(), c->circuit.empreintes.end(), [](const EmpreintePlacee& x) { return x.empreinte == "R_0805"; }) == 12;
      detail = std::to_string(vises) + " visés, " + std::to_string(n) + " remplacés, " + std::to_string(ne) + " empreintes échangées";
    }
    verifier(douze && gardes && unPas, "4.20 : portée « R* » de valeur 10k : douze symboles échangés en une commande, repères et valeurs gardés", detail);
    verifier(compare && empreintes, "4.20 : comparaison en superposition (curseur de transparence, bascule A/B) ; empreintes échangées en masse sur la carte", detail);
  }

  // ---------- 4.23 · Série E24 de 1 Ω à 1 MΩ en 0603 : 145 articles nommés d'après leur gabarit ----------
  {
    AtelierElectronique* a = ouvrir(f, carteSeule("Bibliothèque"));
    bool serie = false;
    std::string detail;
    if (a) {
      a->executerAction("bibliotheque.gerer");
      attendre(200);
      if (DialogueGestionBibliotheque* d = a->gestionBibliotheque()) {
        Article modele;
        modele.symbole = "R";
        modele.empreinte = "R_0603";
        modele.categorie = "Résistances";
        RenommageGabarit g;
        g.gabaritId = "R_{boitier}_{valeur}";
        g.gabaritNom = "Résistance {valeurSI} {boitier}";
        const int n = d->genererSerie(modele, 24, 1, 1e6, g);
        const QStringList ctrl = d->controler();
        bool nommes = false;
        for (int r = 0; r < d->table()->rowCount(); ++r) nommes |= d->table()->item(r, 0) && d->table()->item(r, 0)->text() == QStringLiteral("R_0603_4k7");
        serie = n == 145 && d->table()->rowCount() >= 145 && ctrl.isEmpty() && nommes;
        detail = std::to_string(n) + " articles, " + std::to_string(ctrl.size()) + " défaut(s)";
        d->close();
      }
    }
    verifier(serie, "4.23 : « Série E… » E24 de 1 Ω à 1 MΩ en 0603 : 145 articles nommés d’après leur gabarit, contrôles sans erreur", detail);
  }

  // ---------- 4.24 · Gabarit élargi de 0,1 mm : les 64 pastilles d'une TQFP-64 suivent, en une commande ----------
  {
    ConceptionElec c0 = carteSeule("Piles de pastilles");
    poserEmpreinteSeule(c0, "TQFP-64-10x10-0.5", ptMm(25, 15));
    AtelierElectronique* a = ouvrir(f, c0);
    bool suivent = false;
    std::string detail;
    if (a) {
      a->executerAction("circuit.gabaritsPastilles");
      attendre(200);
      if (DialogueGabaritsPastilles* d = a->gabaritsPastilles()) {
        d->relier();
        std::string nom;
        for (const auto& [n, g] : conception(doc)->circuit.gabarits)
          if (d->emplois(n) == 64) nom = n;
        const int pas = doc->positionHistorique();
        const int n = nom.empty() ? 0 : d->modifier(nom, 0.1, 0);
        const ConceptionElec* c = conception(doc);
        int larges = 0;
        for (const auto& e : c->circuit.empreintes)
          if (const DefEmpreinte* def = c->empreinte(e.empreinte))
            for (const auto& p : def->pastilles) larges += p.gabarit != nom && !p.gabarit.empty() ? 1 : 0;
        suivent = n == 64 && larges == 64 && doc->positionHistorique() == pas + 1;
        detail = "gabarit « " + nom + " », " + std::to_string(n) + " pastilles changées";
        d->close();
      }
    }
    verifier(suivent, "4.24 : gabarit élargi de 0,1 mm : les 64 pastilles d’une TQFP-64 qui l’emploient suivent, en une commande", detail);
  }

  // ---------- 4.26 · Liste de 40 noms en densités M, N et L : 120 empreintes rangées ----------
  {
    AtelierElectronique* a = ouvrir(f, carteSeule("Empreintes par noms"));
    bool cent20 = false;
    std::string detail;
    if (a) {
      a->executerAction("bibliotheque.serieEmpreintes");
      attendre(200);
      if (DialogueSerieEmpreintes* d = a->serieEmpreintes()) {
        d->reglerNoms({QStringLiteral("R_0402"), QStringLiteral("R_0603"), QStringLiteral("R_0805"), QStringLiteral("R_1206"), QStringLiteral("C_0402"), QStringLiteral("C_0603"),
                       QStringLiteral("C_0805"), QStringLiteral("C_1210"), QStringLiteral("L_0805"), QStringLiteral("LED_0603"), QStringLiteral("SOIC-8"), QStringLiteral("SOIC-14"),
                       QStringLiteral("SOIC-16"), QStringLiteral("SOIC-16W"), QStringLiteral("TSSOP-14"), QStringLiteral("TSSOP-20"), QStringLiteral("SSOP-28"), QStringLiteral("MSOP-10"),
                       QStringLiteral("QFN-24-4x4-0.5"), QStringLiteral("QFN-32-5x5-0.5"), QStringLiteral("TQFP-32-7x7-0.8"), QStringLiteral("TQFP-64-10x10-0.5"), QStringLiteral("LQFP-48-7x7-0.5"),
                       QStringLiteral("SOT-23"), QStringLiteral("SOT-23-5"), QStringLiteral("SOT-23-6"), QStringLiteral("SOT-89"), QStringLiteral("DPAK"), QStringLiteral("D2PAK"),
                       QStringLiteral("SOD-123"), QStringLiteral("SOD-323"), QStringLiteral("MiniMELF"), QStringLiteral("SMA"), QStringLiteral("SMB"), QStringLiteral("SMC"),
                       QStringLiteral("DFN-8-3x3-0.65"), QStringLiteral("Crystal_SMD_3225-4Pin"), QStringLiteral("PLCC-44"), QStringLiteral("BGA-256_17.0x17.0mm_Layout16x16_P1.0mm_Ball0.5mm"),
                       QStringLiteral("Edge-2x10_P2.54")});
        d->reglerDensites(true, true, true);
        const int n = d->generer();
        int dansBib = 0;
        for (const auto& e : AtelierElectronique::empreintesPerso()) dansBib += e.id.rfind("RESC", 0) == 0 || e.id.rfind("QFP", 0) == 0 ? 1 : 0;
        cent20 = n == 120 && d->refus().isEmpty() && dansBib >= 12;
        detail = std::to_string(n) + " rangées, " + std::to_string(d->refus().size()) + " refus";
        d->close();
      }
    }
    verifier(cent20, "4.26 : liste de 40 noms en densités M, N et L : 120 empreintes rangées, contrôles sans erreur", detail);
  }

  // ---------- 4.28 · Règle « Résistance 0603 → R_0603 » : 48 composants affectés en une commande ----------
  {
    AtelierElectronique* a = ouvrir(f, schemaResistances("Affectation", 48, 0, false));
    bool affectes = false, nonModale = false;
    std::string detail;
    if (a) {
      a->executerAction("bibliotheque.affecter");
      attendre(200);
      if (FenetreAffectation* w = a->affectation()) {
        nonModale = w->isVisible() && !w->isModal() && QApplication::activeModalWidget() == nullptr;
        const int lignes = w->table()->rowCount();
        w->reglerRegles(QStringLiteral("Résistance 0603 → R_0603"));
        const int pas = doc->positionHistorique();
        const int n = w->appliquerRegles();
        attendre(200);
        const ConceptionElec* c = conception(doc);
        const auto avec = std::count_if(c->composants.begin(), c->composants.end(), [](const ComposantElec& k) { return k.empreinte == "R_0603"; });
        affectes = lignes == 48 && n == 48 && avec == 48 && doc->positionHistorique() == pas + 1 && w->table()->rowCount() == 0;
        detail = std::to_string(lignes) + " lignes, " + std::to_string(n) + " affectés, " + std::to_string(doc->positionHistorique() - pas) + " pas";
        w->close();
      }
    }
    verifier(affectes && nonModale, "4.28 : fenêtre d’affectation non modale, règle « Résistance 0603 → R_0603 » : 48 composants affectés en une commande", detail);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Lot L6 de la 0.34 : boîtier du commerce (6.7), pièce montée (6.8), points de vue (6.11), écarts 3D (6.12), IDF
// (6.17), rendu (6.18)
void essaisElectronique034L6(FenetrePrincipale& f, const Verifier& verifier) {
  Document* doc = f.document();
  const QString dossier = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-electronique"));
  QDir().mkpath(dossier);

  // ---------- 6.7 · Collision détectée avec un boîtier importé ----------
  {
    ConceptionElec c0 = carteSeule("Boîtier du commerce");
    const std::string haut = poserEmpreinteSeule(c0, "R_0805", ptMm(35, 15));
    c0.composant(haut)->hauteur = 15;
    AtelierElectronique* a = ouvrir(f, c0);
    bool collision = false, drc = false;
    std::string detail;
    if (a) {
      prom::PieceNommee p;
      p.nom = "Boîtier";
      p.forme = BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 60, 40, 14).Shape(), BRepPrimAPI_MakeBox(gp_Pnt(2, 2, 2), 56, 36, 10).Shape()).Shape();
      const QString step = QDir(dossier).filePath(QStringLiteral("boitier-commerce.step"));
      prom::exporterStep({p}, step.toStdString());
      DialogueBoitierImporte d(*a, a);
      const bool lu = d.choisirFichier(step);
      // Carte de 50 × 30 dans un intérieur de 56 × 36 : le fond intérieur 4 mm sous la carte, le couvercle à 6 mm
      d.reglerPlace(-5, 35, -1.6 - 4 - 2, 0, 0.5);
      const int n = lu ? d.appliquer() : -1;
      a->recalculer();
      attendre(300);
      for (const auto& v : a->calculs().drc) drc |= v.code == "DRC-47" && v.objets == std::vector<std::string>{haut};
      collision = n == 1;
      detail = std::to_string(n) + " collision(s) à l'import";
    }
    verifier(collision && drc, "6.7 : boîtier du commerce importé en STEP : la collision du corps de 15 mm est détectée (DRC-47)", detail);
  }

  // ---------- 6.8 · Dissipateur listé dans la nomenclature commune ----------
  {
    ConceptionElec c0 = carteSeule("Pièces montées");
    const std::string u1 = poserEmpreinteSeule(c0, "R_0805", ptMm(20, 15));
    c0.pilotageBoitier = true;
    AtelierElectronique* a = ouvrir(f, c0);
    bool liste = false;
    std::string detail;
    if (a) {
      prom::PieceNommee p;
      p.nom = "Dissipateur";
      p.forme = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 15, 10, 20).Shape();
      const QString step = QDir(dossier).filePath(QStringLiteral("dissipateur.step"));
      prom::exporterStep({p}, step.toStdString());
      DialoguePieceMontee d(*a, u1, a);
      d.choisirFichier(step);
      d.reglerArticle(QStringLiteral("DS1"), QStringLiteral("Dissipateur TO-220"), QStringLiteral("Fischer"), QStringLiteral("SK 104"));
      const std::string uuid = d.appliquer();
      const auto lignes = prom::nomenclature(doc->projet(), prom::deriver(doc->projet()));
      liste = !uuid.empty() && std::any_of(lignes.begin(), lignes.end(), [](const prom::LigneNomenclature& l) { return l.groupe == "Pièces montées" && l.ref == "DS1"; });
      detail = std::to_string(lignes.size()) + " lignes dans la nomenclature commune";
    }
    verifier(liste, "6.8 : dissipateur posé sur la carte, listé dans la nomenclature commune", detail);
  }

  // ---------- 6.11 · Point de vue « Face USB » enregistré, projet rouvert : même caméra à 0,1° près ----------
  {
    AtelierElectronique* a = ouvrir(f, carteSeule("Points de vue"));
    bool meme = false;
    std::string detail;
    if (a) {
      a->setMode(AtelierElectronique::Vue3D);
      attendre(600);
      VuePiece* v = a->vue3DCarte();
      v->navigation().vueStandard(Navigation3D::Face::Avant);
      v->navigation().quartDeTour(true);
      attendre(100);
      prom::ecad::PointDeVue fixe;
      fixe.nom = "Face USB";
      fixe.cible[0] = 25, fixe.cible[1] = -15, fixe.cible[2] = 2;
      fixe.azimut = -93.37, fixe.elevation = 12.06, fixe.distance = 120;
      a->appliquerPointDeVue3D(fixe);
      a->enregistrerPointDeVue3D(QStringLiteral("Face USB"));
      const QString fichier = QDir(dossier).filePath(QStringLiteral("points-de-vue.promethee"));
      doc->enregistrer(fichier, true);
      doc->ouvrir(fichier, true);
      f.montrerElectronique(0, AtelierElectronique::Vue3D);
      attendre(600);
      a = f.atelierElectronique();
      const ConceptionElec* c = conception(doc);
      if (a && c && !c->pointsDeVue.empty()) {
        a->vue3DCarte()->navigation().vueStandard(Navigation3D::Face::Dessus);  // ailleurs, puis retour
        a->appliquerPointDeVue3D(c->pointsDeVue.front());
        const prom::ecad::PointDeVue r = a->pointDeVue3D("lu");
        meme = std::abs(r.azimut - fixe.azimut) < 0.1 && std::abs(r.elevation - fixe.elevation) < 0.1 && std::abs(r.distance - fixe.distance) < 0.1;
        char t[160];
        std::snprintf(t, sizeof t, "azimut %.3f°, élévation %.3f°, distance %.2f", r.azimut, r.elevation, r.distance);
        detail = t;
      }
    }
    verifier(meme, "6.11 : point de vue « Face USB » enregistré, projet rouvert : même caméra à 0,1° près", detail);
  }

  // ---------- 6.12 · Deux condensateurs à 0,4 mm sous une règle de 0,5 mm ----------
  {
    ConceptionElec c0 = carteSeule("Écart 3D");
    const std::string c1 = poserEmpreinteSeule(c0, "C_0805", ptMm(20, 15), 0, false, "C");
    const std::string c2 = poserEmpreinteSeule(c0, "C_0805", ptMm(22.4, 15), 0, false, "C");
    Contrainte k;
    k.nom = "Condensateurs";
    k.genre = GenreContrainte::Ecart3D;
    k.portee = "prefixe == 'C'";
    k.valeur.min = nm(0.5);
    c0.circuit.regles.avancees.contraintes.push_back(k);
    AtelierElectronique* a = ouvrir(f, c0);
    bool ecart = false, mesure = false;
    std::string detail;
    if (a) {
      a->recalculer();
      attendre(300);
      for (const auto& v : a->calculs().drc)
        if (v.code == "DRC-75") {
          std::vector<std::string> o = v.objets, att{c1, c2};
          std::sort(o.begin(), o.end());
          std::sort(att.begin(), att.end());
          ecart = o == att;
          detail = v.message;
        }
      const QString m = a->mesurerCorps3D(c1, c2);
      mesure = m.contains(QStringLiteral("0,400")) && m.contains(QStringLiteral("ΔZ"));
    }
    verifier(ecart && mesure, "6.12 : deux condensateurs à 0,4 mm sous une règle de 0,5 mm : un écart de la DRC qui mène aux deux corps ; mesure ΔX ΔY ΔZ", detail);
  }

  // ---------- 6.17 · .emn et .emp réimportés ----------
  {
    ConceptionElec c0 = carteSeule("Carte IDF");
    for (const Pt p : {ptMm(3, 3), ptMm(47, 3), ptMm(3, 27), ptMm(47, 27)}) poserEmpreinteSeule(c0, "MountingHole_3.2", p, 0, false, "H");
    for (int i = 0; i < 32; ++i) {
      const std::string id = poserEmpreinteSeule(c0, "R_0805", ptMm(8 + (i % 8) * 4.5, 7 + (i / 8) * 5), 0, false, "R");
      c0.composant(id)->hauteur = 0.5 + 0.05 * i;
    }
    AtelierElectronique* a = ouvrir(f, c0);
    bool relu = false;
    std::string detail;
    if (a) {
      const QString emn = QDir(dossier).filePath(QStringLiteral("carte.emn"));
      const int avant = static_cast<int>(doc->projet().electronique.size());
      if (a->exporterIdf(emn) && a->importerIdf(emn)) {
        const ConceptionElec& r = doc->projet().electronique.back();
        int aLeurHauteur = 0, trous = 0;
        for (const auto& x : c0.composants)
          if (x.reference[0] == 'R')
            if (const ComposantElec* y = r.parReference(x.reference); y && std::abs(hauteurComposant(r, *y).mm - x.hauteur) < 1e-3) ++aLeurHauteur;
        for (const auto& p : pastilles(r, connectivite(r))) trous += p.pastille.percage == nm(3.2) ? 1 : 0;
        relu = static_cast<int>(doc->projet().electronique.size()) == avant + 1 && aLeurHauteur == 32 && trous == 4 && r.circuit.contour.polygone().size() == c0.circuit.contour.polygone().size();
        detail = std::to_string(aLeurHauteur) + " composants à leur hauteur, " + std::to_string(trous) + " trous";
      }
    }
    verifier(relu, "6.17 : fichiers .emn et .emp réimportés : même contour, mêmes trous, 32 composants à leur hauteur", detail);
  }

  // ---------- 6.18 · Rendu de 1 920 × 1 080 à fond transparent ; interface fluide pendant le calcul ----------
  {
    ConceptionElec c0 = carteSeule("Rendu");
    poserEmpreinteSeule(c0, "SOIC-8", ptMm(25, 15));
    poserEmpreinteSeule(c0, "R_0805", ptMm(10, 10));
    AtelierElectronique* a = ouvrir(f, c0);
    bool png = false, fluide = false;
    std::string detail;
    if (a) {
      a->setMode(AtelierElectronique::Vue3D);
      attendre(500);
      a->executerAction("vue3d.rendu");
      attendre(200);
      if (FenetreRendu* r = a->rendu()) {
        r->reglerTaille(1920, 1080);
        r->reglerFondTransparent(true);
        const QString fichier = QDir(dossier).filePath(QStringLiteral("rendu.png"));
        QFile::remove(fichier);
        QElapsedTimer horloge;
        horloge.start();
        qint64 dernier = 0, plusLong = 0;
        QTimer battement;
        QObject::connect(&battement, &QTimer::timeout, [&] {
          plusLong = std::max(plusLong, horloge.elapsed() - dernier);
          dernier = horloge.elapsed();
        });
        battement.start(20);
        r->lancer(fichier);
        for (int i = 0; i < 1200 && r->enCours(); ++i) attendre(100);
        battement.stop();
        const QImage img(fichier);
        bool transparent = false, opaque = false;
        if (!img.isNull())
          for (int y = 0; y < img.height(); y += 7)
            for (int x = 0; x < img.width(); x += 7) {
              const int al = qAlpha(img.pixel(x, y));
              transparent |= al == 0;
              opaque |= al == 255;
            }
        png = img.width() == 1920 && img.height() == 1080 && img.hasAlphaChannel() && transparent && opaque;
        fluide = plusLong < 250;
        detail = std::to_string(horloge.elapsed()) + " ms, plus longue attente de l'interface " + std::to_string(plusLong) + " ms";
        r->close();
      }
    }
    verifier(png && fluide, "6.18 : rendu de 1 920 × 1 080 à fond transparent : PNG avec canal alpha, interface fluide pendant le calcul", detail);
  }
}

namespace {
// Extrémité d'une broche d'un symbole posé
Pt brocheDe(const ConceptionElec& c, const std::string& symbole, const std::string& numero) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == symbole)
        for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
          if (b.numero == numero) return b.p;
  return {};
}
std::string composantDuSymbole(const ConceptionElec& c, const std::string& symbole) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == symbole) return s.composant;
  return {};
}
void etiqueter(ConceptionElec& c, std::size_t f, Pt p, const std::string& t, TypeEtiquette type = TypeEtiquette::Locale) {
  Etiquette e;
  e.uuid = nouvelUuid();
  e.texte = t;
  e.p = p;
  e.type = type;
  c.feuilles[f].etiquettes.push_back(e);
}
std::map<std::string, std::set<std::string>> brochesParReseau(const ConceptionElec& c) {
  std::map<std::string, std::set<std::string>> r;
  for (const auto& x : connectivite(c).reseaux)
    for (const auto& b : x.broches) r[x.nom].insert(b.composant + ":" + b.broche);
  return r;
}
}  // namespace

void essaisElectronique034L7(FenetrePrincipale& f, const Verifier& verifier) {
  Document* doc = f.document();
  const QString dossier = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-electronique"));
  QDir().mkpath(dossier);

  // ---------- 7.2, 7.3 · Migration en hiérarchie : mêmes réseaux ; fil d'Ariane ; port proposé puis synchronisé ----------
  {
    ConceptionElec c0 = carteSeule("Hiérarchie");
    c0.feuilles[0].nom = "Logique";
    Feuille f2;
    f2.uuid = nouvelUuid();
    f2.nom = "Alimentation";
    c0.feuilles.push_back(f2);
    const std::string r1 = poserSymbole(c0, 0, "R", ptMm(50.8, 50.8));
    const std::string r2 = poserSymbole(c0, 1, "R", ptMm(50.8, 50.8));
    etiqueter(c0, 0, brocheDe(c0, r1, "1"), "DATA", TypeEtiquette::Globale);
    etiqueter(c0, 1, brocheDe(c0, r2, "1"), "DATA", TypeEtiquette::Globale);
    etiqueter(c0, 1, brocheDe(c0, r2, "2"), "VOUT", TypeEtiquette::Hierarchique);
    const auto avant = brochesParReseau(c0);
    AtelierElectronique* a = ouvrir(f, c0);
    bool memes = false, ariane = false, port = false;
    std::string detail;
    if (a) {
      a->setMode(AtelierElectronique::Schema);
      const int racine = a->migrerEnHierarchie();
      const ConceptionElec* c = conception(doc);
      memes = racine == 0 && c && c->feuilles.size() == 3 && brochesParReseau(*c) == avant;
      // Fille ouverte : le fil d'Ariane ramène à la racine d'un clic
      a->vueSchema()->setConception(0, 2);
      a->vueSchema()->repaint();
      attendre(100);
      const auto zones = a->vueSchema()->zonesAriane();
      if (!zones.empty()) {
        reel::cliquer(a->vueSchema(), zones.front().first.center().toPoint());
        attendre(100);
        ariane = a->vueSchema()->feuille() == 0;
      }
      // Le port VOUT manquait au bloc : ERC-33, puis synchronisé en un pas
      a->vueSchema()->setConception(0, 0);
      const int n = a->synchroniserPortsBlocs();
      c = conception(doc);
      port = n == 1 && c && verifierPortsHierarchiques(*c).empty();
      detail = std::to_string(zones.size()) + " niveau(x) au fil d'Ariane, " + std::to_string(n) + " port(s) ajouté(s)";
    }
    verifier(memes && ariane && port, "7.2, 7.3 : feuilles migrées en hiérarchie (mêmes réseaux), fil d'Ariane cliquable, port de la fille ajouté au bloc", detail);
  }

  // ---------- 7.8 · Deux portes d'un 74HC00 échangées : schéma et circuit d'accord ----------
  {
    ConceptionElec c0 = carteSeule("Portes");
    const std::string ua = poserSymbole(c0, 0, "74HC00_unites", ptMm(50.8, 50.8));
    const std::string k = composantDuSymbole(c0, ua);
    const std::string ub = poserUniteEn(c0, 0, k, 2, ptMm(101.6, 50.8));
    const std::string ra = poserSymbole(c0, 0, "R", ptMm(25.4, 101.6));
    etiqueter(c0, 0, brocheDe(c0, ua, "1"), "IN_A");
    etiqueter(c0, 0, brocheDe(c0, ra, "1"), "IN_A");
    mettreAJourCircuit(c0);
    AtelierElectronique* a = ouvrir(f, c0);
    bool accord = false;
    std::string detail;
    if (a) {
      a->setMode(AtelierElectronique::Schema);
      a->selection()->choisir({{SelectionElectronique::Genre::Symbole, ua, k}, {SelectionElectronique::Genre::Symbole, ub, k}});
      const bool fait = a->echangerPortesChoisies();
      const ConceptionElec* c = conception(doc);
      std::string reseau4;
      if (c)
        for (const auto& p : pastilles(*c, connectivite(*c)))
          if (p.composant == k && p.numero == "4") reseau4 = p.reseau;
      accord = fait && reseau4 == "IN_A";
      detail = "pastille 4 sur « " + reseau4 + " »";
    }
    verifier(accord, "7.8 : deux portes d'un 74HC00 échangées : IN_A passe de la broche 1 à la broche 4, au schéma comme sur la carte", detail);
  }

  // ---------- 7.14 · Directive « USB 90 Ω » : classe reprise par les règles, graphe des réseaux ----------
  {
    ConceptionElec c0 = carteSeule("Directives");
    const std::string r1 = poserSymbole(c0, 0, "R", ptMm(50.8, 50.8));
    const std::string r2 = poserSymbole(c0, 0, "R", ptMm(101.6, 50.8));
    for (const char* n : {"1", "2"}) {
      const std::string nom = std::string(n) == "1" ? "D+" : "D-";
      etiqueter(c0, 0, brocheDe(c0, r1, n), nom);
      etiqueter(c0, 0, brocheDe(c0, r2, n), nom);
      etiqueter(c0, 0, brocheDe(c0, r1, n), "USB 90 Ω", TypeEtiquette::Directive);
    }
    AtelierElectronique* a = ouvrir(f, c0);
    bool classe = false, graphe = false;
    std::string detail;
    if (a) {
      a->setMode(AtelierElectronique::Schema);
      const int n = a->appliquerDirectivesSchema();
      const ConceptionElec* c = conception(doc);
      const ClasseReseau* k = c ? classeDe(c->circuit.regles, "D+") : nullptr;
      classe = n == 2 && k && k->nom == "USB 90 Ω";
      a->ouvrirGrapheReseaux();
      attendre(200);
      graphe = a->grapheReseaux() && !a->grapheReseaux()->vue()->graphe().noeuds.empty();
      if (a->grapheReseaux()) a->grapheReseaux()->close();
      detail = std::to_string(n) + " réseau(x) rangé(s)";
    }
    verifier(classe && graphe, "7.14 : directive « USB 90 Ω » sur D+ et D− : classe reprise par les règles du circuit ; graphe des réseaux montré", detail);
  }

  // ---------- 7.16 · Quatre voies à plat : disposition de la première recopiée, 27 composants appariés ----------
  {
    ConceptionElec c0 = carteSeule("Voies");
    c0.circuit.contour.largeur = nm(100);
    c0.circuit.contour.hauteur = nm(80);
    struct Lien {
      const char *symbole, *a, *b;
    };
    const Lien voie[9] = {{"R", "VCC", "n1"}, {"R", "n1", "n2"}, {"C", "n2", "GND"}, {"R", "n2", "n3"}, {"C", "n3", "GND"}, {"R", "n3", "n4"}, {"R", "n4", "n5"}, {"C", "n5", "GND"}, {"R", "n4", "GND"}};
    std::vector<std::string> premiere;
    for (int v = 0; v < 4; ++v)
      for (int j = 0; j < 9; ++j) {
        const std::string s = poserSymbole(c0, 0, voie[j].symbole, ptMm(25.4 + 12.7 * j, 25.4 + 25.4 * v));
        if (v == 0) premiere.push_back(composantDuSymbole(c0, s));
        for (const auto& [n, extremite] : {std::pair<std::string, std::string>{voie[j].a, "1"}, {voie[j].b, "2"}}) {
          const bool commun = n == "VCC" || n == "GND";
          etiqueter(c0, 0, brocheDe(c0, s, extremite), commun ? n : "V" + std::to_string(v + 1) + "_" + n, commun ? TypeEtiquette::Globale : TypeEtiquette::Locale);
        }
      }
    mettreAJourCircuit(c0);
    for (int j = 0; j < 9; ++j)
      for (auto& e : c0.circuit.empreintes)
        if (e.composant == premiere[static_cast<std::size_t>(j)]) e.position = ptMm(10 + 6 * j, 10);
    AtelierElectronique* a = ouvrir(f, c0);
    int n = -1;
    if (a) {
      std::vector<SelectionElectronique::Ref> refs;
      for (const auto& id : premiere) refs.push_back({SelectionElectronique::Genre::Empreinte, id, id});
      a->selection()->choisir(refs);
      n = a->recopierDispositionChoisie(0, 15);
    }
    verifier(n == 27, "7.16 : quatre voies de neuf composants à plat : disposition de la première recopiée sur les trois autres, 27 composants appariés seuls", std::to_string(n) + " composant(s) placé(s)");
  }

  // ---------- 7.19 · DXF de trois calques, un seul choisi : 24 traits en formes ----------
  {
    std::string dxf = "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n";
    for (const char* nom : {"CADRE", "TEXTE", "COTES"}) dxf += std::string("0\nLAYER\n2\n") + nom + "\n70\n0\n62\n1\n";
    dxf += "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
    auto ligne = [&](const char* calque, double x1, double y1, double x2, double y2) {
      dxf += std::string("0\nLINE\n8\n") + calque + "\n10\n" + std::to_string(x1) + "\n20\n" + std::to_string(y1) + "\n11\n" + std::to_string(x2) + "\n21\n" + std::to_string(y2) + "\n";
    };
    for (int i = 0; i < 24; ++i) ligne("CADRE", 5.0 * i, 0, 5.0 * i, 30);
    for (int i = 0; i < 6; ++i) ligne("TEXTE", 0, 5.0 * i, 20, 5.0 * i);
    ligne("COTES", 0, -10, 60, -10);
    dxf += "0\nENDSEC\n0\nEOF\n";
    const QString chemin = QDir(dossier).filePath(QStringLiteral("trois-calques.dxf"));
    QFile fic(chemin);
    if (fic.open(QIODevice::WriteOnly | QIODevice::Truncate)) fic.write(QByteArray::fromStdString(dxf));
    fic.close();
    AtelierElectronique* a = ouvrir(f, carteSeule("Formes"));
    int n = -1;
    std::size_t formes = 0;
    if (a) {
      a->setMode(AtelierElectronique::Schema);
      n = a->importerFormesFeuille(chemin, {"CADRE"});
      if (const ConceptionElec* c = conception(doc)) formes = c->feuilles[0].formes.size();
      a->vueSchema()->repaint();
    }
    verifier(n == 24 && formes == 24, "7.19 : DXF de trois calques, un seul choisi : ses 24 traits deviennent des formes modifiables sur la feuille",
             std::to_string(n) + " forme(s) posée(s)");
  }

  // ---------- 7.20 · Assistant : MOSFET canal P en SOT-23, G S D sur 1 2 3, armé à la pose ----------
  {
    AtelierElectronique* a = ouvrir(f, carteSeule("Assistant"));
    bool juste = false;
    std::string detail;
    if (a) {
      DialogueAssistantSymbole d(*a, a);
      d.regler(QStringLiteral("MOSFET-P"), QStringLiteral("SOT-23"));
      const auto s = d.symbole();
      std::map<std::string, std::string> parNom;
      if (s)
        for (const auto& b : s->broches) parNom[b.nom] = b.numero;
      const bool pose = d.poser();
      juste = s && parNom["G"] == "1" && parNom["S"] == "2" && parNom["D"] == "3" && controlerSymbole(*s).empty() && pose && !a->vueSchema()->symboleArme().empty();
      detail = "symbole armé : " + a->vueSchema()->symboleArme();
      a->vueSchema()->echap();
    }
    verifier(juste, "7.20 : MOSFET canal P en SOT-23 demandé à l'assistant : dessin normalisé, G, S et D sur 1, 2 et 3", detail);
  }

  // ---------- 7.21, 7.9 · BGA 16 × 16 renuméroté de A1 à T16 ; empreinte retouchée enregistrée ----------
  {
    ConceptionElec c0 = carteSeule("Pastilles");
    DefEmpreinte bga;
    bga.id = "BGA-256-ESSAI";
    for (int r = 0; r < 16; ++r)
      for (int col = 0; col < 16; ++col) {
        Pastille p;
        p.numero = "?";
        p.forme = FormePastille::Rond;
        p.largeur = p.hauteur = 450'000;
        p.position = {nm(col - 7.5), nm(r - 7.5)};
        bga.pastilles.push_back(p);
      }
    c0.empreintes[bga.id] = bga;
    AtelierElectronique* a = ouvrir(f, c0);
    bool juste = false;
    std::string detail;
    if (a) {
      a->ouvrirPastilles(bga.id);
      attendre(100);
      DialoguePastilles* d = a->pastillesEmpreinte();
      if (d) {
        const int avant = d->doublons();
        const int n = d->renumeroterMatrice();
        std::set<std::string> numeros;
        for (const auto& p : d->empreinte().pastilles) numeros.insert(p.numero);
        d->enregistrer(false);
        const ConceptionElec* c = conception(doc);
        const DefEmpreinte* e = c ? c->empreinte(bga.id) : nullptr;
        juste = avant == 1 && n == 256 && d->doublons() == 0 && numeros.size() == 256 && numeros.count("A1") && numeros.count("T16") && !numeros.count("I1") && e &&
                e->pastilles.front().numero == "A1";
        detail = std::to_string(numeros.size()) + " numéros distincts, tableau de " + std::to_string(d->tableau()->rowCount()) + " lignes";
        d->close();
      }
    }
    verifier(juste, "7.21, 7.9 : BGA 16 × 16 renuméroté de A1 à T16 (I, O, Q, S sautées), sans doublon, enregistré au projet", detail);
  }

  // ---------- 7.24 · Pastille 1 de U2 agrandie sur la carte : bibliothèque et autres SOIC-8 inchangées, écart affiché ----------
  {
    ConceptionElec c0 = carteSeule("Cuivre propre");
    const std::string s1 = poserSymbole(c0, 0, "NE555", ptMm(50.8, 50.8));
    const std::string s2 = poserSymbole(c0, 0, "NE555", ptMm(101.6, 50.8));
    const std::string u1 = composantDuSymbole(c0, s1), u2 = composantDuSymbole(c0, s2);
    for (auto& x : c0.composants) x.reference = x.uuid == u1 ? "U1" : "U2";
    mettreAJourCircuit(c0);
    AtelierElectronique* a = ouvrir(f, c0);
    bool juste = false;
    std::string detail;
    if (a) {
      a->setMode(AtelierElectronique::Circuit);
      const DefEmpreinte biblio = *conception(doc)->empreinte("SOIC-8");
      a->selection()->choisir({{SelectionElectronique::Genre::Empreinte, u2, u2}});
      const QString copie = a->copieLocaleChoisie();
      attendre(100);
      DialoguePastilles* d = a->pastillesEmpreinte();
      if (d) {
        DefEmpreinte retouche = d->empreinte();
        for (auto& p : retouche.pastilles)
          if (p.numero == "1") p.largeur += 400'000;
        d->setEmpreinte(retouche);
        d->enregistrer(false);
        d->close();
      }
      const ConceptionElec* c = conception(doc);
      const auto ecart = c ? ecartBibliotheque(*c, u2) : std::vector<std::string>{};
      bool u1Biblio = false;
      if (c)
        for (const auto& e : c->circuit.empreintes)
          if (e.composant == u1) u1Biblio = e.empreinte == "SOIC-8";
      juste = c && copie == QStringLiteral("SOIC-8~local~U2") && ecart.size() == 1 && *c->empreinte("SOIC-8") == biblio && u1Biblio;
      detail = ecart.empty() ? std::string("aucun écart") : ecart.front();
    }
    verifier(juste, "7.24 : pastille 1 de U2 agrandie sur la carte : bibliothèque et autres SOIC-8 inchangées, écart affiché", detail);
  }

  // ---------- 7.25 · Jeu « école » chargé : sérigraphie à 1,2 mm, fils de la classe Puissance en rouge de 0,5 mm ----------
  {
    AtelierElectronique* a = ouvrir(f, carteSeule("Styles"));
    bool juste = false;
    std::string detail;
    if (a) {
      const bool ok = a->appliquerJeuStylesNomme(QStringLiteral("école"));
      a->modifierConception([](ConceptionElec& c) { c.circuit.regles.classeReseau["VIN"] = "Puissance"; });
      const ConceptionElec* c = conception(doc);
      const StyleFil st = c ? styleDuFil(*c, "VIN") : StyleFil{};
      juste = ok && c && c->circuit.serigraphie.hauteur == 1'200'000 && st.couleur == "#E53935" && st.epaisseur == 500'000;
      detail = "fils de VIN : " + st.couleur + ", " + std::to_string(st.epaisseur) + " nm";
    }
    verifier(juste, "7.25 : jeu « école » chargé : sérigraphie à 1,2 mm, fils de la classe Puissance en rouge de 0,5 mm", detail);
  }
}
