// Prométhée : lecture d'un dessin SVG en formes (lot L7, tâche 7.19). Voir lecture_svg.hpp.
// Licence GPL-3.0-only.
#include "promethee/lecture_svg.hpp"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <optional>
#include <set>

namespace prom::svg {
namespace {

constexpr double PI = std::numbers::pi;

// Transformation affine x' = a x + c y + e, y' = b x + d y + f (notation SVG)
struct Affine {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  Pt2 operator()(Pt2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  Affine operator*(const Affine& o) const {  // this ∘ o
    return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.e + c * o.f + e, b * o.e + d * o.f + f};
  }
  bool sansRotation() const { return std::abs(b) < 1e-12 && std::abs(c) < 1e-12; }
  double echelleMoyenne() const { return std::sqrt(std::abs(a * d - b * c)); }
};

struct Balise {
  std::string nom;
  std::map<std::string, std::string> attributs;
  bool fermante = false, autoFermee = false;
};

// Balises successives du texte (commentaires, déclarations et CDATA sautés)
class Lecteur {
 public:
  explicit Lecteur(const std::string& t) : m_t(t) {}
  std::optional<Balise> suivante() {
    while (true) {
      const auto lt = m_t.find('<', m_i);
      if (lt == std::string::npos) return std::nullopt;
      m_i = lt;
      if (m_t.compare(m_i, 4, "<!--") == 0) {
        const auto fin = m_t.find("-->", m_i);
        m_i = fin == std::string::npos ? m_t.size() : fin + 3;
        continue;
      }
      if (m_t.compare(m_i, 9, "<![CDATA[") == 0) {
        const auto fin = m_t.find("]]>", m_i);
        m_i = fin == std::string::npos ? m_t.size() : fin + 3;
        continue;
      }
      if (m_t.compare(m_i, 2, "<?") == 0 || m_t.compare(m_i, 2, "<!") == 0) {
        const auto fin = m_t.find('>', m_i);
        m_i = fin == std::string::npos ? m_t.size() : fin + 1;
        continue;
      }
      break;
    }
    Balise b;
    ++m_i;
    if (m_i < m_t.size() && m_t[m_i] == '/') {
      b.fermante = true;
      ++m_i;
    }
    while (m_i < m_t.size() && !std::isspace(static_cast<unsigned char>(m_t[m_i])) && m_t[m_i] != '>' && m_t[m_i] != '/') b.nom += m_t[m_i++];
    if (const auto p = b.nom.find(':'); p != std::string::npos) b.nom = b.nom.substr(p + 1);  // svg:path
    while (m_i < m_t.size()) {
      while (m_i < m_t.size() && std::isspace(static_cast<unsigned char>(m_t[m_i]))) ++m_i;
      if (m_i >= m_t.size()) break;
      if (m_t[m_i] == '>') {
        ++m_i;
        break;
      }
      if (m_t[m_i] == '/') {
        b.autoFermee = true;
        ++m_i;
        continue;
      }
      std::string nom;
      while (m_i < m_t.size() && m_t[m_i] != '=' && m_t[m_i] != '>' && !std::isspace(static_cast<unsigned char>(m_t[m_i])) && m_t[m_i] != '/') nom += m_t[m_i++];
      while (m_i < m_t.size() && std::isspace(static_cast<unsigned char>(m_t[m_i]))) ++m_i;
      std::string valeur;
      if (m_i < m_t.size() && m_t[m_i] == '=') {
        ++m_i;
        while (m_i < m_t.size() && std::isspace(static_cast<unsigned char>(m_t[m_i]))) ++m_i;
        if (m_i < m_t.size() && (m_t[m_i] == '"' || m_t[m_i] == '\'')) {
          const char q = m_t[m_i++];
          const auto fin = m_t.find(q, m_i);
          valeur = m_t.substr(m_i, fin == std::string::npos ? std::string::npos : fin - m_i);
          m_i = fin == std::string::npos ? m_t.size() : fin + 1;
        }
      }
      if (!nom.empty()) b.attributs[nom] = valeur;
    }
    return b;
  }

