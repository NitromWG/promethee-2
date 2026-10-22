// Prométhée : fonctions mécaniques ajoutées après l'audit 0.25 (M1 import DXF dans une esquisse…).
// Licence GPL-3.0-only.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>

#include "promethee/dxf_dessin.hpp"
#include "promethee/esquisse.hpp"

using Catch::Approx;
using namespace prom;

namespace {
// DXF en texte : une paire (code, valeur) par élément
std::string texteDxf(std::initializer_list<std::pair<int, std::string>> paires) {
  std::string t;
  for (const auto& [c, v] : paires) t += std::to_string(c) + "\n" + v + "\n";
  return t;
}
std::string entete(int unites) {
  return texteDxf({{0, "SECTION"}, {2, "HEADER"}, {9, "$ACADVER"}, {1, "AC1015"}, {9, "$INSUNITS"}, {70, std::to_string(unites)}, {0, "ENDSEC"},
              {0, "SECTION"}, {2, "TABLES"}, {0, "TABLE"}, {2, "LAYER"}, {0, "LAYER"}, {2, "CONTOUR"}, {62, "1"}, {70, "0"},
              {0, "LAYER"}, {2, "AXES"}, {62, "5"}, {70, "0"}, {0, "LAYER"}, {2, "GELE"}, {62, "3"}, {70, "1"}, {0, "ENDTAB"}, {0, "ENDSEC"}});
}
}  // namespace

TEST_CASE("M1 DXF dans une esquisse : contour, cercle, unités, calques, relations", "[esquisse][dxf][M1]") {
  const std::string t = entete(4) + texteDxf({{0, "SECTION"}, {2, "ENTITIES"},
      {0, "LWPOLYLINE"}, {8, "CONTOUR"}, {90, "4"}, {70, "1"}, {10, "0"}, {20, "0"}, {10, "40"}, {20, "0"}, {10, "40"}, {20, "20"}, {10, "0"}, {20, "20"},
      {0, "CIRCLE"}, {8, "CONTOUR"}, {10, "20"}, {20, "10"}, {40, "5"},
      {0, "LINE"}, {8, "AXES"}, {10, "-5"}, {20, "10"}, {11, "45"}, {21, "10"},
      {0, "LINE"}, {8, "GELE"}, {10, "0"}, {20, "50"}, {11, "10"}, {21, "50"},
      {0, "TEXT"}, {8, "CONTOUR"}, {10, "0"}, {20, "0"}, {1, "Cartouche"},
      {0, "ENDSEC"}, {0, "EOF"}});
  const auto d = dxf::lireDessinDxf(t);
  CHECK(d.uniteDeclaree);
  CHECK(d.unite == "mm");
  CHECK(d.ignorees.at("TEXT") == 1);
  REQUIRE(d.calque("GELE"));
  CHECK_FALSE(d.calque("GELE")->visible);
  CHECK(d.traits.size() == 7);
  Esquisse e;
  const auto r = dxf::importerDxf(e, d);
  CHECK(r.lignes == 5);  // quatre côtés et l'axe ; le calque gelé est laissé
  CHECK(r.cercles == 1);
  CHECK(r.reunis >= 4);  // coins partagés, et le coin (0, 0) rejoint l'origine
  CHECK(r.relations == 5);
  CHECK(r.xmin == Approx(-5));
  CHECK(r.xmax == Approx(45));
  // Calques de l'esquisse, à la couleur du DXF
  bool rouge = false;
  for (const auto& k : e.calques) rouge |= k.nom == "CONTOUR" && k.couleur == "#FF0000";
  CHECK(rouge);
  // Le contour est fermé : une boucle de 40 × 20, percée du cercle
  const auto b = boucles(e);
  REQUIRE(b.size() >= 2);
  double aireMax = 0;
  for (const auto& x : b) aireMax = std::max(aireMax, std::abs(x.aire));
  CHECK(aireMax == Approx(800).margin(0.01));
  const auto etat = resoudre(e);
  CHECK(etat.resolue);
  CHECK(etat.enConflit.empty());
  // Placement au centre, axes seuls (calque choisi), en construction
  Esquisse e2;
  dxf::OptionsImportDxf o;
  o.placement = dxf::PlacementDxf::Centre;
  o.calques = {"CONTOUR", "GELE"};
  o.construction = true;
  const auto r2 = dxf::importerDxf(e2, d, o);
  CHECK(r2.lignes == 5);  // quatre côtés et la ligne du calque gelé, demandé explicitement
  CHECK(r2.xmin == Approx(-r2.xmax));
  for (const auto& el : e2.elements) CHECK(el.construction);
}

TEST_CASE("M1 DXF : pouces, arcs de polyligne, arc retourné, blocs insérés", "[esquisse][dxf][M1]") {
  const std::string t = entete(1) + texteDxf({{0, "SECTION"}, {2, "BLOCKS"},
      {0, "BLOCK"}, {2, "PATTE"}, {10, "0"}, {20, "0"}, {0, "LINE"}, {8, "0"}, {10, "0"}, {20, "0"}, {11, "1"}, {21, "0"}, {0, "ENDBLK"},
      {0, "ENDSEC"}, {0, "SECTION"}, {2, "ENTITIES"},
      // Un pouce, puis un demi-cercle par renflement 1 (180°, sens direct) de (2, 0) à (4, 0)
      {0, "LINE"}, {8, "CONTOUR"}, {10, "0"}, {20, "0"}, {11, "1"}, {21, "0"},
      {0, "LWPOLYLINE"}, {8, "CONTOUR"}, {90, "2"}, {70, "0"}, {10, "2"}, {20, "0"}, {42, "1"}, {10, "4"}, {20, "0"},
      // Arc dans un repère retourné (extrusion 0 0 -1) : centre (10, 0) du repère objet → (-10, 0)
      {0, "ARC"}, {8, "CONTOUR"}, {10, "10"}, {20, "0"}, {40, "5"}, {50, "0"}, {51, "90"}, {210, "0"}, {220, "0"}, {230, "-1"},
      // Bloc tourné de 90° et doublé, puis le même retourné (échelle X -1)
      {0, "INSERT"}, {8, "AXES"}, {2, "PATTE"}, {10, "10"}, {20, "0"}, {41, "2"}, {42, "2"}, {50, "90"},
      {0, "INSERT"}, {8, "AXES"}, {2, "PATTE"}, {10, "20"}, {20, "0"}, {41, "-1"}, {42, "1"},
      {0, "ENDSEC"}, {0, "EOF"}});
  const auto d = dxf::lireDessinDxf(t);
  CHECK(d.unite == "pouce");
  CHECK(d.facteurMm == Approx(25.4));
  REQUIRE(d.traits.size() == 5);
  const auto& l = d.traits[0];
  CHECK(l.b.x - l.a.x == Approx(25.4));
  const auto& a = d.traits[1];
  REQUIRE(a.genre == dxf::TraitDxf::Genre::Arc);
  CHECK(a.centre.x == Approx(3 * 25.4));
  CHECK(a.rayon == Approx(25.4));
  CHECK(a.a.x == Approx(2 * 25.4));  // départ en (2, 0), sens direct : par le bas
  const auto& ar = d.traits[2];
  CHECK(ar.centre.x == Approx(-10 * 25.4));
  CHECK(ar.a.x == Approx(-10 * 25.4));  // de (−10, 5) à (−15, 0) dans le sens direct
  CHECK(ar.a.y == Approx(5 * 25.4));
  CHECK(ar.b.x == Approx(-15 * 25.4));
  const auto& b1 = d.traits[3];
  CHECK(b1.calque == "AXES");  // le calque 0 du bloc prend celui de l'insertion
  CHECK(b1.a.x == Approx(10 * 25.4));
  CHECK(b1.b.x == Approx(10 * 25.4).margin(1e-9));
  CHECK(b1.b.y == Approx(2 * 25.4));
  const auto& b2 = d.traits[4];
  CHECK(b2.b.x == Approx(19 * 25.4));
  // Unité forcée : le fichier dit pouce, l'utilisateur sait qu'il est en mm
  Esquisse e;
  dxf::OptionsImportDxf o;
  o.facteurMm = 1;
  const auto r = dxf::importerDxf(e, d, o);
  CHECK(r.arcs == 2);
  CHECK(r.lignes == 3);
  CHECK(r.xmin == Approx(-15));
}

