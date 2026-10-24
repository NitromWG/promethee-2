// Prométhée : essais de la 0.34, lot L10 côté noyau — échanges : import KiCad complet (10.1), aller-retour (10.2).
// Licence GPL-3.0-only.
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "promethee/circuit_edition.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/kicad_electronique.hpp"

using namespace prom::ecad;

namespace {
std::string donnee(const std::string& f) { return prom::lireFichier(prom::cheminFichier(std::string(PROMETHEE_DONNEES) + "/" + f)); }
std::vector<std::string> pertes(const RapportImportKicad& r) {
  std::vector<std::string> v;
  for (const auto& a : r.alertes)
    if (a.find(" × ") != std::string::npos) v.push_back(a);
  return v;
}
}  // namespace

TEST_CASE("10.1 · StickHub réimporté sans perte listée : fentes, arcs, dessins, pastilles libres, toutes les couches", "[034][L10][10.1]") {
  RapportImportKicad r;
  const ConceptionElec c = conceptionDepuisKicad(donnee("kicad/StickHub.kicad_pcb"), "StickHub", &r);
  std::string liste;
  for (const auto& p : pertes(r)) liste += p + " | ";
  INFO(liste);
  CHECK(pertes(r).empty());
  CHECK(r.empreintes > 50);
  // L'autre carte d'essai non plus
  RapportImportKicad r2;
  conceptionDepuisKicad(donnee("kicad/ecc83-pp.kicad_pcb"), "ecc83", &r2);
  liste.clear();
  for (const auto& p : pertes(r2)) liste += p + " | ";
  INFO(liste);
  CHECK(pertes(r2).empty());
}

