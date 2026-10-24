// Prométhée : échanges de la conception électronique (lot L10). Licence GPL-3.0-only.
#include "promethee/echanges_electronique.hpp"

#include "promethee/csv.hpp"
#include "promethee/netlists_tierces.hpp"
#include "promethee/valeurs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numeric>
#include <set>
#include <sstream>

#include "kicad_interne.hpp"
#include "promethee/fabrication_avancee.hpp"
#include "promethee/version.hpp"

namespace prom::ecad {

namespace {
using kicad_interne::Expr;
using kicad_interne::Lecteur;

std::string sansBarre(std::string n) {
  if (n.size() > 1 && n[0] == '/') n.erase(0, 1);
  return n;
}
std::string trim(std::string s) {
  const auto a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  const auto b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
std::string prefixeDe(const std::string& ref) {
  std::string p;
  for (char ch : ref) {
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') p += ch;
    else break;
  }
  return p.empty() ? "U" : p;
}
std::string fmt(double v, int d = 3) {
  char t[48];
  std::snprintf(t, sizeof t, "%.*f", d, v);
  return t;
}
std::string guillemets(const std::string& s) {
  std::string r = "\"";
  for (char ch : s) r += ch == '"' ? '\'' : ch;
  return r + "\"";
}
std::string xml(const std::string& t) {
  std::string r;
  for (char ch : t) r += ch == '&' ? "&amp;" : ch == '<' ? "&lt;" : ch == '>' ? "&gt;" : ch == '"' ? "&quot;" : std::string(1, ch);
  return r;
}
// Vers le corps du symbole, selon BrochePlacee::direction ; le fil part dans l'autre sens
Pt versCorps(int direction) {
  switch (direction) {
    case 0: return {1, 0};
    case 1: return {0, 1};
    case 2: return {-1, 0};
    default: return {0, -1};
  }
}
}  // namespace

// ---------- Netlists ----------
NetlistLue lireNetlist(const std::string& texte) {
  NetlistLue n;
  if (texte.find("(export") != std::string::npos) {
    try {
      const Expr racine = Lecteur(texte).lire();
      if (const Expr* cs = racine.enfant("components"))
        for (const auto& e : cs->elements) {
          if (e.nom() != "comp") continue;
          ComposantNetlist x;
          if (const Expr* r = e.enfant("ref")) x.reference = r->valeur(1);
          if (const Expr* v = e.enfant("value")) x.valeur = v->valeur(1);
          if (const Expr* f = e.enfant("footprint")) x.empreinte = f->valeur(1);
          if (!x.reference.empty()) n.composants.push_back(x);
        }
      if (const Expr* ns = racine.enfant("nets"))
        for (const auto& e : ns->elements) {
          if (e.nom() != "net") continue;
          const Expr* nom = e.enfant("name");
          const std::string reseau = sansBarre(nom ? nom->valeur(1) : std::string());
          if (reseau.empty()) continue;
          auto& noeuds = n.reseaux[reseau];
          for (const auto& x : e.elements)
            if (x.nom() == "node")
              if (const Expr* r = x.enfant("ref"); r && x.enfant("pin")) noeuds.push_back({r->valeur(1), x.enfant("pin")->valeur(1)});
        }
    } catch (const std::exception& e) {
      n.erreurs.push_back(std::string("netlist KiCad illisible : ") + e.what());
    }
    return n;
  }
  // Netlists des autres logiciels (10.20) : OrCAD PCB II, PADS, Allegro, CADSTAR, EDIF, SPICE et LTspice, gEDA, TinyCAD
  if (texte.find("Composants") == std::string::npos || texte.find("Réseaux") == std::string::npos)
    if (const auto f = reconnaitreNetlist(texte)) return lireNetlistTierce(texte, *f);
  // Liste de Prométhée : « Composants » puis « Réseaux »
  std::istringstream in(texte);
  std::string ligne;
  int partie = 0;
  while (std::getline(in, ligne)) {
    const std::string t = trim(ligne);
    if (t.empty() || t[0] == '#') continue;
    if (t == "Composants") {
      partie = 1;
      continue;
    }
    if (t == "Réseaux") {
      partie = 2;
      continue;
    }
    if (partie == 1) {
      std::vector<std::string> champs;
      std::string champ;
      std::istringstream l(ligne);
      while (std::getline(l, champ, '\t'))
        if (!trim(champ).empty() || !champs.empty()) champs.push_back(trim(champ));
      if (!champs.empty()) n.composants.push_back({champs[0], champs.size() > 1 ? champs[1] : std::string(), champs.size() > 2 ? champs[2] : std::string()});
    } else if (partie == 2) {
      const auto deux = t.find(" :");
      if (deux == std::string::npos) {
        n.erreurs.push_back("ligne de réseau sans « : » : " + t);
        continue;
      }
      auto& noeuds = n.reseaux[t.substr(0, deux)];
      std::istringstream l(t.substr(deux + 2));
      std::string b;
      while (l >> b)
        if (const auto p = b.rfind('.'); p != std::string::npos && p > 0) noeuds.push_back({b.substr(0, p), b.substr(p + 1)});
    }
  }
  if (n.composants.empty() && n.reseaux.empty()) n.erreurs.push_back("aucun composant ni réseau reconnu : format de netlist inconnu");
  return n;
}

NetlistLue netlistDe(const ConceptionElec& c) {
  NetlistLue n;
  for (const auto& x : c.composants)
    if (!x.reference.empty() && x.reference[0] != '#') n.composants.push_back({x.reference, x.valeur, x.empreinte});
  const Connectivite k = connectivite(c);
  for (const auto& r : k.reseaux) {
    std::vector<std::pair<std::string, std::string>> noeuds;
    for (const auto& b : r.broches)
      if (const ComposantElec* x = c.composant(b.composant)) noeuds.push_back({x->reference, b.broche});
    if (!noeuds.empty()) {
      auto& v = n.reseaux[r.nom];
      v.insert(v.end(), noeuds.begin(), noeuds.end());
    }
  }
  return n;
}

std::string empreinteConnue(const std::string& nom) {
  std::string n = nom;
  if (const auto p = n.find(':'); p != std::string::npos) n = n.substr(p + 1);
  if (n.empty()) return {};
  if (genererEmpreinte(n)) return n;
  // « R_0603_1608Metric » → « R_0603 » ; « SOIC-8_3.9x4.9mm_P1.27mm » → « SOIC-8 »
  for (std::size_t i = n.size(); i > 0; --i)
    if (n[i - 1] == '_' || n[i - 1] == '-') {
      const std::string court = n.substr(0, i - 1);
      if (!court.empty() && genererEmpreinte(court)) return court;
    }
  return n;
}

namespace {
// Pose d'un composant de netlist : symbole (bibliothèque pour une résistance ou un condensateur, généré sinon), repère,
// valeur et empreinte ; rend l'UUID du symbole posé
std::string poserComposantNetlist(ConceptionElec& c, std::size_t feuille, const ComposantNetlist& x, const std::vector<std::string>& broches, Pt position) {
  const std::string prefixe = prefixeDe(x.reference);
  std::string id;
  if ((prefixe == "R" || prefixe == "C") && broches == std::vector<std::string>{"1", "2"}) id = prefixe;
  else {
    std::string signature;
    for (const auto& b : broches) signature += b + ',';
    id = "netlist:" + prefixe + "-" + std::to_string(broches.size()) + "-" + std::to_string(std::hash<std::string>{}(signature) % 1000000);
    if (!c.symboles.count(id)) {
      SpecSymbole s;
      s.id = id;
      s.nom = prefixe + " " + std::to_string(broches.size()) + " broches";
      s.prefixe = prefixe;
      s.categorie = "Netlist";
      for (const auto& b : broches) s.broches.push_back({b, b, TypeBroche::Passive, -1, 0});
      c.symboles[id] = genererSymbole(s);
    }
  }
  const std::string sym = poserSymbole(c, feuille, id, position);
  for (const auto& s : c.feuilles[feuille].symboles)
    if (s.uuid == sym)
      if (ComposantElec* k = c.composant(s.composant)) {
        k->reference = x.reference;
        k->valeur = x.valeur;
        if (!x.empreinte.empty()) k->empreinte = empreinteConnue(x.empreinte);
      }
  return sym;
}
// Étiquette globale au bout d'un fil de 2,54 mm tiré de la broche, vers l'extérieur du symbole
void relierBroche(ConceptionElec& c, std::size_t feuille, const std::string& symbole, const std::string& numero, const std::string& reseau) {
  Feuille& f = c.feuilles[feuille];
  for (const auto& s : f.symboles)
    if (s.uuid == symbole)
      for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
        if (b.broche->numero == numero) {
          const Pt d = versCorps(b.direction);
          const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
          f.fils.push_back({nouvelUuid(), b.p, bout});
          Etiquette e;
          e.uuid = nouvelUuid();
          e.texte = reseau;
          e.p = bout;
          e.type = TypeEtiquette::Globale;
          f.etiquettes.push_back(e);
          return;
        }
}
std::map<std::string, std::vector<std::string>> brochesParComposant(const NetlistLue& n) {
  std::map<std::string, std::set<std::string, bool (*)(const std::string&, const std::string&)>> b;
  auto ensemble = [&](const std::string& ref) -> auto& {
    auto it = b.find(ref);
    if (it == b.end()) it = b.emplace(ref, std::set<std::string, bool (*)(const std::string&, const std::string&)>(ordreNaturel)).first;
    return it->second;
  };
  for (const auto& x : n.composants) ensemble(x.reference);
  for (const auto& [reseau, noeuds] : n.reseaux)
    for (const auto& [ref, broche] : noeuds) ensemble(ref).insert(broche);
  std::map<std::string, std::vector<std::string>> r;
  for (auto& [ref, s] : b) r[ref] = std::vector<std::string>(s.begin(), s.end());
  return r;
}
// Place libre suivante sur la feuille (rangées de 30,48 mm, A3 paysage)
struct Rangement {
  std::size_t feuille = 0;
  double x = 25.4, y = 25.4;
  double rang = 0;  // hauteur du plus grand symbole du rang en cours : le rang suivant passe dessous
  Pt suivant(ConceptionElec& c, double hauteurMm) {
    if (x > 380) {
      x = 25.4;
      y += std::max(40.64, rang + 12.7);
      rang = 0;
    }
    if (y + hauteurMm > 270 && y > 25.4) {
      ++feuille;
      x = 25.4;
      y = 25.4;
      rang = 0;
    }
    rang = std::max(rang, hauteurMm);
    while (c.feuilles.size() <= feuille) {
      Feuille f;
      f.uuid = nouvelUuid();
      f.nom = "Netlist " + std::to_string(c.feuilles.size() + 1);
      f.format = "A3";
      c.feuilles.push_back(f);
    }
    const Pt p = ptMm(x, y);
    x += 50.8;
    return {p.x / GRILLE_SCHEMA * GRILLE_SCHEMA, p.y / GRILLE_SCHEMA * GRILLE_SCHEMA};
  }
};
}  // namespace

ConceptionElec conceptionDepuisNetlist(const NetlistLue& n, const std::string& nom) {
  ConceptionElec c = nouvelleConception(nom);
  c.feuilles[0].format = "A3";
  c.feuilles[0].nom = "Netlist 1";
  const auto broches = brochesParComposant(n);
  std::map<std::string, ComposantNetlist> parRef;
  for (const auto& x : n.composants) parRef[x.reference] = x;
  std::vector<std::string> refs;
  for (const auto& [ref, b] : broches) refs.push_back(ref);
  std::sort(refs.begin(), refs.end(), ordreNaturel);
  std::map<std::string, std::pair<std::size_t, std::string>> poses;  // repère → (feuille, symbole)
  // Symboles empilés en colonnes d'après leur encombrement réel (broches et fils des étiquettes compris) : aucun ne
  // déborde sur un autre, sans quoi une étiquette posée sur la broche d'un voisin réunirait deux réseaux
  std::size_t feuille = 0;
  Nm xCol = nm(25.4), yCur = nm(25.4), largeurCol = 0;
  const Nm marge = nm(7.62), bas = nm(270), droite = nm(400);
  for (const auto& ref : refs) {
    ComposantNetlist x = parRef.count(ref) ? parRef[ref] : ComposantNetlist{ref, {}, {}};
    const auto& b = broches.at(ref);
    while (c.feuilles.size() <= feuille) {
      Feuille f;
      f.uuid = nouvelUuid();
      f.nom = "Netlist " + std::to_string(c.feuilles.size() + 1);
      f.format = "A3";
      c.feuilles.push_back(f);
    }
    const std::string sym = poserComposantNetlist(c, feuille, x, b.empty() ? std::vector<std::string>{"1"} : b, Pt{0, 0});
    SymbolePlace* sp = nullptr;
    for (auto& s : c.feuilles[feuille].symboles)
      if (s.uuid == sym) sp = &s;
    Boite boite = sp ? boiteSymbole(*sp, *c.symbole(sp->symbole)) : Boite{0, 0, nm(10), nm(10)};
    boite = boite.gonflee(nm(5.08));  // fils des étiquettes
    if (yCur + boite.hauteur() > bas && yCur > nm(25.4)) {
      xCol += largeurCol + marge * 2;
      yCur = nm(25.4);
      largeurCol = 0;
    }
    if (xCol + boite.largeur() > droite && xCol > nm(25.4)) {
      // Feuille suivante : le symbole y est déplacé
      Feuille f;
      f.uuid = nouvelUuid();
      f.nom = "Netlist " + std::to_string(c.feuilles.size() + 1);
      f.format = "A3";
      SymbolePlace copie = *sp;
      c.feuilles[feuille].symboles.erase(std::remove_if(c.feuilles[feuille].symboles.begin(), c.feuilles[feuille].symboles.end(), [&](const SymbolePlace& s) { return s.uuid == sym; }),
                                         c.feuilles[feuille].symboles.end());
      ++feuille;
      if (c.feuilles.size() <= feuille) c.feuilles.push_back(f);
      c.feuilles[feuille].symboles.push_back(copie);
      sp = &c.feuilles[feuille].symboles.back();
      xCol = nm(25.4);
      yCur = nm(25.4);
      largeurCol = 0;
    }
    if (sp) {
      Pt q{xCol - boite.x0, yCur - boite.y0};
      q = {(q.x + GRILLE_SCHEMA - 1) / GRILLE_SCHEMA * GRILLE_SCHEMA, (q.y + GRILLE_SCHEMA - 1) / GRILLE_SCHEMA * GRILLE_SCHEMA};
      sp->position = q;
    }
    yCur += boite.hauteur() + marge;
    largeurCol = std::max(largeurCol, boite.largeur());
    poses[ref] = {feuille, sym};
  }
  for (const auto& [reseau, noeuds] : n.reseaux)
    for (const auto& [ref, broche] : noeuds)
      if (const auto it = poses.find(ref); it != poses.end()) relierBroche(c, it->second.first, it->second.second, broche, reseau);
  mettreAJourCircuit(c);
  // Empreintes rangées en grille sur une carte à leur mesure
  Nm pas = 5 * NM_PAR_MM;
  for (const auto& e : c.circuit.empreintes)
    if (const DefEmpreinte* d = c.empreinte(e.empreinte)) pas = std::max(pas, std::max(d->encombrement.largeur(), d->encombrement.hauteur()) + 2 * NM_PAR_MM);
  const int colonnes = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(c.circuit.empreintes.size())))));
  for (std::size_t i = 0; i < c.circuit.empreintes.size(); ++i)
    c.circuit.empreintes[i].position = {5 * NM_PAR_MM + pas / 2 + pas * static_cast<Nm>(i % static_cast<std::size_t>(colonnes)),
                                        5 * NM_PAR_MM + pas / 2 + pas * static_cast<Nm>(i / static_cast<std::size_t>(colonnes))};
  const int rangs = static_cast<int>((c.circuit.empreintes.size() + static_cast<std::size_t>(colonnes) - 1) / static_cast<std::size_t>(colonnes));
  c.circuit.contour.origine = {0, 0};
  c.circuit.contour.largeur = 10 * NM_PAR_MM + pas * colonnes;
  c.circuit.contour.hauteur = 10 * NM_PAR_MM + pas * std::max(1, rangs);
  return c;
}

