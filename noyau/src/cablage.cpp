// Prométhée : bibliothèque de câblage et schéma de câblage de faisceau (lot L19 de la 0.34 ; 19.10). Voir
// cablage.hpp. Licence GPL-3.0-only.
#include "promethee/cablage.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>

#include "electrique_interne.hpp"

namespace prom::elec {

using Json = nlohmann::ordered_json;

namespace {
// Interpolation linéaire dans une table croissante en x
double interpoler(const std::vector<std::pair<double, double>>& t, double x) {
  if (t.empty()) return 0;
  if (x <= t.front().first) return t.front().second;
  if (x >= t.back().first) return t.back().second;
  for (std::size_t i = 1; i < t.size(); ++i)
    if (x <= t[i].first) {
      const double u = (x - t[i - 1].first) / (t[i].first - t[i - 1].first);
      return t[i - 1].second + u * (t[i].second - t[i - 1].second);
    }
  return t.back().second;
}
bool ordreNaturelCablage(const std::string& a, const std::string& b) {
  std::size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
      std::size_t fi = i, fj = j;
      while (fi < a.size() && std::isdigit(static_cast<unsigned char>(a[fi]))) ++fi;
      while (fj < b.size() && std::isdigit(static_cast<unsigned char>(b[fj]))) ++fj;
      const long long x = std::stoll(a.substr(i, std::min<std::size_t>(fi - i, 15))), y = std::stoll(b.substr(j, std::min<std::size_t>(fj - j, 15)));
      if (x != y) return x < y;
      i = fi;
      j = fj;
      continue;
    }
    if (a[i] != b[j]) return a[i] < b[j];
    ++i;
    ++j;
  }
  return a.size() - i < b.size() - j;
}
// Conducteurs DIN 47100 (câbles de commande et de données)
const std::vector<std::string>& couleursDin47100() {
  static const std::vector<std::string> v = {"WH", "BN", "GN", "YE", "GY", "PK", "BU", "RD", "BK", "VT", "GY/PK", "RD/BU", "WH/GN", "BN/GN", "WH/YE", "YE/BN"};
  return v;
}
EntreeCable cableHd308(const std::string& designation, const std::string& description, const std::string& norme, int n, double section, double tension, double prix) {
  EntreeCable e;
  e.designation = designation;
  e.description = description;
  e.norme = norme;
  e.nombreConducteurs = n;
  e.section = section;
  e.tension = tension;
  e.prixMetre = prix;
  return e;
}
EntreeCable cableDin(const std::string& designation, const std::string& description, int n, double section, bool blinde, double prix) {
  EntreeCable e;
  e.designation = designation;
  e.description = description;
  e.norme = "DIN 47100";
  e.section = section;
  e.blinde = blinde;
  e.tension = 500;
  e.prixMetre = prix;
  for (int i = 0; i < n; ++i) e.conducteurs.push_back({std::to_string(i + 1), couleursDin47100()[static_cast<std::size_t>(i) % couleursDin47100().size()], 0, false});
  e.nombreConducteurs = n;
  return e;
}
EntreeCable filSeul(const std::string& designation, double section, const std::string& couleur, double prix) {
  EntreeCable e;
  e.designation = designation;
  e.description = "Fil seul " + detail::nombreFr(section) + " mm²";
  e.norme = designation.rfind("FLRY", 0) == 0 ? "ISO 6722" : "HD 21";
  e.section = section;
  e.nombreConducteurs = 1;
  e.conducteurs = {{"1", couleur, 0, false}};
  e.prixMetre = prix;
  return e;
}
}  // namespace

// ======================================================================================================================
// Bibliothèque
// ======================================================================================================================
const EntreeCable* BibliothequeCablage::cable(const std::string& designation) const {
  for (const auto& c : cables)
    if (c.designation == designation) return &c;
  return nullptr;
}
const EntreeContact* BibliothequeCablage::contact(const std::string& reference) const {
  for (const auto& c : contacts)
    if (c.reference == reference) return &c;
  return nullptr;
}
const EntreeConnecteurCablage* BibliothequeCablage::connecteur(const std::string& reference) const {
  for (const auto& c : connecteurs)
    if (c.reference == reference) return &c;
  return nullptr;
}
const EntreeProtection* BibliothequeCablage::protection(const std::string& reference) const {
  for (const auto& c : protections)
    if (c.reference == reference) return &c;
  return nullptr;
}

