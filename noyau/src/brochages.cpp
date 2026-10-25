// Prométhée : brochages lus. Voir brochages.hpp. Licence GPL-3.0-only.
#include "promethee/brochages.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <sstream>

#include "promethee/approvisionnement.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}
std::string majuscules(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return s;
}
std::string sansBlancs(std::string s) {
  const auto d = s.find_first_not_of(" \t\r\n");
  if (d == std::string::npos) return {};
  const auto f = s.find_last_not_of(" \t\r\n");
  return s.substr(d, f - d + 1);
}
// BSDL sans commentaires (« -- » jusqu'à la fin de la ligne), hors des chaînes
std::string sansCommentaires(const std::string& t) {
  std::string r;
  bool chaine = false;
  for (std::size_t i = 0; i < t.size(); ++i) {
    if (t[i] == '"') chaine = !chaine;
    if (!chaine && t[i] == '-' && i + 1 < t.size() && t[i + 1] == '-') {
      while (i < t.size() && t[i] != '\n') ++i;
      r += '\n';
      continue;
    }
    r += t[i];
  }
  return r;
}
bool avantNumerique(const std::string& a, const std::string& b) {
  // Numéros (« 12 ») puis coordonnées de matrice (« A1 », « B12 ») dans l'ordre naturel
  return ordreNaturel(a, b);
}
}  // namespace

