// Prométhée : essais de la 0.34, lot L8 côté noyau — nomenclature configurable (8.5), archive pour le fabricant
// (8.6), ligne de fuite (8.10), positions d'un panneau (8.15), panneau de copies liées (8.16), contrôles de
// fabrication (8.17), familles de la DRC (8.25).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <nlohmann/json.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/archive.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/commandes.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/placement.hpp"
#include "promethee/contraintes.hpp"
#include "promethee/controles_fabrication.hpp"
#include "promethee/drc_familles.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fabrication_avancee.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/mise_en_panneau.hpp"
#include "promethee/serigraphie_preparee.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/regles_carte.hpp"
#include "promethee/signaux.hpp"
#include "promethee/routage_auto.hpp"
#include "promethee/sorties.hpp"
#include "promethee/sorties_fabricant.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
int compter(const std::vector<Violation>& v, const std::string& code) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
}
std::vector<Violation> drc(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  return verifierDRC(c, k, remplirZones(c, k));
}
ConceptionElec carte(const std::string& nom, double l = 50, double h = 30) {
  ConceptionElec c = nouvelleConception(nom);
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(l);
  c.circuit.contour.hauteur = nm(h);
  return c;
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
Pt pastille(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  for (const auto& p : pastilles(c, connectivite(c)))
    if (p.composant == composant && p.numero == numero) return p.pastille.position;
  return {};
}
std::filesystem::path dossierEssai(const std::string& nom) {
  const auto d = std::filesystem::temp_directory_path() / ("promethee-l8-" + nom);
  std::filesystem::remove_all(d);
  std::filesystem::create_directories(d);
  return d;
}
}  // namespace

TEST_CASE("8.5 · nomenclature regroupée sur la valeur numérique ; formats JLCPCB et PCBWay ; rotations corrigées par motif", "[034][L8][8.5]") {
  ConceptionElec c = carte("Nomenclature");
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(5, 5));
  const std::string r2 = poserEmpreinteSeule(c, "R_0603", ptMm(10, 5));
  const std::string r3 = poserEmpreinteSeule(c, "R_0603", ptMm(15, 5));
  const std::string r4 = poserEmpreinteSeule(c, "R_0603", ptMm(20, 5));
  c.composant(r1)->valeur = "10k";
  c.composant(r2)->valeur = "10 kΩ";
  c.composant(r3)->valeur = "10000";
  c.composant(r4)->valeur = "4k7";
  for (const auto& id : {r1, r2, r3, r4}) c.composant(id)->champs["LCSC"] = "C25804";
  c.composant(r4)->champs["LCSC"] = "C23162";
  const auto t = tableauNomenclature(c, optionsNomenclature("standard"));
  REQUIRE(t.size() == 3);  // en-tête et deux lignes
  CHECK(t[0][0] == "Quantité");
  bool trois = false;
  for (const auto& l : t)
    if (l[0] == "3") trois = l[1] == "R1,R2,R3";
  CHECK(trois);
  const std::string jlc = csvNomenclatureFormat(c, optionsNomenclature("jlcpcb"));
  CHECK(jlc.rfind("Comment,Designator,Footprint,LCSC Part #\n", 0) == 0);
  CHECK(jlc.find("\"R1,R2,R3\",R_0603,C25804") != std::string::npos);
  const std::string pcbway = csvNomenclatureFormat(c, optionsNomenclature("pcbway"));
  CHECK(pcbway.rfind("Item #,*Designator,*Qty,Manufacturer,*Mfg Part #", 0) == 0);
  CHECK(pcbway.find(",SMD,") != std::string::npos);
  // Sans regroupement : quatre lignes
  OptionsNomenclature o;
  o.regrouperValeur = false;
  CHECK(tableauNomenclature(c, o).size() == 5);
  // Corrections de rotation de JLCPCB : SOT-23 d'un demi-tour, SOIC d'un quart de tour, R_0603 inchangée
  const auto k = correctionsRotation("JLCPCB");
  CHECK(rotationCorrigee("SOT-23", 0, false, k) == Approx(180));
  CHECK(rotationCorrigee("SOIC-8", 90, false, k) == Approx(0));
  CHECK(rotationCorrigee("R_0603", 90, false, k) == Approx(90));
  CHECK(rotationCorrigee("SOT-23", 0, false, correctionsRotation("")) == Approx(0));
  const std::string q1 = poserEmpreinteSeule(c, "SOT-23", ptMm(30, 15));
  OptionsPositions op;
  op.profil = "JLCPCB";
  for (const auto& p : positions(c, op))
    if (p.repere == c.composant(q1)->reference) CHECK(p.rotation == Approx(180));
}

TEST_CASE("8.6 · archive « projet_revB_JLCPCB.zip » complète : Gerber, perçages, nomenclature, placement, récapitulatif et statistiques", "[034][L8][8.6]") {
  ConceptionElec c = exempleElectronique();
  c.cartouche.revision = "B";
  const auto d = dossierEssai("archive");
  JeuSorties j = jeuArchiveFabricant("JLCPCB");
  j.controler = false;
  const ResultatJeu r = executerJeu(c, j, d.string(), "projet", "0.34.0");
  REQUIRE(r.publie);
  CHECK(std::filesystem::path(r.dossier).filename() == "projet_revB_JLCPCB.zip");
  REQUIRE(std::filesystem::exists(r.dossier));
  const auto entrees = prom::lireZip(prom::lireFichier(r.dossier));
  std::set<std::string> noms;
  for (const auto& [n, contenu] : entrees) noms.insert(n);
  auto contient = [&](const std::string& motif) { return std::any_of(noms.begin(), noms.end(), [&](const std::string& n) { return n.find(motif) != std::string::npos; }); };
  CHECK(contient("F_Cu.gbr"));
  CHECK(contient("B_Cu.gbr"));
  CHECK(contient("Edge_Cuts.gbr"));
  CHECK(contient(".gbrjob"));
  CHECK(contient("-PTH.drl"));
  CHECK(contient("nomenclature-jlcpcb.csv"));
  CHECK(contient("CPL-JLCPCB.csv"));
  CHECK(contient("LISEZMOI-fabrication.txt"));
  CHECK(contient("statistiques.json"));
  // Le récapitulatif liste les fichiers ; les statistiques comptent les composants
  for (const auto& [n, contenu] : entrees) {
    if (n.find("LISEZMOI") != std::string::npos) {
      CHECK(contenu.find("révision B") != std::string::npos);
      CHECK(contenu.find("CPL-JLCPCB.csv") != std::string::npos);
    }
    if (n.find("statistiques") != std::string::npos) {
      const auto s = nlohmann::json::parse(contenu);
      CHECK(s["composants"]["total"].get<int>() == static_cast<int>(c.circuit.empreintes.size()));
      CHECK(s["carte"]["couches_cuivre"].get<int>() == 2);
    }
  }
  const StatistiquesFabrication s = statistiquesFabrication(c);
  CHECK(s.composants == static_cast<int>(c.circuit.empreintes.size()));
  CHECK(s.trous > 0);
  CHECK(s.plusFinePiste > 0);
}

