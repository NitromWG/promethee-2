// Essais de l'audit de Prométhée 0.25 : fonctions 3D et propriétés de masse (constats FO-xx, AP-24, EX-08).
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <fstream>

#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>

#include "aides.hpp"
#include "promethee/fabrication.hpp"

using namespace verif;

namespace {
double volExtrusionPolygone(const std::vector<std::pair<double, double>>& pts, double L) {
  Piece p; const int e = esquissePolygone(p, pts); extrusion(p, e, L);
  const ResultatPiece r = construirePiece(p);
  return vol(r);
}
}  // namespace

TEST_CASE("FO-01 profils minces d'un seul contour (cornière, U, tube carré)", "[FO-01]") {
  {
    Piece p; const int e = esquissePolygone(p, {{0, 0}, {40, 0}, {40, 3}, {3, 3}, {3, 40}, {0, 40}}); const int x = extrusion(p, e, 10);
    const ResultatPiece r = construirePiece(p);
    INFO("cornière 40×40×3 : erreur « " << r.erreur(x) << " », volume " << vol(r) << " (attendu 2 310)");
    CHECK(vol(r) == Approx(2310).epsilon(1e-6));
  }
  {
    Piece p; const int e = esquissePolygone(p, {{0, 0}, {40, 0}, {40, 20}, {38, 20}, {38, 2}, {2, 2}, {2, 20}, {0, 20}}); const int x = extrusion(p, e, 10);
    const ResultatPiece r = construirePiece(p);
    INFO("profil en U 40×20 ép. 2 : erreur « " << r.erreur(x) << " », volume " << vol(r) << " (attendu 1 520)");
    CHECK(vol(r) == Approx(1520).epsilon(1e-6));
  }
  {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse;
    e.esquisse.ajouterRectangle(0, 0, 40, 40); e.esquisse.ajouterRectangle(2, 2, 38, 38);
    const int ie = p.ajouter(e); const int x = extrusion(p, ie, 10);
    const ResultatPiece r = construirePiece(p);
    INFO("tube carré 40 ép. 2 : erreur « " << r.erreur(x) << " », volume " << vol(r) << " (attendu 3 040)");
    CHECK(vol(r) == Approx(3040).epsilon(1e-6));
  }
  {
    const double v = volExtrusionPolygone({{0, 0}, {40, 0}, {40, 6}, {6, 6}, {6, 40}, {0, 40}}, 10);
    INFO("témoin cornière 40×40×6 : volume " << v << " (attendu 4 440)");
    CHECK(v == Approx(4440).epsilon(1e-6));
  }
  {
    // tube rectangulaire du commerce 60×40×3
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse;
    e.esquisse.ajouterRectangle(0, 0, 60, 40); e.esquisse.ajouterRectangle(3, 3, 57, 37);
    const int ie = p.ajouter(e); const int x = extrusion(p, ie, 100);
    const ResultatPiece r = construirePiece(p);
    INFO("tube 60×40×3 : erreur « " << r.erreur(x) << " », volume " << vol(r) << " (attendu " << (2400 - 54 * 34) * 100 << ")");
    CHECK(vol(r) == Approx((2400.0 - 54 * 34) * 100).epsilon(1e-6));
  }
}

TEST_CASE("FO-03 ligne ouverte qui traverse un rectangle ou sert d'axe", "[FO-03]") {
  {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse;
    e.esquisse.ajouterRectangle(-10, 0, 10, 10);
    e.esquisse.ajouterLigne(e.esquisse.ajouterPoint(0, -5), e.esquisse.ajouterPoint(0, 15));
    const int ie = p.ajouter(e); const int x = extrusion(p, ie, 5);
    const ResultatPiece r = construirePiece(p);
    INFO("(a) rectangle traversé par une ligne : volume " << vol(r) << " (attendu 1 000) ; erreur « " << r.erreur(x) << " »");
    CHECK(vol(r) == Approx(1000).epsilon(1e-6));
  }
  {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.plan = PlanBase::XZ;
    e.esquisse.ajouterRectangle(-10, 0, -4, 3);
    const int axe = e.esquisse.ajouterLigne(e.esquisse.ajouterPoint(0, -5), e.esquisse.ajouterPoint(0, 10));
    const int ie = p.ajouter(e);
    FonctionPiece rv; rv.type = TypeFonction::Revolution; rv.esquisseId = ie; rv.axe = axe; rv.angle = 360;
    const int ir = p.ajouter(rv);
    const ResultatPiece r = construirePiece(p);
    INFO("(b) profil à gauche d'un axe tracé en trait plein : volume " << vol(r) << " (attendu " << PI * (100 - 16) * 3 << ") ; erreur « " << r.erreur(ir) << " »");
    CHECK(vol(r) == Approx(PI * (100 - 16) * 3).epsilon(1e-4));
  }
  {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.plan = PlanBase::XZ;
    e.esquisse.ajouterRectangle(4, 0, 10, 3);
    const int axe = e.esquisse.ajouterLigne(e.esquisse.ajouterPoint(0, -5), e.esquisse.ajouterPoint(0, 10));
    const int ie = p.ajouter(e);
    FonctionPiece rv; rv.type = TypeFonction::Revolution; rv.esquisseId = ie; rv.axe = axe; rv.angle = 360;
    p.ajouter(rv);
    const ResultatPiece r = construirePiece(p);
    INFO("(b-témoin) profil à droite : volume " << vol(r));
    CHECK(vol(r) == Approx(PI * (100 - 16) * 3).epsilon(1e-4));
  }
  {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse;
    e.esquisse.ajouterRectangle(0, 0, 20, 10);
    e.esquisse.ajouterLigne(e.esquisse.ajouterPoint(25, -5), e.esquisse.ajouterPoint(25, 15));
    const int ie = p.ajouter(e); const int x = extrusion(p, ie, 5);
    const ResultatPiece r = construirePiece(p);
    INFO("(c) rectangle + ligne isolée à droite : volume " << vol(r) << " (attendu 1 000) ; erreur « " << r.erreur(x) << " »");
    CHECK(vol(r) == Approx(1000).epsilon(1e-6));
  }
}

