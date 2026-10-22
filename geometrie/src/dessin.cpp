// Prométhée : mise en plan. Licence GPL-3.0-only.
#include "promethee/dessin.hpp"
#include "promethee/fichiers.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <cmath>
#include <map>
#include <numeric>
#include <sstream>
#include <tuple>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepTools.hxx>
#include <Bnd_Box.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Cylinder.hxx>

namespace prom {

namespace {
void ajouterAretes(const TopoDS_Shape& s, TypeTrait type, std::vector<Trait2D>& r) {
  if (s.IsNull()) return;
  for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
    BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
    const double t0 = c.FirstParameter(), t1 = c.LastParameter();
    const int n = c.GetType() == GeomAbs_Line ? 1 : 48;
    Trait2D t;
    t.type = type;
    for (int i = 0; i <= n; ++i) {
      const gp_Pnt p = c.Value(t0 + (t1 - t0) * i / n);
      t.points.push_back({p.X(), p.Y()});
    }
    r.push_back(t);
  }
}

void boite(const std::vector<Trait2D>& traits, double& x0, double& y0, double& x1, double& y1) {
  x0 = y0 = 1e300;
  x1 = y1 = -1e300;
  for (const auto& t : traits)
    for (const auto& [x, y] : t.points) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
  if (x0 > x1) x0 = y0 = x1 = y1 = 0;
}

Trait2D segment(double x0, double y0, double x1, double y1, TypeTrait type = TypeTrait::Vu) {
  Trait2D t;
  t.points = {{x0, y0}, {x1, y1}};
  t.type = type;
  return t;
}

// Priorité des traits (ISO 128) : les portions d'arêtes cachées qui recouvrent une arête vue sont retirées (MP-03)
std::vector<Trait2D> sansCachesSousVus(const std::vector<Trait2D>& traits, double tol) {
  struct Seg { double x0, y0, x1, y1; };
  std::vector<Seg> vus;
  for (const auto& t : traits)
    if (t.type == TypeTrait::Vu || t.type == TypeTrait::Tangente)
      for (std::size_t i = 1; i < t.points.size(); ++i) vus.push_back({t.points[i - 1].first, t.points[i - 1].second, t.points[i].first, t.points[i].second});
  std::sort(vus.begin(), vus.end(), [](const Seg& a, const Seg& b) { return std::min(a.x0, a.x1) < std::min(b.x0, b.x1); });
  std::vector<Trait2D> r;
  std::vector<std::pair<double, double>> restes;
  for (const auto& t : traits) {
    if (t.type != TypeTrait::Cache) { r.push_back(t); continue; }
    Trait2D courant;
    courant.type = TypeTrait::Cache;
    auto clore = [&] { if (courant.points.size() >= 2) r.push_back(courant); courant.points.clear(); };
    for (std::size_t i = 1; i < t.points.size(); ++i) {
      const double ax = t.points[i - 1].first, ay = t.points[i - 1].second, bx = t.points[i].first, by = t.points[i].second;
      const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
      restes.assign(1, {0.0, 1.0});
      if (l2 > 1e-24) {
        const double l = std::sqrt(l2);
        const double xmin = std::min(ax, bx) - tol, xmax = std::max(ax, bx) + tol;
        const auto debut = std::lower_bound(vus.begin(), vus.end(), xmin - 1e9, [](const Seg& s, double x) { return std::min(s.x0, s.x1) < x; });
        for (auto it = debut; it != vus.end() && std::min(it->x0, it->x1) <= xmax && !restes.empty(); ++it) {
          const Seg& v = *it;
          if (std::max(v.x0, v.x1) < xmin || std::min(v.y0, v.y1) > std::max(ay, by) + tol || std::max(v.y0, v.y1) < std::min(ay, by) - tol) continue;
          const double vx = v.x1 - v.x0, vy = v.y1 - v.y0, lv = std::hypot(vx, vy);
          if (lv < 1e-12) continue;
          // Colinéaires et confondus : les deux extrémités du segment caché sont sur la droite du segment vu
          const double dA = std::abs((ax - v.x0) * vy - (ay - v.y0) * vx) / lv, dB = std::abs((bx - v.x0) * vy - (by - v.y0) * vx) / lv;
          if (dA > tol || dB > tol) continue;
          double s0 = ((v.x0 - ax) * dx + (v.y0 - ay) * dy) / l2, s1 = ((v.x1 - ax) * dx + (v.y1 - ay) * dy) / l2;
          if (s0 > s1) std::swap(s0, s1);
          s0 -= tol / l;
          s1 += tol / l;
          std::vector<std::pair<double, double>> n;
          for (const auto& [u0, u1] : restes) {
            if (s1 <= u0 || s0 >= u1) { n.push_back({u0, u1}); continue; }
            if (s0 > u0) n.push_back({u0, s0});
            if (s1 < u1) n.push_back({s1, u1});
          }
          restes = std::move(n);
        }
      }
      for (const auto& [u0, u1] : restes) {
        if ((u1 - u0) * std::sqrt(l2) < tol) continue;
        const std::pair<double, double> p0{ax + dx * u0, ay + dy * u0}, p1{ax + dx * u1, ay + dy * u1};
        if (!courant.points.empty() && std::hypot(courant.points.back().first - p0.first, courant.points.back().second - p0.second) < 1e-9) courant.points.push_back(p1);
        else {
          clore();
          courant.points = {p0, p1};
        }
      }
      if (restes.empty() || restes.back().second < 1 - 1e-12) clore();
    }
    clore();
  }
  return r;
}

// Axes des faces cylindriques d'au moins un demi-tour (trous, arbres, bossages ronds ; pas les congés) : ligne d'axe
// si l'axe est parallèle au plan de la vue, croix de centre s'il lui est perpendiculaire (MP-03)
void ajouterAxes(const TopoDS_Shape& forme, const gp_Ax2& vue, std::vector<Trait2D>& r) {
  const gp_Dir N = vue.Direction(), X = vue.XDirection(), Y = vue.YDirection();
  auto plan = [&](const gp_Pnt& p) { const gp_Vec w(vue.Location(), p); return std::pair{w.Dot(gp_Vec(X)), w.Dot(gp_Vec(Y))}; };
  std::map<std::tuple<long, long, long, long>, bool> vus;
  for (TopExp_Explorer ex(forme, TopAbs_FACE); ex.More(); ex.Next()) {
    const TopoDS_Face f = TopoDS::Face(ex.Current());
    BRepAdaptor_Surface s(f);
    if (s.GetType() != GeomAbs_Cylinder) continue;
    double u0, u1, v0, v1;
    BRepTools::UVBounds(f, u0, u1, v0, v1);
    if (u1 - u0 < M_PI - 1e-6) continue;
    const gp_Cylinder cy = s.Cylinder();
    const gp_Ax1 axe = cy.Axis();
    const double rayon = cy.Radius();
    // Étendue le long de l'axe : la boîte de la face projetée sur l'axe
    Bnd_Box b;
    BRepBndLib::Add(f, b);
    if (b.IsVoid()) continue;
    double bx0, by0, bz0, bx1, by1, bz1;
    b.Get(bx0, by0, bz0, bx1, by1, bz1);
    double tmin = 1e300, tmax = -1e300;
    for (double x : {bx0, bx1})
      for (double y : {by0, by1})
        for (double z : {bz0, bz1}) {
          const double t = gp_Vec(axe.Location(), gp_Pnt(x, y, z)).Dot(gp_Vec(axe.Direction()));
          tmin = std::min(tmin, t);
          tmax = std::max(tmax, t);
        }
    const double cosinus = std::abs(gp_Vec(axe.Direction()).Dot(gp_Vec(N)));
    const double depasse = std::max(1.5, 0.15 * rayon);
    if (cosinus < 1e-6) {
      const gp_Pnt a = axe.Location().Translated(gp_Vec(axe.Direction()) * (tmin - depasse)), c = axe.Location().Translated(gp_Vec(axe.Direction()) * (tmax + depasse));
      const auto [ua, va] = plan(a);
      const auto [uc, vc] = plan(c);
      const auto cle = std::tuple{std::lround(ua * 1e3), std::lround(va * 1e3), std::lround(uc * 1e3), std::lround(vc * 1e3)};
      if (vus.count(cle)) continue;
      vus[cle] = true;
      r.push_back(segment(ua, va, uc, vc, TypeTrait::Axe));
    } else if (cosinus > 1 - 1e-6) {
      const auto [uc, vc] = plan(axe.Location());
      const auto cle = std::tuple{std::lround(uc * 1e3), std::lround(vc * 1e3), 0L, std::lround(rayon * 1e3)};
      if (vus.count(cle)) continue;
      vus[cle] = true;
      const double l = rayon + depasse;
      r.push_back(segment(uc - l, vc, uc + l, vc, TypeTrait::Axe));
      r.push_back(segment(uc, vc - l, uc, vc + l, TypeTrait::Axe));
    }
  }
}

std::size_t longueurUtf8(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}
}  // namespace

MotifTrait motifTrait(TypeTrait t) {
  switch (t) {
    case TypeTrait::Vu: return {0.5, {}};
    case TypeTrait::Cache: return {0.25, {3.0, 0.75}};                 // 12d, 3d avec d = 0,25 mm
    case TypeTrait::Axe: return {0.25, {6.0, 0.75, 0.125, 0.75}};      // 24d, 3d, 0,5d, 3d
    case TypeTrait::Tangente: return {0.18, {}};
    case TypeTrait::Cote: return {0.25, {}};
    case TypeTrait::Hachure: return {0.18, {}};
    case TypeTrait::Fin: return {0.25, {}};
  }
  return {0.5, {}};
}

std::vector<Trait2D> projeter(const TopoDS_Shape& forme, const gp_Ax2& vue, bool cachees, bool tangentes, bool axes) {
  std::vector<Trait2D> r;
  if (forme.IsNull()) return r;
  Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
  algo->Add(forme);
  algo->Projector(HLRAlgo_Projector(vue));
  algo->Update();
  algo->Hide();
  HLRBRep_HLRToShape hlr(algo);
  // Cachées d'abord : à l'affichage, les traits vus passent par-dessus (MP-03)
  if (cachees) {
    ajouterAretes(hlr.HCompound(), TypeTrait::Cache, r);
    ajouterAretes(hlr.OutLineHCompound(), TypeTrait::Cache, r);
  }
  ajouterAretes(hlr.VCompound(), TypeTrait::Vu, r);
  ajouterAretes(hlr.OutLineVCompound(), TypeTrait::Vu, r);
  if (tangentes) ajouterAretes(hlr.Rg1LineVCompound(), TypeTrait::Tangente, r);
  if (axes) {
    try {
      ajouterAxes(forme, vue, r);
    } catch (const Standard_Failure&) {
      // un axe de moins, pas de mise en plan perdue
    }
  }
  if (cachees) {
    double x0, y0, x1, y1;
    boite(r, x0, y0, x1, y1);
    r = sansCachesSousVus(r, std::max(1e-4, 1e-6 * std::hypot(x1 - x0, y1 - y0)));
  }
  return r;
}

double echelleNormalisee(double e) {
  static const double serie[] = {50, 20, 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001};
  if (!(e > 0)) return 1;
  double meilleure = 1, ecart = 1e300;
  for (double s : serie)
    if (const double d = std::abs(std::log(s / e)); d < ecart) { ecart = d; meilleure = s; }
  return meilleure;
}