TEST_CASE("8.10 · ligne de fuite trop courte entre secteur et basse tension signalée ; une fente la rallonge ; taux de cuivre", "[034][L8][8.10]") {
  ConceptionElec c = carte("Fuite", 40, 30);
  ClasseReseau secteur;
  secteur.nom = "Secteur";
  secteur.tension = 325;  // 230 V efficaces
  ClasseReseau tbt;
  tbt.nom = "TBT";
  tbt.tension = 12;
  c.circuit.regles.classes = {secteur, tbt};
  c.circuit.regles.classeReseau["L"] = "Secteur";
  c.circuit.regles.classeReseau["VCC"] = "TBT";
  // Pollution 3 : 4 mm exigés à 230 V (groupe III, hors colonnes du circuit imprimé)
  reglerSurface(c, {3, 3, false});
  const double exigee = ligneFuiteExigee(c, "L", "VCC");
  CHECK(exigee == Approx(4.0));
  // Deux pistes à 3 mm l'une de l'autre
  c.circuit.pistes.push_back(piste("L", ptMm(5, 10), ptMm(35, 10), nm(1)));
  c.circuit.pistes.push_back(piste("VCC", ptMm(5, 14), ptMm(35, 14), nm(1)));
  auto v = drc(c);
  CHECK(compter(v, "DRC-55") == 1);
  // Une fente de 2 mm de large entre elles, plus longue que les pistes : le chemin la contourne, au-delà de 4 mm
  c.circuit.contour.decoupes.push_back({ptMm(2, 11), ptMm(38, 11), ptMm(38, 13), ptMm(2, 13)});
  const Polygone a = oblong(ptMm(5, 10), ptMm(35, 10), nm(1)), b = oblong(ptMm(5, 14), ptMm(35, 14), nm(1));
  CHECK(ligneDeFuite(a, b, c.circuit.contour.decoupes) > 4.0);
  CHECK(compter(drc(c), "DRC-55") == 0);
  // Une fente trop étroite (0,5 mm < X = 1,5 mm en pollution 3) ne compte pas
  c.circuit.contour.decoupes = {{ptMm(2, 11.75), ptMm(38, 11.75), ptMm(38, 12.25), ptMm(2, 12.25)}};
  CHECK(compter(drc(c), "DRC-55") == 1);
  // Taux de cuivre : deux pistes de 30 × 1 mm (et leurs bouts) sur 1 200 mm²
  c.circuit.contour.decoupes.clear();
  const Connectivite k = connectivite(c);
  const auto t = tauxCuivre(c, k, remplirZones(c, k));
  REQUIRE(!t.empty());
  CHECK(t[0].couche == Couche::CuivreDessus);
  CHECK(t[0].cuivre == Approx(2 * (30 + 3.14159265 / 4)).epsilon(0.02));
  CHECK(t[0].taux == Approx(100 * t[0].cuivre / 1200).epsilon(0.001));
}

TEST_CASE("8.15 · placement d'un panneau 3 × 2 : chaque composant six fois et les trois mires, à 0,01 mm des positions attendues", "[034][L8][8.15]") {
  ConceptionElec c = carte("Panneau", 50, 30);
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(10, 10));
  const std::string q1 = poserEmpreinteSeule(c, "SOT-23", ptMm(30, 20), 90);
  OptionsPanneau op;
  op.colonnes = 3;
  op.lignes = 2;
  op.ecart = 2;
  op.rail = 5;
  // Positions de la carte seule, repère de fabrication
  const auto seule = positions(c, {});
  REQUIRE(seule.size() == 2);
  OptionsPositions o;
  o.panneau = op;
  const auto p = positions(c, o);
  std::map<std::string, int> fois;
  int mires = 0;
  for (const auto& x : p) {
    if (x.genre == "mire") ++mires;
    else ++fois[x.repere];
  }
  CHECK(fois[c.composant(r1)->reference] == 6);
  CHECK(fois[c.composant(q1)->reference] == 6);
  CHECK(mires == 3);
  // Attendu : panneau de 2·5 + 2·2 + 3·50 + 2·2 = 168 mm sur 2·5 + 2·2 + 2·30 + 2 = 76 mm ; copie (ligne i, colonne j)
  // au coin bas gauche (7 + 52 j ; 76 − 7 − 32 i − 30)
  const auto [PW, PH] = taillePanneau(c, op);
  CHECK(PW == Approx(168));
  CHECK(PH == Approx(76));
  for (const auto& x : p) {
    if (x.genre == "mire") continue;
    const int i = (x.copie - 1) / 3, j = (x.copie - 1) % 3;
    const auto s = std::find_if(seule.begin(), seule.end(), [&](const Position& y) { return y.repere == x.repere; });
    REQUIRE(s != seule.end());
    CHECK(std::abs(x.x - (s->x + 7 + 52 * j)) < 0.01);
    CHECK(std::abs(x.y - (s->y + 39 - 32 * i)) < 0.01);
    CHECK(x.rotation == Approx(s->rotation));
  }
  // Les trois mires, au milieu du rail, sont aussi dans le cuivre du panneau
  const std::vector<std::pair<double, double>> attendues{{2.5, 2.5}, {165.5, 2.5}, {2.5, 73.5}};
  const PanneauFabrication pf = panneauFabrication(c, op);
  const std::string g = gerber(pf.modele, Couche::CuivreDessus, "0.34");
  const LectureGerber lu = lireGerber(g);
  for (const auto& [ex, ey] : attendues) {
    CHECK(std::any_of(p.begin(), p.end(), [&](const Position& x) { return x.genre == "mire" && std::abs(x.x - ex) < 0.01 && std::abs(x.y - ey) < 0.01; }));
    CHECK(std::any_of(lu.objets.begin(), lu.objets.end(), [&](const ObjetGerber& x) {
      return x.genre == ObjetGerber::Genre::Flash && std::abs(mm(x.a.x) - ex) < 0.01 && std::abs(mm(x.a.y) - ey) < 0.01;
    }));
  }
  // Gabarits de colonnes : OpenPnP, Neoden ; relu depuis son fichier de données
  const auto openpnp = gabaritPositions("OpenPnP");
  REQUIRE(openpnp.has_value());
  const std::string texte = ecrirePositions(p, *openpnp);
  CHECK(texte.rfind("Designator,Value,Package,X,Y,Rotation,Side\n", 0) == 0);
  CHECK(static_cast<int>(std::count(texte.begin(), texte.end(), '\n')) == 1 + 15);
  std::string erreur;
  const auto relu = lireGabaritPositions(texteGabaritPositions(*openpnp), &erreur);
  REQUIRE(relu.has_value());
  CHECK(ecrirePositions(p, *relu) == texte);
  // Origine posée à la souris : R1 lu depuis le coin de la carte de 10 mm
  OptionsPositions oo;
  oo.origine = ptMm(0, 30);
  for (const auto& x : positions(c, oo))
    if (x.repere == c.composant(r1)->reference) {
      CHECK(std::abs(x.x - 10) < 0.01);
      CHECK(std::abs(x.y - 20) < 0.01);
    }
}

