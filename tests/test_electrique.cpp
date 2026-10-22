// Prométhée : essais de la CAO électrique (symboles CEI, pose et numéros de bornes, bornes de bornier, renvois,
// numéros de fils, repérage, vérifications, rapports, calculs, logement NF C 15-100, armoire, faisceau, import
// QElectroTech, enregistrement).
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "promethee/electrique.hpp"

using namespace prom;
using namespace prom::elec;
using Catch::Approx;

namespace {

ecad::Pt P(double x, double y) { return ecad::ptMm(x * 2.54, y * 2.54); }

const ecad::SymbolePlace* pose(const ProjetElectrique& p, const std::string& uuid) {
  for (const auto& f : p.schema.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == uuid) return &s;
  return nullptr;
}

std::vector<std::string> numeros(const ProjetElectrique& p, const std::string& uuid) {
  std::vector<std::string> v;
  const ecad::SymbolePlace* s = pose(p, uuid);
  if (!s) return v;
  const ecad::DefSymbole* d = p.schema.symbole(s->symbole);
  for (const auto& b : ecad::brochesPlacees(*s, *d)) v.push_back(b.numero);
  return v;
}

const ecad::ComposantElec* parRepere(const ProjetElectrique& p, const std::string& ref) { return p.schema.parReference(ref); }

int compte(const std::vector<ecad::Violation>& v, const std::string& code) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const ecad::Violation& x) { return x.code == code; }));
}

int erreurs(const std::vector<ecad::Violation>& v) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [](const ecad::Violation& x) { return x.gravite == ecad::Gravite::Erreur; }));
}

}  // namespace

TEST_CASE("Électrique : bibliothèque CEI 60617, catalogue et lettres de repère") {
  const auto& b = bibliothequeElectrique();
  REQUIRE(b.size() >= 70);
  std::set<std::string> ids;
  for (const auto& d : b) {
    INFO(d.id);
    CHECK(ids.insert(d.id).second);
    CHECK(d.id.rfind("iec:", 0) == 0);
    CHECK_FALSE(d.broches.empty());
    CHECK(std::find(famillesElectriques().begin(), famillesElectriques().end(), d.categorie) != famillesElectriques().end());
    // Bornes sur la grille du schéma : les fils s'y accrochent
    for (const auto& br : d.broches) {
      CHECK(br.extremite.x % 1'270'000 == 0);
      CHECK(br.extremite.y % 1'270'000 == 0);
    }
  }
  for (const auto& e : catalogueElectrique()) {
    INFO(e.id);
    CHECK(symboleElectrique(e.symbole) != nullptr);
  }
  CHECK(prefixeRepere("iec:bobine-km", NormeRepere::Classique) == "KM");
  CHECK(prefixeRepere("iec:bobine-km", NormeRepere::Cei81346) == "QA");
  CHECK(prefixeRepere("iec:bp-no", NormeRepere::Cei81346) == "SF");
  CHECK(prefixeRepere("iec:moteur-3", NormeRepere::Classique) == "M");
  CHECK(roleSymbole("iec:contact-no") == RoleSymbole::ContactNO);
  CHECK(roleSymbole("iec:borne") == RoleSymbole::Borne);
  CHECK(roleSymbole("iec:bobine-km") == RoleSymbole::Principal);
}

TEST_CASE("Électrique : un contacteur et ses contacts reçoivent les numéros de la CEI 60947") {
  ProjetElectrique p = nouveauProjetElectrique("Essai", true);
  REQUIRE(p.schema.feuilles.size() == 3);
  CHECK(p.folio(p.schema.feuilles[1].uuid).type == TypeFolio::Puissance);
  const std::string s = poserAppareil(p, 2, "iec:bobine-km", P(20, 80), 0, "km-9-24dc");
  const std::string km = pose(p, s)->composant;  // UUID : les pointeurs vers les appareils bougent quand on en ajoute
  REQUIRE(p.schema.composant(km));
  CHECK(p.schema.composant(km)->reference == "KM1");
  CHECK(p.schema.composant(km)->champs.at("catalogue") == "km-9-24dc");
  CHECK(repereAffiche(p, *p.schema.composant(km)) == "-KM1");
  const std::string c1 = poserElement(p, 2, "iec:contact-no", P(30, 40), 0, km);
  const std::string c2 = poserElement(p, 2, "iec:contact-no", P(40, 40), 0, km);
  const std::string c3 = poserElement(p, 2, "iec:contact-nf", P(50, 40), 0, km);
  const std::string pl = poserElement(p, 1, "iec:poles-km-3p", P(30, 40), 0, km);
  CHECK(numeros(p, c1) == std::vector<std::string>{"13", "14"});
  CHECK(numeros(p, c2) == std::vector<std::string>{"23", "24"});
  CHECK(numeros(p, c3) == std::vector<std::string>{"31", "32"});
  CHECK(numeros(p, pl) == std::vector<std::string>{"1", "2", "3", "4", "5", "6"});
  // Un deuxième contacteur repart de 13-14
  const std::string s2 = poserAppareil(p, 2, "iec:bobine-km", P(60, 80), 0, "km-9-24dc");
  const std::string km2 = pose(p, s2)->composant;
  CHECK(p.schema.composant(km2)->reference == "KM2");
  CHECK(numeros(p, poserElement(p, 2, "iec:contact-no", P(70, 40), 0, km2)) == std::vector<std::string>{"13", "14"});
  // Relais thermique : 95-96 et 97-98
  const std::string rt = poserAppareil(p, 1, "iec:relais-thermique-3p", P(30, 60), 0, "rt-6");
  const std::string rtc = poserElement(p, 2, "iec:contact-thermique-nf", P(80, 40), 0, pose(p, rt)->composant);
  CHECK(numeros(p, rtc) == std::vector<std::string>{"95", "96"});
  // Appareils auxquels un contact peut se rattacher : les bobines d'abord
  const auto liste = appareilsPour(p, RoleSymbole::ContactNO);
  REQUIRE(liste.size() >= 3);
  CHECK(liste[0] == km);
  CHECK(liste[1] == km2);
  // Un contact posé sans appareil n'en crée pas : la vérification le signale
  const std::string seul = poserAppareil(p, 2, "iec:contact-no", P(90, 40), 0);
  CHECK(pose(p, seul)->composant.empty());
  CHECK(compte(verifierElectrique(p), "ELE-01") == 1);
  // Renvois : la bobine porte la liste de ses contacts, chacun à sa place folio.colonne
  const Renvois r = renvois(p);
  const auto& el = r.parComposant.at(km);
  REQUIRE(el.size() == 5);
  CHECK(el.front().role == RoleSymbole::Principal);
  CHECK(el.front().ou.folio == 3);
  bool poles = false;
  for (const auto& e : el)
    if (e.role == RoleSymbole::PolesPuissance) {
      poles = true;
      CHECK(e.ou.folio == 2);
      CHECK(e.bornes == "1-2 3-4 5-6");
    }
  CHECK(poles);
}

