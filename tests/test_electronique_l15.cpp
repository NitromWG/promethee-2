// Prométhée : essais du lot L15 (logique numérique, FPGA). Licence GPL-3.0-only.
#include <algorithm>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/logique.hpp"
#include "promethee/simulation_avancee.hpp"

using namespace prom::ecad;

namespace {
struct Logique {
  ConceptionElec c = nouvelleConception("Logique");
  double x = 25.4;
  void poser(const std::string& symbole, const std::string& ref, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    Proposition p;
    p.elements.push_back({ref, symbole, "", 0, liaisons});
    poserProposition(c, p, 0, ptMm(x, 50.8));
    x += 25.4;
  }
};
// Étiquettes globales au bout des broches d'un symbole posé
void relier(ConceptionElec& c, const std::string& symbole, const std::vector<std::pair<std::string, std::string>>& liaisons) {
  for (auto& p : c.feuilles[0].symboles)
    if (p.uuid == symbole)
      for (const auto& br : brochesPlacees(p, *c.symbole(p.symbole)))
        for (const auto& [num, reseau] : liaisons)
          if (br.broche->numero == num) {
            const Pt d = br.direction == 0 ? Pt{1, 0} : br.direction == 1 ? Pt{0, 1} : br.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
            const Pt bout{br.p.x - d.x * 2'540'000, br.p.y - d.y * 2'540'000};
            c.feuilles[0].fils.push_back({nouvelUuid(), br.p, bout});
            Etiquette e;
            e.uuid = nouvelUuid();
            e.texte = reseau;
            e.p = bout;
            e.type = TypeEtiquette::Globale;
            c.feuilles[0].etiquettes.push_back(e);
          }
}
}  // namespace

TEST_CASE("15.9 · Demi-additionneur : table de vérité en logique à quatre états", "[L15][logique]") {
  Logique l;
  l.poser("Porte_OU_X", "U1", {{"1", "A"}, {"2", "B"}, {"3", "S"}});
  l.poser("Porte_ET", "U2", {{"1", "A"}, {"2", "B"}, {"3", "RETENUE"}});
  const CircuitLogique c = circuitLogique(l.c);
  CHECK(c.portes.size() == 2);
  CHECK(c.entrees == std::vector<std::string>{"A", "B"});
  const auto t = tableVerite(c, {"A", "B"}, {"S", "RETENUE"});
  REQUIRE(t.size() == 4);
  CHECK(t[0] == std::pair<std::string, std::string>{"00", "00"});
  CHECK(t[1] == std::pair<std::string, std::string>{"01", "10"});
  CHECK(t[2] == std::pair<std::string, std::string>{"10", "10"});
  CHECK(t[3] == std::pair<std::string, std::string>{"11", "01"});
  // Une entrée inconnue rend X, sauf quand l'autre suffit (0 sur un ET)
  SimulateurLogique s(c);
  s.forcer("A", Niveau::Bas);
  s.avancerJusquauRepos();
  CHECK(s.niveau("RETENUE") == Niveau::Bas);
  CHECK(s.niveau("S") == Niveau::X);
  // Verilog structurel
  const std::string v = verilogStructurel(c, "demi_additionneur");
  CHECK(v.find("module demi_additionneur") != std::string::npos);
  CHECK(v.find("input wire A") != std::string::npos);
  CHECK(v.find("output wire S") != std::string::npos);
  CHECK(v.find("xor #10 U1 (S, A, B);") != std::string::npos);
  CHECK(v.find("and #10 U2 (RETENUE, A, B);") != std::string::npos);
  CHECK(v.find("endmodule") != std::string::npos);
}