 private:
  const std::string& m_t;
  std::size_t m_i = 0;
};

// Nombres d'une liste SVG (« 10,20 30-4.5e1 .5.5 »)
class Nombres {
 public:
  explicit Nombres(const std::string& s, std::size_t i = 0) : m_s(s), m_i(i) {}
  void sauter() {
    while (m_i < m_s.size() && (std::isspace(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == ',')) ++m_i;
  }
  bool nombreSuit() {
    sauter();
    if (m_i >= m_s.size()) return false;
    const char ch = m_s[m_i];
    return std::isdigit(static_cast<unsigned char>(ch)) || ch == '-' || ch == '+' || ch == '.';
  }
  double lire() {
    sauter();
    const std::size_t debut = m_i;
    if (m_i < m_s.size() && (m_s[m_i] == '-' || m_s[m_i] == '+')) ++m_i;
    bool point = false;
    while (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) || (m_s[m_i] == '.' && !point))) {
      if (m_s[m_i] == '.') point = true;
      ++m_i;
    }
    if (m_i < m_s.size() && (m_s[m_i] == 'e' || m_s[m_i] == 'E')) {
      ++m_i;
      if (m_i < m_s.size() && (m_s[m_i] == '-' || m_s[m_i] == '+')) ++m_i;
      while (m_i < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_i]))) ++m_i;
    }
    try {
      return std::stod(m_s.substr(debut, m_i - debut));
    } catch (...) {
      return 0;
    }
  }
  // Drapeau d'arc : un seul chiffre, éventuellement collé au suivant (« 0 01 »)
  int drapeau() {
    sauter();
    if (m_i < m_s.size() && (m_s[m_i] == '0' || m_s[m_i] == '1')) return m_s[m_i++] - '0';
    return 0;
  }
  bool fin() {
    sauter();
    return m_i >= m_s.size();
  }
  char lettre() {
    sauter();
    if (m_i < m_s.size() && std::isalpha(static_cast<unsigned char>(m_s[m_i])) && m_s[m_i] != 'e' && m_s[m_i] != 'E') return m_s[m_i++];
    return 0;
  }

 private:
  const std::string& m_s;
  std::size_t m_i;
};

double longueurMm(const std::string& s, double defautPx, bool* avecUnite = nullptr) {
  if (s.empty()) return defautPx;
  Nombres n(s);
  const double v = n.lire();
  std::string u;
  for (char ch : s)
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '%') u += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (avecUnite) *avecUnite = !u.empty() && u != "px";
  if (u == "mm") return v;
  if (u == "cm") return v * 10;
  if (u == "in") return v * 25.4;
  if (u == "pt") return v * 25.4 / 72;
  if (u == "pc") return v * 25.4 / 6;
  return v * 25.4 / 96;  // px ou sans unité
}

Affine lireTransformation(const std::string& s) {
  Affine r;
  std::size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) ++i;
    std::string nom;
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) nom += s[i++];
    const auto ouv = s.find('(', i), ferm = s.find(')', i);
    if (nom.empty() || ouv == std::string::npos || ferm == std::string::npos) break;
    const std::string args = s.substr(ouv + 1, ferm - ouv - 1);
    i = ferm + 1;
    std::vector<double> v;
    Nombres n(args);
    while (n.nombreSuit()) v.push_back(n.lire());
    Affine t;
    if (nom == "matrix" && v.size() >= 6) t = {v[0], v[1], v[2], v[3], v[4], v[5]};
    else if (nom == "translate" && !v.empty()) t = {1, 0, 0, 1, v[0], v.size() > 1 ? v[1] : 0};
    else if (nom == "scale" && !v.empty()) t = {v[0], 0, 0, v.size() > 1 ? v[1] : v[0], 0, 0};
    else if (nom == "rotate" && !v.empty()) {
      const double a = v[0] * PI / 180, co = std::cos(a), si = std::sin(a);
      const Affine rot{co, si, -si, co, 0, 0};
      if (v.size() >= 3) t = Affine{1, 0, 0, 1, v[1], v[2]} * rot * Affine{1, 0, 0, 1, -v[1], -v[2]};
      else t = rot;
    } else if (nom == "skewX" && !v.empty()) t = {1, 0, std::tan(v[0] * PI / 180), 1, 0, 0};
    else if (nom == "skewY" && !v.empty()) t = {1, std::tan(v[0] * PI / 180), 0, 1, 0, 0};
    r = r * t;
  }
  return r;
}

