// Prométhée : essais de la 0.34, lot L8 côté noyau — moteur de lignes de transmission (8.21). Les valeurs de référence
// sont celles du calculateur de KiCad 7.0 (pcb_calculator/transline : microruban, ruban, paire couplée), relevées sur
// les mêmes géométries ; l'écart doit rester sous 2 %.
// Licence GPL-3.0-only.
#include <cmath>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/circuit_edition.hpp"
#include "promethee/electronique.hpp"
#include "promethee/lignes_transmission.hpp"

using namespace prom::ecad;

namespace {
bool aDeuxPourCent(double v, double ref) { return std::abs(v - ref) <= 0.02 * std::abs(ref); }
}  // namespace

TEST_CASE("8.21 · microruban, ruban et paire couplée : écart sous 2 % avec le calculateur de KiCad", "[034][L8][8.21]") {
  struct Ms {
    double er, h, t, w, f, z0;
  };
  // (εr, h mm, t mm, w mm, f Hz) → Z0 Ω du calculateur
  for (const Ms& x : {Ms{4.5, 1.6, 0.035, 3.0, 1e9, 49.7322}, Ms{4.3, 0.2, 0.035, 0.35, 1e9, 51.4410}, Ms{3.66, 0.1, 0.018, 0.2, 5e9, 51.0274}, Ms{4.6, 0.36, 0.035, 0.15, 1e8, 94.6841}}) {
    const LigneTransmission l(GenreLigne::Microruban, {x.er, 0.02, 1e9, 1e3, 1e12, false});
    const ResultatLigne r = l.analyser({x.w * 1e-3, 0, x.h * 1e-3, 0, x.t * 1e-3, 0}, x.f);
    INFO("microruban w=" << x.w << " h=" << x.h << " : " << r.z0 << " Ω contre " << x.z0);
    CHECK(aDeuxPourCent(r.z0, x.z0));
    // Synthèse : la largeur retrouvée donne la même impédance
    const auto w = l.largeurPour(x.z0, {0, 0, x.h * 1e-3, 0, x.t * 1e-3, 0}, x.f);
    REQUIRE(w);
    CHECK(std::abs(*w * 1e3 - x.w) <= 0.02 * x.w);
  }
  struct Sl {
    double er, h, a, t, w, z0;
  };
  for (const Sl& x : {Sl{4.5, 0.5, 0.2325, 0.035, 0.15, 51.1065}, Sl{4.2, 1.0, 0.4825, 0.035, 0.3, 56.8078}, Sl{3.7, 0.4, 0.1, 0.018, 0.12, 52.9380}}) {
    const LigneTransmission l(GenreLigne::Ruban, {x.er, 0.02, 1e9, 1e3, 1e12, false});
    const ResultatLigne r = l.analyser({x.w * 1e-3, 0, x.h * 1e-3, x.a * 1e-3, x.t * 1e-3, 0}, 1e9);
    INFO("ruban w=" << x.w << " : " << r.z0 << " Ω contre " << x.z0);
    CHECK(aDeuxPourCent(r.z0, x.z0));
  }
  struct Cm {
    double er, h, t, w, s, f, zPair, zImpair, zDiff;
  };
  for (const Cm& x : {Cm{4.5, 0.2, 0.035, 0.15, 0.15, 1e9, 98.1010, 61.5630, 123.2347}, Cm{4.3, 0.1, 0.018, 0.1, 0.2, 1e9, 77.7265, 65.5167, 131.1437},
                      Cm{3.66, 0.2, 0.035, 0.3, 0.2, 2e9, 71.5571, 52.4535, 105.0709}}) {
    const LigneTransmission l(GenreLigne::MicrorubanCouple, {x.er, 0.02, 1e9, 1e3, 1e12, false});
    const ResultatLigne r = l.analyser({x.w * 1e-3, x.s * 1e-3, x.h * 1e-3, 0, x.t * 1e-3, 0}, x.f);
    INFO("paire w=" << x.w << " s=" << x.s << " : " << r.zPair << " / " << r.zImpair << " / " << r.zDiff);
    CHECK(aDeuxPourCent(r.zPair, x.zPair));
    CHECK(aDeuxPourCent(r.zImpair, x.zImpair));
    CHECK(aDeuxPourCent(r.zDiff, x.zDiff));
    // Synthèse de l'écart pour Zdiff
    const auto s = l.ecartPour(x.zDiff, {x.w * 1e-3, 0, x.h * 1e-3, 0, x.t * 1e-3, 0}, x.f);
    REQUIRE(s);
    CHECK(std::abs(*s * 1e3 - x.s) <= 0.05 * x.s);
  }
  // Paire en ruban (Cohn) : le mode impair sous la ligne seule, le mode pair au-dessus ; très écartée, les deux s'y rejoignent
  {
    const LigneTransmission seule(GenreLigne::Ruban, {4.4, 0.02, 1e9, 1e3, 1e12, false}), paire(GenreLigne::RubanCouple, {4.4, 0.02, 1e9, 1e3, 1e12, false});
    const double z = seule.analyser({0.15e-3, 0, 0.5e-3, 0, 0.035e-3, 0}, 1e9).z0;
    const ResultatLigne proche = paire.analyser({0.15e-3, 0.15e-3, 0.5e-3, 0, 0.035e-3, 0}, 1e9), loin = paire.analyser({0.15e-3, 5e-3, 0.5e-3, 0, 0.035e-3, 0}, 1e9);
    CHECK(proche.zImpair < z);
    CHECK(proche.zPair > z);
    CHECK(std::abs(loin.zImpair - z) < 0.01 * z);
    CHECK(std::abs(loin.zPair - z) < 0.01 * z);
  }
}

