// Prométhée : essais de la 0.34, lot L12 (travail à plusieurs) — bornier de la carte câblé dans un folio (12.6),
// matériel libre (12.11), revue formelle et son PDF figé (12.13), bibliothèque d'équipe hors réseau (12.15), Git et
// contribution d'article sans réseau (12.16), nomenclature commune du système (12.18), liens entre disciplines
// (12.20). Licence GPL-3.0-only.
#include <algorithm>
#include <filesystem>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/archive.hpp"
#include "promethee/electrique.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/git_projet.hpp"
#include "promethee/hachage.hpp"
#include "promethee/modele.hpp"
#include "promethee/pdf_documents.hpp"
#include "promethee/processus.hpp"
#include "promethee/projet_partage.hpp"

using namespace prom;
using namespace prom::ecad;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {
// Montage : symboles posés en ligne, chaque broche reliée par une étiquette globale au réseau voulu
struct Montage12 {
  ConceptionElec c;
  double x = 25.4;
  explicit Montage12(const std::string& nom) : c(nouvelleConception(nom)) {}
  ComposantElec* poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons,
                       const std::string& mpn = {}) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, 50.8));
    x += 25.4;
    ComposantElec* r = nullptr;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        r = c.composant(p.composant);
        r->reference = ref;
        r->valeur = valeur;
        r->mpn = mpn;
        for (const auto& b : brochesPlacees(p, *c.symbole(p.symbole)))
          for (const auto& [num, reseau] : liaisons)
            if (b.broche->numero == num) {
              const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
              const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
              c.feuilles[0].fils.push_back({nouvelUuid(), b.p, bout});
              Etiquette e;
              e.uuid = nouvelUuid();
              e.texte = reseau;
              e.p = bout;
              e.type = TypeEtiquette::Globale;
              c.feuilles[0].etiquettes.push_back(e);
            }
      }
    return r;
  }
};
std::string dossierEssai(const std::string& nom) {
  std::random_device r;
  const fs::path p = fs::temp_directory_path() / ("promethee-l12-" + nom + "-" + std::to_string(r()));
  fs::create_directories(p);
  return utf8(p);
}
// Pose un appareil personnel (alimentation) dont les bornes portent des potentiels
std::string poserAlimentation(elec::ProjetElectrique& pe, std::size_t folio) {
  elec::SpecAppareil a;
  a.nom = "Alimentation 24 V";
  a.prefixe = "G";
  a.bas = {"+", "-"};
  const std::string id = elec::enregistrerAppareil(pe, a);
  const std::string s = elec::poserAppareil(pe, folio, id, ptMm(60, 60));
  const SymbolePlace* sp = nullptr;
  for (const auto& x : pe.schema.feuilles[folio].symboles)
    if (x.uuid == s) sp = &x;
  REQUIRE(sp);
  const SymbolePlace place = *sp;
  const DefSymbole& d = pe.schema.symboles.at(id);
  for (const auto& b : brochesPlacees(place, d)) {
    const Pt dir = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
    const Pt bout{b.p.x - dir.x * 10'000'000, b.p.y - dir.y * 10'000'000};
    pe.schema.feuilles[folio].fils.push_back({nouvelUuid(), b.p, bout});
    elec::poserPotentiel(pe, folio, b.broche->numero == "+" ? "+24V" : "0V", bout);
  }
  return place.composant;
}
}  // namespace

