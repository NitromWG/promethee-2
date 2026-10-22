// Prométhée : placement automatique (lot L13, 13.12). Licence GPL-3.0-only.
#include "promethee/placement_auto.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <map>
#include <random>

namespace prom::ecad {

namespace {
double mmD(Nm v) { return static_cast<double>(v) * 1e-6; }

// Aire commune de deux boîtes (mm²)
double aireCommune(const Boite& a, const Boite& b) {
  const double l = mmD(std::min(a.x1, b.x1) - std::max(a.x0, b.x0)), h = mmD(std::min(a.y1, b.y1) - std::max(a.y0, b.y0));
  return l > 0 && h > 0 ? l * h : 0;
}
// Aire d'une boîte hors d'un rectangle (mm²)
double aireDehors(const Boite& b, const Boite& r) {
  const double tout = mmD(b.largeur()) * mmD(b.hauteur());
  return std::max(0.0, tout - aireCommune(b, r));
}
Boite decaler(const Boite& b, Pt p) { return {b.x0 + p.x, b.y0 + p.y, b.x1 + p.x, b.y1 + p.y}; }

struct Mobile {
  std::size_t empreinte = 0;  // indice dans circuit.empreintes
  std::array<std::vector<Pt>, 4> pastilles;  // décalages des pastilles pour chaque quart de tour
  std::array<Boite, 4> boites;               // encombrement (gonflé de la moitié de l'écart), à l'origine
  double rotation0 = 0;
  Pt position;
  int quart = 0;
};
struct Membre {
  int mobile = -1;   // -1 : point fixe
  int pastille = 0;
  Pt fixe;
};

class Recuit {
 public:
  Recuit(ConceptionElec& c, const OptionsPlacementAuto& o) : m_c(c), m_o(o), m_alea(o.graine) {}

  ResultatPlacementAuto lancer() {
    ResultatPlacementAuto r;
    preparer();
    if (m_mobiles.empty()) {
      r.chevelu = longueurChevelu(m_c);
      r.collisions = collisionsPlacement(m_c, m_o.ecart);
      return r;
    }
    premierePasse();
    r.interrompu = !recuire();
    legaliser();
    ecrire();
    r.deplaces = static_cast<int>(m_mobiles.size());
    r.chevelu = longueurChevelu(m_c);
    r.collisions = collisionsPlacement(m_c, m_o.ecart);
    return r;
  }