TEST_CASE("8.21 · pertes : rugosité de Huray et diélectrique de Djordjevic et Sarkar ; retard en picosecondes", "[034][L8][8.21]") {
  // Djordjevic et Sarkar : εr et tan δ retrouvés à la fréquence de référence, εr décroît avec la fréquence
  const Dielectrique d{4.3, 0.02, 1e9, 1e3, 1e12, true};
  CHECK(permittiviteA(d, 1e9) == Catch::Approx(4.3).epsilon(1e-9));
  CHECK(pertesA(d, 1e9) == Catch::Approx(0.02).epsilon(1e-6));
  CHECK(permittiviteA(d, 10e9) < permittiviteA(d, 1e9));
  CHECK(permittiviteA(d, 1e8) > permittiviteA(d, 1e9));
  // Huray : la rugosité augmente les pertes du conducteur, davantage à haute fréquence
  const Rugosite rug{0.5e-6, 10, 9.4e-12, 0};
  CHECK(facteurRugosite(rug, 1e8, 5.8e7) < facteurRugosite(rug, 10e9, 5.8e7));
  CHECK(facteurRugosite({}, 10e9, 5.8e7) == 1.0);
  const LigneTransmission lisse(GenreLigne::Microruban, d), rugueuse(GenreLigne::Microruban, d, rug);
  const GeometrieLigne g{0.35e-3, 0, 0.2e-3, 0, 0.035e-3, 0};
  CHECK(rugueuse.analyser(g, 10e9).pertesConducteur > 1.2 * lisse.analyser(g, 10e9).pertesConducteur);
  CHECK(lisse.analyser(g, 10e9).pertesDielectrique > lisse.analyser(g, 1e9).pertesDielectrique);
  // Retard : √εeff / c, soit 3,336 ps/mm dans l'air ; un ruban dans εr = 4 : 6,67 ps/mm
  const ResultatLigne r = LigneTransmission(GenreLigne::Ruban, {4.0, 0.02, 1e9, 1e3, 1e12, false}).analyser({0.15e-3, 0, 0.5e-3, 0, 0.035e-3, 0}, 1e9);
  CHECK(r.retard == Catch::Approx(6.6713).epsilon(1e-3));
}

TEST_CASE("8.21 · profils d'impédance de la carte : largeur par couche, suivie par le routeur au changement de couche ; retards", "[034][L8][8.21]") {
  ConceptionElec c = nouvelleConception("Quatre couches");
  c.circuit.empilage.couchesCuivre = 4;
  c.circuit.empilage.epaisseur = 1.6;
  ProfilImpedance p;
  p.nom = "Z50";
  p.impedance = 50;
  calculerProfil(c, p);
  // Quatre couches : deux microrubans et deux rubans, chacun avec sa largeur
  REQUIRE(p.parCouche.size() == 4);
  const Nm dessus = p.parCouche.at(codeCouche(Couche::CuivreDessus)).first, interne = p.parCouche.at(codeCouche(Couche::CuivreInt1)).first;
  CHECK(dessus > 0);
  CHECK(interne > 0);
  CHECK(interne < dessus);  // le ruban, enfermé entre deux plans, est plus étroit pour 50 Ω
  // Chaque largeur donne bien 50 Ω à 2 % près sur sa couche
  for (Couche k : {Couche::CuivreDessus, Couche::CuivreInt1}) {
    const LigneSurCouche L = ligneSurCouche(c.circuit.empilage, k);
    GeometrieLigne g = L.geometrie;
    g.largeur = mm(p.parCouche.at(codeCouche(k)).first) * 1e-3;
    CHECK(aDeuxPourCent(LigneTransmission(L.genre, L.dielectrique).analyser(g, 1e9).z0, 50));
  }
  // Le profil porte le nom d'une classe : ses réseaux en prennent la largeur, couche par couche
  ClasseReseau k;
  k.nom = "Z50";
  k.largeur = 300'000;
  c.circuit.regles.classes.push_back(k);
  c.circuit.regles.classeReseau["RF_IN"] = "Z50";
  c.circuit.regles.avancees.impedances.push_back(p);
  CHECK(largeurSurCouche(c, "RF_IN", Couche::CuivreDessus) == dessus);
  CHECK(largeurSurCouche(c, "RF_IN", Couche::CuivreInt1) == interne);
  CHECK(largeurSurCouche(c, "AUTRE", Couche::CuivreInt1) == c.circuit.regles.largeur);
  // Retards : plus lent dedans (ruban, εr) que dehors (microruban, εeff) ; via traversante de quelques ps
  CHECK(retardCouche(c, Couche::CuivreInt1, interne) > retardCouche(c, Couche::CuivreDessus, dessus));
  CHECK(retardVia(c, Couche::CuivreDessus, Couche::CuivreDessous) > 5);
  CHECK(retardVia(c, Couche::CuivreDessus, Couche::CuivreDessous) < 15);
  CHECK(psParMm(c, Couche::CuivreInt1, interne) == Catch::Approx(retardCouche(c, Couche::CuivreInt1, interne)).epsilon(1e-6));
}
