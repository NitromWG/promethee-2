// Prométhée : tableur et gestion de bibliothèque (lot L4, tâche 4.23). Voir gestion_bibliotheque.hpp.
// Licence GPL-3.0-only.
#include "promethee/gestion_bibliotheque.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

#include "promethee/approvisionnement.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
// Minuscules sans accents ni espaces ni ponctuation : « Référence fabricant » → « referencefabricant »
std::string cle(const std::string& s) {
  std::string r;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const unsigned char ch = static_cast<unsigned char>(s[i]);
    if (ch == 0xC3 && i + 1 < s.size()) {
      const unsigned char d = static_cast<unsigned char>(s[++i]) | 0x20;
      r += (d >= 0xA8 && d <= 0xAB) ? 'e' : (d >= 0xA0 && d <= 0xA5) ? 'a' : d == 0xA7 ? 'c' : (d >= 0xB2 && d <= 0xB6) ? 'o' : (d >= 0xAC && d <= 0xAF) ? 'i' : (d >= 0xB9 && d <= 0xBC) ? 'u' : '_';
      continue;
    }
    if (std::isalnum(ch)) r += static_cast<char>(std::tolower(ch));
  }
  return r;
}
std::string sansEspaces(std::string s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return s.substr(i);
}
FamilleValeur familleArticle(const Article& a) {
  const std::string c = cle(a.categorie + " " + a.symbole);
  if (a.symbole == "R" || c.find("resist") != std::string::npos) return FamilleValeur::Resistance;
  if (a.symbole == "C" || c.find("condens") != std::string::npos || c.find("capac") != std::string::npos) return FamilleValeur::Condensateur;
  if (a.symbole == "L" || c.find("induct") != std::string::npos) return FamilleValeur::Inductance;
  return familleDepuisPrefixe(a.symbole);
}
const char* uniteArticle(const Article& a) { return uniteDeFamille(familleArticle(a)); }
}  // namespace

std::string champArticle(const Article& a, const std::string& champ) {
  if (champ == "id") return a.id;
  if (champ == "nom") return a.nom;
  if (champ == "description") return a.description;
  if (champ == "categorie") return a.categorie;
  if (champ == "symbole") return a.symbole;
  if (champ == "empreinte") return a.empreinte;
  if (champ == "valeur") return a.valeur;
  if (champ == "fabricant") return a.fabricant;
  if (champ == "mpn") return a.mpn;
  if (champ == "fiche") return a.fiche;
  if (champ == "lcsc") return a.lcsc;
  if (champ == "modele3D") return a.modele3D;
  if (champ == "base") return a.base;
  if (champ == "etatCycle") return a.etatCycle;
  if (champ == "numeroInterne") return a.numeroInterne;
  if (champ == "version") return std::to_string(a.version);
  if (champ == "hauteur") {
    if (a.hauteur <= 0) return {};
    char t[32];
    std::snprintf(t, sizeof t, "%g", a.hauteur);
    return t;
  }
  if (champ.rfind("param:", 0) == 0) {
    const auto it = a.parametres.find(champ.substr(6));
    return it == a.parametres.end() ? std::string() : it->second;
  }
  return {};
}

bool ecrireChampArticle(Article& a, const std::string& champ, const std::string& v) {
  std::string* cible = champ == "id" ? &a.id : champ == "nom" ? &a.nom : champ == "description" ? &a.description : champ == "categorie" ? &a.categorie
                     : champ == "symbole" ? &a.symbole : champ == "empreinte" ? &a.empreinte : champ == "valeur" ? &a.valeur : champ == "fabricant" ? &a.fabricant
                     : champ == "mpn" ? &a.mpn : champ == "fiche" ? &a.fiche : champ == "lcsc" ? &a.lcsc : champ == "modele3D" ? &a.modele3D : champ == "base" ? &a.base
                     : champ == "etatCycle" ? &a.etatCycle : champ == "numeroInterne" ? &a.numeroInterne : nullptr;
  if (cible) {
    if (*cible == v) return false;
    *cible = v;
    return true;
  }
  if (champ == "hauteur") {
    const double h = v.empty() ? 0 : std::atof(v.c_str());
    if (std::abs(h - a.hauteur) < 1e-12) return false;
    a.hauteur = h;
    return true;
  }
  if (champ == "version") {
    const int n = std::max(1, std::atoi(v.c_str()));
    if (n == a.version) return false;
    a.version = n;
    return true;
  }
  if (champ.rfind("param:", 0) == 0) {
    const std::string k = champ.substr(6);
    const auto it = a.parametres.find(k);
    if (v.empty()) {
      if (it == a.parametres.end()) return false;
      a.parametres.erase(it);
      return true;
    }
    if (it != a.parametres.end() && it->second == v) return false;
    a.parametres[k] = v;
    return true;
  }
  return false;
}

