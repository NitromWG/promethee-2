// Essais de l'audit de Prométhée 0.25 : esquisse, noyau (constats ES-xx du rapport, et EI-13).
// Convention : chaque CHECK exprime le comportement CORRECT attendu ; un échec confirme le défaut.
#include <algorithm>
#include <limits>
#include <sstream>

#include "aides.hpp"
#include "promethee/unites.hpp"

using namespace verif;

namespace {
double longueurLigne(const Esquisse& e, int id) {
  const auto* l = e.element(id);
  const auto *a = e.point(l->p1), *b = e.point(l->p2);
  return std::hypot(b->x - a->x, b->y - a->y);
}
}  // namespace

TEST_CASE("ES-01 relecture JSON : valeurs bornées (plan -20, plan -30°, tube plein, 12 m)", "[ES-01]") {
  Piece p;
  FonctionPiece pl; pl.type = TypeFonction::Plan; pl.mode = "decale"; pl.supports = {"XY"}; pl.distance = -20; p.ajouter(pl);
  FonctionPiece pi; pi.type = TypeFonction::Plan; pi.mode = "angle"; pi.supports = {"XY", "X"}; pi.angle = -30; p.ajouter(pi);
  FonctionPiece tu; tu.type = TypeFonction::Balayage; tu.tube = true; tu.diametre = 10; tu.epaisseur = 0; p.ajouter(tu);
  FonctionPiece ex; ex.type = TypeFonction::Extrusion; ex.longueur = 12000; p.ajouter(ex);
  FonctionPiece pg; pg.type = TypeFonction::Plan; pg.mode = "decale"; pg.supports = {"XY"}; pg.distance = 1500; p.ajouter(pg);
  const Piece q = pieceDepuisJson(pieceVersJson(p));
  INFO("distance relue " << q.fonctions[0].distance << ", angle relu " << q.fonctions[1].angle << ", épaisseur tube relue " << q.fonctions[2].epaisseur
       << ", longueur relue " << q.fonctions[3].longueur << ", plan 1500 relu " << q.fonctions[4].distance);
  CHECK(q.fonctions[0].distance == -20);
  CHECK(q.fonctions[1].angle == -30);
  CHECK(q.fonctions[2].epaisseur == 0);
  CHECK(q.fonctions[3].longueur == 12000);
  CHECK(q.fonctions[4].distance == 1500);
}

TEST_CASE("ES-01b effet géométrique : plan décalé de -20 puis cylindre posé dessus, avant/après relecture", "[ES-01]") {
  Piece p;
  FonctionPiece pl; pl.type = TypeFonction::Plan; pl.mode = "decale"; pl.supports = {"XY"}; pl.distance = -20;
  const int ip = p.ajouter(pl);
  FonctionPiece e; e.type = TypeFonction::Esquisse; e.face = "F:" + std::to_string(ip);
  e.esquisse.ajouterCercle(e.esquisse.ajouterPoint(0, 0), 5);
  const int ie = p.ajouter(e);
  extrusion(p, ie, 10);
  const ResultatPiece r1 = construirePiece(p);
  const Piece q = pieceDepuisJson(pieceVersJson(p));
  const ResultatPiece r2 = construirePiece(q);
  REQUIRE_FALSE(r1.forme.IsNull());
  REQUIRE_FALSE(r2.forme.IsNull());
  const Boite b1 = encombrement(r1.forme), b2 = encombrement(r2.forme);
  INFO("zmin avant " << b1.zmin << " après " << b2.zmin);
  CHECK(b2.zmin == Approx(b1.zmin).margin(1e-6));
}

TEST_CASE("ES-01c tube plein : volume avant et après relecture", "[ES-01]") {
  Piece p;
  FonctionPiece c; c.type = TypeFonction::Esquisse3D; c.points3d = {{0, 0, 0}, {0, 0, 50}};
  const int ic = p.ajouter(c);
  FonctionPiece t; t.type = TypeFonction::Balayage; t.tube = true; t.diametre = 10; t.epaisseur = 0; t.cheminId = ic;
  p.ajouter(t);
  const ResultatPiece r1 = construirePiece(p);
  const ResultatPiece r2 = construirePiece(pieceDepuisJson(pieceVersJson(p)));
  INFO("erreurs : " << erreurs(r1) << " / " << erreurs(r2));
  const double v1 = vol(r1), v2 = vol(r2);
  INFO("volume avant " << v1 << " après " << v2 << " attendu " << PI * 25 * 50);
  CHECK(v1 == Approx(PI * 25 * 50).epsilon(1e-3));
  CHECK(v2 == Approx(PI * 25 * 50).epsilon(1e-3));
}

