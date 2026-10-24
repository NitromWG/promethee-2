// Prométhée : nomenclature mécanique en tableau, CSV et XLSX (M10). Licence GPL-3.0-only.
#include "promethee/csv.hpp"
#include "promethee/nomenclature_meca.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "promethee/archive.hpp"
#include "promethee/nomenclature.hpp"
#include "promethee/piece.hpp"
#include "promethee/structure.hpp"

namespace prom {

namespace {

std::string decimal(double v, int chiffres) {
  char t[64];
  std::snprintf(t, sizeof t, "%.*f", chiffres, v);
  std::string s = t;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}

CelluleNomenclature texte(const std::string& t) { return {t, 0, false}; }
CelluleNomenclature entier(int n) { return {std::to_string(n), static_cast<double>(n), true}; }
CelluleNomenclature grammes(double g) { return {decimal(g, 1), std::round(g * 10) / 10, true}; }

std::string echapperXml(const std::string& s) {
  std::string r;
  r.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      default:
        // Caractères de contrôle interdits en XML 1.0 (hors tabulation et fins de ligne)
        if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;
        r += c;
    }
  }
  return r;
}

std::string lettresColonne(std::size_t i) {
  std::string r;
  ++i;
  while (i > 0) {
    const std::size_t k = (i - 1) % 26;
    r.insert(r.begin(), static_cast<char>('A' + k));
    i = (i - 1) / 26;
  }
  return r;
}

}  // namespace

std::string texteMasse(double g) {
  if (std::abs(g) >= 1000) return decimal(g / 1000, 3) + " kg";
  if (std::abs(g) >= 100) return decimal(g, 0) + " g";
  return decimal(g, 1) + " g";
}

std::string TableauNomenclature::texteTotal() const {
  std::string t = std::to_string(quantitePieces) + (quantitePieces > 1 ? " pièces" : " pièce");
  if (masseTotale > 0 || !masseComplete) {
    t += " · " + texteMasse(masseTotale);
    if (!masseComplete) t += " (incomplète : pièce sans volume)";
    else if (masseProvisoire) t += " (provisoire : densité à confirmer)";
  }
  return t;
}

std::vector<ColonneNomenclature> colonnesNomenclature(const Projet& p) {
  std::vector<ColonneNomenclature> c = {
      {"repere", "Rep.", true},           {"numero", "N°", false},           {"niveau", "Niveau", true},
      {"quantite", "Qté", true},          {"quantiteTotale", "Qté totale", true}, {"reference", "Référence", false},
      {"designation", "Désignation", false}, {"materiau", "Matière", false},  {"fournisseur", "Fournisseur", false},
      {"masseUnitaire", "Masse unitaire (g)", true}, {"masseTotale", "Masse totale (g)", true}};
  std::vector<std::string> vues;
  for (const auto& j : p.pieces) {
    if (!j.is_object() || !j.contains("proprietes") || !j["proprietes"].is_array()) continue;
    for (const auto& v : j["proprietes"])
      if (v.is_array() && v.size() == 2 && v[0].is_string()) {
        const std::string n = v[0].get<std::string>();
        if (!n.empty() && std::find(vues.begin(), vues.end(), n) == vues.end()) vues.push_back(n);
      }
  }
  for (const auto& n : vues) c.push_back({"prop:" + n, n, false});
  return c;
}

std::vector<std::string> colonnesParDefaut(VueNomenclature vue) {
  switch (vue) {
    case VueNomenclature::Niveaux: return {"numero", "repere", "quantite", "quantiteTotale", "reference", "designation", "materiau", "masseUnitaire", "masseTotale"};
    case VueNomenclature::PremierNiveau: return {"repere", "quantite", "reference", "designation", "materiau", "masseUnitaire", "masseTotale"};
    default: return {"repere", "quantite", "reference", "designation", "materiau", "fournisseur", "masseUnitaire", "masseTotale"};
  }
}