TEST_CASE("12.6 · bornier de la carte câblé dans un folio de l'atelier électrique", "[034][L12][12.6]") {
  Montage12 m("Capteur");
  m.poser("Bornier_1x04", "J1", "Bornier", {{"1", "+24V"}, {"2", "GND"}, {"3", "SORTIE"}});
  m.poser("C", "C1", "100n", {{"1", "+24V"}, {"2", "GND"}});
  m.poser("R", "R1", "10k", {{"1", "SORTIE"}, {"2", "GND"}});
  elec::ProjetElectrique pe = elec::nouveauProjetElectrique("Machine");
  const std::size_t folio = elec::ajouterFolio(pe, elec::TypeFolio::Commande, "Capteurs");
  const std::string alim = poserAlimentation(pe, folio);
  const CarteAppareil r = poserCarteDansFolio(pe, m.c, "J1", folio, ptMm(140, 60), {{"GND", "0V"}});
  INFO(r.erreur);
  REQUIRE(r.erreur.empty());
  CHECK(r.bornes == std::vector<std::string>{"1", "2", "3", "4"});
  CHECK(r.potentiels == std::vector<std::string>{"+24V", "0V", "SORTIE", ""});
  const ComposantElec* carte = pe.schema.composant(r.composant);
  REQUIRE(carte);
  CHECK(carte->reference.rfind("A", 0) == 0);
  CHECK(carte->champs.at("carte") == m.c.uuid);
  // Le carnet de câblage relie les bornes de la carte à celles de l'alimentation, par leurs potentiels
  const ComposantElec* g = pe.schema.composant(alim);
  REQUIRE(g);
  bool plus = false, moins = false;
  for (const auto& l : elec::carnetCablage(pe)) {
    UNSCOPED_INFO(l.de << " → " << l.vers << " (" << l.fil << ")");
    const auto relie = [&](const std::string& a, const std::string& b) { return (l.de.find(a) != std::string::npos && l.vers.find(b) != std::string::npos) || (l.de.find(b) != std::string::npos && l.vers.find(a) != std::string::npos); };
    plus |= relie(carte->reference + ":1", g->reference + ":+");
    moins |= relie(carte->reference + ":2", g->reference + ":-");
  }
  CHECK(plus);
  CHECK(moins);
  // La carte dans l'armoire, à sa taille
  if (pe.armoires.empty()) pe.armoires.emplace_back();
  CHECK(implanterCarte(pe, 0, r.composant, m.c, ptMm(40, 40)));
  CHECK(pe.armoires[0].implantations.size() == 1);
  const auto e = elec::encombrement(pe, r.composant);
  CHECK(e.largeur == Approx(50).margin(0.01));
  CHECK(e.hauteur == Approx(40).margin(0.01));
  // Connecteur inconnu : refusé avec sa raison
  CHECK(!poserCarteDansFolio(pe, m.c, "J9", folio, ptMm(0, 0)).erreur.empty());
}

TEST_CASE("12.11 · « Publier en matériel libre » sous CERN-OHL-S : paquet rouvert sur un poste neuf, projet identique, licence et visionneuse présentes", "[034][L12][12.11]") {
  Projet p = projetVide();
  p.nom = "Clignotant libre";
  ConceptionElec c = exempleElectronique();
  Article a;
  a.id = "led-rouge";
  a.nom = "LED rouge 0603";
  a.mpn = "LTST-C191KRKT";
  a.licence = "CC-BY-4.0";
  c.articles[a.id] = a;
  Article b;
  b.id = "ne555";
  b.nom = "NE555";
  c.articles[b.id] = b;
  p.electronique.push_back(c);
  OptionsMaterielLibre o;
  o.detenteur = "Évan";
  o.annee = "2026";
  o.description = "Clignotant à NE555, pour apprendre.";
  const PaquetMaterielLibre pk = publierMaterielLibre(p, {}, o);
  INFO(pk.erreur);
  REQUIRE(pk.erreur.empty());
  REQUIRE(!pk.zip.empty());
  auto contient = [&](const std::string& fin) { return std::any_of(pk.fichiers.begin(), pk.fichiers.end(), [&](const std::string& f) { return f.size() >= fin.size() && f.compare(f.size() - fin.size(), fin.size(), fin) == 0; }); };
  CHECK(contient("/LICENSE"));
  CHECK(contient("/README.md"));
  CHECK(contient("/sources/MANIFESTE.json"));
  CHECK(std::any_of(pk.fichiers.begin(), pk.fichiers.end(), [](const std::string& f) { return f.find("/visionneuse/") != std::string::npos && f.size() > 5 && f.substr(f.size() - 5) == ".html"; }));
  CHECK(std::any_of(pk.fichiers.begin(), pk.fichiers.end(), [](const std::string& f) { return f.find("/fabrication/") != std::string::npos; }));
  CHECK(std::any_of(pk.alertes.begin(), pk.alertes.end(), [](const std::string& x) { return x.find("sans licence") != std::string::npos; }));
  std::string licence, lisez;
  for (const auto& [n, contenu] : lireZip(pk.zip)) {
    if (n.size() >= 8 && n.substr(n.size() - 8) == "/LICENSE") licence = contenu;
    if (n.size() >= 10 && n.substr(n.size() - 10) == "/README.md") lisez = contenu;
  }
  CHECK(licence.find("SPDX-License-Identifier: CERN-OHL-S-2.0") != std::string::npos);
  CHECK(licence.find("https://ohwr.org/cern_ohl_s_v2.txt") != std::string::npos);
  CHECK(licence.find("Évan") != std::string::npos);
  CHECK(lisez.find("CC-BY-4.0") != std::string::npos);
  // « Poste neuf » : un dossier vide, le paquet rouvert
  const std::string dossier = dossierEssai("libre");
  std::string erreur;
  const std::string chemin = ouvrirPaquetMaterielLibre(pk.zip, dossier, &erreur);
  INFO(erreur);
  REQUIRE(!chemin.empty());
  const Projet relu = lireProjet(chemin);
  Projet attendu = p;
  attendu.licence = "CERN-OHL-S-2.0";
  CHECK(relu.licence == "CERN-OHL-S-2.0");
  CHECK(versJson(relu).dump() == versJson(normaliser(versJson(attendu))).dump());
  CHECK(licenceLibre("CERN-OHL-S-2.0")->reciproque);
  CHECK(!publierMaterielLibre(p, {}, OptionsMaterielLibre{"MIT-HW"}).erreur.empty());
  fs::remove_all(cheminFichier(dossier));
}

