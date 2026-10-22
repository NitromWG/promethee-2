// Prométhée : essais du calcul photovoltaïque (guide UTE C 15-712-1 ; valeurs de référence calculées à la main).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electrique_pv.hpp"

using namespace prom::elec;
using Catch::Approx;

namespace {

bool aAvis(const ResultatPV& x, NiveauAvis n, const std::string& texte) {
  return std::any_of(x.avis.begin(), x.avis.end(), [&](const AvisCalcul& a) { return a.niveau == n && (a.regle + " " + a.message).find(texte) != std::string::npos; });
}

}  // namespace

TEST_CASE("Photovoltaïque : maison de 4,25 kWc, calepinage, câbles, protections, production") {
  const InstallationPV i;  // 10 modules de 425 Wc, onduleur monophasé 3,6 kVA à 2 MPPT, −10 °C
  const ResultatPV x = calculerPV(i);
  CHECK(x.erreurs() == 0);
  // Uoc au froid : 38,5 × (1 + 0,27 % × 35) = 42,14 V ; 600 V / 42,14 → 14 modules au plus
  CHECK(x.kTemperature == Approx(1.0945));
  CHECK(x.uocModuleMax == Approx(42.138).margin(0.01));
  CHECK(x.seriesMax == 14);
  // Vmp à 70 °C : 32,2 × (1 − 0,27 % × 45) = 28,29 V ; 90 V / 28,29 → 4 modules au moins
  CHECK(x.seriesMin == 4);
  CHECK(x.modulesParChaine == 10);
  CHECK(x.chaines == 1);
  CHECK(x.repartition == std::vector<int>{1, 0});
  CHECK(x.puissanceCrete == Approx(4.25));
  CHECK(x.ratioDCAC == Approx(4250.0 / 3680).margin(0.001));
  CHECK(x.uocChaine == Approx(421.38).margin(0.05));
  // Une chaîne : pas de fusible ; câble 1,25 Isc = 17,4 A → 4 mm² (44 A × 0,91 à 70 °C), ΔU 0,94 %
  CHECK_FALSE(x.fusibles);
  CHECK(x.chainesMaxSansProtection == 2);
  CHECK(x.izRequis == Approx(17.375));
  CHECK(x.sectionDC == Approx(4));
  CHECK(x.izDC == Approx(44 * std::sqrt(50.0 / 60)).margin(0.01));
  CHECK(x.chuteDC == Approx(100.0 * 2 * 0.023 * 20 * 13.2 / (4 * 322)).margin(0.001));
  CHECK_FALSE(x.boiteJonction);
  CHECK(x.sectionneurTension == Approx(x.uocChaine));
  CHECK(x.sectionneurCourant == Approx(17.375));
  // Parafoudres : Lcrit = 115 / 1,5 = 77 m > 20 m ; onduleur à 15 m du tableau : second parafoudre conseillé
  CHECK(x.longueurCritique == Approx(115 / 1.5));
  CHECK_FALSE(x.parafoudreDC);
  CHECK(x.parafoudreAC);
  // Alternatif : 1,1 × 3 680 / 230 = 17,6 A → 20 A ; 6 mm² pour rester sous 1 % (0,86 %) ; 3 kA ; 30 mA
  CHECK(x.courantAC == Approx(17.6));
  CHECK(x.calibreAC == Approx(20));
  CHECK(x.sectionAC == Approx(6));
  CHECK(x.chuteAC == Approx(100.0 * 2 * 0.0225 * 15 / 6 * 17.6 / 230).margin(0.001));
  CHECK(x.pdcAC == Approx(3));
  CHECK(x.differentielAC.find("30 mA") != std::string::npos);
  CHECK(x.production == Approx(4.25 * 1150));
  CHECK(aAvis(x, NiveauAvis::Info, "DC21B"));
  CHECK(aAvis(x, NiveauAvis::Info, "Coupure d'urgence entrée onduleur"));
  CHECK(aAvis(x, NiveauAvis::Info, "entrée d'onduleur sans chaîne"));
}

