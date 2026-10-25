// Prométhée : essais du lot L16 de la 0.34 (platine d'essai synchronisée avec le schéma, connexions et montage posé
// de lui-même, impression et scène 3D, documents de câblage et guitare). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/archive.hpp"
#include "promethee/electronique.hpp"
#include "promethee/enseignement.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/platine.hpp"
#include "promethee/processus.hpp"
#include "promethee/simulation.hpp"
#include "promethee/usinage.hpp"

using namespace prom;
using namespace prom::ecad;
using Catch::Approx;

namespace {
struct Montage16 {
  ConceptionElec c = nouvelleConception("Montage");
  double x = 25.4, y = 50.8;
  std::string poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, y));
    x += 45.72;
    if (x > 260) x = 25.4, y += 60.96;
    std::string uuid;
    for (const auto& p : std::vector<SymbolePlace>(c.feuilles[0].symboles))
      if (p.uuid == s) {
        ComposantElec* r = c.composant(p.composant);
        r->reference = ref;
        if (!valeur.empty()) r->valeur = valeur;
        uuid = r->uuid;
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
    return uuid;
  }
};
// Trou libre (ni patte ni bout de fil) du nœud d'un trou
std::optional<Trou> trouLibreDuNoeud(const VuePlatine& v, const Trou& t) {
  std::set<Trou> occupes;
  for (const auto& p : v.poses)
    for (const auto& [n, x] : p.broches) occupes.insert(x);
  for (const auto& f : v.fils) occupes.insert(f.a), occupes.insert(f.b);
  for (const auto& u : trousDuPotentiel(v, t))
    if (!occupes.count(u)) return u;
  return std::nullopt;
}
const ComposantElec* parRef(const ConceptionElec& c, const std::string& ref) { return c.parReference(ref); }
}  // namespace

TEST_CASE("16.2 · fil ajouté sur la platine entre R1 et la LED : la liaison passe au schéma et au chevelu, en un pas", "[034][L16][16.2]") {
  Montage16 m;
  const std::string r1 = m.poser("R", "R1", "330", {{"1", "+5V"}});
  const std::string d1 = m.poser("LED", "D1", "rouge", {{"1", "GND"}});
  mettreAJourCircuit(m.c);
  const VuePlatine v = monterSurPlatine(m.c);
  ecrirePlatine(m.c, v);
  REQUIRE(platineDe(m.c) == v);
  REQUIRE(v.poses.size() == 2);
  const Connectivite k0 = connectivite(m.c);
  const int a0 = k0.reseau(r1, "2"), b0 = k0.reseau(d1, "2");
  CHECK((a0 < 0 || a0 != b0));
  // Le fil part d'un trou libre de la bande de R1.2 et arrive dans celle de l'anode de D1
  const auto ta = trouLibreDuNoeud(v, v.pose(r1)->broches.at("2"));
  const auto tb = trouLibreDuNoeud(v, v.pose(d1)->broches.at("2"));
  REQUIRE(ta);
  REQUIRE(tb);
  FilPlatine f;
  f.a = *ta;
  f.b = *tb;
  f.couleur = "#FBC02D";
  std::string erreur;
  ConceptionElec apres = m.c;
  const std::string nom = ajouterFilPlatine(apres, f, &erreur);
  INFO(erreur);
  REQUIRE(!nom.empty());
  // Au schéma : les deux broches sur un même réseau
  const Connectivite k1 = connectivite(apres);
  REQUIRE(k1.reseau(r1, "2") >= 0);
  CHECK(k1.reseau(r1, "2") == k1.reseau(d1, "2"));
  CHECK(k1.nom(r1, "2") == nom);
  // À la vue : le fil de plus, de la couleur choisie
  const VuePlatine v1 = platineDe(apres);
  REQUIRE(v1.fils.size() == v.fils.size() + 1);
  CHECK(v1.fils.back().couleur == "#FBC02D");
  // Au chevelu du circuit : une liaison de ce réseau entre la pastille 2 de R1 et l'anode de D1
  Pt pr{}, pd{};
  for (const auto& p : pastilles(apres, k1)) {
    if (p.composant == r1 && p.numero == "2") pr = p.pastille.position;
    if (p.composant == d1 && p.numero == "2") pd = p.pastille.position;
  }
  bool vue = false;
  for (const auto& l : etatRoutage(apres, k1, remplirZones(apres, k1)).chevelu)
    vue |= l.reseau == nom && ((l.a == pr && l.b == pd) || (l.a == pd && l.b == pr));
  CHECK(vue);
  // Avant le fil, aucune liaison entre elles au chevelu
  bool avant = false;
  for (const auto& l : etatRoutage(m.c, k0, remplirZones(m.c, k0)).chevelu) avant |= (l.a == pr && l.b == pd) || (l.a == pd && l.b == pr);
  CHECK_FALSE(avant);
  // La platine montée ne montre plus d'écart avec le schéma enrichi
  CHECK(comparerPlatine(apres, v1).empty());
}