TEST_CASE("10.1 · éléments KiCad repris : fente verticale, pastille trapèze et libre, via borgne, interdiction, arc de piste, cote, six cuivres", "[034][L10][10.1]") {
  const std::string pcb = R"((kicad_pcb (version 20221018) (generator pcbnew)
  (general (thickness 1.6))
  (layers (0 "F.Cu" signal) (1 "In1.Cu" signal) (2 "In2.Cu" signal) (3 "In3.Cu" signal) (4 "In4.Cu" signal) (31 "B.Cu" signal)
          (36 "B.SilkS" user) (37 "F.SilkS" user) (44 "Edge.Cuts" user) (40 "Dwgs.User" user))
  (net 0 "") (net 1 "GND") (net 2 "SIG")
  (gr_rect (start 0 0) (end 40 30) (layer "Edge.Cuts") (width 0.1))
  (footprint "Essai:Fente" (layer "F.Cu") (at 10 10)
    (property "Reference" "J1")
    (pad "1" thru_hole oval (at 0 0) (size 1.2 2.4) (drill oval 0.6 1.8) (layers "*.Cu" "*.Mask") (net 1 "GND"))
    (pad "2" smd trapezoid (at 3 0) (size 1 1) (rect_delta 0 0.3) (layers "F.Cu" "F.Paste" "F.Mask") (net 2 "SIG"))
    (pad "3" smd custom (at 6 0) (size 0.5 0.5) (layers "F.Cu" "F.Mask")
      (options (clearance outline) (anchor rect))
      (primitives (gr_poly (pts (xy -1 -1) (xy 1 -1) (xy 1 1)) (width 0.1))))
    (pad "" smd rect (at 9 0) (size 1 1) (layers "F.Paste"))
    (fp_arc (start 0 0) (end 2 0) (angle 90) (layer "F.SilkS") (width 0.12)))
  (segment (start 10 10) (end 20 10) (width 0.25) (layer "In3.Cu") (net 1))
  (arc (start 20 10) (mid 22.93 12.93) (end 20 20) (width 0.25) (layer "F.Cu") (net 2))
  (via blind (at 25 25) (size 0.6) (drill 0.3) (layers "F.Cu" "In2.Cu") (net 1))
  (via micro (at 26 25) (size 0.3) (drill 0.1) (layers "F.Cu" "In1.Cu") (net 2))
  (zone (net 0) (net_name "") (layers "F.Cu" "B.Cu") (name "Sous l'antenne") (hatch edge 0.5)
    (keepout (tracks not_allowed) (vias not_allowed) (pads allowed) (copperpour not_allowed) (footprints allowed))
    (polygon (pts (xy 30 2) (xy 38 2) (xy 38 8) (xy 30 8))))
  (gr_circle (center 5 25) (end 6 25) (layer "Dwgs.User") (width 0.1))
  (gr_text "COUCHE IN3" (at 15 15) (layer "In3.Cu") (effects (font (size 1 1) (thickness 0.15))))
  (dimension (type aligned) (layer "Dwgs.User") (pts (xy 0 32) (xy 40 32)) (height 2))
))";
  RapportImportKicad r;
  const ConceptionElec c = conceptionDepuisKicad(pcb, "Essai", &r);
  std::string liste;
  for (const auto& p : pertes(r)) liste += p + " | ";
  INFO(liste);
  CHECK(pertes(r).empty());
  CHECK(c.circuit.empilage.couchesCuivre == 6);
  REQUIRE(c.circuit.empreintes.size() == 1);
  const DefEmpreinte* d = c.empreinte(c.circuit.empreintes[0].empreinte);
  REQUIRE(d);
  const auto pastille = [&](const std::string& n) -> const Pastille* {
    for (const auto& p : d->pastilles)
      if (p.numero == n) return &p;
    return nullptr;
  };
  REQUIRE(pastille("1"));
  CHECK(pastille("1")->fente());
  CHECK(pastille("1")->longueurPercage == 1'800'000);
  CHECK(pastille("1")->percage == 600'000);
  REQUIRE(pastille("2"));
  CHECK(pastille("2")->forme == FormePastille::Trapeze);
  CHECK(pastille("2")->trapeze == 300'000);
  REQUIRE(pastille("3"));
  CHECK(pastille("3")->forme == FormePastille::Libre);
  CHECK(pastille("3")->contourLibre.size() >= 4);
  CHECK(d->pastilles.size() == 4);  // l'ouverture de pâte seule est gardée
  CHECK(!d->traits.empty());        // l'arc de KiCad 5
  // Piste sur In3.Cu, arc de piste gardé en arc
  CHECK(std::any_of(c.circuit.pistes.begin(), c.circuit.pistes.end(), [](const Piste& p) { return p.couche == Couche::CuivreInt3; }));
  CHECK(std::any_of(c.circuit.pistes.begin(), c.circuit.pistes.end(), [](const Piste& p) { return p.milieu.has_value(); }));
  // Vias borgne et micro
  REQUIRE(c.circuit.vias.size() == 2);
  CHECK(c.circuit.vias[0].type == TypeVia::Borgne);
  CHECK(c.circuit.vias[0].vers == Couche::CuivreInt2);
  CHECK(c.circuit.vias[1].type == TypeVia::Micro);
  // Interdiction, dessin, texte sur un cuivre interne, cote
  REQUIRE(c.circuit.zonesRegles.size() == 1);
  CHECK(c.circuit.zonesRegles[0].interditPistes);
  CHECK(!c.circuit.zonesRegles[0].interditPastilles);
  CHECK(c.circuit.dessins.size() == 1);
  CHECK(std::any_of(c.circuit.textes.begin(), c.circuit.textes.end(), [](const TexteCircuit& t) { return t.couche == Couche::CuivreInt3; }));
  CHECK(c.circuit.cotes.size() == 1);
}

TEST_CASE("10.2 · aller-retour KiCad : même carte relue (pastilles, pistes, arcs, vias, interdictions, cotes) et même DRC ; .kicad_pro des règles", "[034][L10][10.2]") {
  RapportImportKicad r;
  const ConceptionElec a = conceptionDepuisKicad(donnee("kicad/StickHub.kicad_pcb"), "StickHub", &r);
  const Connectivite ka = connectivite(a);
  const std::string sortie = kicadDepuisConception(a, ka);
  RapportImportKicad r2;
  const ConceptionElec b = conceptionDepuisKicad(sortie, "StickHub", &r2, projetKicadDepuisConception(a));
  CHECK(pertes(r2).empty());
  CHECK(b.circuit.empreintes.size() == a.circuit.empreintes.size());
  CHECK(b.circuit.pistes.size() == a.circuit.pistes.size());
  CHECK(b.circuit.vias.size() == a.circuit.vias.size());
  CHECK(b.circuit.zones.size() == a.circuit.zones.size());
  CHECK(b.circuit.zonesRegles.size() == a.circuit.zonesRegles.size());
  CHECK(b.circuit.dessins.size() == a.circuit.dessins.size());
  CHECK(b.circuit.cotes.size() == a.circuit.cotes.size());
  std::size_t pa = 0, pb = 0;
  for (const auto& [id, d] : a.empreintes) pa += d.pastilles.size();
  for (const auto& [id, d] : b.empreintes) pb += d.pastilles.size();
  CHECK(pa == pb);
  // Mêmes réseaux sur les mêmes broches
  const Connectivite kb = connectivite(b);
  std::multiset<std::string> ra, rb;
  for (const auto& p : pastilles(a, ka)) ra.insert(p.reference + "." + p.numero + "=" + p.reseau);
  for (const auto& p : pastilles(b, kb)) rb.insert(p.reference + "." + p.numero + "=" + p.reseau);
  CHECK(ra == rb);
  // Même DRC, code par code
  auto codes = [](const ConceptionElec& c) {
    const Connectivite k = connectivite(c);
    std::map<std::string, int> m;
    for (const auto& v : verifierDRC(c, k, remplirZones(c, k))) ++m[v.code];
    return m;
  };
  const auto ca = codes(a), cb = codes(b);
  std::string ta, tb;
  for (const auto& [k, n] : ca) ta += k + "×" + std::to_string(n) + " ";
  for (const auto& [k, n] : cb) tb += k + "×" + std::to_string(n) + " ";
  INFO("avant : " << ta << " | relu : " << tb);
  CHECK(ca == cb);
  // Le projet porte les règles et les classes
  const auto pro = nlohmann::json::parse(projetKicadDepuisConception(a));
  CHECK(pro["board"]["design_settings"]["rules"]["min_clearance"].get<double>() == mm(a.circuit.regles.isolement));
  CHECK(pro["net_settings"]["classes"].size() == a.circuit.regles.classes.size() + 1);
}

#include "promethee/echanges_electronique.hpp"
#include "promethee/kicad_schema.hpp"

namespace {
const char* const LIB_KICAD = R"(  (lib_symbols
    (symbol "Device:R" (pin_numbers hide) (pin_names (offset 0)) (in_bom yes) (on_board yes)
      (property "Reference" "R" (at 2.032 0 90) (effects (font (size 1.27 1.27))))
      (property "Value" "R" (at 0 0 90) (effects (font (size 1.27 1.27))))
      (property "Footprint" "" (at -1.778 0 90) (effects (font (size 1.27 1.27)) hide))
      (symbol "R_0_1" (rectangle (start -1.016 -2.54) (end 1.016 2.54) (stroke (width 0.254) (type default)) (fill (type none))))
      (symbol "R_1_1"
        (pin passive line (at 0 3.81 270) (length 1.27) (name "~" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))
        (pin passive line (at 0 -3.81 90) (length 1.27) (name "~" (effects (font (size 1.27 1.27)))) (number "2" (effects (font (size 1.27 1.27)))))))
    (symbol "Connector:Conn_01x02" (pin_names (offset 1.016) hide) (in_bom yes) (on_board yes)
      (property "Reference" "J" (at 0 2.54 0) (effects (font (size 1.27 1.27))))
      (property "Value" "Conn_01x02" (at 0 -5.08 0) (effects (font (size 1.27 1.27))))
      (symbol "Conn_01x02_1_1" (rectangle (start -1.27 1.27) (end 1.27 -3.81) (stroke (width 0.254) (type default)) (fill (type background)))
        (pin passive line (at -5.08 0 0) (length 3.81) (name "Pin_1" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))
        (pin passive line (at -5.08 -2.54 0) (length 3.81) (name "Pin_2" (effects (font (size 1.27 1.27)))) (number "2" (effects (font (size 1.27 1.27)))))))
    (symbol "power:GND" (power) (pin_names (offset 0)) (in_bom yes) (on_board yes)
      (property "Reference" "#PWR" (at 0 -6.35 0) (effects (font (size 1.27 1.27)) hide))
      (property "Value" "GND" (at 0 -3.81 0) (effects (font (size 1.27 1.27))))
      (symbol "GND_0_1" (polyline (pts (xy 0 0) (xy 0 -1.27) (xy 1.27 -1.27) (xy 0 -2.54) (xy -1.27 -1.27) (xy 0 -1.27)) (stroke (width 0) (type default)) (fill (type none))))
      (symbol "GND_1_1" (pin power_in line (at 0 0 270) (length 0) hide (name "GND" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))))
    (symbol "power:+5V" (power) (pin_names (offset 0)) (in_bom yes) (on_board yes)
      (property "Reference" "#PWR" (at 0 -3.81 0) (effects (font (size 1.27 1.27)) hide))
      (property "Value" "+5V" (at 0 3.556 0) (effects (font (size 1.27 1.27))))
      (symbol "+5V_0_1" (polyline (pts (xy -0.762 1.27) (xy 0 2.54) (xy 0.762 1.27)) (stroke (width 0) (type default)) (fill (type none))))
      (symbol "+5V_1_1" (pin power_in line (at 0 0 90) (length 0) hide (name "+5V" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27))))))))
)";
std::string symboleKicad(const std::string& lib, const std::string& ref, const std::string& valeur, double x, double y, int angle, const std::string& chemin, const std::string& uuid) {
  return "  (symbol (lib_id \"" + lib + "\") (at " + std::to_string(x) + " " + std::to_string(y) + " " + std::to_string(angle) + ") (unit 1) (in_bom yes) (on_board yes)\n    (uuid " + uuid +
         ")\n    (property \"Reference\" \"" + ref + "\" (at 0 0 0) (effects (font (size 1.27 1.27))))\n    (property \"Value\" \"" + valeur +
         "\" (at 0 0 0) (effects (font (size 1.27 1.27))))\n    (property \"Footprint\" \"Resistor_SMD:R_0603_1608Metric\" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n    (instances (project \"essai\" (path \"" +
         chemin + "\" (reference \"" + ref + "\") (unit 1)))))\n";
}
std::string fil(double x1, double y1, double x2, double y2) {
  return "  (wire (pts (xy " + std::to_string(x1) + " " + std::to_string(y1) + ") (xy " + std::to_string(x2) + " " + std::to_string(y2) + ")) (stroke (width 0) (type default)) (uuid " + nouvelUuid() + "))\n";
}
using Reseaux = std::set<std::set<std::string>>;
Reseaux reseauxDe(const ConceptionElec& c) {
  Reseaux r;
  for (const auto& [nom, broches] : netlistDe(c).reseaux) {
    std::set<std::string> e;
    for (const auto& [ref, b] : broches) e.insert(ref + "." + b);
    if (!e.empty()) r.insert(e);
  }
  return r;
}
}  // namespace

