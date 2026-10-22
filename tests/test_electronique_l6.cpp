// Prométhée : essais du lot L6 (mécatronique). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <BRepPrimAPI_MakeBox.hxx>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/carte3d.hpp"
#include "promethee/contour_esquisse.hpp"
#include "promethee/derive.hpp"
#include "promethee/electronique.hpp"
#include "promethee/facade.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/import_cao.hpp"
#include "promethee/modele.hpp"
#include "promethee/modeles3d.hpp"

using namespace prom::ecad;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {
std::string poser(ConceptionElec& c, const std::string& symbole, double x, double y, double carteX, double carteY) {
  const std::string s = poserSymbole(c, 0, symbole, ptMm(x, y));
  std::string id;
  for (const auto& f : c.feuilles)
    for (const auto& p : f.symboles)
      if (p.uuid == s) id = p.composant;
  annoter(c, false);
  mettreAJourCircuit(c);
  for (auto& e : c.circuit.empreintes)
    if (e.composant == id) e.position = ptMm(carteX, carteY);
  return id;
}
ConceptionElec carte(double l, double h) {
  ConceptionElec c = nouvelleConception("Carte L6");
  c.circuit.contour.largeur = nm(l);
  c.circuit.contour.hauteur = nm(h);
  c.circuit.contour.rayon = 0;
  return c;
}
fs::path dossierEssai(const std::string& nom) {
  const fs::path d = fs::temp_directory_path() / ("promethee-l6-" + nom);
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
}  // namespace

TEST_CASE("6.4 : une self de 12 mm sous un couvercle à 10 mm est une erreur de la DRC qui vise le composant", "[L6][DRC-45][MEC-04]") {
  ConceptionElec c = carte(50, 30);
  const std::string l = poser(c, "L", 50, 50, 25, 15);
  c.composant(l)->hauteur = 12;
  c.composant(l)->provenanceHauteur = ProvenanceHauteur::Fiche;
  c.pilotageBoitier = true;
  prom::Projet p = prom::projetVide();
  p.sansCarte = false;
  p.electronique.push_back(c);
  p.boitier.hauteurAuto = false;
  p.boitier.hauteur = p.boitier.entretoise + 1.6 + 10;  // 10 mm au-dessus de la carte
  const auto v = prom::verifierCarteDansBoitier(p, 0);
  const auto it = std::find_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == "DRC-45"; });
  REQUIRE(it != v.end());
  CHECK(it->gravite == Gravite::Erreur);
  CHECK(it->objets == std::vector<std::string>{l});
  CHECK(it->position == ptMm(25, 15));
  // À 8 mm, elle passe
  p.electronique[0].composant(l)->hauteur = 8;
  const auto w = prom::verifierCarteDansBoitier(p, 0);
  CHECK(std::none_of(w.begin(), w.end(), [](const Violation& x) { return x.code == "DRC-45"; }));
  // Une carte qui ne pilote pas le boîtier : rien
  p.electronique[0].pilotageBoitier = false;
  CHECK(prom::verifierCarteDansBoitier(p, 0).empty());
}

TEST_CASE("6.2 : un modèle STEP associé à un régulateur remplace sa boîte ; sa hauteur devient celle du composant, « modèle exact »", "[L6][MEC-03][EMP-10]") {
  const fs::path d = dossierEssai("modele");
  // Un boîtier SOT-223 de 6,5 × 3,5 × 1,8 mm, décalé dans son propre repère comme le sont souvent les modèles
  prom::PieceNommee piece{"SOT-223", BRepPrimAPI_MakeBox(gp_Pnt(10, 20, 0.5), 6.5, 3.5, 1.8).Shape()};
  prom::exporterStep({piece}, (d / "SOT-223.step").string());
  std::string erreur;
  const auto m = prom::lireModele3D((d / "SOT-223.step").string(), &erreur);
  INFO(erreur);
  REQUIRE(m);
  CHECK(m->nom == "SOT-223.step");
  CHECK(m->z1 - m->z0 == Approx(1.8).margin(1e-3));
  ConceptionElec c = carte(50, 30);
  const std::string u = poser(c, "Regulateur", 50, 50, 25, 15);
  REQUIRE(c.composant(u)->empreinte == "SOT-223");
  const std::string id = associerModele(c, u, *m);
  REQUIRE_FALSE(id.empty());
  const HauteurComposant h = hauteurComposant(c, *c.composant(u));
  CHECK(h.provenance == ProvenanceHauteur::ModeleExact);
  CHECK(h.mm == Approx(1.8).margin(1e-3));
  // Le corps 3D est le modèle : sa boîte, posée sur la carte, centrée sur l'encombrement de l'empreinte
  const EmpreintePlacee* e = nullptr;
  for (const auto& x : c.circuit.empreintes)
    if (x.composant == u) e = &x;
  REQUIRE(e);
  const prom::Boite b = prom::encombrement(prom::corpsComposant(c, *e));
  CHECK(b.zmax - b.zmin == Approx(1.8).margin(1e-3));
  const Boite enc = c.empreinte("SOT-223")->encombrement;
  const Pt centre = versCarte(*e, enc.centre());
  CHECK((b.xmin + b.xmax) / 2 == Approx(mm(centre.x)).margin(0.01));
  CHECK((b.ymin + b.ymax) / 2 == Approx(-mm(centre.y)).margin(0.01));
  // Le fichier du projet garde le modèle
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  REQUIRE(relue.modeles3D.count(id));
  CHECK(relue.composant(u)->modele3D == id);
  CHECK(hauteurComposant(relue, *relue.composant(u)).provenance == ProvenanceHauteur::ModeleExact);
}

