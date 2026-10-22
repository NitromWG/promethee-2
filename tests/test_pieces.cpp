// Tests du lot 1.B : esquisses, solveur de contraintes, pièces paramétriques et nommage topologique.
// Licence GPL-3.0-only.
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cmath>
#include <numbers>

#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include "promethee/depot.hpp"
#include "promethee/construction.hpp"
#include "promethee/esquisse.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/piece.hpp"
#include "promethee/unites.hpp"

using namespace prom;
using Catch::Approx;

namespace {
constexpr double PI = std::numbers::pi;

// Rectangle L × W dont le coin bas gauche est à l'origine, entièrement contraint.
Esquisse rectangle(double L, double W, std::vector<int>* lignes = nullptr, int* idLongueur = nullptr) {
  Esquisse e;
  const auto l = e.ajouterRectangle(0.3, -0.2, L * 0.8, W * 1.1);  // tracé approximatif, comme à la souris
  const int coin = e.element(l[0])->p1;
  e.contraindre(TypeContrainte::Coincidence, {coin, 0});
  const int c = e.contraindre(TypeContrainte::Longueur, {l[0]}, L);
  e.contraindre(TypeContrainte::Longueur, {l[1]}, W);
  if (lignes) *lignes = l;
  if (idLongueur) *idLongueur = c;
  return e;
}
}  // namespace

TEST_CASE("Un rectangle tracé à la main devient exact une fois contraint") {
  std::vector<int> l;
  Esquisse e = rectangle(60, 40, &l);
  const EtatEsquisse s = resoudre(e);
  REQUIRE(s.resolue);
  CHECK(s.degresLiberte == 0);
  CHECK(s.pointsLibres.empty());
  CHECK(s.enConflit.empty());
  const PointEsquisse* oppose = e.point(e.element(l[1])->p2);
  CHECK(oppose->x == Approx(60).margin(1e-7));
  CHECK(oppose->y == Approx(40).margin(1e-7));
  CHECK(s.message == "Esquisse entièrement contrainte.");
}

TEST_CASE("Les degrés de liberté, les redondances et les conflits sont signalés") {
  Esquisse e;
  const auto l = e.ajouterRectangle(0, 0, 50, 30);
  EtatEsquisse s = resoudre(e);
  CHECK(s.degresLiberte == 4);  // position (2) et dimensions (2)
  CHECK(s.pointsLibres.size() == 4);
  // Une cinquième contrainte d'horizontalité sur une ligne déjà horizontale est redondante…
  e.contraindre(TypeContrainte::Parallele, {l[0], l[2]});
  s = resoudre(e);
  CHECK(s.resolue);
  CHECK_FALSE(s.redondantes.empty());
  // … et une ligne à la fois horizontale et verticale, de longueur non nulle, est impossible.
  Esquisse f;
  const int a = f.ajouterPoint(0, 0), b = f.ajouterPoint(10, 3);
  const int ligne = f.ajouterLigne(a, b);
  f.contraindre(TypeContrainte::Horizontal, {ligne});
  f.contraindre(TypeContrainte::Vertical, {ligne});
  f.contraindre(TypeContrainte::Longueur, {ligne}, 10);
  s = resoudre(f);
  CHECK_FALSE(s.resolue);
  CHECK_FALSE(s.enConflit.empty());
}

TEST_CASE("Tangence, angles, cercles et arcs se résolvent") {
  Esquisse e;
  const int a = e.ajouterPoint(0, 0), b = e.ajouterPoint(40, 1);
  const int base = e.ajouterLigne(a, b);
  const int c = e.ajouterPoint(20, 12);
  const int cercle = e.ajouterCercle(c, 7);
  e.contraindre(TypeContrainte::Coincidence, {a, 0});
  e.contraindre(TypeContrainte::Horizontal, {base});
  e.contraindre(TypeContrainte::Tangente, {base, cercle});
  e.contraindre(TypeContrainte::Rayon, {cercle}, 10);
  e.contraindre(TypeContrainte::DistanceX, {c}, 15);
  const EtatEsquisse s = resoudre(e);
  REQUIRE(s.resolue);
  CHECK(e.point(c)->y == Approx(10).margin(1e-7));  // tangent à la droite y = 0, rayon 10
  CHECK(e.element(cercle)->rayon == Approx(10).margin(1e-7));
  // Angle de 30° entre deux lignes issues de l'origine.
  Esquisse g;
  const int o = g.ajouterPoint(0.1, 0.1), p = g.ajouterPoint(10, 0.5), q = g.ajouterPoint(8, 6);
  const int l1 = g.ajouterLigne(o, p), l2 = g.ajouterLigne(o, q);
  g.contraindre(TypeContrainte::Coincidence, {o, 0});
  g.contraindre(TypeContrainte::Horizontal, {l1});
  g.contraindre(TypeContrainte::Angle, {l1, l2}, 30);
  g.contraindre(TypeContrainte::Longueur, {l2}, 10);
  REQUIRE(resoudre(g).resolue);
  CHECK(g.point(q)->x == Approx(10 * std::cos(PI / 6)).margin(1e-7));
  CHECK(g.point(q)->y == Approx(10 * std::sin(PI / 6)).margin(1e-7));
  // Un arc garde ses deux extrémités à la même distance du centre.
  Esquisse h;
  const int ce = h.ajouterPoint(0, 0), d = h.ajouterPoint(10, 0), f = h.ajouterPoint(0, 12);
  h.ajouterArc(ce, d, f);
  REQUIRE(resoudre(h).resolue);
  CHECK(std::hypot(h.point(f)->x - h.point(ce)->x, h.point(f)->y - h.point(ce)->y) ==
        Approx(std::hypot(h.point(d)->x - h.point(ce)->x, h.point(d)->y - h.point(ce)->y)).margin(1e-7));
}

TEST_CASE("Glisser un point déplace la géométrie sans casser les contraintes") {
  Esquisse e;
  const auto l = e.ajouterRectangle(0, 0, 50, 30);
  e.contraindre(TypeContrainte::Coincidence, {e.element(l[0])->p1, 0});
  const int coin = e.element(l[1])->p2;
  const EtatEsquisse s = resoudre(e, {{coin, Point{70, 45}}});
  REQUIRE(s.resolue);
  CHECK(e.point(coin)->x == Approx(70).margin(1e-6));
  CHECK(e.point(coin)->y == Approx(45).margin(1e-6));
  // Les lignes restent horizontales et verticales.
  CHECK(e.point(e.element(l[0])->p2)->y == Approx(0).margin(1e-7));
  CHECK(e.point(e.element(l[3])->p1)->x == Approx(0).margin(1e-7));
}

TEST_CASE("Les profils fermés sont reconnus, trous compris") {
  Esquisse e = rectangle(60, 40);
  resoudre(e);
  const int c = e.ajouterPoint(30, 20);
  e.ajouterCercle(c, 5);
  std::string err;
  const auto b = boucles(e, &err);
  CHECK(err.empty());
  CHECK(b.size() == 2);
  // Un profil ouvert est expliqué.
  Esquisse o;
  const int a1 = o.ajouterPoint(0, 0), a2 = o.ajouterPoint(10, 0), a3 = o.ajouterPoint(10, 10);
  o.ajouterLigne(a1, a2);
  o.ajouterLigne(a2, a3);
  CHECK(boucles(o, &err).empty());
  CHECK(err.find("ouvert") != std::string::npos);
}

TEST_CASE("Une plaque percée et arrondie se construit, et ses congés survivent à un changement de cote") {
  Piece p;
  FonctionPiece esq;
  esq.type = TypeFonction::Esquisse;
  std::vector<int> lignes;
  int cote = 0;
  esq.esquisse = rectangle(60, 40, &lignes, &cote);
  resoudre(esq.esquisse);
  const int idEsq = p.ajouter(esq);
  FonctionPiece ext;
  ext.type = TypeFonction::Extrusion;
  ext.esquisseId = idEsq;
  ext.longueur = 5;
  const int idExt = p.ajouter(ext);
  ResultatPiece r = construirePiece(p);
  REQUIRE(r.erreurs.empty());
  CHECK(volume(r.forme) == Approx(60 * 40 * 5).epsilon(1e-9));
  // Trou traversant de Ø 10 au milieu.
  FonctionPiece esq2;
  esq2.type = TypeFonction::Esquisse;
  const int centre = esq2.esquisse.ajouterPoint(30, 20);
  esq2.esquisse.ajouterCercle(centre, 5);
  const int idEsq2 = p.ajouter(esq2);
  FonctionPiece trou;
  trou.type = TypeFonction::Extrusion;
  trou.esquisseId = idEsq2;
  trou.enlever = true;
  trou.traversant = true;
  p.ajouter(trou);
  r = construirePiece(p);
  REQUIRE(r.erreurs.empty());
  CHECK(volume(r.forme) == Approx(60 * 40 * 5 - PI * 25 * 5).epsilon(1e-6));
  // Les quatre arêtes verticales sont nommées par les faces latérales qu'elles séparent.
  std::vector<std::string> refs;
  for (size_t i = 0; i < 4; ++i) {
    const std::string a = std::to_string(idExt) + ":cote:" + std::to_string(lignes[i]);
    const std::string b = std::to_string(idExt) + ":cote:" + std::to_string(lignes[(i + 1) % 4]);
    refs.push_back(a < b ? a + "|" + b : b + "|" + a);
  }
  for (const auto& ref : refs) CHECK(r.aretes(ref).size() == 1);
  CHECK(r.nomArete(r.aretes(refs[0]).front()) == refs[0]);
  FonctionPiece conge;
  conge.type = TypeFonction::Conge;
  conge.references = refs;
  conge.rayon = 4;
  const int idConge = p.ajouter(conge);
  r = construirePiece(p);
  REQUIRE(r.erreurs.empty());
  const double retrait = 4 * (16 - PI * 16 / 4) * 5;
  CHECK(volume(r.forme) == Approx(60 * 40 * 5 - PI * 25 * 5 - retrait).epsilon(1e-5));
  // On allonge la plaque à 80 mm : l'arbre se rejoue et le congé retrouve ses quatre arêtes.
  for (auto& c : p.fonction(idEsq)->esquisse.contraintes) if (c.id == cote) c.valeur = 80;
  REQUIRE(resoudre(p.fonction(idEsq)->esquisse).resolue);
  r = construirePiece(p);
  CHECK(r.erreur(idConge).empty());
  CHECK(volume(r.forme) == Approx(80 * 40 * 5 - PI * 25 * 5 - retrait).epsilon(1e-5));
  CHECK(estValide(r.forme));
  CHECK(nombreSolides(r.forme) == 1);
}