std::map<std::string, std::string> lireStyle(const std::string& s) {
  std::map<std::string, std::string> r;
  std::size_t i = 0;
  while (i < s.size()) {
    const auto pv = s.find(';', i);
    const std::string decl = s.substr(i, pv == std::string::npos ? std::string::npos : pv - i);
    if (const auto dp = decl.find(':'); dp != std::string::npos) {
      auto net = [](std::string x) {
        while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) x.erase(x.begin());
        while (!x.empty() && std::isspace(static_cast<unsigned char>(x.back()))) x.pop_back();
        return x;
      };
      r[net(decl.substr(0, dp))] = net(decl.substr(dp + 1));
    }
    if (pv == std::string::npos) break;
    i = pv + 1;
  }
  return r;
}

std::string couleur(const std::string& s) {
  static const std::map<std::string, std::string> noms = {{"black", "#000000"}, {"white", "#FFFFFF"}, {"red", "#FF0000"}, {"green", "#008000"}, {"blue", "#0000FF"},
                                                          {"yellow", "#FFFF00"}, {"gray", "#808080"}, {"grey", "#808080"}, {"orange", "#FFA500"}, {"purple", "#800080"}};
  if (s.empty() || s == "none" || s == "transparent") return {};
  if (s[0] == '#') {
    if (s.size() == 4) {
      std::string r = "#";
      for (int k = 1; k <= 3; ++k) r += std::string(2, static_cast<char>(std::toupper(static_cast<unsigned char>(s[static_cast<std::size_t>(k)]))));
      return r;
    }
    std::string r = s.substr(0, 7);
    for (auto& ch : r) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return r;
  }
  if (s.rfind("rgb(", 0) == 0) {
    Nombres n(s, 4);
    int v[3] = {0, 0, 0};
    for (int& x : v) x = static_cast<int>(std::lround(n.lire()));
    char t[8];
    std::snprintf(t, sizeof t, "#%02X%02X%02X", v[0] & 255, v[1] & 255, v[2] & 255);
    return t;
  }
  if (const auto it = noms.find(s); it != noms.end()) return it->second;
  return "#000000";
}

struct Contexte {
  Affine m;  // de l'élément vers le mm
  std::string trait, fond = "#000000";
  double epaisseur = 1;  // unités utilisateur
  std::string calque;
};

class Construction {
 public:
  explicit Construction(DessinSvg& d) : m_d(d) {}