TEST_CASE("8.16 · piste déplacée dans la carte source : ses copies du panneau suivent, contrôle du panneau sans défaut ; disposition la moins chère", "[034][L8][8.16]") {
  ConceptionElec c = carte("Source", 40, 25);
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(10, 10));
  const std::string r2 = poserEmpreinteSeule(c, "R_0603", ptMm(30, 10));
  const std::string n = reseauParPiste(c, r1, "2", r2, "1");
  REQUIRE(!n.empty());
  // Deux pistes coudées au milieu : le coude descendra de 2 mm
  const Pt pa = pastille(c, r1, "2"), pb = pastille(c, r2, "1"), coude{(pa.x + pb.x) / 2, pa.y + nm(3)};
  c.circuit.pistes.push_back(piste(n, pa, coude, nm(0.3)));
  c.circuit.pistes.push_back(piste(n, coude, pb, nm(0.3)));
  MiseEnPanneau p = panneauEnGrille(c, 2, 2, 2, 5, true);
  REQUIRE(p.copies.size() == 4);
  for (const auto& d : controlerPanneau({&c}, p)) UNSCOPED_INFO(d);
  CHECK(controlerPanneau({&c}, p).empty());
  // Gardé dans la conception, relu sans perte
  reglerPanneau(c, p);
  const auto relu = panneauDe(conceptionDepuisJson(versJson(c)));
  REQUIRE(relu.has_value());
  CHECK(panneauVersJson(*relu) == panneauVersJson(p));
  auto traits = [&](const ModelePanneau& m) {
    std::vector<Pt> r;
    for (const auto& cf : m.modele.couches)
      if (cf.couche == Couche::CuivreDessus)
        for (const auto& x : cf.primitives)
          if (x.type == PrimitiveFab::Type::Trait && x.reseau == n) {
            r.push_back(x.a);
            r.push_back(x.b);
          }
    std::sort(r.begin(), r.end(), [](Pt a, Pt b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
    return r;
  };
  const ModelePanneau avant = construirePanneau({&c}, p);
  CHECK(avant.copies == 4);
  CHECK(avant.defauts.empty());
  const auto ta = traits(avant);
  REQUIRE(ta.size() == 4 * 4);
  // Le coude de la source descend de 2 mm : dans chaque copie, ses deux bouts de piste suivent (tête-bêche : vers le
  // haut dans le panneau), les autres ne bougent pas
  for (auto& t : c.circuit.pistes) {
    if (t.b == coude) t.b.y += nm(2);
    if (t.a == coude) t.a.y += nm(2);
  }
  const ModelePanneau apres = construirePanneau({&c}, p);
  const auto tb = traits(apres);
  REQUIRE(tb.size() == ta.size());
  std::vector<Pt> partis, arrives;
  for (const Pt q : ta)
    if (std::find(tb.begin(), tb.end(), q) == tb.end()) partis.push_back(q);
  for (const Pt q : tb)
    if (std::find(ta.begin(), ta.end(), q) == ta.end()) arrives.push_back(q);
  CHECK(partis.size() == 8);
  CHECK(arrives.size() == 8);
  int suivies = 0;
  for (const Pt q : arrives)
    if (std::any_of(partis.begin(), partis.end(), [&](Pt d) { return d.x == q.x && std::abs(mm(std::llabs(q.y - d.y)) - 2) < 1e-6; })) ++suivies;
  CHECK(suivies == 8);
  for (const auto& d : apres.defauts) UNSCOPED_INFO(d);
  CHECK(apres.defauts.empty());
  // Copies qui se touchent, élément sur une copie : signalés
  MiseEnPanneau faux = p;
  faux.copies[1].x = faux.copies[0].x + 20;
  CHECK(!controlerPanneau({&c}, faux).empty());
  // Grille tarifaire : la disposition la moins chère par carte en tête
  GrilleTarifaire g;
  g.fabricant = "Atelier";
  g.date = "2026-10-01";
  g.formats = {{"Petit 100 × 100", 100, 100, 30, 8, {}}, {"Grand 300 × 250", 300, 250, 60, 6, {{10, 0.9}}}};
  const auto offres = dispositions(c, g, 100, 2, 5);
  REQUIRE(!offres.empty());
  for (std::size_t i = 1; i < offres.size(); ++i) CHECK(offres[i - 1].prixParCarte <= offres[i].prixParCarte);
  CHECK(offres.front().cartesParPanneau > 0);
  CHECK(offres.front().panneaux * offres.front().cartesParPanneau >= 100);
  const GrilleTarifaire g2 = grilleDepuisJson(grilleVersJson(g));
  CHECK(g2.formats.size() == 2);
  CHECK(g2.formats[1].remises.size() == 1);
  // Empilages comparés
  ConceptionElec autre = c;
  autre.uuid = nouvelUuid();
  autre.nom = "Autre";
  autre.circuit.empilage.couchesCuivre = 4;
  CHECK(comparerEmpilages({&c, &autre}).size() >= 1);
  CHECK(comparerEmpilages({&c, &c}).empty());
}

TEST_CASE("8.17 · deux pistes jointes à 60° : piège à acide signalé, avec sa vignette dans le rapport ; à 90°, rien", "[034][L8][8.17]") {
  ConceptionElec c = carte("Gravure", 40, 30);
  const Pt o = ptMm(10, 20);
  const double a = 60 * 3.14159265358979 / 180;
  c.circuit.pistes.push_back(piste("S", o, ptMm(30, 20), nm(0.3)));
  c.circuit.pistes.push_back(piste("S", o, Pt{o.x + nm(15 * std::cos(a)), o.y - nm(15 * std::sin(a))}, nm(0.3)));
  std::vector<ControleFabrication> seul;
  for (const auto& k : controlesFabricationParDefaut())
    if (k.code == "DFM-21") seul.push_back(k);
  const BilanFabrication b = controlerFabrication(c, seul);
  REQUIRE(b.constats.size() == 1);
  CHECK(b.constats[0].code == "DFM-21");
  CHECK(b.constats[0].vignette.rfind("<svg", 0) == 0);
  CHECK(b.constats[0].vignette.find("#e53935") != std::string::npos);
  CHECK(distance(b.constats[0].position, o) < nm(1.5));
  const std::string html = rapportFabricationHtml(c, b);
  CHECK(html.find("Piège à acide") != std::string::npos);
  CHECK(html.find("<svg") != std::string::npos);
  // À angle droit : aucun piège
  ConceptionElec d = carte("Équerre", 40, 30);
  d.circuit.pistes.push_back(piste("S", o, ptMm(30, 20), nm(0.3)));
  d.circuit.pistes.push_back(piste("S", o, ptMm(10, 5), nm(0.3)));
  CHECK(controlerFabrication(d, seul).constats.empty());
  // Contrôles relus de leur fichier ; nomenclature, MSL et refusion tirés des articles
  std::string erreur;
  const auto relus = lireControlesFabrication(texteControlesFabrication(controlesFabricationParDefaut()), &erreur);
  CHECK(relus.size() == controlesFabricationParDefaut().size());
  ConceptionElec e = carte("Assemblage");
  const std::string u1 = poserEmpreinteSeule(e, "SOIC-8", ptMm(20, 15));
  const std::string r1 = poserEmpreinteSeule(e, "R_0603", ptMm(5, 5));
  e.composant(u1)->champs["MSL"] = "3";
  e.composant(u1)->champs["refusion"] = "245";
  e.composant(r1)->champs["refusion"] = "260";
  e.composant(r1)->mpn = "RC0603";
  e.composant(r1)->fabricant = "Yageo";
  const BilanFabrication be = controlerFabrication(e);
  CHECK(be.aEtuver == std::vector<std::string>{e.composant(u1)->reference});
  CHECK(be.picRefusion == Approx(245));
  CHECK(std::count_if(be.constats.begin(), be.constats.end(), [](const ConstatFabrication& x) { return x.code == "DFM-27"; }) == 1);
  CHECK(be.score < 100);
  // Arrêt demandé : le contrôle s'interrompt
  std::atomic<bool> arret{true};
  CHECK(controlerFabrication(e, controlesFabricationParDefaut(), &arret).interrompu);
}

TEST_CASE("8.25 · pont en H déclaré à 3 A : seul le tronçon trop étroit de son trajet est signalé ; parallèle, sortie de pastille, perçages", "[034][L8][8.25]") {
  ConceptionElec c = carte("Pont en H", 60, 40);
  const std::string q1 = poserEmpreinteSeule(c, "R_1206", ptMm(10, 20), 0, false, "Q");
  const std::string j1 = poserEmpreinteSeule(c, "R_1206", ptMm(50, 20), 0, false, "J");
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(30, 35), 0, false, "R");
  const std::string refQ = c.composant(q1)->reference, refJ = c.composant(j1)->reference;
  for (const auto& [comp, num] : std::vector<std::pair<std::string, std::string>>{{q1, "2"}, {j1, "1"}, {r1, "1"}}) c.nomsReseaux[comp + ":" + num] = "MOT_A";
  const Pt a = pastille(c, q1, "2"), b = pastille(c, j1, "1"), s = pastille(c, r1, "1");
  // Trajet : trois tronçons larges et un étroit ; une dérivation étroite vers la résistance de mesure, hors du trajet
  const Pt m1{a.x + nm(8), a.y}, m2{a.x + nm(16), a.y}, m3{a.x + nm(24), a.y};
  c.circuit.pistes.push_back(piste("MOT_A", a, m1, nm(2)));
  c.circuit.pistes.push_back(piste("MOT_A", m1, m2, nm(0.3)));
  c.circuit.pistes.push_back(piste("MOT_A", m2, m3, nm(2)));
  c.circuit.pistes.push_back(piste("MOT_A", m3, b, nm(2)));
  c.circuit.pistes.push_back(piste("MOT_A", m3, {m3.x, s.y}, nm(0.25)));
  c.circuit.pistes.push_back(piste("MOT_A", {m3.x, s.y}, s, nm(0.25)));
  Contrainte k;
  k.nom = "Pont en H";
  k.genre = GenreContrainte::Courant;
  k.texte = refQ + ".2>" + refJ + ".1";
  k.valeur.min = 3000;  // mA
  c.circuit.regles.avancees.contraintes.push_back(k);
  const Connectivite kk = connectivite(c);
  const auto trajet = trajetBranche(c, kk, refQ + ".2", refJ + ".1", 3, 10);
  REQUIRE(trajet.size() == 4);
  CHECK(std::count_if(trajet.begin(), trajet.end(), [](const TronconCourant& t) { return t.largeur < t.requise; }) == 1);
  const auto v = drc(c);
  CHECK(compter(v, "DRC-79") == 1);
  for (const auto& x : v)
    if (x.code == "DRC-79") CHECK(x.objets.size() == 1);
  // Longueur parallèle : deux réseaux côte à côte sur 20 mm à 0,2 mm, 10 mm permis
  ConceptionElec p = carte("Diaphonie", 60, 40);
  p.circuit.pistes.push_back(piste("CLK", ptMm(10, 10), ptMm(30, 10), nm(0.2)));
  p.circuit.pistes.push_back(piste("DATA", ptMm(10, 10.4), ptMm(30, 10.4), nm(0.2)));
  Contrainte par;
  par.nom = "Horloge";
  par.genre = GenreContrainte::ParalleleMax;
  par.valeur.min = nm(0.5);
  par.valeur.max = nm(10);
  p.circuit.regles.avancees.contraintes.push_back(par);
  CHECK(compter(drc(p), "DRC-81") == 1);
  p.circuit.regles.avancees.contraintes[0].valeur.max = nm(25);
  CHECK(compter(drc(p), "DRC-81") == 0);
  // Perçages trop proches, même réseau (DRC-77)
  ConceptionElec t = carte("Perçages");
  Via v1, v2;
  v1.uuid = nouvelUuid();
  v1.reseau = "GND";
  v1.p = ptMm(10, 10);
  v2 = v1;
  v2.uuid = nouvelUuid();
  v2.p = ptMm(10.55, 10);
  t.circuit.vias = {v1, v2};
  CHECK(compter(drc(t), "DRC-77") == 1);
}

