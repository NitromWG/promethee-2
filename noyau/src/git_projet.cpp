// Prométhée : Git du projet et des bibliothèques. Voir git_projet.hpp. Licence GPL-3.0-only.
#include "promethee/git_projet.hpp"

#include <filesystem>
#include <optional>
#include <regex>
#include <sstream>

#include "promethee/articles.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/processus.hpp"

namespace fs = std::filesystem;

namespace prom {

namespace {
std::string sansFin(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}
// git dans le dépôt, sans invite, sans protocole réseau tant qu'il n'est pas permis
ResultatProcessus git(const DepotGit& d, std::vector<std::string> args, const std::string& entree = {}) {
  std::vector<std::string> a{"git"};
  if (!d.auteur.empty()) a.insert(a.end(), {"-c", "user.name=" + d.auteur});
  if (!d.courriel.empty()) a.insert(a.end(), {"-c", "user.email=" + d.courriel});
  a.insert(a.end(), {"-c", "core.quotepath=off", "-c", "init.defaultBranch=principale"});
  a.insert(a.end(), args.begin(), args.end());
  OptionsProcessus o;
  o.dossier = d.dossier;
  o.entree = entree;
  o.delai = 120;
  o.environnement["GIT_TERMINAL_PROMPT"] = "0";
  o.environnement["LC_ALL"] = "C";
  if (!d.reseauPermis) o.environnement["GIT_ALLOW_PROTOCOL"] = "file";
  if (d.reseauPermis && !d.cleSsh.empty()) o.environnement["GIT_SSH_COMMAND"] = "ssh -i \"" + d.cleSsh + "\" -o IdentitiesOnly=yes -o BatchMode=yes";
  return executerProcessus(a, o);
}
ResultatGit rendre(const ResultatProcessus& p, const std::string& reussite, const std::string& echec) {
  ResultatGit r;
  r.ok = p.reussi();
  r.sortie = p.sortie + p.erreurs;
  r.message = r.ok ? reussite : echec + (p.erreurs.empty() ? std::string() : " : " + sansFin(p.erreurs.substr(0, 400)));
  return r;
}
ResultatGit refus(const std::string& m) {
  ResultatGit r;
  r.message = m;
  return r;
}
std::optional<ResultatGit> controler(const DepotGit& d) {
  if (d.coupe) return refus("Git est coupé pour ce projet.");
  if (!gitDisponible()) return refus("git n’est pas installé sur ce poste.");
  return std::nullopt;
}
std::optional<ResultatGit> reseau(const DepotGit& d, const std::string& url) {
  if (adresseReseau(url) && !d.reseauPermis)
    return refus("« " + url + " » passe par le réseau, qui n’est pas activé pour ce dépôt : rien n’a été envoyé ni reçu.");
  return std::nullopt;
}
std::string nomSur(const std::string& id) {
  std::string r;
  for (const char ch : id) r += std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.' ? ch : '-';
  return r.empty() ? std::string("article") : r;
}
}  // namespace

bool gitDisponible(std::string* version) {
  static const std::string v = [] {
    if (programmeDuPoste("git").empty()) return std::string();
    const ResultatProcessus p = executerProcessus({"git", "--version"});
    return p.reussi() ? sansFin(p.sortie) : std::string();
  }();
  if (version) *version = v;
  return !v.empty();
}

bool adresseReseau(const std::string& url) {
  static const std::regex schema(R"(^\s*(https?|ssh|git|ftps?|rsync)://)", std::regex::icase);
  static const std::regex scp(R"(^\s*[A-Za-z0-9._-]+@[A-Za-z0-9._-]+:)");
  static const std::regex hote(R"(^\s*[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+:[^\\/])");  // « serveur.lan:depot.git »
  return std::regex_search(url, schema) || std::regex_search(url, scp) || std::regex_search(url, hote);
}

ResultatGit gitInitialiser(const DepotGit& d) {
  if (auto r = controler(d)) return *r;
  std::error_code ec;
  fs::create_directories(cheminFichier(d.dossier), ec);
  return rendre(git(d, {"init", "-q"}), "Dépôt Git créé dans le dossier.", "Dépôt impossible à créer");
}

ResultatGit gitCloner(const std::string& source, const std::string& dossier, bool reseauPermis) {
  DepotGit d;
  d.reseauPermis = reseauPermis;
  if (auto r = controler(d)) return *r;
  if (auto r = reseau(d, source)) return *r;
  const fs::path cible = cheminFichier(dossier);
  d.dossier = utf8(cible.parent_path());
  std::error_code ec;
  fs::create_directories(cible.parent_path(), ec);
  return rendre(git(d, {"clone", "-q", source, utf8(cible.filename())}), "Dépôt cloné.", "Clonage impossible");
}

ResultatGit gitRattacherDistant(const DepotGit& d, const std::string& nom, const std::string& url) {
  if (auto r = controler(d)) return *r;
  // Rattacher n'échange rien : l'adresse est notée, l'accès réseau reste à activer pour s'en servir
  const ResultatProcessus existe = git(d, {"remote", "get-url", nom});
  const ResultatProcessus p = existe.reussi() ? git(d, {"remote", "set-url", nom, url}) : git(d, {"remote", "add", nom, url});
  ResultatGit r = rendre(p, "Dépôt distant « " + nom + " » rattaché.", "Dépôt distant impossible à rattacher");
  if (r.ok && adresseReseau(url) && !d.reseauPermis) r.message += " Il passe par le réseau : active l’accès pour ce dépôt avant de pousser ou de tirer.";
  return r;
}

std::string gitAdresseDistant(const DepotGit& d, const std::string& distant) {
  if (d.coupe || !gitDisponible()) return {};
  const ResultatProcessus p = git(d, {"remote", "get-url", distant});
  return p.reussi() ? sansFin(p.sortie) : std::string();
}

ResultatGit gitEssaiConnexion(const DepotGit& d, const std::string& distant) {
  if (auto r = controler(d)) return *r;
  const std::string url = gitAdresseDistant(d, distant);
  if (url.empty()) return refus("Aucun dépôt distant « " + distant + " ».");
  if (auto r = reseau(d, url)) return *r;
  return rendre(git(d, {"ls-remote", "--heads", distant}), "Connexion réussie à « " + distant + " ».", "Connexion impossible");
}

ResultatGit gitPousser(const DepotGit& d, const std::string& distant, const std::string& branche) {
  if (auto r = controler(d)) return *r;
  const std::string url = gitAdresseDistant(d, distant);
  if (url.empty()) return refus("Aucun dépôt distant « " + distant + " ».");
  if (auto r = reseau(d, url)) return *r;
  return rendre(git(d, {"push", "-q", distant, branche}), "Branche « " + branche + " » poussée vers « " + distant + " ».", "Envoi impossible");
}

ResultatGit gitTirer(const DepotGit& d, const std::string& distant, const std::string& branche) {
  if (auto r = controler(d)) return *r;
  const std::string url = gitAdresseDistant(d, distant);
  if (url.empty()) return refus("Aucun dépôt distant « " + distant + " ».");
  if (auto r = reseau(d, url)) return *r;
  return rendre(git(d, {"pull", "-q", "--no-rebase", distant, branche}), "Branche « " + branche + " » tirée de « " + distant + " ».", "Réception impossible");
}

ResultatGit gitBrancher(const DepotGit& d, const std::string& branche, bool creer) {
  if (auto r = controler(d)) return *r;
  return rendre(creer ? git(d, {"checkout", "-q", "-b", branche}) : git(d, {"checkout", "-q", branche}), "Branche « " + branche + " ».", "Changement de branche impossible");
}

ResultatGit gitValider(const DepotGit& d, const std::string& message, const std::vector<std::string>& fichiers) {
  if (auto r = controler(d)) return *r;
  std::vector<std::string> ajout{"add", "--"};
  if (fichiers.empty()) ajout = {"add", "-A"};
  else ajout.insert(ajout.end(), fichiers.begin(), fichiers.end());
  if (const ResultatProcessus a = git(d, ajout); !a.reussi()) return rendre(a, {}, "Fichiers impossibles à ajouter");
  return rendre(git(d, {"commit", "-q", "-m", message}), "Validé : « " + message + " ».", "Validation impossible");
}

std::string gitBrancheCourante(const DepotGit& d) {
  if (d.coupe || !gitDisponible()) return {};
  const ResultatProcessus p = git(d, {"rev-parse", "--abbrev-ref", "HEAD"});
  return p.reussi() ? sansFin(p.sortie) : std::string();
}

std::vector<std::string> gitBranches(const DepotGit& d) {
  std::vector<std::string> r;
  if (d.coupe || !gitDisponible()) return r;
  std::istringstream in(git(d, {"branch", "--format=%(refname:short)"}).sortie);
  for (std::string l; std::getline(in, l);)
    if (!sansFin(l).empty()) r.push_back(sansFin(l));
  return r;
}

std::vector<std::string> gitFichiersDeBranche(const DepotGit& d, const std::string& branche) {
  std::vector<std::string> r;
  if (d.coupe || !gitDisponible()) return r;
  std::istringstream in(git(d, {"ls-tree", "-r", "--name-only", branche}).sortie);
  for (std::string l; std::getline(in, l);)
    if (!sansFin(l).empty()) r.push_back(sansFin(l));
  return r;
}

ContributionArticle contribuerArticle(const DepotGit& d, const ecad::Article& a, const ecad::DefSymbole* s, const ecad::DefEmpreinte* e, const std::string& modele3d) {
  ContributionArticle c;
  if (auto r = controler(d)) {
    c.erreur = r->message;
    return c;
  }
  if (a.id.empty()) {
    c.erreur = "Article sans identifiant.";
    return c;
  }
  const std::string depart = gitBrancheCourante(d);
  if (depart.empty() || depart == "HEAD") {
    c.erreur = "La bibliothèque n’a pas encore de branche validée.";
    return c;
  }
  const std::string id = nomSur(a.id);
  c.branche = "contribution/" + id;
  if (const ResultatGit b = gitBrancher(d, c.branche, true); !b.ok) {
    c.erreur = b.message;
    return c;
  }
  const fs::path racine = cheminFichier(d.dossier);
  auto ecrire = [&](const std::string& relatif, const std::string& contenu) {
    const fs::path p = racine / cheminFichier(relatif);
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    ecrireFichierAtomique(p, contenu);
    c.fichiers.push_back(relatif);
  };
  try {
    ecad::Article art = a;
    if (!modele3d.empty()) art.modele3D = "modeles3d/" + utf8(cheminFichier(modele3d).filename());
    ecrire("articles/" + id + ".json", ecad::articleVersJson(art).dump(2) + "\n");
    if (s) ecrire("symboles/" + nomSur(s->id) + ".json", ecad::symboleVersJson(*s).dump(2) + "\n");
    if (e) ecrire("empreintes/" + nomSur(e->id) + ".json", ecad::empreinteVersJson(*e).dump(2) + "\n");
    if (!modele3d.empty()) ecrire(art.modele3D, lireFichier(cheminFichier(modele3d)));
  } catch (const std::exception& x) {
    c.erreur = x.what();
    gitBrancher(d, depart, false);
    return c;
  }
  const ResultatGit v = gitValider(d, "Contribution : " + (a.nom.empty() ? a.id : a.nom) + (a.mpn.empty() ? std::string() : " (" + a.mpn + ")"), c.fichiers);
  gitBrancher(d, depart, false);
  c.ok = v.ok;
  if (!v.ok) c.erreur = v.message;
  return c;
}

}  // namespace prom
