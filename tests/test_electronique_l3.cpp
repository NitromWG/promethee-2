// Prométhée : essais du lot L3 de l'audit du 15 octobre 2026 (« Vérité du composant »), côté noyau : valeurs
// comprises, séries E, paramètres typés, formules, hauteur unique, synchronisation du circuit.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/commandes.hpp"
#include "promethee/derive.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/formules.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/migrations.hpp"
#include "promethee/modele.hpp"
#include "promethee/objets.hpp"
#include "promethee/remplacement.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/valeurs.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
const std::string FINE = " ";  // espace fine insécable avant l'unité
using F = FamilleValeur;

struct CasValeur {
  const char* texte;
  F famille;
  double attendu;  // NAN : refusée
};
bool refusee(const char* t, F f) { return lireValeurComposant(t, f).etat == EtatValeur::Refusee; }
}  // namespace

TEST_CASE("3.2 : la grammaire des valeurs, cas écrits à la main", "[L3][P3]") {
  const double N = NAN;
  const std::vector<CasValeur> cas = {
      // Résistances : RKM, préfixes, unités, virgule ou point
      {"4k7", F::Resistance, 4700}, {"4K7", F::Resistance, 4700}, {"4,7k", F::Resistance, 4700}, {"4.7k", F::Resistance, 4700},
      {"4,7 kΩ", F::Resistance, 4700}, {"4.7kohm", F::Resistance, 4700}, {"4k7Ω", F::Resistance, 4700}, {"4700", F::Resistance, 4700},
      {"4700 Ω", F::Resistance, 4700}, {"4700 ohms", F::Resistance, 4700}, {"4R7", F::Resistance, 4.7}, {"R47", F::Resistance, 0.47},
      {"0R1", F::Resistance, 0.1}, {"R10", F::Resistance, 0.1}, {"0R", F::Resistance, 0}, {"0", F::Resistance, 0},
      {"1M", F::Resistance, 1e6}, {"1m", F::Resistance, 1e-3}, {"15M0", F::Resistance, 15e6}, {"10k", F::Resistance, 1e4},
      {"10K", F::Resistance, 1e4}, {"10 K", F::Resistance, 1e4}, {"2M2", F::Resistance, 2.2e6}, {"1G", F::Resistance, 1e9},
      {"1T", F::Resistance, 1e12}, {"4L7", F::Resistance, 4.7e-3}, {"470R", F::Resistance, 470}, {"1e3", F::Resistance, 1000},
      {"1,5e3", F::Resistance, 1500}, {"2.2E+3", F::Resistance, 2200}, {"  4k7  ", F::Resistance, 4700}, {"4,7 kΩ", F::Resistance, 4700},
      {"4,7 kΩ", F::Resistance, 4700}, {"4,7 kΩ", F::Resistance, 4700}, {"100R", F::Resistance, 100}, {"56k", F::Resistance, 56e3},
      // Résistances refusées
      {"10 µF", F::Resistance, N}, {"10uF", F::Resistance, N}, {"5V", F::Resistance, N}, {"abc", F::Resistance, N},
      {"2T", F::Resistance, N}, {"0,5m", F::Resistance, N}, {"-1k", F::Resistance, N}, {"4k7k", F::Resistance, N},
      // Condensateurs
      {"100n", F::Condensateur, 1e-7}, {"100nF", F::Condensateur, 1e-7}, {"100 nF", F::Condensateur, 1e-7}, {"0,1µF", F::Condensateur, 1e-7},
      {"0.1uF", F::Condensateur, 1e-7}, {"0,1 μF", F::Condensateur, 1e-7}, {"2u2", F::Condensateur, 2.2e-6}, {"4p7", F::Condensateur, 4.7e-12},
      {"6p8", F::Condensateur, 6.8e-12}, {"1n", F::Condensateur, 1e-9}, {"10µF 25V X7R", F::Condensateur, 1e-5}, {"100n 50V 10%", F::Condensateur, 1e-7},
      {"10u 16V", F::Condensateur, 1e-5}, {"1F", F::Condensateur, 1}, {"10 F", F::Condensateur, 10}, {"22pF C0G", F::Condensateur, 22e-12},
      {"1n NP0", F::Condensateur, 1e-9}, {"100 nF X5R 6V3", F::Condensateur, 1e-7}, {"470µF", F::Condensateur, 470e-6}, {"1,5nF", F::Condensateur, 1.5e-9},
      // Condensateurs refusés
      {"100", F::Condensateur, N}, {"104", F::Condensateur, N}, {"47", F::Condensateur, N}, {"10k", F::Condensateur, N},
      {"10Ω", F::Condensateur, N}, {"0,01pF", F::Condensateur, N}, {"20F", F::Condensateur, N},
      // Inductances
      {"10u", F::Inductance, 1e-5}, {"10µH", F::Inductance, 1e-5}, {"4R7", F::Inductance, 4.7e-6}, {"100n", F::Inductance, 1e-7},
      {"1mH", F::Inductance, 1e-3}, {"2u2", F::Inductance, 2.2e-6}, {"100", F::Inductance, N}, {"10 Ω", F::Inductance, N},
      // Perles, fusibles, quartz, Zener, piles
      {"600R", F::Ferrite, 600}, {"120 Ω", F::Ferrite, 120}, {"600", F::Ferrite, 600}, {"500mA", F::Fusible, 0.5},
      {"2A", F::Fusible, 2}, {"1,5 A", F::Fusible, 1.5}, {"2", F::Fusible, 2}, {"200A", F::Fusible, N},
      {"16MHz", F::Quartz, 16e6}, {"32,768kHz", F::Quartz, 32768}, {"8 MHz", F::Quartz, 8e6}, {"16M", F::Quartz, 16e6},
      {"2GHz", F::Quartz, N}, {"5V1", F::Zener, 5.1}, {"3V3", F::Zener, 3.3}, {"12V", F::Zener, 12},
      {"12", F::Zener, 12}, {"1V", F::Zener, N}, {"3V", F::Pile, 3}, {"9 V", F::Pile, 9}, {"100V", F::Pile, N},
  };
  int vus = 0;
  for (const CasValeur& x : cas) {
    INFO(x.texte << " (" << nomFamille(x.famille) << ")");
    const ValeurComprise v = lireValeurComposant(x.texte, x.famille);
    if (std::isnan(x.attendu)) {
      CHECK(v.etat == EtatValeur::Refusee);
      CHECK_FALSE(v.raison.empty());
    } else {
      REQUIRE(v.etat == EtatValeur::Comprise);
      CHECK(v.nombre == Approx(x.attendu).epsilon(1e-9).margin(1e-15));
    }
    ++vus;
  }
  CHECK(vus >= 100);
}

TEST_CASE("3.2 : compléments, notes et propositions", "[L3][P3]") {
  const ValeurComprise c = lireValeurComposant("10µF 25V X7R", F::Condensateur);
  CHECK(c.tension == Approx(25));
  CHECK(c.dielectrique == "X7R");
  const ValeurComprise t = lireValeurComposant("100n 50V 10%", F::Condensateur);
  CHECK(t.tolerance == Approx(10));
  CHECK(t.serieReference == 12);
  CHECK(lireValeurComposant("100 nF X5R 6V3", F::Condensateur).tension == Approx(6.3));
  CHECK(lireValeurComposant("4k7 J", F::Resistance).tolerance == Approx(5));
  CHECK(lireValeurComposant("4k7 K", F::Resistance).tolerance == Approx(10));
  CHECK(lireValeurComposant("4k7 F", F::Resistance).tolerance == Approx(1));
  CHECK(lireValeurComposant("10k 1%", F::Resistance).serieReference == 96);
  CHECK(lireValeurComposant("10k ±5 %", F::Resistance).tolerance == Approx(5));
  CHECK(lireValeurComposant("10k 0,25W", F::Resistance).puissance == Approx(0.25));
  CHECK(lireValeurComposant("10k 1/4W", F::Resistance).puissance == Approx(0.25));
  CHECK(lireValeurComposant("1k 250mW", F::Resistance).puissance == Approx(0.25));
  CHECK(lireValeurComposant("10 K", F::Resistance).tolerance == std::nullopt);  // après un nombre nu, K multiplie
  CHECK(lireValeurComposant("15M0", F::Resistance).zeroFinal);
  CHECK(lireValeurComposant("4R7", F::Inductance).note.find("4,7" + FINE + "µH") != std::string::npos);
  // Nombre nu sur un condensateur : propositions, dont le code EIA
  const ValeurComprise cent = lireValeurComposant("100", F::Condensateur);
  REQUIRE_FALSE(cent.propositions.empty());
  CHECK(cent.propositions.front() == "10" + FINE + "pF (code 100)");
  CHECK(std::find(cent.propositions.begin(), cent.propositions.end(), "100" + FINE + "nF") != cent.propositions.end());
  const ValeurComprise code = lireValeurComposant("104", F::Condensateur);
  REQUIRE_FALSE(code.propositions.empty());
  CHECK(code.propositions.front() == "100" + FINE + "nF (code 104)");
  // Raisons
  CHECK(lireValeurComposant("10 µF", F::Resistance).raison.find("ohms") != std::string::npos);
  CHECK(lireValeurComposant("2T", F::Resistance).raison.find("1" + FINE + "TΩ") != std::string::npos);
  // Familles sans unité, valeur vide
  CHECK(lireValeurComposant("NE555", F::Aucune).etat == EtatValeur::TexteLibre);
  CHECK(lireValeurComposant("ATmega328P-PU", F::Aucune).etat == EtatValeur::TexteLibre);
  CHECK(lireValeurComposant("", F::Resistance).etat == EtatValeur::Vide);
  CHECK(refusee("4k7 1W 2W", F::Resistance) == false);  // compléments en double : le dernier, sans refus
}