TEST_CASE("M1 DXF : ellipse et spline suivies par des arcs, géométrie fixée entièrement contrainte", "[esquisse][dxf][M1]") {
  const std::string t = entete(4) + texteDxf({{0, "SECTION"}, {2, "ENTITIES"},
      {0, "ELLIPSE"}, {8, "CONTOUR"}, {10, "0"}, {20, "0"}, {11, "20"}, {21, "0"}, {40, "0.5"}, {41, "0"}, {42, "6.283185307179586"},
      // Spline cubique de Bézier (vecteur nodal bloqué)
      {0, "SPLINE"}, {8, "CONTOUR"}, {70, "8"}, {71, "3"}, {72, "8"}, {73, "4"},
      {40, "0"}, {40, "0"}, {40, "0"}, {40, "0"}, {40, "1"}, {40, "1"}, {40, "1"}, {40, "1"},
      {10, "30"}, {20, "0"}, {10, "40"}, {20, "20"}, {10, "60"}, {20, "-20"}, {10, "70"}, {20, "0"},
      {0, "ENDSEC"}, {0, "EOF"}});
  const auto d = dxf::lireDessinDxf(t);
  CHECK(d.courbesApprochees == 2);
  // Chaque trait reste sur l'ellipse x²/400 + y²/100 = 1
  int surEllipse = 0;
  for (const auto& tr : d.traits) {
    CHECK(tr.approche);
    if (tr.genre != dxf::TraitDxf::Genre::Arc || tr.centre.x > 25 || std::abs(tr.a.x) > 21) continue;
    ++surEllipse;
    for (const auto& p : {tr.a, tr.b}) CHECK(std::abs(p.x * p.x / 400 + p.y * p.y / 100 - 1) < 1e-3);
    // Milieu de l'arc : à moins de 0,005 mm de l'ellipse
    const double a0 = std::atan2(tr.a.y - tr.centre.y, tr.a.x - tr.centre.x);
    double a1 = std::atan2(tr.b.y - tr.centre.y, tr.b.x - tr.centre.x);
    while (a1 <= a0) a1 += 2 * 3.141592653589793;
    const double am = (a0 + a1) / 2, x = tr.centre.x + tr.rayon * std::cos(am), y = tr.centre.y + tr.rayon * std::sin(am);
    // Distance approchée à l'ellipse par le gradient
    const double f = x * x / 400 + y * y / 100 - 1, g = std::hypot(2 * x / 400, 2 * y / 100);
    CHECK(std::abs(f / g) < 0.006);
  }
  CHECK(surEllipse >= 4);
  CHECK(surEllipse <= 64);
  Esquisse e;
  dxf::OptionsImportDxf o;
  o.fixer = true;
  const auto r = dxf::importerDxf(e, d, o);
  CHECK(r.approches == static_cast<int>(d.traits.size()));
  // L'ellipse est fermée : une boucle d'aire π·a·b
  const auto b = boucles(e);
  REQUIRE(b.size() == 1);
  CHECK(std::abs(b.front().aire) == Approx(3.141592653589793 * 20 * 10).epsilon(0.002));
  // Bout de la spline : (30, 0) et (70, 0)
  bool debut = false, fin = false;
  for (const auto& p : e.points) {
    debut |= std::hypot(p.x - 30, p.y) < 1e-6;
    fin |= std::hypot(p.x - 70, p.y) < 1e-6;
  }
  CHECK(debut);
  CHECK(fin);
  CHECK(diagnostiquer(e).degresLiberte == 0);
  CHECK(dxf::couleurAci(1) == "#FF0000");
  CHECK(dxf::couleurAci(10) == "#FF0000");
  CHECK(dxf::couleurAci(250).size() == 7);
}

TEST_CASE("M1 DXF : fichiers abîmés, binaires ou vides refusés avec une raison", "[esquisse][dxf][M1]") {
  CHECK_FALSE(dxf::lireDessinDxf("").alertes.empty());
  CHECK_FALSE(dxf::lireDessinDxf("AutoCAD Binary DXF\r\n\x1a").alertes.empty());
  const auto d = dxf::lireDessinDxf(texteDxf({{0, "SECTION"}, {2, "ENTITIES"}, {0, "INSERT"}, {2, "ABSENT"}, {10, "0"}, {20, "0"}, {0, "ENDSEC"}, {0, "EOF"}}));
  CHECK(d.traits.empty());
  CHECK(d.alertes.size() >= 2);  // bloc absent, rien à reprendre
  Esquisse e;
  const auto r = dxf::importerDxf(e, d);
  CHECK(r.total() == 0);
  CHECK(e.elements.empty());
}

// ---------------------------------------------------------------------------------------------------------------
// M5 tôlerie
// ---------------------------------------------------------------------------------------------------------------
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>

#include "promethee/construction.hpp"
#include "promethee/tolerie.hpp"

namespace {
double volumeDe(const TopoDS_Shape& s) {
  GProp_GProps g;
  BRepGProp::VolumeProperties(s, g);
  return g.Mass();
}
struct Tole {
  Piece p;
  int tole = 0;
  std::map<char, int> cotes;  // 'b' bas (y = 0), 'd' droite, 'h' haut, 'g' gauche
  Tole(double L = 100, double H = 60, double e = 2, double r = 2) {
    FonctionPiece esq;
    esq.type = TypeFonction::Esquisse;
    const auto l = esq.esquisse.ajouterRectangle(0, 0, L, H);
    for (int id : l) {
      const auto* el = esq.esquisse.element(id);
      const auto *a = esq.esquisse.point(el->p1), *b = esq.esquisse.point(el->p2);
      const char c = std::abs(a->y - b->y) < 1e-9 ? (a->y < 1e-9 ? 'b' : 'h') : (a->x < 1e-9 ? 'g' : 'd');
      cotes[c] = id;
    }
    const int idEsq = p.ajouter(esq);
    FonctionPiece t;
    t.type = TypeFonction::Tole;
    t.esquisseId = idEsq;
    t.epaisseur = e;
    t.rayon = r;
    tole = p.ajouter(t);
  }
  int bord(const std::string& ref, double longueur, double angle = 90, const std::string& position = "matiere-interieure") {
    FonctionPiece b;
    b.type = TypeFonction::BordTombe;
    b.references = {ref};
    b.longueur = longueur;
    b.angle = angle;
    b.rayon = 0;
    b.positionPli = position;
    return p.ajouter(b);
  }
  std::string cote(char c) const { return std::to_string(tole) + ":cote:" + std::to_string(cotes.at(c)); }
};
std::string erreurs(const ResultatPiece& r) {
  std::string t;
  for (const auto& [id, m] : r.erreurs) t += std::to_string(id) + " : " + m + "\n";
  return t;
}
}  // namespace

TEST_CASE("M5 facteur K selon DIN 6935, longueur développée", "[tolerie][M5]") {
  CHECK(facteurKDin6935(2, 2) == Approx(0.325));
  CHECK(facteurKDin6935(20, 2) == Approx(0.5));
  CHECK(facteurKDin6935(1, 2) == Approx(0.25));  // borné
  CHECK(longueurDeveloppee(90, 2, 2, 0.325) == Approx(3.14159265 / 2 * 2.65));
  FonctionPiece t;
  t.type = TypeFonction::Tole;
  t.rayon = 2;
  t.epaisseur = 2;
  CHECK(facteurKTole(t) == Approx(0.325));
  t.facteurK = 0.44;
  CHECK(facteurKTole(t) == Approx(0.44));
}

TEST_CASE("M5 tôle de base et bord tombé : volume, hauteur extérieure, déplié exact, DXF", "[tolerie][M5]") {
  Tole t;
  const int b = t.bord(t.cote('b'), 30);
  const ResultatPiece r = construirePiece(t.p);
  INFO(erreurs(r));
  REQUIRE(r.erreurs.empty());
  const double pi = 3.14159265358979;
  // Plaque entaillée de r + e sous le pli, quart de tube, bord plat de 30 − (r + e)
  CHECK(volumeDe(r.forme) == Approx(100 * 56 * 2 + pi / 4 * (16 - 4) * 100 + 26 * 100 * 2).epsilon(1e-6));
  Bnd_Box bb;
  BRepBndLib::Add(r.forme, bb);
  double x0, y0, z0, x1, y1, z1;
  bb.Get(x0, y0, z0, x1, y1, z1);
  CHECK(y0 == Approx(0).margin(1e-5));   // matière intérieure : la face extérieure du bord reste sur l'arête
  CHECK(z1 == Approx(30).margin(1e-5));  // longueur mesurée à l'extérieur
  CHECK(x1 - x0 == Approx(100).margin(1e-5));
  // Faces nommées du bord
  bool bout = false, pliInt = false;
  for (const auto& f : r.faces) {
    bout |= f.nom == std::to_string(b) + ":bout";
    pliInt |= f.nom == std::to_string(b) + ":pli:int";
  }
  CHECK(bout);
  CHECK(pliInt);
  // Déplié : 56 + BA + 26 dans le sens du pli
  const DeplieTole& d = r.tole;
  REQUIRE(d.present);
  const double ba = longueurDeveloppee(90, 2, 2, 0.325);
  CHECK(d.xmax - d.xmin == Approx(100).margin(1e-6));
  CHECK(d.ymax - d.ymin == Approx(56 + ba + 26).margin(1e-6));
  REQUIRE(d.plis.size() == 1);
  CHECK(d.plis[0].versLeHaut);
  CHECK(d.plis[0].developpee == Approx(ba));
  CHECK(d.plis[0].note == "HAUT 90° R2");
  CHECK(volumeDe(solideDeplie(d)) == Approx(100 * (56 + ba + 26) * 2).epsilon(1e-6));
  // DXF : relu, contour à l'échelle, ligne de pliage sur son calque
  const std::string dxf = dxfDeplie(d, "Equerre");
  const auto lu = dxf::lireDessinDxf(dxf);
  REQUIRE(lu.calque("PLIS-HAUT"));
  int contour = 0, plis = 0;
  double ymax = 0;
  for (const auto& tr : lu.traits) {
    contour += tr.calque == "CONTOUR";
    plis += tr.calque == "PLIS-HAUT";
    ymax = std::max({ymax, tr.a.y, tr.b.y});
  }
  CHECK(contour == 4);
  CHECK(plis == 1);
  CHECK(ymax == Approx(56 + ba + 26).margin(1e-5));
  CHECK(lu.ignorees.at("TEXT") >= 2);
  CHECK(svgDeplie(d, "Equerre").find("<svg") != std::string::npos);
}

