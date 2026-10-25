// Prométhée : essais de la 0.34, lot L7 côté noyau — hiérarchie (7.2, 7.3), textes riches (7.6, 7.11), symboles
// (7.7, 7.18, 7.20), échange de portes (7.8), empreintes (7.9, 7.21, 7.24), renvois (7.10), faisceaux sur deux niveaux
// (7.13), directives (7.14), réutilisation (7.16), formes importées (7.19), jeux de styles (7.25).
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electronique.hpp"
#include "promethee/empreintes_edition.hpp"
#include "promethee/gabarits_ipc.hpp"
#include "promethee/gestion_bibliotheque.hpp"
#include "promethee/reutilisation.hpp"
#include "promethee/schema_avance.hpp"

using namespace prom::ecad;
using Catch::Approx;

namespace {
int compter(const std::vector<Violation>& v, const std::string& code) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
}
Feuille feuille(const std::string& nom, const std::string& format = "A4") {
  Feuille f;
  f.uuid = nouvelUuid();
  f.nom = nom;
  f.format = format;
  return f;
}
// Extrémité d'une broche d'un symbole posé
Pt broche(const ConceptionElec& c, const std::string& symbolePlace, const std::string& numero) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == symbolePlace)
        for (const auto& b : brochesPlacees(s, *c.symbole(s.symbole)))
          if (b.numero == numero) return b.p;
  return {};
}
std::string composantDe(const ConceptionElec& c, const std::string& symbolePlace) {
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == symbolePlace) return s.composant;
  return {};
}
std::string etiquette(ConceptionElec& c, std::size_t f, Pt p, const std::string& t, TypeEtiquette type = TypeEtiquette::Locale) {
  Etiquette e;
  e.uuid = nouvelUuid();
  e.texte = t;
  e.p = p;
  e.type = type;
  c.feuilles[f].etiquettes.push_back(e);
  return e.uuid;
}
// Réseaux d'au moins « min » broches : nom → broches (« composant:numéro »), triées
std::map<std::string, std::set<std::string>> reseaux(const ConceptionElec& c, std::size_t min = 2) {
  std::map<std::string, std::set<std::string>> r;
  for (const auto& x : connectivite(c).reseaux) {
    if (x.broches.size() < min) continue;
    for (const auto& b : x.broches) r[x.nom].insert(b.composant + ":" + b.broche);
  }
  return r;
}
const EmpreintePlacee* placee(const ConceptionElec& c, const std::string& composant) {
  for (const auto& e : c.circuit.empreintes)
    if (e.composant == composant) return &e;
  return nullptr;
}
}  // namespace

TEST_CASE("7.2, 7.3 · feuilles à plat migrées en hiérarchie : réseaux identiques ; port ajouté dans la fille proposé au bloc", "[034][L7][7.2][7.3]") {
  ConceptionElec c = nouvelleConception("Migration");
  c.feuilles[0].nom = "Logique";
  c.feuilles.push_back(feuille("Alimentation"));
  const std::string r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
  const std::string r2 = poserSymbole(c, 1, "R", ptMm(50.8, 50.8));
  const std::string r3 = poserSymbole(c, 1, "R", ptMm(101.6, 50.8));
  etiquette(c, 0, broche(c, r1, "1"), "CLK");
  etiquette(c, 1, broche(c, r2, "1"), "CLK");
  etiquette(c, 0, broche(c, r1, "2"), "DATA", TypeEtiquette::Globale);
  etiquette(c, 1, broche(c, r3, "2"), "DATA", TypeEtiquette::Globale);
  etiquette(c, 1, broche(c, r2, "2"), "N1");
  etiquette(c, 1, broche(c, r3, "1"), "N1");
  const auto avant = reseaux(c, 1);
  REQUIRE(avant.size() == 4);  // /Logique/CLK, /Alimentation/CLK, DATA, N1
  CHECK(avant.count("/Logique/CLK"));
  const std::size_t n = c.feuilles.size();
  CHECK(hierarchiserFeuilles(c) == 0);
  REQUIRE(c.feuilles.size() == n + 1);
  CHECK(c.feuilles[0].nom == "Racine");
  CHECK(c.feuilles[0].blocs.size() == n);
  // Mêmes réseaux, mêmes noms : la racine ne s'écrit pas dans le chemin de ses filles
  CHECK(reseaux(c, 1) == avant);
  CHECK(filAriane(c, 2) == std::vector<std::size_t>{0, 2});
  CHECK(cheminInstance(c, 2) == "/Racine/Alimentation");
  REQUIRE(blocParent(c, 1).has_value());
  CHECK(blocParent(c, 1)->first == 0);
  CHECK(verifierPortsHierarchiques(c).empty());

  // 7.3 : une étiquette hiérarchique ajoutée dans la fille est proposée au bloc (ERC-33), puis synchronisée
  const std::string bloc = c.feuilles[0].blocs[1].uuid;  // bloc de « Alimentation »
  etiquette(c, 2, broche(c, r2, "2"), "VOUT", TypeEtiquette::Hierarchique);
  const EcartPorts e = ecartPorts(c, 0, bloc);
  CHECK(e.manquants == std::vector<std::string>{"VOUT"});
  CHECK(e.orphelins.empty());
  CHECK(compter(verifierPortsHierarchiques(c), "ERC-33") == 1);
  CHECK(compter(verifierERC(c, connectivite(c)), "ERC-33") == 1);
  CHECK(synchroniserPorts(c, 0, bloc) == 1);
  CHECK(ecartPorts(c, 0, bloc).vide());
  CHECK(verifierPortsHierarchiques(c).empty());
  const BlocFeuille& b = c.feuilles[0].blocs[1];
  REQUIRE(b.ports.size() == 1);
  CHECK(b.ports[0].p.x == b.position.x);  // bord gauche
  // Le port relie la mère à la fille : un fil de la racine posé sur le port rejoint VOUT
  const std::string r4 = poserSymbole(c, 0, "R", ptMm(12.7, 127));
  c.feuilles[0].fils.push_back({nouvelUuid(), broche(c, r4, "1"), b.ports[0].p});
  bool relie = false;
  for (const auto& [nom, v] : reseaux(c))
    if (v.count(composantDe(c, r4) + ":1") && v.count(composantDe(c, r2) + ":2")) relie = true;
  CHECK(relie);
  // Un port retiré de la fille devient orphelin, retiré sur demande
  std::erase_if(c.feuilles[2].etiquettes, [](const Etiquette& x) { return x.texte == "VOUT"; });
  CHECK(ecartPorts(c, 0, bloc).orphelins == std::vector<std::string>{"VOUT"});
  CHECK(synchroniserPorts(c, 0, bloc, true) == 1);
  CHECK(c.feuilles[0].blocs[1].ports.empty());
}