TEST_CASE("FO-04 fonction placée avant son esquisse (Monter/Descendre)", "[FO-04]") {
  Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.plan = PlanBase::XZ;
  e.esquisse.ajouterRectangle(0, 0, 10, 10);
  const int ie = p.ajouter(e); const int x = extrusion(p, ie, 5);
  const ResultatPiece r1 = construirePiece(p);
  REQUIRE_FALSE(r1.forme.IsNull());
  const Boite b1 = encombrement(r1.forme);
  std::iter_swap(p.fonctions.begin(), p.fonctions.begin() + 1);
  const ResultatPiece r2 = construirePiece(p);
  const Boite b2 = encombrement(r2.forme);
  INFO("avant : y[" << b1.ymin << ";" << b1.ymax << "] z[" << b1.zmin << ";" << b1.zmax << "] ; après échange : y[" << b2.ymin << ";" << b2.ymax << "] z[" << b2.zmin << ";" << b2.zmax << "] ; erreur « " << r2.erreur(x) << " »");
  const bool erreurExplicite = !r2.erreur(x).empty();
  const bool memeBoite = !r2.forme.IsNull() && std::abs(b2.ymin - b1.ymin) < 1e-6 && std::abs(b2.zmax - b1.zmax) < 1e-6;
  CHECK((erreurExplicite || memeBoite));
}

TEST_CASE("FO-05a trous renumérotés : un chanfrein passe sur un autre trou", "[FO-05]") {
  Bloc b = bloc(60, 40, 10);
  FonctionPiece s; s.type = TypeFonction::Esquisse; s.face = b.face("sommet");
  const int pA = s.esquisse.ajouterPoint(10, 20); s.esquisse.ajouterPoint(30, 20); s.esquisse.ajouterPoint(50, 20);
  const int is = b.p.ajouter(s);
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "simple"; t.diametre = 6; t.traversant = true;
  b.p.ajouter(t);
  ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  const TopoDS_Edge eB = cercleEn(r.forme, gp_Pnt(30, 20, 10), 3);
  REQUIRE_FALSE(eB.IsNull());
  FonctionPiece c; c.type = TypeFonction::Chanfrein; c.distance = 1; c.references = {r.nomArete(eB)};
  b.p.ajouter(c);
  r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  REQUIRE_FALSE(cercleEn(r.forme, gp_Pnt(30, 20, 10), 4).IsNull());
  b.p.fonction(is)->esquisse.supprimer(pA);
  r = construirePiece(b.p);
  const bool surB = !cercleEn(r.forme, gp_Pnt(30, 20, 10), 4).IsNull();
  const bool surC = !cercleEn(r.forme, gp_Pnt(50, 20, 10), 4).IsNull();
  INFO("après suppression du point A : erreurs « " << erreurs(r) << " » ; chanfrein sur B " << surB << " ; sur C " << surC);
  CHECK((surB || !r.erreurs.empty()));
  CHECK_FALSE((surC && r.erreurs.empty()));
}

TEST_CASE("FO-05b répétition à deux directions : la copie k change de place", "[FO-05]") {
  Bloc b = bloc(60, 40, 10);
  FonctionPiece s; s.type = TypeFonction::Esquisse; s.face = b.face("sommet"); s.esquisse.ajouterPoint(10, 10);
  const int is = b.p.ajouter(s);
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "simple"; t.diametre = 4; t.traversant = true;
  const int it = b.p.ajouter(t);
  FonctionPiece rp; rp.type = TypeFonction::Repetition; rp.typeRepetition = "lineaire"; rp.sources = {it};
  rp.direction1 = "X"; rp.pas1 = 15; rp.nombre1 = 3; rp.direction2 = "Y"; rp.pas2 = 10; rp.nombre2 = 2;
  const int irp = b.p.ajouter(rp);
  ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  const TopoDS_Edge e = cercleEn(r.forme, gp_Pnt(25, 20, 10), 2);
  REQUIRE_FALSE(e.IsNull());
  FonctionPiece c; c.type = TypeFonction::Chanfrein; c.distance = 0.5; c.references = {r.nomArete(e)};
  b.p.ajouter(c);
  r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  REQUIRE_FALSE(cercleEn(r.forme, gp_Pnt(25, 20, 10), 2.5).IsNull());
  b.p.fonction(irp)->nombre2 = 3;
  r = construirePiece(b.p);
  const bool ok = !cercleEn(r.forme, gp_Pnt(25, 20, 10), 2.5).IsNull();
  const bool deplace = !cercleEn(r.forme, gp_Pnt(25, 10, 10), 2.5).IsNull();
  INFO("après passage à 3 exemplaires en Y : chanfrein en (25;20) " << ok << ", en (25;10) " << deplace << " ; erreurs « " << erreurs(r) << " »");
  CHECK((ok || !r.erreurs.empty()));
  CHECK_FALSE((deplace && r.erreurs.empty()));
}

TEST_CASE("FO-05c régions renumérotées : point au centre de « sommet.1 »", "[FO-05]") {
  Bloc b = bloc(60, 20, 5);
  FonctionPiece s; s.type = TypeFonction::Esquisse; s.face = b.face("sommet");
  const int c1 = s.esquisse.ajouterCercle(s.esquisse.ajouterPoint(10, 10), 4);
  s.esquisse.ajouterCercle(s.esquisse.ajouterPoint(30, 10), 4);
  s.esquisse.ajouterCercle(s.esquisse.ajouterPoint(50, 10), 4);
  const int is = b.p.ajouter(s);
  const int ib = extrusion(b.p, is, 10);
  // Nom de la face du dessus du cylindre du milieu, tel que la construction le donne (0.25 : « sommet.1 », rang de
  // la région ; depuis la correction, la clé de la région)
  std::string sommetMilieu;
  {
    const ResultatPiece r0 = construirePiece(b.p);
    for (const auto& [f, nom] : r0.faces) {
      if (nom.rfind(std::to_string(ib) + ":sommet", 0) != 0) continue;
      GProp_GProps g; BRepGProp::SurfaceProperties(f, g);
      if (std::abs(g.CentreOfMass().X() - 30) < 1e-6) sommetMilieu = nom;
    }
  }
  REQUIRE_FALSE(sommetMilieu.empty());
  FonctionPiece pt; pt.type = TypeFonction::Point; pt.mode = "centre"; pt.supports = {sommetMilieu};
  b.p.ajouter(pt);
  ResultatPiece r = construirePiece(b.p);
  REQUIRE(r.erreurs.empty());
  REQUIRE(r.pointsReference.size() == 1);
  const gp_Pnt q1 = r.pointsReference[0].second;
  INFO("avant : (" << q1.X() << ";" << q1.Y() << ";" << q1.Z() << ")");
  CHECK(q1.X() == Approx(30).margin(1e-6));
  b.p.fonction(is)->esquisse.supprimer(c1);
  r = construirePiece(b.p);
  const bool erreur = !r.erreurs.empty();
  const double x2 = r.pointsReference.empty() ? -1 : r.pointsReference[0].second.X();
  INFO("après suppression du premier cercle : x = " << x2 << " ; erreurs « " << erreurs(r) << " »");
  CHECK((erreur || std::abs(x2 - 30) < 1e-6));
}

