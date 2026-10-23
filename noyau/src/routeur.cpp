// Prométhée : routeur interactif, contourner et pousser (lot L5). Licence GPL-3.0-only.
#include "promethee/routeur.hpp"
#include "promethee/entier128.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>

namespace prom::ecad {

namespace {
struct Obstacle {
  enum class Genre : std::uint8_t { Pastille, Piste, Via } genre = Genre::Piste;
  std::string reseau, nom, uuid;
  Polygone forme;
  Pt a, b;
  Nm demi = 0;
  Boite boite;
};

// Obstacles d'une couche, pour un réseau, dans une région
struct Terrain {
  const ConceptionElec& c;
  const OptionsTrace& o;
  Nm w2 = 0;
  std::vector<Obstacle> obs;
  std::vector<std::pair<Pt, Pt>> bords;
  std::vector<const PastillePlacee*> propres;  // pastilles du réseau (départ et arrivée)
  std::vector<PastillePlacee> pp;

  Terrain(const ConceptionElec& cc, const OptionsTrace& oo, const Boite& region) : c(cc), o(oo) {
    w2 = (o.largeur > 0 ? o.largeur : largeurPiste(c.circuit.regles, o.reseau)) / 2;
    const Nm marge = w2 + c.circuit.regles.isolement * 4 + 2 * NM_PAR_MM;
    const Boite zone = region.gonflee(marge);
    if (o.pastilles) pp = *o.pastilles;
    else pp = pastilles(c, connectivite(c));
    for (const auto& p : pp) {
      if (!(p.couches & bit(o.couche))) continue;
      if (!p.reseau.empty() && p.reseau == o.reseau) {
        propres.push_back(&p);
        continue;
      }
      Obstacle x;
      x.genre = Obstacle::Genre::Pastille;
      x.reseau = p.reseau;
      x.nom = "la pastille " + p.reference + "." + p.numero;
      x.forme = p.forme;
      x.boite = boite(p.forme);
      if (x.boite.touche(zone)) obs.push_back(std::move(x));
    }
    auto ajouterPiste = [&](const Piste& t) {
      if (t.couche != o.couche || (!t.reseau.empty() && t.reseau == o.reseau) || o.ignorees.count(t.uuid)) return;
      Obstacle x;
      x.genre = Obstacle::Genre::Piste;
      x.reseau = t.reseau;
      x.uuid = t.uuid;
      x.nom = "une piste du réseau « " + t.reseau + " »";
      x.a = t.a;
      x.b = t.b;
      x.demi = t.largeur / 2;
      x.boite.etendre(t.a);
      x.boite.etendre(t.b);
      x.boite = x.boite.gonflee(x.demi);
      if (x.boite.touche(zone)) obs.push_back(std::move(x));
    };
    for (const auto& t : c.circuit.pistes) ajouterPiste(t);
    for (const auto& t : o.obstaclesEnPlus) ajouterPiste(t);
    for (const auto& v : c.circuit.vias) {
      if (!v.reseau.empty() && v.reseau == o.reseau) continue;
      Obstacle x;
      x.genre = Obstacle::Genre::Via;
      x.reseau = v.reseau;
      x.uuid = v.uuid;
      x.nom = "une via du réseau « " + v.reseau + " »";
      x.a = x.b = v.p;
      x.demi = v.diametre / 2;
      x.boite.etendre(v.p);
      x.boite = x.boite.gonflee(x.demi);
      if (x.boite.touche(zone)) obs.push_back(std::move(x));
    }
    bords = c.circuit.contour.aretes();
  }
  double iso(const std::string& r) const { return static_cast<double>(isolementEntre(c.circuit.regles, o.reseau, r)); }
  // Distance du point (ou du segment) à l'obstacle, moins l'isolement voulu : négative s'il gêne
  double jeu(const Obstacle& x, Pt a, Pt b) const {
    const double w = static_cast<double>(w2);
    switch (x.genre) {
      case Obstacle::Genre::Pastille: return distancePolygoneSegment(x.forme, a, b) - w - iso(x.reseau);
      case Obstacle::Genre::Piste: return distanceSegments(a, b, x.a, x.b) - w - static_cast<double>(x.demi) - iso(x.reseau);
      case Obstacle::Genre::Via: return distancePointSegment(x.a, a, b) - w - static_cast<double>(x.demi) - iso(x.reseau);
    }
    return 0;
  }
  bool libre(Pt a, Pt b, std::string* qui = nullptr) const {
    if (!c.circuit.contour.contient(a) || !c.circuit.contour.contient(b)) {
      if (qui) *qui = "le bord de la carte (tracé hors de la matière)";
      return false;
    }
    for (const auto& [u, v] : bords)
      if (distanceSegments(u, v, a, b) - static_cast<double>(w2) < static_cast<double>(c.circuit.regles.bord)) {
        if (qui) *qui = "le bord de la carte";
        return false;
      }
    Boite sb;
    sb.etendre(a);
    sb.etendre(b);
    sb = sb.gonflee(w2 + c.circuit.regles.isolement * 4 + NM_PAR_MM);
    for (const auto& x : obs) {
      if (!x.boite.touche(sb)) continue;
      if (jeu(x, a, b) < -1) {  // 1 nm de tolérance d'arrondi
        if (qui) *qui = x.nom;
        return false;
      }
    }
    return true;
  }
};

Nm signe(Nm v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
bool octilineaire(Pt a, Pt b) {
  const Nm dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
  return dx == 0 || dy == 0 || dx == dy;
}
// Liaisons à 45° entre deux points : droite si elle l'est, sinon deux segments (diagonale d'abord, droit d'abord)
std::vector<std::vector<Pt>> liaisons(Pt a, Pt b) {
  std::vector<std::vector<Pt>> r;
  if (octilineaire(a, b)) {
    r.push_back({a, b});
    return r;
  }
  const Nm dx = b.x - a.x, dy = b.y - a.y, ax = std::abs(dx), ay = std::abs(dy), d = std::min(ax, ay);
  const Pt m1{a.x + signe(dx) * d, a.y + signe(dy) * d};
  const Pt m2 = ax > ay ? Pt{a.x + signe(dx) * (ax - d), a.y} : Pt{a.x, a.y + signe(dy) * (ay - d)};
  r.push_back({a, m1, b});
  r.push_back({a, m2, b});
  return r;
}
std::vector<Pt> sansAlignes(const std::vector<Pt>& p) {
  std::vector<Pt> r;
  for (const Pt q : p) {
    if (!r.empty() && r.back() == q) continue;
    while (r.size() >= 2) {
      const Pt u = r[r.size() - 2], v = r.back();
      using Grand = Entier128;
      const Grand croix = static_cast<Grand>(v.x - u.x) * (q.y - v.y) - static_cast<Grand>(v.y - u.y) * (q.x - v.x);
      const Grand sc = static_cast<Grand>(v.x - u.x) * (q.x - v.x) + static_cast<Grand>(v.y - u.y) * (q.y - v.y);
      if (croix == 0 && sc >= 0) r.pop_back();
      else break;
    }
    r.push_back(q);
  }
  return r;
}
}  // namespace

double ResultatTrace::longueur() const {
  double l = 0;
  for (std::size_t i = 1; i < points.size(); ++i) l += distance(points[i - 1], points[i]);
  return l;
}

std::vector<Piste> pistesDuTrace(const std::vector<Pt>& points, const std::string& reseau, Couche couche, Nm largeur) {
  std::vector<Piste> r;
  for (std::size_t i = 1; i < points.size(); ++i)
    if (points[i] != points[i - 1]) r.push_back({nouvelUuid(), reseau, couche, points[i - 1], points[i], largeur});
  return r;
}

bool segmentLibre(const ConceptionElec& c, Pt a, Pt b, const OptionsTrace& o, std::string* obstacle) {
  Boite r;
  r.etendre(a);
  r.etendre(b);
  const Terrain t(c, o, r);
  return t.libre(a, b, obstacle);
}

namespace {
ResultatTrace contournerDans(const ConceptionElec& c, Pt a, Pt b, const OptionsTrace& o);
}

ResultatTrace contourner(const ConceptionElec& c, Pt a, Pt b, const OptionsTrace& o) {
  ResultatTrace r = contournerDans(c, a, b, o);
  if (r.ok) return r;
  // Pas de passage près des bouts : la recherche s'étend à toute la carte
  OptionsTrace large = o;
  large.etendue = std::max<Nm>(o.etendue * 4, 200 * NM_PAR_MM);
  const ResultatTrace r2 = contournerDans(c, a, b, large);
  return r2.ok ? r2 : r;
}

namespace {
ResultatTrace contournerDans(const ConceptionElec& c, Pt a, Pt b, const OptionsTrace& o) {
  ResultatTrace res;
  Boite region;
  region.etendre(a);
  region.etendre(b);
  const Boite carte = boite(c.circuit.contour.polygone());
  region = region.gonflee(o.etendue);
  if (!carte.vide()) {
    region.x0 = std::max(region.x0, carte.x0);
    region.y0 = std::max(region.y0, carte.y0);
    region.x1 = std::min(region.x1, carte.x1);
    region.y1 = std::min(region.y1, carte.y1);
  }
  if (!region.contient(a) || !region.contient(b)) {
    res.raison = "un bout est hors de la carte";
    return res;
  }
  const Terrain t(c, o, region);
  // Tracé direct : rien à chercher
  for (const auto& l : liaisons(a, b)) {
    bool ok = true;
    for (std::size_t i = 1; i < l.size() && ok; ++i) ok = t.libre(l[i - 1], l[i]);
    if (ok) {
      res.ok = true;
      res.points = l;
      return res;
    }
  }
  // Grille alignée sur le départ
  const Nm isoMin = c.circuit.regles.isolement;
  Nm s = o.pas > 0 ? o.pas : std::clamp<Nm>((t.w2 + isoMin) / 3 / 5'000 * 5'000, 25'000, 250'000);
  const Nm i0 = -((a.x - region.x0) / s), i1 = (region.x1 - a.x) / s, j0 = -((a.y - region.y0) / s), j1 = (region.y1 - a.y) / s;
  const std::int64_t nx = i1 - i0 + 1, ny = j1 - j0 + 1;
  if (nx <= 1 || ny <= 1 || nx * ny > 3'000'000) {
    res.raison = "région de recherche hors de la carte ou trop grande";
    return res;
  }
  auto indice = [&](Nm i, Nm j) { return static_cast<std::size_t>((j - j0) * nx + (i - i0)); };
  auto point = [&](Nm i, Nm j) { return Pt{a.x + i * s, a.y + j * s}; };
  std::vector<std::uint8_t> bloque(static_cast<std::size_t>(nx * ny), 0);
  const double marge = static_cast<double>(s) * 0.7072;
  // Bord de la carte
  for (Nm j = j0; j <= j1; ++j)
    for (Nm i = i0; i <= i1; ++i)
      if (!c.circuit.contour.contient(point(i, j))) bloque[indice(i, j)] = 1;
  auto marquer = [&](const Boite& bx, const std::function<double(Pt)>& jeu) {
    const Nm ia = std::max(i0, (bx.x0 - a.x) / s - 1), ib = std::min(i1, (bx.x1 - a.x) / s + 1);
    const Nm ja = std::max(j0, (bx.y0 - a.y) / s - 1), jb = std::min(j1, (bx.y1 - a.y) / s + 1);
    for (Nm j = ja; j <= jb; ++j)
      for (Nm i = ia; i <= ib; ++i) {
        auto& x = bloque[indice(i, j)];
        if (!x && jeu(point(i, j)) < marge) x = 1;
      }
  };
  for (const auto& [u, v] : t.bords) {
    Boite bx;
    bx.etendre(u);
    bx.etendre(v);
    const Nm g = t.w2 + c.circuit.regles.bord + s * 2;
    marquer(bx.gonflee(g), [&](Pt p) { return distancePointSegment(p, u, v) - static_cast<double>(t.w2) - static_cast<double>(c.circuit.regles.bord); });
  }
  for (const auto& x : t.obs) {
    const Nm g = t.w2 + static_cast<Nm>(t.iso(x.reseau)) + s * 2;
    marquer(x.boite.gonflee(g), [&](Pt p) { return t.jeu(x, p, p); });
  }
  // Les pastilles du réseau qui portent les bouts restent praticables (sortie d'une pastille fine)
  for (const PastillePlacee* p : t.propres)
    if (dedans(p->forme, a) || dedans(p->forme, b)) {
      const Boite bx = boite(p->forme);
      const Nm ia = std::max(i0, (bx.x0 - a.x) / s - 1), ib = std::min(i1, (bx.x1 - a.x) / s + 1);
      const Nm ja = std::max(j0, (bx.y0 - a.y) / s - 1), jb = std::min(j1, (bx.y1 - a.y) / s + 1);
      for (Nm j = ja; j <= jb; ++j)
        for (Nm i = ia; i <= ib; ++i)
          if (dedans(p->forme, point(i, j))) bloque[indice(i, j)] = 0;
    }
  const Nm gi = std::clamp<Nm>((b.x - a.x + (b.x >= a.x ? s / 2 : -s / 2)) / s, i0, i1);
  const Nm gj = std::clamp<Nm>((b.y - a.y + (b.y >= a.y ? s / 2 : -s / 2)) / s, j0, j1);
  bloque[indice(0, 0)] = 0;
  bloque[indice(gi, gj)] = 0;
  // A* à huit directions, coudes pénalisés, angle aigu interdit
  static const int DX[8] = {1, 1, 0, -1, -1, -1, 0, 1}, DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  const std::size_t n = bloque.size();
  std::vector<float> g(n * 9, std::numeric_limits<float>::infinity());
  std::vector<std::int32_t> parent(n * 9, -1);
  using Etat = std::pair<float, std::int64_t>;
  std::priority_queue<Etat, std::vector<Etat>, std::greater<>> file;
  const double sd = static_cast<double>(s);
  auto h = [&](Nm i, Nm j) {
    const double dx = static_cast<double>(std::abs(i - gi)), dy = static_cast<double>(std::abs(j - gj));
    return static_cast<float>(sd * (std::max(dx, dy) + (std::sqrt(2.0) - 1) * std::min(dx, dy)));
  };
  const std::int64_t depart = static_cast<std::int64_t>(indice(0, 0)) * 9 + 8;
  g[static_cast<std::size_t>(depart)] = 0;
  file.push({h(0, 0), depart});
  std::int64_t fin = -1;
  std::size_t explores = 0;
  while (!file.empty()) {
    const auto [f, e] = file.top();
    file.pop();
    const std::size_t noeud = static_cast<std::size_t>(e / 9);
    const int dir = static_cast<int>(e % 9);
    const Nm i = static_cast<Nm>(noeud % static_cast<std::size_t>(nx)) + i0, j = static_cast<Nm>(noeud / static_cast<std::size_t>(nx)) + j0;
    const float ge = g[static_cast<std::size_t>(e)];
    if (f > ge + h(i, j) + 1e-3f) continue;
    if (i == gi && j == gj) {
      fin = e;
      break;
    }
    if (++explores > 6'000'000) break;
    for (int d = 0; d < 8; ++d) {
      int virage = 0;
      if (dir < 8) {
        virage = std::abs(d - dir);
        virage = std::min(virage, 8 - virage);
        if (virage > 2) continue;  // pas d'angle aigu
      }
      const Nm ni = i + DX[d], nj = j + DY[d];
      if (ni < i0 || ni > i1 || nj < j0 || nj > j1) continue;
      const std::size_t nn = indice(ni, nj);
      if (bloque[nn]) continue;
      const float pasLong = static_cast<float>(d % 2 ? sd * std::sqrt(2.0) : sd);
      const float penalite = static_cast<float>(virage == 1 ? sd * 0.4 : virage == 2 ? sd * 2.0 : 0);
      const std::int64_t ne = static_cast<std::int64_t>(nn) * 9 + d;
      const float ng = ge + pasLong + penalite;
      if (ng < g[static_cast<std::size_t>(ne)]) {
        g[static_cast<std::size_t>(ne)] = ng;
        parent[static_cast<std::size_t>(ne)] = static_cast<std::int32_t>(e);
        file.push({ng + h(ni, nj), ne});
      }
    }
  }
  if (fin < 0) {
    std::string qui;
    for (const auto& l : liaisons(a, b))
      for (std::size_t k = 1; k < l.size() && qui.empty(); ++k) t.libre(l[k - 1], l[k], &qui);
    res.raison = "aucun passage à l’isolement sur " + std::string(nomCouche(o.couche)) + (qui.empty() ? std::string() : " (le tracé direct touche " + qui + ")");
    return res;
  }
  std::vector<Pt> brut;
  for (std::int64_t e = fin; e >= 0; e = parent[static_cast<std::size_t>(e)]) {
    const std::size_t noeud = static_cast<std::size_t>(e / 9);
    brut.push_back(point(static_cast<Nm>(noeud % static_cast<std::size_t>(nx)) + i0, static_cast<Nm>(noeud / static_cast<std::size_t>(nx)) + j0));
  }
  std::reverse(brut.begin(), brut.end());
  if (brut.back() != b) brut.push_back(b);
  const std::vector<Pt> p = sansAlignes(brut);
  // Raccourcis à 45° vérifiés exactement, du plus loin au plus proche
  std::vector<Pt> r{p.front()};
  std::size_t i = 0;
  while (i + 1 < p.size()) {
    bool avance = false;
    for (std::size_t j = p.size() - 1; j > i && !avance; --j)
      for (const auto& l : liaisons(p[i], p[j])) {
        bool ok = true;
        for (std::size_t k = 1; k < l.size() && ok; ++k) ok = t.libre(l[k - 1], l[k]);
        if (!ok) continue;
        r.insert(r.end(), l.begin() + 1, l.end());
        i = j;
        avance = true;
        break;
      }
    if (!avance) {
      r.push_back(p[i + 1]);
      ++i;
    }
  }
  res.points = sansAlignes(r);
  for (std::size_t k = 1; k < res.points.size(); ++k) {
    std::string qui;
    if (!t.libre(res.points[k - 1], res.points[k], &qui)) {
      res.raison = "le passage trouvé frôle " + qui + " sous l’isolement";
      return res;
    }
  }
  res.ok = true;
  return res;
}
}  // namespace

ResultatPoussee pousser(ConceptionElec& c, Pt a, Pt b, const OptionsTrace& o0) {
  exigerModifiable(c);
  ResultatPoussee res;
  OptionsTrace o = o0;
  if (o.largeur <= 0) o.largeur = largeurPiste(c.circuit.regles, o.reseau);
  std::string derniere;
  for (const auto& l : liaisons(a, b)) {
    // Ce qui gêne le tracé : des pistes seulement (une pastille, une via ou le bord ne se poussent pas)
    Boite region;
    for (const Pt q : l) region.etendre(q);
    const Terrain t(c, o, region);
    std::set<std::string> genantes;
    bool fixe = false;
    for (std::size_t k = 1; k < l.size() && !fixe; ++k) {
      std::string qui;
      if (!c.circuit.contour.contient(l[k - 1]) || !c.circuit.contour.contient(l[k])) fixe = true;
      for (const auto& [u, v] : t.bords)
        if (distanceSegments(u, v, l[k - 1], l[k]) - static_cast<double>(t.w2) < static_cast<double>(c.circuit.regles.bord)) fixe = true;
      for (const auto& x : t.obs)
        if (t.jeu(x, l[k - 1], l[k]) < -1) {
          if (x.genre == Obstacle::Genre::Piste) genantes.insert(x.uuid);
          else {
            fixe = true;
            derniere = x.nom + " ne se pousse pas";
          }
        }
    }
    if (fixe) continue;
    ConceptionElec essai = c;
    const std::vector<Piste> nouvelles = pistesDuTrace(l, o.reseau, o.couche, o.largeur);
    essai.circuit.pistes.insert(essai.circuit.pistes.end(), nouvelles.begin(), nouvelles.end());
    std::set<std::string> faites;
    bool ok = true;
    std::vector<std::string> deplacees;
    for (const std::string& u : genantes) {
      if (faites.count(u) || !ok) continue;
      // La chaîne de pistes du même réseau et de la même couche, reliées bout à bout
      const auto it = std::find_if(essai.circuit.pistes.begin(), essai.circuit.pistes.end(), [&](const Piste& p) { return p.uuid == u; });
      if (it == essai.circuit.pistes.end()) continue;
      const Piste depart = *it;
      std::set<std::string> chaine{u};
      for (bool encore = true; encore;) {
        encore = false;
        for (const auto& p : essai.circuit.pistes) {
          if (chaine.count(p.uuid) || p.reseau != depart.reseau || p.couche != depart.couche) continue;
          for (const auto& q : essai.circuit.pistes)
            if (chaine.count(q.uuid) && (p.a == q.a || p.a == q.b || p.b == q.a || p.b == q.b)) {
              chaine.insert(p.uuid);
              encore = true;
              break;
            }
        }
      }
      std::map<Pt, int> degre;
      Nm largeur = 0;
      for (const auto& p : essai.circuit.pistes)
        if (chaine.count(p.uuid)) {
          ++degre[p.a];
          ++degre[p.b];
          largeur = std::max(largeur, p.largeur);
        }
      std::vector<Pt> bouts;
      for (const auto& [pt, d] : degre)
        if (d == 1) bouts.push_back(pt);
      if (bouts.size() != 2) {
        ok = false;
        derniere = "la piste du réseau « " + depart.reseau + " » se ramifie : elle ne se pousse pas d’un bloc";
        break;
      }
      OptionsTrace ob;
      ob.reseau = depart.reseau;
      ob.couche = depart.couche;
      ob.largeur = largeur;
      ob.ignorees = chaine;
      ob.etendue = o.etendue;
      // Le bout le plus proche du départ d'origine d'abord : le tracé suit le sens de la piste
      const ResultatTrace r = contourner(essai, bouts[0], bouts[1], ob);
      if (!r.ok) {
        ok = false;
        derniere = "la piste du réseau « " + depart.reseau + " » ne trouve pas de place pour s’écarter (" + r.raison + ")";
        break;
      }
      std::erase_if(essai.circuit.pistes, [&](const Piste& p) { return chaine.count(p.uuid) > 0; });
      for (const auto& p : pistesDuTrace(r.points, depart.reseau, depart.couche, largeur)) essai.circuit.pistes.push_back(p);
      faites.insert(chaine.begin(), chaine.end());
      deplacees.push_back(depart.reseau);
    }
    if (!ok) continue;
    // Contrôle final : chaque piste du tracé et des réseaux écartés, à l'isolement
    bool sain = true;
    for (const auto& p : essai.circuit.pistes) {
      const bool concernee = p.reseau == o.reseau || std::find(deplacees.begin(), deplacees.end(), p.reseau) != deplacees.end();
      if (!concernee || p.couche != o.couche) continue;
      OptionsTrace v;
      v.reseau = p.reseau;
      v.couche = p.couche;
      v.largeur = p.largeur;
      if (!segmentLibre(essai, p.a, p.b, v, &derniere)) {
        sain = false;
        break;
      }
    }
    if (!sain) continue;
    c = std::move(essai);
    res.ok = true;
    res.points = l;
    res.deplacees = deplacees;
    return res;
  }
  res.raison = derniere.empty() ? "aucun tracé direct ne se fait de place" : derniere;
  return res;
}

std::string routerLiaison(ConceptionElec& c, const Liaison& l) {
  exigerModifiable(c);
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const PastillePlacee *pa = nullptr, *pb = nullptr;
  for (const auto& p : pp) {
    if (p.reseau != l.reseau) continue;
    if (p.pastille.position == l.a) pa = &p;
    if (p.pastille.position == l.b) pb = &p;
  }
  if (!pa || !pb) return "une extrémité n’est pas une pastille (piste ou zone) : à relier à la main";
  std::string raisons;
  bool commune = false;
  for (const Couche L : {Couche::CuivreDessus, Couche::CuivreDessous}) {
    if (!(pa->couches & bit(L)) || !(pb->couches & bit(L))) continue;
    commune = true;
    OptionsTrace o;
    o.reseau = l.reseau;
    o.couche = L;
    o.largeur = largeurPiste(c.circuit.regles, l.reseau);
    o.etendue = 20 * NM_PAR_MM;
    const ResultatTrace r = contourner(c, l.a, l.b, o);
    if (r.ok) {
      for (const auto& p : pistesDuTrace(r.points, l.reseau, L, o.largeur)) c.circuit.pistes.push_back(p);
      return {};
    }
    raisons += (raisons.empty() ? "" : " ; ") + r.raison;
  }
  if (!commune) return "les deux pastilles ne partagent aucune couche de cuivre : une via est nécessaire, à poser à la main";
  return raisons;
}

}  // namespace prom::ecad
