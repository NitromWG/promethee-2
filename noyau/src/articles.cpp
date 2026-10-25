// Prométhée : articles, recherche, empreintes compatibles, IPC-7351B (lot L4). Voir articles.hpp.
// Licence GPL-3.0-only.
#include "promethee/articles.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <regex>

#include "promethee/archive.hpp"
#include "promethee/formules.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/remplacement.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

using J = nlohmann::ordered_json;

namespace {
std::string txt(const J& o, const char* k) { return o.is_object() && o.contains(k) && o[k].is_string() ? o[k].get<std::string>() : std::string(); }
std::map<std::string, std::string> chaines(const J& o, const char* k) {
  std::map<std::string, std::string> r;
  if (o.is_object() && o.contains(k) && o[k].is_object())
    for (auto it = o[k].begin(); it != o[k].end(); ++it)
      if (it.value().is_string()) r[it.key()] = it.value().get<std::string>();
  return r;
}
}  // namespace

// ---------- Fichier ----------
J articleVersJson(const Article& a) {
  J o = {{"id", a.id}, {"nom", a.nom}};
  auto mettre = [&](const char* k, const std::string& v) {
    if (!v.empty()) o[k] = v;
  };
  mettre("description", a.description);
  mettre("categorie", a.categorie);
  mettre("symbole", a.symbole);
  mettre("empreinte", a.empreinte);
  mettre("valeur", a.valeur);
  mettre("fabricant", a.fabricant);
  mettre("mpn", a.mpn);
  mettre("fiche", a.fiche);
  mettre("lcsc", a.lcsc);
  mettre("modele3D", a.modele3D);
  if (!a.brochage.empty()) o["brochage"] = a.brochage;
  if (!a.parametres.empty()) o["parametres"] = a.parametres;
  if (a.hauteur > 0) o["hauteur"] = a.hauteur;
  mettre("base", a.base);
  if (!a.nonHerites.empty()) o["nonHerites"] = std::vector<std::string>(a.nonHerites.begin(), a.nonHerites.end());
  if (!a.offres.empty()) {
    J t = J::array();
    for (const auto& x : a.offres) {
      J p = J::array();
      for (const auto& [q, prix] : x.prix) p.push_back(J::array({q, prix}));
      t.push_back({{"distributeur", x.distributeur}, {"reference", x.reference}, {"devise", x.devise}, {"conditionnement", x.conditionnement}, {"stock", x.stock}, {"prix", p}});
    }
    o["offres"] = t;
  }
  if (!a.secondes.empty()) {
    J t = J::array();
    for (const auto& x : a.secondes) t.push_back({{"fabricant", x.fabricant}, {"mpn", x.mpn}, {"cycleVie", x.cycleVie}, {"rang", x.rang}});
    o["secondes"] = t;
  }
  if (a.generique) o["generique"] = true;
  mettre("exigences", a.exigences);
  mettre("etatCycle", a.etatCycle);
  mettre("numeroInterne", a.numeroInterne);
  mettre("licence", a.licence);
  if (!a.modeleSpice.vide()) {
    const ModeleSpice& m = a.modeleSpice;
    J x = {{"texte", m.texte}, {"nom", m.nom}};
    if (!m.broches.empty()) x["broches"] = m.broches;
    for (const auto& [k, v] : {std::pair<const char*, const std::string*>{"compatibilite", &m.compatibilite}, {"fichier", &m.fichier}, {"genre", &m.genre}, {"source", &m.source}})
      if (!v->empty()) x[k] = *v;
    o["modeleSpice"] = x;
  }
  o["version"] = a.version;
  return o;
}

Article articleDepuisJson(const J& o) {
  Article a;
  if (!o.is_object()) return a;
  a.id = txt(o, "id");
  a.nom = txt(o, "nom");
  a.description = txt(o, "description");
  a.categorie = txt(o, "categorie");
  a.symbole = txt(o, "symbole");
  a.empreinte = txt(o, "empreinte");
  a.valeur = txt(o, "valeur");
  a.fabricant = txt(o, "fabricant");
  a.mpn = txt(o, "mpn");
  a.fiche = txt(o, "fiche");
  a.lcsc = txt(o, "lcsc");
  a.modele3D = txt(o, "modele3D");
  a.brochage = chaines(o, "brochage");
  a.parametres = chaines(o, "parametres");
  if (o.contains("hauteur") && o["hauteur"].is_number()) a.hauteur = std::max(0.0, o["hauteur"].get<double>());
  a.base = txt(o, "base");
  if (o.contains("nonHerites") && o["nonHerites"].is_array())
    for (const auto& x : o["nonHerites"])
      if (x.is_string()) a.nonHerites.insert(x.get<std::string>());
  if (o.contains("offres") && o["offres"].is_array())
    for (const auto& x : o["offres"]) {
      if (!x.is_object()) continue;
      OffreArticle f;
      f.distributeur = txt(x, "distributeur");
      f.reference = txt(x, "reference");
      if (const std::string d = txt(x, "devise"); !d.empty()) f.devise = d;
      if (x.contains("conditionnement") && x["conditionnement"].is_number_integer()) f.conditionnement = std::max(1, x["conditionnement"].get<int>());
      if (x.contains("stock") && x["stock"].is_number_integer()) f.stock = x["stock"].get<int>();
      if (x.contains("prix") && x["prix"].is_array())
        for (const auto& p : x["prix"])
          if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number()) f.prix.push_back({p[0].get<int>(), p[1].get<double>()});
      a.offres.push_back(f);
    }
  if (o.contains("secondes") && o["secondes"].is_array())
    for (const auto& x : o["secondes"]) {
      if (!x.is_object()) continue;
      SecondeSource s;
      s.fabricant = txt(x, "fabricant");
      s.mpn = txt(x, "mpn");
      if (const std::string v = txt(x, "cycleVie"); !v.empty()) s.cycleVie = v;
      if (x.contains("rang") && x["rang"].is_number_integer()) s.rang = x["rang"].get<int>();
      a.secondes.push_back(s);
    }
  a.generique = o.contains("generique") && o["generique"].is_boolean() && o["generique"].get<bool>();
  a.exigences = txt(o, "exigences");
  a.etatCycle = txt(o, "etatCycle");
  a.numeroInterne = txt(o, "numeroInterne");
  a.licence = txt(o, "licence");
  if (o.contains("modeleSpice") && o["modeleSpice"].is_object()) {
    const J& x = o["modeleSpice"];
    ModeleSpice& m = a.modeleSpice;
    m.texte = txt(x, "texte");
    m.nom = txt(x, "nom");
    if (x.contains("broches") && x["broches"].is_array())
      for (const auto& b : x["broches"])
        if (b.is_string()) m.broches.push_back(b.get<std::string>());
    m.compatibilite = txt(x, "compatibilite");
    m.fichier = txt(x, "fichier");
    m.genre = txt(x, "genre");
    m.source = txt(x, "source");
  }
  if (o.contains("version") && o["version"].is_number_integer()) a.version = std::max(1, o["version"].get<int>());
  return a;
}

// ---------- Fichier d'articles ----------
std::vector<Article> lireArticles(const std::string& contenu, std::string* erreur) {
  std::vector<Article> r;
  try {
    const auto j = nlohmann::ordered_json::parse(contenu);
    if (!j.is_object() || !j.contains("articles") || !j["articles"].is_array()) {
      if (erreur) *erreur = "ce n’est pas un fichier d’articles Prométhée";
      return r;
    }
    for (const auto& x : j["articles"])
      if (x.is_object()) {
        Article a = articleDepuisJson(x);
        if (!a.id.empty()) r.push_back(std::move(a));
      }
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
  }
  return r;
}

std::string texteArticles(const std::vector<Article>& articles) {
  nlohmann::ordered_json j = {{"format", "promethee-articles"}, {"version", 1}, {"articles", nlohmann::ordered_json::array()}};
  for (const auto& a : articles) j["articles"].push_back(articleVersJson(a));
  return j.dump(1) + "\n";
}