TEST_CASE("Électrique : bornes de bornier numérotées, plan des borniers et nomenclature") {
  const ProjetElectrique p = exempleDemarrageMoteur();
  const ecad::ComposantElec* x1 = parRepere(p, "X1");
  REQUIRE(x1);
  const auto plan = planBorniers(p);
  REQUIRE(plan.size() == 4);
  CHECK(plan[0].bornier == "-X1");
  CHECK(plan[0].borne == "1");
  CHECK(plan[3].borne == "4");
  CHECK(plan[3].protection);
  // Côté armoire : le relais thermique ; côté machine : le moteur ; le câble W1 porte le fil
  CHECK(plan[0].interieur.find("-F1:2") != std::string::npos);
  CHECK(plan[0].exterieur.find("-M1:U1") != std::string::npos);
  CHECK_FALSE(plan[0].fil.empty());
  CHECK(plan[0].cable == "W1:BN");
  CHECK(plan[3].fil == "PE");
  CHECK(plan[3].cable == "W1:GNYE");
  const auto nomenclature = nomenclatureElectrique(p);
  int bornes = 0, bornesPE = 0;
  for (const auto& l : nomenclature) {
    if (l.designation.find("Borne à vis 2,5") != std::string::npos) bornes = l.quantite;
    if (l.designation.find("Borne de terre") != std::string::npos) bornesPE = l.quantite;
  }
  CHECK(bornes == 3);
  CHECK(bornesPE == 1);
  // Un appareil par ligne, avec ses folios
  const auto appareils = listeAppareils(p);
  const auto km = std::find_if(appareils.begin(), appareils.end(), [](const LigneAppareil& a) { return a.repere == "-KM1"; });
  REQUIRE(km != appareils.end());
  CHECK(km->folios.find("2.") != std::string::npos);
  CHECK(km->folios.find("3.") != std::string::npos);
  CHECK(km->fonction == "Contacteur de la pompe");
  const std::string csv = csvBorniers(p);
  CHECK(csv.rfind("\xEF\xBB\xBF", 0) == 0);
  CHECK(csv.find("Bornier;Borne;") != std::string::npos);
  CHECK(csv.find("\r\n") != std::string::npos);
}

TEST_CASE("Électrique : numéros de fils figés, renumérotation dans l'ordre de lecture") {
  ProjetElectrique p = exempleDemarrageMoteur();
  const ecad::Connectivite k = ecad::connectivite(p.schema);
  int sansNumero = 0;
  for (const auto& r : k.reseaux)
    if (!r.nomme && r.broches.size() >= 2 && r.nom.rfind("N-", 0) == 0) ++sansNumero;
  CHECK(sansNumero == 0);
  // Le premier potentiel numéroté (lecture : folio 2, colonne 1, de haut en bas) est « 1 »
  const ecad::ComposantElec* q1 = parRepere(p, "Q1");
  REQUIRE(q1);
  CHECK(k.nom(q1->uuid, "2") == "1");
  const std::string avant = k.nom(parRepere(p, "KM1")->uuid, "A1");
  // Un appareil ajouté ne déplace aucun numéro donné
  poserAppareil(p, 2, "iec:voyant", P(100, 82), 0, "v-blanc");
  numeroterFils(p, false);
  const ecad::Connectivite k2 = ecad::connectivite(p.schema);
  CHECK(k2.nom(q1->uuid, "2") == "1");
  CHECK(k2.nom(parRepere(p, "KM1")->uuid, "A1") == avant);
  // Numérotation par folio : « 2.1 »
  p.reglages.fils = NumerotationFils::ParFolio;
  numeroterFils(p, true);
  const ecad::Connectivite k3 = ecad::connectivite(p.schema);
  CHECK(k3.nom(q1->uuid, "2") == "2.1");
  CHECK(k3.nom(parRepere(p, "KM1")->uuid, "A1").rfind("3.", 0) == 0);
  // Classes et couleurs des conducteurs
  CHECK(classeConducteur("PE") == ClasseConducteur::Protection);
  CHECK(classeConducteur("L2") == ClasseConducteur::Phase);
  CHECK(classeConducteur("24V") == ClasseConducteur::CommandeContinue);
  CHECK(proprietesEffectives(p, "PE").couleur == "GNYE");
  CHECK(proprietesEffectives(p, "N").couleur == "LBU");
  CHECK(proprietesEffectives(p, "L1").couleur == "BN");
  CHECK(proprietesEffectives(p, k3.nom(q1->uuid, "2")).section == Approx(2.5));
  CHECK(proprietesEffectives(p, k3.nom(parRepere(p, "KM1")->uuid, "A1")).couleur == "DBU");
}

