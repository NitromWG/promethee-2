// Prométhée : essais de la boîte à calculs de l'électricien (valeurs de référence calculées à la main).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electrique.hpp"
#include "promethee/electrique_calculateurs.hpp"
#include "promethee/electrique_distribution.hpp"
#include "promethee/electrique_glossaire.hpp"
#include "promethee/electrique_parcours.hpp"
#include "promethee/electrique_pv.hpp"

using namespace prom::elec;
using Catch::Approx;

namespace {

ResultatCalcul calc(const std::string& id, const std::vector<double>& v = {}) {
  const Calculateur* c = calculateur(id);
  REQUIRE(c);
  return executer(*c, v);
}
std::string ligne(const ResultatCalcul& r, const std::string& nom) {
  for (const auto& l : r.lignes)
    if (l.nom.find(nom) != std::string::npos) return l.valeur;
  return {};
}
double valeur(const ResultatCalcul& r, const std::string& nom) {
  std::string t = ligne(r, nom);
  std::replace(t.begin(), t.end(), ',', '.');
  std::string chiffres;
  for (char c : t) {
    if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || (c == '-' && chiffres.empty())) chiffres += c;
    else if (!chiffres.empty() && c != ' ' && static_cast<unsigned char>(c) < 0x80) break;
    else if (!chiffres.empty() && static_cast<unsigned char>(c) >= 0x80) break;
  }
  return chiffres.empty() ? -1 : std::stod(chiffres);
}
bool erreur(const ResultatCalcul& r) {
  return std::any_of(r.avis.begin(), r.avis.end(), [](const AvisCalcul& a) { return a.niveau == NiveauAvis::Erreur; });
}

}  // namespace

TEST_CASE("Calculs : chaque calculateur répond avec ses valeurs par défaut") {
  REQUIRE(calculateursElectriques().size() >= 18);
  for (const auto& c : calculateursElectriques()) {
    INFO(c.id);
    CHECK(!c.nom.empty());
    CHECK(std::find(famillesCalculateurs().begin(), famillesCalculateurs().end(), c.famille) != famillesCalculateurs().end());
    const ResultatCalcul r = executer(c);
    CHECK(!r.lignes.empty());
    CHECK(!r.formule.empty());
    CHECK(!r.explication.empty());
    for (const auto& ch : c.champs) CHECK(ch.valeur >= ch.min);
  }
}

TEST_CASE("Calculs : sections, chutes, courants, courts-circuits, longueurs, contraintes") {
  // Section : 16 A, 25 m, monophasé, B1, PR → 1,5 mm² (Iz 23 A ; chute 4,7 %)
  const ResultatCalcul s = calc("section");
  CHECK(ligne(s, "Section") == "1,5 mm²");
  CHECK(s.explication.find("protégé par") != std::string::npos);
  // Chute : 20 A, 30 m, 2,5 mm² cuivre, monophasé, cos φ 0,9 → 9,76 V, 4,23 %
  const ResultatCalcul c = calc("chute");
  CHECK(valeur(c, "En volts") == Approx(2 * (0.0225 * 30 / 2.5 * 0.9 + 0.00008 * 30 * std::sqrt(1 - 0.81)) * 20).margin(0.01));
  CHECK(valeur(c, "Chute de tension") == Approx(4.23).margin(0.01));
  CHECK_FALSE(erreur(c));
  // Courant : 10 kW triphasé 400 V, cos φ 0,85 → 17,0 A
  CHECK(valeur(calc("courant"), "Courant d’emploi") == Approx(10000 / (std::sqrt(3.0) * 400 * 0.85)).margin(0.05));
  // Court-circuit : 10 kA au départ, 50 m de 2,5 mm² → 0,67 kA maximal, 0,25 kA minimal (≥ 160 A : C 16 déclenche)
  const ResultatCalcul k = calc("icc");
  CHECK(valeur(k, "maximal") == Approx(0.67).margin(0.01));
  CHECK(valeur(k, "minimal") == Approx(0.247).margin(0.005));
  CHECK_FALSE(erreur(k));
  // Longueur maximale : 2,5 mm², PE identique, C 16 A en TN → 62 m
  CHECK(valeur(calc("lmax"), "Longueur maximale") == Approx(0.8 * 230.94 * 2.5 / (1.25 * 0.01851 * 2 * 160)).margin(0.6));
  // Contrainte thermique : 2,5 mm² PR, 6 kA pendant 10 ms → dépassée ; durée admissible 3,6 ms
  const ResultatCalcul t = calc("thermique");
  CHECK(erreur(t));
  CHECK(valeur(t, "Durée admissible") == Approx(std::pow(143 * 2.5 / 6000, 2)).margin(0.0005));
  // Conducteur de protection : 50 mm² → 25 mm² (tableau 54F)
  CHECK(ligne(calc("pe", {static_cast<double>(std::find(sectionsNormalisees().begin(), sectionsNormalisees().end(), 50.0) - sectionsNormalisees().begin())}), "Section du PE") ==
        "25 mm²");
}