TEST_CASE("ES-02 tangence à extrémité commune : faux degrés de liberté", "[ES-02]") {
  Esquisse r; r.ajouterRainure(0, 0, 30, 0, 5);
  EtatEsquisse s = resoudre(r);
  INFO("rainure : ddl " << s.degresLiberte << ", redondantes " << s.redondantes.size());
  CHECK(s.degresLiberte == 5);
  CHECK(s.redondantes.size() <= 1);

  Esquisse c; const auto l = c.ajouterRectangle(0, 0, 60, 40);
  c.contraindre(TypeContrainte::Coincidence, {c.element(l[0])->p1, 0});
  c.contraindre(TypeContrainte::Longueur, {l[0]}, 60);
  c.contraindre(TypeContrainte::Longueur, {l[1]}, 40);
  REQUIRE(resoudre(c).degresLiberte == 0);
  REQUIRE(c.congeEsquisse(c.element(l[1])->p2, 5));
  s = resoudre(c);
  const auto d = diagnostiquer(c);
  INFO("rectangle coté + congé : ddl resoudre " << s.degresLiberte << ", ddl diagnostiquer " << d.degresLiberte << ", redondantes " << s.redondantes.size());
  CHECK(s.degresLiberte == 0);
  CHECK(d.degresLiberte == 0);

  Esquisse t; const int p0 = t.ajouterPoint(0, 0), p1 = t.ajouterPoint(20, 0);
  t.ajouterLigne(p0, p1); t.ajouterArcTangent(p1, 30, 10);
  s = resoudre(t);
  INFO("ligne + arc tangent : ddl " << s.degresLiberte);
  CHECK(s.degresLiberte == 6);
}

TEST_CASE("ES-02b une cote fausse dans une esquisse avec congé : nombre de contraintes désignées en conflit", "[ES-02]") {
  Esquisse c; const auto l = c.ajouterRectangle(0, 0, 60, 40);
  c.contraindre(TypeContrainte::Coincidence, {c.element(l[0])->p1, 0});
  c.contraindre(TypeContrainte::Longueur, {l[0]}, 60);
  c.contraindre(TypeContrainte::Longueur, {l[1]}, 40);
  REQUIRE(c.congeEsquisse(c.element(l[1])->p2, 5));
  REQUIRE(resoudre(c).resolue);
  // cote contradictoire : diagonale imposée impossible
  c.contraindre(TypeContrainte::Distance, {c.element(l[0])->p1, c.element(l[0])->p2}, 10);
  const EtatEsquisse s = resoudre(c);
  INFO("contraintes totales " << c.contraintes.size() << ", désignées en conflit " << s.enConflit.size());
  CHECK_FALSE(s.resolue);
  CHECK(s.enConflit.size() <= 4);
}

TEST_CASE("ES-03 arc tangent qui prolonge un arc du même côté : conflit", "[ES-03]") {
  Esquisse t; const int c = t.ajouterPoint(0, 0), a = t.ajouterPoint(10, 0), b = t.ajouterPoint(0, 10);
  t.ajouterArc(c, a, b);
  t.contraindre(TypeContrainte::Fixe, {c}, 0, 0); t.contraindre(TypeContrainte::Fixe, {a}, 10, 0);
  REQUIRE(resoudre(t).resolue);
  REQUIRE(t.ajouterArcTangent(b, -4, 8) != 0);
  INFO("valeur de la tangence posée : " << t.contraintes.back().valeur << " (0 = extérieure)");
  const EtatEsquisse s = resoudre(t);
  INFO("résolue : " << s.resolue << " ; message : " << s.message);
  CHECK(s.resolue);
}

TEST_CASE("ES-04 cote d'angle : signe perdu quand une variable la réécrit", "[ES-04]") {
  Piece pc; pc.variables = {{"a", "30"}};
  FonctionPiece f; f.type = TypeFonction::Esquisse;
  Esquisse& e = f.esquisse;
  const int p = e.ajouterPoint(10, 0), q = e.ajouterPoint(8.66, -5);
  const int l1 = e.ajouterLigne(0, p), l2 = e.ajouterLigne(0, q);
  e.contraindre(TypeContrainte::Horizontal, {l1});
  e.contraindre(TypeContrainte::Longueur, {l2}, 10);
  ContrainteEsquisse k; k.id = e.prochainId++; k.type = TypeContrainte::Angle; k.refs = {l1, l2};
  k.valeur = mesurer(e, k);
  INFO("mesure initiale " << k.valeur);
  k.expression = "a";
  e.contraintes.push_back(k);
  REQUIRE(resoudre(e).resolue);
  const int id = pc.ajouter(f);
  appliquerVariables(pc);
  INFO("y de l'extrémité après application de la variable : " << pc.fonction(id)->esquisse.point(q)->y);
  CHECK(pc.fonction(id)->esquisse.point(q)->y < 0);
}

TEST_CASE("ES-05 pivoter un rectangle de 30° : la rotation est défaite", "[ES-05]") {
  Esquisse r; const auto l = r.ajouterRectangle(0, 0, 40, 20);
  const double c = std::cos(PI / 6), s = std::sin(PI / 6);
  r.transformer(l, [&](Point p) { return Point{p.x * c - p.y * s, p.x * s + p.y * c}; }, 1, false);
  const EtatEsquisse st = resoudre(r);
  const auto* b = r.element(l[0]);
  const auto *A = r.point(b->p1), *B = r.point(b->p2);
  INFO("résolue " << st.resolue << " ; côté bas de (" << A->x << ";" << A->y << ") à (" << B->x << ";" << B->y << "), longueur " << std::hypot(B->x - A->x, B->y - A->y));
  CHECK(B->y - A->y == Approx(20).margin(1e-3));
  CHECK(std::hypot(B->x - A->x, B->y - A->y) == Approx(40).margin(1e-3));
}

