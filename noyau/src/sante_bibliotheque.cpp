// Prométhée : demandes d'articles et santé de la bibliothèque d'équipe (lot L19 de la 0.34 ; 19.4).
// Voir sante_bibliotheque.hpp. Licence GPL-3.0-only.
#include "promethee/sante_bibliotheque.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <set>
#include <sstream>

#include "promethee/articles.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

namespace {
fs::path dossierDemandes(const std::string& equipe) { return cheminFichier(equipe) / "demandes"; }
std::string maintenant() {
  const std::time_t t = std::time(nullptr);
  char b[32];
  std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", std::gmtime(&t));
  return b;
}
EtatDemande etatDepuis(const std::string& s) {
  if (s == "prise") return EtatDemande::Prise;
  if (s == "publiee") return EtatDemande::Publiee;
  if (s == "refusee") return EtatDemande::Refusee;
  return EtatDemande::Deposee;
}
Json demandeEnJson(const DemandeArticle& d) {
  return {{"format", "promethee-demande-article"},
          {"uuid", d.uuid},
          {"demandeur", d.demandeur},
          {"reference", d.reference},
          {"fabricant", d.fabricant},
          {"fiche", d.fiche},
          {"commentaire", d.commentaire},
          {"date", d.date},
          {"champs", d.champs},
          {"etat", codeEtatDemande(d.etat)},
          {"bibliothecaire", d.bibliothecaire},
          {"article", d.article},
          {"raison", d.raison}};
}
DemandeArticle demandeDepuisJson(const Json& j) {
  DemandeArticle d;
  d.uuid = j.value("uuid", std::string());
  d.demandeur = j.value("demandeur", std::string());
  d.reference = j.value("reference", std::string());
  d.fabricant = j.value("fabricant", std::string());
  d.fiche = j.value("fiche", std::string());
  d.commentaire = j.value("commentaire", std::string());
  d.date = j.value("date", std::string());
  if (j.contains("champs") && j["champs"].is_object())
    for (const auto& [k, v] : j["champs"].items())
      if (v.is_string()) d.champs[k] = v.get<std::string>();
  d.etat = etatDepuis(j.value("etat", std::string("deposee")));
  d.bibliothecaire = j.value("bibliothecaire", std::string());
  d.article = j.value("article", std::string());
  d.raison = j.value("raison", std::string());
  return d;
}
// Symboles que la netlist sait écrire sans modèle propre (pas de modèle de simulation à exiger)
bool simuleSansModele(const std::string& s) {
  static const std::set<std::string> v = {"R", "C", "C_Pol", "L", "Fusible", "Buzzer", "Moteur", "Bouton", "Interrupteur", "Potentiometre", "D", "LED", "D_Zener", "D_Schottky",
                                          "Q_NPN", "Q_PNP", "NE555", "AOP", "LM358_unites", "Regulateur", "LM7805", "Pile", "Source_tension", "Source_courant"};
  return v.count(s) > 0 || s.rfind("Conn_", 0) == 0 || s.rfind("Bornier_", 0) == 0 || s.rfind("Trou_fixation", 0) == 0 || s == "Point_test";
}
// Signature des pastilles d'une empreinte : formes, tailles et positions au centième de millimètre
std::string signaturePastilles(const ConceptionElec& contexte, const std::string& empreinte) {
  const DefEmpreinte* d = empreinte.empty() ? nullptr : contexte.empreinte(empreinte);
  if (!d) return "?" + empreinte;
  std::vector<std::string> v;
  for (const auto& p : d->pastilles) {
    char b[160];
    std::snprintf(b, sizeof b, "%d:%ld,%ld:%ldx%ld:%ld", static_cast<int>(p.forme), std::lround(p.position.x / 1e4), std::lround(p.position.y / 1e4), std::lround(p.largeur / 1e4), std::lround(p.hauteur / 1e4),
                  std::lround(p.percage / 1e4));
    v.push_back(b);
  }
  std::sort(v.begin(), v.end());
  std::string s;
  for (const auto& x : v) s += x + ";";
  return s;
}
// Valeur et paramètres comparables : valeur comprise dans sa famille, paramètres sans casse
std::string signatureParametres(const Article& a) {
  std::string s = a.symbole + "|";
  const FamilleValeur f = familleDepuisPrefixe(a.symbole, a.symbole);
  const ValeurComprise v = lireValeurComposant(a.valeur, f);
  if (v.comprise()) {
    char b[48];
    std::snprintf(b, sizeof b, "%.6g", v.nombre);
    s += b;
  } else
    s += referenceNormalisee(a.valeur);
  for (const auto& [k, x] : a.parametres) s += "|" + referenceNormalisee(k) + "=" + referenceNormalisee(x);
  return s;
}
}  // namespace