TEST_CASE("Électrique : repérage dans l'ordre de lecture et selon la CEI 81346") {
  ProjetElectrique p = nouveauProjetElectrique("Repérage", true);
  const std::string b = poserAppareil(p, 2, "iec:bp-no", P(80, 40), 0);
  const std::string a = poserAppareil(p, 2, "iec:bp-no", P(20, 40), 0);
  const auto* sb = p.schema.composant(pose(p, b)->composant);
  const auto* sa = p.schema.composant(pose(p, a)->composant);
  CHECK(sb->reference == "S1");
  CHECK(sa->reference == "S2");
  // « tout » : dans l'ordre de lecture (colonne de gauche d'abord)
  CHECK(annoterElectrique(p, true) == 2);
  CHECK(sa->reference == "S1");
  CHECK(sb->reference == "S2");
  // CEI 81346 : les lettres changent à la renumérotation complète
  p.reglages.norme = NormeRepere::Cei81346;
  annoterElectrique(p, true);
  CHECK(sa->reference == "SF1");
  // Un repère importé (lettres inconnues de la bibliothèque) garde ses lettres
  p.reglages.norme = NormeRepere::Classique;
  p.schema.composant(sa->uuid)->reference = "SB?";
  annoterElectrique(p, false);
  CHECK(sa->reference == "SB1");
}

TEST_CASE("Électrique : vérifications du métier") {
  ProjetElectrique p = exempleDemarrageMoteur();
  auto v = verifierElectrique(p);
  std::string dits;
  for (const auto& x : v) dits += x.code + " " + x.message + " ; ";
  INFO(dits);
  CHECK(erreurs(v) == 0);
  CHECK(compte(v, "ELE-13") == 0);  // le moteur est protégé par le relais thermique
  CHECK(compte(v, "ELE-15") == 0);  // 95-96 est dans la commande
  CHECK(compte(v, "ELE-16") == 0);  // les pôles sont posés
  CHECK(compte(v, "ELE-07") == 0);  // chaque potentiel a son renvoi
  CHECK(compte(v, "ELE-18") == 0);  // tous les fils sont numérotés
  CHECK(compte(v, "ELE-17") == 0);  // tout a sa référence
  // Retirer le contact 95-96 : la surcharge ne couperait plus rien
  {
    ProjetElectrique q = p;
    for (auto& f : q.schema.feuilles)
      f.symboles.erase(std::remove_if(f.symboles.begin(), f.symboles.end(), [](const ecad::SymbolePlace& s) { return s.symbole == "iec:contact-thermique-nf"; }),
                       f.symboles.end());
    CHECK(compte(verifierElectrique(q), "ELE-15") == 1);
  }
  // PE d'une autre couleur, conducteur vert-jaune qui n'est pas le PE
  {
    ProjetElectrique q = p;
    q.conducteurs["PE"].couleur = "BK";
    q.conducteurs["5"].couleur = "GNYE";
    CHECK(compte(verifierElectrique(q), "ELE-08") == 2);
  }
  // Bobine 230 V alternatif dans une commande en 24 V continu
  {
    ProjetElectrique q = p;
    q.schema.composant(parRepere(q, "KM1")->uuid)->champs["catalogue"] = "km-9-230ac";
    CHECK(compte(verifierElectrique(q), "ELE-11") == 1);
  }
  // Câble dont le vert-jaune porte une phase
  {
    ProjetElectrique q = p;
    q.cables[0].affectation[0].repere = "GNYE";
    CHECK(compte(verifierElectrique(q), "ELE-10") >= 1);
  }
  // Fil trop fin derrière un disjoncteur de 32 A
  {
    ProjetElectrique q = nouveauProjetElectrique("Section", false);
    const std::string d = poserAppareil(q, 0, "iec:disj-1pn", P(40, 30), 0, "dj-1pn-c32");
    const std::string pr = poserAppareil(q, 0, "iec:prise", P(40, 60), 0, "xd-prise");
    (void)d;
    (void)pr;
    q.schema.feuilles[0].fils.push_back({ecad::nouvelUuid(), P(39, 33), P(39, 40)});
    q.schema.feuilles[0].fils.push_back({ecad::nouvelUuid(), P(39, 40), P(39, 57)});
    numeroterFils(q, true);
    const ecad::Connectivite k = ecad::connectivite(q.schema);
    q.conducteurs[k.nom(q.schema.composants[0].uuid, "2")].section = 2.5;
    CHECK(compte(verifierElectrique(q), "ELE-09") == 1);
  }
}

