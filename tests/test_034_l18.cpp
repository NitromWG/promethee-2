// Prométhée : essais du lot L18 de la 0.34 (schéma de lignes, topologie extraite d'un réseau routé, moignons).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/circuit_edition.hpp"
#include "promethee/electronique.hpp"
#include "promethee/simulation.hpp"
#include "promethee/topologie_lignes.hpp"

using namespace prom;
using namespace prom::ecad;
using Catch::Approx;

namespace {
Piste piste(const std::string& reseau, Pt a, Pt b, Couche k = Couche::CuivreDessus, Nm largeur = 150'000) {
  Piste p;
  p.uuid = nouvelUuid();
  p.reseau = reseau;
  p.couche = k;
  p.a = a;
  p.b = b;
  p.largeur = largeur;
  return p;
}
Pt pastille(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  for (const auto& p : pastilles(c, connectivite(c)))
    if (p.composant == composant && p.numero == numero) return p.pastille.position;
  return {};
}
// Bus fly-by : un pilote et quatre récepteurs traversants, ligne principale sur le dessus puis, après une via, sur la
// première couche interne d'une carte de 3,27 mm à quatre couches ; branches de 2 mm
struct FlyBy {
  ConceptionElec c = nouvelleConception("Fly-by");
  std::vector<std::string> u;
  std::string via;
  FlyBy() {
    c.circuit.contour.rayon = 0;
    c.circuit.contour.largeur = nm(110);
    c.circuit.contour.hauteur = nm(40);
    Empilage& e = c.circuit.empilage;
    e.couchesCuivre = 4;
    e.epaisseur = 3.27;
    auto cuivre = [](const char* nom) {
      CoucheEmpilage x;
      x.genre = GenreCoucheEmpilage::Cuivre;
      x.nom = nom;
      x.epaisseur = 0.035;
      return x;
    };
    auto dielectrique = [](double ep) {
      CoucheEmpilage x;
      x.genre = GenreCoucheEmpilage::Dielectrique;
      x.epaisseur = ep;
      return x;
    };
    e.pile = {cuivre("F.Cu"), dielectrique(0.2), cuivre("In1.Cu"), dielectrique(2.73), cuivre("In2.Cu"), dielectrique(0.2), cuivre("B.Cu")};
    for (int i = 0; i < 5; ++i) u.push_back(poserEmpreinteSeule(c, "PinHeader_1x02_P2.54", ptMm(10 + 20 * i, 20)));
    for (const auto& x : u) {
      c.nomsReseaux[x + ":1"] = "DQ0";
      c.nomsReseaux[x + ":2"] = "GND";
    }
    const Pt p0 = pastille(c, u[0], "1");
    const Nm yl = p0.y - nm(2);  // ligne principale 2 mm au-dessus des broches
    // Du pilote à la ligne principale, puis U2, U3 sur le dessus, la via à mi-chemin de U4, U4 et U5 sur In1
    c.circuit.pistes.push_back(piste("DQ0", p0, {p0.x, yl}));
    std::vector<Pt> j;
    for (int i = 1; i < 5; ++i) j.push_back({pastille(c, u[static_cast<std::size_t>(i)], "1").x, yl});
    const Pt pv{(j[1].x + j[2].x) / 2, yl};
    c.circuit.pistes.push_back(piste("DQ0", {p0.x, yl}, j[0]));
    c.circuit.pistes.push_back(piste("DQ0", j[0], j[1]));
    c.circuit.pistes.push_back(piste("DQ0", j[1], pv));
    Via v;
    v.uuid = nouvelUuid();
    v.reseau = "DQ0";
    v.p = pv;
    v.diametre = 600'000;
    v.percage = 300'000;
    c.circuit.vias.push_back(v);
    via = v.uuid;
    c.circuit.pistes.push_back(piste("DQ0", pv, j[2], Couche::CuivreInt1));
    c.circuit.pistes.push_back(piste("DQ0", j[2], j[3], Couche::CuivreInt1));
    for (int i = 1; i < 5; ++i) c.circuit.pistes.push_back(piste("DQ0", j[static_cast<std::size_t>(i - 1)], pastille(c, u[static_cast<std::size_t>(i)], "1"), i >= 3 ? Couche::CuivreInt1 : Couche::CuivreDessus));
  }
  std::string ref(int i) const { return c.composant(u[static_cast<std::size_t>(i)])->reference + ".1"; }
};
}  // namespace