TEST_CASE("12.13 · revue de la révision B : constats clos, deux signatures, PDF figé qui porte l'empreinte SHA-256 de la révision revue", "[034][L12][12.13]") {
  ConceptionElec c = exempleElectronique();
  c.cartouche.revision = "B";
  const std::string empreinte = empreinteRevision(c);
  CHECK(empreinte.size() == 64);
  const RevueFormelle r = ouvrirRevue(c, "Revue de la révision B", "2026-10-25", {{"Découplage de chaque circuit intégré", "règle maison", "U1", true}});
  CHECK(r.numero == 1);
  CHECK(r.empreinte == empreinte);
  CHECK(empreinteRevision(c) == empreinte);  // la revue elle-même ne change pas l'empreinte
  const std::string k1 = ajouterConstat(c, 1, "R1", "Valeur à confirmer par le calcul de la fréquence", "Lou", "2026-10-25");
  const std::string k2 = ajouterConstat(c, 1, "C1", "Tension de service à indiquer", "Lou", "2026-10-25");
  CHECK(k1 == "R1-1");
  CHECK(k2 == "R1-2");
  CHECK(signer(c, 1, "Concepteur", "Évan", "2026-10-26").find("ouvert") != std::string::npos);
  CHECK(clore(c, 1, k1, "Calcul joint : 1,44/((R1+2R2)C) = 1,1 Hz", "Évan", "2026-10-26"));
  CHECK(clore(c, 1, k2, "50 V ajoutés au champ Valeur", "Évan", "2026-10-26"));
  CHECK(!clore(c, 1, k2, "deux fois", "Évan", "2026-10-26"));
  CHECK(signer(c, 1, "Concepteur", "Évan", "2026-10-26").empty());
  CHECK(!revuesDe(c)[0].close());
  CHECK(!signer(c, 1, "Concepteur", "Évan", "2026-10-26").empty());  // deux fois le même rôle
  CHECK(signer(c, 1, "Relecteur", "Lou", "2026-10-27").empty());
  const RevueFormelle fin = revuesDe(c).at(0);
  CHECK(fin.close());
  REQUIRE(fin.empreintePdf.size() == 64);
  // Le rapport porte l'empreinte entière, les constats clos et les deux signatures ; il est figé (même octets)
  const std::string pdf = pdfRevue(c, fin);
  CHECK(pdf.rfind("%PDF-1.7", 0) == 0);
  CHECK(sha256Hex(pdf) == fin.empreintePdf);
  const std::string texte = textePdfSimple(pdf);
  INFO(texte);
  CHECK(texte.find(empreinte) != std::string::npos);
  CHECK(texte.find("Évan") != std::string::npos);
  CHECK(texte.find("Lou") != std::string::npos);
  CHECK(texte.find("clos") != std::string::npos);
  // Le PDF se lit par un lecteur indépendant s'il est installé (poppler)
  if (!programmeDuPoste("pdftotext").empty()) {
    const std::string dossier = dossierEssai("revue");
    const fs::path f = cheminFichier(dossier) / "revue.pdf";
    ecrireFichierAtomique(f, pdf);
    const ResultatProcessus t = executerProcessus({"pdftotext", utf8(f), "-"});
    REQUIRE(t.reussi());
    CHECK(t.sortie.find(empreinte) != std::string::npos);
    fs::remove_all(cheminFichier(dossier));
  }
  // Conception changée après l'ouverture d'une revue : la signature est refusée
  const RevueFormelle r2 = ouvrirRevue(c, "Revue de la révision C", "2026-10-28");
  CHECK(r2.numero == 2);
  c.composants[0].valeur = "4k7";
  CHECK(signer(c, 2, "Concepteur", "Évan", "2026-10-28").find("a changé") != std::string::npos);
  // Le tout survit à l'enregistrement
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  CHECK(revuesDe(relue) == revuesDe(c));
  CHECK(pdfHistorique(c).rfind("%PDF", 0) == 0);
}

