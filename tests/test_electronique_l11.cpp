// Prométhée : essais du lot L11 (simulation). Licence GPL-3.0-only.
#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electronique.hpp"
#include "promethee/simulation.hpp"
#include "promethee/valeurs.hpp"

using namespace prom::ecad;

namespace {
// Montage d'essai : symboles posés en ligne, chaque broche reliée par une étiquette globale au réseau voulu
struct Montage {
  ConceptionElec c = nouvelleConception("Montage");
  double x = 25.4;
  void poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, 50.8));
    x += 25.4;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        ComposantElec* k = c.composant(p.composant);
        k->reference = ref;
        k->valeur = valeur;
        for (const auto& b : brochesPlacees(p, *c.symbole(p.symbole)))
          for (const auto& [num, reseau] : liaisons)
            if (b.broche->numero == num) {
              const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
              const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
              c.feuilles[0].fils.push_back({nouvelUuid(), b.p, bout});
              Etiquette e;
              e.uuid = nouvelUuid();
              e.texte = reseau;
              e.p = bout;
              e.type = TypeEtiquette::Globale;
              c.feuilles[0].etiquettes.push_back(e);
            }
      }
  }
};
Montage filtreRC() {
  Montage m;
  m.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  m.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
  return m;
}
}  // namespace

TEST_CASE("11.2 · Netlist SPICE tirée du schéma, contrôles avant le calcul", "[L11][netlist]") {
  const Montage m = filtreRC();
  AnalyseSpice a;
  a.type = AnalyseSpice::Type::Alternatif;
  const NetlistSpice n = netlistSpice(m.c, a);
  INFO(n.texte);
  CHECK(n.valide());
  CHECK(n.texte.find("RR1 n_entree n_sortie 1000") != std::string::npos);
  CHECK(n.texte.find("CC1 n_sortie 0 1e-07") != std::string::npos);
  CHECK(n.texte.find("VV1 n_entree 0 DC 0 AC 1") != std::string::npos);
  CHECK(n.texte.find(".ac dec 20 1 1000000") != std::string::npos);
  CHECK(vecteurTension(n, "SORTIE") == "v(n_sortie)");
  // Un nœud relié à la masse par un condensateur seul : signalé avant le calcul
  Montage f;
  f.poser("Source_tension", "V1", "5", {{"1", "A"}, {"2", "GND"}});
  f.poser("C", "C1", "1u", {{"1", "A"}, {"2", "FLOTTANT"}});
  f.poser("C", "C2", "1u", {{"1", "FLOTTANT"}, {"2", "GND"}});
  const NetlistSpice nf = netlistSpice(f.c, a);
  CHECK(!nf.valide());
  CHECK(nf.erreurs.front().find("FLOTTANT") != std::string::npos);
  // Sans masse
  Montage s;
  s.poser("R", "R1", "1k", {{"1", "A"}, {"2", "B"}});
  CHECK(!netlistSpice(s.c, a).valide());
  // L'exemple au NE555 : +5V reçoit une source implicite, la netlist est complète
  const NetlistSpice e = netlistSpice(exempleElectronique(), AnalyseSpice{});
  INFO(e.texte);
  CHECK(e.valide());
  CHECK(e.texte.find(".subckt NE555") != std::string::npos);
  CHECK(e.texte.find(" DC 5\n") != std::string::npos);
}