std::string nomParGabarit(const Article& a, const std::string& gabarit) {
  std::string r;
  for (std::size_t i = 0; i < gabarit.size(); ++i) {
    if (gabarit[i] != '{') {
      r += gabarit[i];
      continue;
    }
    const auto fin = gabarit.find('}', i);
    if (fin == std::string::npos) {
      r += gabarit.substr(i);
      break;
    }
    const std::string champ = gabarit.substr(i + 1, fin - i - 1);
    i = fin;
    if (champ == "boitier") {
      const std::string t = codeTaille(a.empreinte);
      r += t.empty() ? (a.parametres.count("boitier") ? a.parametres.at("boitier") : a.empreinte) : t;
    } else if (champ == "valeurSI") {
      const FamilleValeur f = familleArticle(a);
      const ValeurComprise v = lireValeurComposant(a.valeur, f);
      r += v.comprise() ? ecrireSI(v.nombre, uniteArticle(a)) : a.valeur;
    } else if (champ == "tolerance" || champ == "puissance" || champ == "tension") {
      const auto it = a.parametres.find(champ);
      std::string t = it == a.parametres.end() ? std::string() : it->second;
      t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
      r += t;
    } else
      r += champArticle(a, champ);
  }
  // Pas d'espace double ni de séparateur pendant quand un champ manque
  std::string propre;
  for (char ch : r) {
    if ((ch == '_' || ch == ' ' || ch == '-') && !propre.empty() && propre.back() == ch) continue;
    propre += ch;
  }
  while (!propre.empty() && (propre.back() == '_' || propre.back() == ' ' || propre.back() == '-')) propre.pop_back();
  return propre;
}

int renommerParGabarit(std::vector<Article>& articles, const RenommageGabarit& g, const std::vector<std::string>& ids) {
  const std::set<std::string> vises(ids.begin(), ids.end());
  std::set<std::string> pris;
  for (const auto& a : articles)
    if (!vises.empty() && !vises.count(a.id)) pris.insert(a.id);
  std::map<std::string, std::string> renommes;  // ancien → nouveau, pour les bases
  int n = 0;
  for (auto& a : articles) {
    if (!vises.empty() && !vises.count(a.id)) continue;
    bool change = false;
    if (!g.gabaritId.empty()) {
      std::string id = nomParGabarit(a, g.gabaritId);
      if (id.empty()) id = a.id;
      const std::string base = id;
      for (int k = 2; pris.count(id); ++k) id = base + "_" + std::to_string(k);
      pris.insert(id);
      if (id != a.id) {
        renommes[a.id] = id;
        a.id = id;
        change = true;
      }
    }
    if (!g.gabaritNom.empty()) {
      const std::string nom = nomParGabarit(a, g.gabaritNom);
      if (!nom.empty() && nom != a.nom) {
        a.nom = nom;
        change = true;
      }
    }
    n += change ? 1 : 0;
  }
  // Les articles qui héritent d'un article renommé suivent
  for (auto& a : articles)
    if (const auto it = renommes.find(a.base); it != renommes.end()) a.base = it->second;
  return n;
}