std::string texteEchelle(double e) {
  if (!(e > 0) || !std::isfinite(e)) return "1:1";
  // Fraction exacte p:q de petits entiers (0,4 → 2:5 ; 1,5 → 3:2), sinon arrondi
  for (long q = 1; q <= 1000; ++q) {
    const long p = std::lround(e * static_cast<double>(q));
    if (p > 0 && std::abs(static_cast<double>(p) / static_cast<double>(q) - e) <= 1e-9 * std::max(1.0, e)) {
      const long g = std::gcd(p, q);
      return std::to_string(p / g) + ":" + std::to_string(q / g);
    }
  }
  return e >= 1 ? std::to_string(std::lround(e)) + ":1" : "1:" + std::to_string(std::lround(1 / e));
}

namespace {
// ----- Outils de la mise en plan (M6) -----
std::pair<double, double> dansVue(const gp_Ax2& ax, const gp_Pnt& p) {
  const gp_Vec w(ax.Location(), p);
  return {w.Dot(gp_Vec(ax.XDirection())), w.Dot(gp_Vec(ax.YDirection()))};
}

// Garde d'une polyligne ce qui est hors de la bande a < x < b, et rapproche la partie droite de « decal »
std::vector<Trait2D> couperBande(const Trait2D& t, double a, double b, double decal) {
  std::vector<Trait2D> r;
  Trait2D courant;
  courant.type = t.type;
  courant.rempli = false;
  int coteCourant = 0;  // -1 gauche, 1 droite
  auto clore = [&] {
    if (courant.points.size() >= 2) r.push_back(courant);
    courant.points.clear();
  };
  auto ajouter = [&](double x, double y, int cote) {
    if (cote != coteCourant || courant.points.empty()) {
      clore();
      coteCourant = cote;
    }
    const double xx = cote > 0 ? x - decal : x;
    if (courant.points.empty() || std::hypot(courant.points.back().first - xx, courant.points.back().second - y) > 1e-12) courant.points.push_back({xx, y});
  };
  for (std::size_t i = 1; i < t.points.size(); ++i) {
    const auto [x0, y0] = t.points[i - 1];
    const auto [x1, y1] = t.points[i];
    // Morceaux du segment de chaque côté de la bande
    for (const int cote : {-1, 1}) {
      double t0 = 0, t1 = 1;
      const double dx = x1 - x0;
      if (std::abs(dx) < 1e-15) {
        const bool garde = cote < 0 ? x0 <= a : x0 >= b;
        if (!garde) continue;
      } else {
        const double ta = (a - x0) / dx, tb = (b - x0) / dx;
        if (cote < 0) {  // x ≤ a
          if (dx > 0) t1 = std::min(t1, ta); else t0 = std::max(t0, ta);
        } else {          // x ≥ b
          if (dx > 0) t0 = std::max(t0, tb); else t1 = std::min(t1, tb);
        }
        if (t1 - t0 <= 1e-12) continue;
      }
      // Le segment est parcouru dans l'ordre : on ne raccorde que des morceaux qui se suivent
      const double xa = x0 + (x1 - x0) * t0, ya = y0 + (y1 - y0) * t0, xb = x0 + (x1 - x0) * t1, yb = y0 + (y1 - y0) * t1;
      if (cote != coteCourant || courant.points.empty() || std::hypot(courant.points.back().first - (cote > 0 ? xa - decal : xa), courant.points.back().second - ya) > 1e-9) clore();
      ajouter(xa, ya, cote);
      ajouter(xb, yb, cote);
    }
  }
  clore();
  return r;
}

// Ce qui d'une polyligne est dans le cercle (cx, cy, R)
std::vector<Trait2D> couperCercle(const Trait2D& t, double cx, double cy, double R) {
  std::vector<Trait2D> r;
  Trait2D courant;
  courant.type = t.type;
  auto clore = [&] {
    if (courant.points.size() >= 2) r.push_back(courant);
    courant.points.clear();
  };
  for (std::size_t i = 1; i < t.points.size(); ++i) {
    const auto [x0, y0] = t.points[i - 1];
    const auto [x1, y1] = t.points[i];
    const double dx = x1 - x0, dy = y1 - y0, fx = x0 - cx, fy = y0 - cy;
    const double A = dx * dx + dy * dy, B = 2 * (fx * dx + fy * dy), C = fx * fx + fy * fy - R * R;
    double t0 = 0, t1 = 1;
    if (A < 1e-24) continue;
    const double disc = B * B - 4 * A * C;
    if (disc <= 0) { clore(); continue; }
    const double s = std::sqrt(disc);
    t0 = std::max(0.0, (-B - s) / (2 * A));
    t1 = std::min(1.0, (-B + s) / (2 * A));
    if (t1 - t0 <= 1e-12) { clore(); continue; }
    const double xa = x0 + dx * t0, ya = y0 + dy * t0, xb = x0 + dx * t1, yb = y0 + dy * t1;
    if (!courant.points.empty() && std::hypot(courant.points.back().first - xa, courant.points.back().second - ya) > 1e-9) clore();
    if (courant.points.empty()) courant.points.push_back({xa, ya});
    courant.points.push_back({xb, yb});
    if (t1 < 1 - 1e-12) clore();
  }
  clore();
  return r;
}

// Hachures à 45° (ISO 128-50) d'un domaine donné par ses bords (segments), règle pair-impair
std::vector<Trait2D> hachurer(const std::vector<std::array<double, 4>>& bords, double espacement) {
  std::vector<Trait2D> r;
  if (bords.empty() || !(espacement > 0)) return r;
  const double k = 1 / std::sqrt(2.0);
  // n : normale des hachures (lignes n·p = c), d : leur direction
  auto N = [&](double x, double y) { return (-x + y) * k; };
  auto D = [&](double x, double y) { return (x + y) * k; };
  double cmin = 1e300, cmax = -1e300;
  for (const auto& b : bords) {
    cmin = std::min({cmin, N(b[0], b[1]), N(b[2], b[3])});
    cmax = std::max({cmax, N(b[0], b[1]), N(b[2], b[3])});
  }
  if (cmax - cmin > 4000 * espacement) espacement = (cmax - cmin) / 4000;
  for (double c = std::ceil(cmin / espacement) * espacement; c <= cmax; c += espacement) {
    std::vector<double> ts;
    for (const auto& b : bords) {
      const double c1 = N(b[0], b[1]), c2 = N(b[2], b[3]);
      if ((c1 <= c && c < c2) || (c2 <= c && c < c1)) {
        const double u = (c - c1) / (c2 - c1);
        ts.push_back(D(b[0] + (b[2] - b[0]) * u, b[1] + (b[3] - b[1]) * u));
      }
    }
    std::sort(ts.begin(), ts.end());
    for (std::size_t i = 0; i + 1 < ts.size(); i += 2) {
      if (ts[i + 1] - ts[i] < 1e-9) continue;
      // Retour en (x, y) : p = d·D + n·N
      Trait2D t;
      t.type = TypeTrait::Hachure;
      for (const double dd : {ts[i], ts[i + 1]}) t.points.push_back({(dd - c) * k, (dd + c) * k});
      r.push_back(t);
    }
  }
  return r;
}

Trait2D fleche(double x, double y, double ux, double uy, double longueur = 2.5, double largeur = 0.9) {
  // Pointe en (x, y), dirigée selon (ux, uy)
  const double l = std::hypot(ux, uy);
  if (l < 1e-12) return {};
  ux /= l;
  uy /= l;
  Trait2D t;
  t.type = TypeTrait::Cote;
  t.rempli = true;
  t.points = {{x, y}, {x - ux * longueur - uy * largeur / 2, y - uy * longueur + ux * largeur / 2}, {x - ux * longueur + uy * largeur / 2, y - uy * longueur - ux * largeur / 2}, {x, y}};
  return t;
}

std::string nombreCote(double v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.2f", v);
  std::string s = t;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  for (char& c : s)
    if (c == '.') c = ',';
  return s;
}
}  // namespace

bool versFeuille(const VueDessin& v, const gp_Pnt& p, double& x, double& y) {
  auto [u, w] = dansVue(v.repere, p);
  if (v.interrompue()) {
    if (u > v.coupureDebut && u < v.coupureFin) return false;
    if (u >= v.coupureFin) u -= (v.coupureFin - v.coupureDebut) - v.ecart;
  }
  x = v.cx + (u - v.ox) * v.echelle;
  y = v.cy + (w - v.oy) * v.echelle;
  return true;
}

void dimensionsFeuille(const std::string& format, bool portrait, double& largeur, double& hauteur) {
  // ISO 216 : chaque format est la moitié du précédent ; à l'italienne, le grand côté est horizontal
  static const std::map<std::string, std::pair<double, double>> formats = {
      {"A0", {1189, 841}}, {"A1", {841, 594}}, {"A2", {594, 420}}, {"A3", {420, 297}}, {"A4", {297, 210}}};
  const auto it = formats.find(format);
  const auto [l, h] = it == formats.end() ? std::pair{297.0, 210.0} : it->second;
  largeur = portrait ? h : l;
  hauteur = portrait ? l : h;
}

