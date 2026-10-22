// Prométhée : esquisses paramétriques et solveur de contraintes géométriques. Licence GPL-3.0-only.
// Le solveur est un Levenberg-Marquardt sur les coordonnées des points et les rayons des cercles :
// il part de la géométrie actuelle et la déplace le moins possible pour satisfaire les contraintes.
#include "promethee/esquisse.hpp"
#include "promethee/lecture.hpp"
#include "promethee/moindres_carres.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace prom {

namespace {

constexpr double PI = std::numbers::pi;

struct Cle { TypeContrainte t; const char* cle; const char* nom; };
const Cle CLES[] = {
    {TypeContrainte::Coincidence, "coincidence", "Coïncidence"},       {TypeContrainte::Horizontal, "horizontal", "Horizontal"},
    {TypeContrainte::Vertical, "vertical", "Vertical"},                {TypeContrainte::Parallele, "parallele", "Parallèle"},
    {TypeContrainte::Perpendiculaire, "perpendiculaire", "Perpendiculaire"}, {TypeContrainte::Tangente, "tangente", "Tangente"},
    {TypeContrainte::Egal, "egal", "Égal"},                           {TypeContrainte::Fixe, "fixe", "Fixe"},
    {TypeContrainte::Distance, "distance", "Distance"},               {TypeContrainte::DistanceX, "distance_x", "Distance horizontale"},
    {TypeContrainte::DistanceY, "distance_y", "Distance verticale"},  {TypeContrainte::Longueur, "longueur", "Longueur"},
    {TypeContrainte::Rayon, "rayon", "Rayon"},                        {TypeContrainte::Diametre, "diametre", "Diamètre"},
    {TypeContrainte::Angle, "angle", "Angle"},                        {TypeContrainte::PointSur, "point_sur", "Point sur"},
    {TypeContrainte::Milieu, "milieu", "Milieu"},                     {TypeContrainte::Symetrie, "symetrie", "Symétrie"},
    {TypeContrainte::Concentrique, "concentrique", "Concentrique"},   {TypeContrainte::Colineaire, "colineaire", "Colinéaire"},
};

int pointCommun(const Esquisse& e, const ElementEsquisse& a, const ElementEsquisse& b);

// Accès aux paramètres : coordonnées des points (sauf l'origine) et rayons des cercles. Index des points et des
// éléments, et point commun de chaque tangence, calculés une fois : une évaluation de résidus coûte O(1) au lieu
// de parcourir l'esquisse (ES-18, ES-37).
struct Parametres {
  std::unordered_map<int, int> ix, iy, ir;
  std::unordered_set<int> pts;
  std::unordered_map<int, const ElementEsquisse*> elems;
  std::unordered_map<int, int> communs;  // contrainte de tangence → point commun (−1 : aucun)
  int n = 0;
  void construire(const Esquisse& e) {
    for (const auto& p : e.points) {
      pts.insert(p.id);
      if (p.id != 0) { ix[p.id] = n++; iy[p.id] = n++; }
    }
    for (const auto& el : e.elements) {
      elems[el.id] = &el;
      if (el.type == TypeElement::Cercle) ir[el.id] = n++;
    }
    for (const auto& c : e.contraintes) {
      if (c.type != TypeContrainte::Tangente || c.refs.size() < 2) continue;
      const ElementEsquisse* e0 = element(c.refs[0]);
      const ElementEsquisse* e1 = element(c.refs[1]);
      if (!e0 || !e1) continue;
      if (e1->type == TypeElement::Ligne) std::swap(e0, e1);
      communs[c.id] = pointCommun(e, *e0, *e1);
    }
  }
  [[nodiscard]] const ElementEsquisse* element(int id) const {
    const auto it = elems.find(id);
    return it == elems.end() ? nullptr : it->second;
  }
  [[nodiscard]] bool estPoint(int id) const { return pts.count(id) > 0; }
  std::vector<double> lire(const Esquisse& e) const {
    std::vector<double> x(static_cast<size_t>(n));
    for (const auto& p : e.points)
      if (p.id != 0) { x[static_cast<size_t>(ix.at(p.id))] = p.x; x[static_cast<size_t>(iy.at(p.id))] = p.y; }
    for (const auto& el : e.elements)
      if (el.type == TypeElement::Cercle) x[static_cast<size_t>(ir.at(el.id))] = el.rayon;
    return x;
  }
  void ecrire(Esquisse& e, const std::vector<double>& x) const {
    for (auto& p : e.points)
      if (p.id != 0) { p.x = x[static_cast<size_t>(ix.at(p.id))]; p.y = x[static_cast<size_t>(iy.at(p.id))]; }
    for (auto& el : e.elements)
      if (el.type == TypeElement::Cercle) el.rayon = x[static_cast<size_t>(ir.at(el.id))];
  }
};

struct Vue {
  const Esquisse& e;
  const Parametres& P;
  const std::vector<double>& x;
  Point pt(int id) const {
    if (id == 0) return {0, 0};
    auto a = P.ix.find(id);
    if (a == P.ix.end()) return {0, 0};
    return {x[static_cast<size_t>(a->second)], x[static_cast<size_t>(P.iy.at(id))]};
  }
  double rayon(const ElementEsquisse& el) const {
    if (el.type == TypeElement::Cercle) return x[static_cast<size_t>(P.ir.at(el.id))];
    const Point c = pt(el.centre), a = pt(el.p1);
    return std::hypot(a.x - c.x, a.y - c.y);
  }
};

double cross(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

// Point commun à deux éléments (même identifiant, ou extrémités reliées par une coïncidence) ; −1 sinon. Une
// tangence en un point commun s'écrit alors sans singularité (ES-02).
int pointCommun(const Esquisse& e, const ElementEsquisse& a, const ElementEsquisse& b) {
  auto bouts = [](const ElementEsquisse& x) { return x.type == TypeElement::Cercle ? std::array<int, 2>{-1, -1} : std::array<int, 2>{x.p1, x.p2}; };
  const auto ba = bouts(a), bb = bouts(b);
  for (int p : ba)
    for (int q : bb)
      if (p >= 0 && p == q) return p;
  for (const auto& k : e.contraintes) {
    if (k.type != TypeContrainte::Coincidence || k.reference || k.refs.size() != 2) continue;
    for (int p : ba)
      for (int q : bb)
        if (p >= 0 && q >= 0 && ((k.refs[0] == p && k.refs[1] == q) || (k.refs[0] == q && k.refs[1] == p))) return b.type == TypeElement::Ligne ? p : q;
  }
  return -1;
}
// Angle ramené dans ]−π ; π] en temps constant, même pour une cote démesurée (ES-26)
double angleNorm(double a) {
  if (!std::isfinite(a)) return 0;
  a = std::remainder(a, 2 * PI);
  return a <= -PI ? a + 2 * PI : a;
}

// Résidus d'une contrainte (nuls quand elle est satisfaite). Renvoie false si la contrainte est mal formée.
bool residus(const Vue& v, const ContrainteEsquisse& c, std::vector<double>& out) {
  const Esquisse& e = v.e;
  auto el = [&](size_t i) -> const ElementEsquisse* { return i < c.refs.size() ? v.P.element(c.refs[i]) : nullptr; };
  auto estPoint = [&](size_t i) { return i < c.refs.size() && v.P.estPoint(c.refs[i]); };
  auto P = [&](size_t i) { return v.pt(c.refs[i]); };
  auto ligne = [&](const ElementEsquisse* l, Point& a, Point& b) {
    if (!l || l->type != TypeElement::Ligne) return false;
    a = v.pt(l->p1); b = v.pt(l->p2);
    return true;
  };
  // Ligne de l'esquisse, ou l'un des deux axes (AXE_H, AXE_V).
  auto ligneRef = [&](size_t i, Point& a, Point& b) {
    if (i >= c.refs.size()) return false;
    if (c.refs[i] == AXE_H) { a = {0, 0}; b = {1, 0}; return true; }
    if (c.refs[i] == AXE_V) { a = {0, 0}; b = {0, 1}; return true; }
    return ligne(el(i), a, b);
  };
  auto estAxe = [&](size_t i) { return i < c.refs.size() && (c.refs[i] == AXE_H || c.refs[i] == AXE_V); };
  Point a, b, a2, b2;
  switch (c.type) {
    case TypeContrainte::Coincidence:
      if (!estPoint(0) || !estPoint(1)) return false;
      out.push_back(P(0).x - P(1).x); out.push_back(P(0).y - P(1).y);
      return true;
    case TypeContrainte::Horizontal:
    case TypeContrainte::Vertical: {
      if (ligne(el(0), a, b)) {}
      else if (estPoint(0) && estPoint(1)) { a = P(0); b = P(1); }
      else return false;
      out.push_back(c.type == TypeContrainte::Horizontal ? a.y - b.y : a.x - b.x);
      return true;
    }
    case TypeContrainte::Parallele:
    case TypeContrainte::Perpendiculaire: {
      if (!ligneRef(0, a, b) || !ligneRef(1, a2, b2)) return false;
      const double dx1 = b.x - a.x, dy1 = b.y - a.y, dx2 = b2.x - a2.x, dy2 = b2.y - a2.y;
      const double n = std::max(1e-9, std::hypot(dx1, dy1) * std::hypot(dx2, dy2));
      out.push_back(c.type == TypeContrainte::Parallele ? cross(dx1, dy1, dx2, dy2) / n : (dx1 * dx2 + dy1 * dy2) / n);
      return true;
    }
    case TypeContrainte::Tangente: {
      if (estAxe(0) || estAxe(1)) {
        const size_t ia = estAxe(0) ? 0 : 1;
        const ElementEsquisse* cc = el(1 - ia);
        if (!cc || cc->type == TypeElement::Ligne) return false;
        ligneRef(ia, a, b);
        const Point o = v.pt(cc->centre);
        out.push_back(std::abs(cross(b.x - a.x, b.y - a.y, o.x - a.x, o.y - a.y)) - v.rayon(*cc));
        return true;
      }
      const ElementEsquisse* e0 = el(0);
      const ElementEsquisse* e1 = el(1);
      if (!e0 || !e1) return false;
      if (e1->type == TypeElement::Ligne) std::swap(e0, e1);
      // En un point commun S, la tangence s'écrit par des angles, sans le maximum à gradient nul de
      // « distance − rayon » : rayon perpendiculaire à la ligne, ou centres alignés avec S (ES-02, ES-03)
      const auto connu = v.P.communs.find(c.id);
      const int s = connu != v.P.communs.end() ? connu->second : pointCommun(e, *e0, *e1);
      if (e0->type == TypeElement::Ligne && e1->type != TypeElement::Ligne) {
        ligne(e0, a, b);
        const Point o = v.pt(e1->centre);
        if (s >= 0) {
          const Point p = v.pt(s);
          const double n = std::max(1e-12, std::hypot(p.x - o.x, p.y - o.y) * std::hypot(b.x - a.x, b.y - a.y));
          out.push_back(((p.x - o.x) * (b.x - a.x) + (p.y - o.y) * (b.y - a.y)) / n);
          return true;
        }
        const double l = std::max(1e-9, std::hypot(b.x - a.x, b.y - a.y));
        out.push_back(std::abs(cross(b.x - a.x, b.y - a.y, o.x - a.x, o.y - a.y)) / l - v.rayon(*e1));
        return true;
      }
      if (e0->type != TypeElement::Ligne && e1->type != TypeElement::Ligne) {
        const Point o0 = v.pt(e0->centre), o1 = v.pt(e1->centre);
        if (s >= 0) {
          const Point p = v.pt(s);
          const double n = std::max(1e-12, std::hypot(p.x - o0.x, p.y - o0.y) * std::hypot(p.x - o1.x, p.y - o1.y));
          out.push_back(cross(p.x - o0.x, p.y - o0.y, p.x - o1.x, p.y - o1.y) / n);
          return true;
        }
        const double d = std::hypot(o1.x - o0.x, o1.y - o0.y), r0 = v.rayon(*e0), r1 = v.rayon(*e1);
        out.push_back(c.valeur > 0.5 ? d - std::abs(r0 - r1) : d - (r0 + r1));
        return true;
      }
      return false;
    }
    case TypeContrainte::Egal: {
      const ElementEsquisse* e0 = el(0);
      const ElementEsquisse* e1 = el(1);
      if (!e0 || !e1) return false;
      if (e0->type == TypeElement::Ligne && e1->type == TypeElement::Ligne) {
        ligne(e0, a, b); ligne(e1, a2, b2);
        out.push_back(std::hypot(b.x - a.x, b.y - a.y) - std::hypot(b2.x - a2.x, b2.y - a2.y));
        return true;
      }
      if (e0->type != TypeElement::Ligne && e1->type != TypeElement::Ligne) { out.push_back(v.rayon(*e0) - v.rayon(*e1)); return true; }
      return false;
    }
    case TypeContrainte::Fixe:
      if (!estPoint(0)) return false;
      out.push_back(P(0).x - c.valeur); out.push_back(P(0).y - c.valeur2);
      return true;
    case TypeContrainte::Distance:
      if (estPoint(0) && estPoint(1)) {
        // Distance nulle : c'est une coïncidence (deux équations), la norme n'étant pas dérivable en 0 (ES-35)
        if (std::abs(c.valeur) < 1e-12) { out.push_back(P(1).x - P(0).x); out.push_back(P(1).y - P(0).y); return true; }
        out.push_back(std::hypot(P(1).x - P(0).x, P(1).y - P(0).y) - c.valeur);
        return true;
      }
      if (estPoint(0) && ligneRef(1, a, b)) {
        const Point p = P(0);
        const double l = std::max(1e-9, std::hypot(b.x - a.x, b.y - a.y));
        out.push_back(std::abs(cross(b.x - a.x, b.y - a.y, p.x - a.x, p.y - a.y)) / l - c.valeur);
        return true;
      }
      // Deux lignes parallèles : distance du milieu de la seconde à la première.
      if (ligneRef(0, a, b) && ligneRef(1, a2, b2)) {
        const Point m{(a2.x + b2.x) / 2, (a2.y + b2.y) / 2};
        const double l = std::max(1e-9, std::hypot(b.x - a.x, b.y - a.y));
        out.push_back(std::abs(cross(b.x - a.x, b.y - a.y, m.x - a.x, m.y - a.y)) / l - c.valeur);
        return true;
      }
      return false;
    case TypeContrainte::DistanceX:
    case TypeContrainte::DistanceY: {
      const bool x = c.type == TypeContrainte::DistanceX;
      if (estPoint(0) && estPoint(1)) { out.push_back((x ? P(1).x - P(0).x : P(1).y - P(0).y) - c.valeur); return true; }
      if (estPoint(0)) { out.push_back((x ? P(0).x : P(0).y) - c.valeur); return true; }
      if (ligne(el(0), a, b)) { out.push_back((x ? b.x - a.x : b.y - a.y) - c.valeur); return true; }
      return false;
    }
    case TypeContrainte::Longueur:
      if (!ligne(el(0), a, b)) return false;
      out.push_back(std::hypot(b.x - a.x, b.y - a.y) - c.valeur);
      return true;
    case TypeContrainte::Rayon:
    case TypeContrainte::Diametre: {
      const ElementEsquisse* e0 = el(0);
      if (!e0 || e0->type == TypeElement::Ligne) return false;
      out.push_back((c.type == TypeContrainte::Diametre ? 2 : 1) * v.rayon(*e0) - c.valeur);
      return true;
    }
    case TypeContrainte::Angle: {
      if (!ligneRef(0, a, b) || !ligneRef(1, a2, b2)) return false;
      // Secteur mesuré (S2) : une ligne prise à rebours change le quart d'angle coté
      const int secteur = static_cast<int>(std::lround(c.valeur2)) & 3;
      const double s1 = (secteur & 1) ? -1 : 1, s2 = (secteur & 2) ? -1 : 1;
      const double dx1 = s1 * (b.x - a.x), dy1 = s1 * (b.y - a.y), dx2 = s2 * (b2.x - a2.x), dy2 = s2 * (b2.y - a2.y);
      out.push_back(angleNorm(std::atan2(cross(dx1, dy1, dx2, dy2), dx1 * dx2 + dy1 * dy2) - c.valeur * PI / 180));
      return true;
    }
    case TypeContrainte::PointSur: {
      if (!estPoint(0)) return false;
      const Point p = P(0);
      const ElementEsquisse* e1 = el(1);
      if (ligneRef(1, a, b)) {
        const double l = std::max(1e-9, std::hypot(b.x - a.x, b.y - a.y));
        out.push_back(cross(b.x - a.x, b.y - a.y, p.x - a.x, p.y - a.y) / l);
        return true;
      }
      if (e1 && e1->type != TypeElement::Ligne) {
        const Point o = v.pt(e1->centre);
        out.push_back(std::hypot(p.x - o.x, p.y - o.y) - v.rayon(*e1));
        return true;
      }
      return false;
    }
    case TypeContrainte::Milieu:
      if (!estPoint(0) || !ligne(el(1), a, b)) return false;
      out.push_back(P(0).x - (a.x + b.x) / 2); out.push_back(P(0).y - (a.y + b.y) / 2);
      return true;
    case TypeContrainte::Concentrique: {
      const ElementEsquisse* e0 = el(0);
      const ElementEsquisse* e1 = el(1);
      if (!e0 || !e1 || e0->type == TypeElement::Ligne || e1->type == TypeElement::Ligne) return false;
      const Point c0 = v.pt(e0->centre), c1 = v.pt(e1->centre);
      out.push_back(c1.x - c0.x);
      out.push_back(c1.y - c0.y);
      return true;
    }
    case TypeContrainte::Colineaire: {
      if (!ligne(el(0), a, b) || !ligne(el(1), a2, b2)) return false;
      const double l = std::max(1e-9, std::hypot(b.x - a.x, b.y - a.y));
      out.push_back(cross(b.x - a.x, b.y - a.y, a2.x - a.x, a2.y - a.y) / l);
      out.push_back(cross(b.x - a.x, b.y - a.y, b2.x - a.x, b2.y - a.y) / l);
      return true;
    }
    case TypeContrainte::Symetrie: {
      if (!estPoint(0) || !estPoint(1) || !ligneRef(2, a, b)) return false;
      const Point p = P(0), q = P(1), m{(p.x + q.x) / 2, (p.y + q.y) / 2};
      const double l = std::max(1e-9, std::hypot(b.x - a.x, b.y - a.y));
      out.push_back(cross(b.x - a.x, b.y - a.y, m.x - a.x, m.y - a.y) / l);
      out.push_back(((q.x - p.x) * (b.x - a.x) + (q.y - p.y) * (b.y - a.y)) / l);
      return true;
    }
  }
  return false;
}

// Bloc de résidus : la condition implicite d'un arc (rayon égal aux deux extrémités) ou une contrainte active, avec
// les paramètres qu'elle lit. Les blocs sont rangés comme les lignes du système : arcs, puis contraintes.
struct Bloc {
  const ContrainteEsquisse* contrainte = nullptr;  // nullptr : condition d'arc
  const ElementEsquisse* arc = nullptr;
  std::vector<int> params;  // indices globaux, croissants, sans doublon
  int lignes = 0;
  [[nodiscard]] int origine() const { return contrainte ? contrainte->id : 0; }
};

bool evaluerBloc(const Vue& v, const Bloc& b, std::vector<double>& out) {
  if (!b.contrainte) {
    const Point c = v.pt(b.arc->centre), a = v.pt(b.arc->p1), q = v.pt(b.arc->p2);
    out.push_back(std::hypot(a.x - c.x, a.y - c.y) - std::hypot(q.x - c.x, q.y - c.y));
    return true;
  }
  return residus(v, *b.contrainte, out);
}

// Paramètres lus par une liste de références (points, éléments et leurs points, rayons de cercles)
std::vector<int> parametresDe(const Parametres& P, const std::vector<int>& refs) {
  std::vector<int> r;
  auto point = [&](int id) {
    const auto a = P.ix.find(id);
    if (a == P.ix.end()) return;
    r.push_back(a->second);
    r.push_back(P.iy.at(id));
  };
  for (int id : refs) {
    if (P.estPoint(id)) point(id);
    else if (const ElementEsquisse* el = P.element(id)) {
      for (int q : {el->p1, el->p2, el->centre})
        if (q > 0) point(q);
      if (const auto c = P.ir.find(el->id); c != P.ir.end()) r.push_back(c->second);
    }
  }
  std::sort(r.begin(), r.end());
  r.erase(std::unique(r.begin(), r.end()), r.end());
  return r;
}

std::vector<Bloc> construireBlocs(const Esquisse& e, const Parametres& P, const std::vector<double>& x) {
  std::vector<Bloc> blocs;
  const Vue v{e, P, x};
  std::vector<double> F;
  for (const auto& el : e.elements)
    if (el.type == TypeElement::Arc && P.estPoint(el.centre) && P.estPoint(el.p1) && P.estPoint(el.p2)) {
      Bloc b;
      b.arc = &el;
      b.params = parametresDe(P, {el.centre, el.p1, el.p2});
      b.lignes = 1;
      blocs.push_back(std::move(b));
    }
  for (const auto& c : e.contraintes) {
    if (c.reference) continue;
    F.clear();
    if (!residus(v, c, F) || F.empty()) continue;
    Bloc b;
    b.contrainte = &c;
    b.params = parametresDe(P, c.refs);
    b.lignes = static_cast<int>(F.size());
    blocs.push_back(std::move(b));
  }
  return blocs;
}

// Résidus de tous les blocs, avec pour chaque ligne la contrainte d'origine (0 : condition implicite d'un arc)
void evaluerBlocs(const Esquisse& e, const Parametres& P, const std::vector<Bloc>& blocs, const std::vector<double>& x, std::vector<double>& F,
                  std::vector<int>* origine, std::vector<int>* blocDeLigne = nullptr) {
  F.clear();
  if (origine) origine->clear();
  if (blocDeLigne) blocDeLigne->clear();
  const Vue v{e, P, x};
  for (std::size_t k = 0; k < blocs.size(); ++k) {
    const std::size_t avant = F.size();
    evaluerBloc(v, blocs[k], F);
    F.resize(avant + static_cast<std::size_t>(blocs[k].lignes), 0.0);
    if (origine) origine->resize(F.size(), blocs[k].origine());
    if (blocDeLigne) blocDeLigne->resize(F.size(), static_cast<int>(k));
  }
}

// Dérivées d'un bloc par différences centrées, sur ses seuls paramètres ; colonne[g] : colonne du paramètre global
// g (−1 : hors du problème). x est perturbé puis rendu intact.
void deriverBloc(const Esquisse& e, const Parametres& P, const Bloc& b, std::vector<double>& x, const std::vector<int>& colonne,
                 numerique::MatriceCreuse& J) {
  const std::size_t base = J.size();
  J.resize(base + static_cast<std::size_t>(b.lignes));
  std::vector<double> Fp, Fm;
  const Vue v{e, P, x};
  for (int g : b.params) {
    const int col = colonne[static_cast<std::size_t>(g)];
    if (col < 0) continue;
    const double x0 = x[static_cast<std::size_t>(g)];
    const double h = 1e-7 * std::max(1.0, std::abs(x0));
    Fp.clear();
    Fm.clear();
    x[static_cast<std::size_t>(g)] = x0 + h;
    evaluerBloc(v, b, Fp);
    x[static_cast<std::size_t>(g)] = x0 - h;
    evaluerBloc(v, b, Fm);
    x[static_cast<std::size_t>(g)] = x0;
    for (int i = 0; i < b.lignes; ++i) {
      const double d = static_cast<std::size_t>(i) < Fp.size() && static_cast<std::size_t>(i) < Fm.size()
                           ? (Fp[static_cast<std::size_t>(i)] - Fm[static_cast<std::size_t>(i)]) / (2 * h) : 0.0;
      J[base + static_cast<std::size_t>(i)].emplace_back(col, d);  // entrée structurelle, même nulle
    }
  }
  // Colonnes croissantes : params est trié et la numérotation locale conserve l'ordre global
}

// Composante connexe du système : paramètres liés entre eux par des blocs. Chacune se résout seule ; une
// composante déjà satisfaite et que l'on ne tire pas n'est pas recalculée (ES-18).
struct Composante {
  std::vector<int> params;  // globaux, croissants
  std::vector<int> blocs;
};

std::vector<Composante> composantes(const std::vector<Bloc>& blocs, int n) {
  std::vector<int> parent(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) parent[static_cast<std::size_t>(i)] = i;
  auto racine = [&](int a) {
    while (parent[static_cast<std::size_t>(a)] != a) a = parent[static_cast<std::size_t>(a)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(a)])];
    return a;
  };
  for (const auto& b : blocs)
    for (std::size_t k = 1; k < b.params.size(); ++k) {
      const int ra = racine(b.params[0]), rb = racine(b.params[k]);
      if (ra != rb) parent[static_cast<std::size_t>(std::max(ra, rb))] = std::min(ra, rb);
    }
  std::vector<int> numero(static_cast<std::size_t>(n), -1);
  std::vector<Composante> res;
  for (int i = 0; i < n; ++i) {
    const int r = racine(i);
    if (numero[static_cast<std::size_t>(r)] < 0) { numero[static_cast<std::size_t>(r)] = static_cast<int>(res.size()); res.emplace_back(); }
    res[static_cast<std::size_t>(numero[static_cast<std::size_t>(r)])].params.push_back(i);
  }
  for (std::size_t k = 0; k < blocs.size(); ++k)
    if (!blocs[k].params.empty()) res[static_cast<std::size_t>(numero[static_cast<std::size_t>(racine(blocs[k].params[0]))])].blocs.push_back(static_cast<int>(k));
  return res;
}

