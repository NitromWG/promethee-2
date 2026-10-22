// Prométhée : essais du Grafcet (NF EN CEI 60848) — lecture des réceptivités et des actions, vérification, règles
// d'évolution, temporisations, fronts, dessin, texte structuré, fichier. Licence GPL-3.0-only.
#include <algorithm>
#include <set>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electrique.hpp"
#include "promethee/electrique_grafcet.hpp"

using namespace prom::elec;

namespace {

std::string normal(const std::string& t) {
  const LectureReceptivite l = lireReceptivite(t);
  return l.expression ? texteReceptivite(*l.expression) : "✗ " + l.erreur;
}

bool aAvis(const std::vector<AvisCalcul>& v, NiveauAvis n, const std::string& texte) {
  return std::any_of(v.begin(), v.end(), [&](const AvisCalcul& a) { return a.niveau == n && a.message.find(texte) != std::string::npos; });
}

Grafcet sequence(std::vector<std::pair<int, std::string>> etapes, std::vector<std::tuple<std::vector<int>, std::vector<int>, std::string>> transitions) {
  Grafcet g;
  g.uuid = "g";
  for (const auto& [n, actions] : etapes) {
    EtapeGrafcet e;
    e.numero = n;
    e.initiale = n == 0;
    if (!actions.empty()) {
      ActionGrafcet a;
      REQUIRE(lireAction(actions, a));
      e.actions.push_back(a);
    }
    g.etapes.push_back(e);
  }
  for (const auto& [a, b, r] : transitions) g.transitions.push_back({a, b, r, {}});
  return g;
}

}  // namespace

TEST_CASE("Grafcet : réceptivités lues comme on les écrit") {
  CHECK(normal("dcy·a0") == "dcy · a0");
  CHECK(normal("dcy.a0 + /b") == "dcy · a0 + /b");
  CHECK(normal("dcy et non a ou b") == "dcy · /a + b");
  CHECK(normal("/(a+b)·c") == "/(a + b) · c");
  CHECK(normal("(a + b) * c") == "(a + b) · c");
  CHECK(normal("↑S1") == "↑S1");
  CHECK(normal("^S1") == "↑S1");
  CHECK(normal("↓X3") == "↓X3");
  CHECK(normal("5s/X3") == "5s/X3");
  CHECK(normal("500ms/X2") == "500ms/X2");
  CHECK(normal("2min/X0") == "2min/X0");
  CHECK(normal("1,5s/X1") == "1,5s/X1");
  CHECK(normal("t/X3/5s") == "5s/X3");  // ancienne écriture
  CHECK(normal("=1") == "1");
  CHECK(normal("X12 · arrêt") == "X12 · arrêt");
  // Erreurs expliquées, avec la position
  const LectureReceptivite e1 = lireReceptivite("a b");
  CHECK(!e1.expression);
  CHECK(e1.erreur.find("opérateur attendu") != std::string::npos);
  CHECK(e1.position == 2);
  CHECK(lireReceptivite("(a+b").erreur.find("parenthèse") != std::string::npos);
  CHECK(lireReceptivite("10").erreur.find("unité de temps") != std::string::npos);
  CHECK(lireReceptivite("↑(a)").erreur.find("front") != std::string::npos);
  CHECK(!lireReceptivite("").expression);
  // Entrées lues (sans les variables d'étape)
  std::vector<std::string> v;
  variablesReceptivite(*lireReceptivite("dcy · /a0 + ↑S1 · X2 + 3s/X4").expression, v);
  CHECK(v == std::vector<std::string>{"dcy", "a0", "S1"});
}

TEST_CASE("Grafcet : actions continues, conditionnelles et mémorisées") {
  ActionGrafcet a;
  REQUIRE(lireAction("KM1", a));
  CHECK(a.type == ActionGrafcet::Type::Continue);
  CHECK(a.sortie == "KM1");
  REQUIRE(lireAction("KM2 si a·b", a));
  CHECK(a.condition == "a·b");
  CHECK(texteAction(a) == "KM2 si a·b");
  REQUIRE(lireAction("H1 := 1", a));
  CHECK(a.type == ActionGrafcet::Type::Activation);
  CHECK(a.valeur);
  REQUIRE(lireAction("R H1", a));
  CHECK(a.type == ActionGrafcet::Type::Activation);
  CHECK(!a.valeur);
  CHECK(texteAction(a) == "H1 := 0");
  REQUIRE(lireAction("↓ H1 := 0", a));
  CHECK(a.type == ActionGrafcet::Type::Desactivation);
  std::string erreur;
  CHECK(!lireAction("H1 := 2", a, &erreur));
  CHECK(erreur.find("1 ou 0") != std::string::npos);
  CHECK(!lireAction("KM1 si (a", a, &erreur));
  CHECK(!lireAction("a+b", a, &erreur));
}

