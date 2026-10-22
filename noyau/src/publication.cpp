// Prométhée : validation globale d'une conception électronique (santé, étape par étape), porte de publication et
// révisions verrouillées. Une conception publiée ne change plus sans passer à une nouvelle révision ; une
// publication forcée malgré des erreurs garde la raison donnée et la liste des erreurs acceptées. Licence GPL-3.0-only.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hachage.hpp"
#include "promethee/version.hpp"

namespace prom::ecad {

int Validation::bloquants() const {
  int n = 0;
  for (const auto& e : etapes) n += e.erreurs;
  return n;
}

bool derogeable(const std::string& code) {
  // Courts-circuits, connectivité incomplète, bibliothèques manquantes, géométrie invalide, capacités du
  // fabricant, intégrité : jamais de dérogation. S'y ajoutent les CMS sans cuivre d'une carte simple face (DRC-38,
  // R2), le profil de fabricant inconnu (DFM-20, R3) et le brochage non vérifié d'une autre famille de boîtier
  // (ERC-09, B17) : chacun peut envoyer une carte fausse chez le fabricant.
  static const std::set<std::string> critiques = {"INT-01", "INT-03", "INT-04", "PCB-01", "ERC-01", "ERC-02", "ERC-04", "ERC-05", "ERC-06", "ERC-09", "ERC-23",
                                                  "DRC-06", "DRC-08", "DRC-09", "DRC-14", "DRC-15", "DRC-20", "DRC-21", "DRC-38", "DFM-01", "DFM-10", "DFM-11",
                                                  "DFM-12", "DFM-13", "DFM-14", "DFM-15", "DFM-16", "DFM-20"};
  return critiques.count(code) == 0;
}

int Validation::critiques() const {
  int n = 0;
  for (const auto& e : etapes)
    for (const auto& d : e.details) n += d.gravite == Gravite::Erreur && !derogeable(d.code);
  return n;
}

namespace {
std::string mmTexte(Nm v) {
  char t[32];
  std::snprintf(t, sizeof t, "%.3g mm", static_cast<double>(v) * 1e-6);
  return t;
}
bool code(const Violation& v, std::initializer_list<const char*> codes) {
  return std::any_of(codes.begin(), codes.end(), [&](const char* c) { return v.code == c; });
}
void ranger(EtapeValidation& e, const Violation& v) {
  (v.gravite == Gravite::Erreur ? e.erreurs : e.avertissements)++;
  e.details.push_back(v);
}
void signaler(EtapeValidation& e, Gravite g, const std::string& c, const std::string& message, const std::string& conseil, std::vector<std::string> objets = {}) {
  Violation v;
  v.gravite = g;
  v.code = c;
  v.message = message;
  v.conseil = conseil;
  v.objets = std::move(objets);
  ranger(e, v);
}
int borne(double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 100.0))); }
std::string sansEspaces(const std::string& s) {
  const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
std::string nomDeFichier(const std::string& s, const char* defaut) { return nomFichierAscii(s, defaut); }  // EL-46
std::string liste(const std::vector<std::string>& refs) {
  std::string t;
  for (std::size_t i = 0; i < refs.size() && i < 8; ++i) t += (i ? ", " : "") + refs[i];
  if (refs.size() > 8) t += "…";
  return t;
}
}  // namespace

