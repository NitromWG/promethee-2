// Prométhée : contours quelconques et distance signée au bord d'une carte. Licence GPL-3.0-only.
#include "promethee/derive.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace prom {

namespace {
bool proches(const Point& a, const Point& b) { return std::hypot(a.x - b.x, a.y - b.y) < 1e-3; }
bool cercle(const Point& a, const Point& b, const Point& c, Point& centre, double& r) {
  const double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
  if (std::abs(d) < 1e-12) return false;
  const double a2 = a.x * a.x + a.y * a.y, b2 = b.x * b.x + b.y * b.y, c2 = c.x * c.x + c.y * c.y;
  centre = {(a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / d, (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / d};
  r = std::hypot(a.x - centre.x, a.y - centre.y);
  return true;
}
double distSegment(double x, double y, const Point& a, const Point& b) {
  const double vx = b.x - a.x, vy = b.y - a.y, l2 = vx * vx + vy * vy;
  double t = l2 > 0 ? ((x - a.x) * vx + (y - a.y) * vy) / l2 : 0;
  t = std::clamp(t, 0.0, 1.0);
  return std::hypot(x - (a.x + t * vx), y - (a.y + t * vy));
}
}  // namespace

std::vector<Point> Contour::polygone(double pas) const {
  std::vector<Point> p;
  for (const auto& e : elements) {
    Point c{};
    double r = 0;
    if (e.milieu && cercle(e.debut, *e.milieu, e.fin, c, r)) {
      double a0 = std::atan2(e.debut.y - c.y, e.debut.x - c.x), am = std::atan2(e.milieu->y - c.y, e.milieu->x - c.x), a1 = std::atan2(e.fin.y - c.y, e.fin.x - c.x);
      // Sens de l'arc : celui qui passe par le point milieu.
      auto norm = [](double a) { while (a < 0) a += 2 * std::numbers::pi; while (a >= 2 * std::numbers::pi) a -= 2 * std::numbers::pi; return a; };
      double balayage = norm(a1 - a0);
      if (norm(am - a0) > balayage) balayage -= 2 * std::numbers::pi;
      if (proches(e.debut, e.fin)) balayage = 2 * std::numbers::pi;
      const int n = std::max(2, static_cast<int>(std::ceil(std::abs(balayage) * r / pas)));
      for (int i = 0; i < n; ++i) p.push_back({c.x + r * std::cos(a0 + balayage * i / n), c.y + r * std::sin(a0 + balayage * i / n)});
    } else {
      p.push_back(e.debut);
    }
  }
  return p;
}

double Contour::aire() const {
  const auto p = polygone(1.0);
  double a = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    const Point& u = p[i];
    const Point& v = p[(i + 1) % p.size()];
    a += u.x * v.y - v.x * u.y;
  }
  return a / 2;
}


void orienterContours(std::vector<Contour>& contours) {
  for (auto& c : contours)
    if (c.aire() < 0) {
      std::reverse(c.elements.begin(), c.elements.end());
      for (auto& e : c.elements) std::swap(e.debut, e.fin);
    }
  std::stable_sort(contours.begin(), contours.end(), [](const Contour& a, const Contour& b) { return std::abs(a.aire()) > std::abs(b.aire()); });
}

void recalerEncombrement(Carte& c) {
  if (c.contours.empty()) return;
  double x1 = std::numeric_limits<double>::max(), y1 = x1, x2 = std::numeric_limits<double>::lowest(), y2 = x2;
  for (const auto& q : c.contours.front().polygone(0.2)) {
    x1 = std::min(x1, q.x); x2 = std::max(x2, q.x);
    y1 = std::min(y1, q.y); y2 = std::max(y2, q.y);
  }
  c.x0 = (x1 + x2) / 2; c.y0 = (y1 + y2) / 2;
  c.L = x2 - x1; c.W = y2 - y1; c.r = 0;
}

double FormeCarte::sd(double x, double y) const {
  if (polygones.empty()) return std::numeric_limits<double>::max();
  double dmin = std::numeric_limits<double>::max();
  bool dedans = false;
  for (size_t k = 0; k < polygones.size(); ++k) {
    const auto& P = polygones[k];
    bool in = false;
    for (size_t i = 0, j = P.size() - 1; i < P.size(); j = i++) {
      if (((P[i].y > y) != (P[j].y > y)) && (x < (P[j].x - P[i].x) * (y - P[i].y) / (P[j].y - P[i].y) + P[i].x)) in = !in;
      dmin = std::min(dmin, distSegment(x, y, P[j], P[i]));
    }
    if (k == 0) dedans = in;
    else if (in) dedans = false;  // dans une découpe : hors de la carte
  }
  return dedans ? -dmin : dmin;
}

double FormeCarte::bordSelon(Bord b, double u, double defaut) const {
  if (polygones.empty()) return defaut;
  const auto& P = polygones.front();
  const bool hz = horizontal(b);
  bool trouve = false;
  double best = 0;
  for (size_t i = 0, j = P.size() - 1; i < P.size(); j = i++) {
    const double a1 = hz ? P[j].x : P[j].y, a2 = hz ? P[i].x : P[i].y;
    if ((a1 > u) == (a2 > u)) continue;
    const double t = (u - a1) / (a2 - a1);
    const double v = hz ? P[j].y + t * (P[i].y - P[j].y) : P[j].x + t * (P[i].x - P[j].x);
    const bool mieux = (b == Bord::N || b == Bord::E) ? v > best : v < best;
    if (!trouve || mieux) { best = v; trouve = true; }
  }
  return trouve ? best : defaut;
}

}  // namespace prom
