// Prométhée : essais du lot L4 (empreintes et bibliothèque). Licence GPL-3.0-only.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/archive.hpp"
#include "promethee/articles.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/valeurs.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
std::string composantDe(const ConceptionElec& c, const std::string& symbolePose) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == symbolePose) return s.composant;
  return {};
}
// Réseau de la pastille « numero » du composant, d'après la connectivité (broche de même numéro)
std::string brocheNommee(const ConceptionElec& c, const std::string& composant, const std::string& nom) {
  const ComposantElec* k = c.composant(composant);
  const DefSymbole* d = k ? c.symbole(k->symbole) : nullptr;
  if (d)
    for (const auto& b : d->broches)
      if (b.nom == nom) return b.numero;
  return {};
}
}  // namespace

TEST_CASE("4.6 : une inductance à deux broches ne se voit proposer que les six empreintes d'inductance", "[L4][EMP-01][P7]") {
  const DefSymbole* l = symboleBibliotheque("L");
  REQUIRE(l);
  const auto filtrees = empreintesCompatibles(*l, true);
  REQUIRE(filtrees.size() == 6);
  for (const auto& e : filtrees) CHECK(e.id.rfind("L_", 0) == 0);
  const auto toutes = empreintesCompatibles(*l, false);
  CHECK(toutes.size() > filtrees.size());
  CHECK(std::any_of(toutes.begin(), toutes.end(), [](const InfoEmpreinte& e) { return e.id.rfind("Fuse_", 0) == 0; }));
  // Un circuit intégré à huit broches : les boîtiers de CI à huit pastilles (SOIC, TSSOP, DIP)
  const auto ne555 = empreintesCompatibles(*symboleBibliotheque("NE555"), true);
  CHECK(std::any_of(ne555.begin(), ne555.end(), [](const InfoEmpreinte& e) { return e.id == "DIP-8"; }));
  CHECK(std::none_of(ne555.begin(), ne555.end(), [](const InfoEmpreinte& e) { return e.id.rfind("PinHeader", 0) == 0; }));
}

TEST_CASE("4.4, 4.8 : « 10k 0603 » pose une résistance de 10 kΩ en 0603, avec sa hauteur et son corps", "[L4][BIB-05][BIB-02]") {
  const IndexArticles index(bibliothequeArticles());
  CHECK(bibliothequeArticles().size() >= 290);
  const auto r = index.chercher("10k 0603");
  REQUIRE_FALSE(r.empty());
  CHECK(r.front().article.valeur == "10k");
  CHECK(r.front().article.empreinte == "R_0603");
  // Synonymes et boîtiers
  const auto self = index.chercher("self 1210");
  REQUIRE_FALSE(self.empty());
  CHECK(self.front().article.empreinte == "L_1210");
  const auto soic = index.chercher("soic8");
  REQUIRE_FALSE(soic.empty());
  CHECK(soic.front().article.empreinte == "SOIC-8");
  const auto faute = index.chercher("regulatuer 3.3");  // deux lettres inversées
  REQUIRE_FALSE(faute.empty());
  CHECK(faute.front().article.id == "AMS1117-3.3");
  // Pose dans une conception : valeur, empreinte, hauteur générique de la 0603, corps
  ConceptionElec c = nouvelleConception("Recherche");
  const std::string s = poserArticle(c, 0, r.front().article, ptMm(50, 50));
  const ComposantElec* k = c.composant(composantDe(c, s));
  REQUIRE(k);
  CHECK(k->valeur == "10k");
  CHECK(k->empreinte == "R_0603");
  CHECK(k->article == r.front().article.id);
  CHECK(synchroniser(c).empty());
  REQUIRE(c.circuit.empreintes.size() == 1);
  const auto corps = corpsPose(c, c.circuit.empreintes.front());
  REQUIRE(corps);
  CHECK(corps->hauteur.mm > 0.2);
  // Un passif que la bibliothèque n'a pas : composé à la volée (« condo 100n 50v »)
  const auto condo = index.chercher("condo 100n 50v");
  REQUIRE_FALSE(condo.empty());
  CHECK(condo.front().article.symbole == "C");
  CHECK(condo.front().article.empreinte == "C_0603");
  CHECK(condo.front().article.parametres.count("tension"));
  const auto pc = passifCompose("22k 1206");
  REQUIRE(pc);
  CHECK(pc->empreinte == "R_1206");
  CHECK(pc->valeur == "22k");
}