TEST_CASE("6.10 : un dossier de modèles se rapproche des empreintes, chaque modèle calé sur la sienne", "[L6][EMP-29]") {
  ConceptionElec c = carte(50, 30);
  const std::string r = poser(c, "R", 50, 50, 10, 10);
  const std::string u = poser(c, "Regulateur", 80, 50, 25, 15);
  const std::string q = poser(c, "Quartz", 110, 50, 40, 15);
  const std::string fichierR = "/modeles/" + c.composant(r)->empreinte + "_3D.step";
  const auto paires = rapprocherModeles(c, {fichierR, "/modeles/SOT-223.stp", "/modeles/autre.wrl"});
  CHECK(paires.size() == 2);
  CHECK(paires.at(r) == fichierR);
  CHECK(paires.at(u) == "/modeles/SOT-223.stp");
  CHECK_FALSE(paires.count(q));
  // Calage : un modèle allongé en y sur une empreinte allongée en x tourne d'un quart de tour
  Modele3D m;
  m.x0 = 0, m.x1 = 1.25, m.y0 = 0, m.y1 = 2.0, m.z0 = 0, m.z1 = 0.5;
  CHECK(calerModele(*c.empreinte(c.composant(r)->empreinte), m).quarts == 1);
  std::swap(m.x1, m.y1);
  CHECK(calerModele(*c.empreinte(c.composant(r)->empreinte), m).quarts == 0);
}

TEST_CASE("6.9 : la carte équipée sort en STEP, un produit nommé par composant", "[L6][MEC-07][T10]") {
  ConceptionElec c = carte(50, 30);
  poser(c, "R", 50, 50, 10, 10);
  poser(c, "C", 80, 50, 25, 15);
  const fs::path d = dossierEssai("step");
  const std::string f = (d / "carte.step").string();
  prom::exporterStepCarte(c, f);
  const prom::ImportCAO imp = prom::lireCAOStructure(f);
  std::vector<std::string> noms;
  for (const auto& p : imp.produits) noms.push_back(p.nom);
  INFO(noms.size());
  CHECK(std::any_of(noms.begin(), noms.end(), [](const std::string& n) { return n.rfind("R1", 0) == 0; }));
  CHECK(std::any_of(noms.begin(), noms.end(), [](const std::string& n) { return n.rfind("C1", 0) == 0; }));
  CHECK(imp.produits.size() == 3);
}

TEST_CASE("6.16 : un boîtier en coques haute et basse sort en deux STL posés à plat, avec volume, masse et coût d'impression", "[L6][MEC-28]") {
  prom::Projet p = prom::exemple();
  p.boitier.type = "coques";
  const auto pieces = prom::piecesImpression(p);
  REQUIRE(pieces.size() == 2);
  CHECK(pieces[0].nom == "coque basse");
  CHECK(pieces[1].nom == "coque haute");
  const prom::Derive d = prom::deriver(p);
  const double total = prom::volume(prom::construireCorps(p, d)) + prom::volume(prom::construireCouvercle(p, d));
  CHECK(pieces[0].volumeMm3 + pieces[1].volumeMm3 == Approx(total).epsilon(0.01));
  const fs::path dossier = dossierEssai("stl");
  for (const auto& x : pieces) {
    CHECK(prom::encombrement(x.forme).zmin == Approx(0).margin(1e-6));
    CHECK(x.masseG == Approx(x.volumeMm3 / 1000 * 1.27));
    CHECK(x.cout > 0);
    const fs::path f = dossier / (x.nom + ".stl");
    prom::exporterStl(x.forme, f.string());
    CHECK(fs::file_size(f) > 1000);
  }
  // Le fichier garde le type et la matière
  p.boitier.impression.matiere = "PLA";
  p.boitier.impression.densite = 1.24;
  const prom::Projet relu = prom::normaliser(prom::versJson(p));
  CHECK(relu.boitier.type == "coques");
  CHECK(relu.boitier.impression.matiere == "PLA");
  CHECK(relu.boitier.impression.densite == Approx(1.24));
}

