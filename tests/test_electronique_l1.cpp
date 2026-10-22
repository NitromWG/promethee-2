// Prométhée : essais du lot L1 de l'audit du 15 octobre 2026 (« Fondations »), côté noyau : format propre à
// l'électronique, clés inconnues gardées, ordre stable (1.13, ECH-10, ECH-11, A13). Licence GPL-3.0-only.
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/migrations.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using Catch::Matchers::ContainsSubstring;
using J = nlohmann::ordered_json;

TEST_CASE("1.13 : relire puis réécrire une conception donne les mêmes octets", "[L1][ECH-10]") {
  const ConceptionElec c = exempleElectronique();
  const std::string une = versJson(c).dump(2);
  const ConceptionElec relue = conceptionDepuisJson(J::parse(une));
  CHECK(versJson(relue).dump(2) == une);
  // Format propre à la conception
  CHECK(J::parse(une)["format"] == FORMAT_ELECTRONIQUE);
}

TEST_CASE("1.13 : l'ordre écrit ne dépend pas de l'ordre des modifications", "[L1][ECH-10]") {
  ConceptionElec c = exempleElectronique();
  ConceptionElec d = c;
  std::reverse(d.composants.begin(), d.composants.end());
  std::reverse(d.circuit.pistes.begin(), d.circuit.pistes.end());
  std::reverse(d.circuit.vias.begin(), d.circuit.vias.end());
  std::reverse(d.circuit.empreintes.begin(), d.circuit.empreintes.end());
  std::reverse(d.feuilles[0].symboles.begin(), d.feuilles[0].symboles.end());
  std::reverse(d.feuilles[0].fils.begin(), d.feuilles[0].fils.end());
  CHECK(versJson(d).dump() == versJson(c).dump());
}

TEST_CASE("1.13 : les clés inconnues sont gardées objet par objet et réécrites telles quelles", "[L1][ECH-11][A13]") {
  J j = versJson(exempleElectronique());
  j["avenir"] = {{"simulation", true}};
  j["composants"][0]["article"] = "RES-0603-10K";
  j["feuilles"][0]["calques"] = J::array({"notes"});
  j["feuilles"][0]["symboles"][0]["textes"] = J::array({{{"nom", "valeur"}, {"visible", false}}});
  j["feuilles"][0]["fils"][0]["bus"] = false;
  j["circuit"]["regles"]["zonesDeRegles"] = J::array();
  j["circuit"]["regles"]["classes"] = J::array({{{"nom", "Puissance"}, {"isolement", 300000}, {"largeur", 800000}, {"viaDiametre", 900000}, {"viaPercage", 400000}, {"couleur", "#FF0000"}}});
  j["circuit"]["pistes"][0]["verrou"] = "protege";
  j["circuit"]["empreintes"][0]["textes"] = J::array();
  j["circuit"]["empilage"]["dielectriques"] = J::array({0.2, 1.2});
  j["circuit"]["zones"][0]["hachures"] = true;
  j["bibliothequeEmpreintes"][0]["pastilles"][0]["margeVernis"] = 50000;
  j["symboles"][0]["famille"] = "resistance";
  const std::string attendu = j.dump(2);
  const ConceptionElec c = conceptionDepuisJson(j);
  const J reecrit = versJson(c);
  CHECK(reecrit["avenir"]["simulation"] == true);
  CHECK(reecrit["composants"][0]["article"] == "RES-0603-10K");
  CHECK(reecrit["feuilles"][0]["calques"][0] == "notes");
  CHECK(reecrit["feuilles"][0]["symboles"][0]["textes"][0]["nom"] == "valeur");
  CHECK(reecrit["feuilles"][0]["fils"][0]["bus"] == false);
  CHECK(reecrit["circuit"]["regles"]["zonesDeRegles"].is_array());
  CHECK(reecrit["circuit"]["regles"]["classes"][0]["couleur"] == "#FF0000");
  CHECK(reecrit["circuit"]["pistes"][0]["verrou"] == "protege");
  CHECK(reecrit["circuit"]["empreintes"][0]["textes"].is_array());
  CHECK(reecrit["circuit"]["empilage"]["dielectriques"][1] == 1.2);
  CHECK(reecrit["circuit"]["zones"][0]["hachures"] == true);
  CHECK(reecrit["bibliothequeEmpreintes"][0]["pastilles"][0]["margeVernis"] == 50000);
  CHECK(reecrit["symboles"][0]["famille"] == "resistance");
  // Aller-retour complet : mêmes octets
  CHECK(reecrit.dump(2) == attendu);
  // Une clé connue n'est jamais écrasée par une clé gardée
  ConceptionElec modifiee = c;
  modifiee.composants.front().valeur = "4k7";
  CHECK(versJson(modifiee)["composants"][0]["valeur"] == "4k7");
}

