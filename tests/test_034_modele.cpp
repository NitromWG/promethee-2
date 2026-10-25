// Prométhée : essais de la 0.34, modèle du circuit étendu — 32 cuivres et empilage détaillé (8.20), vias spéciales
// (8.22), contre-perçage (8.23), piles de pastilles (4.24), perçage complet (4.13, 4.27), Gerber X2 complet (8.4),
// Gerber X3 (8.7), usinages de la carte nue (8.12), zones enrichies (5.11, 5.25, 5.26), relecture sans perte.
// Licence GPL-3.0-only.
#include <algorithm>
#include <numbers>
#include <set>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"
#include "promethee/fabrication_avancee.hpp"
#include "promethee/fabrication_detaillee.hpp"
#include "promethee/piles_pastilles.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
int erreurs(const std::vector<Violation>& v) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [](const Violation& x) { return x.gravite == Gravite::Erreur; }));
}
// Carte sans schéma : une empreinte posée par son identifiant, son composant créé à la main
std::string poserEmpreinte(ConceptionElec& c, const std::string& ref, const std::string& id, Pt p, double rot = 0) {
  ComposantElec x;
  x.uuid = nouvelUuid();
  x.reference = ref;
  x.symbole = "R";
  x.empreinte = id;
  x.carteSeule = true;
  c.composants.push_back(x);
  c.symboles.emplace("R", *symboleBibliotheque("R"));
  EmpreintePlacee e;
  e.composant = x.uuid;
  e.empreinte = id;
  e.position = p;
  e.rotation = rot;
  c.circuit.empreintes.push_back(e);
  figerEmpreintes(c);
  return x.uuid;
}
ModeleFabrication modele(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  return modeleFabrication(c, k, remplirZones(c, k));
}
}  // namespace

TEST_CASE("8.20 · 32 cuivres, codes et rangs ; empilage détaillé à âme d'aluminium relu sans perte, épaisseur juste au gbrjob", "[034][L8][8.20]") {
  CHECK(couchesCuivre(32).size() == 32);
  CHECK(std::string(codeCouche(couchesCuivre(32)[30])) == "In30.Cu");
  CHECK(rangCuivre(Couche::CuivreDessous, 6) == 5);
  CHECK(rangCuivre(cuivreInterne(4), 6) == 4);
  CHECK(rangCuivre(cuivreInterne(5), 6) == -1);
  CHECK(coucheDepuisCode("In17.Cu") == cuivreInterne(17));
  CHECK(couchesCuivre(3).size() == 3);  // impair : permis (8.20)

  ConceptionElec c = nouvelleConception("Âme métallique");
  c.circuit.contour.largeur = nm(40);
  c.circuit.contour.hauteur = nm(30);
  Empilage& e = c.circuit.empilage;
  e.couchesCuivre = 2;
  CoucheEmpilage cu;
  cu.genre = GenreCoucheEmpilage::Cuivre;
  cu.epaisseur = 0.035;
  cu.nom = "F.Cu";
  CoucheEmpilage thermique;
  thermique.genre = GenreCoucheEmpilage::Dielectrique;
  thermique.epaisseur = 0.1;
  thermique.materiau = "Diélectrique thermique";
  thermique.conductivite = 2.0;
  CoucheEmpilage ame;
  ame.genre = GenreCoucheEmpilage::AmeMetallique;
  ame.epaisseur = 1.0;
  ame.materiau = "Aluminium 5052";
  ame.conductivite = 138;
  CoucheEmpilage cu2 = cu;
  cu2.nom = "B.Cu";
  CoucheEmpilage thermique2 = thermique;
  e.pile = {cu, thermique, ame, thermique2, cu2};
  REQUIRE(epaisseurPile(e.pile) == Approx(1.27));
  // Relu à l'identique
  const auto j = versJson(c);
  const ConceptionElec relue = conceptionDepuisJson(j);
  CHECK(relue.circuit.empilage == c.circuit.empilage);
  CHECK(versJson(relue).dump() == j.dump());
  // gbrjob : épaisseur de la pile, âme métallique déclarée
  const ModeleFabrication m = modele(c);
  const auto job = nlohmann::json::parse(gerberJob(c, m, {}, "essai"));
  CHECK(job["GeneralSpecs"]["BoardThickness"].get<double>() == Approx(1.27));
  bool ameVue = false;
  for (const auto& x : job["MaterialStackup"]) ameVue |= x.value("Name", "") == "Metal core";
  CHECK(ameVue);
  // Six cuivres : fonctions L1 à L6 dans les Gerber
  c.circuit.empilage = Empilage{};
  c.circuit.empilage.couchesCuivre = 6;
  const ModeleFabrication m6 = modele(c);
  std::set<std::string> fonctions;
  for (const auto& cf : m6.couches)
    if (estCuivre(cf.couche)) {
      const std::string g = gerber(m6, cf.couche, "essai");
      const auto i = g.find("%TF.FileFunction,");
      fonctions.insert(g.substr(i + 17, g.find('*', i) - i - 17));
    }
  CHECK(fonctions == std::set<std::string>{"Copper,L1,Top", "Copper,L2,Inr", "Copper,L3,Inr", "Copper,L4,Inr", "Copper,L5,Inr", "Copper,L6,Bot"});
}