const char* codeEtatDemande(EtatDemande e) {
  switch (e) {
    case EtatDemande::Prise: return "prise";
    case EtatDemande::Publiee: return "publiee";
    case EtatDemande::Refusee: return "refusee";
    default: return "deposee";
  }
}

std::vector<std::string> champsImposes(const std::string& dossierEquipe) {
  std::vector<std::string> r;
  try {
    const Json j = Json::parse(lireFichier(dossierDemandes(dossierEquipe) / "champs.json"));
    if (j.is_array())
      for (const auto& x : j)
        if (x.is_string()) r.push_back(x.get<std::string>());
  } catch (...) {
  }
  return r;
}

bool imposerChamps(const std::string& dossierEquipe, const std::vector<std::string>& champs) {
  try {
    fs::create_directories(dossierDemandes(dossierEquipe));
    ecrireFichierAtomique(dossierDemandes(dossierEquipe) / "champs.json", Json(champs).dump(2) + "\n");
    return true;
  } catch (...) {
    return false;
  }
}

bool deposerDemande(const std::string& dossierEquipe, DemandeArticle& d, std::string* erreur) {
  auto refuser = [&](const std::string& e) {
    if (erreur) *erreur = e;
    return false;
  };
  if (d.reference.empty()) return refuser("la référence fabricant manque");
  for (const auto& c : champsImposes(dossierEquipe))
    if (const auto it = d.champs.find(c); it == d.champs.end() || it->second.empty()) return refuser("le champ « " + c + " », imposé par l'équipe, manque");
  try {
    const fs::path dossier = dossierDemandes(dossierEquipe);
    fs::create_directories(dossier / "fiches");
    if (d.uuid.empty()) d.uuid = nouvelUuid();
    if (d.date.empty()) d.date = maintenant();
    // Fiche jointe copiée avec la demande : le bibliothécaire n'a pas accès au poste du demandeur
    if (!d.fiche.empty()) {
      const fs::path f = cheminFichier(d.fiche);
      if (fs::exists(f)) {
        const fs::path cible = dossier / "fiches" / cheminFichier(d.uuid + "-" + utf8(f.filename()));
        fs::copy_file(f, cible, fs::copy_options::overwrite_existing);
        d.fiche = "fiches/" + utf8(cible.filename());
      }
    }
    d.etat = EtatDemande::Deposee;
    ecrireFichierAtomique(dossier / cheminFichier(d.uuid + ".json"), demandeEnJson(d).dump(2) + "\n");
    return true;
  } catch (const std::exception& e) {
    return refuser(e.what());
  }
}

std::vector<DemandeArticle> demandesDeposees(const std::string& dossierEquipe) {
  std::vector<DemandeArticle> r;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dossierDemandes(dossierEquipe), ec)) {
    if (e.path().extension() != ".json" || e.path().filename() == "champs.json") continue;
    try {
      const Json j = Json::parse(lireFichier(e.path()));
      if (j.value("format", std::string()) == "promethee-demande-article") r.push_back(demandeDepuisJson(j));
    } catch (...) {
    }
  }
  std::sort(r.begin(), r.end(), [](const DemandeArticle& a, const DemandeArticle& b) { return a.date != b.date ? a.date < b.date : a.uuid < b.uuid; });
  return r;
}

