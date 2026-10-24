// Lecture de cartes KiCad réelles (démos de KiCad, GPL). Licence GPL-3.0-only.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <map>

#include "promethee/kicad.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/fichiers.hpp"

using namespace prom;
using Catch::Approx;

namespace {
std::string donnee(const char* nom) { return std::string(PROMETHEE_DONNEES) + "/kicad/" + nom; }
const EmpreinteKicad* empreinte(const CarteKicad& c, const std::string& ref) {
  for (const auto& f : c.empreintes) if (f.ref == ref) return &f;
  return nullptr;
}
}  // namespace

TEST_CASE("Une carte KiCad rectangulaire est lue avec ses trous de fixation") {
  const CarteKicad c = lireFichierKicad(donnee("ecc83-pp.kicad_pcb"));
  CHECK(c.epaisseur == Approx(1.6));
  REQUIRE_FALSE(c.contours.empty());
  CHECK(c.contours.front().ferme);
  CHECK(c.contours.front().elements.size() == 4);
  CHECK(c.xmax - c.xmin == Approx(52.07).margin(1e-6));
  CHECK(c.ymax - c.ymin == Approx(46.355).margin(1e-6));
  CHECK(c.contours.front().aire() == Approx(52.07 * 46.355).epsilon(1e-6));
  CHECK(c.empreintes.size() == 15);
  const auto fixations = std::count_if(c.empreintes.begin(), c.empreintes.end(), [](const EmpreinteKicad& f) { return f.fixation; });
  CHECK(fixations == 4);
  for (const auto& f : c.empreintes) if (f.fixation) CHECK(f.percage == Approx(3.2));
  const EmpreinteKicad* c1 = empreinte(c, "C1");
  REQUIRE(c1 != nullptr);
  CHECK(c1->position.x == Approx(141.605));
  CHECK(c1->position.y == Approx(-99.695));  // axe Y retourné
  CHECK(c1->rotation == Approx(90));
  CHECK(c1->xmin < c1->position.x);
  CHECK(c1->xmax > c1->position.x);
  // Zone de placement circulaire de 10,5 mm de diamètre (condensateur radial de 10 mm).
  CHECK(c1->xmax - c1->xmin == Approx(10.5).margin(1e-6));
  CHECK(c1->ymax - c1->ymin == Approx(10.5).margin(1e-6));
}

TEST_CASE("Une carte KiCad au contour arrondi est reconstituée en une boucle fermée") {
  const CarteKicad c = lireFichierKicad(donnee("StickHub.kicad_pcb"));
  REQUIRE_FALSE(c.contours.empty());
  const Contour& ext = c.contours.front();
  CHECK(ext.ferme);
  CHECK(std::any_of(ext.elements.begin(), ext.elements.end(), [](const ElementContour& e) { return e.milieu.has_value(); }));
  CHECK(c.xmax - c.xmin == Approx(16.5).margin(1e-3));
  CHECK(c.ymax - c.ymin == Approx(40).margin(1e-3));
  CHECK(ext.aire() > 0);
  CHECK(ext.aire() < 16.5 * 40);  // les angles arrondis retirent un peu de surface
  CHECK(ext.aire() > 16.5 * 40 * 0.85);  // encoches et arrondis
  CHECK(c.empreintes.size() == 94);
}

TEST_CASE("Un fichier qui n'est pas une carte KiCad est refusé clairement") {
  CHECK_THROWS_WITH(lireKicad("(kicad_sch (version 1))"), Catch::Matchers::ContainsSubstring("pas une carte KiCad"));
  CHECK_THROWS_WITH(lireKicad("(kicad_pcb (general"), Catch::Matchers::ContainsSubstring("tronqué"));
}

#include "promethee/derive.hpp"
#include "promethee/echanges.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/verifs.hpp"

