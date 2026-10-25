// Prométhée : essais de la 0.34, lot L13 (automatisation) — scripts Python sur les commandes annulables (13.3, 13.9),
// serveur MCP (13.4), brochage BSDL (13.5), sorties sans écran (13.6), ligne de commande et macros (13.8), requêtes et
// règles (13.11), moteur de l'assistant (13.15), fiches techniques interrogées (13.16), simulation confiée (13.17).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/assistant.hpp"
#include "promethee/assistant_mcp.hpp"
#include "promethee/brochages.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/commandes_texte.hpp"
#include "promethee/conception_guidee.hpp"
#include "promethee/placement_auto.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/pdf_documents.hpp"
#include "promethee/processus.hpp"
#include "promethee/regles_carte.hpp"
#include "promethee/rendu_png.hpp"
#include "promethee/requetes_objets.hpp"
#include "promethee/scripts_python.hpp"
#include "promethee/simulation.hpp"
#include "promethee/sorties.hpp"

using namespace prom;
using namespace prom::ecad;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {
std::string donnee13(const std::string& f) { return lireFichier(cheminFichier(std::string(PROMETHEE_DONNEES) + "/" + f)); }
struct Montage13 {
  ConceptionElec c = nouvelleConception("Montage");
  double x = 25.4;
  ComposantElec* poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, 50.8));
    x += 25.4;
    ComposantElec* r = nullptr;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        r = c.composant(p.composant);
        r->reference = ref;
        r->valeur = valeur;
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
  std::string uuid(const std::string& ref) const { return c.parReference(ref) ? c.parReference(ref)->uuid : std::string(); }
};
Projet projetDe(const ConceptionElec& c) {
  Projet p = projetVide();
  p.sansCarte = true;
  p.electronique.push_back(c);
  return p;
}
Montage13 filtreRC(const std::string& ref = "R1") {
  Montage13 m;
  m.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", ref, "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  m.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
  return m;
}
std::string dossierEssai(const std::string& nom) {
  std::random_device r;
  const fs::path p = fs::temp_directory_path() / ("promethee-l13-" + nom + "-" + std::to_string(r()));
  fs::create_directories(p);
  return utf8(p);
}
}  // namespace

TEST_CASE("13.3 · un script pose dix résistances, un Ctrl+Z les retire", "[034][L13][13.3]") {
  if (!pythonDisponible()) SKIP("Python 3 n'est pas installé");
  Projet original = projetDe(nouvelleConception("Carte"));
  Projet copie = original;
  const std::string code = "import promethee\n"
                           "for i in range(10):\n"
                           "    promethee.poser_symbole(\"R\", x=20 + 10 * i, y=60, ref=\"R%d\" % (i + 1), valeur=\"10k\")\n"
                           "print(\"posées :\", len(promethee.composants()))\n";
  std::vector<std::string> lignes;
  OptionsScript o;
  o.sortie = [&](const std::string& l) { lignes.push_back(l); };
  const ResultatScript r = executerScript(copie, code, o);
  INFO(r.erreurs);
  REQUIRE(r.reussi);
  CHECK(r.modifications == 10);
  CHECK(copie.electronique[0].composants.size() == 10);
  CHECK(original.electronique[0].composants.empty());  // le projet confié est une copie : un seul pas la reprend
  CHECK(r.nomPas() == "Script : 10 modifications");
  REQUIRE(!lignes.empty());
  CHECK(lignes.back() == "posées : 10");
  // Refus de Prométhée : la ligne fautive est dite
  Projet p2 = original;
  const ResultatScript e = executerScript(p2, "import promethee\nx = 1\npromethee.poser_symbole(\"INCONNU\", x=0, y=0)\n");
  CHECK(!e.reussi);
  CHECK(e.ligneErreur == 3);
  CHECK(e.erreurs.find("symbole inconnu") != std::string::npos);
  // Point d'arrêt et pas à pas par bdb : arrêt ligne 2, variables lues, puis un pas jusqu'à la ligne 3
  Projet p3 = original;
  OptionsScript ob;
  ob.pointsArret = {2};
  std::vector<std::pair<int, std::string>> arrets;
  ob.pause = [&](int ligne, const nlohmann::ordered_json& v) {
    arrets.emplace_back(ligne, v.contains("a") ? v["a"].get<std::string>() : std::string());
    return arrets.size() == 1 ? std::string("pas") : std::string("continuer");
  };
  const ResultatScript b = executerScript(p3, "a = 41\nb = a + 1\nc = b * 2\nprint(c)\n", ob);
  INFO(b.erreurs);
  REQUIRE(b.reussi);
  REQUIRE(arrets.size() == 2);
  CHECK(arrets[0].first == 2);
  CHECK(arrets[0].second == "41");
  CHECK(arrets[1].first == 3);
  CHECK(b.sortie.find("84") != std::string::npos);
  CHECK(modulePython().find("def poser_symbole") != std::string::npos);
}