// ---------- Bibliothèque intégrée ----------
namespace {
Article art(const std::string& id, const std::string& nom, const std::string& categorie, const std::string& symbole, const std::string& empreinte, const std::string& valeur,
            const std::string& fabricant, const std::string& mpn, const std::string& description, std::map<std::string, std::string> brochage = {}, double hauteur = 0) {
  Article a;
  a.id = id;
  a.nom = nom;
  a.categorie = categorie;
  a.symbole = symbole;
  a.empreinte = empreinte;
  a.valeur = valeur;
  a.fabricant = fabricant;
  a.mpn = mpn;
  a.description = description;
  a.brochage = std::move(brochage);
  a.hauteur = hauteur;
  return a;
}

std::vector<Article> construireArticles() {
  std::vector<Article> v;
  // Résistances E24 en 0603, de 10 Ω à 1 MΩ (1 %, 0,1 W) : 121 articles, nom et référence d'après leur gabarit
  for (int decade = 1; decade <= 5; ++decade)
    for (double m : serieE(24)) {
      const double ohms = m * std::pow(10.0, decade);
      const std::string val = ecrireCompact(ohms, FamilleValeur::Resistance);
      Article a = art("R_" + val + "_0603", "Résistance " + val + " 0603", "Résistances", "R", "R_0603", val, "Yageo", "RC0603FR-07" + val + "L",
                      "Résistance CMS " + val + ", 1 %, 0,1 W, 0603");
      a.parametres = {{"tolerance", "1 %"}, {"puissance", "0,1 W"}};
      v.push_back(a);
    }
  // Résistances E12 en 0402, de 10 Ω à 820 kΩ (1 %, 0,063 W)
  for (int decade = 1; decade <= 5; ++decade)
    for (double m : serieE(12)) {
      const double ohms = m * std::pow(10.0, decade);
      const std::string val = ecrireCompact(ohms, FamilleValeur::Resistance);
      Article a = art("R_" + val + "_0402", "Résistance " + val + " 0402", "Résistances", "R", "R_0402", val, "Yageo", "RC0402FR-07" + val + "L",
                      "Résistance CMS " + val + ", 1 %, 0,063 W, 0402");
      a.parametres = {{"tolerance", "1 %"}, {"puissance", "0,063 W"}};
      v.push_back(a);
    }
  {
    Article a = art("R_1M_0603", "Résistance 1M 0603", "Résistances", "R", "R_0603", "1M", "Yageo", "RC0603FR-071ML", "Résistance CMS 1M, 1 %, 0,1 W, 0603");
    a.parametres = {{"tolerance", "1 %"}, {"puissance", "0,1 W"}};
    v.push_back(a);
  }
  // Condensateurs céramiques E6 de 1 pF à 1 µF en 0603 (50 V, C0G jusqu'à 1 nF, X7R au-delà) : 37 articles
  for (int e = -12; e <= -7; ++e)
    for (double m : serieE(6)) {
      const double f = m * std::pow(10.0, e);
      if (f > 1.01e-6) continue;
      const std::string val = ecrireCompact(f, FamilleValeur::Condensateur);
      const bool c0g = f <= 1e-9;
      Article a = art("C_" + val + "_0603", "Condensateur " + val + " 0603", "Condensateurs", "C", "C_0603", val, "Murata", std::string("GRM188") + (c0g ? "5C1H" : "R71H"),
                      "Condensateur céramique " + val + ", 50 V, " + (c0g ? "C0G" : "X7R") + ", 0603");
      a.parametres = {{"tension", "50 V"}, {"dielectrique", c0g ? "C0G" : "X7R"}};
      v.push_back(a);
    }
  for (const char* val : {"1µF", "2,2µF", "4,7µF", "10µF", "22µF"}) {
    Article a = art(std::string("C_") + val + "_0805", std::string("Condensateur ") + val + " 0805", "Condensateurs", "C", "C_0805", val, "Samsung", "CL21A", std::string("Condensateur céramique ") + val + ", 25 V, X5R, 0805");
    a.parametres = {{"tension", "25 V"}, {"dielectrique", "X5R"}};
    v.push_back(a);
  }
  for (const char* val : {"10µF", "47µF", "100µF", "220µF", "470µF", "1000µF"}) {
    const std::string emp = std::string(val) == "1000µF" ? "CP_Radial_D10.0_P5.00" : std::string(val) == "470µF" ? "CP_Radial_D8.0_P3.50" : std::string(val) == "220µF" ? "CP_Radial_D6.3_P2.50" : "CP_Radial_D5.0_P2.00";
    Article a = art(std::string("CP_") + val, std::string("Condensateur chimique ") + val, "Condensateurs", "C_Pol", emp, val, "Panasonic", "EEU-FR", std::string("Condensateur électrolytique ") + val + ", 25 V");
    a.parametres = {{"tension", "25 V"}};
    v.push_back(a);
  }
  // Inductances : E6 de 1 µH à 100 µH, en 0805 et en 1210
  for (const char* boitier : {"0805", "1210"})
    for (int e = -6; e <= -5; ++e)
      for (double m : serieE(6)) {
        const double h = m * std::pow(10.0, e);
        const std::string val = ecrireCompact(h, FamilleValeur::Inductance);
        v.push_back(art("L_" + val + "_" + boitier, "Inductance " + val + " " + boitier, "Inductances", "L", std::string("L_") + boitier, val, "TDK", std::string(boitier) == "0805" ? "MLZ2012" : "NLV32T",
                        "Inductance CMS " + val + ", " + boitier));
      }
  for (const char* val : {"100µH"}) v.push_back(art(std::string("L_") + val + "_1210", std::string("Inductance ") + val + " 1210", "Inductances", "L", "L_1210", val, "TDK", "NLV32T-101J", "Inductance CMS 100 µH, 1210"));
  // Ferrites, fusibles
  for (const char* val : {"600R@100MHz", "120R@100MHz"}) v.push_back(art(std::string("FB_") + val, std::string("Ferrite ") + val, "Inductances", "L", "L_0603", val, "Murata", "BLM18", std::string("Perle de ferrite ") + val + ", 0603"));
  for (const char* val : {"500mA", "1A", "2A", "3A"}) v.push_back(art(std::string("F_") + val + "_1206", std::string("Fusible ") + val + " 1206", "Fusibles", "Fusible", "Fuse_1206", val, "Littelfuse", "0466", std::string("Fusible CMS rapide ") + val + ", 1206"));
  // Diodes et LED (broche 1 : cathode)
  v.push_back(art("1N4148W", "1N4148W", "Diodes", "D", "D_SOD-123", "1N4148W", "onsemi", "1N4148W", "Diode de commutation 100 V 150 mA, SOD-123"));
  v.push_back(art("BAT54", "BAT54", "Diodes", "D_Schottky", "SOT-23", "BAT54", "Nexperia", "BAT54", "Diode Schottky 30 V 200 mA, SOT-23 (1 A, 3 K)", {{"1", "3"}, {"2", "1"}, {"NC", "2"}}));
  v.push_back(art("SS14", "SS14", "Diodes", "D_Schottky", "D_SMA", "SS14", "onsemi", "SS14", "Diode Schottky 40 V 1 A, SMA"));
  v.push_back(art("1N5819", "1N5819", "Diodes", "D_Schottky", "D_DO-41", "1N5819", "onsemi", "1N5819", "Diode Schottky 40 V 1 A, DO-41"));
  v.push_back(art("1N4007", "1N4007", "Diodes", "D", "D_DO-41", "1N4007", "onsemi", "1N4007", "Diode de redressement 1000 V 1 A, DO-41"));
  v.push_back(art("BZT52C3V3", "BZT52C3V3", "Diodes", "D_Zener", "D_SOD-123", "3V3", "Diotec", "BZT52C3V3", "Diode Zener 3,3 V, SOD-123"));
  v.push_back(art("BZT52C5V1", "BZT52C5V1", "Diodes", "D_Zener", "D_SOD-123", "5V1", "Diotec", "BZT52C5V1", "Diode Zener 5,1 V, SOD-123"));
  for (const auto& [couleur, ref] : std::vector<std::pair<std::string, std::string>>{{"Rouge", "150080RS75000"}, {"Vert", "150080GS75000"}, {"Bleu", "150080BS75000"}, {"Jaune", "150080YS75000"}, {"Blanc", "150080WS75000"}})
    v.push_back(art("LED_" + couleur + "_0805", "LED " + couleur + " 0805", "LED", "LED", "LED_0805", couleur, "Würth Elektronik", ref, "LED " + couleur + " 0805 (broche 1 : cathode)"));
  v.push_back(art("LED_Rouge_5mm", "LED rouge 5 mm", "LED", "LED", "LED_D5.0", "Rouge", "Kingbright", "L-53HD", "LED rouge 5 mm traversante"));
  // Transistors : brochage écrit pour le boîtier de l'article
  v.push_back(art("2N7002", "2N7002", "Transistors", "Q_NMOS", "SOT-23", "2N7002", "Nexperia", "2N7002", "MOSFET canal N 60 V 300 mA, SOT-23 (1 G, 2 S, 3 D)", {{"1", "1"}, {"2", "2"}, {"3", "3"}}));
  v.push_back(art("BSS138", "BSS138", "Transistors", "Q_NMOS", "SOT-23", "BSS138", "onsemi", "BSS138", "MOSFET canal N 50 V 220 mA, SOT-23 (1 G, 2 S, 3 D)", {{"1", "1"}, {"2", "2"}, {"3", "3"}}));
  v.push_back(art("AO3400A", "AO3400A", "Transistors", "Q_NMOS", "SOT-23", "AO3400A", "Alpha & Omega", "AO3400A", "MOSFET canal N 30 V 5,7 A, SOT-23 (1 G, 2 S, 3 D)", {{"1", "1"}, {"2", "2"}, {"3", "3"}}));
  v.push_back(art("AO3401A", "AO3401A", "Transistors", "Q_PMOS", "SOT-23", "AO3401A", "Alpha & Omega", "AO3401A", "MOSFET canal P 30 V 4 A, SOT-23 (1 G, 2 S, 3 D)", {{"1", "1"}, {"2", "2"}, {"3", "3"}}));
  // Le symbole Q_NMOS porte G 1, S 2, D 3 : en TO-220, G 1, D 2, S 3
  v.push_back(art("IRLZ44N", "IRLZ44N", "Transistors", "Q_NMOS", "TO-220-3", "IRLZ44N", "Infineon", "IRLZ44NPBF", "MOSFET canal N 55 V 47 A, niveau logique, TO-220 (1 G, 2 D, 3 S)", {{"1", "1"}, {"3", "2"}, {"2", "3"}}, 16.5));
  v.push_back(art("IRF540N", "IRF540N", "Transistors", "Q_NMOS", "TO-220-3", "IRF540N", "Infineon", "IRF540NPBF", "MOSFET canal N 100 V 33 A, TO-220 (1 G, 2 D, 3 S)", {{"1", "1"}, {"3", "2"}, {"2", "3"}}, 16.5));
  v.push_back(art("BC847", "BC847", "Transistors", "Q_NPN", "SOT-23", "BC847", "Nexperia", "BC847B", "Transistor NPN 45 V 100 mA, SOT-23 (1 B, 2 E, 3 C)", {{"1", "1"}, {"2", "2"}, {"3", "3"}}));
  v.push_back(art("BC857", "BC857", "Transistors", "Q_PNP", "SOT-23", "BC857", "Nexperia", "BC857B", "Transistor PNP 45 V 100 mA, SOT-23 (1 B, 2 E, 3 C)", {{"1", "1"}, {"2", "2"}, {"3", "3"}}));
  v.push_back(art("MMBT3904", "MMBT3904", "Transistors", "Q_NPN", "SOT-23", "MMBT3904", "onsemi", "MMBT3904", "Transistor NPN 40 V 200 mA, SOT-23 (1 B, 2 E, 3 C)", {{"1", "1"}, {"2", "2"}, {"3", "3"}}));
  v.push_back(art("MMBT3906", "MMBT3906", "Transistors", "Q_PNP", "SOT-23", "MMBT3906", "onsemi", "MMBT3906", "Transistor PNP 40 V 200 mA, SOT-23 (1 B, 2 E, 3 C)", {{"1", "1"}, {"2", "2"}, {"3", "3"}}));
  // Régulateurs (symbole « Regulateur » : 1 GND, 2 VO, 3 VI)
  v.push_back(art("AMS1117-3.3", "AMS1117-3.3", "Régulateurs", "Regulateur", "SOT-223", "AMS1117-3.3", "Advanced Monolithic Systems", "AMS1117-3.3", "Régulateur linéaire 3,3 V 1 A, SOT-223"));
  v.push_back(art("AMS1117-5.0", "AMS1117-5.0", "Régulateurs", "Regulateur", "SOT-223", "AMS1117-5.0", "Advanced Monolithic Systems", "AMS1117-5.0", "Régulateur linéaire 5 V 1 A, SOT-223"));
  v.push_back(art("LM7805", "LM7805", "Régulateurs", "LM7805", "TO-220-3", "LM7805", "STMicroelectronics", "L7805CV", "Régulateur linéaire 5 V 1,5 A, TO-220", {}, 16.5));
  // Circuits intégrés et microcontrôleurs
  v.push_back(art("NE555", "NE555", "Circuits intégrés", "NE555", "SOIC-8", "NE555", "Texas Instruments", "NE555DR", "Temporisateur 555, SOIC-8"));
  v.push_back(art("LM358", "LM358", "Circuits intégrés", "LM358_unites", "SOIC-8", "LM358", "Texas Instruments", "LM358DR", "Double amplificateur opérationnel, SOIC-8"));
  v.push_back(art("ATtiny85", "ATtiny85", "Microcontrôleurs", "ATtiny85", "SOIC-8", "ATtiny85", "Microchip", "ATTINY85-20SU", "Microcontrôleur AVR 8 bits, SOIC-8"));
  v.push_back(art("74HC00", "74HC00", "Logique", "74HC00_unites", "SOIC-14", "74HC00", "Nexperia", "74HC00D", "Quatre portes NON-ET, SOIC-14"));
  // Quartz, boutons, connecteurs
  for (const char* f : {"8MHz", "12MHz", "16MHz", "20MHz"}) v.push_back(art(std::string("Y_") + f, std::string("Quartz ") + f, "Quartz", "Quartz", "Crystal_HC49-4H", f, "Abracon", "ABLS", std::string("Quartz ") + f + ", HC-49"));
  v.push_back(art("SW_6mm", "Bouton 6 mm", "Électromécanique", "Bouton", "SW_PUSH_6mm", "Bouton", "C&K", "PTS645", "Bouton poussoir 6 × 6 mm traversant"));
  for (int n : {2, 3, 4, 5, 6, 8, 10}) {
    char t[64];
    std::snprintf(t, sizeof t, "PinHeader_1x%02d_P2.54", n);
    char sym[32];
    std::snprintf(sym, sizeof sym, "Conn_01x%02d", n);
    v.push_back(art("HDR_1x" + std::to_string(n), "Barrette 1×" + std::to_string(n), "Connecteurs", sym, t, "Conn_1x" + std::to_string(n), "Amphenol", "10129378", "Barrette mâle 1 × " + std::to_string(n) + ", pas 2,54 mm"));
  }
  return v;
}
}  // namespace

