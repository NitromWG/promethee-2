// Prométhée : Grafcet (NF EN CEI 60848) — lecture des actions et des réceptivités, vérification, simulation,
// disposition du dessin, texte structuré CEI 61131-3. Licence GPL-3.0-only.
#include "promethee/electrique_grafcet.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "electrique_interne.hpp"

namespace prom::elec {

namespace {

const char* const ET_POINT = "\xC2\xB7";    // ·
const char* const ET_PUCE = "\xE2\x80\xA2";  // •
const char* const NON_SIGNE = "\xC2\xAC";   // ¬
const char* const MONTANT = "\xE2\x86\x91"; // ↑
const char* const DESCENDANT = "\xE2\x86\x93";  // ↓

std::string sansBlancs(const std::string& t) {
  std::size_t a = 0, b = t.size();
  while (a < b && std::isspace(static_cast<unsigned char>(t[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1]))) --b;
  return t.substr(a, b - a);
}

std::string minuscules(std::string t) {
  for (char& c : t)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return t;
}

// Caractère d'identifiant : lettre, chiffre, « _ », lettre accentuée (UTF-8 sur deux octets C3 xx, C5 xx)
std::size_t caractereIdentifiant(const std::string& s, std::size_t i) {
  if (i >= s.size()) return 0;
  const unsigned char c = static_cast<unsigned char>(s[i]);
  if (std::isalnum(c) || c == '_') return 1;
  if ((c == 0xC3 || c == 0xC5) && i + 1 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) return 2;
  return 0;
}

std::size_t codepoints(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char c : s) n += (c & 0xC0) != 0x80;
  return n;
}

// Largeur d'un texte en millimètres, à la hauteur de police donnée (moyenne des polices sans empattement)
double largeurTexte(const std::string& s, double taille) { return 0.56 * taille * static_cast<double>(codepoints(s)); }

std::string duree(double secondes) {
  if (secondes > 0 && secondes < 1) return detail::nombreFr(std::round(secondes * 1000)) + "ms";
  if (secondes >= 60 && std::fmod(secondes, 60) < 1e-9) return detail::nombreFr(secondes / 60) + "min";
  return detail::nombreFr(secondes) + "s";
}

// ---------- Lecture des réceptivités ----------
class Lecteur {
 public:
  explicit Lecteur(const std::string& s) : m_s(s) {}

  LectureReceptivite lire() {
    LectureReceptivite r;
    blancs();
    if (m_i >= m_s.size()) {
      r.erreur = "réceptivité vide : écris « 1 » pour une transition toujours franchissable";
      return r;
    }
    std::optional<Receptivite> e = ou();
    if (!e && m_erreur.empty()) erreur("expression illisible");
    blancs();
    if (e && m_erreur.empty() && m_i < m_s.size()) erreur("opérateur attendu (« · » pour ET, « + » pour OU)");
    if (!m_erreur.empty()) {
      r.erreur = m_erreur;
      r.position = m_position;
      return r;
    }
    r.expression = std::move(e);
    return r;
  }

 private:
  void blancs() {
    while (m_i < m_s.size() && (m_s[m_i] == ' ' || m_s[m_i] == '\t')) ++m_i;
  }
  bool symbole(const char* p) {
    blancs();
    const std::size_t n = std::strlen(p);
    if (m_s.compare(m_i, n, p) != 0) return false;
    m_i += n;
    return true;
  }
  // Mot-clé (« et », « ou », « non ») entouré de séparateurs
  bool mot(const char* m) {
    blancs();
    const std::size_t n = std::strlen(m);
    if (m_i + n > m_s.size() || minuscules(m_s.substr(m_i, n)) != m) return false;
    if (caractereIdentifiant(m_s, m_i + n) > 0) return false;
    m_i += n;
    return true;
  }
  std::optional<Receptivite> erreur(const std::string& message) {
    if (m_erreur.empty()) {
      m_erreur = message;
      m_position = codepoints(m_s.substr(0, std::min(m_i, m_s.size())));
    }
    return std::nullopt;
  }
  std::optional<Receptivite> ou() {
    std::optional<Receptivite> g = et();
    if (!g) return g;
    std::vector<Receptivite> termes{*g};
    while (symbole("+") || symbole("|") || mot("ou")) {
      std::optional<Receptivite> d = et();
      if (!d) return d;
      termes.push_back(*d);
    }
    if (termes.size() == 1) return termes.front();
    Receptivite r;
    r.type = Receptivite::Type::Ou;
    r.enfants = std::move(termes);
    return r;
  }
  std::optional<Receptivite> et() {
    std::optional<Receptivite> g = non();
    if (!g) return g;
    std::vector<Receptivite> facteurs{*g};
    for (;;) {
      blancs();
      // « . » d'un nombre décimal n'arrive pas ici : les nombres sont lus d'un bloc dans atome()
      if (!(symbole(ET_POINT) || symbole(ET_PUCE) || symbole(".") || symbole("*") || symbole("&") || mot("et"))) break;
      std::optional<Receptivite> d = non();
      if (!d) return d;
      facteurs.push_back(*d);
    }
    if (facteurs.size() == 1) return facteurs.front();
    Receptivite r;
    r.type = Receptivite::Type::Et;
    r.enfants = std::move(facteurs);
    return r;
  }
  std::optional<Receptivite> non() {
    if (symbole("/") || symbole("!") || symbole(NON_SIGNE) || mot("non")) {
      std::optional<Receptivite> x = non();
      if (!x) return x;
      Receptivite r;
      r.type = Receptivite::Type::Non;
      r.enfants.push_back(*x);
      return r;
    }
    return front();
  }
  std::optional<Receptivite> front() {
    const bool montant = symbole(MONTANT) || symbole("^");
    const bool descendant = !montant && symbole(DESCENDANT);
    if (!montant && !descendant) return atome();
    blancs();
    std::string nom;
    while (const std::size_t k = caractereIdentifiant(m_s, m_i)) {
      nom += m_s.substr(m_i, k);
      m_i += k;
    }
    if (nom.empty()) return erreur(std::string("variable attendue après ") + (montant ? "↑" : "↓") + " : un front se prend sur une entrée (↑S1) ou une étape (↑X3)");
    Receptivite r;
    r.type = montant ? Receptivite::Type::FrontMontant : Receptivite::Type::FrontDescendant;
    r.nom = nom;
    r.etape = numeroEtape(nom);
    return r;
  }
  static int numeroEtape(const std::string& nom) {
    if (nom.size() < 2 || (nom[0] != 'X' && nom[0] != 'x')) return -1;
    for (std::size_t k = 1; k < nom.size(); ++k)
      if (!std::isdigit(static_cast<unsigned char>(nom[k]))) return -1;
    return std::stoi(nom.substr(1, 6));
  }
  // Durée « 5s », « 500ms », « 2min », « 1,5s »
  bool lireDuree(double& secondes) {
    blancs();
    const std::size_t debut = m_i;
    while (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) || ((m_s[m_i] == '.' || m_s[m_i] == ',') && m_i + 1 < m_s.size() &&
                                                                                       std::isdigit(static_cast<unsigned char>(m_s[m_i + 1])))))
      ++m_i;
    if (m_i == debut) return false;
    std::string n = m_s.substr(debut, m_i - debut);
    std::replace(n.begin(), n.end(), ',', '.');
    const double v = std::strtod(n.c_str(), nullptr);
    std::string unite;
    while (m_i < m_s.size() && std::isalpha(static_cast<unsigned char>(m_s[m_i]))) unite += m_s[m_i++];
    unite = minuscules(unite);
    if (unite == "ms") secondes = v / 1000;
    else if (unite == "s") secondes = v;
    else if (unite == "min" || unite == "mn") secondes = v * 60;
    else if (unite == "h") secondes = v * 3600;
    else {
      m_i = debut + n.size();
      erreur("unité de temps attendue après " + n + " : ms, s, min ou h (« 5s/X3 »)");
      return false;
    }
    return true;
  }
  std::optional<Receptivite> etapeApresBarre(Receptivite r) {
    // « /X3 » d'une temporisation
    if (!symbole("/")) return erreur("« /X » et le numéro de l'étape attendus : « 5s/X3 »");
    blancs();
    if (m_i >= m_s.size() || (m_s[m_i] != 'X' && m_s[m_i] != 'x')) return erreur("« X » et le numéro de l'étape attendus après « / » : « 5s/X3 »");
    ++m_i;
    const std::size_t debut = m_i;
    while (m_i < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_i]))) ++m_i;
    if (m_i == debut) return erreur("numéro d'étape attendu après « X »");
    r.etape = std::stoi(m_s.substr(debut, std::min<std::size_t>(6, m_i - debut)));
    return r;
  }
  std::optional<Receptivite> atome() {
    blancs();
    if (m_i >= m_s.size()) return erreur("expression incomplète : une variable, « 1 » ou une parenthèse attendue");
    if (symbole("(")) {
      std::optional<Receptivite> e = ou();
      if (!e) return e;
      if (!symbole(")")) return erreur("parenthèse fermante « ) » manquante");
      return e;
    }
    if (symbole("=")) {
      blancs();
      if (m_i < m_s.size() && m_s[m_i] == '1') {
        ++m_i;
        return Receptivite{};
      }
      return erreur("« =1 » attendu (transition toujours franchissable)");
    }
    const unsigned char c = static_cast<unsigned char>(m_s[m_i]);
    if (std::isdigit(c)) {
      // Constante 0 ou 1, ou temporisation « 5s/X3 »
      std::size_t j = m_i;
      while (j < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[j]))) ++j;
      const bool seul = j >= m_s.size() || (caractereIdentifiant(m_s, j) == 0 && m_s[j] != '.' && m_s[j] != ',' && m_s[j] != '/');
      if (seul && j == m_i + 1 && (c == '0' || c == '1')) {
        ++m_i;
        Receptivite r;
        r.type = c == '1' ? Receptivite::Type::Vrai : Receptivite::Type::Faux;
        return r;
      }
      Receptivite r;
      r.type = Receptivite::Type::Tempo;
      if (!lireDuree(r.duree)) return m_erreur.empty() ? erreur("durée attendue : « 5s/X3 »") : std::nullopt;
      return etapeApresBarre(r);
    }
    std::string nom;
    while (const std::size_t k = caractereIdentifiant(m_s, m_i)) {
      nom += m_s.substr(m_i, k);
      m_i += k;
    }
    if (nom.empty()) return erreur("variable attendue ici");
    // Ancienne écriture « t/X3/5s »
    if (minuscules(nom) == "t" && m_i + 1 < m_s.size() && m_s[m_i] == '/' && (m_s[m_i + 1] == 'X' || m_s[m_i + 1] == 'x')) {
      Receptivite r;
      r.type = Receptivite::Type::Tempo;
      std::optional<Receptivite> x = etapeApresBarre(r);
      if (!x) return x;
      if (!symbole("/")) return erreur("« /durée » attendu : « t/X3/5s »");
      if (!lireDuree(x->duree)) return m_erreur.empty() ? erreur("durée attendue : « t/X3/5s »") : std::nullopt;
      return x;
    }
    Receptivite r;
    r.nom = nom;
    r.etape = numeroEtape(nom);
    r.type = r.etape >= 0 ? Receptivite::Type::Etape : Receptivite::Type::Variable;
    return r;
  }

  const std::string& m_s;
  std::size_t m_i = 0, m_position = 0;
  std::string m_erreur;
};

