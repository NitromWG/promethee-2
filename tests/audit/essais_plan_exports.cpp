// Essais de l'audit de Prométhée 0.25 : mise en plan, exports et imports (constats MP-xx et EX-xx). Les fichiers produits sont relus ensuite par des
// lecteurs indépendants (Python : ezdxf, xml, zipfile, trimesh…).
#include <filesystem>
#include <fstream>
#include <sstream>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <IGESControl_Controller.hxx>
#include <IGESControl_Writer.hxx>
#include <Interface_Static.hxx>

#include "aides.hpp"
#include "promethee/dessin.hpp"
#include "promethee/echanges.hpp"

using namespace verif;
namespace fs = std::filesystem;

namespace {
TopoDS_Shape pieceEssai() {
  // pavé 60×40×30, entaille et trou dans la face avant
  Bloc b = bloc(60, 40, 30);
  FonctionPiece e; e.type = TypeFonction::Esquisse; e.plan = PlanBase::XZ;
  e.esquisse.ajouterRectangle(40, 15, 60, 30);
  const int ie = b.p.ajouter(e);
  FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ie; x.enlever = true; x.fin = "traversant"; x.inverse = true;
  b.p.ajouter(x);
  const int ic = esquisseCercles(b.p, "", {{15, 15, 5}}, PlanBase::XZ);
  FonctionPiece t; t.type = TypeFonction::Extrusion; t.esquisseId = ic; t.enlever = true; t.longueur = 10; t.inverse = true;
  b.p.ajouter(t);
  const ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  return r.forme;
}
struct BoiteVue { double x0, y0, x1, y1; };
BoiteVue boiteVue(const VueDessin& v) { return {v.cx - v.largeur / 2, v.cy - v.hauteur / 2, v.cx + v.largeur / 2, v.cy + v.hauteur / 2}; }
bool chevauche(const BoiteVue& a, const BoiteVue& b) { return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1; }
}  // namespace

TEST_CASE("OK convention de projection européenne", "[ok]") {
  OptionsMiseEnPlan o; o.titre = "Essai";
  const Feuille f = miseEnPlan(pieceEssai(), o);
  const VueDessin *face = nullptr, *dessus = nullptr, *gauche = nullptr;
  for (const auto& v : f.vues) { if (v.nom == "Face") face = &v; if (v.nom == "Dessus") dessus = &v; if (v.nom == "Gauche") gauche = &v; }
  REQUIRE(face); REQUIRE(dessus); REQUIRE(gauche);
  INFO("échelle " << texteEchelle(f.echelle) << " ; face (" << face->cx << ";" << face->cy << ") dessus (" << dessus->cx << ";" << dessus->cy << ") gauche (" << gauche->cx << ";" << gauche->cy << ")");
  CHECK(dessus->cx == Approx(face->cx));
  CHECK(dessus->cy < face->cy);
  CHECK(gauche->cy == Approx(face->cy));
  CHECK(gauche->cx > face->cx);
}

TEST_CASE("MP-02 échelle automatique : vues qui se chevauchent sur les pièces longues", "[MP-02]") {
  struct Cas { double L, W, H; };
  int chevauchements = 0;
  std::ostringstream journal;
  for (const Cas c : {Cas{650, 100, 50}, Cas{1440, 110, 50}, Cas{300, 20, 20}, Cas{10, 10, 10}, Cas{2000, 50, 50}, Cas{120, 80, 60}}) {
    const TopoDS_Shape s = BRepPrimAPI_MakeBox(c.L, c.W, c.H).Shape();
    for (const std::string fmt : {"A4", "A3"}) {
      OptionsMiseEnPlan o; o.format = fmt;
      const Feuille f = miseEnPlan(s, o);
      std::vector<BoiteVue> b; for (const auto& v : f.vues) b.push_back(boiteVue(v));
      bool ch = false;
      for (size_t i = 0; i < b.size(); ++i) for (size_t j = i + 1; j < b.size(); ++j) if (chevauche(b[i], b[j])) ch = true;
      // dans le cadre (10 mm) et hors du cartouche (180 × 30 en bas à droite)
      bool dehors = false;
      for (const auto& x : b) {
        if (x.x0 < 10 || x.y0 < 10 || x.x1 > f.largeur - 10 || x.y1 > f.hauteur - 10) dehors = true;
        if (x.x1 > f.largeur - 10 - 180 && x.y0 < 10 + 30) dehors = true;
      }
      journal << c.L << "×" << c.W << "×" << c.H << " " << fmt << " → " << texteEchelle(f.echelle) << (ch ? " CHEVAUCHEMENT" : "") << (dehors ? " HORS CADRE/CARTOUCHE" : "") << "\n";
      if (ch || dehors) ++chevauchements;
    }
  }
  WARN(journal.str());
  CHECK(chevauchements == 0);
}

TEST_CASE("MP-05 vue de face décochée : la vue de gauche sort du cadre", "[MP-05]") {
  OptionsMiseEnPlan o; o.face = false;
  const Feuille f = miseEnPlan(BRepPrimAPI_MakeBox(100, 100, 100).Shape(), o);
  for (const auto& v : f.vues) {
    const BoiteVue b = boiteVue(v);
    INFO(v.nom << " : y[" << b.y0 << ";" << b.y1 << "] (hauteur de feuille " << f.hauteur << ", cadre à " << f.hauteur - 10 << ")");
    CHECK(b.y1 <= f.hauteur - 10 + 1e-6);
  }
}

TEST_CASE("MP-09 texte d'échelle non normalisée", "[MP-09]") {
  INFO("0,4 → « " << texteEchelle(0.4) << " » ; 1,5 → « " << texteEchelle(1.5) << " »");
  CHECK(texteEchelle(0.4) == "2:5");
  CHECK(texteEchelle(1.5) == "3:2");
}

