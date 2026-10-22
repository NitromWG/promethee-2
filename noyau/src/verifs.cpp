// Prométhée : vérifications continues. Port exact des règles et des messages du prototype.
// Licence GPL-3.0-only.
#include "promethee/verifs.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace prom {

const char* nomSeverite(Severite s) { return s == Severite::Erreur ? "erreur" : "alerte"; }

namespace {
double borne(double v, double a, double b) { return std::min(b, std::max(a, v)); }
double signe(double v) { return static_cast<double>((v > 0) - (v < 0)); }
double distRectPoint(const GeoComp& g, double x, double y) {
  const double dx = std::max({g.x1 - x, 0.0, x - g.x2}), dy = std::max({g.y1 - y, 0.0, y - g.y2});
  return std::hypot(dx, dy);
}
const Composant* composant(const Projet& p, const std::string& id) {
  for (const auto& k : p.composants) if (k.id == id) return &k;
  return nullptr;
}
}  // namespace

std::string ControleBoitier::resume() const {
  if (complet) return {};
  std::string l;
  for (std::size_t i = 0; i < nonVerifies.size() && i < 6; ++i) l += (i ? ", " : "") + nonVerifies[i];
  if (nonVerifies.size() > 6) l += "…";
  return "Contrôle du boîtier incomplet : " + std::to_string(nonVerifies.size()) + " composant(s) sans hauteur vérifiée (" + l +
         ") ; leur hauteur est typique, estimée ou générique.";
}

ControleBoitier controleBoitier(const Projet& p) {
  ControleBoitier c;
  if (p.sansCarte) return c;
  for (const auto& k : p.composants)
    if (k.sourceHauteur != "verifiee") c.nonVerifies.push_back(k.ref.empty() ? k.id : k.ref);
  c.complet = c.nonVerifies.empty();
  return c;
}

