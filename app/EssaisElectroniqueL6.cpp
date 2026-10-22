// Prométhée : essais d'interface du lot L6 de l'audit (« Mécatronique ») : modèle STEP associé à un régulateur, qui
// remplace sa boîte et donne sa hauteur ; self trop haute pour le couvercle, signalée par la DRC de l'atelier ;
// connecteur USB déplacé au bord, sa découpe qui suit ; façade dont la découpe, la légende et l'échelle suivent le
// potentiomètre, contrôlée puis sortie en SVG et en DXF ; contour piloté par une esquisse ; dossier de modèles associé
// d'un coup. Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L6.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

#include <BRepPrimAPI_MakeBox.hxx>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QLabel>
#include <QTableWidget>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetreFacade.hpp"
#include "FenetrePrincipale.hpp"
#include "InspecteurElectronique.hpp"
#include "VueCircuit.hpp"
#include "promethee/carte3d.hpp"
#include "promethee/derive.hpp"
#include "promethee/facade.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/hauteurs.hpp"
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
// Conception d'essai : symboles posés, circuit mis à jour, empreintes à leur place sur une carte l × h
struct Pose {
  std::string symbole;
  double x, y;  // sur la carte (mm)
};
ConceptionElec conceptionAvec(double l, double h, const std::vector<Pose>& poses, std::vector<std::string>* ids) {
  ConceptionElec c = nouvelleConception("Essais L6");
  std::vector<std::string> s;
  for (std::size_t i = 0; i < poses.size(); ++i) s.push_back(poserSymbole(c, 0, poses[i].symbole, ptMm(25.4 + 25.4 * static_cast<double>(i), 50.8)));
  annoter(c, false);
  mettreAJourCircuit(c);
  c.circuit.contour.largeur = nm(l);
  c.circuit.contour.hauteur = nm(h);
  c.circuit.contour.rayon = 0;
  for (std::size_t i = 0; i < poses.size(); ++i) {
    std::string id;
    for (const auto& p : c.feuilles[0].symboles)
      if (p.uuid == s[i]) id = p.composant;
    for (auto& e : c.circuit.empreintes)
      if (e.composant == id) e.position = ptMm(poses[i].x, poses[i].y);
    if (ids) ids->push_back(id);
  }
  return c;
}
AtelierElectronique* ouvrir(FenetrePrincipale& f, ConceptionElec c, bool boitier = false, std::function<void(prom::Projet&)> regler = {}) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = !boitier;
  p.nom = c.nom;
  c.pilotageBoitier = boitier;
  p.electronique.push_back(std::move(c));
  if (regler) regler(p);
  f.document()->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Circuit);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return nullptr;
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Verifications, AtelierElectronique::Panneau::Recherche,
                       AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts, AtelierElectronique::Panneau::Champs,
                       AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  if (boitier) a->lierAuBoitier(false);
  a->recalculer();
  attendre(250);
  a->vueCircuit()->recadrer();
  return a;
}
QString dossierEssai(const QString& nom) {
  const QString d = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-l6-") + nom);
  QDir(d).removeRecursively();
  QDir().mkpath(d);
  return d;
}
// Boîte STEP d'essai : l × p × h, posée en (x0, y0, z0) dans son repère
QString boiteStep(const QString& dossier, const QString& nom, double l, double p, double h, double x0 = 3, double y0 = -2, double z0 = 0.4) {
  const QString f = QDir(dossier).filePath(nom);
  prom::exporterStep({prom::PieceNommee{nom.toStdString(), BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), l, p, h).Shape()}}, f.toStdString());
  return f;
}
const prom::GeoComp* geoDe(const prom::Derive& d, const std::string& id) {
  for (const auto& g : d.comps)
    if (g.id == id) return &g;
  return nullptr;
}
}  // namespace

