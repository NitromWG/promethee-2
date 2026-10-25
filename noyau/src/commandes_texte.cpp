// Prométhée : ligne de commande et macros. Voir commandes_texte.hpp. Licence GPL-3.0-only.
#include "promethee/commandes_texte.hpp"

#include <algorithm>
#include <cctype>
#include <locale>
#include <cmath>
#include <set>
#include <sstream>

#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"

using J = nlohmann::ordered_json;

namespace prom {

namespace {
std::string normal(const std::string& s) {
  std::string t = translitterer(s);
  std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return t;
}
std::string sansBlancs(const std::string& s) {
  const auto d = s.find_first_not_of(" \t\r\n");
  if (d == std::string::npos) return {};
  return s.substr(d, s.find_last_not_of(" \t\r\n") - d + 1);
}
// Découpe en mots, guillemets et crochets respectés
std::vector<std::string> mots(const std::string& l, std::string* erreur) {
  std::vector<std::string> v;
  std::string courant;
  bool guillemet = false;
  int crochet = 0;
  for (std::size_t i = 0; i < l.size(); ++i) {
    const char ch = l[i];
    if (guillemet) {
      if (ch == '\\' && i + 1 < l.size()) {
        courant += ch;
        courant += l[++i];
        continue;
      }
      if (ch == '"') guillemet = false;
      courant += ch;
      continue;
    }
    if (ch == '"') {
      guillemet = true;
      courant += ch;
      continue;
    }
    if (ch == '[') ++crochet;
    if (ch == ']') --crochet;
    if ((ch == ' ' || ch == '\t') && crochet == 0) {
      if (!courant.empty()) v.push_back(courant), courant.clear();
      continue;
    }
    if (ch == '#' && crochet == 0 && courant.empty()) break;  // commentaire en fin de ligne
    courant += ch;
  }
  if (guillemet && erreur) *erreur = "guillemet non fermé";
  if (crochet != 0 && erreur) *erreur = "crochet non fermé";
  if (!courant.empty()) v.push_back(courant);
  return v;
}
// Nombre avec unité vers les millimètres (longueurs) ou l'unité de la valeur ; rien si ce n'est pas un nombre
std::optional<double> nombre(const std::string& t0) {
  std::string t = sansBlancs(t0);
  if (t.empty()) return std::nullopt;
  // Virgule décimale
  for (std::size_t i = 1; i + 1 < t.size(); ++i)
    if (t[i] == ',' && std::isdigit(static_cast<unsigned char>(t[i - 1])) && std::isdigit(static_cast<unsigned char>(t[i + 1]))) t[i] = '.';
  // Lecture indépendante de la langue du poste (point décimal)
  if (!(std::isdigit(static_cast<unsigned char>(t[0])) || ((t[0] == '-' || t[0] == '+' || t[0] == '.') && t.size() > 1))) return std::nullopt;
  std::istringstream in(t);
  in.imbue(std::locale::classic());
  double v = 0;
  if (!(in >> v)) return std::nullopt;
  std::string reste;
  std::getline(in, reste);
  std::string unite = normal(sansBlancs(reste));
  if (unite.empty() || unite == "mm") return v;
  if (unite == "cm") return v * 10;
  if (unite == "m") return v * 1000;
  if (unite == "um" || unite == "µm" || unite == "micron") return v / 1000;
  if (unite == "mil" || unite == "mils" || unite == "th") return v * 0.0254;
  if (unite == "in" || unite == "\"" || unite == "pouce" || unite == "pouces") return v * 25.4;
  if (unite == "deg" || unite == "°" || unite == "degres" || unite == "degre") return v;
  if (unite == "%") return v / 100;
  return std::nullopt;  // « 4k7 », « 100n » : une valeur de composant, gardée en texte
}
J valeur(const std::string& t) {
  if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
    std::string s;
    for (std::size_t i = 1; i + 1 < t.size(); ++i) {
      if (t[i] == '\\' && i + 2 < t.size()) {
        ++i;
        s += t[i] == 'n' ? '\n' : t[i];
        continue;
      }
      s += t[i];
    }
    return J(s);
  }
  if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
    J a = J::array();
    const std::string in = t.substr(1, t.size() - 2);
    std::string courant;
    for (std::size_t i = 0; i <= in.size(); ++i) {
      const bool sep = i == in.size() || in[i] == ';' || (in[i] == ',' && (i + 1 >= in.size() || in[i + 1] == ' ' || !std::isdigit(static_cast<unsigned char>(in[i + 1])) || i == 0 ||
                                                                             !std::isdigit(static_cast<unsigned char>(in[i - 1]))));
      if (sep) {
        if (!sansBlancs(courant).empty()) a.push_back(valeur(sansBlancs(courant)));
        courant.clear();
      } else
        courant += in[i];
    }
    return a;
  }
  const std::string n = normal(t);
  if (n == "oui" || n == "vrai" || n == "true") return J(true);
  if (n == "non" || n == "faux" || n == "false") return J(false);
  if (const auto x = nombre(t)) return J(*x);
  return J(t);
}
std::string texteNombre(double v) {
  char b[40];
  std::snprintf(b, sizeof b, "%.6g", v);
  return b;
}
std::string texteValeur(const J& v) {
  if (v.is_boolean()) return v.get<bool>() ? "oui" : "non";
  if (v.is_number()) return texteNombre(v.get<double>());
  if (v.is_array()) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + texteValeur(v[i]);
    return s + "]";
  }
  if (v.is_string()) {
    const std::string s = v.get<std::string>();
    const bool simple = !s.empty() && s.find_first_of(" \t\"#[]=\\") == std::string::npos && !nombre(s) && normal(s) != "oui" && normal(s) != "non";
    if (simple) return s;
    std::string q = "\"";
    for (const char ch : s) {
      if (ch == '"' || ch == '\\') q += '\\';
      if (ch == '\n') {
        q += "\\n";
        continue;
      }
      q += ch;
    }
    return q + "\"";
  }
  return v.dump();
}
}  // namespace

