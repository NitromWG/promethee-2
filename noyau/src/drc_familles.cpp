// Prométhée : familles de contrôles de la DRC du lot L8 de la 0.34 (8.10, 8.25). Voir drc_familles.hpp.
// Licence GPL-3.0-only.
#include "promethee/drc_familles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <queue>
#include <set>

#include "promethee/contraintes.hpp"
#include "promethee/expressions.hpp"
#include "promethee/regles_carte.hpp"
#include "promethee/signaux.hpp"
#include "promethee/topologie_lignes.hpp"

namespace prom::ecad {

namespace {
std::string mmTexte(double v, int d = 2) {
  char t[32];
  std::snprintf(t, sizeof t, "%.*f", d, v);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
Pt milieuDe(Pt a, Pt b) { return {(a.x + b.x) / 2, (a.y + b.y) / 2}; }
bool dansPortee(const std::string& portee, const Proprietes& p) {
  if (portee.empty()) return true;
  const Expression e = Expression::compiler(portee);
  return e.valide() && e.evaluer(p);
}
// Points du contour tous les « pas » au plus (les plus courts chemins passent par des sommets)
std::vector<Pt> densifier(const Polygone& p, Nm pas) {
  std::vector<Pt> r;
  for (std::size_t i = 0; i < p.size(); ++i) {
    const Pt a = p[i], b = p[(i + 1) % p.size()];
    const double l = distance(a, b);
    const int n = std::max(1, static_cast<int>(std::ceil(l / static_cast<double>(pas))));
    for (int k = 0; k < n; ++k)
      r.push_back({a.x + static_cast<Nm>(std::llround(static_cast<double>(b.x - a.x) * k / n)), a.y + static_cast<Nm>(std::llround(static_cast<double>(b.y - a.y) * k / n))});
  }
  return r;
}
// Le segment traverse-t-il l'intérieur d'un obstacle ? (longer un bord est permis)
bool traverse(Pt a, Pt b, const Polygones& obstacles) {
  for (const auto& o : obstacles) {
    for (std::size_t i = 0; i < o.size(); ++i) {
      const Pt p = o[i], q = o[(i + 1) % o.size()];
      if (p == a || p == b || q == a || q == b) continue;
      if (segmentsSeCoupent(a, b, p, q) && !surSegment(a, p, q) && !surSegment(b, p, q) && !surSegment(p, a, b) && !surSegment(q, a, b)) return true;
    }
    // Les deux bouts sur le bord, le milieu dedans : le segment coupe l'obstacle
    const Pt m = milieuDe(a, b);
    bool surBord = false;
    for (std::size_t i = 0; i < o.size() && !surBord; ++i) surBord = surSegment(m, o[i], o[(i + 1) % o.size()]);
    if (!surBord && dedans(o, m)) return true;
  }
  return false;
}
}  // namespace

// ---------- Ligne de fuite ----------
ReglagesSurface reglagesSurface(const ConceptionElec& c) {
  ReglagesSurface r;
  if (const auto it = c.extensions.find("isolementSurface"); it != c.extensions.end()) {
    r.pollution = std::clamp(it->second.value("pollution", r.pollution), 1, 3);
    r.groupe = std::clamp(it->second.value("groupe", r.groupe), 1, 3);
    r.circuitImprime = it->second.value("circuitImprime", r.circuitImprime);
  }
  return r;
}

void reglerSurface(ConceptionElec& c, const ReglagesSurface& r) {
  c.extensions["isolementSurface"] = {{"pollution", r.pollution}, {"groupe", r.groupe}, {"circuitImprime", r.circuitImprime}};
}

double largeurRainureX(int pollution) { return pollution <= 1 ? 0.25 : pollution == 2 ? 1.0 : 1.5; }

double ligneDeFuite(const Polygone& a, const Polygone& b, const Polygones& obstacles) {
  const double direct = distancePolygones(a, b);
  if (direct <= 0) return 0;
  // Visibilité : sommets des deux cuivres (densifiés) et des obstacles (légèrement gonflés) ; Dijkstra des sommets de
  // « a » vers ceux de « b »
  Polygones obs;
  for (const auto& o : gonfler(obstacles, 2'000))
    if (o.size() >= 3) obs.push_back(o);
  // Sans obstacle sur le plus court segment, la ligne de fuite est la distance dans l'air
  const Nm pas = std::max<Nm>(50'000, static_cast<Nm>(direct / 20));
  const std::vector<Pt> pa = densifier(a, pas), pb = densifier(b, pas);
  std::vector<Pt> noeuds;
  std::vector<int> genre;  // 0 : a, 1 : b, 2 : obstacle
  for (const Pt p : pa) noeuds.push_back(p), genre.push_back(0);
  for (const Pt p : pb) noeuds.push_back(p), genre.push_back(1);
  for (const auto& o : obs)
    for (const Pt p : o) noeuds.push_back(p), genre.push_back(2);
  const std::size_t n = noeuds.size();
  std::vector<double> dist(n, std::numeric_limits<double>::infinity());
  using E = std::pair<double, std::size_t>;
  std::priority_queue<E, std::vector<E>, std::greater<E>> file;
  for (std::size_t i = 0; i < n; ++i)
    if (genre[i] == 0) {
      dist[i] = 0;
      file.push({0, i});
    }
  while (!file.empty()) {
    const auto [d, i] = file.top();
    file.pop();
    if (d > dist[i]) continue;
    if (genre[i] == 1) return d * 1e-6;
    for (std::size_t j = 0; j < n; ++j) {
      if (j == i || genre[j] == 0) continue;
      const double nd = d + distance(noeuds[i], noeuds[j]);
      if (nd >= dist[j]) continue;
      if (!obs.empty() && traverse(noeuds[i], noeuds[j], obs)) continue;
      dist[j] = nd;
      file.push({nd, j});
    }
  }
  return std::numeric_limits<double>::infinity();
}

double ligneFuiteExigee(const ConceptionElec& c, const std::string& a, const std::string& b) {
  const ClasseReseau* ka = classeDe(c.circuit.regles, a);
  const ClasseReseau* kb = classeDe(c.circuit.regles, b);
  const double va = ka ? ka->tension : 0, vb = kb ? kb->tension : 0;
  if (va <= 0 && vb <= 0) return 0;
  // Tension de service crête ramenée en efficace (CEI 60664-1 : tension efficace de service)
  const double u = std::abs(va - vb) / std::sqrt(2.0);
  if (u <= 0) return 0;
  const ReglagesSurface r = reglagesSurface(c);
  return ligneFuiteCEI60664(u, r.pollution, r.groupe, r.circuitImprime);
}

// ---------- Taux de cuivre ----------
std::vector<TauxCuivre> tauxCuivre(const ConceptionElec& c, const Connectivite& k, const Remplissages& r) {
  std::vector<TauxCuivre> res;
  const Polygone carte = c.circuit.contour.polygone();
  const double surface = aireMm2(soustraire({carte}, c.circuit.contour.decoupes));
  const auto pp = pastilles(c, k);
  for (const Couche couche : couchesCuivre(c.circuit.empilage.couchesCuivre)) {
    Polygones cuivre;
    for (const auto& p : c.circuit.pistes)
      if (p.couche == couche)
        for (auto& x : formePiste(p)) cuivre.push_back(std::move(x));
    for (const auto& p : pp)
      if (p.couches & bit(couche)) cuivre.push_back(p.forme);
    for (const auto& v : c.circuit.vias)
      if (couchesVia(v, c.circuit.empilage.couchesCuivre) & bit(couche)) cuivre.push_back(cercle(v.p, v.diametre / 2));
    for (const auto& z : c.circuit.zones) {
      if (z.couche != couche) continue;
      if (const auto it = r.find(z.uuid); it != r.end()) cuivre.insert(cuivre.end(), it->second.begin(), it->second.end());
    }
    for (const auto& g : c.circuit.regions)
      if (g.couche == couche) cuivre.push_back(g.forme);
    TauxCuivre t;
    t.couche = couche;
    t.cuivre = aireMm2(intersecter(unir(cuivre), {carte}));
    t.carte = surface;
    t.taux = surface > 0 ? 100 * t.cuivre / surface : 0;
    res.push_back(t);
  }
  return res;
}

// ---------- Courant d'une branche ----------
std::vector<TronconCourant> trajetBranche(const ConceptionElec& c, const Connectivite& k, const std::string& de, const std::string& vers, double courantA, double echauffement) {
  // Broches « R1.2 » → pastille posée
  const auto pp = pastilles(c, k);
  auto pastilleDe = [&](const std::string& b) -> const PastillePlacee* {
    const auto p = b.rfind('.');
    if (p == std::string::npos) return nullptr;
    for (const auto& x : pp)
      if (x.reference == b.substr(0, p) && x.numero == b.substr(p + 1)) return &x;
    return nullptr;
  };
  const PastillePlacee *a = pastilleDe(de), *b = pastilleDe(vers);
  if (!a || !b || a->reseau != b->reseau) return {};
  const std::string reseau = a->reseau;
  // Graphe : nœuds = bouts de pistes (par couche), vias, pastilles ; arêtes = pistes (coupées aux jonctions en T)
  struct Arete {
    int a, b;
    double l;
    int piste;  // indice dans c.circuit.pistes, −1 : liaison sans longueur (pastille, via)
  };
  std::vector<std::pair<Pt, int>> noeuds;  // point, couche (−1 : toutes, via ; −2 : pastille)
  std::vector<Arete> aretes;
  auto noeud = [&](Pt p, int couche) {
    for (std::size_t i = 0; i < noeuds.size(); ++i)
      if (noeuds[i].first == p && noeuds[i].second == couche) return static_cast<int>(i);
    noeuds.push_back({p, couche});
    return static_cast<int>(noeuds.size()) - 1;
  };
  std::vector<int> pistes;
  for (std::size_t i = 0; i < c.circuit.pistes.size(); ++i)
    if (k.canonique(c.circuit.pistes[i].reseau) == k.canonique(reseau)) pistes.push_back(static_cast<int>(i));
  // Points de coupe de chaque piste : ses bouts, et les bouts des autres pistes de la couche posés sur elle
  for (const int i : pistes) {
    const Piste& p = c.circuit.pistes[static_cast<std::size_t>(i)];
    std::vector<Pt> coupes{p.a, p.b};
    for (const int j : pistes) {
      const Piste& q = c.circuit.pistes[static_cast<std::size_t>(j)];
      if (j == i || q.couche != p.couche) continue;
      for (const Pt x : {q.a, q.b})
        if (x != p.a && x != p.b && surSegment(x, p.a, p.b)) coupes.push_back(x);
    }
    std::sort(coupes.begin(), coupes.end(), [&](Pt u, Pt v) { return distance(p.a, u) < distance(p.a, v); });
    for (std::size_t s = 1; s < coupes.size(); ++s)
      aretes.push_back({noeud(coupes[s - 1], static_cast<int>(p.couche)), noeud(coupes[s], static_cast<int>(p.couche)), distance(coupes[s - 1], coupes[s]), i});
  }
  // Vias : relient les bouts de pistes posés sur elles, toutes couches
  for (const auto& v : c.circuit.vias) {
    if (k.canonique(v.reseau) != k.canonique(reseau)) continue;
    const int iv = noeud(v.p, -1);
    for (std::size_t i = 0; i < noeuds.size(); ++i)
      if (static_cast<int>(i) != iv && noeuds[i].second >= 0 && distance(noeuds[i].first, v.p) <= static_cast<double>(v.diametre) / 2) aretes.push_back({iv, static_cast<int>(i), 0, -1});
  }
  // Pastilles du réseau : reliées aux bouts de pistes posés dedans, sur leurs couches
  int ia = -1, ib = -1;
  for (const auto& x : pp) {
    if (x.reseau != reseau) continue;
    const int ip = noeud(x.pastille.position, -2 - static_cast<int>(&x - pp.data()));
    if (&x == a) ia = ip;
    if (&x == b) ib = ip;
    for (std::size_t i = 0; i < noeuds.size(); ++i)
      if (noeuds[i].second >= 0 && (x.couches & bit(static_cast<Couche>(noeuds[i].second))) && dedans(x.forme, noeuds[i].first)) aretes.push_back({ip, static_cast<int>(i), 0, -1});
  }
  if (ia < 0 || ib < 0) return {};
  // Dijkstra sur la longueur
  std::vector<std::vector<std::pair<int, int>>> voisins(noeuds.size());  // (voisin, arête)
  for (std::size_t e = 0; e < aretes.size(); ++e) {
    voisins[static_cast<std::size_t>(aretes[e].a)].push_back({aretes[e].b, static_cast<int>(e)});
    voisins[static_cast<std::size_t>(aretes[e].b)].push_back({aretes[e].a, static_cast<int>(e)});
  }
  std::vector<double> dist(noeuds.size(), std::numeric_limits<double>::infinity());
  std::vector<int> prec(noeuds.size(), -1);
  using E = std::pair<double, int>;
  std::priority_queue<E, std::vector<E>, std::greater<E>> file;
  dist[static_cast<std::size_t>(ia)] = 0;
  file.push({0, ia});
  while (!file.empty()) {
    const auto [d, i] = file.top();
    file.pop();
    if (d > dist[static_cast<std::size_t>(i)]) continue;
    if (i == ib) break;
    for (const auto& [j, e] : voisins[static_cast<std::size_t>(i)]) {
      const double nd = d + aretes[static_cast<std::size_t>(e)].l;
      if (nd < dist[static_cast<std::size_t>(j)]) {
        dist[static_cast<std::size_t>(j)] = nd;
        prec[static_cast<std::size_t>(j)] = e;
        file.push({nd, j});
      }
    }
  }
  if (!std::isfinite(dist[static_cast<std::size_t>(ib)])) return {};
  std::vector<TronconCourant> r;
  const double cuivre = c.circuit.empilage.cuivre;
  for (int i = ib; i != ia;) {
    const Arete& e = aretes[static_cast<std::size_t>(prec[static_cast<std::size_t>(i)])];
    if (e.piste >= 0) {
      const Piste& p = c.circuit.pistes[static_cast<std::size_t>(e.piste)];
      TronconCourant t;
      t.piste = p.uuid;
      t.couche = p.couche;
      t.a = noeuds[static_cast<std::size_t>(e.a)].first;
      t.b = noeuds[static_cast<std::size_t>(e.b)].first;
      t.largeur = p.largeur;
      t.requise = largeurIPC2221(courantA, echauffement, cuivre, !estCuivreInterne(p.couche));
      r.push_back(t);
    }
    i = e.a == i ? e.b : e.a;
  }
  std::reverse(r.begin(), r.end());
  return r;
}

// ---------- Contrôles ----------
void verifierFamilles034(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis, const std::vector<PastillePlacee>& pp, std::vector<Violation>& v) {
  auto ajouter = [&](Gravite g, const char* code, std::string m, std::string conseil, Pt p, std::vector<std::string> objets = {}) {
    v.push_back({g, code, std::move(m), std::move(conseil), {}, p, std::move(objets)});
  };
  const Circuit& ci = c.circuit;
  const int nc = ci.empilage.couchesCuivre;

  // ---------- DRC-55 · Ligne de fuite entre réseaux de tensions différentes, sur les faces ----------
  bool tensions = false;
  for (const auto& kl : ci.regles.classes) tensions |= kl.tension > 0;
  if (tensions) {
    struct Cuivre {
      Polygone forme;
      std::string reseau, objet;
      Couche couche;
    };
    std::vector<Cuivre> objets;
    for (const auto& p : pp)
      for (const Couche f : {Couche::CuivreDessus, Couche::CuivreDessous})
        if ((p.couches & bit(f)) && !p.reseau.empty()) objets.push_back({p.forme, p.reseau, p.composant, f});
    for (const auto& t : ci.pistes)
      if (t.couche == Couche::CuivreDessus || t.couche == Couche::CuivreDessous)
        for (const auto& f : formePiste(t)) objets.push_back({f, t.reseau, t.uuid, t.couche});
    // Obstacles : découpes plus larges que la rainure X (une plus étroite ne compte pas)
    const ReglagesSurface rs = reglagesSurface(c);
    const Nm x = nm(largeurRainureX(rs.pollution));
    Polygones obstacles;
    for (const auto& d : ci.contour.decoupes)
      if (!gonfler({d}, -x / 2).empty()) obstacles.push_back(d);
    std::set<std::pair<std::string, std::string>> signales;
    for (std::size_t i = 0; i < objets.size(); ++i)
      for (std::size_t j = i + 1; j < objets.size(); ++j) {
        const Cuivre &a = objets[i], &b = objets[j];
        if (a.couche != b.couche || k.canonique(a.reseau) == k.canonique(b.reseau)) continue;
        const double exigee = ligneFuiteExigee(c, a.reseau, b.reseau);
        if (exigee <= 0) continue;
        const double air = distancePolygones(a.forme, b.forme) * 1e-6;
        if (air >= exigee || air <= 0) continue;
        const double fuite = ligneDeFuite(a.forme, b.forme, obstacles);
        if (fuite >= exigee) continue;
        const auto cle = std::minmax(a.objet, b.objet);
        if (!signales.insert({cle.first, cle.second}).second) continue;
        ajouter(Gravite::Erreur, "DRC-55",
                "Ligne de fuite de " + mmTexte(fuite) + " mm entre « " + a.reseau + " » et « " + b.reseau + " » sur " + nomCouche(a.couche) + " : " + mmTexte(exigee) + " mm exigés (CEI 60664-1, pollution " +
                    std::to_string(rs.pollution) + ")",
                "Écarte les deux cuivres, ou fraise une fente d'au moins " + mmTexte(largeurRainureX(rs.pollution), 1) + " mm entre eux.", milieuDe(boite(a.forme).centre(), boite(b.forme).centre()),
                {a.objet, b.objet});
      }
  }

  const ContraintesCarte& av = ci.regles.avancees;
  // ---------- DRC-77 · Isolements quels que soient les réseaux : trous et cuivre des trous non métallisés ----------
  {
    std::vector<std::pair<Pt, Nm>> trous;  // via et pastilles percées
    for (const auto& x : ci.vias) trous.push_back({x.p, x.percage});
    for (const auto& p : pp)
      if (p.pastille.percage > 0) trous.push_back({p.pastille.position, p.pastille.percage});
    for (std::size_t i = 0; i < trous.size(); ++i)
      for (std::size_t j = i + 1; j < trous.size(); ++j) {
        if (trous[i].first == trous[j].first) continue;  // même trou (via dans une pastille)
        const double bord = distance(trous[i].first, trous[j].first) - static_cast<double>(trous[i].second + trous[j].second) / 2;
        if (bord >= static_cast<double>(ci.regles.ecartTrous) || bord < -1e3) continue;
        ajouter(Gravite::Erreur, "DRC-77", "Deux perçages à " + mmTexte(bord * 1e-6) + " mm bord à bord (minimum " + mmTexte(mm(ci.regles.ecartTrous)) + " mm), même réseau compris",
                "Écarte les trous : le foret casse la paroi entre deux perçages trop proches.", milieuDe(trous[i].first, trous[j].first));
      }
  }

  // ---------- DRC-78 · Isolement entre couches à travers le diélectrique ----------
  for (const auto& kk : av.contraintes) {
    if (!kk.active || kk.genre != GenreContrainte::IsolementCouches || kk.valeur.min <= 0) continue;
    const auto pile = pileEmpilage(ci.empilage);
    // Épaisseur de diélectrique entre deux cuivres voisins (dans l'ordre de l'empilage)
    std::vector<std::pair<int, double>> cuivres;  // (rang du cuivre, position z au-dessus)
    double z = 0;
    int rang = 0;
    std::vector<double> entre;  // diélectrique entre le cuivre r et r+1
    double accumule = 0;
    bool premier = true;
    for (const auto& x : pile) {
      if (x.genre == GenreCoucheEmpilage::Cuivre) {
        if (!premier) entre.push_back(accumule);
        premier = false;
        accumule = 0;
        ++rang;
      } else
        accumule += x.epaisseur;
      z += x.epaisseur;
    }
    (void)z;
    (void)cuivres;
    const auto ordre = couchesCuivre(nc);
    for (std::size_t r = 0; r + 1 < ordre.size() && r < entre.size(); ++r) {
      if (entre[r] >= mm(kk.valeur.min)) continue;
      // Cuivres de réseaux différents, l'un dans la portée, superposés d'une couche à l'autre
      for (const auto& p : ci.pistes) {
        if (p.couche != ordre[r] || !dansPortee(kk.portee, proprietesPiste(c, p))) continue;
        for (const auto& q : ci.pistes) {
          if (q.couche != ordre[r + 1] || k.canonique(q.reseau) == k.canonique(p.reseau)) continue;
          if (intersecter(formePiste(p), formePiste(q)).empty()) continue;
          ajouter(Gravite::Erreur, "DRC-78",
                  "« " + p.reseau + " » sur " + nomCouche(p.couche) + " au-dessus de « " + q.reseau + " » sur " + nomCouche(q.couche) + " : " + mmTexte(entre[r], 3) + " mm de diélectrique pour " +
                      mmTexte(mm(kk.valeur.min), 3) + " exigés (« " + kk.nom + " »)",
                  "Décale l'une des deux pistes, ou épaissis le diélectrique entre ces couches.", milieuDe(p.a, p.b), {p.uuid, q.uuid});
        }
      }
    }
  }

  // ---------- DRC-79 · Courant d'une branche, tronçon par tronçon sur son trajet ----------
  for (const auto& kk : av.contraintes) {
    if (!kk.active || kk.genre != GenreContrainte::Courant || kk.valeur.min <= 0) continue;
    std::string t = kk.texte;
    for (const std::string sep : {"→", "->", ">", " "}) {
      const auto p = t.find(sep);
      if (p == std::string::npos) continue;
      const std::string de = t.substr(0, p), vers = t.substr(p + sep.size());
      const double courant = static_cast<double>(kk.valeur.min) / 1000.0;  // milliampères
      double echauffement = 10;
      const auto troncons = trajetBranche(c, k, de, vers, courant, echauffement);
      if (troncons.empty()) {
        ajouter(Gravite::Avertissement, "DRC-79", "Branche « " + kk.nom + " » (" + de + " → " + vers + ") : trajet de cuivre introuvable", "Route la branche, ou corrige ses broches.", {});
        break;
      }
      for (const auto& x : troncons)
        if (x.largeur < x.requise)
          ajouter(Gravite::Erreur, "DRC-79",
                  "Branche « " + kk.nom + " » à " + mmTexte(courant, 1) + " A : tronçon de " + mmTexte(mm(x.largeur), 3) + " mm sur " + nomCouche(x.couche) + ", " + mmTexte(mm(x.requise), 3) +
                      " mm demandés (IPC-2221, échauffement " + mmTexte(echauffement, 0) + " °C)",
                  "Élargis ce tronçon : le reste du trajet tient le courant.", milieuDe(x.a, x.b), {x.piste});
      break;
    }
  }

  // ---------- DRC-80 · Sortie des pastilles CMS : longueur jusqu'à la via ----------
  for (const auto& kk : av.contraintes) {
    if (!kk.active || kk.genre != GenreContrainte::ViasParPastille || kk.valeur.max <= 0) continue;
    for (const auto& p : pp) {
      if (p.pastille.percage > 0 || p.reseau.empty()) continue;
      // Pistes qui partent de la pastille ; suivies tant qu'elles s'enchaînent sans branche, jusqu'à une via
      for (const auto& t : ci.pistes) {
        if (k.canonique(t.reseau) != k.canonique(p.reseau) || !(p.couches & bit(t.couche))) continue;
        Pt bout;
        if (dedans(p.forme, t.a)) bout = t.b;
        else if (dedans(p.forme, t.b)) bout = t.a;
        else continue;
        double l = distance(t.a, t.b);
        Pt courant = bout;
        std::set<std::string> vues{t.uuid};
        bool via = false;
        for (int pas = 0; pas < 50 && !via; ++pas) {
          for (const auto& x : ci.vias)
            if (distance(x.p, courant) <= static_cast<double>(x.diametre) / 2) via = true;
          if (via) break;
          const Piste* suite = nullptr;
          for (const auto& u : ci.pistes)
            if (!vues.count(u.uuid) && u.couche == t.couche && (u.a == courant || u.b == courant)) suite = &u;
          if (!suite) break;
          vues.insert(suite->uuid);
          l += distance(suite->a, suite->b);
          courant = suite->a == courant ? suite->b : suite->a;
        }
        if (via && l > static_cast<double>(kk.valeur.max) && dansPortee(kk.portee, proprietesReseau(c, p.reseau)))
          ajouter(Gravite::Erreur, "DRC-80",
                  "Sortie de " + p.reference + "." + p.numero + " vers sa via : " + mmTexte(l * 1e-6) + " mm pour " + mmTexte(mm(kk.valeur.max)) + " permis (« " + kk.nom + " »)",
                  "Rapproche la via de la pastille.", p.pastille.position, {p.composant, t.uuid});
      }
    }
  }

  // ---------- DRC-81 · Longueur parallèle bornée pour un écart ----------
  for (const auto& kk : av.contraintes) {
    if (!kk.active || kk.genre != GenreContrainte::ParalleleMax || kk.valeur.max <= 0) continue;
    const Nm ecart = kk.valeur.min > 0 ? kk.valeur.min : 500'000;
    std::map<std::pair<std::string, std::string>, double> paralleles;
    std::map<std::pair<std::string, std::string>, Pt> ou;
    for (std::size_t i = 0; i < ci.pistes.size(); ++i) {
      const Piste& p = ci.pistes[i];
      const bool pa = dansPortee(kk.portee, proprietesPiste(c, p));
      for (std::size_t j = i + 1; j < ci.pistes.size(); ++j) {
        const Piste& q = ci.pistes[j];
        if (q.couche != p.couche || k.canonique(q.reseau) == k.canonique(p.reseau)) continue;
        const bool qa = dansPortee(kk.portee, proprietesPiste(c, q));
        const bool pb = kk.porteeB.empty() || dansPortee(kk.porteeB, proprietesPiste(c, p)), qb = kk.porteeB.empty() || dansPortee(kk.porteeB, proprietesPiste(c, q));
        if (!((pa && qb) || (qa && pb))) continue;
        // Parallèles (à 5° près) et à moins de « ecart » bord à bord : longueur commune projetée
        const double ux = static_cast<double>(p.b.x - p.a.x), uy = static_cast<double>(p.b.y - p.a.y), vx = static_cast<double>(q.b.x - q.a.x), vy = static_cast<double>(q.b.y - q.a.y);
        const double lu = std::hypot(ux, uy), lv = std::hypot(vx, vy);
        if (lu < 1 || lv < 1) continue;
        if (std::abs(ux * vy - uy * vx) / (lu * lv) > std::sin(5 * 3.14159265358979 / 180)) continue;
        const double ecartBord = distancePointSegment(milieuDe(q.a, q.b), p.a, p.b) - static_cast<double>(p.largeur + q.largeur) / 2;
        if (ecartBord > static_cast<double>(ecart)) continue;
        const double t0 = ((q.a.x - p.a.x) * ux + (q.a.y - p.a.y) * uy) / lu, t1 = ((q.b.x - p.a.x) * ux + (q.b.y - p.a.y) * uy) / lu;
        const double commun = std::max(0.0, std::min(lu, std::max(t0, t1)) - std::max(0.0, std::min(t0, t1)));
        const auto cle = std::minmax(k.canonique(p.reseau), k.canonique(q.reseau));
        paralleles[{cle.first, cle.second}] += commun;
        ou[{cle.first, cle.second}] = milieuDe(p.a, p.b);
      }
    }
    for (const auto& [cle, l] : paralleles)
      if (l > static_cast<double>(kk.valeur.max))
        ajouter(Gravite::Avertissement, "DRC-81",
                "« " + cle.first + " » et « " + cle.second + " » côte à côte sur " + mmTexte(l * 1e-6) + " mm à moins de " + mmTexte(mm(ecart)) + " mm (" + mmTexte(mm(kk.valeur.max)) +
                    " mm permis, « " + kk.nom + " »)",
                "Écarte les deux pistes, ou fais passer l'une sur une autre couche : la diaphonie croît avec la longueur parallèle.", ou[cle]);
  }
  verifierAssemblage034(c, pp, v);
  // Signaux (8.26) : tronçons From-To et ordre de raccordement
  if (!ci.regles.avancees.signaux.empty())
    for (auto& x : verifierSignaux(c)) v.push_back(std::move(x));
  // Schémas de lignes (18.4) : moignons de via, de chaîne et de pastille, ordre de raccordement
  if (c.extensions.count("lignes"))
    for (auto& x : verifierMoignons(c)) v.push_back(std::move(x));
  (void)remplis;
}

void verifierAssemblage034(const ConceptionElec& c, const std::vector<PastillePlacee>& pp, std::vector<Violation>& v) {
  auto ajouter = [&](Gravite g, const char* code, std::string m, std::string conseil, Pt p, std::vector<std::string> objets = {}) {
    v.push_back({g, code, std::move(m), std::move(conseil), {}, p, std::move(objets)});
  };
  const auto& K = c.circuit.regles.avancees.contraintes;
  auto existe = [&](GenreContrainte g) { return std::any_of(K.begin(), K.end(), [&](const Contrainte& k) { return k.active && k.genre == g; }); };
  struct Pose {
    const EmpreintePlacee* e;
    const ComposantElec* x;
    Polygone enc;
    Boite b;
    Proprietes pr;
  };
  std::vector<Pose> poses;
  if (existe(GenreContrainte::EcartComposants) || existe(GenreContrainte::Orientations))
    for (const auto& e : c.circuit.empreintes) {
      const DefEmpreinte* d = c.empreinte(e.empreinte);
      const ComposantElec* x = c.composant(e.composant);
      if (!d || !x) continue;
      Polygone enc = encombrementSurCarte(e, *d);
      const Boite b = boite(enc);
      poses.push_back({&e, x, std::move(enc), b, proprietesComposant(c, *x)});
    }
  // ---------- DRC-88 · Écart entre composants pour l'assemblage (8.14) : la valeur la plus prudente l'emporte ----------
  if (existe(GenreContrainte::EcartComposants)) {
    Nm portee = 0;
    for (const auto& k : K)
      if (k.active && k.genre == GenreContrainte::EcartComposants) portee = std::max(portee, k.valeur.min);
    for (std::size_t i = 0; i < poses.size(); ++i)
      for (std::size_t j = i + 1; j < poses.size(); ++j) {
        const Pose &a = poses[i], &b = poses[j];
        if (a.e->dessous != b.e->dessous || !a.b.gonflee(portee).touche(b.b)) continue;
        const Resolution r = resoudre(c, GenreContrainte::EcartComposants, a.pr, &b.pr);
        if (!r.trouvee || r.valeur.min <= 0) continue;
        double d = std::numeric_limits<double>::max();
        for (std::size_t q = 0; q < b.enc.size(); ++q) d = std::min(d, distancePolygoneSegment(a.enc, b.enc[q], b.enc[(q + 1) % b.enc.size()]));
        if (d < static_cast<double>(r.valeur.min))
          ajouter(Gravite::Erreur, "DRC-88",
                  a.x->reference + " et " + b.x->reference + " à " + mmTexte(d * 1e-6) + " mm (" + mmTexte(mm(r.valeur.min)) + " mm pour l'assemblage, " + r.regle + ")",
                  "Écarte les deux composants : la machine de pose et la retouche demandent cette place.", a.e->position, {a.x->uuid, b.x->uuid});
      }
  }
  // ---------- DRC-89 · Orientation permise (8.14) : « 0,90 » dans le texte de la contrainte ----------
  if (existe(GenreContrainte::Orientations))
    for (const auto& a : poses)
      for (const auto& k : K) {
        if (!k.active || k.genre != GenreContrainte::Orientations || !dansPortee(k.portee, a.pr) || k.texte.empty()) continue;
        std::vector<double> permises;
        std::string t = k.texte;
        for (char& ch : t)
          if (ch == ';' || ch == ' ') ch = ',';
        for (std::size_t i = 0; i <= t.size();) {
          const std::size_t f = std::min(t.find(',', i), t.size());
          if (f > i) permises.push_back(std::atof(t.substr(i, f - i).c_str()));
          i = f + 1;
        }
        double rot = std::fmod(a.e->rotation, 360.0);
        if (rot < 0) rot += 360;
        const bool ok = std::any_of(permises.begin(), permises.end(), [&](double p) {
          const double d = std::fmod(std::abs(std::fmod(p, 360.0) - rot), 360.0);
          return std::min(d, 360 - d) < 0.01;
        });
        if (!ok)
          ajouter(Gravite::Avertissement, "DRC-89", a.x->reference + " tourné de " + mmTexte(rot) + "° : orientations permises « " + k.texte + " » (" + (k.nom.empty() ? std::string("contrainte") : k.nom) + ")",
                  "Tourne le composant sur une orientation permise : la soudure à la vague ou le chargeur l'exigent.", a.e->position, {a.x->uuid});
        break;
      }
  // ---------- DRC-90 · Pont de vernis entre deux pastilles voisines (8.14) ----------
  if (existe(GenreContrainte::PontVernis)) {
    const ReglagesFabrication& RF = c.circuit.fabrication;
    for (std::size_t i = 0; i < pp.size(); ++i) {
      const ComposantElec* x = c.composant(pp[i].composant);
      if (!x) continue;
      const Resolution r = resoudre(c, GenreContrainte::PontVernis, proprietesComposant(c, *x));
      if (!r.trouvee || r.valeur.min <= 0) continue;
      // Marge de vernis : celle de la pastille, sinon celle des règles d'assemblage, sinon celle de la carte
      const Resolution rv = resoudre(c, GenreContrainte::MargeVernis, proprietesComposant(c, *x));
      const Nm margeRegle = rv.trouvee ? (rv.valeur.min ? rv.valeur.min : rv.valeur.pref) : RF.margeVernis;
      const Nm mi = pp[i].pastille.margeVernis.value_or(margeRegle);
      for (std::size_t j = i + 1; j < pp.size(); ++j) {
        if (pp[j].composant != pp[i].composant || !(pp[i].couches & pp[j].couches & (bit(Couche::CuivreDessus) | bit(Couche::CuivreDessous)))) continue;
        const Nm mj = pp[j].pastille.margeVernis.value_or(margeRegle);
        double d = std::numeric_limits<double>::max();
        const Polygone& B = pp[j].forme;
        for (std::size_t q = 0; q < B.size(); ++q) d = std::min(d, distancePolygoneSegment(pp[i].forme, B[q], B[(q + 1) % B.size()]));
        const double pont = d - static_cast<double>(mi + mj);
        if (pont > 0 && pont < static_cast<double>(r.valeur.min))
          ajouter(Gravite::Avertissement, "DRC-90",
                  "Pont de vernis de " + mmTexte(pont * 1e-6) + " mm entre " + x->reference + "." + pp[i].numero + " et " + x->reference + "." + pp[j].numero + " (" + mmTexte(mm(r.valeur.min)) + " mm au moins, " + r.regle + ")",
                  "Réduis la marge du vernis de ces pastilles, ou ouvre-les ensemble : un pont plus fin se décolle.", pp[i].pastille.position, {x->uuid});
      }
    }
  }
}

}  // namespace prom::ecad
