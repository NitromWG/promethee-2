// Prométhée : mise en panneau de copies liées (8.16). Voir mise_en_panneau.hpp.
// Licence GPL-3.0-only.
#include "promethee/mise_en_panneau.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace prom::ecad {

namespace {
using J = nlohmann::ordered_json;
std::string mmTexte(double v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.2f", v);
  return t;
}
}  // namespace

J panneauVersJson(const MiseEnPanneau& p) {
  J j;
  j["nom"] = p.nom;
  j["largeur"] = p.largeur;
  j["hauteur"] = p.hauteur;
  j["rail"] = p.rail;
  j["ecart"] = p.ecart;
  j["largeurLanguette"] = p.largeurLanguette;
  j["diametrePerforation"] = p.diametrePerforation;
  j["pasPerforations"] = p.pasPerforations;
  j["languettesParBord"] = p.languettesParBord;
  J copies = J::array();
  for (const auto& c : p.copies) copies.push_back({{"carte", c.carte}, {"x", c.x}, {"y", c.y}, {"teteBeche", c.teteBeche}});
  j["copies"] = copies;
  J elements = J::array();
  for (const auto& e : p.elements) {
    J x = {{"genre", e.genre}, {"x", e.x}, {"y", e.y}, {"diametre", e.diametre}};
    if (e.largeur > 0) x["largeur"] = e.largeur;
    if (e.hauteur > 0) x["hauteur"] = e.hauteur;
    if (!e.texte.empty()) x["texte"] = e.texte;
    elements.push_back(x);
  }
  j["elements"] = elements;
  return j;
}

MiseEnPanneau panneauDepuisJson(const J& j) {
  MiseEnPanneau p;
  p.nom = j.value("nom", p.nom);
  p.largeur = j.value("largeur", 0.0);
  p.hauteur = j.value("hauteur", 0.0);
  p.rail = j.value("rail", p.rail);
  p.ecart = j.value("ecart", p.ecart);
  p.largeurLanguette = j.value("largeurLanguette", p.largeurLanguette);
  p.diametrePerforation = j.value("diametrePerforation", p.diametrePerforation);
  p.pasPerforations = j.value("pasPerforations", p.pasPerforations);
  p.languettesParBord = j.value("languettesParBord", p.languettesParBord);
  if (j.contains("copies"))
    for (const auto& c : j["copies"]) p.copies.push_back({c.value("carte", std::string()), c.value("x", 0.0), c.value("y", 0.0), c.value("teteBeche", false)});
  if (j.contains("elements"))
    for (const auto& e : j["elements"])
      p.elements.push_back({e.value("genre", std::string()), e.value("x", 0.0), e.value("y", 0.0), e.value("diametre", 1.0), e.value("largeur", 0.0), e.value("hauteur", 0.0), e.value("texte", std::string())});
  return p;
}

std::optional<MiseEnPanneau> panneauDe(const ConceptionElec& c) {
  const auto it = c.extensions.find("panneau");
  if (it == c.extensions.end()) return std::nullopt;
  return panneauDepuisJson(it->second);
}

void reglerPanneau(ConceptionElec& c, const std::optional<MiseEnPanneau>& p) {
  if (p) c.extensions["panneau"] = panneauVersJson(*p);
  else c.extensions.erase("panneau");
}

MiseEnPanneau panneauEnGrille(const ConceptionElec& c, int colonnes, int lignes, double ecart, double rail, bool teteBeche) {
  MiseEnPanneau p;
  const Boite b = boite(c.circuit.contour.polygone());
  const double W = mm(b.largeur()), H = mm(b.hauteur());
  const int C = std::max(1, colonnes), L = std::max(1, lignes);
  p.rail = rail;
  p.ecart = ecart;
  p.largeur = 2 * rail + 2 * ecart + C * W + (C - 1) * ecart;
  p.hauteur = 2 * rail + 2 * ecart + L * H + (L - 1) * ecart;
  p.nom = "Panneau " + std::to_string(C) + " × " + std::to_string(L);
  for (int i = 0; i < L; ++i)
    for (int j = 0; j < C; ++j) p.copies.push_back({c.uuid, rail + ecart + j * (W + ecart), rail + ecart + i * (H + ecart), teteBeche && (i % 2 == 1)});
  // Trois mires au milieu du rail (bas gauche, bas droit, haut gauche) ; trous d'outillage de 3 mm aux quatre coins
  const double m = rail / 2;
  for (const auto& [x, y] : std::vector<std::pair<double, double>>{{m + 5, m}, {p.largeur - m - 5, m}, {m + 5, p.hauteur - m}}) p.elements.push_back({"mire", x, y, 1.0, 0, 0, {}});
  if (rail >= 4)
    for (const auto& [x, y] : std::vector<std::pair<double, double>>{{m, m}, {p.largeur - m, m}, {m, p.hauteur - m}, {p.largeur - m, p.hauteur - m}})
      p.elements.push_back({"trouOutillage", x, y, std::min(3.0, rail - 1), 0, 0, {}});
  return p;
}