TEST_CASE("Une carte KiCad devient un projet dont le boîtier épouse le contour") {
  RapportImport r;
  const Projet p = projetDepuisKicad(lireFichierKicad(donnee("ecc83-pp.kicad_pcb")), "Préampli ECC83", &r);
  REQUIRE(p.carte.libre());
  CHECK(p.carte.L == Approx(52.07).margin(1e-3));
  CHECK(p.carte.W == Approx(46.355).margin(1e-3));
  CHECK(p.carte.x0 == Approx(0).margin(1e-6));
  CHECK(r.trous == 4);
  CHECK(r.composants == 11);
  CHECK(p.trous.size() == 4);
  for (const auto& t : p.trous) {
    CHECK(t.vis == std::optional<std::string>("M3"));
    CHECK(t.diamTrou == std::optional<double>(3.2));
    CHECK(t.ancrage == "libre");
  }
  const Derive d = deriver(p);
  CHECK(d.libre);
  CHECK(d.sdCarte(0, 0) < 0);
  CHECK(d.sdCarte(100, 0) > 0);
  (void)verifier(p, d);
  const TopoDS_Shape corps = construireCorps(p, d), couvercle = construireCouvercle(p, d), carte = construireCarte(p, d);
  CHECK(estValide(corps));
  CHECK(nombreSolides(corps) == 1);
  CHECK(estValide(couvercle));
  CHECK(estValide(carte));
  const Boite b = encombrement(corps);
  CHECK(b.xmax - b.xmin == Approx(52.07 + 2 * (p.boitier.jeu + p.boitier.paroi)).margin(0.01));
  // En Y, le connecteur P4 dépasse du bord de la carte : le boîtier s'élargit d'autant à cet endroit.
  double ymin = -23.1775, ymax = 23.1775;
  for (const auto& r : d.debords) { ymin = std::min(ymin, r.y1); ymax = std::max(ymax, r.y2); }
  CHECK(b.ymax - b.ymin == Approx(ymax - ymin + 2 * (p.boitier.jeu + p.boitier.paroi)).margin(0.02));
  CHECK(volume(carte) == Approx(52.07 * 46.355 * 1.6 - 4 * 3.14159265 * 1.6 * 1.6 * 1.6).epsilon(0.002));
  // Le projet se relit à l'identique, contour compris.
  const Projet q = normaliser(versJson(p));
  CHECK(versJson(q) == versJson(p));
}

TEST_CASE("Un contour arrondi donne un boîtier arrondi valide et des plans exacts") {
  const Projet p = projetDepuisKicad(lireFichierKicad(donnee("StickHub.kicad_pcb")), "");
  const Derive d = deriver(p);
  const TopoDS_Shape corps = construireCorps(p, d), couvercle = construireCouvercle(p, d);
  CHECK(estValide(corps));
  CHECK(nombreSolides(corps) == 1);
  CHECK(estValide(couvercle));
  CHECK(contourDecale(p, d, p.boitier.jeu).size() > 50);
  const std::string dxf = dxfCarte(p, d);
  CHECK(dxf.find("\r\nARC\r\n") != std::string::npos);
  CHECK(svgCarte(p, d).find("<path") != std::string::npos);
}

TEST_CASE("Les hauteurs sont estimées d'après le nom des empreintes") {
  CHECK(hauteurEstimee("Capacitor_SMD:C_0603_1608Metric") == Approx(0.8));
  CHECK(hauteurEstimee("Package_SO:SOIC-8_3.9x4.9mm_P1.27mm") == Approx(1.75));
  CHECK(hauteurEstimee("Capacitor_THT:CP_Radial_D10.0mm_P5.00mm") == Approx(16));
  CHECK(hauteurEstimee("Connector_USB:USB_C_Receptacle_GCT_USB4085") == Approx(3.3));
  CHECK(hauteurEstimee("Truc:Inconnu") == Approx(2));
}