std::vector<Article> serieArticles(const Article& modele, int serie, double min, double max, const RenommageGabarit& noms) {
  std::vector<Article> r;
  if (min <= 0 || max < min) return r;
  const auto& valeurs = serieE(serie);
  if (valeurs.empty()) return r;
  const FamilleValeur f = familleArticle(modele);
  for (int decade = static_cast<int>(std::floor(std::log10(min))) - 1; decade <= static_cast<int>(std::ceil(std::log10(max))); ++decade)
    for (double v : valeurs) {
      const double x = v * std::pow(10.0, decade);
      if (x < min * (1 - 1e-9) || x > max * (1 + 1e-9)) continue;
      Article a = modele;
      a.valeur = ecrireCompact(x, f);
      a.offres.clear();
      a.mpn.clear();
      a.lcsc.clear();
      a.version = 1;
      r.push_back(std::move(a));
    }
  std::sort(r.begin(), r.end(), [&](const Article& a, const Article& b) { return lireValeurComposant(a.valeur, f).nombre < lireValeurComposant(b.valeur, f).nombre; });
  RenommageGabarit g = noms;
  if (g.gabaritId.empty()) g.gabaritId = modele.symbole + "_{valeur}_{boitier}";
  for (auto& a : r) a.id.clear();
  renommerParGabarit(r, g);
  return r;
}

std::string champDeColonne(const std::string& entete) {
  const std::string k = cle(entete);
  static const std::map<std::string, std::string> connus = {
      {"id", "id"}, {"identifiant", "id"}, {"article", "id"}, {"code", "id"}, {"nom", "nom"}, {"name", "nom"}, {"designation", "nom"},
      {"valeur", "valeur"}, {"value", "valeur"}, {"comment", "valeur"}, {"commentaire", "valeur"},
      {"empreinte", "empreinte"}, {"footprint", "empreinte"}, {"boitier", "param:boitier"}, {"package", "param:boitier"}, {"case", "param:boitier"},
      {"symbole", "symbole"}, {"symbol", "symbole"}, {"fabricant", "fabricant"}, {"manufacturer", "fabricant"}, {"mfr", "fabricant"},
      {"mpn", "mpn"}, {"referencefabricant", "mpn"}, {"manufacturerpart", "mpn"}, {"manufacturerpartnumber", "mpn"}, {"partnumber", "mpn"},
      {"lcsc", "lcsc"}, {"lcscpart", "lcsc"}, {"lcscpartnumber", "lcsc"}, {"categorie", "categorie"}, {"category", "categorie"}, {"famille", "categorie"},
      {"description", "description"}, {"fiche", "fiche"}, {"fichetechnique", "fiche"}, {"datasheet", "fiche"}, {"hauteur", "hauteur"}, {"height", "hauteur"},
      {"modele3d", "modele3D"}, {"3dmodel", "modele3D"}, {"tolerance", "param:tolerance"}, {"puissance", "param:puissance"}, {"power", "param:puissance"},
      {"tension", "param:tension"}, {"voltage", "param:tension"}, {"dielectrique", "param:dielectrique"}, {"dielectric", "param:dielectrique"},
      {"courant", "param:courant"}, {"current", "param:courant"}, {"base", "base"}, {"etat", "etatCycle"}, {"cycledevie", "etatCycle"}, {"lifecycle", "etatCycle"},
      {"numerointerne", "numeroInterne"}, {"internalpartnumber", "numeroInterne"}};
  const auto it = connus.find(k);
  return it == connus.end() ? std::string() : it->second;
}

