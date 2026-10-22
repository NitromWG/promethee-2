// Prométhée : banc d'essais électronique des audits (matrice PCB-01 à PCB-05, FAB-01 ; suite E-01 à E-14).
// Chaque règle ERC et DRC a son cas positif et son cas négatif, les règles de dimension sont essayées pile à la
// limite et juste en dessous, la publication refuse les erreurs critiques et garde une trace vérifiable des
// dérogations. Licence GPL-3.0-only.
#include <algorithm>
#include <chrono>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hachage.hpp"
#include "promethee/kicad.hpp"
#include "promethee/modele.hpp"
#include "promethee/verifs.hpp"

using namespace prom::ecad;
using Catch::Approx;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

namespace {
int compter(const std::vector<Violation>& v, const std::string& code) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
}
bool vise(const std::vector<Violation>& v, const std::string& code, const std::string& objet) {
  return std::any_of(v.begin(), v.end(), [&](const Violation& x) { return x.code == code && std::find(x.objets.begin(), x.objets.end(), objet) != x.objets.end(); });
}

// Banc de schéma : poser des symboles, tirer des fils, poser des étiquettes
struct Banc {
  ConceptionElec c = nouvelleConception("Banc");
  std::string poser(const std::string& id, double x, double y, std::size_t f = 0) { return poserSymbole(c, f, id, ptMm(x, y)); }
  Pt broche(const std::string& sym, const std::string& numero) const {
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles)
        if (s.uuid == sym)
          for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
            if (b.broche->numero == numero) return b.p;
    FAIL("broche introuvable : " << numero);
    return {};
  }
  std::string composant(const std::string& sym) const {
    for (const auto& f : c.feuilles)
      for (const auto& s : f.symboles)
        if (s.uuid == sym) return s.composant;
    return {};
  }
  void fil(Pt a, Pt b, std::size_t f = 0) {
    if (a.x != b.x && a.y != b.y) {
      c.feuilles[f].fils.push_back({nouvelUuid(), a, {b.x, a.y}});
      c.feuilles[f].fils.push_back({nouvelUuid(), {b.x, a.y}, b});
    } else {
      c.feuilles[f].fils.push_back({nouvelUuid(), a, b});
    }
  }
  void etiquette(Pt p, const std::string& texte, bool globale, std::size_t f = 0) {
    Etiquette e;
    e.uuid = nouvelUuid();
    e.texte = texte;
    e.p = p;
    e.type = globale ? TypeEtiquette::Globale : TypeEtiquette::Locale;
    c.feuilles[f].etiquettes.push_back(e);
  }
  std::vector<Violation> erc() const { return verifierERC(c, connectivite(c)); }
};