const BibliothequeCablage& bibliothequeCablageIntegree() {
  static const BibliothequeCablage b = [] {
    BibliothequeCablage x;
    // Les huit désignations de l'atelier électrique, dans leur ordre
    x.cables.push_back(cableHd308("H07RN-F", "Câble souple caoutchouc, gaine polychloroprène, 450/750 V (chantier, extérieur)", "HD 22.4", 3, 1.5, 750, 2.10));
    x.cables.push_back(cableHd308("H05VV-F", "Câble souple PVC, 300/500 V (appareils domestiques)", "HD 21.5", 3, 0.75, 500, 0.85));
    x.cables.push_back(cableHd308("H05RR-F", "Câble souple caoutchouc, 300/500 V (contraintes mécaniques légères)", "HD 22.4", 3, 1.0, 500, 1.40));
    x.cables.push_back(cableHd308("U-1000 R2V", "Câble rigide cuivre, isolant PR, gaine PVC, 0,6/1 kV (pose fixe)", "NF C 32-321", 3, 2.5, 1000, 1.90));
    {
      EntreeCable e = cableHd308("U-1000 AR2V", "Câble rigide aluminium, isolant PR, gaine PVC, 0,6/1 kV (distribution)", "NF C 32-321", 4, 16, 1000, 3.20);
      e.resistanceLineique = 1.91;  // aluminium classe 2, 16 mm² (CEI 60228)
      e.courantAdmissible = 73;
      x.cables.push_back(e);
    }
    x.cables.push_back(cableDin("LiYY", "Câble de commande PVC, conducteurs repérés DIN 47100", 4, 0.25, false, 0.60));
    {
      EntreeCable e = cableDin("LiYCY (blindé)", "Câble de commande PVC, tresse de cuivre étamé", 4, 0.25, true, 1.10);
      e.pieces = {"presse-étoupe CEM"};
      x.cables.push_back(e);
    }
    x.cables.push_back(cableHd308("FR-N1X1G1", "Câble rigide sans halogène, 0,6/1 kV (établissements recevant du public)", "NF C 32-323", 3, 2.5, 1000, 2.40));
    // Câble de données à paires torsadées, blindage par écran et fil de drain
    {
      EntreeCable e;
      e.designation = "LiYCY-TP 2x2x0,25";
      e.description = "Câble de données, deux paires torsadées, écran aluminium et drain";
      e.norme = "DIN 47100";
      e.section = 0.25;
      e.blinde = true;
      e.drain = true;
      e.tension = 250;
      e.diametre = 5.6;
      e.prixMetre = 1.35;
      e.conducteurs = {{"1", "WH", 1, false}, {"2", "BN", 1, false}, {"3", "GN", 2, false}, {"4", "YE", 2, false}, {"D", "", 0, true}};
      e.nombreConducteurs = 5;
      e.pieces = {"manchon de reprise d'écran"};
      x.cables.push_back(e);
    }
    // Fils seuls
    for (const auto& [s, d, c, p] : std::vector<std::tuple<double, const char*, const char*, double>>{
             {0.35, "FLRY-B 0,35", "BK", 0.09}, {0.5, "FLRY-B 0,5", "BK", 0.11}, {0.75, "FLRY-B 0,75", "BK", 0.14}, {1.0, "FLRY-B 1", "BK", 0.17}, {1.5, "FLRY-B 1,5", "BK", 0.23},
             {2.5, "FLRY-B 2,5", "BK", 0.36}, {0.5, "H05V-K 0,5", "BK", 0.12}, {0.75, "H05V-K 0,75", "BK", 0.15}, {1.0, "H05V-K 1", "BK", 0.18}, {1.5, "H07V-K 1,5", "BK", 0.25},
             {2.5, "H07V-K 2,5", "BK", 0.38}})
      x.cables.push_back(filSeul(d, s, c, p));
    // Contacts : à sertir (Micro-Fit, Mini-Fit, DT), embouts de câblage pour les borniers
    x.contacts = {{"43030-0007", "Molex", "femelle", 0.20, 0.52, 3.0, 0, 0.09},
                  {"43030-0001", "Molex", "femelle", 0.75, 0.82, 3.0, 0, 0.09},
                  {"39-00-0039", "Molex", "femelle", 0.33, 1.31, 3.5, 0, 0.12},
                  {"0462-201-16141", "Deutsch", "femelle", 0.50, 1.00, 6.4, 0, 0.35},
                  {"0460-202-16141", "Deutsch", "mâle", 0.50, 1.00, 6.4, 0, 0.35},
                  {"Embout 0,25 mm²", "Générique", "embout", 0.14, 0.25, 8.0, 0, 0.02},
                  {"Embout 0,34 mm²", "Générique", "embout", 0.26, 0.34, 8.0, 0, 0.02},
                  {"Embout 0,5 mm²", "Générique", "embout", 0.35, 0.50, 8.0, 0, 0.02},
                  {"Embout 0,75 mm²", "Générique", "embout", 0.51, 0.75, 8.0, 0, 0.02},
                  {"Embout 1 mm²", "Générique", "embout", 0.76, 1.00, 8.0, 0, 0.02},
                  {"Embout 1,5 mm²", "Générique", "embout", 1.01, 1.50, 10.0, 0, 0.03},
                  {"Embout 2,5 mm²", "Générique", "embout", 1.51, 2.50, 10.0, 0, 0.03}};
    x.connecteurs = {{"43025-0400", "Molex", "Micro-Fit 3.0, boîtier femelle 2×2", 4, false, {"43030-0007", "43030-0001"}, "", "", 0.40},
                     {"43025-0800", "Molex", "Micro-Fit 3.0, boîtier femelle 2×4", 8, false, {"43030-0007", "43030-0001"}, "", "", 0.55},
                     {"DT06-4S", "Deutsch", "DT étanche, 4 voies, prise femelle", 4, true, {"0462-201-16141"}, "", "114017", 2.80},
                     {"DT04-4P", "Deutsch", "DT étanche, 4 voies, embase mâle", 4, true, {"0460-202-16141"}, "", "114017", 2.60},
                     {"Bornier 5 bornes à ressort", "Générique", "bornier de passage, 0,14 à 2,5 mm²", 5, false, {}, "", "", 2.50}};
    x.protections = {{"Gaine tressée 3-7", "gaine", "gaine tressée extensible PET", 3, 7, 0.4, 0, 0.5, 0.45, 0},
                     {"Gaine tressée 6-12", "gaine", "gaine tressée extensible PET", 6, 12, 0.5, 0, 0.5, 0.60, 0},
                     {"Gaine annelée AD10", "gaine", "gaine annelée fendue PA6", 4, 8, 0.8, 0, 0.5, 0.35, 0},
                     {"Gaine annelée AD15,8", "gaine", "gaine annelée fendue PA6", 8, 12.5, 0.9, 0, 0.5, 0.48, 0},
                     {"Ruban PVC 19 mm", "ruban", "ruban isolant PVC", 2, 60, 0.13, 19, 0.5, 0.05, 0},
                     {"Ruban tissu 19 mm", "ruban", "ruban textile (anti-bruit)", 2, 60, 0.3, 19, 0.5, 0.09, 0},
                     {"Manchon thermo 6/2", "manchon", "gaine thermorétractable 3:1", 2, 6, 0.6, 0, 0.5, 0, 0.35},
                     {"Manchon thermo 12/4", "manchon", "gaine thermorétractable 3:1", 4, 12, 0.8, 0, 0.5, 0, 0.55},
                     {"Étiquette 25 × 10", "étiquette", "étiquette auto-plastifiante", 2, 12, 0.1, 0, 0.5, 0, 0.08}};
    return x;
  }();
  return b;
}

namespace {
Json conducteurVersJson(const ConducteurBib& c) {
  Json j = {{"repere", c.repere}, {"couleur", c.couleur}};
  if (c.paire) j["paire"] = c.paire;
  if (c.drain) j["drain"] = true;
  return j;
}
}  // namespace