TEST_CASE("16.3 · clignotant à NE555 monté de lui-même : aucun écart ; une patte décalée d'un trou : point rouge et écart listé", "[034][L16][16.3]") {
  const ConceptionElec c = exempleElectronique();
  const VuePlatine v = monterSurPlatine(c);
  const ComposantElec* u1 = parRef(c, "U1");
  REQUIRE(u1);
  const PosePlatine* pu = v.pose(u1->uuid);
  REQUIRE(pu);
  // Circuit intégré à cheval sur la rainure : broches 1 à 4 sur f, 5 à 8 sur e, 1 face à 8
  CHECK(pu->broches.at("1").rangee == "f");
  CHECK(pu->broches.at("4").rangee == "f");
  CHECK(pu->broches.at("5").rangee == "e");
  CHECK(pu->broches.at("8").rangee == "e");
  CHECK(pu->broches.at("1").colonne == pu->broches.at("8").colonne);
  CHECK(pu->broches.at("2").colonne == pu->broches.at("1").colonne + 1);
  // Tous les composants du schéma posés, aucun écart, toutes les pattes vertes
  for (const auto& x : c.composants)
    if (x.genre == GenreComposant::Standard && !x.reference.empty() && x.reference[0] != '#' && x.reference.rfind("H", 0) != 0) {
      bool auSchema = false;
      for (const auto& s : c.feuilles[0].symboles) auSchema |= s.composant == x.uuid;
      if (auSchema) CHECK(v.pose(x.uuid) != nullptr);
    }
  const auto ecarts = comparerPlatine(c, v);
  for (const auto& e : ecarts) UNSCOPED_INFO(e.message);
  CHECK(ecarts.empty());
  for (const auto& [b, etat] : etatsPattes(c, v)) {
    INFO(b.first << "." << b.second);
    CHECK(etat != EtatPatte::Rouge);
  }
  // Étape par étape : le premier pas pose le circuit intégré seul
  const VuePlatine e1 = monterSurPlatine(c, 1);
  REQUIRE(e1.poses.size() == 1);
  CHECK(e1.poses[0].composant == u1->uuid);
  // Une patte de R3 décalée d'une colonne, vers un trou libre d'un autre nœud
  const ComposantElec* r3 = parRef(c, "R3");
  REQUIRE(r3);
  VuePlatine faux = v;
  const ConnectivitePlatine kp = connectivitePlatine(v);
  std::set<Trou> occupes;
  for (const auto& p : v.poses)
    for (const auto& [n, t] : p.broches) occupes.insert(t);
  for (const auto& f : v.fils) occupes.insert(f.a), occupes.insert(f.b);
  const Trou avant = v.pose(r3->uuid)->broches.at("1");
  // Le trou voisin dans la colonne d'à côté, ou le plus proche de lui s'il est pris
  std::optional<Trou> apres;
  const std::vector<std::string> moitie = avant.rangee < "f" ? std::vector<std::string>{"a", "b", "c", "d", "e"} : std::vector<std::string>{"f", "g", "h", "i", "j"};
  for (int d : {1, -1}) {
    for (const auto& r : moitie) {
      const Trou t{avant.colonne + d, r};
      if (trouValide(v.modele, t) && !occupes.count(t) && kp.noeudDuTrou(t) != kp.noeudDuTrou(avant)) {
        apres = t;
        break;
      }
    }
    if (apres) break;
  }
  REQUIRE(apres);
  faux.pose(r3->uuid)->broches["1"] = *apres;
  const auto e = comparerPlatine(c, faux);
  REQUIRE(!e.empty());
  bool listee = false;
  for (const auto& x : e) listee |= x.composant == r3->uuid && x.broche == "1" && x.trou == *apres && !x.message.empty();
  CHECK(listee);
  CHECK(etatsPattes(c, faux).at({r3->uuid, "1"}) == EtatPatte::Rouge);
  // Clic maintenu sur un trou : tout son potentiel, les cinq trous de sa demi-bande au moins
  const auto potentiel = trousDuPotentiel(v, pu->broches.at("3"));
  CHECK(potentiel.size() >= 5);
  CHECK(std::find(potentiel.begin(), potentiel.end(), pu->broches.at("3")) != potentiel.end());
  // Lecture et écriture de la vue
  ConceptionElec copie = c;
  ecrirePlatine(copie, v);
  CHECK(platineDe(copie) == v);
  // Plaque à bandes : une coupure sous chaque colonne du NE555 ; HPGL et plan miroir des coupures
  const auto coupures = coupuresBandes(v);
  CHECK(coupures.size() >= 4);
  const std::string hpgl = hpglPlaque(v);
  CHECK(hpgl.rfind("IN;", 0) == 0);
  CHECK(hpgl.find("SP2;") != std::string::npos);
  CHECK(svgCoupures(v).find("coupure(s)") != std::string::npos);
}

