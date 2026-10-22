// Prométhée : essais du lot L5 (circuit imprimé : placer, router, vérifier). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electronique.hpp"
#include "promethee/placement_circuit.hpp"
#include "promethee/routeur.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/verification.hpp"

using namespace prom::ecad;

namespace {
struct Banc5 {
  ConceptionElec c = nouvelleConception("Banc L5");
  std::string poser(const std::string& id, double x, double y, int rot = 0) { return poserSymbole(c, 0, id, ptMm(x, y), rot); }
  Pt broche(const std::string& sym, const std::string& numero) const {
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles)
        if (s.uuid == sym)
          for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
            if (b.broche->numero == numero) return b.p;
    return {};
  }
  void fil(Pt a, Pt b) {
    if (a.x != b.x && a.y != b.y) {
      c.feuilles[0].fils.push_back({nouvelUuid(), a, {b.x, a.y}});
      c.feuilles[0].fils.push_back({nouvelUuid(), {b.x, a.y}, b});
    } else
      c.feuilles[0].fils.push_back({nouvelUuid(), a, b});
  }
  EmpreintePlacee* empreinte(const std::string& ref) {
    for (auto& e : c.circuit.empreintes)
      if (c.composant(e.composant)->reference == ref) return &e;
    return nullptr;
  }
};
// Deux résistances reliées (R1.2 — R2.1), sur une carte de 50 × 30
Banc5 deuxResistances() {
  Banc5 b;
  const std::string r1 = b.poser("R", 50.8, 50.8, 1), r2 = b.poser("R", 101.6, 50.8, 1);
  // En U par-dessous : le fil ne touche aucune autre broche
  const Pt p1 = b.broche(r1, "2"), p2 = b.broche(r2, "1");
  const Nm bas = std::max(p1.y, p2.y) + nm(10.16);
  b.fil(p1, {p1.x, bas});
  b.fil({p1.x, bas}, {p2.x, bas});
  b.fil({p2.x, bas}, p2);
  annoter(b.c, false);
  mettreAJourCircuit(b.c);
  b.c.circuit.contour.largeur = nm(50);
  b.c.circuit.contour.hauteur = nm(30);
  b.empreinte("R1")->position = ptMm(8, 15);
  b.empreinte("R2")->position = ptMm(42, 15);
  return b;
}
Liaison liaisonDe(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  const auto ch = etatRoutage(c, k, remplirZones(c, k)).chevelu;
  for (const auto& l : ch) UNSCOPED_INFO("liaison " << l.reseau << " " << mm(l.a.x) << "," << mm(l.a.y) << " → " << mm(l.b.x) << "," << mm(l.b.y));
  REQUIRE(ch.size() == 1);
  return ch.front();
}
bool aQuaranteCinq(Pt a, Pt b) {
  const Nm dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
  return dx == 0 || dy == 0 || dx == dy;
}
// Chaque piste de ces réseaux, sur sa couche, à l'isolement de tout le reste
bool toutesLibres(const ConceptionElec& c, const std::set<std::string>& reseaux) {
  for (const auto& p : c.circuit.pistes) {
    if (!reseaux.count(p.reseau)) continue;
    OptionsTrace o;
    o.reseau = p.reseau;
    o.couche = p.couche;
    o.largeur = p.largeur;
    std::string qui;
    if (!segmentLibre(c, p.a, p.b, o, &qui)) {
      UNSCOPED_INFO(p.reseau << " " << mm(p.a.x) << "," << mm(p.a.y) << " → " << mm(p.b.x) << "," << mm(p.b.y) << " touche " << qui);
      return false;
    }
  }
  return true;
}
}  // namespace

TEST_CASE("5.7 : une piste vers une pastille cachée derrière une autre piste la contourne, à l'isolement, à 45°", "[L5][ROU-01][C2]") {
  Banc5 b = deuxResistances();
  // Un mur de cuivre d'un autre réseau entre les deux résistances, ouvert en bas
  b.c.circuit.pistes.push_back({"mur", "AUTRE", Couche::CuivreDessus, ptMm(25, 1.5), ptMm(25, 23), nm(0.3)});
  const Liaison l = liaisonDe(b.c);
  OptionsTrace o;
  o.reseau = l.reseau;
  o.couche = Couche::CuivreDessus;
  // Le tracé direct touche le mur
  CHECK_FALSE(segmentLibre(b.c, l.a, l.b, o));
  const ResultatTrace r = contourner(b.c, l.a, l.b, o);
  INFO(r.raison);
  REQUIRE(r.ok);
  CHECK(r.points.front() == l.a);
  CHECK(r.points.back() == l.b);
  for (std::size_t i = 1; i < r.points.size(); ++i) CHECK(aQuaranteCinq(r.points[i - 1], r.points[i]));
  // Il passe sous le mur, sans détour inutile
  Nm basMax = 0;
  for (const Pt p : r.points) basMax = std::max(basMax, p.y);
  CHECK(basMax > nm(23));
  CHECK(r.longueur() < 60e6);
  // Posé par « Router cette liaison » : la DRC n'y trouve rien de nouveau
  CHECK(routerLiaison(b.c, l).empty());
  CHECK(toutesLibres(b.c, {l.reseau}));
  const Connectivite k = connectivite(b.c);
  CHECK(etatRoutage(b.c, k, remplirZones(b.c, k)).chevelu.empty());
}

