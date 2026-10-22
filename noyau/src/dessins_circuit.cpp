// Prométhée : dessins sur la carte, congés, chanfreins, contours décalés (lot L7). Licence GPL-3.0-only.
#include "promethee/dessins_circuit.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace prom::ecad {

namespace {
DessinCircuit* trait(Circuit& k, const std::string& id) {
  for (auto& d : k.dessins)
    if (d.uuid == id && d.forme == DessinCircuit::Forme::Trait && d.points.size() == 2) return &d;
  return nullptr;
}
}  // namespace

bool coinCommun(const DessinCircuit& a, const DessinCircuit& b, Pt& coin, Nm tolerance) {
  if (a.points.size() != 2 || b.points.size() != 2) return false;
  for (const Pt p : a.points)
    for (const Pt q : b.points)
      if (distance(p, q) <= static_cast<double>(tolerance)) {
        coin = p;
        return true;
      }
  return false;
}

namespace {
// Les deux traits, orientés depuis le coin ; faux sans coin commun ou s'ils sont alignés
bool depuisLeCoin(DessinCircuit& a, DessinCircuit& b, Pt& coin, int& ia, int& ib, std::string* raison) {
  if (!coinCommun(a, b, coin)) {
    if (raison) *raison = "les deux traits ne se touchent pas par un bout";
    return false;
  }
  ia = distance(a.points[0], coin) < distance(a.points[1], coin) ? 0 : 1;
  ib = distance(b.points[0], coin) < distance(b.points[1], coin) ? 0 : 1;
  return true;
}
}  // namespace

std::string poserConge(Circuit& k, const std::string& t1, const std::string& t2, Nm rayon, std::string* raison) {
  DessinCircuit* a = trait(k, t1);
  DessinCircuit* b = trait(k, t2);
  if (!a || !b || a == b) {
    if (raison) *raison = "il faut deux traits";
    return {};
  }
  Pt coin;
  int ia = 0, ib = 0;
  if (!depuisLeCoin(*a, *b, coin, ia, ib, raison)) return {};
  const Pt fa = a->points[static_cast<std::size_t>(1 - ia)], fb = b->points[static_cast<std::size_t>(1 - ib)];
  const double la = distance(coin, fa), lb = distance(coin, fb);
  const double ux = (fa.x - coin.x) / la, uy = (fa.y - coin.y) / la, vx = (fb.x - coin.x) / lb, vy = (fb.y - coin.y) / lb;
  const double cosT = std::clamp(ux * vx + uy * vy, -1.0, 1.0);
  const double theta = std::acos(cosT);
  if (theta < 1e-6 || std::abs(theta - std::numbers::pi) < 1e-6) {
    if (raison) *raison = "les deux traits sont alignés";
    return {};
  }
  const double r = static_cast<double>(rayon);
  const double dt = r / std::tan(theta / 2);
  if (dt >= la || dt >= lb) {
    if (raison) *raison = "le rayon est trop grand pour ces traits";
    return {};
  }
  const Pt ta{coin.x + std::llround(ux * dt), coin.y + std::llround(uy * dt)}, tb{coin.x + std::llround(vx * dt), coin.y + std::llround(vy * dt)};
  // Centre sur la bissectrice, à r / sin(θ/2) du coin
  double bx = ux + vx, by = uy + vy;
  const double nb = std::hypot(bx, by);
  bx /= nb;
  by /= nb;
  const double dc = r / std::sin(theta / 2);
  const double cx = static_cast<double>(coin.x) + bx * dc, cy = static_cast<double>(coin.y) + by * dc;
  const Pt milieu{std::llround(cx - bx * r), std::llround(cy - by * r)};
  a->points[static_cast<std::size_t>(ia)] = ta;
  b->points[static_cast<std::size_t>(ib)] = tb;
  DessinCircuit arc;
  arc.uuid = nouvelUuid();
  arc.forme = DessinCircuit::Forme::Arc;
  arc.couche = a->couche;
  arc.epaisseur = a->epaisseur;
  arc.points = {ta, milieu, tb};
  k.dessins.push_back(arc);
  return arc.uuid;
}