namespace {
const ConceptionElec* source(const std::vector<const ConceptionElec*>& cartes, const std::string& uuid) {
  for (const auto* c : cartes)
    if (c && c->uuid == uuid) return c;
  return nullptr;
}
struct Rect {
  double x0, y0, x1, y1;
  bool touche(const Rect& o, double marge) const { return x0 < o.x1 + marge && o.x0 < x1 + marge && y0 < o.y1 + marge && o.y0 < y1 + marge; }
};
Rect rectCopie(const ConceptionElec& c, const CopiePanneau& k) {
  const Boite b = boite(c.circuit.contour.polygone());
  return {k.x, k.y, k.x + mm(b.largeur()), k.y + mm(b.hauteur())};
}
}  // namespace

ModelePanneau construirePanneau(const std::vector<const ConceptionElec*>& cartes, const MiseEnPanneau& p, const std::string& version) {
  (void)version;
  ModelePanneau r;
  r.defauts = controlerPanneau(cartes, p);
  ModeleFabrication& m = r.modele;
  const Nm PW = nm(p.largeur), PH = nm(p.hauteur);
  m.nom = p.nom;
  m.boite = {0, 0, PW, PH};
  m.repere = m.boite;
  m.contour = {{0, 0}, {PW, 0}, {PW, PH}, {0, PH}};
  std::map<std::string, ModeleFabrication> modeles;
  auto couche = [&](Couche k) -> CoucheFab& {
    for (auto& cf : m.couches)
      if (cf.couche == k) return cf;
    m.couches.push_back({k, {}});
    return m.couches.back();
  };
  auto trait = [&](Pt a, Pt b) {
    if (a == b) return;
    PrimitiveFab t;
    t.type = PrimitiveFab::Type::Trait;
    t.a = a;
    t.b = b;
    t.l = 100'000;
    t.fonction = "Profile";
    couche(Couche::Contour).primitives.push_back(t);
  };
  trait({0, 0}, {PW, 0});
  trait({PW, 0}, {PW, PH});
  trait({PW, PH}, {0, PH});
  trait({0, PH}, {0, 0});
  int numero = 0;
  for (const auto& k : p.copies) {
    ++numero;
    const ConceptionElec* c = source(cartes, k.carte);
    if (!c) continue;
    auto it = modeles.find(c->uuid);
    if (it == modeles.end()) {
      const Connectivite kk = connectivite(*c);
      it = modeles.emplace(c->uuid, modeleFabrication(*c, kk, remplirZones(*c, kk))).first;
    }
    const ModeleFabrication& base = it->second;
    const Boite b = boite(c->circuit.contour.polygone());
    const Nm W = b.largeur(), H = b.hauteur();
    // Point de la source → panneau (repère carte du panneau, Y vers le bas)
    auto vers = [&](Pt q) {
      Nm u = q.x - b.x0, v = b.y1 - q.y;
      if (k.teteBeche) {
        u = W - u;
        v = H - v;
      }
      return Pt{nm(k.x) + u, PH - (nm(k.y) + v)};
    };
    for (const auto& cf : base.couches) {
      if (cf.couche == Couche::Contour) continue;
      CoucheFab& d = couche(cf.couche);
      for (PrimitiveFab q : cf.primitives) {
        q.a = vers(q.a);
        q.b = vers(q.b);
        q.m = vers(q.m);
        for (Pt& s : q.region) s = vers(s);
        if (k.teteBeche) q.rotation += 180;
        d.primitives.push_back(std::move(q));
      }
    }
    for (TrouFab t : base.trous) {
      t.p = vers(t.p);
      if (k.teteBeche) t.rotation += 180;
      m.trous.push_back(t);
    }
    ++r.copies;
    // Contour de la copie, coupé aux languettes de ses bords haut et bas ; perforations le long des languettes
    const Polygone contour = c->circuit.contour.polygone();
    const Nm lw = nm(p.largeurLanguette), rt = nm(p.diametrePerforation) / 2;
    const int n = std::max(1, p.languettesParBord);
    std::vector<std::pair<Nm, Nm>> languettes;
    const Nm gauche = nm(k.x);
    for (int t = 0; t < n; ++t) {
      const Nm cx = gauche + W * (t + 1) / (n + 1);
      languettes.push_back({cx - lw / 2, cx + lw / 2});
    }
    const Nm haut = PH - nm(k.y) - H, bas = PH - nm(k.y);
    for (std::size_t q = 0; q < contour.size(); ++q) {
      Pt a = vers(contour[q]), bb = vers(contour[(q + 1) % contour.size()]);
      if (a.y == bb.y && (std::abs(a.y - haut) <= 1 || std::abs(a.y - bas) <= 1)) {
        if (a.x > bb.x) std::swap(a, bb);
        Nm x = a.x;
        for (const auto& [t0, t1] : languettes) {
          if (t1 <= x || t0 >= bb.x) continue;
          if (t0 > x) trait({x, a.y}, {t0, a.y});
          x = std::max(x, t1);
        }
        if (x < bb.x) trait({x, a.y}, bb);
      } else
        trait(a, bb);
    }
    for (const Nm y : {haut - rt, bas + rt})
      for (const auto& [t0, t1] : languettes) {
        ++r.languettes;
        for (Nm x = t0 + nm(p.pasPerforations) / 2; x <= t1 - nm(p.diametrePerforation) / 2; x += nm(p.pasPerforations)) {
          m.trous.push_back({{x, y}, nm(p.diametrePerforation), false, "MechanicalDrill"});
          ++r.perforations;
        }
      }
  }
  // Éléments du cadre
  for (const auto& e : p.elements) {
    const Pt c{nm(e.x), PH - nm(e.y)};
    if (e.genre == "mire") {
      PrimitiveFab f;
      f.type = PrimitiveFab::Type::Disque;
      f.a = c;
      f.l = nm(e.diametre);
      f.fonction = "FiducialPad,Global";
      couche(Couche::CuivreDessus).primitives.push_back(f);
      f.l = nm(2 * e.diametre);
      f.fonction = "Material";
      couche(Couche::VernisDessus).primitives.push_back(f);
    } else if (e.genre == "trouOutillage") {
      m.trous.push_back({c, nm(e.diametre), false, "MechanicalDrill"});
    } else if (e.genre == "coupon" || e.genre == "rebut") {
      PrimitiveFab f;
      f.type = PrimitiveFab::Type::Rect;
      f.a = c;
      f.l = nm(e.largeur > 0 ? e.largeur : 10);
      f.h = nm(e.hauteur > 0 ? e.hauteur : 3);
      f.fonction = e.genre == "coupon" ? "Conductor" : "Legend";
      couche(e.genre == "coupon" ? Couche::CuivreDessus : Couche::SerigraphieDessus).primitives.push_back(f);
    }
  }
  return r;
}

