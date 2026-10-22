// Prométhée : opérations de structure sur les pièces et assemblages (rangs tenus à jour, lecture validée).
// Licence GPL-3.0-only.
#include <catch2/catch_test_macros.hpp>

#include "promethee/modele.hpp"
#include "promethee/piece.hpp"
#include "promethee/structure.hpp"

using namespace prom;

namespace {

Json pieceNommee(const std::string& nom) {
  Piece p;
  p.nom = nom;
  return pieceVersJson(p);
}

ComposantAssemblage composantPiece(int piece, bool fixe = false) {
  ComposantAssemblage c;
  c.piece = piece;
  c.fixe = fixe;
  return c;
}

ComposantAssemblage composantSous(int assemblage) {
  ComposantAssemblage c;
  c.assemblage = assemblage;
  return c;
}

ContrainteAssemblage contrainte(int a, int b, const std::string& type = "coincidence") {
  ContrainteAssemblage c;
  c.type = type;
  c.a = a;
  c.b = b;
  return c;
}

// Projet de l'essai AS-01 : Essai (inutilisée), Plaque, Cube ; « Ensemble » : Plaque fixe, Cube posé dessus
Projet projetAS01() {
  Projet p = projetVide();
  p.pieces = {pieceNommee("Essai"), pieceNommee("Plaque"), pieceNommee("Cube")};
  Assemblage a;
  a.nom = "Ensemble";
  a.composants = {composantPiece(1, true), composantPiece(2)};
  a.contraintes = {contrainte(0, 1)};
  p.assemblages = {a};
  return p;
}

}  // namespace

TEST_CASE("AS-01 supprimer une pièce inutilisée garde les pièces des assemblages", "[structure][AS-01]") {
  Projet p = projetAS01();
  CHECK(assemblagesUtilisantPiece(p, 0).empty());
  const BilanRetrait b = supprimerPiece(p, 0);
  CHECK(b.composants == 0);
  REQUIRE(p.pieces.size() == 2);
  const auto& a = p.assemblages[0];
  REQUIRE(a.composants.size() == 2);
  CHECK(p.pieces[static_cast<size_t>(a.composants[0].piece)]["nom"] == "Plaque");
  CHECK(p.pieces[static_cast<size_t>(a.composants[1].piece)]["nom"] == "Cube");
  CHECK(a.contraintes.size() == 1);
}

TEST_CASE("AS-01 supprimer une pièce utilisée retire ses composants et leurs contraintes", "[structure][AS-01]") {
  Projet p = projetAS01();
  REQUIRE(assemblagesUtilisantPiece(p, 1) == std::vector<int>{0});
  const BilanRetrait b = supprimerPiece(p, 1);  // la Plaque
  CHECK(b.composants == 1);
  CHECK(b.contraintes == 1);
  const auto& a = p.assemblages[0];
  REQUIRE(a.composants.size() == 1);
  CHECK(p.pieces[static_cast<size_t>(a.composants[0].piece)]["nom"] == "Cube");
  CHECK(a.contraintes.empty());
}

TEST_CASE("AS-02 supprimer un assemblage décale les sous-assemblages des autres", "[structure][AS-02]") {
  Projet p = projetVide();
  p.pieces = {pieceNommee("Vis")};
  Assemblage brouillon, ensemble, sous;
  brouillon.nom = "Brouillon";
  ensemble.nom = "Ensemble";
  sous.nom = "Sous-ensemble";
  sous.composants = {composantPiece(0, true)};
  ensemble.composants = {composantPiece(0, true), composantSous(2)};
  p.assemblages = {brouillon, ensemble, sous};
  std::string raison;
  // Le sous-ensemble est utilisé : refus, avec le nom de l'assemblage qui l'utilise
  CHECK_FALSE(supprimerAssemblage(p, 2, &raison));
  CHECK(raison.find("Ensemble") != std::string::npos);
  // Le brouillon n'est utilisé nulle part : supprimé, et « Ensemble » vise toujours « Sous-ensemble »
  REQUIRE(supprimerAssemblage(p, 0, &raison));
  REQUIRE(p.assemblages.size() == 2);
  const auto& e = p.assemblages[0];
  REQUIRE(e.nom == "Ensemble");
  CHECK(p.assemblages[static_cast<size_t>(e.composants[1].assemblage)].nom == "Sous-ensemble");
}

