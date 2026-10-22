// Prométhée : essais de la CAO électronique (connectivité, ERC, empreintes, circuit, cuivre, DRC, fabrication).
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <set>
#include <sstream>

#include "promethee/dxf.hpp"
#include "promethee/electronique.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
// Montage d'essai : connecteur J1 (1 : +5V, 2 : GND), résistance R1, LED D1 vers la masse
struct Montage {
  ConceptionElec c = nouvelleConception("Témoin");
  std::string j, r, d;
  Pt broche(const std::string& symbole, const std::string& numero) {
    for (const auto& s : c.feuilles[0].symboles)
      if (s.uuid == symbole)
        for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
          if (b.broche->numero == numero) return b.p;
    return {};
  }
  void fil(Pt a, Pt b) { c.feuilles[0].fils.push_back({nouvelUuid(), a, b}); }
  Montage() {
    j = poserSymbole(c, 0, "Conn_01x02", ptMm(20.32, 50.8));
    r = poserSymbole(c, 0, "R", ptMm(50.8, 38.1));
    d = poserSymbole(c, 0, "LED", ptMm(63.5, 50.8), 3);  // anode en haut, cathode en bas
    const std::string p5 = poserSymbole(c, 0, "+5V", ptMm(30.48, 30.48));
    const std::string gnd = poserSymbole(c, 0, "GND", ptMm(30.48, 66.04));
    const Pt j1 = broche(j, "1"), j2 = broche(j, "2"), r1 = broche(r, "1"), r2 = broche(r, "2"), a = broche(d, "2"), k = broche(d, "1");
    const Pt alim = broche(p5, "1"), masse = broche(gnd, "1");
    fil(j1, {alim.x, j1.y});
    fil({alim.x, j1.y}, alim);
    fil(alim, {r1.x, alim.y});
    fil({r1.x, alim.y}, r1);
    fil(r2, {r2.x, a.y});
    fil({r2.x, a.y}, a);
    fil(k, {k.x, masse.y});
    fil({k.x, masse.y}, masse);
    fil(j2, {masse.x, j2.y});
    fil({masse.x, j2.y}, masse);
  }
};
int compter(const std::vector<Violation>& v, const std::string& code) { return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; })); }
int erreurs(const std::vector<Violation>& v) { return static_cast<int>(std::count_if(v.begin(), v.end(), [](const Violation& x) { return x.gravite == Gravite::Erreur; })); }
}  // namespace

TEST_CASE("Électronique : bibliothèque, connectivité, annotation et vérification électrique d'un montage LED") {
  Montage m;
  CHECK(bibliothequeSymboles().size() >= 30);
  CHECK(m.c.symboles.count("LED") == 1);  // définition verrouillée dans le projet
  Connectivite k = connectivite(m.c);
  const ComposantElec* r = nullptr;
  for (const auto& x : m.c.composants) if (x.symbole == "R") r = &x;
  REQUIRE(r);
  CHECK(k.nom(r->uuid, "1") == "+5V");
  const int iGnd = k.indice("GND");
  REQUIRE(iGnd >= 0);
  CHECK(k.reseaux[static_cast<std::size_t>(iGnd)].broches.size() == 2);
  // Avant annotation : références « ? »
  auto v = verifierERC(m.c, k);
  CHECK(compter(v, "ERC-02") == 3);
  CHECK(annoter(m.c, false) == 3);
  CHECK(m.c.parReference("R1"));
  CHECK(m.c.parReference("D1"));
  CHECK(m.c.parReference("J1"));
  k = connectivite(m.c);
  v = verifierERC(m.c, k);
  CHECK(erreurs(v) == 0);
  CHECK(k.nom(m.c.parReference("D1")->uuid, "2") == "N-D1-2");
  // Une étiquette GND posée sur le +5V : court-circuit
  m.c.feuilles[0].etiquettes.push_back({nouvelUuid(), "GND", m.broche(m.r, "1"), 0, TypeEtiquette::Globale});
  CHECK(compter(verifierERC(m.c, connectivite(m.c)), "ERC-23") == 1);
  m.c.feuilles[0].etiquettes.clear();
  // Nomenclature et liste des réseaux
  const auto bom = nomenclature(m.c);
  CHECK(bom.size() == 3);
  CHECK(netlistTexte(m.c, k).find("GND : D1.1 J1.2") != std::string::npos);
}

TEST_CASE("Électronique : fils en T, croisements sans jonction, broche non connectée") {
  ConceptionElec c = nouvelleConception("Essai");
  auto& f = c.feuilles[0];
  f.fils.push_back({"a", ptMm(0, 0), ptMm(20, 0)});
  f.fils.push_back({"b", ptMm(10, 0), ptMm(10, 10)});    // T : bout posé au milieu du premier fil
  f.fils.push_back({"c", ptMm(30, -5), ptMm(30, 5)});
  f.fils.push_back({"d", ptMm(25, 0), ptMm(35, 0)});     // croise « c » sans jonction
  Connectivite k = connectivite(c);
  CHECK(k.parFil["a"] == k.parFil["b"]);
  CHECK(k.parFil["c"] != k.parFil["d"]);
  f.jonctions.push_back({ptMm(30, 0), nouvelUuid()});
  k = connectivite(c);
  CHECK(k.parFil["c"] == k.parFil["d"]);
  // Résistance seule : deux broches non connectées (avertissements, broches passives)
  const std::string s = poserSymbole(c, 0, "R", ptMm(60, 60));
  (void)s;
  annoter(c, true);
  CHECK(compter(verifierERC(c, connectivite(c)), "ERC-10") == 2);
}

TEST_CASE("Électronique : empreintes paramétriques") {
  const auto soic = genererEmpreinte("SOIC-8");
  REQUIRE(soic);
  CHECK(soic->pastilles.size() == 8);
  CHECK(mm(soic->pastilles[1].position.y - soic->pastilles[0].position.y) == Approx(1.27));
  CHECK(soic->pastilles[0].position.x < 0);
  const auto qfn = genererEmpreinte("QFN-32-5x5-0.5");
  REQUIRE(qfn);
  CHECK(qfn->pastilles.size() == 33);
  CHECK(genererEmpreinte("PinHeader_2x05_P2.54")->pastilles.size() == 10);
  const auto r = genererEmpreinte("R_0603");
  REQUIRE(r);
  CHECK(mm(r->pastilles[1].position.x - r->pastilles[0].position.x) == Approx(1.65));
  CHECK(r->corps.hauteur > 0);
  CHECK_FALSE(genererEmpreinte("Inconnu_42"));
  CHECK(catalogueEmpreintes().size() > 60);
  const auto trou = genererEmpreinte("MountingHole_3.2");
  REQUIRE(trou);
  CHECK(trou->pastilles.front().type == TypePastille::NonMetallisee);
}