std::vector<Probleme> verifier(const Projet& p, const Derive& d) {
  std::vector<Probleme> pb;
  const auto& c = p.carte;
  const auto& b = p.boitier;
  auto ajouter = [&pb](Severite sev, const char* code, std::string msg, std::vector<std::string> ids = {},
                       std::optional<Correction> fix = std::nullopt) {
    pb.push_back({sev, code, std::move(msg), std::move(ids), std::move(fix)});
  };
  auto sdCarte = [&d](double x, double y) { return d.sdCarte(x, y); };
  const Correction corrHauteur{"Ajuster la hauteur", "hauteur", "", 0};
  const auto E = Severite::Erreur, A = Severite::Alerte;

  for (const auto& g : d.comps) {
    if (g.bord) {
      const bool hz = horizontal(*g.bord);
      std::array<Point, 2> dedans;
      if (hz) {
        const double y = g.face - signe(g.face - c.y0) * g.d;
        dedans = {Point{g.x1, y}, Point{g.x2, y}};
      } else {
        const double x = g.face - signe(g.face - c.x0) * g.d;
        dedans = {Point{x, g.y1}, Point{x, g.y2}};
      }
      const double hors = std::max(sdCarte(dedans[0].x, dedans[0].y), sdCarte(dedans[1].x, dedans[1].y));
      if (hors > 0.01) ajouter(E, "bord_hors", g.ref + " dépasse de l\u2019angle de la carte de " + fmt(hors) + " mm.", {g.id});
      const Composant& k = *composant(p, g.id);
      const double portee = b.jeu - k.ecart;
      if (k.ecart > 2)
        ajouter(E, "connecteur_loin", g.ref + " est à " + fmt(k.ecart) + " mm de la paroi : la fiche n\u2019entrera pas.", {g.id},
                Correction{"Rapprocher de la paroi", "ecart", g.id, 0});
      else if (k.ecart < 0.1)
        ajouter(E, "connecteur_paroi", g.ref + " touche la paroi du boîtier.", {g.id}, Correction{"Écarter de 0,4 mm", "ecart", g.id, 0});
      if (portee > k.d - 2)
        ajouter(E, "connecteur_porte", g.ref + " dépasse de " + fmt(portee) + " mm du bord de la carte : il ne reste que " + fmt(k.d - portee) + " mm pour le souder.", {g.id});
      const auto& o = *g.decoupe;
      if (!decoupeSurPartieDroite(d, o))
        ajouter(E, "decoupe_angle", d.libre ? "La découpe de " + g.ref + " tombe sur une partie courbe ou un angle du contour. Place le connecteur sur une portion droite du bord."
                                            : "La découpe de " + g.ref + " tombe dans l\u2019angle du boîtier. Rapproche le connecteur du milieu du bord.", {g.id});
      if (o.zc - o.h / 2 < d.zf + 0.3)
        ajouter(E, "decoupe_bas", "La découpe de " + g.ref + " descend dans le fond du boîtier.", {g.id});
      else if (o.zc + o.h / 2 > d.zt - 0.3)
        ajouter(E, "decoupe_haut", "La découpe de " + g.ref + " dépasse le haut de la paroi.", {g.id}, corrHauteur);
      else if (o.zc + o.h / 2 > d.zLevre - 0.2)
        ajouter(E, "decoupe_levre", "La lèvre du couvercle masque le haut de la découpe de " + g.ref + ".", {g.id}, corrHauteur);
    } else {
      double hors = -std::numeric_limits<double>::infinity();
      const std::array<Point, 4> coins{{{g.x1, g.y1}, {g.x2, g.y1}, {g.x2, g.y2}, {g.x1, g.y2}}};
      for (const auto& q : coins) hors = std::max(hors, sdCarte(q.x, q.y));
      const Composant* k = nullptr;
      for (const auto& q : p.composants) if (q.id == g.id) k = &q;
      if (k && k->origine == "kicad") {
        // Placé par KiCad : il peut déborder de la carte ; le boîtier s'élargit autour de lui (voir Derive::debords).
      } else if (hors > 0.01) {
        ajouter(E, "hors_carte", g.ref + " dépasse du bord de la carte de " + fmt(hors) + " mm.", {g.id});
      }
    }
  }
  std::vector<const GeoComp*> decs;
  for (const auto& g : d.comps) if (g.decoupe) decs.push_back(&g);
  for (size_t i = 0; i < decs.size(); ++i)
    for (size_t j = i + 1; j < decs.size(); ++j) {
      const auto& a = *decs[i]->decoupe;
      const auto& e = *decs[j]->decoupe;
      if (a.mur == e.mur && std::abs(a.u - e.u) < (a.w + e.w) / 2 + 0.6 && std::abs(a.zc - e.zc) < (a.h + e.h) / 2 + 0.6)
        ajouter(E, "decoupes", "Les découpes de " + decs[i]->ref + " et " + decs[j]->ref + " se touchent.", {decs[i]->id, decs[j]->id});
    }
  std::vector<char> deKicad(d.comps.size(), 0);
  for (size_t i = 0; i < d.comps.size() && i < p.composants.size(); ++i) deKicad[i] = p.composants[i].origine == "kicad";
  for (size_t i = 0; i < d.comps.size(); ++i)
    for (size_t j = i + 1; j < d.comps.size(); ++j) {
      const auto& a = d.comps[i];
      const auto& e = d.comps[j];
      // Deux composants placés par KiCad : l'implantation relève de la vérification de KiCad (zones de placement exactes).
      // Deux faces différentes : pas de conflit.
      if ((deKicad[i] && deKicad[j]) || a.dessous != e.dessous) continue;
      if (a.x1 < e.x2 - 0.05 && e.x1 < a.x2 - 0.05 && a.y1 < e.y2 - 0.05 && e.y1 < a.y2 - 0.05)
        ajouter(E, "chevauchement", a.ref + " et " + e.ref + " se chevauchent.", {a.id, e.id});
    }
  for (const auto& t : d.trous)
    for (const auto& g : d.comps)
      if (!g.dessous && distRectPoint(g, t.x, t.y) < t.rTete)
        ajouter(E, "tete_vis", g.ref + " empiète sur la tête de vis du trou " + t.ref + ".", {g.id, t.id});
  for (const auto& t : d.trous) {
    const double minBord = t.vis.trou / 2 + 1;
    const double bord = -sdCarte(t.x, t.y);
    if (bord < 0)
      ajouter(E, "trou_hors", "Le trou " + t.ref + " est hors de la carte.", {t.id}, Correction{"Ramener sur la carte", "trou", t.id, 0});
    else if (bord < minBord - 0.01)
      ajouter(E, "trou_bord", "Le trou " + t.ref + " est à " + fmt(bord) + " mm du bord de la carte (" + fmt(minBord) + " mm au minimum).", {t.id},
              Correction{"Éloigner du bord", "trou", t.id, 0});
    else {
      const double paroi = -d.sdCavite(t.x, t.y);
      if (paroi < t.Rb + 0.3)
        ajouter(E, "pilier_paroi", "Le pilier du trou " + t.ref + " touche la paroi du boîtier.", {t.id}, Correction{"Éloigner de la paroi", "trou", t.id, 0});
    }
  }
  for (size_t i = 0; i < d.trous.size(); ++i)
    for (size_t j = i + 1; j < d.trous.size(); ++j) {
      const auto& a = d.trous[i];
      const auto& e = d.trous[j];
      if (std::hypot(a.x - e.x, a.y - e.y) < a.Rb + e.Rb + 0.4)
        ajouter(E, "piliers", "Les piliers des trous " + a.ref + " et " + e.ref + " se touchent.", {a.id, e.id});
    }
  // Face arrière : place sous la carte, à l'écart du fond et des piliers.
  for (const auto& g : d.comps) {
    if (!g.dessous) continue;
    if (g.bas < d.zf + 0.5) {
      const double v2 = std::ceil((g.h + 0.5) * 2) / 2;
      ajouter(E, "dessous_fond", g.ref + " (face arrière, " + fmt(g.h) + " mm de haut) touche le fond du boîtier : entretoises trop courtes.", {g.id},
              Correction{"Entretoises de " + fmt(v2) + " mm", "entretoise", "", v2});
    }
    for (const auto& t : d.trous)
      if (distRectPoint(g, t.x, t.y) < t.Rb + 0.3)
        ajouter(E, "pilier_dessous", "Le pilier du trou " + t.ref + " heurte " + g.ref + " sous la carte.", {g.id, t.id});
  }
  for (const auto& g : d.comps) {
    if (g.dessous) continue;
    const TypeComposant& T = *typeComposant(g.type);
    if (T.bouton) {
      if (g.top < d.zt - 0.3)
        ajouter(A, "bouton_bas", g.ref + " n\u2019atteint pas le couvercle : il manque " + fmt(d.zt - g.top) + " mm pour pouvoir l\u2019appuyer.", {g.id},
                hauteurIdeale(p, d) < d.H - 0.01 ? std::optional<Correction>(corrHauteur) : std::nullopt);
      else if (g.top > d.ztop + 3)
        ajouter(A, "bouton_haut", g.ref + " dépasse du couvercle de " + fmt(g.top - d.ztop) + " mm.", {g.id});
    } else if (g.top > d.zt - 0.5) {
      ajouter(E, "hauteur", g.ref + " touche le couvercle : il manque " + fmt(g.top - d.zt + 0.5) + " mm.", {g.id}, corrHauteur);
    } else if (dansLevre(d, g) && g.top > d.zLevre - 0.3) {
      ajouter(E, "levre", g.ref + " touche la lèvre du couvercle.", {g.id}, corrHauteur);
    }
    if (g.trouCouvercle) {
      const auto& o = *g.trouCouvercle;
      if (d.sdLevreInt(o.x, o.y) > -(o.d / 2 + 0.3))
        ajouter(E, "trou_couvercle", "Le perçage de " + g.ref + " dans le couvercle tombe sur la lèvre.", {g.id});
    }
  }
  std::vector<const GeoComp*> lids;
  for (const auto& g : d.comps) if (g.trouCouvercle) lids.push_back(&g);
  for (size_t i = 0; i < lids.size(); ++i)
    for (size_t j = i + 1; j < lids.size(); ++j) {
      const auto& a = *lids[i]->trouCouvercle;
      const auto& e = *lids[j]->trouCouvercle;
      if (std::hypot(a.x - e.x, a.y - e.y) < (a.d + e.d) / 2 + 0.8)
        ajouter(E, "percages", "Les perçages de " + lids[i]->ref + " et " + lids[j]->ref + " dans le couvercle se touchent.", {lids[i]->id, lids[j]->id});
    }
  if (p.trous.empty()) ajouter(A, "sans_trou", "Aucun trou de fixation : la carte ne tiendra pas dans le boîtier.");
  if (b.paroi < 1.2) ajouter(A, "paroi", "Parois de " + fmt(b.paroi) + " mm : trop fines pour une impression solide (1,2 mm conseillé).");
  if (b.fond < 1) ajouter(A, "fond", "Fond de " + fmt(b.fond) + " mm : trop fin pour une impression solide (1 mm conseillé).");
  if (b.couvercle < 1.2) ajouter(A, "couvercle", "Couvercle de " + fmt(b.couvercle) + " mm : trop fin pour une impression solide (1,2 mm conseillé).");
  if (b.entretoise < 2)
    ajouter(A, "entretoise", "Entretoises de " + fmt(b.entretoise) + " mm : les pattes des composants traversants risquent de toucher le fond (2 mm conseillé).", {},
            Correction{"Passer à 2 mm", "entretoise", "", 2});
  // Une alerte par taille de vis autotaraudeuse dont la pointe atteindrait le fond.
  std::vector<std::string> tailles;
  for (const auto& t : d.trous)
    if (!t.insert && std::find(tailles.begin(), tailles.end(), t.nomVis) == tailles.end()) tailles.push_back(t.nomVis);
  for (const auto& nom : tailles) {
    const int lv = longueurVis(c.t, vis(nom));
    const double depasse = lv - c.t - (b.entretoise - 0.5);
    if (depasse > 0.01) {
      const double v2 = std::ceil((lv - c.t + 0.5) * 2) / 2;
      ajouter(A, "vis_longue", "Les vis " + nom + " × " + std::to_string(lv) + " toucheront le fond : entretoises de " + fmt(b.entretoise) + " mm trop courtes.", {},
              Correction{"Entretoises de " + fmt(v2) + " mm", "entretoise", "", v2});
    }
  }
  // Inserts laiton : le pilier doit être plus haut que l'insert.
  for (const auto& t : d.trous) {
    if (!t.insert || t.longInsert <= b.entretoise - 0.5) continue;
    const double v2 = std::ceil((t.longInsert + 0.5) * 2) / 2;
    ajouter(A, "insert_long", "Le pilier du trou " + t.ref + " est trop court pour un insert " + t.nomVis + " de " + fmt(t.longInsert) + " mm (entretoises de " + fmt(b.entretoise) + " mm).",
            {t.id}, Correction{"Entretoises de " + fmt(v2) + " mm", "entretoise", "", v2});
  }
  return pb;
}

