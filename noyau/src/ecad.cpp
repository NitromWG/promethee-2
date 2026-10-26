// Prométhée : socle de la CAO électronique (unités, identifiants, géométrie, polygones, index spatial).
// Licence GPL-3.0-only.
#include "promethee/ecad.hpp"
#include "promethee/entier128.hpp"
#include "promethee/hachage.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <numbers>
#include <random>

#include "clipper2/clipper.h"
#include "clipper2/clipper.triangulation.h"

namespace prom::ecad {

void Boite::etendre(Pt p) {
  if (vide()) { x0 = x1 = p.x; y0 = y1 = p.y; return; }
  x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
  y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
}
void Boite::etendre(const Boite& b) {
  if (b.vide()) return;
  etendre(Pt{b.x0, b.y0});
  etendre(Pt{b.x1, b.y1});
}
Boite Boite::gonflee(Nm d) const { return vide() ? *this : Boite{x0 - d, y0 - d, x1 + d, y1 + d}; }
bool Boite::touche(const Boite& b) const { return !vide() && !b.vide() && x0 <= b.x1 && b.x0 <= x1 && y0 <= b.y1 && b.y0 <= y1; }
bool Boite::contient(Pt p) const { return !vide() && p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
bool Boite::contient(const Boite& b) const { return !vide() && !b.vide() && b.x0 >= x0 && b.x1 <= x1 && b.y0 >= y0 && b.y1 <= y1; }

std::string nouvelUuid() {
  thread_local std::mt19937_64 gen{std::random_device{}() ^ static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count())};
  const std::uint64_t a = gen(), b = gen();
  char s[40];
  std::snprintf(s, sizeof s, "%08x-%04x-4%03x-%04x-%012llx", static_cast<unsigned>(a >> 32), static_cast<unsigned>((a >> 16) & 0xffff), static_cast<unsigned>(a & 0xfff),
                static_cast<unsigned>(((b >> 48) & 0x3fff) | 0x8000), static_cast<unsigned long long>(b & 0xffffffffffffULL));
  return s;
}

std::string uuidDerive(std::string_view graine) {
  const std::string h = sha256Hex(graine);
  // Forme 8-4-4-4-12, version 8 (identifiant propre à l'application, RFC 9562) et variante 10xx
  return h.substr(0, 8) + '-' + h.substr(8, 4) + "-8" + h.substr(13, 3) + '-' + "89ab"[std::stoi(h.substr(16, 1), nullptr, 16) & 3] + h.substr(17, 3) + '-' + h.substr(20, 12);
}

Pt tournerQuarts(Pt p, int quarts) {
  const int q = ((quarts % 4) + 4) % 4;
  for (int i = 0; i < q; ++i) p = {p.y, -p.x};
  return p;
}

Pt tournerDegres(Pt p, double degres) {
  const double r = std::fmod(degres, 360.0);
  if (std::abs(std::remainder(r, 90.0)) < 1e-9) return tournerQuarts(p, static_cast<int>(std::llround(r / 90.0)));
  const double a = degres * std::numbers::pi / 180, c = std::cos(a), s = std::sin(a);
  const double x = static_cast<double>(p.x), y = static_cast<double>(p.y);
  return {static_cast<Nm>(std::llround(x * c + y * s)), static_cast<Nm>(std::llround(-x * s + y * c))};
}

double distance(Pt a, Pt b) { return std::hypot(static_cast<double>(a.x - b.x), static_cast<double>(a.y - b.y)); }

Pt projeteSurSegment(Pt p, Pt a, Pt b) {
  const double dx = static_cast<double>(b.x - a.x), dy = static_cast<double>(b.y - a.y), l2 = dx * dx + dy * dy;
  if (l2 <= 0) return a;
  const double t = std::clamp((static_cast<double>(p.x - a.x) * dx + static_cast<double>(p.y - a.y) * dy) / l2, 0.0, 1.0);
  return {a.x + static_cast<Nm>(std::llround(t * dx)), a.y + static_cast<Nm>(std::llround(t * dy))};
}

namespace {
// Carré de la distance d'un point à un segment : la racine n'est prise qu'une fois, au bout des boucles de la DRC
// (std::hypot, exact au dernier bit, coûtait le quart du temps de la DRC d'une grande carte) ; en nanomètres, les
// carrés restent loin des limites d'un double
double distance2PointSegment(Pt p, Pt a, Pt b) {
  const double dx = static_cast<double>(b.x - a.x), dy = static_cast<double>(b.y - a.y), l2 = dx * dx + dy * dy;
  const double px = static_cast<double>(p.x - a.x), py = static_cast<double>(p.y - a.y);
  if (l2 <= 0) return px * px + py * py;
  const double t = std::clamp((px * dx + py * dy) / l2, 0.0, 1.0);
  const double ex = px - t * dx, ey = py - t * dy;
  return ex * ex + ey * ey;
}
}  // namespace

double distancePointSegment(Pt p, Pt a, Pt b) { return std::sqrt(distance2PointSegment(p, a, b)); }

namespace {
// Orientation exacte (9.6, R16) : le produit vectoriel en entiers de 128 bits ; le signe seul compte (-1, 0, 1), sans
// l'arrondi d'un double sur des coordonnées de carte en nanomètres
int orient(Pt a, Pt b, Pt c) {
  const Entier128 v = static_cast<Entier128>(b.x - a.x) * (c.y - a.y) - static_cast<Entier128>(b.y - a.y) * (c.x - a.x);
  return v > 0 ? 1 : v < 0 ? -1 : 0;
}
}  // namespace

bool surSegment(Pt p, Pt a, Pt b) {
  if (p.x < std::min(a.x, b.x) - 1 || p.x > std::max(a.x, b.x) + 1 || p.y < std::min(a.y, b.y) - 1 || p.y > std::max(a.y, b.y) + 1) return false;
  if (a.x == b.x) return std::abs(p.x - a.x) <= 1;
  if (a.y == b.y) return std::abs(p.y - a.y) <= 1;
  return distancePointSegment(p, a, b) <= 1.0;
}

bool segmentsSeCoupent(Pt a1, Pt a2, Pt b1, Pt b2) {
  const int d1 = orient(b1, b2, a1), d2 = orient(b1, b2, a2), d3 = orient(a1, a2, b1), d4 = orient(a1, a2, b2);
  if (d1 * d2 < 0 && d3 * d4 < 0) return true;
  // Contact exact : un bout aligné sur l'autre segment et dans sa boîte (sans la tolérance de surSegment)
  auto sur = [](int o, Pt p, Pt a, Pt b) { return o == 0 && std::min(a.x, b.x) <= p.x && p.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= p.y && p.y <= std::max(a.y, b.y); };
  return sur(d1, a1, b1, b2) || sur(d2, a2, b1, b2) || sur(d3, b1, a1, a2) || sur(d4, b2, a1, a2);
}

namespace {
double distance2Segments(Pt a1, Pt a2, Pt b1, Pt b2) {
  if (segmentsSeCoupent(a1, a2, b1, b2)) return 0;
  return std::min({distance2PointSegment(a1, b1, b2), distance2PointSegment(a2, b1, b2), distance2PointSegment(b1, a1, a2), distance2PointSegment(b2, a1, a2)});
}
}  // namespace

double distanceSegments(Pt a1, Pt a2, Pt b1, Pt b2) { return std::sqrt(distance2Segments(a1, a2, b1, b2)); }

// ---------- Polygones (Clipper2, entiers 64 bits : le nanomètre en est l'unité) ----------
namespace {
using namespace Clipper2Lib;
constexpr double TOLERANCE_ARC = 2000.0;  // 2 µm entre l'arc vrai et sa corde
Path64 enClipper(const Polygone& p) {
  Path64 r;
  r.reserve(p.size());
  for (const Pt& q : p) r.emplace_back(q.x, q.y);
  return r;
}
Paths64 enClipper(const Polygones& p) {
  Paths64 r;
  r.reserve(p.size());
  for (const auto& x : p)
    if (x.size() >= 3) r.push_back(enClipper(x));
  return r;
}
Polygone depuis(const Path64& p) {
  Polygone r;
  r.reserve(p.size());
  for (const auto& q : p) r.push_back({q.x, q.y});
  return r;
}
Polygones depuis(const Paths64& p) {
  Polygones r;
  r.reserve(p.size());
  for (const auto& x : p) r.push_back(depuis(x));
  return r;
}
int segmentsPour(double r) {
  if (r <= 0) return 8;
  const double a = std::acos(std::clamp(1.0 - TOLERANCE_ARC / r, -1.0, 1.0));
  return std::clamp(static_cast<int>(std::ceil(std::numbers::pi / std::max(a, 1e-4))), 16, 160);
}
Pt pt(double x, double y) { return {static_cast<Nm>(std::llround(x)), static_cast<Nm>(std::llround(y))}; }
Polygone transformer(const Polygone& p, Pt c, double degres) {
  Polygone r;
  r.reserve(p.size());
  for (const Pt& q : p) r.push_back(tournerDegres(q, degres) + c);
  return r;
}
std::int64_t divBas(std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
}  // namespace

Polygones unir(const Polygones& a) { return depuis(Union(enClipper(a), FillRule::NonZero)); }
Polygones soustraire(const Polygones& a, const Polygones& b) { return depuis(Difference(enClipper(a), enClipper(b), FillRule::NonZero)); }
Polygones intersecter(const Polygones& a, const Polygones& b) { return depuis(Intersect(enClipper(a), enClipper(b), FillRule::NonZero)); }
Polygones gonfler(const Polygones& a, Nm d) {
  if (d == 0) return unir(a);
  return depuis(InflatePaths(enClipper(a), static_cast<double>(d), JoinType::Round, EndType::Polygon, 2.0, TOLERANCE_ARC));
}

Polygones trianguler(const Polygones& a) {
  const Paths64 propres = Union(enClipper(a), FillRule::NonZero);
  Paths64 sol;
  if (Triangulate(propres, sol, false) == TriangulateResult::success) return depuis(sol);
  // Repli : bandes horizontales coupées à chaque sommet ; aucun trou ne peut tenir dans une bande
  std::vector<Nm> ys;
  for (const auto& p : propres)
    for (const auto& q : p) ys.push_back(q.y);
  std::sort(ys.begin(), ys.end());
  ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
  const Boite b = boite(depuis(propres));
  Polygones r;
  for (std::size_t i = 0; i + 1 < ys.size(); ++i) {
    const Polygone bande{{b.x0 - 1, ys[i]}, {b.x1 + 1, ys[i]}, {b.x1 + 1, ys[i + 1]}, {b.x0 - 1, ys[i + 1]}};
    for (const auto& x : depuis(Intersect(propres, Paths64{enClipper(bande)}, FillRule::NonZero))) r.push_back(x);
  }
  return r;
}

double aireMm2(const Polygones& p) {
  double a = 0;
  for (const auto& x : Union(enClipper(p), FillRule::NonZero)) a += Area(x);
  return std::abs(a) * 1e-12;
}
double aireMm2(const Polygone& p) { return p.size() < 3 ? 0 : std::abs(Area(enClipper(p))) * 1e-12; }

namespace {
using Grand = Entier128;  // produits exacts de coordonnées de 64 bits (MSVC n'a pas __int128)
// Position d'un point par rapport à un polygone simple : -1 dehors, 0 sur le bord, 1 dedans. Arithmétique exacte, sans
// allocation (9.2) : les tests d'inclusion se comptent par millions dans la DRC d'une grande carte.
int positionPoint(const Polygone& p, Pt q) {
  const std::size_t n = p.size();
  if (n < 3) return -1;
  bool dedans = false;
  for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
    const Pt a = p[j], b = p[i];
    // Sur l'arête : colinéaire et dans sa boîte
    if (q.x >= std::min(a.x, b.x) && q.x <= std::max(a.x, b.x) && q.y >= std::min(a.y, b.y) && q.y <= std::max(a.y, b.y) &&
        static_cast<Grand>(b.x - a.x) * (q.y - a.y) == static_cast<Grand>(b.y - a.y) * (q.x - a.x))
      return 0;
    // Croisement de la demi-droite vers +x, règle demi-ouverte sur y
    if ((a.y > q.y) != (b.y > q.y)) {
      const Grand dy = b.y - a.y;
      const Grand num = static_cast<Grand>(a.x - q.x) * dy + static_cast<Grand>(q.y - a.y) * (b.x - a.x);
      if ((num > 0) == (dy > 0)) dedans = !dedans;
    }
  }
  return dedans ? 1 : -1;
}
Grand aireDouble(const Polygone& p) {
  Grand a = 0;
  for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += static_cast<Grand>(p[j].x) * p[i].y - static_cast<Grand>(p[i].x) * p[j].y;
  return a;
}
}  // namespace

bool dedans(const Polygone& p, Pt q) { return positionPoint(p, q) >= 0; }

bool dedans(const Polygones& p, Pt q) {
  int enroulement = 0;
  for (const auto& x : p) {
    if (x.size() < 3) continue;
    const int pos = positionPoint(x, q);
    if (pos < 0) continue;
    // Point sur le bord d'un contour ou d'un trou (9.6) : il touche le cuivre, il est dedans
    if (pos == 0) return true;
    enroulement += aireDouble(x) >= 0 ? 1 : -1;
  }
  return enroulement != 0;
}

Boite boite(const Polygone& p) {
  Boite b;
  for (const Pt& q : p) b.etendre(q);
  return b;
}
Boite boite(const Polygones& p) {
  Boite b;
  for (const auto& x : p) b.etendre(boite(x));
  return b;
}

Polygone orienter(Polygone p) {
  if (p.size() >= 3 && Area(enClipper(p)) < 0) std::reverse(p.begin(), p.end());
  return p;
}

Polygone cercle(Pt c, Nm r) {
  const int n = segmentsPour(static_cast<double>(r));
  const double rr = static_cast<double>(r) / std::cos(std::numbers::pi / n);  // polygone circonscrit
  Polygone p;
  p.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const double a = 2 * std::numbers::pi * i / n;
    p.push_back(pt(static_cast<double>(c.x) + rr * std::cos(a), static_cast<double>(c.y) + rr * std::sin(a)));
  }
  return orienter(p);
}

