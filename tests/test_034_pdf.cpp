// Prométhée : essais de la 0.34, lot L8 côté noyau — documents PDF (8.11, 8.29) : circuit par couches écrit par le
// noyau, signets, liens, métadonnées XMP, mise à jour incrémentale d'un PDF existant, relecture.
// Licence GPL-3.0-only.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/circuit_edition.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/pdf_documents.hpp"

using namespace prom::ecad;

namespace {
ConceptionElec carteQuatreCouches() {
  ConceptionElec c = nouvelleConception("Quatre couches");
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(60);
  c.circuit.contour.hauteur = nm(40);
  c.circuit.empilage.couchesCuivre = 4;
  c.cartouche.titre = "Carte d'essai PDF";
  c.cartouche.auteur = "Évan";
  c.cartouche.revision = "C";
  for (int i = 0; i < 14; ++i) poserEmpreinteSeule(c, "R_0603", ptMm(6 + 7 * (i % 7), 10 + 15 * (i / 7)));
  return c;
}
// pdftotext (poppler) quand il est installé : le texte que trouve la recherche d'un lecteur
std::string texteDuLecteur(const std::string& pdf) {
  if (std::system("pdftotext -v > /dev/null 2>&1") != 0) return {};
  const auto d = std::filesystem::temp_directory_path() / "promethee-pdf-essai";
  std::filesystem::create_directories(d);
  {
    std::ofstream f(d / "c.pdf", std::ios::binary);
    f << pdf;
  }
  if (std::system(("pdftotext -layout \"" + (d / "c.pdf").string() + "\" \"" + (d / "c.txt").string() + "\"").c_str()) != 0) return {};
  std::ifstream t(d / "c.txt");
  std::stringstream s;
  s << t.rdbuf();
  return s.str().empty() ? std::string(" ") : s.str();
}
}  // namespace

TEST_CASE("8.29 · PDF de quatre couches : quatre pages, « R12 » trouvé par la recherche du lecteur ; signets, infobulles, XMP", "[034][L8][8.29]") {
  const ConceptionElec c = carteQuatreCouches();
  REQUIRE(c.composant(c.circuit.empreintes[11].composant)->reference == "R12");
  OptionsPdfCircuit o;
  for (Couche k : couchesCuivre(4)) o.couches.push_back({k, "#C83232"});
  o.filigrane = "PROVISOIRE";
  const std::string pdf = pdfCircuit(c, o);
  const LecturePdf l = lirePdf(pdf);
  REQUIRE(l.ok);
  CHECK(l.pages == 4);
  REQUIRE(l.signets.size() == 4);
  CHECK(l.signets[0] == nomCouche(Couche::CuivreDessus));
  CHECK(l.infobulles >= 14);  // empreintes cliquables sur les deux faces
  CHECK(l.xmp.find("Carte d'essai PDF") != std::string::npos);
  CHECK(l.xmp.find("<prom:Revision>C</prom:Revision>") != std::string::npos);
  CHECK(pdf.find("(R12) Tj") != std::string::npos);
  const std::string texte = texteDuLecteur(pdf);
  if (!texte.empty()) CHECK(texte.find("R12") != std::string::npos);
  // Superposées : une seule page
  o.superposer = true;
  CHECK(lirePdf(pdfCircuit(c, o)).pages == 1);
}

TEST_CASE("8.11 · PDF complété par mise à jour incrémentale : un signet par feuille, liens entre étiquettes de même nom", "[034][L8][8.11]") {
  ConceptionElec c = carteQuatreCouches();
  // Trois feuilles ; une étiquette globale « HORLOGE » sur la première et la troisième
  c.feuilles.resize(3);
  c.feuilles[1].nom = "Alimentation";
  c.feuilles[2].nom = "Interfaces";
  Etiquette e;
  e.texte = "HORLOGE";
  e.type = TypeEtiquette::Globale;
  e.p = ptMm(100, 80);
  c.feuilles[0].etiquettes.push_back(e);
  e.p = ptMm(50, 40);
  c.feuilles[2].etiquettes.push_back(e);
  const auto signets = signetsSchema(c);
  REQUIRE(signets.size() == 3);
  CHECK(signets[1].titre == "2. Alimentation");
  const auto liens = liensSchema(c);
  REQUIRE(liens.size() == 2);  // aller et retour
  CHECK(liens[0].pageCible != liens[0].page);
  // Un PDF existant de trois pages, complété sans être réécrit
  OptionsPdfCircuit o;
  o.couches = {{Couche::CuivreDessus, "#C83232"}, {Couche::CuivreInt1, "#C83232"}, {Couche::CuivreDessous, "#3250C8"}};
  o.metaDonnees = false;
  const std::string avant = pdfCircuit(c, o);
  const std::string apres = completerPdf(avant, signets, liens, metaDepuisCartouche(c));
  CHECK(apres.compare(0, avant.size(), avant) == 0);  // le fichier d'origine est intact, la révision s'ajoute
  const LecturePdf l = lirePdf(apres);
  REQUIRE(l.ok);
  CHECK(l.revisions == 2);
  CHECK(l.pages == 3);
  CHECK(l.liens == 2);
  REQUIRE(l.signets.size() == 3);
  CHECK(l.signets[2] == "3. Interfaces");
  CHECK(l.xmp.find("<rdf:li>Évan</rdf:li>") != std::string::npos);
  if (std::system("qpdf --version > /dev/null 2>&1") == 0) {
    const auto d = std::filesystem::temp_directory_path() / "promethee-pdf-essai";
    std::filesystem::create_directories(d);
    std::ofstream(d / "s.pdf", std::ios::binary) << apres;
    CHECK(std::system(("qpdf --check \"" + (d / "s.pdf").string() + "\" > /dev/null 2>&1").c_str()) == 0);
  }
}

