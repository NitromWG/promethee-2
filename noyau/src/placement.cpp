// Prométhée : ajout d'éléments à une place libre. Port du prototype web. Licence GPL-3.0-only.
#include "promethee/placement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>

#include "promethee/derive.hpp"
#include "promethee/verifs.hpp"

namespace prom {

namespace {

const std::set<std::string>& codesPositionnels() {
  static const std::set<std::string> c = {"hors_carte", "bord_hors", "chevauchement", "tete_vis", "trou_couvercle", "percages", "decoupe_angle",
                                          "decoupes", "levre", "connecteur_porte", "trou_hors", "trou_bord", "pilier_paroi", "piliers"};
  return c;
}

// Nombre d'erreurs de position qui concernent l'élément.
int gene(const Projet& p, const std::string& id) {
  int n = 0;
  for (const auto& q : verifier(p, deriver(p)))
    if (q.sev == Severite::Erreur && codesPositionnels().count(q.code) && std::find(q.ids.begin(), q.ids.end(), id) != q.ids.end()) ++n;
  return n;
}

std::vector<std::string> identifiants(const Projet& p) {
  std::vector<std::string> ids;
  for (const auto& k : p.composants) ids.push_back(k.id);
  for (const auto& t : p.trous) ids.push_back(t.id);
  return ids;
}

void placeLibre(Projet& p, size_t indice) {
  const Carte c = p.carte;
  int meilleur = std::numeric_limits<int>::max();
  double bx = c.x0, by = c.y0;
  for (double r = 0; r < std::max(c.L, c.W); r += 1) {
    const int n = r == 0 ? 1 : std::max(8, static_cast<int>(arrondiJs(r * 2)));
    for (int a = 0; a < n; ++a) {
      const double ang = r == 0 ? 0 : 2 * std::numbers::pi * a / n;
      Composant& k = p.composants[indice];
      k.x = arrondiJs((c.x0 + r * std::cos(ang)) * 2) / 2;
      k.y = arrondiJs((c.y0 + r * std::sin(ang)) * 2) / 2;
      const int g = gene(p, k.id);
      if (g == 0) return;
      if (g < meilleur) { meilleur = g; bx = k.x; by = k.y; }
    }
  }
  p.composants[indice].x = bx;
  p.composants[indice].y = by;
}

void bordLibre(Projet& p, size_t indice) {
  const Carte c = p.carte;
  int meilleur = std::numeric_limits<int>::max();
  Bord bb = Bord::S;
  double bu = c.x0;
  for (Bord b : {Bord::S, Bord::E, Bord::N, Bord::W}) {
    const bool hz = horizontal(b);
    const double centre = hz ? c.x0 : c.y0, demi = (hz ? c.L : c.W) / 2;
    for (double s = 0; s <= demi; s += 1)
      for (double sg : {1.0, -1.0}) {
        Composant& k = p.composants[indice];
        k.bord = b;
        k.le_long = centre + sg * s;
        const int g = gene(p, k.id);
        if (g == 0) return;
        if (g < meilleur) { meilleur = g; bb = b; bu = k.le_long; }
      }
  }
  p.composants[indice].bord = bb;
  p.composants[indice].le_long = bu;
}

}  // namespace

std::string ajouterComposant(Projet& p, const std::string& type) {
  const TypeComposant* T = typeComposant(type);
  if (!T) return {};
  Composant k;
  k.id = nouvelId("c", identifiants(p));
  k.ref = prochaineRef(p, T->prefixe);
  k.type = T->cle; k.valeur = T->valeur; k.w = T->w; k.d = T->d; k.h = T->h;
  if (T->bord) {
    k.bord = Bord::S; k.le_long = p.carte.x0; k.ecart = 0.4; k.decoupe = T->decoupe; k.zc = T->zc;
  } else {
    k.x = p.carte.x0; k.y = p.carte.y0; k.rot = 0;
  }
  if (T->couvercle > 0) k.trou_couvercle = T->couvercle;
  p.composants.push_back(k);
  if (T->bord) bordLibre(p, p.composants.size() - 1);
  else placeLibre(p, p.composants.size() - 1);
  return k.id;
}

std::string ajouterTrou(Projet& p) {
  Trou t;
  t.id = nouvelId("t", identifiants(p));
  t.ref = prochaineRef(p, "T");
  p.trous.push_back(t);
  Trou& n = p.trous.back();
  const Vis& v = vis(p.boitier.vis);
  const double m = std::max(v.trou / 2 + 1, v.avant / 2 + PAROI_PILIER + 0.3 - p.boitier.jeu) + 0.5;
  for (const char* coin : {"SW", "SE", "NE", "NW"}) {
    n.coin = std::string(coin);
    n.ox = arrondiJs(m * 2) / 2 + 0.5;
    n.oy = n.ox;
    if (gene(p, n.id) == 0) return n.id;
  }
  n.coin.reset(); n.ox = 4; n.oy = 4; n.x = p.carte.x0; n.y = p.carte.y0;
  return n.id;
}

}  // namespace prom