TEST_CASE("Chanfrein, coque et désactivation d'une fonction") {
  Piece p;
  FonctionPiece esq;
  esq.type = TypeFonction::Esquisse;
  std::vector<int> l;
  esq.esquisse = rectangle(50, 30, &l);
  resoudre(esq.esquisse);
  const int idEsq = p.ajouter(esq);
  FonctionPiece ext;
  ext.type = TypeFonction::Extrusion;
  ext.esquisseId = idEsq;
  ext.longueur = 20;
  const int idExt = p.ajouter(ext);
  // Boîte évidée par le dessus : la face « sommet » est retirée, parois de 2 mm.
  FonctionPiece coque;
  coque.type = TypeFonction::Coque;
  coque.references = {std::to_string(idExt) + ":sommet"};
  coque.epaisseur = 2;
  const int idCoque = p.ajouter(coque);
  ResultatPiece r = construirePiece(p);
  REQUIRE(r.erreur(idCoque).empty());
  CHECK(volume(r.forme) == Approx(50 * 30 * 20 - 46 * 26 * 18).epsilon(1e-6));
  // Chanfrein de 1 mm sur l'arête entre le fond et le côté avant.
  const std::string a = std::to_string(idExt) + ":base", b = std::to_string(idExt) + ":cote:" + std::to_string(l[0]);
  FonctionPiece ch;
  ch.type = TypeFonction::Chanfrein;
  ch.references = {a < b ? a + "|" + b : b + "|" + a};
  ch.distance = 1;
  const int idCh = p.ajouter(ch);
  r = construirePiece(p);
  CHECK(r.erreur(idCh).empty());
  CHECK(volume(r.forme) < 50 * 30 * 20 - 46 * 26 * 18);
  CHECK(estValide(r.forme));
  // Désactiver la coque rend le bloc plein, chanfreiné.
  p.fonction(idCoque)->desactivee = true;
  r = construirePiece(p);
  CHECK(volume(r.forme) == Approx(50 * 30 * 20 - 0.5 * 1 * 1 * 50).epsilon(1e-6));
}

TEST_CASE("Une révolution autour d'une ligne de l'esquisse donne une rondelle") {
  Piece p;
  FonctionPiece esq;
  esq.type = TypeFonction::Esquisse;
  esq.plan = PlanBase::XZ;
  Esquisse& e = esq.esquisse;
  const int a = e.ajouterPoint(0, -5), b = e.ajouterPoint(0, 10);
  const int axe = e.ajouterLigne(a, b, true);  // axe vertical, en construction
  const auto l = e.ajouterRectangle(4, 0, 10, 3);  // section : rayon 4 à 10, épaisseur 3
  (void)l;
  const int idEsq = p.ajouter(esq);
  FonctionPiece rev;
  rev.type = TypeFonction::Revolution;
  rev.esquisseId = idEsq;
  rev.axe = axe;
  p.ajouter(rev);
  const ResultatPiece r = construirePiece(p);
  REQUIRE(r.erreurs.empty());
  CHECK(volume(r.forme) == Approx(PI * (100 - 16) * 3).epsilon(1e-6));
}

TEST_CASE("Une pièce s'enregistre dans le projet et se relit à l'identique") {
  Piece p;
  p.id = "piece1";
  p.nom = "Support";
  FonctionPiece esq;
  esq.type = TypeFonction::Esquisse;
  esq.esquisse = rectangle(30, 20);
  resoudre(esq.esquisse);
  const int idEsq = p.ajouter(esq);
  FonctionPiece ext;
  ext.type = TypeFonction::Extrusion;
  ext.esquisseId = idEsq;
  ext.longueur = 8;
  ext.symetrique = true;
  p.ajouter(ext);
  const Json j = pieceVersJson(p);
  const Piece q = pieceDepuisJson(j);
  CHECK(pieceVersJson(q) == j);
  CHECK(volume(construirePiece(q).forme) == Approx(30 * 20 * 8).epsilon(1e-9));
  Projet projet = exemple();
  projet.pieces.push_back(j);
  const Projet relu = normaliser(versJson(projet));
  REQUIRE(relu.pieces.size() == 1);
  CHECK(pieceDepuisJson(relu.pieces[0]).nom == "Support");
  CHECK(versJson(relu)["version"] == VERSION_FORMAT);
}

TEST_CASE("L'origine et les axes servent de références aux contraintes") {
  Esquisse e;
  const int a = e.ajouterPoint(3, 7), b = e.ajouterPoint(14, 9);
  const int l = e.ajouterLigne(a, b);
  e.contraindre(TypeContrainte::Parallele, {l, AXE_H});           // parallèle à l'axe X
  e.contraindre(TypeContrainte::Distance, {a, AXE_H}, 12);        // à 12 mm de l'axe X
  e.contraindre(TypeContrainte::Symetrie, {a, b, AXE_V});         // symétrique par rapport à l'axe Y
  e.contraindre(TypeContrainte::Longueur, {l}, 40);
  const EtatEsquisse s = resoudre(e);
  REQUIRE(s.resolue);
  CHECK(s.degresLiberte == 0);
  CHECK(std::abs(e.point(a)->y) == Approx(12).margin(1e-7));
  CHECK(e.point(a)->x == Approx(-e.point(b)->x).margin(1e-7));
  CHECK(std::abs(e.point(b)->x - e.point(a)->x) == Approx(40).margin(1e-7));
}

TEST_CASE("Une cote de référence s'affiche sans contraindre, et mesure la géométrie") {
  Esquisse e = rectangle(60, 40);
  resoudre(e);
  const ElementEsquisse& diag = e.elements[0];
  ContrainteEsquisse ref;
  ref.type = TypeContrainte::Longueur;
  ref.refs.push_back(diag.id);
  ref.reference = true;
  ref.valeur = 999;  // jamais imposée
  e.contraintes.push_back(std::move(ref));
  const EtatEsquisse s = resoudre(e);
  CHECK(s.resolue);
  CHECK(s.enConflit.empty());
  CHECK(mesurer(e, e.contraintes.back()) == Approx(60).margin(1e-7));
  // La position de l'étiquette survit à l'enregistrement.
  e.contraintes.back().place = true;
  e.contraintes.back().lx = 30;
  e.contraintes.back().ly = -8;
  const Esquisse relue = esquisseDepuisJson(esquisseVersJson(e));
  CHECK(relue.contraintes.back().reference);
  CHECK(relue.contraintes.back().ly == Approx(-8));
}

TEST_CASE("Rainure, polygone, arc par trois points et congé d'esquisse donnent des profils fermés") {
  Esquisse r;
  r.ajouterRainure(0, 0, 30, 0, 5);
  REQUIRE(resoudre(r).resolue);
  std::string err;
  auto b = boucles(r, &err);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b[0].aire) == Approx(30 * 10 + PI * 25).epsilon(1e-3));
  Esquisse p;
  p.ajouterPolygone(0, 0, 10, 0, 6);
  REQUIRE(resoudre(p).resolue);
  b = boucles(p, &err);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b[0].aire) == Approx(3 * std::sqrt(3.0) / 2 * 100).epsilon(1e-6));
  Esquisse a;
  const int d = a.ajouterPoint(10, 0), f = a.ajouterPoint(-10, 0);
  const int arc = a.ajouterArc3Points(d, f, 0, 10);
  REQUIRE(arc != 0);
  CHECK(a.rayonDe(*a.element(arc)) == Approx(10).margin(1e-9));
  // Congé de 5 mm sur un coin du rectangle : quatre lignes, un arc, toujours une seule boucle.
  std::vector<int> l;
  Esquisse c = rectangle(60, 40, &l);
  resoudre(c);
  const int coin = c.element(l[1])->p2;  // coin haut droit
  REQUIRE(c.congeEsquisse(coin, 5, &err));
  const EtatEsquisse s = resoudre(c);
  CHECK(s.resolue);
  b = boucles(c, &err);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b[0].aire) == Approx(60 * 40 - (25 - PI * 25 / 4)).epsilon(1e-3));
  // Un rayon trop grand est refusé avec une explication.
  Esquisse c2 = rectangle(20, 10, &l);
  resoudre(c2);
  CHECK_FALSE(c2.congeEsquisse(c2.element(l[1])->p2, 50, &err));
  CHECK(err.find("trop grand") != std::string::npos);
}

TEST_CASE("Une esquisse posée sur la face du dessus perce la plaque, et suit la plaque quand elle s'épaissit") {
  Piece p;
  FonctionPiece esq;
  esq.type = TypeFonction::Esquisse;
  esq.esquisse = rectangle(60, 40);
  resoudre(esq.esquisse);
  const int idEsq = p.ajouter(esq);
  FonctionPiece ext;
  ext.type = TypeFonction::Extrusion;
  ext.esquisseId = idEsq;
  ext.longueur = 10;
  const int idExt = p.ajouter(ext);
  // Esquisse sur la face « sommet » : un cercle de Ø 8 centré en (30, 20), poche de 4 mm vers l'intérieur.
  FonctionPiece surFace;
  surFace.type = TypeFonction::Esquisse;
  surFace.face = std::to_string(idExt) + ":sommet";
  const int c = surFace.esquisse.ajouterPoint(30, 20);
  surFace.esquisse.ajouterCercle(c, 4);
  const int idFace = p.ajouter(surFace);
  FonctionPiece poche;
  poche.type = TypeFonction::Extrusion;
  poche.esquisseId = idFace;
  poche.enlever = true;
  poche.inverse = true;
  poche.longueur = 4;
  p.ajouter(poche);
  ResultatPiece r = construirePiece(p);
  REQUIRE(r.erreurs.empty());
  CHECK(r.repere(idFace).Location().Z() == Approx(10));
  CHECK(volume(r.forme) == Approx(60 * 40 * 10 - PI * 16 * 4).epsilon(1e-6));
  // La plaque passe à 15 mm : l'esquisse monte avec la face, la poche garde sa profondeur.
  p.fonction(idExt)->longueur = 15;
  r = construirePiece(p);
  REQUIRE(r.erreurs.empty());
  CHECK(r.repere(idFace).Location().Z() == Approx(15));
  CHECK(volume(r.forme) == Approx(60 * 40 * 15 - PI * 16 * 4).epsilon(1e-6));
}

TEST_CASE("Les valeurs se saisissent avec leur unité et des opérations") {
  const Unites mm{};
  Unites pouces;
  pouces.longueur = UniteLongueur::Pouce;
  double v = 0;
  REQUIRE(lireValeur("50", false, mm, v));
  CHECK(v == Approx(50));
  REQUIRE(lireValeur("15 in", false, mm, v));
  CHECK(v == Approx(381));
  REQUIRE(lireValeur("2,5cm", false, mm, v));
  CHECK(v == Approx(25));
  REQUIRE(lireValeur("10 + 2*3 mm", false, mm, v));
  CHECK(v == Approx(16));
  REQUIRE(lireValeur("1 in + 5 mm", false, mm, v));
  CHECK(v == Approx(30.4));
  REQUIRE(lireValeur("2", false, pouces, v));  // sans unité : celle du document
  CHECK(v == Approx(50.8));
  REQUIRE(lireValeur("(100 - 40) / 2", false, mm, v));
  CHECK(v == Approx(30));
  REQUIRE(lireValeur("20 rad", true, mm, v));
  CHECK(v == Approx(20 * 180 / PI));
  REQUIRE(lireValeur("45°", true, mm, v));
  CHECK(v == Approx(45));
  std::string err;
  CHECK_FALSE(lireValeur("10 mm + 5°", false, mm, v, &err));
  CHECK_FALSE(lireValeur("30 rad", false, mm, v, &err));
  CHECK(err.find("longueur") != std::string::npos);
  CHECK_FALSE(lireValeur("abc", false, mm, v, &err));
  CHECK(ecrireValeur(25.4, false, pouces) == "1 in");
  CHECK(ecrireValeur(12.5, false, mm) == "12,5 mm");
}

namespace {
// Plaque 60 × 40 × 5 puis esquisse rectangulaire 20 × 10 sur un plan XY décalé à z = 30.
Piece plaqueEtEsquisseHaute(int& idPlaque, int& idHaute) {
  Piece p;
  FonctionPiece e1;
  e1.type = TypeFonction::Esquisse;
  e1.esquisse = rectangle(60, 40);
  resoudre(e1.esquisse);
  const int i1 = p.ajouter(e1);
  FonctionPiece x1;
  x1.type = TypeFonction::Extrusion;
  x1.esquisseId = i1;
  x1.longueur = 5;
  idPlaque = p.ajouter(x1);
  FonctionPiece e2;
  e2.type = TypeFonction::Esquisse;
  e2.decalage = 30;
  e2.esquisse.ajouterRectangle(10, 10, 30, 20);
  idHaute = p.ajouter(e2);
  return p;
}
double volumeAvec(Piece p, FonctionPiece f) {
  p.ajouter(f);
  const ResultatPiece r = construirePiece(p);
  for (const auto& [id, m] : r.erreurs) FAIL_CHECK("fonction " << id << " : " << m);
  return volume(r.forme);
}
}  // namespace