  void ligne(const Contexte& k, const std::vector<Pt2>& pts, bool ferme) {
    if (pts.size() < 2) return;
    FormeSvg f = base(k, ferme);
    f.genre = FormeSvg::Genre::Polyligne;
    for (auto p : pts) f.points.push_back(k.m(p));
    f.ferme = ferme;
    m_d.formes.push_back(std::move(f));
  }
  void bezier(const Contexte& k, Pt2 a, Pt2 b, Pt2 c, Pt2 d) {
    FormeSvg f = base(k, false);
    f.genre = FormeSvg::Genre::Bezier;
    f.points = {k.m(a), k.m(b), k.m(c), k.m(d)};
    m_d.formes.push_back(std::move(f));
  }
  void ellipse(const Contexte& k, Pt2 c, double rx, double ry) {
    if (rx <= 0 || ry <= 0) return;
    if (k.m.sansRotation()) {
      FormeSvg f = base(k, true);
      f.rx = rx * std::abs(k.m.a);
      f.ry = ry * std::abs(k.m.d);
      f.genre = std::abs(f.rx - f.ry) < 1e-9 ? FormeSvg::Genre::Cercle : FormeSvg::Genre::Ellipse;
      f.points = {k.m(c)};
      f.ferme = true;
      m_d.formes.push_back(std::move(f));
      return;
    }
    // Déformée : quatre quarts en Bézier (κ = 0,5523), exacts à la transformation près
    constexpr double K = 0.5522847498;
    const Pt2 e{c.x + rx, c.y}, s{c.x, c.y + ry}, o{c.x - rx, c.y}, n{c.x, c.y - ry};
    bezier(k, e, {e.x, e.y + K * ry}, {s.x + K * rx, s.y}, s);
    bezier(k, s, {s.x - K * rx, s.y}, {o.x, o.y + K * ry}, o);
    bezier(k, o, {o.x, o.y - K * ry}, {n.x - K * rx, n.y}, n);
    bezier(k, n, {n.x + K * rx, n.y}, {e.x, e.y - K * ry}, e);
  }
  void rectangle(const Contexte& k, double x, double y, double w, double h, double rx, double ry) {
    if (w <= 0 || h <= 0) return;
    if (k.m.sansRotation() && std::abs(rx - ry) < 1e-9) {
      FormeSvg f = base(k, true);
      f.genre = FormeSvg::Genre::Rectangle;
      f.points = {k.m({x, y}), k.m({x + w, y + h})};
      f.rx = rx * k.m.echelleMoyenne();
      f.ferme = true;
      m_d.formes.push_back(std::move(f));
      return;
    }
    if (rx <= 0 && ry <= 0) {
      ligne(k, {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, true);
      return;
    }
    // Coins arrondis déformés : côtés en traits, coins en quarts d'ellipse
    constexpr double K = 0.5522847498;
    ligne(k, {{x + rx, y}, {x + w - rx, y}}, false);
    bezier(k, {x + w - rx, y}, {x + w - rx + K * rx, y}, {x + w, y + ry - K * ry}, {x + w, y + ry});
    ligne(k, {{x + w, y + ry}, {x + w, y + h - ry}}, false);
    bezier(k, {x + w, y + h - ry}, {x + w, y + h - ry + K * ry}, {x + w - rx + K * rx, y + h}, {x + w - rx, y + h});
    ligne(k, {{x + w - rx, y + h}, {x + rx, y + h}}, false);
    bezier(k, {x + rx, y + h}, {x + rx - K * rx, y + h}, {x, y + h - ry + K * ry}, {x, y + h - ry});
    ligne(k, {{x, y + h - ry}, {x, y + ry}}, false);
    bezier(k, {x, y + ry}, {x, y + ry - K * ry}, {x + rx - K * rx, y}, {x + rx, y});
  }
  // Arc elliptique de SVG (paramètres d'extrémités) en Bézier cubiques, au plus 90° par morceau
  void arc(const Contexte& k, Pt2 p0, double rx, double ry, double phiDeg, int grand, int sens, Pt2 p1, std::vector<Pt2>& courant) {
    if (std::abs(p0.x - p1.x) < 1e-12 && std::abs(p0.y - p1.y) < 1e-12) return;
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx < 1e-12 || ry < 1e-12) {
      courant.push_back(p1);
      return;
    }
    const double phi = phiDeg * PI / 180, co = std::cos(phi), si = std::sin(phi);
    const double dx = (p0.x - p1.x) / 2, dy = (p0.y - p1.y) / 2;
    const double x1 = co * dx + si * dy, y1 = -si * dx + co * dy;
    const double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
    if (lambda > 1) {
      rx *= std::sqrt(lambda);
      ry *= std::sqrt(lambda);
    }
    const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1, den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double coef = den > 0 ? std::sqrt(std::max(0.0, num / den)) : 0;
    if (grand == sens) coef = -coef;
    const double cx1 = coef * rx * y1 / ry, cy1 = -coef * ry * x1 / rx;
    const double cx = co * cx1 - si * cy1 + (p0.x + p1.x) / 2, cy = si * cx1 + co * cy1 + (p0.y + p1.y) / 2;
    auto angle = [](double ux, double uy, double vx, double vy) {
      const double a = std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
      return a;
    };
    const double t1 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
    double dt = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
    if (!sens && dt > 0) dt -= 2 * PI;
    if (sens && dt < 0) dt += 2 * PI;
    const int n = std::max(1, static_cast<int>(std::ceil(std::abs(dt) / (PI / 2) - 1e-9)));
    const double pasA = dt / n, alpha = 4.0 / 3.0 * std::tan(pasA / 4);
    auto point = [&](double t) { return Pt2{cx + rx * std::cos(t) * co - ry * std::sin(t) * si, cy + rx * std::cos(t) * si + ry * std::sin(t) * co}; };
    auto derivee = [&](double t) { return Pt2{-rx * std::sin(t) * co - ry * std::cos(t) * si, -rx * std::sin(t) * si + ry * std::cos(t) * co}; };
    vider(k, courant);
    for (int i = 0; i < n; ++i) {
      const double ta = t1 + pasA * i, tb = ta + pasA;
      const Pt2 a = point(ta), b = point(tb), da = derivee(ta), db = derivee(tb);
      bezier(k, a, {a.x + alpha * da.x, a.y + alpha * da.y}, {b.x - alpha * db.x, b.y - alpha * db.y}, i + 1 == n ? p1 : b);
    }
    courant = {p1};
  }
  // Polyligne en cours d'un chemin, écrite dès qu'une courbe la coupe
  void vider(const Contexte& k, std::vector<Pt2>& courant, bool ferme = false) {
    if (courant.size() >= 2) ligne(k, courant, ferme);
    if (!courant.empty()) courant = {courant.back()};
  }

