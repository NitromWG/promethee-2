// Prométhée : imports côté schéma (10.14). Voir import_schemas.hpp. Licence GPL-3.0-only.
#include "promethee/import_schemas.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

#include "promethee/archive.hpp"
#include "xml_interne.hpp"

namespace prom::ecad {

namespace {
std::vector<std::string> lignes(const std::string& t) {
  std::vector<std::string> r;
  std::istringstream in(t);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    r.push_back(l);
  }
  return r;
}
// Mots d'une ligne ; un mot entre guillemets peut porter des espaces
std::vector<std::string> mots(const std::string& s) {
  std::vector<std::string> r;
  std::size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size()) break;
    std::string m;
    if (s[i] == '"') {
      for (++i; i < s.size() && s[i] != '"'; ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) ++i;
        m += s[i];
      }
      ++i;
    } else
      while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) m += s[i++];
    r.push_back(m);
  }
  return r;
}
double reel(const std::string& s) {
  char* fin = nullptr;
  const double v = std::strtod(s.c_str(), &fin);
  return fin == s.c_str() ? 0 : v;
}
int entier(const std::string& s) { return static_cast<int>(std::lround(reel(s))); }

// Orientation d'un symbole posé : la matrice T (repère du symbole, y vers le bas → décalage sur la feuille), écrite
// comme SymbolePlace : miroir gauche-droite puis quarts de tour, T = R^k · F^m avec R = [[0, 1], [−1, 0]] et
// F = [[−1, 0], [0, 1]]
struct Orientation {
  bool miroir = false;
  int quarts = 0;
};
Orientation orientationDe(int a, int b, int c, int d) {
  for (int m = 0; m < 2; ++m)
    for (int k = 0; k < 4; ++k) {
      int A = m ? -1 : 1, B = 0, C = 0, D = 1;
      for (int i = 0; i < k; ++i) {
        const int nA = C, nB = D, nC = -A, nD = -B;
        A = nA, B = nB, C = nC, D = nD;
      }
      if (A == a && B == b && C == c && D == d) return {m == 1, k};
    }
  return {};
}

// ---------- KiCad 5 : bibliothèques (.lib, cache du projet) ----------
TypeBroche typeKicad5(const std::string& t) {
  if (t == "I") return TypeBroche::Entree;
  if (t == "O") return TypeBroche::Sortie;
  if (t == "B") return TypeBroche::Bidirectionnelle;
  if (t == "T") return TypeBroche::TroisEtats;
  if (t == "W") return TypeBroche::AlimEntree;
  if (t == "w") return TypeBroche::AlimSortie;
  if (t == "C") return TypeBroche::CollecteurOuvert;
  if (t == "N") return TypeBroche::NonConnectee;
  if (t == "U") return TypeBroche::NonSpecifiee;
  return TypeBroche::Passive;
}
// KiCad 5 : millièmes de pouce ; la bibliothèque a y vers le haut, la feuille y vers le bas
Pt mils(double x, double y) { return ptMm(x * 0.0254, y * 0.0254); }