TEST_CASE("7.6, 7.11 · ${REVISION} suit le cartouche ; @{${LARGEUR}*2} vaut 120 puis 140 ; textes marqués et variables sans valeur", "[034][L7][7.6][7.11]") {
  ConceptionElec c = nouvelleConception("Textes");
  c.cartouche.revision = "A";
  CHECK(texteAffiche("Rév. ${REVISION}", c, 0) == "Rév. A");
  c.cartouche.revision = "B";
  CHECK(texteAffiche("Rév. ${REVISION}", c, 0) == "Rév. B");
  c.cartouche.champs["CLIENT"] = "Atelier Nord";
  CHECK(texteAffiche("${CLIENT} — ${FEUILLE} ${PAGE}/${PAGES}", c, 0) == "Atelier Nord — " + c.feuilles[0].nom + " 1/1");
  // La feuille surcharge la conception
  c.variables["TENSION"] = "5 V";
  CHECK(texteAffiche("${TENSION}", c, 0) == "5 V");
  c.feuilles[0].variables["TENSION"] = "3,3 V";
  CHECK(texteAffiche("${TENSION}", c, 0) == "3,3 V");
  CHECK(texteAffiche("${TENSION}", c, -1) == "5 V");
  // 7.11 : expression après les variables
  c.variables["LARGEUR"] = "60";
  CHECK(texteAffiche("@{${LARGEUR}*2}", c, 0) == "120");
  c.variables["LARGEUR"] = "70";
  CHECK(texteAffiche("@{${LARGEUR}*2}", c, 0) == "140");
  // Styles : exposant, indice, barre, lien
  const TexteResolu r = resoudreTexte("V_{CC} 10^{3} ~{RESET} [[https://exemple.org|fiche]]", c, 0);
  CHECK(r.resolu == "VCC 103 RESET fiche");
  REQUIRE(r.morceaux.size() >= 7);
  CHECK(std::any_of(r.morceaux.begin(), r.morceaux.end(), [](const MorceauTexte& m) { return m.style == MorceauTexte::Style::Indice && m.texte == "CC"; }));
  CHECK(std::any_of(r.morceaux.begin(), r.morceaux.end(), [](const MorceauTexte& m) { return m.style == MorceauTexte::Style::Barre && m.texte == "RESET"; }));
  CHECK(std::any_of(r.morceaux.begin(), r.morceaux.end(), [](const MorceauTexte& m) { return m.style == MorceauTexte::Style::Lien && m.cible == "https://exemple.org"; }));
  // ERC-34 (texte marqué) et ERC-35 (variable sans valeur, expression fausse)
  TexteSchema t;
  t.uuid = nouvelUuid();
  t.texte = "${ERC_WARNING valeur de R3 à confirmer}";
  c.feuilles[0].textes.push_back(t);
  t.uuid = nouvelUuid();
  t.texte = "Courant ${COURANT_MAX}";
  c.feuilles[0].textes.push_back(t);
  t.uuid = nouvelUuid();
  t.texte = "@{2*}";
  c.feuilles[0].textes.push_back(t);
  const auto erc = verifierERC(c, connectivite(c));
  CHECK(compter(erc, "ERC-34") == 1);
  CHECK(compter(erc, "ERC-35") == 2);
  // Côté carte : DRC-83 et DRC-84
  TexteCircuit tc;
  tc.uuid = nouvelUuid();
  tc.texte = "${DRC_ERROR fente à valider} ${INCONNUE}";
  c.circuit.textes.push_back(tc);
  const auto v = verifierTextesRiches(c, true);
  CHECK(compter(v, "DRC-83") == 1);
  CHECK(compter(v, "DRC-84") == 1);
}

TEST_CASE("7.7, 7.18 · symbole de 48 broches collé depuis un tableau, contrôles CS sans erreur ; rangée PA0 à PA15 relue à l'identique en CSV", "[034][L7][7.7][7.18]") {
  std::vector<std::vector<std::string>> lignes{{"Numéro", "Nom", "Type", "Côté"}};
  for (int i = 1; i <= 48; ++i)
    lignes.push_back({std::to_string(i), i == 1 ? "VDD" : i == 48 ? "VSS" : "P" + std::to_string(i), i == 1 ? "alim" : i == 48 ? "alim" : i % 2 ? "entree" : "sortie", ""});
  std::vector<std::string> journal;
  const SpecSymbole s = specDepuisTableau(lignes, "MCU48", &journal);
  CHECK(journal.empty());
  REQUIRE(s.broches.size() == 48);
  const DefSymbole d = genererSymbole(s);
  CHECK(d.broches.size() == 48);
  CHECK(controlerSymbole(d).empty());
  // Un numéro en double et une broche hors grille sont relevés
  DefSymbole faux = d;
  faux.broches[3].numero = faux.broches[2].numero;
  faux.broches[5].extremite.x += 100'000;
  const auto cs = controlerSymbole(faux);
  CHECK(std::any_of(cs.begin(), cs.end(), [](const std::string& x) { return x.rfind("CS-02", 0) == 0; }));
  CHECK(std::any_of(cs.begin(), cs.end(), [](const std::string& x) { return x.rfind("CS-01", 0) == 0; }));

  // 7.18 : rangée posée en un geste, CSV aller-retour
  const auto rangee = rangeeBroches("PA", 0, 15, 1, 0);
  REQUIRE(rangee.size() == 16);
  CHECK(rangee.front().nom == "PA0");
  CHECK(rangee.back().nom == "PA15");
  CHECK(rangee.back().numero == "16");
  SpecSymbole sp;
  sp.nom = "PORTA";
  sp.id = "perso:PORTA";
  sp.broches = rangee;
  const DefSymbole port = genererSymbole(sp);
  CHECK(controlerSymbole(port).empty());
  const std::string csv = brochesVersCsv(port);
  const auto relues = brochesDepuisCsv(csv);
  REQUIRE(relues.size() == 16);
  for (std::size_t i = 0; i < 16; ++i) {
    CHECK(relues[i].numero == rangee[i].numero);
    CHECK(relues[i].nom == rangee[i].nom);
    CHECK(relues[i].type == rangee[i].type);
    CHECK(relues[i].cote == 0);
  }
  // Relues, elles redonnent le même symbole
  SpecSymbole sp2 = sp;
  sp2.broches = relues;
  CHECK(genererSymbole(sp2).broches == port.broches);
}

