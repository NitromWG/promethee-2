// Prométhée : dessins 2D échangés avec la carte (10.21). Voir dessins_carte.hpp. Licence GPL-3.0-only.
#include "promethee/dessins_carte.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <sstream>

#include "promethee/circuit_edition.hpp"
#include "promethee/dxf_dessin.hpp"
#include "promethee/lecture_svg.hpp"
#include "promethee/police.hpp"

namespace prom::ecad {

namespace {
struct P2 {
  double x = 0, y = 0;
};
using Chemin = std::vector<P2>;
struct Brut {
  std::vector<Chemin> fermes;       // contours pleins
  std::vector<Chemin> morceaux;     // morceaux de formes pleines, à réunir (un chemin SVG coupé à chaque courbe)
  std::vector<Chemin> ouverts;      // traits
  std::vector<Chemin> contours;     // traits fermés : contours sans fond
  bool fermerMorceaux = true;       // SVG : un fond referme son chemin ; DXF : un morceau resté ouvert est un trait
};
double d2(P2 a, P2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Morceaux réunis bout à bout (réparation des discontinuités : un écart sous la tolérance est compté) ; rend les
// chemins refermés dans « fermes », les autres dans « ouverts »
void chainer(std::vector<Chemin> restes, double tol, int& reunis, std::vector<Chemin>& fermes, std::vector<Chemin>& ouverts) {
  auto joint = [&](P2 a, P2 b) {
    const double d = d2(a, b);
    if (d > tol) return false;
    if (d > 1e-6) ++reunis;
    return true;
  };
  while (!restes.empty()) {
    Chemin c = std::move(restes.back());
    restes.pop_back();
    bool progres = true;
    while (progres && (c.size() < 3 || d2(c.front(), c.back()) > tol)) {
      progres = false;
      for (std::size_t i = 0; i < restes.size(); ++i) {
        Chemin& r = restes[i];
        if (joint(c.back(), r.front())) c.insert(c.end(), r.begin() + 1, r.end());
        else if (joint(c.back(), r.back())) c.insert(c.end(), r.rbegin() + 1, r.rend());
        else if (joint(c.front(), r.back())) c.insert(c.begin(), r.begin(), r.end() - 1);
        else if (joint(c.front(), r.front())) c.insert(c.begin(), r.rbegin(), r.rend() - 1);
        else continue;
        restes.erase(restes.begin() + static_cast<long>(i));
        progres = true;
        break;
      }
    }
    if (c.size() >= 3 && d2(c.front(), c.back()) <= tol) {
      if (d2(c.front(), c.back()) > 1e-6) ++reunis;
      c.pop_back();
      fermes.push_back(c);
    } else
      ouverts.push_back(c);
  }
}
Chemin bezier(P2 a, P2 c1, P2 c2, P2 b) {
  Chemin r;
  const double l = d2(a, c1) + d2(c1, c2) + d2(c2, b);
  const int n = std::clamp(static_cast<int>(l / 0.2), 4, 64);
  for (int i = 1; i <= n; ++i) {
    const double t = static_cast<double>(i) / n, u = 1 - t;
    r.push_back({u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x, u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y});
  }
  return r;
}
Chemin ellipse(P2 c, double rx, double ry) {
  Chemin r;
  const int n = std::clamp(static_cast<int>(2 * std::numbers::pi * std::max(rx, ry) / 0.2), 16, 128);
  for (int i = 0; i < n; ++i) {
    const double a = 2 * std::numbers::pi * i / n;
    r.push_back({c.x + rx * std::cos(a), c.y + ry * std::sin(a)});
  }
  return r;
}
// Dessin brut (mm, y vers le bas) → dessins de la carte posés et mis à l'échelle
std::vector<DessinCircuit> poser(Brut b, const OptionsDessinCarte& o, RapportDessinCarte& r) {
  // Formes pleines : leurs morceaux réunis, et refermés s'il le faut (un fond referme le chemin)
  {
    std::vector<Chemin> fermes, ouverts;
    chainer(std::move(b.morceaux), o.tolerance, r.reunis, fermes, ouverts);
    for (auto& c : fermes) b.fermes.push_back(std::move(c));
    for (auto& c : ouverts) {
      if (b.fermerMorceaux && c.size() >= 3) b.fermes.push_back(std::move(c));
      else b.ouverts.push_back(std::move(c));
    }
  }
  // Traits : réparés ; ceux qui se referment deviennent des contours sans fond
  {
    std::vector<Chemin> fermes, ouverts;
    int reunis = 0;
    chainer(std::move(b.ouverts), o.tolerance, reunis, fermes, ouverts);
    r.reunis += reunis;
    b.ouverts = std::move(ouverts);
    for (auto& c : fermes) b.contours.push_back(std::move(c));
  }
  if (!o.remplir) {
    for (auto& c : b.fermes) b.contours.push_back(std::move(c));
    b.fermes.clear();
  }
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  for (const auto* liste : {&b.fermes, &b.ouverts, &b.contours})
    for (const auto& c : *liste)
      for (const auto& p : c) {
        x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
      }
  std::vector<DessinCircuit> sortie;
  if (x0 > x1) {
    r.alertes.push_back("Le dessin ne contient aucune géométrie");
    return sortie;
  }
  double ech = o.echelle;
  if (o.largeurVoulue > 0 && x1 > x0) ech = o.largeurVoulue / (x1 - x0);
  const double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  r.largeur = (x1 - x0) * ech;
  r.hauteur = (y1 - y0) * ech;
  auto vers = [&](P2 p) {
    double x = (p.x - cx) * ech, y = (p.y - cy) * ech;
    if (o.miroir) x = -x;
    return o.centre + ptMm(x, y);
  };
  auto nouveau = [&](DessinCircuit::Forme f, const Chemin& c, bool plein) {
    DessinCircuit d;
    d.uuid = nouvelUuid();
    d.couche = o.couche;
    d.forme = f;
    for (const auto& q : c) d.points.push_back(vers(q));
    d.plein = plein;
    d.epaisseur = plein ? 0 : o.epaisseur;
    return d;
  };
  if (!b.fermes.empty()) {
    // Imbrication : un contour contenu dans un nombre impair d'autres est un trou
    Polygones contours;
    for (const auto& c : b.fermes) {
      Polygone p;
      for (const auto& q : c) p.push_back(vers(q));
      if (p.size() >= 3) contours.push_back(orienter(p));
    }
    for (std::size_t i = 0; i < contours.size(); ++i) {
      int profondeur = 0;
      for (std::size_t j = 0; j < contours.size(); ++j)
        if (i != j && dedans(contours[j], contours[i].front()) && std::abs(aireMm2(contours[j])) > std::abs(aireMm2(contours[i]))) ++profondeur;
      if (profondeur % 2) std::reverse(contours[i].begin(), contours[i].end());
    }
    r.contours += static_cast<int>(b.fermes.size());
    for (const Polygone& p : decouperEnContoursSimples(unir(contours))) {
      DessinCircuit d;
      d.uuid = nouvelUuid();
      d.couche = o.couche;
      d.forme = DessinCircuit::Forme::Polygone;
      d.points = p;
      d.plein = true;
      d.epaisseur = 0;
      sortie.push_back(d);
      ++r.ilots;
    }
  }
  for (const auto& c : b.contours) {
    sortie.push_back(nouveau(DessinCircuit::Forme::Polygone, c, false));
    ++r.contours;
  }
  for (const auto& c : b.ouverts) {
    if (c.size() < 2) continue;
    sortie.push_back(nouveau(DessinCircuit::Forme::Trait, c, false));
    ++r.traits;
  }
  return sortie;
}
}  // namespace

Polygones decouperEnContoursSimples(const Polygones& p) {
  Polygones r;
  for (const Ilot& il : ilots(p)) {
    Polygone ext = orienter(il.exterieur);
    // Trous rangés de droite à gauche : chacun relié au contour courant par la coupure la plus courte qui ne
    // traverse rien
    std::vector<Polygone> trous;
    for (Polygone t : il.trous) {
      t = orienter(t);
      std::reverse(t.begin(), t.end());  // trou dans le sens inverse
      trous.push_back(t);
    }
    std::sort(trous.begin(), trous.end(), [](const Polygone& a, const Polygone& b) { return boite(a).x1 > boite(b).x1; });
    for (const Polygone& t : trous) {
      std::size_t it = 0;
      for (std::size_t i = 1; i < t.size(); ++i)
        if (t[i].x > t[it].x) it = i;
      // Sommet du contour le plus proche dont la coupure ne coupe aucun côté (du contour ni des trous restants)
      std::size_t meilleur = 0;
      double dm = 1e300;
      for (std::size_t i = 0; i < ext.size(); ++i) {
        const double d = distance(ext[i], t[it]);
        if (d >= dm) continue;
        bool libre = true;
        for (std::size_t k = 0; k < ext.size() && libre; ++k) {
          const Pt a = ext[k], b = ext[(k + 1) % ext.size()];
          if (k == i || (k + 1) % ext.size() == i) continue;
          libre = !segmentsSeCoupent(ext[i], t[it], a, b);
        }
        for (std::size_t k = 0; k < t.size() && libre; ++k) {
          const Pt a = t[k], b = t[(k + 1) % t.size()];
          if (k == it || (k + 1) % t.size() == it) continue;
          libre = !segmentsSeCoupent(ext[i], t[it], a, b);
        }
        if (libre) {
          dm = d;
          meilleur = i;
        }
      }
      // ext[0..m], trou depuis it (tour complet), retour à ext[m], puis la suite
      Polygone n(ext.begin(), ext.begin() + static_cast<long>(meilleur) + 1);
      for (std::size_t k = 0; k <= t.size(); ++k) n.push_back(t[(it + k) % t.size()]);
      n.insert(n.end(), ext.begin() + static_cast<long>(meilleur), ext.end());
      ext = std::move(n);
    }
    r.push_back(ext);
  }
  return r;
}

std::vector<DessinCircuit> dessinsDepuisSvg(const std::string& texte, const OptionsDessinCarte& o, RapportDessinCarte* rapport) {
  RapportDessinCarte r;
  const svg::DessinSvg d = svg::lireSvg(texte);
  r.alertes = d.alertes;
  Brut b;
  for (const auto& f : d.formes) {
    if (!o.calques.empty() && !o.calques.count(f.calque)) continue;
    Chemin c;
    bool ferme = f.ferme;
    switch (f.genre) {
      case svg::FormeSvg::Genre::Polyligne:
        for (const auto& p : f.points) c.push_back({p.x, p.y});
        break;
      case svg::FormeSvg::Genre::Bezier:
        if (f.points.size() >= 4) {
          c.push_back({f.points[0].x, f.points[0].y});
          for (std::size_t i = 0; i + 3 < f.points.size(); i += 3) {
            const auto morceau = bezier({f.points[i].x, f.points[i].y}, {f.points[i + 1].x, f.points[i + 1].y}, {f.points[i + 2].x, f.points[i + 2].y}, {f.points[i + 3].x, f.points[i + 3].y});
            c.insert(c.end(), morceau.begin(), morceau.end());
          }
        }
        break;
      case svg::FormeSvg::Genre::Cercle:
        if (!f.points.empty()) c = ellipse({f.points[0].x, f.points[0].y}, f.rx, f.rx);
        ferme = true;
        break;
      case svg::FormeSvg::Genre::Ellipse:
        if (!f.points.empty()) c = ellipse({f.points[0].x, f.points[0].y}, f.rx, f.ry);
        ferme = true;
        break;
      case svg::FormeSvg::Genre::Rectangle:
        if (f.points.size() >= 2) c = {{f.points[0].x, f.points[0].y}, {f.points[1].x, f.points[0].y}, {f.points[1].x, f.points[1].y}, {f.points[0].x, f.points[1].y}};
        ferme = true;
        break;
    }
    if (c.size() < 2) continue;
    if (!f.fondChemin.empty() && o.remplir) {
      // Forme pleine : entière si elle est fermée, sinon un morceau de son chemin à réunir
      if (ferme && d2(c.front(), c.back()) < 1e-9) c.pop_back();
      (ferme ? b.fermes : b.morceaux).push_back(c);
    } else if (ferme) {
      if (d2(c.front(), c.back()) < 1e-9) c.pop_back();
      b.contours.push_back(c);
    } else
      b.ouverts.push_back(c);
  }
  auto s = poser(b, o, r);
  if (rapport) *rapport = r;
  return s;
}

std::vector<DessinCircuit> dessinsDepuisDxf(const std::string& texte, const OptionsDessinCarte& o, RapportDessinCarte* rapport) {
  RapportDessinCarte r;
  const dxf::DessinDxf d = dxf::lireDessinDxf(texte);
  r.alertes = d.alertes;
  Brut b;
  b.fermerMorceaux = false;
  for (const auto& t : d.traits) {
    if (!o.calques.empty() && !o.calques.count(t.calque)) continue;
    // DXF : y vers le haut ; la carte : y vers le bas
    auto P = [](dxf::Pt2 p) { return P2{p.x, -p.y}; };
    switch (t.genre) {
      case dxf::TraitDxf::Genre::Ligne: b.morceaux.push_back({P(t.a), P(t.b)}); break;
      case dxf::TraitDxf::Genre::Cercle: b.fermes.push_back(ellipse(P(t.centre), t.rayon, t.rayon)); break;
      case dxf::TraitDxf::Genre::Arc: {
        double a0 = std::atan2(t.a.y - t.centre.y, t.a.x - t.centre.x), a1 = std::atan2(t.b.y - t.centre.y, t.b.x - t.centre.x);
        if (a1 <= a0) a1 += 2 * std::numbers::pi;
        const int n = std::clamp(static_cast<int>((a1 - a0) * t.rayon / 0.2), 4, 128);
        Chemin c;
        for (int i = 0; i <= n; ++i) {
          const double a = a0 + (a1 - a0) * i / n;
          c.push_back(P({t.centre.x + t.rayon * std::cos(a), t.centre.y + t.rayon * std::sin(a)}));
        }
        b.morceaux.push_back(c);
        break;
      }
      case dxf::TraitDxf::Genre::Point: break;
    }
  }
  auto s = poser(b, o, r);
  if (rapport) *rapport = r;
  return s;
}

std::optional<Pastille> pastilleDepuisDessins(const std::vector<DessinCircuit>& d, const std::string& numero) {
  Polygones pleins;
  for (const auto& x : d)
    if (x.plein && x.points.size() >= 3) pleins.push_back(orienter(x.points));
  if (pleins.empty()) return std::nullopt;
  const Polygones u = unir(pleins);
  const auto il = ilots(u);
  if (il.empty()) return std::nullopt;
  const Ilot* grand = &il.front();
  for (const auto& x : il)
    if (std::abs(aireMm2(x.exterieur)) > std::abs(aireMm2(grand->exterieur))) grand = &x;
  const Boite b = boite(grand->exterieur);
  Pastille p;
  p.numero = numero;
  p.type = TypePastille::CMS;
  p.forme = FormePastille::Libre;
  p.position = b.centre();
  p.largeur = b.largeur();
  p.hauteur = b.hauteur();
  for (const Pt& q : grand->exterieur) p.contourLibre.push_back(q - b.centre());
  return p;
}

// ---------- DXF par couches ----------
namespace {
class DxfR12 {
 public:
  void calque(const std::string& nom, int couleur) { m_calques.push_back({nom, couleur}); }
  void ligne(const std::string& c, double x1, double y1, double x2, double y2) {
    m_e << "0\nLINE\n8\n" << c << "\n10\n" << n(x1) << "\n20\n" << n(y1) << "\n30\n0\n11\n" << n(x2) << "\n21\n" << n(y2) << "\n31\n0\n";
  }
  void cercle(const std::string& c, double x, double y, double r) { m_e << "0\nCIRCLE\n8\n" << c << "\n10\n" << n(x) << "\n20\n" << n(y) << "\n30\n0\n40\n" << n(r) << "\n"; }
  void polyligne(const std::string& c, const std::vector<std::pair<double, double>>& p, bool ferme, double largeur = 0) {
    if (p.size() < 2) return;
    m_e << "0\nPOLYLINE\n8\n" << c << "\n66\n1\n10\n0\n20\n0\n30\n0\n70\n" << (ferme ? 1 : 0) << "\n";
    if (largeur > 0) m_e << "40\n" << n(largeur) << "\n41\n" << n(largeur) << "\n";
    for (const auto& [x, y] : p) m_e << "0\nVERTEX\n8\n" << c << "\n10\n" << n(x) << "\n20\n" << n(y) << "\n30\n0\n";
    m_e << "0\nSEQEND\n8\n" << c << "\n";
  }
  void texte(const std::string& c, double x, double y, double h, const std::string& t) {
    m_e << "0\nTEXT\n8\n" << c << "\n10\n" << n(x) << "\n20\n" << n(y) << "\n30\n0\n40\n" << n(h) << "\n1\n" << t << "\n";
  }
  std::string texte() const {
    std::ostringstream o;
    o << "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1009\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n" << m_calques.size() << "\n";
    for (const auto& [nom, couleur] : m_calques) o << "0\nLAYER\n2\n" << nom << "\n70\n0\n62\n" << couleur << "\n6\nCONTINUOUS\n";
    o << "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n" << m_e.str() << "0\nENDSEC\n0\nEOF\n";
    return o.str();
  }

