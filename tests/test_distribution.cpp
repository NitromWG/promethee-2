// Prométhée : essais du calcul d'installation basse tension (valeurs de référence calculées à la main selon le
// guide UTE C 15-105 et la NF C 15-100). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electrique_distribution.hpp"

using namespace prom::elec;
using Catch::Approx;

namespace {

bool aAvis(const ResultatDepartBT& x, NiveauAvis n, const std::string& regle) {
  return std::any_of(x.avis.begin(), x.avis.end(), [&](const AvisCalcul& a) { return a.niveau == n && a.regle.find(regle) != std::string::npos; });
}

ReseauBT reseauTransfo630(RegimeNeutre regime = RegimeNeutre::TNS) {
  ReseauBT r;
  r.source.type = TypeSourceBT::Transformateur;
  r.source.puissance = 630;
  r.source.ucc = 4;
  r.source.sccAmont = 500;
  r.source.regime = regime;
  r.selectivite = false;
  return r;
}

DepartBT& ajouter(ReseauBT& r, TypeCharge t, const std::string& parent) {
  r.departs.push_back(nouveauDepart(r, t, parent));
  return r.departs.back();
}

}  // namespace

TEST_CASE("Distribution : impédance de la source et courant de court-circuit (UTE C 15-105)") {
  ReseauBT r = reseauTransfo630();
  const ResultatReseauBT x = calculerReseau(r);
  // Réseau amont 500 MVA : Za = (1,05 × 400)² / 500 MVA = 0,3528 mΩ ; transformateur 630 kVA, 4 % : ZT = 11,2 mΩ,
  // RT = 0,31 ZT, XT = 0,95 ZT
  CHECK(x.rSource == Approx(0.0351 + 3.472).margin(0.01));
  CHECK(x.xSource == Approx(0.3510 + 10.64).margin(0.01));
  // Ik3 = 1,05 × 1,05 × 230,94 / Z
  CHECK(x.ik3Source == Approx(22.07).margin(0.05));
  CHECK(x.inSource == Approx(630000.0 / (std::sqrt(3.0) * 400)).margin(0.5));

  // Groupe électrogène : courant de court-circuit bien plus faible
  r.source.type = TypeSourceBT::Groupe;
  r.source.puissance = 250;
  const ResultatReseauBT g = calculerReseau(r);
  CHECK(g.ik3Source < 4.0);
  CHECK(g.ik3Source > 2.0);

  // Branchement à puissance limitée monophasé : l'Icc donné est un courant phase-neutre
  r.source.type = TypeSourceBT::BranchementLimite;
  r.source.triphase = false;
  r.source.icc = 6;
  r.source.regime = RegimeNeutre::TT;
  CHECK(calculerReseau(r).ik3Source == Approx(6.0).margin(0.01));
}

TEST_CASE("Distribution : canalisation imposée — courts-circuits, chute de tension, défaut à la masse") {
  ReseauBT r = reseauTransfo630();
  DepartBT& d = ajouter(r, TypeCharge::Autre, "");
  d.polarite = Polarite::Triphase;
  d.puissance = 30;
  d.cosPhi = 0.85;
  d.longueur = 50;
  d.sectionImposee = 25;
  d.unipolaire = false;
  const ResultatReseauBT x = calculerReseau(r);
  const ResultatDepartBT& y = x.departs.front();
  CHECK(y.ib == Approx(30000.0 / 0.85 / (std::sqrt(3.0) * 400)).margin(0.01));
  // R = 18,51 × 50 / 25 = 37,02 mΩ ; X = 0,08 × 50 = 4 mΩ
  CHECK(y.ik3max == Approx(254.61 / std::hypot(3.507 + 37.02, 10.991 + 4.0)).margin(0.02));
  // Biphasé minimal (sans neutre) : ρ1 = 1,25 ρ0, c = 0,95
  CHECK(y.ikMin == Approx(0.866 * 230.36 / std::hypot(3.507 + 1.25 * 37.02, 10.991 + 4.0)).margin(0.02));
  // Chute de tension (annexe G) : (0,0225 × 50 / 25 × 0,85 + 0,00008 × 50 × sin φ) × Ib / U0
  CHECK(y.chute == Approx(100.0 * (0.0225 * 50 / 25 * 0.85 + 0.00008 * 50 * std::sqrt(1 - 0.85 * 0.85)) * y.ib / 230.94).margin(0.005));
  // PE de 16 mm² (tableau 54F), défaut à la masse en TN-S
  CHECK(y.sectionPE == Approx(16));
  CHECK(y.ief == Approx(230.36 / std::hypot(3.507 + 1.25 * (37.02 + 18.51 * 50 / 16), 10.991 + 8.0)).margin(0.02));
  CHECK(y.designationCable.find("3×25") != std::string::npos);
}

