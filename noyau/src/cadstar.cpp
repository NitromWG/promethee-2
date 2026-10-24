// Prométhée : archives CADSTAR de Zuken (10.17 ; ECH-21) : carte (.cpa, « CADSTARPCB ») et schéma (.csa,
// « CADSTARSCM »), d'après le format que lit l'importeur CADSTAR de KiCad (GPL), servant de référence, sans en reprendre
// le code. Carte : empreintes de la bibliothèque (pastilles et leurs codes), composants posés, réseaux de leurs broches,
// pistes (arcs compris), vias, contour. Schéma : composants et réseaux de leurs bornes. Voir formats_cao.hpp. Licence
// GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>

#include "cao_interne.hpp"
#include "kicad_interne.hpp"
#include "promethee/formats_cao.hpp"

namespace prom::ecad {

namespace {
using kicad_interne::Expr;

struct LecteurCadstar {
  double nm = 10;  // résolution : centième de micron
  std::map<std::string, Pastille> codesPastilles;
  std::map<std::string, std::pair<Nm, Nm>> codesVias;  // diamètre, perçage
  std::map<std::string, Nm> codesRoutes;
  std::map<std::string, int> couchesElec;               // couche → rang électrique (1 dessus)
  int nombreElec = 2;

  Nm l(double v) const { return static_cast<Nm>(std::llround(v * nm)); }
  Pt pt(const Expr& e) const { return {l(e.nombre(1)), -l(e.nombre(2))}; }
  Couche couche(const std::string& id) const {
    const auto it = couchesElec.find(id);
    const int r = it == couchesElec.end() ? 1 : it->second;
    if (r <= 1) return Couche::CuivreDessus;
    if (r >= nombreElec) return Couche::CuivreDessous;
    return cuivreInterne(std::clamp(r - 1, 1, MAX_CUIVRES - 2));
  }
  void resolution(const Expr& entete) {
    if (const Expr* r = entete.enfant("RESOLUTION"))
      for (const auto& e : r->elements)
        if (e.liste && e.nom() == "METRIC") {
          const std::string a = e.valeur(1), b = e.valeur(2);
          nm = a == "HUNDREDTH" ? 10 : a == "TENTH" ? 100 : a == "MICRON" ? 1000 : b == "MICRON" ? 1000 : 10;
        }
  }
  Pastille formeDe(const Expr& e) const {
    Pastille p;
    const std::string n = e.nom();
    const double taille = e.nombre(1);
    p.largeur = p.hauteur = l(taille);
    if (n == "ROUND" || n == "ANNULUS") p.forme = FormePastille::Rond;
    else if (n == "SQUARE" || n == "DIAMOND") p.forme = FormePastille::Rect;
    else if (n == "OCTAGON") {
      p.forme = FormePastille::Chanfreine;
      p.chanfrein = 0.2929;
    } else if (n == "RECTANGLE" || n == "FINGER" || n == "BULLET" || n == "ROUNDED") {
      p.forme = n == "FINGER" || n == "BULLET" ? FormePastille::Ovale : n == "ROUNDED" ? FormePastille::RectArrondi : FormePastille::Rect;
      // Longueur : la largeur et les deux prolongements (« r125_60 » : 0,60 + 0,325 + 0,325 = 1,25 mm)
      p.largeur = l(taille + e.nombre(2) + e.nombre(3));
      p.hauteur = l(taille);
    }
    if (n == "DIAMOND") p.rotation = 45;
    if (const Expr* o = e.enfant("ORIENT")) p.rotation += o->nombre(1) / 1000;
    return p;
  }
  void definitions(const Expr& affectations) {
    if (const Expr* couches = affectations.enfant("LAYERDEFS")) {
      std::vector<std::pair<int, std::string>> elec;
      for (const auto& c : couches->elements)
        if (c.liste && c.nom() == "LAYER")
          if (const Expr* e = c.enfant("ELEC")) elec.push_back({static_cast<int>(e->nombre(1)), c.valeur(1)});
      std::sort(elec.begin(), elec.end());
      int k = 0;
      for (const auto& [rang, id] : elec) couchesElec[id] = ++k;
      nombreElec = std::max(2, k);
    }
    if (const Expr* codes = affectations.enfant("CODEDEFS"))
      for (const auto& c : codes->elements) {
        if (!c.liste) continue;
        if (c.nom() == "PADCODE") {
          Pastille p;
          for (const auto& x : c.elements) {
            if (!x.liste) continue;
            const std::string n = x.nom();
            if (n == "ROUND" || n == "SQUARE" || n == "RECTANGLE" || n == "OCTAGON" || n == "ANNULUS" || n == "BULLET" || n == "FINGER" || n == "DIAMOND" || n == "ROUNDED") p = formeDe(x);
          }
          if (const Expr* d = c.enfant("DRILL")) {
            p.percage = l(d->nombre(1));
            p.type = d->enfant("NONPLATED") ? TypePastille::NonMetallisee : TypePastille::Traversante;
          }
          if (const Expr* s = c.enfant("DRILLLENGTH"); s && p.percage > 0) p.longueurPercage = l(s->nombre(1));
          codesPastilles[c.valeur(1)] = p;
        } else if (c.nom() == "VIACODE") {
          Nm d = 0, perc = 0;
          for (const auto& x : c.elements)
            if (x.liste && (x.nom() == "ROUND" || x.nom() == "OCTAGON" || x.nom() == "SQUARE")) d = l(x.nombre(1));
          if (const Expr* x = c.enfant("DRILL")) perc = l(x->nombre(1));
          codesVias[c.valeur(1)] = {d, perc};
        } else if (c.nom() == "ROUTECODE")
          codesRoutes[c.valeur(1)] = l(c.nombre(3));
      }
  }
  // Arc (CWARC, ACWARC : centre puis fin ; CWSEMI, ACWSEMI : fin d'un demi-cercle) parti de « debut » : sa fin et son
  // milieu
  std::optional<std::pair<Pt, Pt>> arc(Pt debut, const Expr& x) const {
    const std::string n = x.nom();
    if (n != "CWARC" && n != "ACWARC" && n != "CWSEMI" && n != "ACWSEMI") return std::nullopt;
    std::vector<Pt> pts;
    for (const auto& y : x.elements)
      if (y.liste && y.nom() == "PT") pts.push_back(pt(y));
    if (pts.empty()) return std::nullopt;
    const Pt fin = pts.back();
    const Pt centre = pts.size() >= 2 ? pts.front() : Pt{(debut.x + fin.x) / 2, (debut.y + fin.y) / 2};
    // Sens horaire dans le repère du fichier (y vers le haut) : le sens trigonométrique des angles calculés ici (y vers
    // le bas)
    const bool horaire = n[0] == 'C';
    const double rayon = distance(debut, centre);
    const double a0 = std::atan2(static_cast<double>(debut.y - centre.y), static_cast<double>(debut.x - centre.x));
    double a1 = std::atan2(static_cast<double>(fin.y - centre.y), static_cast<double>(fin.x - centre.x));
    if (horaire)
      while (a1 <= a0) a1 += 2 * std::numbers::pi;
    else
      while (a1 >= a0) a1 -= 2 * std::numbers::pi;
    const double am = (a0 + a1) / 2;
    return std::pair{fin, Pt{centre.x + static_cast<Nm>(std::llround(rayon * std::cos(am))), centre.y + static_cast<Nm>(std::llround(rayon * std::sin(am)))}};
  }
  // Suite de points avec arcs : le point, et le milieu de l'arc qui y mène
  std::vector<std::pair<Pt, std::optional<Pt>>> chemin(const Expr& e) const {
    std::vector<std::pair<Pt, std::optional<Pt>>> r;
    for (const auto& x : e.elements) {
      if (!x.liste) continue;
      if (x.nom() == "PT") r.push_back({pt(x), std::nullopt});
      else if (!r.empty())
        if (const auto a = arc(r.back().first, x)) r.push_back({a->first, a->second});
    }
    return r;
  }
  Polygone contour(const Expr& e) const {
    Polygone p;
    for (const auto& [q, milieu] : chemin(e)) {
      if (milieu && !p.empty()) {
        // Arc découpé en quatre cordes par le milieu
        const Pt a = p.back();
        p.push_back({(a.x + milieu->x) / 2, (a.y + milieu->y) / 2});
        p.push_back(*milieu);
        p.push_back({(milieu->x + q.x) / 2, (milieu->y + q.y) / 2});
      }
      if (p.empty() || !(p.back() == q)) p.push_back(q);
    }
    if (p.size() > 1 && p.front() == p.back()) p.pop_back();
    return p;
  }
};

ConceptionElec carteCadstar(const Expr& racine, const std::string& nom, RapportLectureCao& r) {
  LecteurCadstar L;
  if (const Expr* h = racine.enfant("HEADER")) L.resolution(*h);
  if (const Expr* a = racine.enfant("ASSIGNMENTS")) L.definitions(*a);
  cao_interne::CarteLue lue;
  lue.cuivres = L.nombreElec;
  // Bibliothèque : une empreinte par définition, les pastilles dans le repère de son origine
  struct Def {
    std::string nom;
    std::map<std::string, std::string> numeros;  // identifiant de pastille → numéro affiché
  };
  std::map<std::string, Def> defs;
  if (const Expr* lib = racine.enfant("LIBRARY"))
    for (const auto& s : lib->elements) {
      if (!s.liste || s.nom() != "SYMDEF") continue;
      Def def;
      def.nom = s.valeur(2) + (s.valeur(3).empty() ? std::string() : "_" + s.valeur(3));
      Pt origine;
      if (const Expr* o = s.enfant("PT")) origine = L.pt(*o);
      DefEmpreinte d;
      d.description = s.valeur(2);
      for (const auto& p : s.elements) {
        if (!p.liste) continue;
        if (p.nom() == "PAD") {
          const std::string id = p.valeur(1);
          const auto code = L.codesPastilles.find(p.valeur(3));
          Pastille q = code == L.codesPastilles.end() ? Pastille{} : code->second;
          if (const Expr* o = p.enfant("PT")) q.position = L.pt(*o) - origine;
          if (const Expr* o = p.enfant("ORIENT")) q.rotation += o->nombre(1) / 1000;
          q.rotation = std::fmod(std::fmod(q.rotation, 360) + 360, 360);
          const std::string cote = p.valeur(4);
          if (q.percage == 0) {
            q.type = TypePastille::CMS;
            if (cote == "MAXIMUM" || cote == "BOTTOM") q.couches = bit(Couche::CuivreDessous) | bit(Couche::VernisDessous) | bit(Couche::PateDessous);
          }
          std::string numero = id;
          if (const Expr* n = p.enfant("PADIDENTIFIER")) numero = n->valeur(1);
          q.numero = numero;
          def.numeros[id] = numero;
          d.pastilles.push_back(q);
        } else if (p.nom() == "FIGURE") {
          // Sérigraphie et encombrement : les contours ouverts et fermés
          for (const auto& f : p.elements)
            if (f.liste && (f.nom() == "OUTLINE" || f.nom() == "OPENSHAPE")) {
              const Polygone c = L.contour(f);
              for (std::size_t k = 0; k + 1 < c.size(); ++k) {
                TraitEmpreinte t;
                t.a = c[k] - origine;
                t.b = c[k + 1] - origine;
                d.traits.push_back(t);
              }
            }
        }
      }
      // Cuivre de l'empreinte relié à des pastilles (pastille thermique) : une pastille de forme libre au numéro de la
      // première, les pastilles associées renumérotées comme elle
      for (const auto& cu : s.elements) {
        if (!cu.liste || cu.nom() != "COMPCOPPER") continue;
        const Expr* plein = cu.enfant("SOLID");
        const Expr* assoc = cu.enfant("ASSOCPIN");
        if (!plein || !assoc || assoc->elements.size() < 2) continue;
        const std::string ancre = assoc->valeur(1);
        const auto n = def.numeros.find(ancre);
        if (n == def.numeros.end()) continue;
        const std::string numero = n->second;
        Pt pos;
        for (const auto& q : d.pastilles)
          if (q.numero == numero) pos = q.position;
        Pastille q;
        q.numero = numero;
        q.type = TypePastille::CMS;
        q.forme = FormePastille::Libre;
        q.position = pos;
        Boite b;
        for (const Pt& c : L.contour(*plein)) {
          q.contourLibre.push_back(c - origine - pos);
          b.etendre(c - origine - pos);
        }
        q.largeur = std::max<Nm>(b.largeur(), 1);
        q.hauteur = std::max<Nm>(b.hauteur(), 1);
        q.couches = bit(L.couche(cu.valeur(2)));
        for (std::size_t k = 1; k < assoc->elements.size(); ++k) {
          const auto m = def.numeros.find(assoc->valeur(k));
          if (m == def.numeros.end()) continue;
          for (auto& x : d.pastilles)
            if (x.numero == m->second) x.numero = numero;
          m->second = numero;
        }
        if (q.contourLibre.size() >= 3) d.pastilles.push_back(q);
      }
      lue.empreintes[def.nom] = d;
      defs[s.valeur(1)] = def;
    }
  std::map<std::string, std::string> nomsParts;
  if (const Expr* parts = racine.enfant("PARTS"))
    for (const auto& p : parts->elements)
      if (p.liste && p.nom() == "PART") nomsParts[p.valeur(1)] = p.valeur(2);
  const Expr* plan = racine.enfant("LAYOUT");
  if (!plan) {
    r.alertes.push_back("Archive CADSTAR sans carte (section LAYOUT)");
    return cao_interne::assemblerCarte(lue, nom, "cadstar:", r);
  }
  std::map<std::string, std::pair<std::string, std::string>> composants;  // CMPx → (repère, définition)
  for (const auto& c : plan->elements) {
    if (!c.liste) continue;
    if (c.nom() == "BOARD") {
      if (const Expr* o = c.enfant("OUTLINE")) lue.contour = L.contour(*o);
    } else if (c.nom() == "COMP") {
      const std::string ref = c.valeur(2), part = c.valeur(3), sym = c.valeur(4);
      const auto d = defs.find(sym);
      if (d == defs.end()) {
        r.alertes.push_back(ref + " : définition « " + sym + " » absente de la bibliothèque");
        continue;
      }
      composants[c.valeur(1)] = {ref, sym};
      cao_interne::CarteLue::Pose pose;
      pose.ref = ref;
      pose.decal = d->second.nom;
      if (const Expr* o = c.enfant("PT")) pose.p = L.pt(*o);
      double angle = 0;
      if (const Expr* o = c.enfant("ORIENT")) angle = o->nombre(1) / 1000;
      pose.dessous = c.enfant("MIRROR") != nullptr;
      pose.rotation = pose.dessous ? -angle : angle;
      lue.poses.push_back(pose);
      lue.netlist.composants.push_back({ref, nomsParts.count(part) ? nomsParts[part] : std::string(), d->second.nom});
    } else if (c.nom() == "NET") {
      std::string reseau;
      if (const Expr* s = c.enfant("SIGNAME")) reseau = s->valeur(1);
      if (reseau.empty()) {
        const Expr* num = c.enfant("SIGNUM");
        reseau = "N" + (num ? num->valeur(1) : c.valeur(1));
      }
      for (const auto& x : c.elements) {
        if (!x.liste) continue;
        if (x.nom() == "PIN") {
          const auto k = composants.find(x.valeur(2));
          if (k == composants.end()) continue;
          const auto& def = defs[k->second.second];
          const auto n = def.numeros.find(x.valeur(3));
          lue.netlist.reseaux[reseau].push_back({k->second.first, n == def.numeros.end() ? x.valeur(3) : n->second});
        } else if (x.nom() == "VIA") {
          Via v;
          v.reseau = reseau;
          const auto code = L.codesVias.find(x.valeur(2));
          if (code != L.codesVias.end()) v.diametre = code->second.first, v.percage = code->second.second;
          if (const Expr* o = x.enfant("PT")) v.p = L.pt(*o);
          lue.vias.push_back(v);
        } else if (x.nom() == "CONN") {
          const Nm defaut = L.codesRoutes.count(x.valeur(3)) ? L.codesRoutes[x.valeur(3)] : 250'000;
          for (const auto& route : x.elements) {
            if (!route.liste || route.nom() != "ROUTE") continue;
            const Couche k = L.couche(route.valeur(1));
            Nm largeur = defaut;
            std::optional<Pt> avant;
            for (const auto& y : route.elements) {
              if (!y.liste) continue;
              const std::string n = y.nom();
              if (n == "ROUTEWIDTH") {
                largeur = L.l(y.nombre(1));
                continue;
              }
              if (n == "PT") {
                const Pt q = L.pt(y);
                if (avant && !(*avant == q)) {
                  Piste p;
                  p.reseau = reseau;
                  p.couche = k;
                  p.a = *avant;
                  p.b = q;
                  p.largeur = std::max<Nm>(largeur, 50'000);
                  lue.pistes.push_back(p);
                }
                avant = q;
              } else if (avant) {
                if (const auto a = L.arc(*avant, y)) {
                  Piste p;
                  p.reseau = reseau;
                  p.couche = k;
                  p.a = *avant;
                  p.b = a->first;
                  p.milieu = a->second;
                  p.largeur = std::max<Nm>(largeur, 50'000);
                  if (!(p.a == p.b)) lue.pistes.push_back(p);
                  avant = a->first;
                }
              }
            }
          }
        }
      }
    }
  }
  return cao_interne::assemblerCarte(lue, nom, "cadstar:", r);
}

// Schéma : composants des symboles, réseaux de leurs bornes (numéro de broche du symbole s'il est donné)
ConceptionElec schemaCadstar(const Expr& racine, const std::string& nom, RapportLectureCao& r) {
  NetlistLue n;
  std::map<std::string, std::string> refDuSymbole;                 // SYMx → repère
  std::map<std::string, std::map<std::string, std::string>> pins;  // SYMx → borne → broche
  std::map<std::string, std::string> nomsParts, partDuRepere;
  if (const Expr* parts = racine.enfant("PARTS"))
    for (const auto& p : parts->elements)
      if (p.liste && p.nom() == "PART") nomsParts[p.valeur(1)] = p.valeur(2);
  const Expr* schema = racine.enfant("SCHEMATIC");
  if (!schema) {
    r.alertes.push_back("Archive CADSTAR sans schéma (section SCHEMATIC)");
    return conceptionDepuisNetlist(n, nom);
  }
  std::set<std::string> vus;
  for (const auto& s : schema->elements) {
    if (!s.liste || s.nom() != "SYMBOL") continue;
    const Expr* comp = s.enfant("COMP");
    if (!comp) continue;  // symbole graphique (masse, renvoi) : pas un composant
    const std::string ref = comp->valeur(1);
    refDuSymbole[s.valeur(1)] = ref;
    if (const Expr* pr = s.enfant("PARTREF")) partDuRepere[ref] = pr->valeur(1);
    for (const auto& x : s.elements)
      if (x.liste && x.nom() == "PINNUM") pins[s.valeur(1)][x.valeur(1)] = x.valeur(2);
    if (vus.insert(ref).second) n.composants.push_back({ref, {}, {}});
  }
  for (auto& c : n.composants)
    if (partDuRepere.count(c.reference)) c.valeur = nomsParts[partDuRepere[c.reference]];
  for (const auto& e : schema->elements) {
    if (!e.liste || e.nom() != "NET") continue;
    std::string reseau;
    if (const Expr* s = e.enfant("SIGNAME")) reseau = s->valeur(1);
    if (reseau.empty()) reseau = e.valeur(1);
    for (const auto& t : e.elements) {
      if (!t.liste || t.nom() != "TERM") continue;
      const auto ref = refDuSymbole.find(t.valeur(2));
      if (ref == refDuSymbole.end()) continue;
      const auto& p = pins[t.valeur(2)];
      const auto b = p.find(t.valeur(3));
      n.reseaux[reseau].push_back({ref->second, b == p.end() ? t.valeur(3) : b->second});
    }
  }
  r.composants = static_cast<int>(n.composants.size());
  r.reseaux = static_cast<int>(n.reseaux.size());
  ConceptionElec c = conceptionDepuisNetlist(n, nom);
  c.schemaSeul = true;
  return c;
}
}  // namespace

bool estArchiveCadstar(const std::string& t) {
  const auto p = t.find_first_not_of(" \t\r\n");
  return p != std::string::npos && (t.compare(p, 11, "(CADSTARPCB") == 0 || t.compare(p, 11, "(CADSTARSCM") == 0);
}

ConceptionElec conceptionDepuisCadstar(const std::string& texte, const std::string& nom, RapportLectureCao* rapport) {
  RapportLectureCao r;
  kicad_interne::Lecteur lecteur(texte);
  const Expr racine = lecteur.lire();
  ConceptionElec c = racine.nom() == "CADSTARSCM" ? schemaCadstar(racine, nom, r) : carteCadstar(racine, nom, r);
  if (rapport) *rapport = r;
  return c;
}

}  // namespace prom::ecad