#include "promethee/plans_carte.hpp"

TEST_CASE("8.30 · un trou de 3,2 mm ajouté à la carte : le tableau de perçage du plan le compte sans autre geste ; notes, empilage", "[034][L8][8.30]") {
  ConceptionElec c = carteQuatreCouches();
  c.extensions["plans"] = dossierPlansVersJson(dossierPlansParDefaut(c));
  const DossierPlans d = dossierPlans(c);
  REQUIRE(d.feuilles.size() == 1);
  REQUIRE(std::any_of(d.feuilles[0].vues.begin(), d.feuilles[0].vues.end(), [](const VuePlan& v) { return v.genre == VuePlan::Genre::TableauPercage; }));
  const ContenuTableau avant = contenuTableau(c, VuePlan::Genre::TableauPercage);
  const std::string svgAvant = svgFeuillePlan(c, d.feuilles[0]);
  // Le trou de fixation de 3,2 mm, posé sur la carte
  poserEmpreinteSeule(c, "MountingHole_3.2", ptMm(54, 34));
  const ContenuTableau apres = contenuTableau(c, VuePlan::Genre::TableauPercage);
  CHECK(apres.total == avant.total + 1);
  CHECK(std::any_of(apres.lignes.begin(), apres.lignes.end(), [](const std::vector<std::string>& l) { return l[1] == "3,20" && l[4] == "1"; }));
  // Le plan, rendu à nouveau, le montre : rien n'a été recopié dans le dossier
  CHECK(dossierPlans(c) == d);
  const std::string svg = svgFeuillePlan(c, dossierPlans(c).feuilles[0]);
  CHECK(svg != svgAvant);
  CHECK(svg.find(">3,20<") != std::string::npos);
  CHECK(svg.find("Total : " + std::to_string(apres.total) + " trou(s)") != std::string::npos);
  // Notes types numérotées, mises à jour en masse
  DossierPlans e = dossierPlans(c);
  CHECK(renumeroterNotes(e.feuilles[0]) == 6);
  CHECK(remplacerDansNotes(e, "± 0,1 mm", "± 0,15 mm") == 1);
  CHECK(texteNote(c, catalogueNotes()[0]).find("4 couches") != std::string::npos);
  // Le tableau posé sur une couche de dessin du circuit
  const std::size_t dessins = c.circuit.dessins.size();
  CHECK(poserTableauSurCouche(c, VuePlan::Genre::TableauPercage, ptMm(0, 50)) > 0);
  CHECK(c.circuit.dessins.size() > dessins);
  // Empilage en classeur
  const auto lignes = lireXlsx(xlsxEmpilage(c));
  CHECK(lignes.size() >= 8);  // en-tête, quatre cuivres et trois diélectriques
}

TEST_CASE("8.29 · typon compensé : facteurs X et Y mesurés sur la règle de 50 mm, correction de largeur des pistes", "[034][L8][8.29]") {
  ConceptionElec c = carteQuatreCouches();
  Piste p;
  p.uuid = nouvelUuid();
  p.reseau = "N";
  p.couche = Couche::CuivreDessous;
  p.a = ptMm(10, 10);
  p.b = ptMm(40, 10);
  p.largeur = 300'000;
  c.circuit.pistes.push_back(p);
  const Connectivite k = connectivite(c);
  OptionsTypon o;
  const ModeleTypon m0 = modeleTypon(c, k, remplirZones(c, k), o);
  // La règle imprimée mesure 49,5 mm en X et 50,25 mm en Y : facteurs 50/49,5 et 50/50,25
  o.echelleX = facteurEchelleTypon(49.5);
  o.echelleY = facteurEchelleTypon(50.25);
  o.correctionLargeur = 0.02;
  const ModeleTypon m1 = modeleTypon(c, k, remplirZones(c, k), o);
  REQUIRE(m0.traits.size() == 1);
  REQUIRE(m1.traits.size() == 1);
  const double l0 = distance(m0.traits[0].a, m0.traits[0].b), l1 = distance(m1.traits[0].a, m1.traits[0].b);
  CHECK(l1 / l0 == Catch::Approx(50.0 / 49.5).epsilon(1e-6));
  CHECK(m1.traits[0].largeur == 320'000);
  CHECK(m1.hauteurCarte == Catch::Approx(m0.hauteurCarte * 50.0 / 50.25).epsilon(1e-9));
  const std::string svg = svgTypon(m1, o, "essai");
  CHECK(svg.find("x2=\"" + std::to_string(2 + 50 * 50.0 / 49.5).substr(0, 6)) != std::string::npos);
}
