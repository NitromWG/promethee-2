// Prométhée : essais du lot L2 de l'audit du 15 octobre 2026 (« Schéma interactif »), côté noyau : repères donnés
// à la pose, article mécanique posé sur le circuit, et les fonctions des tâches suivantes du lot.
// Licence GPL-3.0-only.
#include <algorithm>
#include <set>
#include <map>
#include <optional>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electronique.hpp"
#include "promethee/electronique_parcours.hpp"
#include "promethee/migrations.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;

TEST_CASE("2.2 : le premier repère libre d'un préfixe", "[L2]") {
  ConceptionElec c = nouvelleConception("Repères");
  CHECK(prochainRepere(c, "C") == "C1");
  for (const char* r : {"C1", "C2", "C4", "U1A", "RV1", "C?"}) {
    ComposantElec k;
    k.uuid = nouvelUuid();
    k.reference = r;
    c.composants.push_back(k);
  }
  CHECK(prochainRepere(c, "C") == "C3");
  CHECK(prochainRepere(c, "U") == "U2");   // U1A prend le 1 des U
  CHECK(prochainRepere(c, "R") == "R1");   // RV1 n'est pas un R
}

TEST_CASE("2.2 : un trou de fixation déposé sur le circuit entre au schéma, sur la feuille Mécanique, et sur la carte, repéré", "[L2]") {
  ConceptionElec c = exempleElectronique();
  const std::size_t feuilles = c.feuilles.size(), empreintes = c.circuit.empreintes.size();
  const std::string attendu = prochainRepere(c, "H");  // l'exemple a déjà ses trous de fixation
  const std::string k = poserArticleCircuit(c, "Trou_fixation", ptMm(3, 3), 90);
  REQUIRE(!k.empty());
  // Le symbole va sur une feuille « Mécanique », créée pour lui
  REQUIRE(c.feuilles.size() == feuilles + 1);
  CHECK(c.feuilles.back().nom == "Mécanique");
  CHECK(c.feuilles.back().symboles.size() == 1);
  CHECK(c.circuit.empreintes.size() == empreintes + 1);
  const ComposantElec* x = c.composant(k);
  REQUIRE(x != nullptr);
  CHECK(x->reference == attendu);
  CHECK(c.circuit.empreintes.back().position == ptMm(3, 3));
  CHECK(c.circuit.empreintes.back().rotation == 90);
  // La mise à jour du circuit le garde : son symbole est posé
  const auto b = mettreAJourCircuit(c);
  CHECK(b.retirees == 0);
  // Un second : H2, posé à côté du premier sous le dessin
  const std::string attendu2 = prochainRepere(c, "H");
  const std::string k2 = poserArticleCircuit(c, "Trou_fixation", ptMm(10, 3));
  CHECK(c.composant(k2)->reference == attendu2);
  CHECK(attendu2 != attendu);
  CHECK(c.feuilles.size() == feuilles + 1);  // la même feuille, une place plus loin
  CHECK(c.feuilles.back().symboles.size() == 2);
  CHECK(c.feuilles.back().symboles[0].position != c.feuilles.back().symboles[1].position);
  // Un article sans empreinte (alimentation) ne se pose pas sur le circuit
  CHECK(poserArticleCircuit(c, "GND", ptMm(0, 0)).empty());
}

TEST_CASE("2.3 : le repère est donné à la pose, et les unités s'enchaînent", "[L2][SAI-05][SYM-04]") {
  ConceptionElec c = nouvelleConception("Pose");
  const std::string a = poserSymbole(c, 0, "C", ptMm(20, 20));
  const std::string b = poserSymbole(c, 0, "C", ptMm(30, 20));
  CHECK(annoterALaPose(c, c.feuilles[0].symboles[0].composant) == "C1");
  CHECK(annoterALaPose(c, c.feuilles[0].symboles[1].composant) == "C2");
  // Un repère choisi à la main n'est pas touché
  c.composants[0].reference = "C_DECOUPLAGE";
  CHECK(annoterALaPose(c, c.composants[0].uuid) == "C_DECOUPLAGE");
  // Unités : la première pose prend l'unité A, la suivante proposée est B, puis C, D, E ; plus rien après
  const std::string u = poserSymbole(c, 0, "74HC00_unites", ptMm(60, 40));
  std::string k;
  for (const auto& s : c.feuilles[0].symboles)
    if (s.uuid == u) k = s.composant;
  CHECK(uniteSuivante(c, k) == 2);
  CHECK(!poserUniteEn(c, 0, k, 2, ptMm(80, 40)).empty());
  CHECK(poserUniteEn(c, 0, k, 2, ptMm(90, 40)).empty());  // déjà posée
  CHECK(uniteSuivante(c, k) == 3);
  CHECK(!poserUniteEn(c, 0, k, 3, ptMm(100, 40)).empty());
  CHECK(!poserUniteEn(c, 0, k, 4, ptMm(120, 40)).empty());
  CHECK(uniteSuivante(c, k) == 5);
  CHECK(!poserUniteEn(c, 0, k, 5, ptMm(140, 40)).empty());
  CHECK(uniteSuivante(c, k) == 0);
}

TEST_CASE("2.3 : une unité posée à côté ne recouvre aucun symbole, même décalé (S33)", "[L2][S33]") {
  ConceptionElec c = nouvelleConception("Recouvrement");
  const std::string u = poserSymbole(c, 0, "74HC00_unites", ptMm(50, 50));
  // Une résistance juste à droite, là où l'unité B irait, mais pas à la même position exacte
  poserSymbole(c, 0, "R", ptMm(66.04, 51.27));
  const std::string ub = poserUnite(c, 0, u);
  REQUIRE(!ub.empty());
  const Feuille& f = c.feuilles[0];
  const SymbolePlace* sb = nullptr;
  for (const auto& s : f.symboles)
    if (s.uuid == ub) sb = &s;
  REQUIRE(sb != nullptr);
  const Boite bb = boiteSymbole(*sb, *c.symbole(sb->symbole));
  for (const auto& s : f.symboles)
    if (s.uuid != ub) CHECK_FALSE(boiteSymbole(s, *c.symbole(s.symbole)).touche(bb));
}