TEST_CASE("4.13, 4.27 · Fentes détourées et en G85, trou décalé, trous en plus, tolérance ; tableau, plan, forets du profil", "[034][L4][4.13][4.27]") {
  ConceptionElec c = nouvelleConception("Fentes");
  c.circuit.contour.largeur = nm(40);
  c.circuit.contour.hauteur = nm(30);
  DefEmpreinte d;
  d.id = "perso:Jack";
  d.famille = "jack";
  Pastille p;
  p.numero = "1";
  p.type = TypePastille::Traversante;
  p.forme = FormePastille::Ovale;
  p.largeur = nm(3.0);
  p.hauteur = nm(1.8);
  p.percage = nm(1.0);
  p.longueurPercage = nm(2.2);
  p.tolerancePercage = nm(0.05);
  p.trousEnPlus.push_back({ptMm(0, 0), nm(0.3)});
  d.pastilles.push_back(p);
  Pastille q = p;
  q.numero = "2";
  q.position = ptMm(5, 0);
  q.longueurPercage = 0;
  q.decalagePercage = ptMm(0.2, 0);
  q.trousEnPlus.clear();
  q.percage = nm(0.98);
  d.pastilles.push_back(q);
  d.encombrement = {-nm(3), -nm(2), nm(8), nm(2)};
  c.empreintes[d.id] = d;
  poserEmpreinte(c, "J1", d.id, ptMm(15, 15), 90);
  ModeleFabrication m = modele(c);
  // Excellon : fente détourée (G00, M15, G01, M16) par défaut ; G85 au choix
  const std::string pth = excellon(m, true, "essai");
  CHECK(pth.find("M15") != std::string::npos);
  CHECK(pth.find("M16") != std::string::npos);
  CHECK(pth.find("G85") == std::string::npos);
  OptionsExcellon o;
  o.g85 = true;
  PairePercage tout = pairesPercage(m).front();
  CHECK(excellonPaire(m, tout, o, "essai").find("G85") != std::string::npos);
  // Relu par le lecteur du noyau : trois trous (la fente au milieu), aux mêmes places
  const auto lus = lirePercages(pth);
  CHECK(lus.size() == 3);
  // Trou décalé de 0,2 mm le long de x de la pastille, empreinte tournée de 90° : il monte de 0,2 mm sur la carte
  bool decale = false;
  for (const auto& t : m.trous) decale |= t.diametre == nm(0.98) && std::llabs(t.p.y - (ptMm(15, 15).y - nm(5.2))) < 2;
  CHECK(decale);
  // Tableau : la tolérance et la fente y sont
  const auto table = tableauPercages(m);
  CHECK(std::any_of(table.begin(), table.end(), [](const LignePercage& l) { return l.longueur == nm(2.2) && l.tolerance == nm(0.05); }));
  CHECK(tableauPercagesCsv(m).find("±0,050") != std::string::npos);
  CHECK(nlohmann::json::parse(tableauPercagesJson(m))["percages"].size() == table.size());
  CHECK(planPercageSvg(m, "plan").find("<svg") != std::string::npos);
  // Forets du profil : 0,98 mm ramené à 1,00 mm ; 0,30 mm reste
  std::vector<std::string> sans;
  CHECK(ramenerAuxForets(m, foretsUsuels(), nm(0.03), &sans) == 1);
  CHECK(std::count_if(m.trous.begin(), m.trous.end(), [](const TrouFab& t) { return t.diametre == nm(1.0); }) == 2);
  CHECK(sans.empty());
}

