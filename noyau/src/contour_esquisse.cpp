// Prométhée : contour de la carte piloté par une esquisse (lot L6). Licence GPL-3.0-only.
#include "promethee/contour_esquisse.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

#include "promethee/hauteurs.hpp"

namespace prom {

namespace {
using namespace ecad;
// Centre du perçage d'un trou de fixation posé (repère de la carte)
std::optional<Pt> centreTrou(const ConceptionElec& c, const EmpreintePlacee& e) {
  const DefEmpreinte* d = c.empreinte(e.empreinte);
  if (!d || !trouDeFixation(*d)) return std::nullopt;
  for (const auto& p : d->pastilles)
    if (p.percage >= 2'000'000) return versCarte(e, p.position);
  return std::nullopt;
}
}  // namespace

Esquisse esquisseDeCarte(const ConceptionElec& c) {
  Esquisse e;
  const Boite b = boite(c.circuit.contour.polygone());
  const double w = mm(b.largeur()), h = mm(b.hauteur());
  const double ox = mm(b.x0), oy = mm(b.y0);
  // Repère de l'esquisse : origine au coin haut gauche de la carte, v vers le haut (la carte descend en v négatif)
  const std::vector<int> l = e.ajouterRectangle(0, 0, w, -h);
  const int a = e.element(l[0])->p1, bd = e.element(l[0])->p2, cb = e.element(l[1])->p2, db = e.element(l[2])->p2;
  e.contraindre(TypeContrainte::Coincidence, {a, 0});
  e.contraindre(TypeContrainte::Longueur, {l[0]}, w);
  e.contraindre(TypeContrainte::Longueur, {l[1]}, h);
  for (const auto& x : c.circuit.empreintes) {
    const auto t = centreTrou(c, x);
    if (!t) continue;
    const DefEmpreinte* d = c.empreinte(x.empreinte);
    double percage = 3.2;
    for (const auto& p : d->pastilles)
      if (p.percage >= 2'000'000) percage = mm(p.percage);
    const double u = mm(t->x) - ox, v = -(mm(t->y) - oy);
    const int centre = e.ajouterPoint(u, v);
    const int cercle = e.ajouterCercle(centre, percage / 2);
    e.contraindre(TypeContrainte::Diametre, {cercle}, percage);
    // Coté depuis le coin le plus proche : le trou suit son bord quand la carte grandit
    const bool droite = u > w / 2, bas = v < -h / 2;
    const int coin = droite ? (bas ? cb : bd) : (bas ? db : a);
    const PointEsquisse* pc = e.point(coin);
    e.contraindre(TypeContrainte::DistanceX, {coin, centre}, u - pc->x);
    e.contraindre(TypeContrainte::DistanceY, {coin, centre}, v - pc->y);
  }
  resoudre(e);
  return e;
}

std::vector<std::string> appliquerEsquisseContour(ConceptionElec& c) {
  std::vector<std::string> remarques;
  if (c.esquisseContour.is_null()) return remarques;
  exigerModifiable(c);
  Esquisse e = esquisseDepuisJson(c.esquisseContour);
  const EtatEsquisse etat = resoudre(e);
  if (!etat.resolue) remarques.push_back("l’esquisse du contour ne se résout pas : " + etat.message);
  c.esquisseContour = esquisseVersJson(e);
  auto versCarte = [](const Point& q) { return Pt{nm(q.x), nm(-q.y)}; };
  // Cercles : les trous de fixation ; les autres boucles fermées : le contour (la plus grande) et les découpes
  std::vector<Pt> cercles;
  std::set<int> elementsCercles;
  for (const auto& el : e.elements)
    if (el.type == TypeElement::Cercle && !el.construction)
      if (const PointEsquisse* p = e.point(el.centre)) {
        cercles.push_back(versCarte({p->x, p->y}));
        elementsCercles.insert(el.id);
      }
  std::string erreur;
  std::vector<Boucle> bs = boucles(e, &erreur);
  std::erase_if(bs, [&](const Boucle& b) { return b.elements.size() == 1 && elementsCercles.count(b.elements.front().first); });
  if (bs.empty()) {
    remarques.push_back("l’esquisse n’a pas de boucle fermée pour le contour" + (erreur.empty() ? std::string() : " : " + erreur));
    return remarques;
  }
  std::sort(bs.begin(), bs.end(), [](const Boucle& x, const Boucle& y) { return std::abs(x.aire) > std::abs(y.aire); });
  Polygone exterieur;
  for (const Point& q : bs.front().polygone) exterieur.push_back(versCarte(q));
  c.circuit.contour.libre = orienter(exterieur);
  const Boite be = boite(c.circuit.contour.libre);
  c.circuit.contour.origine = {be.x0, be.y0};
  c.circuit.contour.largeur = be.largeur();
  c.circuit.contour.hauteur = be.hauteur();
  c.circuit.contour.decoupes.clear();
  for (std::size_t i = 1; i < bs.size(); ++i) {
    Polygone d;
    for (const Point& q : bs[i].polygone) d.push_back(versCarte(q));
    c.circuit.contour.decoupes.push_back(d);
  }
  // Chaque trou de fixation rejoint le cercle libre le plus proche
  std::vector<bool> pris(cercles.size(), false);
  for (auto& x : c.circuit.empreintes) {
    const auto t = centreTrou(c, x);
    if (!t) continue;
    std::size_t meilleur = cercles.size();
    double dmin = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < cercles.size(); ++i)
      if (!pris[i] && distance(*t, cercles[i]) < dmin) {
        dmin = distance(*t, cercles[i]);
        meilleur = i;
      }
    if (meilleur == cercles.size()) {
      remarques.push_back("un trou de fixation n’a pas de cercle dans l’esquisse : il reste en place");
      continue;
    }
    pris[meilleur] = true;
    x.position = x.position + (cercles[meilleur] - *t);
  }
  const auto libres = std::count(pris.begin(), pris.end(), false);
  if (libres > 0) remarques.push_back(std::to_string(libres) + " cercle(s) de l’esquisse sans trou de fixation");
  return remarques;
}

std::vector<CoteContour> cotesContour(const ConceptionElec& c) {
  std::vector<CoteContour> r;
  if (c.esquisseContour.is_null()) return r;
  const Esquisse e = esquisseDepuisJson(c.esquisseContour);
  for (const auto& x : e.contraintes) {
    if (!estCote(x.type) || x.reference) continue;
    std::string nom = nomContrainte(x.type);
    char t[64];
    std::snprintf(t, sizeof t, " %.3g mm", x.valeur);
    r.push_back({x.id, nom + t, x.valeur});
  }
  return r;
}

bool changerCoteContour(ConceptionElec& c, int contrainte, double valeur, std::vector<std::string>* remarques) {
  if (c.esquisseContour.is_null()) return false;
  Esquisse e = esquisseDepuisJson(c.esquisseContour);
  bool trouve = false;
  for (auto& x : e.contraintes)
    if (x.id == contrainte) {
      x.valeur = valeur;
      trouve = true;
    }
  if (!trouve) return false;
  c.esquisseContour = esquisseVersJson(e);
  const auto r = appliquerEsquisseContour(c);
  if (remarques) *remarques = r;
  return true;
}

}  // namespace prom