TEST_CASE("7.8 · deux portes d'un 74HC00 échangées : schéma et circuit d'accord", "[034][L7][7.8]") {
  ConceptionElec c = nouvelleConception("Portes");
  const std::string ua = poserSymbole(c, 0, "74HC00_unites", ptMm(50.8, 50.8));
  const std::string k = composantDe(c, ua);
  REQUIRE(!poserUniteEn(c, 0, k, 2, ptMm(101.6, 50.8)).empty());
  std::string ub;
  for (const auto& s : c.feuilles[0].symboles)
    if (s.composant == k && s.unite == 2) ub = s.uuid;
  // Entrée A de la porte A sur IN_A, entrée A de la porte B sur IN_B (avec une résistance pour former le réseau)
  const std::string ra = poserSymbole(c, 0, "R", ptMm(25.4, 101.6));
  const std::string rb = poserSymbole(c, 0, "R", ptMm(76.2, 101.6));
  etiquette(c, 0, broche(c, ua, "1"), "IN_A");
  etiquette(c, 0, broche(c, ra, "1"), "IN_A");
  etiquette(c, 0, broche(c, ub, "4"), "IN_B");
  etiquette(c, 0, broche(c, rb, "1"), "IN_B");
  mettreAJourCircuit(c);
  auto reseauPastille = [&](const std::string& numero) {
    for (const auto& p : pastilles(c, connectivite(c)))
      if (p.composant == k && p.numero == numero) return p.reseau;
    return std::string();
  };
  REQUIRE(reseauPastille("1") == "IN_A");
  REQUIRE(reseauPastille("4") == "IN_B");
  // Une piste arrive sur la pastille 1
  Piste p;
  p.uuid = nouvelUuid();
  p.reseau = "IN_A";
  for (const auto& x : pastilles(c, connectivite(c)))
    if (x.composant == k && x.numero == "1") p.a = x.pastille.position;
  p.b = {p.a.x - nm(5), p.a.y};
  c.circuit.pistes.push_back(p);
  const BilanEchange b = echangerPortes(c, k, 1, 2);
  REQUIRE(b.fait);
  CHECK(b.pistesRetirees == 1);
  // Le schéma : IN_A arrive désormais sur la broche 4 (porte B posée à la place de A) ; le circuit le suit
  const Connectivite kk = connectivite(c);
  CHECK(kk.reseaux[static_cast<std::size_t>(kk.parBroche.at({k, "4"}))].nom == "IN_A");
  CHECK(kk.reseaux[static_cast<std::size_t>(kk.parBroche.at({k, "1"}))].nom == "IN_B");
  CHECK(reseauPastille("4") == "IN_A");
  CHECK(reseauPastille("1") == "IN_B");
  // Deux portes non équivalentes (porte A et unité d'alimentation) ne s'échangent pas
  REQUIRE(!poserUniteEn(c, 0, k, 5, ptMm(150, 50.8)).empty());
  CHECK(!echangerPortes(c, k, 1, 5).fait);
  // Échange de broches : A et B de la porte posée en A
  const BilanEchange e = echangerBroches(c, ua, "4", "5");
  CHECK(e.fait);
  const Connectivite k2 = connectivite(c);
  CHECK(k2.reseaux[static_cast<std::size_t>(k2.parBroche.at({k, "5"}))].nom == "IN_A");
}

TEST_CASE("7.9, 7.21 · empreinte retouchée : contrôles sans erreur, mise à jour sur demande ; BGA 16 × 16 renuméroté de A1 à T16 sans doublon", "[034][L7][7.9][7.21]") {
  // BGA 16 × 16 au pas de 1 mm, numéros quelconques
  DefEmpreinte bga;
  bga.id = "BGA-256";
  for (int r = 0; r < 16; ++r)
    for (int col = 0; col < 16; ++col) {
      Pastille p;
      p.numero = "x";
      p.forme = FormePastille::Rond;
      p.largeur = p.hauteur = 450'000;
      p.position = {nm(col - 7.5), nm(r - 7.5)};
      bga.pastilles.push_back(p);
    }
  CHECK(doublonsPastilles(bga).size() == 1);
  CHECK(renumeroterMatrice(bga) == 256);
  CHECK(doublonsPastilles(bga).empty());
  std::set<std::string> numeros;
  for (const auto& p : bga.pastilles) numeros.insert(p.numero);
  CHECK(numeros.size() == 256);
  CHECK(numeros.count("A1"));
  CHECK(numeros.count("T16"));
  for (const auto& n : numeros) {
    CHECK(n.find_first_of("IOQS") == std::string::npos);
  }
  // Pastille A1 en haut à gauche
  const auto a1 = std::find_if(bga.pastilles.begin(), bga.pastilles.end(), [](const Pastille& p) { return p.numero == "A1"; });
  CHECK(a1->position == Pt{nm(-7.5), nm(-7.5)});
  // Renumérotation en série, création depuis un fichier de coordonnées
  CHECK(renumeroterSerie(bga) == 256);
  CHECK(bga.pastilles.front().numero == "1");
  Pastille modele;
  modele.forme = FormePastille::RectArrondi;
  modele.largeur = 1'500'000;
  modele.hauteur = 600'000;
  std::vector<std::string> journal;
  const DefEmpreinte coord = empreinteDepuisCoordonnees("CONN-4", "numero;x;y\n1;0;0\n2;1,27;0\n3;2,54;0\n4;3,81;0\n", modele, &journal);
  REQUIRE(coord.pastilles.size() == 4);
  CHECK(coord.pastilles[1].position == Pt{1'270'000, 0});
  CHECK(coord.pastilles[1].largeur == 1'500'000);

  // 7.9 : SOIC-8 retouchée (pastille modèle, cote), contrôles propres ; les composants posés suivent sur demande
  ConceptionElec c = nouvelleConception("Retouche");
  const std::string s1 = poserSymbole(c, 0, "NE555", ptMm(50.8, 50.8));
  const std::string s2 = poserSymbole(c, 0, "NE555", ptMm(101.6, 50.8));
  mettreAJourCircuit(c);
  const DefEmpreinte* origine = c.empreinte("SOIC-8");
  REQUIRE(origine != nullptr);
  DefEmpreinte retouchee = *origine;
  Pastille large = retouchee.pastilles.front();
  large.largeur += 200'000;
  CHECK(appliquerPastilleModele(retouchee, large, {"1", "8"}) == 2);
  CHECK(reglerCote(retouchee, "1", "2", 1'270'000));
  CHECK(controlerEmpreinte(retouchee).empty());
  CHECK(composantsDeLEmpreinte(c, "SOIC-8").size() == 2);
  CHECK(mettreAJourEmpreinte(c, retouchee, {composantDe(c, s1)}) == 1);
  CHECK(c.empreinte("SOIC-8")->pastilles.front().largeur == large.largeur);
  (void)s2;
}

