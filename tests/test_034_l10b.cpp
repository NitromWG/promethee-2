// Prométhée : essais de la 0.34, lot L10 (suite) — exports 3D de la carte (10.10, 10.24) et échange IDX avec la
// mécanique (10.10, 10.24). Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
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
