// Prométhée : banc d'essais mécanique des audits (matrice CAD-01 à CAD-05, ASM-01, ASM-02, IO-01 ; suite
// M-01 à M-12). Chaque cas compare le résultat à une valeur calculée à la main, ou vérifie qu'un échec est
// explicite et non destructif. Licence GPL-3.0-only.
#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shell.hxx>

#include "promethee/assemblage.hpp"
#include "promethee/construction.hpp"
#include "promethee/echanges.hpp"
#include "promethee/esquisse.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/structure.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/piece.hpp"

using namespace prom;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

namespace {
constexpr double PI = std::numbers::pi;

// Bloc L × W × H, coin à l'origine, esquisse entièrement contrainte.
struct Bloc {
  Piece p;
  int esquisse = 0, extrusion = 0;
  std::vector<int> lignes;  // bas, droite, haut, gauche
  int coteL = 0, coteW = 0;
  std::string face(const std::string& quoi) const { return std::to_string(extrusion) + ":" + quoi; }
  std::string cote(int i) const { return std::to_string(extrusion) + ":cote:" + std::to_string(lignes[static_cast<size_t>(i)]); }
  // Arête verticale entre les flancs i et i+1
  std::string areteVerticale(int i) const {
    const std::string a = cote(i), b = cote((i + 1) % 4);
    return a < b ? a + "|" + b : b + "|" + a;
  }
};

Bloc bloc(double L, double W, double H, double densite = 1.0) {
  Bloc b;
  b.p.densite = densite;
  b.p.densiteConfirmee = true;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  b.lignes = e.esquisse.ajouterRectangle(0.2, 0.1, L * 0.9, W * 1.2);
  e.esquisse.contraindre(TypeContrainte::Coincidence, {e.esquisse.element(b.lignes[0])->p1, 0});
  b.coteL = e.esquisse.contraindre(TypeContrainte::Longueur, {b.lignes[0]}, L);
  b.coteW = e.esquisse.contraindre(TypeContrainte::Longueur, {b.lignes[1]}, W);
  REQUIRE(resoudre(e.esquisse).degresLiberte == 0);
  b.esquisse = b.p.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = b.esquisse;
  x.longueur = H;
  b.extrusion = b.p.ajouter(x);
  return b;
}

// Esquisse posée sur une face (ou sur le plan XY si « face » est vide) avec des cercles (x, y, rayon).
int esquisseCercles(Piece& p, const std::string& face, const std::vector<std::array<double, 3>>& cercles, PlanBase plan = PlanBase::XY) {
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.face = face;
  e.plan = plan;
  for (const auto& [x, y, r] : cercles) e.esquisse.ajouterCercle(e.esquisse.ajouterPoint(x, y), r);
  return p.ajouter(e);
}

int extrusion(Piece& p, int esquisse, double longueur, bool enlever = false, bool traversant = false, bool inverse = false) {
  FonctionPiece f;
  f.type = TypeFonction::Extrusion;
  f.esquisseId = esquisse;
  f.longueur = longueur;
  f.enlever = enlever;
  f.traversant = traversant;
  f.inverse = inverse;
  return p.ajouter(f);
}

std::string dossierEssais(const std::string& nom) {
  const fs::path d = fs::temp_directory_path() / ("promethee_meca_" + nom);
  fs::remove_all(d);
  fs::create_directories(d);
  return d.string();
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------
TEST_CASE("M-01 / CAD-03 : pavé 20 × 10 × 5 et trou Ø 5 traversant, volumes analytiques", "[mecanique][CAD-03][M-01]") {
  Bloc b = bloc(20, 10, 5);
  ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  ProprietesMasse pm = proprietesMasse(r.forme, 1.0);
  CHECK(pm.statut == StatutMasse::Exact);
  CHECK(pm.volume == Approx(1000).margin(1e-6));
  CHECK(pm.cx == Approx(10).margin(1e-9));
  CHECK(pm.cy == Approx(5).margin(1e-9));
  CHECK(pm.cz == Approx(2.5).margin(1e-9));
  // Trou Ø 5 traversant au centre de la face du dessus : 98,17477 mm³ retirés
  const int e = esquisseCercles(b.p, b.face("sommet"), {{10, 5, 2.5}});
  const int trou = extrusion(b.p, e, 0, true, true);
  // Vers l'extérieur de la face, l'enlèvement ne retire rien : erreur explicite, pas de résultat silencieux
  r = construirePiece(b.p);
  CHECK_THAT(r.erreur(trou), ContainsSubstring("ne retire aucune matière"));
  CHECK(volume(r.forme) == Approx(1000).margin(1e-6));
  b.p.fonction(trou)->inverse = true;
  r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  pm = proprietesMasse(r.forme, 1.0);
  CHECK(1000 - pm.volume == Approx(PI * 2.5 * 2.5 * 5).margin(1e-6));
  CHECK(PI * 2.5 * 2.5 * 5 == Approx(98.17477).margin(1e-5));
  CHECK(pm.statut == StatutMasse::Exact);
  // Même trou par la fonction Perçage (simple, traversant), et trou borgne de 3 mm à fond plat
  Bloc c = bloc(20, 10, 5);
  FonctionPiece pts;
  pts.type = TypeFonction::Esquisse;
  pts.face = c.face("sommet");
  pts.esquisse.ajouterPoint(10, 5);
  const int ip = c.p.ajouter(pts);
  FonctionPiece per;
  per.type = TypeFonction::Percage;
  per.esquisseId = ip;
  per.typePercage = "simple";
  per.diametre = 5;
  per.traversant = true;
  const int idPercage = c.p.ajouter(per);
  r = construirePiece(c.p);
  REQUIRE(r.erreurs.empty());
  CHECK(1000 - volume(r.forme) == Approx(98.17477).margin(1e-4));
  c.p.fonction(idPercage)->traversant = false;
  c.p.fonction(idPercage)->profondeur = 3;
  c.p.fonction(idPercage)->pointeForet = false;
  r = construirePiece(c.p);
  REQUIRE(r.erreurs.empty());
  CHECK(1000 - volume(r.forme) == Approx(PI * 6.25 * 3).margin(1e-4));
}

TEST_CASE("M-01 / CAD-05 : pavé 10 × 20 × 30, volume, surface, centre de gravité, masse et inertie", "[mecanique][CAD-05][M-01]") {
  Bloc b = bloc(10, 20, 30);
  const ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  const ProprietesMasse pm = proprietesMasse(r.forme, 1.0);  // 1 g/cm³
  CHECK(pm.statut == StatutMasse::Exact);
  CHECK(pm.valide);
  CHECK(pm.volume == Approx(6000).margin(1e-6));
  CHECK(pm.aire == Approx(2200).margin(1e-6));
  CHECK(pm.cx == Approx(5).margin(1e-9));
  CHECK(pm.cy == Approx(10).margin(1e-9));
  CHECK(pm.cz == Approx(15).margin(1e-9));
  CHECK(pm.masse == Approx(6).margin(1e-9));  // g
  // Moments au centre de gravité : m (b² + c²) / 12, en g·mm²
  CHECK(pm.ixx == Approx(6 * (20 * 20 + 30 * 30) / 12.0).epsilon(1e-9));
  CHECK(pm.iyy == Approx(6 * (10 * 10 + 30 * 30) / 12.0).epsilon(1e-9));
  CHECK(pm.izz == Approx(6 * (10 * 10 + 20 * 20) / 12.0).epsilon(1e-9));
  // Cube de 10 mm à 1 g/cm³ : 1 cm³, 1 g ; en acier (7,85) : 7,85 g
  const Bloc cube = bloc(10, 10, 10);
  const ResultatPiece rc = construirePiece(cube.p);
  CHECK(proprietesMasse(rc.forme, 1.0).masse == Approx(1).margin(1e-9));
  CHECK(proprietesMasse(rc.forme, 7.85).masse == Approx(7.85).margin(1e-9));
}

TEST_CASE("M-02 / CAD-05 : cylindre Ø 10 × 20 et sphère, volume et inertie analytiques", "[mecanique][CAD-05][M-02]") {
  Piece p;
  const int e = esquisseCercles(p, "", {{0, 0, 5}});
  extrusion(p, e, 20);
  const ResultatPiece r = construirePiece(p);
  REQUIRE(r.erreurs.empty());
  const ProprietesMasse pm = proprietesMasse(r.forme, 1.0);
  const double m = PI * 25 * 20 / 1000;  // g
  CHECK(pm.volume == Approx(PI * 25 * 20).epsilon(1e-9));
  CHECK(pm.masse == Approx(m).epsilon(1e-9));
  CHECK(pm.cz == Approx(10).margin(1e-9));
  CHECK(pm.izz == Approx(m * 25 / 2).epsilon(1e-7));                 // m r² / 2
  CHECK(pm.ixx == Approx(m * (3 * 25 + 20 * 20) / 12).epsilon(1e-7));  // m (3 r² + h²) / 12
  CHECK(pm.iyy == Approx(pm.ixx).epsilon(1e-9));
  // Sphère de rayon 10 : demi-disque tourné autour de son diamètre
  Piece s;
  FonctionPiece es;
  es.type = TypeFonction::Esquisse;
  Esquisse& k = es.esquisse;
  const int c = k.ajouterPoint(0, 0), a = k.ajouterPoint(0, -10), b = k.ajouterPoint(0, 10);
  const int axe = k.ajouterLigne(a, b);
  k.ajouterArc(c, a, b);  // demi-cercle côté x > 0
  const int ies = s.ajouter(es);
  FonctionPiece rv;
  rv.type = TypeFonction::Revolution;
  rv.esquisseId = ies;
  rv.axe = axe;
  rv.angle = 360;
  s.ajouter(rv);
  const ResultatPiece rs = construirePiece(s);
  REQUIRE(rs.erreurs.empty());
  const ProprietesMasse ps = proprietesMasse(rs.forme, 1.0);
  const double ms = 4.0 / 3 * PI * 1000 / 1000;
  CHECK(ps.volume == Approx(4.0 / 3 * PI * 1000).epsilon(1e-6));
  CHECK(ps.ixx == Approx(0.4 * ms * 100).epsilon(1e-5));
  CHECK(ps.statut == StatutMasse::Exact);
}

TEST_CASE("M-12 / CAD-05 : géométrie invalide, vide ou multiple : statut explicite, jamais « exact »", "[mecanique][CAD-05][M-12]") {
  // Boîte dont on retire une face : coque ouverte transformée en « solide »
  const TopoDS_Shape boite = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
  BRepBuilderAPI_Sewing couture;
  int n = 0;
  for (TopExp_Explorer ex(boite, TopAbs_FACE); ex.More(); ex.Next())
    if (n++ < 5) couture.Add(ex.Current());
  couture.Perform();
  TopoDS_Shell coque;
  for (TopExp_Explorer ex(couture.SewedShape(), TopAbs_SHELL); ex.More(); ex.Next()) coque = TopoDS::Shell(ex.Current());
  REQUIRE_FALSE(coque.IsNull());
  const TopoDS_Shape ouvert = BRepBuilderAPI_MakeSolid(coque).Solid();
  CHECK_FALSE(defautGeometrie(ouvert).empty());
  const ProprietesMasse pm = proprietesMasse(ouvert, 1.0);
  CHECK(pm.statut == StatutMasse::Approche);
  CHECK_FALSE(pm.valide);
  CHECK_FALSE(pm.defaut.empty());
  CHECK_THAT(libelleStatut(pm.statut), ContainsSubstring("approché"));
  // Rien : statut vide
  CHECK(proprietesMasse(TopoDS_Shape(), 1.0).statut == StatutMasse::Vide);
  CHECK_FALSE(proprietesMasse(TopoDS_Shape(), 1.0).valide);
  // Deux solides disjoints : valeurs cumulées, signalées comme telles
  TopoDS_Compound deux;
  BRep_Builder bb;
  bb.MakeCompound(deux);
  bb.Add(deux, BRepPrimAPI_MakeBox(10, 10, 10).Shape());
  bb.Add(deux, BRepPrimAPI_MakeBox(gp_Pnt(20, 0, 0), 10, 10, 10).Shape());
  const ProprietesMasse p2 = proprietesMasse(deux, 1.0);
  CHECK(p2.statut == StatutMasse::PlusieursSolides);
  CHECK(p2.solides == 2);
  CHECK(p2.volume == Approx(2000).margin(1e-6));
  CHECK(p2.cx == Approx(15).margin(1e-9));
  // Une pièce invalide est écartée des propriétés d'un assemblage, et le compte rendu le dit
  Bloc a = bloc(10, 10, 10);
  std::vector<Piece> defs = {a.p, a.p};
  std::vector<ResultatPiece> res = {construirePiece(a.p), construirePiece(a.p)};
  res[1].forme = ouvert;
  Assemblage as;
  as.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  as.composants.push_back({1, 20, 0, 0, 0, 0, 0, false});
  const ProprietesAssemblage pa = proprietesAssemblage({as}, 0, defs, res);
  CHECK(pa.pieces == 1);
  REQUIRE(pa.exclues.size() == 1);
  CHECK_FALSE(pa.fiable());
}

TEST_CASE("M-07 : densité absente, nulle, négative ou illisible : défaut signalé comme provisoire", "[mecanique][CAD-05][M-07]") {
  Json j = pieceVersJson(Piece());
  CHECK_FALSE(pieceDepuisJson(j).densiteConfirmee);  // pièce neuve : PLA par défaut, provisoire
  for (const Json& v : {Json(0.0), Json(-3.0), Json("dense"), Json(nullptr), Json(1e9)}) {
    j["materiau"] = "Acier";
    j["densite"] = v;
    const Piece p = pieceDepuisJson(j);
    CHECK(p.densite == Approx(1.24));
    CHECK_FALSE(p.densiteConfirmee);
  }
  j["densite"] = 7.85;
  CHECK(pieceDepuisJson(j).densiteConfirmee);  // matière choisie et densité lisible
  Piece choisie;
  choisie.densite = 1.24;
  choisie.densiteConfirmee = true;  // PLA choisi explicitement
  CHECK(pieceDepuisJson(pieceVersJson(choisie)).densiteConfirmee);
}

// ---------------------------------------------------------------------------------------------------------
TEST_CASE("M-03 / CAD-01 : degrés de liberté exacts, du libre au complètement contraint", "[mecanique][CAD-01][M-03]") {
  Esquisse point;
  point.ajouterPoint(3, 4);
  CHECK(resoudre(point).degresLiberte == 2);
  Esquisse ligne;
  ligne.ajouterLigne(ligne.ajouterPoint(0, 0), ligne.ajouterPoint(10, 2));
  CHECK(resoudre(ligne).degresLiberte == 4);
  Esquisse cercle;
  cercle.ajouterCercle(cercle.ajouterPoint(5, 5), 3);
  CHECK(resoudre(cercle).degresLiberte == 3);
  Esquisse r;
  const auto l = r.ajouterRectangle(1, 1, 50, 30);
  EtatEsquisse s = resoudre(r);
  CHECK(s.degresLiberte == 4);
  CHECK(s.message != "Esquisse entièrement contrainte.");
  // Tant qu'il reste des libertés, glisser un coin le déplace
  const int coin = r.element(l[1])->p2;
  s = resoudre(r, {{coin, Point{60, 35}}});
  CHECK(r.point(coin)->x == Approx(60).margin(1e-6));
  r.contraindre(TypeContrainte::Coincidence, {r.element(l[0])->p1, 0});
  CHECK(resoudre(r).degresLiberte == 2);
  r.contraindre(TypeContrainte::Longueur, {l[0]}, 40);
  r.contraindre(TypeContrainte::Longueur, {l[1]}, 25);
  s = resoudre(r);
  CHECK(s.degresLiberte == 0);
  CHECK(s.message == "Esquisse entièrement contrainte.");
  CHECK(r.point(coin)->x == Approx(40).margin(1e-9));
  CHECK(r.point(coin)->y == Approx(25).margin(1e-9));
  // Contrainte complète : glisser ne déplace plus rien
  resoudre(r, {{coin, Point{70, 70}}});
  CHECK(r.point(coin)->x == Approx(40).margin(1e-9));
  CHECK(r.point(coin)->y == Approx(25).margin(1e-9));
  // Même problème construit dans un autre ordre : même solution
  Esquisse q;
  const auto m = q.ajouterRectangle(3, 2, 41, 27);
  q.contraindre(TypeContrainte::Longueur, {m[1]}, 25);
  q.contraindre(TypeContrainte::Longueur, {m[0]}, 40);
  q.contraindre(TypeContrainte::Coincidence, {q.element(m[0])->p1, 0});
  CHECK(resoudre(q).degresLiberte == 0);
  CHECK(q.point(q.element(m[1])->p2)->x == Approx(40).margin(1e-9));
  // Sauvegarde et relecture : mêmes cotes, mêmes libertés
  Piece pc;
  FonctionPiece fe;
  fe.type = TypeFonction::Esquisse;
  fe.esquisse = r;
  pc.ajouter(fe);
  Piece relue = pieceDepuisJson(pieceVersJson(pc));
  CHECK(resoudre(relue.fonctions[0].esquisse).degresLiberte == 0);
  CHECK(relue.fonctions[0].esquisse.point(coin)->x == Approx(40).margin(1e-9));
}

TEST_CASE("CAD-01 : expressions (ordre, nom inconnu, division par zéro, unités, débordement)", "[mecanique][CAD-01]") {
  Piece p;
  p.variables = {{"L", "60"}, {"e", "L/4"}, {"f", "g*2"}, {"g", "5"}, {"z", "L/(e-15)"}, {"a", "10 mm + 5°"}, {"h", "1e300*1e300"}, {"k", "e + 2 cm"}};
  std::string err;
  const auto v = evaluerVariables(p, &err);
  CHECK(v.at("L") == Approx(60));
  CHECK(v.at("e") == Approx(15));
  CHECK(v.count("f") == 0);  // cite une variable définie seulement après : refusée (pas de dépendance circulaire possible)
  CHECK(v.at("g") == Approx(5));
  CHECK(v.count("z") == 0);  // division par zéro
  CHECK(v.count("a") == 0);  // longueur + angle
  CHECK(v.count("h") == 0);  // débordement : jamais d'infini
  CHECK(v.at("k") == Approx(35));
  CHECK_THAT(err, ContainsSubstring("f"));
  // Une cote pilotée suit la variable ; une expression fautive laisse la cote telle quelle et le dit
  Bloc b = bloc(40, 20, 5);
  b.p.variables = {{"L", "50"}};
  FonctionPiece* e = b.p.fonction(b.esquisse);
  for (auto& c : e->esquisse.contraintes)
    if (c.id == b.coteL) c.expression = "L";
  std::string e1;
  CHECK(appliquerVariables(b.p, &e1));
  CHECK(e1.empty());
  CHECK(volume(construirePiece(b.p).forme) == Approx(50 * 20 * 5).epsilon(1e-9));
  for (auto& c : b.p.fonction(b.esquisse)->esquisse.contraintes)
    if (c.id == b.coteL) c.expression = "L/0";
  std::string e2;
  CHECK_FALSE(appliquerVariables(b.p, &e2));
  CHECK_THAT(e2, ContainsSubstring("zéro"));
  CHECK(volume(construirePiece(b.p).forme) == Approx(50 * 20 * 5).epsilon(1e-9));
}

TEST_CASE("CAD-01 : très petit, très grand, loin de l'origine ; modification sans saut de solution ; relecture exacte", "[mecanique][CAD-01]") {
  for (const auto& [l, w, x0, y0] : {std::array<double, 4>{0.02, 0.01, 0, 0}, {10000, 5000, 0, 0}, {30, 20, 1e5, -2e5}}) {
    INFO(l << " × " << w << " en (" << x0 << " ; " << y0 << ")");
    Esquisse s;
    const auto lignes = s.ajouterRectangle(x0, y0, x0 + l * 0.9, y0 + w * 1.1);
    const int pa = s.element(lignes[0])->p1;
    const int fixe = s.ajouterPoint(x0, y0);
    s.contraindre(TypeContrainte::Fixe, {fixe}, x0, y0);
    s.contraindre(TypeContrainte::Coincidence, {pa, fixe});
    const int cl = s.contraindre(TypeContrainte::Longueur, {lignes[0]}, l);
    s.contraindre(TypeContrainte::Longueur, {lignes[1]}, w);
    const EtatEsquisse st = resoudre(s);
    CHECK(st.resolue);
    CHECK(st.degresLiberte == 0);
    const auto* bas = s.element(lignes[0]);
    const double lu = std::hypot(s.point(bas->p2)->x - s.point(bas->p1)->x, s.point(bas->p2)->y - s.point(bas->p1)->y);
    CHECK(lu == Approx(l).epsilon(1e-9));
    // Agrandir la cote de 10 % : le rectangle s'allonge du même côté, sans basculer de l'autre
    const double xDroitAvant = s.point(bas->p2)->x;
    for (auto& c : s.contraintes)
      if (c.id == cl) c.valeur = l * 1.1;
    REQUIRE(resoudre(s).resolue);
    CHECK((s.point(bas->p2)->x - x0) * (xDroitAvant - x0) > 0);
    CHECK(std::abs(s.point(bas->p2)->x - x0) == Approx(l * 1.1).epsilon(1e-9));
  }
  // Esquisse entièrement cotée, une cote pilotée par une variable : relue à l'identique
  Bloc b = bloc(40, 20, 5);
  b.p.variables = {{"L", "44"}};
  for (auto& c : b.p.fonction(b.esquisse)->esquisse.contraintes)
    if (c.id == b.coteL) c.expression = "L";
  appliquerVariables(b.p);
  const Piece relue = pieceDepuisJson(pieceVersJson(b.p));
  const Esquisse& a = b.p.fonction(b.esquisse)->esquisse;
  const Esquisse& r = relue.fonction(b.esquisse)->esquisse;
  REQUIRE(r.contraintes.size() == a.contraintes.size());
  for (std::size_t i = 0; i < a.contraintes.size(); ++i) {
    CHECK(r.contraintes[i].valeur == a.contraintes[i].valeur);
    CHECK(r.contraintes[i].expression == a.contraintes[i].expression);
  }
  REQUIRE(r.points.size() == a.points.size());
  for (std::size_t i = 0; i < a.points.size(); ++i) {
    CHECK(r.points[i].x == a.points[i].x);
    CHECK(r.points[i].y == a.points[i].y);
  }
  Esquisse r2 = r;
  CHECK(resoudre(r2).degresLiberte == 0);
}

TEST_CASE("M-04 / CAD-01 : contrainte contradictoire signalée, dernière solution valide gardée", "[mecanique][CAD-01][M-04]") {
  Esquisse e;
  const auto l = e.ajouterRectangle(0, 0, 60, 40);
  e.contraindre(TypeContrainte::Coincidence, {e.element(l[0])->p1, 0});
  e.contraindre(TypeContrainte::Longueur, {l[0]}, 60);
  e.contraindre(TypeContrainte::Longueur, {l[1]}, 40);
  REQUIRE(resoudre(e).degresLiberte == 0);
  std::vector<std::pair<double, double>> avant;
  for (const auto& p : e.points) avant.push_back({p.x, p.y});
  // Le côté du haut ne peut pas mesurer 50 si celui du bas en mesure 60 (rectangle)
  const int contradiction = e.contraindre(TypeContrainte::Longueur, {l[2]}, 50);
  const EtatEsquisse s = resoudre(e);
  CHECK_FALSE(s.resolue);
  CHECK_FALSE(s.enConflit.empty());
  CHECK(std::find(s.enConflit.begin(), s.enConflit.end(), contradiction) != s.enConflit.end());
  // La géométrie n'a pas bougé
  for (size_t i = 0; i < e.points.size(); ++i) {
    CHECK(e.points[i].x == Approx(avant[i].first).margin(1e-9));
    CHECK(e.points[i].y == Approx(avant[i].second).margin(1e-9));
  }
  // Retirer la contradiction rend l'état précédent, sans dériver
  e.contraintes.erase(std::remove_if(e.contraintes.begin(), e.contraintes.end(), [&](const ContrainteEsquisse& c) { return c.id == contradiction; }), e.contraintes.end());
  CHECK(resoudre(e).degresLiberte == 0);
  for (size_t i = 0; i < e.points.size(); ++i) {
    CHECK(e.points[i].x == Approx(avant[i].first).margin(1e-9));
    CHECK(e.points[i].y == Approx(avant[i].second).margin(1e-9));
  }
}

TEST_CASE("M-05 / CAD-03 : profil ouvert, arête nulle, cercle dégénéré : erreur ciblée, aucun solide faux", "[mecanique][CAD-03][M-05]") {
  // Profil ouvert : trois côtés d'un carré
  Piece p;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  const int a = e.esquisse.ajouterPoint(0, 0), b = e.esquisse.ajouterPoint(10, 0), c = e.esquisse.ajouterPoint(10, 10), d = e.esquisse.ajouterPoint(0, 10);
  e.esquisse.ajouterLigne(a, b);
  e.esquisse.ajouterLigne(b, c);
  e.esquisse.ajouterLigne(c, d);
  const int ie = p.ajouter(e);
  const int ix = extrusion(p, ie, 5);
  ResultatPiece r = construirePiece(p);
  CHECK_FALSE(r.erreur(ix).empty());
  CHECK(r.forme.IsNull());
  CHECK(r.premiereErreur == ix);
  // Cercle de rayon nul : rien à extruder, erreur
  Piece q;
  const int iq = esquisseCercles(q, "", {{0, 0, 0}});
  const int xq = extrusion(q, iq, 5);
  r = construirePiece(q);
  CHECK_FALSE(r.erreur(xq).empty());
  CHECK(r.forme.IsNull());
  // Rectangle fermé avec une arête de longueur nulle en plus : le profil reste juste, ou l'erreur est explicite
  Bloc bl = bloc(20, 10, 5);
  FonctionPiece& esq = *bl.p.fonction(bl.esquisse);
  const int pz = esq.esquisse.ajouterPoint(5, 5);
  esq.esquisse.ajouterLigne(pz, esq.esquisse.ajouterPoint(5, 5));
  r = construirePiece(bl.p);
  if (r.erreurs.empty()) {
    CHECK(volume(r.forme) == Approx(1000).margin(1e-6));
    CHECK(defautGeometrie(r.forme).empty());
  } else {
    CHECK(r.forme.IsNull());
  }
}

TEST_CASE("M-06 / CAD-04 : congé, coque et révolution impossibles : échec explicite, solide précédent gardé, aval obsolète", "[mecanique][CAD-04][M-06]") {
  Bloc b = bloc(20, 10, 5);
  FonctionPiece conge;
  conge.type = TypeFonction::Conge;
  conge.references = {b.areteVerticale(0)};
  conge.rayon = 50;  // bien plus grand que les faces voisines
  const int ic = b.p.ajouter(conge);
  const int e2 = esquisseCercles(b.p, "", {{10, 5, 2}});
  const int bossage = extrusion(b.p, e2, 12);
  ResultatPiece r = construirePiece(b.p);
  CHECK_FALSE(r.erreur(ic).empty());
  CHECK(r.premiereErreur == ic);
  CHECK(r.obsolete(e2));
  CHECK(r.obsolete(bossage));
  CHECK(volume(r.forme) == Approx(1000).margin(1e-6));  // le dernier solide valide, sans le bossage
  CHECK(defautGeometrie(r.forme).empty());
  // Le rayon redevient raisonnable : tout se recalcule, plus rien d'obsolète
  b.p.fonction(ic)->rayon = 2;
  r = construirePiece(b.p);
  CHECK(r.erreurs.empty());
  CHECK(r.obsoletes.empty());
  CHECK(volume(r.forme) == Approx(1000 - (4 - PI) * 5 + PI * 4 * 7).epsilon(1e-6));  // le bossage dépasse de 7 mm
  // Coque plus épaisse que la pièce : refusée
  Bloc c = bloc(20, 10, 5);
  FonctionPiece coque;
  coque.type = TypeFonction::Coque;
  coque.references = {c.face("sommet")};
  coque.epaisseur = 6;
  const int ik = c.p.ajouter(coque);
  r = construirePiece(c.p);
  CHECK_FALSE(r.erreur(ik).empty());
  CHECK(volume(r.forme) == Approx(1000).margin(1e-6));
  // Coque raisonnable : volume exact d'une boîte ouverte de 1 mm
  c.p.fonction(ik)->epaisseur = 1;
  r = construirePiece(c.p);
  CHECK(r.erreur(ik).empty());
  CHECK(volume(r.forme) == Approx(1000 - 18 * 8 * 4).epsilon(1e-6));
  // Révolution d'un profil qui traverse son axe : refusée, aucun solide
  Piece rv;
  FonctionPiece er;
  er.type = TypeFonction::Esquisse;
  const auto lr = er.esquisse.ajouterRectangle(-5, 0, 5, 10);
  const int ier = rv.ajouter(er);
  FonctionPiece fr;
  fr.type = TypeFonction::Revolution;
  fr.esquisseId = ier;
  fr.axeBase = "Y";
  const int ifr = rv.ajouter(fr);
  (void)lr;
  r = construirePiece(rv);
  CHECK_FALSE(r.erreur(ifr).empty());
  CHECK(r.forme.IsNull());
  // Balayage sans chemin, lissage d'un seul profil ou de deux profils confondus : refusés, solide précédent gardé
  Bloc d = bloc(20, 10, 5);
  const int profil = esquisseCercles(d.p, d.face("sommet"), {{10, 5, 2}});
  FonctionPiece ba;
  ba.type = TypeFonction::Balayage;
  ba.esquisseId = profil;
  ba.cheminId = 999;  // aucune esquisse de ce numéro
  const int ib = d.p.ajouter(ba);
  const int apres = extrusion(d.p, esquisseCercles(d.p, "", {{5, 5, 1}}), 8);
  r = construirePiece(d.p);
  CHECK_FALSE(r.erreur(ib).empty());
  CHECK(r.obsolete(apres));
  CHECK(volume(r.forme) == Approx(1000).margin(1e-6));
  for (const bool confondus : {false, true}) {
    Bloc l = bloc(20, 10, 5);
    FonctionPiece s1;
    s1.type = TypeFonction::Esquisse;
    s1.decalage = 10;
    s1.esquisse.ajouterRectangle(2, 2, 8, 8);
    const int i1 = l.p.ajouter(s1);
    FonctionPiece s2 = s1;
    const int i2 = l.p.ajouter(s2);
    FonctionPiece li;
    li.type = TypeFonction::Lissage;
    li.sources = confondus ? std::vector<int>{i1, i2} : std::vector<int>{i1};
    const int il = l.p.ajouter(li);
    r = construirePiece(l.p);
    INFO((confondus ? "deux profils confondus" : "un seul profil"));
    CHECK_FALSE(r.erreur(il).empty());
    CHECK(volume(r.forme) == Approx(1000).margin(1e-6));
    CHECK(defautGeometrie(r.forme).empty());
  }
}

// ---------------------------------------------------------------------------------------------------------
TEST_CASE("M-07 / CAD-02 : références maintenues après une modification amont", "[mecanique][CAD-02][M-07]") {
  // Bloc, esquisse sur la face du dessus, poche, congés des arêtes verticales, puis on élargit le bloc
  Bloc b = bloc(60, 40, 10);
  const int e = esquisseCercles(b.p, b.face("sommet"), {{30, 20, 4}});
  const int poche = extrusion(b.p, e, 4, true, false, true);
  FonctionPiece conge;
  conge.type = TypeFonction::Conge;
  for (int i = 0; i < 4; ++i) conge.references.push_back(b.areteVerticale(i));
  conge.rayon = 3;
  const int ic = b.p.ajouter(conge);
  ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  const double retrait = 4 * (9 - PI * 9 / 4) * 10;
  CHECK(volume(r.forme) == Approx(60 * 40 * 10 - PI * 16 * 4 - retrait).epsilon(1e-6));
  for (auto& k : b.p.fonction(b.esquisse)->esquisse.contraintes) if (k.id == b.coteW) k.valeur = 50;
  REQUIRE(resoudre(b.p.fonction(b.esquisse)->esquisse).resolue);
  b.p.fonction(b.extrusion)->longueur = 15;
  r = construirePiece(b.p);
  CHECK(r.erreurs.empty());
  CHECK(r.erreur(poche).empty());
  CHECK(r.erreur(ic).empty());
  CHECK(r.repere(e).Location().Z() == Approx(15));
  CHECK(volume(r.forme) == Approx(60 * 50 * 15 - PI * 16 * 4 - 4 * (9 - PI * 9 / 4) * 15).epsilon(1e-6));
  // Enregistrer, relire : même résultat
  const Piece relue = pieceDepuisJson(pieceVersJson(b.p));
  const ResultatPiece r2 = construirePiece(relue);
  CHECK(r2.erreurs.empty());
  CHECK(volume(r2.forme) == Approx(volume(r.forme)).epsilon(1e-12));
}

TEST_CASE("M-07 / CAD-02 : une référence disparue casse la fonction, sans la rattacher ailleurs", "[mecanique][CAD-02][M-07]") {
  // Un bossage cylindrique sur le bloc ; un chanfrein sur son arête du haut ; puis on désactive le bossage.
  Bloc b = bloc(40, 30, 10);
  const int e = esquisseCercles(b.p, b.face("sommet"), {{20, 15, 5}});
  const int bossage = extrusion(b.p, e, 8);
  ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  std::string arete;
  for (TopExp_Explorer ex(r.forme, TopAbs_EDGE); ex.More() && arete.empty(); ex.Next()) {
    const std::string n = r.nomArete(TopoDS::Edge(ex.Current()));
    if (n.find(std::to_string(bossage) + ":sommet") != std::string::npos && n.find(":cote:") != std::string::npos) arete = n;
  }
  REQUIRE_FALSE(arete.empty());
  FonctionPiece ch;
  ch.type = TypeFonction::Chanfrein;
  ch.references = {arete};
  ch.distance = 1;
  const int ich = b.p.ajouter(ch);
  const int e3 = esquisseCercles(b.p, "", {{5, 5, 1}});
  const int trou = extrusion(b.p, e3, 0, true, true);
  r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  const double vAvec = volume(r.forme);
  b.p.fonction(bossage)->desactivee = true;
  r = construirePiece(b.p);
  CHECK_THAT(r.erreur(ich), ContainsSubstring("Référence cassée"));
  CHECK(r.premiereErreur == ich);
  CHECK(r.obsolete(trou));
  // Aucun chanfrein posé ailleurs : la pièce est exactement le bloc
  CHECK(volume(r.forme) == Approx(40 * 30 * 10).margin(1e-6));
  CHECK(vAvec > 40 * 30 * 10);
}

TEST_CASE("M-07 / CAD-02 : face coupée en morceaux, référence ambiguë déclarée", "[mecanique][CAD-02][M-07]") {
  // Rainure traversante qui coupe la face du dessus en deux ; une esquisse s'y pose ensuite.
  Bloc b = bloc(40, 20, 10);
  FonctionPiece er;
  er.type = TypeFonction::Esquisse;
  er.face = b.face("sommet");
  const auto lr = er.esquisse.ajouterRectangle(18, -5, 22, 25);  // dépasse la largeur : coupe toute la face
  const int ie = b.p.ajouter(er);
  const int rainure = extrusion(b.p, ie, 3, true, false, true);
  ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  (void)lr;
  REQUIRE(r.facesNommees(b.face("sommet")).size() == 2);
  // Sans préciser le morceau : ambigu, la fonction échoue et le dit
  const int e2 = esquisseCercles(b.p, b.face("sommet"), {{5, 10, 2}});
  const int poche = extrusion(b.p, e2, 2, true, false, true);
  r = construirePiece(b.p);
  CHECK_THAT(r.erreur(e2), ContainsSubstring("morceaux"));
  CHECK(r.obsolete(poche));
  // Morceau désigné : « #k/n » tel que le produit la sélection à l'écran
  std::vector<TopoDS_Face> morceaux;
  REQUIRE(r.resoudreFaces(b.face("sommet"), morceaux, false).trouvee());
  const std::string morceau = r.nomFace(morceaux.front());
  CHECK_THAT(morceau, ContainsSubstring("#0/2"));
  b.p.fonction(e2)->face = morceau;
  r = construirePiece(b.p);
  CHECK(r.erreurs.empty());
  // La rainure ne traverse plus : la face n'a plus qu'un morceau, la référence « #0/2 » est déclarée ambiguë
  b.p.fonction(rainure)->desactivee = true;
  r = construirePiece(b.p);
  CHECK_THAT(r.erreur(e2), ContainsSubstring("Référence cassée"));
  CHECK(r.obsolete(poche));
  CHECK(volume(r.forme) == Approx(40 * 20 * 10).margin(1e-6));
}

TEST_CASE("CAD-02 : changer l'ordre des fonctions casse explicitement les références qui en dépendent", "[mecanique][CAD-02]") {
  Bloc b = bloc(30, 20, 10);
  FonctionPiece conge;
  conge.type = TypeFonction::Conge;
  conge.references = {b.areteVerticale(1)};
  conge.rayon = 2;
  const int ic = b.p.ajouter(conge);
  REQUIRE(construirePiece(b.p).erreurs.empty());
  // Le congé passe avant l'extrusion qui crée ses arêtes
  std::rotate(b.p.fonctions.begin() + 1, b.p.fonctions.begin() + 2, b.p.fonctions.end());
  REQUIRE(b.p.fonctions[1].id == ic);
  const ResultatPiece r = construirePiece(b.p);
  CHECK_FALSE(r.erreur(ic).empty());
  CHECK(r.obsolete(b.extrusion));
  CHECK(r.forme.IsNull());
}

TEST_CASE("CAD-03 : extrusion jusqu'à une face courbe, et décalée de cette face", "[mecanique][CAD-03]") {
  // Cylindre Ø 40 couché le long de X (axe à z = 30) ; un picot Ø 4 monte depuis z = 0 jusqu'à sa surface.
  Piece p;
  const int ec = esquisseCercles(p, "", {{0, 30, 20}}, PlanBase::YZ);
  extrusion(p, ec, 40);
  ResultatPiece r = construirePiece(p);
  REQUIRE(r.erreurs.empty());
  std::string flanc;
  for (const auto& [f, nom] : r.faces)
    if (nom.find(":cote:") != std::string::npos) flanc = nom;
  REQUIRE_FALSE(flanc.empty());
  const int ep = esquisseCercles(p, "", {{20, 0, 2}});
  FonctionPiece picot;
  picot.type = TypeFonction::Extrusion;
  picot.esquisseId = ep;
  picot.fin = "face";
  picot.refFin = flanc;
  const int ip = p.ajouter(picot);
  r = construirePiece(p);
  REQUIRE(r.erreur(ip).empty());
  // Le picot rejoint le cylindre : un seul solide, de volume cylindre + picot jusqu'à la surface (entre z = 10
  // sous l'axe et z = 10,1 au bord du picot)
  CHECK(nombreSolides(r.forme) == 1);
  const double vCyl = PI * 400 * 40;
  const double vPicot = volume(r.forme) - vCyl;
  CHECK(vPicot > PI * 4 * 10 - 1e-6);
  CHECK(vPicot < PI * 4 * (30 - std::sqrt(400.0 - 4)) + 1e-6);
  // Décalé de 2 mm avant la face : le picot s'arrête 2 mm plus bas, sous un cylindre de rayon 22
  p.fonction(ip)->fin = "decale";
  p.fonction(ip)->decalageFin = -2;
  r = construirePiece(p);
  REQUIRE(r.erreur(ip).empty());
  REQUIRE(nombreSolides(r.forme) == 2);  // le picot ne touche plus le cylindre
  double zmax = 0;
  for (TopExp_Explorer ex(r.forme, TopAbs_SOLID); ex.More(); ex.Next()) {
    const Boite bt = encombrement(ex.Current());
    if (bt.zmax < 9) zmax = bt.zmax;
  }
  CHECK(zmax == Approx(30 - std::sqrt(22.0 * 22 - 4)).margin(2e-3));
}

// ---------------------------------------------------------------------------------------------------------
TEST_CASE("M-10 / ASM-02 : contact, jeu faible, interférence, cavité et arête commune distingués", "[mecanique][ASM-02][M-10]") {
  const TopoDS_Shape a = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
  auto boite = [](double x, double y, double z, double l = 10) { return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), l, l, l).Shape(); };
  // Tangentes : contact, pas d'interférence
  AnalyseContact c = analyserContact(a, boite(10, 0, 0));
  CHECK(c.etat == EtatContact::Contact);
  CHECK(c.volumeCommun < 1e-3);
  // Séparées de 0,01 mm : jeu faible, distance exacte
  c = analyserContact(a, boite(10.01, 0, 0));
  CHECK(c.etat == EtatContact::JeuFaible);
  CHECK(c.distance == Approx(0.01).margin(1e-9));
  CHECK(c.pointA.X() == Approx(10).margin(1e-9));
  // Séparées de 0,5 mm
  c = analyserContact(a, boite(10.5, 0, 0));
  CHECK(c.etat == EtatContact::Separees);
  CHECK(c.distance == Approx(0.5).margin(1e-9));
  // Recouvrement de 0,5 mm : 50 mm³ en commun
  c = analyserContact(a, boite(9.5, 0, 0));
  CHECK(c.etat == EtatContact::Interference);
  CHECK(c.volumeCommun == Approx(50).margin(1e-6));
  // Contact sur une arête seulement
  c = analyserContact(a, boite(10, 10, 0));
  CHECK(c.etat == EtatContact::Contact);
  // Petit cube entièrement à l'intérieur : interférence de tout son volume
  c = analyserContact(a, boite(2, 2, 2, 3));
  CHECK(c.etat == EtatContact::Interference);
  CHECK(c.volumeCommun == Approx(27).margin(1e-6));
  // Cube dans la cavité d'une boîte creuse (paroi de 1 mm), sans la toucher : jeu de 1 mm
  const TopoDS_Shape creuse = BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(20, 20, 20).Shape(), BRepPrimAPI_MakeBox(gp_Pnt(1, 1, 1), 18, 18, 18).Shape()).Shape();
  c = analyserContact(creuse, boite(2, 2, 2, 5));
  CHECK(c.etat == EtatContact::Separees);
  CHECK(c.distance == Approx(1).margin(1e-9));
  // Géométrie invalide : indéterminé, avec la raison
  BRepBuilderAPI_Sewing couture;
  int n = 0;
  for (TopExp_Explorer ex(a, TopAbs_FACE); ex.More(); ex.Next()) if (n++ < 5) couture.Add(ex.Current());
  couture.Perform();
  TopoDS_Shell coque;
  for (TopExp_Explorer ex(couture.SewedShape(), TopAbs_SHELL); ex.More(); ex.Next()) coque = TopoDS::Shell(ex.Current());
  c = analyserContact(BRepBuilderAPI_MakeSolid(coque).Solid(), boite(20, 0, 0));
  CHECK(c.etat == EtatContact::Indetermine);
  CHECK_FALSE(c.detail.empty());
  // Seuils réglables
  TolerancesContact t;
  t.jeuFaible = 1;
  CHECK(analyserContact(a, boite(10.5, 0, 0), t).etat == EtatContact::JeuFaible);
}

