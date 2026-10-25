// Prométhée : essais de la 0.34, lot L10 (fin) — formats anciens (10.18) : Protel Autotrax, gEDA PCB et gschem,
// Sprint-Layout ; conversion des bibliothèques de tous les formats lus. Licence GPL-3.0-only.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "promethee/echanges_electronique.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/formats_anciens.hpp"
#include "promethee/gestion_bibliotheque.hpp"
#include "promethee/import_bibliotheques.hpp"
#include "promethee/import_commun.hpp"
#include "promethee/police.hpp"

using namespace prom::ecad;

namespace {
std::string donnee18(const std::string& f) { return prom::lireFichier(prom::cheminFichier(std::string(PROMETHEE_DONNEES) + "/" + f)); }
std::map<std::string, int> codes18(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  std::map<std::string, int> r;
  for (const auto& v : verifierDRC(c, k, remplirZones(c, k))) ++r[v.code];
  return r;
}
int reseauxMulti(const ConceptionElec& c) {
  int n = 0;
  for (const auto& [nom, v] : netlistDe(c).reseaux) n += v.size() > 1;
  return n;
}
}  // namespace

// Exploration sur les fichiers d'essai de KiCad (PROMETHEE_QA : dossier qa/data de KiCad) : bilan de chaque carte
TEST_CASE("10.18 · exploration des cartes anciennes réelles", "[.explore]") {
  const char* qa = std::getenv("PROMETHEE_QA");
  if (!qa) return;
  namespace fs = std::filesystem;
  const char* seul = std::getenv("PROMETHEE_QA_DOSSIER");
  for (const char* dossier : {"pcbnew/plugins/sprint_layout", "pcbnew/plugins/autotrax", "pcbnew/plugins/geda", "pcbnew/plugins/pcad"}) {
    if (seul && std::string(dossier).find(seul) == std::string::npos) continue;
    for (const auto& e : fs::directory_iterator(fs::path(qa) / dossier)) {
      const std::string o = prom::lireFichier(e.path().string());
      RapportLectureCao r;
      ConceptionElec c;
      if (estSprintLayout(o)) c = conceptionDepuisSprintLayout(o, e.path().stem().string(), &r);
      else if (estAutotrax(o)) c = conceptionDepuisAutotrax(o, e.path().stem().string(), &r);
      else if (estGedaPcb(o)) c = conceptionDepuisGedaPcb(o, e.path().stem().string(), &r);
      else if (estPcadAscii(o)) c = conceptionDepuisPcad(o, e.path().stem().string(), &r);
      else continue;
      const auto d = codes18(c);
      std::string a;
      for (const auto& x : r.alertes) a += "\n    " + x;
      std::string cs;
      for (const auto& [k, n] : d) cs += " " + k + "=" + std::to_string(n);
      WARN(e.path().filename().string() << " : " << c.circuit.empreintes.size() << " empreintes posées, " << c.empreintes.size() << " définitions, " << reseauxMulti(c) << " réseaux, " << c.circuit.pistes.size() << " pistes, "
                                        << c.circuit.vias.size() << " vias, " << c.circuit.zones.size() << " zones, " << c.circuit.regions.size() << " régions, contour " << c.circuit.contour.libre.size() << " pts ;" << cs << a);
    }
  }
}




