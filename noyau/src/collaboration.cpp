// Prométhée : travail à plusieurs (lot L12). Licence GPL-3.0-only.
#include "promethee/collaboration.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "promethee/archive.hpp"
#include "promethee/compression.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hachage.hpp"
#include "promethee/modele.hpp"
#include "promethee/version.hpp"

namespace fs = std::filesystem;

namespace prom {

namespace {
using J = nlohmann::ordered_json;
std::string dateIso(std::int64_t secondes) {
  const std::time_t t = static_cast<std::time_t>(secondes);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char s[40];
  std::strftime(s, sizeof s, "%Y-%m-%dT%H:%M:%S", &tm);
  return s;
}
std::int64_t processus() {
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<std::int64_t>(getpid());
#endif
}
}  // namespace

// ---------- Verrou ----------
std::int64_t secondesMaintenant() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::string cheminVerrou(const std::string& projet) {
  const fs::path p = cheminFichier(projet);
  return utf8(p.parent_path() / (".~" + utf8(p.filename()) + ".verrou"));
}

std::optional<VerrouProjet> lireVerrou(const std::string& projet) {
  try {
    const J o = J::parse(lireFichier(cheminFichier(cheminVerrou(projet))));
    VerrouProjet v;
    v.personne = o.value("personne", std::string());
    v.poste = o.value("poste", std::string());
    v.depuis = o.value("depuis", std::string());
    v.vie = o.value("vie", std::int64_t{0});
    v.processus = o.value("processus", std::int64_t{0});
    return v;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

bool verrouVivant(const VerrouProjet& v, std::int64_t maintenant, int delai) { return maintenant - v.vie <= delai; }

namespace {
void ecrireVerrou(const std::string& projet, const VerrouProjet& v) {
  const J o = {{"format", "promethee-verrou"}, {"personne", v.personne}, {"poste", v.poste}, {"depuis", v.depuis}, {"vie", v.vie}, {"processus", v.processus}};
  ecrireFichierAtomique(cheminFichier(cheminVerrou(projet)), o.dump(2) + "\n");
}
}  // namespace

bool poserVerrou(const std::string& projet, const std::string& personne, const std::string& poste, VerrouProjet* tenu) {
  const std::int64_t t = secondesMaintenant();
  VerrouProjet v{personne, poste, dateIso(t), t, processus()};
  if (const auto x = lireVerrou(projet)) {
    const bool nous = x->personne == personne && x->poste == poste;
    if (!nous && verrouVivant(*x, t)) {
      if (tenu) *tenu = *x;
      return false;
    }
    if (nous && !x->depuis.empty()) v.depuis = x->depuis;
  }
  ecrireVerrou(projet, v);
  return true;
}

void renouvelerVerrou(const std::string& projet, const std::string& personne, const std::string& poste) {
  const auto x = lireVerrou(projet);
  if (!x || x->personne != personne || x->poste != poste) return;
  VerrouProjet v = *x;
  v.vie = secondesMaintenant();
  ecrireVerrou(projet, v);
}

void leverVerrou(const std::string& projet, const std::string& personne, const std::string& poste) {
  const auto x = lireVerrou(projet);
  if (!x || x->personne != personne || x->poste != poste) return;
  std::error_code ec;
  fs::remove(cheminFichier(cheminVerrou(projet)), ec);
}

std::string decrireVerrou(const VerrouProjet& v) {
  std::string d = v.depuis;
  if (d.size() >= 16) d = "le " + d.substr(8, 2) + "/" + d.substr(5, 2) + "/" + d.substr(0, 4) + " à " + d.substr(11, 5);
  return "Tenu par " + (v.personne.empty() ? std::string("quelqu'un") : v.personne) + (v.poste.empty() ? "" : " sur " + v.poste) + (d.empty() ? "" : " depuis " + d);
}

// ---------- Archive ----------
std::string archiverProjet(const Projet& p0, const std::string& cheminProjet, std::vector<std::string>* manquants) {
  Projet p = p0;
  const fs::path base = cheminFichier(cheminProjet).parent_path();
  std::vector<std::pair<std::string, std::string>> fichiers;
  std::map<std::string, std::string> deja;  // chemin source → nom dans l'archive
  // Range un fichier cité et rend son chemin dans l'archive, ou le chemin d'origine s'il est introuvable
  const auto ranger = [&](const std::string& cite) -> std::string {
    if (cite.empty() || cite.find("://") != std::string::npos) return cite;
    fs::path src = cheminFichier(cite);
    if (src.is_relative()) src = base / src;
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) {
      if (manquants) manquants->push_back(cite);
      return cite;
    }
    const std::string cle = utf8(fs::weakly_canonical(src, ec));
    if (const auto it = deja.find(cle); it != deja.end()) return it->second;
    std::string nom = "fichiers/" + utf8(src.filename());
    for (int i = 2; std::any_of(fichiers.begin(), fichiers.end(), [&](const auto& f) { return f.first == nom; }); ++i)
      nom = "fichiers/" + std::to_string(i) + "-" + utf8(src.filename());
    fichiers.emplace_back(nom, lireFichier(src));
    deja[cle] = nom;
    return nom;
  };
  if (p.kicad) p.kicad->fichier = ranger(p.kicad->fichier);
  for (auto& s : p.schemasElectriques) s.fichier = ranger(s.fichier);
  for (auto& c : p.electronique)
    for (auto& k : c.composants) {
      for (const char* champ : {"Sim.Bibliotheque", "Programme"})
        if (const auto it = k.champs.find(champ); it != k.champs.end()) it->second = ranger(it->second);
      if (!k.fiche.empty() && k.fiche.find("://") == std::string::npos) k.fiche = ranger(k.fiche);
    }
  const std::string nomProjet = cheminProjet.empty() ? std::string("projet.prom") : utf8(cheminFichier(cheminProjet).filename());
  const std::string texte = versJson(p).dump(2) + "\n";
  J manifeste = {{"format", "promethee-archive"}, {"logiciel", ligneVersion()}, {"projet", nomProjet}, {"empreintes", J::object()}};
  manifeste["empreintes"][nomProjet] = sha256Hex(texte);
  for (const auto& [nom, contenu] : fichiers) manifeste["empreintes"][nom] = sha256Hex(contenu);
  std::vector<std::pair<std::string, std::string>> tout{{"MANIFESTE.json", manifeste.dump(2) + "\n"}, {nomProjet, texte}};
  tout.insert(tout.end(), fichiers.begin(), fichiers.end());
  return zipComprime(tout);
}

std::string extraireArchive(const std::string& zip, const std::string& dossier, std::vector<std::string>* defauts) {
  const auto fichiers = lireZip(zip);
  J manifeste;
  for (const auto& [nom, contenu] : fichiers)
    if (nom == "MANIFESTE.json") manifeste = J::parse(contenu, nullptr, false);
  if (!manifeste.is_object() || manifeste.value("format", std::string()) != "promethee-archive") throw std::runtime_error("ce n'est pas une archive de projet Prométhée");
  const fs::path racine = cheminFichier(dossier);
  for (const auto& [nom, contenu] : fichiers) {
    if (nom.find("..") != std::string::npos || (!nom.empty() && (nom[0] == '/' || nom[0] == '\\'))) {
      if (defauts) defauts->push_back("chemin refusé : " + nom);
      continue;
    }
    const fs::path cible = racine / cheminFichier(nom);
    fs::create_directories(cible.parent_path());
    ecrireFichierAtomique(cible, contenu);
    if (defauts && nom != "MANIFESTE.json") {
      const auto& e = manifeste["empreintes"];
      if (!e.contains(nom) || e[nom] != sha256Hex(contenu)) defauts->push_back("empreinte différente : " + nom);
    }
  }
  return utf8(racine / cheminFichier(manifeste.value("projet", std::string("projet.prom"))));
}

// ---------- Notes ----------
std::string ajouterNote(ecad::ConceptionElec& c, ecad::NoteObjet n) {
  if (n.uuid.empty()) n.uuid = ecad::nouvelUuid();
  if (n.date.empty()) n.date = dateIso(secondesMaintenant());
  c.notes.push_back(n);
  return n.uuid;
}

std::string repondre(ecad::ConceptionElec& c, const std::string& parent, const std::string& auteur, const std::string& texte) {
  const auto it = std::find_if(c.notes.begin(), c.notes.end(), [&](const ecad::NoteObjet& n) { return n.uuid == parent; });
  if (it == c.notes.end()) return {};
  ecad::NoteObjet r;
  r.parent = parent;
  r.objet = it->objet;
  r.auteur = auteur;
  r.texte = texte;
  return ajouterNote(c, r);
}

bool clore(ecad::ConceptionElec& c, const std::string& note, bool close) {
  for (auto& n : c.notes)
    if (n.uuid == note) {
      n.close = close;
      return true;
    }
  return false;
}

std::vector<const ecad::NoteObjet*> notesDe(const ecad::ConceptionElec& c, const std::string& objet) {
  std::vector<const ecad::NoteObjet*> r;
  for (const auto& n : c.notes)
    if (n.objet == objet) r.push_back(&n);
  return r;
}

std::vector<std::string> mentions(const std::string& texte) {
  std::vector<std::string> r;
  for (std::size_t i = 0; i < texte.size(); ++i) {
    if (texte[i] != '@' || (i > 0 && (std::isalnum(static_cast<unsigned char>(texte[i - 1])) || texte[i - 1] == '.'))) continue;
    std::size_t j = i + 1;
    // Lettres, chiffres, tirets et caractères accentués (octets UTF-8 de poids fort)
    while (j < texte.size() && (std::isalnum(static_cast<unsigned char>(texte[j])) || texte[j] == '-' || texte[j] == '_' || (static_cast<unsigned char>(texte[j]) & 0x80))) ++j;
    if (j > i + 1) {
      const std::string nom = texte.substr(i + 1, j - i - 1);
      if (std::find(r.begin(), r.end(), nom) == r.end()) r.push_back(nom);
    }
    i = j;
  }
  return r;
}

std::vector<const ecad::NoteObjet*> enAttente(const ecad::ConceptionElec& c, const std::string& personne) {
  std::vector<const ecad::NoteObjet*> r;
  for (const auto& n : c.notes) {
    if (n.close) continue;
    const auto m = mentions(n.texte);
    if ((n.genre == ecad::NoteObjet::Genre::Tache && n.responsable == personne) || std::find(m.begin(), m.end(), personne) != m.end()) r.push_back(&n);
  }
  std::sort(r.begin(), r.end(), [](const ecad::NoteObjet* a, const ecad::NoteObjet* b) { return a->priorite != b->priorite ? a->priorite > b->priorite : a->date < b->date; });
  return r;
}

std::string objetLisible(const ecad::ConceptionElec& c, const ecad::NoteObjet& n) {
  if (n.objet.empty()) return "carte";
  if (n.objet.rfind("reseau:", 0) == 0) return "réseau " + n.objet.substr(7);
  if (const ecad::ComposantElec* k = c.composant(n.objet)) return k->reference;
  for (const auto& t : c.circuit.pistes)
    if (t.uuid == n.objet) return "piste de " + t.reseau;
  for (const auto& v : c.circuit.vias)
    if (v.uuid == n.objet) return "via de " + v.reseau;
  for (const auto& z : c.circuit.zones)
    if (z.uuid == n.objet) return "zone " + z.reseau;
  return n.objet.substr(0, 8);
}

int importerRetourRevue(ecad::ConceptionElec& c, const std::string& texte, std::vector<std::string>* alertes) {
  const J o = J::parse(texte, nullptr, false);
  if (!o.is_object() || o.value("format", std::string()) != "promethee-revue") {
    if (alertes) alertes->push_back("Ce fichier n'est pas un retour de revue de Prométhée.");
    return 0;
  }
  if (const std::string u = o.value("conception", std::string()); !u.empty() && u != c.uuid && alertes)
    alertes->push_back("Retour écrit pour une autre carte : remarques placées d'après les repères.");
  int n = 0;
  if (o.contains("remarques") && o["remarques"].is_array())
    for (const auto& r : o["remarques"]) {
      if (!r.is_object()) continue;
      ecad::NoteObjet note;
      note.texte = r.value("texte", std::string());
      note.auteur = r.value("auteur", std::string("relecteur"));
      note.date = r.value("date", std::string());
      const std::string repere = r.value("repere", std::string()), reseau = r.value("reseau", std::string());
      if (!repere.empty()) {
        if (const ecad::ComposantElec* k = c.parReference(repere)) note.objet = k->uuid;
        else if (alertes) alertes->push_back("Remarque sur « " + repere + " » : repère inconnu, placée sur la carte.");
      } else if (!reseau.empty())
        note.objet = "reseau:" + reseau;
      if (note.texte.empty()) continue;
      ajouterNote(c, note);
      ++n;
    }
  return n;
}

// ---------- Modèles de projet ----------
Projet projetDepuisModele(const Projet& modele, const std::string& nom) {
  Projet p = modele;
  p.nom = nom;
  for (auto& c : p.electronique) {
    c.uuid = ecad::nouvelUuid();
    c.publications.clear();
    c.verrouillee = false;
    c.notes.clear();
    if (p.electronique.size() == 1) c.nom = nom;
  }
  p.ordresModification.clear();
  return p;
}

std::vector<std::string> reprendreReglages(ecad::ConceptionElec& cible, const ecad::ConceptionElec& s, const std::set<std::string>& sections) {
  std::vector<std::string> r;
  const auto prendre = [&](const char* nom, const std::function<void()>& f) {
    if (!sections.count(nom)) return;
    f();
    r.push_back(nom);
  };
  prendre("regles", [&] { cible.circuit.regles = s.circuit.regles; });
  prendre("empilage", [&] { cible.circuit.empilage = s.circuit.empilage; });
  prendre("cartouche", [&] {
    // Le titre et la révision restent ceux de la carte : seuls la société, l'auteur et le gabarit passent
    const auto titre = cible.cartouche;
    cible.cartouche = s.cartouche;
    cible.cartouche.titre = titre.titre;
    cible.cartouche.revision = titre.revision;
  });
  prendre("serigraphie", [&] { cible.circuit.serigraphie = s.circuit.serigraphie; });
  prendre("fabrication", [&] { cible.circuit.fabrication = s.circuit.fabrication; });
  prendre("affichage", [&] { cible.circuit.affichage = s.circuit.affichage; });
  prendre("variables", [&] {
    for (const auto& [k, v] : s.variables) cible.variables[k] = v;
  });
  prendre("jeuxSorties", [&] { cible.jeuxSorties = s.jeuxSorties; });
  prendre("erc", [&] { cible.erc = s.erc; });
  return r;
}

// ---------- Index des usages ----------
IndexUsages indexerUsages(const std::vector<std::string>& dossiers, const std::atomic<bool>* arret) {
  IndexUsages r;
  for (const auto& d : dossiers) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(cheminFichier(d), fs::directory_options::skip_permission_denied, ec), fin; it != fin; it.increment(ec)) {
      if (ec) break;
      if (arret && arret->load()) return r;
      if (!it->is_regular_file(ec)) continue;
      const std::string nom = utf8(it->path().filename());
      const bool projet = nom.size() > 5 && (nom.substr(nom.size() - 5) == ".prom" || (nom.size() > 10 && nom.substr(nom.size() - 10) == ".prom.json"));
      if (!projet || nom.rfind(".~", 0) == 0) continue;
      try {
        const Projet p = lireProjet(utf8(it->path()));
        ++r.projets;
        for (const auto& c : p.electronique) {
          std::map<std::string, std::vector<std::string>> em, sy, ar;
          for (const auto& k : c.composants) {
            if (!k.empreinte.empty()) em[k.empreinte].push_back(k.reference);
            if (!k.symbole.empty()) sy[k.symbole].push_back(k.reference);
            if (!k.article.empty()) ar[k.article].push_back(k.reference);
          }
          for (auto* m : {&em, &sy, &ar})
            for (auto& [id, refs] : *m) {
              std::sort(refs.begin(), refs.end(), ecad::ordreNaturel);
              auto& cible = m == &em ? r.empreintes : m == &sy ? r.symboles : r.articles;
              cible[id].push_back({utf8(it->path()), c.nom, refs});
            }
        }
      } catch (const std::exception&) {
        r.illisibles.push_back(utf8(it->path()));
      }
    }
  }
  return r;
}

}  // namespace prom