std::optional<Brochage> lireBsdl(const std::string& texte0, std::string* erreur) {
  const std::string texte = sansCommentaires(texte0);
  auto echec = [&](const std::string& m) -> std::optional<Brochage> {
    if (erreur) *erreur = m;
    return std::nullopt;
  };
  Brochage b;
  std::smatch m;
  static const std::regex entite(R"(\bentity\s+([A-Za-z_][A-Za-z0-9_]*)\s+is\b)", std::regex::icase);
  if (!std::regex_search(texte, m, entite)) return echec("Pas de déclaration « entity … is » : ce n’est pas un fichier BSDL.");
  b.entite = m[1].str();
  static const std::regex generique(R"(PHYSICAL_PIN_MAP\s*:\s*string\s*:=\s*\"([^\"]+)\")", std::regex::icase);
  if (std::regex_search(texte, m, generique)) b.boitier = m[1].str();
  // Ports : « port ( … ); » jusqu'à la parenthèse fermante qui équilibre
  static const std::regex debutPort(R"(\bport\s*\()", std::regex::icase);
  if (!std::regex_search(texte, m, debutPort)) return echec("Pas de liste de ports.");
  std::size_t i = static_cast<std::size_t>(m.position() + m.length());
  int prof = 1;
  std::size_t j = i;
  for (; j < texte.size() && prof > 0; ++j) {
    if (texte[j] == '(') ++prof;
    else if (texte[j] == ')') --prof;
  }
  const std::string ports = texte.substr(i, j - i - 1);
  struct Port {
    std::string direction;
    bool vecteur = false;
    int de = 0, a = 0;
  };
  std::map<std::string, Port> parNom;
  std::vector<std::string> ordre;
  std::istringstream lp(ports);
  for (std::string decl; std::getline(lp, decl, ';');) {
    const std::size_t dp = decl.find(':');
    if (dp == std::string::npos) continue;
    std::string noms = decl.substr(0, dp), reste = minuscules(decl.substr(dp + 1));
    Port p;
    static const std::regex dir(R"(^\s*(in|out|inout|buffer|linkage)\b)");
    std::smatch md;
    if (!std::regex_search(reste, md, dir)) continue;
    p.direction = md[1].str();
    static const std::regex vec(R"(bit_vector\s*\(\s*(\d+)\s+(to|downto)\s+(\d+)\s*\))");
    if (std::regex_search(reste, md, vec)) {
      p.vecteur = true;
      p.de = std::stoi(md[1].str());
      p.a = std::stoi(md[3].str());
    }
    std::istringstream ln(noms);
    for (std::string n; std::getline(ln, n, ',');)
      if (const std::string x = sansBlancs(n); !x.empty()) {
        parNom[majuscules(x)] = p;
        ordre.push_back(majuscules(x));
      }
  }
  if (parNom.empty()) return echec("Liste de ports vide.");
  // Chaîne de brochage : la constante PIN_MAP_STRING du boîtier (ou la première), chaînes concaténées par « & »
  std::string chaine;
  static const std::regex constante(R"(\bconstant\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*PIN_MAP_STRING\s*:=)", std::regex::icase);
  for (auto it = std::sregex_iterator(texte.begin(), texte.end(), constante); it != std::sregex_iterator(); ++it) {
    const std::string nom = (*it)[1].str();
    if (!chaine.empty() && !b.boitier.empty() && majuscules(nom) != majuscules(b.boitier)) continue;
    std::size_t k = static_cast<std::size_t>(it->position() + it->length());
    std::string c;
    while (k < texte.size()) {
      const std::size_t q1 = texte.find('"', k);
      const std::size_t fin = texte.find(';', k);
      if (q1 == std::string::npos || (fin != std::string::npos && fin < q1)) break;
      const std::size_t q2 = texte.find('"', q1 + 1);
      if (q2 == std::string::npos) break;
      c += texte.substr(q1 + 1, q2 - q1 - 1);
      k = q2 + 1;
    }
    if (b.boitier.empty() || majuscules(nom) == majuscules(b.boitier) || chaine.empty()) chaine = c;
    if (b.boitier.empty()) b.boitier = nom;
  }
  if (chaine.empty()) return echec("Pas de chaîne de brochage (PIN_MAP_STRING).");
  // « PORT : 12 » ou « PORT : (1, 2, 3) », séparés par des virgules hors parenthèses
  std::vector<std::string> entrees;
  {
    std::string courant;
    int p = 0;
    for (const char ch : chaine) {
      if (ch == '(') ++p;
      if (ch == ')') --p;
      if (ch == ',' && p == 0) {
        entrees.push_back(courant);
        courant.clear();
      } else
        courant += ch;
    }
    if (!sansBlancs(courant).empty()) entrees.push_back(courant);
  }
  for (const auto& e : entrees) {
    const std::size_t dp = e.find(':');
    if (dp == std::string::npos) continue;
    const std::string nom = majuscules(sansBlancs(e.substr(0, dp)));
    std::string droite = sansBlancs(e.substr(dp + 1));
    std::vector<std::string> numeros;
    if (!droite.empty() && droite.front() == '(') {
      droite = droite.substr(1, droite.find(')') == std::string::npos ? std::string::npos : droite.find(')') - 1);
      std::istringstream ln(droite);
      for (std::string n; std::getline(ln, n, ',');)
        if (const std::string x = sansBlancs(n); !x.empty()) numeros.push_back(x);
    } else if (!droite.empty())
      numeros.push_back(droite);
    const auto it = parNom.find(nom);
    if (it == parNom.end()) {
      b.alertes.push_back("Port « " + nom + " » du brochage absent de la liste des ports.");
      continue;
    }
    const Port& p = it->second;
    if (p.vecteur) {
      const int n = std::abs(p.a - p.de) + 1;
      if (static_cast<int>(numeros.size()) != n) b.alertes.push_back("Port « " + nom + " » : " + std::to_string(numeros.size()) + " broches pour " + std::to_string(n) + " bits.");
      for (std::size_t k = 0; k < numeros.size(); ++k) {
        const int indice = p.de <= p.a ? p.de + static_cast<int>(k) : p.de - static_cast<int>(k);
        b.broches.push_back({numeros[k], nom + std::to_string(indice), p.direction});
      }
    } else {
      // Un port simple sur plusieurs broches (alimentations) : chacune porte le nom du port
      for (const auto& n : numeros) b.broches.push_back({n, nom, p.direction});
    }
  }
  for (const auto& n : ordre)
    if (std::none_of(b.broches.begin(), b.broches.end(), [&](const BrocheLue& x) { return x.nom == n || (parNom[n].vecteur && x.nom.rfind(n, 0) == 0); }))
      b.alertes.push_back("Port « " + n + " » sans broche dans le brochage.");
  std::sort(b.broches.begin(), b.broches.end(), [](const BrocheLue& x, const BrocheLue& y) { return avantNumerique(x.numero, y.numero); });
  for (std::size_t k = 1; k < b.broches.size(); ++k)
    if (b.broches[k].numero == b.broches[k - 1].numero) b.alertes.push_back("Broche " + b.broches[k].numero + " donnée deux fois.");
  return b;
}

