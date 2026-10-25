// Prométhée : essais du lot L9 (performances, robustesse de la lecture). Licence GPL-3.0-only.
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "promethee/controles_fabrication.hpp"
#include "promethee/electronique.hpp"
#include "promethee/remplissage_local.hpp"
#include "promethee/robustesse.hpp"

using namespace prom::ecad;

namespace {
using H = std::chrono::steady_clock;
double ms(H::time_point t0) { return std::chrono::duration<double, std::milli>(H::now() - t0).count(); }
}  // namespace

TEST_CASE("9.7 · Fichier aux UUID doublés : ouvert avec un rapport, sans planter", "[L9][robustesse]") {
  ConceptionElec c = exempleElectronique();
  REQUIRE(c.composants.size() >= 2);
  REQUIRE(c.circuit.pistes.size() >= 2);
  // Un composant copié avec son symbole et son empreinte, sans changer d'identifiant ; une piste et un fil doublés
  const ComposantElec copie = c.composants.front();
  c.composants.push_back(copie);
  for (auto& f : c.feuilles) {
    const auto it = std::find_if(f.symboles.begin(), f.symboles.end(), [&](const SymbolePlace& s) { return s.composant == copie.uuid; });
    if (it != f.symboles.end()) {
      SymbolePlace s = *it;
      s.position = s.position + ptMm(50, 0);
      f.symboles.push_back(s);
      break;
    }
  }
  for (const auto& e : std::vector<EmpreintePlacee>(c.circuit.empreintes))
    if (e.composant == copie.uuid) {
      EmpreintePlacee d = e;
      d.position = d.position + ptMm(0, 8);
      c.circuit.empreintes.push_back(d);
    }
  c.circuit.pistes[1].uuid = c.circuit.pistes[0].uuid;
  if (c.feuilles[0].fils.size() >= 2) c.feuilles[0].fils[1].uuid = c.feuilles[0].fils[0].uuid;
  // Une empreinte posée deux fois pour un autre composant
  c.circuit.empreintes.push_back(c.circuit.empreintes.back());
  std::vector<std::string> alertes;
  ConceptionElec lu = conceptionDepuisJson(versJson(c), &alertes);
  INFO((alertes.empty() ? std::string() : alertes.front()));
  CHECK(alertes.size() >= 3);
  // Tous les identifiants sont uniques ; chaque composant a au plus une empreinte
  std::set<std::string> uuids;
  for (const auto& x : lu.composants) CHECK(uuids.insert(x.uuid).second);
  std::set<std::string> pistes, symboles, empreintes;
  for (const auto& p : lu.circuit.pistes) CHECK(pistes.insert(p.uuid).second);
  for (const auto& f : lu.feuilles)
    for (const auto& s : f.symboles) CHECK(symboles.insert(s.uuid).second);
  for (const auto& e : lu.circuit.empreintes) CHECK(empreintes.insert(e.composant).second);
  // La copie a retrouvé son symbole et son empreinte
  const ComposantElec* dp = nullptr;
  for (const auto& x : lu.composants)
    if (x.reference == copie.reference && x.uuid != copie.uuid) dp = &x;
  REQUIRE(dp);
  const ComposantElec& d = *dp;
  bool sym = false, emp = false;
  for (const auto& f : lu.feuilles)
    for (const auto& s : f.symboles) sym |= s.composant == d.uuid;
  for (const auto& e : lu.circuit.empreintes) emp |= e.composant == d.uuid;
  CHECK(sym);
  CHECK(emp);
  // Tout se calcule
  const Connectivite k = connectivite(lu);
  const Remplissages z = remplirZones(lu, k);
  CHECK_NOTHROW(verifierDRCReglee(lu, k, z));
  CHECK_NOTHROW(verifierERC(lu, k));
  // Sans doublon : rien de changé, rien de dit
  std::vector<std::string> rien;
  const ConceptionElec propre = exempleElectronique();
  const auto j1 = versJson(conceptionDepuisJson(versJson(propre), &rien));
  CHECK(versJson(conceptionDepuisJson(j1, &rien)) == j1);
  CHECK(rien.empty());
}

TEST_CASE("9.3 · Connectivité linéaire : deux fois plus de composants, environ deux fois plus de temps", "[L9][performance]") {
  const ConceptionElec a = carteDeMesure(500), b = carteDeMesure(1000);
  auto temps = [](const ConceptionElec& c) {
    double meilleur = 1e9;
    for (int i = 0; i < 3; ++i) {
      const auto t0 = H::now();
      const Connectivite k = connectivite(c);
      meilleur = std::min(meilleur, ms(t0));
      CHECK(!k.reseaux.empty());
    }
    return meilleur;
  };
  const double ta = temps(a), tb = temps(b);
  WARN("connectivité : " << ta << " ms à 500 composants, " << tb << " ms à 1 000");
  CHECK(tb < 3.0 * ta + 20);
}