TEST_CASE("10.3 · schéma KiCad hiérarchique importé, réseaux identiques ; exporté puis relu sans changer un réseau", "[034][L10][10.3]") {
  const std::string R = "11111111-1111-1111-1111-111111111111", S = "22222222-2222-2222-2222-222222222222";
  std::string racine = "(kicad_sch (version 20230121) (generator eeschema)\n  (uuid " + R + ")\n  (paper \"A4\")\n" + LIB_KICAD;
  racine += symboleKicad("Connector:Conn_01x02", "J1", "Conn", 50, 50, 0, "/" + R, nouvelUuid());
  racine += symboleKicad("Device:R", "R1", "1k", 70, 40, 0, "/" + R, nouvelUuid());
  racine += "  (symbol (lib_id \"power:+5V\") (at 44.92 36.19 0) (unit 1) (in_bom yes) (on_board yes) (uuid " + nouvelUuid() +
            ")\n    (property \"Reference\" \"#PWR01\" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n    (property \"Value\" \"+5V\" (at 0 0 0) (effects (font (size 1.27 1.27)))))\n";
  racine += fil(44.92, 50, 44.92, 36.19) + fil(44.92, 36.19, 70, 36.19);           // +5V : J1.1, R1.1
  racine += fil(70, 43.81, 90, 43.81);                                              // R1.2 → port IN
  racine += fil(110, 50, 115, 50) + fil(115, 50, 115, 30) + fil(115, 30, 40, 30) + fil(40, 30, 40, 52.54) + fil(40, 52.54, 44.92, 52.54);  // port OUT → J1.2
  racine += "  (sheet (at 90 40) (size 20 15) (fields_autoplaced) (stroke (width 0.1524) (type solid)) (fill (color 0 0 0 0.0000))\n    (uuid " + S +
            ")\n    (property \"Sheetname\" \"Filtre\" (at 90 39 0) (effects (font (size 1.27 1.27))))\n    (property \"Sheetfile\" \"filtre.kicad_sch\" (at 90 56 0) (effects (font (size 1.27 1.27))))\n"
            "    (pin \"IN\" input (at 90 43.81 180) (effects (font (size 1.27 1.27))) (uuid " + nouvelUuid() + "))\n    (pin \"OUT\" output (at 110 50 0) (effects (font (size 1.27 1.27))) (uuid " +
            nouvelUuid() + ")))\n)\n";
  std::string fille = "(kicad_sch (version 20230121) (generator eeschema)\n  (uuid " + nouvelUuid() + ")\n  (paper \"A4\")\n" + LIB_KICAD;
  const std::string chemin = "/" + R + "/" + S;
  fille += symboleKicad("Device:R", "R2", "10k", 100, 60, 90, chemin, nouvelUuid());
  fille += symboleKicad("Device:R", "R3", "4k7", 110, 65, 0, chemin, nouvelUuid());
  fille += "  (symbol (lib_id \"power:GND\") (at 110 72 0) (unit 1) (in_bom yes) (on_board yes) (uuid " + nouvelUuid() +
           ")\n    (property \"Reference\" \"#PWR02\" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n    (property \"Value\" \"GND\" (at 0 0 0) (effects (font (size 1.27 1.27)))))\n";
  fille += "  (hierarchical_label \"IN\" (shape input) (at 90 60 180) (effects (font (size 1.27 1.27))) (uuid " + nouvelUuid() + "))\n";
  fille += "  (hierarchical_label \"OUT\" (shape output) (at 120 60 0) (effects (font (size 1.27 1.27))) (uuid " + nouvelUuid() + "))\n";
  fille += "  (junction (at 110 60) (diameter 0) (color 0 0 0 0) (uuid " + nouvelUuid() + "))\n";
  fille += fil(90, 60, 96.19, 60) + fil(103.81, 60, 110, 60) + fil(110, 60, 110, 61.19) + fil(110, 60, 120, 60) + fil(110, 68.81, 110, 72) + ")\n";
  RapportImportSchemaKicad rapport;
  const ConceptionElec c = conceptionDepuisSchemaKicad(racine, "Essai", [&](const std::string& f) -> std::optional<std::string> {
    if (f == "filtre.kicad_sch") return fille;
    return std::nullopt;
  }, &rapport);
  std::string alertes;
  for (const auto& a : rapport.alertes) alertes += a + " | ";
  INFO(alertes);
  CHECK(rapport.alertes.empty());
  CHECK(c.feuilles.size() == 2);
  CHECK(rapport.blocs == 1);
  CHECK(c.composants.size() == 4);
  const Reseaux attendus{{"J1.1", "R1.1"}, {"R1.2", "R2.1"}, {"J1.2", "R2.2", "R3.1"}, {"R3.2"}};
  const Reseaux lus = reseauxDe(c);
  for (const auto& e : lus) {
    std::string t;
    for (const auto& x : e) t += x + " ";
    UNSCOPED_INFO("réseau : " << t);
  }
  CHECK(lus == attendus);
  // Noms des réseaux d'alimentation repris
  const auto n = netlistDe(c);
  CHECK(n.reseaux.count("+5V"));
  CHECK(n.reseaux.count("GND"));
  // Empreinte connue du catalogue
  CHECK(c.composant(c.composants[1].uuid)->empreinte == "R_0603");
  // Export puis relecture : mêmes réseaux
  const auto fichiers = schemaKicadDepuisConception(c, "essai");
  REQUIRE(fichiers.size() == 2);
  const ConceptionElec relu = conceptionDepuisSchemaKicad(fichiers.at("essai.kicad_sch"), "Relu", [&](const std::string& f) -> std::optional<std::string> {
    const auto it = fichiers.find(f);
    return it == fichiers.end() ? std::nullopt : std::optional<std::string>(it->second);
  });
  CHECK(reseauxDe(relu) == attendus);
  CHECK(relu.composants.size() == 4);
}

