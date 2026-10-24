// Prométhée : essais de la 0.34 — langage d'expressions (13.11), contraintes et leur résolution (8.24, 5.12, 5.27),
// tables d'isolement sourcées (IPC-2221B, CEI 60664-1), règles enregistrées et rechargées, contrôles de la DRC
// guidés par les contraintes (DRC-57, DRC-58, DRC-59, DRC-44, DRC-76 de 5.30).
// Licence GPL-3.0-only.
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/circuit_edition.hpp"
#include "promethee/electronique.hpp"
#include "promethee/expressions.hpp"
#include "promethee/regles_carte.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
int compter(const ConceptionElec& c, const std::string& code, std::vector<std::string>* objets = nullptr) {
  const Connectivite k = connectivite(c);
  int n = 0;
  for (const auto& v : verifierDRC(c, k, remplirZones(c, k)))
    if (v.code == code) {
      ++n;
      if (objets) *objets = v.objets;
    }
  return n;
}
Proprietes proprietes(std::vector<std::pair<std::string, ValeurExpression>> v) {
  return [v](const std::string& nom) -> std::optional<ValeurExpression> {
    for (const auto& [n, x] : v)
      if (n == nom) return x;
    return std::nullopt;
  };
}
ConceptionElec carte(int couches = 2) {
  ConceptionElec c = nouvelleConception("Règles");
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(50);
  c.circuit.contour.hauteur = nm(40);
  c.circuit.empilage.couchesCuivre = couches;
  return c;
}
Piste piste(const std::string& reseau, Pt a, Pt b, Couche k = Couche::CuivreDessus) {
  Piste p;
  p.uuid = nouvelUuid();
  p.reseau = reseau;
  p.couche = k;
  p.a = a;
  p.b = b;
  return p;
}
}  // namespace