BilanNetlist mettreAJourDepuisNetlist(ConceptionElec& c, const NetlistLue& n) {
  exigerModifiable(c);
  BilanNetlist b;
  const auto broches = brochesParComposant(n);
  std::map<std::string, ComposantNetlist> parRef;
  for (const auto& x : n.composants) parRef[x.reference] = x;
  // Composants : valeur et empreinte ; ceux qui manquent sont posés à droite de la première feuille
  Rangement place;
  place.y = 200;
  for (const auto& [ref, bs] : broches) {
    const ComposantNetlist x = parRef.count(ref) ? parRef[ref] : ComposantNetlist{ref, {}, {}};
    if (ComposantElec* k = const_cast<ComposantElec*>(c.parReference(ref))) {
      if (!x.valeur.empty() && k->valeur != x.valeur) {
        b.lignes.push_back({ref, "valeur", k->valeur, x.valeur});
        k->valeur = x.valeur;
        ++b.valeurs;
      }
      const std::string e = x.empreinte.empty() ? std::string() : empreinteConnue(x.empreinte);
      if (!e.empty() && k->empreinte != e) {
        b.lignes.push_back({ref, "empreinte", k->empreinte, e});
        k->empreinte = e;
        ++b.empreintes;
      }
      continue;
    }
    poserComposantNetlist(c, 0, x, bs.empty() ? std::vector<std::string>{"1"} : bs, place.suivant(c, 2.54 * static_cast<double>(bs.size()) / 2 + 10));
    b.lignes.push_back({ref, "ajouté", "", x.valeur});
    ++b.ajoutes;
  }
  // Liaisons : une broche dont le réseau diffère est recâblée si elle est reliée par une étiquette au bout de son fil
  const Connectivite k = connectivite(c);
  for (const auto& [reseau, noeuds] : n.reseaux)
    for (const auto& [ref, broche] : noeuds) {
      const ComposantElec* x = c.parReference(ref);
      if (!x) continue;
      const std::string actuel = k.nom(x->uuid, broche);
      if (k.canonique(actuel) == k.canonique(reseau)) continue;
      bool recable = false, avecFil = false;
      for (auto& f : c.feuilles) {
        for (const auto& s : f.symboles) {
          if (s.composant != x->uuid) continue;
          const DefSymbole* d = c.symbole(s.symbole);
          if (!d) continue;
          for (const auto& bp : brochesPlacees(s, *d)) {
            if (bp.broche->numero != broche) continue;
            for (const auto& fil : f.fils) {
              if (fil.a != bp.p && fil.b != bp.p) continue;
              avecFil = true;
              const Pt bout = fil.a == bp.p ? fil.b : fil.a;
              for (auto& e : f.etiquettes)
                if (e.p == bout) {
                  e.texte = reseau;
                  recable = true;
                }
            }
            if (!avecFil) {
              // Broche libre : un fil et une étiquette
              const Pt d0 = versCorps(bp.direction);
              const Pt bout{bp.p.x - d0.x * 2'540'000, bp.p.y - d0.y * 2'540'000};
              f.fils.push_back({nouvelUuid(), bp.p, bout});
              Etiquette e;
              e.uuid = nouvelUuid();
              e.texte = reseau;
              e.p = bout;
              e.type = TypeEtiquette::Globale;
              f.etiquettes.push_back(e);
              recable = true;
            }
          }
        }
      }
      if (recable) {
        b.lignes.push_back({ref + "." + broche, "réseau", actuel, reseau});
        ++b.liaisons;
      } else {
        b.lignes.push_back({ref + "." + broche, "réseau non changé (câblé au schéma)", actuel, reseau});
        ++b.nonChangees;
      }
    }
  for (const auto& x : c.composants)
    if (!x.reference.empty() && x.reference[0] != '#' && !broches.count(x.reference)) b.lignes.push_back({x.reference, "absent de la netlist (gardé)", "", ""});
  mettreAJourCircuit(c);
  return b;
}