// Problème de moindres carrés d'une composante : ses blocs, plus, pendant un glisser, un léger rappel de chaque
// paramètre non tiré vers sa valeur de départ et l'attraction des paramètres tirés vers la souris.
class ProblemeComposante final : public numerique::ProblemeMoindresCarres {
 public:
  struct Cible { int local; double valeur; };

  ProblemeComposante(const Esquisse& e, const Parametres& P, std::vector<double>& x, const std::vector<Bloc>& blocs, const Composante& c)
      : e_(e), P_(P), x_(x), blocs_(blocs), c_(c), colonne_(x.size(), -1) {
    for (std::size_t k = 0; k < c.params.size(); ++k) colonne_[static_cast<std::size_t>(c.params[k])] = static_cast<int>(k);
  }
  [[nodiscard]] int local(int global) const { return colonne_[static_cast<std::size_t>(global)]; }
  [[nodiscard]] std::vector<double> lire() const {
    std::vector<double> xl(c_.params.size());
    for (std::size_t k = 0; k < xl.size(); ++k) xl[k] = x_[static_cast<std::size_t>(c_.params[k])];
    return xl;
  }
  void ecrire(const std::vector<double>& xl) {
    for (std::size_t k = 0; k < xl.size(); ++k) x_[static_cast<std::size_t>(c_.params[k])] = xl[k];
  }
  // Contraintes seules (rappel et cibles retirés)
  void contraintesSeules() { rappel_ = 0; poidsCible_ = 0; cibles_.clear(); }
  void glisser(double rappel, const std::vector<double>& depart, const std::vector<Cible>& cibles, double poids) {
    rappel_ = rappel;
    depart_ = depart;
    cibles_ = cibles;
    poidsCible_ = poids;
    tire_.assign(c_.params.size(), 0);
    for (const auto& t : cibles) tire_[static_cast<std::size_t>(t.local)] = 1;
  }

  [[nodiscard]] int dimension() const override { return static_cast<int>(c_.params.size()); }
  void residus(const std::vector<double>& xl, std::vector<double>& F) override {
    ecrire(xl);
    F.clear();
    const Vue v{e_, P_, x_};
    for (int k : c_.blocs) {
      const Bloc& b = blocs_[static_cast<std::size_t>(k)];
      const std::size_t avant = F.size();
      evaluerBloc(v, b, F);
      F.resize(avant + static_cast<std::size_t>(b.lignes), 0.0);
    }
    if (rappel_ > 0)
      for (std::size_t i = 0; i < xl.size(); ++i)
        if (!tire_[i]) F.push_back(rappel_ * (xl[i] - depart_[i]));
    if (poidsCible_ > 0)
      for (const auto& t : cibles_) F.push_back(poidsCible_ * (xl[static_cast<std::size_t>(t.local)] - t.valeur));
  }
  void jacobienne(const std::vector<double>& xl, numerique::MatriceCreuse& J) override {
    ecrire(xl);
    J.clear();
    for (int k : c_.blocs) deriverBloc(e_, P_, blocs_[static_cast<std::size_t>(k)], x_, colonne_, J);
    if (rappel_ > 0)
      for (std::size_t i = 0; i < xl.size(); ++i)
        if (!tire_[i]) J.push_back({{static_cast<int>(i), rappel_}});
    if (poidsCible_ > 0)
      for (const auto& t : cibles_) J.push_back({{t.local, poidsCible_}});
  }

 private:
  const Esquisse& e_;
  const Parametres& P_;
  std::vector<double>& x_;
  const std::vector<Bloc>& blocs_;
  const Composante& c_;
  std::vector<int> colonne_;
  double rappel_ = 0, poidsCible_ = 0;
  std::vector<double> depart_;
  std::vector<Cible> cibles_;
  std::vector<char> tire_;
};

// Jacobienne creuse de tout le système (colonnes globales)
numerique::MatriceCreuse jacobienneGlobale(const Esquisse& e, const Parametres& P, const std::vector<Bloc>& blocs, std::vector<double> x) {
  std::vector<int> colonne(x.size());
  for (std::size_t i = 0; i < x.size(); ++i) colonne[i] = static_cast<int>(i);
  numerique::MatriceCreuse J;
  for (const auto& b : blocs) deriverBloc(e, P, b, x, colonne, J);
  return J;
}
}  // namespace

double valeurReecrite(const ContrainteEsquisse& c, double saisie) {
  const bool oriente = c.type == TypeContrainte::DistanceX || c.type == TypeContrainte::DistanceY || c.type == TypeContrainte::Angle;
  if (!oriente || c.valeur == 0) return saisie;
  return c.valeur < 0 ? -std::abs(saisie) : std::abs(saisie);
}

bool estCote(TypeContrainte t) {
  return t == TypeContrainte::Distance || t == TypeContrainte::DistanceX || t == TypeContrainte::DistanceY || t == TypeContrainte::Longueur ||
         t == TypeContrainte::Rayon || t == TypeContrainte::Diametre || t == TypeContrainte::Angle;
}

double mesurer(const Esquisse& e, const ContrainteEsquisse& c) {
  Parametres P;
  P.construire(e);
  const std::vector<double> x = P.lire(e);
  const Vue v{e, P, x};
  ContrainteEsquisse k = c;
  k.valeur = 0;
  std::vector<double> r;
  if (!residus(v, k, r) || r.empty()) return 0;
  return c.type == TypeContrainte::Angle ? r[0] * 180 / PI : r[0];
}

namespace {
// Écart d'une contrainte telle qu'elle est (le plus grand de ses résidus) ; 0 si elle ne se lit pas
double mesurerResidu(const Esquisse& e, const ContrainteEsquisse& c) {
  Parametres P;
  P.construire(e);
  const std::vector<double> x = P.lire(e);
  const Vue v{e, P, x};
  std::vector<double> r;
  if (!residus(v, c, r)) return 0;
  double m = 0;
  for (double a : r) m = std::max(m, std::abs(a));
  return m;
}
}  // namespace

std::vector<int> Esquisse::ajouterRectangleCentre(double cx, double cy, double coinX, double coinY, int* centre) {
  const double dx = std::abs(coinX - cx), dy = std::abs(coinY - cy);
  const auto l = ajouterRectangle(cx - dx, cy - dy, cx + dx, cy + dy);
  const int c = ajouterPoint(cx, cy);
  if (centre) *centre = c;
  const int d1 = ajouterLigne(element(l[0])->p1, element(l[1])->p2, true);
  const int d2 = ajouterLigne(element(l[0])->p2, element(l[2])->p2, true);
  // Le centre est le milieu d'une diagonale ; l'autre diagonale d'un rectangle y passe d'elle-même (ES-22)
  contraindre(TypeContrainte::Milieu, {c, d1});
  (void)d2;
  return l;
}