TEST_CASE("7.10 · bobine de K1 page 2, deux contacts page 5 : « 5.B3, 5.E3 » écrit sous la bobine", "[034][L7][7.10]") {
  ConceptionElec c = nouvelleConception("Renvois");
  c.feuilles[0].format = "A3";
  for (int i = 2; i <= 5; ++i) c.feuilles.push_back(feuille("Feuille " + std::to_string(i), "A3"));
  // Relais : bobine (unité 1), deux contacts (unités 2 et 3)
  DefSymbole relais;
  relais.id = "RELAIS_2RT";
  relais.nom = "Relais";
  relais.prefixe = "K";
  relais.unites = 3;
  auto b = [&](const std::string& n, int u, Nm y) {
    Broche x;
    x.numero = n;
    x.nom = n;
    x.unite = u;
    x.extremite = {0, y};
    x.direction = y < 0 ? 3 : 1;
    relais.broches.push_back(x);
  };
  b("A1", 1, -nm(5.08));
  b("A2", 1, nm(5.08));
  b("11", 2, -nm(5.08));
  b("14", 2, nm(5.08));
  b("21", 3, -nm(5.08));
  b("24", 3, nm(5.08));
  c.symboles[relais.id] = relais;
  ComposantElec k;
  k.uuid = nouvelUuid();
  k.reference = "K1";
  k.symbole = relais.id;
  c.composants.push_back(k);
  auto poser = [&](std::size_t f, int unite, Pt p) {
    SymbolePlace s;
    s.uuid = nouvelUuid();
    s.symbole = relais.id;
    s.composant = k.uuid;
    s.unite = unite;
    s.position = p;
    c.feuilles[f].symboles.push_back(s);
    return s.uuid;
  };
  // A3 : 8 colonnes de 52,5 mm, 6 lignes de 49,5 mm ; B3 et E3 sur la page 5
  const std::string bobine = poser(1, 1, ptMm(200, 120));
  const std::string c1 = poser(4, 2, ptMm(130, 75));
  const std::string c2 = poser(4, 3, ptMm(130, 220));
  CHECK(zoneDuPoint(c.feuilles[4], ptMm(130, 75)).texte() == "B3");
  CHECK(zoneDuPoint(c.feuilles[4], ptMm(130, 220)).texte() == "E3");
  const auto r = renvoisComposants(c);
  CHECK(r.at(bobine) == "5.B3, 5.E3");
  CHECK(r.at(c1) == "2." + zoneDuPoint(c.feuilles[1], ptMm(200, 120)).texte());
  CHECK(r.at(c2) == r.at(c1));
  // Numéro de page propre et format des pages
  c.feuilles[4].page = "5bis";
  CHECK(renvoisComposants(c).at(bobine) == "5bis.B3, 5bis.E3");
  CHECK(textePage(c, 1) == "2/5");
}

TEST_CASE("7.13 · faisceau de trois réseaux et d'un bus D[0..7] passé sur deux niveaux : onze réseaux reliés, aucun de plus", "[034][L7][7.13]") {
  ConceptionElec c = nouvelleConception("Faisceau");
  c.feuilles[0].nom = "Racine";
  c.feuilles.push_back(feuille("Interface"));
  c.feuilles.push_back(feuille("Memoire"));
  c.aliasBus["CTRL"] = {"SDA", "SCL", "INT", "D[0..7]"};
  const std::vector<std::string> membres{"SDA", "SCL", "INT", "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"};
  // Racine : une résistance par membre, nommée par une étiquette locale ; un bloc vers « Interface », port CTRL
  std::vector<std::string> haut, bas;
  for (std::size_t i = 0; i < membres.size(); ++i) {
    haut.push_back(poserSymbole(c, 0, "R", ptMm(25.4, 25.4 + 12.7 * static_cast<double>(i))));
    etiquette(c, 0, broche(c, haut.back(), "1"), membres[i]);
  }
  // Une étiquette homonyme hors faisceau n'est réunie à rien
  const std::string bruit = poserSymbole(c, 0, "R", ptMm(150, 25.4));
  etiquette(c, 0, broche(c, bruit, "1"), "BRUIT");
  BlocFeuille b1;
  b1.uuid = nouvelUuid();
  b1.nom = "Interface";
  b1.feuille = c.feuilles[1].uuid;
  b1.position = ptMm(100, 50);
  b1.ports.push_back({nouvelUuid(), "CTRL", ptMm(100, 55), 0, SensSignal::Aucun});
  c.feuilles[0].blocs.push_back(b1);
  // Interface : ne nomme pas les membres, passe le faisceau à « Memoire » (étiquette hiérarchique CTRL, bloc)
  etiquette(c, 1, ptMm(50, 50), "CTRL", TypeEtiquette::Hierarchique);
  BlocFeuille b2;
  b2.uuid = nouvelUuid();
  b2.nom = "Memoire";
  b2.feuille = c.feuilles[2].uuid;
  b2.position = ptMm(100, 50);
  b2.ports.push_back({nouvelUuid(), "CTRL", ptMm(100, 55), 0, SensSignal::Aucun});
  c.feuilles[1].blocs.push_back(b2);
  c.feuilles[1].fils.push_back({nouvelUuid(), ptMm(50, 50), ptMm(100, 55)});
  // Memoire : une résistance par membre, étiquettes hiérarchiques
  for (std::size_t i = 0; i < membres.size(); ++i) {
    bas.push_back(poserSymbole(c, 2, "R", ptMm(25.4, 25.4 + 12.7 * static_cast<double>(i))));
    etiquette(c, 2, broche(c, bas.back(), "1"), membres[i], TypeEtiquette::Hierarchique);
  }
  const std::string bruit2 = poserSymbole(c, 2, "R", ptMm(150, 25.4));
  etiquette(c, 2, broche(c, bruit2, "1"), "BRUIT");
  // Onze réseaux traversent les deux niveaux, un par membre, chacun d'exactement deux broches
  int traversants = 0;
  for (const auto& [nom, v] : reseaux(c)) {
    bool h = false, l = false;
    for (std::size_t i = 0; i < membres.size(); ++i) {
      h |= v.count(composantDe(c, haut[i]) + ":1") > 0;
      l |= v.count(composantDe(c, bas[i]) + ":1") > 0;
    }
    if (h && l) {
      ++traversants;
      CHECK(v.size() == 2);
    }
  }
  CHECK(traversants == 11);
  for (std::size_t i = 0; i < membres.size(); ++i) {
    const Connectivite k = connectivite(c);
    CHECK(k.parBroche.at({composantDe(c, haut[i]), "1"}) == k.parBroche.at({composantDe(c, bas[i]), "1"}));
  }
  // Aucun de plus : les deux « BRUIT » locaux restent séparés
  const Connectivite k = connectivite(c);
  CHECK(k.parBroche.at({composantDe(c, bruit), "1"}) != k.parBroche.at({composantDe(c, bruit2), "1"}));
  // Les ports de faisceau ne sont pas pris pour des ports manquants ou orphelins
  CHECK(verifierPortsHierarchiques(c).empty());
}

