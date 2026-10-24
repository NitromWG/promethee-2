// Prométhée : import d'un projet QElectroTech (.qet, XML). Chaque folio devient un folio, chaque élément un
// symbole (sa définition embarquée est convertie : traits, rectangles, ellipses, arcs, polygones, textes, bornes),
// les contacts esclaves rejoignent l'appareil de leur bobine maîtresse avec des numéros de bornes normalisés,
// les bornes de bornier deviennent des bornes repérées (-X1:3), les renvois de folio des potentiels, les
// conducteurs des fils (avec leur numéro, leur section et leur couleur). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "electrique_interne.hpp"
#include "xml_interne.hpp"
#include "promethee/electrique.hpp"

namespace prom::elec {

using ecad::Broche;
using ecad::DefSymbole;
using ecad::Pt;
using ecad::Trace;

namespace {

using xml_interne::entites;
using xml_interne::lireXml;
using xml_interne::NoeudXml;
using xml_interne::utf8;

double reel(const std::string& s, double defaut = 0) {
  if (s.empty()) return defaut;
  try {
    std::size_t n = 0;
    const double v = std::stod(s, &n);
    return std::isfinite(v) ? v : defaut;
  } catch (...) {
    return defaut;
  }
}

// ---------- Conversion des définitions ----------
constexpr double ECHELLE = 0.254;  // mm par unité QElectroTech : la grille de 10 tombe sur notre pas de 2,54 mm
Pt pq(double x, double y) { return ecad::ptMm(x * ECHELLE, y * ECHELLE); }

struct BorneQet {
  double x = 0, y = 0;
  int orientation = 0;  // 0 nord, 1 est, 2 sud, 3 ouest
  std::string uuid, nom;
};
struct DefQet {
  DefSymbole d;
  std::vector<BorneQet> bornes;
  std::string lien;               // master, slave, simple, terminal, next_report, previous_report
  std::string etat, genre;        // esclave : NO, NC, SW ; simple, power, delay…
  bool tempo = false, terre = false;
};

int orientationDepuis(const std::string& o) {
  if (o == "n" || o == "0") return 0;
  if (o == "e" || o == "1") return 1;
  if (o == "s" || o == "2") return 2;
  if (o == "w" || o == "3") return 3;
  return 0;
}

std::string nomEnFrancais(const NoeudXml& def) {
  std::string fr, en, premier;
  if (const NoeudXml* names = def.enfant("names"))
    for (const NoeudXml* n : names->tous("name")) {
      const std::string l = n->attr("lang");
      if (l == "fr") fr = n->texte;
      else if (l == "en") en = n->texte;
      if (premier.empty()) premier = n->texte;
    }
  return !fr.empty() ? fr : !en.empty() ? en : premier;
}

bool rempli(const NoeudXml& n) {
  const std::string st = n.attr("style");
  return st.find("filling:black") != std::string::npos || st.find("filling:#000") != std::string::npos;
}

DefQet convertirDefinition(const NoeudXml& def, const std::string& id) {
  DefQet q;
  DefSymbole& d = q.d;
  d.id = id;
  d.nom = nomEnFrancais(def);
  if (d.nom.empty()) d.nom = id.substr(id.find_last_of('/') + 1);
  d.categorie = "Importé (QElectroTech)";
  d.prefixe = "";
  d.description = d.nom;
  q.lien = def.attr("link_type", "simple");
  if (const NoeudXml* k = def.enfant("kindInformations"))
    for (const NoeudXml* x : k->tous("kindInformation")) {
      const std::string n = x->attr("name");
      if (n == "state") q.etat = x->texte;
      else if (n == "type") q.genre = x->texte;
      else if (n == "tempoType" && !x->texte.empty()) q.tempo = true;
    }
  if (q.genre.find("delay") != std::string::npos) q.tempo = true;
  std::string nomMin = d.nom;
  for (char& c : nomMin) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const NoeudXml* desc = def.enfant("description");
  if (desc)
    for (const auto& e : desc->enfants) {
      if (e.nom == "line") {
        Trace t;
        t.forme = Trace::Forme::Polyligne;
        t.points = {pq(reel(e.attr("x1")), reel(e.attr("y1"))), pq(reel(e.attr("x2")), reel(e.attr("y2")))};
        d.traces.push_back(t);
      } else if (e.nom == "rect") {
        const double x = reel(e.attr("x")), y = reel(e.attr("y")), w = reel(e.attr("width")), h = reel(e.attr("height"));
        Trace t;
        t.forme = Trace::Forme::Rectangle;
        t.points = {pq(x, y), pq(x + w, y + h)};
        t.rempli = rempli(e);
        d.traces.push_back(t);
      } else if (e.nom == "ellipse" || e.nom == "circle") {
        double x = reel(e.attr("x")), y = reel(e.attr("y")), w = reel(e.attr("width")), h = reel(e.attr("height"));
        if (e.nom == "circle") w = h = reel(e.attr("diameter"));
        Trace t;
        if (std::abs(w - h) < 0.01) {
          t.forme = Trace::Forme::Cercle;
          t.points = {pq(x + w / 2, y + h / 2)};
          t.rayon = ecad::nm(w / 2 * ECHELLE);
        } else {
          t.forme = Trace::Forme::Polyligne;
          for (int k = 0; k < 36; ++k) {
            const double a = 2 * 3.14159265358979 * k / 36;
            t.points.push_back(pq(x + w / 2 + w / 2 * std::cos(a), y + h / 2 + h / 2 * std::sin(a)));
          }
          t.ferme = true;
        }
        t.rempli = rempli(e);
        d.traces.push_back(t);
      } else if (e.nom == "arc") {
        const double x = reel(e.attr("x")), y = reel(e.attr("y")), w = reel(e.attr("width")), h = reel(e.attr("height"));
        const double debut = reel(e.attr("start")), angle = reel(e.attr("angle"));
        Trace t;
        if (std::abs(w - h) < 0.01) {
          t.forme = Trace::Forme::Arc;
          t.points = {pq(x + w / 2, y + h / 2)};
          t.rayon = ecad::nm(w / 2 * ECHELLE);
          t.debut = angle >= 0 ? debut : debut + angle;
          t.fin = angle >= 0 ? debut + angle : debut;
        } else {
          t.forme = Trace::Forme::Polyligne;
          const int n = std::max(4, static_cast<int>(std::abs(angle) / 10));
          for (int k = 0; k <= n; ++k) {
            const double a = (debut + angle * k / n) * 3.14159265358979 / 180;
            t.points.push_back(pq(x + w / 2 + w / 2 * std::cos(a), y + h / 2 - h / 2 * std::sin(a)));
          }
        }
        d.traces.push_back(t);
      } else if (e.nom == "polygon") {
        Trace t;
        t.forme = Trace::Forme::Polyligne;
        for (int k = 1; k < 400; ++k) {
          const std::string xs = e.attr(("x" + std::to_string(k)).c_str()), ys = e.attr(("y" + std::to_string(k)).c_str());
          if (xs.empty() || ys.empty()) break;
          t.points.push_back(pq(reel(xs), reel(ys)));
        }
        t.ferme = e.attr("closed", "true") != "false";
        t.rempli = rempli(e) && t.ferme;
        if (t.points.size() >= 2) d.traces.push_back(t);
      } else if (e.nom == "text") {
        std::string txt = e.attr("text");
        std::replace(txt.begin(), txt.end(), '\n', ' ');
        if (txt.empty()) continue;
        Trace t;
        t.forme = Trace::Forme::Texte;
        t.points = {pq(reel(e.attr("x")), reel(e.attr("y")))};
        t.texte = txt;
        t.taille = ecad::nm(std::clamp(reel(e.attr("size"), 9) * ECHELLE * 1.1, 0.8, 6.0));
        d.traces.push_back(t);
      } else if (e.nom == "terminal") {
        BorneQet b;
        b.x = reel(e.attr("x"));
        b.y = reel(e.attr("y"));
        b.orientation = orientationDepuis(e.attr("orientation"));
        b.uuid = e.attr("uuid");
        b.nom = e.attr("name");
        q.bornes.push_back(b);
      }
    }
  q.terre = q.bornes.size() == 1 && (nomMin.find("terre") != std::string::npos || nomMin.find("masse") != std::string::npos || nomMin.find("ground") != std::string::npos ||
                                     nomMin.find("earth") != std::string::npos);
  // Numéros des bornes : bobine maîtresse A1-A2 ; sinon le nom donné dans la définition, ou le rang
  std::set<std::string> noms;
  bool nomsUtiles = true;
  for (const auto& b : q.bornes) nomsUtiles = nomsUtiles && !b.nom.empty() && b.nom != "_" && noms.insert(b.nom).second;
  for (std::size_t i = 0; i < q.bornes.size(); ++i) {
    const BorneQet& b = q.bornes[i];
    std::string numero = nomsUtiles ? b.nom : std::to_string(i + 1);
    if (q.lien == "master" && q.bornes.size() == 2) {
      const BorneQet& autre = q.bornes[1 - i];
      const bool premier = b.y < autre.y - 0.01 || (std::abs(b.y - autre.y) < 0.01 && b.x < autre.x);
      numero = premier ? "A1" : "A2";
    }
    Broche br;
    br.numero = numero;
    br.nom = "~";
    br.type = ecad::TypeBroche::Passive;
    br.extremite = pq(b.x, b.y);
    br.direction = b.orientation == 0 ? 1 : b.orientation == 1 ? 2 : b.orientation == 2 ? 3 : 0;  // vers le corps
    br.longueur = 0;
    br.nomVisible = false;
    br.numeroVisible = q.lien == "master" || (q.lien == "slave" && q.genre != "power");
    d.broches.push_back(br);
  }
  const double w = reel(def.attr("width"), 20), hx = reel(def.attr("hotspot_x"));
  d.posReference = pq(w - hx + 2, -2);
  d.posValeur = pq(w - hx + 2, 3);
  return q;
}

// Les contacts d'un esclave, groupés par colonne (ou par rangée) : chaque groupe est un contact
std::vector<std::vector<std::size_t>> contactsDe(const DefQet& q, std::size_t taille) {
  if (taille == 3 && q.bornes.size() == 3) return {{0, 1, 2}};
  for (int axe = 0; axe < 2; ++axe) {
    std::map<long long, std::vector<std::size_t>> groupes;
    for (std::size_t i = 0; i < q.bornes.size(); ++i) groupes[std::llround((axe == 0 ? q.bornes[i].x : q.bornes[i].y) * 10)].push_back(i);
    bool ok = !groupes.empty();
    for (const auto& [k, g] : groupes) ok = ok && g.size() == taille;
    if (!ok) continue;
    std::vector<std::vector<std::size_t>> r;
    for (auto& [k, g] : groupes) {
      std::sort(g.begin(), g.end(), [&](std::size_t a, std::size_t b) { return axe == 0 ? q.bornes[a].y < q.bornes[b].y : q.bornes[a].x < q.bornes[b].x; });
      r.push_back(g);
    }
    return r;
  }
  return {};
}

std::string sansTiret(std::string s) {
  while (!s.empty() && (s.front() == '-' || s.front() == ' ')) s.erase(s.begin());
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s;
}

std::string couleurDepuis(std::string c) {
  for (char& x : c) x = static_cast<char>(std::tolower(static_cast<unsigned char>(x)));
  static const std::vector<std::pair<std::string, std::string>> t = {
      {"vert-jaune", "GNYE"}, {"vert/jaune", "GNYE"}, {"green-yellow", "GNYE"}, {"gnye", "GNYE"}, {"bleu clair", "LBU"}, {"bleu foncé", "DBU"}, {"noir", "BK"},
      {"black", "BK"},        {"marron", "BN"},       {"brun", "BN"},          {"brown", "BN"},    {"gris", "GY"},       {"grey", "GY"},       {"gray", "GY"},
      {"bleu", "BU"},         {"blue", "BU"},         {"rouge", "RD"},         {"red", "RD"},      {"blanc", "WH"},      {"white", "WH"},      {"orange", "OG"},
      {"violet", "VT"},       {"jaune", "YE"},        {"yellow", "YE"},        {"vert", "GN"},     {"green", "GN"},      {"rose", "PK"}};
  for (const auto& [k, v] : t)
    if (c.find(k) != std::string::npos) return v;
  return {};
}

struct InstanceQet {
  const NoeudXml* noeud = nullptr;
  std::size_t diagramme = 0;
  std::string uuid, type, label;
  const DefQet* def = nullptr;
  double x = 0, y = 0;
  int orientation = 0;
  std::vector<std::string> liens;
  std::map<std::string, std::string> infos;
};

}  // namespace

ProjetElectrique importerQet(const std::string& contenu, std::vector<std::string>* alertes) {
  NoeudXml racine;
  try {
    racine = lireXml(contenu);
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Fichier QElectroTech illisible : ") + e.what());
  }
  if (racine.nom != "project") throw std::runtime_error("Ce fichier n'est pas un projet QElectroTech (.qet) : sa racine est « " + racine.nom + " ».");
  const std::string titre = racine.attr("title");
  ProjetElectrique p = nouveauProjetElectrique(titre.empty() ? std::string("Projet importé") : titre, false);
  p.schema.feuilles.clear();
  p.folios.clear();
  p.schema.cartouche.titre = p.nom;
  auto alerte = [&](const std::string& t) {
    if (alertes) alertes->push_back(t);
  };

  // Définitions embarquées : « embed://import/catégorie/…/element.elmt »
  std::map<std::string, const NoeudXml*> definitions;
  std::vector<std::pair<const NoeudXml*, std::string>> pile;
  if (const NoeudXml* coll = racine.enfant("collection")) pile.push_back({coll, ""});
  while (!pile.empty()) {
    const auto [n, chemin] = pile.back();
    pile.pop_back();
    for (const auto& e : n->enfants) {
      if (e.nom == "category") pile.push_back({&e, chemin + e.attr("name") + "/"});
      else if (e.nom == "element")
        if (const NoeudXml* d = e.enfant("definition")) definitions[chemin + e.attr("name")] = d;
    }
  }
  std::map<std::string, DefQet> converties;
  auto definitionDe = [&](const std::string& type) -> const DefQet* {
    // « embed://import/10_electric/…/x.elmt » : la collection embarquée a pour racine la catégorie « import »
    std::string chemin = type;
    if (chemin.rfind("embed://", 0) == 0) chemin = chemin.substr(8);
    if (const auto it = converties.find(chemin); it != converties.end()) return &it->second;
    auto it = definitions.find(chemin);
    if (it == definitions.end() && chemin.rfind("import/", 0) == 0) it = definitions.find(chemin.substr(7));
    if (it == definitions.end()) return nullptr;
    return &(converties[chemin] = convertirDefinition(*it->second, "qet:" + chemin));
  };

  // Folios dans l'ordre
  std::vector<const NoeudXml*> diagrammes = racine.tous("diagram");
  std::stable_sort(diagrammes.begin(), diagrammes.end(), [](const NoeudXml* a, const NoeudXml* b) { return reel(a->attr("order"), 0) < reel(b->attr("order"), 0); });
  if (diagrammes.empty()) throw std::runtime_error("Ce projet QElectroTech n'a aucun folio.");

  // 1. Instances
  std::vector<InstanceQet> instances;
  std::map<std::string, std::size_t> parUuid;
  int sansDefinition = 0, graphiques = 0;
  for (std::size_t di = 0; di < diagrammes.size(); ++di) {
    const NoeudXml* elements = diagrammes[di]->enfant("elements");
    if (!elements) continue;
    for (const NoeudXml* e : elements->tous("element")) {
      InstanceQet x;
      x.noeud = e;
      x.diagramme = di;
      x.uuid = e->attr("uuid");
      x.type = e->attr("type");
      x.x = reel(e->attr("x"));
      x.y = reel(e->attr("y"));
      x.orientation = ((static_cast<int>(reel(e->attr("orientation"))) % 4) + 4) % 4;
      x.def = definitionDe(x.type);
      if (!x.def) {
        ++sansDefinition;
        continue;
      }
      if (x.def->bornes.empty()) {
        ++graphiques;
        continue;
      }
      if (const NoeudXml* li = e->enfant("links_uuids"))
        for (const NoeudXml* l : li->tous("link_uuid")) x.liens.push_back(l->attr("uuid"));
      if (const NoeudXml* inf = e->enfant("elementInformations"))
        for (const NoeudXml* i : inf->tous("elementInformation")) x.infos[i->attr("name")] = i->texte;
      x.label = sansTiret(x.infos.count("label") ? x.infos["label"] : std::string());
      if (x.label.empty())
        if (const NoeudXml* inputs = e->enfant("inputs"))
          for (const NoeudXml* in : inputs->tous("input"))
            if (in->attr("tagg") == "label") x.label = sansTiret(in->attr("text"));
      parUuid[x.uuid] = instances.size();
      instances.push_back(std::move(x));
    }
  }

  // 2. Appareils : maîtres et simples ; les esclaves rejoignent leur maître ; bornes dans leur bornier
  std::map<std::size_t, std::string> composantDe;  // instance → UUID d'appareil
  std::map<std::string, std::string> borniers;     // repère → UUID
  std::map<std::size_t, std::string> numeroBorne;  // instance de borne → numéro
  auto nouveauComposant = [&](const InstanceQet& x, const std::string& reference, const std::string& symbole) {
    ecad::ComposantElec k;
    k.uuid = ecad::nouvelUuid();
    k.symbole = symbole;
    k.reference = reference;
    k.description = x.infos.count("designation") && !x.infos.at("designation").empty() ? x.infos.at("designation")
                    : x.infos.count("description") && !x.infos.at("description").empty() ? x.infos.at("description")
                                                                                            : x.def->d.nom;
    k.valeur = k.description;
    k.horsCarte = true;
    if (x.infos.count("manufacturer")) k.fabricant = x.infos.at("manufacturer");
    if (x.infos.count("manufacturer_reference")) k.mpn = x.infos.at("manufacturer_reference");
    if (x.infos.count("function") && !x.infos.at("function").empty()) k.champs["fonction"] = x.infos.at("function");
    if (x.infos.count("location") && !x.infos.at("location").empty()) k.champs["emplacement"] = x.infos.at("location");
    if (x.infos.count("comment") && !x.infos.at("comment").empty()) k.champs["commentaire"] = x.infos.at("comment");
    p.schema.composants.push_back(k);
    return k.uuid;
  };
  auto referenceOuAttente = [&](const InstanceQet& x) {
    if (!x.label.empty()) return x.label;
    const std::string prefixe = x.noeud->attr("prefix");
    return (prefixe.empty() ? std::string("A") : prefixe) + "?";
  };
  int bornesSansBornier = 0;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    const InstanceQet& x = instances[i];
    const std::string& lien = x.def->lien;
    if (lien == "next_report" || lien == "previous_report" || x.def->terre) continue;
    if (lien == "slave") continue;
    if (lien == "terminal") {
      std::string bornier = x.label, numero;
      if (const auto sep = bornier.find_last_of(":."); sep != std::string::npos && sep + 1 < bornier.size()) {
        numero = bornier.substr(sep + 1);
        bornier = bornier.substr(0, sep);
      }
      if (bornier.empty()) {
        bornier = "X?";
        ++bornesSansBornier;
      }
      auto it = borniers.find(bornier);
      if (it == borniers.end()) {
        InstanceQet b = x;
        b.infos.clear();
        const std::string id = nouveauComposant(b, bornier, "iec:borne");
        if (ecad::ComposantElec* k = p.schema.composant(id)) {
          k->description = "Bornier";
          k->valeur = "Bornier";
          k->champs["catalogue"] = "x-2,5";
        }
        it = borniers.emplace(bornier, id).first;
      }
      composantDe[i] = it->second;
      numeroBorne[i] = numero;
      continue;
    }
    composantDe[i] = nouveauComposant(x, referenceOuAttente(x), x.def->d.id);
  }
  int esclavesSeuls = 0;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    const InstanceQet& x = instances[i];
    if (x.def->lien != "slave") continue;
    std::string maitre;
    for (const auto& l : x.liens)
      if (const auto it = parUuid.find(l); it != parUuid.end() && composantDe.count(it->second)) {
        maitre = composantDe[it->second];
        break;
      }
    if (maitre.empty()) {
      ++esclavesSeuls;
      maitre = nouveauComposant(x, referenceOuAttente(x), x.def->d.id);
    }
    composantDe[i] = maitre;
  }
  // Numéros des bornes de bornier sans numéro : à la suite
  {
    std::map<std::string, int> maxParBornier;
    for (const auto& [i, n] : numeroBorne) {
      const double v = detail::lireNombre(n);
      if (!n.empty() && v > 0) maxParBornier[composantDe[i]] = std::max(maxParBornier[composantDe[i]], static_cast<int>(v));
    }
    for (auto& [i, n] : numeroBorne)
      if (n.empty()) n = std::to_string(++maxParBornier[composantDe[i]]);
  }
  if (const ecad::DefSymbole* b = symboleElectrique("iec:borne"); b && !borniers.empty()) p.schema.symboles["iec:borne"] = *b;
  if (const ecad::DefSymbole* t = symboleElectrique("iec:terre")) p.schema.symboles["iec:terre"] = *t;

  // 3. Folios et poses
  std::map<std::size_t, std::size_t> feuilleDe;  // diagramme → feuille
  for (std::size_t di = 0; di < diagrammes.size(); ++di) {
    const NoeudXml* g = diagrammes[di];
    const double cols = reel(g->attr("cols"), 17), colsize = reel(g->attr("colsize"), 60), rows = reel(g->attr("rows"), 8), rowsize = reel(g->attr("rowsize"), 80);
    double largeur = 20 + (cols * colsize + 20) * ECHELLE, hauteur = 20 + (rows * rowsize + 20 + 50) * ECHELLE;
    for (const auto& x : instances)
      if (x.diagramme == di) {
        largeur = std::max(largeur, 30 + x.x * ECHELLE);
        hauteur = std::max(hauteur, 45 + x.y * ECHELLE);
      }
    const std::string format = largeur <= 297 && hauteur <= 210 ? "A4" : largeur <= 420 && hauteur <= 297 ? "A3" : "A2";
    std::string nom = g->attr("title");
    if (nom.empty()) nom = "Folio " + std::to_string(di + 1);
    const std::size_t f = ajouterFolio(p, TypeFolio::Libre, nom);
    p.schema.feuilles[f].format = format;
    feuilleDe[di] = f;
    if (di == 0) {
      p.schema.cartouche.auteur = g->attr("author");
      const std::string date = g->attr("date");
      if (date.size() == 8 && std::all_of(date.begin(), date.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        p.schema.cartouche.date = date.substr(0, 4) + "-" + date.substr(4, 2) + "-" + date.substr(6, 2);
      p.reglages.colonnes = std::clamp(static_cast<int>(cols), 1, 40);
      p.reglages.lignes = std::clamp(static_cast<int>(rows), 1, 26);
    }
  }
  const ecad::Nm marge = 10 * ecad::NM_PAR_MM;
  auto versFeuille = [&](double x, double y) { return Pt{marge + ecad::nm(x * ECHELLE), marge + ecad::nm(y * ECHELLE)}; };
  // Point de connexion d'une borne de la définition, pour une instance
  auto pointBorne = [&](const InstanceQet& x, std::size_t b) {
    const BorneQet& t = x.def->bornes[b];
    const Pt local = ecad::tournerQuarts(pq(t.x, t.y), (4 - x.orientation) % 4);
    return versFeuille(x.x, x.y) + local;
  };
  std::map<std::size_t, std::string> poseDe;  // instance → UUID de la pose (symbole ou étiquette)
  std::map<std::string, int> rangs;            // appareil → dernier rang de contact donné
  std::map<std::string, int> polesDonnes;
  int contactsNumerotes = 0, contactsLibres = 0;
  for (std::size_t i = 0; i < instances.size(); ++i) {
    const InstanceQet& x = instances[i];
    const std::size_t f = feuilleDe[x.diagramme];
    const std::string& lien = x.def->lien;
    if (lien == "next_report" || lien == "previous_report") continue;  // plus bas : potentiels
    ecad::SymbolePlace s;
    s.uuid = ecad::nouvelUuid();
    if (x.def->terre) {
      // Terre : symbole de protection posé sur la borne
      s.symbole = "iec:terre";
      s.nomAlim = "PE";
      s.position = pointBorne(x, 0) + ecad::ptMm(0, 2.54);
      p.schema.feuilles[f].symboles.push_back(s);
      poseDe[i] = s.uuid;
      continue;
    }
    p.schema.symboles[x.def->d.id] = x.def->d;
    s.symbole = x.def->d.id;
    s.composant = composantDe[i];
    s.position = versFeuille(x.x, x.y);
    s.rotation = (4 - x.orientation) % 4;
    if (lien == "terminal") {
      for (const auto& b : x.def->d.broches) s.numeros[b.numero] = numeroBorne[i];
    } else if (lien == "slave") {
      const DefQet& q = *x.def;
      const bool puissance = q.genre == "power";
      const bool inverseur = q.etat == "SW";
      const auto contacts = contactsDe(q, inverseur ? 3 : 2);
      if (contacts.empty()) {
        // Forme inconnue : numéros propres à cette pose, sans collision
        const int n = ++rangs[s.composant] + 100;
        for (const auto& b : q.d.broches) s.numeros[b.numero] = b.numero + "." + std::to_string(n);
        ++contactsLibres;
      } else {
        for (const auto& c : contacts) {
          if (puissance) {
            const int pole = polesDonnes[s.composant]++;
            s.numeros[q.d.broches[c[0]].numero] = std::to_string(2 * pole + 1);
            s.numeros[q.d.broches[c[1]].numero] = std::to_string(2 * pole + 2);
          } else if (inverseur) {
            const int r = ++rangs[s.composant];
            // Commun : la borne seule de son côté ; ouverture puis fermeture de part et d'autre
            std::size_t commun = c[0];
            for (std::size_t a : c) {
              int memeCote = 0;
              for (std::size_t b : c) memeCote += std::abs(q.bornes[a].y - q.bornes[b].y) < 0.01 ? 1 : 0;
              if (memeCote == 1) commun = a;
            }
            std::vector<std::size_t> autres;
            for (std::size_t a : c)
              if (a != commun) autres.push_back(a);
            std::sort(autres.begin(), autres.end(), [&](std::size_t a, std::size_t b) { return q.bornes[a].x < q.bornes[b].x; });
            s.numeros[q.d.broches[commun].numero] = std::to_string(r) + "1";
            if (autres.size() == 2) {
              s.numeros[q.d.broches[autres[0]].numero] = std::to_string(r) + "2";
              s.numeros[q.d.broches[autres[1]].numero] = std::to_string(r) + "4";
            }
          } else {
            const int r = ++rangs[s.composant];
            const bool fermeture = q.etat != "NC";
            const char a = q.tempo ? (fermeture ? '7' : '5') : (fermeture ? '3' : '1');
            s.numeros[q.d.broches[c[0]].numero] = std::to_string(r) + std::string(1, a);
            s.numeros[q.d.broches[c[1]].numero] = std::to_string(r) + std::string(1, static_cast<char>(a + 1));
          }
          ++contactsNumerotes;
        }
      }
    }
    p.schema.feuilles[f].symboles.push_back(s);
    poseDe[i] = s.uuid;
  }

  // 4. Conducteurs
  struct Bout {
    std::size_t instance = 0;
    std::size_t borne = 0;
  };
  std::map<std::size_t, std::string> nomRenvoi;  // instance de renvoi → nom du potentiel
  std::map<std::size_t, std::vector<std::pair<Pt, std::string>>> numerosParFeuille;
  int conducteurs = 0, perdus = 0;
  struct ConducteurLu {
    std::size_t feuille;
    Pt a, b;
    std::vector<std::pair<bool, double>> segments;  // horizontal ?, longueur
    std::string num;
    std::size_t ia, ib;
    bool renvoiA, renvoiB;
  };
  std::vector<ConducteurLu> lus;
  for (std::size_t di = 0; di < diagrammes.size(); ++di) {
    const NoeudXml* g = diagrammes[di];
    // Bornes des instances de ce folio : identifiant numérique (anciens fichiers) ou UUID de la définition
    std::map<std::string, Bout> parId;
    for (std::size_t i = 0; i < instances.size(); ++i) {
      const InstanceQet& x = instances[i];
      if (x.diagramme != di) continue;
      if (const NoeudXml* ts = x.noeud->enfant("terminals")) {
        std::size_t rang = 0;
        for (const NoeudXml* t : ts->tous("terminal")) {
          const std::string id = t->attr("id");
          const double tx = reel(t->attr("x")), ty = reel(t->attr("y"));
          const int o = orientationDepuis(t->attr("orientation"));
          // L'instance garde le point d'attache sur le corps, 4 unités en retrait du point de connexion
          const double dx = o == 1 ? 4 : o == 3 ? -4 : 0, dy = o == 0 ? -4 : o == 2 ? 4 : 0;
          std::size_t trouve = rang < x.def->bornes.size() ? rang : 0;
          for (std::size_t b = 0; b < x.def->bornes.size(); ++b) {
            const BorneQet& d = x.def->bornes[b];
            if ((std::abs(d.x - (tx + dx)) < 0.6 && std::abs(d.y - (ty + dy)) < 0.6) || (std::abs(d.x - tx) < 0.01 && std::abs(d.y - ty) < 0.01)) {
              trouve = b;
              break;
            }
          }
          if (!id.empty()) parId[id] = {i, trouve};
          ++rang;
        }
      }
    }
    const NoeudXml* cs = g->enfant("conductors");
    if (!cs) continue;
    for (const NoeudXml* c : cs->tous("conductor")) {
      auto resoudre = [&](const char* element, const char* terminal, Bout& b) {
        const std::string t = c->attr(terminal);
        const std::string e = c->attr(element);
        if (!e.empty()) {
          const auto it = parUuid.find(e);
          if (it == parUuid.end()) return false;
          const InstanceQet& x = instances[it->second];
          for (std::size_t k = 0; k < x.def->bornes.size(); ++k)
            if (x.def->bornes[k].uuid == t) {
              b = {it->second, k};
              return true;
            }
          return false;
        }
        const auto it = parId.find(t);
        if (it == parId.end()) return false;
        b = it->second;
        return true;
      };
      Bout a, b;
      auto connu = [&](std::size_t i) { return poseDe.count(i) > 0 || instances[i].def->lien.ends_with("report"); };
      if (!resoudre("element1", "terminal1", a) || !resoudre("element2", "terminal2", b) || !connu(a.instance) || !connu(b.instance)) {
        ++perdus;
        continue;
      }
      ConducteurLu l;
      l.feuille = feuilleDe[di];
      l.ia = a.instance;
      l.ib = b.instance;
      l.renvoiA = instances[a.instance].def->lien.ends_with("report");
      l.renvoiB = instances[b.instance].def->lien.ends_with("report");
      l.a = pointBorne(instances[a.instance], a.borne);
      l.b = pointBorne(instances[b.instance], b.borne);
      for (const NoeudXml* sg : c->tous("segment")) l.segments.push_back({sg->attr("orientation") == "horizontal", reel(sg->attr("length"))});
      l.num = c->attr("num");
      if (!l.num.empty()) {
        if (l.renvoiA && !nomRenvoi.count(a.instance)) nomRenvoi[a.instance] = l.num;
        if (l.renvoiB && !nomRenvoi.count(b.instance)) nomRenvoi[b.instance] = l.num;
        const std::string sec = c->attr("conductor_section");
        const std::string col = couleurDepuis(c->attr("conductor_color"));
        if (!sec.empty() || !col.empty()) {
          ProprietesConducteur& pr = p.conducteurs[l.num];
          if (const double v = detail::lireNombre(sec); v > 0) pr.section = v;
          if (!col.empty()) pr.couleur = col;
        }
      }
      lus.push_back(l);
    }
  }
  // Renvois de folio : un potentiel par paire liée, nommé par le numéro de fil qu'elle porte
  {
    int n = 0;
    std::set<std::size_t> faits;
    for (std::size_t i = 0; i < instances.size(); ++i) {
      const InstanceQet& x = instances[i];
      if (!x.def->lien.ends_with("report") || faits.count(i)) continue;
      std::vector<std::size_t> groupe{i};
      for (const auto& l : x.liens)
        if (const auto it = parUuid.find(l); it != parUuid.end()) groupe.push_back(it->second);
      std::string nom;
      for (std::size_t g : groupe)
        if (const auto it = nomRenvoi.find(g); it != nomRenvoi.end() && nom.empty()) nom = it->second;
      if (nom.empty()) {
        const std::string label = x.label;
        nom = !label.empty() ? label : "Renvoi " + std::to_string(++n);
      }
      for (std::size_t g : groupe) {
        if (faits.count(g)) continue;
        faits.insert(g);
        const InstanceQet& y = instances[g];
        if (y.def->bornes.empty()) continue;
        ecad::Etiquette e;
        e.uuid = ecad::nouvelUuid();
        e.texte = nom;
        e.p = pointBorne(y, 0);
        const int monde = (y.def->bornes[0].orientation + y.orientation) % 4;
        e.rotation = monde == 3 ? 0 : monde == 1 ? 2 : monde == 0 ? 3 : 1;
        e.type = ecad::TypeEtiquette::Globale;
        p.schema.feuilles[feuilleDe[y.diagramme]].etiquettes.push_back(e);
        poseDe[g] = e.uuid;
      }
    }
  }
  for (const auto& l : lus) {
    // Tracé : segments de QElectroTech à partir de la première borne, puis raccord exact sur la seconde
    std::vector<Pt> pts{l.a};
    for (const auto& [horizontal, longueur] : l.segments) {
      const ecad::Nm d = ecad::nm(longueur * ECHELLE);
      pts.push_back(pts.back() + (horizontal ? Pt{d, 0} : Pt{0, d}));
    }
    if (pts.size() >= 3) {
      const bool horizontal = l.segments.back().first;
      Pt& avant = pts[pts.size() - 2];
      if (horizontal) avant.y = l.b.y;
      else avant.x = l.b.x;
      pts.back() = l.b;
    } else if (pts.size() == 2) {
      pts.back() = l.b;
      if (pts[0].x != pts[1].x && pts[0].y != pts[1].y) pts.insert(pts.begin() + 1, Pt{l.b.x, l.a.y});
    } else {
      if (l.a.x != l.b.x && l.a.y != l.b.y) pts.push_back(Pt{l.b.x, l.a.y});
      pts.push_back(l.b);
    }
    for (std::size_t k = 1; k < pts.size(); ++k) {
      if (pts[k - 1] == pts[k]) continue;
      ecad::Fil w;
      w.uuid = ecad::nouvelUuid();
      w.a = pts[k - 1];
      w.b = pts[k];
      p.schema.feuilles[l.feuille].fils.push_back(w);
    }
    ++conducteurs;
    if (!l.num.empty()) numerosParFeuille[l.feuille].push_back({l.a, l.num});
  }
  // Numéros de fils : figés sur les potentiels sans nom
  {
    const ecad::Connectivite k = ecad::connectivite(p.schema);
    for (const auto& [f, liste] : numerosParFeuille)
      for (const auto& [pt, num] : liste) {
        int reseau = -1;
        for (const auto& w : p.schema.feuilles[f].fils)
          if ((w.a == pt || w.b == pt))
            if (const auto it = k.parFil.find(w.uuid); it != k.parFil.end()) {
              reseau = it->second;
              break;
            }
        if (reseau < 0) continue;
        const ecad::Reseau& r = k.reseaux[static_cast<std::size_t>(reseau)];
        if (r.nomme) continue;
        for (const auto& b : r.broches) p.schema.nomsReseaux.emplace(b.composant + ":" + b.broche, num);
      }
  }
  // Appareils sans repère dans QElectroTech : numérotés à la suite de ceux qui en ont un
  const int annotes = annoterElectrique(p, false);
  // Compte rendu
  int appareils = 0;
  for (const auto& c : p.schema.composants) appareils += detail::estBornier(c) ? 0 : 1;
  alerte("Importé : " + std::to_string(diagrammes.size()) + " folio(s), " + std::to_string(appareils) + " appareil(s), " + std::to_string(borniers.size()) + " bornier(s), " +
         std::to_string(conducteurs) + " conducteur(s).");
  if (contactsNumerotes > 0) alerte(std::to_string(contactsNumerotes) + " contact(s) rattachés à leur bobine et numérotés selon la CEI 60947 (13-14, 21-22, 1-2…).");
  if (graphiques > 0) alerte(std::to_string(graphiques) + " élément(s) sans borne (cadres, cartouches, logos) non repris : le cadre et le cartouche sont ceux de Prométhée.");
  if (sansDefinition > 0) alerte(std::to_string(sansDefinition) + " élément(s) sans définition embarquée dans le fichier, ignorés.");
  if (perdus > 0) alerte(std::to_string(perdus) + " conducteur(s) dont une extrémité est introuvable, ignorés.");
  if (esclavesSeuls > 0) alerte(std::to_string(esclavesSeuls) + " contact(s) sans bobine maîtresse liée : devenus des appareils à part, à rattacher.");
  if (contactsLibres > 0) alerte(std::to_string(contactsLibres) + " contact(s) de forme inconnue : leurs bornes ont reçu des numéros provisoires.");
  if (annotes > 0) alerte(std::to_string(annotes) + " appareil(s) sans repère dans QElectroTech ont été repérés à la suite (lettre et numéro).");
  if (bornesSansBornier > 0) alerte(std::to_string(bornesSansBornier) + " borne(s) sans repère de bornier, rangées dans « X? ».");
  return p;
}

}  // namespace prom::elec