TEST_CASE("13.11 · langage d'expressions : opérateurs, motifs, unités, fonctions, erreurs situées", "[034][L13][13.11]") {
  const auto p = proprietes({{"reseau", std::string("USB_DP")}, {"largeur", 200'000.0}, {"couche", std::string("In1.Cu")}, {"interne", 1.0}, {"classe", std::string("USB")}});
  CHECK(Expression::compiler("reseau ~ 'USB_D?'").evaluer(p));
  CHECK_FALSE(Expression::compiler("reseau ~ 'USB_DM'").evaluer(p));
  CHECK(Expression::compiler("largeur < 0.25mm && interne == 1").evaluer(p));
  CHECK(Expression::compiler("largeur >= 0,2 mm et classe == 'USB'").evaluer(p));
  CHECK(Expression::compiler("non (couche == 'F.Cu') ou reseau == 'GND'").evaluer(p));
  CHECK(Expression::compiler("dans(couche, 'In1.Cu', 'In2.Cu')").evaluer(p));
  CHECK(Expression::compiler("commence(reseau, 'USB') && finit(reseau, 'P') && contient(reseau, '_D')").evaluer(p));
  CHECK_FALSE(Expression::compiler("existe(region)").evaluer(p));
  CHECK(Expression::compiler("").evaluer(p));  // portée vide : tout objet
  const Expression mauvaise = Expression::compiler("largeur < (0.2mm");
  CHECK_FALSE(mauvaise.valide());
  CHECK_FALSE(mauvaise.erreur().empty());
  const auto ids = Expression::compiler("reseau == 'A' || largeur > 1mm").identifiants();
  CHECK(std::count(ids.begin(), ids.end(), "reseau") == 1);
  CHECK(std::count(ids.begin(), ids.end(), "largeur") == 1);
  double v = 0;
  REQUIRE(lireNombreUnite("1,5 mm", v));
  CHECK(v == Approx(1.5e6));
  REQUIRE(lireNombreUnite("10mil", v));
  CHECK(v == Approx(254'000));
}

TEST_CASE("8.24, 5.12 · valeurs min/préf/max et interne, résolution par priorité puis par sévérité, justification", "[034][L8][8.24][5.12]") {
  ValeurContrainte v;
  REQUIRE(lireValeurContrainte("0,15/0,2", v));
  CHECK(v.minPour(false) == nm(0.15));
  CHECK(v.minPour(true) == nm(0.2));
  REQUIRE(lireValeurContrainte("<3", v));
  CHECK(v.max == 3'000'000);
  CHECK_FALSE(lireValeurContrainte("beaucoup", v));

  ConceptionElec c = carte();
  ClasseReseau secteur;
  secteur.nom = "Secteur";
  secteur.isolement = nm(3);
  c.circuit.regles.classes.push_back(secteur);
  c.circuit.regles.classeReseau["L"] = "Secteur";
  const auto L = proprietesReseau(c, "L"), N = proprietesReseau(c, "N");
  Resolution r = isolementRequis(c, L, N, false);
  REQUIRE(r.trouvee);
  CHECK(r.valeur.min == nm(3));
  CHECK(r.justification.find("Secteur") != std::string::npos);
  // Une contrainte de priorité plus haute l'emporte, même moins sévère ; à priorité égale, la plus sévère
  Contrainte k;
  k.nom = "Essai";
  k.genre = GenreContrainte::Isolement;
  k.portee = "reseau == 'L'";
  k.valeur.min = nm(2);
  k.priorite = 5;
  c.circuit.regles.avancees.contraintes.push_back(k);
  r = isolementRequis(c, L, N, false);
  CHECK(r.valeur.min == nm(2));
  CHECK(r.regle == "Essai");
  c.circuit.regles.avancees.contraintes.back().priorite = 0;
  c.circuit.regles.avancees.contraintes.back().valeur.min = nm(4);
  r = isolementRequis(c, L, N, false);
  CHECK(r.valeur.min == nm(4));
  CHECK_FALSE(r.ecartees.empty());
}

TEST_CASE("5.27 · tables d'isolement sourcées : IPC-2221B 6-1 et CEI 60664-1 F.4", "[034][L5][5.27]") {
  CHECK(isolementIPC2221(10, "B1") == Approx(0.05));
  CHECK(isolementIPC2221(600, "B1") == Approx(0.50));  // au-delà de 500 V : 0,25 + 100 × 0,0025
  CHECK(isolementIPC2221(230, "B2") > isolementIPC2221(230, "B1"));
  CHECK(ligneFuiteCEI60664(250, 2, 3, true) > ligneFuiteCEI60664(50, 2, 3, true));
  CHECK(ligneFuiteCEI60664(250, 3, 3, false) > ligneFuiteCEI60664(250, 2, 3, false));
  CHECK(sourceIPC2221().find("IPC-2221") != std::string::npos);
  CHECK(sourceCEI60664().find("60664") != std::string::npos);
  // Réglages sous le minimum du profil du fabricant : chacun avec sa raison (5.12)
  Regles r;
  r.isolement = nm(0.05);
  const auto e = ecartsAuProfil(r);
  REQUIRE_FALSE(e.empty());
  CHECK(e.front().raison.find("minimum du profil") != std::string::npos);
}

TEST_CASE("5.27 · horloge permise sur les couches internes seules : une piste posée dessus est signalée (DRC-57)", "[034][L5][5.27]") {
  ConceptionElec c = carte(4);
  Contrainte k;
  k.nom = "Horloge dedans";
  k.genre = GenreContrainte::CouchesPermises;
  k.portee = "reseau == 'CLK'";
  k.couches = bit(Couche::CuivreInt1) | bit(Couche::CuivreInt2);
  c.circuit.regles.avancees.contraintes.push_back(k);
  c.circuit.pistes = {piste("CLK", ptMm(10, 10), ptMm(20, 10), Couche::CuivreInt1)};
  CHECK(compter(c, "DRC-57") == 0);
  c.circuit.pistes.push_back(piste("CLK", ptMm(10, 20), ptMm(20, 20), Couche::CuivreDessus));
  std::vector<std::string> objets;
  CHECK(compter(c, "DRC-57", &objets) == 1);
  CHECK(objets == std::vector<std::string>{c.circuit.pistes.back().uuid});
  // Nombre de vias par réseau (DRC-58) et interdiction de vias dans une région (DRC-59)
  Contrainte nv;
  nv.nom = "Peu de vias";
  nv.genre = GenreContrainte::NombreVias;
  nv.portee = "reseau == 'CLK'";
  nv.valeur.max = 1;
  c.circuit.regles.avancees.contraintes.push_back(nv);
  Via a, b;
  a.uuid = nouvelUuid();
  a.reseau = "CLK";
  a.p = ptMm(20, 10);
  b = a;
  b.uuid = nouvelUuid();
  b.p = ptMm(20, 20);
  c.circuit.vias = {a, b};
  CHECK(compter(c, "DRC-58") == 1);
  ZoneRegle antenne;
  antenne.uuid = nouvelUuid();
  antenne.nom = "Antenne";
  antenne.contour = orienter(rectangle(ptMm(20, 20), nm(6), nm(6)));
  c.circuit.zonesRegles.push_back(antenne);
  Contrainte in;
  in.nom = "Pas de via sous l'antenne";
  in.genre = GenreContrainte::Interdiction;
  in.portee = "region == 'Antenne'";
  in.texte = "vias";
  c.circuit.regles.avancees.contraintes.push_back(in);
  CHECK(compter(c, "DRC-59", &objets) == 1);
  CHECK(objets == std::vector<std::string>{b.uuid});
}

TEST_CASE("5.27 · règles enregistrées en fichier, rechargées par ajout ou par remplacement", "[034][L5][5.27]") {
  ConceptionElec a = carte();
  Contrainte k;
  k.nom = "USB";
  k.genre = GenreContrainte::Largeur;
  REQUIRE(lireValeurContrainte("0,15/0,2", k.valeur));
  k.portee = "reseau ~ 'USB_D?'";
  a.circuit.regles.avancees.contraintes.push_back(k);
  a.circuit.regles.avancees.paires.push_back({"USB", "USB_DP", "USB_DM", "", nm(0.2), nm(0.15), 0, 0, nm(5), 90});
  const std::string texte = exporterRegles(a.circuit.regles);
  CHECK(texte.find("promethee-regles") != std::string::npos);
  ConceptionElec b = carte();
  Contrainte autre;
  autre.nom = "Autre";
  b.circuit.regles.avancees.contraintes.push_back(autre);
  std::string erreur;
  REQUIRE(importerRegles(b.circuit.regles, texte, false, &erreur));
  CHECK(b.circuit.regles.avancees.contraintes.size() == 2);  // ajout
  REQUIRE(importerRegles(b.circuit.regles, texte, true, &erreur));
  CHECK(b.circuit.regles.avancees.contraintes == a.circuit.regles.avancees.contraintes);  // remplacement
  CHECK(b.circuit.regles.avancees.paires == a.circuit.regles.avancees.paires);
  CHECK_FALSE(importerRegles(b.circuit.regles, "{pas du json", true, &erreur));
  CHECK_FALSE(erreur.empty());
}

TEST_CASE("5.30 · « C12 à 2 mm au plus de la broche 7 de U3 » : C12 posé à 3 mm, violation qui mène aux deux objets (DRC-76)", "[034][L5][5.30]") {
  ConceptionElec c = carte();
  const std::string u3 = poserEmpreinteSeule(c, "SOIC-8", ptMm(25, 20), 0, false, "U");
  const std::string c12 = poserEmpreinteSeule(c, "C_0603", ptMm(5, 5), 0, false, "C");
  c.composant(u3)->reference = "U3";
  c.composant(c12)->reference = "C12";
  Contrainte k;
  k.nom = "Découplage de U3";
  k.genre = GenreContrainte::Proximite;
  k.portee = "reference == 'C12'";
  k.porteeB = "broche == 'U3.7'";
  k.valeur.max = nm(2);
  c.circuit.regles.avancees.contraintes.push_back(k);
  // Pastille de U3.7 sur la carte
  Pt broche{};
  for (const auto& p : pastilles(c, connectivite(c)))
    if (p.composant == u3 && p.numero == "7") broche = p.pastille.position;
  // C12 placé de façon que sa pastille la plus proche soit à 3 mm de la broche
  auto placerA = [&](double d) {
    for (auto& e : c.circuit.empreintes)
      if (e.composant == c12) e.position = Pt{broche.x + nm(d) + nm(0.825), broche.y};  // pastille 1 à -0,825 mm du centre
  };
  placerA(3);
  std::vector<std::string> objets;
  REQUIRE(compter(c, "DRC-76", &objets) == 1);
  CHECK(std::find(objets.begin(), objets.end(), c12) != objets.end());
  CHECK(std::find(objets.begin(), objets.end(), u3) != objets.end());
  placerA(1.5);
  CHECK(compter(c, "DRC-76") == 0);
}

TEST_CASE("5.24, 5.30 · zone de placement et hauteur maximale : composant hors de sa zone, trop haut (DRC-44)", "[034][L5][5.24][5.30]") {
  ConceptionElec c = carte();
  const std::string r1 = poserEmpreinteSeule(c, "R_0603", ptMm(10, 10));
  ClasseComposants k;
  k.nom = "Entrée";
  k.membres = {"R1"};
  c.circuit.regles.avancees.classesComposants.push_back(k);
  ZoneRegle z;
  z.uuid = nouvelUuid();
  z.nom = "Bloc d'entrée";
  z.contour = orienter(rectangle(ptMm(35, 20), nm(10), nm(10)));
  z.placement = "classe:Entrée";
  c.circuit.zonesRegles.push_back(z);
  CHECK(compter(c, "DRC-44") == 1);
  c.circuit.empreintes[0].position = ptMm(35, 20);
  CHECK(compter(c, "DRC-44") == 0);
  c.circuit.zonesRegles[0].hauteurMax = nm(0.1);  // une résistance 0603 est plus haute
  CHECK(compter(c, "DRC-44") == 1);
  (void)r1;
}

TEST_CASE("5.28 · carte d'essai à quinze défauts semés, un par contrôle nouveau : quinze violations, aucune de plus", "[034][L5][5.28]") {
  ConceptionElec c = nouvelleConception("Quinze défauts");
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(80);
  c.circuit.contour.hauteur = nm(50);
  auto poser = [&](const std::string& id, Pt p) { return poserEmpreinteSeule(c, id, p); };
  const std::string r1 = poser("R_0805", ptMm(10, 10)), r2 = poser("R_0805", ptMm(30, 10)), r3 = poser("R_0805", ptMm(10, 30)), r4 = poser("R_0805", ptMm(30, 30));
  const std::string r5 = poser("R_0805", ptMm(60, 8)), r6 = poser("R_0805", ptMm(60, 42));
  c.nomsReseaux[r1 + ":2"] = c.nomsReseaux[r2 + ":1"] = "A";
  c.nomsReseaux[r3 + ":2"] = c.nomsReseaux[r4 + ":1"] = "B";
  c.nomsReseaux[r5 + ":1"] = c.nomsReseaux[r6 + ":1"] = "GND";
  auto pad = [&](const std::string& comp, const std::string& num) {
    for (const auto& p : pastilles(c, connectivite(c)))
      if (p.composant == comp && p.numero == num) return p.pastille.position;
    return Pt{};
  };
  const Pt a1 = pad(r1, "2"), a2 = pad(r2, "1"), b1 = pad(r3, "2"), b2 = pad(r4, "1"), g1 = pad(r5, "1"), g2 = pad(r6, "1");
  c.circuit.pistes = {piste("A", a1, a2), piste("B", b1, b2), piste("GND", g1, g2)};
  auto via = [&](const std::string& reseau, Pt p) {
    Via v;
    v.uuid = nouvelUuid();
    v.reseau = reseau;
    v.p = p;
    c.circuit.vias.push_back(v);
    return v.uuid;
  };
  via("A", {nm(15), a1.y});
  via("B", {nm(15), b1.y});
  via("GND", {g1.x, nm(15)});
  via("GND", {g1.x, nm(35)});
  auto zone = [&](const std::string& reseau, Polygone contour) {
    Zone z;
    z.uuid = nouvelUuid();
    z.reseau = reseau;
    z.couche = Couche::CuivreDessous;
    z.contour = std::move(contour);
    z.epaisseurMin = 50'000;
    c.circuit.zones.push_back(z);
    return c.circuit.zones.size() - 1;
  };
  auto rect = [](double x0, double y0, double x1, double y1) { return Polygone{ptMm(x0, y0), ptMm(x1, y0), ptMm(x1, y1), ptMm(x0, y1)}; };
  // Plan GND en sablier : deux carrés reliés par un col de 3 mm (un seul morceau)
  auto sablier = [&](double col) {
    const double x = 58.0;
    return Polygone{ptMm(52, 12), ptMm(66, 12), ptMm(66, 22), ptMm(x + col, 22), ptMm(x + col, 28), ptMm(66, 28), ptMm(66, 38), ptMm(52, 38), ptMm(52, 28), ptMm(x, 28), ptMm(x, 22), ptMm(52, 22)};
  };
  const std::size_t zg = zone("GND", sablier(3.0));
  zone("A", rect(12, 6, 18, 20));
  const std::size_t zb = zone("B", rect(12, 24, 18, 34));
  // Empreinte des défauts de pastilles, posée à l'écart : propre au départ
  DefEmpreinte d;
  d.id = "perso:Essai";
  d.famille = "essai";
  auto tht = [](const std::string& n, double x, double cote, double trou) {
    Pastille p;
    p.numero = n;
    p.type = TypePastille::Traversante;
    p.forme = FormePastille::Rond;
    p.position = ptMm(x, 0);
    p.largeur = p.hauteur = nm(cote);
    p.percage = nm(trou);
    return p;
  };
  auto cms = [&](const std::string& n, double x) {
    Pastille p = tht(n, x, 1.6, 0);
    p.type = TypePastille::CMS;
    p.forme = FormePastille::Rect;
    return p;
  };
  d.pastilles = {tht("1", 0, 1.6, 0.8), tht("2", 5, 1.6, 0.8), cms("3", 10), cms("4", 15), tht("5", 20, 1.6, 0.8)};
  d.encombrement = {-nm(1.5), -nm(1.5), nm(21.5), nm(1.5)};
  d.corps.largeur = nm(23);
  d.corps.profondeur = nm(3);
  d.corps.hauteur = nm(2);
  c.empreintes[d.id] = d;
  const std::string je = poser(d.id, ptMm(25, 44));
  auto toutes = [&] {
    const Connectivite k = connectivite(c);
    return verifierDRC(c, k, remplirZones(c, k));
  };
  auto texte = [](const std::vector<Violation>& v) {
    std::string s;
    for (const auto& x : v) s += x.code + " " + x.message + "\n";
    return s;
  };
  const auto base = toutes();
  UNSCOPED_INFO("carte de départ :\n" << texte(base));
  REQUIRE(base.empty());

  // Les quinze défauts
  via("A", ptMm(24, 10));
  c.circuit.vias.back().type = TypeVia::Borgne;
  c.circuit.vias.back().vers = Couche::CuivreDessus;                             // DRC-60 : via d'une seule couche
  c.circuit.zones[zb].contour = rect(12, 18, 18, 34);                             // DRC-61 : B chevauche A, même priorité
  c.circuit.zones[zg].contour = sablier(0.02);                                    // DRC-62 : col trop étroit, plan coupé
  c.circuit.zones[zg].largeurRayon = 100'000;                                     // DRC-64 : rayons de 0,1 mm
  RegionCuivre morte;
  morte.uuid = nouvelUuid();
  morte.reseau = "A";
  morte.genre = "libre";
  morte.forme = rect(70, 24, 71, 25);
  c.circuit.regions.push_back(morte);                                             // DRC-63 : cuivre mort
  for (auto& e : c.circuit.empreintes) {
    if (e.composant == r3) e.repere = ptMm(0, -2.5);
    if (e.composant == r4) e.repere = ptMm(-20, -2.5);                            // DRC-65 : repères superposés
  }
  TexteCircuit t;
  t.uuid = nouvelUuid();
  t.texte = "LOGO";
  t.p = ptMm(42, 20);
  t.miroir = true;
  c.circuit.textes.push_back(t);                                                  // DRC-66 : miroir sur le dessus
  DessinCircuit trait;
  trait.uuid = nouvelUuid();
  trait.couche = Couche::Contour;
  trait.points = {ptMm(40, 26), ptMm(45, 26)};
  c.circuit.dessins.push_back(trait);                                             // DRC-67 : dessin sur le contour
  c.circuit.pistes.push_back(piste("A", a1, {a1.x + 1'000, a1.y}));                // DRC-68 : segment de 1 µm
  Contrainte longueur;
  longueur.nom = "B court";
  longueur.genre = GenreContrainte::Longueur;
  longueur.portee = "reseau == 'B'";
  longueur.valeur.max = nm(5);
  c.circuit.regles.avancees.contraintes.push_back(longueur);                      // DRC-69 : réseau trop long
  c.circuit.pistes.push_back(piste("A", {nm(15.25), a1.y}, a2));                  // DRC-70 : piste qui finit hors du centre de la via
  DefEmpreinte& m = c.empreintes[d.id];
  m.pastilles[0].largeur = m.pastilles[0].hauteur = nm(0.8);                      // DRC-71 : pastille de la taille du trou
  m.pastilles[1].decalagePercage = ptMm(1.0, 0);                                  // DRC-72 : trou hors de sa pastille
  m.pastilles[3].position = ptMm(10.5, 0);                                        // DRC-73 : pastilles 3 et 4 qui se recouvrent
  m.pastilles[4].longueurPercage = nm(0.5);                                       // DRC-74 : fente plus courte que son trou
  (void)je;

  const auto apres = toutes();
  UNSCOPED_INFO("carte aux défauts :\n" << texte(apres));
  std::map<std::string, int> parCode;
  for (const auto& v : apres) ++parCode[v.code];
  for (int n = 60; n <= 74; ++n) CHECK(parCode["DRC-" + std::to_string(n)] == 1);
  CHECK(apres.size() == 15);
}