TEST_CASE("AS-02 parcours sûrs devant une boucle de sous-assemblages", "[structure][AS-02]") {
  Projet p = projetVide();
  p.pieces = {pieceNommee("Vis")};
  Assemblage a, b;
  a.nom = "A";
  b.nom = "B";
  a.composants = {composantPiece(0, true), composantSous(1)};
  b.composants = {composantPiece(0, true), composantSous(0)};
  p.assemblages = {a, b};
  CHECK(contientAssemblage(p, 0, 1));
  CHECK(contientAssemblage(p, 1, 0));
  CHECK_FALSE(contientAssemblage(p, 0, 5));
  const auto q = quantitesPieces(p, 0);  // se termine, chaque niveau compté une fois
  REQUIRE(q.size() == 1);
  CHECK(q[0].second == 2);
  // La relecture coupe la boucle et le dit
  std::vector<std::string> alertes;
  const Projet r = normaliser(versJson(p), &alertes);
  CHECK_FALSE(alertes.empty());
  CHECK_FALSE((contientAssemblage(r, 0, 1) && contientAssemblage(r, 1, 0)));
}

TEST_CASE("AS-03 créer un sous-assemblage dans un projet à un seul assemblage", "[structure][AS-03]") {
  for (int n : {1, 2, 3, 4}) {
    Projet p = projetVide();
    p.pieces = {pieceNommee("Plaque"), pieceNommee("Cube")};
    for (int i = 0; i < n; ++i) {
      Assemblage a;
      a.nom = "A" + std::to_string(i);
      a.composants = {composantPiece(0, true), composantPiece(1), composantPiece(1), composantPiece(1)};
      a.contraintes = {contrainte(0, 1), contrainte(1, 2), contrainte(2, 3)};
      p.assemblages.push_back(a);
    }
    p = normaliser(versJson(p));  // tableau rempli par push_back, comme à l'ouverture
    REQUIRE(p.assemblages.size() == static_cast<size_t>(n));
    const std::set<int> choix{1, 2};
    CHECK(contraintesCoupees(p.assemblages[0], choix) == std::vector<int>{0, 2});
    const int rang = creerSousAssemblage(p, 0, choix);
    REQUIRE(rang == n);
    const auto& parent = p.assemblages[0];
    INFO(n << " assemblage(s)");
    REQUIRE(parent.composants.size() == 3);  // Plaque, Cube 3, sous-assemblage
    CHECK(parent.composants.back().assemblage == rang);
    // Les deux contraintes coupées sont gardées : elles visent la face par son chemin dans le sous-assemblage (AS-16)
    REQUIRE(parent.contraintes.size() == 2);
    CHECK(parent.contraintes[0].a == 0);
    CHECK(parent.contraintes[0].b == 2);
    CHECK(parent.contraintes[0].refB.rfind("@0/", 0) == 0);
    CHECK(parent.contraintes[1].a == 2);
    CHECK(parent.contraintes[1].refA.rfind("@1/", 0) == 0);
    CHECK(parent.contraintes[1].b == 1);
    const auto& sous = p.assemblages[static_cast<size_t>(rang)];
    CHECK(sous.composants.size() == 2);
    REQUIRE(sous.contraintes.size() == 1);
    CHECK(sous.contraintes[0].a == 0);
    CHECK(sous.contraintes[0].b == 1);
  }
}

TEST_CASE("AS-16 un sous-assemblage qui emporte la référence fixe prend sa place", "[structure][AS-16]") {
  Projet p = projetVide();
  p.pieces = {pieceNommee("Plaque"), pieceNommee("Cube")};
  Assemblage a;
  a.composants = {composantPiece(0, true), composantPiece(1), composantPiece(1)};
  a.contraintes = {contrainte(1, 2)};
  p.assemblages = {a};
  const int rang = creerSousAssemblage(p, 0, {0, 1});
  REQUIRE(rang == 1);
  const auto& parent = p.assemblages[0];
  REQUIRE(parent.composants.size() == 2);
  CHECK(parent.composants[0].assemblage == 1);  // le sous-assemblage devient la référence fixe
  CHECK(parent.composants[0].fixe);
  CHECK(parent.composants[1].piece == 1);
  CHECK_FALSE(parent.composants[1].fixe);
}