TEST_CASE("12.15 · poste débranché : trois articles créés sur la copie locale ; rebranché, ils rejoignent la bibliothèque d'équipe sans doublon", "[034][L12][12.15]") {
  ContenuBibliotheque equipe;
  equipe.nom = "Équipe";
  auto article = [](const std::string& id, const std::string& mpn) {
    Article a;
    a.id = id;
    a.nom = id;
    a.mpn = mpn;
    return a;
  };
  equipe.articles = {article("lm358", "LM358DR"), article("ne555", "NE555P")};
  CopieLocale copie = copierPourHorsReseau(equipe, "2026-10-25");
  // Relue depuis son fichier, comme au retour d'un déplacement
  std::string erreur;
  const auto relue = lireCopieLocale(texteCopieLocale(copie), &erreur);
  INFO(erreur);
  REQUIRE(relue);
  CHECK(relue->locale.articles.size() == 2);
  copie = *relue;
  // Hors réseau : trois articles créés, un article modifié
  copie.locale.articles.push_back(article("bc547", "BC547B"));
  copie.locale.articles.push_back(article("1n4148", "1N4148W"));
  copie.locale.articles.push_back(article("regulateur", "LM-317 T"));
  copie.locale.articles[0].description = "Double ampli opérationnel, boîtier SOIC-8";
  DefSymbole s = *symboleBibliotheque("R");
  s.id = "perso:bc547";
  copie.locale.symboles.push_back(s);
  // Pendant ce temps, un autre poste publie le LM317T
  equipe.articles.push_back(article("lm317", "LM317T"));
  const BilanReversement b = reverser(equipe, copie);
  INFO(b.texte());
  CHECK(b.ajoutes == std::vector<std::string>{"bc547", "1n4148"});
  REQUIRE(b.rattaches.count("regulateur"));
  CHECK(b.rattaches.at("regulateur") == "lm317");
  CHECK(b.modifies == std::vector<std::string>{"lm358"});
  CHECK(b.conflits.empty());
  CHECK(b.symboles == 1);
  CHECK(equipe.articles.size() == 5);
  std::set<std::string> references;
  for (const auto& a : equipe.articles) references.insert(referenceComparable(a.mpn));
  CHECK(references.size() == equipe.articles.size());  // aucun doublon
  for (const auto& a : equipe.articles)
    if (a.id == "lm358") CHECK(a.version == 2);
  // La copie repart de l'état de l'équipe ; un second reversement n'ajoute rien
  CHECK(copie.locale.articles.size() == 5);
  const BilanReversement b2 = reverser(equipe, copie);
  CHECK(b2.ajoutes.empty());
  CHECK(b2.modifies.empty());
  CHECK(equipe.articles.size() == 5);
  // Modifié des deux côtés : conflit signalé, l'équipe garde sa version
  CopieLocale c3 = copierPourHorsReseau(equipe, "2026-10-26");
  c3.locale.articles[1].description = "poste";
  for (auto& a : equipe.articles)
    if (a.id == c3.locale.articles[1].id) a.description = "équipe";
  const BilanReversement b3 = reverser(equipe, c3);
  CHECK(b3.conflits.size() == 1);
}