TEST_CASE("Extrusion : deux directions, jusqu'à une face, décalée, jusqu'au sommet, jusqu'au prochain") {
  int plaque = 0, haute = 0;
  const Piece base = plaqueEtEsquisseHaute(plaque, haute);
  const double vPlaque = 60 * 40 * 5;
  FonctionPiece f;
  f.type = TypeFonction::Extrusion;
  f.esquisseId = haute;
  // 10 mm dans un sens et 4 mm dans l'autre : 14 mm de hauteur.
  f.longueur = 10;
  f.fin2 = "borgne";
  f.longueur2 = 4;
  CHECK(volumeAvec(base, f) == Approx(vPlaque + 200 * 14).epsilon(1e-6));
  // Vers le bas jusqu'à la face du dessus de la plaque (z = 5) : 25 mm.
  f = FonctionPiece();
  f.type = TypeFonction::Extrusion;
  f.esquisseId = haute;
  f.inverse = true;
  f.fin = "face";
  f.refFin = std::to_string(plaque) + ":sommet";
  CHECK(volumeAvec(base, f) == Approx(vPlaque + 200 * 25).epsilon(1e-6));
  // Décalée de 5 mm avant la face : 20 mm.
  f.fin = "decale";
  f.decalageFin = -5;
  CHECK(volumeAvec(base, f) == Approx(vPlaque + 200 * 20).epsilon(1e-6));
  // Jusqu'au prochain : la plaque arrête l'extrusion.
  f.fin = "prochain";
  f.decalageFin = 0;
  CHECK(volumeAvec(base, f) == Approx(vPlaque + 200 * 25).epsilon(1e-6));
  // Jusqu'à un sommet de la face du dessus.
  const ResultatPiece r0 = construirePiece(base);
  std::string sommet;
  for (TopExp_Explorer ex(r0.forme, TopAbs_VERTEX); ex.More() && sommet.empty(); ex.Next())
    if (BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())).Z() > 4.9) sommet = r0.nomSommet(TopoDS::Vertex(ex.Current()));
  REQUIRE_FALSE(sommet.empty());
  f.fin = "sommet";
  f.refFin = sommet;
  CHECK(volumeAvec(base, f) == Approx(vPlaque + 200 * 25).epsilon(1e-6));
  // À travers tout dans les deux sens, en enlèvement : un trou rectangulaire dans la plaque.
  f = FonctionPiece();
  f.type = TypeFonction::Extrusion;
  f.esquisseId = haute;
  f.enlever = true;
  f.fin = "traversant";
  f.fin2 = "traversant";
  CHECK(volumeAvec(base, f) == Approx(vPlaque - 200 * 5).epsilon(1e-6));
}

TEST_CASE("Extrusion : dépouille, paroi mince fermée et ouverte, choix des régions") {
  Piece p;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.esquisse = rectangle(20, 20);
  resoudre(e.esquisse);
  const int ie = p.ajouter(e);
  FonctionPiece f;
  f.type = TypeFonction::Extrusion;
  f.esquisseId = ie;
  f.longueur = 10;
  f.depouille = 5;
  const double a2 = 20 - 2 * 10 * std::tan(5 * PI / 180);
  CHECK(volumeAvec(p, f) == Approx(10.0 / 3 * (400 + a2 * a2 + 20 * a2)).epsilon(1e-4));
  f.depouille = 0;
  f.mince = true;
  f.epaisseurMince = 2;
  CHECK(volumeAvec(p, f) == Approx((400 - 256) * 10).epsilon(1e-6));
  // Contour ouvert en L, épaissi de 2 mm : un mur.
  Piece q;
  FonctionPiece eo;
  eo.type = TypeFonction::Esquisse;
  const int a = eo.esquisse.ajouterPoint(0, 0), b = eo.esquisse.ajouterPoint(20, 0), c = eo.esquisse.ajouterPoint(20, 10);
  eo.esquisse.ajouterLigne(a, b);
  eo.esquisse.ajouterLigne(b, c);
  const int io = q.ajouter(eo);
  FonctionPiece fo;
  fo.type = TypeFonction::Extrusion;
  fo.esquisseId = io;
  fo.longueur = 5;
  fo.mince = true;
  fo.epaisseurMince = 2;
  CHECK(volumeAvec(q, fo) == Approx(5 * (30 * 2 + PI + PI / 4 - 1)).epsilon(0.02));
  // Deux carrés séparés : on n'extrude que le premier.
  Piece d;
  FonctionPiece e2;
  e2.type = TypeFonction::Esquisse;
  const auto l1 = e2.esquisse.ajouterRectangle(0, 0, 20, 20);
  e2.esquisse.ajouterRectangle(40, 0, 60, 20);
  const int i2 = d.ajouter(e2);
  const auto regions = regionsEsquisse(*d.fonction(i2), repereEsquisse(PlanBase::XY, 0));
  REQUIRE(regions.size() == 2);
  FonctionPiece fr;
  fr.type = TypeFonction::Extrusion;
  fr.esquisseId = i2;
  fr.longueur = 10;
  for (const auto& rg : regions)
    if (rg.cle.find(std::to_string(l1[0])) != std::string::npos) fr.regions = {rg.cle};
  REQUIRE(fr.regions.size() == 1);
  CHECK(volumeAvec(d, fr) == Approx(4000).epsilon(1e-6));
}

TEST_CASE("Révolution autour d'un axe de base, en plan milieu et dans deux directions") {
  Piece p;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.plan = PlanBase::XZ;
  e.esquisse.ajouterRectangle(4, 0, 10, 3);
  const int ie = p.ajouter(e);
  FonctionPiece r;
  r.type = TypeFonction::Revolution;
  r.esquisseId = ie;
  r.axeBase = "Z";
  r.angle = 90;
  const double plein = PI * (100 - 16) * 3;
  CHECK(volumeAvec(p, r) == Approx(plein / 4).epsilon(1e-6));
  r.milieu = true;
  CHECK(volumeAvec(p, r) == Approx(plein / 4).epsilon(1e-6));
  r.milieu = false;
  r.angle2 = 90;
  CHECK(volumeAvec(p, r) == Approx(plein / 2).epsilon(1e-6));
}

TEST_CASE("Contours qui se croisent : chaque région se choisit, et la règle pair-impair s'applique par défaut") {
  // Carré 40 × 40 et cercle de rayon 15 centré sur son coin haut droit : trois régions.
  Piece p;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.esquisse.ajouterRectangle(0, 0, 40, 40);
  const int c = e.esquisse.ajouterPoint(40, 40);
  e.esquisse.ajouterCercle(c, 15);
  const int ie = p.ajouter(e);
  const auto regions = regionsEsquisse(*p.fonction(ie), repereEsquisse(PlanBase::XY, 0));
  REQUIRE(regions.size() == 3);
  const double quart = PI * 225 / 4;  // quart de disque dans le carré
  FonctionPiece f;
  f.type = TypeFonction::Extrusion;
  f.esquisseId = ie;
  f.longueur = 10;
  // Par défaut : le carré sans le quart, et les trois quarts de disque hors du carré.
  CHECK(volumeAvec(p, f) == Approx((1600 - quart + 3 * quart) * 10).epsilon(1e-4));
  // On ne garde que la lentille commune (le quart de disque intérieur).
  for (const auto& rg : regions)
    if (!rg.parDefaut) f.regions = {rg.cle};
  REQUIRE(f.regions.size() == 1);
  CHECK(volumeAvec(p, f) == Approx(quart * 10).epsilon(1e-4));
  // Toutes les régions : l'union du carré et du disque.
  f.regions.clear();
  for (const auto& rg : regions) f.regions.push_back(rg.cle);
  CHECK(volumeAvec(p, f) == Approx((1600 + 3 * quart) * 10).epsilon(1e-4));
}

TEST_CASE("Propriétés de masse : masse, centre de gravité et moments d'inertie positifs") {
  Piece p;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.esquisse.ajouterRectangle(-60, -50, -50, -30);  // 10 × 20, loin de l'origine
  const int ie = p.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = 30;
  x.inverse = true;
  p.ajouter(x);
  const ProprietesMasse pm = proprietesMasse(construirePiece(p).forme, 1.0);  // 1 g/cm³
  CHECK(pm.valide);
  CHECK(pm.volume == Approx(6000));
  CHECK(pm.masse == Approx(6.0));
  CHECK(pm.cx == Approx(-55));
  CHECK(pm.cy == Approx(-40));
  CHECK(pm.cz == Approx(-15));
  CHECK(pm.ixx == Approx(6.0 * (400 + 900) / 12));
  CHECK(pm.iyy == Approx(6.0 * (100 + 900) / 12));
  CHECK(pm.izz == Approx(6.0 * (100 + 400) / 12));
}

TEST_CASE("Variantes de tracé : rectangles par le centre et par trois points, parallélogramme, cercle trois points, ligne milieu, arc tangent") {
  std::string err;
  Esquisse a;
  a.ajouterRectangleCentre(10, 5, 30, 20);  // 40 × 30 centré en (10, 5)
  REQUIRE(resoudre(a).resolue);
  auto b = boucles(a, &err);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b[0].aire) == Approx(40 * 30).epsilon(1e-9));
  Esquisse r;
  r.ajouterRectangle3Points(0, 0, 30, 40, 30 - 8, 40 + 6);  // côté 50 incliné, largeur 10
  REQUIRE(resoudre(r).resolue);
  b = boucles(r, &err);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b[0].aire) == Approx(50 * 10).epsilon(1e-6));
  Esquisse pg;
  pg.ajouterParallelogramme(0, 0, 20, 0, 25, 10);
  REQUIRE(resoudre(pg).resolue);
  b = boucles(pg, &err);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b[0].aire) == Approx(200).epsilon(1e-9));
  Esquisse c;
  const int cer = c.ajouterCercle3Points(10, 0, 0, 10, -10, 0);
  REQUIRE(cer != 0);
  CHECK(c.element(cer)->rayon == Approx(10));
  Esquisse m;
  const int mil = m.ajouterPoint(5, 5);
  const int l = m.ajouterLigneMilieu(mil, 15, 5);
  REQUIRE(resoudre(m).resolue);
  CHECK(m.point(m.element(l)->p1)->x == Approx(-5));
  // Arc tangent en bout de ligne horizontale : il part vers le haut, centre au-dessus du départ.
  Esquisse t2;
  const int p0 = t2.ajouterPoint(0, 0), p1 = t2.ajouterPoint(20, 0);
  t2.ajouterLigne(p0, p1);
  const int arc = t2.ajouterArcTangent(p1, 30, 10);
  REQUIRE(arc != 0);
  REQUIRE(resoudre(t2).resolue);
  const auto* ea = t2.element(arc);
  CHECK(t2.point(ea->centre)->x == Approx(20).margin(1e-6));
  CHECK(t2.point(ea->centre)->y == Approx(10).margin(1e-6));
  // ES-34 : depuis un coin, le geste choisit le côté prolongé, pas l'ordre interne des éléments
  Esquisse coinRect;
  const auto cotes = coinRect.ajouterRectangle(0, 0, 10, 5);  // bas, droite, haut, gauche
  const ElementEsquisse* bas = coinRect.element(cotes[0]);
  const ElementEsquisse* droite = coinRect.element(cotes[1]);
  const int coin = (bas->p1 == droite->p1 || bas->p1 == droite->p2) ? bas->p1 : bas->p2;
  REQUIRE(coinRect.point(coin)->x == Approx(10));
  REQUIRE(coinRect.point(coin)->y == Approx(0));
  Point centre{0, 0};
  bool direct = true;
  int precedent = 0;
  REQUIRE(coinRect.calculerArcTangent(coin, 14, -1, centre, direct, &precedent));  // dans le prolongement du bas
  CHECK(precedent == cotes[0]);
  CHECK(centre.x == Approx(10).margin(1e-9));
  REQUIRE(coinRect.calculerArcTangent(coin, 11, -4, centre, direct, &precedent));  // dans le prolongement du côté droit
  CHECK(precedent == cotes[1]);
  CHECK(centre.y == Approx(0).margin(1e-9));
  REQUIRE(coinRect.calculerArcTangent(coin, 11, -4, centre, direct, &precedent, cotes[0]));  // élément imposé par l'outil
  CHECK(precedent == cotes[0]);
}