bool estAtome(const Receptivite& r) { return r.type != Receptivite::Type::Et && r.type != Receptivite::Type::Ou; }

// Évaluation avec une valeur pour chaque feuille (variable, étape, front, temporisation)
bool evaluer(const Receptivite& r, const std::function<bool(const Receptivite&)>& feuille) {
  switch (r.type) {
    case Receptivite::Type::Vrai: return true;
    case Receptivite::Type::Faux: return false;
    case Receptivite::Type::Non: return !evaluer(r.enfants.front(), feuille);
    case Receptivite::Type::Et:
      for (const auto& x : r.enfants)
        if (!evaluer(x, feuille)) return false;
      return true;
    case Receptivite::Type::Ou:
      for (const auto& x : r.enfants)
        if (evaluer(x, feuille)) return true;
      return false;
    default: return feuille(r);
  }
}

// Clé d'une feuille, pour énumérer ses valeurs : « a », « ↑a », « X3 », « 5s/X3 »
std::string cleFeuille(const Receptivite& r) {
  switch (r.type) {
    case Receptivite::Type::Variable: return r.nom;
    case Receptivite::Type::Etape: return "X" + std::to_string(r.etape);
    case Receptivite::Type::FrontMontant: return "↑" + r.nom;
    case Receptivite::Type::FrontDescendant: return "↓" + r.nom;
    case Receptivite::Type::Tempo: return duree(r.duree) + "/X" + std::to_string(r.etape);
    default: return {};
  }
}

void feuilles(const Receptivite& r, std::vector<std::string>& v) {
  if (r.type == Receptivite::Type::Et || r.type == Receptivite::Type::Ou || r.type == Receptivite::Type::Non) {
    for (const auto& x : r.enfants) feuilles(x, v);
    return;
  }
  const std::string k = cleFeuille(r);
  if (!k.empty() && std::find(v.begin(), v.end(), k) == v.end()) v.push_back(k);
}

void etapesCitees(const Receptivite& r, std::set<int>& v) {
  if (r.etape >= 0) v.insert(r.etape);
  for (const auto& x : r.enfants) etapesCitees(x, v);
}

std::string listeEtapes(const std::vector<int>& v) {
  std::string t;
  for (int x : v) t += (t.empty() ? "" : ", ") + std::to_string(x);
  return t;
}

AvisCalcul avis(NiveauAvis n, const std::string& regle, const std::string& message) { return {n, regle, message}; }

}  // namespace

// ---------- Modèle ----------
const EtapeGrafcet* Grafcet::etape(int numero) const {
  for (const auto& e : etapes)
    if (e.numero == numero) return &e;
  return nullptr;
}

EtapeGrafcet* Grafcet::etape(int numero) {
  for (auto& e : etapes)
    if (e.numero == numero) return &e;
  return nullptr;
}

int Grafcet::numeroLibre() const {
  int n = 0;
  while (etape(n)) ++n;
  return n;
}

// ---------- Actions ----------
bool lireAction(const std::string& texte, ActionGrafcet& a, std::string* erreur) {
  auto echec = [&](const std::string& m) {
    if (erreur) *erreur = m;
    return false;
  };
  std::string t = sansBlancs(texte);
  if (t.empty()) return echec("action vide");
  a = ActionGrafcet{};
  bool fleche = false;
  if (t.rfind(DESCENDANT, 0) == 0) {
    a.type = ActionGrafcet::Type::Desactivation;
    t = sansBlancs(t.substr(3));
    fleche = true;
  } else if (t.rfind(MONTANT, 0) == 0) {
    a.type = ActionGrafcet::Type::Activation;
    t = sansBlancs(t.substr(3));
    fleche = true;
  }
  // « S KM1 », « R KM1 » : mise à 1, à 0, à l'activation
  if (t.size() > 2 && (t[0] == 'S' || t[0] == 'R') && t[1] == ' ') {
    if (!fleche) a.type = ActionGrafcet::Type::Activation;
    a.valeur = t[0] == 'S';
    t = sansBlancs(t.substr(2));
  } else if (const std::size_t k = t.find(":="); k != std::string::npos) {
    const std::string v = sansBlancs(t.substr(k + 2));
    if (v != "1" && v != "0") return echec("une action mémorisée affecte 1 ou 0 : « KM1 := 1 »");
    if (!fleche) a.type = ActionGrafcet::Type::Activation;
    a.valeur = v == "1";
    t = sansBlancs(t.substr(0, k));
  } else if (fleche) {
    return echec("une action à l'activation ou à la désactivation affecte une valeur : « ↓ KM1 := 0 »");
  } else {
    // Action conditionnelle : « KM1 si a·b »
    const std::string bas = minuscules(t);
    if (const std::size_t k = bas.find(" si "); k != std::string::npos) {
      a.condition = sansBlancs(t.substr(k + 4));
      t = sansBlancs(t.substr(0, k));
      const LectureReceptivite l = lireReceptivite(a.condition);
      if (!l.expression) return echec("condition illisible : " + l.erreur);
    }
  }
  if (t.empty()) return echec("nom de la sortie manquant");
  for (const char* interdit : {"+", "(", ")", "=", "/", "\xC2\xB7"})
    if (t.find(interdit) != std::string::npos) return echec(std::string("la sortie « ") + t + " » contient « " + interdit + " » : un nom simple (KM1, Avance)");
  a.sortie = t;
  return true;
}

