// Prométhée : essais de la 0.34, lot L10 (suite) — exports 3D de la carte (10.10, 10.24) et échange IDX avec la
// mécanique (10.10, 10.24). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "promethee/archive.hpp"
#include "promethee/carte3d.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/electronique.hpp"
#include "promethee/exports3d.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/idx.hpp"

#include <TopExp_Explorer.hxx>

using namespace prom::ecad;

namespace {
std::size_t compter(const std::string& s, const std::string& motif) {
  std::size_t n = 0;
  for (std::size_t p = 0; (p = s.find(motif, p)) != std::string::npos; p += motif.size()) ++n;
  return n;
}
}  // namespace

TEST_CASE("10.10 · carte équipée exportée en GLB, VRML, STL, COLLADA, AMF, OpenSCAD, XAO et STEP compressé, relue par un lecteur indépendant", "[034][L10][10.10][10.24]") {
  const ConceptionElec c = exempleElectronique();
  const auto pieces = prom::piecesCarteCouleurs(c);
  REQUIRE(pieces.size() > 3);
  // GLB : structure contrôlée, un nœud par pièce, la carte à ses dimensions (glTF en mètres)
  const std::string glb = prom::texte3D(pieces, prom::Format3D::GLB);
  const prom::ResumeGlb g = prom::lireGlb(glb);
  INFO(g.erreur);
  REQUIRE(g.ok);
  CHECK(g.noeuds == static_cast<int>(pieces.size()));
  CHECK(g.maillages == static_cast<int>(pieces.size()));
  CHECK(g.materiaux >= 3);  // vernis, pastilles, corps
  CHECK(g.triangles > 1000);
  const Boite b = [&] {
    Boite x;
    for (const Pt& p : c.circuit.contour.libre) x.etendre(p);
    if (x.vide()) x = Boite{c.circuit.contour.origine.x, c.circuit.contour.origine.y, c.circuit.contour.origine.x + c.circuit.contour.largeur, c.circuit.contour.origine.y + c.circuit.contour.hauteur};
    return x;
  }();
  CHECK(std::abs((g.max[0] - g.min[0]) * 1000 - mm(b.largeur())) < 1.0);
  CHECK(std::abs((g.max[2] - g.min[2]) * 1000 - mm(b.hauteur())) < 1.0);
  CHECK(std::find(g.nomsNoeuds.begin(), g.nomsNoeuds.end(), pieces.front().nom) != g.nomsNoeuds.end());
  // Un GLB abîmé est refusé
  std::string abime = glb;
  abime.resize(abime.size() - 8);
  CHECK_FALSE(prom::lireGlb(abime).ok);
  // VRML 2.0, STL binaire, COLLADA, AMF, OpenSCAD
  const std::string wrl = prom::texte3D(pieces, prom::Format3D::VRML);
  CHECK(wrl.rfind("#VRML V2.0 utf8", 0) == 0);
  CHECK(compter(wrl, "Transform {") == pieces.size());
  const std::string stl = prom::texte3D(pieces, prom::Format3D::STL);
  uint32_t n = 0;
  std::memcpy(&n, stl.data() + 80, 4);
  CHECK(stl.size() == 84 + 50 * static_cast<std::size_t>(n));
  CHECK(n == g.triangles);
  const std::string dae = prom::texte3D(pieces, prom::Format3D::COLLADA);
  CHECK(compter(dae, "<node ") == pieces.size());
  CHECK(compter(dae, "<COLLADA") == 1);
  const std::string amf = prom::texte3D(pieces, prom::Format3D::AMF);
  CHECK(compter(amf, "<object ") == pieces.size());
  const std::string scad = prom::texte3D(pieces, prom::Format3D::OpenSCAD);
  CHECK(compter(scad, "polyhedron(") >= pieces.size());
  // XAO : la géométrie exacte se relit, le groupe des pastilles n'est pas vide
  const std::string xao = prom::texte3D(pieces, prom::Format3D::XAO);
  const auto a = xao.find("<![CDATA["), z = xao.find("]]>");
  REQUIRE(a != std::string::npos);
  const TopoDS_Shape forme = prom::formeDepuisBrep(xao.substr(a + 9, z - a - 9));
  REQUIRE(!forme.IsNull());
  int faces = 0;
  for (TopExp_Explorer ex(forme, TopAbs_FACE); ex.More(); ex.Next()) ++faces;
  CHECK(xao.find("<faces count=\"" + std::to_string(faces) + "\"") != std::string::npos);
  const auto gr = xao.find("<group name=\"pastilles\" dimension=\"face\" count=\"");
  REQUIRE(gr != std::string::npos);
  CHECK(std::stoi(xao.substr(gr + 48)) > 10);
  // STEP compressé : gzip qui redonne un STEP
  const std::string stpz = prom::texte3D(pieces, prom::Format3D::STPZ);
  REQUIRE(stpz.size() > 18);
  CHECK(static_cast<unsigned char>(stpz[0]) == 0x1F);
  CHECK(static_cast<unsigned char>(stpz[1]) == 0x8B);
  const std::string step = prom::inflerBrut(stpz.substr(10, stpz.size() - 18));
  CHECK(step.rfind("ISO-10303-21;", 0) == 0);
  uint32_t crc = 0;
  std::memcpy(&crc, stpz.data() + stpz.size() - 8, 4);
  CHECK(crc == prom::crc32(step));
}