TEST_CASE("5.7 : en mode Pousser, la piste existante s'écarte pour laisser passer la nouvelle, sans violer de règle", "[L5][ROU-02]") {
  Banc5 b = deuxResistances();
  // Une piste en U d'un autre réseau, dont le fond barre le tracé droit
  for (const auto& [a, z] : std::vector<std::pair<Pt, Pt>>{{ptMm(20, 19), ptMm(20, 15.2)}, {ptMm(20, 15.2), ptMm(30, 15.2)}, {ptMm(30, 15.2), ptMm(30, 19)}})
    b.c.circuit.pistes.push_back({nouvelUuid(), "X", Couche::CuivreDessus, a, z, nm(0.25)});
  const Liaison l = liaisonDe(b.c);
  OptionsTrace o;
  o.reseau = l.reseau;
  o.couche = Couche::CuivreDessus;
  const ResultatPoussee r = pousser(b.c, l.a, l.b, o);
  INFO(r.raison);
  REQUIRE(r.ok);
  CHECK(r.deplacees == std::vector<std::string>{"X"});
  CHECK(r.points.size() == 2);  // tout droit
  CHECK(toutesLibres(b.c, {l.reseau, "X"}));
  // Le réseau X relie toujours ses deux bouts
  std::map<Pt, int> degre;
  for (const auto& p : b.c.circuit.pistes)
    if (p.reseau == "X") {
      ++degre[p.a];
      ++degre[p.b];
    }
  std::vector<Pt> bouts;
  for (const auto& [p, d] : degre)
    if (d == 1) bouts.push_back(p);
  CHECK(bouts == std::vector<Pt>{ptMm(20, 19), ptMm(30, 19)});
  // Un obstacle qui ne se pousse pas (une via) : rien ne change
  Banc5 v = deuxResistances();
  v.c.circuit.vias.push_back({nouvelUuid(), "Y", ptMm(25, 15), nm(0.8), nm(0.4)});
  const auto avant = v.c.circuit.pistes.size();
  const ResultatPoussee rv = pousser(v.c, l.a, l.b, o);
  CHECK_FALSE(rv.ok);
  CHECK_FALSE(rv.raison.empty());
  CHECK(v.c.circuit.pistes.size() == avant);
}

TEST_CASE("5.21 : une liaison qui ne passe pas reste au chevelu, avec sa raison", "[L5][ROU-11]") {
  Banc5 b = deuxResistances();
  // Un anneau fermé d'un autre réseau autour de R2
  const std::vector<Pt> anneau{ptMm(36, 9), ptMm(48, 9), ptMm(48, 21), ptMm(36, 21), ptMm(36, 9)};
  for (std::size_t i = 1; i < anneau.size(); ++i) b.c.circuit.pistes.push_back({nouvelUuid(), "AUTRE", Couche::CuivreDessus, anneau[i - 1], anneau[i], nm(0.3)});
  b.c.circuit.empilage.couchesCuivre = 1;
  const Liaison l = liaisonDe(b.c);
  const std::string raison = routerLiaison(b.c, l);
  CHECK_FALSE(raison.empty());
  CHECK(raison.find("isolement") != std::string::npos);
}

TEST_CASE("5.13 : une violation exclue ne revient pas tant que rien ne bouge, et revient « à revoir » si la piste bouge", "[L5][DRC-09][R8]") {
  Banc5 b = deuxResistances();
  const Liaison l = liaisonDe(b.c);
  REQUIRE(routerLiaison(b.c, l).empty());
  // Une piste d'un autre réseau trop près
  b.c.circuit.pistes.push_back({"proche", "AUTRE", Couche::CuivreDessus, ptMm(15, 15.35), ptMm(35, 15.35), nm(0.25)});
  Connectivite k = connectivite(b.c);
  auto drc = verifierDRCReglee(b.c, k, remplirZones(b.c, k));
  const auto it = std::find_if(drc.begin(), drc.end(), [](const Violation& v) { return std::find(v.objets.begin(), v.objets.end(), "proche") != v.objets.end(); });
  REQUIRE(it != drc.end());
  const Violation v = *it;
  b.c.erc.exclusions.push_back(exclusionPour(b.c, v, "écart voulu, vérifié avec le fabricant", "Evan"));
  k = connectivite(b.c);
  auto encore = verifierDRCReglee(b.c, k, remplirZones(b.c, k));
  CHECK(std::none_of(encore.begin(), encore.end(), [&](const Violation& x) { return x.code == v.code && x.objets == v.objets; }));
  BilanVerification bilan = bilanDRC(b.c, k, remplirZones(b.c, k));
  CHECK(bilan.exclues.size() == 1);
  // Le fichier garde la signature
  const ConceptionElec relue = conceptionDepuisJson(versJson(b.c));
  REQUIRE(relue.erc.exclusions.size() == 1);
  CHECK(relue.erc.exclusions.front().signature == b.c.erc.exclusions.front().signature);
  // La piste bouge de 50 µm : la violation revient, « à revoir »
  for (auto& p : b.c.circuit.pistes)
    if (p.uuid == "proche") p.a.y = p.b.y = nm(15.30);
  k = connectivite(b.c);
  encore = verifierDRCReglee(b.c, k, remplirZones(b.c, k));
  const auto rev = std::find_if(encore.begin(), encore.end(), [&](const Violation& x) { return x.code == v.code && x.aRevoir; });
  CHECK(rev != encore.end());
  // Le rapport HTML, groupé par règle, exclusions à part, chaque ligne liée
  b.c.erc.exclusions.front().signature = signatureObjets(b.c, v.objets);
  k = connectivite(b.c);
  bilan = bilanDRC(b.c, k, remplirZones(b.c, k));
  const std::string h = rapportVerificationHtml(b.c, verifierERC(b.c, k, NiveauErc::Complet), bilan);
  CHECK(h.find("<h2>Exclusions</h2>") != std::string::npos);
  CHECK(h.find("écart voulu") != std::string::npos);
  CHECK(h.find("promethee:violation/0") != std::string::npos);
  CHECK(h.find("<h3 id=\"") != std::string::npos);
}

