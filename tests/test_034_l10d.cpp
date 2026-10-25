// Prométhée : essais de la 0.34, lot L10 (fin) — reprise de l'existant (10.19) : articles rattachés par la référence du
// fabricant, fiches IPC/DAC-2552, boîtiers JEP30, conception de référence, conversion d'un dossier en série, guide de
// l'ancien outil. Licence GPL-3.0-only.
#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "promethee/echanges_electronique.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/import_altium.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/kicad_schema.hpp"
#include "promethee/reprise_existant.hpp"

using namespace prom::ecad;

namespace {
std::string donnee19(const std::string& f) { return prom::lireFichier(prom::cheminFichier(std::string(PROMETHEE_DONNEES) + "/" + f)); }

// Dossier virtuel (chemin relatif → contenu) et références de fabricant de chaque projet
struct DossierEssai {
  std::map<std::string, std::string> fichiers;
  std::map<std::string, std::set<std::string>> usages;  // référence normalisée → projets
  void employer(const std::string& projet, const std::string& mpn) {
    if (!mpn.empty()) usages[mpnNormalise(mpn)].insert(projet);
  }
};

// Projet KiCad : le clignotant à NE555 (schéma, carte, projet) ; H1 porte une référence propre au projet
void projetKicad(DossierEssai& d, int i) {
  ConceptionElec c = exempleElectronique();
  const std::string nom = "K" + std::to_string(i);
  c.nom = nom;
  for (auto& k : c.composants) {
    if (k.reference == "H1") {
      k.mpn = "UNIQ-K" + std::to_string(i);
      k.fabricant = "Keystone";
    }
    d.employer(nom, k.mpn);
  }
  for (const auto& [f, t] : schemaKicadDepuisConception(c, nom)) d.fichiers["kicad/" + nom + "/" + f] = t;
  d.fichiers["kicad/" + nom + "/" + nom + ".kicad_pcb"] = kicadDepuisConception(c, connectivite(c));
  d.fichiers["kicad/" + nom + "/" + nom + ".kicad_pro"] = projetKicadDepuisConception(c);
}

// Projet EAGLE : l'étage à LM358 des données d'essai, références posées en attributs des pièces
void projetEagle(DossierEssai& d, int i) {
  const std::string nom = "E" + std::to_string(i);
  std::string sch = donnee19("eagle/ampli.sch");
  auto attributs = [&](const std::string& part, const std::string& mpn, const std::string& fab) {
    const std::string debut = "<part name=\"" + part + "\"";
    const auto p = sch.find(debut);
    REQUIRE(p != std::string::npos);
    const auto f = sch.find("/>", p);
    sch.replace(f, 2, "><attribute name=\"MPN\" value=\"" + mpn + "\"/><attribute name=\"MANUFACTURER\" value=\"" + fab + "\"/></part>");
    d.employer(nom, mpn);
  };
  attributs("IC1", "LM358N", "TI");
  attributs("R1", "RC0603FR-0710KL", "Yageo");
  attributs("R2", "RC0603FR-0747KL", "Yageo");
  attributs("R4", "UNIQ-E" + std::to_string(i), "Vishay");
  d.employer(nom, "RC0603FR-071KL");  // R3 : attribut MPN déjà dans le fichier, sans fabricant
  d.fichiers["eagle/" + nom + "/ampli.sch"] = sch;
  d.fichiers["eagle/" + nom + "/ampli.brd"] = donnee19("eagle/ampli.brd");
}

// Projet Altium : schéma (R1, C1, J1) aux paramètres « Manufacturer Part Number » et « Manufacturer » ; un sur deux
// avec son .PrjPcb
void projetAltium(DossierEssai& d, int i) {
  const std::string nom = "A" + std::to_string(i);
  std::vector<EnregistrementAltium> e;
  auto composant = [&](const std::string& ref, int x, int y, const std::string& valeur, const std::string& motif, const std::string& mpn, const std::string& fab) {
    const int k = static_cast<int>(e.size());
    e.push_back({{"RECORD", "1"}, {"LIBREFERENCE", ref.substr(0, 1)}, {"LOCATION.X", std::to_string(x)}, {"LOCATION.Y", std::to_string(y)}, {"ORIENTATION", "0"}, {"CURRENTPARTID", "1"}, {"PARTCOUNT", "2"}, {"UNIQUEID", "U" + ref}});
    e.push_back({{"RECORD", "34"}, {"OWNERINDEX", std::to_string(k)}, {"TEXT", ref}, {"NAME", "Designator"}});
    e.push_back({{"RECORD", "41"}, {"OWNERINDEX", std::to_string(k)}, {"NAME", "Comment"}, {"TEXT", valeur}});
    e.push_back({{"RECORD", "41"}, {"OWNERINDEX", std::to_string(k)}, {"NAME", "Manufacturer Part Number"}, {"TEXT", mpn}});
    e.push_back({{"RECORD", "41"}, {"OWNERINDEX", std::to_string(k)}, {"NAME", "Manufacturer"}, {"TEXT", fab}});
    e.push_back({{"RECORD", "44"}, {"OWNERINDEX", std::to_string(k)}});
    e.push_back({{"RECORD", "45"}, {"OWNERINDEX", std::to_string(static_cast<int>(e.size()) - 1)}, {"MODELNAME", motif}, {"MODELTYPE", "PCBLIB"}, {"ISCURRENT", "T"}});
    for (int b = 0; b < 2; ++b)
      e.push_back({{"RECORD", "2"}, {"OWNERINDEX", std::to_string(k)}, {"OWNERPARTID", "1"}, {"DESIGNATOR", std::to_string(b + 1)}, {"NAME", std::to_string(b + 1)}, {"ELECTRICAL", "4"},
                   {"PINCONGLOMERATE", std::to_string((b ? 0 : 2) | 0x18)}, {"PINLENGTH", "20"}, {"LOCATION.X", std::to_string(x + b * 100)}, {"LOCATION.Y", std::to_string(y)}});
    d.employer(nom, mpn);
  };
  e.push_back({{"RECORD", "31"}, {"USECUSTOMSHEET", "T"}, {"CUSTOMX", "1500"}, {"CUSTOMY", "950"}});
  composant("R1", 300, 500, "10k", "R0603", "RC0603FR-0710KL", "YAGEO");
  composant("C1", 300, 300, "100n", "C0603", "GRM188R71H104KA93D", "Murata");
  composant("J1", 700, 500, "Header", "HDR1X2", "UNIQ-A" + std::to_string(i), "Würth Elektronik");
  e.push_back({{"RECORD", "27"}, {"LOCATIONCOUNT", "2"}, {"X1", "400"}, {"Y1", "500"}, {"X2", "700"}, {"Y2", "500"}});
  d.fichiers["altium/" + nom + "/" + nom + ".SchDoc"] = schDocAltium(e, i % 3 == 0);
  if (i % 2 == 0) d.fichiers["altium/" + nom + "/" + nom + ".PrjPcb"] = "[Design]\r\nHierarchyMode=0\r\n\r\n[Document1]\r\nDocumentPath=" + nom + ".SchDoc\r\n";
}

DossierEssai dossierDeTrente() {
  DossierEssai d;
  for (int i = 1; i <= 10; ++i) {
    projetKicad(d, i);
    projetEagle(d, i);
    projetAltium(d, i);
  }
  return d;
}
std::vector<std::string> cheminsDe(const DossierEssai& d) {
  std::vector<std::string> v;
  for (const auto& [f, t] : d.fichiers) v.push_back(f);
  return v;
}
LecteurDossier lecteurDe(const DossierEssai& d) {
  return [&d](const std::string& f) -> std::optional<std::string> {
    const auto it = d.fichiers.find(f);
    return it == d.fichiers.end() ? std::nullopt : std::optional<std::string>(it->second);
  };
}

// Article complet, tous les champs remplis
Article articleComplet() {
  Article a;
  a.id = "yageo-10k";
  a.nom = "10 kΩ 0603 1 %";
  a.description = "Résistance couche épaisse <0603> & 1 %";
  a.categorie = "Résistances";
  a.symbole = "R";
  a.empreinte = "R_0603";
  a.valeur = "10k";
  a.fabricant = "Yageo";
  a.mpn = "RC0603FR-0710KL";
  a.fiche = "https://www.yageo.com/upload/media/product/productsearch/datasheet/rchip/PYu-RC_Group_51_RoHS_L_12.pdf";
  a.lcsc = "C25804";
  a.modele3D = "modeles/R_0603.step";
  a.brochage = {{"1", "1"}, {"2", "2"}};
  a.parametres = {{"tolerance", "1 %"}, {"puissance", "0,1 W"}, {"longueur", "1.6"}, {"largeur", "0.8"}, {"masse", "0.002"}, {"temperature_min", "-55"},
                  {"temperature_max", "155"}, {"msl", "1"}, {"rohs", "oui"}, {"conditionnement", "Tape_and_reel"}, {"quantite_conditionnement", "5000"}};
  a.hauteur = 0.45;
  a.base = "resistance-0603";
  a.nonHerites = {"fiche"};
  OffreArticle o;
  o.distributeur = "Mouser";
  o.reference = "603-RC0603FR-0710KL";
  o.conditionnement = 10;
  o.stock = 125000;
  o.prix = {{10, 0.0168}, {100, 0.0094}, {1000, 0.0042}};
  a.offres.push_back(o);
  OffreArticle o2;
  o2.distributeur = "Farnell";
  o2.reference = "9330399";
  o2.devise = "GBP";
  a.offres.push_back(o2);
  a.secondes.push_back({"Vishay", "CRCW060310K0FKEA", "actif", 2});
  a.secondes.push_back({"Panasonic", "ERJ-3EKF1002V", "NRND", 3});
  a.version = 3;
  a.etatCycle = "production";
  a.numeroInterne = "RES-000123";
  return a;
}
}  // namespace