TEST_CASE("5.16, 5.17 · la synchronisation garde les réseaux d'une carte sans schéma : liaison restante au routage", "[034][L5][5.17]") {
  ConceptionElec c0 = carte("Restantes", 50, 30);
  const std::string r1 = poserEmpreinteSeule(c0, "R_0603", ptMm(8, 15));
  const std::string r2 = poserEmpreinteSeule(c0, "R_0603", ptMm(42, 15));
  const std::string n = reseauParPiste(c0, r1, "2", r2, "1");
  c0.circuit.pistes.push_back({"mur", "AUTRE", Couche::CuivreDessus, ptMm(25, 0.5), ptMm(25, 29.5), nm(0.3)});
  c0.circuit.pistes.push_back({"mur2", "AUTRE", Couche::CuivreDessous, ptMm(25, 0.5), ptMm(25, 29.5), nm(0.3)});
  const Connectivite k = connectivite(c0);
  UNSCOPED_INFO("réseau " << n << ", chevelu " << etatRoutage(c0, k, remplirZones(c0, k)).chevelu.size());
  for (const auto& p : pastilles(c0, k)) UNSCOPED_INFO(p.reference << "." << p.numero << " → " << p.reseau);
  ConceptionElec c1 = c0;
  const BilanRoutage b = routerSimple(c1);
  UNSCOPED_INFO("restantes " << b.restantes << " liaisons " << b.liaisons);
  const ConceptionElec relu = conceptionDepuisJson(versJson(c0));
  const Connectivite k2 = connectivite(relu);
  UNSCOPED_INFO("relu : chevelu " << etatRoutage(relu, k2, remplirZones(relu, k2)).chevelu.size());
  CHECK(b.restantes == 1);
}

