// Prométhée : lecture générale d'un dessin DXF et insertion dans une esquisse (M1). Licence GPL-3.0-only.
#include "promethee/dxf_dessin.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <sstream>

namespace prom::dxf {
namespace {

constexpr double PI = std::numbers::pi;
constexpr double TOL_APPROCHE = 0.005;  // mm : écart maximal entre une courbe et les arcs qui la remplacent
constexpr double FLECHE_ECHANTILLON = 0.0005;

using Groupes = std::vector<std::pair<int, std::string>>;
struct Ent {
  std::string type;
  Groupes g;
};

double nombre(const std::string& s) {
  try {
    return std::stod(s);
  } catch (...) {
    return 0;
  }
}
double val(const Groupes& g, int code, double defaut = 0) {
  for (const auto& [c, v] : g)
    if (c == code) return nombre(v);
  return defaut;
}
std::string txt(const Groupes& g, int code, const std::string& defaut = {}) {
  for (const auto& [c, v] : g)
    if (c == code) return v;
  return defaut;
}
double dist(Pt2 a, Pt2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Transformation affine du plan : x' = a x + b y + e ; y' = c x + d y + f
struct Affine {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  Pt2 operator()(Pt2 p) const { return {a * p.x + b * p.y + e, c * p.x + d * p.y + f}; }
  Affine operator*(const Affine& o) const {  // (this ∘ o)(p) = this(o(p))
    return {a * o.a + b * o.c, a * o.b + b * o.d, c * o.a + d * o.c, c * o.b + d * o.d, a * o.e + b * o.f + e, c * o.e + d * o.f + f};
  }
  double det() const { return a * d - b * c; }
  // Similitude (rotation, échelle uniforme, miroir éventuel) : les cercles restent des cercles
  bool similitude() const {
    const double n1 = a * a + c * c, n2 = b * b + d * d, pr = a * b + c * d;
    return std::abs(n1 - n2) <= 1e-9 * std::max(n1, n2) && std::abs(pr) <= 1e-9 * std::max(n1, n2);
  }
  static Affine translation(double x, double y) { return {1, 0, 0, 1, x, y}; }
  static Affine rotation(double degres) {
    const double r = degres * PI / 180, co = std::cos(r), si = std::sin(r);
    return {co, -si, si, co, 0, 0};
  }
  static Affine echelle(double sx, double sy) { return {sx, 0, 0, sy, 0, 0}; }
};
// Repère d'objet (OCS) d'une entité plane dont l'extrusion est (0, 0, -1) : X retourné
Affine repereObjet(const Groupes& g) { return val(g, 230, 1) < 0 ? Affine::echelle(-1, 1) : Affine{}; }

// Cercle par trois points ; faux s'ils sont alignés
bool cercle3(Pt2 p, Pt2 q, Pt2 r, Pt2& centre, double& rayon) {
  const double d = 2 * (p.x * (q.y - r.y) + q.x * (r.y - p.y) + r.x * (p.y - q.y));
  const double l = std::max({dist(p, q), dist(q, r), dist(p, r)});
  if (std::abs(d) < 1e-12 * l * l) return false;
  const double p2 = p.x * p.x + p.y * p.y, q2 = q.x * q.x + q.y * q.y, r2 = r.x * r.x + r.y * r.y;
  centre = {(p2 * (q.y - r.y) + q2 * (r.y - p.y) + r2 * (p.y - q.y)) / d, (p2 * (r.x - q.x) + q2 * (p.x - r.x) + r2 * (q.x - p.x)) / d};
  rayon = dist(centre, p);
  return rayon < 1e5;
}
double distSegment(Pt2 p, Pt2 a, Pt2 b) {
  const double lx = b.x - a.x, ly = b.y - a.y, l2 = lx * lx + ly * ly;
  if (l2 < 1e-24) return dist(p, a);
  const double t = std::clamp(((p.x - a.x) * lx + (p.y - a.y) * ly) / l2, 0.0, 1.0);
  return dist(p, {a.x + t * lx, a.y + t * ly});
}

class Lecteur {
 public:
  DessinDxf r;

  void lire(const std::string& texte) {
    Groupes paires;
    {
      std::istringstream in(texte);
      std::string code, valeur;
      auto nettoyer = [](std::string& s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        std::size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        s.erase(0, i);
      };
      while (std::getline(in, code) && std::getline(in, valeur)) {
        nettoyer(code);
        nettoyer(valeur);
        try {
          paires.push_back({std::stoi(code), valeur});
        } catch (...) {
          r.alertes.push_back("Fichier DXF mal formé : code de groupe « " + code + " » illisible (un DXF binaire n’est pas lu : réenregistre-le en ASCII).");
          return;
        }
      }
    }
    if (paires.empty()) {
      r.alertes.push_back("Fichier vide ou qui n’est pas un DXF.");
      return;
    }
    // Découpage en sections, puis en entités
    std::map<std::string, std::vector<Ent>> sections;
    std::string section;
    for (std::size_t i = 0; i < paires.size(); ++i) {
      if (paires[i].first != 0) continue;
      const std::string& t = paires[i].second;
      if (t == "SECTION") {
        section = i + 1 < paires.size() && paires[i + 1].first == 2 ? paires[i + 1].second : std::string();
        // Les variables de l'en-tête suivent « SECTION » sans nouveau code 0 : elles restent dans cette entité
        Ent e{t, {}};
        for (std::size_t k = i + 1; k < paires.size() && paires[k].first != 0; ++k) e.g.push_back(paires[k]);
        if (!section.empty()) sections[section].push_back(std::move(e));
        continue;
      }
      if (t == "ENDSEC") { section.clear(); continue; }
      if (t == "EOF") break;
      Ent e{t, {}};
      for (std::size_t k = i + 1; k < paires.size() && paires[k].first != 0; ++k) e.g.push_back(paires[k]);
      if (!section.empty()) sections[section].push_back(std::move(e));
    }
    if (!sections.count("ENTITIES") && !sections.count("BLOCKS")) {
      r.alertes.push_back("Aucune section d’entités : ce n’est pas un dessin DXF.");
      return;
    }
    lireEntete(sections["HEADER"]);
    lireCalques(sections["TABLES"]);
    lireBlocs(sections["BLOCKS"]);
    // Facteur d'unité appliqué à tout le dessin
    traiter(sections["ENTITIES"], Affine::echelle(r.facteurMm, r.facteurMm), 0, {}, true);
    for (auto& k : r.calques)
      k.traits = static_cast<int>(std::count_if(r.traits.begin(), r.traits.end(), [&](const TraitDxf& t) { return t.calque == k.nom; }));
    if (papier) r.alertes.push_back(std::to_string(papier) + " entité(s) de l’espace papier laissée(s) (cartouche, mise en page).");
    if (r.traits.empty()) r.alertes.push_back("Le dessin ne contient ni ligne, ni arc, ni cercle à reprendre.");
  }

 private:
  struct Bloc {
    Pt2 base;
    std::vector<Ent> entites;
  };
  std::map<std::string, Bloc> blocs;
  int papier = 0;

  void lireEntete(const std::vector<Ent>& h) {
    // L'en-tête est une seule « entité » sans code 0 entre les variables : ses groupes se lisent à la suite
    for (const auto& e : h) {
      const Groupes& g = e.g;
      for (std::size_t i = 0; i + 1 < g.size(); ++i)
        if (g[i].first == 9 && g[i].second == "$INSUNITS" && g[i + 1].first == 70) uniteDepuis(static_cast<int>(nombre(g[i + 1].second)));
    }
  }
  void uniteDepuis(int code) {
    struct U {
      int code;
      double mm;
      const char* nom;
    };
    static const U unites[] = {{1, 25.4, "pouce"}, {2, 304.8, "pied"}, {3, 1609344, "mile"}, {4, 1, "mm"}, {5, 10, "cm"}, {6, 1000, "m"},
                               {7, 1e6, "km"}, {8, 25.4e-6, "micro-pouce"}, {9, 0.0254, "mil"}, {10, 914.4, "yard"}, {11, 1e-7, "ångström"},
                               {12, 1e-6, "nm"}, {13, 1e-3, "µm"}, {14, 100, "dm"}};
    for (const auto& u : unites)
      if (u.code == code) {
        r.facteurMm = u.mm;
        r.unite = u.nom;
        r.uniteDeclaree = true;
      }
  }
  void lireCalques(const std::vector<Ent>& t) {
    for (const auto& e : t) {
      if (e.type != "LAYER") continue;
      CalqueDxf k;
      k.nom = txt(e.g, 2, "0");
      const int c = static_cast<int>(val(e.g, 62, 7));
      k.couleur = std::abs(c);
      k.visible = c >= 0 && !(static_cast<int>(val(e.g, 70)) & 1);
      if (!r.calque(k.nom)) r.calques.push_back(k);
    }
  }
  void lireBlocs(const std::vector<Ent>& b) {
    Bloc* courant = nullptr;
    for (const auto& e : b) {
      if (e.type == "BLOCK") {
        const std::string nom = txt(e.g, 2);
        courant = &blocs[nom];
        courant->base = {val(e.g, 10), val(e.g, 20)};
      } else if (e.type == "ENDBLK") courant = nullptr;
      else if (courant) courant->entites.push_back(e);
    }
  }
  std::string calqueDe(const Groupes& g, const std::string& parent) {
    std::string c = txt(g, 8, "0");
    if (c == "0" && !parent.empty()) c = parent;  // dans un bloc, le calque 0 prend celui de l'insertion
    if (!r.calque(c)) r.calques.push_back({c, 7, true, 0});
    return c;
  }

  // ----- Émission des traits -----
  void ligne(const Affine& M, Pt2 p, Pt2 q, const std::string& calque) {
    const Pt2 a = M(p), b = M(q);
    if (dist(a, b) < 1e-9) return;
    TraitDxf t;
    t.genre = TraitDxf::Genre::Ligne;
    t.calque = calque;
    t.a = a;
    t.b = b;
    r.traits.push_back(t);
  }
  // Arc de centre c, rayon ρ, de l'angle a0 à a1 (degrés, sens direct) ; plein : cercle
  void arc(const Affine& M, Pt2 c, double rho, double a0, double a1, bool plein, const std::string& calque) {
    if (rho <= 1e-9) return;
    if (!M.similitude()) {
      // Échelle non uniforme : le cercle devient une ellipse, suivie par des arcs
      if (plein) a1 = a0 + 360;
      while (a1 <= a0) a1 += 360;
      courbe(M, [&](double t) { return Pt2{c.x + rho * std::cos(t * PI / 180), c.y + rho * std::sin(t * PI / 180)}; }, a0, a1, plein, calque);
      return;
    }
    const double s = std::sqrt(std::abs(M.det()));
    TraitDxf t;
    t.calque = calque;
    t.centre = M(c);
    t.rayon = rho * s;
    if (plein) {
      t.genre = TraitDxf::Genre::Cercle;
      r.traits.push_back(t);
      return;
    }
    const Pt2 p0 = M({c.x + rho * std::cos(a0 * PI / 180), c.y + rho * std::sin(a0 * PI / 180)});
    const Pt2 p1 = M({c.x + rho * std::cos(a1 * PI / 180), c.y + rho * std::sin(a1 * PI / 180)});
    t.genre = TraitDxf::Genre::Arc;
    // Un miroir inverse le sens de parcours
    t.a = M.det() > 0 ? p0 : p1;
    t.b = M.det() > 0 ? p1 : p0;
    if (dist(t.a, t.b) < 1e-9) {
      t.genre = TraitDxf::Genre::Cercle;  // arc de 360°
    }
    r.traits.push_back(t);
  }
  // Courbe paramétrée f sur [t0, t1] : échantillonnée finement, puis remplacée par des arcs et des lignes
  void courbe(const Affine& M, const std::function<Pt2(double)>& f, double t0, double t1, bool fermee, const std::string& calque) {
    std::vector<Pt2> pts{M(f(t0))};
    std::function<void(double, Pt2, double, Pt2, int)> subdiviser = [&](double ta, Pt2 a, double tb, Pt2 b, int prof) {
      const double tm = (ta + tb) / 2;
      const Pt2 m = M(f(tm));
      const Pt2 q1 = M(f(ta + (tb - ta) / 4)), q3 = M(f(ta + 3 * (tb - ta) / 4));
      const double ecart = std::max({distSegment(m, a, b), distSegment(q1, a, b), distSegment(q3, a, b)});
      if (prof < 18 && (prof < 4 || ecart > FLECHE_ECHANTILLON)) {
        subdiviser(ta, a, tm, m, prof + 1);
        subdiviser(tm, m, tb, b, prof + 1);
      } else
        pts.push_back(b);
    };
    subdiviser(t0, pts.front(), t1, M(f(t1)), 0);
    if (fermee) pts.back() = pts.front();
    ++r.courbesApprochees;
    approcher(pts, calque);
  }
  // Suite de points → arcs (par trois points) et lignes, chacun fidèle aux points qu'il couvre à TOL_APPROCHE près
  void approcher(const std::vector<Pt2>& pts, const std::string& calque) {
    const std::size_t n = pts.size();
    auto convient = [&](std::size_t i, std::size_t j, Pt2& centre, double& rayon, bool& droite) {
      const std::size_t m = (i + j) / 2;
      droite = true;
      bool ok = true;
      for (std::size_t k = i + 1; k < j && ok; ++k) ok = distSegment(pts[k], pts[i], pts[j]) <= TOL_APPROCHE;
      if (ok) return true;
      droite = false;
      if (!cercle3(pts[i], pts[m], pts[j], centre, rayon)) return false;
      // Les points doivent être sur le cercle et dans l'ordre le long de l'arc
      const double sens = (pts[m].x - pts[i].x) * (pts[j].y - pts[m].y) - (pts[m].y - pts[i].y) * (pts[j].x - pts[m].x) > 0 ? 1 : -1;
      double prec = std::atan2(pts[i].y - centre.y, pts[i].x - centre.x), parcours = 0;
      for (std::size_t k = i + 1; k <= j; ++k) {
        if (std::abs(dist(pts[k], centre) - rayon) > TOL_APPROCHE) return false;
        const double a = std::atan2(pts[k].y - centre.y, pts[k].x - centre.x);
        double da = a - prec;
        while (da > PI) da -= 2 * PI;
        while (da < -PI) da += 2 * PI;
        if (da * sens < -1e-9) return false;
        parcours += std::abs(da);
        prec = a;
      }
      // Les cordes ne doivent pas s'écarter du cercle entre deux points
      for (std::size_t k = i; k < j; ++k) {
        const Pt2 mil{(pts[k].x + pts[k + 1].x) / 2, (pts[k].y + pts[k + 1].y) / 2};
        if (rayon - dist(mil, centre) > TOL_APPROCHE) return false;
      }
      return parcours < 2 * PI - 1e-6;
    };
    std::size_t i = 0;
    while (i + 1 < n) {
      // Plus long morceau acceptable : avancée par doublement puis dichotomie
      std::size_t bon = i + 1, pas = 1;
      Pt2 c{};
      double rho = 0;
      bool droite = true;
      while (bon + pas < n) {
        Pt2 c2{};
        double r2 = 0;
        bool d2 = true;
        if (!convient(i, bon + pas, c2, r2, d2)) break;
        bon += pas;
        pas *= 2;
      }
      for (std::size_t haut = std::min(n - 1, bon + pas), bas = bon; haut > bas + 1;) {
        const std::size_t mil = (bas + haut) / 2;
        Pt2 c2{};
        double r2 = 0;
        bool d2 = true;
        if (convient(i, mil, c2, r2, d2)) bon = bas = mil;
        else haut = mil;
      }
      convient(i, bon, c, rho, droite);
      TraitDxf t;
      t.calque = calque;
      t.approche = true;
      if (droite || bon == i + 1) {
        if (dist(pts[i], pts[bon]) > 1e-9) {
          t.genre = TraitDxf::Genre::Ligne;
          t.a = pts[i];
          t.b = pts[bon];
          r.traits.push_back(t);
        }
      } else {
        const std::size_t m = (i + bon) / 2;
        const bool direct = (pts[m].x - pts[i].x) * (pts[bon].y - pts[m].y) - (pts[m].y - pts[i].y) * (pts[bon].x - pts[m].x) > 0;
        t.genre = TraitDxf::Genre::Arc;
        t.centre = c;
        t.rayon = rho;
        t.a = direct ? pts[i] : pts[bon];
        t.b = direct ? pts[bon] : pts[i];
        r.traits.push_back(t);
      }
      i = bon;
    }
  }
  // Segment de polyligne avec renflement (tangente du quart de l'angle au centre ; positif : sens direct)
  void segment(const Affine& M, Pt2 p, Pt2 q, double renflement, const std::string& calque) {
    if (std::abs(renflement) < 1e-12 || dist(p, q) < 1e-12) {
      ligne(M, p, q, calque);
      return;
    }
    const double theta = 4 * std::atan(renflement), corde = dist(p, q);
    const Pt2 u{(q.x - p.x) / corde, (q.y - p.y) / corde}, nrm{-u.y, u.x};
    const double h = corde / 2 / std::tan(theta / 2);
    const Pt2 c{(p.x + q.x) / 2 + nrm.x * h, (p.y + q.y) / 2 + nrm.y * h};
    const double rho = dist(c, p);
    const Pt2 debut = theta > 0 ? p : q;
    const double a0 = std::atan2(debut.y - c.y, debut.x - c.x) * 180 / PI;
    arc(M, c, rho, a0, a0 + std::abs(theta) * 180 / PI, false, calque);
  }
  void polyligne(const Affine& M, const std::vector<std::pair<Pt2, double>>& sommets, bool fermee, const std::string& calque) {
    for (std::size_t k = 0; k + 1 < sommets.size(); ++k) segment(M, sommets[k].first, sommets[k + 1].first, sommets[k].second, calque);
    if (fermee && sommets.size() > 2) segment(M, sommets.back().first, sommets.front().first, sommets.back().second, calque);
  }

  // NURBS par l'algorithme de De Boor (coordonnées homogènes)
  static Pt2 deBoor(int p, const std::vector<double>& U, const std::vector<Pt2>& P, const std::vector<double>& W, double t) {
    const int n = static_cast<int>(P.size()) - 1;
    int k = p;
    while (k < n && t >= U[static_cast<std::size_t>(k + 1)]) ++k;
    std::vector<std::array<double, 3>> d(static_cast<std::size_t>(p + 1));
    for (int j = 0; j <= p; ++j) {
      const std::size_t idx = static_cast<std::size_t>(j + k - p);
      const double w = W.empty() ? 1 : W[idx];
      d[static_cast<std::size_t>(j)] = {P[idx].x * w, P[idx].y * w, w};
    }
    for (int rr = 1; rr <= p; ++rr)
      for (int j = p; j >= rr; --j) {
        const double den = U[static_cast<std::size_t>(j + 1 + k - rr)] - U[static_cast<std::size_t>(j + k - p)];
        const double a = den == 0 ? 0 : (t - U[static_cast<std::size_t>(j + k - p)]) / den;
        for (int c = 0; c < 3; ++c) d[static_cast<std::size_t>(j)][static_cast<std::size_t>(c)] = (1 - a) * d[static_cast<std::size_t>(j - 1)][static_cast<std::size_t>(c)] + a * d[static_cast<std::size_t>(j)][static_cast<std::size_t>(c)];
      }
    const auto& q = d[static_cast<std::size_t>(p)];
    return q[2] == 0 ? Pt2{q[0], q[1]} : Pt2{q[0] / q[2], q[1] / q[2]};
  }
  void spline(const Affine& M, const Groupes& g, const std::string& calque) {
    const int p = static_cast<int>(val(g, 71, 3));
    std::vector<double> U, W;
    std::vector<Pt2> P, F;
    for (std::size_t i = 0; i < g.size(); ++i) {
      const auto& [c, v] = g[i];
      if (c == 40) U.push_back(nombre(v));
      else if (c == 41) W.push_back(nombre(v));
      else if (c == 10) P.push_back({nombre(v), i + 1 < g.size() && g[i + 1].first == 20 ? nombre(g[i + 1].second) : 0});
      else if (c == 11) F.push_back({nombre(v), i + 1 < g.size() && g[i + 1].first == 21 ? nombre(g[i + 1].second) : 0});
    }
    const bool fermee = static_cast<int>(val(g, 70)) & 1;
    if (W.size() != P.size()) W.clear();
    if (p >= 1 && P.size() > static_cast<std::size_t>(p) && U.size() == P.size() + static_cast<std::size_t>(p) + 1) {
      const double t0 = U[static_cast<std::size_t>(p)], t1 = U[P.size()];
      if (t1 > t0) {
        courbe(M, [&](double t) { return deBoor(p, U, P, W, std::min(t, t1 - 1e-12 * (t1 - t0))); }, t0, t1, false, calque);
        if (fermee) {
          const Pt2 a = M(deBoor(p, U, P, W, t0)), b = M(deBoor(p, U, P, W, t1 - 1e-12 * (t1 - t0)));
          if (dist(a, b) > 1e-6) ligne(Affine{}, a, b, calque);
        }
        return;
      }
    }
    // Sans vecteur nodal exploitable : la courbe passe par ses points de passage (ou suit ses points de contrôle)
    const std::vector<Pt2>& Q = F.size() >= 2 ? F : P;
    if (Q.size() < 2) return;
    std::vector<Pt2> pts;
    for (const Pt2& q : Q) pts.push_back(M(q));
    if (fermee) pts.push_back(pts.front());
    ++r.courbesApprochees;
    r.alertes.push_back("Spline du calque « " + calque + " » sans vecteur nodal lisible : suivie par ses " + (F.size() >= 2 ? "points de passage." : "points de contrôle."));
    approcher(pts, calque);
  }
  void ellipse(const Groupes& g, const Affine& M, const std::string& calque) {
    const Pt2 c{val(g, 10), val(g, 20)}, a{val(g, 11), val(g, 21)};
    const double k = val(g, 40, 1);
    double u0 = val(g, 41, 0), u1 = val(g, 42, 2 * PI);
    while (u1 <= u0) u1 += 2 * PI;
    const bool plein = std::abs(u1 - u0 - 2 * PI) < 1e-9;
    // Petit axe : N × grand axe (N : extrusion ; (0, 0, -1) retourne le sens de parcours)
    const Pt2 b = val(g, 230, 1) < 0 ? Pt2{a.y * k, -a.x * k} : Pt2{-a.y * k, a.x * k};
    const double ra = std::hypot(a.x, a.y);
    if (std::abs(k - 1) < 1e-9 && ra > 0) {
      // Ellipse circulaire : un arc exact
      const double phi = std::atan2(a.y, a.x) * 180 / PI;
      const bool directe = val(g, 230, 1) >= 0;
      const double d0 = directe ? phi + u0 * 180 / PI : phi - u1 * 180 / PI, d1 = directe ? phi + u1 * 180 / PI : phi - u0 * 180 / PI;
      arc(M, c, ra, d0, d1, plein, calque);
      return;
    }
    courbe(M, [&](double u) { return Pt2{c.x + a.x * std::cos(u) + b.x * std::sin(u), c.y + a.y * std::cos(u) + b.y * std::sin(u)}; }, u0, u1, plein, calque);
  }

  void traiter(const std::vector<Ent>& ents, const Affine& M, int profondeur, const std::string& calqueParent, bool racine) {
    for (std::size_t i = 0; i < ents.size(); ++i) {
      const Ent& e = ents[i];
      const Groupes& g = e.g;
      if (racine && val(g, 67) == 1) {
        ++papier;
        if (e.type == "POLYLINE")
          while (i + 1 < ents.size() && ents[i + 1].type != "SEQEND") ++i;
        continue;
      }
      if (e.type == "SECTION" || e.type == "VERTEX" || e.type == "SEQEND" || e.type == "ATTRIB" || e.type == "ATTDEF") continue;
      const std::string calque = calqueDe(g, calqueParent);
      const Affine O = M * repereObjet(g);
      if (e.type == "LINE") ligne(M, {val(g, 10), val(g, 20)}, {val(g, 11), val(g, 21)}, calque);
      else if (e.type == "POINT") {
        TraitDxf t;
        t.genre = TraitDxf::Genre::Point;
        t.calque = calque;
        t.a = M({val(g, 10), val(g, 20)});
        r.traits.push_back(t);
      } else if (e.type == "CIRCLE") arc(O, {val(g, 10), val(g, 20)}, val(g, 40), 0, 360, true, calque);
      else if (e.type == "ARC") arc(O, {val(g, 10), val(g, 20)}, val(g, 40), val(g, 50), val(g, 51), false, calque);
      else if (e.type == "LWPOLYLINE") {
        std::vector<std::pair<Pt2, double>> s;
        for (std::size_t k = 0; k < g.size(); ++k) {
          if (g[k].first == 10) s.push_back({{nombre(g[k].second), k + 1 < g.size() && g[k + 1].first == 20 ? nombre(g[k + 1].second) : 0}, 0});
          else if (g[k].first == 42 && !s.empty()) s.back().second = nombre(g[k].second);
        }
        polyligne(O, s, static_cast<int>(val(g, 70)) & 1, calque);
      } else if (e.type == "POLYLINE") {
        const int drapeaux = static_cast<int>(val(g, 70));
        std::vector<std::pair<Pt2, double>> s;
        while (i + 1 < ents.size() && ents[i + 1].type == "VERTEX") {
          ++i;
          const int dv = static_cast<int>(val(ents[i].g, 70));
          if (dv & 16) continue;  // sommet de contrôle d'une polyligne lissée : seuls les sommets lissés sont tracés
          s.push_back({{val(ents[i].g, 10), val(ents[i].g, 20)}, val(ents[i].g, 42)});
        }
        if (drapeaux & (16 | 64)) {
          ++r.ignorees[drapeaux & 16 ? "POLYLINE (maillage 3D)" : "POLYLINE (polyface)"];
          continue;
        }
        polyligne(drapeaux & 8 ? M : O, s, drapeaux & 1, calque);
      } else if (e.type == "ELLIPSE") ellipse(g, M, calque);
      else if (e.type == "SPLINE") spline(M, g, calque);
      else if (e.type == "INSERT") {
        const std::string nom = txt(g, 2);
        const auto it = blocs.find(nom);
        if (it == blocs.end()) {
          r.alertes.push_back("Bloc « " + nom + " » inséré mais absent du fichier.");
          continue;
        }
        if (profondeur >= 12) {
          r.alertes.push_back("Blocs imbriqués trop profondément (« " + nom + " ») : arrêt à 12 niveaux.");
          continue;
        }
        const double sx = val(g, 41, 1), sy = val(g, 42, 1), rot = val(g, 50);
        const int colonnes = std::max(1, static_cast<int>(val(g, 70, 1))), rangees = std::max(1, static_cast<int>(val(g, 71, 1)));
        const double dc = val(g, 44), dr = val(g, 45);
        for (int c = 0; c < colonnes; ++c)
          for (int rg = 0; rg < rangees; ++rg) {
            const Affine T = O * Affine::translation(val(g, 10), val(g, 20)) * Affine::rotation(rot) * Affine::translation(c * dc, rg * dr) * Affine::echelle(sx, sy) *
                             Affine::translation(-it->second.base.x, -it->second.base.y);
            traiter(it->second.entites, T, profondeur + 1, calque, false);
          }
      } else
        ++r.ignorees[e.type];
    }
  }
};

// Encombrement d'un trait (arcs : extrémités et quadrants traversés)
void etendre(const TraitDxf& t, double& x0, double& y0, double& x1, double& y1) {
  auto pt = [&](Pt2 p) {
    x0 = std::min(x0, p.x);
    y0 = std::min(y0, p.y);
    x1 = std::max(x1, p.x);
    y1 = std::max(y1, p.y);
  };
  switch (t.genre) {
    case TraitDxf::Genre::Ligne: pt(t.a); pt(t.b); break;
    case TraitDxf::Genre::Point: pt(t.a); break;
    case TraitDxf::Genre::Cercle:
      pt({t.centre.x - t.rayon, t.centre.y - t.rayon});
      pt({t.centre.x + t.rayon, t.centre.y + t.rayon});
      break;
    case TraitDxf::Genre::Arc: {
      pt(t.a);
      pt(t.b);
      const double a0 = std::atan2(t.a.y - t.centre.y, t.a.x - t.centre.x);
      double a1 = std::atan2(t.b.y - t.centre.y, t.b.x - t.centre.x);
      while (a1 <= a0) a1 += 2 * PI;
      for (int q = -4; q <= 8; ++q) {
        const double a = q * PI / 2;
        if (a > a0 && a < a1) pt({t.centre.x + t.rayon * std::cos(a), t.centre.y + t.rayon * std::sin(a)});
      }
      break;
    }
  }
}

}  // namespace

const CalqueDxf* DessinDxf::calque(const std::string& nom) const {
  for (const auto& k : calques)
    if (k.nom == nom) return &k;
  return nullptr;
}

DessinDxf lireDessinDxf(const std::string& texte) {
  Lecteur l;
  l.lire(texte);
  return std::move(l.r);
}

std::string couleurAci(int indice) {
  static const char* base[] = {"#000000", "#FF0000", "#FFFF00", "#00FF00", "#00FFFF", "#0000FF", "#FF00FF", "#000000", "#808080", "#C0C0C0"};
  indice = std::abs(indice);
  if (indice >= 0 && indice <= 9) return base[indice];
  char t[16];
  if (indice >= 250 && indice <= 255) {
    const int v = 51 + (indice - 250) * 41;
    std::snprintf(t, sizeof t, "#%02X%02X%02X", v, v, v);
    return t;
  }
  if (indice > 255) return "#000000";
  // Roue : 24 teintes (pas de 15°), 5 niveaux de luminosité, chacun plein puis atténué
  const int k = indice - 10, teinte = k / 10, niveau = (k % 10) / 2;
  const bool attenue = k % 2;
  const double h = teinte * 15.0, v = 1.0 - niveau * 0.2, s = attenue ? 0.5 : 1.0;
  const double c = v * s, x = c * (1 - std::abs(std::fmod(h / 60, 2) - 1)), m = v - c;
  double rr = 0, gg = 0, bb = 0;
  if (h < 60) { rr = c; gg = x; }
  else if (h < 120) { rr = x; gg = c; }
  else if (h < 180) { gg = c; bb = x; }
  else if (h < 240) { gg = x; bb = c; }
  else if (h < 300) { rr = x; bb = c; }
  else { rr = c; bb = x; }
  std::snprintf(t, sizeof t, "#%02X%02X%02X", static_cast<int>(std::lround((rr + m) * 255)), static_cast<int>(std::lround((gg + m) * 255)), static_cast<int>(std::lround((bb + m) * 255)));
  return t;
}

RapportImportDxf importerDxf(Esquisse& e, const DessinDxf& d, const OptionsImportDxf& o) {
  RapportImportDxf r;
  // Calques retenus
  auto retenu = [&](const std::string& calque) {
    if (o.calquesChoisis || !o.calques.empty()) return o.calques.count(calque) > 0;
    const CalqueDxf* k = d.calque(calque);
    return !k || k->visible;
  };
  const double f = (o.facteurMm > 0 ? o.facteurMm / d.facteurMm : 1.0) * (o.echelle > 0 ? o.echelle : 1.0);
  std::vector<TraitDxf> traits;
  int horsCalque = 0;
  for (const auto& t : d.traits) {
    if (!retenu(t.calque)) { ++horsCalque; continue; }
    TraitDxf u = t;
    for (Pt2* p : {&u.a, &u.b, &u.centre}) { p->x *= f; p->y *= f; }
    u.rayon *= f;
    traits.push_back(u);
  }
  if (horsCalque) r.alertes.push_back(std::to_string(horsCalque) + " trait(s) sur des calques non retenus (éteints, gelés ou décochés).");
  if (traits.empty()) {
    r.alertes.push_back("Rien à insérer.");
    return r;
  }
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  for (const auto& t : traits) etendre(t, x0, y0, x1, y1);
  double ox = o.dx, oy = o.dy;
  if (o.placement == PlacementDxf::Centre) { ox -= (x0 + x1) / 2; oy -= (y0 + y1) / 2; }
  else if (o.placement == PlacementDxf::CoinBasGauche) { ox -= x0; oy -= y0; }
  r.xmin = x0 + ox;
  r.xmax = x1 + ox;
  r.ymin = y0 + oy;
  r.ymax = y1 + oy;
  if (std::max(x1 - x0, y1 - y0) > 1e5) r.alertes.push_back("Dessin de plus de 100 m : vérifie l’unité du fichier.");
  if (traits.size() > 3000) r.alertes.push_back(std::to_string(traits.size()) + " traits : l’esquisse sera lente à modifier ; ne garde que les calques utiles.");
  // Calques de l'esquisse
  if (o.calquesEsquisse)
    for (const auto& t : traits)
      if (!t.calque.empty() && std::none_of(e.calques.begin(), e.calques.end(), [&](const Calque& k) { return k.nom == t.calque; })) {
        Calque k;
        k.nom = t.calque;
        const CalqueDxf* kd = d.calque(t.calque);
        const int aci = kd ? kd->couleur : 7;
        if (aci != 7 && aci != 0) k.couleur = couleurAci(aci);
        e.calques.push_back(k);
      }
  // Points réunis à la tolérance près (grille de recherche) ; l'origine est le point 0
  const double tol = std::max(o.tolerance, 1e-9);
  std::map<std::pair<long long, long long>, std::vector<int>> grille;
  auto cle = [&](double x, double y) { return std::pair{static_cast<long long>(std::floor(x / tol)), static_cast<long long>(std::floor(y / tol))}; };
  grille[cle(0, 0)].push_back(0);
  std::set<int> crees;
  auto point = [&](Pt2 p) {
    const double x = p.x + ox, y = p.y + oy;
    const auto [cx, cy] = cle(x, y);
    for (long long i = cx - 1; i <= cx + 1; ++i)
      for (long long j = cy - 1; j <= cy + 1; ++j) {
        const auto it = grille.find({i, j});
        if (it == grille.end()) continue;
        for (int id : it->second)
          if (const PointEsquisse* q = e.point(id); q && std::hypot(q->x - x, q->y - y) <= tol) {
            ++r.reunis;
            return id;
          }
      }
    const int id = e.ajouterPoint(x, y);
    grille[{cx, cy}].push_back(id);
    crees.insert(id);
    return id;
  };
  auto marquer = [&](int id, const std::string& calque, bool approche) {
    if (ElementEsquisse* el = e.element(id)) {
      if (o.calquesEsquisse) el->calque = calque;
      el->construction = o.construction;
    }
    r.elements.push_back(id);
    r.approches += approche;
  };
  for (const auto& t : traits) {
    switch (t.genre) {
      case TraitDxf::Genre::Ligne: {
        const int a = point(t.a), b = point(t.b);
        if (a == b) break;
        const int id = e.ajouterLigne(a, b, o.construction);
        marquer(id, t.calque, t.approche);
        ++r.lignes;
        const double dx = t.b.x - t.a.x, dy = t.b.y - t.a.y, l = std::hypot(dx, dy);
        if (o.horizontalesVerticales && !o.fixer && !t.approche && l > 0) {
          if (std::abs(dy) <= 1e-7 * l) { e.contraindre(TypeContrainte::Horizontal, {id}); ++r.relations; }
          else if (std::abs(dx) <= 1e-7 * l) { e.contraindre(TypeContrainte::Vertical, {id}); ++r.relations; }
        }
        break;
      }
      case TraitDxf::Genre::Arc: {
        const int c = point(t.centre), a = point(t.a), b = point(t.b);
        int id = 0;
        if (a == b) {
          id = e.ajouterCercle(c, t.rayon, o.construction);
          ++r.cercles;
        } else {
          id = e.ajouterArc(c, a, b, o.construction);
          ++r.arcs;
        }
        marquer(id, t.calque, t.approche);
        break;
      }
      case TraitDxf::Genre::Cercle: {
        const int id = e.ajouterCercle(point(t.centre), t.rayon, o.construction);
        marquer(id, t.calque, t.approche);
        ++r.cercles;
        if (o.fixer) e.contraindre(TypeContrainte::Rayon, {id}, t.rayon);
        break;
      }
      case TraitDxf::Genre::Point:
        point(t.a);
        ++r.points;
        break;
    }
  }
  if (o.fixer)
    for (int id : crees)
      if (const PointEsquisse* p = e.point(id)) e.contraindre(TypeContrainte::Fixe, {id}, p->x, p->y);
  if (d.courbesApprochees) r.alertes.push_back(std::to_string(d.courbesApprochees) + " ellipse(s), spline(s) ou arc(s) déformé(s) suivis par " + std::to_string(r.approches) + " arcs et lignes (à 0,005 mm près).");
  for (const auto& [type, n] : d.ignorees) r.alertes.push_back(std::to_string(n) + " « " + type + " » laissé(s) : ce n’est pas de la géométrie d’esquisse.");
  return r;
}

}  // namespace prom::dxf
