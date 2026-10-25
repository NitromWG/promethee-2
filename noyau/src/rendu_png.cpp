// Prométhée : rendu de la carte en PNG, sans écran. Voir rendu_png.hpp. Licence GPL-3.0-only.
#include "promethee/rendu_png.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "promethee/archive.hpp"
#include "promethee/compression.hpp"

namespace prom::ecad {

std::uint32_t ImageRgba::pixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= largeur || y >= hauteur) return 0;
  const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(largeur) + static_cast<std::size_t>(x)) * 4;
  return (static_cast<std::uint32_t>(pixels[i]) << 24) | (static_cast<std::uint32_t>(pixels[i + 1]) << 16) | (static_cast<std::uint32_t>(pixels[i + 2]) << 8) | pixels[i + 3];
}

namespace {
void mot32(std::string& s, std::uint32_t v) {
  for (int k = 3; k >= 0; --k) s += static_cast<char>((v >> (8 * k)) & 0xFF);
}
std::uint32_t lire32(const std::string& s, std::size_t i) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(s[i])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + 1])) << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(s[i + 2])) << 8) | static_cast<unsigned char>(s[i + 3]);
}
void morceau(std::string& png, const char* type, const std::string& donnees) {
  mot32(png, static_cast<std::uint32_t>(donnees.size()));
  const std::string td = std::string(type, 4) + donnees;
  png += td;
  mot32(png, crc32(td));
}
std::uint32_t adler32(const std::string& d) {
  std::uint32_t a = 1, b = 0;
  for (const char ch : d) {
    a = (a + static_cast<unsigned char>(ch)) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}
}  // namespace

std::string encoderPng(const ImageRgba& i) {
  std::string png = "\x89PNG\r\n\x1a\n";
  std::string ihdr;
  mot32(ihdr, static_cast<std::uint32_t>(i.largeur));
  mot32(ihdr, static_cast<std::uint32_t>(i.hauteur));
  ihdr += std::string("\x08\x06\x00\x00\x00", 5);  // 8 bits, RVBA, deflate, filtres de base, sans entrelacement
  morceau(png, "IHDR", ihdr);
  std::string brut;
  brut.reserve(static_cast<std::size_t>(i.hauteur) * (static_cast<std::size_t>(i.largeur) * 4 + 1));
  for (int y = 0; y < i.hauteur; ++y) {
    brut += '\0';  // filtre « aucun »
    brut.append(reinterpret_cast<const char*>(i.pixels.data()) + static_cast<std::size_t>(y) * static_cast<std::size_t>(i.largeur) * 4, static_cast<std::size_t>(i.largeur) * 4);
  }
  std::string z = "\x78\x01";
  z += deflerBrut(brut);
  mot32(z, adler32(brut));
  morceau(png, "IDAT", z);
  morceau(png, "IEND", {});
  return png;
}