TEST_CASE("FO-06 région choisie disparue après un congé d'esquisse", "[FO-06]") {
  Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse;
  const auto l = e.esquisse.ajouterRectangle(0, 0, 20, 20);
  e.esquisse.ajouterCercle(e.esquisse.ajouterPoint(40, 10), 5);
  const int ie = p.ajouter(e);
  const auto regs = regionsEsquisse(*p.fonction(ie), repereEsquisse(PlanBase::XY, 0));
  REQUIRE(regs.size() == 2);
  FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ie; x.longueur = 10;
  for (const auto& rg : regs) x.regions.push_back(rg.cle);
  const int ix = p.ajouter(x);
  ResultatPiece r = construirePiece(p);
  REQUIRE(vol(r) == Approx(4000 + 250 * PI).epsilon(1e-6));
  Esquisse& es = p.fonction(ie)->esquisse;
  REQUIRE(es.congeEsquisse(es.element(l[1])->p2, 3));
  resoudre(es);
  r = construirePiece(p);
  const double attendu = (400 - (9 - 9 * PI / 4) + 25 * PI) * 10;
  INFO("après congé : volume " << vol(r) << " (attendu " << attendu << ") ; erreur « " << r.erreur(ix) << " »");
  CHECK((vol(r) == Approx(attendu).epsilon(1e-4) || !r.erreur(ix).empty()));
}

TEST_CASE("FO-07a enlèvement « jusqu'au prochain » à travers deux couches", "[FO-07]") {
  Bloc b = bloc(40, 20, 20);
  FonctionPiece t; t.type = TypeFonction::Esquisse; t.plan = PlanBase::XZ; t.esquisse.ajouterRectangle(5, 8, 35, 12);
  const int it = b.p.ajouter(t);
  FonctionPiece xt; xt.type = TypeFonction::Extrusion; xt.esquisseId = it; xt.enlever = true; xt.fin = "traversant"; xt.inverse = true;
  b.p.ajouter(xt);
  ResultatPiece r = construirePiece(b.p);
  INFO("tunnel : " << erreurs(r));
  REQUIRE(vol(r) == Approx(13600).epsilon(1e-6));
  const int ic = esquisseCercles(b.p, "", {{20, 10, 2.5}});
  FonctionPiece xp; xp.type = TypeFonction::Extrusion; xp.esquisseId = ic; xp.enlever = true; xp.fin = "prochain";
  const int ip = b.p.ajouter(xp);
  r = construirePiece(b.p);
  INFO("jusqu'au prochain : volume " << vol(r) << " (attendu 13 442,92 ; à travers tout donnerait 13 285,84) ; erreur « " << r.erreur(ip) << " »");
  CHECK(vol(r) == Approx(13600 - PI * 6.25 * 8).epsilon(1e-5));
}

TEST_CASE("FO-07b bossage « jusqu'au prochain » vers une face courbe", "[FO-07]") {
  Piece p;
  const int e1 = esquisseCercles(p, "", {{0, 30, 20}}, PlanBase::YZ);
  extrusion(p, e1, 40);
  const int e2 = esquisseCercles(p, "", {{20, 0, 2}});
  FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = e2; x.fin = "prochain";
  const int ix = p.ajouter(x);
  const ResultatPiece r = construirePiece(p);
  const Boite bb = encombrement(r.forme);
  INFO("erreur « " << r.erreur(ix) << " » ; solides " << nombreSolides(r.forme) << " ; défaut « " << defautGeometrie(r.forme) << " » ; boîte z[" << bb.zmin << ";" << bb.zmax << "] ; volume " << vol(r) << " (cylindre seul " << PI * 400 * 40 << ")");
  CHECK(nombreSolides(r.forme) == 1);
  CHECK(defautGeometrie(r.forme).empty());
}

TEST_CASE("FO-08 esquisse sur face inclinée : retournement du repère à |n·X| = 0,9", "[FO-08]") {
  std::vector<double> signes;
  for (double t : {0.45, 0.50}) {
    Piece p; std::vector<int> l;
    const int e = esquissePolygone(p, {{0, 0}, {20, 0}, {20 - 20 * t, 20}, {0, 20}}, &l);
    const int ix = extrusion(p, e, 10);
    FonctionPiece s; s.type = TypeFonction::Esquisse; s.face = std::to_string(ix) + ":cote:" + std::to_string(l[1]);
    s.esquisse.ajouterCercle(s.esquisse.ajouterPoint(3, 3), 1.5);
    const int is = p.ajouter(s);
    const ResultatPiece r0 = construirePiece(p);
    const gp_Ax3 ax = r0.repere(is);
    signes.push_back(ax.YDirection().Z());
    INFO("t = " << t << " : normale (" << ax.Direction().X() << ";" << ax.Direction().Y() << ") ; X local (" << ax.XDirection().X() << ";" << ax.XDirection().Y() << ";" << ax.XDirection().Z() << ") ; Y local . Z = " << ax.YDirection().Z());
    CHECK(ax.YDirection().Z() > 0);
  }
  REQUIRE(signes.size() == 2);
  CHECK(signes[0] * signes[1] > 0);
}