struct SymboleK5 {
  DefSymbole def;
  std::string nomAlim;  // symbole d'alimentation : nom de sa broche d'alimentation cachée, qui nomme le réseau
};
std::map<std::string, SymboleK5> lireLibKicad5(const std::string& texte) {
  std::map<std::string, SymboleK5> r;
  SymboleK5 courant;
  std::vector<std::string> alias;
  bool dedans = false, dessin = false, puissance = false;
  auto garder = [&] {
    // Symbole d'alimentation (drapeau « P ») : relié par le nom de sa broche cachée d'entrée d'alimentation ; sans
    // elle (PWR_FLAG), c'est un drapeau qui déclare le réseau alimenté sans le nommer
    if (puissance) {
      for (const auto& b : courant.def.broches)
        if (b.cachee && b.type == TypeBroche::AlimEntree && !b.nom.empty()) courant.nomAlim = b.nom;
      courant.def.alimentation = !courant.nomAlim.empty();
      courant.def.drapeau = courant.nomAlim.empty();
    }
    r[courant.def.nom] = courant;
    for (const auto& a : alias) {
      SymboleK5 x = courant;
      x.def.nom = a;
      x.def.id = "kicad5:" + a;
      r[a] = x;
    }
  };
  for (const auto& l : lignes(texte)) {
    const auto m = mots(l);
    if (m.empty() || m[0][0] == '#') continue;
    if (m[0] == "DEF" && m.size() >= 3) {
      courant = SymboleK5{};
      alias.clear();
      std::string nom = m[1];
      if (!nom.empty() && nom[0] == '~') nom.erase(0, 1);  // valeur masquée
      courant.def.id = "kicad5:" + nom;
      courant.def.nom = nom;
      courant.def.prefixe = m[2] == "~" ? "U" : m[2];
      courant.def.categorie = "Importés de KiCad 5";
      if (m.size() >= 8) courant.def.unites = std::max(1, entier(m[7]));
      puissance = m.size() >= 10 && m[9] == "P";
      dedans = true;
      dessin = false;
    } else if (m[0] == "ENDDEF") {
      if (dedans) garder();
      dedans = dessin = false;
    } else if (!dedans)
      continue;
    else if (m[0] == "ALIAS")
      alias.insert(alias.end(), m.begin() + 1, m.end());
    else if (m[0] == "DRAW")
      dessin = true;
    else if (m[0] == "ENDDRAW")
      dessin = false;
    else if (m[0] == "F2" && m.size() >= 2)
      courant.def.empreinte = m[1];
    else if (dessin && m[0] == "X" && m.size() >= 12) {
      if (m[10] == "2") continue;  // vue De Morgan : la vue normale suffit aux réseaux
      Broche b;
      b.nom = m[1] == "~" ? std::string() : m[1];
      b.numero = m[2];
      b.extremite = mils(reel(m[3]), -reel(m[4]));
      b.longueur = nm(reel(m[5]) * 0.0254);
      // Lettre : sens de l'extrémité vers le corps (U, D : haut, bas à l'écran)
      b.direction = m[6] == "R" ? 0 : m[6] == "D" ? 1 : m[6] == "L" ? 2 : 3;
      b.unite = courant.def.unites > 1 ? entier(m[9]) : 0;
      b.type = typeKicad5(m[11]);
      b.cachee = m.size() >= 13 && m[12].find('N') != std::string::npos;
      if (m.size() >= 13 && m[12].find('I') != std::string::npos) b.style = "inversee";
      courant.def.broches.push_back(b);
    } else if (dessin && m[0] == "S" && m.size() >= 5) {
      if (m.size() >= 7 && m[6] == "2") continue;
      Trace t;
      t.forme = Trace::Forme::Rectangle;
      t.points = {mils(reel(m[1]), -reel(m[2])), mils(reel(m[3]), -reel(m[4]))};
      t.unite = m.size() >= 6 ? entier(m[5]) : 0;
      t.fond = m.size() >= 9 && m[8] == "f";
      t.rempli = m.size() >= 9 && m[8] == "F";
      courant.def.traces.push_back(t);
    } else if (dessin && m[0] == "P" && m.size() >= 5) {
      if (m[3] == "2") continue;
      const int n = entier(m[1]);
      Trace t;
      t.forme = Trace::Forme::Polyligne;
      t.unite = entier(m[2]);
      for (int i = 0; i < n; ++i) {
        const std::size_t k = static_cast<std::size_t>(5 + 2 * i);
        if (k + 1 >= m.size()) break;
        t.points.push_back(mils(reel(m[k]), -reel(m[k + 1])));
      }
      t.rempli = m.back() == "F";
      t.fond = m.back() == "f";
      if (t.points.size() >= 2) courant.def.traces.push_back(t);
    } else if (dessin && m[0] == "C" && m.size() >= 4) {
      if (m.size() >= 6 && m[5] == "2") continue;
      Trace t;
      t.forme = Trace::Forme::Cercle;
      t.points = {mils(reel(m[1]), -reel(m[2]))};
      t.rayon = nm(reel(m[3]) * 0.0254);
      t.unite = m.size() >= 5 ? entier(m[4]) : 0;
      courant.def.traces.push_back(t);
    }
  }
  return r;
}

struct ImporteurK5 {
  ConceptionElec& c;
  const LecteurFichier& lire;
  RapportImportSchema& r;
  std::map<std::string, SymboleK5> lib;
  std::map<std::string, std::string> parRef;  // repère → UUID du composant
  std::set<std::string> enCours;              // feuilles en cours de lecture (une feuille qui s'inclut elle-même)

  const SymboleK5* symbole(const std::string& libNom) const {
    // Le cache nomme « Device_R » ce que la feuille appelle « Device:R » ; une bibliothèque de projet, « R »
    std::string cle = libNom;
    std::replace(cle.begin(), cle.end(), ':', '_');
    if (const auto it = lib.find(cle); it != lib.end()) return &it->second;
    const auto p = libNom.find(':');
    if (const auto it = lib.find(p == std::string::npos ? libNom : libNom.substr(p + 1)); it != lib.end()) return &it->second;
    return nullptr;
  }