Validation valider(const ConceptionElec& c, const Connectivite& k, const EtatRoutage& etat, const std::vector<Violation>& erc, const std::vector<Violation>& drc) {
  (void)k;
  Validation v;
  auto etape = [](const char* nom) {
    EtapeValidation e;
    e.nom = nom;
    return e;
  };
  EtapeValidation integrite = etape("Intégrité du projet"), bibliotheques = etape("Bibliothèques"), electrique = etape("Électrique (ERC)"), circuit = etape("Circuit imprimé (DRC)"),
                  nomenc = etape("Nomenclature"), fabrication = etape("Fabrication (DFM)"), documentation = etape("Documentation");
  // Chaque violation des vérifications va dans une seule étape
  for (const auto& x : erc) {
    if (code(x, {"ERC-02", "ERC-06"})) ranger(integrite, x);
    else if (code(x, {"ERC-01", "ERC-03", "ERC-04", "ERC-05"})) ranger(bibliotheques, x);
    else ranger(electrique, x);
  }
  for (const auto& x : drc) {
    if (code(x, {"DRC-08"})) ranger(bibliotheques, x);
    else ranger(circuit, x);
  }
  // Intégrité : chaque symbole a son composant, chaque composant son symbole, chaque empreinte son composant
  std::set<std::string> dessines;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) {
      if (s.composant.empty()) continue;
      if (!c.composant(s.composant)) signaler(integrite, Gravite::Erreur, "INT-01", "Symbole sans composant sur « " + f.nom + " »", "Supprime le symbole et repose-le depuis la bibliothèque.", {s.uuid});
      else dessines.insert(s.composant);
    }
  for (const auto& x : c.composants)
    if (!dessines.count(x.uuid)) signaler(integrite, Gravite::Avertissement, "INT-02", x.reference + " n’a plus de symbole dans le schéma", "Mets à jour le circuit (F8) pour le retirer, ou repose son symbole.", {x.uuid});
  for (const auto& e : c.circuit.empreintes)
    if (!c.composant(e.composant)) signaler(integrite, Gravite::Erreur, "INT-03", "Empreinte sans composant sur le circuit", "Mets à jour le circuit depuis le schéma (F8).", {e.composant});
  if (c.composants.empty()) signaler(integrite, Gravite::Erreur, "INT-04", "La conception ne contient aucun composant", "Place des composants dans le schéma.");
  integrite.score = borne(100 - 25.0 * integrite.erreurs - 5.0 * integrite.avertissements);
  // Bibliothèques : part des composants dont le symbole et l'empreinte sont connus
  {
    int connus = 0;
    for (const auto& x : c.composants) {
      const DefSymbole* d = c.symbole(x.symbole);
      if (d && (x.empreinte.empty() ? false : c.empreinte(x.empreinte) != nullptr)) ++connus;
    }
    bibliotheques.score = c.composants.empty() ? 100 : borne(100.0 * connus / static_cast<double>(c.composants.size()));
    if (bibliotheques.erreurs && bibliotheques.score == 100) bibliotheques.score = 90;
  }
  electrique.score = borne(100 - 20.0 * electrique.erreurs - 4.0 * electrique.avertissements);
  // Circuit : part des réseaux entièrement routés, moins les autres erreurs
  {
    int autres = 0;
    for (const auto& x : circuit.details) autres += x.gravite == Gravite::Erreur && x.code != "DRC-20";
    const double routes = etat.reseauxTotal ? 100.0 * etat.reseauxComplets / etat.reseauxTotal : (c.circuit.empreintes.empty() ? 0.0 : 100.0);
    circuit.score = borne(routes - 20.0 * autres - 2.0 * circuit.avertissements);
    if (c.circuit.empreintes.empty() && !c.composants.empty())
      signaler(circuit, Gravite::Erreur, "PCB-01", "Le circuit imprimé est vide", "Lance « Mettre à jour depuis le schéma » (F8) puis place et route les composants.");
  }
  // Nomenclature : valeurs présentes, références fabricant
  {
    int complets = 0, compte = 0;
    std::vector<std::string> sansMpn;
    for (const auto& x : c.composants) {
      const DefSymbole* d = c.symbole(x.symbole);
      if (x.nePasMonter || (d && (d->mecanique || d->alimentation || d->drapeau))) continue;
      ++compte;
      if (sansEspaces(x.valeur).empty()) signaler(nomenc, Gravite::Erreur, "BOM-01", x.reference + " n’a pas de valeur", "Donne-lui une valeur (10k, 100nF, référence du circuit…).", {x.uuid});
      if (sansEspaces(x.mpn).empty() || sansEspaces(x.fabricant).empty()) sansMpn.push_back(x.reference);
      else if (!sansEspaces(x.valeur).empty()) ++complets;
    }
    std::sort(sansMpn.begin(), sansMpn.end(), ordreNaturel);
    if (!sansMpn.empty())
      signaler(nomenc, Gravite::Avertissement, "BOM-02", std::to_string(sansMpn.size()) + " composant(s) sans fabricant ni référence fabricant : " + liste(sansMpn),
               "Renseigne le fabricant et sa référence pour que la carte puisse être approvisionnée sans ambiguïté.");
    nomenc.score = compte ? borne(100.0 * complets / compte) : 100;
  }
  // Fabrication : règles de la carte face aux capacités du fabricant choisi. Un profil inconnu n'est plus un
  // avertissement qui saute les contrôles : renommer « Standard » ne doit lever aucune erreur (R3).
  {
    const std::optional<ProfilFabricant> reference = profilDeReference(c.circuit.regles);
    const ProfilFabricant* profil = reference ? &*reference : nullptr;
    const Polygone contour = c.circuit.contour.polygone();
    if (contour.size() < 3 || aireMm2(contour) < 1.0) signaler(fabrication, Gravite::Erreur, "DFM-01", "Contour de carte invalide", "Redessine le contour de la carte.");
    if (!profil)
      signaler(fabrication, Gravite::Erreur, "DFM-20",
               "Profil de fabricant inconnu : « " + c.circuit.regles.profil + " »" +
                   (c.circuit.regles.capacitesPerso ? std::string(" (capacités incomplètes)") : std::string(" : les capacités du fabricant ne sont pas vérifiées")),
               "Choisis un profil du catalogue dans les règles de conception, ou décris les capacités de ton fabricant (isolement, piste, perçage, couronne et distance au bord minimaux).");
    else {
      const Regles& R = c.circuit.regles;
      const Regles& M = profil->regles;
      auto sous = [&](const char* cd, const char* quoi, Nm valeur, Nm minimum) {
        if (valeur < minimum)
          signaler(fabrication, Gravite::Erreur, cd, std::string(quoi) + " de " + mmTexte(valeur) + " sous le minimum de « " + profil->nom + " » (" + mmTexte(minimum) + ")",
                   "Relève la règle, ou choisis un fabricant plus fin.");
      };
      sous("DFM-10", "Isolement", R.isolement, M.isolement);
      sous("DFM-11", "Largeur minimale de piste", R.largeurMin, M.largeurMin);
      sous("DFM-12", "Perçage minimal", R.percageMin, M.percageMin);
      sous("DFM-13", "Couronne minimale", R.anneauMin, M.anneauMin);
      sous("DFM-14", "Distance au bord", R.bord, M.bord);
      int pistes = 0, vias = 0;
      for (const auto& p : c.circuit.pistes) pistes += p.largeur < M.largeurMin;
      for (const auto& x : c.circuit.vias) vias += x.percage < M.percageMin;
      if (pistes) signaler(fabrication, Gravite::Erreur, "DFM-15", std::to_string(pistes) + " piste(s) plus fine(s) que " + mmTexte(M.largeurMin), "Élargis-les : le fabricant ne sait pas les graver.");
      if (vias) signaler(fabrication, Gravite::Erreur, "DFM-16", std::to_string(vias) + " via(s) percée(s) sous " + mmTexte(M.percageMin), "Agrandis leur perçage.");
    }
    fabrication.score = borne(100 - 30.0 * fabrication.erreurs - 5.0 * fabrication.avertissements);
  }
  // Documentation : cartouche
  {
    int remplis = 0;
    for (const auto& [champ, valeur] : {std::pair{"titre", &c.cartouche.titre}, std::pair{"auteur", &c.cartouche.auteur}, std::pair{"révision", &c.cartouche.revision}, std::pair{"date", &c.cartouche.date}}) {
      if (!sansEspaces(*valeur).empty()) ++remplis;
      else signaler(documentation, Gravite::Avertissement, "DOC-01", std::string("Cartouche : ") + champ + " manquant(e)", "Complète le cartouche (onglet Schéma, sans rien sélectionner).");
    }
    documentation.score = borne(25.0 * remplis);
  }
  v.etapes = {integrite, bibliotheques, electrique, circuit, nomenc, fabrication, documentation};
  return v;
}