const std::vector<Article>& bibliothequeArticles() {
  static const std::vector<Article> v = construireArticles();
  return v;
}

const Article* articleIntegre(const std::string& id) {
  for (const auto& a : bibliothequeArticles())
    if (a.id == id) return &a;
  return nullptr;
}

Article resoudreArticle(const Article& a, const std::function<const Article*(const std::string&)>& trouver) {
  Article r = a;
  std::set<std::string> vus{a.id};
  for (const Article* b = a.base.empty() || !trouver ? nullptr : trouver(a.base); b && vus.insert(b->id).second; b = b->base.empty() ? nullptr : trouver(b->base)) {
    auto prendre = [&](const char* nom, std::string& champ, const std::string& deBase) {
      if (champ.empty() && !a.nonHerites.count(nom)) champ = deBase;
    };
    prendre("nom", r.nom, b->nom);
    prendre("description", r.description, b->description);
    prendre("categorie", r.categorie, b->categorie);
    prendre("symbole", r.symbole, b->symbole);
    prendre("empreinte", r.empreinte, b->empreinte);
    prendre("valeur", r.valeur, b->valeur);
    prendre("fabricant", r.fabricant, b->fabricant);
    prendre("mpn", r.mpn, b->mpn);
    prendre("fiche", r.fiche, b->fiche);
    prendre("lcsc", r.lcsc, b->lcsc);
    prendre("modele3D", r.modele3D, b->modele3D);
    prendre("licence", r.licence, b->licence);
    if (r.brochage.empty() && !a.nonHerites.count("brochage")) r.brochage = b->brochage;
    if (r.hauteur <= 0 && !a.nonHerites.count("hauteur")) r.hauteur = b->hauteur;
    if (r.modeleSpice.vide() && !a.nonHerites.count("modeleSpice")) r.modeleSpice = b->modeleSpice;
    for (const auto& [k, val] : b->parametres)
      if (!r.parametres.count(k) && !a.nonHerites.count("parametre:" + k)) r.parametres[k] = val;
  }
  return r;
}

// ---------- Symbole de l'article et pose ----------
DefSymbole symboleDeArticle(const Article& a, const DefSymbole& base) {
  DefSymbole d = base;
  if (!a.empreinte.empty()) d.empreinte = a.empreinte;
  if (!a.fabricant.empty()) d.fabricant = a.fabricant;
  if (!a.mpn.empty()) d.mpn = a.mpn;
  if (!a.fiche.empty()) d.fiche = a.fiche;
  d.id = "art:" + a.id;
  d.nom = a.nom.empty() ? base.nom : a.nom;
  if (!a.description.empty()) d.description = a.description;
  if (a.brochage.empty()) return d;
  std::vector<Broche> broches;
  for (const auto& b : base.broches) {
    const auto it = a.brochage.find(b.numero);
    if (it == a.brochage.end()) {
      broches.push_back(b);
      continue;
    }
    // « 2,4 » : la première pastille porte la broche, les autres des broches empilées, cachées, au même point
    std::vector<std::string> pastilles;
    std::string cur;
    for (char ch : it->second + ",") {
      if (ch == ',' || ch == ' ') {
        if (!cur.empty()) pastilles.push_back(cur);
        cur.clear();
      } else
        cur += ch;
    }
    for (std::size_t i = 0; i < pastilles.size(); ++i) {
      Broche x = b;
      x.numero = pastilles[i];
      if (i > 0) {
        x.nomVisible = false;
        x.numeroVisible = false;
      }
      broches.push_back(x);
    }
  }
  // Pastilles laissées libres par la fiche (« NC » : « 2 ») : une broche non connectée, cachée, sous la première
  if (const auto nc = a.brochage.find("NC"); nc != a.brochage.end() && !base.broches.empty()) {
    std::string cur;
    for (char ch : nc->second + ",") {
      if (ch == ',' || ch == ' ') {
        if (!cur.empty()) {
          Broche x = base.broches.front();
          x.numero = cur;
          x.nom = "NC";
          x.type = TypeBroche::NonConnectee;
          x.extremite = Pt{0, 0};  // au centre du corps : aucun fil n'y arrive
          x.longueur = 0;
          x.nomVisible = x.numeroVisible = false;
          broches.push_back(x);
        }
        cur.clear();
      } else
        cur += ch;
    }
  }
  d.broches = broches;
  return d;
}

std::string installerArticle(ConceptionElec& c, const Article& a0) {
  const Article a = resoudreArticle(a0, [&c](const std::string& id) -> const Article* {
    if (const auto it = c.articles.find(id); it != c.articles.end()) return &it->second;
    return articleIntegre(id);
  });
  // Générique sans symbole : un boîtier à N broches numérotées, que l'inspecteur complète
  if (a.generique && a.symbole.empty() && !c.symboles.count(a.id)) {
    SpecSymbole sp;
    sp.id = a.id;
    sp.nom = a.nom;
    sp.prefixe = "U";
    sp.categorie = "Génériques";
    sp.description = a.description;
    int n = std::atoi(a.parametres.count("broches") ? a.parametres.at("broches").c_str() : "3");
    n = n <= 0 ? 3 : std::max(2, n);
    for (int i = 1; i <= n; ++i) sp.broches.push_back({std::to_string(i), "P" + std::to_string(i), TypeBroche::Passive, -1, 0});
    DefSymbole g = genererSymbole(sp);
    g.id = a.id;
    c.symboles[g.id] = g;
  }
  const DefSymbole* base = c.symbole(a.symbole.empty() && a.generique ? a.id : a.symbole);
  if (!base) return {};
  const DefSymbole d = symboleDeArticle(a, *base);
  c.symboles[d.id] = d;  // la définition suit l'article installé
  if (!a.empreinte.empty() && !c.empreintes.count(a.empreinte))
    if (const DefEmpreinte* e = c.empreinte(a.empreinte)) c.empreintes[a.empreinte] = *e;
  c.articles[a.id] = a0.base.empty() ? a : a0;
  if (!a0.base.empty())
    for (const Article* b = c.articles.count(a0.base) ? &c.articles[a0.base] : articleIntegre(a0.base); b; b = nullptr) c.articles[b->id] = *b;
  return d.id;
}

void remplirDepuisArticle(const ConceptionElec& c, ComposantElec& k) {
  if (k.symbole.rfind("art:", 0) != 0) return;
  const auto it = c.articles.find(k.symbole.substr(4));
  if (it == c.articles.end()) return;
  const Article a = resoudreArticle(it->second, [&c](const std::string& id) -> const Article* {
    if (const auto j = c.articles.find(id); j != c.articles.end()) return &j->second;
    return articleIntegre(id);
  });
  const DefSymbole* d = c.symbole(k.symbole);
  if (!a.valeur.empty()) k.valeur = a.valeur;
  else if (a.generique) k.valeur = a.nom;
  k.empreinte = a.empreinte.empty() && d ? d->empreinte : a.empreinte;
  k.fabricant = a.fabricant;
  k.mpn = a.mpn;
  k.fiche = a.fiche;
  k.lcsc = a.lcsc;
  if (!a.description.empty()) k.description = a.description;
  k.parametres = a.parametres;
  if (a.hauteur > 0) {
    k.hauteur = a.hauteur;
    k.provenanceHauteur = ProvenanceHauteur::Fiche;  // la hauteur de l'article vient de sa fiche
    k.sourceHauteur = "article " + a.id;
  }
  k.article = a.id;
  k.versionArticle = a.version;
  if (!a.brochage.empty()) k.brochageVerifie = k.empreinte;  // le brochage de l'article est écrit pour son boîtier
}

