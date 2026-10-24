// Prométhée : gestes du circuit imprimé côté noyau (lot L5, 0.34) — contour, repère de travail, collisions, chevelu,
// renumérotation, pistes posées, presse-papiers, carte sans schéma, changements en masse, réglages repris.
// Licence GPL-3.0-only.
#include "promethee/circuit_edition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <sstream>

#include "promethee/archive.hpp"
#include "promethee/routeur.hpp"

namespace prom::ecad {

namespace {
Polygone& boucleModifiable(ConceptionElec& c, int boucle, bool& ok) {
  static Polygone vide;
  ok = false;
  ContourCarte& k = c.circuit.contour;
  if (boucle < 0) {
    if (k.libre.size() < 3) k.libre = k.polygone();  // le rectangle devient un polygone libre, mêmes sommets
    ok = true;
    return k.libre;
  }
  if (static_cast<std::size_t>(boucle) < k.decoupes.size()) {
    ok = true;
    return k.decoupes[static_cast<std::size_t>(boucle)];
  }
  return vide;
}
}  // namespace

// ---------- 5.4, 5.31 ----------
std::vector<Pt> sommetsContour(const ConceptionElec& c, int boucle) {
  const ContourCarte& k = c.circuit.contour;
  if (boucle < 0) return k.libre.size() >= 3 ? k.libre : k.polygone();
  return static_cast<std::size_t>(boucle) < k.decoupes.size() ? k.decoupes[static_cast<std::size_t>(boucle)] : std::vector<Pt>{};
}
bool deplacerSommetContour(ConceptionElec& c, int boucle, std::size_t sommet, Pt p) {
  exigerModifiable(c);
  bool ok = false;
  Polygone& b = boucleModifiable(c, boucle, ok);
  if (!ok || sommet >= b.size()) return false;
  b[sommet] = p;
  return true;
}
bool insererSommetContour(ConceptionElec& c, int boucle, std::size_t apres, Pt p) {
  exigerModifiable(c);
  bool ok = false;
  Polygone& b = boucleModifiable(c, boucle, ok);
  if (!ok || apres >= b.size()) return false;
  b.insert(b.begin() + static_cast<std::ptrdiff_t>(apres + 1), p);
  return true;
}
bool supprimerSommetContour(ConceptionElec& c, int boucle, std::size_t sommet) {
  exigerModifiable(c);
  bool ok = false;
  Polygone& b = boucleModifiable(c, boucle, ok);
  if (!ok || sommet >= b.size() || b.size() <= 3) return false;
  b.erase(b.begin() + static_cast<std::ptrdiff_t>(sommet));
  return true;
}
Pt versRepereTravail(const AffichageCircuit& a, Pt p) {
  const Pt o = a.origineTravail.value_or(Pt{});
  Pt q = p - o;
  if (a.yVersLeHaut) q.y = -q.y;
  return q;
}
Pt depuisRepereTravail(const AffichageCircuit& a, Pt q) {
  if (a.yVersLeHaut) q.y = -q.y;
  return q + a.origineTravail.value_or(Pt{});
}
bool contourDepuisPoints(ConceptionElec& c, const std::vector<Pt>& points) {
  exigerModifiable(c);
  if (points.size() < 3) return false;
  Polygone p;
  for (const Pt& q : points) p.push_back(depuisRepereTravail(c.circuit.affichage, q));
  if (std::abs(aireMm2(p)) < 1e-6) return false;
  c.circuit.contour.libre = orienter(p);
  return true;
}
std::vector<std::string> collisionsEncombrement(const ConceptionElec& c, const std::string& composant) {
  std::vector<std::string> r;
  const auto it = std::find_if(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == composant; });
  if (it == c.circuit.empreintes.end()) return r;
  const DefEmpreinte* d = c.empreinte(it->empreinte);
  if (!d) return r;
  const Polygone a = encombrementSurCarte(*it, *d);
  const Boite ba = boite(a);
  for (const auto& e : c.circuit.empreintes) {
    if (e.composant == composant || e.dessous != it->dessous) continue;
    const DefEmpreinte* f = c.empreinte(e.empreinte);
    if (!f) continue;
    const Polygone b = encombrementSurCarte(e, *f);
    if (!ba.touche(boite(b))) continue;
    const Polygones commun = intersecter({a}, {b});
    if (!commun.empty() && aireMm2(commun) > 1e-4) r.push_back(e.composant);
  }
  return r;
}

// ---------- 5.6, 5.19 ----------
double longueurChevelu(const EtatRoutage& e) {
  double s = 0;
  for (const auto& l : e.chevelu) s += distance(l.a, l.b);
  return s;
}

namespace {
struct Place {
  std::string composant, avant, prefixe;
  Pt p;
  bool dessous;
};
std::string prefixeRepere(const std::string& ref) {
  std::string p;
  for (char ch : ref) {
    if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '?') break;
    p += ch;
  }
  return p;
}
}  // namespace

std::vector<ChangementReference> renumerotationPrevue(const ConceptionElec& c, const OptionsRenumerotation& o) {
  std::vector<Place> places;
  for (const auto& e : c.circuit.empreintes) {
    const ComposantElec* x = c.composant(e.composant);
    if (!x || x->reference.empty()) continue;
    if (!o.portee.empty() && !o.portee.count(e.composant)) continue;
    if (o.exclus.count(x->reference)) continue;
    std::string pre = prefixeRepere(x->reference);
    if (e.dessous && !o.prefixeDessous.empty() && pre.rfind(o.prefixeDessous, 0) == 0 && pre.size() > o.prefixeDessous.size()) pre = pre.substr(o.prefixeDessous.size());
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    places.push_back({e.composant, x->reference, pre, d ? versCarte(e, pointDePlacement(*d)) : e.position, e.dessous});
  }
  // Clé de tri : rangée (direction secondaire, arrondie à la tolérance), puis position dans la rangée
  const bool principalX = o.parcours == Parcours::GDHB || o.parcours == Parcours::GDBH || o.parcours == Parcours::DGHB || o.parcours == Parcours::DGBH;
  const bool sensPrincipal = o.parcours == Parcours::GDHB || o.parcours == Parcours::GDBH || o.parcours == Parcours::HBGD || o.parcours == Parcours::HBDG;
  const bool sensSecondaire = o.parcours == Parcours::GDHB || o.parcours == Parcours::DGHB || o.parcours == Parcours::HBGD || o.parcours == Parcours::BHGD;
  // GDHB : de gauche à droite dans une rangée, rangées de haut en bas (lecture) ; y croît vers le bas à l'écran
  auto cle = [&](const Place& p) {
    const Nm principal = principalX ? p.p.x : p.p.y, secondaire = principalX ? p.p.y : p.p.x;
    const Nm rangee = static_cast<Nm>(std::floor(static_cast<double>(sensSecondaire ? secondaire : -secondaire) / static_cast<double>(std::max<Nm>(o.rangee, 1))));
    return std::make_tuple(o.dessusDabord ? (p.dessous ? 1 : 0) : (p.dessous ? 0 : 1), rangee, sensPrincipal ? principal : -principal);
  };
  std::stable_sort(places.begin(), places.end(), [&](const Place& a, const Place& b) { return cle(a) < cle(b); });
  // Repères gardés (exclus, hors portée) : leurs numéros ne sont pas redonnés
  std::set<std::string> pris;
  for (const auto& x : c.composants) {
    const bool renumerote = std::any_of(places.begin(), places.end(), [&](const Place& p) { return p.composant == x.uuid; });
    if (!renumerote && !x.reference.empty()) pris.insert(x.reference);
  }
  std::map<std::string, int> compteurs;
  std::vector<ChangementReference> r;
  for (const auto& p : places) {
    const std::string pre = (p.dessous && !o.prefixeDessous.empty() ? o.prefixeDessous : std::string()) + p.prefixe;
    int& n = compteurs.try_emplace(pre, o.debut).first->second;
    std::string ref;
    do ref = pre + std::to_string(n++);
    while (pris.count(ref));
    pris.insert(ref);
    if (ref != p.avant) r.push_back({p.composant, p.avant, ref});
  }
  return r;
}

