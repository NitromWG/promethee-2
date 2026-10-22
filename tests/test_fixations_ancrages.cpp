// Tests des trous paramétrables, des ancrages, de la nomenclature et des formats d'échange.
// Licence GPL-3.0-only.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <IGESControl_Reader.hxx>

#include "promethee/derive.hpp"
#include "promethee/echanges.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/modele.hpp"
#include "promethee/nomenclature.hpp"
#include "promethee/verifs.hpp"

using namespace prom;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {
Trou& trou(Projet& p, const std::string& ref) { for (auto& t : p.trous) if (t.ref == ref) return t; throw std::runtime_error(ref); }
Composant& comp(Projet& p, const std::string& ref) { for (auto& k : p.composants) if (k.ref == ref) return k; throw std::runtime_error(ref); }
const TrouPlace& place(const Derive& d, const std::string& ref) { for (const auto& t : d.trous) if (t.ref == ref) return t; throw std::runtime_error(ref); }
bool contient(const std::vector<Probleme>& pb, const std::string& code) {
  return std::any_of(pb.begin(), pb.end(), [&](const Probleme& q) { return q.code == code; });
}
std::string lireTout(const fs::path& f) { std::ifstream i(f, std::ios::binary); std::stringstream s; s << i.rdbuf(); return s.str(); }
}  // namespace

TEST_CASE("Chaque trou peut avoir sa vis et un insert laiton") {
  Projet p = exemple();
  trou(p, "T1").vis = "M2";
  trou(p, "T2").fixation = "insert";
  const Derive d = deriver(p);
  CHECK(place(d, "T1").vis.trou == Approx(2.2));
  CHECK(place(d, "T1").rTete < place(d, "T3").rTete);
  CHECK(place(d, "T2").insert);
  CHECK(place(d, "T2").rp == Approx(insert("M3").trou / 2));
  CHECK(place(d, "T2").Rb > place(d, "T3").Rb);
  // Le pilier de 5 mm est trop court pour un insert M3 de 5,7 mm : alerte, puis correction.
  auto pb = verifier(p, d);
  REQUIRE(contient(pb, "insert_long"));
  corrigerTout(p);
  CHECK(p.boitier.entretoise == Approx(6.5));
  CHECK_FALSE(contient(verifier(p, deriver(p)), "insert_long"));
  // La pièce suit : le logement de l'insert est plus large que l'avant-trou.
  const TopoDS_Shape corps = construireCorps(p, deriver(p));
  CHECK(estValide(corps));
  CHECK(nombreSolides(corps) == 1);
}

TEST_CASE("Les réglages des trous survivent à l'enregistrement") {
  Projet p = exemple();
  trou(p, "T1").vis = "M4";
  trou(p, "T1").fixation = "insert";
  changerAncrageTrou(p, trou(p, "T2"), "libre");
  const Projet q = normaliser(versJson(p));
  const Trou& t1 = q.trous[0];
  CHECK(t1.vis == std::optional<std::string>("M4"));
  CHECK(t1.fixation == "insert");
  CHECK(q.trous[1].ancrage == "libre");
  CHECK_FALSE(q.trous[1].coin.has_value());
  CHECK(versJson(q) == versJson(p));
}

TEST_CASE("Un trou peut suivre un coin imposé, même loin de lui") {
  Projet p = exemple();
  Trou& t = trou(p, "T1");
  t.ancrage = "NE";
  ancrerTrou(p, t, 0, 0);
  REQUIRE(t.coin == std::optional<std::string>("NE"));
  CHECK(t.ox == Approx(30));
  p.carte.L += 10;
  p.carte.x0 += 5;  // la carte s'agrandit vers la droite
  CHECK(posTrou(p, t).x == Approx(10));
}

TEST_CASE("Un composant peut suivre un coin, le centre, ou rester où il est") {
  Projet p = exemple();
  Composant& u1 = comp(p, "U1");
  Composant& u2 = comp(p, "U2");
  Composant& c1 = comp(p, "C1");
  changerAncrage(p, u1, "NE");
  changerAncrage(p, c1, "centre");
  const Point avantU1 = posComposant(p, u1), avantU2 = posComposant(p, u2), avantC1 = posComposant(p, c1);
  // La carte grandit de 10 mm vers la droite, comme en tirant la poignée de droite.
  p.carte.L += 10;
  p.carte.x0 += 5;
  CHECK(posComposant(p, u1).x == Approx(avantU1.x + 10));  // suit le coin haut droit
  CHECK(posComposant(p, u2).x == Approx(avantU2.x));       // libre : ne bouge pas
  CHECK(posComposant(p, c1).x == Approx(avantC1.x + 5));   // suit le centre
  // Déplacer un composant ancré garde son ancrage et met à jour ses distances.
  placerComposant(p, u1, 20, 0);
  CHECK(u1.ancrage == "NE");
  CHECK(posComposant(p, u1).x == Approx(20));
  const Projet q = normaliser(versJson(p));
  CHECK(posComposant(q, q.composants[1]).x == Approx(20));
  CHECK(q.composants[1].ancrage == "NE");
}

TEST_CASE("Un connecteur de bord peut suivre une extrémité du bord") {
  Projet p = exemple();
  Composant& j1 = comp(p, "J1");  // bord gauche, au milieu
  changerAncrage(p, j1, "fin");
  const double avant = leLongComposant(p, j1);
  p.carte.W += 8;
  p.carte.y0 += 4;  // la carte grandit vers le haut
  CHECK(leLongComposant(p, j1) == Approx(avant + 8));
  changerAncrage(p, j1, "debut");
  p.carte.W += 6;
  p.carte.y0 += 3;
  CHECK(leLongComposant(p, j1) == Approx(avant + 8));
  const Derive d = deriver(p);
  for (const auto& g : d.comps) if (g.ref == "J1") CHECK(g.decoupe->u == Approx(avant + 8));
}

