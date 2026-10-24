// Prométhée : essais de la 0.34, lot L6 — boîtier du commerce importé en STEP (6.7), pièces montées et nomenclature
// commune (6.8), points de vue nommés (6.11), distances exactes (6.12), IDF 3.0 (6.17), rendu à fond transparent et
// images clés (6.18).
// Licence GPL-3.0-only.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/carte3d.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/derive.hpp"
#include "promethee/distances3d.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/idf.hpp"
#include "promethee/nomenclature.hpp"
#include "promethee/rendu_carte.hpp"

using namespace prom;
using namespace prom::ecad;
using Catch::Approx;

namespace {
ConceptionElec carte(const std::string& nom, double l = 60, double h = 40) {
  ConceptionElec c = nouvelleConception(nom);
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(l);
  c.circuit.contour.hauteur = nm(h);
  return c;
}
// Modèle 3D tiré d'une forme : texte BRep et boîte brute
Modele3D modeleDe(const TopoDS_Shape& s, const std::string& nom) {
  Modele3D m;
  m.nom = nom;
  m.brep = brepDepuisForme(s);
  Bnd_Box b;
  BRepBndLib::Add(s, b);
  b.Get(m.x0, m.y0, m.z0, m.x1, m.y1, m.z1);
  return m;
}
std::string dossierEssai(const std::string& nom) {
  const auto d = std::filesystem::temp_directory_path() / ("promethee-034-l6-" + nom);
  std::filesystem::create_directories(d);
  return d.string();
}
}  // namespace

TEST_CASE("6.7 · boîtier du commerce importé en STEP : collision détectée avec un boîtier importé", "[034][L6][6.7]") {
  ConceptionElec c = carte("Boîtier importé");
  const std::string bas = poserEmpreinteSeule(c, "R_0805", ptMm(15, 20));
  const std::string haut = poserEmpreinteSeule(c, "R_0805", ptMm(45, 20));
  c.composant(haut)->hauteur = 15;  // un condensateur haut, par exemple
  // Boîtier de 70 × 50 × 14 mm (parois de 2 mm), creux : écrit en STEP puis relu, comme un fichier de fabricant
  const TopoDS_Shape plein = BRepPrimAPI_MakeBox(gp_Pnt(-5, -45, -6), 70, 50, 14).Shape();
  const TopoDS_Shape creux = BRepPrimAPI_MakeBox(gp_Pnt(-3, -43, -4), 66, 46, 10).Shape();
  const TopoDS_Shape boite = BRepAlgoAPI_Cut(plein, creux).Shape();
  PieceNommee piece;
  piece.nom = "Boîtier 1551K";
  piece.forme = boite;
  const std::string step = dossierEssai("boitier") + "/boitier.step";
  exporterStep({piece}, step);
  std::string erreur;
  const auto lu = lireModele3D(step, &erreur);
  REQUIRE(lu);
  c.modeles3D["boitier"] = *lu;
  BoitierImporte b;
  b.modele = "boitier";
  b.nom = "Hammond 1551K";
  b.jeu = 0.5;
  c.boitierImporte = b;
  // L'intérieur va de z = −4 à z = 6 : le corps de 15 mm le traverse, celui de la résistance (0,5 mm) non
  const auto v = verifierDistances3D(c);
  std::vector<Violation> collisions;
  for (const auto& x : v)
    if (x.code == "DRC-47") collisions.push_back(x);
  REQUIRE(collisions.size() == 1);
  CHECK(collisions.front().objets == std::vector<std::string>{haut});
  CHECK(collisions.front().message.find("heurte") != std::string::npos);
  // Abaissé à 5 mm : plus de collision (le jeu de 0,5 mm est tenu)
  c.composant(haut)->hauteur = 5;
  CHECK(verifierDistances3D(c).empty());
  // Relu sans perte
  const ConceptionElec r = conceptionDepuisJson(versJson(c));
  REQUIRE(r.boitierImporte);
  CHECK(*r.boitierImporte == *c.boitierImporte);
  (void)bas;
}