TEST_CASE("13.9 · script qui donne cinq valeurs à R2 et relit V(sortie) : fréquence de coupure tracée en fonction de R2, juste à 1 % en chaque point", "[034][L13][13.9]") {
  if (!pythonDisponible()) SKIP("Python 3 n'est pas installé");
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  Projet p = projetDe(filtreRC("R2").c);
  const std::string code = "import promethee\n"
                           "valeurs = [1000, 2200, 4700, 10000, 22000]\n"
                           "fc = []\n"
                           "for r in valeurs:\n"
                           "    promethee.valeur(\"R2\", r)\n"
                           "    fc.append(promethee.mesurer(\"coupure\", \"SORTIE\", promethee.analyse_ac(1, 1e6, 200)))\n"
                           "promethee.courbe(\"fc(R2)\", valeurs, fc, \"R2 (Ω)\", \"fc (Hz)\")\n"
                           "print(promethee.ngspice(\"print v(n_sortie)\")[:1])\n";
  const ResultatScript r = executerScript(p, code);
  INFO(r.erreurs);
  REQUIRE(r.reussi);
  REQUIRE(r.courbes.size() == 1);
  const CourbeScript& k = r.courbes[0];
  CHECK(k.nom == "fc(R2)");
  REQUIRE(k.x.size() == 5);
  REQUIRE(k.y.size() == 5);
  for (std::size_t i = 0; i < 5; ++i) {
    const double attendue = 1 / (2 * std::numbers::pi * k.x[i] * 100e-9);
    INFO("R2 = " << k.x[i] << " Ω : " << k.y[i] << " Hz pour " << attendue);
    CHECK(std::abs(k.y[i] - attendue) / attendue < 0.01);
  }
}

TEST_CASE("13.4 · proposition refusée : projet inchangé", "[034][L13][13.4]") {
  Projet p = projetDe(filtreRC().c);
  const std::string avant = versJson(p).dump();
  ServeurMcp s(p);
  const auto init = s.traiter(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"essai","version":"1"}}})");
  REQUIRE(init);
  CHECK(init->find("serverInfo") != std::string::npos);
  CHECK(!s.traiter(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
  const auto liste = s.traiter(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
  REQUIRE(liste);
  CHECK(liste->find("\"proposer\"") != std::string::npos);
  const auto lecture = s.traiter(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"composants","arguments":{}}})");
  REQUIRE(lecture);
  CHECK(lecture->find("R1") != std::string::npos);
  const auto prop = s.traiter(R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"proposer","arguments":{"methode":"composant.valeur","params":{"ref":"R1","valeur":"4k7"},"raison":"coupure à 340 Hz"}}})");
  REQUIRE(prop);
  CHECK(prop->find("en attente") != std::string::npos);
  REQUIRE(s.propositions().size() == 1);
  // Une proposition qui échouerait est refusée d'avance ; une lecture ne se propose pas
  const auto mauvaise = s.traiter(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"proposer","arguments":{"methode":"composant.valeur","params":{"ref":"R9","valeur":"1k"}}}})");
  CHECK(mauvaise->find("\"isError\":true") != std::string::npos);
  CHECK(s.propositions().size() == 1);
  // Échange par fichier : l'atelier relit les propositions, en refuse une ; le projet ne bouge pas
  auto v = lirePropositions(textePropositions(s.propositions()));
  REQUIRE(v.size() == 1);
  refuserProposition(v[0], "valeur gardée");
  CHECK(v[0].etat == "refusée");
  CHECK(versJson(p).dump() == avant);
  // Une proposition acceptée s'applique en un pas
  const auto prop2 = s.traiter(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"proposer","arguments":{"methode":"composant.valeur","params":{"ref":"C1","valeur":"220n"}}}})");
  REQUIRE(s.propositions().size() == 2);
  PropositionAssistant x = s.propositions()[1];
  const ReponseApi a = accepterProposition(p, x);
  CHECK(a.modifie);
  CHECK(x.etat == "acceptée");
  CHECK(p.electronique[0].parReference("C1")->valeur == "220n");
  CHECK(!accepterProposition(p, x).modifie);  // deux fois : refusé
}