std::optional<Brochage> lireTableauBrochage(const std::string& texte, std::string* erreur) {
  const auto lignes = lireTableau(texte);
  if (lignes.size() < 2) {
    if (erreur) *erreur = "Tableau vide.";
    return std::nullopt;
  }
  int cNum = -1, cNom = -1, cType = -1;
  for (std::size_t i = 0; i < lignes[0].size(); ++i) {
    const std::string h = minuscules(sansBlancs(lignes[0][i]));
    if (cNum < 0 && (h == "numéro" || h == "numero" || h == "broche" || h == "pin" || h == "n°" || h == "pin number" || h == "no")) cNum = static_cast<int>(i);
    else if (cNom < 0 && (h == "nom" || h == "signal" || h == "name" || h == "pin name" || h == "fonction")) cNom = static_cast<int>(i);
    else if (cType < 0 && (h == "type" || h == "direction" || h == "e/s")) cType = static_cast<int>(i);
  }
  if (cNum < 0 || cNom < 0) {
    if (erreur) *erreur = "Colonnes « numéro » et « nom » attendues en tête du tableau.";
    return std::nullopt;
  }
  Brochage b;
  for (std::size_t i = 1; i < lignes.size(); ++i) {
    const auto& l = lignes[i];
    if (static_cast<int>(l.size()) <= std::max(cNum, cNom)) continue;
    const std::string n = sansBlancs(l[static_cast<std::size_t>(cNum)]), nom = sansBlancs(l[static_cast<std::size_t>(cNom)]);
    if (n.empty() || nom.empty()) continue;
    b.broches.push_back({n, nom, cType >= 0 && static_cast<int>(l.size()) > cType ? minuscules(sansBlancs(l[static_cast<std::size_t>(cType)])) : std::string()});
  }
  std::sort(b.broches.begin(), b.broches.end(), [](const BrocheLue& x, const BrocheLue& y) { return avantNumerique(x.numero, y.numero); });
  return b;
}

TypeBroche typeDeBrocheLue(const BrocheLue& b) {
  const std::string d = minuscules(b.direction), n = majuscules(b.nom);
  const bool masse = n.rfind("VSS", 0) == 0 || n.rfind("GND", 0) == 0 || n.rfind("AGND", 0) == 0 || n.rfind("DGND", 0) == 0;
  const bool alim = n.rfind("VDD", 0) == 0 || n.rfind("VCC", 0) == 0 || n.rfind("VBAT", 0) == 0 || n.rfind("AVCC", 0) == 0 || n.rfind("AVDD", 0) == 0 || n.rfind("VREF", 0) == 0;
  if (d == "linkage" || d == "power" || d == "alim" || d == "alimentation" || d == "pwr") return masse || alim ? TypeBroche::AlimEntree : TypeBroche::Passive;
  if (masse || alim) return TypeBroche::AlimEntree;
  if (d == "in" || d == "input" || d == "entrée" || d == "entree" || d == "i") return TypeBroche::Entree;
  if (d == "out" || d == "output" || d == "sortie" || d == "o") return TypeBroche::Sortie;
  if (d == "buffer") return TypeBroche::Sortie;
  if (d == "nc" || d == "non connectée" || d == "non connectee") return TypeBroche::NonConnectee;
  return TypeBroche::Bidirectionnelle;
}