int renumeroter(ConceptionElec& c, const OptionsRenumerotation& o, std::vector<std::string>* journal) {
  exigerModifiable(c);
  const auto changements = renumerotationPrevue(c, o);
  for (const auto& ch : changements)
    if (ComposantElec* x = c.composant(ch.composant)) {
      x->reference = ch.apres;
      if (journal) journal->push_back(ch.avant + " → " + ch.apres);
    }
  return static_cast<int>(changements.size());
}

// ---------- 5.9, 5.23 ----------
std::vector<ChangementNettoyage> nettoyagePrevu(const ConceptionElec& c, const std::set<std::string>* selection) {
  std::vector<ChangementNettoyage> r;
  const auto& P = c.circuit.pistes;
  auto prise = [&](const std::string& id) { return !selection || selection->count(id); };
  std::set<std::string> vus;
  for (std::size_t i = 0; i < P.size(); ++i) {
    const Piste& a = P[i];
    if (!prise(a.uuid) || a.verrou != Verrou::Aucun) continue;
    if (!a.milieu && a.a == a.b) {
      r.push_back({"nul", "Segment nul sur « " + a.reseau + " »", {a.uuid}});
      continue;
    }
    for (std::size_t j = i + 1; j < P.size(); ++j) {
      const Piste& b = P[j];
      if (b.couche != a.couche || b.reseau != a.reseau || b.milieu || a.milieu || b.verrou != Verrou::Aucun) continue;
      if (((a.a == b.a && a.b == b.b) || (a.a == b.b && a.b == b.a)) && vus.insert(b.uuid).second) {
        r.push_back({"double", "Segment en double sur « " + a.reseau + " »", {b.uuid}});
        continue;
      }
      // Deux segments colinéaires bout à bout, de même largeur, que rien d'autre ne touche à leur jonction
      if (b.largeur != a.largeur || vus.count(a.uuid) || vus.count(b.uuid)) continue;
      Pt commun{};
      Pt ea{}, eb{};
      if (a.b == b.a) commun = a.b, ea = a.a, eb = b.b;
      else if (a.b == b.b) commun = a.b, ea = a.a, eb = b.a;
      else if (a.a == b.a) commun = a.a, ea = a.b, eb = b.b;
      else if (a.a == b.b) commun = a.a, ea = a.b, eb = b.a;
      else continue;
      const double croix = static_cast<double>(commun.x - ea.x) * static_cast<double>(eb.y - commun.y) - static_cast<double>(commun.y - ea.y) * static_cast<double>(eb.x - commun.x);
      if (std::abs(croix) > 1e-3 * distance(ea, commun) * distance(commun, eb)) continue;
      int autres = 0;
      for (const auto& x : P)
        if (x.uuid != a.uuid && x.uuid != b.uuid && x.couche == a.couche && (x.a == commun || x.b == commun)) ++autres;
      for (const auto& v : c.circuit.vias)
        if (v.p == commun) ++autres;
      if (autres) continue;
      vus.insert(a.uuid);
      vus.insert(b.uuid);
      r.push_back({"fusion", "Deux segments alignés réunis sur « " + a.reseau + " »", {a.uuid, b.uuid}});
    }
  }
  // Vias reliées à rien (ni piste ni zone de leur réseau)
  for (const auto& v : c.circuit.vias) {
    if (!prise(v.uuid) || v.verrou != Verrou::Aucun || !v.groupe.empty()) continue;
    bool relie = false;
    for (const auto& p : P) relie = relie || (p.reseau == v.reseau && distancePointSegment(v.p, p.a, p.b) <= static_cast<double>(v.diametre) / 2);
    for (const auto& z : c.circuit.zones) relie = relie || (z.reseau == v.reseau && z.contour.size() >= 3 && dedans(z.contour, v.p));
    if (!relie) r.push_back({"via orpheline", "Via de « " + v.reseau + " » reliée à rien", {v.uuid}});
  }
  return r;
}

int nettoyer(ConceptionElec& c, const std::vector<ChangementNettoyage>& choisis) {
  exigerModifiable(c);
  int n = 0;
  auto& P = c.circuit.pistes;
  for (const auto& ch : choisis) {
    if (ch.genre == "nul" || ch.genre == "double") {
      for (const auto& id : ch.objets) n += static_cast<int>(std::erase_if(P, [&](const Piste& p) { return p.uuid == id; }));
    } else if (ch.genre == "fusion" && ch.objets.size() == 2) {
      auto ia = std::find_if(P.begin(), P.end(), [&](const Piste& p) { return p.uuid == ch.objets[0]; });
      auto ib = std::find_if(P.begin(), P.end(), [&](const Piste& p) { return p.uuid == ch.objets[1]; });
      if (ia == P.end() || ib == P.end()) continue;
      // Les deux bouts qui ne sont pas la jonction
      Pt e1 = ia->a == ib->a || ia->a == ib->b ? ia->b : ia->a;
      Pt e2 = ib->a == ia->a || ib->a == ia->b ? ib->b : ib->a;
      ia->a = e1;
      ia->b = e2;
      P.erase(ib);
      ++n;
    } else if (ch.genre == "via orpheline") {
      for (const auto& id : ch.objets) n += static_cast<int>(std::erase_if(c.circuit.vias, [&](const Via& v) { return v.uuid == id; }));
    }
  }
  return n;
}