#include "promethee/import_bibliotheques.hpp"
#include "promethee/import_eagle.hpp"

TEST_CASE("10.4 · bibliothèques KiCad entières importées, une ligne de rapport par objet, articles reliés à leurs empreintes", "[034][L10][10.4]") {
  // Bibliothèque de symboles : la résistance, le connecteur, deux alimentations (refusées : celles de Prométhée)
  std::string sym = LIB_KICAD;
  sym.replace(sym.find("(lib_symbols"), 12, "(kicad_symbol_lib (version 20220914) (generator kicad_symbol_editor)");
  for (const std::string lib : {"Device:", "Connector:", "power:"})
    for (std::size_t p; (p = sym.find("\"" + lib)) != std::string::npos;) sym.erase(p + 1, lib.size());
  // L'empreinte que nomme la résistance est livrée dans la même bibliothèque
  sym.replace(sym.find("(property \"Footprint\" \"\""), 25, "(property \"Footprint\" \"Essai:BGA-256\"");
  const std::string bga = donnee("kicad/BGA-256_17.0x17.0mm_Layout16x16_P1.0mm_Ball0.5mm_Pad0.4mm_NSMD.kicad_mod");
  std::string bgaRenomme = bga;
  bgaRenomme.replace(bgaRenomme.find("BGA-256_17.0x17.0mm_Layout16x16_P1.0mm_Ball0.5mm_Pad0.4mm_NSMD"), 62, "BGA-256");
  const std::vector<FichierBibliotheque> fichiers{{"Essai.kicad_sym", sym}, {"Essai.pretty/BGA-256.kicad_mod", bgaRenomme}, {"Essai.pretty/vide.kicad_mod", "(footprint \"vide\" (layer \"F.Cu\"))"}};
  ContenuBibliotheque b;
  b.nom = "Équipe";
  const RapportBibliotheque r = importerBibliotheques(fichiers, b);
  for (const auto& l : r.objets) UNSCOPED_INFO(l.genre << " " << l.nom << " " << l.etat << " " << l.detail);
  CHECK(r.compter("symbole", "importé") == 2);
  CHECK(r.compter("symbole", "refusé") == 2);  // GND, +5V
  CHECK(r.compter("empreinte", "importé") == 1);
  CHECK(r.compter("empreinte", "refusé") == 1);  // sans pastille
  CHECK(r.compter("article", "importé") == 2);
  CHECK(r.objets.size() == 8);
  REQUIRE(b.articles.size() == 2);
  const auto ar = std::find_if(b.articles.begin(), b.articles.end(), [](const Article& a) { return a.nom == "R"; });
  REQUIRE(ar != b.articles.end());
  CHECK(ar->empreinte == "kicad:Essai:BGA-256");
  CHECK(std::any_of(b.empreintes.begin(), b.empreintes.end(), [](const DefEmpreinte& e) { return e.id == "kicad:Essai:BGA-256" && e.pastilles.size() == 256; }));
  // Rapport en CSV : une ligne par objet et l'en-tête
  const std::string csv = r.csv();
  CHECK(std::count(csv.begin(), csv.end(), '\n') == 9);
  // Deuxième passage : tout est gardé, puis tout est remplacé sur demande
  const RapportBibliotheque r2 = importerBibliotheques(fichiers, b);
  CHECK(r2.compter("article", "gardé") == 2);
  const RapportBibliotheque r3 = importerBibliotheques(fichiers, b, true);
  CHECK(r3.compter("article", "remplacé") == 2);
  CHECK(b.articles.size() == 2);
}