TEST_CASE("10.18 · exploration : schéma gschem contre la netlist de sa carte", "[.explore5]") {
  const char* qa = std::getenv("PROMETHEE_QA");
  const char* sch = std::getenv("PROMETHEE_QA_SCH");
  const char* pcb = std::getenv("PROMETHEE_QA_PCB");
  if (!qa || !sch || !pcb) return;
  namespace fs = std::filesystem;
  const fs::path base = fs::path(qa) / "eeschema/io/geda";
  const LecteurSymboles lire = [base](const std::string& n) -> std::optional<std::string> {
    for (const char* d : {"."})
      if (fs::exists(base / d / n)) return prom::lireFichier((base / d / n).string());
    return std::nullopt;
  };
  RapportLectureCao r;
  const ConceptionElec s = conceptionDepuisGschem(prom::lireFichier((base / sch).string()), "s", lire, &r);
  for (const auto& a : r.alertes) WARN(a);
  const ConceptionElec c = conceptionDepuisGedaPcb(prom::lireFichier((fs::path(qa) / pcb).string()), "c");
  const NetlistLue ns = netlistDe(s), nc = netlistDe(c);
  // Broches connues des deux côtés : celles du schéma (symboles trouvés)
  std::set<std::string> connues;
  std::map<std::string, std::string> netS, netC;
  for (const auto& [n, v] : ns.reseaux)
    for (const auto& [ref, b] : v)
      if (n.rfind("N-", 0) != 0) connues.insert(ref + "." + b), netS[ref + "." + b] = n;  // « N-U1-1 » : broche d'attente d'un symbole absent
  for (const auto& [n, v] : nc.reseaux)
    for (const auto& [ref, b] : v) netC[ref + "." + b] = n;
  int accord = 0, desaccord = 0;
  std::vector<std::string> v(connues.begin(), connues.end());
  for (std::size_t i = 0; i < v.size(); ++i)
    for (std::size_t j = i + 1; j < v.size(); ++j) {
      if (!netC.count(v[i]) || !netC.count(v[j])) continue;
      const bool a = netS[v[i]] == netS[v[j]], b = netC[v[i]] == netC[v[j]];
      if (a == b) ++accord;
      else if (++desaccord < 15) WARN(v[i] << " et " << v[j] << " : schéma " << (a ? "reliées" : "séparées") << " (" << netS[v[i]] << "/" << netS[v[j]] << "), carte " << (b ? "reliées" : "séparées") << " (" << netC[v[i]] << "/" << netC[v[j]] << ")");
    }
  int absentes = 0;
  for (const auto& x : connues) absentes += !netC.count(x);
  WARN(connues.size() << " broches au schéma, " << absentes << " absentes de la carte ; paires : " << accord << " d'accord, " << desaccord << " en désaccord ; " << s.composants.size() << " composants au schéma");
}