// Montage d'essai : connecteur J1 (1 : +5V, 2 : GND), résistance R1, LED D1 vers la masse, sur une carte 40 × 30
struct Montage : Banc {
  std::string j, r, d;
  Montage() {
    j = poser("Conn_01x02", 20.32, 50.8);
    r = poser("R", 50.8, 38.1);
    d = poserSymbole(c, 0, "LED", ptMm(63.5, 50.8), 3);
    const std::string p5 = poser("+5V", 30.48, 30.48), gnd = poser("GND", 30.48, 66.04);
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
  // Annoté, placé et routé sur le cuivre du dessus, documenté : publiable
  void completer() {
    annoter(c, false);
    mettreAJourCircuit(c);
    auto& ci = c.circuit;
    ci.contour.largeur = nm(40);
    ci.contour.hauteur = nm(30);
    for (auto& e : ci.empreintes) {
      const std::string ref = c.composant(e.composant)->reference;
      if (ref == "J1") e.position = ptMm(6, 15);
      if (ref == "R1") e.position = ptMm(20, 10);
      if (ref == "D1") e.position = ptMm(30, 20);
    }
    const Connectivite k = connectivite(c);
    const Remplissages z = remplirZones(c, k);
    for (const auto& l : etatRoutage(c, k, z).chevelu) ci.pistes.push_back({nouvelUuid(), l.reseau, Couche::CuivreDessus, l.a, l.b, nm(0.3)});
    for (auto& x : c.composants) {
      x.fabricant = "Exemple";
      x.mpn = "MPN-" + x.reference;
    }
    c.cartouche = {"Témoin d'alimentation", "ESME", "Evan", "A", "2026-10-09", ""};
  }
  std::vector<Violation> drc() const {
    const Connectivite k = connectivite(c);
    return verifierDRC(c, k, remplirZones(c, k));
  }
};

// Carte nue 40 × 30 sans composant, pour les règles de dimension
ConceptionElec carteNue() {
  ConceptionElec c = nouvelleConception("Nue");
  c.circuit.contour.largeur = nm(40);
  c.circuit.contour.hauteur = nm(30);
  return c;
}
std::vector<Violation> drc(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  return verifierDRC(c, k, remplirZones(c, k));
}

std::string dossierEssais(const std::string& nom) {
  const fs::path d = fs::temp_directory_path() / ("promethee_elec_" + nom);
  fs::remove_all(d);
  fs::create_directories(d);
  return d.string();
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------
TEST_CASE("E-01 / PCB-02 : deux sorties reliées, signalées selon les types électriques", "[electronique][PCB-02][E-01]") {
  Banc b;
  const std::string u1 = b.poser("NE555", 50.8, 50.8), u2 = b.poser("NE555", 101.6, 50.8);
  b.fil(b.broche(u1, "3"), b.broche(u2, "3"));  // Q contre Q
  CHECK(compter(b.erc(), "ERC-20") == 1);
  // Une sortie vers une entrée : rien à redire
  Banc n;
  const std::string v1 = n.poser("NE555", 50.8, 50.8), v2 = n.poser("NE555", 101.6, 50.8);
  n.fil(n.broche(v1, "3"), n.broche(v2, "2"));  // Q vers TR
  CHECK(compter(n.erc(), "ERC-20") == 0);
  // Collecteur ouvert avec collecteur ouvert : permis (ET câblé)
  Banc o;
  const std::string w1 = o.poser("NE555", 50.8, 50.8), w2 = o.poser("NE555", 101.6, 50.8);
  o.fil(o.broche(w1, "7"), o.broche(w2, "7"));
  CHECK(compter(o.erc(), "ERC-20") == 0);
}

TEST_CASE("E-02 / PCB-02 : broche non connectée, avec et sans marque explicite", "[electronique][PCB-02][E-02]") {
  Banc b;
  const std::string u = b.poser("NE555", 50.8, 50.8);
  const Pt reset = b.broche(u, "4"), dis = b.broche(u, "7");
  auto erc = b.erc();
  int sur4 = 0;
  for (const auto& v : erc) sur4 += v.code == "ERC-10" && v.position == reset;
  CHECK(sur4 == 1);
  // Entrée libre : erreur ; collecteur ouvert libre : simple avertissement
  for (const auto& v : erc) {
    if (v.code == "ERC-10" && v.position == reset) CHECK(v.gravite == Gravite::Erreur);
    if (v.code == "ERC-10" && v.position == dis) CHECK(v.gravite == Gravite::Avertissement);
  }
  // Marque de non-connexion : plus de diagnostic sur cette broche
  b.c.feuilles[0].nonConnexions.push_back({reset, nouvelUuid()});
  erc = b.erc();
  for (const auto& v : erc) CHECK_FALSE((v.code == "ERC-10" && v.position == reset));
  CHECK(compter(erc, "ERC-11") == 0);
  // Marquée non connectée mais reliée : ERC-11
  b.fil(reset, {reset.x - nm(10.16), reset.y});
  const std::string r = b.poser("R", 20.32, 45.72);
  b.fil({reset.x - nm(10.16), reset.y}, b.broche(r, "2"));
  CHECK(compter(b.erc(), "ERC-11") == 1);
}

TEST_CASE("E-03 / PCB-02 : étiquettes globales sur deux feuilles, locales cantonnées à la leur", "[electronique][PCB-02][E-03]") {
  Banc b;
  Feuille deux;
  deux.uuid = nouvelUuid();
  deux.nom = "Feuille 2";
  b.c.feuilles.push_back(deux);
  const std::string r1 = b.poser("R", 50.8, 50.8, 0), r2 = b.poser("R", 50.8, 50.8, 1);
  annoter(b.c, false);
  b.etiquette(b.broche(r1, "1"), "BUS", true, 0);
  b.etiquette(b.broche(r2, "1"), "BUS", true, 1);
  b.etiquette(b.broche(r1, "2"), "LOC", false, 0);
  b.etiquette(b.broche(r2, "2"), "LOC", false, 1);
  const Connectivite k = connectivite(b.c);
  const std::string c1 = b.composant(r1), c2 = b.composant(r2);
  CHECK(k.reseau(c1, "1") == k.reseau(c2, "1"));
  CHECK(k.nom(c1, "1") == "BUS");
  CHECK(k.reseau(c1, "2") != k.reseau(c2, "2"));  // une étiquette locale ne passe pas d'une feuille à l'autre
  // La liste des réseaux met R1.1 et R2.1 sur BUS
  const std::string liste = netlistTexte(b.c, k);
  CHECK_THAT(liste, ContainsSubstring("BUS"));
  const auto i = liste.find("BUS");
  const std::string ligne = liste.substr(i, liste.find('\n', i) - i);
  CHECK_THAT(ligne, ContainsSubstring("R1"));
  CHECK_THAT(ligne, ContainsSubstring("R2"));
}

TEST_CASE("PCB-02 : chaque règle ERC, cas positif et cas négatif", "[electronique][PCB-02]") {
  SECTION("ERC-01 symbole inconnu") {
    Banc b;
    SymbolePlace s;
    s.uuid = nouvelUuid();
    s.symbole = "symbole-qui-n-existe-pas";
    b.c.feuilles[0].symboles.push_back(s);
    CHECK(compter(b.erc(), "ERC-01") == 1);
    CHECK(compter(Banc().erc(), "ERC-01") == 0);
  }
  SECTION("ERC-02 non annoté, ERC-06 référence en double") {
    Banc b;
    b.poser("R", 50.8, 50.8);
    b.poser("R", 76.2, 50.8);
    CHECK(compter(b.erc(), "ERC-02") == 2);
    annoter(b.c, false);
    CHECK(compter(b.erc(), "ERC-02") == 0);
    CHECK(compter(b.erc(), "ERC-06") == 0);
    b.c.composants[1].reference = b.c.composants[0].reference;
    CHECK(compter(b.erc(), "ERC-06") == 1);
  }
  SECTION("ERC-03 sans empreinte, ERC-04 broche sans pastille, ERC-05 empreinte inconnue") {
    Banc b;
    b.poser("R", 50.8, 50.8);
    annoter(b.c, false);
    CHECK(compter(b.erc(), "ERC-03") + compter(b.erc(), "ERC-04") + compter(b.erc(), "ERC-05") == 0);
    b.c.composants[0].empreinte = "";
    CHECK(compter(b.erc(), "ERC-03") == 1);
    std::string unePastille;
    for (const auto& i : catalogueEmpreintes())
      if (i.pastilles == 1) unePastille = i.id;
    REQUIRE_FALSE(unePastille.empty());
    b.c.composants[0].empreinte = unePastille;  // deux broches, une seule pastille
    CHECK(compter(b.erc(), "ERC-04") >= 1);
    b.c.composants[0].empreinte = "Empreinte_Inexistante_42";
    CHECK(compter(b.erc(), "ERC-05") == 1);
  }
  SECTION("ERC-21 entrée non pilotée, ERC-22 réseau non alimenté") {
    Banc b;
    const std::string u1 = b.poser("NE555", 50.8, 50.8), u2 = b.poser("NE555", 101.6, 50.8);
    b.fil(b.broche(u1, "2"), b.broche(u2, "2"));  // deux entrées seules
    b.fil(b.broche(u1, "8"), b.broche(u2, "8"));  // deux VCC sans source
    CHECK(compter(b.erc(), "ERC-21") >= 1);
    CHECK(compter(b.erc(), "ERC-22") >= 1);
    // Une sortie pilote l'entrée ; un connecteur (passif) alimente les VCC et les GND
    Banc n;
    const std::string v1 = n.poser("NE555", 50.8, 50.8), v2 = n.poser("NE555", 101.6, 50.8), j = n.poser("Conn_01x02", 20.32, 25.4);
    n.fil(n.broche(v1, "3"), n.broche(v2, "2"));
    for (const std::string& u : {v1, v2}) {
      n.fil(n.broche(j, "1"), n.broche(u, "8"));
      n.fil(n.broche(j, "2"), n.broche(u, "1"));
    }
    const auto erc = n.erc();
    for (const auto& v : erc)
      if (v.code == "ERC-21") CHECK(v.position != n.broche(v2, "2"));
    CHECK(compter(erc, "ERC-22") == 0);
  }
  SECTION("ERC-23 deux alimentations reliées, ERC-24 un réseau à deux noms (alias, EL-09)") {
    Banc b;
    const std::string r = b.poser("R", 50.8, 50.8);
    b.etiquette(b.broche(r, "1"), "ALPHA", true);
    b.etiquette(b.broche(r, "1"), "BETA", true);
    CHECK(compter(b.erc(), "ERC-23") == 0);
    CHECK(compter(b.erc(), "ERC-24") == 1);
    CHECK(derogeable("ERC-24"));
    Banc a;
    const std::string ra = a.poser("R", 50.8, 50.8);
    const Pt p = a.broche(ra, "1");
    a.fil(p, {p.x, p.y - nm(5.08)});
    poserSymbole(a.c, 0, "+5V", {p.x, p.y - nm(5.08)});
    a.fil(p, {p.x - nm(5.08), p.y});
    poserSymbole(a.c, 0, "GND", {p.x - nm(5.08), p.y});
    CHECK(compter(a.erc(), "ERC-23") == 1);
    CHECK_FALSE(derogeable("ERC-23"));
    Banc n;
    const std::string r2 = n.poser("R", 50.8, 50.8);
    n.etiquette(n.broche(r2, "1"), "ALPHA", true);
    CHECK(compter(n.erc(), "ERC-23") == 0);
  }
  SECTION("ERC-30 fil qui ne mène nulle part, ERC-31 étiquette seule") {
    Banc b;
    const std::string r = b.poser("R", 50.8, 50.8);
    b.fil(b.broche(r, "1"), {b.broche(r, "1").x - nm(10.16), b.broche(r, "1").y});
    CHECK(compter(b.erc(), "ERC-30") == 1);
    b.etiquette(b.broche(r, "2"), "SEULE", false);
    CHECK(compter(b.erc(), "ERC-31") == 1);
    Banc n;
    n.poser("R", 50.8, 50.8);
    CHECK(compter(n.erc(), "ERC-30") + compter(n.erc(), "ERC-31") == 0);
  }
}

TEST_CASE("E-04 / E-13 : broche sans pastille, erreur critique : publication bloquée même avec une raison", "[electronique][PCB-02][E-04][E-13]") {
  Montage m;
  m.completer();
  REQUIRE(valider(m.c).publiable());
  std::string unePastille;
  for (const auto& i : catalogueEmpreintes())
    if (i.pastilles == 1) unePastille = i.id;
  for (auto& x : m.c.composants)
    if (x.reference == "R1") x.empreinte = unePastille;
  const Validation v = valider(m.c);
  CHECK_FALSE(v.publiable());
  CHECK(v.critiques() >= 1);
  CHECK_FALSE(derogeable("ERC-04"));
  const std::string d = dossierEssais("broche_sans_pastille");
  CHECK_THROWS_WITH(publier(m.c, d, "Evan", "2026-10-09", "Raison quelconque", "essai"), ContainsSubstring("critique"));
  CHECK(m.c.publications.empty());
  CHECK(fs::is_empty(d));  // rien d'écrit
}

// ---------------------------------------------------------------------------------------------------------
TEST_CASE("E-05 / PCB-03 : largeur de piste pile au minimum, puis juste en dessous", "[electronique][PCB-03][E-05]") {
  ConceptionElec c = carteNue();
  const Nm mini = c.circuit.regles.largeurMin;
  c.circuit.pistes.push_back({"p1", "A", Couche::CuivreDessus, ptMm(10, 10), ptMm(20, 10), mini});
  CHECK(compter(drc(c), "DRC-02") == 0);
  c.circuit.pistes[0].largeur = mini - 1;
  const auto v = drc(c);
  CHECK(compter(v, "DRC-02") == 1);
  CHECK(vise(v, "DRC-02", "p1"));
  CHECK(derogeable("DRC-02"));
}

TEST_CASE("E-06 / PCB-03 : isolement pile à la limite, juste en dessous, et cuivres qui se touchent", "[electronique][PCB-03][E-06]") {
  ConceptionElec c = carteNue();
  const Nm iso = c.circuit.regles.isolement, w = nm(0.25);
  c.circuit.pistes.push_back({"a", "A", Couche::CuivreDessus, ptMm(10, 10), ptMm(30, 10), w});
  c.circuit.pistes.push_back({"b", "B", Couche::CuivreDessus, {nm(10), nm(10) + w + iso}, {nm(30), nm(10) + w + iso}, w});
  CHECK(compter(drc(c), "DRC-01") == 0);
  c.circuit.pistes[1].a.y -= 1;
  c.circuit.pistes[1].b.y -= 1;
  CHECK(compter(drc(c), "DRC-01") == 1);
  // Sur des couches différentes : pas de conflit
  c.circuit.pistes[1].couche = Couche::CuivreDessous;
  CHECK(compter(drc(c), "DRC-01") == 0);
  // Cuivres qui se chevauchent : court-circuit, critique
  c.circuit.pistes[1].couche = Couche::CuivreDessus;
  c.circuit.pistes[1].a.y = c.circuit.pistes[1].b.y = nm(10.1);
  const auto v = drc(c);
  CHECK(compter(v, "DRC-21") >= 1);
  CHECK_FALSE(derogeable("DRC-21"));
  CHECK(derogeable("DRC-01"));
}

TEST_CASE("E-07 / PCB-03 : via au perçage et à la couronne pile au minimum, puis non conformes", "[electronique][PCB-03][E-07]") {
  ConceptionElec c = carteNue();
  const Regles& R = c.circuit.regles;
  c.circuit.vias.push_back({"v1", "A", ptMm(20, 15), R.percageMin + 2 * R.anneauMin, R.percageMin});
  auto v = drc(c);
  CHECK(compter(v, "DRC-03") + compter(v, "DRC-04") == 0);
  c.circuit.vias[0].percage = R.percageMin - 1;
  v = drc(c);
  CHECK(vise(v, "DRC-03", "v1"));
  c.circuit.vias[0].percage = R.percageMin;
  c.circuit.vias[0].diametre = R.percageMin + 2 * R.anneauMin - 2;
  v = drc(c);
  CHECK(vise(v, "DRC-04", "v1"));
}

TEST_CASE("E-08 / PCB-03 : cuivre pile à la distance du bord, trop près, puis hors de la carte", "[electronique][PCB-03][E-08]") {
  ConceptionElec c = carteNue();
  const Nm bord = c.circuit.regles.bord, w = nm(0.3);
  c.circuit.pistes.push_back({"p", "A", Couche::CuivreDessus, {bord + w / 2, nm(10)}, {bord + w / 2, nm(20)}, w});
  CHECK(compter(drc(c), "DRC-07") == 0);
  c.circuit.pistes[0].a.x -= 1;
  c.circuit.pistes[0].b.x -= 1;
  CHECK(compter(drc(c), "DRC-07") == 1);
  c.circuit.pistes[0].a.x = c.circuit.pistes[0].b.x = -nm(1);
  CHECK(compter(drc(c), "DRC-06") == 1);
  CHECK_FALSE(derogeable("DRC-06"));
}

TEST_CASE("PCB-03 : couronne des pastilles traversantes, écart entre perçages, perçages qui se recoupent", "[electronique][PCB-03]") {
  Montage m;
  m.completer();
  // Couronne des pastilles du connecteur (traversant), mesurée sur le modèle
  Nm couronne = 0;
  {
    const Connectivite k = connectivite(m.c);
    for (const auto& p : pastilles(m.c, k))
      if (p.pastille.type == TypePastille::Traversante) {
        const Nm r = (std::min(p.pastille.largeur, p.pastille.hauteur) - p.pastille.percage) / 2;
        couronne = couronne == 0 ? r : std::min(couronne, r);
      }
  }
  REQUIRE(couronne > 0);
  m.c.circuit.regles.anneauMin = couronne;
  CHECK(compter(m.drc(), "DRC-11") == 0);
  m.c.circuit.regles.anneauMin = couronne + 1;
  CHECK(compter(m.drc(), "DRC-11") >= 1);
  // Deux vias d'un même réseau : écart pile à la règle, puis en dessous, puis qui se recoupent
  ConceptionElec c = carteNue();
  const Regles& R = c.circuit.regles;
  const Nm d = 2 * (R.viaPercage / 2) + R.ecartTrous;  // centre à centre
  c.circuit.vias.push_back({"v1", "A", ptMm(20, 15), R.viaDiametre, R.viaPercage});
  c.circuit.vias.push_back({"v2", "A", {nm(20) + d, nm(15)}, R.viaDiametre, R.viaPercage});
  CHECK(compter(drc(c), "DRC-12") == 0);
  c.circuit.vias[1].p.x -= 1;
  CHECK(compter(drc(c), "DRC-12") == 1);
  c.circuit.vias[1].p.x = nm(20) + R.viaPercage / 2;
  CHECK(compter(drc(c), "DRC-14") == 1);
  CHECK_FALSE(derogeable("DRC-14"));
}

TEST_CASE("PCB-03 : sérigraphie sur une pastille, et bibliothèque intégrée sans ce défaut", "[electronique][PCB-03]") {
  // La bibliothèque générée ne pose jamais sa sérigraphie sur ses pastilles : chaque empreinte du catalogue, posée
  // seule sur une carte, passe la DRC-13 (Q6 ; l'essai ne regardait que la carte d'exemple)
  for (const auto& info : catalogueEmpreintes()) {
    INFO(info.id);
    ConceptionElec seule = carteNue();
    EmpreintePlacee e;
    e.composant = nouvelUuid();
    e.empreinte = info.id;
    seule.circuit.contour.largeur = nm(200);
    seule.circuit.contour.hauteur = nm(200);
    e.position = ptMm(100, 100);
    seule.circuit.empreintes.push_back(e);
    CHECK(compter(drc(seule), "DRC-13") == 0);
  }
  ConceptionElec ex = exempleElectronique();
  CHECK(compter(drc(ex), "DRC-13") == 0);
  // R1 déplacé pour que son premier trait de sérigraphie passe par le centre de la pastille 1 de D1
  Montage m;
  m.completer();
  CHECK(compter(m.drc(), "DRC-13") == 0);
  Pt pastilleD1;
  for (const auto& p : pastilles(m.c, connectivite(m.c)))
    if (p.reference == "D1" && p.numero == "1") pastilleD1 = p.pastille.position;
  for (auto& e : m.c.circuit.empreintes) {
    if (m.c.composant(e.composant)->reference != "R1") continue;
    const DefEmpreinte* d = empreinteEnCache(e.empreinte);
    REQUIRE(d);
    const auto t = std::find_if(d->traits.begin(), d->traits.end(), [](const TraitEmpreinte& x) { return x.couche == Couche::SerigraphieDessus && x.rayon == 0; });
    REQUIRE(t != d->traits.end());
    e.position = {0, 0};
    const Pt milieu = versCarte(e, {(t->a.x + t->b.x) / 2, (t->a.y + t->b.y) / 2});
    e.position = {pastilleD1.x - milieu.x, pastilleD1.y - milieu.y};
  }
  const auto v = m.drc();
  CHECK(compter(v, "DRC-13") >= 1);
  CHECK(std::any_of(v.begin(), v.end(), [](const Violation& x) { return x.code == "DRC-13" && x.message.find("D1.1") != std::string::npos; }));
}

TEST_CASE("E-04 / PCB-03 : classe de réseau et courant, largeur IPC-2221", "[electronique][PCB-03]") {
  // Valeurs de référence de l'abaque IPC-2221 (cuivre 35 µm, échauffement 10 °C)
  CHECK(mm(largeurIPC2221(1, 10, 0.035, true)) == Approx(0.300).margin(0.002));
  CHECK(mm(largeurIPC2221(2, 10, 0.035, true)) == Approx(0.781).margin(0.003));
  CHECK(mm(largeurIPC2221(1, 10, 0.035, false)) == Approx(0.781).margin(0.003));  // couche interne : moitié du courant admis
  CHECK(largeurIPC2221(0, 10, 0.035, true) == 0);
  ConceptionElec c = carteNue();
  ClasseReseau puissance;
  puissance.nom = "Puissance";
  puissance.largeur = nm(0.5);
  c.circuit.regles.classes.push_back(puissance);
  c.circuit.regles.classeReseau["MOTEUR"] = "Puissance";
  c.circuit.pistes.push_back({"m", "MOTEUR", Couche::CuivreDessus, ptMm(10, 10), ptMm(30, 10), nm(0.5)});
  // Sans courant renseigné : aucune hypothèse tirée du nom « MOTEUR »
  CHECK(compter(drc(c), "DRC-31") + compter(drc(c), "DRC-32") == 0);
  c.circuit.regles.classes[0].courant = 2;
  auto v = drc(c);
  CHECK(compter(v, "DRC-31") == 1);
  CHECK(compter(v, "DRC-32") == 1);
  CHECK(vise(v, "DRC-31", "m"));
  c.circuit.pistes[0].largeur = nm(0.8);
  c.circuit.regles.classes[0].largeur = nm(0.8);
  v = drc(c);
  CHECK(compter(v, "DRC-31") + compter(v, "DRC-32") == 0);
  // Le courant se garde dans le fichier
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  REQUIRE(relue.circuit.regles.classes.size() == 1);
  CHECK(relue.circuit.regles.classes[0].courant == Approx(2));
  CHECK(relue.circuit.regles.classes[0].echauffement == Approx(10));
}

TEST_CASE("E-09 / PCB-03 : zone de cuivre refaite après une piste, isolée d'elle, sans court-circuit, déterministe", "[electronique][PCB-03][E-09]") {
  Montage m;
  m.completer();
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = "GND";
  z.couche = Couche::CuivreDessous;
  z.contour = {ptMm(0, 0), ptMm(40, 0), ptMm(40, 30), ptMm(0, 30)};
  m.c.circuit.zones.push_back(z);
  Connectivite k = connectivite(m.c);
  const Remplissages avant = remplirZones(m.c, k);
  REQUIRE(avant.count(z.uuid));
  const double aireAvant = aireMm2(avant.at(z.uuid));
  // Une piste d'un autre réseau traverse la zone au dos
  m.c.circuit.pistes.push_back({"traverse", "+5V", Couche::CuivreDessous, ptMm(3, 25), ptMm(37, 25), nm(0.4)});
  k = connectivite(m.c);
  const Remplissages apres = remplirZones(m.c, k);
  const double aireApres = aireMm2(apres.at(z.uuid));
  CHECK(aireApres < aireAvant);
  // Partout au moins l'isolement entre la zone et la piste : distance du segment au cuivre (trous compris)
  const Nm iso = m.c.circuit.regles.isolement;
  const Polygones& cuivre = apres.at(z.uuid);
  const Pt pa = ptMm(3, 25), pb = ptMm(37, 25);
  double ecart = (dedans(cuivre, pa) || dedans(cuivre, pb)) ? 0.0 : 1e18;
  for (const auto& poly : cuivre)
    for (std::size_t i = 0; i < poly.size(); ++i) ecart = std::min(ecart, distanceSegments(poly[i], poly[(i + 1) % poly.size()], pa, pb));
  CHECK(ecart - static_cast<double>(nm(0.2)) >= static_cast<double>(iso) - 1.0);
  const auto v = verifierDRC(m.c, k, apres);
  CHECK(compter(v, "DRC-21") == 0);
  CHECK(compter(v, "DRC-24") == 0);
  // Piste élargie sans refaire le remplissage : le contrôle indépendant du cuivre voit l'isolement manqué
  m.c.circuit.pistes.back().largeur = nm(0.6);
  const auto v2 = verifierDRC(m.c, connectivite(m.c), apres);
  CHECK(compter(v2, "DRC-24") == 1);
  CHECK(compter(v2, "DRC-21") == 0);
  m.c.circuit.pistes.back().largeur = nm(0.4);
  // Remplir deux fois donne exactement le même cuivre
  const Remplissages encore = remplirZones(m.c, k);
  CHECK(aireMm2(encore.at(z.uuid)) == aireApres);
  CHECK(encore.at(z.uuid).size() == apres.at(z.uuid).size());
  // Zone d'un réseau sans pastille : vide après remplissage, signalée
  Zone seule = z;
  seule.uuid = nouvelUuid();
  seule.reseau = "INCONNU";
  seule.couche = Couche::CuivreDessus;
  m.c.circuit.zones.push_back(seule);
  k = connectivite(m.c);
  CHECK(compter(verifierDRC(m.c, k, remplirZones(m.c, k)), "DRC-23") == 1);
}

TEST_CASE("E-10 / PCB-04 : routeur simple avec obstacle et changement de couche, sans violation ajoutée ; liaisons restantes listées avec leur raison", "[electronique][PCB-04][E-10]") {
  Montage m;
  annoter(m.c, false);
  mettreAJourCircuit(m.c);
  m.c.circuit.contour.largeur = nm(40);
  m.c.circuit.contour.hauteur = nm(30);
  for (auto& e : m.c.circuit.empreintes) {
    const std::string ref = m.c.composant(e.composant)->reference;
    if (ref == "J1") e.position = ptMm(6, 15);
    if (ref == "R1") e.position = ptMm(20, 10);
    if (ref == "D1") e.position = ptMm(30, 20);
  }
  // Obstacle : une piste d'un autre réseau sur le chemin direct, au dessus
  m.c.circuit.pistes.push_back({"obstacle", "AUTRE", Couche::CuivreDessus, ptMm(13, 3), ptMm(13, 27), nm(0.3)});
  const auto avant = m.drc();
  const BilanRoutage b = routerSimple(m.c);
  const auto apres = m.drc();
  for (const char* code : {"DRC-01", "DRC-21", "DRC-06", "DRC-07", "DRC-12", "DRC-14"}) CHECK(compter(apres, code) <= compter(avant, code));
  CHECK(b.liaisons + b.restantes >= 1);
  CHECK(b.restantes == static_cast<int>(b.nonRoutees.size()));
  {
    const Connectivite k = connectivite(m.c);
    CHECK(etatRoutage(m.c, k, remplirZones(m.c, k)).chevelu.size() == b.nonRoutees.size());
  }
  // Changement de couche : un mur de cuivre étranger coupe toute la face du dessus entre R1 et D1 ; les liaisons
  // qui le traversent passent au dos par des vias, sans violation ajoutée, et le bilan compte juste
  Montage w;
  annoter(w.c, false);
  mettreAJourCircuit(w.c);
  w.c.circuit.contour.largeur = nm(40);
  w.c.circuit.contour.hauteur = nm(30);
  for (auto& e : w.c.circuit.empreintes) {
    const std::string ref = w.c.composant(e.composant)->reference;
    if (ref == "J1") e.position = ptMm(6, 15);
    if (ref == "R1") e.position = ptMm(20, 10);
    if (ref == "D1") e.position = ptMm(30, 20);
  }
  w.c.circuit.pistes.push_back({"mur", "MUR", Couche::CuivreDessus, ptMm(25, 1), ptMm(25, 29), nm(0.3)});
  std::size_t aFaire = 0;
  {
    const Connectivite k = connectivite(w.c);
    aFaire = etatRoutage(w.c, k, remplirZones(w.c, k)).chevelu.size();
  }
  const auto avantMur = w.drc();
  const BilanRoutage bm = routerSimple(w.c);
  const auto apresMur = w.drc();
  for (const char* code : {"DRC-01", "DRC-21", "DRC-06", "DRC-07", "DRC-12", "DRC-14"}) CHECK(compter(apresMur, code) <= compter(avantMur, code));
  CHECK(bm.vias >= 2);
  CHECK(static_cast<int>(w.c.circuit.vias.size()) == bm.vias);
  CHECK(std::any_of(w.c.circuit.pistes.begin(), w.c.circuit.pistes.end(), [](const Piste& p) { return p.couche == Couche::CuivreDessous; }));
  CHECK(static_cast<std::size_t>(bm.liaisons + bm.restantes) == aFaire);
  {
    const Connectivite k = connectivite(w.c);
    CHECK(etatRoutage(w.c, k, remplirZones(w.c, k)).chevelu.size() == static_cast<std::size_t>(bm.restantes));
  }
  // Carte impossible : la pastille 1 de R1 enfermée dans un anneau de cuivre étranger, sur les deux faces
  Montage f;
  annoter(f.c, false);
  mettreAJourCircuit(f.c);
  f.c.circuit.contour.largeur = nm(40);
  f.c.circuit.contour.hauteur = nm(30);
  for (auto& e : f.c.circuit.empreintes) {
    const std::string ref = f.c.composant(e.composant)->reference;
    if (ref == "J1") e.position = ptMm(6, 15);
    if (ref == "R1") e.position = ptMm(20, 10);
    if (ref == "D1") e.position = ptMm(30, 20);
  }
  Pt centre{};
  {
    const Connectivite k = connectivite(f.c);
    for (const auto& p : pastilles(f.c, k))
      if (p.reference == "R1" && p.numero == "1") centre = p.pastille.position;
  }
  const Nm a = nm(0.9);
  for (const Couche L : {Couche::CuivreDessus, Couche::CuivreDessous}) {
    const std::vector<Pt> coins = {{centre.x - a, centre.y - a}, {centre.x + a, centre.y - a}, {centre.x + a, centre.y + a}, {centre.x - a, centre.y + a}};
    for (std::size_t i = 0; i < 4; ++i) f.c.circuit.pistes.push_back({nouvelUuid(), "ANNEAU", L, coins[i], coins[(i + 1) % 4], nm(0.25)});
  }
  const BilanRoutage bi = routerSimple(f.c);
  CHECK_FALSE(bi.complet());
  REQUIRE(bi.restantes >= 1);
  for (const auto& l : bi.nonRoutees) {
    CHECK_FALSE(l.raison.empty());
    CHECK_FALSE(l.reseau.empty());
  }
  CHECK_THAT(bi.nonRoutees.front().raison, ContainsSubstring("à router à la main"));
  // Et la carte ne se publie pas : liaison non routée, erreur critique
  const Validation v = valider(f.c);
  CHECK(v.critiques() >= 1);
}

TEST_CASE("E-13 : publication depuis un instantané, dérogation tracée, empreintes SHA-256, dossier immuable", "[electronique][E-13][E-14][FAB-01]") {
  Montage m;
  m.completer();
  const std::string racine = dossierEssais("publication");
  std::vector<std::string> fichiers;
  const std::string empreinte = empreinteConception(m.c);
  CHECK(empreinte.size() == 64);
  const Publication p = publier(m.c, racine, "Evan", "2026-10-09 10:00", "", "essai", &fichiers);
  CHECK(p.empreinte == empreinte);
  CHECK(m.c.verrouillee);
  const fs::path dossier = fs::path(racine) / p.dossier;
  REQUIRE(fs::exists(dossier / "SHA256SUMS.txt"));
  REQUIRE(fs::exists(dossier / "conception.json"));
  // Chaque fichier a son empreinte, recalculée ici ; la liste couvre tout le dossier sauf SHA256SUMS.txt
  std::size_t surDisque = 0;
  for (const auto& e : fs::recursive_directory_iterator(dossier)) surDisque += e.is_regular_file() ? 1 : 0;
  CHECK(p.fichiers.size() + 1 == surDisque);
  for (const auto& [f, h] : p.fichiers) CHECK(prom::sha256Hex(prom::lireFichier(dossier / f)) == h);
  // L'instantané se relit et a la même empreinte que la publication
  const ConceptionElec relue = conceptionDepuisJson(nlohmann::ordered_json::parse(prom::lireFichier(dossier / "conception.json")));
  CHECK(empreinteConception(relue) == p.empreinte);
  // La fiche cite les règles figées et la version du logiciel
  const std::string fiche = prom::lireFichier(dossier / "PUBLICATION.txt");
  CHECK_THAT(fiche, ContainsSubstring("Règles de conception"));
  CHECK_THAT(fiche, ContainsSubstring(empreinte));
  CHECK(verifierPublication(p, racine).empty());
  // Un fichier modifié après coup est repéré
  {
    std::ofstream f(dossier / "Gerber" / "Banc-F_Cu.gbr", std::ios::app);
    f << "G04 retouche*\n";
  }
  const auto ecarts = verifierPublication(p, racine);
  REQUIRE(ecarts.size() == 1);
  CHECK_THAT(ecarts[0], ContainsSubstring("modifié"));
  // Révision B avec une erreur dérogeable : refusée sans raison, publiée avec, et la raison est tracée
  nouvelleRevision(m.c);
  for (auto& x : m.c.composants)
    if (x.reference == "R1") x.valeur = "";  // BOM-01 : dérogeable
  CHECK_THROWS_WITH(publier(m.c, racine, "Evan", "2026-10-10", "", "essai"), ContainsSubstring("dérogation"));
  const Publication p2 = publier(m.c, racine, "Evan", "2026-10-10 11:00", "Valeur à choisir au montage", "essai");
  REQUIRE(p2.violations.size() == 1);
  CHECK_THAT(p2.violations[0], ContainsSubstring("BOM-01"));
  const std::string fiche2 = prom::lireFichier(fs::path(racine) / p2.dossier / "PUBLICATION.txt");
  CHECK_THAT(fiche2, ContainsSubstring("DÉROGATION"));
  CHECK_THAT(fiche2, ContainsSubstring("Valeur à choisir au montage"));
  CHECK_THAT(fiche2, ContainsSubstring("Evan"));
  CHECK_THAT(fiche2, ContainsSubstring("2026-10-10 11:00"));
  // La première publication n'a pas bougé (hors la retouche volontaire ci-dessus)
  CHECK(verifierPublication(p, racine).size() == 1);
  // Révision C : court-circuit, critique, aucune dérogation possible
  nouvelleRevision(m.c);
  for (auto& x : m.c.composants)
    if (x.reference == "R1") x.valeur = "1k";
  m.c.circuit.pistes.push_back({"court", "GND", Couche::CuivreDessus, ptMm(1, 1), ptMm(39, 29), nm(0.3)});
  CHECK_THROWS_WITH(publier(m.c, racine, "Evan", "2026-10-11", "Je sais ce que je fais", "essai"), ContainsSubstring("critique"));
  CHECK_FALSE(m.c.verrouillee);
  // Aucun dossier à moitié écrit ne traîne
  for (const auto& e : fs::directory_iterator(racine)) CHECK(e.path().filename().string().find(".ecriture") == std::string::npos);
}

TEST_CASE("E-14 / FAB-01 : dossier de fabrication contrôlé (couches, unités, contour, perçages, placement)", "[electronique][E-14][FAB-01]") {
  ConceptionElec ex = exempleElectronique();
  const std::string d = dossierEssais("fabrication");
  const auto ecrits = ecrireDossierFabrication(ex, d, "essai");
  CHECK(ecrits.size() >= 14);
  const auto defauts = controlerDossierFabrication(ex, d);
  for (const auto& x : defauts) INFO(x);
  CHECK(defauts.empty());
  // Un Gerber sans fin de fichier, un perçage effacé : repérés
  std::string base;
  for (const auto& f : ecrits)
    if (f.find("Edge_Cuts") != std::string::npos) base = f;
  REQUIRE_FALSE(base.empty());
  {
    std::string g = prom::lireFichier(fs::path(d) / base);
    g.erase(g.rfind("M02*"));
    prom::ecrireFichierAtomique(fs::path(d) / base, g);
  }
  std::string drl;
  for (const auto& f : ecrits)
    if (f.find("-PTH.drl") != std::string::npos) drl = f;
  fs::remove(fs::path(d) / drl);
  const auto defauts2 = controlerDossierFabrication(ex, d);
  CHECK(defauts2.size() >= 2);
  // Carte à quatre couches : les perçages traversants vont de L1 à L4, le dessous est L4
  ConceptionElec quatre = exempleElectronique();
  quatre.circuit.empilage.couchesCuivre = 4;
  const Connectivite k4 = connectivite(quatre);
  const ModeleFabrication m4 = modeleFabrication(quatre, k4, remplirZones(quatre, k4));
  CHECK_THAT(excellon(m4, true, "essai"), ContainsSubstring("TF.FileFunction,Plated,1,4,PTH"));
  CHECK_THAT(excellon(m4, false, "essai"), ContainsSubstring("TF.FileFunction,NonPlated,1,4,NPTH"));
  CHECK_THAT(gerber(m4, Couche::CuivreDessous, "essai"), ContainsSubstring("%TF.FileFunction,Copper,L4,Bot*%"));
  CHECK_THAT(gerber(m4, Couche::CuivreInt1, "essai"), ContainsSubstring("%TF.FileFunction,Copper,L2,Inr*%"));
  CHECK_THAT(excellon(modeleFabrication(ex, connectivite(ex), remplirZones(ex, connectivite(ex))), true, "essai"), ContainsSubstring("TF.FileFunction,Plated,1,2,PTH"));
}

TEST_CASE("E-12 / PCB-05 : hauteurs estimées, génériques ou vérifiées ; contrôle du boîtier complet ou non", "[electronique][PCB-05][E-12]") {
  // Carte KiCad : hauteurs estimées d'après les noms d'empreinte
  const prom::CarteKicad carte = prom::lireFichierKicad(std::string(PROMETHEE_DONNEES) + "/kicad/ecc83-pp.kicad_pcb");
  prom::Projet p = prom::projetDepuisKicad(carte, "ecc83");
  REQUIRE_FALSE(p.composants.empty());
  for (const auto& k : p.composants) CHECK(k.sourceHauteur == "estimee");
  prom::ControleBoitier c = prom::controleBoitier(p);
  CHECK_FALSE(c.complet);
  CHECK(c.nonVerifies.size() == p.composants.size());
  CHECK_THAT(c.resume(), ContainsSubstring("incomplet"));
  // La provenance se garde dans le fichier
  const prom::Projet relu = prom::normaliser(prom::versJson(p));
  CHECK(relu.composants.front().sourceHauteur == "estimee");
  // Toutes vérifiées : contrôle complet
  for (auto& k : p.composants) k.sourceHauteur = "verifiee";
  CHECK(prom::controleBoitier(p).complet);
  // Carte conçue dans Prométhée : corps générique des empreintes, puis hauteur confirmée sur la fiche technique
  prom::Projet q = prom::projetVide();
  q.electronique.push_back(exempleElectronique());
  prom::appliquerCarteElectronique(q, 0);
  REQUIRE_FALSE(q.composants.empty());
  for (const auto& k : q.composants) CHECK(k.sourceHauteur == "nominale");
  CHECK_FALSE(prom::controleBoitier(q).complet);
  for (auto& x : q.electronique[0].composants) {
    x.hauteurVerifiee = true;
    if (x.reference == "C1") x.hauteur = 12.5;
  }
  prom::appliquerCarteElectronique(q, 0);
  CHECK(prom::controleBoitier(q).complet);
  bool trouve = false;
  for (const auto& k : q.composants)
    if (k.ref == "C1") {
      trouve = true;
      CHECK(k.h == Approx(12.5));
    }
  CHECK(trouve);
  // Une hauteur vérifiée qui dépasse le couvercle reste une collision signalée
  prom::Projet e = prom::exemple();
  for (auto& k : e.composants) k.sourceHauteur = "verifiee";
  e.boitier.hauteurAuto = false;
  for (auto& k : e.composants)
    if (k.ref == "C1") k.h = 40;
  const auto pb = prom::verifier(e, prom::deriver(e));
  CHECK_FALSE(pb.empty());
}

// ---------------------------------------------------------------------------------------------------------
namespace {
// Compte d'un motif dans un texte
std::size_t occurrences(const std::string& t, const std::string& m) {
  std::size_t n = 0;
  for (std::size_t i = t.find(m); i != std::string::npos; i = t.find(m, i + m.size())) ++n;
  return n;
}
// Expression S sans sa mise en page : blancs réduits à une espace, aucune avant « ) » ni après « ( »
std::string compacte(const std::string& t) {
  std::string r;
  bool blanc = false;
  for (const char c : t) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      blanc = true;
      continue;
    }
    if (blanc && !r.empty() && r.back() != '(' && c != ')') r += ' ';
    blanc = false;
    r += c;
  }
  return r;
}
// Expression parenthésée complète qui commence au premier « motif » (vide s'il n'y en a pas)
std::string sousExpression(const std::string& t, const std::string& motif) {
  const std::size_t debut = t.find(motif);
  if (debut == std::string::npos) return {};
  int profondeur = 0;
  bool chaine = false;
  for (std::size_t i = debut; i < t.size(); ++i) {
    const char c = t[i];
    if (c == '"' && (i == 0 || t[i - 1] != '\\')) chaine = !chaine;
    if (chaine) continue;
    if (c == '(') ++profondeur;
    if (c == ')' && --profondeur == 0) return t.substr(debut, i - debut + 1);
  }
  return t.substr(debut);
}
}  // namespace