TEST_CASE("10.5 · projet EAGLE d'exemple ouvert : schéma et carte reliés, mêmes réseaux des deux côtés", "[034][L10][10.5]") {
  RapportImportEagle r;
  const ConceptionElec c = conceptionDepuisEagle(donnee("eagle/ampli.sch"), donnee("eagle/ampli.brd"), "Ampli", &r);
  std::string alertes;
  for (const auto& a : r.alertes) alertes += a + " | ";
  INFO(alertes);
  CHECK(r.alertes.empty());
  CHECK(r.feuilles == 2);
  CHECK(r.parties == 5);
  CHECK(r.elements == 5);
  CHECK(r.vias == 1);
  CHECK(r.polygones == 1);
  CHECK(r.pistes == 6);
  REQUIRE(c.feuilles.size() == 2);
  // Réseaux du schéma : réunis d'une feuille à l'autre (OUT, GND), alimentations nommées, porte A et alimentation P du LM358
  const Reseaux attendus{{"R1.2"}, {"R1.1", "IC1.3"}, {"IC1.2", "R2.1", "R4.2"}, {"IC1.1", "R2.2", "R3.1"}, {"R4.1", "R3.2", "IC1.4"}, {"IC1.8"}};
  const Reseaux lus = reseauxDe(c);
  for (const auto& e : lus) {
    std::string t;
    for (const auto& x : e) t += x + " ";
    UNSCOPED_INFO("réseau : " << t);
  }
  CHECK(lus == attendus);
  const auto n = netlistDe(c);
  CHECK(n.reseaux.count("OUT"));
  CHECK(n.reseaux.count("GND"));
  CHECK(n.reseaux.count("VCC"));
  CHECK(n.reseaux.count("IN"));
  // Carte reliée : chaque élément posé est la partie du schéma de même nom, avec son empreinte
  int relies = 0;
  for (const auto& em : c.circuit.empreintes) {
    const ComposantElec* x = c.composant(em.composant);
    REQUIRE(x);
    if (x->carteSeule) continue;
    CHECK(!x->symbole.empty());
    CHECK(x->empreinte == em.empreinte);
    ++relies;
  }
  CHECK(relies == 5);
  // Les pastilles de la carte sont sur les réseaux du schéma, et le cuivre de la carte suit leurs noms
  const Connectivite k = connectivite(c);
  auto uuid = [&](const std::string& ref) {
    for (const auto& x : c.composants)
      if (x.reference == ref) return x.uuid;
    return std::string();
  };
  CHECK(k.nom(uuid("IC1"), "1") == "OUT");
  CHECK(k.nom(uuid("R3"), "1") == "OUT");
  CHECK(k.nom(uuid("IC1"), "4") == "GND");
  const std::string n1 = k.nom(uuid("R1"), "1");
  CHECK(n1 == k.nom(uuid("IC1"), "3"));
  int pistesN1 = 0, pistesOut = 0;
  for (const auto& p : c.circuit.pistes) {
    pistesN1 += p.reseau == n1;
    pistesOut += p.reseau == "OUT";
  }
  CHECK(pistesN1 == 2);  // « N$1 » de la carte a pris le nom du schéma
  CHECK(pistesOut == 3);
  CHECK(std::count_if(c.circuit.pistes.begin(), c.circuit.pistes.end(), [](const Piste& p) { return p.milieu.has_value(); }) == 1);
  REQUIRE(c.circuit.zones.size() == 1);
  CHECK(c.circuit.zones.front().reseau == "GND");
  CHECK(c.circuit.zones.front().couche == Couche::CuivreDessous);
  CHECK(c.circuit.vias.front().reseau == "OUT");
  // R3 retournée sur la face de dessous, R1 tournée d'un quart, R4 verrouillée
  for (const auto& em : c.circuit.empreintes) {
    const std::string ref = c.composant(em.composant)->reference;
    if (ref == "R3") CHECK(em.dessous);
    if (ref == "R1") CHECK(em.rotation == 90);
  }
  // Contour à coin arrondi (arc de 5 mm) : aire de 60 × 40 moins le coin
  const double aire = std::abs(aireMm2(c.circuit.contour.libre));
  CHECK(std::abs(aire - (2400 - (25 - 25 * 3.14159265 / 4))) < 0.5);
  // Étiquettes globales du réseau OUT, présent sur les deux feuilles
  for (const auto& f : c.feuilles) CHECK(std::any_of(f.etiquettes.begin(), f.etiquettes.end(), [](const Etiquette& e) { return e.texte == "OUT" && e.type == TypeEtiquette::Globale; }));
  // La DRC ne voit aucun court-circuit entre réseaux reliés différemment des deux côtés
  const auto v = verifierDRC(c, k, remplirZones(c, k));
  CHECK(std::none_of(v.begin(), v.end(), [](const Violation& x) { return x.code == "DRC-02"; }));
}

TEST_CASE("10.18 · bibliothèque EAGLE de 50 composants convertie en bibliothèque d'équipe : 50 articles, un rapport par objet", "[034][L10][10.18]") {
  std::string lbr = R"(<?xml version="1.0" encoding="utf-8"?>
<eagle version="9.6.2"><drawing><library name="maison"><packages>
<package name="R0603"><smd name="1" x="-0.8" y="0" dx="0.9" dy="0.95" layer="1"/><smd name="2" x="0.8" y="0" dx="0.9" dy="0.95" layer="1"/></package>
<package name="C0805"><smd name="1" x="-0.95" y="0" dx="1.3" dy="1.5" layer="1"/><smd name="2" x="0.95" y="0" dx="1.3" dy="1.5" layer="1"/></package>
<package name="SOT23"><smd name="1" x="-0.95" y="-1.1" dx="0.6" dy="0.7" layer="1"/><smd name="2" x="0.95" y="-1.1" dx="0.6" dy="0.7" layer="1"/><smd name="3" x="0" y="1.1" dx="0.6" dy="0.7" layer="1"/></package>
<package name="VIDE"><wire x1="0" y1="0" x2="1" y2="0" width="0.1" layer="21"/></package>
</packages><symbols>
<symbol name="R"><wire x1="-2.54" y1="-0.889" x2="2.54" y2="-0.889" width="0.254" layer="94"/><pin name="1" x="-5.08" y="0" length="short" direction="pas"/><pin name="2" x="5.08" y="0" length="short" direction="pas" rot="R180"/></symbol>
<symbol name="C"><pin name="1" x="0" y="2.54" length="short" direction="pas" rot="R270"/><pin name="2" x="0" y="-5.08" length="short" direction="pas" rot="R90"/></symbol>
<symbol name="NPN"><pin name="B" x="-2.54" y="0" length="short" direction="pas"/><pin name="C" x="2.54" y="5.08" length="short" direction="pas" rot="R270"/><pin name="E" x="2.54" y="-5.08" length="short" direction="pas" rot="R90"/></symbol>
</symbols><devicesets>
)";
  auto ensemble = [](const std::string& nom, const std::string& prefixe, const std::string& symbole, const std::string& paquet, const std::vector<std::pair<std::string, std::string>>& liens) {
    std::string s = "<deviceset name=\"" + nom + "\" prefix=\"" + prefixe + "\"><description>" + nom + "</description><gates><gate name=\"G$1\" symbol=\"" + symbole + "\" x=\"0\" y=\"0\"/></gates><devices><device name=\"\" package=\"" + paquet + "\"><connects>";
    for (const auto& [broche, pastille] : liens) s += "<connect gate=\"G$1\" pin=\"" + broche + "\" pad=\"" + pastille + "\"/>";
    return s + "</connects><technologies><technology name=\"\"/></technologies></device></devices></deviceset>\n";
  };
  for (int i = 0; i < 25; ++i) lbr += ensemble("R" + std::to_string(i) + "K", "R", "R", "R0603", {{"1", "1"}, {"2", "2"}});
  for (int i = 0; i < 15; ++i) lbr += ensemble("C" + std::to_string(i) + "N", "C", "C", "C0805", {{"1", "1"}, {"2", "2"}});
  for (int i = 0; i < 10; ++i) lbr += ensemble("BC8" + std::to_string(40 + i), "Q", "NPN", "SOT23", {{"B", "1"}, {"E", "2"}, {"C", "3"}});
  lbr += "</devicesets></library></drawing></eagle>\n";
  ContenuBibliotheque equipe;
  equipe.nom = "Équipe";
  const RapportBibliotheque r = importerBibliotheques({{"maison.lbr", lbr}}, equipe);
  CHECK(r.compter("article", "importé") == 50);
  CHECK(r.compter("symbole", "importé") == 50);
  CHECK(r.compter("empreinte", "importé") == 3);
  CHECK(r.compter("empreinte", "refusé") == 1);  // paquet sans pastille
  CHECK(r.objets.size() == 104);
  CHECK(equipe.articles.size() == 50);
  // Bibliothèque d'équipe enregistrée puis relue : rien de perdu, chaque article trouve son symbole et son empreinte
  const ContenuBibliotheque relue = lireContenuBibliotheque(texteContenuBibliotheque(equipe));
  CHECK(relue.articles.size() == 50);
  CHECK(relue.symboles.size() == 50);
  CHECK(relue.empreintes.size() == 3);
  const auto controles = controlerBibliotheque(relue);
  std::string t;
  for (const auto& m : controles) t += m + " | ";
  INFO(t);
  CHECK(controles.empty());
  // Brochage du transistor : B, E, C sur les pastilles 1, 2, 3
  const auto q = std::find_if(relue.symboles.begin(), relue.symboles.end(), [](const DefSymbole& d) { return d.nom == "BC840"; });
  REQUIRE(q != relue.symboles.end());
  std::map<std::string, std::string> nums;
  for (const auto& b : q->broches) nums[b.nom] = b.numero;
  CHECK(nums == std::map<std::string, std::string>{{"B", "1"}, {"C", "3"}, {"E", "2"}});
}