TEST_CASE("4.15 : requêtes riches, « Résistance>1k -0402 » sur 10 000 articles", "[L4][BIB-06]") {
  std::vector<Article> grand;
  while (grand.size() < 10'000)
    for (const auto& a : bibliothequeArticles()) {
      Article b = a;
      b.id += "_" + std::to_string(grand.size());
      grand.push_back(b);
      if (grand.size() >= 10'000) break;
    }
  const IndexArticles index(grand);
  const auto debut = std::chrono::steady_clock::now();
  const auto r = index.chercher("Résistance>1k -0402", 10'000);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - debut).count();
  WARN("Résistance>1k -0402 sur 10 000 articles : " << ms << " ms, " << r.size() << " résultats");
  REQUIRE_FALSE(r.empty());
  for (const auto& x : r) {
    CHECK(x.article.empreinte.find("0402") == std::string::npos);
    const ValeurComprise v = lireValeurComposant(x.article.valeur, FamilleValeur::Resistance);
    CHECK(v.nombre > 1000);
  }
  CHECK(ms < 300);  // l'objectif de 30 ms se mesure en Release sur un poste ; l'essai garde une marge
  // Phrase exacte, joker, expression régulière, comparaison sur un paramètre
  const IndexArticles petit(bibliothequeArticles());
  CHECK_FALSE(petit.chercher("\"MOSFET canal N\"").empty());
  for (const auto& x : petit.chercher("R_4*_0603")) CHECK(x.article.id.rfind("R_4", 0) == 0);
  CHECK_FALSE(petit.chercher("/^IRL.*/").empty());
  for (const auto& x : petit.chercher("condensateur tension>=50V")) CHECK(x.article.parametres.count("tension"));
  // Semblables : une 10k 0603 a pour voisines d'autres résistances en 0603
  const Article* r10k = articleIntegre("R_10k_0603");
  REQUIRE(r10k);
  const auto sem = petit.semblables(*r10k, 5);
  REQUIRE_FALSE(sem.empty());
  CHECK(sem.front().article.empreinte == "R_0603");
}

TEST_CASE("4.2, 4.9 : IRLZ44N en TO-220 relie G, D, S aux pastilles 1, 2, 3 ; 2N7002 en SOT-23 : G, S, D", "[L4][BIB-03][B17]") {
  ConceptionElec c = nouvelleConception("Brochages");
  const Article* irlz = articleIntegre("IRLZ44N");
  const Article* n7002 = articleIntegre("2N7002");
  REQUIRE(irlz);
  REQUIRE(n7002);
  const std::string a = composantDe(c, poserArticle(c, 0, *irlz, ptMm(50, 50)));
  const std::string b = composantDe(c, poserArticle(c, 0, *n7002, ptMm(80, 50)));
  CHECK(brocheNommee(c, a, "G") == "1");
  CHECK(brocheNommee(c, a, "D") == "2");
  CHECK(brocheNommee(c, a, "S") == "3");
  CHECK(brocheNommee(c, b, "G") == "1");
  CHECK(brocheNommee(c, b, "S") == "2");
  CHECK(brocheNommee(c, b, "D") == "3");
  CHECK(c.composant(a)->empreinte == "TO-220-3");
  CHECK(c.composant(a)->hauteur == Approx(16.5));
  // Les tables de l'article valent vérification du brochage : pas d'ERC-09
  for (const auto& v : verifierERC(c, connectivite(c), NiveauErc::Complet)) CHECK(v.code != "ERC-09");
  // Brochages transcrits deux fois et comparés (4.9) : une seconde table, écrite à part, d'après les fiches
  const std::map<std::string, std::map<std::string, std::string>> seconde{
      {"IRLZ44N", {{"G", "1"}, {"D", "2"}, {"S", "3"}}}, {"IRF540N", {{"G", "1"}, {"D", "2"}, {"S", "3"}}}, {"2N7002", {{"G", "1"}, {"S", "2"}, {"D", "3"}}},
      {"BSS138", {{"G", "1"}, {"S", "2"}, {"D", "3"}}},   {"AO3400A", {{"G", "1"}, {"S", "2"}, {"D", "3"}}}, {"BC847", {{"B", "1"}, {"E", "2"}, {"C", "3"}}},
      {"MMBT3904", {{"B", "1"}, {"E", "2"}, {"C", "3"}}}, {"BAT54", {{"A", "1"}, {"K", "3"}}}};
  for (const auto& [id, table] : seconde) {
    const Article* art = articleIntegre(id);
    REQUIRE(art);
    const DefSymbole d = symboleDeArticle(*art, *symboleBibliotheque(art->symbole));
    for (const auto& [nom, pastille] : table) {
      INFO(id << " " << nom);
      bool trouve = false;
      for (const auto& br : d.broches) trouve |= br.nom == nom && br.numero == pastille;
      CHECK(trouve);
    }
  }
  // Tous les articles intégrés passent les contrôles (4.19)
  for (const auto& x : bibliothequeArticles()) {
    INFO(x.id);
    const DefSymbole* s = symboleBibliotheque(x.symbole);
    const auto e = genererEmpreinte(x.empreinte);
    CHECK(controlerArticle(x, s, e ? &*e : nullptr).empty());
  }
}

TEST_CASE("4.7 : pastilles IPC-7351B nominales d'une SOIC-8, d'une puce 0603 et d'une QFN-32, calculées à la main", "[L4][EMP-04]") {
  // SOIC-8 (fiche TI, boîtier D) : portée 5,80 à 6,20, patte 0,40 à 1,27, largeur 0,31 à 0,51, pas 1,27
  CotesBoitier soic;
  soic.gabarit = GabaritIPC::AileDeMouette;
  soic.broches = 8;
  soic.pas = 1.27;
  soic.lMin = 5.80, soic.lMax = 6.20, soic.tMin = 0.40, soic.tMax = 1.27, soic.wMin = 0.31, soic.wMax = 0.51;
  soic.corpsL = 4.90, soic.corpsW = 3.90, soic.hauteur = 1.75;
  // À la main : Z = 5,80 + 0,70 + √(0,40² + 0,05² + 0,025²) = 6,904 → 6,95 ; S max quadratique 4,977,
  // G = 4,977 − 0,70 − √(1,6738 + 0,0031) = 2,982 → 2,95 ; X = 0,31 + 0,06 + √(0,20² + 0,0031) = 0,578 → 0,58
  const PastillesIPC p = calculerIPC7351(soic);
  CHECK(p.z == Approx(6.95).margin(0.01));
  CHECK(p.g == Approx(2.95).margin(0.01));
  CHECK(p.x == Approx(0.58).margin(0.01));
  CHECK(p.longueur == Approx(2.00).margin(0.01));
  CHECK(p.centre == Approx(2.475).margin(0.01));
  const DefEmpreinte d = empreinteIPC7351(soic);
  REQUIRE(d.pastilles.size() == 8);
  CHECK(d.pastilles[0].numero == "1");
  CHECK(d.pastilles[0].position == ptMm(-2.475, -1.905));
  CHECK(d.pastilles[7].position == ptMm(2.475, -1.905));
  CHECK(d.id == "SOIC127P600X175-8N");
  CHECK(nomKicadIPC(soic) == "SOIC-8_3.9x4.9mm_P1.27mm");
  // Puce 0603 : longueur 1,45 à 1,75, terminaison 0,15 à 0,45, largeur 0,65 à 0,95
  CotesBoitier puce;
  puce.gabarit = GabaritIPC::Puce;
  puce.lMin = 1.45, puce.lMax = 1.75, puce.tMin = 0.15, puce.tMax = 0.45, puce.wMin = 0.65, puce.wMax = 0.95;
  puce.corpsL = 1.6, puce.corpsW = 0.8, puce.hauteur = 0.55;
  // Z = 1,45 + 0,70 + √(0,09 + 0,0031) = 2,455 → 2,50 ; G = 1,260 − √(0,27 + 0,0031) = 0,737 → 0,70 ; X = 0,955 → 0,96
  const PastillesIPC q = calculerIPC7351(puce);
  CHECK(q.z == Approx(2.50).margin(0.01));
  CHECK(q.g == Approx(0.70).margin(0.01));
  CHECK(q.x == Approx(0.96).margin(0.01));
  // QFN-32 5 × 5 au pas de 0,5 : corps 4,9 à 5,1, plot 0,3 à 0,5, largeur 0,18 à 0,30
  CotesBoitier qfn;
  qfn.gabarit = GabaritIPC::SansPatte;
  qfn.broches = 32;
  qfn.quatreCotes = true;
  qfn.pas = 0.5;
  qfn.lMin = 4.9, qfn.lMax = 5.1, qfn.tMin = 0.3, qfn.tMax = 0.5, qfn.wMin = 0.18, qfn.wMax = 0.30;
  qfn.corpsL = qfn.corpsW = 5, qfn.hauteur = 1.0, qfn.plotCentral = 3.3;
  // Z = 5,5 + √(0,04 + 0,0031) = 5,708 → 5,75 ; G = 4,373 − √(0,12 + 0,0031) = 4,022 → 4,00 ; X = 0,10 + 0,132 = 0,232 → 0,23
  const PastillesIPC r = calculerIPC7351(qfn);
  CHECK(r.z == Approx(5.75).margin(0.01));
  CHECK(r.g == Approx(4.00).margin(0.01));
  CHECK(r.x == Approx(0.23).margin(0.01));
  CHECK(empreinteIPC7351(qfn).pastilles.size() == 33);
}

TEST_CASE("4.10 : un article modifié propose la mise à jour de ses composants, avec les différences", "[L4][BIB-10][SYM-14]") {
  ConceptionElec c = nouvelleConception("Mise à jour");
  const Article* r = articleIntegre("R_10k_0603");
  REQUIRE(r);
  for (int i = 0; i < 6; ++i) poserArticle(c, 0, *r, ptMm(30 + 10 * i, 40));
  Article nouveau = *r;
  nouveau.mpn = "RC0603JR-0710KL";
  nouveau.parametres["tolerance"] = "5 %";
  nouveau.version = 2;
  const auto ids = composantsAMettreAJour(c, nouveau);
  REQUIRE(ids.size() == 6);
  const auto diff = differencesArticle(c, *c.composant(ids.front()), nouveau);
  CHECK(std::any_of(diff.begin(), diff.end(), [](const DifferenceArticle& d) { return d.champ == "référence fabricant" && d.apres == "RC0603JR-0710KL"; }));
  CHECK(std::any_of(diff.begin(), diff.end(), [](const DifferenceArticle& d) { return d.champ == "paramètre tolerance"; }));
  CHECK(mettreAJourDepuisArticle(c, nouveau, ids) == 6);
  CHECK(composantsAMettreAJour(c, nouveau).empty());
  CHECK(c.articles.at(nouveau.id).version == 2);
}

TEST_CASE("4.14 : l'article dérivé suit sa base, sauf le champ dont l'héritage est coupé", "[L4][BIB-22]") {
  std::map<std::string, Article> lib;
  Article base;
  base.id = "LDO_base";
  base.symbole = "Regulateur";
  base.empreinte = "SOT-223";
  base.fabricant = "Diodes";
  base.parametres["tensionSortie"] = "3,3 V";
  lib[base.id] = base;
  for (int i = 0; i < 5; ++i) {
    Article d;
    d.id = "LDO_" + std::to_string(i);
    d.base = "LDO_base";
    if (i == 4) d.nonHerites.insert("parametre:tensionSortie");
    lib[d.id] = d;
  }
  auto trouver = [&](const std::string& id) -> const Article* { return lib.count(id) ? &lib.at(id) : nullptr; };
  lib["LDO_base"].parametres["tensionSortie"] = "5 V";
  int suivent = 0;
  for (int i = 0; i < 5; ++i) {
    const Article r = resoudreArticle(lib.at("LDO_" + std::to_string(i)), trouver);
    CHECK(r.empreinte == "SOT-223");
    suivent += r.parametres.count("tensionSortie") && r.parametres.at("tensionSortie") == "5 V";
  }
  CHECK(suivent == 4);
}

TEST_CASE("4.11, 4.12 : ERC-08 pour un fusible sous une inductance ; code LCSC dans la nomenclature", "[L4][ERC-08][BIB-11]") {
  ConceptionElec c = nouvelleConception("Familles");
  const std::string l = composantDe(c, poserSymbole(c, 0, "L", ptMm(40, 40)));
  c.composant(l)->reference = "L1";
  c.composant(l)->empreinte = "Fuse_1206";
  bool erc08 = false;
  for (const auto& v : verifierERC(c, connectivite(c), NiveauErc::Complet)) erc08 |= v.code == "ERC-08";
  CHECK(erc08);
  Article a = *articleIntegre("R_10k_0603");
  a.lcsc = "C25804";
  const std::string r = composantDe(c, poserArticle(c, 0, a, ptMm(80, 40)));
  c.composant(r)->reference = "R1";
  bool vu = false;
  for (const auto& ligne : nomenclature(c)) vu |= ligne.lcsc == "C25804";
  CHECK(vu);
  CHECK(csvNomenclature(c).find("C25804") != std::string::npos);
}

TEST_CASE("4.17, 4.22 : une archive SnapEDA déposée devient un seul article contrôlé, symbole, empreinte et modèle 3D", "[L4][BIB-27][PRJ-14]") {
  const std::string zip = prom::lireFichier(prom::cheminFichier(std::string(PROMETHEE_DONNEES) + "/archives/ESP32-S3-WROOM-1-N8_SnapEDA.zip"));
  const auto fichiers = prom::lireZip(zip);
  REQUIRE(fichiers.size() == 4);
  CHECK(fichiers[3].second.rfind("SnapEDA ", 0) == 0);
  CHECK(fichiers[3].second.size() == 1600);
  const ImportArchive imp = importerArchiveComposant(zip, "ESP32-S3-WROOM-1-N8_SnapEDA.zip");
  INFO(imp.alertes.size());
  REQUIRE(imp.symboles.size() == 1);
  REQUIRE(imp.empreinte);
  CHECK(imp.symboles.front().broches.size() == 41);
  CHECK(imp.empreinte->pastilles.size() == 41);
  CHECK(imp.nomModele3D == "ESP32-S3-WROOM-1-N8.step");
  CHECK(imp.modele3D.rfind("ISO-10303-21", 0) == 0);
  CHECK(imp.article.id == "ESP32-S3-WROOM-1-N8");
  CHECK(imp.controles.empty());
  // Une archive abîmée se dit, sans planter
  CHECK_FALSE(importerArchiveComposant(zip.substr(0, zip.size() / 2)).alertes.empty());
  // Zip écrit par Prométhée (stocké) relu
  const auto relu = prom::lireZip(prom::zipStocke({{"a.txt", "bonjour"}, {"b/c.txt", std::string(1000, 'x')}}));
  REQUIRE(relu.size() == 2);
  CHECK(relu[1].second == std::string(1000, 'x'));
}

TEST_CASE("4.16 : bibliothèques citées par ${BIBLIO_EQUIPE}, développées par l'environnement ou les réglages", "[L4][BIB-08][PRJ-13]") {
  std::vector<std::string> inconnues;
  CHECK(developperChemin("${BIBLIO_EQUIPE}/passifs.json", {{"BIBLIO_EQUIPE", "/srv/biblio"}}, &inconnues) == "/srv/biblio/passifs.json");
  CHECK(inconnues.empty());
#ifdef _WIN32
  _putenv_s("BIBLIO_EQUIPE", "/mnt/autre");
#else
  setenv("BIBLIO_EQUIPE", "/mnt/autre", 1);
#endif
  CHECK(developperChemin("${BIBLIO_EQUIPE}/passifs.json", {{"BIBLIO_EQUIPE", "/srv/biblio"}}) == "/mnt/autre/passifs.json");
#ifdef _WIN32
  _putenv_s("BIBLIO_EQUIPE", "");  // une valeur vide retire la variable
#else
  unsetenv("BIBLIO_EQUIPE");
#endif
  CHECK(developperChemin("${INCONNUE}/x", {}, &inconnues) == "${INCONNUE}/x");
  CHECK(inconnues == std::vector<std::string>{"INCONNUE"});
}

TEST_CASE("4.21 : « LDO 3,3 V 500 mA » posé sans article reste signalé par l'ERC jusqu'à la standardisation", "[L4][BIB-20][BIB-26]") {
  ConceptionElec c = nouvelleConception("Générique");
  const Article g = articleGenerique("LDO 3,3 V 500 mA", 3);
  const std::string id = composantDe(c, poserArticle(c, 0, g, ptMm(60, 60)));
  REQUIRE_FALSE(id.empty());
  c.composant(id)->reference = "U1";
  auto erc44 = [&] {
    for (const auto& v : verifierERC(c, connectivite(c), NiveauErc::Complet))
      if (v.code == "ERC-44") return true;
    return false;
  };
  CHECK(erc44());
  const IndexArticles index(bibliothequeArticles());
  const auto props = propositionsStandardisation(c, index);
  REQUIRE(props.size() == 1);
  CHECK(props.front().article == "AMS1117-3.3");
  CHECK(appliquerStandardisation(c, index, props) == 1);
  CHECK_FALSE(erc44());
  CHECK(c.composant(id)->article == "AMS1117-3.3");
  CHECK(c.composant(id)->empreinte == "SOT-223");
  // Le fichier garde articles et liens
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  CHECK(relue.articles.count("AMS1117-3.3"));
  CHECK(relue.composant(id)->article == "AMS1117-3.3");
}