TEST_CASE("3.2 : écrire puis relire, en compact et en SI, toute la série E24 sur six décades", "[L3][P3]") {
  int cas = 0;
  for (const auto& [famille, decade0] : std::vector<std::pair<F, int>>{{F::Resistance, 0}, {F::Condensateur, -12}, {F::Inductance, -9}})
    for (int d = 0; d < 6; ++d)
      for (double m : serieE(24)) {
        const double x = m * std::pow(10.0, decade0 + d);
        for (const std::string& t : {ecrireCompact(x, famille), ecrireSI(x, uniteDeFamille(famille))}) {
          INFO(t << " pour " << x);
          const ValeurComprise v = lireValeurComposant(t, famille);
          REQUIRE(v.etat == EtatValeur::Comprise);
          CHECK(v.nombre == Approx(x).epsilon(1e-9));
          ++cas;
        }
      }
  CHECK(cas == 3 * 6 * 24 * 2);
}

TEST_CASE("3.2 : affichage compact CEI 60062 et système international", "[L3][P3]") {
  CHECK(ecrireCompact(4700, F::Resistance) == "4k7");
  CHECK(ecrireCompact(0.47, F::Resistance) == "R47");
  CHECK(ecrireCompact(470, F::Resistance) == "470R");
  CHECK(ecrireCompact(0, F::Resistance) == "0R");
  CHECK(ecrireCompact(1e6, F::Resistance) == "1M");
  CHECK(ecrireCompact(1.5e6, F::Resistance) == "1M5");
  CHECK(ecrireCompact(10e3, F::Resistance) == "10k");
  CHECK(ecrireCompact(1e-7, F::Condensateur) == "100n");
  CHECK(ecrireCompact(2.2e-6, F::Condensateur) == "2u2");
  CHECK(ecrireCompact(4.7e-12, F::Condensateur) == "4p7");
  CHECK(ecrireCompact(4.7e-6, F::Inductance) == "4u7");
  CHECK(ecrireSI(4700, "Ω") == "4,7" + FINE + "kΩ");
  CHECK(ecrireSI(1e-7, "F") == "100" + FINE + "nF");
  CHECK(ecrireSI(1591.549, "Hz") == "1,59" + FINE + "kHz");
  CHECK(ecrireSI(9400, "Ω") == "9,4" + FINE + "kΩ");
  CHECK(ecrireSI(0, "Ω") == "0" + FINE + "Ω");
  const ValeurComprise v = lireValeurComposant("10µF 25V X7R", F::Condensateur);
  CHECK(ecrireValeurComposant(v, AffichageValeur::SystemeInternational, "10µF 25V X7R") == "10" + FINE + "µF 25" + FINE + "V X7R");
  CHECK(ecrireValeurComposant(v, AffichageValeur::Compact, "10µF 25V X7R") == "10u 25V X7R");
  CHECK(ecrireValeurComposant(v, AffichageValeur::CommeTapee, "10µF 25V X7R") == "10µF 25V X7R");
  CHECK(resumeValeur(lireValeurComposant("4k7", F::Resistance)) == "= 4,7" + FINE + "kΩ · E24");
  CHECK(resumeValeur(lireValeurComposant("4k8", F::Resistance)).find("hors E24") != std::string::npos);
}

TEST_CASE("3.2 : séries E de la CEI 60063", "[L3][P3]") {
  for (int n : {3, 6, 12, 24, 48, 96, 192}) CHECK(serieE(n).size() == static_cast<std::size_t>(n));
  // Les valeurs de E24 qui s'écartent du calcul
  const auto& e24 = serieE(24);
  for (double x : {2.7, 3.0, 3.3, 3.6, 3.9, 4.3, 4.7, 8.2}) CHECK(std::find_if(e24.begin(), e24.end(), [x](double y) { return std::abs(x - y) < 1e-9; }) != e24.end());
  const auto& e192 = serieE(192);
  CHECK(std::find_if(e192.begin(), e192.end(), [](double y) { return std::abs(y - 9.20) < 1e-9; }) != e192.end());
  CHECK(std::find_if(e192.begin(), e192.end(), [](double y) { return std::abs(y - 9.19) < 1e-9; }) == e192.end());
  for (std::size_t i = 0; i < 48; ++i) CHECK(serieE(48)[i] == Approx(serieE(96)[2 * i]));
  CHECK(dansSerie(4700, 24));
  CHECK_FALSE(dansSerie(4800, 24));
  CHECK(dansSerie(4750, 96));
  CHECK(plusProcheDansSerie(4800, 24) == Approx(4700));
  CHECK(plusProcheDansSerie(9600, 24) == Approx(10000));  // au-delà de la décade
  CHECK(voisineDansSerie(4700, 24, +1) == Approx(5100));
  CHECK(voisineDansSerie(9100, 24, +1) == Approx(10000));
  CHECK(voisineDansSerie(1000, 24, -1) == Approx(910));
  CHECK(serieDeTolerance(1) == 96);
  CHECK(serieDeTolerance(5) == 24);
  CHECK(serieDeTolerance(0.1) == 192);
  CHECK(serieDeFamille(F::Resistance) == 24);
  CHECK(serieDeFamille(F::Condensateur) == 12);
  CHECK(toleranceDeLettre('J') == Approx(5));
  CHECK(toleranceDeLettre('B') == Approx(0.1));
  CHECK_FALSE(toleranceDeLettre('X').has_value());
}

TEST_CASE("3.2 : la nomenclature regroupe « 4k7 » et « 4,7 kΩ », « 10k » et « 10K »", "[L3][P3]") {
  ConceptionElec c = nouvelleConception("Nomenclature");
  const char* valeurs[] = {"4k7", "4,7 kΩ", "4.7k", "10k", "10K", "NE555"};
  const char* symboles[] = {"R", "R", "R", "R", "R", "NE555"};
  for (int i = 0; i < 6; ++i) {
    const std::string s = poserSymbole(c, 0, symboles[i], ptMm(20.32 + 15.24 * i, 25.4));
    for (auto& x : c.feuilles[0].symboles)
      if (x.uuid == s)
        if (ComposantElec* k = c.composant(x.composant)) {
          k->reference = std::string(symboles[i][0] == 'R' ? "R" : "U") + std::to_string(i + 1);
          k->valeur = valeurs[i];
          k->empreinte = symboles[i][0] == 'R' ? "R_0805" : "SOIC-8";
        }
  }
  const auto bom = nomenclature(c);
  REQUIRE(bom.size() == 3);
  std::multiset<int> quantites;
  for (const auto& l : bom) quantites.insert(l.quantite);
  CHECK(quantites == std::multiset<int>{1, 2, 3});
  // Familles déduites du symbole et de son préfixe
  CHECK(familleDepuisPrefixe("R") == F::Resistance);
  CHECK(familleDepuisPrefixe("RV") == F::Resistance);
  CHECK(familleDepuisPrefixe("C") == F::Condensateur);
  CHECK(familleDepuisPrefixe("FB") == F::Ferrite);
  CHECK(familleDepuisPrefixe("D", "D_Zener") == F::Zener);
  CHECK(familleDepuisPrefixe("D", "LED") == F::Aucune);
  CHECK(familleDepuisPrefixe("U") == F::Aucune);
}

TEST_CASE("3.3 : « Tolérance = 1 % » repris en paramètre typé, texte gardé", "[L3][P10][P18]") {
  ConceptionElec c = nouvelleConception("Paramètres");
  const std::string s = poserSymbole(c, 0, "R", ptMm(25.4, 25.4));
  ComposantElec* k = nullptr;
  for (auto& x : c.feuilles[0].symboles)
    if (x.uuid == s) k = c.composant(x.composant);
  REQUIRE(k);
  k->champs = {{"Tolérance", "1 %"}, {"Puissance", "0,25 W"}, {"Note", "à monter à la main"}};
  CHECK(migrerChampsEnParametres(c) == 2);
  k = c.composant(k->uuid);
  CHECK(k->parametres.at("tolerance") == "1 %");
  CHECK(k->parametres.at("puissance") == "0,25 W");
  CHECK(k->champs == std::map<std::string, std::string>{{"Note", "à monter à la main"}});
  const DefParametre* tol = parametre(F::Resistance, "tolerance");
  REQUIRE(tol);
  CHECK(lireParametre(*tol, "1 %") == Approx(1));
  CHECK(lireParametre(*parametre(F::Resistance, "puissance"), "1/4 W") == Approx(0.25));
  CHECK(lireParametre(*parametre(F::Condensateur, "tension"), "6V3") == Approx(6.3));
  std::string erreur;
  CHECK_FALSE(lireParametre(*tol, "beaucoup", &erreur).has_value());
  CHECK_FALSE(erreur.empty());
  CHECK(parametre(F::Condensateur, "dielectrique")->genre == DefParametre::Genre::Choix);
  CHECK(parametresDeFamille(F::Aucune).size() >= 1);  // tension d'alimentation d'un circuit intégré
  CHECK(migrerChampsEnParametres(c) == 0);             // une seconde fois : rien
}