std::string texteAction(const ActionGrafcet& a) {
  switch (a.type) {
    case ActionGrafcet::Type::Continue: return a.sortie + (a.condition.empty() ? std::string() : " si " + a.condition);
    case ActionGrafcet::Type::Activation: return a.sortie + " := " + (a.valeur ? "1" : "0");
    case ActionGrafcet::Type::Desactivation: return std::string(DESCENDANT) + " " + a.sortie + " := " + (a.valeur ? "1" : "0");
  }
  return a.sortie;
}

// ---------- Réceptivités ----------
LectureReceptivite lireReceptivite(const std::string& texte) { return Lecteur(texte).lire(); }

std::string texteReceptivite(const Receptivite& r) {
  switch (r.type) {
    case Receptivite::Type::Vrai: return "1";
    case Receptivite::Type::Faux: return "0";
    case Receptivite::Type::Variable: return r.nom;
    case Receptivite::Type::Etape: return "X" + std::to_string(r.etape);
    case Receptivite::Type::FrontMontant: return std::string(MONTANT) + r.nom;
    case Receptivite::Type::FrontDescendant: return std::string(DESCENDANT) + r.nom;
    case Receptivite::Type::Tempo: return duree(r.duree) + "/X" + std::to_string(r.etape);
    case Receptivite::Type::Non: {
      const Receptivite& x = r.enfants.front();
      return "/" + (estAtome(x) ? texteReceptivite(x) : "(" + texteReceptivite(x) + ")");
    }
    case Receptivite::Type::Et: {
      std::string t;
      for (const auto& x : r.enfants) {
        const std::string u = x.type == Receptivite::Type::Ou ? "(" + texteReceptivite(x) + ")" : texteReceptivite(x);
        t += (t.empty() ? "" : std::string(" ") + ET_POINT + " ") + u;
      }
      return t;
    }
    case Receptivite::Type::Ou: {
      std::string t;
      for (const auto& x : r.enfants) t += (t.empty() ? "" : " + ") + texteReceptivite(x);
      return t;
    }
  }
  return {};
}

void variablesReceptivite(const Receptivite& r, std::vector<std::string>& v) {
  if ((r.type == Receptivite::Type::Variable || ((r.type == Receptivite::Type::FrontMontant || r.type == Receptivite::Type::FrontDescendant) && r.etape < 0)) &&
      std::find(v.begin(), v.end(), r.nom) == v.end())
    v.push_back(r.nom);
  for (const auto& x : r.enfants) variablesReceptivite(x, v);
}

std::vector<std::string> entreesGrafcet(const Grafcet& g) {
  std::vector<std::string> v;
  for (const auto& t : g.transitions)
    if (const LectureReceptivite l = lireReceptivite(t.receptivite); l.expression) variablesReceptivite(*l.expression, v);
  for (const auto& e : g.etapes)
    for (const auto& a : e.actions)
      if (!a.condition.empty())
        if (const LectureReceptivite l = lireReceptivite(a.condition); l.expression) variablesReceptivite(*l.expression, v);
  // Une sortie relue dans une réceptivité n'est pas une entrée
  const std::vector<std::string> s = sortiesGrafcet(g);
  v.erase(std::remove_if(v.begin(), v.end(), [&](const std::string& x) { return std::find(s.begin(), s.end(), x) != s.end(); }), v.end());
  return v;
}

std::vector<std::string> sortiesGrafcet(const Grafcet& g) {
  std::vector<std::string> v;
  for (const auto& e : g.etapes)
    for (const auto& a : e.actions)
      if (!a.sortie.empty() && std::find(v.begin(), v.end(), a.sortie) == v.end()) v.push_back(a.sortie);
  return v;
}

// ---------- Vérification ----------
std::vector<AvisCalcul> verifierGrafcet(const Grafcet& g) {
  std::vector<AvisCalcul> v;
  const std::string NORME = "NF EN CEI 60848";
  std::map<int, int> occurrences;
  for (const auto& e : g.etapes) ++occurrences[e.numero];
  for (const auto& [n, k] : occurrences)
    if (k > 1) v.push_back(avis(NiveauAvis::Erreur, NORME, "L'étape " + std::to_string(n) + " existe " + std::to_string(k) + " fois : chaque étape a son numéro."));
  if (g.etapes.empty()) {
    v.push_back(avis(NiveauAvis::Erreur, NORME, "Le grafcet n'a aucune étape : commence par l'étape initiale 0."));
    return v;
  }
  if (std::none_of(g.etapes.begin(), g.etapes.end(), [](const EtapeGrafcet& e) { return e.initiale; }))
    v.push_back(avis(NiveauAvis::Erreur, NORME + " (règle 1)", "Aucune étape initiale : la situation de départ n'est pas définie. Coche « Initiale » sur l'étape de repos."));
  std::vector<std::optional<Receptivite>> lues(g.transitions.size());
  for (std::size_t i = 0; i < g.transitions.size(); ++i) {
    const auto& t = g.transitions[i];
    const std::string nom = "La transition " + std::to_string(i + 1);
    // Alternance étape-transition : chaque transition relie au moins une étape amont et une étape aval
    if (t.amont.empty()) v.push_back(avis(NiveauAvis::Erreur, NORME, nom + " n'a pas d'étape en amont : elle ne peut jamais être validée."));
    if (t.aval.empty()) v.push_back(avis(NiveauAvis::Erreur, NORME, nom + " n'a pas d'étape en aval : la franchir ne mènerait nulle part."));
    for (int n : t.amont)
      if (!g.etape(n)) v.push_back(avis(NiveauAvis::Erreur, NORME, nom + " part de l'étape " + std::to_string(n) + ", qui n'existe pas."));
    for (int n : t.aval)
      if (!g.etape(n)) v.push_back(avis(NiveauAvis::Erreur, NORME, nom + " mène à l'étape " + std::to_string(n) + ", qui n'existe pas."));
    const LectureReceptivite l = lireReceptivite(t.receptivite);
    if (!l.expression) {
      v.push_back(avis(NiveauAvis::Erreur, NORME, nom + " : réceptivité « " + t.receptivite + " » illisible au caractère " + std::to_string(l.position + 1) + " — " + l.erreur + "."));
      continue;
    }
    lues[i] = l.expression;
    std::set<int> citees;
    etapesCitees(*l.expression, citees);
    for (int n : citees)
      if (!g.etape(n)) v.push_back(avis(NiveauAvis::Erreur, NORME, nom + " : sa réceptivité cite l'étape " + std::to_string(n) + ", qui n'existe pas."));
  }
  for (const auto& e : g.etapes)
    for (const auto& a : e.actions)
      if (!a.condition.empty())
        if (const LectureReceptivite l = lireReceptivite(a.condition); !l.expression)
          v.push_back(avis(NiveauAvis::Erreur, NORME, "Étape " + std::to_string(e.numero) + " : condition de l'action " + a.sortie + " illisible — " + l.erreur + "."));
  // Étapes puits et étapes jamais atteintes
  std::map<int, std::vector<std::size_t>> apres;
  for (std::size_t i = 0; i < g.transitions.size(); ++i)
    for (int n : g.transitions[i].amont) apres[n].push_back(i);
  for (const auto& e : g.etapes)
    if (!apres.count(e.numero))
      v.push_back(avis(NiveauAvis::Alerte, NORME, "L'étape " + std::to_string(e.numero) + " n'a pas de transition en aval : une fois active, elle le reste (étape puits)."));
  std::set<int> atteintes;
  std::vector<int> pile;
  for (const auto& e : g.etapes)
    if (e.initiale) pile.push_back(e.numero);
  while (!pile.empty()) {
    const int n = pile.back();
    pile.pop_back();
    if (!atteintes.insert(n).second) continue;
    for (const auto& t : g.transitions)
      if (std::find(t.amont.begin(), t.amont.end(), n) != t.amont.end())
        for (int a : t.aval) pile.push_back(a);
  }
  for (const auto& e : g.etapes)
    if (!atteintes.count(e.numero) && std::any_of(g.etapes.begin(), g.etapes.end(), [](const EtapeGrafcet& x) { return x.initiale; }))
      v.push_back(avis(NiveauAvis::Alerte, NORME, "L'étape " + std::to_string(e.numero) + " n'est jamais atteinte depuis la situation initiale."));
  // Divergence en OU : réceptivités exclusives, sinon les deux branches partent ensemble
  for (const auto& [n, liste] : apres) {
    std::vector<std::size_t> seules;
    for (std::size_t i : liste)
      if (g.transitions[i].amont.size() == 1 && lues[i]) seules.push_back(i);
    for (std::size_t a = 0; a < seules.size(); ++a)
      for (std::size_t b = a + 1; b < seules.size(); ++b) {
        const Receptivite& ra = *lues[seules[a]];
        const Receptivite& rb = *lues[seules[b]];
        std::vector<std::string> f;
        feuilles(ra, f);
        feuilles(rb, f);
        if (f.size() > 14) continue;
        std::string exemple;
        for (std::uint32_t k = 0; k < (1u << f.size()) && exemple.empty(); ++k) {
          auto val = [&](const Receptivite& x) {
            const auto it = std::find(f.begin(), f.end(), cleFeuille(x));
            return it != f.end() && ((k >> (it - f.begin())) & 1u) != 0;
          };
          if (evaluer(ra, val) && evaluer(rb, val)) {
            exemple = "toujours";
            std::string t;
            for (std::size_t j = 0; j < f.size(); ++j) t += (t.empty() ? "" : ", ") + f[j] + " = " + (((k >> j) & 1u) ? "1" : "0");
            if (!t.empty()) exemple = t;
          }
        }
        if (!exemple.empty())
          v.push_back(avis(NiveauAvis::Alerte, NORME + " (divergence en OU)",
                           "Après l'étape " + std::to_string(n) + ", les transitions " + std::to_string(seules[a] + 1) + " et " + std::to_string(seules[b] + 1) +
                               " peuvent être vraies ensemble (" + exemple + ") : les deux branches s'activeraient. Rends les réceptivités exclusives (« a » et « /a »)."));
      }
  }
  // Réceptivité toujours vraie : évolution fugace
  for (std::size_t i = 0; i < g.transitions.size(); ++i)
    if (lues[i] && lues[i]->type == Receptivite::Type::Vrai && g.transitions[i].amont.size() == 1)
      if (const EtapeGrafcet* e = g.etape(g.transitions[i].amont.front()); e && !e->actions.empty() && std::all_of(e->actions.begin(), e->actions.end(), [](const ActionGrafcet& a) {
                                                                             return a.type == ActionGrafcet::Type::Continue;
                                                                           }))
        v.push_back(avis(NiveauAvis::Info, NORME + " (évolution fugace)",
                         "La transition " + std::to_string(i + 1) + " est toujours vraie : l'étape " + std::to_string(e->numero) +
                             " n'est active qu'un instant (évolution fugace), ses actions continues ne sont pas émises. Mémorise-les (« := 1 ») si elles doivent agir."));
  if (std::none_of(v.begin(), v.end(), [](const AvisCalcul& a) { return a.niveau != NiveauAvis::Info; }))
    v.insert(v.begin(), avis(NiveauAvis::Info, NORME, "Grafcet conforme : " + std::to_string(g.etapes.size()) + " étape(s), " + std::to_string(g.transitions.size()) +
                                                          " transition(s), alternance étape-transition respectée."));
  return v;
}