std::string poserArticle(ConceptionElec& c, std::size_t feuille, const Article& a, Pt position, int rotation) {
  const std::string id = installerArticle(c, a);
  return id.empty() ? std::string() : poserSymbole(c, feuille, id, position, rotation);  // poserSymbole remplit le composant
}

// ---------- Recherche ----------
namespace {
// Lettres accentuées (UTF-8) ramenées à leur lettre de base
std::string sansAccents(const std::string& s) {
  static const std::vector<std::pair<std::string, char>> table{
      {"à", 'a'}, {"â", 'a'}, {"ä", 'a'}, {"á", 'a'}, {"ç", 'c'}, {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"î", 'i'}, {"ï", 'i'}, {"í", 'i'}, {"ô", 'o'}, {"ö", 'o'}, {"ó", 'o'},
      {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'}, {"ú", 'u'}, {"ÿ", 'y'}, {"À", 'a'}, {"Â", 'a'}, {"Ç", 'c'}, {"É", 'e'}, {"È", 'e'}, {"Ê", 'e'}, {"Î", 'i'}, {"Ô", 'o'}, {"Ù", 'u'}, {"Û", 'u'},
      {"µ", 'u'}, {"Ω", 'r'}, {"×", 'x'}};
  std::string r;
  for (std::size_t i = 0; i < s.size();) {
    bool remplace = false;
    if (static_cast<unsigned char>(s[i]) >= 0x80)
      for (const auto& [a, b] : table)
        if (s.compare(i, a.size(), a) == 0) {
          r += b;
          i += a.size();
          remplace = true;
          break;
        }
    if (!remplace) r += s[i++];
  }
  return r;
}

// Synonymes : le mot tapé → le mot de l'index
const std::map<std::string, std::string>& synonymes() {
  static const std::map<std::string, std::string> m{
      {"self", "inductance"}, {"bobine", "inductance"}, {"inductor", "inductance"}, {"condo", "condensateur"}, {"capa", "condensateur"}, {"capacitor", "condensateur"}, {"cap", "condensateur"},
      {"resistor", "resistance"}, {"res", "resistance"}, {"fuse", "fusible"}, {"diode", "diode"}, {"transistor", "transistor"}, {"mos", "mosfet"}, {"nmos", "mosfet"}, {"pmos", "mosfet"},
      {"regulator", "regulateur"}, {"ldo", "regulateur"}, {"quartz", "quartz"}, {"crystal", "quartz"}, {"xtal", "quartz"}, {"header", "barrette"}, {"connector", "connecteur"},
      {"led", "led"}, {"electrolytique", "chimique"}, {"elko", "chimique"}};
  return m;
}

bool estChiffre(char c) { return c >= '0' && c <= '9'; }

std::vector<std::string> jetonsDe(const std::string& texte) {
  std::vector<std::string> r;
  std::string cur;
  for (char ch : normaliserRecherche(texte) + " ") {
    if (ch == ' ') {
      if (!cur.empty()) r.push_back(cur);
      cur.clear();
    } else
      cur += ch;
  }
  return r;
}

// Distance d'édition bornée (avec transposition) ; > borne : borne + 1
int distanceBornee(const std::string& a, const std::string& b, int borne) {
  if (std::abs(static_cast<int>(a.size()) - static_cast<int>(b.size())) > borne) return borne + 1;
  const std::size_t n = a.size(), m = b.size();
  std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1));
  for (std::size_t i = 0; i <= n; ++i) d[i][0] = static_cast<int>(i);
  for (std::size_t j = 0; j <= m; ++j) d[0][j] = static_cast<int>(j);
  for (std::size_t i = 1; i <= n; ++i)
    for (std::size_t j = 1; j <= m; ++j) {
      d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + (a[i - 1] != b[j - 1])});
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
    }
  return d[n][m];
}

// Famille de valeur d'un article (pour comparer « Résistance>1k »)
FamilleValeur familleArticle(const Article& a) {
  if (a.symbole == "R" || a.symbole == "Potentiometre") return FamilleValeur::Resistance;
  if (a.symbole == "C" || a.symbole == "C_Pol") return FamilleValeur::Condensateur;
  if (a.symbole == "L") return FamilleValeur::Inductance;
  if (a.symbole == "Fusible") return FamilleValeur::Fusible;
  if (a.symbole == "Quartz") return FamilleValeur::Quartz;
  return FamilleValeur::Aucune;
}

FamilleValeur familleDuMot(const std::string& mot) {
  const std::string m = normaliserRecherche(mot);
  if (m == "resistance" || m == "r" || m == "res" || m == "ohm" || m == "resistor") return FamilleValeur::Resistance;
  if (m == "condensateur" || m == "c" || m == "capa" || m == "condo" || m == "capacitor") return FamilleValeur::Condensateur;
  if (m == "inductance" || m == "l" || m == "self" || m == "bobine" || m == "inductor") return FamilleValeur::Inductance;
  if (m == "fusible" || m == "fuse") return FamilleValeur::Fusible;
  return FamilleValeur::Aucune;
}

std::string texteArticle(const Article& a) {
  std::string t = a.id + " " + a.nom + " " + a.description + " " + a.categorie + " " + a.valeur + " " + a.mpn + " " + a.fabricant + " " + a.empreinte + " " + a.lcsc + " " + a.symbole;
  for (const auto& [k, v] : a.parametres) t += " " + v;
  // Le boîtier reconnu, sous sa forme courte (« soic8 », « 0603 »)
  t += " " + normaliserRecherche(a.empreinte);
  std::string compact;
  for (char ch : normaliserRecherche(a.empreinte))
    if (ch != ' ') compact += ch;
  t += " " + compact;
  return t;
}

bool joker(const std::string& motif, const std::string& texte) {
  std::size_t p = 0, t = 0, etoile = std::string::npos, retour = 0;
  while (t < texte.size()) {
    if (p < motif.size() && (motif[p] == '?' || motif[p] == texte[t])) {
      ++p;
      ++t;
    } else if (p < motif.size() && motif[p] == '*') {
      etoile = p++;
      retour = t;
    } else if (etoile != std::string::npos) {
      p = etoile + 1;
      t = ++retour;
    } else
      return false;
  }
  while (p < motif.size() && motif[p] == '*') ++p;
  return p == motif.size();
}
}  // namespace

std::string normaliserRecherche(const std::string& texte) {
  std::string s = sansAccents(texte);
  std::string r;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    if (std::isalnum(ch)) r += static_cast<char>(std::tolower(ch));
    else if ((ch == ',' || ch == '.') && i > 0 && i + 1 < s.size() && estChiffre(s[i - 1]) && estChiffre(s[i + 1])) r += '.';
    else r += ' ';
  }
  return r;
}

std::string boitierReconnu(const std::string& mot) {
  std::string m;
  for (char ch : normaliserRecherche(mot))
    if (ch != ' ') m += ch;
  for (const char* puce : {"0402", "0603", "0805", "1206", "1210", "2512"})
    if (m == puce) return puce;
  static const std::vector<std::pair<std::string, std::string>> familles{{"soic", "SOIC-"}, {"so", "SOIC-"}, {"tssop", "TSSOP-"}, {"dip", "DIP-"}, {"qfn", "QFN-"}, {"tqfp", "TQFP-"}, {"lqfp", "TQFP-"}};
  for (const auto& [f, id] : familles)
    if (m.size() > f.size() && m.compare(0, f.size(), f) == 0 && std::all_of(m.begin() + static_cast<long>(f.size()), m.end(), estChiffre)) return id + m.substr(f.size());
  if (m == "sot23" || m == "sot233") return "SOT-23";
  if (m == "sot235" || m == "sot23 5") return "SOT-23-5";
  if (m == "sot223") return "SOT-223";
  if (m == "to220") return "TO-220-3";
  if (m == "sod123") return "D_SOD-123";
  if (m == "sma") return "D_SMA";
  return {};
}