std::string poserChanfrein(Circuit& k, const std::string& t1, const std::string& t2, Nm d, std::string* raison) {
  DessinCircuit* a = trait(k, t1);
  DessinCircuit* b = trait(k, t2);
  if (!a || !b || a == b) {
    if (raison) *raison = "il faut deux traits";
    return {};
  }
  Pt coin;
  int ia = 0, ib = 0;
  if (!depuisLeCoin(*a, *b, coin, ia, ib, raison)) return {};
  const Pt fa = a->points[static_cast<std::size_t>(1 - ia)], fb = b->points[static_cast<std::size_t>(1 - ib)];
  const double la = distance(coin, fa), lb = distance(coin, fb);
  if (static_cast<double>(d) >= la || static_cast<double>(d) >= lb) {
    if (raison) *raison = "le chanfrein est plus long que les traits";
    return {};
  }
  const Pt ta{coin.x + std::llround((fa.x - coin.x) / la * static_cast<double>(d)), coin.y + std::llround((fa.y - coin.y) / la * static_cast<double>(d))};
  const Pt tb{coin.x + std::llround((fb.x - coin.x) / lb * static_cast<double>(d)), coin.y + std::llround((fb.y - coin.y) / lb * static_cast<double>(d))};
  a->points[static_cast<std::size_t>(ia)] = ta;
  b->points[static_cast<std::size_t>(ib)] = tb;
  DessinCircuit x;
  x.uuid = nouvelUuid();
  x.couche = a->couche;
  x.epaisseur = a->epaisseur;
  x.points = {ta, tb};
  k.dessins.push_back(x);
  return x.uuid;
}

std::vector<DessinCircuit> contourDecale(const Polygones& formes, Nm d, Couche couche) {
  std::vector<DessinCircuit> r;
  for (const Ilot& il : ilots(gonfler(unir(formes), d))) {
    DessinCircuit x;
    x.uuid = nouvelUuid();
    x.forme = DessinCircuit::Forme::Polygone;
    x.couche = couche;
    x.points = il.exterieur;
    x.epaisseur = 100'000;
    r.push_back(x);
  }
  return r;
}

Polygones pastillesDe(const ConceptionElec& c, const std::string& composant) {
  Polygones r;
  for (const auto& e : c.circuit.empreintes) {
    if (e.composant != composant) continue;
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!d) continue;
    for (const auto& p : d->pastilles) r.push_back(formePastille(pastilleSurCarte(e, p)));
  }
  return r;
}

std::vector<Pt> pointsArc(Pt a, Pt m, Pt b, int segments) {
  // Cercle par trois points
  const double ax = static_cast<double>(a.x), ay = static_cast<double>(a.y), mx = static_cast<double>(m.x), my = static_cast<double>(m.y), bx = static_cast<double>(b.x), by = static_cast<double>(b.y);
  const double d = 2 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
  if (std::abs(d) < 1e-9) return {a, b};
  const double ux = ((ax * ax + ay * ay) * (my - by) + (mx * mx + my * my) * (by - ay) + (bx * bx + by * by) * (ay - my)) / d;
  const double uy = ((ax * ax + ay * ay) * (bx - mx) + (mx * mx + my * my) * (ax - bx) + (bx * bx + by * by) * (mx - ax)) / d;
  const double r = std::hypot(ax - ux, ay - uy);
  double t0 = std::atan2(ay - uy, ax - ux), tm = std::atan2(my - uy, mx - ux), t1 = std::atan2(by - uy, bx - ux);
  // Sens qui passe par le milieu
  auto norm = [](double t) {
    while (t < 0) t += 2 * std::numbers::pi;
    while (t >= 2 * std::numbers::pi) t -= 2 * std::numbers::pi;
    return t;
  };
  double balayage = norm(t1 - t0);
  if (norm(tm - t0) > balayage) balayage -= 2 * std::numbers::pi;
  std::vector<Pt> r2;
  for (int i = 0; i <= segments; ++i) {
    const double t = t0 + balayage * i / segments;
    r2.push_back({std::llround(ux + r * std::cos(t)), std::llround(uy + r * std::sin(t))});
  }
  r2.front() = a;
  r2.back() = b;
  return r2;
}

}  // namespace prom::ecad