TEST_CASE("16.4 · platine imprimée à l'échelle par le chemin du typon, composant déplacé gardant ses liaisons, scène 3D", "[034][L16][16.4]") {
  Montage16 m;
  const std::string sw = m.poser("Interrupteur", "SW1", "", {{"1", "+5V"}, {"2", "CMD"}});
  const std::string r1 = m.poser("R", "R1", "330", {{"1", "CMD"}, {"2", "A"}});
  const std::string d1 = m.poser("LED", "D1", "rouge", {{"2", "A"}, {"1", "GND"}});
  VuePlatine v = monterSurPlatine(m.c);
  REQUIRE(comparerPlatine(m.c, v).empty());
  // Typon de la platine : page à la taille du dessin, règle de 50 mm
  OptionsTypon o;
  const ModeleTypon t = modeleTyponPlatine(v, o);
  CHECK(t.largeur == Approx((63 + 2) * 2.54 + 2 * o.marge));
  CHECK(t.hauteurCarte == Approx(48.26 + 2 * o.marge));
  const std::string svg = svgTypon(t, o, "Platine");
  CHECK(svg.find("50 mm entre les traits") != std::string::npos);
  CHECK(svg.find("width=\"" + std::to_string(static_cast<int>(t.largeur))) != std::string::npos);
  const ModeleTypon tc = modeleTyponPlatine(v, o, true);
  CHECK(tc.miroir);
  // R1 déplacé de trois colonnes : ses pattes et les fils de ses bandes suivent, aucun écart
  VuePlatine d = v;
  std::string erreur;
  const Trou avant = d.pose(r1)->broches.at("1");
  const bool bouge = deplacerSurPlatine(d, r1, 3, 0, &erreur);
  INFO(erreur);
  REQUIRE(bouge);
  CHECK(d.pose(r1)->broches.at("1").colonne == avant.colonne + 3);
  CHECK(comparerPlatine(m.c, d).empty());
  // Refusé hors de la platine
  VuePlatine hors = v;
  CHECK_FALSE(deplacerSurPlatine(hors, r1, 200, 0));
  CHECK(hors == v);
  // Scène 3D : la platine, un corps par composant, LED et interrupteur reconnus, un tube par fil
  const auto scene = scenePlatine3D(m.c, v);
  int corps = 0, fils = 0;
  bool led = false, inter = false;
  for (const auto& x : scene) {
    corps += x.genre == "corps" || x.genre == "led" || x.genre == "interrupteur";
    fils += x.genre == "fil";
    led |= x.genre == "led" && x.composant == d1;
    inter |= x.genre == "interrupteur" && x.composant == sw;
  }
  CHECK(corps == 3);
  CHECK(fils == static_cast<int>(v.fils.size()));
  CHECK(led);
  CHECK(inter);
  CHECK(scene.front().genre == "platine");
}

TEST_CASE("16.6 · sélecteur à cinq positions d'une guitare à trois micros : les cinq combinaisons attendues", "[034][L16][16.6]") {
  ConceptionElec c = exempleGuitareTroisMicros();
  const auto positions = analyserGuitare(c, "SEL1", "J1");
  REQUIRE(positions.size() == 5);
  const std::vector<std::string> attendu{"chevalet seul", "milieu et chevalet en parallèle", "milieu seul", "manche et milieu en parallèle", "manche seul"};
  for (std::size_t i = 0; i < 5; ++i) {
    INFO("position " << i + 1);
    CHECK(positions[i].position == static_cast<int>(i) + 1);
    CHECK(positions[i].texte == attendu[i]);
  }
  CHECK(positions[3].couplage == "parallèle");
  CHECK(positions[3].micros.size() == 2);
  CHECK_FALSE(positions[3].horsPhase);
  // Le micro du milieu câblé à l'envers : ses deux combinaisons deviennent hors phase
  ConceptionElec inverse = c;
  for (auto& e : inverse.feuilles[0].etiquettes) {
    if (e.texte == "S2") e.texte = "TMP";
  }
  const ComposantElec* mic2 = inverse.parReference("MIC2");
  REQUIRE(mic2);
  // Les étiquettes posées au bout des broches de MIC2 : « S2 » (devenue TMP) et « GND » échangées
  for (auto& e : inverse.feuilles[0].etiquettes) {
    if (e.texte != "TMP" && e.texte != "GND") continue;
    for (const auto& sp : inverse.feuilles[0].symboles)
      if (sp.composant == mic2->uuid)
        for (const auto& b : brochesPlacees(sp, *inverse.symbole(sp.symbole))) {
          const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
          const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
          if (e.p == bout) e.texte = e.texte == "TMP" ? "GND" : "S2";
        }
  }
  for (auto& e : inverse.feuilles[0].etiquettes)
    if (e.texte == "TMP") e.texte = "S2";
  const auto p2 = analyserGuitare(inverse, "SEL1", "J1");
  REQUIRE(p2.size() == 5);
  CHECK(p2[1].horsPhase);
  CHECK(p2[3].horsPhase);
  CHECK_FALSE(p2[0].horsPhase);
  CHECK(p2[3].texte == "manche et milieu en parallèle, hors phase");
  // Documents de câblage : plan de chargement et liste des fils
  const VuePlatine v = monterSurPlatine(c);
  const std::string plan = planDeChargement(c, v);
  CHECK(plan.find("SEL1") != std::string::npos);
  CHECK(listeFils(v).size() == v.fils.size());
  CHECK(svgPlatine(c, v).rfind("<svg", 0) == 0);
}