TEST_CASE("15.9 · Bascule RS en portes NON-ET d'un 74HC00, oscillateur en anneau, diviseur par deux", "[L15][logique]") {
  {
    // Bascule RS : /S et /R actifs à 0, deux portes d'un 74HC00 posé porte par porte
    Logique l;
    const std::string a = poserSymbole(l.c, 0, "74HC00_unites", ptMm(50.8, 50.8));
    const std::string b = poserUnite(l.c, 0, a, 2);
    REQUIRE(!b.empty());
    for (auto& sp : l.c.feuilles[0].symboles)
      if (sp.uuid == b) sp.position = ptMm(101.6, 50.8);  // à l'écart de la première porte
    relier(l.c, a, {{"1", "nS"}, {"2", "nQ"}, {"3", "Q"}});
    relier(l.c, b, {{"4", "nR"}, {"5", "Q"}, {"6", "nQ"}});
    const CircuitLogique c = circuitLogique(l.c);
    REQUIRE(c.portes.size() == 2);
    SimulateurLogique s(c);
    s.forcer("nS", Niveau::Bas);
    s.forcer("nR", Niveau::Haut);
    s.avancerJusquauRepos();
    CHECK(s.niveau("Q") == Niveau::Haut);
    s.forcer("nS", Niveau::Haut);  // mémoire
    s.avancerJusquauRepos();
    CHECK(s.niveau("Q") == Niveau::Haut);
    CHECK(s.niveau("nQ") == Niveau::Bas);
    s.forcer("nR", Niveau::Bas);
    s.avancerJusquauRepos();
    CHECK(s.niveau("Q") == Niveau::Bas);
  }
  {
    // Anneau de trois inversions, validé par EN : période 2 × 3 × 10 ns
    Logique l;
    l.poser("Porte_NON_ET", "U1", {{"1", "EN"}, {"2", "Z3"}, {"3", "Z1"}});
    l.poser("Porte_NON", "U2", {{"1", "Z1"}, {"2", "Z2"}});
    l.poser("Porte_NON", "U3", {{"1", "Z2"}, {"2", "Z3"}});
    SimulateurLogique s(circuitLogique(l.c));
    s.forcer("EN", Niveau::Bas);
    s.avancer(100e-9);
    CHECK(s.niveau("Z3") == Niveau::Haut);
    s.forcer("EN", Niveau::Haut);
    s.avancer(1e-6);
    std::vector<double> fronts;
    const int z1 = s.circuit().signal("Z1");
    for (const auto& ch : s.trace())
      if (ch.signal == z1 && ch.v == Niveau::Haut && ch.t > 100e-9) fronts.push_back(ch.t);
    REQUIRE(fronts.size() >= 5);
    CHECK(fronts[2] - fronts[1] == Catch::Approx(60e-9).epsilon(1e-6));
  }
  {
    // Bascule D rebouclée sur Q barre : divise l'horloge par deux
    Logique l;
    l.poser("Bascule_D", "U1", {{"1", "D"}, {"2", "CLK"}, {"3", "Q"}, {"4", "D"}});
    SimulateurLogique s(circuitLogique(l.c));
    s.forcer("CLK", Niveau::Bas);
    s.avancer(10e-9);
    int fronts = 0;
    for (int i = 0; i < 8; ++i) {
      s.forcer("CLK", Niveau::Haut);
      s.avancer(s.maintenant() + 50e-9);
      s.forcer("CLK", Niveau::Bas);
      s.avancer(s.maintenant() + 50e-9);
    }
    // Q part de X : la bascule ne se cale qu'avec un D connu ; on la cale par une première valeur
    const int q = s.circuit().signal("Q");
    for (const auto& ch : s.trace()) fronts += ch.signal == q && ch.v == Niveau::Haut;
    CHECK(fronts == 0);  // D = Q barre inconnu au départ : rien ne se décide (quatre états)
    SimulateurLogique t(circuitLogique(l.c));
    t.forcer("CLK", Niveau::Bas);
    t.forcer("D", Niveau::Bas);  // valeur imposée au démarrage, comme une remise à zéro
    t.avancer(10e-9);
    t.forcer("CLK", Niveau::Haut);
    t.avancer(t.maintenant() + 50e-9);
    CHECK(t.niveau("Q") == Niveau::Bas);
    const auto c = verilogStructurel(circuitLogique(l.c), "diviseur");
    CHECK(c.find("always @(posedge CLK)") != std::string::npos);
  }
}

TEST_CASE("15.11 · Symbole de FPGA tiré de son fichier de brochage : une unité par banc", "[L15][fpga]") {
  const std::string csv =
      "Pin,Name,Bank,Type\n"
      "A1,IO_L1P_0,0,io\nA2,IO_L1N_0,0,io\nA3,IO_5_0,0,io\n"
      "B1,IO_L2N_1,1,io\nB2,IO_L2P_1,1,io\nB3,CLK_1,1,input\n"
      "C1,IO_3,2,io\n"
      "D1,VCCINT,,power\nD2,GND,,ground\nD3,VCCO_0,0,power\nD3,DOUBLE,0,io\n";
  std::vector<std::string> alertes;
  const DefSymbole s = symboleDepuisBrochageFpga(csv, "fpga:essai", "FPGA d'essai", &alertes);
  CHECK(s.unites == 4);
  CHECK(s.nomsUnites == std::vector<std::string>{"Banc 0", "Banc 1", "Banc 2", "Alimentations"});
  CHECK(s.broches.size() == 10);
  CHECK(alertes.size() == 1);  // D3 en double
  // Paire différentielle côte à côte, P avant N
  std::vector<std::string> banc1;
  for (const auto& b : s.broches)
    if (b.unite == 2) banc1.push_back(b.nom);
  REQUIRE(banc1.size() == 3);
  const auto p = std::find(banc1.begin(), banc1.end(), "IO_L2P_1"), n = std::find(banc1.begin(), banc1.end(), "IO_L2N_1");
  REQUIRE(p != banc1.end());
  CHECK(n == p + 1);
  for (const auto& b : s.broches) {
    if (b.nom == "CLK_1") CHECK(b.type == TypeBroche::Entree);
    if (b.nom == "GND" || b.nom == "VCCO_0") CHECK(b.unite == 4);
  }
}