TEST_CASE("4.27 · Sept jeux d'options Excellon relus : mêmes trous, mêmes diamètres", "[034][L4][4.27]") {
  ConceptionElec c = exempleElectronique();
  const ModeleFabrication m = modele(c);
  const PairePercage pth = pairesPercage(m).front();
  const auto reference = lirePercages(excellon(m, true, "essai"));
  REQUIRE(!reference.empty());
  // Métrique, décimales 3 à 6, G85 ou détouré, sans attributs, XNC : le lecteur relit les mêmes trous
  std::vector<OptionsExcellon> jeux(7);
  jeux[1].decimales = 3;
  jeux[2].decimales = 6;
  jeux[3].g85 = true;
  jeux[4].attributs = false;
  jeux[5].xnc = true;
  jeux[6].decimales = 5;
  for (const auto& o : jeux) {
    auto lus = lirePercages(excellonPaire(m, pth, o, "essai"));
    REQUIRE(lus.size() == reference.size());
    for (std::size_t i = 0; i < lus.size(); ++i) {
      CHECK(std::llabs(lus[i].first.x - reference[i].first.x) <= 1000);
      CHECK(std::llabs(lus[i].first.y - reference[i].first.y) <= 1000);
      CHECK(lus[i].second == reference[i].second);
    }
  }
}

TEST_CASE("8.4 · Arcs de pistes (G75, G02/G03) et rectangles arrondis en macro, relus par le lecteur du noyau", "[034][L8][8.4]") {
  ConceptionElec c = nouvelleConception("Arcs");
  c.circuit.contour.largeur = nm(40);
  c.circuit.contour.hauteur = nm(40);
  c.cartouche.date = "2026-10-24";
  poserEmpreinte(c, "R1", "R_0805", ptMm(10, 20));
  Piste arc;
  arc.uuid = nouvelUuid();
  arc.reseau = "N1";
  arc.a = ptMm(20, 20);
  arc.b = ptMm(30, 20);
  arc.milieu = ptMm(25, 15);
  arc.largeur = nm(0.3);
  c.circuit.pistes.push_back(arc);
  CHECK(longueurPiste(arc) == Approx(std::numbers::pi * 5e6).epsilon(1e-6));
  const ModeleFabrication m = modele(c);
  const std::string g = gerber(m, Couche::CuivreDessus, "essai");
  CHECK(g.find("%TF.ProjectId,Arcs,") != std::string::npos);
  CHECK(g.find("%TF.CreationDate,2026-10-24T00:00:00+00:00*%") != std::string::npos);
  CHECK(g.find("G75*") != std::string::npos);
  CHECK((g.find("G02X") != std::string::npos || g.find("G03X") != std::string::npos));
  CHECK(g.find("%AMRoundRect*") != std::string::npos);
  CHECK(g.find("RoundRect,") != std::string::npos);
  // Relecture : l'arc revient en cordes de bout à bout, à 2 µm près de l'arc vrai ; les deux pastilles en macro
  const LectureGerber l = lireGerber(g);
  CHECK(l.erreurs.empty());
  double longueur = 0;
  for (const auto& o : l.objets)
    if (o.genre == ObjetGerber::Genre::Trait && o.largeur == nm(0.3)) longueur += distance(o.a, o.b);
  CHECK(longueur == Approx(std::numbers::pi * 5e6).epsilon(2e-3));
  CHECK(std::count_if(l.objets.begin(), l.objets.end(), [](const ObjetGerber& o) { return o.genre == ObjetGerber::Genre::Flash && !o.reference.empty(); }) == 2);
}