std::vector<std::string> controlerPanneau(const std::vector<const ConceptionElec*>& cartes, const MiseEnPanneau& p) {
  std::vector<std::string> d;
  std::vector<Rect> rects;
  int numero = 0;
  std::map<std::string, bool> controlees;
  for (const auto& k : p.copies) {
    ++numero;
    const ConceptionElec* c = source(cartes, k.carte);
    if (!c) {
      d.push_back("copie " + std::to_string(numero) + " : carte source introuvable");
      rects.push_back({0, 0, 0, 0});
      continue;
    }
    const Rect r = rectCopie(*c, k);
    if (r.x0 < p.rail - 1e-6 || r.y0 < p.rail - 1e-6 || r.x1 > p.largeur - p.rail + 1e-6 || r.y1 > p.hauteur - p.rail + 1e-6)
      d.push_back("copie " + std::to_string(numero) + " hors du cadre (" + mmTexte(r.x0) + " ; " + mmTexte(r.y0) + ")");
    for (std::size_t j = 0; j < rects.size(); ++j)
      if (rects[j].x1 > rects[j].x0 && r.touche(rects[j], p.ecart - 0.01))
        d.push_back("copies " + std::to_string(j + 1) + " et " + std::to_string(numero) + " à moins de " + mmTexte(p.ecart) + " mm");
    rects.push_back(r);
    // DRC de chaque source, une fois
    if (!controlees.count(c->uuid)) {
      controlees[c->uuid] = true;
      const Connectivite kk = connectivite(*c);
      for (const auto& v : verifierDRCReglee(*c, kk, remplirZones(*c, kk)))
        if (v.gravite == Gravite::Erreur) d.push_back("carte « " + c->nom + " » : " + v.code + " — " + v.message);
    }
  }
  int e = 0;
  for (const auto& x : p.elements) {
    ++e;
    const double r = std::max({x.diametre, x.largeur, x.hauteur}) / 2;
    const Rect re{x.x - r, x.y - r, x.x + r, x.y + r};
    if (re.x0 < 0 || re.y0 < 0 || re.x1 > p.largeur || re.y1 > p.hauteur) d.push_back(x.genre + " " + std::to_string(e) + " hors du panneau");
    for (std::size_t j = 0; j < rects.size(); ++j)
      if (rects[j].x1 > rects[j].x0 && re.touche(rects[j], 0)) d.push_back(x.genre + " " + std::to_string(e) + " posé sur la copie " + std::to_string(j + 1));
  }
  return d;
}