Feuille miseEnPlan(const TopoDS_Shape& forme, const OptionsMiseEnPlan& o) {
  Feuille f;
  f.format = o.format;
  dimensionsFeuille(o.format, o.portrait, f.largeur, f.hauteur);
  f.troisiemeDiedre = o.troisiemeDiedre;
  f.revision = o.revision;
  f.designation = o.designation;
  f.projet = o.projet;
  f.titre = o.titre;
  f.materiau = o.materiau;
  f.auteur = o.auteur;
  f.date = o.date;
  f.reference = o.reference;
  f.masse = o.masse;
  // Vue de face (D1) : F direction vers l'observateur, U le haut de la vue, R sa droite (R = U ∧ F). Par défaut,
  // la pièce est vue de l'avant (observateur en −Y), Z en haut ; l'isométrique la montre de l'avant gauche, comme la
  // vue 3D de l'application (MP-10)
  gp_Dir F(0, -1, 0), U(0, 0, 1);
  if (o.vueFace == "arriere") { F = gp_Dir(0, 1, 0); }
  else if (o.vueFace == "dessus") { F = gp_Dir(0, 0, 1); U = gp_Dir(0, 1, 0); }
  else if (o.vueFace == "dessous") { F = gp_Dir(0, 0, -1); U = gp_Dir(0, -1, 0); }
  else if (o.vueFace == "gauche") { F = gp_Dir(-1, 0, 0); }
  else if (o.vueFace == "droite") { F = gp_Dir(1, 0, 0); }
  if (const int q = ((o.rotationFace % 360) + 360) % 360; q != 0) U.Rotate(gp_Ax1(gp::Origin(), F), q * M_PI / 180);
  const gp_Dir R = U.Crossed(F);
  const bool dessous = o.vueVerticale == "dessous", droite = o.vueLaterale == "droite";
  const double r3 = 1 / std::sqrt(3.0), r2 = 1 / std::sqrt(2.0);
  auto combine = [](const gp_Dir& a, double ka, const gp_Dir& b, double kb, const gp_Dir& c = gp_Dir(1, 0, 0), double kc = 0) {
    return gp_Dir(a.XYZ() * ka + b.XYZ() * kb + c.XYZ() * kc);
  };
  struct Def { std::string nom, cle; gp_Ax2 ax; bool voulue; };
  const Def defs[] = {
      {"Face", "face", gp_Ax2(gp::Origin(), F, R), o.face},
      // Vue verticale : observateur au-dessus (U) ou au-dessous (−U) de la vue de face, droite conservée
      {dessous ? "Dessous" : "Dessus", "dessus", gp_Ax2(gp::Origin(), dessous ? U.Reversed() : U, R), o.dessus},
      // Vue latérale : observateur à gauche (−R) ou à droite (R), haut conservé
      {droite ? "Droite" : "Gauche", "gauche", droite ? gp_Ax2(gp::Origin(), R, F.Reversed()) : gp_Ax2(gp::Origin(), R.Reversed(), F), o.gauche},
      {"Isométrique", "iso", gp_Ax2(gp::Origin(), combine(R, -r3, F, r3, U, r3), combine(R, r2, F, r2)), o.iso}};
  // Place des vues projetées autour de la face : premier dièdre, la vue de dessus sous la face et celle de gauche à
  // sa droite ; troisième dièdre, l'inverse
  const bool verticaleDessous = o.troisiemeDiedre ? dessous : !dessous;
  const bool lateraleADroite = o.troisiemeDiedre ? droite : !droite;
  Bnd_Box boiteModele;
  if (!forme.IsNull()) BRepBndLib::Add(forme, boiteModele);
  double bx0 = 0, by0 = 0, bz0 = 0, bx1 = 0, by1 = 0, bz1 = 0;
  if (!boiteModele.IsVoid()) boiteModele.Get(bx0, by0, bz0, bx1, by1, bz1);
  const gp_Pnt centreModele((bx0 + bx1) / 2, (by0 + by1) / 2, (bz0 + bz1) / 2);
  // ----- Coupe A-A -----
  int vueCoupe = -1;
  for (int i = 0; i < 3; ++i)
    if (o.coupe == defs[i].cle && defs[i].voulue) vueCoupe = i;
  gp_Pnt pointCoupe = centreModele;
  std::vector<std::array<double, 4>> bordsCoupe;  // bords de la matière coupée, dans la vue en coupe (modèle)
  TopoDS_Shape formeCoupe;
  if (vueCoupe >= 0 && !forme.IsNull()) {
    const gp_Dir n = defs[vueCoupe].ax.Direction();
    pointCoupe = centreModele.Translated(gp_Vec(n) * o.decalageCoupe);
    try {
      TopoDS_Shape retire;
      if (o.demiCoupe) {
        // Demi-coupe (D4) : seul le quart devant le plan et à droite de l'axe vertical de la vue est retiré
        const double grand = 4 * std::sqrt(boiteModele.SquareExtent()) + 10;
        const gp_Dir X = defs[vueCoupe].ax.XDirection(), Y = defs[vueCoupe].ax.YDirection();
        retire = BRepPrimAPI_MakeBox(gp_Ax2(pointCoupe.Translated(gp_Vec(Y) * -grand), n, X), grand, 2 * grand, grand).Shape();
      } else {
        const TopoDS_Face plan = BRepBuilderAPI_MakeFace(gp_Pln(pointCoupe, n)).Face();
        retire = BRepPrimAPI_MakeHalfSpace(plan, pointCoupe.Translated(gp_Vec(n))).Solid();
      }
      BRepAlgoAPI_Cut c(forme, retire);
      if (c.IsDone() && !c.HasErrors()) formeCoupe = c.Shape();
    } catch (const Standard_Failure&) {
    }
    if (formeCoupe.IsNull()) {
      f.alertes.push_back("La coupe n'a pas pu être calculée : vue ordinaire.");
      vueCoupe = -1;
    } else {
      // Faces de la coupe : planes, dans le plan de coupe
      for (TopExp_Explorer ex(formeCoupe, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face fc = TopoDS::Face(ex.Current());
        BRepAdaptor_Surface s(fc);
        if (s.GetType() != GeomAbs_Plane) continue;
        const gp_Pln pl = s.Plane();
        if (std::abs(gp_Vec(pl.Axis().Direction()).Dot(gp_Vec(n))) < 1 - 1e-9 || pl.Distance(pointCoupe) > 1e-6) continue;
        for (TopExp_Explorer ee(fc, TopAbs_EDGE); ee.More(); ee.Next()) {
          BRepAdaptor_Curve cv(TopoDS::Edge(ee.Current()));
          const double t0 = cv.FirstParameter(), t1 = cv.LastParameter();
          const int m = cv.GetType() == GeomAbs_Line ? 1 : 64;
          auto prec = dansVue(defs[vueCoupe].ax, cv.Value(t0));
          for (int k = 1; k <= m; ++k) {
            const auto q = dansVue(defs[vueCoupe].ax, cv.Value(t0 + (t1 - t0) * k / m));
            bordsCoupe.push_back({prec.first, prec.second, q.first, q.second});
            prec = q;
          }
        }
      }
    }
  }
  // ----- Vues brutes (coordonnées projetées du modèle) -----
  std::vector<std::vector<Trait2D>> brutes(4);
  bool presente[4] = {false, false, false, false};
  for (int i = 0; i < 4; ++i) {
    if (!defs[i].voulue) continue;
    if (i == vueCoupe) {
      brutes[i] = projeter(formeCoupe, defs[i].ax, false, o.tangentes, o.axes);
      if (o.demiCoupe) {
        // La limite des deux moitiés est un axe (trait mixte fin), pas une arête : les traits vus qui la suivent
        // sont retirés et l'axe est tracé sur toute la hauteur de la vue
        const double uc = dansVue(defs[i].ax, pointCoupe).first;
        std::vector<Trait2D> gardes;
        double w0 = 1e300, w1 = -1e300;
        for (const auto& t : brutes[i]) {
          bool surAxe = !t.points.empty() && t.type != TypeTrait::Axe;
          for (const auto& [u, w] : t.points) { surAxe = surAxe && std::abs(u - uc) < 1e-6; w0 = std::min(w0, w); w1 = std::max(w1, w); }
          if (!surAxe) gardes.push_back(t);
        }
        if (w1 > w0) gardes.push_back(segment(uc, w0 - 2, uc, w1 + 2, TypeTrait::Axe));
        brutes[i] = gardes;
      }
    }
    else brutes[i] = projeter(forme, defs[i].ax, o.cachees && i != 3, o.tangentes, o.axes && i != 3);
    presente[i] = !brutes[i].empty();
  }
  // ----- Interruption (face et dessus, le long de X) -----
  const double longueurX = bx1 - bx0;
  const bool interrompre = o.interrompue && longueurX > 1e-6 && o.interruptionFin > o.interruptionDebut + 1e-6;
  const double coupA = bx0 + longueurX * std::clamp(o.interruptionDebut, 0.0, 1.0), coupB = bx0 + longueurX * std::clamp(o.interruptionFin, 0.0, 1.0);
  const double ecartCoupure = 0.04 * longueurX;
  auto interrompre2D = [&](std::vector<Trait2D>& traits) {
    std::vector<Trait2D> r;
    for (const auto& t : traits)
      for (auto& x : couperBande(t, coupA, coupB, (coupB - coupA) - ecartCoupure)) {
        x.type = t.type;
        r.push_back(x);
      }
    traits = r;
  };
  std::vector<Trait2D> traitsAvantCoupure[4];
  for (int i = 0; i < 4; ++i) traitsAvantCoupure[i] = brutes[i];
  if (interrompre)
    for (int i : {0, 1})
      if (presente[i]) interrompre2D(brutes[i]);
  double L[4] = {0}, H[4] = {0}, cx0[4] = {0}, cy0[4] = {0};
  for (int i = 0; i < 4; ++i) {
    if (!presente[i]) continue;
    double x0, y0, x1, y1;
    boite(brutes[i], x0, y0, x1, y1);
    L[i] = x1 - x0;
    H[i] = y1 - y0;
    cx0[i] = (x0 + x1) / 2;
    cy0[i] = (y0 + y1) / 2;
  }
  // ----- Place réservée autour des vues (papier) : étiquettes, cotes, tracé de coupe, bulles -----
  const double marge = 10, cartoucheL = 180, cartoucheH = 36, etiquette = 6, placeCote = 12, placeTrace = 9, placeBulles = 16;
  struct Extra { double g = 0, b = 0, d = 0, h = 0; };
  Extra ext[4];
  for (int i = 1; i < 4; ++i) ext[i].b += etiquette;
  if (vueCoupe == 0) ext[0].b += etiquette;
  // Vue qui montre le tracé du plan de coupe, et son orientation (horizontal : le long de X de la vue)
  int vueTrace = -1;
  if (vueCoupe == 0) vueTrace = presente[1] ? 1 : presente[2] ? 2 : -1;
  if (vueCoupe == 1 || vueCoupe == 2) vueTrace = presente[0] ? 0 : presente[vueCoupe == 1 ? 2 : 1] ? (vueCoupe == 1 ? 2 : 1) : -1;
  if (vueTrace >= 0) {
    // Le tracé dépasse de la vue des deux côtés, dans la direction de la trace
    const gp_Dir n = defs[vueCoupe].ax.Direction();
    const double nx = gp_Vec(n).Dot(gp_Vec(defs[vueTrace].ax.XDirection())), ny = gp_Vec(n).Dot(gp_Vec(defs[vueTrace].ax.YDirection()));
    if (std::abs(ny) >= std::abs(nx)) { ext[vueTrace].g += placeTrace; ext[vueTrace].d += placeTrace; }  // trace horizontale
    else { ext[vueTrace].h += placeTrace; ext[vueTrace].b += placeTrace; }
  }
  if (o.cotes) {
    if (presente[0]) { ext[0].b += placeCote; ext[0].g += placeCote; }
    if (presente[2]) ext[2].h += placeCote;
    else if (presente[1]) ext[1].g += placeCote;
    // Diamètres : la ligne de repère sort de la vue en haut à droite
    for (int i = 0; i < 3; ++i)
      if (presente[i]) { ext[i].d += 6; ext[i].h += 4; }
  }
  // Cotes du modèle (D3) : une marge autour des vues où elles peuvent tomber
  if (!o.cotesModele.empty())
    for (int i = 0; i < 3; ++i)
      if (presente[i]) { ext[i].g += 9; ext[i].d += 9; ext[i].h += 9; ext[i].b += 9; }
  const int vueBulles = o.bulles.empty() ? -1 : presente[3] ? 3 : presente[0] ? 0 : -1;
  if (vueBulles >= 0) {
    ext[vueBulles].g += placeBulles;
    ext[vueBulles].d += placeBulles;
    ext[vueBulles].h += placeBulles;
    ext[vueBulles].b += placeBulles;
  }
  // Au-dessus du cartouche : nomenclature (lignes de 6 mm et en-tête), puis notes
  std::vector<std::string> notes = o.notes;
  const double hauteurTable = o.nomenclature.empty() ? 0 : 6.0 * static_cast<double>(o.nomenclature.size() + 1);
  const double hauteurNotes = notes.empty() ? 0 : 4.5 * static_cast<double>(notes.size()) + 6;
  const double hautCartouche = cartoucheH + hauteurTable + hauteurNotes;
  struct Boite { double x0, y0, x1, y1; };
  struct Placement { double cx[4], cy[4], ei; bool ok; };
  auto placer = [&](double e, double g, double reductionIso) {
    Placement p{};
    p.ei = e * reductionIso;
    const double gauche = marge + g, haut = f.hauteur - marge - g;
    auto demiL = [&](int i, double k) { return presente[i] ? L[i] * k / 2 : 0.0; };
    auto demiH = [&](int i, double k) { return presente[i] ? H[i] * k / 2 : 0.0; };
    // Colonne de la face : face et vue verticale centrées sur la même verticale (projection)
    const bool col1 = presente[0] || presente[1];
    double aGauche1 = 0, aDroite1 = 0;
    for (int i : {0, 1})
      if (presente[i]) { aGauche1 = std::max(aGauche1, demiL(i, e) + ext[i].g); aDroite1 = std::max(aDroite1, demiL(i, e) + ext[i].d); }
    // Rangée de la face : face et vue latérale sur la même horizontale
    double aHaut1 = 0, aBas1 = 0;
    for (int i : {0, 2})
      if (presente[i]) { aHaut1 = std::max(aHaut1, demiH(i, e) + ext[i].h); aBas1 = std::max(aBas1, demiH(i, e) + ext[i].b); }
    // Vue latérale à gauche de la face : elle occupe la première colonne
    double debutCol1 = gauche;
    if (!lateraleADroite && presente[2]) {
      p.cx[2] = gauche + demiL(2, e) + ext[2].g;
      debutCol1 = p.cx[2] + demiL(2, e) + ext[2].d + g;
    }
    // Vue verticale au-dessus de la face : elle occupe la première rangée
    double debutRang1 = haut;
    if (!verticaleDessous && presente[1]) {
      p.cy[1] = haut - demiH(1, e) - ext[1].h;
      debutRang1 = p.cy[1] - demiH(1, e) - ext[1].b - g;
    }
    p.cx[0] = p.cx[1] = debutCol1 + aGauche1;
    p.cy[0] = p.cy[2] = debutRang1 - aHaut1;
    const double finCol1 = col1 ? p.cx[0] + aDroite1 + g : debutCol1;
    if (lateraleADroite) p.cx[2] = finCol1 + (presente[2] ? demiL(2, e) + ext[2].g : 0);
    const double finRang1 = (presente[0] || presente[2]) ? p.cy[0] - aBas1 - g : debutRang1;
    if (verticaleDessous) p.cy[1] = finRang1 - (presente[1] ? demiH(1, e) + ext[1].h : 0);
    std::vector<Boite> occupees;
    auto boiteDe = [&](int i, double cx, double cy, double k) { return Boite{cx - L[i] * k / 2 - ext[i].g, cy - H[i] * k / 2 - ext[i].b, cx + L[i] * k / 2 + ext[i].d, cy + H[i] * k / 2 + ext[i].h}; };
    for (int i = 0; i < 3; ++i)
      if (presente[i]) occupees.push_back(boiteDe(i, p.cx[i], p.cy[i], e));
    const Boite cartouche{f.largeur - marge - cartoucheL - g / 2, marge, f.largeur - marge, marge + hautCartouche + g / 2};
    auto dedans = [&](const Boite& b) { return b.x0 >= marge + 1e-9 && b.y0 >= marge + 1e-9 && b.x1 <= f.largeur - marge - 1e-9 && b.y1 <= f.hauteur - marge - 1e-9; };
    auto touche = [&](const Boite& a, const Boite& b, double ecart) { return a.x0 < b.x1 + ecart && b.x0 < a.x1 + ecart && a.y0 < b.y1 + ecart && b.y0 < a.y1 + ecart; };
    auto libre = [&](const Boite& b) {
      if (!dedans(b) || touche(b, cartouche, 0)) return false;
      for (const auto& x : occupees)
        if (touche(b, x, g / 2)) return false;
      return true;
    };
    p.ok = true;
    for (std::size_t i = 0; i < occupees.size() && p.ok; ++i) {
      if (!dedans(occupees[i]) || touche(occupees[i], cartouche, 0)) p.ok = false;
      for (std::size_t j = i + 1; j < occupees.size(); ++j)
        if (touche(occupees[i], occupees[j], g / 2 - 1e-9)) p.ok = false;
    }
    if (!p.ok || !presente[3]) return p;
    // Isométrique : en haut à droite, dans le coin libre en face de la vue de face, au-dessus du cartouche ou en
    // bas à gauche, à la première place libre
    const double li = L[3] * p.ei, hi = H[3] * p.ei;
    const double xDroite = f.largeur - marge - g - li / 2 - ext[3].d, xGauche = gauche + li / 2 + ext[3].g;
    const double yHaut = haut - hi / 2 - ext[3].h, yBas = marge + g + hi / 2 + ext[3].b;
    const double xCoin = lateraleADroite ? finCol1 + li / 2 + ext[3].g : xGauche;
    const double yCoin = verticaleDessous ? finRang1 - hi / 2 - ext[3].h : yHaut;
    const std::pair<double, double> essais[] = {{xDroite, yHaut}, {xCoin, yCoin}, {xDroite, marge + hautCartouche + g + ext[3].b + hi / 2}, {xGauche, yBas}};
    for (const auto& [x, y] : essais) {
      const Boite b{x - li / 2 - ext[3].g, y - hi / 2 - ext[3].b, x + li / 2 + ext[3].d, y + hi / 2 + ext[3].h};
      if (libre(b)) { p.cx[3] = x; p.cy[3] = y; return p; }
    }
    p.ok = false;
    return p;
  };
  // Vue de détail : son cercle dans la vue source (modèle), connu avant l'échelle ; l'échelle retenue doit lui
  // laisser une place libre sur la feuille
  int sourceDetail = -1;
  double detailCx = 0, detailCy = 0, detailR = 0;
  if (o.detail) {
    for (int i = 0; i < 3; ++i)
      if (o.vueDetail == defs[i].cle && presente[i]) sourceDetail = i;
    if (sourceDetail < 0) f.alertes.push_back("Vue de détail : la vue choisie n'est pas sur la feuille.");
    else {
      double sx0, sy0, sx1, sy1;
      boite(traitsAvantCoupure[sourceDetail], sx0, sy0, sx1, sy1);
      detailCx = (sx0 + sx1) / 2 + std::clamp(o.detailX, -0.5, 0.5) * (sx1 - sx0);
      detailCy = (sy0 + sy1) / 2 + std::clamp(o.detailY, -0.5, 0.5) * (sy1 - sy0);
      detailR = std::max(1e-3, std::clamp(o.detailRayon, 0.01, 1.0) * std::max(sx1 - sx0, sy1 - sy0));
    }
  }
  const double agrandissement = std::max(1.0, o.detailEchelle);
  auto placeDetail = [&](const Placement& p, double ech, double& px, double& py) {
    const double rp = detailR * ech * agrandissement;
    std::vector<Boite> occupees;
    for (int i = 0; i < 4; ++i)
      if (presente[i]) {
        const double k = i == 3 ? p.ei : ech;
        occupees.push_back({p.cx[i] - L[i] * k / 2 - ext[i].g - 3, p.cy[i] - H[i] * k / 2 - ext[i].b - 3, p.cx[i] + L[i] * k / 2 + ext[i].d + 3, p.cy[i] + H[i] * k / 2 + ext[i].h + 3});
      }
    occupees.push_back({f.largeur - marge - cartoucheL - 4, marge, f.largeur - marge, marge + hautCartouche + 4});
    for (double y = f.hauteur - marge - 6 - rp; y >= marge + 6 + rp + etiquette; y -= 2)
      for (double x = f.largeur - marge - 6 - rp; x >= marge + 6 + rp; x -= 2) {
        const Boite b{x - rp - 2, y - rp - etiquette - 2, x + rp + 2, y + rp + 2};
        bool ok = true;
        for (const auto& oc : occupees) ok = ok && !(b.x0 < oc.x1 && oc.x0 < b.x1 && b.y0 < oc.y1 && oc.y0 < b.y1);
        if (ok) { px = x; py = y; return true; }
      }
    return false;
  };
  static const double serie[] = {50, 20, 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001};
  double e = o.echelle > 0 ? o.echelle : 0;
  Placement retenu{};
  bool trouve = false, detailPlace = false;
  double detailPx = 0, detailPy = 0;
  auto essayer = [&](double ech, bool avecDetail) {
    for (double g : {15.0, 10.0})
      for (double red : {1.0, 0.5}) {
        const Placement p = placer(ech, g, red);
        if (!p.ok) continue;
        if (avecDetail && !placeDetail(p, ech, detailPx, detailPy)) continue;
        retenu = p;
        detailPlace = avecDetail;
        return true;
      }
    return false;
  };
  const bool detailVoulu = sourceDetail >= 0;
  if (e > 0) {
    trouve = (detailVoulu && essayer(e, true)) || essayer(e, false);
    if (!trouve) retenu = placer(e, 10, 0.5);
  } else {
    // Échelle la plus grande où tout tient, le détail compris ; sinon sans lui
    for (const bool avecDetail : {true, false}) {
      if (avecDetail && !detailVoulu) continue;
      for (double s : serie)
        if (essayer(s, avecDetail)) { e = s; trouve = true; break; }
      if (trouve) break;
    }
    if (!trouve) { e = serie[std::size(serie) - 1]; retenu = placer(e, 10, 0.5); }
  }
  if (detailVoulu && !detailPlace) detailPlace = placeDetail(retenu, e, detailPx, detailPy);
  f.echelle = e;
  f.tientDansLeCadre = trouve;
  // ----- Hachures de la coupe (2,5 mm sur le papier), après l'échelle -----
  if (vueCoupe >= 0 && presente[vueCoupe]) {
    auto h = hachurer(bordsCoupe, 2.5 / e);
    for (auto& t : h) traitsAvantCoupure[vueCoupe].push_back(t);
    if (interrompre && vueCoupe <= 1) interrompre2D(h);
    for (auto& t : h) brutes[vueCoupe].push_back(t);
  }
  // ----- Vues placées -----
  int indiceVue[4] = {-1, -1, -1, -1};
  for (int i = 0; i < 4; ++i) {
    if (!presente[i]) continue;
    const double k = i == 3 ? retenu.ei : e;
    VueDessin v;
    v.nom = i == vueCoupe ? "Coupe A-A" : defs[i].nom;
    v.cx = retenu.cx[i];
    v.cy = retenu.cy[i];
    v.largeur = L[i] * k;
    v.hauteur = H[i] * k;
    v.echelle = k;
    v.repere = defs[i].ax;
    v.ox = cx0[i];
    v.oy = cy0[i];
    if (interrompre && i <= 1) {
      v.coupureDebut = coupA;
      v.coupureFin = coupB;
      v.ecart = ecartCoupure;
    }
    for (auto t : brutes[i]) {
      for (auto& [x, y] : t.points) { x = v.cx + (x - cx0[i]) * k; y = v.cy + (y - cy0[i]) * k; }
      v.traits.push_back(t);
    }
    indiceVue[i] = static_cast<int>(f.vues.size());
    f.vues.push_back(v);
  }
  auto vue = [&](int i) -> VueDessin* { return indiceVue[i] >= 0 ? &f.vues[static_cast<std::size_t>(indiceVue[i])] : nullptr; };
  auto mettre = [&](int i, Trait2D t) {
    if (VueDessin* v = vue(i)) v->traits.push_back(std::move(t));
  };
  // Traits d'interruption : deux zigzags fins, de part et d'autre de l'écart
  if (interrompre)
    for (int i : {0, 1})
      if (VueDessin* v = vue(i)) {
        const double y0 = v->cy - v->hauteur / 2 - 1, y1 = v->cy + v->hauteur / 2 + 1;
        for (const double u : {coupA, coupA + ecartCoupure}) {
          const double x = v->cx + (u - v->ox) * v->echelle;
          Trait2D z;
          z.type = TypeTrait::Fin;
          const double ym = (y0 + y1) / 2;
          z.points = {{x, y0}, {x, ym - 2}, {x - 1.2, ym - 0.8}, {x + 1.2, ym + 0.8}, {x, ym + 2}, {x, y1}};
          v->traits.push_back(z);
        }
      }
  // ----- Tracé du plan de coupe dans la vue voisine -----
  std::vector<Feuille::Texte> textesVues;
  if (vueCoupe >= 0 && vueTrace >= 0)
    if (VueDessin* v = vue(vueTrace)) {
      const gp_Dir n = defs[vueCoupe].ax.Direction();
      const double nx = gp_Vec(n).Dot(gp_Vec(v->repere.XDirection())), ny = gp_Vec(n).Dot(gp_Vec(v->repere.YDirection()));
      double px = 0, py = 0;
      versFeuille(*v, pointCoupe, px, py);
      const bool horizontale = std::abs(ny) >= std::abs(nx);
      // Sens d'observation : de l'observateur vers la pièce (−n), projeté dans la vue
      const double sx = -nx, sy = -ny;
      double ax, ay, bx, by;
      if (horizontale) { ax = v->cx - v->largeur / 2 - 6; bx = v->cx + v->largeur / 2 + 6; ay = by = py; }
      else { ay = v->cy - v->hauteur / 2 - 6; by = v->cy + v->hauteur / 2 + 6; ax = bx = px; }
      v->traits.push_back(segment(ax, ay, bx, by, TypeTrait::Axe));
      const double ux = (bx - ax) / std::hypot(bx - ax, by - ay), uy = (by - ay) / std::hypot(bx - ax, by - ay);
      for (const auto& [ex, ey, sg] : {std::tuple{ax, ay, 1.0}, std::tuple{bx, by, -1.0}}) {
        v->traits.push_back(segment(ex, ey, ex + ux * 5 * sg, ey + uy * 5 * sg, TypeTrait::Vu));
        const double l = std::hypot(sx, sy);
        const double dx = sx / l, dy = sy / l;
        v->traits.push_back(segment(ex + ux * 2 * sg, ey + uy * 2 * sg, ex + ux * 2 * sg + dx * 7, ey + uy * 2 * sg + dy * 7, TypeTrait::Cote));
        v->traits.push_back(fleche(ex + ux * 2 * sg + dx * 7, ey + uy * 2 * sg + dy * 7, dx, dy, 3, 1.2));
        textesVues.push_back({ex + ux * 2 * sg + dx * 9.5 - 1.6, ey + uy * 2 * sg + dy * 9.5 - 1.6, 4.5, "A", true, false});
      }
    }
  // ----- Vue de détail -----
  if (sourceDetail >= 0) {
    const int source = sourceDetail;
    {
      const double cxm = detailCx, cym = detailCy, R = detailR;
      const double kd = e * agrandissement;
      const double rayonPapier = R * kd;
      const double px = detailPx, py = detailPy;
      if (!detailPlace) f.alertes.push_back("Vue de détail : pas de place libre sur la feuille (réduis son rayon ou son agrandissement, ou passe en A3).");
      else {
        VueDessin d;
        d.nom = "Détail B (" + texteEchelle(kd) + ")";
        d.cx = px;
        d.cy = py;
        d.largeur = d.hauteur = 2 * rayonPapier;
        d.echelle = kd;
        d.repere = defs[source].ax;
        d.ox = cxm;
        d.oy = cym;
        for (const auto& t : traitsAvantCoupure[source])
          for (auto x : couperCercle(t, cxm, cym, R)) {
            for (auto& [a, b] : x.points) { a = px + (a - cxm) * kd; b = py + (b - cym) * kd; }
            d.traits.push_back(x);
          }
        Trait2D bord, appel;
        bord.type = appel.type = TypeTrait::Fin;
        // Cercle d'appel dans la vue source (sa position tient compte d'une interruption)
        double ax = 0, ay = 0;
        const VueDessin* vs = vue(source);
        double u = cxm;
        if (vs && vs->interrompue() && u >= vs->coupureFin) u -= (vs->coupureFin - vs->coupureDebut) - vs->ecart;
        if (vs) { ax = vs->cx + (u - vs->ox) * vs->echelle; ay = vs->cy + (cym - vs->oy) * vs->echelle; }
        for (int k = 0; k <= 72; ++k) {
          const double a = 2 * M_PI * k / 72;
          bord.points.push_back({px + rayonPapier * std::cos(a), py + rayonPapier * std::sin(a)});
          appel.points.push_back({ax + R * e * std::cos(a), ay + R * e * std::sin(a)});
        }
        d.traits.push_back(bord);
        if (vs) {
          mettre(source, appel);
          textesVues.push_back({ax + R * e * 0.75 + 1, ay + R * e * 0.75 + 1, 4.5, "B", true, false});
        }
        f.vues.push_back(d);
      }
    }
  }
  // ----- Filetages (ISO 6410) -----
  std::set<std::tuple<int, long, long, long>> cerclesFiletes;  // vue, centre, rayon : pas d'indication Ø en plus
  if (!o.filetages.empty()) {
    std::set<std::string> indiques;
    for (int i : {1, 0, 2}) {
      VueDessin* v = vue(i);
      if (!v) continue;
      const gp_Dir dv = defs[i].ax.Direction();
      std::map<std::string, std::vector<std::pair<double, double>>> enBout;  // indication → centres (feuille)
      std::map<std::string, double> rayonIndication;
      for (const auto& ft : o.filetages) {
        const double cosinus = gp_Vec(ft.axe.Direction()).Dot(gp_Vec(dv));
        const gp_Pnt debut = ft.axe.Location(), fin = debut.Translated(gp_Vec(ft.axe.Direction()) * ft.longueur);
        if (std::abs(cosinus) > 1 - 1e-6) {
          // En bout : trois quarts de cercle fin, ouverts en haut à droite
          double x = 0, y = 0;
          if (!versFeuille(*v, debut, x, y)) continue;
          const double r = (ft.exterieur ? ft.mineur : ft.nominal) / 2 * v->echelle;
          Trait2D arc;
          arc.type = TypeTrait::Fin;
          for (int k = 0; k <= 54; ++k) {
            const double a = M_PI / 2 + 1.5 * M_PI * k / 54;  // de 90° à 360°
            arc.points.push_back({x + r * std::cos(a), y + r * std::sin(a)});
          }
          v->traits.push_back(arc);
          const auto [u, w] = dansVue(defs[i].ax, debut);
          cerclesFiletes.insert({i, std::lround(u * 1e3), std::lround(w * 1e3), std::lround(ft.mineur / 2 * 1e3)});
          cerclesFiletes.insert({i, std::lround(u * 1e3), std::lround(w * 1e3), std::lround(ft.nominal / 2 * 1e3)});
          const std::string indication = ft.designation + " - " + nombreCote(ft.longueur);
          enBout[indication].push_back({x, y});
          rayonIndication[indication] = ft.nominal / 2 * v->echelle;
        } else if (std::abs(cosinus) < 1e-6 && i != vueCoupe) {
          // De côté : deux traits fins le long du filet, limite du filet en trait fort (tirets si caché)
          const gp_Vec radial = gp_Vec(dv) ^ gp_Vec(ft.axe.Direction());
          const double rf = (ft.exterieur ? ft.mineur : ft.nominal) / 2, rn = (ft.exterieur ? ft.nominal : ft.mineur) / 2;
          const TypeTrait type = ft.exterieur ? TypeTrait::Fin : TypeTrait::Cache;
          for (const double s : {-1.0, 1.0}) {
            double x0, y0, x1, y1;
            if (versFeuille(*v, debut.Translated(radial.Normalized() * (s * rf)), x0, y0) && versFeuille(*v, fin.Translated(radial.Normalized() * (s * rf)), x1, y1))
              v->traits.push_back(segment(x0, y0, x1, y1, type));
          }
          double xa, ya, xb, yb;
          if (versFeuille(*v, fin.Translated(radial.Normalized() * -std::max(rf, rn)), xa, ya) && versFeuille(*v, fin.Translated(radial.Normalized() * std::max(rf, rn)), xb, yb))
            v->traits.push_back(segment(xa, ya, xb, yb, ft.exterieur ? TypeTrait::Vu : TypeTrait::Cache));
        }
      }
      // Indications, une fois chacune, sur le cercle le plus en haut à droite
      for (const auto& [indication, centres] : enBout) {
        if (indiques.count(indication)) continue;
        indiques.insert(indication);
        const auto it = std::max_element(centres.begin(), centres.end(), [](const auto& a, const auto& b) { return a.first + a.second < b.first + b.second; });
        const double rp = rayonIndication[indication], c45 = std::cos(M_PI / 4);
        const double ax = it->first + rp * c45, ay = it->second + rp * c45, bx = ax + 5, by = ay + 5;
        const std::string texte = (centres.size() > 1 ? std::to_string(centres.size()) + "× " : std::string()) + indication;
        v->traits.push_back(segment(bx, by, ax, ay, TypeTrait::Cote));
        v->traits.push_back(fleche(ax, ay, -c45, -c45, 2.5, 0.9));
        v->traits.push_back(segment(bx, by, bx + 2.0 * static_cast<double>(longueurUtf8(texte)) + 1, by, TypeTrait::Cote));
        textesVues.push_back({bx + 0.5, by + 0.8, 3.5, texte, false, false});
      }
    }
  }
  // ----- Cotes d'encombrement et diamètres -----
  auto coteLineaire = [&](int iv, double xa, double ya, double xb, double yb, bool horizontale, double position, const std::string& texte) {
    VueDessin* v = vue(iv);
    if (!v) return;
    // Lignes de rappel depuis la vue jusqu'un peu au-delà de la ligne de cote, ligne de cote et deux flèches
    if (horizontale) {
      const double s = position < ya ? -1 : 1;
      v->traits.push_back(segment(xa, ya + s, xa, position + 2 * s, TypeTrait::Cote));
      v->traits.push_back(segment(xb, yb + s, xb, position + 2 * s, TypeTrait::Cote));
      v->traits.push_back(segment(xa, position, xb, position, TypeTrait::Cote));
      v->traits.push_back(fleche(xa, position, -1, 0));
      v->traits.push_back(fleche(xb, position, 1, 0));
      textesVues.push_back({(xa + xb) / 2, position + 1, 3.5, texte, false, true});
    } else {
      const double s = position < xa ? -1 : 1;
      v->traits.push_back(segment(xa + s, ya, position + 2 * s, ya, TypeTrait::Cote));
      v->traits.push_back(segment(xb + s, yb, position + 2 * s, yb, TypeTrait::Cote));
      v->traits.push_back(segment(position, ya, position, yb, TypeTrait::Cote));
      v->traits.push_back(fleche(position, ya, 0, -1));
      v->traits.push_back(fleche(position, yb, 0, 1));
      Feuille::Texte t{position - 1, (ya + yb) / 2, 3.5, texte, false, true};
      t.angle = 90;
      textesVues.push_back(t);
    }
  };
  if (o.cotes && !forme.IsNull()) {
    if (VueDessin* v = vue(0)) {
      const double x0 = v->cx - v->largeur / 2, x1 = v->cx + v->largeur / 2, y0 = v->cy - v->hauteur / 2, y1 = v->cy + v->hauteur / 2;
      coteLineaire(0, x0, y0, x1, y0, true, y0 - 8, nombreCote(bx1 - bx0));
      coteLineaire(0, x0, y0, x0, y1, false, x0 - 8, nombreCote(bz1 - bz0));
    }
    if (VueDessin* v = vue(2)) {
      const double x0 = v->cx - v->largeur / 2, x1 = v->cx + v->largeur / 2, y1 = v->cy + v->hauteur / 2;
      coteLineaire(2, x0, y1, x1, y1, true, y1 + 8, nombreCote(by1 - by0));
    } else if (VueDessin* v = vue(1)) {
      const double x0 = v->cx - v->largeur / 2, y0 = v->cy - v->hauteur / 2, y1 = v->cy + v->hauteur / 2;
      coteLineaire(1, x0, y0, x0, y1, false, x0 - 8, nombreCote(by1 - by0));
    }
    // Diamètres : cercles de la pièce vus en vrai grandeur, regroupés par diamètre, chacun écrit une fois
    std::set<long> diametresEcrits;
    for (int i : {1, 0, 2}) {
      VueDessin* v = vue(i);
      if (!v || i == vueCoupe) continue;
      struct Cercle { double u, w, r; };
      std::map<long, std::vector<Cercle>> parDiametre;
      std::set<std::tuple<long, long, long>> vus;
      for (TopExp_Explorer ex(forme, TopAbs_EDGE); ex.More(); ex.Next()) {
        BRepAdaptor_Curve c(TopoDS::Edge(ex.Current()));
        if (c.GetType() != GeomAbs_Circle) continue;
        const gp_Circ ci = c.Circle();
        if (std::abs(gp_Vec(ci.Axis().Direction()).Dot(gp_Vec(defs[i].ax.Direction()))) < 1 - 1e-9) continue;
        if (std::abs(c.LastParameter() - c.FirstParameter() - 2 * M_PI) > 1e-6) continue;  // cercles entiers seulement
        const auto [u, w] = dansVue(defs[i].ax, ci.Location());
        if (v->interrompue() && u > v->coupureDebut - ci.Radius() && u < v->coupureFin + ci.Radius()) continue;
        const auto cle = std::tuple{std::lround(u * 1e3), std::lround(w * 1e3), std::lround(ci.Radius() * 1e3)};
        if (!vus.insert(cle).second) continue;
        if (cerclesFiletes.count({i, std::get<0>(cle), std::get<1>(cle), std::get<2>(cle)})) continue;  // le filetage a son indication
        parDiametre[std::lround(2 * ci.Radius() * 100)].push_back({u, w, ci.Radius()});
      }
      for (auto& [dk, liste] : parDiametre) {
        if (diametresEcrits.count(dk)) continue;
        diametresEcrits.insert(dk);
        // Le cercle le plus en haut à droite porte l'indication
        const auto it = std::max_element(liste.begin(), liste.end(), [](const Cercle& a, const Cercle& b) { return a.u + a.w < b.u + b.w; });
        double x = 0, y = 0;
        if (!versFeuille(*v, gp_Pnt(0, 0, 0).Translated(gp_Vec(defs[i].ax.XDirection()) * it->u + gp_Vec(defs[i].ax.YDirection()) * it->w), x, y)) continue;
        const double rp = it->r * v->echelle, c45 = std::cos(M_PI / 4);
        const double ax = x + rp * c45, ay = y + rp * c45, bx = ax + 5, by = ay + 5;
        const std::string texte = (liste.size() > 1 ? std::to_string(liste.size()) + "× " : std::string()) + "Ø" + nombreCote(2 * it->r);
        v->traits.push_back(segment(bx, by, ax, ay, TypeTrait::Cote));
        v->traits.push_back(fleche(ax, ay, -c45, -c45, 2.5, 0.9));
        const double lt = 2.0 * static_cast<double>(longueurUtf8(texte)) + 1;
        v->traits.push_back(segment(bx, by, bx + lt, by, TypeTrait::Cote));
        textesVues.push_back({bx + 0.5, by + 0.8, 3.5, texte, false, false});
      }
    }
  }
  // ----- Cotes du modèle (D3) -----
  for (const auto& nom : o.cotesOrphelines) f.alertes.push_back("Cote orpheline : " + nom);
  if (!o.cotesModele.empty()) {
    using CM = OptionsMiseEnPlan::CoteModele;
    std::set<std::tuple<int, long, long, long, long, long>> posees;
    int nonPlacees = 0;
    for (const auto& k : o.cotesModele) {
      // Vue qui voit le plan de l'esquisse de face ; à défaut, pour une cote linéaire, une vue où le segment mesuré
      // est en vraie grandeur
      int iv = -1;
      for (int i : {0, 1, 2})
        if (VueDessin* v = vue(i); v && std::abs(gp_Vec(k.normale).Dot(gp_Vec(defs[i].ax.Direction()))) > 1 - 1e-6) { iv = i; break; }
      if (iv < 0 && k.genre == CM::Genre::Lineaire) {
        const gp_Vec d = k.direction ? gp_Vec(k.sens) : gp_Vec(k.a, k.b);
        for (int i : {0, 1, 2})
          if (VueDessin* v = vue(i); v && d.Magnitude() > 1e-12 && std::abs(d.Normalized().Dot(gp_Vec(defs[i].ax.Direction()))) < 1e-6) { iv = i; break; }
      }
      VueDessin* v = iv >= 0 ? vue(iv) : nullptr;
      double ax = 0, ay = 0, bx = 0, by = 0;
      if (!v || !versFeuille(*v, k.a, ax, ay) || !versFeuille(*v, k.b, bx, by)) { ++nonPlacees; continue; }
      std::string texte = k.genre == CM::Genre::Diametre ? "Ø" + nombreCote(k.valeur)
                        : k.genre == CM::Genre::Rayon    ? "R" + nombreCote(k.valeur)
                        : k.genre == CM::Genre::Angle    ? nombreCote(k.valeur) + "°"
                                                         : nombreCote(k.valeur);
      if (k.reference) texte = "(" + texte + ")";
      const auto cle = std::tuple{iv, std::lround(ax * 10), std::lround(ay * 10), std::lround(bx * 10), std::lround(by * 10), std::lround(k.valeur * 1000)};
      if (!posees.insert(cle).second) continue;
      double lx = 0, ly = 0;
      const bool etiquette = k.place && versFeuille(*v, k.etiquette, lx, ly);
      if (k.genre == CM::Genre::Diametre || k.genre == CM::Genre::Rayon) {
        // Ligne de repère depuis le cercle, vers l'extérieur, puis palier horizontal qui porte la valeur
        double ux = bx - ax, uy = by - ay;
        const double l = std::hypot(ux, uy);
        if (l < 1e-9) { ++nonPlacees; continue; }
        ux /= l;
        uy /= l;
        const double ex = bx + ux * 6, ey = by + uy * 6;
        if (k.genre == CM::Genre::Rayon) v->traits.push_back(segment(ax, ay, ex, ey, TypeTrait::Cote));
        else v->traits.push_back(segment(bx, by, ex, ey, TypeTrait::Cote));
        v->traits.push_back(fleche(bx, by, k.genre == CM::Genre::Rayon ? ux : -ux, k.genre == CM::Genre::Rayon ? uy : -uy, 2.5, 0.9));
        const double lt = 2.0 * static_cast<double>(longueurUtf8(texte)) + 1, sens = ux >= 0 ? 1.0 : -1.0;
        v->traits.push_back(segment(ex, ey, ex + sens * lt, ey, TypeTrait::Cote));
        textesVues.push_back({sens > 0 ? ex + 0.5 : ex - lt + 0.5, ey + 0.8, 3.5, texte, false, false});
      } else if (k.genre == CM::Genre::Angle) {
        double cx = 0, cy = 0;
        if (!versFeuille(*v, k.c, cx, cy)) { ++nonPlacees; continue; }
        const double tb = std::atan2(by - ay, bx - ax);
        double balayage = std::atan2(cy - ay, cx - ax) - tb;
        while (balayage <= -M_PI) balayage += 2 * M_PI;
        while (balayage > M_PI) balayage -= 2 * M_PI;
        const double R = etiquette ? std::max(6.0, std::hypot(lx - ax, ly - ay)) : 12.0;
        Trait2D arc;
        arc.type = TypeTrait::Cote;
        for (int n = 0; n <= 32; ++n) arc.points.push_back({ax + R * std::cos(tb + balayage * n / 32), ay + R * std::sin(tb + balayage * n / 32)});
        v->traits.push_back(arc);
        const double s0 = balayage > 0 ? 1 : -1;
        v->traits.push_back(fleche(ax + R * std::cos(tb), ay + R * std::sin(tb), s0 * std::sin(tb), -s0 * std::cos(tb), 2.5, 0.9));
        const double t1 = tb + balayage;
        v->traits.push_back(fleche(ax + R * std::cos(t1), ay + R * std::sin(t1), -s0 * std::sin(t1), s0 * std::cos(t1), 2.5, 0.9));
        const double tm = tb + balayage / 2;
        textesVues.push_back({ax + (R + 3) * std::cos(tm), ay + (R + 3) * std::sin(tm) - 1.2, 3.5, texte, false, true});
      } else {
        // Linéaire : alignée sur les points, ou le long d'une direction de l'esquisse ; ligne de cote du côté de
        // l'étiquette, sinon à l'extérieur de la vue
        double ux = bx - ax, uy = by - ay;
        if (k.direction) {
          double sx = 0, sy = 0;
          if (!versFeuille(*v, k.a.Translated(gp_Vec(k.sens) * 10), sx, sy)) { ++nonPlacees; continue; }
          ux = sx - ax;
          uy = sy - ay;
        }
        const double l = std::hypot(ux, uy);
        if (l < 1e-9) { ++nonPlacees; continue; }
        ux /= l;
        uy /= l;
        const double nx = -uy, ny = ux;
        const double sa = ax * nx + ay * ny, sb = bx * nx + by * ny;
        double sl;
        if (etiquette) sl = lx * nx + ly * ny;
        else {
          const double centre = v->cx * nx + v->cy * ny, milieu = (sa + sb) / 2;
          sl = milieu >= centre ? std::max(sa, sb) + 7 : std::min(sa, sb) - 7;
        }
        const double pax = ax + (sl - sa) * nx, pay = ay + (sl - sa) * ny, pbx = bx + (sl - sb) * nx, pby = by + (sl - sb) * ny;
        const double sg = sl >= (sa + sb) / 2 ? 1 : -1;
        for (const auto& [x0, y0, x1, y1] : {std::tuple{ax, ay, pax, pay}, std::tuple{bx, by, pbx, pby}})
          if (std::hypot(x1 - x0, y1 - y0) > 1.5) v->traits.push_back(segment(x0 + nx * sg, y0 + ny * sg, x1 + nx * 2 * sg, y1 + ny * 2 * sg, TypeTrait::Cote));
        v->traits.push_back(segment(pax, pay, pbx, pby, TypeTrait::Cote));
        const double dx = pbx - pax, dy = pby - pay, dl = std::max(1e-9, std::hypot(dx, dy));
        v->traits.push_back(fleche(pax, pay, -dx / dl, -dy / dl));
        v->traits.push_back(fleche(pbx, pby, dx / dl, dy / dl));
        // Texte lisible (jamais à l'envers), au-dessus de la ligne
        double angle = std::atan2(dy, dx) * 180 / M_PI;
        if (angle > 90.5) angle -= 180;
        if (angle <= -89.5) angle += 180;
        const double ar = angle * M_PI / 180;
        Feuille::Texte t{(pax + pbx) / 2 - std::sin(ar) * 1, (pay + pby) / 2 + std::cos(ar) * 1, 3.5, texte, false, true};
        t.angle = angle;
        textesVues.push_back(t);
      }
    }
    if (nonPlacees) f.alertes.push_back(std::to_string(nonPlacees) + " cote(s) du modèle sans vue qui les montre en vraie grandeur (choisis une autre vue de face).");
  }
  // ----- Bulles de repère (plan d'assemblage) -----
  if (vueBulles >= 0)
    if (VueDessin* v = vue(vueBulles)) {
      struct B { int rep; double ax, ay, angle; };
      std::vector<B> bs;
      std::set<int> deja;
      for (const auto& b : o.bulles) {
        if (!deja.insert(b.repere).second) continue;
        double x = 0, y = 0;
        if (!versFeuille(*v, b.ancre, x, y)) continue;
        bs.push_back({b.repere, x, y, std::atan2(y - v->cy, x - v->cx)});
      }
      std::sort(bs.begin(), bs.end(), [](const B& a, const B& b) { return a.angle < b.angle; });
      // Bulles sur une ellipse autour de la vue, espacées d'au moins 9 mm le long de son tour
      const double rx = v->largeur / 2 + placeBulles * 0.6, ry = v->hauteur / 2 + placeBulles * 0.6;
      const double perimetre = M_PI * (3 * (rx + ry) - std::sqrt((3 * rx + ry) * (rx + 3 * ry)));
      const double pasMin = bs.empty() ? 0 : std::min(2 * M_PI / static_cast<double>(bs.size()), 9.5 / std::max(1.0, perimetre) * 2 * M_PI);
      std::vector<double> angles;
      for (const auto& b : bs) angles.push_back(b.angle);
      for (int passe = 0; passe < 50; ++passe)
        for (std::size_t k = 1; k < angles.size(); ++k)
          if (angles[k] - angles[k - 1] < pasMin) {
            const double milieu = (angles[k] + angles[k - 1]) / 2;
            angles[k - 1] = milieu - pasMin / 2;
            angles[k] = milieu + pasMin / 2;
          }
      for (std::size_t k = 0; k < bs.size(); ++k) {
        const double bx = v->cx + rx * std::cos(angles[k]), by = v->cy + ry * std::sin(angles[k]);
        const double rb = 4;
        const double l = std::hypot(bs[k].ax - bx, bs[k].ay - by);
        if (l > rb) v->traits.push_back(segment(bs[k].ax, bs[k].ay, bx + (bs[k].ax - bx) / l * rb, by + (bs[k].ay - by) / l * rb, TypeTrait::Cote));
        Trait2D point, cercle;
        point.type = cercle.type = TypeTrait::Cote;
        point.rempli = true;
        for (int a = 0; a <= 12; ++a) point.points.push_back({bs[k].ax + 0.6 * std::cos(2 * M_PI * a / 12), bs[k].ay + 0.6 * std::sin(2 * M_PI * a / 12)});
        for (int a = 0; a <= 48; ++a) cercle.points.push_back({bx + rb * std::cos(2 * M_PI * a / 48), by + rb * std::sin(2 * M_PI * a / 48)});
        v->traits.push_back(point);
        v->traits.push_back(cercle);
        textesVues.push_back({bx, by - 1.6, 4.5, std::to_string(bs[k].rep), true, true});
      }
    }
  // Cadre et cartouche
  const double W = f.largeur, Hh = f.hauteur;
  f.cadre.push_back(segment(marge, marge, W - marge, marge));
  f.cadre.push_back(segment(W - marge, marge, W - marge, Hh - marge));
  f.cadre.push_back(segment(W - marge, Hh - marge, marge, Hh - marge));
  f.cadre.push_back(segment(marge, Hh - marge, marge, marge));
  const double x0 = W - marge - cartoucheL, y0 = marge, y1 = marge + cartoucheH;
  // Cartouche ISO 7200 (D2), trois bandes de 12 mm :
  //   haut   : titre et désignation | type de document et numéro d'identification
  //   milieu : tolérances générales | matière | masse | révision | feuille | échelle
  //   bas    : propriétaire légal | projet | créé par | approuvé par | date | symbole de projection
  const double h3 = cartoucheH / 3;
  f.cadre.push_back(segment(x0, y0, x0, y1));
  f.cadre.push_back(segment(x0, y1, W - marge, y1));
  f.cadre.push_back(segment(x0, y0 + h3, W - marge, y0 + h3));
  f.cadre.push_back(segment(x0, y0 + 2 * h3, W - marge, y0 + 2 * h3));
  for (double c : {125.0}) f.cadre.push_back(segment(x0 + c, y0 + 2 * h3, x0 + c, y1));
  for (double c : {55.0, 95.0, 125.0, 140.0, 160.0}) f.cadre.push_back(segment(x0 + c, y0 + h3, x0 + c, y0 + 2 * h3));
  for (double c : {45.0, 85.0, 115.0, 140.0, 160.0}) f.cadre.push_back(segment(x0 + c, y0, x0 + c, y0 + h3));
  // Texte raccourci à une largeur de papier (caractères d'environ 0,62 fois leur hauteur)
  auto raccourcir = [](const std::string& t, double largeur, double taille) {
    const std::size_t maxCar = static_cast<std::size_t>(std::max(2.0, largeur / (taille * 0.62)));
    if (longueurUtf8(t) <= maxCar) return t;
    std::string court;
    std::size_t n = 0;
    for (std::size_t k = 0; k < t.size(); ++k) {
      const bool debut = (static_cast<unsigned char>(t[k]) & 0xC0) != 0x80;
      if (debut && n >= maxCar - 1) break;
      court += t[k];
      n += debut;
    }
    return court + "…";
  };
  // Une case : petite étiquette en haut à gauche, valeur dessous
  auto caseCartouche = [&](double c0, double c1, double yb, const std::string& etiquetteCase, const std::string& valeur, double taille, bool gras) {
    f.textes.push_back({x0 + c0 + 1.2, yb + h3 - 3.2, 1.8, raccourcir(etiquetteCase, c1 - c0 - 2, 1.8), false});
    f.textes.push_back({x0 + c0 + 1.2, yb + 1.6, taille, raccourcir(valeur.empty() ? std::string("—") : valeur, c1 - c0 - 2, taille), gras});
  };
  // Bande du haut
  f.textes.push_back({x0 + 1.2, y0 + 2 * h3 + h3 - 3.2, 1.8, raccourcir(o.designation.empty() ? std::string("Titre") : "Titre · " + o.designation, 120, 1.8), false});
  f.textes.push_back({x0 + 3, y0 + 2 * h3 + 2.4, 5, raccourcir(o.titre.empty() ? std::string("Pièce") : o.titre, 118, 5), true});
  caseCartouche(125, 180, y0 + 2 * h3, o.typeDocument.empty() ? std::string("N° d'identification") : o.typeDocument + " · n° d'identification", o.reference, 3.5, true);
  // Bande du milieu
  caseCartouche(0, 55, y0 + h3, "Tolérances générales", o.toleranceGenerale, 2.6, false);
  caseCartouche(55, 95, y0 + h3, "Matière", o.materiau, 2.6, false);
  caseCartouche(95, 125, y0 + h3, "Masse", o.masse, 2.6, false);
  caseCartouche(125, 140, y0 + h3, "Rév.", o.revision, 3, true);
  caseCartouche(140, 160, y0 + h3, "Feuille", o.numeroFeuille, 3, false);
  caseCartouche(160, 180, y0 + h3, "Échelle", texteEchelle(e), 3.5, true);
  // Bande du bas
  caseCartouche(0, 45, y0, "Propriétaire légal", o.proprietaire.empty() ? std::string("Prométhée") : o.proprietaire, 2.6, false);
  caseCartouche(45, 85, y0, "Projet", o.projet, 2.6, false);
  caseCartouche(85, 115, y0, "Créé par", o.auteur, 2.6, false);
  caseCartouche(115, 140, y0, "Approuvé par", o.approbateur, 2.6, false);
  caseCartouche(140, 160, y0, "Date", o.date, 2.4, false);
  // Symbole de projection (ISO 5456-2) : tronc de cône vu de face et de bout ; premier dièdre, la vue de bout à
  // droite ; troisième dièdre, à gauche
  {
    const double cy = y0 + h3 / 2 - 0.6, xa = x0 + 160, xb = x0 + 180;
    const double xt = o.troisiemeDiedre ? xb - 10.5 : xa + 1.8;   // trapèze
    const double xc = o.troisiemeDiedre ? xa + 5.5 : xb - 5.5;    // cercles
    Trait2D t;
    t.type = TypeTrait::Vu;
    t.points = {{xt, cy - 1.5}, {xt + 8, cy - 3}, {xt + 8, cy + 3}, {xt, cy + 1.5}, {xt, cy - 1.5}};
    f.cadre.push_back(t);
    f.cadre.push_back(segment(xt - 1.2, cy, xt + 9.2, cy, TypeTrait::Axe));
    for (const double r : {1.5, 3.0}) {
      Trait2D c;
      c.type = TypeTrait::Vu;
      for (int k = 0; k <= 36; ++k) c.points.push_back({xc + r * std::cos(2 * M_PI * k / 36), cy + r * std::sin(2 * M_PI * k / 36)});
      f.cadre.push_back(c);
    }
    f.cadre.push_back(segment(xc - 4, cy, xc + 4, cy, TypeTrait::Axe));
    f.cadre.push_back(segment(xc, cy - 4, xc, cy + 4, TypeTrait::Axe));
  }
  // Nomenclature au-dessus du cartouche, lue de bas en haut (ISO 7573) : en-tête en bas, repère 1 juste au-dessus
  if (!o.nomenclature.empty()) {
    const double colonnes[] = {0, 14, 26, 100, 140, 180};
    const char* titres[] = {"Rep.", "Qté", "Désignation", "Matière", "Référence"};
    auto ligne = [&](double yb, const std::array<std::string, 5>& cases, bool gras) {
      for (int c = 0; c < 5; ++c) {
        std::string t = cases[static_cast<std::size_t>(c)];
        const double largeurCase = colonnes[c + 1] - colonnes[c] - 2;
        const std::size_t maxCar = static_cast<std::size_t>(std::max(1.0, largeurCase / 1.75));
        if (longueurUtf8(t) > maxCar) {
          std::string court;
          std::size_t n = 0;
          for (std::size_t k = 0; k < t.size(); ++k) {
            const bool debut = (static_cast<unsigned char>(t[k]) & 0xC0) != 0x80;
            if (debut && n >= maxCar - 1) break;
            court += t[k];
            n += debut;
          }
          t = court + "…";
        }
        f.textes.push_back({x0 + colonnes[c] + 1.5, yb + 1.8, 3, t, gras, false});
      }
    };
    (void)ligne;
    double y = y1;
    std::array<std::string, 5> entete;
    for (int c = 0; c < 5; ++c) entete[static_cast<std::size_t>(c)] = titres[c];
    std::vector<std::array<std::string, 5>> lignes{entete};
    for (const auto& l : o.nomenclature) lignes.push_back({std::to_string(l.repere), std::to_string(l.quantite), l.designation, l.materiau, l.reference});
    for (std::size_t k = 0; k < lignes.size(); ++k) {
      f.cadre.push_back(segment(x0, y + 6, W - marge, y + 6));
      for (double c : colonnes) f.cadre.push_back(segment(x0 + c, y, x0 + c, y + 6));
      ligne(y, lignes[k], k == 0);
      y += 6;
    }
  }
  // Notes générales au-dessus
  if (!notes.empty()) {
    double y = y1 + hauteurTable + 3;
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
      f.textes.push_back({x0 + 1, y, 3, *it, false, false});
      y += 4.5;
    }
    f.textes.push_back({x0 + 1, y + 0.5, 3.2, "Notes :", true, false});
  }
  // Étiquettes centrées sous les vues ; l'isométrique réduite dit son échelle (MP-10)
  for (const auto& v : f.vues) {
    if (v.nom == "Face" || v.nom.rfind("Détail", 0) == 0) {
      if (v.nom.rfind("Détail", 0) == 0) f.textes.push_back({v.cx, v.cy - v.hauteur / 2 - 4.5, 3.5, v.nom, true, true});
      continue;
    }
    std::string t = v.nom;
    if (std::abs(v.echelle - e) > 1e-12) t += " (" + texteEchelle(v.echelle) + ")";
    double basVue = v.cy - v.hauteur / 2;
    for (int i = 0; i < 4; ++i)
      if (indiceVue[i] >= 0 && &f.vues[static_cast<std::size_t>(indiceVue[i])] == &v) basVue -= ext[i].b - etiquette;
    f.textes.push_back({v.cx, basVue - 4.5, 3, t, v.nom.rfind("Coupe", 0) == 0, true});
  }
  for (auto& t : textesVues) f.textes.push_back(t);
  return f;
}