namespace {
bool valeurSI(const std::string& t, const std::string& ref, double& v) {
  if (t.empty()) return false;
  std::string prefixe;
  for (char ch : ref) {
    if (!std::isalpha(static_cast<unsigned char>(ch))) break;
    prefixe += ch;
  }
  const ValeurComprise c = lireValeurComposant(t, familleDepuisPrefixe(prefixe));
  if (!c.comprise()) return false;
  v = c.nombre;
  return true;
}
}  // namespace

std::string rapportNetlistCsv(const BilanNetlist& b) {
  // Écrivain CSV du poste (10.20) : par défaut point-virgule, virgule décimale et marque UTF-8, ouvert d'un
  // double-clic en colonnes justes par LibreOffice et Excel ; la valeur numérique des valeurs changées suit la marque
  // décimale réglée
  prom::EcrivainCsv w;
  w.ligne({"objet", "changement", "avant", "après", "valeur après (SI)"});
  for (const auto& l : b.lignes) {
    for (std::size_t i = 0; i < 4; ++i) w.champ(i < l.size() ? l[i] : std::string());
    const std::string apres = l.size() > 3 ? l[3] : std::string();
    double v = 0;
    if ((l.size() > 1 && (l[1] == "valeur" || l[1] == "ajouté")) && valeurSI(apres, l[0], v)) w.nombre(v, 12);
    else w.champ("");
    w.fin();
  }
  return w.texte();
}