Validation valider(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  const auto erc = verifierERC(c, k);
  const Remplissages r = remplirZones(c, k);
  const EtatRoutage e = etatRoutage(c, k, r);
  return valider(c, k, e, erc, verifierDRCReglee(c, k, r));  // gravités réglées et exclusions du projet (2.33)
}

std::string empreinteConception(const ConceptionElec& c) {
  nlohmann::ordered_json j = versJson(c);
  j.erase("publications");
  j.erase("verrouillee");
  if (j.contains("circuit") && j["circuit"].is_object()) j["circuit"].erase("affichage");  // grille et unité : réglages de travail
  return sha256Hex(j.dump());
}

std::string revisionSuivante(const std::string& r0) {
  std::string r = sansEspaces(r0);
  if (r.empty()) return "A";
  // Partie finale : chiffres (3 → 4, B2 → B3) ou lettres (A → B, Z → AA, AZ → BA)
  std::size_t i = r.size();
  if (std::isdigit(static_cast<unsigned char>(r.back()))) {
    while (i > 0 && std::isdigit(static_cast<unsigned char>(r[i - 1]))) --i;
    const std::string chiffres = r.substr(i);
    const long long n = std::stoll(chiffres.substr(0, std::min<std::size_t>(chiffres.size(), 17))) + 1;
    return r.substr(0, i) + std::to_string(n);
  }
  if (std::isalpha(static_cast<unsigned char>(r.back()))) {
    while (i > 0 && std::isalpha(static_cast<unsigned char>(r[i - 1]))) --i;
    std::string lettres = r.substr(i);
    for (char& ch : lettres) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    int j = static_cast<int>(lettres.size()) - 1;
    while (j >= 0) {
      if (lettres[static_cast<std::size_t>(j)] != 'Z') {
        ++lettres[static_cast<std::size_t>(j)];
        break;
      }
      lettres[static_cast<std::size_t>(j)] = 'A';
      --j;
    }
    if (j < 0) lettres.insert(lettres.begin(), 'A');
    return r.substr(0, i) + lettres;
  }
  return r + "2";
}