bool suivreDemande(const std::string& dossierEquipe, const std::string& uuid, EtatDemande etat, const std::string& bibliothecaire, const std::string& article, const std::string& raison) {
  try {
    const fs::path f = dossierDemandes(dossierEquipe) / cheminFichier(uuid + ".json");
    DemandeArticle d = demandeDepuisJson(Json::parse(lireFichier(f)));
    d.etat = etat;
    if (!bibliothecaire.empty()) d.bibliothecaire = bibliothecaire;
    if (!article.empty()) d.article = article;
    if (!raison.empty()) d.raison = raison;
    ecrireFichierAtomique(f, demandeEnJson(d).dump(2) + "\n");
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<DemandeArticle> demandesServies(const std::string& dossierEquipe, const std::string& demandeur, const std::vector<Article>& bibliotheque) {
  std::set<std::string> ids, references;
  for (const auto& a : bibliotheque) ids.insert(a.id), references.insert(referenceNormalisee(a.mpn));
  std::vector<DemandeArticle> r;
  for (const auto& d : demandesDeposees(dossierEquipe)) {
    if (d.demandeur != demandeur || d.etat != EtatDemande::Publiee) continue;
    if ((!d.article.empty() && ids.count(d.article)) || references.count(referenceNormalisee(d.reference))) r.push_back(d);
  }
  return r;
}

std::string referenceNormalisee(const std::string& mpn) {
  std::string r;
  for (unsigned char c : mpn) {
    if (std::isspace(c) || c == '-' || c == '_' || c == '.' || c == '/') continue;
    r += static_cast<char>(std::toupper(c));
  }
  return r;
}

namespace {
SanteBibliotheque santeAvecContexte(const std::vector<Article>& bibliotheque, const ConceptionElec& contexte, const std::map<std::string, std::string>* modeles3D, const std::atomic<bool>* arret,
                                    const std::function<void(int, int)>& progres) {
  SanteBibliotheque s;
  s.articles = static_cast<int>(bibliotheque.size());
  std::map<std::string, std::vector<std::string>> parReference, parSignature;
  const int total = static_cast<int>(bibliotheque.size());
  for (int i = 0; i < total; ++i) {
    if (arret && arret->load()) {
      s.interrompu = true;
      break;
    }
    const Article& a = bibliotheque[static_cast<std::size_t>(i)];
    // Contrôles d'article (lot L4) : symbole, empreinte, brochage
    for (const auto& m : controlerArticle(a, contexte.symbole(a.symbole), a.empreinte.empty() ? nullptr : contexte.empreinte(a.empreinte))) s.problemes.push_back({a.id, "contrôle", m});
    if (a.modele3D.empty() && !a.empreinte.empty()) s.problemes.push_back({a.id, "modèle 3D", "aucun modèle 3D exact joint (la boîte du corps tient lieu de modèle)"});
    else if (!a.modele3D.empty() && modeles3D && !modeles3D->count(a.modele3D)) s.problemes.push_back({a.id, "modèle 3D", "modèle 3D « " + a.modele3D + " » absent de la bibliothèque"});
    if (a.modeleSpice.texte.empty() && !simuleSansModele(a.symbole) && !a.generique) s.problemes.push_back({a.id, "modèle de simulation", "aucun modèle de simulation : le composant sera écarté du calcul"});
    if (!a.mpn.empty()) parReference[referenceNormalisee(a.mpn)].push_back(a.id);
    if (!a.empreinte.empty() && !a.valeur.empty()) parSignature[signatureParametres(a) + "#" + signaturePastilles(contexte, a.empreinte)].push_back(a.id);
    if (progres && (i % 16 == 0 || i + 1 == total)) progres(i + 1, total);
  }
  std::set<std::set<std::string>> vus;
  for (const auto& [ref, ids] : parReference)
    if (ids.size() > 1) {
      s.doublons.push_back({ids, "même référence fabricant (" + ref + ")"});
      vus.insert(std::set<std::string>(ids.begin(), ids.end()));
    }
  for (const auto& [sig, ids] : parSignature)
    if (ids.size() > 1 && !vus.count(std::set<std::string>(ids.begin(), ids.end()))) s.doublons.push_back({ids, "mêmes paramètres et mêmes pastilles"});
  return s;
}
}  // namespace

SanteBibliotheque santeBibliotheque(const std::vector<Article>& bibliotheque, const std::atomic<bool>* arret, const std::function<void(int, int)>& progres) {
  const ConceptionElec contexte = nouvelleConception("Santé de la bibliothèque");
  return santeAvecContexte(bibliotheque, contexte, nullptr, arret, progres);
}

SanteBibliotheque santeBibliotheque(const ContenuBibliotheque& b, const std::atomic<bool>* arret, const std::function<void(int, int)>& progres) {
  ConceptionElec contexte = nouvelleConception("Santé de la bibliothèque");
  for (const auto& d : b.symboles) contexte.symboles[d.id] = d;
  for (const auto& e : b.empreintes) contexte.empreintes[e.id] = e;
  return santeAvecContexte(b.articles, contexte, &b.modeles3D, arret, progres);
}

ContenuBibliotheque lireFichierBibliotheque(const std::string& chemin, std::string* erreur) {
  ContenuBibliotheque b;
  try {
    const std::string texte = lireFichier(cheminFichier(chemin));
    const Json j = Json::parse(texte);
    if (j.value("format", std::string()) == "promethee-bibliotheque") return lireContenuBibliotheque(texte, erreur);
    b.nom = utf8(cheminFichier(chemin).stem());
    b.articles = lireArticles(texte, erreur);
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
  }
  return b;
}

std::vector<LigneComparaison> comparerArticles(const Article& a, const Article& b) {
  std::vector<LigneComparaison> r;
  auto ligne = [&](const std::string& champ, const std::string& x, const std::string& y) { r.push_back({champ, x, y, x != y}); };
  ligne("Identifiant", a.id, b.id);
  ligne("Nom", a.nom, b.nom);
  ligne("Fabricant", a.fabricant, b.fabricant);
  ligne("Référence fabricant", a.mpn, b.mpn);
  ligne("Référence normalisée", referenceNormalisee(a.mpn), referenceNormalisee(b.mpn));
  ligne("Valeur", a.valeur, b.valeur);
  ligne("Symbole", a.symbole, b.symbole);
  ligne("Empreinte", a.empreinte, b.empreinte);
  const ConceptionElec contexte = nouvelleConception("Comparaison");
  const bool memes = signaturePastilles(contexte, a.empreinte) == signaturePastilles(contexte, b.empreinte);
  ligne("Pastilles", memes ? "identiques" : "différentes", memes ? "identiques" : "différentes");
  ligne("Modèle 3D", a.modele3D, b.modele3D);
  ligne("Modèle de simulation", a.modeleSpice.texte.empty() ? "" : a.modeleSpice.nom, b.modeleSpice.texte.empty() ? "" : b.modeleSpice.nom);
  ligne("Cycle de vie", a.etatCycle, b.etatCycle);
  ligne("Numéro interne", a.numeroInterne, b.numeroInterne);
  std::set<std::string> cles;
  for (const auto& [k, v] : a.parametres) cles.insert(k);
  for (const auto& [k, v] : b.parametres) cles.insert(k);
  for (const auto& k : cles) ligne("Paramètre " + k, a.parametres.count(k) ? a.parametres.at(k) : "", b.parametres.count(k) ? b.parametres.at(k) : "");
  return r;
}

// ---------- Opérations ----------
namespace {
fs::path cheminJournal(const std::string& fichier) { return cheminFichier(fichier).parent_path() / "journal-bibliotheque.csv"; }
void inscrire(OperationBibliotheque& o, const std::string& operation, const std::string& detail) {
  const fs::path journal = cheminJournal(o.fichier);
  o.journal = utf8(journal);
  std::string j = fs::exists(journal) ? lireFichier(journal) : std::string("Date;Auteur;Opération;Article;Détail;Projets;Composants\n");
  auto champ = [](std::string x) {
    std::replace(x.begin(), x.end(), ';', ',');
    std::replace(x.begin(), x.end(), '\n', ' ');
    return x;
  };
  j += maintenant() + ";" + champ(o.auteur) + ";" + operation + ";" + champ(o.article) + ";" + champ(detail) + ";" + std::to_string(o.projets.size()) + ";" + std::to_string(o.composants) + "\n";
  ecrireFichierAtomique(journal, j);
}
// Tableau « articles » du fichier : l'opération n'y touche que l'élément visé
struct FichierArticles {
  std::string texte;
  Json j;
  Json* article(const std::string& id) {
    for (auto& x : j["articles"])
      if (x.is_object() && x.value("id", std::string()) == id) return &x;
    return nullptr;
  }
};
FichierArticles ouvrir(const std::string& fichier) {
  FichierArticles f;
  f.texte = lireFichier(cheminFichier(fichier));
  f.j = Json::parse(f.texte);
  if (!f.j.is_object() || !f.j.contains("articles") || !f.j["articles"].is_array()) throw std::runtime_error("ce n'est pas un fichier d'articles Prométhée");
  return f;
}
bool citeArticle(const ComposantElec& k, const std::string& id) { return k.article == id || k.symbole == "art:" + id; }
}  // namespace

int repointerArticle(Projet& p, const Article& gardee, const std::string& supprime) {
  const std::string& garde = gardee.id;
  int n = 0;
  for (auto& c : p.electronique) {
    if (std::none_of(c.composants.begin(), c.composants.end(), [&](const ComposantElec& k) { return citeArticle(k, supprime); })) continue;
    const std::string sym = installerArticle(c, gardee);
    for (auto& k : c.composants) {
      if (!citeArticle(k, supprime)) continue;
      const std::string ancien = k.symbole;
      k.article = garde;
      if (ancien == "art:" + supprime && !sym.empty()) {
        k.symbole = sym;
        for (auto& fe : c.feuilles)
          for (auto& s : fe.symboles)
            if (s.composant == k.uuid && s.symbole == ancien) s.symbole = sym;
      }
      k.fabricant = gardee.fabricant;
      k.mpn = gardee.mpn;
      if (!gardee.fiche.empty()) k.fiche = gardee.fiche;
      if (!gardee.lcsc.empty()) k.lcsc = gardee.lcsc;
      remplirDepuisArticle(c, k);
      ++n;
    }
    c.articles.erase(supprime);
    c.symboles.erase("art:" + supprime);
  }
  return n;
}

OperationBibliotheque fusionnerArticles(const std::string& fichierBibliotheque, const std::string& garde, const std::string& supprime, const std::vector<std::string>& projets, const std::string& auteur) {
  OperationBibliotheque b;
  b.genre = "fusion";
  b.fichier = fichierBibliotheque;
  b.article = garde;
  b.autre = supprime;
  b.projetsDonnes = projets;
  b.auteur = auteur;
  try {
    FichierArticles f = ouvrir(fichierBibliotheque);
    if (!f.article(garde) || !f.article(supprime) || garde == supprime) {
      b.erreur = "articles introuvables dans la bibliothèque";
      return b;
    }
    const Article gardee = articleDepuisJson(*f.article(garde));
    b.avant.push_back({fichierBibliotheque, f.texte});
    // Les projets : chaque composant qui cite l'article supprimé cite l'article gardé
    std::vector<std::pair<std::string, Projet>> reecrits;
    for (const auto& chemin : projets) {
      Projet p = lireProjet(chemin);
      const int n = repointerArticle(p, gardee, supprime);
      if (n > 0) {
        b.avant.push_back({chemin, lireFichier(cheminFichier(chemin))});
        reecrits.push_back({chemin, std::move(p)});
        b.composants += n;
      }
    }
    auto& tableau = f.j["articles"];
    for (auto it = tableau.begin(); it != tableau.end(); ++it)
      if (it->is_object() && it->value("id", std::string()) == supprime) {
        tableau.erase(it);
        break;
      }
    // Écritures : la bibliothèque, puis les projets ; le journal enfin
    ecrireFichierAtomique(cheminFichier(fichierBibliotheque), f.j.dump(1) + "\n");
    for (auto& [chemin, p] : reecrits) {
      ecrireProjet(p, chemin);
      b.projets.push_back(chemin);
    }
    b.fait = true;
    inscrire(b, "fusion", supprime + " rejoint " + garde);
  } catch (const std::exception& e) {
    b.erreur = e.what();
  }
  return b;
}

OperationBibliotheque corrigerArticle(const std::string& fichierBibliotheque, const std::string& id, const std::string& champ, const std::string& valeur, const std::string& auteur) {
  OperationBibliotheque b;
  b.genre = "correction";
  b.fichier = fichierBibliotheque;
  b.article = id;
  b.champ = champ;
  b.valeur = valeur;
  b.auteur = auteur;
  try {
    FichierArticles f = ouvrir(fichierBibliotheque);
    Json* x = f.article(id);
    if (!x) {
      b.erreur = "article « " + id + " » introuvable";
      return b;
    }
    Article a = articleDepuisJson(*x);
    static const std::map<std::string, std::string Article::*> champs = {
        {"nom", &Article::nom},       {"description", &Article::description}, {"fabricant", &Article::fabricant}, {"mpn", &Article::mpn},
        {"valeur", &Article::valeur}, {"symbole", &Article::symbole},         {"empreinte", &Article::empreinte}, {"fiche", &Article::fiche},
        {"lcsc", &Article::lcsc},     {"modele3D", &Article::modele3D},       {"etatCycle", &Article::etatCycle}, {"numeroInterne", &Article::numeroInterne}};
    if (const auto it = champs.find(champ); it != champs.end()) a.*(it->second) = valeur;
    else if (champ.rfind("parametre:", 0) == 0) {
      if (valeur.empty()) a.parametres.erase(champ.substr(10));
      else a.parametres[champ.substr(10)] = valeur;
    } else {
      b.erreur = "champ « " + champ + " » inconnu";
      return b;
    }
    ++a.version;  // un article corrigé propose la mise à jour aux composants posés (4.10)
    b.avant.push_back({fichierBibliotheque, f.texte});
    *x = articleVersJson(a);
    ecrireFichierAtomique(cheminFichier(fichierBibliotheque), f.j.dump(1) + "\n");
    b.fait = true;
    inscrire(b, "correction", champ + " = " + valeur);
  } catch (const std::exception& e) {
    b.erreur = e.what();
  }
  return b;
}

OperationBibliotheque supprimerArticle(const std::string& fichierBibliotheque, const std::string& id, const std::vector<std::string>& projets, const std::string& auteur) {
  OperationBibliotheque b;
  b.genre = "suppression";
  b.fichier = fichierBibliotheque;
  b.article = id;
  b.projetsDonnes = projets;
  b.auteur = auteur;
  try {
    for (const auto& chemin : projets) {
      const Projet p = lireProjet(chemin);
      for (const auto& c : p.electronique)
        for (const auto& k : c.composants)
          if (citeArticle(k, id)) {
            b.erreur = utf8(cheminFichier(chemin).filename()) + " cite encore l'article (" + k.reference + ") : le fusionner d'abord";
            return b;
          }
    }
    FichierArticles f = ouvrir(fichierBibliotheque);
    if (!f.article(id)) {
      b.erreur = "article « " + id + " » introuvable";
      return b;
    }
    b.avant.push_back({fichierBibliotheque, f.texte});
    auto& tableau = f.j["articles"];
    for (auto it = tableau.begin(); it != tableau.end(); ++it)
      if (it->is_object() && it->value("id", std::string()) == id) {
        tableau.erase(it);
        break;
      }
    ecrireFichierAtomique(cheminFichier(fichierBibliotheque), f.j.dump(1) + "\n");
    b.fait = true;
    inscrire(b, "suppression", {});
  } catch (const std::exception& e) {
    b.erreur = e.what();
  }
  return b;
}

bool annulerOperation(const OperationBibliotheque& o) {
  if (!o.fait) return false;
  try {
    for (const auto& [chemin, contenu] : o.avant) ecrireFichierAtomique(cheminFichier(chemin), contenu);
    OperationBibliotheque trace = o;
    inscrire(trace, "annulation", o.genre + (o.autre.empty() ? std::string() : " de " + o.autre));
    return true;
  } catch (...) {
    return false;
  }
}

OperationBibliotheque refaireOperation(const OperationBibliotheque& o) {
  if (o.genre == "fusion") return fusionnerArticles(o.fichier, o.article, o.autre, o.projetsDonnes, o.auteur);
  if (o.genre == "correction") return corrigerArticle(o.fichier, o.article, o.champ, o.valeur, o.auteur);
  if (o.genre == "suppression") return supprimerArticle(o.fichier, o.article, o.projetsDonnes, o.auteur);
  OperationBibliotheque r = o;
  r.fait = false;
  r.erreur = "opération inconnue";
  return r;
}

}  // namespace prom::ecad
