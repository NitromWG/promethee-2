// Prométhée : essais de la 0.34, lot L5 côté noyau — contour et repère de travail (5.4, 5.31), collisions,
// renumérotation (5.6, 5.19), pistes posées (5.9, 5.23), paires, accords et gouttes (5.10, 5.22, 8.27), zones
// (5.11, 5.25, 5.26), presse-papiers (5.16, 5.30), carte sans schéma (5.17), changements en masse (5.19), placement
// assisté (5.24), codes 2D et logos (5.15, 5.31), gabarits (5.15), réglages repris (5.32), noms à chemin (5.14).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/archive.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/electronique.hpp"
#include "promethee/routeur.hpp"
#include "promethee/structure_schema.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
int compter(const std::vector<Violation>& v, const std::string& code) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
}
int erreurs(const std::vector<Violation>& v) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [](const Violation& x) { return x.gravite == Gravite::Erreur; }));
}
std::vector<Violation> drc(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  return verifierDRC(c, k, remplirZones(c, k));
}
Piste piste(const std::string& reseau, Pt a, Pt b, Nm largeur = 250'000, Couche k = Couche::CuivreDessus) {
  Piste p;
  p.uuid = nouvelUuid();
  p.reseau = reseau;
  p.couche = k;
  p.a = a;
  p.b = b;
  p.largeur = largeur;
  return p;
}
Via via(const std::string& reseau, Pt p) {
  Via v;
  v.uuid = nouvelUuid();
  v.reseau = reseau;
  v.p = p;
  return v;
}
// Pastille posée sur la carte (repère de la carte)
Pt pastille(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  for (const auto& p : pastilles(c, connectivite(c)))
    if (p.composant == composant && p.numero == numero) return p.pastille.position;
  return {};
}
ConceptionElec carteNue(const std::string& nom) {
  ConceptionElec c = nouvelleConception(nom);
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(50);
  c.circuit.contour.hauteur = nm(40);
  return c;
}
std::string ligne(const std::vector<bool>& r) {
  std::string s;
  for (bool b : r) s += b ? '#' : '.';
  return s;
}
}  // namespace

TEST_CASE("5.4, 5.31 · contour modifié par ses sommets, saisi dans le repère de travail ; collisions d'encombrement", "[034][L5][5.4][5.31]") {
  ConceptionElec c = carteNue("Contour");
  REQUIRE(sommetsContour(c, -1).size() == 4);
  CHECK(deplacerSommetContour(c, -1, 2, ptMm(60, 45)));
  CHECK(c.circuit.contour.libre.size() == 4);
  CHECK(sommetsContour(c, -1)[2] == ptMm(60, 45));
  CHECK(insererSommetContour(c, -1, 0, ptMm(25, -5)));
  CHECK(sommetsContour(c, -1).size() == 5);
  CHECK(supprimerSommetContour(c, -1, 1));
  CHECK(sommetsContour(c, -1).size() == 4);
  CHECK_FALSE(deplacerSommetContour(c, 3, 0, {}));  // pas de découpe 3

  AffichageCircuit a;
  a.origineTravail = ptMm(10, 40);
  a.yVersLeHaut = true;
  CHECK(versRepereTravail(a, ptMm(10, 40)) == Pt{});
  CHECK(versRepereTravail(a, ptMm(20, 30)) == ptMm(10, 10));
  CHECK(depuisRepereTravail(a, ptMm(10, 10)) == ptMm(20, 30));
  c.circuit.affichage = a;
  REQUIRE(contourDepuisPoints(c, {ptMm(0, 0), ptMm(30, 0), ptMm(30, 20), ptMm(0, 20)}));
  const Boite b = boite(c.circuit.contour.libre);
  CHECK(b.x0 == nm(10));
  CHECK(b.y0 == nm(20));
  CHECK(b.x1 == nm(40));
  CHECK(b.y1 == nm(40));
  CHECK_FALSE(contourDepuisPoints(c, {ptMm(0, 0), ptMm(1, 1)}));

  ConceptionElec d = carteNue("Collisions");
  const std::string r1 = poserEmpreinteSeule(d, "R_0603", ptMm(20, 20));
  const std::string r2 = poserEmpreinteSeule(d, "R_0603", ptMm(21, 20));
  const std::string r3 = poserEmpreinteSeule(d, "R_0603", ptMm(30, 20));
  CHECK(collisionsEncombrement(d, r1) == std::vector<std::string>{r2});
  CHECK(collisionsEncombrement(d, r3).empty());
}

TEST_CASE("5.6, 5.19 · renumérotation selon la carte, huit parcours, préfixe du dessous ; chevelu mesuré", "[034][L5][5.6][5.19]") {
  ConceptionElec c = carteNue("Renumérotation");
  const std::string a = poserEmpreinteSeule(c, "R_0603", ptMm(30, 30));
  const std::string b = poserEmpreinteSeule(c, "R_0603", ptMm(10, 30));
  const std::string d = poserEmpreinteSeule(c, "R_0603", ptMm(30, 10));
  const std::string e = poserEmpreinteSeule(c, "R_0603", ptMm(10, 10));
  CHECK(c.composant(a)->reference == "R1");
  CHECK(c.composant(e)->reference == "R4");
  OptionsRenumerotation o;
  CHECK(renumerotationPrevue(c, o).size() == 4);
  std::vector<std::string> journal;
  CHECK(renumeroter(c, o, &journal) == 4);
  CHECK(journal.size() == 4);
  CHECK(c.composant(e)->reference == "R1");
  CHECK(c.composant(d)->reference == "R2");
  CHECK(c.composant(b)->reference == "R3");
  CHECK(c.composant(a)->reference == "R4");
  CHECK(renumerotationPrevue(c, o).empty());  // déjà en ordre
  // De bas en haut puis de droite à gauche
  o.parcours = Parcours::BHDG;
  renumeroter(c, o);
  CHECK(c.composant(a)->reference == "R1");
  CHECK(c.composant(d)->reference == "R2");
  CHECK(c.composant(b)->reference == "R3");
  CHECK(c.composant(e)->reference == "R4");
  // Le dessous à part, préfixé « B », après le dessus
  for (auto& x : c.circuit.empreintes)
    if (x.composant == a) x.dessous = true;
  o = {};
  o.prefixeDessous = "B";
  renumeroter(c, o);
  CHECK(c.composant(a)->reference == "BR1");
  CHECK(c.composant(e)->reference == "R1");
  CHECK(c.composant(b)->reference == "R3");
  // Repère exclu : gardé, son numéro n'est pas redonné
  o.exclus = {"R1"};
  o.parcours = Parcours::DGHB;
  renumeroter(c, o);
  CHECK(c.composant(e)->reference == "R1");
  CHECK(c.composant(d)->reference == "R2");
  CHECK(c.composant(b)->reference == "R3");

  // Chevelu : une liaison de 20 mm
  ConceptionElec k = carteNue("Chevelu");
  const std::string r1 = poserEmpreinteSeule(k, "R_0603", ptMm(10, 20));
  const std::string r2 = poserEmpreinteSeule(k, "R_0603", ptMm(30, 20));
  reseauParPiste(k, r1, "2", r2, "1");
  const Connectivite kk = connectivite(k);
  const EtatRoutage er = etatRoutage(k, kk, remplirZones(k, kk));
  CHECK(longueurChevelu(er) == Approx(distance(pastille(k, r1, "2"), pastille(k, r2, "1"))));
}