TEST_CASE("M5 bords voisins (coin), bord sur bord, sens, positions, retraits et dégagements", "[tolerie][M5]") {
  const double ba = longueurDeveloppee(90, 2, 2, 0.325);
  SECTION("deux bords voisins : le second laisse le pli du premier libre") {
    Tole t;
    t.bord(t.cote('b'), 30);
    t.bord(t.cote('d'), 30);
    const ResultatPiece r = construirePiece(t.p);
    INFO(erreurs(r));
    REQUIRE(r.erreurs.empty());
    REQUIRE(r.tole.plis.size() == 2);
    // Le second bord est raccourci de r + e + 0,2 au coin partagé (l'autre bout est libre)
    const auto& p2 = r.tole.plis[1];
    CHECK(p2.a.Distance(p2.b) == Approx(60 - 4.2).margin(1e-6));
    // Les deux bords ne se touchent pas : un seul solide, sans recouvrement (le volume est la somme des morceaux)
    const double pi = 3.14159265358979;
    const double plaque = 2 * (100 * 60 - 100 * 4 - 56 * 4), plis = pi / 4 * 12 * (100 + 55.8), bords = 26 * 2 * (100 + 55.8);
    CHECK(volumeDe(r.forme) == Approx(plaque + plis + bords).epsilon(1e-6));
  }
  SECTION("bord sur le bout d'un bord (U)") {
    Tole t;
    const int b1 = t.bord(t.cote('b'), 30);
    t.bord(std::to_string(b1) + ":bout", 20);
    const ResultatPiece r = construirePiece(t.p);
    INFO(erreurs(r));
    REQUIRE(r.erreurs.empty());
    REQUIRE(r.tole.plis.size() == 2);
    CHECK(r.tole.ymax - r.tole.ymin == Approx(56 + ba + 22 + ba + 16).margin(1e-6));
  }
  SECTION("vers le dessous, pli extérieur, 45°, longueur tangente") {
    Tole t;
    FonctionPiece b;
    b.type = TypeFonction::BordTombe;
    b.references = {t.cote('h')};
    b.longueur = 20;
    b.angle = 45;
    b.rayon = 3;
    b.inverse = true;
    b.positionPli = "pli-exterieur";
    b.mesure = "tangente";
    t.p.ajouter(b);
    const ResultatPiece r = construirePiece(t.p);
    INFO(erreurs(r));
    REQUIRE(r.erreurs.empty());
    REQUIRE(r.tole.plis.size() == 1);
    CHECK_FALSE(r.tole.plis[0].versLeHaut);
    const double ba45 = longueurDeveloppee(45, 3, 2, 0.325);
    CHECK(r.tole.ymax - r.tole.ymin == Approx(60 + ba45 + 20).margin(1e-6));
    Bnd_Box bb;
    BRepBndLib::Add(r.forme, bb);
    double x0, y0, z0, x1, y1, z1;
    bb.Get(x0, y0, z0, x1, y1, z1);
    CHECK(z0 < -10);  // plié vers le dessous
    CHECK(r.tole.plis[0].note == "BAS 45° R3");
  }
  SECTION("retraits : bord plus court que l'arête, dégagements rectangulaires") {
    Tole t;
    FonctionPiece b;
    b.type = TypeFonction::BordTombe;
    b.references = {t.cote('b')};
    b.longueur = 30;
    b.angle = 90;
    b.rayon = 0;
    b.retraitDebut = 10;
    b.retraitFin = 10;
    t.p.ajouter(b);
    const ResultatPiece r = construirePiece(t.p);
    INFO(erreurs(r));
    REQUIRE(r.erreurs.empty());
    const double pi = 3.14159265358979;
    // Plaque − entaille (80 × 4) − deux dégagements (2 × 5) ; pli et bord sur 80 mm
    CHECK(volumeDe(r.forme) == Approx(2 * (6000 - 80 * 4 - 2 * 2 * 5) + pi / 4 * 12 * 80 + 26 * 80 * 2).epsilon(1e-6));
  }
}

TEST_CASE("M5 refus expliqués et fichier", "[tolerie][M5]") {
  SECTION("arête inconnue, côté déjà plié, bord trop court, seconde tôle") {
    Tole t;
    t.bord("99:cote:1", 20);
    ResultatPiece r = construirePiece(t.p);
    REQUIRE(r.erreurs.size() == 1);
    CHECK(r.erreurs[0].second.find("n'est pas un côté droit") != std::string::npos);
    Tole t2;
    t2.bord(t2.cote('b'), 3);  // plus court que r + e
    r = construirePiece(t2.p);
    REQUIRE(r.erreurs.size() == 1);
    CHECK(r.erreurs[0].second.find("trop court") != std::string::npos);
    Tole t3;
    t3.bord(t3.cote('b'), 20);
    t3.bord(t3.cote('b'), 20);
    r = construirePiece(t3.p);
    REQUIRE_FALSE(r.erreurs.empty());
    CHECK(r.erreurs.back().second.find("déjà") != std::string::npos);
    CHECK(r.tole.plis.size() == 1);  // l'échec n'a rien laissé dans le déplié
  }
  SECTION("aller-retour JSON") {
    Tole t;
    const int b = t.bord(t.cote('g'), 25, 120, "matiere-exterieure");
    t.p.fonction(b)->retraitDebut = 3;
    t.p.fonction(b)->mesure = "interieure";
    t.p.fonction(t.tole)->facteurK = 0.4;
    t.p.fonction(t.tole)->degagement = "oblong";
    const Piece q = pieceDepuisJson(pieceVersJson(t.p));
    const FonctionPiece* tb = q.fonction(t.tole);
    const FonctionPiece* bt = q.fonction(b);
    REQUIRE(tb);
    REQUIRE(bt);
    CHECK(tb->type == TypeFonction::Tole);
    CHECK(tb->facteurK == Approx(0.4));
    CHECK(tb->degagement == "oblong");
    CHECK(bt->type == TypeFonction::BordTombe);
    CHECK(bt->angle == Approx(120));
    CHECK(bt->positionPli == "matiere-exterieure");
    CHECK(bt->mesure == "interieure");
    CHECK(bt->retraitDebut == Approx(3));
    CHECK(std::string(nomTypeFonction(TypeFonction::BordTombe)) == "Bord tombé");
    const ResultatPiece r = construirePiece(q);
    INFO(erreurs(r));
    CHECK(r.erreurs.empty());
    CHECK(r.tole.plis.size() == 1);
  }
}

// ---------------------------------------------------------------------------------------------------------------
// M6 mise en plan : coupe, détail, interruption, cotes, notes, plan d'assemblage
// ---------------------------------------------------------------------------------------------------------------
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "promethee/dessin.hpp"

namespace {
TopoDS_Shape blocPerceM6() {
  TopoDS_Shape bloc = BRepPrimAPI_MakeBox(60, 40, 30).Shape();
  for (double x : {15.0, 45.0}) bloc = BRepAlgoAPI_Cut(bloc, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, 20, -1), gp_Dir(0, 0, 1)), 5, 32).Shape()).Shape();
  return bloc;
}
const VueDessin* vueNommee(const Feuille& f, const std::string& debut) {
  for (const auto& v : f.vues)
    if (v.nom.rfind(debut, 0) == 0) return &v;
  return nullptr;
}
bool texte(const Feuille& f, const std::string& t) {
  for (const auto& x : f.textes)
    if (x.texte == t) return true;
  return false;
}
int compter(const VueDessin& v, TypeTrait t, bool rempli = false) {
  int n = 0;
  for (const auto& x : v.traits) n += x.type == t && x.rempli == rempli;
  return n;
}
// Feuille écrite pour être regardée (PROMETHEE_DESSINS=dossier)
void garder(const Feuille& f, const std::string& nom) {
  if (const char* d = std::getenv("PROMETHEE_DESSINS")) ecrireSvg(f, (std::filesystem::path(d) / (nom + ".svg")).string());
}
}  // namespace