#include "promethee/import_easyeda.hpp"
#include "promethee/nomenclature_entree.hpp"

TEST_CASE("10.6 · composant LCSC importé d'EasyEDA avec son code, son empreinte et son modèle 3D", "[034][L10][10.6]") {
  nlohmann::json sym = {{"head", {{"x", "400"}, {"y", "300"}, {"c_para", {{"pre", "Q?"}, {"name", "MMBT3904"}, {"package", "SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR"}, {"BOM_Manufacturer", "CJ"}, {"BOM_Manufacturer Part", "MMBT3904"}, {"BOM_Supplier Part", "C20526"}}}}},
                        {"shape", {"P~show~0~1~380~300~180~gge1~0^^380~300^^M 380 300 h 10~#880000^^1~393~304~0~B~start~~~#0000FF^^1~387~299~0~1~end~~~#0000FF^^0~387~300^^0~M 390 297 L 393 300 L 390 303",
                                   "P~show~0~2~410~320~90~gge2~0^^410~320^^M 410 320 v -10~#880000^^1~411~314~270~E~start~~~#0000FF^^1~409~317~270~2~end~~~#0000FF^^0~410~313^^0~M 407 310 L 410 313 L 413 310",
                                   "P~show~0~3~410~280~270~gge3~0^^410~280^^M 410 280 v 10~#880000^^1~411~286~90~C~end~~~#0000FF^^1~409~283~90~3~start~~~#0000FF^^0~410~287^^0~M 407 290 L 410 287 L 413 290",
                                   "PL~390 290 390 310~#880000~1~0~none~gge4~0", "PL~390 300 410 290~#880000~1~0~none~gge5~0", "C~400~300~12~#880000~1~0~none~gge6~0"}}};
  const std::string svg = R"svg({"gId":"g1_outline","nodeName":"g","nodeType":1,"layerid":"19","attrs":{"c_width":"11.8","c_height":"7.5","c_rotation":"0,0,90","z":"0","c_origin":"4000,3000","uuid":"8a4b8e6e8d4e4b7a9f1c2b3a4d5e6f70","c_etype":"outline3D","id":"g1_outline","title":"SOT-23-3","layerid":"19","transform":"scale(1) translate(0, 0)"},"childNodes":[]})svg";
  nlohmann::json pkg = {{"head", {{"x", 4000}, {"y", 3000}, {"c_para", {{"package", "SOT-23-3_L2.9-W1.3-P1.90-LS2.4-BR"}}}}},
                        {"shape", {"PAD~RECT~3996.26~3004.33~3.5433~2.7559~1~~1~0~3994.4884 3005.7079 3994.4884 3002.952 3998.0316 3002.952 3998.0316 3005.7079~0~gge7~0~~Y~0~0~0.4~3996.26,3004.33",
                                   "PAD~RECT~4003.74~3004.33~3.5433~2.7559~1~~2~0~~0~gge8~0~~Y~0~0~0.4~4003.74,3004.33",
                                   "PAD~RECT~4000~2995.67~3.5433~2.7559~1~~3~0~~0~gge9~0~~Y~0~0~0.4~4000,2995.67",
                                   "TRACK~0.6~3~~3994 2997 3994 3003~gge10~0", "SVGNODE~" + svg}}};
  nlohmann::json reponse = {{"success", true}, {"result", {{"title", "MMBT3904"}, {"description", "Transistor NPN 40 V 200 mA"}, {"lcsc", {{"number", "C20526"}, {"url", "https://www.lcsc.com/product-detail/C20526.html"}}}, {"dataStr", sym}, {"packageDetail", {{"title", "SOT-23-3"}, {"dataStr", pkg}}}}}};
  // Modèle OBJ d'EasyEDA : un pavé de 2,9 × 1,3 × 1 mm
  const std::string obj = "v -1.45 -0.65 0\nv 1.45 -0.65 0\nv 1.45 0.65 0\nv -1.45 0.65 0\nv -1.45 -0.65 1\nv 1.45 -0.65 1\nv 1.45 0.65 1\nv -1.45 0.65 1\n"
                          "usemtl corps\nf 1 3 2\nf 1 4 3\nf 5 6 7\nf 5 7 8\nf 1 2 6\nf 1 6 5\nf 2 3 7\nf 2 7 6\nf 3 4 8\nf 3 8 7\nf 4 1 5\nf 4 5 8\n";
  CHECK(uuidModeleEasyEda(reponse.dump()) == "8a4b8e6e8d4e4b7a9f1c2b3a4d5e6f70");
  const ImportArchive r = importerComposantEasyEda(reponse.dump(), obj, "8a4b8e6e8d4e4b7a9f1c2b3a4d5e6f70.obj");
  std::string alertes;
  for (const auto& a : r.alertes) alertes += a + " | ";
  INFO(alertes);
  CHECK(r.alertes.empty());
  CHECK(r.controles.empty());
  CHECK(r.article.lcsc == "C20526");
  CHECK(r.article.mpn == "MMBT3904");
  CHECK(r.article.fabricant == "CJ");
  CHECK(r.article.id == "lcsc:C20526");
  REQUIRE(r.symboles.size() == 1);
  const DefSymbole& s = r.symboles.front();
  CHECK(s.prefixe == "Q");
  REQUIRE(s.broches.size() == 3);
  std::map<std::string, std::pair<std::string, int>> b;
  for (const auto& x : s.broches) b[x.numero] = {x.nom, x.direction};
  CHECK(b["1"] == std::pair<std::string, int>{"B", 0});
  CHECK(b["2"] == std::pair<std::string, int>{"E", 3});
  CHECK(b["3"] == std::pair<std::string, int>{"C", 1});
  for (const auto& x : s.broches) CHECK(x.longueur == 2'540'000);
  REQUIRE(r.empreinte);
  REQUIRE(r.empreinte->pastilles.size() == 3);
  const Pastille& p1 = r.empreinte->pastilles.front();
  CHECK(std::abs(mm(p1.position.x) + 0.95) < 0.01);
  CHECK(std::abs(mm(p1.position.y) - 1.1) < 0.01);
  CHECK(std::abs(mm(p1.largeur) - 0.9) < 0.01);
  CHECK(std::abs(mm(p1.hauteur) - 0.7) < 0.01);
  CHECK(s.empreinte == r.empreinte->id);
  // Modèle : 12 triangles, tourné d'un quart de tour (2,9 mm le long de y), posé sur la carte
  CHECK(r.nomModele3D == "C20526.stl");
  CHECK(r.article.modele3D == "C20526.stl");
  int facettes = 0;
  double xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9, zmin = 1e9, zmax = -1e9;
  std::istringstream in(r.modele3D);
  for (std::string l; std::getline(in, l);) {
    if (l.find("facet normal") != std::string::npos) ++facettes;
    if (const auto p = l.find("vertex"); p != std::string::npos) {
      std::istringstream v(l.substr(p + 6));
      double x, y, z;
      v >> x >> y >> z;
      xmin = std::min(xmin, x), xmax = std::max(xmax, x), ymin = std::min(ymin, y), ymax = std::max(ymax, y), zmin = std::min(zmin, z), zmax = std::max(zmax, z);
    }
  }
  CHECK(facettes == 12);
  CHECK(std::abs((xmax - xmin) - 1.3) < 1e-6);
  CHECK(std::abs((ymax - ymin) - 2.9) < 1e-6);
  CHECK(std::abs(zmin) < 1e-9);
  CHECK(std::abs(zmax - 1) < 1e-9);
  // Sans modèle joint : l'alerte dit l'identifiant à télécharger
  const ImportArchive r2 = importerComposantEasyEda(reponse.dump());
  CHECK(std::any_of(r2.alertes.begin(), r2.alertes.end(), [](const std::string& a) { return a.find("8a4b8e6e8d4e4b7a9f1c2b3a4d5e6f70") != std::string::npos; }));
}

