// Prométhée : essais des appareils électriques personnels (symbole dessiné d'après les bornes, pose et repère,
// contacts rattachés, nomenclature, armoire, bilan thermique, modification, fichier). Licence GPL-3.0-only.
#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electrique.hpp"
#include "promethee/electrique_fabrication.hpp"

using namespace prom::elec;
using Catch::Approx;
namespace ecad = prom::ecad;

namespace {

SpecAppareil relaisSecurite() {
  SpecAppareil a;
  a.nom = "Relais de sécurité";
  a.prefixe = "K";
  a.marquage = "SÉCU";
  a.fabricant = "Pilz";
  a.reference = "PNOZ s3";
  a.haut = {"A1", "S11", "S12", "13", "23"};
  a.bas = {"A2", "S21", "S22", "14", "24"};
  a.largeur = 22.5;
  a.hauteur = 100;
  a.profondeur = 120;
  a.pertes = 2.5;
  a.prix = 189.9;
  return a;
}

bool surLaGrille(ecad::Pt q) { return q.x % 1'270'000 == 0 && q.y % 1'270'000 == 0; }

const ecad::SymbolePlace* pose(const ProjetElectrique& p, const std::string& uuid) {
  for (const auto& f : p.schema.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == uuid) return &s;
  return nullptr;
}

std::vector<std::string> numeros(const ProjetElectrique& p, const std::string& uuid) {
  std::vector<std::string> v;
  const ecad::SymbolePlace* s = pose(p, uuid);
  if (!s) return v;
  for (const auto& b : ecad::brochesPlacees(*s, *p.schema.symbole(s->symbole))) v.push_back(b.numero);
  return v;
}

std::size_t folioDeType(const ProjetElectrique& p, TypeFolio t) {
  for (std::size_t i = 0; i < p.schema.feuilles.size(); ++i)
    if (p.folio(p.schema.feuilles[i].uuid).type == t) return i;
  return 0;
}

}  // namespace

TEST_CASE("Appareils personnels : symbole dessiné d'après les bornes") {
  const ecad::DefSymbole d = symboleAppareil(relaisSecurite());
  CHECK(d.id == "perso:Relais-de-securite");
  CHECK(estAppareilPersonnel(d.id));
  CHECK(!estAppareilPersonnel("iec:bobine-km"));
  CHECK(d.prefixe == "K");
  CHECK(d.categorie == "Mes appareils");
  CHECK(d.fabricant == "Pilz");
  CHECK(d.mpn == "PNOZ s3");
  REQUIRE(d.broches.size() == 10);
  // Cadre et marquage
  const auto cadre = std::find_if(d.traces.begin(), d.traces.end(), [](const ecad::Trace& t) { return t.forme == ecad::Trace::Forme::Rectangle; });
  REQUIRE(cadre != d.traces.end());
  const ecad::Nm w = cadre->points[1].x, h = cadre->points[1].y;
  CHECK(cadre->points[0].x == -w);
  CHECK(cadre->points[0].y == -h);
  CHECK(std::any_of(d.traces.begin(), d.traces.end(), [](const ecad::Trace& t) { return t.forme == ecad::Trace::Forme::Texte && t.texte == "SÉCU"; }));
  // Bornes : toutes sur la grille, hors du cadre, au pas de 5,08 mm, dans l'ordre donné, centrées
  for (const auto& b : d.broches) {
    INFO(b.numero);
    CHECK(surLaGrille(b.extremite));
    CHECK(b.type == ecad::TypeBroche::Passive);
    CHECK(b.numeroVisible);
    if (b.direction == 1) CHECK(b.extremite.y == -h - 2'540'000);
    if (b.direction == 3) CHECK(b.extremite.y == h + 2'540'000);
  }
  CHECK(d.broches[0].numero == "A1");
  CHECK(d.broches[0].direction == 1);
  CHECK(d.broches[1].extremite.x - d.broches[0].extremite.x == 5'080'000);
  CHECK(d.broches[0].extremite.x == -d.broches[4].extremite.x);
  CHECK(d.broches[5].numero == "A2");
  CHECK(d.broches[5].direction == 3);
  CHECK(w >= 2 * 5'080'000);  // cinq bornes au pas de 5,08 tiennent dans la largeur
  // Repère et valeur à droite : aucune borne n'y sort
  CHECK(d.posReference.x > w);
  CHECK(d.posValeur.x > w);
  // Relu tel quel pour être modifié
  const SpecAppareil a = specAppareil(d);
  CHECK(a.id == d.id);
  CHECK(a.nom == "Relais de sécurité");
  CHECK(a.marquage == "SÉCU");
  CHECK(a.haut == relaisSecurite().haut);
  CHECK(a.bas == relaisSecurite().bas);
  CHECK(a.gauche.empty());
  CHECK(a.droite.empty());
  CHECK(a.largeur == Approx(22.5));
  CHECK(a.hauteur == Approx(100));
  CHECK(a.profondeur == Approx(120));
  CHECK(a.montage == MontageAppareil::Rail);
  CHECK(a.pertes == Approx(2.5));
  CHECK(a.prix == Approx(189.9));
  CHECK(a.fabricant == "Pilz");
  CHECK(a.reference == "PNOZ s3");

  // Bornes sur les quatre côtés : repère et valeur au-dessus du coin droit
  SpecAppareil v;
  v.nom = "Variateur de vitesse";
  v.prefixe = "tA";  // capitales d'office
  v.haut = {"DI1", "DI2", "DI3", "+24V"};
  v.gauche = {"L1", "L2", "L3"};
  v.droite = {"U", "V", "W"};
  v.bas = {"PE"};
  CHECK(defautsAppareil(v).empty());
  const ecad::DefSymbole dv = symboleAppareil(v);
  CHECK(dv.prefixe == "TA");
  for (const auto& b : dv.broches) CHECK(surLaGrille(b.extremite));
  const auto cadreV = std::find_if(dv.traces.begin(), dv.traces.end(), [](const ecad::Trace& t) { return t.forme == ecad::Trace::Forme::Rectangle; });
  REQUIRE(cadreV != dv.traces.end());
  CHECK(dv.posReference.y < -cadreV->points[1].y);
  CHECK(specAppareil(dv).gauche == v.gauche);
  CHECK(specAppareil(dv).droite == v.droite);
  CHECK(specAppareil(dv).pertes < 0);  // inconnues

  // Saisie des bornes et défauts
  CHECK(lireBornes("A1 A2, 13;14\t+24V") == std::vector<std::string>{"A1", "A2", "13", "14", "+24V"});
  CHECK(texteBornes({"A1", "A2", "13"}) == "A1 A2 13");
  CHECK(defautsAppareil(relaisSecurite()).empty());
  SpecAppareil x = relaisSecurite();
  x.bas.push_back("A1");
  CHECK(defautsAppareil(x).size() == 1);
  x = relaisSecurite();
  x.nom = "  ";
  x.prefixe = "K1";
  CHECK(defautsAppareil(x).size() == 2);
  x = SpecAppareil{};
  x.nom = "Vide";
  CHECK(defautsAppareil(x).size() == 1);  // aucune borne
}

