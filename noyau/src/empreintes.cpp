// Prométhée : générateur paramétrique d'empreintes. Les pastilles viennent de tables de cotes figées, relevées sur les
// empreintes courantes ; ce n'est pas le calcul de l'IPC-7351 (A17), qui arrive avec le générateur à gabarits du lot
// L4. Chaque empreinte fournit ses pastilles, sa sérigraphie (rognée à 0,15 mm du cuivre), son encombrement et son
// corps 3D. Le nom est lu en entier : une variante inconnue est refusée (B6). Origine au centre du corps.
// Licence GPL-3.0-only.
#include "promethee/circuit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

namespace prom::ecad {

namespace {
using F = FormePastille;
using TP = TypePastille;

Pastille P(const std::string& num, TP t, F f, double x, double y, double l, double h, double percage = 0) {
  Pastille p;
  p.numero = num;
  p.type = t;
  p.forme = f;
  p.position = ptMm(x, y);
  p.largeur = nm(l);
  p.hauteur = nm(h);
  p.percage = nm(percage);
  return p;
}
void ligne(DefEmpreinte& d, Couche c, double x0, double y0, double x1, double y1, double ep = 0.12) {
  d.traits.push_back({c, ptMm(x0, y0), ptMm(x1, y1), nm(ep), 0});
}
void rond(DefEmpreinte& d, Couche c, double x, double y, double r, double ep = 0.12) { d.traits.push_back({c, ptMm(x, y), ptMm(x, y), nm(ep), nm(r)}); }
void cadre(DefEmpreinte& d, Couche c, double l, double h, double ep = 0.1) {
  ligne(d, c, -l / 2, -h / 2, l / 2, -h / 2, ep);
  ligne(d, c, l / 2, -h / 2, l / 2, h / 2, ep);
  ligne(d, c, l / 2, h / 2, -l / 2, h / 2, ep);
  ligne(d, c, -l / 2, h / 2, -l / 2, -h / 2, ep);
}
void corps(DefEmpreinte& d, double l, double p, double h, const char* couleur, bool rondCorps = false) {
  d.corps = {nm(l), nm(p), nm(h), rondCorps, couleur};
  if (rondCorps) rond(d, Couche::FabricationDessus, 0, 0, l / 2, 0.1);
  else cadre(d, Couche::FabricationDessus, l, p);
}
// Parties d'un segment qui restent hors des obstacles (polygones quelconques) : paramètres [t0, t1] de [0, 1]
std::vector<std::pair<double, double>> horsObstacles(Pt a, Pt b, const Polygones& obstacles) {
  std::vector<double> t = {0.0, 1.0};
  const double ax = static_cast<double>(a.x), ay = static_cast<double>(a.y), dx = static_cast<double>(b.x - a.x), dy = static_cast<double>(b.y - a.y);
  for (const auto& p : obstacles)
    for (std::size_t i = 0; i < p.size(); ++i) {
      const Pt& u = p[i];
      const Pt& v = p[(i + 1) % p.size()];
      const double ex = static_cast<double>(v.x - u.x), ey = static_cast<double>(v.y - u.y);
      const double den = dx * ey - dy * ex;
      if (std::abs(den) < 1e-9) continue;
      const double wx = static_cast<double>(u.x) - ax, wy = static_cast<double>(u.y) - ay;
      const double s = (wx * ey - wy * ex) / den, r = (wx * dy - wy * dx) / den;
      if (s > 0 && s < 1 && r >= 0 && r <= 1) t.push_back(s);
    }
  std::sort(t.begin(), t.end());
  std::vector<std::pair<double, double>> r;
  for (std::size_t i = 0; i + 1 < t.size(); ++i) {
    if (t[i + 1] - t[i] < 1e-9) continue;
    const double m = (t[i] + t[i + 1]) / 2;
    const Pt milieu{a.x + static_cast<Nm>(std::llround(m * dx)), a.y + static_cast<Nm>(std::llround(m * dy))};
    if (std::any_of(obstacles.begin(), obstacles.end(), [&](const Polygone& p) { return dedans(p, milieu); })) continue;
    if (!r.empty() && std::abs(r.back().second - t[i]) < 1e-9) r.back().second = t[i + 1];
    else r.push_back({t[i], t[i + 1]});
  }
  return r;
}

// Sérigraphie rognée à 0,15 mm du cuivre, plus la demi-épaisseur du trait (B14) : le fabricant n'a plus à la
// couper à sa façon, et la DRC-13 reste vide sur tout le catalogue. Un cercle qui touche une pastille est découpé en
// traits ; un morceau plus court que l'épaisseur du trait disparaît.
void rognerSerigraphie(DefEmpreinte& d) {
  constexpr Nm ECART = 150'000;
  std::vector<TraitEmpreinte> garde;
  for (const auto& t : d.traits) {
    if (t.couche != Couche::SerigraphieDessus && t.couche != Couche::SerigraphieDessous) { garde.push_back(t); continue; }
    Polygones cuivre;
    for (const auto& p : d.pastilles) {
      if (p.type == TypePastille::NonMetallisee) {
        // Trou non métallisé : pas de cuivre, mais le perçage ne prend pas d'encre non plus
        cuivre.push_back(cercle(p.position, p.percage / 2 + ECART + t.epaisseur / 2));
        continue;
      }
      for (auto& x : gonfler({formePastille(p)}, ECART + t.epaisseur / 2)) cuivre.push_back(std::move(x));
    }
    std::vector<std::pair<Pt, Pt>> segments;
    if (t.rayon > 0) {
      bool touche = false;
      for (const auto& p : d.pastilles) touche = touche || distancePolygones(cercle(t.a, t.rayon + t.epaisseur / 2), formePastille(p)) < static_cast<double>(ECART);
      if (!touche) { garde.push_back(t); continue; }
      constexpr int N = 48;
      const double r = static_cast<double>(t.rayon);
      auto sur = [&](int i) {
        const double a = 2 * 3.141592653589793 * i / N;
        return t.a + Pt{static_cast<Nm>(std::llround(r * std::cos(a))), static_cast<Nm>(std::llround(r * std::sin(a)))};
      };
      for (int i = 0; i < N; ++i) segments.push_back({sur(i), sur(i + 1)});
    } else {
      segments.push_back({t.a, t.b});
    }
    for (const auto& [a, b] : segments)
      for (const auto& [t0, t1] : horsObstacles(a, b, cuivre)) {
        const Pt p0{a.x + static_cast<Nm>(std::llround(t0 * static_cast<double>(b.x - a.x))), a.y + static_cast<Nm>(std::llround(t0 * static_cast<double>(b.y - a.y)))};
        const Pt p1{a.x + static_cast<Nm>(std::llround(t1 * static_cast<double>(b.x - a.x))), a.y + static_cast<Nm>(std::llround(t1 * static_cast<double>(b.y - a.y)))};
        const bool point = a == b;  // un point de sérigraphie voulu reste, s'il est hors du cuivre
        if (!point && distance(p0, p1) < static_cast<double>(t.epaisseur)) continue;
        garde.push_back({t.couche, p0, p1, t.epaisseur, 0});
      }
  }
  d.traits = std::move(garde);
}

// Encombrement : pastilles et corps, plus 0,25 mm (arrondi au centième) ; sérigraphie rognée d'abord (B14)
void finir(DefEmpreinte& d) {
  rognerSerigraphie(d);
  Boite b;
  for (const auto& p : d.pastilles) b.etendre(boite(formePastille(p)));
  if (d.corps.largeur > 0) b.etendre(Boite{-d.corps.largeur / 2, -d.corps.profondeur / 2, d.corps.largeur / 2, d.corps.profondeur / 2});
  b = b.gonflee(250'000);
  auto arrondir = [](Nm v) { return (v >= 0 ? (v + 9'999) : (v - 9'999)) / 10'000 * 10'000; };
  d.encombrement = {arrondir(b.x0), arrondir(b.y0), arrondir(b.x1), arrondir(b.y1)};
  const Boite& e = d.encombrement;
  for (const auto& [a, z] : std::vector<std::pair<Pt, Pt>>{{{e.x0, e.y0}, {e.x1, e.y0}}, {{e.x1, e.y0}, {e.x1, e.y1}}, {{e.x1, e.y1}, {e.x0, e.y1}}, {{e.x0, e.y1}, {e.x0, e.y0}}})
    d.traits.push_back({Couche::EncombrementDessus, a, z, 50'000, 0});
}

struct Puce {
  const char* code;
  double padL, padH, entraxe, corpsL, corpsW;
};
const Puce PUCES[] = {{"0402", 0.59, 0.64, 1.02, 1.0, 0.5},   {"0603", 0.80, 0.95, 1.65, 1.6, 0.8},     {"0805", 1.025, 1.40, 1.825, 2.0, 1.25},
                      {"1206", 1.125, 1.75, 2.975, 3.2, 1.6}, {"1210", 1.125, 2.65, 2.975, 3.2, 2.5},   {"2512", 1.525, 3.35, 5.8, 6.3, 3.2}};

std::optional<DefEmpreinte> puce(const std::string& famille, const std::string& code) {
  const Puce* pc = nullptr;
  for (const auto& x : PUCES)
    if (code == x.code) pc = &x;
  if (!pc) return std::nullopt;
  DefEmpreinte d;
  d.famille = "Passifs CMS";
  const double e = pc->entraxe / 2;
  d.pastilles = {P("1", TP::CMS, F::RectArrondi, -e, 0, pc->padL, pc->padH), P("2", TP::CMS, F::RectArrondi, e, 0, pc->padL, pc->padH)};
  double h = 0.5;
  const char* couleur = "#1F2126";
  if (famille == "C") { h = std::min(pc->corpsW, 1.6); couleur = "#B99B74"; d.description = "Condensateur CMS " + code; }
  else if (famille == "L") { h = std::min(pc->corpsW, 1.6); couleur = "#4A4E57"; d.description = "Inductance CMS " + code; }
  else if (famille == "LED") { h = 0.8; couleur = "#E9EEF8"; d.description = "LED CMS " + code + " (pastille 1 : cathode)"; }
  else if (famille == "Fuse") { h = 0.6; couleur = "#E2E2E2"; d.description = "Fusible CMS " + code; }
  else { h = std::clamp(pc->corpsW * 0.55, 0.35, 0.6); d.description = "Résistance CMS " + code; }
  corps(d, pc->corpsL, pc->corpsW, h, couleur);
  // Sérigraphie : deux traits le long du corps, entre les pastilles ; repère de cathode pour les LED
  const double xs = e - pc->padL / 2 - 0.2, ys = std::max(pc->corpsW, pc->padH) / 2 + 0.15;
  if (xs > 0.15) {
    ligne(d, Couche::SerigraphieDessus, -xs, -ys, xs, -ys);
    ligne(d, Couche::SerigraphieDessus, -xs, ys, xs, ys);
  }
  if (famille == "LED") ligne(d, Couche::SerigraphieDessus, -e - pc->padL / 2 - 0.25, -pc->padH / 2, -e - pc->padL / 2 - 0.25, pc->padH / 2);
  finir(d);
  return d;
}

std::optional<DefEmpreinte> diodeCms(const std::string& boitier) {
  struct D { const char* code; double padL, padH, entraxe, l, w, h; };
  static const D TABLE[] = {{"SOD-123", 0.9, 1.2, 3.3, 2.7, 1.6, 1.1}, {"SOD-323", 0.6, 0.45, 2.3, 1.7, 1.25, 0.9}, {"SMA", 2.5, 1.7, 4.0, 4.3, 2.6, 2.3}, {"SMB", 2.5, 2.3, 4.3, 4.3, 3.6, 2.3}};
  for (const auto& t : TABLE) {
    if (boitier != t.code) continue;
    DefEmpreinte d;
    d.famille = "Diodes";
    d.description = std::string("Diode ") + t.code + " (pastille 1 : cathode)";
    d.pastilles = {P("1", TP::CMS, F::RectArrondi, -t.entraxe / 2, 0, t.padL, t.padH), P("2", TP::CMS, F::RectArrondi, t.entraxe / 2, 0, t.padL, t.padH)};
    corps(d, t.l, t.w, t.h, "#1F2126");
    const double ys = std::max(t.w, t.padH) / 2 + 0.15, xs = t.entraxe / 2 + t.padL / 2 + 0.2;
    ligne(d, Couche::SerigraphieDessus, -xs, -ys, t.l / 2, -ys);
    ligne(d, Couche::SerigraphieDessus, -xs, ys, t.l / 2, ys);
    ligne(d, Couche::SerigraphieDessus, -xs, -ys, -xs, ys);
    finir(d);
    return d;
  }
  return std::nullopt;
}

// Circuit à deux rangées de broches (pas p) : à gauche de haut en bas, à droite de bas en haut.
void deuxRangees(DefEmpreinte& d, int n, double pas, double x, double padL, double padH, TP type = TP::CMS, double percage = 0) {
  const int k = n / 2;
  for (int i = 0; i < n; ++i) {
    const bool gauche = i < k;
    const int rang = gauche ? i : (n - 1 - i);
    const double y = (rang - (k - 1) / 2.0) * pas;
    const F forme = type == TP::CMS ? F::RectArrondi : (i == 0 ? F::Rect : F::Ovale);
    d.pastilles.push_back(P(std::to_string(i + 1), type, forme, gauche ? -x : x, y, padL, padH, percage));
  }
}
void repereBroche1(DefEmpreinte& d) {
  if (d.pastilles.empty()) return;
  const Pastille& p = d.pastilles.front();
  rond(d, Couche::SerigraphieDessus, mm(p.position.x) - mm(p.largeur) / 2 - 0.45, mm(p.position.y), 0.12, 0.24);
}

std::optional<DefEmpreinte> soic(int n, bool tssop) {
  if (n < 4 || n % 2) return std::nullopt;
  DefEmpreinte d;
  d.famille = "Circuits intégrés";
  const int k = n / 2;
  if (tssop) {
    d.description = "TSSOP-" + std::to_string(n) + " (4,4 mm, pas 0,65)";
    deuxRangees(d, n, 0.65, 2.8625, 1.475, 0.4);
    const double l = k * 0.65 + 0.15;
    corps(d, 4.4, l, 1.2, "#1F2126");
    ligne(d, Couche::SerigraphieDessus, -2.2, -l / 2 - 0.12, 2.2, -l / 2 - 0.12);
    ligne(d, Couche::SerigraphieDessus, -2.2, l / 2 + 0.12, 2.2, l / 2 + 0.12);
  } else {
    d.description = "SOIC-" + std::to_string(n) + " (3,9 mm, pas 1,27)";
    deuxRangees(d, n, 1.27, 2.475, 1.975, 0.6);
    const double l = (k - 1) * 1.27 + 1.05;
    corps(d, 3.9, l, 1.75, "#1F2126");
    ligne(d, Couche::SerigraphieDessus, -1.95, -l / 2 - 0.12, 1.95, -l / 2 - 0.12);
    ligne(d, Couche::SerigraphieDessus, -1.95, l / 2 + 0.12, 1.95, l / 2 + 0.12);
  }
  repereBroche1(d);
  finir(d);
  return d;
}

// Boîtiers à quatre côtés (QFN, TQFP) : broches 1 en haut à gauche, sens antihoraire. Bornes (B20) : 8 à 400
// broches par multiples de 4, pas de 0,3 à 10 mm, corps de 150 mm au plus ; une rangée qui ne tient pas sur son côté
// (QFN-64 sur un corps de 3 mm) ou une pastille centrale de taille nulle est refusée.
std::optional<DefEmpreinte> quatreCotes(int n, double corpsCote, double pas, bool qfn) {
  if (n < 8 || n > 400 || n % 4 || pas < 0.3 || pas > 10 || corpsCote <= 0 || corpsCote > 150) return std::nullopt;
  DefEmpreinte d;
  d.famille = "Circuits intégrés";
  const int k = n / 4;
  const double longueur = qfn ? 0.875 : 1.5, largeur = qfn ? pas * 0.5 : pas * 0.55, c = qfn ? corpsCote / 2 - 0.0625 : corpsCote / 2 + 0.75;
  // La rangée d'un côté ne doit pas atteindre les pastilles des côtés voisins, à 0,05 mm près
  if ((k - 1) * pas / 2 + largeur / 2 + 0.05 > c - longueur / 2) return std::nullopt;
  if (qfn && (c - longueur / 2 - 0.25) * 2 < 0.2) return std::nullopt;  // pastille centrale d'au moins 0,2 mm
  for (int i = 0; i < n; ++i) {
    const int cote = i / k, j = i % k;
    const double t = (j - (k - 1) / 2.0) * pas;
    double x = 0, y = 0, l = longueur, h = largeur;
    switch (cote) {
      case 0: x = -c; y = t; break;                       // gauche, de haut en bas
      case 1: x = t; y = c; std::swap(l, h); break;       // bas, de gauche à droite
      case 2: x = c; y = -t; break;                       // droite, de bas en haut
      default: x = -t; y = -c; std::swap(l, h); break;    // haut, de droite à gauche
    }
    d.pastilles.push_back(P(std::to_string(i + 1), TP::CMS, F::RectArrondi, x, y, l, h));
  }
  char t[96];
  if (qfn) {
    // Pastille centrale : 62 % du corps, mais jamais à moins de 0,25 mm des broches (EL-05) ; le bord intérieur
    // des broches est à c − longueur/2 du centre
    const double interieur = c - longueur / 2;
    const double ep = std::min(std::round(corpsCote * 0.62 / 0.05) * 0.05, std::floor((interieur - 0.25) * 2 / 0.05 + 1e-9) * 0.05);
    d.pastilles.push_back(P(std::to_string(n + 1), TP::CMS, F::RectArrondi, 0, 0, ep, ep));
    d.pastilles.back().arrondi = 0.05;
    std::snprintf(t, sizeof t, "QFN-%d %gx%g mm, pas %g, pastille centrale", n, corpsCote, corpsCote, pas);
  } else {
    std::snprintf(t, sizeof t, "TQFP-%d %gx%g mm, pas %g", n, corpsCote, corpsCote, pas);
  }
  d.description = t;
  corps(d, corpsCote, corpsCote, qfn ? 0.9 : 1.2, "#1F2126");
  const double a = corpsCote / 2 + 0.12, m = 0.6;
  for (int sx : {-1, 1})
    for (int sy : {-1, 1}) {
      ligne(d, Couche::SerigraphieDessus, sx * a, sy * a, sx * (a - m), sy * a);
      ligne(d, Couche::SerigraphieDessus, sx * a, sy * a, sx * a, sy * (a - m));
    }
  repereBroche1(d);
  finir(d);
  return d;
}

// Boîtier DIL traversant : rangées à 7,62 mm (300 mil, « DIP-8 »), 10,16 mm ou 15,24 mm (600 mil,
// « DIP-40_W15.24mm ») ; toute autre largeur est refusée (B6)
std::optional<DefEmpreinte> dip(int n, double ecart) {
  if (n < 4 || n % 2 || n > 64) return std::nullopt;
  double largeurCorps = 0;
  if (std::abs(ecart - 7.62) < 1e-9) largeurCorps = 6.35;
  else if (std::abs(ecart - 10.16) < 1e-9) largeurCorps = 8.89;
  else if (std::abs(ecart - 15.24) < 1e-9) largeurCorps = 13.97;
  else return std::nullopt;
  DefEmpreinte d;
  d.famille = "Traversants";
  char t[64];
  std::snprintf(t, sizeof t, "DIP-%d (%.2f mm, pas 2,54)", n, ecart);
  d.description = t;
  for (char& ch : d.description)
    if (ch == '.') ch = ',';
  deuxRangees(d, n, 2.54, ecart / 2, 1.6, 1.6, TP::Traversante, 0.8);
  const double l = n / 2 * 2.54 + 0.2;
  corps(d, largeurCorps, l, 4.0, "#1F2126");
  // Sérigraphie entre les rangées, à l'intérieur du corps, comme les empreintes de KiCad : jamais sur les pastilles
  cadre(d, Couche::SerigraphieDessus, ecart - 2.32, l + 0.2, 0.12);
  repereBroche1(d);
  finir(d);
  return d;
}

std::optional<DefEmpreinte> barrette(int lignes, int n, double pas, bool bornier) {
  if (n < 1 || n > 40 || lignes < 1 || lignes > 2) return std::nullopt;
  DefEmpreinte d;
  d.famille = "Connecteurs";
  char t[96];
  if (bornier) std::snprintf(t, sizeof t, "Bornier à vis 1x%02d au pas de %.2f mm", n, pas);
  else std::snprintf(t, sizeof t, "Barrette %dx%02d au pas de %.2f mm", lignes, n, pas);
  d.description = t;
  const double pastille = bornier ? 2.6 : 1.7, trou = bornier ? 1.3 : 1.0;
  int numero = 1;
  for (int i = 0; i < n; ++i)
    for (int c = 0; c < lignes; ++c) {
      const double x = lignes == 2 ? (c == 0 ? -pas / 2 : pas / 2) : 0, y = (i - (n - 1) / 2.0) * pas;
      d.pastilles.push_back(P(std::to_string(numero), TP::Traversante, numero == 1 ? F::Rect : F::Ovale, x, y, pastille, pastille, trou));
      ++numero;
    }
  const double l = bornier ? 8.0 : lignes * pas, h = n * pas;
  corps(d, l, h, bornier ? 10.0 : 8.5, bornier ? "#2E6F9E" : "#1F2126");
  cadre(d, Couche::SerigraphieDessus, l + 0.2, h + 0.2, 0.12);
  finir(d);
  return d;
}

std::optional<DefEmpreinte> traversant2(const std::string& id) {
  DefEmpreinte d;
  d.famille = "Traversants";
  auto deux = [&](double entraxe, double pastille, double trou, bool premierCarre) {
    d.pastilles = {P("1", TP::Traversante, premierCarre ? F::Rect : F::Rond, -entraxe / 2, 0, pastille, pastille, trou),
                   P("2", TP::Traversante, F::Rond, entraxe / 2, 0, pastille, pastille, trou)};
  };
  if (id == "R_Axial_P10.16") { deux(10.16, 1.6, 0.8, false); corps(d, 6.3, 2.5, 2.5, "#D8C49A"); d.description = "Résistance traversante 1/4 W, entraxe 10,16 mm"; }
  else if (id == "D_DO-41") { deux(10.16, 2.2, 1.1, true); corps(d, 5.2, 2.7, 2.7, "#1F2126"); d.description = "Diode DO-41 (1N4007), pastille 1 : cathode"; }
  else if (id == "LED_D3.0") { deux(2.54, 1.8, 0.9, true); corps(d, 3.0, 3.0, 5.3, "#E9EEF8", true); d.description = "LED 3 mm (pastille 1 : cathode)"; }
  else if (id == "LED_D5.0") { deux(2.54, 1.8, 0.9, true); corps(d, 5.0, 5.0, 8.6, "#E9EEF8", true); d.description = "LED 5 mm (pastille 1 : cathode)"; }
  else if (id == "Crystal_HC49-4H") { deux(4.88, 1.5, 0.8, false); corps(d, 11.0, 4.65, 3.6, "#C8CCD4"); d.description = "Quartz HC49 bas profil"; }
  else if (id == "Buzzer_D12_P7.6") {
    deux(7.6, 2.0, 1.0, true);
    corps(d, 12.0, 12.0, 9.5, "#1F2126", true);
    rond(d, Couche::SerigraphieDessus, 0, 0, 6.12);
    d.description = "Buzzer Ø 12 mm, entraxe 7,6 mm (pastille 1 : +)";
  } else if (id == "Trimmer_3296W" || id == "SW_Slide_SPDT_P2.54") {
    // Trois broches en ligne au pas de 2,54 mm
    const bool ajustable = id == "Trimmer_3296W";
    d.famille = ajustable ? "Traversants" : "Électromécanique";
    for (int i = 0; i < 3; ++i) d.pastilles.push_back(P(std::to_string(i + 1), TP::Traversante, i == 0 ? F::Rect : F::Rond, (i - 1) * 2.54, 0, 1.7, 1.7, ajustable ? 0.8 : 0.9));
    if (ajustable) {
      corps(d, 9.53, 4.83, 10.03, "#2E4F9E");
      d.description = "Potentiomètre ajustable multitour 3296W, 3 broches au pas de 2,54 mm";
    } else {
      corps(d, 8.6, 3.6, 3.5, "#1F2126");
      d.description = "Interrupteur à glissière inverseur, 3 broches au pas de 2,54 mm (2 : commun)";
    }
  }
  else if (id.rfind("CP_Radial_D", 0) == 0) {
    double dia = 0, pas = 0;
    int fin = -1;
    // Nom lu en entier, bornes physiques : les deux pattes sous le corps (B6, B20)
    if (std::sscanf(id.c_str(), "CP_Radial_D%lf_P%lf%n", &dia, &pas, &fin) != 2 || fin != static_cast<int>(id.size()) || dia < 3 || dia > 25 || pas < 1 || pas > 12.5 || pas >= dia)
      return std::nullopt;
    deux(pas, dia >= 8 ? 2.0 : 1.6, dia >= 8 ? 1.0 : 0.8, true);
    corps(d, dia, dia, dia <= 6.3 ? 11.0 : dia + 4.0, "#26305A", true);
    rond(d, Couche::SerigraphieDessus, 0, 0, dia / 2 + 0.12);
    // Polarité visible (EL-38) : « + » sérigraphié hors du corps, côté pastille 1 ; côté négatif hachuré sur le
    // corps (couche de fabrication) et repéré par un « − » hors du corps, comme la bande des condensateurs réels
    {
      const double r = dia / 2 + 0.12, b = 0.5, x = -r - 0.9, xm = r + 0.9, y = -r * 0.55;
      ligne(d, Couche::SerigraphieDessus, x - b, y, x + b, y);
      ligne(d, Couche::SerigraphieDessus, x, y - b, x, y + b);
      ligne(d, Couche::SerigraphieDessus, xm - b, y, xm + b, y);
      const double corde = dia / 2 * 0.55;
      for (double xh = corde; xh < dia / 2 - 0.05; xh += 0.4) {
        const double h = std::sqrt(std::max(0.0, dia * dia / 4 - xh * xh));
        ligne(d, Couche::FabricationDessus, xh, -h, xh, h, 0.1);
      }
    }
    char t[192];
    std::snprintf(t, sizeof t, "Condensateur électrolytique radial Ø %g mm, entraxe %g mm (pastille 1 : +, côté hachuré : −)", dia, pas);
    d.description = t;
  } else return std::nullopt;
  if (!d.corps.rond) cadre(d, Couche::SerigraphieDessus, mm(d.corps.largeur) + 0.2, mm(d.corps.profondeur) + 0.2, 0.12);
  finir(d);
  return d;
}

std::optional<DefEmpreinte> generer(const std::string& id) {
  std::optional<DefEmpreinte> r;
  for (const char* f : {"R", "C", "L", "LED", "Fuse"}) {
    const std::string prefixe = std::string(f) + "_";
    // Puce CMS : exactement quatre chiffres (0805) ; « LED_D5.0 » n'en est pas une (EL-11)
    const std::string taille = id.size() == prefixe.size() + 4 ? id.substr(prefixe.size()) : std::string();
    if (id.rfind(prefixe, 0) == 0 && !taille.empty() && std::all_of(taille.begin(), taille.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }))
      return puce(f, taille);
  }
  if (id.rfind("D_SOD-", 0) == 0 || id == "D_SMA" || id == "D_SMB") return diodeCms(id.substr(2));
  int n = 0, a = 0;
  double b = 0, c = 0, p = 0;
  if (id == "SOT-23" || id == "SOT-23-5") {
    DefEmpreinte d;
    d.famille = "Transistors et régulateurs";
    if (id == "SOT-23") {
      d.description = "SOT-23 (3 broches)";
      d.pastilles = {P("1", TP::CMS, F::RectArrondi, -1.1375, -0.95, 1.325, 0.6), P("2", TP::CMS, F::RectArrondi, -1.1375, 0.95, 1.325, 0.6), P("3", TP::CMS, F::RectArrondi, 1.1375, 0, 1.325, 0.6)};
      corps(d, 1.3, 2.9, 1.1, "#1F2126");
    } else {
      d.description = "SOT-23-5";
      d.pastilles = {P("1", TP::CMS, F::RectArrondi, -1.1375, -0.95, 1.06, 0.65), P("2", TP::CMS, F::RectArrondi, -1.1375, 0, 1.06, 0.65), P("3", TP::CMS, F::RectArrondi, -1.1375, 0.95, 1.06, 0.65),
                     P("4", TP::CMS, F::RectArrondi, 1.1375, 0.95, 1.06, 0.65), P("5", TP::CMS, F::RectArrondi, 1.1375, -0.95, 1.06, 0.65)};
      corps(d, 1.6, 2.9, 1.1, "#1F2126");
    }
    ligne(d, Couche::SerigraphieDessus, -0.3, -1.6, 0.3, -1.6);
    ligne(d, Couche::SerigraphieDessus, -0.3, 1.6, 0.3, 1.6);
    repereBroche1(d);
    finir(d);
    return d;
  }
  if (id == "SOT-223") {
    DefEmpreinte d;
    d.famille = "Transistors et régulateurs";
    d.description = "SOT-223 (languette reliée à la broche 2)";
    d.pastilles = {P("1", TP::CMS, F::RectArrondi, -2.3, 3.15, 1.0, 1.8), P("2", TP::CMS, F::RectArrondi, 0, 3.15, 1.0, 1.8), P("3", TP::CMS, F::RectArrondi, 2.3, 3.15, 1.0, 1.8),
                   P("2", TP::CMS, F::RectArrondi, 0, -3.15, 3.3, 1.8)};
    corps(d, 6.5, 3.5, 1.8, "#1F2126");
    ligne(d, Couche::SerigraphieDessus, -3.4, -1.9, -3.4, 1.9);
    ligne(d, Couche::SerigraphieDessus, 3.4, -1.9, 3.4, 1.9);
    finir(d);
    return d;
  }
  // Lecture stricte (B6) : le motif doit couvrir le nom entier (« %n » donne où la lecture s'est arrêtée) ; un pas,
  // un écart ou une variante que le générateur ne sait pas faire est refusé, jamais remplacé par autre chose.
  int fin = -1;
  const int taille = static_cast<int>(id.size());
  auto entier = [&](const char* motif, int* v) { fin = -1; return std::sscanf(id.c_str(), motif, v, &fin) == 1 && fin == taille; };
  if (entier("SOIC-%d%n", &n) && id == "SOIC-" + std::to_string(n)) return n >= 4 && n <= 64 ? soic(n, false) : std::nullopt;
  if (entier("TSSOP-%d%n", &n) && id == "TSSOP-" + std::to_string(n)) return n >= 6 && n <= 64 ? soic(n, true) : std::nullopt;
  fin = -1;
  if (std::sscanf(id.c_str(), "QFN-%d-%lfx%lf-%lf%n", &n, &b, &c, &p, &fin) == 4 && fin == taille) return b == c ? quatreCotes(n, b, p, true) : std::nullopt;
  fin = -1;
  if (std::sscanf(id.c_str(), "TQFP-%d-%lfx%lf-%lf%n", &n, &b, &c, &p, &fin) == 4 && fin == taille) return b == c ? quatreCotes(n, b, p, false) : std::nullopt;
  if (entier("DIP-%d%n", &n) && id == "DIP-" + std::to_string(n)) return dip(n, 7.62);
  fin = -1;
  if (std::sscanf(id.c_str(), "DIP-%d_W%lfmm%n", &n, &b, &fin) == 2 && fin == taille) return dip(n, b);
  fin = -1;
  if (std::sscanf(id.c_str(), "PinHeader_%dx%d_P2.54%n", &a, &n, &fin) == 2 && fin == taille) return barrette(a, n, 2.54, false);
  if (entier("TerminalBlock_1x%d_P5.08%n", &n)) return barrette(1, n, 5.08, true);
  if (id == "TO-220-3") {
    DefEmpreinte d;
    d.famille = "Transistors et régulateurs";
    d.description = "TO-220 vertical, 3 broches au pas de 2,54 mm";
    d.pastilles = {P("1", TP::Traversante, F::Rect, -2.54, 0, 1.9, 2.4, 1.1), P("2", TP::Traversante, F::Ovale, 0, 0, 1.9, 2.4, 1.1), P("3", TP::Traversante, F::Ovale, 2.54, 0, 1.9, 2.4, 1.1)};
    corps(d, 10.0, 4.4, 16.0, "#1F2126");
    cadre(d, Couche::SerigraphieDessus, 10.2, 4.6, 0.12);
    finir(d);
    return d;
  }
  if (id == "SW_PUSH_6mm") {
    DefEmpreinte d;
    d.famille = "Électromécanique";
    d.description = "Bouton poussoir tactile 6 × 6 mm, traversant";
    d.pastilles = {P("1", TP::Traversante, F::Rond, -3.25, -2.25, 2.0, 2.0, 1.1), P("1", TP::Traversante, F::Rond, 3.25, -2.25, 2.0, 2.0, 1.1),
                   P("2", TP::Traversante, F::Rond, -3.25, 2.25, 2.0, 2.0, 1.1), P("2", TP::Traversante, F::Rond, 3.25, 2.25, 2.0, 2.0, 1.1)};
    corps(d, 6.0, 6.0, 5.0, "#2A2D34");
    cadre(d, Couche::SerigraphieDessus, 6.2, 6.2, 0.12);
    rond(d, Couche::SerigraphieDessus, 0, 0, 1.75);
    finir(d);
    return d;
  }
  if (id == "TestPoint_Pad_1.5") {
    DefEmpreinte d;
    d.famille = "Mécanique";
    d.description = "Point de test : pastille ronde de 1,5 mm";
    d.pastilles = {P("1", TP::CMS, F::Rond, 0, 0, 1.5, 1.5)};
    rond(d, Couche::SerigraphieDessus, 0, 0, 1.0);
    finir(d);
    return d;
  }
  if (id == "NetTie-2_SMD") {
    // Liaison de réseaux (2.11) : le seul endroit où deux réseaux se touchent, voulu (masse en étoile)
    DefEmpreinte d;
    d.famille = "Mécanique";
    d.description = "Liaison de réseaux : deux pastilles CMS de 0,6 mm, le seul point où les deux réseaux se rejoignent";
    d.pastilles = {P("1", TP::CMS, F::Rect, -0.5, 0, 0.6, 0.6), P("2", TP::CMS, F::Rect, 0.5, 0, 0.6, 0.6)};
    finir(d);
    return d;
  }
  double dia = 0;
  fin = -1;
  // « MountingHole_3.2 » (non métallisé) ou « MountingHole_3.2_Pad » (métallisé) ; toute autre variante
  // (« _M3_Pad_Via », « mm ») est refusée au lieu de donner un trou non métallisé (B6)
  if (std::sscanf(id.c_str(), "MountingHole_%lf%n", &dia, &fin) == 1 && fin > 0 && (fin == taille || id.compare(static_cast<std::size_t>(fin), std::string::npos, "_Pad") == 0)) {
    if (dia < 0.5 || dia > 10) return std::nullopt;
    const bool pastille = fin != taille;
    DefEmpreinte d;
    d.famille = "Mécanique";
    char t[96];
    std::snprintf(t, sizeof t, pastille ? "Trou de fixation Ø %g mm métallisé, pastille Ø %g mm" : "Trou de fixation Ø %g mm non métallisé", dia, dia * 2);
    d.description = t;
    if (pastille) d.pastilles = {P("1", TP::Traversante, F::Rond, 0, 0, dia * 2, dia * 2, dia)};
    else d.pastilles = {P("", TP::NonMetallisee, F::Rond, 0, 0, dia, dia, dia)};
    rond(d, Couche::SerigraphieDessus, 0, 0, dia + 0.25);
    finir(d);
    return d;
  }
  return traversant2(id);
}
}  // namespace

std::optional<DefEmpreinte> genererEmpreinte(const std::string& id) {
  std::optional<DefEmpreinte> d = generer(id);
  if (d) d->id = id;
  return d;
}

const std::vector<InfoEmpreinte>& catalogueEmpreintes() {
  static const std::vector<InfoEmpreinte> c = [] {
    std::vector<std::string> ids;
    for (const char* f : {"R", "C", "L", "LED", "Fuse"})
      for (const auto& p : PUCES) ids.push_back(std::string(f) + "_" + p.code);
    for (const char* x : {"D_SOD-123", "D_SOD-323", "D_SMA", "D_SMB", "SOT-23", "SOT-23-5", "SOT-223", "TO-220-3"}) ids.push_back(x);
    for (int n : {8, 14, 16}) ids.push_back("SOIC-" + std::to_string(n));
    for (int n : {8, 14, 16, 20, 24, 28}) ids.push_back("TSSOP-" + std::to_string(n));
    for (const char* x : {"QFN-16-3x3-0.5", "QFN-20-4x4-0.5", "QFN-24-4x4-0.5", "QFN-32-5x5-0.5", "QFN-48-7x7-0.5", "TQFP-32-7x7-0.8", "TQFP-44-10x10-0.8", "TQFP-48-7x7-0.5", "TQFP-64-10x10-0.5"}) ids.push_back(x);
    for (int n : {4, 6, 8, 14, 16, 20, 28}) ids.push_back("DIP-" + std::to_string(n));
    for (const char* x : {"R_Axial_P10.16", "D_DO-41", "LED_D3.0", "LED_D5.0", "CP_Radial_D5.0_P2.00", "CP_Radial_D6.3_P2.50", "CP_Radial_D8.0_P3.50", "CP_Radial_D10.0_P5.00", "Crystal_HC49-4H", "Buzzer_D12_P7.6", "Trimmer_3296W", "SW_Slide_SPDT_P2.54"}) ids.push_back(x);
    char t[48];
    for (int n : {1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20}) { std::snprintf(t, sizeof t, "PinHeader_1x%02d_P2.54", n); ids.push_back(t); }
    for (int n : {3, 4, 5, 8, 10, 20}) { std::snprintf(t, sizeof t, "PinHeader_2x%02d_P2.54", n); ids.push_back(t); }
    for (int n : {2, 3, 4}) { std::snprintf(t, sizeof t, "TerminalBlock_1x%02d_P5.08", n); ids.push_back(t); }
    for (const char* x : {"SW_PUSH_6mm", "TestPoint_Pad_1.5", "NetTie-2_SMD", "MountingHole_2.2", "MountingHole_2.7", "MountingHole_3.2", "MountingHole_4.3", "MountingHole_3.2_Pad"}) ids.push_back(x);
    std::vector<InfoEmpreinte> v;
    for (const auto& id : ids)
      if (const auto d = genererEmpreinte(id)) {
        std::vector<std::string> numeros;
        for (const auto& p : d->pastilles)
          if (!p.numero.empty()) numeros.push_back(p.numero);
        std::sort(numeros.begin(), numeros.end());
        numeros.erase(std::unique(numeros.begin(), numeros.end()), numeros.end());
        v.push_back({id, d->famille, d->description, static_cast<int>(numeros.size())});
      }
    return v;
  }();
  return c;
}

std::string identifiantEmpreintePerso(const std::string& nom) {
  std::string r = "perso:";
  for (char c : nom) r += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || (static_cast<unsigned char>(c) & 0x80)) ? c : '_';
  return r;
}

std::string verifierSpecEmpreinte(const SpecEmpreinte& s, Nm couronneMin) {
  auto mmTexte = [](double v) {
    char t[32];
    std::snprintf(t, sizeof t, "%.2f mm", v);
    std::string r = t;
    std::replace(r.begin(), r.end(), '.', ',');
    return r;
  };
  if (s.rangees < 1 || s.rangees > 64 || s.colonnes < 1 || s.colonnes > 256) return "De 1 à 64 rangées et de 1 à 256 colonnes.";
  if (s.rangees * s.colonnes > 400) return "Au plus 400 pastilles.";
  if (s.largeur < 0.1 || s.largeur > 50 || (s.forme != FormePastille::Rond && (s.hauteur < 0.1 || s.hauteur > 50))) return "Pastille de 0,1 à 50 mm de côté.";
  if ((s.colonnes > 1 && (s.pasX < 0.3 || s.pasX > 100)) || (s.rangees > 1 && (s.pasY < 0.3 || s.pasY > 100))) return "Pas de 0,3 à 100 mm.";
  if (s.type == TypePastille::CMS) return {};
  // Trou : la borne est la largeur pour une pastille ronde, le plus petit côté sinon (B15)
  const double cote = s.forme == FormePastille::Rond ? s.largeur : std::min(s.largeur, s.hauteur);
  if (s.percage < 0.1) return "Perçage d’au moins 0,1 mm.";
  if (s.type == TypePastille::Traversante) {
    // La borne du générateur : au moins 0,05 mm de cuivre de chaque côté du trou
    if (s.percage > cote - 0.1 + 1e-9)
      return "Perçage de " + mmTexte(s.percage) + " pour une pastille de " + mmTexte(cote) + " : le trou doit être plus petit que la pastille, d’au moins 0,10 mm.";
    const double couronne = (cote - s.percage) / 2;
    if (couronneMin > 0 && couronne * 1e6 < static_cast<double>(couronneMin))
      return "Couronne de " + mmTexte(couronne) + " sous le minimum de " + mmTexte(static_cast<double>(couronneMin) * 1e-6) + " des règles : agrandis la pastille ou réduis le perçage.";
  }
  return {};
}

DefEmpreinte genererEmpreinteSpec(const SpecEmpreinte& s) {
  DefEmpreinte d;
  d.id = identifiantEmpreintePerso(s.nom.empty() ? std::string("Empreinte") : s.nom);
  d.famille = "Mes empreintes";
  const int R = std::clamp(s.rangees, 1, 64), C = std::clamp(s.colonnes, 1, 256);
  d.description = s.description.empty() ? std::to_string(R) + " × " + std::to_string(C) + " pastilles au pas de " + std::to_string(s.pasX).substr(0, 4) + " mm" : s.description;
  const bool cms = s.type == TypePastille::CMS;
  for (int r = 0; r < R; ++r)
    for (int c = 0; c < C; ++c) {
      int n = r * C + c + 1;
      if (s.numerotation == SpecEmpreinte::Numerotation::ParColonne) n = c * R + r + 1;
      else if (s.numerotation == SpecEmpreinte::Numerotation::Tour && R == 2) n = r == 1 ? c + 1 : C + (C - c);
      Pastille p;
      p.numero = std::to_string(n);
      p.type = s.type;
      p.forme = s.forme;
      if (n == 1 && s.premiereCarree && !cms) p.forme = FormePastille::Rect;
      p.position = ptMm((c - (C - 1) / 2.0) * s.pasX, (r - (R - 1) / 2.0) * s.pasY);
      p.largeur = nm(std::max(0.05, s.largeur));
      p.hauteur = nm(std::max(0.05, s.forme == FormePastille::Rond ? s.largeur : s.hauteur));
      // Perçage borné par la largeur d'une pastille ronde, par son plus petit côté sinon (B15), sans jamais appeler
      // std::clamp avec une borne haute sous la borne basse ; verifierSpecEmpreinte refuse avant d'en arriver là
      const double cote = s.forme == FormePastille::Rond ? s.largeur : std::min(s.largeur, s.hauteur);
      p.percage = cms ? 0 : nm(std::max(0.1, std::min(s.percage, cote - 0.1)));
      if (s.type == TypePastille::NonMetallisee) p.percage = nm(std::max(0.1, s.percage));
      d.pastilles.push_back(p);
    }
  std::sort(d.pastilles.begin(), d.pastilles.end(), [](const Pastille& a, const Pastille& b) { return std::stoi(a.numero) < std::stoi(b.numero); });
  // Corps et sérigraphie : le rectangle des pastilles, écarté de 0,3 mm ; repère de la broche 1 à l'extérieur
  Boite b;
  for (const auto& p : d.pastilles) b.etendre(boite(formePastille(p)));
  const double l = mm(b.largeur()) + 0.6, h = mm(b.hauteur()) + 0.6;
  corps(d, l, h, std::max(0.1, s.hauteurCorps), cms ? "#2A2D34" : "#1C1E24");
  cadre(d, Couche::SerigraphieDessus, l + 0.24, h + 0.24, 0.12);
  const Pt p1 = d.pastilles.front().position;
  const double x1 = mm(p1.x), y1 = mm(p1.y);
  if (R == 1) ligne(d, Couche::SerigraphieDessus, x1 - s.pasX / 2, -h / 2 - 0.5, x1 - s.pasX / 2, h / 2 + 0.5, 0.12);  // trait avant la broche 1
  else rond(d, Couche::SerigraphieDessus, -l / 2 - 0.6, y1, 0.2, 0.12);                                            // point devant la broche 1
  finir(d);
  return d;
}

}  // namespace prom::ecad
