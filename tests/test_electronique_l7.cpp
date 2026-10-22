// Prométhée : essais du lot L7 (schéma avancé). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/dessins_circuit.hpp"
#include "promethee/electronique.hpp"
#include "promethee/structure_schema.hpp"
#include "promethee/synchronisation.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
struct Banc7 {
  ConceptionElec c = nouvelleConception("Banc L7");
  std::string poser(const std::string& id, double x, double y, int rot = 0, std::size_t f = 0) { return poserSymbole(c, f, id, ptMm(x, y), rot); }
  Pt broche(const std::string& sym, const std::string& numero) const {
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles)
        if (s.uuid == sym)
          for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
            if (b.broche->numero == numero) return b.p;
    return {};
  }
  std::string composant(const std::string& sym) const {
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles)
        if (s.uuid == sym) return s.composant;
    return {};
  }
  std::string fil(Pt a, Pt b, std::size_t f = 0) {
    const std::string id = nouvelUuid();
    c.feuilles[f].fils.push_back({id, a, b});
    return id;
  }
  void etiquette(Pt p, const std::string& t, bool globale = false, std::size_t f = 0) {
    Etiquette e;
    e.uuid = nouvelUuid();
    e.texte = t;
    e.p = p;
    e.type = globale ? TypeEtiquette::Globale : TypeEtiquette::Locale;
    c.feuilles[f].etiquettes.push_back(e);
  }
  int compter(const std::string& code) const {
    const auto v = verifierERC(c, connectivite(c), NiveauErc::Complet);
    return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
  }
};
}  // namespace

TEST_CASE("7.4 : un bus D[0..7] relie les huit étiquettes D0 à D7 sans les court-circuiter ; une entrée D8 est signalée", "[L7][STR-04][S10]") {
  Banc7 b;
  // Le bus, nommé à son bout gauche
  b.fil(ptMm(50.8, 101.6), ptMm(152.4, 101.6));
  b.etiquette(ptMm(50.8, 101.6), "D[0..7]");
  // Huit entrées, chacune portant un membre ; de l'autre côté, huit résistances dont la broche 1 porte le même nom
  std::vector<std::string> r;
  for (int i = 0; i < 8; ++i) {
    const Pt sur{nm(60.96 + 10.16 * i), nm(101.6)}, bout{nm(60.96 + 10.16 * i), nm(91.44)};
    b.fil(sur, bout);
    b.etiquette(bout, "D" + std::to_string(i));
    const std::string s = b.poser("R", 60.96 + 10.16 * i, 50.8);
    r.push_back(b.composant(s));
    const Pt p1 = b.broche(s, "1");
    b.etiquette(p1, "D" + std::to_string(i));
  }
  const Connectivite k = connectivite(b.c);
  std::set<int> reseaux;
  for (int i = 0; i < 8; ++i) {
    const int n = k.reseau(r[static_cast<std::size_t>(i)], "1");
    REQUIRE(n >= 0);
    CHECK(k.reseaux[static_cast<std::size_t>(n)].nom == "D" + std::to_string(i));
    reseaux.insert(n);
  }
  CHECK(reseaux.size() == 8);
  CHECK(b.compter("ERC-32") == 0);
  // Une entrée vers D8 : signalée
  b.fil(ptMm(142.24, 101.6), ptMm(142.24, 91.44));
  b.etiquette(ptMm(142.24, 91.44), "D8");
  CHECK(b.compter("ERC-32") == 1);
  CHECK(lireNomBus("D[0..7]")->fin == 7);
  CHECK_FALSE(lireNomBus("D0"));
}