TEST_CASE("ASM-01 : propriétés physiques d'un assemblage, sous-assemblage et matière provisoire", "[mecanique][ASM-01]") {
  // Deux cubes de 10 mm : A à l'origine (1 g/cm³), B décalé de 20 mm en X (2 g/cm³)
  Bloc a = bloc(10, 10, 10, 1.0), b = bloc(10, 10, 10, 2.0);
  const std::vector<Piece> defs = {a.p, b.p};
  const std::vector<ResultatPiece> res = {construirePiece(a.p), construirePiece(b.p)};
  Assemblage as;
  as.nom = "Principal";
  as.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  as.composants.push_back({1, 20, 0, 0, 0, 0, 0, false});
  ProprietesAssemblage pa = proprietesAssemblage({as}, 0, defs, res);
  CHECK(pa.masse == Approx(3).margin(1e-9));
  CHECK(pa.volume == Approx(2000).margin(1e-6));
  CHECK(pa.centre.X() == Approx((5 * 1 + 25 * 2) / 3.0).margin(1e-9));
  CHECK(pa.centre.Y() == Approx(5).margin(1e-9));
  CHECK(pa.fiable());
  // Le même assemblage en sous-assemblage, posé à z = 100, plus un cube A : sous-assemblage développé
  Assemblage haut;
  haut.nom = "Haut";
  haut.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  ComposantAssemblage sous{0, 0, 0, 100, 0, 0, 0, false};
  sous.assemblage = 0;
  haut.composants.push_back(sous);
  pa = proprietesAssemblage({as, haut}, 1, defs, res);
  CHECK(pa.pieces == 3);
  CHECK(pa.masse == Approx(4).margin(1e-9));
  CHECK(pa.centre.Z() == Approx((5 * 1 + 105 * 3) / 4.0).margin(1e-9));
  // Composant suspendu : exclu ; pièce à matière provisoire : comptée et signalée
  as.composants[1].supprime = true;
  pa = proprietesAssemblage({as}, 0, defs, res);
  CHECK(pa.pieces == 1);
  std::vector<Piece> defs2 = defs;
  defs2[0].densiteConfirmee = false;
  as.composants[1].supprime = false;
  pa = proprietesAssemblage({as}, 0, defs2, res);
  CHECK(pa.provisoires == 1);
  CHECK_FALSE(pa.fiable());
  // Boucle (un assemblage qui s'inclut lui-même) : signalée, pas de récursion infinie
  Assemblage boucle;
  ComposantAssemblage soi{0, 0, 0, 0, 0, 0, 0, false};
  soi.assemblage = 0;
  boucle.composants.push_back(soi);
  pa = proprietesAssemblage({boucle}, 0, defs, res);
  CHECK_FALSE(pa.exclues.empty());
}

