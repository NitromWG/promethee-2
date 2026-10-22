// Prométhée : essais du lot L10 (échanges). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "promethee/echanges_electronique.hpp"
#include "promethee/electronique.hpp"

using namespace prom::ecad;

namespace {
// Les réseaux d'une netlist en partition de broches « R1.2 » (les noms ne comptent pas)
std::set<std::set<std::string>> partition(const NetlistLue& n) {
  std::set<std::set<std::string>> r;
  for (const auto& [nom, noeuds] : n.reseaux) {
    std::set<std::string> s;
    for (const auto& [ref, b] : noeuds) s.insert(ref + "." + b);
    if (s.size() >= 2) r.insert(s);
  }
  return r;
}
std::map<std::string, std::string> parBroche(const NetlistLue& n) {
  std::map<std::string, std::string> r;
  for (const auto& [nom, noeuds] : n.reseaux)
    for (const auto& [ref, b] : noeuds) r[ref + "." + b] = nom;
  return r;
}
// Contrôle léger d'un XML : balises équilibrées
bool xmlEquilibre(const std::string& t) {
  std::vector<std::string> pile;
  for (std::size_t i = t.find('<'); i != std::string::npos; i = t.find('<', i + 1)) {
    if (t.compare(i, 2, "<?") == 0 || t.compare(i, 4, "<!--") == 0) continue;
    const std::size_t f = t.find('>', i);
    if (f == std::string::npos) return false;
    std::string balise = t.substr(i + 1, f - i - 1);
    const bool fermante = !balise.empty() && balise[0] == '/', autonome = !balise.empty() && balise.back() == '/';
    std::string nom = balise.substr(fermante ? 1 : 0);
    nom = nom.substr(0, nom.find_first_of(" /"));
    if (fermante) {
      if (pile.empty() || pile.back() != nom) return false;
      pile.pop_back();
    } else if (!autonome)
      pile.push_back(nom);
  }
  return pile.empty();
}
}  // namespace

TEST_CASE("10.7 · Netlist KiCad lue : conception aux mêmes réseaux ; carte mise à jour depuis une netlist", "[L10][netlist]") {
  const ConceptionElec origine = exempleElectronique();
  const NetlistLue lue = lireNetlist(netlistKicad(origine, connectivite(origine)));
  REQUIRE(lue.erreurs.empty());
  CHECK(lue.composants.size() == netlistDe(origine).composants.size());
  CHECK(partition(lue) == partition(netlistDe(origine)));
  // La liste de Prométhée se relit aussi
  CHECK(partition(lireNetlist(netlistTexte(origine, connectivite(origine)))) == partition(netlistDe(origine)));
  ConceptionElec c = conceptionDepuisNetlist(lue, "Depuis la netlist");
  CHECK(partition(netlistDe(c)) == partition(lue));
  CHECK(c.circuit.empreintes.size() == origine.circuit.empreintes.size());
  for (const auto& x : origine.composants)
    if (const ComposantElec* y = c.parReference(x.reference)) {
      CHECK(y->valeur == x.valeur);
      CHECK(y->empreinte == x.empreinte);
    }
  // Netlist changée : R1 vaut 22k, R99 entre deux réseaux, la broche 1 de R2 passe sur un autre réseau
  NetlistLue n2 = lue;
  for (auto& x : n2.composants)
    if (x.reference == "R1") x.valeur = "22k";
  n2.composants.push_back({"R99", "1k", "R_0603"});
  const auto avant = parBroche(lue);
  const std::string reseauR2 = avant.at("R2.1");
  std::string autre;
  for (const auto& [n, noeuds] : n2.reseaux)
    if (n != reseauR2 && noeuds.size() >= 2 && autre.empty()) autre = n;
  REQUIRE(!autre.empty());
  for (auto& [n, noeuds] : n2.reseaux) std::erase_if(noeuds, [](const auto& x) { return x.first == "R2" && x.second == "1"; });
  n2.reseaux[autre].push_back({"R2", "1"});
  n2.reseaux[autre].push_back({"R99", "1"});
  n2.reseaux[reseauR2].push_back({"R99", "2"});
  const BilanNetlist b = mettreAJourDepuisNetlist(c, n2);
  CHECK(b.ajoutes == 1);
  CHECK(b.valeurs == 1);
  CHECK(b.liaisons >= 1);
  CHECK(b.nonChangees == 0);
  CHECK(partition(netlistDe(c)) == partition(n2));
  CHECK(c.parReference("R1")->valeur == "22k");
  bool r99 = false;
  for (const auto& e : c.circuit.empreintes) r99 |= c.composant(e.composant)->reference == "R99";
  CHECK(r99);
  const std::string csv = rapportNetlistCsv(b);
  CHECK(csv.rfind("\xEF\xBB\xBF", 0) == 0);
  CHECK(csv.find("R99;ajouté") != std::string::npos);
  // Sur un schéma câblé, l'écart est listé, rien n'est arraché
  ConceptionElec cablee = exempleElectronique();
  const BilanNetlist b2 = mettreAJourDepuisNetlist(cablee, n2);
  CHECK(b2.nonChangees >= 1);
}

