// Prométhée : essais d'interface de la 0.34, lots L17 et L18 — schéma déduit du circuit de l'exemple au NE555 privé
// de son schéma, mêmes réseaux réseau par réseau, en un pas ; retouches notées par carte, portées au schéma et
// acceptées une à une par la révision suivante (17.6) ; bus fly-by de quatre récepteurs : topologie extraite
// superposée à la prévue, moignon de via de 3 mm signalé, règles posées d'un clic (18.4). Appelés seuls avec
// PROMETHEE_ESSAIS_LOTS=034L17 ou 034L18. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <string>

#include <QApplication>
#include <QEventLoop>
#include <QListWidget>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "Lignes034.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/electronique.hpp"
#include "promethee/modele.hpp"
#include "promethee/schema_deduit.hpp"
#include "promethee/simulation.hpp"
#include "promethee/topologie_lignes.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
using Broches = std::set<std::pair<std::string, std::string>>;
std::vector<std::pair<std::string, Broches>> reseauxMultiples(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  std::vector<std::pair<std::string, Broches>> r;
  for (const auto& n : k.reseaux) {
    Broches b;
    for (const auto& x : n.broches)
      if (const ComposantElec* comp = c.composant(x.composant)) b.insert({comp->reference, x.broche});
    if (b.size() >= 2) r.emplace_back(n.nom, b);
  }
  std::sort(r.begin(), r.end());
  return r;
}
ConceptionElec priveeDeSchema(const ConceptionElec& original) {
  ConceptionElec c = original;
  const Connectivite k = connectivite(original);
  const Remplissages r = remplirZones(original, k);
  for (auto& z : c.circuit.zones)
    if (const auto it = r.find(z.uuid); it != r.end()) {
      z.etat = EtatZone::Figee;
      z.remplissageFige = it->second;
    }
  for (auto& f : c.feuilles) {
    f.symboles.clear();
    f.fils.clear();
    f.etiquettes.clear();
    f.jonctions.clear();
    f.nonConnexions.clear();
    f.textes.clear();
  }
  c.nomsReseaux.clear();
  for (auto& x : c.composants) x.carteSeule = true;
  c.nom = "Carte sans schéma";
  return c;
}
Pt pastille(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  for (const auto& p : pastilles(c, connectivite(c)))
    if (p.composant == composant && p.numero == numero) return p.pastille.position;
  return {};
}
Piste piste(Pt a, Pt b, Couche k = Couche::CuivreDessus) {
  Piste p;
  p.uuid = nouvelUuid();
  p.reseau = "DQ0";
  p.couche = k;
  p.a = a;
  p.b = b;
  p.largeur = 150'000;
  return p;
}
// Bus fly-by : pilote et quatre récepteurs traversants ; via à mi-chemin vers In1 d'une carte de 3,27 mm
ConceptionElec carteFlyBy(std::vector<std::string>& refs) {
  ConceptionElec c = nouvelleConception("Fly-by");
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(110);
  c.circuit.contour.hauteur = nm(40);
  Empilage& e = c.circuit.empilage;
  e.couchesCuivre = 4;
  e.epaisseur = 3.27;
  auto couche = [](GenreCoucheEmpilage g, double ep) {
    CoucheEmpilage x;
    x.genre = g;
    x.epaisseur = ep;
    return x;
  };
  const auto C = GenreCoucheEmpilage::Cuivre, D = GenreCoucheEmpilage::Dielectrique;
  e.pile = {couche(C, 0.035), couche(D, 0.2), couche(C, 0.035), couche(D, 2.73), couche(C, 0.035), couche(D, 0.2), couche(C, 0.035)};
  std::vector<std::string> u;
  for (int i = 0; i < 5; ++i) u.push_back(poserEmpreinteSeule(c, "PinHeader_1x02_P2.54", ptMm(10 + 20 * i, 20)));
  for (const auto& x : u) c.nomsReseaux[x + ":1"] = "DQ0", c.nomsReseaux[x + ":2"] = "GND";
  const Pt p0 = pastille(c, u[0], "1");
  const Nm yl = p0.y - nm(2);
  std::vector<Pt> j;
  for (int i = 1; i < 5; ++i) j.push_back({pastille(c, u[static_cast<std::size_t>(i)], "1").x, yl});
  const Pt pv{(j[1].x + j[2].x) / 2, yl};
  c.circuit.pistes = {piste(p0, {p0.x, yl}), piste({p0.x, yl}, j[0]), piste(j[0], j[1]), piste(j[1], pv), piste(pv, j[2], Couche::CuivreInt1), piste(j[2], j[3], Couche::CuivreInt1)};
  for (int i = 1; i < 5; ++i) c.circuit.pistes.push_back(piste(j[static_cast<std::size_t>(i - 1)], pastille(c, u[static_cast<std::size_t>(i)], "1"), i >= 3 ? Couche::CuivreInt1 : Couche::CuivreDessus));
  Via v;
  v.uuid = nouvelUuid();
  v.reseau = "DQ0";
  v.p = pv;
  v.diametre = 600'000;
  v.percage = 300'000;
  c.circuit.vias.push_back(v);
  for (const auto& x : u) refs.push_back(c.composant(x)->reference + ".1");
  return c;
}
}  // namespace

