// Prométhée : schémas KiCad (.kicad_sch) en entrée et en sortie (10.3). Voir kicad_schema.hpp. Licence GPL-3.0-only.
#include "promethee/kicad_schema.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <set>

#include "kicad_interne.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/fichiers.hpp"

namespace prom::ecad {

namespace {
using kicad_interne::Expr;

Pt pk(const Expr& e, std::size_t i = 1) { return {nm(e.nombre(i)), nm(e.nombre(i + 1))}; }
// Point d'un symbole de bibliothèque (Y vers le haut chez KiCad) dans le repère des symboles de Prométhée (Y vers le bas)
Pt pl(const Expr& e, std::size_t i = 1) { return {nm(e.nombre(i)), -nm(e.nombre(i + 1))}; }
int quarts(double angle) { return ((static_cast<int>(std::lround(angle / 90.0)) % 4) + 4) % 4; }
bool drapeau(const Expr& e, const std::string& n) {
  for (const auto& x : e.elements)
    if (!x.liste && x.atome == n) return true;
  if (const Expr* x = e.enfant(n)) return x->valeur(1) != "no";
  return false;
}
std::string propriete(const Expr& e, const std::string& nom) {
  for (const auto& p : e.elements)
    if (p.nom() == "property" && p.valeur(1) == nom) return p.valeur(2);
  return {};
}
TypeBroche typeDepuisKicad(const std::string& t) {
  if (t == "input") return TypeBroche::Entree;
  if (t == "output") return TypeBroche::Sortie;
  if (t == "bidirectional") return TypeBroche::Bidirectionnelle;
  if (t == "tri_state") return TypeBroche::TroisEtats;
  if (t == "power_in") return TypeBroche::AlimEntree;
  if (t == "power_out") return TypeBroche::AlimSortie;
  if (t == "open_collector" || t == "open_emitter") return TypeBroche::CollecteurOuvert;
  if (t == "no_connect" || t == "unconnected") return TypeBroche::NonConnectee;
  if (t == "unspecified" || t == "free") return TypeBroche::NonSpecifiee;
  return TypeBroche::Passive;
}
const char* typeVersKicad(TypeBroche t) {
  switch (t) {
    case TypeBroche::Entree: return "input";
    case TypeBroche::Sortie: return "output";
    case TypeBroche::Bidirectionnelle: return "bidirectional";
    case TypeBroche::TroisEtats: return "tri_state";
    case TypeBroche::AlimEntree: return "power_in";
    case TypeBroche::AlimSortie: return "power_out";
    case TypeBroche::CollecteurOuvert: return "open_collector";
    case TypeBroche::NonConnectee: return "no_connect";
    case TypeBroche::NonSpecifiee: return "unspecified";
    case TypeBroche::Passive: return "passive";
  }
  return "passive";
}
std::string styleDepuisKicad(const std::string& s) {
  static const std::map<std::string, std::string> t = {{"inverted", "inversee"}, {"clock", "horloge"}, {"inverted_clock", "horlogeInversee"}, {"input_low", "entreeBasse"},
                                                       {"output_low", "sortieBasse"}, {"clock_low", "horlogeBasse"}, {"non_logic", "nonLogique"}};
  const auto it = t.find(s);
  return it == t.end() ? std::string() : it->second;
}
std::string styleVersKicad(const std::string& s) {
  static const std::map<std::string, std::string> t = {{"inversee", "inverted"}, {"horloge", "clock"}, {"horlogeInversee", "inverted_clock"}, {"entreeBasse", "input_low"},
                                                       {"sortieBasse", "output_low"}, {"horlogeBasse", "clock_low"}, {"nonLogique", "non_logic"}};
  const auto it = t.find(s);
  return it == t.end() ? std::string("line") : it->second;
}
SensSignal sensDepuisForme(const std::string& f) {
  if (f == "input") return SensSignal::Entree;
  if (f == "output") return SensSignal::Sortie;
  if (f == "bidirectional") return SensSignal::Bidirectionnel;
  if (f == "tri_state") return SensSignal::TroisEtats;
  if (f == "passive") return SensSignal::Passif;
  return SensSignal::Aucun;
}
const char* formeDepuisSens(SensSignal s) {
  switch (s) {
    case SensSignal::Entree: return "input";
    case SensSignal::Sortie: return "output";
    case SensSignal::Bidirectionnel: return "bidirectional";
    case SensSignal::TroisEtats: return "tri_state";
    case SensSignal::Passif:
    case SensSignal::Aucun: return "passive";
  }
  return "passive";
}
// Angle (degrés, sens direct à l'écran) d'un point autour d'un centre, Y vers le bas
double angleEcran(Pt c, Pt p) { return std::atan2(-static_cast<double>(p.y - c.y), static_cast<double>(p.x - c.x)) * 180 / std::numbers::pi; }
double norm360(double a) {
  a = std::fmod(a, 360.0);
  return a < 0 ? a + 360 : a;
}

// ---------- Symboles de bibliothèque ----------
struct LectureSymboles {
  std::map<std::string, const Expr*> parNom;  // nom de bibliothèque → expression
  std::map<std::string, DefSymbole> faits;
  RapportImportSchemaKicad* rapport;