TEST_CASE("Distribution : la chute de tension impose la section d'un éclairage sur le réseau public") {
  ReseauBT r;
  r.source.type = TypeSourceBT::BranchementLimite;
  r.source.puissance = 36;
  r.source.triphase = true;
  r.source.icc = 6;
  r.source.regime = RegimeNeutre::TT;
  r.source.resistanceTerre = 30;
  DepartBT& t = ajouter(r, TypeCharge::Tableau, "");
  t.longueur = 1;
  t.differentiel = 300;
  t.differentielSelectif = true;
  const std::string tgbt = t.uuid;
  DepartBT& e = ajouter(r, TypeCharge::Eclairage, tgbt);
  e.puissance = 2;
  e.cosPhi = 1;
  e.longueur = 30;
  e.pose = MethodePose::C;
  const ResultatReseauBT x = calculerReseau(r);
  const ResultatDepartBT& y = x.departs[1];
  CHECK(y.ib == Approx(2000.0 / 230.94).margin(0.01));
  CHECK(y.in == Approx(10));
  // 1,5 mm² : 3,4 % > 3 % ; 2,5 mm² : 2,0 %
  CHECK(y.section == Approx(2.5));
  CHECK(y.chuteCumulee < 3.0);
  CHECK(y.chuteLimite == Approx(3.0));
  CHECK(y.personnesProtegees);
  CHECK(y.erreurs() == 0);
  CHECK(x.erreurs == 0);

  // En TT, sans différentiel nulle part, chaque circuit est en défaut
  r.departs[0].differentiel = 0;
  const ResultatReseauBT z = calculerReseau(r);
  CHECK(aAvis(z.departs[1], NiveauAvis::Erreur, "411.5"));
  CHECK_FALSE(z.departs[1].personnesProtegees);
}

TEST_CASE("Distribution : prises sans 30 mA, pouvoir de coupure, contacts indirects en TN") {
  ReseauBT r = reseauTransfo630();
  DepartBT& p = ajouter(r, TypeCharge::Prises, "");
  p.longueur = 20;
  CHECK(p.differentiel == Approx(30));  // les prises naissent protégées par 30 mA
  p.differentiel = 0;
  ResultatReseauBT x = calculerReseau(r);
  CHECK(aAvis(x.departs[0], NiveauAvis::Erreur, "411.3.3"));
  // Pouvoir de coupure choisi au-dessus de l'Icc du point de pose (22 kA aux bornes du transformateur)
  CHECK(x.departs[0].pdcRequis == Approx(x.ik3Source).margin(1e-6));
  CHECK(x.departs[0].pdc >= x.departs[0].pdcRequis);
  r.departs[0].pdcImpose = 10;
  x = calculerReseau(r);
  CHECK(aAvis(x.departs[0], NiveauAvis::Erreur, "434.5.1"));

  // Ligne longue en TN-S, section imposée : le défaut ne fait pas déclencher, longueur maximale conventionnelle
  ReseauBT t = reseauTransfo630();
  DepartBT& l = ajouter(t, TypeCharge::Autre, "");
  l.polarite = Polarite::Monophase;
  l.puissance = 2;
  l.calibreImpose = 16;
  l.sectionImposee = 2.5;
  l.longueur = 300;
  l.courbe = "C";
  const ResultatReseauBT y = calculerReseau(t);
  CHECK(aAvis(y.departs[0], NiveauAvis::Erreur, "411.4"));
  CHECK_FALSE(y.departs[0].personnesProtegees);
  // Lmax = 0,8 U0 S / (ρ1 (1 + m) Ia), Ia = 10 × 16 A
  CHECK(y.departs[0].longueurMax == Approx(0.8 * 230.94 * 2.5 / (1.25 * 0.01851 * 2 * 160)).margin(0.5));
  // Laissé au calcul, la section grossit jusqu'à protéger les personnes
  t.departs[0].sectionImposee = 0;
  const ResultatReseauBT w = calculerReseau(t);
  CHECK(w.departs[0].personnesProtegees);
  CHECK(w.departs[0].section > 2.5);
}