TableauNomenclature tableauNomenclature(const Projet& p, int assemblage, VueNomenclature vue, const std::vector<std::string>& colonnes,
                                        const std::function<MassePiece(int)>& masse) {
  TableauNomenclature t;
  t.titre = rangAssemblageValide(p, assemblage) ? p.assemblages[static_cast<std::size_t>(assemblage)].nom : p.nom;
  const auto toutes = colonnesNomenclature(p);
  for (const auto& cle : colonnes) {
    const auto it = std::find_if(toutes.begin(), toutes.end(), [&](const ColonneNomenclature& c) { return c.cle == cle; });
    if (it != toutes.end()) t.colonnes.push_back(*it);
    else if (cle.rfind("prop:", 0) == 0) t.colonnes.push_back({cle, cle.substr(5), false});
  }
  // Pièces lues une fois, masses une fois
  std::map<int, Piece> pieces;
  auto piece = [&](int i) -> const Piece& {
    auto it = pieces.find(i);
    if (it == pieces.end()) it = pieces.emplace(i, rangPieceValide(p, i) ? pieceDepuisJson(p.pieces[static_cast<std::size_t>(i)]) : Piece{}).first;
    return it->second;
  };
  std::map<int, MassePiece> masses;
  auto massePiece = [&](int i) -> MassePiece {
    if (!masse) return {};
    auto it = masses.find(i);
    if (it == masses.end()) it = masses.emplace(i, masse(i)).first;
    return it->second;
  };
  // Masse d'un sous-assemblage : ses pièces, sous-assemblages développés
  auto masseAssemblage = [&](int a, bool& connue) {
    double g = 0;
    connue = true;
    for (const auto& [ip, q] : quantitesPieces(p, a)) {
      const MassePiece m = massePiece(ip);
      connue = connue && m.connue;
      g += q * m.grammes;
    }
    return g;
  };

  struct Article { bool assemblage; int indice; bool symetrique; int quantite, quantiteTotale, niveau; std::string numero; };
  std::vector<Article> articles;
  if (vue == VueNomenclature::Pieces || !rangAssemblageValide(p, assemblage)) {
    for (const auto& l : nomenclatureMecanique(p, assemblage)) articles.push_back({false, l.piece, l.symetrique, l.quantite, l.quantite, 0, {}});
    std::stable_sort(articles.begin(), articles.end(), [&](const Article& a, const Article& b) {
      return repereArticle(p, false, a.indice, a.symetrique) < repereArticle(p, false, b.indice, b.symetrique);
    });
  } else {
    for (const auto& l : nomenclatureNiveaux(p, assemblage)) {
      if (vue == VueNomenclature::PremierNiveau && l.niveau > 0) continue;
      articles.push_back({l.assemblage, l.indice, l.symetrique, l.quantite, l.quantiteTotale, l.niveau, l.numero});
    }
  }
  for (const auto& a : articles) {
    std::vector<CelluleNomenclature> ligne;
    const Piece* pc = a.assemblage ? nullptr : &piece(a.indice);
    double unitaire = 0;
    bool connue = false;
    if (masse) {
      if (a.assemblage) unitaire = masseAssemblage(a.indice, connue);
      else { const MassePiece m = massePiece(a.indice); unitaire = m.grammes; connue = m.connue; }
    }
    const int qteLigne = vue == VueNomenclature::Niveaux ? a.quantiteTotale : a.quantite;
    for (const auto& c : t.colonnes) {
      const std::string& k = c.cle;
      if (k == "repere") { const int r = repereArticle(p, a.assemblage, a.indice, a.symetrique); ligne.push_back(r > 0 ? entier(r) : texte("")); }
      else if (k == "numero") ligne.push_back(texte(a.numero));
      else if (k == "niveau") ligne.push_back(entier(a.niveau + 1));
      else if (k == "quantite") ligne.push_back(entier(a.quantite));
      else if (k == "quantiteTotale") ligne.push_back(entier(a.quantiteTotale));
      else if (k == "reference") {
        std::string r = pc ? (pc->reference.empty() ? std::string() : pc->reference + (a.symetrique ? "-S" : "")) : std::string();
        ligne.push_back(texte(r));
      } else if (k == "designation") {
        const std::string n = a.assemblage ? p.assemblages[static_cast<std::size_t>(a.indice)].nom + (a.symetrique ? " (symétrique)" : "")
                                           : pc->nom + (a.symetrique ? " (symétrique)" : "");
        ligne.push_back(texte(n));
      } else if (k == "materiau") ligne.push_back(texte(pc ? pc->materiau : std::string()));
      else if (k == "fournisseur") ligne.push_back(texte(pc ? pc->fournisseur : std::string()));
      else if (k == "masseUnitaire") ligne.push_back(masse && connue ? grammes(unitaire) : texte(""));
      else if (k == "masseTotale") ligne.push_back(masse && connue ? grammes(unitaire * qteLigne) : texte(""));
      else if (k.rfind("prop:", 0) == 0) ligne.push_back(texte(pc ? pc->propriete(k.substr(5)) : std::string()));
      else ligne.push_back(texte(""));
    }
    t.lignes.push_back(std::move(ligne));
    t.niveaux.push_back(vue == VueNomenclature::Niveaux ? a.niveau : 0);
    t.assemblages.push_back(a.assemblage ? 1 : 0);
  }
  // Total : toutes les pièces, sous-assemblages développés
  for (const auto& l : nomenclatureMecanique(p, assemblage)) {
    t.quantitePieces += l.quantite;
    if (!masse) continue;
    const MassePiece m = massePiece(l.piece);
    t.masseComplete = t.masseComplete && m.connue;
    t.masseProvisoire = t.masseProvisoire || m.provisoire;
    t.masseTotale += l.quantite * m.grammes;
  }
  if (!masse) t.masseComplete = true;
  return t;
}

