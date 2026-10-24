// Prométhée : essais de la 0.34, lot L4 côté noyau — approvisionnement hors ligne (4.18), échange en masse (4.20),
// tableur et gestion de bibliothèque (4.23), générateur IPC étendu (4.25), série par noms (4.26), règles
// d'équivalence (4.28).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/approvisionnement.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/articles.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/gabarits_ipc.hpp"
#include "promethee/gestion_bibliotheque.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/remplacement.hpp"
#include "promethee/synchronisation.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
// Composant d'un symbole posé
ComposantElec& composantDe(ConceptionElec& c, const std::string& symbole) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == symbole) return *c.composant(s.composant);
  throw std::runtime_error("symbole introuvable");
}
// n résistances posées au schéma, valeur et empreinte données
std::vector<std::string> resistances(ConceptionElec& c, int n, const std::string& valeur, const std::string& empreinte, double y = 20) {
  std::vector<std::string> r;
  for (int i = 0; i < n; ++i) {
    ComposantElec& k = composantDe(c, poserSymbole(c, 0, "R", ptMm(10 + 7.62 * (i % 20), y + 10.16 * (i / 20))));
    k.valeur = valeur;
    k.empreinte = empreinte;
    r.push_back(k.uuid);
  }
  return r;
}
}  // namespace

TEST_CASE("4.18 · huit résistances de 10 kΩ par carte et 50 en tiroir : 30 à acheter pour dix cartes", "[034][L4][4.18]") {
  ConceptionElec c = nouvelleConception("Achat");
  resistances(c, 8, "10k", "R_0603");
  resistances(c, 2, "4k7", "R_0603", 60);
  annoter(c, true);
  // Stock de l'atelier en CSV (en-têtes français, point-virgule) : 50 résistances de 10 kΩ en 0603 ; catalogue de
  // l'assembleur en CSV anglais
  std::string erreur;
  const SourceStock tiroirs = lireSourceStock("Valeur;Boîtier;Quantité;Emplacement\n10 kΩ;0603;50;Tiroir A3\n4,7k;0805;100;Tiroir B1\n", "Tiroirs", true, &erreur);
  REQUIRE(erreur.empty());
  REQUIRE(tiroirs.lignes.size() == 2);
  const SourceStock jlc = lireSourceStock("LCSC Part,Comment,Package,Stock,Price,Type\nC25804,10k,0603,500000,0.0005,Basic\nC23162,4.7k,0603,300000,0.0006,Basic\n", "JLCPCB", false, &erreur);
  REQUIRE(jlc.lignes.size() == 2);
  CHECK(jlc.lignes[0].basique);
  const auto b = besoinsAchat(c, 10, {tiroirs, jlc});
  const auto dix = std::find_if(b.begin(), b.end(), [](const BesoinAchat& x) { return x.ligne.quantite == 8; });
  REQUIRE(dix != b.end());
  CHECK(dix->besoin == 80);
  CHECK(dix->enStock == 50);
  CHECK(dix->aAcheter == 30);
  CHECK(dix->source.find("JLCPCB C25804") != std::string::npos);
  CHECK(dix->source.find("Tiroir A3") != std::string::npos);
  // 4k7 : le tiroir est en 0805, il ne couvre pas le 0603 ; tout s'achète
  const auto quatre = std::find_if(b.begin(), b.end(), [](const BesoinAchat& x) { return x.ligne.quantite == 2; });
  REQUIRE(quatre != b.end());
  CHECK(quatre->enStock == 0);
  CHECK(quatre->aAcheter == 20);
  CHECK(csvBesoins(b).find("30") != std::string::npos);

  // Offres : conditionnement et paliers comparés ; la bobine de 5 000 l'emporte au-delà de son seuil de prix
  std::vector<OffreArticle> offres(2);
  offres[0].distributeur = "Mouser";
  offres[0].reference = "M-1";
  offres[0].conditionnement = 1;
  offres[0].prix = {{1, 0.10}, {100, 0.02}};
  offres[1].distributeur = "Digikey";
  offres[1].reference = "D-1";
  offres[1].conditionnement = 5000;
  offres[1].prix = {{5000, 0.002}};
  CHECK(meilleureOffre(offres, 30).offre == &offres[0]);
  CHECK(meilleureOffre(offres, 30).total == Approx(2.0));  // 100 pièces au palier de 0,02 coûtent moins que 30 à 0,10
  CHECK(meilleureOffre(offres, 30).quantite == 100);
  CHECK(meilleureOffre(offres, 4000).offre == &offres[1]);
  CHECK(meilleureOffre(offres, 4000).quantite == 5000);

  // Remplaçant : même boîtier et même brochage
  Article a, x, y;
  a.id = "A";
  a.empreinte = "SOT-23";
  a.symbole = "Q_NPN";
  a.categorie = "Transistors";
  x = a;
  x.id = "X";
  y = a;
  y.id = "Y";
  y.brochage = {{"1", "2"}, {"2", "1"}};
  std::string raison;
  CHECK(remplacantCompatible(a, x, &raison));
  CHECK_FALSE(remplacantCompatible(a, y, &raison));
  CHECK_FALSE(raison.empty());
}