void essaisElectroniqueL6(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();

  // ---------- 6.2 · Modèle STEP associé à un régulateur : la boîte disparaît, la hauteur vient du modèle ----------
  {
    std::vector<std::string> ids;
    AtelierElectronique* a = ouvrir(f, conceptionAvec(50, 30, {{"Regulateur", 25, 15}}, &ids));
    if (!a) {
      verifier(false, "L6 : la carte d'essai s'ouvre", {});
      return;
    }
    verifier(reel::activer(&f), "L6 : la fenêtre est active", {});
    const QString d = dossierEssai(QStringLiteral("modele"));
    const QString fichier = boiteStep(d, QStringLiteral("AMS1117.step"), 6.5, 3.5, 1.8);
    const bool associe = a->associerModele3D(ids[0], fichier);
    attendre(200);
    const ConceptionElec* c = conception(doc);
    const ComposantElec* k = c->composant(ids[0]);
    const HauteurComposant h = hauteurComposant(*c, *k);
    // L'inspecteur le dit, et le corps 3D est le modèle
    a->montrerComposant(ids[0]);
    attendre(250);
    QLabel* prov = a->inspecteur()->findChild<QLabel*>(QStringLiteral("provenanceHauteur"));
    const bool ditExact = prov && prov->text().contains(QStringLiteral("modèle exact"));
    double hauteur3D = 0;
    for (const auto& e : c->circuit.empreintes)
      if (e.composant == ids[0]) {
        const prom::Boite b = prom::encombrement(prom::corpsComposant(*c, e));
        hauteur3D = b.zmax - b.zmin;
      }
    verifier(associe && h.provenance == ProvenanceHauteur::ModeleExact && std::abs(h.mm - 1.8) < 1e-3 && ditExact && std::abs(hauteur3D - 1.8) < 1e-3,
             "6.2 : le modèle STEP associé au régulateur remplace sa boîte ; sa hauteur, 1,8 mm, devient celle du composant, provenance « modèle exact »",
             std::to_string(h.mm) + " mm, " + nomProvenance(h.provenance) + (prov ? " ; inspecteur : " + prov->text().toStdString() : std::string()));
  }

  // ---------- 6.4 · Self de 12 mm sous un couvercle à 10 mm : erreur de la DRC de l'atelier, qui mène au composant ----------
  {
    std::vector<std::string> ids;
    ConceptionElec c0 = conceptionAvec(50, 30, {{"L", 25, 15}}, &ids);
    c0.composant(ids[0])->hauteur = 12;
    c0.composant(ids[0])->provenanceHauteur = ProvenanceHauteur::Fiche;
    AtelierElectronique* a = ouvrir(f, c0, true, [](prom::Projet& p) {
      p.boitier.hauteurAuto = false;
      p.boitier.hauteur = p.boitier.entretoise + 1.6 + 10;
    });
    bool signalee = false;
    if (a) {
      a->recalculer();
      attendre(200);
      for (const auto& v : a->calculs().drc) signalee |= v.code == "DRC-45" && v.objets == std::vector<std::string>{ids[0]};
    }
    verifier(signalee, "6.4 : une self de 12 mm sous un couvercle à 10 mm est une erreur de la DRC de l'atelier, qui vise le composant", {});
  }

  // ---------- 6.6 · Connecteur USB déplacé au bord de la carte : sa découpe dans la paroi le suit ----------
  {
    std::vector<std::string> ids;
    ConceptionElec c0 = conceptionAvec(50, 30, {{"Conn_01x04", 20, 2.5}}, &ids);
    // Une empreinte de connecteur USB-C : celle de la barrette, renommée (le rôle vient de son nom)
    const std::string base = c0.composant(ids[0])->empreinte;
    DefEmpreinte usb = *c0.empreinte(base);
    usb.id = "USB_C_Receptacle_essai";
    c0.empreintes[usb.id] = usb;
    c0.composant(ids[0])->empreinte = usb.id;
    for (auto& e : c0.circuit.empreintes)
      if (e.composant == ids[0]) e.empreinte = usb.id, e.rotation = 0;
    AtelierElectronique* a = ouvrir(f, c0, true);
    bool suit = false;
    std::string detail;
    if (a) {
      const prom::Derive avant = prom::deriver(doc->projet());
      const prom::GeoComp* g0 = geoDe(avant, ids[0]);
      VueCircuit* vc = a->vueCircuit();
      vc->choisir(VueCircuit::Genre::Empreinte, ids[0]);
      vc->deplacerSelection(ptMm(5, 0));
      attendre(250);
      const prom::Derive apres = prom::deriver(doc->projet());
      const prom::GeoComp* g1 = geoDe(apres, ids[0]);
      if (g0 && g1 && g0->decoupe && g1->decoupe) {
        suit = std::abs((g1->decoupe->u - g0->decoupe->u) - 5) < 0.05 && g1->decoupe->mur == g0->decoupe->mur;
        detail = std::to_string(g0->decoupe->u) + " → " + std::to_string(g1->decoupe->u);
      } else
        detail = g0 && g0->decoupe ? "découpe perdue" : "pas de découpe au départ";
    }
    verifier(suit, "6.6 : le connecteur USB déplacé de 5 mm le long du bord déplace sa découpe dans la paroi de 5 mm", detail);
  }

  // ---------- 6.14, 6.15 · Façade : découpe, légende et échelle suivent le potentiomètre ; contrôle, SVG, DXF ----------
  {
    std::vector<std::string> ids;
    AtelierElectronique* a = ouvrir(f, conceptionAvec(80, 50, {{"Potentiometre", 30, 25}}, &ids));
    bool suivent = false, sortie = false;
    std::string detail;
    if (a) {
      VueCircuit* vc = a->vueCircuit();
      vc->choisir(VueCircuit::Genre::Empreinte, ids[0]);
      attendre(100);
      a->executerAction("boitier.facade");
      attendre(200);
      FenetreFacade* fa = a->facade();
      const int n = fa ? fa->ajouterPourSelection(true) : 0;
      std::vector<PositionFacade> avant;
      if (fa && fa->facade())
        for (const auto& e : fa->facade()->elements) avant.push_back(positionElement(*conception(doc), e));
      vc->choisir(VueCircuit::Genre::Empreinte, ids[0]);
      vc->deplacerSelection(ptMm(10, 0));
      attendre(250);
      int justes = 0;
      if (fa && fa->facade())
        for (std::size_t i = 0; i < fa->facade()->elements.size() && i < avant.size(); ++i) {
          const PositionFacade p = positionElement(*conception(doc), fa->facade()->elements[i]);
          justes += std::abs(p.x - avant[i].x - 10) < 1e-6 && std::abs(p.y - avant[i].y) < 1e-6;
        }
      suivent = n == 3 && justes == 3;
      const QString d = dossierEssai(QStringLiteral("facade"));
      QString raison;
      sortie = fa && fa->controler().empty() && fa->exporter(QDir(d).filePath(QStringLiteral("facade.svg")), QDir(d).filePath(QStringLiteral("facade.dxf")), &raison);
      QFile svg(QDir(d).filePath(QStringLiteral("facade.svg"))), dxf(QDir(d).filePath(QStringLiteral("facade.dxf")));
      sortie = sortie && svg.open(QIODevice::ReadOnly) && dxf.open(QIODevice::ReadOnly) && svg.readAll().contains("<circle") && dxf.readAll().contains("CIRCLE");
      detail = std::to_string(n) + " élément(s), " + std::to_string(justes) + " suivi(s)" + (raison.isEmpty() ? std::string() : ", " + raison.toStdString());
      if (fa) fa->close();
    }
    verifier(suivent && sortie, "6.14 : le potentiomètre déplacé de 10 mm déplace sa découpe, sa légende et son échelle ; la façade passe son contrôle et sort en SVG et en DXF", detail);
  }

  // ---------- 6.13 · Contour piloté par une esquisse : une cote changée redimensionne la carte, trous compris ----------
  {
    std::vector<std::string> ids;
    AtelierElectronique* a = ouvrir(f, conceptionAvec(80, 50, {{"Trou_fixation", 4, 4}, {"Trou_fixation", 76, 4}, {"Trou_fixation", 76, 46}, {"Trou_fixation", 4, 46}}, &ids));
    bool suit = false;
    std::string detail;
    if (a) {
      a->executerAction("carte.contourEsquisse");
      attendre(250);
      DialogueContourEsquisse* d = a->contourEsquisse();
      QDoubleSpinBox* cote = nullptr;
      if (d)
        for (int i = 0; i < d->table()->rowCount(); ++i)
          if (auto* b = qobject_cast<QDoubleSpinBox*>(d->table()->cellWidget(i, 1)); b && std::abs(b->value() - 80) < 1e-6) cote = b;
      const int pas = doc->positionHistorique();
      if (cote) {
        cote->setValue(90);
        attendre(250);
      }
      const ConceptionElec* c = conception(doc);
      const double largeur = mm(boite(c->circuit.contour.polygone()).largeur());
      double xDroite = 0;
      for (const auto& e : c->circuit.empreintes)
        if (e.composant == ids[1]) xDroite = mm(e.position.x);
      suit = cote && std::abs(largeur - 90) < 1e-3 && std::abs(xDroite - 86) < 1e-3 && doc->positionHistorique() == pas + 1;
      detail = std::to_string(largeur) + " mm de large, trou de droite en " + std::to_string(xDroite);
      if (d) d->close();
    }
    verifier(suit, "6.13 : le contour tracé dans l'esquisse pilote la carte : la cote de 80 mm passée à 90 la redimensionne, trous de fixation compris, en un pas", detail);
  }

  // ---------- 6.10 · Un dossier de modèles STEP s'associe d'un coup aux empreintes, chaque modèle calé ----------
  {
    std::vector<std::string> ids;
    AtelierElectronique* a = ouvrir(f, conceptionAvec(50, 30, {{"R", 10, 10}, {"Regulateur", 30, 15}}, &ids));
    bool associes = false;
    std::string detail;
    if (a) {
      const ConceptionElec* c0 = conception(doc);
      const std::string empR = c0->composant(ids[0])->empreinte;
      const QString d = dossierEssai(QStringLiteral("dossier"));
      // La résistance : un modèle tourné d'un quart de tour par rapport à son empreinte (allongé en y)
      const DefEmpreinte* de = c0->empreinte(empR);
      const double l = mm(de->encombrement.largeur()), p = mm(de->encombrement.hauteur());
      boiteStep(d, QString::fromStdString(empR) + QStringLiteral(".step"), std::min(l, p) * 0.8, std::max(l, p) * 0.8, 0.5);
      boiteStep(d, QStringLiteral("SOT-223.stp"), 6.5, 3.5, 1.8);
      const int pas = doc->positionHistorique();
      const int n = a->associerDossierModeles(d);
      attendre(250);
      const ConceptionElec* c = conception(doc);
      int exacts = 0, cales = 0;
      for (const auto& id : ids)
        if (const ComposantElec* k = c->composant(id); k && !k->modele3D.empty()) {
          exacts += hauteurComposant(*c, *k).provenance == ProvenanceHauteur::ModeleExact;
          const Modele3D& m = c->modeles3D.at(k->modele3D);
          cales += id == ids[0] ? m.quarts == (l > p ? 1 : 0) : m.quarts == 0;
        }
      associes = n == 2 && exacts == 2 && cales == 2 && doc->positionHistorique() == pas + 1;
      detail = std::to_string(n) + " associé(s), " + std::to_string(exacts) + " exact(s), " + std::to_string(cales) + " calé(s)";
    }
    verifier(associes, "6.10 : un dossier de modèles STEP s'associe d'un coup aux empreintes, chaque modèle calé sur la sienne, en un pas", detail);
  }
}