TEST_CASE("ES-06 échelle ×2 avec cotes horizontale et verticale", "[ES-06]") {
  Esquisse k; const auto l = k.ajouterRectangle(0, 0, 40, 20);
  const int a = k.element(l[0])->p1, b = k.element(l[0])->p2, c = k.element(l[1])->p2;
  k.contraindre(TypeContrainte::Coincidence, {a, 0});
  k.contraindre(TypeContrainte::DistanceX, {a, b}, 40);
  k.contraindre(TypeContrainte::DistanceY, {b, c}, 20);
  REQUIRE(resoudre(k).degresLiberte == 0);
  k.transformer(l, [](Point p) { return Point{2 * p.x, 2 * p.y}; }, 2, false);
  REQUIRE(resoudre(k).resolue);
  INFO("coin opposé après échelle ×2 : (" << k.point(c)->x << ";" << k.point(c)->y << ")");
  CHECK(k.point(c)->x == Approx(80));
}

TEST_CASE("ES-19 chanfrein d'esquisse avec réallocation (sans ASan : contrôle fonctionnel seulement)", "[ES-19]") {
  Esquisse e; const auto l = e.ajouterRectangle(0, 0, 20, 20);
  e.points.shrink_to_fit();
  const int ch = e.chanfreinEsquisse(e.element(l[0])->p1, 3);
  REQUIRE(ch != 0);
  CHECK(longueurLigne(e, ch) == Approx(3 * std::sqrt(2.0)).margin(1e-6));
}

TEST_CASE("ES-07 chanfrein d'esquisse sur un rectangle coté : il s'effondre", "[ES-07]") {
  Esquisse h; const auto l = h.ajouterRectangle(0, 0, 60, 40);
  h.contraindre(TypeContrainte::Coincidence, {h.element(l[0])->p1, 0});
  h.contraindre(TypeContrainte::Longueur, {l[0]}, 60);
  h.contraindre(TypeContrainte::Longueur, {l[1]}, 40);
  REQUIRE(resoudre(h).degresLiberte == 0);
  const int ch = h.chanfreinEsquisse(h.element(l[1])->p2, 5);
  REQUIRE(ch != 0);
  const EtatEsquisse s = resoudre(h);
  INFO("longueur du chanfrein après résolution " << longueurLigne(h, ch) << " ; ddl " << s.degresLiberte);
  CHECK(longueurLigne(h, ch) == Approx(5 * std::sqrt(2.0)).margin(1e-4));
  CHECK(s.degresLiberte == 0);
}

TEST_CASE("ES-08 prolonger une ligne cotée : la cote la ramène", "[ES-08]") {
  Esquisse g; const int a = g.ajouterPoint(0, 0), b = g.ajouterPoint(10, 0); const int l = g.ajouterLigne(a, b);
  g.contraindre(TypeContrainte::Coincidence, {a, 0});
  g.contraindre(TypeContrainte::Horizontal, {l});
  g.contraindre(TypeContrainte::Longueur, {l}, 10);
  g.ajouterLigne(g.ajouterPoint(30, -5), g.ajouterPoint(30, 5));
  REQUIRE(g.prolonger(l, 9, 0));
  REQUIRE(g.point(b)->x == Approx(30));
  resoudre(g);
  INFO("x de l'extrémité après résolution : " << g.point(b)->x);
  CHECK(g.point(b)->x == Approx(30));
}

TEST_CASE("ES-09 décaler un secteur (ligne, arc, ligne) : jonction arc→ligne", "[ES-09]") {
  Esquisse e; const int A = e.ajouterPoint(10, 0), B = e.ajouterPoint(0, 10);
  const int l1 = e.ajouterLigne(0, A), arc = e.ajouterArc(0, A, B), l2 = e.ajouterLigne(B, 0);
  const auto d = e.decaler({l1, arc, l2}, 1);
  REQUIRE(d.size() == 3);
  const ElementEsquisse* a2 = nullptr;
  for (int id : d) if (e.element(id)->type == TypeElement::Arc) a2 = e.element(id);
  REQUIRE(a2);
  const auto *c = e.point(a2->centre), *q1 = e.point(a2->p1), *q2 = e.point(a2->p2);
  const double r1 = std::hypot(q1->x - c->x, q1->y - c->y), r2 = std::hypot(q2->x - c->x, q2->y - c->y);
  INFO("arc décalé : rayons aux extrémités " << r1 << " et " << r2 << " ; fin (" << q2->x << ";" << q2->y << ")");
  CHECK(r1 == Approx(r2).margin(1e-6));
  CHECK(std::max(r1, r2) == Approx(11).margin(1e-6));
}

TEST_CASE("ES-10 décaler un profil en D (deux éléments) de +2 : vers l'extérieur ?", "[ES-10]") {
  Esquisse d; const int a = d.ajouterPoint(-10, 0), b = d.ajouterPoint(10, 0);
  const int li = d.ajouterLigne(a, b), ar = d.ajouterArc(0, b, a);
  const auto r = d.decaler({li, ar}, 2);
  REQUIRE(r.size() == 2);
  double rayon = 0;
  for (int id : r) if (d.element(id)->type == TypeElement::Arc) rayon = d.rayonDe(*d.element(id));
  INFO("rayon de l'arc décalé : " << rayon);
  CHECK(rayon == Approx(12));
}