TEST_CASE("2.2 à 2.33 : les nouveaux champs du modèle se relisent à l'identique", "[L2][ECH-10]") {
  ConceptionElec c = exempleElectronique();
  Feuille& f = c.feuilles[0];
  f.symboles[0].champValeur = ChampPlace{ptMm(3.81, 2.54), true};
  f.symboles[0].champReference = ChampPlace{ptMm(-3.81, -2.54), false};
  Etiquette e;
  e.uuid = nouvelUuid();
  e.texte = "SDA";
  e.type = TypeEtiquette::HorsFeuille;
  e.sens = SensSignal::Sortie;
  f.etiquettes.push_back(e);
  DefSymbole d = *symboleBibliotheque("R");
  d.id = "perso:Ferrite";
  d.traversant = true;
  d.nomsUnites = {"bobine"};
  c.symboles[d.id] = d;
  c.erc.gravites["ERC-31"] = GraviteReglee::Ignorer;
  c.erc.matrice["sortie/collecteur-ouvert"] = "avertissement";
  c.erc.exclusions.push_back({"ERC-10", {"x"}, "broche laissée libre exprès", "Evan", "2026-10-22", {}});
  f.fils.push_back({nouvelUuid(), ptMm(10.16, 10.16), ptMm(20.32, 10.16), 2'540'000});  // fil courbe (2.19)
  const auto j = versJson(c);
  const ConceptionElec r = conceptionDepuisJson(j);
  // La lecture range les symboles par identifiant (ordre stable, ECH-10)
  std::vector<SymbolePlace> attendus = c.feuilles[0].symboles;
  std::sort(attendus.begin(), attendus.end(), [](const SymbolePlace& a, const SymbolePlace& b) { return a.uuid < b.uuid; });
  CHECK(r.feuilles[0].symboles == attendus);
  CHECK(r.feuilles[0].etiquettes.size() == f.etiquettes.size());
  bool courbe = false;
  for (const auto& w : r.feuilles[0].fils) courbe |= w.rayon == 2'540'000;
  CHECK(courbe);
  bool trouve = false;
  for (const auto& x : r.feuilles[0].etiquettes)
    if (x.uuid == e.uuid) trouve = x.type == TypeEtiquette::HorsFeuille && x.sens == SensSignal::Sortie;
  CHECK(trouve);
  CHECK(r.symboles.at("perso:Ferrite") == d);
  CHECK(r.erc == c.erc);
  CHECK(versJson(r).dump() == j.dump());
  // Un connecteur hors feuille relie comme une globale
  const Connectivite k = connectivite(r);
  CHECK(k.parEtiquette.count(e.uuid) == 1);
}

TEST_CASE("2.3 : une résistance posée sur un fil s'y insère en série", "[L2][SAI-03]") {
  ConceptionElec c = nouvelleConception("Insertion");
  Feuille& f = c.feuilles[0];
  f.fils.push_back({nouvelUuid(), ptMm(20, 10), ptMm(20, 40)});
  // Une résistance debout centrée sur le fil : broches en (20, 21,19) et (20, 28,81)
  const std::string r = poserSymbole(c, 0, "R", ptMm(20, 25));
  REQUIRE(insererDansFil(c, 0, r));
  REQUIRE(c.feuilles[0].fils.size() == 2);
  const Connectivite k = connectivite(c);
  CHECK(k.reseaux.size() == 2);  // deux réseaux : un de chaque côté de la résistance
  // Couchée en travers du fil : refusée, rien ne change
  ConceptionElec d = nouvelleConception("Travers");
  d.feuilles[0].fils.push_back({nouvelUuid(), ptMm(20, 10), ptMm(20, 40)});
  const std::string r2 = poserSymbole(d, 0, "R", ptMm(20, 25), 1);
  CHECK_FALSE(insererDansFil(d, 0, r2));
  CHECK(d.feuilles[0].fils.size() == 1);
}

TEST_CASE("2.7 : le fil à l'équerre contourne un symbole au lieu de le traverser", "[L2][SAI-06][S10]") {
  ConceptionElec c = nouvelleConception("Contour");
  const std::string r = poserSymbole(c, 0, "R", ptMm(50.8, 50.8), 1);  // couchée : ses broches sur la ligne droite
  const std::vector<Pt> ch = tracerFil(c, 0, ptMm(38.1, 50.8), ptMm(63.5, 50.8));
  REQUIRE(ch.size() >= 4);
  CHECK(ch.front() == ptMm(38.1, 50.8));
  CHECK(ch.back() == ptMm(63.5, 50.8));
  const SymbolePlace& s = c.feuilles[0].symboles.front();
  const Boite corps = boiteSymbole(s, *c.symbole("R"));
  for (std::size_t i = 0; i + 1 < ch.size(); ++i) {
    const Pt a = ch[i], b = ch[i + 1];
    CHECK((a.x == b.x || a.y == b.y));  // à l'équerre
    // Aucun segment ne passe sur une extrémité de broche ni dans le corps
    for (const auto& bp : brochesPlacees(s, *c.symbole("R"))) CHECK_FALSE(surSegment(bp.p, a, b));
    const bool dedans = a.y == b.y ? (a.y > corps.y0 && a.y < corps.y1 && std::max(a.x, b.x) > corps.x0 && std::min(a.x, b.x) < corps.x1)
                                   : (a.x > corps.x0 && a.x < corps.x1 && std::max(a.y, b.y) > corps.y0 && std::min(a.y, b.y) < corps.y1);
    CHECK_FALSE(dedans);
  }
  (void)r;
}

TEST_CASE("2.18 : « Relier à… » relie deux broches par un fil qui contourne, et une liste de paires", "[L2][SAI-29]") {
  ConceptionElec c = nouvelleConception("Relier");
  poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
  poserSymbole(c, 0, "R", ptMm(76.2, 76.2));
  poserSymbole(c, 0, "C", ptMm(101.6, 50.8));
  annoter(c, false);
  CHECK(relierBroches(c, "R1:2", "R2:1") > 0);
  CHECK(relierBroches(c, "R9:1", "R2:1") == -1);  // repère introuvable
  std::vector<std::string> echecs;
  CHECK(relierPaires(c, {{"R2:2", "C1:2"}, {"R1:1", "C1:1"}}, &echecs) > 0);
  CHECK(echecs.empty());
  const Connectivite k = connectivite(c);
  const ComposantElec* r1 = c.parReference("R1");
  const ComposantElec* r2 = c.parReference("R2");
  const ComposantElec* c1 = c.parReference("C1");
  CHECK(k.reseau(r1->uuid, "2") == k.reseau(r2->uuid, "1"));
  CHECK(k.reseau(r2->uuid, "2") == k.reseau(c1->uuid, "2"));
  CHECK(k.reseau(r1->uuid, "1") == k.reseau(c1->uuid, "1"));
  CHECK(k.reseau(r1->uuid, "1") != k.reseau(r1->uuid, "2"));  // aucun fil ne court-circuite
}

TEST_CASE("2.7, 2.19 : couper, trancher, fusionner, décaler les croisements", "[L2][SAI-07][SAI-09]") {
  ConceptionElec c = nouvelleConception("Fils");
  Feuille& f = c.feuilles[0];
  f.fils.push_back({nouvelUuid(), ptMm(10, 10), ptMm(30, 10)});
  const std::string w = f.fils[0].uuid;
  REQUIRE(couperFil(c, 0, w, ptMm(20.32, 10)));
  CHECK(c.feuilles[0].fils.size() == 2);
  CHECK(connectivite(c).reseaux.size() == 1);  // les deux moitiés restent reliées
  CHECK(fusionnerFils(c, 0) == 1);
  CHECK(c.feuilles[0].fils.size() == 1);
  REQUIRE(trancherFil(c, 0, c.feuilles[0].fils[0].uuid, ptMm(20.32, 10)));
  CHECK(c.feuilles[0].fils.size() == 2);
  CHECK(connectivite(c).reseaux.size() == 2);  // deux morceaux disjoints
  // Six croisements à quatre branches : douze T, réseaux identiques
  ConceptionElec d = nouvelleConception("Croisements");
  for (int i = 0; i < 6; ++i) {
    const Pt o = ptMm(20 + 30.48 * i, 50.8);
    d.feuilles[0].fils.push_back({nouvelUuid(), o - ptMm(10.16, 0), o + ptMm(10.16, 0)});
    d.feuilles[0].fils.push_back({nouvelUuid(), o - ptMm(0, 10.16), o + ptMm(0, 10.16)});
    d.feuilles[0].jonctions.push_back({o, nouvelUuid()});
  }
  const std::size_t avant = connectivite(d).reseaux.size();
  CHECK(decalerCroisements(d, 0) == 6);
  const Connectivite k = connectivite(d);
  CHECK(k.reseaux.size() == avant);
  std::size_t t = 0;
  for (const auto& [feuille, j] : k.jonctions) t += j.size();
  CHECK(t == 12);
}

