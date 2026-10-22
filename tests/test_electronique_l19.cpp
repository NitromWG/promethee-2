// Prométhée : essais du lot L19 (équipe, cycle de vie, exigences). Licence GPL-3.0-only.
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/equipe.hpp"
#include "promethee/modele.hpp"

using namespace prom;
using namespace prom::ecad;

TEST_CASE("19.3 · Schémas de révision : lettres sans I, O, Q, S, X, Z", "[L19][revisions]") {
  CHECK(revisionSuivante("", SchemaRevision::Lettres) == "A");
  CHECK(revisionSuivante("H", SchemaRevision::Lettres) == "J");
  CHECK(revisionSuivante("N", SchemaRevision::Lettres) == "P");
  CHECK(revisionSuivante("R", SchemaRevision::Lettres) == "T");
  CHECK(revisionSuivante("W", SchemaRevision::Lettres) == "Y");
  CHECK(revisionSuivante("Y", SchemaRevision::Lettres) == "AA");
  CHECK(revisionSuivante("AY", SchemaRevision::Lettres) == "BA");
  CHECK_FALSE(revisionValide("I", SchemaRevision::Lettres));
  CHECK(revisionSuivante("9", SchemaRevision::Nombres) == "10");
  CHECK(revisionSuivante("A3", SchemaRevision::LettreChiffre, false) == "A4");
  CHECK(revisionSuivante("A3", SchemaRevision::LettreChiffre, true) == "B1");
  CHECK(revisionSuivante("H7", SchemaRevision::LettreChiffre, true) == "J1");
}

TEST_CASE("19.2, 19.3, 19.5 · Équipe et rôles, passages du cycle de vie, numéros internes", "[L19][equipe]") {
  const Equipe e = lireEquipe(R"({"nom":"Bureau","personnes":[{"nom":"Lou","role":"responsable"},{"nom":"Sam","role":"concepteur"},
                                 {"nom":"Ana","role":"relecteur"},{"nom":"Tom","role":"observateur"}]})");
  REQUIRE(e.personnes.size() == 4);
  CHECK(lireEquipe(ecrireEquipe(e)).personnes.size() == 4);
  CHECK(peut(Role::Concepteur, "modifier"));
  CHECK_FALSE(peut(Role::Concepteur, "publier"));
  CHECK(peut(Role::Relecteur, "approuver"));
  CHECK_FALSE(peut(Role::Observateur, "modifier"));
  CHECK(peut(Role::Observateur, "commenter"));
  ConceptionElec c = exempleElectronique();
  Article a;
  a.id = "ne555";
  a.mpn = "NE555DR";
  a.numeroInterne = "PRM-000041";
  c.articles["ne555"] = a;
  CHECK(changerEtatArticle(c, "ne555", "pré-série", Role::Concepteur).empty());
  CHECK(!changerEtatArticle(c, "ne555", "production", Role::Concepteur).empty());
  CHECK(changerEtatArticle(c, "ne555", "production", Role::Responsable).empty());
  CHECK(!changerEtatArticle(c, "ne555", "obsolète", Role::Responsable).empty());  // pas de saut d'état
  CHECK(changerEtatArticle(c, "ne555", "fin de série", Role::Responsable).empty());
  CHECK(changerEtatArticle(c, "ne555", "obsolète", Role::Responsable).empty());
  CHECK(!changerEtatArticle(c, "ne555", "fin de série", Role::Responsable).empty());  // personne ne revient d'« obsolète »
  CHECK(numeroInterneSuivant(c) == "PRM-000042");
  CHECK(articleParNumero(c, "PRM-000041") == &c.articles["ne555"]);
  CHECK(articleParNumero(c, "NE555DR") == &c.articles["ne555"]);
  CHECK(articleParNumero(c, "rien") == nullptr);
}

TEST_CASE("19.6 · Ordre de modification : relecteur et responsable, puis la révision passe", "[L19][ordres]") {
  const Equipe e = lireEquipe(R"({"personnes":[{"nom":"Lou","role":"responsable"},{"nom":"Sam","role":"concepteur"},{"nom":"Ana","role":"relecteur"}]})");
  ConceptionElec c = exempleElectronique();
  c.cartouche.revision = "A";
  OrdreModification o;
  o.id = "OM-7";
  o.titre = "R1 passe à 2k2";
  o.revisionDepart = "A";
  o.objets = {"R1"};
  CHECK(!approuver(o, e, "Sam", true, "2026-10-22").empty());  // un concepteur n'approuve pas
  CHECK(!appliquerOrdre(o, c).empty());
  CHECK(approuver(o, e, "Ana", true, "2026-10-22").empty());
  CHECK(o.etat == "ouvert");
  CHECK(!approuver(o, e, "Ana", true, "2026-10-22").empty());  // une seule fois
  CHECK(approuver(o, e, "Lou", true, "2026-10-23").empty());
  CHECK(o.etat == "approuvé");
  CHECK(appliquerOrdre(o, c).empty());
  CHECK(c.cartouche.revision == "B");
  CHECK(o.etat == "appliqué");
  CHECK(o.revisionArrivee == "B");
  OrdreModification refuse;
  refuse.id = "OM-8";
  CHECK(approuver(refuse, e, "Lou", false, "2026-10-23", "pas maintenant").empty());
  CHECK(refuse.etat == "refusé");
}

TEST_CASE("19.8, 19.9 · Exigences vérifiées : coût pour 100 pièces, nombre de composants, liens", "[L19][exigences]") {
  Projet p = projetVide();
  ConceptionElec c = exempleElectronique();
  int n = 0;
  for (const auto& l : nomenclature(c)) n += l.quantite;
  // Un prix pour chaque composant : 0,10 € à 100
  int i = 0;
  for (auto& k : c.composants) {
    Article a;
    a.id = "a" + std::to_string(++i);
    a.mpn = "MPN" + std::to_string(i);
    OffreArticle o;
    o.prix = {{1, 0.2}, {100, 0.1}};
    a.offres.push_back(o);
    c.articles[a.id] = a;
    k.article = a.id;
  }
  p.electronique.push_back(c);
  p.exigences.push_back({"EX-COUT", "Coût ≤ 40 € pour 100", "client", "analyse", {}, "cout", "<=", "40 €", 100});
  p.exigences.push_back({"EX-PEU", "Coût ≤ 0,5 € pour 100", "client", "analyse", {}, "cout", "<=", "0,5 €", 100});
  p.exigences.push_back({"EX-COMP", "Au plus 30 composants", "", "analyse", {}, "composants", "<=", "30", 1});
  p.exigences.push_back({"EX-LIEN", "R1 dimensionnée", "", "revue", {"R1", "R99"}, "", "<=", "", 1});
  p.exigences.push_back({"EX-CONSO", "Consommation ≤ 3 W", "", "essai", {}, "consommation", "<=", "3 W", 1});
  const auto v = verifierExigences(p);
  REQUIRE(v.size() == 5);
  CHECK(v[0].etat == "vérifiée");
  CHECK(v[0].mesure.find(std::to_string(100)) != std::string::npos);
  CHECK(v[1].etat == "en défaut");  // n × 0,10 € > 0,5 €
  CHECK(v[2].etat == (n <= 30 ? "vérifiée" : "en défaut"));
  CHECK(v[3].etat == "en défaut");
  CHECK(v[3].liensManquants == std::vector<std::string>{"R99"});
  CHECK(v[4].etat == "non vérifiable");
  p.electronique[0].composants[0].champs["Consommation"] = "500 mW";
  CHECK(verifierExigences(p)[4].etat == "vérifiée");
}
