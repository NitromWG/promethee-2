// Prométhée : documents PDF du module électronique (8.11, 8.29). Voir pdf_documents.hpp. Licence GPL-3.0-only.
#include "promethee/pdf_documents.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <map>
#include <numbers>
#include <set>

#include "promethee/police.hpp"

namespace prom::ecad {

namespace {
constexpr double PT_PAR_MM = 72.0 / 25.4;

// Nombre sans dépendre de la langue du système (le point décimal du PDF)
std::string nb(double v, int decimales = 3) {
  char b[64];
  const auto r = std::to_chars(b, b + sizeof b, v, std::chars_format::fixed, decimales);
  std::string s(b, r.ptr);
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  if (s == "-0" || s.empty()) s = "0";
  return s;
}
// Points de code d'un texte UTF-8
std::u32string utf32(const std::string& s) {
  std::u32string r;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const std::size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
    char32_t cp = n == 1 ? c : n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
    for (std::size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    r += cp;
    i += n;
  }
  return r;
}
// Chaîne littérale en Latin-1 (police standard en WinAnsi)
std::string litteral(const std::string& s) {
  std::string r = "(";
  for (char32_t cp : utf32(s)) {
    const char ch = cp < 256 ? static_cast<char>(cp) : cp == 0x2019 ? '\'' : cp == 0x2013 || cp == 0x2014 ? '-' : cp == 0x2026 ? '.' : '?';
    if (ch == '(' || ch == ')' || ch == '\\') r += '\\';
    if (ch == '\n') {
      r += "\\n";
      continue;
    }
    r += ch;
  }
  return r + ")";
}
// Chaîne de texte en UTF-16BE (signets, infobulles, métadonnées)
std::string texteUnicode(const std::string& s) {
  static const char* H = "0123456789ABCDEF";
  std::string r = "<FEFF";
  auto mot = [&](unsigned v) {
    for (int k = 12; k >= 0; k -= 4) r += H[(v >> k) & 0xF];
  };
  for (char32_t cp : utf32(s)) {
    if (cp >= 0x10000) {
      const unsigned x = static_cast<unsigned>(cp) - 0x10000;
      mot(0xD800 + (x >> 10));
      mot(0xDC00 + (x & 0x3FF));
    } else
      mot(static_cast<unsigned>(cp));
  }
  return r + ">";
}
std::string decoderTexte(const std::string& t) {
  // <FEFF…> ou (littéral)
  if (t.size() >= 2 && t.front() == '<') {
    std::string h;
    for (char ch : t.substr(1, t.size() - 2))
      if (std::isxdigit(static_cast<unsigned char>(ch))) h += ch;
    std::string out;
    std::size_t i = 0;
    if (h.size() >= 4 && (h.substr(0, 4) == "FEFF" || h.substr(0, 4) == "feff")) i = 4;
    for (; i + 3 < h.size(); i += 4) {
      unsigned v = static_cast<unsigned>(std::stoul(h.substr(i, 4), nullptr, 16));
      if (v >= 0xD800 && v < 0xDC00 && i + 7 < h.size()) {
        const unsigned w = static_cast<unsigned>(std::stoul(h.substr(i + 4, 4), nullptr, 16));
        v = 0x10000 + ((v - 0xD800) << 10) + (w - 0xDC00);
        i += 4;
      }
      if (v < 0x80) out += static_cast<char>(v);
      else if (v < 0x800) {
        out += static_cast<char>(0xC0 | (v >> 6));
        out += static_cast<char>(0x80 | (v & 0x3F));
      } else if (v < 0x10000) {
        out += static_cast<char>(0xE0 | (v >> 12));
        out += static_cast<char>(0x80 | ((v >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (v & 0x3F));
      } else {
        out += static_cast<char>(0xF0 | (v >> 18));
        out += static_cast<char>(0x80 | ((v >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((v >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (v & 0x3F));
      }
    }
    return out;
  }
  std::string out;
  for (std::size_t i = 1; i + 1 < t.size(); ++i) {
    if (t[i] == '\\' && i + 2 < t.size()) {
      ++i;
      out += t[i] == 'n' ? '\n' : t[i];
      continue;
    }
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (c < 0x80) out += static_cast<char>(c);
    else {
      out += static_cast<char>(0xC0 | (c >> 6));
      out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}
std::string echapperXml(const std::string& s) {
  std::string r;
  for (char ch : s) r += ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : ch == '&' ? "&amp;" : ch == '"' ? "&quot;" : std::string(1, ch);
  return r;
}

// ---------- Écrivain ----------
class EcrivainPdf {
 public:
  EcrivainPdf() { m_s = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n"; }
  int reserver() { return ++m_n; }
  void objet(int n, const std::string& corps) {
    m_pos[n] = m_s.size();
    m_s += std::to_string(n) + " 0 obj\n" + corps + "\nendobj\n";
  }
  void flux(int n, const std::string& dict, const std::string& donnees) {
    m_pos[n] = m_s.size();
    m_s += std::to_string(n) + " 0 obj\n<< " + dict + " /Length " + std::to_string(donnees.size()) + " >>\nstream\n" + donnees + "\nendstream\nendobj\n";
  }
  std::string terminer(int racine, int info) {
    const std::size_t xref = m_s.size();
    m_s += "xref\n0 " + std::to_string(m_n + 1) + "\n0000000000 65535 f \n";
    for (int i = 1; i <= m_n; ++i) {
      char l[32];
      std::snprintf(l, sizeof l, "%010zu 00000 n \n", m_pos.count(i) ? m_pos[i] : std::size_t{0});
      m_s += l;
    }
    m_s += "trailer\n<< /Size " + std::to_string(m_n + 1) + " /Root " + std::to_string(racine) + " 0 R" + (info ? " /Info " + std::to_string(info) + " 0 R" : std::string()) + " >>\nstartxref\n" + std::to_string(xref) +
           "\n%%EOF\n";
    return m_s;
  }

 private:
  std::string m_s;
  std::map<int, std::size_t> m_pos;
  int m_n = 0;
};

// ---------- Lecture de la structure ----------
struct Structure {
  std::string s;
  std::map<int, std::size_t> offsets;  // dernière révision de chaque objet
  std::string trailer;                 // trailer de la dernière révision
  std::size_t startxref = 0;
  int taille = 0, revisions = 0;
  bool ok = false;
};
std::size_t sauterBlancs(const std::string& s, std::size_t i) {
  while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == '%')) {
    if (s[i] == '%')
      while (i < s.size() && s[i] != '\n' && s[i] != '\r') ++i;
    else
      ++i;
  }
  return i;
}
// Fin d'une valeur PDF commençant en i (dictionnaire, tableau, chaîne, nom, nombre, référence)
std::size_t finValeur(const std::string& s, std::size_t i) {
  i = sauterBlancs(s, i);
  if (i >= s.size()) return i;
  if (s.compare(i, 2, "<<") == 0) {
    int prof = 0;
    while (i < s.size()) {
      if (s.compare(i, 2, "<<") == 0) {
        ++prof;
        i += 2;
      } else if (s.compare(i, 2, ">>") == 0) {
        --prof;
        i += 2;
        if (prof == 0) return i;
      } else if (s[i] == '(') {
        i = finValeur(s, i);
      } else
        ++i;
    }
    return i;
  }
  if (s[i] == '[') {
    ++i;
    while (i < s.size()) {
      i = sauterBlancs(s, i);
      if (i < s.size() && s[i] == ']') return i + 1;
      i = finValeur(s, i);
    }
    return i;
  }
  if (s[i] == '(') {
    int prof = 0;
    for (; i < s.size(); ++i) {
      if (s[i] == '\\') {
        ++i;
        continue;
      }
      if (s[i] == '(') ++prof;
      else if (s[i] == ')' && --prof == 0) return i + 1;
    }
    return i;
  }
  if (s[i] == '<') return s.find('>', i) + 1;
  if (s[i] == '/') {
    ++i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && std::string("/[]<>()").find(s[i]) == std::string::npos) ++i;
    return i;
  }
  // Nombre, ou référence « n g R »
  auto mot = [&](std::size_t j) {
    while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j])) && std::string("/[]<>()").find(s[j]) == std::string::npos) ++j;
    return j;
  };
  const std::size_t f1 = mot(i);
  const std::size_t j = sauterBlancs(s, f1);
  if (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
    const std::size_t f2 = mot(j);
    const std::size_t k = sauterBlancs(s, f2);
    if (k < s.size() && s[k] == 'R' && (k + 1 >= s.size() || !std::isalnum(static_cast<unsigned char>(s[k + 1])))) return k + 1;
  }
  return f1;
}
// Valeur d'une clé au premier niveau d'un dictionnaire « << … >> »
std::optional<std::string> valeur(const std::string& dict, const std::string& cle) {
  std::size_t i = dict.find("<<");
  if (i == std::string::npos) return std::nullopt;
  i += 2;
  while (i < dict.size()) {
    i = sauterBlancs(dict, i);
    if (dict.compare(i, 2, ">>") == 0) break;
    if (dict[i] != '/') return std::nullopt;
    const std::size_t finCle = finValeur(dict, i);
    const std::string k = dict.substr(i + 1, finCle - i - 1);
    const std::size_t debut = sauterBlancs(dict, finCle), fin = finValeur(dict, debut);
    if (k == cle) return dict.substr(debut, fin - debut);
    i = fin;
  }
  return std::nullopt;
}
// Le même dictionnaire, la clé remplacée (ou ajoutée)
std::string avecCle(const std::string& dict, const std::string& cle, const std::string& v) {
  std::size_t i = dict.find("<<");
  if (i == std::string::npos) return dict;
  i += 2;
  while (i < dict.size()) {
    i = sauterBlancs(dict, i);
    if (dict.compare(i, 2, ">>") == 0) break;
    if (dict[i] != '/') break;
    const std::size_t finCle = finValeur(dict, i);
    const std::string k = dict.substr(i + 1, finCle - i - 1);
    const std::size_t debut = sauterBlancs(dict, finCle), fin = finValeur(dict, debut);
    if (k == cle) return dict.substr(0, debut) + v + dict.substr(fin);
    i = fin;
  }
  const std::size_t f = dict.rfind(">>");
  return dict.substr(0, f) + "/" + cle + " " + v + "\n" + dict.substr(f);
}
int refNum(const std::string& v) { return std::atoi(v.c_str()); }
std::vector<int> refsTableau(const std::string& t) {
  std::vector<int> r;
  std::size_t i = t.find('[');
  if (i == std::string::npos) return r;
  ++i;
  while (i < t.size()) {
    i = sauterBlancs(t, i);
    if (i >= t.size() || t[i] == ']') break;
    const std::size_t f = finValeur(t, i);
    const std::string v = t.substr(i, f - i);
    if (!v.empty() && v.back() == 'R') r.push_back(refNum(v));
    i = f;
  }
  return r;
}

Structure lireStructure(const std::string& s) {
  Structure st;
  st.s = s;
  const std::size_t sx = s.rfind("startxref");
  if (sx == std::string::npos) return st;
  std::size_t xref = static_cast<std::size_t>(std::strtoull(s.c_str() + sx + 9, nullptr, 10));
  st.startxref = xref;
  std::set<std::size_t> vus;
  bool premier = true;
  while (xref < s.size() && vus.insert(xref).second) {
    std::size_t i = sauterBlancs(s, xref);
    if (s.compare(i, 4, "xref") != 0) return st;  // table en flux (PDF 1.5) : non prise en charge
    ++st.revisions;
    i += 4;
    while (true) {
      i = sauterBlancs(s, i);
      if (s.compare(i, 7, "trailer") == 0) break;
      char* fin = nullptr;
      const long debut = std::strtol(s.c_str() + i, &fin, 10);
      const long nombre = std::strtol(fin, &fin, 10);
      i = static_cast<std::size_t>(fin - s.c_str());
      for (long k = 0; k < nombre; ++k) {
        i = sauterBlancs(s, i);
        const std::size_t off = static_cast<std::size_t>(std::strtoull(s.c_str() + i, nullptr, 10));
        const char etat = s[i + 17];
        if (etat == 'n' && !st.offsets.count(static_cast<int>(debut + k))) st.offsets[static_cast<int>(debut + k)] = off;
        i += 20;
      }
    }
    const std::size_t d = s.find("<<", i), f = finValeur(s, d);
    const std::string trailer = s.substr(d, f - d);
    if (premier) {
      st.trailer = trailer;
      if (const auto t = valeur(trailer, "Size")) st.taille = std::atoi(t->c_str());
      premier = false;
    }
    const auto prev = valeur(trailer, "Prev");
    if (!prev) break;
    xref = static_cast<std::size_t>(std::strtoull(prev->c_str(), nullptr, 10));
  }
  st.ok = valeur(st.trailer, "Root").has_value();
  return st;
}
// Texte de l'objet n (le dictionnaire, ou la valeur) sans son flux
std::string objet(const Structure& st, int n) {
  const auto it = st.offsets.find(n);
  if (it == st.offsets.end()) return {};
  std::size_t i = st.s.find("obj", it->second);
  if (i == std::string::npos) return {};
  i = sauterBlancs(st.s, i + 3);
  const std::size_t f = finValeur(st.s, i);
  return st.s.substr(i, f - i);
}
std::string fluxObjet(const Structure& st, int n) {
  const auto it = st.offsets.find(n);
  if (it == st.offsets.end()) return {};
  std::size_t i = st.s.find("stream", it->second);
  const std::size_t f = st.s.find("endstream", it->second);
  if (i == std::string::npos || f == std::string::npos || i > f) return {};
  i += 6;
  if (st.s[i] == '\r') ++i;
  if (st.s[i] == '\n') ++i;
  return st.s.substr(i, f - i);
}
// Pages dans l'ordre (feuilles de l'arbre)
void pagesDe(const Structure& st, int n, std::vector<int>& r, int profondeur = 0) {
  if (profondeur > 32) return;
  const std::string o = objet(st, n);
  const auto type = valeur(o, "Type");
  if (type && *type == "/Page") {
    r.push_back(n);
    return;
  }
  if (const auto kids = valeur(o, "Kids"))
    for (int k : refsTableau(*kids)) pagesDe(st, k, r, profondeur + 1);
}
std::pair<double, double> tailleMm(const Structure& st, int page) {
  std::string o = objet(st, page);
  for (int i = 0; i < 16; ++i) {
    if (const auto mb = valeur(o, "MediaBox")) {
      double v[4] = {0, 0, 0, 0};
      std::size_t p = mb->find('[') + 1;
      for (double& x : v) {
        char* fin = nullptr;
        x = std::strtod(mb->c_str() + p, &fin);
        p = static_cast<std::size_t>(fin - mb->c_str());
      }
      return {(v[2] - v[0]) / PT_PAR_MM, (v[3] - v[1]) / PT_PAR_MM};
    }
    const auto parent = valeur(o, "Parent");
    if (!parent) break;
    o = objet(st, refNum(*parent));
  }
  return {210, 297};
}
}  // namespace

std::string xmpPdf(const MetaPdf& m) {
  return "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>\n"
         "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
         "<rdf:Description rdf:about=\"\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\" xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" "
         "xmlns:pdfaid=\"http://www.aiim.org/pdfa/ns/id/\" xmlns:prom=\"https://promethee.example/ns/1.0/\">\n"
         "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" +
         echapperXml(m.titre) + "</rdf:li></rdf:Alt></dc:title>\n<dc:creator><rdf:Seq><rdf:li>" + echapperXml(m.auteur) + "</rdf:li></rdf:Seq></dc:creator>\n<dc:description><rdf:Alt><rdf:li xml:lang=\"x-default\">" +
         echapperXml(m.sujet) + "</rdf:li></rdf:Alt></dc:description>\n<pdf:Keywords>" + echapperXml(m.motsCles) + "</pdf:Keywords>\n<pdf:Producer>" + echapperXml(m.createur) + "</pdf:Producer>\n<xmp:CreatorTool>" +
         echapperXml(m.createur) + "</xmp:CreatorTool>\n" + (m.date.empty() ? std::string() : "<xmp:CreateDate>" + echapperXml(m.date) + "</xmp:CreateDate>\n") + "<prom:Revision>" + echapperXml(m.revision) +
         "</prom:Revision>\n</rdf:Description>\n</rdf:RDF></x:xmpmeta>\n<?xpacket end=\"w\"?>";
}

MetaPdf metaDepuisCartouche(const ConceptionElec& c) {
  MetaPdf m;
  m.titre = c.cartouche.titre.empty() ? c.nom : c.cartouche.titre;
  m.auteur = c.cartouche.auteur.empty() ? c.cartouche.societe : c.cartouche.auteur;
  m.sujet = c.cartouche.commentaire;
  m.revision = c.cartouche.revision;
  m.date = c.cartouche.date;
  m.motsCles = c.cartouche.societe;
  return m;
}

std::string completerPdf(const std::string& pdf, const std::vector<SignetPdf>& signets, const std::vector<LienPdf>& liens, const std::optional<MetaPdf>& meta) {
  const Structure st = lireStructure(pdf);
  if (!st.ok || st.taille <= 0) return pdf;
  const int racine = refNum(*valeur(st.trailer, "Root"));
  std::string catalogue = objet(st, racine);
  const auto refPages = valeur(catalogue, "Pages");
  if (!refPages) return pdf;
  std::vector<int> pages;
  pagesDe(st, refNum(*refPages), pages);
  if (pages.empty()) return pdf;
  std::string ajout = pdf.back() == '\n' ? std::string() : std::string("\n");
  std::map<int, std::size_t> positions;
  int suivant = st.taille;
  auto ecrire = [&](int n, const std::string& corps) {
    positions[n] = pdf.size() + ajout.size();
    ajout += std::to_string(n) + " 0 obj\n" + corps + "\nendobj\n";
  };
  auto pageValide = [&](int p) { return std::clamp(p, 0, static_cast<int>(pages.size()) - 1); };
  auto destination = [&](int page, double x, double y) {
    const int p = pageValide(page);
    const double h = tailleMm(st, pages[static_cast<std::size_t>(p)]).second;
    return "[" + std::to_string(pages[static_cast<std::size_t>(p)]) + " 0 R /XYZ " + nb(x * PT_PAR_MM) + " " + nb((h - y) * PT_PAR_MM) + " 0]";
  };
  // Signets : une liste à deux niveaux
  if (!signets.empty()) {
    const int racineSignets = suivant++;
    std::vector<int> numeros;
    for (std::size_t i = 0; i < signets.size(); ++i) numeros.push_back(suivant++);
    std::vector<int> parent(signets.size(), racineSignets);
    std::vector<std::vector<std::size_t>> enfants(signets.size() + 1);  // dernier : la racine
    int dernierPremier = -1;
    for (std::size_t i = 0; i < signets.size(); ++i) {
      if (signets[i].niveau > 0 && dernierPremier >= 0) {
        parent[i] = numeros[static_cast<std::size_t>(dernierPremier)];
        enfants[static_cast<std::size_t>(dernierPremier)].push_back(i);
      } else {
        dernierPremier = static_cast<int>(i);
        enfants[signets.size()].push_back(i);
      }
    }
    for (std::size_t g = 0; g <= signets.size(); ++g) {
      const auto& liste = enfants[g];
      for (std::size_t j = 0; j < liste.size(); ++j) {
        const std::size_t i = liste[j];
        std::string d = "<< /Title " + texteUnicode(signets[i].titre) + " /Parent " + std::to_string(parent[i]) + " 0 R /Dest " + destination(signets[i].page, signets[i].x, signets[i].y);
        if (j > 0) d += " /Prev " + std::to_string(numeros[liste[j - 1]]) + " 0 R";
        if (j + 1 < liste.size()) d += " /Next " + std::to_string(numeros[liste[j + 1]]) + " 0 R";
        if (i < signets.size() && !enfants[i].empty())
          d += " /First " + std::to_string(numeros[enfants[i].front()]) + " 0 R /Last " + std::to_string(numeros[enfants[i].back()]) + " 0 R /Count " + std::to_string(enfants[i].size());
        ecrire(numeros[i], d + " >>");
      }
    }
    const auto& premiers = enfants[signets.size()];
    ecrire(racineSignets, "<< /Type /Outlines /First " + std::to_string(numeros[premiers.front()]) + " 0 R /Last " + std::to_string(numeros[premiers.back()]) + " 0 R /Count " + std::to_string(premiers.size()) + " >>");
    catalogue = avecCle(catalogue, "Outlines", std::to_string(racineSignets) + " 0 R");
    catalogue = avecCle(catalogue, "PageMode", "/UseOutlines");
  }
  // Liens : annotations ajoutées aux pages
  std::map<int, std::vector<int>> annotations;
  for (const auto& l : liens) {
    const int p = pageValide(l.page);
    const double h = tailleMm(st, pages[static_cast<std::size_t>(p)]).second;
    const int n = suivant++;
    std::string d = "<< /Type /Annot /Subtype /Link /Rect [" + nb(l.x0 * PT_PAR_MM) + " " + nb((h - l.y1) * PT_PAR_MM) + " " + nb(l.x1 * PT_PAR_MM) + " " + nb((h - l.y0) * PT_PAR_MM) +
                    "] /Border [0 0 0] /Dest " + destination(l.pageCible, l.xCible, l.yCible);
    if (!l.infobulle.empty()) d += " /Contents " + texteUnicode(l.infobulle);
    ecrire(n, d + " >>");
    annotations[pages[static_cast<std::size_t>(p)]].push_back(n);
  }
  for (const auto& [page, refs] : annotations) {
    std::string o = objet(st, page), liste;
    for (int r : refs) liste += " " + std::to_string(r) + " 0 R";
    if (const auto a = valeur(o, "Annots")) {
      if (!a->empty() && a->back() == 'R') {
        // Tableau indirect : sa nouvelle révision
        const int na = refNum(*a);
        std::string t = objet(st, na);
        const std::size_t f = t.rfind(']');
        if (f != std::string::npos) ecrire(na, t.substr(0, f) + liste + " ]");
        continue;
      }
      const std::size_t f = a->rfind(']');
      o = avecCle(o, "Annots", a->substr(0, f) + liste + " ]");
    } else
      o = avecCle(o, "Annots", "[" + liste + " ]");
    ecrire(page, o);
  }
  if (meta) {
    const int n = suivant++;
    const std::string x = xmpPdf(*meta);
    positions[n] = pdf.size() + ajout.size();
    ajout += std::to_string(n) + " 0 obj\n<< /Type /Metadata /Subtype /XML /Length " + std::to_string(x.size()) + " >>\nstream\n" + x + "\nendstream\nendobj\n";
    catalogue = avecCle(catalogue, "Metadata", std::to_string(n) + " 0 R");
  }
  ecrire(racine, catalogue);
  // Table xref de la révision, puis trailer qui pointe la précédente
  const std::size_t xref = pdf.size() + ajout.size();
  ajout += "xref\n0 1\n0000000000 65535 f \n";
  for (const auto& [n, pos] : positions) {
    char l[48];
    std::snprintf(l, sizeof l, "%d 1\n%010zu 00000 n \n", n, pos);
    ajout += l;
  }
  std::string trailer = avecCle(st.trailer, "Size", std::to_string(suivant));
  trailer = avecCle(trailer, "Prev", std::to_string(st.startxref));
  ajout += "trailer\n" + trailer + "\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
  return pdf + ajout;
}

LecturePdf lirePdf(const std::string& pdf) {
  LecturePdf r;
  const Structure st = lireStructure(pdf);
  if (!st.ok) {
    r.erreur = "structure illisible (table xref en flux ou fichier abîmé)";
    return r;
  }
  r.revisions = st.revisions;
  const std::string cat = objet(st, refNum(*valeur(st.trailer, "Root")));
  std::vector<int> pages;
  if (const auto p = valeur(cat, "Pages")) pagesDe(st, refNum(*p), pages);
  r.pages = static_cast<int>(pages.size());
  for (int p : pages) {
    r.taillesPages.push_back(tailleMm(st, p));
    const std::string o = objet(st, p);
    if (const auto a = valeur(o, "Annots")) {
      const std::string t = !a->empty() && a->back() == 'R' ? objet(st, refNum(*a)) : *a;
      for (int n : refsTableau(t)) {
        const std::string an = objet(st, n);
        if (const auto s = valeur(an, "Subtype"); s && *s == "/Link") ++r.liens;
        if (valeur(an, "Contents")) ++r.infobulles;
      }
    }
  }
  // Signets en profondeur d'abord
  if (const auto out = valeur(cat, "Outlines")) {
    std::set<int> vus;
    std::function<void(int)> parcourir = [&](int n) {
      while (n > 0 && vus.insert(n).second) {
        const std::string o = objet(st, n);
        if (const auto t = valeur(o, "Title")) r.signets.push_back(decoderTexte(*t));
        if (const auto f = valeur(o, "First")) parcourir(refNum(*f));
        const auto nx = valeur(o, "Next");
        n = nx ? refNum(*nx) : 0;
      }
    };
    const std::string racine = objet(st, refNum(*out));
    if (const auto f = valeur(racine, "First")) parcourir(refNum(*f));
  }
  if (const auto m = valeur(cat, "Metadata")) r.xmp = fluxObjet(st, refNum(*m));
  r.ok = true;
  return r;
}

// ---------- Schéma ----------
std::vector<SignetPdf> signetsSchema(const ConceptionElec& c) {
  std::vector<SignetPdf> r;
  for (std::size_t i = 0; i < c.feuilles.size(); ++i) {
    const auto& f = c.feuilles[i];
    r.push_back({std::to_string(i + 1) + ". " + f.nom, static_cast<int>(i), 0, 0, 0});
  }
  return r;
}

std::vector<LienPdf> liensSchema(const ConceptionElec& c) {
  struct Place {
    int feuille;
    Pt p;
    std::size_t longueur;
  };
  std::map<std::string, std::vector<Place>> parNom;
  for (std::size_t i = 0; i < c.feuilles.size(); ++i)
    for (const auto& e : c.feuilles[i].etiquettes)
      if ((e.type == TypeEtiquette::Globale || e.type == TypeEtiquette::Hierarchique || e.type == TypeEtiquette::HorsFeuille) && !e.texte.empty())
        parNom[e.texte].push_back({static_cast<int>(i), e.p, utf32(e.texte).size()});
  std::vector<LienPdf> r;
  for (const auto& [nom, places] : parNom) {
    std::set<int> feuilles;
    for (const auto& p : places) feuilles.insert(p.feuille);
    if (feuilles.size() < 2) continue;
    for (const auto& a : places) {
      // La première occurrence sur une autre feuille, en tournant d'une feuille à la suivante
      const Place* cible = nullptr;
      for (int pas = 1; pas < static_cast<int>(c.feuilles.size()) && !cible; ++pas) {
        const int f = (a.feuille + pas) % static_cast<int>(c.feuilles.size());
        for (const auto& b : places)
          if (b.feuille == f) {
            cible = &b;
            break;
          }
      }
      if (!cible) continue;
      const double l = 1.4 * static_cast<double>(a.longueur) + 3;  // mm : largeur du texte et de sa forme
      LienPdf k;
      k.page = a.feuille;
      k.x0 = mm(a.p.x) - l;
      k.x1 = mm(a.p.x) + l;
      k.y0 = mm(a.p.y) - 2;
      k.y1 = mm(a.p.y) + 2;
      k.pageCible = cible->feuille;
      k.xCible = std::max(0.0, mm(cible->p.x) - 20);
      k.yCible = std::max(0.0, mm(cible->p.y) - 20);
      k.infobulle = "« " + nom + " » sur la feuille " + std::to_string(cible->feuille + 1);
      r.push_back(k);
    }
  }
  return r;
}

// ---------- Circuit par couches ----------
namespace {
std::string couleurPdf(const std::string& hex, bool trait) {
  unsigned v = 0x404040;
  if (hex.size() == 7 && hex[0] == '#') v = static_cast<unsigned>(std::stoul(hex.substr(1), nullptr, 16));
  return nb(((v >> 16) & 0xFF) / 255.0) + " " + nb(((v >> 8) & 0xFF) / 255.0) + " " + nb((v & 0xFF) / 255.0) + (trait ? " RG\n" : " rg\n");
}
std::string couleurDefaut(Couche k) {
  switch (k) {
    case Couche::CuivreDessus: return "#C83232";
    case Couche::CuivreDessous: return "#3250C8";
    case Couche::SerigraphieDessus:
    case Couche::SerigraphieDessous: return "#202020";
    case Couche::VernisDessus:
    case Couche::VernisDessous: return "#2E7D32";
    case Couche::PateDessus:
    case Couche::PateDessous: return "#7F7F7F";
    case Couche::Contour: return "#D4A017";
    default: return estCuivre(k) ? "#C88232" : "#606060";
  }
}
bool dessous(Couche k) {
  return k == Couche::CuivreDessous || k == Couche::SerigraphieDessous || k == Couche::VernisDessous || k == Couche::PateDessous || k == Couche::ColleDessous || k == Couche::ProtectionDessous;
}
}  // namespace

std::string pdfCircuit(const ConceptionElec& c, const OptionsPdfCircuit& o) {
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  std::vector<CouchePdf> couches = o.couches;
  if (couches.empty())
    for (const auto& cf : m.couches)
      if (cf.couche != Couche::Contour) couches.push_back({cf.couche, couleurDefaut(cf.couche)});
  const Boite b = m.boite;
  const double W = mm(b.largeur()) + 2 * o.marge, Hc = mm(b.hauteur()) + 2 * o.marge, bandeau = 12, H = Hc + bandeau;
  EcrivainPdf w;
  const int catalogue = w.reserver(), arbre = w.reserver(), police = w.reserver(), info = w.reserver(), transparence = w.reserver();
  w.objet(police, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
  w.objet(transparence, "<< /Type /ExtGState /ca 0.55 /CA 0.55 >>");
  std::vector<std::pair<std::string, std::vector<CouchePdf>>> pagesVoulues;
  if (o.superposer) pagesVoulues.push_back({"Toutes les couches", couches});
  else
    for (const auto& x : couches) pagesVoulues.push_back({nomCouche(x.couche), {x}});
  std::vector<int> pages, signets;
  for (const auto& [titre, liste] : pagesVoulues) {
    const bool miroir = o.miroirDessous && !o.superposer && liste.size() == 1 && dessous(liste.front().couche);
    auto X = [&](Nm x) { return ((miroir ? mm(b.x1) - mm(x) + mm(b.x0) : mm(x)) - mm(b.x0) + o.marge) * PT_PAR_MM; };
    auto Y = [&](Nm y) { return (H - (mm(y) - mm(b.y0) + o.marge)) * PT_PAR_MM; };
    auto P = [&](Pt p) { return nb(X(p.x)) + " " + nb(Y(p.y)); };
    std::string s;
    s += "1 J 1 j\n";
    if (o.superposer) s += "/T0 gs\n";
    auto chemin = [&](const Polygone& poly) {
      if (poly.size() < 3) return;
      s += P(poly.front()) + " m\n";
      for (std::size_t i = 1; i < poly.size(); ++i) s += P(poly[i]) + " l\n";
      s += "h\n";
    };
    for (const auto& x : liste) {
      const CoucheFab* cf = nullptr;
      for (const auto& y : m.couches)
        if (y.couche == x.couche) cf = &y;
      if (!cf) continue;
      s += couleurPdf(x.couleur, false) + couleurPdf(x.couleur, true);
      for (const auto& p : cf->primitives) {
        switch (p.type) {
          case PrimitiveFab::Type::Trait:
          case PrimitiveFab::Type::Arc:
            s += nb(std::max(mm(p.l), 0.05) * PT_PAR_MM) + " w\n" + P(p.a) + " m\n" + (p.type == PrimitiveFab::Type::Arc ? P(p.m) + " l\n" : std::string()) + P(p.b) + " l\nS\n";
            break;
          case PrimitiveFab::Type::Disque: chemin(cercle(p.a, std::max<Nm>(p.l / 2, 1))); s += "f\n"; break;
          case PrimitiveFab::Type::Rect: chemin(rectangle(p.a, p.l, p.h, p.rotation)); s += "f\n"; break;
          case PrimitiveFab::Type::Oblong: chemin(rectangleArrondi(p.a, p.l, p.h, std::min(p.l, p.h) / 2, p.rotation)); s += "f\n"; break;
          case PrimitiveFab::Type::RectArrondi: chemin(rectangleArrondi(p.a, p.l, p.h, p.r, p.rotation)); s += "f\n"; break;
          case PrimitiveFab::Type::Region: chemin(p.region); s += "f*\n"; break;
        }
      }
      // Perçages en blanc sur le cuivre et le vernis
      if (estCuivre(x.couche) || x.couche == Couche::VernisDessus || x.couche == Couche::VernisDessous) {
        s += "1 1 1 rg\n";
        for (const auto& t : m.trous) {
          chemin(cercle(t.p, std::max<Nm>(t.diametre / 2, 1)));
          s += "f\n";
        }
      }
    }
    if (o.contour) {
      s += couleurPdf(couleurDefaut(Couche::Contour), true) + nb(0.15 * PT_PAR_MM) + " w\n";
      chemin(m.contour);
      s += "S\n";
    }
    // Repères en texte invisible (mode 3) à leur place : le lecteur les trouve et les surligne
    std::string annots;
    std::vector<int> refsAnnots;
    for (const auto& e : c.circuit.empreintes) {
      const ComposantElec* x = c.composant(e.composant);
      const DefEmpreinte* d = c.empreinte(e.empreinte);
      if (!x || !d || x->reference.empty()) continue;
      const Boite be = boite(encombrementSurCarte(e, *d));
      const double hauteur = std::clamp(mm(be.hauteur()) * 0.5, 0.6, 2.5);
      if (o.texteCherchable)
        s += "BT 3 Tr /F1 " + nb(hauteur * PT_PAR_MM) + " Tf " + nb(X(be.centre().x) - hauteur * 0.6 * PT_PAR_MM * static_cast<double>(x->reference.size()) / 2) + " " + nb(Y(be.centre().y) - hauteur * 0.35 * PT_PAR_MM) + " Td " +
             litteral(x->reference) + " Tj ET\n";
      if (o.empreintesCliquables && (o.superposer || dessous(liste.front().couche) == e.dessous)) {
        const int n = w.reserver();
        const double x0 = std::min(X(be.x0), X(be.x1)), x1 = std::max(X(be.x0), X(be.x1));
        w.objet(n, "<< /Type /Annot /Subtype /Square /Rect [" + nb(x0) + " " + nb(Y(be.y1)) + " " + nb(x1) + " " + nb(Y(be.y0)) + "] /Border [0 0 0] /F 4 /Contents " +
                       texteUnicode(x->reference + (x->valeur.empty() ? std::string() : " — " + x->valeur) + " — " + e.empreinte) + " /T " + texteUnicode(x->reference) + " >>");
        refsAnnots.push_back(n);
      }
    }
    if (!o.filigrane.empty()) {
      const double cx = W / 2 * PT_PAR_MM, cy = H / 2 * PT_PAR_MM, taille = std::min(W, H) / 8 * PT_PAR_MM;
      s += "q 0.85 g BT /F1 " + nb(taille) + " Tf 0.7071 0.7071 -0.7071 0.7071 " + nb(cx - taille * 0.25 * static_cast<double>(o.filigrane.size())) + " " + nb(cy - taille * 0.3) + " Tm " +
           litteral(o.filigrane) + " Tj ET Q\n";
    }
    // Bandeau : nom, révision, couche, et règle de 50 mm
    s += "0 g BT /F1 9 Tf " + nb(o.marge * PT_PAR_MM) + " " + nb(4 * PT_PAR_MM) + " Td " + litteral(c.nom + " — révision " + c.cartouche.revision + " — " + titre + (miroir ? " (vue de dessous)" : "")) + " Tj ET\n";
    s += "0 G 0.4 w " + nb((W - o.marge - 50) * PT_PAR_MM) + " " + nb(5 * PT_PAR_MM) + " m " + nb((W - o.marge) * PT_PAR_MM) + " " + nb(5 * PT_PAR_MM) + " l S\n";
    const int contenu = w.reserver(), page = w.reserver();
    w.flux(contenu, "", s);
    std::string annotsListe;
    for (int n : refsAnnots) annotsListe += " " + std::to_string(n) + " 0 R";
    w.objet(page, "<< /Type /Page /Parent " + std::to_string(arbre) + " 0 R /MediaBox [0 0 " + nb(W * PT_PAR_MM) + " " + nb(H * PT_PAR_MM) + "] /Contents " + std::to_string(contenu) +
                      " 0 R /Resources << /Font << /F1 " + std::to_string(police) + " 0 R >> /ExtGState << /T0 " + std::to_string(transparence) + " 0 R >> >>" +
                      (annotsListe.empty() ? std::string() : " /Annots [" + annotsListe + " ]") + " >>");
    pages.push_back(page);
    (void)annots;
  }
  std::string kids;
  for (int p : pages) kids += " " + std::to_string(p) + " 0 R";
  w.objet(arbre, "<< /Type /Pages /Kids [" + kids + " ] /Count " + std::to_string(pages.size()) + " >>");
  // Signets : une entrée par page
  const int racineSignets = w.reserver();
  std::vector<int> items;
  for (std::size_t i = 0; i < pages.size(); ++i) items.push_back(w.reserver());
  for (std::size_t i = 0; i < pages.size(); ++i)
    w.objet(items[i], "<< /Title " + texteUnicode(pagesVoulues[i].first) + " /Parent " + std::to_string(racineSignets) + " 0 R /Dest [" + std::to_string(pages[i]) + " 0 R /Fit]" +
                          (i > 0 ? " /Prev " + std::to_string(items[i - 1]) + " 0 R" : std::string()) + (i + 1 < pages.size() ? " /Next " + std::to_string(items[i + 1]) + " 0 R" : std::string()) + " >>");
  w.objet(racineSignets, "<< /Type /Outlines /First " + std::to_string(items.front()) + " 0 R /Last " + std::to_string(items.back()) + " 0 R /Count " + std::to_string(items.size()) + " >>");
  const MetaPdf meta = metaDepuisCartouche(c);
  std::string cat = "<< /Type /Catalog /Pages " + std::to_string(arbre) + " 0 R /Outlines " + std::to_string(racineSignets) + " 0 R /PageMode /UseOutlines";
  if (o.metaDonnees) {
    const int x = w.reserver();
    w.flux(x, "/Type /Metadata /Subtype /XML", xmpPdf(meta));
    cat += " /Metadata " + std::to_string(x) + " 0 R";
  }
  w.objet(catalogue, cat + " >>");
  w.objet(info, "<< /Title " + texteUnicode(meta.titre) + " /Author " + texteUnicode(meta.auteur) + " /Creator " + texteUnicode("Prométhée") + " /Producer " + texteUnicode("Prométhée") + " >>");
  return w.terminer(catalogue, info);
}

// ---------- Documents de texte ----------
namespace {
// Chasses de l'Helvetica (millièmes du corps), de l'espace (32) au tilde (126)
constexpr short CHASSE_HELVETICA[95] = {278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556,
                                        556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,
                                        667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556,
                                        556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584};
double largeurTexte(const std::string& t, double corps, bool gras) {
  double l = 0;
  for (const char32_t cp : utf32(t)) {
    if (cp >= 32 && cp <= 126) l += CHASSE_HELVETICA[cp - 32];
    else if (cp >= 0xC0 && cp <= 0xDE) l += 667;
    else l += 556;
  }
  return l * corps / 1000 * (gras ? 1.07 : 1.0);
}
// Lignes d'un texte coupé à « largeur » points (mots entiers ; un mot trop long est coupé)
std::vector<std::string> couper(const std::string& texte, double largeur, double corps, bool gras) {
  std::vector<std::string> lignes;
  std::size_t debut = 0;
  while (debut <= texte.size()) {
    const std::size_t fin = texte.find('\n', debut);
    const std::string para = texte.substr(debut, fin == std::string::npos ? std::string::npos : fin - debut);
    std::string ligne;
    std::size_t i = 0;
    bool vide = true;
    while (i < para.size()) {
      std::size_t j = para.find(' ', i);
      if (j == std::string::npos) j = para.size();
      std::string mot = para.substr(i, j - i);
      i = j + 1;
      const std::string essai = ligne.empty() ? mot : ligne + " " + mot;
      if (largeurTexte(essai, corps, gras) <= largeur || ligne.empty()) {
        ligne = essai;
        // Mot seul plus large que la ligne : coupé à la largeur
        while (largeurTexte(ligne, corps, gras) > largeur && ligne.size() > 1) {
          std::size_t k = ligne.size();
          while (k > 1 && largeurTexte(ligne.substr(0, k), corps, gras) > largeur) {
            --k;
            while (k > 0 && (static_cast<unsigned char>(ligne[k]) & 0xC0) == 0x80) --k;
          }
          if (k == 0) break;
          lignes.push_back(ligne.substr(0, k));
          ligne = ligne.substr(k);
        }
      } else {
        lignes.push_back(ligne);
        ligne = mot;
      }
      vide = false;
    }
    if (!ligne.empty() || vide) lignes.push_back(ligne);
    if (fin == std::string::npos) break;
    debut = fin + 1;
  }
  return lignes;
}
}  // namespace

std::string pdfDocument(const DocumentPdf& d) {
  const double W = 595.28, H = 841.89, marge = 56, bas = 60, largeur = W - 2 * marge;
  std::vector<std::string> pages(1);
  double y = H - marge;
  auto nouvellePage = [&] {
    pages.emplace_back();
    y = H - marge;
  };
  auto texte = [&](double x, double yy, const std::string& t, double corps, bool gras) {
    pages.back() += "BT /" + std::string(gras ? "F2 " : "F1 ") + nb(corps) + " Tf " + nb(x) + " " + nb(yy) + " Td " + litteral(t) + " Tj ET\n";
  };
  for (const BlocPdf& b : d.blocs) {
    switch (b.genre) {
      case BlocPdf::Genre::SautDePage: nouvellePage(); break;
      case BlocPdf::Genre::Espace: y -= 8; break;
      case BlocPdf::Genre::Titre:
      case BlocPdf::Genre::SousTitre:
      case BlocPdf::Genre::Paragraphe: {
        const double corps = b.genre == BlocPdf::Genre::Titre ? 17 : b.genre == BlocPdf::Genre::SousTitre ? 12.5 : 10;
        const bool gras = b.gras || b.genre != BlocPdf::Genre::Paragraphe;
        const double pas = corps * 1.32;
        if (b.genre != BlocPdf::Genre::Paragraphe) y -= corps * 0.5;
        for (const auto& l : couper(b.texte, largeur, corps, gras)) {
          if (y - pas < bas) nouvellePage();
          y -= pas;
          texte(marge, y, l, corps, gras);
        }
        y -= b.genre == BlocPdf::Genre::Titre ? 6 : 4;
        break;
      }
      case BlocPdf::Genre::Tableau: {
        if (b.lignes.empty()) break;
        const std::size_t n = b.lignes.front().size();
        if (n == 0) break;
        std::vector<double> parts = b.largeurs;
        parts.resize(n, parts.empty() ? 1.0 : 0.0);
        double somme = 0;
        for (double& x : parts) somme += x = x > 0 ? x : 1.0;
        std::vector<double> col(n);
        for (std::size_t c = 0; c < n; ++c) col[c] = largeur * parts[c] / somme;
        const double corps = 8.5, pas = corps * 1.3, marge4 = 3;
        auto rangee = [&](const std::vector<std::string>& cellules, bool entete) {
          std::vector<std::vector<std::string>> lignes(n);
          std::size_t haut = 1;
          for (std::size_t c = 0; c < n; ++c) {
            lignes[c] = couper(c < cellules.size() ? cellules[c] : std::string(), col[c] - 2 * marge4, corps, entete);
            haut = std::max(haut, lignes[c].size());
          }
          const double h = static_cast<double>(haut) * pas + 2 * marge4;
          if (y - h < bas) {
            nouvellePage();
            return false;
          }
          if (entete) pages.back() += "0.92 g " + nb(marge) + " " + nb(y - h) + " " + nb(largeur) + " " + nb(h) + " re f 0 g\n";
          pages.back() += "0.6 G 0.4 w " + nb(marge) + " " + nb(y - h) + " " + nb(largeur) + " " + nb(h) + " re S 0 G\n";
          double x = marge;
          for (std::size_t c = 0; c < n; ++c) {
            for (std::size_t k = 0; k < lignes[c].size(); ++k) texte(x + marge4, y - marge4 - static_cast<double>(k + 1) * pas + 2, lignes[c][k], corps, entete);
            x += col[c];
          }
          y -= h;
          return true;
        };
        y -= 4;
        if (!rangee(b.lignes.front(), true)) rangee(b.lignes.front(), true);
        for (std::size_t r = 1; r < b.lignes.size(); ++r)
          if (!rangee(b.lignes[r], false)) {
            rangee(b.lignes.front(), true);  // en-tête répété en haut de la page suivante
            rangee(b.lignes[r], false);
          }
        y -= 8;
        break;
      }
    }
  }
  EcrivainPdf w;
  const int catalogue = w.reserver(), arbre = w.reserver(), f1 = w.reserver(), f2 = w.reserver(), info = w.reserver(), meta = w.reserver();
  w.objet(f1, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
  w.objet(f2, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
  std::vector<int> objetsPages;
  for (std::size_t i = 0; i < pages.size(); ++i) {
    std::string flux = pages[i];
    const std::string pied = (d.pied.empty() ? std::string() : d.pied + " — ") + "page " + std::to_string(i + 1) + " / " + std::to_string(pages.size());
    flux += "BT /F1 7.5 Tf " + nb(marge) + " 30 Td " + litteral(pied) + " Tj ET\n";
    const int contenu = w.reserver(), page = w.reserver();
    w.flux(contenu, "", flux);
    w.objet(page, "<< /Type /Page /Parent " + std::to_string(arbre) + " 0 R /MediaBox [0 0 " + nb(W) + " " + nb(H) + "] /Resources << /Font << /F1 " + std::to_string(f1) +
                      " 0 R /F2 " + std::to_string(f2) + " 0 R >> >> /Contents " + std::to_string(contenu) + " 0 R >>");
    objetsPages.push_back(page);
  }
  std::string kids;
  for (int p : objetsPages) kids += std::to_string(p) + " 0 R ";
  w.objet(arbre, "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(objetsPages.size()) + " >>");
  w.flux(meta, "/Type /Metadata /Subtype /XML", xmpPdf(d.meta));
  w.objet(catalogue, "<< /Type /Catalog /Pages " + std::to_string(arbre) + " 0 R /Metadata " + std::to_string(meta) + " 0 R >>");
  w.objet(info, "<< /Title " + texteUnicode(d.meta.titre) + " /Author " + texteUnicode(d.meta.auteur) + " /Subject " + texteUnicode(d.meta.sujet) + " /Keywords " +
                    texteUnicode(d.meta.motsCles) + " /Creator " + texteUnicode("Prométhée") + " /Producer " + texteUnicode("Prométhée") + " >>");
  return w.terminer(catalogue, info);
}

std::string textePdfSimple(const std::string& pdf) {
  std::string r;
  for (std::size_t i = pdf.find(" Td ("); i != std::string::npos; i = pdf.find(" Td (", i + 1)) {
    std::size_t j = i + 5;
    std::string t;
    for (; j < pdf.size() && pdf[j] != ')'; ++j) {
      if (pdf[j] == '\\' && j + 1 < pdf.size()) {
        ++j;
        t += pdf[j] == 'n' ? '\n' : pdf[j];
        continue;
      }
      t += pdf[j];
    }
    if (pdf.compare(j, 4, ") Tj") != 0) continue;
    // Latin-1 → UTF-8
    for (const char ch : t) {
      const auto c = static_cast<unsigned char>(ch);
      if (c < 0x80) r += static_cast<char>(c);
      else {
        r += static_cast<char>(0xC0 | (c >> 6));
        r += static_cast<char>(0x80 | (c & 0x3F));
      }
    }
    r += '\n';
  }
  return r;
}

// ---------- Schéma en PDF ----------
std::string pdfSchema(const ConceptionElec& c, const OptionsPdfSchema& o) {
  EcrivainPdf w;
  const int catalogue = w.reserver(), arbre = w.reserver(), f1 = w.reserver(), info = w.reserver();
  w.objet(f1, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
  std::vector<std::size_t> feuilles = o.feuilles;
  if (feuilles.empty())
    for (std::size_t i = 0; i < c.feuilles.size(); ++i) feuilles.push_back(i);
  const std::string encreFil = o.couleurs ? "0 0.45 0.1" : "0 0 0", encreSymbole = o.couleurs ? "0.55 0.1 0.1" : "0 0 0", encreTexte = o.couleurs ? "0.1 0.2 0.5" : "0 0 0";
  std::vector<int> pages;
  int numero = 0;
  for (const std::size_t fi : feuilles) {
    if (fi >= c.feuilles.size()) continue;
    ++numero;
    const Feuille& F = c.feuilles[fi];
    Boite cadre = cadreFeuille(F.format);
    if (F.format == "libre" && F.largeurLibre > 0 && F.hauteurLibre > 0) cadre = {0, 0, F.largeurLibre, F.hauteurLibre};
    const double Wmm = mm(cadre.largeur()), Hmm = mm(cadre.hauteur());
    auto X = [&](Nm x) { return (mm(x) - mm(cadre.x0)) * PT_PAR_MM; };
    auto Y = [&](Nm y) { return (Hmm - (mm(y) - mm(cadre.y0))) * PT_PAR_MM; };
    auto P = [&](Pt q) { return nb(X(q.x)) + " " + nb(Y(q.y)); };
    std::string s = "1 J 1 j\n";
    auto texte = [&](Pt q, const std::string& t, double tailleMm, const std::string& encre, double dx = 0, double dy = 0) {
      if (t.empty()) return;
      s += encre + " rg BT /F1 " + nb(std::max(4.0, tailleMm * PT_PAR_MM)) + " Tf " + nb(X(q.x) + dx) + " " + nb(Y(q.y) + dy) + " Td " + litteral(t) + " Tj ET\n";
    };
    auto ligne = [&](const std::vector<Pt>& pts, bool fermer, bool remplir, const std::string& encre, double epaisseurMm) {
      if (pts.size() < 2) return;
      s += encre + " RG " + encre + " rg " + nb(epaisseurMm * PT_PAR_MM) + " w " + P(pts[0]) + " m";
      for (std::size_t k = 1; k < pts.size(); ++k) s += " " + P(pts[k]) + " l";
      s += fermer || remplir ? " h" : "";
      s += remplir ? " B\n" : " S\n";
    };
    // Cadre et cartouche
    const Nm marge = 5 * NM_PAR_MM;
    ligne({{cadre.x0 + marge, cadre.y0 + marge}, {cadre.x1 - marge, cadre.y0 + marge}, {cadre.x1 - marge, cadre.y1 - marge}, {cadre.x0 + marge, cadre.y1 - marge}}, true, false, "0 0 0", 0.3);
    {
      const Nm L = 110 * NM_PAR_MM, Hc = 24 * NM_PAR_MM;
      const Pt a{cadre.x1 - marge - L, cadre.y1 - marge - Hc}, b{cadre.x1 - marge, cadre.y1 - marge};
      ligne({a, {b.x, a.y}, b, {a.x, b.y}}, true, false, "0 0 0", 0.3);
      ligne({{a.x, a.y + Hc / 3}, {b.x, a.y + Hc / 3}}, false, false, "0 0 0", 0.2);
      ligne({{a.x, a.y + 2 * Hc / 3}, {b.x, a.y + 2 * Hc / 3}}, false, false, "0 0 0", 0.2);
      const Cartouche& k = c.cartouche;
      texte({a.x + NM_PAR_MM * 2, a.y + Hc / 3 - NM_PAR_MM * 2}, k.titre.empty() ? c.nom : k.titre, 3.2, "0 0 0");
      texte({a.x + NM_PAR_MM * 2, a.y + 2 * Hc / 3 - NM_PAR_MM * 2}, (k.societe.empty() ? std::string() : k.societe + " — ") + (k.auteur.empty() ? std::string() : k.auteur), 2.2, "0 0 0");
      texte({a.x + NM_PAR_MM * 2, b.y - NM_PAR_MM * 2}, "Révision " + k.revision + (k.date.empty() ? std::string() : "  ·  " + k.date) + "  ·  " + F.nom + "  ·  feuille " + std::to_string(numero) + " / " + std::to_string(feuilles.size()), 2.2, "0 0 0");
    }
    // Fils et jonctions
    for (const auto& f : F.fils) ligne({f.a, f.b}, false, false, encreFil, 0.25);
    for (const auto& j : F.jonctions) ligne(cercle(j.p, NM_PAR_MM / 2), true, true, encreFil, 0.1);
    for (const auto& n : F.nonConnexions) {
      const Nm d = NM_PAR_MM;
      ligne({{n.p.x - d, n.p.y - d}, {n.p.x + d, n.p.y + d}}, false, false, "0 0 0.8", 0.25);
      ligne({{n.p.x - d, n.p.y + d}, {n.p.x + d, n.p.y - d}}, false, false, "0 0 0.8", 0.25);
    }
    // Symboles
    for (const auto& sp : F.symboles) {
      const DefSymbole* d = c.symbole(sp.symbole);
      if (!d) continue;
      for (const auto& t : d->traces) {
        if (t.unite != 0 && t.unite != sp.unite) continue;
        if (t.vue != 0 && t.vue != sp.vue) continue;
        if (t.forme == Trace::Forme::Texte) {
          if (!t.points.empty()) texte(transformer(sp, t.points[0]), t.texte, mm(t.taille), encreSymbole);
          continue;
        }
        if (t.forme == Trace::Forme::Image) continue;
        std::vector<Pt> pts;
        for (const Pt q : contourTrace(t)) pts.push_back(transformer(sp, q));
        const bool fermee = t.ferme || t.forme == Trace::Forme::Rectangle || t.forme == Trace::Forme::Cercle || t.forme == Trace::Forme::Ellipse || t.forme == Trace::Forme::CadreTexte;
        ligne(pts, fermee, t.rempli, encreSymbole, t.epaisseur > 0 ? mm(t.epaisseur) : 0.25);
        if (t.forme == Trace::Forme::CadreTexte && !t.points.empty()) texte(transformer(sp, t.points[0]), t.texte, mm(t.taille), encreSymbole, 2, -10);
      }
      for (const auto& b : brochesPlacees(sp, *d)) {
        if (b.broche->unite != 0 && b.broche->unite != sp.unite) continue;
        const Pt dir = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
        const Pt fin{b.p.x + dir.x * b.broche->longueur, b.p.y + dir.y * b.broche->longueur};
        ligne({b.p, fin}, false, false, encreSymbole, 0.2);
        if (d->alimentation) continue;
        const auto it = sp.numeros.find(b.broche->numero);
        const std::string numeroBroche = it == sp.numeros.end() ? b.broche->numero : it->second;
        if (b.broche->numeroVisible && d->broches.size() > 2) texte({(b.p.x + fin.x) / 2, (b.p.y + fin.y) / 2}, numeroBroche, 1.1, encreSymbole, -2, 1.5);
        if (b.broche->nomVisible && !sp.nomsMasques && b.broche->nom != b.broche->numero && d->broches.size() > 2 && b.broche->nom.rfind("Pin_", 0) != 0)
          texte(fin, b.broche->nom, 1.2, encreSymbole, dir.x > 0 ? 2 : dir.x < 0 ? -2 - static_cast<double>(b.broche->nom.size()) * 2.6 : -3, dir.y == 0 ? -1.5 : (dir.y > 0 ? -4 : 3));
      }
      const ComposantElec* k = sp.composant.empty() ? nullptr : c.composant(sp.composant);
      if (d->alimentation) {
        texte(ancrageChamp(sp, *d, false), sp.nomAlim.empty() ? d->nom : sp.nomAlim, 1.4, encreTexte, -4, 2);
        continue;
      }
      if (k) {
        const bool plusieurs = std::any_of(d->broches.begin(), d->broches.end(), [](const Broche& x) { return x.unite > 1; });
        texte(ancrageChamp(sp, *d, false), k->reference + (plusieurs ? lettreUnite(sp.unite) : std::string()), 1.5, encreTexte);
        texte(ancrageChamp(sp, *d, true), k->valeur, 1.3, encreTexte);
      }
    }
    // Étiquettes et textes
    for (const auto& e : F.etiquettes) {
      if (e.type != TypeEtiquette::Locale) {
        const Nm h = 1'200'000, l = static_cast<Nm>(e.texte.size() + 2) * 1'000'000;
        ligne({e.p, {e.p.x + h, e.p.y - h}, {e.p.x + h + l, e.p.y - h}, {e.p.x + h + l, e.p.y + h}, {e.p.x + h, e.p.y + h}}, true, false, encreTexte, 0.2);
        texte(e.p, e.texte, 1.4, encreTexte, h / 1e6 * PT_PAR_MM + 3, -1.5);
      } else
        texte(e.p, e.texte, 1.4, encreTexte, 0, 2);
    }
    for (const auto& t : F.textes) {
      std::size_t debut = 0;
      int rang = 0;
      while (debut <= t.texte.size()) {
        const std::size_t fin = t.texte.find('\n', debut);
        texte(t.p, t.texte.substr(debut, fin == std::string::npos ? std::string::npos : fin - debut), mm(t.taille), "0 0 0", 0, -rang * mm(t.taille) * 1.3 * PT_PAR_MM);
        ++rang;
        if (fin == std::string::npos) break;
        debut = fin + 1;
      }
    }
    const int contenu = w.reserver(), page = w.reserver();
    w.flux(contenu, "", s);
    w.objet(page, "<< /Type /Page /Parent " + std::to_string(arbre) + " 0 R /MediaBox [0 0 " + nb(Wmm * PT_PAR_MM) + " " + nb(Hmm * PT_PAR_MM) + "] /Resources << /Font << /F1 " +
                      std::to_string(f1) + " 0 R >> >> /Contents " + std::to_string(contenu) + " 0 R >>");
    pages.push_back(page);
  }
  std::string kids;
  for (int p : pages) kids += std::to_string(p) + " 0 R ";
  w.objet(arbre, "<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages.size()) + " >>");
  w.objet(catalogue, "<< /Type /Catalog /Pages " + std::to_string(arbre) + " 0 R >>");
  const MetaPdf meta = metaDepuisCartouche(c);
  w.objet(info, "<< /Title " + texteUnicode(meta.titre.empty() ? c.nom : meta.titre) + " /Author " + texteUnicode(meta.auteur) + " /Subject " + texteUnicode("Schéma") +
                    " /Creator " + texteUnicode("Prométhée") + " /Producer " + texteUnicode("Prométhée") + " >>");
  return w.terminer(catalogue, info);
}

}  // namespace prom::ecad