TEST_CASE("4.20 · R* de valeur 10k : douze symboles échangés en une commande, repères et valeurs gardés", "[034][L4][4.20]") {
  ConceptionElec c = nouvelleConception("Masse");
  resistances(c, 12, "10k", "R_0603");
  resistances(c, 3, "4k7", "R_0603", 60);
  annoter(c, true);
  appliquerEcarts(c, ecartsSynchronisation(c));
  REQUIRE(c.circuit.empreintes.size() == 15);
  // Symbole de remplacement : la même résistance au dessin américain, dans le projet
  DefSymbole us = *c.symbole("R");
  us.id = "R_US";
  us.nom = "Résistance (dessin américain)";
  c.symboles["R_US"] = us;
  PorteeMasse p;
  p.reference = "R*";
  p.valeur = "10 kΩ";  // comparée comme nombre
  const auto vises = composantsVises(c, p);
  REQUIRE(vises.size() == 12);
  std::map<std::string, std::pair<std::string, std::string>> avant;
  for (const auto& id : vises) avant[id] = {c.composant(id)->reference, c.composant(id)->valeur};
  RegleRemplacement r;
  r.garderRepere = true;
  r.garderValeur = true;
  r.garderEmpreinte = true;
  std::string erreur;
  CHECK(remplacerComposants(c, vises, "R_US", r, &erreur) == 12);
  CHECK(erreur.empty());
  for (const auto& id : vises) {
    const ComposantElec* k = c.composant(id);
    CHECK(k->symbole == "R_US");
    CHECK(k->reference == avant[id].first);
    CHECK(k->valeur == avant[id].second);
  }
  // Les 4k7 ne bougent pas
  PorteeMasse autres;
  autres.valeur = "4k7";
  for (const auto& id : composantsVises(c, autres)) CHECK(c.composant(id)->symbole == "R");
  CHECK(correspondJoker("R1?", "R12"));
  CHECK_FALSE(correspondJoker("R1?", "R1"));
  CHECK(correspondJoker("*0603*", "R_0603_1608Metric"));

  // Empreintes échangées en masse sur la carte, en place : textes et attributs gardés ou remis
  auto& e0 = *std::find_if(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == vises[0]; });
  e0.repere = ptMm(0, -2);
  const Pt place = e0.position;
  c.composant(vises[1])->modele3D = "modele-R";
  RegleEchangeEmpreinte g;
  g.garderModele3D = false;
  CHECK(echangerEmpreintesEnMasse(c, vises, "R_0805", g) == 12);
  for (const auto& e : c.circuit.empreintes)
    if (std::find(vises.begin(), vises.end(), e.composant) != vises.end()) CHECK(e.empreinte == "R_0805");
  const auto& e1 = *std::find_if(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == vises[0]; });
  CHECK(e1.repere == ptMm(0, -2));
  CHECK(e1.position == place);  // même point de placement : centre des pastilles
  CHECK(c.composant(vises[1])->modele3D.empty());
  CHECK(c.composant(vises[0])->empreinte == "R_0805");
}