TEST_CASE("1.13 : une conception d'un format plus récent s'ouvre avec une alerte", "[L1][ECH-10]") {
  J j = versJson(exempleElectronique());
  j["format"] = FORMAT_ELECTRONIQUE + 1;
  std::vector<std::string> alertes;
  const ConceptionElec c = conceptionDepuisJson(j, &alertes);
  CHECK(c.composants.size() == exempleElectronique().composants.size());
  REQUIRE_FALSE(alertes.empty());
  CHECK_THAT(alertes.front(), ContainsSubstring("plus récente"));
  // Un fichier de la 0.29 (« formatElectronique ») se relit comme le format 2
  J ancien = versJson(exempleElectronique());
  ancien.erase("format");
  ancien["formatElectronique"] = 2;
  ancien["circuit"]["textes"][0]["rot"] = 90;
  CHECK(conceptionDepuisJson(ancien).circuit.textes.front().rotation == 90);
}

// ---------------------------------------------------------------------------------------------------------
// 1.3, 1.4 : commandes annulables et changements décrits objet par objet
#include "promethee/commandes.hpp"

namespace {
std::string octets(const ConceptionElec& c) { return versJson(c).dump(); }
}  // namespace

TEST_CASE("1.3 : appliquer puis défaire chaque commande rend la conception identique à l'octet", "[L1][SAI-24]") {
  ConceptionElec c = exempleElectronique();
  const std::string avant = octets(c);
  const ComposantElec* r1 = c.parReference("R1");
  REQUIRE(r1);
  const std::string ancienne = r1->valeur;
  CommandeProprieteComposant valeur(r1->uuid, "valeur", ancienne, "4k7", "R1", 7);
  valeur.appliquer(c);
  CHECK(c.parReference("R1")->valeur == "4k7");
  CHECK(valeur.nom() == "Valeur de R1 : " + ancienne + " → 4k7");
  valeur.defaire(c);
  CHECK(octets(c) == avant);
  // Instantané d'une modification quelconque (déplacement, suppression, ajout)
  ConceptionElec apres = c;
  apres.circuit.empreintes.front().position.x += 2'540'000;
  apres.circuit.pistes.pop_back();
  apres.feuilles[0].textes.push_back({nouvelUuid(), "NOTE", ptMm(10, 10), 1'800'000, 0});
  CommandeInstantane inst("Essai", c, apres);
  inst.appliquer(c);
  CHECK(octets(c) == octets(apres));
  inst.defaire(c);
  CHECK(octets(c) == avant);
}

TEST_CASE("1.3 : les frappes d'une même saisie font un seul pas", "[L1][P2][SAI-24]") {
  ConceptionElec c = exempleElectronique();
  const std::string id = c.parReference("R2")->uuid;
  CommandeProprieteComposant premiere(id, "valeur", "1k", "4", "R2", 42);
  premiere.appliquer(c);
  for (const char* t : {"4k", "4k7"}) {
    CommandeProprieteComposant suite(id, "valeur", c.composant(id)->valeur, t, "R2", 42);
    suite.appliquer(c);
    CHECK(premiere.fusionner(suite));
  }
  CHECK(premiere.nom() == "Valeur de R2 : 1k → 4k7");
  // Une autre saisie, ou un autre champ, ne fusionne pas
  CommandeProprieteComposant autre(id, "valeur", "4k7", "5k", "R2", 43);
  CHECK_FALSE(premiere.fusionner(autre));
  CommandeProprieteComposant champ(id, "fabricant", "", "Yageo", "R2", 42);
  CHECK_FALSE(premiere.fusionner(champ));
  premiere.defaire(c);
  CHECK(c.composant(id)->valeur == "1k");
}

