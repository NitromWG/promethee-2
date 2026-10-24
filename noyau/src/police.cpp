// Prométhée : police vectorielle à trait simple. Licence GPL-3.0-only.
#include "promethee/police.hpp"

#include <cmath>
#include <map>
#include <numbers>
#include <sstream>

namespace prom::ecad {

namespace {
// Glyphes : polylignes séparées par « ; », points « x,y » sur une grille de 4 de large et 6 de haut (y vers le haut)
const std::map<char32_t, const char*>& glyphes() {
  static const std::map<char32_t, const char*> g = {
      {U'A', "0,0 0,4 2,6 4,4 4,0;0,3 4,3"},
      {U'B', "0,0 0,6 3,6 4,5 4,4 3,3 0,3;3,3 4,2 4,1 3,0 0,0"},
      {U'C', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1"},
      {U'D', "0,0 0,6 3,6 4,5 4,1 3,0 0,0"},
      {U'E', "4,6 0,6 0,0 4,0;0,3 3,3"},
      {U'F', "4,6 0,6 0,0;0,3 3,3"},
      {U'G', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1 4,3 2,3"},
      {U'H', "0,0 0,6;4,0 4,6;0,3 4,3"},
      {U'I', "1,6 3,6;2,6 2,0;1,0 3,0"},
      {U'J', "4,6 4,1 3,0 1,0 0,1"},
      {U'K', "0,0 0,6;4,6 0,2;1,3 4,0"},
      {U'L', "0,6 0,0 4,0"},
      {U'M', "0,0 0,6 2,3 4,6 4,0"},
      {U'N', "0,0 0,6 4,0 4,6"},
      {U'O', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0"},
      {U'P', "0,0 0,6 3,6 4,5 4,4 3,3 0,3"},
      {U'Q', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0;2,2 4,0"},
      {U'R', "0,0 0,6 3,6 4,5 4,4 3,3 0,3;2,3 4,0"},
      {U'S', "4,5 3,6 1,6 0,5 0,4 1,3 3,3 4,2 4,1 3,0 1,0 0,1"},
      {U'T', "0,6 4,6;2,6 2,0"},
      {U'U', "0,6 0,1 1,0 3,0 4,1 4,6"},
      {U'V', "0,6 2,0 4,6"},
      {U'W', "0,6 1,0 2,3 3,0 4,6"},
      {U'X', "0,0 4,6;0,6 4,0"},
      {U'Y', "0,6 2,3 4,6;2,3 2,0"},
      {U'Z', "0,6 4,6 0,0 4,0"},
      {U'0', "1,0 0,1 0,5 1,6 3,6 4,5 4,1 3,0 1,0;0,1 4,5"},
      {U'1', "1,5 2,6 2,0;1,0 3,0"},
      {U'2', "0,5 1,6 3,6 4,5 4,4 0,0 4,0"},
      {U'3', "0,5 1,6 3,6 4,5 4,4 3,3 1,3;3,3 4,2 4,1 3,0 1,0 0,1"},
      {U'4', "3,0 3,6 0,2 4,2"},
      {U'5', "4,6 0,6 0,3 3,3 4,2 4,1 3,0 1,0 0,1"},
      {U'6', "4,5 3,6 1,6 0,5 0,1 1,0 3,0 4,1 4,2 3,3 0,3"},
      {U'7', "0,6 4,6 1,0"},
      {U'8', "1,3 0,4 0,5 1,6 3,6 4,5 4,4 3,3 1,3 0,2 0,1 1,0 3,0 4,1 4,2 3,3"},
      {U'9', "0,1 1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,4 1,3 4,3"},
      {U'-', "1,3 3,3"},
      {U'+', "0,3 4,3;2,1 2,5"},
      {U'.', "1.8,0 2.2,0"},
      {U',', "2,0.6 1.4,-0.8"},
      {U'_', "0,-0.6 4,-0.6"},
      {U'/', "0,0 4,6"},
      {U'\\', "0,6 4,0"},
      {U'(', "2.5,6 1.5,5 1.5,1 2.5,0"},
      {U')', "1.5,6 2.5,5 2.5,1 1.5,0"},
      {U'[', "3,6 1.5,6 1.5,0 3,0"},
      {U']', "1,6 2.5,6 2.5,0 1,0"},
      {U':', "2,0.6 2,1;2,3.6 2,4"},
      {U';', "2,3.6 2,4;2,0.6 1.4,-0.8"},
      {U'=', "0,2 4,2;0,4 4,4"},
      {U'%', "0,0 4,6;0,6 1,6 1,5 0,5 0,6;3,0 4,0 4,1 3,1 3,0"},
      {U'#', "1,0 1,6;3,0 3,6;0,2 4,2;0,4 4,4"},
      {U'*', "2,1 2,5;0,2 4,4;0,4 4,2"},
      {U'?', "0,5 1,6 3,6 4,5 4,4 2,3 2,2;2,0 2,0.4"},
      {U'!', "2,6 2,2;2,0 2,0.4"},
      {U'\'', "2,6 2,4.8"},
      {U'"', "1,6 1,4.8;3,6 3,4.8"},
      {U'&', "4,0 1,5 2,6 3,5 0,2 0,1 1,0 2,0 4,2"},
      {U'<', "4,5 0,3 4,1"},
      {U'>', "0,5 4,3 0,1"},
      {U'@', "3,2 3,4 1,4 1,2 3,2 4,3 4,5 3,6 1,6 0,5 0,1 1,0 4,0"},
      {U'~', "0,3 1,4 3,2 4,3"},
      {U'°', "1,5 2,6 3,5 2,4 1,5"},
      {U'Ω', "0,0 1,0 1,1 0,3 0,5 1,6 3,6 4,5 4,3 3,1 3,0 4,0"},
      {U'µ', "0,-1.5 0,4;0,1 1,0 3,0 4,1;4,0 4,4"},
      {U'±', "0,4 4,4;2,2 2,6;0,0 4,0"},
  };
  return g;
}

// Décodage UTF-8 ; lettres accentuées ramenées à leur base, minuscules en capitales
std::u32string decoder(const std::string& s) {
  std::u32string r;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    char32_t cp = c;
    int n = 1;
    if (c >= 0xF0 && i + 3 < s.size()) { cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu); n = 4; }
    else if (c >= 0xE0 && i + 2 < s.size()) { cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu); n = 3; }
    else if (c >= 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu); n = 2; }
    i += static_cast<std::size_t>(n);
    static const std::u32string avec = U"ÀÁÂÃÄÅàáâãäåÇçÈÉÊËèéêëÌÍÎÏìíîïÑñÒÓÔÕÖòóôõöÙÚÛÜùúûüÝýÿŒœÆæ";
    static const std::u32string sans = U"AAAAAAAAAAAACCEEEEEEEEIIIIIIIINNOOOOOOOOOOUUUUUUUUYYYOOAA";
    if (const auto k = avec.find(cp); k != std::u32string::npos) {
      r += sans[k];
      if (cp == U'Œ' || cp == U'œ') r += U'E';
      if (cp == U'Æ' || cp == U'æ') r += U'E';
      continue;
    }
    if (cp >= U'a' && cp <= U'z') cp = cp - U'a' + U'A';
    if (cp == U'\t') cp = U' ';
    r += cp;
  }
  return r;
}

struct Point2 {
  double x, y;
};
std::vector<std::vector<Point2>> polylignes(const char* def) {
  std::vector<std::vector<Point2>> r;
  std::string d(def);
  std::stringstream morceaux(d);
  std::string poly;
  while (std::getline(morceaux, poly, ';')) {
    std::vector<Point2> l;
    std::stringstream pts(poly);
    std::string xy;
    while (pts >> xy) {
      const auto v = xy.find(',');
      l.push_back({std::stod(xy.substr(0, v)), std::stod(xy.substr(v + 1))});
    }
    if (l.size() >= 2) r.push_back(std::move(l));
  }
  return r;
}

constexpr double AVANCE = 5.5;  // largeur d'un caractère, espace compris, en unités de grille
}  // namespace

namespace {
// Lignes d'un texte (0.34, 8.13) : « \n » sépare les lignes, l'interligne vaut 1,6 fois la hauteur des capitales
std::vector<std::u32string> lignes(const std::u32string& t) {
  std::vector<std::u32string> r(1);
  for (char32_t ch : t)
    if (ch == U'\n') r.emplace_back();
    else if (ch != U'\r') r.back() += ch;
  return r;
}
constexpr double INTERLIGNE = 9.6;  // unités de grille (la hauteur des capitales en vaut 6)
}  // namespace

Nm largeurTexte(const std::string& texte, Nm hauteur) {
  const std::u32string t = decoder(texte);
  if (t.empty()) return 0;
  std::size_t n = 0;
  for (const auto& l : lignes(t)) n = std::max(n, l.size());
  return n == 0 ? 0 : static_cast<Nm>(std::llround((static_cast<double>(n) * AVANCE - 1.5) * static_cast<double>(hauteur) / 6.0));
}

Nm epaisseurTexte(Nm hauteur) { return std::max<Nm>(120'000, static_cast<Nm>(std::llround(0.15 * static_cast<double>(hauteur)))); }

std::vector<TraitTexte> traitsTexte(const std::string& texte, Pt position, Nm hauteur, double rotation, bool miroir, AncrageTexte ancrage, bool centreVertical) {
  std::vector<TraitTexte> r;
  const std::u32string tout = decoder(texte);
  if (tout.empty() || hauteur <= 0) return r;
  const double e = static_cast<double>(hauteur) / 6.0;
  const double a = rotation * std::numbers::pi / 180, ca = std::cos(a), sa = std::sin(a);
  const auto ls = lignes(tout);
  const double n = static_cast<double>(ls.size());
  for (std::size_t j = 0; j < ls.size(); ++j) {
    const std::u32string& t = ls[j];
    const double largeur = (static_cast<double>(t.size()) * AVANCE - 1.5) * e;
    const double x0 = ancrage == AncrageTexte::Gauche ? 0 : ancrage == AncrageTexte::Centre ? -largeur / 2 : -largeur;
    // Centré : le bloc de lignes est centré sur la position ; sinon la première ligne est sur la ligne de base
    const double y0 = centreVertical ? -3 * e + ((n - 1) / 2 - static_cast<double>(j)) * INTERLIGNE * e : -static_cast<double>(j) * INTERLIGNE * e;
    auto placer = [&](double x, double y) {
      // Repère du texte : x vers la droite, y vers le haut ; la carte a l'axe Y vers le bas. La rotation suit la même
      // convention que les empreintes (tournerDegres) : un angle positif tourne dans le sens direct à l'écran (X3)
      double u = x0 + x, v = -(y0 + y);
      if (miroir) u = -u;
      const double X = u * ca + v * sa, Y = -u * sa + v * ca;
      return Pt{position.x + static_cast<Nm>(std::llround(X)), position.y + static_cast<Nm>(std::llround(Y))};
    };
    for (std::size_t i = 0; i < t.size(); ++i) {
      const auto it = glyphes().find(t[i]);
      if (it == glyphes().end()) {
        if (t[i] != U' ') {  // caractère inconnu : un petit rectangle, pour qu'il ne disparaisse pas en silence
          const double ox = static_cast<double>(i) * AVANCE * e;
          const Pt p1 = placer(ox, 0), p2 = placer(ox + 4 * e, 0), p3 = placer(ox + 4 * e, 6 * e), p4 = placer(ox, 6 * e);
          r.push_back({p1, p2});
          r.push_back({p2, p3});
          r.push_back({p3, p4});
          r.push_back({p4, p1});
        }
        continue;
      }
      const double ox = static_cast<double>(i) * AVANCE * e;
      for (const auto& l : polylignes(it->second))
        for (std::size_t k = 1; k < l.size(); ++k) r.push_back({placer(ox + l[k - 1].x * e, l[k - 1].y * e), placer(ox + l[k].x * e, l[k].y * e)});
    }
  }
  return r;
}

}  // namespace prom::ecad