ImportTableauArticles articlesDepuisTableau(const std::vector<std::vector<std::string>>& lignes, const std::map<std::string, std::string>& correspondance, const std::string& gabaritId) {
  ImportTableauArticles r;
  if (lignes.empty()) {
    r.journal.push_back("tableau vide");
    return r;
  }
  std::vector<std::string> champs;
  for (const auto& e : lignes.front()) {
    const std::string en = sansEspaces(e);
    std::string ch;
    if (const auto it = correspondance.find(en); it != correspondance.end()) ch = it->second;
    else ch = champDeColonne(en);
    if (ch.empty() && !en.empty()) {
      ch = "param:" + cle(en);
      r.journal.push_back("colonne « " + en + " » gardée en paramètre « " + cle(en) + " »");
    }
    r.colonnes[en] = ch;
    champs.push_back(ch);
  }
  std::set<std::string> ids;
  for (std::size_t i = 1; i < lignes.size(); ++i) {
    Article a;
    bool rien = true;
    for (std::size_t j = 0; j < lignes[i].size() && j < champs.size(); ++j) {
      const std::string v = sansEspaces(lignes[i][j]);
      if (champs[j].empty() || v.empty()) continue;
      ecrireChampArticle(a, champs[j], v);
      rien = false;
    }
    if (rien) continue;
    if (a.valeur.empty() && a.mpn.empty() && a.id.empty()) {
      r.journal.push_back("ligne " + std::to_string(i + 1) + " : ni valeur, ni référence, ni identifiant");
      continue;
    }
    // Empreinte d'après le boîtier pour un passif (« 0603 » et « Résistances » → « R_0603 »)
    if (a.empreinte.empty() && a.parametres.count("boitier")) {
      const std::string t = codeTaille(a.parametres.at("boitier"));
      const FamilleValeur f = familleArticle(a);
      if (!t.empty() && f != FamilleValeur::Aucune) a.empreinte = std::string(f == FamilleValeur::Resistance ? "R" : f == FamilleValeur::Condensateur ? "C" : "L") + "_" + t;
    }
    if (a.symbole.empty()) {
      const FamilleValeur f = familleArticle(a);
      if (f == FamilleValeur::Resistance) a.symbole = "R";
      else if (f == FamilleValeur::Condensateur) a.symbole = "C";
      else if (f == FamilleValeur::Inductance) a.symbole = "L";
    }
    if (a.id.empty()) a.id = nomParGabarit(a, gabaritId);
    const std::string base = a.id;
    for (int k = 2; ids.count(a.id); ++k) a.id = base + "_" + std::to_string(k);
    ids.insert(a.id);
    if (a.nom.empty()) a.nom = a.id;
    r.articles.push_back(std::move(a));
  }
  return r;
}

std::vector<std::vector<std::string>> tableauArticles(const std::vector<Article>& articles, const std::vector<std::string>& champs) {
  std::vector<std::vector<std::string>> t;
  std::vector<std::string> entete{"id"};
  for (const auto& c : champs)
    if (c != "id") entete.push_back(c);
  t.push_back(entete);
  for (const auto& a : articles) {
    std::vector<std::string> l;
    for (const auto& c : entete) l.push_back(champArticle(a, c));
    t.push_back(std::move(l));
  }
  return t;
}

int appliquerTableauArticles(std::vector<Article>& articles, const std::vector<std::vector<std::string>>& tableau, std::vector<std::string>* journal) {
  if (tableau.size() < 2) return 0;
  const auto& entete = tableau.front();
  const auto col = std::find(entete.begin(), entete.end(), "id");
  if (col == entete.end()) {
    if (journal) journal->push_back("colonne « id » absente : les lignes ne se rattachent à aucun article");
    return 0;
  }
  const std::size_t ci = static_cast<std::size_t>(col - entete.begin());
  int n = 0;
  for (std::size_t i = 1; i < tableau.size(); ++i) {
    if (ci >= tableau[i].size()) continue;
    const auto it = std::find_if(articles.begin(), articles.end(), [&](const Article& a) { return a.id == tableau[i][ci]; });
    if (it == articles.end()) {
      if (journal) journal->push_back("ligne " + std::to_string(i + 1) + " : article « " + tableau[i][ci] + " » inconnu");
      continue;
    }
    bool change = false;
    for (std::size_t j = 0; j < entete.size() && j < tableau[i].size(); ++j)
      if (j != ci) change |= ecrireChampArticle(*it, entete[j], tableau[i][j]);
    if (change) {
      ++it->version;  // une modification fait une révision : les composants posés de l'ancienne la voient périmée
      ++n;
    }
  }
  return n;
}

