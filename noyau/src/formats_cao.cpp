// Prométhée : échanges avec les autres outils de CAO électronique (10.17, 10.18, 10.22). Voir formats_cao.hpp.
// Licence GPL-3.0-only.
#include "promethee/formats_cao.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

#include "promethee/circuit_edition.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/netlists_tierces.hpp"
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
std::string sansEspace(const std::string& s) {
  std::string r;
  for (unsigned char ch : s) r += (std::isspace(ch) || ch == '"' || ch == '(' || ch == ')') ? '_' : static_cast<char>(ch);
  return r.empty() ? "_" : r;
}
std::vector<std::string> mots(const std::string& s) {
  std::vector<std::string> r;
  std::istringstream in(s);
  for (std::string m; in >> m;) r.push_back(m);
  return r;
}
double reel(const std::string& s) {
  char* fin = nullptr;
  const double v = std::strtod(s.c_str(), &fin);
  return fin == s.c_str() ? 0 : v;
}
}  // namespace

bool estPcadAscii(const std::string& t) { return t.rfind("ACCEL_ASCII", 0) == 0 || t.find("(asciiHeader") != std::string::npos; }

// ======================================================================================================================
// PADS ASCII (Layout), millimètres
// ======================================================================================================================
// ======================================================================================================================
// P-CAD ASCII (ACCEL_ASCII)
// ======================================================================================================================
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
    for (const auto& p : d->pastilles)
      o << "        (pad (padNum " << ++i << ") (padStyleRef " << q(em.empreinte + "_" + std::to_string(i)) << ") " << pt(p.position) << " (rotation " << nb(p.rotation) << ") (defaultPinDes "
        << q(p.numero) << "))\n";
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
  for (Couche k : couchesCuivre(n))
    o << "  (layerDef " << q(codeCouche(k)) << " (layerNum " << rangCuivre(k, n) + 1 << ") (layerType Signal))\n";
  o << "  (layerDef \"Board\" (layerNum " << n + 1 << ") (layerType NonSignal))\n  (multiLayer\n";
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
    o << "  (layerContents (layerNumRef " << rangCuivre(k, n) + 1 << ")\n";
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
  o << "  (layerContents (layerNumRef " << n + 1 << ")\n";
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
  if (l.contour.size() >= 3) c.circuit.contour.libre = orienter(l.contour);
  synchroniserReseaux(c);
  r.composants = static_cast<int>(l.poses.size());
  r.reseaux = static_cast<int>(l.netlist.reseaux.size());
  r.pistes = static_cast<int>(l.pistes.size());
  r.vias = static_cast<int>(l.vias.size());
  return c;
}
namespace {
Couche couchePads(int numero, int n) {
  if (numero <= 1) return Couche::CuivreDessus;
  if (numero >= n) return Couche::CuivreDessous;
  return cuivreInterne(std::clamp(numero - 1, 1, MAX_CUIVRES - 2));
}
}  // namespace

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
      int cuivres = 0, couchePlanche = -1;
      std::map<int, Couche> couches;
      for (const auto& e : bloc.elements)
        if (e.nom() == "layerDef") {
          const Expr* t = e.enfant("layerType");
          const Expr* num = e.enfant("layerNum");
          if (t && num && t->valeur(1) == "Signal") ++cuivres;
          if (num && (texteDe(e, 1) == "Board" || texteDe(e, 1) == "Board Outline")) couchePlanche = static_cast<int>(num->nombre(1));
        }
      l.cuivres = std::max(2, cuivres);
      for (const auto& e : bloc.elements) {
        if (e.nom() == "multiLayer")
          for (const auto& x : e.elements) {
            if (x.nom() == "pattern") {
              cao_interne::CarteLue::Pose p;
              if (const Expr* a = x.enfant("refDesRef")) p.ref = a->valeur(1);
              if (const Expr* a = x.enfant("patternRef")) p.decal = a->valeur(1);
              const Expr* ppt = x.enfant("pt");
              p.p = {nm(ppt ? ppt->nombre(1) * unite : 0), -nm(ppt ? ppt->nombre(2) * unite : 0)};
              if (const Expr* a = x.enfant("rotation")) p.rotation = a->nombre(1);
              if (const Expr* a = x.enfant("isFlipped")) p.dessous = a->valeur(1) == "True";
              l.poses.push_back(p);
            } else if (x.nom() == "via") {
              Via v;
              const Expr* ppt = x.enfant("pt");
              v.p = {nm(ppt ? ppt->nombre(1) * unite : 0), -nm(ppt ? ppt->nombre(2) * unite : 0)};
              if (const Expr* a = x.enfant("netNameRef")) v.reseau = a->valeur(1);
              if (const Expr* a = x.enfant("diam")) v.diametre = nm(a->nombre(1) * unite);
              if (const Expr* a = x.enfant("holeDiam")) v.percage = nm(a->nombre(1) * unite);
              l.vias.push_back(v);
            }
          }
        if (e.nom() == "layerContents") {
          const Expr* num = e.enfant("layerNumRef");
          const int k = num ? static_cast<int>(num->nombre(1)) : 0;
          for (const auto& x : e.elements) {
            if (x.nom() != "line") continue;
            std::vector<const Expr*> pts;
            for (const auto& y : x.elements)
              if (y.nom() == "pt") pts.push_back(&y);
            if (pts.size() < 2) continue;
            Pt a{nm(pts[0]->nombre(1) * unite), -nm(pts[0]->nombre(2) * unite)}, b{nm(pts[1]->nombre(1) * unite), -nm(pts[1]->nombre(2) * unite)};
            if (k == couchePlanche) {
              if (l.contour.empty()) l.contour.push_back(a);
              if (!(l.contour.front() == b)) l.contour.push_back(b);
              continue;
            }
            if (k < 1 || k > l.cuivres) continue;
            Piste t;
            t.couche = couchePads(k, l.cuivres);
            t.a = a;
            t.b = b;
            if (const Expr* w = x.enfant("width")) t.largeur = nm(w->nombre(1) * unite);
            if (const Expr* n = x.enfant("netNameRef")) t.reseau = n->valeur(1);
            l.pistes.push_back(t);
          }
        }
      }
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