TEST_CASE("18.4 · bus fly-by de quatre récepteurs : topologie extraite superposée à la prévue, moignon de via de 3 mm signalé au-delà de sa limite", "[034][L18][18.4]") {
  FlyBy f;
  SchemaLignes prevue;
  prevue.reseau = "DQ0";
  prevue.topologie = TopologieSignal::FlyBy;
  prevue.ordre = {f.ref(0), f.ref(1), f.ref(2), f.ref(3), f.ref(4)};
  prevue.moignonMax = 3.0;
  prevue.moignonViaMax = 0.5;
  prevue.provenance = "schéma de lignes de l'octet 0";
  // Schéma de lignes prévu : pilote, quatre tronçons de 20 mm, récepteurs
  ElementLigne pil;
  pil.genre = GenreElementLigne::Pilote;
  pil.id = pil.a = f.ref(0);
  pil.resistance = 34;
  prevue.elements.push_back(pil);
  for (int i = 1; i < 5; ++i) {
    ElementLigne t;
    t.genre = GenreElementLigne::Troncon;
    t.id = "T" + std::to_string(i);
    t.a = f.ref(i - 1);
    t.b = f.ref(i);
    t.longueur = 20;
    prevue.elements.push_back(t);
    ElementLigne r;
    r.genre = GenreElementLigne::Recepteur;
    r.id = r.a = f.ref(i);
    r.capacite = 1.5e-12;
    prevue.elements.push_back(r);
  }
  ecrireSchemasLignes(f.c, {prevue});
  REQUIRE(schemasLignes(f.c).size() == 1);
  CHECK(schemasLignes(f.c)[0] == prevue);
  // Topologie extraite du cuivre : même ordre, branches de 2 mm, une via au moignon de 3 mm
  const SchemaLignes x = extraireTopologie(f.c, "DQ0", &prevue);
  CHECK(x.ordre == prevue.ordre);
  int recepteurs = 0, branches = 0, vias = 0;
  double moignon = 0;
  for (const auto& e : x.elements) {
    if (e.genre == GenreElementLigne::Recepteur) {
      ++recepteurs;
      // Les trois premiers sur leur branche de 2 mm ; le dernier ferme la ligne principale
      if (e.id == f.ref(4)) CHECK(e.longueur <= 2.05);
      else branches += std::abs(e.longueur - 2.0) < 0.05;
    }
    if (e.genre == GenreElementLigne::Via) ++vias, moignon = e.longueur;
  }
  CHECK(recepteurs == 4);
  CHECK(branches == 3);
  CHECK(vias == 1);
  CHECK(moignon == Approx(3.0).margin(0.01));
  // Écart : le moignon de via au-delà de sa limite, seul
  const auto ecarts = comparerTopologie(prevue, x);
  REQUIRE(ecarts.size() == 1);
  CHECK(ecarts[0].genre == "moignon de via");
  CHECK(ecarts[0].valeur == Approx(3.0).margin(0.01));
  CHECK(ecarts[0].message.find("3,0 mm") != std::string::npos);
  // Superposition des deux topologies
  const std::string svg = svgTopologies(prevue, x);
  CHECK(svg.find("stroke-dasharray") != std::string::npos);
  CHECK(svg.find("3,0 mm") != std::string::npos);
  // À la DRC de la carte
  const Connectivite k = connectivite(f.c);
  const auto drc = verifierDRC(f.c, k, remplirZones(f.c, k));
  CHECK(std::count_if(drc.begin(), drc.end(), [](const Violation& v) { return v.code == "DRC-91"; }) == 1);
  // Contre-percée jusqu'à In1 avec un talon de 0,2 mm : le moignon rentre dans sa limite
  ConceptionElec cp = f.c;
  for (auto& v : cp.circuit.vias) v.contrePercage = ContrePercage{true, Couche::CuivreInt1, 0, 200'000};
  const SchemaLignes y = extraireTopologie(cp, "DQ0", &prevue);
  for (const auto& e : y.elements)
    if (e.genre == GenreElementLigne::Via) CHECK(e.longueur <= 0.5);
  CHECK(comparerTopologie(prevue, y).empty());
  // Ordre prévu différent de celui du cuivre : signalé
  SchemaLignes autre = prevue;
  std::swap(autre.ordre[2], autre.ordre[3]);
  const auto o = comparerTopologie(autre, extraireTopologie(f.c, "DQ0", &autre));
  CHECK(std::any_of(o.begin(), o.end(), [](const EcartTopologie& e) { return e.genre == "ordre"; }));
  // Valeurs retenues versées aux règles, provenance notée
  ConceptionElec r = f.c;
  CHECK(appliquerAuxRegles(r, prevue) == 4);
  REQUIRE(r.circuit.regles.avancees.signaux.size() == 1);
  CHECK(r.circuit.regles.avancees.signaux[0].topologie == TopologieSignal::FlyBy);
  CHECK(r.circuit.regles.avancees.signaux[0].ordre == prevue.ordre);
  CHECK(r.extensions.at("provenanceRegles").at("DQ0").at("source") == "schéma de lignes");
  // Schéma de lignes simulé par ngspice, le moignon balayé
  if (ngspiceDisponible()) {
    for (double l : {0.0, 3.0}) {
      const std::string net = netlistLignes(x, 0.3e-9, 20e-9, l);
      const ResultatSimulation s = simuler(net);
      INFO(net << s.erreur);
      REQUIRE(s.reussi());
      std::string nom = "v(n_" + f.ref(4) + ")";
      for (auto& ch : nom) ch = ch == '.' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
      const auto* v = s.reel(nom);
      REQUIRE(v);
      CHECK(*std::max_element(v->begin(), v->end()) > 0.5);
    }
  } else
    WARN("ngspice absent : le schéma de lignes n'est pas simulé");
}