TEST_CASE("Les composants qui débordent élargissent le boîtier autour d'eux, sans toucher au jeu") {
  const Projet p = projetDepuisKicad(lireFichierKicad(donnee("StickHub.kicad_pcb")), "");
  const Derive d = deriver(p);
  REQUIRE_FALSE(d.debords.empty());  // J5 dépasse du contour dans KiCad
  CHECK(p.boitier.jeu == Approx(1));
  // Plus aucune erreur de débordement : la cavité contourne chaque composant avec le jeu.
  for (const auto& q : verifier(p, d)) CHECK(q.code != "touche_paroi");
  for (const auto& r : d.debords) {
    CHECK(d.sdCavite(r.x1, r.y1) <= -p.boitier.jeu + 1e-6);
    CHECK(d.sdCavite(r.x2, r.y2) <= -p.boitier.jeu + 1e-6);
  }
  const TopoDS_Shape corps = construireCorps(p, d);
  CHECK(estValide(corps));
  CHECK(nombreSolides(corps) == 1);
  CHECK(estValide(construireCouvercle(p, d)));
  // La paroi dessinée en 2D suit aussi l'élargissement.
  const auto paroi = contourDecale(p, d, p.boitier.jeu + p.boitier.paroi);
  double xmax = -1e9, ymax = -1e9;
  for (const auto& q : paroi) { xmax = std::max(xmax, q.x); ymax = std::max(ymax, q.y); }
  double rx = -1e9, ry = -1e9;
  for (const auto& r : d.debords) { rx = std::max(rx, r.x2); ry = std::max(ry, r.y2); }
  CHECK(std::max(xmax - rx, ymax - ry) >= p.boitier.jeu + p.boitier.paroi - 0.05);
}

TEST_CASE("La face arrière est importée et a ses propres vérifications") {
  RapportImport r;
  Projet p = projetDepuisKicad(lireFichierKicad(donnee("StickHub.kicad_pcb")), "", &r);
  CHECK(r.dessous > 0);
  const auto nDessous = std::count_if(p.composants.begin(), p.composants.end(), [](const Composant& k) { return k.dessous; });
  CHECK(nDessous == r.dessous);
  // Un composant de la face arrière plus haut que les entretoises touche le fond : erreur et correction.
  for (auto& k : p.composants) if (k.dessous) { k.h = 7; break; }
  auto pb = verifier(p, deriver(p));
  REQUIRE(std::any_of(pb.begin(), pb.end(), [](const Probleme& q) { return q.code == "dessous_fond"; }));
  double plusHaut = 0;
  for (const auto& k : p.composants) if (k.dessous) plusHaut = std::max(plusHaut, k.h);
  corrigerTout(p);
  CHECK(p.boitier.entretoise == Approx(std::ceil((plusHaut + 0.5) * 2) / 2));
  pb = verifier(p, deriver(p));
  CHECK_FALSE(std::any_of(pb.begin(), pb.end(), [](const Probleme& q) { return q.code == "dessous_fond"; }));
  // La face est enregistrée avec le projet.
  const Projet q = normaliser(versJson(p));
  CHECK(std::count_if(q.composants.begin(), q.composants.end(), [](const Composant& k) { return k.dessous; }) == nDessous);
  // Les composants de la face arrière pendent sous la carte en 3D.
  const Derive d = deriver(p);
  for (const auto& g : d.comps)
    if (g.dessous) { CHECK(encombrement(construireComposant(d, g)).zmax == Approx(d.zpb).margin(1e-6)); break; }
}

TEST_CASE("Un projet s'exporte vers KiCad et se relit à l'identique") {
  const Projet p = exemple();
  const Derive d = deriver(p);
  const std::string texte = kicadDepuisProjet(p, d);
  const CarteKicad k = lireKicad(texte);
  REQUIRE(k.contours.size() == 1);
  CHECK(k.contours.front().ferme);
  CHECK(k.xmax - k.xmin == Approx(60).margin(1e-6));
  CHECK(k.ymax - k.ymin == Approx(40).margin(1e-6));
  CHECK(std::count_if(k.empreintes.begin(), k.empreintes.end(), [](const EmpreinteKicad& f) { return f.fixation; }) == 4);
  // Réimportée, la carte garde son contour arrondi et ses quatre trous.
  RapportImport r;
  const Projet q = projetDepuisKicad(k, "", &r);
  CHECK(r.trous == 4);
  CHECK(std::abs(q.carte.contours.front().aire()) == Approx(std::abs(lireKicad(texte).contours.front().aire())));
}