TEST_CASE("Électrique : carnet de câblage, entrées et sorties d'automate, folios") {
  const ProjetElectrique p = exempleDemarrageMoteur();
  const auto carnet = carnetCablage(p);
  REQUIRE_FALSE(carnet.empty());
  bool q1km1 = false;
  for (const auto& l : carnet) {
    CHECK_FALSE(l.fil.empty());
    if ((l.de == "-Q1:2" && l.vers == "-KM1:1") || (l.de == "-KM1:1" && l.vers == "-Q1:2")) q1km1 = true;
  }
  CHECK(q1km1);
  const auto folios = listeFolios(p);
  REQUIRE(folios.size() == 6);
  CHECK(folios[1].type == "Puissance");
  CHECK(folios[1].appareils >= 6);
  // Folios dessinés d'office : borniers, implantation, nomenclature ; les types se relisent par leur code
  CHECK(folios[3].type == "Borniers");
  CHECK(folios[4].type == "Implantation");
  CHECK(folios[5].type == "Nomenclature");
  for (TypeFolio t : {TypeFolio::Garde, TypeFolio::Puissance, TypeFolio::Commande, TypeFolio::Automate, TypeFolio::Distribution, TypeFolio::Unifilaire, TypeFolio::Borniers,
                      TypeFolio::Implantation, TypeFolio::Nomenclature, TypeFolio::Libre})
    CHECK(typeFolioDepuis(codeTypeFolio(t)) == t);
  CHECK(folioDessineDOffice(TypeFolio::Garde));
  CHECK(folioDessineDOffice(TypeFolio::Nomenclature));
  CHECK_FALSE(folioDessineDOffice(TypeFolio::Commande));
  // Automate : 16 entrées et sorties, câblées ou en réserve
  ProjetElectrique q = nouveauProjetElectrique("Automate", false);
  const std::string a = poserAppareil(q, 0, "iec:automate-8e8s", P(40, 40), 0, "a-8e8s");
  const ecad::ComposantElec* api = q.schema.composant(pose(q, a)->composant);
  CHECK(api->reference == "A1");
  const auto es = listeEntreesSorties(q);
  CHECK(es.size() == 16);
  CHECK(es.front().adresse == "%I0.0");
  CHECK(es.front().sens == "E");
  CHECK(es.back().sens == "S");
}

TEST_CASE("Électrique : courants admissibles, chute de tension, choix de la section, bilan") {
  CHECK(courantAdmissible(2.5, MethodePose::B1, Isolant::PVC, 2) == Approx(24));
  CHECK(courantAdmissible(1.5, MethodePose::C, Isolant::PVC, 2) == Approx(19.5));
  CHECK(courantAdmissible(10, MethodePose::E, Isolant::PR, 3) == Approx(75));
  CHECK(courantAdmissible(3, MethodePose::C, Isolant::PVC, 2) == 0);
  CHECK(facteurTemperature(Isolant::PVC, 25) == Approx(1.06));
  CHECK(facteurTemperature(Isolant::PVC, 40) == Approx(0.87));
  CHECK(facteurTemperature(Isolant::PR, 50) == Approx(0.82));
  CHECK(facteurGroupement(3) == Approx(0.70));
  CHECK(facteurGroupement(10) == Approx(0.45));
  CHECK(courantAdmissible(2.5, MethodePose::C, Isolant::PVC, 3, 40, 2) == Approx(24 * 0.87 * 0.8).margin(0.06));
  const ChuteTension ct = chuteTension(16, 30, 2.5, false, 1.0, 230, false);
  CHECK(ct.volts == Approx(8.64).margin(1e-3));  // 2 × 0,0225 × 30 / 2,5 × 16
  CHECK(ct.pourcent == Approx(3.757).margin(0.01));
  CHECK(ct.admissible);
  CHECK_FALSE(chuteTension(16, 30, 2.5, false, 1.0, 230, true).admissible);  // 3 % pour l'éclairage
  const ChoixSection s = choisirSection(25, 20, false, 0.9, MethodePose::B1, Isolant::PVC, 30, 1, false, 230);
  CHECK(s.calibre == Approx(25));
  CHECK(s.section == Approx(4));
  CHECK(s.justification.find("In 25 A") != std::string::npos);
  // Long circuit : la chute de tension impose la section
  const ChoixSection l = choisirSection(16, 120, false, 1.0, MethodePose::C, Isolant::PVC, 30, 1, false, 230);
  CHECK(l.section > 2.5);
  CHECK(l.chute.admissible);
  const BilanPuissance b = bilanPuissance(exempleDemarrageMoteur());
  REQUIRE(b.lignes.size() == 1);
  CHECK(b.installee == Approx(2.2));
  CHECK(b.absorbee == Approx(1.65));
  CHECK(b.courant == Approx(1650.0 / (std::sqrt(3.0) * 400 * 0.85)).margin(0.01));
}