TEST_CASE("FO-12 dépouille + à travers tout", "[FO-12]") {
  Bloc b = bloc(100, 100, 20);
  const int ic = esquisseCercles(b.p, b.face("sommet"), {{50, 50, 2.5}});
  FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ic; x.enlever = true; x.fin = "traversant"; x.inverse = true; x.depouille = 2;
  const int ix = b.p.ajouter(x);
  const ResultatPiece r = construirePiece(b.p);
  INFO("erreur « " << r.erreur(ix) << " » ; volume retiré " << 200000 - vol(r));
  CHECK(r.erreur(ix).empty());
  Bloc t = bloc(100, 100, 20);
  const int it = esquisseCercles(t.p, t.face("sommet"), {{50, 50, 2.5}});
  FonctionPiece y; y.type = TypeFonction::Extrusion; y.esquisseId = it; y.enlever = true; y.longueur = 20; y.inverse = true; y.depouille = 2;
  const int iy = t.p.ajouter(y);
  const ResultatPiece rt = construirePiece(t.p);
  INFO("témoin borgne 20 : erreur « " << rt.erreur(iy) << " » ; volume retiré " << 200000 - vol(rt));
  CHECK(rt.erreur(iy).empty());
}

TEST_CASE("FO-13 perçage dans le vide (esquisse sur XY sous la plaque)", "[FO-13]") {
  Bloc b = bloc(60, 40, 10);
  const int is = esquissePoints(b.p, "", {{30, 20}});
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "simple"; t.diametre = 6; t.traversant = true;
  const int it = b.p.ajouter(t);
  const ResultatPiece r = construirePiece(b.p);
  INFO("erreur « " << r.erreur(it) << " » ; volume " << vol(r) << " (sans trou : 24 000)");
  CHECK((!r.erreur(it).empty() || vol(r) < 24000 - 100));
}

TEST_CASE("FO-14 enlèvement qui retire toute la matière", "[FO-14]") {
  Bloc b = bloc(10, 10, 10);
  FonctionPiece e; e.type = TypeFonction::Esquisse; e.esquisse.ajouterRectangle(-5, -5, 15, 15);
  const int ie = b.p.ajouter(e);
  FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ie; x.enlever = true; x.fin = "traversant";
  const int ix = b.p.ajouter(x);
  const ResultatPiece r = construirePiece(b.p);
  INFO("erreur « " << r.erreur(ix) << " » ; solides " << nombreSolides(r.forme) << " ; statut masse " << libelleStatut(proprietesMasse(r.forme, 1).statut));
  CHECK((!r.erreur(ix).empty() || nombreSolides(r.forme) >= 1));
}

TEST_CASE("FO-15 à travers tout depuis une esquisse éloignée", "[FO-15]") {
  Bloc b = bloc(10, 10, 10);
  const int ic = esquisseCercles(b.p, "", {{5, 5, 2.5}}, PlanBase::XY, 115);
  FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ic; x.enlever = true; x.fin = "traversant"; x.inverse = true;
  const int ix = b.p.ajouter(x);
  const ResultatPiece r = construirePiece(b.p);
  INFO("retiré " << 1000 - vol(r) << " mm³ (attendu 196,35) ; erreur « " << r.erreur(ix) << " »");
  CHECK(1000 - vol(r) == Approx(196.35).epsilon(1e-3));
}

TEST_CASE("FO-16 seconde direction de répétition introuvable", "[FO-16]") {
  Bloc b = bloc(60, 40, 10);
  FonctionPiece s; s.type = TypeFonction::Esquisse; s.face = b.face("sommet"); s.esquisse.ajouterPoint(10, 10);
  const int is = b.p.ajouter(s);
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "simple"; t.diametre = 4; t.traversant = true;
  const int it = b.p.ajouter(t);
  FonctionPiece rp; rp.type = TypeFonction::Repetition; rp.typeRepetition = "lineaire"; rp.sources = {it};
  rp.direction1 = "X"; rp.pas1 = 15; rp.nombre1 = 3; rp.direction2 = "1:cote:999|1:sommet"; rp.pas2 = 10; rp.nombre2 = 2;
  const int irp = b.p.ajouter(rp);
  const ResultatPiece r = construirePiece(b.p);
  INFO("erreur « " << r.erreur(irp) << " » ; volume " << vol(r) << " (3 trous : " << 24000 - 3 * PI * 4 * 10 << ")");
  CHECK_FALSE(r.erreur(irp).empty());
}

TEST_CASE("FO-17 repère dans un fichier", "[FO-17]") {
  Bloc b = bloc(10, 10, 10);
  Json j = pieceVersJson(b.p);
  Json rep = {{"id", 50}, {"type", "repere"}, {"nom", "R"}, {"mode", ""}, {"supports", Json::array()}};
  j["fonctions"].insert(j["fonctions"].begin() + 1, rep);
  const Piece q = pieceDepuisJson(j);
  const ResultatPiece r = construirePiece(q);
  INFO("erreurs « " << erreurs(r) << " » ; volume " << vol(r));
  CHECK(vol(r) == Approx(1000).epsilon(1e-6));
}

TEST_CASE("FO-18 fraisure ANSI : angle du cône", "[FO-18]") {
  for (const std::string norme : {"ISO", "ANSI"}) {
    Bloc b = bloc(60, 40, 10);
    FonctionPiece s; s.type = TypeFonction::Esquisse; s.face = b.face("sommet"); s.esquisse.ajouterPoint(30, 20);
    const int is = b.p.ajouter(s);
    FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "fraise"; t.norme = norme; t.taille = norme == "ISO" ? "M5" : "1/4-20"; t.traversant = true;
    const int it = b.p.ajouter(t);
    const ResultatPiece r = construirePiece(b.p);
    double angle = -1;
    for (TopExp_Explorer ex(r.forme, TopAbs_FACE); ex.More(); ex.Next()) {
      BRepAdaptor_Surface s2(TopoDS::Face(ex.Current()));
      if (s2.GetType() == GeomAbs_Cone) angle = 2 * std::abs(s2.Cone().SemiAngle()) * 180 / PI;
    }
    INFO(norme << " : erreur « " << r.erreur(it) << " », angle de fraisure " << angle << "°");
    CHECK(angle == Approx(norme == "ISO" ? 90.0 : 82.0).margin(0.5));
  }
}