Polygone rectangle(Pt c, Nm l, Nm h, double degres) {
  const Polygone p{{-l / 2, -h / 2}, {l / 2, -h / 2}, {l / 2, h / 2}, {-l / 2, h / 2}};
  return orienter(transformer(p, c, degres));
}

namespace {
// Arc de centre (cx, cy) et de rayon r, de a0 à a1 en n pas, extrémités comprises et posées sur le cercle vrai
// (les côtés droits qui s'y raccordent restent exacts). Extérieure : sommets intermédiaires sur les tangentes ;
// intérieure : sommets sur l'arc, cordes à l'intérieur.
void ajouterArc(Polygone& p, double cx, double cy, double r, double a0, double a1, int n, Approche approche) {
  const double pas = (a1 - a0) / n;
  p.push_back(pt(cx + r * std::cos(a0), cy + r * std::sin(a0)));
  if (approche == Approche::Exterieure) {
    const double rr = r / std::cos(pas / 2);
    for (int i = 0; i < n; ++i) {
      const double a = a0 + (i + 0.5) * pas;
      p.push_back(pt(cx + rr * std::cos(a), cy + rr * std::sin(a)));
    }
  } else {
    for (int i = 1; i < n; ++i) {
      const double a = a0 + i * pas;
      p.push_back(pt(cx + r * std::cos(a), cy + r * std::sin(a)));
    }
  }
  p.push_back(pt(cx + r * std::cos(a1), cy + r * std::sin(a1)));
}
}  // namespace