TEST_CASE("Appareils personnels : pose, contacts, nomenclature, armoire et pertes") {
  ProjetElectrique p = exempleDemarrageMoteur();
  if (p.armoires.empty()) p.armoires.push_back(Armoire{});
  const std::string id = enregistrerAppareil(p, relaisSecurite());
  CHECK(id == "perso:Relais-de-securite");
  REQUIRE(appareilsPersonnels(p).size() == 1);
  CHECK(identifiantAppareil(p, "Relais de sécurité") == "perso:Relais-de-securite-2");
  CHECK(identifiantAppareil(p, "Relais de sécurité", {"perso:Relais-de-securite-2"}) == "perso:Relais-de-securite-3");
  CHECK(roleSymbole(id) == RoleSymbole::Principal);
  // Pose : sa lettre, sa désignation, son fabricant et sa référence
  const std::size_t commande = folioDeType(p, TypeFolio::Commande);
  const std::string s = poserAppareil(p, commande, id, ecad::ptMm(254, 101.6));
  REQUIRE(pose(p, s));
  const std::string k = pose(p, s)->composant;
  REQUIRE(p.schema.composant(k));
  CHECK(p.schema.composant(k)->reference == "K1");
  CHECK(p.schema.composant(k)->description == "Relais de sécurité");
  CHECK(p.schema.composant(k)->fabricant == "Pilz");
  CHECK(p.schema.composant(k)->mpn == "PNOZ s3");
  // Il porte des contacts posés ailleurs : le rang suivant ceux de ses propres bornes (13-14, 23-24)
  const auto porteurs = appareilsPour(p, RoleSymbole::ContactNO);
  CHECK(std::find(porteurs.begin(), porteurs.end(), k) != porteurs.end());
  const std::string c = poserElement(p, commande, "iec:contact-no", ecad::ptMm(304.8, 101.6), 0, k);
  CHECK(numeros(p, c) == std::vector<std::string>{"33", "34"});
  // Nomenclature : prix de sa définition
  const auto n = nomenclatureElectrique(p);
  const auto l = std::find_if(n.begin(), n.end(), [](const LigneNomenclatureElec& x) { return x.reference == "PNOZ s3"; });
  REQUIRE(l != n.end());
  CHECK(l->designation == "Relais de sécurité");
  CHECK(l->fabricant == "Pilz");
  CHECK(l->prixUnitaire == Approx(189.9));
  CHECK(!l->prixSaisi);
  // Armoire : ses dimensions, sur rail ; bilan thermique : ses pertes
  const Encombrement e = encombrement(p, k);
  CHECK(e.largeur == Approx(22.5));
  CHECK(e.hauteur == Approx(100));
  CHECK(e.profondeur == Approx(120));
  CHECK(e.rail);
  CHECK(!e.porte);
  CHECK(!e.horsArmoire);
  implanterArmoire(p, 0);
  const BilanThermique t = bilanThermique(p, 0);
  const auto perte = std::find_if(t.pertes.begin(), t.pertes.end(), [&](const PerteAppareil& x) { return x.composant == k; });
  REQUIRE(perte != t.pertes.end());
  CHECK(perte->puissance == Approx(2.5));
  CHECK(perte->designation == "Relais de sécurité");
  CHECK(t.sansDonnee == 0);
  // Enregistré, relu : la définition et ses données d'armoire
  const ProjetElectrique r = projetElectriqueDepuisJson(versJson(p));
  const ecad::DefSymbole* d = r.schema.symbole(id);
  REQUIRE(d);
  CHECK(d->proprietes.at("montage") == "rail");
  CHECK(specAppareil(*d).prix == Approx(189.9));
  CHECK(specAppareil(*d).haut == relaisSecurite().haut);
  REQUIRE(r.schema.composant(k));
  CHECK(r.schema.composant(k)->reference == "K1");

  // Appareil de porte sans dimensions : gabarit d'un appareil de façade ; pertes inconnues : comptées à part
  SpecAppareil indicateur;
  indicateur.nom = "Indicateur de niveau";
  indicateur.prefixe = "P";
  indicateur.haut = {"+", "-"};
  indicateur.montage = MontageAppareil::Porte;
  const std::string idP = enregistrerAppareil(p, indicateur);
  const std::string sP = poserAppareil(p, commande, idP, ecad::ptMm(355.6, 101.6));
  const std::string kP = pose(p, sP)->composant;
  CHECK(p.schema.composant(kP)->reference == "P1");
  const Encombrement eP = encombrement(p, kP);
  CHECK(eP.porte);
  CHECK(!eP.rail);
  CHECK(eP.largeur == Approx(30));
  CHECK(nomMontage(MontageAppareil::Porte) == std::string("Sur la porte (façade)"));
  implanterArmoire(p, 0);
  CHECK(bilanThermique(p, 0).sansDonnee == 1);
}