TEST_CASE("M-08 / ASM-01 : pivot à un degré de liberté ; face d'une contrainte disparue, signalée sans redirection", "[mecanique][ASM-01][M-08]") {
  // Plaque percée Ø 10 et arbre Ø 10 × 40
  Bloc pl = bloc(60, 40, 10);
  FonctionPiece pts;
  pts.type = TypeFonction::Esquisse;
  pts.face = pl.face("sommet");
  pts.esquisse.ajouterPoint(30, 20);
  const int ip = pl.p.ajouter(pts);
  FonctionPiece tr;
  tr.type = TypeFonction::Percage;
  tr.esquisseId = ip;
  tr.typePercage = "simple";
  tr.diametre = 10;
  tr.traversant = true;
  const int it = pl.p.ajouter(tr);
  Piece arbre;
  FonctionPiece ec;
  ec.type = TypeFonction::Esquisse;
  const int cercle = ec.esquisse.ajouterCercle(ec.esquisse.ajouterPoint(0, 0), 5);
  const int iec = arbre.ajouter(ec);
  const int ixa = extrusion(arbre, iec, 40);
  std::vector<ResultatPiece> pieces = {construirePiece(pl.p), construirePiece(arbre)};
  std::string alesage;
  for (const auto& [f, nom] : pieces[0].faces)
    if (nom.rfind(std::to_string(it) + ":trou", 0) == 0) {
      BRepAdaptor_Surface su(f);
      if (su.GetType() == GeomAbs_Cylinder) alesage = nom;
    }
  REQUIRE_FALSE(alesage.empty());
  const std::string flanc = std::to_string(ixa) + ":cote:" + std::to_string(cercle);
  Assemblage a;
  a.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  a.composants.push_back({1, 30, 20, -15, 0, 0, 0, false});
  a.contraintes.push_back(creerLiaison(a, pieces, "pivot", 0, alesage, 1, flanc));
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  AnalyseAssemblage an = analyserAssemblage(a, pieces);
  CHECK(an.degresLiberte == 1);
  CHECK(an.libres[0] == std::array<bool, 6>{false, false, false, false, false, false});  // pièce fixe
  CHECK(an.libres[1][5]);
  // Deux instances de l'arbre, la seconde libre : 6 + 1 degrés de liberté
  a.composants.push_back({1, 80, 0, 0, 0, 0, 0, false});
  CHECK(analyserAssemblage(a, pieces).degresLiberte == 7);
  a.composants.pop_back();
  // La plaque perd son perçage : la face de la contrainte n'existe plus
  pl.p.fonction(it)->desactivee = true;
  pieces[0] = construirePiece(pl.p);
  const ComposantAssemblage avant = a.composants[1];
  const EtatAssemblage e = resoudreAssemblage(a, pieces);
  CHECK_FALSE(e.resolu);
  CHECK(e.contraintesNonSatisfaites >= 1);
  an = analyserAssemblage(a, pieces);
  CHECK(std::find(an.nonSatisfaites.begin(), an.nonSatisfaites.end(), 0) != an.nonSatisfaites.end());
  // L'arbre n'a pas été rattaché à une autre face : il n'a pas bougé
  CHECK(a.composants[1].x == Approx(avant.x).margin(1e-9));
  CHECK(a.composants[1].z == Approx(avant.z).margin(1e-9));
}