TEST_CASE("10.24 · proposition IDX venue de la mécanique : connecteur déplacé de 2 mm et zone interdite ajoutée, acceptée, carte mise à jour, échange inscrit", "[034][L10][10.24]") {
  ConceptionElec c = exempleElectronique();
  // Le connecteur de la carte (sinon le premier composant posé)
  std::string ref;
  for (const auto& em : c.circuit.empreintes)
    if (const ComposantElec* x = c.composant(em.composant); x && (ref.empty() || x->reference.rfind("J", 0) == 0)) ref = x->reference;
  REQUIRE(!ref.empty());
  const ComposantElec* j = c.parReference(ref);
  Pt avant;
  for (const auto& em : c.circuit.empreintes)
    if (em.composant == j->uuid) avant = em.position;
  // Ligne de base, relue : aucune différence avec la carte
  const std::string base = idxLigneDeBase(c);
  CHECK(base.find("BOARD_OUTLINE") != std::string::npos);
  CHECK(lirePropositionIdx(base, c).changements.empty());
  // La mécanique déplace le connecteur de 2 mm en x et ajoute une zone interdite de placement de 10 × 6 mm
  std::string prop = base;
  const auto nom = prop.find("<foundation:ObjectName>" + ref + "</foundation:ObjectName></pdm:InstanceName>");
  REQUIRE(nom != std::string::npos);
  const auto tx = prop.find("<pdm:tx><property:Value>", nom) + 24;
  const auto fin = prop.find('<', tx);
  const double x = std::stod(prop.substr(tx, fin - tx));
  prop.replace(tx, fin - tx, std::to_string(x + 2));
  const std::string zone =
      R"(    <foundation:CartesianPoint id="M1" xsi:type="d2:EDMDCartesianPoint"><d2:X><property:Value>5</property:Value></d2:X><d2:Y><property:Value>-5</property:Value></d2:Y></foundation:CartesianPoint>
    <foundation:CartesianPoint id="M2" xsi:type="d2:EDMDCartesianPoint"><d2:X><property:Value>15</property:Value></d2:X><d2:Y><property:Value>-5</property:Value></d2:Y></foundation:CartesianPoint>
    <foundation:CartesianPoint id="M3" xsi:type="d2:EDMDCartesianPoint"><d2:X><property:Value>15</property:Value></d2:X><d2:Y><property:Value>-11</property:Value></d2:Y></foundation:CartesianPoint>
    <foundation:CartesianPoint id="M4" xsi:type="d2:EDMDCartesianPoint"><d2:X><property:Value>5</property:Value></d2:X><d2:Y><property:Value>-11</property:Value></d2:Y></foundation:CartesianPoint>
    <foundation:PolyLine id="MP" xsi:type="d2:EDMDPolyLine"><d2:Point>M1</d2:Point><d2:Point>M2</d2:Point><d2:Point>M3</d2:Point><d2:Point>M4</d2:Point><d2:Point>M1</d2:Point></foundation:PolyLine>
    <foundation:CurveSet2d id="MC" xsi:type="d2:EDMDCurveSet2d"><d2:LowerBound><property:Value>0</property:Value></d2:LowerBound><d2:UpperBound><property:Value>100</property:Value></d2:UpperBound><d2:DetailedGeometricModelElement>MP</d2:DetailedGeometricModelElement></foundation:CurveSet2d>
    <foundation:ShapeElement id="MS" xsi:type="pdm:EDMDShapeElement"><pdm:DefiningShape>MC</pdm:DefiningShape></foundation:ShapeElement>
    <foundation:Item id="MZ" xsi:type="pdm:EDMDItem" geometryType="KEEPOUT_AREA_COMPONENT"><foundation:Name>Passage du câble</foundation:Name><pdm:ItemType>single</pdm:ItemType><pdm:Shape>MS</pdm:Shape></foundation:Item>
)";
  prop.insert(prop.find("    <foundation:Item id=\"ASSEMBLAGE\""), zone);
  prop.insert(prop.find("    </foundation:Item>\n  </foundation:Body>"), "      <pdm:ItemInstance id=\"MI\"><pdm:Item>MZ</pdm:Item><pdm:InstanceName><foundation:ObjectName>Passage du câble</foundation:ObjectName></pdm:InstanceName></pdm:ItemInstance>\n");
  prop.replace(prop.find("EDMDProcessInstructionSendInformation"), 37, "EDMDProcessInstructionSendChanges");
  const std::string createur = "<foundation:CreatorSystem>Prométhée";
  prop.replace(prop.find(createur), createur.size(), "<foundation:CreatorSystem>SolidWorks");
  // Trois écritures du même fichier : préfixes d'origine, préfixes changés, espace de noms par défaut sans préfixe
  auto sansPrefixes = [](std::string s) {
    for (const std::string p : {"foundation:", "pdm:", "d2:", "property:", "computational:"})
      for (std::size_t k; (k = s.find("<" + p)) != std::string::npos || (k = s.find("</" + p)) != std::string::npos;) s.erase(s.find(p, k), p.size());
    return s;
  };
  auto autresPrefixes = [](std::string s) {
    for (std::size_t k; (k = s.find("foundation:")) != std::string::npos;) s.replace(k, 11, "fnd:");
    return s;
  };
  for (const std::string& variante : {prop, autresPrefixes(prop), sansPrefixes(prop)}) {
    const PropositionIdx p = lirePropositionIdx(variante, c);
    std::string t;
    for (const auto& ch : p.changements) t += ch.description + " | ";
    for (const auto& al : p.alertes) t += "alerte : " + al + " | ";
    INFO(t);
    REQUIRE(p.changements.size() == 2);
    CHECK(p.logiciel == "SolidWorks");
  }
  const PropositionIdx p = lirePropositionIdx(prop, c);
  const auto dep = std::find_if(p.changements.begin(), p.changements.end(), [](const ChangementIdx& x) { return x.genre == ChangementIdx::Genre::Deplacement; });
  REQUIRE(dep != p.changements.end());
  CHECK(dep->description.find("2,00 mm") != std::string::npos);
  // Acceptée : la carte suit, l'échange est à l'historique
  ConceptionElec apres = c;
  CHECK(appliquerPropositionIdx(apres, p, {true, true}, "proposition.idx") == 2);
  for (const auto& em : apres.circuit.empreintes)
    if (em.composant == j->uuid) {
      CHECK(std::abs(mm(em.position.x - avant.x) - 2) < 1e-3);
      CHECK(em.position.y == avant.y);
    }
  REQUIRE(apres.circuit.zonesRegles.size() == c.circuit.zonesRegles.size() + 1);
  const ZoneRegle& z = apres.circuit.zonesRegles.back();
  CHECK(z.nom == "Passage du câble");
  CHECK(z.interditEmpreintes);
  CHECK(std::abs(aireMm2(z.contour) - 60) < 1e-6);
  const auto h = historiqueIdx(apres);
  REQUIRE(h.size() == 1);
  CHECK(h.front().acceptes == 2);
  CHECK(h.front().sens == "reçu");
  CHECK(h.front().logiciel == "SolidWorks");
  // La nouvelle ligne de base relue n'annonce plus rien ; la réponse dit les deux acceptations
  CHECK(lirePropositionIdx(idxLigneDeBase(apres), apres).changements.empty());
  CHECK(compter(idxReponse(p, {true, true}), "<computational:Accept>true") == 2);
  // Refusée : rien ne change, l'échange est quand même inscrit
  ConceptionElec refus = c;
  CHECK(appliquerPropositionIdx(refus, p, {false, false}) == 0);
  CHECK(refus.circuit.zonesRegles.size() == c.circuit.zonesRegles.size());
  CHECK(historiqueIdx(refus).front().refuses == 2);
  // Cuivre en décalques ou en volumes dans la ligne de base
  OptionsIdx o;
  o.cuivre = OptionsIdx::Cuivre::Volumes;
  CHECK(compter(idxLigneDeBase(c, o), "OTHER_OUTLINE") > 3);
}