TEST_CASE("FO-19 esquisse désactivée sous un perçage", "[FO-19]") {
  Bloc b = bloc(60, 40, 10);
  const int is = esquissePoints(b.p, b.face("sommet"), {{30, 20}});
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "simple"; t.diametre = 6; t.traversant = true;
  const int it = b.p.ajouter(t);
  ResultatPiece r = construirePiece(b.p);
  REQUIRE(vol(r) == Approx(24000 - PI * 9 * 10).epsilon(1e-6));
  b.p.fonction(is)->desactivee = true;
  r = construirePiece(b.p);
  INFO("esquisse désactivée : erreur « " << r.erreur(it) << " » ; volume " << vol(r));
  CHECK_FALSE(r.erreur(it).empty());
}

TEST_CASE("FO-09 lissage entre deux carrés tracés dans un ordre différent", "[FO-09]") {
  Piece p;
  FonctionPiece a; a.type = TypeFonction::Esquisse; a.esquisse.ajouterRectangle(-10, -10, 10, 10);
  const int ia = p.ajouter(a);
  FonctionPiece b; b.type = TypeFonction::Esquisse; b.decalage = 20;
  polygone(b.esquisse, {{5, 5}, {-5, 5}, {-5, -5}, {5, -5}});
  const int ib = p.ajouter(b);
  FonctionPiece l; l.type = TypeFonction::Lissage; l.sources = {ia, ib};
  const int il = p.ajouter(l);
  const ResultatPiece r = construirePiece(p);
  INFO("erreur « " << r.erreur(il) << " » ; volume " << vol(r) << " (tronc de pyramide : 4 666,67) ; défaut « " << defautGeometrie(r.forme) << " »");
  CHECK(vol(r) == Approx(20.0 / 3 * (400 + 100 + 200)).epsilon(1e-3));
}

TEST_CASE("FO-20 plan milieu avec dépouille vs deux directions égales", "[FO-20]") {
  auto faire = [](bool milieu) {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.esquisse.ajouterRectangle(-10, -10, 10, 10);
    const int ie = p.ajouter(e);
    FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ie; x.depouille = 5;
    if (milieu) { x.fin = "milieu"; x.longueur = 10; } else { x.longueur = 5; x.fin2 = "borgne"; x.longueur2 = 5; }
    p.ajouter(x);
    return vol(construirePiece(p));
  };
  const double a = faire(true), b = faire(false);
  WARN("plan milieu + dépouille 5° : " << a << " mm³ ; deux directions de 5 mm + dépouille : " << b << " mm³");
  CHECK(a == Approx(b).epsilon(1e-3));
}

TEST_CASE("FO-21 fonction mince : contour en haltère qui se scinde", "[FO-21]") {
  Piece p; const int e = esquissePolygone(p, {{0, 0}, {20, 0}, {20, 9}, {30, 9}, {30, 0}, {50, 0}, {50, 20}, {30, 20}, {30, 11}, {20, 11}, {20, 20}, {0, 20}});
  FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = e; x.longueur = 10; x.mince = true; x.epaisseurMince = 3; x.sensMince = "interieur";
  const int ix = p.ajouter(x);
  const ResultatPiece r = construirePiece(p);
  INFO("erreur « " << r.erreur(ix) << " » ; volume " << vol(r) << " (≈ 4 270 si deux lobes creux, ≈ 6 240 si un lobe reste plein)");
  CHECK(vol(r) < 5000);
}

TEST_CASE("FO-22 révolution autour d'un axe perpendiculaire au plan d'esquisse", "[FO-22]") {
  Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.esquisse.ajouterRectangle(4, 0, 10, 3);
  const int ie = p.ajouter(e);
  FonctionPiece rv; rv.type = TypeFonction::Revolution; rv.esquisseId = ie; rv.axeBase = "Z"; rv.angle = 360;
  const int ir = p.ajouter(rv);
  const ResultatPiece r = construirePiece(p);
  INFO("erreur « " << r.erreur(ir) << " » ; volume " << vol(r) << " ; statut " << libelleStatut(proprietesMasse(r.forme, 1).statut));
  CHECK((!r.erreur(ir).empty() || vol(r) > 1));
}

TEST_CASE("EX-08 import STL : coque ou solide ?", "[EX-08]") {
  Bloc b = bloc(60, 40, 10);
  const ResultatPiece r0 = construirePiece(b.p);
  const std::string f = dossierSorties() + "/plaque.stl";
  exporterStl(r0.forme, f, 0.01);
  std::string err; const std::string brep = lireFichierCAO(f, &err);
  REQUIRE_FALSE(brep.empty());
  Piece p; FonctionPiece im; im.type = TypeFonction::Import; im.donnees = brep; p.ajouter(im);
  const ResultatPiece r = construirePiece(p);
  const ProprietesMasse pm = proprietesMasse(r.forme, 1);
  INFO("solides " << nombreSolides(r.forme) << " ; statut masse « " << libelleStatut(pm.statut) << " » ; volume " << pm.volume << " ; taille du texte BRep " << brep.size() << " octets pour un STL de " << std::filesystem::file_size(f));
  CHECK(nombreSolides(r.forme) == 1);
  // et un enlèvement sur cette pièce importée
  const int ic = esquisseCercles(p, "", {{30, 20, 5}}, PlanBase::XY);
  FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ic; x.enlever = true; x.fin = "traversant"; x.inverse = false;
  const int ix = p.ajouter(x);
  const ResultatPiece r2 = construirePiece(p);
  INFO("enlèvement sur le STL importé : erreur « " << r2.erreur(ix) << " »");
  CHECK(r2.erreur(ix).empty());
}

TEST_CASE("FO-23 produits d'inertie et moments principaux d'une cornière épaisse", "[FO-23]") {
  Piece p; p.densite = 1; p.densiteConfirmee = true;
  const int e = esquissePolygone(p, {{0, 0}, {30, 0}, {30, 10}, {10, 10}, {10, 30}, {0, 30}});
  extrusion(p, e, 10);
  const ResultatPiece r = construirePiece(p);
  const ProprietesMasse pm = proprietesMasse(r.forme, 1.0);
  WARN("volume " << pm.volume << " ; centre (" << pm.cx << ";" << pm.cy << ";" << pm.cz << ") ; Ixx " << pm.ixx << " Iyy " << pm.iyy << " Izz " << pm.izz << " ; Ixy " << pm.ixy << " Ixz " << pm.ixz << " Iyz " << pm.iyz << " ; principaux " << pm.i1 << " " << pm.i2 << " " << pm.i3);
  CHECK(pm.volume == Approx(5000));
  CHECK((pm.i1 <= pm.i2 && pm.i2 <= pm.i3));
}