TEST_CASE("M-09 / ASM-01 : engrenages extérieur et intérieur, vis-écrou à droite et à gauche : rapport et sens", "[mecanique][ASM-01][M-09]") {
  // Plaque percée de deux alésages Ø 10 à 30 mm d'entraxe, arbre Ø 10 × 40
  Bloc pl = bloc(80, 40, 10);
  FonctionPiece pts;
  pts.type = TypeFonction::Esquisse;
  pts.face = pl.face("sommet");
  pts.esquisse.ajouterPoint(25, 20);
  pts.esquisse.ajouterPoint(55, 20);
  const int ip = pl.p.ajouter(pts);
  FonctionPiece tr;
  tr.type = TypeFonction::Percage;
  tr.esquisseId = ip;
  tr.typePercage = "simple";
  tr.diametre = 10;
  tr.traversant = true;
  const int it = pl.p.ajouter(tr);
  Piece arbre;
  FonctionPiece ec;
  ec.type = TypeFonction::Esquisse;
  const int cercle = ec.esquisse.ajouterCercle(ec.esquisse.ajouterPoint(0, 0), 5);
  const int iec = arbre.ajouter(ec);
  const int ixa = extrusion(arbre, iec, 40);
  const std::vector<ResultatPiece> pieces = {construirePiece(pl.p), construirePiece(arbre)};
  std::vector<std::pair<double, std::string>> alesages;  // (x de l'axe, nom de la face)
  for (const auto& [f, nom] : pieces[0].faces)
    if (nom.rfind(std::to_string(it) + ":trou", 0) == 0) {
      BRepAdaptor_Surface su(f);
      if (su.GetType() == GeomAbs_Cylinder) alesages.push_back({su.Cylinder().Location().X(), nom});
    }
  std::sort(alesages.begin(), alesages.end());
  REQUIRE(alesages.size() == 2);
  const std::string flanc = std::to_string(ixa) + ":cote:" + std::to_string(cercle);
  // Deux arbres sur pivots, le premier piloté à 30°, reliés par un engrenage de rapport donné
  auto engrenage = [&](double rapport) {
    Assemblage e;
    e.composants = {{0, 0, 0, 0, 0, 0, 0, true}, {1, 25, 20, -15, 0, 0, 0, false}, {1, 55, 20, -15, 0, 0, 0, false}};
    e.contraintes.push_back(creerLiaison(e, pieces, "pivot", 0, alesages[0].second, 1, flanc));
    e.contraintes.push_back(creerLiaison(e, pieces, "pivot", 0, alesages[1].second, 2, flanc));
    e.contraintes.push_back(creerLiaison(e, pieces, "engrenage", 1, flanc, 2, flanc, rapport));
    e.contraintes[0].pilotee = true;
    e.contraintes[0].valeur = 30;
    REQUIRE(resoudreAssemblage(e, pieces).resolu);
    CHECK(analyserAssemblage(e, pieces).degresLiberte == 0);
    CHECK(e.composants[2].x == Approx(55).margin(1e-4));  // chaque arbre reste dans son alésage
    return std::pair{e.composants[1].rz, e.composants[2].rz};
  };
  // Extérieur, 20 dents menantes pour 40 menées : rapport 2 (menée / menante), la menée tourne deux fois MOINS vite,
  // en sens opposé (AS-10 : la 0.25 appliquait le rapport à l'envers)
  const auto [a1, b1] = engrenage(2);
  CHECK(std::abs(a1) == Approx(30).margin(1e-3));
  CHECK(b1 == Approx(-a1 / 2).margin(1e-3));
  // Intérieur (couronne de 3 fois plus de dents), rapport −3 : même sens, trois fois moins d'angle
  const auto [a2, b2] = engrenage(-3);
  CHECK(std::abs(a2) == Approx(30).margin(1e-3));
  CHECK(b2 == Approx(a2 / 3).margin(1e-3));
  // Vis-écrou au pas de 1,5 mm : deux tours, 3 mm ; filet à gauche (pas négatif) : avance opposée
  auto vis = [&](double pas, double degres) {
    Assemblage v;
    v.composants = {{0, 0, 0, 0, 0, 0, 0, true}, {1, 25, 20, -15, 0, 0, 0, false}};
    v.contraintes.push_back(creerLiaison(v, pieces, "vis", 0, alesages[0].second, 1, flanc, pas));
    v.contraintes[0].pilotee = true;
    v.contraintes[0].valeur = degres;
    REQUIRE(resoudreAssemblage(v, pieces).resolu);
    CHECK(analyserAssemblage(v, pieces).degresLiberte == 0);
    return v.composants[1].z - (-15);
  };
  const double droite = vis(1.5, 720);
  CHECK(std::abs(droite) == Approx(3).margin(1e-3));
  CHECK(vis(-1.5, 720) == Approx(-droite).margin(1e-3));
  CHECK(vis(1.5, -360) == Approx(-droite / 2).margin(1e-3));
}

