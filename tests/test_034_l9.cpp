// Prométhée : essais de la 0.34, lot L9 côté noyau — calcul numérique robuste (9.6 : orientation exacte en 128 bits,
// point sur le bord d'un trou, test de boîtes rétabli) et routeur d'appoint sur l'index (9.8).
// Licence GPL-3.0-only.
#include <chrono>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "promethee/circuit_edition.hpp"
#include "promethee/ecad.hpp"
#include "promethee/electronique.hpp"

using namespace prom::ecad;

TEST_CASE("9.6 · cas dégénérés : bord d'un trou, sommets, segments colinéaires, coordonnées extrêmes", "[034][L9][9.6]") {
  // Carré de 10 mm percé d'un trou carré de 4 mm (sens inverse)
  const Polygone ext{{0, 0}, {10'000'000, 0}, {10'000'000, 10'000'000}, {0, 10'000'000}};
  const Polygone trou{{3'000'000, 3'000'000}, {3'000'000, 7'000'000}, {7'000'000, 7'000'000}, {7'000'000, 3'000'000}};
  const Polygones region{ext, trou};
  CHECK(dedans(region, {1'000'000, 1'000'000}));
  CHECK_FALSE(dedans(region, {5'000'000, 5'000'000}));        // dans le trou
  CHECK(dedans(region, {3'000'000, 5'000'000}));              // sur le bord du trou : il touche le cuivre
  CHECK(dedans(region, {7'000'000, 7'000'000}));              // sommet du trou
  CHECK(dedans(region, {0, 0}));                              // sommet du contour
  CHECK(dedans(region, {10'000'000, 4'000'000}));             // bord du contour
  CHECK_FALSE(dedans(region, {10'000'001, 4'000'000}));
  // Polygone d'aire nulle (trois points alignés) : rien n'est dedans, sauf ses bords
  const Polygone plat{{0, 0}, {5, 5}, {10, 10}};
  CHECK_FALSE(dedans(plat, {3, 4}));
  // Segments colinéaires qui se recouvrent, qui se touchent bout à bout, parallèles disjoints
  CHECK(segmentsSeCoupent({0, 0}, {10, 0}, {5, 0}, {20, 0}));
  CHECK(segmentsSeCoupent({0, 0}, {10, 0}, {10, 0}, {20, 0}));
  CHECK_FALSE(segmentsSeCoupent({0, 0}, {10, 0}, {11, 0}, {20, 0}));
  CHECK_FALSE(segmentsSeCoupent({0, 0}, {10, 0}, {0, 3}, {10, 3}));
  // Coordonnées de l'ordre du mètre : le produit vectoriel dépasse la précision d'un double, le signe reste exact
  const Nm G = 1'000'000'000;
  CHECK(segmentsSeCoupent({0, 0}, {2 * G, 2 * G + 2}, {0, 2 * G}, {2 * G, 0}));
  CHECK_FALSE(segmentsSeCoupent({0, 0}, {G + 1, G}, {0, 3}, {G, G - 1 + 3}));
  const Polygone grand{{-G, -G}, {G, -G}, {G, G}, {-G, G}};
  CHECK(dedans(grand, {G, 0}));
  CHECK(dedans(grand, {G - 1, G - 1}));
  CHECK_FALSE(dedans(grand, {G + 1, 0}));
  // Test de boîtes : deux polygones loin l'un de l'autre, distance exacte
  const Polygone a{{0, 0}, {1'000'000, 0}, {1'000'000, 1'000'000}, {0, 1'000'000}};
  const Polygone b{{5'000'000, 0}, {6'000'000, 0}, {6'000'000, 1'000'000}, {5'000'000, 1'000'000}};
  CHECK(distancePolygones(a, b) == 4'000'000);
  const Polygone dansA{{200'000, 200'000}, {300'000, 200'000}, {300'000, 300'000}};
  CHECK(distancePolygones(a, dansA) == 0);
}

TEST_CASE("9.8 · routeur d'appoint sur l'index : 315 composants routés en quelques secondes", "[034][L9][9.8]") {
  ConceptionElec c = nouvelleConception("Appoint");
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(120);
  c.circuit.contour.hauteur = nm(100);
  std::vector<std::vector<std::string>> r(21, std::vector<std::string>(15));
  for (int i = 0; i < 21; ++i)
    for (int j = 0; j < 15; ++j) r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = poserEmpreinteSeule(c, "R_0603", ptMm(5 + 7.5 * j, 4 + 4.5 * i));
  REQUIRE(c.composants.size() == 315);
  for (int i = 0; i < 21; ++i)
    for (int j = 0; j + 1 < 15; ++j) {
      const std::string n = "N" + std::to_string(i) + "_" + std::to_string(j);
      c.nomsReseaux[r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] + ":2"] = n;
      c.nomsReseaux[r[static_cast<std::size_t>(i)][static_cast<std::size_t>(j + 1)] + ":1"] = n;
    }
  const auto t0 = std::chrono::steady_clock::now();
  const BilanRoutage b = routerSimple(c);
  const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  INFO("routage d'appoint : " << s << " s, " << b.liaisons << " liaison(s), " << b.restantes << " restante(s)");
  CHECK(b.restantes == 0);
  CHECK(b.liaisons == 21 * 14);
  CHECK(s < 10.0);
}
