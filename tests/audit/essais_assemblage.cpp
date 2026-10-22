// Essais de l'audit de Prométhée 0.25 : assemblages, solveur, liaisons, analyses (constats AS-xx).
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

#include "aides.hpp"

using namespace verif;

namespace {
struct Plaques {
  Bloc b = bloc(60, 40, 10);
  std::vector<ResultatPiece> r;
  Plaques() { r = {construirePiece(b.p)}; }
};
bool essaiPlaque(const Plaques& P, const ContrainteAssemblage& k, double rx = 0, double ry = 0, double rz = 0) {
  Assemblage a;
  a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(0, 100, 0, 10, rx, ry, rz)};
  a.contraintes = {ctr("coincidence", 0, P.b.face("sommet"), 1, P.b.face("base")), k};
  return resoudreAssemblage(a, P.r).resolu;
}
}  // namespace

TEST_CASE("AS-04 points selles : contraintes d'orientation insolubles depuis l'orientation d'insertion", "[AS-04]") {
  Plaques P;
  const bool perp = essaiPlaque(P, ctr("perpendiculaire", 0, P.b.cote(0), 1, P.b.cote(0)));
  const bool angle = essaiPlaque(P, ctr("angle", 0, P.b.cote(0), 1, P.b.cote(0), 30));
  const bool para = essaiPlaque(P, ctr("parallele", 0, P.b.cote(0), 1, P.b.cote(1)));
  // témoin : même contrainte perpendiculaire avec 1° de rotation au départ
  const bool perpPerturbe = essaiPlaque(P, ctr("perpendiculaire", 0, P.b.cote(0), 1, P.b.cote(0)), 0, 0, 1);
  Assemblage f; f.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(0, 100, 0, 0)};
  f.contraintes = {ctr("coincidence", 0, P.b.face("sommet"), 1, P.b.face("sommet"))};
  const bool retournement = resoudreAssemblage(f, P.r).resolu;
  INFO("perpendiculaire " << perp << ", angle 30° " << angle << ", parallèle " << para << ", retournement " << retournement << " ; témoin perpendiculaire avec 1° au départ " << perpPerturbe);
  CHECK(perp);
  CHECK(angle);
  CHECK(para);
  CHECK(retournement);
}

TEST_CASE("AS-05 blocage de cardan : arbre couché par ry = 90 dans un pivot", "[AS-05]") {
  Bloc pl = bloc(60, 40, 30);
  const std::string alX = percer(pl.p, pl.cote(1), 20, 15, 10);  // flanc x = 60, normale sortante depuis FO-02 : axe X en (y 20 ; z 15)
  REQUIRE_FALSE(alX.empty());
  Arbre ar = arbre();
  const std::vector<ResultatPiece> pieces = {construirePiece(pl.p), construirePiece(ar.p)};
  Assemblage a;
  a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(1, 60, 20, 15, 0, 90, 0)};
  a.contraintes = {creerLiaison(a, pieces, "pivot", 0, alX, 1, ar.flanc)};
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  const AnalyseAssemblage an = analyserAssemblage(a, pieces);
  INFO("ddl " << an.degresLiberte << " ; rotation X marquée libre " << an.libres[1][3]);
  CHECK(an.libres[1][3]);
  a.contraintes[0].pilotee = true; a.contraintes[0].valeur = 30;
  const bool ok = resoudreAssemblage(a, pieces).resolu;
  const double y = monde(a.composants[1], 0, 1, 0).Y();
  INFO("pilotage à 30° : résolu " << ok << " ; Y local → Y monde " << y << " (attendu cos 30° = 0,866)");
  CHECK(ok);
  CHECK(std::abs(y) == Approx(std::cos(PI / 6)).margin(1e-3));
}

TEST_CASE("AS-05b degré de liberté fantôme : bloc debout entièrement contraint (ry = 90)", "[AS-05]") {
  Bloc b = bloc(60, 40, 10); const std::vector<ResultatPiece> r = {construirePiece(b.p)};
  Assemblage d;
  d.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(0, 45, 5, 60, 0, 90, 0)};
  d.contraintes = {ctr("coincidence", 0, b.face("sommet"), 1, b.cote(1)),
                   ctr("coincidence", 0, b.cote(0), 1, b.cote(0), 0, true),
                   ctr("coincidence", 0, b.cote(1), 1, b.face("sommet"), 0, true)};
  const bool ok = resoudreAssemblage(d, r).resolu;
  const AnalyseAssemblage an = analyserAssemblage(d, r);
  INFO("résolu " << ok << " ; ddl " << an.degresLiberte << " (attendu 0)");
  CHECK(ok);
  CHECK(an.degresLiberte == 0);
}