TEST_CASE("4.23 · série E24 de 1 Ω à 1 MΩ en 0603 : 145 articles nommés d'après leur gabarit, contrôles sans erreur", "[034][L4][4.23]") {
  Article modele;
  modele.symbole = "R";
  modele.empreinte = "R_0603";
  modele.categorie = "Résistances";
  modele.parametres["tolerance"] = "1 %";
  modele.parametres["puissance"] = "0,1 W";
  modele.hauteur = 0.45;
  RenommageGabarit g;
  g.gabaritId = "R_{boitier}_{valeur}_{tolerance}";
  g.gabaritNom = "Résistance {valeurSI} {boitier} {tolerance}";
  const auto serie = serieArticles(modele, 24, 1, 1e6, g);
  REQUIRE(serie.size() == 145);
  CHECK(serie.front().valeur == "1R");
  CHECK(serie.back().valeur == "1M");
  CHECK(serie[0].id == "R_0603_1R_1%");
  std::set<std::string> ids;
  for (const auto& a : serie) ids.insert(a.id);
  CHECK(ids.size() == 145);
  CHECK(std::any_of(serie.begin(), serie.end(), [](const Article& a) { return a.id == "R_0603_4k7_1%"; }));
  ContenuBibliotheque bib;
  bib.nom = "Passifs";
  bib.articles = serie;
  CHECK(controlerBibliotheque(bib).empty());

  // Renommage par gabarit d'une sélection ; les héritiers suivent
  std::vector<Article> v = serie;
  Article fils;
  fils.id = "FILS";
  fils.base = "R_0603_4k7_1%";
  v.push_back(fils);
  RenommageGabarit autre;
  autre.gabaritId = "RES-{valeur}";
  CHECK(renommerParGabarit(v, autre, {"R_0603_4k7_1%"}) == 1);
  CHECK(v.back().base == "RES-4k7");

  // Articles depuis un tableau aux colonnes dans le désordre (en-têtes anglais et français)
  const auto t = lireTableau("Manufacturer;Valeur;MPN;Package;Catégorie;Stock local\nYageo;10k;RC0603FR-0710KL;0603;Résistances;12\nYageo;4k7;RC0603FR-074K7L;0603;Résistances;3\n;;;;;\n");
  const auto imp = articlesDepuisTableau(t);
  REQUIRE(imp.articles.size() == 2);
  CHECK(imp.articles[0].mpn == "RC0603FR-0710KL");
  CHECK(imp.articles[0].empreinte == "R_0603");
  CHECK(imp.articles[0].symbole == "R");
  CHECK(imp.articles[0].parametres.at("stocklocal") == "12");
  CHECK(imp.colonnes.at("Package") == "param:boitier");
  // Tableau d'édition en masse, relu : une révision de plus par article changé
  auto table = tableauArticles(imp.articles, {"fabricant", "param:tolerance"});
  table[1][2] = "1 %";
  std::vector<Article> edites = imp.articles;
  CHECK(appliquerTableauArticles(edites, table) == 1);
  CHECK(edites[0].parametres.at("tolerance") == "1 %");
  CHECK(edites[0].version == 2);

  // Broches d'un symbole depuis un tableau : côté, rang, unité
  const auto broches = lireTableau("Pin;Name;Type;Side;Rank\n1;VCC;power;haut;1\n2;OUT;sortie;droite;1\n3;IN;entrée;gauche;2\n4;EN;entrée;gauche;1\n5;GND;alim;bas;1\n");
  const SpecSymbole spec = specDepuisTableau(broches, "Régulateur");
  REQUIRE(spec.broches.size() == 5);
  const auto it3 = std::find_if(spec.broches.begin(), spec.broches.end(), [](const SpecBroche& b) { return b.numero == "3"; });
  const auto it4 = std::find_if(spec.broches.begin(), spec.broches.end(), [](const SpecBroche& b) { return b.numero == "4"; });
  CHECK(it4 < it3);  // EN (rang 1) avant IN (rang 2) sur le côté gauche
  CHECK(it3->cote == 0);
  CHECK(it3->type == TypeBroche::Entree);
  const DefSymbole sym = genererSymbole(spec);
  CHECK(sym.broches.size() == 5);

  // Copie vers une autre bibliothèque avec ce dont l'article dépend : base, symbole, empreinte, modèle 3D
  ContenuBibliotheque src;
  src.nom = "Source";
  DefSymbole s = sym;
  s.id = "perso:REG";
  src.symboles.push_back(s);
  DefEmpreinte e = *genererEmpreinte("SOT-23-5");
  e.id = "perso:SOT-23-5";
  src.empreintes.push_back(e);
  src.modeles3D["3d/reg.step"] = "ISO-10303-21;";
  Article base;
  base.id = "REG-BASE";
  base.symbole = "perso:REG";
  base.empreinte = "perso:SOT-23-5";
  base.modele3D = "3d/reg.step";
  base.categorie = "Régulateurs";
  Article reg;
  reg.id = "REG-3V3";
  reg.base = "REG-BASE";
  reg.valeur = "3,3 V";
  src.articles = {base, reg};
  const Dependances d = dependancesArticle(src, "REG-3V3");
  CHECK(d.articles.size() == 2);
  CHECK(d.symboles == std::vector<std::string>{"perso:REG"});
  CHECK(d.empreintes == std::vector<std::string>{"perso:SOT-23-5"});
  CHECK(d.modeles3D == std::vector<std::string>{"3d/reg.step"});
  CHECK(d.manquants.empty());
  ContenuBibliotheque dst;
  const RapportCopie rc = copierVersBibliotheque(src, {"REG-3V3"}, dst);
  CHECK(rc.articles == 2);
  CHECK(rc.symboles == 1);
  CHECK(rc.empreintes == 1);
  CHECK(rc.modeles3D == 1);
  // Relue depuis son fichier, la bibliothèque copiée est entière
  std::string err;
  const ContenuBibliotheque relue = lireContenuBibliotheque(texteContenuBibliotheque(dst), &err);
  CHECK(err.empty());
  CHECK(relue.articles == dst.articles);
  CHECK(relue.symboles.size() == 1);
  CHECK(relue.modeles3D.size() == 1);
  // Déjà présent : gardé (et dit) sans « remplacer »
  dst.articles[1].valeur = "5 V";
  const RapportCopie encore = copierVersBibliotheque(src, {"REG-3V3"}, dst);
  CHECK(encore.gardes.size() == 1);
  CHECK(dst.articles[1].valeur == "5 V");
}