// ---------- Simulation ----------
SimulateurGrafcet::SimulateurGrafcet(Grafcet g) : m_g(std::move(g)) {
  for (const auto& t : m_g.transitions) {
    const LectureReceptivite l = lireReceptivite(t.receptivite);
    m_receptivites.push_back(l.expression);
  }
  for (const auto& e : m_g.etapes)
    for (const auto& a : e.actions) m_conditions[e.numero].push_back(a.condition.empty() ? std::optional<Receptivite>{} : lireReceptivite(a.condition).expression);
  initialiser();
}

void SimulateurGrafcet::initialiser() {
  m_actives.clear();
  m_activation.clear();
  m_memoires.clear();
  m_journal.clear();
  m_temps = 0;
  m_instable = false;
  for (const auto& e : m_g.etapes)
    if (e.initiale) {
      m_actives.insert(e.numero);
      m_activation[e.numero] = 0;
      for (const auto& a : e.actions)
        if (a.type == ActionGrafcet::Type::Activation) m_memoires[a.sortie] = a.valeur;
    }
  m_activesAvant = m_actives;
  m_entreesAvant = m_entrees;
  m_journal.push_back("Situation initiale : " + listeEtapes(std::vector<int>(m_actives.begin(), m_actives.end())) + ".");
  evoluer();
}

void SimulateurGrafcet::fixer(const std::string& entree, bool valeur) { m_entrees[entree] = valeur; }

bool SimulateurGrafcet::entree(const std::string& nom) const {
  const auto it = m_entrees.find(nom);
  return it != m_entrees.end() && it->second;
}

double SimulateurGrafcet::dureeActive(int etape) const {
  const auto it = m_activation.find(etape);
  return it == m_activation.end() || !active(etape) ? 0 : m_temps - it->second;
}

void SimulateurGrafcet::avancer(double secondes) {
  m_temps += std::max(0.0, secondes);
  evoluer();
}

bool SimulateurGrafcet::vraie(const Receptivite& r, bool fronts) const {
  return evaluer(r, [&](const Receptivite& x) {
    switch (x.type) {
      case Receptivite::Type::Variable: {
        // Une sortie relue (« KM1 » dans une réceptivité) vaut son état
        if (!m_entrees.count(x.nom))
          for (const auto& [s, val] : sorties())
            if (s == x.nom) return val;
        return entree(x.nom);
      }
      case Receptivite::Type::Etape: return active(x.etape);
      case Receptivite::Type::Tempo: return active(x.etape) && dureeActive(x.etape) + 1e-9 >= x.duree;
      case Receptivite::Type::FrontMontant:
      case Receptivite::Type::FrontDescendant: {
        bool maintenant = false, avant = false;
        if (x.etape >= 0) {
          maintenant = active(x.etape);
          avant = m_activesAvant.count(x.etape) > 0;
        } else {
          if (!fronts) return false;  // l'événement d'entrée n'est pris en compte qu'une fois
          maintenant = entree(x.nom);
          const auto it = m_entreesAvant.find(x.nom);
          avant = it != m_entreesAvant.end() && it->second;
        }
        return x.type == Receptivite::Type::FrontMontant ? maintenant && !avant : !maintenant && avant;
      }
      default: return false;
    }
  });
}

std::vector<std::size_t> SimulateurGrafcet::validees() const {
  std::vector<std::size_t> v;
  for (std::size_t i = 0; i < m_g.transitions.size(); ++i) {
    const auto& t = m_g.transitions[i];
    if (!t.amont.empty() && std::all_of(t.amont.begin(), t.amont.end(), [&](int n) { return active(n); })) v.push_back(i);
  }
  return v;
}

std::vector<std::size_t> SimulateurGrafcet::franchissables() const {
  std::vector<std::size_t> v;
  for (std::size_t i : validees())
    if (m_receptivites[i] && vraie(*m_receptivites[i], false)) v.push_back(i);
  return v;
}