TEST_CASE("12.16 · article proposé avec son symbole, son empreinte et son modèle 3D : la branche de contribution emporte les trois ; sans réseau activé, aucune connexion relevée", "[034][L12][12.16]") {
  CHECK(adresseReseau("https://exemple.org/biblio.git"));
  CHECK(adresseReseau("ssh://git@exemple.org/biblio.git"));
  CHECK(adresseReseau("git@github.com:equipe/biblio.git"));
  CHECK(adresseReseau("serveur.lan:depots/biblio.git"));
  CHECK(!adresseReseau("/home/lou/depots/biblio.git"));
  CHECK(!adresseReseau("C:\\depots\\biblio"));
  CHECK(!adresseReseau("file:///home/lou/biblio.git"));
  if (!gitDisponible()) SKIP("git n'est pas installé");
  const std::string dossier = dossierEssai("git");
  DepotGit d;
  d.dossier = utf8(cheminFichier(dossier) / "bibliotheque");
  d.auteur = "Évan";
  d.courriel = "evan@exemple.org";
  REQUIRE(gitInitialiser(d).ok);
  ecrireFichierAtomique(cheminFichier(d.dossier) / "LISEZMOI.md", "Bibliothèque de l'équipe\n");
  const ResultatGit v = gitValider(d, "Bibliothèque de l'équipe");
  INFO(v.message);
  REQUIRE(v.ok);
  CHECK(gitBrancheCourante(d) == "principale");
  Article a;
  a.id = "capteur-temp";
  a.nom = "Capteur de température TMP117";
  a.mpn = "TMP117AIDRVR";
  a.licence = "CC-BY-4.0";
  DefSymbole s = *symboleBibliotheque("R");
  s.id = "capteur-temp";
  const DefEmpreinte* e0 = empreinteEnCache("R_0603");
  REQUIRE(e0);
  DefEmpreinte e = *e0;
  e.id = "capteur-temp";
  const fs::path step = cheminFichier(dossier) / "tmp117.step";
  ecrireFichierAtomique(step, "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\nENDSEC;\nEND-ISO-10303-21;\n");
  const ContributionArticle c = contribuerArticle(d, a, &s, &e, utf8(step));
  INFO(c.erreur);
  REQUIRE(c.ok);
  CHECK(c.branche == "contribution/capteur-temp");
  const auto fichiers = gitFichiersDeBranche(d, c.branche);
  for (const char* f : {"articles/capteur-temp.json", "symboles/capteur-temp.json", "empreintes/capteur-temp.json", "modeles3d/tmp117.step"})
    CHECK(std::find(fichiers.begin(), fichiers.end(), f) != fichiers.end());
  CHECK(gitBrancheCourante(d) == "principale");
  CHECK(!fs::exists(cheminFichier(d.dossier) / "articles"));
  const auto branches = gitBranches(d);
  CHECK(std::find(branches.begin(), branches.end(), "contribution/capteur-temp") != branches.end());
  // Dépôt distant en ssh rattaché sans échange ; pousser, tirer et l'essai de connexion sont refusés sans réseau
  REQUIRE(gitRattacherDistant(d, "origin", "ssh://git@exemple.org/biblio.git").ok);
  effacerJournalProgrammes();
  const ResultatGit p = gitPousser(d, "origin", "principale");
  CHECK(!p.ok);
  CHECK(p.message.find("réseau") != std::string::npos);
  CHECK(!gitTirer(d, "origin", "principale").ok);
  CHECK(!gitEssaiConnexion(d, "origin").ok);
  CHECK(!gitCloner("https://exemple.org/biblio.git", utf8(cheminFichier(dossier) / "clone"), false).ok);
  for (const auto& l : programmesLances())
    for (const auto& arg : l) {
      CHECK(arg != "push");
      CHECK(arg != "pull");
      CHECK(arg != "ls-remote");
      CHECK(arg != "clone");
    }
  // Un dépôt d'équipe dans un dossier partagé (chemin de fichiers) se pousse sans réseau
  const std::string partage = utf8(cheminFichier(dossier) / "partage.git");
  REQUIRE(executerProcessus({"git", "init", "-q", "--bare", partage}).reussi());
  REQUIRE(gitRattacherDistant(d, "equipe", partage).ok);
  const ResultatGit pp = gitPousser(d, "equipe", c.branche);
  INFO(pp.message);
  CHECK(pp.ok);
  // Git coupé pour un projet : plus aucune commande
  DepotGit coupe = d;
  coupe.coupe = true;
  CHECK(gitValider(coupe, "rien").message.find("coupé") != std::string::npos);
  fs::remove_all(cheminFichier(dossier));
}