  // Lit une feuille ; « chemin » : horodatages des blocs depuis la racine (« /5F0A1B2C »), qui choisissent le repère
  // de chaque exemplaire d'une feuille posée plusieurs fois (lignes « AR »)
  std::size_t feuille(const std::string& texte, const std::string& nomFeuille, const std::string& chemin) {
    Feuille f;
    f.uuid = nouvelUuid();
    f.nom = nomFeuille;
    f.format = "A3";
    const auto ls = lignes(texte);
    for (std::size_t i = 0; i < ls.size(); ++i) {
      const auto m = mots(ls[i]);
      if (m.empty()) continue;
      if (m[0] == "$Comp") {
        std::string libNom, ref, valeur, empreinte, horodatage;
        std::map<std::string, std::string> refsParChemin;
        int unite = 1;
        Pt pos;
        int a = 1, b = 0, cc = 0, d = -1;
        for (++i; i < ls.size(); ++i) {
          const auto x = mots(ls[i]);
          if (x.empty()) continue;
          if (x[0] == "$EndComp") break;
          if (x[0] == "L" && x.size() >= 3) libNom = x[1], ref = x[2];
          else if (x[0] == "U" && x.size() >= 2) {
            unite = std::max(1, entier(x[1]));
            if (x.size() >= 4) horodatage = x[3];
          } else if (x[0] == "P" && x.size() >= 3)
            pos = mils(reel(x[1]), reel(x[2]));
          else if (x[0] == "AR") {
            std::string p, rf;
            for (const auto& y : x) {
              if (y.rfind("Path=", 0) == 0) p = y.substr(5);
              if (y.rfind("Ref=", 0) == 0) rf = y.substr(4);
            }
            if (!p.empty() && !rf.empty()) refsParChemin[p] = rf;
          } else if (x[0] == "F" && x.size() >= 3) {
            if (x[1] == "0") ref = x[2];
            else if (x[1] == "1") valeur = x[2];
            else if (x[1] == "2") empreinte = x[2];
          } else if (x.size() == 4 && (x[0] == "1" || x[0] == "0" || x[0] == "-1"))
            a = entier(x[0]), b = entier(x[1]), cc = entier(x[2]), d = entier(x[3]);
        }
        if (const auto it = refsParChemin.find(chemin + "/" + horodatage); it != refsParChemin.end()) ref = it->second;
        const SymboleK5* s = symbole(libNom);
        if (!s) {
          r.alertes.push_back("Symbole « " + libNom + " » de " + ref + " absent du cache et des bibliothèques : ignoré");
          continue;
        }
        const bool horsListe = !ref.empty() && ref[0] == '#';
        if (horsListe && !s->def.alimentation && !s->def.drapeau) continue;  // logo, mire : ni réseau ni nomenclature
        c.symboles[s->def.id] = s->def;
        SymbolePlace sp;
        sp.uuid = nouvelUuid();
        sp.symbole = s->def.id;
        sp.position = pos;
        // Matrice de KiCad 5 [x1 y1 ; x2 y2] : bibliothèque (y vers le haut) → feuille ; ici y vers le bas
        const Orientation o = orientationDe(a, -b, cc, -d);
        sp.miroir = o.miroir;
        sp.rotation = o.quarts;
        sp.unite = unite;
        if (s->def.alimentation)
          sp.nomAlim = s->nomAlim;
        else if (!s->def.drapeau) {
          auto u = parRef.find(ref);
          if (u == parRef.end()) {
            ComposantElec x;
            x.uuid = nouvelUuid();
            x.reference = ref;
            x.valeur = valeur;
            x.symbole = s->def.id;
            x.empreinte = empreinte;
            c.composants.push_back(x);
            u = parRef.emplace(ref, x.uuid).first;
            ++r.composants;
          }
          sp.composant = u->second;
        }
        f.symboles.push_back(sp);
      } else if (m[0] == "Wire" && m.size() >= 3 && i + 1 < ls.size()) {
        const auto p = mots(ls[++i]);
        if (p.size() < 4) continue;
        if (m[1] == "Wire") {
          f.fils.push_back({nouvelUuid(), mils(reel(p[0]), reel(p[1])), mils(reel(p[2]), reel(p[3]))});
          ++r.fils;
        } else if (m[1] == "Bus")
          r.alertes.push_back("Bus de KiCad 5 dessiné mais non repris : ses étiquettes de membres relient les réseaux");
      } else if (m[0] == "Connection" && m.size() >= 4)
        f.jonctions.push_back({mils(reel(m[2]), reel(m[3])), nouvelUuid()});
      else if (m[0] == "NoConn" && m.size() >= 4)
        f.nonConnexions.push_back({mils(reel(m[2]), reel(m[3])), nouvelUuid()});
      else if (m[0] == "Text" && m.size() >= 4 && i + 1 < ls.size()) {
        const std::string suite = ls[++i];
        const Pt p = mils(reel(m[2]), reel(m[3]));
        if (m[1] == "Label" || m[1] == "GLabel" || m[1] == "HLabel") {
          Etiquette e;
          e.uuid = nouvelUuid();
          e.texte = suite;
          e.p = p;
          e.type = m[1] == "Label" ? TypeEtiquette::Locale : m[1] == "GLabel" ? TypeEtiquette::Globale : TypeEtiquette::Hierarchique;
          f.etiquettes.push_back(e);
          ++r.etiquettes;
        } else {
          TexteSchema t;
          t.uuid = nouvelUuid();
          t.texte = suite;
          for (std::size_t k; (k = t.texte.find("\\n")) != std::string::npos;) t.texte.replace(k, 2, "\n");
          t.p = p;
          f.textes.push_back(t);
        }
      } else if (m[0] == "$Sheet") {
        BlocFeuille bloc;
        bloc.uuid = nouvelUuid();
        std::string fichier, horodatage;
        for (++i; i < ls.size(); ++i) {
          const auto x = mots(ls[i]);
          if (x.empty()) continue;
          if (x[0] == "$EndSheet") break;
          if (x[0] == "S" && x.size() >= 5) {
            bloc.position = mils(reel(x[1]), reel(x[2]));
            bloc.largeur = nm(reel(x[3]) * 0.0254);
            bloc.hauteur = nm(reel(x[4]) * 0.0254);
          } else if (x[0] == "U" && x.size() >= 2)
            horodatage = x[1];
          else if (x[0] == "F0" && x.size() >= 2)
            bloc.nom = x[1];
          else if (x[0] == "F1" && x.size() >= 2)
            fichier = x[1];
          else if (x[0].size() >= 2 && x[0][0] == 'F' && std::isdigit(static_cast<unsigned char>(x[0][1])) && x.size() >= 6) {
            PortBloc p;
            p.uuid = nouvelUuid();
            p.nom = x[1];
            p.sens = x[2] == "I" ? SensSignal::Entree : x[2] == "O" ? SensSignal::Sortie : x[2] == "B" ? SensSignal::Bidirectionnel : x[2] == "T" ? SensSignal::TroisEtats : SensSignal::Passif;
            p.cote = x[3] == "L" ? 0 : x[3] == "R" ? 1 : x[3] == "T" ? 2 : 3;
            p.p = mils(reel(x[4]), reel(x[5]));
            bloc.ports.push_back(p);
          }
        }
        if (!fichier.empty()) {
          if (enCours.count(fichier))
            r.alertes.push_back("Feuille « " + fichier + " » incluse dans elle-même : non relue");
          else if (const auto contenu = lire ? lire(fichier) : std::nullopt) {
            enCours.insert(fichier);
            const std::size_t k = feuille(*contenu, bloc.nom.empty() ? fichier : bloc.nom, chemin + "/" + horodatage);
            enCours.erase(fichier);
            bloc.feuille = c.feuilles[k].uuid;
          } else
            r.alertes.push_back("Feuille « " + fichier + " » introuvable : son bloc reste vide");
        }
        f.blocs.push_back(bloc);
        ++r.blocs;
      }
    }
    c.feuilles.push_back(std::move(f));
    ++r.feuilles;
    return c.feuilles.size() - 1;
  }
};