TEST_CASE("Calculs : réseau, terre, éclairage, bases") {
  CHECK(valeur(calc("compensation"), "Batterie") == Approx(40));
  CHECK(valeur(calc("transformateur"), "Transformateur") == Approx(400));
  const ResultatCalcul d = calc("demarrage");
  CHECK(valeur(d, "Courant nominal") == Approx(15000 / (0.9 * std::sqrt(3.0) * 400 * 0.85)).margin(0.05));
  CHECK(valeur(d, "Courant de démarrage") == Approx(7 * 15000 / (0.9 * std::sqrt(3.0) * 400 * 0.85)).margin(1));
  // Pertes : 50 A, 50 m de 16 mm², triphasé, 3 000 h à 0,20 € → 527 W, 316 €
  const ResultatCalcul p = calc("pertes");
  CHECK(valeur(p, "Pertes") == Approx(3 * 0.0225 * 50 / 16 * 2500).margin(1));
  CHECK(valeur(p, "Coût") == Approx(3 * 0.0225 * 50 / 16 * 2500 * 3 * 0.2).margin(1));
  CHECK(valeur(calc("batterie"), "Autonomie") == Approx(100 * 0.8 / (1000 / (0.9 * 24))).margin(0.01));
  // Prise de terre : piquet de 2 m dans 100 Ω·m → 50 Ω ; avec 500 mA : 25 V, conforme
  const ResultatCalcul e = calc("terre");
  CHECK(valeur(e, "Résistance de la prise") == Approx(50));
  CHECK_FALSE(erreur(e));
  CHECK(erreur(calc("terre", {1000, 0, 2, 4, 0})));  // sol rocheux, 1 A : 500 V
  // Éclairage : 80 m² de bureau à 500 lx, 3 600 lm, U 0,6, M 0,8 → 24 luminaires
  CHECK(valeur(calc("eclairage"), "Luminaires") == Approx(24));
  CHECK(valeur(calc("baes"), "Blocs d’évacuation") == Approx(8));
  CHECK(valeur(calc("baes", {60, 2, 2, 400, 360, 3}), "Blocs d’ambiance") == Approx(6));
  // Loi d'Ohm : 230 V et 2 000 W → 8,70 A et 26,45 Ω
  const ResultatCalcul o = calc("ohm");
  CHECK(valeur(o, "Courant") == Approx(2000.0 / 230).margin(0.001));
  CHECK(valeur(o, "Résistance") == Approx(230.0 * 230 / 2000).margin(0.01));
  CHECK(ligne(calc("awg"), "Jauge") == "AWG 13");
  CHECK(valeur(calc("awg", {1, 10}), "Section") == Approx(5.26).margin(0.01));
  // Armoire 600 × 800 × 250 contre un mur, 150 W, 35 °C dehors, 45 °C admis → 57 °C sans ventilation, 25 m³/h
  const ResultatCalcul a = calc("armoire");
  CHECK(valeur(a, "Température intérieure") == Approx(35 + 150 / (5.5 * (1.4 * 0.6 * 1.05 + 1.8 * 0.25 * 0.8))).margin(0.1));
  CHECK(valeur(a, "Ventilation") == Approx(3.1 * (150 - 5.5 * 1.242 * 10) / 10).margin(0.6));
}

