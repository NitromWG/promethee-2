// Prométhée : bande d'une fonction mince. Licence GPL-3.0-only.
#include "mince.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRep_Tool.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <ShapeFix_Face.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

namespace prom::mince {

namespace {

double aire(const TopoDS_Shape& s) {
  GProp_GProps g;
  BRepGProp::SurfaceProperties(s, g);
  return std::abs(g.Mass());
}

std::vector<TopoDS_Face> facesDe(const TopoDS_Shape& s) {
  std::vector<TopoDS_Face> r;
  if (s.IsNull()) return r;
  for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next())
    if (aire(ex.Current()) > 1e-9) r.push_back(TopoDS::Face(ex.Current()));
  return r;
}

// Région délimitée par des fils fermés d'un même plan, emboîtés ou non : un fil compris dans un nombre pair d'autres
// ajoute de la matière, dans un nombre impair il en retire.
TopoDS_Shape regionDepuisFils(const gp_Pln& plan, const std::vector<TopoDS_Wire>& fils) {
  struct Plein { TopoDS_Face face; double aire; gp_Pnt point; int profondeur = 0; };
  std::vector<Plein> pleins;
  for (const auto& w : fils) {
    BRepBuilderAPI_MakeFace mf(plan, w, true);
    if (!mf.IsDone()) continue;
    ShapeFix_Face fix(mf.Face());
    fix.Perform();
    const TopoDS_Face f = fix.Face();
    TopExp_Explorer ee(w, TopAbs_EDGE);
    if (!ee.More()) continue;
    BRepAdaptor_Curve c(TopoDS::Edge(ee.Current()));
    pleins.push_back({f, aire(f), c.Value((c.FirstParameter() + c.LastParameter()) / 2), 0});
  }
  for (std::size_t i = 0; i < pleins.size(); ++i)
    for (std::size_t j = 0; j < pleins.size(); ++j) {
      if (i == j || pleins[j].aire <= pleins[i].aire) continue;
      BRepClass_FaceClassifier cl(pleins[j].face, pleins[i].point, 1e-7);
      if (cl.State() == TopAbs_IN) ++pleins[i].profondeur;
    }
  std::stable_sort(pleins.begin(), pleins.end(), [](const Plein& a, const Plein& b) { return a.profondeur < b.profondeur; });
  TopoDS_Shape r;
  for (const auto& p : pleins) {
    if (p.profondeur % 2 == 0) r = r.IsNull() ? TopoDS_Shape(p.face) : BRepAlgoAPI_Fuse(r, p.face).Shape();
    else if (!r.IsNull()) r = BRepAlgoAPI_Cut(r, p.face).Shape();
  }
  return r;
}

// Région décalée de d (> 0 : élargie, < 0 : rétrécie) ; forme nulle si elle disparaît.
bool decalerRegion(const TopoDS_Face& region, const gp_Pln& plan, double d, GeomAbs_JoinType raccord, TopoDS_Shape& res) {
  if (std::abs(d) < 1e-12) { res = region; return true; }
  BRepOffsetAPI_MakeOffset mk(region, raccord);
  mk.Perform(d);
  if (!mk.IsDone()) return false;
  std::vector<TopoDS_Wire> fils;
  for (TopExp_Explorer ex(mk.Shape(), TopAbs_WIRE); ex.More(); ex.Next()) fils.push_back(TopoDS::Wire(ex.Current()));
  res = fils.empty() ? TopoDS_Shape() : regionDepuisFils(plan, fils);
  return true;
}

// ---------- Contour ouvert, en 2D ----------

struct Pt { double x = 0, y = 0; };
Pt operator+(Pt a, Pt b) { return {a.x + b.x, a.y + b.y}; }
Pt operator-(Pt a, Pt b) { return {a.x - b.x, a.y - b.y}; }
Pt operator*(Pt a, double k) { return {a.x * k, a.y * k}; }
double norme(Pt a) { return std::hypot(a.x, a.y); }
double dist(Pt a, Pt b) { return norme(a - b); }

// Élément parcouru de a vers b : segment, ou arc de centre c (sens trigonométrique si trigo)
struct Troncon {
  bool arc = false;
  Pt a, b, c;
  double r = 0;
  bool trigo = true;
};

// Décalage à gauche du sens de parcours, de d (signé)
std::optional<Troncon> decaler(const Troncon& t, double d) {
  Troncon o = t;
  if (!t.arc) {
    const Pt u = t.b - t.a;
    const double l = norme(u);
    if (l < 1e-12) return std::nullopt;
    const Pt n{-u.y / l, u.x / l};
    o.a = t.a + n * d;
    o.b = t.b + n * d;
    return o;
  }
  // À gauche d'un arc parcouru dans le sens trigonométrique : vers le centre
  o.r = t.trigo ? t.r - d : t.r + d;
  if (o.r <= 1e-9) return std::nullopt;
  o.a = t.c + (t.a - t.c) * (o.r / t.r);
  o.b = t.c + (t.b - t.c) * (o.r / t.r);
  return o;
}

// Intersections des supports (droite infinie ou cercle complet) de deux tronçons
std::vector<Pt> intersections(const Troncon& p, const Troncon& q) {
  std::vector<Pt> r;
  auto droiteDroite = [&](const Troncon& s, const Troncon& t) {
    const Pt u = s.b - s.a, v = t.b - t.a;
    const double den = u.x * v.y - u.y * v.x;
    if (std::abs(den) < 1e-12 * std::max(1.0, norme(u) * norme(v))) return;
    const Pt w = t.a - s.a;
    const double k = (w.x * v.y - w.y * v.x) / den;
    r.push_back(s.a + u * k);
  };
  auto droiteCercle = [&](const Troncon& s, const Troncon& c) {
    const Pt u = s.b - s.a;
    const double l = norme(u);
    if (l < 1e-12) return;
    const Pt e = u * (1 / l);
    const Pt w = s.a - c.c;
    const double bq = w.x * e.x + w.y * e.y, cq = w.x * w.x + w.y * w.y - c.r * c.r;
    const double disc = bq * bq - cq;
    if (disc < -1e-12) return;
    const double s0 = std::sqrt(std::max(0.0, disc));
    r.push_back(s.a + e * (-bq - s0));
    r.push_back(s.a + e * (-bq + s0));
  };
  if (!p.arc && !q.arc) droiteDroite(p, q);
  else if (!p.arc) droiteCercle(p, q);
  else if (!q.arc) droiteCercle(q, p);
  else {
    const Pt d = q.c - p.c;
    const double l = norme(d);
    if (l < 1e-12) return r;
    const double a = (p.r * p.r - q.r * q.r + l * l) / (2 * l), h2 = p.r * p.r - a * a;
    if (h2 < -1e-12) return r;
    const double h = std::sqrt(std::max(0.0, h2));
    const Pt m = p.c + d * (a / l), n{-d.y / l, d.x / l};
    r.push_back(m + n * h);
    r.push_back(m - n * h);
  }
  return r;
}

// Chaîne décalée de d : chaque raccord est l'intersection des supports la plus proche des extrémités décalées
bool chaineDecalee(const std::vector<Troncon>& ch, double d, std::vector<Troncon>& res, std::string& erreur) {
  res.clear();
  if (std::abs(d) < 1e-12) { res = ch; return true; }
  for (const auto& t : ch) {
    const auto o = decaler(t, d);
    if (!o) { erreur = "Épaisseur trop grande pour un arc du contour (rayon dépassé)."; return false; }
    res.push_back(*o);
  }
  for (std::size_t k = 0; k + 1 < res.size(); ++k) {
    Troncon& p = res[k];
    Troncon& q = res[k + 1];
    if (dist(p.b, q.a) < 1e-9) continue;
    const Pt attendu = (p.b + q.a) * 0.5;
    std::optional<Pt> meilleur;
    for (const Pt& x : intersections(p, q))
      if (!meilleur || dist(x, attendu) < dist(*meilleur, attendu)) meilleur = x;
    if (!meilleur || dist(*meilleur, attendu) > 50 * std::abs(d) + 1e-6) {
      // supports parallèles ou trop loin : raccord droit
      Troncon pont;
      pont.a = p.b;
      pont.b = q.a;
      res.insert(res.begin() + static_cast<std::ptrdiff_t>(k) + 1, pont);
      ++k;
      continue;
    }
    p.b = *meilleur;
    q.a = *meilleur;
  }
  return true;
}

}  // namespace

