// Tests unitaires du noyau. Licence GPL-3.0-only.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>

#include "promethee/derive.hpp"
#include "promethee/modele.hpp"
#include "promethee/verifs.hpp"

using namespace prom;
using Catch::Approx;

TEST_CASE("Les nombres s'affichent au format français, comme dans le prototype") {
  CHECK(fmt(1.25, 2) == "1,25");
  CHECK(fmt(-0.04) == "0");
  CHECK(fmt(60) == "60");
  CHECK(fmt(2.5, 0) == "3");
  CHECK(fmt(-2.5, 0) == "\u22122");
  CHECK(fmt(0.15) == "0,2");
  CHECK(fmt(12.0, 2) == "12");
  CHECK(fmt(std::numeric_limits<double>::quiet_NaN()) == "\u2013");
}

TEST_CASE("Distance signée à un rectangle arrondi") {
  CHECK(sdRR(0, 0, 0, 0, 10, 5, 1) == Approx(-5));
  CHECK(sdRR(12, 0, 0, 0, 10, 5, 1) == Approx(2));
  CHECK(sdRR(10, 5, 0, 0, 10, 5, 2) == Approx(std::hypot(2.0, 2.0) - 2));
}

TEST_CASE("Un trou proche d'un coin suit ce coin, au-delà de 15 mm il devient libre") {
  Projet p = exemple();
  Trou t;
  ancrerTrou(p, t, -25.5, -15.5);
  REQUIRE(t.coin == std::optional<std::string>("SW"));
  CHECK(t.ox == Approx(4.5));
  p.carte.L = 80;  // la carte s'allonge : le trou garde sa distance au coin
  const Point q = posTrou(p, t);
  CHECK(q.x == Approx(-35.5));
  ancrerTrou(p, t, 0, 0);
  CHECK_FALSE(t.coin.has_value());
  CHECK(posTrou(p, t).x == Approx(0));
}

TEST_CASE("Un connecteur de bord se place sur le bord le plus proche, sans dépasser des angles") {
  const Projet p = exemple();
  Composant k = p.composants[0];
  placerSurBord(p, k, 29, 3);
  CHECK(k.bord == Bord::E);
  CHECK(k.le_long == Approx(3));
  placerSurBord(p, k, 5, 25);
  CHECK(k.bord == Bord::N);
  // Loin dans l'angle : le bord le plus proche gagne et la position est bornée pour rester sur la carte.
  placerSurBord(p, k, 40, 100);
  CHECK(k.bord == Bord::E);
  CHECK(k.le_long == Approx(20 - k.w / 2 - p.carte.r));
}

TEST_CASE("Longueur de vis normalisée") {
  CHECK(longueurVis(1.6, vis("M3")) == 6);
  CHECK(longueurVis(1.6, vis("M2")) == 5);
  CHECK(longueurVis(1.6, vis("inconnue")) == 6);
}

TEST_CASE("Le boîtier découle de la carte") {
  Projet p = exemple();
  Derive d = deriver(p);
  CHECK(d.Li == Approx(62));
  CHECK(d.Lo == Approx(66));
  CHECK(d.H == Approx(18));
  CHECK(d.zt == Approx(20));
  CHECK(verifier(p, d).empty());
  // Un composant plus haut fait grandir le boîtier tout seul.
  for (auto& k : p.composants) if (k.ref == "C1") k.h = 16;
  d = deriver(p);
  CHECK(d.H > 18);
  CHECK(verifier(p, d).size() == 1);  // le bouton n'atteint plus le couvercle : une alerte, pas une erreur
  CHECK(verifier(p, d)[0].sev == Severite::Alerte);
}

TEST_CASE("Enregistrer puis relire un projet donne le même projet") {
  const auto chemin = (std::filesystem::temp_directory_path() / "promethee_essai.prom.json").string();
  const Projet p = exemple();
  ecrireProjet(p, chemin);
  const Projet q = lireProjet(chemin);
  CHECK(versJson(q) == versJson(p));
  std::filesystem::remove(chemin);
}

TEST_CASE("Un fichier illisible donne un message clair") {
  const auto chemin = (std::filesystem::temp_directory_path() / "promethee_casse.json").string();
  { std::ofstream f(chemin); f << "pas du json"; }
  CHECK_THROWS_WITH(lireProjet(chemin), Catch::Matchers::ContainsSubstring("n\u2019est pas un fichier JSON valide"));
  { std::ofstream f(chemin); f << "{\"autre\": 1}"; }
  CHECK_THROWS_WITH(lireProjet(chemin), Catch::Matchers::ContainsSubstring("n\u2019est pas un projet"));
  std::filesystem::remove(chemin);
  CHECK_THROWS(lireProjet("/chemin/qui/n/existe/pas.prom.json"));
}

TEST_CASE("Les identifiants générés sont uniques") {
  std::vector<std::string> ids;
  for (int i = 0; i < 2000; ++i) ids.push_back(nouvelId("c", ids));
  std::sort(ids.begin(), ids.end());
  CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
}