namespace {
// Profondeur de coupe réelle le long des G1 horizontaux sous la surface : minimum et maximum, échantillonnés tous les
// 0,1 mm (la fraise suit la droite programmée)
std::pair<double, double> profondeursLeLongDesSillons(const std::string& gcode, const std::function<double(double, double)>& surface) {
  std::istringstream in(gcode);
  std::string l;
  double x = 0, y = 0, z = 0, mini = 1e9, maxi = -1e9;
  int mode = 0;
  static const std::regex mot(R"(([GXYZ])\s*(-?[0-9]*\.?[0-9]+))");
  while (std::getline(in, l)) {
    const std::string code = l.substr(0, l.find('('));
    double x1 = x, y1 = y, z1 = z;
    for (auto it = std::sregex_iterator(code.begin(), code.end(), mot); it != std::sregex_iterator(); ++it) {
      const char c = (*it)[1].str()[0];
      const double v = std::stod((*it)[2]);
      if (c == 'G' && (v == 0 || v == 1)) mode = static_cast<int>(v);
      if (c == 'X') x1 = v;
      if (c == 'Y') y1 = v;
      if (c == 'Z') z1 = v;
    }
    if (mode == 1 && (x1 != x || y1 != y)) {
      const double lg = std::hypot(x1 - x, y1 - y);
      const int n = std::max(1, static_cast<int>(lg / 0.1));
      for (int i = 0; i <= n; ++i) {
        const double t = static_cast<double>(i) / n, xi = x + (x1 - x) * t, yi = y + (y1 - y) * t, zi = z + (z1 - z) * t;
        const double p = surface(xi, yi) - zi;
        if (p <= 0.02) continue;  // au-dessus de la plaque : pas un sillon
        mini = std::min(mini, p), maxi = std::max(maxi, p);
      }
    }
    x = x1, y = y1, z = z1;
  }
  return {mini, maxi};
}
}  // namespace

TEST_CASE("16.8 · relief palpé de 0,3 mm de flèche au centre : profondeur de coupe constante à 0,01 mm près le long des sillons", "[034][L16][16.8]") {
  const ConceptionElec c = exempleElectronique();
  OptionsUsinage o;
  const ResultatIsolement iso = sillonsIsolement(c, Couche::CuivreDessous, o);
  REQUIRE(!iso.sillons.empty());
  const std::string gcode = gcodeIsolement(c, iso, o);
  const Boite b = boite(c.circuit.contour.polygone());
  const double L = mm(b.largeur()), H = mm(b.hauteur());
  // Plaque bombée : 0,3 mm plus haute au centre qu'aux bords, Z0 pris au premier point palpé (un coin)
  const auto surface = [&](double x, double y) {
    const double u = 2 * x / L - 1, v = 2 * y / H - 1;
    return 0.3 * (1 - u * u) * (1 - v * v);
  };
  OptionsPalpage op;
  op.x0 = 0, op.y0 = 0, op.x1 = L, op.y1 = H, op.nx = 11, op.ny = 9;
  const std::string programme = gcodePalpage(op);
  std::size_t palpages = 0;
  for (std::size_t i = programme.find("G38.2"); i != std::string::npos; i = programme.find("G38.2", i + 1)) ++palpages;
  CHECK(palpages == 99);
  CHECK(programme.find("PROBEOPEN") != std::string::npos);
  // Relevé écrit par la machine en suivant le programme : LinuxCNC (neuf axes) et GRBL (journal de la liaison série)
  std::ostringstream linuxcnc, grbl;
  {
    std::istringstream in(programme);
    std::string l;
    static const std::regex xy(R"(G0 X(-?[0-9.]+) Y(-?[0-9.]+))");
    while (std::getline(in, l)) {
      std::smatch m;
      if (!std::regex_search(l, m, xy)) continue;
      const double x = std::stod(m[1]), y = std::stod(m[2]), z = surface(x, y);
      linuxcnc << x << " " << y << " " << z << " 0 0 0 0 0 0\n";
      grbl << "ok\n[PRB:" << x << "," << y << "," << z << ":1]\n";
    }
  }
  std::string erreur;
  const auto g = lireRelevePalpage(linuxcnc.str(), &erreur);
  INFO(erreur);
  REQUIRE(g);
  CHECK(g->nx == 11);
  CHECK(g->ny == 9);
  CHECK(g->hauteur(L / 2, H / 2) == Approx(0.3).margin(1e-6));
  const auto g2 = lireRelevePalpage(grbl.str());
  REQUIRE(g2);
  CHECK(g2->z == g->z);
  // Sans correction, la profondeur varie de la flèche ; corrigée, elle reste à 0,08 mm à 0,01 mm près
  const auto [min0, max0] = profondeursLeLongDesSillons(gcode, surface);
  CHECK(max0 - min0 > 0.2);
  const std::string corrige = corrigerProfondeur(gcode, *g, 1.0);
  const auto [mini, maxi] = profondeursLeLongDesSillons(corrige, surface);
  INFO("profondeur de " << mini << " à " << maxi << " mm");
  CHECK(mini >= o.profondeur - 0.01);
  CHECK(maxi <= o.profondeur + 0.01);
  // Relevé incomplet refusé
  CHECK_FALSE(lireRelevePalpage("0 0 0\n10 0 0\n0 10 0\n"));
  // Fraise en V : largeur qui suit la profondeur ; nettoyage du cuivre en poche
  CHECK(largeurFraiseV(30, 0.1, 0.1) == Approx(0.1 + 0.2 * std::tan(15 * 3.14159265358979323846 / 180)));
  CHECK(profondeurFraiseV(30, 0.1, largeurFraiseV(30, 0.1, 0.12)) == Approx(0.12));
  const ResultatIsolement poche = nettoyageCuivre(c, Couche::CuivreDessous, o);
  CHECK(poche.sillons.size() > 1);
  CHECK(poche.longueur > 0);
}