TEST_CASE("2.9 : deux fils qui se croisent sans jonction ne sont pas reliés, et aucun point n'est déduit", "[L2][SAI-09]") {
  ConceptionElec c = nouvelleConception("Croisement");
  Feuille& f = c.feuilles[0];
  f.fils.push_back({nouvelUuid(), ptMm(10, 20), ptMm(30, 20)});
  f.fils.push_back({nouvelUuid(), ptMm(20, 10), ptMm(20, 30)});
  Connectivite k = connectivite(c);
  CHECK(k.reseaux.size() == 2);
  CHECK(k.jonctions[f.uuid].empty());
  // Une jonction posée au croisement les relie
  c.feuilles[0].jonctions.push_back({ptMm(20, 20), nouvelUuid()});
  k = connectivite(c);
  CHECK(k.reseaux.size() == 1);
  // Un T (un bout sur le milieu d'un fil) : relié, et le point est déduit
  ConceptionElec d = nouvelleConception("T");
  d.feuilles[0].fils.push_back({nouvelUuid(), ptMm(10, 20), ptMm(30, 20)});
  d.feuilles[0].fils.push_back({nouvelUuid(), ptMm(20, 20), ptMm(20, 30)});
  k = connectivite(d);
  CHECK(k.reseaux.size() == 1);
  CHECK(k.jonctions[d.feuilles[0].uuid].size() == 1);
}

TEST_CASE("2.8 : un symbole relié déplacé garde ses fils à l'équerre, marques et coudes suivent", "[L2][SAI-08][S16][S17]") {
  ConceptionElec c = nouvelleConception("Élastique");
  const std::string r = poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
  Feuille& f = c.feuilles[0];
  const Pt haut = ptMm(50.8, 50.8 - 3.81), coin = haut + ptMm(0, -5.08), bout = coin + ptMm(-10.16, 0);
  f.fils.push_back({nouvelUuid(), haut, coin});
  f.fils.push_back({nouvelUuid(), coin, bout});
  f.nonConnexions.push_back({ptMm(50.8, 50.8 + 3.81), nouvelUuid()});
  deplacerSchema(c, 0, {r}, ptMm(7.62, 5.08), true);
  const Feuille& g = c.feuilles[0];
  for (const auto& w : g.fils) CHECK((w.a.x == w.b.x || w.a.y == w.b.y));
  const Pt nouvelleBroche = ptMm(50.8 + 7.62, 50.8 + 5.08 - 3.81);
  bool accroche = false, auBout = false;
  for (const auto& w : g.fils) {
    accroche |= w.a == nouvelleBroche || w.b == nouvelleBroche;
    auBout |= w.a == bout || w.b == bout;
  }
  CHECK(accroche);
  CHECK(auBout);  // le bout libre n'a pas bougé
  CHECK(g.nonConnexions.front().p == ptMm(50.8 + 7.62, 50.8 + 5.08 + 3.81));
  // Décroché (M) : les fils restent où ils étaient
  ConceptionElec d = nouvelleConception("Décroché");
  const std::string r2 = poserSymbole(d, 0, "R", ptMm(50.8, 50.8));
  d.feuilles[0].fils.push_back({nouvelUuid(), haut, coin});
  deplacerSchema(d, 0, {r2}, ptMm(7.62, 0), false);
  CHECK(d.feuilles[0].fils.front().a == haut);
}

namespace {
// Une broche d'un symbole posé, en coordonnées de la feuille
Pt brocheDe(const ConceptionElec& c, const std::string& symbole, const std::string& numero) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == symbole)
        for (const auto& bp : brochesPlacees(s, *c.symbole(s.symbole)))
          if (bp.broche->numero == numero) return bp.p;
  return {};
}
std::string composantDe(const ConceptionElec& c, const std::string& symbole) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == symbole) return s.composant;
  return {};
}
int compter(const std::vector<Violation>& v, const std::string& code) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
}
// Symbole d'essai à une broche du type donné
DefSymbole uneBroche(const std::string& id, TypeBroche t) {
  DefSymbole d;
  d.id = id;
  d.nom = id;
  d.prefixe = "U";
  d.empreinte = "TestPoint_Pad_1.5";
  Broche b;
  b.numero = "1";
  b.nom = "X";
  b.type = t;
  b.extremite = ptMm(0, -2.54);
  b.direction = 1;
  b.longueur = nm(1.27);
  d.broches = {b};
  return d;
}
}  // namespace

TEST_CASE("2.11 : vingt composants non reliés ne donnent que des marques de dessin", "[L2][S9]") {
  ConceptionElec c = nouvelleConception("Vingt");
  for (int i = 0; i < 20; ++i) poserSymbole(c, 0, "R", ptMm(20.32 + 7.62 * (i % 10), 30.48 + 15.24 * (i / 10)));
  for (std::size_t i = 0; i < c.composants.size(); ++i) c.composants[i].reference = "R" + std::to_string(i + 1);
  const Connectivite k = connectivite(c);
  const auto v = verifierERC(c, k, NiveauErc::Complet);
  CHECK(compter(v, "ERC-10") == 40);
  for (const auto& x : v) CHECK(x.niveau != NiveauErc::Immediat);
  for (const auto& x : v)
    if (x.code == "ERC-10") CHECK(x.niveau == NiveauErc::Dessin);
  CHECK(verifierERC(c, k, NiveauErc::Immediat).empty());
  CHECK(niveauErc("ERC-06") == NiveauErc::Immediat);
  CHECK(niveauErc("ERC-22") == NiveauErc::Complet);
}

TEST_CASE("2.11 : gravités réglées et exclusions justifiées du projet", "[L2][E5]") {
  ConceptionElec c = nouvelleConception("Réglages");
  for (int i = 0; i < 3; ++i) poserSymbole(c, 0, "R", ptMm(20.32 + 10.16 * i, 30.48));
  for (std::size_t i = 0; i < c.composants.size(); ++i) c.composants[i].reference = "R" + std::to_string(i + 1);
  const Connectivite k = connectivite(c);
  c.erc.gravites["ERC-10"] = GraviteReglee::Erreur;
  auto v = verifierERC(c, k, NiveauErc::Complet);
  REQUIRE(compter(v, "ERC-10") == 6);
  for (const auto& x : v)
    if (x.code == "ERC-10") CHECK(x.gravite == Gravite::Erreur);
  c.erc.gravites["ERC-10"] = GraviteReglee::Ignorer;
  CHECK(compter(verifierERC(c, k, NiveauErc::Complet), "ERC-10") == 0);
  c.erc.gravites.erase("ERC-10");
  // Une exclusion vise les objets de la violation : les autres composants restent signalés
  c.erc.exclusions.push_back({"ERC-10", {c.composants[0].uuid}, "laissée libre exprès", "Evan", "2026-10-22", {}});
  v = verifierERC(c, k, NiveauErc::Complet);
  CHECK(compter(v, "ERC-10") == 4);
  int exclues = 0;
  for (const auto& x : verifierERC(c, k)) exclues += exclusionDe(c, x) != nullptr;
  CHECK(exclues == 2);
}