Polygone rectangleArrondi(Pt c, Nm l, Nm h, Nm r, double degres, Approche approche) {
  r = std::min({r, l / 2, h / 2});
  if (r <= 0) return rectangle(c, l, h, degres);
  const int n = std::max(4, segmentsPour(static_cast<double>(r)) / 4);
  Polygone p;
  const double cx[4] = {static_cast<double>(l / 2 - r), static_cast<double>(-(l / 2 - r)), static_cast<double>(-(l / 2 - r)), static_cast<double>(l / 2 - r)};
  const double cy[4] = {static_cast<double>(h / 2 - r), static_cast<double>(h / 2 - r), static_cast<double>(-(h / 2 - r)), static_cast<double>(-(h / 2 - r))};
  for (int k = 0; k < 4; ++k) ajouterArc(p, cx[k], cy[k], static_cast<double>(r), std::numbers::pi / 2 * k, std::numbers::pi / 2 * (k + 1), n, approche);
  // Deux coins qui se touchent (côté réduit à rien) donnent des sommets confondus : on les retire
  p.erase(std::unique(p.begin(), p.end()), p.end());
  if (p.size() > 1 && p.front() == p.back()) p.pop_back();
  return orienter(transformer(p, c, degres));
}

Polygone oblong(Pt a, Pt b, Nm largeur) {
  const double r = static_cast<double>(largeur) / 2;
  if (a == b) return cercle(a, largeur / 2);
  const double th = std::atan2(static_cast<double>(b.y - a.y), static_cast<double>(b.x - a.x));
  const int n = std::max(8, segmentsPour(r) / 2);
  Polygone p;
  ajouterArc(p, static_cast<double>(b.x), static_cast<double>(b.y), r, th - std::numbers::pi / 2, th + std::numbers::pi / 2, n, Approche::Exterieure);
  ajouterArc(p, static_cast<double>(a.x), static_cast<double>(a.y), r, th + std::numbers::pi / 2, th + 3 * std::numbers::pi / 2, n, Approche::Exterieure);
  return orienter(p);
}