TEST_CASE("L'aller-retour réécrit le contour et les trous dans la carte KiCad d'origine") {
  const std::string origine = kicadDepuisProjet(exemple(), deriver(exemple()));
  Projet p = projetDepuisKicad(lireKicad(origine), "");
  REQUIRE(p.kicad.has_value());
  p.trous[0].x += 3;  // T1 déplacé dans Prométhée
  const std::string sortie = kicadMisAJour(origine, p, deriver(p));
  const CarteKicad avant = lireKicad(origine), apres = lireKicad(sortie);
  CHECK(apres.contours.front().elements.size() == avant.contours.front().elements.size());
  CHECK(apres.contours.front().aire() == Approx(avant.contours.front().aire()));
  auto pos = [](const CarteKicad& c, const char* ref) { for (const auto& f : c.empreintes) if (f.ref == ref) return f.position; return Point{0, 0}; };
  CHECK(pos(apres, "T1").x == Approx(pos(avant, "T1").x + 3));
  CHECK(pos(apres, "T2").x == Approx(pos(avant, "T2").x));
  // Le reste du fichier est conservé : même nombre d'empreintes.
  CHECK(apres.empreintes.size() == avant.empreintes.size());
}

TEST_CASE("Un projet d'une version plus récente est refusé clairement") {
  Json j = versJson(exemple());
  j["version"] = VERSION_FORMAT + 1;
  CHECK_THROWS_WITH(normaliser(j), Catch::Matchers::ContainsSubstring("version plus récente"));
  j["version"] = 1;  // format du prototype web : lu tel quel
  CHECK_NOTHROW(normaliser(j));
}

TEST_CASE("EL-36 import électrique d'une carte KiCad : empreintes, cuivre, réseaux, schéma de liaison", "[kicad][EL-36]") {
  for (const char* fichier : {"StickHub.kicad_pcb", "ecc83-pp.kicad_pcb"}) {
    DYNAMIC_SECTION(fichier) {
      prom::ecad::RapportImportKicad r;
      const auto c = prom::ecad::conceptionDepuisKicad(lireFichier(cheminFichier(donnee(fichier))), fichier, &r);
      const auto k = prom::ecad::connectivite(c);
      const auto z = prom::ecad::remplirZones(c, k);
      const auto e = prom::ecad::etatRoutage(c, k, z);
      const auto drc = prom::ecad::verifierDRC(c, k, z);
      const auto erc = prom::ecad::verifierERC(c, k);
      std::map<std::string, int> codes;
      for (const auto& v : drc) ++codes[v.code];
      std::string liste;
      for (const auto& [code, n] : codes) liste += code + "×" + std::to_string(n) + " ";
      std::string alertes;
      for (const auto& a : r.alertes) alertes += a + " ; ";
      INFO(fichier << " : " << r.empreintes << " empreintes, " << r.pastilles << " pastilles, " << r.pistes << " pistes, " << r.vias << " vias, " << r.zones << " zones, " << r.reseaux
                   << " réseaux ; chevelu " << e.chevelu.size() << " ; réseaux complets " << e.reseauxComplets << "/" << e.reseauxTotal << " ; DRC " << liste << " ; alertes " << alertes);
      CHECK(r.empreintes > 10);
      CHECK(c.circuit.contour.libre.size() >= 4);
      // Les réseaux de KiCad sont retrouvés : tout est routé, aucun court-circuit, aucune erreur de règle
      CHECK(e.chevelu.empty());
      CHECK(e.reseauxComplets == e.reseauxTotal);
      CHECK(codes["DRC-21"] == 0);
      int erreurs = 0;
      for (const auto& v : drc) erreurs += v.gravite == prom::ecad::Gravite::Erreur;
      CHECK(erreurs == 0);
      // Schéma de liaison : un symbole par composant électrique, ERC sans erreur
      int erreursErc = 0;
      for (const auto& v : erc) erreursErc += v.gravite == prom::ecad::Gravite::Erreur;
      CHECK(erreursErc == 0);
      std::size_t symboles = 0;
      for (const auto& f : c.feuilles) symboles += f.symboles.size();
      CHECK(symboles >= c.composants.size() - 6);
      // Le fichier Prométhée garde tout
      const auto relue = prom::ecad::conceptionDepuisJson(prom::ecad::versJson(c));
      CHECK(relue.circuit.pistes.size() == c.circuit.pistes.size());
      CHECK(relue.empreintes.size() == c.empreintes.size());
    }
  }
}

