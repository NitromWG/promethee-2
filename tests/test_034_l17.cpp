// Prométhée : essais du lot L17 de la 0.34 (schéma déduit d'une carte sans schéma, calque des retouches d'une
// révision fabriquée). Licence GPL-3.0-only.
#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "promethee/electronique.hpp"
#include "promethee/schema_deduit.hpp"

using namespace prom;
using namespace prom::ecad;

namespace {
using Broches = std::set<std::pair<std::string, std::string>>;  // (repère, broche)
std::vector<std::pair<std::string, Broches>> reseauxMultiples(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  std::vector<std::pair<std::string, Broches>> r;
  for (const auto& n : k.reseaux) {
    Broches b;
    for (const auto& x : n.broches)
      if (const ComposantElec* comp = c.composant(x.composant)) b.insert({comp->reference, x.broche});
    if (b.size() >= 2) r.emplace_back(n.nom, b);
  }
  std::sort(r.begin(), r.end());
  return r;
}
// La carte telle qu'elle sort de fabrication, sans son schéma : cuivre des zones figé, feuilles vidées, composants
// posés sur la carte seule, aucun réseau noté hors du cuivre
ConceptionElec priveeDeSchema(const ConceptionElec& original) {
  ConceptionElec c = original;
  const Connectivite k = connectivite(original);
  const Remplissages r = remplirZones(original, k);
  for (auto& z : c.circuit.zones)
    if (const auto it = r.find(z.uuid); it != r.end()) {
      z.etat = EtatZone::Figee;
      z.remplissageFige = it->second;
    }
  for (auto& f : c.feuilles) {
    f.symboles.clear();
    f.fils.clear();
    f.etiquettes.clear();
    f.jonctions.clear();
    f.nonConnexions.clear();
    f.textes.clear();
  }
  c.nomsReseaux.clear();
  for (auto& x : c.composants) x.carteSeule = true;
  return c;
}
}  // namespace

TEST_CASE("17.6 · circuit de l'exemple au NE555 privé de son schéma : schéma déduit aux mêmes réseaux que l'original, réseau par réseau", "[034][L17][17.6]") {
  const ConceptionElec original = exempleElectronique();
  const auto attendus = reseauxMultiples(original);
  REQUIRE(attendus.size() >= 6);
  ConceptionElec carte = priveeDeSchema(original);
  CHECK(reseauxMultiples(carte).empty());  // plus de schéma : plus de réseau au schéma
  // Le cuivre seul donne les réseaux de chaque pastille
  const auto cuivre = reseauxDuCuivre(carte);
  CHECK(!cuivre.empty());
  const BilanSchemaDeduit b = schemaDepuisCircuit(carte);
  for (const auto& a : b.alertes) UNSCOPED_INFO(a);
  CHECK(b.symboles >= 8);
  CHECK(b.etiquettes > 0);
  // Réseau par réseau : mêmes broches, même nom
  const auto deduits = reseauxMultiples(carte);
  for (const auto& [nom, broches] : attendus) {
    INFO("réseau " << nom);
    const auto it = std::find_if(deduits.begin(), deduits.end(), [&](const auto& x) { return x.second == broches; });
    REQUIRE(it != deduits.end());
    CHECK(it->first == nom);
  }
  CHECK(deduits.size() == attendus.size());
  // Les composants ont quitté la carte seule, chacun posé une fois
  for (const auto& x : carte.composants) CHECK_FALSE((x.carteSeule && x.symbole.rfind("carte:", 0) == 0));
  // La carte routée n'a pas plus à router qu'avant
  const Connectivite k0 = connectivite(original), k1 = connectivite(carte);
  CHECK(etatRoutage(carte, k1, remplirZones(carte, k1)).chevelu.size() == etatRoutage(original, k0, remplirZones(original, k0)).chevelu.size());
}

TEST_CASE("17.6 · retouches d'une révision fabriquée : calque par numéro de série, schéma mis à jour, proposées à la révision suivante", "[034][L17][17.6]") {
  ConceptionElec fab = exempleElectronique();
  fab.cartouche.revision = "A";
  // Une piste dont la coupure sépare deux broches de son réseau
  std::string coupee, reseauCoupe;
  for (const auto& t : fab.circuit.pistes) {
    ConceptionElec essai = fab;
    std::erase_if(essai.circuit.pistes, [&](const Piste& p) { return p.uuid == t.uuid; });
    const auto avant = reseauxDuCuivre(fab), apres = reseauxDuCuivre(essai);
    std::set<std::string> nomsAvant, nomsApres;
    for (const auto& [b, n] : avant)
      if (n == t.reseau) nomsApres.insert(apres.count(b) ? apres.at(b) : std::string());
    if (nomsApres.size() > 1) {
      coupee = t.uuid;
      reseauCoupe = t.reseau;
      break;
    }
  }
  REQUIRE(!coupee.empty());
  Retouche valeur;
  valeur.genre = GenreRetouche::ComposantChange;
  valeur.composant = "R3";
  valeur.valeur = "470";
  valeur.numerosSerie = {"SN002"};
  Retouche fil;
  fil.genre = GenreRetouche::FilDeReprise;
  fil.a = "C2.1";
  fil.b = "R3.2";
  fil.description = "fil de reprise de C2 à R3";
  Retouche coupure;
  coupure.genre = GenreRetouche::PisteCoupee;
  coupure.piste = coupee;
  coupure.numerosSerie = {"SN001", "SN002"};
  const ConceptionElec base = fab;  // la révision suivante part de la révision fabriquée
  for (const auto& r : {valeur, fil, coupure}) ajouterRetouche(fab, r);
  REQUIRE(retouchesDe(fab).size() == 3);
  CHECK(retouchesDe(fab)[0].revision == "A");
  // Liste par carte
  CHECK(retouchesDeCarte(fab, "SN002").size() == 3);
  CHECK(retouchesDeCarte(fab, "SN001").size() == 2);
  CHECK(listeRetouches(fab, "SN001").find("piste coupée") != std::string::npos);
  // La révision suivante reçoit les retouches en modifications proposées, acceptées une à une
  ConceptionElec suivante = base;
  suivante.cartouche.revision = "B";
  auto proposees = retouchesProposees(fab, suivante);
  REQUIRE(proposees.size() == 3);
  std::string e;
  REQUIRE(accepterRetouche(suivante, proposees[0], &e));
  CHECK(suivante.parReference("R3")->valeur == "470");
  CHECK(retouchesProposees(fab, suivante).size() == 2);
  REQUIRE(accepterRetouche(suivante, proposees[1], &e));
  {
    const Connectivite k = connectivite(suivante);
    CHECK(k.reseau(suivante.parReference("C2")->uuid, "1") == k.reseau(suivante.parReference("R3")->uuid, "2"));
  }
  INFO(e);
  REQUIRE(accepterRetouche(suivante, proposees[2], &e));
  CHECK(retouchesProposees(fab, suivante).empty());
  CHECK(std::none_of(suivante.circuit.pistes.begin(), suivante.circuit.pistes.end(), [&](const Piste& p) { return p.uuid == coupee; }));
  // Le schéma de la révision fabriquée mis à jour par la même commande
  ConceptionElec corrigee = fab;
  CHECK(appliquerRetouches(corrigee) == 3);
  CHECK(appliquerRetouches(corrigee) == 0);
  CHECK(corrigee.parReference("R3")->valeur == "470");
  // La coupure a séparé le réseau au schéma : un réseau « …_COUPE… » est apparu
  bool separe = false;
  for (const auto& r : connectivite(corrigee).reseaux) separe |= r.nom.find("_COUPE") != std::string::npos;
  CHECK(separe);
}
