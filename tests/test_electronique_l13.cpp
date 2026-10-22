// Prométhée : essais du lot L13 (automatisation). Licence GPL-3.0-only.
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/api_commandes.hpp"
#include "promethee/archive.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/placement_auto.hpp"
#include "promethee/revue_conception.hpp"
#include "promethee/sorties.hpp"

using namespace prom;
using namespace prom::ecad;
namespace fs = std::filesystem;

namespace {
fs::path dossierEssai(const std::string& nom) {
  const fs::path d = fs::temp_directory_path() / ("promethee-l13-" + nom);
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
struct Montage {
  ConceptionElec c = nouvelleConception("Revue");
  double x = 25.4;
  void poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, 50.8));
    x += 25.4;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        ComposantElec* k = c.composant(p.composant);
        k->reference = ref;
        k->valeur = valeur;
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
  }
};
}  // namespace

TEST_CASE("13.2, 13.7 · « Dossier JLCPCB » lancé en une commande ; empreinte différente : publication refusée", "[L13][sorties]") {
  const fs::path d = dossierEssai("jeux");
  ConceptionElec c = exempleElectronique();
  c.cartouche.revision = "B";
  const JeuSorties j = jeuJlcpcb();
  CHECK(dossierDuJeu(j, c, "station") == "sorties/Clignotant_555-B-jlcpcb");
  const ResultatJeu r = executerJeu(c, j, utf8(d), "station", "essai");
  INFO(r.refus.size() << " refus");
  REQUIRE(r.publie);
  CHECK(r.dossier == utf8(d / "sorties" / "Clignotant_555-B-jlcpcb") + ".zip");
  const auto fichiers = lireZip(lireFichier(cheminFichier(r.dossier)));
  std::set<std::string> noms;
  for (const auto& [n, contenu] : fichiers) noms.insert(n);
  CHECK(std::any_of(noms.begin(), noms.end(), [](const std::string& n) { return n.find("F_Cu.gbr") != std::string::npos; }));
  CHECK(noms.count("Percage/Clignotant_555-PTH.drl"));
  CHECK(noms.count("Assemblage/Clignotant_555-placement-en.csv"));
  CHECK(noms.count("Assemblage/Clignotant_555-nomenclature-jlcpcb.csv"));
  CHECK_FALSE(fs::exists(cheminFichier(utf8(d / "sorties" / "Clignotant_555-B-jlcpcb") + ".preparation")));
  // Dossier non compressé, variante, autres sorties ; un second lancement garde le premier en « .precedent »
  JeuSorties k;
  k.nom = "Revue";
  k.dossier = "revue/{carte}";
  k.sorties = {"nomenclature", "visionneuse", "cout", "variantes", "projet", "inconnue"};
  const ResultatJeu r2 = executerJeu(c, k, utf8(d), "station", "essai");
  REQUIRE(r2.publie);
  CHECK(r2.fichiers.size() == 5);
  CHECK(std::any_of(r2.journal.begin(), r2.journal.end(), [](const std::string& l) { return l.find("inconnue") != std::string::npos; }));
  const ResultatJeu r3 = executerJeu(c, k, utf8(d), "station", "essai");
  CHECK(r3.publie);
  CHECK(fs::exists(d / "revue" / "Clignotant_555.precedent"));
  // Empreinte figée du projet modifiée : le jeu s'arrête avant d'écrire, rapport des différences joint
  ConceptionElec modifiee = c;
  REQUIRE(!modifiee.empreintes.empty());
  std::string id;
  for (auto& [e, def] : modifiee.empreintes)
    if (genererEmpreinte(e) && !def.pastilles.empty()) {
      def.pastilles[0].largeur += 200'000;
      id = e;
      break;
    }
  REQUIRE(!id.empty());
  JeuSorties l = jeuJlcpcb();
  l.dossier = "refus/{carte}";
  const ResultatJeu refuse = executerJeu(modifiee, l, utf8(d), "station", "essai");
  CHECK_FALSE(refuse.publie);
  REQUIRE(!refuse.refus.empty());
  CHECK(refuse.refus[0].find(id) != std::string::npos);
  CHECK(refuse.rapport.find("pastille") != std::string::npos);
  CHECK_FALSE(fs::exists(cheminFichier(utf8(d / "refus" / "Clignotant_555") + ".zip")));
  CHECK(fs::exists(cheminFichier(utf8(d / "refus" / "Clignotant_555") + "-refus.txt")));
  // Article générique : refusé aussi
  ConceptionElec generique = c;
  Article a;
  a.id = "ldo";
  a.nom = "LDO 3,3 V";
  a.generique = true;
  generique.articles["ldo"] = a;
  for (auto& x : generique.composants)
    if (x.reference == "U1") x.article = "ldo";
  CHECK(!controlerAvantSorties(generique).empty());
}

