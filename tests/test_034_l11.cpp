// Prométhée : essais de la 0.34, lot L11 (simulation) — modèles des articles et compatibilité PSpice (11.5),
// calculateurs (11.7, 11.17 à 11.19), netlist ouverte et partie simulée seule (11.8), préparation et remèdes (11.9),
// modèles tirés de la fiche (11.11), IBIS et Verilog-A (11.12), courbes calculées (11.13), classeur (11.14), mesures et
// références (11.15). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/articles.hpp"
#include "promethee/calculateurs_electronique.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modeles_simulation.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_projet.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
std::string donnee11(const std::string& f) { return prom::lireFichier(prom::cheminFichier(std::string(PROMETHEE_DONNEES) + "/" + f)); }
// Montage : symboles posés en ligne, chaque broche reliée par une étiquette globale au réseau voulu
struct Montage11 {
  ConceptionElec c = nouvelleConception("Montage");
  double x = 25.4;
  ComposantElec* poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, 50.8));
    x += 25.4;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        ComposantElec* k = c.composant(p.composant);
        k->reference = ref;
        k->valeur = valeur;
      }
    return relier(s, liaisons);
  }
  // Une autre unité du composant (alimentation d'un double ampli), posée et reliée
  void poserUnite(const std::string& ref, int unite, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserUniteEn(c, 0, uuid(ref), unite, ptMm(x, 50.8));
    x += 25.4;
    relier(s, liaisons);
  }
  ComposantElec* relier(const std::string& s, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    ComposantElec* r = nullptr;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        r = c.composant(p.composant);
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
    return r;
  }
  std::string uuid(const std::string& ref) const { return c.parReference(ref) ? c.parReference(ref)->uuid : std::string(); }
};
Montage11 filtre(double r = 1000, double cap = 100e-9) {
  Montage11 m;
  m.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", "R1", std::to_string(r), {{"1", "ENTREE"}, {"2", "SORTIE"}});
  m.poser("C", "C1", std::to_string(cap * 1e9) + "n", {{"1", "SORTIE"}, {"2", "GND"}});
  return m;
}
AnalyseSpice alternatif(double f0 = 10, double f1 = 1e6, int points = 50) {
  AnalyseSpice a;
  a.type = AnalyseSpice::Type::Alternatif;
  a.fDebut = f0, a.fFin = f1, a.pointsParDecade = points;
  return a;
}
double tensionRepos(const ResultatSimulation& r, const NetlistSpice& n, const std::string& reseau) {
  const auto* v = r.reel(vecteurTension(n, reseau));
  return v && !v->empty() ? v->back() : std::nan("");
}
// Ligne de résultat d'un calculateur (son nom contient « nom »)
std::string ligne(const prom::elec::ResultatCalcul& r, const std::string& nom) {
  for (const auto& l : r.lignes)
    if (l.nom.find(nom) != std::string::npos) return l.valeur;
  return {};
}
const char* LM358_PSPICE = R"(* LM358 : macromodèle d'ampli opérationnel (fiche : gain 100 dB, produit gain-bande 0,7 MHz, sortie jusqu'à
* 1,5 V sous V+, courant de repos 0,5 mA) ; syntaxe PSpice, écrit pour les essais de Prométhée
.SUBCKT LM358 1 2 3 4 5
* 1 entrée +, 2 entrée -, 3 V+, 4 V-, 5 sortie
RIN 1 2 2MEG
IB1 1 4 20N
IB2 2 4 20N
GA 0 10 VALUE={1E-3*V(1,2)}
RA 10 0 100MEG TC=0.0005,0
CA 10 0 227P
EOUT 11 0 VALUE={LIMIT(V(10), V(4)+0.02, V(3)-1.5)}
RO 11 5 75
GQ 3 4 VALUE={IF(V(3,4) > 3, 0.5M, 0)}
.ENDS LM358
)";
}  // namespace