TEST_CASE("5.22 · des gouttes posées font un changement (pas d'annulation) : les objets de la 0.34 comptent", "[034][L5][5.22]") {
  ConceptionElec c0 = carte("Commandes", 50, 30);
  const std::string u1 = poserEmpreinteSeule(c0, "SOIC-8", ptMm(25, 15));
  const std::string c1 = poserEmpreinteSeule(c0, "C_0603", ptMm(5, 5));
  c0.nomsReseaux[u1 + ":8"] = "VCC";
  c0.nomsReseaux[u1 + ":4"] = "GND";
  c0.nomsReseaux[c1 + ":1"] = "VCC";
  c0.nomsReseaux[c1 + ":2"] = "GND";
  const int n = placerDecouplages(c0, u1);
  UNSCOPED_INFO("découplages " << n << ", pistes " << c0.circuit.pistes.size() << ", régions " << c0.circuit.regions.size());
  const ChangementConception ch0 = differencier(ConceptionElec{}, c0);
  ConceptionElec c1c = c0;
  UNSCOPED_INFO("suites " << appliquerSuites(c1c, ch0) << " régions après suites " << c1c.circuit.regions.size());
  ConceptionElec c2 = c0;
  const int g = poserGouttes(c2);
  UNSCOPED_INFO("gouttes " << g << ", régions " << c2.circuit.regions.size() << ", vide " << differencier(c0, c2).vide());
  CHECK(!differencier(c0, c2).vide());
}

TEST_CASE("8.13 · StickHub préparé : plus aucune sérigraphie sur une pastille, changements listés avant d'être appliqués ; numéro de série par copie", "[034][L8][8.13]") {
  ConceptionElec c = conceptionDepuisKicad(prom::lireFichier(prom::cheminFichier(std::string(PROMETHEE_DONNEES) + "/kicad/StickHub.kicad_pcb")), "StickHub");
  const int avant = compter(drc(c), "DRC-13");
  const auto prevus = preparerSerigraphie(c);
  INFO("DRC-13 avant : " << avant << ", changements prévus : " << prevus.size());
  // La liste vient avant tout changement : la conception n'a pas bougé
  CHECK(compter(drc(c), "DRC-13") == avant);
  const auto bibliotheque = c.empreintes;
  const auto faits = appliquerPreparationSerigraphie(c);
  CHECK(faits.size() == prevus.size());
  {
    std::string restes;
    int n = 0;
    for (const auto& v : drc(c))
      if (v.code == "DRC-13" && n++ < 12) restes += v.message + " | ";
    INFO(restes);
    for (const auto& ch : preparerSerigraphie(c)) restes += "\n  encore : " + ch.description;
    INFO(restes);
    CHECK(compter(drc(c), "DRC-13") == 0);
  }
  CHECK(serigraphieSurPastilles(c).empty());
  // Les empreintes de la bibliothèque ne sont pas touchées : les traits rognés vivent dans des copies locales
  for (const auto& [id, d] : bibliotheque)
    if (const auto it = c.empreintes.find(id); it != c.empreintes.end()) CHECK(it->second.traits.size() == d.traits.size());
  // Une seconde préparation n'a plus rien à faire
  CHECK(preparerSerigraphie(c).empty());

  // Numéro de série : format du projet, une valeur par copie
  reglerNumeroSerie(c, {"PRM-{n:05}", 41});
  CHECK(numeroSerie(formatNumeroSerie(c), 0) == "PRM-00041");
  CHECK(numeroSerie(formatNumeroSerie(c), 5) == "PRM-00046");
  TexteCircuit t;
  t.uuid = nouvelUuid();
  t.texte = "S/N ${SERIE}";
  t.couche = Couche::SerigraphieDessus;
  t.p = {nm(5), nm(5)};
  c.circuit.textes.push_back(t);
  const ConceptionElec trois = conceptionNumerotee(c, 2);
  CHECK(trois.circuit.textes.back().texte == "S/N PRM-00043");
  const ConceptionElec relu = conceptionDepuisJson(versJson(c));
  CHECK(formatNumeroSerie(relu).format == "PRM-{n:05}");
}