TEST_CASE("EL-36 règles et classes reprises du projet KiCad (.kicad_pro)", "[kicad][EL-36]") {
  const std::string pro = R"({"board": {"design_settings": {"rules": {"min_track_width": 0.2, "min_copper_edge_clearance": 0.5, "min_through_hole_diameter": 0.35}}},
    "net_settings": {"classes": [{"name": "Default", "clearance": 0.3, "track_width": 0.4, "via_diameter": 0.9, "via_drill": 0.45},
                                 {"name": "Haute tension", "clearance": 1.5, "track_width": 1.0, "via_diameter": 1.2, "via_drill": 0.6}],
                     "netclass_patterns": [{"netclass": "Haute tension", "pattern": "HT*"}]}})";
  prom::ecad::RapportImportKicad r;
  const auto c = prom::ecad::conceptionDepuisKicad(lireFichier(cheminFichier(donnee("ecc83-pp.kicad_pcb"))), "ECC83", &r, pro);
  const auto& R = c.circuit.regles;
  CHECK(R.isolement == prom::ecad::nm(0.3));
  CHECK(R.largeur == prom::ecad::nm(0.4));
  CHECK(R.largeurMin == prom::ecad::nm(0.2));
  CHECK(R.bord == prom::ecad::nm(0.5));
  CHECK(R.viaDiametre == prom::ecad::nm(0.9));
  REQUIRE(R.classes.size() == 1);
  CHECK(R.classes[0].isolement == prom::ecad::nm(1.5));
  REQUIRE(prom::ecad::classeDe(R, "HT_ANODE") != nullptr);
  CHECK(prom::ecad::classeDe(R, "HT_ANODE")->nom == "Haute tension");
  CHECK(prom::ecad::classeDe(R, "GND") == nullptr);
}