TEST_CASE("5.9, 5.23 · nettoyage prévu puis choisi, segment glissé à 45°, via déplacée, pistes coupées, dérouter, pinceau, lissage", "[034][L5][5.9][5.23]") {
  ConceptionElec c = carteNue("Pistes");
  auto& P = c.circuit.pistes;
  P.push_back(piste("A", ptMm(5, 5), ptMm(5, 5)));
  P.push_back(piste("A", ptMm(10, 10), ptMm(20, 10)));
  P.push_back(piste("A", ptMm(20, 10), ptMm(10, 10)));
  P.push_back(piste("B", ptMm(10, 20), ptMm(15, 20)));
  P.push_back(piste("B", ptMm(15, 20), ptMm(25, 20)));
  c.circuit.vias.push_back(via("C", ptMm(40, 30)));
  const auto prevu = nettoyagePrevu(c);
  std::multiset<std::string> genres;
  for (const auto& x : prevu) genres.insert(x.genre);
  CHECK(genres == std::multiset<std::string>{"double", "fusion", "nul", "via orpheline"});
  CHECK(nettoyer(c, prevu) == 4);
  REQUIRE(P.size() == 2);
  CHECK(c.circuit.vias.empty());
  const auto fusion = std::find_if(P.begin(), P.end(), [](const Piste& p) { return p.reseau == "B"; });
  CHECK(fusion->a == ptMm(10, 20));
  CHECK(fusion->b == ptMm(25, 20));
  CHECK(nettoyagePrevu(c).empty());

  // Glisser le segment du milieu d'un créneau : ses voisins le suivent le long de leur axe
  ConceptionElec g = carteNue("Glisser");
  const Piste s1 = piste("N", ptMm(10, 10), ptMm(20, 10)), s2 = piste("N", ptMm(20, 10), ptMm(20, 20)), s3 = piste("N", ptMm(20, 20), ptMm(30, 20));
  g.circuit.pistes = {s1, s2, s3};
  REQUIRE(glisserSegment(g, s2.uuid, ptMm(3, 1)));  // la part le long du segment est ignorée
  CHECK(g.circuit.pistes[0].b == ptMm(23, 10));
  CHECK(g.circuit.pistes[1].a == ptMm(23, 10));
  CHECK(g.circuit.pistes[1].b == ptMm(23, 20));
  CHECK(g.circuit.pistes[2].a == ptMm(23, 20));
  CHECK(g.circuit.pistes.size() == 3);

  // Via déplacée : les pistes qui y aboutissent la suivent
  ConceptionElec v = carteNue("Via");
  v.circuit.pistes = {piste("N", ptMm(10, 20), ptMm(20, 20)), piste("N", ptMm(20, 20), ptMm(20, 30), 250'000, Couche::CuivreDessous)};
  const Via vv = via("N", ptMm(20, 20));
  v.circuit.vias = {vv};
  REQUIRE(deplacerVia(v, vv.uuid, ptMm(22, 22)));
  CHECK(v.circuit.pistes[0].b == ptMm(22, 22));
  CHECK(v.circuit.pistes[1].a == ptMm(22, 22));

  // Couper puis trancher
  ConceptionElec k = carteNue("Couper");
  k.circuit.pistes = {piste("N", ptMm(10, 10), ptMm(30, 10))};
  REQUIRE(couperPiste(k, k.circuit.pistes[0].uuid, ptMm(20, 10.3)));
  REQUIRE(k.circuit.pistes.size() == 2);
  CHECK(k.circuit.pistes[0].b == ptMm(20, 10));
  CHECK(k.circuit.pistes[1].a == ptMm(20, 10));
  REQUIRE(trancherPiste(k, k.circuit.pistes[1].uuid, ptMm(25, 10), nm(1)));
  double total = 0;
  for (const auto& p : k.circuit.pistes) total += longueurPiste(p);
  CHECK(total == Approx(20e6 - 1.25e6).margin(10));

  // Dérouter un réseau : le cuivre verrouillé reste, les autres réseaux aussi
  ConceptionElec d = carteNue("Dérouter");
  Piste fige = piste("A", ptMm(10, 30), ptMm(20, 30));
  fige.verrou = Verrou::Fige;
  d.circuit.pistes = {piste("A", ptMm(10, 10), ptMm(20, 10)), fige, piste("B", ptMm(10, 20), ptMm(20, 20))};
  d.circuit.vias = {via("A", ptMm(20, 10))};
  CHECK(derouter(d, "reseau:A") == 2);
  CHECK(d.circuit.pistes.size() == 2);
  CHECK(d.circuit.vias.empty());
  CHECK(appliquerReseau(d, {d.circuit.pistes[1].uuid}, "C") == 1);
  CHECK(d.circuit.pistes[1].reseau == "C");
  CHECK(derouter(d, "carte") == 1);  // seule la piste figée reste

  // Lissage : le coin droit est coupé à 45°
  ConceptionElec l = carteNue("Lisser");
  const Piste t1 = piste("L", ptMm(10, 10), ptMm(20, 10)), t2 = piste("L", ptMm(20, 10), ptMm(20, 20));
  l.circuit.pistes = {t1, t2};
  CHECK(lisser(l, {t1.uuid, t2.uuid}) == 1);
  REQUIRE(l.circuit.pistes.size() == 3);
  const Piste& diag = l.circuit.pistes[2];
  CHECK(std::abs(diag.b.x - diag.a.x) == std::abs(diag.b.y - diag.a.y));
}

TEST_CASE("5.9, 5.21 · plusieurs liaisons routées en une commande ; 5.10 paire à écart constant sans DRC-53", "[034][L5][5.9][5.10]") {
  ConceptionElec c = carteNue("Plusieurs");
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(10, 10));
  const std::string r2 = poserEmpreinteSeule(c, "R_0603", ptMm(30, 10));
  const std::string r3 = poserEmpreinteSeule(c, "R_0603", ptMm(10, 25));
  const std::string r4 = poserEmpreinteSeule(c, "R_0603", ptMm(30, 25));
  reseauParPiste(c, r1, "2", r2, "1");
  reseauParPiste(c, r3, "2", r4, "1");
  Connectivite k = connectivite(c);
  const auto chevelu = etatRoutage(c, k, remplirZones(c, k)).chevelu;
  REQUIRE(chevelu.size() == 2);
  std::vector<std::string> raisons;
  CHECK(routerPlusieurs(c, chevelu, &raisons) == 2);
  CHECK(raisons.empty());
  k = connectivite(c);
  CHECK(etatRoutage(c, k, remplirZones(c, k)).chevelu.empty());

  ConceptionElec p = carteNue("Paire");
  const std::string a1 = poserEmpreinteSeule(p, "R_0603", ptMm(10, 15));
  const std::string b1 = poserEmpreinteSeule(p, "R_0603", ptMm(10, 16));
  const std::string a2 = poserEmpreinteSeule(p, "R_0603", ptMm(40, 15));
  const std::string b2 = poserEmpreinteSeule(p, "R_0603", ptMm(40, 16));
  const std::string P = reseauParPiste(p, a1, "2", a2, "1"), N = reseauParPiste(p, b1, "2", b2, "1");
  PaireDifferentielle paire;
  paire.nom = "USB";
  paire.positif = P;
  paire.negatif = N;
  paire.largeur = 200'000;
  paire.ecart = 800'000;
  paire.ecartMin = 700'000;
  paire.ecartMax = 900'000;
  p.circuit.regles.avancees.paires.push_back(paire);
  const BilanPaire bilan = routerPaire(p, paire, Couche::CuivreDessus);
  REQUIRE(bilan.ok);
  CHECK(bilan.raison.empty());
  CHECK(bilan.ecart == 800'000);
  CHECK(bilan.pistes.size() >= 2);
  CHECK(compter(drc(p), "DRC-53") == 0);
  CHECK(compter(drc(p), "DRC-20") == 0);
  CHECK(longueurReseau(p, P) == Approx(longueurReseau(p, N)).margin(1e3));
}

TEST_CASE("5.22, 8.27 · motifs d'accord : longueurs, accord d'un réseau et d'un groupe, motifs refaits après une règle", "[034][L5][5.22][8.27]") {
  MotifAccord m;
  m.uuid = "m";
  m.reseau = "T";
  m.a = ptMm(10, 20);
  m.b = ptMm(40, 20);
  m.largeur = 200'000;
  m.amplitude = nm(1);
  m.pas = nm(1);
  CHECK(longueurMotif(m) == Approx(88e6).margin(1e3));  // 29 créneaux de 2 × 1 mm
  m.deuxCotes = false;
  CHECK(longueurMotif(m) == Approx(88e6).margin(1e3));
  m.genre = GenreMotifAccord::DentsDeScie;
  CHECK(longueurMotif(m) == Approx(30e6 + 29 * (2 * std::hypot(0.5e6, 1e6) - 1e6)).margin(1e3));
  m.genre = GenreMotifAccord::Trombone;
  CHECK(longueurMotif(m) == Approx(30e6 + 29 * (2e6 + std::numbers::pi * 0.25e6 - 0.5e6)).epsilon(0.002));
  for (const auto& x : segmentsMotif(m)) CHECK(x.motif == "m");

  ConceptionElec c = carteNue("Accord");
  c.circuit.pistes = {piste("T", ptMm(10, 20), ptMm(40, 20), 200'000), piste("U", ptMm(5, 32), ptMm(45, 32), 200'000), piste("U", ptMm(45, 32), ptMm(45, 35), 200'000)};
  CHECK(longueurReseau(c, "T") == Approx(30e6));
  const std::string id = accorderReseau(c, "T", nm(40), GenreMotifAccord::Accordeon);
  REQUIRE_FALSE(id.empty());
  CHECK(longueurReseau(c, "T") == Approx(40e6).margin(5e3));
  CHECK(c.circuit.motifs.size() == 1);
  CHECK(erreurs(drc(c)) == 0);
  CHECK(accorderReseau(c, "T", nm(39), GenreMotifAccord::Accordeon).empty());  // déjà assez long

  GroupeAccord g;
  g.nom = "Bus";
  g.reseaux = {"T", "U"};
  g.tolerance = 250'000;
  c.circuit.regles.avancees.accords.push_back(g);
  CHECK(compter(drc(c), "DRC-54") == 1);  // T à 40 mm, U à 43 mm
  std::vector<std::string> nonTenus;
  CHECK(accorderGroupe(c, g, &nonTenus) == 1);
  CHECK(nonTenus.empty());
  CHECK(longueurReseau(c, "T") == Approx(43e6).margin(250e3));
  CHECK(compter(drc(c), "DRC-54") == 0);

  // Règle changée : les motifs sont refaits à la nouvelle largeur, la cible tenue
  c.circuit.regles.largeur = 150'000;
  CHECK(regenererMotifs(c) == 2);
  for (const auto& p : c.circuit.pistes)
    if (!p.motif.empty()) CHECK(p.largeur == 150'000);
  CHECK(longueurReseau(c, "T") == Approx(43e6).margin(250e3));
}

TEST_CASE("8.27 · accord en retard : microruban plus rapide que la ligne à bande, DRC-54 en picosecondes", "[034][L5][8.27]") {
  ConceptionElec c = carteNue("Retard");
  c.circuit.empilage.couchesCuivre = 4;
  const double ext = psParMm(c, Couche::CuivreDessus, 200'000), inter = psParMm(c, Couche::CuivreInt1, 200'000);
  CHECK(ext > 5.0);
  CHECK(inter > ext);
  CHECK(inter == Approx(std::sqrt(4.4) / 0.299792458).epsilon(0.05));
  c.circuit.pistes = {piste("A", ptMm(5, 10), ptMm(25, 10), 200'000), piste("B", ptMm(5, 20), ptMm(25, 20), 200'000, Couche::CuivreInt1)};
  CHECK(retardReseau(c, "B") > retardReseau(c, "A"));
  GroupeAccord g;
  g.nom = "Horloges";
  g.reseaux = {"A", "B"};
  g.retard = true;
  g.tolerance = 5'000;  // 5 ps
  c.circuit.regles.avancees.accords.push_back(g);
  CHECK(compter(drc(c), "DRC-54") == 1);
  g.retard = false;
  c.circuit.regles.avancees.accords[0] = g;
  CHECK(compter(drc(c), "DRC-54") == 0);  // mêmes longueurs
}

TEST_CASE("5.22 · gouttes posées sur les pastilles et les vias, sans erreur de DRC, puis retirées", "[034][L5][5.22]") {
  ConceptionElec c = carteNue("Gouttes");
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(20, 20));
  const std::string r2 = poserEmpreinteSeule(c, "R_0603", ptMm(40, 20));
  const std::string n = reseauParPiste(c, r1, "2", r2, "1");
  c.circuit.pistes = {piste(n, pastille(c, r1, "2"), ptMm(30, 20), 200'000), piste(n, ptMm(30, 20), pastille(c, r2, "1"), 200'000)};
  Via v = via(n, ptMm(30, 20));
  v.diametre = 600'000;
  v.percage = 300'000;
  c.circuit.vias = {v};
  const int avant = erreurs(drc(c));
  OptionsGouttes o;
  CHECK(poserGouttes(c, o) == 4);  // deux pastilles, la via de chaque côté
  CHECK(c.circuit.regions.size() == 4);
  for (const auto& g : c.circuit.regions) {
    CHECK(g.genre == "goutte");
    CHECK(aireMm2(g.forme) > 0.05);
  }
  CHECK(erreurs(drc(c)) == avant);
  CHECK(poserGouttes(c, o) == 4);  // refaites, pas doublées
  o.vias = false;
  CHECK(poserGouttes(c, o) == 2);
  CHECK(retirerGouttes(c) == 2);
  CHECK(c.circuit.regions.empty());
}

TEST_CASE("5.11, 5.25, 5.26 · zones : plan du réseau, noms uniques, dupliquer, couper, fusionner, soustraire, depuis des pistes, arrondir, figer et périmer", "[034][L5][5.11][5.26]") {
  ConceptionElec c = carteNue("Zones");
  const std::string zb = zoneSurToutLaCarte(c, "GND", Couche::CuivreDessous);
  CHECK(c.circuit.zones.back().nom == "GND · B.Cu");
  const auto plan = planDuReseau(c, "GND", {Couche::CuivreDessus, Couche::CuivreDessous});
  REQUIRE(plan.size() == 1);
  CHECK(c.circuit.zones.back().nom == "GND · F.Cu");
  const std::string zd = dupliquerZone(c, zb, Couche::CuivreDessus);
  CHECK(c.circuit.zones.back().nom == "GND · F.Cu (2)");
  CHECK(nommerZones(c) == 0);

  const auto moities = couperZone(c, zb, ptMm(25, -10), ptMm(25, 50));
  REQUIRE(moities.size() == 2);
  double aire = 0;
  for (const auto& z : c.circuit.zones)
    if (z.couche == Couche::CuivreDessous) aire += aireMm2(z.contour);
  CHECK(aire == Approx(2000).margin(0.01));
  CHECK(fusionnerZones(c, moities) == zb);
  CHECK(std::count_if(c.circuit.zones.begin(), c.circuit.zones.end(), [](const Zone& z) { return z.couche == Couche::CuivreDessous; }) == 1);
  CHECK(couperZone(c, zb, ptMm(60, 0), ptMm(60, 10)).empty());  // la droite ne coupe pas la zone

  // Soustraire une fenêtre du milieu : la zone garde un seul contour, troué par une coupure
  Zone fenetre;
  fenetre.uuid = nouvelUuid();
  fenetre.reseau = "GND";
  fenetre.couche = Couche::CuivreDessus;
  fenetre.contour = orienter(rectangle(ptMm(25, 20), nm(10), nm(10)));
  c.circuit.zones.push_back(fenetre);
  REQUIRE(soustraireZone(c, zd, fenetre.uuid));
  const auto it = std::find_if(c.circuit.zones.begin(), c.circuit.zones.end(), [&](const Zone& z) { return z.uuid == zd; });
  CHECK(aireMm2(it->contour) == Approx(1900).margin(0.5));

  ConceptionElec p = carteNue("Depuis des pistes");
  p.circuit.pistes = {piste("PWR", ptMm(5, 5), ptMm(15, 5))};
  const std::string zp = zoneDepuisPistes(p, {p.circuit.pistes[0].uuid}, nm(1));
  REQUIRE_FALSE(zp.empty());
  CHECK(aireMm2(p.circuit.zones[0].contour) == Approx(10 * 2.25 + std::numbers::pi * 1.125 * 1.125).epsilon(0.01));
  CHECK(p.circuit.zones[0].reseau == "PWR");
  Zone carre;
  carre.uuid = nouvelUuid();
  carre.reseau = "PWR";
  carre.contour = orienter(rectangle(ptMm(30, 20), nm(10), nm(10)));
  p.circuit.zones.push_back(carre);
  REQUIRE(arrondirZone(p, carre.uuid, nm(1), true));
  CHECK(p.circuit.zones.back().contour.size() == 8);
  CHECK(aireMm2(p.circuit.zones.back().contour) == Approx(100 - 4 * 0.5).margin(0.01));
  p.circuit.zones.back().contour = orienter(rectangle(ptMm(30, 20), nm(10), nm(10)));
  REQUIRE(arrondirZone(p, carre.uuid, nm(2), false));
  CHECK(aireMm2(p.circuit.zones.back().contour) == Approx(100 - 4 * (4 - std::numbers::pi)).epsilon(0.01));

  // Zone figée : remplissage gardé, périmé dès que le cuivre voisin change
  ConceptionElec f = carteNue("Figée");
  const std::string zf = zoneSurToutLaCarte(f, "GND", Couche::CuivreDessus);
  f.circuit.vias.push_back(via("GND", ptMm(45, 35)));  // le cuivre de la zone touche son réseau
  CHECK(etatZones(f, {zf}, EtatZone::Figee) == 1);
  CHECK_FALSE(f.circuit.zones[0].remplissageFige.empty());
  CHECK(remplissagesPerimes(f).empty());
  f.circuit.pistes.push_back(piste("SIG", ptMm(10, 10), ptMm(40, 10)));
  CHECK(remplissagesPerimes(f) == std::vector<std::string>{zf});
  CHECK(etatZones(f, {zf}, EtatZone::Normale) == 1);
  CHECK(f.circuit.zones[0].remplissageFige.empty());
  CHECK(remplissagesPerimes(f).empty());
}

TEST_CASE("5.11, 5.25 · couture de vias en grille, îlot relié au plan de l'autre face", "[034][L5][5.11][5.25]") {
  ConceptionElec c = carteNue("Couture");
  const std::string dessus = zoneSurToutLaCarte(c, "GND", Couche::CuivreDessus);
  zoneSurToutLaCarte(c, "GND", Couche::CuivreDessous);
  Via ancre0 = via("GND", ptMm(1.5, 1.5));
  c.circuit.vias.push_back(ancre0);  // le plan touche son réseau
  const int n = coudreZone(c, dessus, nm(5), 600'000, 300'000);
  CHECK(n >= 50);
  CHECK(static_cast<int>(c.circuit.vias.size()) == n + 1);
  for (const auto& v : c.circuit.vias)
    if (v.uuid != ancre0.uuid) CHECK(v.groupe == "couture:" + dessus);
  const auto vd = drc(c);
  for (const auto& v : vd)
    if (v.gravite == Gravite::Erreur) UNSCOPED_INFO(v.code + " " + v.message);
  CHECK(erreurs(vd) == 0);
  CHECK(coudreZone(c, dessus, nm(10)) < n);  // refaite, pas ajoutée
  CHECK(static_cast<int>(c.circuit.vias.size()) < n);

  ConceptionElec i = carteNue("Îlot");
  const std::string zi = zoneSurToutLaCarte(i, "GND", Couche::CuivreDessus);
  zoneSurToutLaCarte(i, "GND", Couche::CuivreDessous);
  // Un anneau d'un autre réseau enferme un îlot de masse ; une via de masse ancre le reste du plan
  i.circuit.pistes = {piste("X", ptMm(10, 10), ptMm(20, 10)), piste("X", ptMm(20, 10), ptMm(20, 20)), piste("X", ptMm(20, 20), ptMm(10, 20)), piste("X", ptMm(10, 20), ptMm(10, 10))};
  Via ancre = via("GND", ptMm(40, 30));
  ancre.diametre = 600'000;
  ancre.percage = 300'000;
  i.circuit.vias = {ancre};
  CHECK(relierIlotsParVias(i, zi) == 1);
  REQUIRE(i.circuit.vias.size() == 2);
  const Via& posee = i.circuit.vias.back();
  CHECK(posee.groupe == "ilot:" + zi);
  CHECK(posee.p.x > nm(10));
  CHECK(posee.p.x < nm(20));
  CHECK(posee.p.y > nm(10));
  CHECK(posee.p.y < nm(20));
  CHECK(relierIlotsParVias(i, zi) == 1);  // refaite
}

TEST_CASE("5.16, 5.30 · presse-papiers du circuit : copie, JSON, collage spécial (réseaux, classe, composants neufs)", "[034][L5][5.16][5.30]") {
  ConceptionElec c = carteNue("Presse-papiers");
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(10, 10));
  const std::string r2 = poserEmpreinteSeule(c, "R_0603", ptMm(20, 10));
  const std::string n = reseauParPiste(c, r1, "2", r2, "1");
  c.circuit.pistes = {piste(n, pastille(c, r1, "2"), pastille(c, r2, "1"))};
  const PressePapiersCircuit pp = copierCircuit(c, {r1, c.circuit.pistes[0].uuid});
  CHECK(pp.empreintes.size() == 1);
  CHECK(pp.pistes.size() == 1);
  CHECK_FALSE(pp.vide());
  const PressePapiersCircuit relu = presseCircuitDepuisJson(presseCircuitVersJson(pp));
  CHECK(relu.empreintes.size() == 1);
  CHECK(relu.composants.size() == 1);
  CHECK(relu.pistes.size() == 1);
  CHECK(relu.reference == pp.reference);
  CHECK(presseCircuitDepuisJson(nlohmann::ordered_json::object()).vide());

  OptionsCollageCircuit o;
  o.classe = "Copie";
  const auto crees = collerCircuit(c, relu, ptMm(10, 25), o);
  CHECK(crees.size() == 2);
  CHECK(c.circuit.empreintes.size() == 3);
  CHECK(c.composants.back().reference == "R3");
  CHECK(c.composants.back().carteSeule);
  CHECK(c.circuit.pistes.back().reseau == n);
  CHECK(c.circuit.pistes.back().a.y == pastille(c, r1, "2").y + nm(15));
  CHECK(c.nomsReseaux[c.composants.back().uuid + ":2"] == n);  // la copie garde le réseau de sa broche
  o.garderReseaux = false;
  collerCircuit(c, relu, ptMm(10, 32), o);
  CHECK(c.circuit.pistes.back().reseau.empty());
}

TEST_CASE("5.17 · carte sans schéma : empreinte posée seule, réseau par la piste, échange, serrer, sélections logiques", "[034][L5][5.17]") {
  ConceptionElec c = carteNue("Sans schéma");
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(10, 10));
  const std::string c1 = poserEmpreinteSeule(c, "C_0603", ptMm(20, 10));
  const std::string j1 = poserEmpreinteSeule(c, "MountingHole_3.2", ptMm(40, 30));
  CHECK(c.composant(r1)->reference == "R1");
  CHECK(c.composant(c1)->reference == "C1");
  CHECK(c.composant(j1)->reference.rfind("H", 0) == 0);
  const std::string n = reseauParPiste(c, r1, "2", c1, "1");
  CHECK(n == "N-R1-2");
  Connectivite k = connectivite(c);
  int surReseau = 0;
  for (const auto& p : pastilles(c, k)) surReseau += p.reseau == n ? 1 : 0;
  CHECK(surReseau == 2);
  // Une troisième pastille rejoint le réseau ; puis deux réseaux réunis par une piste
  const std::string r2 = poserEmpreinteSeule(c, "R_0603", ptMm(30, 10));
  CHECK(reseauParPiste(c, c1, "2", r2, "1") == "N-C1-2");
  CHECK(reseauParPiste(c, r2, "1", r1, "2") == "N-C1-2");
  k = connectivite(c);
  surReseau = 0;
  for (const auto& p : pastilles(c, k)) surReseau += p.reseau == "N-C1-2" ? 1 : 0;
  CHECK(surReseau == 4);
  CHECK(compter(drc(c), "DRC-39") == 0);
  CHECK(compter(drc(c), "DRC-42") == 0);

  const Pt avant1 = c.circuit.empreintes[0].position, avant2 = c.circuit.empreintes[1].position;
  CHECK(echangerEmpreintes(c, r1, c1));
  CHECK(c.circuit.empreintes[0].position == avant2);
  CHECK(c.circuit.empreintes[1].position == avant1);
  CHECK(serrerEmpreintes(c, {r1, c1, r2}) >= 2);
  CHECK(collisionsEncombrement(c, r1).empty());
  CHECK(collisionsEncombrement(c, c1).empty());

  c.circuit.pistes = {piste("N-C1-2", ptMm(1, 1), ptMm(2, 2))};
  const auto sel = selectionLogique(c, "reseau", "N-C1-2");
  CHECK(sel.count(r1));
  CHECK(sel.count(c1));
  CHECK(sel.count(c.circuit.pistes[0].uuid));
  CHECK(selectionLogique(c, "empreinte", "C_0603") == std::set<std::string>{c1});
  GroupeCircuit g;
  g.uuid = "g1";
  g.nom = "Entrée";
  g.membres = {r1, r2};
  c.circuit.groupes.push_back(g);
  CHECK(selectionLogique(c, "groupe", "Entrée") == std::set<std::string>{r1, r2});
}

TEST_CASE("5.19 · placement repris d'un CSV KiCad et d'un classeur XLSX d'Altium, repères placés, textes et couches en masse", "[034][L5][5.19]") {
  ConceptionElec c = carteNue("Import");
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(5, 5));
  const std::string r2 = poserEmpreinteSeule(c, "R_0603", ptMm(6, 5));
  const BilanImportPlacement b = importerPlacement(c, "# Placement\nRef,Val,Package,PosX,PosY,Rot,Side\nR1,10k,R_0603,12.5,7.5,90,top\nR9,1k,R_0603,1,1,0,top\n");
  CHECK(b.places == 1);
  CHECK(b.inconnus == 1);
  CHECK(pastille(c, r1, "1").x + pastille(c, r1, "2").x == 2 * nm(12.5));
  CHECK(c.circuit.empreintes[0].rotation == Approx(90));
  CHECK(c.circuit.empreintes[0].position.y == nm(40 - 7.5));
  // Classeur : chaînes partagées et feuille 1, en-têtes d'Altium, face du dessous
  const std::string chaines =
      "<?xml version=\"1.0\"?><sst><si><t>Designator</t></si><si><t>Mid X</t></si><si><t>Mid Y</t></si><si><t>Rotation</t></si><si><t>Layer</t></si><si><t>R2</t></si><si><t>Bottom</t></si></sst>";
  const std::string feuille =
      "<?xml version=\"1.0\"?><worksheet><sheetData><row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c><c r=\"C1\" t=\"s\"><v>2</v></c><c r=\"D1\" t=\"s\"><v>3</v></c>"
      "<c r=\"E1\" t=\"s\"><v>4</v></c></row><row r=\"2\"><c r=\"A2\" t=\"s\"><v>5</v></c><c r=\"B2\"><v>20</v></c><c r=\"C2\"><v>10</v></c><c r=\"D2\"><v>180</v></c><c r=\"E2\" t=\"s\"><v>6</v></c></row></sheetData></worksheet>";
  const std::string xlsx = prom::zipStocke({{"xl/sharedStrings.xml", chaines}, {"xl/worksheets/sheet1.xml", feuille}});
  const auto lignes = lireXlsx(xlsx);
  REQUIRE(lignes.size() == 2);
  CHECK(lignes[1][0] == "R2");
  CHECK(lignes[1][1] == "20");
  const BilanImportPlacement bx = importerPlacement(c, xlsx);
  CHECK(bx.places == 1);
  CHECK(c.circuit.empreintes[1].dessous);
  CHECK(c.circuit.empreintes[1].rotation == Approx(180));
  CHECK(lireTableau("a;b;\"c;d\"\n1;2;3\n")[0] == std::vector<std::string>{"a", "b", "c;d"});

  CHECK(placerReperes(c, {r1}, 8) == 1);
  const DefEmpreinte* d = c.empreinte(c.circuit.empreintes[0].empreinte);
  REQUIRE(c.circuit.empreintes[0].repere);
  CHECK(c.circuit.empreintes[0].repere->y < d->encombrement.y0);
  CHECK(placerReperes(c, {}, 0) == 2);

  TexteCircuit t;
  t.uuid = nouvelUuid();
  t.texte = "REV A";
  t.couche = Couche::SerigraphieDessus;
  c.circuit.textes.push_back(t);
  CHECK(modifierTextes(c, {}, nm(1.5), Couche::SerigraphieDessous) == 1);
  CHECK(c.circuit.textes[0].hauteur == nm(1.5));
  CHECK(c.circuit.textes[0].couche == Couche::SerigraphieDessous);
  CHECK(c.circuit.serigraphie.hauteur == nm(1.5));

  c.circuit.pistes = {piste("A", ptMm(1, 1), ptMm(2, 2)), piste("B", ptMm(3, 3), ptMm(4, 4), 250'000, Couche::CuivreDessous)};
  CHECK(echangerCouches(c, {{Couche::CuivreDessus, Couche::CuivreDessous}, {Couche::CuivreDessous, Couche::CuivreDessus}}) == 2);
  CHECK(c.circuit.pistes[0].couche == Couche::CuivreDessous);
  CHECK(c.circuit.pistes[1].couche == Couche::CuivreDessus);
}

TEST_CASE("5.24 · découplage placé au plus près de sa broche puis relié ; placement poussé, bloqué par un verrou", "[034][L5][5.24]") {
  ConceptionElec c = carteNue("Découplage");
  const std::string u1 = poserEmpreinteSeule(c, "SOIC-8", ptMm(25, 20));
  const std::string c1 = poserEmpreinteSeule(c, "C_0603", ptMm(5, 5));
  c.composant(c1)->valeur = "100n";
  c.nomsReseaux[u1 + ":8"] = "VCC";
  c.nomsReseaux[u1 + ":4"] = "GND";
  c.nomsReseaux[c1 + ":1"] = "VCC";
  c.nomsReseaux[c1 + ":2"] = "GND";
  CHECK(placerDecouplages(c, u1) == 1);
  const double d = distance(pastille(c, c1, "1"), pastille(c, u1, "8"));
  CHECK(d < nm(4.5));
  CHECK(collisionsEncombrement(c, c1).empty());
  CHECK(std::any_of(c.circuit.pistes.begin(), c.circuit.pistes.end(), [](const Piste& p) { return p.reseau == "VCC"; }));
  CHECK(placerDecouplages(c, u1) == 0);  // déjà en place

  ConceptionElec p = carteNue("Poussé");
  const std::string a = poserEmpreinteSeule(p, "R_0603", ptMm(20, 20));
  const std::string b = poserEmpreinteSeule(p, "R_0603", ptMm(25, 20));
  REQUIRE(pousserEmpreintes(p, a, ptMm(24, 20)));
  CHECK(p.circuit.empreintes[0].position == ptMm(24, 20));
  CHECK(p.circuit.empreintes[1].position.x > nm(25));
  CHECK(collisionsEncombrement(p, a).empty());
  p.circuit.empreintes[1].verrou = true;
  const Pt avant = p.circuit.empreintes[0].position;
  CHECK_FALSE(pousserEmpreintes(p, a, p.circuit.empreintes[1].position));
  CHECK(p.circuit.empreintes[0].position == avant);
  (void)b;
}

TEST_CASE("5.15, 5.31 · QR code, Micro QR et Code 128 aux normes ; modules et barres en polygones ; logo vectorisé", "[034][L5][5.15][5.31]") {
  // QR version 1-M, mode octet : motifs de position, synchronisation, module sombre, informations de format BCH
  const auto q = codeQR("PROMETHEE");
  REQUIRE(q.size() == 21);
  CHECK(ligne(q[0]).substr(0, 7) == "#######");
  CHECK(ligne(q[1]).substr(0, 7) == "#.....#");
  CHECK(ligne(q[6]).substr(8, 5) == "#.#.#");  // synchronisation
  CHECK(q[21 - 8][8]);                         // module toujours sombre
  // Les deux copies du format disent la même chose et sont un mot BCH(15,5) valable, correction M
  unsigned f1 = 0, f2 = 0;
  for (int i = 0; i <= 5; ++i) f1 |= static_cast<unsigned>(q[static_cast<std::size_t>(i)][8]) << i;
  f1 |= static_cast<unsigned>(q[7][8]) << 6;
  f1 |= static_cast<unsigned>(q[8][8]) << 7;
  f1 |= static_cast<unsigned>(q[8][7]) << 8;
  for (int i = 9; i < 15; ++i) f1 |= static_cast<unsigned>(q[8][static_cast<std::size_t>(14 - i)]) << i;
  for (int i = 0; i < 8; ++i) f2 |= static_cast<unsigned>(q[8][static_cast<std::size_t>(20 - i)]) << i;
  for (int i = 8; i < 15; ++i) f2 |= static_cast<unsigned>(q[static_cast<std::size_t>(21 - 15 + i)][8]) << i;
  CHECK(f1 == f2);
  const unsigned brut = f1 ^ 0x5412U;
  CHECK((brut >> 13) == 0U);  // niveau M : 00
  unsigned reste = brut;
  for (int i = 14; i >= 10; --i)
    if ((reste >> i) & 1U) reste ^= 0x537U << (i - 10);
  CHECK(reste == 0U);
  // Matrices relues par un décodeur indépendant (zxing-cpp) au développement : elles servent d'oracle
  const std::vector<std::string> qrAttendu{
      "#######.##....#######", "#.....#...#.#.#.....#", "#.###.#...###.#.###.#", "#.###.#.##....#.###.#", "#.###.#.#...#.#.###.#", "#.....#.##.#..#.....#", "#######.#.#.#.#######",
      "........#####........", "#...#.####.#.#####..#", "#.#....#...##.#...#.#", ".#.#.###.###.#..####.", "..####.####..#.##....", "....###.#.#.###.#.###", "........#.#.#.#..#..#",
      "#######.###.##..#.##.", "#.....#...###..##..#.", "#.###.#.##.#.###..##.", "#.###.#...###..#.#.##", "#.###.#..#.#..#.#....", "#.....#......##.#....", "#######.##..####..#.#"};
  for (std::size_t y = 0; y < q.size(); ++y) CHECK(ligne(q[y]) == qrAttendu[y]);
  const std::vector<std::string> microAttendu{"#######.#.#.#", "#.....#......", "#.###.#.###.#", "#.###.#.#..##", "#.###.#.####.", "#.....#..##.#", "#######.##..#",
                                              ".........####", "##.#.#.####..", ".....#.#..#..", "#......#..##.", ".##.#####...#", "##..#.#######"};
  const auto mq = codeMicroQR("12345");
  for (std::size_t y = 0; y < mq.size(); ++y) CHECK(ligne(mq[y]) == microAttendu[y]);
  CHECK(codeQR(std::string(300, 'x')).empty());  // au-delà de la version 10
  CHECK(codeQR("https://promethee.example/carte/0.34").size() == 29);  // 36 octets : version 3

  // Micro QR : M2 en numérique, M3 et M4 en octets
  CHECK(codeMicroQR("12345").size() == 13);
  CHECK(codeMicroQR("PROM-34").size() == 15);  // alphanumérique, 7 caractères : M3
  CHECK(codeMicroQR("Promethee").size() == 15);
  CHECK(codeMicroQR("Promethee 0.34").size() == 17);
  CHECK(codeMicroQR(std::string(16, 'a')).empty());
  const auto m = codeMicroQR("12345");
  CHECK(ligne(m[0]).substr(0, 7) == "#######");
  CHECK(ligne(m[0]).substr(8) == "#.#.#");  // synchronisation sur le bord

  // Code 128 B : départ, données, somme de contrôle, arrêt ; 11 modules par caractère, 13 pour l'arrêt
  const auto barres = code128("PROM-034");
  int modules = 0;
  for (int x : barres) modules += x;
  CHECK(barres.size() == 6 * 10 + 7);
  CHECK(modules == 11 * 10 + 13);
  CHECK(std::vector<int>(barres.begin(), barres.begin() + 6) == std::vector<int>{2, 1, 1, 2, 1, 4});  // départ B
  CHECK(code128("é").empty());
  const Polygones bp = barresEnPolygones(barres, ptMm(5, 5), 250'000, nm(5));
  double sombres = 0;
  for (std::size_t i = 0; i < barres.size(); i += 2) sombres += barres[i];
  CHECK(aireMm2(bp) == Approx(sombres * 0.25 * 5));

  int noirs = 0;
  for (const auto& r : q)
    for (bool b : r) noirs += b ? 1 : 0;
  const Polygones pq = modulesEnPolygones(q, ptMm(10, 10), 500'000);
  CHECK(aireMm2(pq) == Approx(noirs * 0.25));
  // Posé en sérigraphie : des dessins pleins de même aire ; en cuivre : des régions « logo » du réseau
  ConceptionElec c = carteNue("Code");
  const auto ids = poserMotifGraphique(c, pq, Couche::SerigraphieDessus);
  CHECK(ids.size() == c.circuit.dessins.size());
  double aireDessins = 0;
  for (const auto& d : c.circuit.dessins) {
    CHECK(d.plein);
    aireDessins += aireMm2(d.points);
  }
  CHECK(aireDessins == Approx(noirs * 0.25).epsilon(0.001));
  CHECK_FALSE(poserMotifGraphique(c, pq, Couche::CuivreDessus, "GND").empty());
  CHECK(c.circuit.regions.front().genre == "logo");
  CHECK(c.circuit.regions.front().reseau == "GND");

  // Logo : un anneau de rayons 8 et 15 pixels, 0,1 mm par pixel
  const int w = 40, h = 40;
  std::vector<std::uint8_t> gris(static_cast<std::size_t>(w * h), 255);
  int sombresPixels = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const double r = std::hypot(x - 19.5, y - 19.5);
      if (r >= 8 && r <= 15) {
        gris[static_cast<std::size_t>(y * w + x)] = 0;
        ++sombresPixels;
      }
    }
  const Polygones pixels = logoDepuisImage(gris, w, h, 128, 0.1, ptMm(0, 0), false);
  CHECK(aireMm2(pixels) == Approx(sombresPixels * 0.01));
  const Polygones vecteurs = logoDepuisImage(gris, w, h, 128, 0.1, ptMm(0, 0), true);
  CHECK(aireMm2(vecteurs) == Approx(std::numbers::pi * (15 * 15 - 8 * 8) * 0.01).epsilon(0.08));
  CHECK_FALSE(dedans(vecteurs, ptMm(1.95, 1.95)));  // le trou au centre
  CHECK(dedans(vecteurs, ptMm(1.95 + 1.15, 1.95)));
}

TEST_CASE("5.15, 5.31 · cotes : alignée, horizontale, rayon, diamètre, angulaire, marque de centre ; géométrie et texte", "[034][L5][5.15][5.31]") {
  ConceptionElec c = carteNue("Cotes");
  const std::string id = poserCote(c, GenreCote::Alignee, {ptMm(10, 10), ptMm(40, 10)});
  REQUIRE(c.circuit.cotes.size() == 1);
  CHECK(c.circuit.cotes[0].uuid == id);
  GeometrieCote g = geometrieCote(c.circuit.cotes[0]);
  CHECK(g.valeur == "30,00 mm");
  CHECK(g.traits.size() >= 7);  // deux attaches, la ligne, deux flèches de deux traits
  CHECK(g.angle == Approx(0));
  Cote h;
  h.genre = GenreCote::Horizontale;
  h.points = {ptMm(10, 10), ptMm(25, 30)};
  CHECK(geometrieCote(h).valeur == "15,00 mm");
  Cote d;
  d.genre = GenreCote::Diametre;
  d.points = {ptMm(20, 20), ptMm(21.6, 20)};
  CHECK(geometrieCote(d).valeur == "⌀3,20 mm");
  Cote a;
  a.genre = GenreCote::Angulaire;
  a.points = {ptMm(0, 0), ptMm(10, 0), ptMm(0, 10)};
  const GeometrieCote ga = geometrieCote(a);
  CHECK(ga.valeur == "90,00°");
  CHECK(ga.traits.size() == 26);
  Cote m;
  m.genre = GenreCote::Centre;
  m.points = {ptMm(5, 5), ptMm(6.6, 5)};
  CHECK(geometrieCote(m).traits.size() == 2);
  CHECK(geometrieCote(m).valeur.empty());
}

TEST_CASE("5.15 · gabarits de carte : HAT Raspberry Pi appliqué, contour et trous de fixation ; 5.32 réglages repris", "[034][L5][5.15][5.32]") {
  REQUIRE(gabaritsCarte().size() >= 6);
  const auto it = std::find_if(gabaritsCarte().begin(), gabaritsCarte().end(), [](const GabaritCarte& g) { return g.nom == "HAT Raspberry Pi"; });
  REQUIRE(it != gabaritsCarte().end());
  ConceptionElec c = nouvelleConception("HAT");
  appliquerGabaritCarte(c, *it);
  const Boite b = boite(c.circuit.contour.libre);
  CHECK(b.largeur() == Approx(nm(65)).margin(1000));
  CHECK(b.hauteur() == Approx(nm(56.5)).margin(1000));
  CHECK(c.circuit.empreintes.size() == 4);
  for (const auto& e : c.circuit.empreintes) CHECK(e.empreinte == "MountingHole_2.75");
  CHECK(c.composants.front().reference == "H1");
  CHECK(erreurs(drc(c)) == 0);

  ConceptionElec s = nouvelleConception("Source");
  s.circuit.regles.isolement = 300'000;
  ClasseReseau pwr;
  pwr.nom = "PWR";
  pwr.largeur = 800'000;
  s.circuit.regles.classes.push_back(pwr);
  s.circuit.empilage.couchesCuivre = 4;
  SectionsReglages o;
  o.regles = o.classes = true;
  reprendreReglages(c, s, o);
  CHECK(c.circuit.regles.isolement == 300'000);
  CHECK(c.circuit.regles.classes.size() == s.circuit.regles.classes.size());
  CHECK(c.circuit.empilage.couchesCuivre == 2);
  o.empilage = true;
  reprendreReglages(c, s, o);
  CHECK(c.circuit.empilage.couchesCuivre == 4);
}

TEST_CASE("5.14 · noms de réseaux à chemin : étiquettes locales homonymes de deux feuilles, ordre des feuilles sans effet", "[034][L5][5.14]") {
  ConceptionElec c = nouvelleConception("Chemins");
  Feuille f2;
  f2.uuid = nouvelUuid();
  f2.nom = "Alimentation";
  c.feuilles.push_back(f2);
  c.feuilles[0].nom = "Logique";
  auto broche = [&](const std::string& sym, const std::string& numero) {
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles)
        if (s.uuid == sym)
          for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
            if (b.broche->numero == numero) return b.p;
    return Pt{};
  };
  auto etiquette = [&](std::size_t f, Pt p, const std::string& t) {
    Etiquette e;
    e.uuid = nouvelUuid();
    e.texte = t;
    e.p = p;
    c.feuilles[f].etiquettes.push_back(e);
  };
  const std::string s1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8), 0);
  const std::string s2 = poserSymbole(c, 1, "R", ptMm(50.8, 50.8), 0);
  etiquette(0, broche(s1, "1"), "CLK");
  etiquette(1, broche(s2, "1"), "CLK");
  etiquette(0, broche(s1, "2"), "DATA");
  auto noms = [&] {
    std::set<std::string> r;
    for (const auto& x : connectivite(c).reseaux)
      if (x.nomme) r.insert(x.nom);
    return r;
  };
  const std::set<std::string> attendus{"/Logique/CLK", "/Alimentation/CLK", "DATA"};
  CHECK(noms() == attendus);
  std::swap(c.feuilles[0], c.feuilles[1]);
  CHECK(noms() == attendus);
  for (const auto& x : connectivite(c).reseaux) CHECK(x.nom.find('#') == std::string::npos);
  // Un ancien nom figé « CLK#2 » reste un alias du réseau que ses broches désignent
  std::string composant2;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == s2) composant2 = s.composant;
  c.nomsReseaux[composant2 + ":1"] = "CLK#2";
  const Connectivite k = connectivite(c);
  CHECK(k.canonique("CLK#2") == "/Alimentation/CLK");
}