namespace {
std::string nomBase(const std::string& nom) { return nomFichierAscii(nom, "carte"); }  // EL-46

// Règles de conception figées dans la fiche de publication
std::string texteRegles(const ConceptionElec& c) {
  const Regles& R = c.circuit.regles;
  const Empilage& e = c.circuit.empilage;
  auto m = [](Nm v) { return mmTexte(v); };
  std::ostringstream s;
  s << "Règles de conception (figées avec cette publication) :\n";
  s << "  Profil du fabricant : " << R.profil << "\n";
  s << "  Isolement " << m(R.isolement) << " ; piste " << m(R.largeur) << " (minimum " << m(R.largeurMin) << ") ; via " << m(R.viaDiametre) << " / " << m(R.viaPercage)
    << " ; couronne minimale " << m(R.anneauMin) << " ; perçage minimal " << m(R.percageMin) << " ; distance au bord " << m(R.bord) << " ; écart entre perçages "
    << m(R.ecartTrous) << " ; cuivre au perçage non métallisé " << m(R.cuivreTrou) << " ; rayons thermiques minimum " << R.rayonsMin << "\n";
  for (const auto& cl : R.classes) {
    s << "  Classe « " << cl.nom << " » : piste " << m(cl.largeur) << ", isolement " << m(cl.isolement) << ", via " << m(cl.viaDiametre) << " / " << m(cl.viaPercage);
    if (cl.courant > 0)
      s << " ; " << hypothesesIPC2221(cl.courant, cl.echauffement, e.cuivre, true) << " : largeur recommandée " << m(largeurIPC2221(cl.courant, cl.echauffement, e.cuivre, true));
    else
      s << " ; courant non renseigné (aucun dimensionnement électrique vérifié)";
    std::vector<std::string> reseaux;
    for (const auto& [r, k] : R.classeReseau)
      if (k == cl.nom) reseaux.push_back(r);
    s << " ; réseaux : " << (reseaux.empty() ? std::string("aucun") : liste(reseaux)) << "\n";
  }
  char t[200];
  std::snprintf(t, sizeof t, "  Empilage : %d couches de cuivre, %.2f mm, cuivre %.0f µm, %s, finition %s\n", e.couchesCuivre, e.epaisseur, e.cuivre * 1000, e.materiau.c_str(),
                e.finition.c_str());
  s << t;
  return s.str();
}

std::size_t compter(const std::string& texte, const std::string& motif) {
  std::size_t n = 0;
  for (std::size_t i = texte.find(motif); i != std::string::npos; i = texte.find(motif, i + motif.size())) ++n;
  return n;
}
}  // namespace

