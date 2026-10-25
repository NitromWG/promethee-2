// Prométhée : petit écrivain PDF vectoriel interne au noyau (lot L19 de la 0.34). Pages en millimètres, origine en
// haut à gauche ; traits, rectangles, cercles, polygones, textes en Helvetica (WinAnsi), découpe rectangulaire. Sert
// aux plans à l'échelle 1 (planche de faisceau en pages assemblées) et aux plans de fabrication. Licence GPL-3.0-only.
#pragma once

#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace prom::pdfv {

inline std::string nb(double v) {
  char b[64];
  const auto r = std::to_chars(b, b + sizeof b, v, std::chars_format::fixed, 3);
  std::string s(b, r.ptr);
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  return s == "-0" || s.empty() ? std::string("0") : s;
}
// Texte UTF-8 en chaîne littérale WinAnsi (Latin-1 et quelques signes typographiques)
inline std::string litteral(const std::string& s) {
  std::string r = "(";
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const std::size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    char32_t cp = c;
    if (i + n > s.size()) cp = '?';
    else if (n == 4) cp = ((c & 0x07u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu);
    else if (n == 3) cp = ((c & 0x0Fu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu);
    else if (n == 2) cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu);
    i += i + n > s.size() ? 1 : n;
    char ch = '?';
    if (cp < 256) ch = static_cast<char>(cp);
    else if (cp == 0x2019) ch = '\'';
    else if (cp == 0x2013 || cp == 0x2014) ch = static_cast<char>(0x96);
    else if (cp == 0x2026) ch = static_cast<char>(0x85);
    else if (cp == 0x2192) ch = '>';
    else if (cp == 0x00D7) ch = static_cast<char>(0xD7);
    else if (cp == 0x2264) ch = '<';
    else if (cp == 0x2265) ch = '>';
    else if (cp == 0x2126 || cp == 0x03A9) ch = 'O';
    if (ch == '(' || ch == ')' || ch == '\\') r += '\\';
    r += ch;
  }
  return r + ")";
}