TEST_CASE("1.4 : une valeur changée ne demande ni connectivité ni zones", "[L1][A12]") {
  const ConceptionElec a = exempleElectronique();
  ConceptionElec b = a;
  for (auto& k : b.composants)
    if (k.reference == "R1") k.valeur = "22k";
  ChangementConception ch = differencier(a, b);
  CHECK(ch.proprietes == std::set<std::string>{"valeur"});
  CHECK_FALSE(ch.demandeConnectivite());
  CHECK_FALSE(ch.demandeCuivre());
  CHECK(ch.objets.size() == 1);
  CHECK(ch.touche(a.parReference("R1")->uuid));
  // Un repère touche les réseaux (noms automatiques), un fil aussi
  ConceptionElec c = a;
  for (auto& k : c.composants)
    if (k.reference == "R1") k.reference = "R9";
  CHECK(differencier(a, c).demandeConnectivite());
  ConceptionElec d = a;
  d.feuilles[0].fils.front().b.x += 2'540'000;
  ch = differencier(a, d);
  CHECK(ch.demandeConnectivite());
  CHECK(ch.objets.at(d.feuilles[0].fils.front().uuid) & ChangementConception::Geometrie);
  // Une piste déplacée touche le cuivre, pas la connectivité du schéma
  ConceptionElec e = a;
  e.circuit.pistes.front().a.x += 100'000;
  ch = differencier(a, e);
  CHECK_FALSE(ch.demandeConnectivite());
  CHECK(ch.demandeCuivre());
  CHECK_FALSE(ch.zones);
  // Une zone ajoutée, les règles, le cartouche
  ConceptionElec f = a;
  f.circuit.zones.push_back(f.circuit.zones.front());
  f.circuit.zones.back().uuid = nouvelUuid();
  CHECK(differencier(a, f).zones);
  ConceptionElec g = a;
  g.circuit.regles.isolement += 50'000;
  CHECK(differencier(a, g).regles);
  ConceptionElec h = a;
  h.cartouche.titre = "Nouveau";
  ch = differencier(a, h);
  CHECK(ch.proprietes == std::set<std::string>{"cartouche"});
  CHECK_FALSE(ch.demandeCuivre());
  // Rien n'a changé : changement vide
  CHECK(differencier(a, a).vide());
}

TEST_CASE("1.4 : nom lisible d'une modification", "[L1][SAI-24]") {
  const ConceptionElec a = exempleElectronique();
  ConceptionElec b = a;
  b.circuit.empreintes.front().position.y += 1'000'000;
  const std::string ref = a.composant(a.circuit.empreintes.front().composant)->reference;
  CHECK(nommerChangement(b, differencier(a, b)) == "Déplacement : " + ref);
}