TEST_CASE("4.25 · BGA-256 au pas de 1 mm : pastilles à 0,05 mm près de l'empreinte KiCad équivalente", "[034][L4][4.25]") {
  const std::string nom = "BGA-256_17.0x17.0mm_Layout16x16_P1.0mm_Ball0.5mm_Pad0.4mm_NSMD";
  const auto kicad = empreinteDepuisKicadMod(prom::lireFichier(std::string(PROMETHEE_DONNEES) + "/kicad/" + nom + ".kicad_mod"));
  REQUIRE(kicad);
  const auto p = gabaritDepuisNom(nom);
  REQUIRE(p);
  CHECK(p->genre == GenreGabarit::BGA);
  CHECK(p->rangees == 16);
  const DefEmpreinte g = empreinteGabarit(*p, DensiteIPC::Nominale);
  REQUIRE(g.pastilles.size() == 256);
  REQUIRE(kicad->pastilles.size() == 256);
  for (const auto& k : kicad->pastilles) {
    const auto it = std::find_if(g.pastilles.begin(), g.pastilles.end(), [&](const Pastille& x) { return x.numero == k.numero; });
    REQUIRE(it != g.pastilles.end());
    CHECK(std::abs(mm(it->position.x - k.position.x)) <= 0.05);
    CHECK(std::abs(mm(it->position.y - k.position.y)) <= 0.05);
    CHECK(std::abs(mm(it->largeur - k.largeur)) <= 0.05);
  }
  CHECK(lettreRangee(8) == "J");    // I sauté
  CHECK(lettreRangee(15) == "T");   // A … T : seize rangées sans I, O, Q, S
  CHECK(lettreRangee(20) == "AA");
  CHECK(controlerEmpreinte(g).empty());

  // DFN à plot central : pastilles écartées du plot d'au moins 0,2 mm, pâte du plot découpée en motif
  const auto dfn = empreinteParNom("DFN-8-3x3-0.65-EP1.6x2.4");
  REQUIRE(dfn);
  const auto plot = std::find_if(dfn->pastilles.begin(), dfn->pastilles.end(), [](const Pastille& x) { return x.numero == "9"; });
  REQUIRE(plot != dfn->pastilles.end());
  CHECK(plot->motifPate.has_value());
  const Polygone fp = formePastille(*plot);
  for (const auto& q : dfn->pastilles)
    if (q.numero != "9") CHECK(distancePolygones(formePastille(q), fp) >= nm(0.2) - 2);
  CHECK(controlerEmpreinte(*dfn).empty());
  // SOIC : pattes rognées pour garder 0,15 mm entre pastilles voisines
  const auto soic = empreinteParNom("SOIC-8", DensiteIPC::Plus);
  REQUIRE(soic);
  CHECK(mm(soic->pastilles[0].hauteur) <= 1.27 - 0.15 + 1e-6);
  // PLCC-44 : broche 1 au milieu du côté du haut, la 2 à sa gauche
  const auto plcc = empreinteParNom("PLCC-44");
  REQUIRE(plcc);
  CHECK(plcc->pastilles[0].numero == "1");
  CHECK(plcc->pastilles[0].position.x == 0);
  CHECK(plcc->pastilles[1].position.x < 0);
  CHECK(plcc->pastilles[0].position.y == plcc->pastilles[1].position.y);
}