class Document {
 public:
  static constexpr double PT = 72.0 / 25.4;  // points par millimètre
  void page(double largeurMm, double hauteurMm) {
    m_pages.push_back({largeurMm, hauteurMm, {}});
  }
  std::size_t pages() const { return m_pages.size(); }
  // Couleur de trait et de remplissage (0 à 1)
  void couleur(double r, double v, double b) {
    flux() += nb(r) + " " + nb(v) + " " + nb(b) + " RG " + nb(r) + " " + nb(v) + " " + nb(b) + " rg\n";
  }
  void couleurHex(const std::string& hex) {
    unsigned r = 0, v = 0, b = 0;
    if (hex.size() == 7 && std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &v, &b) == 3) couleur(r / 255.0, v / 255.0, b / 255.0);
  }
  void epaisseur(double mm) { flux() += nb(mm * PT) + " w 1 J 1 j\n"; }
  void tirets(bool oui) { flux() += oui ? "[3 2] 0 d\n" : "[] 0 d\n"; }
  void ligne(double x0, double y0, double x1, double y1) { flux() += X(x0) + " " + Y(y0) + " m " + X(x1) + " " + Y(y1) + " l S\n"; }
  void polyligne(const std::vector<std::pair<double, double>>& p, bool fermer = false, bool remplir = false) {
    if (p.size() < 2) return;
    std::string& f = flux();
    f += X(p[0].first) + " " + Y(p[0].second) + " m ";
    for (std::size_t i = 1; i < p.size(); ++i) f += X(p[i].first) + " " + Y(p[i].second) + " l ";
    f += remplir ? (fermer ? "h B\n" : "B\n") : (fermer ? "h S\n" : "S\n");
  }
  void rectangle(double x, double y, double l, double h, bool remplir = false) {
    flux() += X(x) + " " + Y(y + h) + " " + nb(l * PT) + " " + nb(h * PT) + (remplir ? " re B\n" : " re S\n");
  }
  void cercle(double cx, double cy, double r, bool remplir = false) {
    const double k = 0.5523 * r;
    std::string& f = flux();
    f += X(cx + r) + " " + Y(cy) + " m ";
    f += X(cx + r) + " " + Y(cy - k) + " " + X(cx + k) + " " + Y(cy - r) + " " + X(cx) + " " + Y(cy - r) + " c ";
    f += X(cx - k) + " " + Y(cy - r) + " " + X(cx - r) + " " + Y(cy - k) + " " + X(cx - r) + " " + Y(cy) + " c ";
    f += X(cx - r) + " " + Y(cy + k) + " " + X(cx - k) + " " + Y(cy + r) + " " + X(cx) + " " + Y(cy + r) + " c ";
    f += X(cx + k) + " " + Y(cy + r) + " " + X(cx + r) + " " + Y(cy + k) + " " + X(cx + r) + " " + Y(cy) + " c ";
    f += remplir ? "B\n" : "S\n";
  }
  // Texte : « taille » en mm (hauteur du corps), angle en degrés (sens direct), alignement −1 gauche, 0 centré, 1 droite
  void texte(double x, double y, const std::string& t, double taille, bool gras = false, double angle = 0, int alignement = -1) {
    const double corps = taille * PT;
    const double largeur = 0.5 * corps * static_cast<double>(longueur(t));  // Helvetica : un demi-cadratin en moyenne
    const double a = angle * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
    const double decale = alignement < 0 ? 0 : alignement == 0 ? largeur / 2 : largeur;
    const double px = x * PT - decale * c, py = (m_pages.back().hauteur - y) * PT - decale * s;
    flux() += "BT /" + std::string(gras ? "F2 " : "F1 ") + nb(corps) + " Tf " + nb(c) + " " + nb(s) + " " + nb(-s) + " " + nb(c) + " " + nb(px) + " " + nb(py) + " Tm " + litteral(t) + " Tj ET\n";
  }
  // Découpe : tout ce qui suit jusqu'à « finDecoupe » reste dans le rectangle
  void decoupe(double x, double y, double l, double h) { flux() += "q " + X(x) + " " + Y(y + h) + " " + nb(l * PT) + " " + nb(h * PT) + " re W n\n"; }
  void finDecoupe() { flux() += "Q\n"; }
  // Translation du dessin qui suit (mm), annulée par « finDecoupe » (q … Q)
  void decaler(double dx, double dy) { flux() += "1 0 0 1 " + nb(dx * PT) + " " + nb(-dy * PT) + " cm\n"; }
  // Le point (x, y) du dessin qui suit va en (dx + k·x, dy + k·y) sur la page ; entre « enregistrer » et « restaurer »
  void transformer(double k, double dx, double dy) {
    const double H = m_pages.empty() ? 0 : m_pages.back().hauteur;
    flux() += nb(k) + " 0 0 " + nb(k) + " " + nb(dx * PT) + " " + nb((H - dy - k * H) * PT) + " cm\n";
  }
  void enregistrer() { flux() += "q\n"; }
  void restaurer() { flux() += "Q\n"; }
  std::string terminer(const std::string& titre) const {
    std::string s = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::map<int, std::size_t> pos;
    int n = 0;
    auto objet = [&](int k, const std::string& corps) {
      pos[k] = s.size();
      s += std::to_string(k) + " 0 obj\n" + corps + "\nendobj\n";
    };
    const int catalogue = ++n, arbre = ++n, f1 = ++n, f2 = ++n, info = ++n;
    objet(f1, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    objet(f2, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    std::string kids;
    for (const auto& p : m_pages) {
      const int contenu = ++n, page = ++n;
      pos[contenu] = s.size();
      s += std::to_string(contenu) + " 0 obj\n<< /Length " + std::to_string(p.flux.size()) + " >>\nstream\n" + p.flux + "\nendstream\nendobj\n";
      objet(page, "<< /Type /Page /Parent " + std::to_string(arbre) + " 0 R /MediaBox [0 0 " + nb(p.largeur * PT) + " " + nb(p.hauteur * PT) + "] /Resources << /Font << /F1 " + std::to_string(f1) +
                      " 0 R /F2 " + std::to_string(f2) + " 0 R >> >> /Contents " + std::to_string(contenu) + " 0 R >>");
      kids += std::to_string(page) + " 0 R ";
    }
    objet(arbre, "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(m_pages.size()) + " >>");
    objet(catalogue, "<< /Type /Catalog /Pages " + std::to_string(arbre) + " 0 R >>");
    objet(info, "<< /Title " + litteral(titre) + " /Creator (Promethee) /Producer (Promethee) >>");
    const std::size_t xref = s.size();
    s += "xref\n0 " + std::to_string(n + 1) + "\n0000000000 65535 f \n";
    for (int i = 1; i <= n; ++i) {
      char l[32];
      std::snprintf(l, sizeof l, "%010zu 00000 n \n", pos.count(i) ? pos[i] : std::size_t{0});
      s += l;
    }
    s += "trailer\n<< /Size " + std::to_string(n + 1) + " /Root " + std::to_string(catalogue) + " 0 R /Info " + std::to_string(info) + " 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return s;
  }

 private:
  struct Page {
    double largeur = 0, hauteur = 0;
    std::string flux;
  };
  std::vector<Page> m_pages;
  std::string& flux() {
    if (m_pages.empty()) page(297, 210);
    return m_pages.back().flux;
  }
  std::string X(double mm) const { return nb(mm * PT); }
  std::string Y(double mm) const { return nb((m_pages.back().hauteur - mm) * PT); }
  static std::size_t longueur(const std::string& t) {
    std::size_t n = 0;
    for (const unsigned char c : t) n += (c & 0xC0) != 0x80;
    return n;
  }
};

}  // namespace prom::pdfv
