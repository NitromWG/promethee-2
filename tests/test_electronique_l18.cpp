// Prométhée : essais du lot L18 (intégrité du signal et de l'alimentation). Licence GPL-3.0-only.
#include <algorithm>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/integrite.hpp"
#include "promethee/simulation_avancee.hpp"

using namespace prom::ecad;

namespace {
Piste piste(const std::string& reseau, Pt a, Pt b, Nm largeur = 250'000, Couche c = Couche::CuivreDessus) {
  Piste t;
  t.uuid = nouvelUuid();
  t.reseau = reseau;
  t.couche = c;
  t.a = a;
  t.b = b;
  t.largeur = largeur;
  return t;
}
ConceptionElec carteNue() {
  ConceptionElec c = nouvelleConception("Lignes");
  c.circuit.contour.origine = {0, 0};
  c.circuit.contour.largeur = nm(50);
  c.circuit.contour.hauteur = nm(20);
  c.circuit.empilage.couchesCuivre = 2;
  return c;
}
}  // namespace

TEST_CASE("18.3 · Plan de référence relevé sous chaque tronçon : la moitié d'une piste au-dessus du vide", "[L18][impedance]") {
  ConceptionElec c = carteNue();
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = "GND";
  z.couche = Couche::CuivreDessous;
  z.contour = {ptMm(0.5, 0.5), ptMm(25, 0.5), ptMm(25, 19.5), ptMm(0.5, 19.5)};
  c.circuit.zones.push_back(z);
  Via ancre;  // le plan tient à son réseau par une via
  ancre.uuid = nouvelUuid();
  ancre.reseau = "GND";
  ancre.p = ptMm(2, 2);
  c.circuit.vias.push_back(ancre);
  c.circuit.pistes.push_back(piste("CLK", ptMm(5, 10), ptMm(45, 10)));
  c.circuit.pistes.push_back(piste("DATA", ptMm(5, 15), ptMm(20, 15)));
  const RapportImpedance r = analyserImpedance(c);
  REQUIRE(r.troncons.size() == 2);
  const auto& clk = r.troncons[0].reseau == "CLK" ? r.troncons[0] : r.troncons[1];
  const auto& data = r.troncons[0].reseau == "CLK" ? r.troncons[1] : r.troncons[0];
  INFO("part sans plan : " << clk.partSansPlan << ", " << data.partSansPlan);
  CHECK(clk.partSansPlan == Catch::Approx(0.5).margin(0.1));
  CHECK_FALSE(clk.reference);
  CHECK(data.reference);
  CHECK(data.plan == "GND");
  CHECK(data.z0 > 100);
  CHECK(data.z0 < 160);  // 0,25 mm sur 1,6 mm de FR-4 : environ 130 Ω
  REQUIRE(r.alertes.size() == 1);
  CHECK(r.alertes[0].find("CLK") != std::string::npos);
  // Une piste plus large : impédance plus basse
  c.circuit.pistes[1].largeur = nm(3);
  CHECK(analyserImpedance(c, {"DATA"}).troncons.front().z0 < data.z0 * 0.7);
}

TEST_CASE("18.2 · Diaphonie de deux pistes parallèles proches", "[L18][diaphonie]") {
  ConceptionElec c = carteNue();
  c.circuit.pistes.push_back(piste("A", ptMm(5, 5), ptMm(25, 5)));
  c.circuit.pistes.push_back(piste("B", ptMm(5, 5.45), ptMm(25, 5.45)));  // 0,2 mm de bord à bord
  c.circuit.pistes.push_back(piste("C", ptMm(5, 15), ptMm(25, 15)));      // loin
  c.circuit.pistes.push_back(piste("D", ptMm(30, 5), ptMm(30, 15)));      // perpendiculaire
  const auto r = analyserDiaphonie(c);
  REQUIRE(r.size() == 1);
  CHECK(r[0].agresseur == "A");
  CHECK(r[0].victime == "B");
  CHECK(r[0].longueurCouplee == Catch::Approx(20).epsilon(0.01));
  CHECK(r[0].ecart == Catch::Approx(0.2).epsilon(0.01));
  CHECK(r[0].coefficient == Catch::Approx(1 / (1 + (0.2 / 1.6) * (0.2 / 1.6))).epsilon(0.01));
}

TEST_CASE("18.5 · Chute de tension d'un rail et inductance de montage d'un découplage", "[L18][alimentation]") {
  ConceptionElec c = nouvelleConception("Rail");
  Proposition p;
  p.elements.push_back({"J1", "Conn_01x02", "", 0, {{"1", "+5V"}, {"2", "GND"}}});
  p.elements.push_back({"R1", "R", "10", 0, {{"1", "+5V"}, {"2", "GND"}}});
  p.elements.push_back({"C1", "C", "100n", 0, {{"1", "+5V"}, {"2", "GND"}}});
  poserProposition(c, p, 0, ptMm(25.4, 50.8));
  mettreAJourCircuit(c);
  REQUIRE(c.circuit.empreintes.size() == 3);
  const auto placer = [&](const std::string& ref, double x, double y) {
    for (auto& e : c.circuit.empreintes)
      if (c.composant(e.composant)->reference == ref) {
        e.position = ptMm(x, y);
        e.rotation = 0;
      }
  };
  placer("J1", 10, 10);
  placer("R1", 110, 10);
  placer("C1", 50, 30);
  const Connectivite k = connectivite(c);
  std::map<std::string, Pt> pad;
  for (const auto& x : pastilles(c, k)) pad[x.reference + "." + x.numero] = x.pastille.position;
  // Une piste de J1.1 à R1.1, de 0,25 mm
  c.circuit.pistes.push_back(piste("+5V", pad["J1.1"], pad["R1.1"]));
  const double l = distance(pad["J1.1"], pad["R1.1"]) * 1e-6;
  const ResultatChute r = chuteTension(c, "+5V", "J1.1", 5.0, {{"R1.1", 1.0}});
  INFO((r.alertes.empty() ? std::string() : r.alertes[0]));
  REQUIRE(r.chutes.count("R1.1"));
  const double attendue = 1.72e-5 * l / (0.25 * c.circuit.empilage.cuivre);
  CHECK(r.chutes.at("R1.1") == Catch::Approx(attendue).epsilon(0.01));
  CHECK(r.courantMax == Catch::Approx(1.0).epsilon(0.01));
  CHECK(r.densiteMax == Catch::Approx(1.0 / (0.25 * c.circuit.empilage.cuivre)).epsilon(0.01));
  // Charge non reliée : signalée
  const ResultatChute r2 = chuteTension(c, "+5V", "J1.1", 5.0, {{"C1.1", 0.1}});
  CHECK(!r2.alertes.empty());
  // Découplage : 3 mm de piste de C1.1 à une via
  const Pt v{pad["C1.1"].x, pad["C1.1"].y - nm(3)};
  c.circuit.pistes.push_back(piste("+5V", pad["C1.1"], v));
  Via via;
  via.uuid = nouvelUuid();
  via.reseau = "+5V";
  via.p = v;
  c.circuit.vias.push_back(via);
  const auto d = analyserDecouplage(c);
  REQUIRE(d.size() == 1);
  CHECK(d[0].condensateur == "C1");
  CHECK(d[0].vias == 1);
  CHECK(d[0].longueur == Catch::Approx(3).epsilon(0.01));
  CHECK(d[0].inductance == Catch::Approx(3.9).epsilon(0.02));
}