void repousserTrou(Projet& p, const std::string& id) {
  Trou* t = nullptr;
  for (auto& q : p.trous) if (q.id == id) t = &q;
  if (!t) return;
  const auto& c = p.carte;
  for (int i = 0; i < 400; ++i) {
    const Derive d = deriver(p);
    const Point q = posTrou(p, *t);
    const double bord = -d.sdCarte(q.x, q.y);
    const double paroi = -d.sdCavite(q.x, q.y);
    double rTrou = d.vis.trou / 2, Rb = d.Rb;
    for (const auto& tp : d.trous) if (tp.id == id) { rTrou = tp.vis.trou / 2; Rb = tp.Rb; }
    if (bord >= rTrou + 1 && paroi >= Rb + 0.3) break;
    const double dx = c.x0 - q.x, dy = c.y0 - q.y;
    const double fx = std::abs(q.x - c.x0) > c.L / 2 - 6 ? signe(dx) : 0;
    const double fy = std::abs(q.y - c.y0) > c.W / 2 - 6 ? signe(dy) : 0;
    const double ux = fx != 0 ? fx : (fy != 0 ? 0 : signe(dx));
    const double uy = fy != 0 ? fy : (fx != 0 ? 0 : signe(dy));
    if (ux == 0 && uy == 0) break;
    ancrerTrou(p, *t, q.x + ux * 0.25, q.y + uy * 0.25);
  }
  const Point q = posTrou(p, *t);
  ancrerTrou(p, *t, arrondiJs(q.x * 4) / 4, arrondiJs(q.y * 4) / 4);
}

void corriger(Projet& p, const Correction& fix) {
  auto& b = p.boitier;
  if (fix.type == "hauteur") {
    b.hauteur = borne(hauteurIdeale(p, deriver(p)), b.entretoise + p.carte.t + 1, 200);
  } else if (fix.type == "ecart") {
    for (auto& k : p.composants) if (k.id == fix.id) k.ecart = 0.4;
  } else if (fix.type == "jeu") {
    p.boitier.jeu = fix.valeur;
  } else if (fix.type == "entretoise") {
    b.entretoise = borne(fix.valeur, 1, 30);
    b.hauteur = std::max(b.hauteur, b.entretoise + p.carte.t + 1);
  } else if (fix.type == "trou") {
    repousserTrou(p, fix.id);
  }
}

std::vector<std::string> corrigerTout(Projet& p, int toursMax) {
  std::vector<std::string> faites;
  for (int i = 0; i < toursMax; ++i) {
    const auto pb = verifier(p, deriver(p));
    const auto it = std::find_if(pb.begin(), pb.end(), [](const Probleme& q) { return q.fix.has_value(); });
    if (it == pb.end()) break;
    faites.push_back(it->fix->libelle);
    corriger(p, *it->fix);
  }
  return faites;
}

}  // namespace prom