int SimulateurGrafcet::evoluer() {
  int franchies = 0;
  m_instable = false;
  bool fronts = true;
  m_activesAvant = m_actives;  // pas d'événement d'étape en attente au départ d'une évolution
  for (int cycle = 0; cycle < 100; ++cycle) {
    std::vector<std::size_t> f;
    for (std::size_t i : validees())
      if (m_receptivites[i] && vraie(*m_receptivites[i], fronts)) f.push_back(i);
    if (f.empty()) {
      m_entreesAvant = m_entrees;
      m_activesAvant = m_actives;
      return franchies;
    }
    // Règles 3, 4 et 5 : tout ce qui est franchissable l'est ensemble ; activée et désactivée, une étape reste active
    std::set<int> desactiver, activer;
    for (std::size_t i : f) {
      for (int n : m_g.transitions[i].amont) desactiver.insert(n);
      for (int n : m_g.transitions[i].aval)
        if (m_g.etape(n)) activer.insert(n);
    }
    std::set<int> nouvelles = m_actives;
    for (int n : desactiver)
      if (!activer.count(n)) {
        nouvelles.erase(n);
        m_activation.erase(n);
        if (const EtapeGrafcet* e = m_g.etape(n))
          for (const auto& a : e->actions)
            if (a.type == ActionGrafcet::Type::Desactivation) m_memoires[a.sortie] = a.valeur;
      }
    for (int n : activer)
      if (!m_actives.count(n)) {
        nouvelles.insert(n);
        m_activation[n] = m_temps;
        if (const EtapeGrafcet* e = m_g.etape(n))
          for (const auto& a : e->actions)
            if (a.type == ActionGrafcet::Type::Activation) m_memoires[a.sortie] = a.valeur;
      }
    std::string t;
    for (std::size_t i : f) t += (t.empty() ? "" : ", ") + std::to_string(i + 1);
    m_journal.push_back("t = " + detail::nombreFr(m_temps, 1) + " s : transition(s) " + t + " franchie(s) → étapes actives " +
                        listeEtapes(std::vector<int>(nouvelles.begin(), nouvelles.end())) + ".");
    if (m_journal.size() > 200) m_journal.erase(m_journal.begin(), m_journal.begin() + 50);
    const bool inchangee = nouvelles == m_actives;
    m_activesAvant = m_actives;
    m_actives = std::move(nouvelles);
    franchies += static_cast<int>(f.size());
    // Franchir sans changer la situation (une étape qui se réactive elle-même) : la situation est stable
    if (inchangee) {
      m_entreesAvant = m_entrees;
      m_activesAvant = m_actives;
      return franchies;
    }
    // Les événements d'entrée sont consommés par le premier franchissement
    fronts = false;
    m_entreesAvant = m_entrees;
  }
  m_instable = true;
  m_journal.push_back("Évolution sans fin : des réceptivités toujours vraies forment un cycle (grafcet instable).");
  return franchies;
}

std::map<std::string, bool> SimulateurGrafcet::sorties() const {
  std::map<std::string, bool> s;
  for (const auto& e : m_g.etapes)
    for (const auto& a : e.actions) s.emplace(a.sortie, false);
  for (const auto& [nom, val] : m_memoires)
    if (val) s[nom] = true;
  for (const auto& e : m_g.etapes) {
    if (!active(e.numero)) continue;
    const auto it = m_conditions.find(e.numero);
    for (std::size_t k = 0; k < e.actions.size(); ++k) {
      const ActionGrafcet& a = e.actions[k];
      if (a.type != ActionGrafcet::Type::Continue) continue;
      bool ok = true;
      if (!a.condition.empty()) {
        const std::optional<Receptivite>& c = it == m_conditions.end() || k >= it->second.size() ? std::optional<Receptivite>{} : it->second[k];
        // Condition lue sur les entrées et les étapes (pas sur les sorties : pas de boucle)
        ok = c && evaluer(*c, [&](const Receptivite& x) {
               switch (x.type) {
                 case Receptivite::Type::Variable: return entree(x.nom);
                 case Receptivite::Type::Etape: return active(x.etape);
                 case Receptivite::Type::Tempo: return active(x.etape) && dureeActive(x.etape) + 1e-9 >= x.duree;
                 default: return false;
               }
             });
      }
      if (ok) s[a.sortie] = true;
    }
  }
  return s;
}

// ---------- Dessin ----------
namespace {
// Ce qui s'écrit dans le rectangle d'une action : la sortie, ou l'affectation mémorisée avec sa flèche
std::string libelleAction(const ActionGrafcet& a) {
  if (a.type == ActionGrafcet::Type::Continue) return a.sortie;
  if (a.type == ActionGrafcet::Type::Activation) return std::string(MONTANT) + " " + texteAction(a);
  return texteAction(a);
}
}  // namespace