TEST_CASE("ES-11 rainure en arc : degrés de liberté et tangence au glisser", "[ES-11]") {
  Esquisse ra; const auto arcs = ra.ajouterRainureArc(0, 0, 20, 0, 0, 20, 4);
  const EtatEsquisse s = resoudre(ra);
  INFO("ddl rainure en arc : " << s.degresLiberte << " (6 attendus)");
  CHECK(s.degresLiberte == 6);
}

TEST_CASE("ES-12 répétition circulaire : copies non liées", "[ES-12]") {
  Esquisse q; const int cen = q.ajouterPoint(20, 0); const int c1 = q.ajouterCercle(cen, 2);
  const int fx = q.contraindre(TypeContrainte::Fixe, {cen}, 20, 0);
  q.contraindre(TypeContrainte::Rayon, {c1}, 2);
  const auto cir = q.repeterCirculaire({c1}, 0, 0, 6, 360);
  const EtatEsquisse s = resoudre(q);
  INFO("ddl après répétition circulaire d'un cercle entièrement contraint : " << s.degresLiberte);
  CHECK(s.degresLiberte == 0);
  for (auto& k : q.contraintes) if (k.id == fx) k.valeur = 30;
  resoudre(q);
  REQUIRE_FALSE(cir.empty());
  INFO("copie 1 après déplacement de l'original : x = " << q.point(q.element(cir[0])->centre)->x);
  CHECK(q.point(q.element(cir[0])->centre)->x == Approx(15).margin(1e-3));
}

TEST_CASE("ES-13 ajuster un côté d'hexagone : l'hexagone tourne", "[ES-13]") {
  Esquisse h; const auto cotes = h.ajouterPolygone(0, 0, 10, 0, 6);
  // centre = premier point créé après l'origine ; cercle de construction = premier élément
  h.contraindre(TypeContrainte::Fixe, {h.points[1].id}, 0, 0);
  h.contraindre(TypeContrainte::Rayon, {h.elements[0].id}, 10);
  const int v1 = h.ajouterPoint(7, -1), v2 = h.ajouterPoint(7, 20); h.ajouterLigne(v1, v2);
  h.contraindre(TypeContrainte::Fixe, {v1}, 7, -1); h.contraindre(TypeContrainte::Fixe, {v2}, 7, 20);
  REQUIRE(resoudre(h).resolue);
  // position d'un sommet qui ne doit pas bouger (sommet opposé)
  const auto* c3 = h.element(cotes[3]);
  const Point avant{h.point(c3->p1)->x, h.point(c3->p1)->y};
  REQUIRE(h.ajuster(cotes[0], 9, 2));
  const EtatEsquisse s = resoudre(h);
  const Point apres{h.point(c3->p1)->x, h.point(c3->p1)->y};
  INFO("résolue " << s.resolue << " ; sommet opposé avant (" << avant.x << ";" << avant.y << ") après (" << apres.x << ";" << apres.y << ")");
  CHECK(apres.x == Approx(avant.x).margin(1e-3));
  CHECK(apres.y == Approx(avant.y).margin(1e-3));
}

TEST_CASE("ES-14 saisie « 1/4 in », « 40/2 mm » en pouces, « 3/8\" »", "[ES-14]") {
  double v = 0; const Unites mm{}; Unites po; po.longueur = UniteLongueur::Pouce;
  REQUIRE(lireValeur("1/4 in", false, mm, v)); INFO("1/4 in → " << v << " mm"); CHECK(v == Approx(6.35));
  REQUIRE(lireValeur("40/2 mm", false, po, v)); INFO("40/2 mm (doc pouces) → " << v << " mm"); CHECK(v == Approx(20));
  REQUIRE(lireValeur("3/8\"", false, po, v)); INFO("3/8\" → " << v << " mm"); CHECK(v == Approx(9.525));
  REQUIRE(lireValeur("1/2 in", false, po, v)); INFO("1/2 in (doc pouces) → " << v << " mm"); CHECK(v == Approx(12.7));
}

TEST_CASE("ES-15 variables : sans unité dans un document en pouces, variable d'angle", "[ES-15]") {
  Piece p; p.uniteLongueur = "in"; p.variables = {{"L", "100 mm"}, {"n", "4"}, {"a", "30°"}};
  std::string err; const auto v = evaluerVariables(p, &err);
  INFO("erreur : " << err);
  REQUIRE(v.count("n") == 1);
  INFO("n = " << v.at("n"));
  CHECK(v.at("n") == Approx(4));
  CHECK(v.count("a") == 1);
  double r = 0; Unites u; u.longueur = UniteLongueur::Pouce;
  REQUIRE(lireValeur("L*n", false, u, r, nullptr, &v));
  INFO("L*n = " << r << " mm");
  CHECK(r == Approx(400));
}