TEST_CASE("10.6 · nomenclature CSV en entrée : plages de repères, champs reconnus, repères inconnus et quantités signalés", "[034][L10][10.6]") {
  CHECK(developperReperes("R1-R3, C5;C7 R10-12") == std::vector<std::string>{"R1", "R2", "R3", "C5", "C7", "R10", "R11", "R12"});
  ConceptionElec c = nouvelleConception("Nomenclature");
  for (const char* ref : {"R1", "R2", "R3", "R4", "C1"}) {
    ComposantElec x;
    x.uuid = nouvelUuid();
    x.reference = ref;
    x.valeur = "?";
    c.composants.push_back(x);
  }
  const auto t = lireTableau("Designator;Value;Manufacturer;MPN;LCSC Part;Quantity;Remarque;DNP\n\"R1-R3\";10k;Yageo;RC0603FR-0710KL;C25804;3;essai;\nC1;100n;;;C14663;2;;\nR4;1k;;;;1;;x\nR9;1k;;;;1;;\n");
  // Lecture seule d'abord : rien n'est écrit
  ConceptionElec copie = c;
  const auto apercu = appliquerNomenclature(copie, t, false);
  CHECK(copie.composants == c.composants);
  CHECK(!apercu.changements.empty());
  const auto r = appliquerNomenclature(c, t);
  CHECK(r.lignes == 4);
  CHECK(r.composants == 5);
  CHECK(r.inconnus == std::vector<std::string>{"R9"});
  CHECK(r.alertes.size() == 1);  // C1 : quantité 2 pour un repère
  CHECK(c.composants[0].valeur == "10k");
  CHECK(c.composants[2].mpn == "RC0603FR-0710KL");
  CHECK(c.composants[2].lcsc == "C25804");
  CHECK(c.composants[2].fabricant == "Yageo");
  CHECK(c.composants[2].champs.at("Remarque") == "essai");
  CHECK(c.composants[3].nePasMonter);
  CHECK(!c.composants[0].nePasMonter);
  CHECK(c.composants[4].lcsc == "C14663");
  CHECK(r.changements.size() == apercu.changements.size());
}

#include "promethee/import_commun.hpp"

