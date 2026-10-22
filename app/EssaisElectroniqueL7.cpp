// Prométhée : essais d'interface du lot L7 de l'audit (« Schéma avancé ») : feuille de canal répétée quatre fois,
// quatre jeux de repères et d'empreintes, placement du premier canal reproduit sur les trois autres ; trois composants
// routés déplacés vers une autre feuille ; congé de 2 mm et contour décalé par le menu contextuel du circuit.
// Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L7.
// Licence GPL-3.0-only.
#include <algorithm>
#include <functional>
#include <set>
#include <string>

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QEventLoop>
#include <QInputDialog>
#include <QMenu>
#include <QSpinBox>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"
#include "promethee/dessins_circuit.hpp"
#include "promethee/modele.hpp"
#include "promethee/synchronisation.hpp"

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
std::string composantDe(const ConceptionElec& c, const std::string& sym) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == sym) return s.composant;
  return {};
}
AtelierElectronique* ouvrir(FenetrePrincipale& f, ConceptionElec c, int mode) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = c.nom;
  p.electronique.push_back(std::move(c));
  f.document()->creer(p);
  f.montrerElectronique(0, mode);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return nullptr;
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);
  a->recalculer();
  return a;
}
// Répond au prochain QInputDialog (entier ou liste)
void repondreSaisie(std::function<void(QInputDialog*)> regler) {
  QTimer::singleShot(300, [regler] {
    if (auto* d = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
      regler(d);
      d->accept();
    }
  });
}
QAction* actionDuMenu(const QString& menu, const QString& action) {
  for (QWidget* w : QApplication::topLevelWidgets())
    if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible() && m->objectName() == menu)
      for (QAction* x : m->actions())
        if (x->objectName() == action) return x;
  return nullptr;
}
}  // namespace