TEST_CASE("Le verrou est enregistré avec le projet") {
  Projet p = exemple();
  comp(p, "SW1").verrou = true;
  const Projet q = normaliser(versJson(p));
  CHECK(q.composants[7].verrou);
  CHECK_FALSE(q.composants[0].verrou);
}

TEST_CASE("La nomenclature regroupe électronique, pièces fabriquées et visserie") {
  Projet p = exemple();
  auto l = nomenclature(p, deriver(p));
  CHECK(std::count_if(l.begin(), l.end(), [](const LigneNomenclature& x) { return x.groupe == "Électronique"; }) == 9);
  CHECK(std::count_if(l.begin(), l.end(), [](const LigneNomenclature& x) { return x.groupe == "Pièces fabriquées"; }) == 3);
  const auto vis = std::find_if(l.begin(), l.end(), [](const LigneNomenclature& x) { return x.groupe == "Visserie"; });
  REQUIRE(vis != l.end());
  CHECK(vis->qte == 4);
  CHECK(vis->designation.find("M3 × 6") != std::string::npos);
  trou(p, "T1").fixation = "insert";
  l = nomenclature(p, deriver(p));
  CHECK(std::any_of(l.begin(), l.end(), [](const LigneNomenclature& x) { return x.designation.find("Insert laiton") != std::string::npos && x.qte == 1; }));
  const std::string csv = versCsv(l);
  CHECK(csv.rfind("\xEF\xBB\xBF" "Groupe;Repère;Désignation", 0) == 0);
}

TEST_CASE("Les pièces s'exportent dans de nombreux formats") {
  const Projet p = exemple();
  const Derive d = deriver(p);
  const std::vector<PieceNommee> pieces = {{"Boîtier", construireCorps(p, d), 0.83, 0.85, 0.83}, {"Couvercle", construireCouvercle(p, d), 0.9, 0.91, 0.9}};
  const fs::path dossier = fs::temp_directory_path() / "promethee_formats";
  fs::create_directories(dossier);
  exporterIges(pieces, (dossier / "boitier.igs").string());
  exporterBrep(pieces, (dossier / "boitier.brep").string());
  exporterObj(pieces, (dossier / "boitier.obj").string());
  exporterPly(pieces, (dossier / "boitier.ply").string());
  exporter3mf(pieces, (dossier / "boitier.3mf").string());
  exporterStlTexte(pieces[0].forme, (dossier / "boitier_texte.stl").string());
  { std::ofstream(dossier / "carte.dxf", std::ios::binary) << dxfCarte(p, d); }
  { std::ofstream(dossier / "carte.svg", std::ios::binary) << svgCarte(p, d); }

  TopoDS_Shape relu;
  BRep_Builder b;
  REQUIRE(BRepTools::Read(relu, (dossier / "boitier.brep").string().c_str(), b));
  CHECK(volume(relu) == Approx(volume(pieces[0].forme) + volume(pieces[1].forme)).epsilon(1e-9));
  IGESControl_Reader r;
  REQUIRE(r.ReadFile((dossier / "boitier.igs").string().c_str()) == IFSelect_RetDone);
  r.TransferRoots();
  CHECK_FALSE(r.OneShape().IsNull());
  const std::string obj = lireTout(dossier / "boitier.obj");
  CHECK(obj.find("\nv ") != std::string::npos);
  CHECK(obj.find("\nf ") != std::string::npos);
  CHECK(fs::exists(dossier / "boitier.mtl"));
  CHECK(lireTout(dossier / "boitier.ply").rfind("ply\nformat binary_little_endian", 0) == 0);
  CHECK(lireTout(dossier / "boitier.3mf").rfind("PK", 0) == 0);
  CHECK(lireTout(dossier / "boitier_texte.stl").rfind("solid", 0) == 0);
  const std::string dxf = lireTout(dossier / "carte.dxf");
  CHECK(dxf.find("PERCAGES") != std::string::npos);
  CHECK(dxf.find("EOF") != std::string::npos);
  CHECK(lireTout(dossier / "carte.svg").find("<svg") != std::string::npos);
  std::error_code ec;
  fs::remove_all(dossier, ec);
}

TEST_CASE("Les dimensions d'un trou se personnalisent et reviennent au standard") {
  Projet p = exemple();
  Trou& t = trou(p, "T3");
  t.vis = "M5";
  t.fixation = "insert";
  CHECK(place(deriver(p), "T3").rp == Approx(insert("M5").trou / 2));
  t.diamPilier = 11;
  t.diamLogement = 6.0;
  t.longueurInsert = 4;
  t.diamTrou = 5.5;
  const Derive d = deriver(p);
  CHECK(place(d, "T3").Rb == Approx(5.5));
  CHECK(place(d, "T3").rp == Approx(3.0));
  CHECK(place(d, "T3").longInsert == Approx(4));
  CHECK(place(d, "T3").vis.trou == Approx(5.5));
  const Projet q = normaliser(versJson(p));
  CHECK(q.trous[2].diamPilier == std::optional<double>(11));
  CHECK(versJson(q) == versJson(p));
  CHECK(estValide(construireCorps(q, deriver(q))));
  t.diamPilier.reset(); t.diamLogement.reset(); t.longueurInsert.reset(); t.diamTrou.reset();
  CHECK(versJson(p)["trous"][2].contains("dimensions") == false);
}
