// Prométhée : essais du lot L16 (fabrication à l'atelier). Licence GPL-3.0-only.
#include <algorithm>
#include <sstream>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electronique.hpp"
#include "promethee/usinage.hpp"

using namespace prom::ecad;

namespace {
int compter(const std::string& t, const std::string& motif) {
  int n = 0;
  for (std::size_t i = t.find(motif); i != std::string::npos; i = t.find(motif, i + 1)) ++n;
  return n;
}
}  // namespace

TEST_CASE("16.7 · Gravure à l'anglaise : sillons qui isolent chaque réseau sans toucher au cuivre", "[L16][usinage]") {
  const ConceptionElec c = exempleElectronique();
  OptionsUsinage o;
  o.diametreOutil = 0.2;
  o.passes = 2;
  for (const Couche couche : {Couche::CuivreDessus, Couche::CuivreDessous}) {
    const ResultatIsolement r = sillonsIsolement(c, couche, o);
    REQUIRE(!r.sillons.empty());
    CHECK(r.longueur > 0);
    // Aucun point de sillon à moins du rayon de l'outil d'un cuivre (2 µm de tolérance de calcul)
    Polygones tout;
    for (const auto& [n, p] : cuivreParReseau(c, couche)) tout.insert(tout.end(), p.begin(), p.end());
    const Polygones interdit = gonfler(unir(tout), nm(o.diametreOutil / 2) - 3'000);
    int coupes = 0;
    for (const auto& s : r.sillons)
      for (const Pt& p : s.points) coupes += dedans(interdit, p);
    CHECK(coupes == 0);
    // Chaque réseau a son premier sillon tout autour
    std::set<std::string> isoles;
    for (const auto& s : r.sillons)
      if (s.passe == 0) isoles.insert(s.reseau);
    CHECK(isoles.size() == cuivreParReseau(c, couche).size());
  }
  const ResultatIsolement dessus = sillonsIsolement(c, Couche::CuivreDessus, o);
  const std::string g = gcodeIsolement(c, dessus, o);
  CHECK(g.find("G21") != std::string::npos);
  CHECK(compter(g, "G1 Z-0.080") == static_cast<int>(dessus.sillons.size()));
  CHECK(g.find("M2") != std::string::npos);
  CHECK(dureeGcode(g) > dessus.longueur / o.avance);
  // Outil trop large pour les écarts de la carte : signalé
  OptionsUsinage gros = o;
  gros.diametreOutil = 1.0;
  CHECK(!sillonsIsolement(c, Couche::CuivreDessus, gros).alertes.empty());
  // Dessous en miroir : X retourné
  OptionsUsinage miroir = o;
  miroir.miroir = true;
  CHECK(gcodeIsolement(c, dessus, miroir) != g);
}

TEST_CASE("16.7, 16.9 · Perçages, découpe avec attaches, pochoir de pâte", "[L16][usinage]") {
  const ConceptionElec c = exempleElectronique();
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  const std::string p = gcodePercages(c);
  CHECK(compter(p, "G1 Z-1.800") == static_cast<int>(m.trous.size()));
  CHECK(compter(p, "M0 (changer le foret") >= 1);
  const std::string d = gcodeDecoupe(c);
  CHECK(compter(d, "G1 Z-0.720") == 4);  // quatre attaches remontées à 40 % de la profondeur
  CHECK(dureeGcode(d) > 0);
  // Pochoir : une ouverture par pastille CMS du dessus, réduite
  int cms = 0;
  for (const auto& x : pastilles(c, k)) cms += x.pastille.type == TypePastille::CMS && !x.dessous;
  const Polygones o0 = ouverturesPochoir(c, false, 0), o1 = ouverturesPochoir(c, false, 0.05);
  CHECK(static_cast<int>(o0.size()) == cms);
  CHECK(aireMm2(o1) < aireMm2(o0));
  const std::string svg = pochoirSvg(c, false);
  CHECK(svg.find("<svg") != std::string::npos);
  CHECK(compter(svg, "Z ") == cms);
  const std::string dxf = pochoirDxf(c, false);
  CHECK(compter(dxf, "POLYLINE") == cms + 1);
}