TEST_CASE("2.11 : la matrice des conflits du projet règle la gravité, deux sorties comprises", "[L2][E1]") {
  ConceptionElec c = nouvelleConception("Matrice");
  c.symboles["perso:Sortie"] = uneBroche("perso:Sortie", TypeBroche::Sortie);
  const std::string a = poserSymbole(c, 0, "perso:Sortie", ptMm(20.32, 22.86));
  const std::string b = poserSymbole(c, 0, "perso:Sortie", ptMm(40.64, 22.86));
  c.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(c, a, "1"), brocheDe(c, b, "1")});
  auto gravite20 = [&]() -> std::optional<Gravite> {
    for (const auto& x : verifierERC(c, connectivite(c), NiveauErc::Complet))
      if (x.code == "ERC-20") return x.gravite;
    return std::nullopt;
  };
  CHECK(gravite20() == Gravite::Erreur);
  c.erc.matrice["sortie/sortie"] = "avertissement";
  CHECK(conflitBroches(c, TypeBroche::Sortie, TypeBroche::Sortie) == 'A');
  CHECK(gravite20() == Gravite::Avertissement);
  c.erc.matrice["sortie/sortie"] = "permis";
  CHECK(!gravite20().has_value());
}

TEST_CASE("2.11 : une liaison de réseaux fait passer l'alimentation sans unir les réseaux", "[L2][E3]") {
  ConceptionElec c = nouvelleConception("Liaison");
  c.symboles["perso:Charge"] = uneBroche("perso:Charge", TypeBroche::AlimEntree);
  const std::string u = poserSymbole(c, 0, "perso:Charge", ptMm(20.32, 22.86));
  const std::string nt = poserSymbole(c, 0, "Liaison_reseaux", ptMm(40.64, 20.32));
  const std::string gnd = poserSymbole(c, 0, "GND", ptMm(60.96, 20.32));
  REQUIRE(!nt.empty());
  CHECK(c.symbole("Liaison_reseaux")->liaisonReseaux);
  c.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(c, u, "1"), brocheDe(c, nt, "1")});
  c.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(c, nt, "2"), brocheDe(c, gnd, "1")});
  const Connectivite k = connectivite(c);
  const std::string lien = composantDe(c, nt);
  CHECK(k.reseau(lien, "1") >= 0);
  CHECK(k.reseau(lien, "1") != k.reseau(lien, "2"));  // chacun garde son réseau
  CHECK(compter(verifierERC(c, k), "ERC-22") == 0);
  // Sans la liaison déclarée, le réseau de la charge n'est alimenté par rien
  ConceptionElec d = c;
  d.symboles["Liaison_reseaux"].liaisonReseaux = false;
  CHECK(compter(verifierERC(d, connectivite(d)), "ERC-22") == 1);
}

TEST_CASE("2.11 : contrôles de dessin ERC-14, 16, 17, 18, 25 et 26", "[L2][E10][E6]") {
  ConceptionElec c = nouvelleConception("Dessin");
  Feuille& f = c.feuilles[0];
  // ERC-14 : une broche raccordée au milieu d'un fil
  f.fils.push_back({nouvelUuid(), ptMm(10.16, 20.32), ptMm(30.48, 20.32)});
  poserSymbole(c, 0, "R", ptMm(20.32, 24.13));
  // ERC-18 : broches de deux symboles au même point
  poserSymbole(c, 0, "R", ptMm(101.6, 30.48));
  poserSymbole(c, 0, "R", ptMm(101.6, 38.1));
  // ERC-16, ERC-17 : étiquette et alimentation posées dans le vide
  c.feuilles[0].etiquettes.push_back({nouvelUuid(), "VIDE", ptMm(50.8, 50.8)});
  poserSymbole(c, 0, "GND", ptMm(81.28, 81.28));
  // ERC-25 : « SDA » locale et « SDA » globale, sur deux fils séparés
  c.feuilles[0].fils.push_back({nouvelUuid(), ptMm(10.16, 101.6), ptMm(30.48, 101.6)});
  c.feuilles[0].fils.push_back({nouvelUuid(), ptMm(10.16, 111.76), ptMm(30.48, 111.76)});
  c.feuilles[0].etiquettes.push_back({nouvelUuid(), "SDA", ptMm(10.16, 101.6), 0, TypeEtiquette::Locale});
  c.feuilles[0].etiquettes.push_back({nouvelUuid(), "SDA", ptMm(10.16, 111.76), 0, TypeEtiquette::Globale});
  for (std::size_t i = 0; i < c.composants.size(); ++i) c.composants[i].reference = "R" + std::to_string(i + 1);
  c.composants[1].valeur.clear();  // ERC-26
  const auto v = verifierERC(c, connectivite(c), NiveauErc::Complet);
  for (const char* code : {"ERC-14", "ERC-16", "ERC-17", "ERC-18", "ERC-25", "ERC-26"}) {
    INFO(code);
    CHECK(compter(v, code) >= 1);
  }
}

TEST_CASE("2.13 : un bloc collé d'un projet à l'autre emporte ses définitions, figées", "[L2][SAI-27][S32]") {
  // Projet A : un symbole personnel et son empreinte figée
  ConceptionElec a = nouvelleConception("A");
  DefSymbole perso = *symboleBibliotheque("R");
  perso.id = "perso:Shunt";
  perso.nom = "Shunt";
  perso.empreinte = "perso:R2512";
  a.symboles[perso.id] = perso;
  DefEmpreinte e = *genererEmpreinte("R_0603");
  e.id = "perso:R2512";
  e.description = "Shunt 2512";
  a.empreintes[e.id] = e;
  const std::string s = poserSymbole(a, 0, perso.id, ptMm(20.32, 20.32));
  // Par le presse-papiers du système : le texte JSON
  const PressePapiersSchema pp = presseDepuisJson(presseVersJson(copierSchema(a, 0, {s})));
  REQUIRE(pp.empreintes.count("perso:R2512") == 1);
  REQUIRE(pp.definitions.count("perso:Shunt") == 1);
  // Projet B : le même identifiant, une autre définition
  ConceptionElec b = nouvelleConception("B");
  DefSymbole autre = perso;
  autre.nom = "Autre";
  autre.traces.clear();
  b.symboles[autre.id] = autre;
  const auto crees = collerSchemaAvec(b, 0, pp, ptMm(50.8, 50.8), ReperesCollage::Libres);
  REQUIRE(crees.size() == 1);
  const SymbolePlace colle = b.feuilles[0].symboles.back();
  CHECK(colle.symbole == "perso:Shunt~2");
  CHECK(b.symboles.at("perso:Shunt").nom == "Autre");  // celle du projet reste la sienne
  DefSymbole attendu = pp.definitions.at("perso:Shunt");
  attendu.id = "perso:Shunt~2";
  CHECK(b.symboles.at("perso:Shunt~2") == attendu);
  const ComposantElec* k = b.composant(colle.composant);
  REQUIRE(k != nullptr);
  CHECK(k->symbole == "perso:Shunt~2");
  CHECK(k->empreinte == "perso:R2512");
  CHECK(b.empreintes.at("perso:R2512") == pp.empreintes.at("perso:R2512"));
  CHECK(k->reference == "R1");  // un repère libre
  // Un second collage resert la copie figée
  collerSchemaAvec(b, 0, pp, ptMm(76.2, 50.8), ReperesCollage::Libres);
  CHECK(b.feuilles[0].symboles.back().symbole == "perso:Shunt~2");
  CHECK(b.symboles.count("perso:Shunt~3") == 0);
  CHECK(b.composants.back().reference == "R2");
}