std::vector<std::string> controlerDossierFabrication(const ConceptionElec& c, const std::string& dossier) {
  namespace fs = std::filesystem;
  std::vector<std::string> d;
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  const ModeleFabrication m = modeleFabrication(c, k, r);
  const std::string base = nomBase(c.nom);
  const fs::path racine = cheminFichier(dossier);
  auto lire = [&](const fs::path& rel, std::string& contenu) {
    try {
      contenu = lireFichier(racine / rel);
    } catch (const ErreurFichier&) {
      d.push_back("fichier manquant : " + rel.generic_string());
      return false;
    }
    if (contenu.empty()) {
      d.push_back("fichier vide : " + rel.generic_string());
      return false;
    }
    return true;
  };
  // Couches Gerber : une par couche du modèle, toutes au format 4.6 en millimètres, attributs X2, terminées
  int cuivres = 0;
  bool contour = false;
  for (const auto& cf : m.couches) {
    std::string code = codeCouche(cf.couche);
    std::replace(code.begin(), code.end(), '.', '_');
    const fs::path rel = fs::path("Gerber") / (base + "-" + code + ".gbr");
    std::string g;
    if (!lire(rel, g)) continue;
    const std::string nom = rel.generic_string();
    if (g.find("%FSLAX46Y46*%") == std::string::npos) d.push_back(nom + " : format de coordonnées 4.6 absent");
    if (g.find("%MOMM*%") == std::string::npos) d.push_back(nom + " : unité millimètre absente");
    if (g.find("%TF.FileFunction,") == std::string::npos) d.push_back(nom + " : fonction de la couche (attribut X2) absente");
    if (g.size() < 4 || g.rfind("M02*") == std::string::npos) d.push_back(nom + " : fichier non terminé (M02 absent)");
    if (estCuivre(cf.couche)) ++cuivres;
    if (cf.couche == Couche::Contour) {
      contour = true;
      // Le contour tracé dans le fichier a exactement l'encombrement de la carte
      Nm x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      bool premier = true;
      for (std::size_t i = g.find("\nX"); i != std::string::npos; i = g.find("\nX", i + 2)) {
        long long x = 0, y = 0;
        if (std::sscanf(g.c_str() + i + 1, "X%lldY%lld", &x, &y) != 2) continue;
        const Pt q = depuisRepereFabrication(boite(m.contour), {static_cast<Nm>(x), static_cast<Nm>(y)});
        const Nm px = q.x, py = q.y;
        if (premier) { x0 = x1 = px; y0 = y1 = py; premier = false; }
        x0 = std::min(x0, px); x1 = std::max(x1, px); y0 = std::min(y0, py); y1 = std::max(y1, py);
      }
      const Boite b = boite(m.contour);
      if (premier || std::llabs(x0 - b.x0) > 1000 || std::llabs(x1 - b.x1) > 1000 || std::llabs(y0 - b.y0) > 1000 || std::llabs(y1 - b.y1) > 1000)
        d.push_back(nom + " : le contour du fichier ne correspond pas à celui de la carte");
    }
  }
  if (cuivres != c.circuit.empilage.couchesCuivre) d.push_back("couches de cuivre : " + std::to_string(cuivres) + " fichier(s) pour " + std::to_string(c.circuit.empilage.couchesCuivre) + " couche(s)");
  if (!contour) d.push_back("couche de contour (Edge.Cuts) absente");
  // Perçages : en-tête métrique, autant de coups que de trous du modèle, tous dans la carte
  const Boite carte = boite(m.contour);
  for (const bool metallises : {true, false}) {
    const fs::path rel = fs::path("Percage") / (base + (metallises ? "-PTH.drl" : "-NPTH.drl"));
    std::string e;
    if (!lire(rel, e)) continue;
    const std::string nom = rel.generic_string();
    if (e.rfind("M48", 0) != 0 || e.find("METRIC") == std::string::npos || e.find("M30") == std::string::npos) d.push_back(nom + " : en-tête Excellon métrique incomplet");
    std::size_t coups = 0;
    for (std::size_t i = e.find("\nX"); i != std::string::npos; i = e.find("\nX", i + 2)) {
      double x = 0, y = 0;
      if (std::sscanf(e.c_str() + i + 1, "X%lfY%lf", &x, &y) != 2) continue;
      ++coups;
      const Pt p = depuisRepereFabrication(carte, {static_cast<Nm>(std::llround(x * 1e6)), static_cast<Nm>(std::llround(y * 1e6))});
      if (!carte.gonflee(1000).contient(p)) d.push_back(nom + " : un trou hors de la carte");
    }
    const std::size_t attendus = static_cast<std::size_t>(std::count_if(m.trous.begin(), m.trous.end(), [&](const TrouFab& t) { return t.metallise == metallises; }));
    if (coups != attendus) d.push_back(nom + " : " + std::to_string(coups) + " trou(s) écrit(s) pour " + std::to_string(attendus) + " dans la carte");
  }
  // Placement et nomenclature
  {
    std::string pl;
    if (lire(fs::path("Assemblage") / (base + "-placement.csv"), pl)) {
      // Même règle que le fichier écrit (F14) : un composant hors carte mais encore posé n'est pas attendu
      std::set<std::string> attendus;
      for (const auto& e : c.circuit.empreintes)
        if (dansFichierPlacement(c, e))
          if (const ComposantElec* x = c.composant(e.composant)) attendus.insert(x->reference);
      if (compter(pl, "\n") != attendus.size() + 1) d.push_back("placement : " + std::to_string(compter(pl, "\n") - 1) + " ligne(s) pour " + std::to_string(attendus.size()) + " composant(s) à poser");
      // Dernière comparaison, ligne à ligne : chaque repère de la nomenclature est dans le placement (EL-04)
      std::set<std::string> places;
      std::istringstream lignes(pl);
      std::string ligne;
      std::getline(lignes, ligne);  // en-tête
      while (std::getline(lignes, ligne)) {
        std::string ref;
        if (!ligne.empty() && ligne[0] == '"') {
          for (std::size_t i = 1; i < ligne.size(); ++i) {
            if (ligne[i] == '"' && i + 1 < ligne.size() && ligne[i + 1] == '"') { ref += '"'; ++i; }
            else if (ligne[i] == '"') break;
            else ref += ligne[i];
          }
        } else ref = ligne.substr(0, ligne.find(','));
        places.insert(ref);
      }
      // Chaque écart nomme le composant en cause
      for (const auto& r : attendus)
        if (!places.count(r)) d.push_back("placement : " + r + " devait y être et manque");
      for (const auto& r : places)
        if (!attendus.count(r)) d.push_back("placement : " + r + " y est sans être un composant à poser");
      for (const auto& l : nomenclature(c))
        for (const auto& r : l.references) {
          const ComposantElec* x = c.parReference(r);
          if (x && !x->horsCarte && !x->horsPlacement && !places.count(r)) d.push_back("placement : " + r + " est dans la nomenclature mais pas dans le fichier de placement");
        }
    }
    std::string bom;
    if (lire(fs::path("Nomenclature") / (base + "-nomenclature.csv"), bom) && compter(bom, "\n") < 1 + nomenclature(c).size())
      d.push_back("nomenclature : lignes manquantes");
    std::string lisez;
    lire("LISEZMOI.txt", lisez);
  }
  return d;
}