double distancePolygoneSegment(const Polygone& p, Pt a, Pt b) {
  if (p.size() < 3) return 1e18;
  if (dedans(p, a) || dedans(p, b)) return 0;
  double d2 = 1e36;
  for (std::size_t i = 0; i < p.size(); ++i) {
    d2 = std::min(d2, distance2Segments(p[i], p[(i + 1) % p.size()], a, b));
    if (d2 <= 0) return 0;
  }
  return std::sqrt(d2);
}

double distancePolygones(const Polygone& a, const Polygone& b) {
  if (a.size() < 3 || b.size() < 3) return 1e18;
  // Test de boîtes (9.6, R16) : sans recouvrement des boîtes, aucun des deux ne contient l'autre
  if (boite(a).touche(boite(b)) && (dedans(a, b.front()) || dedans(b, a.front()))) return 0;
  double d = 1e18;
  for (std::size_t i = 0; i < a.size(); ++i) {
    d = std::min(d, distancePolygoneSegment(b, a[i], a[(i + 1) % a.size()]));
    if (d <= 0) return 0;
  }
  return d;
}

std::vector<Ilot> ilots(const Polygones& p) {
  Clipper64 c;
  c.AddSubject(enClipper(p));
  PolyTree64 arbre;
  c.Execute(ClipType::Union, FillRule::NonZero, arbre);
  std::vector<Ilot> r;
  std::function<void(const PolyPath64&)> parcourir = [&](const PolyPath64& n) {
    for (std::size_t i = 0; i < n.Count(); ++i) {
      const PolyPath64* e = n.Child(i);
      Ilot il;
      il.exterieur = depuis(e->Polygon());
      for (std::size_t j = 0; j < e->Count(); ++j) {
        const PolyPath64* t = e->Child(j);
        il.trous.push_back(depuis(t->Polygon()));
        parcourir(*t);
      }
      r.push_back(std::move(il));
    }
  };
  parcourir(arbre);
  return r;
}