TEST_CASE("13.5 · BSDL d'un microcontrôleur : broches exactes", "[034][L13][13.5]") {
  std::string erreur;
  const auto b = lireBsdl(donnee13("bsdl/stm32f103c8_lqfp48.bsd"), &erreur);
  INFO(erreur);
  REQUIRE(b);
  CHECK(b->entite == "STM32F103_LQFP48");
  CHECK(b->boitier == "LQFP48");
  for (const auto& a : b->alertes) UNSCOPED_INFO(a);
  CHECK(b->alertes.empty());
  REQUIRE(b->broches.size() == 48);
  const std::vector<std::pair<std::string, std::string>> attendu = {
      {"1", "VBAT"},  {"2", "PC13"},  {"3", "PC14"},  {"4", "PC15"},  {"5", "PD0"},   {"6", "PD1"},   {"7", "NRST"},  {"8", "VSSA"},  {"9", "VDDA"},  {"10", "PA0"},
      {"11", "PA1"},  {"12", "PA2"},  {"13", "PA3"},  {"14", "PA4"},  {"15", "PA5"},  {"16", "PA6"},  {"17", "PA7"},  {"18", "PB0"},  {"19", "PB1"},  {"20", "PB2"},
      {"21", "PB10"}, {"22", "PB11"}, {"23", "VSS1"}, {"24", "VDD1"}, {"25", "PB12"}, {"26", "PB13"}, {"27", "PB14"}, {"28", "PB15"}, {"29", "PA8"},  {"30", "PA9"},
      {"31", "PA10"}, {"32", "PA11"}, {"33", "PA12"}, {"34", "JTMS"}, {"35", "VSS2"}, {"36", "VDD2"}, {"37", "JTCK"}, {"38", "JTDI"}, {"39", "JTDO"}, {"40", "JNTRST"},
      {"41", "PB5"},  {"42", "PB6"},  {"43", "PB7"},  {"44", "BOOT0"}, {"45", "PB8"}, {"46", "PB9"},  {"47", "VSS3"}, {"48", "VDD3"}};
  for (std::size_t i = 0; i < 48; ++i) {
    CHECK(b->broches[i].numero == attendu[i].first);
    CHECK(b->broches[i].nom == attendu[i].second);
  }
  CHECK(typeDeBrocheLue(b->broches[23]) == TypeBroche::AlimEntree);  // VDD1
  CHECK(typeDeBrocheLue(b->broches[43]) == TypeBroche::Entree);      // BOOT0
  // Symbole : une unité par port, l'alimentation et la commande à part ; les 48 broches s'y trouvent
  const DefSymbole s = symboleDepuisBrochage(*b, "STM32F103C8");
  std::set<std::string> nums;
  std::set<int> unites;
  for (const auto& x : s.broches) nums.insert(x.numero), unites.insert(x.unite);
  CHECK(nums.size() == 48);
  CHECK(unites.size() == 6);  // PA, PB, PC, PD, alimentation, commande
  // Tableau de fiche
  const auto t = lireTableauBrochage("Broche;Nom;Type\n1;VCC;alim\n2;IN;entrée\n3;OUT;sortie\n4;GND;alim\n");
  REQUIRE(t);
  REQUIRE(t->broches.size() == 4);
  CHECK(typeDeBrocheLue(t->broches[1]) == TypeBroche::Entree);
  CHECK(typeDeBrocheLue(t->broches[3]) == TypeBroche::AlimEntree);
  CHECK(!lireBsdl("bonjour", &erreur));
  // Consolidation des passifs : « 10k » et « 10 kΩ » en 0603 se regroupent, pas le 4k7
  auto art = [](const std::string& id, const std::string& v) {
    Article a;
    a.id = id;
    a.symbole = "R";
    a.valeur = v;
    a.empreinte = "R_0603_1608Metric";
    return a;
  };
  const auto g = consoliderPassifs({art("r1", "10k"), art("r2", "10 kΩ"), art("r3", "4k7")});
  REQUIRE(g.size() == 1);
  CHECK(g[0].articles == std::vector<std::string>{"r1", "r2"});
}