namespace {
std::string dossierEssai16(const std::string& nom) {
  const std::filesystem::path p = std::filesystem::temp_directory_path() / ("promethee-l16-" + nom + "-" + nouvelUuid().substr(0, 8));
  std::filesystem::create_directories(p);
  return utf8(p);
}
double tensionRepos(const ConceptionElec& c, const std::string& reseau) {
  AnalyseSpice a;
  a.type = AnalyseSpice::Type::PointDeRepos;
  const NetlistSpice n = netlistSpice(c, a);
  if (!n.valide()) return std::nan("");
  const ResultatSimulation r = simuler(n);
  const auto* v = r.reel(vecteurTension(n, reseau));
  return v && !v->empty() ? v->back() : std::nan("");
}
}  // namespace

TEST_CASE("16.10 · réglages de l'établissement lus et appliqués, recueil d'exemples prêts à simuler", "[034][L16][16.10]") {
  ReglagesEtablissement r;
  r.nom = "Lycée Jean-Moulin";
  r.dossierBibliotheques = "bibliotheques";
  r.profilFabricant = profilsFabricants().front().nom;
  r.interfaceSimplifiee = true;
  r.interfaceVerrouillee = true;
  r.actionsMasquees = {"automatisation.python", "fabrication.publier"};
  r.dossierClasse = "classe";
  const std::string texte = ecrireReglagesEtablissement(r);
  std::string e;
  const auto relu = lireReglagesEtablissement(texte, &e);
  INFO(e);
  REQUIRE(relu);
  CHECK(*relu == r);
  CHECK_FALSE(lireReglagesEtablissement(R"({"profilFabricant": "Fabricant imaginaire"})", &e));
  CHECK(e.find("Fabricant imaginaire") != std::string::npos);
  CHECK_FALSE(lireReglagesEtablissement("{pas du json"));
  CHECK(resoudreDossier("/opt/promethee", "bibliotheques") == "/opt/promethee/bibliotheques");
  CHECK(resoudreDossier("/opt/promethee", "/srv/biblio") == "/srv/biblio");
  // Recueil : six thèmes, chaque exemple expliqué et prêt à simuler
  std::set<std::string> themes;
  for (const auto& x : recueilExemples()) {
    INFO(x.id);
    themes.insert(x.theme);
    CHECK(!x.explication.empty());
    const ConceptionElec c = x.creer();
    CHECK(c.composants.size() >= 2);
    AnalyseSpice a;
    a.type = AnalyseSpice::Type::Transitoire;
    a.pas = 1e-5;
    a.duree = 1e-3;
    const NetlistSpice n = netlistSpice(c, a);
    for (const auto& m : n.erreurs) UNSCOPED_INFO(m);
    CHECK(n.valide());
  }
  CHECK(themes == std::set<std::string>{"bases", "filtres", "amplificateurs", "555", "logique", "alimentations"});
  REQUIRE(exempleCours("filtres-rc"));
}