TEST_CASE("FO-24/FO-10/FO-11 fonctions minces : coins extérieurs, contour troué, contour ouvert", "[FO-10][FO-11][FO-24]") {
  {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.esquisse.ajouterRectangle(0, 0, 20, 20);
    const int ie = p.ajouter(e);
    FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ie; x.longueur = 10; x.mince = true; x.epaisseurMince = 2; x.sensMince = "exterieur";
    p.ajouter(x);
    WARN("FO-24 mince extérieur : volume " << vol(construirePiece(p)) << " (coins vifs 1 760 ; arrondis 1 725,66)");
  }
  {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.esquisse.ajouterRectangle(0, 0, 40, 40);
    e.esquisse.ajouterCercle(e.esquisse.ajouterPoint(20, 20), 10);
    const int ie = p.ajouter(e);
    FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ie; x.longueur = 10; x.mince = true; x.epaisseurMince = 2; x.sensMince = "interieur";
    p.ajouter(x);
    const double v = vol(construirePiece(p));
    INFO("FO-10 mince sur un carré troué : volume " << v << " (paroi le long des deux contours : 4 422,3 ; contour extérieur seul : 3 040)");
    CHECK(v == Approx(3040 + PI * (144 - 100) * 10).epsilon(1e-3));
  }
  {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.esquisse.ajouterLigne(e.esquisse.ajouterPoint(0, 0), e.esquisse.ajouterPoint(20, 0));
    const int ie = p.ajouter(e);
    FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ie; x.longueur = 5; x.mince = true; x.epaisseurMince = 2; x.sensMince = "exterieur";
    const int ix = p.ajouter(x);
    const ResultatPiece r = construirePiece(p);
    INFO("FO-11 mince ouvert « extérieur » : volume " << vol(r) << " ; erreur « " << r.erreur(ix) << " »");
    REQUIRE_FALSE(r.forme.IsNull());
    const Boite bb = encombrement(r.forme);
    INFO("boîte y[" << bb.ymin << ";" << bb.ymax << "]");
    CHECK(bb.ymin == Approx(0).margin(1e-6));
  }
}

TEST_CASE("FO-25 chanfrein à deux distances : sur quelle face tombe la première distance ?", "[FO-25]") {
  Bloc b = bloc(20, 20, 20);
  const std::string base = b.face("base"), sommet = b.face("sommet"), c0 = b.cote(0);
  auto arete = [](const std::string& a, const std::string& c) { return a < c ? a + "|" + c : c + "|" + a; };
  for (const std::string& autre : {base, sommet}) {
    Bloc q = b;
    FonctionPiece ch; ch.type = TypeFonction::Chanfrein; ch.typeChanfrein = "distances"; ch.distance = 2; ch.distance2 = 4; ch.references = {arete(c0, autre)};
    const int ic = q.p.ajouter(ch);
    const ResultatPiece r = construirePiece(q.p);
    Bnd_Box bb;
    for (const auto& [f, nom] : r.faces) if (nom.find(":chanfrein") != std::string::npos) BRepBndLib::Add(f, bb);
    double x0, y0, z0, x1, y1, z1; bb.Get(x0, y0, z0, x1, y1, z1);
    WARN("arête flanc avant | " << (autre == base ? "dessous" : "dessus") << " : erreur « " << r.erreur(ic) << " » ; face du chanfrein : profondeur en Y " << (y1 - y0) << " mm, hauteur en Z " << (z1 - z0) << " mm");
  }
  SUCCEED();
}

TEST_CASE("FO-26 lamage plus profond que le trou borgne", "[FO-26]") {
  Bloc b = bloc(60, 40, 20);
  const int is = esquissePoints(b.p, b.face("sommet"), {{30, 20}});
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "lame"; t.taille = "M5"; t.traversant = false; t.profondeur = 3;
  const int it = b.p.ajouter(t);
  const ResultatPiece r = construirePiece(b.p);
  WARN("lamage M5 (5,4 mm de profondeur) sur un trou de 3 mm : erreur « " << r.erreur(it) << " » ; défaut B-Rep « " << defautGeometrie(r.forme) << " » ; volume retiré " << 48000 - vol(r));
  CHECK_FALSE(r.erreur(it).empty());
}

TEST_CASE("AP-24 tube d'épaisseur ≥ rayon : plein sans message", "[AP-24]") {
  Piece p;
  FonctionPiece c; c.type = TypeFonction::Esquisse3D; c.points3d = {{0, 0, 0}, {0, 0, 50}};
  const int ic = p.ajouter(c);
  FonctionPiece t; t.type = TypeFonction::Balayage; t.tube = true; t.diametre = 10; t.epaisseur = 7; t.cheminId = ic;
  const int it = p.ajouter(t);
  const ResultatPiece r = construirePiece(p);
  INFO("épaisseur 7 sur Ø10 : erreur « " << r.erreur(it) << " » ; volume " << vol(r) << " (plein : " << PI * 25 * 50 << ")");
  CHECK_FALSE(r.erreur(it).empty());
}

TEST_CASE("OK balayage dont le profil n'est pas au début du chemin", "[ok]") {
  Piece p;
  const int prof = esquisseCercles(p, "", {{10, 0, 2}});
  FonctionPiece ch; ch.type = TypeFonction::Esquisse; ch.plan = PlanBase::XZ;
  ch.esquisse.ajouterLigne(ch.esquisse.ajouterPoint(0, 0), ch.esquisse.ajouterPoint(0, 30));
  const int ich = p.ajouter(ch);
  FonctionPiece b; b.type = TypeFonction::Balayage; b.esquisseId = prof; b.cheminId = ich;
  const int ib = p.ajouter(b);
  const ResultatPiece r = construirePiece(p);
  const Boite bb = encombrement(r.forme);
  WARN("erreur « " << r.erreur(ib) << " » ; volume " << vol(r) << " (π·4·30 = " << PI * 4 * 30 << ") ; boîte x[" << bb.xmin << ";" << bb.xmax << "] z[" << bb.zmin << ";" << bb.zmax << "]");
  SUCCEED();
}