TEST_CASE("AS-06 pilotage d'un pivot d'axe X au-delà de 180°", "[AS-06]") {
  Bloc pl = bloc(60, 40, 30);
  const std::string alX = percer(pl.p, pl.cote(1), 20, 15, 10);  // flanc x = 60, normale sortante depuis FO-02 : axe X en (y 20 ; z 15)
  Arbre ar = arbre();
  const std::vector<ResultatPiece> pieces = {construirePiece(pl.p), construirePiece(ar.p)};
  Assemblage a;
  a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(1, 60, 20, 15, 90, 0, 90)};  // couché selon X par rx puis rz
  a.contraintes = {creerLiaison(a, pieces, "pivot", 0, alX, 1, ar.flanc)};
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  a.contraintes[0].pilotee = true;
  double atteint = 0;
  bool echec = false;
  for (double v = 3.6; v <= 360.001; v += 3.6) {
    a.contraintes[0].valeur = v;
    if (!resoudreAssemblage(a, pieces).resolu) { echec = true; break; }
    atteint = v;
  }
  INFO("pilotage pas à pas : dernier angle résolu " << atteint << "° ; échec " << echec);
  CHECK_FALSE(echec);
}

TEST_CASE("AS-07 levier en pivot dont l'axe est loin de son origine : liberté non signalée", "[AS-07]") {
  Bloc pl = bloc(60, 40, 10); const std::string alA = percer(pl.p, pl.face("sommet"), 30, 20);
  Bloc lv = bloc(80, 20, 5); const std::string alB = percer(lv.p, lv.face("sommet"), 70, 10);
  REQUIRE_FALSE(alA.empty()); REQUIRE_FALSE(alB.empty());
  const std::vector<ResultatPiece> pieces = {construirePiece(pl.p), construirePiece(lv.p)};
  Assemblage a;
  a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(1, -40, 10, 10)};
  a.contraintes = {creerLiaison(a, pieces, "pivot", 0, alA, 1, alB)};
  REQUIRE(resoudreAssemblage(a, pieces).resolu);
  const AnalyseAssemblage an = analyserAssemblage(a, pieces);
  int marques = 0; for (bool b : an.libres[1]) marques += b;
  INFO("ddl total " << an.degresLiberte << " ; mouvements marqués libres pour le levier : " << marques << " ; rotation Z libre " << an.libres[1][5]);
  CHECK(an.degresLiberte == 1);
  CHECK(an.libres[1][5]);
}

TEST_CASE("AS-08 « tangente » entre deux cylindres : axes rendus perpendiculaires", "[AS-08]") {
  Arbre gros = arbre(10), petit = arbre(5);
  const std::vector<ResultatPiece> r = {construirePiece(gros.p), construirePiece(petit.p)};
  Assemblage a; a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(1, 30, 0, 0)};
  a.contraintes = {ctr("tangente", 0, gros.flanc, 1, petit.flanc)};
  const EtatAssemblage e = resoudreAssemblage(a, r);
  const gp_Dir z = monde(a.composants[1], 0, 0, 1);
  INFO("résolu " << e.resolu << " ; axe du petit cylindre (" << z.X() << ";" << z.Y() << ";" << z.Z() << ")");
  CHECK((!e.resolu || std::abs(z.Z()) > 0.999));
}