TEST_CASE("Distribution : sélectivité, harmoniques, compensation, bornes de recharge, démarrage") {
  // Sélectivité recherchée : la protection du tableau vaut au moins 1,6 fois le plus fort départ
  ReseauBT r = reseauTransfo630();
  r.selectivite = true;
  const std::string tgbt = ajouter(r, TypeCharge::Tableau, "").uuid;
  DepartBT& m = ajouter(r, TypeCharge::Autre, tgbt);
  m.puissance = 40;
  m.polarite = Polarite::Triphase;
  ResultatReseauBT x = calculerReseau(r);
  CHECK(x.departs[0].ir >= 1.6 * x.departs[1].ir - 1e-6);
  CHECK(x.departs[1].selectivite != "aucune en surcharge");

  // Harmonique 3 à 40 % : neutre chargé à 1,2 Ib, section choisie sur ce courant
  ReseauBT h = reseauTransfo630();
  DepartBT& d = ajouter(h, TypeCharge::Autre, "");
  d.polarite = Polarite::TriphaseNeutre;
  d.puissance = 50;
  d.tauxH3 = 40;
  x = calculerReseau(h);
  CHECK(x.departs[0].iNeutre == Approx(1.2 * x.departs[0].ib).margin(0.01));
  CHECK(x.departs[0].courantDimensionnement >= x.departs[0].iNeutre - 1e-6);
  CHECK(x.departs[0].sectionNeutre == Approx(x.departs[0].section));

  // Compensation : 100 kW à cos φ 0,8 vers 0,93 → 35,5 kvar, arrondis à 40
  ReseauBT c = reseauTransfo630();
  DepartBT& k = ajouter(c, TypeCharge::Autre, "");
  k.puissance = 100;
  k.cosPhi = 0.8;
  x = calculerReseau(c);
  CHECK(x.cosPhi == Approx(0.8).margin(1e-6));
  CHECK(x.compensation == Approx(40));

  // Bornes de recharge : pas de foisonnement (722.311), les autres départs foisonnent
  ReseauBT v = reseauTransfo630();
  const std::string td = ajouter(v, TypeCharge::Tableau, "").uuid;
  for (int n = 0; n < 4; ++n) ajouter(v, TypeCharge::IRVE, td).puissance = 7.4;
  DepartBT& e = ajouter(v, TypeCharge::Eclairage, td);
  e.puissance = 1;
  e.cosPhi = 1;
  x = calculerReseau(v);
  CHECK(x.departs[0].p == Approx(4 * 7.4 + 0.8 * 1).margin(1e-6));  // 5 départs : ks 0,8 pour l'éclairage seul

  // Démarrage direct d'un moteur : chute de tension au démarrage calculée
  ReseauBT mo = reseauTransfo630();
  DepartBT& mt = ajouter(mo, TypeCharge::Moteur, "");
  mt.puissance = 30;
  mt.longueur = 80;
  x = calculerReseau(mo);
  CHECK(x.departs[0].protection == TypeProtection::Moteur);
  CHECK(x.departs[0].chuteDemarrage > x.departs[0].chuteCumulee);
}