// ---------- DXF de la mécanique ----------
std::string dxfMecaniqueCarte(const ConceptionElec& c) {
  const Polygone carte = c.circuit.contour.polygone();
  const Boite b = boite(carte);
  auto X = [&](Nm x) { return fmt(mm(x - b.x0), 4); };
  auto Y = [&](Nm y) { return fmt(mm(b.y1 - y), 4); };
  std::ostringstream s;
  s << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n";
  s << "0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n4\n";
  const std::pair<const char*, int> calques[] = {{"CONTOUR", 7}, {"DECOUPES", 1}, {"PERCAGES", 5}, {"ENCOMBREMENTS", 3}};
  for (const auto& [nom, couleur] : calques) s << "0\nLAYER\n2\n" << nom << "\n70\n0\n62\n" << couleur << "\n6\nCONTINUOUS\n";
  s << "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
  auto poly = [&](const Polygone& p, const char* calque) {
    for (std::size_t i = 0; i < p.size(); ++i) {
      const Pt a = p[i], q = p[(i + 1) % p.size()];
      s << "0\nLINE\n8\n" << calque << "\n10\n" << X(a.x) << "\n20\n" << Y(a.y) << "\n30\n0\n11\n" << X(q.x) << "\n21\n" << Y(q.y) << "\n31\n0\n";
    }
  };
  poly(carte, "CONTOUR");
  for (const auto& d : c.circuit.contour.decoupes) poly(d, "DECOUPES");
  const Connectivite k = connectivite(c);
  for (const auto& p : pastilles(c, k))
    if (p.pastille.type == TypePastille::NonMetallisee || (p.pastille.percage >= 2'500'000 && p.reseau.empty()))
      s << "0\nCIRCLE\n8\nPERCAGES\n10\n" << X(p.pastille.position.x) << "\n20\n" << Y(p.pastille.position.y) << "\n30\n0\n40\n" << fmt(mm(p.pastille.percage) / 2, 4) << "\n";
  for (const auto& e : c.circuit.empreintes)
    if (const DefEmpreinte* d = c.empreinte(e.empreinte); d && !d->encombrement.vide()) poly(encombrementSurCarte(e, *d), "ENCOMBREMENTS");
  s << "0\nENDSEC\n0\nEOF\n";
  return s.str();
}

// ---------- Specctra ----------
namespace {
// Specctra : micromètres, Y vers le haut (Y du circuit nié)
std::string um(Nm v) { return fmt(static_cast<double>(v) / 1000.0, 1); }
std::string nomVia(Nm d, Nm p) { return "Via[0-1]_" + std::to_string(d / 1000) + ":" + std::to_string(p / 1000) + "_um"; }
std::string coucheDsn(Couche x) { return codeCouche(x); }
}  // namespace

std::string specctraDsn(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const Regles& R = c.circuit.regles;
  const std::vector<Couche> cuivres = couchesCuivre(c.circuit.empilage.couchesCuivre);
  std::ostringstream s;
  s << "(pcb " << guillemets(c.nom.empty() ? std::string("carte") : c.nom) << "\n";
  s << "  (parser (host_cad \"Promethee\") (host_version " << guillemets(infosVersion().version) << "))\n";
  s << "  (resolution um 10)\n  (unit um)\n  (structure\n";
  for (std::size_t i = 0; i < cuivres.size(); ++i) s << "    (layer " << coucheDsn(cuivres[i]) << " (type signal) (property (index " << i << ")))\n";
  s << "    (boundary (path pcb 0";
  const Polygone carte = c.circuit.contour.polygone();
  for (const Pt p : carte) s << " " << um(p.x) << " " << um(-p.y);
  if (!carte.empty()) s << " " << um(carte.front().x) << " " << um(-carte.front().y);
  s << "))\n";
  for (const auto& d : c.circuit.contour.decoupes) {
    s << "    (keepout \"\" (polygon signal 0";
    for (const Pt p : d) s << " " << um(p.x) << " " << um(-p.y);
    s << "))\n";
  }
  s << "    (via " << guillemets(nomVia(R.viaDiametre, R.viaPercage)) << ")\n";
  s << "    (rule (width " << um(R.largeur) << ") (clearance " << um(R.isolement) << "))\n  )\n";
  // Une image par composant, pastilles à leur place relative (déjà tournées) : la pose se fait sans rotation
  std::map<std::string, std::string> padstacks;  // forme → nom
  auto padstack = [&](const PastillePlacee& p) {
    std::ostringstream f;
    const Couches couches = p.couches;
    for (Couche x : cuivres) {
      if (!(couches & bit(x))) continue;
      f << "(shape (polygon " << coucheDsn(x) << " 0";
      for (const Pt q : p.forme) f << " " << um(q.x - p.pastille.position.x) << " " << um(-(q.y - p.pastille.position.y));
      f << "))";
    }
    const std::string cle = f.str();
    auto it = padstacks.find(cle);
    if (it == padstacks.end()) it = padstacks.emplace(cle, "Pastille_" + std::to_string(padstacks.size() + 1)).first;
    return it->second;
  };
  std::map<int, std::vector<const PastillePlacee*>> parEmpreinte;
  for (const auto& p : pp)
    if (!p.reference.empty() && !p.numero.empty() && (p.couches & tousLesCuivres(c.circuit.empilage.couchesCuivre))) parEmpreinte[p.empreinte].push_back(&p);
  std::ostringstream images;
  s << "  (placement\n";
  for (const auto& [i, liste] : parEmpreinte) {
    const EmpreintePlacee& e = c.circuit.empreintes[static_cast<std::size_t>(i)];
    const std::string ref = liste.front()->reference;
    s << "    (component " << guillemets("IMG_" + ref) << " (place " << guillemets(ref) << " " << um(e.position.x) << " " << um(-e.position.y) << " " << (e.dessous ? "back" : "front")
      << " 0 (lock_type position)))\n";
    images << "    (image " << guillemets("IMG_" + ref) << "\n";
    for (const PastillePlacee* p : liste)
      images << "      (pin " << guillemets(padstack(*p)) << " " << guillemets(p->numero) << " " << um(p->pastille.position.x - e.position.x) << " "
             << um(-(p->pastille.position.y - e.position.y)) << ")\n";
    images << "    )\n";
  }
  s << "  )\n  (library\n" << images.str();
  for (const auto& [forme, nom] : padstacks) s << "    (padstack " << guillemets(nom) << " " << forme << " (attach off))\n";
  s << "    (padstack " << guillemets(nomVia(R.viaDiametre, R.viaPercage));
  for (Couche x : cuivres) s << " (shape (circle " << coucheDsn(x) << " " << um(R.viaDiametre) << "))";
  s << " (attach off))\n  )\n  (network\n";
  std::map<std::string, std::vector<std::string>> broches;
  for (const auto& p : pp)
    if (!p.reseau.empty() && !p.reference.empty()) broches[p.reseau].push_back(p.reference + "-" + p.numero);
  for (const auto& [n, b] : broches) {
    s << "    (net " << guillemets(n) << " (pins";
    for (const auto& x : b) s << " " << x;
    s << "))\n";
  }
  s << "    (class \"defaut\"";
  for (const auto& [n, b] : broches) s << " " << guillemets(n);
  s << " (circuit (use_via " << guillemets(nomVia(R.viaDiametre, R.viaPercage)) << ")) (rule (width " << um(R.largeur) << ") (clearance " << um(R.isolement) << ")))\n  )\n";
  // Ce qui est déjà routé reste, protégé
  s << "  (wiring\n";
  for (const auto& t : c.circuit.pistes)
    s << "    (wire (path " << coucheDsn(t.couche) << " " << um(t.largeur) << " " << um(t.a.x) << " " << um(-t.a.y) << " " << um(t.b.x) << " " << um(-t.b.y) << ") (net " << guillemets(k.canonique(t.reseau))
      << ") (type protect))\n";
  for (const auto& v : c.circuit.vias)
    s << "    (via " << guillemets(nomVia(R.viaDiametre, R.viaPercage)) << " " << um(v.p.x) << " " << um(-v.p.y) << " (net " << guillemets(k.canonique(v.reseau)) << ") (type protect))\n";
  s << "  )\n)\n";
  return s.str();
}