SpecSymbole specDepuisTableau(const std::vector<std::vector<std::string>>& lignes, const std::string& nom, std::vector<std::string>* journal) {
  SpecSymbole s;
  s.nom = nom;
  s.id = identifiantSymbole(nom);
  if (lignes.empty()) return s;
  int cNum = -1, cNom = -1, cType = -1, cCote = -1, cRang = -1, cUnite = -1;
  for (std::size_t j = 0; j < lignes.front().size(); ++j) {
    const std::string k = cle(lignes.front()[j]);
    const int jj = static_cast<int>(j);
    if (k == "numero" || k == "num" || k == "pin" || k == "broche" || k == "number" || k == "pinnumber") cNum = jj;
    else if (k == "nom" || k == "name" || k == "pinname" || k == "signal") cNom = jj;
    else if (k == "type" || k == "electricaltype" || k == "genre") cType = jj;
    else if (k == "cote" || k == "side" || k == "position") cCote = jj;
    else if (k == "rang" || k == "ordre" || k == "order" || k == "rank") cRang = jj;
    else if (k == "unite" || k == "unit" || k == "partie" || k == "part") cUnite = jj;
  }
  if (cNum < 0) {
    if (journal) journal->push_back("colonne du numéro de broche absente");
    return s;
  }
  auto lire = [](const std::vector<std::string>& l, int c) { return c >= 0 && static_cast<std::size_t>(c) < l.size() ? sansEspaces(l[static_cast<std::size_t>(c)]) : std::string(); };
  struct Ligne {
    SpecBroche b;
    int rang = 0;
    std::size_t ordre = 0;
  };
  std::vector<Ligne> v;
  std::set<std::string> vus;
  for (std::size_t i = 1; i < lignes.size(); ++i) {
    Ligne l;
    l.b.numero = lire(lignes[i], cNum);
    if (l.b.numero.empty()) continue;
    if (!vus.insert(l.b.numero).second) {
      if (journal) journal->push_back("ligne " + std::to_string(i + 1) + " : numéro « " + l.b.numero + " » en double, ignoré");
      continue;
    }
    l.b.nom = lire(lignes[i], cNom);
    if (l.b.nom.empty()) l.b.nom = l.b.numero;
    const std::string t = cle(lire(lignes[i], cType));
    l.b.type = t.empty() ? TypeBroche::Bidirectionnelle
             : t.rfind("entree", 0) == 0 || t == "input" || t == "in" ? TypeBroche::Entree
             : t.rfind("sortie", 0) == 0 || t == "output" || t == "out" ? TypeBroche::Sortie
             : t == "alim" || t == "alimentation" || t == "power" || t == "powerin" || t == "alimentree" ? TypeBroche::AlimEntree
             : t == "powerout" || t == "alimsortie" ? TypeBroche::AlimSortie
             : t == "passive" || t == "passif" ? TypeBroche::Passive
             : t == "nc" || t == "nonconnectee" || t == "noconnect" ? TypeBroche::NonConnectee
             : t == "collecteurouvert" || t == "opencollector" || t == "opendrain" ? TypeBroche::CollecteurOuvert
             : t == "troisetats" || t == "tristate" ? TypeBroche::TroisEtats : typeBrocheDepuis(t);
    const std::string c = cle(lire(lignes[i], cCote));
    l.b.cote = c.empty() || c == "auto" ? -1 : c == "gauche" || c == "left" || c == "g" || c == "l" ? 0 : c == "droite" || c == "right" || c == "d" || c == "r" ? 1 : c == "haut" || c == "top" || c == "h" || c == "t" ? 2 : c == "bas" || c == "bottom" || c == "b" ? 3 : -1;
    const std::string rg = lire(lignes[i], cRang);
    l.rang = rg.empty() ? 0 : std::atoi(rg.c_str());
    const std::string u = lire(lignes[i], cUnite);
    l.b.unite = u.empty() ? 0 : std::isdigit(static_cast<unsigned char>(u[0])) ? std::atoi(u.c_str()) : (u.size() == 1 && std::isalpha(static_cast<unsigned char>(u[0])) ? std::toupper(static_cast<unsigned char>(u[0])) - 'A' + 1 : 0);
    l.ordre = i;
    v.push_back(l);
  }
  // Ordre sur chaque côté : le rang, puis l'ordre des lignes
  std::stable_sort(v.begin(), v.end(), [](const Ligne& a, const Ligne& b) { return std::tie(a.b.unite, a.b.cote, a.rang, a.ordre) < std::tie(b.b.unite, b.b.cote, b.rang, b.ordre); });
  for (const auto& l : v) s.broches.push_back(l.b);
  return s;
}

