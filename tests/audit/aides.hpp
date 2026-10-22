// Aides communes aux essais de vérification de l'audit (Prométhée 0.25).
#pragma once
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <gp_Trsf.hxx>

#include "promethee/assemblage.hpp"
#include "promethee/construction.hpp"
#include "promethee/esquisse.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/piece.hpp"

namespace verif {
using namespace prom;
using Catch::Approx;
constexpr double PI = std::numbers::pi;

struct Bloc {
  Piece p;
  int esquisse = 0, extrusion = 0;
  std::vector<int> lignes;  // bas (y=0), droite (x=L), haut (y=W), gauche (x=0)
  std::string face(const std::string& quoi) const { return std::to_string(extrusion) + ":" + quoi; }
  std::string cote(int i) const { return std::to_string(extrusion) + ":cote:" + std::to_string(lignes[static_cast<size_t>(i)]); }
  std::string areteVerticale(int i) const {
    const std::string a = cote(i), b = cote((i + 1) % 4);
    return a < b ? a + "|" + b : b + "|" + a;
  }
};

inline Bloc bloc(double L, double W, double H, double densite = 1.0) {
  Bloc b;
  b.p.densite = densite;
  b.p.densiteConfirmee = true;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  b.lignes = e.esquisse.ajouterRectangle(0.2, 0.1, L * 0.9, W * 1.2);
  e.esquisse.contraindre(TypeContrainte::Coincidence, {e.esquisse.element(b.lignes[0])->p1, 0});
  e.esquisse.contraindre(TypeContrainte::Longueur, {b.lignes[0]}, L);
  e.esquisse.contraindre(TypeContrainte::Longueur, {b.lignes[1]}, W);
  resoudre(e.esquisse);
  b.esquisse = b.p.ajouter(e);
  FonctionPiece x;
  x.type = TypeFonction::Extrusion;
  x.esquisseId = b.esquisse;
  x.longueur = H;
  b.extrusion = b.p.ajouter(x);
  return b;
}

inline int esquisseCercles(Piece& p, const std::string& face, const std::vector<std::array<double, 3>>& cercles, PlanBase plan = PlanBase::XY, double decalage = 0) {
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.face = face;
  e.plan = plan;
  e.decalage = decalage;
  for (const auto& [x, y, r] : cercles) e.esquisse.ajouterCercle(e.esquisse.ajouterPoint(x, y), r);
  return p.ajouter(e);
}

inline int esquissePoints(Piece& p, const std::string& face, const std::vector<std::pair<double, double>>& pts, PlanBase plan = PlanBase::XY) {
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.face = face;
  e.plan = plan;
  for (const auto& [x, y] : pts) e.esquisse.ajouterPoint(x, y);
  return p.ajouter(e);
}

inline int extrusion(Piece& p, int esquisse, double longueur, bool enlever = false, bool traversant = false, bool inverse = false) {
  FonctionPiece f;
  f.type = TypeFonction::Extrusion;
  f.esquisseId = esquisse;
  f.longueur = longueur;
  f.enlever = enlever;
  f.traversant = traversant;
  f.inverse = inverse;
  return p.ajouter(f);
}

inline std::vector<int> polygone(Esquisse& e, const std::vector<std::pair<double, double>>& pts) {
  std::vector<int> ids, l;
  for (auto [x, y] : pts) ids.push_back(e.ajouterPoint(x, y));
  for (size_t i = 0; i < ids.size(); ++i) l.push_back(e.ajouterLigne(ids[i], ids[(i + 1) % ids.size()]));
  return l;
}

inline int esquissePolygone(Piece& p, const std::vector<std::pair<double, double>>& pts, std::vector<int>* lignes = nullptr, PlanBase plan = PlanBase::XY) {
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  e.plan = plan;
  auto l = polygone(e.esquisse, pts);
  if (lignes) *lignes = l;
  return p.ajouter(e);
}

inline TopoDS_Edge cercleEn(const TopoDS_Shape& s, const gp_Pnt& c, double rr, double tol = 1e-4) {
  for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
    BRepAdaptor_Curve k(TopoDS::Edge(ex.Current()));
    if (k.GetType() == GeomAbs_Circle && k.Circle().Location().Distance(c) < tol && std::abs(k.Circle().Radius() - rr) < tol) return TopoDS::Edge(ex.Current());
  }
  return {};
}

inline double vol(const ResultatPiece& r) { return r.forme.IsNull() ? 0.0 : prom::volume(r.forme); }

inline std::string erreurs(const ResultatPiece& r) {
  std::string s;
  for (const auto& [id, m] : r.erreurs) s += "[" + std::to_string(id) + "] " + m + " ; ";
  return s;
}

// Assemblage
struct Arbre { Piece p; std::string flanc; int ext = 0; };
inline Arbre arbre(double r = 5, double l = 40) {
  Arbre a;
  FonctionPiece e;
  e.type = TypeFonction::Esquisse;
  const int c = e.esquisse.ajouterCercle(e.esquisse.ajouterPoint(0, 0), r);
  const int ie = a.p.ajouter(e);
  a.ext = extrusion(a.p, ie, l);
  a.flanc = std::to_string(a.ext) + ":cote:" + std::to_string(c);
  return a;
}

inline std::string percer(Piece& p, const std::string& face, double u, double v, double d = 10, bool inv = false) {
  FonctionPiece s;
  s.type = TypeFonction::Esquisse;
  s.face = face;
  s.esquisse.ajouterPoint(u, v);
  const int is = p.ajouter(s);
  FonctionPiece t;
  t.type = TypeFonction::Percage;
  t.esquisseId = is;
  t.typePercage = "simple";
  t.diametre = d;
  t.traversant = true;
  t.inverse = inv;
  const int it = p.ajouter(t);
  for (const auto& [f, nom] : construirePiece(p).faces)
    if (nom.rfind(std::to_string(it) + ":trou", 0) == 0 && BRepAdaptor_Surface(f).GetType() == GeomAbs_Cylinder) return nom;
  return {};
}

inline ComposantAssemblage cmp(int piece, double x, double y, double z, double rx = 0, double ry = 0, double rz = 0, bool fixe = false) {
  ComposantAssemblage c;
  c.piece = piece; c.x = x; c.y = y; c.z = z; c.rx = rx; c.ry = ry; c.rz = rz; c.fixe = fixe;
  return c;
}
inline ContrainteAssemblage ctr(const std::string& t, int a, const std::string& ra, int b, const std::string& rb, double v = 0, bool inv = false) {
  ContrainteAssemblage k;
  k.type = t; k.a = a; k.refA = ra; k.b = b; k.refB = rb; k.valeur = v; k.inverse = inv;
  return k;
}
inline TopoDS_Shape placee(const ResultatPiece& r, const ComposantAssemblage& c) {
  const gp_Trsf t = placementComposant(c);
  return c.miroir ? BRepBuilderAPI_Transform(r.forme, t, true).Shape() : r.forme.Moved(TopLoc_Location(t));
}
inline gp_Dir monde(const ComposantAssemblage& c, double x, double y, double z) { return gp_Dir(x, y, z).Transformed(placementComposant(c)); }

struct Chrono {
  std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
  double ms() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
};

// Dossier des fichiers exportés par les essais (relus ensuite par py/verif_fichiers.py).
// Par défaut « sorties » dans le dossier courant ; sinon la variable PROMETHEE_ESSAIS_SORTIES.
inline std::string dossierSorties() {
  const char* e = std::getenv("PROMETHEE_ESSAIS_SORTIES");
  const std::string d = (e && *e) ? e : "sorties";
  std::filesystem::create_directories(d);
  return d;
}
}  // namespace verif