// ---------------------------------------------------------------------------------------------------------
namespace {
// Boîte des sommets : « v x y z » d'un OBJ, ou attributs x= y= z= des <vertex> d'un 3MF (lecture directe, sans
// expressions régulières, que GCC 13 accompagne de faux avertissements en mode débogage).
void etendreBoite(Boite& b, double x, double y, double z) {
  b.xmin = std::min(b.xmin, x); b.ymin = std::min(b.ymin, y); b.zmin = std::min(b.zmin, z);
  b.xmax = std::max(b.xmax, x); b.ymax = std::max(b.ymax, y); b.zmax = std::max(b.zmax, z);
}
Boite boiteSommetsObj(const std::string& texte) {
  Boite b{1e300, 1e300, 1e300, -1e300, -1e300, -1e300};
  std::istringstream lignes(texte);
  std::string l;
  while (std::getline(lignes, l)) {
    if (l.rfind("v ", 0) != 0) continue;
    std::istringstream v(l.substr(2));
    double x = 0, y = 0, z = 0;
    if (v >> x >> y >> z) etendreBoite(b, x, y, z);
  }
  return b;
}
Boite boiteSommets3mf(const std::string& texte) {
  Boite b{1e300, 1e300, 1e300, -1e300, -1e300, -1e300};
  auto attribut = [&](std::size_t debut, const char* nom) {
    const std::size_t i = texte.find(std::string(" ") + nom + "=\"", debut);
    return std::stod(texte.substr(i + 4));
  };
  for (std::size_t i = texte.find("<vertex "); i != std::string::npos; i = texte.find("<vertex ", i + 8))
    etendreBoite(b, attribut(i, "x"), attribut(i, "y"), attribut(i, "z"));
  return b;
}
void verifierBoite(const Boite& b, double L, double W, double H, double tol) {
  CHECK(b.xmin == Approx(0).margin(tol));
  CHECK(b.ymin == Approx(0).margin(tol));
  CHECK(b.zmin == Approx(0).margin(tol));
  CHECK(b.xmax == Approx(L).margin(tol));
  CHECK(b.ymax == Approx(W).margin(tol));
  CHECK(b.zmax == Approx(H).margin(tol));
}
int compter(const TopoDS_Shape& s, TopAbs_ShapeEnum t) {
  int n = 0;
  for (TopExp_Explorer ex(s, t); ex.More(); ex.Next()) ++n;
  return n;
}
}  // namespace