// ---------- LTspice ----------
struct BrocheLt {
  Pt p;  // unités d'LTspice, y vers le bas
  std::string nom;
  int ordre = 0;  // ordre SPICE (numéro de la broche)
};
struct SymboleLt {
  std::vector<BrocheLt> broches;
  std::vector<std::pair<Pt, Pt>> traits;
  std::string prefixe = "X";
};
// Symboles de base d'LTspice (positions des broches de leurs .asy), quand le .asy n'est pas fourni
std::optional<SymboleLt> symboleLtDeBase(std::string n) {
  for (char& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (const auto p = n.find_last_of("\\/"); p != std::string::npos) n = n.substr(p + 1);
  SymboleLt s;
  if (n == "res" || n == "ind") {
    s.broches = {{{16, 16}, "A", 1}, {{16, 96}, "B", 2}};
    s.traits = {{{16, 16}, {16, 96}}, {{0, 24}, {32, 24}}, {{0, 88}, {32, 88}}};
    s.prefixe = n == "res" ? "R" : "L";
  } else if (n == "cap" || n == "polcap") {
    s.broches = {{{16, 0}, "A", 1}, {{16, 64}, "B", 2}};
    s.traits = {{{16, 0}, {16, 28}}, {{0, 28}, {32, 28}}, {{0, 36}, {32, 36}}, {{16, 36}, {16, 64}}};
    s.prefixe = "C";
  } else if (n == "voltage") {
    s.broches = {{{0, 16}, "+", 1}, {{0, 96}, "-", 2}};
    s.traits = {{{0, 16}, {0, 24}}, {{-32, 24}, {32, 24}}, {{-32, 88}, {32, 88}}, {{0, 88}, {0, 96}}};
    s.prefixe = "V";
  } else if (n == "diode" || n == "zener" || n == "schottky") {
    s.broches = {{{16, 0}, "A", 1}, {{16, 64}, "K", 2}};
    s.traits = {{{16, 0}, {16, 64}}, {{0, 20}, {32, 20}}, {{0, 44}, {32, 44}}};
    s.prefixe = "D";
  } else if (n == "npn" || n == "pnp") {
    s.broches = {{{64, 0}, "C", 1}, {{0, 48}, "B", 2}, {{64, 96}, "E", 3}};
    s.traits = {{{16, 16}, {16, 80}}, {{0, 48}, {16, 48}}, {{16, 32}, {64, 0}}, {{16, 64}, {64, 96}}};
    s.prefixe = "Q";
  } else if (n == "nmos" || n == "pmos") {
    s.broches = {{{48, 0}, "D", 1}, {{0, 80}, "G", 2}, {{48, 96}, "S", 3}};
    s.traits = {{{16, 32}, {16, 80}}, {{0, 80}, {16, 80}}, {{48, 0}, {48, 32}}, {{48, 96}, {48, 64}}};
    s.prefixe = "M";
  } else
    return std::nullopt;
  return s;
}
std::optional<SymboleLt> lireAsy(const std::string& t) {
  SymboleLt s;
  for (const auto& l : lignes(t)) {
    const auto m = mots(l);
    if (m.empty()) continue;
    if (m[0] == "PIN" && m.size() >= 3) s.broches.push_back({{static_cast<Nm>(entier(m[1])), static_cast<Nm>(entier(m[2]))}, std::to_string(s.broches.size() + 1), static_cast<int>(s.broches.size()) + 1});
    else if (m[0] == "PINATTR" && m.size() >= 3 && !s.broches.empty()) {
      if (m[1] == "PinName") s.broches.back().nom = m[2];
      if (m[1] == "SpiceOrder") s.broches.back().ordre = entier(m[2]);
    } else if (m[0] == "LINE" && m.size() >= 6)
      s.traits.push_back({{static_cast<Nm>(entier(m[2])), static_cast<Nm>(entier(m[3]))}, {static_cast<Nm>(entier(m[4])), static_cast<Nm>(entier(m[5]))}});
    else if (m[0] == "SYMATTR" && m.size() >= 3 && m[1] == "Prefix")
      s.prefixe = m[2];
  }
  if (s.broches.empty()) return std::nullopt;
  return s;
}
// Seize unités d'LTspice (un pas de sa grille) font 2,54 mm
constexpr double LT_MM = 2.54 / 16;
Pt lt(double x, double y) { return ptMm(x * LT_MM, y * LT_MM); }
std::string apresMot(const std::string& ligne, const std::string& mot) {
  const auto p = ligne.find(mot);
  if (p == std::string::npos || p + mot.size() + 1 > ligne.size()) return {};
  return ligne.substr(p + mot.size() + 1);
}
}  // namespace