std::optional<Article> passifCompose(const std::string& requete) {
  FamilleValeur famille = FamilleValeur::Aucune;
  std::string valeur, boitier, tension;
  double nombre = 0;
  for (const auto& mot : jetonsDe(requete)) {
    const auto syn = synonymes().find(mot);
    if (const FamilleValeur f = familleDuMot(syn == synonymes().end() ? mot : syn->second); f != FamilleValeur::Aucune) {
      famille = f;
      continue;
    }
    if (const std::string b = boitierReconnu(mot); !b.empty() && b.size() == 4 && estChiffre(b[0])) {
      boitier = b;
      continue;
    }
    if (mot.size() > 1 && (mot.back() == 'v') && std::all_of(mot.begin(), mot.end() - 1, [](char ch) { return estChiffre(ch) || ch == '.'; })) {
      tension = mot.substr(0, mot.size() - 1) + " V";
      continue;
    }
    if (valeur.empty() && !mot.empty() && estChiffre(mot[0])) valeur = mot;
    // Un mot qui ne dit rien d'un passif (« ldo », « regulateur », « ma ») : la requête vise autre chose
    static const std::set<std::string> qualificatifs{"smd", "cms", "ceramique", "chimique", "film", "tantale", "ohm", "ohms", "f", "h", "r", "k", "m", "u", "n", "p", "nf", "uf", "pf", "uh", "mh", "nh"};
    if (!mot.empty() && std::none_of(mot.begin(), mot.end(), [](char ch) { return estChiffre(ch); }) && !qualificatifs.count(mot)) return std::nullopt;
  }
  if (valeur.empty()) return std::nullopt;
  // Sans famille dite : l'unité ou le multiplicateur la donne (« 100n » : un condensateur, « 10k » : une résistance)
  std::string v = valeur;
  std::replace(v.begin(), v.end(), '.', ',');
  if (famille == FamilleValeur::Aucune) {
    const char der = v.back();
    const bool lettreFin = std::isalpha(static_cast<unsigned char>(der));
    std::string sansUnite = v;
    if (v.size() > 1 && (der == 'f')) famille = FamilleValeur::Condensateur;
    else if (v.size() > 1 && der == 'h') famille = FamilleValeur::Inductance;
    else if (v.find('p') != std::string::npos || v.find('n') != std::string::npos || v.find('u') != std::string::npos) famille = FamilleValeur::Condensateur;
    else if (!lettreFin || der == 'k' || der == 'm' || der == 'r' || v.find('k') != std::string::npos || v.find('r') != std::string::npos) famille = FamilleValeur::Resistance;
  }
  if (famille == FamilleValeur::Aucune || famille == FamilleValeur::Fusible) return std::nullopt;
  // « u » tapé pour « µ »
  std::string tape = v;
  for (std::size_t i = 0; i < tape.size(); ++i)
    if (tape[i] == 'u') tape.replace(i, 1, "µ");
  const ValeurComprise vc = lireValeurComposant(tape, famille);
  if (!vc.comprise() || vc.nombre <= 0) return std::nullopt;
  nombre = vc.nombre;
  if (boitier.empty()) boitier = famille == FamilleValeur::Inductance ? "0805" : "0603";
  const std::string prefixe = famille == FamilleValeur::Resistance ? "R" : famille == FamilleValeur::Condensateur ? "C" : "L";
  const std::string val = ecrireCompact(nombre, famille);
  Article a;
  a.symbole = prefixe;
  a.empreinte = prefixe + "_" + boitier;
  a.valeur = val;
  a.categorie = famille == FamilleValeur::Resistance ? "Résistances" : famille == FamilleValeur::Condensateur ? "Condensateurs" : "Inductances";
  const std::string nomFam = famille == FamilleValeur::Resistance ? "Résistance" : famille == FamilleValeur::Condensateur ? "Condensateur" : "Inductance";
  a.id = prefixe + "_" + val + "_" + boitier + (tension.empty() ? std::string() : "_" + tension.substr(0, tension.size() - 2) + "V");
  a.nom = nomFam + " " + val + " " + boitier;
  a.description = nomFam + " CMS " + val + ", " + boitier + (tension.empty() ? std::string() : ", " + tension) + " (article composé à la demande)";
  if (!tension.empty()) a.parametres["tension"] = tension;
  if (!genererEmpreinte(a.empreinte)) return std::nullopt;
  return a;
}

IndexArticles::IndexArticles(std::vector<Article> articles) : m_articles(std::move(articles)) {
  m_jetons.reserve(m_articles.size());
  for (std::size_t i = 0; i < m_articles.size(); ++i) {
    std::vector<std::string> j = jetonsDe(texteArticle(m_articles[i]));
    std::sort(j.begin(), j.end());
    j.erase(std::unique(j.begin(), j.end()), j.end());
    for (const auto& x : j) m_index[x].push_back(i);
    m_jetons.push_back(std::move(j));
  }
}

std::vector<ResultatRecherche> IndexArticles::chercher(const std::string& requete, std::size_t maximum, const FiltresRecherche& filtres) const {
  // Analyse : termes positifs, exclusions, phrases, jokers, expressions régulières, comparaisons sur un champ
  struct Comparaison {
    std::string champ, op, valeur;
    std::string normalise;
    FamilleValeur famille = FamilleValeur::Aucune;
  };
  std::vector<std::string> positifs, exclus, phrases, jokers;
  std::vector<std::regex> regex;
  std::vector<Comparaison> comparaisons;
  {
    std::string q = requete;
    std::size_t i = 0;
    while (i < q.size()) {
      while (i < q.size() && q[i] == ' ') ++i;
      if (i >= q.size()) break;
      if (q[i] == '"') {
        const std::size_t f = q.find('"', i + 1);
        phrases.push_back(normaliserRecherche(q.substr(i + 1, (f == std::string::npos ? q.size() : f) - i - 1)));
        i = f == std::string::npos ? q.size() : f + 1;
        continue;
      }
      if (q[i] == '/') {
        const std::size_t f = q.find('/', i + 1);
        if (f != std::string::npos) {
          try {
            regex.emplace_back(q.substr(i + 1, f - i - 1), std::regex::icase);
          } catch (const std::regex_error&) {
          }
          i = f + 1;
          continue;
        }
      }
      std::size_t f = q.find(' ', i);
      if (f == std::string::npos) f = q.size();
      const std::string mot = q.substr(i, f - i);
      i = f;
      if (mot.size() > 1 && mot[0] == '-' && !estChiffre(mot[1])) {
        for (const auto& x : jetonsDe(mot.substr(1))) exclus.push_back(x);
        continue;
      }
      if (mot.size() > 1 && mot[0] == '-' && estChiffre(mot[1])) {  // « -0402 »
        exclus.push_back(normaliserRecherche(mot.substr(1)));
        continue;
      }
      static const std::regex comp(R"(^([^<>=!]+)(>=|<=|!=|>|<|=)(.+)$)");
      std::smatch m;
      if (std::regex_match(mot, m, comp)) {
        comparaisons.push_back({m[1].str(), m[2].str(), m[3].str(), normaliserRecherche(m[1].str()), familleDuMot(m[1].str())});
        continue;
      }
      if (mot.find('*') != std::string::npos || mot.find('?') != std::string::npos) {
        jokers.push_back(normaliserRecherche(mot.substr(0, 0)) + [&] {
          std::string r;
          for (char ch : sansAccents(mot)) r += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
          return r;
        }());
        continue;
      }
      for (auto x : jetonsDe(mot)) {
        if (const auto s = synonymes().find(x); s != synonymes().end()) x = s->second;
        positifs.push_back(x);
      }
    }
  }
  // Valeurs et boîtiers reconnus dans les termes positifs
  std::optional<double> valeurVoulue;
  FamilleValeur familleVoulue = FamilleValeur::Aucune;
  std::string boitierVoulu;
  for (const auto& p : positifs) {
    if (const FamilleValeur f = familleDuMot(p); f != FamilleValeur::Aucune) familleVoulue = f;
    if (const std::string b = boitierReconnu(p); !b.empty()) boitierVoulu = b;
  }
  if (const auto pc = passifCompose(requete); pc && comparaisons.empty()) {
    const ValeurComprise vc = lireValeurComposant(pc->valeur, familleArticle(*pc));
    if (vc.comprise()) {
      valeurVoulue = vc.nombre;
      familleVoulue = familleArticle(*pc);
    }
  }
  // Candidats : l'index inversé (jetons exacts, préfixes, fautes légères) ; sans terme positif, tous
  std::map<std::size_t, double> score;
  const bool tous = positifs.empty();
  if (tous)
    for (std::size_t i = 0; i < m_articles.size(); ++i) score[i] = 0;
  for (const auto& p : positifs) {
    std::map<std::size_t, double> trouve;
    auto ajouter = [&](const std::vector<std::size_t>& ids, double s) {
      for (std::size_t i : ids) trouve[i] = std::max(trouve[i], s);
    };
    if (const auto it = m_index.find(p); it != m_index.end()) ajouter(it->second, 3.0);
    for (auto it = m_index.lower_bound(p); it != m_index.end() && it->first.compare(0, p.size(), p) == 0; ++it)
      if (it->first != p) ajouter(it->second, 1.5);
    if (p.size() >= 4)
      for (const auto& [jeton, ids] : m_index)
        if (jeton.size() + 1 >= p.size() && jeton.size() <= p.size() + 1 && jeton[0] == p[0] && distanceBornee(p, jeton, 1) <= 1) ajouter(ids, 1.0);
    // Une valeur tapée (« 10k ») compte même si l'article l'écrit autrement (« 10 kΩ »)
    const bool motValeur = valeurVoulue && !p.empty() && estChiffre(p[0]) && boitierReconnu(p).empty();
    for (const auto& [i, s] : trouve) score[i] += s;
    if (!motValeur && !trouve.empty() && !tous) {
      // Un terme positif absent : l'article perd, sans sortir (ET souple)
      for (auto& [i, s] : score)
        if (!trouve.count(i)) s -= 2.0;
    }
  }
  std::vector<ResultatRecherche> r;
  auto contient = [&](std::size_t i, const std::string& j) { return std::binary_search(m_jetons[i].begin(), m_jetons[i].end(), j); };
  for (auto [i, s] : score) {
    const Article& a = m_articles[i];
    if (!tous && s <= 0) continue;
    bool garde = true;
    if (!exclus.empty()) {
      const std::string emp = normaliserRecherche(a.empreinte);
      for (const auto& x : exclus) garde &= !contient(i, x) && emp.find(x) == std::string::npos;
    }
    if (!phrases.empty()) {
      const std::string texte = normaliserRecherche(texteArticle(a));
      for (const auto& ph : phrases) garde &= texte.find(ph) != std::string::npos;
    }
    if (!garde) continue;
    for (const auto& jk : jokers) {
      bool un = false;
      for (const auto& champ : {a.id, a.nom, a.empreinte, a.mpn, a.valeur}) {
        std::string c;
        for (char ch : sansAccents(champ)) c += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        un |= joker(jk, c);
      }
      garde &= un;
    }
    for (const auto& re : regex) garde &= std::regex_search(a.id + " " + a.nom + " " + a.mpn + " " + a.empreinte + " " + a.description, re);
    for (const auto& cp : comparaisons) {
      const std::string& champ = cp.normalise;
      const FamilleValeur f = cp.famille;
      std::string actuel;
      FamilleValeur fv = familleArticle(a);
      if (f != FamilleValeur::Aucune || champ == "valeur") {
        if (f != FamilleValeur::Aucune && fv != f) {
          garde = false;
          continue;
        }
        actuel = a.valeur;
      } else if (champ == "boitier" || champ == "empreinte") {
        actuel = a.empreinte;
        fv = FamilleValeur::Aucune;
      } else {
        for (const auto& [k, v] : a.parametres)
          if (normaliserRecherche(k) == champ) actuel = v;
        fv = FamilleValeur::Aucune;
      }
      const FamilleValeur fl = fv == FamilleValeur::Aucune ? f : fv;
      const ValeurComprise va = lireValeurComposant(actuel, fl);
      // La valeur de la comparaison ne se lit qu'une fois par famille
      static thread_local std::map<std::pair<std::string, int>, ValeurComprise> cache;
      if (cache.size() > 256) cache.clear();
      const auto cle = std::make_pair(cp.valeur, static_cast<int>(fl));
      auto ic = cache.find(cle);
      if (ic == cache.end()) ic = cache.emplace(cle, lireValeurComposant(cp.valeur, fl)).first;
      const ValeurComprise& vb = ic->second;
      double x = 0, y = 0;
      bool nombres = va.comprise() && vb.comprise();
      if (nombres) {
        x = va.nombre;
        y = vb.nombre;
      } else {
        const ResultatFormule ra = evaluerFormule(actuel), rb = evaluerFormule(cp.valeur);
        nombres = ra.ok && rb.ok && ra.q.scalaire() && rb.q.scalaire();
        if (nombres) {
          x = ra.q.reel();
          y = rb.q.reel();
        }
      }
      if (!nombres) {
        const bool egal = normaliserRecherche(actuel).find(normaliserRecherche(cp.valeur)) != std::string::npos;
        garde &= cp.op == "=" ? egal : cp.op == "!=" ? !egal : false;
        continue;
      }
      const double eps = std::abs(y) * 1e-9;
      if (cp.op == ">") garde &= x > y + eps;
      else if (cp.op == ">=") garde &= x >= y - eps;
      else if (cp.op == "<") garde &= x < y - eps;
      else if (cp.op == "<=") garde &= x <= y + eps;
      else if (cp.op == "=") garde &= std::abs(x - y) <= eps;
      else if (cp.op == "!=") garde &= std::abs(x - y) > eps;
    }
    if (filtres.avecEmpreinte) garde &= !a.empreinte.empty();
    if (filtres.avecFiche) garde &= !a.fiche.empty();
    if (filtres.avecReference) garde &= !a.mpn.empty();
    if (filtres.avec3D) garde &= !a.modele3D.empty() || (!a.empreinte.empty() && (a.hauteur > 0 || genererEmpreinte(a.empreinte).has_value()));
    if (filtres.avecSimulation) garde &= !a.modeleSpice.vide() || a.parametres.count("modeleSpice") > 0 || familleArticle(a) != FamilleValeur::Aucune;
    if (!garde) continue;
    // Valeur et boîtier demandés : en tête
    if (valeurVoulue) {
      const ValeurComprise va = lireValeurComposant(a.valeur, familleArticle(a));
      if (va.comprise() && familleArticle(a) == familleVoulue && std::abs(va.nombre - *valeurVoulue) <= *valeurVoulue * 1e-6) s += 10;
      else if (familleVoulue != FamilleValeur::Aucune && familleArticle(a) != familleVoulue) s -= 5;
    }
    if (!boitierVoulu.empty() && a.empreinte.find(boitierVoulu) != std::string::npos) s += 4;
    r.push_back({a, s, false});
  }
  std::stable_sort(r.begin(), r.end(), [](const ResultatRecherche& a, const ResultatRecherche& b) { return a.score > b.score; });
  // Le passif demandé n'existe pas encore : composé à la volée, en tête
  if (const auto pc = passifCompose(requete); pc && comparaisons.empty() && exclus.empty()) {
    const bool dejaLa = !r.empty() && r.front().score >= 10 && (boitierVoulu.empty() || r.front().article.empreinte.find(boitierVoulu) != std::string::npos);
    if (!dejaLa) r.insert(r.begin(), {*pc, 100, true});
  }
  if (r.size() > maximum) r.resize(maximum);
  return r;
}