namespace {
const char* const CARTE_4_COUCHES = R"((kicad_pcb (version 20221018) (generator pcbnew)
  (general (thickness 1.6))
  (layers (0 "F.Cu" signal) (1 "In1.Cu" signal "Masse") (2 "In2.Cu" signal) (31 "B.Cu" signal) (36 "B.SilkS" user) (37 "F.SilkS" user) (44 "Edge.Cuts" user))
  (net 0 "") (net 1 "GND") (net 2 "SIG")
  (gr_rect (start 0 0) (end 30 20) (layer "Edge.Cuts") (width 0.1))
  (footprint "Resistor_SMD:R_0603_1608Metric" (layer "F.Cu") (at 10 10)
    (property "Reference" "R1") (property "Value" "10k")
    (pad "1" smd roundrect (at -0.8 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (net 1 "GND"))
    (pad "2" smd roundrect (at 0.8 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (net 2 "SIG")))
  (footprint "Resistor_SMD:R_0603_1608Metric" (layer "F.Cu") (at 20 10)
    (property "Reference" "R2") (property "Value" "10k")
    (pad "1" smd roundrect (at -0.8 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (net 2 "SIG"))
    (pad "2" smd roundrect (at 0.8 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25) (net 1 "GND")))
  (segment (start 10.8 10) (end 12 10) (width 0.25) (layer "F.Cu") (net 2))
  (via (at 12 10) (size 0.6) (drill 0.3) (layers "F.Cu" "B.Cu") (net 2))
  (segment (start 12 10) (end 18 10) (width 0.25) (layer "In1.Cu") (net 2))
  (via (at 18 10) (size 0.6) (drill 0.3) (layers "F.Cu" "B.Cu") (net 2))
  (segment (start 18 10) (end 19.2 10) (width 0.25) (layer "F.Cu") (net 2))
  (segment (start 5 15) (end 25 15) (width 0.5) (layer "In2.Cu") (net 1))
)
)";
}  // namespace

TEST_CASE("10.12 · carte KiCad de quatre couches ajoutée à une carte de 20 composants : couches associées sans retouche, aucun repère en double", "[034][L10][10.12]") {
  ConceptionElec ouverte = carteDeMesure(20, false);
  const std::size_t n0 = ouverte.composants.size();
  REQUIRE(n0 >= 20);
  REQUIRE(ouverte.circuit.empilage.couchesCuivre == 2);
  ConceptionElec ajout = conceptionDepuisKicad(CARTE_4_COUCHES, "Ajout");
  REQUIRE(ajout.circuit.empilage.couchesCuivre == 4);
  // Tableau des couches : proposé sans retouche, noms d'origine montrés, quatre cuivres demandés
  const AssociationCouches a = proposerAssociation("KiCad", ajout, ouverte.circuit.empilage.couchesCuivre);
  CHECK(a.cuivresNecessaires == 4);
  std::map<std::string, std::string> vu;
  for (const auto& l : a.lignes) vu[l.origine] = l.cible ? codeCouche(*l.cible) : "";
  CHECK(vu["F.Cu"] == "F.Cu");
  CHECK(vu["In1.Cu « Masse »"] == "In1.Cu");
  CHECK(vu["In2.Cu"] == "In2.Cu");
  CHECK(vu["B.Cu"] == "B.Cu");
  CHECK(appliquerAssociation(ajout, a) == 0);
  // L'association gardée pour KiCad l'emporte à l'import suivant : In2.Cu ignorée
  auto gardee = associationAGarder(a);
  gardee["In2.Cu"] = "";
  ConceptionElec ajout2 = ajout;
  const AssociationCouches a2 = proposerAssociation("KiCad", ajout2, 2, gardee);
  CHECK(appliquerAssociation(ajout2, a2) == 1);
  CHECK(ajout2.circuit.pistes.size() == ajout.circuit.pistes.size() - 1);
  // Aperçu puis fusion : R1 et R2 renumérotés, GND homonyme montré, carte posée à droite
  const RapportFusion apercu = apercuFusion(ouverte, ajout);
  CHECK(apercu.renumerotes.size() == 2);
  CHECK(std::find(apercu.homonymes.begin(), apercu.homonymes.end(), "GND") != apercu.homonymes.end());
  const std::size_t pistesAvant = ouverte.circuit.pistes.size();
  const RapportFusion r = fusionnerConception(ouverte, ajout);
  CHECK(r.renumerotes == apercu.renumerotes);
  CHECK(ouverte.composants.size() == n0 + 2);
  std::set<std::string> refs;
  for (const auto& x : ouverte.composants) CHECK(refs.insert(x.reference).second);
  CHECK(ouverte.circuit.empilage.couchesCuivre == 4);
  CHECK(ouverte.circuit.pistes.size() == pistesAvant + 4);
  CHECK(std::count_if(ouverte.circuit.pistes.begin(), ouverte.circuit.pistes.end(), [](const Piste& p) { return p.couche == Couche::CuivreInt1; }) == 1);
  CHECK(std::count_if(ouverte.circuit.pistes.begin(), ouverte.circuit.pistes.end(), [](const Piste& p) { return p.couche == Couche::CuivreInt2; }) == 1);
  // Les pastilles des résistances ajoutées gardent leurs réseaux, et le cuivre ajouté ne court-circuite rien
  const Connectivite k = connectivite(ouverte);
  for (const auto& [ancien, nouveau] : r.renumerotes) {
    const ComposantElec* x = ouverte.parReference(nouveau);
    REQUIRE(x);
    CHECK(!k.nom(x->uuid, "1").empty());
  }
  const auto v = verifierDRC(ouverte, k, remplirZones(ouverte, k));
  CHECK(std::none_of(v.begin(), v.end(), [](const Violation& x) { return x.code == "DRC-21"; }));
}

TEST_CASE("10.12 · schéma et carte KiCad lus séparément puis reliés par les repères, le cuivre suit les noms du schéma", "[034][L10][10.12]") {
  std::string sch = "(kicad_sch (version 20230121) (generator eeschema)\n  (uuid 33333333-3333-3333-3333-333333333333)\n  (paper \"A4\")\n" + std::string(LIB_KICAD);
  const std::string chemin = "/33333333-3333-3333-3333-333333333333";
  sch += symboleKicad("Device:R", "R1", "1k", 100, 50, 0, chemin, nouvelUuid());
  sch += symboleKicad("Device:R", "R2", "2k", 120, 50, 0, chemin, nouvelUuid());
  sch += "  (symbol (lib_id \"power:+5V\") (at 100 40 0) (unit 1) (in_bom yes) (on_board yes) (uuid " + nouvelUuid() +
         ")\n    (property \"Reference\" \"#PWR01\" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n    (property \"Value\" \"+5V\" (at 0 0 0) (effects (font (size 1.27 1.27)))))\n";
  sch += "  (symbol (lib_id \"power:GND\") (at 120 60 0) (unit 1) (in_bom yes) (on_board yes) (uuid " + nouvelUuid() +
         ")\n    (property \"Reference\" \"#PWR02\" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))\n    (property \"Value\" \"GND\" (at 0 0 0) (effects (font (size 1.27 1.27)))))\n";
  sch += fil(100, 40, 100, 46.19) + fil(100, 53.81, 110, 53.81) + fil(110, 53.81, 110, 46.19) + fil(110, 46.19, 120, 46.19) + fil(120, 53.81, 120, 60) + ")\n";
  ConceptionElec schema = conceptionDepuisSchemaKicad(sch, "Projet", [](const std::string&) { return std::optional<std::string>(); });
  REQUIRE(schema.composants.size() == 2);
  std::string pcb = CARTE_4_COUCHES;
  // Sur la carte, le réseau du milieu porte le nom automatique de KiCad
  for (std::size_t p; (p = pcb.find("\"SIG\"")) != std::string::npos;) pcb.replace(p, 5, "\"Net-(R1-Pad2)\"");
  for (std::size_t p; (p = pcb.find("(net 1 \"GND\"))")) != std::string::npos;) pcb.replace(p, 14, "(net 3 \"+5V\"))");
  pcb.replace(pcb.find("(net 2 \"Net-(R1-Pad2)\")"), 0, "(net 3 \"+5V\") ");
  const ConceptionElec carte = conceptionDepuisKicad(pcb, "Projet");
  const RapportLiaison r = relierCarteAuSchema(schema, carte);
  CHECK(r.relies == 2);
  CHECK(r.carteSeule.empty());
  CHECK(r.schemaSeul.empty());
  CHECK(schema.circuit.empreintes.size() == 2);
  CHECK(schema.circuit.empilage.couchesCuivre == 4);
  const Connectivite k = connectivite(schema);
  const std::string milieu = k.nom(schema.parReference("R1")->uuid, "2");
  CHECK(milieu == k.nom(schema.parReference("R2")->uuid, "1"));
  CHECK(k.nom(schema.parReference("R1")->uuid, "1") == "+5V");
  // Les pistes du réseau du milieu portent le nom du schéma
  CHECK(std::count_if(schema.circuit.pistes.begin(), schema.circuit.pistes.end(), [&](const Piste& p) { return p.reseau == milieu; }) == 3);
  CHECK(std::count_if(schema.circuit.vias.begin(), schema.circuit.vias.end(), [&](const Via& v) { return v.reseau == milieu; }) == 2);
}
