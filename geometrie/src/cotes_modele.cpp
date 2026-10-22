// Prométhée : cotes du modèle reprises en mise en plan (D3). Licence GPL-3.0-only.
#include "promethee/cotes_modele.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include <gp_Ax3.hxx>
#include <gp_Vec.hxx>

#include "promethee/esquisse.hpp"

namespace prom {

namespace {

struct Plan2D {
  gp_Ax3 ax;
  gp_Pnt vers3D(double x, double y) const {
    return ax.Location().Translated(gp_Vec(ax.XDirection()) * x + gp_Vec(ax.YDirection()) * y);
  }
  gp_Dir dir3D(double x, double y) const { return gp_Dir(gp_Vec(ax.XDirection()) * x + gp_Vec(ax.YDirection()) * y); }
};

std::string nomEsquisse(const FonctionPiece& f) { return f.nom.empty() ? "Esquisse " + std::to_string(f.id) : f.nom; }

}  // namespace

std::vector<OptionsMiseEnPlan::CoteModele> cotesDuModele(const Piece& p, const ResultatPiece& r, std::vector<std::string>* orphelines) {
  std::vector<OptionsMiseEnPlan::CoteModele> res;
  // Esquisses employées par une fonction active (profil, chemin, profils d'un lissage)
  std::set<int> employees;
  for (const auto& f : p.fonctions) {
    if (f.desactivee) continue;
    if (f.esquisseId) employees.insert(f.esquisseId);
    if (f.cheminId) employees.insert(f.cheminId);
    for (int s : f.sources) employees.insert(s);
  }
  // Fonctions en échec ou obsolètes : leurs esquisses ne sont pas cotées
  std::set<int> enEchec;
  for (const auto& [id, m] : r.erreurs) enEchec.insert(id);
  for (int id : r.obsoletes) enEchec.insert(id);
  std::set<int> esquissesEnEchec;
  for (const auto& f : p.fonctions)
    if (enEchec.count(f.id)) {
      if (f.type == TypeFonction::Esquisse) esquissesEnEchec.insert(f.id);
      if (f.esquisseId) esquissesEnEchec.insert(f.esquisseId);
    }
  for (const auto& f : p.fonctions) {
    if (f.type != TypeFonction::Esquisse || f.desactivee || !employees.count(f.id)) continue;
    const Esquisse& e = f.esquisse;
    auto estCote = [](const ContrainteEsquisse& c) {
      switch (c.type) {
        case TypeContrainte::Longueur: case TypeContrainte::Distance: case TypeContrainte::DistanceX: case TypeContrainte::DistanceY:
        case TypeContrainte::Rayon: case TypeContrainte::Diametre: case TypeContrainte::Angle: return true;
        default: return false;
      }
    };
    if (esquissesEnEchec.count(f.id)) {
      if (orphelines)
        for (const auto& c : e.contraintes)
          if (estCote(c)) orphelines->push_back(nomEsquisse(f) + " · cote " + std::to_string(c.id) + " : la fonction qui l'emploie est en échec");
      continue;
    }
    const Plan2D pl{r.repere(f.id)};
    auto pt = [&](int id, double& x, double& y) {
      if (id == 0) { x = y = 0; return true; }
      const PointEsquisse* q = e.point(id);
      if (!q) return false;
      x = q->x;
      y = q->y;
      return true;
    };
    auto ligne = [&](int id, double& ax, double& ay, double& bx, double& by) {
      if (id == AXE_H) { ax = ay = by = 0; bx = 1; return true; }
      if (id == AXE_V) { ax = ay = bx = 0; by = 1; return true; }
      const ElementEsquisse* l = e.element(id);
      if (!l || l->type != TypeElement::Ligne) return false;
      return pt(l->p1, ax, ay) && pt(l->p2, bx, by);
    };
    for (const auto& c : e.contraintes) {
      if (!estCote(c)) continue;
      OptionsMiseEnPlan::CoteModele k;
      k.normale = pl.ax.Direction();
      k.reference = c.reference;
      k.nom = nomEsquisse(f) + " · cote " + std::to_string(c.id);
      k.valeur = mesurer(e, c);
      if (c.place) { k.place = true; k.etiquette = pl.vers3D(c.lx, c.ly); }
      bool ok = true;
      switch (c.type) {
        case TypeContrainte::Rayon:
        case TypeContrainte::Diametre: {
          const ElementEsquisse* el = c.refs.empty() ? nullptr : e.element(c.refs[0]);
          double cx = 0, cy = 0;
          if (!el || el->type == TypeElement::Ligne || !pt(el->centre, cx, cy)) { ok = false; break; }
          const double rr = e.rayonDe(*el);
          k.genre = c.type == TypeContrainte::Rayon ? OptionsMiseEnPlan::CoteModele::Genre::Rayon : OptionsMiseEnPlan::CoteModele::Genre::Diametre;
          k.a = pl.vers3D(cx, cy);
          // Point du cercle vers l'étiquette (en haut à droite par défaut)
          double ux = std::cos(M_PI / 4), uy = std::sin(M_PI / 4);
          if (c.place && std::hypot(c.lx - cx, c.ly - cy) > 1e-9) { ux = (c.lx - cx) / std::hypot(c.lx - cx, c.ly - cy); uy = (c.ly - cy) / std::hypot(c.lx - cx, c.ly - cy); }
          k.b = pl.vers3D(cx + rr * ux, cy + rr * uy);
          break;
        }
        case TypeContrainte::Angle: {
          double a1x, a1y, b1x, b1y, a2x, a2y, b2x, b2y;
          if (c.refs.size() < 2 || !ligne(c.refs[0], a1x, a1y, b1x, b1y) || !ligne(c.refs[1], a2x, a2y, b2x, b2y)) { ok = false; break; }
          // Secteur coté (S2) : chaque ligne dans le sens retenu par la cote
          const int secteur = static_cast<int>(std::lround(c.valeur2)) & 3;
          const double s1 = (secteur & 1) ? -1 : 1, s2 = (secteur & 2) ? -1 : 1;
          const double d1x = s1 * (b1x - a1x), d1y = s1 * (b1y - a1y), d2x = s2 * (b2x - a2x), d2y = s2 * (b2y - a2y);
          const double det = d1x * d2y - d1y * d2x;
          if (std::abs(det) < 1e-12) { ok = false; break; }
          const double t = ((a2x - a1x) * d2y - (a2y - a1y) * d2x) / det;
          const double ix = a1x + t * d1x, iy = a1y + t * d1y;
          k.genre = OptionsMiseEnPlan::CoteModele::Genre::Angle;
          k.a = pl.vers3D(ix, iy);
          // Côtés : la première direction, puis la seconde (l'angle orienté mesuré les sépare)
          const double ang1 = std::atan2(d1y, d1x), balayage = k.valeur * M_PI / 180;
          k.b = pl.vers3D(ix + std::cos(ang1), iy + std::sin(ang1));
          k.c = pl.vers3D(ix + std::cos(ang1 + balayage), iy + std::sin(ang1 + balayage));
          k.valeur = std::abs(k.valeur);
          break;
        }
        default: {
          // Linéaire : une ligne, deux points, un point et une ligne, deux lignes parallèles
          double p1x = 0, p1y = 0, p2x = 0, p2y = 0;
          const ElementEsquisse* l0 = c.refs.empty() ? nullptr : e.element(c.refs[0]);
          if (l0 && l0->type == TypeElement::Ligne && c.refs.size() == 1) ok = pt(l0->p1, p1x, p1y) && pt(l0->p2, p2x, p2y);
          else if (c.refs.size() == 1) ok = pt(c.refs[0], p2x, p2y);
          else if (c.refs.size() >= 2) {
            double ax, ay, bx, by;
            if (c.refs[0] == 0 || e.point(c.refs[0])) {
              ok = pt(c.refs[0], p1x, p1y);
              if (ok && (c.refs[1] == 0 || e.point(c.refs[1]))) ok = pt(c.refs[1], p2x, p2y);
              else if (ok && ligne(c.refs[1], ax, ay, bx, by)) {
                const double vx = bx - ax, vy = by - ay, l2 = std::max(1e-12, vx * vx + vy * vy), tt = ((p1x - ax) * vx + (p1y - ay) * vy) / l2;
                p2x = ax + tt * vx;
                p2y = ay + tt * vy;
              } else ok = false;
            } else if (ligne(c.refs[0], ax, ay, bx, by) && ligne(c.refs[1], p1x, p1y, p2x, p2y)) {
              const double mx = (p1x + p2x) / 2, my = (p1y + p2y) / 2;
              const double vx = bx - ax, vy = by - ay, l2 = std::max(1e-12, vx * vx + vy * vy), tt = ((mx - ax) * vx + (my - ay) * vy) / l2;
              p1x = ax + tt * vx;
              p1y = ay + tt * vy;
              p2x = mx;
              p2y = my;
            } else ok = false;
          } else ok = false;
          if (!ok) break;
          k.genre = OptionsMiseEnPlan::CoteModele::Genre::Lineaire;
          k.a = pl.vers3D(p1x, p1y);
          k.b = pl.vers3D(p2x, p2y);
          if (c.type == TypeContrainte::DistanceX) { k.direction = true; k.sens = pl.dir3D(1, 0); }
          else if (c.type == TypeContrainte::DistanceY) { k.direction = true; k.sens = pl.dir3D(0, 1); }
          if (k.a.Distance(k.b) < 1e-9 && !k.direction) ok = false;
          break;
        }
      }
      if (!ok) {
        if (orphelines) orphelines->push_back(k.nom + " : élément coté disparu");
        continue;
      }
      res.push_back(k);
    }
  }
  return res;
}

}  // namespace prom