TEST_CASE("2.17 : le parcours « Ta première carte » se suit étape par étape, chacune vérifiée sur la conception", "[L2][AUT-11]") {
  const ParcoursApprendre* p = parcoursElectronique("premiere-carte");
  REQUIRE(p != nullptr);
  REQUIRE(p->etapes.size() >= 8);
  for (const auto& e : p->etapes) {
    CHECK(!e.titre.empty());
    CHECK(!e.pourquoi.empty());
    CHECK(!e.comment.empty());
    CHECK(e.faite);
  }
  ConceptionElec c = nouvelleConception("Première carte");
  CHECK(etapeEnCours(*p, c) == 0);
  const std::string led = poserSymbole(c, 0, "LED", ptMm(50.8, 50.8));
  CHECK(etapeEnCours(*p, c) == 1);
  const std::string r = poserSymbole(c, 0, "R", ptMm(50.8, 30.48));
  const std::string v5 = poserSymbole(c, 0, "+5V", ptMm(50.8, 20.32));
  const std::string gnd = poserSymbole(c, 0, "GND", ptMm(50.8, 66.04));
  for (auto& k : c.composants) annoterALaPose(c, k.uuid);
  CHECK(etapeEnCours(*p, c) == 3);  // pas encore relié
  // Les fils : +5V → R.1, R.2 → LED, LED → GND
  auto broche = [&](const std::string& s, const std::string& n) {
    for (const auto& sp : c.feuilles[0].symboles)
      if (sp.uuid == s)
        for (const auto& b : brochesPlacees(sp, *c.symbole(sp.symbole)))
          if (b.broche->numero == n) return b.p;
    return Pt{};
  };
  const DefSymbole* dl = c.symbole("LED");
  REQUIRE(dl != nullptr);
  c.feuilles[0].fils.push_back({nouvelUuid(), broche(v5, "1"), broche(r, "1")});
  c.feuilles[0].fils.push_back({nouvelUuid(), broche(r, "2"), broche(led, dl->broches[1].numero)});
  c.feuilles[0].fils.push_back({nouvelUuid(), broche(led, dl->broches[0].numero), broche(gnd, "1")});
  CHECK(etapeEnCours(*p, c) >= 4);  // relié ; la vérification dit le reste
}

TEST_CASE("2.18 : une liste de dix paires reliée d'un coup, sans traverser un symbole ni croiser un autre réseau", "[L2][SAI-29][SAI-30]") {
  ConceptionElec c = nouvelleConception("Dix paires");
  const std::string j = poserSymbole(c, 0, "Conn_01x10", ptMm(152.4, 101.6));
  std::vector<std::string> resistances;
  for (int i = 0; i < 10; ++i) resistances.push_back(poserSymbole(c, 0, "R", ptMm(101.6, 90.17 + 2.54 * i), 1));
  annoter(c, false);
  auto symbole = [&](const std::string& id) -> const SymbolePlace& {
    for (const auto& s : c.feuilles[0].symboles)
      if (s.uuid == id) return s;
    return c.feuilles[0].symboles.front();
  };
  // Chaque résistance, par sa broche tournée vers le connecteur, à la broche du connecteur de même hauteur
  std::vector<std::pair<std::string, std::string>> paires;
  const SymbolePlace& sj = symbole(j);
  const std::string refJ = c.composant(sj.composant)->reference;
  for (const std::string& r : resistances) {
    const SymbolePlace& sr = symbole(r);
    const auto br = brochesPlacees(sr, *c.symbole(sr.symbole));
    const auto droite = std::max_element(br.begin(), br.end(), [](const BrochePlacee& a, const BrochePlacee& b) { return a.p.x < b.p.x; });
    std::string numeroJ;
    for (const auto& b : brochesPlacees(sj, *c.symbole(sj.symbole)))
      if (b.p.y == droite->p.y) numeroJ = b.numero;
    REQUIRE(!numeroJ.empty());
    paires.push_back({refJ + ":" + numeroJ, c.composant(sr.composant)->reference + ":" + droite->numero});
  }
  std::vector<std::string> echecs;
  CHECK(relierPaires(c, paires, &echecs) >= 10);
  CHECK(echecs.empty());
  const Connectivite k = connectivite(c);
  std::set<int> reseaux;
  for (const auto& [a, b] : paires) {
    auto reseau = [&](const std::string& d) {
      const auto deux = d.find(':');
      const ComposantElec* x = c.parReference(d.substr(0, deux));
      return x ? k.reseau(x->uuid, d.substr(deux + 1)) : -1;
    };
    CHECK(reseau(a) >= 0);
    CHECK(reseau(a) == reseau(b));
    reseaux.insert(reseau(a));
  }
  CHECK(reseaux.size() == 10);
  // Aucun fil ne traverse le corps d'un symbole (sa boîte réduite d'un demi-pas)
  int traversees = 0;
  for (const auto& w : c.feuilles[0].fils)
    for (const auto& s : c.feuilles[0].symboles) {
      const Boite b = boiteSymbole(s, *c.symbole(s.symbole)).gonflee(-GRILLE_SCHEMA / 2);
      const bool horizontal = w.a.y == w.b.y;
      if (horizontal ? (w.a.y > b.y0 && w.a.y < b.y1 && std::max(w.a.x, w.b.x) > b.x0 && std::min(w.a.x, w.b.x) < b.x1)
                     : (w.a.x > b.x0 && w.a.x < b.x1 && std::max(w.a.y, w.b.y) > b.y0 && std::min(w.a.y, w.b.y) < b.y1))
        ++traversees;
    }
  CHECK(traversees == 0);
  // Aucun croisement entre fils de réseaux différents : un chemin sans croisement existe pour chaque paire
  int croisements = 0;
  const auto& fils = c.feuilles[0].fils;
  for (std::size_t i = 0; i < fils.size(); ++i)
    for (std::size_t m = i + 1; m < fils.size(); ++m) {
      const Fil& h = fils[i].a.y == fils[i].b.y ? fils[i] : fils[m];
      const Fil& v = fils[i].a.y == fils[i].b.y ? fils[m] : fils[i];
      if (h.a.y != h.b.y || v.a.x != v.b.x || &h == &v) continue;
      const bool coupe = v.a.x > std::min(h.a.x, h.b.x) && v.a.x < std::max(h.a.x, h.b.x) && h.a.y > std::min(v.a.y, v.b.y) && h.a.y < std::max(v.a.y, v.b.y);
      if (coupe && k.parFil.at(h.uuid) != k.parFil.at(v.uuid)) ++croisements;
    }
  CHECK(croisements == 0);
}