TEST_CASE("3.14 : formules en notation ingénieur, unités suivies, complexes et matrices", "[L3][SAI-44][ERG-12]") {
  auto valeur = [](const std::string& t) {
    const ResultatFormule r = evaluerFormule(t);
    INFO(t << " : " << r.erreur);
    REQUIRE(r.ok);
    return r.q;
  };
  CHECK(valeur("2*4k7").reel() == Approx(9400));
  CHECK(valeur("4,7k + 300").reel() == Approx(5000));
  CHECK(valeur("100nF").reel() == Approx(1e-7));
  CHECK(symboleDimension(valeur("100nF").dim) == "F");
  CHECK(symboleDimension(valeur("10kΩ * 10nF").dim) == "s");
  const Quantite fc = valeur("1/(2π·10kΩ·10nF)");
  CHECK(fc.reel() == Approx(1591.549).epsilon(1e-5));
  CHECK(symboleDimension(fc.dim) == "Hz");
  CHECK(ecrireQuantite(fc) == "1,59" + FINE + "kHz");
  CHECK(valeur("abs(3+4j)").reel() == Approx(5));
  CHECK(valeur("re((1+2j)*(3-1j))").reel() == Approx(5));
  CHECK(ecrireQuantite(valeur("(1+2j)*(3-1j)")) == "5 + 5j");
  CHECK(valeur("det([1, 2; 3, 4])").reel() == Approx(-2));
  const Quantite m = valeur("inv([2, 0; 0, 4]) * [2; 4]");
  REQUIRE(m.lignes == 2);
  CHECK(m.v[0].real() == Approx(1));
  CHECK(m.v[1].real() == Approx(1));
  CHECK(ecrireQuantite(valeur("[1, 2; 3, 4]")) == "[1, 2; 3, 4]");
  const Quantite r = valeur("rled(5V, 2V, 20mA)");
  CHECK(r.reel() == Approx(150));
  CHECK(ecrireQuantite(r) == "150" + FINE + "Ω");
  CHECK(valeur("eserie(4,8k, 24)").reel() == Approx(4700));
  CHECK(valeur("par(10k, 10k)").reel() == Approx(5000));
  CHECK(valeur("10k ∥ 10k").reel() == Approx(5000));
  CHECK(valeur("sqrt(16)").reel() == Approx(4));
  CHECK(valeur("2^10").reel() == Approx(1024));
  CHECK(valeur("pi").reel() == Approx(3.14159265));
  // Erreurs
  for (const char* faux : {"1V + 2A", "R9", "2*", "(1+2", "sqrt(1V)", "[1, 2] + [1; 2]", "1/0", "exp(1Ω)"}) {
    INFO(faux);
    CHECK_FALSE(evaluerFormule(faux).ok);
    CHECK_FALSE(evaluerFormule(faux).erreur.empty());
  }
  // Noms lus
  CHECK(nomsDeFormule("1/(2π·R3·C5) + Vcc") == std::vector<std::string>{"R3", "C5", "Vcc"});
}

