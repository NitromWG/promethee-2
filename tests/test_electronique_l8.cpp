// Prométhée : essais du lot L8 (fabrication et cartes complexes). Licence GPL-3.0-only.
#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electronique.hpp"
#include "promethee/fabrication_avancee.hpp"
#include "promethee/synchronisation.hpp"

using namespace prom::ecad;

namespace {
int erreurs(const std::vector<Violation>& v) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [](const Violation& x) { return x.gravite == Gravite::Erreur; }));
}
std::map<Couche, std::string> cuivresEcrits(const ConceptionElec& c, std::string& percages) {
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  std::map<Couche, std::string> r;
  for (const auto& cf : m.couches)
    if (estCuivre(cf.couche)) r[cf.couche] = gerber(m, cf.couche, "essai");
  percages = excellon(m, true, "essai");
  return r;
}
// Deux résistances CMS reliées par une seule piste (R1.2 — R2.1)
ConceptionElec deuxResistancesRoutees() {
  ConceptionElec c = nouvelleConception("Banc L8");
  const std::string r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8), 1), r2 = poserSymbole(c, 0, "R", ptMm(101.6, 50.8), 1);
  auto broche = [&](const std::string& sym, const std::string& numero) {
    for (const auto& s : c.feuilles[0].symboles)
      if (s.uuid == sym)
        for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
          if (b.broche->numero == numero) return b.p;
    return Pt{};
  };
  const Pt p1 = broche(r1, "2"), p2 = broche(r2, "1");
  const Nm bas = std::max(p1.y, p2.y) + nm(10.16);
  c.feuilles[0].fils.push_back({nouvelUuid(), p1, {p1.x, bas}});
  c.feuilles[0].fils.push_back({nouvelUuid(), {p1.x, bas}, {p2.x, bas}});
  c.feuilles[0].fils.push_back({nouvelUuid(), {p2.x, bas}, p2});
  annoter(c, false);
  mettreAJourCircuit(c);
  c.circuit.contour.largeur = nm(50);
  c.circuit.contour.hauteur = nm(30);
  for (auto& e : c.circuit.empreintes) e.position = c.composant(e.composant)->reference == "R1" ? ptMm(10, 15) : ptMm(40, 15);
  const Connectivite k = connectivite(c);
  Pt a, b;
  std::string reseau;
  for (const auto& p : pastilles(c, k)) {
    if (p.reference == "R1" && p.numero == "2") a = p.pastille.position, reseau = p.reseau;
    if (p.reference == "R2" && p.numero == "1") b = p.pastille.position;
  }
  c.circuit.pistes.push_back({nouvelUuid(), reseau, Couche::CuivreDessus, a, b, 250'000});
  return c;
}
}  // namespace

TEST_CASE("8.8 · Panneau 3 × 2 en languettes perforées : contrôlé sans défaut, Gerber et perçages relus", "[L8][fabrication]") {
  const ConceptionElec c = exempleElectronique();
  const PanneauFabrication p = panneauFabrication(c, OptionsPanneau{});
  INFO((p.defauts.empty() ? std::string() : p.defauts.front()));
  CHECK(p.defauts.empty());
  CHECK(p.languettes == 3 * 2 * 2 * 2);
  CHECK(p.perforations >= p.languettes * 3);
  const Connectivite k = connectivite(c);
  const ModeleFabrication base = modeleFabrication(c, k, remplirZones(c, k));
  // Six copies de chaque pastille du dessus, relues dans le Gerber du panneau
  auto flashs = [](const std::string& g) {
    const LectureGerber l = lireGerber(g);
    return std::count_if(l.objets.begin(), l.objets.end(), [](const ObjetGerber& o) { return o.genre != ObjetGerber::Genre::Trait && !o.reference.empty(); });
  };
  CHECK(flashs(gerber(p.modele, Couche::CuivreDessus, "essai")) == 6 * flashs(gerber(base, Couche::CuivreDessus, "essai")));
  // Perçages : six fois ceux de la carte, plus les perforations, tous dans le cadre
  const auto pth = lirePercages(excellon(p.modele, true, "essai")), npth = lirePercages(excellon(p.modele, false, "essai"));
  const auto pthBase = lirePercages(excellon(base, true, "essai")), npthBase = lirePercages(excellon(base, false, "essai"));
  CHECK(pth.size() == 6 * pthBase.size());
  CHECK(npth.size() == 6 * npthBase.size() + static_cast<std::size_t>(p.perforations));
  for (const auto& [q, d] : npth) {
    CHECK(q.x >= 0);
    CHECK(q.x <= p.modele.boite.largeur());
    CHECK(q.y >= 0);
    CHECK(q.y <= p.modele.boite.hauteur());
  }
  // Le contour fraisé est coupé aux languettes : plus de traits que de côtés
  const std::string contour = gerber(p.modele, Couche::Contour, "essai");
  CHECK(lireGerber(contour).objets.size() > 4 + 6 * base.contour.size());
}

TEST_CASE("8.9 · Routeur complet : l'exemple au NE555 routé en entier, sans violation, en moins de 10 s", "[L8][routage]") {
  ConceptionElec c = exempleElectronique();
  c.circuit.pistes.clear();
  c.circuit.vias.clear();
  const auto t0 = std::chrono::steady_clock::now();
  const BilanRoutage b = routerComplet(c);
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  INFO("restantes : " << b.restantes << (b.nonRoutees.empty() ? std::string() : " — " + b.nonRoutees.front().reseau + " : " + b.nonRoutees.front().raison));
  CHECK(b.complet());
  CHECK(s < 10.0);
  const Connectivite k = connectivite(c);
  const Remplissages z = remplirZones(c, k);
  CHECK(etatRoutage(c, k, z).chevelu.empty());
  const auto v = verifierDRCReglee(c, k, z);
  for (const auto& x : v)
    if (x.gravite == Gravite::Erreur) UNSCOPED_INFO(x.code << " " << x.message);
  CHECK(erreurs(v) == 0);
}