  void graphique(const Expr& g, int unite, int vue, DefSymbole& d) {
    const std::string n = g.nom();
    Trace t;
    t.unite = unite;
    t.vue = vue > 1 ? vue : 0;
    if (const Expr* f = g.enfant("fill")) {
      const std::string ty = f->enfant("type") ? f->enfant("type")->valeur(1) : f->valeur(1);
      t.fond = ty == "background";
      t.rempli = ty == "outline";
    }
    if (const Expr* st = g.enfant("stroke"))
      if (const Expr* w = st->enfant("width")) t.epaisseur = nm(w->nombre(1, 0));
    if (n == "rectangle" && g.enfant("start") && g.enfant("end")) {
      t.forme = Trace::Forme::Rectangle;
      t.points = {pl(*g.enfant("start")), pl(*g.enfant("end"))};
    } else if (n == "polyline" && g.enfant("pts")) {
      t.forme = Trace::Forme::Polyligne;
      for (const auto& xy : g.enfant("pts")->elements)
        if (xy.nom() == "xy") t.points.push_back(pl(xy));
      t.ferme = t.points.size() > 2 && t.points.front() == t.points.back();
    } else if (n == "circle" && g.enfant("center")) {
      t.forme = Trace::Forme::Cercle;
      t.points = {pl(*g.enfant("center"))};
      t.rayon = nm(g.enfant("radius") ? g.enfant("radius")->nombre(1, 1) : 1);
    } else if (n == "arc" && g.enfant("start") && g.enfant("end") && g.enfant("mid")) {
      const Pt a = pl(*g.enfant("start")), m = pl(*g.enfant("mid")), b = pl(*g.enfant("end"));
      double cx = 0, cy = 0, r = 0;
      if (!cercleParTroisPoints(a, m, b, cx, cy, r)) {
        t.forme = Trace::Forme::Polyligne;
        t.points = {a, b};
      } else {
        const Pt c{static_cast<Nm>(std::llround(cx)), static_cast<Nm>(std::llround(cy))};
        t.forme = Trace::Forme::Arc;
        t.points = {c};
        t.rayon = static_cast<Nm>(std::llround(r));
        const double aa = norm360(angleEcran(c, a)), am = norm360(angleEcran(c, m)), ab = norm360(angleEcran(c, b));
        // Sens direct de « debut » à « fin » en passant par le milieu
        const bool direct = norm360(am - aa) <= norm360(ab - aa);
        t.debut = direct ? aa : ab;
        t.fin = direct ? ab : aa;
      }
    } else if (n == "text" && g.enfant("at")) {
      t.forme = Trace::Forme::Texte;
      t.points = {pl(*g.enfant("at"))};
      t.texte = g.valeur(1);
    } else
      return;
    d.traces.push_back(t);
  }
  void broche(const Expr& p, int unite, int vue, DefSymbole& d) {
    Broche b;
    b.type = typeDepuisKicad(p.valeur(1));
    b.style = styleDepuisKicad(p.valeur(2));
    b.unite = unite;
    b.vue = vue > 1 ? vue : 0;
    if (const Expr* at = p.enfant("at")) {
      b.extremite = pl(*at);
      const int q = quarts(at->nombre(3, 0));
      static const int dir[4] = {0, 3, 2, 1};  // 0° → +x, 90° (vers le haut) → −y…
      b.direction = dir[q];
    }
    if (const Expr* l = p.enfant("length")) b.longueur = nm(l->nombre(1, 2.54));
    if (const Expr* n = p.enfant("name")) b.nom = n->valeur(1) == "~" ? std::string() : n->valeur(1);
    if (const Expr* n = p.enfant("number")) b.numero = n->valeur(1);
    b.cachee = drapeau(p, "hide");
    for (const auto& alt : p.elements)
      if (alt.nom() == "alternate") b.alternatives.push_back({alt.valeur(1), typeDepuisKicad(alt.valeur(2))});
    d.broches.push_back(b);
  }
  const DefSymbole* definition(const std::string& nomLib, int profondeur = 0) {
    if (const auto it = faits.find(nomLib); it != faits.end()) return &it->second;
    const auto ie = parNom.find(nomLib);
    if (ie == parNom.end() || profondeur > 8) return nullptr;
    const Expr& s = *ie->second;
    DefSymbole d;
    if (const Expr* ext = s.enfant("extends")) {
      // Symbole dérivé : le dessin et les broches du parent (de la même bibliothèque)
      const std::string bib = nomLib.find(':') == std::string::npos ? std::string() : nomLib.substr(0, nomLib.find(':') + 1);
      const DefSymbole* parent = definition(bib + ext->valeur(1), profondeur + 1);
      if (!parent) parent = definition(ext->valeur(1), profondeur + 1);
      if (parent) d = *parent;
      d.parent = parent ? parent->id : std::string();
    }
    d.id = "kicad:" + nomLib;
    d.nom = nomLib.substr(nomLib.find(':') == std::string::npos ? 0 : nomLib.find(':') + 1);
    d.categorie = "Importés de KiCad";
    std::string ref = propriete(s, "Reference");
    while (!ref.empty() && (ref.back() == '?' || std::isdigit(static_cast<unsigned char>(ref.back())))) ref.pop_back();
    if (!ref.empty()) d.prefixe = ref;
    if (const std::string v = propriete(s, "Datasheet"); !v.empty() && v != "~") d.fiche = v;
    if (const std::string v = propriete(s, "Description"); !v.empty()) d.description = v;
    if (const std::string v = propriete(s, "ki_description"); !v.empty()) d.description = v;
    if (const std::string v = propriete(s, "Footprint"); !v.empty()) d.proprietes["empreinteKicad"] = v;
    if (const std::string v = propriete(s, "ki_keywords"); !v.empty()) d.motsCles = v;
    d.alimentation = drapeau(s, "power");
    if (d.alimentation) d.prefixe = "#PWR";
    int unites = std::max(d.unites, 1);
    bool demorgan = false;
    for (const auto& sub : s.elements) {
      if (sub.nom() != "symbol") continue;
      // « R_1_1 » : unité 1, vue 1 ; « R_0_1 » : commun à toutes les unités
      const std::string n = sub.valeur(1);
      const auto p2 = n.rfind('_');
      const auto p1 = p2 == std::string::npos ? std::string::npos : n.rfind('_', p2 - 1);
      int unite = 0, vue = 1;
      if (p1 != std::string::npos) {
        unite = std::atoi(n.substr(p1 + 1, p2 - p1 - 1).c_str());
        vue = std::atoi(n.substr(p2 + 1).c_str());
      }
      demorgan |= vue == 2;
      unites = std::max(unites, unite);
      for (const auto& g : sub.elements) {
        if (g.nom() == "pin") broche(g, unite, vue, d);
        else if (g.liste) graphique(g, unite, vue, d);
      }
    }
    d.unites = unites;
    d.vues = demorgan ? 2 : 1;
    // Une broche d'unité 0 est commune : celles d'un symbole à une unité le sont toutes
    if (d.unites <= 1)
      for (auto& b : d.broches) b.unite = 0;
    return &(faits[nomLib] = std::move(d));
  }
};

struct Importeur {
  ConceptionElec& c;
  RapportImportSchemaKicad& r;
  const std::function<std::optional<std::string>(const std::string&)>& lire;
  std::map<std::string, std::string> composantsParReference;  // repère → UUID (unités d'un même composant)
  std::map<std::string, std::pair<std::string, int>> instances6;  // KiCad 6 : chemin → (repère, unité)
  std::string racineUuid;
  std::vector<std::unique_ptr<Expr>> gardes;  // expressions des sous-feuilles, gardées pendant la lecture