TEST_CASE("ES-16 appliquerVariables ignore l'unité d'angle (radians)", "[ES-16]") {
  Piece p; p.uniteAngle = "rad"; p.variables = {{"a", "1"}};
  FonctionPiece f; f.type = TypeFonction::Esquisse;
  const int l1 = f.esquisse.ajouterLigne(0, f.esquisse.ajouterPoint(10, 0));
  const int l2 = f.esquisse.ajouterLigne(0, f.esquisse.ajouterPoint(5, 8));
  f.esquisse.contraindre(TypeContrainte::Horizontal, {l1});
  const int ang = f.esquisse.contraindre(TypeContrainte::Angle, {l1, l2}, 85.94);
  f.esquisse.contraintes.back().expression = "a + 0,5";
  const int id = p.ajouter(f);
  appliquerVariables(p);
  for (const auto& c : p.fonction(id)->esquisse.contraintes)
    if (c.id == ang) { INFO("valeur après application : " << c.valeur << "° (attendu 1,5 rad = 85,94°)"); CHECK(c.valeur == Approx(85.9437).epsilon(1e-3)); }
}

TEST_CASE("ES-17 appliquerVariables : un conflit créé par la variable n'est pas signalé", "[ES-17]") {
  Piece p; p.variables = {{"L", "40"}};
  FonctionPiece f; f.type = TypeFonction::Esquisse;
  const int b = f.esquisse.ajouterPoint(40, 0); const int l = f.esquisse.ajouterLigne(0, b);
  f.esquisse.contraindre(TypeContrainte::Fixe, {b}, 40, 0);
  f.esquisse.contraindre(TypeContrainte::Longueur, {l}, 40);
  f.esquisse.contraintes.back().expression = "L";
  const int id = p.ajouter(f);
  p.variables[0].second = "60";
  std::string err; const bool ok = appliquerVariables(p, &err);
  Esquisse copie = p.fonction(id)->esquisse;
  REQUIRE_FALSE(resoudre(copie).resolue);
  INFO("appliquerVariables a rendu " << ok << " avec l'erreur « " << err << " »");
  CHECK((!ok || !err.empty()));
}

TEST_CASE("ES-20 rayon de cercle négatif accepté", "[ES-20]") {
  Esquisse g; const int ca = g.ajouterPoint(0, 0), cb = g.ajouterPoint(3, 0);
  const int A = g.ajouterCercle(ca, 10), B = g.ajouterCercle(cb, 2);
  g.contraindre(TypeContrainte::Fixe, {ca}, 0, 0); g.contraindre(TypeContrainte::Fixe, {cb}, 3, 0);
  g.contraindre(TypeContrainte::Rayon, {A}, 10); g.contraindre(TypeContrainte::Tangente, {A, B}, 0);
  const EtatEsquisse s = resoudre(g);
  INFO("résolue " << s.resolue << " ; rayon de B " << g.element(B)->rayon);
  CHECK((!s.resolue || g.element(B)->rayon > 0));
}

TEST_CASE("ES-21 parallèle posée sur deux lignes exactement perpendiculaires", "[ES-21]") {
  Esquisse e; const int l1 = e.ajouterLigne(e.ajouterPoint(0, 0), e.ajouterPoint(20, 0));
  const int l2 = e.ajouterLigne(e.ajouterPoint(30, 5), e.ajouterPoint(30, 25));
  e.contraindre(TypeContrainte::Horizontal, {l1});
  e.contraindre(TypeContrainte::Parallele, {l1, l2});
  const EtatEsquisse s = resoudre(e);
  INFO("résolue " << s.resolue << " ; résidu " << s.residu);
  CHECK(s.resolue);
}

TEST_CASE("ES-22 rectangle par le centre : redondance dès la création", "[ES-22]") {
  Esquisse e; e.ajouterRectangleCentre(10, 5, 30, 20);
  const EtatEsquisse s = resoudre(e);
  INFO("redondantes : " << s.redondantes.size());
  CHECK(s.redondantes.empty());
}

TEST_CASE("ES-23 diagnostiquer : degrés de liberté fantômes à 10 m", "[ES-23]") {
  for (double k : {1000.0, 10000.0, 30000.0}) {
    Esquisse e; const int a = e.ajouterPoint(0, 0), b = e.ajouterPoint(k, 0.05 * k), c = e.ajouterPoint(0.4 * k, 0.7 * k);
    const int l1 = e.ajouterLigne(a, b), l2 = e.ajouterLigne(b, c), l3 = e.ajouterLigne(c, a);
    e.contraindre(TypeContrainte::Coincidence, {a, 0});
    e.contraindre(TypeContrainte::Angle, {AXE_H, l1}, 0);
    e.contraindre(TypeContrainte::Longueur, {l1}, k);
    e.contraindre(TypeContrainte::Angle, {l1, l3}, -120);
    e.contraindre(TypeContrainte::Parallele, {l2, AXE_V});
    const EtatEsquisse s = resoudre(e);
    const auto d = diagnostiquer(e);
    INFO("échelle " << k << " mm : resoudre " << s.degresLiberte << " ddl, diagnostiquer " << d.degresLiberte << " ddl");
    CHECK(s.degresLiberte == d.degresLiberte);
  }
}