DefSymbole symboleDepuisBrochage(const Brochage& b, const std::string& nom, bool unitesParBanc) {
  SpecSymbole s;
  s.nom = nom.empty() ? b.entite : nom;
  s.id = identifiantSymbole(s.nom);
  s.description = "Brochage " + (b.boitier.empty() ? std::string() : b.boitier + " ") + "lu (" + std::to_string(b.broches.size()) + " broches)";
  // Bancs : lettres en tête du nom avant le chiffre (« PA3 » → « PA ») ; alimentations à part
  std::map<std::string, int> bancs;
  auto banc = [](const std::string& n) {
    std::string p;
    for (const char ch : n) {
      if (std::isdigit(static_cast<unsigned char>(ch))) break;
      p += ch;
    }
    return p.size() == n.size() ? std::string() : p;
  };
  if (unitesParBanc && b.broches.size() >= 24) {
    for (const auto& x : b.broches)
      if (typeDeBrocheLue(x) != TypeBroche::AlimEntree)
        if (const std::string k = banc(x.nom); k.size() >= 2 && k[0] == 'P') bancs.emplace(k, 0);
    if (bancs.size() < 2) bancs.clear();
  }
  int u = 0;
  for (auto& [k, n] : bancs) {
    n = ++u;
    s.nomsUnites.push_back("Port " + k);
  }
  const int uniteAlim = bancs.empty() ? 0 : ++u;
  const int uniteDivers = bancs.empty() ? 0 : ++u;
  if (!bancs.empty()) {
    s.nomsUnites.push_back("Alimentation");
    s.nomsUnites.push_back("Commande");
  }
  for (const auto& x : b.broches) {
    SpecBroche p;
    p.numero = x.numero;
    p.nom = x.nom;
    p.type = typeDeBrocheLue(x);
    if (!bancs.empty()) {
      if (p.type == TypeBroche::AlimEntree) p.unite = uniteAlim;
      else if (const auto it = bancs.find(banc(x.nom)); it != bancs.end()) p.unite = it->second;
      else p.unite = uniteDivers;
    }
    s.broches.push_back(p);
  }
  return genererSymbole(s);
}

std::vector<GroupePassifs> consoliderPassifs(const std::vector<Article>& articles) {
  std::map<std::string, GroupePassifs> g;
  for (const auto& a : articles) {
    FamilleValeur f = FamilleValeur::Aucune;
    if (a.symbole == "R") f = FamilleValeur::Resistance;
    else if (a.symbole == "C" || a.symbole == "C_Pol") f = FamilleValeur::Condensateur;
    else if (a.symbole == "L") f = FamilleValeur::Inductance;
    if (f == FamilleValeur::Aucune) continue;
    const ValeurComprise v = lireValeurComposant(a.valeur, f);
    if (!v.comprise()) continue;
    std::string tol;
    if (const auto it = a.parametres.find("tolerance"); it != a.parametres.end()) tol = it->second;
    else if (v.tolerance) tol = std::to_string(static_cast<int>(*v.tolerance)) + " %";
    std::string tension;
    if (const auto it = a.parametres.find("tension"); it != a.parametres.end()) tension = it->second;
    const std::string boitier = codeTaille(a.empreinte);
    const std::string cle = std::string(f == FamilleValeur::Resistance ? "R " : f == FamilleValeur::Condensateur ? "C " : "L ") + ecrireCompact(v.nombre, f) + " " +
                            (boitier.empty() ? a.empreinte : boitier) + (tol.empty() ? std::string() : " " + tol) + (tension.empty() ? std::string() : " " + tension) +
                            (v.dielectrique.empty() ? std::string() : " " + v.dielectrique);
    GroupePassifs& x = g[cle];
    x.cle = cle;
    x.articles.push_back(a.id);
  }
  std::vector<GroupePassifs> r;
  for (auto& [k, x] : g)
    if (x.articles.size() > 1) r.push_back(std::move(x));
  return r;
}

}  // namespace prom::ecad