std::vector<int> Esquisse::ajouterRectangle3Points(double ax, double ay, double bx, double by, double cx, double cy) {
  const double L = std::hypot(bx - ax, by - ay);
  if (L < 1e-9) return {};
  const double nx = -(by - ay) / L, ny = (bx - ax) / L;
  const double w = (cx - bx) * nx + (cy - by) * ny;  // largeur signée, perpendiculaire au premier côté
  if (std::abs(w) < 1e-9) return {};
  const int a = ajouterPoint(ax, ay), b = ajouterPoint(bx, by), c = ajouterPoint(bx + nx * w, by + ny * w), d = ajouterPoint(ax + nx * w, ay + ny * w);
  const std::vector<int> l = {ajouterLigne(a, b), ajouterLigne(b, c), ajouterLigne(c, d), ajouterLigne(d, a)};
  contraindre(TypeContrainte::Perpendiculaire, {l[0], l[1]});
  contraindre(TypeContrainte::Parallele, {l[0], l[2]});
  contraindre(TypeContrainte::Parallele, {l[1], l[3]});
  return l;
}

std::vector<int> Esquisse::ajouterParallelogramme(double ax, double ay, double bx, double by, double cx, double cy) {
  if (std::abs((bx - ax) * (cy - by) - (by - ay) * (cx - bx)) < 1e-9) return {};
  const int a = ajouterPoint(ax, ay), b = ajouterPoint(bx, by), c = ajouterPoint(cx, cy), d = ajouterPoint(ax + cx - bx, ay + cy - by);
  const std::vector<int> l = {ajouterLigne(a, b), ajouterLigne(b, c), ajouterLigne(c, d), ajouterLigne(d, a)};
  contraindre(TypeContrainte::Parallele, {l[0], l[2]});
  contraindre(TypeContrainte::Parallele, {l[1], l[3]});
  return l;
}

int Esquisse::ajouterCercle3Points(double ax, double ay, double bx, double by, double px, double py) {
  const double d = 2 * (ax * (by - py) + bx * (py - ay) + px * (ay - by));
  if (std::abs(d) < 1e-12) return 0;
  const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, p2 = px * px + py * py;
  const double cx = (a2 * (by - py) + b2 * (py - ay) + p2 * (ay - by)) / d, cy = (a2 * (px - bx) + b2 * (ax - px) + p2 * (bx - ax)) / d;
  return ajouterCercle(ajouterPoint(cx, cy), std::hypot(ax - cx, ay - cy));
}

int Esquisse::ajouterLigneMilieu(int m, double fx, double fy) {
  const PointEsquisse* pm = point(m);
  if (!pm) return 0;
  const double mx = pm->x, my = pm->y;
  if (std::hypot(fx - mx, fy - my) < 1e-9) return 0;
  const int a = ajouterPoint(2 * mx - fx, 2 * my - fy), b = ajouterPoint(fx, fy);
  const int l = ajouterLigne(a, b);
  contraindre(TypeContrainte::Milieu, {m, l});
  return l;
}

bool Esquisse::calculerArcTangent(int depart, double fx, double fy, Point& centre, bool& sensDirect, int* precedent, int prefere) const {
  const PointEsquisse* s = point(depart);
  if (!s) return false;
  // Direction de sortie au point de départ, dans le prolongement d'un élément qui y aboutit
  auto direction = [&](const ElementEsquisse& el, double& tx, double& ty) {
    if (el.type == TypeElement::Ligne) {
      const PointEsquisse* o = point(el.p1 == depart ? el.p2 : el.p1);
      if (!o) return false;
      tx = s->x - o->x; ty = s->y - o->y;
    } else {
      const PointEsquisse* c = point(el.centre);
      if (!c) return false;
      const double rx = s->x - c->x, ry = s->y - c->y;
      tx = -ry; ty = rx;
      if (el.p1 == depart) { tx = -tx; ty = -ty; }
    }
    const double n = std::hypot(tx, ty);
    if (n < 1e-12) return false;
    tx /= n; ty /= n;
    return true;
  };
  // Élément de départ : celui que désigne l'outil (le dernier tracé), sinon, parmi les éléments qui aboutissent au
  // point (géométrie d'abord, construction ensuite), celui dont le prolongement va le plus vers l'arrivée visée :
  // depuis un coin, c'est le geste qui choisit le côté, pas l'ordre interne des éléments (ES-34).
  const ElementEsquisse* avant = nullptr;
  double tx = 0, ty = 0;
  if (const ElementEsquisse* el = element(prefere); el && el->type != TypeElement::Cercle && (el->p1 == depart || el->p2 == depart) && direction(*el, tx, ty)) avant = el;
  if (!avant) {
    const double vx = fx - s->x, vy = fy - s->y, vn = std::max(std::hypot(vx, vy), 1e-12);
    double meilleur = -1e300;
    for (const auto& el : elements) {
      if ((el.p1 != depart && el.p2 != depart) || el.type == TypeElement::Cercle) continue;
      double dx = 0, dy = 0;
      if (!direction(el, dx, dy)) continue;
      const double score = (dx * vx + dy * vy) / vn - (el.construction ? 10.0 : 0.0);
      if (score > meilleur) { meilleur = score; avant = &el; tx = dx; ty = dy; }
    }
  }
  if (!avant) return false;
  const double nx = -ty, ny = tx;
  const double vx = fx - s->x, vy = fy - s->y;
  const double den = 2 * (vx * nx + vy * ny);
  if (std::abs(den) < 1e-12) return false;
  const double t = (vx * vx + vy * vy) / den;
  centre = {s->x + nx * t, s->y + ny * t};
  sensDirect = t > 0;
  if (precedent) *precedent = avant->id;
  return true;
}

int Esquisse::ajouterArcTangent(int depart, double fx, double fy, int prefere) {
  Point centre{0, 0};
  bool direct = true;
  int precedent = 0;
  if (!calculerArcTangent(depart, fx, fy, centre, direct, &precedent, prefere)) return 0;
  const int c = ajouterPoint(centre.x, centre.y);
  const int f = ajouterPoint(fx, fy);
  const int arc = direct ? ajouterArc(c, depart, f) : ajouterArc(c, f, depart);
  // Tangence intérieure quand les deux centres sont du même côté du raccord (ES-03)
  double interieure = 0;
  if (const ElementEsquisse* pr = element(precedent); pr && pr->type != TypeElement::Ligne)
    if (const PointEsquisse* S = point(depart); S && point(pr->centre)) {
      const PointEsquisse* C0 = point(pr->centre);
      if ((C0->x - S->x) * (centre.x - S->x) + (C0->y - S->y) * (centre.y - S->y) > 0) interieure = 1;
    }
  contraindre(TypeContrainte::Tangente, {precedent, arc}, interieure);
  return arc;
}

namespace {
// Copie d'éléments par une transformation des points ; renvoie la correspondance des points (original → copie).
std::vector<int> copier(Esquisse& e, const std::vector<int>& ids, const std::function<Point(Point)>& t, bool inverserArcs, std::map<int, int>& correspondance) {
  std::vector<int> crees;
  auto copiePoint = [&](int p) {
    if (p == 0 && !correspondance.count(0)) {
      const PointEsquisse* o = e.point(0);
      const Point q = t({o->x, o->y});
      correspondance[0] = std::abs(q.x) < 1e-12 && std::abs(q.y) < 1e-12 ? 0 : e.ajouterPoint(q.x, q.y);
    }
    if (!correspondance.count(p)) {
      const PointEsquisse* o = e.point(p);
      const Point q = t({o->x, o->y});
      correspondance[p] = e.ajouterPoint(q.x, q.y);
    }
    return correspondance[p];
  };
  // On copie d'abord la liste : ajouter des éléments peut déplacer le tableau.
  std::vector<ElementEsquisse> sources;
  for (int id : ids) if (const ElementEsquisse* el = e.element(id)) sources.push_back(*el);
  for (const auto& el : sources) {
    ElementEsquisse c = el;
    c.id = e.prochainId++;
    if (el.type != TypeElement::Cercle) { c.p1 = copiePoint(el.p1); c.p2 = copiePoint(el.p2); }
    if (el.type != TypeElement::Ligne) c.centre = copiePoint(el.centre);
    if (el.type == TypeElement::Arc && inverserArcs) std::swap(c.p1, c.p2);
    e.elements.push_back(c);
    crees.push_back(c.id);
  }
  return crees;
}
}  // namespace

namespace {
// Les deux lignes (hors construction) qui bordent le coin au point « pid » ; s'il en part plus de deux, celles qui
// encadrent la direction du clic (cx, cy).
std::vector<ElementEsquisse*> lignesDuCoin(Esquisse& e, int pid, double cx, double cy) {
  std::vector<ElementEsquisse*> lignes;
  for (auto& el : e.elements)
    if (el.type == TypeElement::Ligne && !el.construction && (el.p1 == pid || el.p2 == pid)) lignes.push_back(&el);
  if (lignes.size() <= 2) return lignes;
  const PointEsquisse* P = e.point(pid);
  if (!P || cx > 1e299) return {};
  auto angle = [&](const ElementEsquisse* l) {
    const PointEsquisse* o = e.point(l->p1 == pid ? l->p2 : l->p1);
    return std::atan2(o->y - P->y, o->x - P->x);
  };
  std::sort(lignes.begin(), lignes.end(), [&](auto* a, auto* b) { return angle(a) < angle(b); });
  const double ac = std::atan2(cy - P->y, cx - P->x);
  for (size_t i = 0; i < lignes.size(); ++i) {
    const double a0 = angle(lignes[i]);
    double a1 = angle(lignes[(i + 1) % lignes.size()]);
    while (a1 <= a0) a1 += 2 * std::numbers::pi;
    double a = ac;
    while (a < a0) a += 2 * std::numbers::pi;
    if (a <= a1) return {lignes[i], lignes[(i + 1) % lignes.size()]};
  }
  return {};
}
}  // namespace

std::vector<int> Esquisse::ajouterPolygoneCirconscrit(double cx, double cy, double mx, double my, int cotes) {
  const int n = std::max(3, cotes);
  const double r = std::hypot(mx - cx, my - cy) / std::cos(std::numbers::pi / n);
  const double a = std::atan2(my - cy, mx - cx) + std::numbers::pi / n;
  return ajouterPolygone(cx, cy, cx + r * std::cos(a), cy + r * std::sin(a), n);
}

int Esquisse::chanfreinEsquisse(int pid, double d, double cx, double cy, double d2) {
  if (d2 <= 0) d2 = d;
  std::vector<ElementEsquisse*> lignes = lignesDuCoin(*this, pid, cx, cy);
  if (lignes.size() != 2 || d <= 0) return 0;
  const PointEsquisse* pc = point(pid);
  if (!pc) return 0;
  // Tout est calculé avant de rien ajouter : les pointeurs vers les points et les éléments ne survivent pas à un
  // ajout (réallocation du tableau, ES-19), et un chanfrein refusé ne doit rien laisser (ES-27).
  const double cxp = pc->x, cyp = pc->y;
  const int idL[2] = {lignes[0]->id, lignes[1]->id};
  const int loin[2] = {lignes[0]->p1 == pid ? lignes[0]->p2 : lignes[0]->p1, lignes[1]->p1 == pid ? lignes[1]->p2 : lignes[1]->p1};
  const double dist[2] = {d, d2};
  double nx[2], ny[2];
  for (int k = 0; k < 2; ++k) {
    const PointEsquisse* o = point(loin[k]);
    if (!o) return 0;
    const double L = std::hypot(o->x - cxp, o->y - cyp);
    if (L <= dist[k] + 1e-9) return 0;
    nx[k] = cxp + (o->x - cxp) / L * dist[k];
    ny[k] = cyp + (o->y - cyp) / L * dist[k];
  }
  const int n0 = ajouterPoint(nx[0], ny[0]), n1 = ajouterPoint(nx[1], ny[1]);
  const int nouveaux[2] = {n0, n1};
  // Les deux lignes s'arrêtent avant le coin
  for (int k = 0; k < 2; ++k)
    for (auto& el : elements)
      if (el.id == idL[k]) (el.p1 == pid ? el.p1 : el.p2) = nouveaux[k];
  const int ch = ajouterLigne(n0, n1);
  // Comme le congé : l'ancien sommet reste « coin virtuel » à l'intersection des lignes prolongées, les cotes
  // de longueur des lignes deviennent des distances jusqu'à ce coin, et les deux distances du chanfrein sont
  // cotées depuis le coin : le chanfrein reste paramétrique et l'esquisse garde ses degrés de liberté (ES-07).
  contraindre(TypeContrainte::PointSur, {pid, idL[0]});
  contraindre(TypeContrainte::PointSur, {pid, idL[1]});
  for (auto& k : contraintes)
    if (k.type == TypeContrainte::Longueur && k.refs.size() == 1 && (k.refs[0] == idL[0] || k.refs[0] == idL[1])) {
      k.type = TypeContrainte::Distance;
      k.refs = {k.refs[0] == idL[0] ? loin[0] : loin[1], pid};
    }
  contraindre(TypeContrainte::Distance, {n0, pid}, d);
  contraindre(TypeContrainte::Distance, {n1, pid}, d2);
  return ch;
}

std::vector<int> Esquisse::ajouterRainureArc(double cx, double cy, double ax, double ay, double bx, double by, double largeur) {
  const double r = std::hypot(ax - cx, ay - cy), w = largeur / 2;
  if (r <= w || w <= 0) return {};
  const double a0 = std::atan2(ay - cy, ax - cx);
  double a1 = std::atan2(by - cy, bx - cx);
  while (a1 <= a0) a1 += 2 * std::numbers::pi;
  const int c = ajouterPoint(cx, cy);
  auto P = [&](double rr, double a) { return ajouterPoint(cx + rr * std::cos(a), cy + rr * std::sin(a)); };
  const int e0 = P(r + w, a0), e1 = P(r + w, a1), i0 = P(r - w, a0), i1 = P(r - w, a1);
  const int c0 = P(r, a0), c1 = P(r, a1);
  const std::vector<int> arcs = {ajouterArc(c, e0, e1), ajouterArc(c, i0, i1), ajouterArc(c1, e1, i1), ajouterArc(c0, i0, e0)};
  // Chaque bouchon est tangent aux deux arcs en ses extrémités (formulation au point commun, ES-02) : la forme
  // garde ses six degrés de liberté (centre, rayon médian, largeur, deux angles) au glisser comme à la cote (ES-11).
  // L'égalité des bouchons s'en déduit et n'est plus posée.
  contraindre(TypeContrainte::Tangente, {arcs[0], arcs[2]});
  contraindre(TypeContrainte::Tangente, {arcs[1], arcs[2]});
  contraindre(TypeContrainte::Tangente, {arcs[0], arcs[3]});
  contraindre(TypeContrainte::Tangente, {arcs[1], arcs[3]});
  return arcs;
}

std::vector<int> Esquisse::transformer(const std::vector<int>& ids, const std::function<Point(Point)>& t, double facteur, bool copie) {
  if (copie) {
    std::map<int, int> corr;
    const auto crees = copier(*this, ids, t, false, corr);
    for (int id : crees)
      for (auto& el : elements) if (el.id == id && el.type == TypeElement::Cercle) el.rayon *= facteur;
    return crees;
  }
  std::set<int> pts;
  std::set<int> elts(ids.begin(), ids.end());
  for (const auto& el : elements) {
    if (!elts.count(el.id)) continue;
    for (int p : {el.p1, el.p2, el.centre}) if (p > 0) pts.insert(p);
  }
  for (auto& p : points)
    if (pts.count(p.id)) { const Point q = t({p.x, p.y}); p.x = q.x; p.y = q.y; }
  for (auto& el : elements) if (elts.count(el.id) && el.type == TypeElement::Cercle) el.rayon *= facteur;
  // Les contraintes suivent la transformation (ES-05, ES-06) : points fixes à leur nouvelle place ; toute cote
  // dont les références ont toutes bougé (les axes et l'origine servent de repère) est remesurée, signe compris ;
  // une relation horizontale ou verticale qu'une rotation a défaite devient un angle avec l'axe.
  auto bouge = [&](int r) { return elts.count(r) > 0 || pts.count(r) > 0; };
  auto repere = [&](int r) { return r == AXE_H || r == AXE_V || r == 0; };
  std::vector<int> aRetirer;
  for (auto& c : contraintes) {
    if (c.type == TypeContrainte::Fixe && !c.refs.empty() && pts.count(c.refs[0])) {
      if (const PointEsquisse* p = point(c.refs[0])) { c.valeur = p->x; c.valeur2 = p->y; }
      continue;
    }
    bool tous = !c.refs.empty(), un = false;
    for (int r : c.refs) {
      tous &= bouge(r) || repere(r);
      un |= bouge(r);
    }
    if (!tous || !un) continue;
    if (estCote(c.type)) {
      c.valeur = mesurer(*this, c);
      if (c.type == TypeContrainte::Diametre || c.type == TypeContrainte::Rayon || c.type == TypeContrainte::Longueur || c.type == TypeContrainte::Distance) c.valeur = std::abs(c.valeur);
      continue;
    }
    if (c.type == TypeContrainte::Horizontal || c.type == TypeContrainte::Vertical) {
      ContrainteEsquisse essai = c;
      if (std::abs(mesurerResidu(*this, essai)) < 1e-7) continue;  // toujours vraie (translation, échelle)
      const ElementEsquisse* l = c.refs.empty() ? nullptr : element(c.refs[0]);
      if (l && l->type == TypeElement::Ligne) {
        const bool h = c.type == TypeContrainte::Horizontal;
        c.type = TypeContrainte::Angle;
        c.refs = {h ? AXE_H : AXE_V, l->id};
        c.valeur = mesurer(*this, c);
      } else aRetirer.push_back(c.id);
    }
  }
  if (!aRetirer.empty())
    contraintes.erase(std::remove_if(contraintes.begin(), contraintes.end(), [&](const ContrainteEsquisse& k) { return std::find(aRetirer.begin(), aRetirer.end(), k.id) != aRetirer.end(); }),
                      contraintes.end());
  return ids;
}