TEST_CASE("M6 coupe A-A : matière coupée hachurée, trou laissé blanc, plan de coupe tracé dans la vue de dessus", "[dessin][M6]") {
  OptionsMiseEnPlan o;
  o.coupe = "face";
  o.titre = "Bloc";
  const Feuille f = miseEnPlan(blocPerceM6(), o);
  garder(f, "m6-coupe");
  REQUIRE(f.alertes.empty());
  const VueDessin* c = vueNommee(f, "Coupe A-A");
  REQUIRE(c);
  CHECK_FALSE(vueNommee(f, "Face"));
  CHECK(compter(*c, TypeTrait::Hachure) > 20);
  CHECK(compter(*c, TypeTrait::Cache) == 0);  // pas d'arêtes cachées dans une coupe
  // Aucune hachure dans les trous : abscisses du modèle 10–20 et 40–50 (vue de face : X vers la droite)
  int dansTrou = 0;
  for (const auto& t : c->traits) {
    if (t.type != TypeTrait::Hachure) continue;
    const double xm = (t.points[0].first + t.points[1].first) / 2;
    const double xModele = c->ox + (xm - c->cx) / c->echelle;
    dansTrou += (xModele > 10.3 && xModele < 19.7) || (xModele > 40.3 && xModele < 49.7);
  }
  CHECK(dansTrou == 0);
  const VueDessin* dessus = vueNommee(f, "Dessus");
  REQUIRE(dessus);
  CHECK(compter(*dessus, TypeTrait::Axe) >= 1);
  CHECK(compter(*dessus, TypeTrait::Cote, true) == 2);  // deux flèches
  int a = 0;
  for (const auto& x : f.textes) a += x.texte == "A";
  CHECK(a == 2);
  CHECK(texte(f, "Coupe A-A"));
  CHECK(f.tientDansLeCadre);
}

TEST_CASE("M6 vue de détail : cercle d'appel, vue agrandie bornée par son cercle", "[dessin][M6]") {
  OptionsMiseEnPlan o;
  o.detail = true;
  o.vueDetail = "dessus";
  o.detailX = -0.25;
  o.detailY = 0;
  o.detailRayon = 0.15;
  o.detailEchelle = 2;
  const Feuille f = miseEnPlan(blocPerceM6(), o);
  garder(f, "m6-detail");
  INFO(f.alertes.size());
  const VueDessin* d = vueNommee(f, "Détail B");
  REQUIRE(d);
  CHECK(d->echelle == f.echelle * 2);
  CHECK(d->traits.size() > 3);
  for (const auto& t : d->traits)
    for (const auto& [x, y] : t.points) CHECK(std::hypot(x - d->cx, y - d->cy) <= d->largeur / 2 + 1e-6);
  const VueDessin* dessus = vueNommee(f, "Dessus");
  REQUIRE(dessus);
  CHECK(compter(*dessus, TypeTrait::Fin) == 1);  // cercle d'appel
  CHECK(texte(f, "B"));
  // Le détail ne recouvre aucune autre vue
  for (const auto& v : f.vues) {
    if (&v == d) continue;
    const bool recouvre = std::abs(v.cx - d->cx) < (v.largeur + d->largeur) / 2 && std::abs(v.cy - d->cy) < (v.hauteur + d->hauteur) / 2;
    CHECK_FALSE(recouvre);
  }
}

TEST_CASE("M6 vue interrompue et cotes automatiques : vraie longueur écrite, diamètres regroupés", "[dessin][M6]") {
  TopoDS_Shape barre = BRepPrimAPI_MakeBox(400, 30, 12).Shape();
  for (double x : {20.0, 380.0}) barre = BRepAlgoAPI_Cut(barre, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, 15, -1), gp_Dir(0, 0, 1)), 4, 20).Shape()).Shape();
  OptionsMiseEnPlan o;
  o.interrompue = true;
  o.interruptionDebut = 0.2;
  o.interruptionFin = 0.8;
  o.cotes = true;
  o.notes = {"Arêtes cassées 0,5 × 45°", "Ébavurer"};
  o.toleranceGenerale = "ISO 2768-mK";
  const Feuille f = miseEnPlan(barre, o);
  garder(f, "m6-interrompue");
  const VueDessin* face = vueNommee(f, "Face");
  REQUIRE(face);
  CHECK(face->interrompue());
  CHECK(face->largeur < 0.5 * 400 * f.echelle);  // 60 % de la longueur retirés, moins l'écart
  CHECK(compter(*face, TypeTrait::Fin) == 2);
  CHECK(texte(f, "400"));  // la cote garde la vraie longueur
  CHECK(texte(f, "12"));
  CHECK(texte(f, "30"));
  CHECK(texte(f, "2× Ø8"));
  CHECK(texte(f, "Ébavurer"));
  CHECK(texte(f, "Notes :"));
  CHECK(texte(f, "ISO 2768-mK"));
  CHECK(texte(f, "Tolérances générales"));
  bool vertical = false;
  for (const auto& x : f.textes) vertical |= x.angle == 90;
  CHECK(vertical);
  // Écritures : la flèche est un SOLID, le texte vertical garde son angle, les cotes ont leur calque
  const auto dossier = std::filesystem::temp_directory_path();
  const std::string dxf = (dossier / "promethee-m6.dxf").string(), svg = (dossier / "promethee-m6.svg").string();
  REQUIRE(ecrireDxf(f, dxf));
  REQUIRE(ecrireSvg(f, svg));
  std::stringstream sa, sb;
  {
    // Fichiers refermés avant d'être effacés : Windows refuse d'effacer un fichier encore ouvert
    std::ifstream a(dxf), b(svg);
    sa << a.rdbuf();
    sb << b.rdbuf();
  }
  CHECK(sa.str().find("SOLID") != std::string::npos);
  CHECK(sa.str().find("COTES") != std::string::npos);
  CHECK(sb.str().find("rotate(-90") != std::string::npos);
  CHECK(sb.str().find("fill=\"black\"") != std::string::npos);
  std::error_code ec;
  std::filesystem::remove(dxf, ec);
  std::filesystem::remove(svg, ec);
}

TEST_CASE("M6 plan d'assemblage : bulles autour de la vue isométrique, nomenclature au-dessus du cartouche", "[dessin][M6]") {
  BRep_Builder bb;
  TopoDS_Compound c;
  bb.MakeCompound(c);
  bb.Add(c, BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 80, 50, 10).Shape());
  bb.Add(c, BRepPrimAPI_MakeBox(gp_Pnt(10, 10, 10), 20, 20, 30).Shape());
  bb.Add(c, BRepPrimAPI_MakeBox(gp_Pnt(50, 10, 10), 20, 20, 30).Shape());
  OptionsMiseEnPlan o;
  o.titre = "Support";
  o.nomenclature = {{1, 1, "Platine", "Acier S235", "SUP-01"}, {2, 2, "Colonne", "Aluminium 6060", "SUP-02"}};
  o.bulles = {{1, gp_Pnt(75, 25, 10)}, {2, gp_Pnt(20, 20, 40)}, {2, gp_Pnt(60, 20, 40)}};
  const Feuille f = miseEnPlan(c, o);
  garder(f, "m6-assemblage");
  const VueDessin* iso = vueNommee(f, "Isométrique");
  REQUIRE(iso);
  int cercles = 0;
  for (const auto& t : iso->traits) cercles += t.type == TypeTrait::Cote && !t.rempli && t.points.size() > 40;
  CHECK(cercles == 2);  // une bulle par repère, pas par exemplaire
  CHECK(texte(f, "Rep."));
  CHECK(texte(f, "Platine"));
  CHECK(texte(f, "Aluminium 6060"));
  CHECK(texte(f, "SUP-02"));
  CHECK(f.tientDansLeCadre);
}