TEST_CASE("7.14 · directive « USB 90 Ω » posée sur D+ et D− : classe reprise par les règles du circuit, sans motif de nom", "[034][L7][7.14]") {
  ConceptionElec c = nouvelleConception("Directives");
  const std::string r1 = poserSymbole(c, 0, "R", ptMm(50.8, 50.8));
  const std::string r2 = poserSymbole(c, 0, "R", ptMm(101.6, 50.8));
  const std::string r3 = poserSymbole(c, 0, "R", ptMm(50.8, 101.6));
  etiquette(c, 0, broche(c, r1, "1"), "D+");
  etiquette(c, 0, broche(c, r2, "1"), "D+");
  etiquette(c, 0, broche(c, r1, "2"), "D-");
  etiquette(c, 0, broche(c, r2, "2"), "D-");
  etiquette(c, 0, broche(c, r3, "1"), "AUTRE");
  const std::string d1 = etiquette(c, 0, broche(c, r1, "1"), "USB 90 Ω", TypeEtiquette::Directive);
  const std::string d2 = etiquette(c, 0, broche(c, r1, "2"), "USB 90 Ω", TypeEtiquette::Directive);
  for (auto& e : c.feuilles[0].etiquettes)
    if (e.uuid == d1 || e.uuid == d2) e.proprietes["impedance"] = "90";
  const Connectivite k = connectivite(c);
  // Une directive ne nomme pas le réseau
  CHECK(k.parNom.count("D+"));
  CHECK(!k.parNom.count("USB 90 Ω"));
  const ConceptionCompilee cc = compiler(c, k);
  REQUIRE(cc.reseaux.count("D+"));
  CHECK(cc.reseaux.at("D+").classe == "USB 90 Ω");
  CHECK(cc.reseaux.at("D-").regles.at("impedance") == "90");
  CHECK(!cc.reseaux.count("AUTRE"));
  CHECK(appliquerDirectives(c) == 2);
  const Regles& R = c.circuit.regles;
  REQUIRE(classeDe(R, "D+") != nullptr);
  CHECK(classeDe(R, "D+")->nom == "USB 90 Ω");
  CHECK(classeDe(R, "D-")->nom == "USB 90 Ω");
  CHECK((classeDe(R, "AUTRE") == nullptr || classeDe(R, "AUTRE")->nom != "USB 90 Ω"));
  for (const auto& [nom, classe] : R.classeReseau)
    if (classe == "USB 90 Ω") CHECK(!motifReseau(nom));
  // Une seconde application ne change rien
  CHECK(appliquerDirectives(c) == 0);
  // Graphe des réseaux, rangé par ressorts : déterministe
  GrapheReseaux g = grapheReseaux(c, k);
  rangerParRessorts(g, 50);
  GrapheReseaux g2 = grapheReseaux(c, k);
  rangerParRessorts(g2, 50);
  REQUIRE(g.noeuds.size() == g2.noeuds.size());
  CHECK(g.noeuds.front().x == g2.noeuds.front().x);
}

