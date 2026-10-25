// Prométhée : formats anciens (10.18 ; ECH-23, ECH-24) : cartes Protel Autotrax (.PCB, DOS), cartes et empreintes
// gEDA PCB et Lepton (.pcb, .fp), schémas gschem et lepton-schematic (.sch, symboles .sym), cartes et macros
// Sprint-Layout (.lay6, .lmk). D'après les formats que lisent les importeurs de KiCad et de pcb-rnd (GPL), servant de
// référence, sans en reprendre le code. Une carte sans netlist (Autotrax, Sprint-Layout) tire ses réseaux du cuivre
// connecté. Voir formats_anciens.hpp. Licence GPL-3.0-only.
#include "promethee/formats_anciens.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

#include "cao_interne.hpp"
#include "kicad_interne.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/police.hpp"

namespace prom::ecad {

// ======================================================================================================================
// Outils communs : réseaux tirés du cuivre, contour assemblé, textes Windows
// ======================================================================================================================
void cao_interne::reseauxDepuisCuivre(CarteLue& l, const LiaisonsCuivre& imposees) {
  struct Morceau {
    Polygone forme;
    Couches couches = 0;
    int pose = -1;  // pastille : rang de la pose
    std::string numero;
    int piste = -1, via = -1, region = -1;
  };
  std::vector<Morceau> m;
  const Couches tous = tousLesCuivres(l.cuivres);
  std::map<Broche, std::vector<int>> parBroche;
  std::vector<int> debutPiste(l.pistes.size(), -1), debutRegion(l.regions.size(), -1);
  for (std::size_t i = 0; i < l.poses.size(); ++i) {
    const auto& p = l.poses[i];
    const auto d = l.empreintes.find(p.decal);
    if (d == l.empreintes.end()) continue;
    EmpreintePlacee e;
    e.position = p.p;
    e.rotation = p.rotation;
    e.dessous = p.dessous;
    for (const Pastille& q : d->second.pastilles) {
      if (q.numero.empty()) continue;
      const Pastille s = pastilleSurCarte(e, q);
      Morceau x;
      x.forme = formePastille(s);
      x.couches = couchesPastille(q, p.dessous, l.cuivres) & tous;
      x.pose = static_cast<int>(i);
      x.numero = q.numero;
      parBroche[{p.ref, q.numero}].push_back(static_cast<int>(m.size()));
      m.push_back(x);
    }
  }
  for (std::size_t i = 0; i < l.pistes.size(); ++i) {
    const Piste& t = l.pistes[i];
    for (const auto& [a, b] : segmentsPiste(t, 20'000)) {
      Morceau x;
      x.forme = oblong(a, b, t.largeur);
      x.couches = bit(t.couche);
      x.piste = static_cast<int>(i);
      if (debutPiste[i] < 0) debutPiste[i] = static_cast<int>(m.size());
      m.push_back(x);
    }
  }
  for (std::size_t i = 0; i < l.vias.size(); ++i) {
    Morceau x;
    x.forme = cercle(l.vias[i].p, l.vias[i].diametre / 2);
    x.couches = tous;
    x.via = static_cast<int>(i);
    m.push_back(x);
  }
  for (std::size_t i = 0; i < l.regions.size(); ++i) {
    if (l.regions[i].forme.size() < 3) continue;
    Morceau x;
    x.forme = l.regions[i].forme;
    x.couches = bit(l.regions[i].couche);
    x.region = static_cast<int>(i);
    debutRegion[i] = static_cast<int>(m.size());
    m.push_back(x);
  }
  std::vector<int> parent(m.size());
  for (std::size_t i = 0; i < m.size(); ++i) parent[i] = static_cast<int>(i);
  std::function<int(int)> racine = [&](int i) { return parent[static_cast<std::size_t>(i)] == i ? i : parent[static_cast<std::size_t>(i)] = racine(parent[static_cast<std::size_t>(i)]); };
  auto unir = [&](int a, int b) {
    if (a >= 0 && b >= 0) parent[static_cast<std::size_t>(racine(a))] = racine(b);
  };
  IndexSpatial index(5 * NM_PAR_MM);
  std::vector<Boite> boites(m.size());
  for (std::size_t i = 0; i < m.size(); ++i) index.inserer(static_cast<int>(i), boites[i] = boite(m[i].forme));
  for (std::size_t i = 0; i < m.size(); ++i)
    for (const int j : index.chercher(boites[i])) {
      const auto k = static_cast<std::size_t>(j);
      if (j <= static_cast<int>(i) || !(m[i].couches & m[k].couches) || !boites[i].touche(boites[k])) continue;
      if (racine(j) != racine(static_cast<int>(i)) && distancePolygones(m[i].forme, m[k].forme) <= 0) unir(j, static_cast<int>(i));
    }
  // Une broche est une : ses pastilles de même numéro sont reliées ; puis le chevelu du fichier
  for (const auto& [b, v] : parBroche)
    for (std::size_t i = 1; i < v.size(); ++i) unir(v[i], v[0]);
  for (const auto& [a, b] : imposees.chevelu) {
    const auto x = parBroche.find(a), y = parBroche.find(b);
    if (x != parBroche.end() && y != parBroche.end()) unir(x->second[0], y->second[0]);
  }
  // Réseaux imposés : tous leurs objets dans un même îlot, qui prend leur nom
  std::map<std::string, int> imposeA;
  auto imposer = [&](const std::string& nom, int i) {
    if (i < 0) return;
    const auto [it, neuf] = imposeA.try_emplace(nom, i);
    if (!neuf) unir(i, it->second);
  };
  for (const auto& [nom, v] : imposees.pastilles)
    for (const Broche& b : v)
      if (const auto it = parBroche.find(b); it != parBroche.end()) imposer(nom, it->second[0]);
  for (const auto& [nom, v] : imposees.pistes)
    for (const std::size_t i : v)
      if (i < debutPiste.size()) imposer(nom, debutPiste[i]);
  for (const auto& [nom, v] : imposees.regions)
    for (const std::size_t i : v)
      if (i < debutRegion.size()) imposer(nom, debutRegion[i]);
  std::map<int, std::string> noms;
  for (const auto& [nom, i] : imposeA) noms.try_emplace(racine(i), nom);
  // Un réseau par îlot portant une pastille, nommé « N$1 »… dans l'ordre des poses
  int n = 0;
  std::set<std::string> pris;
  for (const auto& [nom, i] : imposeA) pris.insert(nom);
  for (std::size_t i = 0; i < m.size(); ++i) {
    if (m[i].pose < 0) continue;
    const int r = racine(static_cast<int>(i));
    auto [it, neuf] = noms.try_emplace(r, std::string());
    if (neuf)
      do it->second = "N$" + std::to_string(++n);
      while (pris.count(it->second));
    auto& v = l.netlist.reseaux[it->second];
    const Broche b{l.poses[static_cast<std::size_t>(m[i].pose)].ref, m[i].numero};
    if (std::find(v.begin(), v.end(), b) == v.end()) v.push_back(b);
  }
  for (std::size_t i = 0; i < m.size(); ++i) {
    const auto it = noms.find(racine(static_cast<int>(i)));
    if (it == noms.end()) continue;
    if (m[i].piste >= 0) l.pistes[static_cast<std::size_t>(m[i].piste)].reseau = it->second;
    if (m[i].via >= 0) l.vias[static_cast<std::size_t>(m[i].via)].reseau = it->second;
    if (m[i].region >= 0) l.regions[static_cast<std::size_t>(m[i].region)].reseau = it->second;
  }
}

bool cao_interne::contourDepuisTraits(CarteLue& l, const std::vector<std::pair<Pt, Pt>>& traits) {
  std::vector<ElementContour> morceaux;
  for (const auto& [a, b] : traits)
    if (!(a == b)) morceaux.push_back({{mm(a.x), mm(a.y)}, {mm(b.x), mm(b.y)}, std::nullopt});
  bool premier = true;
  for (const auto& ct : kicad_interne::chainer(morceaux)) {
    if (!ct.ferme) continue;
    Polygone q;
    for (const Point& x : ct.polygone(0.1)) {
      const Pt y = ptMm(x.x, x.y);
      if (q.empty() || !(q.back() == y)) q.push_back(y);
    }
    while (q.size() > 1 && q.front() == q.back()) q.pop_back();
    if (q.size() < 3) continue;
    if (premier) {
      l.contour = q;
      premier = false;
    } else
      l.decoupes.push_back(q);
  }
  return !premier;
}

namespace {
void ajouterUtf8(std::string& s, char32_t c) {
  if (c < 0x80) s += static_cast<char>(c);
  else if (c < 0x800) {
    s += static_cast<char>(0xC0 | (c >> 6));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    s += static_cast<char>(0xE0 | (c >> 12));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  }
}
bool estUtf8(const std::string& s) {
  for (std::size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (n == 0 || i + n > s.size()) return false;
    for (std::size_t k = 1; k < n; ++k)
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    i += n;
  }
  return true;
}
}  // namespace

std::string cao_interne::texteWindows(const std::string& octets) {
  if (estUtf8(octets)) return octets;
  // Windows-1252, de 0x80 à 0x9F (le reste est Latin-1) ; Windows-1251, de 0x80 à 0xBF (puis А…я)
  static constexpr char16_t ouest[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
                                         0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
  static constexpr char16_t cyrillique[64] = {
      0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
      0x2013, 0x2014, 0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB,
      0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};
  // Un mot cyrillique n'a guère que des octets hauts (0xC0 et plus), un mot de l'Ouest surtout des lettres ASCII ;
  // les lettres de l'allemand (ÄÖÜäöüß) ne comptent pas, comme dans KiCad
  int hautes = 0, ascii = 0;
  for (const char ch : octets) {
    const auto c = static_cast<unsigned char>(ch);
    if (std::isalpha(c) && c < 0x80) ++ascii;
    else if (c >= 0xC0 && c != 0xC4 && c != 0xD6 && c != 0xDC && c != 0xE4 && c != 0xF6 && c != 0xFC && c != 0xDF) ++hautes;
  }
  const bool cyr = hautes > ascii;
  std::string r;
  for (const char ch : octets) {
    const auto c = static_cast<unsigned char>(ch);
    if (c < 0x80) r += ch;
    else if (cyr) ajouterUtf8(r, c < 0xC0 ? cyrillique[c - 0x80] : static_cast<char32_t>(0x0410 + (c - 0xC0)));
    else ajouterUtf8(r, c < 0xA0 ? ouest[c - 0x80] : static_cast<char32_t>(c));
  }
  return r;
}

namespace {
std::vector<std::string> mots(const std::string& s) {
  std::vector<std::string> r;
  std::istringstream in(s);
  for (std::string x; in >> x;) r.push_back(x);
  return r;
}
double nombre(const std::string& s, double defaut = 0) {
  char* fin = nullptr;
  const double v = std::strtod(s.c_str(), &fin);
  return fin == s.c_str() ? defaut : v;
}
std::vector<std::string> lignesDe(const std::string& t) {
  std::vector<std::string> r;
  std::istringstream in(t);
  for (std::string l; std::getline(in, l);) {
    while (!l.empty() && (l.back() == '\r' || l.back() == ' ' || l.back() == '\t')) l.pop_back();
    r.push_back(l);
  }
  return r;
}
DefEmpreinte cadrer(DefEmpreinte d) {
  Boite b;
  for (const auto& p : d.pastilles) b.etendre(boite(formePastille(p)));
  for (const auto& t : d.traits)
    if (t.rayon > 0) b.etendre(Boite{t.a.x - t.rayon, t.a.y - t.rayon, t.a.x + t.rayon, t.a.y + t.rayon});
    else b.etendre(t.a), b.etendre(t.b);
  if (b.vide()) b = Boite{-NM_PAR_MM, -NM_PAR_MM, NM_PAR_MM, NM_PAR_MM};
  d.encombrement = b.gonflee(250'000);
  d.corps.largeur = d.encombrement.largeur();
  d.corps.profondeur = d.encombrement.hauteur();
  d.corps.hauteur = NM_PAR_MM;
  d.corps.decalage = d.encombrement.centre();
  return d;
}
bool proche(Pt a, Pt b, Nm t) { return std::abs(a.x - b.x) <= t && std::abs(a.y - b.y) <= t; }
// Deux définitions de même forme à l'arrondi près (positions des fichiers en flottants, rotations quelconques)
bool pareilles(const DefEmpreinte& a, const DefEmpreinte& b) {
  constexpr Nm t = 500;
  if (a.pastilles.size() != b.pastilles.size() || a.traits.size() != b.traits.size()) return false;
  for (std::size_t j = 0; j < a.pastilles.size(); ++j) {
    const Pastille &x = a.pastilles[j], &y = b.pastilles[j];
    if (x.numero != y.numero || x.forme != y.forme || x.type != y.type || x.couches != y.couches || !proche(x.position, y.position, t) || std::abs(x.largeur - y.largeur) > t ||
        std::abs(x.hauteur - y.hauteur) > t || std::abs(x.percage - y.percage) > t || x.contourLibre.size() != y.contourLibre.size())
      return false;
    if (x.forme != FormePastille::Rond && std::abs(std::remainder(x.rotation - y.rotation, x.forme == FormePastille::Libre ? 360.0 : 180.0)) > 0.5) return false;
    for (std::size_t k = 0; k < x.contourLibre.size(); ++k)
      if (!proche(x.contourLibre[k], y.contourLibre[k], t)) return false;
  }
  for (std::size_t j = 0; j < a.traits.size(); ++j) {
    const TraitEmpreinte &x = a.traits[j], &y = b.traits[j];
    if (x.couche != y.couche || !proche(x.a, y.a, t) || !proche(x.b, y.b, t) || std::abs(x.rayon - y.rayon) > t) return false;
  }
  return true;
}
// Empreintes d'une carte lue, dédoublonnées : une même forme sous un même nom partage la définition, une autre forme
// prend « nom~2 », « nom~3 »…
class Empreintes {
 public:
  explicit Empreintes(cao_interne::CarteLue& l) : l_(l) {}
  std::string ranger(const std::string& base, DefEmpreinte d) {
    auto& v = variantes_[base];
    for (const std::string& id : v)
      if (pareilles(l_.empreintes[id], d)) return id;
    const std::string id = v.empty() ? base : base + "~" + std::to_string(v.size() + 1);
    if (d.description.empty()) d.description = base;
    l_.empreintes[id] = cadrer(std::move(d));
    v.push_back(id);
    return id;
  }

 private:
  cao_interne::CarteLue& l_;
  std::map<std::string, std::vector<std::string>> variantes_;
};
// Repère unique : vide ou déjà pris, il reçoit un suffixe (« C1-2 ») inscrit au rapport
std::string repereUnique(std::set<std::string>& pris, std::string ref, const std::string& defaut, RapportLectureCao& r) {
  if (ref.empty()) ref = defaut;
  if (pris.insert(ref).second) return ref;
  for (int k = 2;; ++k)
    if (const std::string x = ref + "-" + std::to_string(k); pris.insert(x).second) {
      r.alertes.push_back("repère « " + ref + " » en double : le suivant devient « " + x + " »");
      return x;
    }
}
// Cuivre d'une carte dont la netlist est connue : chaque îlot prend le réseau des pastilles qu'il touche
void cuivreSurNetlist(cao_interne::CarteLue& l) {
  cao_interne::CarteLue copie = l;
  copie.netlist.reseaux.clear();
  cao_interne::reseauxDepuisCuivre(copie);
  std::map<cao_interne::Broche, std::string> parBroche;
  for (const auto& [n, v] : l.netlist.reseaux)
    for (const auto& b : v) parBroche[b] = n;
  std::map<std::string, std::string> vers;  // îlot → réseau du fichier
  for (const auto& [ilot, broches] : copie.netlist.reseaux)
    for (const auto& b : broches)
      if (const auto it = parBroche.find(b); it != parBroche.end()) vers.try_emplace(ilot, it->second);
  auto nomDe = [&](const std::string& ilot) {
    const auto it = vers.find(ilot);
    return it == vers.end() ? std::string() : it->second;
  };
  for (std::size_t k = 0; k < l.pistes.size(); ++k) l.pistes[k].reseau = nomDe(copie.pistes[k].reseau);
  for (std::size_t k = 0; k < l.vias.size(); ++k) l.vias[k].reseau = nomDe(copie.vias[k].reseau);
  for (std::size_t k = 0; k < l.regions.size(); ++k) l.regions[k].reseau = nomDe(copie.regions[k].reseau);
  // Pièces de la carte seule : réseau de l'îlot
  for (const auto& [ilot, broches] : copie.netlist.reseaux)
    for (const auto& b : broches)
      if (l.carteSeule.count(b.first) && !nomDe(ilot).empty()) l.netlist.reseaux[nomDe(ilot)].push_back(b);
}
// Texte posé à la taille de sa boîte d'origine : la police de Prométhée diffère de celle du fichier ; la hauteur est
// réduite tant que le texte dépasse la longueur d'origine, pour qu'un texte de cuivre ne déborde jamais sur un voisin
void ajusterTexte(TexteCircuit& t, double longueur, double hauteur) {
  t.hauteur = static_cast<Nm>(std::llround(hauteur));
  const double l = static_cast<double>(largeurTexte(t.texte, t.hauteur));
  if (l > longueur && l > 0 && longueur > 0) t.hauteur = static_cast<Nm>(std::floor(static_cast<double>(t.hauteur) * longueur / l));
  t.hauteur = std::max<Nm>(t.hauteur, 100'000);
}
// Arc découpé en cordes (centre, rayon, angles en degrés dans le sens direct, repère y vers le bas)
std::vector<Pt> pointsArcDegres(Pt c, double r, double debut, double balayage) {
  std::vector<Pt> v;
  const int n = std::max(2, static_cast<int>(std::ceil(std::abs(balayage) / 10)));
  for (int k = 0; k <= n; ++k) {
    const double a = (debut + balayage * k / n) * std::numbers::pi / 180;
    v.push_back({c.x + static_cast<Nm>(std::llround(r * std::cos(a))), c.y - static_cast<Nm>(std::llround(r * std::sin(a)))});
  }
  return v;
}
}  // namespace

// ======================================================================================================================
// Protel Autotrax et Easytrax
// ======================================================================================================================
bool estAutotrax(const std::string& t) {
  for (const auto& l : lignesDe(t)) {
    const auto p = l.find_first_not_of(" \t");
    if (p == std::string::npos || l[p] == '#') continue;
    return l.compare(p, 10, "PCB FILE 4") == 0 || l.compare(p, 10, "PCB FILE 5") == 0;
  }
  return false;
}

namespace {
namespace autotrax {
// Enregistrements, en mils, y vers le haut ; les lignes vides comptent (en-tête positionnel des composants)
struct Trait {
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0, largeur = 0;
  int couche = 0;
};
struct Arc {
  double cx = 0, cy = 0, rayon = 0, largeur = 0;
  int quarts = 15, couche = 0;  // quarts : bits 0 à 3, haut droit, haut gauche, bas gauche, bas droit
};
struct TrouVia {
  double x = 0, y = 0, diametre = 0, percage = 0;
};
struct Pad {
  double x = 0, y = 0, lx = 0, ly = 0, percage = 0;
  int forme = 1, plans = 0, couche = 13;
  std::string nom;
};
struct Plein {
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  int couche = 0;
};
struct Texte {
  double x = 0, y = 0, hauteur = 0, largeur = 0;
  int sens = 0, couche = 0;
  bool miroir = false;
  std::string texte;
};
struct Contenu {
  std::vector<Trait> traits;
  std::vector<Arc> arcs;
  std::vector<TrouVia> vias;
  std::vector<Pad> pads;
  std::vector<Plein> pleins;
  std::vector<Texte> textes;
};
struct Composant : Contenu {
  std::string ref, motif, valeur;
  double x = 0, y = 0;
};
struct Carte : Contenu {
  int version = 4;
  std::vector<Composant> composants;
  std::map<std::string, std::vector<cao_interne::Broche>> reseaux;
};
class Lecteur {
 public:
  explicit Lecteur(const std::string& t) : L_(lignesDe(t)) {}
  Carte lire() {
    Carte c;
    while (auto l = ligne()) {
      const std::string k = net(*l);
      if (k.empty()) continue;
      if (k.rfind("PCB FILE 5", 0) == 0) c.version = 5;
      else if (k.rfind("ENDPCB", 0) == 0) break;
      else if (k.rfind("NETDEF", 0) == 0) reseau(c);
      else if (k.rfind("COMP", 0) == 0) c.composants.push_back(composant());
      else enregistrement(k, c, false);
    }
    return c;
  }

 private:
  std::vector<std::string> L_;
  std::size_t i_ = 0;
  std::optional<std::string> ligne() { return i_ < L_.size() ? std::optional<std::string>(L_[i_++]) : std::nullopt; }
  static std::string net(const std::string& s) {
    const auto a = s.find_first_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a);
  }
  std::vector<double> champs() {
    std::vector<double> v;
    if (const auto l = ligne())
      for (const auto& m : mots(*l)) v.push_back(nombre(m));
    return v;
  }
  static double a(const std::vector<double>& v, std::size_t k) { return k < v.size() ? v[k] : 0; }
  static int n(const std::vector<double>& v, std::size_t k) { return static_cast<int>(a(v, k)); }
  Texte texte(const std::vector<double>& v) {
    Texte t;
    t.x = a(v, 0), t.y = a(v, 1), t.hauteur = a(v, 2), t.largeur = a(v, 4), t.couche = n(v, 5);
    t.sens = n(v, 3) & 3;
    t.miroir = (n(v, 3) & 16) != 0;
    return t;
  }
  // Un enregistrement F… (libre) ou C… (d'un composant) : vrai s'il est connu
  bool enregistrement(const std::string& k, Contenu& c, bool composant) {
    if (k.size() != 2 || k[0] != (composant ? 'C' : 'F')) return false;
    switch (k[1]) {
      case 'T': {
        const auto v = champs();
        if (v.size() >= 6) c.traits.push_back({a(v, 0), a(v, 1), a(v, 2), a(v, 3), a(v, 4), n(v, 5)});
        return true;
      }
      case 'A': {
        const auto v = champs();
        if (v.size() >= 6) c.arcs.push_back({a(v, 0), a(v, 1), a(v, 2), a(v, 4), n(v, 3), n(v, 5)});
        return true;
      }
      case 'V': {
        const auto v = champs();
        if (v.size() >= 4) c.vias.push_back({a(v, 0), a(v, 1), a(v, 2), a(v, 3)});
        return true;
      }
      case 'F': {
        const auto v = champs();
        if (v.size() >= 5) c.pleins.push_back({a(v, 0), a(v, 1), a(v, 2), a(v, 3), n(v, 4)});
        return true;
      }
      case 'P': {
        const auto v = champs();
        Pad p;
        p.x = a(v, 0), p.y = a(v, 1), p.lx = a(v, 2), p.ly = a(v, 3), p.forme = n(v, 4), p.percage = a(v, 5), p.plans = n(v, 6), p.couche = n(v, 7);
        p.nom = net(ligne().value_or(""));
        if (v.size() >= 8) c.pads.push_back(p);
        return true;
      }
      case 'S': {
        Texte t = texte(champs());
        t.texte = ligne().value_or("");
        c.textes.push_back(t);
        return true;
      }
      default: return false;
    }
  }
  Composant composant() {
    Composant c;
    c.ref = net(ligne().value_or(""));
    c.motif = net(ligne().value_or(""));
    c.valeur = net(ligne().value_or(""));
    ligne();  // place de la valeur
    ligne();  // place du repère
    const auto o = champs();
    c.x = a(o, 0), c.y = a(o, 1);
    while (auto l = ligne()) {
      const std::string k = net(*l);
      if (k.rfind("ENDCOMP", 0) == 0) break;
      enregistrement(k, c, true);
    }
    return c;
  }
  void reseau(Carte& c) {
    const std::string nom = net(ligne().value_or(""));
    bool dans = false, table = false;
    while (auto l = ligne()) {
      const std::string s = net(*l);
      if (s.rfind("ENDPCB", 0) == 0) {
        --i_;
        return;
      }
      if (s.empty()) continue;
      if (s[0] == '[') {
        while (auto m = ligne())
          if (net(*m) == "]") break;
      } else if (s[0] == '(')
        dans = true;
      else if (s[0] == ')')
        return;
      else if (s[0] == '{')
        table = true;
      else if (s[0] == '}')
        table = false;
      else if (dans && !table && !nom.empty())
        if (const auto t = s.rfind('-'); t != std::string::npos && t > 0) c.reseaux[nom].push_back({s.substr(0, t), s.substr(t + 1)});
    }
  }
};
// Suites de quarts allumés (masque 15 ou 0 : cercle entier) : (début, balayage) en degrés, sens direct
std::vector<std::pair<double, double>> portions(int q) {
  if (q <= 0 || q >= 15) return {{0, 360}};
  auto allume = [&](int k) { return ((q >> ((k + 4) % 4)) & 1) != 0; };
  std::vector<std::pair<double, double>> v;
  for (int k = 0; k < 4; ++k) {
    if (!allume(k) || allume(k - 1)) continue;
    int n = 1;
    while (n < 4 && allume(k + n)) ++n;
    v.push_back({90.0 * k, 90.0 * n});
  }
  return v;
}
}  // namespace autotrax
}  // namespace

ConceptionElec conceptionDepuisAutotrax(const std::string& texte, const std::string& nom, RapportLectureCao* rapport) {
  using namespace autotrax;
  RapportLectureCao r;
  const Carte a = Lecteur(texte).lire();
  auto mil = [](double v) { return static_cast<Nm>(std::llround(v * 25'400)); };
  auto P = [&](double x, double y) { return Pt{mil(x), -mil(y)}; };
  // Couches : 1 dessus, 2 à 5 internes, 6 dessous, 7 et 8 sérigraphies, 9 et 10 plans (internes après les autres),
  // 11 contour, 12 interdit (souvent le bord de la carte), 13 toutes couches
  int internes = 0;
  bool plans[2] = {false, false};
  auto noter = [&](int k) {
    if (k >= 2 && k <= 5) internes = std::max(internes, k - 1);
    if (k == 9 || k == 10) plans[k - 9] = true;
  };
  auto parcourir = [&](const Contenu& c) {
    for (const auto& t : c.traits) noter(t.couche);
    for (const auto& x : c.arcs) noter(x.couche);
    for (const auto& x : c.pleins) noter(x.couche);
  };
  parcourir(a);
  for (const auto& c : a.composants) parcourir(c);
  const int plan9 = plans[0] ? ++internes : 0;
  const int plan10 = plans[1] ? ++internes : 0;
  if (plan9 || plan10) r.alertes.push_back("plans de masse ou d'alimentation (couches 9 et 10) repris en couches de cuivre internes, dessinés en positif");
  auto cuivre = [&](int k) -> std::optional<Couche> {
    if (k == 1 || k == 13) return Couche::CuivreDessus;
    if (k == 6) return Couche::CuivreDessous;
    if (k >= 2 && k <= 5) return cuivreInterne(k - 1);
    if (k == 9 && plan9) return cuivreInterne(plan9);
    if (k == 10 && plan10) return cuivreInterne(plan10);
    return std::nullopt;
  };
  cao_interne::CarteLue l;
  l.cuivres = 2 + internes;
  int cibles = 0, horsCouche = 0, reliesPlans = 0;
  // Pastille dans le repère d'origine « o » : forme 1 ronde ou ovale, 2 rectangle, 3 octogone, 4 coins arrondis ;
  // 5 et 6 (mires) écartées
  auto pastille = [&](const Pad& p, Pt o) -> std::optional<Pastille> {
    if (p.forme == 5 || p.forme == 6) {
      ++cibles;
      return std::nullopt;
    }
    if (p.couche != 1 && p.couche != 6 && p.couche != 13) {
      ++horsCouche;
      return std::nullopt;
    }
    if (p.plans > 1) ++reliesPlans;
    Pastille q;
    q.numero = p.nom;
    q.position = P(p.x, p.y) - o;
    q.largeur = std::max<Nm>(mil(p.lx), 1);
    q.hauteur = std::max<Nm>(mil(p.ly), 1);
    q.forme = p.forme == 2 ? FormePastille::Rect : p.forme == 3 ? FormePastille::Chanfreine : p.forme == 4 ? FormePastille::RectArrondi : p.lx == p.ly ? FormePastille::Rond : FormePastille::Ovale;
    if (p.forme == 3) q.chanfrein = 0.25;
    // Traversante sur la couche 13 (toutes couches) seulement : une pastille des couches 1 ou 6 est CMS, son perçage
    // n'étant qu'une valeur par défaut restée (30 mils sur une pastille de 20)
    q.percage = p.couche == 13 ? mil(p.percage) : 0;
    q.type = q.percage > 0 ? TypePastille::Traversante : TypePastille::CMS;
    if (q.type == TypePastille::CMS && p.couche == 6) q.couches = bit(Couche::CuivreDessous) | bit(Couche::VernisDessous) | bit(Couche::PateDessous);
    return q;
  };
  auto seri = [](int k) { return k == 8 ? Couche::SerigraphieDessous : Couche::SerigraphieDessus; };
  std::vector<std::pair<Pt, Pt>> bord, interdit;
  Boite boiteInterdit, boitePercages;
  // Cuivre et dessins libres ; le cuivre d'un composant suit le même chemin (pistes de la carte)
  auto cuivreLibre = [&](const Contenu& c) {
    for (const auto& t : c.traits) {
      if (t.couche == 11 || t.couche == 12) {
        (t.couche == 11 ? bord : interdit).push_back({P(t.x1, t.y1), P(t.x2, t.y2)});
        if (t.couche == 12) boiteInterdit.etendre(P(t.x1, t.y1)), boiteInterdit.etendre(P(t.x2, t.y2));
        continue;
      }
      if (const auto k = cuivre(t.couche)) {
        Piste p;
        p.couche = *k;
        p.a = P(t.x1, t.y1);
        p.b = P(t.x2, t.y2);
        p.largeur = std::max<Nm>(mil(t.largeur), 50'000);
        l.pistes.push_back(p);
      }
    }
    for (const auto& x : c.arcs)
      for (const auto& [d, b] : portions(x.quarts)) {
        const auto pts = pointsArcDegres(P(x.cx, x.cy), static_cast<double>(mil(x.rayon)), d, b);
        if (x.couche == 11 || x.couche == 12) {
          for (std::size_t k = 1; k < pts.size(); ++k) (x.couche == 11 ? bord : interdit).push_back({pts[k - 1], pts[k]});
          continue;
        }
        if (const auto k = cuivre(x.couche)) {
          // Arc de piste : en deux moitiés pour un cercle entier
          const int morceaux = b >= 360 ? 2 : 1;
          for (int m = 0; m < morceaux; ++m) {
            const double d0 = d + b * m / morceaux, bm = b / morceaux;
            const auto q = pointsArcDegres(P(x.cx, x.cy), static_cast<double>(mil(x.rayon)), d0, bm);
            Piste p;
            p.couche = *k;
            p.a = q.front();
            p.b = q.back();
            p.milieu = pointsArcDegres(P(x.cx, x.cy), static_cast<double>(mil(x.rayon)), d0 + bm / 2, 0).front();
            p.largeur = std::max<Nm>(mil(x.largeur), 50'000);
            l.pistes.push_back(p);
          }
        }
      }
    for (const auto& v : c.vias) boitePercages.etendre(P(v.x, v.y));
    for (const auto& x : c.pleins)
      if (const auto k = cuivre(x.couche)) {
        RegionCuivre g;
        g.genre = "libre";
        g.couche = *k;
        const Pt p1 = P(x.x1, x.y1), p2 = P(x.x2, x.y2);
        g.forme = {{std::min(p1.x, p2.x), std::min(p1.y, p2.y)}, {std::max(p1.x, p2.x), std::min(p1.y, p2.y)}, {std::max(p1.x, p2.x), std::max(p1.y, p2.y)}, {std::min(p1.x, p2.x), std::max(p1.y, p2.y)}};
        l.regions.push_back(g);
      }
  };
  cuivreLibre(a);
  for (const auto& v : a.vias) {
    ecad::Via x;
    x.p = P(v.x, v.y);
    x.percage = std::max<Nm>(mil(v.percage), 1);
    x.diametre = std::max<Nm>(mil(v.diametre), x.percage + 2);
    l.vias.push_back(x);
  }
  for (const auto& t : a.traits)
    if (t.couche == 7 || t.couche == 8) {
      DessinCircuit d;
      d.couche = seri(t.couche);
      d.points = {P(t.x1, t.y1), P(t.x2, t.y2)};
      d.epaisseur = std::max<Nm>(mil(t.largeur), 50'000);
      l.dessins.push_back(d);
    }
  for (const auto& x : a.arcs)
    if (x.couche == 7 || x.couche == 8)
      for (const auto& [d0, b] : portions(x.quarts)) {
        DessinCircuit d;
        d.couche = seri(x.couche);
        d.points = pointsArcDegres(P(x.cx, x.cy), static_cast<double>(mil(x.rayon)), d0, b);
        d.epaisseur = std::max<Nm>(mil(x.largeur), 50'000);
        l.dessins.push_back(d);
      }
  for (const auto& t : a.textes) {
    const auto k = cuivre(t.couche);
    if (!k && t.couche != 7 && t.couche != 8) continue;
    TexteCircuit x;
    x.texte = cao_interne::texteWindows(t.texte);
    x.couche = k ? *k : seri(t.couche);
    x.hauteur = std::max<Nm>(mil(t.hauteur), 100'000);
    // Ancré en bas à gauche dans Autotrax : le centre est déplacé d'une demi-boîte dans le sens du texte
    const double ang = (t.miroir ? -90.0 : 90.0) * t.sens;
    const double w = static_cast<double>(largeurTexte(x.texte, x.hauteur)), h = static_cast<double>(x.hauteur);
    const double c = std::cos(ang * std::numbers::pi / 180), s = std::sin(ang * std::numbers::pi / 180);
    const double dx = (t.miroir ? -w : w) / 2, dy = -h / 2;
    x.p = P(t.x, t.y) + Pt{static_cast<Nm>(std::llround(dx * c + dy * s)), static_cast<Nm>(std::llround(-dx * s + dy * c))};
    x.rotation = ang;
    if (t.miroir) x.miroir = true;
    l.textes.push_back(x);
  }
  // Composants : pastilles posées dans le repère de leur origine (Autotrax n'a pas de rotation), une définition par
  // forme ; pastilles libres en pièces de la carte seule
  Empreintes ranges(l);
  std::set<std::string> pris;
  for (const Composant& c : a.composants) {
    const Pt o = P(c.x, c.y);
    DefEmpreinte d;
    d.description = c.motif;
    for (const Pad& p : c.pads)
      if (auto q = pastille(p, o)) d.pastilles.push_back(*q);
    // Vias d'un composant : des pastilles traversantes qui le suivent
    for (const TrouVia& v : c.vias) {
      Pad p;
      p.x = v.x, p.y = v.y, p.lx = p.ly = v.diametre, p.percage = v.percage;
      if (auto q = pastille(p, o)) d.pastilles.push_back(*q);
    }
    for (const Trait& t : c.traits)
      if (t.couche == 7 || t.couche == 8) {
        TraitEmpreinte s;
        s.couche = seri(t.couche);
        s.a = P(t.x1, t.y1) - o;
        s.b = P(t.x2, t.y2) - o;
        s.epaisseur = std::max<Nm>(mil(t.largeur), 50'000);
        d.traits.push_back(s);
      }
    for (const Arc& x : c.arcs)
      if (x.couche == 7 || x.couche == 8)
        for (const auto& [d0, b] : portions(x.quarts)) {
          if (b >= 360) {
            TraitEmpreinte s;
            s.couche = seri(x.couche);
            s.a = P(x.cx, x.cy) - o;
            s.b = s.a;
            s.rayon = mil(x.rayon);
            s.epaisseur = std::max<Nm>(mil(x.largeur), 50'000);
            d.traits.push_back(s);
            continue;
          }
          const auto pts = pointsArcDegres(P(x.cx, x.cy) - o, static_cast<double>(mil(x.rayon)), d0, b);
          for (std::size_t k = 1; k < pts.size(); ++k) {
            TraitEmpreinte s;
            s.couche = seri(x.couche);
            s.a = pts[k - 1];
            s.b = pts[k];
            s.epaisseur = std::max<Nm>(mil(x.largeur), 50'000);
            d.traits.push_back(s);
          }
        }
    // Cuivre propre au composant (pistes, arcs, pleins) : posé sur la carte, relié par le cuivre
    Contenu cuivreComp;
    for (const Trait& t : c.traits)
      if (cuivre(t.couche) || t.couche == 11 || t.couche == 12) cuivreComp.traits.push_back(t);
    for (const Arc& x : c.arcs)
      if (cuivre(x.couche) || x.couche == 11 || x.couche == 12) cuivreComp.arcs.push_back(x);
    for (const Plein& x : c.pleins) cuivreComp.pleins.push_back(x);
    cuivreLibre(cuivreComp);
    for (const Pad& p : c.pads) boitePercages.etendre(P(p.x, p.y));
    const std::string ref = repereUnique(pris, c.ref, "U" + std::to_string(l.poses.size() + 1), r);
    cao_interne::CarteLue::Pose pose;
    pose.ref = ref;
    pose.decal = ranges.ranger(c.motif.empty() ? "sans-motif" : c.motif, d);
    pose.p = o;
    l.poses.push_back(pose);
    l.netlist.composants.push_back({ref, c.valeur, pose.decal});
  }
  int libres = 0;
  for (const Pad& p : a.pads) {
    Pad q = p;
    if (q.nom.empty()) q.nom = "1";
    const auto s = pastille(q, P(p.x, p.y));
    if (!s) continue;
    boitePercages.etendre(P(p.x, p.y));
    DefEmpreinte d;
    d.pastilles.push_back(*s);
    cao_interne::CarteLue::Pose pose;
    pose.ref = repereUnique(pris, "PL" + std::to_string(++libres), "PL", r);
    pose.decal = ranges.ranger("pastille-libre", d);
    pose.p = P(p.x, p.y);
    l.poses.push_back(pose);
    l.carteSeule.insert(pose.ref);
  }
  // Contour : couche 11, sinon la couche d'interdiction si elle entoure tous les perçages
  if (!cao_interne::contourDepuisTraits(l, bord) && !interdit.empty() && (boitePercages.vide() || boiteInterdit.contient(boitePercages)))
    if (cao_interne::contourDepuisTraits(l, interdit)) r.alertes.push_back("contour de la carte tiré de la couche d'interdiction (12), qui entoure tout le cuivre");
  // Réseaux : la netlist du fichier (NETDEF) si elle y est, sinon le cuivre connecté
  if (!a.reseaux.empty()) {
    l.netlist.reseaux = a.reseaux;
    cuivreSurNetlist(l);
  } else {
    cao_interne::reseauxDepuisCuivre(l);
    r.alertes.push_back("Autotrax ne garde pas de netlist : les réseaux sont tirés du cuivre connecté (N$1…)");
  }
  if (cibles) r.alertes.push_back(std::to_string(cibles) + " mire(s) de positionnement (pastilles de forme 5 ou 6) écartée(s)");
  if (horsCouche) r.alertes.push_back(std::to_string(horsCouche) + " pastille(s) hors des couches 1, 6 et 13 écartée(s)");
  if (reliesPlans && !(plan9 || plan10)) r.alertes.push_back(std::to_string(reliesPlans) + " pastille(s) marquée(s) reliée(s) à un plan absent du fichier");
  if (libres) r.alertes.push_back(std::to_string(libres) + " pastille(s) libre(s) posée(s) sur la carte seule (PL1…)");
  ConceptionElec c = cao_interne::assemblerCarte(l, nom, "autotrax:", r);
  if (rapport) *rapport = r;
  return c;
}

// ======================================================================================================================
// gEDA PCB et Lepton
// ======================================================================================================================
namespace {
// Une ligne de gEDA : le mot-clé, puis les paramètres entre [ ] (centièmes de mil) ou ( ) (mils), chaînes entre
// guillemets
struct LigneGeda {
  std::string cle;
  bool crochets = true;
  std::vector<std::string> p;
  bool ouverture = false, fermeture = false;  // « ( » ou « ) » seuls
};
LigneGeda decouper(const std::string& s) {
  LigneGeda r;
  std::size_t i = s.find_first_not_of(" \t");
  if (i == std::string::npos) return r;
  if (s[i] == '(' && s.find_first_not_of(" \t", i + 1) == std::string::npos) {
    r.ouverture = true;
    return r;
  }
  if (s[i] == ')' && s.find_first_not_of(" \t", i + 1) == std::string::npos) {
    r.fermeture = true;
    return r;
  }
  while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) r.cle += s[i++];
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  if (i >= s.size()) return r;
  r.crochets = s[i] == '[';
  const char fin = r.crochets ? ']' : ')';
  ++i;
  while (i < s.size() && s[i] != fin) {
    if (std::isspace(static_cast<unsigned char>(s[i]))) {
      ++i;
      continue;
    }
    std::string m;
    if (s[i] == '"') {
      for (++i; i < s.size() && s[i] != '"'; ++i) m += s[i];
      ++i;
    } else
      while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != fin) m += s[i++];
    r.p.push_back(m);
  }
  return r;
}
// Longueur : suffixe d'unité (« 1.5mm », « 40mil »), sinon centièmes de mil entre [ ], mils entre ( )
Nm longueur(const std::string& v, bool crochets) {
  const double x = nombre(v);
  if (v.size() > 2 && v.compare(v.size() - 2, 2, "mm") == 0) return static_cast<Nm>(std::llround(x * 1e6));
  if (v.size() > 3 && v.compare(v.size() - 3, 3, "mil") == 0) return static_cast<Nm>(std::llround(x * 25'400));
  if (v.size() > 2 && v.compare(v.size() - 2, 2, "um") == 0) return static_cast<Nm>(std::llround(x * 1e3));
  return static_cast<Nm>(std::llround(x * (crochets ? 254 : 25'400)));
}
bool drapeau(const std::string& f, const char* nom, unsigned masque) {
  if (f.find(nom) != std::string::npos) return true;
  if (!f.empty() && (std::isdigit(static_cast<unsigned char>(f[0])) || f.rfind("0x", 0) == 0)) return (std::strtoul(f.c_str(), nullptr, 0) & masque) != 0;
  return false;
}
// Élément (empreinte posée ou fichier .fp) : pastilles et traits dans le repère de sa marque, tels qu'écrits (un
// élément du dessous est écrit tel qu'il se trouve sur la carte, déjà retourné)
struct ElementGeda {
  std::string ref, valeur, description;
  Pt marque;
  bool dessous = false;
  DefEmpreinte def;
  std::vector<std::set<int>> thermiques;  // par pastille : couches (rang à partir de 0) reliées aux polygones
  std::vector<Nm> degagements;            // par pastille : écart que lui laissent les polygones (moitié du « Clearance »)
};
std::set<int> couchesThermiques(const std::string& f) {
  std::set<int> v;
  const auto t = f.find("thermal(");
  if (t == std::string::npos) return v;
  const auto fin = f.find(')', t);
  std::istringstream in(f.substr(t + 8, fin == std::string::npos ? std::string::npos : fin - t - 8));
  for (std::string x; std::getline(in, x, ',');)
    if (!x.empty() && std::isdigit(static_cast<unsigned char>(x[0]))) v.insert(std::atoi(x.c_str()));
  return v;
}
// Point d'un arc de gEDA : angle 0 vers −x, 90 vers +y (le bas), croissant dans le sens direct à l'écran ; dans le
// sens direct de Prométhée, l'angle vaut 180 + celui de gEDA
std::vector<Pt> arcGeda(Pt c, double rayon, double debut, double balayage) { return pointsArcDegres(c, rayon, 180 + debut, balayage); }

std::optional<ElementGeda> lireElement(const std::vector<std::string>& L, std::size_t& i) {
  const LigneGeda e = decouper(L[i]);
  if (e.cle != "Element") return std::nullopt;
  ElementGeda x;
  const auto& p = e.p;
  // Element[SFlags "Desc" "Name" "Value" MX MY TX TY TDir TScale TSFlags] ; formes anciennes sans marque :
  // Element(NFlags "Desc" "Name" "Value" TX TY TDir TScale TNFlags), Element("Desc" "Name" TX TY TDir TScale TNFlags)
  std::string drapeaux;
  if (p.size() >= 8) {
    drapeaux = p[0];
    x.description = p[1];
    x.ref = p[2];
    x.valeur = p[3];
    if (p.size() >= 11) x.marque = {longueur(p[4], e.crochets), longueur(p[5], e.crochets)};
  } else if (p.size() >= 2) {
    x.description = p[0];
    x.ref = p[1];
  }
  x.dessous = drapeau(drapeaux, "onsolder", 0x0080);
  x.def.description = x.description;
  for (++i; i < L.size(); ++i) {
    const LigneGeda m = decouper(L[i]);
    if (m.fermeture) break;
    if (m.ouverture || m.cle.empty()) continue;
    const auto& q = m.p;
    if (m.cle == "Pin" && q.size() >= 5) {
      // Pin[rX rY Thickness Clearance Mask Drill "Name" "Number" SFlags] ; Pin(rX rY Thickness Drill "Name" "Number" NFlags)
      Pastille s;
      s.position = {longueur(q[0], m.crochets), longueur(q[1], m.crochets)};
      s.largeur = s.hauteur = longueur(q[2], m.crochets);
      s.percage = q.size() >= 9 ? longueur(q[5], m.crochets) : longueur(q[3], m.crochets);
      s.numero = q[q.size() - 2];
      const std::string f = q.back();
      s.type = drapeau(f, "hole", 0x0008) ? TypePastille::NonMetallisee : TypePastille::Traversante;
      s.forme = drapeau(f, "square", 0x0100) ? FormePastille::Rect : drapeau(f, "octagon", 0x0800) ? FormePastille::Chanfreine : FormePastille::Rond;
      if (s.forme == FormePastille::Chanfreine) s.chanfrein = 0.29;
      x.def.pastilles.push_back(s);
      x.thermiques.push_back(couchesThermiques(f));
      x.degagements.push_back(q.size() >= 9 ? longueur(q[3], m.crochets) / 2 : 0);
    } else if (m.cle == "Pad" && q.size() >= 7) {
      // Pad[rX1 rY1 rX2 rY2 Thickness Clearance Mask "Name" "Number" SFlags] : un trait épaissi
      const Pt a{longueur(q[0], m.crochets), longueur(q[1], m.crochets)}, b{longueur(q[2], m.crochets), longueur(q[3], m.crochets)};
      const Nm w = longueur(q[4], m.crochets);
      Pastille s;
      s.type = TypePastille::CMS;
      s.position = {(a.x + b.x) / 2, (a.y + b.y) / 2};
      s.largeur = static_cast<Nm>(std::llround(distance(a, b))) + w;
      s.hauteur = w;
      s.rotation = -std::atan2(static_cast<double>(b.y - a.y), static_cast<double>(b.x - a.x)) * 180 / std::numbers::pi;
      s.numero = q[q.size() - 2];
      const std::string f = q.back();
      s.forme = drapeau(f, "square", 0x0100) ? FormePastille::Rect : s.largeur == s.hauteur ? FormePastille::Rond : FormePastille::Ovale;
      // Côté de la pastille dans la définition : celui de la carte, ramené au dessus si l'élément est retourné
      if (drapeau(f, "onsolder", 0x0080) != x.dessous) s.couches = bit(Couche::CuivreDessous) | bit(Couche::VernisDessous) | bit(Couche::PateDessous);
      if (s.largeur > 0 && s.hauteur > 0) {
        x.def.pastilles.push_back(s);
        x.thermiques.push_back(couchesThermiques(f));
        x.degagements.push_back(q.size() >= 10 ? longueur(q[5], m.crochets) / 2 : 0);
      }
    } else if (m.cle == "ElementLine" && q.size() >= 5) {
      TraitEmpreinte t;
      t.a = {longueur(q[0], m.crochets), longueur(q[1], m.crochets)};
      t.b = {longueur(q[2], m.crochets), longueur(q[3], m.crochets)};
      t.epaisseur = longueur(q[4], m.crochets);
      x.def.traits.push_back(t);
    } else if (m.cle == "ElementArc" && q.size() >= 7) {
      // ElementArc[X Y Width Height StartAngle DeltaAngle Thickness] : rayons (largeur et hauteur moyennées)
      const Pt c{longueur(q[0], m.crochets), longueur(q[1], m.crochets)};
      const double r = (static_cast<double>(longueur(q[2], m.crochets)) + static_cast<double>(longueur(q[3], m.crochets))) / 2;
      const double a0 = nombre(q[4]), da = nombre(q[5]);
      const Nm ep = longueur(q[6], m.crochets);
      if (std::abs(da) >= 360) {
        TraitEmpreinte t;
        t.a = t.b = c;
        t.rayon = static_cast<Nm>(std::llround(r));
        t.epaisseur = ep;
        x.def.traits.push_back(t);
        continue;
      }
      const auto pts = arcGeda(c, r, a0, da);
      for (std::size_t k = 1; k < pts.size(); ++k) {
        TraitEmpreinte t;
        t.a = pts[k - 1];
        t.b = pts[k];
        t.epaisseur = ep;
        x.def.traits.push_back(t);
      }
    }
  }
  return x;
}
// Définition dans le repère du dessus : un élément du dessous est remis à l'endroit (miroir en y) et sa pose
// retournée d'un demi-tour (x → −x puis 180° : le miroir en y revient)
DefEmpreinte definitionAuDessus(const ElementGeda& e) {
  DefEmpreinte d = e.def;
  if (!e.dessous) return d;
  for (auto& q : d.pastilles) {
    q.position.y = -q.position.y;
    q.rotation = -q.rotation;
  }
  for (auto& t : d.traits) {
    t.a.y = -t.a.y;
    t.b.y = -t.b.y;
  }
  return d;
}
}  // namespace

bool estGedaPcb(const std::string& t) {
  for (const auto& l : lignesDe(t)) {
    const auto p = l.find_first_not_of(" \t");
    if (p == std::string::npos || l[p] == '#') continue;
    return l.compare(p, 4, "PCB[") == 0 || l.compare(p, 4, "PCB(") == 0 || l.compare(p, 12, "FileVersion[") == 0 || l.compare(p, 8, "Element[") == 0 || l.compare(p, 8, "Element(") == 0;
  }
  return false;
}

std::optional<DefEmpreinte> empreinteDepuisGedaFp(const std::string& texte, const std::string& nom) {
  const auto L = lignesDe(texte);
  for (std::size_t i = 0; i < L.size(); ++i)
    if (auto e = lireElement(L, i)) {
      DefEmpreinte d = cadrer(definitionAuDessus(*e));
      d.id = "geda:" + nom;
      d.description = e->description.empty() ? nom : e->description;
      d.famille = "Importées de gEDA";
      return d;
    }
  return std::nullopt;
}

ConceptionElec conceptionDepuisGedaPcb(const std::string& texte, const std::string& nom, RapportLectureCao* rapport) {
  RapportLectureCao r;
  const auto L = lignesDe(texte);
  cao_interne::CarteLue l;
  // Groupes : « 1,c:2,s:3:4 » — un groupe est une couche physique ; celui marqué « c » le dessus, « s » le dessous
  std::map<int, int> groupeDe;
  std::vector<std::vector<int>> groupes;
  int groupeDessus = -1, groupeDessous = -1;
  Nm largeurCarte = 0, hauteurCarte = 0;
  std::map<std::string, std::string> parConnect;  // « U1-1 » → réseau
  struct Objet {
    int couche = 0;
    enum Genre { Ligne, Arc, Texte } genre = Ligne;
    Pt a, b, milieu;
    std::vector<Pt> points;  // arc en cordes
    Nm largeur = 0;
    bool cercle = false;
    Pt centre;
    double rayon = 0;
    std::string texte;
    int sens = 0;
    double echelle = 100;
    bool joint = false;  // ligne sans « clearline » : reliée aux polygones qu'elle touche
    Nm degagement = 0;   // écart que lui laissent les polygones
  };
  std::vector<Objet> objets;
  struct PolygoneGeda {
    int couche;
    Polygone contour;
    bool degage = true, entier = false;  // « clearpoly » : dégagé du cuivre ; « fullpoly » : tous ses morceaux gardés
  };
  std::vector<PolygoneGeda> polygones;
  int trous = 0;
  std::map<int, std::string> nomsCouches;
  std::vector<int> ordreCouches;
  struct ThermiqueVia {
    std::size_t via;
    std::set<int> couches;
  };
  std::vector<ThermiqueVia> viasThermiques;
  // Pastilles des éléments sur la carte (cuivre, dégagement, liaisons thermiques) pour les polygones
  struct PastilleGeda {
    cao_interne::Broche broche;
    Pt p;
    Polygone forme;
    bool traversante = false, dessous = false;
    Nm degagement = 0;
    std::set<int> thermiques;
  };
  std::vector<PastilleGeda> pastillesCarte;
  std::vector<Nm> degagementVia, degagementPiste;
  Empreintes ranges(l);
  std::set<std::string> pris;
  // Police du fichier (gEDA l'écrit avec la carte) : chasse et hauteur de chaque lettre, pour poser les textes à leur
  // taille d'origine
  struct Glyphe {
    double droite = 0, espace = 0, haut = 0, bas = 0;
  };
  std::map<unsigned char, Glyphe> police;
  int couche = 0;
  for (std::size_t i = 0; i < L.size(); ++i) {
    const LigneGeda g = decouper(L[i]);
    if (g.cle == "Symbol" && g.p.size() >= 2) {
      const std::string& q = g.p[0];
      const auto a = q.find('\''), b = q.rfind('\'');
      const std::string c = a != std::string::npos && b > a ? q.substr(a + 1, b - a - 1) : q;
      Glyphe x;
      x.espace = static_cast<double>(longueur(g.p[1], g.crochets));
      bool premier = true;
      for (++i; i < L.size(); ++i) {
        const LigneGeda m = decouper(L[i]);
        if (m.fermeture) break;
        if (m.cle != "SymbolLine" || m.p.size() < 4) continue;
        for (int k = 0; k < 2; ++k) {
          const double px = static_cast<double>(longueur(m.p[static_cast<std::size_t>(2 * k)], m.crochets)), py = static_cast<double>(longueur(m.p[static_cast<std::size_t>(2 * k + 1)], m.crochets));
          x.droite = std::max(x.droite, px);
          x.haut = premier ? py : std::min(x.haut, py);
          x.bas = premier ? py : std::max(x.bas, py);
          premier = false;
        }
      }
      if (!c.empty()) police[static_cast<unsigned char>(c.back())] = x;
      continue;
    }
    if (g.cle == "PCB" && g.p.size() >= 3) largeurCarte = longueur(g.p[1], g.crochets), hauteurCarte = longueur(g.p[2], g.crochets);
    else if (g.cle == "Groups" && !g.p.empty()) {
      std::istringstream in(g.p[0]);
      int k = 0;
      for (std::string grp; std::getline(in, grp, ':'); ++k) {
        std::vector<int> v;
        std::istringstream in2(grp);
        for (std::string x; std::getline(in2, x, ',');) {
          if (x == "c") groupeDessus = k;
          else if (x == "s") groupeDessous = k;
          else if (!x.empty() && std::isdigit(static_cast<unsigned char>(x[0]))) {
            v.push_back(std::atoi(x.c_str()));
            groupeDe[std::atoi(x.c_str())] = k;
          }
        }
        groupes.push_back(v);
      }
    } else if (g.cle == "Element") {
      auto e = lireElement(L, i);
      if (!e) continue;
      const std::string ref = repereUnique(pris, e->ref, "U" + std::to_string(l.poses.size() + 1), r);
      cao_interne::CarteLue::Pose p;
      p.ref = ref;
      p.decal = ranges.ranger(e->description.empty() ? "sans-description" : e->description, definitionAuDessus(*e));
      p.p = e->marque;
      p.dessous = e->dessous;
      p.rotation = e->dessous ? 180 : 0;
      l.poses.push_back(p);
      l.netlist.composants.push_back({ref, e->valeur, p.decal});
      for (std::size_t k = 0; k < e->def.pastilles.size(); ++k) {
        Pastille q = e->def.pastilles[k];
        q.position = e->marque + q.position;
        PastilleGeda x;
        x.broche = {ref, q.numero};
        x.p = q.position;
        x.forme = formePastille(q);
        x.traversante = q.type != TypePastille::CMS;
        // Côté sur la carte : celui de la définition, retourné avec l'élément
        x.dessous = (q.couches & bit(Couche::CuivreDessous)) ? !e->dessous : e->dessous;
        x.degagement = e->degagements[k];
        x.thermiques = e->thermiques[k];
        pastillesCarte.push_back(x);
      }
    } else if (g.cle == "Via" && g.p.size() >= 4) {
      Via v;
      v.p = {longueur(g.p[0], g.crochets), longueur(g.p[1], g.crochets)};
      v.diametre = longueur(g.p[2], g.crochets);
      v.percage = longueur(g.p[g.p.size() >= 8 ? 5 : 3], g.crochets);
      if (const auto t = couchesThermiques(g.p.back()); !t.empty()) viasThermiques.push_back({l.vias.size(), t});
      degagementVia.push_back(g.p.size() >= 8 ? longueur(g.p[3], g.crochets) / 2 : 0);
      l.vias.push_back(v);
    } else if (g.cle == "Layer" && !g.p.empty()) {
      couche = std::atoi(g.p[0].c_str());
      if (g.p.size() >= 2) nomsCouches[couche] = g.p[1];
      ordreCouches.push_back(couche);
    } else if (g.cle == "Line" && g.p.size() >= 5 && couche > 0) {
      Objet x;
      x.couche = couche;
      x.a = {longueur(g.p[0], g.crochets), longueur(g.p[1], g.crochets)};
      x.b = {longueur(g.p[2], g.crochets), longueur(g.p[3], g.crochets)};
      x.largeur = std::max<Nm>(longueur(g.p[4], g.crochets), 50'000);
      x.joint = g.p.size() >= 6 && !drapeau(g.p.back(), "clearline", 0x0020);
      if (g.p.size() >= 7) x.degagement = longueur(g.p[5], g.crochets) / 2;
      objets.push_back(x);
    } else if (g.cle == "Arc" && g.p.size() >= 8 && couche > 0) {
      // Arc[X Y Width Height Thickness Clearance StartAngle DeltaAngle SFlags] ; Arc(X Y Width Height Thickness StartAngle DeltaAngle Flags)
      Objet x;
      x.couche = couche;
      x.genre = Objet::Arc;
      x.centre = {longueur(g.p[0], g.crochets), longueur(g.p[1], g.crochets)};
      x.rayon = (static_cast<double>(longueur(g.p[2], g.crochets)) + static_cast<double>(longueur(g.p[3], g.crochets))) / 2;
      x.largeur = std::max<Nm>(longueur(g.p[4], g.crochets), 50'000);
      const std::size_t k = g.p.size() >= 9 ? 6 : 5;
      const double a0 = nombre(g.p[k]), da = nombre(g.p[k + 1]);
      x.cercle = std::abs(da) >= 360;
      x.points = arcGeda(x.centre, x.rayon, a0, x.cercle ? 360 : da);
      x.a = x.points.front();
      x.b = x.points.back();
      x.milieu = arcGeda(x.centre, x.rayon, a0 + da / 2, 0).front();
      objets.push_back(x);
    } else if (g.cle == "Text" && g.p.size() >= 5 && couche > 0) {
      // Text[X Y Direction Scale "String" SFlags] : coin haut gauche, sens par quarts de tour
      Objet x;
      x.couche = couche;
      x.genre = Objet::Texte;
      x.a = {longueur(g.p[0], g.crochets), longueur(g.p[1], g.crochets)};
      x.sens = std::atoi(g.p[2].c_str()) & 3;
      x.echelle = nombre(g.p[3], 100);
      x.texte = g.p[4];
      objets.push_back(x);
    } else if (g.cle == "Polygon" && couche > 0) {
      const std::string drapeaux = g.p.empty() ? std::string() : g.p[0];
      Polygone q;
      int profondeur = 0;
      for (++i; i < L.size(); ++i) {
        const LigneGeda h = decouper(L[i]);
        if (h.cle == "Hole") {
          // Trou d'un polygone : sauté (le remplissage de la zone dégage déjà le cuivre des autres réseaux)
          ++trous;
          int n = L[i].find('(') != std::string::npos ? 1 : 0;
          for (++i; i < L.size(); ++i) {
            if (L[i].find('(') != std::string::npos) ++n;
            if (L[i].find(')') != std::string::npos && --n <= 0) break;
          }
          continue;
        }
        if (h.ouverture) {
          ++profondeur;
          continue;
        }
        if (h.fermeture && --profondeur <= 0) break;
        for (std::size_t a = L[i].find('['); a != std::string::npos; a = L[i].find('[', a + 1)) {
          const auto b = L[i].find(']', a);
          const auto m = mots(L[i].substr(a + 1, b - a - 1));
          if (m.size() >= 2) q.push_back({longueur(m[0], true), longueur(m[1], true)});
        }
      }
      if (q.size() >= 3) polygones.push_back({couche, q, drapeau(drapeaux, "clearpoly", 0x0010), drapeau(drapeaux, "fullpoly", 0x100000)});
    } else if (g.cle == "Net" && !g.p.empty()) {
      const std::string reseau = g.p[0];
      for (++i; i < L.size(); ++i) {
        const LigneGeda c = decouper(L[i]);
        if (c.fermeture) break;
        if (c.cle == "Connect" && !c.p.empty()) parConnect[c.p[0]] = reseau;
      }
    }
  }
  // Rôle des couches : contour et sérigraphies d'après leur nom (la première « silk » est celle du dessous, la
  // seconde celle du dessus, comme les écrit pcb) ; masques, pâte et fabrication écartés ; le reste en cuivre, par
  // groupe : dessus, dessous, puis les groupes internes qui portent quelque chose
  auto minuscules = [](std::string n) {
    for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return n;
  };
  enum class Role { Cuivre, Contour, SeriDessus, SeriDessous, Autre };
  std::map<int, Role> role;
  std::vector<int> silks;
  for (const int k : ordreCouches) {
    const std::string n = minuscules(nomsCouches[k]);
    if (n.find("outline") != std::string::npos || n.find("route") != std::string::npos) role[k] = Role::Contour;
    else if (n.find("silk") != std::string::npos) {
      role[k] = n.find("solder") != std::string::npos || n.find("bottom") != std::string::npos ? Role::SeriDessous : Role::SeriDessus;
      if (n == "silk") silks.push_back(k);
    } else if (n.find("mask") != std::string::npos || n.find("paste") != std::string::npos || n.find("fab") != std::string::npos)
      role[k] = Role::Autre;
    else
      role[k] = Role::Cuivre;
  }
  if (silks.size() >= 2) role[silks[0]] = Role::SeriDessous;
  std::set<int> portees;  // couches qui portent un objet
  for (const auto& o : objets) portees.insert(o.couche);
  for (const auto& q : polygones) portees.insert(q.couche);
  std::map<int, Couche> cuivre;
  int internes = 0;
  auto groupe = [&](int k) { return groupeDe.count(k) ? groupeDe[k] : -1; };
  for (std::size_t k = 0; k < groupes.size(); ++k) {
    std::vector<int> cuivres;
    for (const int c : groupes[k])
      if (role.count(c) ? role[c] == Role::Cuivre : true) cuivres.push_back(c);
    if (cuivres.empty()) continue;
    std::optional<Couche> q;
    if (static_cast<int>(k) == groupeDessus) q = Couche::CuivreDessus;
    else if (static_cast<int>(k) == groupeDessous) q = Couche::CuivreDessous;
    else if (std::any_of(cuivres.begin(), cuivres.end(), [&](int c) { return portees.count(c) > 0; })) q = cuivreInterne(std::min(++internes, MAX_CUIVRES - 2));
    if (q)
      for (const int c : cuivres) cuivre[c] = *q;
  }
  for (const auto& [k, x] : role)
    if (x == Role::Cuivre && groupe(k) < 0 && !cuivre.count(k)) {
      if (k == 1) cuivre[k] = Couche::CuivreDessus;
      else if (k == 2) cuivre[k] = Couche::CuivreDessous;
      else if (portees.count(k)) cuivre[k] = cuivreInterne(std::min(++internes, MAX_CUIVRES - 2));
    }
  if (groupes.empty() && ordreCouches.empty()) cuivre = {{1, Couche::CuivreDessus}, {2, Couche::CuivreDessous}};
  l.cuivres = 2 + internes;
  if (internes) r.alertes.push_back(std::to_string(internes) + " couche(s) de cuivre interne(s) d'après les groupes de gEDA");
  // Objets des couches
  std::vector<std::pair<Pt, Pt>> bord;
  std::vector<std::size_t> pistesJointes;
  for (const auto& o : objets) {
    const Role x = role.count(o.couche) ? role[o.couche] : Role::Cuivre;
    if (x == Role::Contour) {
      if (o.genre == Objet::Ligne) bord.push_back({o.a, o.b});
      else if (o.genre == Objet::Arc)
        for (std::size_t k = 1; k < o.points.size(); ++k) bord.push_back({o.points[k - 1], o.points[k]});
      continue;
    }
    if (x == Role::Autre) continue;
    if (o.genre == Objet::Texte) {
      TexteCircuit t;
      t.texte = cao_interne::texteWindows(o.texte);
      if (const auto c = cuivre.find(o.couche); c != cuivre.end()) t.couche = c->second;
      else if (x == Role::SeriDessous) t.couche = Couche::SerigraphieDessous;
      else if (x == Role::SeriDessus) t.couche = Couche::SerigraphieDessus;
      else continue;
      // Boîte d'origine d'après la police du fichier (à défaut celle de pcb : capitales de 40 mils entre 10 et 50,
      // chasse de 37 mils), à l'échelle du texte ; ancre en haut à gauche, rotation autour d'elle
      const double e = o.echelle / 100, mil = 25'400;
      double w = 0, dernier = 0;
      for (const char ch : o.texte) {
        const auto it = police.find(static_cast<unsigned char>(ch));
        const double avance = it != police.end() ? it->second.droite + it->second.espace : 37 * mil;
        w += avance * e;
        dernier = (it != police.end() ? it->second.espace : 12 * mil) * e;
      }
      w = std::max(0.0, w - dernier);
      const auto A = police.find('A');
      const double haut = (A != police.end() ? A->second.haut : 10 * mil) * e, bas = (A != police.end() ? A->second.bas : 50 * mil) * e;
      ajusterTexte(t, w, bas - haut);
      const double dx = w / 2, dy = (haut + bas) / 2;
      const double ang = 90.0 * o.sens, c = std::cos(ang * std::numbers::pi / 180), s = std::sin(ang * std::numbers::pi / 180);
      t.p = o.a + Pt{static_cast<Nm>(std::llround(dx * c + dy * s)), static_cast<Nm>(std::llround(-dx * s + dy * c))};
      t.rotation = ang;
      l.textes.push_back(t);
      continue;
    }
    if (const auto c = cuivre.find(o.couche); c != cuivre.end()) {
      if (o.joint) pistesJointes.push_back(l.pistes.size());
      const std::size_t avant = l.pistes.size();
      const int morceaux = o.genre == Objet::Arc && o.cercle ? 2 : 1;
      for (int m = 0; m < morceaux; ++m) {
        Piste p;
        p.couche = c->second;
        p.largeur = o.largeur;
        if (o.genre == Objet::Ligne) {
          p.a = o.a;
          p.b = o.b;
        } else {
          // Arc de piste ; un cercle entier en deux moitiés
          const std::size_t n = o.points.size() - 1, d = n * static_cast<std::size_t>(m) / static_cast<std::size_t>(morceaux), f = n * static_cast<std::size_t>(m + 1) / static_cast<std::size_t>(morceaux);
          p.a = o.points[d];
          p.b = o.points[f];
          p.milieu = o.points[(d + f) / 2];
          if (p.a == *p.milieu || p.b == *p.milieu) p.milieu.reset();
        }
        l.pistes.push_back(p);
      }
      degagementPiste.resize(l.pistes.size(), o.joint ? -1 : o.degagement);
      (void)avant;
    } else if (x == Role::SeriDessus || x == Role::SeriDessous) {
      DessinCircuit d;
      d.couche = x == Role::SeriDessous ? Couche::SerigraphieDessous : Couche::SerigraphieDessus;
      d.epaisseur = o.largeur;
      if (o.genre == Objet::Ligne) d.points = {o.a, o.b};
      else if (o.cercle) {
        d.forme = DessinCircuit::Forme::Cercle;
        d.points = {o.centre, o.centre + Pt{static_cast<Nm>(std::llround(o.rayon)), 0}};
      } else
        d.points = o.points;
      l.dessins.push_back(d);
    }
  }
  if (!cao_interne::contourDepuisTraits(l, bord) && largeurCarte > 0 && hauteurCarte > 0) {
    l.contour = {{0, 0}, {largeurCarte, 0}, {largeurCarte, hauteurCarte}, {0, hauteurCarte}};
    r.alertes.push_back("aucun contour fermé sur la couche « outline » : la carte prend la taille du fichier");
  }
  // Réseaux : la netlist du fichier si elle y est, sinon le cuivre connecté
  if (!parConnect.empty()) {
    for (const auto& [connect, reseau] : parConnect) {
      const auto t = connect.rfind('-');
      if (t == std::string::npos) continue;
      l.netlist.reseaux[reseau].push_back({connect.substr(0, t), connect.substr(t + 1)});
    }
    cuivreSurNetlist(l);
  } else {
    cao_interne::reseauxDepuisCuivre(l);
    r.alertes.push_back("carte gEDA sans netlist : réseaux tirés du cuivre connecté (N$1…)");
  }
  // Polygones : des zones, morceau par morceau. Un polygone « clearpoly » de gEDA s'écarte du cuivre des autres objets
  // (de la moitié de leur « Clearance ») ; il ne se relie qu'aux broches et vias à liaison thermique sur sa couche et
  // aux lignes écrites sans « clearline ». Le dégagement le coupe en morceaux, chacun de son réseau ; sans « fullpoly »
  // seul le plus grand reste. Une via thermique relie les morceaux qu'elle traverse (couture), et en prend le réseau.
  std::map<cao_interne::Broche, std::string> reseauDe;
  for (const auto& [n, v] : l.netlist.reseaux)
    for (const auto& b : v) reseauDe[b] = n;
  struct Morceau {
    Couche couche;
    Polygone exterieur;
    Polygones forme;
    std::set<std::string> reseaux;
    std::vector<std::size_t> vias;
  };
  std::vector<Morceau> morceaux;
  for (const auto& q : polygones) {
    const auto c = cuivre.find(q.couche);
    if (c == cuivre.end()) {
      // Polygone de sérigraphie : un dessin plein
      const Role x = role.count(q.couche) ? role[q.couche] : Role::Autre;
      if (x == Role::SeriDessus || x == Role::SeriDessous) {
        DessinCircuit d;
        d.forme = DessinCircuit::Forme::Polygone;
        d.couche = x == Role::SeriDessous ? Couche::SerigraphieDessous : Couche::SerigraphieDessus;
        d.points = q.contour;
        d.plein = true;
        d.epaisseur = 0;
        l.dessins.push_back(d);
      }
      continue;
    }
    const Couche k = c->second;
    const int rang = q.couche - 1;
    const Polygone contour = orienter(q.contour);
    const Boite bq = boite(contour);
    auto surCouche = [&](const PastilleGeda& x) { return x.traversante || (x.dessous ? k == Couche::CuivreDessous : k == Couche::CuivreDessus); };
    // Cuivre dégagé : lignes « clearline », broches et vias sans liaison thermique sur cette couche, pastilles CMS
    Polygones obstacles;
    if (q.degage) {
      for (std::size_t i = 0; i < l.pistes.size(); ++i) {
        const Piste& p = l.pistes[i];
        if (p.couche != k || (i < degagementPiste.size() && degagementPiste[i] < 0)) continue;
        const Nm d = i < degagementPiste.size() ? degagementPiste[i] : 0;
        for (const auto& [a, b] : segmentsPiste(p, 20'000)) {
          const Boite bs = Boite{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)}.gonflee(p.largeur / 2 + d);
          if (bs.touche(bq)) obstacles.push_back(oblong(a, b, p.largeur + 2 * d));
        }
      }
      for (const auto& x : pastillesCarte) {
        if (!surCouche(x) || (x.traversante && x.thermiques.count(rang))) continue;
        if (!boite(x.forme).gonflee(x.degagement).touche(bq)) continue;
        for (const auto& f : gonfler({x.forme}, x.degagement)) obstacles.push_back(f);
      }
      for (std::size_t v = 0; v < l.vias.size(); ++v) {
        const bool thermique = std::any_of(viasThermiques.begin(), viasThermiques.end(), [&](const ThermiqueVia& t) { return t.via == v && t.couches.count(rang); });
        if (thermique) continue;
        const Nm rv = l.vias[v].diametre / 2 + (v < degagementVia.size() ? degagementVia[v] : 0);
        if (Boite{l.vias[v].p.x, l.vias[v].p.y, l.vias[v].p.x, l.vias[v].p.y}.gonflee(rv).touche(bq)) obstacles.push_back(cercle(l.vias[v].p, rv));
      }
    }
    std::vector<Ilot> pieces = ilots(obstacles.empty() ? Polygones{contour} : soustraire({contour}, unir(obstacles)));
    if (!q.entier && pieces.size() > 1) {
      const auto plus = std::max_element(pieces.begin(), pieces.end(), [](const Ilot& a, const Ilot& b) { return aireMm2(a.exterieur) < aireMm2(b.exterieur); });
      pieces = {*plus};
    }
    for (const Ilot& il : pieces) {
      if (il.exterieur.size() < 3 || aireMm2(il.exterieur) < 1e-3) continue;
      Morceau m;
      m.couche = k;
      m.exterieur = il.exterieur;
      m.forme = {il.exterieur};
      m.forme.insert(m.forme.end(), il.trous.begin(), il.trous.end());
      // Ce qui s'y relie : broches et vias thermiques dedans, lignes soudées qui le touchent ; sans « clearpoly », tout
      // le cuivre qu'il recouvre
      for (const auto& x : pastillesCarte)
        if (surCouche(x) && (!q.degage || (x.traversante && x.thermiques.count(rang))) && (dedans(m.forme, x.p) || (!q.degage && distancePolygones(il.exterieur, x.forme) <= 0)))
          if (const auto it = reseauDe.find(x.broche); it != reseauDe.end()) m.reseaux.insert(it->second);
      for (std::size_t v = 0; v < l.vias.size(); ++v) {
        const bool thermique = std::any_of(viasThermiques.begin(), viasThermiques.end(), [&](const ThermiqueVia& t) { return t.via == v && t.couches.count(rang); });
        if ((thermique || !q.degage) && dedans(m.forme, l.vias[v].p)) {
          m.vias.push_back(v);
          if (!l.vias[v].reseau.empty()) m.reseaux.insert(l.vias[v].reseau);
        }
      }
      for (std::size_t i = 0; i < l.pistes.size(); ++i) {
        const Piste& p = l.pistes[i];
        const bool soudee = i < degagementPiste.size() && degagementPiste[i] < 0;
        if (p.couche != k || p.reseau.empty() || (q.degage && !soudee)) continue;
        if (distancePolygoneSegment(il.exterieur, p.a, p.b) <= static_cast<double>(p.largeur) / 2) m.reseaux.insert(p.reseau);
      }
      morceaux.push_back(std::move(m));
    }
  }
  // Morceaux reliés par une via thermique commune : un même cuivre
  std::vector<std::size_t> parent(morceaux.size());
  for (std::size_t i = 0; i < morceaux.size(); ++i) parent[i] = i;
  std::function<std::size_t(std::size_t)> racine = [&](std::size_t i) { return parent[i] == i ? i : parent[i] = racine(parent[i]); };
  std::map<std::size_t, std::size_t> morceauDeVia;
  for (std::size_t i = 0; i < morceaux.size(); ++i)
    for (const std::size_t v : morceaux[i].vias) {
      const auto [it, neuf] = morceauDeVia.emplace(v, i);
      if (!neuf) parent[racine(i)] = racine(it->second);
    }
  std::map<std::size_t, std::set<std::string>> reseauxGroupe;
  for (std::size_t i = 0; i < morceaux.size(); ++i) reseauxGroupe[racine(i)].insert(morceaux[i].reseaux.begin(), morceaux[i].reseaux.end());
  int sansReseau = 0, cousues = 0, melanges = 0, eclats = 0;
  for (std::size_t i = 0; i < morceaux.size(); ++i) {
    const auto& ens = reseauxGroupe[racine(i)];
    // Éclat du dégagement relié à rien (moins de 5 mm²) : retiré, comme le ferait le remplissage
    if (ens.empty() && morceaux[i].vias.empty() && aireMm2(morceaux[i].exterieur) < 5) {
      ++eclats;
      continue;
    }
    Zone z;
    z.couche = morceaux[i].couche;
    z.contour = morceaux[i].exterieur;
    if (ens.size() == 1) {
      z.reseau = *ens.begin();
      for (const std::size_t v : morceaux[i].vias)
        if (l.vias[v].reseau.empty()) l.vias[v].reseau = z.reseau, ++cousues;
    } else if (ens.empty())
      ++sansReseau;
    else
      ++melanges;
    l.zones.push_back(z);
  }
  if (cousues) r.alertes.push_back(std::to_string(cousues) + " via(s) de couture prise(s) au réseau de leur polygone");
  if (eclats) r.alertes.push_back(std::to_string(eclats) + " éclat(s) de polygone reliés à rien (moins de 5 mm²) retiré(s)");
  if (melanges) r.alertes.push_back(std::to_string(melanges) + " morceau(x) de polygone relié(s) à plusieurs réseaux : zones sans réseau, à vérifier");
  if (sansReseau) r.alertes.push_back(std::to_string(sansReseau) + " morceau(x) de polygone relié(s) à aucun réseau : zones sans réseau");
  if (trous) r.alertes.push_back(std::to_string(trous) + " trou(s) de polygone non repris (le remplissage dégage les autres réseaux)");
  ConceptionElec c = cao_interne::assemblerCarte(l, nom, "geda:", r);
  if (rapport) *rapport = r;
  return c;
}

// ======================================================================================================================
// Schémas gschem et lepton-schematic
// ======================================================================================================================
bool estSchemaGschem(const std::string& t) {
  const auto p = t.find_first_not_of(" \t\r\n");
  return p != std::string::npos && t.compare(p, 2, "v ") == 0 && (t.find("\nC ") != std::string::npos || t.find("\nN ") != std::string::npos);
}

namespace {
namespace gschem {
using Attributs = std::multimap<std::string, std::string>;
std::string premier(const Attributs& a, const std::string& cle) {
  const auto it = a.find(cle);
  return it == a.end() ? std::string() : it->second;
}
struct Broche {
  Pt bout;  // le bout qui se relie (« whichend »)
  std::string numero, sequence, etiquette;
};
struct Symbole {
  std::vector<Broche> broches;
  Attributs attributs;  // attributs propres au symbole (net=, slotdef=, device=, footprint=…)
};
// Attributs entre { } qui suivent un objet : textes « T » (une ou plusieurs lignes « nom=valeur »)
void lireAttributs(const std::vector<std::string>& L, std::size_t& i, Attributs& a) {
  if (i + 1 >= L.size() || L[i + 1].rfind("{", 0) != 0) return;
  for (i += 2; i < L.size() && L[i].rfind("}", 0) != 0; ++i)
    if (L[i].rfind("T ", 0) == 0) {
      const auto m = mots(L[i]);
      const int lignes = m.size() >= 10 ? std::max(1, std::atoi(m[9].c_str())) : 1;
      for (int k = 0; k < lignes && i + 1 < L.size(); ++k) {
        const std::string& v = L[++i];
        if (const auto e = v.find('='); e != std::string::npos && e > 0) a.emplace(v.substr(0, e), v.substr(e + 1));
      }
    }
}
// Symbole : ses broches et ses attributs de premier niveau (les textes « T » hors broches)
Symbole lireSymbole(const std::vector<std::string>& L, std::size_t debut, std::size_t fin) {
  Symbole x;
  for (std::size_t i = debut; i < fin; ++i) {
    const auto m = mots(L[i]);
    if (m.empty()) continue;
    if (m[0] == "P" && m.size() >= 8) {
      Broche b;
      const Pt p1{static_cast<Nm>(nombre(m[1])), static_cast<Nm>(nombre(m[2]))}, p2{static_cast<Nm>(nombre(m[3])), static_cast<Nm>(nombre(m[4]))};
      b.bout = m[7] == "1" ? p2 : p1;
      Attributs a;
      lireAttributs(L, i, a);
      b.numero = premier(a, "pinnumber");
      if (b.numero.empty()) b.numero = premier(a, "pin#");
      b.sequence = premier(a, "pinseq");
      b.etiquette = premier(a, "pinlabel");
      if (b.numero.empty()) b.numero = b.sequence.empty() ? std::to_string(x.broches.size() + 1) : b.sequence;
      x.broches.push_back(b);
    } else if (m[0] == "T" && m.size() >= 10) {
      const int lignes = std::max(1, std::atoi(m[9].c_str()));
      for (int k = 0; k < lignes && i + 1 < fin; ++k) {
        const std::string& v = L[++i];
        if (const auto e = v.find('='); e != std::string::npos && e > 0) x.attributs.emplace(v.substr(0, e), v.substr(e + 1));
      }
    } else if (m[0] == "{") {
      // attributs d'un objet graphique : sautés
      while (i < fin && L[i].rfind("}", 0) != 0) ++i;
    }
  }
  return x;
}
// Point de connexion : bout de fil, bout de broche d'un composant (repère, numéro), ou nom de réseau imposé
struct Point {
  Pt p;
  std::string ref, broche, reseau;
  bool fil = false;
};
// Lecture d'une page (ou d'une feuille fille), points de connexion et fils ; « prefixe » : chemin de la feuille fille
// (« S1/ ») ajouté aux repères et aux réseaux locaux, comme le fait gnetlist
class Page {
 public:
  Page(const std::function<std::optional<std::string>(const std::string&)>& lire, std::map<std::string, std::optional<Symbole>>& symboles, RapportLectureCao& r)
      : lire_(lire), symboles_(symboles), r_(r) {}
  struct Lu {
    std::vector<Point> points;
    std::vector<std::pair<Pt, Pt>> fils;
    std::map<std::string, ComposantNetlist> composants;
    std::vector<std::pair<std::string, std::string>> egaux;  // réseaux à réunir (feuille fille ↔ broche du parent)
  };
  void lire(const std::string& texte, const std::string& prefixe, int profondeur, Lu& lu) {
    const auto L = lignesDe(texte);
    for (std::size_t i = 0; i < L.size(); ++i) {
      const auto m = mots(L[i]);
      if (m.empty()) continue;
      if (m[0] == "N" && m.size() >= 5) {
        const Pt a{static_cast<Nm>(nombre(m[1])), static_cast<Nm>(nombre(m[2]))}, b{static_cast<Nm>(nombre(m[3])), static_cast<Nm>(nombre(m[4]))};
        Attributs at;
        lireAttributs(L, i, at);
        const std::string n = premier(at, "netname");
        lu.fils.push_back({a, b});
        lu.points.push_back({a, "", "", n.empty() ? std::string() : prefixe + n, true});
        lu.points.push_back({b, "", "", "", true});
      } else if (m[0] == "C" && m.size() >= 7)
        composant(L, i, m, prefixe, profondeur, lu);
    }
  }

 private:
  const std::function<std::optional<std::string>(const std::string&)>& lire_;
  std::map<std::string, std::optional<Symbole>>& symboles_;
  RapportLectureCao& r_;
  const std::optional<Symbole>& symbole(const std::string& n) {
    if (const auto it = symboles_.find(n); it != symboles_.end()) return it->second;
    std::optional<Symbole> s;
    // Le symbole du projet, sinon celui de la bibliothèque standard de gEDA (incorporée)
    std::optional<std::string> t = lire_ ? lire_(n) : std::nullopt;
    if (!t) t = symboleStandardGeda(n);
    if (t) {
      const auto L = lignesDe(*t);
      s = lireSymbole(L, 0, L.size());
      // Symbole d'alimentation du projet sans « net= » (copie réduite de gnd-1.sym…) : il prend celui du symbole
      // standard de même nom, comme dans KiCad
      if (!s->attributs.count("net"))
        if (const auto std = symboleStandardGeda(n)) {
          const auto M = lignesDe(*std);
          const Symbole x = lireSymbole(M, 0, M.size());
          for (auto [a, b] = x.attributs.equal_range("net"); a != b; ++a) s->attributs.emplace(a->first, a->second);
        }
    }
    if (!s && n.rfind("title-", 0) != 0) r_.alertes.push_back("symbole « " + n + " » introuvable : ses broches ne sont pas reliées");
    return symboles_.emplace(n, s).first->second;
  }
  void composant(const std::vector<std::string>& L, std::size_t& i, const std::vector<std::string>& m, const std::string& prefixe, int profondeur, Lu& lu) {
    const Pt o{static_cast<Nm>(nombre(m[1])), static_cast<Nm>(nombre(m[2]))};
    const int angle = std::atoi(m[4].c_str());
    const bool miroir = m[5] == "1";
    std::string fichier = m[6];
    if (const auto p = fichier.find_last_of("/\\"); p != std::string::npos) fichier = fichier.substr(p + 1);
    // Symbole incorporé : sa définition suit entre [ ]
    std::optional<Symbole> incorpore;
    if (i + 1 < L.size() && L[i + 1].rfind("[", 0) == 0) {
      const std::size_t d = i + 2;
      std::size_t f = d;
      while (f < L.size() && L[f].rfind("]", 0) != 0) ++f;
      incorpore = lireSymbole(L, d, f);
      i = f;
    }
    if (fichier.rfind("EMBEDDED", 0) == 0) fichier = fichier.substr(8);
    Attributs at;
    lireAttributs(L, i, at);
    // « source= » ne désigne une feuille fille que s'il nomme un schéma (« bloc.sch ») ; sinon c'est une provenance
    // (fournisseur, « source=Yageo »)
    std::string source = premier(at, "source");
    {
      std::string m = source;
      for (char& ch : m) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      if (const auto v = m.find(','); v != std::string::npos) source = source.substr(0, v), m = m.substr(0, v);
      if (m.size() < 4 || m.compare(m.size() - 4, 4, ".sch") != 0) source.clear();
    }
    const std::optional<Symbole>& s = incorpore ? incorpore : symbole(fichier);
    if (!s && source.empty()) {
      // Sans symbole : le composant reste à la nomenclature, sans broche reliée
      const std::string ref = premier(at, "refdes");
      if (!ref.empty() && ref.find('?') == std::string::npos) lu.composants.emplace(prefixe + ref, ComposantNetlist{prefixe + ref, premier(at, "value"), premier(at, "footprint")});
      return;
    }
    // Attributs : ceux de l'instance d'abord, puis ceux du symbole pour les clés absentes
    Attributs tous = at;
    if (s)
      for (const auto& [k, v] : s->attributs)
        if (!at.count(k)) tous.emplace(k, v);
    const std::string ref0 = premier(tous, "refdes");
    // gschem tourne puis retourne (x → −x), dans le repère y vers le haut
    auto placer = [&](Pt q) {
      Pt t = q;
      switch (((angle % 360) + 360) % 360) {
        case 90: t = {-q.y, q.x}; break;
        case 180: t = {-q.x, -q.y}; break;
        case 270: t = {q.y, -q.x}; break;
        default: break;
      }
      if (miroir) t.x = -t.x;
      return o + t;
    };
    // Feuille fille (« source= ») : ses repères et réseaux locaux préfixés ; ses ports (composants dont le repère
    // est l'étiquette d'une broche du bloc) se relient aux broches du bloc
    if (!source.empty()) {
      const std::string chemin = prefixe + (ref0.empty() ? "S" + std::to_string(++feuilles_) : ref0) + "/";
      if (profondeur > 8) {
        r_.alertes.push_back("feuille « " + source + " » trop profonde (boucle ?) : non lue");
        return;
      }
      const auto t = lire_ ? lire_(source) : std::nullopt;
      if (!t) {
        r_.alertes.push_back("feuille fille « " + source + " » introuvable");
        return;
      }
      Lu fille;
      lire(*t, chemin, profondeur + 1, fille);
      // Broches du bloc : un point de réseau « chemin + étiquette » à leur bout ; sans symbole du bloc, la feuille est
      // lue sans liaison au parent
      if (!s) r_.alertes.push_back("symbole du bloc « " + fichier + " » introuvable : la feuille « " + source + " » est lue sans liaison à ses broches");
      for (const auto& b : s ? s->broches : std::vector<Broche>{}) {
        const std::string e = b.etiquette.empty() ? b.numero : b.etiquette;
        lu.points.push_back({placer(b.bout), "", "", chemin + "port:" + e, false});
        // Le port de la feuille porte ce repère : réseau commun
        lu.egaux.push_back({chemin + "port:" + e, chemin + "ref:" + e});
      }
      for (auto& p : fille.points) lu.points.push_back(p);
      for (auto& f : fille.fils) lu.fils.push_back(f);
      for (auto& [k, c] : fille.composants) {
        // Ports de la feuille : composants d'entrée et de sortie, leur repère désigne la broche du bloc
        if (estPort(c)) continue;
        lu.composants.emplace(k, c);
      }
      for (auto& e : fille.egaux) lu.egaux.push_back(e);
      return;
    }
    // Brochage d'une porte (« slot= » avec « slotdef=N:1,2,3 ») : numéros par rang de broche (« pinseq »)
    std::vector<std::string> brochage;
    if (const std::string slot = premier(tous, "slot"); !slot.empty()) {
      for (const auto& [k, v] : tous)
        if (k == "slotdef" && v.rfind(slot + ":", 0) == 0) {
          std::istringstream in(v.substr(slot.size() + 1));
          for (std::string x; std::getline(in, x, ',');) brochage.push_back(x);
          break;
        }
    }
    auto numero = [&](const Broche& b) {
      if (!brochage.empty() && !b.sequence.empty()) {
        const int k = std::atoi(b.sequence.c_str());
        if (k >= 1 && static_cast<std::size_t>(k) <= brochage.size()) return brochage[static_cast<std::size_t>(k - 1)];
      }
      return b.numero;
    };
    // net=NOM:1,2 : broches reliées au réseau NOM (symboles d'alimentation, broches cachées)
    std::map<std::string, std::string> imposes;
    for (const auto& [k, v] : tous)
      if (k == "net")
        if (const auto c = v.find(':'); c != std::string::npos) {
          std::istringstream in(v.substr(c + 1));
          for (std::string b; std::getline(in, b, ',');) imposes[b] = v.substr(0, c);
        }
    const bool graphique = premier(tous, "graphical") == "1";
    const bool composant = !ref0.empty() && ref0.find('?') == std::string::npos && !graphique;
    const std::string ref = prefixe + ref0;
    const std::string device = premier(tous, "device");
    const bool padE = device == "IPAD" || device == "OPAD" || device == "IOPAD" || device == "INPUT" || device == "OUTPUT" || device == "IO";
    if (composant) lu.composants.emplace(ref, ComposantNetlist{ref, padE || premier(tous, "value").empty() ? device : premier(tous, "value"), premier(tous, "footprint")});
    // Port d'une feuille fille : sa broche porte le réseau « chemin + ref: + repère »
    const bool port = composant && !prefixe.empty() && estPort(lu.composants[ref]);
    for (const auto& b : s->broches) {
      Point p;
      p.p = placer(b.bout);
      const std::string n = numero(b);
      if (port) p.reseau = prefixe + "ref:" + ref0;
      else if (composant) p.ref = ref, p.broche = n;
      if (imposes.count(n)) p.reseau = imposes[n];
      if (p.ref.empty() && p.reseau.empty()) continue;
      lu.points.push_back(p);
    }
    // Broches cachées : citées par net= sans être dessinées
    if (composant && !port)
      for (const auto& [b, reseau] : imposes)
        if (std::none_of(s->broches.begin(), s->broches.end(), [&](const Broche& x) { return numero(x) == b; })) lu.points.push_back({o, ref, b, reseau, false});
  }
  // Port d'une feuille fille : symbole d'entrée, de sortie ou d'entrée-sortie (in-1.sym, out-1.sym, io-1.sym :
  // « device=INPUT », « OUTPUT », « IO », ou les plots IPAD, OPAD, IOPAD)
  static bool estPort(const ComposantNetlist& c) {
    std::string v = c.valeur;
    for (char& ch : v) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return v == "INPUT" || v == "OUTPUT" || v == "IO" || v == "IPAD" || v == "OPAD" || v == "IOPAD";
  }
  int feuilles_ = 0;
};
}  // namespace gschem
}  // namespace

std::optional<DefSymbole> symboleDepuisGedaSym(const std::string& texte, const std::string& nom, std::string* raison) {
  const auto L = lignesDe(texte);
  const gschem::Symbole s = gschem::lireSymbole(L, 0, L.size());
  auto tous = [&](const std::string& k) {
    std::vector<std::string> v;
    for (auto [a, b] = s.attributs.equal_range(k); a != b; ++a) v.push_back(a->second);
    return v;
  };
  if (gschem::premier(s.attributs, "graphical") == "1" || s.broches.empty()) {
    if (raison) *raison = s.broches.empty() ? "symbole sans broche (cartouche, dessin)" : "symbole graphique";
    return std::nullopt;
  }
  if (!tous("net").empty() && s.broches.size() == 1 && gschem::premier(s.attributs, "refdes").empty()) {
    if (raison) *raison = "symbole d'alimentation : ceux de Prométhée (GND, +5V…) le remplacent";
    return std::nullopt;
  }
  SpecSymbole x;
  x.id = "geda:" + nom;
  x.nom = nom;
  std::string ref = gschem::premier(s.attributs, "refdes");
  while (!ref.empty() && (ref.back() == '?' || std::isdigit(static_cast<unsigned char>(ref.back())))) ref.pop_back();
  x.prefixe = ref.empty() ? "U" : ref;
  x.categorie = "Importés de gEDA";
  x.description = gschem::premier(s.attributs, "description");
  if (x.description.empty()) x.description = gschem::premier(s.attributs, "device");
  x.fiche = gschem::premier(s.attributs, "documentation");
  // Côté d'une broche : d'où elle part vers le corps (le bout relié est à l'extérieur)
  Boite b;
  for (const auto& p : s.broches) b.etendre(p.bout);
  const Pt centre = b.centre();
  auto cote = [&](Pt p) {
    const double dx = static_cast<double>(p.x - centre.x), dy = static_cast<double>(p.y - centre.y);
    if (std::abs(dx) * std::max<Nm>(b.hauteur(), 1) >= std::abs(dy) * std::max<Nm>(b.largeur(), 1)) return dx < 0 ? 0 : 1;
    return dy > 0 ? 2 : 3;  // y vers le haut dans gschem
  };
  auto type = [](const std::string& t) {
    if (t == "in" || t == "clk") return TypeBroche::Entree;
    if (t == "out") return TypeBroche::Sortie;
    if (t == "io") return TypeBroche::Bidirectionnelle;
    if (t == "tri" || t == "tp") return TypeBroche::TroisEtats;
    if (t == "oc" || t == "oe") return TypeBroche::CollecteurOuvert;
    if (t == "pwr") return TypeBroche::AlimEntree;
    if (t == "pas") return TypeBroche::Passive;
    return TypeBroche::NonSpecifiee;
  };
  // Le type des broches n'est pas gardé par lireSymbole : relu ici, broche après broche
  std::vector<std::string> types;
  for (std::size_t i = 0; i < L.size(); ++i)
    if (L[i].rfind("P ", 0) == 0) {
      gschem::Attributs a;
      gschem::lireAttributs(L, i, a);
      types.push_back(gschem::premier(a, "pintype"));
    }
  // Portes : une unité par « slotdef=N:… », numéros pris dans l'ordre des broches (« pinseq »)
  std::map<int, std::vector<std::string>> portes;
  for (const auto& v : tous("slotdef"))
    if (const auto c = v.find(':'); c != std::string::npos) {
      std::istringstream in(v.substr(c + 1));
      for (std::string n; std::getline(in, n, ',');) portes[std::atoi(v.substr(0, c).c_str())].push_back(n);
    }
  if (portes.size() > 1) {
    for (const auto& [k, numeros] : portes)
      for (std::size_t i = 0; i < s.broches.size(); ++i) {
        const auto& p = s.broches[i];
        const int seq = std::atoi(p.sequence.c_str());
        const std::string n = seq >= 1 && static_cast<std::size_t>(seq) <= numeros.size() ? numeros[static_cast<std::size_t>(seq - 1)] : p.numero;
        x.broches.push_back({n, p.etiquette.empty() ? n : p.etiquette, i < types.size() ? type(types[i]) : TypeBroche::NonSpecifiee, cote(p.bout), k});
      }
  } else
    for (std::size_t i = 0; i < s.broches.size(); ++i) {
      const auto& p = s.broches[i];
      x.broches.push_back({p.numero, p.etiquette.empty() ? p.numero : p.etiquette, i < types.size() ? type(types[i]) : TypeBroche::NonSpecifiee, cote(p.bout), 0});
    }
  // Broches d'alimentation cachées (« net=VCC:14 ») : communes à toutes les unités
  for (const auto& v : tous("net"))
    if (const auto c = v.find(':'); c != std::string::npos) {
      std::istringstream in(v.substr(c + 1));
      for (std::string n; std::getline(in, n, ',');)
        if (std::none_of(x.broches.begin(), x.broches.end(), [&](const SpecBroche& q) { return q.numero == n; })) x.broches.push_back({n, v.substr(0, c), TypeBroche::AlimEntree, 3, 0});
    }
  return genererSymbole(x);
}

ConceptionElec conceptionDepuisGschem(const std::vector<std::pair<std::string, std::string>>& pages, const std::string& nom, const LecteurSymboles& lire, RapportLectureCao* rapport) {
  RapportLectureCao r;
  std::map<std::string, std::optional<gschem::Symbole>> symboles;
  gschem::Page page(lire, symboles, r);
  gschem::Page::Lu lu;
  for (const auto& [fichier, texte] : pages) page.lire(texte, "", 0, lu);
  auto& points = lu.points;
  // Union : points confondus ; bout posé sur un fil (jonction en T) ; noms égaux
  std::vector<int> parent(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) parent[i] = static_cast<int>(i);
  std::function<int(int)> racine = [&](int i) { return parent[static_cast<std::size_t>(i)] == i ? i : parent[static_cast<std::size_t>(i)] = racine(parent[static_cast<std::size_t>(i)]); };
  auto unir = [&](int a, int b) { parent[static_cast<std::size_t>(racine(a))] = racine(b); };
  std::map<Pt, int> parPoint;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto [it, neuf] = parPoint.emplace(points[i].p, static_cast<int>(i));
    if (!neuf) unir(static_cast<int>(i), it->second);
  }
  IndexSpatial index(2000);
  for (std::size_t f = 0; f < lu.fils.size(); ++f) {
    Boite b;
    b.etendre(lu.fils[f].first);
    b.etendre(lu.fils[f].second);
    index.inserer(static_cast<int>(f), b);
    unir(parPoint[lu.fils[f].first], parPoint[lu.fils[f].second]);
  }
  // Un bout de fil ou de broche posé sur un fil s'y relie (gnetlist fait de même, vérifié sur des cartes réelles)
  for (std::size_t i = 0; i < points.size(); ++i) {
    for (const int f : index.chercher(Boite{points[i].p.x, points[i].p.y, points[i].p.x, points[i].p.y}.gonflee(1))) {
      const auto& [a, b] = lu.fils[static_cast<std::size_t>(f)];
      if (distancePointSegment(points[i].p, a, b) < 1) unir(static_cast<int>(i), parPoint[a]);
    }
  }
  std::map<std::string, int> parNom;
  for (std::size_t i = 0; i < points.size(); ++i)
    if (!points[i].reseau.empty()) {
      const auto [it, neuf] = parNom.emplace(points[i].reseau, static_cast<int>(i));
      if (!neuf) unir(static_cast<int>(i), it->second);
    }
  for (const auto& [a, b] : lu.egaux)
    if (parNom.count(a) && parNom.count(b)) unir(parNom[a], parNom[b]);
  // Noms : le nom imposé le plus court (un nom global plutôt qu'un nom de port), sinon « N$k »
  std::map<int, std::string> nomDe;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const std::string& n = points[i].reseau;
    if (n.empty() || n.find("port:") != std::string::npos || n.find("ref:") != std::string::npos) continue;
    auto& x = nomDe[racine(static_cast<int>(i))];
    if (x.empty() || n.size() < x.size() || (n.size() == x.size() && n < x)) x = n;
  }
  NetlistLue n;
  for (const auto& [ref, c] : lu.composants) n.composants.push_back(c);
  int k = 0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto& e = points[i];
    if (e.ref.empty()) continue;
    const int rac = racine(static_cast<int>(i));
    if (!nomDe.count(rac)) nomDe[rac] = "N$" + std::to_string(++k);
    auto& v = n.reseaux[nomDe[rac]];
    if (std::find(v.begin(), v.end(), std::pair{e.ref, e.broche}) == v.end()) v.push_back({e.ref, e.broche});
  }
  r.composants = static_cast<int>(n.composants.size());
  r.reseaux = static_cast<int>(n.reseaux.size());
  ConceptionElec c = conceptionDepuisNetlist(n, nom);
  c.schemaSeul = true;
  if (rapport) *rapport = r;
  return c;
}

ConceptionElec conceptionDepuisGschem(const std::string& texte, const std::string& nom, const LecteurSymboles& lire, RapportLectureCao* rapport) {
  return conceptionDepuisGschem(std::vector<std::pair<std::string, std::string>>{{nom, texte}}, nom, lire, rapport);
}

// ======================================================================================================================
// Sprint-Layout (ABACOM) : cartes .lay6 et .lay (versions 3 à 6), macros .lmk ; format binaire, petit-boutiste,
// décrit d'après l'importeur de KiCad (et les travaux de xlay et d'OpenBoardView qu'il cite)
// ======================================================================================================================
namespace {
namespace sprint {
enum Type : std::uint8_t { Segment = 1, PastilleTrou = 2, TexteContour = 3, Poly = 4, Cercle = 5, Ligne = 6, TexteTrait = 7, PastilleCms = 8 };
struct Point {
  float x = 0, y = 0;
};
struct DonneesComposant {
  bool valide = false;
  double rotation = 0;
  std::string boitier, commentaire;
};
struct Objet {
  std::uint8_t type = 0, couche = 0, forme = 0, plein = 0, miroirH = 0, miroirV = 0, interdit = 0, metallise = 0, vernis = 1;
  float x = 0, y = 0, ext = 0, inte = 0;  // pastille : rayons extérieur et du trou (CMS : largeur et hauteur)
  std::int32_t largeur = 0, angleDebut = 0, isolement = 0, rotation = 0;
  std::uint16_t composant = 0;
  std::string texte, identifiant;
  std::vector<Point> points;
  std::vector<Objet> enfants;
  DonneesComposant donnees;
};
struct Plaque {
  std::string nom;
  std::uint32_t largeur = 0, hauteur = 0;
  std::array<std::uint8_t, 7> plans{};  // plan de masse par couche (C1, S1, C2, S2, I1, I2, O ; avant la 5 : C1, C2)
  bool multicouche = false;
  std::vector<Objet> objets;
  std::vector<std::vector<std::uint32_t>> liaisons;  // par objet pastille : rangs des pastilles reliées (chevelu)
};
struct Fichier {
  int version = 6;
  std::vector<Plaque> plaques;
};

class Lecteur {
 public:
  explicit Lecteur(const std::string& o) : o_(o) {}
  Fichier carte() {
    Fichier f;
    entete(f);
    const std::uint32_t n = v_ >= 3 ? nonSigne() : 1;
    if (n == 0 || n > 100) throw std::runtime_error("nombre de cartes invalide");
    f.plaques.resize(n);
    for (auto& p : f.plaques) {
      entetePlaque(p);
      objets(p);
      p.liaisons.resize(p.objets.size());
      if (v_ >= 3)
        for (std::size_t j = 0; j < p.objets.size(); ++j)
          if (p.objets[j].type == PastilleTrou || p.objets[j].type == PastilleCms) {
            const std::uint32_t m = borne(nonSigne(), 1'000'000);
            for (std::uint32_t q = 0; q < m; ++q) p.liaisons[j].push_back(nonSigne());
          }
    }
    // La fin (onglet actif, nom du projet, auteur, société, commentaire) n'est pas reprise
    return f;
  }
  Plaque macro() {
    Fichier f;
    entete(f);
    Plaque p;
    objets(p);
    return p;
  }

 private:
  const std::string& o_;
  std::size_t i_ = 0;
  int v_ = 6;
  void besoin(std::size_t n) const {
    if (i_ + n > o_.size()) throw std::runtime_error("fin de fichier inattendue à l'octet " + std::to_string(i_));
  }
  static std::uint32_t borne(std::uint32_t n, std::uint32_t max) {
    if (n > max) throw std::runtime_error("compte invalide (" + std::to_string(n) + ")");
    return n;
  }
  std::uint8_t u1() {
    besoin(1);
    return static_cast<std::uint8_t>(o_[i_++]);
  }
  std::uint16_t u2() {
    besoin(2);
    const auto v = static_cast<std::uint16_t>(static_cast<std::uint8_t>(o_[i_]) | (static_cast<std::uint8_t>(o_[i_ + 1]) << 8));
    i_ += 2;
    return v;
  }
  std::uint32_t u4() {
    besoin(4);
    std::uint32_t v = 0;
    for (int k = 3; k >= 0; --k) v = (v << 8) | static_cast<std::uint8_t>(o_[i_ + static_cast<std::size_t>(k)]);
    i_ += 4;
    return v;
  }
  std::uint64_t u8() {
    const std::uint64_t a = u4(), b = u4();
    return a | (b << 32);
  }
  std::int32_t s4() { return static_cast<std::int32_t>(u4()); }
  std::uint32_t nonSigne() { return v_ >= 3 ? u4() : u2(); }
  std::int32_t signe() { return v_ >= 3 ? s4() : static_cast<std::int16_t>(u2()); }
  float f4() {
    const std::uint32_t b = u4();
    float f;
    std::memcpy(&f, &b, 4);
    return f;
  }
  double f8() {
    const std::uint64_t b = u8();
    double d;
    std::memcpy(&d, &b, 8);
    return d;
  }
  float coord() { return v_ >= 5 ? f4() : v_ >= 3 ? static_cast<float>(s4()) : static_cast<float>(static_cast<std::int16_t>(u2())); }
  std::string fixe(std::size_t n) {
    const std::size_t l = std::min<std::size_t>(u1(), n);
    besoin(n);
    std::string s = o_.substr(i_, l);
    i_ += n;
    return s;
  }
  std::string variable() {
    const std::uint32_t l = borne(u4(), 100'000);
    besoin(l);
    std::string s = o_.substr(i_, l);
    i_ += l;
    return s;
  }
  void sauter(std::size_t n) {
    besoin(n);
    i_ += n;
  }
  void entete(Fichier& f) {
    v_ = u1();
    const std::uint8_t a = u1(), b = u1(), c = u1();
    if (v_ > 6 || a != 0x33 || b != 0xAA || c != 0xFF) throw std::runtime_error("en-tête de Sprint-Layout absent");
    f.version = v_;
  }
  void entetePlaque(Plaque& p) {
    if (v_ < 3) {
      p.largeur = u4();
      p.hauteur = u4();
      return;
    }
    p.nom = fixe(30);
    sauter(4);
    p.largeur = u4();
    p.hauteur = u4();
    for (auto& x : p.plans) x = u1();
    if (v_ >= 5) {
      f8();       // pas de la grille
      f8();       // zoom
      sauter(8);  // décalage de la vue
      sauter(4);  // couche active
      sauter(9);  // couches visibles, copies numérisées affichées
      fixe(200);  // copies numérisées (dessus, dessous)
      fixe(200);
      sauter(24 + 8);
      s4();  // centre (x, y)
      s4();
      p.multicouche = u1() != 0;
    } else {
      sauter(19 + 4 + 7 + 400 + 33);
      if (v_ >= 4) sauter(8);
    }
  }
  void objets(Plaque& p) {
    const std::uint32_t n = borne(nonSigne(), 1'000'000);
    p.objets.reserve(n);
    for (std::uint32_t j = 0; j < n; ++j) p.objets.push_back(objet(false));
  }
  // Points : un compte parfois plus grand que la liste écrite ; une abscisse anormale (ni nulle ni normale) marque
  // la fin de la liste, comme dans l'importeur de KiCad
  void points(Objet& o) {
    const std::uint32_t n = borne(nonSigne(), 1'000'000);
    for (std::uint32_t k = 0; k < n; ++k) {
      const std::size_t avant = i_;
      const float x = coord(), y = coord();
      if (x == 0.0f || std::isnormal(x)) o.points.push_back({x, y});
      else {
        i_ = avant;
        break;
      }
    }
  }
  Objet objet(bool enfant) {
    Objet o;
    o.type = u1();
    if (o.type < Segment || o.type > PastilleCms) throw std::runtime_error("objet de type inconnu (" + std::to_string(o.type) + ") à l'octet " + std::to_string(i_ - 1));
    o.x = coord();
    o.y = coord();
    o.ext = coord();
    o.inte = coord();
    o.largeur = signe();
    sauter(1);
    o.couche = u1();
    o.forme = u1();
    if (v_ >= 5) {
      sauter(4);
      o.composant = u2();
      sauter(1);
      o.angleDebut = s4();
      sauter(5);
      o.plein = u1();
      o.isolement = s4();
      sauter(5);
      o.miroirH = u1();
      o.miroirV = u1();
      o.interdit = u1();
      o.rotation = s4();
      o.metallise = u1();
      o.vernis = u1();
      sauter(18);
      if (!enfant) {
        o.texte = variable();
        o.identifiant = variable();
      }
    } else if (v_ >= 4) {
      sauter(7);
      o.angleDebut = s4();
      sauter(5);
      o.plein = u1();
      o.isolement = s4();
      sauter(9);
      o.miroirH = u1();
      o.miroirV = u1();
      o.interdit = u1();
      sauter(18);
      if (!enfant) o.texte = variable();
    } else if (v_ >= 3) {
      if (o.type == TexteContour) {
        o.texte = fixe(15);
        sauter(7);
        o.rotation = static_cast<std::int16_t>(u2());
        sauter(25);
      } else {
        sauter(23);
        o.angleDebut = s4();
        sauter(3);
        o.miroirH = u1();
        o.miroirV = u1();
        sauter(1);
        o.isolement = s4();
        sauter(13);
      }
    } else if (o.type == TexteContour) {
      o.texte = fixe(15);
      sauter(7);
      o.rotation = static_cast<std::int16_t>(u2());
      sauter(10);
    } else
      sauter(35);
    if (v_ >= 2 && !enfant) {
      const std::uint32_t n = borne(nonSigne(), 100'000);
      for (std::uint32_t k = 0; k < n; ++k) nonSigne();  // groupes
    }
    switch (o.type) {
      case Poly:
      case Ligne: points(o); break;
      case PastilleTrou:
        if (v_ >= 5) points(o);
        else if (v_ >= 3) sauter(4);
        break;
      case PastilleCms:
        if (v_ >= 5) points(o);
        break;
      case TexteTrait: {
        const std::uint32_t n = borne(u4(), 10'000);
        for (std::uint32_t k = 0; k < n; ++k) o.enfants.push_back(objet(true));
        if (v_ >= 6 && o.forme == 1) {
          o.donnees.valide = true;
          coord();  // décalage (x, y)
          coord();
          u1();
          o.donnees.rotation = f8();
          o.donnees.boitier = variable();
          o.donnees.commentaire = variable();
          u1();
        }
        break;
      }
      default: break;
    }
    return o;
  }
};

// Construction de la carte de Prométhée depuis une plaque lue
class Construction {
 public:
  Construction(int version, const Plaque& p, RapportLectureCao& r) : v_(version), p_(p), r_(r), unite_(version >= 6 ? 100.0 : 10'000.0) {}

  ConceptionElec conception(const std::string& nom) {
    couchesCuivre();
    plans();
    composants();
    objetsLibres();
    contour();
    reseaux();
    ConceptionElec c = cao_interne::assemblerCarte(l_, nom, "sprint:", r_);
    return c;
  }
  // Macro : tous ses objets en une empreinte, centrée sur ses pastilles
  std::optional<DefEmpreinte> empreinte(std::string* alerte) {
    couchesCuivre();
    std::vector<std::size_t> tous;
    for (std::size_t j = 0; j < p_.objets.size(); ++j) tous.push_back(j);
    Pt position;
    double rotation = 0;
    DefEmpreinte d = definition(tous, 0, false, position, rotation, nullptr);
    if (d.pastilles.empty() && alerte) *alerte = "macro sans pastille";
    if (d.pastilles.empty() && d.traits.empty()) return std::nullopt;
    return cadrer(d);
  }

 private:
  int v_;
  const Plaque& p_;
  RapportLectureCao& r_;
  double unite_;  // nm par unité du fichier
  cao_interne::CarteLue l_;
  cao_interne::LiaisonsCuivre imposees_;
  std::map<std::size_t, cao_interne::Broche> brocheDe_;  // rang de l'objet pastille → broche
  std::set<std::string> pris_;
  std::array<std::optional<Couche>, 4> plan_{};  // couches à plan de masse (au plus quatre)
  Nm isolementPlan_ = 300'000;
  bool internes_ = false;
  std::vector<std::pair<Pt, Pt>> bord_;
  static constexpr const char* PLAN = "GND_PLANE";

  Nm lg(double v) const { return static_cast<Nm>(std::llround(v * unite_)); }
  Pt pt(float x, float y) const { return {lg(x), -lg(y)}; }
  enum class Role { Cuivre, Seri, Contour, Autre };
  struct Place {
    Role role = Role::Autre;
    Couche couche = Couche::CuivreDessus;
  };
  // Couches : C1, S1, C2, S2, I1, I2, O (avant la 4, C1 et C2 échangées, I1 et I2 servant aux macros)
  Place place(std::uint8_t c) const {
    if (v_ >= 4) switch (c) {
        case 1: return {Role::Cuivre, Couche::CuivreDessus};
        case 2: return {Role::Seri, Couche::SerigraphieDessus};
        case 3: return {Role::Cuivre, Couche::CuivreDessous};
        case 4: return {Role::Seri, Couche::SerigraphieDessous};
        case 5: return {Role::Cuivre, cuivreInterne(1)};
        case 6: return {Role::Cuivre, cuivreInterne(2)};
        case 7: return {Role::Contour, Couche::CuivreDessus};
        default: return {};
      }
    switch (c) {
      case 1: return {Role::Cuivre, v_ >= 3 ? Couche::CuivreDessus : Couche::CuivreDessous};
      case 2: return {Role::Seri, Couche::SerigraphieDessus};
      case 3: return {Role::Cuivre, v_ >= 3 ? Couche::CuivreDessous : Couche::CuivreDessus};
      case 4: return {Role::Seri, Couche::SerigraphieDessous};
      case 5: return {Role::Cuivre, Couche::CuivreDessous};
      case 6: return {Role::Seri, Couche::SerigraphieDessus};
      case 7: return {Role::Contour, Couche::CuivreDessus};
      default: return {};
    }
  }
  void couchesCuivre() {
    for (const Objet& o : p_.objets)
      if (v_ >= 4 && (o.couche == 5 || o.couche == 6)) internes_ = true;
    l_.cuivres = internes_ || p_.multicouche ? 4 : 2;
  }
  // Plans de masse : une zone « GND_PLANE » sur toute la carte par couche marquée ; s'y relient les objets posés
  // sans isolement (et les pastilles à liaison thermique)
  void plans() {
    const std::vector<std::pair<int, Couche>> marques = v_ >= 5 ? std::vector<std::pair<int, Couche>>{{0, Couche::CuivreDessus}, {2, Couche::CuivreDessous}, {4, cuivreInterne(1)}, {5, cuivreInterne(2)}}
                                                                : std::vector<std::pair<int, Couche>>{{0, Couche::CuivreDessus}, {1, Couche::CuivreDessous}};
    std::size_t k = 0;
    for (const auto& [i, c] : marques)
      if (p_.plans[static_cast<std::size_t>(i)] && k < plan_.size()) plan_[k++] = c;
    if (!k) return;
    std::map<std::int32_t, int> isolements;
    for (const Objet& o : p_.objets)
      if (o.isolement > 0 && place(o.couche).role == Role::Cuivre) ++isolements[o.isolement];
    if (!isolements.empty()) isolementPlan_ = lg(std::max_element(isolements.begin(), isolements.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first);
  }
  bool surPlan(Couche c) const {
    return std::any_of(plan_.begin(), plan_.end(), [&](const std::optional<Couche>& x) { return x && *x == c; });
  }
  bool reliePlan(const Objet& o, Couche c) const {
    const bool pastille = o.type == PastilleTrou || o.type == PastilleCms;
    // Une pastille traversante métallisée traverse toutes les couches : n'importe quel plan la reçoit
    const bool couche = surPlan(c) || (pastille && o.type == PastilleTrou && o.metallise && plan_[0].has_value());
    return couche && (o.isolement == 0 || (v_ >= 5 && pastille && o.miroirH != 0));
  }
  static double angleDe(const std::vector<Point>& s, std::size_t a, std::size_t b) {
    return std::atan2(static_cast<double>(s[b].y) - static_cast<double>(s[a].y), static_cast<double>(s[b].x) - static_cast<double>(s[a].x)) * 180 / std::numbers::pi;
  }
  // Pastille dans le repère de la carte ; « cuivre » : sa couche de cuivre sur la carte
  std::optional<Pastille> pastille(const Objet& o, Couche& cuivre) const {
    const Place k = place(o.couche);
    if (k.role != Role::Cuivre && o.type == PastilleCms) return std::nullopt;
    cuivre = k.role == Role::Cuivre ? k.couche : Couche::CuivreDessus;
    Pastille q;
    double angle = 0;
    const auto& s = o.points;
    if (s.size() == 2 || s.size() == 4) angle = angleDe(s, 0, 1);
    else if (s.size() == 8)
      angle = angleDe(s, 1, 2);
    angle = std::round(angle * 100) / 100;
    q.position = pt(o.x, o.y);
    if (o.type == PastilleTrou) {
      const Nm d = lg(2.0 * o.ext), t = lg(2.0 * o.inte);
      const bool metal = o.metallise != 0 || (v_ <= 3 && o.couche == 5);
      // Trou non métallisé entouré de cuivre (cartes simple face) : repris en trou métallisé
      if (metal || (t > 0 && d > t)) q.type = TypePastille::Traversante;
      else q.type = t > 0 ? TypePastille::NonMetallisee : TypePastille::CMS;
      q.percage = q.type == TypePastille::CMS ? 0 : t;
      q.largeur = q.hauteur = std::max<Nm>(d, 1);
      switch (o.forme) {
        case 4:
        case 5:
        case 6: q.largeur *= 2; break;
        case 7:
        case 8:
        case 9: q.hauteur *= 2; break;
        default: break;
      }
      switch (o.forme) {
        case 4:
        case 7: q.forme = FormePastille::Ovale; break;
        case 2:
        case 5:
        case 8:
          q.forme = FormePastille::Chanfreine;
          q.chanfrein = 0.25;
          break;
        case 3:
        case 6:
        case 9: q.forme = FormePastille::Rect; break;
        default: q.forme = FormePastille::Rond; break;
      }
      if (o.forme == 7) angle -= 90;
    } else {
      q.type = TypePastille::CMS;
      q.forme = FormePastille::Rect;
      q.largeur = std::max<Nm>(lg(o.ext), 1);
      q.hauteur = o.inte > 0 ? lg(o.inte) : q.largeur;
      if (!s.empty()) {
        double x = 0, y = 0;
        for (const auto& c : s) x += c.x, y += c.y;
        q.position = pt(static_cast<float>(x / static_cast<double>(s.size())), static_cast<float>(y / static_cast<double>(s.size())));
      }
    }
    q.rotation = angle;
    return q;
  }
  // Couches d'une pastille dans sa définition : face du cuivre ramenée au dessus si la pose est retournée ; vernis
  // absent (pastille couverte) ou couche interne écrits en clair
  static Couches couchesDef(const Pastille& q, Couche cuivre, bool dessous, bool vernis) {
    if (q.type == TypePastille::NonMetallisee) return 0;
    if (q.type == TypePastille::Traversante) return vernis ? 0 : bit(Couche::CuivreDessus);
    const Couche c = dessous && (cuivre == Couche::CuivreDessus || cuivre == Couche::CuivreDessous) ? opposee(cuivre) : cuivre;
    if (c == Couche::CuivreDessus) return vernis ? 0 : bit(Couche::CuivreDessus) | bit(Couche::PateDessus);
    if (c == Couche::CuivreDessous) return bit(Couche::CuivreDessous) | bit(Couche::PateDessous) | (vernis ? bit(Couche::VernisDessous) : 0);
    return bit(c);
  }
  bool estCuivre(const Objet& o) const { return place(o.couche).role == Role::Cuivre; }
  // Définition d'empreinte depuis des objets (rangs), pose dans le repère « position, rotation, dessous » : les
  // pastilles d'abord numérotées dans l'ordre (les polygones de cuivre du composant en sont, de forme libre), puis
  // la sérigraphie ; origine au centre de la boîte des pastilles
  DefEmpreinte definition(const std::vector<std::size_t>& rangs, double rotation, bool dessous, Pt& position, double& rotationPose, const std::string* ref) {
    // Centre de gravité des pastilles (insensible à la rotation), sinon de tous les points
    double gx = 0, gy = 0;
    int n = 0;
    auto ajouter = [&](Pt p) {
      gx += static_cast<double>(p.x);
      gy += static_cast<double>(p.y);
      ++n;
    };
    for (const std::size_t j : rangs) {
      const Objet& o = p_.objets[j];
      Couche c{};
      if (o.type == PastilleTrou || o.type == PastilleCms) {
        if (const auto q = pastille(o, c)) ajouter(q->position);
      } else if (o.type == Poly && estCuivre(o) && !o.interdit)
        for (const auto& s : o.points) ajouter(pt(s.x, s.y));
    }
    if (!n)
      for (const std::size_t j : rangs)
        for (const auto& s : p_.objets[j].points) ajouter(pt(s.x, s.y));
    EmpreintePlacee e;
    e.position = n ? Pt{static_cast<Nm>(std::llround(gx / n)), static_cast<Nm>(std::llround(gy / n))} : Pt{};
    e.rotation = rotation;
    e.dessous = dessous;
    DefEmpreinte d;
    int numero = 0;
    for (const std::size_t j : rangs) {
      const Objet& o = p_.objets[j];
      if (o.type == PastilleTrou || o.type == PastilleCms) {
        Couche c{};
        auto q = pastille(o, c);
        if (!q) continue;
        q->numero = std::to_string(++numero);
        q->couches = couchesDef(*q, c, dessous, o.vernis != 0);
        q->position = versEmpreinte(e, q->position);
        q->rotation = dessous ? -(q->rotation - rotation) : q->rotation - rotation;
        q->rotation = std::remainder(q->rotation, 360.0);
        if (std::abs(q->rotation) < 0.005) q->rotation = 0;
        if (ref) brocheDe_[j] = {*ref, q->numero};
        d.pastilles.push_back(*q);
      } else if (o.type == Poly && estCuivre(o) && !o.interdit && o.points.size() >= 3) {
        // Polygone de cuivre du composant (pastille dessinée) : une pastille CMS de forme libre
        Polygone poly;
        for (const auto& s : o.points) poly.push_back(versEmpreinte(e, pt(s.x, s.y)));
        poly = orienter(poly);
        const Boite b = boite(poly);
        Pastille q;
        q.numero = std::to_string(++numero);
        q.type = TypePastille::CMS;
        q.forme = FormePastille::Libre;
        q.position = b.centre();
        q.largeur = std::max<Nm>(b.largeur(), 1);
        q.hauteur = std::max<Nm>(b.hauteur(), 1);
        for (const Pt& s : poly) q.contourLibre.push_back(s - q.position);
        q.couches = couchesDef(q, place(o.couche).couche, dessous, true);
        if (ref) brocheDe_[j] = {*ref, q.numero};
        d.pastilles.push_back(q);
      }
    }
    for (const std::size_t j : rangs) {
      const Objet& o = p_.objets[j];
      const Place k = place(o.couche);
      if (k.role != Role::Seri) continue;
      const Couche seri = dessous ? opposee(k.couche) : k.couche;
      auto trait = [&](Pt a, Pt b, Nm ep) {
        TraitEmpreinte t;
        t.couche = seri;
        t.a = versEmpreinte(e, a);
        t.b = versEmpreinte(e, b);
        t.epaisseur = ep > 0 ? ep : 120'000;
        d.traits.push_back(t);
      };
      if (o.type == Ligne || o.type == Poly) {
        for (std::size_t s = 1; s < o.points.size(); ++s) trait(pt(o.points[s - 1].x, o.points[s - 1].y), pt(o.points[s].x, o.points[s].y), lg(o.largeur));
        if (o.type == Poly && o.points.size() >= 3) trait(pt(o.points.back().x, o.points.back().y), pt(o.points.front().x, o.points.front().y), lg(o.largeur));
      } else if (o.type == Segment)
        trait(pt(o.x, o.y), pt(o.ext, o.inte), lg(o.largeur));
      else if (o.type == Cercle) {
        const auto [entier, debut, balayage] = arc(o);
        const Pt c = pt(o.x, o.y);
        const double rayon = static_cast<double>(lg((o.ext + o.inte) / 2.0));
        const Nm ep = std::max<Nm>(lg(o.ext - o.inte), 50'000);
        if (entier) {
          TraitEmpreinte t;
          t.couche = seri;
          t.a = t.b = versEmpreinte(e, c);
          t.rayon = static_cast<Nm>(std::llround(rayon));
          t.epaisseur = ep;
          d.traits.push_back(t);
        } else {
          const auto pts = pointsArcDegres(c, rayon, debut, balayage);
          for (std::size_t s = 1; s < pts.size(); ++s) trait(pts[s - 1], pts[s], ep);
        }
      }
    }
    // Origine au centre de la boîte des pastilles (sinon des traits)
    Boite b;
    for (const auto& q : d.pastilles) b.etendre(q.position);
    if (b.vide())
      for (const auto& t : d.traits) b.etendre(t.a), b.etendre(t.b);
    const Pt centre = b.vide() ? Pt{} : b.centre();
    for (auto& q : d.pastilles) q.position = q.position - centre;
    for (auto& t : d.traits) t.a = t.a - centre, t.b = t.b - centre;
    position = versCarte(e, centre);
    rotationPose = rotation;
    return d;
  }
  // Cercle : entier ou arc (début, balayage) en degrés, sens direct ; angles en millièmes de degré dans la 6 quand
  // ils dépassent 1000
  std::tuple<bool, double, double> arc(const Objet& o) const {
    if (v_ < 3) return {true, 0, 360};
    double a = o.angleDebut, b = o.largeur;
    if (v_ >= 6 && (std::abs(a) > 1000 || std::abs(b) > 1000)) a /= 1000, b /= 1000;
    if ((a == 0 && b == 0) || b - a >= 360 || a == b) return {true, 0, 360};
    if (b <= a) b += 360;
    return {false, a, b - a};
  }
  void composants() {
    // Composants : objets portant le même numéro ; données (repère, boîtier, commentaire, rotation) sur le texte
    // repère, qui définit le composant
    std::map<std::uint16_t, std::vector<std::size_t>> parComposant;
    std::map<std::uint16_t, std::size_t> texteRepere, texteValeur;
    for (std::size_t j = 0; j < p_.objets.size(); ++j) {
      const Objet& o = p_.objets[j];
      if (!o.composant) continue;
      parComposant[o.composant].push_back(j);
      if (o.type == TexteTrait && o.forme == 1 && !texteRepere.count(o.composant)) texteRepere[o.composant] = j;
      if (o.type == TexteTrait && o.forme == 2 && !texteValeur.count(o.composant)) texteValeur[o.composant] = j;
    }
    Empreintes ranges(l_);
    int sansPastille = 0;
    for (const auto& [id, rangs] : parComposant) {
      const Objet* t = texteRepere.count(id) ? &p_.objets[texteRepere[id]] : nullptr;
      std::string ref = t ? cao_interne::texteWindows(t->texte) : std::string();
      ref.erase(std::remove_if(ref.begin(), ref.end(), [](char c) { return c == '\r' || c == '\n'; }), ref.end());
      ref = repereUnique(pris_, ref, "U" + std::to_string(id), r_);
      std::string valeur, boitier;
      double rotation = 0;
      if (t && t->donnees.valide) {
        valeur = cao_interne::texteWindows(t->donnees.commentaire);
        boitier = cao_interne::texteWindows(t->donnees.boitier);
        rotation = t->donnees.rotation;
      }
      if (valeur.empty() && t) valeur = cao_interne::texteWindows(t->identifiant);
      if (valeur.empty() && texteValeur.count(id)) valeur = cao_interne::texteWindows(p_.objets[texteValeur[id]].texte);
      // Face : celle des pastilles CMS s'il y en a, sinon celle de la sérigraphie (texte repère compris)
      int bas = 0, haut = 0, seriBas = 0, seriHaut = 0;
      std::vector<std::size_t> propres;  // pastilles, polygones de cuivre, sérigraphie : la définition
      for (const std::size_t j : rangs) {
        const Objet& o = p_.objets[j];
        const Place k = place(o.couche);
        const bool cms = o.type == PastilleCms || (o.type == Poly && k.role == Role::Cuivre && !o.interdit);
        if (cms && k.couche == Couche::CuivreDessous) ++bas;
        if (cms && k.couche == Couche::CuivreDessus) ++haut;
        if (k.role == Role::Seri) (k.couche == Couche::SerigraphieDessous ? seriBas : seriHaut) += 1;
        if (o.type == PastilleTrou || o.type == PastilleCms || (o.type == Poly && k.role == Role::Cuivre && !o.interdit) ||
            (k.role == Role::Seri && (o.type == Ligne || o.type == Poly || o.type == Segment || o.type == Cercle)))
          propres.push_back(j);
      }
      const bool dessous = bas + haut > 0 ? bas > haut : seriBas > seriHaut;
      Pt position;
      double rotationPose = 0;
      DefEmpreinte d = definition(propres, rotation, dessous, position, rotationPose, &ref);
      const std::string base = !boitier.empty() ? boitier : "composant-" + std::to_string(d.pastilles.size()) + "p";
      cao_interne::CarteLue::Pose pose;
      pose.ref = ref;
      pose.decal = ranges.ranger(base, d);
      pose.p = position;
      pose.rotation = rotationPose;
      pose.dessous = dessous;
      l_.poses.push_back(pose);
      if (d.pastilles.empty()) {
        // Dessin seul (logo, cadre) : sur la carte sans symbole
        l_.carteSeule.insert(ref);
        ++sansPastille;
      } else
        l_.netlist.composants.push_back({ref, valeur, pose.decal});
      // Le reste (cuivre tracé, contour, textes libres) suit le chemin des objets libres
      for (const std::size_t j : rangs)
        if (std::find(propres.begin(), propres.end(), j) == propres.end()) restants_.push_back(j);
    }
    if (sansPastille) r_.alertes.push_back(std::to_string(sansPastille) + " composant(s) sans pastille (dessins) posé(s) sur la carte seule");
    // Pastilles libres (cid 0) : une pièce de la carte seule chacune, « PL1 »…
    int libres = 0;
    for (std::size_t j = 0; j < p_.objets.size(); ++j) {
      const Objet& o = p_.objets[j];
      if (o.composant || (o.type != PastilleTrou && o.type != PastilleCms)) continue;
      Couche c{};
      auto q = pastille(o, c);
      if (!q) continue;
      const std::string ref = repereUnique(pris_, "PL" + std::to_string(++libres), "PL", r_);
      cao_interne::CarteLue::Pose pose;
      pose.ref = ref;
      pose.p = q->position;
      pose.rotation = q->rotation;
      q->position = {};
      q->rotation = 0;
      q->numero = "1";
      q->couches = couchesDef(*q, c, false, o.vernis != 0);
      DefEmpreinte d;
      d.pastilles.push_back(*q);
      pose.decal = ranges.ranger("pastille-libre", d);
      l_.poses.push_back(pose);
      l_.carteSeule.insert(ref);
      brocheDe_[j] = {ref, "1"};
      if (reliePlan(o, c)) imposees_.pastilles[PLAN].push_back({ref, "1"});
    }
    if (libres) r_.alertes.push_back(std::to_string(libres) + " pastille(s) libre(s) (vias, points de soudure) posée(s) sur la carte seule (PL1…)");
    // Pastilles des composants reliées au plan
    for (const auto& [j, b] : brocheDe_) {
      const Objet& o = p_.objets[j];
      if (!o.composant) continue;
      Couche c = place(o.couche).couche;
      if (o.type == PastilleTrou || o.type == PastilleCms) pastille(o, c);
      if (reliePlan(o, c)) imposees_.pastilles[PLAN].push_back(b);
    }
  }
  std::vector<std::size_t> restants_;
  void objetsLibres() {
    std::vector<std::size_t> rangs = restants_;
    for (std::size_t j = 0; j < p_.objets.size(); ++j)
      if (!p_.objets[j].composant) rangs.push_back(j);
    int interdits = 0;
    for (const std::size_t j : rangs) {
      const Objet& o = p_.objets[j];
      const Place k = place(o.couche);
      auto piste = [&](Pt a, Pt b, Nm larg, std::optional<Pt> milieu = std::nullopt) {
        Piste x;
        x.couche = k.couche;
        x.a = a;
        x.b = b;
        x.milieu = milieu;
        x.largeur = larg > 0 ? larg : 250'000;
        if (reliePlan(o, k.couche)) imposees_.pistes[PLAN].push_back(l_.pistes.size());
        l_.pistes.push_back(x);
      };
      switch (o.type) {
        case Ligne:
          if (k.role == Role::Contour)
            for (std::size_t s = 1; s < o.points.size(); ++s) bord_.push_back({pt(o.points[s - 1].x, o.points[s - 1].y), pt(o.points[s].x, o.points[s].y)});
          else if (k.role == Role::Cuivre)
            for (std::size_t s = 1; s < o.points.size(); ++s) piste(pt(o.points[s - 1].x, o.points[s - 1].y), pt(o.points[s].x, o.points[s].y), lg(o.largeur));
          else if (k.role == Role::Seri && o.points.size() >= 2) {
            DessinCircuit d;
            d.couche = k.couche;
            for (const auto& s : o.points) d.points.push_back(pt(s.x, s.y));
            d.epaisseur = o.largeur > 0 ? lg(o.largeur) : 250'000;
            l_.dessins.push_back(d);
          }
          break;
        case Segment: {
          const Pt a = pt(o.x, o.y), b = pt(o.ext, o.inte);
          if (o.largeur == 0 && a == b) break;
          if (k.role == Role::Contour) bord_.push_back({a, b});
          else if (k.role == Role::Cuivre) piste(a, b, lg(o.largeur));
          else if (k.role == Role::Seri) {
            DessinCircuit d;
            d.couche = k.couche;
            d.points = {a, b};
            d.epaisseur = std::max<Nm>(lg(o.largeur), 50'000);
            l_.dessins.push_back(d);
          }
          break;
        }
        case Poly: {
          if (o.points.size() < 2) break;
          Polygone q;
          for (const auto& s : o.points) q.push_back(pt(s.x, s.y));
          if (k.role == Role::Contour) {
            for (std::size_t s = 0; s < q.size(); ++s) bord_.push_back({q[s], q[(s + 1) % q.size()]});
          } else if (k.role == Role::Cuivre && q.size() >= 3) {
            if (o.interdit) {
              // Évidement du plan de masse : zone d'interdiction des remplissages sur sa couche
              ZoneRegle z;
              z.nom = "évidement " + std::to_string(++interdits);
              z.contour = orienter(q);
              z.couches = bit(k.couche);
              z.interditZones = true;
              l_.zonesRegles.push_back(z);
            } else {
              RegionCuivre g;
              g.genre = "libre";
              g.couche = k.couche;
              g.forme = orienter(q);
              if (reliePlan(o, k.couche)) imposees_.regions[PLAN].push_back(l_.regions.size());
              l_.regions.push_back(g);
            }
          } else if (k.role == Role::Seri && q.size() >= 3) {
            DessinCircuit d;
            d.forme = DessinCircuit::Forme::Polygone;
            d.couche = k.couche;
            d.points = q;
            d.plein = true;
            d.epaisseur = std::max<Nm>(lg(o.largeur), 0);
            l_.dessins.push_back(d);
          }
          break;
        }
        case Cercle: {
          const auto [entier, debut, balayage] = arc(o);
          const Pt c = pt(o.x, o.y);
          const double rayon = static_cast<double>(lg((o.ext + o.inte) / 2.0));
          const Nm ep = o.ext > o.inte ? lg(o.ext - o.inte) : 250'000;
          if (k.role == Role::Contour) {
            const auto pts = pointsArcDegres(c, rayon, debut, balayage);
            for (std::size_t s = 1; s < pts.size(); ++s) bord_.push_back({pts[s - 1], pts[s]});
          } else if (k.role == Role::Cuivre) {
            // Arc de piste ; un cercle entier en deux moitiés
            const int morceaux = entier ? 2 : 1;
            for (int m = 0; m < morceaux; ++m) {
              const double d0 = debut + balayage * m / morceaux, b = balayage / morceaux;
              const auto pts = pointsArcDegres(c, rayon, d0, b);
              piste(pts.front(), pts.back(), ep, pointsArcDegres(c, rayon, d0 + b / 2, 0).front());
            }
          } else if (k.role == Role::Seri) {
            DessinCircuit d;
            d.couche = k.couche;
            d.epaisseur = ep;
            if (entier) {
              d.forme = DessinCircuit::Forme::Cercle;
              d.points = {c, c + Pt{static_cast<Nm>(std::llround(rayon)), 0}};
            } else {
              d.forme = DessinCircuit::Forme::Arc;
              d.points = {pointsArcDegres(c, rayon, debut, 0).front(), pointsArcDegres(c, rayon, debut + balayage / 2, 0).front(), pointsArcDegres(c, rayon, debut + balayage, 0).front()};
            }
            l_.dessins.push_back(d);
          }
          break;
        }
        case TexteTrait:
        case TexteContour: {
          // Repère et valeur d'un composant : écrits par Prométhée ; les autres textes posés à leur place
          if (o.composant && o.type == TexteTrait && (o.forme == 1 || o.forme == 2)) break;
          if (o.texte.empty() || (k.role != Role::Cuivre && k.role != Role::Seri)) break;
          TexteCircuit x;
          x.texte = cao_interne::texteWindows(o.texte);
          x.couche = k.couche;
          // Rotation de Sprint-Layout dans le sens horaire ; miroir : rotation inversée, miroir vertical d'un demi-tour
          const double rho = v_ == 4 ? o.angleDebut * 90.0 : static_cast<double>(o.rotation);
          const bool miroir = (o.miroirH != 0) != (o.miroirV != 0);
          x.rotation = std::remainder((miroir ? rho : -rho) + (o.miroirV ? 180.0 : 0.0), 360.0);
          if (miroir) x.miroir = true;
          const double h0 = o.type == TexteTrait ? static_cast<double>(lg(o.ext)) * 0.75 : static_cast<double>(lg(std::abs(o.largeur) * 100.0 / 133)) * 0.75;
          // Boîte des traits du texte, dans son sens de lecture : le texte y est centré et ajusté (police différente)
          std::vector<Pt> traits;
          for (const Objet& e : o.enfants) {
            if (e.type == Ligne)
              for (const auto& s : e.points) traits.push_back(pt(s.x, s.y));
            else if (e.type == Segment)
              traits.push_back(pt(e.x, e.y)), traits.push_back(pt(e.ext, e.inte));
          }
          if (traits.empty()) {
            x.p = pt(o.x, o.y);
            x.hauteur = std::max<Nm>(static_cast<Nm>(h0), 200'000);
          } else {
            const double a = x.rotation * std::numbers::pi / 180;
            const double ux = std::cos(a), uy = -std::sin(a), vx = std::sin(a), vy = std::cos(a);
            double u0 = 1e300, u1 = -1e300, w0 = 1e300, w1 = -1e300;
            for (const Pt& q : traits) {
              const double u = static_cast<double>(q.x) * ux + static_cast<double>(q.y) * uy, w = static_cast<double>(q.x) * vx + static_cast<double>(q.y) * vy;
              u0 = std::min(u0, u), u1 = std::max(u1, u), w0 = std::min(w0, w), w1 = std::max(w1, w);
            }
            const double um = (u0 + u1) / 2, wm = (w0 + w1) / 2;
            x.p = {static_cast<Nm>(std::llround(um * ux + wm * vx)), static_cast<Nm>(std::llround(um * uy + wm * vy))};
            ajusterTexte(x, u1 - u0, std::min(h0 > 0 ? h0 : w1 - w0, w1 - w0 > 0 ? w1 - w0 : h0));
          }
          l_.textes.push_back(x);
          break;
        }
        default: break;
      }
    }
  }
  void contour() {
    if (cao_interne::contourDepuisTraits(l_, bord_)) return;
    if (!bord_.empty()) r_.alertes.push_back("contour de la couche O non fermé : la carte prend la taille du fichier");
    // Taille de la carte (Sprint-Layout la donne toujours), origine en haut à gauche
    const Nm w = lg(p_.largeur), h = lg(p_.hauteur);
    if (w > 0 && h > 0) l_.contour = {{0, 0}, {w, 0}, {w, h}, {0, h}};
  }
  void reseaux() {
    // Chevelu gardé par le fichier : pastilles reliées sans cuivre
    for (std::size_t j = 0; j < p_.liaisons.size(); ++j)
      for (const std::uint32_t k : p_.liaisons[j])
        if (k > j && brocheDe_.count(j) && brocheDe_.count(k)) imposees_.chevelu.push_back({brocheDe_[j], brocheDe_[k]});
    cao_interne::reseauxDepuisCuivre(l_, imposees_);
    r_.alertes.push_back("Sprint-Layout ne garde pas de netlist : réseaux tirés du cuivre connecté et du chevelu (N$1…)");
    // Plans de masse : zones du réseau GND_PLANE sur tout le contour
    int n = 0;
    for (const auto& c : plan_)
      if (c) {
        Zone z;
        z.couche = *c;
        z.reseau = PLAN;
        z.nom = std::string(PLAN) + (n ? "-" + std::to_string(n + 1) : std::string());
        z.contour = orienter(l_.contour);
        z.isolement = isolementPlan_;
        l_.zones.push_back(z);
        ++n;
      }
    if (n) r_.alertes.push_back(std::to_string(n) + " plan(s) de masse repris en zone(s) « GND_PLANE » ; isolement " + std::to_string(mm(isolementPlan_)).substr(0, 4) + " mm, le plus courant du fichier");
  }
};
}  // namespace sprint
}  // namespace

bool estSprintLayout(const std::string& o) {
  return o.size() >= 8 && static_cast<unsigned char>(o[0]) <= 6 && static_cast<unsigned char>(o[1]) == 0x33 && static_cast<unsigned char>(o[2]) == 0xAA && static_cast<unsigned char>(o[3]) == 0xFF;
}

std::size_t nombreCartesSprintLayout(const std::string& octets) {
  try {
    return sprint::Lecteur(octets).carte().plaques.size();
  } catch (const std::exception&) {
    return 0;
  }
}

ConceptionElec conceptionDepuisSprintLayout(const std::string& octets, const std::string& nom, RapportLectureCao* rapport, std::size_t rang) {
  RapportLectureCao r;
  sprint::Fichier f;
  try {
    f = sprint::Lecteur(octets).carte();
  } catch (const std::exception& e) {
    r.alertes.push_back(std::string("fichier Sprint-Layout illisible : ") + e.what());
    if (rapport) *rapport = r;
    return nouvelleConception(nom);
  }
  if (rang >= f.plaques.size()) rang = 0;
  if (f.plaques.size() > 1)
    r.alertes.push_back("fichier à " + std::to_string(f.plaques.size()) + " cartes : la carte " + std::to_string(rang + 1) + " (« " + cao_interne::texteWindows(f.plaques[rang].nom) + " ») est lue, les autres s'ouvrent une à une");
  sprint::Construction c(f.version, f.plaques[rang], r);
  ConceptionElec x = c.conception(nom);
  if (rapport) *rapport = r;
  return x;
}

std::optional<DefEmpreinte> empreinteDepuisMacroSprint(const std::string& octets, const std::string& nom, std::string* alerte) {
  try {
    sprint::Lecteur lecteur(octets);
    const sprint::Plaque p = lecteur.macro();
    RapportLectureCao r;
    const int version = static_cast<unsigned char>(octets[0]);
    sprint::Construction c(version, p, r);
    auto d = c.empreinte(alerte);
    if (d) {
      d->id = "sprint:" + nom;
      d->description = nom;
      d->famille = "Importées de Sprint-Layout";
    }
    return d;
  } catch (const std::exception& e) {
    if (alerte) *alerte = std::string("macro illisible : ") + e.what();
    return std::nullopt;
  }
}

}  // namespace prom::ecad