// ---------------------------------------------------------------------------------------------------------------
// M3 nervure, M4 filetage cosmétique
// ---------------------------------------------------------------------------------------------------------------
namespace {
// Équerre : profil en L dans le plan XZ, extrudé de 40 mm en plan milieu (y de −20 à 20)
Piece equerre(int* idL = nullptr) {
  Piece p;
  FonctionPiece esq;
  esq.type = TypeFonction::Esquisse;
  esq.plan = PlanBase::XZ;
  Esquisse& e = esq.esquisse;
  const std::pair<double, double> pts[] = {{0, 0}, {60, 0}, {60, 5}, {5, 5}, {5, 60}, {0, 60}};
  std::vector<int> ids;
  for (const auto& [x, z] : pts) ids.push_back(e.ajouterPoint(x, z));
  for (std::size_t k = 0; k < ids.size(); ++k) e.ajouterLigne(ids[k], ids[(k + 1) % ids.size()]);
  const int idEsq = p.ajouter(esq);
  FonctionPiece ext;
  ext.type = TypeFonction::Extrusion;
  ext.esquisseId = idEsq;
  ext.fin = "milieu";
  ext.longueur = 40;
  const int id = p.ajouter(ext);
  if (idL) *idL = id;
  return p;
}
int trait(Piece& p, PlanBase plan, double decalage, double x1, double y1, double x2, double y2) {
  FonctionPiece esq;
  esq.type = TypeFonction::Esquisse;
  esq.plan = plan;
  esq.decalage = decalage;
  esq.esquisse.ajouterLigne(esq.esquisse.ajouterPoint(x1, y1), esq.esquisse.ajouterPoint(x2, y2));
  return p.ajouter(esq);
}
}  // namespace

TEST_CASE("M3 nervure parallèle au plan : triangle entre l'aile, la base et le trait, épaisseur centrée", "[nervure][M3]") {
  Piece p = equerre();
  const int esq = trait(p, PlanBase::XZ, 0, 5, 50, 50, 5);
  FonctionPiece n;
  n.type = TypeFonction::Nervure;
  n.esquisseId = esq;
  n.epaisseur = 4;
  n.mode = "parallele";
  n.sensMince = "milieu";
  const int id = p.ajouter(n);
  const ResultatPiece r = construirePiece(p);
  INFO(erreurs(r));
  REQUIRE(r.erreurs.empty());
  CHECK(volumeDe(r.forme) == Approx((60 * 5 + 5 * 55) * 40 + 45.0 * 45 / 2 * 4).epsilon(1e-6));
  bool flanc = false;
  for (const auto& f : r.faces) flanc |= f.nom == std::to_string(id) + ":flanc1";
  CHECK(flanc);
  // D'un seul côté du plan : même volume, la nervure va de y = 0 à y = 4 (ou −4)
  p.fonction(id)->sensMince = "exterieur";
  const ResultatPiece r2 = construirePiece(p);
  REQUIRE(r2.erreurs.empty());
  CHECK(volumeDe(r2.forme) == Approx(volumeDe(r.forme)).epsilon(1e-6));
  // Trait qui ne touche pas la pièce et ne la traverse pas : refus expliqué
  Piece q = equerre();
  FonctionPiece n2 = n;
  n2.esquisseId = trait(q, PlanBase::XZ, 0, 100, 100, 120, 110);
  q.ajouter(n2);
  const ResultatPiece r3 = construirePiece(q);
  REQUIRE(r3.erreurs.size() == 1);
  CHECK(r3.erreurs[0].second.find("ne ferme aucune région") != std::string::npos);
}

TEST_CASE("M3 nervure normale au plan : trait épaissi poussé jusqu'à la pièce, sens trouvé", "[nervure][M3]") {
  Piece p;
  FonctionPiece esq;
  esq.type = TypeFonction::Esquisse;
  esq.esquisse.ajouterRectangle(0, 0, 60, 40);
  const int idEsq = p.ajouter(esq);
  FonctionPiece ext;
  ext.type = TypeFonction::Extrusion;
  ext.esquisseId = idEsq;
  ext.longueur = 5;
  p.ajouter(ext);
  FonctionPiece n;
  n.type = TypeFonction::Nervure;
  n.esquisseId = trait(p, PlanBase::XY, 30, 10, 20, 50, 20);  // au-dessus de la plaque
  n.epaisseur = 3;
  n.mode = "normale";
  n.sensMince = "milieu";
  p.ajouter(n);
  const ResultatPiece r = construirePiece(p);
  INFO(erreurs(r));
  REQUIRE(r.erreurs.empty());
  CHECK(volumeDe(r.forme) == Approx(60 * 40 * 5 + 40 * 3 * 25).epsilon(1e-6));
  CHECK_FALSE(r.avertissements.empty());  // dirigée vers la pièce, et dit
}

TEST_CASE("M4 filetage cosmétique : arbre M10 et trou M8 reconnus, taraudage du perçage, hélice, mise en plan", "[filetage][M4]") {
  Piece p;
  FonctionPiece esq;
  esq.type = TypeFonction::Esquisse;
  const int c = esq.esquisse.ajouterCercle(esq.esquisse.ajouterPoint(0, 0), 5);
  const int idEsq = p.ajouter(esq);
  FonctionPiece ext;
  ext.type = TypeFonction::Extrusion;
  ext.esquisseId = idEsq;
  ext.longueur = 30;
  const int idExt = p.ajouter(ext);
  FonctionPiece fil;
  fil.type = TypeFonction::Filetage;
  fil.references = {std::to_string(idExt) + ":cote:" + std::to_string(c)};
  fil.norme = "ISO";
  fil.taille = "";
  fil.longueurFiletee = 15;
  const int idFil = p.ajouter(fil);
  ResultatPiece r = construirePiece(p);
  INFO(erreurs(r));
  REQUIRE(r.erreurs.empty());
  REQUIRE(r.filetages.size() == 1);
  const auto& ft = r.filetages[0];
  CHECK(ft.fonction == idFil);
  CHECK(ft.exterieur);
  CHECK(ft.taille == "M10");
  CHECK(ft.pas == Approx(1.5));
  CHECK(ft.nominal == Approx(10));
  CHECK(ft.mineur == Approx(10 - 1.2269 * 1.5));
  CHECK(ft.longueur == Approx(15));
  CHECK(ft.designation == "M10×1,5");
  REQUIRE(ft.axes.size() == 1);
  CHECK(ft.axes[0].Location().Z() == Approx(30));  // depuis le bout libre
  CHECK(ft.axes[0].Direction().Z() == Approx(-1));
  CHECK_FALSE(traceFiletages(r).IsNull());
  // Mise en plan : trois quarts de cercle en bout (vue de dessus) et indication
  OptionsMiseEnPlan o;
  o.cotes = true;
  for (const auto& f : r.filetages)
    for (const auto& a : f.axes) o.filetages.push_back({a, f.nominal, f.mineur, f.longueur, f.exterieur, f.designation});
  const Feuille feuille = miseEnPlan(r.forme, o);
  garder(feuille, "m4-filetage");
  CHECK(texte(feuille, "M10×1,5 - 15"));
  CHECK_FALSE(texte(feuille, "Ø10"));  // le filetage remplace l'indication du diamètre
  const VueDessin* dessus = vueNommee(feuille, "Dessus");
  REQUIRE(dessus);
  CHECK(compter(*dessus, TypeTrait::Fin) == 1);
  const VueDessin* face = vueNommee(feuille, "Face");
  REQUIRE(face);
  CHECK(compter(*face, TypeTrait::Fin) == 2);  // deux traits fins le long du filet
  // Taraudage : un trou percé à Ø6,8 est reconnu M8 ; aller-retour du fichier
  Piece q;
  FonctionPiece e2;
  e2.type = TypeFonction::Esquisse;
  e2.esquisse.ajouterRectangle(-20, -20, 20, 20);
  const int c2 = e2.esquisse.ajouterCercle(e2.esquisse.ajouterPoint(0, 0), 3.4);
  const int idE2 = q.ajouter(e2);
  FonctionPiece x2;
  x2.type = TypeFonction::Extrusion;
  x2.esquisseId = idE2;
  x2.longueur = 12;
  const int idX2 = q.ajouter(x2);
  FonctionPiece f2;
  f2.type = TypeFonction::Filetage;
  f2.references = {std::to_string(idX2) + ":cote:" + std::to_string(c2)};
  f2.norme = "ISO";
  f2.taille = "";
  f2.longueurFiletee = 0;  // toute la face
  q.ajouter(f2);
  const ResultatPiece r2 = construirePiece(pieceDepuisJson(pieceVersJson(q)));
  INFO(erreurs(r2));
  REQUIRE(r2.filetages.size() == 1);
  CHECK_FALSE(r2.filetages[0].exterieur);
  CHECK(r2.filetages[0].taille == "M8");
  CHECK(r2.filetages[0].nominal == Approx(8));
  CHECK(r2.filetages[0].mineur == Approx(6.8));
  CHECK(r2.filetages[0].longueur == Approx(12));
  CHECK(diametreNominal(*donneesTrou("ANSI", "1/4-20")) == Approx(6.35));
  CHECK(designationFiletage("1/4-20", 1.27, "ANSI") == "1/4-20 UNC");
}