TEST_CASE("AS-10 engrenage : rapport « dents menée / dents menante » et sens selon l'orientation de l'axe", "[AS-10]") {
  Bloc pl = bloc(80, 40, 10);
  FonctionPiece pts; pts.type = TypeFonction::Esquisse; pts.face = pl.face("sommet");
  pts.esquisse.ajouterPoint(25, 20); pts.esquisse.ajouterPoint(55, 20);
  const int ip = pl.p.ajouter(pts);
  FonctionPiece tr; tr.type = TypeFonction::Percage; tr.esquisseId = ip; tr.typePercage = "simple"; tr.diametre = 10; tr.traversant = true;
  const int it = pl.p.ajouter(tr);
  Arbre ar = arbre();
  const std::vector<ResultatPiece> pieces = {construirePiece(pl.p), construirePiece(ar.p)};
  std::vector<std::pair<double, std::string>> al;
  for (const auto& [f, nom] : pieces[0].faces)
    if (nom.rfind(std::to_string(it) + ":trou", 0) == 0) { BRepAdaptor_Surface su(f); if (su.GetType() == GeomAbs_Cylinder) al.push_back({su.Cylinder().Location().X(), nom}); }
  std::sort(al.begin(), al.end());
  REQUIRE(al.size() == 2);
  auto engrenage = [&](bool retourne) {
    Assemblage e;
    e.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(1, 25, 20, -15), retourne ? cmp(1, 55, 20, 25, 180, 0, 0) : cmp(1, 55, 20, -15)};
    e.contraintes.push_back(creerLiaison(e, pieces, "pivot", 0, al[0].second, 1, ar.flanc));
    e.contraintes.push_back(creerLiaison(e, pieces, "pivot", 0, al[1].second, 2, ar.flanc));
    e.contraintes.push_back(creerLiaison(e, pieces, "engrenage", 1, ar.flanc, 2, ar.flanc, 2));
    e.contraintes[0].pilotee = true; e.contraintes[0].valeur = 30;
    const bool ok = resoudreAssemblage(e, pieces).resolu;
    // angle réel de chaque arbre autour de Z monde (vecteur X local)
    auto angleZ = [](const ComposantAssemblage& c) { const gp_Dir x = monde(c, 1, 0, 0); return std::atan2(x.Y(), x.X()) * 180 / PI; };
    // rx = 180° retourne l'arbre autour de X : son axe X local reste X, rien à retrancher (0.25 retranchait 180°)
    return std::tuple{ok, angleZ(e.composants[1]), angleZ(e.composants[2])};
  };
  const auto [ok1, a1, b1] = engrenage(false);
  const auto [ok2, a2, b2] = engrenage(true);
  WARN("rapport saisi 2 (« dents menée / dents menante ») : roue A tourne de " << a1 << "°, roue B de " << b1 << "° ; avec l'arbre B monté retourné : A " << a2 << "°, B " << b2 << "°");
  CHECK(ok1);
  // cinématique d'un engrenage extérieur : sens opposés quel que soit le sens de montage des arbres
  CHECK(a1 * b1 < 0);
  CHECK(a2 * b2 < 0);
  // rapport « dents menée / dents menante » = 2 : la menée tourne deux fois MOINS vite
  CHECK(std::abs(b1) == Approx(std::abs(a1) / 2).margin(1e-2));
}

TEST_CASE("AS-11 normale d'une face issue d'une symétrie de pièce", "[AS-11]") {
  Bloc b = bloc(60, 40, 10);
  FonctionPiece sy; sy.type = TypeFonction::Symetrie; sy.supports = {"YZ"};
  const int isy = b.p.ajouter(sy);
  Bloc c = bloc(20, 20, 20);
  const std::vector<ResultatPiece> r = {construirePiece(b.p), construirePiece(c.p)};
  const std::string flancMiroir = std::to_string(isy) + ":" + b.cote(1);
  REQUIRE(r[0].facesNommees(flancMiroir).size() == 1);
  Assemblage a; a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(1, -200, 10, -5, 0, 0, 3)};
  a.contraintes = {ctr("coincidence", 0, flancMiroir, 1, c.cote(1))};
  const bool ok = resoudreAssemblage(a, r).resolu;
  const Boite bb = encombrement(placee(r[1], a.composants[1]));
  INFO("résolu " << ok << " ; cube posé en x[" << bb.xmin << ";" << bb.xmax << "] (attendu xmax = -60, à l'extérieur)");
  CHECK(ok);
  CHECK(bb.xmax == Approx(-60).margin(1e-3));
  // même défaut côté pièce : esquisse sur la face symétrique, extrusion de 5
  FonctionPiece e; e.type = TypeFonction::Esquisse; e.face = flancMiroir; e.esquisse.ajouterRectangle(5, -3, 15, 3);
  const int ie = b.p.ajouter(e);
  const int ix = extrusion(b.p, ie, 5);
  const ResultatPiece r2 = construirePiece(b.p);
  const Boite b2 = encombrement(r2.forme);
  INFO("bossage sur la face symétrique : erreur « " << r2.erreur(ix) << " » ; xmin " << b2.xmin << " (attendu -65) ; volume " << vol(r2) << " (attendu " << 48000 + 300 << ")");
  CHECK(b2.xmin == Approx(-65).margin(1e-3));
}

