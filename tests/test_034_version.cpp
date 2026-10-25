// Prométhée : version 0.34, format électronique 6 et fichier écrit pour la 0.33 (ECH-10, ECH-11).
// Licence GPL-3.0-only.
#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "promethee/cablage.hpp"
#include "promethee/electrique.hpp"
#include "promethee/electronique.hpp"
#include "promethee/migrations.hpp"
#include "promethee/modele.hpp"

using namespace prom;
using namespace prom::ecad;

namespace {
bool dit(const std::vector<std::string>& pertes, const std::string& texte) {
  return std::any_of(pertes.begin(), pertes.end(), [&](const std::string& p) { return p.find(texte) != std::string::npos; });
}
}  // namespace

TEST_CASE("Format 6 (0.34) : un projet écrit pour la 0.33 garde ce qu'elle ne connaît pas, retire la seconde vue des symboles et dit le reste", "[version][ECH-11]") {
  CHECK(FORMAT_ELECTRONIQUE == 6);
  CHECK(versionFichierCourante() == "0.34");
  REQUIRE(!versionsAnterieures().empty());
  CHECK(versionsAnterieures().front() == "0.33");

  // Un projet qui porte ce que la 0.34 ajoute, posé dans son fichier puis relu par la 0.34
  Projet p0 = projetVide();
  p0.electronique.push_back(exempleElectronique());
  elec::ProjetElectrique pe = elec::nouveauProjetElectrique("Machine");
  const std::size_t i = elec::ajouterFolio(pe, elec::TypeFolio::Cablage, "Faisceau W1");
  pe.folios[pe.schema.feuilles[i].uuid].faisceau = "W1";
  elec::Cablage h;
  h.nom = "W1";
  elec::ecrireCablages(pe, {h});
  p0.electrique.push_back(pe);
  Json j = versJson(p0);
  Json& c = j["electronique"][0];
  CHECK(c["format"] == 6);
  REQUIRE(!c["feuilles"].empty());
  REQUIRE(c["symboles"].size() >= 2);
  Json& f = c["feuilles"][0];
  f["blocs"] = Json::array({{{"uuid", "bloc-alim"}, {"nom", "Alim"}, {"feuille", "feuille-alim"}, {"p", {0, 0}}, {"largeur", 10'160'000}, {"hauteur", 5'080'000},
                             {"ports", Json::array({{{"uuid", "port-vcc"}, {"nom", "VCC"}, {"p", {0, 2'540'000}}, {"cote", 0}}})}}});
  f["etiquettes"].push_back({{"uuid", "etiquette-h"}, {"texte", "VCC"}, {"p", {2'540'000, 2'540'000}}, {"rot", 0}, {"globale", false}, {"hierarchique", true}});
  // Symbole à deux vues : une broche et un trait de la seconde
  Json& s = c["symboles"][0];
  REQUIRE(!s["broches"].empty());
  s["vues"] = 2;
  Json b2 = s["broches"][0];
  b2["vue"] = 2;
  s["broches"].push_back(b2);
  s["traces"].push_back({{"forme", 0}, {"points", Json::array({{0, 0}, {2'540'000, 0}})}, {"vue", 2}});
  const std::size_t brochesAvant = s["broches"].size(), tracesAvant = s["traces"].size();
  c["symboles"][1]["broches"][0]["cachee"] = true;
  c["articles"] = Json::array({{{"id", "ART-LIBRE"}, {"nom", "Carte libre"}, {"licence", "CERN-OHL-S-2.0"}}});
  Json& ci = c["circuit"];
  ci["vias"].push_back({{"uuid", "via-borgne"}, {"reseau", "GND"}, {"p", {1'000'000, 1'000'000}}, {"diametre", 600'000}, {"percage", 300'000}, {"typeVia", "micro"}, {"de", "F.Cu"}, {"vers", "B.Cu"}});
  ci["pistes"].push_back({{"uuid", "piste-arc"}, {"reseau", "GND"}, {"couche", "F.Cu"}, {"a", {0, 0}}, {"b", {2'000'000, 0}}, {"largeur", 250'000}, {"milieu", {1'000'000, 500'000}}});
  ci["regions"] = Json::array({{{"uuid", "region-goutte"}, {"reseau", "GND"}, {"couche", "F.Cu"}, {"forme", Json::array({{0, 0}, {1'000'000, 0}, {1'000'000, 1'000'000}})}}});
  c["pointsDeVue"] = Json::array({{{"nom", "Face"}, {"cible", {0, 0, 0}}, {"azimut", 0}, {"elevation", 30}, {"distance", 100}, {"orthographique", false}, {"champ", 45}}});
  c["extensions"] = {{"platine", {{"rangees", 30}}}};
  j["licence"] = "CERN-OHL-S-2.0";
  j["liensDisciplines"] = Json::array({{{"id", "L1"}, {"de", "carte"}, {"vers", "boitier"}, {"quoi", "contour"}, {"empreinte", "a1"}, {"releve", "a1"}, {"etat", "à jour"}}});
  const Projet p = normaliser(j);
  REQUIRE(p.electronique.size() == 1);
  REQUIRE(p.electrique.size() == 1);

  const FichierAnterieur f33 = pourVersion(p, "0.33");
  std::string toutes;
  for (const auto& x : f33.pertes) toutes += x + "\n";
  INFO(toutes);
  CHECK(dit(f33.pertes, "bloc hiérarchique"));
  CHECK(dit(f33.pertes, "étiquette hiérarchique en locale"));
  CHECK(dit(f33.pertes, "seconde vue (De Morgan)"));
  CHECK(dit(f33.pertes, "broche cachée"));
  CHECK(dit(f33.pertes, "licence de 1 article"));
  CHECK(dit(f33.pertes, "via borgne, enterrée ou micro"));
  CHECK(dit(f33.pertes, "piste en arc"));
  CHECK(dit(f33.pertes, "région de cuivre plein"));
  CHECK(dit(f33.pertes, "point de vue 3D"));
  CHECK(dit(f33.pertes, "platine d'essai"));
  CHECK(dit(f33.pertes, "licence du projet"));
  CHECK(dit(f33.pertes, "lien entre disciplines"));
  CHECK(dit(f33.pertes, "folio de câblage de faisceau"));
  CHECK(dit(f33.pertes, "1 faisceau"));
  // Format 5, seconde vue retirée (une broche et un trait de moins), le reste gardé tel quel pour la 0.33
  const Json& c33 = f33.projet["electronique"][0];
  CHECK(c33["format"] == 5);
  CHECK(c33["formatElectronique"] == 5);
  CHECK(c33["symboles"][0]["broches"].size() == brochesAvant - 1);
  CHECK(c33["symboles"][0]["traces"].size() == tracesAvant - 1);
  CHECK(c33.contains("pointsDeVue"));
  CHECK(c33["circuit"].contains("regions"));
  const Json& pe33 = f33.projet["electrique"][0];
  CHECK(pe33["schema"]["format"] == 5);
  bool libre = false;
  for (auto it = pe33["folios"].begin(); it != pe33["folios"].end(); ++it) libre |= (*it)["type"] == "libre" && !it->contains("faisceau");
  CHECK(libre);
  CHECK(pe33["schema"]["extensions"].contains("cablage"));
  // Relu par la 0.34, le fichier écrit pour la 0.33 remonte au format 6 avec ce que la 0.33 a gardé
  const Projet relu = normaliser(f33.projet);
  REQUIRE(relu.electronique.size() == 1);
  CHECK(relu.electronique[0].pointsDeVue.size() == 1);
  CHECK(relu.electronique[0].circuit.regions.size() == 1);
  CHECK(versJson(relu)["electronique"][0]["format"] == 6);
  // Plus bas, chaque étape garde sa descente : la 0.32 reçoit le format 4
  const FichierAnterieur f32 = pourVersion(p, "0.32");
  CHECK(f32.projet["electronique"][0]["format"] == 4);
  CHECK(dit(f32.pertes, "bloc hiérarchique"));
}