// ---------------------------------------------------------------------------------------------------------------
// M8 axe de révolution au clic : axe de base, axe de référence, ligne d'une autre esquisse ; sens inversé
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("M8 révolution autour d'un axe de référence, d'une ligne d'une autre esquisse, sens de rotation", "[revolution][M8]") {
  auto base = [](Piece& p) {
    FonctionPiece esq;
    esq.type = TypeFonction::Esquisse;
    esq.plan = PlanBase::XZ;
    esq.esquisse.ajouterRectangle(10, 0, 20, 30);
    return p.ajouter(esq);
  };
  const double pi = 3.14159265358979;
  const double tube = pi * (400 - 100) * 30;
  auto revol = [&](Piece& p, int esq, const std::string& axe, double angle = 360, bool inverse = false) {
    FonctionPiece r;
    r.type = TypeFonction::Revolution;
    r.esquisseId = esq;
    r.axeBase = axe;
    r.angle = angle;
    r.inverse = inverse;
    return p.ajouter(r);
  };
  {
    Piece p;
    revol(p, base(p), "Z");
    const ResultatPiece r = construirePiece(p);
    INFO(erreurs(r));
    REQUIRE(r.erreurs.empty());
    CHECK(volumeDe(r.forme) == Approx(tube).epsilon(1e-6));
  }
  {
    // Axe de référence à l'intersection des plans XZ et YZ (l'axe Z)
    Piece p;
    const int esq = base(p);
    FonctionPiece a;
    a.type = TypeFonction::Axe;
    a.mode = "deuxPlans";
    a.supports = {"XZ", "YZ"};
    const int idAxe = p.ajouter(a);
    revol(p, esq, "F:" + std::to_string(idAxe));
    const ResultatPiece r = construirePiece(p);
    INFO(erreurs(r));
    REQUIRE(r.erreurs.empty());
    CHECK(volumeDe(r.forme) == Approx(tube).epsilon(1e-6));
    CHECK(parentsDe(*p.fonction(p.fonctions.back().id)) == std::vector<int>{esq, idAxe});
  }
  {
    // Ligne verticale d'une autre esquisse, dans le même plan
    Piece p;
    const int esq = base(p);
    FonctionPiece e2;
    e2.type = TypeFonction::Esquisse;
    e2.plan = PlanBase::XZ;
    const int l = e2.esquisse.ajouterLigne(e2.esquisse.ajouterPoint(0, 0), e2.esquisse.ajouterPoint(0, 10));
    const int idE2 = p.ajouter(e2);
    revol(p, esq, "E:" + std::to_string(idE2) + ":" + std::to_string(l));
    const ResultatPiece r = construirePiece(p);
    INFO(erreurs(r));
    REQUIRE(r.erreurs.empty());
    CHECK(volumeDe(r.forme) == Approx(tube).epsilon(1e-6));
  }
  {
    // Quart de tour dans un sens puis dans l'autre : même volume, de part et d'autre du plan de l'esquisse
    double y[2] = {0, 0};
    for (int k = 0; k < 2; ++k) {
      Piece p;
      revol(p, base(p), "Z", 90, k == 1);
      const ResultatPiece r = construirePiece(p);
      REQUIRE(r.erreurs.empty());
      CHECK(volumeDe(r.forme) == Approx(tube / 4).epsilon(1e-6));
      GProp_GProps g;
      BRepGProp::VolumeProperties(r.forme, g);
      y[k] = g.CentreOfMass().Y();
    }
    CHECK(y[0] * y[1] < 0);
  }
  {
    // Axe hors du plan de l'esquisse : refus expliqué
    Piece p;
    revol(p, base(p), "E:99:1");
    const ResultatPiece r = construirePiece(p);
    REQUIRE(r.erreurs.size() == 1);
    CHECK(r.erreurs[0].second.find("n'existe plus") != std::string::npos);
  }
}

TEST_CASE("M10 cartouche : révision, désignation et projet repris des propriétés de la pièce", "[dessin][M10]") {
  OptionsMiseEnPlan o;
  o.titre = "Support";
  o.reference = "SUP-001";
  o.revision = "C";
  o.designation = "Support moteur gauche";
  o.projet = "Banc d'essai";
  const Feuille f = miseEnPlan(BRepPrimAPI_MakeBox(40, 20, 10).Shape(), o);
  auto present = [&](const std::string& t) { return std::any_of(f.textes.begin(), f.textes.end(), [&](const auto& x) { return x.texte == t; }); };
  CHECK(present("Rév."));
  CHECK(present("C"));
  CHECK(present("Titre · Support moteur gauche"));
  CHECK(present("Banc d'essai"));
  // Propriété cherchée sans tenir compte de la casse ni des accents
  Piece p;
  p.proprietes = {{"RÉVISION", "D"}, {"auteur", "Évan"}};
  CHECK(p.propriete("Révision") == "D");
  CHECK(p.propriete("Auteur") == "Évan");
  CHECK(p.propriete("Projet").empty());
  // Relecture JSON : propriétés et repères gardés
  p.repere = 7;
  p.repereSymetrique = 9;
  const Piece q = pieceDepuisJson(pieceVersJson(p));
  CHECK(q.proprietes == p.proprietes);
  CHECK(q.repere == 7);
  CHECK(q.repereSymetrique == 9);
}

// ---------- M11 : mesure complète ----------
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include "promethee/mesure.hpp"

TEST_CASE("M11 mesure : éléments décrits, distance et ΔX ΔY ΔZ, centre à centre, entraxe, écart, angle vrai, totaux", "[mesure][M11]") {
  // Plaque 60 × 40 × 10 percée de deux trous Ø8 à 30 mm d'entraxe
  TopoDS_Shape plaque = BRepPrimAPI_MakeBox(60, 40, 10).Shape();
  for (double x : {15.0, 45.0}) plaque = BRepAlgoAPI_Cut(plaque, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, 20, -1), gp::DZ()), 4, 12).Shape()).Shape();
  std::vector<TopoDS_Face> plans, cylindres;
  for (TopExp_Explorer ex(plaque, TopAbs_FACE); ex.More(); ex.Next()) {
    const TopoDS_Face f = TopoDS::Face(ex.Current());
    (BRepAdaptor_Surface(f).GetType() == GeomAbs_Cylinder ? cylindres : plans).push_back(f);
  }
  REQUIRE(cylindres.size() == 2);
  auto face = [&](double z) {
    for (const auto& f : plans) {
      BRepAdaptor_Surface s(f);
      if (s.GetType() == GeomAbs_Plane && std::abs(s.Plane().Location().Z() - z) < 1e-9 && std::abs(s.Plane().Axis().Direction().Z()) > 0.99) return f;
    }
    return TopoDS_Face();
  };
  auto element = [&](const TopoDS_Shape& s) { ElementMesure e; e.forme = s; e.solide = plaque; return e; };
  // Un trou seul : Ø, rayon, aire, périmètre (couture exclue)
  const DescriptionMesure trou = decrireElement(element(cylindres[0]));
  CHECK(trou.type == "Face cylindrique");
  CHECK(trou.grandeurs[0].nom == "Ø");
  CHECK(trou.grandeurs[0].valeur == Approx(8));
  CHECK(trou.aire == Approx(2 * M_PI * 4 * 10).epsilon(1e-6));
  CHECK(trou.grandeurs.back().valeur == Approx(2 * 2 * M_PI * 4).epsilon(1e-6));  // deux cercles
  // Deux trous : entraxe et centre à centre 30 mm, distance minimale entre parois 22 mm, axes parallèles (0°)
  const ResultatMesure deux = mesurer({element(cylindres[0]), element(cylindres[1])});
  CHECK(deux.entraxe);
  CHECK(deux.valeurEntraxe == Approx(30));
  CHECK(deux.centres);
  CHECK(deux.distanceCentres == Approx(30));
  CHECK(std::abs(deux.deltaCentres.X()) == Approx(30));
  CHECK(deux.distanceMin == Approx(22));
  CHECK(deux.angle);
  CHECK(deux.natureAngle == ResultatMesure::Angle::Axes);
  CHECK(deux.angleDeg == Approx(0).margin(1e-9));
  // Dessus et dessous : écart 10, normales opposées (180°), faces parallèles
  const ResultatMesure ep = mesurer({element(face(10)), element(face(0))});
  CHECK(ep.ecartPlans);
  CHECK(ep.valeurEcartPlans == Approx(10));
  CHECK(ep.distanceMin == Approx(10));
  CHECK(ep.delta.Z() == Approx(-10));
  CHECK(ep.natureAngle == ResultatMesure::Angle::Normales);
  CHECK(ep.angleDeg == Approx(180));
  // Dessus et face avant : 90°
  TopoDS_Face avant;
  for (const auto& f : plans) { BRepAdaptor_Surface s(f); if (std::abs(s.Plane().Axis().Direction().Y()) > 0.99 && std::abs(s.Plane().Location().Y()) < 1e-9) avant = f; }
  REQUIRE(!avant.IsNull());
  CHECK(mesurer({element(face(10)), element(avant)}).angleDeg == Approx(90));
  // Arête droite et plan de base XY : angle avec le plan
  TopoDS_Edge verticale;
  for (TopExp_Explorer ex(plaque, TopAbs_EDGE); ex.More() && verticale.IsNull(); ex.Next()) {
    BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
    if (c.GetType() == GeomAbs_Line && std::abs(c.Line().Direction().Z()) > 0.99) verticale = TopoDS::Edge(ex.Current());
  }
  ElementMesure xy;
  xy.genre = ElementMesure::Genre::Plan;
  xy.plan = gp_Pln(gp::XOY());
  xy.nom = "Plan de base XY";
  const ResultatMesure dp = mesurer({element(verticale), xy});
  CHECK(dp.natureAngle == ResultatMesure::Angle::DroitePlan);
  CHECK(dp.angleDeg == Approx(90));
  CHECK(dp.elements[1].type == "Plan de base XY");
  // Arc : angle de l'arc ; trois éléments : totaux
  const TopoDS_Edge arc = BRepBuilderAPI_MakeEdge(gp_Circ(gp::XOY(), 5), 0, M_PI / 2).Edge();
  const DescriptionMesure da = decrireElement(element(arc));
  CHECK(da.type == "Arête circulaire (arc)");
  bool angleArc = false;
  for (const auto& g : da.grandeurs) angleArc = angleArc || (g.nom == "angle de l'arc" && std::abs(g.valeur - 90) < 1e-9);
  CHECK(angleArc);
  const ResultatMesure trois = mesurer({element(arc), element(verticale), element(face(10))});
  CHECK(trois.aretes == 2);
  CHECK(trois.longueurTotale == Approx(10 + 5 * M_PI / 2));
  CHECK(trois.faces == 1);
  // Lignes lisibles, en pouces
  Unites pouces;
  pouces.longueur = UniteLongueur::Pouce;
  const auto lignes = lignesMesure(deux, pouces);
  bool entraxe = false;
  for (const auto& l : lignes) entraxe = entraxe || l.find("Entraxe 1.181") != std::string::npos || l.find("Entraxe 1,181") != std::string::npos;
  CHECK(entraxe);
}