bool estSchemaKicad5(const std::string& t) { return t.rfind("EESchema Schematic File", 0) == 0; }
bool estSchemaLtspice(const std::string& t) {
  std::size_t p = 0;
  if (t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF && static_cast<unsigned char>(t[1]) == 0xBB) p = 3;
  while (p < t.size() && std::isspace(static_cast<unsigned char>(t[p]))) ++p;
  return t.compare(p, 8, "Version ") == 0 && (t.find("\nSHEET") != std::string::npos || t.find("\nWIRE") != std::string::npos || t.find("\nSYMBOL") != std::string::npos);
}

ConceptionElec conceptionDepuisKicad5(const std::string& racine, const std::string& nom, const LecteurFichier& lire, RapportImportSchema* rapport) {
  RapportImportSchema r;
  ConceptionElec c = nouvelleConception(nom);
  c.feuilles.clear();
  c.schemaSeul = true;
  ImporteurK5 imp{c, lire, r, {}, {}, {}};
  // Cache du projet d'abord (les symboles tels qu'ils étaient posés), puis la bibliothèque du projet
  for (const std::string& f : {nom + "-cache.lib", nom + ".lib"})
    if (const auto t = lire ? lire(f) : std::nullopt)
      for (auto& [k, v] : lireLibKicad5(*t)) imp.lib.try_emplace(k, v);
  if (imp.lib.empty()) r.alertes.push_back("Ni cache (« " + nom + "-cache.lib ») ni bibliothèque : les symboles ne peuvent pas être repris");
  if (!estSchemaKicad5(racine)) r.alertes.push_back("Ce fichier n'est pas un schéma KiCad 5 (« EESchema Schematic File »)");
  imp.feuille(racine, nom.empty() ? "Feuille 1" : nom, "");
  // Les filles sont écrites avant leur mère : la racine passe en tête
  if (c.feuilles.size() > 1) std::rotate(c.feuilles.begin(), c.feuilles.end() - 1, c.feuilles.end());
  synchroniserReseaux(c);
  if (rapport) *rapport = r;
  return c;
}

