// Prométhée : essais de la fabrication de l'armoire (prix, longueurs des fils, liste de coupe, bilan thermique,
// chiffrage) et des révisions du dossier. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electrique.hpp"
#include "promethee/electrique_fabrication.hpp"

using namespace prom::elec;
using Catch::Approx;

namespace {

// Démarrage direct dans son armoire, implantée automatiquement
ProjetElectrique moteurEnArmoire() {
  ProjetElectrique p = exempleDemarrageMoteur();
  if (p.armoires.empty()) p.armoires.push_back(Armoire{});
  implanterArmoire(p, 0);
  return p;
}

}  // namespace

TEST_CASE("Fabrication : chaque appareil du catalogue a un prix indicatif") {
  for (const auto& e : catalogueElectrique()) {
    INFO(e.id);
    CHECK(e.prix > 0);
  }
  // Le prix suit le calibre : un disjoncteur de 40 A coûte plus qu'un de 10 A
  REQUIRE(entreeCatalogue("dj-1pn-c10"));
  REQUIRE(entreeCatalogue("dj-1pn-c40"));
  CHECK(entreeCatalogue("dj-1pn-c40")->prix > entreeCatalogue("dj-1pn-c10")->prix);
}

TEST_CASE("Fabrication : prix saisis dans la nomenclature, gardés dans le fichier") {
  ProjetElectrique p = moteurEnArmoire();
  std::vector<LigneNomenclatureElec> n = nomenclatureElectrique(p);
  REQUIRE(!n.empty());
  const auto km = std::find_if(n.begin(), n.end(), [](const LigneNomenclatureElec& l) { return l.designation.find("Contacteur") != std::string::npos; });
  REQUIRE(km != n.end());
  CHECK(!km->prixSaisi);
  CHECK(km->prixUnitaire > 0);
  const std::string cle = km->cle;
  CHECK(cle == clePrix(km->designation, km->fabricant, km->reference));
  const double avant = chiffrage(n).total, montantAvant = km->montant();
  const int quantite = km->quantite;
  fixerPrix(p, cle, 31.5);
  n = nomenclatureElectrique(p);  // km ne sert plus : la liste est refaite
  const auto km2 = std::find_if(n.begin(), n.end(), [&](const LigneNomenclatureElec& l) { return l.cle == cle; });
  REQUIRE(km2 != n.end());
  CHECK(km2->prixSaisi);
  CHECK(km2->prixUnitaire == Approx(31.5));
  CHECK(chiffrage(n).total == Approx(avant - montantAvant + 31.5 * quantite));
  CHECK(chiffrage(n).saisies == 1);
  // Le CSV porte les prix et le total
  const std::string csv = csvNomenclatureElectrique(p);
  CHECK(csv.find("Prix unitaire HT") != std::string::npos);
  CHECK(csv.find("31,50") != std::string::npos);
  CHECK(csv.find("Total HT") != std::string::npos);
  // Enregistré, relu : les prix, le taux horaire, les réglages thermiques de l'armoire
  p.tauxHoraire = 62;
  p.armoires[0].pose = 4;
  p.armoires[0].temperatureExterieure = 38;
  p.armoires[0].charge = 0.6;
  const ProjetElectrique r = projetElectriqueDepuisJson(versJson(p));
  CHECK(r.prix.at(cle) == Approx(31.5));
  CHECK(r.tauxHoraire == Approx(62));
  CHECK(r.armoires[0].pose == 4);
  CHECK(r.armoires[0].temperatureExterieure == Approx(38));
  CHECK(r.armoires[0].charge == Approx(0.6));
  // Un prix négatif rend la ligne au prix indicatif
  fixerPrix(p, cle, -1);
  CHECK(p.prix.count(cle) == 0);
}