TEST_CASE("ES-24 redondance attribuée à l'ancienne contrainte sur une petite ligne", "[ES-24]") {
  for (double L : {20.0, 0.5}) {
    Esquisse m; const int p1 = m.ajouterPoint(0, 0), p2 = m.ajouterPoint(L, 0); const int li = m.ajouterLigne(p1, p2);
    m.contraindre(TypeContrainte::Coincidence, {p1, 0});
    m.contraindre(TypeContrainte::Horizontal, {li});
    m.contraindre(TypeContrainte::Longueur, {li}, L);
    REQUIRE(resoudre(m).degresLiberte == 0);
    const int ang = m.contraindre(TypeContrainte::Angle, {AXE_H, li}, 0);
    const auto s = resoudre(m);
    INFO("ligne de " << L << " mm : la nouvelle cote est-elle désignée redondante ?");
    CHECK(std::find(s.redondantes.begin(), s.redondantes.end(), ang) != s.redondantes.end());
  }
}

TEST_CASE("ES-25 prolonger vers le départ exact d'un arc", "[ES-25]") {
  int manques = 0, essais = 0;
  for (int deg = 0; deg < 360; deg += 7) {
    ++essais;
    const double t = deg * PI / 180;
    Esquisse e; const int c = e.ajouterPoint(0, 0);
    e.ajouterArc(c, e.ajouterPoint(10 * std::cos(t), 10 * std::sin(t)), e.ajouterPoint(10 * std::cos(t + 1), 10 * std::sin(t + 1)));
    const int v = e.ajouterPoint(15 * std::cos(t), 15 * std::sin(t));
    const int l = e.ajouterLigne(e.ajouterPoint(20 * std::cos(t), 20 * std::sin(t)), v);
    if (!e.prolonger(l, 15 * std::cos(t), 15 * std::sin(t)) || std::hypot(e.point(v)->x - 10 * std::cos(t), e.point(v)->y - 10 * std::sin(t)) > 1e-6) ++manques;
  }
  INFO(manques << " orientations manquées sur " << essais);
  CHECK(manques == 0);
}

TEST_CASE("ES-26 cote d'angle démesurée (1e9°) : temps de résolution", "[ES-26][lent]") {
  Esquisse e; const int l1 = e.ajouterLigne(0, e.ajouterPoint(10, 0)), l2 = e.ajouterLigne(0, e.ajouterPoint(5, 5));
  e.contraindre(TypeContrainte::Angle, {l1, l2}, 1e9);
  Chrono ch;
  resoudre(e);
  WARN("résolution avec un angle de 1e9° : " << ch.ms() << " ms");
  CHECK(ch.ms() < 1000);
}

TEST_CASE("ES-26b cote d'angle 1e20° : la résolution ne rend jamais la main", "[ES-26][gel]") {
  Esquisse e; const int l1 = e.ajouterLigne(0, e.ajouterPoint(10, 0)), l2 = e.ajouterLigne(0, e.ajouterPoint(5, 5));
  e.contraindre(TypeContrainte::Angle, {l1, l2}, 1e20);
  resoudre(e);
  SUCCEED("rendu la main");
}

TEST_CASE("ES-27 chanfrein refusé : point orphelin laissé", "[ES-27]") {
  Esquisse e; const auto l = e.ajouterRectangle(0, 0, 20, 5);
  const size_t n = e.points.size();
  CHECK(e.chanfreinEsquisse(e.element(l[0])->p1, 10) == 0);
  INFO("points avant " << n << " après " << e.points.size());
  CHECK(e.points.size() == n);
}

TEST_CASE("ES-28 ajuster un arc de construction : le morceau neuf n'est plus de construction", "[ES-28]") {
  Esquisse e; const int c = e.ajouterPoint(0, 0);
  const int arc = e.ajouterArc(c, e.ajouterPoint(10, 0), e.ajouterPoint(-10, 0), true);
  e.ajouterLigne(e.ajouterPoint(-5, -1), e.ajouterPoint(-5, 20));
  e.ajouterLigne(e.ajouterPoint(5, -1), e.ajouterPoint(5, 20));
  REQUIRE(e.ajuster(arc, 0, 10));
  int arcsNonConstruction = 0;
  for (const auto& el : e.elements) if (el.type == TypeElement::Arc && !el.construction) ++arcsNonConstruction;
  INFO("arcs devenus « profil » : " << arcsNonConstruction);
  CHECK(arcsNonConstruction == 0);
}

TEST_CASE("ES-29 décaler deux rectangles sélectionnés ensemble", "[ES-29]") {
  Esquisse e; auto r1 = e.ajouterRectangle(0, 0, 10, 10), r2 = e.ajouterRectangle(20, 0, 30, 10);
  r1.insert(r1.end(), r2.begin(), r2.end());
  const auto d = e.decaler(r1, 1);
  INFO("éléments créés : " << d.size());
  CHECK(d.size() == 8);
}

TEST_CASE("ES-30 boucles() : un trait isolé fait perdre le cercle", "[ES-30]") {
  Esquisse e; e.ajouterCercle(e.ajouterPoint(0, 0), 5); e.ajouterLigne(e.ajouterPoint(20, 0), e.ajouterPoint(30, 0));
  std::string err; const auto b = boucles(e, &err);
  INFO("boucles : " << b.size() << " ; erreur : " << err);
  CHECK(b.size() == 1);
}

TEST_CASE("ES-31 espace insécable dans une saisie", "[ES-31]") {
  double v = 0; std::string err;
  const bool ok = lireValeur("10\xC2\xA0mm", false, Unites{}, v, &err);
  INFO("erreur : " << err);
  CHECK(ok);
}