TEST_CASE("Chanfreins : deux distances et distance-angle") {
  auto bloc = [](FonctionPiece ch) {
    Piece p;
    FonctionPiece e;
    e.type = TypeFonction::Esquisse;
    std::vector<int> l;
    e.esquisse = rectangle(20, 20, &l);
    resoudre(e.esquisse);
    const int ie = p.ajouter(e);
    FonctionPiece x;
    x.type = TypeFonction::Extrusion;
    x.esquisseId = ie;
    x.longueur = 20;
    const int ix = p.ajouter(x);
    const std::string a = std::to_string(ix) + ":base", b = std::to_string(ix) + ":cote:" + std::to_string(l[0]);
    ch.type = TypeFonction::Chanfrein;
    ch.references = {a < b ? a + "|" + b : b + "|" + a};
    const int ic = p.ajouter(ch);
    const ResultatPiece r = construirePiece(p);
    CHECK(r.erreur(ic).empty());
    return 8000 - volume(r.forme);
  };
  FonctionPiece d;
  d.typeChanfrein = "distances";
  d.distance = 2;
  d.distance2 = 4;
  CHECK(bloc(d) == Approx(0.5 * 2 * 4 * 20).epsilon(1e-6));
  FonctionPiece a;
  a.typeChanfrein = "angle";
  a.distance = 3;
  a.angleChanfrein = 30;
  CHECK(bloc(a) == Approx(0.5 * 3 * 3 * std::tan(PI / 6) * 20).epsilon(1e-6));
}

TEST_CASE("Un trou dessiné dans la même esquisse ne change pas les noms des faces : les congés tiennent") {
  Piece p;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  std::vector<int> l;
  e.esquisse = rectangle(70, 45, &l);
  const int c = e.esquisse.ajouterPoint(35, 22.5);
  e.esquisse.ajouterCercle(c, 8);
  resoudre(e.esquisse);
  const int ie = p.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = 8;
  const int ix = p.ajouter(x);
  FonctionPiece cg;
  cg.type = TypeFonction::Conge;
  cg.rayon = 6;
  for (size_t i = 0; i < 4; ++i) {
    const std::string a = std::to_string(ix) + ":cote:" + std::to_string(l[i]), b = std::to_string(ix) + ":cote:" + std::to_string(l[(i + 1) % 4]);
    cg.references.push_back(a < b ? a + "|" + b : b + "|" + a);
  }
  const int ic = p.ajouter(cg);
  const ResultatPiece r = construirePiece(p);
  CHECK(r.erreur(ic).empty());
  CHECK(volume(r.forme) == Approx((70 * 45 - PI * 64 - 4 * (36 - PI * 36 / 4)) * 8).epsilon(1e-5));
}

TEST_CASE("Esquisse : symétrie, répétitions linéaire et circulaire, calques enregistrés") {
  // Symétrie d'un triangle par rapport à l'axe vertical : les copies restent symétriques quand l'original bouge.
  Esquisse s;
  const int a = s.ajouterPoint(10, 0), b = s.ajouterPoint(30, 0), c = s.ajouterPoint(20, 15);
  const std::vector<int> tri = {s.ajouterLigne(a, b), s.ajouterLigne(b, c), s.ajouterLigne(c, a)};
  const int cer = s.ajouterCercle(s.ajouterPoint(20, 30), 4);
  const std::vector<int> elts = {tri[0], tri[1], tri[2], cer};
  const auto sym = s.symetriser(elts, AXE_V);
  REQUIRE(sym.size() == 4);
  REQUIRE(resoudre(s).resolue);
  CHECK(s.point(s.element(sym[0])->p1)->x == Approx(-10));
  CHECK(s.element(sym[3])->rayon == Approx(4));
  // L'original change de rayon : la copie suit (égalité)
  s.contraindre(TypeContrainte::Rayon, {cer}, 6);
  REQUIRE(resoudre(s).resolue);
  CHECK(s.element(sym[3])->rayon == Approx(6).epsilon(1e-6));
  // Répétition linéaire de trois cercles au pas de 25 mm
  Esquisse r;
  const int c0 = r.ajouterCercle(r.ajouterPoint(0, 0), 3);
  const auto rep = r.repeterLineaire({c0}, 25, 0, 3);
  REQUIRE(rep.size() == 2);
  REQUIRE(resoudre(r).resolue);
  CHECK(r.point(r.element(rep[1])->centre)->x == Approx(50));
  // Répétition circulaire de six cercles sur un tour complet
  Esquisse q;
  const int c1 = q.ajouterCercle(q.ajouterPoint(20, 0), 2);
  const auto cir = q.repeterCirculaire({c1}, 0, 0, 6, 360);
  REQUIRE(cir.size() == 5);
  CHECK(q.point(q.element(cir[2])->centre)->x == Approx(-20).margin(1e-9));
  // Calques : écrits et relus
  Esquisse k;
  k.calques.push_back({"Perçages", "#C2402E", false});
  const int l = k.ajouterLigne(k.ajouterPoint(0, 0), k.ajouterPoint(10, 0));
  for (auto& el : k.elements) if (el.id == l) el.calque = "Perçages";
  const Esquisse k2 = esquisseDepuisJson(esquisseVersJson(k));
  REQUIRE(k2.calques.size() == 1);
  CHECK(k2.calques[0].nom == "Perçages");
  // Les calques n'ont plus d'interface de visibilité : un calque masqué est relu visible (audit EI-12), sinon ses
  // éléments resteraient cachés à l'écran tout en comptant dans les profils.
  CHECK(k2.calques[0].visible);
  CHECK(k2.element(l)->calque == "Perçages");
}

namespace {
// Plaque 60 × 40 × h posée sur XY ; rend l'identifiant de l'extrusion et les lignes du rectangle.
int plaque(Piece& p, double h, std::vector<int>* lignes = nullptr) {
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  std::vector<int> l;
  e.esquisse = rectangle(60, 40, &l);
  resoudre(e.esquisse);
  const int ie = p.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = h;
  if (lignes) *lignes = l;
  return p.ajouter(x);
}
int esquissePoints(Piece& p, const std::string& face, const std::vector<std::pair<double, double>>& pts) {
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.face = face;
  for (const auto& [x, y] : pts) e.esquisse.ajouterPoint(x, y);
  return p.ajouter(e);
}
}  // namespace

TEST_CASE("Géométrie de référence : plan décalé, plan par trois points, esquisse sur un plan de référence") {
  Piece p;
  FonctionPiece pl;
  pl.type = TypeFonction::Plan;
  pl.mode = "decale";
  pl.supports = {"XY"};
  pl.distance = 20;
  const int ip = p.ajouter(pl);
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.face = "F:" + std::to_string(ip);
  e.esquisse.ajouterRectangle(0, 0, 10, 10);
  resoudre(e.esquisse);
  const int ie = p.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = 5;
  p.ajouter(x);
  const ResultatPiece r = construirePiece(p);
  REQUIRE_FALSE(r.forme.IsNull());
  Bnd_Box b;
  BRepBndLib::Add(r.forme, b);
  double x0, y0, z0, x1, y1, z1;
  b.Get(x0, y0, z0, x1, y1, z1);
  CHECK(z0 == Approx(20).margin(1e-5));
  CHECK(z1 == Approx(25).margin(1e-5));
  // Trois points → plan horizontal à z = 7
  Piece q;
  std::vector<int> pts;
  for (const auto& c : {std::array<double, 3>{0, 0, 7}, std::array<double, 3>{10, 0, 7}, std::array<double, 3>{0, 10, 7}}) {
    FonctionPiece pt;
    pt.type = TypeFonction::Point;
    pt.mode = "coordonnees";
    pt.x = c[0]; pt.y = c[1]; pt.z = c[2];
    pts.push_back(q.ajouter(pt));
  }
  FonctionPiece p3;
  p3.type = TypeFonction::Plan;
  p3.mode = "troisPoints";
  for (int id : pts) p3.supports.push_back("F:" + std::to_string(id));
  const int i3 = q.ajouter(p3);
  const ResultatPiece r3 = construirePiece(q);
  CHECK(r3.erreur(i3).empty());
  REQUIRE(r3.plansReference.size() == 1);
  CHECK(r3.plansReference[0].second.Location().Z() == Approx(7));
  CHECK(std::abs(r3.plansReference[0].second.Direction().Z()) == Approx(1));
}

TEST_CASE("Perçage : trou simple traversant, lamé, fraisé et taraudé ISO") {
  auto avecTrou = [](const std::string& type, const std::string& taille) {
    Piece p;
    const int ix = plaque(p, 10);
    const int ie = esquissePoints(p, std::to_string(ix) + ":sommet", {{30, 20}});
    FonctionPiece t;
    t.type = TypeFonction::Percage;
    t.esquisseId = ie;
    t.typePercage = type;
    t.taille = taille;
    t.diametre = 6;
    t.traversant = true;
    const int it = p.ajouter(t);
    const ResultatPiece r = construirePiece(p);
    CHECK(r.erreur(it).empty());
    return 24000 - volume(r.forme);
  };
  CHECK(avecTrou("simple", "M5") == Approx(PI * 9 * 10).epsilon(1e-4));
  CHECK(avecTrou("lame", "M5") == Approx(PI * 25 * 5.4 + PI * 2.75 * 2.75 * (10 - 5.4)).epsilon(1e-4));
  const double h = 5.2 - 2.75;  // fraisure à 90° : hauteur = différence des rayons
  CHECK(avecTrou("fraise", "M5") == Approx(PI * h / 3 * (5.2 * 5.2 + 5.2 * 2.75 + 2.75 * 2.75) + PI * 2.75 * 2.75 * (10 - h)).epsilon(1e-4));
  CHECK(avecTrou("taraude", "M5") == Approx(PI * 2.1 * 2.1 * 10).epsilon(1e-4));
  REQUIRE(donneesTrou("ISO", "M8") != nullptr);
  CHECK(donneesTrou("ISO", "M8")->forageTaraud == Approx(6.8));
}

