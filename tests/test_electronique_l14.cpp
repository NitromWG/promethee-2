// Prométhée : essais du lot L14 (simulation par passes, assistants). Licence GPL-3.0-only.
#include <cmath>
#include <numbers>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/simulation_avancee.hpp"
#include "promethee/valeurs.hpp"

using namespace prom::ecad;

namespace {
struct Montage {
  ConceptionElec c = nouvelleConception("Montage");
  double x = 25.4;
  void poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    Proposition p;
    p.elements.push_back({ref, symbole, valeur, 0, liaisons});
    poserProposition(c, p, 0, ptMm(x, 50.8));
    x += 25.4;
  }
};
ConceptionElec filtreRC(const std::string& r = "1k", const std::string& c = "100n") {
  Montage m;
  m.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", "R1", r, {{"1", "ENTREE"}, {"2", "SORTIE"}});
  m.poser("C", "C1", c, {{"1", "SORTIE"}, {"2", "GND"}});
  return m.c;
}
AnalyseSpice alternatif() {
  AnalyseSpice a;
  a.type = AnalyseSpice::Type::Alternatif;
  a.fDebut = 10;
  a.fFin = 1e6;
  a.pointsParDecade = 100;
  return a;
}
}  // namespace

TEST_CASE("14.12, 14.13 · Assistants : valeurs normalisées et ce qu'elles donnent", "[L14][assistants]") {
  const Proposition led = assistantLed(5, 2, 0.01);
  REQUIRE(led.elements.size() == 2);
  CHECK(led.resultats.at("R") == Catch::Approx(300));
  CHECK(led.resultats.at("I") == Catch::Approx(0.01));
  const Proposition div = assistantDiviseur(12, 3.3, 10e3);
  CHECK(div.resultats.at("Vout") == Catch::Approx(3.3).epsilon(0.03));
  const Proposition ast = assistantAstable555(1000, 0.6, 100e-9);
  CHECK(ast.resultats.at("f") == Catch::Approx(1000).epsilon(0.05));
  CHECK(ast.resultats.at("rapport") == Catch::Approx(0.6).epsilon(0.05));
  for (const auto& e : ast.elements)
    if (e.symbole == "R") CHECK(dansSerie(lireValeurComposant(e.valeur, FamilleValeur::Resistance).nombre, 24));
  const Proposition rc = assistantPasseBasRC(1591.5, 100e-9);
  CHECK(rc.resultats.at("fc") == Catch::Approx(1591.5).epsilon(0.03));
  const Proposition sk = assistantSallenKey(1000);
  CHECK(sk.resultats.at("fc") == Catch::Approx(1000).epsilon(0.05));
  CHECK(sk.resultats.at("Q") == Catch::Approx(0.7071).epsilon(0.06));
  // Posé dans le schéma : repères libres, réseaux reliés
  ConceptionElec c = nouvelleConception("Assistant");
  poserSymbole(c, 0, "R", ptMm(10, 10));
  c.composants.back().reference = "R1";
  const auto refs = poserProposition(c, sk, 0, ptMm(30, 80));
  CHECK(refs.size() == 5);
  CHECK(std::find(refs.begin(), refs.end(), "R1") == refs.end());  // R1 était pris
  const Connectivite k = connectivite(c);
  CHECK(k.indice("SK_N") >= 0);
  CHECK(k.indice("SORTIE") >= 0);
}

TEST_CASE("14.9 · Monte-Carlo et pire cas d'un filtre RC, balayage de R", "[L14][passes]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  ConceptionElec c = filtreRC();
  // Tolérances : 5 % pour R (défaut), 10 % pour C (défaut)
  const double fc = 1 / (2 * std::numbers::pi * 1000 * 100e-9);
  const double bas = 1 / (2 * std::numbers::pi * 1050 * 110e-9), haut = 1 / (2 * std::numbers::pi * 950 * 90e-9);
  const ResultatPasses mc = monteCarlo(c, alternatif(), mesureCoupure("SORTIE"), 30, 7);
  INFO(mc.erreur);
  REQUIRE(mc.mesures.size() == 30);
  CHECK(mc.nominale == Catch::Approx(fc).epsilon(0.01));
  CHECK(mc.min >= bas * 0.99);
  CHECK(mc.max <= haut * 1.01);
  CHECK(mc.ecartType > 0);
  const ResultatPasses pc = pireCas(c, alternatif(), mesureCoupure("SORTIE"));
  CHECK(pc.min == Catch::Approx(bas).epsilon(0.01));
  CHECK(pc.max == Catch::Approx(haut).epsilon(0.01));
  CHECK(pc.sensibilites.at("R1") == Catch::Approx(-1).margin(0.05));
  CHECK(pc.sensibilites.at("C1") == Catch::Approx(-1).margin(0.05));
  // Tolérance lue dans le composant
  c.composants[1].parametres["tolerance"] = "1 %";
  const ResultatPasses pc1 = pireCas(c, alternatif(), mesureCoupure("SORTIE"), {{"C1", 0.01}});
  CHECK(pc1.max / pc1.min == Catch::Approx(1.0 / 0.99 / 0.99 * 1.01 * 1.01).epsilon(0.01));
  const auto b = balayer(c, alternatif(), "R1", {500, 1000, 2000}, mesureCoupure("SORTIE"));
  REQUIRE(b.size() == 3);
  CHECK(b[0].second == Catch::Approx(2 * b[1].second).epsilon(0.02));
  CHECK(b[2].second == Catch::Approx(b[1].second / 2).epsilon(0.02));
}

TEST_CASE("14.13 · Sallen-Key de Butterworth posé par l'assistant : coupure simulée à 3 % de la prévision", "[L14][assistants][ngspice]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  for (const bool passeHaut : {false, true}) {
    const Proposition sk = assistantSallenKey(1000, passeHaut);
    Montage m;
    m.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
    m.poser("Source_tension", "V2", "DC 12", {{"1", "VCC"}, {"2", "GND"}});
    m.poser("Source_tension", "V3", "DC 12", {{"1", "GND"}, {"2", "VEE"}});
    poserProposition(m.c, sk, 0, ptMm(120, 50.8));
    const NetlistSpice n = netlistSpice(m.c, alternatif());
    INFO(n.texte);
    REQUIRE(n.valide());
    const ResultatSimulation r = simuler(n.texte);
    INFO(r.erreur);
    REQUIRE(r.reussi());
    const double fc = mesureCoupure("SORTIE")(r, n);
    CHECK(fc == Catch::Approx(sk.resultats.at("fc")).epsilon(0.03));
    CHECK(fc == Catch::Approx(1000).epsilon(0.06));
  }
}