TEST_CASE("Grafcet : vérification (règles de syntaxe et de bon sens)") {
  for (const auto& [id, nom] : exemplesGrafcet()) {
    INFO(nom);
    const auto v = verifierGrafcet(exempleGrafcet(id));
    CHECK(aAvis(v, NiveauAvis::Info, "Grafcet conforme"));
    CHECK(std::none_of(v.begin(), v.end(), [](const AvisCalcul& a) { return a.niveau != NiveauAvis::Info; }));
  }
  Grafcet g = sequence({{0, ""}, {1, "KM1"}, {2, "KM2"}, {3, ""}}, {{{0}, {1}, "a"}, {{0}, {2}, "a + b"}, {{1}, {9}, "c"}, {{2}, {0}, "d d"}});
  g.etapes[0].initiale = false;
  const auto v = verifierGrafcet(g);
  CHECK(aAvis(v, NiveauAvis::Erreur, "Aucune étape initiale"));
  CHECK(aAvis(v, NiveauAvis::Erreur, "mène à l'étape 9, qui n'existe pas"));
  CHECK(aAvis(v, NiveauAvis::Erreur, "illisible"));
  CHECK(aAvis(v, NiveauAvis::Alerte, "L'étape 3 n'a pas de transition en aval"));
  // Divergence en OU non exclusive : « a » et « a + b » vrais ensemble
  g.etapes[0].initiale = true;
  const auto v2 = verifierGrafcet(g);
  CHECK(aAvis(v2, NiveauAvis::Alerte, "peuvent être vraies ensemble (a = 1"));
  CHECK(aAvis(v2, NiveauAvis::Alerte, "L'étape 3 n'est jamais atteinte"));
  // Exclusives : plus d'alerte
  g.transitions[1].receptivite = "/a · b";
  CHECK(!aAvis(verifierGrafcet(g), NiveauAvis::Alerte, "vraies ensemble"));
  // Étape définie deux fois
  g.etapes.push_back(g.etapes[1]);
  CHECK(aAvis(verifierGrafcet(g), NiveauAvis::Erreur, "L'étape 1 existe 2 fois"));
  // Évolution fugace signalée
  Grafcet f = sequence({{0, ""}, {1, "KM1"}, {2, ""}}, {{{0}, {1}, "a"}, {{1}, {2}, "1"}, {{2}, {0}, "b"}});
  CHECK(aAvis(verifierGrafcet(f), NiveauAvis::Info, "évolution fugace"));
}

