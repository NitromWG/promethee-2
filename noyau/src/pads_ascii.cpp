// Prométhée : PADS ASCII (10.17, 10.22 ; ECH-21, ECH-22). Lecteur des cartes de PADS Layout (PowerPCB, unités
// BASIC, mils, millimètres ou pouces) et des schémas de PADS Logic, d'après le format que lit l'importeur PADS de
// KiCad (GPL), servant de référence, sans en reprendre le code ; écrivain de cartes et de schémas au même format.
// Voir formats_cao.hpp. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

#include "cao_interne.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/formats_cao.hpp"
#include "promethee/import_commun.hpp"

namespace prom::ecad {

namespace {
std::vector<std::string> motsPads(const std::string& s) {
  std::vector<std::string> r;
  std::istringstream in(s);
  for (std::string m; in >> m;) r.push_back(m);
  return r;
}
double nombre(const std::string& s, double defaut = 0) {
  char* fin = nullptr;
  const double v = std::strtod(s.c_str(), &fin);
  return fin == s.c_str() ? defaut : v;
}
bool estNombre(const std::string& s) {
  if (s.empty()) return false;
  char* fin = nullptr;
  std::strtod(s.c_str(), &fin);
  return fin == s.c_str() + s.size();
}
std::string nbp(double v, int dec = 3) {
  char t[48];
  std::snprintf(t, sizeof t, "%.*f", dec, std::abs(v) < 1e-12 ? 0.0 : v);
  std::string s = t;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  return s == "-0" ? "0" : s;
}
std::string sansBlanc(const std::string& s) {
  std::string r;
  for (unsigned char ch : s) r += (std::isspace(ch) || ch == '"') ? '_' : static_cast<char>(ch);
  return r.empty() ? "_" : r;
}

bool estSection(const std::string& x) { return !x.empty() && x[0] == '*' && x.size() > 1 && x.find('*', 1) != std::string::npos; }
std::string nomSection(const std::string& x) { return x.substr(0, x.find('*', 1) + 1); }
// Lignes du fichier, une file avec retour arrière
struct Lignes {
  std::vector<std::string> l;
  std::size_t i = 0;
  explicit Lignes(const std::string& t) {
    std::istringstream in(t);
    for (std::string x; std::getline(in, x);) {
      if (!x.empty() && x.back() == '\r') x.pop_back();
      l.push_back(x);
    }
  }
  bool fin() const { return i >= l.size(); }
  // Ligne suivante non vide, hors remarques
  std::optional<std::string> suivante() {
    while (i < l.size()) {
      const std::string& x = l[i++];
      const auto p = x.find_first_not_of(" \t");
      if (p == std::string::npos) continue;
      if (x.compare(p, 8, "*REMARK*") == 0) continue;
      return x;
    }
    return std::nullopt;
  }
  void reculer() {
    if (i > 0) --i;
  }
  // Ligne d'un objet en cours : une ligne de section l'arrête (laissée pour la boucle des sections)
  std::optional<std::string> interne() {
    auto x = suivante();
    if (x && estSection(*x)) {
      reculer();
      return std::nullopt;
    }
    return x;
  }
};

// Arc d'un coin : « x y début delta xmin ymin xmax ymax » (angles en dixièmes de degré, sens direct, y vers le haut)
struct Coin {
  double x = 0, y = 0;
  bool arc = false;
  double cx = 0, cy = 0, rayon = 0, debut = 0, delta = 0;
};
Coin lireCoin(const std::string& ligne) {
  const auto m = motsPads(ligne);
  Coin c;
  if (m.size() >= 2) c.x = nombre(m[0]), c.y = nombre(m[1]);
  if (m.size() >= 8) {
    c.arc = true;
    c.debut = nombre(m[2]) / 10;
    c.delta = nombre(m[3]) / 10;
    c.cx = (nombre(m[4]) + nombre(m[6])) / 2;
    c.cy = (nombre(m[5]) + nombre(m[7])) / 2;
    c.rayon = (nombre(m[6]) - nombre(m[4])) / 2;
  }
  return c;
}
// Points d'une suite de coins, arcs découpés (unités du fichier, y vers le haut)
std::vector<std::pair<double, double>> pointsCoins(const std::vector<Coin>& coins, double ox, double oy) {
  std::vector<std::pair<double, double>> r;
  for (const Coin& c : coins) {
    if (!c.arc) {
      r.push_back({ox + c.x, oy + c.y});
      continue;
    }
    const int n = std::max(2, static_cast<int>(std::ceil(std::abs(c.delta) / 10)));
    for (int k = 0; k <= n; ++k) {
      const double a = (c.debut + c.delta * k / n) * std::numbers::pi / 180;
      r.push_back({ox + c.cx + c.rayon * std::cos(a), oy + c.cy + c.rayon * std::sin(a)});
    }
  }
  return r;
}

struct PilePads {
  double taille = 0, longueur = 0, angle = 0, percage = 0;
  std::string forme = "R";
  bool metallise = true, dessus = false, dessous = false;
};
struct DecalPads {
  std::vector<std::tuple<double, double, std::string>> bornes;  // x, y, nom
  std::map<int, PilePads> piles;                                 // 0 : toutes les bornes
  std::vector<std::vector<std::pair<double, double>>> traits;
};
struct PiecePads {
  std::string ref, type, decal;
  double x = 0, y = 0, angle = 0;
  bool miroir = false;
  int alt = 0;
};

class LecteurPads {
 public:
  explicit LecteurPads(const std::string& t) : lignes_(t) {}