std::string texteBibliothequeCablage(const BibliothequeCablage& b) {
  Json j = {{"format", "promethee-cables"}, {"version", kVersionBibliothequeCablage}};
  Json& cables = j["cables"] = Json::array();
  for (const auto& c : b.cables) {
    Json x = {{"designation", c.designation}, {"description", c.description}, {"norme", c.norme}, {"section", c.section}};
    if (c.conducteurs.empty()) x["conducteurs"] = c.nombreConducteurs;
    else {
      x["conducteurs"] = Json::array();
      for (const auto& k : c.conducteurs) x["conducteurs"].push_back(conducteurVersJson(k));
    }
    if (c.blinde) x["blinde"] = true;
    if (c.drain) x["drain"] = true;
    if (c.diametre > 0) x["diametre"] = c.diametre;
    if (c.diametreConducteur > 0) x["diametreConducteur"] = c.diametreConducteur;
    if (c.resistanceLineique > 0) x["resistanceLineique"] = c.resistanceLineique;
    if (c.courantAdmissible > 0) x["courantAdmissible"] = c.courantAdmissible;
    if (c.rayonCourbure > 0) x["rayonCourbure"] = c.rayonCourbure;
    if (c.tension > 0) x["tension"] = c.tension;
    if (c.prixMetre > 0) x["prixMetre"] = c.prixMetre;
    if (!c.pieces.empty()) x["pieces"] = c.pieces;
    cables.push_back(x);
  }
  Json& contacts = j["contacts"] = Json::array();
  for (const auto& c : b.contacts)
    contacts.push_back({{"reference", c.reference}, {"fabricant", c.fabricant}, {"genre", c.genre}, {"sectionMin", c.sectionMin}, {"sectionMax", c.sectionMax}, {"denudage", c.denudage},
                        {"recul", c.recul}, {"prix", c.prix}});
  Json& connecteurs = j["connecteurs"] = Json::array();
  for (const auto& c : b.connecteurs)
    connecteurs.push_back({{"reference", c.reference}, {"fabricant", c.fabricant}, {"description", c.description}, {"cavites", c.cavites}, {"etanche", c.etanche}, {"contacts", c.contacts},
                           {"joint", c.joint}, {"bouchon", c.bouchon}, {"prix", c.prix}});
  Json& protections = j["protections"] = Json::array();
  for (const auto& p : b.protections)
    protections.push_back({{"reference", p.reference}, {"genre", p.genre}, {"description", p.description}, {"diametreMin", p.diametreMin}, {"diametreMax", p.diametreMax},
                           {"epaisseur", p.epaisseur}, {"largeur", p.largeur}, {"recouvrement", p.recouvrement}, {"prixMetre", p.prixMetre}, {"prix", p.prix}});
  return j.dump(1) + "\n";
}

