// Prométhée : dossier de plans de la carte (8.30). Voir plans_carte.hpp. Licence GPL-3.0-only.
#include "promethee/plans_carte.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>

#include "promethee/circuit_edition.hpp"
#include "promethee/fabrication_detaillee.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/lignes_transmission.hpp"
#include "promethee/nomenclature_meca.hpp"
#include "promethee/police.hpp"

namespace prom::ecad {

namespace {
std::string f2(double v, int d = 2) {
  char b[48];
  const auto r = std::to_chars(b, b + sizeof b, v, std::chars_format::fixed, d);
  return std::string(b, r.ptr);
}
std::string virgule(double v, int d = 2) {
  std::string s = f2(v, d);
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
std::string xml(const std::string& s) {
  std::string r;
  for (char ch : s) r += ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : ch == '&' ? "&amp;" : ch == '"' ? "&quot;" : std::string(1, ch);
  return r;
}
const char* codeGenreVue(VuePlan::Genre g) {
  switch (g) {
    case VuePlan::Genre::Couche: return "couche";
    case VuePlan::Genre::Cote: return "cote";
    case VuePlan::Genre::Empilage: return "empilage";
    case VuePlan::Genre::Detail: return "detail";
    case VuePlan::Genre::TableauPercage: return "percage";
    case VuePlan::Genre::TableauLignes: return "lignes";
    case VuePlan::Genre::Nomenclature: return "nomenclature";
    case VuePlan::Genre::Notes: return "notes";
  }
  return "couche";
}
VuePlan::Genre genreVueDepuis(const std::string& s) {
  for (auto g : {VuePlan::Genre::Couche, VuePlan::Genre::Cote, VuePlan::Genre::Empilage, VuePlan::Genre::Detail, VuePlan::Genre::TableauPercage, VuePlan::Genre::TableauLignes, VuePlan::Genre::Nomenclature,
                 VuePlan::Genre::Notes})
    if (s == codeGenreVue(g)) return g;
  return VuePlan::Genre::Couche;
}
}  // namespace

nlohmann::ordered_json dossierPlansVersJson(const DossierPlans& d) {
  nlohmann::ordered_json t = nlohmann::ordered_json::array();
  for (const auto& f : d.feuilles) {
    nlohmann::ordered_json vues = nlohmann::ordered_json::array(), notes = nlohmann::ordered_json::array();
    for (const auto& v : f.vues)
      vues.push_back({{"genre", codeGenreVue(v.genre)}, {"titre", v.titre}, {"couche", v.couche}, {"x", v.x}, {"y", v.y}, {"echelle", v.echelle},
                      {"centre", {mm(v.centre.x), mm(v.centre.y)}}, {"rayon", v.rayon}, {"circulaire", v.circulaire}});
    for (const auto& n : f.notes) notes.push_back({{"numero", n.numero}, {"texte", n.texte}});
    t.push_back({{"nom", f.nom}, {"format", f.format}, {"vues", vues}, {"notes", notes}});
  }
  return {{"feuilles", t}};
}

DossierPlans dossierPlansDepuisJson(const nlohmann::ordered_json& o) {
  DossierPlans d;
  if (!o.contains("feuilles")) return d;
  for (const auto& x : o["feuilles"]) {
    FeuillePlan f;
    f.nom = x.value("nom", f.nom);
    f.format = x.value("format", f.format);
    if (x.contains("vues"))
      for (const auto& v : x["vues"]) {
        VuePlan p;
        p.genre = genreVueDepuis(v.value("genre", std::string("couche")));
        p.titre = v.value("titre", std::string());
        p.couche = v.value("couche", p.couche);
        p.x = v.value("x", p.x);
        p.y = v.value("y", p.y);
        p.echelle = v.value("echelle", p.echelle);
        if (v.contains("centre") && v["centre"].is_array() && v["centre"].size() == 2) p.centre = ptMm(v["centre"][0].get<double>(), v["centre"][1].get<double>());
        p.rayon = v.value("rayon", p.rayon);
        p.circulaire = v.value("circulaire", p.circulaire);
        f.vues.push_back(p);
      }
    if (x.contains("notes"))
      for (const auto& n : x["notes"]) f.notes.push_back({n.value("numero", 1), n.value("texte", std::string())});
    d.feuilles.push_back(std::move(f));
  }
  return d;
}

const std::vector<std::string>& catalogueNotes() {
  static const std::vector<std::string> v = {
      "Carte de ${COUCHES} couches, épaisseur finie ${EPAISSEUR} mm ± 10 %.",
      "Matériau ${MATERIAU} ; cuivre de ${CUIVRE} µm sur les faces externes.",
      "Finition ${FINITION} ; vernis épargne ${VERNIS} sur les deux faces, sérigraphie ${SERIGRAPHIE}.",
      "Cotes en millimètres ; tolérance générale ± 0,1 mm sur le contour.",
      "Perçages : diamètres finis, tolérance + 0,08 / − 0,05 mm pour les trous métallisés.",
      "Tous les trous non métallisés sont repérés « NPTH » au tableau des perçages.",
      "Impédances contrôlées selon le tableau des lignes de transmission, tolérance ± 10 %.",
      "Contrôle électrique à 100 % selon le fichier IPC-D-356A joint.",
      "Marquer le code de date et le logo UL sur la sérigraphie du dessous si la place le permet.",
      "Matière et finition conformes à la directive RoHS 2011/65/UE."};
  return v;
}

DossierPlans dossierPlansParDefaut(const ConceptionElec& c) {
  DossierPlans d;
  FeuillePlan f;
  const Boite b = boite(c.circuit.contour.polygone());
  const double l = mm(b.largeur()), h = mm(b.hauteur());
  // Échelle de la vue : la carte tient dans 170 × 120 mm
  double e = std::min({2.0, 170.0 / std::max(l, 1.0), 120.0 / std::max(h, 1.0)});
  e = e >= 1 ? std::floor(e) : 1.0 / std::ceil(1.0 / e);
  f.vues.push_back({VuePlan::Genre::Couche, "Vue de dessus", "F.Cu", 20, 25, e, {}, 5, false});
  f.vues.push_back({VuePlan::Genre::Cote, "Vue de côté", "avant", 20, 35 + h * e, e, {}, 5, false});
  f.vues.push_back({VuePlan::Genre::TableauPercage, "Tableau des perçages", {}, 230, 25, 1, {}, 5, false});
  f.vues.push_back({VuePlan::Genre::Empilage, "Coupe de l'empilage", {}, 230, 140, 1, {}, 5, false});
  f.vues.push_back({VuePlan::Genre::Notes, "Notes", {}, 20, 230, 1, {}, 5, false});
  int n = 1;
  for (std::size_t i = 0; i < 6; ++i) f.notes.push_back({n++, catalogueNotes()[i]});
  d.feuilles.push_back(f);
  return d;
}

DossierPlans dossierPlans(const ConceptionElec& c) {
  if (const auto it = c.extensions.find("plans"); it != c.extensions.end()) {
    DossierPlans d = dossierPlansDepuisJson(it->second);
    if (!d.feuilles.empty()) return d;
  }
  return dossierPlansParDefaut(c);
}

void reglerDossierPlans(ConceptionElec& c, const DossierPlans& d) {
  exigerModifiable(c);
  c.extensions["plans"] = dossierPlansVersJson(d);
}

int renumeroterNotes(FeuillePlan& f) {
  int n = 0;
  for (auto& x : f.notes) x.numero = ++n;
  return n;
}

int remplacerDansNotes(DossierPlans& d, const std::string& ancien, const std::string& nouveau) {
  if (ancien.empty()) return 0;
  int n = 0;
  for (auto& f : d.feuilles)
    for (auto& x : f.notes) {
      bool change = false;
      for (std::size_t i = x.texte.find(ancien); i != std::string::npos; i = x.texte.find(ancien, i + nouveau.size())) {
        x.texte.replace(i, ancien.size(), nouveau);
        change = true;
      }
      n += change;
    }
  return n;
}

std::string texteNote(const ConceptionElec& c, const std::string& modele) {
  const Empilage& e = c.circuit.empilage;
  std::string s = modele;
  const std::pair<const char*, std::string> table[] = {{"${COUCHES}", std::to_string(e.couchesCuivre)}, {"${EPAISSEUR}", virgule(e.epaisseur, 2)}, {"${CUIVRE}", f2(e.cuivre * 1000, 0)},
                                                        {"${FINITION}", e.finition}, {"${VERNIS}", e.vernis}, {"${SERIGRAPHIE}", e.serigraphie}, {"${MATERIAU}", e.materiau}};
  for (const auto& [cle, v] : table)
    for (std::size_t i = s.find(cle); i != std::string::npos; i = s.find(cle, i + v.size())) s.replace(i, std::strlen(cle), v);
  return s;
}

ContenuTableau contenuTableau(const ConceptionElec& c, VuePlan::Genre genre) {
  ContenuTableau t;
  switch (genre) {
    case VuePlan::Genre::TableauPercage: {
      const Connectivite k = connectivite(c);
      const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
      t.entetes = {"Symbole", "Diamètre (mm)", "Fente (mm)", "Métallisé", "Nombre", "Couches"};
      static const char* const SYMB[] = {"A", "B", "C", "D", "E", "F", "G", "H", "J", "K", "L", "M", "N", "P", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
      std::size_t i = 0;
      for (const auto& l : tableauPercages(m)) {
        t.lignes.push_back({SYMB[i++ % 23], virgule(mm(l.diametre), 2), l.longueur ? virgule(mm(l.longueur), 2) : "", l.metallise ? "PTH" : "NPTH", std::to_string(l.nombre), l.paire});
        t.total += l.nombre;
      }
      break;
    }
    case VuePlan::Genre::TableauLignes: {
      t.entetes = {"Profil", "Impédance", "Couche", "Largeur (mm)", "Écart (mm)"};
      for (const auto& p : c.circuit.regles.avancees.impedances)
        for (const auto& [couche, lw] : p.parCouche) {
          t.lignes.push_back({p.nom, virgule(p.impedance, 0) + (p.differentiel ? " Ω diff." : " Ω"), couche, virgule(mm(lw.first), 3), lw.second ? virgule(mm(lw.second), 3) : ""});
          ++t.total;
        }
      break;
    }
    case VuePlan::Genre::Nomenclature: {
      t.entetes = {"Qté", "Repères", "Valeur", "Empreinte", "Fabricant", "Référence"};
      for (const auto& l : nomenclature(c)) {
        std::string refs;
        for (const auto& r : l.references) refs += (refs.empty() ? "" : ", ") + r;
        t.lignes.push_back({std::to_string(l.quantite), refs, l.valeur, l.empreinte, l.fabricant, l.mpn});
        t.total += l.quantite;
      }
      break;
    }
    default: break;
  }
  return t;
}

namespace {
// Tableau en SVG au point (x, y) ; rend sa hauteur
double svgTableau(std::ostringstream& s, const ContenuTableau& t, double x, double y, const std::string& titre) {
  const double h = 5.5, taille = 2.5;
  std::vector<double> largeurs;
  for (std::size_t j = 0; j < t.entetes.size(); ++j) {
    std::size_t n = t.entetes[j].size();
    for (const auto& l : t.lignes)
      if (j < l.size()) n = std::max(n, l[j].size());
    largeurs.push_back(std::max(10.0, 1.6 * static_cast<double>(n) + 3));
  }
  double largeur = 0;
  for (double w : largeurs) largeur += w;
  s << "<text x=\"" << f2(x) << "\" y=\"" << f2(y - 1.5) << "\" font-size=\"3.2\" font-weight=\"bold\">" << xml(titre) << "</text>\n";
  const std::size_t n = t.lignes.size() + 1;
  s << "<rect x=\"" << f2(x) << "\" y=\"" << f2(y) << "\" width=\"" << f2(largeur) << "\" height=\"" << f2(h * static_cast<double>(n)) << "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.35\"/>\n";
  for (std::size_t i = 0; i < n; ++i) {
    const auto& l = i == 0 ? t.entetes : t.lignes[i - 1];
    double cx = x;
    for (std::size_t j = 0; j < largeurs.size(); ++j) {
      if (j < l.size())
        s << "<text x=\"" << f2(cx + 1.5) << "\" y=\"" << f2(y + h * static_cast<double>(i) + 3.9) << "\" font-size=\"" << f2(taille) << "\"" << (i == 0 ? " font-weight=\"bold\"" : "") << ">" << xml(l[j]) << "</text>\n";
      cx += largeurs[j];
      if (j + 1 < largeurs.size()) s << "<line x1=\"" << f2(cx) << "\" y1=\"" << f2(y) << "\" x2=\"" << f2(cx) << "\" y2=\"" << f2(y + h * static_cast<double>(n)) << "\" stroke=\"#000\" stroke-width=\"0.18\"/>\n";
    }
    if (i > 0) s << "<line x1=\"" << f2(x) << "\" y1=\"" << f2(y + h * static_cast<double>(i)) << "\" x2=\"" << f2(x + largeur) << "\" y2=\"" << f2(y + h * static_cast<double>(i)) << "\" stroke=\"#000\" stroke-width=\"0.18\"/>\n";
  }
  return h * static_cast<double>(n);
}
std::string chemin(const Polygone& p, const std::function<std::pair<double, double>(Pt)>& T) {
  std::string d;
  for (std::size_t i = 0; i < p.size(); ++i) {
    const auto [x, y] = T(p[i]);
    d += (i ? "L" : "M") + f2(x, 3) + " " + f2(y, 3);
  }
  return d + "Z";
}
}  // namespace

std::string svgFeuillePlan(const ConceptionElec& c, const FeuillePlan& f) {
  const Boite cadre = cadreFeuille(f.format);
  const double W = mm(cadre.largeur()), H = mm(cadre.hauteur());
  std::ostringstream s;
  s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << f2(W) << "mm\" height=\"" << f2(H) << "mm\" viewBox=\"0 0 " << f2(W) << " " << f2(H)
    << "\" font-family=\"sans-serif\">\n<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/>\n";
  s << "<rect x=\"10\" y=\"10\" width=\"" << f2(W - 20) << "\" height=\"" << f2(H - 20) << "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.7\"/>\n";
  // Cartouche
  {
    const double cx = W - 10 - 180, cy = H - 10 - 32;
    s << "<rect x=\"" << f2(cx) << "\" y=\"" << f2(cy) << "\" width=\"180\" height=\"32\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.5\"/>\n";
    const std::string titre = c.cartouche.titre.empty() ? c.nom : c.cartouche.titre;
    s << "<text x=\"" << f2(cx + 3) << "\" y=\"" << f2(cy + 9) << "\" font-size=\"6\" font-weight=\"bold\">" << xml(titre) << "</text>\n";
    s << "<text x=\"" << f2(cx + 3) << "\" y=\"" << f2(cy + 17) << "\" font-size=\"4\">" << xml(f.nom) << "</text>\n";
    s << "<text x=\"" << f2(cx + 3) << "\" y=\"" << f2(cy + 25) << "\" font-size=\"3\">" << xml("Révision " + c.cartouche.revision + (c.cartouche.date.empty() ? "" : " — " + c.cartouche.date) +
                                                                                                    (c.cartouche.societe.empty() ? "" : " — " + c.cartouche.societe))
      << "</text>\n";
  }
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  const Boite b = m.boite;
  for (const auto& v : f.vues) {
    switch (v.genre) {
      case VuePlan::Genre::Couche:
      case VuePlan::Genre::Detail: {
        const bool detail = v.genre == VuePlan::Genre::Detail;
        const double e = v.echelle;
        const double ox = detail ? mm(v.centre.x) - v.rayon : mm(b.x0), oy = detail ? mm(v.centre.y) - v.rayon : mm(b.y0);
        auto T = [&](Pt p) { return std::pair{v.x + (mm(p.x) - ox) * e, v.y + (mm(p.y) - oy) * e}; };
        const auto couche = coucheDepuisCode(v.couche).value_or(Couche::CuivreDessus);
        std::string clip;
        if (detail) {
          const std::string id = "d" + std::to_string(reinterpret_cast<std::uintptr_t>(&v) % 100000);
          s << "<clipPath id=\"" << id << "\">"
            << (v.circulaire ? "<circle cx=\"" + f2(v.x + v.rayon * e) + "\" cy=\"" + f2(v.y + v.rayon * e) + "\" r=\"" + f2(v.rayon * e) + "\"/>"
                             : "<rect x=\"" + f2(v.x) + "\" y=\"" + f2(v.y) + "\" width=\"" + f2(2 * v.rayon * e) + "\" height=\"" + f2(2 * v.rayon * e) + "\"/>")
            << "</clipPath>\n";
          clip = " clip-path=\"url(#" + id + ")\"";
        }
        s << "<g" << clip << ">\n";
        for (const auto& cf : m.couches) {
          if (cf.couche != couche) continue;
          for (const auto& p : cf.primitives) {
            if (p.type == PrimitiveFab::Type::Trait || p.type == PrimitiveFab::Type::Arc) {
              const auto [x1, y1] = T(p.a);
              const auto [x2, y2] = T(p.b);
              s << "<line x1=\"" << f2(x1, 3) << "\" y1=\"" << f2(y1, 3) << "\" x2=\"" << f2(x2, 3) << "\" y2=\"" << f2(y2, 3) << "\" stroke=\"#B33\" stroke-linecap=\"round\" stroke-width=\"" << f2(mm(p.l) * e, 3) << "\"/>\n";
              continue;
            }
            Polygone poly = p.type == PrimitiveFab::Type::Disque ? cercle(p.a, std::max<Nm>(p.l / 2, 1))
                            : p.type == PrimitiveFab::Type::Rect ? rectangle(p.a, p.l, p.h, p.rotation)
                            : p.type == PrimitiveFab::Type::Oblong ? rectangleArrondi(p.a, p.l, p.h, std::min(p.l, p.h) / 2, p.rotation)
                            : p.type == PrimitiveFab::Type::RectArrondi ? rectangleArrondi(p.a, p.l, p.h, p.r, p.rotation)
                                                                        : p.region;
            s << "<path d=\"" << chemin(poly, T) << "\" fill=\"#B33\"/>\n";
          }
        }
        for (const auto& t : m.trous) {
          const auto [x, y] = T(t.p);
          s << "<circle cx=\"" << f2(x, 3) << "\" cy=\"" << f2(y, 3) << "\" r=\"" << f2(mm(t.diametre) / 2 * e, 3) << "\" fill=\"#fff\" stroke=\"#000\" stroke-width=\"0.1\"/>\n";
        }
        s << "<path d=\"" << chemin(m.contour, T) << "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.35\"/>\n</g>\n";
        if (detail) s << (v.circulaire ? "<circle cx=\"" + f2(v.x + v.rayon * e) + "\" cy=\"" + f2(v.y + v.rayon * e) + "\" r=\"" + f2(v.rayon * e) + "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.35\"/>\n"
                                       : "<rect x=\"" + f2(v.x) + "\" y=\"" + f2(v.y) + "\" width=\"" + f2(2 * v.rayon * e) + "\" height=\"" + f2(2 * v.rayon * e) + "\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.35\"/>\n");
        // Cotes hors tout de la vue de couche
        if (!detail) {
          const double x0 = v.x, x1 = v.x + mm(b.largeur()) * e, y0 = v.y, y1 = v.y + mm(b.hauteur()) * e;
          s << "<line x1=\"" << f2(x0) << "\" y1=\"" << f2(y1 + 5) << "\" x2=\"" << f2(x1) << "\" y2=\"" << f2(y1 + 5) << "\" stroke=\"#000\" stroke-width=\"0.18\"/>\n<text x=\"" << f2((x0 + x1) / 2) << "\" y=\"" << f2(y1 + 4)
            << "\" font-size=\"2.5\" text-anchor=\"middle\">" << virgule(mm(b.largeur()), 2) << "</text>\n";
          s << "<line x1=\"" << f2(x1 + 5) << "\" y1=\"" << f2(y0) << "\" x2=\"" << f2(x1 + 5) << "\" y2=\"" << f2(y1) << "\" stroke=\"#000\" stroke-width=\"0.18\"/>\n<text x=\"" << f2(x1 + 6) << "\" y=\"" << f2((y0 + y1) / 2)
            << "\" font-size=\"2.5\">" << virgule(mm(b.hauteur()), 2) << "</text>\n";
        }
        s << "<text x=\"" << f2(v.x) << "\" y=\"" << f2(v.y - 3) << "\" font-size=\"3.2\" font-weight=\"bold\">" << xml(v.titre.empty() ? (detail ? "Détail" : nomCouche(couche)) : v.titre) << " (échelle "
          << (v.echelle >= 1 ? f2(v.echelle, 0) + ":1" : "1:" + f2(1 / v.echelle, 0)) << ")</text>\n";
        break;
      }
      case VuePlan::Genre::Cote: {
        // Vue de côté : la carte en coupe, chaque corps à sa hauteur au-dessus (dessus) ou au-dessous (dessous)
        const double e = v.echelle, ep = c.circuit.empilage.epaisseur;
        const bool gauche = v.couche == "gauche";
        auto X = [&](const Boite& bb) { return std::pair{gauche ? mm(bb.y0) - mm(b.y0) : mm(bb.x0) - mm(b.x0), gauche ? mm(bb.y1) - mm(b.y0) : mm(bb.x1) - mm(b.x0)}; };
        const auto [a0, a1] = X(b);
        double hmax = 0;
        for (const auto& em : c.circuit.empreintes)
          if (const ComposantElec* x = c.composant(em.composant)) hmax = std::max(hmax, hauteurComposant(c, *x).mm);
        const double base = v.y + (hmax + 2) * e;
        s << "<rect x=\"" << f2(v.x + a0 * e) << "\" y=\"" << f2(base) << "\" width=\"" << f2((a1 - a0) * e) << "\" height=\"" << f2(ep * e) << "\" fill=\"#3a7d44\" stroke=\"#000\" stroke-width=\"0.2\"/>\n";
        for (const auto& em : c.circuit.empreintes) {
          const ComposantElec* x = c.composant(em.composant);
          const DefEmpreinte* d = c.empreinte(em.empreinte);
          if (!x || !d) continue;
          const auto [c0, c1] = X(boite(encombrementSurCarte(em, *d)));
          const double hc = hauteurComposant(c, *x).mm;
          const double y = em.dessous ? base + ep * e : base - hc * e;
          s << "<rect x=\"" << f2(v.x + c0 * e) << "\" y=\"" << f2(y) << "\" width=\"" << f2((c1 - c0) * e) << "\" height=\"" << f2(hc * e) << "\" fill=\"#ddd\" stroke=\"#000\" stroke-width=\"0.15\"/>\n";
        }
        s << "<text x=\"" << f2(v.x) << "\" y=\"" << f2(v.y - 3) << "\" font-size=\"3.2\" font-weight=\"bold\">" << xml(v.titre.empty() ? std::string("Vue de côté") : v.titre) << " — hauteur maximale "
          << virgule(hmax, 2) << " mm</text>\n";
        break;
      }
      case VuePlan::Genre::Empilage: {
        // Coupe de l'empilage, chaque couche à une hauteur lisible (au moins 3 mm), son épaisseur réelle en texte
        double y = v.y;
        s << "<text x=\"" << f2(v.x) << "\" y=\"" << f2(y - 3) << "\" font-size=\"3.2\" font-weight=\"bold\">" << xml(v.titre.empty() ? std::string("Coupe de l'empilage") : v.titre) << " — "
          << virgule(epaisseurPile(pileEmpilage(c.circuit.empilage)), 3) << " mm</text>\n";
        for (const auto& x : pileEmpilage(c.circuit.empilage)) {
          const bool cuivre = x.genre == GenreCoucheEmpilage::Cuivre;
          const double h = cuivre ? 2 : std::clamp(x.epaisseur * 10, 3.0, 8.0);
          s << "<rect x=\"" << f2(v.x) << "\" y=\"" << f2(y) << "\" width=\"60\" height=\"" << f2(h) << "\" fill=\"" << (cuivre ? "#c87533" : x.genre == GenreCoucheEmpilage::AmeMetallique ? "#999" : "#d8cf9a")
            << "\" stroke=\"#000\" stroke-width=\"0.15\"/>\n<text x=\"" << f2(v.x + 63) << "\" y=\"" << f2(y + h / 2 + 1) << "\" font-size=\"2.4\">"
            << xml((x.nom.empty() ? (cuivre ? std::string("Cuivre") : std::string("Diélectrique")) : x.nom) + " — " + x.materiau + " — " + virgule(x.epaisseur, 3) + " mm" + (cuivre ? "" : " — εr " + virgule(x.permittivite, 2)))
            << "</text>\n";
          y += h;
        }
        break;
      }
      case VuePlan::Genre::TableauPercage:
      case VuePlan::Genre::TableauLignes:
      case VuePlan::Genre::Nomenclature: {
        const ContenuTableau t = contenuTableau(c, v.genre);
        const std::string titre = v.titre.empty() ? std::string(v.genre == VuePlan::Genre::TableauPercage ? "Tableau des perçages" : v.genre == VuePlan::Genre::TableauLignes ? "Lignes de transmission" : "Nomenclature") : v.titre;
        const double h = svgTableau(s, t, v.x, v.y, titre);
        if (v.genre == VuePlan::Genre::TableauPercage) s << "<text x=\"" << f2(v.x) << "\" y=\"" << f2(v.y + h + 4) << "\" font-size=\"2.5\">Total : " << t.total << " trou(s)</text>\n";
        break;
      }
      case VuePlan::Genre::Notes: {
        s << "<text x=\"" << f2(v.x) << "\" y=\"" << f2(v.y - 3) << "\" font-size=\"3.2\" font-weight=\"bold\">" << xml(v.titre.empty() ? std::string("Notes") : v.titre) << "</text>\n";
        double y = v.y + 2;
        for (const auto& n : f.notes) {
          s << "<text x=\"" << f2(v.x) << "\" y=\"" << f2(y) << "\" font-size=\"2.8\">" << n.numero << ". " << xml(texteNote(c, n.texte)) << "</text>\n";
          y += 4.5;
        }
        break;
      }
    }
  }
  s << "</svg>\n";
  return s.str();
}

int poserTableauSurCouche(ConceptionElec& c, VuePlan::Genre genre, Pt origine, Couche couche) {
  exigerModifiable(c);
  const ContenuTableau t = contenuTableau(c, genre);
  if (t.entetes.empty()) return 0;
  const Nm h = 2'500'000, hauteurTexte = 1'000'000;
  std::vector<Nm> largeurs;
  for (std::size_t j = 0; j < t.entetes.size(); ++j) {
    Nm w = largeurTexte(t.entetes[j], hauteurTexte);
    for (const auto& l : t.lignes)
      if (j < l.size()) w = std::max(w, largeurTexte(l[j], hauteurTexte));
    largeurs.push_back(w + 2'000'000);
  }
  Nm largeur = 0;
  for (Nm w : largeurs) largeur += w;
  int n = 0;
  const std::size_t rangees = t.lignes.size() + 1;
  auto trait = [&](Pt a, Pt b) {
    DessinCircuit d;
    d.uuid = nouvelUuid();
    d.forme = DessinCircuit::Forme::Trait;
    d.couche = couche;
    d.points = {a, b};
    d.epaisseur = 120'000;
    c.circuit.dessins.push_back(d);
    ++n;
  };
  for (std::size_t i = 0; i <= rangees; ++i) trait({origine.x, origine.y + static_cast<Nm>(i) * h}, {origine.x + largeur, origine.y + static_cast<Nm>(i) * h});
  Nm x = origine.x;
  for (std::size_t j = 0; j <= largeurs.size(); ++j) {
    trait({x, origine.y}, {x, origine.y + static_cast<Nm>(rangees) * h});
    if (j < largeurs.size()) x += largeurs[j];
  }
  for (std::size_t i = 0; i < rangees; ++i) {
    const auto& l = i == 0 ? t.entetes : t.lignes[i - 1];
    Nm cx = origine.x;
    for (std::size_t j = 0; j < largeurs.size(); ++j) {
      if (j < l.size() && !l[j].empty()) {
        TexteCircuit tx;
        tx.uuid = nouvelUuid();
        tx.texte = l[j];
        tx.couche = couche;
        tx.hauteur = hauteurTexte;
        tx.p = {cx + largeurs[j] / 2, origine.y + static_cast<Nm>(i) * h + h / 2};
        c.circuit.textes.push_back(tx);
        ++n;
      }
      cx += largeurs[j];
    }
  }
  return n;
}

std::string xlsxEmpilage(const ConceptionElec& c) {
  prom::TableauNomenclature t;
  t.titre = "Empilage de " + c.nom;
  t.colonnes = {{"rang", "Rang", true}, {"genre", "Genre", false}, {"nom", "Nom", false}, {"materiau", "Matériau", false}, {"epaisseur", "Épaisseur (mm)", true}, {"er", "εr", true}, {"tand", "tan δ", true}};
  int rang = 0;
  auto nombre = [](double v, int d) {
    std::string s = f2(v, d);
    std::replace(s.begin(), s.end(), '.', ',');
    return prom::CelluleNomenclature{s, v, true};
  };
  auto texte = [](std::string s) { return prom::CelluleNomenclature{std::move(s), 0, false}; };
  for (const auto& x : pileEmpilage(c.circuit.empilage)) {
    const bool cuivre = x.genre == GenreCoucheEmpilage::Cuivre;
    t.lignes.push_back({nombre(++rang, 0), texte(cuivre ? "cuivre" : x.genre == GenreCoucheEmpilage::AmeMetallique ? "âme métallique" : x.preimpregne ? "préimprégné" : "âme"), texte(x.nom), texte(x.materiau),
                        nombre(x.epaisseur, 3), cuivre ? texte({}) : nombre(x.permittivite, 2), cuivre ? texte({}) : nombre(x.pertes, 4)});
  }
  t.niveaux.assign(t.lignes.size(), 0);
  t.assemblages.assign(t.lignes.size(), 0);
  return prom::xlsxNomenclature(t);
}

}  // namespace prom::ecad
