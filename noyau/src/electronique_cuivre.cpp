// Prométhée : cuivre du circuit imprimé — zones remplies (isolements, rayons thermiques, épaisseur minimale, îlots),
// connectivité du cuivre, chevelu, courts-circuits et vérification des règles de conception (DRC), le tout appuyé
// sur l'index spatial. Licence GPL-3.0-only.
#include <algorithm>
#include <memory>
#include <thread>
#include <future>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <map>
#include <set>

#include "promethee/electronique.hpp"
#include "promethee/remplissage_local.hpp"

namespace prom::ecad {

namespace {
struct Objet {
  enum class Type { Pastille, Piste, Via } type = Type::Piste;
  int indice = 0;
  std::string reseau;
  Couches couches = 0;
  Polygone forme;  // pastille
  Pt a, b;         // piste : segment ; via : centre
  Nm largeur = 0;  // piste : largeur ; via : diamètre
  Boite boite;
  int empreinte = -1;
  // Trait d'un texte posé sur une couche de cuivre (C20) : du cuivre sans réseau, compté par les zones, la
  // connectivité et la DRC comme une piste, mais sans bout libre à signaler
  bool texte = false;
  std::string libelle;  // texte de cuivre : son contenu
};

// Zones avec le nom actuel de leur réseau (un ancien nom figé suit son réseau : EL-01)
std::vector<Zone> zonesResolues(const ConceptionElec& c, const Connectivite& k) {
  std::vector<Zone> r = c.circuit.zones;
  for (auto& z : r) z.reseau = k.canonique(z.reseau);
  return r;
}

std::vector<Objet> objetsCuivre(const ConceptionElec& c, const std::vector<PastillePlacee>& pp, const Connectivite& k) {
  std::vector<Objet> r;
  const Couches cuivres = tousLesCuivres(c.circuit.empilage.couchesCuivre);
  for (std::size_t i = 0; i < pp.size(); ++i) {
    // Une pastille CMS sans cuivre (face absente d'une carte simple face) reste un objet sans couche : elle ne
    // touche rien, mais le chevelu la compte (R2)
    if (!(pp[i].couches & cuivres) && !pp[i].inaccessible) continue;
    Objet o;
    o.type = Objet::Type::Pastille;
    o.indice = static_cast<int>(i);
    o.reseau = pp[i].reseau;
    o.couches = pp[i].couches & cuivres;
    o.forme = pp[i].forme;
    o.a = o.b = pp[i].pastille.position;
    o.boite = boite(o.forme);
    o.empreinte = pp[i].empreinte;
    r.push_back(std::move(o));
  }
  for (std::size_t i = 0; i < c.circuit.pistes.size(); ++i) {
    const Piste& p = c.circuit.pistes[i];
    Objet o;
    o.type = Objet::Type::Piste;
    o.indice = static_cast<int>(i);
    o.reseau = k.canonique(p.reseau);
    o.couches = bit(p.couche);
    o.a = p.a;
    o.b = p.b;
    o.largeur = p.largeur;
    o.boite.etendre(p.a);
    o.boite.etendre(p.b);
    o.boite = o.boite.gonflee(p.largeur / 2);
    r.push_back(std::move(o));
  }
  for (std::size_t i = 0; i < c.circuit.vias.size(); ++i) {
    const Via& v = c.circuit.vias[i];
    Objet o;
    o.type = Objet::Type::Via;
    o.indice = static_cast<int>(i);
    o.reseau = k.canonique(v.reseau);
    o.couches = cuivres;
    o.a = o.b = v.p;
    o.largeur = v.diametre;
    o.boite.etendre(v.p);
    o.boite = o.boite.gonflee(v.diametre / 2);
    r.push_back(std::move(o));
  }
  // Textes posés sur une couche de cuivre (C20) : chaque trait de la police est du cuivre sans réseau
  for (const auto& t : textesSerigraphie(c)) {
    if (!estCuivre(t.couche) || !(bit(t.couche) & cuivres)) continue;
    Objet o;
    o.type = Objet::Type::Piste;
    o.texte = true;
    o.libelle = t.texte;
    o.indice = -1;
    o.couches = bit(t.couche);
    o.a = t.a;
    o.b = t.b;
    o.largeur = t.epaisseur;
    o.boite.etendre(t.a);
    o.boite.etendre(t.b);
    o.boite = o.boite.gonflee(t.epaisseur / 2);
    r.push_back(std::move(o));
  }
  return r;
}

Polygone contourObjet(const Objet& o, Nm marge) {
  switch (o.type) {
    case Objet::Type::Pastille: {
      const Polygones g = gonfler({o.forme}, marge);
      return g.empty() ? o.forme : g.front();
    }
    case Objet::Type::Piste: return oblong(o.a, o.b, o.largeur + 2 * marge);
    case Objet::Type::Via: return cercle(o.a, o.largeur / 2 + marge);
  }
  return {};
}

// Distance bord à bord (0 s'ils se touchent)
double ecart(const Objet& x, const Objet& y) {
  using T = Objet::Type;
  const Objet& a = x.type <= y.type ? x : y;
  const Objet& b = x.type <= y.type ? y : x;
  double d = 0;
  if (a.type == T::Pastille && b.type == T::Pastille) d = distancePolygones(a.forme, b.forme);
  else if (a.type == T::Pastille && b.type == T::Piste) d = distancePolygoneSegment(a.forme, b.a, b.b) - static_cast<double>(b.largeur) / 2;
  else if (a.type == T::Pastille && b.type == T::Via) d = distancePolygoneSegment(a.forme, b.a, b.a) - static_cast<double>(b.largeur) / 2;
  else if (a.type == T::Piste && b.type == T::Piste) d = distanceSegments(a.a, a.b, b.a, b.b) - static_cast<double>(a.largeur + b.largeur) / 2;
  else if (a.type == T::Piste && b.type == T::Via) d = distancePointSegment(b.a, a.a, a.b) - static_cast<double>(a.largeur + b.largeur) / 2;
  else d = distance(a.a, b.a) - static_cast<double>(a.largeur + b.largeur) / 2;
  return std::max(0.0, d);
}

// Cuivre d'une zone (ou d'un îlot) prêt pour des mesures rapides : ses arêtes rangées dans un index spatial. La
// distance d'un objet ne regarde que les arêtes proches, au lieu de tout le contour (qui fait des milliers de
// sommets sur une grande carte).
__extension__ typedef __int128 Grand;
class CuivreIndexe {
 public:
  explicit CuivreIndexe(const Polygones& p) : m_poly(p), m_index(NM_PAR_MM) {
    for (std::size_t k = 0; k < p.size(); ++k) {
      const auto& poly = p[k];
      if (poly.size() < 3) {
        m_signes.push_back(0);
        continue;
      }
      Grand aire = 0;
      for (std::size_t i = 0; i < poly.size(); ++i) {
        const Pt a = poly[i], b = poly[(i + 1) % poly.size()];
        aire += static_cast<Grand>(a.x) * b.y - static_cast<Grand>(b.x) * a.y;
        Boite bo;
        bo.etendre(a);
        bo.etendre(b);
        m_index.inserer(static_cast<int>(m_aretes.size()), bo);
        m_aretes.push_back({a, b});
        m_polygone.push_back(static_cast<int>(k));
        m_boite.etendre(a);
      }
      m_signes.push_back(aire >= 0 ? 1 : -1);
    }
  }
  // Point dans le cuivre (bord compris, trous exclus), comme dedans(Polygones) : seules les arêtes que croise la
  // demi-droite vers +x sont regardées, par l'index (9.2)
  bool contient(Pt q) const {
    if (m_boite.vide() || !m_boite.contient(q)) return false;
    std::vector<std::pair<int, char>> etats;  // polygone → 1 : croisements impairs ; 2 : sur le bord
    auto etat = [&etats](int k) -> char& {
      for (auto& [x, e] : etats)
        if (x == k) return e;
      etats.push_back({k, 0});
      return etats.back().second;
    };
    // La demi-droite part vers le bord le plus proche de la boîte (moins de cases à lire) ; la parité des croisements
    // ne dépend pas de la direction. Les coordonnées sont tournées pour ramener la direction à +x.
    const Nm versXp = m_boite.x1 - q.x, versXm = q.x - m_boite.x0, versYp = m_boite.y1 - q.y, versYm = q.y - m_boite.y0;
    const Nm plusCourt = std::min({versXp, versXm, versYp, versYm});
    const int sens = plusCourt == versXp ? 0 : plusCourt == versXm ? 1 : plusCourt == versYp ? 2 : 3;
    const Boite rayon = sens == 0 ? Boite{q.x, q.y, m_boite.x1, q.y} : sens == 1 ? Boite{m_boite.x0, q.y, q.x, q.y} : sens == 2 ? Boite{q.x, q.y, q.x, m_boite.y1} : Boite{q.x, m_boite.y0, q.x, q.y};
    auto tourner = [sens](Pt p) { return sens == 0 ? p : sens == 1 ? Pt{-p.x, p.y} : sens == 2 ? Pt{p.y, p.x} : Pt{-p.y, p.x}; };
    const Pt qt = tourner(q);
    for (int i : m_index.chercher(rayon)) {
      const auto& [a0, b0] = m_aretes[static_cast<std::size_t>(i)];
      char& e = etat(m_polygone[static_cast<std::size_t>(i)]);
      if (e == 2) continue;
      if (q.x >= std::min(a0.x, b0.x) && q.x <= std::max(a0.x, b0.x) && q.y >= std::min(a0.y, b0.y) && q.y <= std::max(a0.y, b0.y) &&
          static_cast<Grand>(b0.x - a0.x) * (q.y - a0.y) == static_cast<Grand>(b0.y - a0.y) * (q.x - a0.x)) {
        e = 2;
        continue;
      }
      const Pt a = tourner(a0), b = tourner(b0);
      if ((a.y > qt.y) != (b.y > qt.y)) {
        const Grand dy = b.y - a.y;
        const Grand num = static_cast<Grand>(a.x - qt.x) * dy + static_cast<Grand>(qt.y - a.y) * (b.x - a.x);
        if ((num > 0) == (dy > 0)) e ^= 1;
      }
    }
    int enroulement = 0;
    for (const auto& [k, e] : etats)
      if (e) enroulement += m_signes[static_cast<std::size_t>(k)];
    return enroulement != 0;
  }
  // Distance de l'objet au cuivre : 0 s'il le touche ou s'y trouve ; au-delà de « portee », une valeur plus grande
  double distance(const Objet& o, Nm portee) const {
    if (contient(o.a) || (o.type == Objet::Type::Piste && contient(o.b))) return 0;
    double d = static_cast<double>(portee) + 1;
    for (int e : m_index.chercher(o.boite.gonflee(portee + 1))) {
      const auto& [a, b] = m_aretes[static_cast<std::size_t>(e)];
      switch (o.type) {
        case Objet::Type::Pastille: d = std::min(d, distancePolygoneSegment(o.forme, a, b)); break;
        case Objet::Type::Piste: d = std::min(d, distanceSegments(a, b, o.a, o.b) - static_cast<double>(o.largeur) / 2); break;
        case Objet::Type::Via: d = std::min(d, distancePointSegment(o.a, a, b) - static_cast<double>(o.largeur) / 2); break;
      }
      if (d <= 0) return 0;
    }
    return std::max(0.0, d);
  }
  const Polygones& polygones() const { return m_poly; }