TEST_CASE("2.20 : « Renommer le réseau » SDA en I2C_SDA sur toutes les feuilles change ses six étiquettes", "[L2][SAI-32]") {
  ConceptionElec c = nouvelleConception("Bus I2C");
  for (int i = 0; i < 2; ++i) {
    Feuille f;
    f.uuid = nouvelUuid();
    f.nom = "Feuille " + std::to_string(i + 2);
    c.feuilles.push_back(f);
  }
  for (std::size_t i = 0; i < 3; ++i) {
    Feuille& f = c.feuilles[i];
    f.fils.push_back({nouvelUuid(), ptMm(20.32, 20.32), ptMm(40.64, 20.32)});
    f.fils.push_back({nouvelUuid(), ptMm(20.32, 40.64), ptMm(40.64, 40.64)});
    f.etiquettes.push_back({nouvelUuid(), "SDA", ptMm(20.32, 20.32), 0, TypeEtiquette::Globale});
    f.etiquettes.push_back({nouvelUuid(), "SDA", ptMm(20.32, 40.64), 0, TypeEtiquette::Globale});
    f.etiquettes.push_back({nouvelUuid(), "SCL", ptMm(40.64, 40.64), 0, TypeEtiquette::Globale});
  }
  CHECK(renommerReseau(c, "SDA", "I2C_SDA", -1) == 6);
  int sda = 0, i2c = 0, scl = 0;
  for (const auto& f : c.feuilles)
    for (const auto& e : f.etiquettes) {
      sda += e.texte == "SDA";
      i2c += e.texte == "I2C_SDA";
      scl += e.texte == "SCL";
    }
  CHECK(sda == 0);
  CHECK(i2c == 6);
  CHECK(scl == 3);
  // Sur une feuille seulement
  CHECK(renommerReseau(c, "SCL", "I2C_SCL", 1) == 1);
}

TEST_CASE("2.21 : « Terminer les broches libres » d'un connecteur de vingt broches dont huit reliées : douze étiquettes à leur nom", "[L2][SAI-13]") {
  ConceptionElec c = nouvelleConception("Connecteur");
  const std::string j = poserSymbole(c, 0, "Conn_01x20", ptMm(101.6, 101.6));
  REQUIRE(!j.empty());
  annoter(c, false);
  std::string comp;
  for (const auto& s : c.feuilles[0].symboles)
    if (s.uuid == j) comp = s.composant;
  const SymbolePlace& sj = c.feuilles[0].symboles.front();
  const auto broches = brochesPlacees(sj, *c.symbole(sj.symbole));
  REQUIRE(broches.size() == 20);
  for (std::size_t i = 0; i < 8; ++i) c.feuilles[0].fils.push_back({nouvelUuid(), broches[i].p, broches[i].p + ptMm(-5.08, 0)});
  const std::size_t etiquettes0 = c.feuilles[0].etiquettes.size();
  CHECK(terminerBrochesLibres(c, 0, {comp}, true) == 12);
  CHECK(c.feuilles[0].etiquettes.size() == etiquettes0 + 12);
  // Chaque broche libre est maintenant reliée à son étiquette
  const Connectivite k = connectivite(c);
  int nommees = 0;
  for (const auto& b : broches)
    if (const int r = k.reseau(comp, b.numero); r >= 0) nommees += k.reseaux[static_cast<std::size_t>(r)].nomme;
  CHECK(nommees == 12);
}

TEST_CASE("2.23 : repère et valeur replacés à côté du corps d'un symbole tourné, sur la grille, sans chevauchement ; un champ bloqué reste", "[L2][SAI-34]") {
  ConceptionElec c = nouvelleConception("Champs");
  const std::string r = poserSymbole(c, 0, "R", ptMm(101.6, 101.6), 1);
  annoter(c, false);
  replacerChamps(c, 0, r);
  const SymbolePlace* s = &c.feuilles[0].symboles.front();
  const DefSymbole& d = *c.symbole(s->symbole);
  const Pt ref = ancrageChamp(*s, d, false), val = ancrageChamp(*s, d, true);
  CHECK(ref.x % GRILLE_SCHEMA == 0);
  CHECK(ref.y % GRILLE_SCHEMA == 0);
  CHECK(val.x % GRILLE_SCHEMA == 0);
  CHECK(val.y % GRILLE_SCHEMA == 0);
  CHECK(ref != val);
  const Boite corps = boiteSymbole(*s, d);
  CHECK(!corps.gonflee(-GRILLE_SCHEMA / 2).contient(ref));  // à côté du corps, pas dessus
  CHECK(distance(corps.centre(), ref) < 12'000'000);         // et pas loin
  // Un champ bloqué garde sa place
  c.feuilles[0].symboles.front().champValeur = ChampPlace{ptMm(10.16, 10.16), true};
  replacerChamps(c, 0, r);
  CHECK(c.feuilles[0].symboles.front().champValeur->decalage == ptMm(10.16, 10.16));
}

TEST_CASE("2.24 : matrice 4 × 8 d'une LED et de sa résistance au pas de 5,08 mm, numérotée d'abord en ligne", "[L2][SAI-03][SAI-27]") {
  ConceptionElec c = nouvelleConception("Matrice");
  const std::string led = poserSymbole(c, 0, "LED", ptMm(20.32, 20.32));
  const std::string r = poserSymbole(c, 0, "R", ptMm(20.32, 30.48));
  const PressePapiersSchema pp = copierSchema(c, 0, {led, r});
  ConceptionElec m = nouvelleConception("Matrice");
  const auto crees = collerEnMatrice(m, 0, pp, ptMm(50.8, 50.8), 4, 8, ptMm(5.08 * 2, 5.08 * 4), true);
  CHECK(crees.size() == 64);
  std::set<std::string> d, rr;
  for (const auto& k : m.composants) (k.reference[0] == 'D' ? d : rr).insert(k.reference);
  CHECK(d.size() == 32);
  CHECK(rr.size() == 32);
  CHECK(d.count("D1") == 1);
  CHECK(d.count("D32") == 1);
  CHECK(rr.count("R32") == 1);
  // D1 à D8 sur la première ligne, de gauche à droite
  std::map<std::string, Pt> ou;
  for (const auto& s : m.feuilles[0].symboles)
    if (const ComposantElec* k = m.composant(s.composant)) ou[k->reference] = s.position;
  CHECK(ou["D1"].y == ou["D8"].y);
  CHECK(ou["D1"].x < ou["D8"].x);
  CHECK(ou["D9"].y > ou["D1"].y);
}

TEST_CASE("2.24 : collage spécial, repères gardés même en double, libres ou effacés", "[L2][SAI-27]") {
  ConceptionElec c = nouvelleConception("Collage spécial");
  const std::string r = poserSymbole(c, 0, "R", ptMm(20.32, 20.32));
  annoter(c, false);
  const std::string ref = c.composants.front().reference;
  const PressePapiersSchema pp = copierSchema(c, 0, {r});
  auto compter = [&c](const std::string& x) { return std::count_if(c.composants.begin(), c.composants.end(), [&](const ComposantElec& k) { return k.reference == x; }); };
  collerSchemaAvec(c, 0, pp, ptMm(50.8, 20.32), ReperesCollage::Gardes);
  CHECK(compter(ref) == 2);  // même en double (SAI-27) : l'ERC le dira
  collerSchemaAvec(c, 0, pp, ptMm(76.2, 20.32), ReperesCollage::Libres);
  CHECK(compter(ref) == 2);
  CHECK(c.composants.back().reference != ref);
  CHECK(c.composants.back().reference.find('?') == std::string::npos);
  collerSchemaAvec(c, 0, pp, ptMm(101.6, 20.32), ReperesCollage::Effaces);
  CHECK(c.composants.back().reference.find('?') != std::string::npos);
}

