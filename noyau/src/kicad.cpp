// Prométhée : lecture des cartes KiCad (.kicad_pcb). Licence GPL-3.0-only.
#include "promethee/kicad.hpp"
#include "promethee/fichiers.hpp"
#include "kicad_interne.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
#include <stdexcept>

namespace prom {

namespace {

using namespace kicad_interne;

void etendre(EmpreinteKicad& f, const Point& p) {
  f.xmin = std::min(f.xmin, p.x); f.xmax = std::max(f.xmax, p.x);
  f.ymin = std::min(f.ymin, p.y); f.ymax = std::max(f.ymax, p.y);
}

}  // namespace

CarteKicad lireKicad(const std::string& texte) {
  const Expr racine = Lecteur(texte).lire();
  if (racine.nom() != "kicad_pcb") throw std::runtime_error("Ce fichier n’est pas une carte KiCad (.kicad_pcb).");
  CarteKicad carte;
  if (const Expr* g = racine.enfant("general"))
    if (const Expr* t = g->enfant("thickness")) carte.epaisseur = t->nombre(1, 1.6);
  if (const Expr* tb = racine.enfant("title_block"))
    if (const Expr* t = tb->enfant("title")) carte.titre = t->valeur(1);
  std::vector<ElementContour> morceaux;
  for (const auto& e : racine.elements) {
    const std::string& n = e.nom();
    if (n.rfind("gr_", 0) != 0 || couche(e) != "Edge.Cuts") continue;
    const Expr* debut = e.enfant("start");
    const Expr* fin = e.enfant("end");
    if (n == "gr_line" && debut && fin) {
      morceaux.push_back({pt(*debut), pt(*fin), std::nullopt});
    } else if (n == "gr_arc" && debut && fin && e.enfant("mid")) {
      morceaux.push_back({pt(*debut), pt(*fin), pt(*e.enfant("mid"))});
    } else if (n == "gr_rect" && debut && fin) {
      const Point a = pt(*debut), b = pt(*fin);
      morceaux.push_back({{a.x, a.y}, {b.x, a.y}, std::nullopt});
      morceaux.push_back({{b.x, a.y}, {b.x, b.y}, std::nullopt});
      morceaux.push_back({{b.x, b.y}, {a.x, b.y}, std::nullopt});
      morceaux.push_back({{a.x, b.y}, {a.x, a.y}, std::nullopt});
    } else if (n == "gr_circle" && e.enfant("center") && fin) {
      const Point c = pt(*e.enfant("center")), f2 = pt(*fin);
      const double r = std::hypot(f2.x - c.x, f2.y - c.y);
      morceaux.push_back({{c.x + r, c.y}, {c.x - r, c.y}, Point{c.x, c.y + r}});
      morceaux.push_back({{c.x - r, c.y}, {c.x + r, c.y}, Point{c.x, c.y - r}});
    } else if (n == "gr_poly") {
      if (const Expr* pts = e.enfant("pts")) {
        std::vector<Point> p;
        for (const auto& xy : pts->elements) if (xy.nom() == "xy") p.push_back(pt(xy));
        for (size_t i = 0; i + 1 < p.size() + 1 && p.size() > 1; ++i) morceaux.push_back({p[i], p[(i + 1) % p.size()], std::nullopt});
      }
    }
  }
  carte.contours = chainer(morceaux);
  if (!carte.contours.empty()) {
    const auto poly = carte.contours.front().polygone();
    carte.xmin = carte.ymin = std::numeric_limits<double>::max();
    carte.xmax = carte.ymax = std::numeric_limits<double>::lowest();
    for (const auto& q : poly) {
      carte.xmin = std::min(carte.xmin, q.x); carte.xmax = std::max(carte.xmax, q.x);
      carte.ymin = std::min(carte.ymin, q.y); carte.ymax = std::max(carte.ymax, q.y);
    }
  }
  for (const auto& e : racine.elements) {
    if (e.nom() != "footprint" && e.nom() != "module") continue;
    EmpreinteKicad f;
    f.nom = e.valeur(1);
    f.dessous = couche(e) == "B.Cu";
    if (const Expr* at = e.enfant("at")) { f.position = pt(*at); f.rotation = at->nombre(3, 0); }
    for (const auto& s : e.elements) {
      if (s.nom() == "property") {
        if (s.valeur(1) == "Reference") f.ref = s.valeur(2);
        else if (s.valeur(1) == "Value") f.valeur = s.valeur(2);
      } else if (s.nom() == "fp_text") {
        if (s.valeur(1) == "reference") f.ref = s.valeur(2);
        else if (s.valeur(1) == "value") f.valeur = s.valeur(2);
      }
    }
    // Repère local de l'empreinte (Y vers le haut) tourné de sa rotation, puis placé.
    const double a = f.rotation * std::numbers::pi / 180;
    auto monde = [&](const Expr& x) {
      const double lx = x.nombre(1), ly = -x.nombre(2);
      return Point{f.position.x + lx * std::cos(a) - ly * std::sin(a), f.position.y + lx * std::sin(a) + ly * std::cos(a)};
    };
    f.xmin = f.ymin = std::numeric_limits<double>::max();
    f.xmax = f.ymax = std::numeric_limits<double>::lowest();
    bool zone = false;
    for (const auto& s : e.elements) {
      const std::string c = couche(s);
      if ((c == "F.CrtYd" || c == "B.CrtYd") && (s.nom() == "fp_line" || s.nom() == "fp_rect" || s.nom() == "fp_circle" || s.nom() == "fp_poly" || s.nom() == "fp_arc")) {
        zone = true;
        if (s.nom() == "fp_circle" && s.enfant("center") && s.enfant("end")) {
          // Cercle : son encombrement est un carré de côté 2r autour du centre, quelle que soit la rotation.
          const Expr& c = *s.enfant("center");
          const double r = std::hypot(s.enfant("end")->nombre(1) - c.nombre(1), s.enfant("end")->nombre(2) - c.nombre(2));
          const Point m = monde(c);
          etendre(f, {m.x - r, m.y - r});
          etendre(f, {m.x + r, m.y + r});
          continue;
        }
        for (const char* cle : {"start", "end", "mid", "center"})
          if (const Expr* q = s.enfant(cle)) etendre(f, monde(*q));
        if (const Expr* pts = s.enfant("pts"))
          for (const auto& xy : pts->elements) if (xy.nom() == "xy") etendre(f, monde(xy));
      }
    }
    for (const auto& s : e.elements) {
      if (s.nom() != "pad") continue;
      const Expr* at = s.enfant("at");
      const Expr* taille = s.enfant("size");
      if (const Expr* dr = s.enfant("drill")) f.percage = std::max(f.percage, dr->nombre(1, 0));
      if (zone || !at || !taille) continue;
      const Point c = monde(*at);
      const double r = std::max(taille->nombre(1), taille->nombre(2)) / 2;
      etendre(f, {c.x - r, c.y - r});
      etendre(f, {c.x + r, c.y + r});
    }
    if (f.xmin > f.xmax) { f.xmin = f.xmax = f.position.x; f.ymin = f.ymax = f.position.y; }
    f.fixation = f.nom.find("MountingHole") != std::string::npos || (f.ref.rfind("H", 0) == 0 && f.percage >= 2.0);
    carte.empreintes.push_back(f);
  }
  return carte;
}

namespace {
std::string minuscules(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
bool contient(const std::string& s, std::initializer_list<const char*> mots) {
  for (const char* m : mots) if (s.find(m) != std::string::npos) return true;
  return false;
}
// Diamètre lu dans un nom d'empreinte du type « CP_Radial_D10.0mm ».
double diametreDansNom(const std::string& n) {
  const auto i = n.find("_d");
  if (i == std::string::npos) return 0;
  return std::strtod(n.c_str() + i + 2, nullptr);
}
std::string visPourPercage(double d) {
  if (d <= 2.45) return "M2";
  if (d <= 2.95) return "M2.5";
  if (d <= 3.7) return "M3";
  if (d <= 4.7) return "M4";
  return "M5";
}
}  // namespace

double hauteurEstimee(const std::string& nomEmpreinte) {
  const std::string n = minuscules(nomEmpreinte);
  if (contient(n, {"cp_radial", "c_radial", "cp_elec"})) {
    const double d = diametreDansNom(n);
    return d <= 6.3 ? 11 : d <= 8 ? 12 : d <= 10 ? 16 : 20;
  }
  // std::vector et non std::initializer_list : la liste stockée dans une paire ne survivrait pas à l'initialisation.
  static const std::vector<std::pair<std::vector<const char*>, double>> table = {
      {{"usb_c", "usb-c", "type-c", "typec"}, 3.3}, {{"barreljack", "barrel_jack", "dc_jack"}, 11}, {{"esp32", "esp-", "rf_module"}, 3.2},
      {{"sw_push", "tactile", "button"}, 5}, {{"led_d3", "led_d5"}, 8.6}, {{"pinheader", "pin_header", "pinsocket", "pin_socket"}, 8.5},
      {{"screwterminal", "terminalblock", "terminal_block"}, 10}, {{"relay"}, 15}, {{"to-220", "to220"}, 16}, {{"to-92", "to92"}, 5},
      {{"jst_sh"}, 4.3}, {{"jst_ph"}, 6}, {{"jst_xh"}, 7}, {{"dip-", "dip_"}, 4.5}, {{"crystal", "hc49", "hc-49"}, 4},
      {{"r_axial", "d_axial", "do-41", "do41"}, 3}, {{"c_disc"}, 6}, {{"c_rect"}, 10}, {{"sot-223", "sot223"}, 1.8}, {{"sot-23", "sot23"}, 1.1},
      {{"soic", "so-"}, 1.75}, {{"tssop", "msop", "ssop"}, 1.2}, {{"lqfp", "tqfp", "qfp"}, 1.6}, {{"qfn", "dfn", "lga"}, 1.0}, {{"bga"}, 1.5},
      {{"0201", "0402"}, 0.5}, {{"0603", "1608"}, 0.8}, {{"0805", "2012"}, 1.0}, {{"1206", "3216"}, 1.2}};
  for (const auto& [mots, h] : table)
    for (const char* m : mots)
      if (n.find(m) != std::string::npos) return h;
  return 2.0;
}

Projet projetDepuisKicad(const CarteKicad& k, const std::string& nom, RapportImport* rapport) {
  if (k.contours.empty() || !k.contours.front().ferme) throw std::runtime_error("La carte KiCad n’a pas de contour Edge.Cuts fermé.");
  RapportImport r;
  const double dx = -(k.xmin + k.xmax) / 2, dy = -(k.ymin + k.ymax) / 2;
  auto decale = [&](Point q) { return Point{q.x + dx, q.y + dy}; };
  Json j = Json::object();
  j["nom"] = nom.empty() ? (k.titre.empty() ? std::string("Carte KiCad") : k.titre) : nom;
  double t = 1.6, ecartMin = 1e9;
  for (double e : EPAISSEURS_PCB) if (std::abs(e - k.epaisseur) < ecartMin) { ecartMin = std::abs(e - k.epaisseur); t = e; }
  Json contours = Json::array();
  for (const auto& c : k.contours) {
    if (!c.ferme) continue;
    Json elements = Json::array();
    for (const auto& e : c.elements) {
      const Point a = decale(e.debut), b = decale(e.fin);
      Json je = {{"debut", {a.x, a.y}}, {"fin", {b.x, b.y}}};
      if (e.milieu) { const Point m = decale(*e.milieu); je["milieu"] = {m.x, m.y}; }
      elements.push_back(je);
    }
    contours.push_back({{"elements", elements}});
  }
  j["carte"] = {{"t", t}, {"contours", contours}};
  j["boitier"] = {{"jeu", 1}, {"paroi", 2}, {"fond", 2}, {"entretoise", 5}, {"couvercle", 2}, {"vis", "M3"}, {"hauteurAuto", true}};
  j["trous"] = Json::array();
  j["composants"] = Json::array();
  const double L = k.xmax - k.xmin, W = k.ymax - k.ymin;
  for (const auto& f : k.empreintes) {
    const Point c = decale(f.position);
    if (f.fixation) {
      Json jt = {{"ref", f.ref}, {"x", c.x}, {"y", c.y}, {"ancrage", "libre"}};
      if (f.percage > 0) { jt["vis"] = visPourPercage(f.percage); jt["dimensions"] = {{"trou", f.percage}}; }
      j["trous"].push_back(jt);
      ++r.trous;
      continue;
    }
    if (f.ref.empty() || f.ref[0] == '#' || f.xmax - f.xmin < 0.05) continue;
    const std::string n = minuscules(f.nom);
    const double x1 = f.xmin + dx, x2 = f.xmax + dx, y1 = f.ymin + dy, y2 = f.ymax + dy;
    const double cx = (x1 + x2) / 2, cy = (y1 + y2) / 2, w = x2 - x1, d = y2 - y1;
    Json jk = {{"ref", f.ref}, {"valeur", f.valeur}, {"h", hauteurEstimee(f.nom)}, {"origine", "kicad"}, {"sourceHauteur", "estimee"}};
    if (f.dessous) { jk["face"] = "dessous"; ++r.dessous; }
    const bool usbc = contient(n, {"usb_c", "usb-c", "type-c", "typec"});
    const bool jack = contient(n, {"barreljack", "barrel_jack", "dc_jack"});
    const bool autre = contient(n, {"usb_micro", "usb_mini", "usb_a", "usb_b", "rj45", "rj11", "8p8c", "jack_3.5", "audio_jack", "stereo_jack", "pj-3", "hdmi",
                                    "dsub", "db9", "de9", "sd_card", "microsd", "micro_sd", "terminalblock", "terminal_block", "ethernet", "sma_", "bnc"});
    const double dE = L / 2 - x2, dW = x1 + L / 2, dN = W / 2 - y2, dS = y1 + W / 2;
    const double dBord = std::min({dE, dW, dN, dS});
    if ((usbc || jack || autre) && !f.dessous && dBord < 1.5) {
      // Connecteur de bord : rattaché au côté le plus proche de l'encombrement de la carte ; il perce la paroi.
      const char* bord = dBord == dE ? "E" : dBord == dW ? "W" : dBord == dN ? "N" : "S";
      const bool hz = dBord == dN || dBord == dS;
      jk["type"] = usbc ? "usbc" : jack ? "jack" : "connecteur";
      jk["bord"] = bord;
      jk["le_long"] = hz ? cx : cy;
      jk["w"] = hz ? w : d;
      jk["d"] = hz ? d : w;
      if (!usbc && !jack) {
        const double h = hauteurEstimee(f.nom);
        jk["decoupe"] = {{"w", std::max(3.0, (hz ? w : d) + 1)}, {"h", h + 1}, {"r", 1}};
        jk["zc"] = h / 2;
      }
      ++r.connecteursBord;
    } else {
      std::string type = "generique";
      if (contient(n, {"cp_radial", "c_radial", "cp_elec"})) type = "condo";
      else if (contient(n, {"led_d3", "led_d5"})) type = "led";
      else if (contient(n, {"sw_push_6mm", "sw_tactile_6", "tactile_6"})) type = "bouton";
      else if (contient(n, {"esp32", "esp-", "rf_module"})) type = "module";
      else if (contient(n, {"jst"})) type = "jst";
      jk["type"] = type;
      jk["x"] = cx; jk["y"] = cy; jk["rot"] = 0;
      jk["w"] = w; jk["d"] = d;
    }
    j["composants"].push_back(jk);
    ++r.composants;
  }
  j["kicad"] = {{"dx", dx}, {"dy", dy}};
  if (rapport) *rapport = r;
  return normaliser(j);
}

// ---------- Écriture vers KiCad ----------
namespace {

std::string nb(double v) {
  char t[40];
  std::snprintf(t, sizeof t, "%.6f", std::abs(v) < 1e-9 ? 0.0 : v);
  std::string s = t;
  while (s.back() == '0') s.pop_back();
  if (s.back() == '.') s.pop_back();
  return s == "-0" ? "0" : s;
}

std::string uuid() {
  static std::mt19937_64 alea{std::random_device{}()};
  const uint64_t a = alea(), b = alea();
  char t[40];
  std::snprintf(t, sizeof t, "%08x-%04x-4%03x-%04x-%012llx", static_cast<unsigned>(a >> 32), static_cast<unsigned>((a >> 16) & 0xFFFF), static_cast<unsigned>(a & 0xFFF),
                static_cast<unsigned>(0x8000 | (b >> 50)), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
  return t;
}

Expr atome(const std::string& s, bool guillemets = false) { Expr e; e.atome = s; e.guillemets = guillemets; return e; }
Expr liste(std::initializer_list<Expr> el) { Expr e; e.liste = true; e.elements = el; return e; }

void serialiser(const Expr& e, std::string& o, int niveau) {
  if (!e.liste) {
    if (e.guillemets) { o += '"'; o += e.atome; o += '"'; }
    else o += e.atome;
    return;
  }
  o += '(';
  bool premier = true;
  for (const auto& x : e.elements) {
    if (!premier) {
      if (x.liste && niveau < 2) { o += '\n'; o.append(static_cast<size_t>(2 * (niveau + 1)), ' '); }
      else o += ' ';
    }
    serialiser(x, o, niveau + 1);
    premier = false;
  }
  o += ')';
}

// Élément de contour Edge.Cuts dans le repère KiCad ; « jeton » : uuid (KiCad 8 et suivants) ou tstamp (KiCad 7).
Expr elementBord(const Point& a, const Point& b, const std::optional<Point>& m, const std::string& jeton) {
  Expr e = liste({atome(m ? "gr_arc" : "gr_line"), liste({atome("start"), atome(nb(a.x)), atome(nb(a.y))})});
  if (m) e.elements.push_back(liste({atome("mid"), atome(nb(m->x)), atome(nb(m->y))}));
  e.elements.push_back(liste({atome("end"), atome(nb(b.x)), atome(nb(b.y))}));
  e.elements.push_back(liste({atome("stroke"), liste({atome("width"), atome("0.1")}), liste({atome("type"), atome("default")})}));
  e.elements.push_back(liste({atome("layer"), atome("Edge.Cuts", true)}));
  e.elements.push_back(liste({atome(jeton), atome(uuid(), jeton == "uuid")}));
  return e;
}

// Contours de la carte (libre, ou rectangle arrondi décomposé en segments et arcs) dans le repère du projet.
std::vector<Contour> contoursProjet(const Projet& p) {
  if (p.carte.libre()) return p.carte.contours;
  const auto& c = p.carte;
  const double x1 = c.x0 - c.L / 2, x2 = c.x0 + c.L / 2, y1 = c.y0 - c.W / 2, y2 = c.y0 + c.W / 2, r = c.r;
  Contour k;
  k.ferme = true;
  if (r < 1e-6) {
    k.elements = {{{x1, y1}, {x2, y1}, {}}, {{x2, y1}, {x2, y2}, {}}, {{x2, y2}, {x1, y2}, {}}, {{x1, y2}, {x1, y1}, {}}};
  } else {
    const double s = r * (1 - std::sqrt(0.5));
    k.elements = {{{x1 + r, y1}, {x2 - r, y1}, {}}, {{x2 - r, y1}, {x2, y1 + r}, Point{x2 - s, y1 + s}},
                  {{x2, y1 + r}, {x2, y2 - r}, {}}, {{x2, y2 - r}, {x2 - r, y2}, Point{x2 - s, y2 - s}},
                  {{x2 - r, y2}, {x1 + r, y2}, {}}, {{x1 + r, y2}, {x1, y2 - r}, Point{x1 + s, y2 - s}},
                  {{x1, y2 - r}, {x1, y1 + r}, {}}, {{x1, y1 + r}, {x1 + r, y1}, Point{x1 + s, y1 + s}}};
  }
  return {k};
}

}  // namespace

std::string kicadDepuisProjet(const Projet& p, const Derive& d) {
  // Coin supérieur gauche de l'encombrement en (100 ; 80) mm sur la feuille KiCad ; axe Y vers le bas.
  const double xmin = p.carte.x0 - p.carte.L / 2, ymax = p.carte.y0 + p.carte.W / 2;
  auto K = [&](const Point& q) { return Point{q.x - xmin + 100, (ymax - q.y) + 80}; };
  std::string o =
      "(kicad_pcb (version 20221018) (generator promethee)\n"
      "  (general (thickness " + nb(p.carte.t) + "))\n"
      "  (paper \"A4\")\n"
      "  (title_block (title \"" + p.nom + "\") (comment 1 \"Contour, trous de fixation et emplacements exportés par Prométhée\"))\n"
      "  (layers\n"
      "    (0 \"F.Cu\" signal) (31 \"B.Cu\" signal)\n"
      "    (32 \"B.Adhes\" user \"B.Adhesive\") (33 \"F.Adhes\" user \"F.Adhesive\")\n"
      "    (34 \"B.Paste\" user) (35 \"F.Paste\" user)\n"
      "    (36 \"B.SilkS\" user \"B.Silkscreen\") (37 \"F.SilkS\" user \"F.Silkscreen\")\n"
      "    (38 \"B.Mask\" user) (39 \"F.Mask\" user)\n"
      "    (40 \"Dwgs.User\" user \"User.Drawings\") (41 \"Cmts.User\" user \"User.Comments\")\n"
      "    (42 \"Eco1.User\" user \"User.Eco1\") (43 \"Eco2.User\" user \"User.Eco2\")\n"
      "    (44 \"Edge.Cuts\" user) (45 \"Margin\" user)\n"
      "    (46 \"B.CrtYd\" user \"B.Courtyard\") (47 \"F.CrtYd\" user \"F.Courtyard\")\n"
      "    (48 \"B.Fab\" user) (49 \"F.Fab\" user)\n"
      "    (50 \"User.1\" user) (51 \"User.2\" user))\n"
      "  (setup (pad_to_mask_clearance 0))\n"
      "  (net 0 \"\")\n";
  for (const auto& c : contoursProjet(p))
    for (const auto& e : c.elements) {
      std::string s;
      serialiser(elementBord(K(e.debut), K(e.fin), e.milieu ? std::optional<Point>(K(*e.milieu)) : std::nullopt, "tstamp"), s, 1);
      o += "  " + s + "\n";
    }
  // Trous de fixation : empreintes à perçage non métallisé, repérées comme dans Prométhée.
  for (const auto& t : d.trous) {
    const Point q = K({t.x, t.y});
    const std::string dt = nb(t.vis.trou), rc = nb(t.vis.tete / 2 + 0.25);
    o += "  (footprint \"MountingHole:MountingHole_" + dt + "mm\" (layer \"F.Cu\") (tstamp " + uuid() + ") (at " + nb(q.x) + " " + nb(q.y) + ")\n"
         "    (attr exclude_from_pos_files exclude_from_bom)\n"
         "    (fp_text reference \"" + t.ref + "\" (at 0 " + nb(-(t.vis.tete / 2 + 1.5)) + ") (layer \"F.SilkS\") (effects (font (size 1 1) (thickness 0.15))) (tstamp " + uuid() + "))\n"
         "    (fp_text value \"Fixation " + t.nomVis + "\" (at 0 " + nb(t.vis.tete / 2 + 1.5) + ") (layer \"F.Fab\") (effects (font (size 1 1) (thickness 0.15))) (tstamp " + uuid() + "))\n"
         "    (fp_circle (center 0 0) (end " + rc + " 0) (stroke (width 0.05) (type solid)) (fill none) (layer \"F.CrtYd\") (tstamp " + uuid() + "))\n"
         "    (pad \"\" np_thru_hole circle (at 0 0) (size " + dt + " " + dt + ") (drill " + dt + ") (layers \"*.Cu\" \"*.Mask\") (tstamp " + uuid() + ")))\n";
  }
  // Emplacements des composants placés dans Prométhée, en repères sur User.1 (dessus) et User.2 (dessous).
  for (const auto& g : d.comps) {
    const Point a = K({g.x1, g.y2}), b = K({g.x2, g.y1}), c = K({g.x, g.y});
    const std::string calque = g.dessous ? "User.2" : "User.1";
    o += "  (gr_rect (start " + nb(a.x) + " " + nb(a.y) + ") (end " + nb(b.x) + " " + nb(b.y) + ") (stroke (width 0.1) (type default)) (fill none) (layer \"" + calque + "\") (tstamp " + uuid() + "))\n";
    o += "  (gr_text \"" + g.ref + "\" (at " + nb(c.x) + " " + nb(c.y) + ") (layer \"" + calque + "\") (tstamp " + uuid() + ") (effects (font (size 1 1) (thickness 0.15))))\n";
  }
  o += ")\n";
  return o;
}

std::string kicadMisAJour(const std::string& texte, const Projet& p, const Derive& d) {
  if (!p.kicad) throw std::runtime_error("Ce projet ne vient pas d’une carte KiCad : utilise l’export d’une nouvelle carte.");
  Expr racine = Lecteur(texte).lire();
  if (racine.nom() != "kicad_pcb") throw std::runtime_error("Le fichier d’origine n’est pas une carte KiCad.");
  const double dx = p.kicad->dx, dy = p.kicad->dy;
  auto K = [&](const Point& q) { return Point{q.x - dx, -(q.y - dy)}; };
  // Jeton d'identifiant du fichier : uuid depuis KiCad 8, tstamp avant.
  const bool uuidModerne = texte.find("(uuid") != std::string::npos;
  const std::string jeton = uuidModerne ? "uuid" : "tstamp";
  std::vector<Expr> gardes;
  size_t position = 0;
  for (const auto& e : racine.elements) {
    if (e.nom().rfind("gr_", 0) == 0 && couche(e) == "Edge.Cuts") continue;  // ancien contour retiré
    if (e.nom() == "gr_line" || e.nom() == "gr_arc" || e.nom() == "footprint" || e.nom() == "segment") { if (!position) position = gardes.size(); }
    gardes.push_back(e);
  }
  std::vector<Expr> contour;
  for (const auto& c : contoursProjet(p))
    for (const auto& e : c.elements) contour.push_back(elementBord(K(e.debut), K(e.fin), e.milieu ? std::optional<Point>(K(*e.milieu)) : std::nullopt, jeton));
  if (!position) position = gardes.size();
  gardes.insert(gardes.begin() + static_cast<long>(position), contour.begin(), contour.end());
  // Trous de fixation déplacés dans Prométhée : on met à jour la position de leur empreinte.
  for (auto& e : gardes) {
    if (e.nom() != "footprint" && e.nom() != "module") continue;
    std::string ref;
    for (const auto& s : e.elements) {
      if (s.nom() == "property" && s.valeur(1) == "Reference") ref = s.valeur(2);
      if (s.nom() == "fp_text" && s.valeur(1) == "reference") ref = s.valeur(2);
    }
    for (const auto& t : d.trous) {
      if (t.ref != ref) continue;
      for (auto& s : e.elements)
        if (s.nom() == "at" && s.elements.size() >= 3) {
          const Point q = K({t.x, t.y});
          s.elements[1] = atome(nb(q.x));
          s.elements[2] = atome(nb(q.y));
        }
    }
  }
  racine.elements = gardes;
  std::string o;
  serialiser(racine, o, 0);
  return o + "\n";
}

CarteKicad lireFichierKicad(const std::string& chemin) { return lireKicad(lireFichier(cheminFichier(chemin))); }

}  // namespace prom