 private:
  void preparer() {
    const Polygone contour = m_c.circuit.contour.polygone();
    m_carte = boite(contour);
    m_interieur = {m_carte.x0 + m_o.marge, m_carte.y0 + m_o.marge, m_carte.x1 - m_o.marge, m_carte.y1 - m_o.marge};
    const Connectivite k = connectivite(m_c);
    std::map<std::pair<std::string, std::string>, std::string> reseauDe;  // (composant, numéro) → réseau
    std::map<std::string, std::vector<Pt>> pointsFixes;
    std::map<std::string, std::size_t> indiceEmpreinte;
    for (std::size_t i = 0; i < m_c.circuit.empreintes.size(); ++i) indiceEmpreinte[m_c.circuit.empreintes[i].composant] = i;
    std::vector<int> mobileDe(m_c.circuit.empreintes.size(), -1);
    for (std::size_t i = 0; i < m_c.circuit.empreintes.size(); ++i) {
      const EmpreintePlacee& e = m_c.circuit.empreintes[i];
      const DefEmpreinte* d = m_c.empreinte(e.empreinte);
      const ComposantElec* x = m_c.composant(e.composant);
      if (!d || !x || e.verrou || m_o.fixes.count(e.composant) || x->horsCarte) continue;
      Mobile m;
      m.empreinte = i;
      m.rotation0 = e.rotation;
      m.position = e.position;
      for (int q = 0; q < 4; ++q) {
        EmpreintePlacee z = e;
        z.position = {0, 0};
        z.rotation = std::fmod(e.rotation + 90.0 * q, 360.0);
        for (const auto& p : d->pastilles) m.pastilles[static_cast<std::size_t>(q)].push_back(pastilleSurCarte(z, p).position);
        m.boites[static_cast<std::size_t>(q)] = boite(encombrementSurCarte(z, *d)).gonflee(m_o.ecart / 2);
      }
      mobileDe[i] = static_cast<int>(m_mobiles.size());
      m_mobiles.push_back(std::move(m));
    }
    // Réseaux : chaque pastille d'un mobile est un membre ; les pastilles des empreintes fixes sont des points fixes
    std::map<std::string, std::vector<Membre>> parReseau;
    for (const auto& p : pastilles(m_c, k)) {
      if (p.reseau.empty()) continue;
      const int mi = p.empreinte >= 0 && static_cast<std::size_t>(p.empreinte) < mobileDe.size() ? mobileDe[static_cast<std::size_t>(p.empreinte)] : -1;
      Membre mb;
      if (mi < 0) {
        mb.fixe = p.pastille.position;
      } else {
        mb.mobile = mi;
        const DefEmpreinte* d = m_c.empreinte(m_c.circuit.empreintes[static_cast<std::size_t>(p.empreinte)].empreinte);
        for (std::size_t j = 0; j < d->pastilles.size(); ++j)
          if (d->pastilles[j].numero == p.numero) {
            mb.pastille = static_cast<int>(j);
            break;
          }
      }
      parReseau[p.reseau].push_back(mb);
    }
    for (auto& [nom, membres] : parReseau)
      if (membres.size() >= 2) m_reseaux.push_back(std::move(membres));
    m_reseauxDe.assign(m_mobiles.size(), {});
    for (std::size_t n = 0; n < m_reseaux.size(); ++n)
      for (const auto& mb : m_reseaux[n])
        if (mb.mobile >= 0 && (m_reseauxDe[static_cast<std::size_t>(mb.mobile)].empty() || m_reseauxDe[static_cast<std::size_t>(mb.mobile)].back() != static_cast<int>(n)))
          m_reseauxDe[static_cast<std::size_t>(mb.mobile)].push_back(static_cast<int>(n));
    m_longueur.assign(m_reseaux.size(), 0);
  }