TEST_CASE("13.6 · machine sans écran : sorties -D REVISION=C produit rendu PNG, statistiques JSON et PDF du schéma", "[034][L13][13.6]") {
  ConceptionElec c = exempleElectronique();
  const std::string png = pngCarte(c);
  const auto im = lirePng(png);
  REQUIRE(im);
  const double L = mm(boite(c.circuit.contour.polygone()).largeur()) + 2, H = mm(boite(c.circuit.contour.polygone()).hauteur()) + 2;
  CHECK(std::abs(im->largeur - L * 12) <= 2);
  CHECK(std::abs(im->hauteur - H * 12) <= 2);
  // Vernis vert, pastilles dorées, bord transparent
  int vert = 0, dore = 0;
  for (int y = 0; y < im->hauteur; ++y)
    for (int x = 0; x < im->largeur; ++x) {
      const std::uint32_t p = im->pixel(x, y);
      const int r = static_cast<int>(p >> 24), g = static_cast<int>((p >> 16) & 0xFF), b = static_cast<int>((p >> 8) & 0xFF), a = static_cast<int>(p & 0xFF);
      if (a == 255 && g > r + 40 && g > b + 20) ++vert;
      if (a == 255 && r > 180 && g > 140 && b < 110) ++dore;
    }
  CHECK(vert > im->largeur * im->hauteur / 4);
  CHECK(dore > 50);
  CHECK((im->pixel(0, 0) & 0xFF) == 0);
  // Schéma en PDF : composants et cartouche
  c.cartouche.revision = "C";
  const std::string pdf = pdfSchema(c);
  CHECK(pdf.rfind("%PDF", 0) == 0);
  const std::string t = textePdfSimple(pdf);
  CHECK(t.find("R1") != std::string::npos);
  CHECK(t.find("Révision C") != std::string::npos);
  // Le jeu « Documentation » sur la conception d'origine, révision passée par -D
  const std::string dossier = dossierEssai("doc");
  const ConceptionElec c0 = exempleElectronique();
  const ResultatJeu r = executerJeu(c0, jeuDocumentation(), dossier, "ex", "0.34", {{"REVISION", "C"}});
  for (const auto& l : r.journal) UNSCOPED_INFO(l);
  REQUIRE(r.refus.empty());
  auto present = [&](const std::string& fin) { return std::any_of(r.fichiers.begin(), r.fichiers.end(), [&](const std::string& f) { return f.size() >= fin.size() && f.compare(f.size() - fin.size(), fin.size(), fin) == 0; }); };
  CHECK(present("-dessus.png"));
  CHECK(present("-dessous.png"));
  CHECK(present("-statistiques.json"));
  CHECK(present("-schema.pdf"));
  CHECK(r.dossier.find("-C-documentation") != std::string::npos);
  const std::string pdf2 = lireFichier(cheminFichier(r.dossier) / cheminFichier(nomFichierAscii(c0.nom, "carte") + "-schema.pdf"));
  CHECK(textePdfSimple(pdf2).find("Révision C") != std::string::npos);
  fs::remove_all(cheminFichier(dossier));
}

TEST_CASE("13.8 · macro « format de l'école » enregistrée, relue depuis son fichier texte et rejouée sur une carte neuve : même résultat qu'à la main, annulé d'un seul Ctrl+Z", "[034][L13][13.8]") {
  // Lignes de commande : unités, mots sans accents, valeurs de composants gardées en texte
  std::string erreur;
  const auto q = requeteDeLigne("carte contour largeur=100mm hauteur=8cm rayon=3", &erreur);
  INFO(erreur);
  REQUIRE(q);
  CHECK((*q)["method"] == "carte.contour");
  CHECK((*q)["params"]["largeur"].get<double>() == Approx(100));
  CHECK((*q)["params"]["hauteur"].get<double>() == Approx(80));
  const auto v = requeteDeLigne("composant.valeur ref=R1 valeur=4k7");
  REQUIRE(v);
  CHECK((*v)["params"]["valeur"] == "4k7");
  CHECK((*requeteDeLigne("composant.valeur ref=R1 valeur=100"))["params"]["valeur"] == "100");
  CHECK((*requeteDeLigne("carte.trou x=4 y=4 diametre=125mil"))["params"]["diametre"].get<double>() == Approx(3.175));
  CHECK(!requeteDeLigne("faire.nimporte quoi=1", &erreur));
  CHECK(erreur.find("inconnue") != std::string::npos);
  CHECK(!requeteDeLigne("# commentaire"));
  CHECK(!completer("carte.c").empty());
  CHECK(completer("carte.contour lar").front() == "carte.contour largeur=");
  // La macro écrite comme on l'enregistre, relue depuis son texte
  Macro m;
  m.nom = "format de l'école";
  m.description = "Carte de 100 × 80 mm, quatre trous M3, cartouche de l'école";
  m.lignes = {"carte.contour largeur=100 hauteur=80 rayon=3",
              "carte.trou x=4 y=4 diametre=3.2",
              "carte.trou x=96 y=4 diametre=3.2",
              "carte.trou x=4 y=76 diametre=3.2",
              "carte.trou x=96 y=76 diametre=3.2",
              "carte.texte texte=\"ESME Sudria\" x=10 y=70 hauteur=1.5",
              "carte.regles isolement=0.2 largeur=0.3",
              "cartouche societe=\"ESME Sudria\" auteur=\"Atelier\""};
  const std::string fichier = texteMacro(m);
  const Macro relue = lireMacro(fichier);
  CHECK(relue.nom == m.nom);
  CHECK(relue.lignes == m.lignes);
  // Rejouée sur une carte neuve, en un lot (un seul pas)
  Projet p = projetDe(nouvelleConception("Carte neuve"));
  std::vector<std::string> erreurs;
  const ReponseApi r = executerMacro(p, relue, &erreurs);
  for (const auto& e : erreurs) UNSCOPED_INFO(e);
  REQUIRE(r.modifie);
  CHECK(r.nomPas == "Macro « format de l'école »");
  // À la main, la même chose
  ConceptionElec main = nouvelleConception("Carte neuve");
  main.uuid = p.electronique[0].uuid;
  main.circuit.contour.libre.clear();
  main.circuit.contour.largeur = nm(100), main.circuit.contour.hauteur = nm(80), main.circuit.contour.rayon = nm(3);
  for (const auto& [x, y] : std::vector<std::pair<double, double>>{{4, 4}, {96, 4}, {4, 76}, {96, 76}})
    poserEmpreinteSeule(main, "MountingHole_3.2", {main.circuit.contour.origine.x + nm(x), main.circuit.contour.origine.y + nm(y)}, 0, false, "H");
  const ConceptionElec& auto_ = p.electronique[0];
  CHECK(auto_.circuit.contour == main.circuit.contour);
  REQUIRE(auto_.circuit.empreintes.size() == main.circuit.empreintes.size());
  for (std::size_t i = 0; i < main.circuit.empreintes.size(); ++i) {
    CHECK(auto_.circuit.empreintes[i].position == main.circuit.empreintes[i].position);
    CHECK(auto_.circuit.empreintes[i].empreinte == main.circuit.empreintes[i].empreinte);
  }
  REQUIRE(auto_.circuit.textes.size() == 1);
  CHECK(auto_.circuit.textes[0].texte == "ESME Sudria");
  CHECK(auto_.circuit.regles.isolement == nm(0.2));
  CHECK(auto_.circuit.regles.largeur == nm(0.3));
  CHECK(auto_.cartouche.societe == "ESME Sudria");
  // Tout ou rien : une ligne fautive et rien n'est fait
  Projet p2 = projetDe(nouvelleConception("Carte"));
  Macro mauvaise = relue;
  mauvaise.lignes.push_back("carte.trou x=1 y=1 diametre=-2");
  const std::string avant = versJson(p2).dump();
  CHECK(!executerMacro(p2, mauvaise).modifie);
  CHECK(versJson(p2).dump() == avant);
  // Commandes probables d'après les successions
  PredicteurCommandes pr;
  for (int i = 0; i < 3; ++i) pr.observer("carte.contour"), pr.observer("carte.trou");
  pr.observer("carte.contour");
  pr.observer("cartouche");
  pr.observer("carte.contour");
  REQUIRE(!pr.proposer().empty());
  CHECK(pr.proposer().front() == "carte.trou");
  PredicteurCommandes relu;
  relu.lire(pr.texte());
  CHECK(relu.proposer() == pr.proposer());
}