TEST_CASE("M-11 / IO-01 : aller-retour STEP, BREP, IGES, STL, 3MF et OBJ : dimensions, volume, unités", "[mecanique][IO-01][M-11]") {
  const std::string d = dossierEssais("echanges");
  // Pièce de référence : pavé 20 × 10 × 5 percé d'un trou Ø 5
  Bloc b = bloc(20, 10, 5);
  const int e = esquisseCercles(b.p, b.face("sommet"), {{10, 5, 2.5}});
  extrusion(b.p, e, 0, true, true, true);
  const ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  const double v = volume(r.forme);
  const int faces = compter(r.forme, TopAbs_FACE);
  const std::vector<PieceNommee> lot = {{"Bloc perce", r.forme, 0.2, 0.4, 0.9}};
  // STEP : géométrie exacte, unités en millimètres, nom du produit
  exporterStep(lot, d + "/bloc.step");
  const std::string step = lireFichier(cheminFichier(d + "/bloc.step"));
  CHECK_THAT(step, ContainsSubstring(".MILLI."));
  CHECK_THAT(step, ContainsSubstring("Bloc perce"));
  const TopoDS_Shape s = lireStep(d + "/bloc.step");
  CHECK(volume(s) == Approx(v).epsilon(1e-7));
  CHECK(nombreSolides(s) == 1);
  CHECK(compter(s, TopAbs_FACE) == faces);
  verifierBoite(encombrement(s), 20, 10, 5, 1e-6);
  CHECK(defautGeometrie(s).empty());
  // STEP écrit en pouces par un autre logiciel : relu en millimètres
  Interface_Static::SetCVal("write.step.unit", "INCH");
  STEPControl_Writer w;
  w.Transfer(r.forme, STEPControl_AsIs);
  REQUIRE(w.Write((d + "/pouces.step").c_str()) == IFSelect_RetDone);
  Interface_Static::SetCVal("write.step.unit", "MM");
  CHECK_THAT(lireFichier(cheminFichier(d + "/pouces.step")), ContainsSubstring("INCH"));
  const TopoDS_Shape sp = lireStep(d + "/pouces.step");
  CHECK(volume(sp) == Approx(v).epsilon(1e-6));
  verifierBoite(encombrement(sp), 20, 10, 5, 1e-5);
  // BREP : exact
  exporterBrep(lot, d + "/bloc.brep");
  std::string err;
  const TopoDS_Shape sb = formeDepuisBrep(lireFichierCAO(d + "/bloc.brep", &err));
  CHECK(err.empty());
  CHECK(volume(sb) == Approx(v).epsilon(1e-12));
  CHECK(compter(sb, TopAbs_FACE) == faces);
  // IGES : surfaces exactes ; boîte identique
  exporterIges(lot, d + "/bloc.igs");
  const TopoDS_Shape si = formeDepuisBrep(lireFichierCAO(d + "/bloc.igs", &err));
  REQUIRE_FALSE(si.IsNull());
  verifierBoite(encombrement(si), 20, 10, 5, 1e-4);
  // STL binaire et texte : maillage, boîte à la flèche près, volume à 1 % près
  exporterStl(r.forme, d + "/bloc.stl", 0.01);
  exporterStlTexte(r.forme, d + "/bloc_texte.stl", 0.01);
  CHECK_THAT(lireFichier(cheminFichier(d + "/bloc_texte.stl")).substr(0, 5), ContainsSubstring("solid"));
  for (const char* f : {"/bloc.stl", "/bloc_texte.stl"}) {
    const TopoDS_Shape m = formeDepuisBrep(lireFichierCAO(d + f, &err));
    REQUIRE_FALSE(m.IsNull());
    verifierBoite(encombrement(m), 20, 10, 5, 0.02);
    CHECK(compter(m, TopAbs_FACE) > 12);
  }
  // 3MF : unité millimètre, sommets dans la même boîte
  exporter3mf(lot, d + "/bloc.3mf");
  const std::string zip3mf = lireFichier(cheminFichier(d + "/bloc.3mf"));
  CHECK_THAT(zip3mf, ContainsSubstring("unit=\"millimeter\""));
  verifierBoite(boiteSommets3mf(zip3mf), 20, 10, 5, 0.02);
  // OBJ : sommets et matière
  exporterObj(lot, d + "/bloc.obj");
  const std::string obj = lireFichier(cheminFichier(d + "/bloc.obj"));
  verifierBoite(boiteSommetsObj(obj), 20, 10, 5, 0.02);
  CHECK(fs::exists(d + "/bloc.mtl"));
  // Fichiers vides ou tronqués : erreur claire, aucun plantage
  { std::ofstream(d + "/vide.step") << ""; }
  { std::ofstream(d + "/coupe.step") << step.substr(0, step.size() / 3); }
  for (const char* f : {"/vide.step", "/coupe.step", "/absent.step"}) {
    err.clear();
    const std::string brep = lireFichierCAO(d + f, &err);
    CHECK((brep.empty() || formeDepuisBrep(brep).IsNull() || nombreSolides(formeDepuisBrep(brep)) == 0));
    if (brep.empty()) CHECK_FALSE(err.empty());
  }
  fs::remove_all(d);
}

TEST_CASE("AS-16 un sous-assemblage garde ses contraintes avec le reste et se contraint comme un corps rigide", "[mecanique][ASM-01][AS-16]") {
  Bloc pl = bloc(60, 40, 10);
  FonctionPiece pts;
  pts.type = TypeFonction::Esquisse;
  pts.face = pl.face("sommet");
  pts.esquisse.ajouterPoint(30, 20);
  const int ip = pl.p.ajouter(pts);
  FonctionPiece tr;
  tr.type = TypeFonction::Percage;
  tr.esquisseId = ip;
  tr.typePercage = "simple";
  tr.diametre = 10;
  tr.traversant = true;
  const int it = pl.p.ajouter(tr);
  Piece arbre;
  FonctionPiece ec;
  ec.type = TypeFonction::Esquisse;
  const int cercle = ec.esquisse.ajouterCercle(ec.esquisse.ajouterPoint(0, 0), 5);
  const int iec = arbre.ajouter(ec);
  const int ixa = extrusion(arbre, iec, 40);
  std::vector<ResultatPiece> pieces = {construirePiece(pl.p), construirePiece(arbre)};
  std::string alesage;
  for (const auto& [f, nom] : pieces[0].faces)
    if (nom.rfind(std::to_string(it) + ":trou", 0) == 0 && BRepAdaptor_Surface(f).GetType() == GeomAbs_Cylinder) alesage = nom;
  REQUIRE_FALSE(alesage.empty());
  const std::string flanc = std::to_string(ixa) + ":cote:" + std::to_string(cercle);
  Projet p = projetVide();
  Assemblage a;
  a.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  a.composants.push_back({1, 30, 20, -15, 0, 0, 0, false});
  a.composants.push_back({1, 80, 0, 0, 0, 0, 0, false});
  a.contraintes.push_back(creerLiaison(a, pieces, "pivot", 0, alesage, 1, flanc));
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  p.assemblages = {a};
  // Les deux arbres deviennent un sous-assemblage : la liaison est gardée, par le chemin de la face
  const int rang = creerSousAssemblage(p, 0, {1, 2});
  REQUIRE(rang == 1);
  Assemblage& parent = p.assemblages[0];
  REQUIRE(parent.contraintes.size() == 1);
  ContrainteAssemblage& c = parent.contraintes[0];
  CHECK(c.b == 1);
  CHECK(c.refB == "@0/" + flanc);
  CHECK(genreFaceComposant(parent.composants[1], c.refB, pieces, &p.assemblages) == "cylindre");
  // Liaison sans état de départ : recalculée sur le sous-assemblage
  CHECK_FALSE(resoudreAssemblage(parent, pieces, &p.assemblages).problemes.empty());
  c = creerLiaison(parent, pieces, c.type, c.a, c.refA, c.b, c.refB, 0, &p.assemblages);
  // Le sous-assemblage écarté de 12 mm revient dans l'alésage
  parent.composants[1].x += 12;
  parent.composants[1].y -= 5;
  const EtatAssemblage e = resoudreAssemblage(parent, pieces, &p.assemblages);
  REQUIRE(e.resolu);
  const gp_Trsf T = placementComposant(parent.composants[1]) * placementComposant(p.assemblages[1].composants[0]);
  const gp_Pnt axe = gp_Pnt(0, 0, 0).Transformed(T);
  CHECK(axe.X() == Approx(30).margin(1e-6));
  CHECK(axe.Y() == Approx(20).margin(1e-6));
  const AnalyseAssemblage an = analyserAssemblage(parent, pieces, &p.assemblages);
  CHECK(an.degresLiberte == 1);  // le pivot : seule la rotation autour de l'alésage reste
}