TEST_CASE("Électronique : circuit imprimé — mise à jour, chevelu, routage, DRC, zone, fabrication, lien boîtier") {
  Montage m;
  annoter(m.c, false);
  auto& ci = m.c.circuit;
  ci.contour.largeur = nm(40);
  ci.contour.hauteur = nm(30);
  const BilanMiseAJour b = mettreAJourCircuit(m.c);
  CHECK(b.ajoutees == 3);
  // Placement : connecteur à gauche, résistance et LED à droite (repère de la carte, y vers le bas)
  for (auto& e : ci.empreintes) {
    const std::string ref = m.c.composant(e.composant)->reference;
    if (ref == "J1") e.position = ptMm(6, 15);
    if (ref == "R1") e.position = ptMm(20, 10);
    if (ref == "D1") e.position = ptMm(30, 20);
  }
  Connectivite k = connectivite(m.c);
  Remplissages zones = remplirZones(m.c, k);
  EtatRoutage etat = etatRoutage(m.c, k, zones);
  CHECK(etat.reseauxTotal == 3);
  CHECK(etat.chevelu.size() == 3);
  // Routage : chaque liaison du chevelu devient une piste sur le cuivre du dessus
  for (const auto& l : etat.chevelu) ci.pistes.push_back({nouvelUuid(), l.reseau, Couche::CuivreDessus, l.a, l.b, nm(0.3)});
  etat = etatRoutage(m.c, k, zones);
  CHECK(etat.chevelu.empty());
  CHECK(etat.reseauxComplets == 3);
  auto drc = verifierDRC(m.c, k, zones);
  CHECK(compter(drc, "DRC-01") == 0);
  CHECK(compter(drc, "DRC-20") == 0);
  CHECK(compter(drc, "DRC-21") == 0);
  // Une piste GND qui frôle la pastille +5V du connecteur : isolement violé, puis court-circuit
  const auto pp = pastilles(m.c, k);
  Pt p5{};
  for (const auto& p : pp) if (p.reference == "J1" && p.numero == "1") p5 = p.pastille.position;
  ci.pistes.push_back({"fautive", "GND", Couche::CuivreDessus, p5 + ptMm(-3, 1.1), p5 + ptMm(3, 1.1), nm(0.3)});
  drc = verifierDRC(m.c, k, zones);
  CHECK(compter(drc, "DRC-01") >= 1);
  ci.pistes.pop_back();
  // Zone de masse sur le dessous : elle évite la pastille +5V et se relie à la pastille GND par des rayons
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = "GND";
  z.couche = Couche::CuivreDessous;
  z.contour = {ptMm(0, 0), ptMm(40, 0), ptMm(40, 30), ptMm(0, 30)};
  ci.zones.push_back(z);
  zones = remplirZones(m.c, k);
  REQUIRE(zones.count(z.uuid));
  CHECK(aireMm2(zones[z.uuid]) > 500);
  CHECK_FALSE(dedans(zones[z.uuid], p5));
  CHECK(compter(verifierDRC(m.c, k, zones), "DRC-21") == 0);
  // Fabrication
  const ModeleFabrication fab = modeleFabrication(m.c, k, zones);
  const std::string g = gerber(fab, Couche::CuivreDessus, "essai");
  CHECK(g.find("%TF.FileFunction,Copper,L1,Top*%") != std::string::npos);
  CHECK(g.find("%FSLAX46Y46*%") != std::string::npos);
  CHECK(g.find("D03*") != std::string::npos);
  CHECK(g.find("M02*") != std::string::npos);
  CHECK(gerber(fab, Couche::CuivreDessous, "essai").find("G36*") != std::string::npos);
  CHECK(gerber(fab, Couche::VernisDessus, "essai").find("FilePolarity,Negative") != std::string::npos);
  const std::string drill = excellon(fab, true, "essai");
  CHECK(drill.find("METRIC") != std::string::npos);
  CHECK(drill.find("T1C") != std::string::npos);
  CHECK(csvPlacement(m.c).find("\"R1\",") != std::string::npos);
  const auto dossier = std::filesystem::temp_directory_path() / "promethee-essai-fabrication";
  std::filesystem::remove_all(dossier);
  const auto fichiers = ecrireDossierFabrication(m.c, dossier.string(), "essai");
  CHECK(fichiers.size() >= 14);
  CHECK(std::filesystem::exists(dossier / "LISEZMOI.txt"));
  // Aller-retour JSON exact
  const std::string avant = versJson(m.c).dump();
  CHECK(versJson(conceptionDepuisJson(nlohmann::ordered_json::parse(avant))).dump() == avant);
  // Lien avec le boîtier : la carte du projet suit le circuit imprimé
  prom::Projet p = prom::projetVide();
  p.electronique.push_back(m.c);
  prom::appliquerCarteElectronique(p, 0);
  CHECK(p.carte.L == Approx(40));
  CHECK(p.carte.W == Approx(30));
  CHECK(p.composants.size() == 3);
  const prom::Projet relu = prom::normaliser(prom::versJson(p));
  REQUIRE(relu.electronique.size() == 1);
  CHECK(relu.electronique[0].circuit.empreintes.size() == 3);
}

TEST_CASE("Électronique : composant créé par l'utilisateur (liste de broches → symbole sur la grille)") {
  const auto broches = lireListeBroches("# brochage\n1 VCC\n2 GND\n3 SDA io\n4 SCL in gauche\n5 INT out\n6 NC\n7\tEN;entree;haut\n");
  REQUIRE(broches.size() == 7);
  CHECK(broches[0].type == TypeBroche::AlimEntree);
  CHECK(broches[1].type == TypeBroche::AlimEntree);
  CHECK(broches[2].type == TypeBroche::Bidirectionnelle);
  CHECK(broches[3].type == TypeBroche::Entree);
  CHECK(broches[3].cote == 0);
  CHECK(broches[4].type == TypeBroche::Sortie);
  CHECK(broches[5].type == TypeBroche::NonConnectee);
  CHECK(broches[6].nom == "EN");
  CHECK(broches[6].cote == 2);
  SpecSymbole s;
  s.nom = "Capteur I2C";
  s.prefixe = "U";
  s.empreinte = "SOIC-8";
  s.mpn = "XYZ-1";
  s.fabricant = "Exemple";
  s.broches = broches;
  const DefSymbole d = genererSymbole(s);
  CHECK(d.id == "perso:Capteur-I2C");
  CHECK(d.broches.size() == 7);
  CHECK(d.categorie == "Mes composants");
  // Toutes les extrémités sur la grille de 1,27 mm ; alimentation en haut, masse en bas, sortie à droite
  for (const auto& b : d.broches) {
    CHECK(b.extremite.x % GRILLE_SCHEMA == 0);
    CHECK(b.extremite.y % GRILLE_SCHEMA == 0);
    if (b.nom == "VCC") CHECK(b.direction == 1);
    if (b.nom == "GND") CHECK(b.direction == 3);
    if (b.nom == "INT") CHECK(b.direction == 2);
    if (b.nom == "SCL") CHECK(b.direction == 0);
  }
  // Pas deux broches au même endroit
  for (std::size_t i = 0; i < d.broches.size(); ++i)
    for (std::size_t j = i + 1; j < d.broches.size(); ++j) CHECK(d.broches[i].extremite != d.broches[j].extremite);
  // Relu depuis le symbole : mêmes broches, mêmes côtés
  const SpecSymbole relu = specDepuisSymbole(d);
  CHECK(relu.broches.size() == 7);
  CHECK(genererSymbole(relu).broches.size() == 7);
  // Posé dans une conception : le composant reprend la référence fabricant
  ConceptionElec c = nouvelleConception("Perso");
  c.symboles[d.id] = d;
  const std::string u = poserSymbole(c, 0, d.id, ptMm(100, 80));
  REQUIRE_FALSE(u.empty());
  REQUIRE(c.composants.size() == 1);
  CHECK(c.composants[0].mpn == "XYZ-1");
  CHECK(c.composants[0].empreinte == "SOIC-8");
  CHECK(c.composants[0].reference == "U?");
  // Aller-retour JSON de la définition
  CHECK(symboleVersJson(symboleDepuisJson(symboleVersJson(d))).dump() == symboleVersJson(d).dump());
}