TEST_CASE("8.3 · Paire différentielle de 90 Ω : largeur et écart proposés pour l'empilage", "[L8][impedance]") {
  for (const int couches : {2, 4}) {
    Empilage e;
    e.couchesCuivre = couches;
    const ProposPaire p = proposerPaire(90, e);
    INFO(couches << " couches : " << p.largeur << " / " << p.ecart << " mm");
    CHECK(p.largeur > 0.05);
    CHECK(p.ecart >= 0.15);
    CHECK(p.zdiff == Catch::Approx(90).margin(1.0));
    CHECK(p.z0 > p.zdiff / 2);
  }
  // Microruban de référence : 0,3 mm sur 0,2 mm de FR-4, autour de 50 Ω
  CHECK(impedanceMicroruban(0.3, Microruban{0.2, 0.035, 4.5}) == Catch::Approx(50).margin(5));
}

TEST_CASE("8.19 · Connectivité relue dans les fichiers écrits ; segment retiré à la main : coupure signalée avec ses deux pastilles", "[L8][relecture]") {
  {
    const ConceptionElec c = exempleElectronique();
    std::string pth;
    const auto g = cuivresEcrits(c, pth);
    CHECK(comparerFabrication(c, g, pth).empty());
    // Les groupes relus égalent la netlist sur les réseaux routés
    const Connectivite k = connectivite(c);
    std::map<std::string, std::string> reseauDe;
    for (const auto& p : pastilles(c, k)) reseauDe[p.reference + "." + p.numero] = p.reseau;
    const auto groupes = groupesConception(c);
    std::map<std::string, std::set<std::size_t>> parReseau;
    for (std::size_t i = 0; i < groupes.size(); ++i) {
      std::set<std::string> n;
      for (const auto& x : groupes[i]) n.insert(reseauDe[x]);
      CHECK(n.size() == 1);
      for (const auto& x : n) parReseau[x].insert(i);
    }
    const auto chevelu = etatRoutage(c, k, remplirZones(c, k)).chevelu;
    CHECK(chevelu.empty());
    for (const auto& [n, g2] : parReseau)
      if (!n.empty()) CHECK(g2.size() == 1);
  }
  ConceptionElec c = deuxResistancesRoutees();
  std::string pth;
  auto g = cuivresEcrits(c, pth);
  REQUIRE(comparerFabrication(c, g, pth).empty());
  // Le trait de la piste : la ligne « …D01* » qui suit un « …D02* » hors région
  std::string& f = g[Couche::CuivreDessus];
  std::istringstream in(f);
  std::string ligne, sortie;
  bool retire = false, region = false, apresD02 = false;
  while (std::getline(in, ligne)) {
    if (ligne.rfind("G36", 0) == 0) region = true;
    if (ligne.rfind("G37", 0) == 0) region = false;
    const bool d01 = ligne.size() > 4 && ligne.compare(ligne.size() - 4, 4, "D01*") == 0;
    if (!retire && !region && apresD02 && d01) {
      retire = true;
      apresD02 = false;
      continue;
    }
    apresD02 = ligne.size() > 4 && ligne.compare(ligne.size() - 4, 4, "D02*") == 0;
    sortie += ligne + "\n";
  }
  REQUIRE(retire);
  f = sortie;
  const auto d = comparerFabrication(c, g, pth);
  REQUIRE(d.size() == 1);
  CHECK(d.front().find("coupure") != std::string::npos);
  CHECK(d.front().find("R1.2") != std::string::npos);
  CHECK(d.front().find("R2.1") != std::string::npos);
}

TEST_CASE("8.18 · Devis de dix cartes : JLCPCB et PCBWay côte à côte, sans réseau, grilles datées", "[L8][devis]") {
  const ConceptionElec c = exempleElectronique();
  const auto o = devis(c, 10);
  REQUIRE(o.size() == 2);
  CHECK(o[0].fabricant == "JLCPCB");
  CHECK(o[1].fabricant == "PCBWay");
  for (const auto& x : o) {
    CHECK(x.quantite == 10);
    CHECK(x.prix > 0);
    CHECK(x.delaiJours > 0);
    CHECK(x.date.size() == 10);
  }
  // Plus de cartes : plus cher
  CHECK(devis(c, 100)[0].prix > o[0].prix);
}

TEST_CASE("8.31 · Visionneuse HTML autonome : schéma, circuit, 3D et nomenclature, sans réseau", "[L8][visionneuse]") {
  const ConceptionElec c = exempleElectronique();
  const std::string h = visionneuseHtml(c);
  CHECK(h.rfind("<!doctype html>", 0) == 0);
  CHECK(h.find("<script src") == std::string::npos);
  CHECK(h.find("<link") == std::string::npos);
  CHECK(h.find("id=\"schema\"") != std::string::npos);
  CHECK(h.find("id=\"circuit\"") != std::string::npos);
  CHECK(h.find("id=\"toile\"") != std::string::npos);
  CHECK(h.find("function allumer") != std::string::npos);
  // Chaque repère est cliquable au schéma, au circuit et dans la nomenclature, et a son corps en 3D
  for (const auto& x : c.composants) {
    if (x.reference.empty() || x.reference[0] == '#') continue;
    const std::string cle = "data-ref=\"" + x.reference + "\"";
    std::size_t n = 0;
    for (std::size_t i = h.find(cle); i != std::string::npos; i = h.find(cle, i + 1)) ++n;
    INFO(x.reference);
    CHECK(n >= 2);
  }
  CHECK(h.find("{r:\"U1\"") != std::string::npos);
}