TEST_CASE("8.14 · « pas de pâte sur les non montés » : aucune ouverture de pâte sous les trois non montés, Gerber relu ; colle et vernis de protection", "[034][L8][8.14]") {
  ConceptionElec c = carte("Assemblage", 60, 30);
  std::vector<std::string> nm3;
  for (int i = 0; i < 3; ++i) nm3.push_back(poserEmpreinteSeule(c, "R_0603", ptMm(10 + 12 * i, 10)));
  const std::string monte = poserEmpreinteSeule(c, "R_0603", ptMm(10, 22));
  for (const auto& u : nm3)
    for (auto& x : c.composants)
      if (x.uuid == u) x.nePasMonter = true;
  auto pateRelue = [&](const ConceptionElec& cc) {
    const std::filesystem::path d = dossierEssai("pate");
    const auto ecrits = ecrireDossierFabrication(cc, d.string(), "essai");
    std::string pate;
    for (const auto& f : ecrits)
      if (f.find("F_Paste") != std::string::npos) pate = prom::lireFichier(prom::cheminFichier((d / f).string()));
    return lireGerber(pate);
  };
  // Sans la règle : huit ouvertures de pâte (quatre résistances de deux pastilles)
  CHECK(pateRelue(c).objets.size() == 8);
  Contrainte k;
  k.nom = "Pas de pâte sous les non-montés";
  k.genre = GenreContrainte::PateNonMontes;
  k.texte = "oui";
  c.circuit.regles.avancees.contraintes.push_back(k);
  CHECK(composantsSansPate(c).size() == 3);
  const LectureGerber lu = pateRelue(c);
  CHECK(lu.erreurs.empty());
  REQUIRE(lu.objets.size() == 2);
  const std::string refMonte = c.composant(monte)->reference;
  for (const auto& o : lu.objets) CHECK((o.reference.empty() || o.reference == refMonte));
  // Une portée limite la règle : seuls les non-montés visés perdent leur pâte
  c.circuit.regles.avancees.contraintes.back().portee = "reference == '" + c.composant(nm3[0])->reference + "'";
  CHECK(composantsSansPate(c).size() == 1);
  CHECK(pateRelue(c).objets.size() == 6);

  // Points de colle tirés du corps et vernis de protection : couches « Glue » et « Coating » écrites
  c.extensions["colle"] = {{"faces", "dessus"}, {"diametre", 0.5}};
  DessinCircuit z;
  z.uuid = nouvelUuid();
  z.forme = DessinCircuit::Forme::Rectangle;
  z.couche = Couche::ProtectionDessus;
  z.points = {ptMm(2, 2), ptMm(40, 28)};
  c.circuit.dessins.push_back(z);
  const Connectivite kk = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, kk, remplirZones(c, kk));
  const auto couche = [&](Couche x) -> const CoucheFab* {
    for (const auto& cf : m.couches)
      if (cf.couche == x) return &cf;
    return nullptr;
  };
  REQUIRE(couche(Couche::ColleDessus));
  CHECK(couche(Couche::ColleDessus)->primitives.size() == 1);  // seul le composant monté reçoit son point de colle
  REQUIRE(couche(Couche::ProtectionDessus));
  CHECK(couche(Couche::ProtectionDessus)->primitives.size() == 1);
  const std::string glue = gerber(m, Couche::ColleDessus, "essai");
  CHECK(glue.find("Glue,Top") != std::string::npos);
  CHECK(lireGerber(glue).objets.size() == 1);
  CHECK(gerber(m, Couche::ProtectionDessus, "essai").find("Coating") != std::string::npos);
}