void essaisElectroniqueL7(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();

  // ---------- 7.5 · Feuille de canal ×4 : quatre jeux de repères et d'empreintes ; le placement du canal 1 se reproduit ----------
  {
    ConceptionElec c0 = nouvelleConception("Canaux");
    const std::string r = poserSymbole(c0, 0, "R", ptMm(50.8, 50.8), 1), cc = poserSymbole(c0, 0, "C", ptMm(88.9, 63.5));
    c0.composant(composantDe(c0, r))->reference = "R5";
    c0.composant(composantDe(c0, cc))->reference = "C5";
    const Pt a = broche(c0, r, "2"), z = broche(c0, cc, "1");
    c0.feuilles[0].fils.push_back({nouvelUuid(), a, {z.x, a.y}});
    c0.feuilles[0].fils.push_back({nouvelUuid(), {z.x, a.y}, z});
    AtelierElectronique* at = ouvrir(f, c0, AtelierElectronique::Schema);
    if (!at) {
      verifier(false, "L7 : la conception d'essai s'ouvre", {});
      return;
    }
    verifier(reel::activer(&f), "L7 : la fenêtre est active", {});
    repondreSaisie([](QInputDialog* d) { d->setIntValue(4); });
    at->executerAction("schema.repeterFeuille");
    attendre(300);
    at->mettreAJourCircuit();
    attendre(300);
    const ConceptionElec* c = conception(doc);
    std::set<std::string> refs;
    for (const auto& k : c->composants) refs.insert(k.reference);
    const bool quatre = c->feuilles.size() == 4 && c->circuit.empreintes.size() == 8 && refs == std::set<std::string>{"R5", "R205", "R305", "R405", "C5", "C205", "C305", "C405"};
    // Le canal 1 placé et routé à la main, puis reproduit
    at->modifierConception([](ConceptionElec& cx) {
      cx.circuit.contour.largeur = nm(100);
      cx.circuit.contour.hauteur = nm(40);
      for (auto& e : cx.circuit.empreintes) {
        if (e.composant == cx.parReference("R5")->uuid) e.position = ptMm(8, 10);
        if (e.composant == cx.parReference("C5")->uuid) e.position = ptMm(14, 10);
      }
    }, QStringLiteral("Canal 1 placé"));
    at->recalculer();
    attendre(200);
    VueCircuit* vc = at->vueCircuit();
    at->setMode(AtelierElectronique::Circuit);
    attendre(150);
    c = conception(doc);
    const std::string reseau1 = connectivite(*c).nom(c->parReference("R5")->uuid, "2");
    Pt pa, pb;
    for (const auto& p : pastilles(*c, connectivite(*c)))
      if (p.reseau == reseau1) (pa == Pt{} ? pa : pb) = p.pastille.position;
    vc->router({pa, pb});
    attendre(200);
    at->executerAction("circuit.reproduireCanal");
    attendre(300);
    c = conception(doc);
    const Connectivite k = connectivite(*c);
    const auto chevelu = etatRoutage(*c, k, remplirZones(*c, k)).chevelu;
    int reproduits = 0;
    for (const char* ref : {"R205", "R305", "R405"}) {
      const std::string n = k.nom(c->parReference(ref)->uuid, "2");
      reproduits += std::none_of(chevelu.begin(), chevelu.end(), [&](const Liaison& l) { return l.reseau == n; });
    }
    verifier(quatre && reproduits == 3, "7.5 : une feuille de canal utilisée quatre fois donne quatre jeux de repères et d'empreintes ; le placement et le routage du canal 1 se reproduisent sur les trois autres",
             std::to_string(c->feuilles.size()) + " feuilles, " + std::to_string(reproduits) + " canaux routés");
  }

  // ---------- 7.12 · Trois composants routés déplacés vers une autre feuille : repères, empreintes, pistes gardés ----------
  {
    ConceptionElec c0 = nouvelleConception("Déplacement");
    std::vector<std::string> s;
    for (int i = 0; i < 4; ++i) s.push_back(poserSymbole(c0, 0, "R", ptMm(50.8 + 25.4 * i, 50.8), 1));
    for (int i = 0; i < 3; ++i) c0.feuilles[0].fils.push_back({nouvelUuid(), broche(c0, s[static_cast<std::size_t>(i)], "2"), broche(c0, s[static_cast<std::size_t>(i + 1)], "1")});
    Feuille f2;
    f2.uuid = nouvelUuid();
    f2.nom = "Puissance";
    c0.feuilles.push_back(f2);
    annoter(c0, false);
    mettreAJourCircuit(c0);
    c0.circuit.contour.largeur = nm(60);
    c0.circuit.contour.hauteur = nm(30);
    for (std::size_t i = 0; i < c0.circuit.empreintes.size(); ++i) c0.circuit.empreintes[i].position = ptMm(8 + 12 * static_cast<double>(i), 15);
    {
      const Connectivite k = connectivite(c0);
      for (const auto& l : etatRoutage(c0, k, remplirZones(c0, k)).chevelu) c0.circuit.pistes.push_back({nouvelUuid(), l.reseau, Couche::CuivreDessus, l.a, l.b, nm(0.25)});
    }
    const auto empreintes = c0.circuit.empreintes;
    const auto nbPistes = c0.circuit.pistes.size();
    AtelierElectronique* at = ouvrir(f, c0, AtelierElectronique::Schema);
    bool garde = false;
    std::string detail;
    if (at) {
      VueSchema* vs = at->vueSchema();
      vs->selectionner({s[0], s[1], s[2]});
      attendre(100);
      repondreSaisie([](QInputDialog* d) { d->setTextValue(QStringLiteral("Puissance")); });
      at->executerAction("schema.deplacerVersFeuille");
      attendre(400);
      const ConceptionElec* c = conception(doc);
      const Connectivite k = connectivite(*c);
      const bool chevelu = etatRoutage(*c, k, remplirZones(*c, k)).chevelu.empty();
      int etiquettes = 0;
      for (const auto& fe : c->feuilles) etiquettes += static_cast<int>(fe.etiquettes.size());
      garde = c->feuilles[1].symboles.size() == 3 && c->circuit.empreintes == empreintes && c->circuit.pistes.size() == nbPistes && chevelu && etiquettes == 2 && c->parReference("R3");
      detail = std::to_string(c->feuilles[1].symboles.size()) + " symbole(s) déplacé(s), " + std::to_string(etiquettes) + " étiquette(s)" + (chevelu ? "" : ", liaisons ouvertes");
    }
    verifier(garde, "7.12 : trois composants routés déplacés vers une autre feuille gardent repères, empreintes et pistes ; la liaison coupée devient des étiquettes", detail);
  }

  // ---------- 7.22, 7.23 · Congé de 2 mm au coin de deux traits, contour décalé de 0,25 mm, par le menu contextuel ----------
  {
    ConceptionElec c0 = nouvelleConception("Dessins");
    const std::string r = poserSymbole(c0, 0, "R", ptMm(50.8, 50.8));
    annoter(c0, false);
    mettreAJourCircuit(c0);
    c0.composant(composantDe(c0, r))->empreinte = "R_0402";
    mettreAJourCircuit(c0);
    c0.circuit.contour.largeur = nm(40);
    c0.circuit.contour.hauteur = nm(30);
    for (auto& e : c0.circuit.empreintes) e.position = ptMm(30, 20);
    c0.circuit.dessins.push_back({DessinCircuit::Forme::Trait, "a", Couche::SerigraphieDessus, {ptMm(5, 5), ptMm(20, 5)}, nm(0.15), false});
    c0.circuit.dessins.push_back({DessinCircuit::Forme::Trait, "b", Couche::SerigraphieDessus, {ptMm(20, 5), ptMm(20, 15)}, nm(0.15), false});
    AtelierElectronique* at = ouvrir(f, c0, AtelierElectronique::Circuit);
    bool conge = false, contour = false;
    std::string detail;
    if (at) {
      VueCircuit* vc = at->vueCircuit();
      vc->recadrer();
      vc->setOutil(VueCircuit::Outil::Selection);
      attendre(150);
      reel::cliquer(vc, vc->versEcran(ptMm(20, 5)).toPoint(), Qt::RightButton);
      attendre(250);
      if (QAction* x = actionDuMenu(QStringLiteral("menuCoin"), QStringLiteral("conge:2"))) {
        x->trigger();
        if (auto* m = qobject_cast<QMenu*>(x->parent())) m->close();
      }
      attendre(200);
      const ConceptionElec* c = conception(doc);
      const auto& d = c->circuit.dessins;
      conge = d.size() == 3 && d[0].points[1] == ptMm(18, 5) && d[1].points[0] == ptMm(20, 7) && d[2].forme == DessinCircuit::Forme::Arc;
      // Contour décalé autour des pastilles de R1 (0402 : 0,36 mm d'écart) : un seul polygone
      reel::cliquer(vc, vc->versEcran(ptMm(30, 20)).toPoint(), Qt::RightButton);
      attendre(250);
      if (QAction* x = actionDuMenu(QStringLiteral("menuEmpreinte"), QStringLiteral("contourPastilles"))) {
        x->trigger();
        if (auto* m = qobject_cast<QMenu*>(x->parent())) m->close();
      }
      attendre(200);
      c = conception(doc);
      int polygones = 0;
      for (const auto& x : c->circuit.dessins) polygones += x.forme == DessinCircuit::Forme::Polygone;
      contour = polygones == 1;
      detail = std::to_string(c->circuit.dessins.size()) + " dessin(s), " + std::to_string(polygones) + " polygone(s)";
    }
    verifier(conge && contour, "7.22, 7.23 : au circuit, un congé de 2 mm se pose au coin de deux traits par le menu contextuel ; le contour décalé de 0,25 mm autour des pastilles réunies est un seul polygone",
             detail);
  }
}