std::string csvNomenclature(const TableauNomenclature& t) {
  EcrivainCsv w;  // écrivain du poste (10.20)
  for (const auto& col : t.colonnes) w.champ(col.titre);
  w.fin();
  for (std::size_t i = 0; i < t.lignes.size(); ++i) {
    for (std::size_t c = 0; c < t.lignes[i].size(); ++c) {
      std::string v = t.lignes[i][c].texte;
      // Retrait des niveaux dans la désignation, lisible sans mise en forme
      if (t.colonnes[c].cle == "designation" && i < t.niveaux.size()) v = std::string(static_cast<std::size_t>(2 * t.niveaux[i]), ' ') + v;
      w.champ(v);
    }
    w.fin();
  }
  w.champ("Total : " + t.texteTotal()).fin();
  return w.texte();
}

std::string xlsxNomenclature(const TableauNomenclature& t) {
  // Styles : 0 ordinaire ; 1 en-tête (gras, fond gris, bordure basse) ; 2 à 8 retrait 0 à 6 ; 9 à 15 retrait 0 à 6
  // en gras (sous-assemblages) ; 16 nombre « 0.0 » ; 17 nombre « 0.0 » gras ; 18 texte gras (total)
  std::string styles =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
      "<numFmts count=\"1\"><numFmt numFmtId=\"164\" formatCode=\"0.0\"/></numFmts>"
      "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font><font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
      "<fills count=\"3\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill>"
      "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFE4E8EE\"/><bgColor indexed=\"64\"/></patternFill></fill></fills>"
      "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
      "<border><left/><right/><top/><bottom style=\"thin\"><color rgb=\"FF8A93A8\"/></bottom><diagonal/></border></borders>"
      "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs><cellXfs count=\"19\">"
      "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
      "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\"/>";
  for (int gras = 0; gras < 2; ++gras)
    for (int k = 0; k <= 6; ++k)
      styles += "<xf numFmtId=\"0\" fontId=\"" + std::to_string(gras) + "\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyAlignment=\"1\"><alignment indent=\"" +
                std::to_string(k) + "\"/></xf>";
  styles +=
      "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
      "<xf numFmtId=\"164\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\" applyFont=\"1\"/>"
      "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
      "</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>";

  auto cellule = [](const std::string& ref, const CelluleNomenclature& c, int style) {
    const std::string s = style ? " s=\"" + std::to_string(style) + "\"" : std::string();
    if (c.estNombre) {
      char v[64];
      std::snprintf(v, sizeof v, "%.10g", c.nombre);
      return "<c r=\"" + ref + "\"" + s + "><v>" + v + "</v></c>";
    }
    if (c.texte.empty()) return std::string();
    return "<c r=\"" + ref + "\"" + s + " t=\"inlineStr\"><is><t xml:space=\"preserve\">" + echapperXml(c.texte) + "</t></is></c>";
  };
  // Largeurs de colonnes d'après le plus long texte (caractères UTF-8 comptés une fois)
  std::vector<double> largeurs(t.colonnes.size(), 6);
  auto longueur = [](const std::string& s) { std::size_t n = 0; for (unsigned char c : s) n += (c & 0xC0) != 0x80; return static_cast<double>(n); };
  for (std::size_t c = 0; c < t.colonnes.size(); ++c) {
    largeurs[c] = std::max(largeurs[c], longueur(t.colonnes[c].titre) + 2);
    for (std::size_t i = 0; i < t.lignes.size(); ++i) {
      const double retrait = t.colonnes[c].cle == "designation" ? 3.0 * t.niveaux[i] : 0;
      largeurs[c] = std::max(largeurs[c], longueur(t.lignes[i][c].texte) + 2 + retrait);
    }
    largeurs[c] = std::min(largeurs[c], 60.0);
  }
  std::string feuille =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
  const std::size_t nLignes = t.lignes.size() + 2;
  if (!t.colonnes.empty()) feuille += "<dimension ref=\"A1:" + lettresColonne(t.colonnes.size() - 1) + std::to_string(nLignes) + "\"/>";
  feuille += "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>";
  feuille += "<sheetFormatPr defaultRowHeight=\"15\"/>";
  if (!t.colonnes.empty()) {
    feuille += "<cols>";
    for (std::size_t c = 0; c < t.colonnes.size(); ++c) {
      char w[32];
      std::snprintf(w, sizeof w, "%.1f", largeurs[c]);
      feuille += "<col min=\"" + std::to_string(c + 1) + "\" max=\"" + std::to_string(c + 1) + "\" width=\"" + w + "\" customWidth=\"1\"/>";
    }
    feuille += "</cols>";
  }
  feuille += "<sheetData><row r=\"1\">";
  for (std::size_t c = 0; c < t.colonnes.size(); ++c) feuille += cellule(lettresColonne(c) + "1", texte(t.colonnes[c].titre), 1);
  feuille += "</row>";
  for (std::size_t i = 0; i < t.lignes.size(); ++i) {
    const std::string r = std::to_string(i + 2);
    const bool gras = i < t.assemblages.size() && t.assemblages[i];
    feuille += "<row r=\"" + r + "\">";
    for (std::size_t c = 0; c < t.lignes[i].size(); ++c) {
      const auto& x = t.lignes[i][c];
      int style = gras ? 9 : 0;
      if (t.colonnes[c].cle == "designation") style = (gras ? 9 : 2) + std::min(6, t.niveaux[i]);
      else if (x.estNombre && (t.colonnes[c].cle == "masseUnitaire" || t.colonnes[c].cle == "masseTotale")) style = gras ? 17 : 16;
      feuille += cellule(lettresColonne(c) + r, x, style);
    }
    feuille += "</row>";
  }
  // Total : libellé dans la première colonne, quantité et masse dans leurs colonnes si elles sont montrées
  {
    const std::string r = std::to_string(t.lignes.size() + 2);
    feuille += "<row r=\"" + r + "\">";
    for (std::size_t c = 0; c < t.colonnes.size(); ++c) {
      const std::string& k = t.colonnes[c].cle;
      if (c == 0) feuille += cellule("A" + r, texte("Total"), 18);
      else if (k == "quantiteTotale" || (k == "quantite" && std::none_of(t.colonnes.begin(), t.colonnes.end(), [](const ColonneNomenclature& x) { return x.cle == "quantiteTotale"; })))
        feuille += cellule(lettresColonne(c) + r, entier(t.quantitePieces), 18);
      else if (k == "masseTotale" && t.masseTotale > 0) feuille += cellule(lettresColonne(c) + r, grammes(t.masseTotale), 17);
      else if (k == "designation") feuille += cellule(lettresColonne(c) + r, texte(t.texteTotal()), 18);
    }
    feuille += "</row>";
  }
  feuille += "</sheetData><pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/></worksheet>";

  // Nom de feuille : 31 caractères au plus, sans : \ / ? * [ ]
  std::string nom;
  std::size_t n = 0;
  for (std::size_t i = 0; i < t.titre.size() && n < 31; ++i) {
    const char c = t.titre[i];
    if (std::string(":\\/?*[]").find(c) != std::string::npos) continue;
    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
    if (n > 31) break;
    nom += c;
  }
  while (!nom.empty() && (static_cast<unsigned char>(nom.back()) & 0xC0) == 0xC0) nom.pop_back();  // octet de tête sans suite
  if (nom.empty()) nom = "Nomenclature";
  const std::string classeur =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
      "<sheets><sheet name=\"" + echapperXml(nom) + "\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
  const std::string types =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
      "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
      "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
      "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
      "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
      "</Types>";
  const std::string liens =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
      "</Relationships>";
  const std::string liensClasseur =
      "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
      "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
      "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" Target=\"styles.xml\"/>"
      "</Relationships>";
  return zipStocke({{"[Content_Types].xml", types},
                    {"_rels/.rels", liens},
                    {"xl/workbook.xml", classeur},
                    {"xl/_rels/workbook.xml.rels", liensClasseur},
                    {"xl/styles.xml", styles},
                    {"xl/worksheets/sheet1.xml", feuille}});
}

}  // namespace prom