TEST_CASE("10.19 · références de fabricant lues dans les champs des autres logiciels", "[034][L10][10.19]") {
  ConceptionElec c = nouvelleConception("champs");
  ComposantElec a, b, x;
  a.uuid = "a", a.reference = "R1", a.champs = {{"MANUFACTURER PART NUMBER", "RC0603FR-0710KL"}, {"Manufacturer", "Yageo"}, {"Supplier 1", "Mouser"}, {"LCSC Part", "C25804"}};
  b.uuid = "b", b.reference = "U1", b.mpn = "NE555DR", b.champs = {{"MPN", "AUTRE"}, {"MFR", "TI"}};
  x.uuid = "x", x.reference = "C1", x.champs = {{"Manufacturer_Part_Number", "~"}, {"Tolerance", "10 %"}};
  c.composants = {a, b, x};
  CHECK(completerReferencesFabricant(c) == 2);
  CHECK(c.composants[0].mpn == "RC0603FR-0710KL");
  CHECK(c.composants[0].fabricant == "Yageo");
  CHECK(c.composants[0].fournisseur == "Mouser");
  CHECK(c.composants[0].lcsc == "C25804");
  CHECK(c.composants[1].mpn == "NE555DR");  // ce que le composant porte n'est pas écrasé
  CHECK(c.composants[1].fabricant == "TI");
  CHECK(c.composants[2].mpn.empty());  // « ~ » : pas une référence
  CHECK(mpnNormalise(" rc0603fr-07 10kl ") == "RC0603FR-0710KL");
}

