// Prométhée : import électrique d'une carte KiCad (.kicad_pcb). Licence GPL-3.0-only.
#include "promethee/fichiers.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/kicad_electronique.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <cstdio>

#include <nlohmann/json.hpp>

#include "kicad_interne.hpp"
#include "promethee/kicad.hpp"
#include "promethee/police.hpp"

namespace prom::ecad {
namespace {

using kicad_interne::Expr;

Pt kp(const Expr& e, std::size_t i = 1) { return {nm(e.nombre(i)), nm(e.nombre(i + 1))}; }

std::optional<Couche> coucheKicad(const std::string& n) {
  // Toutes les couches du modèle (0.34, 10.1) : 32 cuivres, vernis, pâte, colle, dessins, couches personnelles
  if (n == "F.SilkS" || n == "F.Silkscreen") return Couche::SerigraphieDessus;
  if (n == "B.SilkS" || n == "B.Silkscreen") return Couche::SerigraphieDessous;
  if (n == "F.CrtYd" || n == "F.Courtyard") return Couche::EncombrementDessus;
  if (n == "B.CrtYd" || n == "B.Courtyard") return Couche::EncombrementDessous;
  if (n == "F.Adhes" || n == "F.Adhesive") return Couche::ColleDessus;
  if (n == "B.Adhes" || n == "B.Adhesive") return Couche::ColleDessous;
  if (n == "Dwgs.User" || n == "User.Drawings" || n == "Eco1.User" || n == "User.Eco1") return Couche::Dessins;
  if (n == "Cmts.User" || n == "User.Comments" || n == "Eco2.User" || n == "User.Eco2" || n == "Margin") return Couche::Commentaires;
  return coucheDepuisCode(n);
}
// Cuivres d'une carte de « nc » couches
Couches tousLesCuivres(int nc) {
  Couches r = 0;
  for (Couche k : couchesCuivre(std::max(nc, 1))) r |= bit(k);
  return r;
}
// Arc de KiCad 5 (« start » : centre, « end » : départ, « angle » : balayage en degrés) en trois points
std::array<Pt, 3> arcKicad5(Pt centre, Pt depart, double angle) {
  const double dx = static_cast<double>(depart.x - centre.x), dy = static_cast<double>(depart.y - centre.y);
  auto tourner = [&](double a) {
    const double r = a * std::numbers::pi / 180, c = std::cos(r), s = std::sin(r);
    return Pt{centre.x + static_cast<Nm>(std::llround(dx * c - dy * s)), centre.y + static_cast<Nm>(std::llround(dx * s + dy * c))};
  };
  return {depart, tourner(angle / 2), tourner(angle)};
}

double largeurTrait(const Expr& e, double defaut = 0.12) {
  if (const Expr* s = e.enfant("stroke"))
    if (const Expr* w = s->enfant("width")) return w->nombre(1, defaut);
  if (const Expr* w = e.enfant("width")) return w->nombre(1, defaut);
  return defaut;
}

// Arc par trois points : suite de points, extrémités comprises
std::vector<Pt> arcTroisPoints(Pt a, Pt m, Pt b) {
  const double ax = static_cast<double>(a.x), ay = static_cast<double>(a.y), mx = static_cast<double>(m.x), my = static_cast<double>(m.y), bx = static_cast<double>(b.x),
               by = static_cast<double>(b.y);
  const double d = 2 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
  if (std::abs(d) < 1e-6) return {a, b};
  const double ux = ((ax * ax + ay * ay) * (my - by) + (mx * mx + my * my) * (by - ay) + (bx * bx + by * by) * (ay - my)) / d;
  const double uy = ((ax * ax + ay * ay) * (bx - mx) + (mx * mx + my * my) * (ax - bx) + (bx * bx + by * by) * (mx - ax)) / d;
  const double r = std::hypot(ax - ux, ay - uy);
  const double a0 = std::atan2(ay - uy, ax - ux), am = std::atan2(my - uy, mx - ux), a1 = std::atan2(by - uy, bx - ux);
  auto norm = [](double x) {
    while (x < 0) x += 2 * std::numbers::pi;
    while (x >= 2 * std::numbers::pi) x -= 2 * std::numbers::pi;
    return x;
  };
  // Sens : celui qui passe par le point milieu
  double balayage = norm(a1 - a0);
  if (norm(am - a0) > balayage) balayage -= 2 * std::numbers::pi;
  const int n = std::max(2, static_cast<int>(std::ceil(std::abs(balayage) / (std::numbers::pi / 18))));
  std::vector<Pt> v;
  for (int k = 0; k <= n; ++k) {
    const double t = a0 + balayage * k / n;
    v.push_back({static_cast<Nm>(std::llround(ux + r * std::cos(t))), static_cast<Nm>(std::llround(uy + r * std::sin(t)))});
  }
  v.front() = a;
  v.back() = b;
  return v;
}

struct BrocheReseau {
  std::string numero, nom, reseau;
};

// Définition d'une empreinte KiCad (pastilles, sérigraphie, encombrement, corps), dans le repère local de
// Prométhée pour son placement « em » ; « broches » reçoit les numéros de pastilles et leur réseau
DefEmpreinte definitionDepuisKicad(const Expr& e, const EmpreintePlacee& em, const std::string& nomKicad, std::map<std::string, int>& /*ignores*/,
                                   const std::function<std::string(const Expr&)>& reseau, std::vector<BrocheReseau>& broches, int nc = 4) {
  // Points de l'empreinte : position du fichier (repère de l'empreinte, déjà retournée sur la face arrière)
  // tournée et placée, puis ramenée dans le repère local de Prométhée
  auto local = [&](const Expr& x, std::size_t i = 1) { return versEmpreinte(em, tournerDegres(kp(x, i), em.rotation) + em.position); };
  auto coucheLocale = [&](Couche x) { return em.dessous ? opposee(x) : x; };
  DefEmpreinte d;
  d.description = "Empreinte KiCad « " + nomKicad + " »";
  d.famille = "KiCad";
  std::set<std::string> numeros;
  Boite enc;
  for (const auto& s : e.elements) {
    const std::string n = s.nom();
    if (n == "pad") {
      // Pastille sans cuivre (ouverture de pâte ou de vernis seule) : gardée avec ses seules couches, sans numéro
      // électrique (10.1)
      bool cuivre = false;
      if (const Expr* ls = s.enfant("layers"))
        for (std::size_t i = 1; i < ls->elements.size(); ++i) {
          const std::string l = ls->valeur(i);
          cuivre |= l.size() > 3 && l.compare(l.size() - 3, 3, ".Cu") == 0;
        }
      Pastille p;
      p.numero = cuivre ? s.valeur(1) : std::string();
      const std::string type = s.valeur(2), forme = s.valeur(3);
      // « connect » : contact sans trou ni pâte (doigt de connecteur, plage de clavier), une CMS à ses seules couches
      p.type = type == "smd" || type == "connect" ? TypePastille::CMS : type == "np_thru_hole" ? TypePastille::NonMetallisee : TypePastille::Traversante;
      if (type == "connect") p.fabrication = "connecteurBord";
      p.forme = forme == "circle" ? FormePastille::Rond : forme == "oval" ? FormePastille::Ovale : forme == "roundrect" ? FormePastille::RectArrondi : forme == "trapezoid" ? FormePastille::Trapeze
                : forme == "custom" ? FormePastille::Libre : FormePastille::Rect;
      if (const Expr* rr = s.enfant("roundrect_rratio")) p.arrondi = rr->nombre(1, 0.25);
      if (const Expr* ch = s.enfant("chamfer_ratio")) {
        p.forme = FormePastille::Chanfreine;
        p.chanfrein = ch->nombre(1, 0.2);
        if (const Expr* coins = s.enfant("chamfer")) {
          p.coinsChanfrein = 0;
          for (std::size_t i = 1; i < coins->elements.size(); ++i) {
            const std::string c2 = coins->valeur(i);
            p.coinsChanfrein |= c2 == "top_left" ? 1 : c2 == "top_right" ? 2 : c2 == "bottom_right" ? 4 : c2 == "bottom_left" ? 8 : 0;
          }
        }
      }
      if (const Expr* t = s.enfant("size")) {
        p.largeur = nm(t->nombre(1));
        p.hauteur = nm(t->nombre(2, t->nombre(1)));
      }
      if (p.forme == FormePastille::Trapeze)
        if (const Expr* d2 = s.enfant("rect_delta")) p.trapeze = nm(std::max(std::abs(d2->nombre(1, 0)), std::abs(d2->nombre(2, 0))));
      // Pastille de forme libre : l'ancre et ses primitives (polygones, cercles, traits épais), dans le repère de la
      // pastille
      if (p.forme == FormePastille::Libre) {
        Polygones morceaux;
        const bool ancreRonde = [&] {
          if (const Expr* o = s.enfant("options"))
            if (const Expr* a = o->enfant("anchor")) return a->valeur(1) == "circle";
          return false;
        }();
        morceaux.push_back(ancreRonde ? cercle({0, 0}, std::max<Nm>(p.largeur / 2, 1)) : rectangle({0, 0}, std::max<Nm>(p.largeur, 1), std::max<Nm>(p.hauteur, 1), 0));
        if (const Expr* prims = s.enfant("primitives"))
          for (const auto& g : prims->elements) {
            const std::string gn = g.nom();
            const Nm ep = nm(largeurTrait(g, 0));
            if (gn == "gr_poly")
              if (const Expr* pts = g.enfant("pts")) {
                Polygone q;
                for (const auto& xy : pts->elements)
                  if (xy.nom() == "xy") q.push_back(kp(xy));
                if (q.size() >= 3) morceaux.push_back(orienter(q));
                if (ep > 0) for (std::size_t i = 0; i < q.size(); ++i) morceaux.push_back(oblong(q[i], q[(i + 1) % q.size()], ep));
              }
            if (gn == "gr_circle" && g.enfant("center") && g.enfant("end")) {
              const Pt c2 = kp(*g.enfant("center"));
              morceaux.push_back(cercle(c2, static_cast<Nm>(std::llround(distance(c2, kp(*g.enfant("end"))) + static_cast<double>(ep) / 2))));
            }
            if ((gn == "gr_line" || gn == "gr_rect") && g.enfant("start") && g.enfant("end")) {
              const Pt a2 = kp(*g.enfant("start")), b2 = kp(*g.enfant("end"));
              if (gn == "gr_rect") morceaux.push_back(orienter(Polygone{a2, {b2.x, a2.y}, b2, {a2.x, b2.y}}));
              else morceaux.push_back(oblong(a2, b2, std::max<Nm>(ep, 1)));
            }
          }
        const Polygones u = unir(morceaux);
        if (!u.empty()) p.contourLibre = *std::max_element(u.begin(), u.end(), [](const Polygone& x, const Polygone& y) { return aireMm2(x) < aireMm2(y); });
      }
      if (const Expr* dr = s.enfant("drill")) {
        if (dr->valeur(1) == "oval") {
          // Fente (4.27) : le long de l'axe x de la pastille ; une fente verticale tourne la pastille d'un quart de tour
          const double dx = dr->nombre(2), dy = dr->nombre(3, dr->nombre(2));
          p.percage = nm(std::min(dx, dy));
          p.longueurPercage = nm(std::max(dx, dy));
          if (dy > dx) {
            std::swap(p.largeur, p.hauteur);
            p.rotation = 90;  // ajouté à l'angle de la pastille plus bas
          }
        } else
          p.percage = nm(dr->nombre(1));
        if (const Expr* off = dr->enfant("offset")) p.decalagePercage = kp(*off);
      }
      if (const Expr* zc = s.enfant("zone_connect")) {
        const int v = static_cast<int>(zc->nombre(1, -1));
        p.connexion = v == 0 ? ConnexionZone::Aucune : v == 1 ? ConnexionZone::Thermique : v == 2 ? ConnexionZone::Pleine : ConnexionZone::Heritee;
      }
      if (const Expr* m = s.enfant("solder_mask_margin")) p.margeVernis = nm(m->nombre(1, 0));
      if (const Expr* m = s.enfant("solder_paste_margin")) p.margePate = nm(m->nombre(1, 0));
      // KiCad écrit l'angle absolu de la pastille et l'omet quand il vaut 0 : la valeur par défaut est donc 0, pas
      // l'angle de l'empreinte (X1). Une pastille de 3 × 0,5 mm sans angle dans une empreinte à 90° reste à 0°.
      double angleAbsolu = 0;
      if (const Expr* at = s.enfant("at")) {
        p.position = local(*at);
        angleAbsolu = at->nombre(3, 0.0);
      }
      // Couches de la pastille (X2), ramenées dans le repère de l'empreinte posée dessus : une CMS sur B.Cu dans
      // une empreinte du dessus (connecteur de tranche), une pastille sans pâte (doigt de connecteur, mire) ou sans
      // ouverture de vernis gardent leurs couches réelles
      if (const Expr* ls = s.enfant("layers")) {
        Couches lues = 0;
        for (std::size_t i = 1; i < ls->elements.size(); ++i) {
          const std::string l = ls->valeur(i);
          if (l == "*.Cu") lues |= tousLesCuivres(std::max(nc, 4));
          else if (l == "F&B.Cu") lues |= bit(Couche::CuivreDessus) | bit(Couche::CuivreDessous);
          else if (l == "*.Mask") lues |= bit(Couche::VernisDessus) | bit(Couche::VernisDessous);
          else if (l == "*.Paste") lues |= bit(Couche::PateDessus) | bit(Couche::PateDessous);
          else if (l == "F.Mask") lues |= bit(Couche::VernisDessus);
          else if (l == "B.Mask") lues |= bit(Couche::VernisDessous);
          else if (l == "F.Paste") lues |= bit(Couche::PateDessus);
          else if (l == "B.Paste") lues |= bit(Couche::PateDessous);
          else if (const std::optional<Couche> k = coucheKicad(l)) lues |= bit(*k);
        }
        Couches locales = 0;
        for (int i = 0; i < NB_COUCHES; ++i)
          if (lues & bit(static_cast<Couche>(i))) locales |= bit(coucheLocale(static_cast<Couche>(i)));
        // Un trou non métallisé n'a jamais de cuivre, même si KiCad écrit « *.Cu »
        if (p.type == TypePastille::NonMetallisee) locales &= ~tousLesCuivres(32);
        // Seulement quand elles diffèrent de ce que le type suppose : un fichier simple reste simple
        Pastille parDefaut;
        parDefaut.type = p.type;
        if (locales != couchesPastille(parDefaut, false, std::max(nc, 4))) p.couches = locales;
      }
      p.rotation = p.rotation + (em.dessous ? em.rotation - angleAbsolu : angleAbsolu - em.rotation);
      p.rotation = std::fmod(p.rotation + 720.0, 360.0);
      if (p.largeur <= 0) continue;
      d.pastilles.push_back(p);
      
      enc.etendre(boite(formePastille(p)));
      if (!p.numero.empty() && numeros.insert(p.numero).second) {
        BrocheReseau b;
        b.numero = p.numero;
        if (const Expr* pf = s.enfant("pinfunction")) b.nom = pf->valeur(1);
        b.reseau = reseau ? reseau(s) : std::string();
        broches.push_back(b);
      } else if (!p.numero.empty() && reseau) {
        // Même numéro (pastille thermique en plusieurs morceaux) : même broche ; un réseau manquant se complète
        for (auto& b : broches)
          if (b.numero == p.numero && b.reseau.empty()) b.reseau = reseau(s);
      }
      continue;
    }
    const std::optional<Couche> cl = coucheKicad(kicad_interne::couche(s));
    if (!cl) continue;
    const Couche cd = coucheLocale(*cl);
    const bool encombrement = cd == Couche::EncombrementDessus;
    auto trait = [&](Pt a, Pt b) {
      if (encombrement) {
        enc.etendre(a);
        enc.etendre(b);
        return;
      }
      TraitEmpreinte t;
      t.couche = cd;
      t.a = a;
      t.b = b;
      t.epaisseur = nm(largeurTrait(s));
      d.traits.push_back(t);
    };
    if ((n == "fp_line") && s.enfant("start") && s.enfant("end")) trait(local(*s.enfant("start")), local(*s.enfant("end")));
    else if (n == "fp_rect" && s.enfant("start") && s.enfant("end") && s.enfant("start")->elements.size() >= 3 && s.enfant("end")->elements.size() >= 3) {
      const Expr& a = *s.enfant("start");
      const Expr& b = *s.enfant("end");
      Expr c1 = a, c2 = b;
      c1.elements[2] = b.elements[2];
      c2.elements[2] = a.elements[2];
      const Pt p0 = local(a), p1 = local(c2), p2 = local(b), p3 = local(c1);
      trait(p0, p1);
      trait(p1, p2);
      trait(p2, p3);
      trait(p3, p0);
    } else if (n == "fp_circle" && s.enfant("center") && s.enfant("end")) {
      const Pt centre = local(*s.enfant("center")), bord = local(*s.enfant("end"));
      const Nm rayon = static_cast<Nm>(std::llround(distance(centre, bord)));
      if (encombrement) {
        enc.etendre(centre - Pt{rayon, rayon});
        enc.etendre(centre + Pt{rayon, rayon});
      } else {
        TraitEmpreinte t;
        t.couche = cd;
        t.a = centre;
        t.rayon = rayon;
        t.epaisseur = nm(largeurTrait(s));
        d.traits.push_back(t);
      }
    } else if (n == "fp_arc" && s.enfant("start") && s.enfant("end")) {
      if (s.enfant("mid")) {
        const auto pts = arcTroisPoints(local(*s.enfant("start")), local(*s.enfant("mid")), local(*s.enfant("end")));
        for (std::size_t i = 1; i < pts.size(); ++i) trait(pts[i - 1], pts[i]);
      } else if (const Expr* ang = s.enfant("angle")) {
        // KiCad 5 : centre, point de départ et balayage
        const auto a3 = arcKicad5(local(*s.enfant("start")), local(*s.enfant("end")), em.dessous ? -ang->nombre(1) : ang->nombre(1));
        const auto pts = arcTroisPoints(a3[0], a3[1], a3[2]);
        for (std::size_t i = 1; i < pts.size(); ++i) trait(pts[i - 1], pts[i]);
      }
    } else if (n == "fp_poly") {
      if (const Expr* pts = s.enfant("pts")) {
        std::vector<Pt> v;
        for (const auto& xy : pts->elements)
          if (xy.nom() == "xy") v.push_back(local(xy));
        for (std::size_t i = 0; v.size() > 1 && i < v.size(); ++i) trait(v[i], v[(i + 1) % v.size()]);
      }
    }
  }
  if (enc.vide()) enc = Boite{-NM_PAR_MM, -NM_PAR_MM, NM_PAR_MM, NM_PAR_MM};
  d.encombrement = enc;
  const double hauteur = prom::hauteurEstimee(nomKicad);
  d.corps.largeur = enc.largeur();
  d.corps.profondeur = enc.hauteur();
  d.corps.hauteur = nm(hauteur > 0 ? hauteur : 1.0);
  // L'origine d'une empreinte KiCad est souvent à la broche 1 (traversants, connecteurs) : le corps est posé au
  // centre de l'encombrement (3.4, T5)
  d.corps.decalage = decalageParDefaut(d);
  std::sort(d.pastilles.begin(), d.pastilles.end(), [](const Pastille& a, const Pastille& b) { return a.numero < b.numero || (a.numero == b.numero && a.position < b.position); });
  return d;
}

// Empreinte du catalogue de Prométhée qui porte, au nom normalisé près, exactement l'empreinte KiCad nommée (B6) :
// « Package_SO:SOIC-8_3.9x4.9mm_P1.27mm » → « SOIC-8 », « R_0603_1608Metric » → « R_0603 »,
// « PinHeader_1x04_P2.54mm_Vertical » → « PinHeader_1x04_P2.54 », « DIP-8_W7.62mm » → « DIP-8 ». La table est écrite
// règle par règle ; un nom qui n'y répond pas exactement (« R_0603_HandSolder », « PinHeader_1x04_P2.00mm »,
// « MountingHole_3.2mm_M3_Pad_Via ») ne reçoit rien : l'empreinte KiCad est gardée, jamais une autre géométrie.
std::string empreinteCatalogueProche(const std::string& nomKicad) {
  const std::string n = nomKicad.substr(nomKicad.find(':') == std::string::npos ? 0 : nomKicad.find(':') + 1);
  auto existe = [](const std::string& id) { return genererEmpreinte(id) ? id : std::string(); };
  if (!existe(n).empty()) return n;  // déjà un nom que le générateur lit en entier
  const int taille = static_cast<int>(n.size());
  int a = 0, b = 0, fin = -1;
  double x = 0;
  // Puces CMS : « R_0603_1608Metric » (et C, L, LED, Fuse), rien d'autre derrière
  for (const char* f : {"R", "C", "L", "LED", "Fuse"}) {
    const std::string prefixe = std::string(f) + "_";
    if (n.rfind(prefixe, 0) != 0 || n.size() != prefixe.size() + 15) continue;
    fin = -1;
    if (std::sscanf(n.c_str() + prefixe.size(), "%4d_%4dMetric%n", &a, &b, &fin) == 2 && fin == 15) return existe(prefixe + n.substr(prefixe.size(), 4));
  }
  fin = -1;
  if (std::sscanf(n.c_str(), "SOIC-%d_3.9x%lfmm_P1.27mm%n", &a, &x, &fin) == 2 && fin == taille) return existe("SOIC-" + std::to_string(a));
  fin = -1;
  if (std::sscanf(n.c_str(), "TSSOP-%d_4.4x%lfmm_P0.65mm%n", &a, &x, &fin) == 2 && fin == taille) return existe("TSSOP-" + std::to_string(a));
  fin = -1;
  if (std::sscanf(n.c_str(), "DIP-%d_W7.62mm%n", &a, &fin) == 1 && fin == taille) return existe("DIP-" + std::to_string(a));
  fin = -1;
  if (std::sscanf(n.c_str(), "PinHeader_%dx%d_P2.54mm_Vertical%n", &a, &b, &fin) == 2 && fin == taille) {
    char t[48];
    std::snprintf(t, sizeof t, "PinHeader_%dx%02d_P2.54", a, b);
    return existe(t);
  }
  static const std::map<std::string, std::string> TABLE = {{"SOT-23-3", "SOT-23"},         {"SOT-223-3_TabPin2", "SOT-223"}, {"TO-220-3_Vertical", "TO-220-3"},
                                                           {"D_SOD-123", "D_SOD-123"},     {"D_SOD-323", "D_SOD-323"},       {"D_SMA", "D_SMA"},
                                                           {"D_SMB", "D_SMB"}};
  if (const auto it = TABLE.find(n); it != TABLE.end()) return existe(it->second);
  return {};
}

bool memesPastilles(const DefEmpreinte& a, const DefEmpreinte& b) {
  if (a.pastilles.size() != b.pastilles.size()) return false;
  for (std::size_t i = 0; i < a.pastilles.size(); ++i)
    if (a.pastilles[i].numero != b.pastilles[i].numero || distance(a.pastilles[i].position, b.pastilles[i].position) > 2000 || a.pastilles[i].largeur != b.pastilles[i].largeur ||
        a.pastilles[i].hauteur != b.pastilles[i].hauteur)
      return false;
  return true;
}

}  // namespace

namespace {
std::string mmTexte(Nm v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.3f mm", static_cast<double>(v) * 1e-6);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}

// Règles du projet KiCad : minimums de la carte, classe par défaut, autres classes, motifs et affectations
bool reglesDuProjet(ConceptionElec& c, const std::string& texte, RapportImportKicad& r) {
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(texte);
  } catch (...) {
    r.alertes.push_back("Fichier .kicad_pro illisible : règles relevées sur la carte.");
    return false;
  }
  Regles& R = c.circuit.regles;
  auto mmDe = [](const nlohmann::json& o, const char* cle, Nm& cible) {
    if (o.is_object() && o.contains(cle) && o[cle].is_number() && o[cle].get<double>() > 0) cible = nm(o[cle].get<double>());
  };
  if (j.contains("board") && j["board"].contains("design_settings") && j["board"]["design_settings"].contains("rules")) {
    const auto& ru = j["board"]["design_settings"]["rules"];
    mmDe(ru, "min_track_width", R.largeurMin);
    mmDe(ru, "min_through_hole_diameter", R.percageMin);
    mmDe(ru, "min_via_annular_width", R.anneauMin);
    mmDe(ru, "min_copper_edge_clearance", R.bord);
    mmDe(ru, "min_hole_to_hole", R.ecartTrous);
    mmDe(ru, "min_hole_clearance", R.cuivreTrou);
  }
  if (!j.contains("net_settings")) return true;
  const auto& ns = j["net_settings"];
  if (ns.contains("classes") && ns["classes"].is_array())
    for (const auto& cl : ns["classes"]) {
      const std::string nomClasse = cl.value("name", std::string("Default"));
      if (nomClasse == "Default") {
        mmDe(cl, "clearance", R.isolement);
        mmDe(cl, "track_width", R.largeur);
        mmDe(cl, "via_diameter", R.viaDiametre);
        mmDe(cl, "via_drill", R.viaPercage);
        continue;
      }
      ClasseReseau x;
      x.nom = nomClasse;
      x.isolement = R.isolement;
      x.largeur = R.largeur;
      x.viaDiametre = R.viaDiametre;
      x.viaPercage = R.viaPercage;
      mmDe(cl, "clearance", x.isolement);
      mmDe(cl, "track_width", x.largeur);
      mmDe(cl, "via_diameter", x.viaDiametre);
      mmDe(cl, "via_drill", x.viaPercage);
      R.classes.push_back(x);
    }
  if (ns.contains("netclass_patterns") && ns["netclass_patterns"].is_array())
    for (const auto& m : ns["netclass_patterns"])
      if (m.is_object() && m.contains("pattern") && m.contains("netclass") && m["pattern"].is_string() && m["netclass"].is_string() && m["netclass"].get<std::string>() != "Default")
        R.classeReseau[m["pattern"].get<std::string>()] = m["netclass"].get<std::string>();
  if (ns.contains("netclass_assignments") && ns["netclass_assignments"].is_object())
    for (auto it = ns["netclass_assignments"].begin(); it != ns["netclass_assignments"].end(); ++it) {
      std::string cl;
      if (it.value().is_string()) cl = it.value().get<std::string>();
      else if (it.value().is_array() && !it.value().empty() && it.value()[0].is_string()) cl = it.value()[0].get<std::string>();
      if (!cl.empty() && cl != "Default") R.classeReseau[it.key()] = cl;
    }
  R.largeurMin = std::min(R.largeurMin, R.largeur);
  r.alertes.push_back("Règles et classes de réseaux reprises du projet KiCad (.kicad_pro).");
  return true;
}

// Sans projet KiCad : règles relevées sur la carte routée (le plus fin de ce qui est posé), l'isolement étant le
// plus grand des pas usuels que le cuivre respecte partout
void reglesReleveesSurLaCarte(ConceptionElec& c, RapportImportKicad& r) {
  Regles& R = c.circuit.regles;
  std::map<Nm, int> largeurs, vias;
  for (const auto& p : c.circuit.pistes) ++largeurs[p.largeur];
  if (!largeurs.empty()) {
    R.largeurMin = std::min(R.largeurMin, largeurs.begin()->first);
    R.largeur = std::max_element(largeurs.begin(), largeurs.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
  }
  for (const auto& v : c.circuit.vias) {
    R.percageMin = std::min(R.percageMin, v.percage);
    R.anneauMin = std::min(R.anneauMin, (v.diametre - v.percage) / 2);
    ++vias[v.diametre * 4'000'000'000LL + v.percage];
  }
  if (!c.circuit.vias.empty()) {
    const Nm cle = std::max_element(vias.begin(), vias.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
    R.viaDiametre = cle / 4'000'000'000LL;
    R.viaPercage = cle % 4'000'000'000LL;
  }
  for (const auto& [id, d] : c.empreintes)
    for (const auto& p : d.pastilles)
      if (p.percage > 0) R.percageMin = std::min(R.percageMin, p.percage);
  // Bord et isolement : les pas usuels, du plus large au plus fin, jusqu'au premier que la carte respecte
  const Connectivite k = connectivite(c);
  // (sans les zones : leur remplissage respecte l'isolement par construction)
  const Remplissages sansZones;
  auto respecte = [&](const char* code) {
    const auto v = verifierDRC(c, k, sansZones);
    return std::none_of(v.begin(), v.end(), [code](const Violation& x) { return x.code == code; });
  };
  for (Nm bord : {300'000, 250'000, 200'000, 150'000, 100'000, 50'000, 0}) {
    R.bord = bord;
    if (respecte("DRC-07")) break;
  }
  for (Nm ecart : {250'000, 200'000, 150'000, 100'000, 50'000, 0}) {
    R.cuivreTrou = ecart;
    if (respecte("DRC-15")) break;
  }
  for (Nm ecart : {250'000, 200'000, 150'000, 100'000}) {
    R.ecartTrous = ecart;
    if (respecte("DRC-12")) break;
  }
  for (Nm iso : {200'000, 175'000, 150'000, 127'000, 100'000, 90'000, 75'000}) {
    R.isolement = iso;
    if (respecte("DRC-01")) break;
  }
  r.alertes.push_back("Sans fichier .kicad_pro, règles relevées sur la carte : isolement " + mmTexte(R.isolement) + ", piste " + mmTexte(R.largeur) + " (minimum " + mmTexte(R.largeurMin) +
                      "), via " + mmTexte(R.viaDiametre) + " / " + mmTexte(R.viaPercage) + ", bord " + mmTexte(R.bord) + ", cuivre au bord d’un trou " + mmTexte(R.cuivreTrou) + ". Vérifie-les face au fabricant.");
}
}  // namespace

ConceptionElec conceptionDepuisKicad(const std::string& texte, const std::string& nom, RapportImportKicad* rapport, const std::string& projetKicad) {
  RapportImportKicad r;
  const Expr racine = kicad_interne::Lecteur(texte).lire();
  if (racine.nom() != "kicad_pcb") throw std::runtime_error("Ce fichier n’est pas une carte KiCad (.kicad_pcb).");
  ConceptionElec c = nouvelleConception(nom);
  std::map<std::string, int> ignores;
  // Empilage
  if (const Expr* g = racine.enfant("general"))
    if (const Expr* t = g->enfant("thickness")) c.circuit.empilage.epaisseur = t->nombre(1, 1.6);
  int cuivres = 0;
  if (const Expr* ls = racine.enfant("layers"))
    for (const auto& l : ls->elements)
      if (l.liste && l.valeur(1).size() > 3 && l.valeur(1).compare(l.valeur(1).size() - 3, 3, ".Cu") == 0) ++cuivres;
  // Noms donnés aux couches dans KiCad (« (0 "F.Cu" signal "Dessus") ») : gardés pour le tableau d'association (10.12)
  if (const Expr* ls = racine.enfant("layers"))
    for (const auto& l : ls->elements)
      if (l.liste && !l.valeur(3).empty() && coucheDepuisCode(l.valeur(1))) c.extensions["importCouches"][l.valeur(1)] = l.valeur(1) + " « " + l.valeur(3) + " »";
  // Toutes les couches de cuivre (0.34 : jusqu'à 32)
  c.circuit.empilage.couchesCuivre = std::clamp(cuivres, 1, 32);
  r.couchesCuivre = cuivres;
  if (const Expr* tb = racine.enfant("title_block")) {
    if (const Expr* t = tb->enfant("title")) c.cartouche.titre = t->valeur(1);
    if (const Expr* t = tb->enfant("rev")) c.cartouche.revision = t->valeur(1);
    if (const Expr* t = tb->enfant("company")) c.cartouche.societe = t->valeur(1);
    if (const Expr* t = tb->enfant("date")) c.cartouche.date = t->valeur(1);
  }
  // Réseaux : table numérotée (KiCad 6 à 8), ou nom donné directement
  std::map<std::string, std::string> nets;
  for (const auto& e : racine.elements)
    if (e.nom() == "net") nets[e.valeur(1)] = e.valeur(2);
  auto reseau = [&](const Expr& x) -> std::string {
    const Expr* n = x.enfant("net");
    if (!n) return {};
    std::string v = n->elements.size() >= 3 ? n->valeur(2) : n->valeur(1);
    if (n->elements.size() < 3)
      if (const auto it = nets.find(v); it != nets.end()) v = it->second;
    if (v.rfind("unconnected-", 0) == 0) return {};
    return v;
  };
  // Contour et découpes (Edge.Cuts), en coordonnées de KiCad (Y vers le bas, comme la carte)
  {
    std::vector<ElementContour> morceaux;
    auto P = [](const Expr& e) { return Point{e.nombre(1), e.nombre(2)}; };
    for (const auto& e : racine.elements) {
      const std::string& n = e.nom();
      if (n.rfind("gr_", 0) != 0 || kicad_interne::couche(e) != "Edge.Cuts") continue;
      const Expr* debut = e.enfant("start");
      const Expr* fin = e.enfant("end");
      if (n == "gr_line" && debut && fin) morceaux.push_back({P(*debut), P(*fin), std::nullopt});
      else if (n == "gr_arc" && debut && fin && e.enfant("mid")) morceaux.push_back({P(*debut), P(*fin), P(*e.enfant("mid"))});
      else if (n == "gr_rect" && debut && fin) {
        const Point a = P(*debut), b = P(*fin);
        morceaux.push_back({{a.x, a.y}, {b.x, a.y}, std::nullopt});
        morceaux.push_back({{b.x, a.y}, {b.x, b.y}, std::nullopt});
        morceaux.push_back({{b.x, b.y}, {a.x, b.y}, std::nullopt});
        morceaux.push_back({{a.x, b.y}, {a.x, a.y}, std::nullopt});
      } else if (n == "gr_circle" && e.enfant("center") && fin) {
        const Point q = P(*e.enfant("center")), f2 = P(*fin);
        const double rr = std::hypot(f2.x - q.x, f2.y - q.y);
        morceaux.push_back({{q.x + rr, q.y}, {q.x - rr, q.y}, Point{q.x, q.y + rr}});
        morceaux.push_back({{q.x - rr, q.y}, {q.x + rr, q.y}, Point{q.x, q.y - rr}});
      } else if (n == "gr_poly") {
        if (const Expr* pts = e.enfant("pts")) {
          std::vector<Point> p;
          for (const auto& xy : pts->elements)
            if (xy.nom() == "xy") p.push_back(P(xy));
          for (std::size_t i = 0; p.size() > 1 && i < p.size(); ++i) morceaux.push_back({p[i], p[(i + 1) % p.size()], std::nullopt});
        }
      } else if (n == "gr_curve") {
        // Courbe de Bézier cubique : découpée en 16 cordes
        if (const Expr* pts = e.enfant("pts")) {
          std::vector<Point> q;
          for (const auto& xy : pts->elements)
            if (xy.nom() == "xy") q.push_back(P(xy));
          if (q.size() == 4)
            for (int k = 0; k < 16; ++k) {
              auto b = [&](double t) {
                const double u = 1 - t;
                return Point{u * u * u * q[0].x + 3 * u * u * t * q[1].x + 3 * u * t * t * q[2].x + t * t * t * q[3].x, u * u * u * q[0].y + 3 * u * u * t * q[1].y + 3 * u * t * t * q[2].y + t * t * t * q[3].y};
              };
              morceaux.push_back({b(k / 16.0), b((k + 1) / 16.0), std::nullopt});
            }
        }
      } else
        ++ignores["tracé de contour " + n];
    }
    const auto contours = kicad_interne::chainer(morceaux);
    bool premier = true;
    for (const auto& ct : contours) {
      if (!ct.ferme) {
        r.alertes.push_back("Contour Edge.Cuts ouvert ignoré.");
        continue;
      }
      Polygone poly;
      for (const Point& q : ct.polygone(0.1)) {
        const Pt x = ptMm(q.x, q.y);
        if (poly.empty() || !(poly.back() == x)) poly.push_back(x);
      }
      while (poly.size() > 1 && poly.front() == poly.back()) poly.pop_back();
      if (poly.size() < 3) continue;
      if (premier) {
        c.circuit.contour.libre = orienter(poly);
        premier = false;
      } else if (dedans(c.circuit.contour.libre, poly.front()))
        c.circuit.contour.decoupes.push_back(orienter(poly));
      else
        r.alertes.push_back("Contour Edge.Cuts hors de la carte ignoré.");
    }
    if (premier) r.alertes.push_back("Aucun contour fermé sur Edge.Cuts : la carte garde un rectangle par défaut.");
  }
  // Empreintes : définitions figées dans le projet (une par empreinte KiCad, une variante si la géométrie diffère),
  // composants et placement
  struct Instance {
    std::string composant, reference;
    std::vector<BrocheReseau> broches;
  };
  std::vector<Instance> instances;
  std::set<std::string> surCarteSeulement;  // empreintes « board_only » : pas de symbole de liaison (X7)
  for (const auto& e : racine.elements) {
    if (e.nom() != "footprint" && e.nom() != "module") continue;
    const std::string nomKicad = e.valeur(1);
    EmpreintePlacee em;
    em.composant = nouvelUuid();
    em.dessous = kicad_interne::couche(e) == "B.Cu";
    if (const Expr* at = e.enfant("at")) {
      em.position = kp(*at);
      em.rotation = at->nombre(3, 0);
    }
    em.verrou = e.enfant("locked") != nullptr || std::any_of(e.elements.begin(), e.elements.end(), [](const Expr& x) { return !x.liste && x.atome == "locked"; });
    ComposantElec comp;
    comp.uuid = em.composant;
    for (const auto& s : e.elements) {
      if (s.nom() == "property") {
        if (s.valeur(1) == "Reference") comp.reference = s.valeur(2);
        else if (s.valeur(1) == "Value") comp.valeur = s.valeur(2);
        else if (s.valeur(1) == "Datasheet" && s.valeur(2) != "~") comp.fiche = s.valeur(2);
        else if (s.valeur(1) == "Description") comp.description = s.valeur(2);
        else if (s.valeur(1) == "MPN" || s.valeur(1) == "Manufacturer_Part_Number") comp.mpn = s.valeur(2);
        else if (s.valeur(1) == "Manufacturer") comp.fabricant = s.valeur(2);
      } else if (s.nom() == "fp_text") {
        if (s.valeur(1) == "reference") comp.reference = s.valeur(2);
        else if (s.valeur(1) == "value") comp.valeur = s.valeur(2);
      } else if (s.nom() == "attr") {
        // Attributs d'exclusion (X7) : une mire, un logo ou un trou métallisé ne finit plus ni dans la nomenclature
        // ni dans le fichier de placement ; « board_only » : sur la carte seulement, sans symbole de liaison
        for (const auto& a : s.elements) {
          if (a.liste) continue;
          if (a.atome == "dnp") comp.nePasMonter = true;
          else if (a.atome == "exclude_from_bom") comp.horsNomenclature = true;
          else if (a.atome == "exclude_from_pos_files") comp.horsPlacement = true;
          else if (a.atome == "board_only") surCarteSeulement.insert(comp.uuid);
        }
      }
    }
    if (comp.reference.empty()) comp.reference = "?";
    Instance inst;
    inst.composant = em.composant;
    inst.reference = comp.reference;
    DefEmpreinte d = definitionDepuisKicad(e, em, nomKicad, ignores, reseau, inst.broches, c.circuit.empilage.couchesCuivre);
    r.pastilles += static_cast<int>(d.pastilles.size());
    // Une empreinte sans pastille ni dessin (repère de montage vide) est gardée : son corps et sa place comptent
    // Identifiant : celui de KiCad, une variante si une autre géométrie porte déjà ce nom
    std::string id = "kicad:" + nomKicad;
    for (int variante = 2; c.empreintes.count(id) && !memesPastilles(c.empreintes.at(id), d); ++variante) id = "kicad:" + nomKicad + "#" + std::to_string(variante);
    d.id = id;
    if (!c.empreintes.count(id)) c.empreintes[id] = d;
    em.empreinte = id;
    comp.empreinte = id;
    c.composants.push_back(comp);
    c.circuit.empreintes.push_back(em);
    instances.push_back(inst);
    ++r.empreintes;
  }
  // Pistes (arcs découpés en segments), vias, zones, textes de sérigraphie
  for (const auto& e : racine.elements) {
    const std::string n = e.nom();
    if (n == "segment" || n == "arc") {
      const auto cl = coucheKicad(kicad_interne::couche(e));
      const Nm largeur = nm(e.enfant("width") ? e.enfant("width")->nombre(1, 0.25) : 0.25);
      if (cl && !estCuivre(*cl) && e.enfant("start") && e.enfant("end")) {
        // Piste sur une couche technique : un dessin de la carte (10.1)
        DessinCircuit d;
        d.uuid = nouvelUuid();
        d.couche = *cl;
        d.epaisseur = largeur;
        if (n == "arc" && e.enfant("mid")) {
          d.forme = DessinCircuit::Forme::Arc;
          d.points = {kp(*e.enfant("start")), kp(*e.enfant("mid")), kp(*e.enfant("end"))};
        } else
          d.points = {kp(*e.enfant("start")), kp(*e.enfant("end"))};
        c.circuit.dessins.push_back(d);
        continue;
      }
      if (!cl) { ++ignores["piste sur une couche inconnue"]; continue; }
      const std::string res = reseau(e);
      if (n == "segment" && e.enfant("start") && e.enfant("end")) {
        if (!(kp(*e.enfant("start")) == kp(*e.enfant("end")))) c.circuit.pistes.push_back({nouvelUuid(), res, *cl, kp(*e.enfant("start")), kp(*e.enfant("end")), largeur});
      } else if (n == "arc" && e.enfant("start") && e.enfant("mid") && e.enfant("end")) {
        // Arc de piste (8.4) : gardé en arc, de « start » à « end » par « mid »
        Piste t{nouvelUuid(), res, *cl, kp(*e.enfant("start")), kp(*e.enfant("end")), largeur};
        t.milieu = kp(*e.enfant("mid"));
        c.circuit.pistes.push_back(t);
      }
      ++r.pistes;
    } else if (n == "via") {
      Via v;
      v.uuid = nouvelUuid();
      if (const Expr* at = e.enfant("at")) v.p = kp(*at);
      if (const Expr* t = e.enfant("size")) v.diametre = nm(t->nombre(1, 0.8));
      if (const Expr* t = e.enfant("drill")) v.percage = nm(t->nombre(1, 0.4));
      v.reseau = reseau(e);
      // Vias borgnes, enterrées et micro-vias (8.22) : leurs couches de départ et d'arrivée
      if (const Expr* ls = e.enfant("layers"); ls && ls->elements.size() >= 3)
        if (const auto a = coucheKicad(ls->valeur(1)), b = coucheKicad(ls->valeur(2)); a && b && estCuivre(*a) && estCuivre(*b)) {
          v.de = *a;
          v.vers = *b;
        }
      if (e.valeur(1) == "micro") {
        v.type = TypeVia::Micro;
        v.laser = true;
      } else if (e.valeur(1) == "blind") {
        const bool exterieure = v.de == Couche::CuivreDessus || v.de == Couche::CuivreDessous || v.vers == Couche::CuivreDessus || v.vers == Couche::CuivreDessous;
        v.type = exterieure ? TypeVia::Borgne : TypeVia::Enterree;
      }
      c.circuit.vias.push_back(v);
      ++r.vias;
    } else if (n == "zone") {
      if (const Expr* ko = e.enfant("keepout")) {
        // Zone d'interdiction (keepout) : une zone de règles (5.4) avec ses interdictions et ses couches
        ZoneRegle zr;
        zr.uuid = nouvelUuid();
        if (const Expr* nn = e.enfant("name")) zr.nom = nn->valeur(1);
        if (zr.nom.empty()) zr.nom = "Interdiction KiCad";
        auto interdit = [&](const char* quoi) {
          const Expr* x = ko->enfant(quoi);
          return x && x->valeur(1) == "not_allowed";
        };
        zr.interditPistes = interdit("tracks");
        zr.interditVias = interdit("vias");
        zr.interditPastilles = interdit("pads");
        zr.interditZones = interdit("copperpour");
        zr.interditEmpreintes = interdit("footprints");
        if (const auto cl = coucheKicad(kicad_interne::couche(e)); cl && estCuivre(*cl)) zr.couches |= bit(*cl);
        if (const Expr* ls = e.enfant("layers"))
          for (std::size_t i = 1; i < ls->elements.size(); ++i) {
            const std::string l = ls->valeur(i);
            if (l == "*.Cu" || l == "F&B.Cu") zr.couches = 0;
            else if (const auto cl = coucheKicad(l); cl && estCuivre(*cl)) zr.couches |= bit(*cl);
          }
        if (const Expr* poly = e.enfant("polygon"))
          if (const Expr* pts = poly->enfant("pts"))
            for (const auto& xy : pts->elements)
              if (xy.nom() == "xy") zr.contour.push_back(kp(xy));
        if (zr.contour.size() >= 3) c.circuit.zonesRegles.push_back(zr);
        continue;
      }
      std::vector<Couche> couches;
      if (const auto cl = coucheKicad(kicad_interne::couche(e)); cl && estCuivre(*cl)) couches.push_back(*cl);
      if (const Expr* ls = e.enfant("layers"))
        for (std::size_t i = 1; i < ls->elements.size(); ++i)
          if (const auto cl = coucheKicad(ls->valeur(i)); cl && estCuivre(*cl)) couches.push_back(*cl);
      const Expr* poly = e.enfant("polygon");
      const Expr* pts = poly ? poly->enfant("pts") : nullptr;
      if (couches.empty() || !pts) { ++ignores["zone sans couche de cuivre ou sans contour"]; continue; }
      Zone z;
      z.reseau = reseau(e);
      if (z.reseau.empty())
        if (const Expr* nn = e.enfant("net_name"); nn && nn->valeur(1).rfind("unconnected-", 0) != 0) z.reseau = nn->valeur(1);
      for (const auto& xy : pts->elements)
        if (xy.nom() == "xy") z.contour.push_back(kp(xy));
      if (const Expr* p = e.enfant("priority")) z.priorite = static_cast<int>(p->nombre(1, 0));
      if (const Expr* cp = e.enfant("connect_pads")) {
        if (const Expr* cl = cp->enfant("clearance")) z.isolement = nm(cl->nombre(1, 0));
        z.thermique = cp->valeur(1) != "yes";
      }
      if (const Expr* t = e.enfant("min_thickness")) z.epaisseurMin = nm(t->nombre(1, 0.25));
      if (const Expr* f = e.enfant("fill")) {
        if (const Expr* g = f->enfant("thermal_gap")) z.ecartThermique = nm(g->nombre(1, 0.5));
        if (const Expr* w = f->enfant("thermal_bridge_width")) z.largeurRayon = nm(w->nombre(1, 0.5));
      }
      if (z.contour.size() < 3) continue;
      for (Couche x : couches) {
        z.uuid = nouvelUuid();
        z.couche = x;
        c.circuit.zones.push_back(z);
      }
      ++r.zones;
    } else if (n == "gr_text") {
      const auto cl = coucheKicad(kicad_interne::couche(e));
      if (!cl || *cl == Couche::Contour) { ++ignores["texte sur une couche inconnue ou sur le contour"]; continue; }
      TexteCircuit t;
      t.uuid = nouvelUuid();
      t.texte = e.valeur(1);
      // « \n » de KiCad : saut de ligne (texte sur plusieurs lignes)
      for (std::size_t i = t.texte.find("\\n"); i != std::string::npos; i = t.texte.find("\\n", i + 1)) t.texte.replace(i, 2, "\n");
      t.couche = *cl;
      if (const Expr* at = e.enfant("at")) {
        t.p = kp(*at);
        // Angle KiCad (sens direct à l'écran) = angle Prométhée depuis la 0.29 (même sens que les empreintes, X3)
        t.rotation = at->nombre(3, 0);
      }
      if (const Expr* ef = e.enfant("effects"))
        if (const Expr* fo = ef->enfant("font"))
          if (const Expr* sz = fo->enfant("size")) t.hauteur = nm(sz->nombre(1, 1.0));
      c.circuit.textes.push_back(t);
      ++r.textes;
    } else if (n.rfind("gr_", 0) == 0 && kicad_interne::couche(e) != "Edge.Cuts") {
      // Dessins de la carte (5.31) sur leur couche : traits, arcs, cercles, rectangles, polygones, courbes
      const auto cl = coucheKicad(kicad_interne::couche(e));
      if (!cl) { ++ignores["dessin sur une couche inconnue"]; continue; }
      DessinCircuit d;
      d.uuid = nouvelUuid();
      d.couche = *cl;
      d.epaisseur = nm(largeurTrait(e, 0.15));
      if (const Expr* f = e.enfant("fill")) d.plein = f->valeur(1) == "solid" || f->valeur(1) == "yes";
      const Expr* debut = e.enfant("start");
      const Expr* fin = e.enfant("end");
      if (n == "gr_line" && debut && fin) d.points = {kp(*debut), kp(*fin)};
      else if (n == "gr_arc" && debut && fin && e.enfant("mid")) {
        d.forme = DessinCircuit::Forme::Arc;
        d.points = {kp(*debut), kp(*e.enfant("mid")), kp(*fin)};
      } else if (n == "gr_arc" && debut && fin && e.enfant("angle")) {
        const auto a3 = arcKicad5(kp(*debut), kp(*fin), e.enfant("angle")->nombre(1));
        d.forme = DessinCircuit::Forme::Arc;
        d.points = {a3[0], a3[1], a3[2]};
      } else if (n == "gr_circle" && e.enfant("center") && fin) {
        d.forme = DessinCircuit::Forme::Cercle;
        d.points = {kp(*e.enfant("center")), kp(*fin)};
      } else if (n == "gr_rect" && debut && fin) {
        d.forme = DessinCircuit::Forme::Rectangle;
        d.points = {kp(*debut), kp(*fin)};
      } else if ((n == "gr_poly" || n == "gr_curve") && e.enfant("pts")) {
        d.forme = n == "gr_poly" ? DessinCircuit::Forme::Polygone : DessinCircuit::Forme::Bezier;
        for (const auto& xy : e.enfant("pts")->elements)
          if (xy.nom() == "xy") d.points.push_back(kp(xy));
      } else {
        ++ignores["dessin " + n + " sans géométrie lisible"];
        continue;
      }
      if (d.points.size() >= 2) c.circuit.dessins.push_back(d);
    } else if (n == "dimension") {
      // Cote (5.15) : points mesurés, genre, couche
      Cote k;
      k.uuid = nouvelUuid();
      if (const auto cl = coucheKicad(kicad_interne::couche(e))) k.couche = *cl;
      const std::string genre = e.enfant("type") ? e.enfant("type")->valeur(1) : std::string("aligned");
      k.genre = genre == "orthogonal" ? GenreCote::Horizontale : genre == "radial" ? GenreCote::Rayon : genre == "center" ? GenreCote::Centre : GenreCote::Alignee;
      if (const Expr* pts = e.enfant("pts"))
        for (const auto& xy : pts->elements)
          if (xy.nom() == "xy") k.points.push_back(kp(xy));
      if (const Expr* h = e.enfant("height")) k.decalage = nm(h->nombre(1, 2));
      if (k.points.size() >= 2) c.circuit.cotes.push_back(k);
      else ++ignores["cote sans points"];
    }
  }
  // Schéma de liaison : un symbole par composant (broches = numéros de pastilles), chaque broche prolongée d'un
  // fil et d'une étiquette globale au nom de son réseau ; les broches sans réseau reçoivent une marque de
  // non-connexion
  {
    std::set<std::string> reseaux;
    std::sort(instances.begin(), instances.end(), [](const Instance& a, const Instance& b) { return a.reference < b.reference; });
    c.feuilles.front().nom = "Liaisons importées de KiCad";
    c.feuilles.front().format = "A3";
    const Boite cadre = cadreFeuille("A3");
    const Nm marge = 25 * NM_PAR_MM;
    Pt curseur{marge, marge};
    Nm hauteurLigne = 0;
    std::size_t feuille = 0;
    for (const Instance& inst : instances) {
      ComposantElec* comp = c.composant(inst.composant);
      if (!comp) continue;
      SpecSymbole spec;
      spec.nom = "KiCad " + comp->empreinte.substr(6);
      spec.id = identifiantSymbole(spec.nom);
      spec.prefixe = comp->reference.substr(0, comp->reference.find_first_of("0123456789?"));
      if (spec.prefixe.empty()) spec.prefixe = "U";
      spec.categorie = "Importés de KiCad";
      spec.empreinte = comp->empreinte;
      for (const auto& b : inst.broches) {
        SpecBroche sb;
        sb.numero = b.numero;
        sb.nom = b.nom.empty() ? b.numero : b.nom;
        sb.type = TypeBroche::Passive;
        spec.broches.push_back(sb);
      }
      if (spec.broches.empty() || surCarteSeulement.count(comp->uuid)) continue;  // trou de fixation, logo, « board_only » : sur la carte, sans symbole
      DefSymbole def = genererSymbole(spec);
      // Même nom, autres broches : symbole propre à ce composant
      if (c.symboles.count(def.id) && c.symboles.at(def.id).broches.size() != def.broches.size()) def.id += "-" + comp->reference;
      if (!c.symboles.count(def.id)) c.symboles[def.id] = def;
      comp->symbole = def.id;
      SymbolePlace sp;
      sp.uuid = nouvelUuid();
      sp.symbole = def.id;
      sp.composant = comp->uuid;
      sp.position = surGrille(curseur);
      Boite b = boiteSymbole(sp, def);
      // Place réservée : le corps et les étiquettes de part et d'autre
      const Nm largeur = b.largeur() + 50 * NM_PAR_MM, hauteur = b.hauteur() + 15 * NM_PAR_MM;
      if (curseur.x + largeur > cadre.x1 - marge) {
        curseur = {marge, curseur.y + hauteurLigne};
        hauteurLigne = 0;
      }
      if (curseur.y + hauteur > cadre.y1 - marge) {
        Feuille f;
        f.uuid = nouvelUuid();
        f.nom = "Liaisons importées de KiCad (" + std::to_string(c.feuilles.size() + 1) + ")";
        f.format = "A3";
        c.feuilles.push_back(f);
        feuille = c.feuilles.size() - 1;
        curseur = {marge, marge};
        hauteurLigne = 0;
      }
      sp.position = surGrille(curseur + Pt{25 * NM_PAR_MM - (b.x0 - sp.position.x), -(b.y0 - sp.position.y)});
      Feuille& F = c.feuilles[feuille];
      F.symboles.push_back(sp);
      for (const auto& bp : brochesPlacees(sp, def)) {
        std::string res;
        for (const auto& x : inst.broches)
          if (x.numero == bp.numero) res = x.reseau;
        if (res.empty()) {
          F.nonConnexions.push_back({bp.p, uuidDerive(F.uuid + ":nc:" + std::to_string(bp.p.x) + ":" + std::to_string(bp.p.y))});
          continue;
        }
        reseaux.insert(res);
        static const Pt dirs[4] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        const Pt v = dirs[((bp.direction % 4) + 4) % 4];
        const Pt q{bp.p.x - v.x * 2 * GRILLE_SCHEMA, bp.p.y - v.y * 2 * GRILLE_SCHEMA};
        F.fils.push_back({nouvelUuid(), bp.p, q});
        Etiquette et;
        et.uuid = nouvelUuid();
        et.texte = res;
        et.p = q;
        et.type = TypeEtiquette::Globale;
        const int dehors = (((bp.direction % 4) + 4) % 4 + 2) % 4;  // direction vers l'extérieur du corps
        et.rotation = dehors == 0 ? 0 : dehors == 2 ? 2 : dehors == 3 ? 1 : 3;
        F.etiquettes.push_back(et);
      }
      curseur.x += largeur;
      hauteurLigne = std::max(hauteurLigne, hauteur);
    }
    r.reseaux = static_cast<int>(reseaux.size());
  }
  // Les composants sans broche (trous, logos) n'ont pas de symbole : ils restent sur la carte, hors nomenclature
  for (auto& comp : c.composants)
    if (comp.symbole.empty()) comp.nePasMonter = comp.nePasMonter || comp.reference.rfind("H", 0) == 0;
  synchroniserReseaux(c);
  if (projetKicad.empty() || !reglesDuProjet(c, projetKicad, r)) reglesReleveesSurLaCarte(c, r);
  for (const auto& [quoi, n] : ignores) r.alertes.push_back(std::to_string(n) + " × " + quoi);
  if (rapport) *rapport = r;
  return c;
}

std::optional<DefEmpreinte> empreinteDepuisKicadMod(const std::string& texte, std::vector<std::string>* alertes) {
  Expr racine;
  try {
    racine = kicad_interne::Lecteur(texte).lire();
  } catch (const std::exception& e) {
    if (alertes) alertes->push_back(e.what());
    return std::nullopt;
  }
  if (racine.nom() != "footprint" && racine.nom() != "module") {
    if (alertes) alertes->push_back("Ce fichier n’est pas une empreinte KiCad (.kicad_mod).");
    return std::nullopt;
  }
  const EmpreintePlacee origine;
  std::map<std::string, int> ignores;
  std::vector<BrocheReseau> broches;
  const std::string nom = racine.valeur(1);
  DefEmpreinte d = definitionDepuisKicad(racine, origine, nom, ignores, nullptr, broches);
  if (alertes)
    for (const auto& [quoi, n] : ignores) alertes->push_back(std::to_string(n) + " × " + quoi);
  if (d.pastilles.empty()) {
    if (alertes) alertes->push_back("L’empreinte « " + nom + " » n’a aucune pastille de cuivre.");
    return std::nullopt;
  }
  if (const Expr* desc = racine.enfant("descr")) d.description = desc->valeur(1);
  d.id = "kicad:" + nom;
  return d;
}

std::vector<DefSymbole> symbolesDepuisKicadSym(const std::string& texte, std::vector<std::string>* alertes) {
  std::vector<DefSymbole> r;
  Expr racine;
  try {
    racine = kicad_interne::Lecteur(texte).lire();
  } catch (const std::exception& e) {
    if (alertes) alertes->push_back(e.what());
    return r;
  }
  if (racine.nom() != "kicad_symbol_lib") {
    if (alertes) alertes->push_back("Ce fichier n’est pas une bibliothèque de symboles KiCad (.kicad_sym).");
    return r;
  }
  struct BrocheKicad {
    SpecBroche spec;
    double x = 0, y = 0;
    int unite = 0;  // 0 : commune à toutes les unités
  };
  struct Lu {
    std::string nom, extends, prefixe = "U", valeur, empreinte, fiche, description, motsCles;
    bool alimentation = false;
    std::vector<BrocheKicad> broches;
  };
  auto typeKicad = [](const std::string& t) {
    if (t == "input") return TypeBroche::Entree;
    if (t == "output") return TypeBroche::Sortie;
    if (t == "bidirectional") return TypeBroche::Bidirectionnelle;
    if (t == "tri_state") return TypeBroche::TroisEtats;
    if (t == "power_in") return TypeBroche::AlimEntree;
    if (t == "power_out") return TypeBroche::AlimSortie;
    if (t == "open_collector" || t == "open_emitter") return TypeBroche::CollecteurOuvert;
    if (t == "no_connect") return TypeBroche::NonConnectee;
    if (t == "unspecified" || t == "free") return TypeBroche::NonSpecifiee;
    return TypeBroche::Passive;
  };
  std::map<std::string, Lu> lus;
  std::vector<std::string> ordre;
  for (const auto& e : racine.elements) {
    if (e.nom() != "symbol") continue;
    Lu l;
    l.nom = e.valeur(1);
    for (const auto& x : e.elements) {
      if (x.nom() == "extends") l.extends = x.valeur(1);
      else if (x.nom() == "power") l.alimentation = true;
      else if (x.nom() == "property") {
        const std::string cle = x.valeur(1), v = x.valeur(2);
        if (cle == "Reference") l.prefixe = v;
        else if (cle == "Value") l.valeur = v;
        else if (cle == "Footprint") l.empreinte = v;
        else if (cle == "Datasheet" && v != "~") l.fiche = v;
        else if (cle == "Description" || cle == "ki_description") l.description = v;
        else if (cle == "ki_keywords") l.motsCles = v;
      } else if (x.nom() == "symbol") {
        // Sous-symbole « NOM_U_S » : unité U (0 : commune), style S (2 : variante De Morgan, laissée)
        int unite = 0, style = 1;
        {
          const std::string sn = x.valeur(1);
          const auto p2 = sn.rfind('_');
          const auto p1 = p2 == std::string::npos || p2 == 0 ? std::string::npos : sn.rfind('_', p2 - 1);
          if (p1 != std::string::npos) {
            try {
              unite = std::stoi(sn.substr(p1 + 1, p2 - p1 - 1));
              style = std::stoi(sn.substr(p2 + 1));
            } catch (const std::exception&) {
              unite = 0;
              style = 1;
            }
          }
        }
        if (style == 2) continue;
        for (const auto& pin : x.elements) {
          if (pin.nom() != "pin") continue;
          BrocheKicad b;
          b.spec.type = typeKicad(pin.valeur(1));
          if (const Expr* n = pin.enfant("name")) b.spec.nom = n->valeur(1) == "~" ? std::string() : n->valeur(1);
          if (const Expr* n = pin.enfant("number")) b.spec.numero = n->valeur(1);
          if (const Expr* at = pin.enfant("at")) {
            b.x = at->nombre(1);
            b.y = at->nombre(2);
            const int angle = static_cast<int>(std::lround(at->nombre(3, 0))) % 360;
            // L'angle pointe de la connexion vers le corps : 0 → broche à gauche, 180 → à droite, 90 → en bas, 270 → en haut
            b.spec.cote = angle == 0 ? 0 : angle == 180 ? 1 : angle == 90 ? 3 : 2;
          }
          if (b.spec.numero.empty()) continue;
          b.unite = std::max(0, unite);
          bool deja = false;
          for (const auto& y : l.broches) deja |= y.spec.numero == b.spec.numero && y.unite == b.unite;
          if (!deja) l.broches.push_back(b);
        }
      }
    }
    ordre.push_back(l.nom);
    lus[l.nom] = std::move(l);
  }
  int alims = 0, multi = 0;
  for (const auto& nom : ordre) {
    Lu l = lus[nom];
    for (int k = 0; k < 4 && !l.extends.empty() && l.broches.empty(); ++k) {
      const auto it = lus.find(l.extends);
      if (it == lus.end()) break;
      l.broches = it->second.broches;
      if (l.empreinte.empty()) l.empreinte = it->second.empreinte;
      if (l.description.empty()) l.description = it->second.description;
      l.extends = it->second.extends;
    }
    if (l.alimentation) { ++alims; continue; }
    if (l.broches.empty()) {
      if (alertes) alertes->push_back("Symbole « " + nom + " » sans broche : ignoré.");
      continue;
    }
    // Ordre sur chaque côté : de haut en bas à gauche et à droite, de gauche à droite en haut et en bas (Y de KiCad vers le haut)
    std::stable_sort(l.broches.begin(), l.broches.end(), [](const BrocheKicad& a, const BrocheKicad& b) {
      if (a.spec.cote != b.spec.cote) return a.spec.cote < b.spec.cote;
      return a.spec.cote <= 1 ? a.y > b.y : a.x < b.x;
    });
    SpecSymbole s;
    s.id = "kicad:" + nom;
    s.nom = nom;
    s.prefixe = l.prefixe.empty() || l.prefixe == "~" ? std::string("U") : l.prefixe;
    s.categorie = "Importés de KiCad";
    s.description = l.description;
    s.motsCles = l.motsCles;
    s.fiche = l.fiche;
    // Empreinte : celle du catalogue de Prométhée quand son nom y est (« Package_SO:SOIC-8… » → « SOIC-8 »),
    // sinon le nom KiCad, à importer (.kicad_mod)
    const std::string proche = l.empreinte.empty() ? std::string() : empreinteCatalogueProche(l.empreinte);
    s.empreinte = l.empreinte.empty() ? std::string() : !proche.empty() ? proche : "kicad:" + l.empreinte;
    // Unités : genererSymbole fait un corps par unité quand il y en a plusieurs (U1A, U1B…)
    std::set<int> unites;
    for (const auto& b : l.broches) {
      SpecBroche sb = b.spec;
      sb.unite = b.unite;
      s.broches.push_back(sb);
      if (b.unite > 0) unites.insert(b.unite);
    }
    if (unites.size() > 1) ++multi;
    r.push_back(genererSymbole(s));
  }
  if (alertes) {
    if (alims) alertes->push_back(std::to_string(alims) + " symbole(s) d’alimentation laissé(s) : ceux de Prométhée (GND, +5V…) les remplacent.");
    if (multi) alertes->push_back(std::to_string(multi) + " symbole(s) à plusieurs unités : chaque unité se pose à part (U1A, U1B…), les broches communes figurent sur chacune.");
  }
  return r;
}

namespace {
std::string nbK(double v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.6f", v);
  std::string r = t;
  while (!r.empty() && r.back() == '0') r.pop_back();
  if (!r.empty() && r.back() == '.') r.pop_back();
  return r == "-0" ? "0" : r;
}
std::string ptK(Pt p) { return nbK(mm(p.x)) + " " + nbK(mm(p.y)); }
std::string chaineK(const std::string& t) {
  std::string r = "\"";
  for (char ch : t) {
    if (ch == '\n') {
      r += "\\n";
      continue;
    }
    if (ch == '"' || ch == '\\') r += '\\';
    r += ch;
  }
  return r + "\"";
}
const char* nomKicad(Couche c) {
  // Les codes des couches sont ceux de KiCad
  return codeCouche(c);
}
}  // namespace

std::string kicadDepuisConception(const ConceptionElec& c, const Connectivite& k) {
  std::string o;
  const int nc = c.circuit.empilage.couchesCuivre;
  o += "(kicad_pcb (version 20221018) (generator promethee)\n";
  o += "  (general (thickness " + nbK(c.circuit.empilage.epaisseur) + "))\n  (paper \"A4\")\n";
  o += "  (title_block (title " + chaineK(c.cartouche.titre.empty() ? c.nom : c.cartouche.titre) + ") (rev " + chaineK(c.cartouche.revision) + ")";
  if (!c.cartouche.societe.empty()) o += " (company " + chaineK(c.cartouche.societe) + ")";
  if (!c.cartouche.date.empty()) o += " (date " + chaineK(c.cartouche.date) + ")";
  o += ")\n  (layers\n    (0 \"F.Cu\" signal)\n";
  // Cuivres internes (0.34 : jusqu'à 30), numérotés comme KiCad
  for (int i = 1; i + 1 < nc && i <= 30; ++i) o += "    (" + std::to_string(i) + " \"In" + std::to_string(i) + ".Cu\" signal)\n";
  o += "    (31 \"B.Cu\" signal)\n    (32 \"B.Adhes\" user \"B.Adhesive\")\n    (33 \"F.Adhes\" user \"F.Adhesive\")\n    (34 \"B.Paste\" user)\n    (35 \"F.Paste\" user)\n    (36 \"B.SilkS\" user \"B.Silkscreen\")\n    (37 \"F.SilkS\" user \"F.Silkscreen\")\n"
       "    (38 \"B.Mask\" user)\n    (39 \"F.Mask\" user)\n    (44 \"Edge.Cuts\" user)\n    (46 \"B.CrtYd\" user \"B.Courtyard\")\n    (47 \"F.CrtYd\" user \"F.Courtyard\")\n"
       "    (48 \"B.Fab\" user)\n    (49 \"F.Fab\" user)\n    (40 \"Dwgs.User\" user \"User.Drawings\")\n    (41 \"Cmts.User\" user \"User.Comments\")\n  )\n";
  o += "  (setup (pad_to_mask_clearance " + nbK(mm(c.circuit.fabrication.margeVernis)) + "))\n";
  // Réseaux : numéros dans l'ordre alphabétique ; 0 = sans réseau
  std::map<std::string, int> numeros;
  {
    std::set<std::string> noms;
    for (const auto& r : k.reseaux)
      if (!r.nom.empty()) noms.insert(r.nom);
    for (const auto& p : c.circuit.pistes) if (!p.reseau.empty()) noms.insert(p.reseau);
    for (const auto& v : c.circuit.vias) if (!v.reseau.empty()) noms.insert(v.reseau);
    for (const auto& z : c.circuit.zones) if (!z.reseau.empty()) noms.insert(z.reseau);
    o += "  (net 0 \"\")\n";
    int n = 0;
    for (const auto& nom : noms) {
      numeros[nom] = ++n;
      o += "  (net " + std::to_string(n) + " " + chaineK(nom) + ")\n";
    }
  }
  auto net = [&](const std::string& nom) {
    const auto it = numeros.find(nom);
    return it == numeros.end() ? std::string("(net 0 \"\")") : "(net " + std::to_string(it->second) + " " + chaineK(nom) + ")";
  };
  // Réseau de chaque pastille posée
  std::map<std::pair<int, std::string>, std::string> reseauPastille;
  for (const auto& p : pastilles(c, k)) reseauPastille[{p.empreinte, p.pastille.numero}] = p.reseau;
  for (std::size_t ie = 0; ie < c.circuit.empreintes.size(); ++ie) {
    const EmpreintePlacee& e = c.circuit.empreintes[ie];
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    const ComposantElec* x = c.composant(e.composant);
    if (!d) continue;
    const std::string face = e.dessous ? "B" : "F";
    // Coordonnées de KiCad dans l'empreinte : la position sur la carte ramenée par la seule rotation (la face
    // dessous est déjà retournée dans le fichier)
    auto locK = [&](Pt local) { return tournerDegres(versCarte(e, local) - e.position, -e.rotation); };
    auto coucheK = [&](Couche cl) { return std::string(nomKicad(coucheSurCarte(e, cl))); };
    const std::string nomE = e.empreinte.rfind("kicad:", 0) == 0 ? e.empreinte.substr(6) : "Promethee:" + e.empreinte;
    o += "  (footprint " + chaineK(nomE) + " (layer \"" + face + ".Cu\")" + (e.verrou ? " (locked)" : "") + "\n";
    o += "    (at " + ptK(e.position) + (std::abs(e.rotation) > 1e-9 ? " " + nbK(e.rotation) : std::string()) + ")\n";
    const Boite enc = d->encombrement;
    o += "    (property \"Reference\" " + chaineK(x ? x->reference : std::string("?")) + " (at 0 " + nbK(mm(enc.y0) - 1) + " " + nbK(e.rotation) + ") (layer \"" + face +
         ".SilkS\") (effects (font (size 1 1) (thickness 0.15))" + (e.dessous ? " (justify mirror)" : "") + "))\n";
    o += "    (property \"Value\" " + chaineK(x ? x->valeur : std::string()) + " (at 0 " + nbK(mm(enc.y1) + 1) + " " + nbK(e.rotation) + ") (layer \"" + face +
         ".Fab\") (effects (font (size 1 1) (thickness 0.15))" + (e.dessous ? " (justify mirror)" : "") + "))\n";
    if (x && !x->fiche.empty()) o += "    (property \"Datasheet\" " + chaineK(x->fiche) + " (at 0 0 0) (layer \"" + face + ".Fab\") hide)\n";
    if (x && x->nePasMonter) o += "    (attr smd dnp)\n";
    for (const auto& t : d->traits) {
      const std::string l = coucheK(t.couche);
      const std::string trait = "(stroke (width " + nbK(mm(t.epaisseur)) + ") (type solid)) (layer \"" + l + "\")";
      if (t.rayon > 0) o += "    (fp_circle (center " + ptK(locK(t.a)) + ") (end " + ptK(locK(t.a + Pt{t.rayon, 0})) + ") " + trait + " (fill none))\n";
      else o += "    (fp_line (start " + ptK(locK(t.a)) + ") (end " + ptK(locK(t.b)) + ") " + trait + ")\n";
    }
    if (x && x->horsNomenclature) o += "    (attr exclude_from_bom)\n";
    if (x && x->horsPlacement) o += "    (attr exclude_from_pos_files)\n";
    for (const auto& pa : d->pastilles) {
      const Pastille ps = pastilleSurCarte(e, pa);
      std::string type = pa.type == TypePastille::CMS ? "smd" : pa.type == TypePastille::NonMetallisee ? "np_thru_hole" : "thru_hole";
      const char* forme = pa.forme == FormePastille::Rond ? "circle" : pa.forme == FormePastille::Ovale ? "oval" : pa.forme == FormePastille::Rect ? "rect"
                          : pa.forme == FormePastille::Trapeze ? "trapezoid" : pa.forme == FormePastille::Libre ? "custom" : "roundrect";
      std::string couches;
      if (pa.couches) {
        // Couches explicites de la pastille (X2), sur la face où l'empreinte est posée
        for (int i = 0; i < NB_COUCHES; ++i)
          if (pa.couches & bit(static_cast<Couche>(i))) couches += (couches.empty() ? "" : " ") + std::string("\"") + coucheK(static_cast<Couche>(i)) + "\"";
      } else if (pa.type == TypePastille::CMS) couches = "\"" + face + ".Cu\" \"" + face + ".Paste\" \"" + face + ".Mask\"";
      else couches = "\"*.Cu\" \"*.Mask\"";
      if (pa.type == TypePastille::CMS && pa.fabrication == "connecteurBord") type = "connect";
      // Fente : la pastille KiCad garde son angle, la fente le long de son axe x
      o += "    (pad " + chaineK(pa.numero) + " " + type + " " + forme + " (at " + ptK(locK(pa.position)) + " " + nbK(ps.rotation) + ") (size " + nbK(mm(pa.largeur)) + " " +
           nbK(mm(pa.hauteur)) + ")";
      if (pa.fente()) o += " (drill oval " + nbK(mm(pa.longueurPercage)) + " " + nbK(mm(pa.percage)) + ")";
      else if (pa.percage > 0) o += " (drill " + nbK(mm(pa.percage)) + (pa.decalagePercage == Pt{} ? std::string() : " (offset " + ptK(pa.decalagePercage) + ")") + ")";
      o += " (layers " + couches + ")";
      if (pa.forme == FormePastille::RectArrondi) o += " (roundrect_rratio " + nbK(pa.arrondi) + ")";
      if (pa.forme == FormePastille::Chanfreine) o += " (chamfer_ratio " + nbK(pa.chanfrein) + ")";
      if (pa.forme == FormePastille::Trapeze) o += " (rect_delta 0 " + nbK(mm(pa.trapeze)) + ")";
      if (pa.margeVernis) o += " (solder_mask_margin " + nbK(mm(*pa.margeVernis)) + ")";
      if (pa.margePate) o += " (solder_paste_margin " + nbK(mm(*pa.margePate)) + ")";
      if (pa.connexion != ConnexionZone::Heritee) o += " (zone_connect " + std::string(pa.connexion == ConnexionZone::Aucune ? "0" : pa.connexion == ConnexionZone::Thermique ? "1" : "2") + ")";
      if (pa.forme == FormePastille::Libre && pa.contourLibre.size() >= 3) {
        o += " (options (clearance outline) (anchor rect)) (primitives (gr_poly (pts";
        for (const Pt& q : pa.contourLibre) o += " (xy " + ptK(q) + ")";
        o += ") (width 0) (fill yes)))";
      }
      if (pa.type != TypePastille::NonMetallisee) {
        const auto it = reseauPastille.find({static_cast<int>(ie), pa.numero});
        if (it != reseauPastille.end() && !it->second.empty()) o += " " + net(it->second);
      }
      o += ")\n";
    }
    // Encombrement (courtyard)
    o += "    (fp_rect (start " + ptK(locK({enc.x0, enc.y0})) + ") (end " + ptK(locK({enc.x1, enc.y1})) + ") (stroke (width 0.05) (type solid)) (fill none) (layer \"" +
         face + ".CrtYd\"))\n";
    o += "  )\n";
  }
  for (const auto& p : c.circuit.pistes) {
    if (p.milieu) o += "  (arc (start " + ptK(p.a) + ") (mid " + ptK(*p.milieu) + ") (end " + ptK(p.b) + ") (width " + nbK(mm(p.largeur)) + ") (layer \"" + nomKicad(p.couche) + "\") " + net(p.reseau) + ")\n";
    else o += "  (segment (start " + ptK(p.a) + ") (end " + ptK(p.b) + ") (width " + nbK(mm(p.largeur)) + ") (layer \"" + nomKicad(p.couche) + "\") " + net(p.reseau) + ")\n";
  }
  for (const auto& v : c.circuit.vias) {
    const char* genre = v.type == TypeVia::Micro ? "micro " : v.type == TypeVia::Borgne || v.type == TypeVia::Enterree ? "blind " : "";
    o += "  (via " + std::string(genre) + "(at " + ptK(v.p) + ") (size " + nbK(mm(v.diametre)) + ") (drill " + nbK(mm(v.percage)) + ") (layers \"" + nomKicad(v.de) + "\" \"" + nomKicad(v.vers) + "\") " + net(v.reseau) + ")\n";
  }
  // Zones de règles qui interdisent quelque chose : zones d'interdiction de KiCad (keepout)
  for (const auto& zr : c.circuit.zonesRegles) {
    if (zr.contour.size() < 3 || !(zr.interditPistes || zr.interditVias || zr.interditPastilles || zr.interditZones || zr.interditEmpreintes)) continue;
    std::string couches;
    if (zr.couches == 0) couches = "\"*.Cu\"";
    else
      for (Couche x : couchesCuivre(nc))
        if (zr.couches & bit(x)) couches += (couches.empty() ? "" : " ") + std::string("\"") + nomKicad(x) + "\"";
    auto permis = [](bool interdit) { return interdit ? "not_allowed" : "allowed"; };
    o += "  (zone (net 0) (net_name \"\") (layers " + couches + ") (name " + chaineK(zr.nom) + ") (hatch edge 0.5)\n    (keepout (tracks " + permis(zr.interditPistes) + ") (vias " + permis(zr.interditVias) +
         ") (pads " + permis(zr.interditPastilles) + ") (copperpour " + permis(zr.interditZones) + ") (footprints " + permis(zr.interditEmpreintes) + "))\n    (polygon (pts";
    for (const Pt& q : zr.contour) o += " (xy " + ptK(q) + ")";
    o += "))\n  )\n";
  }
  // Dessins de la carte (5.31) et cotes (5.15)
  for (const auto& d : c.circuit.dessins) {
    if (d.points.size() < 2) continue;
    const std::string trait = "(stroke (width " + nbK(mm(d.epaisseur)) + ") (type solid)) (layer \"" + nomKicad(d.couche) + "\")";
    const std::string rempli = std::string(" (fill ") + (d.plein ? "solid" : "none") + ")";
    switch (d.forme) {
      case DessinCircuit::Forme::Trait: o += "  (gr_line (start " + ptK(d.points[0]) + ") (end " + ptK(d.points[1]) + ") " + trait + ")\n"; break;
      case DessinCircuit::Forme::Arc:
        if (d.points.size() >= 3) o += "  (gr_arc (start " + ptK(d.points[0]) + ") (mid " + ptK(d.points[1]) + ") (end " + ptK(d.points[2]) + ") " + trait + ")\n";
        break;
      case DessinCircuit::Forme::Cercle: o += "  (gr_circle (center " + ptK(d.points[0]) + ") (end " + ptK(d.points[1]) + ") " + trait + rempli + ")\n"; break;
      case DessinCircuit::Forme::Rectangle: o += "  (gr_rect (start " + ptK(d.points[0]) + ") (end " + ptK(d.points[1]) + ") " + trait + rempli + ")\n"; break;
      case DessinCircuit::Forme::Polygone:
      case DessinCircuit::Forme::Bezier: {
        o += std::string("  (") + (d.forme == DessinCircuit::Forme::Bezier && d.points.size() == 4 ? "gr_curve" : "gr_poly") + " (pts";
        for (const Pt& q : d.points) o += " (xy " + ptK(q) + ")";
        o += ") " + trait + (d.forme == DessinCircuit::Forme::Polygone ? rempli : std::string()) + ")\n";
        break;
      }
    }
  }
  for (const auto& k2 : c.circuit.cotes) {
    if (k2.points.size() < 2) continue;
    const char* genre = k2.genre == GenreCote::Horizontale || k2.genre == GenreCote::Verticale ? "orthogonal" : k2.genre == GenreCote::Rayon || k2.genre == GenreCote::Diametre ? "radial" : k2.genre == GenreCote::Centre ? "center" : "aligned";
    o += "  (dimension (type " + std::string(genre) + ") (layer \"" + nomKicad(k2.couche) + "\") (pts (xy " + ptK(k2.points[0]) + ") (xy " + ptK(k2.points[1]) + ")) (height " + nbK(mm(k2.decalage)) +
         ") (format (units 2) (units_format 1) (precision " + std::to_string(k2.precision) + ")) (style (thickness " + nbK(mm(k2.epaisseur)) + ") (arrow_length 1.27) (text_position_mode 0) (extension_height 0.58642) (extension_offset 0.5) keep_text_aligned))\n";
  }
  for (const auto& z : c.circuit.zones) {
    if (z.contour.size() < 3) continue;
    const auto it = numeros.find(z.reseau);
    o += "  (zone (net " + std::to_string(it == numeros.end() ? 0 : it->second) + ") (net_name " + chaineK(z.reseau) + ") (layer \"" + nomKicad(z.couche) + "\") (hatch edge 0.5)";
    if (z.priorite) o += " (priority " + std::to_string(z.priorite) + ")";
    o += "\n    (connect_pads" + std::string(z.thermique ? "" : " yes") + " (clearance " + nbK(mm(z.isolement > 0 ? z.isolement : c.circuit.regles.isolement)) + "))\n";
    o += "    (min_thickness " + nbK(mm(z.epaisseurMin)) + ")\n    (fill yes (thermal_gap " + nbK(mm(z.ecartThermique)) + ") (thermal_bridge_width " + nbK(mm(z.largeurRayon)) + "))\n";
    o += "    (polygon (pts";
    for (const Pt& p : z.contour) o += " (xy " + ptK(p) + ")";
    o += "))\n  )\n";
  }
  for (const auto& [a, b] : c.circuit.contour.aretes())
    o += "  (gr_line (start " + ptK(a) + ") (end " + ptK(b) + ") (stroke (width 0.1) (type solid)) (layer \"Edge.Cuts\"))\n";
  // Angle des textes : même sens que KiCad, sans conversion (X3)
  for (const auto& t : c.circuit.textes)
    o += "  (gr_text " + chaineK(t.texte) + " (at " + ptK(t.p) + " " + nbK(t.rotation) + ") (layer \"" + nomKicad(t.couche) + "\") (effects (font (size " + nbK(mm(t.hauteur)) + " " +
         nbK(mm(t.hauteur)) + ") (thickness " + nbK(mm(epaisseurTexte(t.hauteur))) + "))" + (t.couche == Couche::SerigraphieDessous ? " (justify mirror)" : "") + "))\n";
  o += ")\n";
  return o;
}

std::string projetKicadDepuisConception(const ConceptionElec& c) {
  // Fichier .kicad_pro (JSON) : règles de conception et classes de réseaux avec leurs motifs, que KiCad relit pour
  // sa DRC ; les noms des réseaux sont ceux de la carte écrite à côté
  const Regles& R = c.circuit.regles;
  nlohmann::ordered_json classes = nlohmann::ordered_json::array();
  auto classe = [&](const std::string& nom, Nm isolement, Nm largeur, Nm via, Nm percage) {
    return nlohmann::ordered_json{{"name", nom}, {"clearance", mm(isolement)}, {"track_width", mm(largeur)}, {"via_diameter", mm(via)}, {"via_drill", mm(percage)},
                                  {"microvia_diameter", mm(c.circuit.empilage.microViaDiametre)}, {"microvia_drill", mm(c.circuit.empilage.microViaPercage)},
                                  {"diff_pair_gap", 0.25}, {"diff_pair_width", mm(largeur)}, {"pcb_color", "rgba(0, 0, 0, 0.000)"}, {"schematic_color", "rgba(0, 0, 0, 0.000)"}, {"wire_width", 6}, {"bus_width", 12}, {"line_style", 0}};
  };
  classes.push_back(classe("Default", R.isolement, R.largeur, R.viaDiametre, R.viaPercage));
  for (const auto& k : R.classes) classes.push_back(classe(k.nom, k.isolement, k.largeur, k.viaDiametre, k.viaPercage));
  nlohmann::ordered_json motifs = nlohmann::ordered_json::array();
  for (const auto& [motif, nom] : R.classeReseau) motifs.push_back({{"netclass", nom}, {"pattern", motif}});
  nlohmann::ordered_json p;
  p["board"]["design_settings"]["rules"] = {{"min_clearance", mm(R.isolement)}, {"min_track_width", mm(R.largeurMin)}, {"min_via_diameter", mm(R.viaDiametre)},
                                             {"min_through_hole_diameter", mm(R.percageMin)}, {"min_copper_edge_clearance", mm(R.bord)}, {"min_hole_to_hole", mm(R.ecartTrous)},
                                             {"min_via_annular_width", mm(R.anneauMin)}, {"min_hole_clearance", mm(R.cuivreTrou)}};
  p["board"]["design_settings"]["defaults"] = {{"board_outline_line_width", 0.1}, {"copper_line_width", 0.2}};
  p["meta"] = {{"filename", nomFichierAscii(c.nom, "carte") + ".kicad_pro"}, {"version", 1}};
  p["net_settings"] = {{"classes", classes}, {"meta", {{"version", 3}}}, {"netclass_patterns", motifs}};
  return p.dump(2) + "\n";
}

}  // namespace prom::ecad