bool bandeRegion(const TopoDS_Face& region, const gp_Ax3& ax, double e, const std::string& sens, bool arrondi, std::vector<TopoDS_Face>& faces,
                 std::string& erreur) {
  faces.clear();
  if (!(e > 0)) { erreur = "L'épaisseur de la paroi doit être positive."; return false; }
  const gp_Pln plan(ax);
  const GeomAbs_JoinType raccord = arrondi ? GeomAbs_Arc : GeomAbs_Intersection;
  const double dPlus = sens == "exterieur" ? e : sens == "milieu" ? e / 2 : 0;
  const double dMoins = sens == "exterieur" ? 0 : sens == "milieu" ? -e / 2 : -e;
  try {
    TopoDS_Shape grande, petite;
    if (!decalerRegion(region, plan, dPlus, raccord, grande) || grande.IsNull()) {
      erreur = "Paroi impossible : le contour ne se décale pas vers l'extérieur.";
      return false;
    }
    if (!decalerRegion(region, plan, dMoins, GeomAbs_Intersection, petite)) {
      erreur = "Épaisseur impossible pour ce contour (trop grande par rapport à la forme).";
      return false;
    }
    const TopoDS_Shape bande = petite.IsNull() ? grande : BRepAlgoAPI_Cut(grande, petite).Shape();
    for (const auto& f : facesDe(bande)) {
      ShapeFix_Face fix(f);
      fix.Perform();
      faces.push_back(fix.Face());
    }
  } catch (const Standard_Failure&) {
    erreur = "Impossible de former la paroi mince.";
    return false;
  }
  if (faces.empty()) { erreur = "Impossible de former la paroi mince."; return false; }
  return true;
}