TEST_CASE("A1 : sens des contraintes parallèle et angle orienté (inversion, relecture)", "[mecanique][ASM-01][A1]") {
  Bloc b = bloc(20, 10, 5);
  std::vector<ResultatPiece> pieces = {construirePiece(b.p)};
  const std::string sommet = b.face("sommet");
  // Normale de la face « sommet » (+Z de la pièce) une fois le composant placé
  auto normale = [](const ComposantAssemblage& c) { return gp_Vec(0, 0, 1).Transformed(placementComposant(c)); };
  Assemblage a;
  a.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  a.composants.push_back({0, 40, 0, 0, 30, 10, 0, false});
  ContrainteAssemblage par;
  par.type = "parallele";
  par.a = 0;
  par.b = 1;
  par.refA = sommet;
  par.refB = sommet;
  // Même sens imposé, puis opposé : la pièce se retourne au lieu de prendre la solution la plus proche
  par.alignement = 1;
  a.contraintes.push_back(par);
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  CHECK(normale(a.composants[1]).Z() == Approx(1).margin(1e-6));
  a.contraintes[0].alignement = 2;
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  CHECK(normale(a.composants[1]).Z() == Approx(-1).margin(1e-6));
  // Relecture du projet : le sens est gardé
  Projet p = projetVide();
  p.pieces.push_back(pieceVersJson(b.p));
  p.assemblages.push_back(a);
  const Projet relu = normaliser(versJson(p));
  REQUIRE_FALSE(relu.assemblages.empty());
  REQUIRE(relu.assemblages.back().contraintes.size() == 1);
  CHECK(relu.assemblages.back().contraintes[0].alignement == 2);

  // Angle orienté : 20° autour de X au départ, l'axe est pris tel quel, puis 35° dans un sens et dans l'autre
  a.contraintes.clear();
  a.composants[1] = {0, 40, 0, 0, 20, 0, 0, false};
  ContrainteAssemblage ang = par;
  ang.type = "angle";
  ang.alignement = 0;
  ang.valeur = 50;
  a.contraintes.push_back(ang);
  REQUIRE(orienterAngle(a, 0, pieces));
  CHECK(a.contraintes[0].alignement == 1);
  CHECK(a.contraintes[0].valeur == Approx(20).margin(1e-6));  // l'état actuel, rien ne bouge
  REQUIRE(a.contraintes[0].parametres.size() == 3);
  const gp_Vec axe(a.contraintes[0].parametres[0], a.contraintes[0].parametres[1], a.contraintes[0].parametres[2]);
  CHECK(std::abs(axe.X()) == Approx(1).margin(1e-6));
  auto angleSigne = [&] {
    const gp_Vec n = normale(a.composants[1]);
    return std::atan2(gp_Vec(0, 0, 1).Crossed(n).Dot(axe), n.Z()) * 180 / PI;
  };
  CHECK(angleSigne() == Approx(20).margin(1e-6));
  a.contraintes[0].valeur = 35;
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  CHECK(angleSigne() == Approx(35).margin(1e-5));
  a.contraintes[0].alignement = 2;
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  CHECK(angleSigne() == Approx(-35).margin(1e-5));
  // Faces parallèles : pas d'axe, l'angle ne peut pas être orienté
  a.composants[1] = {0, 40, 0, 0, 0, 0, 0, false};
  a.contraintes[0].alignement = 0;
  CHECK_FALSE(orienterAngle(a, 0, pieces));
}

TEST_CASE("A2 : contraintes sur arêtes, sommets, axes, repère de la pièce et plans de l'assemblage", "[mecanique][ASM-01][A2]") {
  Bloc b = bloc(20, 10, 5);
  std::vector<ResultatPiece> pieces = {construirePiece(b.p)};
  const ResultatPiece& r = pieces[0];
  // Sommet à l'origine de la pièce et arête verticale qui y passe (entre « gauche » et « bas »)
  std::string sommet;
  for (TopExp_Explorer ex(r.forme, TopAbs_VERTEX); ex.More(); ex.Next())
    if (BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())).Distance(gp::Origin()) < 1e-9) sommet = r.nomSommet(TopoDS::Vertex(ex.Current()));
  REQUIRE_FALSE(sommet.empty());
  const std::string arete = b.areteVerticale(3);
  CHECK(genreFace(r, arete) == "droite");
  CHECK(genreFace(r, sommet) == "point");
  CHECK(genreFace(r, "base:XY") == "plan");
  CHECK(genreFace(r, "base:Z") == "droite");
  CHECK(genreFace(r, "base:O") == "point");
  CHECK(genreFace(r, "plan:999").empty());
  // Admissions : point sur plan, droite dans un plan, droites confondues ; pas de point dans un parallélisme
  CHECK(facesAdmises("coincidence", "point", "plan").empty());
  CHECK(facesAdmises("coincidence", "droite", "plan").empty());
  CHECK(facesAdmises("coincidence", "droite", "cylindre").empty());
  CHECK(facesAdmises("concentrique", "cercle", "cylindre").empty());
  CHECK_FALSE(facesAdmises("parallele", "point", "plan").empty());
  CHECK_FALSE(facesAdmises("coincidence", "cylindre", "cylindre").empty());
  CHECK_FALSE(facesAdmises("pivot", "droite", "cylindre").empty());

  // 1. Repère de la pièce sur les plans de l'assemblage (alignés), décalé de 15 mm en X : la pièce y est posée
  Assemblage a;
  a.composants.push_back({0, 0, 0, 0, 0, 0, 0, true});
  a.composants.push_back({0, 33, -12, 7, 20, -35, 50, false});
  auto aligner = [&](const std::string& plan, double decalage) {
    ContrainteAssemblage c;
    c.type = "coincidence";
    c.inverse = true;
    c.a = c.b = 1;  // le côté « assemblage » porte le composant de l'autre côté
    c.refA = std::string(PREFIXE_REFERENCE_ASSEMBLAGE) + plan;
    c.refB = "base:" + plan;
    c.valeur = decalage;
    a.contraintes.push_back(c);
  };
  aligner("XY", 0);
  aligner("XZ", 0);
  aligner("YZ", 15);
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  const gp_Trsf T1 = placementComposant(a.composants[1]);
  CHECK(gp::Origin().Transformed(T1).Distance(gp_Pnt(15, 0, 0)) < 1e-6);
  CHECK(gp_Vec(1, 0, 0).Transformed(T1).X() == Approx(1).margin(1e-6));
  CHECK(gp_Vec(0, 0, 1).Transformed(T1).Z() == Approx(1).margin(1e-6));
  CHECK(analyserAssemblage(a, pieces).libertes[1] == 0);

  // 2. Sommet sur sommet puis arête sur arête : il ne reste que la rotation autour de l'arête commune
  a.composants.push_back({0, -40, 25, 10, 15, 5, -20, false});
  ContrainteAssemblage pp;
  pp.type = "coincidence";
  pp.a = 1;
  pp.b = 2;
  pp.refA = sommet;
  pp.refB = sommet;
  a.contraintes.push_back(pp);
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  CHECK(gp::Origin().Transformed(placementComposant(a.composants[2])).Distance(gp_Pnt(15, 0, 0)) < 1e-6);
  CHECK(analyserAssemblage(a, pieces).libertes[2] == 3);
  ContrainteAssemblage dd = pp;
  dd.type = "concentrique";
  dd.refA = arete;
  dd.refB = arete;
  a.contraintes.push_back(dd);
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  CHECK(std::abs(gp_Vec(0, 0, 1).Transformed(placementComposant(a.composants[2])).Z()) == Approx(1).margin(1e-6));
  CHECK(analyserAssemblage(a, pieces).libertes[2] == 1);
  // Origine de l'assemblage à 30 mm sous la face « sommet » d'un composant gardé droit (Z parallèle, même sens)
  a.composants.push_back({0, 60, 60, -8, 10, 25, 0, false});
  ContrainteAssemblage droit;
  droit.type = "parallele";
  droit.alignement = 1;
  droit.a = droit.b = 3;
  droit.refA = std::string(PREFIXE_REFERENCE_ASSEMBLAGE) + "Z";
  droit.refB = "base:Z";
  a.contraintes.push_back(droit);
  ContrainteAssemblage op;
  op.type = "coincidence";
  op.a = op.b = 3;
  op.refA = std::string(PREFIXE_REFERENCE_ASSEMBLAGE) + "O";
  op.refB = b.face("sommet");
  op.valeur = -30;  // côté opposé à la normale sortante de la face
  a.contraintes.push_back(op);
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  CHECK(gp_Pnt(3, 4, 5).Transformed(placementComposant(a.composants[3])).Z() == Approx(30).margin(1e-6));
  CHECK(analyserAssemblage(a, pieces).libertes[3] == 3);  // glisse dans son plan et tourne autour de Z

  // 3. Une liaison ne vise pas l'assemblage ; deux références d'assemblage : contrainte écartée
  CHECK_THROWS_AS(creerLiaison(a, pieces, "pivot", 1, std::string(PREFIXE_REFERENCE_ASSEMBLAGE) + "Z", 2, arete), std::invalid_argument);
  Assemblage vide = a;
  ContrainteAssemblage deux;
  deux.type = "parallele";
  deux.a = deux.b = 1;
  deux.refA = std::string(PREFIXE_REFERENCE_ASSEMBLAGE) + "XY";
  deux.refB = std::string(PREFIXE_REFERENCE_ASSEMBLAGE) + "XZ";
  vide.contraintes.push_back(deux);
  const EtatAssemblage ev = resoudreAssemblage(vide, pieces);
  CHECK(std::any_of(ev.problemes.begin(), ev.problemes.end(), [&](const auto& x) { return x.first == static_cast<int>(vide.contraintes.size()) - 1; }));

  // 4. Relecture : la contrainte vers l'assemblage (même composant des deux côtés) est gardée
  Projet p = projetVide();
  p.pieces.push_back(pieceVersJson(b.p));
  p.assemblages.push_back(a);
  const Projet relu = normaliser(versJson(p));
  REQUIRE(relu.assemblages.back().contraintes.size() == a.contraintes.size());
  CHECK(relu.assemblages.back().contraintes[0].refA == "assemblage:XY");

  // 5. Le composant 1 part dans un sous-assemblage : ses contraintes vers l'assemblage le suivent par leur chemin
  Projet q = p;
  const int sa = creerSousAssemblage(q, static_cast<int>(q.assemblages.size()) - 1, {1, 2});
  REQUIRE(sa >= 0);
  const Assemblage& pere = q.assemblages[q.assemblages.size() - 2];
  int ks = -1;
  for (int k = 0; k < static_cast<int>(pere.composants.size()); ++k)
    if (pere.composants[static_cast<size_t>(k)].assemblage == sa) ks = k;
  REQUIRE(ks >= 0);
  int versAss = 0;
  for (const auto& c : pere.contraintes)
    if (contrainteVersAssemblage(c) && c.a == ks) {
      ++versAss;
      CHECK(c.b == ks);
      CHECK(c.refB.rfind("@0/base:", 0) == 0);  // le composant 1 est le premier du sous-assemblage
    }
  CHECK(versAss == 3);
  Assemblage pereRes = pere;
  pereRes.composants[static_cast<size_t>(ks)].x += 7;  // déplacé : les contraintes le ramènent
  REQUIRE(resoudreAssemblage(pereRes, pieces, &q.assemblages).resolu);
  CHECK(pereRes.composants[static_cast<size_t>(ks)].x == Approx(pere.composants[static_cast<size_t>(ks)].x).margin(1e-6));
}