TEST_CASE("7.16 · quatre voies de neuf composants à plat : disposition de la première recopiée sur les trois autres, 27 composants appariés seuls", "[034][L7][7.16]") {
  ConceptionElec c = nouvelleConception("Voies");
  c.circuit.contour.largeur = nm(100);
  c.circuit.contour.hauteur = nm(80);
  // Voie : neuf composants (six résistances, trois condensateurs) en chaîne, VCC et GND communs
  struct Lien {
    const char* symbole;
    const char* a;
    const char* b;
  };
  const Lien voie[9] = {{"R", "VCC", "n1"}, {"R", "n1", "n2"}, {"C", "n2", "GND"}, {"R", "n2", "n3"}, {"C", "n3", "GND"},
                        {"R", "n3", "n4"},  {"R", "n4", "n5"}, {"C", "n5", "GND"}, {"R", "n4", "GND"}};
  std::vector<std::vector<std::string>> composants(4);
  for (int v = 0; v < 4; ++v)
    for (int j = 0; j < 9; ++j) {
      const std::string s = poserSymbole(c, 0, voie[j].symbole, ptMm(25.4 + 12.7 * j, 25.4 + 25.4 * v));
      composants[static_cast<std::size_t>(v)].push_back(composantDe(c, s));
      auto nom = [&](const std::string& n) { return n == "VCC" || n == "GND" ? n : "V" + std::to_string(v + 1) + "_" + n; };
      const auto type = [&](const std::string& n) { return n == "VCC" || n == "GND" ? TypeEtiquette::Globale : TypeEtiquette::Locale; };
      etiquette(c, 0, broche(c, s, "1"), nom(voie[j].a), type(voie[j].a));
      etiquette(c, 0, broche(c, s, "2"), nom(voie[j].b), type(voie[j].b));
    }
  mettreAJourCircuit(c);
  REQUIRE(c.circuit.empreintes.size() == 36);
  // Appariement : trois voies semblables à la première, neuf composants chacune
  const auto canaux = apparierCanaux(c, composants[0]);
  REQUIRE(canaux.size() == 3);
  int apparies = 0;
  std::set<std::string> cibles;
  for (const auto& m : canaux) {
    apparies += static_cast<int>(m.size());
    for (const auto& [s, t] : m) cibles.insert(t);
  }
  CHECK(apparies == 27);
  CHECK(cibles.size() == 27);
  // Chaque composant de la voie 1 est apparié au composant de même rang d'une autre voie
  for (const auto& m : canaux) {
    int voieCible = -1;
    for (int v = 1; v < 4; ++v)
      if (std::find(composants[static_cast<std::size_t>(v)].begin(), composants[static_cast<std::size_t>(v)].end(), m.begin()->second) != composants[static_cast<std::size_t>(v)].end()) voieCible = v;
    REQUIRE(voieCible > 0);
    for (int j = 0; j < 9; ++j) CHECK(m.at(composants[0][static_cast<std::size_t>(j)]) == composants[static_cast<std::size_t>(voieCible)][static_cast<std::size_t>(j)]);
  }
  // Disposition de la voie 1 et une piste sur son réseau n1
  for (int j = 0; j < 9; ++j)
    for (auto& e : c.circuit.empreintes)
      if (e.composant == composants[0][static_cast<std::size_t>(j)]) e.position = ptMm(10 + 6 * j, 10);
  Piste p;
  p.uuid = nouvelUuid();
  p.reseau = "V1_n1";
  p.a = ptMm(10, 12);
  p.b = ptMm(16, 12);
  c.circuit.pistes.push_back(p);
  CHECK(recopierDisposition(c, composants[0], canaux, ptMm(0, 15)) == 27);
  for (std::size_t i = 0; i < canaux.size(); ++i)
    for (const auto& [s, t] : canaux[i]) CHECK(placee(c, t)->position == placee(c, s)->position + ptMm(0, 15.0 * static_cast<double>(i + 1)));
  int copiees = 0;
  for (const auto& q : c.circuit.pistes)
    if (q.reseau != "V1_n1" && q.reseau.size() > 3 && q.reseau.substr(2) == "_n1") ++copiees;
  CHECK(copiees == 3);

  // Extrait du circuit en fichier : relu et posé, mêmes composants
  std::set<std::string> sel(composants[0].begin(), composants[0].end());
  const Extrait x = extraitCircuit(c, sel, "Voie");
  std::string erreur;
  const auto relu = lireExtrait(texteExtrait(x), &erreur);
  REQUIRE(relu.has_value());
  CHECK(relu->nom == "Voie");
  CHECK(relu->genre == "circuit");
  CHECK(relu->contenu == x.contenu);
  CHECK(!lireExtrait("{\"format\":\"autre\"}").has_value());
}