TEST_CASE("Électronique : validation globale, porte de publication, révision verrouillée") {
  CHECK(revisionSuivante("A") == "B");
  CHECK(revisionSuivante("Z") == "AA");
  CHECK(revisionSuivante("AZ") == "BA");
  CHECK(revisionSuivante("3") == "4");
  CHECK(revisionSuivante("B9") == "B10");
  CHECK(revisionSuivante("") == "A");
  Montage m;
  // Non annoté et non routé : la validation bloque
  Validation v = valider(m.c);
  REQUIRE(v.etapes.size() == 7);
  CHECK_FALSE(v.publiable());
  CHECK(v.etapes[0].erreurs >= 3);  // ERC-02 rangées dans l'intégrité
  const auto dossier = std::filesystem::temp_directory_path() / "promethee-essai-publication";
  std::filesystem::remove_all(dossier);
  CHECK_THROWS(publier(m.c, dossier.string(), "Evan", "2026-10-09", "", "essai"));
  CHECK(m.c.publications.empty());
  // Carte complète : annotée, placée, routée, documentée, références fabricant
  annoter(m.c, false);
  mettreAJourCircuit(m.c);
  auto& ci = m.c.circuit;
  ci.contour.largeur = nm(40);
  ci.contour.hauteur = nm(30);
  for (auto& e : ci.empreintes) {
    const std::string ref = m.c.composant(e.composant)->reference;
    if (ref == "J1") e.position = ptMm(6, 15);
    if (ref == "R1") e.position = ptMm(20, 10);
    if (ref == "D1") e.position = ptMm(30, 20);
  }
  {
    const Connectivite k = connectivite(m.c);
    const Remplissages z = remplirZones(m.c, k);
    for (const auto& l : etatRoutage(m.c, k, z).chevelu) ci.pistes.push_back({nouvelUuid(), l.reseau, Couche::CuivreDessus, l.a, l.b, nm(0.3)});
  }
  for (auto& x : m.c.composants) {
    x.fabricant = "Exemple";
    x.mpn = "MPN-" + x.reference;
  }
  m.c.cartouche = {"Témoin d'alimentation", "ESME", "Evan", "A", "2026-10-09", ""};
  v = valider(m.c);
  for (const auto& e : v.etapes)
    for (const auto& d : e.details) INFO(e.nom + " : " + d.message);
  CHECK(v.publiable());
  CHECK(v.etapes[3].score == 100);  // tous les réseaux routés
  CHECK(v.etapes[4].score == 100);  // nomenclature complète
  CHECK(v.etapes[6].score == 100);  // cartouche rempli
  const std::string empreinte = empreinteConception(m.c);
  CHECK(empreinte.size() == 64);  // SHA-256 en hexadécimal
  std::vector<std::string> fichiers;
  const Publication p = publier(m.c, dossier.string(), "Evan", "2026-10-09", "", "essai", &fichiers);
  CHECK(p.id == "R001");
  CHECK(p.revision == "A");
  CHECK(p.empreinte == empreinte);
  CHECK(p.derogation.empty());
  CHECK(m.c.verrouillee);
  CHECK(std::filesystem::exists(dossier / p.dossier / "PUBLICATION.txt"));
  CHECK(std::find(fichiers.begin(), fichiers.end(), "PUBLICATION.txt") != fichiers.end());
  CHECK(empreinteConception(m.c) == empreinte);  // la publication ne change pas l'empreinte
  CHECK_THROWS(publier(m.c, dossier.string(), "Evan", "2026-10-09", "", "essai"));  // déjà verrouillée
  // Nouvelle révision, une erreur, publication forcée avec une raison : journal d'audit
  nouvelleRevision(m.c);
  CHECK_FALSE(m.c.verrouillee);
  CHECK(m.c.cartouche.revision == "B");
  // Une capacité du fabricant dépassée ne se dérogue jamais, même avec une raison
  const Nm isolementAvant = m.c.circuit.regles.isolement;
  m.c.circuit.regles.isolement = nm(0.1);  // sous le minimum du profil Standard
  v = valider(m.c);
  CHECK(v.etapes[5].erreurs == 1);
  CHECK(v.critiques() == 1);
  CHECK_THROWS(publier(m.c, dossier.string(), "Evan", "2026-10-10", "Le fabricant a confirmé 0,1 mm", "essai"));
  CHECK_FALSE(m.c.verrouillee);
  m.c.circuit.regles.isolement = isolementAvant;
  // Valeur manquante (BOM-01) : dérogeable, avec une raison écrite
  for (auto& x : m.c.composants)
    if (x.reference == "R1") x.valeur.clear();
  v = valider(m.c);
  CHECK(v.etapes[4].erreurs == 1);
  CHECK(v.critiques() == 0);
  CHECK_THROWS(publier(m.c, dossier.string(), "Evan", "2026-10-10", "  ", "essai"));
  const Publication p2 = publier(m.c, dossier.string(), "Evan", "2026-10-10", "Prototype : valeur de R1 choisie au montage", "essai");
  CHECK(p2.id == "R002");
  CHECK(p2.revision == "B");
  CHECK(p2.violations.size() == 1);
  CHECK(p2.derogation.find("Prototype") == 0);
  // Les publications et le verrou passent dans le fichier
  const std::string avant = versJson(m.c).dump();
  const ConceptionElec relue = conceptionDepuisJson(nlohmann::ordered_json::parse(avant));
  CHECK(relue.verrouillee);
  REQUIRE(relue.publications.size() == 2);
  CHECK(relue.publications[1].violations.size() == 1);
  CHECK(versJson(relue).dump() == avant);
}

