// Prométhée : grandeurs dérivées. Licence GPL-3.0-only.
#include "promethee/derive.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace prom {

namespace {
double borne(double v, double a, double b) { return std::min(b, std::max(a, v)); }

GeoComp geoComp(const Projet& p, const Derive& d, const Composant& k) {
  GeoComp g;
  g.id = k.id; g.ref = k.ref; g.type = k.type; g.h = k.h; g.top = d.zpt + k.h; g.bord = k.bord; g.w = k.w; g.d = k.d;
  g.dessous = k.dessous && !k.bord;
  if (g.dessous) { g.top = d.zpb; g.bas = d.zpb - k.h; }
  if (k.bord) {
    const double jeu = p.boitier.jeu, u = leLongComposant(p, k);
    if (*k.bord == Bord::E || *k.bord == Bord::W) {
      const double s = *k.bord == Bord::E ? 1 : -1;
      g.face = d.bordCarte(*k.bord, u) + s * (jeu - k.ecart);
      g.x = g.face - s * k.d / 2; g.y = u; g.hx = k.d / 2; g.hy = k.w / 2;
    } else {
      const double s = *k.bord == Bord::N ? 1 : -1;
      g.face = d.bordCarte(*k.bord, u) + s * (jeu - k.ecart);
      g.y = g.face - s * k.d / 2; g.x = u; g.hx = k.w / 2; g.hy = k.d / 2;
    }
    g.decoupe = DecoupeMur{*k.bord, u, d.zpt + k.zc, k.decoupe.w, k.decoupe.h, k.decoupe.r};
    switch (*k.bord) { case Bord::E: g.rot = 0; break; case Bord::N: g.rot = 90; break; case Bord::W: g.rot = 180; break; default: g.rot = 270; }
  } else {
    const bool echange = k.rot == 90 || k.rot == 270;
    const Point q = posComposant(p, k);
    g.x = q.x; g.y = q.y;
    g.hx = (echange ? k.d : k.w) / 2; g.hy = (echange ? k.w : k.d) / 2; g.rot = k.rot;
  }
  g.x1 = g.x - g.hx; g.x2 = g.x + g.hx; g.y1 = g.y - g.hy; g.y2 = g.y + g.hy;
  if (k.trou_couvercle) g.trouCouvercle = Percage{g.x, g.y, *k.trou_couvercle};
  return g;
}
}  // namespace

bool dansLevre(const Derive& d, const GeoComp& g) {
  double m = -std::numeric_limits<double>::infinity();
  const std::array<Point, 4> coins{{{g.x1, g.y1}, {g.x2, g.y1}, {g.x2, g.y2}, {g.x1, g.y2}}};
  for (const auto& q : coins) m = std::max(m, d.sdLevreInt(q.x, q.y));
  return m > -0.2;
}

double hauteurIdeale(const Projet& p, const Derive& d) {
  const auto& b = p.boitier;
  const double base = b.entretoise + p.carte.t;
  double H = base + 1;
  for (const auto& g : d.comps) {
    if (typeComposant(g.type)->bouton || g.dessous) continue;
    H = std::max(H, base + g.h + (dansLevre(d, g) ? Levre::h + 0.3 : 0.5));
    if (g.decoupe) H = std::max(H, g.decoupe->zc + g.decoupe->h / 2 - d.zf + Levre::h + 0.2);
  }
  for (const auto& g : d.comps) if (typeComposant(g.type)->bouton && !g.dessous) H = std::max(H, base + g.h - b.couvercle);
  return std::min(200.0, std::ceil(H * 2 - 1e-9) / 2);
}

