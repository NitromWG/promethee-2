// Prométhée : intégrité du signal et de l'alimentation (lot L18). Licence GPL-3.0-only.
#include "promethee/integrite.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <numeric>
#include <set>

#include "promethee/fabrication_avancee.hpp"
#include "promethee/revue_conception.hpp"

namespace prom::ecad {

namespace {
double mmD(Nm v) { return static_cast<double>(v) * 1e-6; }
std::string chiffre(double v, int d = 1) {
  char s[32];
  std::snprintf(s, sizeof s, "%.*f", d, v);
  std::string r = s;
  std::replace(r.begin(), r.end(), '.', ',');
  return r;
}
Couche coucheVoisine(Couche c, int couches) {
  if (couches >= 4) return c == Couche::CuivreDessus ? Couche::CuivreInt1 : c == Couche::CuivreDessous ? Couche::CuivreInt2 : c;
  return opposee(c);
}
bool exterieure(Couche c) { return c == Couche::CuivreDessus || c == Couche::CuivreDessous; }
}  // namespace

RapportImpedance analyserImpedance(const ConceptionElec& c, const std::vector<std::string>& reseaux) {
  RapportImpedance r;
  const Connectivite k = connectivite(c);
  const Remplissages remplis = remplirZones(c, k);
  const Microruban m = microrubanDe(c.circuit.empilage);
  // Plans de chaque couche : zones remplies, avec leur réseau
  std::map<Couche, std::vector<std::pair<std::string, const Polygones*>>> plans;
  for (const auto& z : c.circuit.zones)
    if (const auto it = remplis.find(z.uuid); it != remplis.end()) plans[z.couche].emplace_back(z.reseau, &it->second);
  const std::set<std::string> voulus(reseaux.begin(), reseaux.end());
  for (const auto& t : c.circuit.pistes) {
    if (t.reseau.empty() || (!voulus.empty() && !voulus.count(t.reseau))) continue;
    Troncon x;
    x.piste = t.uuid;
    x.reseau = t.reseau;
    x.couche = t.couche;
    x.longueur = distance(t.a, t.b) * 1e-6;
    x.largeur = mmD(t.largeur);
    if (exterieure(t.couche) && c.circuit.empilage.couchesCuivre >= 2) x.z0 = impedanceMicroruban(x.largeur, m);
    const Couche voisine = coucheVoisine(t.couche, c.circuit.empilage.couchesCuivre);
    const auto it = plans.find(voisine);
    // Échantillons tous les 0,25 mm : part de la longueur sans cuivre de plan dessous
    const int n = std::max(2, static_cast<int>(std::ceil(x.longueur / 0.25)) + 1);
    int sans = 0;
    std::map<std::string, int> sous;
    for (int i = 0; i < n; ++i) {
      const double u = static_cast<double>(i) / (n - 1);
      const Pt p{t.a.x + static_cast<Nm>((t.b.x - t.a.x) * u), t.a.y + static_cast<Nm>((t.b.y - t.a.y) * u)};
      bool dessous = false;
      if (it != plans.end())
        for (const auto& [reseau, poly] : it->second)
          if (dedans(*poly, p)) {
            dessous = true;
            sous[reseau]++;
            break;
          }
      sans += !dessous;
    }
    x.partSansPlan = static_cast<double>(sans) / n;
    x.reference = sans == 0;
    if (!sous.empty()) x.plan = std::max_element(sous.begin(), sous.end(), [](auto a, auto b) { return a.second < b.second; })->first;
    if (!x.reference && c.circuit.empilage.couchesCuivre >= 2 && x.longueur * x.partSansPlan >= 1.0)
      r.alertes.push_back("Piste de « " + x.reseau + " » (" + codeCouche(x.couche) + ") : " + chiffre(x.longueur * x.partSansPlan) + " mm sans plan de référence sur " + codeCouche(voisine) +
                          (x.plan.empty() ? std::string() : " (fente ou vide du plan « " + x.plan + " »)") + ".");
    r.troncons.push_back(x);
  }
  return r;
}

std::vector<Couplage> analyserDiaphonie(const ConceptionElec& c, double longueurMin, double seuil) {
  const Microruban m = microrubanDe(c.circuit.empilage);
  struct Cle {
    std::string a, b;
    Couche couche;
    bool operator<(const Cle& o) const { return std::tie(a, b, couche) < std::tie(o.a, o.b, o.couche); }
  };
  std::map<Cle, Couplage> parPaire;
  const auto& p = c.circuit.pistes;
  for (std::size_t i = 0; i < p.size(); ++i)
    for (std::size_t j = i + 1; j < p.size(); ++j) {
      const Piste &s = p[i], &t = p[j];
      if (s.couche != t.couche || s.reseau == t.reseau || s.reseau.empty() || t.reseau.empty()) continue;
      const double dx = static_cast<double>(s.b.x - s.a.x), dy = static_cast<double>(s.b.y - s.a.y), l = std::hypot(dx, dy);
      const double ex = static_cast<double>(t.b.x - t.a.x), ey = static_cast<double>(t.b.y - t.a.y), lt = std::hypot(ex, ey);
      if (l < 1 || lt < 1) continue;
      const double ux = dx / l, uy = dy / l;
      if (std::abs(ux * ey / lt - uy * ex / lt) > 0.087) continue;  // plus de 5° d'écart : pas parallèles
      // Projections de t sur la droite de s ; recouvrement le long de s
      const auto proj = [&](Pt q) { return (static_cast<double>(q.x - s.a.x) * ux + static_cast<double>(q.y - s.a.y) * uy); };
      const double t0 = std::min(proj(t.a), proj(t.b)), t1 = std::max(proj(t.a), proj(t.b));
      const double recouvre = std::min(l, t1) - std::max(0.0, t0);
      if (recouvre <= 0) continue;
      const double d = std::abs(static_cast<double>(t.a.x - s.a.x) * -uy + static_cast<double>(t.a.y - s.a.y) * ux);
      const double ecart = std::max(0.0, d - (s.largeur + t.largeur) / 2.0) * 1e-6;
      const double k = 1 / (1 + (ecart / m.h) * (ecart / m.h));
      Cle cle{std::min(s.reseau, t.reseau), std::max(s.reseau, t.reseau), s.couche};
      Couplage& x = parPaire[cle];
      x.agresseur = cle.a;
      x.victime = cle.b;
      x.couche = s.couche;
      x.longueurCouplee += recouvre * 1e-6;
      x.ecart = x.coefficient == 0 ? ecart : std::min(x.ecart, ecart);
      x.coefficient = std::max(x.coefficient, k);
    }
  std::vector<Couplage> r;
  for (auto& [cle, x] : parPaire)
    if (x.longueurCouplee >= longueurMin && x.coefficient >= seuil) r.push_back(x);
  std::sort(r.begin(), r.end(), [](const Couplage& a, const Couplage& b) { return a.coefficient * a.longueurCouplee > b.coefficient * b.longueurCouplee; });
  return r;
}

namespace {
// Nœuds du cuivre d'un réseau : extrémités de pistes, fusionnées quand elles se touchent (même point, via, pastille)
struct GrapheCuivre {
  std::map<std::pair<Nm, Nm>, int> indice;
  std::vector<int> parent;
  int noeud(Pt p) {
    const auto it = indice.find({p.x, p.y});
    if (it != indice.end()) return it->second;
    const int n = static_cast<int>(parent.size());
    indice[{p.x, p.y}] = n;
    parent.push_back(n);
    return n;
  }
  int racine(int x) { return parent[static_cast<std::size_t>(x)] == x ? x : parent[static_cast<std::size_t>(x)] = racine(parent[static_cast<std::size_t>(x)]); }
  void unir(int a, int b) { parent[static_cast<std::size_t>(racine(a))] = racine(b); }
};
}  // namespace

ResultatChute chuteTension(const ConceptionElec& c, const std::string& nomReseau, const std::string& source, double tension, const std::vector<ChargeCourant>& charges) {
  ResultatChute r;
  const Connectivite k = connectivite(c);
  const double epaisseur = c.circuit.empilage.cuivre;  // mm
  constexpr double RHO = 1.72e-5;                       // Ω·mm (cuivre à 20 °C)
  GrapheCuivre g;
  struct Arete {
    int a, b;
    double r, section;
    std::string piste;
  };
  std::vector<Arete> aretes;
  std::vector<const Piste*> pistes;
  for (const auto& t : c.circuit.pistes)
    if (t.reseau == nomReseau) {
      const int a = g.noeud(t.a), b = g.noeud(t.b);
      const double l = distance(t.a, t.b) * 1e-6, w = mmD(t.largeur);
      aretes.push_back({a, b, RHO * std::max(l, 1e-6) / (w * epaisseur), w * epaisseur, t.uuid});
      pistes.push_back(&t);
    }
  // Pastilles du réseau : nœud propre, joint aux extrémités de pistes qui tombent dedans
  std::map<std::string, int> pastille;  // « R1.2 » → nœud
  for (const auto& p : pastilles(c, k)) {
    if (p.reseau != nomReseau) continue;
    const int n = g.noeud(p.pastille.position);
    pastille[p.reference + "." + p.numero] = n;
    for (const Piste* t : pistes)
      for (Pt e : {t->a, t->b})
        if (dedans(p.forme, e)) g.unir(g.noeud(e), n);
  }
  // Vias : les pistes des deux faces qui y arrivent sont au même point, donc au même nœud
  for (const auto& v : c.circuit.vias)
    if (v.reseau == nomReseau) g.noeud(v.p);
  const auto itSource = pastille.find(source);
  if (itSource == pastille.end()) {
    r.alertes.push_back("Pastille source « " + source + " » absente du réseau « " + nomReseau + " ».");
    return r;
  }
  // Nœuds réduits (après fusions), composante reliée à la source
  std::map<int, int> rang;
  for (std::size_t i = 0; i < g.parent.size(); ++i) {
    const int x = g.racine(static_cast<int>(i));
    if (!rang.count(x)) rang[x] = static_cast<int>(rang.size());
  }
  const int n = static_cast<int>(rang.size());
  std::vector<std::vector<std::pair<int, double>>> voisins(static_cast<std::size_t>(n));
  for (const auto& a : aretes) {
    const int u = rang[g.racine(a.a)], v = rang[g.racine(a.b)];
    if (u == v) continue;
    voisins[static_cast<std::size_t>(u)].emplace_back(v, 1 / a.r);
    voisins[static_cast<std::size_t>(v)].emplace_back(u, 1 / a.r);
  }
  const int s = rang[g.racine(itSource->second)];
  std::vector<char> relie(static_cast<std::size_t>(n), 0);
  std::deque<int> file{s};
  relie[static_cast<std::size_t>(s)] = 1;
  while (!file.empty()) {
    const int u = file.front();
    file.pop_front();
    for (const auto& [v, gg] : voisins[static_cast<std::size_t>(u)])
      if (!relie[static_cast<std::size_t>(v)]) {
        relie[static_cast<std::size_t>(v)] = 1;
        file.push_back(v);
      }
  }
  // Inconnues : les nœuds reliés hors source
  std::vector<int> inconnue(static_cast<std::size_t>(n), -1);
  int m = 0;
  for (int i = 0; i < n; ++i)
    if (relie[static_cast<std::size_t>(i)] && i != s) inconnue[static_cast<std::size_t>(i)] = m++;
  if (m > 3000) {
    r.alertes.push_back("Réseau trop découpé pour le calcul direct (" + std::to_string(m) + " nœuds).");
    return r;
  }
  std::vector<double> A(static_cast<std::size_t>(m) * static_cast<std::size_t>(m), 0.0), b(static_cast<std::size_t>(m), 0.0);
  const auto at = [&](int i, int j) -> double& { return A[static_cast<std::size_t>(i) * static_cast<std::size_t>(m) + static_cast<std::size_t>(j)]; };
  for (int u = 0; u < n; ++u) {
    const int iu = inconnue[static_cast<std::size_t>(u)];
    if (iu < 0) continue;
    for (const auto& [v, gg] : voisins[static_cast<std::size_t>(u)]) {
      at(iu, iu) += gg;
      const int iv = inconnue[static_cast<std::size_t>(v)];
      if (iv >= 0) at(iu, iv) -= gg;
      else if (v == s) b[static_cast<std::size_t>(iu)] += gg * tension;
    }
  }
  for (const auto& ch : charges) {
    const auto it = pastille.find(ch.point);
    if (it == pastille.end()) {
      r.alertes.push_back("Charge « " + ch.point + " » : pastille absente du réseau.");
      continue;
    }
    const int u = rang[g.racine(it->second)];
    if (!relie[static_cast<std::size_t>(u)]) {
      r.alertes.push_back("Charge « " + ch.point + " » : aucun cuivre ne la relie à " + source + ".");
      continue;
    }
    if (const int iu = inconnue[static_cast<std::size_t>(u)]; iu >= 0) b[static_cast<std::size_t>(iu)] -= ch.courant;
  }
  // Élimination de Gauss avec pivot partiel
  for (int col = 0; col < m; ++col) {
    int piv = col;
    for (int i = col + 1; i < m; ++i)
      if (std::abs(at(i, col)) > std::abs(at(piv, col))) piv = i;
    if (std::abs(at(piv, col)) < 1e-15) continue;
    if (piv != col) {
      for (int j = 0; j < m; ++j) std::swap(at(col, j), at(piv, j));
      std::swap(b[static_cast<std::size_t>(col)], b[static_cast<std::size_t>(piv)]);
    }
    for (int i = col + 1; i < m; ++i) {
      const double f = at(i, col) / at(col, col);
      if (f == 0) continue;
      for (int j = col; j < m; ++j) at(i, j) -= f * at(col, j);
      b[static_cast<std::size_t>(i)] -= f * b[static_cast<std::size_t>(col)];
    }
  }
  std::vector<double> x(static_cast<std::size_t>(m), tension);
  for (int i = m - 1; i >= 0; --i) {
    double sum = b[static_cast<std::size_t>(i)];
    for (int j = i + 1; j < m; ++j) sum -= at(i, j) * x[static_cast<std::size_t>(j)];
    x[static_cast<std::size_t>(i)] = std::abs(at(i, i)) < 1e-15 ? tension : sum / at(i, i);
  }
  const auto tensionNoeud = [&](int u) { return u == s ? tension : inconnue[static_cast<std::size_t>(u)] >= 0 ? x[static_cast<std::size_t>(inconnue[static_cast<std::size_t>(u)])] : 0.0; };
  for (const auto& ch : charges)
    if (const auto it = pastille.find(ch.point); it != pastille.end()) {
      const int u = rang[g.racine(it->second)];
      if (!relie[static_cast<std::size_t>(u)]) continue;
      r.tensions[ch.point] = tensionNoeud(u);
      r.chutes[ch.point] = tension - tensionNoeud(u);
      r.chuteMax = std::max(r.chuteMax, tension - tensionNoeud(u));
    }
  for (const auto& a : aretes) {
    const int u = rang[g.racine(a.a)], v = rang[g.racine(a.b)];
    if (u == v || !relie[static_cast<std::size_t>(u)]) continue;
    const double i = std::abs(tensionNoeud(u) - tensionNoeud(v)) / a.r;
    if (i > r.courantMax) {
      r.courantMax = i;
      r.tronconMax = a.piste;
    }
    r.densiteMax = std::max(r.densiteMax, i / a.section);
  }
  return r;
}

std::vector<MontageDecouplage> analyserDecouplage(const ConceptionElec& c) {
  std::vector<MontageDecouplage> r;
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  std::set<std::string> poses;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) poses.insert(s.composant);
  for (const auto& x : c.composants) {
    if (!poses.count(x.uuid) || !estCondensateur(c, x)) continue;
    const std::string a = k.nom(x.uuid, "1"), b = k.nom(x.uuid, "2");
    const int ia = k.indice(a), ib = k.indice(b);
    if (ia < 0 || ib < 0) continue;
    const bool aAlim = estAlimentation(k.reseaux[static_cast<std::size_t>(ia)]), bAlim = estAlimentation(k.reseaux[static_cast<std::size_t>(ib)]);
    if (!((aAlim && estMasse(b)) || (bAlim && estMasse(a)))) continue;
    MontageDecouplage m;
    m.condensateur = x.reference;
    m.reseau = aAlim ? a : b;
    m.masse = aAlim ? b : a;
    // Pour chaque pastille : chemin de pistes le plus court jusqu'à une via ou à la pastille d'un autre composant
    for (const auto& p : pp) {
      if (p.composant != x.uuid) continue;
      std::map<std::pair<Nm, Nm>, double> dist;
      std::deque<Pt> file;
      std::set<std::pair<Nm, Nm>> departs;
      for (const auto& t : c.circuit.pistes)
        if (t.reseau == p.reseau)
          for (Pt e : {t.a, t.b})
            if (dedans(p.forme, e) && departs.insert({e.x, e.y}).second) {
              dist[{e.x, e.y}] = 0;
              file.push_back(e);
            }
      double meilleur = -1;
      bool parVia = false;
      while (!file.empty()) {
        const Pt u = file.front();
        file.pop_front();
        const double du = dist[{u.x, u.y}];
        const bool via = std::any_of(c.circuit.vias.begin(), c.circuit.vias.end(), [&](const Via& v) { return v.reseau == p.reseau && distance(v.p, u) <= v.diametre / 2.0; });
        const bool autre = std::any_of(pp.begin(), pp.end(), [&](const PastillePlacee& q) { return q.composant != x.uuid && q.reseau == p.reseau && dedans(q.forme, u); });
        if (via || autre) {
          if (meilleur < 0 || du < meilleur) {
            meilleur = du;
            parVia = via;
          }
          continue;
        }
        for (const auto& t : c.circuit.pistes) {
          if (t.reseau != p.reseau) continue;
          for (int sens = 0; sens < 2; ++sens) {
            const Pt de = sens ? t.b : t.a, vers = sens ? t.a : t.b;
            if (de != u) continue;
            const double nd = du + distance(de, vers) * 1e-6;
            const auto it = dist.find({vers.x, vers.y});
            if (it == dist.end() || nd < it->second) {
              dist[{vers.x, vers.y}] = nd;
              file.push_back(vers);
            }
          }
        }
      }
      if (meilleur > 0) m.longueur += meilleur;
      m.vias += parVia;
    }
    m.inductance = m.longueur * 1.0 + m.vias * 0.5 + 0.4;
    r.push_back(m);
  }
  std::sort(r.begin(), r.end(), [](const MontageDecouplage& a, const MontageDecouplage& b) { return a.inductance > b.inductance; });
  return r;
}

}  // namespace prom::ecad