DispositionGrafcet disposerGrafcet(const Grafcet& g) {
  DispositionGrafcet d;
  const std::size_t n = g.etapes.size(), m = g.transitions.size();
  if (n == 0) return d;
  std::map<int, std::size_t> indice;
  for (std::size_t i = 0; i < n; ++i) indice.emplace(g.etapes[i].numero, i);
  std::vector<std::vector<std::size_t>> avalTr(n), amontTr(n);
  for (std::size_t t = 0; t < m; ++t) {
    for (int a : g.transitions[t].amont)
      if (indice.count(a)) avalTr[indice[a]].push_back(t);
    for (int b : g.transitions[t].aval)
      if (indice.count(b)) amontTr[indice[b]].push_back(t);
  }
  // Racines : étapes initiales (par numéro), puis étapes sans amont, puis le reste
  std::vector<std::size_t> racines;
  {
    std::vector<std::size_t> ordre(n);
    for (std::size_t i = 0; i < n; ++i) ordre[i] = i;
    std::stable_sort(ordre.begin(), ordre.end(), [&](std::size_t a, std::size_t b) {
      const auto ka = std::make_tuple(!g.etapes[a].initiale, !amontTr[a].empty(), g.etapes[a].numero);
      const auto kb = std::make_tuple(!g.etapes[b].initiale, !amontTr[b].empty(), g.etapes[b].numero);
      return ka < kb;
    });
    racines = ordre;
  }
  // Retours (liaisons qui remontent) : repérés par un parcours en profondeur
  std::vector<int> etat(n, 0);
  std::set<std::pair<std::size_t, std::size_t>> retours;  // (transition, étape aval)
  std::function<void(std::size_t)> parcourir = [&](std::size_t s) {
    etat[s] = 1;
    for (std::size_t t : avalTr[s])
      for (int b : g.transitions[t].aval) {
        if (!indice.count(b)) continue;
        const std::size_t dd = indice[b];
        if (etat[dd] == 1) retours.insert({t, dd});
        else if (etat[dd] == 0) parcourir(dd);
      }
    etat[s] = 2;
  };
  for (std::size_t r : racines)
    if (etat[r] == 0) parcourir(r);
  // Rangs : plus long chemin dans le graphe sans les retours
  std::vector<int> rang(n, 0);
  {
    std::vector<int> entrants(n, 0);
    std::vector<std::vector<std::size_t>> succ(n);
    for (std::size_t t = 0; t < m; ++t)
      for (int a : g.transitions[t].amont)
        for (int b : g.transitions[t].aval) {
          if (!indice.count(a) || !indice.count(b) || retours.count({t, indice[b]})) continue;
          succ[indice[a]].push_back(indice[b]);
          ++entrants[indice[b]];
        }
    std::vector<std::size_t> file;
    for (std::size_t i = 0; i < n; ++i)
      if (entrants[i] == 0) file.push_back(i);
    for (std::size_t k = 0; k < file.size(); ++k)
      for (std::size_t s2 : succ[file[k]]) {
        rang[s2] = std::max(rang[s2], rang[file[k]] + 1);
        if (--entrants[s2] == 0) file.push_back(s2);
      }
  }
  std::vector<int> rangT(m, 0);
  for (std::size_t t = 0; t < m; ++t)
    for (int a : g.transitions[t].amont)
      if (indice.count(a)) rangT[t] = std::max(rangT[t], rang[indice[a]]);
  auto retour = [&](std::size_t t, std::size_t s) { return retours.count({t, s}) > 0; };
  // Colonnes : la première branche continue celle de son étape, les autres en ouvrent une à droite
  std::vector<int> colE(n, -1), colT(m, -1);
  int prochaine = 0;
  std::function<void(std::size_t)> placer = [&](std::size_t s) {
    bool premiere = true;
    for (std::size_t t : avalTr[s]) {
      if (colT[t] < 0) colT[t] = premiere ? colE[s] : prochaine++;
      premiere = false;
      bool premierAval = true;
      for (int b : g.transitions[t].aval) {
        if (!indice.count(b)) continue;
        const std::size_t dd = indice[b];
        if (retour(t, dd)) continue;
        if (colE[dd] < 0) {
          colE[dd] = premierAval ? colT[t] : prochaine++;
          placer(dd);
        }
        premierAval = false;
      }
    }
  };
  for (std::size_t r : racines)
    if (colE[r] < 0) {
      colE[r] = prochaine++;
      placer(r);
    }
  for (std::size_t t = 0; t < m; ++t)
    if (colT[t] < 0) colT[t] = prochaine++;
  const int colonnes = std::max(1, prochaine);
  // Largeur de chaque colonne : ce qui s'écrit à droite des étapes (actions) et des transitions (réceptivités)
  constexpr double TAILLE = 2.5, CARRE = 10, PAS = 26, HAUT = 8;
  auto largeurAction = [&](const ActionGrafcet& a) { return std::max(14.0, largeurTexte(libelleAction(a), TAILLE) + 4); };
  std::vector<double> droite(static_cast<std::size_t>(colonnes), 5);
  for (std::size_t i = 0; i < n; ++i) {
    double w = CARRE / 2;
    if (!g.etapes[i].actions.empty()) {
      w += 4;
      for (const auto& a : g.etapes[i].actions) w += largeurAction(a);
    }
    if (!g.etapes[i].commentaire.empty()) w += 3 + largeurTexte("« " + g.etapes[i].commentaire + " »", 2.2);
    droite[static_cast<std::size_t>(colE[i])] = std::max(droite[static_cast<std::size_t>(colE[i])], w);
  }
  for (std::size_t t = 0; t < m; ++t)
    droite[static_cast<std::size_t>(colT[t])] = std::max(droite[static_cast<std::size_t>(colT[t])], 6 + largeurTexte(g.transitions[t].receptivite, TAILLE));
  std::vector<std::pair<std::size_t, std::size_t>> listeRetours(retours.begin(), retours.end());
  // Couloirs des retours : les plus courts au plus près du dessin
  std::stable_sort(listeRetours.begin(), listeRetours.end(), [&](const auto& a, const auto& b) {
    return rangT[a.first] - rang[a.second] < rangT[b.first] - rang[b.second];
  });
  const double couloirs = 5.0 * static_cast<double>(listeRetours.size());
  std::vector<double> xc(static_cast<std::size_t>(colonnes));
  xc[0] = 8 + couloirs + 11;
  for (int c = 1; c < colonnes; ++c) xc[static_cast<std::size_t>(c)] = xc[static_cast<std::size_t>(c - 1)] + std::max(28.0, droite[static_cast<std::size_t>(c - 1)] + 16);
  auto X = [&](int col) { return xc[static_cast<std::size_t>(col)]; };
  auto Y = [&](int r) { return HAUT + PAS * r; };  // haut du carré d'une étape de ce rang
  auto YT = [&](std::size_t t) { return Y(rangT[t]) + 16; };
  auto trait = [&](double x0, double y0, double x1, double y1, bool fleche = false) {
    if (std::abs(x0 - x1) < 1e-9 && std::abs(y0 - y1) < 1e-9) return;
    d.traits.push_back({x0, y0, x1, y1, false, fleche, -1});
  };
  auto doubleTrait = [&](double xa, double xb, double y) {
    trait(std::min(xa, xb) - 2, y, std::max(xa, xb) + 2, y);
    trait(std::min(xa, xb) - 2, y + 1, std::max(xa, xb) + 2, y + 1);
  };
  // Étapes, actions, commentaires
  for (std::size_t i = 0; i < n; ++i) {
    const EtapeGrafcet& e = g.etapes[i];
    const double x = X(colE[i]), y = Y(rang[i]);
    d.boites.push_back({x - CARRE / 2, y, CARRE, CARRE, e.initiale, e.numero});
    d.textes.push_back({x, y + CARRE / 2, 3.5, std::to_string(e.numero), 0, false});
    d.zones.push_back({e.numero, -1, x - CARRE / 2, y, x + CARRE / 2, y + CARRE});
    double xa = x + CARRE / 2;
    if (!e.actions.empty()) {
      trait(xa, y + CARRE / 2, xa + 4, y + CARRE / 2);
      xa += 4;
      for (const auto& a : e.actions) {
        const double w = largeurAction(a);
        d.boites.push_back({xa, y + 1.5, w, 7, false, -1});
        d.textes.push_back({xa + w / 2, y + CARRE / 2, TAILLE, libelleAction(a), 0, false});
        if (!a.condition.empty()) {
          // Condition au-dessus de l'action, sur un petit trait vertical
          trait(xa + w / 2, y + 1.5, xa + w / 2, y - 1.5);
          d.textes.push_back({xa + w / 2 + 1, y - 2.6, 2.2, a.condition, -1, false});
        }
        d.zones.push_back({e.numero, -1, xa, y + 1.5, xa + w, y + 8.5});
        xa += w;
      }
    }
    if (!e.commentaire.empty()) d.textes.push_back({xa + 3, y + CARRE / 2, 2.2, "« " + e.commentaire + " »", -1, true});
  }
  // Transitions et liaisons
  std::set<std::size_t> divergenceTracee, convergenceTracee;
  for (std::size_t t = 0; t < m; ++t) {
    const TransitionGrafcet& tr = g.transitions[t];
    const double xt = X(colT[t]), yt = YT(t);
    d.traits.push_back({xt - 4, yt, xt + 4, yt, true, false, static_cast<int>(t)});
    d.textes.push_back({xt + 6, yt, TAILLE, tr.receptivite, -1, false});
    d.textes.push_back({xt - 5.5, yt, 2.2, "(" + std::to_string(t + 1) + ")", 1, true});
    d.zones.push_back({-1, static_cast<int>(t), xt - 5, yt - 2.5, xt + 6 + largeurTexte(tr.receptivite, TAILLE), yt + 2.5});
    // Amont
    std::vector<std::size_t> U;
    for (int a : tr.amont)
      if (indice.count(a)) U.push_back(indice[a]);
    if (U.empty()) trait(xt, yt - 4, xt, yt);
    else if (U.size() == 1) {
      const std::size_t s = U.front();
      const double xs = X(colE[s]), ys = Y(rang[s]) + CARRE;
      if (avalTr[s].size() > 1) {
        // Divergence en OU : trait simple sous l'étape, une branche par transition
        const double yh = ys + 3;
        if (divergenceTracee.insert(s).second) {
          double x0 = xs, x1 = xs;
          for (std::size_t u : avalTr[s]) {
            x0 = std::min(x0, X(colT[u]));
            x1 = std::max(x1, X(colT[u]));
          }
          trait(xs, ys, xs, yh);
          trait(x0, yh, x1, yh);
        }
        trait(xt, yh, xt, yt);
      } else if (std::abs(xs - xt) < 1e-9) {
        trait(xs, ys, xt, yt);
      } else {
        trait(xs, ys, xs, yt - 3);
        trait(xs, yt - 3, xt, yt - 3);
        trait(xt, yt - 3, xt, yt);
      }
    } else {
      // Convergence en ET : double trait au-dessus de la transition
      const double yd = yt - 4.5;
      double x0 = xt, x1 = xt;
      for (std::size_t s : U) {
        x0 = std::min(x0, X(colE[s]));
        x1 = std::max(x1, X(colE[s]));
        trait(X(colE[s]), Y(rang[s]) + CARRE, X(colE[s]), yd);
      }
      doubleTrait(x0, x1, yd);
      trait(xt, yd + 1, xt, yt);
    }
    // Aval
    std::vector<std::size_t> D, Dr;
    for (int b : tr.aval)
      if (indice.count(b)) (retour(t, indice[b]) ? Dr : D).push_back(indice[b]);
    if (D.size() == 1) {
      const std::size_t s = D.front();
      const double xs = X(colE[s]), ys = Y(rang[s]);
      std::vector<std::size_t> entrees;
      for (std::size_t u : amontTr[s])
        if (!retour(u, s)) entrees.push_back(u);
      if (entrees.size() > 1) {
        // Convergence en OU : les branches se rejoignent sur un trait simple au-dessus de l'étape
        const double yh = ys - 3;
        trait(xt, yt, xt, yh);
        if (convergenceTracee.insert(s).second) {
          double x0 = xs, x1 = xs;
          for (std::size_t u : entrees) {
            x0 = std::min(x0, X(colT[u]));
            x1 = std::max(x1, X(colT[u]));
          }
          trait(x0, yh, x1, yh);
          trait(xs, yh, xs, ys);
        }
      } else if (std::abs(xs - xt) < 1e-9) {
        trait(xt, yt, xs, ys);
      } else {
        trait(xt, yt, xt, ys - 3);
        trait(xt, ys - 3, xs, ys - 3);
        trait(xs, ys - 3, xs, ys);
      }
    } else if (D.size() > 1) {
      // Divergence en ET : double trait sous la transition
      const double yd = yt + 3.5;
      double x0 = xt, x1 = xt;
      for (std::size_t s : D) {
        x0 = std::min(x0, X(colE[s]));
        x1 = std::max(x1, X(colE[s]));
      }
      trait(xt, yt, xt, yd);
      doubleTrait(x0, x1, yd);
      for (std::size_t s : D) trait(X(colE[s]), yd + 1, X(colE[s]), Y(rang[s]));
    }
    // Retours : par un couloir à gauche, en remontant (flèche vers le haut)
    for (std::size_t s : Dr) {
      std::size_t k = 0;
      while (k < listeRetours.size() && listeRetours[k] != std::make_pair(t, s)) ++k;
      const double xl = X(0) - 11 - 5.0 * static_cast<double>(k);
      const double y0 = yt + 4, ys = Y(rang[s]), yh = ys - 4;
      trait(xt, yt, xt, y0);
      trait(xt, y0, xl, y0);
      trait(xl, y0, xl, yh, true);
      trait(xl, yh, X(colE[s]), yh);
      trait(X(colE[s]), yh, X(colE[s]), ys);
    }
  }
  // Étendue
  for (const auto& b : d.boites) {
    d.largeur = std::max(d.largeur, b.x + b.l);
    d.hauteur = std::max(d.hauteur, b.y + b.h);
  }
  for (const auto& t : d.traits) {
    d.largeur = std::max({d.largeur, t.x0, t.x1});
    d.hauteur = std::max({d.hauteur, t.y0, t.y1});
  }
  for (const auto& t : d.textes) {
    const double w = largeurTexte(t.texte, t.taille);
    d.largeur = std::max(d.largeur, t.alignement < 0 ? t.x + w : t.alignement == 0 ? t.x + w / 2 : t.x);
    d.hauteur = std::max(d.hauteur, t.y + t.taille);
  }
  d.largeur += 6;
  d.hauteur += 6;
  return d;
}