TEST_CASE("11.5 · modèle LM358 du fabricant, en syntaxe PSpice, rattaché à l'article, chargé et simulé", "[034][L11][11.5]") {
  const AdaptationModele ad = adapterModele(LM358_PSPICE, "ps");
  for (const auto& n : ad.notes) UNSCOPED_INFO(n);
  CHECK(ad.lisible());
  CHECK(ad.notes.size() == 3);  // LIMIT, TC, IF
  CHECK(ad.texte.find("min(max(V(10), V(4)+0.02), V(3)-1.5)") != std::string::npos);
  CHECK(ad.texte.find("tc1=0.0005 tc2=0") != std::string::npos);
  CHECK(nomModele(LM358_PSPICE) == "LM358");
  CHECK(noeudsSousCircuit(LM358_PSPICE, "LM358") == 5);
  // Un modèle chiffré est refusé avec sa ligne
  const AdaptationModele chiffre = adapterModele("* modèle\n$CDNENCSTART\nabcdef\n$CDNENCFINISH\n", "ps");
  REQUIRE(!chiffre.refus.empty());
  CHECK(chiffre.refus.front().rfind("ligne 2", 0) == 0);
  // Ampli non inverseur de gain 11 sur 12 V : U1 posé avec l'article LM358 (deux amplis, le second non relié)
  Montage11 m;
  m.poser("Source_tension", "V1", "0.5", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("LM358_unites", "U1", "LM358", {{"3", "ENTREE"}, {"2", "FB"}, {"1", "SORTIE"}});
  m.poserUnite("U1", 3, {{"8", "+12V"}, {"4", "GND"}});
  m.poser("R", "R1", "1k", {{"1", "FB"}, {"2", "GND"}});
  m.poser("R", "R2", "10k", {{"1", "SORTIE"}, {"2", "FB"}});
  ComposantElec* u = m.c.composant(m.uuid("U1"));
  REQUIRE(u);
  Article a;
  a.id = "ti-lm358";
  a.mpn = "LM358DR";
  a.fabricant = "Texas Instruments";
  a.modeleSpice.texte = LM358_PSPICE;
  a.modeleSpice.nom = "LM358";
  a.modeleSpice.broches = {"3 2 8 4 1", "5 6 8 4 7"};
  a.modeleSpice.compatibilite = "ps";
  a.modeleSpice.genre = "sous-circuit";
  m.c.articles[a.id] = a;
  u->article = a.id;
  AnalyseSpice op;
  op.type = AnalyseSpice::Type::PointDeRepos;
  const NetlistSpice n = netlistSpice(m.c, op);
  INFO(n.texte);
  for (const auto& e : n.erreurs) UNSCOPED_INFO(e);
  REQUIRE(n.valide());
  CHECK(n.compatibilite == "ps");
  CHECK(n.texte.find("XU1 n_entree n_fb n__12v 0 n_sortie LM358") != std::string::npos);
  CHECK(n.texte.find("XU1_2") == std::string::npos);  // l'unité B, non reliée, est laissée
  CHECK(n.instances.at("xu1") == "U1");
  // Un article inconnu de la netlist, un nombre de nœuds faux : refusé
  Article faux = a;
  faux.modeleSpice.broches = {"3 2 8 1"};
  m.c.articles[a.id] = faux;
  CHECK(!netlistSpice(m.c, op).valide());
  m.c.articles[a.id] = a;
  // Article : le modèle passe dans le fichier et revient
  CHECK(articleDepuisJson(articleVersJson(a)) == a);
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  const ResultatSimulation r = simuler(n);
  for (const auto& l : r.console) UNSCOPED_INFO(l);
  REQUIRE(r.reussi());
  CHECK(tensionRepos(r, n, "SORTIE") == Approx(5.5).epsilon(0.001));
  // Entrée de 1,2 V : la sortie bute 1,5 V sous l'alimentation, comme le dit la fiche
  m.c.composants[0].valeur = "1.2";
  const NetlistSpice n2 = netlistSpice(m.c, op);
  const ResultatSimulation r2 = simuler(n2);
  REQUIRE(r2.reussi());
  CHECK(tensionRepos(r2, n2, "SORTIE") == Approx(10.5).epsilon(0.01));  // 75 Ω de sortie dans les 11 kΩ du gain
}

TEST_CASE("11.7 · calculateurs courants comparés aux exemples des spécifications", "[034][L11][11.7]") {
  const auto* piste = calculateurElectronique("piste");
  REQUIRE(piste);
  // 1 A, 10 °C, 1 oz dehors : 0,30 mm (11,8 mil) par l'IPC-2221
  const auto rp = executer(*piste, {1, 10, 1, 0, 50, 25, 1});
  CHECK(ligne(rp, "Largeur").rfind("0,30", 0) == 0);
  const auto* led = calculateurElectronique("led");
  const auto rl = executer(*led, {5, 2, 10, 1});
  CHECK(ligne(rl, "Résistance exacte").find("300") != std::string::npos);
  CHECK(ligne(rl, "Résistance normalisée").find("300") != std::string::npos);
  // LM317 : 5 V avec R1 = 240 Ω : R2 = 713 Ω, 715 Ω en E96
  const auto* reg = calculateurElectronique("regulateur");
  const auto rr = executer(*reg, {0, 5, 240, 4, 1, 3});
  CHECK(ligne(rr, "R2 exacte").find("713") != std::string::npos);
  CHECK(ligne(rr, "R2 normalisée").find("715") != std::string::npos);
  CHECK(!ligne(rr, "huit coins").empty());
  const auto* rc = calculateurElectronique("filtre-rc");
  CHECK(ligne(executer(*rc, {0, 1, 100, 0}), "coupure").find("1,59") != std::string::npos);
  const auto* div = calculateurElectronique("diviseur");
  const auto rd = executer(*div, {12, 3.3, 10, 3});
  CHECK(!ligne(rd, "R1").empty());
  const std::string ecart = ligne(rd, "Écart");
  CHECK(std::abs(std::stod(ecart.substr(0, ecart.find(' ')).replace(ecart.find(','), 1, "."))) < 1.0);
  CHECK(famillesCalculateursElectroniques().size() == 4);
  for (const auto& c : calculateursElectroniques()) {
    INFO(c.id);
    const auto r = executer(c);
    CHECK((!r.lignes.empty() || !r.explication.empty()));
  }
}

TEST_CASE("11.8 · étage d'entrée simulé seul, sortie chargée par les 10 kΩ de l'étage suivant : même gain que le schéma complet", "[034][L11][11.8]") {
  // Étage d'entrée : ampli non inverseur (AOP) de gain 3 ; étage suivant : 10 kΩ d'entrée, puis un filtre
  Montage11 m;
  m.poser("Source_tension", "V1", "DC 1 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("AOP", "U1", "AOP", {{"1", "ENTREE"}, {"3", "FB"}, {"4", "S1"}, {"5", "+12V"}, {"2", "-12V"}});
  m.poser("R", "R1", "10k", {{"1", "FB"}, {"2", "GND"}});
  m.poser("R", "R2", "20k", {{"1", "S1"}, {"2", "FB"}});
  m.poser("R", "R5", "1k", {{"1", "S1"}, {"2", "MILIEU"}});  // sortie de l'étage par 1 kΩ : la charge compte
  m.poser("R", "R3", "10k", {{"1", "MILIEU"}, {"2", "FB2"}});
  m.poser("AOP", "U2", "AOP", {{"1", "GND"}, {"3", "FB2"}, {"4", "SORTIE"}, {"5", "+12V"}, {"2", "-12V"}});
  m.poser("R", "R4", "10k", {{"1", "SORTIE"}, {"2", "FB2"}});
  AnalyseSpice op;
  op.type = AnalyseSpice::Type::PointDeRepos;
  const std::set<std::string> etage{m.uuid("V1"), m.uuid("U1"), m.uuid("R1"), m.uuid("R2"), m.uuid("R5")};
  const auto coupees = liaisonsDeLaPartie(m.c, etage);
  // MILIEU, et les deux alimentations partagées avec l'étage suivant, tenues d'après leur nom
  REQUIRE(coupees.size() == 3);
  for (const auto& l : coupees) {
    if (l.reseau == "MILIEU") CHECK(l.bouclage == BouclageLiaison::Rien);
    else {
      CHECK(l.bouclage == BouclageLiaison::Source);
      CHECK(std::abs(l.valeur) == Approx(12));
    }
  }
  OptionsSimulation o;
  o.composants = etage;
  o.liaisons = {{"MILIEU", BouclageLiaison::Charge, 10e3, 0}};
  const NetlistSpice n = netlistSpice(m.c, op, o);
  INFO(n.texte);
  REQUIRE(n.valide());
  CHECK(n.texte.find("RR3") == std::string::npos);
  CHECK(n.texte.find("XU2") == std::string::npos);
  CHECK(n.texte.find("R_coupe1 n_milieu 0 10000") != std::string::npos);
  // Préparation : la coupure est dite
  bool partie = false;
  for (const auto& l : preparerSimulation(m.c, op, o)) partie |= l.genre == "partie" && l.reseau == "MILIEU";
  CHECK(partie);
  // Directives de la feuille recopiées ; un bloc .control refusé tant qu'il n'est pas permis ; jamais « shell »
  TexteSchema d;
  d.uuid = nouvelUuid();
  d.texte = ".options savecurrents\n.param Rcharge=4.7k";
  m.c.feuilles[0].textes.push_back(d);
  CHECK(netlistSpice(m.c, op).texte.find(".options savecurrents") != std::string::npos);
  TexteSchema ctl;
  ctl.uuid = nouvelUuid();
  ctl.texte = ".control\nprint all\n.endc";
  m.c.feuilles[0].textes.push_back(ctl);
  CHECK(!netlistSpice(m.c, op).valide());
  OptionsSimulation permis;
  permis.autoriserControle = true;
  CHECK(netlistSpice(m.c, op, permis).valide());
  m.c.feuilles[0].textes.back().texte = ".control\nshell rm -rf /\n.endc";
  CHECK(!netlistSpice(m.c, op, permis).valide());
  m.c.feuilles[0].textes.pop_back();
  // Paramètres du projet : ordre des dépendances, définition circulaire refusée, valeurs entre accolades
  OptionsSimulation p;
  p.parametres = {{"gain", "1 + Rf/Rg", ""}, {"Rf", "20k", "Ω"}, {"Rg", "10k", "Ω"}};
  std::string erreur;
  const auto ordre = ordonnerParametres(p.parametres, &erreur);
  REQUIRE(ordre.size() == 3);
  CHECK(ordre.back().nom == "gain");
  CHECK(ordonnerParametres({{"a", "b*2", ""}, {"b", "a/2", ""}}, &erreur).empty());
  CHECK(erreur.find("se définissent l’un par l’autre") != std::string::npos);
  m.c.composant(m.uuid("R2"))->valeur = "{Rf}";
  const NetlistSpice np = netlistSpice(m.c, op, p);
  CHECK(np.texte.find(".param Rf=20k") != std::string::npos);
  CHECK(np.texte.find("RR2 n_s1 n_fb {Rf}") != std::string::npos);
  // Console : lecture permise, « shell » refusé
  std::string refus;
  CHECK(commandeConsole("shell ls", &refus).empty());
  CHECK(!refus.empty());
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  const ResultatSimulation complet = simuler(np);
  INFO(np.texte);
  REQUIRE(complet.reussi());
  OptionsSimulation seul = o;
  seul.parametres = p.parametres;
  const NetlistSpice ns = netlistSpice(m.c, op, seul);
  const ResultatSimulation partiel = simuler(ns);
  REQUIRE(partiel.reussi());
  const double gComplet = tensionRepos(complet, np, "MILIEU"), gSeul = tensionRepos(partiel, ns, "MILIEU");
  CHECK(gComplet == Approx(3.0 * 10 / 11).epsilon(0.01));
  CHECK(std::abs(gSeul - gComplet) / gComplet < 0.001);
  // Sans la charge, l'écart dépasse le millième (sortie de l'étage par 1 kΩ)
  OptionsSimulation vide = seul;
  vide.liaisons.clear();
  const NetlistSpice nv = netlistSpice(m.c, op, vide);
  const ResultatSimulation rv = simuler(nv);
  REQUIRE(rv.reussi());
  CHECK(std::abs(tensionRepos(rv, nv, "MILIEU") - gComplet) / gComplet > 0.001);
  // La console lit le dernier calcul
  const auto lignes = commandeConsole("print v(n_milieu)");
  bool lu = false;
  for (const auto& l : lignes) lu |= l.find("n_milieu") != std::string::npos;
  CHECK(lu);
}

TEST_CASE("11.9 · circuit arrêté sur « timestep too small » : cause dite en français, montée des sources proposée, calcul abouti et noté", "[034][L11][11.9]") {
  // Pont redresseur à diodes très raides, gros condensateur, départ sans point de repos : le pas s'effondre
  Montage11 m;
  m.poser("Source_tension", "V1", "SIN(0 325 50)", {{"1", "SECTEUR"}, {"2", "GND"}});
  const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> diodes = {
      {"D1", {{"2", "SECTEUR"}, {"1", "P"}}}, {"D2", {{"2", "GND"}, {"1", "P"}}}, {"D3", {{"2", "M"}, {"1", "SECTEUR"}}}, {"D4", {{"2", "M"}, {"1", "GND"}}}};
  for (const auto& [ref, l] : diodes) {
    ComposantElec* k = m.poser("D", ref, "DX", l);
    k->champs["Sim.Modele"] = "D{ref} {2} {1} DX";
    k->champs["Sim.Bibliotheque"] = ".model DX D(IS=1e-30 N=0.3 RS=0)";
  }
  m.poser("C", "C1", "1000u", {{"1", "P"}, {"2", "M"}});
  m.poser("R", "R1", "100", {{"1", "P"}, {"2", "M"}});
  AnalyseSpice a;
  a.type = AnalyseSpice::Type::Transitoire;
  a.pas = 10e-6, a.duree = 40e-3;
  OptionsSimulation o;
  o.sansPointRepos = true;
  const NetlistSpice n = netlistSpice(m.c, a, o);
  INFO(n.texte);
  REQUIRE(n.valide());
  CHECK(std::count(n.texte.begin(), n.texte.end(), '\n') > 8);
  CHECK(n.texte.find(".model DX") == n.texte.rfind(".model DX"));  // la bibliothèque une seule fois
  CHECK(n.texte.find("uic") != std::string::npos);
  // Préparation : une boucle de deux sources idéales est trouvée, et une analyse alternative sans amplitude AC
  Montage11 b;
  b.poser("Source_tension", "V1", "5", {{"1", "A"}, {"2", "GND"}});
  b.poser("Source_tension", "V2", "5", {{"1", "A"}, {"2", "GND"}});
  b.poser("R", "R1", "1k", {{"1", "A"}, {"2", "GND"}});
  bool boucle = false, ac = false;
  for (const auto& l : preparerSimulation(b.c, alternatif())) {
    if (l.genre == "boucle") boucle = std::find(l.remedes.begin(), l.remedes.end(), "Insérer 1 mΩ en série") != l.remedes.end() && !l.composant.empty();
    if (l.genre == "source" && l.niveau == "erreur") ac = true;
  }
  CHECK(boucle);
  CHECK(ac);
  // Modèles déduits du repère pour un composant importé (symbole inconnu de la netlist), en un geste
  Montage11 imp;
  imp.poser("Source_tension", "V1", "5", {{"1", "A"}, {"2", "GND"}});
  ComposantElec* ri = imp.poser("R", "R7", "4k7", {{"1", "A"}, {"2", "GND"}});
  ri->symbole = "importe:R";
  imp.c.symboles["importe:R"] = *imp.c.symbole("R");
  imp.c.symboles["importe:R"].id = "importe:R";
  bool modele = false;
  for (const auto& l : preparerSimulation(imp.c, AnalyseSpice{})) modele |= l.genre == "modèle" && l.composant == ri->uuid;
  CHECK(modele);
  std::vector<std::string> aSaisir;
  CHECK(deduireModeles(imp.c, {ri->uuid}, &aSaisir) == 1);
  CHECK(netlistSpice(imp.c, AnalyseSpice{}).texte.find("RR7 n_a 0 4700") != std::string::npos);
  CHECK(retirerModeles(imp.c, {ri->uuid}) == 1);
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  const ResultatSimulation r = simuler(n);
  REQUIRE(!r.reussi());
  const DiagnosticEchec d = analyserEchec(r, n);
  INFO(d.cause);
  CHECK(d.genre == "pas");
  CHECK(d.cause.find("Le pas de calcul s’est effondré") != std::string::npos);
  CHECK((d.reseau == "M" || !d.composant.empty()));
  CHECK(std::find(d.remedes.begin(), d.remedes.end(), "srcsteps") != d.remedes.end());
  CHECK(libelleRemede("srcsteps") == "Montée progressive des sources");
  OptionsSimulation essai = o;
  const EssaiRemedes e = simulerAvecRemedes(m.c, a, essai);
  for (const auto& x : e.essayes) UNSCOPED_INFO("essayé : " << x);
  REQUIRE(e.resultat.reussi());
  CHECK(!e.retenu.empty());
  REQUIRE(essai.remedes.size() == 1);
  CHECK(essai.remedes.front().find(libelleRemede(e.retenu)) == 0);
  CHECK(essai != o);
}

TEST_CASE("11.11 · diode modélisée depuis trois points de sa fiche : tensions directes simulées à 2 % des valeurs relevées", "[034][L11][11.11]") {
  // Fiche (1N4148, 25 °C) : 0,60 V à 1 mA, 0,72 V à 10 mA, 0,93 V à 100 mA
  const std::vector<PointFiche> points = {{1e-3, 0.60}, {10e-3, 0.72}, {100e-3, 0.93}};
  const AjustementModele d = ajusterDiode(points, "D1N4148_FICHE", 25);
  INFO(d.texte << d.erreur);
  REQUIRE(d.reussi);
  CHECK(d.ecartMax < 0.005);
  CHECK(d.parametres.at("N") > 1);
  Article a;
  a.id = "1n4148";
  rattacherModele(a, d);
  CHECK(a.modeleSpice.nom == "D1N4148_FICHE");
  CHECK(a.modeleSpice.source.find("3 point(s)") != std::string::npos);
  // Fiches à champs bornés
  const FicheModele* f = ficheModele("diode");
  REQUIRE(f);
  std::string refus;
  CHECK(ecrireModele(*f, "DX", {}, &refus)->rfind(".model DX D(IS=", 0) == 0);
  CHECK(!ecrireModele(*f, "DX", {1e-14, 10}, &refus));
  CHECK(refus.find("hors bornes") != std::string::npos);
  for (const auto& x : fichesModeles()) CHECK(ecrireModele(x, "M_" + x.id, {}).has_value());
  // MOSFET de puissance : seuil 2 V à 250 µA, 50 mΩ à 10 V
  const AjustementModele mos = ajusterVdmos(2.0, 250e-6, 0.05, 10, 30e-9, 10, 8e-9, 50, false, "IRF_FICHE");
  REQUIRE(mos.reussi);
  CHECK(mos.ecartMax < 0.01);
  CHECK(modeleTransformateur(1e-3, 0.1, 1e-5, "T1").parametres.at("K") == Approx(std::sqrt(0.99)));
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  for (const auto& p : points) {
    const std::string net = "* diode de la fiche\nI1 0 a DC " + std::to_string(p.x) + "\nD1 a 0 D1N4148_FICHE\n" + d.texte + ".op\n.end\n";
    const ResultatSimulation r = simuler(net);
    REQUIRE(r.reussi());
    const auto* v = r.reel("v(a)");
    REQUIRE(v);
    INFO(p.x << " A : " << v->back() << " V");
    CHECK(std::abs(v->back() - p.y) / p.y < 0.02);
  }
  // Résistance passante du MOSFET ajusté, à 10 V de grille et 1 A
  const std::string netm = "* mosfet\nVG g 0 DC 10\nVD d 0 DC 0.05\nM1 d g 0 0 IRF_FICHE\n" + mos.texte + ".op\n.end\n";
  const ResultatSimulation rm = simuler(netm);
  REQUIRE(rm.reussi());
  const auto* id = rm.reel("vd#branch");
  REQUIRE(id);
  CHECK(0.05 / std::abs(id->back()) == Approx(0.05).epsilon(0.05));
}

TEST_CASE("11.12 · tampon de sortie IBIS, coin typique, sur la charge d'essai du fichier : front simulé à 5 % de sa table", "[034][L11][11.12]") {
  std::string erreur;
  const auto f = lireIbis(donnee11("ibis/ibis_v5_1.ibs"), &erreur);
  INFO(erreur);
  REQUIRE(f);
  CHECK(f->composant == "Virtual");
  CHECK(f->fabricant == "KiCad");
  CHECK(f->broches.size() == 4);
  const ModeleIbis* m = f->modele("AC40");
  REQUIRE(m);
  CHECK(m->genre == "Output");
  CHECK(m->tension[0] == Approx(1.35));
  CHECK(m->pullup.x.size() == 4);
  CHECK(m->pulldown.x.size() == 4);
  REQUIRE(m->montees.size() == 2);
  CHECK(m->montees[0].rFixture == Approx(50));
  CHECK(m->montees[1].vFixture == Approx(1.35));
  CHECK(m->montees[0].table.x.size() == 5);  // le [Composite Current] qui suit n'y entre pas
  CHECK(f->modele("B1") == m);               // par la broche
  CHECK(f->modele("DQ") != nullptr);          // par le sélecteur
  OptionsTamponIbis o;
  o.modele = "AC40";
  o.fronts = {1e-9};
  const AjustementModele t = tamponIbis(*f, o, "AC40_TYP");
  INFO(t.texte);
  REQUIRE(t.reussi);
  CHECK(t.texte.find(".subckt AC40_TYP out") != std::string::npos);
  // Verilog-A : module lu, modèle d'article à élément N et bibliothèque OSDI
  const auto va = lireVerilogA("`include \"disciplines.vams\"\nmodule resva(p, n);\ninout p, n;\nelectrical p, n;\nparameter real r = 1k from (0:inf);\nanalog I(p,n) <+ V(p,n)/r;\nendmodule\n");
  REQUIRE(va);
  CHECK(va->nom == "resva");
  CHECK(va->ports == std::vector<std::string>{"p", "n"});
  CHECK(va->parametres.at("r") == "1k");
  std::string raison;
  if (!openvafDisponible(&raison)) CHECK(raison.find("OpenVAF") != std::string::npos);
  CHECK(commandeOpenvaf("resva.va", "resva.osdi").front() == "openvaf");
  Montage11 mv;
  mv.poser("Source_tension", "V1", "1", {{"1", "A"}, {"2", "GND"}});
  ComposantElec* k = mv.poser("R", "R1", "1k", {{"1", "A"}, {"2", "GND"}});
  Article art;
  art.id = "resva";
  art.modeleSpice = modeleVerilogA(*va, "", "modeles-spice/resva.osdi");
  mv.c.articles[art.id] = art;
  k->article = art.id;
  const NetlistSpice nv = netlistSpice(mv.c, AnalyseSpice{});
  CHECK(nv.texte.find("NR1 n_a 0 resva_va") != std::string::npos);
  CHECK(nv.chargements == std::vector<std::string>{"modeles-spice/resva.osdi"});
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  const std::string net = "* tampon IBIS sur la charge d'essai\nX1 out AC40_TYP\nRFIX out 0 50\n" + t.texte + ".tran 5p 11n\n.end\n";
  const ResultatSimulation r = simuler(net);
  for (const auto& l : r.console) UNSCOPED_INFO(l);
  REQUIRE(r.reussi());
  const auto* tt = r.reel("time");
  const auto* vo = r.reel("v(out)");
  REQUIRE((tt && vo));
  // La table, décalée du front de l'entrée, comparée dans une bande de 5 % de l'excursion
  const TableIbis& w = m->montees[0].table;
  std::vector<double> xr, yr;
  for (std::size_t i = 0; i < w.x.size(); ++i) xr.push_back(w.x[i] + 1e-9), yr.push_back(w.typ[i]);
  const double excursion = *std::max_element(yr.begin(), yr.end()) - *std::min_element(yr.begin(), yr.end());
  const Comparaison c = comparerCourbes(*tt, *vo, xr, yr, 0.05 * excursion, GenreBande::Absolue);
  INFO("écart maximal " << c.ecartMax << " V à " << c.xPire << " s");
  CHECK(c.reussie);
}

TEST_CASE("11.13 · V(sortie)/V(entrée) en dB tracée par expression sur le filtre RC : −3,0 dB à 1/(2πRC)", "[034][L11][11.13]") {
  const Montage11 m = filtre(1000, 100e-9);
  const NetlistSpice n = netlistSpice(m.c, alternatif(10, 1e6, 100));
  // Expressions évaluées sans moteur sur un résultat écrit à la main
  ResultatSimulation main;
  main.echelle = "frequency";
  main.reels["frequency"] = {1, 2, 3};
  main.complexes["v(n_sortie)"] = {{1, 0}, {0, 1}, {0.5, 0.5}};
  main.complexes["v(n_entree)"] = {{1, 0}, {1, 0}, {1, 0}};
  const CourbeCalculee c0 = evaluerCourbe("mag(V(SORTIE) - V(ENTREE))", main, n);
  REQUIRE(c0.valide());
  CHECK(c0.reels()[1] == Approx(std::sqrt(2.0)));
  CHECK(!evaluerCourbe("inconnue(V(SORTIE))", main, n).valide());
  CHECK(!evaluerCourbe("V(NULLEPART)", main, n).valide());
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  const ResultatSimulation r = simuler(n);
  REQUIRE(r.reussi());
  const CourbeCalculee c = evaluerCourbe("db(V(SORTIE)/V(ENTREE))", r, n);
  REQUIRE(c.valide());
  const double fc = 1 / (2 * std::numbers::pi * 1000 * 100e-9);
  CHECK(valeurA(c.x, c.reels(), fc) == Approx(-3.0).margin(0.05));
  const CourbeCalculee ph = evaluerCourbe("ph(V(SORTIE))", r, n);
  CHECK(valeurA(ph.x, ph.reels(), fc) == Approx(-45).margin(0.5));
  // CSV : écrit puis relu, mêmes vecteurs (complexes en module et phase)
  const std::string csv = csvResultat(r, {"v(n_sortie)"});
  const auto f = resultatDepuisCsv(csv, "csv");
  REQUIRE(f);
  const ResultatSimulation relu = resultatDe(*f);
  REQUIRE(relu.complexe("v(n_sortie)"));
  CHECK(std::abs(relu.complexe("v(n_sortie)")->at(50) - r.complexe("v(n_sortie)")->at(50)) < 1e-6);
}

TEST_CASE("11.14 · classeur de trois analyses enregistré puis rouvert : mêmes réglages, mêmes courbes figées", "[034][L11][11.14]") {
  Montage11 m = filtre();
  ClasseurSimulation k;
  k.parametres = {{"Rcharge", "47", "Ω"}};
  OngletClasseur repos;
  repos.nom = "Point de repos";
  repos.analyse.type = AnalyseSpice::Type::PointDeRepos;
  OngletClasseur ac;
  ac.nom = "Réponse en fréquence";
  ac.analyse = alternatif();
  ac.sondes = {"SORTIE"};
  ac.expressions = {"db(V(SORTIE)/V(ENTREE))"};
  ac.mesures = {{GenreMesure::Coupure, "SORTIE", 0}};
  ac.reference = "Référence";
  ac.courbeReference = "SORTIE";
  ac.bande = 0.01;
  ac.options.moteur = {{"reltol", "0.001"}};
  OngletClasseur tr;
  tr.nom = "Échelon";
  tr.analyse.type = AnalyseSpice::Type::Transitoire;
  tr.analyse.pas = 1e-6, tr.analyse.duree = 1e-3;
  tr.options.conditionsInitiales = {{"SORTIE", 0}};
  tr.options.composants = {m.uuid("R1")};
  tr.options.liaisons = {{"ENTREE", BouclageLiaison::Source, 1, 0}};
  k.onglets = {repos, ac, tr};
  ResultatSimulation r;
  r.echelle = "frequency";
  r.reels["frequency"] = {10, 100, 1000};
  r.complexes["v(n_sortie)"] = {{1, 0}, {0.99, -0.06}, {0.5, -0.5}};
  k.figes.push_back(figer(r, netlistSpice(m.c, alternatif()), "Référence", "Réponse en fréquence", "2026-10-25"));
  ecrireClasseur(m.c, k);
  // Enregistré avec la conception, relu « sur un autre poste »
  const ConceptionElec relue = conceptionDepuisJson(versJson(m.c));
  std::vector<std::string> alertes;
  const ClasseurSimulation k2 = classeurDe(relue, &alertes);
  CHECK(alertes.empty());
  CHECK(k2 == k);
  CHECK(k2.onglets.size() == 3);
  REQUIRE(k2.fige("Référence"));
  CHECK(k2.fige("Référence")->complexes.at("v(n_sortie)") == r.complexes.at("v(n_sortie)"));
  // Format plus récent : lu au mieux, signalé ; format 0 (un seul onglet en tête) migré
  auto j = classeurVersJson(k);
  j["format"] = FORMAT_CLASSEUR + 1;
  classeurDepuisJson(j, &alertes);
  CHECK(alertes.size() == 1);
  nlohmann::ordered_json ancien = {{"nom", "Ancien"}, {"analyse", {{"type", "alternatif"}, {"fDebut", 5}}}};
  const ClasseurSimulation k0 = classeurDepuisJson(ancien);
  REQUIRE(k0.onglets.size() == 1);
  CHECK(k0.onglets[0].analyse.type == AnalyseSpice::Type::Alternatif);
  CHECK(k0.onglets[0].analyse.fDebut == 5);
}

TEST_CASE("11.15 · filtre RC comparé à sa référence dans une bande de 1 % : réussite ; R1 augmentée de 10 % : échec, écart surligné", "[034][L11][11.15]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  const Montage11 m = filtre(1000, 100e-9);
  const NetlistSpice n = netlistSpice(m.c, alternatif());
  const ResultatSimulation r = simuler(n);
  REQUIRE(r.reussi());
  // Mesures automatiques
  const ResultatMesure fc = mesurer({GenreMesure::Coupure, "SORTIE", 0}, r, n);
  REQUIRE(fc.valide);
  CHECK(fc.valeur == Approx(1591.55).epsilon(0.005));
  CHECK(fc.unite == "Hz");
  const ResultatFige ref = figer(r, n, "Référence", "AC");
  auto courbe = [](const ResultatSimulation& x, const NetlistSpice& nn) { return evaluerCourbe("mag(V(SORTIE))", x, nn); };
  const CourbeCalculee cr = courbe(resultatDe(ref), noeudsDe(ref));
  // Même filtre : réussi
  const ResultatSimulation r2 = simuler(netlistSpice(m.c, alternatif()));
  const CourbeCalculee c2 = courbe(r2, n);
  const Comparaison ok = comparerCourbes(c2.x, c2.reels(), cr.x, cr.reels(), 0.01);
  CHECK(ok.reussie);
  CHECK(ok.verdict() == "réussi");
  // R1 augmentée de 10 % : échec, tronçons hors de la bande
  Montage11 m3 = filtre(1100, 100e-9);
  const NetlistSpice n3 = netlistSpice(m3.c, alternatif());
  const ResultatSimulation r3 = simuler(n3);
  REQUIRE(r3.reussi());
  const CourbeCalculee c3 = courbe(r3, n3);
  const Comparaison ko = comparerCourbes(c3.x, c3.reels(), cr.x, cr.reels(), 0.01);
  CHECK(!ko.reussie);
  CHECK(ko.verdict() == "échoué");
  REQUIRE(!ko.horsBande.empty());
  CHECK(ko.ecartMax > 0.05);
  CHECK(c3.x[ko.horsBande.front().first] > 100);  // hors de la bande autour et au-dessus de la coupure
  // Transitoire : montée de 10 à 90 % d'un RC = 2,2 RC
  AnalyseSpice t;
  t.type = AnalyseSpice::Type::Transitoire;
  t.pas = 1e-6, t.duree = 1e-3;
  Montage11 e;
  e.poser("Source_tension", "V1", "PULSE(0 1 10u 1n 1n 1 2)", {{"1", "ENTREE"}, {"2", "GND"}});
  e.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  e.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
  const NetlistSpice ne = netlistSpice(e.c, t);
  const ResultatSimulation re = simuler(ne);
  REQUIRE(re.reussi());
  const ResultatMesure montee = mesurer({GenreMesure::Montee, "SORTIE", 0}, re, ne);
  REQUIRE(montee.valide);
  CHECK(montee.valeur == Approx(2.2e-4).epsilon(0.03));
}

TEST_CASE("11.17 · 600 V en couche interne : 0,50 mm (IPC-2221, table 6-1) ; piste et via choisies dans l'inspecteur", "[034][L11][11.17]") {
  const auto* iso = calculateurElectronique("isolement");
  REQUIRE(iso);
  CHECK(ligne(executer(*iso, {600, 0, 1, 2}), "IPC-2221") == "0,50 mm");
  ConceptionElec c = nouvelleConception("piste");
  Piste p;
  p.a = ptMm(0, 0), p.b = ptMm(50, 0), p.largeur = 250'000;
  const ProprietesCuivre x = proprietesPiste(c, p);
  CHECK(x.longueur == Approx(50));
  CHECK(x.resistance == Approx(0.05 / 58e6 / (0.25e-3 * 35e-6)).epsilon(0.01));
  CHECK(x.delai > 2e-10);
  CHECK(x.courantAdmissible > 0.5);
  CHECK(!x.resume().empty());
  Via v;
  const ProprietesCuivre y = proprietesVia(c, v);
  CHECK(y.longueur == Approx(1.6));
  CHECK(y.resistance > 0);
  CHECK(y.inductance > 0);
  const auto* via = calculateurElectronique("via");
  CHECK(!ligne(executer(*via), "Résistance").empty());
  CHECK(ligne(executer(*calculateurElectronique("fusion")), "fusion").find("A") != std::string::npos);
}

TEST_CASE("11.18 · anneaux jaune, violet, rouge, or : 4,7 kΩ à 5 % ; et l'inverse ; atténuateur de 10 dB sous 50 Ω", "[034][L11][11.18]") {
  double ohms = 0, tol = 0;
  REQUIRE(valeurDesAnneaux({"jaune", "violet", "rouge", "or"}, ohms, tol));
  CHECK(ohms == Approx(4700));
  CHECK(tol == Approx(0.05));
  CHECK(anneauxDe(4700, 0.05) == std::vector<std::string>{"jaune", "violet", "rouge", "or"});
  CHECK(anneauxDe(10, 0.01, 5) == std::vector<std::string>{"marron", "noir", "noir", "or", "marron"});
  CHECK(!valeurDesAnneaux({"or", "violet", "rouge", "or"}, ohms, tol));
  const auto* coul = calculateurElectronique("couleurs");
  REQUIRE(coul);
  CHECK(ligne(executer(*coul, {0, 4, 7, 2, 10, 1}), "Valeur").find("4,7") != std::string::npos);
  const Attenuateur pi = attenuateurPi(10, 50), t = attenuateurT(10, 50);
  CHECK(pi.parallele == Approx(96.2).margin(0.05));
  CHECK(pi.serie == Approx(71.2).margin(0.05));
  CHECK(t.serie == Approx(26.0).margin(0.05));
  CHECK(t.parallele == Approx(35.1).margin(0.05));
  // Couple galvanique : cuivre et aluminium dépassent 0,25 V
  const auto* gal = calculateurElectronique("potentiels");
  CHECK(!executer(*gal, {3, 9, 0.25}).avis.empty());
}

TEST_CASE("11.19 · veille à 20 µA pendant 59 s et mesure à 15 mA pendant 1 s sur 2 000 mAh : 7 416 h, part de chaque mode", "[034][L11][11.19]") {
  const Autonomie a = autonomie({{20e-6, 59}, {15e-3, 1}}, 2000);
  CHECK(a.courantMoyen == Approx(269.67e-6).epsilon(1e-4));
  CHECK(std::floor(a.heures) == 7416);
  REQUIRE(a.parts.size() == 2);
  CHECK(a.parts[1] == Approx(0.927).margin(0.001));
  const Autonomie b = autonomie({{20e-6, 59}, {15e-3, 1}}, 1000);
  CHECK(std::floor(b.heures / 24) == 154);
  const auto* aut = calculateurElectronique("autonomie");
  REQUIRE(aut);
  const auto r = executer(*aut, {0.02, 59, 15, 1, 0, 0, 2000, 0, 100});
  CHECK(ligne(r, "hors autodécharge").rfind("7416", 0) == 0);
  CHECK(ligne(r, "Part du mode 2").rfind("92,7", 0) == 0);
  const auto* th = calculateurElectronique("thermique");
  CHECK(ligne(executer(*th), "jonction").find("°C") != std::string::npos);
}