bool glisserSegment(ConceptionElec& c, const std::string& id, Pt delta) {
  exigerModifiable(c);
  auto& P = c.circuit.pistes;
  const auto it = std::find_if(P.begin(), P.end(), [&](const Piste& p) { return p.uuid == id; });
  if (it == P.end() || it->verrou == Verrou::Fige || it->milieu) return false;
  // Le segment ne glisse que perpendiculairement à lui-même : le déplacement le long de son axe est retiré
  const double dx = static_cast<double>(it->b.x - it->a.x), dy = static_cast<double>(it->b.y - it->a.y), L = std::hypot(dx, dy);
  if (L < 1) return false;
  const double nx = -dy / L, ny = dx / L;
  const double d = static_cast<double>(delta.x) * nx + static_cast<double>(delta.y) * ny;
  const Pt dep{static_cast<Nm>(std::llround(d * nx)), static_cast<Nm>(std::llround(d * ny))};
  const Pt a0 = it->a, b0 = it->b;
  const Pt a1 = a0 + dep, b1 = b0 + dep;
  const Couche couche = it->couche;
  const std::string reseau = it->reseau;
  const Nm largeur = it->largeur;
  it->a = a1;
  it->b = b1;
  // Chaque voisin accroché à un bout : il se raccourcit ou s'allonge le long de son axe s'il le peut (angle de 45°
  // gardé), sinon un raccord à 45° est inséré
  std::vector<Piste> ajouts;
  for (const auto& [ancien, neuf] : {std::pair{a0, a1}, std::pair{b0, b1}}) {
    for (auto& q : P) {
      if (q.uuid == id || q.couche != couche || q.milieu) continue;
      Pt* bout = q.a == ancien ? &q.a : q.b == ancien ? &q.b : nullptr;
      if (!bout) continue;
      Pt* autre = bout == &q.a ? &q.b : &q.a;
      const double qx = static_cast<double>(bout->x - autre->x), qy = static_cast<double>(bout->y - autre->y), ql = std::hypot(qx, qy);
      if (ql < 1) {
        *bout = neuf;
        continue;
      }
      // Intersection de l'axe du voisin avec la nouvelle droite du segment glissé : t tel que autre + t·u y soit,
      // (autre + t·u − a1)·n = 0 ; un voisin parallèle au segment ne la coupe pas, un raccord est posé
      const double ux = qx / ql, uy = qy / ql;
      const double un = ux * nx + uy * ny;
      const double ax = static_cast<double>(a1.x), ay = static_cast<double>(a1.y);
      if (std::abs(un) > 1e-6) {
        const double t = ((ax - static_cast<double>(autre->x)) * nx + (ay - static_cast<double>(autre->y)) * ny) / un;
        const Pt x{static_cast<Nm>(std::llround(static_cast<double>(autre->x) + t * ux)), static_cast<Nm>(std::llround(static_cast<double>(autre->y) + t * uy))};
        *bout = x;
        // Le segment glissé est ramené à cette nouvelle jonction
        if (ancien == a0) it->a = x;
        else it->b = x;
      } else {
        ajouts.push_back({nouvelUuid(), reseau, couche, ancien, neuf, largeur});
      }
    }
  }
  for (auto& p : ajouts) P.push_back(std::move(p));
  return true;
}

bool deplacerVia(ConceptionElec& c, const std::string& id, Pt p) {
  exigerModifiable(c);
  const auto it = std::find_if(c.circuit.vias.begin(), c.circuit.vias.end(), [&](const Via& v) { return v.uuid == id; });
  if (it == c.circuit.vias.end() || it->verrou == Verrou::Fige) return false;
  const Pt ancien = it->p;
  it->p = p;
  for (auto& q : c.circuit.pistes) {
    if (q.reseau != it->reseau) continue;
    if (q.a == ancien) q.a = p;
    if (q.b == ancien) q.b = p;
  }
  return true;
}

bool couperPiste(ConceptionElec& c, const std::string& id, Pt p) {
  exigerModifiable(c);
  auto& P = c.circuit.pistes;
  const auto it = std::find_if(P.begin(), P.end(), [&](const Piste& x) { return x.uuid == id; });
  if (it == P.end() || it->milieu) return false;
  const Pt q = projeteSurSegment(p, it->a, it->b);
  if (q == it->a || q == it->b) return false;
  Piste b = *it;
  b.uuid = nouvelUuid();
  b.a = q;
  it->b = q;
  P.push_back(b);
  return true;
}

bool trancherPiste(ConceptionElec& c, const std::string& id, Pt p, Nm ecart) {
  exigerModifiable(c);
  auto& P = c.circuit.pistes;
  const auto it = std::find_if(P.begin(), P.end(), [&](const Piste& x) { return x.uuid == id; });
  if (it == P.end() || it->milieu) return false;
  const Pt q = projeteSurSegment(p, it->a, it->b);
  const double L = distance(it->a, it->b);
  if (L < 1) return false;
  const double ux = static_cast<double>(it->b.x - it->a.x) / L, uy = static_cast<double>(it->b.y - it->a.y) / L;
  const double demi = static_cast<double>(ecart) / 2 + static_cast<double>(it->largeur) / 2;
  const Pt q1{q.x - static_cast<Nm>(std::llround(ux * demi)), q.y - static_cast<Nm>(std::llround(uy * demi))};
  const Pt q2{q.x + static_cast<Nm>(std::llround(ux * demi)), q.y + static_cast<Nm>(std::llround(uy * demi))};
  if (distance(it->a, q1) < 1 || distance(q2, it->b) < 1) return false;
  Piste b = *it;
  b.uuid = nouvelUuid();
  b.a = q2;
  it->b = q1;
  P.push_back(b);
  return true;
}

int derouter(ConceptionElec& c, const std::string& portee, const std::set<std::string>& gardes) {
  exigerModifiable(c);
  const Connectivite k = connectivite(c);
  std::set<std::string> reseaux;
  bool tout = portee == "carte";
  if (portee.rfind("reseau:", 0) == 0) reseaux.insert(k.canonique(portee.substr(7)));
  if (portee.rfind("composant:", 0) == 0) {
    const std::string comp = portee.substr(10);
    for (const auto& p : pastilles(c, k))
      if (p.composant == comp && !p.reseau.empty()) reseaux.insert(p.reseau);
  }
  std::set<std::string> pistesVisees;
  if (portee.rfind("connexion:", 0) == 0) {
    // Les segments reliés bout à bout à la piste désignée, jusqu'aux pastilles et aux vias
    const std::string depart = portee.substr(10);
    std::vector<std::string> pile{depart};
    while (!pile.empty()) {
      const std::string id = pile.back();
      pile.pop_back();
      if (!pistesVisees.insert(id).second) continue;
      const auto it = std::find_if(c.circuit.pistes.begin(), c.circuit.pistes.end(), [&](const Piste& p) { return p.uuid == id; });
      if (it == c.circuit.pistes.end()) continue;
      for (const auto& q : c.circuit.pistes)
        if (q.couche == it->couche && q.reseau == it->reseau && (q.a == it->a || q.a == it->b || q.b == it->a || q.b == it->b)) pile.push_back(q.uuid);
    }
  }
  auto vise = [&](const std::string& reseau, const std::string& id) {
    if (gardes.count(k.canonique(reseau))) return false;
    if (!pistesVisees.empty()) return pistesVisees.count(id) > 0;
    return tout || reseaux.count(k.canonique(reseau)) > 0;
  };
  int n = 0;
  n += static_cast<int>(std::erase_if(c.circuit.pistes, [&](const Piste& p) { return p.verrou == Verrou::Aucun && vise(p.reseau, p.uuid); }));
  if (pistesVisees.empty()) n += static_cast<int>(std::erase_if(c.circuit.vias, [&](const Via& v) { return v.verrou == Verrou::Aucun && v.groupe.empty() && vise(v.reseau, v.uuid); }));
  return n;
}