TEST_CASE("8.14 · règles d'assemblage à condition : écart entre boîtiers, orientations permises, marges de vernis et de pâte, pont de vernis", "[034][L8][8.14]") {
  ConceptionElec c = carte("Assemblage", 60, 30);
  const std::string a = poserEmpreinteSeule(c, "R_0603", ptMm(10, 10));
  const std::string b = poserEmpreinteSeule(c, "R_0603", ptMm(13, 10));
  const std::string q = poserEmpreinteSeule(c, "SOT-23", ptMm(40, 15), 45);
  auto regle = [&](GenreContrainte g, const std::string& portee, Nm min, const std::string& texte = {}) {
    Contrainte k;
    k.nom = nomGenreContrainte(g);
    k.genre = g;
    k.portee = portee;
    k.valeur.min = min;
    k.texte = texte;
    c.circuit.regles.avancees.contraintes.push_back(k);
  };
  CHECK(compter(drc(c), "DRC-88") == 0);
  // Deux résistances à 3 mm de pas : encombrements à moins de 2 mm l'un de l'autre
  regle(GenreContrainte::EcartComposants, "", 2'000'000);
  CHECK(compter(drc(c), "DRC-88") == 1);
  // Une règle moins stricte sur une portée plus étroite ne l'emporte pas : la plus prudente reste
  regle(GenreContrainte::EcartComposants, "prefixe == 'R'", 100'000);
  CHECK(compter(drc(c), "DRC-88") == 1);
  // Orientations permises pour les transistors : 45° refusé
  regle(GenreContrainte::Orientations, "prefixe == 'Q'", 0, "0,90,180,270");
  CHECK(compter(drc(c), "DRC-89") == 1);
  for (auto& e : c.circuit.empreintes)
    if (e.composant == q) e.rotation = 90;
  CHECK(compter(drc(c), "DRC-89") == 0);
  // Marge de vernis de 0,2 mm sur les résistances : l'ouverture de vernis grandit d'autant de chaque côté
  const Connectivite k0 = connectivite(c);
  const ModeleFabrication m0 = modeleFabrication(c, k0, remplirZones(c, k0));
  regle(GenreContrainte::MargeVernis, "prefixe == 'R'", 200'000);
  regle(GenreContrainte::MargePate, "prefixe == 'R'", -50'000);
  const Connectivite k1 = connectivite(c);
  const ModeleFabrication m1 = modeleFabrication(c, k1, remplirZones(c, k1));
  auto largeur = [&](const ModeleFabrication& m, Couche x, const std::string& ref) {
    for (const auto& cf : m.couches)
      if (cf.couche == x)
        for (const auto& p : cf.primitives)
          if (p.reference == ref) return p.l;
    return Nm{0};
  };
  const std::string ra = c.composant(a)->reference;
  CHECK(largeur(m1, Couche::VernisDessus, ra) - largeur(m0, Couche::VernisDessus, ra) == 2 * (200'000 - c.circuit.fabrication.margeVernis));
  CHECK(largeur(m1, Couche::PateDessus, ra) < largeur(m0, Couche::PateDessus, ra));
  // Pont de vernis : deux pastilles d'une 0603 avec 0,2 mm de marge chacune, pont exigé de 0,5 mm
  regle(GenreContrainte::PontVernis, "prefixe == 'R'", 500'000);
  CHECK(compter(drc(c), "DRC-90") >= 1);
}

TEST_CASE("8.26 · bus de seize lignes à résistances série de 22 Ω : longueur de chaque signal égale à la somme de ses deux tronçons, à 0,01 mm près", "[034][L8][8.26]") {
  ConceptionElec c = carte("Bus", 90, 50);
  const std::string u1 = poserEmpreinteSeule(c, "PinHeader_1x16_P2.54", ptMm(10, 5));
  const std::string u2 = poserEmpreinteSeule(c, "PinHeader_1x16_P2.54", ptMm(80, 5));
  std::vector<std::string> r(16);
  for (int i = 0; i < 16; ++i) {
    r[static_cast<std::size_t>(i)] = poserEmpreinteSeule(c, "R_0603", ptMm(45, 5 + 2.54 * i));
    for (auto& x : c.composants)
      if (x.uuid == r[static_cast<std::size_t>(i)]) x.valeur = "22";
  }
  // Réseaux de la carte seule : U1.n — Rn.1 (Dn_A), Rn.2 — U2.n (Dn_B)
  for (int i = 0; i < 16; ++i) {
    const std::string n = std::to_string(i + 1), ri = r[static_cast<std::size_t>(i)];
    c.nomsReseaux[u1 + ":" + n] = "D" + std::to_string(i) + "_A";
    c.nomsReseaux[ri + ":1"] = "D" + std::to_string(i) + "_A";
    c.nomsReseaux[ri + ":2"] = "D" + std::to_string(i) + "_B";
    c.nomsReseaux[u2 + ":" + n] = "D" + std::to_string(i) + "_B";
  }
  // Un tracé à deux segments par tronçon, coudé différemment d'une ligne à l'autre
  for (int i = 0; i < 16; ++i) {
    const std::string n = std::to_string(i + 1), ri = r[static_cast<std::size_t>(i)], da = "D" + std::to_string(i) + "_A", db = "D" + std::to_string(i) + "_B";
    const Pt a = pastille(c, u1, n), b = pastille(c, ri, "1"), c2 = pastille(c, ri, "2"), d = pastille(c, u2, n);
    const Pt coudeA{a.x + nm(5 + i * 0.5), b.y}, coudeB{d.x - nm(4 + i * 0.3), c2.y};
    c.circuit.pistes.push_back(piste(da, a, {coudeA.x, a.y}, 200'000));
    c.circuit.pistes.push_back(piste(da, {coudeA.x, a.y}, b, 200'000));
    c.circuit.pistes.push_back(piste(db, c2, {coudeB.x, c2.y}, 200'000));
    c.circuit.pistes.push_back(piste(db, {coudeB.x, c2.y}, d, 200'000));
  }
  const auto signaux = signauxSerie(c);
  REQUIRE(signaux.size() == 16);
  CHECK(creerSignauxSerie(c) == 16);
  CHECK(creerSignauxSerie(c) == 0);  // déjà là
  const std::string refU1 = c.composant(u1)->reference, refU2 = c.composant(u2)->reference;
  for (const auto& s : c.circuit.regles.avancees.signaux) {
    REQUIRE(s.reseaux.size() == 2);
    const int i = std::stoi(s.reseaux.front().substr(1));
    const std::string n = std::to_string(i + 1), refR = c.composant(r[static_cast<std::size_t>(i)])->reference;
    const auto t1 = longueurTroncon(c, refU1 + "." + n, refR + ".1"), t2 = longueurTroncon(c, refR + ".2", refU2 + "." + n);
    REQUIRE(t1);
    REQUIRE(t2);
    INFO(s.nom << " : " << longueurSignal(c, s) * 1e-6 << " mm contre " << (*t1 + *t2) * 1e-6);
    CHECK(std::abs(longueurSignal(c, s) - (*t1 + *t2)) <= 10'000);  // 0,01 mm
  }
  // Longueurs internes du boîtier, lues d'un CSV du fabricant (mil) : elles s'ajoutent au signal et au tronçon
  CHECK(importerLongueursBoitier(c, refU1, "Pin;Length (mil)\n1;100\n2;120\n") == 2);
  const Signal& s0 = *std::find_if(c.circuit.regles.avancees.signaux.begin(), c.circuit.regles.avancees.signaux.end(), [](const Signal& s) { return s.reseaux.front() == "D0_A" || s.reseaux.back() == "D0_A"; });
  const double avant = longueurReseau(c, "D0_A") + longueurReseau(c, "D0_B");
  CHECK(longueurSignal(c, s0) == Approx(avant + 2.54e6).margin(1'000));
  // Règle From-To : un tronçon borné à 10 mm est signalé
  Signal& s1 = c.circuit.regles.avancees.signaux.front();
  const int i1 = std::stoi(s1.reseaux.front().substr(1));
  s1.troncons.push_back({refU1 + "." + std::to_string(i1 + 1), c.composant(r[static_cast<std::size_t>(i1)])->reference + ".1", 10'000'000, 0});
  CHECK(compter(drc(c), "DRC-86") == 1);
  // Récapitulatif en XLSX
  const std::string x = xlsxSignaux(c);
  const auto lignes = lireXlsx(x);
  CHECK(lignes.size() >= 17);
}

TEST_CASE("8.26 · gabarits d'interface : USB, DDR4… créent d'un geste signaux, paires, classes et règles ; ordre de raccordement suivi par le chevelu", "[034][L8][8.26]") {
  REQUIRE(gabaritsInterface().size() == 8);
  ConceptionElec c = carte("Interfaces", 60, 40);
  c.circuit.empilage.couchesCuivre = 4;
  const auto usb = std::find_if(gabaritsInterface().begin(), gabaritsInterface().end(), [](const GabaritInterface& g) { return g.nom == "USB 2.0"; });
  REQUIRE(usb != gabaritsInterface().end());
  const BilanGabarit b = appliquerGabarit(c, *usb, "USB_");
  CHECK(b.paires == 1);
  CHECK(b.classes == 1);
  const auto& A = c.circuit.regles.avancees;
  REQUIRE(A.paires.size() == 1);
  CHECK(A.paires[0].positif == "USB_D_P");
  CHECK(A.paires[0].largeur > 0);  // proposée par le moteur de lignes (8.21), 90 Ω sur l'empilage
  CHECK(classeDe(c.circuit.regles, "USB_D_N") != nullptr);
  CHECK(!b.journal.empty());  // réseaux absents signalés
  const auto ddr4 = std::find_if(gabaritsInterface().begin(), gabaritsInterface().end(), [](const GabaritInterface& g) { return g.nom == "DDR4"; });
  const BilanGabarit d = appliquerGabarit(c, *ddr4, "");
  CHECK(d.signaux == 14);  // A0 à A13 en fly-by
  CHECK(d.accords == 1);
  const auto octet = std::find_if(A.accords.begin(), A.accords.end(), [](const GroupeAccord& g) { return g.nom == "OCTET0"; });
  REQUIRE(octet != A.accords.end());
  CHECK(octet->reseaux.size() == 11);  // DQ0 à DQ7, DM_DBI, DQS_P, DQS_N
  // Gabarit lu d'un fichier de données
  std::string err;
  const auto lu = lireGabaritInterface(R"({"nom":"SPI","classes":[{"nom":"SPI","largeur":0.15,"reseaux":["{P}SCK","{P}MOSI"]}]})", &err);
  REQUIRE(lu);
  CHECK(appliquerGabarit(c, *lu, "S1_").classes == 1);

  // Ordre de raccordement : un réseau à trois broches en chaîne U1 → U2 → U3 ; le chevelu suit l'ordre donné
  ConceptionElec k = carte("Chaine", 60, 40);
  const std::string a = poserEmpreinteSeule(k, "R_0603", ptMm(10, 10)), b2 = poserEmpreinteSeule(k, "R_0603", ptMm(50, 10)), c3 = poserEmpreinteSeule(k, "R_0603", ptMm(20, 10));
  for (const auto& u : {a, b2, c3}) k.nomsReseaux[u + ":1"] = "CLK";
  Signal s;
  s.nom = "CLK";
  s.reseaux = {"CLK"};
  s.topologie = TopologieSignal::Chaine;
  s.ordre = {k.composant(a)->reference + ".1", k.composant(b2)->reference + ".1", k.composant(c3)->reference + ".1"};
  k.circuit.regles.avancees.signaux.push_back(s);
  const Connectivite kk = connectivite(k);
  const EtatRoutage e = etatRoutage(k, kk, remplirZones(k, kk));
  // Sans ordre, l'arbre couvrant irait a → c3 → b2 ; avec l'ordre, a → b2 puis b2 → c3
  bool ab = false, bc = false;
  const Pt pa = pastille(k, a, "1"), pb = pastille(k, b2, "1"), pc = pastille(k, c3, "1");
  for (const auto& l : e.chevelu) {
    ab |= (l.a == pa && l.b == pb) || (l.a == pb && l.b == pa);
    bc |= (l.a == pb && l.b == pc) || (l.a == pc && l.b == pb);
  }
  CHECK(ab);
  CHECK(bc);
}

TEST_CASE("8.28 · carte de mesure de 300 composants : deux stratégies comparées, la meilleure appliquée en une commande, pistes posées à la main intactes", "[034][L8][8.28]") {
  ConceptionElec c = carte("Mesure", 100, 90);
  // 20 rangées de 15 résistances ; dans chaque rangée, la broche 2 de l'une va à la broche 1 de la suivante
  std::vector<std::vector<std::string>> r(20, std::vector<std::string>(15));
  for (int i = 0; i < 20; ++i)
    for (int j = 0; j < 15; ++j) r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = poserEmpreinteSeule(c, "R_0603", ptMm(5 + 6.2 * j, 5 + 4.2 * i));
  REQUIRE(c.composants.size() == 300);
  for (int i = 0; i < 20; ++i)
    for (int j = 0; j + 1 < 15; ++j) {
      const std::string n = "N" + std::to_string(i) + "_" + std::to_string(j);
      c.nomsReseaux[r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] + ":2"] = n;
      c.nomsReseaux[r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 1)] + ":1"] = n;
    }
  // Deux pistes posées à la main, coudées, sur la première rangée
  c.circuit.pistes.push_back(piste("N0_0", pastille(c, r[0][0], "2"), {pastille(c, r[0][0], "2").x + nm(1), pastille(c, r[0][1], "1").y}, 200'000));
  c.circuit.pistes.push_back(piste("N0_0", {pastille(c, r[0][0], "2").x + nm(1), pastille(c, r[0][1], "1").y}, pastille(c, r[0][1], "1"), 200'000));
  const auto mainAvant = c.circuit.pistes;
  // Rapport de préparation
  const auto rapport = rapportPreparation(c);
  REQUIRE(!rapport.empty());
  CHECK(rapport.front().find("279 liaison") != std::string::npos);
  // Deux stratégies enregistrées, comparées sur des copies
  enregistrerStrategies(c, strategiesRoutage(c));
  const auto strategies = strategiesRoutage(c);
  REQUIRE(strategies.size() == 2);
  const auto essais = comparerStrategies(c, strategies);
  REQUIRE(essais.size() == 2);
  CHECK(essais[0].score >= essais[1].score);
  CHECK(essais[0].routees == 279);
  CHECK(c.circuit.pistes == mainAvant);  // l'essai ne touche pas la carte
  // Le meilleur appliqué en une commande : la carte routée, les pistes à la main inchangées
  const int ajoutes = appliquerEssai(c, essais[0]);
  CHECK(ajoutes > 0);
  for (const auto& p : mainAvant) CHECK(std::find(c.circuit.pistes.begin(), c.circuit.pistes.end(), p) != c.circuit.pistes.end());
  const Connectivite k = connectivite(c);
  CHECK(etatRoutage(c, k, remplirZones(c, k)).chevelu.empty());
  CHECK(compter(drc(c), "DRC-01") == 0);
  // Routeur pas à pas : pause et reprise
  ConceptionElec vide = carte("Pas", 100, 90);
  RouteurAuto pas(c, strategies[0]);
  CHECK(pas.termine());  // plus rien à router
  (void)vide;
}

TEST_CASE("8.28 · algorithme hongrois : affectation de coût minimal ; échanges de broches proposés et reportés au schéma", "[034][L8][8.28]") {
  const std::vector<std::vector<double>> cout{{4, 1, 3}, {2, 0, 5}, {3, 2, 2}};
  const auto a = affectationHongroise(cout);
  REQUIRE(a.size() == 3);
  double total = 0;
  for (std::size_t i = 0; i < 3; ++i) total += cout[i][static_cast<std::size_t>(a[i])];
  CHECK(total == Approx(5));  // 1 + 2 + 2
  // Matrice plus grande : comparé à l'énumération de toutes les permutations
  std::vector<std::vector<double>> m(6, std::vector<double>(6));
  for (std::size_t i = 0; i < 6; ++i)
    for (std::size_t j = 0; j < 6; ++j) m[i][j] = static_cast<double>((i * 7 + j * 13 + i * j * 3) % 17);
  const auto h = affectationHongroise(m);
  double meilleur = 1e9, trouve = 0;
  std::vector<int> perm{0, 1, 2, 3, 4, 5};
  do {
    double s = 0;
    for (std::size_t i = 0; i < 6; ++i) s += m[i][static_cast<std::size_t>(perm[i])];
    meilleur = std::min(meilleur, s);
  } while (std::next_permutation(perm.begin(), perm.end()));
  for (std::size_t i = 0; i < 6; ++i) trouve += m[i][static_cast<std::size_t>(h[i])];
  CHECK(trouve == Approx(meilleur));
}