  cao_interne::CarteLue lire(RapportLectureCao& r, std::vector<std::pair<std::string, Zone>>& zones) {
    entete();
    while (auto x = lignes_.suivante()) {
      if (!estSection(*x)) continue;
      const std::string s = nomSection(*x);
      if (s == "*PCB*") pcb();
      else if (s == "*VIA*") vias();
      else if (s == "*PARTDECAL*") decals();
      else if (s == "*PARTTYPE*") types();
      else if (s == "*PART*") pieces();
      else if (s == "*ROUTE*") routes();
      else if (s == "*LINES*") traits();
      else if (s == "*POUR*") pours();
      else if (s == "*END*") break;
    }
    return assembler(r, zones);
  }

 private:
  Lignes lignes_;
  double nm_ = 25'400;  // nm par unité du fichier
  bool basique_ = false, polices_ = true;
  int maxCouche_ = 2;
  std::map<std::string, std::pair<double, double>> viasDef_;  // nom → (diamètre, perçage)
  std::map<std::string, DecalPads> decals_;
  std::map<std::string, std::vector<std::string>> types_;
  std::vector<PiecePads> pieces_;
  std::map<std::string, std::set<std::pair<std::string, std::string>>> reseaux_;
  std::vector<Piste> pistes_;
  std::vector<Via> vias_;
  std::vector<std::pair<double, double>> contour_;
  std::vector<std::pair<std::string, std::vector<std::pair<double, double>>>> pours_;

  Pt pt(double x, double y) const { return {static_cast<Nm>(std::llround(x * nm_)), -static_cast<Nm>(std::llround(y * nm_))}; }
  Nm lg(double v) const { return static_cast<Nm>(std::llround(v * nm_)); }