TEST_CASE("7.19 · DXF de trois calques, un seul choisi : ses 24 traits deviennent des formes modifiables sur la feuille ; SVG importé", "[034][L7][7.19]") {
  std::string dxf = "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n";
  for (const auto& [nom, couleur] : std::vector<std::pair<std::string, int>>{{"CADRE", 1}, {"TEXTE", 3}, {"COTES", 5}}) dxf += "0\nLAYER\n2\n" + nom + "\n70\n0\n62\n" + std::to_string(couleur) + "\n";
  dxf += "0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n";
  auto ligne = [&](const std::string& calque, double x1, double y1, double x2, double y2) {
    dxf += "0\nLINE\n8\n" + calque + "\n10\n" + std::to_string(x1) + "\n20\n" + std::to_string(y1) + "\n11\n" + std::to_string(x2) + "\n21\n" + std::to_string(y2) + "\n";
  };
  // 24 traits sur CADRE : une grille de 6 × 4 segments ; d'autres traits et un cercle sur les autres calques
  for (int i = 0; i < 6; ++i) ligne("CADRE", 10.0 * i, 0, 10.0 * i + 10, 0);
  for (int i = 0; i < 6; ++i) ligne("CADRE", 10.0 * i, 40, 10.0 * i + 10, 40);
  for (int i = 0; i < 6; ++i) ligne("CADRE", 10.0 * i, 0, 10.0 * i, 40);
  for (int i = 0; i < 6; ++i) ligne("CADRE", 10.0 * i + 5, 10, 10.0 * i + 5, 30);
  for (int i = 0; i < 5; ++i) ligne("TEXTE", 0, 5.0 * i, 20, 5.0 * i);
  ligne("COTES", 0, -10, 60, -10);
  dxf += "0\nCIRCLE\n8\nCOTES\n10\n30\n20\n20\n40\n5\n";
  dxf += "0\nENDSEC\n0\nEOF\n";

  ConceptionElec c = nouvelleConception("Formes");
  const BilanImportFormes b = importerDxfFeuille(c, 0, dxf, {"CADRE"}, ptMm(20, 200), 2);
  CHECK(b.formes == 24);
  REQUIRE(c.feuilles[0].formes.size() == 24);
  for (const auto& f : c.feuilles[0].formes) {
    CHECK(f.trace.forme == Trace::Forme::Polyligne);
    CHECK(f.trace.points.size() == 2);
    CHECK(f.calque == 2);
    CHECK(f.trace.couleur == "#FF0000");
  }
  // Coin bas-gauche du dessin en (20, 200) : la ligne y = 40 du DXF est 40 mm plus haut
  const auto& premiere = c.feuilles[0].formes.front().trace;
  CHECK(premiere.points[0] == ptMm(20, 200));
  CHECK(c.feuilles[0].formes[6].trace.points[0] == ptMm(20, 160));
  // Modifiables : ordre de superposition, mesure, calque masqué
  const std::string u = c.feuilles[0].formes[3].uuid;
  CHECK(changerOrdre(c.feuilles[0], u, Superposition::ArrierePlan));
  CHECK(formesRangees(c.feuilles[0]).front()->uuid == u);
  CHECK(changerOrdre(c.feuilles[0], u, Superposition::PremierPlan));
  CHECK(formesRangees(c.feuilles[0]).back()->uuid == u);
  CHECK(mesurer(c.feuilles[0].formes.front().trace.points) == Approx(10));
  c.feuilles[0].calquesMasques = {2};
  CHECK(formesRangees(c.feuilles[0]).empty());
  // Tous les calques visibles : 24 + 5 + 1 + le cercle
  ConceptionElec c2 = nouvelleConception("Tout");
  CHECK(importerDxfFeuille(c2, 0, dxf, {}, ptMm(0, 100)).formes == 31);

  // SVG en millimètres : rectangle arrondi, cercle, chemin avec courbe et arc, polyligne dans un groupe déplacé
  const std::string svg = R"SVG(<?xml version="1.0"?>
<svg xmlns="http://www.w3.org/2000/svg" width="100mm" height="50mm" viewBox="0 0 100 50">
  <rect x="5" y="5" width="20" height="10" rx="2" stroke="#000" fill="none" stroke-width="0.5"/>
  <circle cx="50" cy="25" r="10" style="stroke:red;fill:none"/>
  <path d="M 60 10 C 70 0, 80 20, 90 10 A 5 5 0 0 1 90 20 Z" stroke="blue" fill="none"/>
  <g transform="translate(10,30)"><polyline points="0,0 10,5 20,0" stroke="black" fill="none"/></g>
  <text x="0" y="0">ignoré</text>
</svg>)SVG";
  ConceptionElec c3 = nouvelleConception("SVG");
  const BilanImportFormes s = importerSvgFeuille(c3, 0, svg, ptMm(0, 0), 1);
  const auto& fs = c3.feuilles[0].formes;
  REQUIRE(s.formes == static_cast<int>(fs.size()));
  CHECK(std::count_if(fs.begin(), fs.end(), [](const FormeSchema& f) { return f.trace.forme == Trace::Forme::Rectangle; }) == 1);
  CHECK(std::count_if(fs.begin(), fs.end(), [](const FormeSchema& f) { return f.trace.forme == Trace::Forme::Cercle; }) == 1);
  CHECK(std::count_if(fs.begin(), fs.end(), [](const FormeSchema& f) { return f.trace.forme == Trace::Forme::Bezier; }) >= 2);
  const auto rect = std::find_if(fs.begin(), fs.end(), [](const FormeSchema& f) { return f.trace.forme == Trace::Forme::Rectangle; });
  CHECK(rect->trace.points[0] == ptMm(5, 5));
  CHECK(rect->trace.points[1] == ptMm(25, 15));
  CHECK(rect->trace.arrondi == nm(2));
  CHECK(rect->trace.epaisseur == nm(0.5));
  const auto rond = std::find_if(fs.begin(), fs.end(), [](const FormeSchema& f) { return f.trace.forme == Trace::Forme::Cercle; });
  CHECK(rond->trace.points[0] == ptMm(50, 25));
  CHECK(rond->trace.rayon == nm(10));
  CHECK(rond->trace.couleur == "#FF0000");
  const auto poly = std::find_if(fs.begin(), fs.end(), [](const FormeSchema& f) { return f.trace.forme == Trace::Forme::Polyligne && f.trace.points.size() == 3; });
  REQUIRE(poly != fs.end());
  CHECK(poly->trace.points[1] == ptMm(20, 35));
  CHECK(!s.alertes.empty());  // le texte est signalé
}

TEST_CASE("7.20 · MOSFET canal P en SOT-23 demandé à l'assistant : dessin normalisé, G, S et D sur 1, 2 et 3", "[034][L7][7.20]") {
  const auto d = symboleNormalise("MOSFET-P", "SOT-23");
  REQUIRE(d.has_value());
  std::map<std::string, std::string> parNom;
  for (const auto& b : d->broches) parNom[b.nom] = b.numero;
  CHECK(parNom["G"] == "1");
  CHECK(parNom["S"] == "2");
  CHECK(parNom["D"] == "3");
  CHECK(d->prefixe == "Q");
  CHECK(d->empreinte == "SOT-23");
  CHECK(!d->traces.empty());
  CHECK(controlerSymbole(*d).empty());
  // Transistor NPN en SOT-23 : B, E, C ; en TO-92 : E, B, C
  const auto npn = symboleNormalise("NPN", "SOT-23");
  REQUIRE(npn.has_value());
  CHECK(std::find_if(npn->broches.begin(), npn->broches.end(), [](const Broche& b) { return b.nom == "B"; })->numero == "1");
  const auto to92 = symboleNormalise("NPN", "TO-92");
  CHECK(std::find_if(to92->broches.begin(), to92->broches.end(), [](const Broche& b) { return b.nom == "E"; })->numero == "1");
  for (const char* g : {"MOSFET-N", "PNP", "DIODE", "ZENER", "LED", "AOP", "REGULATEUR"}) {
    const auto x = symboleNormalise(g, "");
    REQUIRE(x.has_value());
    CHECK(controlerSymbole(*x).empty());
  }
  CHECK(!symboleNormalise("INCONNU", "SOT-23").has_value());
  // Symbole dérivé : dessin et broches du parent, champs propres du dérivé
  ConceptionElec c = nouvelleConception("Dérivés");
  DefSymbole parent = *d;
  parent.id = "MOS_P";
  c.symboles[parent.id] = parent;
  DefSymbole derive;
  derive.id = "BSS84";
  derive.parent = "MOS_P";
  derive.mpn = "BSS84";
  derive.fabricant = "Nexperia";
  const DefSymbole r = symboleResolu(c, derive);
  CHECK(r.broches == parent.broches);
  CHECK(r.mpn == "BSS84");
  CHECK(r.parent.empty());
  CHECK(r.id == "BSS84");
}