TEST_CASE("16.11 · consigne de trois étapes écrite sans programmer : chaque étape se valide seule sur le projet de l'élève", "[034][L16][16.11]") {
  Consigne k;
  k.id = "diviseur";
  k.titre = "Le diviseur de tension";
  EtapeConsigne e1, e2, e3;
  e1.titre = "Pose deux résistances de 10 kΩ";
  e1.condition.genre = GenreCondition::ComposantPose;
  e1.condition.symbole = "R";
  e1.condition.valeur = "10k";
  e1.condition.nombre = 2;
  e2.titre = "Relie R2 à R1";
  e2.media = "images/diviseur.png";
  e2.condition.genre = GenreCondition::BrochesReliees;
  e2.condition.a = "R2.2";
  e2.condition.b = "R1.1";
  e3.titre = "Mesure 2,5 V au milieu";
  e3.question = "Quelle tension lis-tu au point MID ?";
  e3.condition.genre = GenreCondition::MesureDansPlage;
  e3.condition.a = "MID";
  e3.condition.min = 2.4;
  e3.condition.max = 2.6;
  k.etapes = {e1, e2, e3};
  // La consigne est une donnée : écrite, relue, rangée dans le projet
  std::string err;
  const auto relue = consigneDepuisTexte(consigneEnTexte(k), &err);
  INFO(err);
  REQUIRE(relue);
  CHECK(*relue == k);
  ConceptionElec prof = nouvelleConception("Professeur");
  ecrireConsignes(prof, {k});
  REQUIRE(consignesDe(prof).size() == 1);
  CHECK(consignesDe(prof)[0] == k);
  // Projet de l'élève, de plus en plus avancé
  Montage16 vide;
  CHECK_FALSE(verifierEtape(e1, vide.c).faite);
  Montage16 poses;
  poses.poser("R", "R1", "10k", {});
  poses.poser("R", "R2", "10k", {});
  CHECK(verifierEtape(e1, poses.c).faite);
  CHECK_FALSE(verifierEtape(e2, poses.c).faite);
  Montage16 relies;
  relies.poser("R", "R1", "10k", {{"1", "MID"}});
  relies.poser("R", "R2", "10k", {{"2", "MID"}});
  CHECK(verifierEtape(e1, relies.c).faite);
  CHECK(verifierEtape(e2, relies.c).faite);
  const ConceptionElec complet = exempleDiviseur();
  CHECK(verifierEtape(e2, complet).faite);
  if (ngspiceDisponible()) {
    const EtatEtape m = verifierEtape(e3, complet);
    INFO(m.raison);
    CHECK(m.faite);
    CHECK_FALSE(verifierEtape(e3, relies.c).faite);
  } else
    WARN("ngspice absent : l'étape de mesure n'est pas vérifiée");
  // Réponse à une question : nombre à 2 % près, texte sans casse
  EtapeConsigne q;
  q.condition.genre = GenreCondition::Reponse;
  q.condition.reponse = "2.5";
  CHECK(verifierEtape(q, complet, "2,5").faite);
  CHECK(verifierEtape(q, complet, "2.52").faite);
  CHECK_FALSE(verifierEtape(q, complet, "3").faite);
  // Parcours « Apprendre » tiré de la consigne : la première étape non faite
  const ParcoursApprendre p = parcoursDepuisConsigne(k);
  REQUIRE(p.etapes.size() == 3);
  CHECK(etapeEnCours(p, vide.c) == 0);
  CHECK(etapeEnCours(p, poses.c) == 1);
  CHECK(conditionsProposees().size() >= 4);
}

TEST_CASE("16.12 · panne cachée « R2 coupée » : la feuille ne change pas, le voltmètre lit 0 V au lieu de 2,5 V ; couche scellée", "[034][L16][16.12]") {
  ConceptionElec c = exempleDiviseur();
  const ConceptionElec juste = c;
  const auto feuilles = c.feuilles;
  CoucheEnseignant e;
  PanneCachee p;
  p.genre = GenrePanne::Coupure;
  p.composant = "R2";
  p.titre = "R2 coupée";
  e.pannes = {p};
  const ComposantElec* r1 = c.parReference("R1");
  REQUIRE(r1);
  e.verrous = {r1->uuid};
  e.restrictions.actionsMasquees = {"simulation.calculateurs"};
  e.restrictions.profilSimulation = {{"reltol", "1e-4"}};
  ecrireCoucheEnseignant(c, e);
  sceller(c, "secret du professeur");
  CHECK(verifierSceau(c, "secret du professeur") == EtatSceau::Intact);
  CHECK(verifierSceau(c, "autre secret") == EtatSceau::Retouche);
  // Les pannes ne se lisent pas en clair dans le fichier
  const std::string fichier = c.extensions.at("enseignant").dump();
  CHECK(fichier.find("R2 coupée") == std::string::npos);
  CHECK(fichier.find("coupure") == std::string::npos);
  REQUIRE(pannesCachees(c).size() == 1);
  CHECK(pannesCachees(c)[0].titre == "R2 coupée");
  // Feuille intacte, netlist en panne
  CHECK(c.feuilles == feuilles);
  AnalyseSpice a;
  a.type = AnalyseSpice::Type::PointDeRepos;
  const NetlistSpice n = netlistSpice(c, a);
  CHECK(n.texte.find("1e12") != std::string::npos);
  CHECK(n.texte.find("reltol=1e-4") != std::string::npos);
  if (ngspiceDisponible()) {
    CHECK(tensionRepos(juste, "MID") == Approx(2.5).margin(0.01));
    CHECK(std::abs(tensionRepos(c, "MID")) < 0.01);
  } else
    WARN("ngspice absent : la mesure du voltmètre n'est pas vérifiée");
  CHECK(c.feuilles == feuilles);
  // Retouche du fichier : visible au relevé
  ConceptionElec triche = c;
  triche.composant(r1->uuid)->valeur = "1k";
  CHECK(verifierSceau(triche, "secret du professeur") == EtatSceau::Retouche);
  ConceptionElec sansPanne = c;
  CoucheEnseignant e2 = *coucheEnseignant(sansPanne);
  e2.pannes.clear();
  ecrireCoucheEnseignant(sansPanne, e2);
  CHECK(verifierSceau(sansPanne, "secret du professeur") == EtatSceau::Retouche);
  // Parties verrouillées refusées avec la raison ; le reste permis
  const std::string raison = refusEnseignant(c, triche);
  CHECK(raison.find("R1") != std::string::npos);
  ConceptionElec permis = c;
  permis.composant(permis.parReference("R2")->uuid)->valeur = "22k";
  CHECK(refusEnseignant(c, permis).empty());
  CHECK_FALSE(refusEnseignant(c, sansPanne).empty());
  CHECK_FALSE(actionPermise(c, "simulation.calculateurs"));
  CHECK(actionPermise(c, "schema.fil"));
  // Court-circuit et valeur fausse
  ConceptionElec cc = juste;
  CoucheEnseignant e3;
  PanneCachee court;
  court.genre = GenrePanne::CourtCircuit;
  court.a = "MID";
  court.b = "GND";
  PanneCachee fausse;
  fausse.genre = GenrePanne::ValeurFausse;
  fausse.composant = "R1";
  fausse.valeur = "30k";
  e3.pannes = {fausse};
  ecrireCoucheEnseignant(cc, e3);
  if (ngspiceDisponible()) CHECK(tensionRepos(cc, "MID") == Approx(3.75).margin(0.01));
  e3.pannes = {court};
  ecrireCoucheEnseignant(cc, e3);
  if (ngspiceDisponible()) CHECK(std::abs(tensionRepos(cc, "MID")) < 0.01);
}