TEST_CASE("7.15 : feuille « 4 × canal », R5 = 10 kΩ au canal 1 et 22 kΩ au canal 2 : trois 10 kΩ et un 22 kΩ", "[L7][STR-22][STR-05]") {
  Banc7 b;
  const std::string s = b.poser("R", 50.8, 50.8, 1), sc = b.poser("C", 76.2, 50.8);
  b.c.composant(b.composant(s))->reference = "R5";
  b.c.composant(b.composant(s))->valeur = "10k";
  b.c.composant(b.composant(sc))->reference = "C5";
  // R5.2 — C5.1 : un réseau propre au canal
  const Pt a = b.broche(s, "2"), z = b.broche(sc, "1");
  b.fil(a, {a.x, z.y});
  b.fil({a.x, z.y}, z);
  CHECK(repeterFeuille(b.c, 0, 4) == 3);
  REQUIRE(b.c.feuilles.size() == 4);
  std::set<std::string> refs;
  for (const auto& k : b.c.composants) refs.insert(k.reference);
  CHECK(refs == std::set<std::string>{"R5", "R205", "R305", "R405", "C5", "C205", "C305", "C405"});
  for (auto& k : b.c.composants)
    if (k.reference == "R205") k.valeur = "22k";
  int dix = 0, vingtDeux = 0;
  for (const auto& l : nomenclature(b.c))
    if (l.valeur == "10k") dix += l.quantite;
    else if (l.valeur == "22k") vingtDeux += l.quantite;
  CHECK(dix == 3);
  CHECK(vingtDeux == 1);
  // Les réseaux propres ne se mélangent pas d'un canal à l'autre
  const Connectivite k = connectivite(b.c);
  CHECK(k.reseau(b.c.parReference("R5")->uuid, "2") != k.reseau(b.c.parReference("R205")->uuid, "2"));
  CHECK(k.reseau(b.c.parReference("R205")->uuid, "2") == k.reseau(b.c.parReference("C205")->uuid, "1"));
  // Quatre groupes d'empreintes ; le canal 1 placé et routé se reproduit sur les trois autres
  mettreAJourCircuit(b.c);
  CHECK(b.c.circuit.empreintes.size() == 8);
  b.c.circuit.contour.largeur = nm(100);
  b.c.circuit.contour.hauteur = nm(60);
  for (auto& e : b.c.circuit.empreintes) {
    if (e.composant == b.c.parReference("R5")->uuid) e.position = ptMm(10, 10);
    if (e.composant == b.c.parReference("C5")->uuid) e.position = ptMm(16, 10);
  }
  const Connectivite k2 = connectivite(b.c);
  for (const auto& l : etatRoutage(b.c, k2, remplirZones(b.c, k2)).chevelu)
    if (l.reseau == k2.nom(b.c.parReference("R5")->uuid, "2")) b.c.circuit.pistes.push_back({nouvelUuid(), l.reseau, Couche::CuivreDessus, l.a, l.b, nm(0.25)});
  CHECK(reproduireCanal(b.c, 0, ptMm(20, 0)) == 6);
  for (const auto& [ref, x] : std::vector<std::pair<std::string, double>>{{"R205", 30}, {"R305", 50}, {"R405", 70}, {"C405", 76}})
    for (const auto& e : b.c.circuit.empreintes)
      if (e.composant == b.c.parReference(ref)->uuid) CHECK(mm(e.position.x) == Approx(x));
  const Connectivite k3 = connectivite(b.c);
  int restantes = 0;
  for (const auto& l : etatRoutage(b.c, k3, remplirZones(b.c, k3)).chevelu)
    for (const char* ref : {"R205", "R305", "R405"})
      restantes += l.reseau == k3.nom(b.c.parReference(ref)->uuid, "2");
  CHECK(restantes == 0);
}

TEST_CASE("7.17 : un électrolytique dont le « + » va à la masse d'un rail de 12 V est une erreur de l'ERC qui vise le composant", "[L7][ERC-45]") {
  Banc7 b;
  const std::string c = b.poser("C_Pol", 76.2, 76.2);
  const std::string gnd = b.poser("GND", 101.6, 101.6), v12 = b.poser("+12V", 50.8, 25.4);
  const Pt plus = b.broche(c, "1"), moins = b.broche(c, "2");
  // Monté à l'envers : « + » vers la masse, « − » vers le +12 V (fils partis de côté : aucun ne passe sur l'autre broche)
  const Pt g = b.broche(gnd, "1"), v = b.broche(v12, "1");
  b.fil(plus, {g.x, plus.y});
  b.fil({g.x, plus.y}, g);
  b.fil(moins, {v.x, moins.y});
  b.fil({v.x, moins.y}, v);
  const Connectivite k = connectivite(b.c);
  const auto erc = verifierERC(b.c, k, NiveauErc::Complet);
  const auto it = std::find_if(erc.begin(), erc.end(), [](const Violation& x) { return x.code == "ERC-45"; });
  REQUIRE(it != erc.end());
  CHECK(it->gravite == Gravite::Erreur);
  CHECK(it->objets == std::vector<std::string>{b.composant(c)});
  CHECK(tensionReseau(Reseau{"", true, {}, {}, {"+3V3"}}).value() == Approx(3.3));
  CHECK(tensionReseau(Reseau{"", true, {}, {}, {"-12V"}}).value() == Approx(-12));
}