TEST_CASE("D1 D2 mise en plan : formats A0 à A4, à la française, troisième dièdre, vue de face choisie, cartouche ISO 7200", "[dessin][D1]") {
  const TopoDS_Shape piece = BRepPrimAPI_MakeBox(80, 30, 20).Shape();
  double l = 0, h = 0;
  dimensionsFeuille("A0", false, l, h);
  CHECK((l == 1189 && h == 841));
  dimensionsFeuille("A3", true, l, h);
  CHECK((l == 297 && h == 420));
  auto vueNommee = [](const Feuille& f, const std::string& nom) -> const VueDessin* {
    for (const auto& v : f.vues) if (v.nom == nom) return &v;
    return nullptr;
  };
  // Premier dièdre : dessus sous la face, gauche à sa droite
  OptionsMiseEnPlan o;
  const Feuille f1 = miseEnPlan(piece, o);
  const VueDessin *face1 = vueNommee(f1, "Face"), *dessus1 = vueNommee(f1, "Dessus"), *gauche1 = vueNommee(f1, "Gauche");
  REQUIRE((face1 && dessus1 && gauche1));
  CHECK(dessus1->cy < face1->cy);
  CHECK(gauche1->cx > face1->cx);
  CHECK(std::abs(dessus1->cx - face1->cx) < 1e-9);
  // Troisième dièdre, vue de droite : dessus au-dessus de la face, droite à sa droite
  o.troisiemeDiedre = true;
  o.vueLaterale = "droite";
  o.format = "A3";
  o.portrait = true;
  const Feuille f3 = miseEnPlan(piece, o);
  CHECK((f3.largeur == 297 && f3.hauteur == 420));
  const VueDessin *face3 = vueNommee(f3, "Face"), *dessus3 = vueNommee(f3, "Dessus"), *droite3 = vueNommee(f3, "Droite");
  REQUIRE((face3 && dessus3 && droite3));
  CHECK(dessus3->cy > face3->cy);
  CHECK(droite3->cx > face3->cx);
  CHECK(std::abs(droite3->cy - face3->cy) < 1e-9);
  CHECK(droite3->repere.Direction().IsEqual(gp_Dir(1, 0, 0), 1e-9));
  CHECK(f3.tientDansLeCadre);
  // Premier dièdre, vue de dessous : au-dessus de la face ; vue de droite : à gauche de la face
  OptionsMiseEnPlan o2;
  o2.vueVerticale = "dessous";
  o2.vueLaterale = "droite";
  const Feuille f2 = miseEnPlan(piece, o2);
  const VueDessin *face2 = vueNommee(f2, "Face"), *dessous2 = vueNommee(f2, "Dessous"), *droite2 = vueNommee(f2, "Droite");
  REQUIRE((face2 && dessous2 && droite2));
  CHECK(dessous2->cy > face2->cy);
  CHECK(droite2->cx < face2->cx);
  // Vue de face prise du dessus, tournée de 90° : la longueur de 80 mm devient verticale
  OptionsMiseEnPlan o4;
  o4.vueFace = "dessus";
  o4.rotationFace = 90;
  const Feuille f4 = miseEnPlan(piece, o4);
  const VueDessin* face4 = vueNommee(f4, "Face");
  REQUIRE(face4);
  CHECK(face4->repere.Direction().IsEqual(gp_Dir(0, 0, 1), 1e-9));
  CHECK(face4->hauteur / face4->echelle == Approx(80).margin(1e-6));
  CHECK(face4->largeur / face4->echelle == Approx(30).margin(1e-6));
  // Cartouche : cases et symbole de projection (deux cercles)
  OptionsMiseEnPlan oc;
  oc.titre = "Support";
  oc.proprietaire = "Atelier Évan";
  oc.approbateur = "J. Martin";
  oc.auteur = "Évan";
  oc.reference = "SUP-001";
  oc.numeroFeuille = "1/2";
  const Feuille fc = miseEnPlan(piece, oc);
  auto present = [&](const std::string& t) { return std::any_of(fc.textes.begin(), fc.textes.end(), [&](const auto& x) { return x.texte == t; }); };
  for (const char* t : {"Propriétaire légal", "Atelier Évan", "Approuvé par", "J. Martin", "Créé par", "Évan", "Feuille", "1/2", "Échelle", "SUP-001", "Masse", "Rév."}) CHECK(present(t));
  int cercles = 0;
  for (const auto& t : fc.cadre) cercles += t.points.size() == 37;
  CHECK(cercles == 2);
}

#include "promethee/cotes_modele.hpp"

TEST_CASE("D3 cotes du modèle en mise en plan : longueurs, diamètre, cote de référence, orphelines", "[dessin][D3]") {
  Piece p;
  FonctionPiece es;
  es.type = TypeFonction::Esquisse;
  const auto lignes = es.esquisse.ajouterRectangle(0, 0, 60, 40);
  es.esquisse.contraindre(TypeContrainte::Longueur, {lignes[0]}, 60);
  es.esquisse.contraindre(TypeContrainte::Longueur, {lignes[1]}, 40);
  const int cercle = es.esquisse.ajouterCercle(es.esquisse.ajouterPoint(30, 20), 5);
  es.esquisse.contraindre(TypeContrainte::Diametre, {cercle}, 10);
  const int ref = es.esquisse.contraindre(TypeContrainte::Longueur, {lignes[2]}, 60);
  for (auto& c : es.esquisse.contraintes) if (c.id == ref) c.reference = true;
  resoudre(es.esquisse);
  const int ie = p.ajouter(es);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = 10;
  p.ajouter(x);
  // Une seconde esquisse, ouverte, dont l'extrusion échoue : ses cotes sont orphelines
  FonctionPiece es2;
  es2.type = TypeFonction::Esquisse;
  const int a = es2.esquisse.ajouterPoint(0, 0), b = es2.esquisse.ajouterPoint(20, 0);
  const int l = es2.esquisse.ajouterLigne(a, b);
  es2.esquisse.contraindre(TypeContrainte::Longueur, {l}, 20);
  resoudre(es2.esquisse);
  const int ie2 = p.ajouter(es2);
  FonctionPiece x2;
  x2.type = TypeFonction::Extrusion;
  x2.esquisseId = ie2;
  x2.longueur = 5;
  p.ajouter(x2);
  const ResultatPiece r = construirePiece(p);
  REQUIRE(!r.forme.IsNull());
  std::vector<std::string> orphelines;
  const auto cotes = cotesDuModele(p, r, &orphelines);
  int lineaires = 0, diametres = 0, references = 0;
  for (const auto& k : cotes) {
    lineaires += k.genre == OptionsMiseEnPlan::CoteModele::Genre::Lineaire;
    diametres += k.genre == OptionsMiseEnPlan::CoteModele::Genre::Diametre;
    references += k.reference;
    CHECK(k.normale.IsParallel(gp_Dir(0, 0, 1), 1e-9));
  }
  CHECK(lineaires == 3);
  CHECK(diametres == 1);
  CHECK(references == 1);
  CHECK(orphelines.size() == 1);
  OptionsMiseEnPlan o;
  o.cotesModele = cotes;
  o.cotesOrphelines = orphelines;
  const Feuille f = miseEnPlan(r.forme, o);
  auto present = [&](const std::string& t) { return std::any_of(f.textes.begin(), f.textes.end(), [&](const auto& z) { return z.texte == t; }); };
  CHECK(present("60"));
  CHECK(present("40"));
  CHECK(present("Ø10"));
  CHECK(present("(60)"));
  CHECK(std::any_of(f.alertes.begin(), f.alertes.end(), [](const std::string& m) { return m.find("Cote orpheline") != std::string::npos; }));
  // Vue de face prise de l'avant : le plan de l'esquisse n'y est pas de face, mais les longueurs le long de X y sont
  // en vraie grandeur ; le diamètre, lui, part dans la vue de dessus
  if (const char* dossier = std::getenv("PROMETHEE_DESSINS")) ecrireSvg(f, std::string(dossier) + "/d3-cotes-modele.svg");
}