namespace {
// Intersections de deux éléments (lignes prises comme segments, arcs limités à leur étendue).
struct Croisement { Point p; double t; };  // t : paramètre sur le premier élément (0..1 pour une ligne, angle sinon)
double angleDe(const Point& c, const Point& p) { return std::atan2(p.y - c.y, p.x - c.x); }
bool dansArc(const Esquisse& e, const ElementEsquisse& a, double ang) {
  if (a.type != TypeElement::Arc) return true;
  if (!e.elementValide(a)) return false;
  const Point c{e.point(a.centre)->x, e.point(a.centre)->y};
  double a0 = angleDe(c, {e.point(a.p1)->x, e.point(a.p1)->y}), a1 = angleDe(c, {e.point(a.p2)->x, e.point(a.p2)->y});
  while (a1 <= a0) a1 += 2 * std::numbers::pi;
  // Tolérance aux deux bouts : un point exactement au départ de l'arc, à l'arrondi près, en fait partie (ES-25)
  while (ang < a0 - 1e-9) ang += 2 * std::numbers::pi;
  while (ang > a0 - 1e-9 + 2 * std::numbers::pi) ang -= 2 * std::numbers::pi;
  return ang <= a1 + 1e-9;
}
std::vector<Point> croisementsBruts(const Esquisse& e, const ElementEsquisse& a, const ElementEsquisse& b, bool segmentA = true) {
  std::vector<Point> r;
  if (!e.elementValide(a) || !e.elementValide(b)) return r;
  auto P = [&](int id) { const PointEsquisse* p = e.point(id); return Point{p->x, p->y}; };
  auto cercle = [&](const ElementEsquisse& x, Point& c, double& rr) { c = P(x.centre); rr = e.rayonDe(x); };
  if (a.type == TypeElement::Ligne && b.type == TypeElement::Ligne) {
    const Point p = P(a.p1), q = P(a.p2), u = P(b.p1), v = P(b.p2);
    const double d = (q.x - p.x) * (v.y - u.y) - (q.y - p.y) * (v.x - u.x);
    if (std::abs(d) < 1e-12) return r;
    const double t = ((u.x - p.x) * (v.y - u.y) - (u.y - p.y) * (v.x - u.x)) / d;
    const double s = ((u.x - p.x) * (q.y - p.y) - (u.y - p.y) * (q.x - p.x)) / d;
    if ((!segmentA || (t >= -1e-9 && t <= 1 + 1e-9)) && s >= -1e-9 && s <= 1 + 1e-9) r.push_back({p.x + t * (q.x - p.x), p.y + t * (q.y - p.y)});
    return r;
  }
  if (a.type == TypeElement::Ligne || b.type == TypeElement::Ligne) {
    const ElementEsquisse& l = a.type == TypeElement::Ligne ? a : b;
    const ElementEsquisse& k = a.type == TypeElement::Ligne ? b : a;
    const bool segL = a.type == TypeElement::Ligne ? segmentA : true;
    const Point p = P(l.p1), q = P(l.p2);
    Point c;
    double rr;
    cercle(k, c, rr);
    const double dx = q.x - p.x, dy = q.y - p.y, fx = p.x - c.x, fy = p.y - c.y;
    const double A = dx * dx + dy * dy, B = 2 * (fx * dx + fy * dy), C = fx * fx + fy * fy - rr * rr;
    const double disc = B * B - 4 * A * C;
    if (A < 1e-18 || disc < 0) return r;
    for (double sg : {-1.0, 1.0}) {
      const double t = (-B + sg * std::sqrt(disc)) / (2 * A);
      if (segL && (t < -1e-9 || t > 1 + 1e-9)) continue;
      const Point x{p.x + t * dx, p.y + t * dy};
      if (dansArc(e, k, angleDe(c, x))) r.push_back(x);
      if (disc < 1e-18) break;
    }
    return r;
  }
  Point c1, c2;
  double r1, r2;
  cercle(a, c1, r1);
  cercle(b, c2, r2);
  const double d = std::hypot(c2.x - c1.x, c2.y - c1.y);
  if (d < 1e-12 || d > r1 + r2 + 1e-9 || d < std::abs(r1 - r2) - 1e-9) return r;
  const double l = (r1 * r1 - r2 * r2 + d * d) / (2 * d), h = std::sqrt(std::max(0.0, r1 * r1 - l * l));
  const Point m{c1.x + l * (c2.x - c1.x) / d, c1.y + l * (c2.y - c1.y) / d};
  for (double sg : {-1.0, 1.0}) {
    const Point x{m.x + sg * h * (c2.y - c1.y) / d, m.y - sg * h * (c2.x - c1.x) / d};
    if (dansArc(e, a, angleDe(c1, x)) && dansArc(e, b, angleDe(c2, x))) r.push_back(x);
    if (h < 1e-12) break;
  }
  return r;
}
}  // namespace

bool Esquisse::ajuster(int id, double x, double y) {
  const ElementEsquisse* el0 = element(id);
  if (!el0 || !elementValide(*el0)) return false;
  const ElementEsquisse el = *el0;
  auto P = [&](int pid) { const PointEsquisse* p = point(pid); return Point{p->x, p->y}; };
  // Un point devenu inutile (plus aucun élément ne s'en sert) disparaît, avec ses contraintes.
  auto retirerSiOrphelin = [&](int pid) {
    if (pid <= 0) return;
    for (const auto& e2 : elements) if (e2.p1 == pid || e2.p2 == pid || e2.centre == pid) return;
    contraintes.erase(std::remove_if(contraintes.begin(), contraintes.end(), [pid](const ContrainteEsquisse& c) { return std::find(c.refs.begin(), c.refs.end(), pid) != c.refs.end(); }), contraintes.end());
    points.erase(std::remove_if(points.begin(), points.end(), [pid](const PointEsquisse& p) { return p.id == pid; }), points.end());
  };
  // Relations de l'élément coupé qui n'ont plus de sens (ES-13) : sa longueur, le milieu d'un point sur lui, et son
  // égalité avec d'autres éléments, reportée entre ceux-ci (un côté de polygone coupé laisse les autres égaux)
  auto retirerCotesLongueur = [&](int eid) {
    std::vector<int> egaux;
    for (const auto& c : contraintes)
      if (c.type == TypeContrainte::Egal && c.refs.size() == 2 && (c.refs[0] == eid || c.refs[1] == eid)) egaux.push_back(c.refs[0] == eid ? c.refs[1] : c.refs[0]);
    contraintes.erase(std::remove_if(contraintes.begin(), contraintes.end(),
                                     [eid](const ContrainteEsquisse& c) {
                                       const bool vise = std::find(c.refs.begin(), c.refs.end(), eid) != c.refs.end();
                                       return vise && (c.type == TypeContrainte::Longueur || c.type == TypeContrainte::Egal || c.type == TypeContrainte::Milieu);
                                     }),
                      contraintes.end());
    for (std::size_t k = 1; k < egaux.size(); ++k) contraindre(TypeContrainte::Egal, {egaux[0], egaux[k]});
  };
  // Le morceau neuf garde la nature de l'élément coupé : construction, calque (ES-28)
  auto commeOriginal = [&](int nouveau) {
    for (auto& e2 : elements)
      if (e2.id == nouveau) { e2.construction = el.construction; e2.calque = el.calque; }
  };
  // Intersections le long de l'élément cliqué : paramètre (0..1 pour une ligne, angle sinon) et élément coupant
  struct Coupe { double t; int par; };
  std::vector<Coupe> coupes;
  const Point c0 = el.type == TypeElement::Ligne ? Point{0, 0} : P(el.centre);
  for (const auto& autre : elements) {
    if (autre.id == el.id) continue;
    for (const Point& q : croisementsBruts(*this, el, autre)) {
      if (el.type == TypeElement::Ligne) {
        const Point a = P(el.p1), b = P(el.p2);
        const double l2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
        coupes.push_back({((q.x - a.x) * (b.x - a.x) + (q.y - a.y) * (b.y - a.y)) / l2, autre.id});
      } else {
        coupes.push_back({angleDe(c0, q), autre.id});
      }
    }
  }
  auto lierA = [&](int pid, int par) { if (par > 0 && element(par)) contraindre(TypeContrainte::PointSur, {pid, par}); };
  if (el.type == TypeElement::Ligne) {
    const Point a = P(el.p1), b = P(el.p2);
    const double l2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
    if (l2 < 1e-18) return false;
    const double tc = ((x - a.x) * (b.x - a.x) + (y - a.y) * (b.y - a.y)) / l2;
    Coupe bas{0, 0}, haut{1, 0};
    bool aBas = false, aHaut = false;
    for (const auto& k : coupes) {
      if (k.t > 1e-6 && k.t < tc && k.t > bas.t) { bas = k; aBas = true; }
      if (k.t < 1 - 1e-6 && k.t > tc && k.t < haut.t) { haut = k; aHaut = true; }
    }
    const int ancien1 = el.p1, ancien2 = el.p2;
    if (!aBas && !aHaut) {
      supprimer(el.id);
      retirerSiOrphelin(ancien1);
      retirerSiOrphelin(ancien2);
      return true;
    }
    retirerCotesLongueur(el.id);
    auto en = [&](double t) { return ajouterPoint(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)); };
    if (aBas && aHaut) {
      // Morceau du milieu retiré : la ligne garde le début, une nouvelle ligne reprend la fin
      const int n1 = en(bas.t), n2 = en(haut.t);
      for (auto& e2 : elements) if (e2.id == el.id) e2.p2 = n1;
      const int morceau = ajouterLigne(n2, ancien2, el.construction);
      commeOriginal(morceau);
      contraindre(TypeContrainte::Colineaire, {el.id, morceau});  // les deux morceaux restent alignés
      lierA(n1, bas.par);
      lierA(n2, haut.par);
    } else if (aHaut) {
      const int n = en(haut.t);
      for (auto& e2 : elements) if (e2.id == el.id) e2.p1 = n;
      lierA(n, haut.par);
      retirerSiOrphelin(ancien1);
    } else {
      const int n = en(bas.t);
      for (auto& e2 : elements) if (e2.id == el.id) e2.p2 = n;
      lierA(n, bas.par);
      retirerSiOrphelin(ancien2);
    }
    return true;
  }
  // Cercle ou arc : angles
  const double r = rayonDe(el);
  const double ac = std::atan2(y - c0.y, x - c0.x);
  auto ramener = [&](double t, double ref) { while (t < ref) t += 2 * std::numbers::pi; while (t >= ref + 2 * std::numbers::pi) t -= 2 * std::numbers::pi; return t; };
  auto surCercle = [&](double t) { return ajouterPoint(c0.x + r * std::cos(t), c0.y + r * std::sin(t)); };
  if (el.type == TypeElement::Cercle) {
    if (coupes.size() < 2) { supprimer(el.id); return true; }
    // Le morceau cliqué va de l'intersection juste avant à celle juste après, en tournant : on garde le reste.
    Coupe apres{1e9, 0}, avant{-1e9, 0};
    for (const auto& k : coupes) {
      const double d = ramener(k.t, ac) - ac;
      if (d > 1e-9 && d < apres.t) apres = {d, k.par};
      const double g = d - 2 * std::numbers::pi;
      if (g < -1e-9 && g > avant.t) avant = {g, k.par};
    }
    const int p1 = surCercle(ac + apres.t), p2 = surCercle(ac + avant.t + 2 * std::numbers::pi);
    for (auto& e2 : elements)
      if (e2.id == el.id) { e2.type = TypeElement::Arc; e2.p1 = p1; e2.p2 = p2; }
    lierA(p1, apres.par);
    lierA(p2, avant.par);
    return true;
  }
  const double a0 = angleDe(c0, P(el.p1));
  double a1 = angleDe(c0, P(el.p2));
  while (a1 <= a0) a1 += 2 * std::numbers::pi;
  const double tc = ramener(ac, a0);
  Coupe bas{a0, 0}, haut{a1, 0};
  bool aBas = false, aHaut = false;
  for (const auto& k : coupes) {
    const double u = ramener(k.t, a0);
    if (u > a0 + 1e-6 && u < tc && u > bas.t) { bas = {u, k.par}; aBas = true; }
    if (u < a1 - 1e-6 && u > tc && u < haut.t) { haut = {u, k.par}; aHaut = true; }
  }
  const int ancien1 = el.p1, ancien2 = el.p2;
  if (!aBas && !aHaut) {
    supprimer(el.id);
    retirerSiOrphelin(ancien1);
    retirerSiOrphelin(ancien2);
    retirerSiOrphelin(el.centre);
    return true;
  }
  if (aBas && aHaut) {
    const int n1 = surCercle(bas.t), n2 = surCercle(haut.t);
    for (auto& e2 : elements) if (e2.id == el.id) e2.p2 = n1;
    const int morceau = ajouterArc(el.centre, n2, ancien2);
    commeOriginal(morceau);
    contraindre(TypeContrainte::Egal, {el.id, morceau});  // même centre, même rayon
    lierA(n1, bas.par);
    lierA(n2, haut.par);
  } else if (aHaut) {
    const int n = surCercle(haut.t);
    for (auto& e2 : elements) if (e2.id == el.id) e2.p1 = n;
    lierA(n, haut.par);
    retirerSiOrphelin(ancien1);
  } else {
    const int n = surCercle(bas.t);
    for (auto& e2 : elements) if (e2.id == el.id) e2.p2 = n;
    lierA(n, bas.par);
    retirerSiOrphelin(ancien2);
  }
  return true;
}

bool Esquisse::prolonger(int id, double x, double y) {
  const ElementEsquisse* el = element(id);
  if (!el || el->type != TypeElement::Ligne || !elementValide(*el)) return false;
  const Point a{point(el->p1)->x, point(el->p1)->y}, b{point(el->p2)->x, point(el->p2)->y};
  const bool finB = std::hypot(x - b.x, y - b.y) <= std::hypot(x - a.x, y - a.y);
  const int extremite = finB ? el->p2 : el->p1;
  // Une extrémité partagée (coin d'un contour) ne se prolonge pas : le coin s'ouvrirait (ES-08)
  for (const auto& e2 : elements)
    if (e2.id != id && (e2.p1 == extremite || e2.p2 == extremite || e2.centre == extremite)) return false;
  // Demi-droite depuis l'extrémité choisie, vers l'extérieur
  ElementEsquisse rayon = *el;
  const Point depart = finB ? b : a, dir{finB ? b.x - a.x : a.x - b.x, finB ? b.y - a.y : a.y - b.y};
  const double l = std::hypot(dir.x, dir.y);
  if (l < 1e-12) return false;
  Esquisse tmp = *this;
  const int q1 = tmp.ajouterPoint(depart.x, depart.y), q2 = tmp.ajouterPoint(depart.x + dir.x / l * 1e6, depart.y + dir.y / l * 1e6);
  rayon.p1 = q1;
  rayon.p2 = q2;
  double meilleur = 1e300;
  Point cible{0, 0};
  int vise = 0;
  for (const auto& autre : elements) {
    if (autre.id == id) continue;
    for (const Point& q : croisementsBruts(tmp, rayon, autre)) {
      const double d = std::hypot(q.x - depart.x, q.y - depart.y);
      if (d > 1e-6 && d < meilleur) { meilleur = d; cible = q; vise = autre.id; }
    }
  }
  if (meilleur > 1e299) return false;
  for (auto& p : points) if (p.id == extremite) { p.x = cible.x; p.y = cible.y; }
  // La ligne s'attache à ce qu'elle atteint (une extrémité, sinon un point sur l'élément) et ses cotes suivent
  // sa nouvelle longueur, au lieu de la ramener (ES-08)
  int surExtremite = -1;
  if (const ElementEsquisse* c = element(vise); c && c->type != TypeElement::Cercle)
    for (int pid : {c->p1, c->p2})
      if (const PointEsquisse* p = point(pid); p && std::hypot(p->x - cible.x, p->y - cible.y) < 1e-9) surExtremite = pid;
  if (surExtremite >= 0) contraindre(TypeContrainte::Coincidence, {extremite, surExtremite});
  else if (vise > 0) contraindre(TypeContrainte::PointSur, {extremite, vise});
  for (auto& c : contraintes) {
    const bool surLigne = c.refs.size() == 1 && c.refs[0] == id;
    const bool entreBouts = c.refs.size() == 2 && ((c.refs[0] == el->p1 && c.refs[1] == el->p2) || (c.refs[0] == el->p2 && c.refs[1] == el->p1));
    if ((c.type == TypeContrainte::Longueur && surLigne) ||
        ((c.type == TypeContrainte::DistanceX || c.type == TypeContrainte::DistanceY) && (surLigne || entreBouts)) || (c.type == TypeContrainte::Distance && entreBouts)) {
      const double v = mesurer(*this, c);
      c.valeur = c.type == TypeContrainte::DistanceX || c.type == TypeContrainte::DistanceY ? v : std::abs(v);
    }
  }
  return true;
}