TEST_CASE("Répétitions de fonctions : linéaire et circulaire ; symétrie du corps") {
  // Un trou, puis répété trois fois au pas de 15 mm selon X
  Piece p;
  const int ix = plaque(p, 10);
  const int ie = esquissePoints(p, std::to_string(ix) + ":sommet", {{10, 20}});
  FonctionPiece t;
  t.type = TypeFonction::Percage;
  t.esquisseId = ie;
  t.typePercage = "simple";
  t.diametre = 4;
  t.traversant = true;
  const int it = p.ajouter(t);
  FonctionPiece rl;
  rl.type = TypeFonction::Repetition;
  rl.typeRepetition = "lineaire";
  rl.sources = {it};
  rl.direction1 = "X";
  rl.nombre1 = 3;
  rl.pas1 = 15;
  const int ir = p.ajouter(rl);
  ResultatPiece r = construirePiece(p);
  CHECK(r.erreur(ir).empty());
  CHECK(24000 - volume(r.forme) == Approx(3 * PI * 4 * 10).epsilon(1e-4));
  // Six trous autour d'un axe vertical passant par le centre de la plaque
  Piece q;
  const int jx = plaque(q, 10);
  const int je = esquissePoints(q, std::to_string(jx) + ":sommet", {{45, 20}});
  t.esquisseId = je;
  const int jt = q.ajouter(t);
  FonctionPiece a1, a2;
  a1.type = a2.type = TypeFonction::Point;
  a1.mode = a2.mode = "coordonnees";
  a1.x = 30; a1.y = 20; a1.z = 0;
  a2.x = 30; a2.y = 20; a2.z = 10;
  const int ia1 = q.ajouter(a1), ia2 = q.ajouter(a2);
  FonctionPiece axe;
  axe.type = TypeFonction::Axe;
  axe.mode = "deuxPoints";
  axe.supports = {"F:" + std::to_string(ia1), "F:" + std::to_string(ia2)};
  const int iax = q.ajouter(axe);
  FonctionPiece rc;
  rc.type = TypeFonction::Repetition;
  rc.typeRepetition = "circulaire";
  rc.sources = {jt};
  rc.direction1 = "F:" + std::to_string(iax);
  rc.nombre1 = 6;
  rc.angle = 360;
  const int irc = q.ajouter(rc);
  r = construirePiece(q);
  CHECK(r.erreur(irc).empty());
  CHECK(24000 - volume(r.forme) == Approx(6 * PI * 4 * 10).epsilon(1e-4));
  // Symétrie de tout le corps par rapport au plan YZ : la plaque double
  Piece s;
  plaque(s, 10);
  FonctionPiece sy;
  sy.type = TypeFonction::Symetrie;
  sy.supports = {"YZ"};
  const int isy = s.ajouter(sy);
  r = construirePiece(s);
  CHECK(r.erreur(isy).empty());
  CHECK(volume(r.forme) == Approx(48000).epsilon(1e-6));
}

TEST_CASE("Dépouille des flancs et coque à épaisseurs multiples") {
  // Bloc 20 × 20 × 20, flancs inclinés de 5° depuis le plan XY : tronc de pyramide
  Piece p;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  std::vector<int> l;
  e.esquisse = rectangle(20, 20, &l);
  resoudre(e.esquisse);
  const int ie = p.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = 20;
  const int ix = p.ajouter(x);
  FonctionPiece d;
  d.type = TypeFonction::Depouille;
  d.planNeutre = "XY";
  d.angle = 5;
  for (int li : l) d.references.push_back(std::to_string(ix) + ":cote:" + std::to_string(li));
  const int id = p.ajouter(d);
  ResultatPiece r = construirePiece(p);
  CHECK(r.erreur(id).empty());
  const double c = 20 - 2 * 20 * std::tan(5 * PI / 180);
  CHECK(volume(r.forme) == Approx(20.0 / 3 * (400 + c * c + 20 * c)).epsilon(1e-3));
  // Coque : ouverte en haut, 2 mm partout sauf un flanc de 5 mm
  Piece q;
  FonctionPiece e2;
  e2.type = TypeFonction::Esquisse;
  std::vector<int> l2;
  e2.esquisse = rectangle(50, 30, &l2);
  resoudre(e2.esquisse);
  const int je = q.ajouter(e2);
  FonctionPiece x2;
  x2.type = TypeFonction::Extrusion;
  x2.esquisseId = je;
  x2.longueur = 20;
  const int jx = q.ajouter(x2);
  FonctionPiece co;
  co.type = TypeFonction::Coque;
  co.references = {std::to_string(jx) + ":sommet"};
  co.epaisseur = 2;
  co.epaisseursFaces = {{std::to_string(jx) + ":cote:" + std::to_string(l2[1]), 5}};
  const int jc = q.ajouter(co);
  r = construirePiece(q);
  CHECK(r.erreur(jc).empty());
  CHECK(volume(r.forme) == Approx(30000 - 43.0 * 26 * 18).epsilon(1e-4));
}

TEST_CASE("Relations concentrique et colinéaire") {
  Esquisse e;
  const int c1 = e.ajouterCercle(e.ajouterPoint(0, 0), 10);
  const int c2 = e.ajouterCercle(e.ajouterPoint(3, 4), 5);
  e.contraindre(TypeContrainte::Fixe, {e.element(c1)->centre});
  e.contraindre(TypeContrainte::Concentrique, {c1, c2});
  REQUIRE(resoudre(e).resolue);
  CHECK(e.point(e.element(c2)->centre)->x == Approx(0).margin(1e-6));
  CHECK(e.point(e.element(c2)->centre)->y == Approx(0).margin(1e-6));
  Esquisse l;
  const int a = l.ajouterLigne(l.ajouterPoint(0, 0), l.ajouterPoint(10, 0));
  const int b = l.ajouterLigne(l.ajouterPoint(15, 3), l.ajouterPoint(25, 2));
  l.contraindre(TypeContrainte::Fixe, {l.element(a)->p1}, 0, 0);
  l.contraindre(TypeContrainte::Fixe, {l.element(a)->p2}, 10, 0);
  l.contraindre(TypeContrainte::Colineaire, {a, b});
  REQUIRE(resoudre(l).resolue);
  CHECK(l.point(l.element(b)->p1)->y == Approx(0).margin(1e-6));
  CHECK(l.point(l.element(b)->p2)->y == Approx(0).margin(1e-6));
  // Relu depuis le fichier
  const Esquisse r = esquisseDepuisJson(esquisseVersJson(l));
  bool trouve = false;
  for (const auto& k : r.contraintes) trouve |= k.type == TypeContrainte::Colineaire;
  CHECK(trouve);
}

TEST_CASE("Édition d'esquisse : déplacer, pivoter, échelle, décaler, ajuster, prolonger") {
  std::string err;
  // Échelle ×2 d'un rectangle coté : les cotes suivent
  Esquisse e;
  const auto l = e.ajouterRectangle(0, 0, 20, 10);
  e.contraindre(TypeContrainte::Longueur, {l[0]}, 20);
  e.transformer(l, [](Point p) { return Point{2 * p.x, 2 * p.y}; }, 2, false);
  REQUIRE(resoudre(e).resolue);
  auto b = boucles(e, &err);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b[0].aire) == Approx(800).epsilon(1e-6));
  // Pivoter une copie de 90° autour de l'origine
  Esquisse p;
  const int li = p.ajouterLigne(p.ajouterPoint(10, 0), p.ajouterPoint(20, 0));
  const auto copie = p.transformer({li}, [](Point q) { return Point{-q.y, q.x}; }, 1, true);
  REQUIRE(copie.size() == 1);
  CHECK(p.point(p.element(copie[0])->p1)->y == Approx(10));
  // Décaler un carré de 2 mm vers l'extérieur : carré de 24 × 24
  Esquisse d;
  const auto c = d.ajouterRectangle(0, 0, 20, 20);
  const auto dec = d.decaler(c, 2);
  REQUIRE(dec.size() == 4);
  Esquisse seul;
  for (int id : dec) {
    const auto* x = d.element(id);
    const auto *a = d.point(x->p1), *q = d.point(x->p2);
    seul.ajouterLigne(seul.ajouterPoint(a->x, a->y), seul.ajouterPoint(q->x, q->y));
  }
  // les quatre lignes décalées se rejoignent aux coins (−2, −2) et (22, 22)
  double xmin = 1e9, xmax = -1e9;
  for (const auto& pt : seul.points) if (pt.id) { xmin = std::min(xmin, pt.x); xmax = std::max(xmax, pt.x); }
  CHECK(xmin == Approx(-2));
  CHECK(xmax == Approx(22));
  // Décaler un cercle
  Esquisse ce;
  const int cc = ce.ajouterCercle(ce.ajouterPoint(0, 0), 5);
  const auto dc = ce.decaler({cc}, 3);
  REQUIRE(dc.size() == 1);
  CHECK(ce.element(dc[0])->rayon == Approx(8));
  // Ajuster : une ligne croisée par une autre perd le bout cliqué
  Esquisse a;
  const int h = a.ajouterLigne(a.ajouterPoint(0, 0), a.ajouterPoint(20, 0));
  a.ajouterLigne(a.ajouterPoint(10, -5), a.ajouterPoint(10, 5));
  REQUIRE(a.ajuster(h, 16, 0));
  CHECK(a.point(a.element(h)->p2)->x == Approx(10));
  // Ajuster un cercle coupé par une ligne : il devient un arc
  Esquisse ac;
  const int cer = ac.ajouterCercle(ac.ajouterPoint(0, 0), 10);
  ac.ajouterLigne(ac.ajouterPoint(-20, 0), ac.ajouterPoint(20, 0));
  REQUIRE(ac.ajuster(cer, 0, 10));
  CHECK(ac.element(cer)->type == TypeElement::Arc);
  // Prolonger une ligne jusqu'à la ligne verticale x = 30
  Esquisse pr;
  const int lp = pr.ajouterLigne(pr.ajouterPoint(0, 0), pr.ajouterPoint(10, 0));
  pr.ajouterLigne(pr.ajouterPoint(30, -5), pr.ajouterPoint(30, 5));
  REQUIRE(pr.prolonger(lp, 9, 0));
  CHECK(pr.point(pr.element(lp)->p2)->x == Approx(30));
}

TEST_CASE("Balayage d'un cercle le long d'une ligne, lissage entre deux carrés") {
  Piece p;
  FonctionPiece prof;
  prof.type = TypeFonction::Esquisse;
  prof.esquisse.ajouterCercle(0, 2);
  const int ip = p.ajouter(prof);
  FonctionPiece ch;
  ch.type = TypeFonction::Esquisse;
  ch.plan = PlanBase::XZ;
  ch.esquisse.ajouterLigne(0, ch.esquisse.ajouterPoint(0, 30));
  const int ic = p.ajouter(ch);
  FonctionPiece b;
  b.type = TypeFonction::Balayage;
  b.esquisseId = ip;
  b.cheminId = ic;
  const int ib = p.ajouter(b);
  ResultatPiece r = construirePiece(p);
  CHECK(r.erreur(ib).empty());
  CHECK(volume(r.forme) == Approx(PI * 4 * 30).epsilon(1e-4));
  // Lissage : carré 20 × 20 à z = 0 vers carré 10 × 10 à z = 20
  Piece q;
  FonctionPiece s1;
  s1.type = TypeFonction::Esquisse;
  s1.esquisse.ajouterRectangle(-10, -10, 10, 10);
  const int i1 = q.ajouter(s1);
  FonctionPiece s2;
  s2.type = TypeFonction::Esquisse;
  s2.decalage = 20;
  s2.esquisse.ajouterRectangle(-5, -5, 5, 5);
  const int i2 = q.ajouter(s2);
  FonctionPiece l;
  l.type = TypeFonction::Lissage;
  l.sources = {i1, i2};
  const int il = q.ajouter(l);
  r = construirePiece(q);
  CHECK(r.erreur(il).empty());
  CHECK(volume(r.forme) == Approx(20.0 / 3 * (400 + 100 + 200)).epsilon(1e-4));
  // Relu depuis le fichier
  const Piece q2 = pieceDepuisJson(pieceVersJson(p));
  CHECK(q2.fonction(ib)->cheminId == ic);
}