std::optional<BibliothequeCablage> lireBibliothequeCablage(const std::string& texte, std::vector<std::string>* migrations, std::string* erreur) {
  try {
    const Json j = Json::parse(texte);
    if (j.value("format", std::string()) != "promethee-cables") throw std::runtime_error("ce n'est pas une bibliothèque de câbles Prométhée");
    const int version = j.value("version", 1);
    if (version > kVersionBibliothequeCablage) throw std::runtime_error("bibliothèque de câbles d'une version plus récente (" + std::to_string(version) + ")");
    BibliothequeCablage b;
    for (const auto& x : j.value("cables", Json::array())) {
      EntreeCable c;
      c.designation = x.value("designation", std::string());
      if (c.designation.empty()) continue;
      c.description = x.value("description", std::string());
      c.norme = x.value("norme", std::string());
      c.section = x.value("section", 1.5);
      c.blinde = x.value("blinde", false);
      c.drain = x.value("drain", false);
      c.diametre = x.value("diametre", 0.0);
      c.diametreConducteur = x.value("diametreConducteur", 0.0);
      c.resistanceLineique = x.value("resistanceLineique", 0.0);
      c.courantAdmissible = x.value("courantAdmissible", 0.0);
      c.rayonCourbure = x.value("rayonCourbure", 0.0);
      c.tension = x.value("tension", 0.0);
      c.prixMetre = x.value("prixMetre", 0.0);
      if (x.contains("pieces") && x["pieces"].is_array())
        for (const auto& p : x["pieces"])
          if (p.is_string()) c.pieces.push_back(p.get<std::string>());
      if (x.contains("conducteurs") && x["conducteurs"].is_array()) {
        for (const auto& k : x["conducteurs"]) c.conducteurs.push_back({k.value("repere", std::string()), k.value("couleur", std::string()), k.value("paire", 0), k.value("drain", false)});
        c.nombreConducteurs = static_cast<int>(c.conducteurs.size());
      } else
        c.nombreConducteurs = x.value("conducteurs", 3);
      // Version 1 : le blindage d'un câble à paires portait son drain sans le dire ; les paires venaient de « paires »
      if (version < 2) {
        const int paires = x.value("paires", 0);
        if (paires > 0 && c.conducteurs.empty()) {
          for (int p = 0; p < paires; ++p)
            for (int k = 0; k < 2; ++k) {
              const std::size_t i = static_cast<std::size_t>(2 * p + k);
              c.conducteurs.push_back({std::to_string(i + 1), couleursDin47100()[i % couleursDin47100().size()], p + 1, false});
            }
          if (c.blinde) {
            c.drain = true;
            c.conducteurs.push_back({"D", "", 0, true});
          }
          c.nombreConducteurs = static_cast<int>(c.conducteurs.size());
          if (migrations) migrations->push_back(c.designation + " : " + std::to_string(paires) + " paire(s) détaillée(s)" + (c.blinde ? ", drain ajouté" : ""));
        }
      }
      b.cables.push_back(std::move(c));
    }
    for (const auto& x : j.value("contacts", Json::array()))
      b.contacts.push_back({x.value("reference", std::string()), x.value("fabricant", std::string()), x.value("genre", std::string("femelle")), x.value("sectionMin", 0.0), x.value("sectionMax", 0.0),
                            x.value("denudage", 0.0), x.value("recul", 0.0), x.value("prix", 0.0)});
    for (const auto& x : j.value("connecteurs", Json::array())) {
      EntreeConnecteurCablage c{x.value("reference", std::string()), x.value("fabricant", std::string()), x.value("description", std::string()), x.value("cavites", 0), x.value("etanche", false), {},
                                x.value("joint", std::string()), x.value("bouchon", std::string()), x.value("prix", 0.0)};
      if (x.contains("contacts") && x["contacts"].is_array())
        for (const auto& k : x["contacts"])
          if (k.is_string()) c.contacts.push_back(k.get<std::string>());
      b.connecteurs.push_back(std::move(c));
    }
    for (const auto& x : j.value("protections", Json::array()))
      b.protections.push_back({x.value("reference", std::string()), x.value("genre", std::string("gaine")), x.value("description", std::string()), x.value("diametreMin", 0.0),
                               x.value("diametreMax", 0.0), x.value("epaisseur", 0.0), x.value("largeur", 0.0), x.value("recouvrement", 0.5), x.value("prixMetre", 0.0), x.value("prix", 0.0)});
    if (version < 2 && migrations) migrations->insert(migrations->begin(), "format des câbles " + std::to_string(version) + " → " + std::to_string(kVersionBibliothequeCablage));
    b.version = kVersionBibliothequeCablage;
    return b;
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
}

std::vector<ConducteurBib> conducteursCable(const EntreeCable& c) {
  if (!c.conducteurs.empty()) return c.conducteurs;
  std::vector<ConducteurBib> r;
  // HD 308 : conducteur de protection à partir de trois conducteurs (« 3G1,5 »)
  const bool pe = c.nombreConducteurs >= 3;
  for (const auto& rep : reperesConducteurs(c.nombreConducteurs, pe, c.nombreConducteurs > 5)) r.push_back({rep, rep == "GNYE" ? "GN/YE" : std::isdigit(static_cast<unsigned char>(rep[0])) ? "BK" : rep, 0, false});
  return r;
}

double diametreFil(double section) {
  // Fil seul à isolant mince (ISO 6722, FLRY-B), diamètre extérieur moyen
  static const std::vector<std::pair<double, double>> t = {{0.13, 1.0}, {0.22, 1.1}, {0.35, 1.2}, {0.5, 1.4}, {0.75, 1.7}, {1.0, 1.9}, {1.5, 2.2}, {2.5, 2.9},
                                                           {4.0, 3.6}, {6.0, 4.3}, {10.0, 5.6}, {16.0, 7.0}, {25.0, 8.6}, {35.0, 9.9}};
  if (section > 35) return 2 * std::sqrt(section / 3.14159265358979323846) * 1.25 + 1.2;
  return interpoler(t, section);
}

double resistanceCuivre(double section) {
  // CEI 60228, classe 5 (souple), cuivre recuit nu à 20 °C, Ω/km = mΩ/m
  static const std::vector<std::pair<double, double>> t = {{0.5, 39.0}, {0.75, 26.0}, {1.0, 19.5}, {1.5, 13.3}, {2.5, 7.98}, {4.0, 4.95}, {6.0, 3.30}, {10.0, 1.91}, {16.0, 1.21}, {25.0, 0.780}, {35.0, 0.554}};
  if (section <= 0) return 0;
  for (const auto& [s, r] : t)
    if (std::abs(s - section) < 1e-9) return r;
  return 19.5 / section;  // hors table : 19,5 mΩ·mm²/m
}

double courantAdmissibleFil(double section) {
  static const std::vector<std::pair<double, double>> t = {{0.13, 3}, {0.22, 5}, {0.35, 7}, {0.5, 9}, {0.75, 12}, {1.0, 15}, {1.5, 19}, {2.5, 26}, {4.0, 35}, {6.0, 45}, {10.0, 63}, {16.0, 85}, {25.0, 112}, {35.0, 138}};
  return interpoler(t, section);
}

double resistanceLineique(const EntreeCable& c) { return c.resistanceLineique > 0 ? c.resistanceLineique : resistanceCuivre(c.section); }
double courantAdmissibleCable(const EntreeCable& c) {
  if (c.courantAdmissible > 0) return c.courantAdmissible;
  // Plusieurs conducteurs chargés dans une gaine : facteur de groupement
  const auto conducteurs = conducteursCable(c);
  const int charges = std::max(1, static_cast<int>(std::count_if(conducteurs.begin(), conducteurs.end(), [](const ConducteurBib& k) { return !k.drain && k.couleur != "GN/YE"; })));
  return courantAdmissibleFil(c.section) * (charges <= 1 ? 1.0 : charges <= 3 ? 0.8 : charges <= 5 ? 0.7 : 0.6);
}
double diametreCable(const EntreeCable& c) {
  if (c.diametre > 0) return c.diametre;
  const auto k = conducteursCable(c);
  const double d = c.diametreConducteur > 0 ? c.diametreConducteur : diametreFil(c.section);
  if (k.size() <= 1) return d;
  // Empilement des conducteurs, gaine de 0,8 mm (et écran de 0,3 mm)
  return diametreFaisceau(std::vector<double>(k.size(), d), 1.0) + 2 * (0.8 + (c.blinde ? 0.3 : 0));
}

const EntreeContact* contactPour(const BibliothequeCablage& b, double section, const EntreeConnecteurCablage* connecteur) {
  const EntreeContact* r = nullptr;
  for (const auto& c : b.contacts) {
    if (section < c.sectionMin - 1e-9 || section > c.sectionMax + 1e-9) continue;
    if (connecteur && !connecteur->contacts.empty() && std::find(connecteur->contacts.begin(), connecteur->contacts.end(), c.reference) == connecteur->contacts.end()) continue;
    if (!connecteur && c.genre == "embout") continue;  // un embout ne va qu'à un bornier
    if (!r || c.sectionMax - c.sectionMin < r->sectionMax - r->sectionMin) r = &c;
  }
  return r;
}

std::vector<const EntreeProtection*> protectionsPour(const BibliothequeCablage& b, double diametre, const std::string& genre) {
  std::vector<const EntreeProtection*> r;
  for (const auto& p : b.protections)
    if (p.genre == genre && diametre >= p.diametreMin - 1e-9 && diametre <= p.diametreMax + 1e-9) r.push_back(&p);
  std::sort(r.begin(), r.end(), [](const EntreeProtection* a, const EntreeProtection* c) { return a->diametreMax - a->diametreMin < c->diametreMax - c->diametreMin; });
  return r;
}

// ======================================================================================================================
// Modèle
// ======================================================================================================================
const char* nomProvenance(ProvenanceLongueur p) {
  switch (p) {
    case ProvenanceLongueur::Saisie: return "saisie";
    case ProvenanceLongueur::Planche: return "planche";
    case ProvenanceLongueur::Topologie: return "topologie";
    case ProvenanceLongueur::Trace3D: return "3D";
    default: return "à mesurer";
  }
}
namespace {
ProvenanceLongueur provenanceDepuis(const std::string& s) {
  if (s == "saisie") return ProvenanceLongueur::Saisie;
  if (s == "planche") return ProvenanceLongueur::Planche;
  if (s == "topologie") return ProvenanceLongueur::Topologie;
  if (s == "3D") return ProvenanceLongueur::Trace3D;
  return ProvenanceLongueur::Aucune;
}
}  // namespace

const CaviteCablage* ConnecteurCablage::cavite(const std::string& numero) const {
  for (const auto& c : cavites)
    if (c.numero == numero) return &c;
  return nullptr;
}
CaviteCablage* ConnecteurCablage::cavite(const std::string& numero) {
  for (auto& c : cavites)
    if (c.numero == numero) return &c;
  return nullptr;
}
ConnecteurCablage* Cablage::connecteur(const std::string& repere) {
  for (auto& c : connecteurs)
    if (c.repere == repere || c.uuid == repere) return &c;
  return nullptr;
}
const ConnecteurCablage* Cablage::connecteur(const std::string& repere) const {
  for (const auto& c : connecteurs)
    if (c.repere == repere || c.uuid == repere) return &c;
  return nullptr;
}
FilCablage* Cablage::fil(const std::string& numero) {
  for (auto& f : fils)
    if (f.numero == numero || f.uuid == numero) return &f;
  return nullptr;
}
const FilCablage* Cablage::fil(const std::string& numero) const {
  for (const auto& f : fils)
    if (f.numero == numero || f.uuid == numero) return &f;
  return nullptr;
}

Json cablageVersJson(const Cablage& h) {
  Json j = {{"uuid", h.uuid}, {"nom", h.nom}, {"foisonnement", h.foisonnement}, {"surlongueur", h.surlongueur}};
  Json& ks = j["connecteurs"] = Json::array();
  for (const auto& k : h.connecteurs) {
    Json x = {{"uuid", k.uuid}, {"repere", k.repere}, {"reference", k.reference}, {"carte", k.carte}, {"conception", k.conception}, {"composant", k.composant}, {"etanche", k.etanche},
              {"x", k.position.x}, {"y", k.position.y}};
    Json& cs = x["cavites"] = Json::array();
    for (const auto& c : k.cavites) {
      Json y = {{"numero", c.numero}};
      if (!c.reseau.empty()) y["reseau"] = c.reseau;
      if (!c.contact.empty()) y["contact"] = c.contact;
      if (!c.joint.empty()) y["joint"] = c.joint;
      if (!c.bouchon.empty()) y["bouchon"] = c.bouchon;
      if (c.denudage > 0) y["denudage"] = c.denudage;
      if (c.recul > 0) y["recul"] = c.recul;
      cs.push_back(y);
    }
    ks.push_back(x);
  }
  Json& cbs = j["cables"] = Json::array();
  for (const auto& c : h.cables)
    cbs.push_back({{"uuid", c.uuid}, {"reference", c.reference}, {"designation", c.designation}, {"de", c.de}, {"vers", c.vers}, {"longueur", c.longueur}, {"provenance", nomProvenance(c.provenance)}});
  Json& fs = j["fils"] = Json::array();
  for (const auto& f : h.fils) {
    Json x = {{"uuid", f.uuid}, {"numero", f.numero}, {"section", f.section}, {"couleur", f.couleur}, {"de", f.de}, {"deCavite", f.deCavite}, {"vers", f.vers}, {"versCavite", f.versCavite},
              {"longueur", f.longueur}, {"provenance", nomProvenance(f.provenance)}};
    if (!f.cable.empty()) x["cable"] = f.cable, x["conducteur"] = f.conducteur;
    if (f.paire) x["paire"] = f.paire;
    if (f.drain) x["drain"] = true;
    if (!f.surFil.empty()) x["surFil"] = f.surFil;
    if (!f.reseau.empty()) x["reseau"] = f.reseau;
    if (f.cavalier) x["cavalier"] = true;
    if (f.longueurForcee > 0) x["longueurForcee"] = f.longueurForcee;
    if (!f.chemin.empty()) x["chemin"] = f.chemin;
    fs.push_back(x);
  }
  Json& es = j["epissures"] = Json::array();
  for (const auto& e : h.epissures) es.push_back({{"uuid", e.uuid}, {"repere", e.repere}, {"fils", e.fils}, {"noeud", e.noeud}});
  Json& ns = j["noeuds"] = Json::array();
  for (const auto& n : h.noeuds) ns.push_back({{"uuid", n.uuid}, {"nom", n.nom}, {"connecteur", n.connecteur}, {"x", n.x}, {"y", n.y}});
  Json& ts = j["troncons"] = Json::array();
  for (const auto& t : h.troncons) ts.push_back({{"uuid", t.uuid}, {"a", t.a}, {"b", t.b}, {"longueur", t.longueur}, {"coupure", t.coupure}});
  Json& ps = j["protections"] = Json::array();
  for (const auto& p : h.protections)
    ps.push_back({{"troncon", p.troncon}, {"reference", p.reference}, {"debut", p.debut}, {"fin", p.fin}, {"decalage", p.decalage}, {"texte", p.texte}});
  return j;
}

Cablage cablageDepuisJson(const Json& j) {
  Cablage h;
  h.uuid = j.value("uuid", std::string());
  h.nom = j.value("nom", std::string("Faisceau"));
  h.foisonnement = j.value("foisonnement", 1.0);
  h.surlongueur = j.value("surlongueur", 50.0);
  auto texte = [](const Json& x, const char* k) { return x.value(k, std::string()); };
  for (const auto& x : j.value("connecteurs", Json::array())) {
    ConnecteurCablage k;
    k.uuid = texte(x, "uuid");
    k.repere = texte(x, "repere");
    k.reference = texte(x, "reference");
    k.carte = x.value("carte", false);
    k.conception = x.value("conception", -1);
    k.composant = texte(x, "composant");
    k.etanche = x.value("etanche", false);
    k.position = {x.value("x", ecad::Nm{0}), x.value("y", ecad::Nm{0})};
    for (const auto& c : x.value("cavites", Json::array()))
      k.cavites.push_back({texte(c, "numero"), texte(c, "reseau"), texte(c, "contact"), texte(c, "joint"), texte(c, "bouchon"), c.value("denudage", 0.0), c.value("recul", 0.0)});
    h.connecteurs.push_back(std::move(k));
  }
  for (const auto& x : j.value("cables", Json::array()))
    h.cables.push_back({texte(x, "uuid"), texte(x, "reference"), texte(x, "designation"), texte(x, "de"), texte(x, "vers"), x.value("longueur", 0.0), provenanceDepuis(texte(x, "provenance"))});
  for (const auto& x : j.value("fils", Json::array())) {
    FilCablage f;
    f.uuid = texte(x, "uuid");
    f.numero = texte(x, "numero");
    f.section = x.value("section", 0.5);
    f.couleur = texte(x, "couleur");
    f.cable = texte(x, "cable");
    f.conducteur = x.value("conducteur", -1);
    f.paire = x.value("paire", 0);
    f.drain = x.value("drain", false);
    f.de = texte(x, "de");
    f.deCavite = texte(x, "deCavite");
    f.vers = texte(x, "vers");
    f.versCavite = texte(x, "versCavite");
    f.surFil = texte(x, "surFil");
    f.reseau = texte(x, "reseau");
    f.cavalier = x.value("cavalier", false);
    f.longueur = x.value("longueur", 0.0);
    f.provenance = provenanceDepuis(texte(x, "provenance"));
    f.longueurForcee = x.value("longueurForcee", 0.0);
    if (x.contains("chemin") && x["chemin"].is_array())
      for (const auto& c : x["chemin"])
        if (c.is_string()) f.chemin.push_back(c.get<std::string>());
    h.fils.push_back(std::move(f));
  }
  for (const auto& x : j.value("epissures", Json::array())) {
    EpissureCablage e{texte(x, "uuid"), texte(x, "repere"), {}, texte(x, "noeud")};
    for (const auto& f : x.value("fils", Json::array()))
      if (f.is_string()) e.fils.push_back(f.get<std::string>());
    h.epissures.push_back(std::move(e));
  }
  for (const auto& x : j.value("noeuds", Json::array())) h.noeuds.push_back({texte(x, "uuid"), texte(x, "nom"), texte(x, "connecteur"), x.value("x", 0.0), x.value("y", 0.0)});
  for (const auto& x : j.value("troncons", Json::array())) h.troncons.push_back({texte(x, "uuid"), texte(x, "a"), texte(x, "b"), x.value("longueur", 0.0), x.value("coupure", false)});
  for (const auto& x : j.value("protections", Json::array()))
    h.protections.push_back({texte(x, "troncon"), texte(x, "reference"), x.value("debut", 0.0), x.value("fin", 0.0), x.value("decalage", 0.0), texte(x, "texte")});
  return h;
}

std::vector<Cablage> cablages(const ProjetElectrique& p) {
  std::vector<Cablage> r;
  const auto it = p.schema.extensions.find("cablage");
  if (it == p.schema.extensions.end() || !it->second.is_array()) return r;
  for (const auto& x : it->second)
    if (x.is_object()) r.push_back(cablageDepuisJson(x));
  return r;
}

void ecrireCablages(ProjetElectrique& p, const std::vector<Cablage>& v) {
  if (v.empty()) {
    p.schema.extensions.erase("cablage");
    return;
  }
  Json a = Json::array();
  for (const auto& h : v) a.push_back(cablageVersJson(h));
  p.schema.extensions["cablage"] = a;
}

// ======================================================================================================================
// Connecteurs, câbles, fils (19.10)
// ======================================================================================================================
std::optional<ConnecteurCablage> connecteurDeCarte(const ecad::ConceptionElec& carte, int conception, const std::string& repere) {
  const ecad::ComposantElec* k = carte.parReference(repere);
  if (!k) return std::nullopt;
  const ecad::DefSymbole* d = carte.symbole(k->symbole);
  if (!d) return std::nullopt;
  ConnecteurCablage r;
  r.uuid = ecad::nouvelUuid();
  r.repere = repere;
  r.carte = true;
  r.conception = conception;
  r.composant = k->uuid;
  r.reference = k->champs.count("contrepartie") ? k->champs.at("contrepartie") : std::string();
  std::vector<std::string> numeros;
  for (const auto& b : d->broches) numeros.push_back(b.numero);
  std::sort(numeros.begin(), numeros.end(), ordreNaturelCablage);
  numeros.erase(std::unique(numeros.begin(), numeros.end()), numeros.end());
  const ecad::Connectivite kc = ecad::connectivite(carte);
  for (const auto& n : numeros) {
    CaviteCablage c;
    c.numero = n;
    const int i = kc.reseau(k->uuid, n);
    if (i >= 0) c.reseau = kc.reseaux[static_cast<std::size_t>(i)].nom;
    r.cavites.push_back(c);
  }
  return r;
}

ConnecteurCablage connecteurDuSchema(const ProjetElectrique& p, const std::string& repere, int cavites) {
  ConnecteurCablage r;
  r.uuid = ecad::nouvelUuid();
  r.repere = repere;
  const std::string sansTiret = !repere.empty() && repere[0] == '-' ? repere.substr(1) : repere;
  const ecad::ComposantElec* k = nullptr;
  for (const auto& c : p.schema.composants)
    if (c.reference == sansTiret || c.reference == repere) k = &c;
  if (k) {
    r.composant = k->uuid;
    std::vector<std::string> numeros;
    const ecad::Connectivite kc = ecad::connectivite(p.schema);
    std::map<std::string, std::string> reseaux;
    for (const auto& f : p.schema.feuilles)
      for (const auto& s : f.symboles) {
        if (s.composant != k->uuid) continue;
        const ecad::DefSymbole* d = detail::definitionDe(p, s.symbole);
        if (!d) continue;
        for (const auto& b : d->broches) {
          const std::string n = ecad::numeroBroche(s, b.numero);
          if (std::find(numeros.begin(), numeros.end(), n) != numeros.end()) continue;
          numeros.push_back(n);
          const int i = kc.reseau(k->uuid, n);
          if (i >= 0 && kc.reseaux[static_cast<std::size_t>(i)].nomme) reseaux[n] = kc.reseaux[static_cast<std::size_t>(i)].nom;
        }
      }
    std::sort(numeros.begin(), numeros.end(), ordreNaturelCablage);
    for (const auto& n : numeros) r.cavites.push_back({n, reseaux.count(n) ? reseaux[n] : std::string(), {}, {}, {}, 0, 0});
  }
  for (int i = static_cast<int>(r.cavites.size()); i < cavites; ++i) r.cavites.push_back({std::to_string(i + 1), {}, {}, {}, {}, 0, 0});
  return r;
}

namespace {
std::string numeroSuivant(const Cablage& h) {
  long long m = 0;
  for (const auto& f : h.fils) {
    char* fin = nullptr;
    const long long v = std::strtoll(f.numero.c_str(), &fin, 10);
    if (fin && *fin == '\0') m = std::max(m, v);
  }
  return std::to_string(m + 1);
}
bool caviteOccupee(const Cablage& h, const std::string& connecteur, const std::string& cavite) {
  return std::any_of(h.fils.begin(), h.fils.end(), [&](const FilCablage& f) { return (f.de == connecteur && f.deCavite == cavite) || (f.vers == connecteur && f.versCavite == cavite); });
}
// Cavités à prendre dans l'ordre : celles imposées, puis les libres
struct Distributeur {
  const Cablage& h;
  const ConnecteurCablage& k;
  std::vector<std::string> imposees;
  std::size_t i = 0;
  std::set<std::string> prises;
  std::string suivante() {
    while (i < imposees.size()) {
      const std::string c = imposees[i++];
      if (!c.empty()) {
        prises.insert(c);
        return c;
      }
    }
    for (const auto& c : k.cavites)
      if (!prises.count(c.numero) && !caviteOccupee(h, k.repere, c.numero)) {
        prises.insert(c.numero);
        return c.numero;
      }
    return {};
  }
};
std::string reseauDe(const Cablage& h, const std::string& connecteur, const std::string& cavite) {
  const ConnecteurCablage* k = h.connecteur(connecteur);
  if (!k || !k->carte) return {};
  const CaviteCablage* c = k->cavite(cavite);
  return c ? c->reseau : std::string();
}
}  // namespace

std::string poserCable(Cablage& h, const BibliothequeCablage& b, const std::string& designation, const std::string& de, const std::string& vers, const std::vector<std::string>& cavitesDe,
                       const std::vector<std::string>& cavitesVers) {
  const EntreeCable* e = b.cable(designation);
  const ConnecteurCablage* A = h.connecteur(de);
  const ConnecteurCablage* B = h.connecteur(vers);
  if (!e || !A || !B) return {};
  int n = 0;
  for (const auto& c : h.cables)
    if (c.reference.size() > 1 && c.reference[0] == 'W') n = std::max(n, std::atoi(c.reference.c_str() + 1));
  CableCablage w;
  w.uuid = ecad::nouvelUuid();
  w.reference = "W" + std::to_string(n + 1);
  w.designation = designation;
  w.de = A->repere;
  w.vers = B->repere;
  Distributeur da{h, *A, cavitesDe}, db{h, *B, cavitesVers};
  const auto conducteurs = conducteursCable(*e);
  std::vector<FilCablage> nouveaux;
  for (std::size_t i = 0; i < conducteurs.size(); ++i) {
    const ConducteurBib& k = conducteurs[i];
    FilCablage f;
    f.uuid = ecad::nouvelUuid();
    f.section = e->section;
    f.couleur = k.drain ? std::string() : k.couleur;
    f.cable = w.uuid;
    f.conducteur = static_cast<int>(i);
    f.paire = k.paire;
    f.drain = k.drain;
    if (!k.drain) {
      f.de = A->repere;
      f.deCavite = da.suivante();
      if (f.deCavite.empty()) f.de.clear();
    }
    f.versCavite = db.suivante();
    if (!f.versCavite.empty()) f.vers = B->repere;
    f.reseau = reseauDe(h, f.de, f.deCavite);
    if (f.reseau.empty()) f.reseau = reseauDe(h, f.vers, f.versCavite);
    nouveaux.push_back(std::move(f));
  }
  for (auto& f : nouveaux) {
    f.numero = numeroSuivant(h);
    h.fils.push_back(std::move(f));
  }
  h.cables.push_back(w);
  return w.reference;
}

std::string poserFil(Cablage& h, double section, const std::string& couleur, const std::string& de, const std::string& deCavite, const std::string& vers, const std::string& versCavite,
                     const std::string& surFil) {
  FilCablage f;
  f.uuid = ecad::nouvelUuid();
  f.numero = numeroSuivant(h);
  f.section = section;
  f.couleur = couleur;
  f.de = de;
  f.deCavite = deCavite;
  f.vers = vers;
  f.versCavite = versCavite;
  f.surFil = vers.empty() ? surFil : std::string();
  f.reseau = reseauDe(h, de, deCavite);
  if (f.reseau.empty()) f.reseau = reseauDe(h, vers, versCavite);
  if (f.reseau.empty() && !f.surFil.empty())
    if (const FilCablage* o = h.fil(f.surFil)) f.reseau = o->reseau;
  h.fils.push_back(f);
  return f.numero;
}

std::string poserCavalier(Cablage& h, const std::string& connecteur, const std::string& a, const std::string& b, double section, double longueur) {
  const std::string n = poserFil(h, section, "BK", connecteur, a, connecteur, b);
  if (FilCablage* f = h.fil(n)) {
    f->cavalier = true;
    f->longueur = longueur;
    f->provenance = ProvenanceLongueur::Saisie;
  }
  return n;
}

int poserEpissures(Cablage& h) {
  for (const auto& f : h.fils) {
    if (f.surFil.empty() || !h.fil(f.surFil)) continue;
    // L'épissure de l'autre fil, s'il en a une ; sinon une nouvelle
    EpissureCablage* e = nullptr;
    for (auto& x : h.epissures)
      if (std::find(x.fils.begin(), x.fils.end(), f.surFil) != x.fils.end() || std::find(x.fils.begin(), x.fils.end(), f.numero) != x.fils.end()) e = &x;
    if (!e) {
      int n = 0;
      for (const auto& x : h.epissures)
        if (x.repere.size() > 2 && x.repere.rfind("SP", 0) == 0) n = std::max(n, std::atoi(x.repere.c_str() + 2));
      h.epissures.push_back({ecad::nouvelUuid(), "SP" + std::to_string(n + 1), {}, {}});
      e = &h.epissures.back();
    }
    for (const std::string& numero : {f.surFil, f.numero})
      if (std::find(e->fils.begin(), e->fils.end(), numero) == e->fils.end()) e->fils.push_back(numero);
  }
  return static_cast<int>(h.epissures.size());
}

int affecterContacts(Cablage& h, const BibliothequeCablage& b) {
  int servies = 0;
  for (auto& k : h.connecteurs) {
    const EntreeConnecteurCablage* e = k.reference.empty() ? nullptr : b.connecteur(k.reference);
    const bool etanche = k.etanche || (e && e->etanche);
    // Bornier (aucun boîtier de la bibliothèque, ni carte) : embouts de câblage
    const bool bornier = !e && !k.carte;
    for (auto& c : k.cavites) {
      const FilCablage* f = nullptr;
      for (const auto& x : h.fils)
        if ((x.de == k.repere && x.deCavite == c.numero) || (x.vers == k.repere && x.versCavite == c.numero)) f = &x;
      if (!f) {
        if (etanche && c.bouchon.empty() && e) c.bouchon = e->bouchon.empty() ? "bouchon" : e->bouchon;
        continue;
      }
      c.bouchon.clear();
      const EntreeContact* actuel = c.contact.empty() ? nullptr : b.contact(c.contact);
      const bool convient = actuel && f->section >= actuel->sectionMin - 1e-9 && f->section <= actuel->sectionMax + 1e-9;
      const EntreeContact* choisi = convient ? actuel : nullptr;
      if (!choisi) {
        if (bornier) {
          for (const auto& x : b.contacts)
            if (x.genre == "embout" && f->section >= x.sectionMin - 1e-9 && f->section <= x.sectionMax + 1e-9 && (!choisi || x.sectionMax - x.sectionMin < choisi->sectionMax - choisi->sectionMin))
              choisi = &x;
        } else
          choisi = contactPour(b, f->section, e);
      }
      if (!choisi) continue;
      c.contact = choisi->reference;
      if (c.denudage <= 0) c.denudage = choisi->denudage;
      if (c.recul <= 0) c.recul = choisi->recul;
      if (etanche && c.joint.empty()) c.joint = e && !e->joint.empty() ? e->joint : "joint intégré";
      ++servies;
    }
  }
  return servies;
}

int mettreAJourCables(Cablage& h, const BibliothequeCablage& b) {
  int n = 0;
  for (const auto& w : h.cables) {
    const EntreeCable* e = b.cable(w.designation);
    if (!e) continue;
    const auto conducteurs = conducteursCable(*e);
    for (auto& f : h.fils) {
      if (f.cable != w.uuid || f.conducteur < 0 || f.conducteur >= static_cast<int>(conducteurs.size())) continue;
      const ConducteurBib& k = conducteurs[static_cast<std::size_t>(f.conducteur)];
      const std::string couleur = k.drain ? std::string() : k.couleur;
      if (f.section == e->section && f.couleur == couleur && f.paire == k.paire && f.drain == k.drain) continue;
      f.section = e->section;
      f.couleur = couleur;
      f.paire = k.paire;
      f.drain = k.drain;
      ++n;
    }
  }
  return n;
}

std::vector<std::vector<std::string>> equipotentielles(const Cablage& h) {
  std::map<std::string, std::string> parent;
  auto racine = [&](std::string x) {
    if (!parent.count(x)) parent[x] = x;
    while (parent[x] != x) x = parent[x] = parent[parent[x]];
    return x;
  };
  auto unir = [&](const std::string& a, const std::string& b) { parent[racine(a)] = racine(b); };
  auto bout = [](const std::string& k, const std::string& c) { return k + ":" + c; };
  std::map<std::string, std::string> epissureDe;  // numéro de fil → repère de son épissure
  for (const auto& e : h.epissures)
    for (const auto& f : e.fils) epissureDe[f] = e.repere;
  for (const auto& f : h.fils) {
    const std::string ufil = "#" + f.numero;  // le fil lui-même : ses deux bouts y sont reliés
    racine(ufil);
    if (!f.de.empty() && !f.deCavite.empty()) unir(bout(f.de, f.deCavite), ufil);
    if (!f.vers.empty() && !f.versCavite.empty()) unir(bout(f.vers, f.versCavite), ufil);
    if (const auto it = epissureDe.find(f.numero); it != epissureDe.end()) unir(it->second, ufil);
    else if (!f.surFil.empty()) unir("#" + f.surFil, ufil);
  }
  std::map<std::string, std::vector<std::string>> groupes;
  for (const auto& [x, p] : parent)
    if (x[0] != '#') groupes[racine(x)].push_back(x);
  std::vector<std::vector<std::string>> r;
  for (auto& [k, v] : groupes) {
    std::sort(v.begin(), v.end(), ordreNaturelCablage);
    r.push_back(v);
  }
  std::sort(r.begin(), r.end(), [](const auto& a, const auto& b) { return ordreNaturelCablage(a.front(), b.front()); });
  return r;
}

std::vector<LigneFil> listeFils(const Cablage& h, const BibliothequeCablage& b) {
  std::vector<LigneFil> r;
  std::map<std::string, std::string> epissureDe;
  for (const auto& e : h.epissures)
    for (const auto& f : e.fils) epissureDe[f] = e.repere;
  for (const auto& f : h.fils) {
    LigneFil l;
    l.numero = f.numero;
    l.de = f.de.empty() ? std::string() : f.de + ":" + f.deCavite;
    l.vers = f.vers.empty() ? std::string() : f.vers + ":" + f.versCavite;
    if (l.vers.empty() && epissureDe.count(f.numero)) l.vers = epissureDe[f.numero];
    else if (l.vers.empty() && !f.surFil.empty()) l.vers = "fil " + f.surFil;
    l.couleur = f.couleur;
    for (const auto& w : h.cables)
      if (w.uuid == f.cable) l.cable = w.reference;
    l.reseau = f.reseau;
    l.section = f.section;
    l.longueur = f.longueur;
    l.provenance = nomProvenance(f.provenance);
    auto contact = [&](const std::string& k, const std::string& c, std::string& ref, double& den) {
      const ConnecteurCablage* x = h.connecteur(k);
      const CaviteCablage* y = x ? x->cavite(c) : nullptr;
      if (!y) return;
      ref = y->contact;
      den = y->denudage;
      if (den <= 0)
        if (const EntreeContact* e = b.contact(y->contact)) den = e->denudage;
    };
    contact(f.de, f.deCavite, l.contactDe, l.denudageDe);
    contact(f.vers, f.versCavite, l.contactVers, l.denudageVers);
    l.paire = f.paire;
    l.drain = f.drain;
    l.cavalier = f.cavalier;
    r.push_back(l);
  }
  std::sort(r.begin(), r.end(), [](const LigneFil& a, const LigneFil& c) { return ordreNaturelCablage(a.numero, c.numero); });
  return r;
}

}  // namespace prom::elec