TEST_CASE("Fabrication : longueurs des fils par les goulottes et liste de coupe") {
  const ProjetElectrique p = moteurEnArmoire();
  const std::vector<LigneLiaison> carnet = carnetCablage(p);
  REQUIRE(!carnet.empty());
  int dedans = 0, cables = 0;
  for (const auto& l : carnet) {
    INFO(l.de + " → " + l.vers);
    CHECK(std::fmod(l.longueur, 50.0) == Approx(0));
    if (l.longueur > 0) {
      ++dedans;
      CHECK(l.longueur >= 2 * EstimateurLongueurs::RESERVE);
    } else {
      ++cables;
      // Le seul bout hors armoire : le moteur, raccordé par câble
      CHECK((l.vers.rfind("-M1", 0) == 0 || l.de.rfind("-M1", 0) == 0));
    }
  }
  CHECK(dedans > 10);
  CHECK(cables == 4);
  // Un fil vers la porte passe par la charnière : plus long qu'un fil entre deux appareils voisins sur le rail
  const auto porte = std::find_if(carnet.begin(), carnet.end(), [](const LigneLiaison& l) { return l.vers == "-S1:11"; });
  const auto rail = std::find_if(carnet.begin(), carnet.end(), [](const LigneLiaison& l) { return l.de == "-KM1:2"; });
  REQUIRE(porte != carnet.end());
  REQUIRE(rail != carnet.end());
  CHECK(porte->longueur > rail->longueur + EstimateurLongueurs::CHARNIERE - 1);

  const ListeDeCoupe c = listeDeCoupe(p);
  CHECK(static_cast<int>(c.lignes.size()) == dedans);
  CHECK(c.sansLongueur == cables);
  // Ordre de coupe : par section, puis couleur, puis du plus long au plus court
  for (std::size_t i = 1; i < c.lignes.size(); ++i) {
    const auto& a = c.lignes[i - 1];
    const auto& b = c.lignes[i];
    CHECK(std::make_tuple(a.section, a.couleur, -a.longueur) <= std::make_tuple(b.section, b.couleur, -b.longueur));
    CHECK(b.numero == a.numero + 1);
  }
  // Deux fils sur -KM1:A2 (le 0 V vers le voyant et depuis l'alimentation) : un embout double, compté une fois
  int surA2 = 0;
  for (const auto& l : c.lignes) {
    if (l.de == "-KM1:A2") {
      ++surA2;
      CHECK(l.emboutDe.rfind("embout double", 0) == 0);
    }
    if (l.vers == "-KM1:A2") {
      ++surA2;
      CHECK(l.emboutVers.rfind("embout double", 0) == 0);
    }
  }
  CHECK(surA2 == 2);
  // Embouts au code couleur français
  CHECK(embout(1.5) == "embout 1,5 mm² noir");
  CHECK(embout(0.75, true) == "embout double 2 × 0,75 mm² bleu");
  CHECK(embout(2.5) == "embout 2,5 mm² gris");
  // Filerie : la somme des longueurs, 10 % de chute
  double total = 0, commande = 0;
  for (const auto& l : c.lignes) total += l.longueur / 1000;
  for (const auto& b : c.bobines) commande += b.metres;
  CHECK(commande == Approx(total * 1.1).margin(0.1 * static_cast<double>(c.bobines.size())));
  CHECK(csvListeDeCoupe(p).find("Embout") != std::string::npos);
}