#include "promethee/dessin.hpp"

TEST_CASE("Mise en plan : vues projetées à l'échelle sur une feuille A4, écritures DXF et SVG") {
  Piece p;
  plaque(p, 10);
  const ResultatPiece r = construirePiece(p);
  // Vue de dessus d'une plaque 60 × 40 : rectangle de 60 × 40
  const auto traits = projeter(r.forme, gp_Ax2(gp::Origin(), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), false);
  double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
  for (const auto& t : traits) for (const auto& [x, y] : t.points) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
  CHECK(x1 - x0 == Approx(60).epsilon(1e-6));
  CHECK(y1 - y0 == Approx(40).epsilon(1e-6));
  OptionsMiseEnPlan o;
  o.titre = "Plaque";
  const Feuille f = miseEnPlan(r.forme, o);
  CHECK(f.vues.size() == 4);
  CHECK(f.echelle == Approx(2));  // mise en page en millimètres de papier : la plaque tient à 2:1 sur un A4 (MP-02)
  for (const auto& v : f.vues) {
    CHECK(v.cx - v.largeur / 2 >= 10);
    CHECK(v.cx + v.largeur / 2 <= 287);
  }
  // Dossier temporaire du système (/tmp n'existe pas sous Windows)
  const std::string dxf = (std::filesystem::temp_directory_path() / "promethee-essai-plan.dxf").string();
  const std::string svg = (std::filesystem::temp_directory_path() / "promethee-essai-plan.svg").string();
  CHECK(ecrireDxf(f, dxf));
  CHECK(ecrireSvg(f, svg));
  std::ifstream lu(svg);
  std::string contenu((std::istreambuf_iterator<char>(lu)), std::istreambuf_iterator<char>());
  CHECK(contenu.find("Plaque") != std::string::npos);
}

#include "promethee/assemblage.hpp"

TEST_CASE("Contraintes d'assemblage : une plaque posée et centrée sur une autre") {
  // Deux plaques 60 × 40 × 10 ; la seconde, placée n'importe où, vient se poser sur la première (face du dessus contre
  // face du dessous), puis s'aligne sur ses flancs par deux coïncidences de faces planes.
  Piece p;
  std::vector<int> l;
  const int ix = plaque(p, 10, &l);
  const ResultatPiece r = construirePiece(p);
  Assemblage a;
  a.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  a.composants.push_back({0, 37, -12, 25, 10, -20, 30, false});
  const std::string dessus = std::to_string(ix) + ":sommet", dessous = std::to_string(ix) + ":base";
  const std::string cote0 = std::to_string(ix) + ":cote:" + std::to_string(l[0]), cote3 = std::to_string(ix) + ":cote:" + std::to_string(l[3]);
  a.contraintes.push_back({"coincidence", 0, 1, dessus, dessous, 0, false});
  a.contraintes.push_back({"coincidence", 0, 1, cote0, cote0, 0, true});   // flancs avant alignés
  a.contraintes.push_back({"coincidence", 0, 1, cote3, cote3, 0, true});   // flancs gauches alignés
  const EtatAssemblage e = resoudreAssemblage(a, {r});
  CHECK(e.resolu);
  // La seconde plaque est exactement au-dessus : translation (0, 0, 10), sans rotation
  const gp_Trsf t = placementComposant(a.composants[1]);
  const gp_Pnt o = gp_Pnt(0, 0, 0).Transformed(t), c = gp_Pnt(60, 40, 10).Transformed(t);
  CHECK(o.Distance(gp_Pnt(0, 0, 10)) < 1e-5);
  CHECK(c.Distance(gp_Pnt(60, 40, 20)) < 1e-5);
}

TEST_CASE("Polygone circonscrit, chanfrein d'esquisse, rainure en arc, variables de pièce") {
  std::string err;
  Esquisse h;
  h.ajouterPolygoneCirconscrit(0, 0, 10, 0, 6);  // hexagone : cercle inscrit de rayon 10
  for (const auto& el : h.elements)
    if (el.type == TypeElement::Ligne && !el.construction) {
      const auto *a = h.point(el.p1), *b = h.point(el.p2);
      const double mx = (a->x + b->x) / 2, my = (a->y + b->y) / 2;
      CHECK(std::hypot(mx, my) == Approx(10).epsilon(1e-9));
    }
  // Chanfrein de 3 mm au coin d'un carré 20 × 20 : aire 400 − 4,5
  Esquisse c;
  const auto l = c.ajouterRectangle(0, 0, 20, 20);
  const int coin = c.element(l[0])->p1;
  REQUIRE(c.chanfreinEsquisse(coin, 3) != 0);
  REQUIRE(resoudre(c).resolue);
  auto b = boucles(c, &err);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b[0].aire) == Approx(400 - 4.5).epsilon(1e-9));
  // Rainure en arc : quart de couronne + deux demi-disques
  Esquisse r;
  r.ajouterRainureArc(0, 0, 20, 0, 0, 20, 4);
  b = boucles(r, &err);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b[0].aire) == Approx(PI / 4 * (22 * 22 - 18 * 18) + PI * 4).epsilon(5e-3));  // arcs approchés par des segments
  // Variables : L = 60, e = L/4 ; une cote vaut « e » puis suit quand L change
  Piece p;
  p.variables = {{"L", "60"}, {"e", "L/4"}};
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  const int li = e.esquisse.ajouterLigne(0, e.esquisse.ajouterPoint(10, 0));
  e.esquisse.contraindre(TypeContrainte::Longueur, {li}, 10);
  e.esquisse.contraintes.back().expression = "e";
  e.esquisse.contraindre(TypeContrainte::Horizontal, {li});
  const int ie = p.ajouter(e);
  REQUIRE(appliquerVariables(p, &err));
  CHECK(err.empty());
  auto longueur = [&] { const auto* x = p.fonction(ie)->esquisse.element(li); const auto *a = p.fonction(ie)->esquisse.point(x->p1), *q = p.fonction(ie)->esquisse.point(x->p2); return std::hypot(q->x - a->x, q->y - a->y); };
  CHECK(longueur() == Approx(15));
  p.variables[0].second = "100";
  REQUIRE(appliquerVariables(p, &err));
  CHECK(longueur() == Approx(25));
  const Piece relue = pieceDepuisJson(pieceVersJson(p));
  REQUIRE(relue.variables.size() == 2);
  CHECK(relue.fonction(ie)->esquisse.contraintes.front().expression == "e");
}

TEST_CASE("Congé et chanfrein d'esquisse à un coin où partent aussi d'autres lignes") {
  // Coin d'un rectangle avec une diagonale de construction et une troisième ligne normale
  Esquisse e;
  const auto l = e.ajouterRectangle(0, 0, 20, 20);
  const int coin = e.element(l[0])->p1;  // (0, 0)
  e.ajouterLigne(coin, e.ajouterPoint(20, 20), true);  // diagonale de construction : ignorée
  std::string err;
  CHECK(e.congeEsquisse(coin, 3, &err));
  CHECK(err.empty());
  // Trois lignes normales : le clic désigne l'angle à arrondir
  Esquisse f;
  const auto m = f.ajouterRectangle(0, 0, 20, 20);
  const int c2 = f.element(m[0])->p1;
  f.ajouterLigne(c2, f.ajouterPoint(-10, -10));
  CHECK_FALSE(f.congeEsquisse(c2, 3, &err));          // ambigu sans clic
  CHECK(f.congeEsquisse(c2, 3, &err, 5, 5));            // clic dans l'angle du carré
  Esquisse g;
  const auto n = g.ajouterRectangle(0, 0, 20, 20);
  const int c3 = g.element(n[0])->p1;
  g.ajouterLigne(c3, g.ajouterPoint(20, 20), true);
  CHECK(g.chanfreinEsquisse(c3, 2) != 0);
}

TEST_CASE("Ajuster : pas de points orphelins, extrémités liées à l'élément coupant, cotes de longueur retirées") {
  // Ligne horizontale cotée, coupée par une verticale : le bout droit disparaît, sans laisser son ancien point
  Esquisse a;
  const int h = a.ajouterLigne(a.ajouterPoint(0, 0), a.ajouterPoint(20, 0));
  a.contraindre(TypeContrainte::Longueur, {h}, 20);
  const int v = a.ajouterLigne(a.ajouterPoint(10, -5), a.ajouterPoint(10, 5));
  const size_t pointsAvant = a.points.size();
  REQUIRE(a.ajuster(h, 16, 0));
  CHECK(a.points.size() == pointsAvant);  // un point nouveau, l'ancien extrémité retiré
  bool lie = false, cote = false;
  for (const auto& c : a.contraintes) {
    lie |= c.type == TypeContrainte::PointSur && c.refs.size() == 2 && c.refs[1] == v;
    cote |= c.type == TypeContrainte::Longueur;
  }
  CHECK(lie);
  CHECK_FALSE(cote);
  REQUIRE(resoudre(a).resolue);
  CHECK(a.point(a.element(h)->p2)->x == Approx(10).margin(1e-6));
  // Plusieurs ajustements successifs dans une grille : aucun point isolé ne reste
  Esquisse g;
  for (int i = 0; i < 3; ++i) {
    g.ajouterLigne(g.ajouterPoint(0, i * 10.0), g.ajouterPoint(30, i * 10.0));
    g.ajouterLigne(g.ajouterPoint(i * 10.0 + 5, -5), g.ajouterPoint(i * 10.0 + 5, 25));
  }
  const auto ids = [&] { std::vector<int> r; for (const auto& e : g.elements) r.push_back(e.id); return r; }();
  g.ajuster(ids[0], 1, 0);
  g.ajuster(ids[0], 28, 0);
  g.ajuster(ids[1], 5, -3);
  for (const auto& p : g.points) {
    if (p.id == 0) continue;
    bool sert = false;
    for (const auto& e : g.elements) sert |= e.p1 == p.id || e.p2 == p.id || e.centre == p.id;
    CHECK(sert);
  }
  REQUIRE(resoudre(g).resolue);
}

TEST_CASE("Diagnostic d'esquisse : degrés de liberté expliqués et extrémités libres") {
  // Rectangle sans aucune cote, avec ses relations horizontales et verticales : déplacement horizontal, vertical,
  // largeur et hauteur libres — quatre degrés de liberté, dont les deux translations d'ensemble.
  Esquisse r;
  r.ajouterRectangle(10, 10, 50, 30);
  resoudre(r);
  const DiagnosticEsquisse d = diagnostiquer(r);
  CHECK(d.degresLiberte == 4);
  REQUIRE(d.libertes.size() == 4);
  CHECK(d.libertes[0].description.find("horizontal") != std::string::npos);
  CHECK(d.libertes[1].description.find("vertical") != std::string::npos);
  CHECK(d.extremitesLibres.empty());
  // Coin fixé sur l'origine et largeur cotée : il ne reste que la hauteur
  Esquisse c;
  const auto l = c.ajouterRectangle(0, 0, 50, 30);
  c.contraindre(TypeContrainte::Coincidence, {c.element(l[0])->p1, 0});
  c.contraindre(TypeContrainte::Longueur, {l[0]}, 50);
  resoudre(c);
  const DiagnosticEsquisse d2 = diagnostiquer(c);
  CHECK(d2.degresLiberte == 1);
  REQUIRE(d2.libertes.size() == 1);
  CHECK(d2.libertes[0].description.find("longueur") != std::string::npos);
  // Profil ouvert : deux lignes en V laissent deux extrémités libres
  Esquisse o;
  const int p = o.ajouterPoint(0, 0);
  o.ajouterLigne(o.ajouterPoint(-10, 10), p);
  o.ajouterLigne(p, o.ajouterPoint(10, 10));
  CHECK(diagnostiquer(o).extremitesLibres.size() == 2);
  // Cercle seul, centre fixé : seul le rayon est libre
  Esquisse k;
  const int ce = k.ajouterCercle(k.ajouterPoint(5, 5), 3);
  k.contraindre(TypeContrainte::Fixe, {k.element(ce)->centre}, 5, 5);
  resoudre(k);
  const DiagnosticEsquisse d3 = diagnostiquer(k);
  CHECK(d3.degresLiberte == 1);
  REQUIRE(d3.libertes.size() == 1);
  CHECK(d3.libertes[0].description.find("rayon") != std::string::npos);
}