std::string echapperXml(const std::string& t) {
  std::string r;
  r.reserve(t.size());
  for (char c : t) {
    switch (c) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      case '\'': r += "&apos;"; break;
      default: r += c;
    }
  }
  return r;
}

std::string versCp1252(const std::string& s) {
  static const std::map<unsigned, unsigned char> speciaux = {
      {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87}, {0x02C6, 0x88}, {0x2030, 0x89},
      {0x0160, 0x8A}, {0x2039, 0x8B}, {0x0152, 0x8C}, {0x017D, 0x8E}, {0x2018, 0x91}, {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95},
      {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98}, {0x2122, 0x99}, {0x0161, 0x9A}, {0x203A, 0x9B}, {0x0153, 0x9C}, {0x017E, 0x9E}, {0x0178, 0x9F}};
  std::string r;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    unsigned cp = 0;
    int n = 0;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
    else { r += '?'; ++i; continue; }
    if (i + static_cast<std::size_t>(n) > s.size()) { r += '?'; break; }
    for (int k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3F);
    i += static_cast<std::size_t>(n);
    if (cp < 0x80 || (cp >= 0xA0 && cp <= 0xFF)) r += static_cast<char>(cp);
    else if (const auto it = speciaux.find(cp); it != speciaux.end()) r += static_cast<char>(it->second);
    else r += '?';
  }
  return r;
}