  std::pair<std::string, int> instance(const Expr& sym, const std::string& chemin, const std::string& uuidSymbole) {
    // KiCad 7 et suivants : instances par symbole, chemin de la feuille
    if (const Expr* ins = sym.enfant("instances"))
      for (const auto& pr : ins->elements) {
        if (pr.nom() != "project") continue;
        for (const auto& pa : pr.elements)
          if (pa.nom() == "path" && pa.valeur(1) == chemin) {
            const Expr* re = pa.enfant("reference");
            const Expr* un = pa.enfant("unit");
            return {re ? re->valeur(1) : std::string(), un ? static_cast<int>(un->nombre(1, 1)) : 0};
          }
      }
    // KiCad 6 : table des instances de la racine, chemin de la feuille suivi de l'UUID du symbole
    std::string chemin6 = chemin.substr(racineUuid.empty() ? 0 : std::min(chemin.size(), racineUuid.size() + 1));
    const std::string cle = (chemin6.empty() || chemin6 == "/" ? std::string() : (chemin6.front() == '/' ? chemin6 : "/" + chemin6)) + "/" + uuidSymbole;
    if (const auto it = instances6.find(cle); it != instances6.end()) return it->second;
    return {propriete(sym, "Reference"), 0};
  }

  void feuille(const Expr& racine, std::size_t indice, const std::string& chemin, LectureSymboles& lib, int profondeur) {
    // Symboles de bibliothèque de ce fichier
    if (const Expr* ls = racine.enfant("lib_symbols"))
      for (const auto& s : ls->elements)
        if (s.nom() == "symbol" && !lib.parNom.count(s.valeur(1))) lib.parNom[s.valeur(1)] = &s;
    if (const Expr* p = racine.enfant("paper")) c.feuilles[indice].format = p->valeur(1) == "A3" || p->valeur(1) == "A2" || p->valeur(1) == "A1" || p->valeur(1) == "A0" ? "A3" : "A4";
    ++r.feuilles;
    for (const auto& e : racine.elements) {
      const std::string n = e.nom();
      Feuille& f = c.feuilles[indice];
      if (n == "symbol" && e.enfant("lib_id")) {
        const std::string nomLib = e.enfant("lib_id")->valeur(1);
        const std::string libName = e.enfant("lib_name") ? e.enfant("lib_name")->valeur(1) : nomLib;
        const DefSymbole* d = lib.definition(libName);
        if (!d) d = lib.definition(nomLib);
        if (!d) {
          r.alertes.push_back("Symbole « " + nomLib + " » absent des symboles du fichier : laissé de côté");
          continue;
        }
        if (!c.symboles.count(d->id)) c.symboles[d->id] = *d;
        SymbolePlace sp;
        sp.uuid = e.enfant("uuid") ? e.enfant("uuid")->valeur(1) : nouvelUuid();
        if (std::any_of(f.symboles.begin(), f.symboles.end(), [&](const SymbolePlace& x) { return x.uuid == sp.uuid; }) || sp.uuid.empty()) sp.uuid = nouvelUuid();
        sp.symbole = d->id;
        if (const Expr* at = e.enfant("at")) {
          sp.position = pk(*at);
          sp.rotation = quarts(at->nombre(3, 0));
        }
        if (const Expr* m = e.enfant("mirror")) {
          sp.miroir = true;
          if (m->valeur(1) == "x") sp.rotation = (sp.rotation + 2) % 4;
        }
        if (const Expr* u = e.enfant("unit")) sp.unite = std::max(1, static_cast<int>(u->nombre(1, 1)));
        if (const Expr* cv = e.enfant("convert")) sp.vue = std::max(1, static_cast<int>(cv->nombre(1, 1)));
        if (const Expr* bs = e.enfant("body_style")) sp.vue = std::max(1, static_cast<int>(bs->nombre(1, 1)));
        if (d->alimentation) {
          sp.nomAlim = propriete(e, "Value");
          if (sp.nomAlim.empty()) sp.nomAlim = d->nom;
          f.symboles.push_back(sp);
          ++r.symboles;
          continue;
        }
        const auto [reference, uniteInstance] = instance(e, chemin, sp.uuid);
        if (uniteInstance > 0) sp.unite = uniteInstance;
        const bool annote = !reference.empty() && reference.back() != '?';
        ComposantElec* comp = nullptr;
        if (annote)
          if (const auto it = composantsParReference.find(reference); it != composantsParReference.end()) comp = c.composant(it->second);
        if (!comp) {
          ComposantElec x;
          x.uuid = nouvelUuid();
          x.reference = reference.empty() ? d->prefixe + "?" : reference;
          x.valeur = propriete(e, "Value");
          x.symbole = d->id;
          const std::string fp = propriete(e, "Footprint");
          if (!fp.empty()) {
            x.champs["Footprint"] = fp;
            x.empreinte = empreinteConnue(fp);  // l'empreinte du catalogue quand KiCad en nomme une connue
          }
          if (const std::string v = propriete(e, "Datasheet"); !v.empty() && v != "~") x.fiche = v;
          if (const std::string v = propriete(e, "MPN"); !v.empty()) x.mpn = v;
          if (const std::string v = propriete(e, "Manufacturer"); !v.empty()) x.fabricant = v;
          if (const std::string v = propriete(e, "Description"); !v.empty()) x.description = v;
          for (const auto& p : e.elements)
            if (p.nom() == "property") {
              const std::string k = p.valeur(1);
              if (k != "Reference" && k != "Value" && k != "Footprint" && k != "Datasheet" && k != "Description" && k != "MPN" && k != "Manufacturer" && !p.valeur(2).empty()) x.champs[k] = p.valeur(2);
            }
          x.nePasMonter = drapeau(e, "dnp");
          if (const Expr* ib = e.enfant("in_bom")) x.horsNomenclature = ib->valeur(1) == "no";
          if (const Expr* ob = e.enfant("on_board")) x.horsCarte = ob->valeur(1) == "no";
          c.composants.push_back(x);
          comp = &c.composants.back();
          if (annote) composantsParReference[reference] = comp->uuid;
          ++r.composants;
        }
        sp.composant = comp->uuid;
        f.symboles.push_back(sp);
        ++r.symboles;
      } else if (n == "wire" && e.enfant("pts")) {
        std::vector<Pt> p;
        for (const auto& xy : e.enfant("pts")->elements)
          if (xy.nom() == "xy") p.push_back(pk(xy));
        for (std::size_t i = 1; i < p.size(); ++i) f.fils.push_back({nouvelUuid(), p[i - 1], p[i]});
        ++r.fils;
      } else if (n == "junction" && e.enfant("at")) {
        f.jonctions.push_back({pk(*e.enfant("at")), nouvelUuid()});
      } else if (n == "no_connect" && e.enfant("at")) {
        f.nonConnexions.push_back({pk(*e.enfant("at")), nouvelUuid()});
      } else if ((n == "label" || n == "global_label" || n == "hierarchical_label") && e.enfant("at")) {
        Etiquette et;
        et.uuid = nouvelUuid();
        et.texte = e.valeur(1);
        et.p = pk(*e.enfant("at"));
        et.rotation = quarts(e.enfant("at")->nombre(3, 0));
        et.type = n == "label" ? TypeEtiquette::Locale : n == "global_label" ? TypeEtiquette::Globale : TypeEtiquette::Hierarchique;
        if (const Expr* sh = e.enfant("shape")) et.sens = sensDepuisForme(sh->valeur(1));
        f.etiquettes.push_back(et);
        ++r.etiquettes;
      } else if (n == "text" && e.enfant("at")) {
        TexteSchema t;
        t.uuid = nouvelUuid();
        t.texte = e.valeur(1);
        t.p = pk(*e.enfant("at"));
        t.rotation = quarts(e.enfant("at")->nombre(3, 0));
        f.textes.push_back(t);
      } else if (n == "bus" || n == "bus_entry") {
        r.alertes.push_back(n == "bus" ? "Bus dessiné repris en fil" : "Entrée de bus reprise en fil");
        std::vector<Pt> p;
        if (n == "bus" && e.enfant("pts"))
          for (const auto& xy : e.enfant("pts")->elements)
            if (xy.nom() == "xy") p.push_back(pk(xy));
        if (n == "bus_entry" && e.enfant("at") && e.enfant("size")) p = {pk(*e.enfant("at")), pk(*e.enfant("at")) + pk(*e.enfant("size"))};
        for (std::size_t i = 1; i < p.size(); ++i) f.fils.push_back({nouvelUuid(), p[i - 1], p[i]});
      } else if (n == "sheet" && e.enfant("at")) {
        BlocFeuille b;
        b.uuid = e.enfant("uuid") ? e.enfant("uuid")->valeur(1) : nouvelUuid();
        b.position = pk(*e.enfant("at"));
        if (const Expr* sz = e.enfant("size")) {
          b.largeur = nm(sz->nombre(1, 20));
          b.hauteur = nm(sz->nombre(2, 15));
        }
        b.nom = propriete(e, "Sheetname");
        if (b.nom.empty()) b.nom = propriete(e, "Sheet name");
        std::string fichier = propriete(e, "Sheetfile");
        if (fichier.empty()) fichier = propriete(e, "Sheet file");
        for (const auto& p : e.elements) {
          if (p.nom() != "pin" || !p.enfant("at")) continue;
          PortBloc port;
          port.uuid = p.enfant("uuid") ? p.enfant("uuid")->valeur(1) : nouvelUuid();
          port.nom = p.valeur(1);
          port.sens = sensDepuisForme(p.valeur(2));
          port.p = pk(*p.enfant("at"));
          const Nm g = std::abs(port.p.x - b.position.x), d = std::abs(port.p.x - (b.position.x + b.largeur)), h = std::abs(port.p.y - b.position.y), bas = std::abs(port.p.y - (b.position.y + b.hauteur));
          const Nm m = std::min({g, d, h, bas});
          port.cote = m == g ? 0 : m == d ? 1 : m == h ? 2 : 3;
          b.ports.push_back(port);
        }
        // La feuille fille, lue de son fichier
        Feuille fille;
        fille.uuid = nouvelUuid();
        fille.nom = b.nom.empty() ? fichier : b.nom;
        b.feuille = fille.uuid;
        c.feuilles.push_back(fille);
        const std::size_t indiceFille = c.feuilles.size() - 1;
        c.feuilles[indice].blocs.push_back(b);
        ++r.blocs;
        const auto contenu = lire ? lire(fichier) : std::nullopt;
        if (!contenu) {
          r.alertes.push_back("Feuille « " + fichier + " » introuvable : son bloc reste, vide");
          continue;
        }
        if (profondeur > 16) {
          r.alertes.push_back("Hiérarchie de plus de 16 niveaux : « " + fichier + " » s'arrête là");
          continue;
        }
        try {
          gardes.push_back(std::make_unique<Expr>(kicad_interne::Lecteur(*contenu).lire()));
          feuille(*gardes.back(), indiceFille, chemin + "/" + b.uuid, lib, profondeur + 1);
        } catch (const std::exception& ex) {
          r.alertes.push_back("Feuille « " + fichier + " » illisible : " + ex.what());
        }
      }
    }
  }
};

// ---------- Écriture ----------
std::string nb(double v) {
  char b[48];
  const auto r = std::to_chars(b, b + sizeof b, v, std::chars_format::fixed, 4);
  std::string s(b, r.ptr);
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s == "-0" || s.empty() ? std::string("0") : s;
}
std::string chaine(const std::string& t) {
  std::string r = "\"";
  for (char ch : t) {
    if (ch == '\n') {
      r += "\\n";
      continue;
    }
    if (ch == '"' || ch == '\\') r += '\\';
    r += ch;
  }
  return r + "\"";
}
std::string pt(Pt p) { return nb(mm(p.x)) + " " + nb(mm(p.y)); }
std::string ptLib(Pt p) { return nb(mm(p.x)) + " " + nb(-mm(p.y)); }
std::string effets(bool cache = false) { return std::string("(effects (font (size 1.27 1.27))") + (cache ? " hide" : "") + ")"; }
std::string nomLib(const std::string& id) {
  // « kicad:Device:R » → « Device:R » ; un symbole de Prométhée → « Promethee:<id> »
  if (id.rfind("kicad:", 0) == 0) return id.substr(6);
  std::string s = id;
  for (char& ch : s)
    if (ch == ':' || ch == ' ' || ch == '/') ch = '_';
  return "Promethee:" + s;
}
std::string sousNom(const std::string& lib) { return lib.substr(lib.find(':') == std::string::npos ? 0 : lib.find(':') + 1); }

std::string symboleBibliotheque(const DefSymbole& d) {
  const std::string lib = nomLib(d.id), court = sousNom(lib);
  std::string o = "    (symbol " + chaine(lib) + (d.alimentation ? " (power)" : "") + " (in_bom " + (d.alimentation ? "no" : "yes") + ") (on_board " + (d.alimentation ? "no" : "yes") + ")\n";
  o += "      (property \"Reference\" " + chaine(d.alimentation ? "#PWR" : d.prefixe) + " (at 0 0 0) " + effets() + ")\n";
  o += "      (property \"Value\" " + chaine(d.nom) + " (at 0 0 0) " + effets() + ")\n";
  const auto fp = d.proprietes.find("empreinteKicad");
  o += "      (property \"Footprint\" " + chaine(fp != d.proprietes.end() ? fp->second : d.empreinte) + " (at 0 0 0) " + effets(true) + ")\n";
  o += "      (property \"Datasheet\" " + chaine(d.fiche.empty() ? "~" : d.fiche) + " (at 0 0 0) " + effets(true) + ")\n";
  const int unites = std::max(1, d.unites);
  for (int u = 0; u <= unites; ++u) {
    std::string corps;
    for (const auto& t : d.traces) {
      if (t.unite != u || t.vue == 2) continue;
      const std::string remplissage = std::string("(fill (type ") + (t.fond ? "background" : t.rempli ? "outline" : "none") + "))";
      const std::string trait = "(stroke (width " + nb(mm(t.epaisseur)) + ") (type default))";
      switch (t.forme) {
        case Trace::Forme::Rectangle:
          if (t.points.size() >= 2) corps += "        (rectangle (start " + ptLib(t.points[0]) + ") (end " + ptLib(t.points[1]) + ") " + trait + " " + remplissage + ")\n";
          break;
        case Trace::Forme::Cercle:
          if (!t.points.empty()) corps += "        (circle (center " + ptLib(t.points[0]) + ") (radius " + nb(mm(t.rayon)) + ") " + trait + " " + remplissage + ")\n";
          break;
        case Trace::Forme::Arc: {
          if (t.points.empty()) break;
          const auto pts = contourTrace(t, 2);
          if (pts.size() >= 3) corps += "        (arc (start " + ptLib(pts.front()) + ") (mid " + ptLib(pts[pts.size() / 2]) + ") (end " + ptLib(pts.back()) + ") " + trait + " " + remplissage + ")\n";
          break;
        }
        case Trace::Forme::Texte:
          if (!t.points.empty()) corps += "        (text " + chaine(t.texte) + " (at " + ptLib(t.points[0]) + " 0) " + effets() + ")\n";
          break;
        default: {
          const auto pts = contourTrace(t, 16);
          if (pts.size() < 2) break;
          corps += "        (polyline (pts";
          for (const Pt& p : pts) corps += " (xy " + ptLib(p) + ")";
          if (t.ferme && !(pts.front() == pts.back())) corps += " (xy " + ptLib(pts.front()) + ")";
          corps += ") " + trait + " " + remplissage + ")\n";
        }
      }
    }
    for (const auto& b : d.broches) {
      if (b.unite != u || b.vue == 2) continue;
      static const int angle[4] = {0, 270, 180, 90};
      corps += "        (pin " + std::string(typeVersKicad(b.type)) + " " + styleVersKicad(b.style) + " (at " + ptLib(b.extremite) + " " + std::to_string(angle[((b.direction % 4) + 4) % 4]) + ") (length " +
               nb(mm(b.longueur)) + ")" + (b.cachee ? " hide" : "") + "\n          (name " + chaine(b.nom.empty() ? "~" : b.nom) + " " + effets() + ")\n          (number " + chaine(b.numero) + " " + effets() + "))\n";
    }
    if (!corps.empty() || u == 1) o += "      (symbol " + chaine(court + "_" + std::to_string(u) + "_1") + "\n" + corps + "      )\n";
  }
  return o + "    )\n";
}
}  // namespace

ConceptionElec conceptionDepuisSchemaKicad(const std::string& racine, const std::string& nom, const std::function<std::optional<std::string>(const std::string&)>& lire,
                                           RapportImportSchemaKicad* rapport) {
  RapportImportSchemaKicad r;
  const Expr arbre = kicad_interne::Lecteur(racine).lire();
  if (arbre.nom() != "kicad_sch") throw std::runtime_error("Ce fichier n’est pas un schéma KiCad (.kicad_sch).");
  ConceptionElec c = nouvelleConception(nom);
  c.feuilles.front().nom = nom;
  Importeur im{c, r, lire, {}, {}, {}, {}};
  im.racineUuid = arbre.enfant("uuid") ? arbre.enfant("uuid")->valeur(1) : std::string();
  // KiCad 6 : table des repères par chemin, à la racine
  if (const Expr* si = arbre.enfant("symbol_instances"))
    for (const auto& p : si->elements)
      if (p.nom() == "path") {
        const Expr* re = p.enfant("reference");
        const Expr* un = p.enfant("unit");
        im.instances6[p.valeur(1)] = {re ? re->valeur(1) : std::string(), un ? static_cast<int>(un->nombre(1, 1)) : 1};
      }
  if (const Expr* tb = arbre.enfant("title_block")) {
    if (const Expr* t = tb->enfant("title")) c.cartouche.titre = t->valeur(1);
    if (const Expr* t = tb->enfant("rev")) c.cartouche.revision = t->valeur(1);
    if (const Expr* t = tb->enfant("company")) c.cartouche.societe = t->valeur(1);
    if (const Expr* t = tb->enfant("date")) c.cartouche.date = t->valeur(1);
  }
  LectureSymboles lib;
  lib.rapport = &r;
  im.feuille(arbre, 0, "/" + im.racineUuid, lib, 0);
  // Les composants reçoivent l'empreinte du catalogue quand KiCad en nomme une connue
  if (rapport) *rapport = r;
  return c;
}

std::map<std::string, std::string> schemaKicadDepuisConception(const ConceptionElec& c, const std::string& nomProjet) {
  std::map<std::string, std::string> fichiers;
  if (c.feuilles.empty()) return fichiers;
  const std::string base = nomFichierAscii(nomProjet, "schema");
  // Feuilles filles : celles que désigne un bloc ; les autres feuilles de premier niveau deviennent des blocs de la racine
  std::set<std::string> filles;
  for (const auto& f : c.feuilles)
    for (const auto& b : f.blocs) filles.insert(b.feuille);
  std::vector<std::size_t> premiers;
  for (std::size_t i = 0; i < c.feuilles.size(); ++i)
    if (!filles.count(c.feuilles[i].uuid)) premiers.push_back(i);
  if (premiers.empty()) premiers.push_back(0);
  const std::size_t racine = premiers.front();
  std::map<std::string, std::string> fichierDe;  // UUID de feuille → nom de fichier
  for (std::size_t i = 0; i < c.feuilles.size(); ++i)
    fichierDe[c.feuilles[i].uuid] = i == racine ? base + ".kicad_sch" : base + "-" + std::to_string(i + 1) + "-" + nomFichierAscii(c.feuilles[i].nom, "feuille") + ".kicad_sch";
  const std::string uuidRacine = c.feuilles[racine].uuid.size() == 36 ? c.feuilles[racine].uuid : uuidDerive("racine:" + c.uuid);
  // Chemin d'instance de chaque feuille (« /racine/bloc/… »)
  std::map<std::string, std::string> chemins;
  chemins[c.feuilles[racine].uuid] = "/" + uuidRacine;
  std::map<std::string, std::string> blocDeRacine;  // feuilles de premier niveau supplémentaires → UUID de leur bloc
  for (std::size_t k = 1; k < premiers.size(); ++k) {
    blocDeRacine[c.feuilles[premiers[k]].uuid] = uuidDerive("bloc:" + c.feuilles[premiers[k]].uuid);
    chemins[c.feuilles[premiers[k]].uuid] = "/" + uuidRacine + "/" + blocDeRacine[c.feuilles[premiers[k]].uuid];
  }
  for (bool change = true; change;) {
    change = false;
    for (const auto& f : c.feuilles) {
      const auto it = chemins.find(f.uuid);
      if (it == chemins.end()) continue;
      for (const auto& b : f.blocs)
        if (!chemins.count(b.feuille)) {
          chemins[b.feuille] = it->second + "/" + b.uuid;
          change = true;
        }
    }
  }
  for (std::size_t i = 0; i < c.feuilles.size(); ++i) {
    const Feuille& f = c.feuilles[i];
    const std::string uuidF = i == racine ? uuidRacine : (f.uuid.size() == 36 ? f.uuid : uuidDerive("feuille:" + f.uuid));
    std::string o = "(kicad_sch (version 20230121) (generator promethee)\n  (uuid " + uuidF + ")\n  (paper " + chaine(f.format == "A3" ? "A3" : "A4") + ")\n";
    if (i == racine)
      o += "  (title_block (title " + chaine(c.cartouche.titre.empty() ? c.nom : c.cartouche.titre) + ") (rev " + chaine(c.cartouche.revision) + ")" +
           (c.cartouche.societe.empty() ? std::string() : " (company " + chaine(c.cartouche.societe) + ")") + ")\n";
    // Symboles de bibliothèque de la feuille
    std::set<std::string> utilises;
    for (const auto& s : f.symboles) utilises.insert(s.symbole);
    o += "  (lib_symbols\n";
    for (const auto& id : utilises)
      if (const DefSymbole* d = c.symbole(id)) o += symboleBibliotheque(*d);
    o += "  )\n";
    for (const auto& j : f.jonctions) o += "  (junction (at " + pt(j.p) + ") (diameter 0) (color 0 0 0 0) (uuid " + (j.uuid.size() == 36 ? j.uuid : uuidDerive(j.uuid)) + "))\n";
    for (const auto& n : f.nonConnexions) o += "  (no_connect (at " + pt(n.p) + ") (uuid " + (n.uuid.size() == 36 ? n.uuid : uuidDerive(n.uuid)) + "))\n";
    for (const auto& w : f.fils) o += "  (wire (pts (xy " + pt(w.a) + ") (xy " + pt(w.b) + ")) (stroke (width 0) (type default)) (uuid " + (w.uuid.size() == 36 ? w.uuid : uuidDerive(w.uuid)) + "))\n";
    for (const auto& e : f.etiquettes) {
      if (e.type == TypeEtiquette::Directive) continue;
      const std::string genre = e.type == TypeEtiquette::Locale ? "label" : e.type == TypeEtiquette::Hierarchique ? "hierarchical_label" : "global_label";
      o += "  (" + genre + " " + chaine(e.texte) + (genre == "label" ? std::string() : std::string(" (shape ") + formeDepuisSens(e.sens) + ")") + " (at " + pt(e.p) + " " + std::to_string(e.rotation * 90) +
           ") " + effets() + " (uuid " + (e.uuid.size() == 36 ? e.uuid : uuidDerive(e.uuid)) + "))\n";
    }
    for (const auto& t : f.textes) o += "  (text " + chaine(t.texte) + " (at " + pt(t.p) + " " + std::to_string(t.rotation * 90) + ") " + effets() + " (uuid " + (t.uuid.size() == 36 ? t.uuid : uuidDerive(t.uuid)) + "))\n";
    for (const auto& s : f.symboles) {
      const DefSymbole* d = c.symbole(s.symbole);
      if (!d) continue;
      const ComposantElec* x = s.composant.empty() ? nullptr : c.composant(s.composant);
      const std::string ref = d->alimentation ? "#PWR" + std::to_string(std::hash<std::string>{}(s.uuid) % 1000) : (x ? x->reference : d->prefixe + "?");
      const std::string val = d->alimentation ? s.nomAlim : (x ? x->valeur : d->nom);
      std::string fp;
      if (x) {
        if (const auto it = x->champs.find("Footprint"); it != x->champs.end()) fp = it->second;
        else if (!x->empreinte.empty()) fp = x->empreinte.rfind("kicad:", 0) == 0 ? x->empreinte.substr(6) : "Promethee:" + x->empreinte;
      }
      o += "  (symbol (lib_id " + chaine(nomLib(d->id)) + ") (at " + pt(s.position) + " " + std::to_string(s.rotation * 90) + ")" + (s.miroir ? " (mirror y)" : "") + " (unit " + std::to_string(std::max(1, s.unite)) +
           ")\n    (in_bom " + (x && x->horsNomenclature ? "no" : "yes") + ") (on_board " + (x && x->horsCarte ? "no" : "yes") + ") (dnp " + (x && x->nePasMonter ? "yes" : "no") + ")\n    (uuid " +
           (s.uuid.size() == 36 ? s.uuid : uuidDerive(s.uuid)) + ")\n";
      o += "    (property \"Reference\" " + chaine(ref) + " (at " + pt(s.position + Pt{2'540'000, -2'540'000}) + " 0) " + effets(d->alimentation) + ")\n";
      o += "    (property \"Value\" " + chaine(val) + " (at " + pt(s.position + Pt{2'540'000, 2'540'000}) + " 0) " + effets() + ")\n";
      o += "    (property \"Footprint\" " + chaine(fp) + " (at " + pt(s.position) + " 0) " + effets(true) + ")\n";
      o += "    (property \"Datasheet\" " + chaine(x && !x->fiche.empty() ? x->fiche : "~") + " (at " + pt(s.position) + " 0) " + effets(true) + ")\n";
      if (x) {
        // Référence et nom du fabricant (10.19) : relues par KiCad et par Prométhée sous ces noms
        if (!x->mpn.empty() && !x->champs.count("MPN")) o += "    (property \"MPN\" " + chaine(x->mpn) + " (at " + pt(s.position) + " 0) " + effets(true) + ")\n";
        if (!x->fabricant.empty() && !x->champs.count("Manufacturer"))
          o += "    (property \"Manufacturer\" " + chaine(x->fabricant) + " (at " + pt(s.position) + " 0) " + effets(true) + ")\n";
        for (const auto& [k, v] : x->champs)
          if (k != "Footprint") o += "    (property " + chaine(k) + " " + chaine(v) + " (at " + pt(s.position) + " 0) " + effets(true) + ")\n";
      }
      o += "    (instances (project " + chaine(nomProjet) + " (path " + chaine(chemins.count(f.uuid) ? chemins[f.uuid] : "/" + uuidRacine) + " (reference " + chaine(ref) + ") (unit " +
           std::to_string(std::max(1, s.unite)) + "))))\n  )\n";
    }
    auto bloc = [&](const std::string& uuid, const std::string& nomBloc, const std::string& fichier, Pt p, Nm l, Nm h, const std::vector<PortBloc>& ports, int page) {
      o += "  (sheet (at " + pt(p) + ") (size " + nb(mm(l)) + " " + nb(mm(h)) + ") (fields_autoplaced) (stroke (width 0.1524) (type solid)) (fill (color 0 0 0 0.0000))\n    (uuid " + uuid + ")\n";
      o += "    (property \"Sheetname\" " + chaine(nomBloc) + " (at " + pt(p - Pt{0, 700'000}) + " 0) " + effets() + ")\n";
      o += "    (property \"Sheetfile\" " + chaine(fichier) + " (at " + pt(p + Pt{0, h + 700'000}) + " 0) " + effets() + ")\n";
      for (const auto& port : ports) {
        static const int angle[4] = {180, 0, 90, 270};
        o += "    (pin " + chaine(port.nom) + " " + formeDepuisSens(port.sens) + " (at " + pt(port.p) + " " + std::to_string(angle[((port.cote % 4) + 4) % 4]) + ") " + effets() + " (uuid " +
             (port.uuid.size() == 36 ? port.uuid : uuidDerive(port.uuid)) + "))\n";
      }
      o += "    (instances (project " + chaine(nomProjet) + " (path " + chaine(chemins.count(f.uuid) ? chemins[f.uuid] : "/" + uuidRacine) + " (page " + chaine(std::to_string(page)) + "))))\n  )\n";
    };
    int page = 2;
    for (const auto& b : f.blocs) {
      const auto it = fichierDe.find(b.feuille);
      if (it == fichierDe.end()) continue;
      bloc(b.uuid.size() == 36 ? b.uuid : uuidDerive(b.uuid), b.nom, it->second, b.position, b.largeur, b.hauteur, b.ports, page++);
    }
    if (i == racine)
      for (std::size_t k = 1; k < premiers.size(); ++k) {
        const Feuille& g = c.feuilles[premiers[k]];
        bloc(blocDeRacine[g.uuid], g.nom, fichierDe[g.uuid], {nm(20 + 50.0 * static_cast<double>(k - 1)), nm(260)}, nm(40), nm(15), {}, page++);
      }
    if (i == racine) o += "  (sheet_instances (path \"/\" (page \"1\")))\n";
    o += ")\n";
    fichiers[fichierDe[f.uuid]] = o;
  }
  return fichiers;
}

}  // namespace prom::ecad