TEST_CASE("10.19 · articles rattachés par la référence, en propre ou en seconde source, sinon créés une seule fois avec symbole et empreinte", "[034][L10][10.19]") {
  ContenuBibliotheque bib;
  bib.nom = "Équipe";
  Article r10k;
  r10k.id = "yageo-10k", r10k.nom = "10k", r10k.mpn = "RC0603FR-0710KL", r10k.fabricant = "YAGEO Corporation", r10k.version = 4;
  Article ampli;
  ampli.id = "lm358", ampli.nom = "LM358", ampli.mpn = "LM358DR", ampli.fabricant = "Texas Instruments";
  ampli.secondes.push_back({"STMicroelectronics", "LM358DT", "actif", 2});
  bib.articles = {r10k, ampli};
  // Un symbole de même identifiant, mais autre, est déjà dans la bibliothèque
  DefSymbole autre;
  autre.id = "imp:R";
  autre.nom = "autre";
  bib.symboles.push_back(autre);

  ConceptionElec c = nouvelleConception("rattachement");
  DefSymbole s;
  s.id = "imp:R";
  s.nom = "R importée";
  s.prefixe = "R";
  Broche b1, b2;
  b1.numero = "1", b2.numero = "2";
  s.broches = {b1, b2};
  c.symboles[s.id] = s;
  DefEmpreinte e;
  e.id = "imp:R0603";
  Pastille p;
  p.numero = "1", p.largeur = 800'000, p.hauteur = 950'000, p.position = {-800'000, 0};
  e.pastilles.push_back(p);
  p.numero = "2", p.position = {800'000, 0};
  e.pastilles.push_back(p);
  c.empreintes[e.id] = e;
  auto composant = [&](const std::string& ref, const std::string& mpn, const std::string& fab) {
    ComposantElec k;
    k.uuid = "u-" + ref, k.reference = ref, k.mpn = mpn, k.fabricant = fab, k.symbole = "imp:R", k.empreinte = "imp:R0603", k.valeur = "1k";
    k.champs["Tolerance"] = "1 %";
    c.composants.push_back(k);
  };
  composant("R1", "rc0603fr-0710kl", "Yageo");           // la bibliothèque, référence en minuscules
  composant("U1", "LM358DT", "ST");                      // seconde source d'un article
  composant("R2", "RC0603FR-0710KL", "Vishay");          // même référence, autre fabricant : créé
  composant("R3", "ERJ-3EKF1001V", "Panasonic");         // créé…
  composant("R4", "ERJ-3EKF1001V", "Panasonic");         // … une seule fois
  composant("C1", "", "");                               // sans référence
  composant("#PWR01", "", "");                           // alimentation : ignorée
  const RattachementArticles r = rattacherArticles(c, bib, "Repris de KiCad");
  for (const auto& l : r.lignes) UNSCOPED_INFO(l.reference << " " << l.mpn << " → " << l.article << " " << l.etat << " " << l.detail);
  CHECK(r.rattaches == 3);
  CHECK(r.crees == 2);
  CHECK(r.sansReference == 1);
  CHECK(r.lignes.size() == 6);
  CHECK(c.parReference("R1")->article == "yageo-10k");
  CHECK(c.parReference("R1")->versionArticle == 4);
  CHECK(c.parReference("U1")->article == "lm358");
  CHECK(c.parReference("R2")->article != "yageo-10k");
  CHECK(c.parReference("R3")->article == c.parReference("R4")->article);
  CHECK(c.parReference("C1")->article.empty());
  REQUIRE(bib.articles.size() == 4);
  const Article& cree = bib.articles[3];
  CHECK(cree.mpn == "ERJ-3EKF1001V");
  CHECK(cree.categorie == "Repris de KiCad");
  CHECK(cree.parametres.at("Tolerance") == "1 %");
  // Symbole copié sous un autre nom (l'identifiant était pris), une seule fois ; empreinte copiée
  CHECK(cree.symbole == "imp:R~2");
  CHECK(bib.symboles.size() == 2);
  CHECK(cree.empreinte == "imp:R0603");
  CHECK(bib.empreintes.size() == 1);
  // L'article est dans la conception ; rien à mettre à jour pour un composant de l'article qu'il a fait naître
  REQUIRE(c.articles.count(cree.id));
  CHECK(r.articlesUtilises.size() == 4);
  // Une seconde conception qui emploie les mêmes références ne crée rien
  ConceptionElec c2 = c;
  for (auto& k : c2.composants) k.article.clear();
  const RattachementArticles r2 = rattacherArticles(c2, bib, "Repris de KiCad");
  CHECK(r2.crees == 0);
  CHECK(r2.rattaches == 5);
  CHECK(bib.articles.size() == 4);
}

TEST_CASE("10.19 · fiche IPC/DAC-2552 écrite puis relue : article identique, somme de contrôle juste, fiche retouchée signalée", "[034][L10][10.19]") {
  const Article a = articleComplet();
  const std::string xml = xmlIpc2552(a, nullptr, "2026-10-25T10:00:00Z");
  CHECK(xml.find("<Manufacturer_part_number>RC0603FR-0710KL</Manufacturer_part_number>") != std::string::npos);
  CHECK(xml.find("&lt;0603&gt; &amp; 1 %") != std::string::npos);
  CHECK(xml.find("<Weight unit=\"g\">0.002</Weight>") != std::string::npos);
  CHECK(xml.find("<Checksum algorithm=\"SHA-256\">") != std::string::npos);
  std::string erreur;
  const auto f = articleDepuisIpc2552(xml, &erreur);
  REQUIRE(f);
  CHECK(f->sommeJuste);
  CHECK(f->alertes.empty());
  CHECK(f->article == a);
  // Retouchée après l'écriture : somme fausse, signalée
  std::string retouche = xml;
  retouche.replace(retouche.find("0.002"), 5, "0.003");
  const auto g = articleDepuisIpc2552(retouche);
  REQUIRE(g);
  CHECK_FALSE(g->sommeJuste);
  CHECK(g->article.parametres.at("masse") == "0.003");
  // Avec l'empreinte : boîtier, montage, nombre de broches et cotes du corps
  const DefEmpreinte* e = empreinteEnCache("SOIC-8");
  REQUIRE(e);
  Article b;
  b.id = "ne555", b.mpn = "NE555DR", b.fabricant = "Texas Instruments", b.empreinte = "SOIC-8";
  const std::string xb = xmlIpc2552(b, e);
  CHECK(xb.find("mounting=\"SMT\" terminals=\"8\"") != std::string::npos);
  const auto fb = articleDepuisIpc2552(xb);
  REQUIRE(fb);
  CHECK(fb->article.hauteur > 1.0);
  CHECK(fb->article.parametres.count("longueur"));
  // Pas une fiche
  CHECK_FALSE(articleDepuisIpc2552("<autre/>", &erreur));
  CHECK_FALSE(erreur.empty());
  CHECK_FALSE(articleDepuisIpc2552("<IPC-2552><Component>", &erreur));
}

TEST_CASE("10.19 · boîtier JEP30 écrit puis relu : mêmes broches, empreinte reconstruite aux mêmes pastilles", "[034][L10][10.19]") {
  const DefEmpreinte* e = empreinteEnCache("SOIC-8");
  REQUIRE(e);
  Article a;
  a.mpn = "NE555DR";
  a.fabricant = "Texas Instruments";
  const BoitierJep30 b = boitierDepuisEmpreinte(*e, &a);
  CHECK(b.type == "SO");
  CHECK(b.montage == "CMS");
  CHECK(b.terminaux.size() == 8);
  CHECK(std::abs(b.pas - 1.27) < 1e-3);
  const std::string xml = xmlJep30(b);
  CHECK(xml.find("JEP30-D10:PartModel") != std::string::npos);
  CHECK(xml.find("PartModelStatus=\"Released\"") != std::string::npos);
  const auto relu = lireJep30(xml);
  REQUIRE(relu);
  CHECK(relu->nom == "SOIC-8");
  CHECK(relu->mpn == "NE555DR");
  CHECK(relu->fabricant == "Texas Instruments");
  REQUIRE(relu->terminaux.size() == 8);
  for (std::size_t i = 0; i < 8; ++i) {
    CHECK(relu->terminaux[i].numero == b.terminaux[i].numero);
    CHECK(std::abs(relu->terminaux[i].x - b.terminaux[i].x) < 1e-4);
    CHECK(std::abs(relu->terminaux[i].y - b.terminaux[i].y) < 1e-4);
  }
  const DefEmpreinte r = empreinteDepuisBoitier(*relu);
  CHECK(r.id == "jep30:SOIC-8");
  REQUIRE(r.pastilles.size() == e->pastilles.size());
  for (std::size_t i = 0; i < r.pastilles.size(); ++i) {
    CHECK(r.pastilles[i].numero == e->pastilles[i].numero);
    CHECK(distance(r.pastilles[i].position, e->pastilles[i].position) < 200);
    CHECK(std::abs(r.pastilles[i].largeur - e->pastilles[i].largeur) < 200);
    CHECK(r.pastilles[i].type == TypePastille::CMS);
  }
  CHECK(std::abs(r.corps.hauteur - e->corps.hauteur) < 200);
  CHECK_FALSE(r.encombrement.vide());
  // Cotes en mils converties
  std::string mils = xml;
  const std::string balise = "<JEP30-D10:SeatedHeight unit=\"mm\">";
  REQUIRE(mils.find(balise) != std::string::npos);
  mils.replace(mils.find(balise), balise.size(), "<JEP30-D10:SeatedHeight unit=\"mil\">");
  const auto m = lireJep30(mils);
  REQUIRE(m);
  CHECK(m->hauteur < b.hauteur);
  CHECK_FALSE(lireJep30("<JEP30-D10:PartModel/>"));
}

TEST_CASE("10.19 · conception de référence d'un fabricant importée avec ses articles et ses fiches", "[034][L10][10.19]") {
  ContenuBibliotheque bib;
  bib.nom = "Équipe";
  // Fiche IPC/DAC-2552 de la résistance R3 (RC0603FR-071KL) et boîtier JEP30 joints au projet
  Article fiche;
  fiche.id = "fiche-1k";
  fiche.mpn = "RC0603FR-071KL";
  fiche.fabricant = "Yageo";
  fiche.parametres = {{"msl", "1"}, {"rohs", "oui"}};
  fiche.fiche = "https://www.yageo.com/rc";
  Article seule;
  seule.id = "lm4040";
  seule.mpn = "LM4040AIM3-2.5";
  seule.fabricant = "Texas Instruments";
  BoitierJep30 b;
  b.nom = "SOT-23";
  b.type = "SOT";
  b.longueur = 2.9, b.largeur = 1.3, b.hauteur = 1.1;
  b.terminaux = {{"1", -0.95, -1.1, 0.6, 0.7, 0, 0, "rectangulaire"}, {"2", 0.95, -1.1, 0.6, 0.7, 0, 0, "rectangulaire"}, {"3", 0, 1.1, 0.6, 0.7, 0, 0, "rectangulaire"}};
  const std::vector<std::string> joints = {xmlIpc2552(fiche), xmlIpc2552(seule), xmlJep30(b), "<pas une fiche>"};
  const std::string sch = donnee19("eagle/ampli.sch");
  std::string erreur;
  const auto r = importerConceptionReference("ampli.sch", sch, [](const std::string& f) -> std::optional<std::string> {
    if (f == "ampli.brd") return donnee19("eagle/ampli.brd");
    return std::nullopt;
  }, bib, joints, &erreur);
  REQUIRE(r);
  for (const auto& a : r->alertes) UNSCOPED_INFO(a);
  CHECK(r->import.logiciel == "EAGLE");
  CHECK(r->fichesLues == 2);
  CHECK(r->boitiers == 1);
  CHECK(r->articles.crees == 1);  // R3 seul porte une référence
  CHECK(r->articlesEnrichis == 1);
  CHECK(r->alertes.size() == 1);  // la pièce jointe illisible
  const ComposantElec* r3 = r->import.conception.parReference("R3");
  REQUIRE(r3);
  REQUIRE(!r3->article.empty());
  const Article& copie = r->import.conception.articles.at(r3->article);
  CHECK(copie.parametres.at("msl") == "1");
  CHECK(copie.fiche == "https://www.yageo.com/rc");
  // La fiche sans composant au projet entre comme article ; le boîtier comme empreinte
  CHECK(std::any_of(bib.articles.begin(), bib.articles.end(), [](const Article& a) { return a.mpn == "LM4040AIM3-2.5"; }));
  CHECK(std::any_of(bib.empreintes.begin(), bib.empreintes.end(), [](const DefEmpreinte& e) { return e.id == "jep30:SOT-23" && e.pastilles.size() == 3; }));
}

TEST_CASE("10.19 · projets d'un dossier : un par projet, feuilles filles, documents d'un projet Altium et sauvegardes non comptés à part", "[034][L10][10.19]") {
  const std::vector<std::string> f = {"kicad/K1/K1.kicad_pro", "kicad/K1/K1.kicad_sch", "kicad/K1/K1.kicad_pcb", "kicad/K1/alim.kicad_sch", "kicad/K1/K1-backups/K1.kicad_sch",
                                      "eagle/E1/ampli.sch", "eagle/E1/ampli.brd", "altium/A1/A1.PrjPcb", "altium/A1/A1.SchDoc", "altium/A1/sous/A1b.SchDoc",
                                      "altium/A1/History/A1.SchDoc", "altium/A2/A2.SchDoc", "altium/A2/A2.PcbDoc", "geda\\g\\g.sch", "geda/g/g.pcb", "LISEZMOI.md",
                                      "kicad5/v/v.sch", "kicad5/v/v.kicad_pcb", "kicad5/v/sous.sch", "geda/g/bloc.sch", "sprint/s.lay6", "notes.txt"};
  // Feuilles filles appelées : KiCad 5 (« F1 ») et gschem (« source= »)
  const auto p = projetsDuDossier(f, [](const std::string& x) -> std::optional<std::string> {
    if (x == "kicad5/v/v.sch") return std::string("EESchema Schematic File Version 4\n$Sheet\nS 100 100 500 500\nF0 \"Sous\" 50\nF1 \"sous.sch\" 50\n$EndSheet\n");
    if (x == "geda/g/g.sch") return std::string("v 20130925 2\nC 100 100 1 0 0 bloc.sym\n{\nsource=bloc.sch\n}\n");
    return std::string();
  });
  std::vector<std::string> principaux;
  for (const auto& x : p) principaux.push_back(x.fichier);
  CHECK(principaux == std::vector<std::string>{"altium/A1/A1.PrjPcb", "altium/A2/A2.SchDoc", "eagle/E1/ampli.sch", "geda/g/g.sch", "kicad/K1/K1.kicad_pro", "kicad5/v/v.sch", "sprint/s.lay6"});
  CHECK(p[0].nom == "A1");
}

TEST_CASE("10.19 · dossier de 30 projets Altium, EAGLE et KiCad converti en série : 30 rapports, articles communs rangés une seule fois", "[034][L10][10.19]") {
  const DossierEssai d = dossierDeTrente();
  const auto projets = projetsDuDossier(cheminsDe(d));
  REQUIRE(projets.size() == 30);
  ContenuBibliotheque equipe;
  equipe.nom = "Équipe";
  std::vector<std::string> ecrits;
  std::size_t derniere = 0;
  const BilanSerie b = convertirEnSerie(projets, equipe, lecteurDe(d), [&](const RapportProjet& r, const ConceptionElec& c) {
    CHECK(c.composants.size() == static_cast<std::size_t>(r.composants));
    ecrits.push_back(r.projet);
  }, [&](std::size_t fait, std::size_t total) {
    CHECK(total == 30);
    derniere = fait;
  });
  for (const auto& r : b.projets)
    if (!r.reussi) UNSCOPED_INFO(r.projet << " : " << r.erreur);
  CHECK(b.projets.size() == 30);
  CHECK(b.reussis == 30);
  CHECK(b.echecs == 0);
  CHECK(ecrits.size() == 30);
  CHECK(derniere == 30);
  // Une référence, un article : 41 références distinctes dans les 30 projets
  std::set<std::string> refs;
  for (const auto& a : equipe.articles) CHECK(refs.insert(mpnNormalise(a.mpn)).second);
  std::set<std::string> attendues;
  int communes = 0, emplois = 0;
  for (const auto& [m, projetsDeM] : d.usages) {
    attendues.insert(m);
    communes += projetsDeM.size() >= 2;
    emplois += static_cast<int>(projetsDeM.size());
  }
  CHECK(attendues.size() == 41);
  CHECK(refs == attendues);
  CHECK(b.articlesCrees == 41);
  CHECK(b.articlesCommuns == communes);
  CHECK(communes == 11);
  // Chaque rapport : son logiciel, ses articles ; une référence commune n'est créée que par le premier projet
  std::map<std::string, int> creations;
  int rattaches = 0;
  for (const auto& r : b.projets) {
    CHECK(!r.logiciel.empty());
    CHECK(r.texte().find("# Conversion de « " + r.projet + " »") == 0);
    for (const auto& l : r.articles.lignes)
      if (l.etat == "créé") ++creations[mpnNormalise(l.mpn)];
    rattaches += r.articles.rattaches;
    if (r.logiciel == "KiCad") CHECK(r.articles.sansReference == 3);  // trous de fixation H2 à H4
  }
  for (const auto& [m, n] : creations) CHECK(n == 1);
  CHECK(creations.size() == 41);
  CHECK(rattaches == emplois - 41);  // chaque emploi d'une référence après sa création
  CHECK(b.texte().find("30 projet(s) : 30 converti(s)") != std::string::npos);
}

TEST_CASE("10.19 · série sans arrêt au premier échec, et série interrompue à la demande", "[034][L10][10.19]") {
  DossierEssai d;
  projetAltium(d, 1);
  d.fichiers["casse/casse.kicad_pcb"] = "(kicad_pcb (version 20221018) (generator pcbnew) (footprint \"R\" (layer \"F.Cu\") (pad \"1\" smd rect (at 0 0) (size";
  projetEagle(d, 2);
  projetKicad(d, 3);
  const auto projets = projetsDuDossier(cheminsDe(d));
  REQUIRE(projets.size() == 4);
  ContenuBibliotheque equipe;
  const BilanSerie b = convertirEnSerie(projets, equipe, lecteurDe(d));
  CHECK(b.projets.size() == 4);
  CHECK(b.echecs == 1);
  CHECK(b.reussis == 3);
  const auto casse = std::find_if(b.projets.begin(), b.projets.end(), [](const RapportProjet& r) { return r.projet == "casse"; });
  REQUIRE(casse != b.projets.end());
  CHECK_FALSE(casse->reussi);
  CHECK_FALSE(casse->erreur.empty());
  CHECK(casse->texte().find("échec") != std::string::npos);
  // Arrêt demandé après le deuxième projet
  std::atomic<bool> arret{false};
  ContenuBibliotheque e2;
  const BilanSerie b2 = convertirEnSerie(projets, e2, lecteurDe(d), {}, [&](std::size_t fait, std::size_t) { arret = fait >= 2; }, &arret);
  CHECK(b2.interrompu);
  CHECK(b2.projets.size() == 2);
}

TEST_CASE("10.19 · guide des termes et des gestes de l'ancien outil", "[034][L10][10.19]") {
  for (const std::string l : {"KiCad", "EAGLE", "Altium", "PADS", "OrCAD", "EasyEDA", "gEDA", "Sprint-Layout", "Protel Autotrax", "P-CAD", "CADSTAR", "LTspice"}) {
    INFO(l);
    CHECK(guideAncienOutil(l).size() >= 4);
  }
  CHECK(logicielsDuGuide().size() == 12);
  const std::vector<TermeGuide> k = guideAncienOutil("KiCad 5");
  CHECK(std::any_of(k.begin(), k.end(), [](const TermeGuide& t) { return t.promethee == "Synchroniser" && t.geste.find("F8") != std::string::npos; }));
  CHECK(guideAncienOutil("CircuitMaker").front().ancien == guideAncienOutil("Altium").front().ancien);
  CHECK(guideAncienOutil("Lepton").front().ancien == guideAncienOutil("gEDA").front().ancien);
  CHECK(!guideAncienOutil("Visio").empty());  // termes communs
  const std::string h = guideHtml("Altium");
  CHECK(h.find("<table>") != std::string::npos);
  CHECK(h.find("Polygon Pour") != std::string::npos);
  CHECK(h.find("Ctrl+Z") != std::string::npos);
}