TEST_CASE("Fabrication : bilan thermique de l'armoire (CEI/TR 60890)") {
  // Surface d'échange : 600 × 800 × 250, isolée contre un mur → 1,4 × 0,6 × (0,8 + 0,25) + 1,8 × 0,25 × 0,8
  CHECK(surfaceEchangeArmoire(600, 800, 250, 1) == Approx(1.4 * 0.6 * 1.05 + 1.8 * 0.25 * 0.8));
  CHECK(surfaceEchangeArmoire(600, 800, 250, 6) < surfaceEchangeArmoire(600, 800, 250, 0));
  ProjetElectrique p = moteurEnArmoire();
  BilanThermique t = bilanThermique(p, 0);
  CHECK(t.dissipee > 5);
  CHECK(t.sansDonnee == 0);
  CHECK(t.echauffement == Approx(t.dissipee / (5.5 * t.surface)));
  CHECK(t.temperatureInterieure == Approx(p.armoires[0].temperatureExterieure + t.echauffement));
  CHECK(t.ventilation == 0);
  CHECK(t.climatisation == 0);
  CHECK(std::is_sorted(t.pertes.begin(), t.pertes.end(), [](const PerteAppareil& a, const PerteAppareil& b) { return a.puissance > b.puissance; }));
  // Moins de charge : moins de pertes (celles du courant suivent son carré)
  p.armoires[0].charge = 0.4;
  CHECK(bilanThermique(p, 0).dissipee < t.dissipee);
  // Local chaud, peu d'écart admis : il faut ventiler
  p.armoires[0].charge = 1;
  p.armoires[0].temperatureExterieure = 44;
  p.armoires[0].temperatureAdmise = 45;
  t = bilanThermique(p, 0);
  CHECK(t.ventilation > 0);
  CHECK(t.ventilation == Approx(3.1 * (t.dissipee - 5.5 * t.surface * 1) / 1));
  // Plus chaud dehors que l'admis : seul un climatiseur convient
  p.armoires[0].temperatureExterieure = 50;
  t = bilanThermique(p, 0);
  CHECK(t.climatisation == Approx(t.dissipee + 5.5 * t.surface * 5));
  CHECK(t.ventilation == 0);
}

TEST_CASE("Fabrication : chiffrage du tableau") {
  ProjetElectrique p = moteurEnArmoire();
  p.tauxHoraire = 50;
  const Chiffrage c = chiffrer(p);
  CHECK(c.materiel == Approx(chiffrage(nomenclatureElectrique(p)).total));
  CHECK(c.filerie > 0);
  CHECK(c.heures > 0);
  CHECK(c.mainOeuvre == Approx(c.heures * 50).margin(0.01));
  CHECK(c.total == Approx(c.materiel + c.filerie + c.mainOeuvre));
  double h = 0;
  for (const auto& x : c.postes) h += x.heures;
  CHECK(c.heures == Approx(h).margin(0.05));
  CHECK(std::any_of(c.postes.begin(), c.postes.end(), [](const PosteChiffrage& x) { return x.nom.find("porte") != std::string::npos && x.quantite == 4; }));
  CHECK(csvChiffrage(p).find("Total HT") != std::string::npos);
}

TEST_CASE("Révisions : indices, cartouche et fichier") {
  CHECK(indiceSuivant("") == "A");
  CHECK(indiceSuivant("A") == "B");
  CHECK(indiceSuivant("Z") == "AA");
  CHECK(indiceSuivant("AZ") == "BA");
  CHECK(indiceSuivant("1") == "1A");
  ProjetElectrique p = nouveauProjetElectrique("Essai");
  p.reglages.auteur = "E. G.";
  const RevisionProjet a = nouvelleRevision(p, "2026-10-13", "Première diffusion");
  CHECK(a.indice == (p.schema.cartouche.revision.empty() ? "A" : a.indice));
  CHECK(a.auteur == "E. G.");
  const RevisionProjet b = nouvelleRevision(p, "2026-10-20", "Ajout du variateur");
  CHECK(b.indice == indiceSuivant(a.indice));
  CHECK(revisionCourante(p) == b.indice);
  CHECK(p.schema.cartouche.revision == b.indice);
  CHECK(p.schema.cartouche.date == "2026-10-20");
  const ProjetElectrique r = projetElectriqueDepuisJson(versJson(p));
  REQUIRE(r.revisions.size() == 2);
  CHECK(r.revisions[1].description == "Ajout du variateur");
  CHECK(r.revisions[0].indice == a.indice);
}