TEST_CASE("Fonctions KiCad : empreinte .kicad_mod et symboles .kicad_sym importés", "[kicad][bibliotheques]") {
  const std::string mod = R"((footprint "R_0603_1608Metric" (version 20240108) (generator "pcbnew") (layer "F.Cu")
  (descr "Resistor SMD 0603")
  (property "Reference" "REF**" (at 0 -1.43 0) (layer "F.SilkS"))
  (fp_line (start -0.237258 -0.5225) (end 0.237258 -0.5225) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
  (fp_line (start -0.237258 0.5225) (end 0.237258 0.5225) (stroke (width 0.12) (type solid)) (layer "F.SilkS"))
  (fp_rect (start -1.48 -0.73) (end 1.48 0.73) (stroke (width 0.05) (type solid)) (layer "F.CrtYd"))
  (pad "1" smd roundrect (at -0.825 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
  (pad "2" smd roundrect (at 0.825 0) (size 0.8 0.95) (layers "F.Cu" "F.Paste" "F.Mask") (roundrect_rratio 0.25))
  (pad "" smd rect (at 0 0) (size 0.2 0.2) (layers "F.Paste"))
))";
  std::vector<std::string> alertes;
  const auto d = prom::ecad::empreinteDepuisKicadMod(mod, &alertes);
  REQUIRE(d);
  CHECK(d->id == "kicad:R_0603_1608Metric");
  // 0.34 (10.1) : l'ouverture de pâte seule est reprise, pastille sans numéro sur la seule couche de pâte
  REQUIRE(d->pastilles.size() == 3);
  const prom::ecad::Pastille* p1 = nullptr;
  const prom::ecad::Pastille* p2 = nullptr;
  const prom::ecad::Pastille* pate = nullptr;
  for (const auto& p : d->pastilles) (p.numero == "1" ? p1 : p.numero == "2" ? p2 : pate) = &p;
  REQUIRE(p1);
  REQUIRE(p2);
  REQUIRE(pate);
  CHECK(pate->couches == prom::ecad::bit(prom::ecad::Couche::PateDessus));
  CHECK(p1->position == prom::ecad::ptMm(-0.825, 0));
  CHECK(p2->largeur == prom::ecad::nm(0.8));
  CHECK(d->traits.size() >= 2);
  CHECK(prom::ecad::mm(d->encombrement.largeur()) == Approx(2.96).margin(1e-6));
  CHECK_FALSE(prom::ecad::empreinteDepuisKicadMod("(kicad_pcb (version 1))"));
  const std::string sym = R"((kicad_symbol_lib (version 20231120) (generator "kicad_symbol_editor")
  (symbol "LM358" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)
    (property "Reference" "U" (at 0 5.08 0)) (property "Value" "LM358" (at 0 -5.08 0))
    (property "Footprint" "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm" (at 0 0 0)) (property "Datasheet" "http://www.ti.com/lit/ds/symlink/lm2904-n.pdf" (at 0 0 0))
    (symbol "LM358_1_1" (pin output line (at 7.62 0 180) (length 2.54) (name "~" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))
      (pin input line (at -7.62 -2.54 0) (length 2.54) (name "-" (effects (font (size 1.27 1.27)))) (number "2" (effects (font (size 1.27 1.27)))))
      (pin input line (at -7.62 2.54 0) (length 2.54) (name "+" (effects (font (size 1.27 1.27)))) (number "3" (effects (font (size 1.27 1.27))))))
    (symbol "LM358_2_1" (pin input line (at -7.62 2.54 0) (length 2.54) (name "+" (effects (font (size 1.27 1.27)))) (number "5" (effects (font (size 1.27 1.27)))))
      (pin input line (at -7.62 -2.54 0) (length 2.54) (name "-" (effects (font (size 1.27 1.27)))) (number "6" (effects (font (size 1.27 1.27)))))
      (pin output line (at 7.62 0 180) (length 2.54) (name "~" (effects (font (size 1.27 1.27)))) (number "7" (effects (font (size 1.27 1.27))))))
    (symbol "LM358_3_1" (pin power_in line (at -2.54 -7.62 90) (length 3.81) (name "V-" (effects (font (size 1.27 1.27)))) (number "4" (effects (font (size 1.27 1.27)))))
      (pin power_in line (at -2.54 7.62 270) (length 3.81) (name "V+" (effects (font (size 1.27 1.27)))) (number "8" (effects (font (size 1.27 1.27)))))))
  (symbol "LM2904" (extends "LM358") (property "Reference" "U" (at 0 5.08 0)) (property "Value" "LM2904" (at 0 0 0)))
  (symbol "GND" (power) (pin_names (offset 0)) (property "Reference" "#PWR" (at 0 0 0))
    (symbol "GND_0_1" (pin power_in line (at 0 0 270) (length 0) hide (name "GND" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))))
))";
  alertes.clear();
  const auto syms = prom::ecad::symbolesDepuisKicadSym(sym, &alertes);
  REQUIRE(syms.size() == 2);  // LM358, LM2904 (dérivé) ; GND laissé
  CHECK(syms[0].id == "kicad:LM358");
  CHECK(syms[0].broches.size() == 8);
  CHECK(syms[0].unites == 3);  // deux amplis et l'alimentation, posés à part (U1A, U1B, U1C)
  for (const auto& b : syms[0].broches) {
    if (b.numero == "1" || b.numero == "2" || b.numero == "3") CHECK(b.unite == 1);
    if (b.numero == "5" || b.numero == "6" || b.numero == "7") CHECK(b.unite == 2);
    if (b.numero == "4" || b.numero == "8") CHECK(b.unite == 3);
  }
  CHECK(syms[1].unites == 3);
  CHECK(syms[0].prefixe == "U");
  CHECK(syms[0].empreinte == "SOIC-8");  // trouvée dans le catalogue
  CHECK(syms[1].broches.size() == 8);   // broches du parent
  const prom::ecad::Broche* v = nullptr;
  for (const auto& b : syms[0].broches) if (b.numero == "8") v = &b;
  REQUIRE(v);
  CHECK(v->type == prom::ecad::TypeBroche::AlimEntree);
  CHECK(v->direction == 1);  // broche en haut, tournée vers le bas (le corps)
  CHECK(alertes.size() == 2);
}