TEST_CASE("Photovoltaïque : chaînes en parallèle, fusibles gPV, boîte de jonction") {
  InstallationPV i;
  i.nombreModules = 30;
  OnduleurPV& o = i.onduleur;
  o.puissanceAC = 10000;
  o.puissanceDCMax = 15000;
  o.tensionDCMax = 600;
  o.mppt = 1;
  o.entreesParMppt = 1;
  o.courantMppt = 50;
  o.iscMppt = 60;
  o.triphase = true;
  const ResultatPV x = calculerPV(i);
  CHECK(x.erreurs() == 0);
  CHECK(x.modulesParChaine == 10);
  CHECK(x.chaines == 3);
  CHECK(x.chainesParMppt == 3);
  // 3 chaînes > 1 + 25 / 13,9 = 2,8 : fusibles de 20 A (1,4 × 13,9 = 19,5 A ≤ 20 ≤ IRM 25), câbles Iz ≥ 1,45 × 20 = 29 A
  CHECK(x.fusibles);
  CHECK(x.calibreFusible == Approx(20));
  CHECK(x.izRequis == Approx(29));
  CHECK(x.sectionDC == Approx(4));
  // Trois chaînes sur un connecteur : boîte de jonction, câble principal Iz ≥ 1,25 × 13,9 × 3 = 52,1 A → 10 mm²
  CHECK(x.boiteJonction);
  CHECK(x.sectionPrincipal == Approx(10));
  CHECK(x.sectionneurCourant == Approx(1.25 * 13.9 * 3));
  // Triphasé : 1,1 × 10 000 / (√3 × 400) = 15,9 A → 16 A
  CHECK(x.courantAC == Approx(1.1 * 10000 / (std::sqrt(3.0) * 400)).margin(0.01));
  CHECK(x.calibreAC == Approx(16));
  CHECK(aAvis(x, NiveauAvis::Info, "gPV de 20 A"));
}

TEST_CASE("Photovoltaïque : incompatibilités, foudre, raccordement, orientation") {
  // Plage MPPT trop haute pour la tension admise : erreur, aucune chaîne
  InstallationPV a;
  a.onduleur.mpptMin = 500;
  const ResultatPV xa = calculerPV(a);
  CHECK(xa.erreurs() >= 1);
  CHECK(xa.modulesParChaine == 0);
  CHECK(aAvis(xa, NiveauAvis::Erreur, "incompatibles"));
  // Chaîne imposée trop longue : tension à vide au-delà de 600 V par grand froid
  InstallationPV b;
  b.nombreModules = 15;
  b.modulesParChaine = 15;
  CHECK(aAvis(calculerPV(b), NiveauAvis::Erreur, "tension à vide de la chaîne"));
  // Foudroiement élevé et câbles longs : parafoudres des deux côtés ; paratonnerre : type 1
  InstallationPV c;
  c.ng = 3;
  c.longueurDC = 40;
  const ResultatPV xc = calculerPV(c);
  CHECK(xc.longueurCritique == Approx(115 / 3.0));
  CHECK(xc.parafoudreDC);
  CHECK(xc.parafoudreAC);
  CHECK(aAvis(xc, NiveauAvis::Alerte, "Ng > 2,5"));
  c.paratonnerre = true;
  CHECK(calculerPV(c).typeParafoudreAC == "type 1");
  // Tertiaire : Lcrit = 450 / Ng ; 300 mA
  InstallationPV d;
  d.batiment = BatimentPV::Tertiaire;
  const ResultatPV xd = calculerPV(d);
  CHECK(xd.longueurCritique == Approx(300));
  CHECK(xd.differentielAC.find("300 mA") != std::string::npos);
  // Deux onduleurs monophasés de 3,68 kVA : 7,36 kVA en monophasé, refusé par le distributeur
  InstallationPV e;
  e.nombreModules = 20;
  e.onduleurs = 2;
  CHECK(aAvis(calculerPV(e), NiveauAvis::Erreur, "monophasé limitée à 6 kVA"));
  // Vente totale : disjoncteur de branchement réglé au-dessus du courant total
  InstallationPV v;
  v.venteTotale = true;
  CHECK(calculerPV(v).reglageBranchement == Approx(30));
  // Orientation et inclinaison
  CHECK(coefficientOrientation(0, 30) == Approx(1.0));
  CHECK(coefficientOrientation(90, 30) == Approx(0.90));
  CHECK(coefficientOrientation(-90, 30) == Approx(0.90));
  CHECK(coefficientOrientation(-45, 60) == Approx(0.88));
  CHECK(coefficientOrientation(0, 0) == Approx(0.93));
  CHECK(coefficientOrientation(0, 90) == Approx(0.68));
  CHECK(coefficientOrientation(45, 45) == Approx(0.92));
  CHECK(coefficientOrientation(22.5, 15) == Approx(0.955));
  InstallationPV ouest;
  ouest.orientation = 90;
  CHECK(calculerPV(ouest).production == Approx(4.25 * 1150 * 0.90));
  ouest.orientation = 150;
  CHECK(aAvis(calculerPV(ouest), NiveauAvis::Alerte, "nord"));
  CHECK(productiblesIndicatifs().size() >= 4);
}