TEST_CASE("Glossaire : fiches et recherche sans accents") {
  REQUIRE(glossaireElectrique().size() >= 40);
  for (const auto& t : glossaireElectrique()) {
    INFO(t.cle);
    CHECK(!t.terme.empty());
    CHECK(!t.definition.empty());
    CHECK(termeGlossaire(t.cle) == &t);
  }
  CHECK(termeGlossaire("iz") != nullptr);
  const auto r = chercherGlossaire("selectivite");
  REQUIRE(!r.empty());
  CHECK(r.front()->cle == "selectivite");
  CHECK(!chercherGlossaire("différentiel 30").empty());
}

TEST_CASE("Glossaire : chaque symbole de la bibliothèque a sa fiche « C'est quoi ? »") {
  for (const auto& d : bibliothequeElectrique()) {
    INFO(d.id);
    const AideSymbole* a = aideSymbole(d.id);
    REQUIRE(a != nullptr);
    CHECK(a->quoi.size() > 20);
    CHECK(!a->usage.empty());
  }
  CHECK(aideSymbole("iec:inexistant") == nullptr);
}

namespace {
// Indice de la première étape vérifiable encore à faire (−1 : toutes faites)
int aFaire(const Parcours& m, const ProjetElectrique& p) {
  for (std::size_t i = 0; i < m.etapes.size(); ++i)
    if (m.etapes[i].faite && !m.etapes[i].faite(p)) return static_cast<int>(i);
  return -1;
}
}  // namespace

TEST_CASE("Parcours : chaque étape se lit, renvoie à une fiche qui existe, et se valide sur un vrai projet") {
  REQUIRE(parcoursElectriques().size() >= 4);
  for (const auto& m : parcoursElectriques()) {
    INFO(m.id);
    CHECK(!m.nom.empty());
    CHECK(!m.etapes.empty());
    CHECK(parcoursElectrique(m.id) == &m);
    for (const auto& e : m.etapes) {
      INFO(e.titre);
      CHECK(!e.pourquoi.empty());
      CHECK(!e.comment.empty());
      if (!e.glossaire.empty()) CHECK(termeGlossaire(e.glossaire) != nullptr);
    }
  }
  // Démarrage d'un moteur : le circuit type remplit toutes les étapes, une fois les fils numérotés
  {
    const Parcours& m = *parcoursElectrique("moteur");
    ProjetElectrique p = nouveauProjetElectrique("Moteur", true);
    CHECK(aFaire(m, p) == 0);
    insererCircuitType(p, CircuitType::DemarrageDirect);
    numeroterFils(p, false);
    CHECK(aFaire(m, p) == -1);
  }
  // Installation : vide, prises, 60 m, moteur, unifilaire
  {
    const Parcours& m = *parcoursElectrique("installation");
    ProjetElectrique p = nouveauProjetElectrique("Installation", false);
    CHECK(aFaire(m, p) == 0);
    p.distribution = reseauModele(ModeleReseau::Vide);
    CHECK(aFaire(m, p) == 1);
    std::string tgbt;
    for (const auto& d : p.distribution->departs)
      if (d.charge == TypeCharge::Tableau) tgbt = d.uuid;
    p.distribution->departs.push_back(nouveauDepart(*p.distribution, TypeCharge::Prises, tgbt));
    CHECK(aFaire(m, p) == 3);
    p.distribution->departs.back().longueur = 60;
    CHECK(aFaire(m, p) == 4);
    p.distribution->departs.push_back(nouveauDepart(*p.distribution, TypeCharge::Moteur, tgbt));
    CHECK(aFaire(m, p) == 5);
    genererUnifilaire(p);
    CHECK(aFaire(m, p) == -1);
  }
  // Photovoltaïque : taille, site, orientation, module, unifilaire
  {
    const Parcours& m = *parcoursElectrique("photovoltaique");
    ProjetElectrique p = nouveauProjetElectrique("Toit", false);
    CHECK(aFaire(m, p) == 0);
    p.photovoltaique = installationPVType(6);
    CHECK(aFaire(m, p) == 2);
    p.photovoltaique->tMin = -20;
    p.photovoltaique->orientation = 90;
    p.photovoltaique->module = modulesPV()[2];
    CHECK(aFaire(m, p) == 5);
    genererUnifilairePV(p);
    CHECK(aFaire(m, p) == -1);
  }
  // Logement : le projet tiré de l'assistant est conforme
  {
    const Parcours& m = *parcoursElectrique("logement");
    const ProjetElectrique p = projetLogement(logementType(80, 3));
    CHECK(aFaire(m, p) == -1);
  }
}