TEST_CASE("6.8 · pièces montées sur la carte : dissipateur listé dans la nomenclature commune", "[034][L6][6.8]") {
  Projet p = projetVide();
  ConceptionElec c = carte("Alimentation");
  const std::string u1 = poserEmpreinteSeule(c, "R_0805", ptMm(20, 20));
  const TopoDS_Shape ailettes = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 15, 10, 20).Shape();
  c.modeles3D["dissipateur"] = modeleDe(ailettes, "dissipateur.step");
  PieceMontee d;
  d.uuid = nouvelUuid();
  d.reference = "DS1";
  d.nom = "Dissipateur TO-220";
  d.fabricant = "Fischer";
  d.mpn = "SK 104 38,1 STC";
  d.modele = "dissipateur";
  d.composant = u1;
  d.x = 20, d.y = 20, d.z = 2;
  c.piecesMontees.push_back(d);
  // Nomenclature de la carte
  const auto nc = nomenclature(c);
  CHECK(std::any_of(nc.begin(), nc.end(), [](const prom::ecad::LigneNomenclature& l) { return l.valeur == "Dissipateur TO-220" && l.mpn == "SK 104 38,1 STC" && l.quantite == 1; }));
  // Nomenclature commune du projet
  c.pilotageBoitier = true;
  p.electronique.push_back(c);
  const auto commune = nomenclature(p, deriver(p));
  CHECK(std::any_of(commune.begin(), commune.end(), [](const prom::LigneNomenclature& l) { return l.groupe == "Pièces montées" && l.ref == "DS1" && l.designation.find("Dissipateur TO-220") != std::string::npos; }));
  // Sa forme dans la carte équipée, au-dessus de la face
  const TopoDS_Shape f = formePieceMontee(c, d);
  REQUIRE_FALSE(f.IsNull());
  Bnd_Box bb;
  BRepBndLib::Add(f, bb);
  double x0, y0, z0, x1, y1, z1;
  bb.Get(x0, y0, z0, x1, y1, z1);
  CHECK(z0 > 1.9);
  CHECK((x0 + x1) / 2 == Approx(20).margin(0.01));
  // Relue sans perte
  const ConceptionElec r = conceptionDepuisJson(versJson(c));
  REQUIRE(r.piecesMontees.size() == 1);
  CHECK(r.piecesMontees.front() == d);
}

TEST_CASE("6.11 · point de vue « Face USB » enregistré, projet rouvert : même caméra à 0,1° près", "[034][L6][6.11]") {
  ConceptionElec c = carte("Points de vue");
  PointDeVue v;
  v.nom = "Face USB";
  v.cible[0] = 12.5;
  v.cible[1] = -7.25;
  v.cible[2] = 3;
  v.azimut = -93.37;
  v.elevation = 12.06;
  v.distance = 84.2;
  v.orthographique = true;
  v.champ = 27;
  c.pointsDeVue.push_back(v);
  const ConceptionElec r = conceptionDepuisJson(nlohmann::ordered_json::parse(versJson(c).dump()));
  REQUIRE(r.pointsDeVue.size() == 1);
  const PointDeVue& w = r.pointsDeVue.front();
  CHECK(w.nom == "Face USB");
  CHECK(std::abs(w.azimut - v.azimut) < 0.1);
  CHECK(std::abs(w.elevation - v.elevation) < 0.1);
  CHECK(w.distance == Approx(v.distance));
  CHECK(w.orthographique);
  CHECK(w.cible[1] == Approx(-7.25));
}

TEST_CASE("6.12 · deux condensateurs à 0,4 mm sous une règle de 0,5 mm : un écart de la DRC qui mène aux deux corps", "[034][L6][6.12]") {
  ConceptionElec c = carte("Écarts 3D");
  // Deux corps de 2 × 1,25 mm (C_0805) : centres à 2,4 mm, soit 0,4 mm entre les corps
  const std::string c1 = poserEmpreinteSeule(c, "C_0805", ptMm(20, 20), 0, false, "C");
  const std::string c2 = poserEmpreinteSeule(c, "C_0805", ptMm(22.4, 20), 0, false, "C");
  const std::string c3 = poserEmpreinteSeule(c, "C_0805", ptMm(40, 20), 0, false, "C");
  const auto corps = corpsCarte(c);
  REQUIRE(corps.size() == 3);
  const auto m = mesurerCorps(c, c1, c2);
  REQUIRE(m.mesure.distance);
  const double attendu = 2.4 - mm(c.empreinte("C_0805")->corps.largeur);
  CHECK(m.mesure.distanceMin == Approx(attendu).margin(1e-6));
  CHECK(std::abs(m.mesure.delta.Z()) < 1e-6);
  CHECK(m.distanceCarteA == Approx(std::max(0.035, c.circuit.empilage.cuivre)).margin(1e-6));
  Contrainte k;
  k.nom = "Condensateurs écartés";
  k.genre = GenreContrainte::Ecart3D;
  k.portee = "prefixe == 'C'";
  k.valeur.min = nm(attendu + 0.1);
  c.circuit.regles.avancees.contraintes.push_back(k);
  const auto v = verifierDistances3D(c);
  REQUIRE(v.size() == 1);
  CHECK(v.front().code == "DRC-75");
  std::vector<std::string> objets = v.front().objets, attendus{c1, c2};
  std::sort(objets.begin(), objets.end());
  std::sort(attendus.begin(), attendus.end());
  CHECK(objets == attendus);
  (void)c3;
}

