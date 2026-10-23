// Prométhée : essais du lot L12 (travail à plusieurs, variantes, système). Licence GPL-3.0-only.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <set>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/collaboration.hpp"
#include "promethee/comparaison.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/historique_local.hpp"
#include "promethee/migrations.hpp"
#include "promethee/modele.hpp"
#include "promethee/systeme_erc.hpp"
#include "promethee/variantes.hpp"

using namespace prom;
using namespace prom::ecad;
namespace fs = std::filesystem;

namespace {
JeuSorties jeuJlcpcbPourEssai() {
  JeuSorties j;
  j.nom = "Dossier";
  j.sorties = {"gerber"};
  return j;
}
fs::path dossierEssai(const std::string& nom) {
  const fs::path d = fs::temp_directory_path() / ("promethee-l12-" + nom);
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
ComposantElec* parRef(ConceptionElec& c, const std::string& r) {
  for (auto& k : c.composants)
    if (k.reference == r) return &k;
  return nullptr;
}
// Copie aux identifiants tous renouvelés : deux fichiers sans ancêtre commun
ConceptionElec sansAncetre(const ConceptionElec& c) {
  std::string t = versJson(c).dump();
  static const std::regex uuid(R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})");
  std::map<std::string, std::string> nouveaux;
  std::string r;
  std::size_t debut = 0;
  for (std::sregex_iterator it(t.begin(), t.end(), uuid), fin; it != fin; ++it) {
    r += t.substr(debut, static_cast<std::size_t>(it->position()) - debut);
    auto& n = nouveaux[it->str()];
    if (n.empty()) n = nouvelUuid();
    r += n;
    debut = static_cast<std::size_t>(it->position() + it->length());
  }
  r += t.substr(debut);
  return conceptionDepuisJson(nlohmann::ordered_json::parse(r));
}
Article articlePrix(const std::string& id, const std::string& mpn, std::vector<std::pair<int, double>> prix, int lot = 1) {
  Article a;
  a.id = id;
  a.nom = id;
  a.mpn = mpn;
  OffreArticle o;
  o.distributeur = "Mouser";
  o.prix = std::move(prix);
  o.conditionnement = lot;
  a.offres.push_back(o);
  return a;
}
}  // namespace

TEST_CASE("12.1, 12.7 · Variantes « avec LED » et « sans LED » : deux nomenclatures, deux placements", "[L12][variantes]") {
  ConceptionElec c = exempleElectronique();
  REQUIRE(ajouterVariante(c, "avec LED", "carte complète"));
  REQUIRE(ajouterVariante(c, "sans LED"));
  CHECK_FALSE(ajouterVariante(c, "sans LED"));
  SurchargeVariante nm;
  nm.montage = MontageVariante::NonMonte;
  REQUIRE(reglerSurcharge(c, "D1", "sans LED", nm));
  SurchargeVariante v;
  v.valeur = "4k7";
  REQUIRE(reglerSurcharge(c, "R3", "sans LED", v));
  CHECK(verifierVariantes(c).empty());
  const auto quantite = [](const ConceptionElec& x) {
    int n = 0;
    for (const auto& l : nomenclature(x)) n += l.quantite;
    return n;
  };
  const ConceptionElec avec = conceptionPourVariante(c, "avec LED"), sans = conceptionPourVariante(c, "sans LED");
  CHECK(quantite(sans) == quantite(avec) - 1);
  CHECK(csvPlacement(avec).find("D1") != std::string::npos);
  CHECK(csvPlacement(sans).find("D1") == std::string::npos);
  CHECK(sans.parReference("R3")->valeur == "4k7");
  CHECK(c.parReference("R3")->valeur != "4k7");  // la conception de base n'est pas touchée
  const auto tableau = tableauVariantes(c);
  REQUIRE(tableau.size() == 2);
  CHECK(tableau[0].reference == "D1");
  CHECK(tableau[0].cellules[1] == "non monté");
  // Renommer fait suivre les surcharges ; le fichier les garde
  REQUIRE(renommerVariante(c, "sans LED", "économique"));
  CHECK(c.parReference("D1")->variantes.count("économique"));
  c.varianteActive = "économique";
  const ConceptionElec relue = conceptionDepuisJson(versJson(c));
  CHECK(relue.variantesMontage == c.variantesMontage);
  CHECK(relue.varianteActive == "économique");
  CHECK(relue.parReference("D1")->variantes == c.parReference("D1")->variantes);
  // Variante inconnue citée : signalée
  ComposantElec* r1 = parRef(c, "R1");
  r1->variantes["fantôme"].valeur = "1k";
  CHECK(!verifierVariantes(c).empty());
}

