// Prométhée : schéma avancé de la 0.34 (lot L7). Voir schema_avance.hpp.
// Licence GPL-3.0-only.
#include "promethee/schema_avance.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cmath>
#include <functional>
#include <numbers>
#include <sstream>

#include "promethee/formules.hpp"
#include "promethee/gestion_bibliotheque.hpp"
#include "promethee/structure_schema.hpp"

namespace prom::ecad {

namespace {
std::optional<std::size_t> indiceFeuille(const ConceptionElec& c, const std::string& uuid) {
  for (std::size_t i = 0; i < c.feuilles.size(); ++i)
    if (c.feuilles[i].uuid == uuid) return i;
  return std::nullopt;
}
const BlocFeuille* trouverBloc(const Feuille& f, const std::string& uuid) {
  for (const auto& b : f.blocs)
    if (b.uuid == uuid) return &b;
  return nullptr;
}
}  // namespace

// ---------- Hiérarchie ----------
std::optional<std::pair<std::size_t, std::string>> blocParent(const ConceptionElec& c, std::size_t feuille) {
  if (feuille >= c.feuilles.size()) return std::nullopt;
  for (std::size_t i = 0; i < c.feuilles.size(); ++i)
    for (const auto& b : c.feuilles[i].blocs)
      if (b.feuille == c.feuilles[feuille].uuid && i != feuille) return std::pair{i, b.uuid};
  return std::nullopt;
}

std::vector<std::size_t> filAriane(const ConceptionElec& c, std::size_t feuille) {
  std::vector<std::size_t> r;
  std::set<std::size_t> vues;
  for (std::optional<std::size_t> f = feuille; f && vues.insert(*f).second;) {
    r.push_back(*f);
    const auto p = blocParent(c, *f);
    f = p ? std::optional<std::size_t>(p->first) : std::nullopt;
  }
  std::reverse(r.begin(), r.end());
  return r;
}

std::string cheminInstance(const ConceptionElec& c, std::size_t feuille) {
  std::string r;
  const auto chemin = filAriane(c, feuille);
  for (std::size_t i = 0; i < chemin.size(); ++i) {
    std::string nom = c.feuilles[chemin[i]].nom;
    if (i > 0)
      if (const auto p = blocParent(c, chemin[i]))
        if (const BlocFeuille* b = trouverBloc(c.feuilles[p->first], p->second); b && !b->nom.empty()) nom = b->nom;
    r += "/" + nom;
  }
  return r;
}

EcartPorts ecartPorts(const ConceptionElec& c, std::size_t feuille, const std::string& bloc) {
  EcartPorts r;
  if (feuille >= c.feuilles.size()) return r;
  const BlocFeuille* b = trouverBloc(c.feuilles[feuille], bloc);
  if (!b) return r;
  const auto fille = indiceFeuille(c, b->feuille);
  if (!fille) return r;
  std::set<std::string> etiquettes, ports, couverts, passes;
  for (const auto& e : c.feuilles[*fille].etiquettes)
    if (e.type == TypeEtiquette::Hierarchique) etiquettes.insert(e.texte);
  // Un port de bus ou de faisceau couvre ses membres (7.13) ; la fille qui le passe à ses propres blocs l'emploie
  for (const auto& p : b->ports) {
    ports.insert(p.nom);
    for (const auto& m : membresFaisceau(p.nom, c.aliasBus)) couverts.insert(m);
  }
  for (const auto& bb : c.feuilles[*fille].blocs)
    for (const auto& p : bb.ports) passes.insert(p.nom);
  for (const auto& e : etiquettes)
    if (!ports.count(e) && !couverts.count(e)) r.manquants.push_back(e);
  for (const auto& p : ports) {
    if (etiquettes.count(p)) continue;
    const auto m = membresFaisceau(p, c.aliasBus);
    if (!m.empty() && (passes.count(p) || std::any_of(m.begin(), m.end(), [&](const std::string& x) { return etiquettes.count(x) > 0; }))) continue;
    r.orphelins.push_back(p);
  }
  return r;
}

int synchroniserPorts(ConceptionElec& c, std::size_t feuille, const std::string& bloc, bool retirerOrphelins) {
  exigerModifiable(c);
  const EcartPorts e = ecartPorts(c, feuille, bloc);
  if (feuille >= c.feuilles.size()) return 0;
  BlocFeuille* b = nullptr;
  for (auto& x : c.feuilles[feuille].blocs)
    if (x.uuid == bloc) b = &x;
  if (!b) return 0;
  int n = 0;
  // Nouveaux ports sur le bord gauche, sous les ports existants, au pas de la grille ; le bloc grandit au besoin
  Nm y = b->position.y + 2 * GRILLE_SCHEMA;
  for (const auto& p : b->ports)
    if (p.cote == 0) y = std::max(y, p.p.y + 2 * GRILLE_SCHEMA);
  const auto fille = indiceFeuille(c, b->feuille);
  for (const auto& nom : e.manquants) {
    PortBloc p;
    p.uuid = nouvelUuid();
    p.nom = nom;
    p.cote = 0;
    p.p = {b->position.x, y};
    if (fille)
      for (const auto& et : c.feuilles[*fille].etiquettes)
        if (et.type == TypeEtiquette::Hierarchique && et.texte == nom) p.sens = et.sens;
    b->ports.push_back(p);
    y += 2 * GRILLE_SCHEMA;
    ++n;
  }
  if (y > b->position.y + b->hauteur) b->hauteur = y - b->position.y + GRILLE_SCHEMA;
  if (retirerOrphelins)
    for (const auto& nom : e.orphelins) n += static_cast<int>(std::erase_if(b->ports, [&](const PortBloc& p) { return p.nom == nom; }));
  return n;
}

std::size_t hierarchiserFeuilles(ConceptionElec& c, const std::string& nomRacine) {
  exigerModifiable(c);
  Feuille racine;
  racine.uuid = nouvelUuid();
  racine.nom = nomRacine;
  racine.format = c.feuilles.empty() ? std::string("A4") : c.feuilles.front().format;
  // Un bloc par feuille, en grille ; les étiquettes globales gardent leurs liens : les réseaux ne changent pas
  int i = 0;
  for (const auto& f : c.feuilles) {
    BlocFeuille b;
    b.uuid = nouvelUuid();
    b.nom = f.nom;
    b.feuille = f.uuid;
    b.position = {nm(25.4) + (i % 3) * nm(76.2), nm(25.4) + (i / 3) * nm(50.8)};
    racine.blocs.push_back(b);
    ++i;
  }
  c.feuilles.insert(c.feuilles.begin(), racine);
  return 0;
}

std::vector<Violation> verifierPortsHierarchiques(const ConceptionElec& c) {
  std::vector<Violation> v;
  for (std::size_t i = 0; i < c.feuilles.size(); ++i)
    for (const auto& b : c.feuilles[i].blocs) {
      const EcartPorts e = ecartPorts(c, i, b.uuid);
      if (e.vide()) continue;
      Violation x;
      x.gravite = Gravite::Avertissement;
      x.code = "ERC-33";
      std::string t;
      for (const auto& m : e.manquants) t += (t.empty() ? "" : ", ") + m;
      std::string o;
      for (const auto& m : e.orphelins) o += (o.empty() ? "" : ", ") + m;
      x.message = "Bloc « " + (b.nom.empty() ? std::string("sans nom") : b.nom) + " »" + (t.empty() ? std::string() : " : port(s) à ajouter pour " + t) +
                  (o.empty() ? std::string() : " ; port(s) sans étiquette dans la fille : " + o);
      x.conseil = "« Synchroniser les ports » du bloc les ajoute ou les retire.";
      x.feuille = c.feuilles[i].uuid;
      x.position = b.position;
      x.objets = {b.uuid};
      v.push_back(x);
    }
  return v;
}

// ---------- Textes riches ----------
namespace {
std::optional<std::string> variable(const std::string& nom, const ConceptionElec& c, int feuille, bool& herite) {
  herite = false;
  if (feuille >= 0 && static_cast<std::size_t>(feuille) < c.feuilles.size()) {
    const Feuille& f = c.feuilles[static_cast<std::size_t>(feuille)];
    if (const auto it = f.variables.find(nom); it != f.variables.end()) return it->second;
    if (nom == "FEUILLE" || nom == "SHEETNAME") return f.nom;
    if (nom == "PAGE" || nom == "#") return numeroPage(c, static_cast<std::size_t>(feuille));
    if (nom == "PAGES" || nom == "##") return std::to_string(c.feuilles.size());
    if (nom == "CHEMIN" || nom == "SHEETPATH") return cheminInstance(c, static_cast<std::size_t>(feuille));
  }
  herite = true;
  if (const auto it = c.variables.find(nom); it != c.variables.end()) return it->second;
  const Cartouche& k = c.cartouche;
  if (nom == "REVISION") return k.revision;
  if (nom == "TITRE" || nom == "TITLE") return k.titre;
  if (nom == "DATE" || nom == "ISSUE_DATE") return k.date;
  if (nom == "AUTEUR") return k.auteur;
  if (nom == "SOCIETE" || nom == "COMPANY") return k.societe;
  if (nom == "COMMENTAIRE") return k.commentaire;
  if (nom == "PROJET" || nom == "PROJECTNAME") return c.nom;
  if (const auto it = k.champs.find(nom); it != k.champs.end()) return it->second;
  return std::nullopt;
}
// Fin du groupe ouvert à « debut » (« { » compté) ; npos s'il ne se ferme pas
std::size_t fermante(const std::string& s, std::size_t debut) {
  int n = 0;
  for (std::size_t i = debut; i < s.size(); ++i) {
    if (s[i] == '{') ++n;
    else if (s[i] == '}' && --n == 0) return i;
  }
  return std::string::npos;
}
}  // namespace

TexteResolu resoudreTexte(const std::string& texte, const ConceptionElec& c, int feuille) {
  TexteResolu r;
  // 1. Variables ${NOM}, jusqu'à quatre niveaux (une variable qui en cite une autre)
  std::string s = texte;
  bool herite = false;
  for (int passe = 0; passe < 4 && s.find("${") != std::string::npos; ++passe) {
    std::string t;
    for (std::size_t i = 0; i < s.size(); ++i) {
      if (s.compare(i, 2, "${") != 0) {
        t += s[i];
        continue;
      }
      const std::size_t fin = fermante(s, i + 1);
      if (fin == std::string::npos) {
        t += s.substr(i);
        break;
      }
      const std::string nom = s.substr(i + 2, fin - i - 2);
      // Textes marqués : ${ERC_WARNING message}, ${ERC_ERROR message} (et DRC_…)
      for (const auto& [prefixe, niveau] : {std::pair<const char*, int>{"ERC_WARNING", 1}, {"DRC_WARNING", 1}, {"ERC_ERROR", 2}, {"DRC_ERROR", 2}})
        if (nom.rfind(prefixe, 0) == 0) {
          r.avertissement = std::max(r.avertissement, niveau);
          r.message = nom.size() > std::strlen(prefixe) ? nom.substr(std::strlen(prefixe) + 1) : std::string();
        }
      if (r.avertissement && (nom.rfind("ERC_", 0) == 0 || nom.rfind("DRC_", 0) == 0)) {
        i = fin;
        continue;
      }
      bool h = false;
      if (const auto v = variable(nom, c, feuille, h)) {
        t += *v;
        herite |= h;
      } else {
        if (std::find(r.inconnues.begin(), r.inconnues.end(), nom) == r.inconnues.end()) r.inconnues.push_back(nom);
        t += "${" + nom + "}";
      }
      i = fin;
    }
    s = t;
    if (!r.inconnues.empty()) break;
  }
  // 2. Expressions @{…}, calculées avec les grandeurs (unités suivies) ; 3. balises de style
  std::string courant;
  MorceauTexte::Style style = MorceauTexte::Style::Normal;
  auto pousser = [&](const std::string& x, MorceauTexte::Style st, const std::string& cible = {}) {
    if (x.empty()) return;
    r.morceaux.push_back({st, x, cible, herite});
    r.resolu += x;
  };
  for (std::size_t i = 0; i < s.size(); ++i) {
    const char ch = s[i];
    if (ch == '@' && i + 1 < s.size() && s[i + 1] == '{') {
      const std::size_t fin = fermante(s, i + 1);
      if (fin != std::string::npos) {
        const std::string expr = s.substr(i + 2, fin - i - 2);
        const ResultatFormule f = evaluerFormule(expr);
        pousser(courant, style);
        courant.clear();
        if (f.ok) pousser(ecrireQuantite(f.q, 6), style);
        else {
          r.erreurs.push_back("« " + expr + " » : " + f.erreur);
          pousser("@{" + expr + "}", style);
        }
        i = fin;
        continue;
      }
    }
    if ((ch == '^' || ch == '_' || ch == '~') && i + 1 < s.size() && s[i + 1] == '{') {
      const std::size_t fin = fermante(s, i + 1);
      if (fin != std::string::npos) {
        pousser(courant, style);
        courant.clear();
        pousser(s.substr(i + 2, fin - i - 2), ch == '^' ? MorceauTexte::Style::Exposant : ch == '_' ? MorceauTexte::Style::Indice : MorceauTexte::Style::Barre);
        i = fin;
        continue;
      }
    }
    if (ch == '[' && i + 1 < s.size() && s[i + 1] == '[') {
      const std::size_t fin = s.find("]]", i + 2);
      if (fin != std::string::npos) {
        const std::string corps = s.substr(i + 2, fin - i - 2);
        const auto barre = corps.find('|');
        pousser(courant, style);
        courant.clear();
        pousser(barre == std::string::npos ? corps : corps.substr(barre + 1), MorceauTexte::Style::Lien, barre == std::string::npos ? corps : corps.substr(0, barre));
        i = fin + 1;
        continue;
      }
    }
    courant += ch;
  }
  pousser(courant, style);
  return r;
}

std::string texteAffiche(const std::string& texte, const ConceptionElec& c, int feuille) {
  if (texte.find_first_of("$@^_~[") == std::string::npos) return texte;
  return resoudreTexte(texte, c, feuille).resolu;
}

std::vector<Violation> verifierTextesRiches(const ConceptionElec& c, bool circuit) {
  std::vector<Violation> v;
  auto controler = [&](const std::string& texte, int feuille, Pt p, const std::string& objet) {
    if (texte.find_first_of("$@") == std::string::npos) return;
    const TexteResolu r = resoudreTexte(texte, c, feuille);
    const std::string pre = circuit ? "DRC-" : "ERC-";
    if (r.avertissement) {
      Violation x;
      x.gravite = r.avertissement == 2 ? Gravite::Erreur : Gravite::Avertissement;
      x.code = pre + (circuit ? "83" : "34");
      x.message = r.message.empty() ? std::string("Texte marqué « à revoir »") : r.message;
      x.conseil = "Texte marqué dans le dessin : règle ce qu'il signale, puis retire la marque.";
      x.feuille = feuille >= 0 ? c.feuilles[static_cast<std::size_t>(feuille)].uuid : std::string();
      x.position = p;
      x.objets = {objet};
      v.push_back(x);
    }
    for (const auto& n : r.inconnues) {
      Violation x;
      x.gravite = Gravite::Avertissement;
      x.code = pre + (circuit ? "84" : "35");
      x.message = "Variable « ${" + n + "} » sans valeur";
      x.conseil = "Donne-lui une valeur dans les variables de la feuille ou de la conception, ou corrige son nom.";
      x.feuille = feuille >= 0 ? c.feuilles[static_cast<std::size_t>(feuille)].uuid : std::string();
      x.position = p;
      x.objets = {objet};
      v.push_back(x);
    }
    for (const auto& e : r.erreurs) {
      Violation x;
      x.gravite = Gravite::Avertissement;
      x.code = pre + (circuit ? "84" : "35");
      x.message = "Expression fausse : " + e;
      x.conseil = "Corrige l'expression entre @{ et }.";
      x.position = p;
      x.objets = {objet};
      v.push_back(x);
    }
  };
  if (circuit) {
    for (const auto& t : c.circuit.textes) controler(t.texte, -1, t.p, t.uuid);
  } else {
    for (std::size_t i = 0; i < c.feuilles.size(); ++i) {
      const Feuille& f = c.feuilles[i];
      for (const auto& t : f.textes) controler(t.texte, static_cast<int>(i), t.p, t.uuid);
      for (const auto& e : f.etiquettes) controler(e.texte, static_cast<int>(i), e.p, e.uuid);
    }
    for (const auto& x : c.composants)
      for (const auto& [k, val] : x.champs) controler(val, -1, {}, x.uuid);
  }
  return v;
}

// ---------- Cadre, pages et renvois ----------
std::string ZoneCadre::texte() const { return std::string(1, static_cast<char>('A' + std::clamp(ligne, 0, 25))) + std::to_string(colonne); }

std::string numeroPage(const ConceptionElec& c, std::size_t feuille) {
  if (feuille >= c.feuilles.size()) return {};
  if (!c.feuilles[feuille].page.empty()) return c.feuilles[feuille].page;
  return std::to_string(feuille + 1);
}

std::string textePage(const ConceptionElec& c, std::size_t feuille) {
  std::string f = c.reglagesSchema.formatPage.empty() ? std::string("{n}/{N}") : c.reglagesSchema.formatPage;
  auto remplacer = [&](const std::string& de, const std::string& a) {
    for (std::size_t i = f.find(de); i != std::string::npos; i = f.find(de, i + a.size())) f.replace(i, de.size(), a);
  };
  remplacer("{n}", numeroPage(c, feuille));
  remplacer("{N}", std::to_string(c.feuilles.size()));
  return f;
}

namespace {
Boite cadreDe(const Feuille& f) {
  if (f.format == "libre" && f.largeurLibre > 0 && f.hauteurLibre > 0) return Boite{0, 0, f.largeurLibre, f.hauteurLibre};
  return cadreFeuille(f.format);
}
}  // namespace

// Bordure en zones : environ 50 mm par colonne et par ligne (CEI 61082), au moins 4 × 3
int colonnesCadre(const Feuille& f) { return std::max(4, static_cast<int>(std::lround(mm(cadreDe(f).largeur()) / 50.0))); }
int lignesCadre(const Feuille& f) { return std::max(3, static_cast<int>(std::lround(mm(cadreDe(f).hauteur()) / 50.0))); }

ZoneCadre zoneDuPoint(const Feuille& f, Pt p) {
  const Boite b = cadreDe(f);
  ZoneCadre z;
  const int nc = colonnesCadre(f), nl = lignesCadre(f);
  const double u = (static_cast<double>(p.x - b.x0)) / std::max<double>(1, static_cast<double>(b.largeur()));
  const double v = (static_cast<double>(p.y - b.y0)) / std::max<double>(1, static_cast<double>(b.hauteur()));
  z.colonne = std::clamp(static_cast<int>(std::floor(u * nc)) + 1, 1, nc);
  z.ligne = std::clamp(static_cast<int>(std::floor(v * nl)), 0, nl - 1);
  return z;
}

std::string texteRenvoi(const ConceptionElec& c, std::size_t feuille, Pt p) {
  if (feuille >= c.feuilles.size()) return {};
  std::string f = c.reglagesSchema.formatRenvoi.empty() ? std::string("{page}.{zone}") : c.reglagesSchema.formatRenvoi;
  const ZoneCadre z = zoneDuPoint(c.feuilles[feuille], p);
  auto remplacer = [&](const std::string& de, const std::string& a) {
    for (std::size_t i = f.find(de); i != std::string::npos; i = f.find(de, i + a.size())) f.replace(i, de.size(), a);
  };
  remplacer("{page}", numeroPage(c, feuille));
  remplacer("{zone}", z.texte());
  remplacer("{colonne}", std::to_string(z.colonne));
  remplacer("{ligne}", std::string(1, static_cast<char>('A' + z.ligne)));
  return f;
}

std::map<std::string, std::string> renvoisComposants(const ConceptionElec& c) {
  struct Pose {
    std::string symbole;
    std::size_t feuille = 0;
    Pt p;
    int unite = 1;
  };
  std::map<std::string, std::vector<Pose>> parComposant;
  for (std::size_t i = 0; i < c.feuilles.size(); ++i)
    for (const auto& s : c.feuilles[i].symboles)
      if (!s.composant.empty()) parComposant[s.composant].push_back({s.uuid, i, s.position, s.unite});
  std::map<std::string, std::string> r;
  for (auto& [comp, poses] : parComposant) {
    if (poses.size() < 2) continue;
    // La première unité (la bobine) d'abord ; les autres dans l'ordre des pages puis des zones
    std::stable_sort(poses.begin(), poses.end(), [&](const Pose& a, const Pose& b) {
      if (a.unite != b.unite) return a.unite < b.unite;
      if (a.feuille != b.feuille) return a.feuille < b.feuille;
      return a.p.x < b.p.x;
    });
    const Pose& tete = poses.front();
    std::vector<Pose> autres(poses.begin() + 1, poses.end());
    std::stable_sort(autres.begin(), autres.end(), [&](const Pose& a, const Pose& b) {
      if (a.feuille != b.feuille) return a.feuille < b.feuille;
      return zoneDuPoint(c.feuilles[a.feuille], a.p).colonne * 100 + zoneDuPoint(c.feuilles[a.feuille], a.p).ligne <
             zoneDuPoint(c.feuilles[b.feuille], b.p).colonne * 100 + zoneDuPoint(c.feuilles[b.feuille], b.p).ligne;
    });
    std::string liste;
    for (const auto& a : autres) liste += (liste.empty() ? "" : ", ") + texteRenvoi(c, a.feuille, a.p);
    r[tete.symbole] = liste;
    for (const auto& a : autres) r[a.symbole] = texteRenvoi(c, tete.feuille, tete.p);
  }
  return r;
}

int appliquerModeleFeuille(ConceptionElec& c, std::size_t feuille, const Feuille& modele) {
  exigerModifiable(c);
  if (feuille >= c.feuilles.size()) return 0;
  Feuille& f = c.feuilles[feuille];
  f.format = modele.format;
  f.largeurLibre = modele.largeurLibre;
  f.hauteurLibre = modele.hauteurLibre;
  f.modeleFeuille = modele.nom;
  // Variables fusionnées : celles de la feuille l'emportent
  for (const auto& [k, v] : modele.variables) f.variables.emplace(k, v);
  int n = 0;
  // Éléments « répétés » du modèle (textes et formes de la mise en page) : recopiés, sans doublon
  for (const auto& t : modele.textes)
    if (std::none_of(f.textes.begin(), f.textes.end(), [&](const TexteSchema& x) { return x.texte == t.texte && x.p == t.p; })) {
      TexteSchema copie = t;
      copie.uuid = nouvelUuid();
      f.textes.push_back(copie);
      ++n;
    }
  for (const auto& g : modele.formes)
    if (std::none_of(f.formes.begin(), f.formes.end(), [&](const FormeSchema& x) { return x.trace == g.trace; })) {
      FormeSchema copie = g;
      copie.uuid = nouvelUuid();
      f.formes.push_back(copie);
      ++n;
    }
  return n;
}

// ---------- Échange de portes et de broches ----------
namespace {
// Numéros réels des broches d'une unité posée, dans l'ordre de la définition
std::vector<std::string> numerosUnite(const ConceptionElec& c, const SymbolePlace& s) {
  std::vector<std::string> r;
  const DefSymbole* d = c.symbole(s.symbole);
  if (!d) return r;
  for (const auto& b : brochesPlacees(s, *d))
    if (b.broche->unite == s.unite) r.push_back(b.numero);
  return r;
}
// Pistes et vias dont un bout touche une pastille du composant parmi « numeros » : retirées
int retirerCuivreDesPastilles(ConceptionElec& c, const std::string& composant, const std::set<std::string>& numeros) {
  const Connectivite k = connectivite(c);
  std::vector<Polygone> formes;
  for (const auto& p : pastilles(c, k))
    if (p.composant == composant && numeros.count(p.numero)) formes.push_back(p.forme);
  auto touche = [&](Pt q) {
    return std::any_of(formes.begin(), formes.end(), [&](const Polygone& f) { return dedans(f, q); });
  };
  const std::size_t avant = c.circuit.pistes.size();
  std::erase_if(c.circuit.pistes, [&](const Piste& t) { return touche(t.a) || touche(t.b); });
  return static_cast<int>(avant - c.circuit.pistes.size());
}
}  // namespace

BilanEchange echangerPortes(ConceptionElec& c, const std::string& composant, int uniteA, int uniteB) {
  exigerModifiable(c);
  BilanEchange r;
  SymbolePlace* a = nullptr;
  SymbolePlace* b = nullptr;
  for (auto& f : c.feuilles)
    for (auto& s : f.symboles)
      if (s.composant == composant) {
        if (s.unite == uniteA) a = &s;
        if (s.unite == uniteB) b = &s;
      }
  if (!a || !b || uniteA == uniteB) {
    r.raison = "les deux unités doivent être posées";
    return r;
  }
  const DefSymbole* d = c.symbole(a->symbole);
  if (!d || a->symbole != b->symbole) {
    r.raison = "unités de symboles différents";
    return r;
  }
  // Portes équivalentes : même nombre de broches, mêmes noms dans le même ordre
  std::vector<std::string> na, nb;
  for (const auto& br : d->broches) {
    if (br.unite == uniteA) na.push_back(br.nom);
    if (br.unite == uniteB) nb.push_back(br.nom);
  }
  if (na != nb) {
    r.raison = "les deux portes ne sont pas équivalentes";
    return r;
  }
  std::set<std::string> touches;
  for (const auto& n : numerosUnite(c, *a)) touches.insert(n);
  for (const auto& n : numerosUnite(c, *b)) touches.insert(n);
  std::swap(a->unite, b->unite);
  std::swap(a->numeros, b->numeros);
  // Le circuit suit le schéma : les réseaux des pastilles changent ; le cuivre qui y menait n'est plus juste
  r.pistesRetirees = retirerCuivreDesPastilles(c, composant, touches);
  r.fait = true;
  return r;
}

BilanEchange echangerBroches(ConceptionElec& c, const std::string& symbolePlace, const std::string& numeroA, const std::string& numeroB) {
  exigerModifiable(c);
  BilanEchange r;
  for (auto& f : c.feuilles)
    for (auto& s : f.symboles) {
      if (s.uuid != symbolePlace) continue;
      const DefSymbole* d = c.symbole(s.symbole);
      if (!d) break;
      // Numéro de la définition de chaque broche réelle
      std::string defA, defB;
      for (const auto& bp : brochesPlacees(s, *d)) {
        if (bp.numero == numeroA) defA = bp.broche->numero;
        if (bp.numero == numeroB) defB = bp.broche->numero;
      }
      if (defA.empty() || defB.empty()) {
        r.raison = "broche inconnue";
        return r;
      }
      s.numeros[defA] = numeroB;
      s.numeros[defB] = numeroA;
      r.pistesRetirees = retirerCuivreDesPastilles(c, s.composant, {numeroA, numeroB});
      r.fait = true;
      return r;
    }
  r.raison = "symbole introuvable";
  return r;
}

// ---------- Conception compilée ----------
ConceptionCompilee compiler(const ConceptionElec& c, const Connectivite& k) {
  ConceptionCompilee r;
  auto regler = [&](int indice, const std::map<std::string, std::string>& props, const std::string& source) {
    if (indice < 0 || static_cast<std::size_t>(indice) >= k.reseaux.size()) return;
    const std::string& nom = k.reseaux[static_cast<std::size_t>(indice)].nom;
    if (nom.empty()) return;
    ReseauCompile& x = r.reseaux[nom];
    x.nom = nom;
    for (const auto& [cle, valeur] : props) {
      if (cle == "classe") x.classe = valeur;
      else x.regles[cle] = valeur;
    }
    x.sources.push_back(source);
  };
  for (std::size_t i = 0; i < c.feuilles.size(); ++i) {
    const Feuille& f = c.feuilles[i];
    for (const auto& e : f.etiquettes) {
      if (e.type != TypeEtiquette::Directive) continue;
      std::map<std::string, std::string> props = e.proprietes;
      // « USB 90 Ω » sans propriété : le texte est la classe
      if (!props.count("classe")) props["classe"] = e.texte;
      if (const auto it = k.parEtiquette.find(e.uuid); it != k.parEtiquette.end()) regler(it->second, props, "directive « " + e.texte + " »");
    }
    for (const auto& z : f.zonesRegles) {
      if (z.contour.size() < 3) continue;
      // Ce qu'elle entoure : symboles (leurs composants, masqués s'il le faut), fils (leurs réseaux)
      std::set<int> reseaux;
      for (const auto& s : f.symboles) {
        if (!dedans(z.contour, s.position)) continue;
        if (z.masque && !s.composant.empty()) r.composantsMasques.insert(s.composant);
        if (const DefSymbole* d = c.symbole(s.symbole))
          for (const auto& bp : brochesPlacees(s, *d))
            if (const auto it = k.parBroche.find({s.composant, bp.numero}); it != k.parBroche.end()) reseaux.insert(it->second);
      }
      for (const auto& w : f.fils)
        if (dedans(z.contour, w.a) && dedans(z.contour, w.b))
          if (const auto it = k.parFil.find(w.uuid); it != k.parFil.end()) reseaux.insert(it->second);
      for (int n : reseaux) {
        regler(n, z.proprietes, "zone « " + z.nom + " »");
        if (z.masque) r.reseaux[k.reseaux[static_cast<std::size_t>(n)].nom].masque = true;
      }
    }
  }
  return r;
}

int appliquerDirectives(ConceptionElec& c) {
  exigerModifiable(c);
  const Connectivite k = connectivite(c);
  const ConceptionCompilee cc = compiler(c, k);
  int n = 0;
  Regles& R = c.circuit.regles;
  for (const auto& [nom, x] : cc.reseaux) {
    if (x.classe.empty()) continue;
    auto cl = std::find_if(R.classes.begin(), R.classes.end(), [&](const ClasseReseau& y) { return y.nom == x.classe; });
    if (cl == R.classes.end()) {
      ClasseReseau nouvelle;
      nouvelle.nom = x.classe;
      nouvelle.isolement = R.isolement;
      nouvelle.largeur = R.largeur;
      nouvelle.viaDiametre = R.viaDiametre;
      nouvelle.viaPercage = R.viaPercage;
      R.classes.push_back(nouvelle);
      cl = R.classes.end() - 1;
    }
    // Règles écrites : largeur et isolement pris dans la classe
    for (const auto& [cle, valeur] : x.regles) {
      std::string v = valeur;
      std::replace(v.begin(), v.end(), ',', '.');
      const double mmv = std::atof(v.c_str());
      if (mmv <= 0) continue;
      if (cle == "isolement") cl->isolement = std::max(cl->isolement, nm(mmv));
      if (cle == "largeur") cl->largeur = std::max(cl->largeur, nm(mmv));
    }
    // Affectation exacte, placée avant les motifs
    if (const auto it = R.classeReseau.find(nom); it == R.classeReseau.end() || it->second != x.classe) {
      R.classeReseau[nom] = x.classe;
      ++n;
    }
  }
  return n;
}

GrapheReseaux grapheReseaux(const ConceptionElec& c, const Connectivite& k, bool sansAlimentations) {
  GrapheReseaux g;
  std::map<std::string, int> composants;
  for (std::size_t i = 0; i < k.reseaux.size(); ++i) {
    const Reseau& r = k.reseaux[i];
    if (r.broches.size() < 2 || (sansAlimentations && !r.alimentations.empty())) continue;
    const int nr = static_cast<int>(g.noeuds.size());
    g.noeuds.push_back({"reseau:" + r.nom, r.nom, true});
    std::set<std::string> vus;
    for (const auto& b : r.broches) {
      if (!vus.insert(b.composant).second) continue;
      auto [it, neuf] = composants.emplace(b.composant, static_cast<int>(g.noeuds.size()));
      if (neuf) {
        const ComposantElec* x = c.composant(b.composant);
        g.noeuds.push_back({b.composant, x ? x->reference : b.composant, false});
      }
      g.aretes.push_back({nr, it->second});
    }
  }
  // Place de départ : un cercle, déterministe
  const double n = std::max<double>(1, static_cast<double>(g.noeuds.size()));
  for (std::size_t i = 0; i < g.noeuds.size(); ++i) {
    const double a = 2 * std::numbers::pi * static_cast<double>(i) / n;
    g.noeuds[i].x = 100 * std::cos(a);
    g.noeuds[i].y = 100 * std::sin(a);
  }
  return g;
}

void rangerParRessorts(GrapheReseaux& g, int iterations, const std::atomic<bool>* arret) {
  const std::size_t n = g.noeuds.size();
  if (n < 2) return;
  const double aire = 400.0 * 400.0, kk = std::sqrt(aire / static_cast<double>(n));
  double temperature = 40;
  std::vector<double> dx(n), dy(n);
  for (int it = 0; it < iterations; ++it) {
    if (arret && arret->load()) return;
    std::fill(dx.begin(), dx.end(), 0.0);
    std::fill(dy.begin(), dy.end(), 0.0);
    // Répulsion entre tous les nœuds, attraction le long des arêtes (Fruchterman-Reingold)
    for (std::size_t i = 0; i < n; ++i)
      for (std::size_t j = i + 1; j < n; ++j) {
        double ex = g.noeuds[i].x - g.noeuds[j].x, ey = g.noeuds[i].y - g.noeuds[j].y;
        double d = std::hypot(ex, ey);
        if (d < 1e-6) {
          ex = 0.01 * static_cast<double>(i + 1), ey = 0.01 * static_cast<double>(j + 1), d = std::hypot(ex, ey);
        }
        const double f = kk * kk / d;
        dx[i] += ex / d * f, dy[i] += ey / d * f;
        dx[j] -= ex / d * f, dy[j] -= ey / d * f;
      }
    for (const auto& [a, b] : g.aretes) {
      const std::size_t i = static_cast<std::size_t>(a), j = static_cast<std::size_t>(b);
      const double ex = g.noeuds[i].x - g.noeuds[j].x, ey = g.noeuds[i].y - g.noeuds[j].y, d = std::max(1e-6, std::hypot(ex, ey));
      const double f = d * d / kk;
      dx[i] -= ex / d * f, dy[i] -= ey / d * f;
      dx[j] += ex / d * f, dy[j] += ey / d * f;
    }
    for (std::size_t i = 0; i < n; ++i) {
      const double d = std::max(1e-6, std::hypot(dx[i], dy[i]));
      g.noeuds[i].x += dx[i] / d * std::min(d, temperature);
      g.noeuds[i].y += dy[i] / d * std::min(d, temperature);
    }
    temperature = std::max(0.5, temperature * 0.97);
  }
}

// ---------- Symboles ----------
std::vector<std::string> controlerSymbole(const DefSymbole& d) {
  std::vector<std::string> r;
  if (d.prefixe.empty() && !d.alimentation && !d.drapeau) r.push_back("CS-06 : préfixe de repère absent");
  std::map<std::pair<std::string, int>, std::string> numeros;
  std::map<std::tuple<Nm, Nm, int>, std::string> places;
  std::set<int> unitesAvecBroches;
  for (const auto& b : d.broches) {
    if (b.extremite.x % GRILLE_SCHEMA != 0 || b.extremite.y % GRILLE_SCHEMA != 0) r.push_back("CS-01 : broche " + b.numero + " hors de la grille de 1,27 mm");
    if (b.numero.empty() && b.nom.empty()) r.push_back("CS-04 : broche sans nom ni numéro");
    // Un numéro en double, dans la même unité ou commune (une broche cachée empilée de même nom est permise)
    if (!b.numero.empty()) {
      const auto [it, neuf] = numeros.emplace(std::pair{b.numero, b.unite}, b.nom);
      if (!neuf && !(b.cachee && it->second == b.nom)) r.push_back("CS-02 : numéro " + b.numero + " en double");
    }
    const auto [ip, nouvelle] = places.emplace(std::tuple{b.extremite.x, b.extremite.y, b.unite}, b.numero);
    if (!nouvelle && !b.cachee) r.push_back("CS-03 : broches " + ip->second + " et " + b.numero + " au même point");
    if (b.cachee && (b.type == TypeBroche::AlimEntree || b.type == TypeBroche::AlimSortie) && b.nom.empty())
      r.push_back("CS-07 : broche d'alimentation cachée " + b.numero + " sans nom (elle ne se relie à rien)");
    unitesAvecBroches.insert(b.unite);
  }
  for (int u = 1; u <= d.unites && d.unites > 1; ++u)
    if (!unitesAvecBroches.count(u)) r.push_back("CS-05 : unité " + lettreUnite(u) + " sans broche");
  return r;
}

std::string brochesVersCsv(const DefSymbole& d) {
  std::ostringstream s;
  s << "numero;nom;type;cote;rang;unite;section;groupe\n";
  for (const auto& b : d.broches) {
    const int cote = b.direction == 0 ? 0 : b.direction == 2 ? 1 : b.direction == 1 ? 2 : 3;  // vers le corps : +x ⇒ à gauche
    s << b.numero << ';' << b.nom << ';' << codeTypeBroche(b.type) << ';' << (cote == 0 ? "gauche" : cote == 1 ? "droite" : cote == 2 ? "haut" : "bas") << ';' << b.rang << ';' << b.unite << ';'
      << b.section << ';' << b.groupe << '\n';
  }
  return s.str();
}

std::vector<SpecBroche> brochesDepuisCsv(const std::string& csv, std::vector<std::string>* journal) {
  std::vector<std::vector<std::string>> lignes;
  std::istringstream in(csv);
  std::string l;
  while (std::getline(in, l)) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.empty()) continue;
    std::vector<std::string> cases;
    std::string x;
    for (char ch : l + ";") {
      if (ch == ';' || ch == ',' || ch == '\t') {
        cases.push_back(x);
        x.clear();
      } else
        x += ch;
    }
    lignes.push_back(cases);
  }
  return specDepuisTableau(lignes, "symbole", journal).broches;
}