TEST_CASE("1.5 : jonctions et marques de non-connexion ont un identifiant stable", "[L1][S26][S23]") {
  ConceptionElec c = exempleElectronique();
  Feuille& f = c.feuilles[0];
  f.jonctions.push_back({ptMm(30, 30), {}});
  f.nonConnexions.push_back({ptMm(40, 30), {}});
  // Un fichier sans identifiant : la lecture en tire un, le même à chaque lecture
  J j = versJson(c);
  for (auto& x : j["feuilles"][0]["jonctions"]) x = J::array({x[0], x[1]});
  for (auto& x : j["feuilles"][0]["nonConnexions"]) x = J::array({x[0], x[1]});
  const ConceptionElec a = conceptionDepuisJson(j), b = conceptionDepuisJson(j);
  REQUIRE(!a.feuilles[0].jonctions.empty());
  REQUIRE(!a.feuilles[0].nonConnexions.empty());
  CHECK(!a.feuilles[0].jonctions[0].uuid.empty());
  CHECK(a.feuilles[0].jonctions[0].uuid == b.feuilles[0].jonctions[0].uuid);
  CHECK(a.feuilles[0].nonConnexions[0].uuid == b.feuilles[0].nonConnexions[0].uuid);
  CHECK(a.feuilles[0].jonctions[0].uuid.size() == 36);
  // Écrit en [x, y, uuid] : relu à l'identique ; une version qui ne lit que [x, y] y trouve toujours le point
  const J ecrit = versJson(a);
  const J& premiere = ecrit["feuilles"][0]["jonctions"][0];
  CHECK(premiere.size() == 3);
  CHECK(premiere[0].is_number());
  CHECK(premiere[1].is_number());
  CHECK(conceptionDepuisJson(ecrit).feuilles[0].jonctions[0].uuid == a.feuilles[0].jonctions[0].uuid);
  // Tourner la sélection garde l'identifiant ; le collage en donne un nouveau
  ConceptionElec d = a;
  const std::string id = idJonction(d.feuilles[0].jonctions[0]);
  transformerSelectionSchema(d, 0, {id, d.feuilles[0].symboles[0].uuid}, OperationSchema::Tourner);
  bool trouve = false;
  for (const auto& x : d.feuilles[0].jonctions)
    if (x.uuid == id) trouve = true;
  CHECK(trouve);
  const PressePapiersSchema pp = copierSchema(d, 0, {id});
  REQUIRE(pp.jonctions.size() == 1);
  const auto crees = collerSchema(d, 0, pp, ptMm(80, 80));
  REQUIRE(crees.size() == 1);
  CHECK(*crees.begin() != id);
  CHECK(std::count_if(d.feuilles[0].jonctions.begin(), d.feuilles[0].jonctions.end(), [&](const Jonction& x) { return x.uuid == *crees.begin(); }) == 1);
}

TEST_CASE("1.21 : les dispositions des panneaux gardées dans le projet sont relues et réécrites", "[L1][ERG-23]") {
  prom::Projet p;
  p.nom = "Dispositions";
  CHECK_FALSE(prom::versJson(p).contains("dispositions"));  // rien d'écrit quand il n'y en a pas
  p.dispositions["Routage"] = R"({"format":"promethee-disposition","version":1,"nom":"Routage","etat":"AAAA"})";
  p.dispositions["Saisie"] = "{}";
  const auto j = prom::versJson(p);
  REQUIRE(j.contains("dispositions"));
  std::vector<std::string> avertissements;
  const prom::Projet relu = prom::normaliser(j, &avertissements);
  CHECK(relu.dispositions == p.dispositions);
  CHECK(relu.inconnus.empty());  // champ connu : pas gardé à part, pas signalé
  CHECK(std::none_of(avertissements.begin(), avertissements.end(), [](const std::string& a) { return a.find("dispositions") != std::string::npos; }));
  // Une valeur qui n'est pas une chaîne est écartée
  auto abime = j;
  abime["dispositions"]["Abîmée"] = 3;
  CHECK(prom::normaliser(abime).dispositions.size() == 2);
}

namespace {
// Réseaux d'une conception : nom → broches (repère du composant, numéro), pour comparer deux lectures
std::map<std::string, std::set<std::string>> reseaux(const ConceptionElec& c) {
  std::map<std::string, std::set<std::string>> r;
  const Connectivite k = connectivite(c);
  for (const auto& x : k.reseaux) {
    std::set<std::string>& b = r[x.nom];
    for (const auto& n : x.broches) {
      const ComposantElec* comp = c.composant(n.composant);
      b.insert((comp ? comp->reference : n.composant) + "." + n.broche);
    }
  }
  return r;
}
bool contient(const std::vector<std::string>& v, const std::string& morceau) {
  return std::any_of(v.begin(), v.end(), [&](const std::string& x) { return x.find(morceau) != std::string::npos; });
}
}  // namespace