TEST_CASE("Électrique : tableau de logement selon la NF C 15-100") {
  Logement l = logementType(80, 3);
  CHECK(l.pieces.size() == 8);
  const TableauLogement t = tableauLogement(l);
  CHECK(t.differentiels.size() >= 3);
  std::map<int, int> parDiff;
  bool cuissonA = false, lingeA = false;
  int eclairage = 0, prises = 0, prisesTotal = 0;
  std::set<int> diffEclairage, diffPrises;
  for (const auto& c : t.circuits) {
    ++parDiff[c.differentiel];
    const std::string type = t.differentiels.at(static_cast<std::size_t>(c.differentiel)).type;
    if (c.usage == "cuisson") {
      cuissonA = type == "A";
      CHECK(c.calibre == Approx(32));
      CHECK(c.section == Approx(6));
    }
    if (c.usage == "lave-linge") lingeA = type == "A";
    if (c.usage == "eclairage") {
      ++eclairage;
      diffEclairage.insert(c.differentiel);
      CHECK(c.points <= 8);
    }
    if (c.usage == "prises") {
      ++prises;
      prisesTotal += c.points;
      diffPrises.insert(c.differentiel);
      CHECK(c.points <= 12);
    }
  }
  for (const auto& [d, n] : parDiff) CHECK(n <= 8);
  CHECK(cuissonA);
  CHECK(lingeA);
  CHECK(eclairage >= 2);
  CHECK(diffEclairage.size() >= 2);
  CHECK(diffPrises.size() >= 2);
  // Séjour (24 m²) : 7 prises ; 3 chambres : 9 ; cuisine : 6 ; entrée : 1 ; salle d'eau : 1 ; hotte : 1
  CHECK(prisesTotal == 7 + 9 + 6 + 1 + 1 + 1);
  CHECK(t.modulesTotal - t.modulesOccupes >= 0.2 * t.modulesTotal - 1e-9);
  CHECK(verifierLogement(l, t).empty());
  // Moins de prises que la norme n'en demande dans le séjour
  Logement m = l;
  for (auto& pc : m.pieces)
    if (pc.type == Piece::Sejour) pc.prises = 3;
  const auto avis = verifierLogement(m, tableauLogement(m));
  REQUIRE_FALSE(avis.empty());
  CHECK(avis.front().exigence);
  CHECK(avis.front().regle == "Prises par pièce");
  // Projet complet : unifilaire, coffret implanté avec 20 % de réserve, sans erreur
  const ProjetElectrique p = projetLogement(l);
  REQUIRE(p.logement.has_value());
  REQUIRE(p.schema.feuilles.size() >= 4);
  CHECK(p.folio(p.schema.feuilles[1].uuid).type == TypeFolio::Unifilaire);
  CHECK(p.folio(p.schema.feuilles[p.schema.feuilles.size() - 2].uuid).type == TypeFolio::Implantation);
  CHECK(p.folio(p.schema.feuilles.back().uuid).type == TypeFolio::Nomenclature);
  const auto v = verifierElectrique(p);
  for (const auto& x : v) INFO(x.code + " " + x.message);
  CHECK(erreurs(v) == 0);
  REQUIRE(p.armoires.size() == 1);
  CHECK(p.armoires[0].tableauModulaire);
  const BilanArmoire ba = bilanArmoire(p, 0);
  CHECK(ba.horsPlaque == 0);
  CHECK(ba.reserve >= 0.2 - 1e-9);
  int disjoncteurs = 0;
  for (const auto& c : p.schema.composants) disjoncteurs += c.symbole == "iec:u-disj" ? 1 : 0;
  CHECK(disjoncteurs == static_cast<int>(t.circuits.size()));
}

TEST_CASE("Électrique : armoire implantée, plan de perçage, faisceau") {
  ProjetElectrique p = exempleDemarrageMoteur();
  REQUIRE(p.armoires.size() == 1);
  const BilanArmoire b = bilanArmoire(p, 0);
  CHECK(b.horsPlaque == 0);
  CHECK(b.tauxRemplissage > 0);
  CHECK(b.tauxRemplissage < 1);
  int porte = 0, plaque = 0;
  for (const auto& im : p.armoires[0].implantations) (im.porte ? porte : plaque) += 1;
  CHECK(porte == 4);   // S1, S2, H1, H2
  CHECK(plaque == 6);  // Q1, Q2, G1, KM1, F1, X1
  // Le relais thermique suit son contacteur
  const auto ordre = appareilsArmoire(p, p.armoires[0]);
  const auto ikm = std::find(ordre.begin(), ordre.end(), parRepere(p, "KM1")->uuid);
  REQUIRE(ikm != ordre.end());
  REQUIRE(ikm + 1 != ordre.end());
  CHECK(*(ikm + 1) == parRepere(p, "F1")->uuid);
  const std::string dxf = dxfPlaque(p, 0);
  CHECK(dxf.find("AC1009") != std::string::npos);
  CHECK(dxf.find("PERCAGES") != std::string::npos);
  CHECK(dxf.find("-KM1") != std::string::npos);
  CHECK(dxf.substr(dxf.size() - 4) == "EOF\n");
  // Faisceau : deux connecteurs reliés par deux branches de 300 et 200 mm
  ProjetElectrique q = nouveauProjetElectrique("Faisceau", false);
  const std::string ca = poserAppareil(q, 0, "iec:connecteur", P(20, 20), 0);
  const std::string idA = pose(q, ca)->composant;
  const std::string cb = poserAppareil(q, 0, "iec:connecteur", P(60, 20), 0);
  const std::string idB = pose(q, cb)->composant;
  q.schema.feuilles[0].fils.push_back({ecad::nouvelUuid(), P(20, 22), P(60, 22)});
  numeroterFils(q, true);
  Faisceau f;
  f.uuid = "f";
  f.noeuds = {{"n1", "A", P(0, 0), idA}, {"n2", "D", P(10, 0), {}}, {"n3", "B", P(10, 10), idB}};
  f.branches = {{"n1", "n2", 300, "gaine"}, {"n2", "n3", 200, "gaine"}};
  f.surlongueur = 50;
  q.faisceaux.push_back(f);
  const auto fils = filsFaisceau(q, 0);
  REQUIRE(fils.size() == 1);
  CHECK(fils[0].longueur == Approx(600));
  CHECK(fils[0].branches.size() == 2);
  CHECK(longueurBranche(f, BrancheFaisceau{"n1", "n2", 0, {}}) == Approx(25.4));
}