int appliquerReseau(ConceptionElec& c, const std::set<std::string>& objets, const std::string& reseau) {
  exigerModifiable(c);
  int n = 0;
  for (auto& p : c.circuit.pistes)
    if (objets.count(p.uuid) && p.reseau != reseau) p.reseau = reseau, ++n;
  for (auto& v : c.circuit.vias)
    if (objets.count(v.uuid) && v.reseau != reseau) v.reseau = reseau, ++n;
  for (auto& z : c.circuit.zones)
    if (objets.count(z.uuid) && z.reseau != reseau) z.reseau = reseau, ++n;
  for (auto& g : c.circuit.regions)
    if (objets.count(g.uuid) && g.reseau != reseau) g.reseau = reseau, ++n;
  return n;
}

int lisser(ConceptionElec& c, const std::set<std::string>& ids, int effort) {
  exigerModifiable(c);
  // Coin à angle droit entre deux segments d'une même piste : coupé à 45° sur une longueur qui dépend de l'effort,
  // si le raccourci reste libre à l'isolement
  auto& P = c.circuit.pistes;
  int n = 0;
  const double part = effort <= 1 ? 0.2 : effort == 2 ? 0.35 : 0.5;
  for (std::size_t i = 0; i < P.size(); ++i) {
    if (!ids.count(P[i].uuid) || P[i].verrou != Verrou::Aucun || P[i].milieu) continue;
    for (std::size_t j = 0; j < P.size(); ++j) {
      if (i == j || P[j].couche != P[i].couche || P[j].reseau != P[i].reseau || P[j].largeur != P[i].largeur || P[j].verrou != Verrou::Aucun || P[j].milieu) continue;
      if (P[i].b != P[j].a) continue;
      const Pt o = P[i].b;
      const double ax = static_cast<double>(P[i].a.x - o.x), ay = static_cast<double>(P[i].a.y - o.y), bx = static_cast<double>(P[j].b.x - o.x), by = static_cast<double>(P[j].b.y - o.y);
      const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
      if (la < 1 || lb < 1 || std::abs(ax * bx + ay * by) > 1e-6 * la * lb) continue;  // pas un angle droit
      const double d = std::min(la, lb) * part;
      const Pt pa{o.x + static_cast<Nm>(std::llround(ax / la * d)), o.y + static_cast<Nm>(std::llround(ay / la * d))};
      const Pt pb{o.x + static_cast<Nm>(std::llround(bx / lb * d)), o.y + static_cast<Nm>(std::llround(by / lb * d))};
      OptionsTrace opt;
      opt.reseau = P[i].reseau;
      opt.couche = P[i].couche;
      opt.largeur = P[i].largeur;
      opt.ignorees = {P[i].uuid, P[j].uuid};
      if (!segmentLibre(c, pa, pb, opt)) continue;
      P[i].b = pa;
      P[j].a = pb;
      P.push_back({nouvelUuid(), P[i].reseau, P[i].couche, pa, pb, P[i].largeur});
      ++n;
      break;
    }
  }
  return n;
}

int routerPlusieurs(ConceptionElec& c, const std::vector<Liaison>& liaisons, std::vector<std::string>* raisons) {
  exigerModifiable(c);
  // Les plus courtes d'abord : elles gênent moins les suivantes
  std::vector<Liaison> l = liaisons;
  std::stable_sort(l.begin(), l.end(), [](const Liaison& a, const Liaison& b) { return distance(a.a, a.b) < distance(b.a, b.b); });
  int n = 0;
  for (const auto& x : l) {
    const std::string raison = routerLiaison(c, x);
    if (raison.empty()) ++n;
    else if (raisons) raisons->push_back(x.reseau + " : " + raison);
  }
  return n;
}

// ---------- 5.16, 5.30 · Presse-papiers ----------
PressePapiersCircuit copierCircuit(const ConceptionElec& c, const std::set<std::string>& sel) {
  PressePapiersCircuit pp;
  Boite b;
  for (const auto& e : c.circuit.empreintes)
    if (sel.count(e.composant)) {
      pp.empreintes.push_back(e);
      if (const ComposantElec* x = c.composant(e.composant)) pp.composants.push_back(*x);
      if (const DefEmpreinte* d = c.empreinte(e.empreinte)) pp.definitions[e.empreinte] = *d;
      b.etendre(e.position);
    }
  // Réseau de chaque broche copiée (schéma ou carte seule) : la copie collée les reprend si l'on garde les réseaux
  if (!pp.empreintes.empty())
    for (const auto& p : pastilles(c, connectivite(c)))
      if (sel.count(p.composant) && !p.reseau.empty()) pp.reseaux[p.composant + ":" + p.numero] = p.reseau;
  for (const auto& p : c.circuit.pistes)
    if (sel.count(p.uuid)) {
      pp.pistes.push_back(p);
      b.etendre(p.a);
      b.etendre(p.b);
    }
  for (const auto& v : c.circuit.vias)
    if (sel.count(v.uuid)) {
      pp.vias.push_back(v);
      b.etendre(v.p);
    }
  for (const auto& z : c.circuit.zones)
    if (sel.count(z.uuid)) {
      pp.zones.push_back(z);
      for (const Pt& q : z.contour) b.etendre(q);
    }
  for (const auto& t : c.circuit.textes)
    if (sel.count(t.uuid)) {
      pp.textes.push_back(t);
      b.etendre(t.p);
    }
  for (const auto& d : c.circuit.dessins)
    if (sel.count(d.uuid)) {
      pp.dessins.push_back(d);
      for (const Pt& q : d.points) b.etendre(q);
    }
  pp.reference = b.vide() ? Pt{} : Pt{b.x0, b.y0};
  return pp;
}

std::set<std::string> collerCircuit(ConceptionElec& c, const PressePapiersCircuit& pp, Pt ou, const OptionsCollageCircuit& o) {
  exigerModifiable(c);
  std::set<std::string> crees;
  const Pt d = ou - pp.reference;
  std::map<std::string, std::string> nouveauxComposants;
  for (const auto& [id, def] : pp.definitions)
    if (!c.empreintes.count(id)) c.empreintes[id] = def;
  for (const auto& x : pp.composants) {
    ComposantElec y = x;
    if (o.composantsNeufs) {
      y.uuid = nouvelUuid();
      std::string pre;
      for (char ch : x.reference) {
        if (std::isdigit(static_cast<unsigned char>(ch))) break;
        pre += ch;
      }
      y.reference = prochainRepere(c, pre.empty() ? std::string("U") : pre);
      y.carteSeule = true;
    }
    nouveauxComposants[x.uuid] = y.uuid;
    if (!c.composant(y.uuid)) c.composants.push_back(y);
    if (o.composantsNeufs && o.garderReseaux)
      for (const auto& [cle, r] : pp.reseaux)
        if (cle.rfind(x.uuid + ":", 0) == 0) c.nomsReseaux[y.uuid + cle.substr(x.uuid.size())] = r;
  }
  auto reseau = [&](const std::string& r) -> std::string {
    if (!o.garderReseaux) return {};
    if (!o.classe.empty() && !r.empty()) c.circuit.regles.classeReseau[r] = o.classe;
    return r;
  };
  for (auto e : pp.empreintes) {
    e.composant = nouveauxComposants.count(e.composant) ? nouveauxComposants[e.composant] : e.composant;
    e.position = e.position + d;
    if (std::none_of(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& x) { return x.composant == e.composant; })) {
      c.circuit.empreintes.push_back(e);
      crees.insert(e.composant);
    }
  }
  for (auto p : pp.pistes) {
    p.uuid = nouvelUuid();
    p.a = p.a + d;
    p.b = p.b + d;
    if (p.milieu) p.milieu = *p.milieu + d;
    p.reseau = reseau(p.reseau);
    crees.insert(p.uuid);
    c.circuit.pistes.push_back(p);
  }
  for (auto v : pp.vias) {
    v.uuid = nouvelUuid();
    v.p = v.p + d;
    v.reseau = reseau(v.reseau);
    crees.insert(v.uuid);
    c.circuit.vias.push_back(v);
  }
  for (auto z : pp.zones) {
    z.uuid = nouvelUuid();
    for (Pt& q : z.contour) q = q + d;
    z.reseau = reseau(z.reseau);
    z.remplissageFige.clear();
    z.etat = EtatZone::Normale;
    crees.insert(z.uuid);
    c.circuit.zones.push_back(z);
  }
  for (auto t : pp.textes) {
    t.uuid = nouvelUuid();
    t.p = t.p + d;
    crees.insert(t.uuid);
    c.circuit.textes.push_back(t);
  }
  for (auto x : pp.dessins) {
    x.uuid = nouvelUuid();
    for (Pt& q : x.points) q = q + d;
    crees.insert(x.uuid);
    c.circuit.dessins.push_back(x);
  }
  return crees;
}