TEST_CASE("8.22 · Tracé de L1 vers L2 sur une carte HDI : micro-via laser, sans réglage ; vias à remplir en Gerber Viafill", "[034][L8][8.22]") {
  Empilage e;
  e.couchesCuivre = 6;
  e.microVias = true;
  e.viasBorgnes = true;
  e.viasEnterrees = true;
  Regles r;
  const Via v = viaPour(e, r, "D+", Couche::CuivreDessus, Couche::CuivreInt1);
  CHECK(v.type == TypeVia::Micro);
  CHECK(v.laser);
  CHECK(v.percage == e.microViaPercage);
  CHECK(viaPour(e, r, "D+", Couche::CuivreDessus, cuivreInterne(3)).type == TypeVia::Borgne);
  CHECK(viaPour(e, r, "D+", Couche::CuivreInt1, cuivreInterne(3)).type == TypeVia::Enterree);
  CHECK(viaPour(e, r, "D+", Couche::CuivreDessus, Couche::CuivreDessous).type == TypeVia::Traversante);
  e.microVias = false;
  CHECK(viaPour(e, r, "D+", Couche::CuivreDessus, Couche::CuivreInt1).type == TypeVia::Borgne);
  // Sur la carte : la micro-via ne relie que L1 et L2 ; son perçage a son fichier ; remplie, elle est au Viafill
  ConceptionElec c = nouvelleConception("HDI");
  c.circuit.empilage.couchesCuivre = 6;
  c.circuit.empilage.microVias = true;
  Via mv = viaPour(c.circuit.empilage, c.circuit.regles, "N1", Couche::CuivreDessus, Couche::CuivreInt1);
  mv.uuid = nouvelUuid();
  mv.p = ptMm(20, 20);
  mv.remplissage = "VII";
  c.circuit.vias.push_back(mv);
  CHECK(couchesVia(mv, 6) == (bit(Couche::CuivreDessus) | bit(Couche::CuivreInt1)));
  const ModeleFabrication m = modele(c);
  const auto paires = pairesPercage(m);
  REQUIRE(paires.size() == 1);
  CHECK(paires.front().fonction == "Plated,1,2,Blind");
  CHECK(gerber(m, cuivreInterne(2), "essai").find("D03") == std::string::npos);
  CHECK(gerberViaFill(m, c, "essai").find("%TF.FileFunction,Viafill*%") != std::string::npos);
  // Relu sans perte
  CHECK(conceptionDepuisJson(versJson(c)).circuit.vias.front() == c.circuit.vias.front());
}

TEST_CASE("8.23 · Contre-perçage par le dessous jusqu'à L3 : talon de 0,2 mm au plus, fichier dédié, pistes coupées signalées", "[034][L8][8.23]") {
  ConceptionElec c = nouvelleConception("Contre-perçage");
  c.circuit.empilage.couchesCuivre = 6;
  c.circuit.empilage.epaisseur = 1.6;
  Via v;
  v.uuid = nouvelUuid();
  v.reseau = "CLK";
  v.p = ptMm(20, 20);
  ContrePercage cp;
  cp.dessous = true;
  cp.jusqua = cuivreInterne(2);  // L3
  cp.talonMax = nm(0.2);
  v.contrePercage = cp;
  c.circuit.vias.push_back(v);
  const BilanContrePercage b = bilanContrePercage(v, c.circuit.empilage);
  INFO(b.raison);
  CHECK(b.possible);
  CHECK(b.talon <= 0.2 + 1e-9);
  CHECK(b.coupees.size() == 3);  // L4, L5, L6
  // Une piste de CLK sur L5, reliée à la via : coupée par le contre-perçage
  c.circuit.pistes.push_back({nouvelUuid(), "CLK", cuivreInterne(4), v.p, ptMm(30, 20), nm(0.2)});
  CHECK(pistesSurCouchesCoupees(c, v).size() == 1);
  const ModeleFabrication m = modele(c);
  const auto paires = pairesPercage(m);
  const auto it = std::find_if(paires.begin(), paires.end(), [](const PairePercage& p) { return p.contrePercage; });
  REQUIRE(it != paires.end());
  CHECK(it->nomFichier == "contre-percage-6-4");
  const auto lus = lirePercages(excellonPaire(m, *it, OptionsExcellon{}, "essai"));
  REQUIRE(lus.size() == 1);
  CHECK(lus.front().second == v.percage + nm(0.2));
}