#include "promethee/csv.hpp"
#include "promethee/echanges_electronique.hpp"
#include "promethee/netlists_tierces.hpp"

namespace {
using Partition = std::set<std::set<std::string>>;
Partition partitionDe(const NetlistLue& n) {
  Partition p;
  for (const auto& [nom, noeuds] : n.reseaux) {
    std::set<std::string> e;
    for (const auto& [r, b] : noeuds) e.insert(r + "." + b);
    if (e.size() > 1) p.insert(e);
  }
  return p;
}
}  // namespace

TEST_CASE("10.20 · un seul écrivain CSV réglé une fois : séparateur, guillemets, décimale, marque UTF-8", "[034][L10][10.20]") {
  const prom::ReglagesCsv avant = prom::reglagesCsv();
  CHECK(avant == prom::ReglagesCsv::tableur());
  prom::EcrivainCsv w;
  w.champ("a;b").nombre(4.75, 3).fixe(12.5, 2).entier(3).champ("dit \"x\"").fin();
  CHECK(w.texte() == "\xEF\xBB\xBF\"a;b\";4,75;12,50;3;\"dit \"\"x\"\"\"\r\n");
  // Réglé autrement pour tout le poste : les nomenclatures suivent
  prom::reglerCsv(prom::reglagesCsvDepuisTexte(", . sansbom lf"));
  CHECK(prom::reglagesCsv().separateur == ',');
  CHECK(prom::texteReglagesCsv(prom::reglagesCsv()) == ", . sansbom lf");
  const std::string n = csvNomenclature(exempleElectronique());
  CHECK(n.rfind("Quantité,Références", 0) == 0);
  prom::EcrivainCsv w2;
  w2.champ("R1, R2").nombre(0.5).fin();
  CHECK(w2.texte() == "\"R1, R2\",0.5\n");  // le séparateur dans un champ : protégé
  prom::reglerCsv(avant);
  CHECK(csvNomenclature(exempleElectronique()).rfind("\xEF\xBB\xBFQuantité;Références", 0) == 0);
  // Les formats imposés des fabricants ne suivent pas le poste
  prom::EcrivainCsv f(prom::ReglagesCsv::international());
  f.nombre(1.5).fin();
  CHECK(f.texte() == "1.5\n");
}