nlohmann::ordered_json presseCircuitVersJson(const PressePapiersCircuit& pp) {
  // Le presse-papiers voyage comme une petite conception : le format du projet le décrit déjà en entier
  ConceptionElec c;
  c.nom = "presse-papiers";
  c.composants = pp.composants;
  c.empreintes = pp.definitions;
  c.circuit.empreintes = pp.empreintes;
  c.circuit.pistes = pp.pistes;
  c.circuit.vias = pp.vias;
  c.circuit.zones = pp.zones;
  c.circuit.textes = pp.textes;
  c.circuit.dessins = pp.dessins;
  c.nomsReseaux = pp.reseaux;
  nlohmann::ordered_json j;
  j["promethee"] = "presse-papiers-circuit";
  j["reference"] = {pp.reference.x, pp.reference.y};
  j["conception"] = versJson(c);
  return j;
}

PressePapiersCircuit presseCircuitDepuisJson(const nlohmann::ordered_json& j) {
  PressePapiersCircuit pp;
  if (!j.is_object() || j.value("promethee", "") != "presse-papiers-circuit" || !j.contains("conception")) return pp;
  const ConceptionElec c = conceptionDepuisJson(j["conception"]);
  pp.composants = c.composants;
  pp.definitions = c.empreintes;
  pp.empreintes = c.circuit.empreintes;
  pp.pistes = c.circuit.pistes;
  pp.vias = c.circuit.vias;
  pp.zones = c.circuit.zones;
  pp.textes = c.circuit.textes;
  pp.dessins = c.circuit.dessins;
  pp.reseaux = c.nomsReseaux;
  if (j.contains("reference") && j["reference"].is_array() && j["reference"].size() == 2) pp.reference = {j["reference"][0].get<Nm>(), j["reference"][1].get<Nm>()};
  return pp;
}

// ---------- 5.17 ----------
std::string poserEmpreinteSeule(ConceptionElec& c, const std::string& id, Pt p, double rotation, bool dessous, const std::string& prefixeVoulu) {
  exigerModifiable(c);
  const DefEmpreinte* d = c.empreinte(id);
  if (!d) {
    if (const auto g = genererEmpreinte(id)) c.empreintes[id] = *g;
    d = c.empreinte(id);
  }
  if (!d) return {};
  std::string prefixe = prefixeVoulu;
  if (prefixe.empty()) {
    // Préfixe d'après l'identifiant de l'empreinte (conventions de nommage de KiCad), « U » à défaut
    static const std::pair<const char*, const char*> prefixes[] = {
        {"R_", "R"}, {"C_", "C"}, {"CP_", "C"}, {"L_", "L"}, {"D_", "D"}, {"LED", "D"}, {"MountingHole", "H"}, {"TestPoint", "TP"}, {"Fiducial", "FID"},
        {"PinHeader", "J"}, {"PinSocket", "J"}, {"Conn", "J"}, {"USB", "J"}, {"SW", "SW"}, {"Crystal", "Y"}, {"Fuse", "F"}, {"NetTie", "NT"}, {"SOT-23", "Q"}};
    for (const auto& [debut, pre] : prefixes)
      if (prefixe.empty() && id.rfind(debut, 0) == 0) prefixe = pre;
    if (prefixe.empty()) prefixe = familleBoitier(id) == "barrette" ? "J" : "U";
  }
  ComposantElec x;
  x.uuid = nouvelUuid();
  x.reference = prochainRepere(c, prefixe);
  x.empreinte = id;
  x.carteSeule = true;
  c.composants.push_back(x);
  EmpreintePlacee e;
  e.composant = x.uuid;
  e.empreinte = id;
  e.position = p;
  e.rotation = rotation;
  e.dessous = dessous;
  c.circuit.empreintes.push_back(e);
  figerEmpreintes(c);
  return x.uuid;
}

std::string reseauParPiste(ConceptionElec& c, const std::string& ca, const std::string& na, const std::string& cb, const std::string& nb) {
  exigerModifiable(c);
  const Connectivite k = connectivite(c);
  auto reseauDe = [&](const std::string& comp, const std::string& num) -> std::string {
    const std::string schema = k.nom(comp, num);
    if (!schema.empty()) return schema;
    const auto it = c.nomsReseaux.find(comp + ":" + num);
    return it == c.nomsReseaux.end() ? std::string() : it->second;
  };
  const std::string ra = reseauDe(ca, na), rb = reseauDe(cb, nb);
  const ComposantElec* xa = c.composant(ca);
  std::string nom = !ra.empty() ? ra : !rb.empty() ? rb : "N-" + (xa ? xa->reference : std::string("?")) + "-" + na;
  // Le réseau de l'autre rejoint celui-ci : toutes ses broches de la carte seule, et son cuivre
  const std::string autre = nom == ra ? rb : ra;
  for (auto& [cle, r] : c.nomsReseaux)
    if (!autre.empty() && r == autre) {
      const std::string comp = cle.substr(0, cle.find(':'));
      if (const ComposantElec* x = c.composant(comp); x && x->carteSeule) r = nom;
    }
  for (const auto& [comp, num] : {std::pair{ca, na}, std::pair{cb, nb}})
    if (const ComposantElec* x = c.composant(comp); x && x->carteSeule) c.nomsReseaux[comp + ":" + num] = nom;
  if (!autre.empty() && autre != nom) {
    for (auto& p : c.circuit.pistes)
      if (p.reseau == autre) p.reseau = nom;
    for (auto& v : c.circuit.vias)
      if (v.reseau == autre) v.reseau = nom;
    for (auto& z : c.circuit.zones)
      if (z.reseau == autre) z.reseau = nom;
  }
  return nom;
}