TEST_CASE("7.24 · pastille 1 de U2 agrandie sur la carte : bibliothèque et autres SOIC-8 inchangées, écart affiché ; générateurs", "[034][L7][7.24]") {
  ConceptionElec c = nouvelleConception("Locale");
  const std::string s1 = poserSymbole(c, 0, "NE555", ptMm(50.8, 50.8));
  const std::string s2 = poserSymbole(c, 0, "NE555", ptMm(101.6, 50.8));
  for (auto& x : c.composants) x.reference = x.uuid == composantDe(c, s1) ? "U1" : "U2";
  mettreAJourCircuit(c);
  const std::string u1 = composantDe(c, s1), u2 = composantDe(c, s2);
  const DefEmpreinte biblio = *c.empreinte("SOIC-8");
  const std::string copie = copierEmpreinteLocale(c, u2);
  CHECK(copie == "SOIC-8~local~U2");
  CHECK(placee(c, u2)->empreinte == copie);
  CHECK(placee(c, u2)->copieLocale == "SOIC-8");
  CHECK(ecartBibliotheque(c, u2).empty());
  for (auto& p : c.empreintes[copie].pastilles)
    if (p.numero == "1") p.largeur += 400'000;
  const auto ecart = ecartBibliotheque(c, u2);
  REQUIRE(ecart.size() == 1);
  CHECK(ecart[0].rfind("pastille 1 :", 0) == 0);
  // La bibliothèque et l'autre SOIC-8 ne bougent pas
  CHECK(*c.empreinte("SOIC-8") == biblio);
  CHECK(placee(c, u1)->empreinte == "SOIC-8");
  CHECK(ecartBibliotheque(c, u1).empty());
  // La mise à jour du circuit garde la copie ; la DRC ne la prend pas pour une empreinte changée
  mettreAJourCircuit(c);
  CHECK(placee(c, u2)->empreinte == copie);
  const Connectivite k = connectivite(c);
  CHECK(compter(verifierDRC(c, k, remplirZones(c, k)), "DRC-40") == 0);
  // La pastille agrandie est bien sur la carte
  Nm largeur = 0;
  for (const auto& p : pastilles(c, k))
    if (p.composant == u2 && p.numero == "1") largeur = p.pastille.largeur;
  CHECK(largeur == biblio.pastilles.front().largeur + 400'000);
  // Retour à la bibliothèque : la copie disparaît
  CHECK(revenirBibliotheque(c, u2));
  CHECK(placee(c, u2)->empreinte == "SOIC-8");
  CHECK(!c.empreintes.count(copie));

  // Bobine planaire carrée de 5 spires, 10 mm : inductance de Wheeler modifiée (Mohan, 1999)
  Bobine bob;
  CHECK(diametreInterieur(bob) == Approx(5.5));
  CHECK(inductanceBobine(bob) * 1e9 == Approx(316.9).margin(1.0));
  const auto pistes = poserBobine(c, bob, ptMm(25, 20), "L1");
  CHECK(pistes.size() >= 4 * 5 - 1);
  // Piste résistive de 0,1 Ω en 0,2 mm sur 35 µm : longueur ρ L / (l e)
  const double l = poserPisteResistive(c, ptMm(5, 5), ptMm(15, 5), 0.1, 200'000, "SHUNT");
  CHECK(l > 10);
  // Curseur capacitif de cinq segments, zone dégagée autour
  Capteur cap;
  cap.genre = GenreCapteur::Curseur;
  cap.segments = 5;
  cap.taille = 40;
  const std::size_t zones = c.circuit.zonesRegles.size();
  CHECK(poserCapteur(c, cap, ptMm(30, 30)) == 5);
  CHECK(c.circuit.zonesRegles.size() == zones + 1);
}

TEST_CASE("7.25 · jeu « école » chargé : sérigraphie à 1,2 mm, fils de la classe Puissance en rouge de 0,5 mm", "[034][L7][7.25]") {
  ConceptionElec c = nouvelleConception("Styles");
  const auto jeux = jeuxStylesIntegres();
  const auto ecole = std::find_if(jeux.begin(), jeux.end(), [](const JeuStyles& j) { return j.nom == "école"; });
  REQUIRE(ecole != jeux.end());
  CHECK(appliquerJeuStyles(c, *ecole) >= 1);
  CHECK(c.circuit.serigraphie.hauteur == 1'200'000);
  CHECK(texteParDefaut(c, Couche::SerigraphieDessus, "R1", ptMm(1, 1)).hauteur == 1'200'000);
  CHECK(texteParDefaut(c, Couche::SerigraphieDessous, "R1", ptMm(1, 1)).hauteur == 1'200'000);
  c.circuit.regles.classeReseau["VIN"] = "Puissance";
  const StyleFil f = styleDuFil(c, "VIN");
  CHECK(f.couleur == "#E53935");
  CHECK(f.epaisseur == 500'000);
  // Un réseau ordinaire prend le style des fils
  CHECK(styleDuFil(c, "SIGNAL").epaisseur == ecole->schema.fil.epaisseur);
  // Le jeu de la conception, enregistré puis relu, est le même
  const JeuStyles mien = jeuDeLaConception(c, "mon jeu");
  std::string erreur;
  const auto relu = lireJeuStyles(texteJeuStyles(mien), &erreur);
  REQUIRE(relu.has_value());
  CHECK(relu->schema == mien.schema);
  CHECK(relu->carte == mien.carte);
  CHECK(relu->classes.size() == mien.classes.size());
  // Le jeu « standard » revient aux valeurs de la 0.33
  const auto standard = std::find_if(jeux.begin(), jeux.end(), [](const JeuStyles& j) { return j.nom == "standard"; });
  appliquerJeuStyles(c, *standard);
  CHECK(defautsCarte(c) == DefautsCarte{});
  CHECK(!c.extensions.count("defautsCarte"));
  // Les valeurs par défaut de la carte voyagent avec le projet
  ConceptionElec d = nouvelleConception("Voyage");
  appliquerJeuStyles(d, *ecole);
  const ConceptionElec relue = conceptionDepuisJson(versJson(d));
  CHECK(defautsCarte(relue) == defautsCarte(d));
  CHECK(relue.reglagesSchema == d.reglagesSchema);
}