TEST_CASE("10.20 · netlists OrCAD PCB II, PADS, Allegro, CADSTAR, EDIF, gEDA et TinyCAD écrites puis relues : mêmes réseaux", "[034][L10][10.20]") {
  const NetlistLue n = netlistDe(exempleElectronique());
  REQUIRE(!n.reseaux.empty());
  for (const auto& info : formatsNetlist()) {
    if (info.format == FormatNetlist::Spice) continue;  // les broches y sont rangées par position : essai à part
    const std::string t = ecrireNetlist(n, info.format, "Essai");
    INFO(info.nom);
    const auto f = reconnaitreNetlist(t);
    REQUIRE(f.has_value());
    CHECK(*f == info.format);
    const NetlistLue relue = lireNetlist(t);
    CHECK(relue.erreurs.empty());
    CHECK(partitionDe(relue) == partitionDe(n));
    if (info.format != FormatNetlist::Geda) {
      CHECK(relue.composants.size() == n.composants.size());
      for (const auto& x : n.composants)
        for (const auto& y : relue.composants)
          if (x.reference == y.reference && !x.empreinte.empty() && info.format != FormatNetlist::Cadstar) CHECK(y.empreinte == x.empreinte);
    }
  }
  // Exporteur maison par gabarit
  const std::string g = netlistGabarit(n, "# {titre}\n@composant COMP {ref} {valeur}\n@reseau NET {nom} ({nombre})\n@broche   {ref}:{broche}\n# fin", "Maison");
  CHECK(g.rfind("# Maison\n", 0) == 0);
  CHECK(g.find("COMP R1 ") != std::string::npos);
  CHECK(g.find("NET GND (") != std::string::npos);
  CHECK(g.substr(g.size() - 6) == "# fin\n");
}

TEST_CASE("10.20 · netlist d'un schéma LTspice : carte mise à jour, rapport des changements en colonnes et décimales justes", "[034][L10][10.20]") {
  const std::string v1 = "* C:\\users\\essai\\filtre.asc\nV1 IN 0 SINE(0 1 1k)\nR1 IN OUT 10k\nC1 OUT 0 100n\nQ1 N001 OUT 0 2N3904\n.tran 5m\n.backanno\n.end\n";
  REQUIRE(reconnaitreNetlist(v1) == FormatNetlist::Spice);
  const NetlistLue n1 = lireNetlist(v1);
  CHECK(n1.composants.size() == 4);
  CHECK(n1.reseaux.at("GND").size() == 3);
  ConceptionElec c = conceptionDepuisNetlist(n1, "Filtre");
  // Nouvelle version du schéma : R1 à 22k, C2 de 4,7 µF ajouté entre OUT et la masse
  const std::string v2 = "* C:\\users\\essai\\filtre.asc\nV1 IN 0 SINE(0 1 1k)\nR1 IN OUT 22k\nC1 OUT 0 100n\nC2 OUT 0 4.7µ\nQ1 N001 OUT 0 2N3904\n.tran 5m\n.end\n";
  const BilanNetlist b = mettreAJourDepuisNetlist(c, lireNetlist(v2));
  CHECK(b.ajoutes == 1);
  CHECK(b.valeurs == 1);
  CHECK(c.parReference("R1")->valeur == "22k");
  CHECK(c.parReference("C2"));
  // Rapport : cinq colonnes sur chaque ligne, relu par le lecteur de tableau ; valeurs SI à la virgule décimale
  const std::string csv = rapportNetlistCsv(b);
  CHECK(csv.rfind("\xEF\xBB\xBF", 0) == 0);
  const auto t = lireTableau(csv);
  REQUIRE(t.size() >= 3);
  for (const auto& l : t) CHECK(l.size() == 5);
  bool r1 = false, c2 = false;
  for (const auto& l : t) {
    if (l[0] == "R1" && l[1] == "valeur") r1 = l[3] == "22k" && l[4] == "22000";
    if (l[0] == "C2" && l[1] == "ajouté") c2 = l[4] == "0,0000047";
  }
  for (const auto& l : t) {
    std::string x;
    for (const auto& y : l) x += y + " | ";
    UNSCOPED_INFO(x);
  }
  CHECK(r1);
  CHECK(c2);
  // Écrite en SPICE puis relue : mêmes réseaux
  const NetlistLue n3 = lireNetlist(ecrireNetlist(lireNetlist(v2), FormatNetlist::Spice));
  CHECK(partitionDe(n3) == partitionDe(lireNetlist(v2)));
}

#include "promethee/dessins_carte.hpp"
#include "promethee/dxf_dessin.hpp"
#include "promethee/lecture_svg.hpp"

namespace {
// Lecteur Gerber minimal écrit ici, sans rien partager avec l'écrivain : format %FSLAX…Y…*%, unité MO, coordonnées
// des régions (G36…G37) seules, pour mesurer l'encombrement d'un logo
struct BoiteGerber {
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  int regions = 0;
};
BoiteGerber regionsGerber(const std::string& g) {
  BoiteGerber b;
  int decimales = 6;
  double unite = 1;
  if (const auto p = g.find("%FSLAX"); p != std::string::npos) decimales = g[p + 7] - '0';
  if (g.find("%MOIN*%") != std::string::npos) unite = 25.4;
  bool dansRegion = false;
  long long x = 0, y = 0;
  std::size_t i = 0;
  while (i < g.size()) {
    const auto f = g.find('*', i);
    if (f == std::string::npos) break;
    std::string cmd = g.substr(i, f - i);
    i = f + 1;
    while (!cmd.empty() && (cmd.front() == '\n' || cmd.front() == '\r' || cmd.front() == '%')) cmd.erase(cmd.begin());
    if (cmd == "G36") {
      dansRegion = true;
      ++b.regions;
      continue;
    }
    if (cmd == "G37") {
      dansRegion = false;
      continue;
    }
    if (!dansRegion || cmd.empty() || (cmd[0] != 'X' && cmd[0] != 'Y')) continue;
    const auto px = cmd.find('X'), py = cmd.find('Y');
    if (px != std::string::npos) x = std::stoll(cmd.substr(px + 1));
    if (py != std::string::npos) y = std::stoll(cmd.substr(py + 1));
    const double X = x / std::pow(10.0, decimales) * unite, Y = y / std::pow(10.0, decimales) * unite;
    b.x0 = std::min(b.x0, X), b.y0 = std::min(b.y0, Y), b.x1 = std::max(b.x1, X), b.y1 = std::max(b.y1, Y);
  }
  return b;
}
}  // namespace