TEST_CASE("4.24 · Gabarit de pile de pastilles élargi de 0,1 mm : les 64 pastilles d'une TQFP-64 suivent en une commande", "[034][L4][4.24]") {
  ConceptionElec c = nouvelleConception("Gabarits");
  poserEmpreinte(c, "U1", "TQFP-64-10x10-0.5", ptMm(25, 25));
  DefEmpreinte& d = c.empreintes.at("TQFP-64-10x10-0.5");
  REQUIRE(d.pastilles.size() == 64);
  std::map<std::string, GabaritPastille> gabarits;
  CHECK(lierAuxGabarits(d, gabarits) == 64);
  REQUIRE(gabarits.size() == 1);
  const std::string ancien = gabarits.begin()->first;
  GabaritPastille g = gabarits.begin()->second;
  CHECK(g.nom.rfind("RRX", 0) == 0);
  g.largeur += nm(0.1);
  g.nom = nomIPCGabarit(g);
  CHECK(g.nom != ancien);
  const Nm avant = d.pastilles.front().largeur;
  CHECK(appliquerGabaritPartout(c, ancien, g) == 64);
  for (const auto& p : c.empreintes.at("TQFP-64-10x10-0.5").pastilles) {
    CHECK(p.largeur == avant + nm(0.1));
    CHECK(p.gabarit == g.nom);
  }
  CHECK(c.circuit.gabarits.count(g.nom) == 1);
  // Relu sans perte : gabarit et lien des pastilles
  const ConceptionElec r = conceptionDepuisJson(versJson(c));
  CHECK(r.circuit.gabarits == c.circuit.gabarits);
  CHECK(r.empreintes.at("TQFP-64-10x10-0.5") == c.empreintes.at("TQFP-64-10x10-0.5"));
}

TEST_CASE("4.24 · Pastilles complètes : chanfreinée, trapèze et forme libre, vernis et pâte propres, motif de pâte, raccord imposé", "[034][L4][4.24]") {
  Pastille p;
  p.forme = FormePastille::Chanfreine;
  p.largeur = nm(2);
  p.hauteur = nm(1);
  p.chanfrein = 0.25;
  p.coinsChanfrein = 1;
  CHECK(formePastille(p).size() == 5);
  p.forme = FormePastille::Trapeze;
  p.trapeze = nm(0.4);
  CHECK(aireMm2(formePastille(p)) == Approx(2.0).epsilon(1e-3));
  p.forme = FormePastille::Libre;
  p.contourLibre = {ptMm(-1, -0.5), ptMm(1, -0.5), ptMm(0, 0.5)};
  CHECK(aireMm2(formePastille(p)) == Approx(1.0).epsilon(1e-3));
  // Pastille de connecteur de tranche, motif de pâte 2 × 2 à 60 % : quatre fenêtres, aire à 60 %
  ConceptionElec c = nouvelleConception("Pâte");
  c.circuit.contour.largeur = nm(40);
  c.circuit.contour.hauteur = nm(30);
  DefEmpreinte d;
  d.id = "perso:Plot";
  Pastille a;
  a.numero = "1";
  a.forme = FormePastille::Rect;
  a.largeur = nm(4);
  a.hauteur = nm(4);
  a.motifPate = MotifPate{2, 2, nm(0.3), 0, 0.6};
  a.margeVernis = nm(0.1);
  d.pastilles.push_back(a);
  Pastille b = a;
  b.numero = "2";
  b.position = ptMm(8, 0);
  b.motifPate.reset();
  b.pateLibre = {ptMm(-1, -1), ptMm(1, -1), ptMm(1, 1), ptMm(-1, 1)};
  b.connexion = ConnexionZone::Pleine;
  d.pastilles.push_back(b);
  d.encombrement = {-nm(3), -nm(3), nm(11), nm(3)};
  c.empreintes[d.id] = d;
  poserEmpreinte(c, "J1", d.id, ptMm(10, 15));
  const ModeleFabrication m = modele(c);
  double pate1 = 0, pate2 = 0;
  int fenetres = 0;
  for (const auto& cf : m.couches)
    if (cf.couche == Couche::PateDessus)
      for (const auto& x : cf.primitives) {
        if (x.numero == "1") {
          ++fenetres;
          pate1 += aireMm2(x.region);
        }
        if (x.numero == "2") pate2 += aireMm2(x.region);
      }
  CHECK(fenetres == 4);
  CHECK(pate1 == Approx(16 * 0.6).epsilon(0.02));
  CHECK(pate2 == Approx(4.0).epsilon(1e-3));
  // Vernis propre : 4,2 mm
  for (const auto& cf : m.couches)
    if (cf.couche == Couche::VernisDessus)
      for (const auto& x : cf.primitives)
        if (x.numero == "1") CHECK(x.l == nm(4.2));
  CHECK(conceptionDepuisJson(versJson(c)).empreintes.at(d.id) == c.empreintes.at(d.id));
}