TEST_CASE("E-11 / PCB-01 : aller-retour KiCad sans perte (comparaison sémantique)", "[electronique][PCB-01][E-11]") {
  for (const char* nom : {"ecc83-pp.kicad_pcb", "StickHub.kicad_pcb"}) {
    INFO(nom);
    const std::string chemin = std::string(PROMETHEE_DONNEES) + "/kicad/" + nom;
    const std::string original = prom::lireFichier(prom::cheminFichier(chemin));
    const prom::CarteKicad avant = prom::lireKicad(original);
    prom::Projet p = prom::projetDepuisKicad(avant, "essai");
    const std::string ecrit = prom::kicadMisAJour(original, p, prom::deriver(p));
    const prom::CarteKicad apres = prom::lireKicad(ecrit);
    // Contour, empreintes (référence, position, rotation, face) identiques à 1 µm près
    CHECK(apres.titre == avant.titre);
    CHECK(apres.epaisseur == Approx(avant.epaisseur));
    REQUIRE(apres.contours.size() == avant.contours.size());
    CHECK(apres.xmin == Approx(avant.xmin).margin(1e-3));
    CHECK(apres.xmax == Approx(avant.xmax).margin(1e-3));
    CHECK(apres.ymin == Approx(avant.ymin).margin(1e-3));
    CHECK(apres.ymax == Approx(avant.ymax).margin(1e-3));
    REQUIRE(apres.empreintes.size() == avant.empreintes.size());
    std::map<std::string, prom::EmpreinteKicad> parRef;
    for (const auto& e : avant.empreintes) parRef[e.ref + "|" + e.nom] = e;
    for (const auto& e : apres.empreintes) {
      const auto it = parRef.find(e.ref + "|" + e.nom);
      REQUIRE(it != parRef.end());
      CHECK(e.position.x == Approx(it->second.position.x).margin(1e-3));
      CHECK(e.position.y == Approx(it->second.position.y).margin(1e-3));
      CHECK(e.rotation == Approx(it->second.rotation).margin(1e-6));
      CHECK(e.dessous == it->second.dessous);
    }
    // Rien de ce que Prométhée ne modifie pas ne disparaît : réseaux, pistes, vias, zones, textes, modèles 3D
    for (const char* motif : {"(net ", "(segment", "(via", "(zone", "(gr_text", "(model ", "(arc", "(footprint ", "(pad "})
      CHECK(occurrences(ecrit, motif) == occurrences(original, motif));
    // Définition des couches, empilage et réglages de la carte : mêmes expressions (la mise en page peut changer)
    for (const char* bloc : {"(layers", "(setup", "(general"}) {
      INFO(bloc);
      CHECK(compacte(sousExpression(ecrit, bloc)) == compacte(sousExpression(original, bloc)));
      CHECK_FALSE(sousExpression(original, bloc).empty());
    }
  }
  // Projet exporté vers une carte KiCad neuve puis relu : contour et trous de fixation
  const prom::Projet ex = prom::exemple();
  const prom::CarteKicad neuve = prom::lireKicad(prom::kicadDepuisProjet(ex, prom::deriver(ex)));
  CHECK(neuve.xmax - neuve.xmin == Approx(ex.carte.L).margin(1e-6));
  CHECK(neuve.ymax - neuve.ymin == Approx(ex.carte.W).margin(1e-6));
  int fixations = 0;
  for (const auto& e : neuve.empreintes) fixations += e.fixation;
  CHECK(fixations == static_cast<int>(ex.trous.size()));
}