TEST_CASE("4.26 · liste de 40 noms en densités M, N et L : 120 empreintes rangées, contrôles sans erreur", "[034][L4][4.26]") {
  const std::vector<std::string> noms = {
      "R_0402", "R_0603", "R_0805", "R_1206", "C_0402", "C_0603", "C_0805", "C_1210", "L_0805", "LED_0603",
      "SOIC-8", "SOIC-14", "SOIC-16", "SOIC-16W", "TSSOP-14", "TSSOP-20", "SSOP-28", "MSOP-10",
      "QFN-24-4x4-0.5", "QFN-32-5x5-0.5", "TQFP-32-7x7-0.8", "TQFP-64-10x10-0.5", "LQFP-48-7x7-0.5",
      "SOT-23", "SOT-23-5", "SOT-23-6", "SOT-89", "DPAK", "D2PAK",
      "SOD-123", "SOD-323", "MiniMELF", "SMA", "SMB", "SMC",
      "DFN-8-3x3-0.65", "Crystal_SMD_3225-4Pin", "PLCC-44", "BGA-256_17.0x17.0mm_Layout16x16_P1.0mm_Ball0.5mm", "Edge-2x10_P2.54"};
  REQUIRE(noms.size() == 40);
  std::vector<std::string> refus;
  const auto serie = serieEmpreintes(noms, {DensiteIPC::Plus, DensiteIPC::Nominale, DensiteIPC::Moins}, &refus);
  for (const auto& r : refus) UNSCOPED_INFO(r);
  CHECK(refus.empty());
  REQUIRE(serie.size() == 120);
  std::set<std::string> ids;
  for (const auto& e : serie) {
    ids.insert(e.id);
    const auto d = controlerEmpreinte(e);
    for (const auto& x : d) UNSCOPED_INFO(e.id << " : " << x);
    CHECK(d.empty());
  }
  CHECK(ids.size() == 120);
  // La densité M donne des pastilles plus grandes que la L, pour la même puce
  const auto m = std::find_if(serie.begin(), serie.end(), [](const DefEmpreinte& e) { return e.id.rfind("RESC1608", 0) == 0 && e.id.back() == 'M'; });
  const auto l = std::find_if(serie.begin(), serie.end(), [](const DefEmpreinte& e) { return e.id.rfind("RESC1608", 0) == 0 && e.id.back() == 'L'; });
  REQUIRE(m != serie.end());
  REQUIRE(l != serie.end());
  CHECK(m->pastilles[0].largeur > l->pastilles[0].largeur);
  // Rangées dans une bibliothèque, relue
  ContenuBibliotheque b;
  b.nom = "Générées";
  b.empreintes = serie;
  const auto relue = lireContenuBibliotheque(texteContenuBibliotheque(b));
  CHECK(relue.empreintes.size() == 120);

  // Variante d'empreinte d'après la carte : brasage à la vague → densité M, échangée en place
  ConceptionElec c = nouvelleConception("Variantes");
  const std::string r1 = poserEmpreinteSeule(c, "SOIC-8", ptMm(20, 20));
  (void)r1;
  c.circuit.fabrication.procede = "vague";
  CHECK(varianteEmpreinte("SOIC-8", 'N', "vague") == "SOIC127P600X175-8M");
  CHECK(appliquerVariantes(c) == 1);
  CHECK(c.circuit.empreintes.front().empreinte == "SOIC127P600X175-8M");
  c.circuit.fabrication.procede = "refusion";
  c.circuit.fabrication.densite = 'L';
  CHECK(appliquerVariantes(c) == 1);
  CHECK(c.circuit.empreintes.front().empreinte == "SOIC127P600X175-8L");
}