TEST_CASE("OK révolution autour des axes X et Y (non testée par le projet)", "[ok]") {
  for (const std::string axe : {"X", "Y"}) {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.plan = PlanBase::XY;
    if (axe == "X") e.esquisse.ajouterRectangle(0, 4, 10, 6); else e.esquisse.ajouterRectangle(4, 0, 6, 10);
    const int ie = p.ajouter(e);
    FonctionPiece rv; rv.type = TypeFonction::Revolution; rv.esquisseId = ie; rv.axeBase = axe; rv.angle = 360;
    const int ir = p.ajouter(rv);
    const ResultatPiece r = construirePiece(p);
    INFO("axe " << axe << " : erreur « " << r.erreur(ir) << " » ; volume " << vol(r) << " (attendu " << PI * (36 - 16) * 10 << ")");
    CHECK(vol(r) == Approx(PI * 20 * 10).epsilon(1e-6));
  }
}

TEST_CASE("OK coque : épaisseurs multiples et validité", "[ok]") {
  Bloc b = bloc(60, 40, 30);
  FonctionPiece c; c.type = TypeFonction::Coque; c.epaisseur = 2; c.references = {b.face("sommet")}; c.epaisseursFaces = {{b.cote(0), 5}};
  const int ic = b.p.ajouter(c);
  const ResultatPiece r = construirePiece(b.p);
  const double attendu = 60.0 * 40 * 30 - (60 - 4) * (40 - 2 - 5) * (30 - 2);
  INFO("erreur « " << r.erreur(ic) << " » ; volume " << vol(r) << " (attendu " << attendu << ")");
  CHECK(vol(r) == Approx(attendu).epsilon(1e-3));
}

TEST_CASE("OK symétrie de fonctions (non testée par le projet)", "[ok]") {
  Bloc b = bloc(60, 40, 10);
  const int is = esquissePoints(b.p, b.face("sommet"), {{10, 10}});
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "simple"; t.diametre = 6; t.traversant = true;
  const int it = b.p.ajouter(t);
  // plan de symétrie : plan YZ décalé de 30 (milieu de la plaque)
  FonctionPiece pl; pl.type = TypeFonction::Plan; pl.mode = "decale"; pl.supports = {"YZ"}; pl.distance = 30;
  const int ipl = b.p.ajouter(pl);
  FonctionPiece s; s.type = TypeFonction::Symetrie; s.sources = {it}; s.supports = {"F:" + std::to_string(ipl)};
  const int isym = b.p.ajouter(s);
  const ResultatPiece r = construirePiece(b.p);
  INFO("erreur « " << r.erreur(isym) << " » ; volume " << vol(r) << " (attendu " << 24000 - 2 * PI * 9 * 10 << ") ; trou miroir en (50;10) : " << !cercleEn(r.forme, gp_Pnt(50, 10, 10), 3).IsNull());
  CHECK(vol(r) == Approx(24000 - 2 * PI * 9 * 10).epsilon(1e-6));
  CHECK_FALSE(cercleEn(r.forme, gp_Pnt(50, 10, 10), 3).IsNull());
}

TEST_CASE("OK répétition circulaire de perçages autour de Z", "[ok]") {
  Piece p; const int e = esquisseCercles(p, "", {{0, 0, 30}}); const int ix = extrusion(p, e, 5);
  const int is = esquissePoints(p, std::to_string(ix) + ":sommet", {{20, 0}});
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "simple"; t.diametre = 4; t.traversant = true;
  const int it = p.ajouter(t);
  FonctionPiece rp; rp.type = TypeFonction::Repetition; rp.typeRepetition = "circulaire"; rp.sources = {it}; rp.direction1 = "Z"; rp.nombre1 = 6; rp.angle = 360;
  const int irp = p.ajouter(rp);
  const ResultatPiece r = construirePiece(p);
  INFO("erreur « " << r.erreur(irp) << " » ; volume " << vol(r) << " (attendu " << PI * 900 * 5 - 6 * PI * 4 * 5 << ")");
  CHECK(vol(r) == Approx(PI * 900 * 5 - 6 * PI * 4 * 5).epsilon(1e-5));
}

TEST_CASE("OK dépouille par plan neutre sur les quatre flancs", "[ok]") {
  Bloc b = bloc(40, 40, 20);
  FonctionPiece d; d.type = TypeFonction::Depouille; d.planNeutre = "XY"; d.angle = 5;
  d.references = {b.cote(0), b.cote(1), b.cote(2), b.cote(3)};
  const int id = b.p.ajouter(d);
  const ResultatPiece r = construirePiece(b.p);
  const Boite bb = encombrement(r.forme);
  INFO("erreur « " << r.erreur(id) << " » ; volume " << vol(r) << " ; boîte x[" << bb.xmin << ";" << bb.xmax << "]");
  CHECK(r.erreur(id).empty());
  CHECK(vol(r) < 32000);
}

TEST_CASE("OK perçage taraudé : la géométrie est au diamètre de perçage, le filetage n'est qu'une donnée", "[ok]") {
  Bloc b = bloc(60, 40, 10);
  const int is = esquissePoints(b.p, b.face("sommet"), {{30, 20}});
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "taraude"; t.taille = "M6"; t.traversant = true;
  b.p.ajouter(t);
  const ResultatPiece r = construirePiece(b.p);
  const bool foret = !cercleEn(r.forme, gp_Pnt(30, 20, 10), 2.5).IsNull();
  WARN("M6 taraudé : arête Ø5 (foret) présente " << foret << " ; filetages déclarés " << r.filetages.size() << (r.filetages.empty() ? "" : " pas " + std::to_string(r.filetages[0].pas)));
  CHECK(foret);
}

TEST_CASE("OK congé trop grand puis fonctions suivantes : toutes obsolètes, rien ne le montre", "[ok]") {
  Bloc b = bloc(60, 40, 10);
  FonctionPiece c; c.type = TypeFonction::Conge; c.rayon = 50; c.references = {b.areteVerticale(0)};
  const int ic = b.p.ajouter(c);
  const int is = esquissePoints(b.p, b.face("sommet"), {{30, 20}});
  FonctionPiece t; t.type = TypeFonction::Percage; t.esquisseId = is; t.typePercage = "simple"; t.diametre = 6; t.traversant = true;
  const int it = b.p.ajouter(t);
  const ResultatPiece r = construirePiece(b.p);
  WARN("congé R50 : erreur « " << r.erreur(ic) << " » ; perçage obsolète " << r.obsolete(it) << " ; volume final " << vol(r));
  CHECK(r.obsolete(it));
}