TEST_CASE("ES-32 unité appliquée deux fois après une parenthèse", "[ES-32]") {
  double v = 0;
  const bool ok = lireValeur("(10 mm + 5) cm", false, Unites{}, v);
  INFO("accepté " << ok << " valeur " << v);
  CHECK_FALSE(ok);
  double w = 0;
  if (lireValeur("(1 m) m", false, Unites{}, w)) { INFO("(1 m) m = " << w); CHECK(w != Approx(1e6)); }
}

TEST_CASE("ES-33 rayon minimal arbitraire à la relecture", "[ES-33]") {
  Esquisse e; const int c = e.ajouterCercle(e.ajouterPoint(0, 0), 0.0005);
  const Esquisse r = esquisseDepuisJson(esquisseVersJson(e));
  INFO("rayon relu " << r.element(c)->rayon);
  CHECK(r.element(c)->rayon == 0.0005);
}

TEST_CASE("ES-35 distance nulle entre deux points : ddl", "[ES-35]") {
  Esquisse e; const int a = e.ajouterPoint(0, 0), b = e.ajouterPoint(3, 4);
  e.contraindre(TypeContrainte::Fixe, {a}, 0, 0); e.contraindre(TypeContrainte::Distance, {a, b}, 0);
  const EtatEsquisse s = resoudre(e);
  INFO("ddl " << s.degresLiberte << " ; résidu " << s.residu);
  CHECK(s.degresLiberte == 0);
}

TEST_CASE("ES-39 noms par défaut en double après suppression", "[ES-39]") {
  Piece p;
  for (int i = 0; i < 3; ++i) { FonctionPiece e; e.type = TypeFonction::Esquisse; p.ajouter(e); }
  p.fonctions.erase(p.fonctions.begin() + 1);  // « Esquisse 2 »
  FonctionPiece e; e.type = TypeFonction::Esquisse; p.ajouter(e);
  std::vector<std::string> noms; for (const auto& f : p.fonctions) noms.push_back(f.nom);
  std::sort(noms.begin(), noms.end());
  INFO("noms : " << noms[0] << ", " << noms[1] << ", " << noms[2]);
  CHECK(std::adjacent_find(noms.begin(), noms.end()) == noms.end());
  Piece q; FonctionPiece a; a.type = TypeFonction::Extrusion; q.ajouter(a);
  FonctionPiece b; b.type = TypeFonction::Extrusion; b.enlever = true; q.ajouter(b);
  FonctionPiece c; c.type = TypeFonction::Extrusion; q.ajouter(c);
  INFO("troisième fonction (extrusion) nommée « " << q.fonctions[2].nom << " »");
  CHECK(q.fonctions[2].nom == "Extrusion 2");
}

TEST_CASE("ES-36 arc qui se retourne quand on tire son extrémité", "[ES-36]") {
  Esquisse e; const int c = e.ajouterPoint(0, 0), a = e.ajouterPoint(10, 0), b = e.ajouterPoint(0, 10);
  e.ajouterArc(c, a, b);
  e.contraindre(TypeContrainte::Fixe, {c}, 0, 0); e.contraindre(TypeContrainte::Fixe, {a}, 10, 0);
  resoudre(e, {{b, Point{7.07, -7.07}}});
  const double t0 = std::atan2(e.point(a)->y, e.point(a)->x), t1 = std::atan2(e.point(b)->y, e.point(b)->x);
  double balayage = (t1 - t0) * 180 / PI; while (balayage < 0) balayage += 360;
  WARN("balayage de l'arc après avoir tiré son extrémité sous le départ : " << balayage << "°");
  SUCCEED();
}

TEST_CASE("OK NaN dans une cote : relecture JSON", "[ok]") {
  Esquisse e; const int l = e.ajouterLigne(0, e.ajouterPoint(10, 0));
  e.contraindre(TypeContrainte::Longueur, {l}, std::numeric_limits<double>::quiet_NaN());
  const Json j = esquisseVersJson(e);
  CHECK_NOTHROW(esquisseDepuisJson(j));
}

TEST_CASE("EI-13 symétrie d'un demi-profil : points de l'axe dédoublés, boucles()", "[EI-13]") {
  Esquisse e;
  const int p0 = e.ajouterPoint(0, 0.5), p1 = e.ajouterPoint(10, 0.5), p2 = e.ajouterPoint(10, 20), p3 = e.ajouterPoint(0, 20);
  const std::vector<int> demi = {e.ajouterLigne(p0, p1), e.ajouterLigne(p1, p2), e.ajouterLigne(p2, p3)};
  e.symetriser(demi, AXE_V);
  REQUIRE(resoudre(e).resolue);
  int confondus = 0;
  for (size_t i = 0; i < e.points.size(); ++i)
    for (size_t j = i + 1; j < e.points.size(); ++j)
      if (std::hypot(e.points[i].x - e.points[j].x, e.points[i].y - e.points[j].y) < 1e-9) ++confondus;
  std::string err; const auto b = boucles(e, &err);
  INFO("paires de points confondus : " << confondus << " ; boucles : " << b.size() << " ; erreur : " << err);
  CHECK(confondus == 0);
  CHECK(b.size() == 1);
}