bool ecrireDxf(const Feuille& f, const std::string& chemin) {
  std::ostringstream s;
  s.imbue(std::locale::classic());
  // R12 en Windows-1252, déclarés dans l'en-tête : les lecteurs relisent les accents (MP-04)
  s << "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n9\n$DWGCODEPAGE\n3\nANSI_1252\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n";
  auto ltype = [&](const char* nom, const char* desc, const std::vector<double>& motif) {
    double total = 0;
    for (double m : motif) total += m;
    s << "0\nLTYPE\n2\n" << nom << "\n70\n0\n3\n" << desc << "\n72\n65\n73\n" << motif.size() << "\n40\n" << total << "\n";
    for (std::size_t k = 0; k < motif.size(); ++k) s << "49\n" << (k % 2 ? -motif[k] : motif[k]) << "\n";
  };
  s << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLTYPE\n70\n3\n";
  ltype("CONTINUOUS", "Trait continu", {});
  ltype("HIDDEN", "Tirets ISO 128", motifTrait(TypeTrait::Cache).motif);
  ltype("CENTER", "Trait mixte ISO 128", motifTrait(TypeTrait::Axe).motif);
  s << "0\nENDTAB\n";
  s << "0\nTABLE\n2\nLAYER\n70\n8\n";
  for (const auto& [nom, couleur, type] : {std::tuple{"VUES", 7, "CONTINUOUS"}, std::tuple{"CACHEES", 8, "HIDDEN"}, std::tuple{"AXES", 1, "CENTER"},
                                           std::tuple{"TANGENTES", 8, "CONTINUOUS"}, std::tuple{"CADRE", 7, "CONTINUOUS"}, std::tuple{"COTES", 3, "CONTINUOUS"},
                                           std::tuple{"HACHURES", 8, "CONTINUOUS"}, std::tuple{"FINS", 7, "CONTINUOUS"}})
    s << "0\nLAYER\n2\n" << nom << "\n70\n0\n62\n" << couleur << "\n6\n" << type << "\n";
  s << "0\nENDTAB\n0\nENDSEC\n";
  s << "0\nSECTION\n2\nENTITIES\n";
  auto lignes = [&](const Trait2D& t, const char* calque) {
    for (size_t i = 1; i < t.points.size(); ++i)
      s << "0\nLINE\n8\n" << calque << "\n10\n" << t.points[i - 1].first << "\n20\n" << t.points[i - 1].second << "\n30\n0\n11\n" << t.points[i].first << "\n21\n" << t.points[i].second << "\n31\n0\n";
  };
  auto calque = [](TypeTrait t) {
    switch (t) {
      case TypeTrait::Cache: return "CACHEES";
      case TypeTrait::Axe: return "AXES";
      case TypeTrait::Tangente: return "TANGENTES";
      case TypeTrait::Cote: return "COTES";
      case TypeTrait::Hachure: return "HACHURES";
      case TypeTrait::Fin: return "FINS";
      case TypeTrait::Vu: break;
    }
    return "VUES";
  };
  // Pointe de flèche : triangle plein (SOLID, quatrième sommet répété)
  auto plein = [&](const Trait2D& t, const char* cq) {
    if (t.points.size() < 3) return;
    const auto& a = t.points[0];
    const auto& b = t.points[1];
    const auto& c = t.points[2];
    s << "0\nSOLID\n8\n" << cq << "\n10\n" << a.first << "\n20\n" << a.second << "\n30\n0\n11\n" << b.first << "\n21\n" << b.second << "\n31\n0\n12\n" << c.first << "\n22\n"
      << c.second << "\n32\n0\n13\n" << c.first << "\n23\n" << c.second << "\n33\n0\n";
  };
  for (const auto& v : f.vues)
    for (const auto& t : v.traits) {
      if (t.rempli && t.points.size() <= 5) plein(t, calque(t.type));
      lignes(t, calque(t.type));
    }
  for (const auto& t : f.cadre) lignes(t, "CADRE");
  for (const auto& x : f.textes) {
    const std::string texte = versCp1252(x.texte);
    s << "0\nTEXT\n8\nCADRE\n10\n" << x.x << "\n20\n" << x.y << "\n30\n0\n40\n" << x.taille << "\n1\n" << texte << "\n";
    if (x.angle != 0) s << "50\n" << x.angle << "\n";
    if (x.centre) s << "72\n1\n11\n" << x.x << "\n21\n" << x.y << "\n31\n0\n";
  }
  s << "0\nENDSEC\n0\nEOF\n";
  try {
    ecrireFichierAtomique(cheminFichier(chemin), s.str());
  } catch (...) {
    return false;
  }
  return true;
}