TEST_CASE("11.10, 11.16 · Sources en français, associations de valeurs normalisées", "[L11][calculs]") {
  CHECK(sourcePwl("0 V pendant 1 ms, puis rampe à 5 V en 10 µs") == "PWL(0 0 0.001 0 0.00101 5)");
  std::string e;
  CHECK(sourcePwl("n'importe quoi", &e).empty());
  CHECK(!e.empty());
  const CombinaisonE x = combinaisonE(12'340, 24);
  CHECK(x.ecart < 0.001);
  CHECK(x.valeur == Catch::Approx(12'340).epsilon(0.001));
  CHECK(dansSerie(x.a, 24));
  CHECK(dansSerie(x.b, 24));
}

TEST_CASE("11.3, 11.4 · ngspice : filtre RC à 1 %, astable à 555 à 5 %, arrêt d'un calcul", "[L11][ngspice]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  CHECK(!versionNgspice().empty());
  {
    const Montage m = filtreRC();
    AnalyseSpice a;
    a.type = AnalyseSpice::Type::Alternatif;
    a.fDebut = 10;
    a.fFin = 1e6;
    a.pointsParDecade = 50;
    const NetlistSpice n = netlistSpice(m.c, a);
    REQUIRE(n.valide());
    const ResultatSimulation r = simuler(n.texte);
    INFO(r.erreur);
    REQUIRE(r.reussi());
    const auto* f = r.reel("frequency");
    const auto* v = r.complexe(vecteurTension(n, "SORTIE"));
    REQUIRE(f);
    REQUIRE(v);
    const double fc = frequenceCoupure(*f, *v), attendue = 1 / (2 * std::numbers::pi * 1000 * 100e-9);
    CHECK(fc == Catch::Approx(attendue).epsilon(0.01));
    CHECK(valeurA(*f, gainDb(*v), attendue) == Catch::Approx(-3.01).margin(0.1));
  }
  {
    // Astable : R1 1k, R2 10k, C 10n ; f = 1,44 / ((R1 + 2 R2) C)
    Montage m;
    m.poser("Source_tension", "V1", "5", {{"1", "VCC"}, {"2", "GND"}});
    m.poser("NE555", "U1", "NE555", {{"8", "VCC"}, {"4", "VCC"}, {"1", "GND"}, {"7", "DIS"}, {"6", "THR"}, {"2", "THR"}, {"3", "SORTIE"}, {"5", "CV"}});
    m.poser("R", "R1", "1k", {{"1", "VCC"}, {"2", "DIS"}});
    m.poser("R", "R2", "10k", {{"1", "DIS"}, {"2", "THR"}});
    m.poser("C", "C1", "10n", {{"1", "THR"}, {"2", "GND"}});
    m.poser("C", "C2", "10n", {{"1", "CV"}, {"2", "GND"}});
    m.poser("R", "R3", "1k", {{"1", "SORTIE"}, {"2", "GND"}});
    AnalyseSpice a;
    a.type = AnalyseSpice::Type::Transitoire;
    a.pas = 0.5e-6;
    a.duree = 3e-3;
    const NetlistSpice n = netlistSpice(m.c, a);
    INFO(n.texte);
    REQUIRE(n.valide());
    const ResultatSimulation r = simuler(n.texte);
    INFO(r.erreur);
    REQUIRE(r.reussi());
    const auto* t = r.reel("time");
    const auto* v = r.reel(vecteurTension(n, "SORTIE"));
    REQUIRE(t);
    REQUIRE(v);
    const double f = frequenceMesuree(*t, *v), attendue = 1.44 / ((1000 + 2 * 10'000) * 10e-9);
    CHECK(f == Catch::Approx(attendue).epsilon(0.05));
  }
  {
    // Un calcul long s'arrête à la demande
    const Montage m = filtreRC();
    AnalyseSpice a;
    a.type = AnalyseSpice::Type::Transitoire;
    a.pas = 1e-9;
    a.duree = 10;
    std::atomic<bool> arret{false};
    std::thread t([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      arret = true;
    });
    const auto t0 = std::chrono::steady_clock::now();
    const ResultatSimulation r = simuler(netlistSpice(m.c, a).texte, &arret);
    t.join();
    CHECK(r.interrompu);
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 10);
  }
  {
    // La source décrite en français suit la même courbe qu'une PWL écrite à la main
    Montage m;
    m.poser("Source_tension", "V1", "0 V pendant 1 ms, puis rampe à 5 V en 10 µs", {{"1", "A"}, {"2", "GND"}});
    m.poser("R", "R1", "1k", {{"1", "A"}, {"2", "GND"}});
    AnalyseSpice a;
    a.pas = 1e-6;
    a.duree = 2e-3;
    const NetlistSpice n = netlistSpice(m.c, a);
    REQUIRE(n.valide());
    const ResultatSimulation r = simuler(n.texte);
    REQUIRE(r.reussi());
    const auto* t = r.reel("time");
    const auto* v = r.reel(vecteurTension(n, "A"));
    REQUIRE(t);
    REQUIRE(v);
    CHECK(valeurA(*t, *v, 0.5e-3) == Catch::Approx(0).margin(1e-6));
    CHECK(valeurA(*t, *v, 1.005e-3) == Catch::Approx(2.5).margin(0.05));
    CHECK(valeurA(*t, *v, 1.5e-3) == Catch::Approx(5).margin(1e-6));
  }
}