TEST_CASE("12.5 · Coût pour 10 cartes calculé depuis des prix par palier", "[L12][cout]") {
  ConceptionElec c = exempleElectronique();
  c.articles["res-1k"] = articlePrix("res-1k", "RC0603-1K", {{1, 0.10}, {10, 0.05}, {100, 0.01}});
  c.articles["ne555"] = articlePrix("ne555", "NE555DR", {{1, 0.50}, {25, 0.40}});
  c.articles["led"] = articlePrix("led", "LED-R", {{50, 0.02}}, 50);  // minimum de commande 50
  int resistances = 0;
  for (auto& k : c.composants) {
    if (k.reference == "U1") k.article = "ne555";
    if (k.reference == "D1") k.article = "led";
    if (k.reference.rfind('R', 0) == 0) {
      k.article = "res-1k";
      ++resistances;
    }
  }
  REQUIRE(resistances > 0);
  const CoutNomenclature cn = coutNomenclature(c, 10);
  const double attendu = resistances * 10 * 0.05 + 10 * 0.50 + 50 * 0.02;  // R au palier 10, NE555 au palier 1, LED par 50
  CHECK(cn.total == Catch::Approx(attendu));
  CHECK(cn.parCarte == Catch::Approx(attendu / 10));
  CHECK(cn.lignesSansPrix > 0);  // condensateurs et connecteur sans article
  double pu = 0;
  int cmd = 0;
  OffreArticle o;
  o.prix = {{1, 1.0}, {100, 0.5}};
  o.conditionnement = 3;
  REQUIRE(prixPourQuantite(o, 98, pu, cmd));
  CHECK(cmd == 99);
  CHECK(pu == Catch::Approx(1.0));
  // Nomenclature commune du projet : deux cartes, quantités en colonnes
  Projet p = projetVide();
  p.electronique = {c, c};
  p.electronique[1].nom = "Seconde carte";
  p.electronique[1].uuid = nouvelUuid();
  const NomenclatureProjet np = nomenclatureProjet(p, 10);
  REQUIRE(np.cartes.size() == 2);
  for (const auto& l : np.lignes) CHECK(l.parCarte[0] == l.parCarte[1]);
  CHECK(np.coutSysteme > 0);
}

TEST_CASE("12.2 · Deux révisions comparées, par catégorie", "[L12][comparer]") {
  const ConceptionElec a = exempleElectronique();
  ConceptionElec b = a;
  parRef(b, "R1")->valeur = "2k2";
  b.feuilles[0].fils.erase(b.feuilles[0].fils.begin());
  Via v;
  v.uuid = nouvelUuid();
  v.p = ptMm(10, 10);
  v.reseau = "GND";
  b.circuit.vias.push_back(v);
  b.circuit.regles.isolement += 50'000;
  ComparaisonConceptions d = comparerConceptions(a, b);
  CHECK(d.parCategorie["composant"] == 1);
  CHECK(d.parCategorie["fil"] == 1);
  CHECK(d.parCategorie["via"] == 1);
  CHECK(d.parCategorie["règles"] == 1);
  bool valeur = false;
  for (const auto& x : d.differences)
    if (x.categorie == "composant") valeur = x.repere == "R1" && x.detail.find("2k2") != std::string::npos && x.genre == "modifié";
  CHECK(valeur);
  CHECK(comparerConceptions(a, a).identiques());
}