std::vector<int> Esquisse::decaler(const std::vector<int>& ids, double d) {
  std::vector<int> crees;
  std::vector<int> chaine;
  for (int id : ids) {
    const ElementEsquisse* el = element(id);
    if (!el || !elementValide(*el)) continue;
    if (el->type == TypeElement::Cercle) {
      const bool construction = el->construction;
      const double r = std::max(1e-3, el->rayon + d);
      const PointEsquisse* c = point(el->centre);
      const double cx = c->x, cy = c->y;
      crees.push_back(ajouterCercle(ajouterPoint(cx, cy), r, construction));  // garde sa nature (ES-28)
      continue;
    }
    chaine.push_back(id);
  }
  if (chaine.empty()) return crees;
  // Plusieurs contours sélectionnés ensemble : chacun est décalé pour lui-même (ES-29)
  {
    std::vector<std::vector<int>> groupes;
    std::set<int> vus;
    for (int id : chaine) {
      if (vus.count(id)) continue;
      std::vector<int> g{id};
      vus.insert(id);
      for (std::size_t k = 0; k < g.size(); ++k) {
        const ElementEsquisse* a = element(g[k]);
        for (int j : chaine) {
          if (vus.count(j)) continue;
          const ElementEsquisse* b = element(j);
          if (a->p1 == b->p1 || a->p1 == b->p2 || a->p2 == b->p1 || a->p2 == b->p2) { g.push_back(j); vus.insert(j); }
        }
      }
      groupes.push_back(std::move(g));
    }
    if (groupes.size() > 1) {
      for (const auto& g : groupes) {
        const auto r = decaler(g, d);
        crees.insert(crees.end(), r.begin(), r.end());
      }
      return crees;
    }
  }
  // Mise en ordre de la chaîne : chaque élément partage une extrémité avec le suivant ; sens de parcours de chacun.
  struct Maillon { int id; bool direct; };  // direct : parcouru de p1 vers p2
  std::vector<Maillon> ordre;
  std::set<int> restants(chaine.begin(), chaine.end());
  auto bouts = [&](int id) { const ElementEsquisse* el = element(id); return std::pair<int, int>{el->p1, el->p2}; };
  // Départ : un élément dont une extrémité n'est partagée par aucun autre de la chaîne (chaîne ouverte), sinon le premier
  int depart = chaine.front();
  bool sensDepart = true;
  for (int id : chaine) {
    const auto [p1, p2] = bouts(id);
    int n1 = 0, n2 = 0;
    for (int j : chaine) if (j != id) { const auto [q1, q2] = bouts(j); n1 += (q1 == p1 || q2 == p1); n2 += (q1 == p2 || q2 == p2); }
    if (n1 == 0) { depart = id; sensDepart = true; break; }
    if (n2 == 0) { depart = id; sensDepart = false; break; }
  }
  ordre.push_back({depart, sensDepart});
  restants.erase(depart);
  while (!restants.empty()) {
    const auto [p1, p2] = bouts(ordre.back().id);
    const int fin = ordre.back().direct ? p2 : p1;
    bool trouve = false;
    for (int j : restants) {
      const auto [q1, q2] = bouts(j);
      if (q1 == fin || q2 == fin) { ordre.push_back({j, q1 == fin}); restants.erase(j); trouve = true; break; }
    }
    if (!trouve) break;
  }
  auto P = [&](int pid) { const PointEsquisse* p = point(pid); return Point{p->x, p->y}; };
  const auto [f1, f2] = bouts(ordre.back().id);
  const auto [d1, d2] = bouts(ordre.front().id);
  const bool fermee = ordre.size() > 1 && (ordre.back().direct ? f2 : f1) == (ordre.front().direct ? d1 : d2);
  // Sens : pour une chaîne fermée, distance positive vers l'extérieur ; décalage calculé à gauche du parcours.
  double gauche = -d;
  if (fermee) {
    double aire = 0;
    for (const auto& m : ordre) {
      const auto [p1, p2] = bouts(m.id);
      const Point a = P(m.direct ? p1 : p2), b = P(m.direct ? p2 : p1);
      aire += a.x * b.y - b.x * a.y;
      // Aire vraie : un arc ajoute (ou retire, parcouru à rebours) son segment circulaire au polygone des
      // cordes ; sans lui, un profil en D a une aire nulle et le décalage part du mauvais côté (ES-10)
      const ElementEsquisse* el = element(m.id);
      if (el->type == TypeElement::Arc) {
        const Point c = P(el->centre), q1 = P(p1), q2 = P(p2);
        double phi = std::atan2(q2.y - c.y, q2.x - c.x) - std::atan2(q1.y - c.y, q1.x - c.x);
        while (phi <= 0) phi += 2 * std::numbers::pi;
        const double r = rayonDe(*el);
        aire += (m.direct ? 1.0 : -1.0) * r * r * (phi - std::sin(phi));
      }
    }
    gauche = aire > 0 ? -d : d;  // parcours direct : l'intérieur est à gauche
  }
  // Géométrie décalée de chaque maillon (ligne : deux points ; arc : centre et rayon)
  struct Decale { bool ligne; Point a, b, c; double r; };
  std::vector<Decale> dec;
  for (const auto& m : ordre) {
    const ElementEsquisse* el = element(m.id);
    const auto [p1, p2] = bouts(m.id);
    const Point a = P(m.direct ? p1 : p2), b = P(m.direct ? p2 : p1);
    if (el->type == TypeElement::Ligne) {
      const double l = std::max(1e-12, std::hypot(b.x - a.x, b.y - a.y));
      const Point n{-(b.y - a.y) / l * gauche, (b.x - a.x) / l * gauche};
      dec.push_back({true, {a.x + n.x, a.y + n.y}, {b.x + n.x, b.y + n.y}, {0, 0}, 0});
    } else {
      // Arc parcouru dans son sens direct (p1 vers p2) : la gauche est vers le centre
      const double r = rayonDe(*el) + (m.direct ? -gauche : gauche);
      dec.push_back({false, a, b, P(el->centre), std::max(1e-3, r)});
    }
  }
  // Jonctions : intersection des géométries voisines, la plus proche du sommet d'origine
  // Toujours entre les géométries prolongées (droites infinies, cercles entiers) ; une quasi-tangence compte comme
  // tangence ; à défaut, le sommet décalé, jamais le sommet d'origine (ES-09)
  auto droiteCercle = [](const Point& p, const Point& q, const Point& c, double r, std::vector<Point>& out) {
    const double dx = q.x - p.x, dy = q.y - p.y, A = dx * dx + dy * dy;
    if (A < 1e-24) return;
    const double fx = p.x - c.x, fy = p.y - c.y, B = 2 * (fx * dx + fy * dy), C = fx * fx + fy * fy - r * r;
    double disc = B * B - 4 * A * C;
    if (disc < 0 && disc > -1e-9 * B * B - 1e-12) disc = 0;
    if (disc < 0) return;
    const double sq = std::sqrt(disc);
    for (double t : {(-B - sq) / (2 * A), (-B + sq) / (2 * A)}) out.push_back({p.x + t * dx, p.y + t * dy});
  };
  auto cercleCercle = [](const Point& c0, double r0, const Point& c1, double r1, std::vector<Point>& out) {
    const double d = std::hypot(c1.x - c0.x, c1.y - c0.y);
    if (d < 1e-12) return;
    const double a = (r0 * r0 - r1 * r1 + d * d) / (2 * d);
    double h2 = r0 * r0 - a * a;
    if (h2 < 0 && h2 > -1e-9 * r0 * r0) h2 = 0;
    if (h2 < 0) return;
    const double h = std::sqrt(h2), ux = (c1.x - c0.x) / d, uy = (c1.y - c0.y) / d;
    const Point m{c0.x + a * ux, c0.y + a * uy};
    out.push_back({m.x - h * uy, m.y + h * ux});
    out.push_back({m.x + h * uy, m.y - h * ux});
  };
  auto jonction = [&](size_t i, size_t j, const Point& sommet) {
    const Decale& x = dec[i];
    const Decale& y = dec[j];
    std::vector<Point> cand;
    if (x.ligne && y.ligne) {
      const Point p = x.a, q = x.b, u = y.a, v = y.b;
      const double den = (q.x - p.x) * (v.y - u.y) - (q.y - p.y) * (v.x - u.x);
      if (std::abs(den) > 1e-12) {
        const double t = ((u.x - p.x) * (v.y - u.y) - (u.y - p.y) * (v.x - u.x)) / den;
        cand.push_back({p.x + t * (q.x - p.x), p.y + t * (q.y - p.y)});
      }
    } else if (x.ligne) droiteCercle(x.a, x.b, y.c, y.r, cand);
    else if (y.ligne) droiteCercle(y.a, y.b, x.c, x.r, cand);
    else cercleCercle(x.c, x.r, y.c, y.r, cand);
    if (cand.empty()) {
      if (x.ligne) return x.b;
      if (y.ligne) return y.a;
      const double l = std::max(1e-12, std::hypot(sommet.x - x.c.x, sommet.y - x.c.y));
      return Point{x.c.x + (sommet.x - x.c.x) / l * x.r, x.c.y + (sommet.y - x.c.y) / l * x.r};
    }
    Point best = cand.front();
    for (const auto& q : cand) if (std::hypot(q.x - sommet.x, q.y - sommet.y) < std::hypot(best.x - sommet.x, best.y - sommet.y)) best = q;
    return best;
  };
  const size_t n = dec.size();
  std::vector<int> pts(n + 1, 0);
  for (size_t k = 0; k <= n; ++k) {
    Point q;
    if (k == 0 || k == n) {
      if (fermee) {
        const auto [p1, p2] = bouts(ordre.front().id);
        q = jonction(n - 1, 0, P(ordre.front().direct ? p1 : p2));
      } else if (k == 0) {
        const Decale& x = dec.front();
        if (x.ligne) q = x.a;
        else { const double ang = std::atan2(x.a.y - x.c.y, x.a.x - x.c.x); q = {x.c.x + x.r * std::cos(ang), x.c.y + x.r * std::sin(ang)}; }
      } else {
        const Decale& x = dec.back();
        if (x.ligne) q = x.b;
        else { const double ang = std::atan2(x.b.y - x.c.y, x.b.x - x.c.x); q = {x.c.x + x.r * std::cos(ang), x.c.y + x.r * std::sin(ang)}; }
      }
    } else {
      const auto [p1, p2] = bouts(ordre[k].id);
      q = jonction(k - 1, k, P(ordre[k].direct ? p1 : p2));
    }
    if (k == n && fermee) { pts[k] = pts[0]; continue; }
    pts[k] = ajouterPoint(q.x, q.y);
  }
  for (size_t k = 0; k < n; ++k) {
    const ElementEsquisse* el = element(ordre[k].id);
    if (dec[k].ligne) crees.push_back(ajouterLigne(pts[k], pts[k + 1], el->construction));
    else {
      const int c = ajouterPoint(dec[k].c.x, dec[k].c.y);
      crees.push_back(ordre[k].direct ? ajouterArc(c, pts[k], pts[k + 1], el->construction) : ajouterArc(c, pts[k + 1], pts[k], el->construction));
    }
  }
  return crees;
}

std::vector<int> Esquisse::symetriser(const std::vector<int>& ids, int axe) {
  Point a{0, 0}, b{1, 0};
  if (axe == AXE_V) b = {0, 1};
  else if (axe != AXE_H) {
    const ElementEsquisse* l = element(axe);
    if (!l || l->type != TypeElement::Ligne) return {};
    const PointEsquisse *pa = point(l->p1), *pb = point(l->p2);
    a = {pa->x, pa->y};
    b = {pb->x, pb->y};
  }
  const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
  if (l2 < 1e-18) return {};
  auto miroir = [&](Point p) {
    const double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2;
    const Point h{a.x + t * dx, a.y + t * dy};
    return Point{2 * h.x - p.x, 2 * h.y - p.y};
  };
  std::vector<int> elts;
  for (int id : ids) if (id != axe) elts.push_back(id);
  std::map<int, int> corr;
  // Un point de l'original posé sur l'axe n'est pas dédoublé : la copie le reprend, et il est tenu sur l'axe
  // (un demi-profil symétrisé forme un seul contour fermé, EI-13)
  {
    const double l = std::sqrt(l2);
    std::set<int> surAxe;
    for (int id : elts)
      if (const ElementEsquisse* el = element(id))
        for (int p : {el->p1, el->p2, el->centre})
          if (const PointEsquisse* q = p >= 0 ? point(p) : nullptr; q && std::abs(cross(dx, dy, q->x - a.x, q->y - a.y)) / l < 1e-7 * std::max(1.0, l)) surAxe.insert(p);
    for (int p : surAxe) {
      corr[p] = p;
      if (p == 0) continue;
      const bool tenu = std::any_of(contraintes.begin(), contraintes.end(), [&](const ContrainteEsquisse& k) {
        return (k.type == TypeContrainte::PointSur && k.refs.size() == 2 && k.refs[0] == p && k.refs[1] == axe) || (k.type == TypeContrainte::Coincidence && std::find(k.refs.begin(), k.refs.end(), p) != k.refs.end());
      });
      if (!tenu) contraindre(TypeContrainte::PointSur, {p, axe});
    }
  }
  const auto crees = copier(*this, elts, miroir, true, corr);
  for (const auto& [o, c] : corr)
    if (o != c) contraindre(TypeContrainte::Symetrie, {o, c, axe});
  for (size_t i = 0; i < elts.size() && i < crees.size(); ++i)
    if (const ElementEsquisse* el = element(elts[i]); el && el->type == TypeElement::Cercle) contraindre(TypeContrainte::Egal, {elts[i], crees[i]});
  return crees;
}

std::vector<int> Esquisse::repeterLineaire(const std::vector<int>& ids, double dx, double dy, int nombre) {
  std::vector<int> tous;
  for (int k = 1; k < std::max(1, nombre); ++k) {
    std::map<int, int> corr;
    const auto crees = copier(*this, ids, [&](Point p) { return Point{p.x + k * dx, p.y + k * dy}; }, false, corr);
    for (const auto& [o, c] : corr) {
      if (o == c) continue;
      contraindre(TypeContrainte::DistanceX, {o, c}, k * dx);
      contraindre(TypeContrainte::DistanceY, {o, c}, k * dy);
    }
    for (size_t i = 0; i < ids.size() && i < crees.size(); ++i)
      if (const ElementEsquisse* el = element(ids[i]); el && el->type == TypeElement::Cercle) contraindre(TypeContrainte::Egal, {ids[i], crees[i]});
    tous.insert(tous.end(), crees.begin(), crees.end());
  }
  return tous;
}