std::string specctraSes(const ConceptionElec& c) {
  const Regles& R = c.circuit.regles;
  const std::vector<Couche> cuivres = couchesCuivre(c.circuit.empilage.couchesCuivre);
  const Connectivite k = connectivite(c);
  // Unités de la session : 0,1 µm (résolution um 10)
  auto u = [](Nm v) { return std::to_string(static_cast<long long>(std::llround(static_cast<double>(v) / 100.0))); };
  std::ostringstream s;
  s << "(session " << guillemets(c.nom + ".ses") << "\n  (base_design " << guillemets(c.nom + ".dsn") << ")\n  (routes\n    (resolution um 10)\n    (parser (host_cad \"Promethee\"))\n";
  s << "    (library_out\n      (padstack " << guillemets(nomVia(R.viaDiametre, R.viaPercage));
  for (Couche x : cuivres) s << " (shape (circle " << coucheDsn(x) << " " << u(R.viaDiametre) << " 0 0))";
  s << " (attach off))\n    )\n    (network_out\n";
  std::map<std::string, std::vector<const Piste*>> pistes;
  std::map<std::string, std::vector<const Via*>> vias;
  for (const auto& t : c.circuit.pistes) pistes[k.canonique(t.reseau)].push_back(&t);
  for (const auto& v : c.circuit.vias) vias[k.canonique(v.reseau)].push_back(&v);
  std::set<std::string> reseaux;
  for (const auto& [n, x] : pistes) reseaux.insert(n);
  for (const auto& [n, x] : vias) reseaux.insert(n);
  for (const auto& n : reseaux) {
    s << "      (net " << guillemets(n) << "\n";
    for (const Piste* t : pistes[n]) s << "        (wire (path " << coucheDsn(t->couche) << " " << u(t->largeur) << " " << u(t->a.x) << " " << u(-t->a.y) << " " << u(t->b.x) << " " << u(-t->b.y) << "))\n";
    for (const Via* v : vias[n]) s << "        (via " << guillemets(nomVia(v->diametre, v->percage)) << " " << u(v->p.x) << " " << u(-v->p.y) << ")\n";
    s << "      )\n";
  }
  s << "    )\n  )\n)\n";
  return s.str();
}

SessionSpecctra lireSpecctraSes(const std::string& texte0) {
  SessionSpecctra r;
  // « (string_quote ") » déclare le guillemet : ôté avant la lecture
  std::string texte = texte0;
  for (std::size_t i = texte.find("(string_quote"); i != std::string::npos; i = texte.find("(string_quote", i)) {
    const std::size_t f = texte.find(')', i + 14);
    if (f == std::string::npos) break;
    texte.erase(i, f - i + 1);
  }
  Expr racine;
  try {
    racine = Lecteur(texte).lire();
  } catch (const std::exception& e) {
    r.erreurs.push_back(std::string("session illisible : ") + e.what());
    return r;
  }
  const Expr* routes = racine.enfant("routes");
  if (!routes) {
    r.erreurs.push_back("session sans « routes »");
    return r;
  }
  // Unité : « (resolution um 10) » → une unité vaut 0,1 µm
  double nmParUnite = 100;
  if (const Expr* res = routes->enfant("resolution")) {
    const std::string unite = res->valeur(1);
    const double f = std::max(1.0, res->nombre(2, 1));
    const double base = unite == "mil" ? 25'400 : unite == "mm" ? 1e6 : unite == "inch" ? 25.4e6 : unite == "cm" ? 1e7 : 1000;
    nmParUnite = base / f;
  }
  auto nmDe = [&](double v) { return static_cast<Nm>(std::llround(v * nmParUnite)); };
  std::map<std::string, Nm> diametres;
  if (const Expr* lib = routes->enfant("library_out"))
    for (const auto& p : lib->elements)
      if (p.nom() == "padstack")
        for (const auto& sh : p.elements)
          if (sh.nom() == "shape")
            if (const Expr* cercle = sh.enfant("circle")) diametres[p.valeur(1)] = std::max(diametres[p.valeur(1)], nmDe(cercle->nombre(2)));
  const Expr* net = routes->enfant("network_out");
  if (!net) return r;
  for (const auto& n : net->elements) {
    if (n.nom() != "net") continue;
    const std::string reseau = n.valeur(1);
    for (const auto& w : n.elements) {
      if (w.nom() == "wire")
        if (const Expr* path = w.enfant("path")) {
          const auto couche = coucheDepuisCode(path->valeur(1));
          if (!couche) {
            r.erreurs.push_back("couche inconnue « " + path->valeur(1) + " »");
            continue;
          }
          const Nm largeur = nmDe(path->nombre(2));
          std::vector<Pt> points;
          for (std::size_t i = 3; i + 1 < path->elements.size(); i += 2) points.push_back({nmDe(path->nombre(i)), -nmDe(path->nombre(i + 1))});
          for (std::size_t i = 1; i < points.size(); ++i) r.pistes.push_back({nouvelUuid(), reseau, *couche, points[i - 1], points[i], largeur});
        }
      if (w.nom() == "via") {
        const std::string nom = w.valeur(1);
        Via v{nouvelUuid(), reseau, {nmDe(w.nombre(2)), -nmDe(w.nombre(3))}};
        if (const auto it = diametres.find(nom); it != diametres.end() && it->second > 0) v.diametre = it->second;
        // « Via[0-1]_800:400_um » : diamètre et perçage en micromètres
        if (const auto a = nom.find('_'), b = nom.find(':'); a != std::string::npos && b != std::string::npos && b > a) {
          try {
            v.diametre = std::stoll(nom.substr(a + 1, b - a - 1)) * 1000;
            v.percage = std::stoll(nom.substr(b + 1)) * 1000;
          } catch (const std::exception&) {
          }
        }
        r.vias.push_back(v);
      }
    }
  }
  return r;
}

int appliquerSessionSpecctra(ConceptionElec& c, const SessionSpecctra& s) {
  exigerModifiable(c);
  const Connectivite k = connectivite(c);
  std::set<std::string> routes;
  for (const auto& t : s.pistes) routes.insert(k.canonique(t.reseau));
  for (const auto& v : s.vias) routes.insert(k.canonique(v.reseau));
  std::erase_if(c.circuit.pistes, [&](const Piste& t) { return routes.count(k.canonique(t.reseau)) > 0; });
  std::erase_if(c.circuit.vias, [&](const Via& v) { return routes.count(k.canonique(v.reseau)) > 0; });
  for (const auto& t : s.pistes) c.circuit.pistes.push_back(t);
  for (const auto& v : s.vias) c.circuit.vias.push_back(v);
  return static_cast<int>(s.pistes.size());
}