std::vector<ResultatRecherche> IndexArticles::semblables(const Article& a, std::size_t maximum) const {
  std::vector<ResultatRecherche> r;
  const FamilleValeur f = familleArticle(a);
  const ValeurComprise va = lireValeurComposant(a.valeur, f);
  for (const auto& b : m_articles) {
    if (b.id == a.id) continue;
    double s = 0;
    if (b.categorie == a.categorie) s += 2;
    if (b.symbole == a.symbole) s += 2;
    if (b.empreinte == a.empreinte) s += 3;
    if (va.comprise()) {
      const ValeurComprise vb = lireValeurComposant(b.valeur, f);
      if (vb.comprise() && vb.nombre > 0 && va.nombre > 0) s += 4 - std::min(4.0, std::abs(std::log10(vb.nombre / va.nombre)) * 4);
    } else if (b.valeur == a.valeur)
      s += 4;
    for (const auto& [k, v] : a.parametres)
      if (const auto it = b.parametres.find(k); it != b.parametres.end() && it->second == v) s += 0.5;
    if (s >= 4) r.push_back({b, s, false});
  }
  std::stable_sort(r.begin(), r.end(), [](const ResultatRecherche& x, const ResultatRecherche& y) { return x.score > y.score; });
  if (r.size() > maximum) r.resize(maximum);
  return r;
}

// ---------- Empreintes compatibles ----------
std::string sorteEmpreinte(const std::string& id) {
  for (const char* p : {"R_", "C_", "CP_", "L_", "LED_", "Fuse_", "D_", "PinHeader_", "TerminalBlock_", "MountingHole_", "Crystal_", "SW_"})
    if (id.rfind(p, 0) == 0) return std::string(p).substr(0, std::string(p).size() - 1);
  for (const char* p : {"SOIC", "TSSOP", "DIP", "QFN", "TQFP", "SOT-223", "SOT-23", "TO-220"})
    if (id.rfind(p, 0) == 0) return p;
  for (const auto& i : catalogueEmpreintes())
    if (i.id == id) return i.famille;
  return {};
}

std::vector<InfoEmpreinte> empreintesCompatibles(const DefSymbole& s, bool filtre) {
  std::set<std::string> numeros;
  for (const auto& b : s.broches)
    if (!b.numero.empty()) numeros.insert(b.numero);
  const std::string sorte = sorteEmpreinte(s.empreinte);
  std::string familleRef;
  for (const auto& i : catalogueEmpreintes())
    if (i.id == s.empreinte) familleRef = i.famille;
  // Les circuits intégrés acceptent tous les boîtiers de circuits intégrés au même nombre de pastilles
  static const std::set<std::string> ci{"SOIC", "TSSOP", "DIP", "QFN", "TQFP"};
  std::vector<InfoEmpreinte> r;
  for (const auto& i : catalogueEmpreintes()) {
    if (i.pastilles != static_cast<int>(numeros.size())) continue;
    if (filtre && !sorte.empty()) {
      const std::string so = sorteEmpreinte(i.id);
      const bool memeSorte = so == sorte || (ci.count(sorte) && ci.count(so)) || (sorte == "C" && so == "CP") || (sorte == "CP" && so == "C");
      if (!memeSorte) continue;
    } else if (filtre && !familleRef.empty() && i.famille != familleRef)
      continue;
    r.push_back(i);
  }
  return r;
}

// ---------- IPC-7351B ----------
namespace {
double arrondiHaut(double v, double pas) { return std::ceil(v / pas - 1e-9) * pas; }
double arrondiBas(double v, double pas) { return std::floor(v / pas + 1e-9) * pas; }
double arrondiPres(double v, double pas) { return std::round(v / pas) * pas; }
struct Gains {
  double jt, jh, js;
};
Gains gains(const CotesBoitier& c, DensiteIPC d) {
  const int k = static_cast<int>(d);  // 0 M, 1 N, 2 L
  switch (c.gabarit) {
    case GabaritIPC::Puce: {
      const double t[3][3] = {{0.55, 0.00, 0.05}, {0.35, 0.00, 0.00}, {0.15, 0.00, -0.05}};
      return {t[k][0], t[k][1], t[k][2]};
    }
    case GabaritIPC::SansPatte: {
      const double t[3][3] = {{0.40, 0.00, -0.04}, {0.30, 0.00, -0.04}, {0.20, 0.00, -0.04}};
      return {t[k][0], t[k][1], t[k][2]};
    }
    case GabaritIPC::AileDeMouette:
    default: {
      const double grand[3][3] = {{0.55, 0.45, 0.05}, {0.35, 0.35, 0.03}, {0.15, 0.25, 0.01}};
      const double fin[3][3] = {{0.55, 0.45, 0.01}, {0.35, 0.35, -0.02}, {0.15, 0.25, -0.04}};
      const auto& t = c.pas > 0.625 ? grand : fin;
      return {t[k][0], t[k][1], t[k][2]};
    }
  }
}
char lettreDensite(DensiteIPC d) { return d == DensiteIPC::Plus ? 'M' : d == DensiteIPC::Nominale ? 'N' : 'L'; }
std::string cent(double mm) {
  char t[16];
  std::snprintf(t, sizeof t, "%d", static_cast<int>(std::lround(mm * 100)));
  return t;
}
}  // namespace

PastillesIPC calculerIPC7351(const CotesBoitier& c, DensiteIPC d, const ProfilIPC& p) {
  PastillesIPC r;
  const Gains g = gains(c, d);
  r.jt = g.jt;
  r.jh = g.jh;
  r.js = g.js;
  const double cl = c.lMax - c.lMin, ct = c.tMax - c.tMin, cw = c.wMax - c.wMin;
  const double ff = p.fabrication * p.fabrication + p.placement * p.placement;
  // Écart intérieur S = L − 2T, ramené à sa tolérance quadratique
  const double sMin = c.lMin - 2 * c.tMax, sMax = c.lMax - 2 * c.tMin;
  const double sTol = sMax - sMin, sTolRms = std::sqrt(cl * cl + 2 * ct * ct);
  const double sMaxRms = sMax - (sTol - sTolRms) / 2;
  r.z = arrondiHaut(c.lMin + 2 * g.jt + std::sqrt(cl * cl + ff), p.arrondiZG);
  r.g = arrondiBas(sMaxRms - 2 * g.jh - std::sqrt(sTolRms * sTolRms + ff), p.arrondiZG);
  r.x = arrondiPres(c.wMin + 2 * g.js + std::sqrt(cw * cw + ff), p.arrondiX);
  r.longueur = (r.z - r.g) / 2;
  r.largeur = r.x;
  r.centre = (r.z + r.g) / 4;
  return r;
}