TEST_CASE("Électrique : import d'un projet QElectroTech") {
  // Projet synthétique : une bobine maîtresse, un contact esclave, une borne de bornier, deux conducteurs numérotés
  const std::string qet = R"(<?xml version="1.0" encoding="UTF-8"?>
<project title="Essai &amp; import" version="0.80">
  <diagram title="Commande" order="1" cols="17" colsize="60" rows="8" rowsize="80" author="Evan" date="20260101">
    <elements>
      <element type="embed://import/elec/bobine.elmt" x="100" y="200" orientation="0" uuid="{M}">
        <terminals><terminal id="1" x="0" y="-16" orientation="0"/><terminal id="2" x="0" y="16" orientation="2"/></terminals>
        <links_uuids><link_uuid uuid="{S}"/></links_uuids>
        <elementInformations><elementInformation name="label" show="1">-KM1</elementInformation>
        <elementInformation name="function" show="1">Pompe</elementInformation></elementInformations>
      </element>
      <element type="embed://import/elec/contact_no.elmt" x="200" y="200" orientation="0" uuid="{S}">
        <terminals><terminal id="3" x="0" y="-16" orientation="0"/><terminal id="4" x="0" y="16" orientation="2"/></terminals>
        <links_uuids><link_uuid uuid="{M}"/></links_uuids>
      </element>
      <element type="embed://import/elec/borne.elmt" x="200" y="100" orientation="0" uuid="{X}">
        <terminals><terminal id="5" x="0" y="-6" orientation="0"/><terminal id="6" x="0" y="6" orientation="2"/></terminals>
        <elementInformations><elementInformation name="label" show="1">X1:7</elementInformation></elementInformations>
      </element>
    </elements>
    <conductors>
      <conductor terminal1="6" terminal2="3" num="12" conductor_section="1.5" conductor_color="Rouge"><segment orientation="vertical" length="70"/></conductor>
      <conductor terminal1="4" terminal2="2" num="13"/>
    </conductors>
  </diagram>
  <collection><category name="import"><category name="elec">
    <element name="bobine.elmt"><definition type="element" link_type="master" width="40" height="40" hotspot_x="20" hotspot_y="20">
      <names><name lang="fr">Bobine</name><name lang="en">Coil</name></names>
      <description><rect x="-10" y="-10" width="20" height="20"/><line x1="0" y1="-20" x2="0" y2="-10"/><line x1="0" y1="10" x2="0" y2="20"/>
      <terminal x="0" y="-20" orientation="n"/><terminal x="0" y="20" orientation="s"/></description></definition></element>
    <element name="contact_no.elmt"><definition type="element" link_type="slave" width="20" height="40" hotspot_x="10" hotspot_y="20">
      <names><name lang="fr">Contact NO</name></names>
      <kindInformations><kindInformation name="state">NO</kindInformation><kindInformation name="type">simple</kindInformation><kindInformation name="number">1</kindInformation></kindInformations>
      <description><line x1="0" y1="-20" x2="0" y2="-8"/><line x1="0" y1="8" x2="-6" y2="-6"/><arc x="-5" y="-5" width="10" height="10" start="0" angle="90"/>
      <terminal x="0" y="-20" orientation="n"/><terminal x="0" y="20" orientation="s"/></description></definition></element>
    <element name="borne.elmt"><definition type="element" link_type="terminal" width="10" height="20" hotspot_x="5" hotspot_y="10">
      <names><name lang="fr">Borne</name></names>
      <description><ellipse x="-3" y="-3" width="6" height="6"/><terminal x="0" y="-10" orientation="n"/><terminal x="0" y="10" orientation="s"/></description></definition></element>
  </category></category></collection>
</project>)";
  std::vector<std::string> alertes;
  const ProjetElectrique p = importerQet(qet, &alertes);
  CHECK(p.nom == "Essai & import");
  REQUIRE(p.schema.feuilles.size() == 1);
  CHECK(p.schema.feuilles[0].nom == "Commande");
  CHECK(p.schema.cartouche.date == "2026-01-01");
  const ecad::ComposantElec* km = parRepere(p, "KM1");
  REQUIRE(km);
  CHECK(km->champs.at("fonction") == "Pompe");
  const ecad::ComposantElec* x1 = parRepere(p, "X1");
  REQUIRE(x1);
  CHECK(x1->symbole == "iec:borne");
  // Le contact esclave est une pose de -KM1, numérotée 13-14 ; la bobine A1-A2
  int posesKm = 0;
  std::set<std::string> broches;
  for (const auto& s : p.schema.feuilles[0].symboles)
    if (s.composant == km->uuid) {
      ++posesKm;
      for (const auto& b : ecad::brochesPlacees(s, *p.schema.symbole(s.symbole))) broches.insert(b.numero);
    }
  CHECK(posesKm == 2);
  CHECK(broches == std::set<std::string>{"13", "14", "A1", "A2"});
  // Conducteurs et numéros de fils repris ; la borne porte son numéro (-X1:7)
  const ecad::Connectivite k = ecad::connectivite(p.schema);
  CHECK(k.nom(km->uuid, "13") == "12");
  CHECK(k.nom(x1->uuid, "7") == "12");
  CHECK(k.nom(km->uuid, "A2") == "13");
  CHECK(k.nom(km->uuid, "14") == "13");
  CHECK(p.conducteurs.at("12").couleur == "RD");
  CHECK(p.conducteurs.at("12").section == Approx(1.5));
  REQUIRE_FALSE(alertes.empty());
  CHECK(alertes.front().find("1 folio") != std::string::npos);
  // Fichiers qui ne sont pas des projets QElectroTech
  CHECK_THROWS_AS(importerQet("<svg></svg>"), std::runtime_error);
  CHECK_THROWS_AS(importerQet("<project><diagram>"), std::runtime_error);
}

