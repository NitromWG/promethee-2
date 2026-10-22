// Prométhée : lecture d'un contour de carte dans un fichier DXF. Licence GPL-3.0-only.
#include "promethee/dxf.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <sstream>

namespace prom::ecad {
namespace {

struct P {
  double x = 0, y = 0;
};
double dist(P a, P b) { return std::hypot(a.x - b.x, a.y - b.y); }

constexpr double TOLERANCE = 0.01;  // mm : extrémités considérées comme jointes
constexpr double FLECHE = 0.005;    // mm : écart maximal entre un arc et ses cordes

// Points d'un arc de centre c, rayon r, depuis l'angle a0 sur un balayage signé (radians), extrémités comprises
std::vector<P> arc(P c, double r, double a0, double balayage) {
  double pas = r > FLECHE ? 2 * std::acos(1 - FLECHE / r) : std::numbers::pi / 18;
  pas = std::clamp(pas, std::numbers::pi / 180, std::numbers::pi / 18);
  const int n = std::max(2, static_cast<int>(std::ceil(std::abs(balayage) / pas)));
  std::vector<P> v;
  for (int k = 0; k <= n; ++k) {
    const double a = a0 + balayage * k / n;
    v.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
  }
  return v;
}

// Segment p1 → p2 avec renflement (bulge) : tangente de l'angle au centre / 4, positif dans le sens direct
void ajouterSegment(std::vector<P>& v, P p1, P p2, double renflement) {
  if (std::abs(renflement) < 1e-9 || dist(p1, p2) < 1e-12) {
    if (v.empty()) v.push_back(p1);
    v.push_back(p2);
    return;
  }
  const double theta = 4 * std::atan(renflement);
  const double c = dist(p1, p2);
  const P d{(p2.x - p1.x) / c, (p2.y - p1.y) / c};
  const P n{-d.y, d.x};
  const double h = c / 2 / std::tan(theta / 2);
  const P centre{(p1.x + p2.x) / 2 + n.x * h, (p1.y + p2.y) / 2 + n.y * h};
  const double r = dist(centre, p1);
  const auto pts = arc(centre, r, std::atan2(p1.y - centre.y, p1.x - centre.x), theta);
  if (v.empty()) v.push_back(p1);
  for (std::size_t k = 1; k < pts.size(); ++k) v.push_back(pts[k]);
  v.back() = p2;
}

struct Morceau {
  std::vector<P> pts;
  bool ferme = false;
};

double nombre(const std::string& s) {
  try {
    return std::stod(s);
  } catch (...) {
    return 0;
  }
}

}  // namespace

ContourDxf lireContourDxf(const std::string& texte) {
  ContourDxf r;
  // Paires (code de groupe, valeur)
  std::vector<std::pair<int, std::string>> paires;
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
        r.alertes.push_back("Fichier DXF mal formé : code de groupe « " + code + " » illisible.");
        return r;
      }
    }
  }
  // Unité ($INSUNITS) : 1 pouce, 2 pied, 4 mm, 5 cm, 6 m ; 0 ou absent : mm
  double facteur = 1;
  for (std::size_t i = 0; i + 1 < paires.size(); ++i)
    if (paires[i].first == 9 && paires[i].second == "$INSUNITS" && paires[i + 1].first == 70) {
      switch (static_cast<int>(nombre(paires[i + 1].second))) {
        case 1: facteur = 25.4; break;
        case 2: facteur = 304.8; break;
        case 5: facteur = 10; break;
        case 6: facteur = 1000; break;
        case 8: facteur = 25.4e-6; break;   // micro-pouce
        case 9: facteur = 0.0254; break;    // mil
        case 13: facteur = 1e-3; break;     // micron
        default: break;
      }
    }
  // Entités de la section ENTITIES (et des blocs, ignorés)
  std::vector<Morceau> morceaux;
  std::map<std::string, int> ignorees;
  bool entites = false;
  std::size_t i = 0;
  auto entite = [&](std::size_t debut) {
    std::vector<std::pair<int, std::string>> e;
    std::size_t k = debut + 1;
    while (k < paires.size() && paires[k].first != 0) e.push_back(paires[k++]);
    return std::pair{e, k};
  };
  auto valeur = [](const std::vector<std::pair<int, std::string>>& e, int code, double defaut = 0) {
    for (const auto& [c, v] : e)
      if (c == code) return nombre(v);
    return defaut;
  };
  while (i < paires.size()) {
    if (paires[i].first != 0) { ++i; continue; }
    const std::string type = paires[i].second;
    if (type == "SECTION") {
      entites = i + 1 < paires.size() && paires[i + 1].first == 2 && paires[i + 1].second == "ENTITIES";
      ++i;
      continue;
    }
    if (type == "ENDSEC") { entites = false; ++i; continue; }
    if (!entites) { ++i; continue; }
    auto [e, suivant] = entite(i);
    if (type == "LINE") {
      morceaux.push_back({{{valeur(e, 10), valeur(e, 20)}, {valeur(e, 11), valeur(e, 21)}}, false});
    } else if (type == "ARC") {
      const P c{valeur(e, 10), valeur(e, 20)};
      const double a0 = valeur(e, 50) * std::numbers::pi / 180;
      double a1 = valeur(e, 51) * std::numbers::pi / 180;
      while (a1 <= a0) a1 += 2 * std::numbers::pi;
      morceaux.push_back({arc(c, valeur(e, 40), a0, a1 - a0), false});
    } else if (type == "CIRCLE") {
      auto pts = arc({valeur(e, 10), valeur(e, 20)}, valeur(e, 40), 0, 2 * std::numbers::pi);
      pts.pop_back();
      morceaux.push_back({pts, true});
    } else if (type == "LWPOLYLINE") {
      std::vector<P> sommets;
      std::vector<double> renflements;
      for (const auto& [c, v] : e) {
        if (c == 10) { sommets.push_back({nombre(v), 0}); renflements.push_back(0); }
        else if (c == 20 && !sommets.empty()) sommets.back().y = nombre(v);
        else if (c == 42 && !renflements.empty()) renflements.back() = nombre(v);
      }
      const bool ferme = static_cast<int>(valeur(e, 70)) & 1;
      Morceau m;
      m.ferme = ferme;
      for (std::size_t k = 0; k + 1 < sommets.size(); ++k) ajouterSegment(m.pts, sommets[k], sommets[k + 1], renflements[k]);
      if (ferme && sommets.size() >= 2) ajouterSegment(m.pts, sommets.back(), sommets.front(), renflements.back());
      if (sommets.size() == 1) m.pts = sommets;
      if (ferme && m.pts.size() > 1 && dist(m.pts.front(), m.pts.back()) < 1e-9) m.pts.pop_back();
      morceaux.push_back(m);
    } else if (type == "POLYLINE") {
      // Polyligne R12 : sommets dans les entités VERTEX qui suivent, jusqu'à SEQEND
      const bool ferme = static_cast<int>(valeur(e, 70)) & 1;
      std::vector<P> sommets;
      std::vector<double> renflements;
      std::size_t k = suivant;
      while (k < paires.size() && paires[k].first == 0 && paires[k].second == "VERTEX") {
        auto [ev, apres] = entite(k);
        sommets.push_back({valeur(ev, 10), valeur(ev, 20)});
        renflements.push_back(valeur(ev, 42));
        k = apres;
      }
      if (k < paires.size() && paires[k].first == 0 && paires[k].second == "SEQEND") k = entite(k).second;
      suivant = k;
      Morceau m;
      m.ferme = ferme;
      for (std::size_t j = 0; j + 1 < sommets.size(); ++j) ajouterSegment(m.pts, sommets[j], sommets[j + 1], renflements[j]);
      if (ferme && sommets.size() >= 2) ajouterSegment(m.pts, sommets.back(), sommets.front(), renflements.back());
      if (ferme && m.pts.size() > 1 && dist(m.pts.front(), m.pts.back()) < 1e-9) m.pts.pop_back();
      morceaux.push_back(m);
    } else if (type != "SEQEND" && type != "VERTEX" && type != "EOF") {
      ++ignorees[type];
    }
    i = suivant;
  }
  for (const auto& [type, n] : ignorees) r.alertes.push_back(std::to_string(n) + " entité(s) " + type + " ignorée(s) (seuls lignes, arcs, cercles et polylignes forment le contour).");
  // Enchaînement des morceaux ouverts en boucles
  std::vector<std::vector<P>> boucles;
  std::vector<Morceau> ouverts;
  for (auto& m : morceaux) {
    if (m.pts.size() < 2) continue;
    if (m.ferme || (m.pts.size() >= 3 && dist(m.pts.front(), m.pts.back()) < TOLERANCE)) {
      if (!m.ferme) m.pts.pop_back();
      boucles.push_back(m.pts);
    } else
      ouverts.push_back(m);
  }
  std::vector<bool> pris(ouverts.size(), false);
  for (std::size_t a = 0; a < ouverts.size(); ++a) {
    if (pris[a]) continue;
    pris[a] = true;
    std::vector<P> chaine = ouverts[a].pts;
    bool progres = true;
    while (progres && dist(chaine.front(), chaine.back()) >= TOLERANCE) {
      progres = false;
      for (std::size_t b = 0; b < ouverts.size() && !progres; ++b) {
        if (pris[b]) continue;
        const auto& q = ouverts[b].pts;
        if (dist(chaine.back(), q.front()) < TOLERANCE) chaine.insert(chaine.end(), q.begin() + 1, q.end());
        else if (dist(chaine.back(), q.back()) < TOLERANCE) chaine.insert(chaine.end(), q.rbegin() + 1, q.rend());
        else if (dist(chaine.front(), q.back()) < TOLERANCE) chaine.insert(chaine.begin(), q.begin(), q.end() - 1);
        else if (dist(chaine.front(), q.front()) < TOLERANCE) chaine.insert(chaine.begin(), q.rbegin(), q.rend() - 1);
        else continue;
        pris[b] = true;
        progres = true;
      }
    }
    if (chaine.size() >= 3 && dist(chaine.front(), chaine.back()) < TOLERANCE) {
      chaine.pop_back();
      boucles.push_back(chaine);
    } else {
      std::ostringstream m;
      m.precision(3);
      m << std::fixed << "Tracé ouvert ignoré : de (" << chaine.front().x << " ; " << chaine.front().y << ") à (" << chaine.back().x << " ; " << chaine.back().y << ").";
      r.alertes.push_back(m.str());
    }
  }
  // En coordonnées de la carte (nm, Y vers le bas), la plus grande boucle d'abord
  Polygones polys;
  for (const auto& b : boucles) {
    Polygone p;
    for (const P& q : b) {
      const Pt x{nm(q.x * facteur), nm(-q.y * facteur)};
      if (p.empty() || !(p.back() == x)) p.push_back(x);
    }
    while (p.size() > 1 && p.front() == p.back()) p.pop_back();
    if (p.size() >= 3 && aireMm2(p) > 1e-6) polys.push_back(orienter(p));
  }
  if (polys.empty()) {
    r.alertes.push_back("Aucune boucle fermée dans le fichier : le contour doit être un tracé fermé.");
    return r;
  }
  std::stable_sort(polys.begin(), polys.end(), [](const Polygone& a, const Polygone& b) { return aireMm2(a) > aireMm2(b); });
  r.exterieur = polys.front();
  for (std::size_t k = 1; k < polys.size(); ++k) {
    const Polygone& d = polys[k];
    if (!dedans(r.exterieur, d.front())) {
      r.alertes.push_back("Boucle hors du contour ignorée (" + std::to_string(static_cast<long long>(std::lround(aireMm2(d)))) + " mm²).");
      continue;
    }
    bool ile = false;
    for (const auto& autre : r.decoupes) ile |= dedans(autre, d.front());
    if (ile) {
      r.alertes.push_back("Boucle à l'intérieur d'une découpe ignorée (une carte ne garde pas d'île).");
      continue;
    }
    r.decoupes.push_back(d);
  }
  return r;
}

}  // namespace prom::ecad
