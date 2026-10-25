// Prométhée : essais du lot L19 de la 0.34, second volet (faisceau : schéma de câblage, planche, longueurs, circuits
// continus, fabrication ; dépôt vers le PLM). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/archive.hpp"
#include "promethee/cablage.hpp"
#include "promethee/classeur.hpp"
#include "promethee/electrique.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/pdf_documents.hpp"
#include "promethee/plm.hpp"

using namespace prom;
using namespace prom::elec;
using Catch::Approx;
using ecad::ptMm;
namespace fs = std::filesystem;

namespace {
fs::path dossierEssai(const std::string& nom) {
  const fs::path d = fs::temp_directory_path() / ("promethee-l19b-" + nom);
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
// Fil court et étiquette locale au bout de la broche « numero » d'un symbole posé
void etiqueter(ecad::ConceptionElec& c, const std::string& symbole, const std::string& numero, const std::string& nom) {
  for (auto& f : c.feuilles)
    for (auto& s : f.symboles) {
      if (s.uuid != symbole) continue;
      for (const auto& b : ecad::brochesPlacees(s, *c.symbole(s.symbole)))
        if (b.broche->numero == numero) {
          const ecad::Pt a = b.p, e{b.p.x - 5'080'000, b.p.y};
          f.fils.push_back({ecad::nouvelUuid(), a, e});
          f.etiquettes.push_back({ecad::nouvelUuid(), nom, e, 0, ecad::TypeEtiquette::Locale});
        }
    }
}
// Carte à J3 (quatre broches : CAN_H, CAN_L, RS485_A, RS485_B)
ecad::ConceptionElec carteJ3() {
  ecad::ConceptionElec c = ecad::nouvelleConception("Carte capteur");
  const std::string s = ecad::poserSymbole(c, 0, "Conn_01x04", ptMm(80, 60));
  for (const auto& f : c.feuilles)
    for (const auto& x : f.symboles)
      if (x.uuid == s)
        if (ecad::ComposantElec* k = c.composant(x.composant)) k->reference = "J3";
  const char* reseaux[] = {"CAN_H", "CAN_L", "RS485_A", "RS485_B"};
  for (int i = 0; i < 4; ++i) etiqueter(c, s, std::to_string(i + 1), reseaux[i]);
  return c;
}
// Projet électrique au bornier X1 de cinq bornes
ProjetElectrique projetX1() {
  ProjetElectrique pe = nouveauProjetElectrique("Machine");
  const std::size_t f = ajouterFolio(pe, TypeFolio::Commande, "Bornier");
  const std::string s1 = poserBorne(pe, f, ptMm(100, 50), 0, {});
  std::string bornier;
  for (const auto& x : pe.schema.feuilles[f].symboles)
    if (x.uuid == s1) bornier = x.composant;
  for (int i = 1; i < 5; ++i) poserBorne(pe, f, ptMm(100 + 10.16 * i, 50), 0, bornier);
  return pe;
}
ConnecteurCablage generique(const std::string& repere, int cavites, const std::string& reference = {}) {
  ConnecteurCablage k;
  k.uuid = ecad::nouvelUuid();
  k.repere = repere;
  k.reference = reference;
  for (int i = 0; i < cavites; ++i) k.cavites.push_back({std::to_string(i + 1), {}, {}, {}, {}, 0, 0});
  return k;
}
}  // namespace

TEST_CASE("19.10 · Câble blindé à deux paires de J3 au bornier X1 : quatre fils et un drain, réseaux et contacts", "[L19][cablage]") {
  const auto& b = bibliothequeCablageIntegree();
  // Les huit désignations de l'atelier électrique entrent les premières
  const std::vector<std::string> huit = {"H07RN-F", "H05VV-F", "H05RR-F", "U-1000 R2V", "U-1000 AR2V", "LiYY", "LiYCY (blindé)", "FR-N1X1G1"};
  for (std::size_t i = 0; i < huit.size(); ++i) {
    REQUIRE(b.cables.size() > i);
    CHECK(b.cables[i].designation == huit[i]);
  }
  CHECK(conducteursCable(*b.cable("H07RN-F")).size() == 3);
  CHECK(conducteursCable(*b.cable("H07RN-F")).back().couleur == "GN/YE");
  // Carte et bornier
  const ecad::ConceptionElec carte = carteJ3();
  auto j3 = connecteurDeCarte(carte, 0, "J3");
  REQUIRE(j3);
  REQUIRE(j3->cavites.size() == 4);
  CHECK(j3->carte);
  CHECK(j3->cavites[0].reseau == "CAN_H");
  CHECK(j3->cavites[3].reseau == "RS485_B");
  j3->reference = "43025-0400";
  const ProjetElectrique pe0 = projetX1();
  const ConnecteurCablage x1 = connecteurDuSchema(pe0, "X1");
  REQUIRE(x1.cavites.size() == 5);
  CHECK(x1.cavites[0].numero == "1");
  CHECK(x1.cavites[4].numero == "5");
  Cablage h;
  h.uuid = ecad::nouvelUuid();
  h.nom = "Liaison capteur";
  h.connecteurs = {*j3, x1};
  CHECK(poserCable(h, b, "LiYCY-TP 2x2x0,25", "J3", "X1") == "W1");
  CHECK(affecterContacts(h, b) == 9);  // quatre cavités de J3, cinq de X1 (drain compris)
  const auto liste = listeFils(h, b);
  REQUIRE(liste.size() == 5);
  const char* reseaux[] = {"CAN_H", "CAN_L", "RS485_A", "RS485_B"};
  int drains = 0, signaux = 0;
  for (const auto& l : liste) {
    CHECK(l.cable == "W1");
    if (l.drain) {
      ++drains;
      CHECK(l.de.empty());  // blindage repris d'un seul côté
      CHECK(l.vers == "X1:5");
      CHECK(l.contactVers == "Embout 0,25 mm²");
      continue;
    }
    const int i = signaux++;
    CHECK(l.de == "J3:" + std::to_string(i + 1));
    CHECK(l.vers == "X1:" + std::to_string(i + 1));
    CHECK(l.reseau == reseaux[i]);
    CHECK(l.paire == i / 2 + 1);
    // Le contact qui accepte 0,25 mm² : Micro-Fit 43030-0007 (0,20 à 0,52 mm²) côté carte, embout 0,25 mm² au bornier
    CHECK(l.contactDe == "43030-0007");
    const EntreeContact* c = b.contact(l.contactDe);
    REQUIRE(c);
    CHECK(l.section >= c->sectionMin);
    CHECK(l.section <= c->sectionMax);
    CHECK(l.contactVers == "Embout 0,25 mm²");
    CHECK(l.denudageDe == Approx(3.0));
  }
  CHECK(drains == 1);
  CHECK(signaux == 4);
  CHECK(equipotentielles(h).size() == 5);
  // Gardé dans le projet électrique, relu à l'identique
  ProjetElectrique pe = pe0;
  ecrireCablages(pe, {h});
  const auto relus = cablages(projetElectriqueDepuisJson(versJson(pe)));
  REQUIRE(relus.size() == 1);
  CHECK(relus[0] == h);
  // Changement d'entrée : les câbles posés suivent
  BibliothequeCablage b2 = b;
  for (auto& e : b2.cables)
    if (e.designation == "LiYCY-TP 2x2x0,25") e.conducteurs[0].couleur = "PK";
  Cablage h2 = h;
  CHECK(mettreAJourCables(h2, b2) == 1);
  CHECK(h2.fils[0].couleur == "PK");
  // Piquage : un fil qui s'arrête sur un autre pose une épissure ; les deux deviennent une équipotentielle
  h2.connecteurs.push_back(generique("P1", 2));
  const std::string n = poserFil(h2, 0.25, "OG", "P1", "1", {}, {}, h2.fils[0].numero);
  CHECK(poserEpissures(h2) == 1);
  CHECK(h2.epissures[0].repere == "SP1");
  CHECK(h2.fil(n)->reseau == "CAN_H");
  bool ensemble = false;
  for (const auto& e : equipotentielles(h2))
    if (std::find(e.begin(), e.end(), "P1:1") != e.end() && std::find(e.begin(), e.end(), "J3:1") != e.end()) ensemble = true;
  CHECK(ensemble);
  // Cavalier entre deux bornes de X1, à sa longueur saisie
  const std::string cav = poserCavalier(h2, "X1", "3", "4", 0.5, 0.08);
  CHECK(h2.fil(cav)->cavalier);
  CHECK(h2.fil(cav)->longueur == Approx(0.08));
  // Format de la bibliothèque : version 1 migrée
  const std::string v1 = R"({"format":"promethee-cables","version":1,"cables":[{"designation":"Ancien 2x2","conducteurs":4,"section":0.25,"blinde":true,"paires":2}]})";
  std::vector<std::string> migrations;
  const auto lu = lireBibliothequeCablage(v1, &migrations);
  REQUIRE(lu);
  CHECK(lu->version == kVersionBibliothequeCablage);
  REQUIRE(lu->cables.size() == 1);
  CHECK(lu->cables[0].conducteurs.size() == 5);
  CHECK(lu->cables[0].drain);
  CHECK(lu->cables[0].conducteurs[2].paire == 2);
  CHECK(migrations.size() == 2);
  const auto relue = lireBibliothequeCablage(texteBibliothequeCablage(b));
  REQUIRE(relue);
  CHECK(*relue == b);
  // Folio de câblage : un trait par fil, le câble cerclé et blindé
  const DispositionCablage d = disposerFolioCablage(h, b, 380, 250);
  CHECK(d.boites.size() == 2);
  CHECK(d.traits.size() == 5);
  REQUIRE(d.cables.size() == 1);
  CHECK(d.cables[0].blinde);
  CHECK(typeFolioDepuis("cablage") == TypeFolio::Cablage);
  CHECK(folioDessineDOffice(TypeFolio::Cablage));
}

TEST_CASE("19.11 · Sept fils de 1,6 mm : tronçon de 4,8 mm ; une cote de 500 mm mesure 500 mm sur la planche", "[L19][planche]") {
  CHECK(rapportEmpilement(1) == Approx(1));
  CHECK(rapportEmpilement(3) == Approx(2.1547).epsilon(1e-4));
  CHECK(diametreFaisceau(std::vector<double>(7, 1.6), 1.0) == Approx(4.8));  // empilement hexagonal : trois diamètres
  CHECK(diametreFaisceau(std::vector<double>(7, 1.6), 1.2) == Approx(5.76));
  CHECK(diametreFaisceau({3.0}, 1.0) == Approx(3.0));
  const auto& b = bibliothequeCablageIntegree();
  Cablage h;
  h.nom = "Faisceau d'essai";
  h.connecteurs = {generique("A", 7), generique("B", 7), generique("C", 2)};
  for (int i = 1; i <= 7; ++i) poserFil(h, 0.5, "BU", "A", std::to_string(i), "B", std::to_string(i));
  poserFil(h, 0.5, "RD", "C", "1", "A", "7");
  // Topologie : A — N1 (500 mm) — B (300 mm), branche N1 — C (200 mm)
  h.noeuds = {{"na", "A", "A", 0, 0}, {"nb", "B", "B", 0, 0}, {"nc", "C", "C", 0, 0}, {"n1", "N1", {}, 0, 0}};
  h.troncons = {{"t1", "na", "n1", 500, false}, {"t2", "n1", "nb", 300, false}, {"t3", "n1", "nc", 200, false}};
  CHECK(longueursDepuisPlanche(h) == 8);
  CHECK(h.fil("1")->longueur == Approx(0.9));  // 500 + 300 + deux surlongueurs de 50 mm
  CHECK(h.fil("1")->provenance == ProvenanceLongueur::Planche);
  CHECK(h.fil("8")->longueur == Approx(0.8));
  const auto occ = occupationTroncons(h, b);
  REQUIRE(occ.size() == 3);
  CHECK(occ[0].fils.size() == 8);
  CHECK(occ[1].fils.size() == 7);
  CHECK(occ[1].diametre == Approx(rapportEmpilement(7) * diametreFil(0.5)));
  PlancheDessinee d = dessinerPlanche(h, b);
  // Le tronc (plus long chemin, 800 mm) à l'horizontale ; la branche de part et d'autre
  REQUIRE(d.tronc.size() == 3);
  auto noeud = [&](const std::string& id) { return *std::find_if(d.noeuds.begin(), d.noeuds.end(), [&](const PlancheDessinee::Noeud& n) { return n.uuid == id; }); };
  CHECK(noeud("na").y == Approx(noeud("nb").y));
  CHECK(noeud("n1").y == Approx(noeud("na").y));
  CHECK(std::abs(noeud("nb").x - noeud("na").x) == Approx(800));
  CHECK(std::abs(noeud("nc").y - noeud("n1").y) == Approx(200));
  const std::string svg = svgPlanche(h, b, d, 1.0);
  std::smatch m;
  REQUIRE(std::regex_search(svg, m, std::regex("width=\"([0-9.]+)mm\" height=\"([0-9.]+)mm\" viewBox=\"0 0 ([0-9.]+) ([0-9.]+)\"")));
  CHECK(std::stod(m[1]) == Approx(std::stod(m[3])));  // une unité du dessin = un millimètre imprimé
  REQUIRE(std::regex_search(svg, m, std::regex("class=\"cote\" data-longueur=\"500\" x1=\"([-0-9.]+)\" y1=\"([-0-9.]+)\" x2=\"([-0-9.]+)\" y2=\"([-0-9.]+)\"")));
  CHECK(std::hypot(std::stod(m[3]) - std::stod(m[1]), std::stod(m[4]) - std::stod(m[2])) == Approx(500));
  CHECK(svg.find(">500<") != std::string::npos);
  // Échelle 1:2 : même dessin, moitié de taille
  const std::string svg2 = svgPlanche(h, b, d, 0.5);
  REQUIRE(std::regex_search(svg2, m, std::regex("width=\"([0-9.]+)mm\"")));
  CHECK(std::stod(m[1]) == Approx(d.largeur / 2).epsilon(1e-3));
  // Pages assemblées : A4 à l'italienne, autant de pages que de cases
  const PagesPlanche pages = pagesPlanche(h, b, d, "A4", 10);
  CHECK(pages.colonnes == static_cast<int>(std::ceil((d.largeur - 10) / 267)));
  const ecad::LecturePdf lp = ecad::lirePdf(pages.pdf);
  REQUIRE(lp.ok);
  CHECK(lp.pages == pages.colonnes * pages.lignes);
  CHECK(lp.taillesPages[0].first == Approx(297).margin(0.1));
  CHECK(lp.taillesPages[0].second == Approx(210).margin(0.1));
  // Coupure : la longueur dessinée n'est plus la réelle, la cote et les fils gardent la réelle
  h.troncons[1].longueur = 3000;
  h.troncons[1].coupure = true;
  d = dessinerPlanche(h, b);
  CHECK(std::abs(noeud("nb").x - noeud("n1").x) < 3000);
  CHECK(svgPlanche(h, b, d).find(">3000 (coupure)<") != std::string::npos);  // cote de plan : millimètres sans séparateur
  longueursDepuisPlanche(h);
  CHECK(h.fil("1")->longueur == Approx(3.6));
  // Longueur forcée, chemin imposé
  h.fil("2")->longueurForcee = 1.25;
  h.fil("3")->chemin = {"t1"};
  longueursDepuisPlanche(h);
  CHECK(h.fil("2")->longueur == Approx(1.25));
  CHECK(h.fil("2")->provenance == ProvenanceLongueur::Saisie);
  CHECK(h.fil("3")->longueur == Approx(0.6));
  // Protections proposées dans leur plage ; ruban compté en spirale
  const auto gaines = protectionsPour(b, 4.8, "gaine");
  REQUIRE_FALSE(gaines.empty());
  CHECK(gaines.front()->diametreMin <= 4.8);
  CHECK(gaines.front()->diametreMax >= 4.8);
  CHECK(longueurRuban(10, 100, 19, 0.5) == Approx(100 / 9.5 * std::hypot(3.14159265358979 * 10, 9.5)).epsilon(1e-6));
  h.protections.push_back({"t1", "Gaine tressée 3-7", 0, 0, 0, {}});
  h.protections.push_back({"t1", "Étiquette 25 × 10", 100, 125, 0, "W1\nCAPTEUR"});
  const std::string avecProtections = svgPlanche(h, b, dessinerPlanche(h, b));
  CHECK(avecProtections.find("class=\"protection\"") != std::string::npos);
  CHECK(avecProtections.find(">CAPTEUR<") != std::string::npos);
  const std::string coupe = svgCoupe(h, b, "t2");
  CHECK(std::count(coupe.begin(), coupe.end(), '\n') > 7);
  CHECK(coupe.find("class=\"enveloppe\"") != std::string::npos);
}

TEST_CASE("19.12 · Deux fils de 0,75 mm² routés en 3D sur 4 m, 6 A sous 12 V : chute de 1,2 V (10 %) signalée", "[L19][continu]") {
  BibliothequeCablage b = bibliothequeCablageIntegree();
  EntreeCable e;
  e.designation = "Essai 2x0,75";
  e.section = 0.75;
  e.resistanceLineique = 25;
  e.conducteurs = {{"1", "BN", 0, false}, {"2", "BU", 0, false}};
  b.cables.push_back(e);
  Cablage h;
  h.connecteurs = {generique("S", 2), generique("L", 2)};
  REQUIRE(poserCable(h, b, "Essai 2x0,75", "S", "L") == "W1");
  // Route 3D par points d'attache : droite de 4 m ; un coude au rayon de 50 mm raccourcit la courbe
  const Route3D droite = routerEn3D({{0, 0, 0}, {1500, 0, 0}, {4000, 0, 0}}, 50);
  CHECK(droite.longueur == Approx(4000));
  const Route3D coude = routerEn3D({{0, 0, 0}, {2000, 0, 0}, {2000, 2000, 0}}, 50);
  CHECK(coude.longueur == Approx(4000 - 100 + 50 * 3.14159265358979 / 2));
  CHECK(coude.alertes.empty());
  CHECK(routerEn3D({{0, 0, 0}, {30, 0, 0}, {30, 30, 0}}, 50).alertes.size() == 1);  // coude trop serré
  CHECK(longueurDepuis3D(h, "W1", droite.longueur) == 2);
  for (const auto& l : listeFils(h, b)) {
    CHECK(l.longueur == Approx(4.0));
    CHECK(l.provenance == "3D");
  }
  CircuitContinu c;
  c.tension = 12;
  c.aller = {"1"};
  c.retour = {"2"};
  c.charge = {"moteur", 6, 0, 0};
  c.chuteMax = 5;
  const BilanContinu r = resoudreContinu(h, b, c);
  CHECK(r.courant == Approx(6));
  CHECK(r.chute == Approx(1.2));
  CHECK(r.pourcent == Approx(10));
  CHECK(r.tensionCharge == Approx(10.8));
  CHECK(r.depasse);
  REQUIRE_FALSE(r.alertes.empty());
  CHECK(r.alertes[0].find("1,2 V (10 %) au-delà de 5 %") != std::string::npos);
  const auto defauts = verifierFaisceau(h, b, {c});
  CHECK(std::any_of(defauts.begin(), defauts.end(), [](const DefautFaisceau& d) { return d.genre == "chute de tension"; }));
  // Charge résistive, puis à puissance constante (itérations)
  CircuitContinu cr = c;
  cr.charge = {"résistance", 0, 2, 0};
  CHECK(resoudreContinu(h, b, cr).courant == Approx(12 / 2.2));
  CircuitContinu cp = c;
  cp.charge = {"puissance", 0, 0, 72};
  const BilanContinu rp = resoudreContinu(h, b, cp);
  CHECK(rp.courant == Approx((12 - std::sqrt(144 - 57.6)) / 0.4).epsilon(1e-6));
  CHECK(rp.iterations > 1);
  // Fusible trop fort pour la section
  CircuitContinu cf = c;
  cf.fusible = 20;
  const BilanContinu rf = resoudreContinu(h, b, cf);
  CHECK(std::any_of(rf.alertes.begin(), rf.alertes.end(), [](const std::string& a) { return a.find("fusible de 20 A") != std::string::npos; }));
  // Analyse nodale : diviseur de tension
  const auto v = analyseNodale(2, {{1, 2, 1000, 0}, {2, 0, 3000, 0}}, 1, 8);
  REQUIRE(v.size() == 3);
  CHECK(v[2] == Approx(6));
  // Feuille de topologie : plus court chemin de Dijkstra, passage imposé
  Topologie t;
  t.espaces = {"armoire", "châssis"};
  t.points = {{"A", "armoire", 0, 0}, {"P", "armoire", 1000, 0}, {"C", "châssis", 1000, 1000}, {"D", "châssis", 0, 1200}};
  t.passages = {{"A", "P", 0, true}, {"P", "C", 0, true}, {"A", "D", 0, true}, {"D", "C", 0, true}};
  const RouteTopologie r1 = routerTopologie(t, "A", "C");
  REQUIRE(r1.trouvee);
  CHECK(r1.longueur == Approx(2000));
  CHECK(r1.points == std::vector<std::string>{"A", "P", "C"});
  const RouteTopologie r2 = routerTopologie(t, "A", "C", {"D"});
  REQUIRE(r2.trouvee);
  CHECK(r2.points == std::vector<std::string>{"A", "D", "C"});
  CHECK(r2.longueur == Approx(1200 + std::hypot(1000, 200)));
  t.passages[2].ouvert = false;  // A–D fermé : D se rejoint encore par C
  CHECK(routerTopologie(t, "A", "C", {"D"}).points == std::vector<std::string>{"A", "P", "C", "D", "C"});
  t.passages[3].ouvert = false;  // D isolé
  CHECK_FALSE(routerTopologie(t, "A", "C", {"D"}).trouvee);
  // La longueur de la route remplace « à mesurer » dans la liste des câbles et sur le câble du faisceau
  ProjetElectrique pe = nouveauProjetElectrique("Véhicule");
  Cable w;
  w.uuid = ecad::nouvelUuid();
  w.reference = "W1";
  pe.cables.push_back(w);
  ecrireCablages(pe, {h});
  REQUIRE(longueurDepuisTopologie(pe, "W1", r1, 100));
  CHECK(pe.cables[0].longueur == Approx(2.2));
  const auto apres = cablages(pe);
  CHECK(apres[0].cables[0].provenance == ProvenanceLongueur::Topologie);
  CHECK(apres[0].fils[0].longueur == Approx(2.2));
}

TEST_CASE("19.13 · Faisceau d'essai de 24 fils et 48 sertissages : main-d'œuvre égale à la somme des temps unitaires", "[L19][devis]") {
  const auto& b = bibliothequeCablageIntegree();
  Cablage h;
  h.nom = "Essai 24 fils";
  for (const char* k : {"A1", "A2", "A3", "B1", "B2", "B3"}) h.connecteurs.push_back(generique(k, 8, "43025-0800"));
  for (int c = 1; c <= 3; ++c)
    for (int i = 1; i <= 8; ++i) poserFil(h, 0.5, i % 2 ? "BU" : "WH", "A" + std::to_string(c), std::to_string(i), "B" + std::to_string(c), std::to_string(i));
  REQUIRE(h.fils.size() == 24);
  CHECK(affecterContacts(h, b) == 48);
  topologieParDefaut(h);
  for (auto& t : h.troncons) t.longueur = 400;
  longueursDepuisPlanche(h);
  h.protections.push_back({h.troncons[0].uuid, "Gaine tressée 3-7", 0, 0, 0, {}});
  h.protections.push_back({h.troncons[1].uuid, "Gaine tressée 3-7", 0, 0, 0, {}});
  h.protections.push_back({h.troncons[2].uuid, "Étiquette 25 × 10", 50, 75, 0, "ESSAI"});
  TempsUnitaires t;
  t.coupe = 12;
  t.sertissage = 20;
  t.gaine = 45;
  t.controle = 6;
  t.etiquette = 15;
  t.tauxHoraire = 55;
  const DevisFaisceau d = devisFaisceau(h, b, t);
  CHECK(d.fils == 24);
  CHECK(d.sertissages == 48);
  CHECK(d.protections == 2);
  CHECK(d.etiquettes == 1);
  CHECK(d.connexions == 24);
  const double main = 24 * 12 + 48 * 20 + 2 * 45 + 1 * 15 + 24 * 6;  // à la main : 1 497 s
  CHECK(d.secondes == Approx(main));
  CHECK(d.mainOeuvre == Approx(main / 3600 * 55));
  CHECK(d.materiel > 0);
  CHECK(d.total == Approx(d.materiel + d.mainOeuvre));
  CHECK(csvDevis(d).find("main-d'œuvre") != std::string::npos);
  CHECK(ecad::lirePdf(pdfDevis(d, "Essai")).ok);
  const auto lt = lireTempsUnitaires(R"({"coupe":10,"tauxHoraire":60})");
  REQUIRE(lt);
  CHECK(lt->coupe == 10);
  CHECK(lt->sertissage == 20);
  // Nomenclature « selon besoin » à bulles
  const auto nom = nomenclatureCablage(h, b);
  REQUIRE_FALSE(nom.empty());
  CHECK(nom.front().bulle == 1);
  CHECK(nom.front().selonBesoin);
  const auto contacts = std::find_if(nom.begin(), nom.end(), [](const LigneNomenclatureCablage& l) { return l.reference == "43030-0007"; });
  REQUIRE(contacts != nom.end());
  CHECK(contacts->quantite == 48);
  // Classeur : quatre feuilles
  const std::string x = xlsxFaisceau(h, b);
  const auto zip = lireZip(x);
  int feuilles = 0;
  for (const auto& [nomFichier, contenu] : zip) feuilles += nomFichier.rfind("xl/worksheets/sheet", 0) == 0;
  CHECK(feuilles == 4);
  // Contrôles : faisceau propre, puis défauts semés
  CHECK(verifierFaisceau(h, b).empty());
  Cablage k = h;
  k.fils[0].vers.clear();
  k.fils[1].section = 1.5;  // le contact 43030-0007 ne la prend pas
  k.protections[0].reference = "Gaine tressée 6-12";  // faisceau de 4,6 mm : hors de sa plage
  k.connecteurs.push_back(generique("X9", 4, "DT06-4S"));
  poserFil(k, 0.75, "BK", "X9", "1", "X9", "2");
  affecterContacts(k, b);
  k.connecteur("X9")->cavites[2].bouchon.clear();
  std::set<std::string> genres;
  for (const auto& df : verifierFaisceau(k, b)) genres.insert(df.genre);
  CHECK(genres.count("fil sans extrémité"));
  CHECK(genres.count("contact hors plage"));
  CHECK(genres.count("étanchéité"));
  CHECK(genres.count("protection hors plage"));
  // Machines et testeurs : gabarits modifiables
  const auto& g = gabaritsMachines();
  REQUIRE(g.size() >= 5);
  const std::string komax = fichierMachine(h, b, g[0]);
  CHECK(std::count(komax.begin(), komax.end(), '\n') == 25);
  CHECK(komax.find("1;900;0,5;BU;3;3;43030-0007;43030-0007;1") != std::string::npos);
  const std::string cirris = fichierMachine(h, b, g[3]);
  CHECK(cirris.find("CONNECT A1-1 B1-1 ; fil 1") != std::string::npos);
  const auto relu = lireGabaritMachine(texteGabaritMachine(g[4]));
  REQUIRE(relu);
  CHECK(*relu == g[4]);
  // Plan de fabrication : planche, fils, connexions, nomenclature
  const ecad::LecturePdf plan = ecad::lirePdf(pdfPlanFaisceau(h, b, "Banc d'essai", "B"));
  REQUIRE(plan.ok);
  CHECK(plan.pages >= 4);
  CHECK(plan.taillesPages[0].first == Approx(420).margin(0.1));
}

TEST_CASE("19.14 · Dépôt « Vers le PLM » : nomenclature commune, structure PLMXML, manifeste relu", "[L19][plm]") {
  Projet p = projetVide();
  p.nom = "Capteur de pression";
  p.electronique.push_back(ecad::exempleElectronique());
  ProjetElectrique pe = projetX1();
  Cablage h;
  h.nom = "Liaison capteur";
  h.connecteurs = {*connecteurDeCarte(carteJ3(), 0, "J3"), connecteurDuSchema(pe, "X1")};
  poserCable(h, bibliothequeCablageIntegree(), "LiYCY-TP 2x2x0,25", "J3", "X1");
  ecrireCablages(pe, {h});
  p.electrique.push_back(pe);
  DepotPlm d = preparerDepotPlm(p, "A", "2026-10-25", "Evan");
  CHECK(d.numero == "PRM-P-Capteur-de-pression");
  REQUIRE(d.articles.size() > 3);
  CHECK(d.articles[0].genre == "produit");
  CHECK(std::any_of(d.articles.begin(), d.articles.end(), [](const ArticlePlm& a) { return a.genre == "carte"; }));
  CHECK(std::any_of(d.articles.begin(), d.articles.end(), [](const ArticlePlm& a) { return a.genre == "faisceau"; }));
  const std::string csv = csvNomenclatureCommune(d);
  CHECK(csv.find("PRM-P-Capteur-de-pression;PRM-C1;A;") != std::string::npos);
  const std::string x = plmxml(d);
  CHECK(x.find("<PLMXML xmlns=\"http://www.plmxml.org/Schemas/PLMXMLSchema\"") != std::string::npos);
  CHECK(x.find("occurrenceRefs=") != std::string::npos);
  d.fichiers.push_back({"modeles/carte.step", "ISO-10303-21;\nHEADER;\nENDSEC;\nEND-ISO-10303-21;\n"});
  const fs::path equipe = dossierEssai("plm");
  std::string erreur;
  const std::string dossier = deposerPourPlm(utf8(equipe), d, &erreur);
  REQUIRE_FALSE(dossier.empty());
  CHECK(fs::exists(equipe / "Vers le PLM" / "PRM-P-Capteur-de-pression-A" / "manifeste.json"));
  const LectureDepotPlm l = lireDepotPlm(dossier);
  CHECK(l.ok);
  CHECK(l.revision == "A");
  CHECK(l.articles == static_cast<int>(d.articles.size()));
  CHECK(l.fichiers.size() == 3);
  ecrireFichierAtomique(cheminFichier(dossier) / "modeles" / "carte.step", "altéré");
  const LectureDepotPlm l2 = lireDepotPlm(dossier);
  CHECK_FALSE(l2.ok);
  CHECK(l2.sommesFausses == std::vector<std::string>{"modeles/carte.step"});
  // Retour du PLM, écrit par le programme d'accès en ligne
  const std::vector<EtatPlm> etats = {{d.numero, "B", "restitué", "", "2026-10-25", "révision B"}};
  REQUIRE(ecrireEtatsPlm(utf8(equipe), etats));
  CHECK(lireEtatsPlm(utf8(equipe)) == etats);
}