std::optional<ImageRgba> lirePng(const std::string& s) {
  if (s.size() < 33 || s.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return std::nullopt;
  ImageRgba im;
  int profondeur = 0, couleur = 0;
  std::string z;
  for (std::size_t i = 8; i + 12 <= s.size();) {
    const std::uint32_t n = lire32(s, i);
    const std::string type = s.substr(i + 4, 4);
    if (i + 12 + n > s.size()) return std::nullopt;
    const std::string d = s.substr(i + 8, n);
    if (type == "IHDR") {
      im.largeur = static_cast<int>(lire32(d, 0));
      im.hauteur = static_cast<int>(lire32(d, 4));
      profondeur = static_cast<unsigned char>(d[8]);
      couleur = static_cast<unsigned char>(d[9]);
      if (static_cast<unsigned char>(d[12]) != 0) return std::nullopt;  // entrelacé
    } else if (type == "IDAT")
      z += d;
    else if (type == "IEND")
      break;
    i += 12 + n;
  }
  if (profondeur != 8 || (couleur != 2 && couleur != 6) || im.largeur <= 0 || im.hauteur <= 0) return std::nullopt;
  const std::string brut = inflerZlib(z);
  const int bpp = couleur == 6 ? 4 : 3;
  const std::size_t ligne = static_cast<std::size_t>(im.largeur) * static_cast<std::size_t>(bpp);
  if (brut.size() < static_cast<std::size_t>(im.hauteur) * (ligne + 1)) return std::nullopt;
  std::vector<std::uint8_t> prec(ligne, 0), cour(ligne);
  im.pixels.assign(static_cast<std::size_t>(im.largeur) * static_cast<std::size_t>(im.hauteur) * 4, 255);
  for (int y = 0; y < im.hauteur; ++y) {
    const std::size_t d = static_cast<std::size_t>(y) * (ligne + 1);
    const int f = static_cast<unsigned char>(brut[d]);
    for (std::size_t x = 0; x < ligne; ++x) {
      const int a = x >= static_cast<std::size_t>(bpp) ? cour[x - static_cast<std::size_t>(bpp)] : 0, b = prec[x], c = x >= static_cast<std::size_t>(bpp) ? prec[x - static_cast<std::size_t>(bpp)] : 0;
      const int v = static_cast<unsigned char>(brut[d + 1 + x]);
      int r = v;
      if (f == 1) r = v + a;
      else if (f == 2) r = v + b;
      else if (f == 3) r = v + (a + b) / 2;
      else if (f == 4) {
        const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
        r = v + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
      }
      cour[x] = static_cast<std::uint8_t>(r & 0xFF);
    }
    for (int x = 0; x < im.largeur; ++x)
      for (int k = 0; k < bpp; ++k) im.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(im.largeur) + static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(k)] = cour[static_cast<std::size_t>(x * bpp + k)];
    std::swap(prec, cour);
  }
  return im;
}

namespace {
// Masque binaire sur-échantillonné, rempli par balayage pair-impair aux centres des échantillons
class Masque {
 public:
  Masque(int l, int h) : m_l(l), m_h(h), m_v(static_cast<std::size_t>(l) * static_cast<std::size_t>(h), 0) {}
  void remplir(const std::vector<std::pair<double, double>>& p) {
    if (p.size() < 3) return;
    double y0 = 1e300, y1 = -1e300;
    for (const auto& q : p) y0 = std::min(y0, q.second), y1 = std::max(y1, q.second);
    const int r0 = std::max(0, static_cast<int>(std::floor(y0))), r1 = std::min(m_h - 1, static_cast<int>(std::ceil(y1)));
    std::vector<double> xs;
    for (int r = r0; r <= r1; ++r) {
      const double yc = r + 0.5;
      xs.clear();
      for (std::size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        const auto& a = p[i];
        const auto& b = p[j];
        if ((a.second <= yc) != (b.second <= yc)) xs.push_back(a.first + (yc - a.second) * (b.first - a.first) / (b.second - a.second));
      }
      std::sort(xs.begin(), xs.end());
      for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
        const int c0 = std::max(0, static_cast<int>(std::ceil(xs[k] - 0.5))), c1 = std::min(m_l - 1, static_cast<int>(std::floor(xs[k + 1] - 0.5)));
        for (int c = c0; c <= c1; ++c) m_v[static_cast<std::size_t>(r) * static_cast<std::size_t>(m_l) + static_cast<std::size_t>(c)] = 1;
      }
    }
  }
  bool operator()(int x, int y) const { return m_v[static_cast<std::size_t>(y) * static_cast<std::size_t>(m_l) + static_cast<std::size_t>(x)] != 0; }