// ---------- Texte structuré ----------
namespace {

// Identifiant CEI 61131-3 : lettres sans accent, chiffres, « _ » ; ne commence pas par un chiffre
std::string identifiantST(const std::string& t) {
  static const std::map<std::string, char> accents = {{"à", 'a'}, {"â", 'a'}, {"ä", 'a'}, {"ç", 'c'}, {"é", 'e'}, {"è", 'e'}, {"ê", 'e'}, {"ë", 'e'}, {"î", 'i'},
                                                      {"ï", 'i'}, {"ô", 'o'}, {"ö", 'o'}, {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'}, {"ÿ", 'y'}, {"À", 'A'}, {"Â", 'A'},
                                                      {"Ç", 'C'}, {"É", 'E'}, {"È", 'E'}, {"Ê", 'E'}, {"Î", 'I'}, {"Ô", 'O'}, {"Ù", 'U'}, {"Û", 'U'}};
  std::string r;
  for (std::size_t i = 0; i < t.size();) {
    const unsigned char c = static_cast<unsigned char>(t[i]);
    if (std::isalnum(c) || c == '_') {
      r += static_cast<char>(c);
      ++i;
      continue;
    }
    std::size_t k = 1;
    if (c >= 0xC0) k = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    const auto it = accents.find(t.substr(i, k));
    if (it != accents.end()) r += it->second;
    else if (!r.empty() && r.back() != '_') r += '_';
    i += k;
  }
  while (!r.empty() && r.back() == '_') r.pop_back();
  if (r.empty() || std::isdigit(static_cast<unsigned char>(r[0]))) r = "_" + r;
  return r;
}

std::string dureeST(double s) {
  if (s >= 1 && std::fmod(s, 1) < 1e-9) return "T#" + std::to_string(static_cast<long long>(s)) + "S";
  return "T#" + std::to_string(static_cast<long long>(std::llround(s * 1000))) + "MS";
}

std::string nomTempo(const Receptivite& r) { return "TX" + std::to_string(r.etape) + "_" + std::to_string(static_cast<long long>(std::llround(r.duree * 1000))) + "MS"; }

std::string nomFront(const Receptivite& r) {
  return std::string(r.type == Receptivite::Type::FrontMontant ? "R_" : "F_") + (r.etape >= 0 ? "X" + std::to_string(r.etape) : identifiantST(r.nom));
}

std::string exprST(const Receptivite& r) {
  switch (r.type) {
    case Receptivite::Type::Vrai: return "TRUE";
    case Receptivite::Type::Faux: return "FALSE";
    case Receptivite::Type::Variable: return identifiantST(r.nom);
    case Receptivite::Type::Etape: return "X" + std::to_string(r.etape);
    case Receptivite::Type::FrontMontant:
    case Receptivite::Type::FrontDescendant: return nomFront(r) + ".Q";
    case Receptivite::Type::Tempo: return nomTempo(r) + ".Q";
    case Receptivite::Type::Non: return "NOT " + (estAtome(r.enfants.front()) ? exprST(r.enfants.front()) : "(" + exprST(r.enfants.front()) + ")");
    case Receptivite::Type::Et: {
      std::string t;
      for (const auto& x : r.enfants) t += (t.empty() ? "" : " AND ") + (x.type == Receptivite::Type::Ou ? "(" + exprST(x) + ")" : exprST(x));
      return t;
    }
    case Receptivite::Type::Ou: {
      std::string t;
      for (const auto& x : r.enfants) t += (t.empty() ? "" : " OR ") + exprST(x);
      return t;
    }
  }
  return "FALSE";
}

void collecter(const Receptivite& r, std::map<std::string, const Receptivite*>& fronts, std::map<std::string, const Receptivite*>& tempos) {
  if (r.type == Receptivite::Type::FrontMontant || r.type == Receptivite::Type::FrontDescendant) fronts.emplace(nomFront(r), &r);
  if (r.type == Receptivite::Type::Tempo) tempos.emplace(nomTempo(r), &r);
  for (const auto& x : r.enfants) collecter(x, fronts, tempos);
}

}  // namespace

std::string texteStructureGrafcet(const Grafcet& g) {
  std::ostringstream o;
  const std::string nom = identifiantST(g.nom.empty() ? std::string("Grafcet") : g.nom);
  std::vector<LectureReceptivite> lues;
  for (const auto& t : g.transitions) lues.push_back(lireReceptivite(t.receptivite));
  std::vector<std::vector<std::optional<Receptivite>>> conditions;
  for (const auto& e : g.etapes) {
    conditions.emplace_back();
    for (const auto& a : e.actions) conditions.back().push_back(a.condition.empty() ? std::optional<Receptivite>{} : lireReceptivite(a.condition).expression);
  }
  std::map<std::string, const Receptivite*> fronts, tempos;
  for (const auto& l : lues)
    if (l.expression) collecter(*l.expression, fronts, tempos);
  for (const auto& c : conditions)
    for (const auto& x : c)
      if (x) collecter(*x, fronts, tempos);
  const std::vector<std::string> entrees = entreesGrafcet(g), sorties = sortiesGrafcet(g);
  std::set<std::string> memorisees;
  bool activations = false;
  for (const auto& e : g.etapes)
    for (const auto& a : e.actions)
      if (a.type != ActionGrafcet::Type::Continue) {
        memorisees.insert(a.sortie);
        activations = true;
      }
  o << "(* Grafcet " << g.nom << " — NF EN CEI 60848, traduit en texte structuré CEI 61131-3 par Prométhée.\n"
    << "   Un bit par étape ; à chaque cycle : transitions franchissables, puis activation et désactivation des étapes,\n"
    << "   puis sorties. Init (vrai au démarrage) remet la situation initiale. *)\n";
  o << "FUNCTION_BLOCK FB_" << nom << "\n";
  o << "VAR_INPUT\n  Init : BOOL; (* situation initiale *)\n";
  for (const auto& e : entrees) o << "  " << identifiantST(e) << " : BOOL;\n";
  o << "END_VAR\n";
  o << "VAR_OUTPUT\n";
  for (const auto& s : sorties) o << "  " << identifiantST(s) << " : BOOL;\n";
  o << "END_VAR\n";
  o << "VAR\n";
  for (const auto& e : g.etapes) o << "  X" << e.numero << " : BOOL" << (e.initiale ? " := TRUE" : "") << "; (* étape " << e.numero << (e.commentaire.empty() ? "" : " : " + e.commentaire) << " *)\n";
  for (std::size_t t = 0; t < g.transitions.size(); ++t) o << "  Tr" << (t + 1) << " : BOOL; (* transition " << (t + 1) << " franchissable *)\n";
  for (const auto& [n, r] : fronts) o << "  " << n << " : " << (r->type == Receptivite::Type::FrontMontant ? "R_TRIG" : "F_TRIG") << ";\n";
  for (const auto& [n, r] : tempos) o << "  " << n << " : TON; (* " << duree(r->duree) << " après l'activation de l'étape " << r->etape << " *)\n";
  for (const auto& s : memorisees) o << "  M_" << identifiantST(s) << " : BOOL; (* mémoire de " << s << " *)\n";
  if (activations)
    for (const auto& e : g.etapes) o << "  Act" << e.numero << " : R_TRIG; Des" << e.numero << " : F_TRIG; (* activation, désactivation de l'étape " << e.numero << " *)\n";
  o << "END_VAR\n\n";
  if (!fronts.empty() || !tempos.empty()) {
    o << "(* Fronts et temporisations *)\n";
    for (const auto& [n, r] : fronts) o << n << "(CLK := " << (r->etape >= 0 ? "X" + std::to_string(r->etape) : identifiantST(r->nom)) << ");\n";
    for (const auto& [n, r] : tempos) o << n << "(IN := X" << r->etape << ", PT := " << dureeST(r->duree) << ");\n";
    o << "\n";
  }
  o << "(* Transitions franchissables : étapes amont actives et réceptivité vraie *)\n";
  for (std::size_t t = 0; t < g.transitions.size(); ++t) {
    const auto& tr = g.transitions[t];
    std::string amont;
    for (int a : tr.amont) amont += (amont.empty() ? "" : " AND ") + std::string("X") + std::to_string(a);
    if (amont.empty()) amont = "FALSE";
    const std::string rec = lues[t].expression ? exprST(*lues[t].expression) : "FALSE (* réceptivité illisible : " + tr.receptivite + " *)";
    const bool parentheses = lues[t].expression && lues[t].expression->type == Receptivite::Type::Ou;
    o << "Tr" << (t + 1) << " := " << amont << " AND " << (parentheses ? "(" + rec + ")" : rec) << "; (* " << tr.receptivite << " *)\n";
  }
  o << "\n(* Évolution : franchir active l'aval et désactive l'amont ; activée et désactivée à la fois, une étape reste active *)\n";
  o << "IF Init THEN\n";
  for (const auto& e : g.etapes) o << "  X" << e.numero << " := " << (e.initiale ? "TRUE" : "FALSE") << ";\n";
  for (const auto& s : memorisees) o << "  M_" << identifiantST(s) << " := FALSE;\n";
  o << "ELSE\n";
  for (const auto& e : g.etapes) {
    std::string sortant, entrant;
    for (std::size_t t = 0; t < g.transitions.size(); ++t) {
      const auto& tr = g.transitions[t];
      if (std::find(tr.amont.begin(), tr.amont.end(), e.numero) != tr.amont.end()) sortant += (sortant.empty() ? "" : " OR ") + std::string("Tr") + std::to_string(t + 1);
      if (std::find(tr.aval.begin(), tr.aval.end(), e.numero) != tr.aval.end()) entrant += (entrant.empty() ? "" : " OR ") + std::string("Tr") + std::to_string(t + 1);
    }
    o << "  X" << e.numero << " := ";
    if (sortant.empty()) o << "X" << e.numero;
    else o << "(X" << e.numero << " AND NOT (" << sortant << "))";
    if (!entrant.empty()) o << " OR " << entrant;
    o << ";\n";
  }
  o << "END_IF;\n\n";
  if (activations) {
    o << "(* Actions mémorisées : à l'activation, à la désactivation des étapes *)\n";
    for (const auto& e : g.etapes) o << "Act" << e.numero << "(CLK := X" << e.numero << "); Des" << e.numero << "(CLK := X" << e.numero << ");\n";
    for (const auto& e : g.etapes)
      for (const auto& a : e.actions) {
        if (a.type == ActionGrafcet::Type::Continue) continue;
        o << "IF " << (a.type == ActionGrafcet::Type::Activation ? "Act" : "Des") << e.numero << ".Q THEN M_" << identifiantST(a.sortie) << " := " << (a.valeur ? "TRUE" : "FALSE")
          << "; END_IF;\n";
      }
    o << "\n";
  }
  o << "(* Sorties *)\n";
  for (const auto& s : sorties) {
    std::string termes;
    for (std::size_t i = 0; i < g.etapes.size(); ++i) {
      const auto& e = g.etapes[i];
      for (std::size_t k = 0; k < e.actions.size(); ++k) {
        const auto& a = e.actions[k];
        if (a.type != ActionGrafcet::Type::Continue || a.sortie != s) continue;
        std::string terme = "X" + std::to_string(e.numero);
        if (!a.condition.empty()) {
          const auto& c = conditions[i][k];
          terme += " AND " + (c ? (c->type == Receptivite::Type::Ou ? "(" + exprST(*c) + ")" : exprST(*c)) : std::string("FALSE"));
          terme = "(" + terme + ")";
        }
        termes += (termes.empty() ? "" : " OR ") + terme;
      }
    }
    if (memorisees.count(s)) termes += (termes.empty() ? "" : " OR ") + std::string("M_") + identifiantST(s);
    o << identifiantST(s) << " := " << (termes.empty() ? "FALSE" : termes) << ";\n";
  }
  o << "END_FUNCTION_BLOCK\n";
  return o.str();
}

// ---------- Exemples ----------
std::vector<std::pair<std::string, std::string>> exemplesGrafcet() {
  return {{"chariot", "Chariot en aller-retour"}, {"tri", "Tri de pièces (choix en OU)"}, {"poste", "Perçage et taraudage simultanés (ET)"}, {"feux", "Feux tricolores (temporisations)"}};
}

Grafcet exempleGrafcet(const std::string& id) {
  Grafcet g;
  auto etape = [&](int n, bool initiale, std::vector<std::string> actions, const std::string& commentaire = {}) {
    EtapeGrafcet e;
    e.numero = n;
    e.initiale = initiale;
    e.commentaire = commentaire;
    for (const auto& a : actions) {
      ActionGrafcet x;
      if (lireAction(a, x)) e.actions.push_back(x);
    }
    g.etapes.push_back(e);
  };
  auto transition = [&](std::vector<int> amont, std::vector<int> aval, const std::string& r, const std::string& commentaire = {}) {
    g.transitions.push_back({std::move(amont), std::move(aval), r, commentaire});
  };
  if (id == "tri") {
    g.nom = "Tri";
    etape(0, true, {}, "attente");
    etape(1, false, {"KM1"}, "tapis");
    etape(2, false, {"V1"}, "poussoir gauche");
    etape(3, false, {"V2"}, "poussoir droit");
    etape(4, false, {"H1"}, "pièce triée");
    transition({0}, {1}, "dcy · p");
    transition({1}, {2}, "c · g");
    transition({1}, {3}, "c · /g");
    transition({2}, {4}, "s1");
    transition({3}, {4}, "s2");
    transition({4}, {0}, "2s/X4");
  } else if (id == "poste") {
    g.nom = "Poste";
    etape(0, true, {}, "repos");
    etape(1, false, {"KM1"}, "perçage");
    etape(2, false, {}, "attente");
    etape(3, false, {"KM2"}, "taraudage");
    etape(4, false, {}, "attente");
    etape(5, false, {"V1"}, "évacuation");
    transition({0}, {1, 3}, "dcy");
    transition({1}, {2}, "fp");
    transition({3}, {4}, "ft");
    transition({2, 4}, {5}, "1");
    transition({5}, {0}, "fe");
  } else if (id == "feux") {
    g.nom = "Feux";
    etape(0, true, {"VA", "RB"});
    etape(1, false, {"OA", "RB"});
    etape(2, false, {"RA", "VB"});
    etape(3, false, {"RA", "OB"});
    transition({0}, {1}, "20s/X0");
    transition({1}, {2}, "3s/X1");
    transition({2}, {3}, "20s/X2");
    transition({3}, {0}, "3s/X3");
  } else {
    g.nom = "Chariot";
    etape(0, true, {}, "chariot à gauche");
    etape(1, false, {"KM1"}, "avance");
    etape(2, false, {"KM2"}, "retour");
    transition({0}, {1}, "dcy · a");
    transition({1}, {2}, "b");
    transition({2}, {0}, "a");
  }
  return g;
}

}  // namespace prom::elec