TEST_CASE("A3 : une pièce modifiée remet à jour les assemblages de bas en haut, sous-assemblages d'abord", "[mecanique][ASM-01][A3][AS-12]") {
  Bloc plaque = bloc(60, 40, 8);
  Bloc cube = bloc(10, 10, 10);
  std::vector<ResultatPiece> pieces = {construirePiece(plaque.p), construirePiece(cube.p)};
  // Sous-assemblage : la plaque fixe, un cube posé dessus
  Assemblage sous;
  sous.nom = "Socle";
  sous.composants = {{0, 0, 0, 0, 0, 0, 0, true}, {1, 5, 5, 8, 0, 0, 0, false}};
  ContrainteAssemblage pose;
  pose.type = "coincidence";
  pose.a = 0;
  pose.b = 1;
  pose.refA = plaque.face("sommet");
  pose.refB = cube.face("base");
  sous.contraintes.push_back(pose);
  // Parent : le socle fixe, un second cube posé sur le cube du socle (face visée par son chemin « @1/… »)
  Assemblage pere;
  pere.nom = "Ensemble";
  ComposantAssemblage s;
  s.assemblage = 0;
  s.fixe = true;
  pere.composants = {s, {1, 20, 0, 18, 0, 0, 0, false}};
  ContrainteAssemblage dessus;
  dessus.type = "coincidence";
  dessus.a = 0;
  dessus.b = 1;
  dessus.refA = "@1/" + cube.face("sommet");
  dessus.refB = cube.face("base");
  pere.contraintes.push_back(dessus);
  std::vector<Assemblage> tous = {sous, pere};
  CHECK(ordreAssemblages(tous) == std::vector<int>{0, 1});
  CHECK(ordreAssemblages(tous, 1) == std::vector<int>{0, 1});
  CHECK(ordreAssemblages(tous, 0) == std::vector<int>{0});
  auto maj = mettreAJourAssemblages(tous, pieces);
  REQUIRE(maj.size() == 2);
  CHECK(maj[0].etat.resolu);
  CHECK(maj[1].etat.resolu);
  CHECK(tous[1].composants[1].z == Approx(18).margin(1e-6));
  // La plaque passe de 8 à 20 mm : le cube du socle monte de 12 mm, puis le cube du parent avec lui
  for (auto& f : plaque.p.fonctions) if (f.id == plaque.extrusion) f.longueur = 20;
  pieces[0] = construirePiece(plaque.p);
  maj = mettreAJourAssemblages(tous, pieces);
  REQUIRE(maj.size() == 2);
  CHECK(maj[0].assemblage == 0);
  CHECK(maj[0].deplace);
  CHECK(maj[1].deplace);
  CHECK(tous[0].composants[1].z == Approx(20).margin(1e-6));
  CHECK(tous[1].composants[1].z == Approx(30).margin(1e-6));
  // Rien n'a changé : rien ne bouge
  maj = mettreAJourAssemblages(tous, pieces);
  CHECK_FALSE(maj[0].deplace);
  CHECK_FALSE(maj[1].deplace);
  // Une boucle d'imbrication (fichier abîmé) est parcourue sans tourner en rond
  std::vector<Assemblage> boucle = tous;
  ComposantAssemblage retour;
  retour.assemblage = 1;
  boucle[0].composants.push_back(retour);
  CHECK(ordreAssemblages(boucle).size() == 2);
}

TEST_CASE("A4 : déplacer et pivoter un composant dans le respect de ses contraintes, les composants liés suivent", "[mecanique][ASM-01][A4]") {
  Bloc plaque = bloc(80, 60, 8);
  Bloc cube = bloc(10, 10, 10);
  std::vector<ResultatPiece> pieces = {construirePiece(plaque.p), construirePiece(cube.p)};
  Assemblage a;
  a.composants = {{0, 0, 0, 0, 0, 0, 0, true}, {1, 20, 20, 8, 0, 0, 0, false}, {1, 20, 20, 18, 0, 0, 0, false}, {1, 60, 40, 30, 10, 20, 0, false}};
  auto lier = [&](int ca, const std::string& ra, int cb, const std::string& rb, bool aligne) {
    ContrainteAssemblage k;
    k.a = ca;
    k.b = cb;
    k.refA = ra;
    k.refB = rb;
    k.inverse = aligne;
    a.contraintes.push_back(k);
  };
  lier(0, plaque.face("sommet"), 1, cube.face("base"), false);
  lier(1, cube.face("sommet"), 2, cube.face("base"), false);
  lier(1, cube.cote(0), 2, cube.cote(0), true);
  lier(1, cube.cote(3), 2, cube.cote(3), true);
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  // Le premier cube glissé vers (30 ; 25 ; 15) : il reste sur la plaque, le second le suit
  ComposantAssemblage cible = a.composants[1];
  cible.x += 10;
  cible.y += 5;
  cible.z += 7;
  auto r = deplacerDansContraintes(a, 1, cible, pieces);
  REQUIRE(r);
  CHECK((*r)[1].x == Approx(30).margin(1e-6));
  CHECK((*r)[1].y == Approx(25).margin(1e-6));
  CHECK((*r)[1].z == Approx(8).margin(1e-6));
  CHECK((*r)[2].x == Approx(30).margin(1e-6));
  CHECK((*r)[2].y == Approx(25).margin(1e-6));
  CHECK((*r)[2].z == Approx(18).margin(1e-6));
  CHECK((*r)[3].x == Approx(60).margin(1e-9));  // non lié : ne bouge pas
  // Le second cube tiré : il entraîne le premier (le mécanisme suit), qui reste sur la plaque
  cible = a.composants[2];
  cible.x -= 4;
  r = deplacerDansContraintes(a, 2, cible, pieces);
  REQUIRE(r);
  CHECK((*r)[2].x == Approx(16).margin(1e-6));
  CHECK((*r)[1].x == Approx(16).margin(1e-6));
  CHECK((*r)[1].z == Approx(8).margin(1e-6));
  // Pivoter le premier autour de X : la plaque l'interdit, il reste à plat ; autour de Z (par son centre) : permis
  cible = a.composants[1];
  pivoterAutour(cible, gp_Ax1(gp_Pnt(25, 25, 13), gp::DX()), 30);
  r = deplacerDansContraintes(a, 1, cible, pieces);
  REQUIRE(r);
  ComposantAssemblage pose = (*r)[1];
  CHECK(gp_Vec(0, 0, 1).Transformed(placementComposant(pose)).Z() == Approx(1).margin(1e-6));
  CHECK(pose.z == Approx(8).margin(1e-6));
  cible = a.composants[1];
  pivoterAutour(cible, gp_Ax1(gp_Pnt(25, 25, 13), gp::DZ()), 90);
  r = deplacerDansContraintes(a, 1, cible, pieces);
  REQUIRE(r);
  pose = (*r)[1];
  CHECK(gp_Vec(1, 0, 0).Transformed(placementComposant(pose)).Y() == Approx(1).margin(1e-6));
  CHECK(gp_Pnt(5, 5, 5).Transformed(placementComposant(pose)).Distance(gp_Pnt(25, 25, 13)) < 1e-6);
  CHECK(gp_Vec(1, 0, 0).Transformed(placementComposant((*r)[2])).Y() == Approx(1).margin(1e-6));  // le second tourne avec
  // Sans contrainte : la cible telle quelle
  cible = a.composants[3];
  cible.x = 0;
  r = deplacerDansContraintes(a, 3, cible, pieces);
  REQUIRE(r);
  CHECK((*r)[3].x == Approx(0).margin(1e-12));
  // pivoterAutour : rotation autour d'une droite quelconque, centre gardé
  ComposantAssemblage t{1, 3, 4, 5, 10, 20, 30, false};
  const gp_Pnt c0 = gp_Pnt(1, 2, 3).Transformed(placementComposant(t));
  pivoterAutour(t, gp_Ax1(c0, gp_Dir(1, 1, 1)), 77);
  CHECK(gp_Pnt(1, 2, 3).Transformed(placementComposant(t)).Distance(c0) < 1e-9);
}