TEST_CASE("OK tangence ligne-cercle : le cercle peut-il passer de l'autre côté ?", "[ok]") {
  Esquisse e; const int a = e.ajouterPoint(0, 0), b = e.ajouterPoint(40, 0); const int l = e.ajouterLigne(a, b);
  e.contraindre(TypeContrainte::Fixe, {a}, 0, 0); e.contraindre(TypeContrainte::Fixe, {b}, 40, 0);
  const int c = e.ajouterPoint(20, 10); const int cer = e.ajouterCercle(c, 10);
  e.contraindre(TypeContrainte::Tangente, {l, cer}); e.contraindre(TypeContrainte::DistanceX, {c}, 20);
  const int r = e.contraindre(TypeContrainte::Rayon, {cer}, 10);
  for (double R : {2.0, 30.0, 0.5}) {
    for (auto& k : e.contraintes) if (k.id == r) k.valeur = R;
    REQUIRE(resoudre(e).resolue);
    INFO("rayon " << R << " : y du centre " << e.point(c)->y);
    CHECK(e.point(c)->y > 0);
  }
}

TEST_CASE("ES-38 cote entre deux lignes « parallèles » qui cessent de l'être", "[ES-38]") {
  Esquisse e; const int l1 = e.ajouterLigne(e.ajouterPoint(0, 0), e.ajouterPoint(40, 0));
  const int a = e.ajouterPoint(0, 10), b = e.ajouterPoint(40, 10); const int l2 = e.ajouterLigne(a, b);
  e.contraindre(TypeContrainte::Horizontal, {l1}); e.contraindre(TypeContrainte::Distance, {l1, l2}, 10);
  resoudre(e, {{b, Point{40, 20}}});
  WARN("après avoir tiré une extrémité de la seconde ligne : (" << e.point(a)->x << ";" << e.point(a)->y << ") → (" << e.point(b)->x << ";" << e.point(b)->y << ")");
  SUCCEED();
}

TEST_CASE("OK aller-retour JSON de toutes les contraintes", "[ok]") {
  Esquisse e; const auto l = e.ajouterRectangle(0, 0, 40, 20);
  const int c = e.ajouterPoint(10, 10); const int ce = e.ajouterCercle(c, 3);
  const int c2 = e.ajouterPoint(30, 10); const int ce2 = e.ajouterCercle(c2, 3);
  e.contraindre(TypeContrainte::Tangente, {ce, ce2}, 1);
  e.contraindre(TypeContrainte::Fixe, {c}, 10, 10);
  e.contraindre(TypeContrainte::Angle, {AXE_H, l[0]}, 0);
  e.contraindre(TypeContrainte::Egal, {ce, ce2});
  e.contraindre(TypeContrainte::Concentrique, {ce, ce});
  e.contraindre(TypeContrainte::Diametre, {ce}, 6);
  e.contraintes.back().reference = true;
  e.contraintes.back().place = true; e.contraintes.back().lx = 3; e.contraintes.back().ly = 4;
  e.contraintes.back().expression = "D/2";
  const Json j1 = esquisseVersJson(e);
  const Json j2 = esquisseVersJson(esquisseDepuisJson(j1));
  CHECK(j1 == j2);
}

TEST_CASE("ES-18 temps d'un glisser sur des esquisses moyennes", "[ES-18][perf]") {
  for (int n : {25, 64, 100}) {
    Esquisse e; int coin = 0; const int cote = static_cast<int>(std::sqrt(n));
    for (int i = 0; i < cote; ++i)
      for (int j = 0; j < cote; ++j) {
        const auto l = e.ajouterRectangle(i * 30, j * 30, i * 30 + 20, j * 30 + 10);
        const int p1 = e.element(l[0])->p1;
        e.contraindre(TypeContrainte::DistanceX, {0, p1}, i * 30);
        e.contraindre(TypeContrainte::DistanceY, {0, p1}, j * 30);
        e.contraindre(TypeContrainte::Longueur, {l[0]}, 20);
        coin = e.element(l[1])->p2;
      }
    Chrono c0; resoudre(e); const double tRes = c0.ms();
    Chrono c1; resoudre(e, {{coin, Point{e.point(coin)->x + 1, e.point(coin)->y + 3}}}); const double tGl = c1.ms();
    Chrono c2; const auto d = diagnostiquer(e); const double tDiag = c2.ms();
    WARN(n << " rectangles (" << e.points.size() << " points) : résolution " << tRes << " ms, un glisser " << tGl << " ms, diagnostic " << tDiag << " ms (ddl " << d.degresLiberte << ")");
    CHECK(tGl < 50);
  }
}

TEST_CASE("ES-37 temps de diagnostiquer sur ~125 points", "[ES-37][perf]") {
  Esquisse e; e.ajouterPolygone(0, 0, 10, 0, 64); e.ajouterPolygone(30, 0, 40, 0, 58);
  resoudre(e);
  Chrono c; const auto d = diagnostiquer(e);
  WARN("diagnostiquer sur " << e.points.size() << " points : " << c.ms() << " ms, ddl " << d.degresLiberte);
  CHECK(c.ms() < 100);
}