TEST_CASE("8.12 · Module à huit demi-trous coupés par le contour : DRC sans erreur, CastellatedPad et CastellatedDrill, gbrjob Castellated", "[034][L8][8.12]") {
  ConceptionElec c = nouvelleConception("Module");
  c.circuit.contour.origine = {};
  c.circuit.contour.largeur = nm(20);
  c.circuit.contour.hauteur = nm(15);
  c.circuit.contour.rayon = 0;
  DefEmpreinte d;
  d.id = "perso:Creneaux";
  for (int i = 0; i < 8; ++i) {
    Pastille p;
    p.numero = std::to_string(i + 1);
    p.type = TypePastille::Traversante;
    p.forme = FormePastille::Ovale;
    p.largeur = nm(1.0);
    p.hauteur = nm(2.0);
    p.percage = nm(0.6);
    p.position = {nm(2.54) * i, 0};
    p.fabrication = "castellee";
    d.pastilles.push_back(p);
  }
  d.encombrement = {-nm(1), -nm(1.2), nm(2.54 * 7 + 1), nm(1.2)};
  c.empreintes[d.id] = d;
  poserEmpreinte(c, "J1", d.id, {nm(1.11), nm(15)});  // centres des trous sur le bord du bas
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  const auto drc = verifierDRC(c, k, r);
  for (const auto& v : drc)
    if (v.gravite == Gravite::Erreur) INFO(v.code + " " + v.message);
  CHECK(erreurs(drc) == 0);
  const ModeleFabrication m = modeleFabrication(c, k, r);
  CHECK(gerber(m, Couche::CuivreDessus, "essai").find("CastellatedPad") != std::string::npos);
  CHECK(excellon(m, true, "essai").find("CastellatedDrill") != std::string::npos);
  const auto job = nlohmann::json::parse(gerberJob(c, m, {}, "essai"));
  CHECK(job["GeneralSpecs"]["Castellated"] == true);
  CHECK(usinagesCarte(c).demiTrous == 8);
}

TEST_CASE("8.7 · Gerber X3 : composants d'une face avec .CRot, .CMnt, .CHgt, .CVal et leurs broches", "[034][L8][8.7]") {
  const ConceptionElec c = exempleElectronique();
  const std::string g = gerberComposants(c, false, "essai");
  CHECK(g.find("%TF.FileFunction,Component,L1,Top*%") != std::string::npos);
  for (const char* a : {"%TO.CRot,", "%TO.CMnt,", "%TO.CHgt,", "%TO.CVal,", "%TO.CFtp,", "%TO.P,"}) CHECK(g.find(a) != std::string::npos);
  CHECK(g.find("M02*") != std::string::npos);
}