ConceptionElec conceptionDepuisLtspice(const std::string& asc, const std::string& nom, const LecteurFichier& lire, RapportImportSchema* rapport) {
  RapportImportSchema r;
  ConceptionElec c = nouvelleConception(nom);
  c.schemaSeul = true;
  Feuille& f = c.feuilles.front();
  f.format = "A3";
  if (const DefSymbole* g = symboleBibliotheque("GND")) c.symboles["GND"] = *g;
  const std::vector<std::string> ls = lignes(asc);
  // LTspice centre souvent son dessin sur l'origine : la feuille commence à huit pas du coin
  double xmin = 1e300, ymin = 1e300;
  for (const auto& l : ls) {
    const auto m = mots(l);
    if (m.size() >= 3 && (m[0] == "WIRE" || m[0] == "FLAG" || m[0] == "SYMBOL" || m[0] == "TEXT")) {
      xmin = std::min(xmin, reel(m[1]));
      ymin = std::min(ymin, reel(m[2]));
      if (m[0] == "WIRE" && m.size() >= 5) xmin = std::min(xmin, reel(m[3])), ymin = std::min(ymin, reel(m[4]));
    }
  }
  if (xmin > 1e299) xmin = ymin = 0;
  const double ox = 16 * std::floor(xmin / 16) - 128, oy = 16 * std::floor(ymin / 16) - 128;
  auto P = [&](double x, double y) { return lt(x - ox, y - oy); };
  struct Pose {
    std::string symbole, rot;
    double x = 0, y = 0;
    std::string ref, valeur, valeur2;
  };
  std::vector<Pose> poses;
  for (const auto& l : ls) {
    const auto m = mots(l);
    if (m.empty()) continue;
    if (m[0] == "WIRE" && m.size() >= 5) {
      f.fils.push_back({nouvelUuid(), P(reel(m[1]), reel(m[2])), P(reel(m[3]), reel(m[4]))});
      ++r.fils;
    } else if (m[0] == "FLAG" && m.size() >= 4) {
      const Pt p = P(reel(m[1]), reel(m[2]));
      if (m[3] == "0") {
        SymbolePlace sp;  // masse
        sp.uuid = nouvelUuid();
        sp.symbole = "GND";
        sp.nomAlim = "GND";
        sp.position = p;
        f.symboles.push_back(sp);
      } else {
        Etiquette e;
        e.uuid = nouvelUuid();
        e.texte = m[3];
        e.p = p;
        e.type = m[3].rfind("$G_", 0) == 0 ? TypeEtiquette::Globale : TypeEtiquette::Locale;
        f.etiquettes.push_back(e);
        ++r.etiquettes;
      }
    } else if (m[0] == "SYMBOL" && m.size() >= 5)
      poses.push_back({m[1], m[4], reel(m[2]), reel(m[3]), {}, {}, {}});
    else if (m[0] == "SYMATTR" && m.size() >= 3 && !poses.empty()) {
      if (m[1] == "InstName") poses.back().ref = m[2];
      else if (m[1] == "Value") poses.back().valeur = apresMot(l, "Value");
      else if (m[1] == "Value2") poses.back().valeur2 = apresMot(l, "Value2");
    } else if (m[0] == "TEXT" && m.size() >= 6) {
      // Directive SPICE (« ! ») gardée pour la simulation ; commentaire (« ; ») gardé en texte
      const auto p = l.find_first_of("!;");
      if (p == std::string::npos) continue;
      std::string t = l.substr(p + 1);
      for (std::size_t k; (k = t.find("\\n")) != std::string::npos;) t.replace(k, 2, "\n");
      TexteSchema x;
      x.uuid = nouvelUuid();
      x.texte = l[p] == '!' ? "SPICE : " + t : t;
      x.p = P(reel(m[1]), reel(m[2]));
      f.textes.push_back(x);
      if (l[p] == '!') r.directives.push_back(t);
    }
  }
  std::map<std::string, std::optional<SymboleLt>> cache;
  for (const auto& p : poses) {
    auto it = cache.find(p.symbole);
    if (it == cache.end()) {
      std::optional<SymboleLt> s;
      const auto k = p.symbole.find_last_of("\\/");
      if (lire)
        if (const auto t = lire((k == std::string::npos ? p.symbole : p.symbole.substr(k + 1)) + ".asy")) s = lireAsy(*t);
      if (!s) s = symboleLtDeBase(p.symbole);
      it = cache.emplace(p.symbole, s).first;
    }
    if (!it->second) {
      r.alertes.push_back("Symbole « " + p.symbole + " » de " + p.ref + " inconnu (son .asy n'est pas fourni) : ignoré");
      continue;
    }
    const SymboleLt& s = *it->second;
    DefSymbole d;
    d.id = "ltspice:" + p.symbole;
    d.nom = p.symbole;
    d.prefixe = s.prefixe;
    d.categorie = "Importés d'LTspice";
    Boite corps;
    for (const auto& [a, b] : s.traits) corps.etendre(a), corps.etendre(b);
    for (const auto& b : s.broches) {
      Broche x;
      x.numero = std::to_string(b.ordre);
      x.nom = b.nom;
      x.extremite = lt(static_cast<double>(b.p.x), static_cast<double>(b.p.y));
      x.longueur = 0;
      x.type = TypeBroche::Passive;
      // Vers le corps du symbole
      const Pt v = corps.vide() ? Pt{} : corps.centre() - b.p;
      x.direction = std::abs(v.x) >= std::abs(v.y) ? (v.x >= 0 ? 0 : 2) : (v.y >= 0 ? 1 : 3);
      d.broches.push_back(x);
    }
    for (const auto& [a, b] : s.traits) {
      Trace t;
      t.forme = Trace::Forme::Polyligne;
      t.points = {lt(static_cast<double>(a.x), static_cast<double>(a.y)), lt(static_cast<double>(b.x), static_cast<double>(b.y))};
      d.traces.push_back(t);
    }
    c.symboles[d.id] = d;
    ComposantElec x;
    x.uuid = nouvelUuid();
    x.reference = p.ref.empty() ? s.prefixe + "?" : p.ref;
    x.valeur = p.valeur + (p.valeur2.empty() ? "" : " " + p.valeur2);
    x.symbole = d.id;
    c.composants.push_back(x);
    ++r.composants;
    SymbolePlace sp;
    sp.uuid = nouvelUuid();
    sp.symbole = d.id;
    sp.composant = x.uuid;
    sp.position = P(p.x, p.y);
    // LTspice (y vers le bas) : R90 (x, y) → (−y, x), R180 (−x, −y), R270 (y, −x) ; Mθ : Rθ puis retourné en x
    const bool miroir = !p.rot.empty() && p.rot[0] == 'M';
    const int deg = p.rot.size() > 1 ? entier(p.rot.substr(1)) : 0;
    int a = 1, b = 0, cc = 0, dd = 1;
    if (deg == 90) a = 0, b = -1, cc = 1, dd = 0;
    else if (deg == 180) a = -1, b = 0, cc = 0, dd = -1;
    else if (deg == 270) a = 0, b = 1, cc = -1, dd = 0;
    if (miroir) a = -a, b = -b;  // F · Rθ
    const Orientation o = orientationDe(a, b, cc, dd);
    sp.miroir = o.miroir;
    sp.rotation = o.quarts;
    f.symboles.push_back(sp);
  }
  if (!r.directives.empty()) {
    auto& t = c.extensions["directivesSpice"];
    t = nlohmann::ordered_json::array();
    for (const auto& d : r.directives) t.push_back(d);
  }
  synchroniserReseaux(c);
  r.feuilles = 1;
  if (rapport) *rapport = r;
  return c;
}