TEST_CASE("13.11 · règle « isolement 3 mm entre la classe Secteur et le reste sur les couches extérieures » : objets visés comptés, piste posée à 2,5 mm signalée", "[034][L13][13.11]") {
  ConceptionElec c = nouvelleConception("Secteur");
  c.circuit.contour.libre.clear();
  c.circuit.contour.largeur = nm(80), c.circuit.contour.hauteur = nm(50);
  const Pt o = c.circuit.contour.origine;
  auto piste = [&](const std::string& id, const std::string& reseau, double y, Couche couche = Couche::CuivreDessus) {
    Piste p;
    p.uuid = id;
    p.reseau = reseau;
    p.couche = couche;
    p.a = {o.x + nm(10), o.y + nm(y)};
    p.b = {o.x + nm(60), o.y + nm(y)};
    p.largeur = nm(0.5);
    c.circuit.pistes.push_back(p);
  };
  piste("l", "SECTEUR_L", 10);
  piste("n", "SECTEUR_N", 30);
  piste("int", "SECTEUR_L", 20, Couche::CuivreInt1);
  piste("tbt", "TBT", 10 + 0.5 + 2.5);  // bord à bord : 2,5 mm de la phase
  c.circuit.empilage.couchesCuivre = 4;
  ClasseReseau secteur;
  secteur.nom = "Secteur";
  c.circuit.regles.classes.push_back(secteur);
  c.circuit.regles.classeReseau["SECTEUR_*"] = "Secteur";
  // Éditeur de requêtes : objets visés comptés, requête invalide marquée
  std::string erreur;
  const std::string a = "classe == 'Secteur' && externe == 1";
  const ObjetsVises va = objetsVises(c, a, &erreur);
  INFO(erreur);
  CHECK(va.pistes == 2);  // phase et neutre sur la face ; pas la piste interne
  CHECK(va.texte() == "2 pistes");
  CHECK(objetsVises(c, "classe != 'Secteur' && externe == 1").pistes == 1);
  objetsVises(c, "classe == 'Secteur' &&", &erreur);
  CHECK(!erreur.empty());
  // La règle créée depuis les requêtes, sur les couches extérieures
  c.circuit.regles.avancees.contraintes.push_back(regleDepuisRequetes("Secteur 3 mm", GenreContrainte::Isolement, "classe == 'Secteur'", "classe != 'Secteur'", nm(3), couchesExternes()));
  const Connectivite k = connectivite(c);
  auto drc01 = [&](const ConceptionElec& x) {
    int n = 0;
    for (const auto& v : verifierDRCReglee(x, connectivite(x), remplirZones(x, connectivite(x))))
      if (v.code == "DRC-01" && v.message.find("TBT") != std::string::npos) ++n;
    return n;
  };
  CHECK(drc01(c) == 1);
  // Écartée à 3,5 mm : plus rien
  for (auto& p : c.circuit.pistes)
    if (p.uuid == "tbt") p.a.y = p.b.y = o.y + nm(10 + 0.5 + 3.5);
  CHECK(drc01(c) == 0);
  // Historique et favoris des requêtes
  HistoriqueRequetes h;
  h.noter(a);
  h.noter("genre == 'via'");
  h.noter(a);
  CHECK(h.recentes.front() == a);
  CHECK(h.recentes.size() == 2);
  h.basculerFavori(a);
  HistoriqueRequetes h2;
  h2.lire(h.texte());
  CHECK(h2.favoris == std::vector<std::string>{a});
  CHECK(!exemplesRequetes().empty());
}