bool echangerEmpreintes(ConceptionElec& c, const std::string& a, const std::string& b) {
  exigerModifiable(c);
  auto& E = c.circuit.empreintes;
  auto ia = std::find_if(E.begin(), E.end(), [&](const EmpreintePlacee& e) { return e.composant == a; });
  auto ib = std::find_if(E.begin(), E.end(), [&](const EmpreintePlacee& e) { return e.composant == b; });
  if (ia == E.end() || ib == E.end() || ia->verrou || ib->verrou) return false;
  std::swap(ia->position, ib->position);
  std::swap(ia->rotation, ib->rotation);
  std::swap(ia->dessous, ib->dessous);
  return true;
}

int serrerEmpreintes(ConceptionElec& c, const std::vector<std::string>& composants, Nm ecart) {
  exigerModifiable(c);
  // Chaque composant avance vers le centre commun par petits pas, tant qu'il ne recouvre aucun autre
  std::vector<EmpreintePlacee*> v;
  Boite b;
  for (auto& e : c.circuit.empreintes)
    if (std::find(composants.begin(), composants.end(), e.composant) != composants.end() && !e.verrou) {
      v.push_back(&e);
      b.etendre(e.position);
    }
  if (v.size() < 2) return 0;
  const Pt centre = b.centre();
  std::sort(v.begin(), v.end(), [&](const EmpreintePlacee* x, const EmpreintePlacee* y) { return distance(x->position, centre) < distance(y->position, centre); });
  int bouges = 0;
  for (EmpreintePlacee* e : v) {
    const DefEmpreinte* d = c.empreinte(e->empreinte);
    if (!d) continue;
    bool bouge = false;
    for (int pas = 0; pas < 400; ++pas) {
      const double L = distance(e->position, centre);
      if (L < 50'000) break;
      const Pt essai{e->position.x + static_cast<Nm>(std::llround(static_cast<double>(centre.x - e->position.x) / L * 100'000)),
                     e->position.y + static_cast<Nm>(std::llround(static_cast<double>(centre.y - e->position.y) / L * 100'000))};
      const Pt avant = e->position;
      e->position = essai;
      const Polygone enc = gonfler({encombrementSurCarte(*e, *d)}, ecart).front();
      bool libre = true;
      for (const auto& f : c.circuit.empreintes) {
        if (&f == e || f.dessous != e->dessous) continue;
        const DefEmpreinte* g = c.empreinte(f.empreinte);
        if (!g) continue;
        const Polygones commun = intersecter({enc}, {encombrementSurCarte(f, *g)});
        if (!commun.empty() && aireMm2(commun) > 1e-6) {
          libre = false;
          break;
        }
      }
      if (!libre) {
        e->position = avant;
        break;
      }
      bouge = true;
    }
    bouges += bouge ? 1 : 0;
  }
  return bouges;
}

std::set<std::string> selectionLogique(const ConceptionElec& c, const std::string& critere, const std::string& valeur) {
  std::set<std::string> r;
  if (critere == "reseau") {
    const Connectivite k = connectivite(c);
    const std::string n = k.canonique(valeur);
    for (const auto& p : c.circuit.pistes)
      if (k.canonique(p.reseau) == n) r.insert(p.uuid);
    for (const auto& v : c.circuit.vias)
      if (k.canonique(v.reseau) == n) r.insert(v.uuid);
    for (const auto& z : c.circuit.zones)
      if (k.canonique(z.reseau) == n) r.insert(z.uuid);
    for (const auto& p : pastilles(c, k))
      if (p.reseau == n) r.insert(p.composant);
  } else if (critere == "feuille") {
    for (const auto& f : c.feuilles)
      if (f.uuid == valeur || f.nom == valeur)
        for (const auto& s : f.symboles)
          if (!s.composant.empty()) r.insert(s.composant);
  } else if (critere == "classe") {
    for (const auto& k : c.circuit.regles.avancees.classesComposants)
      if (k.nom == valeur)
        for (const auto& x : c.composants)
          if (std::find(k.membres.begin(), k.membres.end(), x.reference) != k.membres.end()) r.insert(x.uuid);
    const Connectivite kk = connectivite(c);
    for (const auto& p : c.circuit.pistes)
      if (const ClasseReseau* cl = classeDe(c.circuit.regles, kk.canonique(p.reseau)); cl && cl->nom == valeur) r.insert(p.uuid);
  } else if (critere == "valeur" || critere == "empreinte") {
    for (const auto& x : c.composants)
      if ((critere == "valeur" ? x.valeur : x.empreinte) == valeur) r.insert(x.uuid);
  } else if (critere == "groupe") {
    for (const auto& g : c.circuit.groupes)
      if (g.uuid == valeur || g.nom == valeur) r.insert(g.membres.begin(), g.membres.end());
  } else if (critere == "composant") {
    r.insert(valeur);
    const Connectivite k = connectivite(c);
    std::set<std::string> reseaux;
    for (const auto& p : pastilles(c, k))
      if (p.composant == valeur && !p.reseau.empty()) reseaux.insert(p.reseau);
    for (const auto& p : c.circuit.pistes)
      if (reseaux.count(k.canonique(p.reseau))) r.insert(p.uuid);
  }
  return r;
}

// ---------- 5.19 ----------
std::vector<std::vector<std::string>> lireTableau(const std::string& texte) {
  std::vector<std::vector<std::string>> r;
  std::istringstream in(texte);
  std::string ligne;
  char sep = 0;
  while (std::getline(in, ligne)) {
    if (!ligne.empty() && ligne.back() == '\r') ligne.pop_back();
    if (ligne.rfind("\xEF\xBB\xBF", 0) == 0) ligne = ligne.substr(3);
    if (ligne.empty() || ligne[0] == '#') continue;
    if (!sep) sep = ligne.find(';') != std::string::npos ? ';' : ligne.find('\t') != std::string::npos ? '\t' : ligne.find(',') != std::string::npos ? ',' : ' ';
    std::vector<std::string> champs;
    std::string champ;
    bool guillemets = false;
    for (std::size_t i = 0; i < ligne.size(); ++i) {
      const char ch = ligne[i];
      if (ch == '"') {
        if (guillemets && i + 1 < ligne.size() && ligne[i + 1] == '"') {
          champ += '"';
          ++i;
        } else guillemets = !guillemets;
      } else if (ch == sep && !guillemets) {
        if (sep != ' ' || !champ.empty()) champs.push_back(champ);
        champ.clear();
      } else champ += ch;
    }
    if (sep != ' ' || !champ.empty()) champs.push_back(champ);
    for (auto& x : champs) {
      while (!x.empty() && std::isspace(static_cast<unsigned char>(x.back()))) x.pop_back();
      while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) x.erase(0, 1);
    }
    r.push_back(champs);
  }
  return r;
}

std::vector<std::vector<std::string>> lireXlsx(const std::string& octets) {
  // Classeur Office Open XML : les chaînes partagées, puis la première feuille ; cellules « r="B3" » rangées à leur
  // colonne, valeurs « t="s" » lues dans la table des chaînes, « inlineStr » lues sur place
  std::vector<std::vector<std::string>> r;
  std::vector<std::pair<std::string, std::string>> fichiers;
  try {
    fichiers = lireZip(octets);
  } catch (const std::exception&) {
    return r;
  }
  std::string chaines, feuille;
  for (const auto& [nom, contenu] : fichiers) {
    if (nom == "xl/sharedStrings.xml") chaines = contenu;
    if (nom == "xl/worksheets/sheet1.xml") feuille = contenu;
  }
  auto texteXml = [](std::string s) {
    std::string r2;
    for (std::size_t i = 0; i < s.size(); ++i) {
      if (s[i] == '&') {
        const auto f = s.find(';', i);
        const std::string e = s.substr(i, f - i + 1);
        r2 += e == "&amp;" ? "&" : e == "&lt;" ? "<" : e == "&gt;" ? ">" : e == "&quot;" ? "\"" : e == "&apos;" ? "'" : e;
        i = f;
      } else r2 += s[i];
    }
    return r2;
  };
  std::vector<std::string> partagees;
  for (std::size_t i = chaines.find("<si>"); i != std::string::npos; i = chaines.find("<si>", i + 1)) {
    const auto fin = chaines.find("</si>", i);
    std::string si = chaines.substr(i, fin - i), t;
    for (std::size_t j = si.find("<t"); j != std::string::npos; j = si.find("<t", j + 1)) {
      if (si.compare(j, 3, "<t>") != 0 && si.compare(j, 3, "<t ") != 0) continue;
      const auto debut = si.find('>', j) + 1;
      t += si.substr(debut, si.find("</t>", debut) - debut);
    }
    partagees.push_back(texteXml(t));
  }
  for (std::size_t i = feuille.find("<row"); i != std::string::npos; i = feuille.find("<row", i + 1)) {
    const auto fin = feuille.find("</row>", i);
    if (fin == std::string::npos) break;
    const std::string ligne = feuille.substr(i, fin - i);
    std::vector<std::string> cellules;
    for (std::size_t j = ligne.find("<c "); j != std::string::npos; j = ligne.find("<c ", j + 1)) {
      const auto finBalise = ligne.find('>', j);
      const std::string balise = ligne.substr(j, finBalise - j);
      const auto rr = balise.find("r=\"");
      int col = static_cast<int>(cellules.size());
      if (rr != std::string::npos) {
        col = 0;
        for (std::size_t q = rr + 3; q < balise.size() && std::isalpha(static_cast<unsigned char>(balise[q])); ++q) col = col * 26 + (std::toupper(static_cast<unsigned char>(balise[q])) - 'A' + 1);
        --col;
      }
      std::string v;
      const bool vide = balise.back() == '/';
      if (!vide) {
        const auto finCellule = ligne.find("</c>", finBalise);
        const std::string corps = ligne.substr(finBalise + 1, finCellule - finBalise - 1);
        if (balise.find("t=\"inlineStr\"") != std::string::npos) {
          const auto t0 = corps.find("<t");
          if (t0 != std::string::npos) {
            const auto d = corps.find('>', t0) + 1;
            v = texteXml(corps.substr(d, corps.find("</t>", d) - d));
          }
        } else {
          const auto v0 = corps.find("<v>");
          if (v0 != std::string::npos) v = corps.substr(v0 + 3, corps.find("</v>", v0) - v0 - 3);
          if (balise.find("t=\"s\"") != std::string::npos) {
            const std::size_t n = static_cast<std::size_t>(std::atoi(v.c_str()));
            v = n < partagees.size() ? partagees[n] : std::string();
          } else v = texteXml(v);
        }
      }
      if (col >= 0) {
        if (static_cast<std::size_t>(col) >= cellules.size()) cellules.resize(static_cast<std::size_t>(col) + 1);
        cellules[static_cast<std::size_t>(col)] = v;
      }
    }
    r.push_back(cellules);
  }
  return r;
}

BilanImportPlacement importerPlacement(ConceptionElec& c, const std::string& contenu, bool unitesPouces) {
  exigerModifiable(c);
  BilanImportPlacement b;
  const bool xlsx = contenu.size() > 4 && contenu.compare(0, 2, "PK") == 0;
  const auto lignes = xlsx ? lireXlsx(contenu) : lireTableau(contenu);
  if (lignes.empty()) {
    b.journal.push_back("fichier vide ou illisible");
    return b;
  }
  // En-têtes reconnus : KiCad (Ref, PosX, PosY, Rot, Side), Altium (Designator, Mid X/Center-X(mm), Mid Y,
  // Rotation, Layer), PADS (Name, X, Y, Orient., Layer/Side), Allegro (REFDES, SYM_X, SYM_Y, SYM_ROTATE, SYM_MIRROR),
  // Prométhée (Ref, PosX, PosY, Rot, Face)
  auto minus = [](std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s;
  };
  std::size_t entete = 0;
  int cRef = -1, cX = -1, cY = -1, cRot = -1, cFace = -1;
  for (std::size_t li = 0; li < std::min<std::size_t>(lignes.size(), 20) && cRef < 0; ++li) {
    const auto& l = lignes[li];
    for (std::size_t i = 0; i < l.size(); ++i) {
      const std::string h = minus(l[i]);
      if (h == "ref" || h == "designator" || h == "refdes" || h == "name" || h == "reference" || h == "repère" || h == "repere") cRef = static_cast<int>(i);
      else if (h == "posx" || h == "midx" || h == "center-x(mm)" || h == "centerx" || h == "x" || h == "sym_x" || h == "locationx" || h == "ref-x(mm)") cX = static_cast<int>(i);
      else if (h == "posy" || h == "midy" || h == "center-y(mm)" || h == "centery" || h == "y" || h == "sym_y" || h == "locationy" || h == "ref-y(mm)") cY = static_cast<int>(i);
      else if (h == "rot" || h == "rotation" || h == "orient." || h == "orientation" || h == "sym_rotate" || h == "angle") cRot = static_cast<int>(i);
      else if (h == "side" || h == "layer" || h == "face" || h == "sym_mirror" || h == "tb" || h == "mirror") cFace = static_cast<int>(i);
    }
    if (cRef >= 0) entete = li;
  }
  if (cRef < 0 || cX < 0 || cY < 0) {
    b.journal.push_back("en-têtes non reconnus : il faut au moins un repère et deux coordonnées");
    return b;
  }
  const Boite carte = repereFichiers(c);  // même repère que le fichier de placement écrit (5.31)
  for (std::size_t li = entete + 1; li < lignes.size(); ++li) {
    const auto& l = lignes[li];
    auto champ = [&](int i) { return i >= 0 && static_cast<std::size_t>(i) < l.size() ? l[static_cast<std::size_t>(i)] : std::string(); };
    const std::string ref = champ(cRef);
    if (ref.empty()) continue;
    const ComposantElec* x = c.parReference(ref);
    if (!x) {
      ++b.inconnus;
      b.journal.push_back(ref + " : inconnu de la carte, ligne ignorée");
      continue;
    }
    auto nombre = [&](std::string s) {
      std::string t;
      for (char ch : s)
        if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.' || ch == ',') t += ch == ',' ? '.' : ch;
      return t.empty() ? 0.0 : std::atof(t.c_str());
    };
    double X = nombre(champ(cX)), Y = nombre(champ(cY));
    const std::string sx = minus(champ(cX));
    const bool mil = sx.find("mil") != std::string::npos, pouces = unitesPouces || sx.find("in") != std::string::npos;
    if (mil) X *= 0.0254, Y *= 0.0254;
    else if (pouces) X *= 25.4, Y *= 25.4;
    const std::string face = minus(champ(cFace));
    const bool dessous = face == "bottom" || face == "b" || face == "dessous" || face == "bot" || face == "yes" || face == "m";
    // Repère des fichiers de placement : origine au coin bas gauche de la carte, y vers le haut
    auto it = std::find_if(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == x->uuid; });
    if (it == c.circuit.empreintes.end()) {
      b.journal.push_back(ref + " : sans empreinte sur la carte, ligne ignorée");
      ++b.inconnus;
      continue;
    }
    if (it->verrou) {
      b.journal.push_back(ref + " : verrouillé, gardé à sa place");
      continue;
    }
    const DefEmpreinte* d = c.empreinte(it->empreinte);
    it->dessous = dessous;
    it->rotation = nombre(champ(cRot));
    const Pt voulu = depuisRepereFabrication(carte, ptMm(X, Y));
    // La position lue est le point de placement (centre des pastilles), pas l'origine de l'empreinte
    const Pt decalage = d ? versCarte(*it, pointDePlacement(*d)) - it->position : Pt{};
    it->position = voulu - decalage;
    ++b.places;
  }
  b.journal.insert(b.journal.begin(), std::to_string(b.places) + " composant(s) placé(s), " + std::to_string(b.inconnus) + " ligne(s) sans composant");
  return b;
}

int placerReperes(ConceptionElec& c, const std::set<std::string>& composants, int position) {
  exigerModifiable(c);
  int n = 0;
  const Nm h = c.circuit.serigraphie.hauteur;
  for (auto& e : c.circuit.empreintes) {
    if (!composants.empty() && !composants.count(e.composant)) continue;
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!d) continue;
    const Boite b = d->encombrement;
    const Nm marge = h / 2 + 200'000;
    if (position >= 1 && position <= 9) {
      // Pavé numérique : 7 8 9 en haut, 4 5 6 au milieu, 1 2 3 en bas (repère de l'empreinte, y vers le bas)
      const int col = (position - 1) % 3, lig = 2 - (position - 1) / 3;
      const Nm x = col == 0 ? b.x0 - marge * 2 : col == 1 ? (b.x0 + b.x1) / 2 : b.x1 + marge * 2;
      const Nm y = lig == 0 ? b.y0 - marge : lig == 1 ? (b.y0 + b.y1) / 2 : b.y1 + marge;
      e.repere = Pt{x, y};
      ++n;
      continue;
    }
    // Spirale autour du corps : la première place qui ne touche aucune pastille ni aucun autre encombrement
    const Pt centre = b.centre();
    const Nm rayon0 = std::max(b.largeur(), b.hauteur()) / 2 + marge;
    bool pose = false;
    for (int tour = 0; tour < 6 && !pose; ++tour)
      for (int k = 0; k < 16 && !pose; ++k) {
        const double a = -std::numbers::pi / 2 + 2 * std::numbers::pi * k / 16;
        const Nm r = rayon0 + tour * h;
        const Pt local{centre.x + static_cast<Nm>(std::llround(std::cos(a) * static_cast<double>(r))), centre.y + static_cast<Nm>(std::llround(std::sin(a) * static_cast<double>(r)))};
        const Pt surCarte = versCarte(e, local);
        const Polygone zoneTexte = rectangle(surCarte, h * 3, h + 100'000);
        bool libre = true;
        for (const auto& f : c.circuit.empreintes) {
          const DefEmpreinte* g = c.empreinte(f.empreinte);
          if (!g || f.dessous != e.dessous) continue;
          if (!intersecter({zoneTexte}, {encombrementSurCarte(f, *g)}).empty()) {
            libre = false;
            break;
          }
        }
        if (libre) {
          e.repere = local;
          pose = true;
          ++n;
        }
      }
  }
  return n;
}

int modifierTextes(ConceptionElec& c, const std::set<std::string>& textes, std::optional<Nm> hauteur, std::optional<Couche> couche) {
  exigerModifiable(c);
  int n = 0;
  for (auto& t : c.circuit.textes) {
    if (!textes.empty() && !textes.count(t.uuid)) continue;
    if (hauteur) t.hauteur = *hauteur;
    if (couche) t.couche = *couche;
    ++n;
  }
  // Repères des composants (« reperes » dans la liste, ou liste vide) : la hauteur de sérigraphie commune
  if (hauteur && (textes.empty() || textes.count("reperes"))) c.circuit.serigraphie.hauteur = *hauteur;
  return n;
}

int echangerCouches(ConceptionElec& c, const std::map<Couche, Couche>& table) {
  exigerModifiable(c);
  int n = 0;
  auto conv = [&](Couche& k) {
    const auto it = table.find(k);
    if (it != table.end()) {
      k = it->second;
      ++n;
    }
  };
  for (auto& p : c.circuit.pistes) conv(p.couche);
  for (auto& z : c.circuit.zones) conv(z.couche);
  for (auto& t : c.circuit.textes) conv(t.couche);
  for (auto& d : c.circuit.dessins) conv(d.couche);
  for (auto& g : c.circuit.regions) conv(g.couche);
  for (auto& v : c.circuit.vias) {
    conv(v.de);
    conv(v.vers);
  }
  return n;
}

// ---------- 5.32 ----------
void reprendreReglages(ConceptionElec& c, const ConceptionElec& s, const SectionsReglages& o) {
  exigerModifiable(c);
  Regles& R = c.circuit.regles;
  const Regles& S = s.circuit.regles;
  if (o.regles) {
    R.isolement = S.isolement;
    R.largeur = S.largeur;
    R.largeurMin = S.largeurMin;
    R.viaDiametre = S.viaDiametre;
    R.viaPercage = S.viaPercage;
    R.anneauMin = S.anneauMin;
    R.percageMin = S.percageMin;
    R.bord = S.bord;
    R.ecartTrous = S.ecartTrous;
    R.cuivreTrou = S.cuivreTrou;
    R.rayonsMin = S.rayonsMin;
    R.profil = S.profil;
    R.capacitesPerso = S.capacitesPerso;
  }
  if (o.classes) {
    R.classes = S.classes;
    R.classeReseau = S.classeReseau;
  }
  if (o.contraintes) R.avancees = S.avancees;
  if (o.empilage) c.circuit.empilage = s.circuit.empilage;
  if (o.couches) {
    c.circuit.affichage.couchesVisibles = s.circuit.affichage.couchesVisibles;
    c.circuit.affichage.nomsCouches = s.circuit.affichage.nomsCouches;
  }
  if (o.vues) {
    c.circuit.affichage.jeux = s.circuit.affichage.jeux;
    c.circuit.affichage.couleursReseaux = s.circuit.affichage.couleursReseaux;
    c.circuit.affichage.unite = s.circuit.affichage.unite;
    c.circuit.affichage.grille = s.circuit.affichage.grille;
  }
  if (o.fabrication) c.circuit.fabrication = s.circuit.fabrication;
  if (o.serigraphie) c.circuit.serigraphie = s.circuit.serigraphie;
}

}  // namespace prom::ecad