TEST_CASE("Distribution : maison sur branchement monophasé, modèles d'installation sans erreur") {
  const ReseauBT r = reseauModele(ModeleReseau::Maison);
  const ResultatReseauBT x = calculerReseau(r);
  CHECK(x.erreurs == 0);
  for (const auto& d : x.departs) {
    // Monophasé : le courant suit S / U0 et le court-circuit maximal est entre phase et neutre
    CHECK(d.ik3max == Approx(d.ik1max).margin(1e-9));
    CHECK(d.pdc == Approx(3));  // 3 kA suffisent derrière un branchement à puissance limitée
  }
  CHECK(x.departs[0].ib == Approx(x.s * 1000.0 / 230.94).margin(0.05));
  for (ModeleReseau m : {ModeleReseau::Vide, ModeleReseau::Tertiaire, ModeleReseau::Atelier, ModeleReseau::ParkingIRVE, ModeleReseau::Maison}) {
    const ResultatReseauBT y = calculerReseau(reseauModele(m));
    INFO(nomModeleReseau(m));
    CHECK(y.erreurs == 0);
  }
}

TEST_CASE("Distribution : arbre, suppression, repères, enregistrement, documents, unifilaire") {
  ReseauBT r = reseauModele(ModeleReseau::Tertiaire);
  const auto ordre = ordreArbre(r);
  REQUIRE(ordre.size() == r.departs.size());
  CHECK(ordre.front().second == 0);
  CHECK(r.departs[ordre.front().first].repere == "TGBT");
  CHECK(prochainRepere(r, TypeCharge::Tableau) == "TD3");
  CHECK(prochainRepere(r, TypeCharge::Eclairage) == "Q12");
  // Supprimer un tableau emporte ses départs
  std::string rdc;
  for (const auto& d : r.departs)
    if (d.nom == "Tableau du rez-de-chaussée") rdc = d.uuid;
  ReseauBT s = r;
  CHECK(supprimerDepart(s, rdc) == 5);

  // Enregistrement
  ProjetElectrique p = nouveauProjetElectrique("Bureaux", true);
  p.distribution = r;
  const ProjetElectrique q = projetElectriqueDepuisJson(versJson(p));
  REQUIRE(q.distribution);
  REQUIRE(q.distribution->departs.size() == r.departs.size());
  CHECK(q.distribution->source.type == TypeSourceBT::Transformateur);
  CHECK(q.distribution->source.puissance == Approx(250));
  for (std::size_t i = 0; i < r.departs.size(); ++i) {
    CHECK(q.distribution->departs[i].uuid == r.departs[i].uuid);
    CHECK(q.distribution->departs[i].parent == r.departs[i].parent);
    CHECK(q.distribution->departs[i].charge == r.departs[i].charge);
    CHECK(q.distribution->departs[i].puissance == Approx(r.departs[i].puissance));
  }
  CHECK(calculerReseau(*q.distribution).s == Approx(calculerReseau(r).s));

  // Note de calcul
  const ResultatReseauBT x = calculerReseau(r);
  const std::string csv = csvNoteDeCalcul(r, x);
  CHECK(csv.find("TGBT") != std::string::npos);
  CHECK(csv.find("Ik3 max en bout (kA)") != std::string::npos);
  const std::string html = htmlNoteDeCalcul(r, x, "Bureaux");
  CHECK(html.find("UTE C 15-105") != std::string::npos);
  CHECK(html.find("Q10") != std::string::npos);

  // Unifilaire : un folio par tableau, repères du calcul, régénération sans doublon
  const std::size_t avant = p.schema.feuilles.size();
  const std::size_t premier = genererUnifilaire(p);
  CHECK(premier == avant);
  const std::size_t apres = p.schema.feuilles.size();
  CHECK(apres - avant == 3);  // TGBT, rez-de-chaussée, étage
  int origine = 0;
  for (const auto& f : p.schema.feuilles) origine += p.folio(f.uuid).origine == "distribution" ? 1 : 0;
  CHECK(origine == 3);
  CHECK(p.schema.parReference("Q10") != nullptr);
  genererUnifilaire(p);
  CHECK(p.schema.feuilles.size() == apres);
  int q10 = 0;
  for (const auto& c : p.schema.composants) q10 += c.reference == "Q10" ? 1 : 0;
  CHECK(q10 == 1);
  // Le schéma tiré du calcul est propre : aucune erreur à la vérification
  int erreurs = 0;
  for (const auto& v : verifierElectrique(p)) erreurs += v.gravite == prom::ecad::Gravite::Erreur ? 1 : 0;
  CHECK(erreurs == 0);
}