TEST_CASE("7.12 : trois composants routés déplacés vers une autre feuille gardent repères, empreintes et pistes ; les liaisons coupées deviennent des étiquettes", "[L7][STR-15]") {
  Banc7 b;
  std::vector<std::string> s;
  for (int i = 0; i < 4; ++i) s.push_back(b.poser("R", 50.8 + 25.4 * i, 50.8, 1));
  for (int i = 0; i < 3; ++i) b.fil(b.broche(s[static_cast<std::size_t>(i)], "2"), b.broche(s[static_cast<std::size_t>(i + 1)], "1"));
  annoter(b.c, false);
  mettreAJourCircuit(b.c);
  b.c.circuit.contour.largeur = nm(60);
  b.c.circuit.contour.hauteur = nm(30);
  for (std::size_t i = 0; i < b.c.circuit.empreintes.size(); ++i) b.c.circuit.empreintes[i].position = ptMm(8 + 12 * static_cast<double>(i), 15);
  Connectivite k = connectivite(b.c);
  for (const auto& l : etatRoutage(b.c, k, remplirZones(b.c, k)).chevelu) b.c.circuit.pistes.push_back({nouvelUuid(), l.reseau, Couche::CuivreDessus, l.a, l.b, nm(0.25)});
  const std::vector<Piste> pistes = b.c.circuit.pistes;
  const std::vector<EmpreintePlacee> empreintes = b.c.circuit.empreintes;
  std::map<std::string, std::string> refs;
  for (const auto& x : b.c.composants) refs[x.uuid] = x.reference;
  Feuille f2;
  f2.uuid = nouvelUuid();
  f2.nom = "Feuille 2";
  b.c.feuilles.push_back(f2);
  const BilanDeplacement bilan = deplacerVersFeuille(b.c, 0, {s[0], s[1], s[2]}, 1);
  CHECK(bilan.symboles == 3);
  CHECK(bilan.fils == 2);
  CHECK(bilan.etiquettes == 2);
  CHECK(b.c.feuilles[1].symboles.size() == 3);
  for (const auto& x : b.c.composants) CHECK(refs[x.uuid] == x.reference);
  CHECK(b.c.circuit.empreintes == empreintes);
  // Les pistes restent reliées : synchroniser ne coupe rien, le chevelu reste vide
  synchroniserReseaux(b.c);
  k = connectivite(b.c);
  CHECK(etatRoutage(b.c, k, remplirZones(b.c, k)).chevelu.empty());
  CHECK(b.c.circuit.pistes.size() == pistes.size());
  CHECK(k.reseau(b.composant(s[2]), "2") == k.reseau(b.composant(s[3]), "1"));
}

TEST_CASE("7.22, 7.23 : congé de 2 mm au coin de deux traits ; contour décalé de 0,25 mm autour de pastilles proches : un seul polygone", "[L7][CAR-26][CAR-27]") {
  Circuit k;
  k.dessins.push_back({DessinCircuit::Forme::Trait, "a", Couche::SerigraphieDessus, {ptMm(0, 0), ptMm(10, 0)}, nm(0.15), false});
  k.dessins.push_back({DessinCircuit::Forme::Trait, "b", Couche::SerigraphieDessus, {ptMm(10, 0), ptMm(10, 10)}, nm(0.15), false});
  std::string raison;
  const std::string arc = poserConge(k, "a", "b", nm(2), &raison);
  INFO(raison);
  REQUIRE_FALSE(arc.empty());
  CHECK(k.dessins[0].points[1] == ptMm(8, 0));
  CHECK(k.dessins[1].points[0] == ptMm(10, 2));
  const DessinCircuit& x = k.dessins[2];
  REQUIRE(x.points.size() == 3);
  // Le milieu de l'arc est à 2 mm du centre (8, 2)
  CHECK(distance(x.points[1], ptMm(8, 2)) == Approx(2e6).margin(2));
  CHECK(poserConge(k, "a", "b", nm(2), &raison).empty());  // les traits ne se touchent plus
  // Trois pastilles proches (0,4 mm d'écart) : un seul polygone ; une quatrième loin : deux
  Polygones p{rectangle(ptMm(0, 0), nm(0.6), nm(0.5)), rectangle(ptMm(1, 0), nm(0.6), nm(0.5)), rectangle(ptMm(2, 0), nm(0.6), nm(0.5))};
  CHECK(contourDecale(p, nm(0.25), Couche::EncombrementDessus).size() == 1);
  p.push_back(rectangle(ptMm(10, 0), nm(0.6), nm(0.5)));
  CHECK(contourDecale(p, nm(0.25), Couche::EncombrementDessus).size() == 2);
}