TEST_CASE("10.8 · DXF de la mécanique de la carte : contour, découpes, perçages, encombrements", "[L10][dxf]") {
  ConceptionElec c = exempleElectronique();
  c.circuit.contour.decoupes.push_back({ptMm(20, 10), ptMm(24, 10), ptMm(24, 14), ptMm(20, 14)});
  const std::string d = dxfMecaniqueCarte(c);
  for (const char* calque : {"CONTOUR", "DECOUPES", "PERCAGES", "ENCOMBREMENTS"}) CHECK(d.find(std::string("\nLAYER\n2\n") + calque) != std::string::npos);
  std::size_t lignesDecoupe = 0;
  for (std::size_t i = d.find("LINE\n8\nDECOUPES"); i != std::string::npos; i = d.find("LINE\n8\nDECOUPES", i + 1)) ++lignesDecoupe;
  CHECK(lignesDecoupe == 4);
  CHECK(d.rfind("0\nEOF\n") == d.size() - 6);
}

TEST_CASE("10.11 · Specctra : DSN pour Freerouting, session SES relue et posée", "[L10][specctra]") {
  const ConceptionElec origine = exempleElectronique();
  const std::string dsn = specctraDsn(origine);
  CHECK(dsn.rfind("(pcb ", 0) == 0);
  CHECK(dsn.find("(network") != std::string::npos);
  CHECK(dsn.find("(boundary (path pcb 0") != std::string::npos);
  std::size_t places = 0;
  for (std::size_t i = dsn.find("(place "); i != std::string::npos; i = dsn.find("(place ", i + 1)) ++places;
  std::size_t avecCuivre = 0;  // les trous de fixation, sans cuivre, ne vont pas au routeur
  for (const auto& e : origine.circuit.empreintes)
    if (const DefEmpreinte* d = origine.empreinte(e.empreinte))
      avecCuivre += std::any_of(d->pastilles.begin(), d->pastilles.end(), [](const Pastille& p) { return p.type != TypePastille::NonMetallisee; }) ? 1 : 0;
  CHECK(places == avecCuivre);
  // Le routeur externe rend une session : relue, posée sur la carte sans routage, elle redonne une carte routée
  std::string ses = specctraSes(origine);
  ses.insert(ses.find("(routes") + 7, "\n    (parser (string_quote \") (space_in_quoted_tokens on))");
  const SessionSpecctra s = lireSpecctraSes(ses);
  REQUIRE(s.erreurs.empty());
  CHECK(s.pistes.size() == origine.circuit.pistes.size());
  CHECK(s.vias.size() == origine.circuit.vias.size());
  ConceptionElec c = origine;
  c.circuit.pistes.clear();
  c.circuit.vias.clear();
  CHECK(appliquerSessionSpecctra(c, s) == static_cast<int>(origine.circuit.pistes.size()));
  const Connectivite k = connectivite(c);
  const Remplissages z = remplirZones(c, k);
  CHECK(etatRoutage(c, k, z).chevelu.empty());
  int erreurs = 0;
  for (const auto& v : verifierDRCReglee(c, k, z)) erreurs += v.gravite == Gravite::Erreur;
  CHECK(erreurs == 0);
}