std::vector<int> Esquisse::repeterCirculaire(const std::vector<int>& ids, double cx, double cy, int nombre, double angle) {
  std::vector<int> tous;
  const int n = std::max(1, nombre);
  const double pas = (std::abs(angle) >= 359.999 ? 360.0 / n : angle / std::max(1, n - 1)) * std::numbers::pi / 180;
  // Les copies suivent l'original (ES-12) : un point centre (l'origine, ou un point fixé), et pour chaque point
  // copié un rayon de construction égal à celui de l'original, tourné de k fois le pas
  int centre = 0;
  if (std::abs(cx) > 1e-12 || std::abs(cy) > 1e-12) {
    centre = ajouterPoint(cx, cy);
    contraindre(TypeContrainte::Fixe, {centre}, cx, cy);
  }
  std::map<int, int> rayonOriginal;  // point d'origine → rayon de construction depuis le centre
  auto rayonDepuisCentre = [&](int p) { return ajouterLigne(centre, p, true); };
  for (int k = 1; k < n; ++k) {
    const double c = std::cos(k * pas), s = std::sin(k * pas);
    std::map<int, int> corr;
    const auto crees = copier(*this, ids, [&](Point p) { return Point{cx + (p.x - cx) * c - (p.y - cy) * s, cy + (p.x - cx) * s + (p.y - cy) * c}; }, false, corr);
    double angle = std::fmod(k * pas * 180 / std::numbers::pi, 360.0);
    if (angle > 180) angle -= 360;
    if (angle <= -180) angle += 360;
    for (const auto& [o, q] : corr) {
      if (o == q || o == centre) continue;
      const PointEsquisse* po = point(o);
      if (!po || std::hypot(po->x - cx, po->y - cy) < 1e-9) {
        if (o != q) contraindre(TypeContrainte::Coincidence, {q, centre});  // point au centre : il y reste
        continue;
      }
      if (!rayonOriginal.count(o)) rayonOriginal[o] = rayonDepuisCentre(o);
      const int rq = rayonDepuisCentre(q);
      contraindre(TypeContrainte::Egal, {rayonOriginal[o], rq});
      contraindre(TypeContrainte::Angle, {rayonOriginal[o], rq}, angle);
    }
    for (size_t i = 0; i < ids.size() && i < crees.size(); ++i) {
      const ElementEsquisse* el = element(ids[i]);
      if (el && el->type == TypeElement::Cercle) contraindre(TypeContrainte::Egal, {ids[i], crees[i]});
    }
    tous.insert(tous.end(), crees.begin(), crees.end());
  }
  return tous;
}

int Esquisse::ajouterArc3Points(int debut, int fin, double xp, double yp) {
  const PointEsquisse* a = point(debut);
  const PointEsquisse* b = point(fin);
  if (!a || !b) return 0;
  // Centre du cercle passant par les trois points.
  const double ax = a->x, ay = a->y, bx = b->x, by = b->y;
  const double d = 2 * (ax * (by - yp) + bx * (yp - ay) + xp * (ay - by));
  if (std::abs(d) < 1e-12) return 0;
  const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, p2 = xp * xp + yp * yp;
  const double cx = (a2 * (by - yp) + b2 * (yp - ay) + p2 * (ay - by)) / d, cy = (a2 * (xp - bx) + b2 * (ax - xp) + p2 * (bx - ax)) / d;
  const int c = ajouterPoint(cx, cy);
  // L'arc est parcouru dans le sens direct du début à la fin : on échange si le point de passage est de l'autre côté.
  const double t0 = std::atan2(ay - cy, ax - cx);
  double t1 = std::atan2(by - cy, bx - cx), tp = std::atan2(yp - cy, xp - cx);
  while (t1 <= t0) t1 += 2 * PI;
  while (tp <= t0) tp += 2 * PI;
  return tp < t1 ? ajouterArc(c, debut, fin) : ajouterArc(c, fin, debut);
}

std::vector<int> Esquisse::ajouterRainure(double x1, double y1, double x2, double y2, double r) {
  const double L = std::hypot(x2 - x1, y2 - y1);
  if (L < 1e-9 || r <= 0) return {};
  const double nx = -(y2 - y1) / L * r, ny = (x2 - x1) / L * r;
  const int c1 = ajouterPoint(x1, y1), c2 = ajouterPoint(x2, y2);
  const int a1 = ajouterPoint(x1 + nx, y1 + ny), a2 = ajouterPoint(x2 + nx, y2 + ny), b1 = ajouterPoint(x1 - nx, y1 - ny), b2 = ajouterPoint(x2 - nx, y2 - ny);
  const int haut = ajouterLigne(a1, a2), bas = ajouterLigne(b2, b1);
  const int arc2 = ajouterArc(c2, b2, a2), arc1 = ajouterArc(c1, a1, b1);
  contraindre(TypeContrainte::Tangente, {haut, arc1});
  contraindre(TypeContrainte::Tangente, {haut, arc2});
  contraindre(TypeContrainte::Tangente, {bas, arc1});
  contraindre(TypeContrainte::Tangente, {bas, arc2});
  contraindre(TypeContrainte::Egal, {arc1, arc2});
  // Pas de parallèle : les quatre tangences et l'égalité des rayons l'imposent déjà (ES-02)
  return {haut, arc2, bas, arc1};
}

std::vector<int> Esquisse::ajouterPolygone(double cx, double cy, double sx, double sy, int n) {
  n = std::clamp(n, 3, 64);
  const double r = std::hypot(sx - cx, sy - cy), t0 = std::atan2(sy - cy, sx - cx);
  if (r < 1e-9) return {};
  const int c = ajouterPoint(cx, cy);
  const int cercle = ajouterCercle(c, r, true);
  std::vector<int> sommets, cotes;
  for (int i = 0; i < n; ++i) sommets.push_back(ajouterPoint(cx + r * std::cos(t0 + 2 * PI * i / n), cy + r * std::sin(t0 + 2 * PI * i / n)));
  for (int i = 0; i < n; ++i) cotes.push_back(ajouterLigne(sommets[static_cast<size_t>(i)], sommets[static_cast<size_t>((i + 1) % n)]));
  for (int s : sommets) contraindre(TypeContrainte::PointSur, {s, cercle});
  for (int i = 1; i < n; ++i) contraindre(TypeContrainte::Egal, {cotes[0], cotes[static_cast<size_t>(i)]});
  return cotes;
}


bool Esquisse::congeEsquisse(int pid, double r, std::string* erreur, double cx, double cy) {
  auto echec = [&](const char* m) { if (erreur) *erreur = m; return false; };
  std::vector<ElementEsquisse*> lignes = lignesDuCoin(*this, pid, cx, cy);
  if (lignes.size() != 2) return echec("Il faut deux lignes qui se rejoignent en ce point (les lignes de construction ne comptent pas) ; s'il en part davantage, clique dans l'angle à arrondir.");
  const PointEsquisse* P = point(pid);
  const int idL1 = lignes[0]->id, idL2 = lignes[1]->id;
  const int autre1 = lignes[0]->p1 == pid ? lignes[0]->p2 : lignes[0]->p1, autre2 = lignes[1]->p1 == pid ? lignes[1]->p2 : lignes[1]->p1;
  const PointEsquisse* A = point(autre1);
  const PointEsquisse* B = point(autre2);
  const double l1 = std::hypot(A->x - P->x, A->y - P->y), l2 = std::hypot(B->x - P->x, B->y - P->y);
  if (l1 < 1e-9 || l2 < 1e-9) return echec("Lignes de longueur nulle.");
  const double u1x = (A->x - P->x) / l1, u1y = (A->y - P->y) / l1, u2x = (B->x - P->x) / l2, u2y = (B->y - P->y) / l2;
  const double theta = std::acos(std::clamp(u1x * u2x + u1y * u2y, -1.0, 1.0));
  if (theta < 1e-3 || theta > PI - 1e-3) return echec("Les deux lignes sont alignées : pas d'angle à arrondir.");
  const double t = r / std::tan(theta / 2);
  if (t >= l1 || t >= l2) return echec("Rayon trop grand pour la longueur des lignes.");
  const double bx = u1x + u2x, by = u1y + u2y, bl = std::hypot(bx, by);
  const double dc = r / std::sin(theta / 2);
  const double px = P->x, py = P->y;
  const int t1 = ajouterPoint(px + u1x * t, py + u1y * t), t2 = ajouterPoint(px + u2x * t, py + u2y * t);
  const int c = ajouterPoint(px + bx / bl * dc, py + by / bl * dc);
  // Les lignes s'arrêtent aux points de tangence ; l'ancien sommet disparaît s'il ne sert plus.
  for (auto& el : elements) {
    if (el.id == idL1) { if (el.p1 == pid) el.p1 = t1; else el.p2 = t1; }
    if (el.id == idL2) { if (el.p1 == pid) el.p1 = t2; else el.p2 = t2; }
  }
  // Sens direct de t1 vers t2 autour du centre, sinon l'inverse.
  const PointEsquisse* C = point(c);
  const PointEsquisse* T1 = point(t1);
  const PointEsquisse* T2 = point(t2);
  const double a1 = std::atan2(T1->y - C->y, T1->x - C->x);
  double a2 = std::atan2(T2->y - C->y, T2->x - C->x);
  while (a2 <= a1) a2 += 2 * PI;
  const int arc = (a2 - a1) < PI ? ajouterArc(c, t1, t2) : ajouterArc(c, t2, t1);
  contraindre(TypeContrainte::Tangente, {idL1, arc});
  contraindre(TypeContrainte::Tangente, {idL2, arc});
  contraindre(TypeContrainte::Rayon, {arc}, r);
  // L'ancien sommet reste comme « coin virtuel », à l'intersection des deux lignes prolongées (comme dans
  // SolidWorks) : les cotes qui mesuraient les lignes jusqu'au coin le mesurent toujours.
  contraindre(TypeContrainte::PointSur, {pid, idL1});
  contraindre(TypeContrainte::PointSur, {pid, idL2});
  for (auto& k : contraintes)
    if (k.type == TypeContrainte::Longueur && k.refs.size() == 1 && (k.refs[0] == idL1 || k.refs[0] == idL2)) {
      const int loin = k.refs[0] == idL1 ? autre1 : autre2;
      k.type = TypeContrainte::Distance;
      k.refs = {loin, pid};
    }
  return true;
}

const char* nomContrainte(TypeContrainte t) {
  for (const auto& c : CLES) if (c.t == t) return c.nom;
  return "?";
}

Esquisse::Esquisse() { points.push_back({0, 0, 0}); }

int Esquisse::ajouterPoint(double x, double y) { points.push_back({prochainId, x, y}); return prochainId++; }

bool Esquisse::elementValide(const ElementEsquisse& e) const {
  switch (e.type) {
    case TypeElement::Ligne: return point(e.p1) && point(e.p2);
    case TypeElement::Cercle: return point(e.centre) != nullptr;
    case TypeElement::Arc: return point(e.centre) && point(e.p1) && point(e.p2);
  }
  return false;
}

// Les ajouts refusent une référence de point absente (rendent 0) : aucun élément ne vise un point qui n'existe pas
int Esquisse::ajouterLigne(int a, int b, bool cons) {
  if (!point(a) || !point(b)) return 0;
  ElementEsquisse e;
  e.id = prochainId++; e.type = TypeElement::Ligne; e.p1 = a; e.p2 = b; e.construction = cons;
  elements.push_back(e);
  return e.id;
}

int Esquisse::ajouterCercle(int c, double r, bool cons) {
  if (!point(c)) return 0;
  ElementEsquisse e;
  e.id = prochainId++; e.type = TypeElement::Cercle; e.centre = c; e.rayon = r; e.construction = cons;
  elements.push_back(e);
  return e.id;
}

int Esquisse::ajouterArc(int c, int a, int b, bool cons) {
  if (!point(c) || !point(a) || !point(b)) return 0;
  ElementEsquisse e;
  e.id = prochainId++; e.type = TypeElement::Arc; e.centre = c; e.p1 = a; e.p2 = b; e.construction = cons;
  elements.push_back(e);
  return e.id;
}

int Esquisse::contraindre(TypeContrainte t, std::vector<int> refs, double valeur, double valeur2) {
  ContrainteEsquisse c;
  c.id = prochainId++; c.type = t; c.refs = std::move(refs); c.valeur = valeur; c.valeur2 = valeur2;
  contraintes.push_back(c);
  return c.id;
}

std::vector<int> Esquisse::ajouterRectangle(double x1, double y1, double x2, double y2) {
  const int a = ajouterPoint(x1, y1), b = ajouterPoint(x2, y1), c = ajouterPoint(x2, y2), d = ajouterPoint(x1, y2);
  const std::vector<int> l = {ajouterLigne(a, b), ajouterLigne(b, c), ajouterLigne(c, d), ajouterLigne(d, a)};
  contraindre(TypeContrainte::Horizontal, {l[0]});
  contraindre(TypeContrainte::Vertical, {l[1]});
  contraindre(TypeContrainte::Horizontal, {l[2]});
  contraindre(TypeContrainte::Vertical, {l[3]});
  return l;
}

PointEsquisse* Esquisse::point(int id) { for (auto& p : points) if (p.id == id) return &p; return nullptr; }
const PointEsquisse* Esquisse::point(int id) const { for (const auto& p : points) if (p.id == id) return &p; return nullptr; }
ElementEsquisse* Esquisse::element(int id) { for (auto& e : elements) if (e.id == id) return &e; return nullptr; }
const ElementEsquisse* Esquisse::element(int id) const { for (const auto& e : elements) if (e.id == id) return &e; return nullptr; }

double Esquisse::rayonDe(const ElementEsquisse& e) const {
  if (e.type == TypeElement::Cercle) return e.rayon;
  const PointEsquisse* c = point(e.centre);
  const PointEsquisse* a = point(e.p1);
  return (c && a) ? std::hypot(a->x - c->x, a->y - c->y) : 0;
}

void Esquisse::supprimer(int id) {
  if (id == 0) return;
  std::set<int> morts = {id};
  // Un point supprimé emporte les éléments qui l'utilisent ; on recommence jusqu'à stabilité.
  bool change = true;
  while (change) {
    change = false;
    for (const auto& e : elements)
      if (!morts.count(e.id) && (morts.count(e.p1) || morts.count(e.p2) || morts.count(e.centre))) { morts.insert(e.id); change = true; }
  }
  // Points devenus orphelins (plus utilisés par aucun élément ni contrainte restante) : supprimés eux aussi s'ils
  // appartenaient à un élément supprimé.
  std::set<int> pointsDesMorts;
  for (const auto& e : elements)
    if (morts.count(e.id)) for (int p : {e.p1, e.p2, e.centre}) if (p > 0) pointsDesMorts.insert(p);
  elements.erase(std::remove_if(elements.begin(), elements.end(), [&](const ElementEsquisse& e) { return morts.count(e.id) > 0; }), elements.end());
  for (int p : pointsDesMorts) {
    bool utilise = false;
    for (const auto& e : elements) if (e.p1 == p || e.p2 == p || e.centre == p) utilise = true;
    if (!utilise) morts.insert(p);
  }
  points.erase(std::remove_if(points.begin(), points.end(), [&](const PointEsquisse& p) { return p.id != 0 && morts.count(p.id) > 0; }), points.end());
  contraintes.erase(std::remove_if(contraintes.begin(), contraintes.end(),
                                   [&](const ContrainteEsquisse& c) {
                                     if (morts.count(c.id)) return true;
                                     return std::any_of(c.refs.begin(), c.refs.end(), [&](int r) { return morts.count(r) > 0; });
                                   }),
                    contraintes.end());
  // Liens vers la pièce : retirés avec leur point (ou avec la contrainte Fixe qui le tenait)
  std::erase_if(liensExternes, [&](const LienExterne& l) {
    if (morts.count(l.point)) return true;
    return std::none_of(contraintes.begin(), contraintes.end(), [&](const ContrainteEsquisse& c) { return c.type == TypeContrainte::Fixe && !c.refs.empty() && c.refs[0] == l.point; });
  });
  for (auto& l : liensExternes) if (morts.count(l.cercle)) l.cercle = 0;
}