TEST_CASE("10.21 · logo SVG de 20 mm importé en sérigraphie : 20,00 mm dans le Gerber relu par un lecteur indépendant, trous gardés", "[034][L10][10.21]") {
  // Anneau (deux sous-chemins, le trou dans l'autre sens), barre, et un trait ouvert en deux morceaux disjoints de 0,005 mm
  const std::string svg = R"(<svg xmlns="http://www.w3.org/2000/svg" width="40mm" height="20mm" viewBox="0 0 40 20">
  <path d="M 10 2 A 8 8 0 1 1 9.99 2 Z M 10 6 A 4 4 0 1 0 10.01 6 Z" fill="#000"/>
  <rect x="22" y="4" width="16" height="12" fill="#000"/>
  <path d="M 22 18 L 30 18" stroke="#000" fill="none" stroke-width="0.3"/>
  <path d="M 30.005 18 L 38 18" stroke="#000" fill="none" stroke-width="0.3"/>
</svg>)";
  OptionsDessinCarte o;
  o.couche = Couche::SerigraphieDessus;
  o.largeurVoulue = 20;
  o.centre = ptMm(25, 20);
  RapportDessinCarte r;
  const auto d = dessinsDepuisSvg(svg, o, &r);
  INFO(r.alertes.size());
  CHECK(std::abs(r.largeur - 20) < 1e-9);
  CHECK(r.reunis == 1);   // les deux morceaux du trait réunis
  CHECK(r.traits == 1);
  CHECK(r.ilots == 2);    // l'anneau et la barre
  // Le trou de l'anneau reste ouvert : l'aire pleine est celle d'un anneau, pas d'un disque
  double aire = 0;
  for (const auto& x : d)
    if (x.plein) aire += aireMm2(x.points);
  const double k = 20.0 / 36;  // mise à l'échelle (le dessin fait 36 mm de large, trait compris)
  const double attendu = (std::numbers::pi * (64 - 16) + 16 * 12) * k * k;
  CHECK(std::abs(aire - attendu) / attendu < 0.02);
  // Posé sur une carte, écrit en Gerber, relu : 20,00 mm de large
  ConceptionElec c = exempleElectronique();
  c.circuit.textes.clear();
  c.circuit.dessins.insert(c.circuit.dessins.end(), d.begin(), d.end());
  const Connectivite kc = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, kc, remplirZones(c, kc));
  const std::string g = gerber(m, Couche::SerigraphieDessus, "0.34");
  const BoiteGerber b = regionsGerber(g);
  CHECK(b.regions == 2);
  // Les régions pleines vont de l'anneau à la barre : les 36 mm du dessin, mis à 20 mm
  CHECK(std::abs((b.x1 - b.x0) - 20.0) < 0.005);
  // Le DXF du même dessin donne le même encombrement
  OptionsDessinCarte od = o;
  const std::string dxf = "0\nSECTION\n2\nENTITIES\n0\nLINE\n8\nLOGO\n10\n0\n20\n0\n30\n0\n11\n20\n21\n0\n31\n0\n0\nLINE\n8\nLOGO\n10\n20\n20\n0\n30\n0\n11\n20\n21\n10\n31\n0\n"
                          "0\nLINE\n8\nLOGO\n10\n20\n20\n10\n30\n0\n11\n0.004\n21\n10\n31\n0\n0\nLINE\n8\nLOGO\n10\n0\n20\n10\n30\n0\n11\n0\n21\n0\n31\n0\n0\nENDSEC\n0\nEOF\n";
  RapportDessinCarte rd;
  const auto dd = dessinsDepuisDxf(dxf, od, &rd);
  REQUIRE(dd.size() == 1);
  CHECK(rd.reunis == 1);  // la seule discontinuité (0,004 mm) réparée
  CHECK(std::abs(aireMm2(dd.front().points) - 200) < 0.5);
  // En forme de pastille pour l'éditeur d'empreinte
  const auto p = pastilleDepuisDessins(d, "1");
  REQUIRE(p);
  CHECK(p->forme == FormePastille::Libre);
  CHECK(!p->contourLibre.empty());
}

TEST_CASE("10.21 · DXF de toutes les couches et des cotes, un calque par couche, jeux enregistrés avec leur miroir", "[034][L10][10.21]") {
  ConceptionElec c = exempleElectronique();
  Cote x;
  x.uuid = nouvelUuid();
  x.points = {ptMm(0, 0), ptMm(30, 0)};
  c.circuit.cotes.push_back(x);
  JeuDxf j;
  j.nom = "Mécanique vue de dessous";
  j.miroir = true;
  j.couches = {Couche::CuivreDessous, Couche::SerigraphieDessous};
  enregistrerJeuDxf(c, j);
  enregistrerJeuDxf(c, JeuDxf{});
  const auto jeux = jeuxDxf(c);
  REQUIRE(jeux.size() == 2);
  CHECK(jeux.front().miroir);
  CHECK(jeux.front().couches.size() == 2);
  const std::string tout = dxfCouches(c, jeux.back());
  for (const char* calque : {"Edge.Cuts", "F.Cu", "B.Cu", "F.SilkS", "Cotes"}) CHECK(tout.find(std::string("0\nLAYER\n2\n") + calque + "\n") != std::string::npos);
  CHECK(tout.find("0\nTEXT\n8\nCotes\n") != std::string::npos);
  // Relu par le lecteur de DXF de l'esquisseur : traits sur les calques attendus, le miroir retourne les x
  const auto relu = prom::dxf::lireDessinDxf(tout);
  CHECK(relu.traits.size() > 50);
  const auto dessous = prom::dxf::lireDessinDxf(dxfCouches(c, jeux.front()));
  double xmaxTout = -1e9, xmaxDessous = -1e9;
  for (const auto& t : relu.traits)
    if (t.calque == "Edge.Cuts") xmaxTout = std::max({xmaxTout, t.a.x, t.b.x});
  for (const auto& t : dessous.traits)
    if (t.calque == "Edge.Cuts") xmaxDessous = std::max({xmaxDessous, t.a.x, t.b.x});
  CHECK(xmaxTout > 0);
  CHECK(xmaxDessous <= 1e-6);
}