TEST_CASE("1.22 : chaque étape de migration a sa descente, et les étapes s'enchaînent", "[L1][ECH-11]") {
  const auto& etapes = prom::etapesMigration();
  REQUIRE_FALSE(etapes.empty());
  for (const auto& e : etapes) {
    INFO(e.depuis << " → " << e.vers << " (" << e.objet << ")");
    CHECK(static_cast<bool>(e.monter));
    CHECK(static_cast<bool>(e.descendre));  // une migration sans inverse est refusée
    CHECK(prom::comparerVersions(e.depuis, e.vers) < 0);
    CHECK((e.objet == "projet" || e.objet == "conception"));
    CHECK_FALSE(e.resume.empty());
  }
  // Rangées de la plus ancienne à la plus récente, sans trou : chaque version de départ est l'arrivée d'une autre
  for (std::size_t i = 1; i < etapes.size(); ++i) CHECK(prom::comparerVersions(etapes[i - 1].depuis, etapes[i].depuis) <= 0);
  CHECK(etapes.back().vers == prom::versionFichierCourante());
  CHECK(prom::versionsAnterieures() == std::vector<std::string>{"0.30", "0.29", "0.28"});
  CHECK(prom::comparerVersions("0.29", "0.30") < 0);
  CHECK(prom::comparerVersions("0.30", "0.30.0") == 0);
  CHECK(prom::comparerVersions("0.100", "0.30") > 0);
  CHECK(prom::nomPourVersion("carte.prom", "0.29") == "carte.pour-0-29.prom");
  CHECK(prom::nomPourVersion("dossier/carte.prom.json", "0.28") == "dossier/carte.pour-0-28.prom.json");
}

TEST_CASE("1.22 : un projet de la 0.30 écrit pour la 0.29 garde ses réseaux, et chaque perte est dite", "[L1][ECH-11]") {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.electronique.push_back(exempleElectronique());
  p.dispositions["Routage"] = "{}";
  // Une clé d'une version plus récente, que la 0.30 garde
  auto j = prom::versJson(p);
  j["electronique"][0]["futur"] = 1;
  p = prom::normaliser(j);
  const ConceptionElec& c = p.electronique.front();
  REQUIRE_FALSE(c.clesInconnues.empty());
  // Les écritures de la 0.30 gardent « formatElectronique » : la 0.29 ne les prend pas pour des fichiers de la 0.28
  CHECK(prom::versJson(p)["electronique"][0]["formatElectronique"] == FORMAT_ELECTRONIQUE);
  CHECK(prom::versJson(p)["electronique"][0]["format"] == FORMAT_ELECTRONIQUE);

  const prom::FichierAnterieur f = prom::pourVersion(p, "0.29");
  CHECK(f.version == "0.29");
  CHECK_FALSE(f.projet.contains("dispositions"));
  const auto& ce = f.projet["electronique"][0];
  CHECK_FALSE(ce.contains("format"));
  CHECK(ce["formatElectronique"] == 2);
  CHECK(prom::versionConception(ce) == "0.29");
  for (const auto& fe : ce["feuilles"])
    for (const char* k : {"jonctions", "nonConnexions"})
      for (const auto& x : fe[k]) CHECK(x.size() == 2);
  CHECK(contient(f.pertes, "disposition des panneaux"));
  CHECK(contient(f.pertes, "version plus récente"));
  const bool jonctions = std::any_of(c.feuilles.begin(), c.feuilles.end(), [](const Feuille& x) { return !x.jonctions.empty() || !x.nonConnexions.empty(); });
  CHECK(contient(f.pertes, "non-connexion") == jonctions);
  // Rien d'inventé : pas de schéma seul, pas de perte à son sujet
  CHECK_FALSE(contient(f.pertes, "schéma seul"));
  // Relu (ici par la 0.30, qui le monte), le projet a les mêmes réseaux, broche par broche
  std::vector<std::string> notes;
  const prom::Projet relu = prom::normaliser(f.projet, &notes);
  REQUIRE(relu.electronique.size() == 1);
  CHECK(reseaux(relu.electronique.front()) == reseaux(c));
  CHECK(relu.electronique.front().circuit.textes.size() == c.circuit.textes.size());

  // Un schéma seul le dit
  prom::Projet s = p;
  s.electronique.front().schemaSeul = true;
  CHECK(contient(prom::pourVersion(s, "0.29").pertes, "schéma seul"));
  // Version inconnue : refusée
  CHECK_THROWS_AS(prom::pourVersion(p, "0.12"), std::invalid_argument);
}