Derive deriver(const Projet& p) {
  const auto& c = p.carte;
  const auto& b = p.boitier;
  const Vis& v = vis(b.vis);
  Derive d;
  d.cx = c.x0; d.cy = c.y0;
  d.Li = c.L + 2 * b.jeu; d.Wi = c.W + 2 * b.jeu;
  d.ri = std::max(c.r + b.jeu, 0.5);
  d.Lo = d.Li + 2 * b.paroi; d.Wo = d.Wi + 2 * b.paroi; d.ro = d.ri + b.paroi;
  d.zf = b.fond; d.zpb = d.zf + b.entretoise; d.zpt = d.zpb + c.t;
  d.vis = v; d.Rb = v.avant / 2 + PAROI_PILIER; d.rp = v.avant / 2; d.rTete = v.tete / 2 + 0.6;
  d.lo = {d.Li / 2 - Levre::jeu, d.Wi / 2 - Levre::jeu, std::max(d.ri - Levre::jeu, 0.3)};
  d.li = {d.lo.hx - Levre::ep, d.lo.hy - Levre::ep, std::max(d.lo.r - Levre::ep, 0.3)};
  d.sb = d.Li / 2 - d.ri; d.sa = d.Wi / 2 - d.ri;
  d.libre = c.libre();
  d.jeu = b.jeu;
  d.carteRR = {c.L / 2, c.W / 2, c.r};
  if (d.libre)
    for (const auto& k : c.contours) d.forme.polygones.push_back(k.polygone(0.2));
  for (const auto& t : p.trous) {
    const Point q = posTrou(p, t);
    TrouPlace tp;
    tp.id = t.id; tp.ref = t.ref; tp.x = q.x; tp.y = q.y;
    tp.nomVis = t.vis.value_or(b.vis);
    tp.vis = vis(tp.nomVis);
    tp.insert = t.fixation == "insert";
    if (t.diamTrou) tp.vis.trou = *t.diamTrou;
    if (t.diamPastille) tp.vis.tete = *t.diamPastille;
    tp.rp = t.diamLogement ? *t.diamLogement / 2 : tp.insert ? insert(tp.nomVis).trou / 2 : tp.vis.avant / 2;
    tp.Rb = t.diamPilier ? std::max(*t.diamPilier / 2, tp.rp + 0.4) : tp.rp + PAROI_PILIER;
    tp.rTete = tp.vis.tete / 2 + 0.6;
    tp.longInsert = tp.insert ? t.longueurInsert.value_or(insert(tp.nomVis).longueur) : 0;
    tp.longVis = longueurVis(c.t, tp.vis);
    d.trous.push_back(tp);
  }
  for (const auto& k : p.composants) d.comps.push_back(geoComp(p, d, k));
  if (d.libre)
    for (const auto& g : d.comps) {
      if (g.bord) continue;
      const double hors = std::max({d.forme.sd(g.x1, g.y1), d.forme.sd(g.x2, g.y1), d.forme.sd(g.x2, g.y2), d.forme.sd(g.x1, g.y2)});
      const double m = b.margeDebord;  // marge en plus du jeu : la carte touche avant ses composants
      if (hors > 0.01) d.debords.push_back({g.x1 - m, g.y1 - m, g.x2 + m, g.y2 + m});
    }
  d.longVis = longueurVis(c.t, v);
  d.H = b.hauteurAuto ? borne(hauteurIdeale(p, d), b.entretoise + c.t + 1, 200) : b.hauteur;
  d.zt = d.zf + d.H; d.ztop = d.zt + b.couvercle; d.zLevre = d.zt - Levre::h;
  return d;
}

std::map<Bord, std::vector<DecoupeValide>> decoupesValides(const Derive& d) {
  std::map<Bord, std::vector<DecoupeValide>> res{{Bord::N, {}}, {Bord::S, {}}, {Bord::E, {}}, {Bord::W, {}}};
  for (const auto& g : d.comps) {
    if (!g.decoupe) continue;
    const auto& o = *g.decoupe;
    if (!decoupeSurPartieDroite(d, o)) continue;
    if (o.zc - o.h / 2 < d.zf + 0.3 || o.zc + o.h / 2 > d.zt - 0.3) continue;
    auto& lst = res[o.mur];
    const bool conflit = std::any_of(lst.begin(), lst.end(), [&](const DecoupeValide& q) {
      return std::abs(q.s - o.u) < (q.w + o.w) / 2 + 0.3 && std::abs(q.t - o.zc) < (q.h + o.h) / 2 + 0.3;
    });
    if (conflit) continue;
    lst.push_back({o.u, o.zc, o.w, o.h, o.r, g.id});
  }
  return res;
}

