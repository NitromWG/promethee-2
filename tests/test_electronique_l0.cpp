// Prométhée : essais du lot L0 de l'audit du 15 octobre 2026 (« plus aucune carte fausse en silence »). Un essai par
// défaut corrigé, nommé par son code (R1, B6, F1…) : il échouait sur la 0.28.3 et passe depuis la 0.29.
// Licence GPL-3.0-only.
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <set>
#include <sstream>

#include "promethee/hauteurs.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/police.hpp"

using namespace prom::ecad;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

namespace {
int compter(const std::vector<Violation>& v, const std::string& code) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
}
bool contient(const std::vector<Violation>& v, const std::string& code, const std::string& morceau) {
  return std::any_of(v.begin(), v.end(), [&](const Violation& x) { return x.code == code && x.message.find(morceau) != std::string::npos; });
}
ConceptionElec carte(double l = 60, double h = 40) {
  ConceptionElec c = nouvelleConception("Essai L0");
  c.circuit.contour.largeur = nm(l);
  c.circuit.contour.hauteur = nm(h);
  c.circuit.contour.rayon = 0;
  return c;
}
std::vector<Violation> drc(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  return verifierDRC(c, k, remplirZones(c, k));
}
Polygone rect(double x0, double y0, double x1, double y1) { return orienter({ptMm(x0, y0), ptMm(x1, y0), ptMm(x1, y1), ptMm(x0, y1)}); }
Zone zone(const std::string& reseau, Polygone contour, int priorite = 0, Couche couche = Couche::CuivreDessus) {
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = reseau;
  z.couche = couche;
  z.contour = std::move(contour);
  z.priorite = priorite;
  z.thermique = false;
  return z;
}
Piste piste(const std::string& reseau, Pt a, Pt b, Couche couche = Couche::CuivreDessus, double largeur = 0.3) { return {nouvelUuid(), reseau, couche, a, b, nm(largeur)}; }
// Plus petite distance entre deux cuivres remplis : 0 s'ils se touchent
double ecartMm(const Polygones& a, const Polygones& b) {
  double d = 1e18;
  for (const auto& x : a)
    for (const auto& y : b) d = std::min(d, distancePolygones(x, y));
  return d * 1e-6;
}
std::string dossier(const std::string& nom) {
  const fs::path d = fs::temp_directory_path() / ("promethee_l0_" + nom);
  fs::remove_all(d);
  fs::create_directories(d);
  return d.string();
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------
// Cuivre et zones
TEST_CASE("R1 : l'isolement entre zones suit les classes, dans les deux sens", "[L0][R1]") {
  for (const int hautePriorite : {0, 1}) {
    ConceptionElec c = carte();
    c.circuit.regles.classes.push_back({"Secteur", nm(2.0), nm(0.5)});
    c.circuit.regles.classeReseau["HT"] = "Secteur";
    // Zones qui se recouvrent sur la même couche, chacune tenue par une piste de son réseau
    c.circuit.zones.push_back(zone("HT", rect(2, 2, 32, 38), hautePriorite == 0 ? 1 : 0));
    c.circuit.zones.push_back(zone("BT", rect(28, 2, 58, 38), hautePriorite == 0 ? 0 : 1));
    c.circuit.pistes.push_back(piste("HT", ptMm(10, 20), ptMm(15, 20)));
    c.circuit.pistes.push_back(piste("BT", ptMm(45, 20), ptMm(50, 20)));
    const Connectivite k = connectivite(c);
    Remplissages r = remplirZones(c, k);
    const Polygones& ht = r[c.circuit.zones[0].uuid];
    const Polygones& bt = r[c.circuit.zones[1].uuid];
    REQUIRE_FALSE(ht.empty());
    REQUIRE_FALSE(bt.empty());
    // Remplies à 2,0 mm au moins l'une de l'autre, quelle que soit la priorité
    CHECK(ecartMm(ht, bt) >= 2.0 - 0.01);
    CHECK(compter(verifierDRC(c, k, r), "DRC-24") == 0);
    // Contrôle indépendant : un remplissage abîmé, à 0,5 mm de l'autre zone, est vu par la DRC-24, dans les deux sens
    const Polygones intacte = ht;
    r[c.circuit.zones[1].uuid] = soustraire({rect(2, 2, 58, 38)}, gonfler(intacte, nm(0.5)));
    CHECK(compter(verifierDRC(c, k, r), "DRC-24") >= 1);
  }
}

TEST_CASE("C19 : le voisinage du remplissage couvre un isolement de classe de plus de 5 mm", "[L0][C19]") {
  ConceptionElec c = carte(80, 40);
  c.circuit.regles.classes.push_back({"HT", nm(6.0), nm(0.5)});
  c.circuit.regles.classeReseau["L"] = "HT";
  c.circuit.zones.push_back(zone("GND", rect(2, 2, 40, 38)));
  c.circuit.pistes.push_back(piste("GND", ptMm(10, 20), ptMm(15, 20)));
  // Piste haute tension à 5,5 mm du bord de la zone : hors de l'ancien voisinage de 5 mm
  c.circuit.pistes.push_back(piste("L", ptMm(45.5 + 0.15, 5), ptMm(45.5 + 0.15, 35)));
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  const Polygones& gnd = r.at(c.circuit.zones[0].uuid);
  REQUIRE_FALSE(gnd.empty());
  double d = 1e18;
  for (const auto& p : gnd) d = std::min(d, distancePolygoneSegment(p, ptMm(45.65, 5), ptMm(45.65, 35)) - 150'000.0);
  CHECK(d * 1e-6 >= 6.0 - 0.01);
}

TEST_CASE("C21 : deux zones du même réseau qui se recouvrent forment un seul cuivre", "[L0][C21]") {
  ConceptionElec c = carte(80, 40);
  // Deux connecteurs, chacun relié à GND, chacun sous sa propre zone ; les zones se recouvrent au milieu
  const std::string j1 = poserSymbole(c, 0, "Conn_01x02", ptMm(20.32, 50.8)), j2 = poserSymbole(c, 0, "Conn_01x02", ptMm(60.96, 50.8));
  const std::string g1 = poserSymbole(c, 0, "GND", ptMm(30.48, 66.04)), g2 = poserSymbole(c, 0, "GND", ptMm(71.12, 66.04));
  auto broche = [&](const std::string& sym, const std::string& n) {
    for (const auto& s : c.feuilles[0].symboles)
      if (s.uuid == sym)
        for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
          if (b.numero == n) return b.p;
    return Pt{};
  };
  for (const auto& [j, g] : {std::pair{j1, g1}, std::pair{j2, g2}}) {
    const Pt a = broche(j, "2"), m = broche(g, "1");
    c.feuilles[0].fils.push_back({nouvelUuid(), a, {m.x, a.y}});
    c.feuilles[0].fils.push_back({nouvelUuid(), {m.x, a.y}, m});
  }
  annoter(c, false);
  mettreAJourCircuit(c);
  for (auto& e : c.circuit.empreintes) e.position = c.composant(e.composant)->reference == "J1" ? ptMm(12, 20) : ptMm(68, 20);
  c.circuit.zones.push_back(zone("GND", rect(2, 2, 45, 38), 0, Couche::CuivreDessous));
  c.circuit.zones.push_back(zone("GND", rect(35, 2, 78, 38), 0, Couche::CuivreDessous));
  // Une troisième zone GND sans pastille, qui ne touche que la première : elle est gardée (îlot relié par elle)
  c.circuit.zones.push_back(zone("GND", rect(2, 30, 20, 38), 0, Couche::CuivreDessus));
  c.circuit.zones.back().couche = Couche::CuivreDessous;
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  for (const auto& z : c.circuit.zones) CHECK_FALSE(r.at(z.uuid).empty());
  const EtatRoutage e = etatRoutage(c, k, r);
  CHECK(std::none_of(e.chevelu.begin(), e.chevelu.end(), [](const Liaison& l) { return l.reseau == "GND"; }));
  CHECK(compter(verifierDRC(c, k, r), "DRC-20") == 0);
}

TEST_CASE("R6 : deux cuivres sans réseau qui se touchent ne sont pas un court-circuit", "[L0][R6]") {
  ConceptionElec c = carte();
  c.circuit.pistes.push_back(piste("", ptMm(10, 10), ptMm(20, 10)));
  c.circuit.pistes.push_back(piste("", ptMm(20, 10), ptMm(30, 10)));
  const auto v = drc(c);
  CHECK(compter(v, "DRC-21") == 0);
  CHECK(compter(v, "DRC-56") == 2);  // signalé à part, en avertissement
  for (const auto& x : v)
    if (x.code == "DRC-56") CHECK(x.gravite == Gravite::Avertissement);
  // Le même cuivre sans réseau entre deux réseaux nommés reste un court-circuit
  c.circuit.pistes.push_back(piste("A", ptMm(5, 10), ptMm(10, 10)));
  c.circuit.pistes.push_back(piste("B", ptMm(30, 10), ptMm(35, 10)));
  CHECK(compter(drc(c), "DRC-21") >= 1);
}

TEST_CASE("C20 : un texte en cuivre est du cuivre pour les zones, la connectivité et le Gerber", "[L0][C20]") {
  ConceptionElec c = carte();
  c.circuit.textes.push_back({nouvelUuid(), "LOGO", Couche::CuivreDessus, ptMm(30, 20), nm(3), 0});
  c.circuit.zones.push_back(zone("GND", rect(2, 2, 58, 38)));
  c.circuit.pistes.push_back(piste("GND", ptMm(5, 5), ptMm(10, 5)));
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  // La zone s'écarte du texte : aucun trait du texte n'est dans le cuivre rempli
  for (const auto& t : textesSerigraphie(c))
    if (t.couche == Couche::CuivreDessus) CHECK_FALSE(dedans(r.at(c.circuit.zones[0].uuid), t.a));
  const auto v = verifierDRC(c, k, r);
  CHECK(contient(v, "DRC-56", "LOGO"));
  // Gerber : du cuivre sans fonction électrique, plus une légende
  const ModeleFabrication m = modeleFabrication(c, k, r);
  const std::string g = gerber(m, Couche::CuivreDessus, "essai");
  CHECK_THAT(g, ContainsSubstring("NonConductor"));
  // Un texte d'un seul tenant (« H ») qui touche deux pistes de réseaux différents fait un court-circuit, vu par la
  // connectivité et placé sur l'un des contacts
  ConceptionElec d = carte();
  d.circuit.textes.push_back({nouvelUuid(), "H", Couche::CuivreDessus, ptMm(30, 20), nm(3), 0});
  Boite b;
  for (const auto& t : textesSerigraphie(d)) { b.etendre(t.a); b.etendre(t.b); }
  d.circuit.pistes.push_back(piste("A", Pt{b.x0 - nm(3), b.centre().y}, Pt{b.x0 + nm(0.2), b.centre().y}));
  d.circuit.pistes.push_back(piste("B", Pt{b.x1 - nm(0.2), b.centre().y}, Pt{b.x1 + nm(3), b.centre().y}));
  const Connectivite kd = connectivite(d);
  const EtatRoutage ed = etatRoutage(d, kd, remplirZones(d, kd));
  REQUIRE(ed.courtsCircuits.size() == 1);
  CHECK(b.gonflee(nm(0.5)).contient(ed.courtsCircuits.front().position));
}

TEST_CASE("R4 : du cuivre qui coupe le bord ou une découpe est en DRC-06, jamais dérogeable", "[L0][R4]") {
  ConceptionElec c = carte();
  // Piste à cheval sur le bord droit : son centre et ses bouts restent dedans, sa forme non
  c.circuit.pistes.push_back(piste("A", ptMm(59.9, 10), ptMm(59.9, 20), Couche::CuivreDessus, 0.5));
  auto v = drc(c);
  CHECK(compter(v, "DRC-06") == 1);
  CHECK_FALSE(derogeable("DRC-06"));
  // Piste qui traverse une découpe
  ConceptionElec d = carte();
  d.circuit.contour.decoupes.push_back(rect(25, 15, 35, 25));
  d.circuit.pistes.push_back(piste("A", ptMm(20, 20), ptMm(40, 20)));
  CHECK(compter(drc(d), "DRC-06") == 1);
  // Composant dont les pastilles sont à moitié hors de la carte : DRC-06 et DRC-09, toutes deux critiques
  ConceptionElec e = carte();
  const std::string j = poserSymbole(e, 0, "Conn_01x02", ptMm(50.8, 50.8));
  (void)j;
  annoter(e, false);
  mettreAJourCircuit(e);
  e.circuit.empreintes.front().position = ptMm(0.2, 20);
  v = drc(e);
  CHECK(compter(v, "DRC-06") == 1);
  CHECK(compter(v, "DRC-09") == 1);
  for (const auto& x : v)
    if (x.code == "DRC-09") CHECK(x.gravite == Gravite::Erreur);
}

TEST_CASE("R7 : la DRC-24 contrôle les trous non métallisés à la distance cuivre-trou, et les découpes", "[L0][R7]") {
  ConceptionElec c = carte();
  c.circuit.regles.cuivreTrou = nm(1.0);
  c.circuit.zones.push_back(zone("GND", rect(2, 2, 58, 38)));
  c.circuit.pistes.push_back(piste("GND", ptMm(5, 5), ptMm(10, 5)));
  c.circuit.contour.decoupes.push_back(rect(40, 15, 45, 20));
  const Connectivite k = connectivite(c);
  Remplissages r = remplirZones(c, k);
  CHECK(compter(verifierDRC(c, k, r), "DRC-07") == 0);
  // Remplissage abîmé : il entre dans la découpe
  r[c.circuit.zones[0].uuid].push_back(rect(39, 14, 46, 21));
  r[c.circuit.zones[0].uuid] = unir(r[c.circuit.zones[0].uuid]);
  CHECK(contient(verifierDRC(c, k, r), "DRC-07", "découpe"));
}

// ---------------------------------------------------------------------------------------------------------
// Porte de publication et classes
TEST_CASE("R2 : une carte simple face avec des CMS sur la face sans cuivre ne se publie pas", "[L0][R2]") {
  ConceptionElec c = exempleElectronique();
  c.circuit.empilage.couchesCuivre = 1;
  c.circuit.pistes.clear();
  c.circuit.vias.clear();
  c.circuit.zones.clear();
  const Validation v = valider(c);
  int drc38 = 0, drc20 = 0;
  for (const auto& e : v.etapes)
    for (const auto& d : e.details) {
      drc38 += d.code == "DRC-38";
      drc20 += d.code == "DRC-20";
    }
  CHECK(drc38 >= 1);
  CHECK(drc20 >= 1);  // les pastilles inaccessibles comptent au chevelu
  CHECK_FALSE(derogeable("DRC-38"));
  CHECK(v.critiques() > 0);
  CHECK_THROWS(publier(c, dossier("r2"), "Evan", "2026-10-17", "Je sais ce que je fais", "essai"));
}

TEST_CASE("R3 : renommer le profil du fabricant ne fait disparaître aucune erreur de capacité", "[L0][R3]") {
  ConceptionElec c = exempleElectronique();
  c.circuit.regles.isolement = nm(0.05);
  c.circuit.regles.bord = 0;
  auto critiques = [](const ConceptionElec& x) { return valider(x).critiques(); };
  const int standard = critiques(c);
  CHECK(standard >= 2);
  c.circuit.regles.profil = "Mon fabricant";
  CHECK(critiques(c) >= 1);
  CHECK_FALSE(derogeable("DFM-20"));
  CHECK_FALSE(valider(c).publiable());
  // Profil personnalisé complet, aux mêmes capacités que « Standard » : mêmes erreurs qu'avec lui
  CapacitesFabricant cap;
  const Regles& s = profilsFabricants().front().regles;
  cap.isolement = s.isolement;
  cap.largeurMin = s.largeurMin;
  cap.percageMin = s.percageMin;
  cap.anneauMin = s.anneauMin;
  cap.bord = s.bord;
  c.circuit.regles.capacitesPerso = cap;
  CHECK(critiques(c) == standard);
  // Incomplet : refusé comme un profil inconnu
  c.circuit.regles.capacitesPerso->bord = 0;
  CHECK_FALSE(profilDeReference(c.circuit.regles));
}

TEST_CASE("R5 : les motifs de classes s'essaient dans l'ordre saisi, un motif masqué est signalé", "[L0][R5]") {
  Regles r;
  r.classes = {{"Defaut", nm(0.2), nm(0.25)}, {"Puissance", nm(0.3), nm(1.0)}};
  r.classeReseau["+*V"] = "Puissance";
  r.classeReseau["*"] = "Defaut";
  REQUIRE(classeDe(r, "+5V"));
  CHECK(classeDe(r, "+5V")->nom == "Puissance");
  CHECK(classeDe(r, "SDA")->nom == "Defaut");
  // Ordre inverse : « * » prend tout, « +*V » est masqué et signalé
  Regles inverse = r;
  inverse.classeReseau.deplacer("*", 0);
  CHECK(classeDe(inverse, "+5V")->nom == "Defaut");
  ConceptionElec c = exempleElectronique();
  c.circuit.regles = inverse;
  const Connectivite k = connectivite(c);
  const auto d = diagnosticAffectations(inverse, k);
  CHECK(std::any_of(d.begin(), d.end(), [](const std::string& m) { return m.find("+*V") != std::string::npos && m.find("masqué") != std::string::npos; }));
  // L'ordre est gardé dans le fichier
  c.circuit.regles = r;
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  REQUIRE(relue.circuit.regles.classeReseau.size() == 2);
  CHECK(relue.circuit.regles.classeReseau.begin()->first == "+*V");
}

TEST_CASE("R10 : une affectation vers une classe supprimée laisse les motifs s'appliquer et se signale", "[L0][R10]") {
  Regles r;
  r.classes = {{"Puissance", nm(0.3), nm(1.0)}};
  r.classeReseau["+5V"] = "Supprimee";
  r.classeReseau["+*V"] = "Puissance";
  REQUIRE(classeDe(r, "+5V"));
  CHECK(classeDe(r, "+5V")->nom == "Puissance");
  const auto d = diagnosticAffectations(r, Connectivite{});
  CHECK(std::any_of(d.begin(), d.end(), [](const std::string& m) { return m.find("Supprimee") != std::string::npos && m.find("n’existe plus") != std::string::npos; }));
}

TEST_CASE("R11 : deux affectations qui visent le même réseau après un renommage : conflit dit, la plus stricte gardée", "[L0][R11]") {
  ConceptionElec c = nouvelleConception("R11");
  const std::string r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
  annoter(c, false);
  Pt p1;
  for (const auto& b : brochesPlacees(c.feuilles[0].symboles.front(), *c.symbole("R")))
    if (b.numero == "1") p1 = b.p;
  Etiquette e;
  e.uuid = nouvelUuid();
  e.texte = "ANCIEN";
  e.p = p1;
  c.feuilles[0].etiquettes.push_back(e);
  synchroniserReseaux(c);  // fige « ANCIEN » sur la broche
  c.feuilles[0].etiquettes.front().texte = "NOUVEAU";
  c.circuit.regles.classes = {{"Large", nm(0.5), nm(0.5)}, {"Fine", nm(0.15), nm(0.15)}};
  c.circuit.regles.classeReseau["NOUVEAU"] = "Fine";
  c.circuit.regles.classeReseau["ANCIEN"] = "Large";
  const Connectivite k = connectivite(c);
  CHECK(k.canonique("ANCIEN") == "NOUVEAU");
  const auto d = diagnosticAffectations(c.circuit.regles, k);
  CHECK(std::any_of(d.begin(), d.end(), [](const std::string& m) { return m.find("NOUVEAU") != std::string::npos && m.find("Large") != std::string::npos; }));
  std::vector<std::string> conflits;
  synchroniserReseaux(c, &conflits);
  REQUIRE(conflits.size() == 1);
  CHECK_THAT(conflits.front(), ContainsSubstring("Large"));
  CHECK(c.circuit.regles.classeReseau.size() == 1);
  CHECK(c.circuit.regles.classeReseau.find("NOUVEAU")->second == "Large");
  (void)r1;
}

TEST_CASE("F4 : le noyau refuse toute modification d'une conception publiée", "[L0][F4]") {
  ConceptionElec c = exempleElectronique();
  c.verrouillee = true;
  const std::string avant = versJson(c).dump();
  CHECK_THROWS_AS(routerSimple(c), ConceptionVerrouillee);
  CHECK_THROWS_AS(mettreAJourCircuit(c), ConceptionVerrouillee);
  CHECK_THROWS_AS(annoter(c, true), ConceptionVerrouillee);
  CHECK_THROWS_AS(poserSymbole(c, 0, "R", ptMm(10, 10)), ConceptionVerrouillee);
  CHECK_THROWS_AS(synchroniserReseaux(c), ConceptionVerrouillee);
  CHECK_THROWS_AS(figerEmpreintes(c), ConceptionVerrouillee);
  CHECK_THROWS_AS(transformerSelectionSchema(c, 0, {c.feuilles[0].symboles.front().uuid}, OperationSchema::Tourner), ConceptionVerrouillee);
  CHECK(versJson(c).dump() == avant);
}

TEST_CASE("F8 : une publication refusée ne touche pas la conception", "[L0][F8]") {
  ConceptionElec c = exempleElectronique();
  c.empreintes.clear();
  c.cartouche.auteur.clear();
  c.circuit.pistes.clear();  // liaisons non routées : erreur critique
  const std::string avant = versJson(c).dump();
  CHECK_THROWS(publier(c, dossier("f8"), "Evan", "2026-10-17", "raison", "essai"));
  CHECK(versJson(c).dump() == avant);
  CHECK(c.empreintes.empty());
}

// ---------------------------------------------------------------------------------------------------------
// Empreintes et brochages
TEST_CASE("B6 : aucun identifiant approximatif ne donne une autre géométrie", "[L0][B6]") {
  // Cinquante noms pièges : chacun est refusé, ou donne exactement ce qu'il nomme
  const std::vector<std::string> pieges = {
      "PinHeader_1x04_P2.00", "PinHeader_1x04_P2.00mm", "PinHeader_1x04_P2.54mm_Vertical", "PinHeader_1x04_P2.54mm_Vertical_SMD", "PinHeader_1x04_P2.54mm_Horizontal",
      "PinHeader_1x04_P1.27", "PinHeader_2x05_P2.00", "PinHeader_3x04_P2.54", "PinHeader_1x41_P2.54", "PinHeader_1x04_P2.54xyz",
      "TerminalBlock_1x03_P3.50", "TerminalBlock_1x03_P5.00", "TerminalBlock_1x03_P5.08mm", "TerminalBlock_2x03_P5.08", "TerminalBlock_1x03_P5.08_Horizontal",
      "DIP-40_W12.70mm", "DIP-8_W7.62", "DIP-8x", "DIP-7", "DIP-66",
      "MountingHole_3.2mm_M3_Pad_Via", "MountingHole_3.2mm_M3", "MountingHole_3.2mm", "MountingHole_3.2_Pad_Via", "MountingHole_0.2",
      "R_0603_HandSolder", "R_0603_1608Metric", "R_0603x", "R_0604", "C_0805_2012Metric_Pad1.18x1.45mm_HandSolder",
      "LED_0805_HandSolder", "SOIC-8_3.9x4.9mm_P1.27mm", "SOIC-08", "SOIC-8x", "SOIC-7",
      "TSSOP-8x", "QFN-32-5x5-0.5xyz", "QFN-32-5x5-0.5mm", "QFN-32-5x4-0.5", "QFN-30-5x5-0.5",
      "TQFP-44-10x10-0.8mm", "TQFP-44-10x10", "CP_Radial_D5.0_P2.00mm", "CP_Radial_D5.0_P6.00", "CP_Radial_D5.0",
      "SOT-23-3", "SOT-223-3_TabPin2", "TO-220-3_Vertical", "D_SOD-123F", "SW_PUSH_6mm_H5mm"};
  REQUIRE(pieges.size() == 50);
  for (const auto& id : pieges) {
    INFO(id);
    CHECK_FALSE(genererEmpreinte(id));
  }
  // Ce qui est accepté donne ce que le nom dit : DIP-40 à 15,24 mm entre rangées
  const auto dip = genererEmpreinte("DIP-40_W15.24mm");
  REQUIRE(dip);
  Nm xmin = 0, xmax = 0;
  for (const auto& p : dip->pastilles) { xmin = std::min(xmin, p.position.x); xmax = std::max(xmax, p.position.x); }
  CHECK(xmax - xmin == nm(15.24));
  // Le catalogue reste entier
  for (const auto& i : catalogueEmpreintes()) CHECK(genererEmpreinte(i.id));
  // Import de symboles KiCad : une empreinte au pas de 2,00 mm n'est jamais associée à la barrette de 2,54 mm
  const std::string sym = R"((kicad_symbol_lib (version 20231120) (generator "essai")
  (symbol "Barrette4" (in_bom yes) (on_board yes)
    (property "Reference" "J" (at 0 0 0)) (property "Value" "Barrette4" (at 0 0 0))
    (property "Footprint" "Connector_PinHeader_2.00mm:PinHeader_1x04_P2.00mm_Vertical" (at 0 0 0))
    (symbol "Barrette4_1_1" (pin passive line (at 0 0 0) (length 2.54) (name "1" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))))
  (symbol "Barrette4b" (in_bom yes) (on_board yes)
    (property "Reference" "J" (at 0 0 0)) (property "Value" "Barrette4b" (at 0 0 0))
    (property "Footprint" "Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical" (at 0 0 0))
    (symbol "Barrette4b_1_1" (pin passive line (at 0 0 0) (length 2.54) (name "1" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))))
))";
  const auto syms = symbolesDepuisKicadSym(sym);
  REQUIRE(syms.size() == 2);
  CHECK(syms[0].empreinte == "kicad:Connector_PinHeader_2.00mm:PinHeader_1x04_P2.00mm_Vertical");
  CHECK(syms[1].empreinte == "PinHeader_1x04_P2.54");
}