TEST_CASE("5.18 : vingt-quatre LED en couronne de 20 mm, tournées vers le centre, D1 à D24, chaque centre à 1 µm", "[L5][PLA-21]") {
  Banc5 b;
  for (int i = 0; i < 24; ++i) b.poser("LED", 25.4 + 12.7 * (i % 8), 25.4 + 25.4 * (i / 8));
  annoter(b.c, false);
  mettreAJourCircuit(b.c);
  b.c.circuit.contour.largeur = nm(60);
  b.c.circuit.contour.hauteur = nm(60);
  // L'ordre de la sélection : à rebours des repères
  std::vector<std::string> ids;
  for (const auto& k : b.c.composants) ids.push_back(k.uuid);
  std::reverse(ids.begin(), ids.end());
  OptionsCercle o;
  o.centre = ptMm(30, 30);
  o.rayon = nm(20);
  REQUIRE(disposerEnCercle(b.c, ids, o) == 24);
  for (std::size_t i = 0; i < ids.size(); ++i) {
    const ComposantElec* k = b.c.composant(ids[i]);
    CHECK(k->reference == "D" + std::to_string(i + 1));
    const EmpreintePlacee* e = nullptr;
    for (const auto& x : b.c.circuit.empreintes)
      if (x.composant == ids[i]) e = &x;
    REQUIRE(e);
    const Pt attendu = placeSurCercle(o, i, ids.size());
    CHECK(distance(e->position, attendu) <= 1000);
    CHECK(std::abs(distance(e->position, o.centre) - 20e6) <= 1000);
    // Le haut de l'empreinte regarde le centre
    const Pt haut = tournerDegres({0, -NM_PAR_MM}, e->rotation);
    const double vx = static_cast<double>(o.centre.x - e->position.x), vy = static_cast<double>(o.centre.y - e->position.y), n = std::hypot(vx, vy);
    CHECK(std::abs(static_cast<double>(haut.x) / 1e6 - vx / n) < 1e-3);
    CHECK(std::abs(static_cast<double>(haut.y) / 1e6 - vy / n) < 1e-3);
  }
}

TEST_CASE("5.2 : les composants ajoutés au schéma attendent dans « À placer », groupés par feuille, puis se posent sur la carte sans se recouvrir", "[L5][PLA-01][C1]") {
  Banc5 b;
  for (int i = 0; i < 6; ++i) b.poser("R", 25.4 + 12.7 * i, 25.4);
  annoter(b.c, false);
  mettreAJourCircuit(b.c);
  b.c.circuit.contour.largeur = nm(50);
  b.c.circuit.contour.hauteur = nm(30);
  auto groupes = groupesAPlacer(b.c);
  REQUIRE(groupes.size() == 1);
  CHECK(groupes.front().composants.size() == 6);
  CHECK(b.c.composant(groupes.front().composants.front())->reference == "R1");
  CHECK(placerGroupe(b.c, groupes.front().composants, ptMm(3, 3)) == 6);
  CHECK(groupesAPlacer(b.c).empty());
  std::vector<Boite> boites;
  for (const auto& e : b.c.circuit.empreintes) {
    CHECK_FALSE(aPlacer(b.c, e));
    boites.push_back(boite(encombrementSurCarte(e, *b.c.empreinte(e.empreinte))));
  }
  for (std::size_t i = 0; i < boites.size(); ++i)
    for (std::size_t j = i + 1; j < boites.size(); ++j) {
      const Boite& p = boites[i];
      const Boite& q = boites[j];
      const bool chevauche = p.x0 < q.x1 && q.x0 < p.x1 && p.y0 < q.y1 && q.y0 < p.y1;
      CHECK_FALSE(chevauche);
    }
}