// ---------- Index spatial ----------
void IndexSpatial::inserer(int id, const Boite& b) {
  if (b.vide()) return;
  const std::int64_t i0 = divBas(b.x0, m_cellule), i1 = divBas(b.x1, m_cellule), j0 = divBas(b.y0, m_cellule), j1 = divBas(b.y1, m_cellule);
  if ((i1 - i0 + 1) * (j1 - j0 + 1) > 4096) { m_grands.push_back(id); return; }
  for (std::int64_t i = i0; i <= i1; ++i)
    for (std::int64_t j = j0; j <= j1; ++j) m_cases[cle(i, j)].push_back(id);
}

std::vector<int> IndexSpatial::chercher(const Boite& b) const {
  std::vector<int> r = m_grands;
  if (!b.vide()) {
    const std::int64_t i0 = divBas(b.x0, m_cellule), i1 = divBas(b.x1, m_cellule), j0 = divBas(b.y0, m_cellule), j1 = divBas(b.y1, m_cellule);
    if ((i1 - i0 + 1) * (j1 - j0 + 1) > static_cast<std::int64_t>(m_cases.size()) * 4) {
      for (const auto& [k, v] : m_cases) r.insert(r.end(), v.begin(), v.end());
    } else {
      for (std::int64_t i = i0; i <= i1; ++i)
        for (std::int64_t j = j0; j <= j1; ++j)
          if (const auto it = m_cases.find(cle(i, j)); it != m_cases.end()) r.insert(r.end(), it->second.begin(), it->second.end());
    }
  }
  std::sort(r.begin(), r.end());
  r.erase(std::unique(r.begin(), r.end()), r.end());
  return r;
}

}  // namespace prom::ecad