TEST_CASE("B20 : le générateur a des bornes et refuse les géométries impossibles", "[L0][B20]") {
  for (const char* id : {"SOIC-2000", "SOIC-66", "QFN-64-3x3-0.5", "QFN-8-0.5x0.5-0.5", "TQFP-404-10x10-0.5", "QFN-32-200x200-0.5", "QFN-32-5x5-0.2", "TQFP-32-7x7-12"}) {
    INFO(id);
    CHECK_FALSE(genererEmpreinte(id));
  }
  const auto q = genererEmpreinte("QFN-32-5x5-0.5");
  REQUIRE(q);
  CHECK(q->pastilles.back().largeur > nm(0.2));  // pastille centrale de taille réelle
}

TEST_CASE("B14 : la sérigraphie de tout le catalogue reste à 0,15 mm du cuivre", "[L0][B14]") {
  // Mesure directe, indépendante de la DRC-13 (essayée empreinte par empreinte dans PCB-03, Q6) : le bord de chaque
  // trait est à 0,15 mm au moins de chaque pastille et de chaque perçage non métallisé
  constexpr double ECART = 150'000, TOLERANCE = 5'000;  // facettes des arrondis
  int empreintes = 0;
  for (const auto& info : catalogueEmpreintes()) {
    const auto d = genererEmpreinte(info.id);
    REQUIRE(d);
    ++empreintes;
    for (const auto& t : d->traits) {
      if (t.couche != Couche::SerigraphieDessus && t.couche != Couche::SerigraphieDessous) continue;
      std::vector<std::pair<Pt, Pt>> morceaux;
      if (t.rayon > 0) {
        // Cercle : 64 cordes
        auto point = [&](int k) {
          const double a = 2 * std::numbers::pi * k / 64;
          return Pt{t.a.x + static_cast<Nm>(std::llround(static_cast<double>(t.rayon) * std::cos(a))), t.a.y + static_cast<Nm>(std::llround(static_cast<double>(t.rayon) * std::sin(a)))};
        };
        for (int k = 0; k < 64; ++k) morceaux.emplace_back(point(k), point(k + 1));
      } else
        morceaux.emplace_back(t.a, t.b);
      for (const auto& p : d->pastilles) {
        const Polygone cuivre = p.type == TypePastille::NonMetallisee ? cercle(p.position, p.percage / 2) : formePastille(p);
        for (const auto& [a, b] : morceaux) {
          const double ecart = distancePolygoneSegment(cuivre, a, b) - static_cast<double>(t.epaisseur) / 2;
          if (ecart < ECART - TOLERANCE) FAIL_CHECK(info.id + " : sérigraphie à " + std::to_string(ecart * 1e-6) + " mm de la pastille " + p.numero);
        }
      }
    }
  }
  CHECK(empreintes >= 100);
  // Le rognage garde de la sérigraphie : un DIP garde son cadre, entre ses rangées
  const auto d = genererEmpreinte("DIP-8");
  REQUIRE(d);
  CHECK(std::count_if(d->traits.begin(), d->traits.end(), [](const TraitEmpreinte& t) { return t.couche == Couche::SerigraphieDessus; }) >= 4);
}