TEST_CASE("16.13 · montage d'élève équivalent aux repères différents : reconnu juste ; une résistance en trop : l'écart est signalé", "[034][L16][16.13]") {
  const ConceptionElec reference = exempleDiviseur();
  Montage16 eleve;
  eleve.poser("R", "R9", "10k", {{"2", "M"}, {"1", "+5V"}});
  eleve.poser("Pile", "B2", "5V", {{"1", "+5V"}, {"2", "GND"}});
  eleve.poser("R", "R7", "10k", {{"1", "GND"}, {"2", "M"}});
  const ResultatEvaluation r = evaluerMontage(eleve.c, reference);
  for (const auto& x : r.ecarts) UNSCOPED_INFO(x);
  CHECK(r.juste);
  CHECK(r.correspondance.at("B2") == "BT1");
  CHECK((r.correspondance.at("R9") == "R2" || r.correspondance.at("R9") == "R1"));
  Montage16 trop = eleve;
  trop.poser("R", "R3", "10k", {{"1", "M"}, {"2", "GND"}});
  const ResultatEvaluation t = evaluerMontage(trop.c, reference);
  CHECK_FALSE(t.juste);
  REQUIRE(!t.ecarts.empty());
  CHECK(t.ecarts.front().find("résistance de trop") != std::string::npos);
  CHECK(t.ecarts.front().find("R3") != std::string::npos);
  // Mêmes composants, autre câblage : la pile en court-circuit
  Montage16 faux;
  faux.poser("R", "R1", "10k", {{"1", "+5V"}, {"2", "M"}});
  faux.poser("R", "R2", "10k", {{"1", "M"}, {"2", "+5V"}});
  faux.poser("Pile", "BT1", "5V", {{"1", "+5V"}, {"2", "GND"}});
  const ResultatEvaluation f = evaluerMontage(faux.c, reference);
  CHECK_FALSE(f.juste);
  // Formules et nombres
  CHECK(formulesEquivalentes("R1*R2/(R1+R2)", "1/(1/R1+1/R2)"));
  CHECK_FALSE(formulesEquivalentes("R1+R2", "1/(1/R1+1/R2)"));
  CHECK(nombreJuste("2,5", 2.5));
  CHECK_FALSE(nombreJuste("2,8", 2.5));
}