TEST_CASE("2.25 : cinq résistances alignées à gauche puis réparties : même abscisse, pas égal sur la grille, fils à l'équerre", "[L2][SAI-35]") {
  ConceptionElec c = nouvelleConception("Aligner");
  std::set<std::string> sel;
  const double xs[] = {30.48, 38.1, 33.02, 45.72, 35.56}, ys[] = {20.32, 33.02, 50.8, 60.96, 91.44};
  for (int i = 0; i < 5; ++i) sel.insert(poserSymbole(c, 0, "R", ptMm(xs[i], ys[i]), 1));
  // Un fil accroché à chacune, vers la droite
  for (const auto& s : c.feuilles[0].symboles) {
    const auto br = brochesPlacees(s, *c.symbole(s.symbole));
    const auto droite = std::max_element(br.begin(), br.end(), [](const BrochePlacee& a, const BrochePlacee& b) { return a.p.x < b.p.x; });
    c.feuilles[0].fils.push_back({nouvelUuid(), droite->p, droite->p + ptMm(12.7, 0)});
  }
  alignerSchema(c, 0, sel, Alignement::Gauche);
  alignerSchema(c, 0, sel, Alignement::RepartirVertical);
  std::vector<Pt> p;
  for (const auto& s : c.feuilles[0].symboles) p.push_back(s.position);
  std::sort(p.begin(), p.end(), [](Pt a, Pt b) { return a.y < b.y; });
  for (const Pt& x : p) {
    CHECK(x.x == p.front().x);
    CHECK(x.y % GRILLE_SCHEMA == 0);
  }
  const Nm pas = p[1].y - p[0].y;
  for (std::size_t i = 1; i < p.size(); ++i) CHECK(p[i].y - p[i - 1].y == pas);
  for (const auto& w : c.feuilles[0].fils) CHECK((w.a.x == w.b.x || w.a.y == w.b.y));
  // Toujours reliées : le fil de chaque résistance arrive encore sur sa broche
  for (const auto& s : c.feuilles[0].symboles) {
    const auto br = brochesPlacees(s, *c.symbole(s.symbole));
    const auto droite = std::max_element(br.begin(), br.end(), [](const BrochePlacee& a, const BrochePlacee& b) { return a.p.x < b.p.x; });
    bool relie = false;
    for (const auto& w : c.feuilles[0].fils) relie |= w.a == droite->p || w.b == droite->p;
    CHECK(relie);
  }
}

TEST_CASE("2.23 : les noms des unités passent de la fiche du composant au symbole, et reviennent", "[L2][SYM-04]") {
  SpecSymbole sp;
  sp.nom = "Double ampli";
  for (int u = 1; u <= 2; ++u) {
    const std::string n[3] = {u == 1 ? "1" : "7", u == 1 ? "2" : "6", u == 1 ? "3" : "5"};
    sp.broches.push_back({n[0], "OUT", TypeBroche::Sortie, -1, u});
    sp.broches.push_back({n[1], "IN-", TypeBroche::Entree, -1, u});
    sp.broches.push_back({n[2], "IN+", TypeBroche::Entree, -1, u});
  }
  sp.nomsUnites = {"Ampli 1", "Ampli 2"};
  const DefSymbole d = genererSymbole(sp);
  CHECK(d.unites == 2);
  CHECK(d.nomsUnites == std::vector<std::string>{"Ampli 1", "Ampli 2"});
  CHECK(specDepuisSymbole(d).nomsUnites == sp.nomsUnites);
}

namespace {
// Double ampli à broches d'alimentation communes (8 : V+, 4 : V−), comme le crée « Créer un composant »
DefSymbole doubleAmpliCommun() {
  SpecSymbole sp;
  sp.id = "perso:DoubleAmpli";
  sp.nom = "Double ampli";
  sp.empreinte = "SOIC-8";
  for (int u = 1; u <= 2; ++u) {
    sp.broches.push_back({u == 1 ? "1" : "7", "OUT", TypeBroche::Sortie, -1, u});
    sp.broches.push_back({u == 1 ? "2" : "6", "IN-", TypeBroche::Entree, -1, u});
    sp.broches.push_back({u == 1 ? "3" : "5", "IN+", TypeBroche::Entree, -1, u});
  }
  sp.broches.push_back({"8", "V+", TypeBroche::AlimEntree, -1, 0});
  sp.broches.push_back({"4", "V-", TypeBroche::AlimEntree, -1, 0});
  DefSymbole d = genererSymbole(sp);
  d.id = sp.id;
  return d;
}
}  // namespace

TEST_CASE("2.33 : broche commune reliée à +5V sur l'unité A et à +12V sur l'unité B : une erreur nomme les deux réseaux, aucun n'est fusionné", "[L2][ERC-04]") {
  ConceptionElec c = nouvelleConception("Commune");
  c.symboles["perso:DoubleAmpli"] = doubleAmpliCommun();
  const std::string a = poserSymbole(c, 0, "perso:DoubleAmpli", ptMm(50.8, 50.8));
  const std::string b = poserUnite(c, 0, a);
  REQUIRE(!b.empty());
  const std::string p5 = poserSymbole(c, 0, "+5V", ptMm(50.8, 25.4));
  const std::string p12 = poserSymbole(c, 0, "+12V", ptMm(101.6, 25.4));
  c.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(c, a, "8"), brocheDe(c, p5, "1")});
  c.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(c, b, "8"), brocheDe(c, p12, "1")});
  const Connectivite k = connectivite(c);
  REQUIRE(k.conflitsCommuns.size() == 1);
  const auto& x = k.conflitsCommuns.front();
  CHECK(x.broche == "8");
  CHECK(std::find(x.reseaux.begin(), x.reseaux.end(), "+5V") != x.reseaux.end());
  CHECK(std::find(x.reseaux.begin(), x.reseaux.end(), "+12V") != x.reseaux.end());
  CHECK(k.indice("+5V") != k.indice("+12V"));
  const auto v = verifierERC(c, k);
  CHECK(compter(v, "ERC-42") == 1);
  CHECK(compter(v, "ERC-23") == 0);  // aucun court-circuit fabriqué en silence
  for (const auto& y : v)
    if (y.code == "ERC-42") {
      CHECK(y.gravite == Gravite::Erreur);
      CHECK(y.message.find("+5V") != std::string::npos);
      CHECK(y.message.find("+12V") != std::string::npos);
    }
  // Reliée au même réseau sur les deux unités : rien à dire
  ConceptionElec d = c;
  d.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(d, p12, "1"), brocheDe(d, p12, "1") + ptMm(0, -2.54)});
  for (auto& s : d.feuilles[0].symboles)
    if (s.uuid == p12) s.nomAlim = "+5V";
  CHECK(connectivite(d).conflitsCommuns.empty());
}

TEST_CASE("2.33 : une unité non posée qui porte des entrées ou des alimentations est une erreur", "[L2][ERC-01]") {
  ConceptionElec c = nouvelleConception("Unités");
  poserSymbole(c, 0, "LM358_unites", ptMm(50.8, 50.8));
  annoter(c, false);
  int erreurs = 0;
  for (const auto& v : verifierERC(c, connectivite(c)))
    if (v.code == "ERC-40") erreurs += v.gravite == Gravite::Erreur;
  CHECK(erreurs == 2);  // l'ampli B (entrées) et l'alimentation C
}