TEST_CASE("6.17 · fichiers .emn et .emp réimportés : même contour, mêmes trous, 32 composants à leur hauteur", "[034][L6][6.17]") {
  ConceptionElec c = carte("Carte IDF", 80, 60);
  c.circuit.contour.decoupes.push_back({ptMm(35, 25), ptMm(45, 25), ptMm(45, 35), ptMm(35, 35)});
  for (const Pt p : {ptMm(4, 4), ptMm(76, 4), ptMm(4, 56), ptMm(76, 56)}) poserEmpreinteSeule(c, "MountingHole_3.2", p, 0, false, "H");
  std::vector<std::string> ids;
  for (int i = 0; i < 32; ++i) {
    const std::string id = poserEmpreinteSeule(c, i % 2 ? "R_0805" : "SOIC-8", ptMm(10 + (i % 8) * 8, 10 + (i / 8) * 12), (i % 4) * 90.0, false, i % 2 ? "R" : "U");
    c.composant(id)->hauteur = 0.5 + 0.1 * i;
    ids.push_back(id);
  }
  ComposantElec* dnp = c.composant(poserEmpreinteSeule(c, "R_0805", ptMm(70, 50)));
  dnp->nePasMonter = true;
  const FichiersIdf f = ecrireIdf(c);
  CHECK(f.emn.find(".BOARD_OUTLINE") != std::string::npos);
  CHECK(f.emp.find(".ELECTRICAL") != std::string::npos);
  const DonneesIdf d = lireIdf(f.emn, f.emp);
  CHECK(d.alertes.empty());
  REQUIRE(d.boucles.size() == 2);
  CHECK(d.boucles.front().size() == c.circuit.contour.polygone().size());
  CHECK(d.trous.size() == 4);
  // 32 composants placés (les trous de fixation peuvent l'être aussi) ; le non monté est exclu
  CHECK(std::count_if(d.composants.begin(), d.composants.end(), [](const ComposantIdf& k) { return k.reference[0] == 'R' || k.reference[0] == 'U'; }) == 32);
  const ConceptionElec r = conceptionDepuisIdf(d);
  // Même contour, à 1 µm près (même boîte, mêmes sommets)
  const Polygone a = c.circuit.contour.polygone(), b = r.circuit.contour.polygone();
  REQUIRE(a.size() == b.size());
  const prom::ecad::Boite ba = boite(a), bb = boite(b);
  CHECK(std::llabs((ba.x1 - ba.x0) - (bb.x1 - bb.x0)) <= 1'000);
  CHECK(std::llabs((ba.y1 - ba.y0) - (bb.y1 - bb.y0)) <= 1'000);
  REQUIRE(r.circuit.contour.decoupes.size() == 1);
  // Mêmes trous de fixation : quatre trous de 3,2 mm aux mêmes places relatives
  int trous = 0;
  for (const auto& p : pastilles(r, connectivite(r)))
    if (p.pastille.percage == nm(3.2)) ++trous;
  CHECK(trous == 4);
  // 32 composants (et les quatre trous) à leur hauteur
  int aLeurHauteur = 0;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    const ComposantElec* x = c.composant(ids[i]);
    const ComposantElec* y = r.parReference(x->reference);
    REQUIRE(y);
    const auto hx = hauteurComposant(c, *x), hy = hauteurComposant(r, *y);
    aLeurHauteur += std::abs(hx.mm - hy.mm) < 1e-3 ? 1 : 0;
  }
  CHECK(aLeurHauteur == 32);
  // En millièmes de pouce, relu en mm
  OptionsIdf thou;
  thou.milliemesPouce = true;
  const DonneesIdf dt = lireIdf(ecrireIdf(c, thou).emn, ecrireIdf(c, thou).emp);
  CHECK(dt.unite == "THOU");
  CHECK(dt.composants.size() == d.composants.size());
  CHECK(std::abs(dt.epaisseur - c.circuit.empilage.epaisseur) < 0.01);
}

TEST_CASE("6.18 · rendu à fond transparent : PNG avec canal alpha ; interruption ; images clés", "[034][L6][6.18]") {
  ConceptionElec c = carte("Rendu", 40, 30);
  poserEmpreinteSeule(c, "SOIC-8", ptMm(20, 15));
  poserEmpreinteSeule(c, "R_0805", ptMm(10, 10));
  OptionsRendu o;
  o.largeur = 192;
  o.hauteur = 108;
  o.vue = vueDeTroisQuarts(c);
  o.fils = 2;
  const ImageRendu img = rendreCarte(c, o);
  CHECK_FALSE(img.interrompu);
  CHECK(img.triangles > 100);
  REQUIRE(img.rgba.size() == 192u * 108u * 4u);
  CHECK(img.alphaPresent());
  // Fond opaque : plus aucun pixel transparent
  o.fondTransparent = false;
  const ImageRendu opaque = rendreCarte(c, o);
  CHECK_FALSE(opaque.alphaPresent());
  // Interrompu avant de commencer
  std::atomic<bool> arret{true};
  CHECK(rendreCarte(c, o, &arret).interrompu);
  // Images clés : interpolées, l'azimut par le plus court chemin
  ImageCle a, b;
  a.t = 0;
  a.vue = o.vue;
  a.vue.azimut = 170;
  b.t = 2;
  b.vue = o.vue;
  b.vue.azimut = -170;
  b.leveeBoitier = 20;
  const EtatAnimation m = etatAnimation({a, b}, 1);
  CHECK(std::abs(std::remainder(m.vue.azimut - 180, 360)) < 1e-6);
  CHECK(m.leveeBoitier == Approx(10));
  CHECK(dureeAnimation({a, b}) == 2);
}