TEST_CASE("12.18 · projet de deux cartes et un faisceau : quantités par carte en colonnes, fils en lignes, coût de 10 systèmes égal au calcul à la main", "[034][L12][12.18]") {
  Montage12 a("Carte A"), b("Carte B");
  a.poser("Conn_01x04", "J1", "XH 4 points", {{"1", "+5V"}, {"2", "GND"}, {"3", "TX"}, {"4", "RX"}}, "B4B-XH-A");
  for (const char* r : {"R1", "R2", "R3"}) a.poser("R", r, "10k", {{"1", "+5V"}, {"2", "TX"}}, "RC0603FR-0710KL");
  a.poser("LED", "D1", "rouge", {{"1", "GND"}, {"2", "RX"}}, "LTST-C191KRKT");
  b.poser("Conn_01x04", "J1", "XH 4 points", {{"1", "+5V"}, {"2", "GND"}, {"3", "TX"}, {"4", "RX"}}, "B4B-XH-A");
  for (const char* r : {"R1", "R2"}) b.poser("R", r, "10k", {{"1", "TX"}, {"2", "GND"}}, "RC0603FR-07-10KL");  // même pièce, écrite avec un tiret
  b.poser("C", "C1", "100n", {{"1", "+5V"}, {"2", "GND"}}, "CL10B104KB8NNNC");
  Projet p = projetVide();
  p.electronique = {a.c, b.c};
  LiaisonSysteme w;
  w.uuid = nouvelUuid();
  w.nom = "W1";
  w.genre = "faisceau";
  w.carteA = a.c.uuid, w.connecteurA = "J1";
  w.carteB = b.c.uuid, w.connecteurB = "J1";
  w.longueur = 500;
  w.fil = "Fil 0,25 mm²";
  p.liaisons.push_back(w);
  const std::map<std::string, double> prix = {{"RC0603FR-0710KL", 0.01}, {"LTST-C191KRKT", 0.05}, {"CL10B104KB8NNNC", 0.02}, {"B4B-XH-A", 0.10}, {"Fil 0,25 mm²", 0.30}};
  const std::vector<DonneeAchat> achats = lireDonneesAchat("mpn;distributeur;stock;délai;agréé;risque;prix\nLTST-C191KRKT;Mouser;12000;3;oui;faible;0,05\nLTST-C191KRKT;Marché;500;20;non;élevé;0,01\n");
  REQUIRE(achats.size() == 2);
  CHECK(!achats[1].agree);
  const NomenclatureSysteme n = nomenclatureSysteme(p, prix, achats, {"LTST-C191KRKT"});
  CHECK(n.colonnes == std::vector<std::string>{"Carte A", "Carte B", "W1"});
  const LigneNomenclatureSysteme* r10k = nullptr;
  const LigneNomenclatureSysteme* fil = nullptr;
  const LigneNomenclatureSysteme* led = nullptr;
  for (const auto& l : n.lignes) {
    UNSCOPED_INFO(l.designation << " : " << l.total << " " << l.unite << " à " << l.prix);
    if (referenceComparable(l.mpn) == "RC0603FR0710KL") r10k = &l;
    if (l.genre == "fil") fil = &l;
    if (l.mpn == "LTST-C191KRKT") led = &l;
  }
  REQUIRE(r10k);
  CHECK(r10k->parColonne.at("Carte A") == 3);
  CHECK(r10k->parColonne.at("Carte B") == 2);
  CHECK(r10k->total == 5);
  REQUIRE(fil);
  CHECK(fil->parColonne.at("W1") == Approx(2.0));  // quatre fils de 500 mm
  CHECK(fil->unite == "m");
  REQUIRE(led);
  CHECK(led->achats.size() == 2);
  CHECK(led->prefere);
  CHECK(n.sansPrix().empty());
  // À la main : 5 × 0,01 + 0,05 + 0,02 + 2 × 0,10 + 2 m × 0,30 = 0,92 € par système
  CHECK(n.cout(1) == Approx(0.92));
  CHECK(n.cout(10) == Approx(9.20));
  CHECK(n.csv().find("W1") != std::string::npos);
  // Le faisceau de la liaison : quatre fils, du J1 de A au J1 de B, aux réseaux de la carte A
  const FaisceauEntreCartes f = faisceauDeLiaison(p, w);
  INFO(f.erreur);
  REQUIRE(f.fils.size() == 4);
  CHECK(f.fils[0].de == "J1:1");
  CHECK(f.fils[0].vers == "J1:1");
  CHECK(f.fils[0].reseau == "+5V");
  CHECK(f.fils[2].reseau == "TX");
  CHECK(f.faisceau.branches.size() == 1);
  CHECK(f.faisceau.branches[0].longueur == Approx(500));
  // La liaison garde sa longueur et son fil à l'enregistrement
  const Projet relu = normaliser(versJson(p));
  REQUIRE(relu.liaisons.size() == 1);
  CHECK(relu.liaisons[0].longueur == Approx(500));
  CHECK(relu.liaisons[0].fil == "Fil 0,25 mm²");
}