TEST_CASE("AS-27 retirer un composant renumérote les contraintes", "[structure][AS-27]") {
  Assemblage a;
  a.composants = {composantPiece(0, true), composantPiece(0), composantPiece(0), composantPiece(0)};
  a.contraintes = {contrainte(0, 1), contrainte(2, 3), contrainte(1, 3)};
  const BilanRetrait b = retirerComposant(a, 1);
  CHECK(b.contraintes == 2);
  REQUIRE(a.contraintes.size() == 1);
  CHECK(a.contraintes[0].a == 1);
  CHECK(a.contraintes[0].b == 2);
}

TEST_CASE("AS-28 AS-32 relecture d'assemblages abîmés", "[structure][AS-28][AS-32]") {
  Projet p = projetAS01();
  Json j = versJson(p);
  j["assemblages"][0]["composants"][1]["x"] = "12";
  j["assemblages"][0]["composants"][1]["rz"] = nullptr;  // forme sous laquelle un NaN est écrit
  j["assemblages"][0]["composants"].push_back({{"piece", -1}});
  j["assemblages"][0]["composants"].push_back({{"piece", 0}, {"assemblage", 7}});
  j["assemblages"][0]["contraintes"].push_back({{"type", "came"}, {"a", 0}, {"b", 1}});
  j["assemblages"][0]["contraintes"].push_back({{"type", "coincidence"}, {"a", 0}, {"b", 9}});
  std::vector<std::string> alertes;
  Projet q;
  REQUIRE_NOTHROW(q = normaliser(j, &alertes));
  const auto& a = q.assemblages[0];
  CHECK(a.composants.size() == 2);       // pièce −1 et sous-assemblage 7 retirés
  CHECK(a.composants[1].x == 0);          // valeur illisible : défaut
  REQUIRE(a.contraintes.size() == 2);    // la contrainte vers le composant 9 est écartée
  CHECK(a.contraintes[1].type == "came");  // type inconnu gardé tel quel
  CHECK_FALSE(typeContrainteAssemblageConnu("came"));
  CHECK(alertes.size() >= 5);
}

TEST_CASE("AP-02 identifiants de pièces uniques à la lecture", "[structure][AP-02]") {
  Projet p = projetVide();
  Json a = pieceNommee("A"), b = pieceNommee("B"), c = pieceNommee("C");
  a["id"] = "piece2";
  b["id"] = "piece2";
  c.erase("id");
  p.pieces = {a, b, c};
  std::vector<std::string> alertes;
  const Projet q = normaliser(versJson(p), &alertes);
  std::set<std::string> ids;
  for (const auto& x : q.pieces) ids.insert(x["id"].get<std::string>());
  CHECK(ids.size() == 3);
  CHECK(q.pieces[0]["id"] == "piece2");
  // Relire une seconde fois ne change plus rien
  CHECK(versJson(normaliser(versJson(q))) == versJson(q));
}

#include "promethee/esquisse.hpp"

TEST_CASE("EI-02 une référence de point absente est refusée et ignorée", "[structure][EI-02]") {
  Esquisse e;
  const int a = e.ajouterPoint(10, 10), b = e.ajouterPoint(40, 10);
  const int l = e.ajouterLigne(a, b);
  REQUIRE(l > 0);
  CHECK(e.ajouterLigne(a, 999) == 0);
  CHECK(e.ajouterArc(999, a, b) == 0);
  CHECK(e.ajouterCercle(999, 5) == 0);
  // Élément abîmé à la main (fichier, ancienne version) : les opérations l'ignorent sans lire un point absent
  ElementEsquisse x;
  x.id = e.prochainId++;
  x.type = TypeElement::Ligne;
  x.p1 = 998;
  x.p2 = b;
  e.elements.push_back(x);
  CHECK_FALSE(e.elementValide(x));
  CHECK_FALSE(e.ajuster(x.id, 20, 10));
  CHECK_FALSE(e.prolonger(x.id, 40, 10));
  CHECK(e.decaler({x.id}, 2).empty());
  CHECK_NOTHROW(diagnostiquer(e));
  CHECK_NOTHROW(e.ajuster(l, 20, 10));
}
