// Prométhée : mise en plan (MP-01 à MP-10) : mise en page en millimètres de papier, priorité des traits, axes,
// échelles exactes, fichiers échappés et codés. Licence GPL-3.0-only.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <gp_Ax2.hxx>

#include "promethee/dessin.hpp"

using namespace prom;

namespace {
TopoDS_Shape blocPerce() {
  const TopoDS_Shape bloc = BRepPrimAPI_MakeBox(60, 40, 30).Shape();
  const TopoDS_Shape trou = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(20, 20, -1), gp_Dir(0, 0, 1)), 5, 32).Shape();
  return BRepAlgoAPI_Cut(bloc, trou).Shape();
}
std::string lire(const std::string& chemin) {
  std::ifstream f(chemin, std::ios::binary);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}
}  // namespace

TEST_CASE("Mise en plan : échelles écrites exactement, série ISO 5455") {
  CHECK(texteEchelle(0.4) == "2:5");
  CHECK(texteEchelle(1.5) == "3:2");
  CHECK(texteEchelle(0.5) == "1:2");
  CHECK(texteEchelle(5) == "5:1");
  CHECK(echelleNormalisee(0.4) == 0.5);
  CHECK(echelleNormalisee(3) == 2);
}

TEST_CASE("Mise en plan : aucun trait caché sur un trait vu, axes sur le trou") {
  const auto traits = projeter(blocPerce(), gp_Ax2(gp::Origin(), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0)), true);
  int axes = 0, caches = 0;
  for (const auto& t : traits) {
    axes += t.type == TypeTrait::Axe;
    caches += t.type == TypeTrait::Cache;
  }
  CHECK(axes >= 1);    // la ligne d'axe du trou vu de face
  CHECK(caches >= 2);  // les deux génératrices cachées du trou
  // Aucun segment caché n'est posé sur un segment vu
  for (const auto& c : traits) {
    if (c.type != TypeTrait::Cache) continue;
    for (std::size_t i = 1; i < c.points.size(); ++i) {
      const double mx = (c.points[i - 1].first + c.points[i].first) / 2, my = (c.points[i - 1].second + c.points[i].second) / 2;
      for (const auto& v : traits) {
        if (v.type != TypeTrait::Vu) continue;
        for (std::size_t j = 1; j < v.points.size(); ++j) {
          const double ax = v.points[j - 1].first, ay = v.points[j - 1].second, bx = v.points[j].first, by = v.points[j].second;
          const double l2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
          if (l2 < 1e-18) continue;
          const double t = std::clamp(((mx - ax) * (bx - ax) + (my - ay) * (by - ay)) / l2, 0.0, 1.0);
          CHECK(std::hypot(mx - (ax + t * (bx - ax)), my - (ay + t * (by - ay))) > 1e-6);
        }
      }
    }
  }
  // Vue de dessus : croix de centre (deux traits d'axe)
  const auto dessus = projeter(blocPerce(), gp_Ax2(gp::Origin(), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)), true);
  int croix = 0;
  for (const auto& t : dessus) croix += t.type == TypeTrait::Axe;
  CHECK(croix == 2);
}

TEST_CASE("Mise en plan : sans vue de face, rien ne sort du cadre (MP-05) ; un petit cube passe à 5:1 (MP-02)") {
  OptionsMiseEnPlan o;
  o.face = false;
  const Feuille f = miseEnPlan(BRepPrimAPI_MakeBox(100, 100, 100).Shape(), o);
  for (const auto& v : f.vues) {
    CHECK(v.cy + v.hauteur / 2 <= f.hauteur - 10 + 1e-6);
    CHECK(v.cy - v.hauteur / 2 >= 10 - 1e-6);
  }
  CHECK(f.tientDansLeCadre);
  const Feuille petit = miseEnPlan(BRepPrimAPI_MakeBox(10, 10, 10).Shape(), OptionsMiseEnPlan{});
  CHECK(petit.echelle == 5);
}

TEST_CASE("Mise en plan : SVG échappé, DXF déclaré en Windows-1252 (MP-01, MP-04)") {
  OptionsMiseEnPlan o;
  o.titre = "Équerre R&D <V2>";
  o.materiau = "Acier « S235 »";
  const Feuille f = miseEnPlan(blocPerce(), o);
  const std::filesystem::path d = std::filesystem::temp_directory_path() / "promethee-essai-dessin";
  std::filesystem::create_directories(d);
  REQUIRE(ecrireSvg(f, (d / "plan.svg").string()));
  REQUIRE(ecrireDxf(f, (d / "plan.dxf").string()));
  const std::string svg = lire((d / "plan.svg").string());
  CHECK(svg.find("R&amp;D &lt;V2&gt;") != std::string::npos);
  CHECK(svg.find("R&D <V2>") == std::string::npos);
  const std::string dxf = lire((d / "plan.dxf").string());
  CHECK(dxf.find("$DWGCODEPAGE\n3\nANSI_1252") != std::string::npos);
  CHECK(dxf.find("\xC9" "chelle") != std::string::npos);  // « Échelle » en Windows-1252
  CHECK(dxf.find("\xC3\x89") == std::string::npos);      // plus d'UTF-8 brut
  CHECK(versCp1252("Ø6 — « œ »") == std::string("\xD8" "6 \x97 \xAB \x9C \xBB"));
}