TEST_CASE("4.28 · règle d'équivalence « Résistance 0603 → R_0603 » : 48 composants affectés en une commande", "[034][L4][4.28]") {
  ConceptionElec c = nouvelleConception("Affectation");
  auto sans = resistances(c, 48, "10k", "");
  for (const auto& id : sans) c.composant(id)->champs["Boîtier"] = "0603";
  // Trois résistances en 0805 et quatre condensateurs 0603 : pas visés par la règle
  for (const auto& id : resistances(c, 3, "1k", "", 80)) c.composant(id)->champs["Boîtier"] = "0805";
  for (int i = 0; i < 4; ++i) {
    ComposantElec& k = composantDe(c, poserSymbole(c, 0, "C", ptMm(10 + 7.62 * i, 110)));
    k.valeur = "100n 0603";  // boîtier écrit dans la valeur
    k.empreinte.clear();
  }
  std::string erreur;
  const auto r = lireRegleEquivalence("Résistance 0603 → R_0603", &erreur);
  REQUIRE(r);
  CHECK(r->famille == FamilleValeur::Resistance);
  CHECK(r->boitier == "0603");
  CHECK(composantsDeRegle(c, *r).size() == 48);
  const auto rc = lireRegleEquivalence("C* 0603 -> C_0603");
  REQUIRE(rc);
  CHECK(rc->reference == "C*");
  std::vector<std::string> journal;
  CHECK(appliquerEquivalences(c, {*r}, false, &journal) == 48);
  for (const auto& id : sans) CHECK(c.composant(id)->empreinte == "R_0603");
  CHECK(std::count_if(c.composants.begin(), c.composants.end(), [](const ComposantElec& k) { return k.empreinte == "R_0603"; }) == 48);
  CHECK(journal.size() == 1);
  annoter(c, true);
  CHECK(appliquerEquivalences(c, {*rc}) == 4);
  CHECK_FALSE(lireRegleEquivalence("Résistance 0603", &erreur));
  CHECK_FALSE(erreur.empty());
}