TEST_CASE("5.25 · Plan partagé entre +3V3, +5V et +12V : trois régions sans recouvrement, chaque pastille dans celle de son réseau", "[034][L5][5.25]") {
  ConceptionElec c = nouvelleConception("Plans partagés");
  c.circuit.contour.largeur = nm(60);
  c.circuit.contour.hauteur = nm(40);
  c.circuit.empilage.couchesCuivre = 4;
  const char* reseaux[] = {"+3V3", "+5V", "+12V"};
  const Pt lieux[] = {ptMm(10, 10), ptMm(50, 10), ptMm(30, 32)};
  for (int i = 0; i < 3; ++i) {
    Via v;
    v.uuid = nouvelUuid();
    v.reseau = reseaux[i];
    v.p = lieux[i];
    c.circuit.vias.push_back(v);
    Zone z;
    z.uuid = nouvelUuid();
    z.reseau = reseaux[i];
    z.couche = Couche::CuivreInt2;
    z.contour = {ptMm(0, 0), ptMm(60, 0), ptMm(60, 40), ptMm(0, 40)};
    z.partition = "Alimentations";
    z.thermique = false;
    c.circuit.zones.push_back(z);
  }
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  for (std::size_t i = 0; i < 3; ++i) {
    const Polygones& f = r.at(c.circuit.zones[i].uuid);
    REQUIRE(!f.empty());
    CHECK(dedans(f, lieux[i]));
    for (std::size_t j = 0; j < 3; ++j)
      if (j != i) {
        CHECK_FALSE(dedans(f, lieux[j]));
        const Polygones commun = intersecter(f, r.at(c.circuit.zones[j].uuid));
        CHECK(aireMm2(commun) < 1e-6);
      }
  }
  // Pas de court-circuit entre les trois plans
  CHECK(etatRoutage(c, k, r).courtsCircuits.empty());
}

TEST_CASE("5.11, 5.26 · Zone hachurée, zone figée, zone mise de côté, pastille isolée du plan", "[034][L5][5.11][5.26]") {
  ConceptionElec c = exempleElectronique();
  REQUIRE(!c.circuit.zones.empty());
  Connectivite k = connectivite(c);
  const Remplissages plein = remplirZones(c, k);
  const std::string id = c.circuit.zones.front().uuid;
  const double airePleine = aireMm2(plein.at(id));
  c.circuit.zones.front().remplissage = RemplissageZone::Hachure;
  const Remplissages hachure = remplirZones(c, k);
  CHECK(aireMm2(hachure.at(id)) < 0.8 * airePleine);
  CHECK(aireMm2(hachure.at(id)) > 0.2 * airePleine);
  // Figée : son cuivre gardé ; mise de côté : vide
  c.circuit.zones.front().remplissage = RemplissageZone::Plein;
  c.circuit.zones.front().etat = EtatZone::Figee;
  c.circuit.zones.front().remplissageFige = {{ptMm(1, 1), ptMm(3, 1), ptMm(3, 3), ptMm(1, 3)}};
  CHECK(aireMm2(remplirZones(c, k).at(id)) == Approx(4.0));
  c.circuit.zones.front().etat = EtatZone::MiseDeCote;
  CHECK(remplirZones(c, k).at(id).empty());
  c.circuit.zones.front().etat = EtatZone::Normale;
  c.circuit.zones.front().remplissageFige.clear();
  // Une pastille du réseau de la zone qu'on isole : elle n'est plus reliée au plan (liaison au chevelu)
  const EtatRoutage avant = etatRoutage(c, k, plein);
  const std::string reseauZone = c.circuit.zones.front().reseau;
  bool fait = false;
  for (const auto& p : pastilles(c, k))
    if (!fait && p.reseau == reseauZone && (p.couches & bit(c.circuit.zones.front().couche)) && p.pastille.type == TypePastille::Traversante)
      for (auto& [eid, d] : c.empreintes)
        for (auto& pa : d.pastilles)
          if (!fait && eid == c.circuit.empreintes[static_cast<std::size_t>(p.empreinte)].empreinte && pa.numero == p.numero) {
            pa.connexion = ConnexionZone::Aucune;
            fait = true;
          }
  REQUIRE(fait);
  k = connectivite(c);
  const Remplissages isole = remplirZones(c, k);
  CHECK(etatRoutage(c, k, isole).chevelu.size() >= avant.chevelu.size());
}