TEST_CASE("6.14 : potentiomètre déplacé de 10 mm : sa découpe, sa légende et son échelle suivent ; contrôle, SVG, DXF", "[L6][MEC-24][MEC-25]") {
  ConceptionElec c = carte(80, 50);
  const std::string rv = poser(c, "Potentiometre", 50, 50, 30, 25);
  Facade f;
  f.uuid = nouvelUuid();
  f.elements = elementsPour(c, rv, true);
  REQUIRE(f.elements.size() == 3);
  std::vector<PositionFacade> avant;
  for (const auto& e : f.elements) avant.push_back(positionElement(c, e));
  for (auto& e : c.circuit.empreintes)
    if (e.composant == rv) e.position = e.position + ptMm(10, 0);
  for (std::size_t i = 0; i < f.elements.size(); ++i) {
    const PositionFacade p = positionElement(c, f.elements[i]);
    CHECK(p.x - avant[i].x == Approx(10));
    CHECK(p.y - avant[i].y == Approx(0));
  }
  CHECK(controlerFacade(c, f).empty());
  const std::string svg = svgFacade(c, f);
  CHECK(svg.find("<circle") != std::string::npos);
  CHECK(svg.find("<line") != std::string::npos);
  const std::string dxf = dxfFacade(c, f);
  CHECK(dxf.find("CIRCLE") != std::string::npos);
  CHECK(dxf.find("GRAVURE") != std::string::npos);
  // Deux découpes à 1,5 mm l'une de l'autre : refusé, l'écart nommé
  ElementFacade autre = f.elements.front();
  autre.uuid = nouvelUuid();
  autre.dx = 7 + 1.5;
  f.elements.push_back(autre);
  const auto defauts = controlerFacade(c, f);
  REQUIRE(defauts.size() == 1);
  CHECK(defauts.front().message.find("1.50") != std::string::npos);
  CHECK(defauts.front().elements.size() == 2);
  // Le fichier garde la façade
  c.facades.push_back(f);
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  REQUIRE(relue.facades.size() == 1);
  CHECK(relue.facades.front() == f);
}

TEST_CASE("6.13 : le contour tracé dans l'esquisse pilote la carte : 80 → 90 mm, les trous de fixation suivent leur bord", "[L6][MEC-29]") {
  ConceptionElec c = carte(80, 50);
  const std::string h1 = poser(c, "Trou_fixation", 20, 20, 4, 4), h2 = poser(c, "Trou_fixation", 40, 20, 76, 4);
  const std::string h3 = poser(c, "Trou_fixation", 60, 20, 76, 46), h4 = poser(c, "Trou_fixation", 80, 20, 4, 46);
  const prom::Esquisse e = prom::esquisseDeCarte(c);
  c.esquisseContour = prom::esquisseVersJson(e);
  CHECK(prom::appliquerEsquisseContour(c).empty());
  CHECK(mm(boite(c.circuit.contour.polygone()).largeur()) == Approx(80).margin(1e-3));
  const auto cotes = prom::cotesContour(c);
  const auto it = std::find_if(cotes.begin(), cotes.end(), [](const prom::CoteContour& x) { return std::abs(x.valeur - 80) < 1e-6; });
  REQUIRE(it != cotes.end());
  std::vector<std::string> remarques;
  REQUIRE(prom::changerCoteContour(c, it->contrainte, 90, &remarques));
  CHECK(remarques.empty());
  CHECK(mm(boite(c.circuit.contour.polygone()).largeur()) == Approx(90).margin(1e-3));
  CHECK(mm(boite(c.circuit.contour.polygone()).hauteur()) == Approx(50).margin(1e-3));
  auto pos = [&](const std::string& id) {
    for (const auto& x : c.circuit.empreintes)
      if (x.composant == id) return x.position;
    return Pt{};
  };
  CHECK(mm(pos(h1).x) == Approx(4).margin(1e-3));
  CHECK(mm(pos(h2).x) == Approx(86).margin(1e-3));
  CHECK(mm(pos(h3).x) == Approx(86).margin(1e-3));
  CHECK(mm(pos(h4).x) == Approx(4).margin(1e-3));
  CHECK(mm(pos(h3).y) == Approx(46).margin(1e-3));
  // Le fichier garde l'esquisse
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  CHECK(prom::cotesContour(relue).size() == cotes.size());
}