#include "promethee/formats_cao.hpp"
#include "promethee/import_commun.hpp"
#include "promethee/kicad_electronique.hpp"

namespace {
using Paires = std::set<std::string>;
// Réseau de chaque pastille posée, aux noms près : partition des pastilles par réseau
std::set<std::set<std::string>> partitionPastilles(const ConceptionElec& c) {
  std::map<std::string, std::set<std::string>> m;
  const Connectivite k = connectivite(c);
  for (const auto& p : pastilles(c, k))
    if (!p.reseau.empty() && !p.numero.empty()) m[p.reseau].insert(p.reference + "." + p.numero);
  std::set<std::set<std::string>> r;
  for (const auto& [n, e] : m)
    if (e.size() > 1) r.insert(e);
  return r;
}
}  // namespace

TEST_CASE("10.22 · carte exportée en PADS ASCII puis relue : mêmes réseaux et mêmes pistes, zones manquantes listées", "[034][L10][10.22][10.17]") {
  const ConceptionElec c = exempleElectronique();
  RapportEcritureCao r;
  const std::string asc = padsAsciiCarte(c, &r);
  CHECK(asc.rfind("!PADS-POWERPCB", 0) == 0);
  CHECK(r.composants == static_cast<int>(c.circuit.empreintes.size()));
  CHECK(std::count_if(r.nonEcrits.begin(), r.nonEcrits.end(), [](const std::string& x) { return x.rfind("zone de cuivre", 0) == 0; }) == static_cast<long>(c.circuit.zones.size()));
  RapportLectureCao l;
  const ConceptionElec relue = conceptionDepuisPads(std::nullopt, asc, "Relue", &l);
  for (const auto& a : l.alertes) UNSCOPED_INFO(a);
  CHECK(relue.circuit.empreintes.size() == c.circuit.empreintes.size());
  CHECK(partitionPastilles(relue) == partitionPastilles(c));
  std::size_t segments = 0;
  for (const auto& p : c.circuit.pistes) segments += segmentsPiste(p, 20'000).size();
  CHECK(relue.circuit.pistes.size() == segments);
  CHECK(relue.circuit.vias.size() == c.circuit.vias.size());
  // Mêmes pistes : chaque segment relu existe dans la carte d'origine, sur la même couche et le même réseau
  int retrouvees = 0;
  const Connectivite kr = connectivite(relue);
  for (const auto& t : relue.circuit.pistes)
    for (const auto& o : c.circuit.pistes)
      if (o.couche == t.couche && distance(o.a, t.a) < 1000 && distance(o.b, t.b) < 1000) ++retrouvees;
  CHECK(retrouvees == static_cast<int>(segments));
  // Schéma en PADS Logic, relu seul
  const std::string logic = padsAsciiSchema(c);
  const ConceptionElec schema = conceptionDepuisPads(logic, std::nullopt, "Schéma");
  CHECK(schema.schemaSeul);
  // Par la chaîne d'import commune, le fichier est reconnu à son contenu
  const auto imp = importerConceptionTierce("carte.asc", asc, {});
  REQUIRE(imp);
  CHECK(imp->logiciel == "PADS");
}

TEST_CASE("10.22 · P-CAD ASCII écrit puis relu ; archive pour Altium aux formats KiCad et PADS", "[034][L10][10.22][10.18]") {
  const ConceptionElec c = exempleElectronique();
  RapportEcritureCao r;
  const std::string pcad = pcadAsciiCarte(c, &r);
  CHECK(estPcadAscii(pcad));
  CHECK(!r.nonEcrits.empty());
  RapportLectureCao l;
  const ConceptionElec relue = conceptionDepuisPcad(pcad, "Relue", &l);
  for (const auto& a : l.alertes) UNSCOPED_INFO(a);
  CHECK(relue.circuit.empreintes.size() == c.circuit.empreintes.size());
  CHECK(partitionPastilles(relue) == partitionPastilles(c));
  CHECK(relue.circuit.vias.size() == c.circuit.vias.size());
  CHECK(relue.circuit.contour.libre.size() >= 4);
  // Archive Altium : la carte KiCad qu'elle porte se relit aux mêmes réseaux
  RapportEcritureCao ra;
  const auto f = archivePourAltium(c, &ra);
  REQUIRE(f.size() == 5);
  const auto pcb = std::find_if(f.begin(), f.end(), [](const auto& x) { return x.first.size() > 10 && x.first.substr(x.first.size() - 10) == ".kicad_pcb"; });
  REQUIRE(pcb != f.end());
  CHECK(partitionPastilles(conceptionDepuisKicad(pcb->second, "Altium")) == partitionPastilles(c));
  CHECK(!ra.nonEcrits.empty());
}

#include "promethee/import_schemas.hpp"
#include "promethee/fichiers.hpp"

namespace {
std::string donnee14(const std::string& f) { return prom::lireFichier(prom::cheminFichier(std::string(PROMETHEE_DONNEES) + "/" + f)); }
LecteurFichier lecteurDe(const std::string& dossier) {
  return [dossier](const std::string& f) -> std::optional<std::string> {
    try {
      return donnee14(dossier + "/" + f);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };
}
std::string nomSansChemin(std::string n) {
  const auto p = n.find_last_of('/');
  return p == std::string::npos ? n : n.substr(p + 1);
}
// Réseau qui porte cette broche : son nom
std::string reseauDe(const NetlistLue& n, const std::string& ref, const std::string& broche) {
  for (const auto& [nom, noeuds] : n.reseaux)
    for (const auto& [r, b] : noeuds)
      if (r == ref && b == broche) return nom;
  return {};
}
}  // namespace

TEST_CASE("10.14 · projet KiCad 5 (.sch et cache .lib, feuille hiérarchique) : réseaux identiques à la netlist de KiCad 5", "[034][L10][10.14]") {
  const std::string sch = donnee14("kicad5/filtre.sch");
  REQUIRE(estSchemaKicad5(sch));
  RapportImportSchema r;
  const ConceptionElec c = conceptionDepuisKicad5(sch, "filtre", lecteurDe("kicad5"), &r);
  for (const auto& a : r.alertes) UNSCOPED_INFO(a);
  CHECK(r.alertes.empty());
  CHECK(r.feuilles == 2);
  CHECK(r.blocs == 1);
  CHECK(c.feuilles.size() == 2);
  CHECK(c.feuilles.front().blocs.size() == 1);
  CHECK(c.composants.size() == 5);
  REQUIRE(c.parReference("R2"));
  CHECK(c.parReference("R2")->valeur == "4k7");
  CHECK(c.parReference("U1")->empreinte == "Package_SO:SOIC-14_3.9x8.7mm_P1.27mm");
  const NetlistLue attendue = lireNetlist(donnee14("kicad5/filtre.net"));
  REQUIRE(attendue.erreurs.empty());
  const NetlistLue lue = netlistDe(c);
  for (const auto& [nom, noeuds] : lue.reseaux) {
    std::string x = nom + " :";
    for (const auto& [ref, b] : noeuds) x += " " + ref + "." + b;
    UNSCOPED_INFO(x);
  }
  CHECK(partitionDe(lue) == partitionDe(attendue));
  // Noms donnés par les alimentations (broches cachées comprises) et les étiquettes
  CHECK(reseauDe(lue, "U1", "7") == "GND");
  CHECK(reseauDe(lue, "U1", "14") == "VCC");
  CHECK(reseauDe(lue, "J1", "3") == "VCC");
  CHECK(reseauDe(lue, "U1", "1") == "OUT");
  CHECK(nomSansChemin(reseauDe(lue, "J1", "1")) == nomSansChemin(reseauDe(attendue, "J1", "1")));
  CHECK(nomSansChemin(reseauDe(lue, "J1", "4")) == nomSansChemin(reseauDe(attendue, "J1", "4")));
  // Par la chaîne commune : « .sch » de KiCad 5 reconnu à son contenu (EAGLE écrit du XML sous la même extension)
  const auto imp = importerConceptionTierce("filtre.sch", sch, lecteurDe("kicad5"));
  REQUIRE(imp);
  CHECK(imp->logiciel == "KiCad 5");
  CHECK(partitionDe(netlistDe(imp->conception)) == partitionDe(attendue));
}

TEST_CASE("10.14 · circuit LTspice (.asc) : réseaux identiques à la netlist d'LTspice, directive SPICE gardée", "[034][L10][10.14]") {
  const std::string asc = donnee14("ltspice/rc.asc");
  REQUIRE(estSchemaLtspice(asc));
  RapportImportSchema r;
  const ConceptionElec c = conceptionDepuisLtspice(asc, "rc", lecteurDe("ltspice"), &r);
  for (const auto& a : r.alertes) UNSCOPED_INFO(a);
  CHECK(r.alertes.empty());
  CHECK(c.composants.size() == 4);
  REQUIRE(r.directives.size() == 1);
  CHECK(r.directives.front() == ".tran 10m");
  REQUIRE(c.extensions.count("directivesSpice"));
  CHECK(c.extensions.at("directivesSpice").size() == 1);
  CHECK(c.parReference("V1")->valeur == "PULSE(0 5 0 1n 1n 1m 2m)");
  const NetlistLue attendue = lireNetlist(donnee14("ltspice/rc.net"));
  const NetlistLue lue = netlistDe(c);
  for (const auto& [nom, noeuds] : lue.reseaux) {
    std::string x = nom + " :";
    for (const auto& [ref, b] : noeuds) x += " " + ref + "." + b;
    UNSCOPED_INFO(x);
  }
  CHECK(partitionDe(lue) == partitionDe(attendue));
  CHECK(reseauDe(lue, "C1", "2") == "GND");
  CHECK(reseauDe(lue, "R2", "2") == "GND");
  CHECK(reseauDe(lue, "R1", "1") == "OUT");
  CHECK(reseauDe(lue, "V1", "1") == "IN");
  // Un symbole sans .asy connu est signalé, pas inventé
  RapportImportSchema r2;
  conceptionDepuisLtspice("Version 4\nSHEET 1 880 680\nSYMBOL Opamps\\\\LT1001 96 96 R0\nSYMATTR InstName U1\n", "x", {}, &r2);
  CHECK(r2.alertes.size() == 1);
  const auto imp = importerConceptionTierce("rc.asc", asc, lecteurDe("ltspice"));
  REQUIRE(imp);
  CHECK(imp->logiciel == "LTspice");
}

TEST_CASE("10.14 · synoptique Visio (.vsdx) : formes en blocs, connecteurs en liaisons du schéma fonctionnel", "[034][L10][10.14]") {
  const std::string ns = R"( xmlns="http://schemas.microsoft.com/office/visio/2012/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships")";
  auto forme = [](int id, double x, double y, const std::string& texte) {
    return "<Shape ID=\"" + std::to_string(id) + "\" NameU=\"Process\" Type=\"Shape\" Master=\"2\"><Cell N=\"PinX\" V=\"" + std::to_string(x) + "\"/><Cell N=\"PinY\" V=\"" + std::to_string(y) +
           "\"/><Cell N=\"Width\" V=\"1.5\"/><Cell N=\"Height\" V=\"0.75\"/><Text><cp IX=\"0\"/>" + texte + "</Text></Shape>";
  };
  auto connecteur = [](int id, const std::string& texte) {
    return "<Shape ID=\"" + std::to_string(id) + "\" NameU=\"Dynamic connector\" Type=\"Shape\" Master=\"5\"><Cell N=\"BeginX\" V=\"0\"/><Cell N=\"EndX\" V=\"1\"/>" + (texte.empty() ? "" : "<Text>" + texte + "</Text>") + "</Shape>";
  };
  auto lien = [](int de, int debut, int fin) {
    return "<Connect FromSheet=\"" + std::to_string(de) + "\" FromCell=\"BeginX\" FromPart=\"9\" ToSheet=\"" + std::to_string(debut) + "\" ToCell=\"PinX\" ToPart=\"3\"/><Connect FromSheet=\"" + std::to_string(de) +
           "\" FromCell=\"EndX\" FromPart=\"12\" ToSheet=\"" + std::to_string(fin) + "\" ToCell=\"PinX\" ToPart=\"3\"/>";
  };
  const std::string page = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<PageContents" + ns + " xml:space=\"preserve\"><Shapes>" + forme(1, 2, 8, "Alimentation") + forme(2, 5, 8, "Microcontrôleur") + forme(3, 5, 5, "Capteurs") +
                           connecteur(4, "5 V") + connecteur(5, "") + "</Shapes><Connects>" + lien(4, 1, 2) + lien(5, 2, 3) + "</Connects></PageContents>";
  const std::string vsdx = prom::zipStocke({{"[Content_Types].xml", "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>"},
                                            {"visio/document.xml", "<?xml version=\"1.0\"?><VisioDocument" + ns + "/>"},
                                            {"visio/pages/pages.xml", "<?xml version=\"1.0\"?><Pages" + ns + "><Page ID=\"0\" NameU=\"Page-1\"/></Pages>"},
                                            {"visio/pages/page1.xml", page}});
  RapportImportSchema r;
  const ConceptionElec c = conceptionDepuisVisio(vsdx, "synoptique", &r);
  for (const auto& a : r.alertes) UNSCOPED_INFO(a);
  CHECK(r.alertes.empty());
  REQUIRE(c.feuilles.size() == 1);
  const Feuille& f = c.feuilles.front();
  REQUIRE(f.blocs.size() == 3);
  CHECK(f.blocs[0].nom == "Alimentation");
  CHECK(f.blocs[1].nom == "Microcontrôleur");
  CHECK(r.liaisons == 2);
  // Alimentation à gauche du microcontrôleur : port à droite de l'une, à gauche de l'autre, nommé du texte du
  // connecteur ; les capteurs dessous : port en bas du microcontrôleur, en haut des capteurs
  REQUIRE(f.blocs[0].ports.size() == 1);
  CHECK(f.blocs[0].ports[0].cote == 1);
  CHECK(f.blocs[0].ports[0].nom == "5 V");
  REQUIRE(f.blocs[1].ports.size() == 2);
  CHECK(f.blocs[1].ports[0].cote == 0);
  CHECK(f.blocs[1].ports[1].cote == 3);
  CHECK(f.blocs[2].ports.at(0).cote == 2);
  // Chaque liaison est un chemin de fils continu d'un port à l'autre
  auto relies = [&](Pt a, Pt b) {
    std::set<std::pair<Nm, Nm>> atteints{{a.x, a.y}};
    for (bool change = true; change;) {
      change = false;
      for (const auto& w : f.fils) {
        const bool ia = atteints.count({w.a.x, w.a.y}), ib = atteints.count({w.b.x, w.b.y});
        if (ia != ib) atteints.insert(ia ? std::pair{w.b.x, w.b.y} : std::pair{w.a.x, w.a.y}), change = true;
      }
    }
    return atteints.count({b.x, b.y}) == 1;
  };
  CHECK(relies(f.blocs[0].ports[0].p, f.blocs[1].ports[0].p));
  CHECK(relies(f.blocs[1].ports[1].p, f.blocs[2].ports[0].p));
  CHECK(!relies(f.blocs[0].ports[0].p, f.blocs[2].ports[0].p));
  const auto imp = importerConceptionTierce("synoptique.vsdx", vsdx, {});
  REQUIRE(imp);
  CHECK(imp->logiciel == "Visio");
}