void essaisElectronique034L17(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  const ConceptionElec original = exempleElectronique();
  const auto attendus = reseauxMultiples(original);
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = "Carte retrouvée";
  p.electronique = {priveeDeSchema(original)};
  doc->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Circuit);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) {
    verifier(false, "034 L17 : l'atelier électronique s'ouvre", {});
    return;
  }
  // ---------- 17.6 · Schéma déduit du circuit ----------
  {
    const int pas0 = doc->positionHistorique();
    a->executerAction("schema.depuisCircuit");
    attendre(300);
    const ConceptionElec& c = doc->projet().electronique[0];
    const auto deduits = reseauxMultiples(c);
    int justes = 0;
    std::string manquant;
    for (const auto& [nom, broches] : attendus) {
      const auto it = std::find_if(deduits.begin(), deduits.end(), [&b = broches](const auto& x) { return x.second == b; });
      if (it != deduits.end() && it->first == nom) ++justes;
      else if (manquant.empty()) manquant = nom;
    }
    verifier(justes == static_cast<int>(attendus.size()) && deduits.size() == attendus.size() && doc->positionHistorique() == pas0 + 1,
             "034 L17 · 17.6 : circuit de l'exemple au NE555 privé de son schéma : schéma déduit aux mêmes réseaux que l'original, réseau par réseau, en un pas",
             std::to_string(justes) + " réseau(x) sur " + std::to_string(attendus.size()) + (manquant.empty() ? std::string() : ", premier différent : " + manquant));
  }
  // ---------- 17.6 · Retouches de la révision fabriquée ----------
  {
    doc->modifierConception(0, [](ConceptionElec& c) { c.cartouche.revision = "A"; });
    a->executerAction("circuit.retouches");
    attendre(250);
    FenetreRetouches* r = a->retouches();
    Retouche valeur;
    valeur.genre = GenreRetouche::ComposantChange;
    valeur.composant = "R3";
    valeur.valeur = "470";
    valeur.numerosSerie = {"SN002"};
    Retouche fil;
    fil.genre = GenreRetouche::FilDeReprise;
    fil.a = "C2.1";
    fil.b = "R3.2";
    // La révision suivante part de la révision fabriquée, avant les retouches
    doc->modifier([](prom::Projet& p) {
      ConceptionElec b = p.electronique[0];
      b.uuid = nouvelUuid();
      b.nom = "Carte révision B";
      b.cartouche.revision = "B";
      p.electronique.push_back(b);
    });
    const bool notees = r->ajouter(valeur) && r->ajouter(fil);
    const QString liste = r->liste(QStringLiteral("SN002")), listeSn1 = r->liste(QStringLiteral("SN001"));
    const bool parCarte = liste.count(QLatin1Char('\n')) == 3 && listeSn1.count(QLatin1Char('\n')) == 2;
    const int portees = r->porterAuSchema();
    const ConceptionElec& c = doc->projet().electronique[0];
    const Connectivite k = connectivite(c);
    const bool auSchema = portees == 2 && c.parReference("R3")->valeur == "470" && k.reseau(c.parReference("C2")->uuid, "1") == k.reseau(c.parReference("R3")->uuid, "2");
    f.montrerElectronique(1, AtelierElectronique::Circuit);
    attendre(300);
    r = a->retouches();
    const std::size_t avant = r->proposees(0).size();
    const bool accepte = r->accepter(0, 0);
    const std::size_t apres = r->proposees(0).size();
    const ConceptionElec& b = doc->projet().electronique[1];
    verifier(notees && parCarte && auSchema && avant == 2 && accepte && apres == 1 && b.parReference("R3")->valeur == "470",
             "034 L17 · 17.6 : retouches notées par numéro de série, portées au schéma ; la révision suivante les reçoit en modifications proposées, acceptées une à une",
             liste.toStdString());
    r->close();
  }
}