TEST_CASE("QA-03 : grande carte, temps et stabilité des calculs (DRC, zones, routage, fabrication)", "[electronique][QA-03]") {
  // 120 résistances en grille, reliées deux à deux, zone de masse : mesure de bout en bout
  const auto debut = std::chrono::steady_clock::now();
  ConceptionElec c = nouvelleConception("Grande carte");
  const int n = 120;
  std::vector<std::string> syms;
  for (int i = 0; i < n; ++i) syms.push_back(poserSymbole(c, 0, "R", ptMm(25.4 + 12.7 * (i % 12), 25.4 + 12.7 * (i / 12))));
  annoter(c, false);
  for (int i = 0; i + 1 < n; i += 2) {
    Pt a{}, b{};
    for (const auto& s : c.feuilles[0].symboles) {
      if (s.uuid == syms[static_cast<std::size_t>(i)])
        for (const auto& bp : brochesPlacees(s, *c.symbole(s.symbole))) if (bp.broche->numero == "2") a = bp.p;
      if (s.uuid == syms[static_cast<std::size_t>(i + 1)])
        for (const auto& bp : brochesPlacees(s, *c.symbole(s.symbole))) if (bp.broche->numero == "1") b = bp.p;
    }
    c.feuilles[0].fils.push_back({nouvelUuid(), a, {b.x, a.y}});
    c.feuilles[0].fils.push_back({nouvelUuid(), {b.x, a.y}, b});
  }
  mettreAJourCircuit(c);
  c.circuit.contour.largeur = nm(160);
  c.circuit.contour.hauteur = nm(110);
  for (std::size_t i = 0; i < c.circuit.empreintes.size(); ++i)
    c.circuit.empreintes[i].position = ptMm(8 + 12.0 * static_cast<double>(i % 12), 8 + 10.0 * static_cast<double>(i / 12));
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = "GND";
  z.couche = Couche::CuivreDessous;
  z.contour = {ptMm(0, 0), ptMm(160, 0), ptMm(160, 110), ptMm(0, 110)};
  c.circuit.zones.push_back(z);
  const BilanRoutage b = routerSimple(c);
  CHECK(b.liaisons + b.restantes >= n / 2);
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  const auto v = verifierDRC(c, k, r);
  CHECK(compter(v, "DRC-21") == 0);
  const std::string d = dossierEssais("grande");
  ecrireDossierFabrication(c, d, "essai");
  CHECK(controlerDossierFabrication(c, d).empty());
  const double secondes = std::chrono::duration<double>(std::chrono::steady_clock::now() - debut).count();
  CHECK(secondes < 300);
}