TEST_CASE("B15 : le perçage d'une empreinte personnelle est borné par la largeur d'une pastille ronde", "[L0][B15]") {
  SpecEmpreinte s;
  s.forme = FormePastille::Rond;
  s.largeur = 2.0;
  s.hauteur = 0.5;  // sans objet pour une pastille ronde
  s.percage = 1.0;
  CHECK(verifierSpecEmpreinte(s).empty());
  const DefEmpreinte d = genererEmpreinteSpec(s);
  for (const auto& p : d.pastilles) CHECK(p.percage == nm(1.0));
  // Hors bornes : refusé avec la raison, jamais ramené en silence
  s.percage = 2.0;
  CHECK_THAT(verifierSpecEmpreinte(s), ContainsSubstring("plus petit que la pastille"));
  s.percage = 1.0;
  CHECK_THAT(verifierSpecEmpreinte(s, nm(0.6)), ContainsSubstring("Couronne"));
  // Pastille minuscule : aucun comportement indéfini
  s.largeur = 0.15;
  s.percage = 0.1;
  CHECK_FALSE(verifierSpecEmpreinte(s).empty());
  CHECK_NOTHROW(genererEmpreinteSpec(s));
}

TEST_CASE("B17 : un MOSFET passé du SOT-23 au TO-220 sans brochage vérifié bloque la publication", "[L0][B17]") {
  ConceptionElec c = nouvelleConception("B17");
  poserSymbole(c, 0, "Q_NMOS", ptMm(50.8, 50.8));
  poserSymbole(c, 0, "NE555", ptMm(101.6, 50.8));
  annoter(c, false);
  for (auto& x : c.composants) {
    if (x.symbole == "Q_NMOS") x.empreinte = "TO-220-3";
    if (x.symbole == "NE555") x.empreinte = "DIP-8";  // même famille DIL, même numérotation : rien à dire
  }
  auto erc = verifierERC(c, connectivite(c));
  CHECK(compter(erc, "ERC-09") == 1);
  CHECK_FALSE(derogeable("ERC-09"));
  for (const auto& v : erc)
    if (v.code == "ERC-09") CHECK(v.gravite == Gravite::Erreur);
  // Brochage vérifié pour cette empreinte : levé ; une autre empreinte le remet en cause
  for (auto& x : c.composants)
    if (x.symbole == "Q_NMOS") x.brochageVerifie = "TO-220-3";
  CHECK(compter(verifierERC(c, connectivite(c)), "ERC-09") == 0);
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  CHECK(compter(verifierERC(relue, connectivite(relue)), "ERC-09") == 0);
  CHECK(familleBoitier("SOT-23") != familleBoitier("TO-220-3"));
  CHECK(familleBoitier("kicad:Package_SO:SOIC-8_3.9x4.9mm_P1.27mm") == familleBoitier("DIP-8"));
}