TEST_CASE("EX-fichiers : DXF, SVG, STL, 3MF, OBJ, PLY, STEP, IGES pour relecture indépendante", "[EX-fichiers]") {
  const std::string d = dossierSorties();
  const TopoDS_Shape s = pieceEssai();
  OptionsMiseEnPlan o; o.titre = "Équerre R&D <V2>"; o.materiau = "Acier « S235 »";
  const Feuille f = miseEnPlan(s, o);
  CHECK(ecrireDxf(f, d + "/plan.dxf"));
  CHECK(ecrireSvg(f, d + "/plan.svg"));
  exporterStl(s, d + "/piece.stl");
  exporterStlTexte(s, d + "/piece_texte.stl");
  exporter3mf({{"Bride & joint", s}}, d + "/piece.3mf");
  exporter3mf({{"Bride", s}}, d + "/piece_simple.3mf");
  exporterObj({{"Support moteur", s, 0.18, 0.42, 0.31}}, d + "/Support moteur.obj");
  exporterPly({{"Support", s, 0.18, 0.42, 0.31}}, d + "/piece.ply");
  exporterStep({{"Vert", s, 0.18, 0.42, 0.31}, {"Plaque d’appui Ø6", s.Moved(TopLoc_Location(gp_Trsf())), 0.8, 0.1, 0.1}}, d + "/couleurs.step");
  exporterIges({{"Piece", s}}, d + "/piece.igs");
  exporterBrep({{"Piece", s}}, d + "/piece.brep");
  // assemblage : deux instances d'une même forme et une copie miroir, comme l'atelier d'assemblage
  gp_Trsf t; t.SetTranslation(gp_Vec(100, 0, 0));
  gp_Trsf m; m.SetMirror(gp::YOZ());
  exporterStep({{"Plaque_1", s}, {"Plaque_2", s.Moved(TopLoc_Location(t))}, {"Plaque_miroir", BRepBuilderAPI_Transform(s, m, true).Shape()}}, d + "/assemblage.step");
  SUCCEED();
}

TEST_CASE("OK import IGES écrit en pouces", "[ok]") {
  const std::string d = dossierSorties();
  IGESControl_Controller::Init();
  IGESControl_Writer w("IN", 0);
  w.AddShape(BRepPrimAPI_MakeBox(20, 10, 5).Shape());
  w.ComputeModel();
  REQUIRE(w.Write((d + "/pouces.igs").c_str()));
  std::string err; const std::string brep = lireFichierCAO(d + "/pouces.igs", &err);
  REQUIRE_FALSE(brep.empty());
  const Boite b = encombrement(formeDepuisBrep(brep));
  INFO("boîte relue : " << b.xmax - b.xmin << " × " << b.ymax - b.ymin << " × " << b.zmax - b.zmin << " (écrit : 20 × 10 × 5 mm)");
  CHECK(b.xmax - b.xmin == Approx(20).margin(1e-3));
}

TEST_CASE("OK fichiers invalides à l'import", "[ok]") {
  const std::string d = dossierSorties();
  struct Cas { std::string nom, contenu; };
  std::string stl84(84, '\0'); stl84[80] = '\xff'; stl84[81] = '\xff'; stl84[82] = '\xff'; stl84[83] = '\x7f';
  for (const Cas& c : {Cas{"faux.stl", stl84}, Cas{"bruit.igs", std::string(5000, 'x')}, Cas{"tronque.brep", "CASCADE Topology V1, (c) Matra-Datavision\nLocations 0\nCurve2ds 3\n1 0 0"},
                       Cas{"entete.step", "ISO-10303-21;\nHEADER;\nENDSEC;\n"}, Cas{"vide.step", ""}}) {
    std::ofstream(d + "/" + c.nom, std::ios::binary) << c.contenu;
    std::string err, res;
    bool exception = false;
    try { res = lireFichierCAO(d + "/" + c.nom, &err); } catch (...) { exception = true; }
    INFO(c.nom << " : exception " << exception << " ; résultat " << res.size() << " octets ; message « " << err << " »");
    CHECK_FALSE(exception);
    CHECK((res.empty() && !err.empty()));
  }
}

TEST_CASE("EX-08 STL relu : solide ?", "[EX-08]") {
  const std::string d = dossierSorties();
  exporterStl(BRepPrimAPI_MakeBox(10, 10, 10).Shape(), d + "/cube.stl");
  std::string err; const TopoDS_Shape s = formeDepuisBrep(lireFichierCAO(d + "/cube.stl", &err));
  INFO("solides " << nombreSolides(s) << " ; statut masse " << libelleStatut(proprietesMasse(s, 1).statut));
  CHECK(nombreSolides(s) == 1);
}

TEST_CASE("PERF mise en plan : temps de calcul sur une pièce à 100 trous", "[perf]") {
  Bloc b = bloc(200, 100, 10);
  FonctionPiece s; s.type = TypeFonction::Esquisse; s.face = b.face("sommet");
  for (int i = 0; i < 10; ++i) for (int j = 0; j < 10; ++j) s.esquisse.ajouterPoint(10 + 20 * i, 5 + 10 * j);
  const int is = b.p.ajouter(s);
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "lame"; t.taille = "M3"; t.traversant = true;
  b.p.ajouter(t);
  Chrono c0; const ResultatPiece r = construirePiece(b.p); const double tc = c0.ms();
  Chrono c1; const Feuille f = miseEnPlan(r.forme, OptionsMiseEnPlan{}); const double tp = c1.ms();
  WARN("plaque à 100 trous lamés : construction " << tc << " ms ; mise en plan (4 vues, lignes cachées exactes) " << tp << " ms ; " << f.vues.size() << " vues");
  CHECK(tp < 5000);
}