TEST_CASE("Fonctions KiCad : empreinte créée par tableau de pastilles", "[electronique][bibliotheques]") {
  using namespace prom::ecad;
  SpecEmpreinte dip;
  dip.nom = "DIP-8 maison";
  dip.rangees = 2;
  dip.colonnes = 4;
  dip.pasY = 7.62;
  dip.numerotation = SpecEmpreinte::Numerotation::Tour;
  const DefEmpreinte d = genererEmpreinteSpec(dip);
  CHECK(d.id == "perso:DIP-8_maison");
  REQUIRE(d.pastilles.size() == 8);
  // Tour : 1 en bas à gauche, 4 en bas à droite, 5 en haut à droite, 8 en haut à gauche
  CHECK(d.pastilles[0].position == ptMm(-3.81, 3.81));
  CHECK(d.pastilles[3].position == ptMm(3.81, 3.81));
  CHECK(d.pastilles[4].position == ptMm(3.81, -3.81));
  CHECK(d.pastilles[7].position == ptMm(-3.81, -3.81));
  CHECK(d.pastilles[0].forme == FormePastille::Rect);  // broche 1 carrée
  CHECK(d.pastilles[1].forme == FormePastille::Rond);
  CHECK(d.pastilles[0].percage == nm(1.0));
  CHECK(d.encombrement.largeur() > nm(7.62 + 1.7));
  SpecEmpreinte barrette;
  barrette.nom = "Barrette 2x5";
  barrette.rangees = 2;
  barrette.colonnes = 5;
  barrette.numerotation = SpecEmpreinte::Numerotation::ParColonne;
  const DefEmpreinte b = genererEmpreinteSpec(barrette);
  CHECK(b.pastilles[1].position == ptMm(-5.08, 1.27));  // 2 sous la 1
  CHECK(b.pastilles[2].position == ptMm(-2.54, -1.27));
  // L'empreinte se pose et se vérifie comme les autres
  ConceptionElec c = exempleElectronique();
  c.empreintes[d.id] = d;
  c.composants.front().empreinte = d.id;
  CHECK(c.empreinte(d.id) != nullptr);
  CHECK(empreinteDepuisJson(empreinteVersJson(d)).pastilles.size() == 8);
}

TEST_CASE("Export d'une carte vers KiCad puis relecture : même cuivre, mêmes réseaux, DRC propre", "[kicad][export-kicad]") {
  using namespace prom::ecad;
  ConceptionElec c = exempleElectronique();
  c.circuit.textes.push_back({nouvelUuid(), "REV A", Couche::SerigraphieDessus, ptMm(5, 5), 1'000'000, 0});
  c.circuit.empreintes.front().verrou = true;
  const Connectivite k = connectivite(c);
  const std::string texte = kicadDepuisConception(c, k);
  CHECK(texte.rfind("(kicad_pcb (version 20221018)", 0) == 0);
  RapportImportKicad r;
  const ConceptionElec relue = conceptionDepuisKicad(texte, "relue", &r);
  CHECK(relue.circuit.empreintes.size() == c.circuit.empreintes.size());
  CHECK(relue.circuit.pistes.size() == c.circuit.pistes.size());
  CHECK(relue.circuit.vias.size() == c.circuit.vias.size());
  CHECK(relue.circuit.zones.size() == c.circuit.zones.size());
  CHECK(relue.circuit.textes.size() == c.circuit.textes.size());
  CHECK(relue.circuit.empreintes.front().verrou);
  // Pastilles au même endroit, avec la même forme
  const auto avant = pastilles(c, k);
  const Connectivite k2 = connectivite(relue);
  const auto apres = pastilles(relue, k2);
  REQUIRE(avant.size() == apres.size());
  int memes = 0;
  for (const auto& a : avant)
    for (const auto& b : apres)
      if (distance(a.pastille.position, b.pastille.position) < 1000 && a.reseau == b.reseau && std::abs(aireMm2(a.forme) - aireMm2(b.forme)) < 1e-3) { ++memes; break; }
  CHECK(memes == static_cast<int>(avant.size()));
  const auto z = remplirZones(relue, k2);
  CHECK(etatRoutage(relue, k2, z).chevelu.empty());
  int erreurs = 0;
  std::string codes;
  for (const auto& v : verifierDRC(relue, k2, z))
    if (v.gravite == Gravite::Erreur) { ++erreurs; codes += v.code + " " + v.message + " ; "; }
  INFO(codes);
  CHECK(erreurs == 0);
}