TEST_CASE("Esquisse 3D : chemin en L dans l'espace, balayage d'un cercle le long de lui") {
  Piece p;
  FonctionPiece e3;
  e3.type = TypeFonction::Esquisse3D;
  e3.points3d = {{0, 0, 0}, {0, 0, 40}, {30, 0, 40}};  // monte, puis part selon X
  const int i3 = p.ajouter(e3);
  FonctionPiece prof;
  prof.type = TypeFonction::Esquisse;
  prof.esquisse.ajouterCercle(0, 2);
  const int ip = p.ajouter(prof);
  FonctionPiece b;
  b.type = TypeFonction::Balayage;
  b.esquisseId = ip;
  b.cheminId = i3;
  const int ib = p.ajouter(b);
  ResultatPiece r = construirePiece(p);
  CHECK(r.erreur(i3).empty());
  CHECK(r.erreur(ib).empty());
  // Tube de rayon 2 sur 70 mm de chemin, coude vif : le volume vaut à peu près π·4·70
  CHECK(volume(r.forme) == Approx(PI * 4 * 70).epsilon(0.03));
  // Courbe lisse fermée et relecture
  e3.lisse = true;
  e3.fermee = true;
  e3.points3d = {{0, 0, 0}, {20, 0, 10}, {20, 20, 0}, {0, 20, 10}};
  Piece q;
  const int j3 = q.ajouter(e3);
  r = construirePiece(q);
  CHECK(r.erreur(j3).empty());
  const Piece q2 = pieceDepuisJson(pieceVersJson(q));
  REQUIRE(q2.fonction(j3)->points3d.size() == 4);
  CHECK(q2.fonction(j3)->lisse);
  CHECK(q2.fonction(j3)->fermee);
}

#include "promethee/fabrication.hpp"

TEST_CASE("Vérification de fabrication : paroi mince, petit rayon, trou profond, angles intérieurs vifs") {
  auto contient = [](const std::vector<AvisFabrication>& a, const std::string& mot) {
    for (const auto& x : a) if (x.message.find(mot) != std::string::npos) return true;
    return false;
  };
  // Plaque 60 × 40 × 0,6 : paroi trop mince
  Piece mince;
  plaque(mince, 0.6);
  CHECK(contient(verifierFabrication(construirePiece(mince).forme), "Paroi de 0,6"));
  // Plaque de 30 mm d'épaisseur percée d'un trou de Ø 2 traversant : quinze fois le diamètre
  Piece epaisse;
  const int ix = plaque(epaisse, 30);
  const int ie = esquissePoints(epaisse, std::to_string(ix) + ":sommet", {{30, 20}});
  FonctionPiece t;
  t.type = TypeFonction::Percage;
  t.esquisseId = ie;
  t.typePercage = "simple";
  t.diametre = 2;
  t.traversant = true;
  epaisse.ajouter(t);
  const auto avisTrou = verifierFabrication(construirePiece(epaisse).forme);
  CHECK(contient(avisTrou, "Trou de Ø 2"));
  CHECK_FALSE(contient(avisTrou, "Paroi"));
  // Congé de 0,3 mm : trop petit
  Piece arrondie;
  std::vector<int> l;
  const int jx = plaque(arrondie, 10, &l);
  FonctionPiece c;
  c.type = TypeFonction::Conge;
  c.rayon = 0.3;
  c.references = {std::to_string(jx) + ":cote:" + std::to_string(l[0]) + "|" + std::to_string(jx) + ":sommet"};
  arrondie.ajouter(c);
  const ResultatPiece ra = construirePiece(arrondie);
  CHECK(contient(verifierFabrication(ra.forme), "trop petit"));
  // Pièce en L : un angle intérieur vif ; une plaque simple n'en a aucun
  Piece el;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  const int a = e.esquisse.ajouterPoint(0, 0), b = e.esquisse.ajouterPoint(40, 0), cc = e.esquisse.ajouterPoint(40, 10), d = e.esquisse.ajouterPoint(10, 10),
            ee = e.esquisse.ajouterPoint(10, 30), ff = e.esquisse.ajouterPoint(0, 30);
  e.esquisse.ajouterLigne(a, b); e.esquisse.ajouterLigne(b, cc); e.esquisse.ajouterLigne(cc, d);
  e.esquisse.ajouterLigne(d, ee); e.esquisse.ajouterLigne(ee, ff); e.esquisse.ajouterLigne(ff, a);
  const int ies = el.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ies;
  x.longueur = 10;
  el.ajouter(x);
  CHECK(contient(verifierFabrication(construirePiece(el).forme), "1 angle(s) intérieur(s)"));
  Piece simple;
  plaque(simple, 10);
  CHECK(verifierFabrication(construirePiece(simple).forme).empty());
}

TEST_CASE("Import de géométrie (BREP gardé dans le projet) et tube le long d'une esquisse 3D") {
  // Une plaque construite, écrite en BREP, relue comme pièce importée
  Piece src;
  plaque(src, 10);
  const std::string brep = brepDepuisForme(construirePiece(src).forme);
  REQUIRE_FALSE(brep.empty());
  Piece imp;
  FonctionPiece f;
  f.type = TypeFonction::Import;
  f.donnees = brep;
  f.source = "plaque.step";
  const int ii = imp.ajouter(f);
  ResultatPiece r = construirePiece(imp);
  CHECK(r.erreur(ii).empty());
  CHECK(volume(r.forme) == Approx(24000).epsilon(1e-6));
  // Relecture du projet : la géométrie voyage avec lui
  const Piece relue = pieceDepuisJson(pieceVersJson(imp));
  CHECK(volume(construirePiece(relue).forme) == Approx(24000).epsilon(1e-6));
  // Fichier STEP écrit puis relu
  const std::string step = (std::filesystem::temp_directory_path() / "promethee-essai-import.step").string();
  exporterStep({{"plaque", construirePiece(src).forme}}, step);
  std::string err;
  const std::string lu = lireFichierCAO(step, &err);
  CHECK(err.empty());
  CHECK(volume(formeDepuisBrep(lu)) == Approx(24000).epsilon(1e-4));
  // Tube Ø 10, paroi 1 mm, le long d'un chemin droit de 50 mm
  Piece t2;
  FonctionPiece e3;
  e3.type = TypeFonction::Esquisse3D;
  e3.points3d = {{0, 0, 0}, {0, 0, 50}};
  const int i3 = t2.ajouter(e3);
  FonctionPiece b;
  b.type = TypeFonction::Balayage;
  b.tube = true;
  b.diametre = 10;
  b.epaisseur = 1;
  b.cheminId = i3;
  const int ib = t2.ajouter(b);
  r = construirePiece(t2);
  CHECK(r.erreur(ib).empty());
  CHECK(volume(r.forme) == Approx(PI * (25 - 16) * 50).epsilon(1e-4));
}

TEST_CASE("Assemblage : symétrie d'un composant, contrainte suspendue, angle, tangence, libertés par composant") {
  Piece p;
  std::vector<int> l;
  const int ix = plaque(p, 10, &l);
  const ResultatPiece r = construirePiece(p);
  const std::string dessus = std::to_string(ix) + ":sommet", dessous = std::to_string(ix) + ":base";
  // Seule la coïncidence dessus/dessous : la seconde plaque garde 3 libertés (glisser en X, en Y, tourner en Z)
  Assemblage a;
  a.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  a.composants.push_back({0, 5, 7, 30, 0, 0, 15, false});
  a.contraintes.push_back({"coincidence", 0, 1, dessus, dessous, 0, false});
  REQUIRE(resoudreAssemblage(a, {r}).resolu);
  const AnalyseAssemblage an = analyserAssemblage(a, {r});
  CHECK(an.degresLiberte == 3);
  CHECK(an.libres[1][0]);        // translation X libre
  CHECK(an.libres[1][1]);        // translation Y libre
  CHECK_FALSE(an.libres[1][2]);  // translation Z bloquée
  CHECK_FALSE(an.libres[1][3]);  // rotation X bloquée
  CHECK(an.libres[1][5]);        // rotation Z libre
  // Contrainte suspendue : la plaque n'est plus tenue, on peut la déplacer sans que la résolution la ramène
  a.contraintes[0].suspendue = true;
  a.composants[1].z = 50;
  resoudreAssemblage(a, {r});
  CHECK(a.composants[1].z == Approx(50));
  CHECK(analyserAssemblage(a, {r}).degresLiberte == 6);
  // Angle de 90° entre deux faces planes (dessus de l'une, dessus de l'autre)
  Assemblage b;
  b.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  b.composants.push_back({0, 0, 0, 80, 20, 0, 0, false});
  b.contraintes.push_back({"angle", 0, 1, dessus, dessus, 90, false});
  REQUIRE(resoudreAssemblage(b, {r}).resolu);
  const gp_Dir n2 = gp_Dir(0, 0, 1).Transformed(placementComposant(b.composants[1]));
  CHECK(std::abs(n2.Z()) < 1e-5);
  // Symétrie d'un composant par rapport au plan YZ : la plaque passe en X négatif
  ComposantAssemblage sym{0, 0, 0, 0, 0, 0, 0, false, -1, 3};
  const gp_Pnt coin = gp_Pnt(60, 40, 10).Transformed(placementComposant(sym));
  CHECK(coin.X() == Approx(-60));
}