TEST_CASE("FO-02 esquisse posée sur une face autre que le dessus : normale rentrante", "[FO-02]") {
  Bloc b = bloc(60, 40, 30);
  const ResultatPiece r = construirePiece(b.p);
  int fautes = 0;
  std::ostringstream j;
  for (const std::string& nom : std::initializer_list<std::string>{b.face("sommet"), b.face("base"), b.cote(0), b.cote(1), b.cote(2), b.cote(3)}) {
    std::vector<TopoDS_Face> fs; r.resoudreFaces(nom, fs, true);
    gp_Ax3 ax; repereFace(fs.front(), ax);
    // centre de la face exprimé dans le repère de l'esquisse
    Bnd_Box bb; BRepBndLib::Add(fs.front(), bb); double x0, y0, z0, x1, y1, z1; bb.Get(x0, y0, z0, x1, y1, z1);
    const gp_Pnt c((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    const gp_Vec v(ax.Location(), c);
    const double u0 = v.Dot(gp_Vec(ax.XDirection())), v0 = v.Dot(gp_Vec(ax.YDirection()));
    Piece q = b.p;
    FonctionPiece s; s.type = TypeFonction::Esquisse; s.face = nom; s.esquisse.ajouterRectangle(u0 - 2, v0 - 2, u0 + 2, v0 + 2);
    const int is = q.ajouter(s);
    const int ix = extrusion(q, is, 5);
    const ResultatPiece rq = construirePiece(q);
    const double ajout = vol(rq) - vol(r);
    j << nom << " : bossage 4×4×5 mm → " << ajout << " mm³ ajoutés (attendu 80) ; erreur « " << rq.erreur(ix) << " »\n";
    if (std::abs(ajout - 80) > 1e-6) ++fautes;
  }
  WARN(j.str());
  CHECK(fautes == 0);
}

TEST_CASE("FO-11 fonction mince sur contour ouvert : intérieur, extérieur, milieu", "[FO-11]") {
  for (const std::string sens : {"interieur", "exterieur", "milieu"}) {
    Piece p; FonctionPiece e; e.type = TypeFonction::Esquisse; e.esquisse.ajouterLigne(e.esquisse.ajouterPoint(0, 0), e.esquisse.ajouterPoint(20, 0));
    const int ie = p.ajouter(e);
    FonctionPiece x; x.type = TypeFonction::Extrusion; x.esquisseId = ie; x.longueur = 5; x.mince = true; x.epaisseurMince = 2; x.sensMince = sens;
    const int ix = p.ajouter(x);
    const ResultatPiece r = construirePiece(p);
    INFO("mince ouvert « " << sens << " » : volume " << vol(r) << " ; erreur « " << r.erreur(ix) << " »");
    CHECK(r.erreur(ix).empty());
  }
}

TEST_CASE("FO-27 encombrement d'une forme nulle : exception au lieu d'une boîte vide", "[FO-27]") {
  bool exception = false;
  try {
    (void)encombrement(TopoDS_Shape());
  } catch (...) {
    exception = true;  // Standard_ConstructionError « Bnd_Box is void », hors std::exception
  }
  INFO("encombrement(forme nulle) lève une exception : " << (exception ? "oui" : "non"));
  CHECK_FALSE(exception);
}

TEST_CASE("FO-12 bis dépouille jusqu'à une face inclinée", "[FO-12]") {
  // Chanfreins de 15 mm sur deux arêtes du dessus : un trou Ø 5 dépouillé monte du dessous jusqu'au chanfrein qui
  // le surplombe (celui des deux que l'extrusion sans dépouille atteint)
  auto essai = [](double depouille, int quel, std::string* erreur, bool inverse) {
    Bloc b = bloc(100, 100, 20);
    ResultatPiece r = construirePiece(b.p);
    auto arete = [&](int i) {
      const std::string a = b.face("sommet"), c = b.cote(i);
      return a < c ? a + "|" + c : c + "|" + a;
    };
    FonctionPiece ch;
    ch.type = TypeFonction::Chanfrein;
    ch.distance = 15;
    ch.references = {arete(0), arete(2)};
    const int ic = b.p.ajouter(ch);
    r = construirePiece(b.p);
    std::vector<std::string> inclinees;
    for (const auto& [f, nom] : r.faces)
      if (nom.rfind(std::to_string(ic) + ":", 0) == 0) inclinees.push_back(nom);
    if (quel >= static_cast<int>(inclinees.size())) return -1.0;
    const int ie = esquisseCercles(b.p, "", {{50, 7, 2.5}});  // plan XY : le dessous de la pièce, en coordonnées du monde
    FonctionPiece x;
    x.type = TypeFonction::Extrusion;
    x.esquisseId = ie;
    x.enlever = true;
    x.inverse = inverse;
    x.fin = "face";
    x.refFin = inclinees[static_cast<std::size_t>(quel)];
    x.depouille = depouille;
    const int ix = b.p.ajouter(x);
    const double avant = vol(r);
    r = construirePiece(b.p);
    *erreur = r.erreur(ix);
    return avant - vol(r);
  };
  int quel = -1;
  bool inverse = false;
  std::string err;
  for (int k = 0; k < 8 && quel < 0; ++k)
    if (const double v = essai(0, k / 2, &err, k % 2); v > 50 && err.empty()) { quel = k / 2; inverse = k % 2; }
    else UNSCOPED_INFO("face " << k / 2 << (k % 2 ? " inversée" : "") << " : " << v << " « " << err << " »");
  REQUIRE(quel >= 0);
  const double sansDepouille = essai(0, quel, &err, inverse);
  for (const double depouille : {2.0, -2.0}) {
    const double retire = essai(depouille, quel, &err, inverse);
    INFO("dépouille " << depouille << " : erreur « " << err << " » ; volume retiré " << retire << " (sans dépouille " << sansDepouille << ")");
    CHECK(err.empty());
    CHECK(retire > 0.5 * sansDepouille);
    CHECK(retire < 1.5 * sansDepouille);
    CHECK((depouille > 0 ? retire < sansDepouille : retire > sansDepouille));
  }
}