TEST_CASE("13.11 · contrôle écrit en Python rendu comme une violation ordinaire", "[034][L13][13.11]") {
  if (!pythonDisponible()) SKIP("Python 3 n'est pas installé");
  Projet p = projetDe(filtreRC().c);
  std::vector<std::string> erreurs;
  const auto v = executerControlesPython(p, {{"resistances", "import promethee\nfor k in promethee.composants():\n    if k['ref'].startswith('R') and k['valeur'] == '1k':\n        promethee.signaler('Résistance de 1 kΩ : préférer 1,2 kΩ dans cette gamme', code='PY-7', objet=k['ref'])\n"}}, &erreurs);
  for (const auto& e : erreurs) UNSCOPED_INFO(e);
  REQUIRE(v.size() == 1);
  CHECK(v[0].code == "PY-7");
  CHECK(v[0].objets.size() == 1);
  CHECK(v[0].message.find("contrôle « resistances »") != std::string::npos);
}

TEST_CASE("13.15 · poste hors réseau, modèle local : « Expliquer ce bloc » sur l'alimentation répond, aucune connexion sortante relevée", "[034][L13][13.15]") {
  if (!pythonDisponible()) SKIP("Python 3 n'est pas installé");
  // Moteur local d'essai : un programme du poste qui lit l'invite et répond (comme llama-cli sur un modèle GGUF)
  const std::string dossier = dossierEssai("moteur");
  const fs::path moteur = cheminFichier(dossier) / "moteur_local.py";
  ecrireFichierAtomique(moteur, "import sys, re\n"
                                "invite = sys.stdin.read()\n"
                                "refs = sorted(set(re.findall(r'^- (U\\d+|C\\d+) : ([^ ;(]+)', invite, re.M)))\n"
                                "print('Ce bloc est une alimentation linéaire : ' + ', '.join(r + ' (' + v + ')' for r, v in refs) + '.')\n");
  MoteurAssistant m;
  m.genre = "commande";
  std::string py;
  pythonDisponible(&py);
  m.programme = programmeDuPoste("python3").empty() ? "python" : "python3";
  m.arguments = {"-I", utf8(moteur)};
  CHECK(m.configure());
  // La carte : un régulateur 7805 et ses deux condensateurs, plus une LED hors du bloc
  Montage13 a;
  a.poser("Regulateur", "U1", "LM7805", {{"1", "+12V"}, {"2", "GND"}, {"3", "+5V"}});
  a.poser("C", "C1", "330n", {{"1", "+12V"}, {"2", "GND"}});
  a.poser("C", "C2", "100n", {{"1", "+5V"}, {"2", "GND"}});
  a.poser("LED", "D1", "rouge", {{"1", "+5V"}, {"2", "GND"}});
  const std::set<std::string> bloc{a.uuid("U1"), a.uuid("C1"), a.uuid("C2")};
  MemoireAssistant memo;
  memo.regles = {"Toujours un condensateur de 100 nF au plus près de chaque régulateur"};
  const std::string invite = inviteExpliquerBloc(a.c, bloc, memo);
  CHECK(invite.find("U1 : LM7805") != std::string::npos);
  CHECK(invite.find("D1") == std::string::npos);  // hors du bloc
  CHECK(invite.find("100 nF au plus près") != std::string::npos);
  memo.suspendue = true;
  CHECK(inviteExpliquerBloc(a.c, bloc, memo).find("100 nF au plus près") == std::string::npos);
  effacerJournalProgrammes();
  const ReponseAssistant r = demander(m, systemeAssistant(), invite);
  INFO(r.erreur);
  REQUIRE(r.ok);
  CHECK(r.texte.find("U1 (LM7805)") != std::string::npos);
  CHECK(r.texte.find("C2 (100n)") != std::string::npos);
  // Seul le moteur du poste a été lancé ; Prométhée lui-même n'ouvre aucune connexion (reseau.cmake lit le source)
  const auto lances = programmesLances();
  REQUIRE(lances.size() == 1);
  CHECK(lances[0].front() == m.programme);
  // llama.cpp : ligne de commande d'un modèle GGUF local, sans conversation
  MoteurAssistant l;
  l.genre = "llama.cpp";
  l.programme = "llama-cli";
  l.modele = "/modeles/qwen2.5-3b-instruct-q4_k_m.gguf";
  const auto cmd = commandeMoteur(l, "Bonjour");
  CHECK(cmd[0] == "llama-cli");
  CHECK(std::find(cmd.begin(), cmd.end(), "-no-cnv") != cmd.end());
  CHECK(cmd.back() == "Bonjour");
  MoteurAssistant aucun;
  CHECK(!demander(aucun, {}, "?").ok);
  CHECK(moteurDepuisJson(moteurVersJson(l)).modele == l.modele);
  fs::remove_all(cheminFichier(dossier));
}

