// Prométhée : échanges avec les autres outils de CAO électronique (10.17, 10.18, 10.22). Voir formats_cao.hpp.
// Licence GPL-3.0-only.
#include "promethee/formats_cao.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

#include "promethee/circuit_edition.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/netlists_tierces.hpp"
#include "promethee/police.hpp"
#include "cao_interne.hpp"
#include "kicad_interne.hpp"

namespace prom::ecad {

namespace {
std::string nb(double v, int dec = 4) {
  char t[40];
  std::snprintf(t, sizeof t, "%.*f", dec, std::abs(v) < 1e-12 ? 0.0 : v);
  std::string s = t;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  return s == "-0" ? "0" : s;
}
}  // namespace

bool estPcadAscii(const std::string& t) { return t.rfind("ACCEL_ASCII", 0) == 0 || t.find("(asciiHeader") != std::string::npos; }

// ======================================================================================================================
// PADS ASCII (Layout), millimètres
// ======================================================================================================================
// ======================================================================================================================
// P-CAD ASCII (ACCEL_ASCII)
// ======================================================================================================================
namespace {
int numeroPcad(Couche k, int n) { return k == Couche::CuivreDessus ? 1 : k == Couche::CuivreDessous ? 2 : 11 + rangCuivre(k, n); }
}  // namespace

std::string pcadAsciiCarte(const ConceptionElec& c, RapportEcritureCao* rapport) {
  RapportEcritureCao r;
  const int n = c.circuit.empilage.couchesCuivre;
  auto q = [](const std::string& s) {
    std::string t = "\"";
    for (char ch : s) t += ch == '"' ? '\'' : ch;
    return t + "\"";
  };
  auto pt = [](Pt p) { return "(pt " + nb(mm(p.x)) + " " + nb(-mm(p.y)) + ")"; };
  std::ostringstream o;
  o << "ACCEL_ASCII " << q(c.nom + ".pcb") << "\n\n(asciiHeader\n  (asciiVersion 3 0)\n  (program \"Prométhée\" \"0.34\")\n  (fileUnits mm)\n)\n\n(library \"Library_1\"\n";
  std::set<std::string> vues;
  for (const auto& em : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(em.empreinte);
    if (!d || !vues.insert(em.empreinte).second) continue;
    int i = 0;
    for (const auto& p : d->pastilles) {
      ++i;
      o << "  (padStyleDef " << q(em.empreinte + "_" + std::to_string(i)) << " (holeDiam " << nb(mm(p.percage)) << ")"
        << " (padShape (layerType " << (p.percage > 0 ? "Signal" : "Top") << ") (padShapeType " << (p.forme == FormePastille::Rond ? "Ellipse" : p.forme == FormePastille::Ovale ? "Oval" : "Rect")
        << ") (shapeWidth " << nb(mm(p.largeur)) << ") (shapeHeight " << nb(mm(p.hauteur)) << ")))\n";
    }
    o << "  (patternDefExtended " << q(em.empreinte) << " (originalName " << q(em.empreinte) << ")\n    (patternGraphicsDef (patternGraphicsNameDef \"Primary\")\n      (multiLayer\n";
    i = 0;
    for (const auto& p : d->pastilles) {
      ++i;
      o << "        (pad (padNum " << i << ") (padStyleRef " << q(em.empreinte + "_" + std::to_string(i)) << ") " << pt(p.position) << " (rotation " << nb(p.rotation) << ") (defaultPinDes "
        << q(p.numero) << "))\n";
    }
    o << "      )\n      (layerContents (layerNumRef 6)\n";
    for (const auto& t : d->traits)
      if (t.couche == Couche::SerigraphieDessus && t.rayon == 0) o << "        (line " << pt(t.a) << " " << pt(t.b) << " (width " << nb(mm(t.epaisseur)) << "))\n";
    o << "      )\n    )\n  )\n";
  }
  o << ")\n\n(netlist \"Netlist_1\"\n";
  for (const auto& em : c.circuit.empreintes)
    if (const ComposantElec* x = c.composant(em.composant)) {
      o << "  (compInst " << q(x->reference) << " (compRef " << q(em.empreinte) << ") (compValue " << q(x->valeur) << ") (patternName " << q(em.empreinte) << "))\n";
      ++r.composants;
    }
  for (const auto& [nom, noeuds] : netlistDe(c).reseaux) {
    o << "  (net " << q(nom);
    for (const auto& [ref, broche] : noeuds) o << " (node " << q(ref) << " " << q(broche) << ")";
    o << ")\n";
    ++r.reseaux;
  }
  o << ")\n\n(pcbDesign \"PCBDesign_1\"\n";
  // Couches numérotées comme dans P-CAD : Top 1, Bottom 2, Board 3, sérigraphies 6 et 7, internes à partir de 12
  for (Couche k : couchesCuivre(n))
    o << "  (layerDef " << q(k == Couche::CuivreDessus ? "Top" : k == Couche::CuivreDessous ? "Bottom" : "Mid" + std::to_string(rangCuivre(k, n))) << " (layerNum " << numeroPcad(k, n) << ") (layerType Signal))\n";
  o << "  (layerDef \"Board\" (layerNum 3) (layerType NonSignal))\n  (layerDef \"Top Silk\" (layerNum 6) (layerType NonSignal))\n  (multiLayer\n";
  for (const auto& em : c.circuit.empreintes)
    if (const ComposantElec* x = c.composant(em.composant))
      o << "    (pattern (patternRef " << q(em.empreinte) << ") (refDesRef " << q(x->reference) << ") " << pt(em.position) << " (rotation " << nb(em.rotation) << ") (isFlipped "
        << (em.dessous ? "True" : "False") << "))\n";
  for (const auto& v : c.circuit.vias) {
    o << "    (via (viaStyleRef " << q("V_" + nb(mm(v.diametre), 3) + "_" + nb(mm(v.percage), 3)) << ") " << pt(v.p) << " (netNameRef " << q(v.reseau) << ") (diam " << nb(mm(v.diametre)) << ") (holeDiam "
      << nb(mm(v.percage)) << "))\n";
    ++r.vias;
  }
  o << "  )\n";
  for (Couche k : couchesCuivre(n)) {
    o << "  (layerContents (layerNumRef " << numeroPcad(k, n) << ")\n";
    for (const auto& p : c.circuit.pistes) {
      if (p.couche != k) continue;
      for (const auto& [a, b] : segmentsPiste(p, 20'000)) {
        o << "    (line " << pt(a) << " " << pt(b) << " (width " << nb(mm(p.largeur)) << ") (netNameRef " << q(p.reseau) << "))\n";
        ++r.pistes;
      }
    }
    o << "  )\n";
  }
  const Polygone contour = c.circuit.contour.polygone();
  o << "  (layerContents (layerNumRef 3)\n";
  for (std::size_t i = 0; i < contour.size(); ++i) o << "    (line " << pt(contour[i]) << " " << pt(contour[(i + 1) % contour.size()]) << " (width 0.1))\n";
  o << "  )\n)\n";
  for (const auto& z : c.circuit.zones) r.nonEcrits.push_back("zone de cuivre « " + z.reseau + " » sur " + codeCouche(z.couche));
  if (!c.circuit.regles.classes.empty()) r.nonEcrits.push_back("classes de réseaux et règles de conception");
  r.nonEcrits.push_back("empilage détaillé");
  if (rapport) *rapport = r;
  return o.str();
}

std::vector<std::pair<std::string, std::string>> archivePourAltium(const ConceptionElec& c, RapportEcritureCao* rapport) {
  RapportEcritureCao r;
  const std::string base = prom::nomFichierAscii(c.nom, "carte");
  std::vector<std::pair<std::string, std::string>> f;
  f.push_back({base + ".kicad_pcb", kicadDepuisConception(c, connectivite(c))});
  f.push_back({base + ".kicad_pro", projetKicadDepuisConception(c)});
  RapportEcritureCao rp;
  f.push_back({base + ".asc", padsAsciiCarte(c, &rp)});
  f.push_back({base + ".edf", ecrireNetlist(netlistDe(c), FormatNetlist::Edif, c.nom)});
  f.push_back({"LISEZMOI.txt", "Archive écrite par Prométhée pour Altium Designer.\r\n\r\nDans Altium : File > Import Wizard, puis « KiCad Design Files » et le fichier " + base +
                                   ".kicad_pcb (carte, règles du .kicad_pro), ou « PADS ASCII Design And Library Files » et le fichier " + base +
                                   ".asc. La netlist EDIF " + base + ".edf sert à vérifier les réseaux après l'import.\r\n"});
  r = rp;
  r.nonEcrits.insert(r.nonEcrits.begin(), "schéma : passe par la netlist EDIF (dessin des feuilles non repris)");
  if (rapport) *rapport = r;
  return f;
}

// ======================================================================================================================
// Lecture PADS ASCII et P-CAD ASCII
// ======================================================================================================================
ConceptionElec cao_interne::assemblerCarte(CarteLue& l, const std::string& nom, const std::string& prefixe, RapportLectureCao& r) {
  // Pièces de la carte seule : hors du schéma, leurs broches gardent le réseau du cuivre
  if (!l.carteSeule.empty()) {
    std::erase_if(l.netlist.composants, [&](const ComposantNetlist& x) { return l.carteSeule.count(x.reference) > 0; });
    for (auto& [n, v] : l.netlist.reseaux)
      std::erase_if(v, [&l, &n = n](const Broche& b) {
        if (!l.carteSeule.count(b.first)) return false;
        l.reseauxCarteSeule[b] = n;
        return true;
      });
  }
  // Valeurs et empreintes, puis schéma de liaison par la netlist
  for (auto& x : l.netlist.composants) x.empreinte = prefixe + x.empreinte;
  ConceptionElec c = conceptionDepuisNetlist(l.netlist, nom);
  c.circuit.empilage.couchesCuivre = std::clamp(l.cuivres, 2, 32);
  for (auto& [n, d] : l.empreintes) {
    d.id = prefixe + n;
    d.famille = "Importées";
    Boite b;
    for (const auto& p : d.pastilles) b.etendre(boite(formePastille(p)));
    for (const auto& t : d.traits) {
      b.etendre(t.a);
      b.etendre(t.b);
    }
    if (b.vide()) b = Boite{-NM_PAR_MM, -NM_PAR_MM, NM_PAR_MM, NM_PAR_MM};
    d.encombrement = b.gonflee(250'000);
    d.corps.largeur = d.encombrement.largeur();
    d.corps.profondeur = d.encombrement.hauteur();
    d.corps.hauteur = NM_PAR_MM;
    d.corps.decalage = d.encombrement.centre();
    c.empreintes[d.id] = d;
  }
  for (const auto& p : l.poses) {
    ComposantElec* x = nullptr;
    for (auto& y : c.composants)
      if (y.reference == p.ref) x = &y;
    if (!x) {
      // Pièce sans réseau (trou de fixation, logo) : posée sur la carte seule
      ComposantElec n;
      n.uuid = nouvelUuid();
      n.reference = p.ref;
      n.empreinte = prefixe + p.decal;
      n.carteSeule = true;
      c.composants.push_back(n);
      EmpreintePlacee em;
      em.composant = n.uuid;
      em.empreinte = n.empreinte;
      em.position = p.p;
      em.rotation = p.rotation;
      em.dessous = p.dessous;
      c.circuit.empreintes.push_back(em);
      if (const auto d = l.empreintes.find(p.decal); d != l.empreintes.end())
        for (const Pastille& q : d->second.pastilles)
          if (const auto it = l.reseauxCarteSeule.find({p.ref, q.numero}); it != l.reseauxCarteSeule.end()) c.nomsReseaux[n.uuid + ":" + q.numero] = it->second;
      continue;
    }
    x->empreinte = prefixe + p.decal;
    bool pose = false;
    for (auto& em : c.circuit.empreintes)
      if (em.composant == x->uuid) {
        em.empreinte = x->empreinte;
        em.position = p.p;
        em.rotation = p.rotation;
        em.dessous = p.dessous;
        pose = true;
      }
    if (!pose) {
      EmpreintePlacee em;
      em.composant = x->uuid;
      em.empreinte = x->empreinte;
      em.position = p.p;
      em.rotation = p.rotation;
      em.dessous = p.dessous;
      c.circuit.empreintes.push_back(em);
    }
  }
  for (auto& p : l.pistes) {
    p.uuid = nouvelUuid();
    c.circuit.pistes.push_back(p);
  }
  for (auto& v : l.vias) {
    v.uuid = nouvelUuid();
    c.circuit.vias.push_back(v);
  }
  for (auto& x : l.regions) {
    x.uuid = nouvelUuid();
    c.circuit.regions.push_back(x);
  }
  for (auto& x : l.zones) {
    x.uuid = nouvelUuid();
    c.circuit.zones.push_back(x);
  }
  for (auto& x : l.zonesRegles) {
    x.uuid = nouvelUuid();
    c.circuit.zonesRegles.push_back(x);
  }
  for (auto& x : l.dessins) {
    x.uuid = nouvelUuid();
    c.circuit.dessins.push_back(x);
  }
  for (auto& x : l.textes) {
    x.uuid = nouvelUuid();
    c.circuit.textes.push_back(x);
  }
  if (l.contour.size() < 3) {
    // Sans contour dans le fichier : la boîte du cuivre élargie de 2 mm, plutôt que le rectangle d'une carte neuve
    Boite b;
    for (const auto& p : l.poses)
      if (const auto d = l.empreintes.find(p.decal); d != l.empreintes.end()) {
        EmpreintePlacee e;
        e.position = p.p;
        e.rotation = p.rotation;
        e.dessous = p.dessous;
        for (const Pastille& q : d->second.pastilles) b.etendre(boite(formePastille(pastilleSurCarte(e, q))));
      }
    for (const auto& p : l.pistes)
      for (const auto& [a, x] : segmentsPiste(p)) b.etendre(Boite{std::min(a.x, x.x), std::min(a.y, x.y), std::max(a.x, x.x), std::max(a.y, x.y)}.gonflee(p.largeur / 2));
    for (const auto& v : l.vias) b.etendre(Boite{v.p.x, v.p.y, v.p.x, v.p.y}.gonflee(v.diametre / 2));
    for (const auto& g : l.regions) b.etendre(boite(g.forme));
    for (const auto& d : l.dessins)
      for (const Pt& q : d.points) b.etendre(q);
    for (const auto& t : l.textes) {
      const Nm m = std::max(largeurTexte(t.texte, t.hauteur), t.hauteur);
      b.etendre(Boite{t.p.x, t.p.y, t.p.x, t.p.y}.gonflee(m / 2 + t.hauteur));
    }
    if (!b.vide()) {
      b = b.gonflee(2 * NM_PAR_MM);
      l.contour = {{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}};
      r.alertes.push_back("aucun contour de carte dans le fichier : la boîte du cuivre élargie de 2 mm en tient lieu");
    }
  }
  if (l.contour.size() >= 3) c.circuit.contour.libre = orienter(l.contour);
  for (const Polygone& q : l.decoupes)
    if (q.size() >= 3) c.circuit.contour.decoupes.push_back(orienter(q));
  synchroniserReseaux(c);
  r.composants = static_cast<int>(l.poses.size());
  r.reseaux = static_cast<int>(l.netlist.reseaux.size());
  r.pistes = static_cast<int>(l.pistes.size());
  r.vias = static_cast<int>(l.vias.size());
  return c;
}

ConceptionElec conceptionDepuisPcad(const std::string& texte, const std::string& nom, RapportLectureCao* rapport) {
  RapportLectureCao r;
  cao_interne::CarteLue l;
  const auto debut = texte.find('(');
  kicad_interne::Expr racine;
  try {
    racine = kicad_interne::Lecteur("(pcad " + texte.substr(debut == std::string::npos ? 0 : debut) + ")").lire();
  } catch (const std::exception& e) {
    r.alertes.push_back(std::string("P-CAD illisible : ") + e.what());
    if (rapport) *rapport = r;
    return nouvelleConception(nom);
  }
  using kicad_interne::Expr;
  auto pt = [](const Expr* e) { return e ? Pt{nm(e->nombre(1)), -nm(e->nombre(2))} : Pt{}; };
  auto texteDe = [](const Expr& e, std::size_t i) { return e.valeur(i); };
  double unite = 1;  // mm ; mil si fileUnits Mil
  std::map<std::string, std::vector<std::pair<double, double>>> stylesPads;  // largeur, hauteur, perçage
  for (const auto& bloc : racine.elements) {
    if (bloc.nom() == "asciiHeader")
      if (const Expr* u = bloc.enfant("fileUnits"); u && (u->valeur(1) == "Mil" || u->valeur(1) == "mil")) unite = 0.0254;
    if (bloc.nom() == "library")
      for (const auto& e : bloc.elements) {
        if (e.nom() == "padStyleDef") {
          double w = 0, h = 0, trou = 0, forme = 0;
          if (const Expr* hd = e.enfant("holeDiam")) trou = hd->nombre(1);
          if (const Expr* s = e.enfant("padShape")) {
            if (const Expr* x = s->enfant("shapeWidth")) w = x->nombre(1);
            if (const Expr* x = s->enfant("shapeHeight")) h = x->nombre(1);
            if (const Expr* x = s->enfant("padShapeType")) {
              const std::string t = x->valeur(1);
              forme = t == "Ellipse" || t == "Oval" ? 1 : t == "RndRect" || t == "RoundedRect" ? 2 : t == "Polygon" ? 3 : 0;
            }
          }
          stylesPads[texteDe(e, 1)] = {{w * unite, h * unite}, {trou * unite, forme}};
        } else if (e.nom() == "patternDefExtended" || e.nom() == "patternDef") {
          DefEmpreinte& d = l.empreintes[texteDe(e, 1)];
          std::function<void(const Expr&)> parcourir = [&](const Expr& x) {
            if (x.nom() == "pad") {
              Pastille p;
              if (const Expr* n = x.enfant("defaultPinDes")) p.numero = n->valeur(1);
              else if (const Expr* n2 = x.enfant("padNum")) p.numero = n2->valeur(1);
              const Expr* ppt = x.enfant("pt");
              p.position = {nm(ppt ? ppt->nombre(1) * unite : 0), -nm(ppt ? ppt->nombre(2) * unite : 0)};
              if (const Expr* rr = x.enfant("rotation")) p.rotation = rr->nombre(1);
              if (const Expr* st = x.enfant("padStyleRef"); st && stylesPads.count(st->valeur(1))) {
                const auto& s = stylesPads[st->valeur(1)];
                p.largeur = nm(s[0].first);
                p.hauteur = nm(s[0].second);
                p.percage = nm(s[1].first);
                p.type = p.percage > 0 ? TypePastille::Traversante : TypePastille::CMS;
                const int forme = static_cast<int>(s[1].second);
                p.forme = forme == 1 ? (p.largeur == p.hauteur ? FormePastille::Rond : FormePastille::Ovale) : forme == 2 ? FormePastille::RectArrondi : FormePastille::Rect;
                p.arrondi = forme == 2 ? 0.25 : 0;
              }
              d.pastilles.push_back(p);
            } else if (x.nom() == "line") {
              std::vector<const Expr*> pts;
              for (const auto& y : x.elements)
                if (y.nom() == "pt") pts.push_back(&y);
              if (pts.size() >= 2) {
                TraitEmpreinte t;
                t.a = pt(pts[0]);
                t.b = pt(pts[1]);
                if (const Expr* w = x.enfant("width")) t.epaisseur = nm(w->nombre(1) * unite);
                d.traits.push_back(t);
              }
            } else
              for (const auto& y : x.elements)
                if (y.liste) parcourir(y);
          };
          parcourir(e);
        }
      }
    if (bloc.nom() == "netlist")
      for (const auto& e : bloc.elements) {
        if (e.nom() == "compInst") {
          ComposantNetlist x;
          x.reference = texteDe(e, 1);
          if (const Expr* v = e.enfant("compValue")) x.valeur = v->valeur(1);
          if (const Expr* p = e.enfant("patternName")) x.empreinte = p->valeur(1);
          l.netlist.composants.push_back(x);
        } else if (e.nom() == "net") {
          auto& noeuds = l.netlist.reseaux[texteDe(e, 1)];
          for (const auto& n : e.elements)
            if (n.nom() == "node") noeuds.push_back({n.valeur(1), n.valeur(2)});
        }
      }
    if (bloc.nom() == "pcbDesign") {
      // Couches d'après leur définition : Signal ou Plane (Top, Bottom, internes dans l'ordre), Board, sérigraphies ;
      // sans définition, la numérotation de P-CAD (1 Top, 2 Bottom, 3 Board, 6 et 7 sérigraphies)
      enum class Role { Cuivre, Contour, Seri, Autre };
      struct CouchePcad {
        Role role = Role::Autre;
        Couche couche = Couche::CuivreDessus;
      };
      std::map<int, CouchePcad> couches;
      std::vector<std::pair<int, std::string>> internes;
      for (const auto& e : bloc.elements)
        if (e.nom() == "layerDef") {
          const Expr* t = e.enfant("layerType");
          const Expr* num = e.enfant("layerNum");
          if (!num) continue;
          const int k = static_cast<int>(num->nombre(1));
          std::string n = texteDe(e, 1);
          for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
          const std::string type = t ? t->valeur(1) : std::string();
          if (type == "Signal" || type == "Plane") {
            if (n == "top" || k == 1) couches[k] = {Role::Cuivre, Couche::CuivreDessus};
            else if (n == "bottom" || k == 2) couches[k] = {Role::Cuivre, Couche::CuivreDessous};
            else internes.push_back({k, n});
          } else if (n == "board" || n == "board outline") couches[k] = {Role::Contour, Couche::CuivreDessus};
          else if (n == "top silk") couches[k] = {Role::Seri, Couche::SerigraphieDessus};
          else if (n == "bot silk" || n == "bottom silk") couches[k] = {Role::Seri, Couche::SerigraphieDessous};
        }
      if (couches.empty() && internes.empty())
        couches = {{1, {Role::Cuivre, Couche::CuivreDessus}}, {2, {Role::Cuivre, Couche::CuivreDessous}}, {3, {Role::Contour, Couche::CuivreDessus}}, {6, {Role::Seri, Couche::SerigraphieDessus}}, {7, {Role::Seri, Couche::SerigraphieDessous}}};
      std::sort(internes.begin(), internes.end());
      for (std::size_t i = 0; i < internes.size(); ++i) couches[internes[i].first] = {Role::Cuivre, cuivreInterne(static_cast<int>(std::min<std::size_t>(i + 1, MAX_CUIVRES - 2)))};
      l.cuivres = 2 + static_cast<int>(internes.size());
      // Longueur d'une valeur, suivie ou non de son unité (« 17.8 mm » au milieu d'un fichier en mils)
      auto longueur = [&](const Expr& x, std::size_t i) {
        const double v = x.nombre(i);
        const std::string u = x.valeur(i + 1);
        return nm(u == "mm" ? v : u == "mil" ? v * 0.0254 : u == "in" ? v * 25.4 : v * unite);
      };
      auto point = [&](const Expr* e) { return e ? Pt{longueur(*e, 1), -longueur(*e, 2)} : Pt{}; };
      std::vector<std::pair<Pt, Pt>> bord;
      int libres = 0;
      for (const auto& e : bloc.elements) {
        if (e.nom() == "multiLayer")
          for (const auto& x : e.elements) {
            if (x.nom() == "pattern") {
              cao_interne::CarteLue::Pose p;
              if (const Expr* a = x.enfant("refDesRef")) p.ref = a->valeur(1);
              if (const Expr* a = x.enfant("patternRef")) p.decal = a->valeur(1);
              p.p = point(x.enfant("pt"));
              if (const Expr* a = x.enfant("rotation")) p.rotation = a->nombre(1);
              if (const Expr* a = x.enfant("isFlipped")) p.dessous = a->valeur(1) == "True";
              l.poses.push_back(p);
            } else if (x.nom() == "via") {
              Via v;
              v.p = point(x.enfant("pt"));
              if (const Expr* a = x.enfant("netNameRef")) v.reseau = a->valeur(1);
              if (const Expr* a = x.enfant("diam")) v.diametre = longueur(*a, 1);
              if (const Expr* a = x.enfant("holeDiam")) v.percage = longueur(*a, 1);
              l.vias.push_back(v);
            } else if (x.nom() == "pad") {
              // Pastille libre : une pièce de la carte seule, son réseau gardé
              const Expr* st = x.enfant("padStyleRef");
              if (!st || !stylesPads.count(st->valeur(1))) continue;
              const auto& sp = stylesPads[st->valeur(1)];
              Pastille q;
              q.numero = "1";
              q.largeur = nm(sp[0].first);
              q.hauteur = nm(sp[0].second);
              q.percage = nm(sp[1].first);
              q.type = q.percage > 0 ? TypePastille::Traversante : TypePastille::CMS;
              q.forme = sp[1].second == 1 ? (q.largeur == q.hauteur ? FormePastille::Rond : FormePastille::Ovale) : FormePastille::Rect;
              DefEmpreinte d;
              d.pastilles.push_back(q);
              const std::string id = "pastille-libre-" + std::to_string(++libres);
              l.empreintes[id] = d;
              cao_interne::CarteLue::Pose p;
              p.ref = "PL" + std::to_string(libres);
              p.decal = id;
              p.p = point(x.enfant("pt"));
              if (const Expr* a = x.enfant("rotation")) p.rotation = a->nombre(1);
              l.poses.push_back(p);
              l.carteSeule.insert(p.ref);
              if (const Expr* a = x.enfant("netNameRef")) l.netlist.reseaux[a->valeur(1)].push_back({p.ref, "1"});
            }
          }
        if (e.nom() != "layerContents") continue;
        const Expr* num = e.enfant("layerNumRef");
        const auto ic = couches.find(num ? static_cast<int>(num->nombre(1)) : 0);
        if (ic == couches.end()) continue;
        const CouchePcad k = ic->second;
        auto points = [&](const Expr& x) {
          Polygone q;
          for (const auto& y : x.elements)
            if (y.nom() == "pt") q.push_back(point(&y));
          return q;
        };
        for (const auto& x : e.elements) {
          const Expr* w = x.enfant("width");
          const Nm largeur = w ? longueur(*w, 1) : 0;
          const std::string reseau = x.enfant("netNameRef") ? x.enfant("netNameRef")->valeur(1) : std::string();
          if (x.nom() == "line" || x.nom() == "arc") {
            // Arc : centre, rayon, angle de départ et balayage en degrés (sens direct, y vers le haut)
            std::vector<Pt> traits = points(x);
            std::optional<Pt> milieu;
            if (x.nom() == "arc") {
              const Pt c = point(x.enfant("pt"));
              const double r = static_cast<double>(x.enfant("radius") ? longueur(*x.enfant("radius"), 1) : 0);
              const double a0 = x.enfant("startAngle") ? x.enfant("startAngle")->nombre(1) : 0, da = x.enfant("sweepAngle") ? x.enfant("sweepAngle")->nombre(1) : 360;
              auto sur = [&](double a) { return Pt{c.x + static_cast<Nm>(std::llround(r * std::cos(a * std::numbers::pi / 180))), c.y - static_cast<Nm>(std::llround(r * std::sin(a * std::numbers::pi / 180)))}; };
              traits.clear();
              const int n = std::max(2, static_cast<int>(std::ceil(std::abs(da) / 10)));
              for (int i = 0; i <= n; ++i) traits.push_back(sur(a0 + da * i / n));
              if (k.role == Role::Cuivre && std::abs(da) < 360) {
                // Arc de piste d'un seul tenant
                Piste t;
                t.couche = k.couche;
                t.a = sur(a0);
                t.b = sur(a0 + da);
                t.milieu = sur(a0 + da / 2);
                t.largeur = std::max<Nm>(largeur, 50'000);
                t.reseau = reseau;
                l.pistes.push_back(t);
                continue;
              }
            }
            for (std::size_t i = 1; i < traits.size(); ++i) {
              if (k.role == Role::Contour) bord.push_back({traits[i - 1], traits[i]});
              else if (k.role == Role::Cuivre) {
                Piste t;
                t.couche = k.couche;
                t.a = traits[i - 1];
                t.b = traits[i];
                t.largeur = std::max<Nm>(largeur, 50'000);
                t.reseau = reseau;
                l.pistes.push_back(t);
              } else if (k.role == Role::Seri) {
                DessinCircuit d;
                d.couche = k.couche;
                d.points = {traits[i - 1], traits[i]};
                d.epaisseur = std::max<Nm>(largeur, 50'000);
                l.dessins.push_back(d);
              }
            }
          } else if (x.nom() == "pcbPoly") {
            const Polygone q = points(x);
            if (q.size() < 3) continue;
            if (k.role == Role::Cuivre) {
              RegionCuivre g;
              g.genre = "libre";
              g.couche = k.couche;
              g.forme = orienter(q);
              g.reseau = reseau;
              l.regions.push_back(g);
            } else if (k.role == Role::Seri) {
              DessinCircuit d;
              d.forme = DessinCircuit::Forme::Polygone;
              d.couche = k.couche;
              d.points = q;
              d.plein = true;
              d.epaisseur = 0;
              l.dessins.push_back(d);
            } else if (k.role == Role::Contour)
              for (std::size_t i = 0; i < q.size(); ++i) bord.push_back({q[i], q[(i + 1) % q.size()]});
          } else if ((x.nom() == "copperPour95" || x.nom() == "planeObj") && k.role == Role::Cuivre) {
            // Remplissage : une zone au réseau donné, contour pris de son polygone
            const Expr* poly = x.enfant("pcbPoly");
            const Polygone q = poly ? points(*poly) : points(x);
            if (q.size() < 3) continue;
            Zone z;
            z.couche = k.couche;
            z.reseau = reseau;
            z.contour = orienter(q);
            l.zones.push_back(z);
          } else if (x.nom() == "polyKeepOut" && k.role == Role::Cuivre) {
            const Polygone q = points(x);
            if (q.size() < 3) continue;
            ZoneRegle z;
            z.nom = "interdit P-CAD";
            z.contour = orienter(q);
            z.couches = bit(k.couche);
            z.interditPistes = z.interditVias = z.interditZones = true;
            l.zonesRegles.push_back(z);
          } else if (x.nom() == "text" && (k.role == Role::Cuivre || k.role == Role::Seri)) {
            // Texte : ancre en bas à gauche, boîte donnée (« extent »), retourné s'il est sur l'autre face
            TexteCircuit t;
            t.texte = x.valeur(2);
            for (auto& ch : t.texte)
              if (ch == '\r' || ch == '\n') ch = ' ';
            t.couche = k.couche;
            const Pt a = point(x.enfant("pt"));
            double lx = 0, ly = 0;
            if (const Expr* ex = x.enfant("extent")) {
              lx = static_cast<double>(longueur(*ex, 1));
              const bool unite1 = ex->valeur(2) == "mm" || ex->valeur(2) == "mil" || ex->valeur(2) == "in";
              ly = static_cast<double>(longueur(*ex, unite1 ? 3 : 2));
            }
            const double rot = x.enfant("rotation") ? x.enfant("rotation")->nombre(1) : 0;
            const bool retourne = x.enfant("isFlipped") && x.enfant("isFlipped")->valeur(1) == "True";
            t.rotation = retourne ? -rot : rot;
            if (retourne) t.miroir = true;
            t.hauteur = static_cast<Nm>(std::max(100'000.0, ly > 0 ? ly * 0.7 : 1'000'000.0));
            if (lx > 0) {
              const double lt = static_cast<double>(largeurTexte(t.texte, t.hauteur));
              if (lt > lx) t.hauteur = static_cast<Nm>(static_cast<double>(t.hauteur) * lx / lt);
            }
            const double dx = (retourne ? -lx : lx) / 2, dy = -ly / 2, c = std::cos(t.rotation * std::numbers::pi / 180), sn = std::sin(t.rotation * std::numbers::pi / 180);
            t.p = a + Pt{static_cast<Nm>(std::llround(dx * c + dy * sn)), static_cast<Nm>(std::llround(-dx * sn + dy * c))};
            l.textes.push_back(t);
          }
        }
      }
      if (!bord.empty() && !cao_interne::contourDepuisTraits(l, bord)) r.alertes.push_back("contour de la couche Board non fermé");
    }
  }
  for (auto& x : l.netlist.composants)
    for (const auto& p : l.poses)
      if (p.ref == x.reference && x.empreinte.empty()) x.empreinte = p.decal;
  ConceptionElec c = cao_interne::assemblerCarte(l, nom, "pcad:", r);
  if (rapport) *rapport = r;
  return c;
}

}  // namespace prom::ecad