std::vector<PilierValide> piliersValides(const Derive& d) {
  std::vector<PilierValide> res;
  for (const auto& t : d.trous) {
    if (d.sdCavite(t.x, t.y) > -(t.Rb + 0.25)) continue;
    const bool conflit = std::any_of(res.begin(), res.end(), [&](const PilierValide& q) { return std::hypot(q.x - t.x, q.y - t.y) < q.Rb + t.Rb + 0.25; });
    if (conflit) continue;
    res.push_back({t.x, t.y, t.id, t.Rb, t.rp});
  }
  return res;
}

std::vector<PercageValide> percagesValides(const Derive& d) {
  std::vector<PercageValide> res;
  for (const auto& g : d.comps) {
    if (!g.trouCouvercle) continue;
    const auto& o = *g.trouCouvercle;
    if (d.sdLevreInt(o.x, o.y) > -(o.d / 2 + 0.3)) continue;
    const bool conflit = std::any_of(res.begin(), res.end(), [&](const PercageValide& q) { return std::hypot(q.x - o.x, q.y - o.y) < (q.d + o.d) / 2 + 0.4; });
    if (conflit) continue;
    res.push_back({o.x, o.y, o.d, g.id});
  }
  return res;
}

}  // namespace prom

namespace prom {

double Derive::sdCarte(double x, double y) const {
  if (libre) return forme.sd(x, y);
  return sdRR(x, y, cx, cy, carteRR.hx, carteRR.hy, carteRR.r);
}
double Derive::sdEnveloppe(double x, double y) const {
  double s = libre ? forme.sd(x, y) : sdRR(x, y, cx, cy, carteRR.hx, carteRR.hy, carteRR.r);
  for (const auto& r : debords) s = std::min(s, sdRR(x, y, (r.x1 + r.x2) / 2, (r.y1 + r.y2) / 2, (r.x2 - r.x1) / 2, (r.y2 - r.y1) / 2, 0));
  return s;
}
double Derive::sdCavite(double x, double y) const {
  if (libre) return sdEnveloppe(x, y) - jeu;
  return sdRR(x, y, cx, cy, Li / 2, Wi / 2, ri);
}
double Derive::sdLevreInt(double x, double y) const {
  if (libre) return sdEnveloppe(x, y) - (jeu - Levre::jeu - Levre::ep);
  return sdRR(x, y, cx, cy, li.hx, li.hy, li.r);
}
double Derive::bordCarte(Bord b, double u) const {
  const double rect = b == Bord::N ? cy + carteRR.hy : b == Bord::S ? cy - carteRR.hy : b == Bord::E ? cx + carteRR.hx : cx - carteRR.hx;
  return libre ? forme.bordSelon(b, u, rect) : rect;
}

bool decoupeSurPartieDroite(const Derive& d, const DecoupeMur& o) {
  if (!d.libre) {
    const bool hz = horizontal(o.mur);
    const double centre = hz ? d.cx : d.cy, demi = hz ? d.sb : d.sa;
    return std::abs(o.u - centre) + o.w / 2 <= demi - 0.3;
  }
  // Contour libre : le bord doit rester droit sur toute la largeur de la découpe, plus une marge.
  const double ref = d.bordCarte(o.mur, o.u);
  for (int i = 0; i <= 8; ++i) {
    const double s = o.u - o.w / 2 - 0.5 + (o.w + 1.0) * i / 8;
    if (std::abs(d.bordCarte(o.mur, s) - ref) > 0.2) return false;
  }
  return true;
}

}  // namespace prom