TEST_CASE("12.20 · contour de la carte agrandi de 2 mm : boîtier marqué « à revoir » avec ce seul changement, jusqu'à l'accusé de lecture", "[034][L12][12.20]") {
  Projet p = projetVide();
  p.electronique.push_back(exempleElectronique());
  ConceptionElec& c = p.electronique[0];
  c.circuit.contour.libre.clear();
  c.circuit.contour.largeur = nm(50);
  c.circuit.contour.hauteur = nm(40);
  p.liensDisciplines.push_back(lierDisciplines(p, "carte:" + c.uuid, "boitier", "contour", "Lou", "2026-10-25"));
  const std::string id = p.liensDisciplines[0].id;
  CHECK(verifierLiens(p) == 0);
  CHECK(p.liensDisciplines[0].etat == "à jour");
  // Une piste ajoutée ne touche pas au contour : rien à revoir
  c.circuit.pistes.push_back({"p1", "N", Couche::CuivreDessus, ptMm(5, 5), ptMm(10, 5), nm(0.25)});
  CHECK(verifierLiens(p) == 0);
  // Contour agrandi de 2 mm
  c.circuit.contour.largeur = nm(52);
  CHECK(verifierLiens(p) == 1);
  const LienDisciplines& l = p.liensDisciplines[0];
  CHECK(l.etat == "à revoir");
  REQUIRE(l.changements.size() == 1);
  CHECK(l.changements[0] == "contour.largeur : 50 → 52 mm");
  CHECK(verifierLiens(p) == 0);  // déjà à revoir
  CHECK(p.liensDisciplines[0].etat == "à revoir");
  // Gardé avec le projet
  const Projet relu = normaliser(versJson(p));
  REQUIRE(relu.liensDisciplines.size() == 1);
  CHECK(relu.liensDisciplines[0] == p.liensDisciplines[0]);
  // Accusé de lecture : à jour
  CHECK(accuserLecture(p, id, "Évan", "2026-10-26"));
  CHECK(p.liensDisciplines[0].etat == "à jour");
  CHECK(p.liensDisciplines[0].changements.empty());
  CHECK(p.liensDisciplines[0].accusePar == "Évan");
  CHECK(verifierLiens(p) == 0);
  // Connecteurs surveillés pour l'armoire : un connecteur déplacé se dit avec sa nouvelle place
  p.liensDisciplines.push_back(lierDisciplines(p, "carte:" + c.uuid, "armoire:1", "connecteurs", "Lou", "2026-10-26"));
  bool deplace = false;
  for (auto& e : c.circuit.empreintes)
    if (const ComposantElec* k = c.composant(e.composant); k && !deplace && !k->reference.empty() && std::string("JXP").find(k->reference[0]) != std::string::npos) {
      e.position.x += nm(1);
      deplace = true;
    }
  if (deplace) {
    CHECK(verifierLiens(p) == 1);
    CHECK(p.liensDisciplines[1].changements.size() == 1);
  }
}