TEST_CASE("13.16 · « courant de sortie maximal ? » sur la fiche d'un LM317 : 1,5 A, avec l'extrait de la page qui le dit", "[034][L13][13.16]") {
  // Fiche d'essai écrite par Prométhée (texte de démonstration, pas celui du fabricant), trois pages
  DocumentPdf d;
  d.meta.titre = "LM317 — fiche d'essai";
  using G = BlocPdf::Genre;
  d.blocs = {{G::Titre, "LM317 adjustable positive voltage regulator"},
             {G::Paragraphe, "Features: output voltage adjustable from 1.25 V to 37 V. Output current in excess of 1.5 A. Internal thermal overload protection."},
             {G::Paragraphe, "Short-circuit current limiting, constant with temperature."},
             {G::SautDePage},
             {G::SousTitre, "Typical application"},
             {G::Paragraphe, "Vout = 1.25 V x (1 + R2 / R1) + Iadj x R2. Use 240 ohm for R1. Adjustment pin current 50 uA typical."},
             {G::SautDePage},
             {G::SousTitre, "Electrical characteristics"},
             {G::Paragraphe, "Reference voltage 1.25 V typical. Line regulation 0.01 %/V. Load regulation 0.1 %."},
             {G::Paragraphe, "Current limit 2.2 A typical when the input-output differential is below 15 V. Thermal resistance junction to case 5 °C/W."}};
  const std::string pdf = pdfDocument(d);
  std::string erreur;
  const auto f = lireFicheTechnique(pdf, "LM317.pdf", &erreur);
  INFO(erreur);
  REQUIRE(f);
  REQUIRE(f->pages.size() == 3);
  const auto extraits = chercherDansFiche(*f, "courant de sortie maximal ?");
  REQUIRE(!extraits.empty());
  CHECK(extraits[0].page == 1);
  const ReponseFiche r = repondreSurFiche(*f, "courant de sortie maximal ?");
  INFO(r.texte);
  REQUIRE(r.trouvee);
  CHECK(r.valeur == "1,5");
  CHECK(r.unite == "A");
  CHECK(r.nombre == Approx(1.5));
  CHECK(r.extrait.page == 1);
  CHECK(r.extrait.passage.find("Output current in excess of 1.5 A") != std::string::npos);
  CHECK(r.texte.find("page 1") != std::string::npos);
  // Autre question : la tension de référence, page 3
  const ReponseFiche t = repondreSurFiche(*f, "tension de référence ?");
  CHECK(t.trouvee);
  CHECK(t.valeur == "1,25");
  CHECK(t.extrait.page == 3);
  CHECK(!lireFicheTechnique("pas un pdf", "x", &erreur));
}