EtatEsquisse resoudre(Esquisse& e, const std::vector<std::pair<int, Point>>& glisser) {
  EtatEsquisse etat;
  Parametres P;
  P.construire(e);
  std::vector<double> x = P.lire(e);
  const std::vector<double> xDepart = x;  // dernière géométrie connue, rendue là où le système est insoluble
  const std::vector<Bloc> blocs = construireBlocs(e, P, x);
  const std::vector<Composante> comps = composantes(blocs, P.n);
  std::vector<int> composanteDe(static_cast<std::size_t>(P.n), -1);
  for (std::size_t k = 0; k < comps.size(); ++k)
    for (int g : comps[k].params) composanteDe[static_cast<std::size_t>(g)] = static_cast<int>(k);
  // Cibles des points tirés, par paramètre global
  std::vector<std::pair<int, double>> cibles;
  for (const auto& [id, cible] : glisser) {
    const auto ax = P.ix.find(id);
    if (ax == P.ix.end()) continue;
    cibles.emplace_back(ax->second, cible.x);
    cibles.emplace_back(P.iy.at(id), cible.y);
  }
  // Chaque composante se résout seule. Phase 1 : contraintes, plus un léger rappel de chaque paramètre vers sa valeur
  // de départ (la géométrie bouge le moins possible et ne s'effondre pas sur un point) et une attraction vers la
  // souris pour les points tirés ; on essaie ensuite d'amener exactement les points tirés sous la souris. Phase 2 :
  // contraintes seules, jusqu'à la précision voulue.
  std::vector<char> estRayon(static_cast<std::size_t>(P.n), 0);
  for (const auto& [id, ir] : P.ir) estRayon[static_cast<std::size_t>(ir)] = 1;
  double coutTotal = 0;
  std::vector<char> enEchec(comps.size(), 0);
  std::vector<double> F;
  for (std::size_t k = 0; k < comps.size(); ++k) {
    const Composante& c = comps[k];
    std::vector<ProblemeComposante::Cible> ciblesLocales;
    ProblemeComposante pb(e, P, x, blocs, c);
    for (const auto& [g, val] : cibles)
      if (composanteDe[static_cast<std::size_t>(g)] == static_cast<int>(k)) ciblesLocales.push_back({pb.local(g), val});
    if (c.blocs.empty()) {
      for (const auto& t : ciblesLocales) x[static_cast<std::size_t>(c.params[static_cast<std::size_t>(t.local)])] = t.valeur;
      continue;
    }
    std::vector<double> xl = pb.lire();
    pb.residus(xl, F);
    double cout = 0;
    for (double r : F) cout += r * r;
    if (ciblesLocales.empty() && cout <= 1e-22) { coutTotal += cout; continue; }  // satisfaite et immobile
    pb.glisser(0.01, xl, ciblesLocales, 0.05);
    numerique::levenbergMarquardt(pb, xl, {80});
    if (!ciblesLocales.empty()) {
      std::vector<double> essai = xl;
      pb.glisser(0, xl, ciblesLocales, 1.0);
      if (numerique::levenbergMarquardt(pb, essai, {80}) < 1e-18) xl = essai;
    }
    pb.contraintesSeules();
    cout = numerique::levenbergMarquardt(pb, xl, {200});
    pb.ecrire(xl);
    coutTotal += cout;
    // Un cercle de rayon négatif ou nul « satisfait » certaines équations sans être une géométrie : refusé (ES-20)
    bool rayonNegatif = false;
    for (int g : c.params)
      if (estRayon[static_cast<std::size_t>(g)] && x[static_cast<std::size_t>(g)] <= 1e-9) rayonNegatif = true;
    if (rayonNegatif) etat.message = "Contraintes incompatibles : elles demandent un cercle de rayon négatif ou nul.";
    if (std::sqrt(cout) >= 1e-6 || rayonNegatif) enEchec[k] = 1;
  }
  etat.residu = std::sqrt(coutTotal);
  etat.resolue = std::none_of(enEchec.begin(), enEchec.end(), [](char a) { return a != 0; });
  // Analyse à la solution trouvée : rang, contraintes redondantes ou en conflit, paramètres encore libres
  std::vector<int> origine, blocDeLigne;
  evaluerBlocs(e, P, blocs, x, F, &origine, &blocDeLigne);
  const numerique::AnalyseRang a = numerique::analyserRang(jacobienneGlobale(e, P, blocs, x), P.n);
  etat.degresLiberte = P.n - a.rang;
  auto echecLigne = [&](std::size_t i) {
    const Bloc& b = blocs[static_cast<std::size_t>(blocDeLigne[i])];
    return !b.params.empty() && enEchec[static_cast<std::size_t>(composanteDe[static_cast<std::size_t>(b.params.front())])] != 0;
  };
  std::set<int> conflit, redondantes;
  for (int l : a.lignesDependantes) {
    const std::size_t i = static_cast<std::size_t>(l);
    if (origine[i] == 0) continue;
    (echecLigne(i) ? conflit : redondantes).insert(origine[i]);
  }
  // En conflit : les contraintes dont le résidu reste non nul, plus celles qui leur sont liées
  for (std::size_t i = 0; i < F.size(); ++i)
    if (std::abs(F[i]) > 1e-6 && origine[i] != 0 && echecLigne(i)) conflit.insert(origine[i]);
  etat.enConflit.assign(conflit.begin(), conflit.end());
  etat.redondantes.assign(redondantes.begin(), redondantes.end());
  // Composante insoluble : aucun compromis n'est écrit, elle reste telle qu'avant (dernière solution valide)
  for (std::size_t k = 0; k < comps.size(); ++k)
    if (enEchec[k])
      for (int g : comps[k].params) x[static_cast<std::size_t>(g)] = xDepart[static_cast<std::size_t>(g)];
  P.ecrire(e, x);
  for (const auto& p : e.points) {
    if (p.id == 0) continue;
    if (a.parametreLibre[static_cast<std::size_t>(P.ix.at(p.id))] || a.parametreLibre[static_cast<std::size_t>(P.iy.at(p.id))]) etat.pointsLibres.push_back(p.id);
  }
  std::unordered_set<int> libres(etat.pointsLibres.begin(), etat.pointsLibres.end());
  for (const auto& el : e.elements) {
    bool l = libres.count(el.p1) || libres.count(el.p2) || libres.count(el.centre);
    if (el.type == TypeElement::Cercle) l = l || a.parametreLibre[static_cast<std::size_t>(P.ir.at(el.id))];
    if (l) etat.elementsLibres.push_back(el.id);
  }
  if (!etat.message.empty()) return etat;  // rayon négatif : message déjà posé
  if (!etat.enConflit.empty() || !etat.resolue) etat.message = "Contraintes incompatibles : retire l'une des contraintes en rouge.";
  else if (etat.degresLiberte == 0) etat.message = etat.redondantes.empty() ? "Esquisse entièrement contrainte." : "Esquisse entièrement contrainte, avec des contraintes redondantes.";
  else etat.message = std::to_string(etat.degresLiberte) + (etat.degresLiberte > 1 ? " degrés de liberté restants." : " degré de liberté restant.");
  return etat;
}

namespace {
std::string nomElement(const ElementEsquisse& el) {
  return (el.type == TypeElement::Ligne ? "Ligne " : el.type == TypeElement::Cercle ? "Cercle " : "Arc ") + std::to_string(el.id);
}
}  // namespace

DiagnosticEsquisse diagnostiquer(const Esquisse& e0) {
  DiagnosticEsquisse d;
  Esquisse e = e0;
  // Extrémités libres : extrémités de lignes et d'arcs (hors construction) qu'aucun autre élément ne rejoint
  {
    std::map<int, int> usages;
    for (const auto& el : e.elements)
      if (!el.construction && el.type != TypeElement::Cercle) { ++usages[el.p1]; ++usages[el.p2]; }
    std::unordered_map<int, const PointEsquisse*> parId;
    for (const auto& p : e.points) parId[p.id] = &p;
    auto pointDe = [&](int id) -> const PointEsquisse* { const auto it = parId.find(id); return it == parId.end() ? nullptr : it->second; };
    for (const auto& [pid, n] : usages) {
      if (n != 1) continue;
      const PointEsquisse* p = pointDe(pid);
      bool rejoint = false;
      for (const auto& [q, m] : usages) {
        if (q == pid) continue;
        const PointEsquisse* o = pointDe(q);
        if (p && o && std::hypot(p->x - o->x, p->y - o->y) < 1e-6) { rejoint = true; break; }
      }
      if (!rejoint) d.extremitesLibres.push_back(pid);
    }
  }
  Parametres P;
  P.construire(e);
  std::vector<double> x = P.lire(e);
  const int n = static_cast<int>(x.size());
  if (n == 0) return d;
  const std::vector<Bloc> blocs = construireBlocs(e, P, x);
  // Nombre de degrés de liberté : le même calcul de rang que la résolution, pour que les deux s'accordent à toute
  // échelle (ES-23). Le noyau de la jacobienne (directions dans lesquelles l'esquisse bouge sans violer ses
  // contraintes) vient de la même élimination creuse, sans décomposition propre dense (ES-37).
  const int ddlMaxDetail = 150;  // au-delà, le nombre seul : une liste de 150 libertés n'aide personne
  numerique::AnalyseRang a = numerique::analyserRang(jacobienneGlobale(e, P, blocs, x), n, true);
  d.degresLiberte = n - a.rang;
  if (d.degresLiberte > ddlMaxDetail) return d;
  std::vector<std::vector<double>> noyau = std::move(a.noyau);
  if (noyau.empty()) return d;
  auto produit = [](const std::vector<double>& a, const std::vector<double>& b) { double s = 0; for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i]; return s; };
  auto normer = [&](std::vector<double>& v) { const double l = std::sqrt(produit(v, v)); if (l > 1e-12) for (double& q : v) q /= l; return l; };
  // Base orthonormée du noyau (Gram-Schmidt modifié, deux passes)
  {
    std::vector<std::vector<double>> base;
    for (auto& v : noyau) {
      for (int passe = 0; passe < 2; ++passe)
        for (const auto& b : base) { const double c = produit(v, b); for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] -= c * b[static_cast<size_t>(i)]; }
      if (normer(v) > 1e-9) base.push_back(std::move(v));
    }
    noyau = std::move(base);
  }
  if (noyau.empty()) return d;
  // Tous les éléments qui ont des points (pour nommer ce qui bouge)
  auto elementsDe = [&](const std::set<int>& pts, const std::set<int>& rayons) {
    std::vector<int> r;
    for (const auto& el : e.elements) {
      const bool bouge = pts.count(el.p1) || pts.count(el.p2) || pts.count(el.centre) || rayons.count(el.id);
      if (bouge) r.push_back(el.id);
    }
    return r;
  };
  // Mouvements d'ensemble : translations horizontale et verticale, rotation autour de l'origine
  std::vector<double> tx(static_cast<size_t>(n), 0), ty(static_cast<size_t>(n), 0), rot(static_cast<size_t>(n), 0);
  for (const auto& p : e.points) {
    if (p.id == 0) continue;
    tx[static_cast<size_t>(P.ix.at(p.id))] = 1;
    ty[static_cast<size_t>(P.iy.at(p.id))] = 1;
    rot[static_cast<size_t>(P.ix.at(p.id))] = -p.y;
    rot[static_cast<size_t>(P.iy.at(p.id))] = p.x;
  }
  std::set<int> tousPoints;
  for (const auto& p : e.points) if (p.id) tousPoints.insert(p.id);
  const std::vector<int> tous = elementsDe(tousPoints, {});
  const struct { std::vector<double>* v; const char* desc; const char* sugg; } ensembles[] = {
      {&tx, "Déplacement horizontal de toute l'esquisse", "Relie un point à l'origine (coïncidente) ou cote sa distance horizontale à l'origine."},
      {&ty, "Déplacement vertical de toute l'esquisse", "Relie un point à l'origine (coïncidente) ou cote sa distance verticale à l'origine."},
      {&rot, "Rotation de toute l'esquisse autour de l'origine", "Rends une ligne horizontale ou verticale, ou cote un angle."}};
  for (const auto& g : ensembles) {
    std::vector<double> u = *g.v;
    if (normer(u) < 1e-12) continue;
    std::vector<double> proj(static_cast<size_t>(n), 0);
    for (const auto& v : noyau) { const double c = produit(u, v); for (int i = 0; i < n; ++i) proj[static_cast<size_t>(i)] += c * v[static_cast<size_t>(i)]; }
    if (std::sqrt(produit(proj, proj)) < 0.999) continue;
    d.libertes.push_back({g.desc, g.sugg, tous, {}});
    // On retire ce mouvement du noyau avant d'expliquer le reste
    for (auto& v : noyau) { const double c = produit(u, v); for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] -= c * u[static_cast<size_t>(i)]; }
    std::vector<std::vector<double>> base;
    for (auto& v : noyau) {
      for (const auto& b : base) { const double c = produit(v, b); for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] -= c * b[static_cast<size_t>(i)]; }
      if (normer(v) > 1e-6) base.push_back(v);
    }
    noyau = base;
  }
  // Mouvements restants : on les exprime par ce qu'ils changent de mesurable. Grandeurs, par ordre de priorité :
  // longueurs des lignes et rayons, puis orientations des lignes, puis positions des points. Une élimination de Gauss
  // sur ces taux de variation donne une base où chaque mouvement change une grandeur et laisse les autres pivots fixes.
  struct Grandeur { int priorite; int element; int point; int sorte; };  // sorte : 0 longueur, 1 rayon, 2 angle, 3 x, 4 y
  std::vector<Grandeur> grandeurs;
  for (const auto& el : e.elements) {
    if (el.type == TypeElement::Ligne) { grandeurs.push_back({0, el.id, 0, 0}); grandeurs.push_back({1, el.id, 0, 2}); }
    else grandeurs.push_back({0, el.id, 0, 1});
  }
  for (const auto& p : e.points) if (p.id) { grandeurs.push_back({2, 0, p.id, 3}); grandeurs.push_back({2, 0, p.id, 4}); }
  auto taux = [&](const std::vector<double>& v, const Grandeur& g) -> double {
    auto dp = [&](int pid) { return pid ? Point{v[static_cast<size_t>(P.ix.at(pid))], v[static_cast<size_t>(P.iy.at(pid))]} : Point{0, 0}; };
    if (g.sorte == 3) return dp(g.point).x;
    if (g.sorte == 4) return dp(g.point).y;
    const ElementEsquisse* el = e.element(g.element);
    if (!el || !e.elementValide(*el)) return 0;
    if (g.sorte == 1) {
      if (el->type == TypeElement::Cercle) return v[static_cast<size_t>(P.ir.at(el->id))];
      const PointEsquisse *c = e.point(el->centre), *q = e.point(el->p1);
      const double r = std::max(1e-12, std::hypot(q->x - c->x, q->y - c->y));
      const Point d1 = dp(el->p1), dc = dp(el->centre);
      return ((d1.x - dc.x) * (q->x - c->x) + (d1.y - dc.y) * (q->y - c->y)) / r;
    }
    const PointEsquisse *pa = e.point(el->p1), *pb = e.point(el->p2);
    const double ux = pb->x - pa->x, uy = pb->y - pa->y, l = std::max(1e-12, std::hypot(ux, uy));
    const Point da = dp(el->p1), db = dp(el->p2);
    const double ddx = db.x - da.x, ddy = db.y - da.y;
    return g.sorte == 0 ? (ddx * ux + ddy * uy) / l : (ddx * uy - ddy * ux) / (l * l) * -1;
  };
  const int k = static_cast<int>(noyau.size());
  const int q = static_cast<int>(grandeurs.size());
  std::vector<std::vector<double>> M(static_cast<size_t>(k), std::vector<double>(static_cast<size_t>(q)));
  for (int r = 0; r < k; ++r) for (int c = 0; c < q; ++c) M[static_cast<size_t>(r)][static_cast<size_t>(c)] = taux(noyau[static_cast<size_t>(r)], grandeurs[static_cast<size_t>(c)]);
  std::vector<int> pivots(static_cast<size_t>(k), -1);
  for (int etape = 0; etape < k; ++etape) {
    int br = -1, bc = -1;
    double bv = 1e-7;
    for (int prio = 0; prio <= 2 && br < 0; ++prio)
      for (int r = etape; r < k; ++r)
        for (int c = 0; c < q; ++c)
          if (grandeurs[static_cast<size_t>(c)].priorite == prio && std::abs(M[static_cast<size_t>(r)][static_cast<size_t>(c)]) > bv) { bv = std::abs(M[static_cast<size_t>(r)][static_cast<size_t>(c)]); br = r; bc = c; }
    if (br < 0) break;
    std::swap(M[static_cast<size_t>(etape)], M[static_cast<size_t>(br)]);
    std::swap(noyau[static_cast<size_t>(etape)], noyau[static_cast<size_t>(br)]);
    const double piv = M[static_cast<size_t>(etape)][static_cast<size_t>(bc)];
    for (double& x2 : M[static_cast<size_t>(etape)]) x2 /= piv;
    for (double& x2 : noyau[static_cast<size_t>(etape)]) x2 /= piv;
    for (int r = 0; r < k; ++r) {
      if (r == etape) continue;
      const double fct = M[static_cast<size_t>(r)][static_cast<size_t>(bc)];
      if (std::abs(fct) < 1e-15) continue;
      for (int c = 0; c < q; ++c) M[static_cast<size_t>(r)][static_cast<size_t>(c)] -= fct * M[static_cast<size_t>(etape)][static_cast<size_t>(c)];
      for (int i = 0; i < n; ++i) noyau[static_cast<size_t>(r)][static_cast<size_t>(i)] -= fct * noyau[static_cast<size_t>(etape)][static_cast<size_t>(i)];
    }
    pivots[static_cast<size_t>(etape)] = bc;
  }
  for (int r = 0; r < k; ++r) {
    const int c = pivots[static_cast<size_t>(r)];
    if (c < 0) continue;
    const Grandeur& g = grandeurs[static_cast<size_t>(c)];
    LiberteEsquisse l;
    // Les éléments dont une grandeur bouge avec ce mouvement (le pivot d'abord)
    std::set<int> concernes;
    for (int c2 = 0; c2 < q; ++c2)
      if (std::abs(M[static_cast<size_t>(r)][static_cast<size_t>(c2)]) > 1e-6 && grandeurs[static_cast<size_t>(c2)].element) concernes.insert(grandeurs[static_cast<size_t>(c2)].element);
    if (g.element) { l.elements.push_back(g.element); concernes.erase(g.element); }
    l.elements.insert(l.elements.end(), concernes.begin(), concernes.end());
    const ElementEsquisse* el = g.element ? e.element(g.element) : nullptr;
    const std::string nom = el ? nomElement(*el) : "Point " + std::to_string(g.point);
    if (g.sorte == 0) { l.description = nom + " : longueur libre"; l.suggestion = "Cote sa longueur (D, puis clic sur la ligne) ou rends-la égale à une autre ligne."; }
    else if (g.sorte == 1) { l.description = nom + " : rayon libre"; l.suggestion = "Cote son diamètre ou son rayon (D, puis clic dessus), ou rends-le égal à un autre."; }
    else if (g.sorte == 2) { l.description = nom + " : orientation libre"; l.suggestion = "Rends-la horizontale ou verticale, parallèle ou perpendiculaire à une autre, ou cote un angle."; }
    else {
      l.points.push_back(g.point);
      l.description = nom + (g.sorte == 3 ? " : position horizontale libre" : " : position verticale libre");
      l.suggestion = "Cote sa distance à l'origine ou à un autre élément, ou relie-le par une coïncidence.";
    }
    for (int eid : l.elements)
      if (const ElementEsquisse* x2 = e.element(eid)) for (int pid : {x2->p1, x2->p2, x2->centre}) if (pid > 0 && std::find(l.points.begin(), l.points.end(), pid) == l.points.end()) l.points.push_back(pid);
    d.libertes.push_back(l);
  }
  return d;
}

