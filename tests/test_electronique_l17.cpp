// Prométhée : essais du lot L17 (essais, mise en service, fiabilité). Licence GPL-3.0-only.
#include <algorithm>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/essais_production.hpp"
#include "promethee/simulation_avancee.hpp"

using namespace prom::ecad;

namespace {
struct Montage {
  ConceptionElec c = nouvelleConception("Carte d'essai");
  double x = 25.4;
  ComposantElec* poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    Proposition p;
    p.elements.push_back({ref, symbole, valeur, 0, liaisons});
    poserProposition(c, p, 0, ptMm(x, 50.8));
    x += 25.4;
    return &c.composants.back();
  }
};
// Trois rails : VIN par le connecteur, +5V par un 7805, 3V3 par un régulateur à partir de +5V
Montage troisRails() {
  Montage m;
  m.poser("Conn_01x02", "J1", "", {{"1", "VIN"}, {"2", "GND"}});
  m.poser("LM7805", "U1", "LM7805", {{"1", "VIN"}, {"2", "GND"}, {"3", "+5V"}});
  m.poser("Regulateur", "U2", "3V3", {{"3", "+5V"}, {"1", "GND"}, {"2", "3V3"}});
  m.poser("C", "C1", "10u", {{"1", "+5V"}, {"2", "GND"}});
  m.poser("C", "C2", "10u", {{"1", "3V3"}, {"2", "GND"}});
  return m;
}
}  // namespace

TEST_CASE("17.2 · Plan de mise sous tension : contrôles à l'ohmmètre, puis les rails dans l'ordre des régulateurs", "[L17][mise]") {
  const Montage m = troisRails();
  const PlanMiseSousTension p = planMiseSousTension(m.c);
  REQUIRE(p.rails.size() == 3);
  CHECK(p.entrees == std::vector<std::string>{"VIN"});
  CHECK(p.rails[0] == "VIN");
  CHECK(p.rails[1] == "+5V");
  CHECK(p.rails[2] == "3V3");
  CHECK(p.etapes.front().action.find("ohmmètre") != std::string::npos);
  const auto mesure = std::find_if(p.etapes.begin(), p.etapes.end(), [](const EtapeMiseSousTension& e) { return e.action.rfind("Mesurer 3V3", 0) == 0; });
  REQUIRE(mesure != p.etapes.end());
  CHECK(mesure->valeur == Catch::Approx(3.3));
  CHECK(mesure->min == Catch::Approx(3.135));
  CHECK(std::any_of(p.alertes.begin(), p.alertes.end(), [](const std::string& a) { return a.find("VIN") != std::string::npos; }));  // tension de VIN inconnue
}

TEST_CASE("17.3 à 17.5 · Points de test, couverture, procès-verbal par numéro de série", "[L17][pv]") {
  Montage m = troisRails();
  ComposantElec* tp1 = m.poser("Point_test", "TP1", "", {{"1", "3V3"}});
  tp1->champs["Valeur attendue"] = "3,3 V";
  tp1->champs["Tolérance"] = "3 %";
  ComposantElec* tp2 = m.poser("Point_test", "TP2", "", {{"1", "+5V"}});
  tp2->champs["Valeur attendue"] = "5 V";
  const auto pts = pointsDeTest(m.c);
  REQUIRE(pts.size() == 2);
  CHECK(pts[0].reseau == "3V3");
  CHECK(pts[0].attendue == Catch::Approx(3.3));
  CHECK(pts[0].tolerance == Catch::Approx(0.03));
  CHECK(pts[1].tolerance == Catch::Approx(0.05));
  const CouvertureTest cv = couvertureTest(m.c);
  CHECK(cv.reseaux >= 4);
  CHECK(cv.accessibles >= 2);
  CHECK(cv.taux > 0);
  const ProcesVerbal bon = procesVerbal(m.c, "SN-0042", {{"TP1", 3.31}, {"+5V", 5.02}}, "Lou", "2026-10-22");
  CHECK(bon.conforme);
  const ProcesVerbal mauvais = procesVerbal(m.c, "SN-0043", {{"TP1", 3.0}, {"TP2", 5.0}});
  CHECK_FALSE(mauvais.conforme);
  CHECK(mauvais.csv().find("hors tolérance") != std::string::npos);
  const ProcesVerbal incomplet = procesVerbal(m.c, "SN-0044", {{"TP1", 3.3}});
  CHECK_FALSE(incomplet.conforme);
  CHECK(incomplet.csv().find("non mesuré") != std::string::npos);
}

TEST_CASE("17.7 à 17.10 · Contraintes d'emploi, durée de vie, MTBF, conformité", "[L17][fiabilite]") {
  Montage m;
  m.poser("Conn_01x02", "J1", "", {{"1", "+12V"}, {"2", "GND"}});
  m.poser("C", "C1", "10u", {{"1", "+12V"}, {"2", "GND"}})->parametres["tension"] = "10 V";
  m.poser("C", "C2", "10u", {{"1", "+12V"}, {"2", "GND"}})->parametres["tension"] = "25 V";
  ComposantElec* r = m.poser("R", "R1", "100", {{"1", "+12V"}, {"2", "GND"}});
  r->empreinte = "R_0603";
  m.poser("R", "R2", "100k", {{"1", "+12V"}, {"2", "GND"}})->empreinte = "R_0603";
  const auto ce = contraintesEmploi(m.c);
  const auto trouve = [&](const std::string& ref) { return std::find_if(ce.begin(), ce.end(), [&](const ContrainteEmploi& x) { return x.reference == ref; }); };
  REQUIRE(trouve("C1") != ce.end());
  CHECK(trouve("C1")->depasse);  // 12 V sur 10 V
  CHECK_FALSE(trouve("C2")->depasse);
  REQUIRE(trouve("R1") != ce.end());
  CHECK(trouve("R1")->emploi == Catch::Approx(1.44));  // 12² / 100
  CHECK(trouve("R1")->depasse);
  CHECK_FALSE(trouve("R2")->depasse);
  CHECK(dureeVie(2000, 105, 65) == Catch::Approx(32000));
  CHECK(dureeVie(2000, 105, 115) == Catch::Approx(1000));
  const Fiabilite f = fiabilite(m.c, 40);
  CHECK(f.lignes.size() == 5);
  CHECK(f.fitTotal == Catch::Approx(5 + 1 + 1 + 0.5 + 0.5));
  CHECK(f.mtbfHeures == Catch::Approx(1e9 / 8));
  CHECK(fiabilite(m.c, 50).fitTotal == Catch::Approx(2 * f.fitTotal));
  // Conformité
  m.c.composants[1].parametres["rohs"] = "oui";
  m.c.composants[1].parametres["reach"] = "oui";
  m.c.composants[2].parametres["rohs"] = "non";
  const Conformite co = conformite(m.c);
  CHECK(co.conformes == 1);
  CHECK(co.nonConformes.size() == 1);
  CHECK(!co.inconnus.empty());
}