std::string nomIPC7351(const CotesBoitier& c, DensiteIPC d) {
  const double l = (c.lMin + c.lMax) / 2, w = (c.wMin + c.wMax) / 2;
  switch (c.gabarit) {
    case GabaritIPC::Puce: return "CHIP" + cent(l / 10 * 10) + cent(w) + "X" + cent(c.hauteur) + lettreDensite(d);
    case GabaritIPC::SansPatte: return "QFN" + cent(c.pas) + "P" + cent(l) + "X" + cent(l) + "X" + cent(c.hauteur) + "-" + std::to_string(c.broches + (c.plotCentral > 0 ? 1 : 0)) + lettreDensite(d);
    case GabaritIPC::AileDeMouette:
    default:
      if (c.quatreCotes) return "QFP" + cent(c.pas) + "P" + cent(l) + "X" + cent(l) + "X" + cent(c.hauteur) + "-" + std::to_string(c.broches) + lettreDensite(d);
      return std::string(std::abs(c.pas - 1.27) < 1e-6 ? "SOIC" : "SOP") + cent(c.pas) + "P" + cent(l) + "X" + cent(c.hauteur) + "-" + std::to_string(c.broches) + lettreDensite(d);
  }
}

std::string nomKicadIPC(const CotesBoitier& c) {
  char t[96];
  switch (c.gabarit) {
    case GabaritIPC::Puce: std::snprintf(t, sizeof t, "Chip_%.1fx%.1fmm", c.corpsL, c.corpsW); break;
    case GabaritIPC::SansPatte: std::snprintf(t, sizeof t, "QFN-%d_%.0fx%.0fmm_P%.2fmm", c.broches, c.corpsL, c.corpsW, c.pas); break;
    case GabaritIPC::AileDeMouette:
    default:
      if (c.quatreCotes) std::snprintf(t, sizeof t, "TQFP-%d_%.0fx%.0fmm_P%.1fmm", c.broches, c.corpsL, c.corpsW, c.pas);
      else std::snprintf(t, sizeof t, "%s-%d_%.1fx%.1fmm_P%.2fmm", std::abs(c.pas - 1.27) < 1e-6 ? "SOIC" : "SOP", c.broches, c.corpsW, c.corpsL, c.pas);
  }
  return t;
}

DefEmpreinte empreinteIPC7351(const CotesBoitier& c, DensiteIPC dens, const ProfilIPC& profil) {
  const PastillesIPC p = calculerIPC7351(c, dens, profil);
  DefEmpreinte d;
  d.id = nomIPC7351(c, dens);
  d.description = "Généré selon l'IPC-7351B (" + std::string(1, lettreDensite(dens)) + ") : " + nomKicadIPC(c) + ", orientation zéro : broche 1 en haut à gauche";
  d.famille = c.gabarit == GabaritIPC::Puce ? "Passifs CMS" : "Circuits intégrés";
  auto pastille = [](const std::string& n, double x, double y, double lx, double ly) {
    Pastille q;
    q.numero = n;
    q.type = TypePastille::CMS;
    q.forme = FormePastille::RectArrondi;
    q.position = ptMm(x, y);
    q.largeur = nm(lx);
    q.hauteur = nm(ly);
    return q;
  };
  if (c.gabarit == GabaritIPC::Puce) {
    d.pastilles = {pastille("1", -p.centre, 0, p.longueur, p.largeur), pastille("2", p.centre, 0, p.longueur, p.largeur)};
  } else if (!c.quatreCotes) {
    const int parCote = c.broches / 2;
    const double y0 = -(parCote - 1) * c.pas / 2;
    for (int i = 0; i < parCote; ++i) d.pastilles.push_back(pastille(std::to_string(i + 1), -p.centre, y0 + i * c.pas, p.longueur, p.largeur));
    for (int i = 0; i < parCote; ++i) d.pastilles.push_back(pastille(std::to_string(parCote + i + 1), p.centre, -y0 - i * c.pas, p.longueur, p.largeur));
  } else {
    const int parCote = c.broches / 4;
    const double a0 = -(parCote - 1) * c.pas / 2;
    int n = 1;
    for (int i = 0; i < parCote; ++i) d.pastilles.push_back(pastille(std::to_string(n++), -p.centre, a0 + i * c.pas, p.longueur, p.largeur));
    for (int i = 0; i < parCote; ++i) d.pastilles.push_back(pastille(std::to_string(n++), a0 + i * c.pas, p.centre, p.largeur, p.longueur));
    for (int i = 0; i < parCote; ++i) d.pastilles.push_back(pastille(std::to_string(n++), p.centre, -a0 - i * c.pas, p.longueur, p.largeur));
    for (int i = 0; i < parCote; ++i) d.pastilles.push_back(pastille(std::to_string(n++), -a0 - i * c.pas, -p.centre, p.largeur, p.longueur));
  }
  if (c.gabarit == GabaritIPC::SansPatte && c.plotCentral > 0) d.pastilles.push_back(pastille(std::to_string(c.broches + 1), 0, 0, c.plotCentral, c.plotCentral));
  // Encombrement : pastilles et corps, marge selon la densité, au 0,05 mm
  double ex = c.corpsW / 2, ey = c.corpsL / 2;
  if (c.gabarit == GabaritIPC::Puce) std::swap(ex, ey);
  for (const auto& q : d.pastilles) {
    ex = std::max(ex, std::abs(mm(q.position.x)) + mm(q.largeur) / 2);
    ey = std::max(ey, std::abs(mm(q.position.y)) + mm(q.hauteur) / 2);
  }
  const double marge = dens == DensiteIPC::Plus ? 0.5 : dens == DensiteIPC::Nominale ? 0.25 : 0.1;
  ex = arrondiHaut(ex + marge, 0.05);
  ey = arrondiHaut(ey + marge, 0.05);
  d.encombrement = Boite{nm(-ex), nm(-ey), nm(ex), nm(ey)};
  // Sérigraphie : le corps hors des pastilles, et le repère de la broche 1
  const double bx = c.gabarit == GabaritIPC::Puce ? c.corpsL / 2 : c.corpsW / 2, by = c.gabarit == GabaritIPC::Puce ? c.corpsW / 2 : c.corpsL / 2;
  auto trait = [&](double x0, double y0, double x1, double y1) {
    TraitEmpreinte t;
    t.couche = Couche::SerigraphieDessus;
    t.a = ptMm(x0, y0);
    t.b = ptMm(x1, y1);
    d.traits.push_back(t);
  };
  if (c.gabarit != GabaritIPC::Puce) {
    trait(-bx, -by, bx, -by);
    trait(-bx, by, bx, by);
    TraitEmpreinte rep;
    rep.couche = Couche::SerigraphieDessus;
    rep.a = ptMm(-ex + 0.2, -ey + 0.2);
    rep.rayon = nm(0.1);
    rep.b = rep.a;
    d.traits.push_back(rep);
  }
  d.corps.largeur = nm(c.gabarit == GabaritIPC::Puce ? c.corpsL : c.corpsW);
  d.corps.profondeur = nm(c.gabarit == GabaritIPC::Puce ? c.corpsW : c.corpsL);
  d.corps.hauteur = nm(c.hauteur);
  d.corps.couleur = c.gabarit == GabaritIPC::Puce ? "#1F2126" : "#2A2D34";
  return d;
}

// ---------- Mise à jour depuis l'article ----------
std::vector<DifferenceArticle> differencesArticle(const ConceptionElec& c, const ComposantElec& k, const Article& a) {
  std::vector<DifferenceArticle> r;
  auto cmp = [&](const char* champ, const std::string& avant, const std::string& apres) {
    if (avant != apres) r.push_back({champ, avant, apres});
  };
  const DefSymbole* base = c.symbole(a.symbole);
  const std::string symbole = base ? symboleDeArticle(a, *base).id : a.symbole;
  // Composant posé avec le symbole même de l'article, sans brochage propre (article repris d'un import, 10.19) : mêmes
  // broches, mêmes numéros, rien à mettre à jour
  if (k.symbole != a.symbole || !a.brochage.empty()) cmp("symbole", k.symbole, symbole);
  cmp("empreinte", k.empreinte, a.empreinte);
  if (!a.valeur.empty()) cmp("valeur", k.valeur, a.valeur);
  cmp("fabricant", k.fabricant, a.fabricant);
  cmp("référence fabricant", k.mpn, a.mpn);
  cmp("fiche", k.fiche, a.fiche);
  cmp("LCSC", k.lcsc, a.lcsc);
  if (!a.description.empty()) cmp("description", k.description, a.description);
  if (a.hauteur > 0 && std::abs(k.hauteur - a.hauteur) > 1e-9) r.push_back({"hauteur", std::to_string(k.hauteur), std::to_string(a.hauteur)});
  for (const auto& [cle, v] : a.parametres)
    if (const auto it = k.parametres.find(cle); it == k.parametres.end() || it->second != v) r.push_back({"paramètre " + cle, it == k.parametres.end() ? std::string() : it->second, v});
  if (k.versionArticle != a.version) r.push_back({"version", std::to_string(k.versionArticle), std::to_string(a.version)});
  return r;
}

std::vector<std::string> composantsAMettreAJour(const ConceptionElec& c, const Article& a) {
  std::vector<std::string> r;
  for (const auto& k : c.composants)
    if (k.article == a.id && !differencesArticle(c, k, a).empty()) r.push_back(k.uuid);
  return r;
}