Publication publier(ConceptionElec& c, const std::string& dossierParent, const std::string& auteur, const std::string& date, const std::string& derogation, const std::string& version,
                    std::vector<std::string>* fichiers, const std::function<std::vector<std::string>(const std::string&)>& complements) {
  namespace fs = std::filesystem;
  if (c.verrouillee)
    throw std::runtime_error("« " + c.nom + " » est déjà publiée" + (c.publications.empty() ? std::string() : " (" + c.publications.back().id + ")") +
                             " : passe à une nouvelle révision pour la modifier et la publier de nouveau.");
  // Instantané immuable : la validation, les fichiers et l'empreinte viennent de cette copie, jamais du projet
  // en cours de modification. Les empreintes publiées y sont figées (EL-35) : une évolution du générateur ne
  // changera jamais ce qui a été fabriqué. La conception elle-même n'est touchée qu'en cas de succès (F8).
  const std::string avant = empreinteConception(c);
  ConceptionElec inst = c;
  figerEmpreintes(inst);
  const std::string empreinte = empreinteConception(inst);
  const Validation v = valider(inst);
  Publication p;
  const std::string raison = sansEspaces(derogation);
  if (v.critiques() > 0) {
    std::string l;
    int n = 0;
    for (const auto& e : v.etapes)
      for (const auto& d : e.details)
        if (d.gravite == Gravite::Erreur && !derogeable(d.code)) {
          if (n < 6) l += "\n  " + d.code + " : " + d.message;
          ++n;
        }
    throw std::runtime_error("Publication impossible : " + std::to_string(n) + " erreur(s) critique(s), qu’aucune dérogation ne peut lever :" + l + (n > 6 ? "\n  …" : "") +
                             "\nCorrige-les avant de publier.");
  }
  if (!v.publiable()) {
    if (raison.empty())
      throw std::runtime_error("Publication refusée : " + std::to_string(v.bloquants()) + " erreur(s) bloquante(s). Corrige-les, ou donne la raison d’une dérogation.");
    p.derogation = raison;
    for (const auto& e : v.etapes)
      for (const auto& d : e.details)
        if (d.gravite == Gravite::Erreur) p.violations.push_back(d.code + " — " + e.nom + " : " + d.message + (d.objets.empty() ? std::string() : " [" + liste(d.objets) + "]"));
  }
  char id[24];
  std::snprintf(id, sizeof id, "R%03zu", c.publications.size() + 1);
  p.id = id;
  p.revision = inst.cartouche.revision.empty() ? std::string("A") : inst.cartouche.revision;
  p.date = date;
  p.auteur = sansEspaces(auteur).empty() ? inst.cartouche.auteur : sansEspaces(auteur);
  p.empreinte = empreinte;
  p.dossier = nomDeFichier(inst.nom, "carte") + "_rev" + nomDeFichier(p.revision, "A") + "_" + p.id;
  const fs::path parent = cheminFichier(dossierParent);
  const fs::path final = parent / cheminFichier(p.dossier);
  std::error_code ec;
  if (fs::exists(final, ec))
    throw std::runtime_error("Le dossier « " + p.dossier + " » existe déjà : une publication écrite n’est jamais remplacée. Choisis un autre dossier.");
  // Écriture à côté, contrôle, puis renommage : le dossier final est complet, ou n'existe pas.
  const fs::path tmp = parent / cheminFichier("." + p.dossier + ".ecriture");
  fs::remove_all(tmp, ec);
  std::vector<std::string> ecrits;
  try {
    ecrits = ecrireDossierFabrication(inst, utf8(tmp), version);
    const std::vector<std::string> defauts = controlerDossierFabrication(inst, utf8(tmp));
    if (!defauts.empty()) {
      std::string l;
      for (const auto& x : defauts) l += "\n  " + x;
      throw std::runtime_error("Publication interrompue : le dossier de fabrication écrit ne passe pas son contrôle :" + l);
    }
    ecrireFichierAtomique(tmp / "conception.json", versJson(inst).dump(2) + "\n");
    ecrits.push_back("conception.json");
    if (complements)
      for (const auto& f : complements(utf8(tmp))) ecrits.push_back(f);
    for (const auto& f : ecrits) p.fichiers.push_back({f, sha256Hex(lireFichier(tmp / cheminFichier(f)))});
    std::ostringstream s;
    s << "PUBLICATION POUR LA FABRICATION\n\n";
    s << "Produit : " << (inst.cartouche.titre.empty() ? inst.nom : inst.cartouche.titre) << "\n";
    s << "Conception : " << inst.nom << " (" << inst.uuid << ")\n";
    s << "Révision : " << p.revision << "\nPublication : " << p.id << "\n";
    s << "Date : " << p.date << "\nAuteur : " << (p.auteur.empty() ? std::string("non renseigné") : p.auteur) << "\n";
    s << "Empreinte SHA-256 de la conception publiée : " << p.empreinte << "\n";
    s << "Instantané de la conception : conception.json (relisible dans Prométhée)\n";
    s << "Schéma, circuit et nomenclature : révision " << p.revision << " ; données de fabrication : " << p.id << "\n";
    s << "Produit par " << ligneVersion() << ", identifiant " << infosVersion().identifiant << "\n\n";
    s << texteRegles(inst) << "\nValidation :\n";
    for (const auto& e : v.etapes) s << "  " << e.nom << " : " << e.score << " % - " << e.erreurs << " erreur(s), " << e.avertissements << " avertissement(s)\n";
    if (!p.derogation.empty()) {
      s << "\nDÉROGATION : publiée malgré " << p.violations.size() << " erreur(s) dérogeable(s), accordée par " << (p.auteur.empty() ? std::string("non renseigné") : p.auteur)
        << " le " << p.date << ".\nRaison : " << p.derogation << "\nErreurs acceptées :\n";
      for (const auto& x : p.violations) s << "  - " << x << "\n";
    }
    s << "\nLimites : vérification de règles géométriques et électriques de base. Pas de simulation (SPICE, intégrité du signal, thermique). "
         "Faire contrôler les fichiers par un visualiseur Gerber indépendant et par le fabricant avant production.\n";
    s << "\nFichiers (SHA-256) :\n";
    for (const auto& [f, h] : p.fichiers) s << "  " << h << "  " << f << "\n";
    ecrireFichierAtomique(tmp / "PUBLICATION.txt", s.str());
    ecrits.push_back("PUBLICATION.txt");
    p.fichiers.push_back({"PUBLICATION.txt", sha256Hex(s.str())});
    std::string sommes;
    for (const auto& [f, h] : p.fichiers) sommes += h + "  " + f + "\n";
    ecrireFichierAtomique(tmp / "SHA256SUMS.txt", sommes);
    ecrits.push_back("SHA256SUMS.txt");
    if (empreinteConception(c) != avant) throw std::runtime_error("La conception a changé pendant la publication : recommence.");
    fs::rename(tmp, final);
  } catch (...) {
    fs::remove_all(tmp, ec);
    throw;
  }
  c.empreintes = inst.empreintes;  // les empreintes figées de l'instantané publié
  c.publications.push_back(p);
  c.verrouillee = true;
  if (fichiers) *fichiers = ecrits;
  return p;
}

std::vector<std::string> verifierPublication(const Publication& p, const std::string& dossierParent) {
  namespace fs = std::filesystem;
  std::vector<std::string> ecarts;
  if (p.fichiers.empty()) {
    ecarts.push_back("publication antérieure à la version 0.25 : aucune empreinte de fichier à vérifier");
    return ecarts;
  }
  const fs::path racine = cheminFichier(dossierParent) / cheminFichier(p.dossier);
  for (const auto& [f, h] : p.fichiers) {
    try {
      if (sha256Hex(lireFichier(racine / cheminFichier(f))) != h) ecarts.push_back(f + " a été modifié depuis la publication");
    } catch (const ErreurFichier&) {
      ecarts.push_back(f + " manque");
    }
  }
  return ecarts;
}

void nouvelleRevision(ConceptionElec& c) {
  c.verrouillee = false;
  c.cartouche.revision = revisionSuivante(c.cartouche.revision);
}

}  // namespace prom::ecad