TEST_CASE("Grafcet : règles d'évolution, sorties, fronts, temporisations") {
  // Chariot : 0 →(dcy·a) 1 [KM1] →(b) 2 [KM2] →(a) 0
  SimulateurGrafcet s(exempleGrafcet("chariot"));
  CHECK(s.actives() == std::set<int>{0});
  s.fixer("a", true);
  s.evoluer();
  CHECK(s.actives() == std::set<int>{0});
  s.fixer("dcy", true);
  CHECK(s.franchissables().size() == 1);
  CHECK(s.evoluer() == 1);
  CHECK(s.actives() == std::set<int>{1});
  CHECK(s.sorties().at("KM1"));
  CHECK(!s.sorties().at("KM2"));
  s.fixer("dcy", false);
  s.fixer("a", false);
  s.fixer("b", true);
  s.evoluer();
  CHECK(s.actives() == std::set<int>{2});
  CHECK(s.sorties().at("KM2"));
  s.fixer("b", false);
  s.fixer("a", true);
  s.evoluer();
  CHECK(s.actives() == std::set<int>{0});
  // Évolution fugace : on traverse l'étape 1 sans émettre son action continue, mais l'action mémorisée s'exécute
  Grafcet f = sequence({{0, ""}, {1, "KM1"}, {2, ""}}, {{{0}, {1}, "a"}, {{1}, {2}, "1"}, {{2}, {0}, "b"}});
  ActionGrafcet m;
  REQUIRE(lireAction("H1 := 1", m));
  f.etape(1)->actions.push_back(m);
  SimulateurGrafcet sf(f);
  sf.fixer("a", true);
  CHECK(sf.evoluer() == 2);
  CHECK(sf.actives() == std::set<int>{2});
  CHECK(!sf.sorties().at("KM1"));
  CHECK(sf.sorties().at("H1"));
  // Front montant : pris une seule fois
  Grafcet fr = sequence({{0, ""}, {1, "V1"}}, {{{0}, {1}, "↑p"}, {{1}, {0}, "↑p"}});
  SimulateurGrafcet sr(fr);
  sr.fixer("p", true);
  sr.evoluer();
  CHECK(sr.actives() == std::set<int>{1});  // l'événement ne fait pas aussi franchir le retour
  sr.evoluer();
  CHECK(sr.actives() == std::set<int>{1});
  sr.fixer("p", false);
  sr.evoluer();
  sr.fixer("p", true);
  sr.evoluer();
  CHECK(sr.actives() == std::set<int>{0});
  // Temporisations : feux tricolores
  SimulateurGrafcet t(exempleGrafcet("feux"));
  t.avancer(19.5);
  CHECK(t.active(0));
  t.avancer(0.5);
  CHECK(t.active(1));
  CHECK(t.sorties().at("OA"));
  t.avancer(3);
  CHECK(t.active(2));
  CHECK(t.dureeActive(2) == Catch::Approx(0));
}

TEST_CASE("Grafcet : divergences et convergences en ET (règles 3 à 5)") {
  SimulateurGrafcet s(exempleGrafcet("poste"));
  s.fixer("dcy", true);
  s.evoluer();
  CHECK(s.actives() == std::set<int>{1, 3});  // divergence en ET : deux séquences en parallèle
  CHECK(s.sorties().at("KM1"));
  CHECK(s.sorties().at("KM2"));
  s.fixer("fp", true);
  s.evoluer();
  CHECK(s.actives() == std::set<int>{2, 3});  // la convergence attend l'autre branche
  s.fixer("ft", true);
  s.evoluer();
  CHECK(s.actives() == std::set<int>{5});  // les deux attentes actives : « 1 » franchie aussitôt
  // Règle 5 : une étape activée et désactivée en même temps reste active
  Grafcet g = sequence({{0, ""}, {1, ""}}, {{{0}, {0, 1}, "a"}});
  SimulateurGrafcet r(g);
  r.fixer("a", true);
  r.evoluer();
  CHECK(r.actives() == std::set<int>{0, 1});
  CHECK(!r.instable());
  // Cycle de réceptivités toujours vraies : instable, signalé
  Grafcet c = sequence({{0, ""}, {1, ""}}, {{{0}, {1}, "1"}, {{1}, {0}, "1"}});
  SimulateurGrafcet ci(c);
  CHECK(ci.instable());
}

TEST_CASE("Grafcet : dessin normalisé") {
  for (const auto& [id, nom] : exemplesGrafcet()) {
    INFO(nom);
    const Grafcet g = exempleGrafcet(id);
    const DispositionGrafcet d = disposerGrafcet(g);
    CHECK(d.largeur > 20);
    CHECK(d.hauteur > 20);
    // Un carré par étape (double pour l'initiale), une barre par transition, sans chevauchement des étapes
    std::vector<BoiteGrafcet> carres;
    for (const auto& b : d.boites)
      if (b.etape >= 0) carres.push_back(b);
    CHECK(carres.size() == g.etapes.size());
    for (std::size_t i = 0; i < carres.size(); ++i) {
      CHECK(carres[i].double_ == g.etape(carres[i].etape)->initiale);
      for (std::size_t j = i + 1; j < carres.size(); ++j) {
        const bool separes = carres[i].x + carres[i].l <= carres[j].x || carres[j].x + carres[j].l <= carres[i].x || carres[i].y + carres[i].h <= carres[j].y ||
                             carres[j].y + carres[j].h <= carres[i].y;
        CHECK(separes);
      }
    }
    CHECK(std::count_if(d.traits.begin(), d.traits.end(), [](const TraitGrafcet& t) { return t.epais; }) == static_cast<long>(g.transitions.size()));
    // Tout est dans le cadre, positif
    for (const auto& t : d.traits) {
      CHECK(std::min(t.x0, t.x1) >= 0);
      CHECK(std::max(t.x0, t.x1) <= d.largeur);
      CHECK(std::min(t.y0, t.y1) >= 0);
    }
  }
  // Le retour vers l'étape initiale remonte, avec sa flèche
  const DispositionGrafcet c = disposerGrafcet(exempleGrafcet("chariot"));
  CHECK(std::any_of(c.traits.begin(), c.traits.end(), [](const TraitGrafcet& t) { return t.fleche && t.y1 < t.y0; }));
  // Divergence en ET : doubles traits
  const DispositionGrafcet p = disposerGrafcet(exempleGrafcet("poste"));
  int horizontaux = 0;
  for (const auto& t : p.traits)
    if (!t.epais && t.y0 == t.y1 && std::abs(t.x1 - t.x0) > 20) ++horizontaux;
  CHECK(horizontaux >= 4);  // double trait sous la transition 1, double trait au-dessus de la convergence
}