void essaisElectronique034L18(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  std::vector<std::string> refs;
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = "Bus fly-by";
  p.electronique = {carteFlyBy(refs)};
  doc->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Circuit);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) {
    verifier(false, "034 L18 : l'atelier électronique s'ouvre", {});
    return;
  }
  a->executerAction("circuit.schemaLignes");
  attendre(250);
  FenetreLignes* l = a->lignes();
  SchemaLignes prevue;
  prevue.reseau = "DQ0";
  prevue.topologie = TopologieSignal::FlyBy;
  prevue.ordre = refs;
  prevue.moignonMax = 3.0;
  prevue.moignonViaMax = 0.5;
  for (int i = 1; i < 5; ++i) {
    ElementLigne t;
    t.genre = GenreElementLigne::Troncon;
    t.id = "T" + std::to_string(i);
    t.a = refs[static_cast<std::size_t>(i - 1)];
    t.b = refs[static_cast<std::size_t>(i)];
    t.longueur = 20;
    prevue.elements.push_back(t);
  }
  l->setPrevue(prevue);
  const int pas0 = doc->positionHistorique();
  const bool enregistre = l->enregistrer() && doc->positionHistorique() == pas0 + 1 && schemasLignes(doc->projet().electronique[0]).size() == 1;
  const int n = l->extraireEtComparer();
  auto* liste = l->findChild<QListWidget*>(QStringLiteral("ecartsTopologie"));
  const bool signale = n == 1 && l->ecarts()[0].genre == "moignon de via" && liste && liste->count() == 1 && liste->item(0)->text().contains(QStringLiteral("3,0 mm"));
  const bool memeOrdre = l->extraite().ordre == refs;
  const ConceptionElec& c = doc->projet().electronique[0];
  const Connectivite k = connectivite(c);
  const auto drc = verifierDRC(c, k, remplirZones(c, k));
  const bool aLaDrc = std::count_if(drc.begin(), drc.end(), [](const Violation& v) { return v.code == "DRC-91"; }) == 1;
  verifier(enregistre && signale && memeOrdre && aLaDrc, "034 L18 · 18.4 : bus fly-by de quatre récepteurs : topologie extraite superposée à la prévue, moignon de via de 3 mm signalé au-delà de sa limite",
           liste && liste->count() ? liste->item(0)->text().toStdString() : std::string("aucun écart"));
  const bool regles = l->versRegles() && !doc->projet().electronique[0].circuit.regles.avancees.signaux.empty();
  const double v = l->simuler(3.0);
  verifier(regles && (!ngspiceDisponible() || v > 0.5), "034 L18 · 18.4 : valeurs du schéma de lignes versées aux règles d'un clic ; schéma de lignes simulé par ngspice, moignon balayé",
           std::to_string(v) + " V au dernier récepteur");
  l->close();
}