bool bandeOuverte(const Esquisse& e, const gp_Ax3& ax, double ep, const std::string& sens, TopoDS_Face& face, std::string& erreur) {
  if (!(ep > 0)) { erreur = "L'épaisseur de la paroi doit être positive."; return false; }
  // Chaîne des éléments hors construction (lignes et arcs), depuis l'extrémité libre de plus petit identifiant
  std::map<int, std::vector<int>> parPoint;
  std::vector<const ElementEsquisse*> els;
  for (const auto& el : e.elements)
    if (!el.construction && el.type != TypeElement::Cercle && e.elementValide(el)) {
      els.push_back(&el);
      parPoint[el.p1].push_back(el.id);
      parPoint[el.p2].push_back(el.id);
    }
  if (els.empty()) { erreur = "L'esquisse ne contient aucun élément."; return false; }
  int depart = -1;
  for (const auto& [pt, l] : parPoint) {
    if (l.size() > 2) { erreur = "Le contour ouvert se ramifie : il doit former une seule chaîne."; return false; }
    if (l.size() == 1 && depart < 0) depart = pt;
  }
  if (depart < 0) { erreur = "Le contour est fermé : il n'a pas d'extrémité."; return false; }
  auto P = [&](int id) { const PointEsquisse* p = e.point(id); return Pt{p->x, p->y}; };
  std::vector<Troncon> chaine;
  std::set<int> vus;
  int premierId = 1 << 30;
  bool premierDirect = true;
  int point = depart;
  for (std::size_t n = 0; n < els.size(); ++n) {
    const ElementEsquisse* s = nullptr;
    for (int id : parPoint[point])
      if (!vus.count(id)) { s = e.element(id); break; }
    if (!s) break;
    vus.insert(s->id);
    const bool direct = s->p1 == point;
    if (s->id < premierId) { premierId = s->id; premierDirect = direct; }
    Troncon t;
    t.a = P(direct ? s->p1 : s->p2);
    t.b = P(direct ? s->p2 : s->p1);
    if (s->type == TypeElement::Arc) {
      t.arc = true;
      t.c = P(s->centre);
      t.r = dist(t.a, t.c);
      t.trigo = direct;  // un arc d'esquisse va de p1 à p2 dans le sens trigonométrique
    }
    chaine.push_back(t);
    point = direct ? s->p2 : s->p1;
  }
  if (vus.size() != els.size()) { erreur = "Les éléments de l'esquisse ne forment pas une seule chaîne continue."; return false; }
  // Sens de parcours : celui du tracé du premier élément dessiné (de son p1 vers son p2)
  if (!premierDirect) {
    std::reverse(chaine.begin(), chaine.end());
    for (auto& t : chaine) { std::swap(t.a, t.b); t.trigo = !t.trigo; }
  }
  const double dGauche = sens == "exterieur" ? ep : sens == "milieu" ? ep / 2 : 0;
  const double dDroite = sens == "exterieur" ? 0 : sens == "milieu" ? -ep / 2 : -ep;
  std::vector<Troncon> g, d;
  if (!chaineDecalee(chaine, dGauche, g, erreur) || !chaineDecalee(chaine, dDroite, d, erreur)) return false;
  // Contour : bord gauche, bout d'arrivée, bord droit à rebours, bout de départ
  auto vers3 = [&](Pt q) { return ax.Location().Translated(gp_Vec(ax.XDirection()) * q.x + gp_Vec(ax.YDirection()) * q.y); };
  auto arete = [&](const Troncon& t) -> TopoDS_Edge {
    if (!t.arc) return BRepBuilderAPI_MakeEdge(vers3(t.a), vers3(t.b)).Edge();
    const gp_Circ cercle(gp_Ax2(vers3(t.c), ax.Direction(), ax.XDirection()), t.r);
    return t.trigo ? BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(cercle, vers3(t.a), vers3(t.b), true).Value()).Edge()
                   : BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(cercle, vers3(t.b), vers3(t.a), true).Value()).Edge();
  };
  try {
    BRepBuilderAPI_MakeWire mw;
    for (const auto& t : g) mw.Add(arete(t));
    if (dist(g.back().b, d.back().b) > 1e-9) mw.Add(BRepBuilderAPI_MakeEdge(vers3(g.back().b), vers3(d.back().b)).Edge());
    for (auto it = d.rbegin(); it != d.rend(); ++it) mw.Add(arete(*it));
    if (dist(d.front().a, g.front().a) > 1e-9) mw.Add(BRepBuilderAPI_MakeEdge(vers3(d.front().a), vers3(g.front().a)).Edge());
    if (!mw.IsDone() || !mw.Wire().Closed()) { erreur = "Impossible d'épaissir ce contour ouvert (raccords incohérents)."; return false; }
    BRepBuilderAPI_MakeFace mf(gp_Pln(ax), mw.Wire(), true);
    if (!mf.IsDone()) { erreur = "Impossible d'épaissir ce contour ouvert."; return false; }
    ShapeFix_Face fix(mf.Face());
    fix.Perform();
    face = fix.Face();
  } catch (const Standard_Failure&) {
    erreur = "Impossible d'épaissir ce contour ouvert.";
    return false;
  }
  return true;
}

}  // namespace prom::mince