TEST_CASE("Grafcet : texte structuré CEI 61131-3") {
  const std::string st = texteStructureGrafcet(exempleGrafcet("tri"));
  CHECK(st.find("FUNCTION_BLOCK FB_Tri") != std::string::npos);
  CHECK(st.find("X0 : BOOL := TRUE;") != std::string::npos);
  CHECK(st.find("Tr3 := X1 AND c AND NOT g;") != std::string::npos);
  CHECK(st.find("X4 := (X4 AND NOT (Tr6)) OR Tr4 OR Tr5;") != std::string::npos);
  CHECK(st.find("TX4_2000MS(IN := X4, PT := T#2S);") != std::string::npos);
  CHECK(st.find("KM1 := X1;") != std::string::npos);
  CHECK(st.find("END_FUNCTION_BLOCK") != std::string::npos);
  // Fronts et actions mémorisées
  Grafcet g = sequence({{0, ""}, {1, "H1 := 1"}, {2, "R H1"}}, {{{0}, {1}, "↑dcy"}, {{1}, {2}, "a"}, {{2}, {0}, "b"}});
  const std::string s2 = texteStructureGrafcet(g);
  CHECK(s2.find("R_dcy : R_TRIG;") != std::string::npos);
  CHECK(s2.find("R_dcy(CLK := dcy);") != std::string::npos);
  CHECK(s2.find("IF Act1.Q THEN M_H1 := TRUE; END_IF;") != std::string::npos);
  CHECK(s2.find("IF Act2.Q THEN M_H1 := FALSE; END_IF;") != std::string::npos);
  CHECK(s2.find("H1 := M_H1;") != std::string::npos);
}

TEST_CASE("Grafcet : enregistré avec le projet, son folio aussi") {
  ProjetElectrique p = nouveauProjetElectrique("Automatisme");
  Grafcet g = exempleGrafcet("tri");
  g.uuid = "grafcet-1";
  g.etapes[1].commentaire = "tapis";
  ActionGrafcet a;
  REQUIRE(lireAction("H2 := 1", a));
  g.etapes[4].actions.push_back(a);
  p.grafcets.push_back(g);
  const std::size_t f = ajouterFolio(p, TypeFolio::Grafcet, "Grafcet du tri");
  p.folios[p.schema.feuilles[f].uuid].grafcet = g.uuid;
  CHECK(folioDessineDOffice(TypeFolio::Grafcet));
  const ProjetElectrique r = projetElectriqueDepuisJson(versJson(p));
  REQUIRE(r.grafcets.size() == 1);
  const Grafcet& x = r.grafcets[0];
  CHECK(x.nom == "Tri");
  CHECK(x.etapes.size() == g.etapes.size());
  CHECK(x.transitions.size() == g.transitions.size());
  CHECK(x.transitions[2].receptivite == "c · /g");
  CHECK(x.transitions[5].receptivite == "2s/X4");
  CHECK(x.etapes[0].initiale);
  CHECK(x.etapes[1].commentaire == "tapis");
  CHECK(texteAction(x.etapes[4].actions.back()) == "H2 := 1");
  CHECK(r.folio(r.schema.feuilles[f].uuid).type == TypeFolio::Grafcet);
  CHECK(r.folio(r.schema.feuilles[f].uuid).grafcet == "grafcet-1");
  CHECK(verifierElectrique(r).empty());
}
