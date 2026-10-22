// Tests des pièces exactes (OpenCascade). Licence GPL-3.0-only.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "promethee/derive.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/modele.hpp"

using namespace prom;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {
const Json& reference() {
  static const Json j = [] {
    std::ifstream f(std::string(PROMETHEE_DONNEES) + "/reference_js.json");
    std::stringstream s;
    s << f.rdbuf();
    return Json::parse(s.str());
  }();
  return j;
}
}  // namespace

TEST_CASE("Chaque projet de référence donne deux solides sains au volume du prototype") {
  for (const auto& cas : reference().at("cas")) {
    const std::string nom = cas.at("nom").get<std::string>();
    const Projet p = normaliser(cas.at("projet"));
    const Derive d = deriver(p);
    const TopoDS_Shape corps = construireCorps(p, d), couvercle = construireCouvercle(p, d);
    INFO(nom);
    REQUIRE(estValide(corps));
    REQUIRE(estValide(couvercle));
    CHECK(nombreSolides(corps) == 1);
    CHECK(nombreSolides(couvercle) == 1);
    // Le prototype maille les arrondis finement ; la pièce exacte doit tomber à moins de 0,3 %.
    const double vc = cas.at("volumes").at("corps").get<double>(), vl = cas.at("volumes").at("couvercle").get<double>();
    INFO("corps " << volume(corps) << " / " << vc << ", couvercle " << volume(couvercle) << " / " << vl);
    CHECK(std::abs(volume(corps) - vc) / vc < 0.003);
    CHECK(std::abs(volume(couvercle) - vl) / vl < 0.003);
    const Boite b = encombrement(corps);
    CHECK(b.zmin == Approx(0).margin(1e-6));
    CHECK(b.zmax == Approx(d.zt).margin(1e-6));
    CHECK(b.xmax - b.xmin == Approx(d.Lo).margin(1e-6));
  }
}

TEST_CASE("Les pièces passent par le STEP sans perte") {
  const Projet p = exemple();
  const Derive d = deriver(p);
  const TopoDS_Shape corps = construireCorps(p, d), couvercle = construireCouvercle(p, d);
  const fs::path dossier = fs::temp_directory_path() / "promethee_step";
  fs::create_directories(dossier);
  const std::string chemin = (dossier / "boitier.step").string();
  exporterStep({{"Boîtier", corps}, {"Couvercle", couvercle}}, chemin);
  const TopoDS_Shape relu = lireStep(chemin);
  CHECK(nombreSolides(relu) == 2);
  CHECK(volume(relu) == Approx(volume(corps) + volume(couvercle)).epsilon(1e-6));
  std::string contenu;
  {
    std::ifstream f(chemin);  // refermé à la fin du bloc : Windows refuse d'effacer un fichier ouvert
    std::stringstream s;
    s << f.rdbuf();
    contenu = s.str();
  }
  CHECK(contenu.find("Couvercle") != std::string::npos);
  std::error_code ec;
  fs::remove_all(dossier, ec);
}

TEST_CASE("Les pièces sont posées sur le plateau, couvercle retourné") {
  const Projet p = exemple();
  const Derive d = deriver(p);
  const TopoDS_Shape corps = pourImpression(construireCorps(p, d), false, d.cx, d.cy);
  const TopoDS_Shape couvercle = pourImpression(construireCouvercle(p, d), true, d.cx, d.cy);
  const Boite bc = encombrement(corps), bl = encombrement(couvercle);
  CHECK(bc.zmin == Approx(0).margin(1e-6));
  CHECK(bl.zmin == Approx(0).margin(1e-6));
  CHECK(bl.zmax == Approx(p.boitier.couvercle + Levre::h).margin(1e-6));
  CHECK((bc.xmin + bc.xmax) / 2 == Approx(0).margin(1e-6));
  const fs::path stl = fs::temp_directory_path() / "promethee_couvercle.stl";
  exporterStl(couvercle, stl.string());
  CHECK(fs::file_size(stl) > 10000);
  std::error_code ec;
  fs::remove(stl, ec);
}

TEST_CASE("La géométrie suit le modèle : agrandir la carte agrandit le boîtier") {
  Projet p = exemple();
  const double v0 = volume(construireCorps(p, deriver(p)));
  p.carte.L += 10;
  const Derive d = deriver(p);
  const TopoDS_Shape corps = construireCorps(p, d);
  CHECK(estValide(corps));
  CHECK(volume(corps) > v0);
  CHECK(encombrement(corps).xmax - encombrement(corps).xmin == Approx(76).margin(1e-6));
}