std::vector<SpecBroche> rangeeBroches(const std::string& prefixe, int de, int a, int premierNumero, int cote, TypeBroche type) {
  std::vector<SpecBroche> r;
  const int pas = de <= a ? 1 : -1;
  int numero = premierNumero;
  for (int i = de;; i += pas) {
    SpecBroche b;
    b.numero = std::to_string(numero++);
    b.nom = prefixe + std::to_string(i);
    b.type = type;
    b.cote = cote;
    r.push_back(b);
    if (i == a) break;
  }
  return r;
}

DefSymbole symboleResolu(const ConceptionElec& c, const DefSymbole& d) {
  if (d.parent.empty()) return d;
  const DefSymbole* p = c.symbole(d.parent);
  if (!p || p->id == d.id) return d;
  DefSymbole r = symboleResolu(c, *p);
  r.id = d.id;
  r.parent.clear();
  // Champs propres du dérivé ; le dessin et les broches du parent, sauf s'il en a
  for (const auto& [cible, source] : std::vector<std::pair<std::string*, const std::string*>>{
           {&r.nom, &d.nom}, {&r.description, &d.description}, {&r.categorie, &d.categorie}, {&r.empreinte, &d.empreinte}, {&r.fabricant, &d.fabricant}, {&r.mpn, &d.mpn}, {&r.fiche, &d.fiche}, {&r.motsCles, &d.motsCles}})
    if (!source->empty()) *cible = *source;
  if (!d.prefixe.empty() && d.prefixe != "U") r.prefixe = d.prefixe;
  if (!d.broches.empty()) r.broches = d.broches;
  if (!d.traces.empty()) r.traces = d.traces;
  for (const auto& [k, v] : d.proprietes) r.proprietes[k] = v;
  return r;
}