// ---------------------------------------------------------------------------------------------------------
// Dossier de fabrication
TEST_CASE("F1 / F11 : chaque ligne IPC-D-356 suit le gabarit de la norme, vias comprises", "[L0][F1][F11]") {
  ConceptionElec c = exempleElectronique();
  c.circuit.vias.push_back({nouvelUuid(), "GND", ptMm(10, 10), nm(12), nm(0.4)});  // via géante : taille plafonnée
  for (const bool couvertes : {true, false}) {
    c.circuit.fabrication.viasCouvertes = couvertes;
    const std::string t = ipcD356(c, connectivite(c));
    std::istringstream lignes(t);
    std::string l;
    int vias = 0, pastilles = 0;
    bool plafond = false;
    while (std::getline(lignes, l)) {
      if (l.rfind("C  ", 0) == 0 && l.find("plafonne") != std::string::npos) plafond = true;
      if (l.rfind("317", 0) != 0 && l.rfind("327", 0) != 0) continue;
      INFO(l);
      REQUIRE(l.size() == 74);
      CHECK(l[26] == '-');
      CHECK(l[41] == 'X');
      CHECK(l[49] == 'Y');
      CHECK(l[57] == 'X');
      CHECK(l[62] == 'Y');
      CHECK(l[67] == 'R');
      CHECK(l[72] == 'S');
      if (l.compare(20, 3, "VIA") == 0) {
        ++vias;
        CHECK(l[31] == 'M');  // milieu de réseau
        CHECK(l[32] == 'D');
        CHECK(l[37] == 'P');
        CHECK(l[73] == (couvertes ? '3' : '0'));
      } else {
        ++pastilles;
        // Vernis : faces où il couvre la pastille ; une CMS du dessus se sonde par-dessus (S2), une traversante des
        // deux côtés (S0)
        if (l.rfind("327", 0) == 0) CHECK(l.substr(38, 3) + l.substr(72, 2) == "A01S2");
        else CHECK(l.substr(38, 3) + l.substr(72, 2) == "A00S0");
      }
    }
    CHECK(vias == static_cast<int>(c.circuit.vias.size()));
    CHECK(pastilles > 0);
    CHECK(plafond);
  }
}