TEST_CASE("0.34 · Tous les champs nouveaux du circuit relus sans perte", "[034][json]") {
  ConceptionElec c = exempleElectronique();
  Circuit& k = c.circuit;
  k.zonesRegles.push_back({nouvelUuid(), "Interdit", {ptMm(1, 1), ptMm(5, 1), ptMm(5, 5)}, bit(Couche::CuivreDessus), true, true, false, false, true, "Secteur", nm(5), "classe:Puissance", false, Verrou::Fige});
  k.regions.push_back({nouvelUuid(), "GND", "logo", Couche::CuivreDessus, {ptMm(2, 2), ptMm(4, 2), ptMm(3, 4)}, "", Verrou::Aucun});
  k.motifs.push_back({nouvelUuid(), "N1", GenreMotifAccord::Trombone, Couche::CuivreDessous, ptMm(1, 1), ptMm(9, 1), nm(0.2), nm(1), nm(0.5), nm(0.1), nm(30), false});
  k.groupes.push_back({nouvelUuid(), "Alim", "", {"a", "b"}, true});
  Cote cote;
  cote.uuid = nouvelUuid();
  cote.genre = GenreCote::Diametre;
  cote.points = {ptMm(10, 10), ptMm(11.6, 10)};
  k.cotes.push_back(cote);
  CHECK(texteCote(cote) == "⌀3,20 mm");
  k.affichage.origineTravail = ptMm(3, 4);
  k.affichage.yVersLeHaut = true;
  k.affichage.couleursReseaux["GND"] = "#202020";
  k.affichage.jeux.push_back({"Cuivre seul", bit(Couche::CuivreDessus), {{"zones", 0.3}}});
  k.affichage.images.push_back({"photo.png", 0, 0, 640, 0, ptMm(0, 0), ptMm(50, 0), 0.4, true});
  k.fabrication.procede = "vague";
  k.fabrication.densite = 'M';
  k.fabrication.margePate = -nm(0.05);
  k.contour.traitements.push_back({ContourCarte::Traitement::Genre::Biseau, 0, 0, 30, nm(0.5)});
  k.regles.classes.push_back({"Secteur", nm(3), nm(1), nm(1.2), nm(0.6), 0, 10, 230, "#E53935"});
  Contrainte ct;
  ct.nom = "USB";
  ct.genre = GenreContrainte::Largeur;
  REQUIRE(lireValeurContrainte("0,15/0,2", ct.valeur));
  ct.portee = "reseau ~ 'USB_D?'";
  k.regles.avancees.contraintes.push_back(ct);
  k.regles.avancees.paires.push_back({"USB", "USB_DP", "USB_DM", "", nm(0.2), nm(0.15), 0, 0, nm(5), 90});
  Piste& p = k.pistes.front();
  p.largeurFin = nm(0.6);
  p.verrou = Verrou::Protege;
  Zone& z = k.zones.front();
  z.nom = "GND dessous";
  z.remplissage = RemplissageZone::Hachure;
  z.partition = "P";
  const auto j = versJson(c);
  std::vector<std::string> alertes;
  const ConceptionElec r = conceptionDepuisJson(j, &alertes);
  // Le fichier range les objets dans un ordre stable (UUID) : la comparaison se fait champ par champ, objet par objet
  CHECK(r.circuit.zonesRegles == c.circuit.zonesRegles);
  CHECK(r.circuit.regions == c.circuit.regions);
  CHECK(r.circuit.motifs == c.circuit.motifs);
  CHECK(r.circuit.groupes == c.circuit.groupes);
  CHECK(r.circuit.cotes == c.circuit.cotes);
  CHECK(r.circuit.affichage == c.circuit.affichage);
  CHECK(r.circuit.fabrication == c.circuit.fabrication);
  CHECK(r.circuit.contour == c.circuit.contour);
  CHECK(r.circuit.regles == c.circuit.regles);
  auto memes = [](auto a, auto b) {
    auto parId = [](const auto& x, const auto& y) { return x.uuid < y.uuid; };
    std::sort(a.begin(), a.end(), parId);
    std::sort(b.begin(), b.end(), parId);
    return a == b;
  };
  CHECK(memes(r.circuit.pistes, c.circuit.pistes));
  CHECK(memes(r.circuit.vias, c.circuit.vias));
  CHECK(memes(r.circuit.zones, c.circuit.zones));
  CHECK(r.circuit.empreintes.size() == c.circuit.empreintes.size());
  CHECK(versJson(r).dump() == j.dump());
  CHECK(r.circuit.regles.avancees.contraintes.front().valeur.minPour(true) == nm(0.2));
}