std::string methodeDeMots(const std::string& m0) {
  std::string m = normal(sansBlancs(m0));
  std::replace(m.begin(), m.end(), ' ', '.');
  for (const auto& [nom, d] : methodesApi())
    if (normal(nom) == m) return nom;
  return {};
}

std::vector<std::string> parametresDe(const std::string& methode) {
  std::vector<std::string> v;
  for (const auto& [nom, d] : methodesApi()) {
    if (nom != methode) continue;
    const std::size_t a = d.find('{'), b = d.find('}', a == std::string::npos ? 0 : a);
    if (a == std::string::npos || b == std::string::npos) break;
    std::istringstream in(d.substr(a + 1, b - a - 1));
    for (std::string x; std::getline(in, x, ',');) {
      x = sansBlancs(x);
      const std::size_t fin = x.find_first_of(":?[ ");
      x = x.substr(0, fin);
      if (!x.empty() && std::isalpha(static_cast<unsigned char>(x[0]))) v.push_back(x);
    }
  }
  return v;
}

std::optional<J> requeteDeLigne(const std::string& ligne0, std::string* erreur) {
  const std::string ligne = sansBlancs(ligne0);
  if (ligne.empty() || ligne[0] == '#') return std::nullopt;
  std::string e;
  const auto m = mots(ligne, &e);
  if (!e.empty()) {
    if (erreur) *erreur = e;
    return std::nullopt;
  }
  if (m.empty()) return std::nullopt;
  // La méthode : un mot, ou deux (« carte contour ») tant qu'ils ne portent pas de « = »
  std::string methode;
  std::size_t premier = 0;
  if (m.size() >= 2 && m[1].find('=') == std::string::npos) methode = methodeDeMots(m[0] + " " + m[1]), premier = 2;
  if (methode.empty()) methode = methodeDeMots(m[0]), premier = 1;
  if (methode.empty()) {
    if (erreur) *erreur = "commande inconnue : « " + m[0] + " »";
    return std::nullopt;
  }
  J params = J::object();
  const auto connus = parametresDe(methode);
  for (std::size_t i = premier; i < m.size(); ++i) {
    const std::size_t eg = m[i].find('=');
    if (eg == std::string::npos || eg == 0) {
      if (erreur) *erreur = "« " + m[i] + " » : clé=valeur attendue";
      return std::nullopt;
    }
    std::string cle = normal(m[i].substr(0, eg));
    for (const auto& k : connus)
      if (normal(k) == cle) cle = k;
    // Clés de texte : la valeur reste écrite telle quelle (« valeur=100 » est une valeur de composant, pas 100 mm)
    static const std::set<std::string> textes = {"ref", "valeur", "texte", "reseau", "symbole", "couche", "titre", "societe", "auteur", "revision", "date", "commentaire",
                                                 "variante", "expression", "mesure", "commande", "source", "nom"};
    const std::string brut = m[i].substr(eg + 1);
    J v = valeur(brut);
    if (textes.count(cle) && v.is_number()) v = J(brut);
    params[cle] = v;
  }
  return J{{"jsonrpc", "2.0"}, {"method", methode}, {"params", params}};
}

std::string ligneDeRequete(const J& q) {
  std::string l = q.value("method", std::string());
  if (q.contains("params") && q["params"].is_object())
    for (auto it = q["params"].begin(); it != q["params"].end(); ++it) l += " " + it.key() + "=" + texteValeur(it.value());
  return l;
}