  void entete() {
    const auto x = lignes_.suivante();
    if (!x) return;
    const std::string& h = *x;
    if (h.find("BASIC") != std::string::npos) basique_ = true;
    if (basique_) nm_ = 25'400.0 / 38'100.0;
    else if (h.find("-METRIC") != std::string::npos || h.find("-MM") != std::string::npos) nm_ = 1e6;
    else if (h.find("-INCH") != std::string::npos) nm_ = 25.4e6;
    // Version : V9.0 et au-delà (2005…) écrivent une ligne de police sous chaque texte
    const auto v = h.find("-V");
    if (v != std::string::npos) {
      const int majeur = std::atoi(h.c_str() + v + 2);
      polices_ = majeur == 0 || majeur >= 9;
    }
  }
  void pcb() {
    while (auto x = lignes_.suivante()) {
      if (estSection(*x)) {
        lignes_.reculer();
        return;
      }
      const auto m = motsPads(*x);
      if (m.size() >= 2 && m[0] == "MAXIMUMLAYER") maxCouche_ = std::max(1, std::atoi(m[1].c_str()));
      if (m.size() >= 2 && m[0] == "UNITS" && !basique_) {
        const int u = std::atoi(m[1].c_str());
        nm_ = u == 1 ? 1e6 : u == 2 ? 25.4e6 : 25'400;
      }
    }
  }
  void vias() {
    while (auto x = lignes_.suivante()) {
      if (estSection(*x)) {
        lignes_.reculer();
        return;
      }
      const auto m = motsPads(*x);
      if (m.size() < 3 || estNombre(m[0])) continue;
      const double percage = nombre(m[1]);
      const int n = std::atoi(m[2].c_str());
      double diametre = 0;
      for (int k = 0; k < n; ++k) {
        const auto y = lignes_.interne();
        if (!y) break;
        const auto s = motsPads(*y);
        if (s.size() >= 2) diametre = std::max(diametre, nombre(s[1]));
      }
      viasDef_[m[0]] = {diametre, percage};
    }
  }
  void decals() {
    while (auto x = lignes_.suivante()) {
      if (estSection(*x)) {
        lignes_.reculer();
        return;
      }
      // En-tête : NOM UNITÉS ORIX ORIY PIÈCES BORNES PILES TEXTES ÉTIQUETTES (le nom peut être un nombre : « 0402 »)
      const auto m = motsPads(*x);
      if (m.size() < 9 || estNombre(m[1]) || !std::all_of(m.begin() + 2, m.begin() + 9, estNombre)) continue;
      DecalPads d;
      const int pieces = std::atoi(m[4].c_str()), bornes = std::atoi(m[5].c_str()), piles = std::atoi(m[6].c_str()), textes = std::atoi(m[7].c_str()), etiquettes = std::atoi(m[8].c_str());
      for (int k = 0; k < pieces; ++k) {
        const auto y = lignes_.interne();
        if (!y) return;
        const auto p = motsPads(*y);
        const int coins = p.size() >= 2 ? std::atoi(p[1].c_str()) : 0;
        std::vector<Coin> cs;
        for (int j = 0; j < coins; ++j)
          if (const auto z = lignes_.interne()) cs.push_back(lireCoin(*z));
        if (!p.empty() && (p[0] == "OPEN" || p[0] == "CLOSED")) d.traits.push_back(pointsCoins(cs, 0, 0));
      }
      for (int k = 0; k < textes + etiquettes; ++k) {
        lignes_.interne();
        if (polices_) lignes_.interne();
        lignes_.interne();
      }
      for (int k = 0; k < bornes; ++k) {
        auto y = lignes_.interne();
        if (!y) return;
        std::string t = *y;
        if (const auto p = t.find('T'); p != std::string::npos) t[p] = ' ';
        const auto b = motsPads(t);
        if (b.size() < 2) continue;
        d.bornes.push_back({nombre(b[0]), nombre(b[1]), b.size() >= 5 ? b[4] : std::to_string(k + 1)});
      }
      for (int k = 0; k < piles; ++k) {
        const auto y = lignes_.interne();
        if (!y) return;
        const auto h = motsPads(*y);
        if (h.size() < 3 || h[0] != "PAD") continue;
        const int idx = std::atoi(h[1].c_str()), n = std::atoi(h[2].c_str());
        PilePads pile;
        if (h.size() >= 4) {
          if (h[3] == "N") pile.metallise = false;
          else if (h[3] != "P") pile.percage = nombre(h[3]);
        }
        for (int j = 0; j < n; ++j) {
          const auto z = lignes_.interne();
          if (!z) break;
          const auto s = motsPads(*z);
          if (s.size() < 3) continue;
          const int niveau = std::atoi(s[0].c_str());
          const double taille = nombre(s[1]);
          const std::string& forme = s[2];
          std::size_t reste = 3;
          double longueur = taille, angle = 0;
          if (forme == "OF" || forme == "RF" || forme == "RC") {
            if (s.size() >= 6) angle = nombre(s[3]), longueur = nombre(s[4]);
            reste = forme == "RC" || (forme == "RF" && s.size() >= 8 && estNombre(s[6]) && s.size() > 7) ? 7 : 6;
          } else if (forme == "S" || forme == "A")
            reste = 4;
          else if (forme == "RT" || forme == "ST")
            reste = 7;
          // Perçage et métallisation éventuels en fin de ligne
          for (std::size_t q = std::min(reste, s.size()); q < s.size(); ++q) {
            if (s[q] == "N") pile.metallise = false;
            else if (s[q] == "P") pile.metallise = true;
            else if (estNombre(s[q]) && nombre(s[q]) > 0 && pile.percage == 0) pile.percage = nombre(s[q]);
          }
          if (taille <= 0) continue;
          const bool face = niveau == -2 || niveau == 0 || niveau == 1 || niveau == maxCouche_;
          if (!face && pile.taille > 0) continue;
          if (taille > pile.taille || pile.taille == 0) {
            pile.taille = taille;
            pile.longueur = std::max(longueur, taille);
            pile.angle = angle;
            pile.forme = forme;
          }
          if (niveau == -2 || niveau == 1) pile.dessus = true;
          if (niveau == 0 || niveau == maxCouche_) pile.dessous = true;
        }
        d.piles[idx] = pile;
      }
      decals_[m[0]] = d;
    }
  }
  void types() {
    while (auto x = lignes_.suivante()) {
      if (estSection(*x)) {
        lignes_.reculer();
        return;
      }
      // NOM DECALS UNITÉS GENRE PORTES … : les lignes de portes (« G 1 1 ») et de broches (« 1.0.U ») sont passées
      const auto m = motsPads(*x);
      if (m.size() < 8 || m[0] == "G" || m[0] == "S" || m[0] == "SIGPIN" || !estNombre(m[m.size() - 2]) || !estNombre(m[m.size() - 3])) continue;
      std::vector<std::string> alternatives;
      std::istringstream in(m[1]);
      for (std::string a; std::getline(in, a, ':');) alternatives.push_back(a);
      types_[m[0]] = alternatives;
    }
  }
  void pieces() {
    while (auto x = lignes_.suivante()) {
      if (estSection(*x)) {
        lignes_.reculer();
        return;
      }
      // REPÈRE TYPE X Y ANGLE COLLE MIROIR … (les lignes d'étiquettes ont un nombre en sixième place)
      const auto m = motsPads(*x);
      if (m.size() < 7 || !estNombre(m[2]) || !estNombre(m[3]) || !estNombre(m[4]) || estNombre(m[5])) continue;
      PiecePads p;
      p.ref = m[0];
      p.type = m[1];
      if (const auto a = p.type.find('@'); a != std::string::npos) p.decal = p.type.substr(a + 1), p.type = p.type.substr(0, a);
      p.x = nombre(m[2]);
      p.y = nombre(m[3]);
      p.angle = nombre(m[4]);
      p.miroir = m.size() >= 7 && m[6] == "M";
      p.alt = m.size() >= 8 ? std::atoi(m[7].c_str()) : 0;
      const int etiquettes = m.size() >= 12 ? std::atoi(m[11].c_str()) : 0;
      for (int k = 0; k < etiquettes; ++k) {
        lignes_.interne();
        if (polices_) lignes_.interne();
        lignes_.interne();
      }
      pieces_.push_back(p);
    }
  }
  int coucheDe(int l) const { return l; }
  Couche couche(int l) const {
    if (l <= 1) return Couche::CuivreDessus;
    if (l >= maxCouche_) return Couche::CuivreDessous;
    return cuivreInterne(std::clamp(l - 1, 1, MAX_CUIVRES - 2));
  }
  void routes() {
    std::string reseau;
    struct Point {
      double x, y;
      int couche;
      double largeur;
      std::string via, sens;
    };
    std::vector<Point> chemin;
    auto tracer = [&] {
      // Segments du chemin : la couche et la largeur du point de départ ; « 65 » (arrivée sur une pastille) prend celle
      // de l'autre bout ; un centre d'arc (CW, CCW) courbe le segment suivant
      for (std::size_t k = 0; k + 1 < chemin.size(); ++k) {
        const Point& a = chemin[k];
        if (!a.sens.empty()) continue;
        std::size_t j = k + 1;
        const Point* centre = nullptr;
        if (!chemin[j].sens.empty() && j + 1 < chemin.size()) centre = &chemin[j], ++j;
        const Point& b = chemin[j];
        int c = a.couche == 65 ? b.couche : a.couche;
        if (c == 65 || c <= 0 || c > maxCouche_) continue;
        const double l = a.largeur > 0 ? a.largeur : b.largeur;
        if (l <= 0) continue;
        Piste p;
        p.reseau = reseau;
        p.couche = couche(c);
        p.a = pt(a.x, a.y);
        p.b = pt(b.x, b.y);
        p.largeur = lg(l);
        if (centre) {
          const double r = std::hypot(a.x - centre->x, a.y - centre->y);
          const double t0 = std::atan2(a.y - centre->y, a.x - centre->x), t1 = std::atan2(b.y - centre->y, b.x - centre->x);
          double balayage = t1 - t0;
          if (centre->sens == "CCW")
            while (balayage <= 0) balayage += 2 * std::numbers::pi;
          else
            while (balayage >= 0) balayage -= 2 * std::numbers::pi;
          const double tm = t0 + balayage / 2;
          p.milieu = pt(centre->x + r * std::cos(tm), centre->y + r * std::sin(tm));
        }
        if (!(p.a == p.b)) pistes_.push_back(p);
      }
      for (const Point& a : chemin)
        if (!a.via.empty()) {
          Via v;
          v.reseau = reseau;
          v.p = pt(a.x, a.y);
          const auto d = viasDef_.find(a.via);
          if (d != viasDef_.end()) v.diametre = lg(d->second.first), v.percage = lg(d->second.second);
          if (std::none_of(vias_.begin(), vias_.end(), [&](const Via& w) { return w.p == v.p && w.reseau == v.reseau; })) vias_.push_back(v);
        }
      chemin.clear();
    };
    while (auto x = lignes_.suivante()) {
      if (estSection(*x)) {
        if (x->rfind("*SIGNAL*", 0) == 0) {
          tracer();
          const auto m = motsPads(*x);
          reseau = m.size() >= 2 ? m[1] : std::string();
          reseaux_[reseau];
          continue;
        }
        tracer();
        lignes_.reculer();
        return;
      }
      const auto m = motsPads(*x);
      if (m.empty()) continue;
      if (!estNombre(m[0])) {
        // Liaison « REF.BROCHE REF.BROCHE » : un nouveau chemin
        tracer();
        for (const auto& t : m)
          if (const auto p = t.find('.'); p != std::string::npos && p > 0 && t.rfind("@@@", 0) != 0 && t.rfind(".REUSE.", 0) != 0)
            reseaux_[reseau].insert({t.substr(0, p), t.substr(p + 1)});
        continue;
      }
      if (m.size() < 4) continue;
      Point p{nombre(m[0]), nombre(m[1]), std::atoi(m[2].c_str()), nombre(m[3]), {}, {}};
      for (std::size_t k = 4; k < m.size(); ++k) {
        if (m[k] == "CW" || m[k] == "CCW") p.sens = m[k];
        else if (viasDef_.count(m[k])) p.via = m[k];
      }
      chemin.push_back(p);
    }
    tracer();
  }
  void traits() {
    while (auto x = lignes_.suivante()) {
      if (estSection(*x)) {
        lignes_.reculer();
        return;
      }
      const auto m = motsPads(*x);
      if (m.size() < 5 || estNombre(m[1]) || !estNombre(m[2]) || !estNombre(m[3]) || !estNombre(m[4])) continue;
      const std::string type = m[1];
      const double ox = nombre(m[2]), oy = nombre(m[3]);
      const int pieces = std::atoi(m[4].c_str());
      int textes = 0;
      if (m.size() >= 8) textes = std::atoi(m[6].c_str());
      else if (m.size() == 7 && estNombre(m[6])) textes = std::atoi(m[6].c_str());
      if (auto y = lignes_.interne(); y && y->find(".REUSE.") == std::string::npos) lignes_.reculer();
      for (int k = 0; k < pieces; ++k) {
        const auto y = lignes_.interne();
        if (!y) return;
        const auto p = motsPads(*y);
        const int coins = p.size() >= 2 ? std::atoi(p[1].c_str()) : 0;
        std::vector<Coin> cs;
        for (int j = 0; j < coins; ++j)
          if (const auto z = lignes_.interne()) cs.push_back(lireCoin(*z));
        if (type == "BOARD" && !p.empty() && (p[0] == "CLOSED" || p[0] == "BRDCLS" || p[0] == "OPEN") && contour_.empty()) contour_ = pointsCoins(cs, ox, oy);
        if (type == "BOARD" && !p.empty() && (p[0] == "CIRCLE" || p[0] == "BRDCIR") && cs.size() >= 2 && contour_.empty()) {
          const double cx = ox + (cs[0].x + cs[1].x) / 2, cy = oy + (cs[0].y + cs[1].y) / 2, rr = std::hypot(cs[1].x - cs[0].x, cs[1].y - cs[0].y) / 2;
          for (int a = 0; a < 72; ++a) contour_.push_back({cx + rr * std::cos(a * std::numbers::pi / 36), cy + rr * std::sin(a * std::numbers::pi / 36)});
        }
      }
      for (int k = 0; k < textes; ++k) {
        lignes_.interne();
        if (polices_) lignes_.interne();
        lignes_.interne();
      }
    }
  }
  void pours() {
    while (auto x = lignes_.suivante()) {
      if (estSection(*x)) {
        lignes_.reculer();
        return;
      }
      const auto m = motsPads(*x);
      if (m.size() < 5 || estNombre(m[1]) || !estNombre(m[2]) || !estNombre(m[3]) || !estNombre(m[4])) continue;
      const std::string type = m[1];
      const double ox = nombre(m[2]), oy = nombre(m[3]);
      const int pieces = std::atoi(m[4].c_str());
      const std::string reseau = m.size() >= 8 ? m[7] : std::string();
      for (int k = 0; k < pieces; ++k) {
        const auto y = lignes_.interne();
        if (!y) return;
        const auto p = motsPads(*y);
        const int coins = p.size() >= 2 ? std::atoi(p[1].c_str()) : 0;
        const int arcs = p.size() >= 3 ? std::atoi(p[2].c_str()) : 0;
        std::vector<Coin> cs;
        for (int j = 0; j < coins + arcs; ++j)
          if (const auto z = lignes_.interne()) cs.push_back(lireCoin(*z));
        // Contour de la zone (POUROUT), sur la couche donnée en fin de pièce
        if (type == "POUROUT" && !p.empty() && p[0] == "POLY" && cs.size() >= 3) {
          auto contour = pointsCoins(cs, ox, oy);
          const int c = p.size() >= 5 ? std::atoi(p[4].c_str()) : 1;
          pours_.push_back({reseau + "#" + std::to_string(c), contour});
        }
      }
    }
  }

