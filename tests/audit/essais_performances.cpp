// Essais de l'audit de Prométhée 0.25 : mesures de performance et essais de robustesse aléatoires (pièces).
#include <random>
#include <sstream>

#include "aides.hpp"

using namespace verif;

TEST_CASE("P-01 temps de reconstruction en fonction du nombre de fonctions", "[perf]") {
  std::ostringstream j;
  for (int n : {5, 10, 20, 40}) {
    Bloc b = bloc(400, 100, 20);
    for (int k = 0; k < n; ++k) {
      const int ic = esquisseCercles(b.p, b.face("sommet"), {{10.0 + 9.5 * k, 50, 3}});
      FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ic; x.enlever = true; x.longueur = 10; x.inverse = true;
      b.p.ajouter(x);
    }
    Chrono c; const ResultatPiece r = construirePiece(b.p); const double t = c.ms();
    j << n << " poches (" << b.p.fonctions.size() << " fonctions) : " << t << " ms, " << r.erreurs.size() << " erreur(s)\n";
  }
  WARN(j.str());
  SUCCEED();
}

TEST_CASE("P-02 congé sur de nombreuses arêtes", "[perf]") {
  Bloc b = bloc(100, 100, 20);
  FonctionPiece s; s.type = TypeFonction::Esquisse; s.face = b.face("sommet");
  for (int i = 0; i < 5; ++i) for (int j = 0; j < 5; ++j) s.esquisse.ajouterRectangle(8 + 18 * i, 8 + 18 * j, 18 + 18 * i, 18 + 18 * j);
  const int is = b.p.ajouter(s);
  const int ix = extrusion(b.p, is, 10);
  ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  FonctionPiece c; c.type = TypeFonction::Conge; c.rayon = 1;
  for (TopExp_Explorer ex(r.forme, TopAbs_EDGE); ex.More(); ex.Next()) {
    const std::string n = r.nomArete(TopoDS::Edge(ex.Current()));
    if (n.find(std::to_string(ix) + ":sommet") != std::string::npos && n.find(std::to_string(ix) + ":cote") != std::string::npos) c.references.push_back(n);
  }
  const size_t nb = c.references.size();
  const int ic = b.p.ajouter(c);
  Chrono ch; r = construirePiece(b.p); const double t = ch.ms();
  Chrono ch2; for (int k = 0; k < 20; ++k) (void)r.aretes(c.references[static_cast<size_t>(k) % nb]); const double t2 = ch2.ms();
  WARN("congé R1 sur " << nb << " arêtes (25 plots) : reconstruction " << t << " ms ; erreur « " << r.erreur(ic) << " » ; 20 résolutions de références d'arêtes : " << t2 << " ms");
  CHECK(r.erreur(ic).empty());
}

TEST_CASE("P-03 essais aléatoires : bossages, poches, congés — exceptions et solides invalides", "[perf]") {
  std::mt19937 g(12345);
  std::uniform_real_distribution<double> u(0, 1);
  int invalides = 0, exceptions = 0, erreursFonction = 0, total = 0, vides = 0;
  std::ostringstream exemples;
  for (int essai = 0; essai < 60; ++essai) {
    Bloc b = bloc(40 + 60 * u(g), 30 + 50 * u(g), 5 + 20 * u(g));
    const int nf = 2 + static_cast<int>(u(g) * 6);
    for (int k = 0; k < nf; ++k) {
      const double kind = u(g);
      if (kind < 0.4) {
        const int ic = esquisseCercles(b.p, b.face("sommet"), {{5 + 30 * u(g), 5 + 25 * u(g), 1 + 6 * u(g)}});
        FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ic; x.enlever = u(g) < 0.5; x.longueur = 1 + 15 * u(g); x.inverse = x.enlever;
        b.p.ajouter(x);
      } else if (kind < 0.7) {
        FonctionPiece e; e.type = TypeFonction::Esquisse; e.face = b.face("sommet");
        const double x0 = 30 * u(g), y0 = 25 * u(g);
        e.esquisse.ajouterRectangle(x0, y0, x0 + 3 + 15 * u(g), y0 + 3 + 15 * u(g));
        const int ie = b.p.ajouter(e);
        FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ie; x.enlever = u(g) < 0.5; x.longueur = 1 + 10 * u(g); x.inverse = x.enlever; x.depouille = u(g) < 0.3 ? 3 : 0;
        b.p.ajouter(x);
      } else {
        FonctionPiece c; c.type = TypeFonction::Conge; c.rayon = 0.5 + 3 * u(g);
        c.references = {b.areteVerticale(static_cast<int>(u(g) * 4) % 4)};
        b.p.ajouter(c);
      }
    }
    ++total;
    try {
      const ResultatPiece r = construirePiece(b.p);
      if (!r.erreurs.empty()) ++erreursFonction;
      if (r.forme.IsNull() || nombreSolides(r.forme) == 0) ++vides;
      else if (!defautGeometrie(r.forme).empty()) { ++invalides; exemples << "essai " << essai << " : " << defautGeometrie(r.forme) << "\n"; }
    } catch (...) { ++exceptions; }
  }
  WARN(total << " pièces aléatoires : " << exceptions << " exception(s) non rattrapée(s), " << invalides << " solide(s) invalide(s) acceptés, " << erreursFonction << " pièce(s) avec une fonction en échec (signalée), " << vides << " vide(s)\n" << exemples.str());
  CHECK(exceptions == 0);
  CHECK(invalides == 0);
}