TEST_CASE("13.10 · Interface JSON-RPC : dix vias posées en un lot, un seul pas ; un lot en échec ne touche rien", "[L13][api]") {
  Projet p = projetVide();
  p.electronique.push_back(exempleElectronique());
  const std::size_t vias = p.electronique[0].circuit.vias.size();
  nlohmann::ordered_json lot = {{"jsonrpc", "2.0"}, {"id", 7}, {"method", "lot"}, {"params", nlohmann::ordered_json::array()}};
  for (int i = 0; i < 10; ++i) lot["params"].push_back({{"method", "via.poser"}, {"params", {{"x", 5.0 + i}, {"y", 5.0}, {"reseau", "GND"}}}});
  const ReponseApi r = traiterRequete(p, lot);
  CHECK(r.modifie);
  CHECK(r.nomPas == "API : 10 modifications");
  CHECK(r.reponse["id"] == 7);
  CHECK(r.reponse["result"].size() == 10);
  CHECK(p.electronique[0].circuit.vias.size() == vias + 10);
  // Une requête du lot échoue : rien n'est appliqué
  lot["params"][3]["params"]["diametre"] = 0.1;
  const ReponseApi e = traiterRequete(p, lot);
  CHECK_FALSE(e.modifie);
  CHECK(e.reponse.contains("error"));
  CHECK(p.electronique[0].circuit.vias.size() == vias + 10);
  // Lecture, erreurs JSON-RPC, méthodes
  CHECK(traiterTexte(p, R"({"jsonrpc":"2.0","id":1,"method":"projet.resume"})").reponse["result"]["conceptions"][0]["composants"] == p.electronique[0].composants.size());
  CHECK(traiterTexte(p, R"({"jsonrpc":"2.0","id":2,"method":"inconnue"})").reponse["error"]["code"] == -32601);
  CHECK(traiterTexte(p, "{abîmé").reponse["error"]["code"] == -32700);
  const ReponseApi v = traiterTexte(p, R"({"jsonrpc":"2.0","id":3,"method":"composant.valeur","params":{"ref":"R1","valeur":"3k3"}})");
  CHECK(v.modifie);
  CHECK(p.electronique[0].parReference("R1")->valeur == "3k3");
  CHECK(traiterTexte(p, R"({"jsonrpc":"2.0","id":4,"method":"conception.reseaux"})").reponse["result"].contains("GND"));
  CHECK(traiterTexte(p, R"({"jsonrpc":"2.0","id":5,"method":"revue"})").reponse["result"].is_array());
  CHECK(methodesApi().size() >= 12);
}