TEST_CASE("3.14 : R2 = 2·R1 suit R1, passe dans la nomenclature ; un cycle est refusé ; un texte montre un calcul", "[L3][SAI-44]") {
  ConceptionElec c = nouvelleConception("Formules");
  auto poser = [&](const char* symbole, const char* ref, const char* valeur, double x) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, 25.4));
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s)
        if (ComposantElec* k = c.composant(p.composant)) {
          k->reference = ref;
          k->valeur = valeur;
          k->empreinte = std::string(symbole) == "R" ? "R_0805" : "C_0805";
          return k->uuid;
        }
    return std::string();
  };
  poser("R", "R1", "4k7", 20);
  const std::string r2 = poser("R", "R2", "", 40);
  poser("R", "R3", "10k", 60);
  poser("C", "C5", "10n", 80);
  c.composant(r2)->formule = "2·R1";
  CHECK(recalculerFormules(c).empty());
  CHECK(c.composant(r2)->valeur == "9,4" + FINE + "kΩ");
  for (auto& k : c.composants)
    if (k.reference == "R1") k.valeur = "10k";
  CHECK(recalculerFormules(c).empty());
  CHECK(c.composant(r2)->valeur == "20" + FINE + "kΩ");
  bool vu = false;
  for (const auto& l : nomenclature(c)) vu |= l.valeur == "20" + FINE + "kΩ";
  CHECK(vu);
  // Texte qui montre un calcul
  CHECK(texteCalcule(c, "fc = {=1/(2π·R3·C5)}") == "fc = 1,59" + FINE + "kHz");
  CHECK(texteCalcule(c, "rien à calculer") == "rien à calculer");
  CHECK(texteCalcule(c, "{=R99}") == "{?R99}");
  // Variables de la conception
  c.variables["Vcc"] = "5V";
  CHECK(texteCalcule(c, "I = {=Vcc/R3}") == "I = 500" + FINE + "µA");
  // Cycle : les deux formules sont refusées, leurs valeurs gardées
  const std::string ra = poser("R", "R10", "1k", 100), rb = poser("R", "R11", "2k", 120);
  c.composant(ra)->formule = "R11*2";
  c.composant(rb)->formule = "R10/2";
  const auto erreurs = recalculerFormules(c);
  CHECK(erreurs.size() == 2);
  CHECK(c.composant(ra)->valeur == "1k");
  CHECK(c.composant(rb)->valeur == "2k");
  // Une formule qui ne donne pas des ohms sur une résistance
  c.composant(ra)->formule = "C5";
  c.composant(rb)->formule.clear();
  const auto e2 = recalculerFormules(c);
  REQUIRE(e2.size() == 1);
  CHECK(e2.front().message.find("ohms") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------------------------
// 3.4 à 3.7 : hauteur unique, corps posé au bon endroit, trous de fixation, synchronisation du circuit
// ---------------------------------------------------------------------------------------------------------------
namespace {
ComposantElec* parRepere(ConceptionElec& c, const std::string& ref) {
  for (auto& x : c.composants)
    if (x.reference == ref) return &x;
  return nullptr;
}
EmpreintePlacee* empreinteDe(ConceptionElec& c, const std::string& ref) {
  const ComposantElec* x = parRepere(c, ref);
  for (auto& e : c.circuit.empreintes)
    if (x && e.composant == x->uuid) return &e;
  return nullptr;
}
int cuivreDe(const ConceptionElec& c, const EmpreintePlacee& e) {
  int n = 0;
  if (const DefEmpreinte* d = c.empreinte(e.empreinte))
    for (const auto& p : d->pastilles) n += cuivreSurPastille(c, e, p).nombre();
  return n;
}
}  // namespace

TEST_CASE("3.4 : une seule hauteur, avec sa provenance, lue par la 3D et le boîtier", "[L3][MEC-02][P4][P5][T2]") {
  ConceptionElec c = exempleElectronique();
  ComposantElec* c3 = parRepere(c, "C3");
  REQUIRE(c3);
  const HauteurComposant g = hauteurComposant(c, *c3);
  CHECK(g.provenance == ProvenanceHauteur::Generique);
  CHECK(g.mm > 0);
  CHECK(g.mm < 2);
  CHECK(g.texte().find("générique") != std::string::npos);
  CHECK_FALSE(g.confirmee());
  // 12 mm saisis : provenance « saisie », non confirmée
  c3->hauteur = 12;
  c3->provenanceHauteur = ProvenanceHauteur::Saisie;
  const HauteurComposant s = hauteurComposant(c, *c3);
  CHECK(s.mm == Approx(12));
  CHECK(s.provenance == ProvenanceHauteur::Saisie);
  CHECK(s.texte() == "12,00" + FINE + "mm · saisie");
  CHECK_FALSE(s.confirmee());
  // Vraisemblance : plus de trois fois la générique, ou plus de 25 mm en CMS
  CHECK_FALSE(hauteurAConfirmer(12, c.empreinte(c3->empreinte)).empty());
  CHECK_FALSE(hauteurAConfirmer(30, c.empreinte("L_0805")).empty());
  CHECK(hauteurAConfirmer(g.mm * 1.5, c.empreinte(c3->empreinte)).empty());
  // Le corps 3D et le boîtier prennent la même hauteur
  const EmpreintePlacee* e = empreinteDe(c, "C3");
  REQUIRE(e);
  const auto corps = corpsPose(c, *e);
  REQUIRE(corps);
  CHECK(corps->hauteur.mm == Approx(12));
  prom::Projet p;
  p.electronique.push_back(c);
  prom::appliquerCarteElectronique(p, 0);
  bool vu = false;
  for (const auto& m : p.composants)
    if (m.ref == "C3") {
      vu = true;
      CHECK(m.h == Approx(12));
      CHECK(m.sourceHauteur == "saisie");
    }
  CHECK(vu);
  // Vérifiée sur la fiche : le contrôle du boîtier devient complet
  c3->provenanceHauteur = ProvenanceHauteur::Fiche;
  CHECK(hauteurComposant(c, *c3).confirmee());
  p.electronique[0] = c;
  prom::appliquerCarteElectronique(p, 0);
  for (const auto& m : p.composants)
    if (m.ref == "C3") CHECK(m.sourceHauteur == "verifiee");
  // Une empreinte sans corps a une pastille plate de 0,1 mm, et non plus rien (T2)
  DefEmpreinte plate = *c.empreinte("R_0603");
  plate.corps = {};
  CHECK(hauteurComposant(nullptr, &plate).mm == Approx(0.1));
}

TEST_CASE("3.4 : le corps posé au centre de l'encombrement, connecteur USB de StickHub", "[L3][T5][MEC-05]") {
  const ConceptionElec c = conceptionDepuisKicad(prom::lireFichier(prom::cheminFichier(std::string(PROMETHEE_DONNEES) + "/kicad/StickHub.kicad_pcb")), "StickHub");
  const EmpreintePlacee* j1 = nullptr;
  for (const auto& e : c.circuit.empreintes)
    if (const ComposantElec* x = c.composant(e.composant); x && x->reference == "J1") j1 = &e;
  REQUIRE(j1);
  const DefEmpreinte* d = c.empreinte(j1->empreinte);
  REQUIRE(d);
  // L'origine de l'empreinte est au bord du connecteur ; son encombrement va de 0 à 11,5 mm
  CHECK(mm(centreCorps(*d).x) == Approx(5.75).margin(0.01));
  const auto corps = corpsPose(c, *j1);
  REQUIRE(corps);
  const Pt attendu = versCarte(*j1, d->encombrement.centre());
  CHECK(std::abs(corps->centre.x - attendu.x) <= 1000);
  CHECK(std::abs(corps->centre.y - attendu.y) <= 1000);
  // Le boîtier pose le corps au même endroit
  prom::Projet p;
  p.electronique.push_back(c);
  prom::appliquerCarteElectronique(p, 0);
  for (const auto& m : p.composants)
    if (m.ref == "J1" && !m.bord) {
      CHECK(m.x == Approx(mm(attendu.x)).margin(0.01));
      CHECK(m.y == Approx(-mm(attendu.y)).margin(0.01));
    }
}

TEST_CASE("3.5 : un trou « kicad:MountingHole… » et un trou « perso:… » donnent des piliers", "[L3][T6][MEC-12][EMP-18]") {
  ConceptionElec c = nouvelleConception("Trous");
  // Empreinte personnelle : une seule pastille percée de 4,3 mm, sans numéro
  DefEmpreinte perso;
  perso.id = "perso:Fixation_M4";
  Pastille t;
  t.type = TypePastille::NonMetallisee;
  t.forme = FormePastille::Rond;
  t.largeur = t.hauteur = t.percage = nm(4.3);
  perso.pastilles = {t};
  perso.encombrement = {-nm(4), -nm(4), nm(4), nm(4)};
  c.empreintes[perso.id] = perso;
  // Empreinte KiCad : trou métallisé de 3,2 mm, pastille 1 reliée à rien
  DefEmpreinte kicad = perso;
  kicad.id = "kicad:MountingHole:MountingHole_3.2mm_M3_Pad";
  kicad.pastilles[0].type = TypePastille::Traversante;
  kicad.pastilles[0].numero = "1";
  kicad.pastilles[0].percage = nm(3.2);
  kicad.pastilles[0].largeur = kicad.pastilles[0].hauteur = nm(6);
  c.empreintes[kicad.id] = kicad;
  // Une résistance traversante n'est pas un trou de fixation
  const char* empreintes[] = {"perso:Fixation_M4", "kicad:MountingHole:MountingHole_3.2mm_M3_Pad", "R_Axial_P10.16"};
  for (int i = 0; i < 3; ++i) {
    const std::string s = poserSymbole(c, 0, i < 2 ? "Trou_fixation" : "R", ptMm(20 + 20 * i, 30));
    for (auto& x : c.feuilles[0].symboles)
      if (x.uuid == s)
        if (ComposantElec* k = c.composant(x.composant)) {
          k->reference = i < 2 ? "H" + std::to_string(i + 1) : "R1";
          k->empreinte = empreintes[i];
          k->valeur = i < 2 ? "M3" : "1k";
        }
  }
  mettreAJourCircuit(c);
  for (std::size_t i = 0; i < c.circuit.empreintes.size(); ++i) c.circuit.empreintes[i].position = ptMm(10 + 15.0 * static_cast<double>(i), 15);
  CHECK(trouDeFixation(*c.empreinte("perso:Fixation_M4")).has_value());
  CHECK(trouDeFixation(*c.empreinte("kicad:MountingHole:MountingHole_3.2mm_M3_Pad")).has_value());
  CHECK_FALSE(trouDeFixation(*c.empreinte("R_Axial_P10.16")).has_value());
  CHECK(visDuPercage(2.2) == "M2");
  CHECK(visDuPercage(2.7) == "M2.5");
  CHECK(visDuPercage(3.2) == "M3");
  CHECK(visDuPercage(4.3) == "M4");
  CHECK(visDuPercage(5.3) == "M5");
  CHECK(visDuPercage(6.4) == "M6");
  prom::Projet p;
  p.electronique.push_back(c);
  prom::appliquerCarteElectronique(p, 0);
  REQUIRE(p.trous.size() == 2);
  std::set<std::string> vis;
  for (const auto& tr : p.trous) vis.insert(tr.vis.value_or("?"));
  CHECK(vis == std::set<std::string>{"M3", "M4"});
  CHECK(prom::visConnue("M6"));
}

TEST_CASE("3.6 : R3 supprimé du schéma alors qu'il est routé : écart en attente, aucune piste orpheline en silence", "[L3][A4][S18][C22]") {
  ConceptionElec c = exempleElectronique();
  CHECK(ecartsSynchronisation(c).empty());  // l'exemple est à jour
  EmpreintePlacee* r3 = empreinteDe(c, "R3");
  REQUIRE(r3);
  REQUIRE(cuivreDe(c, *r3) > 0);
  const std::size_t pistes = c.circuit.pistes.size();
  const std::string uuid = parRepere(c, "R3")->uuid;
  // Le symbole part du schéma
  std::erase_if(c.feuilles[0].symboles, [&](const SymbolePlace& s) { return s.composant == uuid; });
  const auto attente = synchroniser(c);
  REQUIRE(attente.size() == 1);
  CHECK(attente.front().genre == GenreEcart::SupprimeRoute);
  CHECK_FALSE(attente.front().sansRisque);
  CHECK(attente.front().description.find("R3 supprimé du schéma") != std::string::npos);
  CHECK(attente.front().description.find("piste") != std::string::npos);
  CHECK(empreinteDe(c, "R3"));                // l'empreinte reste tant qu'on n'a rien décidé
  CHECK(c.circuit.pistes.size() == pistes);   // et les pistes aussi
  // Appliquer : l'empreinte part, les pistes restent en bouts libres
  CHECK(appliquerEcarts(c, attente) == 1);
  CHECK_FALSE(empreinteDe(c, "R3"));
  CHECK(c.circuit.pistes.size() == pistes);
  CHECK(ecartsSynchronisation(c).empty());
  // Un composant non routé supprimé part sans demander
  ConceptionElec d = exempleElectronique();
  const std::string h1 = parRepere(d, "H1")->uuid;
  std::erase_if(d.feuilles[0].symboles, [&](const SymbolePlace& s) { return s.composant == h1; });
  CHECK(synchroniser(d).empty());
  CHECK_FALSE(empreinteDe(d, "H1"));
}

TEST_CASE("3.6 : composant nouveau à placer, hors carte, mode sur confirmation, conception publiée", "[L3][PLA-16][A4]") {
  ConceptionElec c = exempleElectronique();
  const std::size_t avant = c.circuit.empreintes.size();
  const std::string s = poserSymbole(c, 0, "R", ptMm(200, 60));
  std::string id;
  for (auto& x : c.feuilles[0].symboles)
    if (x.uuid == s) id = x.composant;
  c.composant(id)->reference = "R9";
  c.composant(id)->valeur = "10k";
  // Sur confirmation : rien ne change, l'écart attend
  c.synchroSurConfirmation = true;
  auto e = synchroniser(c);
  REQUIRE(e.size() == 1);
  CHECK(e.front().genre == GenreEcart::Nouveau);
  CHECK(e.front().sansRisque);
  CHECK(c.circuit.empreintes.size() == avant);
  // Automatique : l'empreinte est créée « à placer », hors de la carte
  c.synchroSurConfirmation = false;
  CHECK(synchroniser(c).empty());
  REQUIRE(c.circuit.empreintes.size() == avant + 1);
  CHECK(aPlacer(c, c.circuit.empreintes.back()));
  // Une seconde, rangée à côté de la première sans la chevaucher
  const std::string s2 = poserSymbole(c, 0, "C", ptMm(210, 60));
  for (auto& x : c.feuilles[0].symboles)
    if (x.uuid == s2) c.composant(x.composant)->reference = "C9";
  CHECK(synchroniser(c).empty());
  REQUIRE(c.circuit.empreintes.size() == avant + 2);
  const auto& a = c.circuit.empreintes[avant];
  const auto& b = c.circuit.empreintes[avant + 1];
  CHECK_FALSE(boite(encombrementSurCarte(a, *c.empreinte(a.empreinte))).touche(boite(encombrementSurCarte(b, *c.empreinte(b.empreinte)))));
  // Hors carte : retirée (aucun cuivre), puis remise à placer
  c.composant(id)->horsCarte = true;
  CHECK(synchroniser(c).empty());
  CHECK(c.circuit.empreintes.size() == avant + 1);
  c.composant(id)->horsCarte = false;
  CHECK(synchroniser(c).empty());
  CHECK(c.circuit.empreintes.size() == avant + 2);
  // Conception publiée : rien
  c.composant(id)->horsCarte = true;
  c.verrouillee = true;
  CHECK(ecartsSynchronisation(c).empty());
}

TEST_CASE("3.7 : R3 passe en 0805 : même place, même rotation, pistes accrochées", "[L3][P6][PLA-16]") {
  ConceptionElec c = exempleElectronique();
  EmpreintePlacee* r3 = empreinteDe(c, "R3");
  REQUIRE(r3);
  const EmpreintePlacee avant = *r3;
  const Pt point = versCarte(avant, pointDePlacement(*c.empreinte(avant.empreinte)));
  std::map<std::string, int> cuivreParBroche;
  for (const auto& p : c.empreinte(avant.empreinte)->pastilles) cuivreParBroche[p.numero] += cuivreSurPastille(c, avant, p).nombre();
  REQUIRE(cuivreParBroche["1"] > 0);
  REQUIRE(cuivreParBroche["2"] > 0);
  parRepere(c, "R3")->empreinte = "R_0805";
  const auto e = ecartsSynchronisation(c);
  REQUIRE(e.size() == 1);
  CHECK(e.front().genre == GenreEcart::EmpreinteRemplacee);
  CHECK(e.front().sansRisque);
  CHECK(e.front().description == "Empreinte de R3 : R_0603 → R_0805");
  CHECK(synchroniser(c).empty());
  r3 = empreinteDe(c, "R3");
  REQUIRE(r3);
  CHECK(r3->empreinte == "R_0805");
  CHECK(r3->rotation == avant.rotation);
  CHECK(r3->dessous == avant.dessous);
  const Pt apres = versCarte(*r3, pointDePlacement(*c.empreinte("R_0805")));
  CHECK(std::abs(apres.x - point.x) <= 1);
  CHECK(std::abs(apres.y - point.y) <= 1);
  // Chaque broche garde son cuivre, accroché à la nouvelle pastille
  for (const auto& p : c.empreinte("R_0805")->pastilles) CHECK(cuivreSurPastille(c, *r3, p).nombre() == cuivreParBroche[p.numero]);
  // L'empreinte verrouillée attend une confirmation
  ConceptionElec v = exempleElectronique();
  empreinteDe(v, "R3")->verrou = true;
  parRepere(v, "R3")->empreinte = "R_0805";
  const auto ev = synchroniser(v);
  REQUIRE(ev.size() == 1);
  CHECK(ev.front().genre == GenreEcart::Verrouillee);
  CHECK(empreinteDe(v, "R3")->empreinte == "R_0603");
  // Une pastille routée sans pendant : en attente
  ConceptionElec w = exempleElectronique();
  parRepere(w, "R3")->empreinte = "TestPoint_Pad_1.5";
  const auto ew = synchroniser(w);
  REQUIRE(ew.size() == 1);
  CHECK(ew.front().genre == GenreEcart::PastillePerdue);
  CHECK(ew.front().description.find("la pastille 2 n'existe pas") != std::string::npos);
}

TEST_CASE("3.6 : broche passée à un autre réseau sous du cuivre, réseau renommé", "[L3][PLA-16]") {
  ConceptionElec c = exempleElectronique();
  // Le réseau de la sortie (U1.3, R3.1) est nommé au schéma : le cuivre suit, sans risque
  const Connectivite k = connectivite(c);
  const std::string ancien = k.nom(parRepere(c, "R3")->uuid, "1");
  REQUIRE_FALSE(ancien.empty());
  CHECK(renommerReseauDepuisCircuit(c, ancien, "SORTIE") > 0);
  const Connectivite k2 = connectivite(c);
  CHECK(k2.nom(parRepere(c, "R3")->uuid, "1") == "SORTIE");
  // Une étiquette est posée au schéma, sur une broche du réseau qui n'avait pas de nom
  bool etiquette = false;
  for (const auto& e : c.feuilles[0].etiquettes) etiquette |= e.texte == "SORTIE";
  CHECK(etiquette);
  bool cuivre = false;
  for (const auto& t : c.circuit.pistes) cuivre |= t.reseau == "SORTIE";
  CHECK(cuivre);
  CHECK(ecartsSynchronisation(c).empty());
  // Renommer au schéma : le cuivre garde l'ancien nom, écart sans risque appliqué tout seul
  CHECK(renommerReseau(c, "SORTIE", "LED_OUT") > 0);
  const auto e = ecartsSynchronisation(c);
  std::string dits;
  for (const auto& x : e) dits += x.description + " | ";
  INFO(dits);
  REQUIRE(e.size() == 1);
  CHECK(e.front().genre == GenreEcart::ReseauRenomme);
  CHECK(synchroniser(c).empty());
  for (const auto& t : c.circuit.pistes) CHECK(t.reseau != "SORTIE");
}

TEST_CASE("3.18 : piste tracée entre des pastilles que le schéma ne relie pas : réseau proposé au schéma (ROU-34)", "[L3][ROU-34]") {
  ConceptionElec c = nouvelleConception("Liaison");
  for (int i = 0; i < 2; ++i) {
    const std::string s = poserSymbole(c, 0, "R", ptMm(30 + 20 * i, 30));
    for (auto& x : c.feuilles[0].symboles)
      if (x.uuid == s) c.composant(x.composant)->reference = "R" + std::to_string(i + 1);
  }
  mettreAJourCircuit(c);
  c.circuit.empreintes[0].position = ptMm(10, 10);
  c.circuit.empreintes[1].position = ptMm(20, 10);
  const PastillePlacee* a = nullptr;
  const PastillePlacee* b = nullptr;
  const auto pp = pastilles(c, connectivite(c));
  for (const auto& p : pp) {
    if (p.reference == "R1" && p.numero == "2") a = &p;
    if (p.reference == "R2" && p.numero == "1") b = &p;
  }
  REQUIRE(a);
  REQUIRE(b);
  c.circuit.pistes.push_back({nouvelUuid(), "", Couche::CuivreDessus, a->pastille.position, b->pastille.position, 250'000});
  const auto e = ecartsSynchronisation(c);
  REQUIRE(e.size() == 1);
  CHECK(e.front().genre == GenreEcart::LiaisonCircuit);
  CHECK_FALSE(e.front().sansRisque);
  CHECK(e.front().pastilles.size() == 2);
  CHECK(appliquerEcarts(c, e) == 1);
  const Connectivite k = connectivite(c);
  CHECK(k.nom(parRepere(c, "R1")->uuid, "2") == k.nom(parRepere(c, "R2")->uuid, "1"));
  CHECK(c.circuit.pistes.back().reseau == k.nom(parRepere(c, "R1")->uuid, "2"));
  CHECK(ecartsSynchronisation(c).empty());
}

TEST_CASE("3.19 : carte importée sans liens : appariement par repère, puis par coût minimal", "[L3][PLA-17][PLA-18]") {
  // Algorithme hongrois : affectation de coût minimal
  const std::vector<std::vector<double>> couts = {{4, 1, 3}, {2, 0, 5}, {3, 2, 2}};
  const auto choix = affectationCoutMinimal(couts);
  REQUIRE(choix.size() == 3);
  CHECK(couts[0][static_cast<std::size_t>(choix[0])] + couts[1][static_cast<std::size_t>(choix[1])] + couts[2][static_cast<std::size_t>(choix[2])] == Approx(5));
  // Cinquante composants : quarante-huit retrouvent leur repère, deux ont été renumérotés sur la carte
  ConceptionElec c = nouvelleConception("Appariement");
  std::vector<EmpreinteSansLien> carte;
  for (int i = 0; i < 50; ++i) {
    const bool resistance = i % 2 == 0;
    const std::string s = poserSymbole(c, 0, resistance ? "R" : "C", ptMm(20 + 10 * (i % 10), 20 + 15 * (i / 10)));
    for (auto& x : c.feuilles[0].symboles)
      if (x.uuid == s) {
        ComposantElec* k = c.composant(x.composant);
        k->reference = (resistance ? "R" : "C") + std::to_string(i + 1);
        k->valeur = resistance ? (i == 10 ? "4k7" : "10k") : "100n";
        carte.push_back({k->reference, k->valeur, k->empreinte});
      }
  }
  carte[10].reference = "R99";  // renumérotée sur la carte, valeur unique
  carte[11].reference = "C98";  // renumérotée, valeur partagée par d'autres
  const ResultatAppariement r = apparier(c, carte);
  int parRepere = 0, autres = 0;
  for (const auto& p : r.paires) (p.par == Appariement::Par::Repere ? parRepere : autres) += 1;
  CHECK(parRepere == 48);
  CHECK(autres == 2);
  CHECK(r.empreintesSeules.empty());
  CHECK(r.composantsSeuls.empty());
  for (const auto& p : r.paires)
    if (p.empreinte == 10) CHECK(c.composant(p.composant)->reference == "R11");
}

// ---------------------------------------------------------------------------------------------------------------
// 3.12, 3.13, 3.15 à 3.17, 3.20 : ERC-07 et exclusions, format 4 et migration, modification objet par objet,
// remplacement d'un composant, genre des composants, piliers et fabrication
// ---------------------------------------------------------------------------------------------------------------
TEST_CASE("3.12 : pastille thermique d'un QFN sans broche au symbole : ERC-07 ; exclusions par composant et par feuille", "[L3][E4][STR-08]") {
  ConceptionElec c = nouvelleConception("QFN");
  const std::string s = poserSymbole(c, 0, "ATtiny85", ptMm(50, 50));
  std::string id;
  for (auto& x : c.feuilles[0].symboles)
    if (x.uuid == s) id = x.composant;
  c.composant(id)->reference = "U1";
  // Une empreinte à neuf pastilles (huit broches et une pastille centrale « 9 ») pour un symbole à huit broches
  DefEmpreinte qfn = *c.empreinte("SOIC-8");
  qfn.id = "perso:QFN-8-EP";
  Pastille ep = qfn.pastilles.front();
  ep.numero = "9";
  ep.position = {};
  qfn.pastilles.push_back(ep);
  Pastille mp = ep;
  mp.numero = "MP";  // pastille mécanique : pas d'alerte
  qfn.pastilles.push_back(mp);
  c.empreintes[qfn.id] = qfn;
  c.composant(id)->empreinte = qfn.id;
  const auto erc = verifierERC(c, connectivite(c), NiveauErc::Complet);
  int e07 = 0;
  std::string message;
  for (const auto& v : erc)
    if (v.code == "ERC-07") {
      ++e07;
      message = v.message;
      CHECK(v.gravite == Gravite::Avertissement);
    }
  CHECK(e07 == 1);
  CHECK(message.find("« 9 »") != std::string::npos);
  // Exclusion de la feuille entière, tous contrôles
  c.erc.exclusions.push_back({"*", {c.feuilles[0].uuid}, "feuille d'essai", "Evan", "2026-10-22", {}});
  int restant = 0;
  for (const auto& v : verifierERC(c, connectivite(c), NiveauErc::Complet)) restant += v.feuille == c.feuilles[0].uuid;
  CHECK(restant == 0);
  // Exclusion d'un composant pour tous les contrôles
  c.erc.exclusions = {{"*", {id}, "à poser plus tard", "Evan", "2026-10-22"}};
  for (const auto& v : verifierERC(c, connectivite(c), NiveauErc::Complet))
    CHECK(std::find(v.objets.begin(), v.objets.end(), id) == v.objets.end());
  // Ne pas monter toute la feuille d'un coup
  CHECK(exclureFeuille(c, 0, "nePasMonter", true) == 1);
  CHECK(c.composant(id)->nePasMonter);
}

TEST_CASE("3.17 : entretoise posée au schéma : un repère, une ligne de nomenclature, aucune ligne au fichier de placement", "[L3][SAI-45]") {
  ConceptionElec c = nouvelleConception("Mécanique");
  const std::string s = poserSymbole(c, 0, "Entretoise", ptMm(40, 40));
  const std::string r = poserSymbole(c, 0, "R", ptMm(60, 40));
  std::string id, idR;
  for (auto& x : c.feuilles[0].symboles) {
    if (x.uuid == s) id = x.composant;
    if (x.uuid == r) idR = x.composant;
  }
  REQUIRE(c.composant(id));
  CHECK(c.composant(id)->genre == GenreComposant::Mecanique);
  CHECK(annoter(c, true) > 0);
  CHECK(c.composant(id)->reference == "MP1");
  mettreAJourCircuit(c);
  CHECK(c.circuit.empreintes.size() == 2);
  // Une ligne de nomenclature pour l'entretoise
  int lignes = 0;
  for (const auto& l : nomenclature(c))
    for (const auto& ref : l.references) lignes += ref == "MP1";
  CHECK(lignes == 1);
  // Aucune ligne au fichier de placement
  const std::string pos = csvPlacement(c);
  CHECK(pos.find("MP1") == std::string::npos);
  CHECK(pos.find(c.composant(idR)->reference) != std::string::npos);
  CHECK(dansNomenclature(c, *c.composant(id)));
  CHECK_FALSE(dansPlacement(c, *c.composant(id)));
  // Une vis ne va qu'à la nomenclature : hors carte d'office, sans erreur d'empreinte
  const std::string v = poserSymbole(c, 0, "Vis", ptMm(80, 40));
  for (auto& x : c.feuilles[0].symboles)
    if (x.uuid == v) CHECK(c.composant(x.composant)->horsCarte);
  // Les autres genres
  ComposantElec* x = c.composant(idR);
  x->genre = GenreComposant::Graphique;
  CHECK_FALSE(dansNomenclature(c, *x));
  CHECK_FALSE(dansPlacement(c, *x));
  x->genre = GenreComposant::HorsNomenclature;
  CHECK_FALSE(dansNomenclature(c, *x));
  CHECK(dansPlacement(c, *x));
  x->genre = GenreComposant::Cavalier;
  CHECK(dansNomenclature(c, *x));
  CHECK(dansPlacement(c, *x));
  // Le genre survit au fichier
  const ConceptionElec relu = conceptionDepuisJson(versJson(c));
  CHECK(relu.composant(id)->genre == GenreComposant::Mecanique);
  CHECK(relu.composant(idR)->genre == GenreComposant::Cavalier);
}

TEST_CASE("3.16 : LM358 remplacé par un TL072, broches par nom : les fils restent accrochés, l'empreinte change sur le circuit", "[L3][SAI-42]") {
  ConceptionElec c = nouvelleConception("Remplacer");
  const std::string s = poserSymbole(c, 0, "LM358", ptMm(100, 80));
  std::string id;
  for (auto& x : c.feuilles[0].symboles)
    if (x.uuid == s) id = x.composant;
  c.composant(id)->reference = "U1";
  // Huit fils, un par broche, chacun vers une étiquette à son nom
  std::map<std::string, std::string> nomParNumero;
  for (const auto& sp : c.feuilles[0].symboles)
    if (sp.uuid == s)
      for (const auto& b : brochesPlacees(sp, *c.symbole("LM358"))) {
        const Pt dir = b.direction == 0 ? Pt{-1, 0} : b.direction == 2 ? Pt{1, 0} : b.direction == 1 ? Pt{0, -1} : Pt{0, 1};
        const Pt bout{b.p.x + dir.x * 5'080'000, b.p.y + dir.y * 5'080'000};
        c.feuilles[0].fils.push_back({nouvelUuid(), b.p, bout});
        c.feuilles[0].etiquettes.push_back({nouvelUuid(), "S_" + b.broche->numero, bout, 0, TypeEtiquette::Locale});
        nomParNumero[b.broche->numero] = b.broche->nom;
      }
  REQUIRE(nomParNumero.size() == 8);
  mettreAJourCircuit(c);
  REQUIRE(c.circuit.empreintes.size() == 1);
  c.circuit.empreintes[0].position = ptMm(20, 20);
  c.circuit.empreintes[0].rotation = 90;
  // Le TL072 : mêmes noms de broches, rangées autrement, en boîtier DIP-8
  SpecSymbole sp;
  sp.id = "perso:TL072";
  sp.nom = "TL072";
  sp.prefixe = "U";
  sp.empreinte = "DIP-8";
  sp.broches = lireListeBroches("8 V+ alim\n4 V- alim\n3 +INA in\n2 -INA in\n1 OUTA out\n5 +INB in\n6 -INB in\n7 OUTB out");
  c.symboles[sp.id] = genererSymbole(sp);
  const ApercuRemplacement a = apercuRemplacement(c, id, sp.id);
  CHECK(a.possible());
  CHECK(a.reliees.size() == 8);
  CHECK(a.fils == 8);
  CHECK(a.empreinteApres == "DIP-8");
  std::string erreur;
  CHECK(remplacerComposants(c, {id}, sp.id, {}, &erreur) == 1);
  INFO(erreur);
  // Chaque broche garde son réseau, désigné par son étiquette
  const Connectivite k = connectivite(c);
  for (const auto& [num, nom] : nomParNumero) {
    INFO("broche " << num << " " << nom);
    CHECK(k.nom(id, num) == "S_" + num);
  }
  CHECK(c.composant(id)->reference == "U1");
  CHECK(c.composant(id)->valeur == "TL072");
  // Le circuit suit : empreinte remplacée en place, même rotation
  CHECK(synchroniser(c).empty());
  CHECK(c.circuit.empreintes[0].empreinte == "DIP-8");
  CHECK(c.circuit.empreintes[0].rotation == 90);
  // Une broche reliée sans remplaçante bloque tout
  SpecSymbole petit = sp;
  petit.id = "perso:AOP5";
  petit.broches = lireListeBroches("1 OUTA out\n2 -INA in\n3 +INA in\n4 V- alim\n8 V+ alim");
  c.symboles[petit.id] = genererSymbole(petit);
  const ApercuRemplacement b = apercuRemplacement(c, id, petit.id);
  CHECK_FALSE(b.possible());
  CHECK(b.perdues.size() == 3);
  CHECK(remplacerComposants(c, {id}, petit.id, {}, &erreur) == 0);
  CHECK(erreur.find("sans remplaçante") != std::string::npos);
  CHECK(c.composant(id)->symbole == sp.id);
}

TEST_CASE("3.15 : modifier une valeur objet par objet, pinceau de propriétés", "[L3][SAI-15][SAI-41]") {
  CHECK(transformerTexte("10k", {RegleTexte::Genre::Suffixe, "", "_A"}) == "10k_A");
  CHECK(transformerTexte("10k", {RegleTexte::Genre::Prefixe, "", "R_"}) == "R_10k");
  CHECK(transformerTexte("100nF 50V", {RegleTexte::Genre::Remplacer, "50V", "25V"}) == "100nF 25V");
  CHECK(transformerTexte("4k7", {RegleTexte::Genre::Formule, "", "2*x"}, FamilleValeur::Resistance) == "9,4" + FINE + "kΩ");
  CHECK(transformerTexte("10n", {RegleTexte::Genre::Formule, "", "x*10"}, FamilleValeur::Condensateur) == "100" + FINE + "nF");
  std::string erreur;
  CHECK_FALSE(transformerTexte("NE555", {RegleTexte::Genre::Formule, "", "2*x"}, FamilleValeur::Aucune, &erreur).has_value());
  CHECK_FALSE(erreur.empty());
  // Pinceau : valeur et paramètres d'une résistance recopiés sur une autre
  ConceptionElec c = exempleElectronique();
  ComposantElec* r1 = parRepere(c, "R1");
  r1->parametres["tolerance"] = "1 %";
  const ComposantElec modele = *r1;
  CHECK(appliquerPinceau(c, modele, parRepere(c, "R2")->uuid, {}));
  CHECK(parRepere(c, "R2")->valeur == modele.valeur);
  CHECK(parRepere(c, "R2")->parametres.at("tolerance") == "1 %");
  CHECK_FALSE(appliquerPinceau(c, modele, parRepere(c, "R2")->uuid, {}));  // déjà pareil
}

TEST_CASE("3.20 : pilier M4 posé par le dessous, lamage de la table ISO de la mécanique ; fabrication par pastille ; volume libre", "[L3][MEC-12][EMP-18][EMP-11]") {
  // La table du boîtier et celle de la mécanique disent le même lamage
  for (const char* v : {"M2", "M2.5", "M3", "M4", "M5", "M6"}) {
    INFO(v);
    REQUIRE(prom::lamageISO4762(v));
    REQUIRE(prom::visConnue(v));
  }
  CHECK(prom::lamageISO4762("M4")->d == Approx(8.0));
  CHECK(prom::lamageISO4762("M4")->h == Approx(4.4));
  prom::Projet p = prom::exemple();
  REQUIRE_FALSE(p.trous.empty());
  p.trous[0].vis = "M4";
  p.trous[0].pose = "dessous";
  p.trous[0].nervures = true;
  const prom::Derive d = prom::deriver(p);
  bool vu = false;
  for (const auto& t : d.trous)
    if (t.id == p.trous[0].id) {
      vu = true;
      CHECK(t.dessous);
      CHECK(t.nervures);
      CHECK(t.lamageD == Approx(8.0));
      CHECK(t.lamageH == Approx(4.4));
      CHECK(t.rp == Approx(4.3 / 2));  // passage de la vis
      CHECK(t.Rb >= 4.0 + 0.8);         // le pilier entoure le lamage
    }
  CHECK(vu);
  // Le réglage passe par le fichier
  const prom::Projet relu = prom::normaliser(prom::versJson(p));
  CHECK(relu.trous[0].pose == "dessous");
  CHECK(relu.trous[0].nervures);
  CHECK(relu.trous[0].vis.value_or("") == "M4");
  // Propriété de fabrication d'une pastille, jusque dans l'attribut Gerber X2
  CHECK(std::string(fonctionGerberPastille("fiducialGlobal", true)) == "FiducialPad,Global");
  CHECK(std::string(fonctionGerberPastille("bga", true)) == "BGAPad,CuDef");
  CHECK(std::string(fonctionGerberPastille("", false)) == "ComponentPad");
  ConceptionElec c = exempleElectronique();
  figerEmpreintes(c);
  DefEmpreinte& r = c.empreintes.at("R_0603");
  r.pastilles[0].fabrication = "test";
  r.volumes.push_back({{ptMm(-1, -1), ptMm(1, -1), ptMm(1, 1), ptMm(-1, 1)}, nm(0.5), nm(2), "#AA3333"});
  c.circuit.volumes.push_back({{ptMm(5, 5), ptMm(10, 5), ptMm(10, 10)}, 0, nm(3), "#3333AA"});
  const ConceptionElec rc = conceptionDepuisJson(versJson(c));
  CHECK(rc.empreintes.at("R_0603").pastilles[0].fabrication == "test");
  REQUIRE(rc.empreintes.at("R_0603").volumes.size() == 1);
  CHECK(rc.empreintes.at("R_0603").volumes[0].z1 == nm(2));
  CHECK(rc.circuit.volumes.size() == 1);
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  bool testPad = false;
  for (const auto& cf : m.couches)
    for (const auto& pr : cf.primitives) testPad |= pr.fonction == "TestPad";
  CHECK(testPad);
}

TEST_CASE("3.13 : format 4 ; un projet de la 0.31 monte, sa hauteur cochée devient « saisie » et la liste le dit ; il redescend en disant ce qu'il perd", "[L3][A3][MEC-02]") {
  ConceptionElec c = exempleElectronique();
  parRepere(c, "C1")->hauteur = 12.5;
  auto j = versJson(c);
  CHECK(j["format"] == FORMAT_ELECTRONIQUE);
  // Un fichier de la 0.31 : format 3 et la case « hauteurVerifiee »
  j["format"] = 3;
  j["formatElectronique"] = 3;
  for (auto& k : j["composants"])
    if (k["ref"] == "C1") k["hauteurVerifiee"] = true;
  std::vector<std::string> alertes;
  const ConceptionElec m = conceptionDepuisJson(j, &alertes);
  const ComposantElec* c1 = nullptr;
  for (const auto& x : m.composants)
    if (x.reference == "C1") c1 = &x;
  REQUIRE(c1);
  CHECK(c1->hauteur == Approx(12.5));
  CHECK(c1->provenanceHauteur == ProvenanceHauteur::Saisie);
  bool dit = false;
  for (const auto& a : alertes) dit |= a.find("C1") != std::string::npos && a.find("saisie") != std::string::npos;
  CHECK(dit);
  CHECK(m.clesInconnues.empty());
  // Redescente vers la 0.31 : une formule, un paramètre, une hauteur confirmée
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  ConceptionElec n = exempleElectronique();
  parRepere(n, "R2")->formule = "2*R1";
  parRepere(n, "R1")->parametres["tolerance"] = "1 %";
  parRepere(n, "C3")->provenanceHauteur = ProvenanceHauteur::Fiche;
  p.electronique.push_back(n);
  const prom::FichierAnterieur f = prom::pourVersion(p, "0.31");
  const auto& ce = f.projet["electronique"][0];
  CHECK(ce["format"] == 3);
  bool tolerance = false, verifiee = false, formule = false;
  for (const auto& k : ce["composants"]) {
    formule |= k.contains("formule");
    if (k["ref"] == "R1") tolerance = k.contains("champs") && k["champs"].contains("Tolérance");
    if (k["ref"] == "C3") verifiee = k.value("hauteurVerifiee", false);
  }
  CHECK_FALSE(formule);
  CHECK(tolerance);
  CHECK(verifiee);
  bool perteFormule = false, perteParametre = false;
  for (const auto& x : f.pertes) {
    perteFormule |= x.find("valeur calculée") != std::string::npos;
    perteParametre |= x.find("paramètre typé") != std::string::npos;
  }
  CHECK(perteFormule);
  CHECK(perteParametre);
}

TEST_CASE("3.6 : la valeur tapée suit dans la même commande : R2 = 2·R1, un seul pas d'annulation", "[L3][SAI-44][A2]") {
  ConceptionElec c = exempleElectronique();
  parRepere(c, "R2")->formule = "2*R1";
  ChangementConception tout;
  tout.entiere = true;
  appliquerSuites(c, tout);
  CHECK(parRepere(c, "R2")->valeur == "2" + FINE + "kΩ");
  // La commande de la valeur de R1, puis ses suites
  const ConceptionElec avant = c;
  auto cmd = std::make_unique<CommandeProprieteComposant>(parRepere(c, "R1")->uuid, "valeur", "1k", "4k7", "R1", 1);
  cmd->appliquer(c);
  const ConceptionElec apresCommande = c;
  REQUIRE(appliquerSuites(c, cmd->changement()));
  CommandeAvecSuites tout2(std::move(cmd), std::make_unique<CommandeInstantane>("suites", apresCommande, c));
  CHECK(parRepere(c, "R2")->valeur == "9,4" + FINE + "kΩ");
  tout2.defaire(c);
  CHECK(parRepere(c, "R1")->valeur == "1k");
  CHECK(parRepere(c, "R2")->valeur == "2" + FINE + "kΩ");
  tout2.appliquer(c);
  CHECK(parRepere(c, "R2")->valeur == "9,4" + FINE + "kΩ");
  CHECK(tout2.changement().touche(parRepere(c, "R2")->uuid));
}

TEST_CASE("3.19 : StickHub reporté sur son schéma sans liens : appariés par repère, deux à la main, écarts appliqués en un pas", "[L3][PLA-17][PLA-18]") {
  const ConceptionElec carte = conceptionDepuisKicad(prom::lireFichier(prom::cheminFichier(std::string(PROMETHEE_DONNEES) + "/kicad/StickHub.kicad_pcb")), "StickHub");
  REQUIRE(carte.circuit.empreintes.size() > 10);
  // Une carte importée sans schéma n'a aucun écart : ses trous de fixation sans symbole restent
  CHECK(ecartsSynchronisation(carte).empty());
  // Le schéma, recréé : mêmes composants sous d'autres identifiants, sans circuit ; deux repères changés depuis
  ConceptionElec schema = carte;
  schema.uuid = nouvelUuid();
  schema.circuit.empreintes.clear();
  schema.circuit.pistes.clear();
  schema.circuit.vias.clear();
  schema.circuit.zones.clear();
  std::map<std::string, std::string> nouveaux;
  for (auto& x : schema.composants) {
    nouveaux[x.uuid] = nouvelUuid();
    x.uuid = nouveaux[x.uuid];
  }
  for (auto& f : schema.feuilles)
    for (auto& s : f.symboles)
      if (!s.composant.empty() && nouveaux.count(s.composant)) s.composant = nouveaux[s.composant];
  // Les noms figés des réseaux suivent les identifiants (« UUID:broche »)
  std::map<std::string, std::string> noms;
  for (const auto& [cle, nom] : schema.nomsReseaux) {
    const std::size_t deux = cle.rfind(':');
    const std::string u = cle.substr(0, deux);
    noms[(nouveaux.count(u) ? nouveaux[u] : u) + cle.substr(deux)] = nom;
  }
  schema.nomsReseaux = noms;
  std::vector<std::string> changes;
  for (auto& x : schema.composants)
    if (changes.size() < 2 && x.reference.rfind("R", 0) == 0) {
      changes.push_back(x.reference);
      x.reference += "0";  // « R5 » devient « R50 » au schéma
    }
  REQUIRE(changes.size() == 2);
  const auto sansLien = empreintesSansLien(carte);
  const ResultatAppariement r = apparier(schema, sansLien);
  int parRepere = 0;
  for (const auto& p : r.paires) parRepere += p.par == Appariement::Par::Repere;
  CHECK(parRepere == static_cast<int>(sansLien.size()) - 2);
  // Les deux autres : appariés par valeur et empreinte, au coût minimal, ou laissés à la main
  std::vector<Appariement> paires = r.paires;
  for (std::size_t i : r.empreintesSeules)
    for (const auto& x : schema.composants)
      if (x.reference == sansLien[i].reference + "0") paires.push_back({i, x.uuid, Appariement::Par::Main, 0});
  CHECK(paires.size() == sansLien.size());
  ConceptionElec cible = schema;
  CHECK(reporterCarte(cible, carte, paires) == static_cast<int>(sansLien.size()));
  CHECK(cible.circuit.pistes.size() == carte.circuit.pistes.size());
  // Les réseaux du cuivre sont ceux de la carte, le schéma est le même : rien à reporter
  std::string dits;
  for (const auto& e : ecartsSynchronisation(cible)) dits += e.description + " | ";
  INFO(dits);
  CHECK(ecartsSynchronisation(cible).empty());
}

namespace {
// Les réseaux, chacun par ses broches : deux conceptions de même connectivité donnent le même ensemble
std::set<std::vector<std::string>> reseauxParBroches(const ConceptionElec& c) {
  std::set<std::vector<std::string>> r;
  for (const auto& n : connectivite(c).reseaux) {
    std::vector<std::string> b;
    for (const auto& x : n.broches) b.push_back(x.composant + ":" + x.broche);
    std::sort(b.begin(), b.end());
    if (b.size() > 1) r.insert(b);
  }
  return r;
}
}  // namespace

TEST_CASE("3.21 : tableau de tous les objets, enfants compris, modifiable par expression, substitution ou collage", "[L3][ERG-41]") {
  CHECK(lireNombreObjet("12,7 mm") == Approx(12.7));
  CHECK(lireNombreObjet(" -3.5 ") == Approx(-3.5));
  CHECK(lireNombreObjet("90°") == Approx(90));
  CHECK(lireNombreObjet("=2*3,5") .value_or(0) == Approx(7));
  CHECK_FALSE(lireNombreObjet("abc").has_value());
  CHECK(ecrireNombreObjet(12.7) == "12,7");
  CHECK(ecrireNombreObjet(-0.00001) == "0");
  CHECK(transformerCellule("12,5", ProprieteObjet::X, {RegleTexte::Genre::Formule, "", "x+2.54"}) == "15,04");
  CHECK(transformerCellule("R_IN", ProprieteObjet::Nom, {RegleTexte::Genre::Remplacer, "_IN", "_ENTREE"}) == "R_ENTREE");
  std::string erreur;
  CHECK_FALSE(transformerCellule("abc", ProprieteObjet::X, {RegleTexte::Genre::Formule, "", "x+1"}, &erreur).has_value());

  ConceptionElec c = exempleElectronique();
  const std::vector<LigneObjet> lignes = objetsConception(c);
  const ComposantElec* r1 = parRepere(c, "R1");
  REQUIRE(r1);
  // R1, puis ses enfants : son symbole posé et son empreinte
  const auto i = std::find_if(lignes.begin(), lignes.end(), [&](const LigneObjet& l) { return l.genre == GenreObjet::Composant && l.id == r1->uuid; });
  REQUIRE(i != lignes.end());
  REQUIRE(i + 2 < lignes.end());
  CHECK(i[1].genre == GenreObjet::Symbole);
  CHECK(i[1].profondeur == 1);
  CHECK(i[1].parent == "R1");
  const auto empreinte = std::find_if(lignes.begin(), lignes.end(), [&](const LigneObjet& l) { return l.genre == GenreObjet::Empreinte && l.id == r1->uuid; });
  REQUIRE(empreinte != lignes.end());
  CHECK(empreinte->parent == "R1");
  int pistes = 0;
  for (const auto& l : lignes) pistes += l.genre == GenreObjet::Piste;
  CHECK(pistes == static_cast<int>(c.circuit.pistes.size()));

  // Le symbole déplacé de 2,54 mm : ses fils suivent, la connectivité ne change pas
  const auto avant = reseauxParBroches(c);
  const LigneObjet sym = i[1];
  const std::string x = *transformerCellule(sym.textes.at(ProprieteObjet::X), ProprieteObjet::X, {RegleTexte::Genre::Formule, "", "x+2.54"});
  CHECK(ecrireProprieteObjet(c, sym, ProprieteObjet::X, x, &erreur));
  const SymbolePlace* sp = nullptr;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == sym.id) sp = &s;
  REQUIRE(sp);
  CHECK(mm(sp->position.x) == Approx(*lireNombreObjet(x)));
  CHECK(reseauxParBroches(c) == avant);
  // Le schéma tourne par quarts de tour
  CHECK_FALSE(ecrireProprieteObjet(c, sym, ProprieteObjet::Rotation, "45", &erreur));
  CHECK(erreur.find("quarts") != std::string::npos);
  CHECK(ecrireProprieteObjet(c, sym, ProprieteObjet::Rotation, "180", &erreur));
  CHECK(sp->rotation == 2);
  // Empreinte : coller une position et une face
  CHECK(ecrireProprieteObjet(c, *empreinte, ProprieteObjet::X, "31,75", &erreur));
  CHECK(ecrireProprieteObjet(c, *empreinte, ProprieteObjet::Lieu, "Dessous", &erreur));
  CHECK_FALSE(ecrireProprieteObjet(c, *empreinte, ProprieteObjet::Lieu, "à côté", &erreur));
  for (const auto& e : c.circuit.empreintes)
    if (e.composant == r1->uuid) {
      CHECK(e.position.x == nm(31.75));
      CHECK(e.dessous);
    }
  // Valeur d'un composant ; un repère vide est refusé ; une colonne qui ne se modifie pas aussi
  const LigneObjet comp = *i;
  CHECK(ecrireProprieteObjet(c, comp, ProprieteObjet::Valeur, "22k", &erreur));
  CHECK(parRepere(c, "R1"));
  CHECK(parRepere(c, "R1")->valeur == "22k");
  CHECK_FALSE(ecrireProprieteObjet(c, comp, ProprieteObjet::Nom, "  ", &erreur));
  CHECK_FALSE(ecrireProprieteObjet(c, comp, ProprieteObjet::X, "3", &erreur));
  // Une piste : largeur et couche
  if (!c.circuit.pistes.empty()) {
    LigneObjet piste;
    for (const auto& l : objetsConception(c))
      if (l.genre == GenreObjet::Piste && l.id == c.circuit.pistes.front().uuid) piste = l;
    CHECK(ecrireProprieteObjet(c, piste, ProprieteObjet::Largeur, "0,5", &erreur));
    CHECK(c.circuit.pistes.front().largeur == nm(0.5));
    CHECK_FALSE(ecrireProprieteObjet(c, piste, ProprieteObjet::Lieu, "Sérigraphie dessus", &erreur));
    CHECK(ecrireProprieteObjet(c, piste, ProprieteObjet::Lieu, "B.Cu", &erreur));
    CHECK(c.circuit.pistes.front().couche == Couche::CuivreDessous);
  }
}

TEST_CASE("S16 : un fil élastique avec un fil en T en son milieu garde la liaison quand son symbole se décale", "[L3][S16]") {
  ConceptionElec c = exempleElectronique();
  const auto avant = reseauxParBroches(c);
  // R1 est vertical ; le fil qui descend vers R2 porte en son milieu le fil de l'étiquette DECH
  std::string r1;
  for (const auto& s : c.feuilles[0].symboles)
    if (s.composant == parRepere(c, "R1")->uuid) r1 = s.uuid;
  REQUIRE_FALSE(r1.empty());
  deplacerSchema(c, 0, {r1}, ptMm(2.54, 0), true);
  CHECK(reseauxParBroches(c) == avant);
  deplacerSchema(c, 0, {r1}, ptMm(-5.08, 2.54), true);
  CHECK(reseauxParBroches(c) == avant);
  // Tous les fils restent à l'équerre
  for (const auto& w : c.feuilles[0].fils) CHECK((w.a.x == w.b.x || w.a.y == w.b.y));
}
