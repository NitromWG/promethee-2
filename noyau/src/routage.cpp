// Prométhée : routage automatique simple. Chaque liaison du chevelu est essayée par des chemins courts à 45° et en
// équerre, avec une sortie droite des pastilles, sur le cuivre du dessus puis du dessous (traversants) ; une piste
// n'est posée que si elle respecte partout l'isolement et le bord de la carte. Les pastilles CMS d'un réseau qui a
// une zone sur l'autre face y descendent par une via. Ce qui ne passe pas reste au chevelu, pour la main.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>

#include "promethee/electronique.hpp"

namespace prom::ecad {

namespace {
using Chemin = std::vector<std::pair<Pt, Pt>>;

struct Obstacles {
  const ConceptionElec& c;
  const std::vector<PastillePlacee>& pp;
  Polygone carte;
  std::vector<std::pair<Pt, Pt>> bords = c.circuit.contour.aretes();  // contour et découpes (EL-44)
  bool dansCarte(Pt p) const { return c.circuit.contour.contient(p); }

  double iso(const std::string& a, const std::string& b) const { return static_cast<double>(isolementEntre(c.circuit.regles, a, b)); }

  bool segment(Pt a, Pt b, Nm largeur, Couche L, const std::string& N) const {
    const Regles& R = c.circuit.regles;
    const double w = static_cast<double>(largeur) / 2;
    if (!dansCarte(a) || !dansCarte(b)) return true;
    for (const auto& [u, v] : bords)
      if (distanceSegments(u, v, a, b) - w < static_cast<double>(R.bord)) return true;
    for (const auto& p : pp) {
      if (!(p.couches & bit(L)) || (!p.reseau.empty() && p.reseau == N)) continue;
      if (distancePolygoneSegment(p.forme, a, b) - w < iso(N, p.reseau)) return true;
    }
    for (const auto& t : c.circuit.pistes) {
      if (t.couche != L || (!t.reseau.empty() && t.reseau == N)) continue;
      if (distanceSegments(a, b, t.a, t.b) - w - static_cast<double>(t.largeur) / 2 < iso(N, t.reseau)) return true;
    }
    for (const auto& v : c.circuit.vias) {
      if (!v.reseau.empty() && v.reseau == N) continue;
      if (distancePointSegment(v.p, a, b) - w - static_cast<double>(v.diametre) / 2 < iso(N, v.reseau)) return true;
    }
    return false;
  }
  // Premier obstacle rencontré par un segment (diagnostic) ; vide si le segment passe.
  std::string bloqueur(Pt a, Pt b, Nm largeur, Couche L, const std::string& N) const {
    const Regles& R = c.circuit.regles;
    const double w = static_cast<double>(largeur) / 2;
    if (!dansCarte(a) || !dansCarte(b)) return "le bord de la carte (tracé hors de la matière)";
    for (const auto& [u, v] : bords)
      if (distanceSegments(u, v, a, b) - w < static_cast<double>(R.bord)) return "le bord de la carte";
    for (const auto& p : pp) {
      if (!(p.couches & bit(L)) || (!p.reseau.empty() && p.reseau == N)) continue;
      if (distancePolygoneSegment(p.forme, a, b) - w < iso(N, p.reseau))
        return "la pastille " + p.reference + "." + p.numero + (p.reseau.empty() ? std::string() : " (réseau « " + p.reseau + " »)");
    }
    for (const auto& t : c.circuit.pistes) {
      if (t.couche != L || (!t.reseau.empty() && t.reseau == N)) continue;
      if (distanceSegments(a, b, t.a, t.b) - w - static_cast<double>(t.largeur) / 2 < iso(N, t.reseau)) return "une piste du réseau « " + t.reseau + " »";
    }
    for (const auto& v : c.circuit.vias) {
      if (!v.reseau.empty() && v.reseau == N) continue;
      if (distancePointSegment(v.p, a, b) - w - static_cast<double>(v.diametre) / 2 < iso(N, v.reseau)) return "une via du réseau « " + v.reseau + " »";
    }
    return {};
  }
  bool chemin(const Chemin& ch, Nm largeur, Couche L, const std::string& N) const {
    for (const auto& [a, b] : ch)
      if (segment(a, b, largeur, L, N)) return true;
    return false;
  }
  // Via : loin du bord, de toutes les pastilles (pas de via dans une pastille), des pistes et vias des autres réseaux
  bool via(Pt p, Nm diametre, const std::string& N) const {
    const Regles& R = c.circuit.regles;
    const double r = static_cast<double>(diametre) / 2;
    if (!dansCarte(p)) return true;
    for (const auto& [u, v] : bords)
      if (distancePointSegment(p, u, v) - r < static_cast<double>(R.bord)) return true;
    for (const auto& x : pp)
      if (distancePolygoneSegment(x.forme, p, p) - r < iso(N, x.reseau)) return true;
    for (const auto& t : c.circuit.pistes) {
      if (!t.reseau.empty() && t.reseau == N) continue;
      if (distancePointSegment(p, t.a, t.b) - r - static_cast<double>(t.largeur) / 2 < iso(N, t.reseau)) return true;
    }
    for (const auto& v : c.circuit.vias)
      if (distance(p, v.p) - r - static_cast<double>(v.diametre) / 2 < iso(N, v.reseau)) return true;
    return false;
  }
};

Nm signe(Nm v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

// Chemins à deux segments entre a et b : 45° (diagonale d'abord ou droit d'abord) et équerres
std::vector<Chemin> variantes(Pt a, Pt b) {
  std::vector<Chemin> r;
  const Nm dx = b.x - a.x, dy = b.y - a.y, ax = std::abs(dx), ay = std::abs(dy), d = std::min(ax, ay);
  const Nm sx = signe(dx), sy = signe(dy);
  auto ajouter = [&](Pt m) {
    Chemin ch;
    if (m != a) ch.push_back({a, m});
    if (b != m) ch.push_back({m, b});
    if (ch.empty()) return;
    for (const auto& x : r)
      if (x == ch) return;
    r.push_back(ch);
  };
  ajouter({a.x + sx * d, a.y + sy * d});
  ajouter(ax > ay ? Pt{a.x + sx * (ax - d), a.y} : Pt{a.x, a.y + sy * (ay - d)});
  ajouter({b.x, a.y});
  ajouter({a.x, b.y});
  return r;
}

// Sortie droite d'une pastille, dans l'axe de sa plus grande longueur, vers l'autre extrémité
Pt sortie(const PastillePlacee& p, Pt vers) {
  const Boite bo = boite(p.forme);
  const Pt c = p.pastille.position;
  if (bo.largeur() >= bo.hauteur()) {
    const Nm l = bo.largeur() / 2 + 600'000;
    return {c.x + (vers.x >= c.x ? l : -l), c.y};
  }
  const Nm l = bo.hauteur() / 2 + 600'000;
  return {c.x, c.y + (vers.y >= c.y ? l : -l)};
}

double longueur(const Chemin& ch) {
  double l = 0;
  for (const auto& [a, b] : ch) l += distance(a, b);
  return l;
}

// Points de descente possibles autour d'une pastille (via juste hors de la pastille), le premier vers l'autre bout
std::vector<Pt> descentes(const PastillePlacee& p, Pt vers, Nm diametreVia, Nm isolement) {
  const Boite bo = boite(p.forme);
  const Pt c = p.pastille.position;
  const Nm ex = bo.largeur() / 2 + diametreVia / 2 + isolement + 50'000, ey = bo.hauteur() / 2 + diametreVia / 2 + isolement + 50'000;
  std::vector<Pt> r = {{c.x + (vers.x >= c.x ? ex : -ex), c.y}, {c.x, c.y + (vers.y >= c.y ? ey : -ey)}, {c.x - (vers.x >= c.x ? ex : -ex), c.y}, {c.x, c.y - (vers.y >= c.y ? ey : -ey)}};
  return r;
}

const PastillePlacee* pastilleEn(const std::vector<PastillePlacee>& pp, Pt p, const std::string& reseau) {
  for (const auto& x : pp)
    if (x.pastille.position == p && x.reseau == reseau) return &x;
  return nullptr;
}

// Tracés directs essayés pour une liaison : variantes à deux segments, avec ou sans sortie droite des pastilles,
// du plus court au plus long
std::vector<Chemin> candidatsDirects(const PastillePlacee& pa, const PastillePlacee& pb, const Liaison& l) {
  std::vector<Chemin> candidats;
  const Pt sa = sortie(pa, l.b), sb = sortie(pb, l.a);
  for (const auto& v : variantes(l.a, l.b)) candidats.push_back(v);
  for (const auto& v : variantes(sa, l.b)) {
    Chemin ch{{l.a, sa}};
    ch.insert(ch.end(), v.begin(), v.end());
    candidats.push_back(ch);
  }
  for (const auto& v : variantes(l.a, sb)) {
    Chemin ch = v;
    ch.push_back({sb, l.b});
    candidats.push_back(ch);
  }
  for (const auto& v : variantes(sa, sb)) {
    Chemin ch{{l.a, sa}};
    ch.insert(ch.end(), v.begin(), v.end());
    ch.push_back({sb, l.b});
    candidats.push_back(ch);
  }
  std::stable_sort(candidats.begin(), candidats.end(), [](const Chemin& x, const Chemin& y) { return longueur(x) < longueur(y); });
  return candidats;
}

// Une passe : chaque liaison du chevelu, du plus court au plus long
int routerLiaisons(ConceptionElec& c, bool changerDeFace) {
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const Remplissages z = remplirZones(c, k);
  auto chevelu = etatRoutage(c, k, z).chevelu;
  std::sort(chevelu.begin(), chevelu.end(), [](const Liaison& a, const Liaison& b) { return distance(a.a, a.b) < distance(b.a, b.b); });
  Obstacles o{c, pp, c.circuit.contour.polygone()};
  int poses = 0;
  for (const Liaison& l : chevelu) {
    const PastillePlacee* pa = pastilleEn(pp, l.a, l.reseau);
    const PastillePlacee* pb = pastilleEn(pp, l.b, l.reseau);
    if (!pa || !pb) continue;
    const Nm largeur = largeurPiste(c.circuit.regles, l.reseau);
    const std::vector<Chemin> candidats = candidatsDirects(*pa, *pb, l);
    bool fait = false;
    for (const Couche L : {Couche::CuivreDessus, Couche::CuivreDessous}) {
      if (!(pa->couches & bit(L)) || !(pb->couches & bit(L))) continue;
      for (const auto& ch : candidats) {
        if (o.chemin(ch, largeur, L, l.reseau)) continue;
        for (const auto& [a, b] : ch) c.circuit.pistes.push_back({nouvelUuid(), l.reseau, L, a, b, largeur});
        ++poses;
        fait = true;
        break;
      }
      if (fait) break;
    }
    if (fait || !changerDeFace || c.circuit.empilage.couchesCuivre < 2) continue;  // simple face : pas de via
    // Changement de face : descente par une via près de chaque bout, trajet au dos, remontée
    const Nm dv = viaDiametre(c.circuit.regles, l.reseau), pv = viaPercage(c.circuit.regles, l.reseau), isolement = c.circuit.regles.isolement;
    const Couche Lh = (pa->couches & bit(Couche::CuivreDessus)) ? Couche::CuivreDessus : Couche::CuivreDessous;
    const Couche Lb = Lh == Couche::CuivreDessus ? Couche::CuivreDessous : Couche::CuivreDessus;
    const Couche Lf = (pb->couches & bit(Lh)) ? Lh : Lb;
    if (!(pb->couches & bit(Lf))) continue;
    struct Essai {
      Chemin haut, dos, fin;
      Pt va, vb;
      double l;
    };
    // Une pastille traversante descend sans via : le trajet au dos part d'elle directement
    const bool aTraverse = (pa->couches & bit(Lb)) != 0, bTraverse = (pb->couches & bit(Lb)) != 0;
    std::vector<Essai> essais;
    std::vector<Pt> pointsA = descentes(*pa, l.b, dv, isolement), pointsB = descentes(*pb, l.a, dv, isolement);
    if (aTraverse) pointsA.insert(pointsA.begin(), l.a);
    if (bTraverse) pointsB.insert(pointsB.begin(), l.b);
    for (const Pt va : pointsA)
      for (const Pt vb : pointsB) {
        if (va == vb || (va == l.a && vb == l.b)) continue;
        for (const auto& d : variantes(va, vb)) {
          Essai e{{}, d, {}, va, vb, 0};
          if (va != l.a) e.haut = {{l.a, va}};
          if (vb != l.b) e.fin = {{vb, l.b}};
          e.l = longueur(e.haut) + longueur(d) + longueur(e.fin) + (va != l.a ? 2e6 : 0) + (vb != l.b ? 2e6 : 0);  // une via coûte 2 mm
          essais.push_back(e);
        }
      }
    std::stable_sort(essais.begin(), essais.end(), [](const Essai& x, const Essai& y) { return x.l < y.l; });
    for (const auto& e : essais) {
      const bool viaA = e.va != l.a, viaB = e.vb != l.b;
      if ((viaA && o.via(e.va, dv, l.reseau)) || (viaB && o.via(e.vb, dv, l.reseau))) continue;
      if (o.chemin(e.haut, largeur, Lh, l.reseau) || o.chemin(e.dos, largeur, Lb, l.reseau) || o.chemin(e.fin, largeur, Lf == Lh ? Lh : Lb, l.reseau)) continue;
      if (viaA && viaB && distance(e.va, e.vb) < static_cast<double>(dv + isolement)) continue;
      for (const auto& [a, b] : e.haut) c.circuit.pistes.push_back({nouvelUuid(), l.reseau, Lh, a, b, largeur});
      for (const auto& [a, b] : e.dos) c.circuit.pistes.push_back({nouvelUuid(), l.reseau, Lb, a, b, largeur});
      for (const auto& [a, b] : e.fin) c.circuit.pistes.push_back({nouvelUuid(), l.reseau, Lf == Lh ? Lh : Lb, a, b, largeur});
      if (viaA) c.circuit.vias.push_back({nouvelUuid(), l.reseau, e.va, dv, pv});
      if (viaB) c.circuit.vias.push_back({nouvelUuid(), l.reseau, e.vb, dv, pv});
      ++poses;
      break;
    }
  }
  return poses;
}

// Une passe : les pastilles CMS d'un réseau à zone sur l'autre face y descendent par une via
int coudre(ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const Remplissages z = remplirZones(c, k);
  const auto chevelu = etatRoutage(c, k, z).chevelu;
  Obstacles o{c, pp, c.circuit.contour.polygone()};
  int vias = 0;
  std::vector<Pt> traitees;
  for (const Liaison& l : chevelu)
    for (const Pt bout : {l.a, l.b}) {
      if (std::find(traitees.begin(), traitees.end(), bout) != traitees.end()) continue;
      const PastillePlacee* p = pastilleEn(pp, bout, l.reseau);
      if (!p || p->pastille.type != TypePastille::CMS) continue;
      const Couche L = (p->couches & bit(Couche::CuivreDessus)) ? Couche::CuivreDessus : Couche::CuivreDessous;
      // Une zone du même réseau sur une autre couche de cuivre
      const Zone* zone = nullptr;
      for (const auto& x : c.circuit.zones)
        if (x.reseau == l.reseau && x.couche != L && z.count(x.uuid)) zone = &x;
      if (!zone) continue;
      traitees.push_back(bout);
      const Nm dv = viaDiametre(c.circuit.regles, l.reseau), pv = viaPercage(c.circuit.regles, l.reseau), largeur = largeurPiste(c.circuit.regles, l.reseau);
      const Boite bo = boite(p->forme);
      const Nm ex = bo.largeur() / 2 + dv / 2 + 300'000, ey = bo.hauteur() / 2 + dv / 2 + 300'000;
      const Pt centre = p->pastille.position;
      const std::vector<Pt> essais = {{centre.x + ex, centre.y}, {centre.x - ex, centre.y}, {centre.x, centre.y + ey}, {centre.x, centre.y - ey},
                                      {centre.x + ex, centre.y + ey}, {centre.x - ex, centre.y + ey}, {centre.x + ex, centre.y - ey}, {centre.x - ex, centre.y - ey},
                                      {centre.x + ex + 700'000, centre.y}, {centre.x - ex - 700'000, centre.y}, {centre.x, centre.y + ey + 700'000}, {centre.x, centre.y - ey - 700'000}};
      for (const Pt v : essais) {
        if (!dedans(z.at(zone->uuid), v) || o.via(v, dv, l.reseau)) continue;
        Chemin ch{{centre, v}};
        if (o.chemin(ch, largeur, L, l.reseau)) continue;
        c.circuit.pistes.push_back({nouvelUuid(), l.reseau, L, centre, v, largeur});
        c.circuit.vias.push_back({nouvelUuid(), l.reseau, v, dv, pv});
        ++vias;
        break;
      }
    }
  return vias;
}
}  // namespace

std::vector<LiaisonRestante> diagnostiquerChevelu(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const Remplissages z = remplirZones(c, k);
  const auto chevelu = etatRoutage(c, k, z).chevelu;
  Obstacles o{c, pp, c.circuit.contour.polygone()};
  std::vector<LiaisonRestante> r;
  for (const Liaison& l : chevelu) {
    LiaisonRestante x{l.reseau, l.a, l.b, {}};
    const PastillePlacee* pa = pastilleEn(pp, l.a, l.reseau);
    const PastillePlacee* pb = pastilleEn(pp, l.b, l.reseau);
    if (!pa || !pb) {
      x.raison = "une extrémité n’est pas une pastille (piste ou zone) : à relier à la main";
      r.push_back(x);
      continue;
    }
    const Nm largeur = largeurPiste(c.circuit.regles, l.reseau);
    const std::vector<Chemin> candidats = candidatsDirects(*pa, *pb, l);
    std::string cause;
    bool coucheCommune = false;
    for (const Couche L : {Couche::CuivreDessus, Couche::CuivreDessous}) {
      if (!(pa->couches & bit(L)) || !(pb->couches & bit(L))) continue;
      coucheCommune = true;
      if (candidats.empty() || !cause.empty()) continue;
      for (const auto& [a, b] : candidats.front())
        if (const std::string q = o.bloqueur(a, b, largeur, L, l.reseau); !q.empty()) {
          cause = "le tracé le plus court (" + std::string(nomCouche(L)) + ") passe trop près de " + q;
          break;
        }
    }
    if (!coucheCommune) cause = "pastilles sur des faces différentes, et aucune descente par via n’a trouvé de place";
    if (cause.empty()) cause = "tous les tracés directs essayés coupent l’isolement d’un autre réseau";
    x.raison = cause + " ; le routeur simple ne cherche pas de détour : à router à la main";
    r.push_back(x);
  }
  return r;
}

BilanRoutage routerSimple(ConceptionElec& c) {
  exigerModifiable(c);  // une révision publiée ne reçoit plus une piste, même hors de l'interface (F4)
  BilanRoutage b;
  synchroniserReseaux(c);  // le cuivre existant porte les noms actuels des réseaux (EL-01)
  // Le bilan se compte sur la carte elle-même : liaisons du chevelu résolues, vias ajoutées (descentes vers
  // les plans comme changements de face), liaisons restantes
  const std::size_t viasAvant = c.circuit.vias.size();
  std::size_t aFaire = 0;
  {
    const Connectivite k = connectivite(c);
    aFaire = etatRoutage(c, k, remplirZones(c, k)).chevelu.size();
  }
  // D'abord les descentes vers les plans, puis les liaisons sur une face, enfin celles qui changent de face
  for (int passe = 0; passe < 6; ++passe) {
    const int v = coudre(c);
    int p = routerLiaisons(c, false);
    if (p == 0 && v == 0) p = routerLiaisons(c, true);
    if (p == 0 && v == 0) break;
  }
  b.nonRoutees = diagnostiquerChevelu(c);
  b.restantes = static_cast<int>(b.nonRoutees.size());
  b.vias = static_cast<int>(c.circuit.vias.size() - viasAvant);
  b.liaisons = static_cast<int>(aFaire > b.nonRoutees.size() ? aFaire - b.nonRoutees.size() : 0);
  return b;
}

}  // namespace prom::ecad
