// Prométhée : classeur XLSX à plusieurs feuilles. Voir classeur.hpp. Licence GPL-3.0-only.
#include "promethee/classeur.hpp"

#include <algorithm>
#include <cstdio>

#include "promethee/archive.hpp"

namespace prom {

namespace {
std::string xml(const std::string& s) {
  std::string r;
  for (const char c : s) {
    switch (c) {
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '&': r += "&amp;"; break;
      case '"': r += "&quot;"; break;
      default:
        if (static_cast<unsigned char>(c) >= 0x20 || c == '\n' || c == '\t') r += c;
    }
  }
  return r;
}
std::string colonne(std::size_t c) {
  std::string s;
  for (std::size_t n = c + 1; n > 0; n = (n - 1) / 26) s.insert(s.begin(), static_cast<char>('A' + (n - 1) % 26));
  return s;
}
double longueurUtf8(const std::string& s) {
  std::size_t n = 0;
  for (const unsigned char c : s) n += (c & 0xC0) != 0x80;
  return static_cast<double>(n);
}
std::string cellule(const std::string& ref, const CelluleClasseur& c, bool gras) {
  const std::string s = gras ? " s=\"1\"" : "";
  if (c.estNombre) {
    char v[64];
    std::snprintf(v, sizeof v, "%.12g", c.nombre);
    return "<c r=\"" + ref + "\"" + s + "><v>" + v + "</v></c>";
  }
  if (c.texte.empty()) return {};
  return "<c r=\"" + ref + "\"" + s + " t=\"inlineStr\"><is><t xml:space=\"preserve\">" + xml(c.texte) + "</t></is></c>";
}
std::string feuilleXml(const FeuilleClasseur& f) {
  const std::size_t n = std::max<std::size_t>(f.entetes.size(), f.lignes.empty() ? 0 : std::max_element(f.lignes.begin(), f.lignes.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); })->size());
  std::vector<double> largeurs(n, 8);
  for (std::size_t c = 0; c < f.entetes.size(); ++c) largeurs[c] = std::max(largeurs[c], longueurUtf8(f.entetes[c]) + 2);
  for (const auto& l : f.lignes)
    for (std::size_t c = 0; c < l.size(); ++c) largeurs[c] = std::min(60.0, std::max(largeurs[c], (l[c].estNombre ? 10.0 : longueurUtf8(l[c].texte)) + 2));
  std::string x =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
  if (n > 0) x += "<dimension ref=\"A1:" + colonne(n - 1) + std::to_string(f.lignes.size() + 1) + "\"/>";
  x += "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>";
  x += "<sheetFormatPr defaultRowHeight=\"15\"/>";
  if (n > 0) {
    x += "<cols>";
    for (std::size_t c = 0; c < n; ++c) {
      char w[32];
      std::snprintf(w, sizeof w, "%.1f", largeurs[c]);
      x += "<col min=\"" + std::to_string(c + 1) + "\" max=\"" + std::to_string(c + 1) + "\" width=\"" + w + "\" customWidth=\"1\"/>";
    }
    x += "</cols>";
  }
  x += "<sheetData><row r=\"1\">";
  for (std::size_t c = 0; c < f.entetes.size(); ++c) x += cellule(colonne(c) + "1", CelluleClasseur(f.entetes[c]), true);
  x += "</row>";
  for (std::size_t i = 0; i < f.lignes.size(); ++i) {
    const std::string r = std::to_string(i + 2);
    x += "<row r=\"" + r + "\">";
    for (std::size_t c = 0; c < f.lignes[i].size(); ++c) x += cellule(colonne(c) + r, f.lignes[i][c], false);
    x += "</row>";
  }
  return x + "</sheetData></worksheet>";
}
}  // namespace

std::string nomFeuilleValide(const std::string& nom) {
  std::string r;
  for (const char c : nom) r += std::string("[]:*?/\\").find(c) == std::string::npos ? c : '-';
  if (r.empty()) r = "Feuille";
  // 31 caractères (et non octets) au plus
  std::string s;
  std::size_t n = 0;
  for (std::size_t i = 0; i < r.size(); ++i) {
    if ((static_cast<unsigned char>(r[i]) & 0xC0) != 0x80 && ++n > 31) break;
    s += r[i];
  }
  return s;
}

std::string xlsxClasseur(const std::vector<FeuilleClasseur>& feuilles) {
  const std::size_t n = std::max<std::size_t>(1, feuilles.size());
  std::string types =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
      "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
      "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
      "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
  for (std::size_t i = 0; i < n; ++i)
    types += "<Override PartName=\"/xl/worksheets/sheet" + std::to_string(i + 1) + ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
  types += "</Types>";
  const std::string racine =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>";
  std::string classeur =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>";
  std::string liens =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
  std::vector<std::string> noms;
  for (std::size_t i = 0; i < n; ++i) {
    std::string nom = nomFeuilleValide(i < feuilles.size() ? feuilles[i].nom : "Feuille");
    // Noms uniques : « Fils (2) »
    for (int k = 2; std::find(noms.begin(), noms.end(), nom) != noms.end(); ++k) nom = nomFeuilleValide(nom.substr(0, 26) + " (" + std::to_string(k) + ")");
    noms.push_back(nom);
    classeur += "<sheet name=\"" + xml(nom) + "\" sheetId=\"" + std::to_string(i + 1) + "\" r:id=\"rId" + std::to_string(i + 1) + "\"/>";
    liens += "<Relationship Id=\"rId" + std::to_string(i + 1) + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet" +
             std::to_string(i + 1) + ".xml\"/>";
  }
  classeur += "</sheets></workbook>";
  liens += "<Relationship Id=\"rId" + std::to_string(n + 1) + "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/></Relationships>";
  const std::string styles =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
      "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font><font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
      "<fills count=\"3\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
      "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE4E8EE\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
      "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
      "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
      "<cellXfs count=\"2\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
      "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\"/></cellXfs>"
      "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>";
  std::vector<std::pair<std::string, std::string>> fichiers = {
      {"[Content_Types].xml", types}, {"_rels/.rels", racine}, {"xl/workbook.xml", classeur}, {"xl/_rels/workbook.xml.rels", liens}, {"xl/styles.xml", styles}};
  for (std::size_t i = 0; i < n; ++i) fichiers.push_back({"xl/worksheets/sheet" + std::to_string(i + 1) + ".xml", i < feuilles.size() ? feuilleXml(feuilles[i]) : feuilleXml({})});
  return zipStocke(fichiers);
}

}  // namespace prom