std::vector<Boucle> boucles(const Esquisse& e, std::string* erreur) {
  std::vector<Boucle> res;
  auto pt = [&](int id) { const PointEsquisse* p = e.point(id); return p ? Point{p->x, p->y} : Point{0, 0}; };
  // Points confondus par une contrainte de coïncidence : même nœud.
  std::map<int, int> parent;
  std::function<int(int)> racine = [&](int a) { auto it = parent.find(a); if (it == parent.end() || it->second == a) return a; return parent[a] = racine(it->second); };
  for (const auto& c : e.contraintes)
    if (c.type == TypeContrainte::Coincidence && c.refs.size() == 2) parent[racine(c.refs[0])] = racine(c.refs[1]);
  std::map<int, std::vector<std::pair<int, int>>> adj;  // nœud -> (élément, extrémité 1 ou 2)
  for (const auto& el : e.elements) {
    if (el.construction) continue;
    if (el.type == TypeElement::Cercle) {
      Boucle b;
      b.elements.push_back({el.id, false});
      const Point c = pt(el.centre);
      const int n = std::max(24, static_cast<int>(el.rayon * 4));
      for (int i = 0; i < n; ++i) b.polygone.push_back({c.x + el.rayon * std::cos(2 * PI * i / n), c.y + el.rayon * std::sin(2 * PI * i / n)});
      b.aire = PI * el.rayon * el.rayon;
      res.push_back(b);
      continue;
    }
    adj[racine(el.p1)].push_back({el.id, 1});
    adj[racine(el.p2)].push_back({el.id, 2});
  }
  // Un contour dont un nœud n'a pas exactement deux éléments est écarté et signalé ; les autres contours, et les
  // cercles, restent (ES-30)
  std::set<int> vus;
  {
    std::map<int, std::vector<int>> noeudsDe;  // élément → nœuds
    for (const auto& [noeud, l] : adj)
      for (const auto& [id, ext] : l) noeudsDe[id].push_back(noeud);
    for (const auto& [noeud, l] : adj) {
      if (l.size() == 2) continue;
      if (erreur && erreur->empty())
        *erreur = l.size() == 1 ? "Profil ouvert : un élément s'arrête sans rejoindre le suivant." : "Plusieurs éléments partent du même point : le profil doit former des boucles simples.";
      // Tout le contour (partie connexe) de ce nœud est écarté
      std::vector<int> pile;
      for (const auto& [id, ext] : l) pile.push_back(id);
      while (!pile.empty()) {
        const int id = pile.back();
        pile.pop_back();
        if (!vus.insert(id).second) continue;
        for (int nd : noeudsDe[id])
          for (const auto& [j, ext] : adj[nd])
            if (!vus.count(j)) pile.push_back(j);
      }
    }
  }
  for (const auto& el0 : e.elements) {
    if (el0.construction || el0.type == TypeElement::Cercle || vus.count(el0.id)) continue;
    Boucle b;
    int courant = el0.id;
    bool inverse = false;
    for (int garde = 0; garde < 10000; ++garde) {
      vus.insert(courant);
      b.elements.push_back({courant, inverse});
      const ElementEsquisse* el = e.element(courant);
      const int depart = inverse ? el->p2 : el->p1, arrivee = inverse ? el->p1 : el->p2;
      if (el->type == TypeElement::Ligne) {
        b.polygone.push_back(pt(depart));
      } else {
        const Point c = pt(el->centre), a = pt(el->p1), z = pt(el->p2);
        const double r = std::hypot(a.x - c.x, a.y - c.y);
        double a0 = std::atan2(a.y - c.y, a.x - c.x), a1 = std::atan2(z.y - c.y, z.x - c.x);
        while (a1 <= a0) a1 += 2 * PI;
        const int n = std::max(4, static_cast<int>((a1 - a0) / (5 * PI / 180)));
        for (int i = 0; i < n; ++i) {
          const double t = inverse ? a1 - (a1 - a0) * i / n : a0 + (a1 - a0) * i / n;
          b.polygone.push_back({c.x + r * std::cos(t), c.y + r * std::sin(t)});
        }
      }
      // Élément suivant au nœud d'arrivée : l'autre élément qui y aboutit (entré par sa fin : parcouru à l'envers).
      (void)depart;
      const int noeud = racine(arrivee), extArrivee = inverse ? 1 : 2;
      int suivant = -1;
      bool suivantInverse = false;
      for (const auto& [id, ext] : adj[noeud]) {
        if (id == courant && ext == extArrivee) continue;
        suivant = id;
        suivantInverse = ext == 2;
      }
      if (suivant < 0 || vus.count(suivant)) break;
      courant = suivant;
      inverse = suivantInverse;
    }
    double a = 0;
    for (size_t i = 0; i < b.polygone.size(); ++i) {
      const Point& u = b.polygone[i];
      const Point& w = b.polygone[(i + 1) % b.polygone.size()];
      a += u.x * w.y - w.x * u.y;
    }
    b.aire = a / 2;
    res.push_back(b);
  }
  return res;
}

Json esquisseVersJson(const Esquisse& e) {
  Json j;
  j["prochainId"] = e.prochainId;
  j["points"] = Json::array();
  for (const auto& p : e.points) if (p.id != 0) j["points"].push_back({p.id, p.x, p.y});
  j["elements"] = Json::array();
  for (const auto& el : e.elements) {
    Json o = {{"id", el.id}};
    o["type"] = el.type == TypeElement::Ligne ? "ligne" : el.type == TypeElement::Cercle ? "cercle" : "arc";
    if (el.type != TypeElement::Cercle) { o["p1"] = el.p1; o["p2"] = el.p2; }
    if (el.type != TypeElement::Ligne) o["centre"] = el.centre;
    if (el.type == TypeElement::Cercle) o["rayon"] = el.rayon;
    if (el.construction) o["construction"] = true;
    if (!el.calque.empty()) o["calque"] = el.calque;
    j["elements"].push_back(o);
  }
  if (!e.calques.empty()) {
    j["calques"] = Json::array();
    for (const auto& k : e.calques) j["calques"].push_back({{"nom", k.nom}, {"couleur", k.couleur}, {"visible", k.visible}});
  }
  j["contraintes"] = Json::array();
  for (const auto& c : e.contraintes) {
    const char* cle = "?";
    for (const auto& k : CLES) if (k.t == c.type) cle = k.cle;
    Json o = {{"id", c.id}, {"type", cle}, {"refs", c.refs}};
    if (c.valeur != 0) o["valeur"] = c.valeur;
    if (c.valeur2 != 0) o["valeur2"] = c.valeur2;
    if (c.reference) o["reference"] = true;
    if (c.place) { o["lx"] = c.lx; o["ly"] = c.ly; }
    if (!c.expression.empty()) o["expression"] = c.expression;
    j["contraintes"].push_back(o);
  }
  for (const auto& o : e.contraintesInconnues) j["contraintes"].push_back(o);
  if (!e.liensExternes.empty()) {
    j["liensExternes"] = Json::array();
    for (const auto& l : e.liensExternes) {
      Json o = {{"point", l.point}, {"arete", l.arete}};
      if (l.centre) o["centre"] = true;
      if (l.cercle) o["cercle"] = l.cercle;
      j["liensExternes"].push_back(o);
    }
  }
  LecteurJson::reporter(j, e.autres);
  return j;
}

Esquisse esquisseDepuisJson(const Json& j, std::vector<std::string>* alertes) {
  Esquisse e;
  if (!j.is_object()) return e;
  const LecteurJson L(alertes, "Esquisse");
  int maxId = 0;
  std::set<int> ids;
  auto idLibre = [&](int id) { return id > 0 && ids.insert(id).second; };
  if (j.contains("points") && j["points"].is_array())
    for (const auto& p : j["points"]) {
      if (p.is_array() && p.size() == 3 && p[0].is_number_integer() && p[1].is_number() && p[2].is_number()) {
        const long long id = p[0].get<long long>();
        const double x = p[1].get<double>(), y = p[2].get<double>();
        if (id > 0 && id < 1'000'000'000 && std::isfinite(x) && std::isfinite(y) && idLibre(static_cast<int>(id))) {
          e.points.push_back({static_cast<int>(id), x, y});
          maxId = std::max(maxId, static_cast<int>(id));
          continue;
        }
      }
      L.alerter("un point illisible ou en double est écarté");
    }
  if (j.contains("elements") && j["elements"].is_array())
    for (const auto& o : j["elements"]) {
      if (!o.is_object()) { L.alerter("un élément illisible est écarté"); continue; }
      ElementEsquisse el;
      el.id = L.entier(o, "id", 0);
      const std::string t = L.texte(o, "type", "ligne");
      el.type = t == "cercle" ? TypeElement::Cercle : t == "arc" ? TypeElement::Arc : TypeElement::Ligne;
      el.p1 = L.entier(o, "p1", -1); el.p2 = L.entier(o, "p2", -1); el.centre = L.entier(o, "centre", -1);
      // Rayon d'un cercle : gardé tel qu'écrit dès qu'il est positif (aucun minimum arbitraire)
      el.rayon = L.nombre(o, "rayon", 5.0);
      if (el.type == TypeElement::Cercle && !(el.rayon > 0)) { L.alerter("cercle de rayon nul ou négatif écarté"); continue; }
      el.construction = L.booleen(o, "construction", false);
      el.calque = L.texte(o, "calque", std::string());
      // Élément qui référence un point inexistant : écarté (et signalé) plutôt que de faire échouer la lecture.
      const bool ok = el.type == TypeElement::Cercle ? e.point(el.centre) != nullptr
                      : el.type == TypeElement::Arc ? (e.point(el.centre) && e.point(el.p1) && e.point(el.p2))
                                                    : (e.point(el.p1) && e.point(el.p2) && el.p1 != el.p2);
      if (!ok || !idLibre(el.id)) { L.alerter("un élément qui vise un point absent, ou d'identifiant en double, est écarté"); continue; }
      e.elements.push_back(el);
      maxId = std::max(maxId, el.id);
    }
  // Calques : leur interface a disparu en 0.17 ; un calque masqué d'un ancien fichier ne pourrait plus être
  // rendu visible. Ils sont donc relus visibles (EI-12), avec une alerte s'ils ne l'étaient pas.
  if (j.contains("calques") && j["calques"].is_array())
    for (const auto& k : j["calques"])
      if (k.is_object() && k.contains("nom")) {
        Calque c{L.texte(k, "nom", std::string()), L.texte(k, "couleur", std::string("#1E7BE0")), true};
        if (!L.booleen(k, "visible", true)) L.alerter("calque « " + c.nom + " » masqué : il est de nouveau visible");
        e.calques.push_back(c);
      }
  if (j.contains("contraintes") && j["contraintes"].is_array())
    for (const auto& o : j["contraintes"]) {
      if (!o.is_object()) { L.alerter("une contrainte illisible est écartée"); continue; }
      ContrainteEsquisse c;
      c.id = L.entier(o, "id", 0);
      const std::string t = L.texte(o, "type", "");
      bool connu = false;
      for (const auto& k : CLES) if (t == k.cle) { c.type = k.t; connu = true; }
      if (!connu) {
        // Contrainte d'une version plus récente : gardée pour être réécrite, mais pas résolue
        L.alerter("contrainte de type « " + t + " » inconnue de cette version : gardée sans être appliquée");
        e.contraintesInconnues.push_back(o);
        maxId = std::max(maxId, c.id);
        continue;
      }
      c.refs = L.entiers(o, "refs");
      // Une référence vers un point ou un élément absent ferait lire le point (0 ; 0) au solveur : écartée
      bool refsOk = true;
      for (int r : c.refs)
        if (r > 0 && !e.point(r) && !e.element(r)) refsOk = false;
        else if (r < AXE_V) refsOk = false;
      if (!refsOk) { L.alerter("une contrainte qui vise un objet absent est écartée"); continue; }
      if (!idLibre(c.id)) c.id = -1;  // identifiant manquant ou en double : un neuf lui est donné plus bas
      c.valeur = L.nombre(o, "valeur", 0.0);
      c.valeur2 = L.nombre(o, "valeur2", 0.0);
      c.reference = L.booleen(o, "reference", false);
      if (o.contains("lx") && o.contains("ly")) { c.place = true; c.lx = L.nombre(o, "lx", 0.0); c.ly = L.nombre(o, "ly", 0.0); }
      c.expression = L.texte(o, "expression", std::string());
      e.contraintes.push_back(c);
      maxId = std::max(maxId, c.id);
    }
  e.prochainId = std::max(L.entier(j, "prochainId", 1), maxId + 1);
  for (auto& c : e.contraintes)
    if (c.id < 0) c.id = e.prochainId++;
  if (j.contains("liensExternes") && j["liensExternes"].is_array())
    for (const auto& o : j["liensExternes"]) {
      if (!o.is_object()) continue;
      LienExterne l;
      l.point = L.entier(o, "point", 0);
      l.arete = L.texte(o, "arete", std::string());
      l.centre = L.booleen(o, "centre", false);
      l.cercle = L.entier(o, "cercle", 0);
      if (l.point > 0 && !l.arete.empty() && e.point(l.point)) e.liensExternes.push_back(l);
      else L.alerter("lien vers une arête de la pièce illisible : écarté");
    }
  e.autres = LecteurJson::inconnus(j, {"prochainId", "points", "elements", "calques", "contraintes", "liensExternes"});
  return e;
}

}  // namespace prom