TEST_CASE("12.8 · Fusion à trois voies : deux feuilles gardées, seul R3 demande", "[L12][fusion]") {
  ConceptionElec ancetre = exempleElectronique();
  Feuille f2;
  f2.uuid = nouvelUuid();
  f2.nom = "Extension";
  ancetre.feuilles.push_back(f2);
  ConceptionElec nous = ancetre, eux = ancetre;
  poserSymbole(nous, 0, "R", ptMm(30, 30));
  parRef(nous, "R3")->valeur = "22k";
  poserSymbole(eux, 1, "C", ptMm(40, 40));
  parRef(eux, "R3")->valeur = "47k";
  ResultatFusion f = fusionner(ancetre, nous, eux);
  for (const auto& x : f.conflits) UNSCOPED_INFO("conflit : " << x.chemin << " (" << x.ancetre << " / " << x.nous << " / " << x.eux << ")");
  REQUIRE(f.conflits.size() == 1);
  CHECK(f.conflits[0].repere == "R3");
  CHECK(f.conflits[0].propriete == "valeur");
  CHECK(f.conflits[0].nous == "22k");
  CHECK(f.conflits[0].eux == "47k");
  CHECK(f.conception.feuilles[0].symboles.size() == nous.feuilles[0].symboles.size());
  CHECK(f.conception.feuilles[1].symboles.size() == 1);
  CHECK(f.conception.composants.size() == ancetre.composants.size() + 2);
  CHECK(f.conception.parReference("R3")->valeur == "22k");
  // Le conflit tranché pour « eux »
  ResultatFusion g = fusionner(ancetre, nous, eux, {{f.conflits[0].chemin, ChoixFusion::Eux}});
  CHECK(g.conception.parReference("R3")->valeur == "47k");
  // Deux copies sans ancêtre commun : alignées, seule la vraie différence reste
  ConceptionElec copie = sansAncetre(ancetre);
  parRef(copie, "R2")->valeur = "100k";
  CHECK(comparerConceptions(ancetre, copie).differences.size() > 20);
  int apparies = 0;
  const ConceptionElec alignee = aligner(ancetre, copie, 10'000, &apparies);
  CHECK(apparies > 20);
  const auto d = comparerConceptions(ancetre, alignee);
  for (const auto& x : d.differences) UNSCOPED_INFO(x.categorie << " " << x.genre << " " << x.chemin << " " << x.detail);
  REQUIRE(d.differences.size() == 1);
  CHECK(d.differences[0].repere == "R2");
  // Réseau renommé reconnu à ses broches
  ConceptionElec renomme = ancetre;
  for (auto& f : renomme.feuilles)
    for (auto& e : f.etiquettes)
      if (e.texte == "SORTIE") e.texte = "OUT";
  const auto r = reseauxRenommes(ancetre, renomme);
  bool trouve = false;
  for (const auto& [x, y] : r) trouve |= x == "SORTIE" && y == "OUT";
  CHECK((trouve || std::none_of(ancetre.feuilles[0].etiquettes.begin(), ancetre.feuilles[0].etiquettes.end(), [](const Etiquette& e) { return e.texte == "SORTIE"; })));
}

TEST_CASE("12.9 · Historique local : trente enregistrements sous le plafond, version restaurée à l'octet", "[L12][historique]") {
  const fs::path d = dossierEssai("historique");
  HistoriqueLocal h(utf8(d / "historique"));
  Projet p = projetVide();
  p.electronique.push_back(exempleElectronique());
  std::vector<std::pair<std::string, std::string>> versions;
  for (int i = 0; i < 30; ++i) {
    parRef(p.electronique[0], "R1")->valeur = std::to_string(1000 + i) + "R";
    const std::string texte = versJson(p).dump(2) + "\n";
    versions.emplace_back(h.enregistrer(texte, i == 3 ? "veille" : ""), texte);
  }
  const auto chrono = h.chronologie();
  CHECK(chrono.size() == 30);
  CHECK(h.occupation() < (std::uint64_t{50} << 20));
  // Les morceaux communs ne sont rangés qu'une fois : trente versions tiennent dans bien moins que trente copies
  CHECK(h.occupation() < versions[0].second.size() * 6);
  CHECK(h.restaurer(versions[3].first) == versions[3].second);
  CHECK(h.restaurer(versions[29].first) == versions[29].second);
  CHECK(h.enregistrer(versions[29].second) == versions[29].first);  // rien n'a changé : pas de nouvel instantané
  // Plafond serré : les anciens partent, le nommé et le dernier restent
  HistoriqueLocal serre(utf8(d / "historique"), h.occupation() / 3);
  serre.enregistrer(versions[29].second + " ");
  const auto apres = serre.chronologie();
  CHECK(apres.size() < 31);
  CHECK(std::any_of(apres.begin(), apres.end(), [](const auto& x) { return x.nom == "veille"; }));
  CHECK(serre.restaurer(versions[3].first) == versions[3].second);
  // Découpage défini par le contenu : une insertion au début ne déplace pas les coupes suivantes
  const std::string t = versions[0].second, t2 = "insertion " + t;
  const auto c1 = HistoriqueLocal::decouper(t), c2 = HistoriqueLocal::decouper(t2);
  std::set<std::string> m1, m2;
  for (const auto& [a, n] : c1) m1.insert(t.substr(a, n));
  for (const auto& [a, n] : c2) m2.insert(t2.substr(a, n));
  int communs = 0;
  for (const auto& m : m1) communs += m2.count(m);
  CHECK(communs >= static_cast<int>(m1.size()) - 2);
}

TEST_CASE("12.10 · Verrou à côté du projet : l'autre poste voit qui le tient", "[L12][verrou]") {
  const fs::path d = dossierEssai("verrou");
  const std::string projet = utf8(d / "carte.prom");
  ecrireProjet(projetVide(), projet);
  REQUIRE(poserVerrou(projet, "Lou", "ATELIER-1"));
  CHECK(fs::exists(cheminFichier(cheminVerrou(projet))));
  VerrouProjet tenu;
  CHECK_FALSE(poserVerrou(projet, "Sam", "ATELIER-2", &tenu));
  CHECK(tenu.personne == "Lou");
  CHECK(decrireVerrou(tenu).find("Lou sur ATELIER-1") != std::string::npos);
  CHECK(poserVerrou(projet, "Lou", "ATELIER-1"));  // le même poste le reprend
  // Signal de vie arrêté depuis dix minutes : verrou périmé, l'autre poste le prend
  {
    nlohmann::ordered_json o = nlohmann::ordered_json::parse(lireFichier(cheminFichier(cheminVerrou(projet))));
    o["vie"] = secondesMaintenant() - 600;
    ecrireFichierAtomique(cheminFichier(cheminVerrou(projet)), o.dump());
  }
  CHECK_FALSE(verrouVivant(*lireVerrou(projet), secondesMaintenant()));
  CHECK(poserVerrou(projet, "Sam", "ATELIER-2"));
  leverVerrou(projet, "Lou", "ATELIER-1");  // pas le sien : rien ne bouge
  CHECK(lireVerrou(projet)->personne == "Sam");
  leverVerrou(projet, "Sam", "ATELIER-2");
  CHECK_FALSE(lireVerrou(projet));
}

TEST_CASE("12.4 · Archive du projet rouverte ailleurs à l'identique", "[L12][archive]") {
  const fs::path d = dossierEssai("archive");
  fs::create_directories(d / "source" / "modeles");
  {
    std::ofstream(d / "source" / "modeles" / "diode.lib", std::ios::binary) << ".model DX D(IS=1e-14)\n";  // octets exacts, sans CRLF sous Windows
  }
  Projet p = projetVide();
  p.nom = "Archive";
  p.electronique.push_back(exempleElectronique());
  parRef(p.electronique[0], "D1")->champs["Sim.Bibliotheque"] = "modeles/diode.lib";
  parRef(p.electronique[0], "R1")->champs["Sim.Bibliotheque"] = "manquant.lib";
  const std::string chemin = utf8(d / "source" / "carte.prom");
  ecrireProjet(p, chemin);
  std::vector<std::string> manquants;
  const std::string zip = archiverProjet(p, chemin, &manquants);
  CHECK(manquants == std::vector<std::string>{"manquant.lib"});
  std::vector<std::string> defauts;
  const std::string extrait = extraireArchive(zip, utf8(d / "ailleurs"), &defauts);
  CHECK(defauts.empty());
  const Projet q = lireProjet(extrait);
  CHECK(q.nom == p.nom);
  REQUIRE(q.electronique.size() == 1);
  CHECK(q.electronique[0].composants.size() == p.electronique[0].composants.size());
  const std::string lib = q.electronique[0].parReference("D1")->champs.at("Sim.Bibliotheque");
  CHECK(lib == "fichiers/diode.lib");
  CHECK(lireFichier(cheminFichier(extrait).parent_path() / cheminFichier(lib)) == ".model DX D(IS=1e-14)\n");
  // Archive abîmée : signalée
  std::string abime = zip;
  const auto pos = abime.find("DX D");
  if (pos != std::string::npos) abime[pos] = 'Y';
  std::vector<std::string> d2;
  try {
    extraireArchive(abime, utf8(d / "abime"), &d2);
  } catch (const std::exception&) {
    d2.push_back("illisible");
  }
  CHECK((!d2.empty() || pos == std::string::npos));
}

TEST_CASE("12.12 · Notes attachées aux objets, mentions, retour de revue de la visionneuse", "[L12][notes]") {
  ConceptionElec c = exempleElectronique();
  NoteObjet t;
  t.genre = NoteObjet::Genre::Tache;
  t.objet = c.parReference("R1")->uuid;
  t.texte = "Vérifier la puissance avec @Lou";
  t.responsable = "Sam";
  t.priorite = 2;
  const std::string id = ajouterNote(c, t);
  CHECK(!repondre(c, id, "Lou", "Vu : 0,1 W suffit").empty());
  CHECK(notesDe(c, c.parReference("R1")->uuid).size() == 2);
  CHECK(mentions("@Lou et @Évan, voir @Lou") == std::vector<std::string>{"Lou", "Évan"});
  CHECK(enAttente(c, "Sam").size() == 1);
  CHECK(enAttente(c, "Lou").size() == 1);
  CHECK(objetLisible(c, c.notes[0]) == "R1");
  REQUIRE(clore(c, id));
  CHECK(enAttente(c, "Sam").empty());
  const std::string retour = R"({"format":"promethee-revue","remarques":[{"repere":"D1","texte":"Couleur ?","auteur":"relecteur"},{"reseau":"GND","texte":"Plan continu ?"},{"repere":"R99","texte":"?"}]})";
  std::vector<std::string> alertes;
  CHECK(importerRetourRevue(c, retour, &alertes) == 3);
  CHECK(alertes.size() == 1);
  CHECK(notesDe(c, c.parReference("D1")->uuid).size() == 1);
  CHECK(notesDe(c, "reseau:GND").size() == 1);
  // Les notes restent dans le fichier
  const ConceptionElec r = conceptionDepuisJson(versJson(c));
  CHECK(r.notes == c.notes);
}

TEST_CASE("12.14, 12.17 · Projet repris d'un modèle, réglages par sections, index des usages", "[L12][modeles]") {
  Projet modele = projetVide();
  modele.nom = "Modèle d'équipe";
  ConceptionElec m = exempleElectronique();
  m.circuit.regles.isolement = 300'000;
  m.circuit.empilage.couchesCuivre = 4;
  m.cartouche.societe = "Atelier";
  m.jeuxSorties.push_back({"Fabrication", "sorties/{carte}", "", {"fabrication"}, false, true});
  modele.electronique.push_back(m);
  const Projet p = projetDepuisModele(modele, "Station 2");
  CHECK(p.nom == "Station 2");
  CHECK(p.electronique[0].nom == "Station 2");
  CHECK(p.electronique[0].uuid != m.uuid);
  CHECK(p.electronique[0].feuilles.size() == m.feuilles.size());
  CHECK(p.electronique[0].circuit.empilage.couchesCuivre == 4);
  ConceptionElec cible = exempleElectronique();
  cible.cartouche.titre = "Ma carte";
  const auto prises = reprendreReglages(cible, m, {"regles", "cartouche", "jeuxSorties"});
  CHECK(prises.size() == 3);
  CHECK(cible.circuit.regles.isolement == 300'000);
  CHECK(cible.cartouche.societe == "Atelier");
  CHECK(cible.cartouche.titre == "Ma carte");
  CHECK(cible.circuit.empilage.couchesCuivre != 4);
  CHECK(cible.jeuxSorties.size() == 1);
  // Index : deux projets dans un dossier déclaré
  const fs::path d = dossierEssai("index");
  ecrireProjet(p, utf8(d / "a.prom"));
  fs::create_directories(d / "sous");
  ecrireProjet(modele, utf8(d / "sous" / "b.prom"));
  const IndexUsages ix = indexerUsages({utf8(d)});
  CHECK(ix.projets == 2);
  const std::string empreinteR1 = m.parReference("R1")->empreinte;
  REQUIRE(ix.empreintes.count(empreinteR1));
  CHECK(ix.empreintes.at(empreinteR1).size() == 2);
}

namespace {
// Carte d'essai : symboles en ligne, chaque broche reliée par une étiquette globale
struct Montage {
  ConceptionElec c = nouvelleConception("Carte");
  double x = 25.4;
  void poser(const std::string& symbole, const std::string& ref, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, 50.8));
    x += 25.4;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        c.composant(p.composant)->reference = ref;
        for (const auto& b : brochesPlacees(p, *c.symbole(p.symbole)))
          for (const auto& [num, reseau] : liaisons)
            if (b.broche->numero == num) {
              const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
              const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
              c.feuilles[0].fils.push_back({nouvelUuid(), b.p, bout});
              Etiquette e;
              e.uuid = nouvelUuid();
              e.texte = reseau;
              e.p = bout;
              e.type = TypeEtiquette::Globale;
              c.feuilles[0].etiquettes.push_back(e);
            }
      }
  }
};
}  // namespace