TEST_CASE("9.2 · DRC complète de la carte de 2 000 composants avec sa zone GND sous 10 s", "[L9][performance]") {
  const ConceptionElec c = carteDeMesure(2000);
  auto t0 = H::now();
  const Connectivite k = connectivite(c);
  const double tk = ms(t0);
  t0 = H::now();
  const Remplissages z = remplirZones(c, k);
  const double tz = ms(t0);
  t0 = H::now();
  const auto v = verifierDRCReglee(c, k, z);
  const double td = ms(t0);
  t0 = H::now();
  const EtatRoutage e = etatRoutage(c, k, z);
  const double te = ms(t0);
  WARN("2 000 composants : connectivité " << tk << " ms, zones " << tz << " ms, DRC " << td << " ms, état du routage " << te << " ms ; " << v.size() << " violation(s), "
                                          << e.chevelu.size() << " liaison(s)");
  CHECK(tk + tz + td < 10'000);
  // Contrôles de fabrication (8.17) sur la connectivité et les zones déjà calculées : ils tiennent sur le fil du calcul
  t0 = H::now();
  const BilanFabrication f = controlerFabrication(c, k, z);
  const double tf = ms(t0);
  WARN("contrôles de fabrication : " << tf << " ms, " << f.constats.size() << " constat(s)");
  CHECK(tf < 5'000);
}

// Profilage à la main : PROMETHEE_PHASE=connectivite|zones|drc|etat, PROMETHEE_COMPOSANTS=2000 (caché)
TEST_CASE("Mesure d'une phase pour le profilage", "[.mesure]") {
  const char* ph = std::getenv("PROMETHEE_PHASE");
  const char* n = std::getenv("PROMETHEE_COMPOSANTS");
  const std::string phase = ph ? ph : "drc";
  const ConceptionElec c = carteDeMesure(n ? std::atoi(n) : 2000);
  const Connectivite k = connectivite(c);
  const Remplissages z = phase == "connectivite" || phase == "zones" ? Remplissages{} : remplirZones(c, k);
  const auto t0 = H::now();
  for (int i = 0; i < 3; ++i) {
    if (phase == "connectivite") CHECK(!connectivite(c).reseaux.empty());
    else if (phase == "zones") CHECK(!remplirZones(c, k).empty());
    else if (phase == "etat") CHECK(etatRoutage(c, k, z).reseauxTotal > 0);
    else if (phase == "erc") CHECK_NOTHROW(verifierERC(c, k, NiveauErc::Complet));
    else if (phase == "pastilles") CHECK(!pastilles(c, k).empty());
    else CHECK_NOTHROW(verifierDRCReglee(c, k, z));
  }
  WARN(phase << " : " << ms(t0) / 3 << " ms");
}

TEST_CASE("9.9 · Remplissage local : la zone recalculée dans une fenêtre égale le remplissage complet", "[L9][zones]") {
  ConceptionElec c = exempleElectronique();
  REQUIRE(!c.circuit.zones.empty());
  const Zone& z = c.circuit.zones.front();
  Piste t{nouvelUuid(), "+5V", z.couche, ptMm(6, 6), ptMm(14, 6), 300'000};
  c.circuit.pistes.push_back(t);
  const Connectivite k = connectivite(c);
  const Remplissages avant = remplirZones(c, k);
  const RemplissageLocal local(c, k, avant, z.uuid, {t.uuid});
  REQUIRE(local.valide());
  // La piste glissée de 1,5 mm vers le bas
  Piste t2 = t;
  t2.a = t.a + ptMm(0, 1.5);
  t2.b = t.b + ptMm(0, 1.5);
  c.circuit.pistes.back() = t2;
  const auto t0 = H::now();
  Boite fenetre;
  for (const Pt p : {t.a, t.b, t2.a, t2.b}) fenetre.etendre(p);
  fenetre = fenetre.gonflee(t.largeur / 2 + local.portee());
  const Polygones l = local.dans(fenetre, {t2}, {});
  const double duree = ms(t0);
  const Polygones g = remplirZones(c, connectivite(c)).at(z.uuid);
  const Polygone cadre{{fenetre.x0, fenetre.y0}, {fenetre.x1, fenetre.y0}, {fenetre.x1, fenetre.y1}, {fenetre.x0, fenetre.y1}};
  const Polygones gw = intersecter(g, {cadre});
  const double ecart = aireMm2(soustraire(l, gw)) + aireMm2(soustraire(gw, l));
  WARN("remplissage local : " << duree << " ms, écart " << ecart << " mm² sur " << aireMm2(gw) << " mm²");
  CHECK(aireMm2(gw) > 1);
  CHECK(ecart < 0.01 * aireMm2(gw) + 0.01);
  CHECK(duree < 50);
}