TEST_CASE("13.12 · Carte de 40 composants placée d'office : chevelu raccourci d'au moins 30 %, aucune collision", "[L13][placement]") {
  ConceptionElec c = carteDeMesure(40, false);
  REQUIRE(c.circuit.empreintes.size() >= 40);
  // Rangement autour de la carte : tout en file sous le contour
  const Boite carte = boite(c.circuit.contour.polygone());
  Nm x = carte.x0;
  for (auto& e : c.circuit.empreintes) {
    e.position = {x, carte.y1 + 15'000'000};
    x += 9'000'000;
  }
  const double avant = longueurChevelu(c);
  OptionsPlacementAuto o;
  o.graine = 3;
  const ResultatPlacementAuto r = placerAutomatiquement(c, o);
  INFO("chevelu " << avant << " mm → " << r.chevelu << " mm, " << r.collisions << " collision(s)");
  CHECK(r.collisions == 0);
  CHECK(r.chevelu <= 0.7 * avant);
  CHECK(collisionsPlacement(c, o.ecart) == 0);
  CHECK(r.deplaces == static_cast<int>(c.circuit.empreintes.size()));
  // Une empreinte verrouillée ne bouge pas
  c.circuit.empreintes[0].verrou = true;
  const Pt fixe = c.circuit.empreintes[0].position;
  placerAutomatiquement(c, o);
  CHECK(c.circuit.empreintes[0].position == fixe);
}

TEST_CASE("13.13 · Revue : LED sans résistance, régulateur sans condensateur de sortie, I2C sans tirage", "[L13][revue]") {
  Montage m;
  m.poser("Conn_01x02", "J1", "", {{"1", "VIN"}, {"2", "GND"}});
  m.poser("LM7805", "U1", "LM7805", {{"1", "VIN"}, {"2", "GND"}, {"3", "+5V"}});
  m.poser("C", "C1", "10u", {{"1", "VIN"}, {"2", "GND"}});
  m.poser("LED", "D1", "rouge", {{"2", "+5V"}, {"1", "GND"}});
  m.poser("ATtiny85", "U2", "ATtiny85", {{"8", "+5V"}, {"4", "GND"}, {"5", "SDA"}, {"7", "SCL"}});
  m.poser("C", "C2", "100n", {{"1", "+5V"}, {"2", "GND"}});
  const auto points = revueConception(m.c);
  std::set<std::string> regles;
  for (const auto& p : points) regles.insert(p.regle);
  INFO(points.size() << " point(s)");
  CHECK(regles.count("LED-R"));
  CHECK(regles.count("I2C-TIRAGE"));
  // C2 est sur +5V : le régulateur a son condensateur de sortie ; le retirer fait apparaître REG-COUT
  CHECK_FALSE(regles.count("REG-COUT"));
  CHECK_FALSE(regles.count("REG-CIN"));
  Montage n = m;
  n.c.feuilles[0].symboles.erase(std::remove_if(n.c.feuilles[0].symboles.begin(), n.c.feuilles[0].symboles.end(),
                                                [&](const SymbolePlace& s) { return n.c.composant(s.composant)->reference == "C2"; }),
                                 n.c.feuilles[0].symboles.end());
  std::set<std::string> r2;
  for (const auto& p : revueConception(n.c)) r2.insert(p.regle);
  CHECK(r2.count("REG-COUT"));
  CHECK(r2.count("DECOUPLAGE"));
  // Corrections faites : plus rien sur ces règles
  m.poser("R", "R1", "4k7", {{"1", "SDA"}, {"2", "+5V"}});
  m.poser("R", "R2", "4k7", {{"1", "SCL"}, {"2", "+5V"}});
  m.poser("R", "R3", "1k", {{"1", "+5V"}, {"2", "LED_A"}});
  for (auto& f : m.c.feuilles)
    for (auto& e : f.etiquettes)
      if (e.texte == "+5V" && e.p.x < ptMm(25.4 * 4 + 5, 0).x && e.p.x > ptMm(25.4 * 4 - 5, 0).x) e.texte = "LED_A";
  std::set<std::string> r3;
  for (const auto& p : revueConception(m.c)) r3.insert(p.regle);
  CHECK_FALSE(r3.count("I2C-TIRAGE"));
  // L'exemple au NE555 est propre sur ces trois règles
  std::set<std::string> ex;
  for (const auto& p : revueConception(exempleElectronique())) ex.insert(p.regle);
  CHECK_FALSE(ex.count("LED-R"));
  CHECK_FALSE(ex.count("I2C-TIRAGE"));
  CHECK(reglesRevue().size() == 7);
}