TEST_CASE("AS-20 contrainte contradictoire : le compromis est écrit dans le modèle", "[AS-20]") {
  Plaques P;
  Assemblage a; a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(0, 0, 0, 10)};
  a.contraintes = {ctr("coincidence", 0, P.b.face("sommet"), 1, P.b.face("base"), 0), ctr("coincidence", 0, P.b.face("sommet"), 1, P.b.face("base"), 10)};
  const bool ok = resoudreAssemblage(a, P.r).resolu;
  INFO("résolu " << ok << " ; z du composant après l'échec : " << a.composants[1].z << " (avant : 10)");
  CHECK_FALSE(ok);
  CHECK(a.composants[1].z == Approx(10));
}

TEST_CASE("AS-24 angle de 0° : degrés de liberté", "[AS-24]") {
  Plaques P;
  Assemblage a; a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(0, 100, 0, 0, 20, 10, 0)};
  a.contraintes = {ctr("angle", 0, P.b.face("sommet"), 1, P.b.face("sommet"), 0)};
  const bool ok = resoudreAssemblage(a, P.r).resolu;
  const AnalyseAssemblage an = analyserAssemblage(a, P.r);
  Assemblage b; b.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(0, 100, 0, 0, 20, 10, 0)};
  b.contraintes = {ctr("parallele", 0, P.b.face("sommet"), 1, P.b.face("sommet"))};
  resoudreAssemblage(b, P.r);
  const AnalyseAssemblage bn = analyserAssemblage(b, P.r);
  INFO("angle 0° : résolu " << ok << ", ddl " << an.degresLiberte << " ; parallèle : ddl " << bn.degresLiberte << " (attendu 4 dans les deux cas)");
  CHECK(an.degresLiberte == 4);
}

TEST_CASE("AS-25 deux composants fixes, contrainte violée", "[AS-25]") {
  Plaques P;
  Assemblage a; a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(0, 0, 0, 50, 0, 0, 0, true)};
  a.contraintes = {ctr("coincidence", 0, P.b.face("sommet"), 1, P.b.face("base"))};
  const EtatAssemblage e = resoudreAssemblage(a, P.r);
  const AnalyseAssemblage an = analyserAssemblage(a, P.r);
  INFO("message « " << e.message << " » ; contraintes non satisfaites selon l'analyse : " << an.nonSatisfaites.size());
  CHECK(an.nonSatisfaites.size() == 1);
}

TEST_CASE("AS-33 interférence avec un composé (sous-assemblage)", "[AS-33]") {
  TopoDS_Compound grand; BRep_Builder bb; bb.MakeCompound(grand); bb.Add(grand, BRepPrimAPI_MakeBox(10, 10, 10).Shape());
  const AnalyseContact c = analyserContact(grand, BRepPrimAPI_MakeBox(gp_Pnt(2, 2, 2), 3, 3, 3).Shape());
  INFO("état " << libelleContact(c.etat) << " ; volume commun " << c.volumeCommun << " ; distance " << c.distance);
  CHECK(c.etat == EtatContact::Interference);
  CHECK(c.volumeCommun == Approx(27).margin(1e-6));
}

TEST_CASE("AS-34 recouvrement de 0,5 µm entre grandes faces", "[AS-34]") {
  const TopoDS_Shape a = BRepPrimAPI_MakeBox(100, 100, 10).Shape();
  const TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 10 - 0.0005), 100, 100, 10).Shape();
  const AnalyseContact c = analyserContact(a, b);
  INFO("état « " << libelleContact(c.etat) << " » ; volume commun " << c.volumeCommun << " mm³ (tolérance de contact documentée : 1 µm)");
  CHECK(c.etat == EtatContact::Contact);
}