TEST_CASE("Électrique : enregistrement et relecture à l'identique") {
  ProjetElectrique p = exempleDemarrageMoteur();
  p.reglages.norme = NormeRepere::Cei81346;
  p.reglages.regime = RegimeNeutre::TNS;
  p.conducteurs["24V"] = {0.75, "DBU", "H05V-K"};
  p.logement = logementType(60, 2);
  Faisceau f;
  f.uuid = "f1";
  f.noeuds = {{"a", "A", P(0, 0), {}}, {"b", "B", P(10, 0), {}}};
  f.branches = {{"a", "b", 250, "gaine annelée"}};
  p.faisceaux.push_back(f);
  const auto j = versJson(p);
  std::vector<std::string> alertes;
  const ProjetElectrique q = projetElectriqueDepuisJson(j, &alertes);
  CHECK(alertes.empty());
  CHECK(versJson(q).dump() == j.dump());
  CHECK(q.reglages.norme == NormeRepere::Cei81346);
  CHECK(q.cables.size() == 1);
  CHECK(q.armoires[0].implantations.size() == p.armoires[0].implantations.size());
  CHECK(q.logement->pieces.size() == p.logement->pieces.size());
  // Lecture tolérante : un objet abîmé ne fait pas tout perdre
  auto abime = j;
  abime["cables"].push_back(42);
  abime["reglages"]["colonnes"] = "dix";
  const ProjetElectrique r = projetElectriqueDepuisJson(abime);
  CHECK(r.cables.size() == 1);
  CHECK(r.reglages.colonnes == 10);
}