namespace {
LecteurSymboles symbolesDe(const std::string& dossier) {
  return [dossier](const std::string& n) -> std::optional<std::string> {
    try {
      return donnee18(dossier + "/" + n);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };
}
// Broches que la netlist du schéma et celle de la carte rangent de même : paires d'accord et en désaccord, sur les
// broches dont le schéma connaît le symbole (« N-U1-1 » : broche d'attente d'un symbole absent)
std::pair<int, int> accordNetlists(const NetlistLue& schema, const NetlistLue& carte) {
  std::map<std::string, std::string> s, c;
  for (const auto& [n, v] : schema.reseaux)
    for (const auto& [ref, b] : v)
      if (n.rfind("N-", 0) != 0) s[ref + "." + b] = n;
  for (const auto& [n, v] : carte.reseaux)
    for (const auto& [ref, b] : v) c[ref + "." + b] = n;
  std::vector<std::string> broches;
  for (const auto& [b, n] : s)
    if (c.count(b)) broches.push_back(b);
  int accord = 0, desaccord = 0;
  for (std::size_t i = 0; i < broches.size(); ++i)
    for (std::size_t j = i + 1; j < broches.size(); ++j) ((s[broches[i]] == s[broches[j]]) == (c[broches[i]] == c[broches[j]]) ? accord : desaccord) += 1;
  return {accord, desaccord};
}
const ComposantElec* parRef(const ConceptionElec& c, const std::string& r) {
  for (const auto& x : c.composants)
    if (x.reference == r) return &x;
  return nullptr;
}
const EmpreintePlacee* poseDe(const ConceptionElec& c, const std::string& r) {
  const ComposantElec* x = parRef(c, r);
  for (const auto& e : c.circuit.empreintes)
    if (x && e.composant == x->uuid) return &e;
  return nullptr;
}
// Macro Sprint-Layout écrite à la main (version 6) : objets pastille CMS et ligne
void u4(std::string& o, std::uint32_t v) {
  for (int k = 0; k < 4; ++k) o += static_cast<char>((v >> (8 * k)) & 0xFF);
}
void f4(std::string& o, float f) {
  std::uint32_t b;
  std::memcpy(&b, &f, 4);
  u4(o, b);
}
std::string objetSprint(std::uint8_t type, float x, float y, float ext, float inte, std::int32_t largeur, std::uint8_t couche, const std::vector<std::pair<float, float>>& points) {
  std::string o;
  o += static_cast<char>(type);
  f4(o, x), f4(o, y), f4(o, ext), f4(o, inte);
  u4(o, static_cast<std::uint32_t>(largeur));
  o += '\0';
  o += static_cast<char>(couche);
  o += '\0';                       // forme
  o += std::string(4, '\0');       // remplissage
  o += std::string(2, '\0');       // composant
  o += '\0';                       // choisi
  u4(o, 0);                        // angle de début
  o += std::string(5, '\0');
  o += '\0';                       // plein
  u4(o, 0);                        // isolement
  o += std::string(5, '\0');
  o += std::string(3, '\0');       // miroirs, interdit
  u4(o, 0);                        // rotation
  o += '\0';                       // métallisé
  o += '\1';                       // vernis
  o += std::string(18, '\0');
  u4(o, 0);                        // texte
  u4(o, 0);                        // identifiant
  u4(o, 0);                        // groupes
  u4(o, static_cast<std::uint32_t>(points.size()));
  for (const auto& [px, py] : points) f4(o, px), f4(o, py);
  return o;
}
// Pastille CMS de Sprint-Layout : centre, côtés (dixièmes de micron), coins en points, y vers le haut
std::string cmsSprint(float cx, float cy, float l, float h) {
  return objetSprint(8, 0, 0, l, h, 0, 1, {{cx - l / 2, cy + h / 2}, {cx + l / 2, cy + h / 2}, {cx + l / 2, cy - h / 2}, {cx - l / 2, cy - h / 2}});
}
}  // namespace

TEST_CASE("10.18 · carte Sprint-Layout réelle : lue au dernier octet, réseaux du cuivre, plans de masse en zones, empreintes partagées", "[034][L10][10.18]") {
  const std::string o = donnee18("sprint/12F629_SM.lay6");
  REQUIRE(estSprintLayout(o));
  CHECK(nombreCartesSprintLayout(o) == 1);
  RapportLectureCao r;
  const ConceptionElec c = conceptionDepuisSprintLayout(o, "12F629_SM", &r);
  for (const auto& a : r.alertes) UNSCOPED_INFO(a);
  // 9 composants (le second « C1 » renommé), 5 pastilles libres posées sur la carte seule
  CHECK(c.circuit.empreintes.size() == 14);
  CHECK(parRef(c, "C1-2") != nullptr);
  CHECK(std::any_of(r.alertes.begin(), r.alertes.end(), [](const std::string& a) { return a.find("« C1 » en double") != std::string::npos; }));
  int seules = 0;
  for (const auto& x : c.composants) seules += x.carteSeule;
  CHECK(seules == 5);
  // Plans de masse dessus et dessous : deux zones « GND_PLANE » sur toute la carte (25 × 25 mm)
  int plans = 0;
  for (const auto& z : c.circuit.zones) plans += z.reseau == "GND_PLANE";
  CHECK(plans == 2);
  CHECK(c.circuit.contour.libre.size() == 4);
  CHECK(boite(c.circuit.contour.libre).largeur() == 25 * NM_PAR_MM);
  // Le cuivre ne mêle aucun réseau et relie tout ce qu'il doit
  const auto d = codes18(c);
  CHECK(d.count("DRC-21") == 0);
  CHECK(d.count("DRC-20") == 0);
  CHECK(reseauxMulti(c) == 8);
  // C1 et C1-2 (même boîtier 1206, même forme, même rotation) partagent leur empreinte ; « 10K », boîtier 1206 à
  // pastilles carrées, a la sienne
  const EmpreintePlacee* c1 = poseDe(c, "C1");
  const EmpreintePlacee* c12 = poseDe(c, "C1-2");
  const EmpreintePlacee* r10k = poseDe(c, "10K");
  REQUIRE(c1);
  REQUIRE(c12);
  REQUIRE(r10k);
  CHECK(c1->empreinte == c12->empreinte);
  CHECK(c1->empreinte == "sprint:1206");
  CHECK(r10k->empreinte == "sprint:1206~2");
  CHECK(c1->rotation == 180);
  CHECK(r10k->rotation == 270);
  // Le SOIC-8 du PIC : huit pastilles numérotées 1 à 8
  const EmpreintePlacee* pic = poseDe(c, "12F629");
  REQUIRE(pic);
  const DefEmpreinte* soic = c.empreinte(pic->empreinte);
  REQUIRE(soic);
  CHECK(soic->pastilles.size() == 8);
  CHECK(soic->pastilles.back().numero == "8");
}

TEST_CASE("10.18 · Sprint-Layout : chevelu gardé par le fichier (liaisons à router), plans à pastilles reliées sans isolement", "[034][L10][10.18]") {
  // Deux liaisons tirées au chevelu, sans cuivre : deux liaisons non routées dans la DRC, ni plus ni moins
  RapportLectureCao r;
  const ConceptionElec k = conceptionDepuisSprintLayout(donnee18("sprint/karpaty-rx-pcb5-buffer-freq-doubler-orig.lay6"), "karpaty", &r);
  for (const auto& a : r.alertes) UNSCOPED_INFO(a);
  const auto dk = codes18(k);
  CHECK(dk.count("DRC-21") == 0);
  REQUIRE(dk.count("DRC-20") == 1);
  CHECK(dk.at("DRC-20") == 2);
  // Plans de masse des deux faces ; les pastilles posées sans isolement y sont reliées (réseau GND_PLANE)
  const ConceptionElec g = conceptionDepuisSprintLayout(donnee18("sprint/gpio2nesc.lay6"), "gpio2nesc");
  int plans = 0;
  for (const auto& z : g.circuit.zones) plans += z.reseau == "GND_PLANE";
  CHECK(plans == 2);
  const NetlistLue n = netlistDe(g);
  REQUIRE(n.reseaux.count("GND_PLANE"));
  CHECK(n.reseaux.at("GND_PLANE").size() >= 4);
  CHECK(codes18(g).count("DRC-21") == 0);
  // Évidements du plan : zones d'interdiction des remplissages
  CHECK(!g.circuit.zonesRegles.empty());
  // Fichier tronqué : refusé avec sa raison, sans plantage
  RapportLectureCao rt;
  const std::string o = donnee18("sprint/gpio2nesc.lay6");
  const ConceptionElec t = conceptionDepuisSprintLayout(o.substr(0, o.size() / 2), "tronque", &rt);
  CHECK(t.circuit.empreintes.empty());
  REQUIRE(!rt.alertes.empty());
  CHECK(rt.alertes.front().find("illisible") != std::string::npos);
}

TEST_CASE("10.18 · macro Sprint-Layout (.lmk) en empreinte de bibliothèque", "[034][L10][10.18]") {
  // Trois pastilles CMS de 0,9 × 0,8 mm (unités de 0,1 µm) et un trait de sérigraphie
  std::string lmk = "\x06\x33\xAA\xFF";
  u4(lmk, 4);
  lmk += cmsSprint(-9500, -11000, 9000, 8000);
  lmk += cmsSprint(9500, -11000, 9000, 8000);
  lmk += cmsSprint(0, 11000, 9000, 8000);
  lmk += objetSprint(6, 0, 0, 0, 0, 1500, 2, {{-15000, 0}, {15000, 0}});
  std::string alerte;
  const auto d = empreinteDepuisMacroSprint(lmk, "SOT-23", &alerte);
  REQUIRE(d);
  CHECK(alerte.empty());
  CHECK(d->id == "sprint:SOT-23");
  REQUIRE(d->pastilles.size() == 3);
  CHECK(d->pastilles[0].numero == "1");
  CHECK(d->pastilles[2].numero == "3");
  CHECK(d->pastilles[0].largeur == 900'000);
  CHECK(d->pastilles[0].hauteur == 800'000);
  // Origine au centre des pastilles ; y retourné (vers le bas)
  CHECK(d->pastilles[0].position == Pt{-950'000, 1'100'000});
  CHECK(d->pastilles[2].position == Pt{0, -1'100'000});
  REQUIRE(d->traits.size() == 1);
  CHECK(d->traits[0].couche == Couche::SerigraphieDessus);
  // Converti en bibliothèque avec sa ligne au rapport
  ContenuBibliotheque bib;
  const RapportBibliotheque rap = importerBibliotheques({{"macros/SOT-23.lmk", lmk}}, bib);
  REQUIRE(bib.empreintes.size() == 1);
  CHECK(bib.empreintes[0].id == "sprint:macros:SOT-23");
  CHECK(rap.objets.size() == 1);
}

TEST_CASE("10.18 · cartes Protel Autotrax réelles : netlist NETDEF ou réseaux du cuivre, CMS sur les couches 1 et 6", "[034][L10][10.18]") {
  const std::string demo = donnee18("autotrax/DEMOSMD.PCB");
  REQUIRE(estAutotrax(demo));
  RapportLectureCao r;
  const ConceptionElec c = conceptionDepuisAutotrax(demo, "DEMOSMD", &r);
  for (const auto& a : r.alertes) UNSCOPED_INFO(a);
  CHECK(c.circuit.empreintes.size() == 36);
  CHECK(c.circuit.vias.size() == 276);
  CHECK(reseauxMulti(c) == 88);  // les réseaux de la netlist du fichier
  // Pastilles de la couche 1 : CMS, malgré leur perçage noté par défaut
  const EmpreintePlacee* u = nullptr;
  for (const auto& e : c.circuit.empreintes)
    if (const DefEmpreinte* d = c.empreinte(e.empreinte); d && !d->pastilles.empty() && d->pastilles.front().largeur < 1'000'000 && d->pastilles.front().type == TypePastille::CMS) u = &e;
  CHECK(u != nullptr);
  const auto d = codes18(c);
  CHECK(d.count("DRC-21") == 0);
  CHECK(d.count("DRC-20") == 0);
  CHECK(d.count("DRC-06") == 0);  // contour tiré de la couche d'interdiction, qui entoure tout le cuivre
  CHECK(c.circuit.contour.libre.size() >= 4);
  // Sans netlist : réseaux tirés du cuivre, carte sans court-circuit ni liaison ouverte, contour à la boîte du cuivre
  RapportLectureCao rp;
  const ConceptionElec p = conceptionDepuisAutotrax(donnee18("autotrax/PRJ.PCB"), "PRJ", &rp);
  CHECK(p.circuit.empreintes.size() == 9);
  CHECK(reseauxMulti(p) == 40);
  const auto dp = codes18(p);
  CHECK(dp.count("DRC-21") == 0);
  CHECK(dp.count("DRC-20") == 0);
  CHECK(dp.count("DRC-06") == 0);
  CHECK(std::any_of(rp.alertes.begin(), rp.alertes.end(), [](const std::string& a) { return a.find("tirés du cuivre") != std::string::npos; }));
  // Par la chaîne d'import commune
  const auto imp = importerConceptionTierce("PRJ.PCB", donnee18("autotrax/PRJ.PCB"), {});
  REQUIRE(imp);
  CHECK(imp->logiciel == "Protel Autotrax");
}

TEST_CASE("10.18 · carte gEDA PCB réelle : netlist du fichier, polygone au réseau de ses liaisons thermiques, élément du dessous", "[034][L10][10.18]") {
  const std::string pcb = donnee18("geda/goodfet50.pcb");
  REQUIRE(estGedaPcb(pcb));
  RapportLectureCao r;
  const ConceptionElec c = conceptionDepuisGedaPcb(pcb, "goodfet50", &r);
  for (const auto& a : r.alertes) UNSCOPED_INFO(a);
  CHECK(c.circuit.empreintes.size() == 16);
  CHECK(reseauxMulti(c) == 23);
  CHECK(c.circuit.vias.size() == 21);
  REQUIRE(c.circuit.zones.size() == 1);
  CHECK(c.circuit.zones[0].reseau == "GND");
  CHECK(codes18(c).count("DRC-21") == 0);
  // Élément « onsolder » : posé dessous, retourné d'un demi-tour, définition à l'endroit ; ses pastilles sont sur le
  // cuivre du dessous, là où le fichier les place
  const ConceptionElec s = conceptionDepuisGedaPcb(donnee18("geda/onsolder_test.pcb"), "onsolder");
  const EmpreintePlacee* c1 = poseDe(s, "C1");
  const EmpreintePlacee* r1 = poseDe(s, "R1");
  REQUIRE(c1);
  REQUIRE(r1);
  CHECK(c1->dessous);
  CHECK(c1->rotation == 180);
  CHECK(!r1->dessous);
  const DefEmpreinte* d = s.empreinte(c1->empreinte);
  REQUIRE(d);
  REQUIRE(d->pastilles.size() == 2);
  CHECK(couchesPastille(d->pastilles[0], c1->dessous, 2) & bit(Couche::CuivreDessous));
  // Pastille 1 du fichier : trait de (−295, −2067) à (295, −2067) cmils autour de la marque (15000, 5000)
  const Pastille p1 = pastilleSurCarte(*c1, d->pastilles[0]);
  CHECK(std::abs(p1.position.x - 15000 * 254) <= 2);
  CHECK(std::abs(p1.position.y - (5000 - 2067) * 254) <= 2);
}

TEST_CASE("10.18 · schémas gschem réels : netlists identiques à celles que gEDA a écrites dans leurs cartes", "[034][L10][10.18]") {
  // Symboles standard de gEDA incorporés : les schémas se relient sans leur bibliothèque ; les symboles propres aux
  // projets, absents, sont dits au rapport et leurs broches comptent pour rien
  for (const auto& [sch, pcb, attendu] : std::vector<std::tuple<std::string, std::string, int>>{{"geda/goodfet50.sch", "geda/goodfet50.pcb", 300}, {"geda/powermeter.sch", "geda/powermeter.pcb", 1200}}) {
    INFO(sch);
    RapportLectureCao r;
    const ConceptionElec s = conceptionDepuisGschem(donnee18(sch), "s", symbolesDe("geda"), &r);
    CHECK(s.schemaSeul);
    const ConceptionElec c = conceptionDepuisGedaPcb(donnee18(pcb), "c");
    const auto [accord, desaccord] = accordNetlists(netlistDe(s), netlistDe(c));
    CHECK(accord >= attendu);
    CHECK(desaccord == 0);
  }
  CHECK(symboleStandardGeda("resistor-1.sym").has_value());
  CHECK(!symboleStandardGeda("inconnu.sym").has_value());
  // Le projet entier par la chaîne d'import : schéma et carte voisins reliés par les repères
  auto lire = [](const std::string& f) -> std::optional<std::string> {
    try {
      return donnee18("geda/" + f);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };
  const auto imp = importerConceptionTierce("goodfet50.sch", donnee18("geda/goodfet50.sch"), lire);
  REQUIRE(imp);
  CHECK(imp->logiciel == "gEDA");
  CHECK(imp->fichiers.size() == 2);
  CHECK(!imp->conception.schemaSeul);
  CHECK(imp->conception.circuit.empreintes.size() == 16);
}

TEST_CASE("10.18 · gschem : portes (slot), pages reliées par les noms, feuille fille préfixée, projet à gafrc", "[034][L10][10.18]") {
  // Deux portes du même 7400 : un composant, broches 1 à 3 (porte 1) et 4 à 6 (porte 2) ; VCC et GND cachés (net=)
  const ConceptionElec m = conceptionDepuisGschem(donnee18("geda/multislot_test.sch"), "m", symbolesDe("geda"));
  REQUIRE(parRef(m, "U1"));
  std::set<std::string> broches;
  for (const auto& [n, v] : netlistDe(m).reseaux)
    for (const auto& [ref, b] : v)
      if (ref == "U1") broches.insert(b);
  for (const char* b : {"1", "2", "3", "4", "5", "6"}) CHECK(broches.count(b));
  // Deux pages : les pages d'essai de KiCad se lisent ensemble ; un même nom de réseau relie deux pages (R1 de la
  // première, R2 de la seconde, au bout d'un fil nommé « LIEN »)
  const ConceptionElec p = conceptionDepuisGschem({{"p1", donnee18("geda/multipage_page1.sch")}, {"p2", donnee18("geda/multipage_page2.sch")}}, "pages", symbolesDe("geda"));
  CHECK(parRef(p, "R2") != nullptr);
  const std::string page1 = "v 20130925 2\nC 40000 40000 1 0 0 resistor-1.sym\n{\nT 0 0 5 10 1 1 0 0 1\nrefdes=R1\n}\nN 40900 40100 42000 40100 4\n{\nT 0 0 5 10 1 1 0 0 1\nnetname=LIEN\n}\n";
  const std::string page2 = "v 20130925 2\nC 50000 40000 1 0 0 resistor-1.sym\n{\nT 0 0 5 10 1 1 0 0 1\nrefdes=R2\n}\nN 48000 40100 50000 40100 4\n{\nT 0 0 5 10 1 1 0 0 1\nnetname=LIEN\n}\n";
  const NetlistLue nl = netlistDe(conceptionDepuisGschem({{"p1", page1}, {"p2", page2}}, "lien", {}));
  REQUIRE(nl.reseaux.count("LIEN"));
  CHECK(nl.reseaux.at("LIEN") == std::vector<std::pair<std::string, std::string>>{{"R1", "2"}, {"R2", "1"}});
  // Feuille fille de l'essai de KiCad : sa résistance sous « S1/R1 » ; le symbole du bloc manque, c'est dit
  RapportLectureCao rh;
  const ConceptionElec h = conceptionDepuisGschem(donnee18("geda/hierarchy_test.sch"), "h", symbolesDe("geda"), &rh);
  CHECK(parRef(h, "S1/R1") != nullptr);
  CHECK(std::any_of(rh.alertes.begin(), rh.alertes.end(), [](const std::string& a) { return a.find("sub_block.sym") != std::string::npos; }));
  // Bloc incorporé au schéma, broche « IN » ; dans la feuille, le port d'entrée « IN » (in-1.sym) relie R2 : R1 du
  // parent et S1/R2 de la feuille sont sur un même réseau
  const std::string parent =
      "v 20130925 2\nC 40000 40000 1 0 0 EMBEDDEDbloc.sym\n[\nP 0 100 300 100 1 0 0\n{\nT 0 0 5 8 0 1 0 0 1\npinlabel=IN\nT 0 0 5 8 0 1 0 0 1\npinnumber=1\n}\n]\n{\nT 0 0 5 10 1 1 0 0 1\nrefdes=S1\nT 0 0 5 10 1 1 0 0 "
      "1\nsource=fille.sch\n}\nC 37000 40000 1 0 0 resistor-1.sym\n{\nT 0 0 5 10 1 1 0 0 1\nrefdes=R1\n}\nN 37900 40100 40000 40100 4\n";
  const std::string fille =
      "v 20130925 2\nC 40000 40000 1 0 0 in-1.sym\n{\nT 0 0 5 10 1 1 0 0 1\nrefdes=IN\n}\nC 41000 40000 1 0 0 resistor-1.sym\n{\nT 0 0 5 10 1 1 0 0 1\nrefdes=R2\n}\nN 40600 40100 41000 40100 4\n";
  const LecteurSymboles lireH = [fille](const std::string& n) -> std::optional<std::string> { return n == "fille.sch" ? std::optional<std::string>(fille) : std::nullopt; };
  const ConceptionElec hb = conceptionDepuisGschem(parent, "hb", lireH);
  CHECK(parRef(hb, "S1/R2") != nullptr);
  CHECK(parRef(hb, "S1/IN") == nullptr);  // le port n'est pas un composant
  const NetlistLue nb = netlistDe(hb);
  bool ensemble = false;
  for (const auto& [n, v] : nb.reseaux)
    ensemble |= std::find(v.begin(), v.end(), std::pair<std::string, std::string>{"R1", "2"}) != v.end() && std::find(v.begin(), v.end(), std::pair<std::string, std::string>{"S1/R2", "1"}) != v.end();
  CHECK(ensemble);
  // Projet à gafrc : symboles du projet et de sa bibliothèque trouvés dans les dossiers qu'il nomme ; alimentations
  // et étiquettes en réseaux nommés
  auto lire = [](const std::string& f) -> std::optional<std::string> {
    try {
      return donnee18("geda/ayab/" + f);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };
  const auto imp = importerConceptionTierce("ayab_rs.sch", donnee18("geda/ayab/ayab_rs.sch"), lire);
  REQUIRE(imp);
  for (const auto& a : imp->alertes) UNSCOPED_INFO(a);
  CHECK(std::none_of(imp->alertes.begin(), imp->alertes.end(), [](const std::string& a) { return a.find("introuvable") != std::string::npos; }));
  // Les symboles « stdlib » de ce projet d'essai sont des ébauches (KiCad n'y vérifie que les nombres) : les noms de
  // réseaux posés par les attributs net= et les symboles d'alimentation y sont, et chaque composant à repère aussi
  const NetlistLue na = netlistDe(imp->conception);
  for (const char* n : {"GND", "5V", "9V"}) {
    INFO(n);
    CHECK(na.reseaux.count(n));
  }
  CHECK(imp->conception.composants.size() == 54);  // les 54 repères du schéma
}

TEST_CASE("10.18 · bibliothèques de tous les formats lus converties, un objet par ligne de rapport", "[034][L10][10.18]") {
  // Empreinte gEDA (.fp) : l'élément R1 d'une carte ; symbole gEDA à quatre portes ; macro Sprint-Layout ; archive
  // de bibliothèque CADSTAR ; composant LCSC enregistré depuis EasyEDA ; symbole d'alimentation refusé avec sa raison
  const std::string carte = donnee18("geda/onsolder_test.pcb");
  const auto debut = carte.find("Element[\"\" \"0805\"");
  const auto fin = carte.find(")", carte.find("(", debut));
  REQUIRE(debut != std::string::npos);
  const std::string fp = carte.substr(debut, fin - debut + 1);
  std::string lmk = "\x06\x33\xAA\xFF";
  u4(lmk, 2);
  lmk += cmsSprint(-7500, 0, 6000, 9000);
  lmk += cmsSprint(7500, 0, 6000, 9000);
  ContenuBibliotheque bib;
  const RapportBibliotheque rap = importerBibliotheques({{"newlib/0805.fp", fp},
                                                        {"sym/7400-1.sym", *symboleStandardGeda("7400-1.sym")},
                                                        {"sym/gnd-1.sym", *symboleStandardGeda("gnd-1.sym")},
                                                        {"macros/0603.lmk", lmk},
                                                        {"cadstar/footprint-with-thermal-pad.cpa", donnee18("cadstar/footprint-with-thermal-pad.cpa")},
                                                        {"lcsc/C20526.json", donnee18("easyeda/C20526.json")}},
                                                       bib);
  for (const auto& l : rap.objets) UNSCOPED_INFO(l.genre << " " << l.nom << " : " << l.etat << " " << l.detail);
  auto ligne = [&](const std::string& genre, const std::string& nom) {
    return std::find_if(rap.objets.begin(), rap.objets.end(), [&](const LigneRapportObjet& l) { return l.genre == genre && l.nom.find(nom) != std::string::npos; });
  };
  REQUIRE(ligne("empreinte", "newlib:0805") != rap.objets.end());
  CHECK(ligne("empreinte", "newlib:0805")->etat == "importé");
  REQUIRE(ligne("symbole", "sym:7400-1") != rap.objets.end());
  CHECK(ligne("article", "sym:7400-1") != rap.objets.end());
  REQUIRE(ligne("symbole", "sym:gnd-1") != rap.objets.end());
  CHECK(ligne("symbole", "sym:gnd-1")->etat == "refusé");
  CHECK(ligne("empreinte", "macros:0603") != rap.objets.end());
  CHECK(std::count_if(rap.objets.begin(), rap.objets.end(), [](const LigneRapportObjet& l) { return l.genre == "empreinte" && l.etat == "importé"; }) == 4);
  // Le 7400 : quatre unités (portes), broches d'alimentation communes
  const auto s = std::find_if(bib.symboles.begin(), bib.symboles.end(), [](const DefSymbole& d) { return d.id == "geda:sym:7400-1"; });
  REQUIRE(s != bib.symboles.end());
  CHECK(s->unites == 4);
  std::set<std::string> numeros;
  for (const auto& b : s->broches) numeros.insert(b.numero);
  for (const char* b : {"1", "3", "4", "6", "8", "11", "13", "7", "14"}) CHECK(numeros.count(b));
  // L'empreinte gEDA : deux pastilles CMS de la R1 0805
  const auto e = std::find_if(bib.empreintes.begin(), bib.empreintes.end(), [](const DefEmpreinte& d) { return d.id == "geda:newlib:0805"; });
  REQUIRE(e != bib.empreintes.end());
  CHECK(e->pastilles.size() == 2);
  CHECK(e->pastilles[0].type == TypePastille::CMS);
}

TEST_CASE("10.18 · carte P-CAD 2000 réelle : couches d'après leurs définitions, unités mêlées, polygones et textes repris", "[034][L10][10.18]") {
  const std::string o = donnee18("pcad/pcad_4layer_glyph_test_ascii.PCB");
  REQUIRE(estPcadAscii(o));
  RapportLectureCao r;
  const ConceptionElec c = conceptionDepuisPcad(o, "glyphes", &r);
  for (const auto& a : r.alertes) UNSCOPED_INFO(a);
  // Top (1), In1 (30), In2 (31), Bottom (2) : quatre cuivres ; chacun porte un polygone de cuivre
  CHECK(c.circuit.empilage.couchesCuivre == 4);
  std::set<Couche> couches;
  for (const auto& g : c.circuit.regions) couches.insert(g.couche);
  CHECK(couches.size() == 4);
  CHECK(couches.count(Couche::CuivreDessous));
  // Textes du cuivre du dessus et du dessous, ceux du dessous retournés ; « 17.79587 mm » lu en millimètres
  int dessus = 0, dessous = 0, retournes = 0;
  for (const auto& t : c.circuit.textes) {
    dessus += t.couche == Couche::CuivreDessus;
    dessous += t.couche == Couche::CuivreDessous;
    retournes += t.miroir.value_or(false);
  }
  CHECK(dessus >= 20);
  CHECK(dessous == 4);
  CHECK(retournes == 4);
  const auto copyright = std::find_if(c.circuit.textes.begin(), c.circuit.textes.end(), [](const TexteCircuit& t) { return t.texte == "C  COPYRIGHT"; });
  REQUIRE(copyright != c.circuit.textes.end());
  CHECK(largeurTexte(copyright->texte, copyright->hauteur) <= 17'800'000);
  // Le cuivre reste dans la carte (contour à la boîte du cuivre, textes compris)
  CHECK(codes18(c).count("DRC-06") == 0);
}