TEST_CASE("Photovoltaïque : grande toiture, deux onduleurs de 100 kVA, chaînes réparties") {
  const InstallationPV i = installationPVType(250);
  CHECK(i.onduleurs == 2);
  CHECK(i.nombreModules == 588);
  const ResultatPV x = calculerPV(i);
  CHECK(x.erreurs() == 0);
  CHECK(x.modulesParChaine == 21);
  CHECK(x.chaines == 28);
  REQUIRE(x.repartition.size() == 20);
  CHECK(std::accumulate(x.repartition.begin(), x.repartition.end(), 0) == 28);
  CHECK(x.chainesParMppt == 2);
  // Onduleurs équilibrés : 14 chaînes chacun
  CHECK(std::accumulate(x.repartition.begin(), x.repartition.begin() + 10, 0) == 14);
  CHECK_FALSE(x.fusibles);
  CHECK(x.calibreAC == Approx(160));
  CHECK(x.calibreGeneral == Approx(320));
  CHECK(x.pdcAC == Approx(6));
  CHECK(aAvis(x, NiveauAvis::Info, "Plus de 36 kVA"));
  for (double k : {3.0, 6.0, 9.0, 12.0, 36.0, 100.0}) {
    INFO(k);
    CHECK(calculerPV(installationPVType(k)).erreurs() == 0);
  }
}

TEST_CASE("Photovoltaïque : enregistrement, note de calcul, unifilaire vérifié") {
  ProjetElectrique p = nouveauProjetElectrique("Maison solaire", true);
  InstallationPV i = installationPVType(6);
  i.venteTotale = true;
  i.orientation = -20;
  i.onduleur.courantACMax = 27.5;
  p.photovoltaique = i;
  const ProjetElectrique q = projetElectriqueDepuisJson(versJson(p));
  REQUIRE(q.photovoltaique);
  CHECK(q.photovoltaique->nombreModules == i.nombreModules);
  CHECK(q.photovoltaique->module.voc == Approx(i.module.voc));
  CHECK(q.photovoltaique->onduleur.mppt == i.onduleur.mppt);
  CHECK(q.photovoltaique->onduleur.courantACMax == Approx(27.5));
  CHECK(q.photovoltaique->venteTotale);
  CHECK(q.photovoltaique->orientation == Approx(-20));
  CHECK(calculerPV(*q.photovoltaique).production == Approx(calculerPV(i).production));

  const ResultatPV x = calculerPV(i);
  const std::string html = htmlNotePV(i, x, "Maison solaire");
  CHECK(html.find("UTE C 15-712-1") != std::string::npos);
  CHECK(html.find("Lcrit") != std::string::npos);
  CHECK(html.find("kWh") != std::string::npos);

  const std::size_t avant = p.schema.feuilles.size();
  const std::size_t f = genererUnifilairePV(p);
  CHECK(f == avant);
  CHECK(p.schema.feuilles.size() == avant + 1);
  CHECK(p.folio(p.schema.feuilles[f].uuid).origine == "photovoltaique");
  int pv = 0, onduleurs = 0;
  for (const auto& c : p.schema.composants) {
    pv += c.symbole == "iec:u-pv" ? 1 : 0;
    onduleurs += c.symbole.rfind("iec:u-onduleur", 0) == 0 ? 1 : 0;
  }
  CHECK(pv == x.chaines);  // une chaîne par entrée ici
  CHECK(onduleurs == 1);
  genererUnifilairePV(p);
  CHECK(p.schema.feuilles.size() == avant + 1);
  int erreurs = 0, filsEnLAir = 0;
  for (const auto& v : verifierElectrique(p)) {
    erreurs += v.gravite == prom::ecad::Gravite::Erreur ? 1 : 0;
    filsEnLAir += v.code == "ERC-30" || v.code == "ERC-10" ? 1 : 0;
  }
  CHECK(erreurs == 0);
  CHECK(filsEnLAir == 0);
  // Projet né pour le calcul (un folio vierge) : l'unifilaire le remplace, puis se remplace lui-même quand
  // l'installation change, sans laisser l'ancien (un projet garde toujours un folio)
  ProjetElectrique seul = nouveauProjetElectrique("Toit", false);
  seul.photovoltaique = installationPVType(6);
  genererUnifilairePV(seul);
  REQUIRE(seul.schema.feuilles.size() == 1);
  seul.photovoltaique = installationPVType(250);
  genererUnifilairePV(seul);
  CHECK(seul.schema.feuilles.size() == 1);
  CHECK(seul.schema.feuilles.front().nom.find("249,90") != std::string::npos);
  ProjetElectrique inst = nouveauProjetElectrique("Atelier", false);
  inst.distribution = reseauModele(ModeleReseau::Atelier);
  genererUnifilaire(inst);
  const std::size_t foliosInst = inst.schema.feuilles.size();
  genererUnifilaire(inst);
  CHECK(inst.schema.feuilles.size() == foliosInst);
  // Grande installation : onduleurs réunis, folio sans erreur
  ProjetElectrique g = nouveauProjetElectrique("Hangar", true);
  g.photovoltaique = installationPVType(250);
  genererUnifilairePV(g);
  int erreursG = 0;
  for (const auto& v : verifierElectrique(g)) erreursG += v.gravite == prom::ecad::Gravite::Erreur || v.code == "ERC-30" ? 1 : 0;
  CHECK(erreursG == 0);
}
