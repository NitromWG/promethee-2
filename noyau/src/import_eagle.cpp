// Prométhée : import EAGLE (10.5, 10.18). Voir import_eagle.hpp. Licence GPL-3.0-only.
#include "promethee/import_eagle.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

#include "promethee/circuit_edition.hpp"
#include "kicad_interne.hpp"
#include "xml_interne.hpp"

namespace prom::ecad {

namespace {
using xml_interne::lireXml;
using xml_interne::NoeudXml;

double reel(const std::string& s, double defaut = 0) {
  if (s.empty()) return defaut;
  char* fin = nullptr;
  const double v = std::strtod(s.c_str(), &fin);
  return fin == s.c_str() ? defaut : v;
}
// EAGLE : millimètres, Y vers le haut ; Prométhée : nanomètres, Y vers le bas
Pt pe(const NoeudXml& n, const char* x = "x", const char* y = "y") { return {nm(reel(n.attr(x))), -nm(reel(n.attr(y)))}; }
struct Rotation {
  double degres = 0;
  bool miroir = false;
};
Rotation rotation(const std::string& r) {
  Rotation o;
  std::size_t i = 0;
  while (i < r.size() && (r[i] == 'M' || r[i] == 'S')) {
    if (r[i] == 'M') o.miroir = true;
    ++i;
  }
  if (i < r.size() && r[i] == 'R') ++i;
  o.degres = reel(r.substr(i));
  return o;
}
// Milieu d'un arc d'EAGLE tracé de a à b sous l'angle « curve » (degrés, sens direct), en coordonnées d'EAGLE
std::optional<Point> milieuArc(double ax, double ay, double bx, double by, double courbe) {
  if (std::abs(courbe) < 1e-6) return std::nullopt;
  const double th = courbe * std::numbers::pi / 180, mx = (ax + bx) / 2, my = (ay + by) / 2, dx = bx - ax, dy = by - ay, corde = std::hypot(dx, dy);
  if (corde < 1e-9) return std::nullopt;
  const double dc = corde / 2 / std::tan(th / 2);  // distance signée du milieu de la corde au centre
  const double cx = mx - dy / corde * dc, cy = my + dx / corde * dc;
  const double a0 = std::atan2(ay - cy, ax - cx), rr = std::hypot(ax - cx, ay - cy);
  return Point{cx + rr * std::cos(a0 + th / 2), cy + rr * std::sin(a0 + th / 2)};
}
std::vector<const NoeudXml*> enfants(const NoeudXml* n, const char* nom) { return n ? n->tous(nom) : std::vector<const NoeudXml*>{}; }
const NoeudXml* chemin(const NoeudXml& racine, std::initializer_list<const char*> noms) {
  const NoeudXml* n = &racine;
  for (const char* x : noms) {
    n = n->enfant(x);
    if (!n) return nullptr;
  }
  return n;
}
Couche coucheCuivreEagle(int l, int nc) {
  if (l == 1) return Couche::CuivreDessus;
  if (l == 16) return Couche::CuivreDessous;
  const auto cu = couchesCuivre(std::max(nc, 2));
  const std::size_t rang = static_cast<std::size_t>(std::clamp(l - 1, 1, static_cast<int>(cu.size()) - 2));
  return cu[rang];
}

struct Porte {
  std::string nom, symbole;
};
struct Variante {
  std::string nom, paquet;
  std::map<std::pair<std::string, std::string>, std::vector<std::string>> connects;  // (porte, broche) → pastilles
};
struct Ensemble {
  std::string nom, prefixe = "U";
  bool valeurLibre = false;
  std::vector<Porte> portes;
  std::vector<Variante> variantes;
  std::string description;
};
struct Bibliotheque {
  std::string nom;
  std::map<std::string, const NoeudXml*> paquets, symboles;
  std::map<std::string, Ensemble> ensembles;
};
Bibliotheque lireBibliotheque(const NoeudXml& lib) {
  Bibliotheque b;
  b.nom = lib.attr("name");
  for (const auto* p : enfants(lib.enfant("packages"), "package")) b.paquets[p->attr("name")] = p;
  for (const auto* s : enfants(lib.enfant("symbols"), "symbol")) b.symboles[s->attr("name")] = s;
  for (const auto* d : enfants(lib.enfant("devicesets"), "deviceset")) {
    Ensemble e;
    e.nom = d->attr("name");
    e.prefixe = d->attr("prefix", "U");
    e.valeurLibre = d->attr("uservalue") == "yes";
    if (const NoeudXml* desc = d->enfant("description")) e.description = desc->texte;
    for (const auto* g : enfants(d->enfant("gates"), "gate")) e.portes.push_back({g->attr("name"), g->attr("symbol")});
    for (const auto* v : enfants(d->enfant("devices"), "device")) {
      Variante va;
      va.nom = v->attr("name");
      va.paquet = v->attr("package");
      for (const auto* cx : enfants(v->enfant("connects"), "connect")) {
        std::istringstream pads(cx->attr("pad"));
        std::string p;
        while (pads >> p) va.connects[{cx->attr("gate"), cx->attr("pin")}].push_back(p);
      }
      e.variantes.push_back(va);
    }
    if (e.variantes.empty()) e.variantes.push_back({});
    b.ensembles[e.nom] = e;
  }
  return b;
}
std::string idEmpreinte(const std::string& lib, const std::string& paquet) { return "eagle:" + lib + ":" + paquet; }
std::string idSymbole(const std::string& lib, const std::string& ensemble, const std::string& variante) { return "eagle:" + lib + ":" + ensemble + (variante.empty() ? std::string() : ":" + variante); }

// ---------- Empreintes ----------
DefEmpreinte empreinte(const NoeudXml& paquet, const std::string& id) {
  DefEmpreinte d;
  d.id = id;
  d.description = paquet.enfant("description") ? paquet.enfant("description")->texte : "Paquet EAGLE « " + paquet.attr("name") + " »";
  d.famille = "EAGLE";
  Boite enc;
  for (const auto& n : paquet.enfants) {
    if (n.nom == "smd") {
      Pastille p;
      p.numero = n.attr("name");
      p.type = TypePastille::CMS;
      p.position = pe(n);
      p.largeur = nm(reel(n.attr("dx")));
      p.hauteur = nm(reel(n.attr("dy")));
      const double rond = reel(n.attr("roundness"));
      p.forme = rond > 0 ? FormePastille::RectArrondi : FormePastille::Rect;
      p.arrondi = rond / 200;
      p.rotation = rotation(n.attr("rot")).degres;
      if (n.attr("layer") == "16") p.couches = bit(Couche::CuivreDessous) | bit(Couche::VernisDessous) | bit(Couche::PateDessous);
      if (n.attr("cream") == "no") p.couches = (p.couches ? p.couches : bit(Couche::CuivreDessus) | bit(Couche::VernisDessus) | bit(Couche::PateDessus)) & ~(bit(Couche::PateDessus) | bit(Couche::PateDessous));
      d.pastilles.push_back(p);
      enc.etendre(boite(formePastille(p)));
    } else if (n.nom == "pad" || n.nom == "hole") {
      Pastille p;
      p.numero = n.nom == "pad" ? n.attr("name") : std::string();
      p.type = n.nom == "pad" ? TypePastille::Traversante : TypePastille::NonMetallisee;
      p.position = pe(n);
      p.percage = nm(reel(n.attr("drill")));
      Nm dia = nm(reel(n.attr("diameter")));
      if (n.nom == "hole") dia = p.percage;
      else if (dia <= 0) dia = p.percage + 2 * std::max<Nm>(250'000, p.percage / 4);  // couronne par défaut d'EAGLE
      p.largeur = p.hauteur = dia;
      const std::string forme = n.attr("shape", "round");
      p.forme = n.nom == "hole" || forme == "round" ? FormePastille::Rond : forme == "square" ? FormePastille::Rect : forme == "octagon" ? FormePastille::Chanfreine : FormePastille::Ovale;
      if (forme == "octagon") p.chanfrein = 0.29;
      if (forme == "long" || forme == "offset") p.largeur = 2 * dia;
      p.rotation = rotation(n.attr("rot")).degres;
      d.pastilles.push_back(p);
      enc.etendre(boite(formePastille(p)));
    } else if (n.nom == "wire" || n.nom == "circle" || n.nom == "rectangle") {
      const std::string l = n.attr("layer");
      const Couche k = l == "21" ? Couche::SerigraphieDessus : l == "22" ? Couche::SerigraphieDessous : l == "51" ? Couche::FabricationDessus : l == "52" ? Couche::FabricationDessous
                       : l == "35" ? Couche::ColleDessus : l == "36" ? Couche::ColleDessous : Couche::Commentaires;
      if (k == Couche::Commentaires) continue;  // calques d'interdiction et de documentation non repris dans l'empreinte
      const Nm ep = std::max<Nm>(nm(reel(n.attr("width"), 0.127)), 50'000);
      auto trait = [&](Pt a, Pt b) {
        TraitEmpreinte t;
        t.couche = k;
        t.a = a;
        t.b = b;
        t.epaisseur = ep;
        d.traits.push_back(t);
        enc.etendre(a);
        enc.etendre(b);
      };
      if (n.nom == "wire") trait(pe(n, "x1", "y1"), pe(n, "x2", "y2"));
      else if (n.nom == "rectangle") {
        const Pt a = pe(n, "x1", "y1"), b = pe(n, "x2", "y2");
        trait(a, {b.x, a.y});
        trait({b.x, a.y}, b);
        trait(b, {a.x, b.y});
        trait({a.x, b.y}, a);
      } else {
        TraitEmpreinte t;
        t.couche = k;
        t.a = pe(n);
        t.rayon = nm(reel(n.attr("radius")));
        t.epaisseur = ep;
        d.traits.push_back(t);
        enc.etendre(t.a + Pt{t.rayon, t.rayon});
        enc.etendre(t.a - Pt{t.rayon, t.rayon});
      }
    }
  }
  if (enc.vide()) enc = Boite{-NM_PAR_MM, -NM_PAR_MM, NM_PAR_MM, NM_PAR_MM};
  d.encombrement = enc.gonflee(250'000);
  d.corps.largeur = d.encombrement.largeur();
  d.corps.profondeur = d.encombrement.hauteur();
  d.corps.hauteur = NM_PAR_MM;
  return d;
}

// ---------- Symboles ----------
TypeBroche typeBroche(const std::string& d) {
  if (d == "in") return TypeBroche::Entree;
  if (d == "out") return TypeBroche::Sortie;
  if (d == "io") return TypeBroche::Bidirectionnelle;
  if (d == "oc") return TypeBroche::CollecteurOuvert;
  if (d == "pwr" || d == "sup") return TypeBroche::AlimEntree;
  if (d == "hiz") return TypeBroche::TroisEtats;
  if (d == "nc") return TypeBroche::NonConnectee;
  return TypeBroche::Passive;
}
bool estAlimentation(const Bibliotheque& b, const Ensemble& e) {
  for (const auto& v : e.variantes)
    if (!v.paquet.empty()) return false;
  bool sup = false;
  for (const auto& g : e.portes)
    if (const auto it = b.symboles.find(g.symbole); it != b.symboles.end())
      for (const auto* p : it->second->tous("pin")) {
        if (p->attr("direction") != "sup") return false;
        sup = true;
      }
  return sup;
}
DefSymbole symbole(const Bibliotheque& b, const Ensemble& e, const Variante& v) {
  DefSymbole d;
  d.id = idSymbole(b.nom, e.nom, v.nom);
  d.nom = e.nom + v.nom;
  d.prefixe = e.prefixe.empty() ? "U" : e.prefixe;
  d.categorie = "Importés d'EAGLE";
  d.description = e.description;
  d.alimentation = estAlimentation(b, e);
  if (!v.paquet.empty()) d.empreinte = idEmpreinte(b.nom, v.paquet);
  d.unites = std::max<int>(1, static_cast<int>(e.portes.size()));
  for (std::size_t gi = 0; gi < e.portes.size(); ++gi) {
    const Porte& g = e.portes[gi];
    const auto it = b.symboles.find(g.symbole);
    if (it == b.symboles.end()) continue;
    const int unite = e.portes.size() > 1 ? static_cast<int>(gi) + 1 : 0;
    for (const auto& n : it->second->enfants) {
      if (n.nom == "pin") {
        Broche br;
        br.nom = n.attr("name");
        br.type = typeBroche(n.attr("direction", "io"));
        br.extremite = pe(n);
        const Rotation r = rotation(n.attr("rot"));
        static const int dir[4] = {0, 3, 2, 1};
        br.direction = dir[((static_cast<int>(std::lround(r.degres / 90)) % 4) + 4) % 4];
        const std::string lg = n.attr("length", "long");
        br.longueur = nm(lg == "point" ? 0 : lg == "short" ? 2.54 : lg == "middle" ? 5.08 : 7.62);
        const std::string fn = n.attr("function");
        br.style = fn == "dot" ? "inversee" : fn == "clk" ? "horloge" : fn == "dotclk" ? "horlogeInversee" : std::string();
        const std::string vis = n.attr("visible", "both");
        br.nomVisible = vis == "both" || vis == "pin";
        br.numeroVisible = vis == "both" || vis == "pad";
        br.unite = unite;
        br.groupe = n.attr("swaplevel") != "0" && !n.attr("swaplevel").empty() ? "echange" + n.attr("swaplevel") : std::string();
        const auto pads = v.connects.find({g.nom, br.nom});
        if (pads == v.connects.end() || pads->second.empty()) {
          br.numero = br.nom;  // alimentation, ou variante sans paquet
          d.broches.push_back(br);
        } else
          for (const auto& p : pads->second) {
            // Une broche reliée à plusieurs pastilles : une broche par pastille, au même point
            br.numero = p;
            d.broches.push_back(br);
          }
      } else if (n.nom == "wire" || n.nom == "rectangle" || n.nom == "circle" || n.nom == "polygon") {
        Trace t;
        t.unite = unite;
        t.epaisseur = nm(reel(n.attr("width"), 0.254));
        if (n.nom == "wire") {
          t.forme = Trace::Forme::Polyligne;
          t.points = {pe(n, "x1", "y1"), pe(n, "x2", "y2")};
        } else if (n.nom == "rectangle") {
          t.forme = Trace::Forme::Rectangle;
          t.points = {pe(n, "x1", "y1"), pe(n, "x2", "y2")};
          t.rempli = true;
        } else if (n.nom == "circle") {
          t.forme = Trace::Forme::Cercle;
          t.points = {pe(n)};
          t.rayon = nm(reel(n.attr("radius")));
        } else {
          t.forme = Trace::Forme::Polyligne;
          for (const auto* vx : n.tous("vertex")) t.points.push_back(pe(*vx));
          t.ferme = true;
          t.rempli = true;
        }
        d.traces.push_back(t);
      }
    }
  }
  if (d.alimentation) d.prefixe = "#PWR";
  return d;
}

// Point d'une broche d'une instance (repère de la feuille), d'après le symbole converti
Pt transformerEagle(const SymbolePlace& s, Pt q) {
  if (s.miroir) q.x = -q.x;
  return tournerQuarts(q, s.rotation) + s.position;
}
}  // namespace

ConceptionElec conceptionDepuisEagle(const std::optional<std::string>& schema, const std::optional<std::string>& carte, const std::string& nom, RapportImportEagle* rapport) {
  RapportImportEagle r;
  ConceptionElec c = nouvelleConception(nom);
  std::map<std::string, Bibliotheque> bibs;  // bibliothèques du schéma et de la carte
  std::vector<NoeudXml> arbres;
  arbres.reserve(2);
  auto ouvrir = [&](const std::string& texte) -> const NoeudXml& {
    arbres.push_back(lireXml(texte));
    if (arbres.back().nom != "eagle") throw std::runtime_error("Ce fichier n’est pas un fichier EAGLE (racine « " + arbres.back().nom + " »).");
    return arbres.back();
  };
  std::map<std::string, std::string> composantDe;  // nom de partie → UUID
  std::map<std::string, std::vector<std::pair<std::string, std::string>>> contacts;  // signal de la carte → (UUID, pastille)
  // ---------- Schéma ----------
  if (schema) {
    const NoeudXml& racine = ouvrir(*schema);
    const NoeudXml* sch = chemin(racine, {"drawing", "schematic"});
    if (!sch) throw std::runtime_error("Fichier EAGLE sans schéma.");
    for (const auto* l : enfants(sch->enfant("libraries"), "library")) bibs[l->attr("name")] = lireBibliotheque(*l);
    // Parties : composants (les alimentations n'en sont pas)
    struct Partie {
      std::string bib, ensemble, variante, valeur;
      bool alim = false;
    };
    std::map<std::string, Partie> parties;
    for (const auto* p : enfants(sch->enfant("parts"), "part")) {
      Partie x{p->attr("library"), p->attr("deviceset"), p->attr("device"), p->attr("value"), false};
      const auto ib = bibs.find(x.bib);
      if (ib == bibs.end() || !ib->second.ensembles.count(x.ensemble)) {
        r.alertes.push_back("Partie " + p->attr("name") + " : ensemble « " + x.ensemble + " » absent de la bibliothèque « " + x.bib + " »");
        continue;
      }
      const Bibliotheque& b = ib->second;
      const Ensemble& e = b.ensembles.at(x.ensemble);
      const Variante* v = &e.variantes.front();
      for (const auto& vv : e.variantes)
        if (vv.nom == x.variante) v = &vv;
      const DefSymbole d = symbole(b, e, *v);
      if (!c.symboles.count(d.id)) c.symboles[d.id] = d;
      if (!v->paquet.empty() && !c.empreintes.count(idEmpreinte(b.nom, v->paquet)))
        if (const auto ip = b.paquets.find(v->paquet); ip != b.paquets.end()) c.empreintes[idEmpreinte(b.nom, v->paquet)] = empreinte(*ip->second, idEmpreinte(b.nom, v->paquet));
      x.alim = d.alimentation;
      parties[p->attr("name")] = x;
      if (x.alim) continue;
      ComposantElec comp;
      comp.uuid = nouvelUuid();
      comp.reference = p->attr("name");
      comp.valeur = !x.valeur.empty() ? x.valeur : e.valeurLibre ? std::string() : e.nom + v->nom;
      comp.symbole = d.id;
      comp.empreinte = d.empreinte;
      for (const auto* a : p->tous("attribute"))
        if (!a->attr("value").empty()) comp.champs[a->attr("name")] = a->attr("value");
      if (const auto it = comp.champs.find("MPN"); it != comp.champs.end()) comp.mpn = it->second;
      c.composants.push_back(comp);
      composantDe[comp.reference] = comp.uuid;
      ++r.parties;
    }
    // Feuilles : instances, fils, étiquettes, jonctions ; réseaux réunis par nom d'une feuille à l'autre
    const auto feuilles = enfants(sch->enfant("sheets"), "sheet");
    std::map<std::string, std::set<std::size_t>> feuillesDuReseau;
    for (std::size_t i = 0; i < feuilles.size(); ++i)
      for (const auto* n : enfants(feuilles[i]->enfant("nets"), "net")) feuillesDuReseau[n->attr("name")].insert(i);
    c.feuilles.clear();
    for (std::size_t i = 0; i < feuilles.size(); ++i) {
      Feuille f;
      f.uuid = nouvelUuid();
      f.nom = "Feuille " + std::to_string(i + 1);
      f.format = "A3";
      std::map<std::pair<std::string, std::string>, SymbolePlace> places;  // (partie, porte) → pose
      for (const auto* in : enfants(feuilles[i]->enfant("instances"), "instance")) {
        const auto ip = parties.find(in->attr("part"));
        if (ip == parties.end()) continue;
        const Bibliotheque& b = bibs.at(ip->second.bib);
        const Ensemble& e = b.ensembles.at(ip->second.ensemble);
        const Variante* v = &e.variantes.front();
        for (const auto& vv : e.variantes)
          if (vv.nom == ip->second.variante) v = &vv;
        SymbolePlace sp;
        sp.uuid = nouvelUuid();
        sp.symbole = idSymbole(b.nom, e.nom, v->nom);
        sp.position = pe(*in);
        const Rotation rot = rotation(in->attr("rot"));
        // « MRθ » d'EAGLE : tourné de θ puis retourné de gauche à droite, soit retourné puis tourné de −θ
        const int quarts = ((static_cast<int>(std::lround(rot.degres / 90)) % 4) + 4) % 4;
        sp.rotation = rot.miroir ? (4 - quarts) % 4 : quarts;
        sp.miroir = rot.miroir;
        for (std::size_t gi = 0; gi < e.portes.size(); ++gi)
          if (e.portes[gi].nom == in->attr("gate")) sp.unite = e.portes.size() > 1 ? static_cast<int>(gi) + 1 : 1;
        if (ip->second.alim) {
          // Alimentation : le réseau porte le nom de sa broche
          const DefSymbole& d = c.symboles.at(sp.symbole);
          sp.nomAlim = d.broches.empty() ? ip->second.valeur : d.broches.front().nom;
        } else
          sp.composant = composantDe[in->attr("part")];
        places[{in->attr("part"), in->attr("gate")}] = sp;
        f.symboles.push_back(sp);
      }
      for (const auto* n : enfants(feuilles[i]->enfant("nets"), "net")) {
        const std::string nomReseau = n->attr("name");
        const bool global = feuillesDuReseau[nomReseau].size() > 1;
        const bool nomme = nomReseau.rfind("N$", 0) != 0;
        for (const auto* sg : n->tous("segment")) {
          std::vector<Pt> bouts;
          for (const auto* w : sg->tous("wire")) {
            const Pt a = pe(*w, "x1", "y1"), b2 = pe(*w, "x2", "y2");
            f.fils.push_back({nouvelUuid(), a, b2});
            bouts.push_back(a);
            bouts.push_back(b2);
          }
          for (const auto* j : sg->tous("junction")) f.jonctions.push_back({pe(*j), nouvelUuid()});
          bool etiquette = false;
          for (const auto* l : sg->tous("label")) {
            Etiquette et;
            et.uuid = nouvelUuid();
            et.texte = nomReseau;
            et.p = pe(*l);
            // L'étiquette d'EAGLE est posée près du fil : son point de connexion est l'extrémité de fil la plus proche
            double meilleur = 1e300;
            for (const Pt& q : bouts)
              if (distance(q, et.p) < meilleur) {
                meilleur = distance(q, et.p);
                et.p = q;
              }
            et.type = global || l->attr("xref") == "yes" ? TypeEtiquette::Globale : TypeEtiquette::Locale;
            f.etiquettes.push_back(et);
            etiquette = true;
          }
          // Broches du segment : un point de fil doit les toucher ; sinon une étiquette du réseau, posée sur la broche,
          // garde le lien que dit le fichier
          std::vector<Pt> broches;
          for (const auto* pr : sg->tous("pinref")) {
            const auto it = places.find({pr->attr("part"), pr->attr("gate")});
            if (it == places.end()) continue;
            const DefSymbole& d = c.symboles.at(it->second.symbole);
            for (const auto& b : d.broches)
              if (b.nom == pr->attr("pin") && dansUnite(b.unite, it->second, d)) {
                broches.push_back(transformerEagle(it->second, b.extremite));
                break;
              }
          }
          for (const Pt& p : broches) {
            const bool touche = std::any_of(f.fils.begin(), f.fils.end(), [&](const Fil& x) { return surSegment(p, x.a, x.b); });
            if (touche) continue;
            Etiquette et;
            et.uuid = nouvelUuid();
            et.texte = nomReseau;
            et.p = p;
            et.type = global ? TypeEtiquette::Globale : TypeEtiquette::Locale;
            f.etiquettes.push_back(et);
            etiquette = true;
          }
          // Réseau nommé ou présent sur plusieurs feuilles : son nom posé sur le segment
          if (!etiquette && (global || nomme) && (!bouts.empty() || !broches.empty())) {
            Etiquette et;
            et.uuid = nouvelUuid();
            et.texte = nomReseau;
            et.p = !bouts.empty() ? bouts.front() : broches.front();
            et.type = global ? TypeEtiquette::Globale : TypeEtiquette::Locale;
            f.etiquettes.push_back(et);
          }
        }
        ++r.reseaux;
      }
      for (const auto* t : enfants(feuilles[i]->enfant("plain"), "text")) {
        TexteSchema tx;
        tx.uuid = nouvelUuid();
        tx.texte = t->texte;
        tx.p = pe(*t);
        f.textes.push_back(tx);
      }
      c.feuilles.push_back(std::move(f));
      ++r.feuilles;
    }
    if (c.feuilles.empty()) c.feuilles.push_back(Feuille{});
  }
  // ---------- Carte ----------
  if (carte) {
    const NoeudXml& racine = ouvrir(*carte);
    const NoeudXml* brd = chemin(racine, {"drawing", "board"});
    if (!brd) throw std::runtime_error("Fichier EAGLE sans carte.");
    // Cuivres déclarés : couches 1 à 16 utilisées
    int cuivres = 2;
    if (const NoeudXml* ls = chemin(racine, {"drawing", "layers"})) {
      int n = 0;
      for (const auto* l : ls->tous("layer")) {
        const int num = static_cast<int>(reel(l->attr("number")));
        if (num >= 1 && num <= 16 && l->attr("active", "yes") == "yes") ++n;
      }
      if (n >= 2) cuivres = n % 2 ? n + 1 : n;
    }
    c.circuit.empilage.couchesCuivre = std::clamp(cuivres, 2, 16);
    // Noms d'origine des couches, pour le tableau d'association (10.12)
    if (const NoeudXml* ls = chemin(racine, {"drawing", "layers"}))
      for (const auto* l : ls->tous("layer")) {
        const int num = static_cast<int>(reel(l->attr("number")));
        std::optional<Couche> k;
        if (num >= 1 && num <= 16) k = coucheCuivreEagle(num, c.circuit.empilage.couchesCuivre);
        else if (num == 20) k = Couche::Contour;
        else if (num == 21 || num == 22) k = num == 21 ? Couche::SerigraphieDessus : Couche::SerigraphieDessous;
        else if (num == 29 || num == 30) k = num == 29 ? Couche::VernisDessus : Couche::VernisDessous;
        else if (num == 31 || num == 32) k = num == 31 ? Couche::PateDessus : Couche::PateDessous;
        else if (num == 35 || num == 36) k = num == 35 ? Couche::ColleDessus : Couche::ColleDessous;
        else if (num == 51 || num == 52) k = num == 51 ? Couche::FabricationDessus : Couche::FabricationDessous;
        if (k && (num < 2 || num > 15 || l->attr("active", "yes") == "yes")) c.extensions["importCouches"][codeCouche(*k)] = std::to_string(num) + " " + l->attr("name");
      }
    std::map<std::string, Bibliotheque> bibsCarte;
    for (const auto* l : enfants(brd->enfant("libraries"), "library")) bibsCarte[l->attr("name")] = lireBibliotheque(*l);
    // Contour : traits de la couche Dimension (20)
    std::vector<ElementContour> morceaux;
    for (const auto* w : enfants(brd->enfant("plain"), "wire"))
      if (w->attr("layer") == "20") {
        const double x1 = reel(w->attr("x1")), y1 = reel(w->attr("y1")), x2 = reel(w->attr("x2")), y2 = reel(w->attr("y2"));
        std::optional<Point> m = milieuArc(x1, y1, x2, y2, reel(w->attr("curve")));
        if (m) m->y = -m->y;
        morceaux.push_back({{x1, -y1}, {x2, -y2}, m});
      }
    bool premier = true;
    for (const auto& ct : kicad_interne::chainer(morceaux)) {
      if (!ct.ferme) continue;
      Polygone poly;
      for (const Point& q : ct.polygone(0.1)) {
        const Pt x = ptMm(q.x, q.y);
        if (poly.empty() || !(poly.back() == x)) poly.push_back(x);
      }
      while (poly.size() > 1 && poly.front() == poly.back()) poly.pop_back();
      if (poly.size() < 3) continue;
      if (premier) {
        c.circuit.contour.libre = orienter(poly);
        premier = false;
      } else
        c.circuit.contour.decoupes.push_back(orienter(poly));
    }
    if (premier) r.alertes.push_back("Aucun contour fermé sur la couche Dimension : la carte garde un rectangle par défaut");
    // Trous de la carte : une empreinte de trou par diamètre
    int trous = 0;
    for (const auto* h : enfants(brd->enfant("plain"), "hole")) {
      const double dr = reel(h->attr("drill"));
      const std::string id = "eagle:trou:" + std::to_string(dr);
      if (!c.empreintes.count(id)) {
        DefEmpreinte d;
        d.id = id;
        d.famille = "EAGLE";
        d.description = "Trou de " + std::to_string(dr) + " mm";
        Pastille p;
        p.type = TypePastille::NonMetallisee;
        p.forme = FormePastille::Rond;
        p.percage = p.largeur = p.hauteur = nm(dr);
        d.pastilles.push_back(p);
        d.encombrement = boite(formePastille(p)).gonflee(250'000);
        c.empreintes[id] = d;
      }
      ComposantElec x;
      x.uuid = nouvelUuid();
      x.reference = "H" + std::to_string(++trous + 900);
      x.empreinte = id;
      x.carteSeule = true;
      x.genre = GenreComposant::Mecanique;
      EmpreintePlacee em;
      em.composant = x.uuid;
      em.empreinte = id;
      em.position = pe(*h);
      c.composants.push_back(x);
      c.circuit.empreintes.push_back(em);
    }
    // Textes et dessins de la carte
    for (const auto* t : enfants(brd->enfant("plain"), "text")) {
      const std::string l = t->attr("layer");
      const Couche k = l == "21" ? Couche::SerigraphieDessus : l == "22" ? Couche::SerigraphieDessous : l == "1" ? Couche::CuivreDessus : l == "16" ? Couche::CuivreDessous : Couche::Dessins;
      TexteCircuit tx;
      tx.uuid = nouvelUuid();
      tx.texte = t->texte;
      tx.couche = k;
      tx.p = pe(*t);
      tx.hauteur = nm(reel(t->attr("size"), 1.27));
      tx.rotation = rotation(t->attr("rot")).degres;
      c.circuit.textes.push_back(tx);
    }
    // Éléments : empreintes posées, reliées aux parties du schéma de même nom
    for (const auto* el : enfants(brd->enfant("elements"), "element")) {
      const std::string nomE = el->attr("name"), bib = el->attr("library"), paquet = el->attr("package");
      const auto ib = bibsCarte.find(bib);
      if (ib == bibsCarte.end() || !ib->second.paquets.count(paquet)) {
        r.alertes.push_back("Élément " + nomE + " : paquet « " + paquet + " » absent de la bibliothèque « " + bib + " »");
        continue;
      }
      const std::string id = idEmpreinte(bib, paquet);
      c.empreintes[id] = empreinte(*ib->second.paquets.at(paquet), id);  // la carte fait foi pour la géométrie
      ComposantElec* x = nullptr;
      if (const auto it = composantDe.find(nomE); it != composantDe.end()) x = c.composant(it->second);
      if (!x) {
        ComposantElec n;
        n.uuid = nouvelUuid();
        n.reference = nomE;
        n.valeur = el->attr("value");
        n.carteSeule = true;
        c.composants.push_back(n);
        x = &c.composants.back();
        composantDe[nomE] = n.uuid;
      }
      x->empreinte = id;
      EmpreintePlacee em;
      em.composant = x->uuid;
      em.empreinte = id;
      em.position = pe(*el);
      const Rotation rot = rotation(el->attr("rot"));
      // Élément retourné (« MRθ ») : tourné de θ puis retourné, soit retourné puis tourné de −θ
      em.rotation = rot.miroir ? std::fmod(360 - rot.degres, 360.0) : rot.degres;
      em.dessous = rot.miroir;
      em.verrou = el->attr("locked") == "yes";
      c.circuit.empreintes.push_back(em);
      ++r.elements;
    }
    // Signaux : réseaux des pastilles (cartes sans schéma), pistes, vias, polygones
    for (const auto* sg : enfants(brd->enfant("signals"), "signal")) {
      const std::string reseau = sg->attr("name");
      for (const auto* cr : sg->tous("contactref")) {
        const auto it = composantDe.find(cr->attr("element"));
        if (it == composantDe.end()) continue;
        contacts[reseau].push_back({it->second, cr->attr("pad")});
        if (const ComposantElec* x = c.composant(it->second); x && x->carteSeule) c.nomsReseaux[it->second + ":" + cr->attr("pad")] = reseau;
      }
      for (const auto* w : sg->tous("wire")) {
        const int l = static_cast<int>(reel(w->attr("layer")));
        if (l < 1 || l > 16) continue;
        Piste p{nouvelUuid(), reseau, coucheCuivreEagle(l, c.circuit.empilage.couchesCuivre), pe(*w, "x1", "y1"), pe(*w, "x2", "y2"), nm(reel(w->attr("width"), 0.254))};
        // Trait courbe : arc d'angle « curve »
        if (const auto m = milieuArc(reel(w->attr("x1")), reel(w->attr("y1")), reel(w->attr("x2")), reel(w->attr("y2")), reel(w->attr("curve")))) p.milieu = ptMm(m->x, -m->y);
        c.circuit.pistes.push_back(p);
        ++r.pistes;
      }
      for (const auto* v : sg->tous("via")) {
        Via via;
        via.uuid = nouvelUuid();
        via.reseau = reseau;
        via.p = pe(*v);
        via.percage = nm(reel(v->attr("drill"), 0.4));
        const double dia = reel(v->attr("diameter"));
        via.diametre = dia > 0 ? nm(dia) : via.percage + 2 * std::max<Nm>(150'000, via.percage / 4);
        const std::string ext = v->attr("extent", "1-16");
        if (const auto t = ext.find('-'); t != std::string::npos) {
          const int de = static_cast<int>(reel(ext.substr(0, t))), a = static_cast<int>(reel(ext.substr(t + 1)));
          via.de = coucheCuivreEagle(de, c.circuit.empilage.couchesCuivre);
          via.vers = coucheCuivreEagle(a, c.circuit.empilage.couchesCuivre);
          if (!(de == 1 && a == 16)) via.type = de == 1 || a == 16 ? TypeVia::Borgne : TypeVia::Enterree;
        }
        c.circuit.vias.push_back(via);
        ++r.vias;
      }
      for (const auto* pg : sg->tous("polygon")) {
        const int l = static_cast<int>(reel(pg->attr("layer")));
        if (l < 1 || l > 16) continue;
        Zone z;
        z.uuid = nouvelUuid();
        z.reseau = reseau;
        z.couche = coucheCuivreEagle(l, c.circuit.empilage.couchesCuivre);
        for (const auto* vx : pg->tous("vertex")) z.contour.push_back(pe(*vx));
        if (const double iso = reel(pg->attr("isolate")); iso > 0) z.isolement = nm(iso);
        if (!pg->attr("rank").empty()) z.priorite = 7 - static_cast<int>(reel(pg->attr("rank"), 1));
        if (z.contour.size() >= 3) {
          c.circuit.zones.push_back(z);
          ++r.polygones;
        }
      }
    }
  }
  if (schema) {
    synchroniserReseaux(c);
    // Projet relié : chaque signal de la carte prend le nom que le schéma donne à ses pastilles (« N$3 » devient le
    // nom automatique du réseau) ; un signal qui réunit des pastilles de réseaux différents est signalé
    const Connectivite k = connectivite(c);
    std::map<std::string, std::string> renommer;
    for (const auto& [signal, liste] : contacts) {
      std::set<std::string> noms;
      for (const auto& [u, pad] : liste)
        if (const std::string n = k.nom(u, pad); !n.empty()) noms.insert(n);
      if (noms.size() > 1) {
        std::string t;
        for (const auto& n : noms) t += (t.empty() ? "" : ", ") + n;
        r.alertes.push_back("Signal « " + signal + " » de la carte : ses pastilles sont sur plusieurs réseaux du schéma (" + t + ")");
      }
      if (noms.size() == 1 && *noms.begin() != signal) renommer[signal] = *noms.begin();
    }
    for (auto& p : c.circuit.pistes)
      if (const auto it = renommer.find(p.reseau); it != renommer.end()) p.reseau = it->second;
    for (auto& v : c.circuit.vias)
      if (const auto it = renommer.find(v.reseau); it != renommer.end()) v.reseau = it->second;
    for (auto& z : c.circuit.zones)
      if (const auto it = renommer.find(z.reseau); it != renommer.end()) z.reseau = it->second;
  }
  if (rapport) *rapport = r;
  return c;
}

RapportBibliotheque importerBibliothequeEagle(const std::string& lbr, ContenuBibliotheque& destination, bool remplacer) {
  RapportBibliotheque rap;
  const NoeudXml racine = lireXml(lbr);
  const NoeudXml* lib = chemin(racine, {"drawing", "library"});
  if (racine.nom != "eagle" || !lib) throw std::runtime_error("Ce fichier n’est pas une bibliothèque EAGLE (.lbr).");
  Bibliotheque b = lireBibliotheque(*lib);
  if (b.nom.empty()) b.nom = "eagle";
  for (const auto& [nomP, paquet] : b.paquets) {
    DefEmpreinte d = empreinte(*paquet, idEmpreinte(b.nom, nomP));
    if (d.pastilles.empty()) {
      rap.refuser("empreinte", nomP, "paquet sans pastille");
      continue;
    }
    rap.poser(destination.empreintes, std::move(d), "empreinte", nomP, remplacer);
  }
  for (const auto& [nomE, e] : b.ensembles)
    for (const auto& v : e.variantes) {
      const DefSymbole d = symbole(b, e, v);
      const std::string nomObjet = nomE + v.nom;
      if (d.broches.empty()) {
        rap.refuser("symbole", nomObjet, "aucune porte ne désigne un symbole de la bibliothèque");
        continue;
      }
      // Broches sans pastille dans une variante à paquet : signalées, l'article reste
      std::string detail;
      if (!v.paquet.empty())
        for (const auto& br : d.broches)
          if (br.numero == br.nom && !b.paquets.count(v.paquet)) detail = "paquet « " + v.paquet + " » absent";
      rap.poser(destination.symboles, d, "symbole", nomObjet, remplacer);
      if (d.alimentation) continue;
      Article a;
      a.id = "eagle:" + b.nom + ":" + nomObjet;
      a.nom = nomObjet;
      a.description = e.description;
      a.categorie = e.prefixe;
      a.symbole = d.id;
      a.empreinte = v.paquet.empty() ? std::string() : idEmpreinte(b.nom, v.paquet);
      rap.poser(destination.articles, std::move(a), "article", nomObjet, remplacer);
      if (!detail.empty()) rap.objets.back().detail = detail;
    }
  return rap;
}

}  // namespace prom::ecad