TEST_CASE("F2 : les fenêtres de pâte d'une grande pastille ronde restent dans le cuivre", "[L0][F2]") {
  SpecEmpreinte s;
  s.nom = "Rond3";
  s.type = TypePastille::CMS;
  s.forme = FormePastille::Rond;
  s.rangees = 1;
  s.colonnes = 1;
  s.largeur = 3.0;
  ConceptionElec c = carte();
  DefEmpreinte d = genererEmpreinteSpec(s);
  c.empreintes[d.id] = d;
  EmpreintePlacee e;
  e.composant = nouvelUuid();
  e.empreinte = d.id;
  e.position = ptMm(30, 20);
  c.circuit.empreintes.push_back(e);
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  const Polygone cuivre = cercle(ptMm(30, 20), nm(1.5));
  double pate = 0;
  int fenetres = 0;
  for (const auto& cf : m.couches) {
    if (cf.couche != Couche::PateDessus) continue;
    for (const auto& p : cf.primitives) {
      REQUIRE(p.type == PrimitiveFab::Type::Region);
      ++fenetres;
      CHECK(aireMm2(soustraire({p.region}, gonfler({cuivre}, 2'000))) < 1e-6);
      pate += aireMm2(p.region);
    }
  }
  CHECK(fenetres >= 4);
  CHECK(pate / aireMm2(cuivre) == Catch::Approx(c.circuit.fabrication.tauxPateGrandes).margin(0.05));
}

TEST_CASE("F3 : le placement donne le centre des pastilles d'un traversant venu de KiCad", "[L0][F3]") {
  ConceptionElec c = carte();
  DefEmpreinte d;
  d.id = "kicad:R_Axial_DIN0207";
  Pastille p1, p2;
  p1.numero = "1";
  p2.numero = "2";
  p1.type = p2.type = TypePastille::Traversante;
  p1.forme = p2.forme = FormePastille::Rond;
  p1.largeur = p1.hauteur = p2.largeur = p2.hauteur = nm(1.6);
  p1.percage = p2.percage = nm(0.8);
  p1.position = {0, 0};  // origine à la broche 1, comme KiCad
  p2.position = ptMm(10.16, 0);
  d.pastilles = {p1, p2};
  d.encombrement = Boite{-nm(1), -nm(1), nm(11.16), nm(1)};
  c.empreintes[d.id] = d;
  ComposantElec x;
  x.uuid = nouvelUuid();
  x.reference = "R1";
  x.valeur = "10k";
  x.empreinte = d.id;
  c.composants.push_back(x);
  EmpreintePlacee e;
  e.composant = x.uuid;
  e.empreinte = d.id;
  e.position = ptMm(20, 20);
  c.circuit.empreintes.push_back(e);
  const std::string pl = csvPlacement(c, true);
  // Origine au coin bas gauche (0, 40) : centre des pastilles à (25,08 ; 20)
  CHECK_THAT(pl, ContainsSubstring("\"R1\",\"10k\",\"kicad:R_Axial_DIN0207\",25.0800,20.0000,0.0,top"));
}

TEST_CASE("F9 / F10 / F14 : simple face non métallisée, %TO.P avec le nom de broche, hors carte compté pareil", "[L0][F9][F10][F14]") {
  ConceptionElec c = exempleElectronique();
  {
    const Connectivite k = connectivite(c);
    const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
    std::string tout;
    for (const auto& cf : m.couches)
      if (estCuivre(cf.couche)) tout += gerber(m, cf.couche, "essai");
    CHECK_THAT(tout, ContainsSubstring("%TO.P,U1,8,VCC*%"));
    CHECK(tout.find("%TO.P,U1,8,N-") == std::string::npos);
  }
  {
    ConceptionElec s = c;
    s.circuit.empilage.couchesCuivre = 1;
    const Connectivite k = connectivite(s);
    const ModeleFabrication m = modeleFabrication(s, k, remplirZones(s, k));
    CHECK(std::none_of(m.trous.begin(), m.trous.end(), [](const TrouFab& t) { return t.metallise; }));
    CHECK_THAT(excellon(m, false, "essai"), ContainsSubstring("NonPlated,1,1,NPTH"));
  }
  {
    ConceptionElec h = c;
    h.composants.front().horsCarte = true;  // déclaré hors carte, encore posé
    const std::string d = dossier("f14");
    ecrireDossierFabrication(h, d, "essai");
    for (const auto& defaut : controlerDossierFabrication(h, d)) {
      INFO(defaut);
      CHECK(defaut.find("placement") == std::string::npos);
    }
  }
}

// ---------------------------------------------------------------------------------------------------------
// Import KiCad, textes, attributs
namespace {
const char* CARTE_KICAD = R"((kicad_pcb (version 20221018) (generator essai)
  (general (thickness 1.6))
  (layers (0 "F.Cu" signal) (31 "B.Cu" signal) (36 "B.SilkS" user) (37 "F.SilkS" user) (38 "B.Mask" user) (39 "F.Mask" user) (44 "Edge.Cuts" user))
  (net 0 "") (net 1 "GND")
  (footprint "Essai:Tranche" (layer "F.Cu") (at 50 30 90)
    (property "Reference" "J1") (property "Value" "Tranche")
    (attr smd exclude_from_pos_files exclude_from_bom board_only)
    (pad "1" smd rect (at 0 0) (size 3 0.5) (layers "F.Cu" "F.Mask") (net 1 "GND"))
    (pad "2" smd rect (at 0 4 90) (size 3 0.5) (layers "B.Cu" "B.Mask") (net 1 "GND")))
  (gr_text "AB" (at 20 20 90) (layer "F.SilkS") (effects (font (size 2 2) (thickness 0.3))))
  (gr_rect (start 0 0) (end 100 60) (layer "Edge.Cuts") (stroke (width 0.1) (type solid)))
))";
}  // namespace

TEST_CASE("X1 / X2 / X7 : angle des pastilles, couches des pastilles et attributs repris de KiCad", "[L0][X1][X2][X7]") {
  RapportImportKicad r;
  const ConceptionElec c = conceptionDepuisKicad(CARTE_KICAD, "essai", &r);
  REQUIRE(c.circuit.empreintes.size() == 1);
  const auto pp = pastilles(c, connectivite(c));
  REQUIRE(pp.size() == 2);
  for (const auto& p : pp) {
    const Boite b = boite(p.forme);
    if (p.numero == "1") {
      // X1 : sans angle écrit, la pastille garde l'angle 0 : 3 mm le long de X sur la carte
      CHECK(b.largeur() == Catch::Approx(nm(3)).margin(2'000));
      CHECK(b.hauteur() == Catch::Approx(nm(0.5)).margin(2'000));
      // X2 : vernis ouvert, pas de pâte sur cette pastille
      CHECK((p.couches & bit(Couche::CuivreDessus)));
      CHECK((p.couches & bit(Couche::VernisDessus)));
      CHECK_FALSE((p.couches & bit(Couche::PateDessus)));
    } else {
      // X2 : CMS sur B.Cu dans une empreinte du dessus, vernis ouvert dessous
      CHECK((p.couches & bit(Couche::CuivreDessous)));
      CHECK((p.couches & bit(Couche::VernisDessous)));
      CHECK_FALSE((p.couches & bit(Couche::CuivreDessus)));
      CHECK(b.largeur() == Catch::Approx(nm(0.5)).margin(2'000));
    }
  }
  // X7 : hors nomenclature, hors placement, sans symbole de liaison
  const ComposantElec& j = c.composants.front();
  CHECK(j.horsNomenclature);
  CHECK(j.horsPlacement);
  CHECK(j.symbole.empty());
  CHECK(csvPlacement(c).find("J1") == std::string::npos);
  // Relu à l'identique après un aller-retour dans le fichier du projet
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  CHECK(versJson(relue).dump() == versJson(c).dump());
}

TEST_CASE("X3 : un texte à +90° tourne dans le même sens qu'une empreinte à +90°", "[L0][X3]") {
  // Empreinte à +90° : le point (1, 0) de l'empreinte monte à l'écran (Y diminue)
  EmpreintePlacee e;
  e.rotation = 90;
  CHECK(versCarte(e, ptMm(1, 0)).y < 0);
  // Texte à +90° : exactement le texte à 0° tourné comme l'empreinte, trait par trait
  const auto droit = traitsTexte("ABC", ptMm(0, 0), nm(2), 0), tourne = traitsTexte("ABC", ptMm(0, 0), nm(2), 90);
  REQUIRE(droit.size() == tourne.size());
  REQUIRE_FALSE(droit.empty());
  for (std::size_t i = 0; i < droit.size(); ++i) {
    CHECK(distance(versCarte(e, droit[i].a), tourne[i].a) < 3.0);
    CHECK(distance(versCarte(e, droit[i].b), tourne[i].b) < 3.0);
  }
  // Un texte de carte KiCad à 90° arrive dans le même sens
  const ConceptionElec c = conceptionDepuisKicad(CARTE_KICAD, "essai");
  REQUIRE(c.circuit.textes.size() == 1);
  CHECK(c.circuit.textes.front().rotation == Catch::Approx(90));
  // Un projet de la 0.28 (format 1) garde le dessin de ses textes tournés : l'angle change de signe à la lecture
  auto j = versJson(c);
  j.erase("format");
  j.erase("formatElectronique");
  std::vector<std::string> alertes;
  const ConceptionElec ancien = conceptionDepuisJson(j, &alertes);
  CHECK(ancien.circuit.textes.front().rotation == Catch::Approx(-90));
  CHECK_FALSE(alertes.empty());
}

TEST_CASE("P4 / T2 : la hauteur saisie d'un composant donne le corps en 3D et dans le boîtier", "[L0][P4][T2]") {
  ConceptionElec c = exempleElectronique();
  ComposantElec* x = nullptr;
  for (auto& k : c.composants)
    if (k.empreinte.rfind("C_", 0) == 0) x = &k;
  REQUIRE(x);
  const DefEmpreinte* d = c.empreinte(x->empreinte);
  REQUIRE(d);
  CHECK(hauteurComposant(x, d).mm == Catch::Approx(mm(d->corps.hauteur)));  // hauteurComposant() depuis la 0.32 (3.4)
  x->hauteur = 12;
  CHECK(hauteurComposant(x, d).mm == Catch::Approx(12));
  // Empreinte sans corps : une hauteur saisie lui en donne un, à son encombrement
  const auto point = genererEmpreinte("TestPoint_Pad_1.5");
  REQUIRE(point);
  CHECK(point->corps.hauteur == 0);
  ComposantElec tp;
  tp.hauteur = 3;
  CHECK(hauteurComposant(&tp, &*point).mm == Catch::Approx(3));
  CHECK(empriseCorps(*point).largeur() > 0);
}
