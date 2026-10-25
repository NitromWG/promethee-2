// Prométhée : serveur MCP. Voir assistant_mcp.hpp. Licence GPL-3.0-only.
#include "promethee/assistant_mcp.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <set>

#include "promethee/fichiers.hpp"
#include "promethee/version.hpp"

using J = nlohmann::ordered_json;

namespace prom {

namespace {
std::string maintenant() {
  const std::time_t t = std::time(nullptr);
  char b[32];
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return b;
}
J schema(std::initializer_list<std::pair<const char*, const char*>> proprietes, std::initializer_list<const char*> requis = {}) {
  J p = J::object();
  for (const auto& [n, t] : proprietes) p[n] = {{"type", t}};
  J s = {{"type", "object"}, {"properties", p}};
  if (requis.size()) {
    J r = J::array();
    for (const char* x : requis) r.push_back(x);
    s["required"] = r;
  }
  return s;
}
// Méthodes de l'interface qui modifient le projet : seules elles se proposent
bool methodeQuiModifie(const std::string& m) {
  static const std::set<std::string> lecture = {"version", "methodes", "projet.resume", "conception.composants", "conception.reseaux", "erc", "drc", "revue", "cout",
                                                "simulation.mesurer", "simulation.courbe", "ngspice.commande"};
  return !lecture.count(m);
}
J texteOutil(const std::string& t, bool erreur = false) { return J{{"content", J::array({J{{"type", "text"}, {"text", t}}})}, {"isError", erreur}}; }
}  // namespace

J propositionVersJson(const PropositionAssistant& p) {
  return J{{"numero", p.numero}, {"methode", p.methode}, {"params", p.params}, {"raison", p.raison}, {"auteur", p.auteur},
           {"date", p.date}, {"etat", p.etat}, {"resultat", p.resultat}, {"essai", p.essai}};
}

PropositionAssistant propositionDepuisJson(const J& j) {
  PropositionAssistant p;
  if (!j.is_object()) return p;
  p.numero = j.value("numero", 0);
  p.methode = j.value("methode", std::string());
  if (j.contains("params")) p.params = j["params"];
  p.raison = j.value("raison", std::string());
  p.auteur = j.value("auteur", std::string("assistant"));
  p.date = j.value("date", std::string());
  p.etat = j.value("etat", std::string("en attente"));
  p.resultat = j.value("resultat", std::string());
  p.essai = j.value("essai", std::string());
  return p;
}

std::string cheminPropositions(const std::string& projet) { return projet + ".propositions.json"; }

std::vector<PropositionAssistant> lirePropositions(const std::string& texte) {
  std::vector<PropositionAssistant> v;
  const J j = J::parse(texte, nullptr, false);
  if (j.is_object() && j.contains("propositions") && j["propositions"].is_array())
    for (const auto& x : j["propositions"]) v.push_back(propositionDepuisJson(x));
  return v;
}

std::string textePropositions(const std::vector<PropositionAssistant>& v) {
  J j = {{"format", "promethee-propositions"}, {"version", 1}, {"propositions", J::array()}};
  for (const auto& p : v) j["propositions"].push_back(propositionVersJson(p));
  return j.dump(1) + "\n";
}

ReponseApi accepterProposition(Projet& p, PropositionAssistant& x) {
  ReponseApi r;
  if (x.etat != "en attente") {
    r.reponse = {{"jsonrpc", "2.0"}, {"id", x.numero}, {"error", {{"code", -32020}, {"message", "proposition déjà " + x.etat}}}};
    return r;
  }
  r = traiterRequete(p, J{{"jsonrpc", "2.0"}, {"id", x.numero}, {"method", x.methode}, {"params", x.params}});
  if (r.reponse.contains("error")) {
    x.etat = "refusée";
    x.resultat = "échec : " + r.reponse["error"].value("message", std::string());
  } else {
    x.etat = "acceptée";
    x.resultat = r.nomPas.empty() ? std::string("appliquée") : r.nomPas;
    r.nomPas = "Assistant : " + (x.raison.empty() ? x.resultat : x.raison);
  }
  return r;
}

void refuserProposition(PropositionAssistant& x, const std::string& raison) {
  if (x.etat != "en attente") return;
  x.etat = "refusée";
  x.resultat = raison.empty() ? std::string("refusée par l’utilisateur") : raison;
}

const std::vector<std::pair<std::string, std::string>>& outilsMcp() {
  static const std::vector<std::pair<std::string, std::string>> v = {
      {"lire_projet", "Résumé du projet : nom, conceptions électroniques, nombre de composants, feuilles, pistes, vias, variantes."},
      {"composants", "Composants d'une conception : repère, valeur, empreinte."},
      {"reseaux", "Réseaux d'une conception et leurs broches (repère.numéro)."},
      {"verifier", "ERC, DRC et revue automatique d'une conception, en texte."},
      {"methodes", "Commandes de l'interface que l'on peut proposer, avec leurs paramètres."},
      {"proposer", "Propose une commande (methode, params, raison) : essayée sur une copie, elle attend l'acceptation de l'utilisateur dans l'atelier ; rien n'est modifié tant qu'il ne l'a pas acceptée."},
      {"propositions", "État des propositions faites : en attente, acceptées, refusées."},
  };
  return v;
}

ServeurMcp::ServeurMcp(Projet p) : m_projet(std::move(p)) {}

J ServeurMcp::appelerOutil(const std::string& nom, const J& a, bool& erreur) {
  erreur = false;
  auto lecture = [&](const std::string& methode, J params) {
    Projet copie = m_projet;  // une méthode de lecture ne change rien, mais par prudence elle travaille sur une copie
    const ReponseApi r = traiterRequete(copie, J{{"jsonrpc", "2.0"}, {"id", 1}, {"method", methode}, {"params", params}});
    if (r.reponse.contains("error")) {
      erreur = true;
      return r.reponse["error"].value("message", std::string("erreur"));
    }
    return r.reponse["result"].dump(1);
  };
  J conception = a.is_object() && a.contains("conception") ? J{{"conception", a["conception"]}} : J::object();
  if (nom == "lire_projet" || nom == "composants" || nom == "reseaux") {
    const std::string t = lecture(nom == "lire_projet" ? "projet.resume" : nom == "composants" ? "conception.composants" : "conception.reseaux", nom == "lire_projet" ? J::object() : conception);
    return texteOutil(t, erreur);
  }
  if (nom == "verifier") {
    const std::string erc = lecture("erc", conception), drc = lecture("drc", conception), revue = lecture("revue", conception);
    return texteOutil("ERC :\n" + erc + "\nDRC :\n" + drc + "\nRevue :\n" + revue, erreur);
  }
  if (nom == "methodes") {
    std::string t;
    for (const auto& [m, d] : methodesApi())
      if (methodeQuiModifie(m) && m != "lot") t += m + " — " + d + "\n";
    return texteOutil(t);
  }
  if (nom == "propositions") {
    std::string t;
    for (const auto& p : m_propositions) t += "n° " + std::to_string(p.numero) + " " + p.methode + " : " + p.etat + (p.resultat.empty() ? std::string() : " (" + p.resultat + ")") + "\n";
    return texteOutil(t.empty() ? std::string("Aucune proposition.") : t);
  }
  if (nom == "proposer") {
    PropositionAssistant x;
    x.methode = a.value("methode", std::string());
    if (a.contains("params") && a["params"].is_object()) x.params = a["params"];
    x.raison = a.value("raison", std::string());
    x.date = maintenant();
    if (x.methode.empty()) {
      erreur = true;
      return texteOutil("« methode » attendue", true);
    }
    bool connue = false;
    for (const auto& [m, d] : methodesApi()) connue |= m == x.methode;
    if (!connue) {
      erreur = true;
      return texteOutil("méthode inconnue : " + x.methode, true);
    }
    if (!methodeQuiModifie(x.methode)) {
      erreur = true;
      return texteOutil("« " + x.methode + " » ne modifie rien : appelle l'outil de lecture correspondant.", true);
    }
    // Essai sur une copie : la proposition dit d'avance si elle s'appliquera
    Projet copie = m_projet;
    const ReponseApi r = traiterRequete(copie, J{{"jsonrpc", "2.0"}, {"id", 1}, {"method", x.methode}, {"params", x.params}});
    if (r.reponse.contains("error")) {
      erreur = true;
      return texteOutil("refusée à l'essai : " + r.reponse["error"].value("message", std::string()), true);
    }
    x.essai = r.nomPas.empty() ? std::string("s'appliquerait") : r.nomPas;
    int n = 0;
    for (const auto& p : m_propositions) n = std::max(n, p.numero);
    x.numero = n + 1;
    m_propositions.push_back(x);
    return texteOutil("Proposition n° " + std::to_string(x.numero) + " enregistrée (" + x.essai + ") : en attente de l'acceptation de l'utilisateur dans Prométhée. Le projet n'est pas modifié.");
  }
  erreur = true;
  return texteOutil("outil inconnu : " + nom, true);
}

std::optional<std::string> ServeurMcp::traiter(const std::string& message) {
  const J q = J::parse(message, nullptr, false);
  if (q.is_discarded() || !q.is_object()) return J{{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32700}, {"message", "JSON illisible"}}}}.dump();
  const bool notification = !q.contains("id");
  const J id = notification ? J() : q["id"];
  const std::string methode = q.value("method", std::string());
  const J params = q.contains("params") ? q["params"] : J::object();
  J r = {{"jsonrpc", "2.0"}, {"id", id}};
  if (methode == "initialize") {
    m_initialise = true;
    r["result"] = {{"protocolVersion", params.value("protocolVersion", std::string("2024-11-05"))},
                   {"capabilities", {{"tools", {{"listChanged", false}}}}},
                   {"serverInfo", {{"name", "promethee"}, {"version", infosVersion().version}}},
                   {"instructions", "Prométhée : CAO électronique. Lis le projet par les outils de lecture ; toute modification passe par « proposer » et attend l'acceptation de l'utilisateur."}};
  } else if (methode == "notifications/initialized" || methode.rfind("notifications/", 0) == 0) {
    return std::nullopt;
  } else if (methode == "ping") {
    r["result"] = J::object();
  } else if (methode == "tools/list") {
    J outils = J::array();
    for (const auto& [n, d] : outilsMcp()) {
      J s = schema({{"conception", "string"}});
      if (n == "proposer") s = schema({{"methode", "string"}, {"params", "object"}, {"raison", "string"}}, {"methode"});
      outils.push_back({{"name", n}, {"description", d}, {"inputSchema", s}});
    }
    r["result"] = {{"tools", outils}};
  } else if (methode == "tools/call") {
    bool erreur = false;
    r["result"] = appelerOutil(params.value("name", std::string()), params.contains("arguments") ? params["arguments"] : J::object(), erreur);
  } else if (methode == "resources/list") {
    r["result"] = {{"resources", J::array()}};
  } else if (methode == "prompts/list") {
    r["result"] = {{"prompts", J::array()}};
  } else {
    if (notification) return std::nullopt;
    r["error"] = {{"code", -32601}, {"message", "méthode inconnue : " + methode}};
  }
  if (notification) return std::nullopt;
  return r.dump();
}

int servirMcp(const std::string& cheminProjet, std::istream& entree, std::ostream& sortie) {
  Projet p;
  try {
    p = lireProjet(cheminProjet);
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 2;
  }
  ServeurMcp s(p);
  const std::string fichier = cheminPropositions(cheminProjet);
  try {
    s.reprendrePropositions(lirePropositions(lireFichier(cheminFichier(fichier))));
  } catch (...) {
  }
  for (std::string ligne; std::getline(entree, ligne);) {
    if (!ligne.empty() && ligne.back() == '\r') ligne.pop_back();
    if (ligne.empty()) continue;
    const std::size_t avant = s.propositions().size();
    if (const auto r = s.traiter(ligne)) {
      sortie << *r << "\n";
      sortie.flush();
    }
    if (s.propositions().size() != avant) {
      try {
        ecrireFichierAtomique(cheminFichier(fichier), textePropositions(s.propositions()));
      } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
      }
    }
  }
  return 0;
}

}  // namespace prom
