// Prométhée : moindres carrés creux (Cholesky à profil, Levenberg-Marquardt, analyse de rang) et tenue en temps du
// solveur d'esquisse sur des esquisses moyennes (ES-18, ES-37). Licence GPL-3.0-only.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>

#include "promethee/esquisse.hpp"
#include "promethee/moindres_carres.hpp"

using namespace prom;
using namespace prom::numerique;
using Catch::Approx;

TEST_CASE("Cholesky à profil : solution d'un système creux mal numéroté") {
  // Chaîne 0-5-1-4-2-3 : bande large dans la numérotation d'origine, étroite après Cuthill-McKee
  const int n = 6;
  const int chaine[] = {0, 5, 1, 4, 2, 3};
  MatriceCreuse J;
  for (int k = 0; k + 1 < n; ++k) {
    const int a = std::min(chaine[k], chaine[k + 1]), b = std::max(chaine[k], chaine[k + 1]);
    J.push_back({{a, 1.0 + k}, {b, -2.0}});
  }
  for (int i = 0; i < n; ++i) J.push_back({{i, 0.5}});
  CholeskyProfil c;
  c.structure(J, n);
  std::vector<double> diag;
  c.assemblerNormale(J, diag);
  REQUIRE(c.factoriser(std::vector<double>(n, 0.0)));
  const std::vector<double> b = {1, -2, 3, 0.5, 4, -1};
  std::vector<double> x;
  c.resoudre(b, x);
  // Vérification : JᵀJ x = b
  std::vector<double> Ax(n, 0.0);
  for (const auto& l : J) {
    double s = 0;
    for (const auto& [j, v] : l) s += v * x[static_cast<size_t>(j)];
    for (const auto& [j, v] : l) Ax[static_cast<size_t>(j)] += v * s;
  }
  for (int i = 0; i < n; ++i) CHECK(Ax[static_cast<size_t>(i)] == Approx(b[static_cast<size_t>(i)]).margin(1e-12));
  CHECK(c.occupation() <= 2u * n);  // profil de bande 1 après renumérotation
}

TEST_CASE("Cholesky à profil : une matrice singulière est refusée") {
  MatriceCreuse J = {{{0, 1.0}, {1, -1.0}}};
  CholeskyProfil c;
  c.structure(J, 2);
  std::vector<double> diag;
  c.assemblerNormale(J, diag);
  CHECK_FALSE(c.factoriser({0.0, 0.0}));
  CHECK(c.factoriser({1e-9, 1e-9}));
}

TEST_CASE("Analyse de rang : rang, ligne redondante (la plus récente), paramètres libres et noyau") {
  // x0 = 0 ; x0 + x1 = 0 ; 2·x0 + 2·x1 = 0 (redondante) ; x2 libre
  MatriceCreuse J = {{{0, 1.0}}, {{0, 1.0}, {1, 1.0}}, {{0, 2.0}, {1, 2.0}}};
  const AnalyseRang a = analyserRang(J, 3, true);
  CHECK(a.rang == 2);
  REQUIRE(a.lignesDependantes.size() == 1);
  CHECK(a.lignesDependantes[0] == 2);
  CHECK_FALSE(a.parametreLibre[0]);
  CHECK_FALSE(a.parametreLibre[1]);
  CHECK(a.parametreLibre[2]);
  REQUIRE(a.noyau.size() == 1);
  CHECK(a.noyau[0][2] == Approx(1));
  CHECK(std::abs(a.noyau[0][0]) < 1e-12);
}

namespace {
class Rosenbrock final : public ProblemeMoindresCarres {
 public:
  int dimension() const override { return 2; }
  void residus(const std::vector<double>& x, std::vector<double>& F) override { F = {10 * (x[1] - x[0] * x[0]), 1 - x[0]}; }
  void jacobienne(const std::vector<double>& x, MatriceCreuse& J) override { J = {{{0, -20 * x[0]}, {1, 10.0}}, {{0, -1.0}}}; }
};
}  // namespace

TEST_CASE("Levenberg-Marquardt creux : Rosenbrock") {
  Rosenbrock r;
  std::vector<double> x = {-1.2, 1.0};
  CHECK(levenbergMarquardt(r, x) < 1e-20);
  CHECK(x[0] == Approx(1).margin(1e-9));
  CHECK(x[1] == Approx(1).margin(1e-9));
}

TEST_CASE("Esquisse de 100 rectangles cotés : glisser et diagnostic restent interactifs (ES-18, ES-37)") {
  Esquisse e;
  int coin = 0;
  for (int i = 0; i < 10; ++i)
    for (int j = 0; j < 10; ++j) {
      const auto l = e.ajouterRectangle(i * 30, j * 30, i * 30 + 20, j * 30 + 10);
      const int p1 = e.element(l[0])->p1;
      e.contraindre(TypeContrainte::DistanceX, {0, p1}, i * 30);
      e.contraindre(TypeContrainte::DistanceY, {0, p1}, j * 30);
      e.contraindre(TypeContrainte::Longueur, {l[0]}, 20);
      coin = e.element(l[1])->p2;
    }
  REQUIRE(resoudre(e).resolue);
  const auto t0 = std::chrono::steady_clock::now();
  const EtatEsquisse s = resoudre(e, {{coin, Point{e.point(coin)->x + 1, e.point(coin)->y + 3}}});
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(s.resolue);
  CHECK(s.degresLiberte == 100);  // une hauteur libre par rectangle
  CHECK(e.point(coin)->x == Approx(9 * 30 + 20).margin(1e-6));  // la largeur cotée tient : seul y suit la souris
  CHECK(ms < 50);
  const auto t1 = std::chrono::steady_clock::now();
  const DiagnosticEsquisse d = diagnostiquer(e);
  CHECK(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() < 200);
  CHECK(d.degresLiberte == 100);
  CHECK(d.libertes.size() == 100);
}

TEST_CASE("Composante insoluble : seule elle reste à sa géométrie d'avant, les autres sont résolues") {
  Esquisse e;
  const auto a = e.ajouterRectangle(0, 0, 20, 10);
  const auto b = e.ajouterRectangle(50, 0, 70, 10);
  e.contraindre(TypeContrainte::Longueur, {a[0]}, 25);  // soluble
  e.contraindre(TypeContrainte::Longueur, {b[0]}, 30);
  e.contraindre(TypeContrainte::Longueur, {b[2]}, 10);  // en conflit avec la précédente (rectangle)
  const int pb = e.element(b[0])->p2;
  const double xAvant = e.point(pb)->x;
  const EtatEsquisse s = resoudre(e);
  CHECK_FALSE(s.resolue);
  CHECK_FALSE(s.enConflit.empty());
  CHECK(e.point(pb)->x == Approx(xAvant));
  const auto* l = e.element(a[0]);
  CHECK(std::hypot(e.point(l->p2)->x - e.point(l->p1)->x, e.point(l->p2)->y - e.point(l->p1)->y) == Approx(25).margin(1e-6));
}