TEST_CASE("Liaisons mécaniques : pivot piloté, glissière, engrenage, vis-écrou") {
  // Plaque percée (alésage Ø 10) et arbre (cylindre Ø 10, 40 mm)
  Piece plaqueP;
  const int ix = plaque(plaqueP, 10);
  const int ie = esquissePoints(plaqueP, std::to_string(ix) + ":sommet", {{30, 20}});
  FonctionPiece tr;
  tr.type = TypeFonction::Percage;
  tr.esquisseId = ie;
  tr.typePercage = "simple";
  tr.diametre = 10;
  tr.traversant = true;
  const int it = plaqueP.ajouter(tr);
  Piece arbre;
  FonctionPiece ec;
  ec.type = TypeFonction::Esquisse;
  const int cercle = ec.esquisse.ajouterCercle(ec.esquisse.ajouterPoint(0, 0), 5);
  const int iec = arbre.ajouter(ec);
  FonctionPiece xa;
  xa.type = TypeFonction::Extrusion;
  xa.esquisseId = iec;
  xa.longueur = 40;
  const int ixa = arbre.ajouter(xa);
  const std::vector<ResultatPiece> pieces = {construirePiece(plaqueP), construirePiece(arbre)};
  // Face cylindrique de l'alésage
  std::string alesage;
  for (const auto& [f, nom] : pieces[0].faces)
    if (nom.rfind(std::to_string(it) + ":trou", 0) == 0) { BRepAdaptor_Surface s(f); if (s.GetType() == GeomAbs_Cylinder) alesage = nom; }
  REQUIRE_FALSE(alesage.empty());
  const std::string flanc = std::to_string(ixa) + ":cote:" + std::to_string(cercle);
  // Pivot : l'arbre, placé dans l'alésage, ne peut plus que tourner
  Assemblage a;
  a.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  a.composants.push_back({1, 30, 20, -15, 0, 0, 0, false});
  a.contraintes.push_back(creerLiaison(a, pieces, "pivot", 0, alesage, 1, flanc));
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  AnalyseAssemblage an = analyserAssemblage(a, pieces);
  CHECK(an.degresLiberte == 1);
  CHECK(an.libres[1][5]);  // rotation autour de Z
  // Pilotée à 90° : l'arbre tourne d'un quart de tour, sans bouger d'axe
  a.contraintes[0].pilotee = true;
  a.contraintes[0].valeur = 90;
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  CHECK(std::fmod(std::abs(a.composants[1].rz) + 1e-6, 360.0) == Approx(90).margin(1e-3));
  CHECK(a.composants[1].x == Approx(30).margin(1e-4));
  CHECK(analyserAssemblage(a, pieces).degresLiberte == 0);
  // Glissière : seule la course le long de l'axe reste
  Assemblage g;
  g.composants = {{0, 0, 0, 0, 0, 0, 0, true}, {1, 30, 20, -15, 0, 0, 0, false}};
  g.contraintes.push_back(creerLiaison(g, pieces, "glissiere", 0, alesage, 1, flanc));
  REQUIRE(resoudreAssemblage(g, pieces).resolu);
  an = analyserAssemblage(g, pieces);
  CHECK(an.degresLiberte == 1);
  CHECK(an.libres[1][2]);  // translation Z
  // Vis-écrou au pas de 2 mm : un tour fait avancer de 2 mm
  Assemblage v;
  v.composants = {{0, 0, 0, 0, 0, 0, 0, true}, {1, 30, 20, -15, 0, 0, 0, false}};
  v.contraintes.push_back(creerLiaison(v, pieces, "vis", 0, alesage, 1, flanc, 2));
  v.contraintes[0].pilotee = true;
  v.contraintes[0].valeur = 360;
  REQUIRE(resoudreAssemblage(v, pieces).resolu);
  CHECK(std::abs(v.composants[1].z - (-15)) == Approx(2).margin(1e-3));  // le sens d'avance suit l'orientation de l'axe
  // Engrenage de rapport 2 (dents menée / menante) entre deux arbres sur pivots : 30° d'un côté, −15° de l'autre
  Assemblage e;
  e.composants = {{0, 0, 0, 0, 0, 0, 0, true}, {1, 30, 20, -15, 0, 0, 0, false}, {1, 30, 20, -15, 0, 0, 0, false}};
  e.contraintes.push_back(creerLiaison(e, pieces, "pivot", 0, alesage, 1, flanc));
  e.contraintes.push_back(creerLiaison(e, pieces, "pivot", 0, alesage, 2, flanc));
  e.contraintes.push_back(creerLiaison(e, pieces, "engrenage", 1, flanc, 2, flanc, 2));
  e.contraintes[0].pilotee = true;
  e.contraintes[0].valeur = 30;
  REQUIRE(resoudreAssemblage(e, pieces).resolu);
  CHECK(std::abs(e.composants[1].rz) == Approx(30).margin(1e-3));
  CHECK(e.composants[2].rz == Approx(-e.composants[1].rz / 2).margin(1e-3));  // rapport 2 : deux fois moins vite, sens opposé
}

TEST_CASE("Régions : une rainure en arc qui croise une rainure droite donne toutes ses régions (le ventre des arcs compte)") {
  // Chaque point intérieur à l'un des contours doit tomber dans une région ; avant la correction, le cadre de découpe
  // ignorait le ventre des arcs et perdait les régions qu'il coupait.
  struct Cas { double rayon, a0, a1, x1, y1, x2, y2; };
  for (const Cas& k : {Cas{40, 0.35, 3.49, 10, -10, 60, 25}, Cas{40, -1.0, 3.0, -10, -10, 70, -45}, Cas{50, -0.6, 2.6, 0, 0, 80, -50}}) {
    FonctionPiece e;
    e.type = TypeFonction::Esquisse;
    e.esquisse.ajouterRainureArc(0, 0, k.rayon * std::cos(k.a0), k.rayon * std::sin(k.a0), k.rayon * std::cos(k.a1), k.rayon * std::sin(k.a1), 6);
    e.esquisse.ajouterRainure(k.x1, k.y1, k.x2, k.y2, 10);
    const auto regions = regionsEsquisse(e, repereEsquisse(PlanBase::XY, 0));
    int dedans = 0, couverts = 0;
    for (double x = -79.63; x <= 120; x += 1)      // grille décalée : aucun point pile sur une frontière
      for (double y = -79.71; y <= 80; y += 1) {
        const double dx = k.x2 - k.x1, dy = k.y2 - k.y1, t = std::clamp(((x - k.x1) * dx + (y - k.y1) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
        const bool droite = std::hypot(x - k.x1 - t * dx, y - k.y1 - t * dy) < 9.9;
        double aa = std::atan2(y, x);
        while (aa < k.a0) aa += 2 * PI;
        while (aa >= k.a0 + 2 * PI) aa -= 2 * PI;
        const double rr = std::hypot(x, y);
        bool arc = rr > k.rayon - 2.9 && rr < k.rayon + 2.9 && aa <= k.a1;
        if (!droite && !arc) continue;
        ++dedans;
        for (const auto& r : regions) {
          BRepClass_FaceClassifier cl(r.face, gp_Pnt(x, y, 0), 1e-6);
          if (cl.State() == TopAbs_IN) { ++couverts; break; }
        }
      }
    CHECK(couverts == dedans);
  }
}

TEST_CASE("AP-06 monter et descendre une fonction avec son esquisse, sans passer avant une dépendance", "[pieces][AP-06]") {
  auto pieceQuatre = [](bool surFace) {
    Piece p;
    FonctionPiece e1;
    e1.type = TypeFonction::Esquisse;
    e1.nom = "Esquisse 1";
    const int ie1 = p.ajouter(e1);
    FonctionPiece x1;
    x1.type = TypeFonction::Extrusion;
    x1.nom = "Extrusion 1";
    x1.esquisseId = ie1;
    const int ix1 = p.ajouter(x1);
    FonctionPiece e2;
    e2.type = TypeFonction::Esquisse;
    e2.nom = "Esquisse 2";
    if (surFace) e2.face = std::to_string(ix1) + ":sommet";
    const int ie2 = p.ajouter(e2);
    FonctionPiece x2;
    x2.type = TypeFonction::Extrusion;
    x2.nom = "Extrusion 2";
    x2.esquisseId = ie2;
    p.ajouter(x2);
    return p;
  };
  auto noms = [](const Piece& p) {
    std::string r;
    for (const auto& f : p.fonctions) r += (r.empty() ? "" : ",") + f.nom;
    return r;
  };
  Piece p = pieceQuatre(false);
  const int x2 = p.fonctions[3].id;
  REQUIRE(deplacerBloc(p, x2, -1));
  CHECK(noms(p) == "Esquisse 2,Extrusion 2,Esquisse 1,Extrusion 1");
  // L'esquisse rangée sous sa fonction déplace la fonction
  REQUIRE(deplacerBloc(p, p.fonctions[0].id, +1));
  CHECK(noms(p) == "Esquisse 1,Extrusion 1,Esquisse 2,Extrusion 2");
  std::string raison;
  CHECK_FALSE(deplacerBloc(p, x2, +1, &raison));  // déjà en fin
  // Esquisse 2 posée sur une face de l'extrusion 1 : le bloc ne peut pas monter au-dessus
  Piece q = pieceQuatre(true);
  CHECK_FALSE(deplacerBloc(q, q.fonctions[3].id, -1, &raison));
  CHECK(raison.find("Extrusion 1") != std::string::npos);
  CHECK(noms(q) == "Esquisse 1,Extrusion 1,Esquisse 2,Extrusion 2");
}

TEST_CASE("AP-16 aires écrites dans l'unité du document au carré", "[unites][AP-16]") {
  Unites u;
  u.longueur = UniteLongueur::Centimetre;
  CHECK(ecrireAire(100, u) == "1 cm²");
  u.longueur = UniteLongueur::Pouce;
  CHECK(ecrireAire(645.16, u) == "1 in²");
  u.longueur = UniteLongueur::Metre;
  CHECK(ecrireAire(1e6, u) == "1 m²");
  u.longueur = UniteLongueur::Millimetre;
  CHECK(ecrireAire(12.5, u) == "12,5 mm²");
}

TEST_CASE("AP-23 noms de variables citables", "[pieces][AP-23]") {
  CHECK(defautNomVariable("L").empty());
  CHECK(defautNomVariable("epaisseur_2").empty());
  CHECK_FALSE(defautNomVariable("épaisseur").empty());
  CHECK_FALSE(defautNomVariable("2a").empty());
  CHECK_FALSE(defautNomVariable("cm").empty());
  CHECK_FALSE(defautNomVariable("a b").empty());
  CHECK_FALSE(defautNomVariable("").empty());
}

TEST_CASE("EX-06 géométrie importée gardée une fois : références dans le projet et l'historique, textes à part dans le fichier", "[pieces][EX-06]") {
  Piece src;
  plaque(src, 10);
  const std::string brep = brepDepuisForme(construirePiece(src).forme);
  REQUIRE(brep.size() >= TAILLE_DEPOT_MIN);
  Piece imp;
  FonctionPiece f;
  f.type = TypeFonction::Import;
  f.donnees = brep;  // forme d'un ancien fichier : le texte dans la fonction
  f.source = "plaque.step";
  imp.ajouter(f);
  // Relue, la pièce ne porte plus que la référence ; la forme se construit depuis le dépôt
  const Piece relue = pieceDepuisJson(pieceVersJson(imp));
  REQUIRE(estReferenceGeometrie(relue.fonctions.back().donnees));
  CHECK(volume(construirePiece(relue).forme) == Approx(24000).epsilon(1e-6));
  Projet p = projetVide();
  p.pieces.push_back(pieceVersJson(relue));
  p.pieces.push_back(pieceVersJson(relue));  // deux pièces, une seule géométrie
  const std::string instantane = versJson(p, false).dump();
  const std::string fichier = versJson(p).dump();
  CHECK(instantane.size() < 4000);
  CHECK(fichier.size() > brep.size());
  CHECK(fichier.size() < 2 * brep.size());  // le texte n'est écrit qu'une fois
  // Ancien format (texte dans la pièce) et nouveau (texte à part) se relisent à l'identique
  const Projet q = normaliser(Json::parse(fichier));
  REQUIRE(q.pieces.size() == 2);
  CHECK(volume(construirePiece(pieceDepuisJson(q.pieces[1])).forme) == Approx(24000).epsilon(1e-6));
  Json ancien = versJson(p, false);
  ancien["pieces"][0]["fonctions"].back()["donnees"] = brep;
  const Projet r = normaliser(ancien);
  CHECK(estReferenceGeometrie(r.pieces[0]["fonctions"].back()["donnees"].get<std::string>()));
  // Une géométrie abîmée dans le fichier est signalée
  Json abime = Json::parse(fichier);
  for (auto it = abime["geometries"].begin(); it != abime["geometries"].end(); ++it) it.value() = it.value().get<std::string>() + " ";
  std::vector<std::string> alertes;
  (void)normaliser(abime, &alertes);
  CHECK(std::any_of(alertes.begin(), alertes.end(), [](const std::string& a) { return a.find("abîmée") != std::string::npos; }));
}