// ---------- BoardView ----------
std::string boardView(const ConceptionElec& c) {
  const Polygone carte = c.circuit.contour.polygone();
  const Boite b = boite(carte);
  auto mil = [](Nm v) { return static_cast<long long>(std::llround(static_cast<double>(v) / 25'400.0)); };
  auto X = [&](Nm x) { return mil(x - b.x0); };
  auto Y = [&](Nm y) { return mil(b.y1 - y); };
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  std::map<std::string, int> reseaux;
  for (const auto& p : pp) reseaux.emplace(p.reseau.empty() ? std::string("UNCONNECTED") : p.reseau, 0);
  int i = 0;
  for (auto& [n, id] : reseaux) id = ++i;
  std::ostringstream s;
  s << "BRDOUT: " << carte.size() + 1 << " " << mil(b.largeur()) << " " << mil(b.hauteur()) << "\n";
  for (const Pt p : carte) s << X(p.x) << " " << Y(p.y) << "\n";
  if (!carte.empty()) s << X(carte.front().x) << " " << Y(carte.front().y) << "\n";
  s << "\nNETS: " << reseaux.size() << "\n";
  for (const auto& [n, id] : reseaux) s << id << " " << n << "\n";
  std::map<int, std::vector<const PastillePlacee*>> parEmpreinte;
  for (const auto& p : pp)
    if (!p.reference.empty()) parEmpreinte[p.empreinte].push_back(&p);
  s << "\nPARTS: " << parEmpreinte.size() << "\n";
  std::size_t fin = 0;
  std::ostringstream broches;
  for (const auto& [e, liste] : parEmpreinte) {
    Boite bb;
    for (const PastillePlacee* p : liste) bb.etendre(boite(p->forme));
    fin += liste.size();
    const bool dessous = c.circuit.empreintes[static_cast<std::size_t>(e)].dessous;
    s << liste.front()->reference << " " << X(bb.x0) << " " << Y(bb.y1) << " " << X(bb.x1) << " " << Y(bb.y0) << " " << fin << " " << (dessous ? 2 : 1) << "\n";
    for (const PastillePlacee* p : liste)
      broches << X(p->pastille.position.x) << " " << Y(p->pastille.position.y) << " " << reseaux[p->reseau.empty() ? std::string("UNCONNECTED") : p->reseau] << " " << (dessous ? 2 : 1) << "\n";
  }
  s << "\nPINS: " << fin << "\n" << broches.str() << "\nNAILS: 0\n";
  return s.str();
}

// ---------- IPC-2581 ----------
std::string ipc2581(const ConceptionElec& c, const std::string& version) {
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const Polygone carte = c.circuit.contour.polygone();
  const Boite b = boite(carte);
  auto X = [&](Nm x) { return fmt(mm(x - b.x0), 4); };
  auto Y = [&](Nm y) { return fmt(mm(b.y1 - y), 4); };
  auto nomCoucheIpc = [](Couche x) { return std::string(codeCouche(x)); };
  const std::vector<Couche> cuivres = couchesCuivre(c.circuit.empilage.couchesCuivre);
  std::ostringstream s;
  s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<IPC-2581 revision=\"B\" xmlns=\"http://webstds.ipc.org/2581\">\n";
  s << "  <Content roleRef=\"Proprietaire\">\n    <FunctionMode mode=\"ASSEMBLY\"/>\n    <StepRef name=\"carte\"/>\n";
  for (Couche x : cuivres) s << "    <LayerRef name=\"" << nomCoucheIpc(x) << "\"/>\n";
  s << "    <BomRef name=\"nomenclature\"/>\n  </Content>\n";
  s << "  <LogisticHeader>\n    <Role id=\"Proprietaire\" roleFunction=\"SENDER\"/>\n    <Enterprise id=\"Promethee\" code=\"NONE\"/>\n  </LogisticHeader>\n";
  s << "  <HistoryRecord number=\"1\" software=\"Promethee\"><FileRevision fileRevisionId=\"1\"><SoftwarePackage name=\"Promethee\" revision=\"" << xml(version.empty() ? infosVersion().version : version)
    << "\" vendor=\"Promethee\"/></FileRevision></HistoryRecord>\n";
  s << "  <Bom name=\"nomenclature\">\n    <BomHeader assembly=\"carte\" revision=\"1\"><StepRef name=\"carte\"/></BomHeader>\n";
  for (const auto& l : nomenclature(c)) {
    s << "    <BomItem OEMDesignNumberRef=\"" << xml(l.valeur + " " + l.empreinte) << "\" quantity=\"" << l.quantite << "\" category=\"ELECTRICAL\">\n";
    for (const auto& r : l.references) s << "      <RefDes name=\"" << xml(r) << "\" packageRef=\"" << xml(l.empreinte) << "\" populate=\"true\" layerRef=\"" << nomCoucheIpc(cuivres.front()) << "\"/>\n";
    s << "      <Characteristics category=\"ELECTRICAL\"/>\n    </BomItem>\n";
  }
  s << "  </Bom>\n  <Ecad name=\"carte\">\n    <CadHeader units=\"MILLIMETER\"/>\n    <CadData>\n";
  for (Couche x : cuivres)
    s << "      <Layer name=\"" << nomCoucheIpc(x) << "\" layerFunction=\"SIGNAL\" side=\"" << (x == Couche::CuivreDessus ? "TOP" : x == Couche::CuivreDessous ? "BOTTOM" : "INTERNAL") << "\" polarity=\"POSITIVE\"/>\n";
  s << "      <Step name=\"carte\">\n        <Datum x=\"0\" y=\"0\"/>\n        <Profile><Polygon>";
  for (std::size_t i = 0; i <= carte.size() && !carte.empty(); ++i) {
    const Pt p = carte[i % carte.size()];
    s << (i == 0 ? "<PolyBegin" : "<PolyStepSegment") << " x=\"" << X(p.x) << "\" y=\"" << Y(p.y) << "\"/>";
  }
  s << "</Polygon></Profile>\n";
  std::set<std::string> paquets;
  for (const auto& e : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!d || !paquets.insert(e.empreinte).second) continue;
    s << "        <Package name=\"" << xml(e.empreinte) << "\" type=\"OTHER\" pinOne=\"1\">\n";
    for (const auto& p : d->pastilles)
      s << "          <Pin number=\"" << xml(p.numero) << "\" type=\"" << (p.type == TypePastille::CMS ? "SURFACE" : "THRU") << "\"><Location x=\"" << fmt(mm(p.position.x), 4) << "\" y=\""
        << fmt(-mm(p.position.y), 4) << "\"/></Pin>\n";
    s << "        </Package>\n";
  }
  for (const auto& e : c.circuit.empreintes) {
    const ComposantElec* x = c.composant(e.composant);
    if (!x) continue;
    s << "        <Component refDes=\"" << xml(x->reference) << "\" packageRef=\"" << xml(e.empreinte) << "\" layerRef=\"" << (e.dessous ? nomCoucheIpc(cuivres.back()) : nomCoucheIpc(cuivres.front()))
      << "\" part=\"" << xml(x->valeur) << "\"><Xform rotation=\"" << fmt(e.rotation, 2) << "\"" << (e.dessous ? " mirror=\"true\"" : "") << "/><Location x=\"" << X(e.position.x) << "\" y=\""
      << Y(e.position.y) << "\"/></Component>\n";
  }
  std::map<std::string, std::vector<const PastillePlacee*>> parReseau;
  for (const auto& p : pp)
    if (!p.reseau.empty() && !p.reference.empty()) parReseau[p.reseau].push_back(&p);
  for (const auto& [n, l] : parReseau) {
    s << "        <LogicalNet name=\"" << xml(n) << "\">";
    for (const PastillePlacee* p : l) s << "<PinRef componentRef=\"" << xml(p->reference) << "\" pin=\"" << xml(p->numero) << "\"/>";
    s << "</LogicalNet>\n";
  }
  for (Couche x : cuivres) {
    s << "        <LayerFeature layerRef=\"" << nomCoucheIpc(x) << "\">\n";
    std::map<std::string, std::vector<const Piste*>> pistes;
    for (const auto& t : c.circuit.pistes)
      if (t.couche == x) pistes[k.canonique(t.reseau)].push_back(&t);
    for (const auto& [n, l] : pistes) {
      s << "          <Set net=\"" << xml(n) << "\"><Features>";
      for (const Piste* t : l)
        s << "<Line startX=\"" << X(t->a.x) << "\" startY=\"" << Y(t->a.y) << "\" endX=\"" << X(t->b.x) << "\" endY=\"" << Y(t->b.y) << "\"><LineDesc lineEnd=\"ROUND\" lineWidth=\"" << fmt(mm(t->largeur), 4)
          << "\"/></Line>";
      s << "</Features></Set>\n";
    }
    s << "        </LayerFeature>\n";
  }
  s << "      </Step>\n    </CadData>\n  </Ecad>\n</IPC-2581>\n";
  return s.str();
}

// ---------- Carte relue ----------
CarteRelue relireCarte(const std::map<Couche, std::string>& gerbers, const std::string& percagesMetallises) {
  CarteRelue r;
  struct Morceau {
    Couche couche;
    Polygone forme;
    Boite b;
    int pastille = -1;
    std::string reseauNomme;
  };
  std::vector<Morceau> morceaux;
  std::vector<std::pair<Couche, LectureGerber>> lectures;
  bool avecX2 = false;  // des pastilles repérées (%TO.P) : un flash sans repère est une via, pas une pastille
  for (const auto& [couche, texte] : gerbers) {
    if (!estCuivre(couche)) continue;
    lectures.push_back({couche, lireGerber(texte)});
    for (const auto& o : lectures.back().second.objets) avecX2 |= !o.reference.empty();
  }
  for (const auto& [couche, l] : lectures) {
    for (const auto& e : l.erreurs) r.erreurs.push_back(std::string(nomCouche(couche)) + " : " + e);
    for (const auto& o : l.objets) {
      Morceau m{couche, o.forme, boite(o.forme), -1, o.reseau};
      if (o.genre == ObjetGerber::Genre::Trait) r.pistes.push_back({nouvelUuid(), o.reseau, couche, o.a, o.b, o.largeur});
      // Une pastille : un flash repéré, ou une région repérée ; sans attributs X2, tout flash et toute petite région
      // (moins de 6 mm de côté : une pastille tournée, pas un plan)
      const bool petite = std::max(m.b.largeur(), m.b.hauteur()) <= 6 * NM_PAR_MM;
      const bool pastille = avecX2 ? !o.reference.empty() && o.genre != ObjetGerber::Genre::Trait
                                   : o.genre == ObjetGerber::Genre::Flash || (o.genre == ObjetGerber::Genre::Region && petite);
      if (pastille) {
        PastilleRelue p;
        p.position = o.genre == ObjetGerber::Genre::Flash ? o.a : m.b.centre();
        p.forme = o.forme;
        p.couches = bit(couche);
        p.reference = o.reference;
        p.numero = o.numero;
        m.pastille = static_cast<int>(r.pastilles.size());
        r.pastilles.push_back(p);
      }
      morceaux.push_back(std::move(m));
    }
  }
  // Ensembles disjoints sur l'index spatial : le cuivre qui se touche, puis ce que relie chaque perçage métallisé
  std::vector<int> parent(morceaux.size());
  std::iota(parent.begin(), parent.end(), 0);
  std::function<int(int)> racine = [&](int x) {
    while (parent[static_cast<std::size_t>(x)] != x) x = parent[static_cast<std::size_t>(x)] = parent[static_cast<std::size_t>(parent[static_cast<std::size_t>(x)])];
    return x;
  };
  auto unir = [&](int a, int b) { parent[static_cast<std::size_t>(racine(a))] = racine(b); };
  IndexSpatial index(2 * NM_PAR_MM);
  for (std::size_t i = 0; i < morceaux.size(); ++i) index.inserer(static_cast<int>(i), morceaux[i].b);
  for (std::size_t i = 0; i < morceaux.size(); ++i)
    for (int j : index.chercher(morceaux[i].b)) {
      if (static_cast<std::size_t>(j) <= i || morceaux[static_cast<std::size_t>(j)].couche != morceaux[i].couche || !morceaux[i].b.touche(morceaux[static_cast<std::size_t>(j)].b)) continue;
      if (distancePolygones(morceaux[i].forme, morceaux[static_cast<std::size_t>(j)].forme) <= 0) unir(static_cast<int>(i), j);
    }
  for (const auto& [p, d] : lirePercages(percagesMetallises)) {
    Boite bp;
    bp.etendre(p);
    int premier = -1;
    bool pastille = false;
    for (int j : index.chercher(bp.gonflee(d / 2))) {
      if (!dedans(morceaux[static_cast<std::size_t>(j)].forme, p)) continue;
      pastille |= morceaux[static_cast<std::size_t>(j)].pastille >= 0 && !r.pastilles[static_cast<std::size_t>(morceaux[static_cast<std::size_t>(j)].pastille)].reference.empty();
      if (premier < 0) premier = j;
      else unir(premier, j);
    }
    // Un perçage sans pastille repérée : une via
    if (!pastille && premier >= 0) r.vias.push_back({nouvelUuid(), morceaux[static_cast<std::size_t>(premier)].reseauNomme, p, std::max<Nm>(d + 300'000, 600'000), d});
  }
  std::map<int, int> numeros;
  for (std::size_t i = 0; i < morceaux.size(); ++i) {
    const int rac = racine(static_cast<int>(i));
    const auto [it, neuf] = numeros.emplace(rac, static_cast<int>(numeros.size()));
    if (morceaux[i].pastille >= 0) {
      PastilleRelue& p = r.pastilles[static_cast<std::size_t>(morceaux[i].pastille)];
      p.reseau = it->second;
    }
  }
  // Une même pastille sur deux couches (traversante) : un seul objet, ses couches réunies
  std::vector<PastilleRelue> uniques;
  for (const auto& p : r.pastilles) {
    auto it = std::find_if(uniques.begin(), uniques.end(), [&](const PastilleRelue& q) { return q.position == p.position && q.reference == p.reference && q.numero == p.numero; });
    if (it == uniques.end()) uniques.push_back(p);
    else it->couches |= p.couches;
  }
  r.pastilles = std::move(uniques);
  r.reseaux = static_cast<int>(numeros.size());
  return r;
}

NetlistLue netlistDepuisCarteRelue(const CarteRelue& c) {
  NetlistLue n;
  std::set<std::string> vus;
  int libre = 0;
  for (const auto& p : c.pastilles) {
    std::string ref = p.reference, numero = p.numero;
    if (ref.empty()) {
      ref = "P" + std::to_string(++libre);
      numero = "1";
    }
    if (vus.insert(ref).second) n.composants.push_back({ref, {}, {}});
    if (p.reseau >= 0) n.reseaux["N" + std::to_string(p.reseau + 1)].push_back({ref, numero.empty() ? std::string("1") : numero});
  }
  // Un réseau d'une seule broche n'en est pas un
  std::erase_if(n.reseaux, [](const auto& x) { return x.second.size() < 2; });
  return n;
}

ConceptionElec conceptionDepuisCarteRelue(const CarteRelue& r, const std::string& nom, const std::string& contourGerber, const std::string& percagesMetallises) {
  const NetlistLue n = netlistDepuisCarteRelue(r);
  // Repère de la carte : celui de fabrication (Y vers le haut) retourné, l'origine en haut à gauche du cuivre
  Boite b;
  for (const auto& p : r.pastilles) b.etendre(boite(p.forme));
  for (const auto& t : r.pistes) {
    b.etendre(t.a);
    b.etendre(t.b);
  }
  Polygone contour;
  if (!contourGerber.empty()) {
    const LectureGerber l = lireGerber(contourGerber);
    for (const auto& o : l.objets)
      if (o.genre == ObjetGerber::Genre::Trait) {
        if (contour.empty()) contour.push_back(o.a);
        contour.push_back(o.b);
      }
    if (contour.size() > 2 && contour.front() == contour.back()) contour.pop_back();
    for (const Pt q : contour) b.etendre(q);
  }
  if (b.vide()) return conceptionDepuisNetlist(n, nom);
  const Nm marge = contour.size() >= 3 ? 0 : 2 * NM_PAR_MM;
  const Nm haut = b.y1 + marge, gauche = b.x0 - marge;
  auto versCarte = [&](Pt p) { return Pt{p.x - gauche, haut - p.y}; };
  ConceptionElec c = conceptionDepuisNetlist(n, nom);
  // Perçages : traversantes et leur diamètre
  std::vector<std::pair<Pt, Nm>> percages = lirePercages(percagesMetallises);
  auto percageEn = [&](Pt p) -> Nm {
    for (const auto& [q, d] : percages)
      if (std::abs(q.x - p.x) <= 2000 && std::abs(q.y - p.y) <= 2000) return d;
    return 0;
  };
  // Empreintes tirées des pastilles, groupées comme la netlist (repère X2, sinon une par pastille)
  std::map<std::string, std::vector<const PastilleRelue*>> groupes;
  int libre = 0;
  for (const auto& p : r.pastilles) groupes[p.reference.empty() ? "P" + std::to_string(++libre) : p.reference].push_back(&p);
  for (const auto& [ref, liste] : groupes) {
    ComposantElec* x = nullptr;
    for (auto& k : c.composants)
      if (k.reference == ref) x = &k;
    if (!x) continue;
    Boite bg;
    for (const PastilleRelue* p : liste) bg.etendre(p->position);
    const Pt centre = versCarte(bg.centre());
    DefEmpreinte d;
    d.id = "gerber:" + ref;
    d.description = "Empreinte relue des fichiers de fabrication";
    d.famille = "Relue";
    int numero = 0;
    Boite enc;
    for (const PastilleRelue* p : liste) {
      const Boite bp = boite(p->forme);
      Pastille pa;
      pa.numero = p->numero.empty() ? std::to_string(++numero) : p->numero;
      const Nm perc = percageEn(p->position);
      pa.type = perc > 0 ? TypePastille::Traversante : TypePastille::CMS;
      pa.percage = perc;
      pa.forme = FormePastille::Rect;
      pa.largeur = std::max<Nm>(bp.largeur(), 100'000);
      pa.hauteur = std::max<Nm>(bp.hauteur(), 100'000);
      const Pt q = versCarte(p->position);
      pa.position = {q.x - centre.x, q.y - centre.y};
      if (pa.type == TypePastille::CMS && !(p->couches & bit(Couche::CuivreDessus))) pa.couches = bit(Couche::CuivreDessous) | bit(Couche::VernisDessous) | bit(Couche::PateDessous);
      enc.etendre(Boite{pa.position.x - pa.largeur / 2, pa.position.y - pa.hauteur / 2, pa.position.x + pa.largeur / 2, pa.position.y + pa.hauteur / 2});
      d.pastilles.push_back(pa);
    }
    d.encombrement = enc.gonflee(250'000);
    c.empreintes[d.id] = d;
    x->empreinte = d.id;
  }
  mettreAJourCircuit(c);
  for (auto& e : c.circuit.empreintes)
    if (const DefEmpreinte* d = c.empreinte(e.empreinte); d && d->id.rfind("gerber:", 0) == 0)
      for (const auto& [ref, liste] : groupes)
        if (d->id == "gerber:" + ref) {
          Boite bg;
          for (const PastilleRelue* p : liste) bg.etendre(p->position);
          e.position = versCarte(bg.centre());
          e.rotation = 0;
          e.dessous = false;
        }
  // Pistes et vias, au réseau de la conception (lu sur les pastilles qu'elles touchent, à la reconstruction suivante)
  for (const auto& t : r.pistes) c.circuit.pistes.push_back({nouvelUuid(), {}, t.couche, versCarte(t.a), versCarte(t.b), t.largeur});
  for (const auto& v : r.vias) c.circuit.vias.push_back({nouvelUuid(), {}, versCarte(v.p), v.diametre, v.percage});
  c.circuit.contour.origine = {0, 0};
  c.circuit.contour.largeur = b.largeur() + 2 * marge;
  c.circuit.contour.hauteur = b.hauteur() + 2 * marge;
  c.circuit.contour.rayon = 0;
  if (contour.size() >= 3) {
    Polygone libre2;
    for (const Pt q : contour) libre2.push_back(versCarte(q));
    c.circuit.contour.libre = libre2;
  }
  // Réseau des pistes et vias : celui de la pastille qu'elles touchent par le cuivre
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  for (int passe = 0; passe < 4; ++passe) {
    bool change = false;
    auto reseauEn = [&](Pt q, Couches couches) -> std::string {
      for (const auto& p : pp)
        if ((p.couches & couches) && dedans(p.forme, q)) return p.reseau;
      for (const auto& t : c.circuit.pistes)
        if (!t.reseau.empty() && (bit(t.couche) & couches) && distancePointSegment(q, t.a, t.b) <= static_cast<double>(t.largeur) / 2) return t.reseau;
      for (const auto& v : c.circuit.vias)
        if (!v.reseau.empty() && distance(q, v.p) <= static_cast<double>(v.diametre) / 2) return v.reseau;
      return {};
    };
    for (auto& t : c.circuit.pistes)
      if (t.reseau.empty())
        for (const Pt q : {t.a, t.b})
          if (const std::string n2 = reseauEn(q, bit(t.couche)); !n2.empty()) {
            t.reseau = n2;
            change = true;
            break;
          }
    for (auto& v : c.circuit.vias)
      if (v.reseau.empty())
        if (const std::string n2 = reseauEn(v.p, tousLesCuivres(c.circuit.empilage.couchesCuivre)); !n2.empty()) {
          v.reseau = n2;
          change = true;
        }
    if (!change) break;
  }
  return c;
}

}  // namespace prom::ecad