TEST_CASE("12.19 · Deux cartes reliées par un câble TX à TX : « sortie contre sortie »", "[L12][systeme]") {
  Montage a, b;
  a.c.nom = "Maître";
  b.c.nom = "Esclave";
  for (Montage* m : {&a, &b}) {
    m->poser("NE555", "U1", {{"3", "TX"}, {"8", "+5V"}, {"1", "GND"}});
    m->poser("Conn_01x02", "J1", {{"1", "TX"}, {"2", "GND"}});
  }
  Projet p = projetVide();
  p.electronique = {a.c, b.c};
  LiaisonSysteme l;
  l.uuid = nouvelUuid();
  l.nom = "W1";
  l.carteA = a.c.uuid;
  l.connecteurA = "J1";
  l.carteB = b.c.nom;  // une carte se désigne aussi par son nom
  l.connecteurB = "J1";
  p.liaisons.push_back(l);
  const NetlistSysteme n = netlistSysteme(p);
  CHECK(n.alertes.empty());
  REQUIRE(n.reseaux.size() == 2);  // TX et GND traversent le câble
  const auto v = verifierSysteme(p);
  bool sorties = false;
  for (const auto& x : v) sorties |= x.message.find("Sortie contre sortie sur le câble W1") != std::string::npos;
  CHECK(sorties);
  // Croisé : TX de A sur GND de B → plus de sortie contre sortie, mais deux réseaux joints autrement
  p.liaisons[0].broches = {{"1", "2"}, {"2", "1"}};
  bool encore = false;
  for (const auto& x : verifierSysteme(p)) encore |= x.message.find("Sortie contre sortie") != std::string::npos;
  CHECK_FALSE(encore);
  // Connecteur absent : signalé
  p.liaisons[0].connecteurB = "J9";
  CHECK(!netlistSysteme(p).alertes.empty());
  // Le projet garde ses liaisons, ses exigences et ses ordres
  p.exigences.push_back({"EX-1", "Coût", "client", "analyse", {}, "cout", "<=", "40 €", 100});
  OrdreModification o;
  o.id = "OM-1";
  o.titre = "Changer R1";
  p.ordresModification.push_back(o);
  const Projet q = normaliser(versJson(p));
  CHECK(q.liaisons == p.liaisons);
  CHECK(q.exigences == p.exigences);
  CHECK(q.ordresModification == p.ordresModification);
}