 private:
  const Polygones& m_poly;
  IndexSpatial m_index;
  std::vector<std::pair<Pt, Pt>> m_aretes;
  std::vector<int> m_polygone;  // polygone de chaque arête
  std::vector<int> m_signes;    // +1 contour, −1 trou (sens de parcours)
  Boite m_boite;
};

std::string decrire(const Objet& o, const std::vector<PastillePlacee>& pp) {
  const std::string r = o.reseau.empty() ? std::string() : " (" + o.reseau + ")";
  switch (o.type) {
    case Objet::Type::Pastille: return "pastille " + pp[static_cast<std::size_t>(o.indice)].reference + "." + pp[static_cast<std::size_t>(o.indice)].numero + r;
    case Objet::Type::Piste: return o.texte ? "texte de cuivre « " + o.libelle + " »" : "piste" + r;
    case Objet::Type::Via: return "via" + r;
  }
  return {};
}

struct Unions {
  std::vector<int> p;
  explicit Unions(std::size_t n) : p(n) { for (std::size_t i = 0; i < n; ++i) p[i] = static_cast<int>(i); }
  int racine(int x) {
    while (p[static_cast<std::size_t>(x)] != x) { p[static_cast<std::size_t>(x)] = p[static_cast<std::size_t>(p[static_cast<std::size_t>(x)])]; x = p[static_cast<std::size_t>(x)]; }
    return x;
  }
  void unir(int a, int b) { a = racine(a); b = racine(b); if (a != b) p[static_cast<std::size_t>(std::max(a, b))] = std::min(a, b); }
};
// Marge ajoutée aux isolements lors du remplissage : couvre l'écart entre un arc et son polygone (2 µm côté
// objet, 2 µm côté décalage) et les arrondis au nanomètre, pour que le cuivre rempli respecte la règle et passe
// la vérification indépendante (DRC-24). 5 µm restent négligeables devant un isolement de 0,1 mm et plus.
constexpr Nm MARGE_REMPLISSAGE = 5'000;
// Deux cuivres sont en contact à moins d'un micromètre (approximation des arcs en polygones)
constexpr Nm PORTEE_CONTACT = 1'000;
// Rayons thermiques : plus larges que l'épaisseur minimale de la zone, sinon le remplissage les efface (EL-02)
Nm largeurRayonEffective(const Zone& z) { return z.epaisseurMin > 0 ? std::max(z.largeurRayon, z.epaisseurMin + 20'000) : z.largeurRayon; }

std::string mmTexte(double v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.3f", v * 1e-6);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s + " mm";
}
}  // namespace

namespace {
// Ouverture morphologique (érosion puis dilatation de « r ») : retire le cuivre plus étroit que 2r. Sur un grand
// remplissage, le calcul se fait par bandes verticales en parallèle (9.2) : le résultat en un point ne dépend que
// du cuivre à moins de 2r, chaque bande est donc calculée sur elle-même élargie de 2r, puis rognée à sa largeur.
Polygones ouverture(const Polygones& f, Nm r) {
  std::size_t sommets = 0;
  for (const auto& p : f) sommets += p.size();
  const unsigned fils = std::max(2u, std::min(8u, std::thread::hardware_concurrency()));
  if (sommets < 20'000) return gonfler(gonfler(f, -r), r);
  const Boite b = boite(f);
  const int bandes = static_cast<int>(2 * fils);
  const Nm marge = 2 * r + 10'000;
  std::vector<Polygones> morceaux(static_cast<std::size_t>(bandes));
  auto calculer = [&](int i) {
    const Nm x0 = b.x0 + (b.largeur() + 1) * i / bandes, x1 = b.x0 + (b.largeur() + 1) * (i + 1) / bandes;
    const Polygone large{{x0 - marge, b.y0 - 1}, {x1 + marge, b.y0 - 1}, {x1 + marge, b.y1 + 1}, {x0 - marge, b.y1 + 1}};
    const Polygone etroit{{i == 0 ? b.x0 - 1 : x0, b.y0 - 1}, {i == bandes - 1 ? b.x1 + 1 : x1, b.y0 - 1}, {i == bandes - 1 ? b.x1 + 1 : x1, b.y1 + 1}, {i == 0 ? b.x0 - 1 : x0, b.y1 + 1}};
    const Polygones local = intersecter(f, {large});
    morceaux[static_cast<std::size_t>(i)] = intersecter(gonfler(gonfler(local, -r), r), {etroit});
  };
  std::vector<std::thread> t;
  for (int i = 1; i < bandes; ++i) t.emplace_back(calculer, i);
  calculer(0);
  for (auto& x : t) x.join();
  Polygones tout;
  for (auto& m : morceaux) tout.insert(tout.end(), m.begin(), m.end());
  return unir(tout);
}
}  // namespace

Remplissages remplirZones(const ConceptionElec& c, const Connectivite& k) {
  Remplissages r;
  const auto pp = pastilles(c, k);
  const auto objets = objetsCuivre(c, pp, k);
  const std::vector<Zone> zs = zonesResolues(c, k);
  const Regles R = reglesResolues(c.circuit.regles, k);
  // Matière de la carte, rognée de la marge au bord : contour extérieur et découpes internes (EL-44)
  Polygones interieur = gonfler({c.circuit.contour.polygone()}, -(R.bord + MARGE_REMPLISSAGE));
  if (!c.circuit.contour.decoupes.empty()) {
    Polygones trous;
    for (const auto& d : c.circuit.contour.decoupes)
      if (d.size() >= 3) trous.push_back(orienter(d));
    interieur = soustraire(interieur, gonfler(trous, R.bord + MARGE_REMPLISSAGE));
  }
  std::vector<const Zone*> zones;
  for (const auto& z : zs) zones.push_back(&z);
  std::stable_sort(zones.begin(), zones.end(), [](const Zone* a, const Zone* b) { return a->priorite > b->priorite; });
  auto isolementZone = [&R](const Zone& z) { return z.isolement > 0 ? z.isolement : R.isolement; };
  // Plus grand isolement en jeu : règle générale, classes, zones. Le voisinage examiné autour d'une zone le couvre
  // toujours, pour qu'un isolement de classe de plusieurs millimètres (haute tension) voie ses obstacles (C19).
  Nm isoMax = R.isolement;
  for (const auto& cl : R.classes) isoMax = std::max(isoMax, cl.isolement);
  for (const auto& z : zs) isoMax = std::max(isoMax, isolementZone(z));
  isoMax = std::max(isoMax, R.cuivreTrou);
  // Îlots retirés faute de liaison, gardés pour la seconde passe (C21)
  struct Retire {
    const Zone* zone;
    Polygones ilot;
  };
  std::vector<Retire> retires;
  for (const Zone* z : zones) {
    if (z->contour.size() < 3) { r[z->uuid] = {}; continue; }
    const Polygones base = intersecter({orienter(z->contour)}, interieur);
    const Nm isoZone = isolementZone(*z);
    const Boite bz = boite(base).gonflee(std::max<Nm>(5 * NM_PAR_MM, isoMax + MARGE_REMPLISSAGE));
    Polygones autres, anneaux, rayons;
    std::vector<const Objet*> memeReseau;
    for (const Objet& o : objets) {
      if (!(o.couches & bit(z->couche)) || !o.boite.touche(bz)) continue;
      if (!z->reseau.empty() && o.reseau == z->reseau) {
        memeReseau.push_back(&o);
        if (o.type == Objet::Type::Pastille && z->thermique) {
          for (auto& x : gonfler({o.forme}, z->ecartThermique)) anneaux.push_back(x);
          const Pastille& pa = pp[static_cast<std::size_t>(o.indice)].pastille;
          const Nm longueur = std::max(o.boite.largeur(), o.boite.hauteur()) + 2 * z->ecartThermique + 2 * z->epaisseurMin;
          const Nm l = largeurRayonEffective(*z);
          rayons.push_back(rectangle(pa.position, longueur, l, pa.rotation));
          rayons.push_back(rectangle(pa.position, l, longueur, pa.rotation));
        }
        continue;
      }
      autres.push_back(contourObjet(o, std::max(isoZone, isolementEntre(R, z->reseau, o.reseau)) + MARGE_REMPLISSAGE));
    }
    for (const auto& p : pp)
      if (p.pastille.type == TypePastille::NonMetallisee) autres.push_back(cercle(p.pastille.position, p.pastille.percage / 2 + std::max(isoZone, R.cuivreTrou) + MARGE_REMPLISSAGE));
    // Zones déjà remplies (priorité plus haute) d'un autre réseau : écartées du plus grand de leurs deux isolements
    // et de l'isolement entre leurs classes, quel que soit le sens (R1)
    for (const Zone* y : zones) {
      if (y == z) break;
      if (y->couche == z->couche && y->reseau != z->reseau) {
        const Nm requis = std::max({isoZone, isolementZone(*y), isolementEntre(R, z->reseau, y->reseau)});
        for (auto& x : gonfler(r[y->uuid], requis + MARGE_REMPLISSAGE)) autres.push_back(x);
      }
    }
    Polygones obstacles = autres;
    obstacles.insert(obstacles.end(), anneaux.begin(), anneaux.end());
    Polygones rempli = soustraire(base, obstacles);
    if (!rayons.empty()) {
      Polygones r2 = soustraire(intersecter(rayons, base), autres);
      rempli.insert(rempli.end(), r2.begin(), r2.end());
      rempli = unir(rempli);
    }
    if (z->epaisseurMin > 0) rempli = ouverture(rempli, z->epaisseurMin / 2);
    // Îlots : on ne garde que le cuivre relié à une pastille, une via ou une piste du réseau de la zone, ou au cuivre
    // déjà gardé d'une autre zone du même réseau sur la même couche (C21)
    Polygones garde;
    for (const Ilot& il : ilots(rempli)) {
      Polygones ilp{il.exterieur};
      ilp.insert(ilp.end(), il.trous.begin(), il.trous.end());
      const Boite bi = boite(il.exterieur);
      bool relie = z->reseau.empty();
      std::optional<CuivreIndexe> cuivre;
      for (const Objet* o : memeReseau) {
        if (relie) break;
        if (!o->boite.gonflee(PORTEE_CONTACT).touche(bi)) continue;
        if (!cuivre) cuivre.emplace(ilp);
        // Contact réel du cuivre, rayons thermiques compris (EL-02) : jamais de liaison « à distance »
        relie = cuivre->distance(*o, PORTEE_CONTACT) <= static_cast<double>(PORTEE_CONTACT);
      }
      for (const Zone* y : zones) {
        if (relie || y == z) break;
        if (y->couche != z->couche || y->reseau != z->reseau) continue;
        const auto it = r.find(y->uuid);
        relie = it != r.end() && !it->second.empty() && boite(it->second).gonflee(PORTEE_CONTACT).touche(bi) && !intersecter(gonfler(ilp, PORTEE_CONTACT), it->second).empty();
      }
      if (relie) garde.insert(garde.end(), ilp.begin(), ilp.end());
      else retires.push_back({z, ilp});
    }
    r[z->uuid] = garde;
  }
  // Seconde passe (C21) : un îlot retiré qui touche le cuivre gardé d'une zone sœur (même réseau, même couche),
  // remplie après lui, est relié par elle. Il revient s'il ne gêne aucune zone d'un autre réseau remplie depuis
  // (celle-ci a pu occuper la place qu'il laissait) : jamais de recouvrement ni d'isolement violé.
  for (bool change = true; change;) {
    change = false;
    for (auto& x : retires) {
      if (x.ilot.empty()) continue;
      const Zone& z = *x.zone;
      const Boite bi = boite(x.ilot);
      bool relie = false, gene = false;
      for (const Zone* y : zones) {
        if (y == x.zone || y->couche != z.couche) continue;
        const auto it = r.find(y->uuid);
        if (it == r.end() || it->second.empty()) continue;
        if (y->reseau == z.reseau && !z.reseau.empty()) {
          relie = relie || (boite(it->second).gonflee(PORTEE_CONTACT).touche(bi) && !intersecter(gonfler(x.ilot, PORTEE_CONTACT), it->second).empty());
        } else {
          const Nm requis = std::max({isolementZone(z), isolementZone(*y), isolementEntre(R, z.reseau, y->reseau)});
          gene = gene || (boite(it->second).gonflee(requis).touche(bi) && !intersecter(gonfler(x.ilot, requis), it->second).empty());
        }
      }
      if (!relie || gene) continue;
      Polygones& cuivreZone = r[z.uuid];
      cuivreZone.insert(cuivreZone.end(), x.ilot.begin(), x.ilot.end());
      x.ilot.clear();
      change = true;
    }
  }
  return r;
}

EtatRoutage etatRoutage(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis) {
  EtatRoutage e;
  const auto pp = pastilles(c, k);
  const auto objets = objetsCuivre(c, pp, k);
  const std::vector<Zone> zs = zonesResolues(c, k);
  // Îlots des zones : objets supplémentaires pour la connectivité
  struct IlotZone {
    Polygones poly;
    Couche couche;
    std::string reseau;
    Boite b;
  };
  std::vector<IlotZone> ilotsZones;
  for (const auto& z : zs) {
    const auto it = remplis.find(z.uuid);
    if (it == remplis.end()) continue;
    for (const Ilot& il : ilots(it->second)) {
      IlotZone x{{il.exterieur}, z.couche, z.reseau, boite(il.exterieur)};
      x.poly.insert(x.poly.end(), il.trous.begin(), il.trous.end());
      ilotsZones.push_back(std::move(x));
    }
  }
  const std::size_t n = objets.size();
  Unions u(n + ilotsZones.size());
  struct Contact {
    int objet;  // un des deux cuivres (son morceau est celui du court-circuit)
    Pt position;
    bool deuxReseaux = true;  // faux : un cuivre sans réseau (texte, piste orpheline) touche un réseau
  };
  std::vector<Contact> contacts;  // contacts entre cuivres de réseaux différents, avec leur position
  IndexSpatial index(2 * NM_PAR_MM);
  for (std::size_t i = 0; i < n; ++i) index.inserer(static_cast<int>(i), objets[i].boite.gonflee(1));
  for (std::size_t i = 0; i < n; ++i)
    for (int j : index.chercher(objets[i].boite.gonflee(1))) {
      if (j <= static_cast<int>(i) || !(objets[i].couches & objets[static_cast<std::size_t>(j)].couches)) continue;
      const Objet& a = objets[i];
      const Objet& b = objets[static_cast<std::size_t>(j)];
      if (ecart(a, b) <= 1.0) {
        u.unir(static_cast<int>(i), j);
        if (a.reseau != b.reseau) {
          Boite x = a.boite;
          x.etendre(b.boite);
          Boite commun{std::max(a.boite.x0, b.boite.x0), std::max(a.boite.y0, b.boite.y0), std::min(a.boite.x1, b.boite.x1), std::min(a.boite.y1, b.boite.y1)};
          contacts.push_back({static_cast<int>(i), commun.vide() ? x.centre() : commun.centre(), !a.reseau.empty() && !b.reseau.empty()});
        }
      }
    }
  // Pastilles de même numéro d'une même empreinte : reliées dans le composant (pattes d'un bouton, languette d'un
  // SOT-223) ; elles n'ont pas de liaison à tracer entre elles (EL-15)
  {
    std::map<std::pair<int, std::string>, std::size_t> premiere;
    for (std::size_t i = 0; i < n; ++i) {
      if (objets[i].type != Objet::Type::Pastille) continue;
      const PastillePlacee& p = pp[static_cast<std::size_t>(objets[i].indice)];
      if (p.numero.empty()) continue;
      const auto [it, neuf] = premiere.emplace(std::make_pair(objets[i].empreinte, p.numero), i);
      if (!neuf) u.unir(static_cast<int>(it->second), static_cast<int>(i));
    }
  }
  // Îlots de deux zones du même réseau sur la même couche qui se touchent ou se recouvrent : un seul morceau de
  // cuivre (C21). Sans cela, deux plans de masse superposés donnaient une fausse liaison non routée.
  for (std::size_t a = 0; a < ilotsZones.size(); ++a)
    for (std::size_t b = a + 1; b < ilotsZones.size(); ++b) {
      const IlotZone& x = ilotsZones[a];
      const IlotZone& y = ilotsZones[b];
      if (x.couche != y.couche || x.reseau.empty() || x.reseau != y.reseau || !x.b.gonflee(PORTEE_CONTACT).touche(y.b)) continue;
      if (!intersecter(gonfler(x.poly, PORTEE_CONTACT), y.poly).empty()) u.unir(static_cast<int>(n + a), static_cast<int>(n + b));
    }
  for (std::size_t z = 0; z < ilotsZones.size(); ++z) {
    const IlotZone& il = ilotsZones[z];
    const CuivreIndexe cuivre(il.poly);
    for (int j : index.chercher(il.b)) {
      const Objet& o = objets[static_cast<std::size_t>(j)];
      if (!(o.couches & bit(il.couche))) continue;
      // Tout cuivre qui touche réellement l'îlot y est relié, pastilles comprises (par leurs rayons thermiques,
      // s'ils existent encore : EL-02) ; c'est un court-circuit s'il est d'un autre réseau.
      const bool memeReseau = !il.reseau.empty() && o.reseau == il.reseau;
      const Nm portee = memeReseau ? PORTEE_CONTACT : 1;
      if (cuivre.distance(o, portee) <= static_cast<double>(portee)) {
        u.unir(static_cast<int>(n + z), j);
        if (!memeReseau && !o.reseau.empty() && !il.reseau.empty()) contacts.push_back({j, o.a});
      }
    }
  }
  // Courts-circuits : deux réseaux dans un même morceau de cuivre
  std::map<int, std::set<std::string>> reseauxParMorceau;
  for (std::size_t i = 0; i < n; ++i)
    if (!objets[i].reseau.empty()) reseauxParMorceau[u.racine(static_cast<int>(i))].insert(objets[i].reseau);
  for (std::size_t z = 0; z < ilotsZones.size(); ++z)
    if (!ilotsZones[z].reseau.empty()) reseauxParMorceau[u.racine(static_cast<int>(n + z))].insert(ilotsZones[z].reseau);
  // Liaisons de réseaux (2.33) : le morceau de cuivre qui ne réunit que les deux réseaux d'une liaison, par ses
  // pastilles, est le pont voulu, pas un court-circuit
  std::map<int, std::set<std::string>> reseauxLiaison;  // empreinte → ses réseaux
  for (const auto& p : pp) {
    const ComposantElec* x = c.composant(p.composant);
    const DefSymbole* d = x ? c.symbole(x->symbole) : nullptr;
    if (d && d->liaisonReseaux && !p.reseau.empty()) reseauxLiaison[p.empreinte].insert(p.reseau);
  }
  auto pontVoulu = [&](int rac, const std::set<std::string>& ens) {
    for (const auto& [emp, reseaux] : reseauxLiaison) {
      if (reseaux != ens) continue;
      for (std::size_t i = 0; i < n; ++i)
        if (objets[i].type == Objet::Type::Pastille && objets[i].empreinte == emp && u.racine(static_cast<int>(i)) == rac) return true;
    }
    return false;
  };
  for (const auto& [rac, ens] : reseauxParMorceau) {
    if (ens.size() < 2 || pontVoulu(rac, ens)) continue;
    CourtCircuit cc{*ens.begin(), *std::next(ens.begin()), {}};
    // Position : un contact entre deux réseaux différents dans ce morceau de cuivre ; à défaut, le contact d'un
    // cuivre sans réseau qui fait le pont (texte en cuivre, piste orpheline : C20)
    const auto dansMorceau = [&](bool deuxReseaux) {
      return std::find_if(contacts.begin(), contacts.end(), [&](const Contact& k2) { return k2.deuxReseaux == deuxReseaux && u.racine(k2.objet) == rac; });
    };
    if (auto it = dansMorceau(true); it != contacts.end()) cc.position = it->position;
    else if (auto jt = dansMorceau(false); jt != contacts.end()) cc.position = jt->position;
    e.courtsCircuits.push_back(cc);
  }
  // Chevelu : par réseau, arbre couvrant minimal entre les morceaux de cuivre
  std::map<std::string, std::vector<std::size_t>> pastillesParReseau;
  for (std::size_t i = 0; i < n; ++i)
    if (objets[i].type == Objet::Type::Pastille && !objets[i].reseau.empty()) pastillesParReseau[objets[i].reseau].push_back(i);
  for (const auto& [reseau, liste] : pastillesParReseau) {
    if (liste.size() < 2) continue;
    ++e.reseauxTotal;
    struct Arete {
      double d;
      std::size_t a, b;
    };
    std::vector<Arete> aretes;
    for (std::size_t x = 0; x < liste.size(); ++x)
      for (std::size_t y = x + 1; y < liste.size(); ++y) {
        if (u.racine(static_cast<int>(liste[x])) == u.racine(static_cast<int>(liste[y]))) continue;
        aretes.push_back({distance(objets[liste[x]].a, objets[liste[y]].a), liste[x], liste[y]});
      }
    std::sort(aretes.begin(), aretes.end(), [](const Arete& p, const Arete& q) { return p.d < q.d; });
    Unions groupes = u;
    for (const Arete& a : aretes)
      if (groupes.racine(static_cast<int>(a.a)) != groupes.racine(static_cast<int>(a.b))) {
        groupes.unir(static_cast<int>(a.a), static_cast<int>(a.b));
        e.chevelu.push_back({reseau, objets[a.a].a, objets[a.b].a});
      }
    bool complet = true;
    for (std::size_t x = 1; x < liste.size(); ++x) complet &= u.racine(static_cast<int>(liste[x])) == u.racine(static_cast<int>(liste[0]));
    if (complet) ++e.reseauxComplets;
  }
  // Bouts de piste qui ne touchent rien (les traits d'un texte de cuivre n'en sont pas)
  for (std::size_t i = 0; i < n; ++i) {
    if (objets[i].type != Objet::Type::Piste || objets[i].texte) continue;
    for (const Pt bout : {objets[i].a, objets[i].b}) {
      Objet point = objets[i];
      point.a = point.b = bout;
      point.boite = Boite{bout.x, bout.y, bout.x, bout.y}.gonflee(objets[i].largeur / 2);
      bool relie = false;
      for (int j : index.chercher(point.boite.gonflee(1))) {
        if (j == static_cast<int>(i) || !(objets[static_cast<std::size_t>(j)].couches & point.couches)) continue;
        if (ecart(point, objets[static_cast<std::size_t>(j)]) <= 1.0) { relie = true; break; }
      }
      for (std::size_t z = 0; z < ilotsZones.size() && !relie; ++z)
        if ((bit(ilotsZones[z].couche) & point.couches) && ilotsZones[z].b.contient(bout) && dedans(ilotsZones[z].poly, bout)) relie = true;
      if (!relie) e.boutsLibres.push_back(bout);
    }
  }
  return e;
}

std::string reseauDeZoneSous(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis, Pt p) {
  for (const auto& z : zonesResolues(c, k))
    if (!z.reseau.empty())
      if (const auto it = remplis.find(z.uuid); it != remplis.end() && dedans(it->second, p)) return z.reseau;
  return {};
}

// ---------- Remplissage local (9.9) ----------
struct RemplissageLocal::Donnees {
  Zone z;
  bool valide = false;
  Regles R;
  Nm isoZone = 0, portee = 0;
  Polygones base;
  Boite boiteBase;
  std::vector<Polygone> obstacles, anneaux, rayons;  // isolements des autres réseaux, anneaux et rayons thermiques
  std::vector<Boite> boitesObstacles, boitesAnneaux, boitesRayons;
  IndexSpatial indexObstacles{2 * NM_PAR_MM}, indexAnneaux{2 * NM_PAR_MM}, indexRayons{2 * NM_PAR_MM};
  struct Prioritaire {
    Polygones cuivre;
    Boite b;
    Nm requis = 0;
  };
  std::vector<Prioritaire> prioritaires;
  Connectivite k;
};

RemplissageLocal::RemplissageLocal(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis, const std::string& zone, const std::set<std::string>& mobiles)
    : d(std::make_unique<Donnees>()) {
  const std::vector<Zone> zs = zonesResolues(c, k);
  const auto it = std::find_if(zs.begin(), zs.end(), [&](const Zone& z) { return z.uuid == zone; });
  if (it == zs.end() || it->contour.size() < 3) return;
  Donnees& D = *d;
  D.z = *it;
  D.k = k;
  D.R = reglesResolues(c.circuit.regles, k);
  const Regles& R = D.R;
  Polygones interieur = gonfler({c.circuit.contour.polygone()}, -(R.bord + MARGE_REMPLISSAGE));
  if (!c.circuit.contour.decoupes.empty()) {
    Polygones trous;
    for (const auto& x : c.circuit.contour.decoupes)
      if (x.size() >= 3) trous.push_back(orienter(x));
    interieur = soustraire(interieur, gonfler(trous, R.bord + MARGE_REMPLISSAGE));
  }
  D.base = intersecter({orienter(D.z.contour)}, interieur);
  D.boiteBase = boite(D.base);
  D.isoZone = D.z.isolement > 0 ? D.z.isolement : R.isolement;
  Nm isoMax = std::max(R.isolement, R.cuivreTrou);
  for (const auto& cl : R.classes) isoMax = std::max(isoMax, cl.isolement);
  const auto pp = pastilles(c, k);
  for (const Objet& o : objetsCuivre(c, pp, k)) {
    if (!(o.couches & bit(D.z.couche))) continue;
    if ((o.type == Objet::Type::Piste && o.indice >= 0 && mobiles.count(c.circuit.pistes[static_cast<std::size_t>(o.indice)].uuid)) ||
        (o.type == Objet::Type::Via && mobiles.count(c.circuit.vias[static_cast<std::size_t>(o.indice)].uuid)))
      continue;
    if (!D.z.reseau.empty() && o.reseau == D.z.reseau) {
      if (o.type == Objet::Type::Pastille && D.z.thermique) {
        for (auto& x : gonfler({o.forme}, D.z.ecartThermique)) {
          D.indexAnneaux.inserer(static_cast<int>(D.anneaux.size()), boite(x));
          D.boitesAnneaux.push_back(boite(x));
          D.anneaux.push_back(std::move(x));
        }
        const Pastille& pa = pp[static_cast<std::size_t>(o.indice)].pastille;
        const Nm longueur = std::max(o.boite.largeur(), o.boite.hauteur()) + 2 * D.z.ecartThermique + 2 * D.z.epaisseurMin;
        const Nm l = largeurRayonEffective(D.z);
        for (Polygone x : {rectangle(pa.position, longueur, l, pa.rotation), rectangle(pa.position, l, longueur, pa.rotation)}) {
          D.indexRayons.inserer(static_cast<int>(D.rayons.size()), boite(x));
          D.boitesRayons.push_back(boite(x));
          D.rayons.push_back(std::move(x));
        }
      }
      continue;
    }
    Polygone x = contourObjet(o, std::max(D.isoZone, isolementEntre(R, D.z.reseau, o.reseau)) + MARGE_REMPLISSAGE);
    D.indexObstacles.inserer(static_cast<int>(D.obstacles.size()), boite(x));
    D.boitesObstacles.push_back(boite(x));
    D.obstacles.push_back(std::move(x));
  }
  for (const auto& p : pp)
    if (p.pastille.type == TypePastille::NonMetallisee) {
      Polygone x = cercle(p.pastille.position, p.pastille.percage / 2 + std::max(D.isoZone, R.cuivreTrou) + MARGE_REMPLISSAGE);
      D.indexObstacles.inserer(static_cast<int>(D.obstacles.size()), boite(x));
      D.boitesObstacles.push_back(boite(x));
      D.obstacles.push_back(std::move(x));
    }
  for (const auto& y : zs) {
    if (y.uuid == D.z.uuid || y.couche != D.z.couche || y.reseau == D.z.reseau || y.priorite <= D.z.priorite) continue;
    const auto f = remplis.find(y.uuid);
    if (f == remplis.end() || f->second.empty()) continue;
    const Nm requis = std::max({D.isoZone, y.isolement > 0 ? y.isolement : R.isolement, isolementEntre(R, D.z.reseau, y.reseau)});
    D.prioritaires.push_back({f->second, boite(f->second), requis});
  }
  D.portee = std::max(isoMax, D.isoZone) + MARGE_REMPLISSAGE + 2 * std::max<Nm>(D.z.epaisseurMin, 0) + D.z.ecartThermique + 50'000;
  D.valide = true;
}

RemplissageLocal::~RemplissageLocal() = default;
bool RemplissageLocal::valide() const { return d && d->valide; }
Couche RemplissageLocal::couche() const { return d->z.couche; }
const std::string& RemplissageLocal::zone() const { return d->z.uuid; }
const std::string& RemplissageLocal::reseau() const { return d->z.reseau; }
Nm RemplissageLocal::portee() const { return d->portee; }

Polygones RemplissageLocal::dans(const Boite& fenetre, const std::vector<Piste>& pistes, const std::vector<Via>& vias) const {
  if (!valide()) return {};
  const Donnees& D = *d;
  // Le calcul se fait sur la fenêtre élargie de ce qui peut l'influencer, puis il est rogné à la fenêtre
  const Boite large = fenetre.gonflee(D.portee);
  const Polygone cadreLarge{{large.x0, large.y0}, {large.x1, large.y0}, {large.x1, large.y1}, {large.x0, large.y1}};
  const Polygone cadre{{fenetre.x0, fenetre.y0}, {fenetre.x1, fenetre.y0}, {fenetre.x1, fenetre.y1}, {fenetre.x0, fenetre.y1}};
  const Polygones base = intersecter(D.base, {cadreLarge});
  if (base.empty()) return {};
  Polygones autres, anneaux, rayons;
  for (int i : D.indexObstacles.chercher(large))
    if (D.boitesObstacles[static_cast<std::size_t>(i)].touche(large)) autres.push_back(D.obstacles[static_cast<std::size_t>(i)]);
  for (int i : D.indexAnneaux.chercher(large))
    if (D.boitesAnneaux[static_cast<std::size_t>(i)].touche(large)) anneaux.push_back(D.anneaux[static_cast<std::size_t>(i)]);
  for (int i : D.indexRayons.chercher(large))
    if (D.boitesRayons[static_cast<std::size_t>(i)].touche(large)) rayons.push_back(D.rayons[static_cast<std::size_t>(i)]);
  // Les objets mobiles, à leur place courante : un autre réseau s'écarte, le même réseau se fond dans la zone
  for (const auto& p : pistes) {
    if (p.couche != D.z.couche) continue;
    const std::string n = D.k.canonique(p.reseau);
    if (!D.z.reseau.empty() && n == D.z.reseau) continue;
    autres.push_back(oblong(p.a, p.b, p.largeur + 2 * (std::max(D.isoZone, isolementEntre(D.R, D.z.reseau, n)) + MARGE_REMPLISSAGE)));
  }
  for (const auto& v : vias) {
    const std::string n = D.k.canonique(v.reseau);
    if (!D.z.reseau.empty() && n == D.z.reseau) continue;
    autres.push_back(cercle(v.p, v.diametre / 2 + std::max(D.isoZone, isolementEntre(D.R, D.z.reseau, n)) + MARGE_REMPLISSAGE));
  }
  for (const auto& y : D.prioritaires)
    if (y.b.touche(large))
      for (auto& x : gonfler(intersecter(y.cuivre, {cadreLarge}), y.requis + MARGE_REMPLISSAGE)) autres.push_back(std::move(x));
  Polygones obstacles = autres;
  obstacles.insert(obstacles.end(), anneaux.begin(), anneaux.end());
  Polygones rempli = soustraire(base, obstacles);
  if (!rayons.empty()) {
    Polygones r2 = soustraire(intersecter(rayons, base), autres);
    rempli.insert(rempli.end(), r2.begin(), r2.end());
    rempli = unir(rempli);
  }
  if (D.z.epaisseurMin > 0) rempli = gonfler(gonfler(rempli, -D.z.epaisseurMin / 2), D.z.epaisseurMin / 2);
  return intersecter(rempli, {cadre});
}

std::vector<Violation> verifierDRC(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis) {
  // L'état du routage (chevelu, courts-circuits, bouts libres) se calcule sur un autre fil pendant les contrôles
  // d'isolement (9.2) : il ne lit que des données constantes
  auto etatFutur = std::async(std::launch::async, [&c, &k, &remplis] { return etatRoutage(c, k, remplis); });
  std::vector<Violation> v;
  auto ajouter = [&](Gravite g, const char* code, std::string m, std::string conseil, Pt p, std::vector<std::string> objets = {}) {
    v.push_back({g, code, std::move(m), std::move(conseil), {}, p, std::move(objets)});
  };
  const Regles R = reglesResolues(c.circuit.regles, k);
  const auto pp = pastilles(c, k);
  const auto objets = objetsCuivre(c, pp, k);
  const std::vector<Zone> zs = zonesResolues(c, k);
  Nm isoMax = R.isolement;
  for (const auto& cl : R.classes) isoMax = std::max(isoMax, cl.isolement);
  IndexSpatial index(2 * NM_PAR_MM);
  for (std::size_t i = 0; i < objets.size(); ++i) index.inserer(static_cast<int>(i), objets[i].boite);
  // Liaisons de réseaux (2.33, ERC-15) : l'empreinte d'un symbole « liaison de réseaux » est le seul endroit où ses
  // deux réseaux se touchent, voulu ; le cuivre qui les y rejoint n'est pas un court-circuit, mais il doit exister
  struct PontReseaux {
    std::set<std::string> reseaux;
    Boite zone;
    std::string reference, composant;
  };
  std::map<int, PontReseaux> liaisons;  // indice de l'empreinte → liaison
  for (const auto& p : pp) {
    const ComposantElec* x = c.composant(p.composant);
    const DefSymbole* d = x ? c.symbole(x->symbole) : nullptr;
    if (!d || !d->liaisonReseaux) continue;
    PontReseaux& l = liaisons[p.empreinte];
    if (!p.reseau.empty()) l.reseaux.insert(p.reseau);
    for (const Pt& q : p.forme) l.zone.etendre(q);
    l.reference = p.reference;
    l.composant = p.composant;
  }
  auto dansLiaison = [&](const Objet& a, const Objet& b, Pt ou) {
    for (const Objet* o : {&a, &b}) {
      if (o->type != Objet::Type::Pastille) continue;
      const auto it = liaisons.find(o->empreinte);
      if (it != liaisons.end() && it->second.reseaux.count(a.reseau) && it->second.reseaux.count(b.reseau) && it->second.zone.gonflee(300'000).contient(ou)) return true;
    }
    return false;
  };
  // Isolements
  for (std::size_t i = 0; i < objets.size(); ++i) {
    const Objet& a = objets[i];
    for (int jj : index.chercher(a.boite.gonflee(isoMax))) {
      const std::size_t j = static_cast<std::size_t>(jj);
      if (j <= i) continue;
      const Objet& b = objets[j];
      if (!(a.couches & b.couches)) continue;
      // L'isolement ne se contrôle qu'entre réseaux nommés distincts (R6) : le cuivre sans réseau (motif, logo,
      // antenne, texte de cuivre) forme un morceau à part ; il est signalé à part (DRC-56), et un court-circuit
      // qu'il ferait entre deux réseaux reste vu par la connectivité du cuivre.
      if (a.reseau.empty() || b.reseau.empty() || a.reseau == b.reseau) continue;
      // Deux pastilles d'une même empreinte se contrôlent aussi dès que leurs réseaux diffèrent (EL-05) : une
      // empreinte trop serrée pour le procédé choisi est signalée.
      const bool memeEmpreinte = a.type == Objet::Type::Pastille && b.type == Objet::Type::Pastille && a.empreinte == b.empreinte;
      const Nm requis = isolementEntre(R, a.reseau, b.reseau);
      if (!a.boite.gonflee(requis).touche(b.boite)) continue;
      const double d = ecart(a, b);
      if (d + 0.5 < static_cast<double>(requis)) {
        Boite x = a.boite;
        x.etendre(b.boite);
        // Le lieu du contact : le recouvrement des deux boîtes, ou le milieu de l'écart
        const Boite inter{std::max(a.boite.x0, b.boite.x0), std::max(a.boite.y0, b.boite.y0), std::min(a.boite.x1, b.boite.x1), std::min(a.boite.y1, b.boite.y1)};
        if (dansLiaison(a, b, inter.vide() ? Pt{(std::max(a.boite.x0, b.boite.x0) + std::min(a.boite.x1, b.boite.x1)) / 2, (std::max(a.boite.y0, b.boite.y0) + std::min(a.boite.y1, b.boite.y1)) / 2}
                                           : inter.centre()))
          continue;
        // Les deux objets en cause (5.13) : une exclusion les vise, une ligne du rapport y mène
        auto ident = [&](const Objet& o) -> std::string {
          switch (o.type) {
            case Objet::Type::Pastille: return pp[static_cast<std::size_t>(o.indice)].composant;
            case Objet::Type::Piste: return c.circuit.pistes[static_cast<std::size_t>(o.indice)].uuid;
            case Objet::Type::Via: return c.circuit.vias[static_cast<std::size_t>(o.indice)].uuid;
          }
          return {};
        };
        std::vector<std::string> enCause;
        for (const Objet* o : {&a, &b})
          if (const std::string id = o->texte ? std::string() : ident(*o); !id.empty() && std::find(enCause.begin(), enCause.end(), id) == enCause.end()) enCause.push_back(id);
        if (d <= 0)
          ajouter(Gravite::Erreur, "DRC-21", "Court-circuit : " + decrire(a, pp) + " touche " + decrire(b, pp), "Les deux cuivres se touchent : déplace la piste ou le composant.",
                  x.centre(), enCause);
        else if (memeEmpreinte)
          ajouter(Gravite::Erreur, "DRC-01", "Isolement de " + mmTexte(d) + " entre " + decrire(a, pp) + " et " + decrire(b, pp) + " de la même empreinte (minimum " +
                                                 mmTexte(static_cast<double>(requis)) + ")",
                  "L’empreinte est trop serrée pour ces règles : choisis une variante plus large ou un procédé plus fin.", x.centre(), enCause);
        else
          ajouter(Gravite::Erreur, "DRC-01", "Isolement de " + mmTexte(d) + " entre " + decrire(a, pp) + " et " + decrire(b, pp) + " (minimum " + mmTexte(static_cast<double>(requis)) + ")",
                  "Écarte les deux cuivres, ou affine la piste.", x.centre(), enCause);
      }
    }
  }
  // Chaque liaison de réseaux a son pont de cuivre : une piste qui touche ses deux pastilles (DRC-43)
  for (const auto& [e, l] : liaisons) {
    std::vector<Boite> pads;
    for (const auto& p : pp)
      if (p.empreinte == e) {
        Boite b;
        for (const Pt& q : p.forme) b.etendre(q);
        pads.push_back(b);
      }
    bool pont = false;
    for (const auto& t : c.circuit.pistes) {
      Boite s;
      s.etendre(t.a);
      s.etendre(t.b);
      s = s.gonflee(t.largeur / 2);
      int touches = 0;
      for (const Boite& b : pads) touches += s.touche(b);
      pont |= touches >= 2;
    }
    if (pads.size() >= 2 && !pont)
      ajouter(Gravite::Avertissement, "DRC-43", "Liaison de réseaux " + l.reference + " sans pont de cuivre : ses deux réseaux ne se rejoignent pas sur la carte",
              "Trace une courte piste d’une pastille à l’autre de la liaison : c’est le seul endroit où les deux réseaux doivent se toucher.", l.zone.centre(), {l.composant});
  }
  // Largeurs, vias, perçages
  for (const auto& p : c.circuit.pistes)
    if (p.largeur < R.largeurMin) ajouter(Gravite::Erreur, "DRC-02", "Piste de " + mmTexte(static_cast<double>(p.largeur)) + " (minimum " + mmTexte(static_cast<double>(R.largeurMin)) + ")", "Élargis la piste.", {(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2}, {p.uuid});
  for (const auto& via : c.circuit.vias) {
    if (via.percage < R.percageMin) ajouter(Gravite::Erreur, "DRC-03", "Via percée à " + mmTexte(static_cast<double>(via.percage)) + " (minimum " + mmTexte(static_cast<double>(R.percageMin)) + ")", "Agrandis le perçage de la via.", via.p, {via.uuid});
    if ((via.diametre - via.percage) / 2 < R.anneauMin) ajouter(Gravite::Erreur, "DRC-04", "Couronne de via trop fine : " + mmTexte(static_cast<double>((via.diametre - via.percage) / 2)), "Augmente le diamètre de la via ou réduis son perçage.", via.p, {via.uuid});
  }
  for (const auto& p : pp) {
    if (p.pastille.percage > 0 && p.pastille.percage < R.percageMin)
      ajouter(Gravite::Erreur, "DRC-05", "Pastille " + p.reference + "." + p.numero + " percée à " + mmTexte(static_cast<double>(p.pastille.percage)), "Choisis une empreinte au perçage plus large.", p.pastille.position);
    // Couronne des pastilles traversantes métallisées (comme celle des vias)
    if (p.pastille.type == TypePastille::Traversante && p.pastille.percage > 0) {
      const Nm couronne = (std::min(p.pastille.largeur, p.pastille.hauteur) - p.pastille.percage) / 2;
      if (couronne < R.anneauMin)
        ajouter(Gravite::Erreur, "DRC-11", "Couronne de la pastille " + p.reference + "." + p.numero + " trop fine : " + mmTexte(static_cast<double>(couronne)) + " (minimum " +
                                              mmTexte(static_cast<double>(R.anneauMin)) + ")",
                "Agrandis la pastille ou réduis son perçage : le foret pourrait la couper.", p.pastille.position);
    }
  }
  // Écart entre perçages (vias, pastilles traversantes et non métallisées), bord à bord
  {
    struct Trou { Pt p; Nm r; std::string nom; };
    std::vector<Trou> trous;  // chaque perçage une fois : deux trous superposés sont deux objets (EL-28)
    for (const auto& via : c.circuit.vias) trous.push_back({via.p, via.percage / 2, "via"});
    for (const auto& p : pp)
      if (p.pastille.percage > 0) trous.push_back({p.pastille.position, p.pastille.percage / 2, "pastille " + p.reference + "." + p.numero});
    // Cuivre trop près d'un perçage non métallisé : la perceuse le couperait (EL-03)
    for (const auto& p : pp) {
      if (p.pastille.type != TypePastille::NonMetallisee || p.pastille.percage <= 0) continue;
      Objet trou;
      trou.type = Objet::Type::Via;
      trou.a = trou.b = p.pastille.position;
      trou.largeur = p.pastille.percage;
      trou.boite.etendre(trou.a);
      trou.boite = trou.boite.gonflee(p.pastille.percage / 2);
      for (int jj : index.chercher(trou.boite.gonflee(R.cuivreTrou))) {
        const Objet& o = objets[static_cast<std::size_t>(jj)];
        if (o.type == Objet::Type::Pastille && o.empreinte == p.empreinte) continue;  // pastilles de la même pièce (anneau)
        const double d = ecart(trou, o);
        if (d + 0.5 < static_cast<double>(R.cuivreTrou))
          ajouter(Gravite::Erreur, "DRC-15", decrire(o, pp) + " à " + mmTexte(d) + " du perçage non métallisé " + p.reference + (d <= 0 ? " : la perceuse la coupera" : "") + " (minimum " +
                                                 mmTexte(static_cast<double>(R.cuivreTrou)) + ")",
                  "Contourne le trou : un perçage non métallisé n’a pas de cuivre et traverse toutes les couches.", p.pastille.position, {p.composant});
      }
    }
    IndexSpatial it(2 * NM_PAR_MM);
    for (std::size_t i = 0; i < trous.size(); ++i) it.inserer(static_cast<int>(i), Boite{trous[i].p.x - trous[i].r, trous[i].p.y - trous[i].r, trous[i].p.x + trous[i].r, trous[i].p.y + trous[i].r});
    for (std::size_t i = 0; i < trous.size(); ++i) {
      const Trou& a = trous[i];
      for (int jj : it.chercher(Boite{a.p.x - a.r, a.p.y - a.r, a.p.x + a.r, a.p.y + a.r}.gonflee(R.ecartTrous + 4 * NM_PAR_MM))) {
        const std::size_t j = static_cast<std::size_t>(jj);
        if (j <= i) continue;
        const Trou& b = trous[j];
        const double ecart = distance(a.p, b.p) - static_cast<double>(a.r + b.r);
        if (ecart < 0)
          ajouter(Gravite::Erreur, "DRC-14", "Perçages qui se recoupent : " + a.nom + " et " + b.nom, "Écarte-les : deux trous qui se chevauchent arrachent la matière entre eux.",
                  Pt{(a.p.x + b.p.x) / 2, (a.p.y + b.p.y) / 2});
        else if (ecart + 0.5 < static_cast<double>(R.ecartTrous))
          ajouter(Gravite::Erreur, "DRC-12", "Perçages à " + mmTexte(ecart) + " l’un de l’autre : " + a.nom + " et " + b.nom + " (minimum " +
                                                 mmTexte(static_cast<double>(R.ecartTrous)) + ")",
                  "Écarte-les : la paroi entre deux trous trop proches casse au perçage.", Pt{(a.p.x + b.p.x) / 2, (a.p.y + b.p.y) / 2});
      }
    }
  }
  // Courant : chaque piste d'une classe dont le courant est renseigné face à la largeur IPC-2221
  {
    const double cuivre = c.circuit.empilage.cuivre;
    for (const auto& cl : R.classes) {
      if (cl.courant <= 0) continue;
      const Nm externe = largeurIPC2221(cl.courant, cl.echauffement, cuivre, true);
      if (cl.largeur < externe)
        ajouter(Gravite::Avertissement, "DRC-32", "Classe « " + cl.nom + " » : largeur " + mmTexte(static_cast<double>(cl.largeur)) + " sous la recommandation de " +
                                                    mmTexte(static_cast<double>(externe)) + " (" + hypothesesIPC2221(cl.courant, cl.echauffement, cuivre, true) + ")",
                "Élargis la classe, ou vérifie le courant et l’échauffement admis.", {});
    }
    // Largeur et vias de la classe, même sans courant renseigné (EL-39) : la classe est la valeur de travail
    for (const auto& p : c.circuit.pistes) {
      const ClasseReseau* cl = classeDe(R, k.canonique(p.reseau));
      if (cl && p.largeur < cl->largeur)
        ajouter(Gravite::Avertissement, "DRC-33", "Piste de " + mmTexte(static_cast<double>(p.largeur)) + " sur « " + p.reseau + " », plus étroite que sa classe « " + cl->nom + " » (" +
                                                    mmTexte(static_cast<double>(cl->largeur)) + ")",
                "Élargis la piste à la largeur de la classe, ou change la classe.", {(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2}, {p.uuid});
    }
    for (const auto& v : c.circuit.vias) {
      const ClasseReseau* cl = classeDe(R, k.canonique(v.reseau));
      if (cl && (v.diametre < cl->viaDiametre || v.percage < cl->viaPercage))
        ajouter(Gravite::Avertissement, "DRC-34", "Via de " + mmTexte(static_cast<double>(v.diametre)) + " / " + mmTexte(static_cast<double>(v.percage)) + " sur « " + v.reseau +
                                                    " », plus petite que celle de sa classe « " + cl->nom + " » (" + mmTexte(static_cast<double>(cl->viaDiametre)) + " / " +
                                                    mmTexte(static_cast<double>(cl->viaPercage)) + ")",
                "Reprends les dimensions de via de la classe.", v.p, {v.uuid});
    }
    // Affectations de classes : réseau ou motif qui ne prend rien, motif masqué par un motif placé avant lui (R5),
    // classe supprimée (R10), un même réseau visé par deux classes (R11)
    for (const auto& m : diagnosticAffectations(c.circuit.regles, k)) {
      std::string texte = m;
      if (!texte.empty()) texte[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(texte[0])));
      ajouter(Gravite::Avertissement, "DRC-35", texte, "Corrige la liste des affectations dans les classes du circuit : l’ordre des lignes est celui dans lequel les motifs s’essaient.", {});
    }
    for (const auto& p : c.circuit.pistes) {
      const ClasseReseau* cl = classeDe(R, k.canonique(p.reseau));
      if (!cl || cl->courant <= 0) continue;
      const bool externe = p.couche == Couche::CuivreDessus || p.couche == Couche::CuivreDessous;
      const Nm requis = largeurIPC2221(cl->courant, cl->echauffement, cuivre, externe);
      if (p.largeur < requis)
        ajouter(Gravite::Erreur, "DRC-31", "Piste de " + mmTexte(static_cast<double>(p.largeur)) + " sur « " + p.reseau + " » : " + mmTexte(static_cast<double>(requis)) +
                                             " recommandés pour " + hypothesesIPC2221(cl->courant, cl->echauffement, cuivre, externe),
                "Élargis la piste (ou double-la par une zone) : trop étroite, elle chauffe.", {(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2}, {p.uuid});
    }
  }
  // Bord de carte
  const Polygone carte = c.circuit.contour.polygone();
  const auto aretesCarte = c.circuit.contour.aretes();
  auto dansCarte = [&](Pt p) { return c.circuit.contour.contient(p); };
  auto distanceAuBord = [&](const Objet& o) {
    double d = 1e18;
    for (const auto& [a, b] : aretesCarte) {
      switch (o.type) {
        case Objet::Type::Pastille: d = std::min(d, distancePolygoneSegment(o.forme, a, b)); break;
        case Objet::Type::Piste: d = std::min(d, distanceSegments(a, b, o.a, o.b) - static_cast<double>(o.largeur) / 2); break;
        case Objet::Type::Via: d = std::min(d, distancePointSegment(o.a, a, b) - static_cast<double>(o.largeur) / 2); break;
      }
    }
    return std::max(0.0, d);
  };
  // Matière de la carte : contour moins les découpes ; tout cuivre qui en sort, même en partie, est en DRC-06 (R4)
  Polygones matiere{carte};
  {
    Polygones trous;
    for (const auto& d : c.circuit.contour.decoupes)
      if (d.size() >= 3) trous.push_back(orienter(d));
    if (!trous.empty()) matiere = soustraire(matiere, trous);
  }
  std::set<int> empreintesHors;
  for (const Objet& o : objets) {
    // Hors de la carte, ou dans une découpe : le centre d'abord, puis la forme entière quand elle touche un bord
    bool horsMatiere = !dansCarte(o.a) || !dansCarte(o.b);
    const double d = horsMatiere ? 0.0 : distanceAuBord(o);
    if (!horsMatiere && d <= 0.5) {
      const Polygones dehors = soustraire({contourObjet(o, 0)}, matiere);
      horsMatiere = !dehors.empty() && aireMm2(dehors) > 1e-6;
    }
    if (horsMatiere) {
      if (o.type == Objet::Type::Pastille) {
        // Une seule ligne par empreinte, la DRC-09 dit en plus que le composant dépasse
        if (empreintesHors.insert(o.empreinte).second)
          ajouter(Gravite::Erreur, "DRC-06", decrire(o, pp) + " : du cuivre sort de la carte ou entre dans une découpe", "Ramène le composant dans la matière de la carte : la fraise couperait la pastille.",
                  o.a, {pp[static_cast<std::size_t>(o.indice)].composant});
      } else
        ajouter(Gravite::Erreur, "DRC-06", decrire(o, pp) + (dedans(carte, o.a) && dedans(carte, o.b) ? " coupé(e) par une découpe de la carte" : " hors de la carte, ou à cheval sur son bord"),
                "Ramène-la dans la matière de la carte, hors du contour et des découpes.", o.a);
      continue;
    }
    if (d + 0.5 < static_cast<double>(R.bord)) ajouter(Gravite::Erreur, "DRC-07", decrire(o, pp) + " à " + mmTexte(d) + " du bord de la carte (minimum " + mmTexte(static_cast<double>(R.bord)) + ")", "Éloigne le cuivre du bord : la découpe pourrait l’entamer.", o.a);
  }
  // Encombrements calculés une fois et indexés : le chevauchement (DRC-10) ne regarde que les voisins (9.3)
  const std::size_t nEmp = c.circuit.empreintes.size();
  std::vector<Polygone> encs(nEmp);
  std::vector<Boite> boitesEnc(nEmp);
  IndexSpatial indexEnc(5 * NM_PAR_MM);
  for (std::size_t i = 0; i < nEmp; ++i)
    if (const DefEmpreinte* d = c.empreinte(c.circuit.empreintes[i].empreinte)) {
      encs[i] = encombrementSurCarte(c.circuit.empreintes[i], *d);
      boitesEnc[i] = boite(encs[i]);
      indexEnc.inserer(static_cast<int>(i), boitesEnc[i]);
    }
  for (std::size_t i = 0; i < nEmp; ++i) {
    const EmpreintePlacee& e = c.circuit.empreintes[i];
    const ComposantElec* x = c.composant(e.composant);
    const std::string ref = x ? x->reference : std::string("?");
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!d) { ajouter(Gravite::Erreur, "DRC-08", ref + " : empreinte inconnue « " + e.empreinte + " »", "Choisis une empreinte du catalogue dans le schéma.", e.position, {e.composant}); continue; }
    const Polygone& enc = encs[i];
    bool dedansCarte = true;
    for (const Pt& p : enc) dedansCarte &= dansCarte(p);
    if (empreintesHors.count(static_cast<int>(i)) || !dedansCarte)
      ajouter(empreintesHors.count(static_cast<int>(i)) ? Gravite::Erreur : Gravite::Avertissement, "DRC-09", ref + " dépasse de la carte", "Place-le à l’intérieur du contour (un connecteur peut dépasser exprès du bord).", e.position, {e.composant});
    for (const int jj : indexEnc.chercher(boitesEnc[i])) {
      const std::size_t j = static_cast<std::size_t>(jj);
      if (j <= i) continue;
      const EmpreintePlacee& f = c.circuit.empreintes[j];
      if (f.dessous != e.dessous || encs[j].empty()) continue;
      const Polygone& enc2 = encs[j];
      if (!boitesEnc[i].touche(boitesEnc[j])) continue;
      if (!intersecter({enc}, {enc2}).empty() && aireMm2(intersecter({enc}, {enc2})) > 1e-4) {
        const ComposantElec* y = c.composant(f.composant);
        ajouter(Gravite::Avertissement, "DRC-10", "Encombrements de " + ref + " et " + (y ? y->reference : std::string("?")) + " qui se chevauchent", "Écarte les composants : l’assemblage risque de les heurter.", e.position, {e.composant, f.composant});
      }
    }
  }
  // Sérigraphie posée sur une pastille (la sienne ou celle d'un voisin) : le fabricant la rogne, ou l'imprime
  // sur la soudure
  {
    IndexSpatial indexPastilles(2 * NM_PAR_MM);
    for (std::size_t q = 0; q < pp.size(); ++q) indexPastilles.inserer(static_cast<int>(q), boite(pp[q].forme));
    std::set<std::pair<std::size_t, std::string>> signalees;  // (empreinte, pastille « REF.n »)
    for (std::size_t i = 0; i < c.circuit.empreintes.size(); ++i) {
      const EmpreintePlacee& e = c.circuit.empreintes[i];
      const DefEmpreinte* d = c.empreinte(e.empreinte);
      if (!d) continue;
      const ComposantElec* x = c.composant(e.composant);
      const Couche cuivre = e.dessous ? Couche::CuivreDessous : Couche::CuivreDessus;
      for (const auto& t : d->traits) {
        if (t.couche != Couche::SerigraphieDessus) continue;
        const double demi = static_cast<double>(t.epaisseur) / 2;
        std::vector<std::pair<Pt, Pt>> segments;
        if (t.rayon > 0) {
          for (int k2 = 0; k2 < 32; ++k2) {
            const double a0 = 2 * 3.141592653589793 * k2 / 32, a1 = 2 * 3.141592653589793 * (k2 + 1) / 32;
            const double r = static_cast<double>(t.rayon);
            const Pt p0{t.a.x + static_cast<Nm>(std::lround(r * std::cos(a0))), t.a.y + static_cast<Nm>(std::lround(r * std::sin(a0)))};
            const Pt p1{t.a.x + static_cast<Nm>(std::lround(r * std::cos(a1))), t.a.y + static_cast<Nm>(std::lround(r * std::sin(a1)))};
            segments.push_back({versCarte(e, p0), versCarte(e, p1)});
          }
        } else {
          segments.push_back({versCarte(e, t.a), versCarte(e, t.b)});
        }
        for (const auto& [a, b] : segments) {
          Boite bs;
          bs.etendre(a);
          bs.etendre(b);
          for (int qq : indexPastilles.chercher(bs.gonflee(static_cast<Nm>(demi) + 1))) {
            const PastillePlacee& p = pp[static_cast<std::size_t>(qq)];
            if (!(p.couches & bit(cuivre))) continue;
            const std::string cle = p.reference + "." + p.numero;
            if (signalees.count({i, cle})) continue;
            if (distancePolygoneSegment(p.forme, a, b) < demi) {
              signalees.insert({i, cle});
              const std::string ref = x ? x->reference : std::string("?");
              ajouter(Gravite::Avertissement, "DRC-13", "Sérigraphie de " + ref + " sur la pastille " + cle,
                      "Le fabricant la rogne ou l’imprime sur la soudure : décale le dessin ou le composant.", p.pastille.position, {e.composant});
            }
          }
        }
      }
    }
  }
  // Textes de sérigraphie (repères, valeurs, textes de carte) posés sur une pastille de leur face (EL-06)
  {
    IndexSpatial indexPastilles(2 * NM_PAR_MM);
    for (std::size_t q = 0; q < pp.size(); ++q) indexPastilles.inserer(static_cast<int>(q), boite(pp[q].forme));
    std::set<std::pair<std::string, std::string>> signales;
    for (const auto& t : textesSerigraphie(c)) {
      const Couche cuivre = t.couche == Couche::SerigraphieDessous ? Couche::CuivreDessous : Couche::CuivreDessus;
      Boite bs;
      bs.etendre(t.a);
      bs.etendre(t.b);
      const double demi = static_cast<double>(t.epaisseur) / 2;
      for (int qq : indexPastilles.chercher(bs.gonflee(static_cast<Nm>(demi) + 1))) {
        const PastillePlacee& p = pp[static_cast<std::size_t>(qq)];
        if (!(p.couches & bit(cuivre)) || distancePolygoneSegment(p.forme, t.a, t.b) >= demi) continue;
        const std::string cle = p.reference + "." + p.numero;
        if (!signales.insert({t.texte, cle}).second) continue;
        ajouter(Gravite::Avertissement, "DRC-13", "Texte « " + t.texte + " » de la sérigraphie sur la pastille " + cle,
                "Déplace le texte (ou le composant) : le fabricant rogne la sérigraphie posée sur une pastille.", p.pastille.position, {t.composant});
      }
    }
  }
  // Routage : liaisons manquantes, courts-circuits, bouts de piste libres, zones vides
  const EtatRoutage etat = etatFutur.get();
  std::map<std::string, std::vector<const Liaison*>> parReseau;
  for (const auto& l : etat.chevelu) parReseau[l.reseau].push_back(&l);
  for (const auto& [reseau, liste] : parReseau)
    ajouter(Gravite::Erreur, "DRC-20", "Réseau « " + reseau + " » : " + std::to_string(liste.size()) + " liaison(s) non routée(s)", "Trace les pistes qui manquent (le chevelu montre les liaisons à faire).",
            {(liste.front()->a.x + liste.front()->b.x) / 2, (liste.front()->a.y + liste.front()->b.y) / 2});
  for (const auto& cc : etat.courtsCircuits)
    ajouter(Gravite::Erreur, "DRC-21", "Court-circuit entre « " + cc.a + " » et « " + cc.b + " »", "Une piste, une via ou une zone relie deux réseaux : supprime le cuivre en trop.", cc.position);
  for (const Pt& p : etat.boutsLibres) ajouter(Gravite::Avertissement, "DRC-22", "Bout de piste qui ne mène nulle part", "Prolonge la piste jusqu’à une pastille, ou supprime-la.", p);
  // Vias sans réseau, ou qui ne touchent rien de leur réseau (ni piste, ni pastille, ni zone remplie) : EL-12
  // Cuivre des zones indexé une fois, pour les tests d'inclusion qui suivent (9.2)
  std::map<std::string, std::unique_ptr<CuivreIndexe>> cuivresIndexes;
  auto cuivreDeZone = [&](const std::string& uuid) -> const CuivreIndexe* {
    auto& x = cuivresIndexes[uuid];
    if (!x)
      if (const auto it = remplis.find(uuid); it != remplis.end()) x = std::make_unique<CuivreIndexe>(it->second);
    return x.get();
  };
  for (const auto& v : c.circuit.vias) {
    if (v.reseau.empty()) {
      ajouter(Gravite::Avertissement, "DRC-25", "Via sans réseau", "Donne-lui un réseau dans son panneau (une via posée dans une zone prend le réseau de la zone).", v.p);
      continue;
    }
    bool relie = false;
    for (const auto& t : c.circuit.pistes) relie = relie || (t.reseau == v.reseau && distancePointSegment(v.p, t.a, t.b) <= static_cast<double>(v.diametre) / 2);
    for (const auto& p : pp) relie = relie || (p.reseau == v.reseau && dedans(p.forme, v.p));
    for (const auto& z : zs)
      if (!relie && z.reseau == v.reseau)
        if (const CuivreIndexe* x = cuivreDeZone(z.uuid)) relie = x->contient(v.p);
    if (!relie) ajouter(Gravite::Avertissement, "DRC-25", "Via du réseau « " + v.reseau + " » reliée à rien", "Relie-la par une piste ou une zone de son réseau, ou supprime-la.", v.p);
  }
  for (const auto& z : zs) {
    const auto it = remplis.find(z.uuid);
    if (it == remplis.end() || it->second.empty()) ajouter(Gravite::Avertissement, "DRC-23", "Zone « " + (z.reseau.empty() ? std::string("sans réseau") : z.reseau) + " » vide après remplissage", "Agrandis la zone, ou vérifie son réseau : il doit toucher au moins une pastille.", z.contour.empty() ? Pt{} : z.contour.front());
  }
  // Cuivre des zones, vérifié tel qu'il est rempli (contrôle indépendant du remplissage) : isolement aux
  // objets des autres réseaux, aux perçages non métallisés, aux zones voisines et au bord de la carte.
  for (const auto& z : zs) {
    const auto it = remplis.find(z.uuid);
    if (it == remplis.end() || it->second.empty()) continue;
    const Polygones& cuivre = it->second;
    const Nm isoZone = z.isolement > 0 ? z.isolement : R.isolement;
    const std::string nomZone = "Zone « " + (z.reseau.empty() ? std::string("sans réseau") : z.reseau) + " »";
    const Boite bz = boite(cuivre);
    const CuivreIndexe indexe(cuivre);
    // Distance du cuivre de la zone à un objet (0 si l'objet touche le cuivre)
    auto distanceAuCuivre = [&](const Objet& o, Nm requis) { return indexe.distance(o, requis); };
    for (const Objet& o : objets) {
      if (!(o.couches & bit(z.couche)) || (!z.reseau.empty() && o.reseau == z.reseau)) continue;
      const Nm requis = std::max(isoZone, isolementEntre(R, z.reseau, o.reseau));
      if (!bz.gonflee(requis).touche(o.boite)) continue;
      const double d = distanceAuCuivre(o, requis);
      // Contact : déjà signalé comme court-circuit (DRC-21) par la connectivité
      if (d > 0 && d + 0.5 < static_cast<double>(requis))
        ajouter(Gravite::Erreur, "DRC-24", nomZone + " à " + mmTexte(d) + " de la " + decrire(o, pp) + " (minimum " + mmTexte(static_cast<double>(requis)) + ")",
                "Refais le remplissage des zones (il suit les règles) ; s’il persiste, écarte l’objet ou augmente l’isolement de la zone.", o.a, {z.uuid});
    }
    // Perçages non métallisés : la même distance que le remplissage, le plus grand de l'isolement de la zone et de
    // la distance cuivre-trou (R7)
    const Nm requisTrou = std::max(isoZone, R.cuivreTrou);
    for (const auto& p : pp) {
      if (p.pastille.type != TypePastille::NonMetallisee) continue;
      Objet trou;
      trou.type = Objet::Type::Via;
      trou.a = trou.b = p.pastille.position;
      trou.largeur = p.pastille.percage;
      trou.boite.etendre(trou.a);
      trou.boite = trou.boite.gonflee(p.pastille.percage / 2);
      if (!bz.gonflee(requisTrou).touche(trou.boite)) continue;
      const double d = distanceAuCuivre(trou, requisTrou);
      if (d + 0.5 < static_cast<double>(requisTrou))
        ajouter(Gravite::Erreur, "DRC-24", nomZone + " à " + mmTexte(d) + " du perçage non métallisé " + p.reference + "." + p.numero + " (minimum " + mmTexte(static_cast<double>(requisTrou)) + ")",
                "Refais le remplissage des zones.", p.pastille.position, {z.uuid});
    }
    // Zone contre zone d'un autre réseau : le plus grand des deux isolements de zone et de l'isolement entre leurs
    // classes, le même quel que soit le sens (R1) ; chaque paire n'est donc contrôlée qu'une fois
    for (const auto& y : zs) {
      if (&y == &z || y.couche != z.couche || y.reseau == z.reseau || y.uuid < z.uuid) continue;
      const auto jt = remplis.find(y.uuid);
      if (jt == remplis.end() || jt->second.empty()) continue;
      const Nm requis = std::max({isoZone, y.isolement > 0 ? y.isolement : R.isolement, isolementEntre(R, z.reseau, y.reseau)});
      if (!bz.gonflee(requis).touche(boite(jt->second))) continue;
      const Polygones commun = intersecter(gonfler(cuivre, requis - 1'000), jt->second);
      if (!commun.empty() && aireMm2(commun) > 1e-6)
        ajouter(Gravite::Erreur, "DRC-24", nomZone + " trop près de la zone « " + (y.reseau.empty() ? std::string("sans réseau") : y.reseau) + " » (minimum " + mmTexte(static_cast<double>(requis)) + ")",
                "Refais le remplissage des zones, ou règle leurs priorités.", boite(commun).centre(), {z.uuid, y.uuid});
    }
    const Polygones horsBord = soustraire(cuivre, gonfler({carte}, -(R.bord - 1'000)));
    if (!horsBord.empty() && aireMm2(horsBord) > 1e-6)
      ajouter(Gravite::Erreur, "DRC-07", nomZone + " à moins de " + mmTexte(static_cast<double>(R.bord)) + " du bord de la carte", "Refais le remplissage des zones (il garde la distance au bord).", boite(horsBord).centre(), {z.uuid});
    // Découpes internes, contrôlées comme le contour (R7)
    for (const auto& dec : c.circuit.contour.decoupes) {
      if (dec.size() < 3) continue;
      const Polygones pres = intersecter(cuivre, gonfler({orienter(dec)}, R.bord - 1'000));
      if (!pres.empty() && aireMm2(pres) > 1e-6)
        ajouter(Gravite::Erreur, "DRC-07", nomZone + " à moins de " + mmTexte(static_cast<double>(R.bord)) + " d’une découpe de la carte", "Refais le remplissage des zones (il garde la distance aux découpes).",
                boite(pres).centre(), {z.uuid});
    }
  }
  // Cuivre sans réseau (R6) : il ne relie rien, son isolement aux autres cuivres n'est pas contrôlé ; il est dit ici
  for (const auto& p : c.circuit.pistes)
    if (p.reseau.empty())
      ajouter(Gravite::Avertissement, "DRC-56", "Piste sans réseau : cuivre isolé, dont l’isolement aux autres cuivres n’est pas contrôlé",
              "Donne-lui le réseau voulu dans son panneau, ou supprime-la si elle ne sert à rien.", {(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2}, {p.uuid});
  for (const auto& z : zs)
    if (z.reseau.empty() && z.contour.size() >= 3)
      ajouter(Gravite::Avertissement, "DRC-56", "Zone sans réseau : cuivre isolé, dont l’isolement aux autres cuivres n’est pas contrôlé",
              "Choisis le réseau de la zone (une masse, un blindage) si elle doit en porter un.", z.contour.front(), {z.uuid});
  {
    const Couches presentes = tousLesCuivres(c.circuit.empilage.couchesCuivre);
    for (const auto& t : c.circuit.textes)
      if (estCuivre(t.couche) && (bit(t.couche) & presentes) && !t.texte.empty())
        ajouter(Gravite::Avertissement, "DRC-56", "Texte « " + t.texte + " » en cuivre sur " + nomCouche(t.couche) + " : cuivre sans réseau",
                "Vérifie qu’il ne touche aucune piste : il relierait ce qu’il touche ; sinon place-le en sérigraphie.", t.p, {t.uuid});
  }
  // Vias sans réseau : reliées à rien, un plan s'en écarte au lieu de les prendre (EL-12)
  for (const auto& via : c.circuit.vias)
    if (via.reseau.empty())
      ajouter(Gravite::Avertissement, "DRC-16", "Via sans réseau : cette via n’est reliée à aucun cuivre", "Donne-lui le réseau voulu dans son panneau (via de couture d’un plan de masse), ou supprime-la.",
              via.p, {via.uuid});
  // Objets sur une couche que l'empilage n'a pas (passage de quatre à deux couches) (EL-17)
  {
    const Couches presentes = tousLesCuivres(c.circuit.empilage.couchesCuivre);
    for (const auto& p : c.circuit.pistes)
      if (estCuivre(p.couche) && !(bit(p.couche) & presentes))
        ajouter(Gravite::Erreur, "DRC-17", std::string("Piste sur la couche ") + nomCouche(p.couche) + ", absente de l’empilage", "Déplace-la sur une couche de la carte, ou supprime-la.",
                {(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2}, {p.uuid});
    // Simple face (gravure maison) : une via est un pont de fil à souder, une pastille CMS sur le dessus n'a pas de cuivre
    if (c.circuit.empilage.couchesCuivre == 1) {
      for (const auto& v : c.circuit.vias)
        ajouter(Gravite::Avertissement, "DRC-37", "Via sur une carte simple face : un pont de fil (strap) à poser à la main", "Évite-la par un détour, ou garde-la et soude un fil.", v.p, {v.uuid});
      std::set<int> signalees;
      for (const auto& p : pp)
        if (p.pastille.type == TypePastille::CMS && !(p.couches & presentes) && signalees.insert(p.empreinte).second)
          ajouter(Gravite::Erreur, "DRC-38", p.reference + " : pastilles CMS sur la face du dessus, sans cuivre sur une carte simple face",
                  "Retourne le composant sous la carte (F), choisis une version traversante, ou passe la carte en double face.", p.pastille.position);
    }
    for (const auto& z : zs)
      if (estCuivre(z.couche) && !(bit(z.couche) & presentes))
        ajouter(Gravite::Erreur, "DRC-17", "Zone « " + (z.reseau.empty() ? std::string("sans réseau") : z.reseau) + " » sur la couche " + nomCouche(z.couche) + ", absente de l’empilage",
                "Déplace-la sur une couche de la carte, ou supprime-la.", z.contour.empty() ? Pt{} : z.contour.front(), {z.uuid});
  }
  // Rayons thermiques réellement reliés : chaque pastille d'une zone de son réseau doit y tenir par au moins
  // « rayonsMin » rayons (KiCad : min_resolved_spokes) ; des vias, des pistes ou l'épaisseur minimale peuvent les
  // couper (EL-02)
  for (const auto& z : zs) {
    if (!z.thermique || z.reseau.empty() || z.contour.size() < 3) continue;
    const auto it = remplis.find(z.uuid);
    if (it == remplis.end()) continue;
    const CuivreIndexe* cuivre = cuivreDeZone(z.uuid);
    const Polygone contourZone = orienter(z.contour);
    const Boite boiteZone = boite(contourZone);
    if (z.epaisseurMin > z.largeurRayon)
      ajouter(Gravite::Avertissement, "DRC-27", "Zone « " + z.reseau + " » : rayons thermiques de " + mmTexte(static_cast<double>(z.largeurRayon)) + " sous l’épaisseur minimale de " +
                                                   mmTexte(static_cast<double>(z.epaisseurMin)) + ", élargis à " + mmTexte(static_cast<double>(largeurRayonEffective(z))),
              "Règle la largeur des rayons au-dessus de l’épaisseur minimale de la zone.", z.contour.front(), {z.uuid});
    for (const auto& p : pp) {
      if (p.reseau != z.reseau || !(p.couches & bit(z.couche)) || !boiteZone.contient(p.pastille.position) || !dedans(contourZone, p.pastille.position)) continue;
      const Pastille& pa = p.pastille;
      const double a = pa.rotation * 3.141592653589793 / 180;
      const double ux = std::cos(a), uy = std::sin(a);
      int rayons = 0;
      // Point au-delà de l'anneau d'isolement thermique, au milieu de chaque rayon : du cuivre, ou rien
      for (int k2 = 0; k2 < 4; ++k2) {
        const double dx = k2 == 0 ? ux : k2 == 1 ? -ux : k2 == 2 ? -uy : uy;
        const double dy = k2 == 0 ? uy : k2 == 1 ? -uy : k2 == 2 ? ux : -ux;
        const double demi = (k2 < 2 ? static_cast<double>(pa.largeur) : static_cast<double>(pa.hauteur)) / 2;
        const double r = demi + static_cast<double>(z.ecartThermique) + 0.6 * static_cast<double>(std::max<Nm>(z.epaisseurMin, 100'000));
        const Pt q{pa.position.x + static_cast<Nm>(std::lround(r * dx)), pa.position.y + static_cast<Nm>(std::lround(r * dy))};
        if (cuivre && cuivre->contient(q)) ++rayons;
      }
      if (rayons < R.rayonsMin)
        ajouter(Gravite::Erreur, "DRC-26", "Pastille " + p.reference + "." + p.numero + " reliée à la zone « " + z.reseau + " » par " + std::to_string(rayons) + " rayon(s) thermique(s) (minimum " +
                                              std::to_string(R.rayonsMin) + ")",
                "Écarte les vias ou les pistes qui coupent ses rayons, ou relie-la par une piste : la soudure se ferait mal ou la masse resterait ouverte.", pa.position, {p.composant});
    }
  }
  // Schéma et circuit : composants qui manquent
  std::set<std::string> places;
  for (const auto& e : c.circuit.empreintes) places.insert(e.composant);
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) {
      const ComposantElec* x = c.composant(s.composant);
      // Bloquant (EL-04) : sinon la nomenclature listerait un composant que le placement ignore
      if (x && !x->empreinte.empty() && !x->horsCarte && !places.count(x->uuid)) {
        places.insert(x->uuid);  // un composant à plusieurs symboles n'est signalé qu'une fois
        ajouter(Gravite::Erreur, "DRC-30", x->reference + " est dans le schéma mais pas sur le circuit", "Lance « Mettre à jour depuis le schéma » (F8), ou coche « hors carte » s’il ne va pas sur la carte.", {}, {x->uuid});
      }
    }
  // Parité schéma-circuit (comme la DRC de KiCad) : empreintes en trop, empreinte changée au schéma, composant
  // déclaré hors carte mais posé, cuivre d'un réseau qui n'existe plus
  {
    std::set<std::string> auSchema;
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles)
        if (!s.composant.empty()) auSchema.insert(s.composant);
    for (const auto& e : c.circuit.empreintes) {
      const ComposantElec* x = c.composant(e.composant);
      const std::string ref = x ? x->reference : std::string("?");
      if (!x || !auSchema.count(e.composant)) {
        // Une carte importée sans schéma (composants sans symbole) n'est pas en défaut
        if (!x || !x->symbole.empty())
          ajouter(Gravite::Erreur, "DRC-39", ref + " est sur le circuit mais plus dans le schéma", "Lance « Mettre à jour depuis le schéma » (F8) pour la retirer, ou remets le composant au schéma.", e.position, {e.composant});
        continue;
      }
      if (!x->empreinte.empty() && x->empreinte != e.empreinte)
        ajouter(Gravite::Erreur, "DRC-40", ref + " : empreinte « " + e.empreinte + " » sur le circuit, « " + x->empreinte + " » au schéma",
                "Lance « Mettre à jour depuis le schéma » (F8) : l’empreinte du circuit suivra celle du schéma.", e.position, {e.composant});
      if (x->horsCarte)
        ajouter(Gravite::Avertissement, "DRC-41", ref + " est déclaré hors carte mais posé sur le circuit", "Retire-le par la mise à jour (F8), ou décoche « hors carte ».", e.position, {e.composant});
    }
    std::set<std::string> orphelins;
    auto reseauConnu = [&](const std::string& n) { return n.empty() || k.indice(k.canonique(n)) >= 0; };
    for (const auto& p : c.circuit.pistes)
      if (!reseauConnu(p.reseau) && orphelins.insert(p.reseau).second)
        ajouter(Gravite::Avertissement, "DRC-42", "Cuivre du réseau « " + p.reseau + " », qui n’existe plus dans le schéma", "Supprime ce cuivre, ou rends son réseau au schéma (étiquette du même nom).",
                {(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2}, {p.uuid});
    for (const auto& vi : c.circuit.vias)
      if (!reseauConnu(vi.reseau) && orphelins.insert(vi.reseau).second)
        ajouter(Gravite::Avertissement, "DRC-42", "Via du réseau « " + vi.reseau + " », qui n’existe plus dans le schéma", "Supprime-la, ou rends son réseau au schéma.", vi.p, {vi.uuid});
    for (const auto& z : c.circuit.zones)
      if (!reseauConnu(z.reseau) && orphelins.insert(z.reseau).second)
        ajouter(Gravite::Avertissement, "DRC-42", "Zone du réseau « " + z.reseau + " », qui n’existe plus dans le schéma", "Choisis un réseau existant pour la zone.", z.contour.empty() ? Pt{} : z.contour.front(), {z.uuid});
  }
  std::stable_sort(v.begin(), v.end(), [](const Violation& a, const Violation& b) { return a.gravite < b.gravite; });
  return v;
}

}  // namespace prom::ecad