TEST_CASE("Électronique : exemple livré (clignotant 555) — schéma propre, circuit routé, publiable") {
  const ConceptionElec c = exempleElectronique();
  const Connectivite k = connectivite(c);
  const auto erc = verifierERC(c, k);
  for (const auto& v : erc) UNSCOPED_INFO("ERC " + v.code + " : " + v.message);
  CHECK(erreurs(erc) == 0);
  CHECK(c.composants.size() == 13);
  // Réseaux attendus
  CHECK(k.indice("+5V") >= 0);
  CHECK(k.indice("GND") >= 0);
  CHECK(k.indice("DECH") >= 0);
  CHECK(k.nom(c.parReference("U1")->uuid, "7") == "DECH");
  CHECK(k.nom(c.parReference("U1")->uuid, "4") == "+5V");
  CHECK(k.nom(c.parReference("U1")->uuid, "2") == k.nom(c.parReference("U1")->uuid, "6"));
  CHECK(k.nom(c.parReference("C1")->uuid, "1") == k.nom(c.parReference("U1")->uuid, "2"));
  const Remplissages z = remplirZones(c, k);
  const EtatRoutage e = etatRoutage(c, k, z);
  for (const auto& l : e.chevelu) UNSCOPED_INFO("non routé : " + l.reseau + " (" + std::to_string(mm(l.a.x)) + ", " + std::to_string(mm(l.a.y)) + ") → (" + std::to_string(mm(l.b.x)) + ", " + std::to_string(mm(l.b.y)) + ")");
  const auto drc = verifierDRC(c, k, z);
  for (const auto& v : drc) UNSCOPED_INFO("DRC " + v.code + " : " + v.message);
  CHECK(e.chevelu.empty());
  CHECK(e.reseauxComplets == e.reseauxTotal);
  CHECK(erreurs(drc) == 0);
  const Validation v = valider(c);
  for (const auto& et : v.etapes)
    for (const auto& d : et.details) UNSCOPED_INFO(et.nom + " : " + d.message);
  CHECK(v.publiable());
}

// Dessin de débogage du circuit de l'exemple (lancé à la main : promethee_tests "[.dessin]")
TEST_CASE("Électronique : vidage de l'exemple pour dessin", "[.dessin]") {
  const ConceptionElec c = exempleElectronique();
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const Remplissages z = remplirZones(c, k);
  nlohmann::ordered_json j;
  for (const auto& p : pp) {
    nlohmann::ordered_json f = nlohmann::ordered_json::array();
    for (const Pt& q : p.forme) f.push_back({mm(q.x), mm(q.y)});
    j["pastilles"].push_back({{"ref", p.reference + "." + p.numero}, {"reseau", p.reseau}, {"haut", (p.couches & bit(Couche::CuivreDessus)) != 0}, {"bas", (p.couches & bit(Couche::CuivreDessous)) != 0}, {"forme", f}});
  }
  for (const auto& t : c.circuit.pistes) j["pistes"].push_back({{"reseau", t.reseau}, {"bas", t.couche == Couche::CuivreDessous}, {"a", {mm(t.a.x), mm(t.a.y)}}, {"b", {mm(t.b.x), mm(t.b.y)}}, {"l", mm(t.largeur)}});
  for (const auto& v : c.circuit.vias) j["vias"].push_back({{"reseau", v.reseau}, {"p", {mm(v.p.x), mm(v.p.y)}}, {"d", mm(v.diametre)}});
  for (const auto& [id, polys] : z)
    for (const auto& poly : polys) {
      nlohmann::ordered_json f = nlohmann::ordered_json::array();
      for (const Pt& q : poly) f.push_back({mm(q.x), mm(q.y)});
      j["zones"].push_back(f);
    }
  for (const auto& l : etatRoutage(c, k, z).chevelu) j["chevelu"].push_back({{mm(l.a.x), mm(l.a.y)}, {mm(l.b.x), mm(l.b.y)}});
  std::ofstream(std::filesystem::temp_directory_path() / "promethee-exemple-carte.json") << j.dump();
}

TEST_CASE("Électronique : intégrité de la bibliothèque intégrée (grille, numéros, empreintes)") {
  CHECK(bibliothequeSymboles().size() >= 50);
  CHECK(catalogueEmpreintes().size() >= 100);
  for (const auto& d : bibliothequeSymboles()) {
    INFO("symbole " + d.id);
    std::set<std::string> numeros;
    for (const auto& b : d.broches) {
      CHECK(b.extremite.x % GRILLE_SCHEMA == 0);
      CHECK(b.extremite.y % GRILLE_SCHEMA == 0);
      CHECK(numeros.insert(b.numero).second);
    }
    if (d.alimentation || d.drapeau) continue;
    if (d.empreinte.empty()) continue;
    const DefEmpreinte* e = empreinteEnCache(d.empreinte);
    REQUIRE(e != nullptr);
    std::set<std::string> pastilles;
    for (const auto& p : e->pastilles) pastilles.insert(p.numero);
    for (const auto& n : numeros) CHECK(pastilles.count(n) == 1);
  }
  // Toutes les empreintes du catalogue se génèrent, avec un encombrement et des pastilles
  for (const auto& i : catalogueEmpreintes()) {
    INFO("empreinte " + i.id);
    const DefEmpreinte* e = empreinteEnCache(i.id);
    REQUIRE(e != nullptr);
    CHECK(!e->pastilles.empty());
    CHECK(!e->encombrement.vide());
  }
}

TEST_CASE("EL-19 EL-24 tourner ou mirer un symbole câblé garde ses connexions", "[electronique][EL-19]") {
  for (const OperationSchema op : {OperationSchema::Tourner, OperationSchema::MiroirGaucheDroite, OperationSchema::MiroirHautBas}) {
    Montage m;
    annoter(m.c, false);
    const Connectivite avant = connectivite(m.c);
    const std::string r1 = m.c.parReference("R1")->uuid;
    const std::string reseau1 = avant.nom(r1, "1"), reseau2 = avant.nom(r1, "2");
    const auto erreursAvant = erreurs(verifierERC(m.c, avant));
    transformerSelectionSchema(m.c, 0, {m.r}, op);
    INFO("opération " << static_cast<int>(op));
    std::string fils;
    for (const auto& w : m.c.feuilles[0].fils) fils += std::to_string(mm(w.a.x)) + "," + std::to_string(mm(w.a.y)) + " > " + std::to_string(mm(w.b.x)) + "," + std::to_string(mm(w.b.y)) + "\n";
    std::string br;
    for (const char* n : {"1", "2"}) { const Pt q = m.broche(m.r, n); br += std::string(n) + ":" + std::to_string(mm(q.x)) + "," + std::to_string(mm(q.y)) + " "; }
    INFO(fils << br);
    const Connectivite apres = connectivite(m.c);
    // R1 a pu être retournée : ses deux broches restent sur les deux mêmes réseaux (dans un ordre ou l'autre)
    const std::set<std::string> attendus{reseau1, reseau2}, obtenus{apres.nom(r1, "1"), apres.nom(r1, "2")};
    CHECK(obtenus == attendus);
    CHECK(erreurs(verifierERC(m.c, apres)) == erreursAvant);
    // Tous les fils restent à l'équerre
    for (const auto& w : m.c.feuilles[0].fils) CHECK((w.a.x == w.b.x || w.a.y == w.b.y));
  }
  // Miroir haut-bas d'un symbole non tourné : les broches haut et bas s'échangent, la gauche et la droite non
  Montage m;
  const auto& s0 = m.c.feuilles[0].symboles;
  const auto it = std::find_if(s0.begin(), s0.end(), [&](const SymbolePlace& s) { return s.uuid == m.r; });
  REQUIRE(it != s0.end());
  const Pt p1 = m.broche(m.r, "1"), p2 = m.broche(m.r, "2"), centre = it->position;
  transformerSelectionSchema(m.c, 0, {m.r}, OperationSchema::MiroirHautBas);
  CHECK(m.broche(m.r, "1") == Pt{p1.x, 2 * centre.y - p1.y});
  CHECK(m.broche(m.r, "2") == Pt{p2.x, 2 * centre.y - p2.y});
}