  void chemin(const Contexte& k, const std::string& d) {
    Nombres n(d);
    Pt2 p{}, depart{}, ctrl{};
    char cmd = 0, precedente = 0;
    std::vector<Pt2> courant;
    while (!n.fin()) {
      if (const char l = n.lettre()) cmd = l;
      else if (!cmd) break;
      const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
      const char C = static_cast<char>(std::toupper(static_cast<unsigned char>(cmd)));
      auto lirePt = [&] {
        const double x = n.lire(), y = n.lire();
        return rel ? Pt2{p.x + x, p.y + y} : Pt2{x, y};
      };
      switch (C) {
        case 'M': {
          vider(k, courant);
          p = depart = lirePt();
          courant = {p};
          cmd = rel ? 'l' : 'L';  // les paires suivantes sont des traits
          break;
        }
        case 'L': p = lirePt(); courant.push_back(p); break;
        case 'H': {
          const double x = n.lire();
          p.x = rel ? p.x + x : x;
          courant.push_back(p);
          break;
        }
        case 'V': {
          const double y = n.lire();
          p.y = rel ? p.y + y : y;
          courant.push_back(p);
          break;
        }
        case 'C': {
          const Pt2 c1 = lirePt(), c2 = lirePt(), q = lirePt();
          vider(k, courant);
          bezier(k, p, c1, c2, q);
          ctrl = c2;
          p = q;
          courant = {p};
          break;
        }
        case 'S': {
          const Pt2 c1 = (precedente == 'C' || precedente == 'S') ? Pt2{2 * p.x - ctrl.x, 2 * p.y - ctrl.y} : p;
          const Pt2 c2 = lirePt(), q = lirePt();
          vider(k, courant);
          bezier(k, p, c1, c2, q);
          ctrl = c2;
          p = q;
          courant = {p};
          break;
        }
        case 'Q':
        case 'T': {
          const Pt2 c = C == 'Q' ? lirePt() : ((precedente == 'Q' || precedente == 'T') ? Pt2{2 * p.x - ctrl.x, 2 * p.y - ctrl.y} : p);
          const Pt2 q = lirePt();
          vider(k, courant);
          // Quadratique élevée en cubique : contrôles aux deux tiers
          bezier(k, p, {p.x + 2.0 / 3 * (c.x - p.x), p.y + 2.0 / 3 * (c.y - p.y)}, {q.x + 2.0 / 3 * (c.x - q.x), q.y + 2.0 / 3 * (c.y - q.y)}, q);
          ctrl = c;
          p = q;
          courant = {p};
          break;
        }
        case 'A': {
          const double rx = n.lire(), ry = n.lire(), phi = n.lire();
          const int grand = n.drapeau(), sens = n.drapeau();
          const Pt2 q = lirePt();
          arc(k, p, rx, ry, phi, grand, sens, q, courant);
          p = q;
          break;
        }
        case 'Z': {
          if (courant.size() >= 2) {
            const bool ferme = std::abs(courant.front().x - depart.x) < 1e-12 && std::abs(courant.front().y - depart.y) < 1e-12;
            if (ferme) {
              ligne(k, courant, true);
              courant.clear();
            } else {
              courant.push_back(depart);
              vider(k, courant);
            }
          } else if (std::abs(p.x - depart.x) > 1e-12 || std::abs(p.y - depart.y) > 1e-12) {
            ligne(k, {p, depart}, false);
          }
          p = depart;
          courant = {p};
          break;
        }
        default:
          m_d.alertes.push_back(std::string("commande de chemin inconnue : ") + cmd);
          return;
      }
      precedente = C;
    }
    vider(k, courant);
  }