TEST_CASE("1.22 : écrit pour la 0.28, un projet garde le dessin de ses textes de carte et dit ce qu'il perd", "[L1][ECH-11][X3]") {
  prom::Projet p = prom::projetVide();
  ConceptionElec c = exempleElectronique();
  TexteCircuit t;
  t.uuid = "texte-essai";
  t.texte = "V1";
  t.p = ptMm(10, 10);
  t.rotation = 90;
  c.circuit.textes.push_back(t);
  c.circuit.regles.capacitesPerso = CapacitesFabricant{};
  c.composants.front().horsNomenclature = true;
  c.composants.front().brochageVerifie = "QFN-16";
  p.electronique.push_back(c);
  const prom::FichierAnterieur f = prom::pourVersion(p, "0.28");
  const auto& ce = f.projet["electronique"][0];
  CHECK_FALSE(ce.contains("format"));
  CHECK_FALSE(ce.contains("formatElectronique"));
  CHECK(prom::versionConception(ce) == "0.28");
  bool retourne = false;
  for (const auto& x : ce["circuit"]["textes"])
    if (x["uuid"] == "texte-essai") retourne = x["rot"].get<double>() == -90;
  CHECK(retourne);  // la 0.28 tournait ses textes dans l'autre sens
  CHECK(contient(f.pertes, "profil de fabricant personnalisé"));
  CHECK(contient(f.pertes, "nomenclature"));
  CHECK(contient(f.pertes, "brochage vérifié"));
  // Relu par la 0.30 : le texte revient à 90°, les réseaux sont les mêmes
  const prom::Projet relu = prom::normaliser(f.projet);
  double rot = 0;
  for (const auto& x : relu.electronique.front().circuit.textes)
    if (x.uuid == "texte-essai") rot = x.rotation;
  CHECK(rot == Catch::Approx(90));
  CHECK(reseaux(relu.electronique.front()) == reseaux(p.electronique.front()));
}

TEST_CASE("1.22 : une carte de la 0.28 est copiée à côté avant d'être réécrite", "[L1][ECH-11]") {
  namespace fs = std::filesystem;
  const fs::path dossier = fs::temp_directory_path() / "promethee-essai-migration";
  fs::remove_all(dossier);
  fs::create_directories(dossier);
  prom::Projet p = prom::projetVide();
  p.electronique.push_back(exempleElectronique());
  const prom::FichierAnterieur f = prom::pourVersion(p, "0.28");
  const std::string chemin = prom::utf8(dossier / "carte.prom");
  prom::ecrirePourVersion(f, chemin);
  const std::string original = prom::lireFichier(dossier / "carte.prom");
  prom::RapportLecture r;
  const prom::Projet lu = prom::lireProjet(chemin, &r);
  CHECK(contient(r.avertissements, ".format1.bak"));
  prom::ecrireProjet(lu, chemin);
  REQUIRE(fs::exists(dossier / "carte.prom.format1.bak"));
  CHECK(prom::lireFichier(dossier / "carte.prom.format1.bak") == original);
  // Réécrit, le fichier est à la version courante ; une seconde écriture ne touche pas la copie
  CHECK(prom::versionConception(prom::Json::parse(prom::lireFichier(dossier / "carte.prom"))["electronique"][0]) == prom::versionFichierCourante());
  prom::ecrireProjet(lu, chemin);
  CHECK(prom::lireFichier(dossier / "carte.prom.format1.bak") == original);
  fs::remove_all(dossier);
}