std::vector<std::string> comparerEmpilages(const std::vector<const ConceptionElec*>& cartes) {
  std::vector<std::string> r;
  const ConceptionElec* ref = nullptr;
  for (const auto* c : cartes) {
    if (!c) continue;
    if (!ref) {
      ref = c;
      continue;
    }
    const Empilage &a = ref->circuit.empilage, &b = c->circuit.empilage;
    if (a.couchesCuivre != b.couchesCuivre) r.push_back("« " + c->nom + " » : " + std::to_string(b.couchesCuivre) + " cuivres, « " + ref->nom + " » : " + std::to_string(a.couchesCuivre));
    if (std::abs(epaisseurPile(pileEmpilage(a)) - epaisseurPile(pileEmpilage(b))) > 0.005)
      r.push_back("« " + c->nom + " » : épaisseur " + mmTexte(epaisseurPile(pileEmpilage(b))) + " mm, « " + ref->nom + " » : " + mmTexte(epaisseurPile(pileEmpilage(a))) + " mm");
    if (a.finition != b.finition) r.push_back("« " + c->nom + " » : finition " + b.finition + ", « " + ref->nom + " » : " + a.finition);
    if (a.materiau != b.materiau) r.push_back("« " + c->nom + " » : matériau " + b.materiau + ", « " + ref->nom + " » : " + a.materiau);
  }
  return r;
}

// ---------- Grille tarifaire ----------
GrilleTarifaire grilleDepuisJson(const J& j) {
  GrilleTarifaire g;
  g.fabricant = j.value("fabricant", std::string());
  g.date = j.value("date", std::string());
  if (j.contains("formats"))
    for (const auto& f : j["formats"]) {
      FormatPanneau x;
      x.nom = f.value("nom", std::string());
      x.largeur = f.value("largeur", 0.0);
      x.hauteur = f.value("hauteur", 0.0);
      x.prixFixe = f.value("prixFixe", 0.0);
      x.prixParDm2 = f.value("prixParDm2", 0.0);
      if (f.contains("remises"))
        for (const auto& r : f["remises"]) x.remises.push_back({r.value("a_partir_de", 0), r.value("facteur", 1.0)});
      g.formats.push_back(x);
    }
  return g;
}

J grilleVersJson(const GrilleTarifaire& g) {
  J j;
  j["fabricant"] = g.fabricant;
  j["date"] = g.date;
  J f = J::array();
  for (const auto& x : g.formats) {
    J r = J::array();
    for (const auto& [n, k] : x.remises) r.push_back({{"a_partir_de", n}, {"facteur", k}});
    f.push_back({{"nom", x.nom}, {"largeur", x.largeur}, {"hauteur", x.hauteur}, {"prixFixe", x.prixFixe}, {"prixParDm2", x.prixParDm2}, {"remises", r}});
  }
  j["formats"] = f;
  return j;
}

std::vector<OffrePanneau> dispositions(const ConceptionElec& c, const GrilleTarifaire& g, int quantite, double ecart, double rail, const std::atomic<bool>* arret) {
  std::vector<OffrePanneau> r;
  const Boite b = boite(c.circuit.contour.polygone());
  const double w0 = mm(b.largeur()), h0 = mm(b.hauteur());
  quantite = std::max(1, quantite);
  for (const auto& f : g.formats) {
    for (const bool tournee : {false, true}) {
      if (arret && arret->load()) return r;
      const double w = tournee ? h0 : w0, h = tournee ? w0 : h0;
      const int C = static_cast<int>(std::floor((f.largeur - 2 * rail - ecart + 1e-9) / (w + ecart)));
      const int L = static_cast<int>(std::floor((f.hauteur - 2 * rail - ecart + 1e-9) / (h + ecart)));
      if (C < 1 || L < 1) continue;
      OffrePanneau o;
      o.format = f.nom;
      o.colonnes = C;
      o.lignes = L;
      o.tournee = tournee;
      o.cartesParPanneau = C * L;
      o.panneaux = (quantite + o.cartesParPanneau - 1) / o.cartesParPanneau;
      double facteur = 1;
      for (const auto& [n, k] : f.remises)
        if (o.panneaux >= n) facteur = k;
      o.prixTotal = f.prixFixe + o.panneaux * f.largeur * f.hauteur / 1e4 * f.prixParDm2 * facteur;
      o.prixParCarte = o.prixTotal / quantite;
      r.push_back(o);
    }
  }
  std::stable_sort(r.begin(), r.end(), [](const OffrePanneau& a, const OffrePanneau& b) { return a.prixParCarte < b.prixParCarte; });
  return r;
}

}  // namespace prom::ecad