TEST_CASE("EL-22 copier et coller une partie du schéma", "[electronique][EL-22]") {
  Montage m;
  annoter(m.c, false);
  const std::size_t composants = m.c.composants.size(), symboles = m.c.feuilles[0].symboles.size();
  const auto pp = copierSchema(m.c, 0, {m.r, m.d});
  REQUIRE(pp.symboles.size() == 2);
  REQUIRE(pp.composants.size() == 2);
  const auto crees = collerSchema(m.c, 0, pp, pp.reference + ptMm(50.8, 0));
  CHECK(crees.size() == 2);
  CHECK(m.c.composants.size() == composants + 2);
  CHECK(m.c.feuilles[0].symboles.size() == symboles + 2);
  // Les copies sont des composants neufs, à annoter
  int aAnnoter = 0;
  for (const auto& k : m.c.composants) aAnnoter += k.reference.back() == '?';
  CHECK(aAnnoter == 2);
  CHECK(annoter(m.c, false) == 2);
  CHECK(m.c.parReference("R2"));
  CHECK(m.c.parReference("D2"));
}

TEST_CASE("EL-12 via de couture : réseau pris à la zone, via isolée signalée", "[electronique][EL-12]") {
  ConceptionElec c = exempleElectronique();
  Connectivite k = connectivite(c);
  Remplissages r = remplirZones(c, k);
  // Un point de la zone de masse remplie
  Pt dans{};
  bool trouve = false;
  for (const auto& z : c.circuit.zones)
    if (const auto it = r.find(z.uuid); !trouve && it != r.end() && !it->second.empty() && !it->second.front().empty()) {
      const Boite b = boite(it->second);
      for (Nm y = b.y0; y < b.y1 && !trouve; y += 250'000)
        for (Nm x = b.x0; x < b.x1 && !trouve; x += 250'000)
          if (dedans(it->second, Pt{x, y}) && reseauDeZoneSous(c, k, r, Pt{x, y}) == z.reseau) { dans = {x, y}; trouve = true; }
    }
  REQUIRE(trouve);
  const std::string reseau = reseauDeZoneSous(c, k, r, dans);
  CHECK_FALSE(reseau.empty());
  // Via sans réseau : signalée
  c.circuit.vias.push_back({nouvelUuid(), "", dans});
  k = connectivite(c);
  r = remplirZones(c, k);
  auto v = verifierDRC(c, k, r);
  CHECK(compter(v, "DRC-25") >= 1);
  // Avec le réseau de la zone : reliée par la zone, plus d'alerte pour elle
  c.circuit.vias.back().reseau = reseau;
  k = connectivite(c);
  r = remplirZones(c, k);
  v = verifierDRC(c, k, r);
  CHECK(compter(v, "DRC-25") == 0);
}

TEST_CASE("EL-44 découpes internes de la carte : zones, DRC, fraisage, fichier", "[electronique][EL-44]") {
  ConceptionElec c = exempleElectronique();
  Connectivite k = connectivite(c);
  Remplissages r = remplirZones(c, k);
  // Un point de cuivre de zone loin de tout : on y perce une fenêtre de 3 × 3 mm
  Pt centre{};
  bool trouve = false;
  for (const auto& z : c.circuit.zones)
    if (const auto it = r.find(z.uuid); !trouve && it != r.end() && !it->second.empty()) {
      const Boite b = boite(it->second);
      for (Nm y = b.y0; y < b.y1 && !trouve; y += 250'000)
        for (Nm x = b.x0; x < b.x1 && !trouve; x += 250'000) {
          bool plein = true;
          for (Nm dy = -2'500'000; dy <= 2'500'000 && plein; dy += 500'000)
            for (Nm dx = -2'500'000; dx <= 2'500'000 && plein; dx += 500'000) plein = dedans(it->second, Pt{x + dx, y + dy});
          if (plein) { centre = {x, y}; trouve = true; }
        }
    }
  REQUIRE(trouve);
  double avant = 0;
  for (const auto& [id, ps] : r) avant += aireMm2(ps);
  const Polygone fenetre = rectangle(centre, 3 * NM_PAR_MM, 3 * NM_PAR_MM, 0);
  c.circuit.contour.decoupes.push_back(fenetre);
  CHECK_FALSE(c.circuit.contour.contient(centre));
  CHECK(c.circuit.contour.contient(Pt{centre.x + 1'500'000, centre.y}));  // bord de la découpe : matière
  CHECK(c.circuit.contour.aretes().size() == c.circuit.contour.polygone().size() + 4);
  k = connectivite(c);
  r = remplirZones(c, k);
  double apres = 0;
  for (const auto& [id, ps] : r) {
    apres += aireMm2(ps);
    CHECK_FALSE(dedans(ps, centre));
  }
  CHECK(avant - apres > 9.0);  // la fenêtre et sa marge au bord
  // Rien de nouveau tant que le cuivre reste dans la matière
  auto v = verifierDRC(c, k, r);
  CHECK(compter(v, "DRC-06") == 0);
  // Une via dans la fenêtre : hors de la matière ; une piste qui la traverse est coupée par la fraise : elle aussi
  // en DRC-06, jamais dérogeable, et plus en DRC-07 (R4, audit du 15 octobre)
  c.circuit.vias.push_back({nouvelUuid(), "", centre});
  c.circuit.pistes.push_back({nouvelUuid(), "", Couche::CuivreDessus, Pt{centre.x - 2'400'000, centre.y + 1'000'000}, Pt{centre.x + 2'400'000, centre.y + 1'000'000}, 250'000});
  k = connectivite(c);
  r = remplirZones(c, k);
  v = verifierDRC(c, k, r);
  CHECK(compter(v, "DRC-06") >= 2);
  c.circuit.vias.pop_back();
  c.circuit.pistes.pop_back();
  // Fraisage : les quatre côtés de la fenêtre s'ajoutent au contour dans la couche Edge.Cuts
  k = connectivite(c);
  r = remplirZones(c, k);
  const ModeleFabrication m = modeleFabrication(c, k, r);
  for (const auto& cf : m.couches)
    if (cf.couche == Couche::Contour) CHECK(cf.primitives.size() == c.circuit.contour.aretes().size());
  // Le fichier garde la découpe
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  REQUIRE(relue.circuit.contour.decoupes.size() == 1);
  CHECK(relue.circuit.contour.decoupes[0] == fenetre);
}

TEST_CASE("EL-44 contour et découpes lus dans un DXF", "[electronique][EL-44]") {
  // Carte de 60 × 40 mm aux coins arrondis (polyligne à renflements), en pouces ; une fenêtre de 10 × 6 mm faite
  // de quatre lignes dans le désordre ; un trou rond ; un tracé ouvert et un texte, signalés
  const double in = 1 / 25.4;
  std::ostringstream d;
  d << "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n1\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
  const double r = 3, L = 60, H = 40, b = std::tan(std::numbers::pi / 8);  // quart de cercle
  d << "0\nLWPOLYLINE\n90\n8\n70\n1\n";
  const double pts[8][3] = {{r, 0, 0}, {L - r, 0, b}, {L, r, 0}, {L, H - r, b}, {L - r, H, 0}, {r, H, b}, {0, H - r, 0}, {0, r, b}};
  for (const auto& p : pts) d << "10\n" << p[0] * in << "\n20\n" << p[1] * in << "\n42\n" << p[2] << "\n";
  auto ligne = [&](double x0, double y0, double x1, double y1) { d << "0\nLINE\n10\n" << x0 * in << "\n20\n" << y0 * in << "\n11\n" << x1 * in << "\n21\n" << y1 * in << "\n"; };
  ligne(20, 10, 30, 10);
  ligne(20, 16, 20, 10);
  ligne(30, 16, 20, 16);
  ligne(30, 10, 30, 16);
  d << "0\nCIRCLE\n10\n" << 45 * in << "\n20\n" << 20 * in << "\n40\n" << 2 * in << "\n";
  ligne(50, 30, 55, 35);
  d << "0\nTEXT\n10\n0\n20\n0\n1\nCarte\n0\nENDSEC\n0\nEOF\n";
  const ContourDxf c = lireContourDxf(d.str());
  REQUIRE(c.exterieur.size() > 8);
  CHECK(aireMm2(c.exterieur) == Approx(L * H - (4 - std::numbers::pi) * r * r).epsilon(0.002));
  const Boite bb = boite(c.exterieur);
  CHECK(mm(bb.largeur()) == Approx(L).margin(0.01));
  CHECK(mm(bb.y0) == Approx(-H).margin(0.01));  // Y retourné
  REQUIRE(c.decoupes.size() == 2);
  double fenetre = 0, trou = 0;
  for (const auto& x : c.decoupes) (aireMm2(x) > 30 ? fenetre : trou) = aireMm2(x);
  CHECK(fenetre == Approx(60).margin(0.01));
  CHECK(trou == Approx(std::numbers::pi * 4).epsilon(0.01));
  CHECK(c.alertes.size() == 2);  // tracé ouvert, texte
}

TEST_CASE("EL-39 classes de réseaux : motifs, largeur et vias contrôlés, noms inconnus signalés", "[electronique][EL-39]") {
  CHECK(correspond("+*V", "+5V"));
  CHECK(correspond("+*V", "+12V"));
  CHECK_FALSE(correspond("+*V", "+5V_USB"));
  CHECK(correspond("USB_D?", "USB_DP"));
  CHECK_FALSE(correspond("USB_D?", "USB_DPP"));
  CHECK(correspond("*", ""));
  ConceptionElec c = exempleElectronique();
  Connectivite k = connectivite(c);
  std::string alim;
  for (const auto& r : k.reseaux)
    if (!r.nom.empty() && r.nom[0] == '+') alim = r.nom;
  REQUIRE_FALSE(alim.empty());
  ClasseReseau cl;
  cl.nom = "Puissance";
  cl.largeur = 2 * NM_PAR_MM;
  cl.viaDiametre = 1'500'000;
  cl.viaPercage = 800'000;
  c.circuit.regles.classes.push_back(cl);
  c.circuit.regles.classeReseau["+*"] = "Puissance";  // motif
  c.circuit.regles.classeReseau["VCC_FAUTE"] = "Puissance";
  REQUIRE(classeDe(c.circuit.regles, alim) != nullptr);
  CHECK(largeurPiste(c.circuit.regles, alim) == 2 * NM_PAR_MM);
  CHECK(classeDe(c.circuit.regles, "GND") == nullptr);
  // Le nom exact passe avant le motif
  ClasseReseau fine = cl;
  fine.nom = "Fine";
  fine.largeur = 300'000;
  c.circuit.regles.classes.push_back(fine);
  c.circuit.regles.classeReseau[alim] = "Fine";
  CHECK(classeDe(c.circuit.regles, alim)->nom == "Fine");
  c.circuit.regles.classeReseau.erase(alim);
  // Pistes et vias de l'alimentation plus fines que la classe : signalées sans courant renseigné
  c.circuit.vias.push_back({nouvelUuid(), alim, c.circuit.empreintes.front().position, 800'000, 400'000});
  k = connectivite(c);
  const auto r = remplirZones(c, k);
  const auto v = verifierDRC(c, k, r);
  bool pisteAlim = false;
  for (const auto& p : c.circuit.pistes) pisteAlim |= p.reseau == alim && p.largeur < 2 * NM_PAR_MM;
  CHECK(compter(v, "DRC-33") >= (pisteAlim ? 1 : 0));
  CHECK(compter(v, "DRC-34") >= 1);
  CHECK(compter(v, "DRC-35") == 1);  // VCC_FAUTE
}

TEST_CASE("Carte simple face : cuivre dessous seulement, sans via, CMS du dessus signalés", "[electronique][simple-face]") {
  ConceptionElec c = exempleElectronique();
  c.circuit.empilage.couchesCuivre = 1;
  CHECK(couchesCuivre(1) == std::vector<Couche>{Couche::CuivreDessous});
  // Repartir d'une carte non routée, toutes les empreintes traversantes ou retournées dessous
  c.circuit.pistes.clear();
  c.circuit.vias.clear();
  for (auto& z : c.circuit.zones) z.couche = Couche::CuivreDessous;
  Connectivite k = connectivite(c);
  auto pp = pastilles(c, k);
  bool cmsDessus = false;
  for (const auto& p : pp) cmsDessus |= p.pastille.type == TypePastille::CMS && !p.dessous;
  auto v = verifierDRC(c, k, remplirZones(c, k));
  CHECK((compter(v, "DRC-38") > 0) == cmsDessus);
  for (auto& e : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (d && std::any_of(d->pastilles.begin(), d->pastilles.end(), [](const Pastille& p) { return p.type == TypePastille::CMS; })) e.dessous = true;
  }
  routerSimple(c);
  for (const auto& p : c.circuit.pistes) CHECK(p.couche == Couche::CuivreDessous);
  CHECK(c.circuit.vias.empty());
  k = connectivite(c);
  const auto r = remplirZones(c, k);
  v = verifierDRC(c, k, r);
  CHECK(compter(v, "DRC-38") == 0);
  CHECK(compter(v, "DRC-17") == 0);
  // Une via posée à la main : pont de fil signalé
  c.circuit.vias.push_back({nouvelUuid(), "", c.circuit.empreintes.front().position + ptMm(0, 3)});
  v = verifierDRC(c, connectivite(c), r);
  CHECK(compter(v, "DRC-37") == 1);
  // Fabrication : une seule couche de cuivre, nommée L1 dessous ; le fichier garde l'empilage
  const ModeleFabrication m = modeleFabrication(c, k, r);
  int cuivres = 0;
  for (const auto& cf : m.couches) cuivres += estCuivre(cf.couche);
  CHECK(cuivres == 1);
  CHECK(gerber(m, Couche::CuivreDessous, "essai").find("Copper,L1,Bot") != std::string::npos);
  CHECK(conceptionDepuisJson(versJson(c)).circuit.empilage.couchesCuivre == 1);
}

TEST_CASE("Parité schéma-circuit dans la DRC : empreinte en trop, changée, hors carte, cuivre d'un réseau disparu", "[electronique][parite]") {
  ConceptionElec c = exempleElectronique();
  {
    const Connectivite k = connectivite(c);
    const auto v = verifierDRC(c, k, remplirZones(c, k));
    for (const char* code : {"DRC-39", "DRC-40", "DRC-41", "DRC-42"}) CHECK(compter(v, code) == 0);
  }
  // Un composant dont l'empreinte change au schéma ; un autre retiré du schéma ; un troisième déclaré hors carte
  ComposantElec& a = c.composants[0];
  const std::string ancienne = a.empreinte;
  a.empreinte = ancienne == "R_0805" ? "R_1206" : "R_0805";
  const std::string retire = c.composants[1].uuid;
  for (auto& f : c.feuilles) f.symboles.erase(std::remove_if(f.symboles.begin(), f.symboles.end(), [&](const SymbolePlace& s) { return s.composant == retire; }), f.symboles.end());
  c.composants[2].horsCarte = true;
  c.circuit.pistes.push_back({nouvelUuid(), "FANTOME", Couche::CuivreDessus, ptMm(2, 2), ptMm(3, 2), 250'000});
  const Connectivite k = connectivite(c);
  const auto v = verifierDRC(c, k, remplirZones(c, k));
  CHECK(compter(v, "DRC-40") == 1);
  CHECK(compter(v, "DRC-39") == 1);
  CHECK(compter(v, "DRC-41") == 1);
  CHECK(compter(v, "DRC-42") == 1);
  // La mise à jour depuis le schéma rétablit la parité
  mettreAJourCircuit(c);
  c.circuit.pistes.pop_back();
  const Connectivite k2 = connectivite(c);
  const auto v2 = verifierDRC(c, k2, remplirZones(c, k2));
  for (const char* code : {"DRC-39", "DRC-40", "DRC-41", "DRC-42"}) CHECK(compter(v2, code) == 0);
}

TEST_CASE("Typon pour la gravure maison : couche à l'échelle 1, miroir, perçages réduits, règle", "[electronique][typon]") {
  const ConceptionElec c = exempleElectronique();
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  OptionsTypon o;
  o.couche = Couche::CuivreDessous;
  const ModeleTypon m = modeleTypon(c, k, r, o);
  CHECK(m.miroir);  // la face dessous se lit côté cuivre, donc en miroir
  const Boite carte = boite(c.circuit.contour.polygone());
  CHECK(m.largeur == Approx(std::max(90.0, mm(carte.largeur()) + 2 * o.marge)).margin(1e-6));
  CHECK_FALSE(m.cuivre.empty());
  REQUIRE_FALSE(m.trous.empty());
  for (const auto& [p, d] : m.trous) CHECK(d <= nm(0.6));
  // Le miroir : une pastille à gauche sur la carte se retrouve à droite sur le typon
  const auto pp = pastilles(c, k);
  const PastillePlacee* gauche = nullptr;
  for (const auto& p : pp)
    if (p.pastille.percage > 0 && (!gauche || p.pastille.position.x < gauche->pastille.position.x)) gauche = &p;
  REQUIRE(gauche);
  // x du typon = largeur de la carte avec ses marges − x depuis le bord gauche
  const double largeurCarte = mm(carte.largeur()) + 2 * o.marge;
  const double attendu = largeurCarte - (mm(gauche->pastille.position.x - carte.x0) + o.marge);
  bool trouve = false;
  for (const auto& [p, d] : m.trous) trouve |= std::abs(mm(p.x) - attendu) < 1e-3;
  CHECK(trouve);
  // Dessus sans miroir, avec un miroir de transfert : miroir
  o.couche = Couche::CuivreDessus;
  CHECK_FALSE(modeleTypon(c, k, r, o).miroir);
  o.miroirTransfert = true;
  CHECK(modeleTypon(c, k, r, o).miroir);
  const std::string svg = svgTypon(m, o, "Essai & co");
  CHECK(svg.find("width=\"") != std::string::npos);
  CHECK(svg.find("mm\"") != std::string::npos);
  CHECK(svg.find("Essai &amp; co") != std::string::npos);
  CHECK(svg.find("<circle") != std::string::npos);
}

TEST_CASE("Échanges : IPC-D-356, fichier de travail Gerber, liste de réseaux KiCad", "[electronique][echanges-ecad]") {
  const ConceptionElec c = exempleElectronique();
  const Connectivite k = connectivite(c);
  const std::string ipc = ipcD356(c, k);
  int lignes317 = 0, lignes327 = 0;
  std::istringstream in(ipc);
  std::string l;
  while (std::getline(in, l)) {
    if (l.rfind("317", 0) == 0) ++lignes317;
    if (l.rfind("327", 0) == 0) ++lignes327;
    if (l.rfind("3", 0) == 0) CHECK(l.find('X') != std::string::npos);
  }
  int traversantes = 0, cms = 0;
  for (const auto& p : pastilles(c, k)) (p.pastille.type == TypePastille::CMS ? cms : traversantes) += p.pastille.type != TypePastille::NonMetallisee;
  CHECK(lignes317 == traversantes + static_cast<int>(c.circuit.vias.size()));
  CHECK(lignes327 == cms);
  CHECK(ipc.find("999") != std::string::npos);
  const auto m = modeleFabrication(c, k, remplirZones(c, k));
  const std::string job = gerberJob(c, m, {{"x-F_Cu.gbr", Couche::CuivreDessus}, {"x-B_Cu.gbr", Couche::CuivreDessous}}, "essai");
  const auto j = nlohmann::json::parse(job);
  CHECK(j["GeneralSpecs"]["LayerNumber"] == 2);
  CHECK(j["FilesAttributes"].size() == 2);
  CHECK(j["FilesAttributes"][1]["FileFunction"] == "Copper,L2,Bot");
  const std::string net = netlistKicad(c, k);
  CHECK(net.rfind("(export (version \"E\")", 0) == 0);
  for (const auto& x : c.composants) CHECK(net.find("(ref \"" + x.reference + "\")") != std::string::npos);
  CHECK(net.find("(name \"GND\")") != std::string::npos);
}

TEST_CASE("E10 symboles à plusieurs unités : pose une à une, repère avec lettre, broches communes, ERC, fichier", "[electronique][unites]") {
  CHECK(lettreUnite(1) == "A");
  CHECK(lettreUnite(5) == "E");
  CHECK(lettreUnite(27) == "AA");
  CHECK(lettreUnite(0).empty());
  ConceptionElec c = nouvelleConception("Portes");
  const std::string a = poserSymbole(c, 0, "74HC00_unites", ptMm(50.8, 50.8));
  annoter(c, true);
  const DefSymbole* d = c.symbole("74HC00_unites");
  REQUIRE(d);
  CHECK(d->unites == 5);
  const std::string comp = c.feuilles[0].symboles.front().composant;
  CHECK(unitesPosees(c, comp) == std::set<int>{1});
  CHECK(compter(verifierERC(c, connectivite(c)), "ERC-40") == 4);
  // Chaque unité n'a que ses broches : trois pour une porte, deux pour l'alimentation
  CHECK(brochesPlacees(c.feuilles[0].symboles.front(), *d).size() == 3);
  std::vector<std::string> poses{a};
  for (int i = 0; i < 4; ++i) poses.push_back(poserUnite(c, 0, a));
  CHECK(poserUnite(c, 0, a).empty());  // toutes posées
  CHECK(unitesPosees(c, comp) == std::set<int>{1, 2, 3, 4, 5});
  REQUIRE(c.feuilles[0].symboles.size() == 5);
  std::set<Pt> positions;
  for (const auto& s : c.feuilles[0].symboles) positions.insert(s.position);
  CHECK(positions.size() == 5);  // pas l'une sur l'autre
  CHECK(brochesPlacees(c.feuilles[0].symboles.back(), *d).size() == 2);
  CHECK(compter(verifierERC(c, connectivite(c)), "ERC-40") == 0);
  // La sortie de la porte A pilote une entrée de la porte B : un fil entre unités différentes du même composant
  auto lieu = [&](std::size_t i, const std::string& numero) {
    for (const auto& b : brochesPlacees(c.feuilles[0].symboles[i], *d))
      if (b.broche->numero == numero) return b.p;
    FAIL("broche " << numero << " absente de l'unité " << i);
    return Pt{};
  };
  c.feuilles[0].fils.push_back({nouvelUuid(), lieu(0, "3"), lieu(1, "4")});
  {
    const Connectivite k = connectivite(c);
    CHECK(k.reseau(comp, "3") >= 0);
    CHECK(k.reseau(comp, "3") == k.reseau(comp, "4"));
    CHECK(k.reseau(comp, "3") != k.reseau(comp, "5"));
  }
  // Même unité posée deux fois : erreur, et l'unité laissée manque
  c.feuilles[0].symboles[1].unite = 1;
  {
    const auto v = verifierERC(c, connectivite(c));
    CHECK(compter(v, "ERC-41") == 1);
    CHECK(compter(v, "ERC-40") == 1);
  }
  c.feuilles[0].symboles[1].unite = 2;
  // Le fichier garde unités du symbole, des broches et des symboles posés
  const ConceptionElec r = conceptionDepuisJson(versJson(c));
  REQUIRE(r.symbole("74HC00_unites"));
  CHECK(r.symbole("74HC00_unites")->unites == 5);
  std::vector<int> u;
  for (const auto& s : r.feuilles[0].symboles) u.push_back(s.unite);
  std::sort(u.begin(), u.end());  // le fichier range les symboles posés par identifiant (ordre stable, 1.13)
  CHECK(u == std::vector<int>{1, 2, 3, 4, 5});
  int brochesUniteE = 0;
  for (const auto& b : r.symbole("74HC00_unites")->broches) brochesUniteE += b.unite == 5;
  CHECK(brochesUniteE == 2);
}

TEST_CASE("E10 composant créé à plusieurs unités : liste « u1 », broche commune répétée et réunie", "[electronique][unites]") {
  const auto liste = lireListeBroches("1 A in u1\n2 Y out unite=A\n3 A in u2\n4 Y out u:B\n5 VCC alim commune");
  REQUIRE(liste.size() == 5);
  CHECK(liste[0].unite == 1);
  CHECK(liste[1].unite == 1);
  CHECK(liste[3].unite == 2);
  CHECK(liste[4].unite == 0);
  SpecSymbole sp;
  sp.nom = "Double tampon";
  sp.broches = liste;
  const DefSymbole d = genererSymbole(sp);
  CHECK(d.unites == 2);
  int cinq = 0;
  for (const auto& b : d.broches) cinq += b.numero == "5";
  CHECK(cinq == 2);  // la broche commune figure sur chaque unité
  // Retour à la liste : la broche commune redevient une seule ligne, d'unité 0
  const SpecSymbole retour = specDepuisSymbole(d);
  CHECK(retour.broches.size() == 5);
  for (const auto& b : retour.broches) CHECK(b.unite == (b.numero == "5" ? 0 : b.numero <= "2" ? 1 : 2));
  // Dans un schéma : la broche 5 reliée sur l'unité B seulement n'est pas « non connectée » sur l'unité A
  ConceptionElec c = nouvelleConception("Tampon");
  c.symboles[d.id] = d;
  const std::string s1 = poserSymbole(c, 0, d.id, ptMm(50.8, 50.8));
  const std::string s2 = poserUnite(c, 0, s1);
  REQUIRE_FALSE(s2.empty());
  annoter(c, true);
  const std::string comp = c.feuilles[0].symboles.front().composant;
  Pt p5;
  for (const auto& b : brochesPlacees(c.feuilles[0].symboles[1], d))
    if (b.broche->numero == "5") p5 = b.p;
  c.feuilles[0].etiquettes.push_back({nouvelUuid(), "VCC_LOGIQUE", p5, 0, TypeEtiquette::Globale});
  const Connectivite k = connectivite(c);
  CHECK(k.nom(comp, "5") == "VCC_LOGIQUE");
  for (const auto& v : verifierERC(c, k)) CHECK_FALSE((v.code == "ERC-10" && v.message.find("broche 5") != std::string::npos));
}