// ---------- Bibliothèque complète ----------
std::string texteContenuBibliotheque(const ContenuBibliotheque& b) {
  nlohmann::ordered_json j;
  j["format"] = "promethee-bibliotheque";
  j["version"] = 1;
  j["nom"] = b.nom;
  auto& a = j["articles"] = nlohmann::ordered_json::array();
  for (const auto& x : b.articles) a.push_back(articleVersJson(x));
  auto& s = j["symboles"] = nlohmann::ordered_json::array();
  for (const auto& x : b.symboles) s.push_back(symboleVersJson(x));
  auto& e = j["empreintes"] = nlohmann::ordered_json::array();
  for (const auto& x : b.empreintes) e.push_back(empreinteVersJson(x));
  auto& m = j["modeles3D"] = nlohmann::ordered_json::object();
  for (const auto& [chemin, contenu] : b.modeles3D) m[chemin] = contenu;
  return j.dump(1);
}

ContenuBibliotheque lireContenuBibliotheque(const std::string& texte, std::string* erreur) {
  ContenuBibliotheque b;
  try {
    const auto j = nlohmann::ordered_json::parse(texte);
    if (j.value("format", std::string()) != "promethee-bibliotheque") throw std::runtime_error("ce n'est pas une bibliothèque Prométhée");
    b.nom = j.value("nom", std::string());
    if (j.contains("articles"))
      for (const auto& x : j["articles"]) b.articles.push_back(articleDepuisJson(x));
    if (j.contains("symboles"))
      for (const auto& x : j["symboles"]) b.symboles.push_back(symboleDepuisJson(x));
    if (j.contains("empreintes"))
      for (const auto& x : j["empreintes"]) b.empreintes.push_back(empreinteDepuisJson(x));
    if (j.contains("modeles3D"))
      for (const auto& [k, v] : j["modeles3D"].items()) b.modeles3D[k] = v.get<std::string>();
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
  }
  return b;
}

Dependances dependancesArticle(const ContenuBibliotheque& src, const std::string& id) {
  Dependances d;
  std::set<std::string> vus;
  std::string courant = id;
  while (!courant.empty() && vus.insert(courant).second) {
    const auto it = std::find_if(src.articles.begin(), src.articles.end(), [&](const Article& a) { return a.id == courant; });
    if (it == src.articles.end()) {
      if (!articleIntegre(courant)) d.manquants.push_back("article « " + courant + " »");
      break;
    }
    d.articles.push_back(courant);
    const Article& a = *it;
    if (!a.symbole.empty()) {
      if (std::any_of(src.symboles.begin(), src.symboles.end(), [&](const DefSymbole& s) { return s.id == a.symbole; })) {
        if (std::find(d.symboles.begin(), d.symboles.end(), a.symbole) == d.symboles.end()) d.symboles.push_back(a.symbole);
      } else if (!symboleBibliotheque(a.symbole))
        d.manquants.push_back("symbole « " + a.symbole + " » de " + a.id);
    }
    if (!a.empreinte.empty()) {
      if (std::any_of(src.empreintes.begin(), src.empreintes.end(), [&](const DefEmpreinte& e) { return e.id == a.empreinte; })) {
        if (std::find(d.empreintes.begin(), d.empreintes.end(), a.empreinte) == d.empreintes.end()) d.empreintes.push_back(a.empreinte);
      } else if (!genererEmpreinte(a.empreinte))
        d.manquants.push_back("empreinte « " + a.empreinte + " » de " + a.id);
    }
    if (!a.modele3D.empty()) {
      if (src.modeles3D.count(a.modele3D)) {
        if (std::find(d.modeles3D.begin(), d.modeles3D.end(), a.modele3D) == d.modeles3D.end()) d.modeles3D.push_back(a.modele3D);
      } else
        d.manquants.push_back("modèle 3D « " + a.modele3D + " » de " + a.id);
    }
    courant = a.base;
  }
  return d;
}