TEST_CASE("2.33 : le pilotage suit les passifs en série ; une entrée derrière une résistance qui ne mène à rien n'est pas pilotée", "[L2][ERC-08]") {
  ConceptionElec c = nouvelleConception("Pilotage");
  c.symboles["perso:Entree"] = uneBroche("perso:Entree", TypeBroche::Entree);
  const std::string e = poserSymbole(c, 0, "perso:Entree", ptMm(25.4, 50.8));
  const std::string r = poserSymbole(c, 0, "R", ptMm(50.8, 25.4));
  annoter(c, false);
  c.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(c, e, "1"), brocheDe(c, r, "1")});
  CHECK(compter(verifierERC(c, connectivite(c)), "ERC-21") == 1);
  // L'autre bout de la résistance sur +5V : l'entrée est tirée, donc pilotée
  const std::string p = poserSymbole(c, 0, "+5V", ptMm(76.2, 12.7));
  c.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(c, r, "2"), brocheDe(c, p, "1")});
  CHECK(compter(verifierERC(c, connectivite(c)), "ERC-21") == 0);
}

TEST_CASE("2.33 : contrôle de projet, une référence fabricant posée avec deux empreintes", "[L2][ERC-15]") {
  ConceptionElec c = nouvelleConception("Projet");
  const std::string r1 = poserSymbole(c, 0, "R", ptMm(25.4, 25.4));
  const std::string r2 = poserSymbole(c, 0, "R", ptMm(50.8, 25.4));
  annoter(c, false);
  for (auto& k : c.composants) {
    k.mpn = "RC0603FR-0710KL";
    k.empreinte = k.uuid == composantDe(c, r1) ? "R_0603" : "R_0805";
  }
  auto v = verifierERC(c, connectivite(c));
  CHECK(compter(v, "ERC-43") == 1);
  c.composant(composantDe(c, r2))->empreinte = "R_0603";
  CHECK(compter(verifierERC(c, connectivite(c)), "ERC-43") == 0);
}

TEST_CASE("2.33 : le cuivre d'une liaison de réseaux est vérifié au circuit, et une gravité de la DRC se règle", "[L2][ERC-11][ERC-15]") {
  ConceptionElec c = nouvelleConception("Liaison au circuit");
  c.symboles["perso:Charge"] = uneBroche("perso:Charge", TypeBroche::AlimEntree);
  const std::string u = poserSymbole(c, 0, "perso:Charge", ptMm(20.32, 22.86));
  const std::string nt = poserSymbole(c, 0, "Liaison_reseaux", ptMm(40.64, 20.32));
  const std::string gnd = poserSymbole(c, 0, "GND", ptMm(60.96, 20.32));
  annoter(c, false);
  c.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(c, u, "1"), brocheDe(c, nt, "1")});
  c.feuilles[0].fils.push_back({nouvelUuid(), brocheDe(c, nt, "2"), brocheDe(c, gnd, "1")});
  mettreAJourCircuit(c);
  Connectivite k = connectivite(c);
  const std::string lien = composantDe(c, nt);
  std::vector<PastillePlacee> pads;
  for (const auto& p : pastilles(c, k))
    if (p.composant == lien) pads.push_back(p);
  REQUIRE(pads.size() == 2);
  // Sans pont : la liaison ne relie rien sur la carte
  CHECK(compter(verifierDRC(c, k, remplirZones(c, k)), "DRC-43") == 1);
  // Le pont, une courte piste d'une pastille à l'autre : ni court-circuit ni isolement signalés entre ses réseaux
  c.circuit.pistes.push_back({nouvelUuid(), pads[0].reseau, pads[0].dessous ? Couche::CuivreDessous : Couche::CuivreDessus, pads[0].pastille.position, pads[1].pastille.position, 250'000});
  k = connectivite(c);
  const auto v = verifierDRC(c, k, remplirZones(c, k));
  CHECK(compter(v, "DRC-43") == 0);
  int courts = 0;
  for (const auto& x : v)
    if ((x.code == "DRC-21" || x.code == "DRC-01") && distance(x.position, pads[0].pastille.position) < 2'000'000) ++courts;
  CHECK(courts == 0);
  // Gravité réglée : la DRC-43 ignorée n'est plus rendue
  c.circuit.pistes.pop_back();
  c.erc.gravites["DRC-43"] = GraviteReglee::Ignorer;
  k = connectivite(c);
  CHECK(compter(verifierDRCReglee(c, k, remplirZones(c, k)), "DRC-43") == 0);
  CHECK(compter(verifierDRC(c, k, remplirZones(c, k)), "DRC-43") == 1);
}

TEST_CASE("2.34 : un projet de la 0.31 écrit pour la 0.30 dit ce qu'elle n'applique pas, et revient intact", "[L2][ECH-11]") {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  ConceptionElec c = exempleElectronique();
  c.erc.gravites["DRC-43"] = GraviteReglee::Ignorer;
  c.erc.exclusions.push_back({"ERC-26", {"x"}, "valeur au montage", "essai", "2026-10-22", {}});
  SymbolePlace* s = nullptr;
  for (auto& f : c.feuilles)
    for (auto& x : f.symboles)
      if (!s && !x.composant.empty()) s = &x;
  REQUIRE(s);
  s->champValeur = ChampPlace{ptMm(2.54, 5.08), true};
  c.feuilles.front().etiquettes.push_back({nouvelUuid(), "HORS", ptMm(10, 10), 0, TypeEtiquette::HorsFeuille, SensSignal::Entree});
  p.electronique.push_back(c);
  CHECK(prom::versionConception(prom::versJson(p)["electronique"][0]) == prom::versionFichierCourante());
  const prom::FichierAnterieur f = prom::pourVersion(p, "0.30");
  const auto& ce = f.projet["electronique"][0];
  CHECK(ce["format"] == 2);
  CHECK(ce["formatElectronique"] == 2);
  CHECK(prom::versionConception(ce) == "0.30");
  auto dit = [&f](const std::string& m) { return std::any_of(f.pertes.begin(), f.pertes.end(), [&](const std::string& x) { return x.find(m) != std::string::npos; }); };
  CHECK(dit("gravités réglées de 1 contrôle"));
  CHECK(dit("1 alerte exclue"));
  CHECK(dit("champ de symbole placé ou bloqué"));
  CHECK(dit("connecteur hors feuille"));
  CHECK(dit("sens des signaux"));
  // Relu par la 0.31 (montée), rien n'a été perdu : la 0.30 garde ce qu'elle ne connaît pas
  std::vector<std::string> notes;
  const prom::Projet relu = prom::normaliser(f.projet, &notes);
  REQUIRE(relu.electronique.size() == 1);
  const ConceptionElec& r = relu.electronique.front();
  CHECK(r.erc.gravites.at("DRC-43") == GraviteReglee::Ignorer);
  CHECK(r.erc.exclusions.size() == 1);
  bool champ = false, hors = false;
  for (const auto& fe : r.feuilles) {
    for (const auto& x : fe.symboles)
      if (x.uuid == s->uuid) champ = x.champValeur && x.champValeur->bloque && x.champValeur->decalage == ptMm(2.54, 5.08);
    for (const auto& e : fe.etiquettes) hors = hors || (e.texte == "HORS" && e.type == TypeEtiquette::HorsFeuille && e.sens == SensSignal::Entree);
  }
  CHECK(champ);
  CHECK(hors);
  CHECK(prom::versJson(relu)["electronique"][0]["format"] == FORMAT_ELECTRONIQUE);
}