TEST_CASE("OK rang de l'analyse quand la pièce est loin de son origine", "[ok]") {
  for (double x0 : {1000.0, 100000.0}) {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.esquisse.ajouterRectangle(x0, 0, x0 + 60, 40);
    const int ie = p.ajouter(e); const int ix = extrusion(p, ie, 10);
    const std::vector<ResultatPiece> r = {construirePiece(p)};
    // faces : sommet, base, flanc avant (y=0)
    std::string flanc;
    for (const auto& [f, nom] : r[0].faces) { BRepAdaptor_Surface s(f); if (s.GetType() == GeomAbs_Plane && std::abs(s.Plane().Axis().Direction().Y()) > 0.99 && s.Plane().Location().Y() < 1) flanc = nom; }
    Assemblage a; a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(0, 0, 5, 10, 0, 0, 2)};
    a.contraintes = {ctr("coincidence", 0, std::to_string(ix) + ":sommet", 1, std::to_string(ix) + ":base"), ctr("parallele", 0, flanc, 1, flanc)};
    resoudreAssemblage(a, r);
    const AnalyseAssemblage an = analyserAssemblage(a, r);
    INFO("géométrie à x = " << x0 << " mm : ddl " << an.degresLiberte << " (attendu 2)");
    CHECK(an.degresLiberte == 2);
  }
}

TEST_CASE("AS-28 relecture JSON d'un assemblage : valeur non numérique, type inconnu", "[AS-28]") {
  Projet p = projetVide();
  p.sansCarte = true;
  Bloc b = bloc(10, 10, 10);
  p.pieces = {pieceVersJson(b.p)};
  Assemblage a; a.composants = {cmp(0, 0, 0, 0, 0, 0, 0, true), cmp(0, 0, 0, 10)};
  a.contraintes = {ctr("came", 0, b.face("sommet"), 1, b.face("base"))};
  p.assemblages = {a};
  Json j = versJson(p);
  {
    std::vector<std::string> av;
    const Projet q = normaliser(j, &av);
    INFO("type « came » relu comme « " << q.assemblages[0].contraintes[0].type << " » ; avertissements : " << av.size());
    CHECK(q.assemblages[0].contraintes[0].type == "came");
  }
  j["assemblages"][0]["composants"][1]["x"] = "12";
  std::vector<std::string> av;
  CHECK_NOTHROW(normaliser(j, &av));
}

TEST_CASE("PARTIEL propriétés physiques d'un assemblage : pas d'inertie", "[AS-partiel]") {
  // Constat : ProprietesAssemblage n'a ni tenseur d'inertie ni moments principaux (en-tête assemblage.hpp)
  ProprietesAssemblage pa;
  (void)pa;
  SUCCEED("structure : masse, volume, centre, pieces, provisoires, exclues — aucun champ d'inertie");
}

TEST_CASE("PERF solveur d'assemblage : 50 plaques empilées partant de l'orientation d'insertion", "[perf]") {
  Plaques P;
  Assemblage a;
  a.composants.push_back(cmp(0, 0, 0, 0, 0, 0, 0, true));
  for (int i = 1; i < 50; ++i) {
    a.composants.push_back(cmp(0, 3.0 * i, 2.0 * i, 12.0 * i));
    a.contraintes.push_back(ctr("coincidence", i - 1, P.b.face("sommet"), i, P.b.face("base")));
    a.contraintes.push_back(ctr("coincidence", i - 1, P.b.cote(0), i, P.b.cote(0), 0, true));
    a.contraintes.push_back(ctr("coincidence", i - 1, P.b.cote(3), i, P.b.cote(3), 0, true));
  }
  Chrono c; const EtatAssemblage e = resoudreAssemblage(a, P.r); const double t = c.ms();
  Chrono c2; const AnalyseAssemblage an = analyserAssemblage(a, P.r); const double t2 = c2.ms();
  WARN("50 plaques : résolu " << e.resolu << " en " << t << " ms ; analyse " << t2 << " ms ; ddl " << an.degresLiberte);
  CHECK(e.resolu);
}