TEST_CASE("10.13 · Dossier Gerber relu en carte, attributs X2 retirés : réseaux identiques à la conception", "[L10][gerber]") {
  const ConceptionElec c = exempleElectronique();
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  std::map<Couche, std::string> gerbers, sansX2;
  for (const auto& cf : m.couches)
    if (estCuivre(cf.couche)) {
      gerbers[cf.couche] = gerber(m, cf.couche, "essai");
      std::istringstream in(gerbers[cf.couche]);
      std::string ligne, t;
      while (std::getline(in, ligne))
        if (ligne.rfind("%TO", 0) != 0 && ligne.rfind("%TD", 0) != 0 && ligne.rfind("%TA", 0) != 0) t += ligne + "\n";
      sansX2[cf.couche] = t;
    }
  const std::string pth = excellon(m, true, "essai");
  for (const auto& entree : {gerbers, sansX2}) {
    const CarteRelue r = relireCarte(entree, pth);
    REQUIRE(r.erreurs.empty());
    CHECK(!r.pistes.empty());
    // Chaque pastille de la conception, retrouvée à sa place : même partition en réseaux
    std::map<std::string, std::set<int>> reseauxRelus;  // réseau de la conception → réseaux relus
    std::map<int, std::set<std::string>> conceptionParRelu;
    int trouvees = 0, total = 0;
    for (const auto& p : pastilles(c, k)) {
      if (p.reseau.empty() || p.pastille.type == TypePastille::NonMetallisee) continue;
      ++total;
      const Pt q = versRepereFabrication(m.boite, p.pastille.position);
      for (const auto& x : r.pastilles)
        if (std::abs(x.position.x - q.x) <= 2 && std::abs(x.position.y - q.y) <= 2) {
          ++trouvees;
          reseauxRelus[p.reseau].insert(x.reseau);
          conceptionParRelu[x.reseau].insert(p.reseau);
          break;
        }
    }
    CHECK(trouvees == total);
    for (const auto& [n, s] : reseauxRelus) {
      INFO(n);
      CHECK(s.size() == 1);
    }
    for (const auto& [i, s] : conceptionParRelu) CHECK(s.size() == 1);
  }
  // Avec les attributs : composants retrouvés par leur repère, netlist cohérente
  const NetlistLue n = netlistDepuisCarteRelue(relireCarte(gerbers, pth));
  std::set<std::string> refs;
  for (const auto& x : n.composants) refs.insert(x.reference);
  CHECK(refs.count("U1") == 1);
  CHECK(partition(n) == partition(netlistDe(c)));
}

TEST_CASE("10.23, 10.9 · BoardView et IPC-2581", "[L10][sorties]") {
  const ConceptionElec c = exempleElectronique();
  const std::string bv = boardView(c);
  CHECK(bv.rfind("BRDOUT: ", 0) == 0);
  const Connectivite k = connectivite(c);
  std::size_t nPastilles = 0;
  for (const auto& p : pastilles(c, k)) nPastilles += p.reference.empty() ? 0 : 1;
  CHECK(bv.find("PINS: " + std::to_string(nPastilles) + "\n") != std::string::npos);
  CHECK(bv.find("\nU1 ") != std::string::npos);
  CHECK(bv.find(" GND\n") != std::string::npos);
  const std::string x = ipc2581(c, "essai");
  CHECK(xmlEquilibre(x));
  std::size_t composants = 0;
  for (std::size_t i = x.find("<Component "); i != std::string::npos; i = x.find("<Component ", i + 1)) ++composants;
  CHECK(composants == c.circuit.empreintes.size());
  CHECK(x.find("<LogicalNet name=\"GND\">") != std::string::npos);
}

TEST_CASE("10.13 · Carte modifiable reconstruite des fichiers de fabrication", "[L10][gerber]") {
  const ConceptionElec c = exempleElectronique();
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  std::map<Couche, std::string> gerbers;
  for (const auto& cf : m.couches)
    if (estCuivre(cf.couche)) gerbers[cf.couche] = gerber(m, cf.couche, "essai");
  const std::string pth = excellon(m, true, "essai");
  const ConceptionElec r = conceptionDepuisCarteRelue(relireCarte(gerbers, pth), "Relue", gerber(m, Couche::Contour, "essai"), pth);
  CHECK(partition(netlistDe(r)) == partition(netlistDe(c)));
  std::size_t avecCuivre = 0;
  for (const auto& e : c.circuit.empreintes)
    if (const DefEmpreinte* d = c.empreinte(e.empreinte))
      avecCuivre += std::any_of(d->pastilles.begin(), d->pastilles.end(), [](const Pastille& p) { return p.type != TypePastille::NonMetallisee; }) ? 1 : 0;
  CHECK(r.circuit.empreintes.size() == avecCuivre);
  CHECK(r.circuit.pistes.size() == c.circuit.pistes.size());
  for (const auto& t : r.circuit.pistes) CHECK(!t.reseau.empty());
  const Connectivite kr = connectivite(r);
  const EtatRoutage e = etatRoutage(r, kr, remplirZones(r, kr));
  CHECK(e.courtsCircuits.empty());
  // Contour relu : la carte garde ses dimensions
  CHECK(std::abs(boite(r.circuit.contour.polygone()).largeur() - boite(c.circuit.contour.polygone()).largeur()) < 50'000);
}