TEST_CASE("Appareils personnels : modifiés, les appareils posés suivent") {
  ProjetElectrique p = nouveauProjetElectrique("Essai");
  const std::string id = enregistrerAppareil(p, relaisSecurite());
  const std::string k1 = pose(p, poserAppareil(p, 0, id, ecad::ptMm(100, 100)))->composant;
  const std::string k2 = pose(p, poserAppareil(p, 0, id, ecad::ptMm(200, 100)))->composant;
  CHECK(p.schema.composant(k1)->reference == "K1");
  CHECK(p.schema.composant(k2)->reference == "K2");
  p.schema.composant(k2)->mpn = "PNOZ X2";  // référence changée à la main : elle reste
  CHECK(p.schema.symbole(id)->version == 1);
  SpecAppareil m = specAppareil(*p.schema.symbole(id));
  m.nom = "Relais de sécurité deux voies";
  m.prefixe = "KF";
  m.reference = "PNOZ s4";
  m.bas.push_back("34");
  CHECK(enregistrerAppareil(p, m) == id);
  const ecad::DefSymbole* d = p.schema.symbole(id);
  REQUIRE(d);
  CHECK(d->version == 2);
  CHECK(d->broches.size() == 11);
  CHECK(appareilsPersonnels(p).size() == 1);
  const ecad::ComposantElec* a = p.schema.composant(k1);
  const ecad::ComposantElec* b = p.schema.composant(k2);
  CHECK(a->reference == "KF1");
  CHECK(b->reference == "KF2");
  CHECK(a->description == "Relais de sécurité deux voies");
  CHECK(a->mpn == "PNOZ s4");
  CHECK(b->mpn == "PNOZ X2");
  CHECK(numeros(p, p.schema.feuilles[0].symboles[0].uuid).size() == 11);
}
