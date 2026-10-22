// Prométhée : modèle unique carte + boîtier. Licence GPL-3.0-only.
#include "promethee/modele.hpp"
#include "promethee/depot.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/lecture.hpp"
#include "promethee/migrations.hpp"
#include "promethee/piece.hpp"
#include "promethee/structure.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>

namespace prom {

// ---------- Données de référence ----------
namespace {
constexpr std::size_t MAX_TROUS = 400, MAX_COMPOSANTS = 4000;
const std::vector<std::pair<std::string, Vis>>& tableVis() {
  static const std::vector<std::pair<std::string, Vis>> t = {
      {"M2", {2.0, 2.2, 1.6, 3.8}}, {"M2.5", {2.5, 2.7, 2.0, 4.5}}, {"M3", {3.0, 3.2, 2.5, 5.5}}, {"M4", {4.0, 4.3, 3.3, 7.0}}, {"M5", {5.0, 5.3, 4.2, 8.5}},
      {"M6", {6.0, 6.4, 5.0, 10.0}}};
  return t;
}
}  // namespace

bool visConnue(const std::string& nom) {
  for (const auto& [n, v] : tableVis()) if (n == nom) return true;
  return false;
}
const Vis& vis(const std::string& nom) {
  for (const auto& [n, v] : tableVis()) if (n == nom) return v;
  return tableVis()[2].second;  // M3 par défaut
}
const std::vector<std::string>& nomsVis() {
  static const std::vector<std::string> n = {"M2", "M2.5", "M3", "M4", "M5", "M6"};
  return n;
}

std::optional<Lamage> lamageISO4762(const std::string& nomVis) {
  // Mêmes valeurs que la table des trous de la mécanique (geometrie/src/construction.cpp), qu'un essai compare
  static const std::vector<std::pair<std::string, Lamage>> t = {{"M2", {4.3, 2.3}}, {"M2.5", {5.0, 2.8}}, {"M3", {6.5, 3.3}}, {"M4", {8.0, 4.4}}, {"M5", {10.0, 5.4}}, {"M6", {11.0, 6.4}}};
  for (const auto& [n, l] : t)
    if (n == nomVis) return l;
  return std::nullopt;
}

const Insert& insert(const std::string& nomVis) {
  static const std::vector<std::pair<std::string, Insert>> t = {{"M2", {3.2, 4.0}}, {"M2.5", {3.6, 5.7}}, {"M3", {4.0, 5.7}}, {"M4", {5.6, 8.1}}, {"M5", {6.4, 9.5}}, {"M6", {8.0, 12.7}}};
  for (const auto& [n, i] : t) if (n == nomVis) return i;
  return t[2].second;
}

const std::vector<TypeComposant>& typesComposants() {
  static const std::vector<TypeComposant> t = [] {
    std::vector<TypeComposant> v;
    auto ajout = [&v](TypeComposant x) { v.push_back(std::move(x)); };
    TypeComposant x;
    x = {}; x.cle = "module"; x.nom = "Module radio"; x.prefixe = "U"; x.w = 18; x.d = 20; x.h = 3.2; x.valeur = "ESP32-C3-WROOM-02"; ajout(x);
    x = {}; x.cle = "ic"; x.nom = "Circuit intégré"; x.prefixe = "U"; x.w = 6.5; x.d = 7; x.h = 1.8; x.valeur = "Régulateur 3,3 V"; ajout(x);
    x = {}; x.cle = "capteur"; x.nom = "Capteur"; x.prefixe = "U"; x.w = 2.5; x.d = 2.5; x.h = 0.93; x.valeur = "BME280"; ajout(x);
    x = {}; x.cle = "usbc"; x.nom = "Connecteur USB-C"; x.prefixe = "J"; x.w = 8.94; x.d = 7.35; x.h = 3.26; x.bord = true;
    x.decoupe = {12.5, 7, 2}; x.zc = 1.63; x.valeur = "USB-C 16 broches"; ajout(x);
    x = {}; x.cle = "jack"; x.nom = "Jack d\u2019alimentation"; x.prefixe = "J"; x.w = 9; x.d = 14; x.h = 11; x.bord = true;
    x.decoupe = {8, 8, 4}; x.zc = 6.3; x.valeur = "5,5 × 2,1 mm"; ajout(x);
    x = {}; x.cle = "jst"; x.nom = "Connecteur JST-PH"; x.prefixe = "J"; x.w = 6; x.d = 4.5; x.h = 6; x.valeur = "2 broches"; ajout(x);
    x = {}; x.cle = "led"; x.nom = "LED 3 mm"; x.prefixe = "D"; x.w = 3.2; x.d = 3.2; x.h = 5.3; x.rond = true; x.couvercle = 3.4; x.valeur = "Rouge"; ajout(x);
    x = {}; x.cle = "bouton"; x.nom = "Bouton poussoir"; x.prefixe = "SW"; x.w = 6; x.d = 6; x.h = 13; x.couvercle = 4.2; x.bouton = true;
    x.valeur = "6 × 6 mm, tige de 13 mm"; ajout(x);
    x = {}; x.cle = "condo"; x.nom = "Condensateur"; x.prefixe = "C"; x.w = 6.3; x.d = 6.3; x.h = 7.7; x.rond = true; x.valeur = "100 µF 16 V"; ajout(x);
    x = {}; x.cle = "generique"; x.nom = "Composant"; x.prefixe = "U"; x.w = 5; x.d = 5; x.h = 2; x.valeur = ""; ajout(x);
    x = {}; x.cle = "connecteur"; x.nom = "Connecteur de bord"; x.prefixe = "J"; x.w = 10; x.d = 8; x.h = 5; x.bord = true;
    x.decoupe = {11, 6, 1}; x.zc = 2.5; x.valeur = ""; ajout(x);
    return v;
  }();
  return t;
}
const TypeComposant* typeComposant(const std::string& cle) {
  for (const auto& t : typesComposants()) if (t.cle == cle) return &t;
  return nullptr;
}

char lettre(Bord b) {
  switch (b) { case Bord::N: return 'N'; case Bord::S: return 'S'; case Bord::E: return 'E'; default: return 'W'; }
}
std::optional<Bord> bordDe(const std::string& s) {
  if (s == "N") return Bord::N;
  if (s == "S") return Bord::S;
  if (s == "E") return Bord::E;
  if (s == "W") return Bord::W;
  return std::nullopt;
}

// ---------- Outils ----------
double arrondiJs(double x) {
  const double f = std::floor(x);
  return (x - f) >= 0.5 ? f + 1 : f;
}

std::string fmt(double v, int decimales) {
  if (!std::isfinite(v)) return "\u2013";
  const double k = std::pow(10.0, decimales);
  char tampon[64];
  std::snprintf(tampon, sizeof tampon, "%.*f", decimales, arrondiJs(v * k) / k);
  std::string s = tampon;
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  const auto point = s.find('.');
  if (point != std::string::npos) s.replace(point, 1, ",");
  const auto moins = s.find('-');
  if (moins != std::string::npos) s.replace(moins, 1, "\u2212");
  return s;
}

double sdRR(double px, double py, double cx, double cy, double hx, double hy, double r) {
  const double qx = std::abs(px - cx) - (hx - r), qy = std::abs(py - cy) - (hy - r);
  return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - r;
}

std::string nouvelId(const std::string& prefixe, const std::vector<std::string>& existants) {
  static std::mt19937 gen{std::random_device{}()};
  static const char* chiffres = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::uniform_int_distribution<int> tirage(0, 35);
  std::string id;
  do {
    id = prefixe;
    for (int i = 0; i < 6; ++i) id += chiffres[tirage(gen)];
  } while (std::find(existants.begin(), existants.end(), id) != existants.end());
  return id;
}

std::string prochaineRef(const Projet& p, const std::string& prefixe, const void* exclure) {
  std::set<std::string> pris;
  for (const auto& k : p.composants) if (&k != exclure) pris.insert(k.ref);
  for (const auto& t : p.trous) if (&t != exclure) pris.insert(t.ref);
  int i = 1;
  while (pris.count(prefixe + std::to_string(i))) ++i;
  return prefixe + std::to_string(i);
}

namespace {
const Json& champ(const Json& o, const char* cle) {
  static const Json nul;
  if (!o.is_object()) return nul;
  const auto it = o.find(cle);
  return it == o.end() ? nul : *it;
}
double nombre(const Json& v, double def) {
  if (v.is_number()) { const double n = v.get<double>(); return std::isfinite(n) ? n : def; }
  if (v.is_string()) {
    const std::string s = v.get<std::string>();
    char* fin = nullptr;
    const double n = std::strtod(s.c_str(), &fin);
    if (fin != s.c_str() && std::isfinite(n)) return n;
  }
  return def;
}
double borne(double v, double a, double b) { return std::min(b, std::max(a, v)); }
bool espace(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
std::string rogner(const std::string& s) {
  size_t a = 0, b = s.size();
  for (;;) {
    if (a < b && espace(static_cast<unsigned char>(s[a]))) { ++a; continue; }
    if (a + 1 < b && static_cast<unsigned char>(s[a]) == 0xC2 && static_cast<unsigned char>(s[a + 1]) == 0xA0) { a += 2; continue; }
    break;
  }
  for (;;) {
    if (b > a && espace(static_cast<unsigned char>(s[b - 1]))) { --b; continue; }
    if (b >= a + 2 && static_cast<unsigned char>(s[b - 2]) == 0xC2 && static_cast<unsigned char>(s[b - 1]) == 0xA0) { b -= 2; continue; }
    break;
  }
  return s.substr(a, b - a);
}
// Tronque à n unités UTF-16, comme String.prototype.slice.
std::string tronquer(const std::string& s, size_t n) {
  size_t i = 0, unites = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
    const size_t u = len == 4 ? 2 : 1;
    if (unites + u > n) break;
    unites += u; i += len;
  }
  return s.substr(0, i);
}
std::string chaine(const Json& v, size_t n) {
  if (!v.is_string()) return "";
  const std::string t = rogner(v.get<std::string>());
  return t.empty() ? "" : tronquer(t, n);
}
bool parmi(double v, std::initializer_list<double> l) {
  for (double x : l) if (v == x) return true;
  return false;
}
}  // namespace

Projet normaliser(const Json& s, std::vector<std::string>* avertissements) {
  auto signaler = [avertissements](const std::string& texte) {
    if (avertissements) avertissements->push_back(texte);
  };
  const Json& jversion = champ(s, "version");
  if (jversion.is_number_integer() && jversion.get<int>() > VERSION_FORMAT)
    throw ErreurFichier("Ce projet a été enregistré par une version plus récente de Prométhée (format " + std::to_string(jversion.get<int>()) +
                        ", ce logiciel lit jusqu’au format " + std::to_string(VERSION_FORMAT) + "). Mets le logiciel à jour pour l’ouvrir.");
  // Migrations : les formats 1 (prototype web, versions 0.1 à 0.5) et 2 (0.6) se lisent tels quels, leurs nouveautés étant facultatives ;
  // le format 3 ajoute les pièces paramétriques (« pieces »).
  Projet p;
  const Json& c = champ(s, "carte");
  const Json& b = champ(s, "boitier");
  const std::string nom = chaine(champ(s, "nom"), 80);
  p.nom = nom.empty() ? "Projet sans nom" : nom;
  p.sansCarte = s.is_object() && s.value("sansCarte", false);
  if (s.is_object() && s.contains("schemasElectriques") && s["schemasElectriques"].is_array())
    for (const auto& o : s["schemasElectriques"])
      if (o.is_object()) p.schemasElectriques.push_back({o.value("fichier", std::string()), o.value("titre", std::string())});
  if (s.is_object() && s.contains("assemblages") && s["assemblages"].is_array()) {
    // Lecture tolérante (AS-28) : une valeur mal typée garde son défaut et est signalée ; les rangs impossibles
    // sont écartés plus bas par validerAssemblages, une fois les pièces lues.
    int na = 0;
    for (const auto& o : s["assemblages"]) {
      ++na;
      if (!o.is_object()) { signaler("Assemblage n° " + std::to_string(na) + " illisible : ignoré."); continue; }
      Assemblage a;
      const LecteurJson la(avertissements, "Assemblage n° " + std::to_string(na));
      a.nom = la.texte(o, "nom", "Assemblage");
      a.repere = std::max(0, la.entier(o, "repere", 0));
      const LecteurJson l = la.sous("« " + a.nom + " »");
      if (o.contains("composants") && o["composants"].is_array()) {
        int k = 0;
        for (const auto& c : o["composants"]) {
          ++k;
          const LecteurJson lc = l.sous("composant n° " + std::to_string(k));
          if (!c.is_object()) { lc.alerter("illisible, ignoré"); continue; }
          ComposantAssemblage x;
          x.piece = lc.entier(c, "piece", 0);
          x.x = lc.nombre(c, "x", 0); x.y = lc.nombre(c, "y", 0); x.z = lc.nombre(c, "z", 0);
          x.rx = lc.nombre(c, "rx", 0); x.ry = lc.nombre(c, "ry", 0); x.rz = lc.nombre(c, "rz", 0);
          x.fixe = lc.booleen(c, "fixe", false);
          x.assemblage = std::max(-1, lc.entier(c, "assemblage", -1));
          x.miroir = lc.entier(c, "miroir", 0);
          if (x.miroir < 0 || x.miroir > 3) { lc.alerter("symétrie inconnue, ignorée"); x.miroir = 0; }
          x.masque = lc.booleen(c, "masque", false);
          x.supprime = lc.booleen(c, "supprime", false);
          a.composants.push_back(x);
        }
      }
      if (o.contains("contraintes") && o["contraintes"].is_array()) {
        int k = 0;
        for (const auto& c : o["contraintes"]) {
          ++k;
          const LecteurJson lc = l.sous("contrainte n° " + std::to_string(k));
          if (!c.is_object()) { lc.alerter("illisible, ignorée"); continue; }
          ContrainteAssemblage x;
          x.type = lc.texte(c, "type", "coincidence");
          x.a = lc.entier(c, "a", 0);
          x.b = lc.entier(c, "b", 1);
          x.refA = lc.texte(c, "refA", "");
          x.refB = lc.texte(c, "refB", "");
          x.valeur = lc.nombre(c, "valeur", 0);
          x.inverse = lc.booleen(c, "inverse", false);
          x.suspendue = lc.booleen(c, "suspendue", false);
          x.alignement = std::clamp(lc.entier(c, "alignement", 0), 0, 2);
          x.pilotee = lc.booleen(c, "pilotee", false);
          if (c.contains("parametres")) {
            if (!c["parametres"].is_array()) lc.alerter("« parametres » n'est pas une liste, ignorée");
            else
              for (const auto& v : c["parametres"]) {
                if (v.is_number() && std::isfinite(v.get<double>())) x.parametres.push_back(v.get<double>());
                else { lc.alerter("paramètre illisible : liaison remise à son état de départ"); x.parametres.clear(); break; }
              }
          }
          if (c.contains("angleSuivi")) {
            double u = 0, v = 0;
            if (c["angleSuivi"].is_array() && c["angleSuivi"].size() == 2 && c["angleSuivi"][0].is_number() && c["angleSuivi"][1].is_number() &&
                std::isfinite(u = c["angleSuivi"][0].get<double>()) && std::isfinite(v = c["angleSuivi"][1].get<double>()))
              x.angleSuivi = {u, v};
            else lc.alerter("« angleSuivi » illisible, remis à zéro");
          }
          a.contraintes.push_back(x);
        }
      }
      p.assemblages.push_back(std::move(a));
    }
  }
  if (s.is_object() && s.contains("electrique") && s["electrique"].is_array()) {
    for (const auto& o : s["electrique"]) {
      if (o.is_object()) {
        try {
          p.electrique.push_back(elec::projetElectriqueDepuisJson(o, avertissements));
        } catch (const std::exception& e) {
          p.electriqueIllisible.push_back(o);
          signaler("Projet électrique « " + (o.contains("nom") && o["nom"].is_string() ? o["nom"].get<std::string>() : std::string("sans nom")) +
                   " » illisible : il n'est pas affiché mais reste dans le fichier (" + e.what() + ").");
        }
      } else {
        p.electriqueIllisible.push_back(o);
        signaler("Un projet électrique n'est pas un objet lisible : il reste dans le fichier sans être affiché.");
      }
    }
  }
  if (s.is_object() && s.contains("electronique") && s["electronique"].is_array()) {
    for (const auto& o : s["electronique"]) {
      if (o.is_object()) {
        try {
          p.electronique.push_back(ecad::conceptionDepuisJson(o, avertissements));
        } catch (const std::exception& e) {
          // Une conception illisible est gardée telle quelle (réécrite à l'enregistrement), et la perte est dite
          p.electroniqueIllisible.push_back(o);
          signaler("Conception électronique « " + (o.contains("nom") && o["nom"].is_string() ? o["nom"].get<std::string>() : std::string("sans nom")) +
                   " » illisible : elle n'est pas affichée mais reste dans le fichier (" + e.what() + ").");
        }
      } else {
        p.electroniqueIllisible.push_back(o);
        signaler("Une conception électronique n'est pas un objet lisible : elle reste dans le fichier sans être affichée.");
      }
    }
  }
  const double t0 = nombre(champ(c, "t"), 1.6);
  p.carte.x0 = nombre(champ(c, "x0"), 0);
  p.carte.y0 = nombre(champ(c, "y0"), 0);
  p.carte.L = borne(nombre(champ(c, "L"), 60), 10, 300);
  p.carte.W = borne(nombre(champ(c, "W"), 40), 10, 300);
  p.carte.t = std::find(EPAISSEURS_PCB.begin(), EPAISSEURS_PCB.end(), t0) != EPAISSEURS_PCB.end() ? t0 : 1.6;
  p.carte.r = borne(nombre(champ(c, "r"), 2), 0, std::min(p.carte.L, p.carte.W) / 2 - 0.5);
  const Json& jcontours = champ(c, "contours");
  if (jcontours.is_array()) {
    auto point = [](const Json& v, Point& q) {
      if (!v.is_array() || v.size() != 2 || !v[0].is_number() || !v[1].is_number()) return false;
      q = {v[0].get<double>(), v[1].get<double>()};
      return std::isfinite(q.x) && std::isfinite(q.y);
    };
    for (const auto& jc : jcontours) {
      Contour k;
      bool valide = champ(jc, "elements").is_array();
      if (valide)
        for (const auto& je : champ(jc, "elements")) {
          ElementContour e;
          if (!point(champ(je, "debut"), e.debut) || !point(champ(je, "fin"), e.fin)) { valide = false; break; }
          Point mil{};
          if (point(champ(je, "milieu"), mil)) e.milieu = mil;
          k.elements.push_back(e);
        }
      if (!valide || k.elements.size() < 2) continue;
      k.ferme = std::hypot(k.elements.front().debut.x - k.elements.back().fin.x, k.elements.front().debut.y - k.elements.back().fin.y) < 1e-3;
      if (k.ferme && std::abs(k.aire()) > 1) p.carte.contours.push_back(k);
    }
    if (!p.carte.contours.empty()) {
      orienterContours(p.carte.contours);
      recalerEncombrement(p.carte);
    }
  }

  auto& bo = p.boitier;
  bo.jeu = borne(nombre(champ(b, "jeu"), 1), 0.2, 10);
  bo.paroi = borne(nombre(champ(b, "paroi"), 2), 0.8, 8);
  bo.fond = borne(nombre(champ(b, "fond"), 2), 0.8, 8);
  bo.entretoise = borne(nombre(champ(b, "entretoise"), 5), 1, 30);
  bo.couvercle = borne(nombre(champ(b, "couvercle"), 2), 0.8, 8);
  const Json& jv = champ(b, "vis");
  bo.vis = jv.is_string() && visConnue(jv.get<std::string>()) ? jv.get<std::string>() : "M3";
  p.boitier.margeDebord = borne(nombre(champ(b, "margeDebord"), 1), 0, 20);
  if (const Json& jk = champ(s, "kicad"); jk.is_object()) {
    SourceKicad sk;
    sk.fichier = chaine(champ(jk, "fichier"), 4000);
    sk.dx = nombre(champ(jk, "dx"), 0);
    sk.dy = nombre(champ(jk, "dy"), 0);
    p.kicad = sk;
  }
  const Json& ja = champ(b, "hauteurAuto");
  bo.hauteurAuto = ja.is_boolean() ? ja.get<bool>() : true;
  bo.hauteur = borne(nombre(champ(b, "hauteur"), 20), bo.entretoise + p.carte.t + 1, 200);
  if (const Json& jt = champ(b, "type"); jt.is_string() && (jt.get<std::string>() == "coques" || jt.get<std::string>() == "couvercle")) bo.type = jt.get<std::string>();
  if (const Json& ji = champ(b, "impression"); ji.is_object()) {
    if (const Json& m = champ(ji, "matiere"); m.is_string()) bo.impression.matiere = chaine(m, 40);
    bo.impression.densite = borne(nombre(champ(ji, "densite"), 1.27), 0.1, 20);
    bo.impression.prixKg = borne(nombre(champ(ji, "prixKg"), 25), 0, 10000);
    bo.impression.debit = borne(nombre(champ(ji, "debit"), 8), 0.1, 1000);
    bo.impression.prixHeure = borne(nombre(champ(ji, "prixHeure"), 1.5), 0, 10000);
  }

  std::vector<std::string> ids;
  auto prendreId = [&ids](const Json& v, const char* prefixe) {
    std::string id;
    if (v.is_string() && !v.get<std::string>().empty() &&
        std::find(ids.begin(), ids.end(), v.get<std::string>()) == ids.end())
      id = v.get<std::string>();
    else
      id = nouvelId(prefixe, ids);
    ids.push_back(id);
    return id;
  };

  const Json& jt = champ(s, "trous");
  if (jt.is_array()) {
    size_t n = 0;
    for (const auto& t : jt) {
      if (n++ >= MAX_TROUS) {
        signaler(std::to_string(jt.size() - MAX_TROUS) + " trous de fixation au-delà de " + std::to_string(MAX_TROUS) + " ignorés.");
        break;
      }
      if (!(t.is_object() || t.is_array())) continue;
      Trou o;
      o.id = prendreId(champ(t, "id"), "t");
      o.ref = chaine(champ(t, "ref"), 12);
      const Json& coin = champ(t, "coin");
      const std::string cs = coin.is_string() ? coin.get<std::string>() : "";
      if (cs == "NE" || cs == "NW" || cs == "SE" || cs == "SW") {
        o.coin = cs;
        o.ox = borne(nombre(champ(t, "ox"), 4), -300, 300);
        o.oy = borne(nombre(champ(t, "oy"), 4), -300, 300);
      } else {
        o.x = nombre(champ(t, "x"), p.carte.x0);
        o.y = nombre(champ(t, "y"), p.carte.y0);
      }
      const Json& ja = champ(t, "ancrage");
      const std::string as = ja.is_string() ? ja.get<std::string>() : "";
      if (as == "libre" || as == "NE" || as == "NW" || as == "SE" || as == "SW") o.ancrage = as;
      const Json& jv = champ(t, "vis");
      if (jv.is_string() && visConnue(jv.get<std::string>())) o.vis = jv.get<std::string>();
      const Json& jf = champ(t, "fixation");
      if (jf.is_string() && jf.get<std::string>() == "insert") o.fixation = "insert";
      if (const Json& jp = champ(t, "pose"); jp.is_string() && jp.get<std::string>() == "dessous") o.pose = "dessous";
      if (const Json& jn = champ(t, "nervures"); jn.is_boolean()) o.nervures = jn.get<bool>();
      const Json& dims = champ(t, "dimensions");
      auto dimension = [&dims](const char* cle, double a, double b) -> std::optional<double> {
        const Json& v = champ(dims, cle);
        if (!v.is_number()) return std::nullopt;
        return borne(v.get<double>(), a, b);
      };
      o.diamTrou = dimension("trou", 0.5, 20);
      o.diamPastille = dimension("pastille", 1, 30);
      o.diamPilier = dimension("pilier", 2, 40);
      o.diamLogement = dimension("logement", 0.5, 30);
      o.longueurInsert = dimension("insert", 1, 30);
      p.trous.push_back(o);
    }
  }
  const Json& jc = champ(s, "composants");
  if (jc.is_array()) {
    size_t n = 0;
    for (const auto& k : jc) {
      if (n++ >= MAX_COMPOSANTS) {
        signaler(std::to_string(jc.size() - MAX_COMPOSANTS) + " composants au-delà de " + std::to_string(MAX_COMPOSANTS) + " ignorés.");
        break;
      }
      if (!(k.is_object() || k.is_array())) continue;
      const Json& jtype = champ(k, "type");
      const TypeComposant* T = jtype.is_string() ? typeComposant(jtype.get<std::string>()) : nullptr;
      if (!T) {
        signaler("Composant « " + chaine(champ(k, "ref"), 12) + " » de type inconnu (" + (jtype.is_string() ? jtype.get<std::string>() : std::string("sans type")) +
                 ") ignoré.");
        continue;
      }
      Composant o;
      o.id = prendreId(champ(k, "id"), "c");
      o.ref = chaine(champ(k, "ref"), 12);
      o.type = T->cle;
      const Json& jval = champ(k, "valeur");
      o.valeur = jval.is_string() ? tronquer(jval.get<std::string>(), 60) : T->valeur;
      o.w = borne(nombre(champ(k, "w"), T->w), 0.5, 150);
      o.d = borne(nombre(champ(k, "d"), T->d), 0.5, 150);
      o.h = borne(nombre(champ(k, "h"), T->h), 0.2, 100);
      if (T->bord) {
        const Json& jb = champ(k, "bord");
        const auto bd = jb.is_string() ? bordDe(jb.get<std::string>()) : std::nullopt;
        o.bord = bd ? *bd : Bord::W;
        o.le_long = nombre(champ(k, "le_long"), horizontal(*o.bord) ? p.carte.x0 : p.carte.y0);
        o.ecart = borne(nombre(champ(k, "ecart"), 0.4), 0, 10);
        const Json& jd = champ(k, "decoupe");
        const Json dc = jd.is_object() ? jd : Json::object();
        o.decoupe.w = borne(nombre(champ(dc, "w"), T->decoupe.w), 1, 100);
        o.decoupe.h = borne(nombre(champ(dc, "h"), T->decoupe.h), 1, 100);
        o.decoupe.r = borne(nombre(champ(dc, "r"), T->decoupe.r), 0, std::min(o.decoupe.w, o.decoupe.h) / 2);
        o.zc = borne(nombre(champ(k, "zc"), T->zc), 0, 100);
      } else {
        o.x = nombre(champ(k, "x"), p.carte.x0);
        o.y = nombre(champ(k, "y"), p.carte.y0);
        const double r = nombre(champ(k, "rot"), 0);
        o.rot = parmi(r, {0, 90, 180, 270}) ? static_cast<int>(r) : 0;
      }
      if (T->couvercle > 0) o.trou_couvercle = borne(nombre(champ(k, "trou_couvercle"), T->couvercle), 0.5, 30);
      const Json& ja = champ(k, "ancrage");
      if (ja.is_string() && ancrageValide(o, ja.get<std::string>()) && ja.get<std::string>() != "libre") {
        // Sans décalages enregistrés, on les déduit de la position absolue.
        const bool decalages = champ(k, "ax").is_number();
        const std::string a = ja.get<std::string>();
        if (decalages) {
          o.ancrage = a;
          o.ax = borne(nombre(champ(k, "ax"), 0), -300, 300);
          o.ay = borne(nombre(champ(k, "ay"), 0), -300, 300);
        } else {
          changerAncrage(p, o, a);
        }
      }
      const Json& jverrou = champ(k, "verrou");
      o.verrou = jverrou.is_boolean() && jverrou.get<bool>();
      const Json& jorigine = champ(k, "origine");
      if (jorigine.is_string() && jorigine.get<std::string>() == "kicad") o.origine = "kicad";
      const Json& jface = champ(k, "face");
      o.dessous = !o.bord && jface.is_string() && jface.get<std::string>() == "dessous";
      const Json& jsource = champ(k, "sourceHauteur");
      const std::string source = jsource.is_string() ? jsource.get<std::string>() : std::string();
      if (source == "type" || source == "estimee" || source == "nominale" || source == "saisie" || source == "verifiee") o.sourceHauteur = source;
      else o.sourceHauteur = o.origine == "kicad" ? "estimee" : "type";  // imports KiCad d'avant la 0.25 : hauteurs estimées
      p.composants.push_back(o);
    }
  }
  for (auto& t : p.trous) if (t.ref.empty()) t.ref = prochaineRef(p, "T", &t);
  for (auto& k : p.composants) if (k.ref.empty()) k.ref = prochaineRef(p, typeComposant(k.type)->prefixe, &k);
  // Géométries importées rangées à part dans le fichier (EX-06) : versées au dépôt, contrôlées par leur empreinte
  if (const Json& jg = champ(s, "geometries"); jg.is_object())
    for (auto it = jg.begin(); it != jg.end(); ++it) {
      if (!it.value().is_string()) continue;
      const std::string ref = deposerGeometrie(it.value().get<std::string>());
      if (ref != it.key()) signaler("Géométrie importée « " + it.key().substr(0, 20) + "… » abîmée : son empreinte ne correspond plus à son contenu.");
    }
  const Json& jpieces = champ(s, "pieces");
  if (jpieces.is_array()) {
    int n = 0;
    for (Json x : jpieces) {
      // Ancien fichier : un texte BRep gardé dans la fonction d'import passe au dépôt, la pièce ne garde que sa référence
      if (x.is_object() && x.contains("fonctions") && x["fonctions"].is_array())
        for (auto& f : x["fonctions"])
          if (f.is_object() && f.contains("donnees") && f["donnees"].is_string() && f["donnees"].get_ref<const std::string&>().size() >= TAILLE_DEPOT_MIN)
            f["donnees"] = deposerGeometrie(f["donnees"].get<std::string>());
      ++n;
      if (!x.is_object()) { signaler("Pièce n° " + std::to_string(n) + " illisible : ignorée."); continue; }
      // La pièce reste sous sa forme JSON (rien n'est perdu) ; sa relecture signale ce qui ne sera pas compris
      std::vector<std::string> alertes;
      if (avertissements) try {
        (void)pieceDepuisJson(x, &alertes);
      } catch (const std::exception& e) {
        alertes.push_back(std::string("illisible (") + e.what() + ")");
      }
      const std::string nomPiece = x.contains("nom") && x["nom"].is_string() ? x["nom"].get<std::string>() : "n° " + std::to_string(n);
      for (const auto& a : alertes) signaler("Pièce « " + nomPiece + " » : " + a);
      p.pieces.push_back(x);
    }
  }
  assurerIdentifiantsPieces(p, avertissements);
  assurerReperes(p);
  validerAssemblages(p, avertissements);
  // Dispositions des panneaux enregistrées avec le projet : des chaînes, les autres valeurs écartées
  if (s.is_object() && s.contains("dispositions") && s["dispositions"].is_object())
    for (auto it = s["dispositions"].begin(); it != s["dispositions"].end(); ++it)
      if (it.value().is_string()) p.dispositions[it.key()] = it.value().get<std::string>();
  // Champs inconnus : conservés pour la réécriture, et signalés.
  static const std::set<std::string> connus = {"format", "version", "nom", "sansCarte", "schemasElectriques", "assemblages", "electronique", "electrique",
                                                "carte", "boitier", "kicad", "trous", "composants", "pieces", "geometries", "dispositions"};
  if (s.is_object())
    for (auto it = s.begin(); it != s.end(); ++it)
      if (!connus.count(it.key())) {
        p.inconnus[it.key()] = it.value();
        signaler("Champ « " + it.key() + " » inconnu de cette version : conservé tel quel.");
      }
  return p;
}

Json versJson(const Projet& p, bool avecGeometries) {
  Json j;
  if (!p.dispositions.empty()) {
    Json d = Json::object();
    for (const auto& [nom, etat] : p.dispositions) d[nom] = etat;
    j["dispositions"] = d;
  }
  j["format"] = "promethee-projet";
  j["version"] = VERSION_FORMAT;
  j["nom"] = p.nom;
  if (p.sansCarte) j["sansCarte"] = true;
  if (!p.schemasElectriques.empty()) {
    Json l = Json::array();
    for (const auto& sc : p.schemasElectriques) l.push_back({{"fichier", sc.fichier}, {"titre", sc.titre}});
    j["schemasElectriques"] = l;
  }
  if (!p.assemblages.empty()) {
    Json l = Json::array();
    for (const auto& a : p.assemblages) {
      Json cs = Json::array();
      for (const auto& c : a.composants) {
        Json o = {{"piece", c.piece}, {"x", c.x}, {"y", c.y}, {"z", c.z}, {"rx", c.rx}, {"ry", c.ry}, {"rz", c.rz}};
        if (c.fixe) o["fixe"] = true;
        if (c.assemblage >= 0) o["assemblage"] = c.assemblage;
        if (c.miroir) o["miroir"] = c.miroir;
        if (c.masque) o["masque"] = true;
        if (c.supprime) o["supprime"] = true;
        cs.push_back(o);
      }
      Json ks = Json::array();
      for (const auto& k : a.contraintes) {
        Json o = {{"type", k.type}, {"a", k.a}, {"b", k.b}, {"refA", k.refA}, {"refB", k.refB}, {"valeur", k.valeur}, {"inverse", k.inverse}};
        if (k.suspendue) o["suspendue"] = true;
        if (k.alignement) o["alignement"] = k.alignement;
        if (k.pilotee) o["pilotee"] = true;
        if (!k.parametres.empty()) o["parametres"] = k.parametres;
        if (k.angleSuivi[0] != 0 || k.angleSuivi[1] != 0) o["angleSuivi"] = {k.angleSuivi[0], k.angleSuivi[1]};
        ks.push_back(o);
      }
      Json ja = {{"nom", a.nom}, {"composants", cs}, {"contraintes", ks}};
      if (a.repere > 0) ja["repere"] = a.repere;
      l.push_back(ja);
    }
    j["assemblages"] = l;
  }
  if (!p.electronique.empty() || !p.electroniqueIllisible.empty()) {
    Json l = Json::array();
    for (const auto& c : p.electronique) l.push_back(ecad::versJson(c));
    for (const auto& x : p.electroniqueIllisible) l.push_back(x);  // réécrites telles quelles (EL-07)
    j["electronique"] = l;
  }
  if (!p.electrique.empty() || !p.electriqueIllisible.empty()) {
    Json l = Json::array();
    for (const auto& e : p.electrique) l.push_back(elec::versJson(e));
    for (const auto& x : p.electriqueIllisible) l.push_back(x);
    j["electrique"] = l;
  }
  j["carte"] = {{"x0", p.carte.x0}, {"y0", p.carte.y0}, {"L", p.carte.L}, {"W", p.carte.W}, {"r", p.carte.r}, {"t", p.carte.t}};
  if (p.carte.libre()) {
    Json contours = Json::array();
    for (const auto& k : p.carte.contours) {
      Json elements = Json::array();
      for (const auto& e : k.elements) {
        Json je = {{"debut", {e.debut.x, e.debut.y}}, {"fin", {e.fin.x, e.fin.y}}};
        if (e.milieu) je["milieu"] = {e.milieu->x, e.milieu->y};
        elements.push_back(je);
      }
      contours.push_back({{"elements", elements}});
    }
    j["carte"]["contours"] = contours;
  }
  const auto& b = p.boitier;
  j["boitier"] = {{"jeu", b.jeu}, {"paroi", b.paroi}, {"fond", b.fond}, {"entretoise", b.entretoise},
                  {"hauteur", b.hauteur}, {"couvercle", b.couvercle}, {"vis", b.vis}, {"hauteurAuto", b.hauteurAuto}};
  if (std::abs(b.margeDebord - 1) > 1e-9) j["boitier"]["margeDebord"] = b.margeDebord;
  if (b.type != "couvercle") j["boitier"]["type"] = b.type;
  if (!(b.impression == Impression{}))
    j["boitier"]["impression"] = {{"matiere", b.impression.matiere}, {"densite", b.impression.densite}, {"prixKg", b.impression.prixKg}, {"debit", b.impression.debit}, {"prixHeure", b.impression.prixHeure}};
  if (p.kicad) j["kicad"] = {{"fichier", p.kicad->fichier}, {"dx", p.kicad->dx}, {"dy", p.kicad->dy}};
  j["trous"] = Json::array();
  for (const auto& t : p.trous) {
    Json o = {{"id", t.id}, {"ref", t.ref}};
    if (t.coin) { o["coin"] = *t.coin; o["ox"] = t.ox; o["oy"] = t.oy; }
    else { o["x"] = t.x; o["y"] = t.y; }
    if (t.ancrage != "auto") o["ancrage"] = t.ancrage;
    if (t.vis) o["vis"] = *t.vis;
    if (t.fixation != "autotaraudeuse") o["fixation"] = t.fixation;
    if (t.pose != "dessus") o["pose"] = t.pose;
    if (t.nervures) o["nervures"] = true;
    Json dims = Json::object();
    if (t.diamTrou) dims["trou"] = *t.diamTrou;
    if (t.diamPastille) dims["pastille"] = *t.diamPastille;
    if (t.diamPilier) dims["pilier"] = *t.diamPilier;
    if (t.diamLogement) dims["logement"] = *t.diamLogement;
    if (t.longueurInsert) dims["insert"] = *t.longueurInsert;
    if (!dims.empty()) o["dimensions"] = dims;
    j["trous"].push_back(o);
  }
  j["composants"] = Json::array();
  for (const auto& k : p.composants) {
    Json o = {{"id", k.id}, {"ref", k.ref}, {"type", k.type}, {"valeur", k.valeur}, {"w", k.w}, {"d", k.d}, {"h", k.h}};
    if (k.bord) {
      o["bord"] = std::string(1, lettre(*k.bord));
      o["le_long"] = leLongComposant(p, k);
      o["ecart"] = k.ecart;
      o["decoupe"] = {{"w", k.decoupe.w}, {"h", k.decoupe.h}, {"r", k.decoupe.r}};
      o["zc"] = k.zc;
    } else {
      const Point q = posComposant(p, k);
      o["x"] = q.x; o["y"] = q.y; o["rot"] = k.rot;
    }
    if (k.trou_couvercle) o["trou_couvercle"] = *k.trou_couvercle;
    if (k.ancrage != "libre") {
      o["ancrage"] = k.ancrage;
      o["ax"] = k.ax;
      if (!k.bord) o["ay"] = k.ay;
    }
    if (k.verrou) o["verrou"] = true;
    if (!k.origine.empty()) o["origine"] = k.origine;
    if (k.dessous) o["face"] = "dessous";
    if (k.sourceHauteur != "type") o["sourceHauteur"] = k.sourceHauteur;
    j["composants"].push_back(o);
  }
  if (!p.pieces.empty()) j["pieces"] = p.pieces;
  // Textes des géométries importées, une fois chacun, à part des pièces qui les citent (EX-06)
  if (avecGeometries) {
    Json g = Json::object();
    for (const auto& x : p.pieces)
      if (x.is_object() && x.contains("fonctions") && x["fonctions"].is_array())
        for (const auto& f : x["fonctions"])
          if (f.is_object() && f.contains("donnees") && f["donnees"].is_string() && estReferenceGeometrie(f["donnees"].get_ref<const std::string&>())) {
            const std::string& ref = f["donnees"].get_ref<const std::string&>();
            if (g.contains(ref)) continue;
            if (const auto t = geometrieDeposee(ref)) g[ref] = *t;
          }
    if (!g.empty()) j["geometries"] = g;
  }
  if (p.inconnus.is_object())
    for (auto it = p.inconnus.begin(); it != p.inconnus.end(); ++it)
      if (!j.contains(it.key())) j[it.key()] = it.value();
  return j;
}

namespace {
std::string nomCourt(const std::string& chemin) { return "« " + utf8(cheminFichier(chemin).filename()) + " »"; }
}  // namespace

namespace {
int formatDe(const Json& j) {
  if (j.is_discarded() || !j.is_object()) return 0;
  if (champ(j, "format") != Json("promethee-projet")) return j.contains("carte") ? 1 : 0;
  const Json& v = champ(j, "version");
  return v.is_number_integer() ? std::max(1, v.get<int>()) : 1;
}
// Une carte électronique écrite par la 0.28 (format électronique 1, sans clé) : la 0.28 relirait mal le fichier
// réécrit, dont les textes de carte tournent désormais dans le sens des empreintes
bool porteCarte028(const Json& j) {
  if (!j.is_object() || !j.contains("electronique") || !j["electronique"].is_array()) return false;
  return std::any_of(j["electronique"].begin(), j["electronique"].end(), [](const Json& c) { return c.is_object() && versionConception(c) == "0.28"; });
}
}  // namespace

int formatProjet(const std::string& contenu) { return formatDe(Json::parse(contenu, nullptr, false)); }

Projet lireProjet(const std::string& chemin, RapportLecture* rapport) {
  const std::string texte = lireFichier(cheminFichier(chemin));
  const std::string nom = nomCourt(chemin);
  if (texte.find_first_not_of(" \t\r\n") == std::string::npos)
    throw ErreurFichier(nom + " est vide : l’enregistrement a sans doute été interrompu. Cherche une copie de récupération ou une sauvegarde.");
  Json j;
  try {
    j = Json::parse(texte);
  } catch (const Json::parse_error& e) {
    const std::size_t octet = e.byte;
    if (octet + 1 >= texte.size())
      throw ErreurFichier(nom + " est tronqué : il s’arrête au bout de " + std::to_string(texte.size()) +
                          " octets, avant la fin du projet (enregistrement interrompu, copie incomplète ?). Cherche une copie de récupération ou une sauvegarde.");
    throw ErreurFichier(nom + " n’est pas un fichier JSON valide : contenu illisible à l’octet " + std::to_string(octet) + " sur " + std::to_string(texte.size()) +
                        " (fichier abîmé ou d’un autre type).");
  }
  if (!j.is_object() || (champ(j, "format") != Json("promethee-projet") && !j.contains("carte")))
    throw ErreurFichier(nom + " n’est pas un projet Prométhée.");
  RapportLecture r;
  r.versionFormat = formatProjet(texte);
  Projet p = normaliser(j, &r.avertissements);
  if (r.versionFormat < VERSION_FORMAT)
    r.avertissements.push_back("Projet au format " + std::to_string(r.versionFormat) + " : il sera réécrit au format " + std::to_string(VERSION_FORMAT) +
                               " à l’enregistrement, après une copie de l’original.");
  else if (porteCarte028(j))
    r.avertissements.push_back("Carte électronique écrite par la 0.28 : elle sera réécrite au format " + versionFichierCourante() +
                               " à l’enregistrement, après une copie de l’original (« .format1.bak »), que la 0.28 relit toujours.");
  if (rapport) *rapport = std::move(r);
  return p;
}

void ecrireProjet(const Projet& p, const std::string& chemin) {
  namespace fs = std::filesystem;
  const fs::path cible = cheminFichier(chemin);
  std::error_code ec;
  if (fs::is_regular_file(cible, ec)) {
    std::string ancien;
    try {
      ancien = lireFichier(cible);
    } catch (const ErreurFichier&) {
      ancien.clear();
    }
    const Json existant = Json::parse(ancien, nullptr, false);
    const int format = formatDe(existant);
    if (format > VERSION_FORMAT)
      throw ErreurFichier("Enregistrement refusé : " + nomCourt(chemin) + " est un projet au format " + std::to_string(format) +
                          ", plus récent que ce logiciel (format " + std::to_string(VERSION_FORMAT) + "). L’écraser perdrait ce qu’il contient : enregistre sous un autre nom.");
    if (format > 0 && format < VERSION_FORMAT) {
      // Copie de secours avant migration : l'ancien logiciel peut toujours relire l'original.
      fs::path copie = cible;
      copie += ".format" + std::to_string(format) + ".bak";
      if (!fs::exists(copie, ec)) ecrireFichierAtomique(copie, ancien);
    } else if (format == VERSION_FORMAT && porteCarte028(existant)) {
      // Même copie pour une carte de la 0.28 (format électronique 1), montée en mémoire à la lecture (1.22)
      fs::path copie = cible;
      copie += ".format1.bak";
      if (!fs::exists(copie, ec)) ecrireFichierAtomique(copie, ancien);
    }
  }
  ecrireFichierAtomique(cible, versJson(p).dump(2) + "\n");
}

Projet exemple() {
  return normaliser(Json::parse(R"({
    "nom": "Station météo ESP32",
    "carte": {"x0": 0, "y0": 0, "L": 60, "W": 40, "r": 2, "t": 1.6},
    "boitier": {"jeu": 1, "paroi": 2, "fond": 2, "entretoise": 5, "hauteur": 18, "couvercle": 2, "vis": "M3", "hauteurAuto": true},
    "trous": [
      {"id": "t1", "ref": "T1", "coin": "SW", "ox": 4.5, "oy": 4.5},
      {"id": "t2", "ref": "T2", "coin": "SE", "ox": 4.5, "oy": 4.5},
      {"id": "t3", "ref": "T3", "coin": "NE", "ox": 4.5, "oy": 4.5},
      {"id": "t4", "ref": "T4", "coin": "NW", "ox": 4.5, "oy": 4.5}
    ],
    "composants": [
      {"id": "c1", "ref": "J1", "type": "usbc", "bord": "W", "le_long": 0, "ecart": 0.4},
      {"id": "c2", "ref": "U1", "type": "module", "x": 16, "y": 0, "rot": 90},
      {"id": "c3", "ref": "U2", "type": "ic", "valeur": "AMS1117-3.3", "x": -16.5, "y": 0, "rot": 0},
      {"id": "c4", "ref": "U3", "type": "capteur", "x": -3, "y": -5, "rot": 0},
      {"id": "c5", "ref": "C1", "type": "condo", "x": -3, "y": 6, "rot": 0},
      {"id": "c6", "ref": "D1", "type": "led", "valeur": "Verte", "x": -14, "y": 13, "rot": 0},
      {"id": "c7", "ref": "D2", "type": "led", "valeur": "Rouge", "x": -8.5, "y": 13, "rot": 0},
      {"id": "c8", "ref": "SW1", "type": "bouton", "x": -14, "y": -12, "rot": 0},
      {"id": "c9", "ref": "J2", "type": "jst", "valeur": "Batterie, 2 broches", "x": 2, "y": -15, "rot": 0}
    ]})"));
}

Projet projetVide() {
  return normaliser(Json::parse(R"({
    "nom": "Nouveau projet",
    "carte": {"x0": 0, "y0": 0, "L": 50, "W": 30, "r": 1.5, "t": 1.6},
    "boitier": {"jeu": 1, "paroi": 2, "fond": 2, "entretoise": 5, "hauteur": 12, "couvercle": 2, "vis": "M3", "hauteurAuto": true},
    "trous": [
      {"ref": "T1", "coin": "SW", "ox": 4, "oy": 4}, {"ref": "T2", "coin": "SE", "ox": 4, "oy": 4},
      {"ref": "T3", "coin": "NE", "ox": 4, "oy": 4}, {"ref": "T4", "coin": "NW", "ox": 4, "oy": 4}
    ],
    "composants": []})"));
}

Point posTrou(const Projet& p, const Trou& t) {
  const auto& c = p.carte;
  if (t.coin) {
    const double sx = t.coin->find('E') != std::string::npos ? 1 : -1;
    const double sy = t.coin->find('N') != std::string::npos ? 1 : -1;
    return {c.x0 + sx * (c.L / 2 - t.ox), c.y0 + sy * (c.W / 2 - t.oy)};
  }
  return {t.x, t.y};
}

void ancrerTrou(const Projet& p, Trou& t, double x, double y) {
  const auto& c = p.carte;
  if (t.ancrage == "libre") {
    t.coin.reset(); t.ox = 4; t.oy = 4; t.x = x; t.y = y;
    return;
  }
  if (t.ancrage.size() == 2) {
    const double sx = t.ancrage[1] == 'E' ? 1 : -1, sy = t.ancrage[0] == 'N' ? 1 : -1;
    t.coin = t.ancrage;
    t.ox = c.L / 2 - sx * (x - c.x0); t.oy = c.W / 2 - sy * (y - c.y0); t.x = 0; t.y = 0;
    return;
  }
  const double sx = x >= c.x0 ? 1 : -1, sy = y >= c.y0 ? 1 : -1;
  const double ox = c.L / 2 - sx * (x - c.x0), oy = c.W / 2 - sy * (y - c.y0);
  if (ox <= 15 && oy <= 15) {
    t.coin = std::string(1, sy > 0 ? 'N' : 'S') + (sx > 0 ? 'E' : 'W');
    t.ox = ox; t.oy = oy; t.x = 0; t.y = 0;
  } else {
    t.coin.reset(); t.ox = 4; t.oy = 4; t.x = x; t.y = y;
  }
}

void placerSurBord(const Projet& p, Composant& k, double x, double y) {
  const auto& c = p.carte;
  const double dE = std::abs(c.x0 + c.L / 2 - x), dW = std::abs(x - (c.x0 - c.L / 2));
  const double dN = std::abs(c.y0 + c.W / 2 - y), dS = std::abs(y - (c.y0 - c.W / 2));
  Bord b = Bord::E; double m = dE;
  if (dW < m) { b = Bord::W; m = dW; }
  if (dN < m) { b = Bord::N; m = dN; }
  if (dS < m) { b = Bord::S; m = dS; }
  k.bord = b;
  const bool hz = horizontal(b);
  const double centre = hz ? c.x0 : c.y0, demi = (hz ? c.L : c.W) / 2;
  const double lim = std::max(0.0, demi - k.w / 2 - c.r);
  k.le_long = borne(hz ? x : y, centre - lim, centre + lim);
}

void changerAncrageTrou(const Projet& p, Trou& t, const std::string& ancrage) {
  const Point q = posTrou(p, t);
  t.ancrage = ancrage;
  ancrerTrou(p, t, q.x, q.y);
}

bool ancrageValide(const Composant& k, const std::string& a) {
  if (k.bord) return a == "libre" || a == "debut" || a == "fin" || a == "milieu";
  return a == "libre" || a == "centre" || a == "NE" || a == "NW" || a == "SE" || a == "SW";
}

Point posComposant(const Projet& p, const Composant& k) {
  const auto& c = p.carte;
  if (k.ancrage == "centre") return {c.x0 + k.ax, c.y0 + k.ay};
  if (k.ancrage.size() == 2) {
    const double sx = k.ancrage[1] == 'E' ? 1 : -1, sy = k.ancrage[0] == 'N' ? 1 : -1;
    return {c.x0 + sx * (c.L / 2 - k.ax), c.y0 + sy * (c.W / 2 - k.ay)};
  }
  return {k.x, k.y};
}

namespace {
void bornesBord(const Projet& p, const Composant& k, double& centre, double& demi) {
  const bool hz = k.bord && horizontal(*k.bord);
  centre = hz ? p.carte.x0 : p.carte.y0;
  demi = (hz ? p.carte.L : p.carte.W) / 2;
}
void fixerLeLong(const Projet& p, Composant& k, double u) {
  double centre, demi;
  bornesBord(p, k, centre, demi);
  if (k.ancrage == "debut") k.ax = u - (centre - demi);
  else if (k.ancrage == "fin") k.ax = (centre + demi) - u;
  else if (k.ancrage == "milieu") k.ax = u - centre;
  k.le_long = u;
}
}  // namespace

double leLongComposant(const Projet& p, const Composant& k) {
  if (!k.bord) return 0;
  double centre, demi;
  bornesBord(p, k, centre, demi);
  if (k.ancrage == "debut") return centre - demi + k.ax;
  if (k.ancrage == "fin") return centre + demi - k.ax;
  if (k.ancrage == "milieu") return centre + k.ax;
  return k.le_long;
}

void placerLeLong(const Projet& p, Composant& k, double u) { fixerLeLong(p, k, u); }

void placerComposant(const Projet& p, Composant& k, double x, double y) {
  if (k.bord) {
    placerSurBord(p, k, x, y);
    fixerLeLong(p, k, k.le_long);
    return;
  }
  const auto& c = p.carte;
  if (k.ancrage == "centre") {
    k.ax = x - c.x0; k.ay = y - c.y0;
  } else if (k.ancrage.size() == 2) {
    const double sx = k.ancrage[1] == 'E' ? 1 : -1, sy = k.ancrage[0] == 'N' ? 1 : -1;
    k.ax = c.L / 2 - sx * (x - c.x0); k.ay = c.W / 2 - sy * (y - c.y0);
  }
  k.x = x; k.y = y;
}

void changerAncrage(const Projet& p, Composant& k, const std::string& ancrage) {
  if (!ancrageValide(k, ancrage)) return;
  if (k.bord) {
    const double u = leLongComposant(p, k);
    k.ancrage = ancrage;
    fixerLeLong(p, k, u);
  } else {
    const Point q = posComposant(p, k);
    k.ancrage = ancrage;
    placerComposant(p, k, q.x, q.y);
  }
}

void contraindreBords(Projet& p) {
  for (auto& k : p.composants) {
    if (!k.bord) continue;
    double centre, demi;
    bornesBord(p, k, centre, demi);
    const double lim = std::max(0.0, demi - k.w / 2 - p.carte.r);
    fixerLeLong(p, k, borne(leLongComposant(p, k), centre - lim, centre + lim));
  }
}

int longueurVis(double t, const Vis& v) {
  for (int L : LONGUEURS_VIS) if (L - t >= 1.5 * v.d - 0.2) return L;
  return LONGUEURS_VIS.back();
}

}  // namespace prom