TEST_CASE("Électrique : circuits types posés en un geste, justes et sans chevauchement") {
  REQUIRE(circuitsTypes().size() == 6);
  for (const auto& info : circuitsTypes()) {
    INFO(info.nom);
    ProjetElectrique p = nouveauProjetElectrique("Essai", true);
    const int f = insererCircuitType(p, info.type);
    REQUIRE(f >= 0);
    REQUIRE(static_cast<std::size_t>(f) < p.schema.feuilles.size());
    const auto v = verifierElectrique(p);
    std::string dits;
    for (const auto& x : v) dits += x.code + " " + x.message + " ; ";
    INFO(dits);
    CHECK(v.empty());
    // Posé une seconde fois : à droite du premier ou sur un folio neuf, sans chevaucher ce qui est dessiné
    std::set<std::string> anciens;
    for (const auto& fe : p.schema.feuilles)
      for (const auto& s : fe.symboles) anciens.insert(s.uuid);
    const std::size_t appareils = p.schema.composants.size();
    insererCircuitType(p, info.type);
    CHECK(p.schema.composants.size() == 2 * appareils);
    int chevauchements = 0;
    for (const auto& fe : p.schema.feuilles)
      for (const auto& s : fe.symboles) {
        if (anciens.count(s.uuid)) continue;
        const ecad::Boite b = ecad::boiteSymbole(s, *p.schema.symbole(s.symbole));
        for (const auto& t : fe.symboles) {
          if (!anciens.count(t.uuid)) continue;
          const ecad::Boite a = ecad::boiteSymbole(t, *p.schema.symbole(t.symbole));
          chevauchements += a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
        }
      }
    CHECK(chevauchements == 0);
    const auto v2 = verifierElectrique(p);
    std::string dits2;
    for (const auto& x : v2) dits2 += x.code + " " + x.message + " ; ";
    INFO(dits2);
    CHECK(v2.empty());
  }
  auto pose = [](const ProjetElectrique& p, const std::string& composant, const std::string& symbole) -> const ecad::SymbolePlace* {
    for (const auto& fe : p.schema.feuilles)
      for (const auto& s : fe.symboles)
        if (s.composant == composant && s.symbole == symbole) return &s;
    return nullptr;
  };
  // Démarrage direct : moteur derrière le bornier et les pôles, protégé en amont par le disjoncteur moteur ;
  // bobine entre 24V et 0V par l'arrêt, la marche et son auto-maintien 13-14 ; voyant par le 23-24
  {
    ProjetElectrique p = nouveauProjetElectrique("Essai", true);
    insererCircuitType(p, CircuitType::DemarrageDirect);
    const ecad::Connectivite k = ecad::connectivite(p.schema);
    const ecad::ComposantElec* km = p.schema.parReference("KM1");
    const ecad::ComposantElec* m = p.schema.parReference("M1");
    const ecad::ComposantElec* x1 = p.schema.parReference("X1");
    const ecad::ComposantElec* s2 = p.schema.parReference("S2");
    REQUIRE((km && m && x1 && s2));
    CHECK(k.reseau(m->uuid, "U1") == k.reseau(km->uuid, "2"));
    CHECK(k.reseau(m->uuid, "U1") == k.reseau(x1->uuid, "1"));
    CHECK(k.nom(m->uuid, "PE") == "PE");
    CHECK(k.reseau(km->uuid, "A1") == k.reseau(s2->uuid, "14"));
    CHECK(k.reseau(km->uuid, "14") == k.reseau(km->uuid, "A1"));
    CHECK(k.nom(km->uuid, "A2") == "0V");
    CHECK(k.nom(km->uuid, "23") == "24V");
    CHECK(compte(verifierElectrique(p), "ELE-13") == 0);
    // Sans le disjoncteur moteur, plus rien ne protège le moteur : la vérification le dit
    supprimerAppareil(p, p.schema.parReference("Q1")->uuid);
    CHECK(compte(verifierElectrique(p), "ELE-13") == 1);
  }
  // Inverseur : KM2 croise L1 et L3 (son 2 va au W1 du moteur, son 6 au U1), chaque bobine verrouillée par un
  // contact à ouverture de l'autre contacteur
  {
    ProjetElectrique p = nouveauProjetElectrique("Essai", true);
    insererCircuitType(p, CircuitType::InverseurSens);
    const ecad::Connectivite k = ecad::connectivite(p.schema);
    const ecad::ComposantElec* km1 = p.schema.parReference("KM1");
    const ecad::ComposantElec* km2 = p.schema.parReference("KM2");
    const ecad::ComposantElec* m = p.schema.parReference("M1");
    REQUIRE((km1 && km2 && m));
    CHECK(k.reseau(km2->uuid, "2") == k.reseau(m->uuid, "W1"));
    CHECK(k.reseau(km2->uuid, "6") == k.reseau(m->uuid, "U1"));
    CHECK(k.reseau(km2->uuid, "4") == k.reseau(m->uuid, "V1"));
    CHECK(k.reseau(km1->uuid, "2") == k.reseau(m->uuid, "U1"));
    CHECK(k.reseau(km1->uuid, "1") == k.reseau(km2->uuid, "1"));
    CHECK(k.reseau(km2->uuid, "A1") == k.reseau(km1->uuid, "22"));
    CHECK(k.reseau(km1->uuid, "A1") == k.reseau(km2->uuid, "22"));
    const ecad::SymbolePlace* nf = pose(p, km1->uuid, "iec:contact-nf");
    REQUIRE(nf);
  }
  // Bâtiment : poussoirs en parallèle sur la bobine du télérupteur, lampe derrière son contact ; navettes du
  // va-et-vient croisées ; prises sur la même phase, terre au PE, conducteurs de 2,5 mm²
  {
    ProjetElectrique p = nouveauProjetElectrique("Essai", true);
    insererCircuitType(p, CircuitType::Telerupteur);
    insererCircuitType(p, CircuitType::VaEtVient);
    insererCircuitType(p, CircuitType::Prises);
    const ecad::Connectivite k = ecad::connectivite(p.schema);
    const auto* kt = p.schema.parReference("KA1");
    const auto* s1 = p.schema.parReference("S1");
    const auto* s2 = p.schema.parReference("S2");
    const auto* e1 = p.schema.parReference("E1");
    REQUIRE((kt && s1 && s2 && e1));
    CHECK(k.reseau(s1->uuid, "2") == k.reseau(kt->uuid, "A1"));
    CHECK(k.reseau(s2->uuid, "2") == k.reseau(kt->uuid, "A1"));
    CHECK(k.reseau(e1->uuid, "1") == k.reseau(kt->uuid, "2"));
    // Retour par le neutre protégé : la sortie N2 du disjoncteur du circuit
    CHECK(k.reseau(e1->uuid, "2") == k.reseau(p.schema.parReference("Q1")->uuid, "N2"));
    CHECK(k.nom(p.schema.parReference("Q1")->uuid, "N1") == "N");
    const auto* sa = p.schema.parReference("S3");
    const auto* sb = p.schema.parReference("S4");
    const auto* e2 = p.schema.parReference("E2");
    REQUIRE((sa && sb && e2));
    CHECK(k.reseau(sa->uuid, "2") == k.reseau(sb->uuid, "1"));
    CHECK(k.reseau(sa->uuid, "1") == k.reseau(sb->uuid, "2"));
    CHECK(k.reseau(sb->uuid, "L") == k.reseau(e2->uuid, "1"));
    const auto* xd1 = p.schema.parReference("XD1");
    const auto* xd3 = p.schema.parReference("XD3");
    REQUIRE((xd1 && xd3));
    CHECK(k.reseau(xd1->uuid, "L") == k.reseau(xd3->uuid, "L"));
    CHECK(k.nom(xd3->uuid, "PE") == "PE");
    CHECK(k.reseau(xd3->uuid, "N") == k.reseau(p.schema.parReference("Q3")->uuid, "N2"));
    CHECK(proprietesEffectives(p, k.nom(xd1->uuid, "L")).section == Approx(2.5));
    CHECK(verifierElectrique(p).empty());
    // Tous trois sur un même folio du bâtiment, côte à côte
    CHECK(p.schema.feuilles.size() == 4);
    CHECK(p.folio(p.schema.feuilles[3].uuid).type == TypeFolio::Distribution);
  }
  // Renvois : un nom de potentiel posé une seule fois est signalé, pas une arrivée d'alimentation (L1, 24V…)
  {
    ProjetElectrique p = nouveauProjetElectrique("Essai", true);
    poserPotentiel(p, 1, "L1", P(20, 20), 0);
    poserPotentiel(p, 2, "24V", P(20, 20), 0);
    poserPotentiel(p, 2, "Vers F5", P(40, 20), 0);
    const auto v = verifierElectrique(p);
    CHECK(compte(v, "ELE-07") == 1);
  }
}