 private:
  static std::string n(double v) {
    char t[32];
    std::snprintf(t, sizeof t, "%.4f", std::abs(v) < 1e-9 ? 0.0 : v);
    return t;
  }
  std::vector<std::pair<std::string, int>> m_calques;
  std::ostringstream m_e;
};
int couleurCalque(Couche k) {
  if (k == Couche::CuivreDessus) return 1;
  if (k == Couche::CuivreDessous) return 5;
  if (estCuivre(k)) return 3;
  if (k == Couche::SerigraphieDessus || k == Couche::SerigraphieDessous) return 7;
  if (k == Couche::Contour) return 2;
  return 8;
}
}  // namespace

std::string dxfCouches(const ConceptionElec& c, const JeuDxf& j) {
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  std::vector<Couche> couches = j.couches;
  if (couches.empty()) {
    for (Couche x : couchesCuivre(c.circuit.empilage.couchesCuivre)) couches.push_back(x);
    for (Couche x : {Couche::VernisDessus, Couche::VernisDessous, Couche::PateDessus, Couche::PateDessous, Couche::SerigraphieDessus, Couche::SerigraphieDessous, Couche::Dessins}) couches.push_back(x);
  }
  // Repère : millimètres, y vers le haut (DXF), miroir pour une vue de dessous
  auto X = [&](Nm x) { return j.miroir ? -mm(x) : mm(x); };
  auto Y = [&](Nm y) { return -mm(y); };
  auto pts = [&](const Polygone& p) {
    std::vector<std::pair<double, double>> v;
    for (const Pt& q : p) v.push_back({X(q.x), Y(q.y)});
    return v;
  };
  DxfR12 d;
  if (j.contour) d.calque("Edge.Cuts", 2);
  for (Couche x : couches) d.calque(codeCouche(x), couleurCalque(x));
  if (j.cotes) d.calque("Cotes", 4);
  if (j.contour) {
    d.polyligne("Edge.Cuts", pts(c.circuit.contour.polygone()), true);
    for (const auto& dec : c.circuit.contour.decoupes) d.polyligne("Edge.Cuts", pts(dec), true);
  }
  auto veut = [&](Couche x) { return std::find(couches.begin(), couches.end(), x) != couches.end(); };
  for (const auto& p : pp)
    for (Couche x : couches)
      if (p.couches & bit(x)) {
        d.polyligne(codeCouche(x), pts(p.forme), true);
        if (p.pastille.percage > 0 && estCuivre(x)) d.cercle(codeCouche(x), X(p.pastille.position.x), Y(p.pastille.position.y), mm(p.pastille.percage) / 2);
      }
  for (const auto& t : c.circuit.pistes)
    if (veut(t.couche)) {
      std::vector<std::pair<double, double>> v;
      for (const auto& [a, b] : segmentsPiste(t)) {
        if (v.empty()) v.push_back({X(a.x), Y(a.y)});
        v.push_back({X(b.x), Y(b.y)});
      }
      d.polyligne(codeCouche(t.couche), v, false, mm(t.largeur));
    }
  for (const auto& v : c.circuit.vias)
    for (Couche x : couchesCuivre(c.circuit.empilage.couchesCuivre))
      if (veut(x) && rangCuivre(x, c.circuit.empilage.couchesCuivre) >= rangCuivre(v.de, c.circuit.empilage.couchesCuivre) &&
          rangCuivre(x, c.circuit.empilage.couchesCuivre) <= rangCuivre(v.vers, c.circuit.empilage.couchesCuivre)) {
        d.cercle(codeCouche(x), X(v.p.x), Y(v.p.y), mm(diametreViaSur(v, x, c.circuit.empilage.couchesCuivre)) / 2);
        d.cercle(codeCouche(x), X(v.p.x), Y(v.p.y), mm(v.percage) / 2);
      }
  for (const auto& z : c.circuit.zones)
    if (veut(z.couche)) d.polyligne(codeCouche(z.couche), pts(z.contour), true);
  for (const auto& x : c.circuit.dessins)
    if (veut(x.couche)) {
      const auto p = contourDessin(x);
      const bool ferme = x.forme != DessinCircuit::Forme::Trait && x.forme != DessinCircuit::Forme::Arc && x.forme != DessinCircuit::Forme::Bezier;
      d.polyligne(codeCouche(x.couche), pts(Polygone(p.begin(), p.end())), ferme, mm(x.epaisseur));
    }
  for (const auto& t : textesSerigraphie(c))
    if (veut(t.couche)) d.ligne(codeCouche(t.couche), X(t.a.x), Y(t.a.y), X(t.b.x), Y(t.b.y));
  if (j.cotes)
    for (const auto& x : c.circuit.cotes) {
      if (x.points.size() < 2) continue;
      d.ligne("Cotes", X(x.points[0].x), Y(x.points[0].y), X(x.points[1].x), Y(x.points[1].y));
      const Pt m{(x.points[0].x + x.points[1].x) / 2, (x.points[0].y + x.points[1].y) / 2};
      d.texte("Cotes", X(m.x), Y(m.y), mm(x.hauteurTexte), texteCote(x));
    }
  return d.texte();
}

std::vector<JeuDxf> jeuxDxf(const ConceptionElec& c) {
  std::vector<JeuDxf> r;
  const auto it = c.extensions.find("jeuxDxf");
  if (it == c.extensions.end() || !it->second.is_array()) return r;
  for (const auto& j : it->second) {
    JeuDxf x;
    x.nom = j.value("nom", "");
    x.miroir = j.value("miroir", false);
    x.cotes = j.value("cotes", true);
    x.contour = j.value("contour", true);
    if (j.contains("couches"))
      for (const auto& k : j.at("couches"))
        if (const auto cc = coucheDepuisCode(k.get<std::string>())) x.couches.push_back(*cc);
    r.push_back(x);
  }
  return r;
}

void enregistrerJeuDxf(ConceptionElec& c, const JeuDxf& j) {
  auto& t = c.extensions["jeuxDxf"];
  if (!t.is_array()) t = nlohmann::ordered_json::array();
  nlohmann::ordered_json x{{"nom", j.nom}, {"miroir", j.miroir}, {"cotes", j.cotes}, {"contour", j.contour}, {"couches", nlohmann::ordered_json::array()}};
  for (Couche k : j.couches) x["couches"].push_back(codeCouche(k));
  for (auto& e : t)
    if (e.value("nom", "") == j.nom) {
      e = x;
      return;
    }
  t.push_back(x);
}

}  // namespace prom::ecad