TEST_CASE("Format 5 (0.33) : un projet écrit pour la 0.32 garde ce qu'elle ne connaît pas et le dit", "[L12][ECH-11]") {
  Projet p = projetVide();
  ConceptionElec c = exempleElectronique();
  ajouterVariante(c, "sans LED");
  NoteObjet n;
  n.texte = "à revoir";
  ajouterNote(c, n);
  c.jeuxSorties.push_back(jeuJlcpcbPourEssai());
  p.electronique.push_back(c);
  p.exigences.push_back({"EX-1", "Coût", "", "analyse", {}, "cout", "<=", "40 €", 100});
  CHECK(versJson(c)["format"] == 5);
  CHECK(versionFichierCourante() == "0.33");
  const FichierAnterieur f = pourVersion(p, "0.32");
  std::string pertes;
  for (const auto& x : f.pertes) pertes += x + "\n";
  INFO(pertes);
  CHECK(pertes.find("variante de montage") != std::string::npos);
  CHECK(pertes.find("note attachée") != std::string::npos);
  CHECK(pertes.find("jeu de sorties") != std::string::npos);
  CHECK(pertes.find("exigence") != std::string::npos);
  // Le fichier pour la 0.32 porte le format 4 et garde les clés nouvelles, que la 0.32 réécrit telles quelles
  const std::string texte = f.projet.dump();
  CHECK(texte.find("\"variantesMontage\"") != std::string::npos);
  CHECK(texte.find("\"format\":4") != std::string::npos);
}