TEST_CASE("EL-05 EL-11 catalogue d'empreintes complet : chaque entrée se génère, écarts internes", "[electronique][EL-05][EL-11]") {
  // Chaque empreinte annoncée au catalogue se génère (EL-11 : LED_D3.0 et LED_D5.0 étaient introuvables)
  REQUIRE(catalogueEmpreintes().size() >= 102);
  for (const auto& info : catalogueEmpreintes()) {
    INFO(info.id);
    const auto d = genererEmpreinte(info.id);
    REQUIRE(d);
    std::set<std::string> numeros;
    for (const auto& pa : d->pastilles)
      if (!pa.numero.empty()) numeros.insert(pa.numero);
    CHECK(static_cast<int>(numeros.size()) == info.pastilles);
    // Écart entre pastilles de numéros différents : au moins 0,2 mm (isolement du profil Standard) (EL-05)
    double mini = 1e18;
    for (std::size_t a = 0; a < d->pastilles.size(); ++a)
      for (std::size_t b = a + 1; b < d->pastilles.size(); ++b)
        if (d->pastilles[a].numero != d->pastilles[b].numero)
          mini = std::min(mini, distancePolygones(formePastille(d->pastilles[a]), formePastille(d->pastilles[b])));
    CHECK(mini >= 199'000);
  }
  CHECK(genererEmpreinte("LED_D3.0"));
  CHECK(genererEmpreinte("LED_D5.0"));
  CHECK_FALSE(genererEmpreinte("LED_D5.x"));
}

TEST_CASE("EL-47 îlot troué écrit en une seule région à coupures", "[electronique][EL-47]") {
  const Polygone ext{ptMm(0, 0), ptMm(10, 0), ptMm(10, 10), ptMm(0, 10)};
  const Polygones trous{{ptMm(2, 2), ptMm(4, 2), ptMm(4, 4), ptMm(2, 4)}, {ptMm(6, 5), ptMm(8, 5), ptMm(8, 8), ptMm(6, 8)}};
  const Polygone r = regionAvecCoupures(ext, trous);
  double a = 0;
  for (std::size_t i = 0; i < r.size(); ++i) {
    const Pt u = r[i], v = r[(i + 1) % r.size()];
    a += (mm(u.x) * mm(v.y) - mm(v.x) * mm(u.y)) / 2;
  }
  CHECK(std::abs(std::abs(a) - (100 - 4 - 6)) < 1e-6);
  // L'exemple : une région par îlot de zone, plus de triangles
  const ConceptionElec c = exempleElectronique();
  const Connectivite k = connectivite(c);
  const Remplissages z = remplirZones(c, k);
  std::size_t ilotsZones = 0;
  for (const auto& [id, cuivre] : z) ilotsZones += ilots(cuivre).size();
  const ModeleFabrication m = modeleFabrication(c, k, z);
  std::size_t regions = 0;
  for (const auto& cf : m.couches)
    if (estCuivre(cf.couche))
      for (const auto& p : cf.primitives) regions += p.type == PrimitiveFab::Type::Region && p.fonction == "Conductor";
  CHECK(regions == ilotsZones);
}