bool ecrireSvg(const Feuille& f, const std::string& chemin) {
  std::ostringstream s;
  s.imbue(std::locale::classic());
  auto Y = [&](double y) { return f.hauteur - y; };
  s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << f.largeur << "mm\" height=\"" << f.hauteur << "mm\" viewBox=\"0 0 " << f.largeur << " " << f.hauteur << "\">\n";
  s << "<title>" << echapperXml(f.titre) << "</title>\n";
  s << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
  auto style = [](TypeTrait t) {
    const MotifTrait m = motifTrait(t);
    std::ostringstream st;
    st.imbue(std::locale::classic());
    st << "stroke=\"" << (t == TypeTrait::Cache ? "#555" : t == TypeTrait::Axe ? "#444" : t == TypeTrait::Hachure ? "#333" : "black") << "\" stroke-width=\"" << m.epaisseur << "\"";
    if (!m.motif.empty()) {
      st << " stroke-dasharray=\"";
      for (std::size_t k = 0; k < m.motif.size(); ++k) st << (k ? "," : "") << m.motif[k];
      st << "\"";
    }
    return st.str();
  };
  auto chemin2 = [&](const Trait2D& t, const std::string& st) {
    if (t.points.size() < 2) return;
    s << "<path fill=\"" << (t.rempli ? "black" : "none") << "\" " << st << " d=\"M";
    for (size_t i = 0; i < t.points.size(); ++i) s << (i ? " L" : "") << t.points[i].first << " " << Y(t.points[i].second);
    s << "\"/>\n";
  };
  // Ordre de tracé : cachés, axes, tangentes, puis vus par-dessus
  for (TypeTrait type : {TypeTrait::Hachure, TypeTrait::Cache, TypeTrait::Axe, TypeTrait::Tangente, TypeTrait::Fin, TypeTrait::Cote, TypeTrait::Vu})
    for (const auto& v : f.vues)
      for (const auto& t : v.traits)
        if (t.type == type) chemin2(t, style(type));
  std::ostringstream cadre;
  cadre << "stroke=\"black\" stroke-width=\"" << EPAISSEUR_CADRE << "\"";
  for (const auto& t : f.cadre) chemin2(t, cadre.str());
  for (const auto& x : f.textes) {
    s << "<text x=\"" << x.x << "\" y=\"" << Y(x.y) << "\" font-family=\"sans-serif\" font-size=\"" << x.taille << "\"" << (x.gras ? " font-weight=\"bold\"" : "")
      << (x.centre ? " text-anchor=\"middle\"" : "");
    if (x.angle != 0) s << " transform=\"rotate(" << -x.angle << " " << x.x << " " << Y(x.y) << ")\"";
    s << ">" << echapperXml(x.texte) << "</text>\n";
  }
  s << "</svg>\n";
  try {
    ecrireFichierAtomique(cheminFichier(chemin), s.str());
  } catch (...) {
    return false;
  }
  return true;
}

}  // namespace prom