 private:
  FormeSvg base(const Contexte& k, bool fermable) const {
    FormeSvg f;
    f.trait = k.trait;
    f.fond = fermable ? k.fond : std::string();
    f.fondChemin = k.fond;
    f.epaisseur = k.trait.empty() ? 0 : k.epaisseur * k.m.echelleMoyenne();
    f.calque = k.calque;
    return f;
  }
  DessinSvg& m_d;
};

double attr(const Balise& b, const char* nom, double defaut = 0) {
  const auto it = b.attributs.find(nom);
  if (it == b.attributs.end() || it->second.empty()) return defaut;
  Nombres n(it->second);
  return n.lire();
}
std::vector<Pt2> points(const std::string& s) {
  std::vector<Pt2> r;
  Nombres n(s);
  while (n.nombreSuit()) {
    const double x = n.lire();
    if (!n.nombreSuit()) break;
    r.push_back({x, n.lire()});
  }
  return r;
}

}  // namespace

DessinSvg lireSvg(const std::string& texte) {
  DessinSvg d;
  Construction cons(d);
  Lecteur lecteur(texte);
  std::vector<Contexte> pile;
  int ignorer = 0;  // profondeur dans defs, symbol, clipPath, mask, pattern…
  bool racine = false;
  while (auto b = lecteur.suivante()) {
    static const std::set<std::string> conteneursIgnores = {"defs", "symbol", "clipPath", "mask", "pattern", "marker", "linearGradient", "radialGradient", "style", "metadata", "title", "desc"};
    if (b->fermante) {
      if (ignorer > 0) {
        if (conteneursIgnores.count(b->nom)) --ignorer;
        continue;
      }
      if ((b->nom == "g" || b->nom == "svg" || b->nom == "a") && !pile.empty()) pile.pop_back();
      continue;
    }
    if (ignorer > 0) {
      if (conteneursIgnores.count(b->nom) && !b->autoFermee) ++ignorer;
      continue;
    }
    if (conteneursIgnores.count(b->nom)) {
      if (!b->autoFermee) ++ignorer;
      if (b->nom != "style" && b->nom != "metadata" && b->nom != "title" && b->nom != "desc") ++d.ignores[b->nom];
      continue;
    }
    // Contexte de l'élément : celui du parent, sa transformation, son style
    Contexte k = pile.empty() ? Contexte{} : pile.back();
    if (b->nom == "svg" && !racine) {
      racine = true;
      bool uw = false, uh = false;
      const double w = longueurMm(b->attributs.count("width") ? b->attributs["width"] : std::string(), 0, &uw);
      const double h = longueurMm(b->attributs.count("height") ? b->attributs["height"] : std::string(), 0, &uh);
      std::vector<double> vb;
      if (b->attributs.count("viewBox")) {
        Nombres n(b->attributs["viewBox"]);
        while (n.nombreSuit()) vb.push_back(n.lire());
      }
      if (vb.size() == 4 && vb[2] > 0 && vb[3] > 0) {
        const double sx = w > 0 ? w / vb[2] : 25.4 / 96, sy = h > 0 ? h / vb[3] : sx;
        k.m = Affine{sx, 0, 0, sy, -vb[0] * sx, -vb[1] * sy};
        d.largeur = w > 0 ? w : vb[2] * sx;
        d.hauteur = h > 0 ? h : vb[3] * sy;
      } else {
        k.m = Affine{25.4 / 96, 0, 0, 25.4 / 96, 0, 0};
        d.largeur = w;
        d.hauteur = h;
      }
      if (!uw && !uh && w == 0 && vb.empty()) d.alertes.push_back("taille du dessin absente : pixels à 96 par pouce");
    }
    if (b->attributs.count("transform")) k.m = k.m * lireTransformation(b->attributs["transform"]);
    std::map<std::string, std::string> st;
    for (const char* cle : {"stroke", "fill", "stroke-width"})
      if (b->attributs.count(cle)) st[cle] = b->attributs[cle];
    if (b->attributs.count("style"))
      for (const auto& [x, v] : lireStyle(b->attributs["style"])) st[x] = v;
    if (st.count("stroke")) k.trait = couleur(st["stroke"]);
    if (st.count("fill")) k.fond = couleur(st["fill"]);
    if (st.count("stroke-width")) k.epaisseur = Nombres(st["stroke-width"]).lire();
    if (b->nom == "g") {
      if (b->attributs.count("inkscape:label")) k.calque = b->attributs["inkscape:label"];
      else if (b->attributs.count("id")) k.calque = b->attributs["id"];
    }
    const std::string& n = b->nom;
    if (n == "svg" || n == "g" || n == "a") {
      if (!b->autoFermee) pile.push_back(k);
      continue;
    }
    if (n == "line") cons.ligne(k, {{attr(*b, "x1"), attr(*b, "y1")}, {attr(*b, "x2"), attr(*b, "y2")}}, false);
    else if (n == "rect") {
      double rx = attr(*b, "rx", -1), ry = attr(*b, "ry", -1);
      if (rx < 0) rx = ry < 0 ? 0 : ry;
      if (ry < 0) ry = rx;
      const double w = attr(*b, "width"), h = attr(*b, "height");
      cons.rectangle(k, attr(*b, "x"), attr(*b, "y"), w, h, std::min(rx, w / 2), std::min(ry, h / 2));
    } else if (n == "circle") cons.ellipse(k, {attr(*b, "cx"), attr(*b, "cy")}, attr(*b, "r"), attr(*b, "r"));
    else if (n == "ellipse") cons.ellipse(k, {attr(*b, "cx"), attr(*b, "cy")}, attr(*b, "rx"), attr(*b, "ry"));
    else if (n == "polyline" || n == "polygon") cons.ligne(k, points(b->attributs["points"]), n == "polygon");
    else if (n == "path") cons.chemin(k, b->attributs["d"]);
    else if (n == "text" || n == "image" || n == "use" || n == "tspan" || n == "foreignObject") ++d.ignores[n];
  }
  for (const auto& [nom, nb] : d.ignores) d.alertes.push_back(std::to_string(nb) + " « " + nom + " » non repris");
  return d;
}

}  // namespace prom::svg