TEST_CASE("D4 demi-coupe : la moitié droite en coupe hachurée, la gauche vue de l'extérieur, limite en axe", "[dessin][D4]") {
  // Bague : cylindre Ø40 percé Ø20, axe Y (vue de face : on voit le profil en long)
  const TopoDS_Shape bague = BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(gp_Ax2(gp::Origin(), gp::DY()), 20, 30).Shape(),
                                             BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, -1, 0), gp::DY()), 10, 32).Shape()).Shape();
  OptionsMiseEnPlan o;
  o.coupe = "face";
  o.vueFace = "dessus";  // la vue de face regarde le long de Z : l'axe Y de la bague est vertical sur la feuille
  o.demiCoupe = true;
  const Feuille f = miseEnPlan(bague, o);
  const VueDessin* coupe = nullptr;
  for (const auto& v : f.vues) if (v.nom == "Coupe A-A") coupe = &v;
  REQUIRE(coupe);
  int hachures = 0, axes = 0;
  double xMinHachure = 1e300;
  for (const auto& t : coupe->traits) {
    if (t.type == TypeTrait::Hachure) { ++hachures; for (const auto& [x, y] : t.points) xMinHachure = std::min(xMinHachure, x); }
    axes += t.type == TypeTrait::Axe;
  }
  CHECK(hachures > 0);
  CHECK(axes >= 1);
  CHECK(xMinHachure >= coupe->cx - 1e-6);  // hachures dans la moitié droite seulement
  if (const char* dossier = std::getenv("PROMETHEE_DESSINS")) ecrireSvg(f, std::string(dossier) + "/d4-demi-coupe.svg");
}

TEST_CASE("S2 cote d'angle : secteur choisi (deux lignes dans un sens ou l'autre), valeur tenue par le solveur", "[esquisse][S2]") {
  Esquisse e;
  const int o = e.ajouterPoint(0, 0), a = e.ajouterPoint(10, 0), b = e.ajouterPoint(10 * std::cos(M_PI / 6), 10 * std::sin(M_PI / 6));
  const int l1 = e.ajouterLigne(o, a), l2 = e.ajouterLigne(o, b);
  e.contraindre(TypeContrainte::Fixe, {o}, 0, 0);
  e.contraindre(TypeContrainte::Horizontal, {l1});
  e.contraindre(TypeContrainte::Longueur, {l1}, 10);
  e.contraindre(TypeContrainte::Longueur, {l2}, 10);
  ContrainteEsquisse k;
  k.type = TypeContrainte::Angle;
  k.refs = {l1, l2};
  CHECK(mesurer(e, k) == Approx(30));
  k.valeur2 = 1;  // première ligne à rebours : l'angle supplémentaire (orienté)
  CHECK(std::abs(mesurer(e, k)) == Approx(150));
  k.valeur2 = 3;  // les deux à rebours : le même angle, opposé par le sommet
  CHECK(mesurer(e, k) == Approx(30));
  // Coter le secteur supplémentaire à 120° : la seconde ligne fait 60° avec la première
  const int id = e.contraindre(TypeContrainte::Angle, {l1, l2}, -120, 1);
  (void)id;
  const EtatEsquisse s = resoudre(e);
  CHECK(s.resolue);
  const PointEsquisse* pb = e.point(b);
  CHECK(std::atan2(pb->y, pb->x) * 180 / M_PI == Approx(60).margin(1e-6));
}

TEST_CASE("S3 références externes associatives : l'arête reprise suit la pièce quand elle change", "[esquisse][S3]") {
  Piece p;
  FonctionPiece es;
  es.type = TypeFonction::Esquisse;
  const auto cotes = es.esquisse.ajouterRectangle(0, 0, 40, 20);
  const int longueur = es.esquisse.contraindre(TypeContrainte::Longueur, {cotes[0]}, 40);
  es.esquisse.contraindre(TypeContrainte::Longueur, {cotes[1]}, 20);
  es.esquisse.contraindre(TypeContrainte::Fixe, {es.esquisse.elements[0].p1}, 0, 0);
  resoudre(es.esquisse);
  const int ie = p.ajouter(es);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = ie;
  x.longueur = 10;
  const int ix = p.ajouter(x);
  const ResultatPiece r0 = construirePiece(p);
  // Arête du dessus le long de X (y = 0, z = 10)
  std::string nom;
  for (TopExp_Explorer ex(r0.forme, TopAbs_EDGE); ex.More(); ex.Next()) {
    BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
    const gp_Pnt a = c.Value(c.FirstParameter()), b = c.Value(c.LastParameter());
    if (std::abs(a.Z() - 10) < 1e-9 && std::abs(b.Z() - 10) < 1e-9 && std::abs(a.Y()) < 1e-9 && std::abs(b.Y()) < 1e-9) nom = r0.nomArete(TopoDS::Edge(ex.Current()));
  }
  REQUIRE(!nom.empty());
  // Esquisse sur le dessus : l'arête reprise, liée, et un cercle dont le centre est coté depuis son extrémité
  FonctionPiece es2;
  es2.type = TypeFonction::Esquisse;
  es2.face = std::to_string(ix) + ":sommet";
  p.ajouter(es2);  // pour obtenir son repère
  const ResultatPiece r1 = construirePiece(p);
  const gp_Ax3 ax = r1.repere(p.fonctions.back().id);
  auto uv = [&](const gp_Pnt& q) { const gp_Vec w(ax.Location(), q); return std::pair<double, double>{w.Dot(gp_Vec(ax.XDirection())), w.Dot(gp_Vec(ax.YDirection()))}; };
  Esquisse& e2 = p.fonctions.back().esquisse;
  const auto [ua, va] = uv(gp_Pnt(0, 0, 10));
  const auto [ub, vb] = uv(gp_Pnt(40, 0, 10));
  const int pa = e2.ajouterPoint(ua, va), pb = e2.ajouterPoint(ub, vb);
  e2.contraindre(TypeContrainte::Fixe, {pa}, ua, va);
  e2.contraindre(TypeContrainte::Fixe, {pb}, ub, vb);
  e2.ajouterLigne(pa, pb, true);
  e2.liensExternes = {{pa, nom, false, 0}, {pb, nom, false, 0}};
  resoudre(e2);
  // La longueur passe de 40 à 60 : le point repris suit l'arête
  for (auto& c : p.fonctions.front().esquisse.contraintes) if (c.id == longueur) c.valeur = 60;
  resoudre(p.fonctions.front().esquisse);
  const ResultatPiece r2 = construirePiece(p);
  const Esquisse* suivie = r2.esquisseSuivie(p.fonctions.back().id);
  REQUIRE(suivie);
  const auto [uc, vc] = uv(gp_Pnt(60, 0, 10));
  const PointEsquisse* qb = suivie->point(pb);
  const PointEsquisse* qa = suivie->point(pa);
  REQUIRE((qa && qb));
  CHECK(std::hypot(qb->x - uc, qb->y - vc) < 1e-6);
  CHECK(std::hypot(qa->x - ua, qa->y - va) < 1e-6);
  CHECK(r2.avertissements.empty());
  // Relecture JSON des liens
  const Esquisse relue = esquisseDepuisJson(esquisseVersJson(e2));
  REQUIRE(relue.liensExternes.size() == 2);
  CHECK(relue.liensExternes[0].arete == nom);
  // Arête disparue (nom inconnu) : avertissement, géométrie gardée
  p.fonctions.back().esquisse.liensExternes[0].arete = "999:rien|998:rien";
  const ResultatPiece r3 = construirePiece(p);
  CHECK(!r3.avertissements.empty());
}