int mettreAJourDepuisArticle(ConceptionElec& c, const Article& a, const std::vector<std::string>& composants) {
  exigerModifiable(c);
  const DefSymbole* base = c.symbole(a.symbole);
  if (!base) return 0;
  const DefSymbole d = symboleDeArticle(a, *base);
  c.symboles[d.id] = d;
  if (!a.empreinte.empty() && !c.empreintes.count(a.empreinte))
    if (const DefEmpreinte* e = c.empreinte(a.empreinte)) c.empreintes[a.empreinte] = *e;
  c.articles[a.id] = a;
  int n = 0;
  for (const auto& id : composants) {
    ComposantElec* k = c.composant(id);
    if (!k) continue;
    if (k->symbole != d.id) {
      // Le symbole change : remplacement en place, fils gardés (3.16)
      RegleRemplacement regle;
      regle.garderValeur = true;
      regle.garderEmpreinte = true;
      std::string erreur;
      if (remplacerComposants(c, {id}, d.id, regle, &erreur) == 0) continue;
      k = c.composant(id);
      if (!k) continue;
    }
    k->empreinte = a.empreinte;
    if (!a.valeur.empty()) k->valeur = a.valeur;
    k->fabricant = a.fabricant;
    k->mpn = a.mpn;
    k->fiche = a.fiche;
    k->lcsc = a.lcsc;
    if (!a.description.empty()) k->description = a.description;
    for (const auto& [cle, v] : a.parametres) k->parametres[cle] = v;
    if (a.hauteur > 0) {
      k->hauteur = a.hauteur;
      k->provenanceHauteur = ProvenanceHauteur::Fiche;
    }
    k->article = a.id;
    k->versionArticle = a.version;
    if (!a.brochage.empty()) k->brochageVerifie = k->empreinte;
    ++n;
  }
  return n;
}

// ---------- Générique et standardisation ----------
Article articleGenerique(const std::string& exigences, int broches) {
  Article a;
  std::string id;
  for (char ch : normaliserRecherche(exigences))
    id += ch == ' ' ? '_' : ch;
  a.id = "gen:" + id;
  a.nom = exigences;
  a.description = "Composant générique provisoire : " + exigences;
  a.categorie = "Génériques";
  a.generique = true;
  a.exigences = exigences;
  a.parametres["broches"] = std::to_string(broches);
  return a;
}

std::vector<PropositionStandard> propositionsStandardisation(const ConceptionElec& c, const IndexArticles& index, const std::set<std::string>& exclus) {
  std::vector<PropositionStandard> r;
  for (const auto& k : c.composants) {
    if (exclus.count(k.uuid)) continue;
    const auto ia = c.articles.find(k.article);
    const bool generique = ia != c.articles.end() && ia->second.generique;
    if (!k.article.empty() && !generique) continue;
    const DefSymbole* d = c.symbole(k.symbole);
    if (d && (d->alimentation || d->drapeau || d->mecanique)) continue;
    std::string requete;
    if (generique) requete = ia->second.exigences;
    else {
      requete = k.valeur;
      const std::string b = k.empreinte.size() > 2 && (k.empreinte.rfind("R_", 0) == 0 || k.empreinte.rfind("C_", 0) == 0 || k.empreinte.rfind("L_", 0) == 0) ? k.empreinte.substr(2) : k.empreinte;
      requete += " " + b;
      if (d) requete += " " + d->nom;
    }
    const auto res = index.chercher(requete, 3);
    if (res.empty() || res.front().score <= 0) continue;
    const ResultatRecherche& best = res.front();
    // Un générique ne se remplace que par un article du même genre de symbole, quand il en a déjà un
    if (!generique && d && best.article.symbole != d->id && !best.compose && d->id.rfind("art:", 0) != 0 && best.article.symbole != k.symbole) continue;
    PropositionStandard p;
    p.composant = k.uuid;
    p.article = best.article.id;
    p.confiance = std::clamp(best.score / (best.compose ? 100.0 : 20.0), 0.0, 1.0);
    p.raison = "« " + requete + " » → " + best.article.nom;
    r.push_back(p);
  }
  return r;
}

int appliquerStandardisation(ConceptionElec& c, const IndexArticles& index, const std::vector<PropositionStandard>& propositions) {
  exigerModifiable(c);
  int n = 0;
  for (const auto& p : propositions) {
    const Article* a = nullptr;
    std::optional<Article> compose;
    for (const auto& x : index.articles())
      if (x.id == p.article) a = &x;
    if (!a) {
      for (const auto& r : index.chercher(p.raison.substr(0, p.raison.find(" → ")), 5))
        if (r.article.id == p.article) compose = r.article;
      if (compose) a = &*compose;
    }
    if (!a) continue;
    n += mettreAJourDepuisArticle(c, *a, {p.composant});
  }
  return n;
}

// ---------- Contrôles et archives ----------
std::vector<std::string> controlerArticle(const Article& a, const DefSymbole* s, const DefEmpreinte* e) {
  std::vector<std::string> r;
  if (a.id.empty()) r.push_back("article sans identifiant");
  if (a.generique) return r;
  if (!s) {
    r.push_back(a.id + " : symbole « " + a.symbole + " » introuvable");
    return r;
  }
  if (s->prefixe.empty()) r.push_back(a.id + " : le symbole n'a pas de préfixe de repère");
  const DefSymbole d = symboleDeArticle(a, *s);
  std::map<std::string, std::string> nomParNumero;
  for (const auto& b : d.broches) {
    if (b.numero.empty()) {
      r.push_back(a.id + " : broche « " + b.nom + " » sans numéro");
      continue;
    }
    if (const auto it = nomParNumero.find(b.numero); it != nomParNumero.end() && it->second != b.nom && b.nomVisible)
      r.push_back(a.id + " : le numéro " + b.numero + " porte deux broches (" + it->second + ", " + b.nom + ")");
    nomParNumero[b.numero] = b.nom;
  }
  if (a.empreinte.empty()) return r;
  if (!e) {
    r.push_back(a.id + " : empreinte « " + a.empreinte + " » introuvable");
    return r;
  }
  std::set<std::string> pastilles;
  for (const auto& p : e->pastilles)
    if (!p.numero.empty() && p.type != TypePastille::NonMetallisee) pastilles.insert(p.numero);
  for (const auto& [n, nom] : nomParNumero)
    if (!pastilles.count(n)) r.push_back(a.id + " : la broche " + n + " (" + nom + ") n'a pas de pastille dans " + e->id);
  for (const auto& n : pastilles)
    if (!nomParNumero.count(n) && n != "MP" && n != "SH" && n != "EP") r.push_back(a.id + " : la pastille " + n + " de " + e->id + " n'a pas de broche");
  return r;
}

ImportArchive importerArchiveComposant(const std::string& zip, const std::string& nomArchive) {
  ImportArchive r;
  std::vector<std::pair<std::string, std::string>> fichiers;
  try {
    fichiers = lireZip(zip);
  } catch (const std::exception& ex) {
    r.alertes.push_back(std::string("archive illisible : ") + ex.what());
    return r;
  }
  auto finit = [](const std::string& n, const char* ext) {
    std::string b;
    for (char ch : n) b += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const std::string e = ext;
    return b.size() >= e.size() && b.compare(b.size() - e.size(), e.size(), e) == 0;
  };
  for (const auto& [nom, contenu] : fichiers) {
    if (finit(nom, ".kicad_sym")) {
      for (auto& d : symbolesDepuisKicadSym(contenu, &r.alertes)) r.symboles.push_back(d);
    } else if (finit(nom, ".kicad_mod") && !r.empreinte) {
      r.empreinte = empreinteDepuisKicadMod(contenu, &r.alertes);
    } else if ((finit(nom, ".step") || finit(nom, ".stp") || finit(nom, ".wrl")) && r.modele3D.empty()) {
      r.nomModele3D = nom.substr(nom.find_last_of('/') == std::string::npos ? 0 : nom.find_last_of('/') + 1);
      r.modele3D = contenu;
    } else if (finit(nom, ".lib") || finit(nom, ".mod"))
      r.alertes.push_back("« " + nom + " » : ancien format de KiCad (5), non lu ; l'archive au format KiCad 6 ou plus convient");
  }
  if (r.symboles.empty()) {
    r.alertes.push_back("aucun symbole .kicad_sym dans l'archive " + nomArchive);
    return r;
  }
  const DefSymbole& s = r.symboles.front();
  Article& a = r.article;
  a.id = s.id.rfind("kicad:", 0) == 0 ? s.id.substr(6) : s.id;
  a.nom = s.nom.empty() ? a.id : s.nom;
  a.description = s.description.empty() ? "Importé de " + (nomArchive.empty() ? std::string("l'archive d'un portail") : nomArchive) : s.description;
  a.categorie = "Importés";
  a.symbole = s.id;
  a.valeur = a.nom;
  a.fabricant = s.fabricant;
  a.mpn = s.mpn.empty() ? a.nom : s.mpn;
  a.fiche = s.fiche;
  if (r.empreinte) a.empreinte = r.empreinte->id;
  if (!r.nomModele3D.empty()) a.modele3D = r.nomModele3D;
  r.controles = controlerArticle(a, &s, r.empreinte ? &*r.empreinte : nullptr);
  return r;
}

// ---------- Chemins ----------
std::string developperChemin(const std::string& chemin, const std::map<std::string, std::string>& reglages, std::vector<std::string>* inconnues) {
  std::string r;
  for (std::size_t i = 0; i < chemin.size();) {
    if (chemin.compare(i, 2, "${") == 0) {
      const std::size_t f = chemin.find('}', i + 2);
      if (f != std::string::npos) {
        const std::string nom = chemin.substr(i + 2, f - i - 2);
        if (const char* env = std::getenv(nom.c_str()); env && *env) r += env;
        else if (const auto it = reglages.find(nom); it != reglages.end()) r += it->second;
        else {
          r += chemin.substr(i, f - i + 1);
          if (inconnues) inconnues->push_back(nom);
        }
        i = f + 1;
        continue;
      }
    }
    r += chemin[i++];
  }
  return r;
}

}  // namespace prom::ecad