std::vector<std::string> completer(const std::string& debut, std::size_t n) {
  std::vector<std::string> v;
  const std::string d = normal(debut);
  const std::size_t espace = d.rfind(' ');
  if (espace == std::string::npos) {
    for (const auto& [nom, x] : methodesApi())
      if (normal(nom).rfind(d, 0) == 0) v.push_back(nom);
  } else {
    // Après la méthode : ses clés pas encore écrites
    std::string e;
    const auto m = mots(debut, &e);
    std::string methode = m.empty() ? std::string() : methodeDeMots(m[0]);
    if (methode.empty() && m.size() >= 2) methode = methodeDeMots(m[0] + " " + m[1]);
    const std::string dernier = d.substr(espace + 1);
    for (const auto& k : parametresDe(methode))
      if (normal(k).rfind(dernier, 0) == 0 && debut.find(" " + k + "=") == std::string::npos) v.push_back(debut.substr(0, debut.size() - dernier.size()) + k + "=");
  }
  if (v.size() > n) v.resize(n);
  return v;
}

Macro lireMacro(const std::string& texte) {
  Macro m;
  std::istringstream in(texte);
  bool tete = true;
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    const std::string t = sansBlancs(l);
    if (t.empty()) continue;
    if (t[0] == '#') {
      if (!tete) continue;
      const std::string c = sansBlancs(t.substr(1));
      if (m.nom.empty() && normal(c).rfind("macro", 0) == 0 && c.find(':') != std::string::npos) m.nom = sansBlancs(c.substr(c.find(':') + 1));
      else if (!c.empty()) m.description += (m.description.empty() ? "" : "\n") + c;
      continue;
    }
    tete = false;
    m.lignes.push_back(t);
  }
  return m;
}

std::string texteMacro(const Macro& m) {
  std::string t = "# macro : " + (m.nom.empty() ? std::string("sans nom") : m.nom) + "\n";
  std::istringstream d(m.description);
  for (std::string l; std::getline(d, l);) t += "# " + l + "\n";
  for (const auto& l : m.lignes) t += l + "\n";
  return t;
}

ReponseApi executerMacro(Projet& p, const Macro& m, std::vector<std::string>* erreurs) {
  J lot = J::array();
  int n = 0;
  for (const auto& l : m.lignes) {
    ++n;
    std::string e;
    const auto q = requeteDeLigne(l, &e);
    if (!q) {
      if (!e.empty()) {
        if (erreurs) erreurs->push_back("ligne " + std::to_string(n) + " : " + e);
        ReponseApi r;
        r.reponse = {{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32602}, {"message", "ligne " + std::to_string(n) + " : " + e}}}};
        return r;
      }
      continue;
    }
    J x = *q;
    x["id"] = n;
    lot.push_back(x);
  }
  ReponseApi r = traiterRequete(p, J{{"jsonrpc", "2.0"}, {"id", 0}, {"method", "lot"}, {"params", lot}});
  if (r.reponse.contains("error") && erreurs) erreurs->push_back(r.reponse["error"].value("message", std::string()));
  if (r.modifie) r.nomPas = "Macro « " + (m.nom.empty() ? std::string("sans nom") : m.nom) + " »";
  return r;
}

void PredicteurCommandes::observer(const std::string& methode) {
  if (methode.empty()) return;
  if (!m_derniere.empty()) ++m_suites[m_derniere][methode];
  m_derniere = methode;
}

std::vector<std::string> PredicteurCommandes::proposer(std::size_t n) const {
  std::vector<std::pair<int, std::string>> v;
  if (const auto it = m_suites.find(m_derniere); it != m_suites.end())
    for (const auto& [m, k] : it->second) v.emplace_back(-k, m);
  std::sort(v.begin(), v.end());
  std::vector<std::string> r;
  for (const auto& [k, m] : v)
    if (r.size() < n) r.push_back(m);
  return r;
}

std::string PredicteurCommandes::texte() const {
  J j = {{"derniere", m_derniere}, {"suites", J::object()}};
  for (const auto& [a, s] : m_suites)
    for (const auto& [b, k] : s) j["suites"][a][b] = k;
  return j.dump();
}

void PredicteurCommandes::lire(const std::string& t) {
  const J j = J::parse(t, nullptr, false);
  if (!j.is_object()) return;
  m_derniere = j.value("derniere", std::string());
  m_suites.clear();
  if (j.contains("suites") && j["suites"].is_object())
    for (auto a = j["suites"].begin(); a != j["suites"].end(); ++a)
      if (a.value().is_object())
        for (auto b = a.value().begin(); b != a.value().end(); ++b)
          if (b.value().is_number_integer()) m_suites[a.key()][b.key()] = b.value().get<int>();
}

}  // namespace prom