  cao_interne::CarteLue assembler(RapportLectureCao& r, std::vector<std::pair<std::string, Zone>>& zones) {
    cao_interne::CarteLue l;
    l.cuivres = std::clamp(maxCouche_, 2, 32);
    // Empreintes : une par decal, bornes et piles de pastilles
    std::set<std::string> utilises;
    for (const PiecePads& p : pieces_) {
      std::string decal = p.decal;
      if (decal.empty()) {
        const auto it = types_.find(p.type);
        if (it != types_.end() && !it->second.empty()) decal = it->second[std::clamp<std::size_t>(static_cast<std::size_t>(std::max(0, p.alt)), 0, it->second.size() - 1)];
        else decal = p.type;
      }
      cao_interne::CarteLue::Pose pose;
      pose.ref = p.ref;
      pose.decal = decal;
      pose.p = pt(p.x, p.y);
      pose.dessous = p.miroir;
      // Retournée : PADS tourne puis retourne de gauche à droite ; ici le miroir vient avant la rotation
      pose.rotation = p.miroir ? -p.angle : p.angle;
      l.poses.push_back(pose);
      utilises.insert(decal);
      l.netlist.composants.push_back({p.ref, p.type, decal});
    }
    for (const std::string& n : utilises) {
      const auto it = decals_.find(n);
      if (it == decals_.end()) {
        r.alertes.push_back("Empreinte (decal) « " + n + " » absente du fichier");
        continue;
      }
      const DecalPads& d = it->second;
      DefEmpreinte e;
      e.description = n;
      for (std::size_t k = 0; k < d.bornes.size(); ++k) {
        const auto& [x, y, nom] = d.bornes[k];
        auto pi = d.piles.find(static_cast<int>(k + 1));
        if (pi == d.piles.end()) pi = d.piles.find(0);
        if (pi == d.piles.end()) continue;
        const PilePads& s = pi->second;
        Pastille q;
        q.numero = nom;
        q.position = pt(x, y);
        q.percage = lg(s.percage);
        q.type = s.percage > 0 ? (s.metallise ? TypePastille::Traversante : TypePastille::NonMetallisee) : TypePastille::CMS;
        if (s.forme == "R" || s.forme == "A") {
          q.forme = FormePastille::Rond;
          q.largeur = q.hauteur = lg(s.taille);
        } else if (s.forme == "S") {
          q.forme = FormePastille::Rect;
          q.largeur = q.hauteur = lg(s.taille);
        } else if (s.forme == "OF") {
          q.forme = FormePastille::Ovale;
          q.largeur = lg(s.longueur);
          q.hauteur = lg(s.taille);
          q.rotation = s.angle;
        } else {
          q.forme = s.forme == "RC" ? FormePastille::RectArrondi : FormePastille::Rect;
          q.largeur = lg(s.longueur);
          q.hauteur = lg(s.taille);
          q.rotation = s.angle;
        }
        if (q.type == TypePastille::CMS && s.dessous && !s.dessus) q.couches = bit(Couche::CuivreDessous) | bit(Couche::VernisDessous) | bit(Couche::PateDessous);
        if (q.largeur > 0) e.pastilles.push_back(q);
      }
      for (const auto& t : d.traits)
        for (std::size_t k = 0; k + 1 < t.size(); ++k) {
          TraitEmpreinte s;
          s.a = pt(t[k].first, t[k].second);
          s.b = pt(t[k + 1].first, t[k + 1].second);
          s.epaisseur = 150'000;
          e.traits.push_back(s);
        }
      l.empreintes[n] = e;
    }
    for (const auto& [nom, broches] : reseaux_)
      for (const auto& [ref, b] : broches) l.netlist.reseaux[nom].push_back({ref, b});
    l.pistes = pistes_;
    l.vias = vias_;
    for (const auto& [x, y] : contour_) l.contour.push_back(pt(x, y));
    if (l.contour.size() > 1 && l.contour.front() == l.contour.back()) l.contour.pop_back();
    for (const auto& [cle, contour] : pours_) {
      Zone z;
      const auto d = cle.rfind('#');
      z.couche = couche(std::atoi(cle.c_str() + d + 1));
      for (const auto& [x, y] : contour) z.contour.push_back(pt(x, y));
      zones.push_back({cle.substr(0, d), z});
    }
    return l;
  }
};

// PADS Logic : pièces (type, decal du type) et signaux
NetlistLue lireLogic(const std::string& texte) {
  NetlistLue n;
  Lignes l(texte);
  std::map<std::string, std::string> decalDuType;
  std::string section, reseau;
  bool polices = true;
  if (const auto h = l.suivante()) {
    const auto v = h->find("-V");
    if (v != std::string::npos) polices = std::atoi(h->c_str() + v + 2) >= 9 || std::atoi(h->c_str() + v + 2) == 0;
  }
  std::set<std::string> vues;
  while (auto x = l.suivante()) {
    if (estSection(*x)) {
      section = nomSection(*x);
      if (section == "*SIGNAL*") {
        const auto m = motsPads(*x);
        reseau = m.size() >= 2 ? m[1] : std::string();
        n.reseaux[reseau];
      }
      continue;
    }
    const auto m = motsPads(*x);
    if (m.empty()) continue;
    if (section == "*PARTTYPE*" && m.size() >= 2 && !estNombre(m[0]) && m[0] != "G" && m[0].find('.') == std::string::npos) decalDuType[m[0]] = m[1].substr(0, m[1].find(':'));
    if (section == "*PART*" && m.size() >= 4 && !estNombre(m[0]) && estNombre(m[2]) && estNombre(m[3])) {
      std::string type = m[1], decal;
      if (const auto a = type.find('@'); a != std::string::npos) decal = type.substr(a + 1), type = type.substr(0, a);
      if (vues.insert(m[0]).second) n.composants.push_back({m[0], type, decal});
    }
    if (section == "*SIGNAL*" && !estNombre(m[0]))
      for (const auto& t : m)
        if (const auto p = t.find('.'); p != std::string::npos && p > 0 && t.rfind("@@@", 0) != 0) {
          const std::pair<std::string, std::string> b{t.substr(0, p), t.substr(p + 1)};
          auto& v = n.reseaux[reseau];
          if (std::find(v.begin(), v.end(), b) == v.end()) v.push_back(b);
        }
  }
  (void)polices;
  for (auto& c : n.composants)
    if (c.empreinte.empty() && decalDuType.count(c.valeur)) c.empreinte = decalDuType[c.valeur];
  return n;
}
}  // namespace

bool estPadsAscii(const std::string& t) { return t.rfind("!PADS-", 0) == 0 || t.rfind("*PADS-", 0) == 0 || t.find("*PADS-LOGIC") != std::string::npos || t.find("!PADS-POWERLOGIC") != std::string::npos; }

ConceptionElec conceptionDepuisPads(const std::optional<std::string>& logic, const std::optional<std::string>& layout, const std::string& nom, RapportLectureCao* rapport) {
  RapportLectureCao r;
  std::optional<NetlistLue> schema;
  if (logic) schema = lireLogic(*logic);
  ConceptionElec c;
  if (layout) {
    LecteurPads lecteur(*layout);
    std::vector<std::pair<std::string, Zone>> zones;
    cao_interne::CarteLue l = lecteur.lire(r, zones);
    // Les valeurs viennent du schéma s'il est donné
    if (schema)
      for (auto& x : l.netlist.composants)
        for (const auto& y : schema->composants)
          if (y.reference == x.reference && !y.valeur.empty()) x.valeur = y.valeur;
    c = cao_interne::assemblerCarte(l, nom, "pads:", r);
    for (auto& [reseau, z] : zones) {
      z.uuid = nouvelUuid();
      z.reseau = reseau;
      c.circuit.zones.push_back(z);
    }
    if (schema) {
      // Le schéma relie : ses réseaux doivent être ceux de la carte
      ConceptionElec s = conceptionDepuisNetlist(*schema, nom);
      const RapportLiaison li = relierCarteAuSchema(s, c);
      for (const auto& e : li.ecarts) r.alertes.push_back(e);
      c = std::move(s);
    }
  } else if (schema) {
    c = conceptionDepuisNetlist(*schema, nom);
    c.schemaSeul = true;
    r.composants = static_cast<int>(schema->composants.size());
    r.reseaux = static_cast<int>(schema->reseaux.size());
  } else
    c = nouvelleConception(nom);
  if (rapport) *rapport = r;
  return c;
}

// ======================================================================================================================
// Écrivains (unités en mils)
// ======================================================================================================================
namespace {
std::string mil(Nm v) { return nbp(static_cast<double>(v) / 25'400.0, 3); }
std::string milY(Nm v) { return nbp(-static_cast<double>(v) / 25'400.0, 3); }
int couchePads(Couche k, int n) {
  if (k == Couche::CuivreDessus) return 1;
  if (k == Couche::CuivreDessous) return n;
  return rangCuivre(k, n) + 1;
}
}  // namespace

std::string padsAsciiCarte(const ConceptionElec& c, RapportEcritureCao* rapport) {
  RapportEcritureCao r;
  const int n = c.circuit.empilage.couchesCuivre;
  std::ostringstream o;
  o << "!PADS-POWERPCB-V9.0-MILS! DESIGN DATABASE ASCII FILE 1.0\n\n*PCB*        GENERAL PARAMETERS OF THE PCB DESIGN\n\nUNITS        0              2=Inches 1=Metric 0=Mils\nMAXIMUMLAYER "
    << n << "\nWORKLEVEL    1\nJOBNAME      " << sansBlanc(c.nom) << "\n\n*REMARK* Écrit par Prométhée\n\n";
  // Contour
  const Polygone contour = c.circuit.contour.polygone();
  o << "*LINES*      LINES ITEMS\n\nBOARD            BOARD    0 0 1\nCLOSED " << contour.size() + 1 << " 10 0\n";
  for (std::size_t i = 0; i <= contour.size(); ++i) o << mil(contour[i % contour.size()].x) << " " << milY(contour[i % contour.size()].y) << "\n";
  o << "\n";
  // Vias
  std::map<std::pair<Nm, Nm>, std::string> viasDef;
  for (const auto& v : c.circuit.vias) viasDef.emplace(std::make_pair(v.diametre, v.percage), "VIA_" + mil(v.diametre) + "_" + mil(v.percage));
  o << "*VIA*  ITEMS\n\n";
  for (const auto& [dim, nom] : viasDef) o << nom << " " << mil(dim.second) << " 3\n-2 " << mil(dim.first) << " R\n-1 " << mil(dim.first) << " R\n0  " << mil(dim.first) << " R\n\n";
  // Decals : une pile par pastille ; sérigraphie du dessus en pièces ouvertes
  o << "*PARTDECAL*  ITEMS\n\n";
  std::set<std::string> decals;
  for (const auto& em : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(em.empreinte);
    if (!d || !decals.insert(em.empreinte).second) continue;
    std::vector<const TraitEmpreinte*> traits;
    for (const auto& t : d->traits)
      if (t.couche == Couche::SerigraphieDessus && t.rayon == 0) traits.push_back(&t);
    o << sansBlanc(em.empreinte) << " I 0 0 " << traits.size() << " " << d->pastilles.size() << " " << d->pastilles.size() << " 0 0\n";
    for (const auto* t : traits) o << "OPEN   2   " << mil(t->epaisseur) << " 26\n" << mil(t->a.x) << " " << milY(t->a.y) << "\n" << mil(t->b.x) << " " << milY(t->b.y) << "\n";
    int k = 0;
    for (const auto& p : d->pastilles) o << "T" << mil(p.position.x) << " " << milY(p.position.y) << " " << mil(p.position.x) << " " << milY(p.position.y) << " " << (p.numero.empty() ? "MH" + std::to_string(++k) : sansBlanc(p.numero)) << "\n";
    int i = 0;
    for (const auto& p : d->pastilles) {
      ++i;
      const bool trou = p.percage > 0;
      o << "PAD " << i << " 3" << (trou ? std::string(p.type == TypePastille::NonMetallisee ? " N" : " P") : std::string()) << "\n";
      auto ligne = [&](int niveau, bool present) {
        if (!present) return std::string(std::to_string(niveau) + " 0 R\n");
        std::string forme;
        if (p.forme == FormePastille::Rond) forme = " R";
        else if (p.forme == FormePastille::Ovale) forme = " OF " + nbp(p.rotation) + " " + mil(std::max(p.largeur, p.hauteur)) + " 0";
        else forme = " RF " + nbp(p.rotation) + " " + mil(std::max(p.largeur, p.hauteur)) + " 0";
        const Nm taille = p.forme == FormePastille::Rond ? p.largeur : std::min(p.largeur, p.hauteur);
        return std::to_string(niveau) + " " + mil(taille) + forme + (trou && niveau == -2 ? " " + mil(p.percage) : std::string()) + "\n";
      };
      const bool basse = p.couches && (p.couches & bit(Couche::CuivreDessous)) && !(p.couches & bit(Couche::CuivreDessus));
      o << ligne(-2, trou || !basse) << ligne(-1, trou) << ligne(0, trou || basse);
    }
    o << "\n";
  }
  // Types et pièces
  o << "*PARTTYPE*   ITEMS\n\n";
  std::set<std::string> types;
  auto typeDe = [&](const EmpreintePlacee& em, const ComposantElec& x) { return sansBlanc(x.valeur.empty() ? em.empreinte : x.valeur) + "_" + sansBlanc(em.empreinte); };
  for (const auto& em : c.circuit.empreintes)
    if (const ComposantElec* x = c.composant(em.composant))
      if (types.insert(typeDe(em, *x)).second) o << typeDe(em, *x) << " " << sansBlanc(em.empreinte) << " I UND 0 0 0 0 Y\n\n";
  o << "*PART*       ITEMS\n\n";
  for (const auto& em : c.circuit.empreintes) {
    const ComposantElec* x = c.composant(em.composant);
    if (!x) continue;
    const double angle = std::fmod(std::fmod(em.dessous ? -em.rotation : em.rotation, 360) + 360, 360);
    o << sansBlanc(x->reference) << " " << typeDe(em, *x) << " " << mil(em.position.x) << " " << milY(em.position.y) << " " << nbp(angle) << " U " << (em.dessous ? "M" : "N") << " 0 -1 0 -1 0\n";
    ++r.composants;
  }
  // Routage : par signal, les liaisons de proche en proche entre broches, puis chaque segment de cuivre en liaison
  // propre (de la broche la plus proche de son départ à celle la plus proche de son arrivée)
  const Connectivite k = connectivite(c);
  std::map<std::string, std::vector<std::pair<std::string, Pt>>> brochesDe;
  for (const auto& p : pastilles(c, k))
    if (!p.reseau.empty() && !p.numero.empty()) brochesDe[p.reseau].push_back({sansBlanc(p.reference) + "." + sansBlanc(p.numero), p.pastille.position});
  o << "\n*ROUTE*\n\n";
  std::set<std::string> nets;
  for (const auto& [nom, _] : brochesDe) nets.insert(nom);
  for (const auto& p : c.circuit.pistes) nets.insert(p.reseau);
  for (const std::string& nom : nets) {
    if (nom.empty()) continue;
    const auto& b = brochesDe[nom];
    auto proche = [&](Pt q) {
      std::string meilleur = b.empty() ? std::string() : b.front().first;
      double d = 1e300;
      for (const auto& [n2, p2] : b)
        if (distance(q, p2) < d) d = distance(q, p2), meilleur = n2;
      return meilleur;
    };
    o << "*SIGNAL* " << sansBlanc(nom) << " 0 -2\n";
    for (std::size_t i = 0; i + 1 < b.size(); ++i) o << b[i].first << " " << b[i + 1].first << "\n";
    ++r.reseaux;
    if (b.empty()) continue;
    for (const auto& p : c.circuit.pistes) {
      if (p.reseau != nom) continue;
      for (const auto& [a, e] : segmentsPiste(p, 20'000)) {
        o << proche(a) << " " << proche(e) << "\n" << mil(a.x) << " " << milY(a.y) << " " << couchePads(p.couche, n) << " " << mil(p.largeur) << " 0\n" << mil(e.x) << " " << milY(e.y) << " "
          << couchePads(p.couche, n) << " " << mil(p.largeur) << " 0\n";
        ++r.pistes;
      }
    }
    for (const auto& v : c.circuit.vias)
      if (v.reseau == nom) {
        o << proche(v.p) << " " << proche(v.p) << "\n" << mil(v.p.x) << " " << milY(v.p.y) << " 1 0 0 " << viasDef.at({v.diametre, v.percage}) << "\n";
        ++r.vias;
      }
  }
  o << "\n*END*     OF ASCII OUTPUT FILE\n";
  for (const auto& z : c.circuit.zones) r.nonEcrits.push_back("zone de cuivre « " + z.reseau + " » sur " + codeCouche(z.couche) + " (à remplir de nouveau dans l'outil d'arrivée)");
  if (!c.circuit.regles.classes.empty()) r.nonEcrits.push_back(std::to_string(c.circuit.regles.classes.size()) + " classe(s) de réseaux et leurs règles");
  r.nonEcrits.push_back("empilage détaillé (épaisseurs et matériaux)");
  for (const auto& p : c.circuit.pistes)
    if (p.milieu) {
      r.nonEcrits.push_back("arcs de piste écrits en cordes");
      break;
    }
  if (rapport) *rapport = r;
  return o.str();
}

std::string padsAsciiSchema(const ConceptionElec& c, RapportEcritureCao* rapport) {
  RapportEcritureCao r;
  std::ostringstream o;
  o << "*PADS-LOGIC-V9.0* DESIGN EXPORT FILE FROM PROMETHEE\n\n*SCH*\nUNITS 0\nJOBNAME \"" << c.nom << "\"\n\n*SHT* 1 Feuille1 0 Root\n\n*PARTTYPE*\n";
  std::map<std::string, std::string> types;
  for (const auto& x : c.composants) {
    if (x.symbole.empty() || x.reference.empty() || x.reference[0] == '#') continue;
    types[sansBlanc(x.valeur.empty() ? "?" : x.valeur) + "_" + sansBlanc(x.empreinte.empty() ? "?" : x.empreinte)] = sansBlanc(x.empreinte.empty() ? "?" : x.empreinte);
  }
  for (const auto& [t, d] : types) o << t << " " << d << " I UND 0 0 0 0 Y\n";
  o << "\n*PART*\n";
  int y = 0;
  for (const auto& x : c.composants) {
    if (x.symbole.empty() || x.reference.empty() || x.reference[0] == '#') continue;
    o << sansBlanc(x.reference) << " " << sansBlanc(x.valeur.empty() ? "?" : x.valeur) + "_" + sansBlanc(x.empreinte.empty() ? "?" : x.empreinte) << " 0 " << (y += 500) << " 0 0 50 69 50 69 0 0 0 0 0 0\n\"Arial\"\n\"Arial\"\n";
    ++r.composants;
  }
  o << "\n*CONNECTION*\n";
  for (const auto& [nom, noeuds] : netlistDe(c).reseaux) {
    if (noeuds.empty()) continue;
    o << "*SIGNAL* " << sansBlanc(nom) << " 0 0\n";
    for (std::size_t i = 0; i + 1 < noeuds.size(); ++i) o << sansBlanc(noeuds[i].first) << "." << sansBlanc(noeuds[i].second) << " " << sansBlanc(noeuds[i + 1].first) << "." << sansBlanc(noeuds[i + 1].second) << " 2 0\n0 0\n0 0\n";
    if (noeuds.size() == 1) o << sansBlanc(noeuds[0].first) << "." << sansBlanc(noeuds[0].second) << " @@@O1 2 0\n0 0\n0 0\n";
    ++r.reseaux;
  }
  o << "\n*END*\n";
  r.nonEcrits.push_back("dessin des feuilles (symboles, fils, étiquettes) : pièces et signaux seulement");
  if (rapport) *rapport = r;
  return o.str();
}

}  // namespace prom::ecad