ConceptionElec conceptionDepuisVisio(const std::string& vsdx, const std::string& nom, RapportImportSchema* rapport) {
  using xml_interne::NoeudXml;
  RapportImportSchema r;
  ConceptionElec c = nouvelleConception(nom);
  c.feuilles.clear();
  c.schemaSeul = true;
  std::vector<std::pair<std::string, std::string>> fichiers;
  try {
    fichiers = lireZip(vsdx);
  } catch (const std::exception& e) {
    r.alertes.push_back(std::string("Archive Visio illisible : ") + e.what());
  }
  auto local = [](const std::string& n) { return n.substr(n.find(':') == std::string::npos ? 0 : n.find(':') + 1); };
  auto enfantLocal = [&](const NoeudXml& n, const char* nomLocal) -> const NoeudXml* {
    for (const auto& e : n.enfants)
      if (local(e.nom) == nomLocal) return &e;
    return nullptr;
  };
  std::function<std::string(const NoeudXml&)> texteDe = [&](const NoeudXml& t) {
    std::string s = t.texte;
    for (const auto& x : t.enfants) s += texteDe(x);
    return s;
  };
  // Pages dans l'ordre de leur numéro (page1, page2…, page10)
  std::vector<std::pair<int, const std::string*>> pages;
  for (const auto& [chemin, contenu] : fichiers) {
    const std::string debut = "visio/pages/page";
    if (chemin.rfind(debut, 0) != 0 || chemin.size() <= debut.size() || !std::isdigit(static_cast<unsigned char>(chemin[debut.size()])) || chemin.find(".xml") == std::string::npos || chemin.find("_rels") != std::string::npos) continue;
    pages.push_back({std::atoi(chemin.c_str() + debut.size()), &contenu});
  }
  std::sort(pages.begin(), pages.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [numero, contenu] : pages) {
    NoeudXml doc;
    try {
      doc = xml_interne::lireXml(*contenu);
    } catch (const std::exception& e) {
      r.alertes.push_back("Page " + std::to_string(numero) + " illisible : " + e.what());
      continue;
    }
    const NoeudXml* page = &doc;  // l'élément racine, PageContents
    struct Forme {
      std::string id, texte;
      double x = 0, y = 0, l = 1, h = 0.5;
      bool connecteur = false;
    };
    std::vector<Forme> formes;
    std::map<std::string, std::pair<std::string, std::string>> liens;  // connecteur → (forme du début, de la fin)
    if (const NoeudXml* shapes = enfantLocal(*page, "Shapes"))
      for (const auto& n : shapes->enfants) {
        if (local(n.nom) != "Shape") continue;
        Forme f;
        f.id = n.attr("ID");
        std::string sousTextes;
        for (const auto& e : n.enfants) {
          const std::string le = local(e.nom);
          if (le == "Cell") {
            const std::string cle = e.attr("N");
            const double v = std::strtod(e.attr("V").c_str(), nullptr);
            if (cle == "PinX") f.x = v;
            else if (cle == "PinY") f.y = v;
            else if (cle == "Width") f.l = v;
            else if (cle == "Height") f.h = v;
            else if (cle == "BeginX" || cle == "EndX") f.connecteur = true;
          } else if (le == "Text")
            f.texte = texteDe(e);
          else if (le == "Shapes")  // groupe : le texte de ses formes, s'il n'en a pas
            for (const auto& s : e.enfants)
              if (const NoeudXml* t = enfantLocal(s, "Text")) sousTextes += (sousTextes.empty() ? "" : " ") + texteDe(*t);
        }
        if (f.texte.empty()) f.texte = sousTextes;
        while (!f.texte.empty() && std::isspace(static_cast<unsigned char>(f.texte.back()))) f.texte.pop_back();
        while (!f.texte.empty() && std::isspace(static_cast<unsigned char>(f.texte.front()))) f.texte.erase(0, 1);
        formes.push_back(f);
      }
    if (const NoeudXml* connects = enfantLocal(*page, "Connects"))
      for (const auto& n : connects->enfants) {
        if (local(n.nom) != "Connect") continue;
        auto& x = liens[n.attr("FromSheet")];
        (n.attr("FromCell") == "EndX" ? x.second : x.first) = n.attr("ToSheet");
      }
    Feuille f;
    f.uuid = nouvelUuid();
    f.nom = "Synoptique " + std::to_string(c.feuilles.size() + 1);
    f.format = "A3";
    // Visio : pouces, y vers le haut ; la feuille : mm, y vers le bas, la page posée sous le haut d'un A3 (297 mm)
    auto P = [](double x, double y) { return ptMm(x * 25.4, 297 - y * 25.4); };
    std::map<std::string, std::size_t> blocDe;
    std::map<std::string, std::string> texteDeForme;
    for (const auto& x : formes) {
      texteDeForme[x.id] = x.texte;
      if (x.connecteur || liens.count(x.id)) continue;
      BlocFeuille b;
      b.uuid = nouvelUuid();
      b.nom = x.texte.empty() ? "Bloc " + x.id : x.texte;
      b.position = P(x.x - x.l / 2, x.y + x.h / 2);
      b.largeur = nm(x.l * 25.4);
      b.hauteur = nm(x.h * 25.4);
      blocDe[x.id] = f.blocs.size();
      f.blocs.push_back(b);
      ++r.blocs;
    }
    // Connecteur : une liaison d'un port à l'autre, chacun sur le côté du bloc qui regarde l'autre
    int n = 0;
    for (const auto& [id, de] : liens) {
      const auto a = blocDe.find(de.first), b = blocDe.find(de.second);
      if (a == blocDe.end() || b == blocDe.end() || a->second == b->second) {
        r.alertes.push_back("Connecteur " + id + " relié à moins de deux formes : ignoré");
        continue;
      }
      const std::string nomLien = texteDeForme[id].empty() ? "L" + std::to_string(++n) : texteDeForme[id];
      BlocFeuille& A = f.blocs[a->second];
      BlocFeuille& B = f.blocs[b->second];
      const Pt ca = A.position + Pt{A.largeur / 2, A.hauteur / 2}, cb = B.position + Pt{B.largeur / 2, B.hauteur / 2};
      const bool horizontal = std::abs(cb.x - ca.x) >= std::abs(cb.y - ca.y);
      auto port = [&](BlocFeuille& x, bool versPlus, Pt centre) {
        PortBloc p;
        p.uuid = nouvelUuid();
        p.nom = nomLien;
        if (horizontal) {
          p.cote = versPlus ? 1 : 0;
          p.p = {versPlus ? x.position.x + x.largeur : x.position.x, centre.y};
        } else {
          p.cote = versPlus ? 3 : 2;
          p.p = {centre.x, versPlus ? x.position.y + x.hauteur : x.position.y};
        }
        x.ports.push_back(p);
        return p.p;
      };
      const Pt pa = port(A, horizontal ? cb.x > ca.x : cb.y > ca.y, ca);
      const Pt pb = port(B, horizontal ? ca.x > cb.x : ca.y > cb.y, cb);
      // Liaison en équerre, coudée à mi-chemin
      const Pt c1 = horizontal ? Pt{(pa.x + pb.x) / 2, pa.y} : Pt{pa.x, (pa.y + pb.y) / 2};
      const Pt c2 = horizontal ? Pt{(pa.x + pb.x) / 2, pb.y} : Pt{pb.x, (pa.y + pb.y) / 2};
      for (const auto& [u, v] : std::vector<std::pair<Pt, Pt>>{{pa, c1}, {c1, c2}, {c2, pb}})
        if (u != v) f.fils.push_back({nouvelUuid(), u, v});
      ++r.liaisons;
    }
    c.feuilles.push_back(std::move(f));
    ++r.feuilles;
  }
  if (c.feuilles.empty()) {
    c.feuilles.push_back(Feuille{});
    c.feuilles.back().uuid = nouvelUuid();
    r.alertes.push_back("Aucune page lisible dans l'archive Visio");
  }
  if (rapport) *rapport = r;
  return c;
}

}  // namespace prom::ecad