TEST_CASE("16.14 · page HTML d'un filtre RC ouverte hors ligne : la simulation tourne et donne la coupure à 1 kHz ; classe sans service en ligne", "[034][L16][16.14]") {
  const ConceptionElec c = exempleFiltreRC();
  std::string erreur;
  const auto el = circuitLineaire(c, &erreur);
  INFO(erreur);
  CHECK(erreur.empty());
  CHECK(el.size() == 3);
  const std::string page = pageHtmlCircuit(c);
  CHECK(page.find("<script id=\"moteur\">") != std::string::npos);
  CHECK(page.find("<svg id=\"schema\"") != std::string::npos);
  // Aucune ressource extérieure : ni src ni href vers le réseau, ni requête
  for (const char* interdit : {"src=\"http", "href=\"http", "fetch(", "XMLHttpRequest", "WebSocket", "import("}) CHECK(page.find(interdit) == std::string::npos);
  const std::string d = dossierEssai16("page");
  const std::string chemin = d + "/filtre.html";
  ecrireFichierAtomique(cheminFichier(chemin), page);
  // Le moteur de la page, mené par Node.js s'il est sur le poste
  if (!programmeDuPoste("node").empty()) {
    const std::string harnais = d + "/harnais.js";
    ecrireFichierAtomique(cheminFichier(harnais),
                          "const fs = require('fs');\nconst h = fs.readFileSync(process.argv[2], 'utf8');\n"
                          "const bloc = id => h.split('<script id=\"' + id + '\"')[1].split('>').slice(1).join('>').split('</script>')[0];\n"
                          "const m = new Function('module', bloc('moteur') + '\\nreturn module.exports;')({exports: {}});\n"
                          "const c = JSON.parse(bloc('circuit'));\nconst r = m.simulerCa(c.elements, c.sortie, c.fDebut, c.fFin, 50);\n"
                          "console.log(m.frequenceCoupure(r.frequences, r.gainDb));\n");
    const ResultatProcessus r = executerProcessus({programmeDuPoste("node"), harnais, chemin});
    INFO(r.erreurs);
    REQUIRE(r.reussi());
    CHECK(std::stod(r.sortie) == Approx(1000).epsilon(0.01));
  } else
    WARN("Node.js absent : le moteur de la page n'est pas mené");
  // La page dans un vrai navigateur sans réseau (Chromium par Playwright, s'il est sur le poste)
  const std::string python = programmeDuPoste("python3");
  if (!python.empty() && executerProcessus({python, "-c", "import playwright"}).reussi()) {
    const std::string script = d + "/navigateur.py";
    ecrireFichierAtomique(cheminFichier(script),
                          "import sys\nfrom playwright.sync_api import sync_playwright\nwith sync_playwright() as p:\n"
                          "    b = p.chromium.launch()\n    c = b.new_context(offline=True)\n    g = c.new_page()\n"
                          "    g.goto('file://' + sys.argv[1])\n    g.wait_for_function('document.body.dataset.coupure !== undefined', timeout=20000)\n"
                          "    print(g.evaluate('document.body.dataset.coupure'))\n    print(g.inner_text('#resultat'))\n    b.close()\n");
    const ResultatProcessus r = executerProcessus({python, script, chemin}, OptionsProcessus{{}, {}, 120, {}});
    INFO(r.sortie << r.erreurs);
    if (r.reussi()) {
      CHECK(std::stod(r.sortie.substr(0, r.sortie.find('\n'))) == Approx(1000).epsilon(0.01));
      CHECK(r.sortie.find("1,00 kHz") != std::string::npos);
    } else
      WARN("Chromium ne s'est pas lancé : " << r.erreurs);
  } else
    WARN("Playwright absent : la page n'est pas ouverte dans un navigateur");
  // Classe : devoir publié, rendus déposés et relevés, notes en CSV, paquet IMS Common Cartridge
  const std::string classe = dossierEssai16("classe");
  Devoir dv;
  dv.id = "filtre-rc";
  dv.titre = "Filtre RC";
  dv.consigne = "Monte le filtre et mesure sa coupure.";
  dv.echeance = "2026-11-14T18:00";
  dv.interdits = {"simulation.calculateurs"};
  REQUIRE(publierDevoir(classe, dv, "{\"projet\": 1}"));
  REQUIRE(devoirsPublies(classe).size() == 1);
  CHECK(devoirsPublies(classe)[0].interdits == dv.interdits);
  REQUIRE(deposerRendu(classe, "filtre-rc", "Alice Martin", "{\"a\": 1}", "2026-11-13T09:00"));
  REQUIRE(deposerRendu(classe, "filtre-rc", "Bruno Petit", "{\"b\": 1}", "2026-11-15T10:00"));
  CHECK_FALSE(deposerRendu(classe, "inconnu", "Alice Martin", "{}", "2026-11-13T09:00"));
  REQUIRE(noter(classe, "filtre-rc", "Alice Martin", 15.5, "coupure juste"));
  const auto rendus = releverRendus(classe, "filtre-rc");
  REQUIRE(rendus.size() == 2);
  CHECK(rendus[0].eleve == "Alice Martin");
  CHECK(rendus[0].note == 15.5);
  CHECK_FALSE(rendus[0].enRetard);
  CHECK(rendus[1].enRetard);
  const std::string csv = notesCsv(rendus, "filtre-rc");
  CHECK(csv.find("Alice Martin;filtre-rc;15,5;coupure juste") != std::string::npos);
  const auto zip = lireZip(paquetCommonCartridge(dv, "{\"projet\": 1}"));
  bool manifeste = false;
  for (const auto& [nom, contenu] : zip) manifeste |= nom == "imsmanifest.xml" && contenu.find("imsccv1p1") != std::string::npos;
  CHECK(manifeste);
  CHECK(zip.size() == 3);
}