RapportCopie copierVersBibliotheque(const ContenuBibliotheque& src, const std::vector<std::string>& ids, ContenuBibliotheque& dst, bool remplacer) {
  RapportCopie r;
  std::set<std::string> articles, symboles, empreintes, modeles;
  for (const auto& id : ids) {
    const Dependances d = dependancesArticle(src, id);
    articles.insert(d.articles.begin(), d.articles.end());
    symboles.insert(d.symboles.begin(), d.symboles.end());
    empreintes.insert(d.empreintes.begin(), d.empreintes.end());
    modeles.insert(d.modeles3D.begin(), d.modeles3D.end());
    for (const auto& m : d.manquants)
      if (std::find(r.manquants.begin(), r.manquants.end(), m) == r.manquants.end()) r.manquants.push_back(m);
  }
  // Un objet présent : remplacé ou gardé ; absent : ajouté
  auto poser = [&](auto& liste, const auto& objet, const std::string& id, const std::string& genre, int& compte) {
    const auto it = std::find_if(liste.begin(), liste.end(), [&](const auto& x) { return x.id == id; });
    if (it == liste.end()) {
      liste.push_back(objet);
      ++compte;
    } else if (remplacer) {
      if (!(*it == objet)) r.remplaces.push_back(genre + " « " + id + " »");
      *it = objet;
      ++compte;
    } else if (!(*it == objet))
      r.gardes.push_back(genre + " « " + id + " »");
  };
  for (const auto& a : src.articles)
    if (articles.count(a.id)) poser(dst.articles, a, a.id, "article", r.articles);
  for (const auto& s : src.symboles)
    if (symboles.count(s.id)) poser(dst.symboles, s, s.id, "symbole", r.symboles);
  for (const auto& e : src.empreintes)
    if (empreintes.count(e.id)) poser(dst.empreintes, e, e.id, "empreinte", r.empreintes);
  for (const auto& m : modeles) {
    const auto& contenu = src.modeles3D.at(m);
    const auto it = dst.modeles3D.find(m);
    if (it == dst.modeles3D.end() || remplacer) {
      if (it != dst.modeles3D.end() && it->second != contenu) r.remplaces.push_back("modèle 3D « " + m + " »");
      dst.modeles3D[m] = contenu;
      ++r.modeles3D;
    } else if (it->second != contenu)
      r.gardes.push_back("modèle 3D « " + m + " »");
  }
  return r;
}

std::vector<std::string> controlerBibliotheque(const ContenuBibliotheque& b) {
  std::vector<std::string> r;
  std::set<std::string> ids;
  for (const auto& a : b.articles) {
    if (!ids.insert(a.id).second) r.push_back(a.id + " : identifiant en double");
    // Symbole et empreinte : ceux de la bibliothèque, sinon ceux de Prométhée ; l'article résolu le long de ses bases
    const Article x = resoudreArticle(a, [&](const std::string& id) -> const Article* {
      const auto it = std::find_if(b.articles.begin(), b.articles.end(), [&](const Article& y) { return y.id == id; });
      return it != b.articles.end() ? &*it : articleIntegre(id);
    });
    const auto s = std::find_if(b.symboles.begin(), b.symboles.end(), [&](const DefSymbole& y) { return y.id == x.symbole; });
    const DefSymbole* ps = s != b.symboles.end() ? &*s : symboleBibliotheque(x.symbole);
    const auto e = std::find_if(b.empreintes.begin(), b.empreintes.end(), [&](const DefEmpreinte& y) { return y.id == x.empreinte; });
    std::optional<DefEmpreinte> generee;
    if (e == b.empreintes.end() && !x.empreinte.empty()) generee = genererEmpreinte(x.empreinte);
    const DefEmpreinte* pe = e != b.empreintes.end() ? &*e : generee ? &*generee : nullptr;
    for (auto& m : controlerArticle(x, ps, pe)) r.push_back(std::move(m));
    if (!x.modele3D.empty() && !b.modeles3D.count(x.modele3D)) r.push_back(a.id + " : modèle 3D « " + x.modele3D + " » absent de la bibliothèque");
  }
  return r;
}

int RapportBibliotheque::compter(const std::string& genre, const std::string& etat) const {
  return static_cast<int>(std::count_if(objets.begin(), objets.end(), [&](const LigneRapportObjet& l) { return l.genre == genre && (etat.empty() || l.etat == etat); }));
}

std::string RapportBibliotheque::csv() const {
  auto champ = [](const std::string& s) {
    std::string r = "\"";
    for (char ch : s) r += ch == '"' ? std::string("\"\"") : std::string(1, ch);
    return r + "\"";
  };
  std::string o = "\xEF\xBB\xBFGenre;Nom;État;Détail\r\n";
  for (const auto& l : objets) o += champ(l.genre) + ";" + champ(l.nom) + ";" + champ(l.etat) + ";" + champ(l.detail) + "\r\n";
  return o;
}

}  // namespace prom::ecad