TEST_CASE("13.17 · « fréquence de coupure de ce filtre ? » : analyse lancée par l'assistant, réponse à 1 % de 1/(2πRC), courbe jointe", "[034][L13][13.17]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  Montage13 m;  // source sans amplitude alternative : l'assistant lui en donne une
  m.poser("Source_tension", "V1", "5", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "NOEUD"}});
  m.poser("C", "C1", "100n", {{"1", "NOEUD"}, {"2", "GND"}});
  const ReponseSimulation r = repondreSimulation(m.c, "Quelle est la fréquence de coupure de ce filtre ?");
  INFO(r.erreur);
  for (const auto& l : r.journal) UNSCOPED_INFO(l);
  REQUIRE(r.ok);
  CHECK(r.reseau == "NOEUD");  // trouvé par le condensateur de filtrage
  const double attendue = 1 / (2 * std::numbers::pi * 1000 * 100e-9);
  CHECK(std::abs(r.valeur - attendue) / attendue < 0.01);
  CHECK(r.unite == "Hz");
  CHECK(r.texte.find("1,59") != std::string::npos);
  REQUIRE(r.x.size() > 100);
  CHECK(r.x.size() == r.y.size());
  CHECK(valeurA(r.x, r.y, attendue) == Approx(-3.0).margin(0.1));
  // La conception confiée n'est pas modifiée
  CHECK(m.c.parReference("V1")->valeur == "5");
}

TEST_CASE("13.14 · carte de 50 × 30 mm, module ESP32 et port USB choisis : blocs posés et reliés, ERC et DRC sans erreur", "[034][L13][13.14]") {
  using namespace prom::ecad;
  REQUIRE(moduleGalerie("esp32-s3-wroom-1") != nullptr);
  CHECK(moduleGalerie("inconnu") == nullptr);
  const DefEmpreinte e = empreinteModule(*moduleGalerie("esp32-s3-wroom-1"));
  CHECK(e.pastilles.size() == 41);
  DemandeGuidee d;
  d.largeur = 50, d.hauteur = 30;
  d.module = "esp32-s3-wroom-1";
  d.interfaces = {"usb"};
  const ResultatGuide r = concevoirParModules(d);
  INFO([&] {
    std::string t;
    for (const auto& l : r.journal) t += l + "\n";
    for (const auto& l : r.violations) t += l + "\n";
    return t;
  }());
  REQUIRE(r.refus.empty());
  const ConceptionElec& c = r.conception;
  // Blocs posés : module, connecteur, protection, régulateur, condensateurs, tirage
  for (const char* ref : {"U1", "J1", "U2", "U3", "C1", "C2", "C3", "C4", "C5", "R1"}) CHECK(c.parReference(ref) != nullptr);
  CHECK(c.circuit.empreintes.size() == 10);
  // Reliés selon l'interface : D- et D+ du connecteur jusqu'aux broches USB du module, en passant par la protection
  const Connectivite k = connectivite(c);
  CHECK(k.nom(c.parReference("J1")->uuid, "2") == k.nom(c.parReference("U1")->uuid, "13"));
  CHECK(k.nom(c.parReference("J1")->uuid, "3") == k.nom(c.parReference("U1")->uuid, "14"));
  CHECK(k.nom(c.parReference("U3")->uuid, "1") == k.nom(c.parReference("U1")->uuid, "13"));
  CHECK(k.nom(c.parReference("U2")->uuid, "3") == k.nom(c.parReference("J1")->uuid, "1"));
  CHECK(k.nom(c.parReference("U2")->uuid, "2") == k.nom(c.parReference("U1")->uuid, "2"));
  // Placé sur la carte, routé, vérifié
  CHECK(collisionsPlacement(c) == 0);
  CHECK(r.erreursErc == 0);
  CHECK(r.erreursDrc == 0);
  CHECK(r.reussi());
  // Les circuits types sont là : plus rien à proposer pour le découplage ni pour la protection USB
  for (const auto& p : circuitsTypes(c)) CHECK(p.regle != "esd");
  // Sans eux, ils sont proposés
  DemandeGuidee nu = d;
  nu.router = false;
  ResultatGuide s = concevoirParModules(nu);
  ConceptionElec& sc = s.conception;
  sc.composants.erase(std::remove_if(sc.composants.begin(), sc.composants.end(), [](const ComposantElec& x) { return x.reference == "U3"; }), sc.composants.end());
  bool esd = false;
  PropositionCircuitType proposition;
  for (const auto& p : circuitsTypes(sc))
    if (p.regle == "esd") esd = true, proposition = p;
  REQUIRE(esd);
  // Posée d'un clic : la protection revient, reliée aux deux lignes
  std::string erreur;
  const auto poses = poserCircuitType(sc, proposition, &erreur);
  INFO(erreur);
  REQUIRE(poses.size() == 1);
  for (const auto& p : circuitsTypes(sc)) CHECK(p.regle != "esd");
  const Connectivite ks = connectivite(sc);
  CHECK(ks.nom(poses[0], "1") == ks.nom(sc.parReference("U1")->uuid, "13"));
  CHECK(poserCircuitType(sc, {"inconnue", "", "", {}}, &erreur).empty());
  // Gabarit trop petit : refusé, en clair
  DemandeGuidee petit = d;
  petit.hauteur = 20;
  CHECK(!concevoirParModules(petit).refus.empty());
}