 private:
  int m_l, m_h;
  std::vector<std::uint8_t> m_v;
};
struct Couleur {
  int r, g, b;
};
Couleur couleurVernis(const std::string& v) {
  if (v == "bleu") return {16, 52, 120};
  if (v == "rouge") return {150, 20, 25};
  if (v == "noir") return {22, 22, 24};
  if (v == "blanc") return {225, 225, 220};
  if (v == "violet") return {70, 30, 110};
  return {0, 92, 46};
}
Couleur eclaircir(Couleur c, double f) { return {std::min(255, static_cast<int>(c.r + (255 - c.r) * f)), std::min(255, static_cast<int>(c.g + (255 - c.g) * f)), std::min(255, static_cast<int>(c.b + (255 - c.b) * f))}; }
}  // namespace

ImageRgba rendreCarte(const ConceptionElec& c, const OptionsRendu& o) {
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  const Boite b = m.boite;
  const double marge = 1.0;  // mm autour de la carte
  const double Lmm = mm(b.largeur()) + 2 * marge, Hmm = mm(b.hauteur()) + 2 * marge;
  double px = o.pxParMm;
  if (std::max(Lmm, Hmm) * px > o.maxPixels) px = o.maxPixels / std::max(Lmm, Hmm);
  ImageRgba im;
  im.largeur = std::max(1, static_cast<int>(std::lround(Lmm * px)));
  im.hauteur = std::max(1, static_cast<int>(std::lround(Hmm * px)));
  constexpr int S = 3;  // sur-échantillonnage
  const int L = im.largeur * S, H = im.hauteur * S;
  const double e = px * S;
  auto vers = [&](Pt q) -> std::pair<double, double> {
    const double x = mm(q.x) - mm(b.x0) + marge, y = mm(q.y) - mm(b.y0) + marge;
    return {(o.dessous ? Lmm - x : x) * e, y * e};
  };
  auto poly = [&](const Polygone& p) {
    std::vector<std::pair<double, double>> v;
    v.reserve(p.size());
    for (const Pt q : p) v.push_back(vers(q));
    return v;
  };
  auto trait = [&](Masque& M, Pt a, Pt c2, Nm largeur) {
    const Nm r = std::max<Nm>(largeur / 2, 1);
    M.remplir(poly(cercle(a, r)));
    M.remplir(poly(cercle(c2, r)));
    const double dx = static_cast<double>(c2.x - a.x), dy = static_cast<double>(c2.y - a.y), n = std::hypot(dx, dy);
    if (n < 1) return;
    const Nm nx = static_cast<Nm>(std::llround(-dy / n * static_cast<double>(r))), ny = static_cast<Nm>(std::llround(dx / n * static_cast<double>(r)));
    M.remplir(poly({{a.x + nx, a.y + ny}, {c2.x + nx, c2.y + ny}, {c2.x - nx, c2.y - ny}, {a.x - nx, a.y - ny}}));
  };
  auto primitives = [&](Masque& M, Couche couche) {
    for (const auto& cf : m.couches) {
      if (cf.couche != couche) continue;
      for (const auto& p : cf.primitives) switch (p.type) {
          case PrimitiveFab::Type::Disque: M.remplir(poly(cercle(p.a, std::max<Nm>(p.l / 2, 1)))); break;
          case PrimitiveFab::Type::Rect: M.remplir(poly(rectangle(p.a, p.l, p.h, p.rotation))); break;
          case PrimitiveFab::Type::Oblong: M.remplir(poly(rectangleArrondi(p.a, p.l, p.h, std::min(p.l, p.h) / 2, p.rotation))); break;
          case PrimitiveFab::Type::RectArrondi: M.remplir(poly(rectangleArrondi(p.a, p.l, p.h, p.r, p.rotation))); break;
          case PrimitiveFab::Type::Region: M.remplir(poly(p.region)); break;
          case PrimitiveFab::Type::Trait: trait(M, p.a, p.b, p.l); break;
          case PrimitiveFab::Type::Arc: {
            // Cercle par les trois points a, m, b ; découpé en segments épais
            const double ax = static_cast<double>(p.a.x), ay = static_cast<double>(p.a.y), mx = static_cast<double>(p.m.x), my = static_cast<double>(p.m.y), bx = static_cast<double>(p.b.x),
                         by = static_cast<double>(p.b.y);
            const double d = 2 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
            if (std::abs(d) < 1e-6) {
              trait(M, p.a, p.b, p.l);
              break;
            }
            const double ux = ((ax * ax + ay * ay) * (my - by) + (mx * mx + my * my) * (by - ay) + (bx * bx + by * by) * (ay - my)) / d;
            const double uy = ((ax * ax + ay * ay) * (bx - mx) + (mx * mx + my * my) * (ax - bx) + (bx * bx + by * by) * (mx - ax)) / d;
            const double R = std::hypot(ax - ux, ay - uy);
            double t0 = std::atan2(ay - uy, ax - ux), tm = std::atan2(my - uy, mx - ux), t1 = std::atan2(by - uy, bx - ux);
            auto norm = [](double t) { return t < 0 ? t + 2 * std::numbers::pi : t; };
            const double d1 = norm(t1 - t0), dm = norm(tm - t0);
            const double balayage = dm <= d1 ? d1 : d1 - 2 * std::numbers::pi;
            const int n = std::max(8, static_cast<int>(std::abs(balayage) * 16));
            Pt prec = p.a;
            for (int i = 1; i <= n; ++i) {
              const double t = t0 + balayage * i / n;
              const Pt q{static_cast<Nm>(std::llround(ux + R * std::cos(t))), static_cast<Nm>(std::llround(uy + R * std::sin(t)))};
              trait(M, prec, q, p.l);
              prec = q;
            }
            break;
          }
        }
    }
  };
  Masque carte(L, H), cuivre(L, H), ouvert(L, H), serig(L, H), trous(L, H);
  carte.remplir(poly(m.contour));
  primitives(cuivre, o.dessous ? Couche::CuivreDessous : Couche::CuivreDessus);
  primitives(ouvert, o.dessous ? Couche::VernisDessous : Couche::VernisDessus);
  primitives(serig, o.dessous ? Couche::SerigraphieDessous : Couche::SerigraphieDessus);
  for (const auto& t : m.trous) trous.remplir(poly(cercle(t.p, std::max<Nm>(t.diametre / 2, 1))));
  const Couleur vernis = couleurVernis(o.vernis), cuivreSous = eclaircir(vernis, 0.22), epoxy = {196, 176, 118}, blanc = {242, 242, 236};
  const Couleur finition = o.finition == "HASL" ? Couleur{198, 200, 206} : o.finition == "OSP" ? Couleur{205, 120, 70} : Couleur{214, 172, 72};
  im.pixels.assign(static_cast<std::size_t>(im.largeur) * static_cast<std::size_t>(im.hauteur) * 4, 0);
  for (int y = 0; y < im.hauteur; ++y)
    for (int x = 0; x < im.largeur; ++x) {
      int r = 0, g = 0, bl = 0, a = 0;
      for (int sy = 0; sy < S; ++sy)
        for (int sx = 0; sx < S; ++sx) {
          const int X = x * S + sx, Y = y * S + sy;
          if (!carte(X, Y) || trous(X, Y)) continue;
          Couleur col;
          if (ouvert(X, Y)) col = cuivre(X, Y) ? finition : epoxy;
          else col = serig(X, Y) ? blanc : cuivre(X, Y) ? cuivreSous : vernis;
          r += col.r, g += col.g, bl += col.b, ++a;
        }
      const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(im.largeur) + static_cast<std::size_t>(x)) * 4;
      if (a) {
        im.pixels[i] = static_cast<std::uint8_t>(r / a);
        im.pixels[i + 1] = static_cast<std::uint8_t>(g / a);
        im.pixels[i + 2] = static_cast<std::uint8_t>(bl / a);
      }
      im.pixels[i + 3] = static_cast<std::uint8_t>(a * 255 / (S * S));
    }
  return im;
}

std::string pngCarte(const ConceptionElec& c, const OptionsRendu& o) { return encoderPng(rendreCarte(c, o)); }

}  // namespace prom::ecad