std::optional<DefSymbole> symboleNormalise(const std::string& genre0, const std::string& empreinte, const std::string& style) {
  std::string genre = genre0;
  std::transform(genre.begin(), genre.end(), genre.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  // Numérotation de l'empreinte : SOT-23 (1, 2, 3), TO-92 (1, 2, 3), TO-220 (1, 2, 3), SOD (1 cathode, 2 anode)
  struct Rang {
    std::string nom, numero;
  };
  std::vector<Rang> broches;
  const bool sot23 = empreinte.rfind("SOT-23", 0) == 0;
  if (genre == "MOSFET-P" || genre == "MOSFET-N") {
    // SOT-23 : 1 G, 2 S, 3 D ; TO-220 : 1 G, 2 D, 3 S
    const bool gsd = sot23 || empreinte.empty();
    broches = {{"G", "1"}, {gsd ? "S" : "D", "2"}, {gsd ? "D" : "S", "3"}};
  } else if (genre == "NPN" || genre == "PNP") {
    const bool bec = sot23 || empreinte.empty();
    broches = {{bec ? "B" : "E", "1"}, {bec ? "E" : "B", "2"}, {"C", "3"}};
  } else if (genre == "DIODE" || genre == "ZENER" || genre == "LED") {
    broches = {{"K", "1"}, {"A", "2"}};
  } else if (genre == "AOP") {
    broches = {{"OUT", "1"}, {"-", "2"}, {"+", "3"}, {"V-", "4"}, {"V+", "5"}};
  } else if (genre == "REGULATEUR") {
    broches = {{"IN", "1"}, {"GND", "2"}, {"OUT", "3"}};
  } else
    return std::nullopt;
  DefSymbole d;
  d.id = "assistant:" + genre + (empreinte.empty() ? std::string() : ":" + empreinte);
  d.nom = genre;
  d.style = style;
  d.empreinte = empreinte;
  d.categorie = "Assistant";
  d.prefixe = genre.rfind("MOSFET", 0) == 0 || genre == "NPN" || genre == "PNP" ? "Q" : genre == "AOP" || genre == "REGULATEUR" ? "U" : "D";
  const Nm g = GRILLE_SCHEMA;
  auto broche = [&](const Rang& r, Pt p, int dir, TypeBroche t) {
    Broche b;
    b.numero = r.numero;
    b.nom = r.nom;
    b.extremite = p;
    b.direction = dir;
    b.type = t;
    b.longueur = 2 * g;
    b.nomVisible = genre == "AOP" || genre == "REGULATEUR";
    d.broches.push_back(b);
  };
  auto trait = [&](std::vector<Pt> pts, bool ferme = false, bool rempli = false) {
    Trace t;
    t.forme = Trace::Forme::Polyligne;
    t.points = std::move(pts);
    t.ferme = ferme;
    t.rempli = rempli;
    d.traces.push_back(t);
  };
  auto par = [&](const std::string& nom) -> Rang {
    const auto it = std::find_if(broches.begin(), broches.end(), [&](const Rang& r) { return r.nom == nom; });
    return it == broches.end() ? Rang{nom, std::string()} : *it;
  };
  if (genre == "MOSFET-P" || genre == "MOSFET-N") {
    // Grille à gauche, drain en haut, source en bas ; canal en trois traits, flèche vers le canal (N) ou sortante (P)
    broche(par("G"), {-4 * g, 2 * g}, 0, TypeBroche::Entree);
    broche(par("D"), {2 * g, -4 * g}, 1, TypeBroche::Passive);
    broche(par("S"), {2 * g, 4 * g}, 3, TypeBroche::Passive);
    trait({{-2 * g, 2 * g}, {-g, 2 * g}});
    trait({{-g, -g}, {-g, g}});
    trait({{0, -g * 3 / 2}, {0, -g / 2}});
    trait({{0, -g / 4}, {0, g / 4}});
    trait({{0, g / 2}, {0, g * 3 / 2}});
    trait({{0, -g}, {2 * g, -g}, {2 * g, -2 * g}});
    trait({{0, g}, {2 * g, g}, {2 * g, 2 * g}});
    trait({{0, 0}, {2 * g, 0}, {2 * g, g}});
    if (genre == "MOSFET-N") trait({{0, 0}, {g / 2, -g / 3}, {g / 2, g / 3}}, true, true);
    else trait({{g, 0}, {g / 2, -g / 3}, {g / 2, g / 3}}, true, true);
  } else if (genre == "NPN" || genre == "PNP") {
    broche(par("B"), {-4 * g, 0}, 0, TypeBroche::Entree);
    broche(par("C"), {2 * g, -4 * g}, 1, TypeBroche::Passive);
    broche(par("E"), {2 * g, 4 * g}, 3, TypeBroche::Passive);
    trait({{-2 * g, 0}, {0, 0}});
    trait({{0, -g * 3 / 2}, {0, g * 3 / 2}});
    trait({{0, -g / 2}, {2 * g, -2 * g}});
    trait({{0, g / 2}, {2 * g, 2 * g}});
    if (genre == "NPN") trait({{2 * g, 2 * g}, {g + g / 3, 2 * g - g / 6}, {2 * g - g / 3, g + g / 3}}, true, true);
    else trait({{0, g / 2}, {g / 2, g / 2 + g / 2}, {g / 3, g + g / 6}}, true, true);
  } else if (genre == "DIODE" || genre == "ZENER" || genre == "LED") {
    broche(par("A"), {-4 * g, 0}, 0, TypeBroche::Passive);
    broche(par("K"), {4 * g, 0}, 2, TypeBroche::Passive);
    trait({{-g, -g}, {g, 0}, {-g, g}}, true, style == "ANSI");
    trait({{g, -g}, {g, g}});
    if (genre == "ZENER") trait({{g, -g}, {g * 3 / 2, -g * 3 / 2}});
    if (genre == "LED") {
      trait({{0, -g * 3 / 2}, {g, -g * 5 / 2}});
      trait({{g, -g * 3 / 2}, {2 * g, -g * 5 / 2}});
    }
  } else if (genre == "AOP") {
    broche(par("-"), {-6 * g, -2 * g}, 0, TypeBroche::Entree);
    broche(par("+"), {-6 * g, 2 * g}, 0, TypeBroche::Entree);
    broche(par("OUT"), {6 * g, 0}, 2, TypeBroche::Sortie);
    broche(par("V+"), {0, -6 * g}, 1, TypeBroche::AlimEntree);
    broche(par("V-"), {0, 6 * g}, 3, TypeBroche::AlimEntree);
    trait({{-4 * g, -4 * g}, {4 * g, 0}, {-4 * g, 4 * g}}, true);
  } else {
    broche(par("IN"), {-6 * g, 0}, 0, TypeBroche::AlimEntree);
    broche(par("OUT"), {6 * g, 0}, 2, TypeBroche::AlimSortie);
    broche(par("GND"), {0, 4 * g}, 3, TypeBroche::AlimEntree);
    Trace t;
    t.forme = Trace::Forme::Rectangle;
    t.points = {{-4 * g, -2 * g}, {4 * g, 2 * g}};
    t.fond = true;
    d.traces.push_back(t);
  }
  return d;
}

}  // namespace prom::ecad