  Pt position(const Membre& mb) const {
    if (mb.mobile < 0) return mb.fixe;
    const Mobile& m = m_mobiles[static_cast<std::size_t>(mb.mobile)];
    const Pt d = m.pastilles[static_cast<std::size_t>(m.quart)][static_cast<std::size_t>(mb.pastille)];
    return {m.position.x + d.x, m.position.y + d.y};
  }
  double demiPerimetre(std::size_t n) const {
    Boite b;
    for (const auto& mb : m_reseaux[n]) b.etendre(position(mb));
    return mmD(b.largeur()) + mmD(b.hauteur());
  }
  Boite boiteDe(std::size_t i) const {
    const Mobile& m = m_mobiles[i];
    return decaler(m.boites[static_cast<std::size_t>(m.quart)], m.position);
  }
  double penalite(std::size_t i) const {
    const Boite b = boiteDe(i);
    double p = 4 * aireDehors(b, m_interieur);
    for (std::size_t j = 0; j < m_mobiles.size(); ++j)
      if (j != i) p += 2 * aireCommune(b, boiteDe(j));
    for (const auto& f : m_fixes) p += 2 * aireCommune(b, f);
    return p;
  }
  double coutTotal() {
    double c = 0;
    for (std::size_t n = 0; n < m_reseaux.size(); ++n) c += (m_longueur[n] = demiPerimetre(n));
    for (std::size_t i = 0; i < m_mobiles.size(); ++i) c += penalite(i) / 2 + 2 * aireDehors(boiteDe(i), m_interieur);
    return c;
  }
  // Coût de ce que touchent les mobiles i et j (j = -1 : i seul)
  double coutLocal(std::size_t i, int j) const {
    double c = 0;
    std::vector<int> nets = m_reseauxDe[i];
    if (j >= 0) nets.insert(nets.end(), m_reseauxDe[static_cast<std::size_t>(j)].begin(), m_reseauxDe[static_cast<std::size_t>(j)].end());
    std::sort(nets.begin(), nets.end());
    nets.erase(std::unique(nets.begin(), nets.end()), nets.end());
    for (int n : nets) c += demiPerimetre(static_cast<std::size_t>(n));
    c += penalite(i);
    if (j >= 0) c += penalite(static_cast<std::size_t>(j)) - 2 * aireCommune(boiteDe(i), boiteDe(static_cast<std::size_t>(j)));
    return c;
  }
  Pt surGrille(Pt p) const {
    const Nm g = std::max<Nm>(m_o.pas, 1);
    return {static_cast<Nm>(std::llround(static_cast<double>(p.x) / g) * g), static_cast<Nm>(std::llround(static_cast<double>(p.y) / g) * g)};
  }
  // Rangée de départ : en largeur à partir du composant le plus relié, chaque voisin à la suite (groupes d'affinité)
  void premierePasse() {
    for (const auto& e : m_c.circuit.empreintes)
      if (std::none_of(m_mobiles.begin(), m_mobiles.end(), [&](const Mobile& m) { return &m_c.circuit.empreintes[m.empreinte] == &e; }))
        if (const DefEmpreinte* d = m_c.empreinte(e.empreinte)) m_fixes.push_back(boite(encombrementSurCarte(e, *d)).gonflee(m_o.ecart / 2));
    std::vector<std::vector<int>> voisins(m_mobiles.size());
    for (const auto& r : m_reseaux)
      for (const auto& a : r)
        for (const auto& b : r)
          if (a.mobile >= 0 && b.mobile >= 0 && a.mobile != b.mobile) voisins[static_cast<std::size_t>(a.mobile)].push_back(b.mobile);
    std::vector<int> ordre, vu(m_mobiles.size(), 0);
    std::vector<int> degres(m_mobiles.size());
    for (std::size_t i = 0; i < m_mobiles.size(); ++i) degres[i] = static_cast<int>(voisins[i].size());
    while (ordre.size() < m_mobiles.size()) {
      int depart = -1;
      for (std::size_t i = 0; i < m_mobiles.size(); ++i)
        if (!vu[i] && (depart < 0 || degres[i] > degres[static_cast<std::size_t>(depart)])) depart = static_cast<int>(i);
      std::deque<int> file{depart};
      vu[static_cast<std::size_t>(depart)] = 1;
      while (!file.empty()) {
        const int i = file.front();
        file.pop_front();
        ordre.push_back(i);
        std::map<int, int> poids;
        for (int v : voisins[static_cast<std::size_t>(i)])
          if (!vu[static_cast<std::size_t>(v)]) poids[v]++;
        std::vector<std::pair<int, int>> tri(poids.begin(), poids.end());
        std::sort(tri.begin(), tri.end(), [](auto a, auto b) { return a.second > b.second; });
        for (const auto& [v, w] : tri) {
          vu[static_cast<std::size_t>(v)] = 1;
          file.push_back(v);
        }
      }
    }
    // Rangées dans le rectangle utile, en serpentin pour garder les voisins proches
    Nm x = m_interieur.x0, y = m_interieur.y0, hauteurRangee = 0;
    bool versLaDroite = true;
    for (int i : ordre) {
      Mobile& m = m_mobiles[static_cast<std::size_t>(i)];
      m.quart = 0;
      const Boite b = m.boites[0];
      if ((versLaDroite && x + b.largeur() > m_interieur.x1) || (!versLaDroite && x - b.largeur() < m_interieur.x0)) {
        y += hauteurRangee;
        hauteurRangee = 0;
        versLaDroite = !versLaDroite;
        x = versLaDroite ? m_interieur.x0 : m_interieur.x1;
      }
      const Nm gauche = versLaDroite ? x : x - b.largeur();
      m.position = surGrille({gauche - b.x0, y - b.y0});
      x = versLaDroite ? x + b.largeur() : x - b.largeur();
      hauteurRangee = std::max(hauteurRangee, b.hauteur());
    }
  }
  bool recuire() {
    std::uniform_real_distribution<double> u(0, 1);
    std::uniform_int_distribution<std::size_t> choix(0, m_mobiles.size() - 1);
    double total = coutTotal();
    // Température de départ : l'écart moyen d'une vingtaine de déplacements au hasard
    double t0 = 0;
    int n0 = 0;
    const double largeur = mmD(m_interieur.largeur()), hauteur = mmD(m_interieur.hauteur());
    for (int s = 0; s < 40; ++s) {
      const std::size_t i = choix(m_alea);
      const Mobile avant = m_mobiles[i];
      const double c0 = coutLocal(i, -1);
      m_mobiles[i].position = surGrille({m_mobiles[i].position.x + static_cast<Nm>((u(m_alea) - 0.5) * largeur * 0.3e6), m_mobiles[i].position.y + static_cast<Nm>((u(m_alea) - 0.5) * hauteur * 0.3e6)});
      t0 += std::abs(coutLocal(i, -1) - c0);
      ++n0;
      m_mobiles[i] = avant;
    }
    t0 = std::max(1.0, t0 / std::max(1, n0));
    const double tFin = t0 * 1e-4;
    const int n = std::max(1, m_o.iterations);
    for (int it = 0; it < n; ++it) {
      if (m_o.arret && (it & 255) == 0 && m_o.arret->load()) return false;
      const double avance = static_cast<double>(it) / n;
      const double t = t0 * std::pow(tFin / t0, avance);
      const double rayon = std::max(1.0, 0.5 * std::max(largeur, hauteur) * (1 - avance));
      const std::size_t i = choix(m_alea);
      const double tirage = u(m_alea);
      int j = -1;
      const Mobile ai = m_mobiles[i];
      Mobile aj;
      if (tirage < 0.15 && m_mobiles.size() > 1) {
        j = static_cast<int>(choix(m_alea));
        if (static_cast<std::size_t>(j) == i) continue;
        aj = m_mobiles[static_cast<std::size_t>(j)];
      }
      const double c0 = coutLocal(i, j);
      if (j >= 0) std::swap(m_mobiles[i].position, m_mobiles[static_cast<std::size_t>(j)].position);
      else if (tirage < 0.25 && m_o.tourner)
        m_mobiles[i].quart = (m_mobiles[i].quart + 1 + static_cast<int>(u(m_alea) * 3)) % 4;
      else
        m_mobiles[i].position = surGrille({m_mobiles[i].position.x + static_cast<Nm>((u(m_alea) * 2 - 1) * rayon * 1e6), m_mobiles[i].position.y + static_cast<Nm>((u(m_alea) * 2 - 1) * rayon * 1e6)});
      const double delta = coutLocal(i, j) - c0;
      if (delta <= 0 || u(m_alea) < std::exp(-delta / t)) {
        total += delta;
      } else {
        m_mobiles[i] = ai;
        if (j >= 0) m_mobiles[static_cast<std::size_t>(j)] = aj;
      }
    }
    (void)total;
    return true;
  }
  bool libre(std::size_t i) const {
    const Boite b = boiteDe(i);
    if (!m_interieur.contient(b)) return false;
    for (std::size_t j = 0; j < m_mobiles.size(); ++j)
      if (j != i && aireCommune(b, boiteDe(j)) > 0) return false;
    for (const auto& f : m_fixes)
      if (aireCommune(b, f) > 0) return false;
    return true;
  }
  // Ce qui recouvre encore ou sort : la place libre la plus proche, en spirale sur la grille
  void legaliser() {
    for (int passe = 0; passe < 3; ++passe)
      for (std::size_t i = 0; i < m_mobiles.size(); ++i) {
        if (libre(i)) continue;
        const Mobile depart = m_mobiles[i];
        bool trouve = false;
        const Nm pas = std::max<Nm>(m_o.pas, 250'000);
        const int rayonMax = static_cast<int>(std::max(m_interieur.largeur(), m_interieur.hauteur()) / pas) + 1;
        for (int r = 1; r <= rayonMax && !trouve; ++r)
          for (int dx = -r; dx <= r && !trouve; ++dx)
            for (int dy = -r; dy <= r && !trouve; ++dy) {
              if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
              for (int q = 0; q < (m_o.tourner ? 4 : 1) && !trouve; ++q) {
                m_mobiles[i].quart = (depart.quart + q) % 4;
                m_mobiles[i].position = {depart.position.x + dx * pas, depart.position.y + dy * pas};
                trouve = libre(i);
              }
            }
        if (!trouve) m_mobiles[i] = depart;
      }
  }
  void ecrire() {
    for (const auto& m : m_mobiles) {
      EmpreintePlacee& e = m_c.circuit.empreintes[m.empreinte];
      e.position = m.position;
      e.rotation = std::fmod(m.rotation0 + 90.0 * m.quart, 360.0);
      e.repere.reset();  // le repère reprend sa place par défaut
    }
  }

  ConceptionElec& m_c;
  const OptionsPlacementAuto& m_o;
  std::mt19937 m_alea;
  Boite m_carte, m_interieur;
  std::vector<Mobile> m_mobiles;
  std::vector<Boite> m_fixes;
  std::vector<std::vector<Membre>> m_reseaux;
  std::vector<std::vector<int>> m_reseauxDe;
  std::vector<double> m_longueur;
};
}  // namespace

ResultatPlacementAuto placerAutomatiquement(ConceptionElec& c, const OptionsPlacementAuto& o) { return Recuit(c, o).lancer(); }

double longueurChevelu(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  std::map<std::string, std::vector<Pt>> parReseau;
  for (const auto& p : pastilles(c, k))
    if (!p.reseau.empty()) parReseau[p.reseau].push_back(p.pastille.position);
  double total = 0;
  for (const auto& [nom, pts] : parReseau) {
    if (pts.size() < 2) continue;
    // Arbre couvrant minimal (Prim)
    std::vector<double> dist(pts.size(), 1e300);
    std::vector<char> dans(pts.size(), 0);
    dist[0] = 0;
    for (std::size_t n = 0; n < pts.size(); ++n) {
      std::size_t u = 0;
      double best = 1e301;
      for (std::size_t i = 0; i < pts.size(); ++i)
        if (!dans[i] && dist[i] < best) best = dist[i], u = i;
      dans[u] = 1;
      total += dist[u];
      for (std::size_t i = 0; i < pts.size(); ++i)
        if (!dans[i]) dist[i] = std::min(dist[i], std::hypot(mmD(pts[i].x - pts[u].x), mmD(pts[i].y - pts[u].y)));
    }
  }
  return total;
}

int collisionsPlacement(const ConceptionElec& c, Nm ecart) {
  const Boite carte = boite(c.circuit.contour.polygone());
  std::vector<Boite> b;
  for (const auto& e : c.circuit.empreintes)
    if (const DefEmpreinte* d = c.empreinte(e.empreinte)) b.push_back(boite(encombrementSurCarte(e, *d)).gonflee(ecart / 2));
  int n = 0;
  for (std::size_t i = 0; i < b.size(); ++i) {
    bool touche = !carte.contient(b[i]);
    for (std::size_t j = 0; j < b.size() && !touche; ++j)
      if (j != i && aireCommune(b[i], b[j]) > 0) touche = true;
    n += touche;
  }
  return n;
}

}  // namespace prom::ecad
