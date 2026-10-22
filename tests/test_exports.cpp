// Prométhée : exports et nomenclatures (constats EX de l'audit mécanique 0.25). Licence GPL-3.0-only.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include "promethee/assemblage.hpp"
#include "promethee/construction.hpp"
#include "promethee/echanges.hpp"
#include "promethee/fabrication.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/modele.hpp"
#include "promethee/nomenclature.hpp"
#include "promethee/piece.hpp"
#include "promethee/structure.hpp"

using namespace prom;
using Catch::Approx;

namespace {

std::string dossierEssai(const std::string& nom) {
  const auto d = std::filesystem::temp_directory_path() / ("promethee_exports_" + nom);
  std::filesystem::create_directories(d);
  return d.string();
}

Json pieceNommee(const std::string& nom, const std::string& couleur = "#C9CFD6") {
  Piece p;
  p.nom = nom;
  p.couleur = couleur;
  return pieceVersJson(p);
}

ComposantAssemblage composantPiece(int piece, double x = 0, int miroir = 0) {
  ComposantAssemblage c;
  c.piece = piece;
  c.x = x;
  c.miroir = miroir;
  return c;
}

ComposantAssemblage composantSous(int assemblage, double x = 0, int miroir = 0) {
  ComposantAssemblage c;
  c.assemblage = assemblage;
  c.x = x;
  c.miroir = miroir;
  return c;
}

int quantite(const std::vector<LigneNomenclatureMeca>& l, int piece, bool symetrique) {
  for (const auto& x : l)
    if (x.piece == piece && x.symetrique == symetrique) return x.quantite;
  return 0;
}

// Forme attendue d'un assemblage, composée comme l'atelier d'assemblage : chaque composant (pièce ou
// sous-assemblage) transformé par son placement complet, symétrie comprise.
TopoDS_Shape composer(const Projet& p, int a, const std::vector<ResultatPiece>& pieces) {
  BRep_Builder b;
  TopoDS_Compound c;
  b.MakeCompound(c);
  for (const auto& k : p.assemblages[static_cast<std::size_t>(a)].composants) {
    if (k.supprime) continue;
    const TopoDS_Shape f = k.assemblage >= 0 ? composer(p, k.assemblage, pieces) : pieces[static_cast<std::size_t>(k.piece)].forme;
    b.Add(c, BRepBuilderAPI_Transform(f, placementComposant(k), true).Shape());
  }
  return c;
}

struct Masse { double volume; gp_Pnt centre; };
Masse masse(const TopoDS_Shape& s) {
  GProp_GProps g;
  BRepGProp::VolumeProperties(s, g);
  return {g.Mass(), g.CentreOfMass()};
}

}  // namespace

TEST_CASE("EX-01 nomenclature : sous-assemblages développés, racines seules, suspendus écartés", "[exports][EX-01]") {
  Projet p;
  p.pieces = {pieceNommee("Plaque"), pieceNommee("Vis")};
  Assemblage a1, a2;
  a1.nom = "Visserie";
  a1.composants = {composantPiece(1), composantPiece(1), composantPiece(1), composantPiece(1)};
  a2.nom = "Ensemble";
  ComposantAssemblage suspendu = composantPiece(0);
  suspendu.supprime = true;
  a2.composants = {composantPiece(0), composantSous(0), composantSous(0), suspendu};
  p.assemblages = {a1, a2};
  const auto l = nomenclatureMecanique(p);
  CHECK(quantite(l, 0, false) == 1);
  CHECK(quantite(l, 1, false) == 8);
  // Nomenclature d'un seul assemblage
  CHECK(quantite(nomenclatureMecanique(p, 0), 1, false) == 4);
  // Renvoi circulaire (fichier abîmé) : la récursion s'arrête
  p.assemblages[0].composants.push_back(composantSous(1));
  CHECK_NOTHROW(nomenclatureMecanique(p, 1));
}

TEST_CASE("EX-11 nomenclature : un composant symétrique est une pièce distincte", "[exports][EX-11]") {
  Projet p;
  p.pieces = {pieceNommee("Support")};
  Assemblage a, s;
  s.nom = "Côté";
  s.composants = {composantPiece(0)};
  a.nom = "Bâti";
  a.composants = {composantPiece(0), composantPiece(0, 50, 3), composantSous(0, 100, 0), composantSous(0, 150, 1)};
  p.assemblages = {s, a};
  const auto l = nomenclatureMecanique(p, 1);
  CHECK(quantite(l, 0, false) == 2);
  CHECK(quantite(l, 0, true) == 2);  // symétrique direct, et pièce d'un sous-assemblage symétrique
}

TEST_CASE("EX-14 tri naturel des repères à longue suite de chiffres, champs CSV", "[exports][EX-14]") {
  CHECK(avantNaturelRepere("TP2", "TP10"));
  CHECK(avantNaturelRepere("TP3000000000", "TP30000000000000000000000"));
  CHECK_FALSE(avantNaturelRepere("TP30000000000000000000000", "TP3000000000"));
  CHECK(avantNaturelRepere("R007", "R8"));
  CHECK(champCsv("a\rb").front() == '"');
  CHECK(champCsv("x;y") == "\"x;y\"");
  CHECK(champCsv("dit \"oui\"") == "\"dit \"\"oui\"\"\"");
}

TEST_CASE("EX-07 EX-09 EX-02 STEP d'assemblage : racine, prototypes nommés et colorés, symétries", "[exports][EX-07]") {
  // Placement complet = placement rigide · rotation · symétrie YZ, base de l'écriture des symétries en STEP
  for (int m : {1, 2, 3}) {
    ComposantAssemblage c = composantPiece(0, 12, m);
    c.y = -4; c.z = 7; c.rx = 20; c.ry = -35; c.rz = 110;
    ComposantAssemblage r = c;
    r.miroir = 0;
    gp_Trsf q, s;
    if (m == 1) q.SetRotation(gp::OY(), M_PI);
    if (m == 2) q.SetRotation(gp::OZ(), M_PI);
    s.SetMirror(gp::YOZ());
    const gp_Trsf attendu = placementComposant(r) * q * s, obtenu = placementComposant(c);
    const gp_Pnt t(3, -8, 5);
    CHECK(t.Transformed(attendu).Distance(t.Transformed(obtenu)) < 1e-9);
  }

  Projet p;
  p.pieces = {pieceNommee("Plaque", "#2E6B4F"), pieceNommee("Patte", "#C2402E")};
  // Pièces dissymétriques : la symétrie se voit sur le centre de masse
  std::vector<ResultatPiece> pieces(2);
  pieces[0].forme = BRepPrimAPI_MakeBox(gp_Pnt(10, 0, 0), 60, 40, 5).Shape();
  pieces[1].forme = BRepPrimAPI_MakeBox(gp_Pnt(5, 2, 1), 8, 3, 20).Shape();
  Assemblage sous, ens;
  sous.nom = "Coin";
  sous.composants = {composantPiece(0), composantPiece(1, 20)};
  sous.composants[1].rz = 30;
  ens.nom = "Bâti & co";
  ComposantAssemblage suspendu = composantPiece(1, 500);
  suspendu.supprime = true;
  ens.composants = {composantPiece(0), composantPiece(0, 100), composantPiece(0, -100, 3), composantSous(0, 0, 0), composantSous(0, 300, 2), suspendu};
  ens.composants[3].z = 40;
  ens.composants[4].z = -40;
  ens.composants[4].rx = 15;
  p.assemblages = {sous, ens};

  const std::string chemin = dossierEssai("step") + "/assemblage.step";
  const int manquants = exporterStepAssemblage(p, 1, [&](int i) -> const ResultatPiece& { return pieces[static_cast<std::size_t>(i)]; }, chemin);
  CHECK(manquants == 0);

  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  Handle(TDocStd_Document) doc;
  app->NewDocument("MDTV-XCAF", doc);
  STEPCAFControl_Reader lecteur;
  lecteur.SetNameMode(true);
  lecteur.SetColorMode(true);
  REQUIRE(lecteur.ReadFile(chemin.c_str()) == IFSelect_RetDone);
  REQUIRE(lecteur.Transfer(doc));
  Handle(XCAFDoc_ShapeTool) formes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
  Handle(XCAFDoc_ColorTool) couleurs = XCAFDoc_DocumentTool::ColorTool(doc->Main());
  TDF_LabelSequence libres;
  formes->GetFreeShapes(libres);
  REQUIRE(libres.Length() == 1);
  const TDF_Label racine = libres.Value(1);
  CHECK(formes->IsAssembly(racine));
  TDF_LabelSequence composants;
  formes->GetComponents(racine, composants, false);
  CHECK(composants.Length() == 5);  // le composant suspendu n'est pas écrit

  // Prototypes : Plaque (verte) partagée, Plaque symétrique distincte, sous-assemblage et sa version symétrique
  int plaques = 0, symetriques = 0, sousAssemblages = 0;
  for (int i = 1; i <= composants.Length(); ++i) {
    TDF_Label ref;
    REQUIRE(formes->GetReferredShape(composants.Value(i), ref));
    Handle(TDataStd_Name) nom;
    REQUIRE(ref.FindAttribute(TDataStd_Name::GetID(), nom));
    const TCollection_AsciiString n(nom->Get());
    if (formes->IsAssembly(ref)) { ++sousAssemblages; continue; }
    if (n.IsEqual("Plaque")) ++plaques;
    if (n.IsEqual("Plaque (symétrique)")) ++symetriques;
    // Le lecteur range la couleur sur l'étiquette ou sur la forme, en générique ou en surface selon la version
    Quantity_Color c;
    const TopoDS_Shape forme = formes->GetShape(ref);
    REQUIRE((couleurs->GetColor(ref, XCAFDoc_ColorGen, c) || couleurs->GetColor(ref, XCAFDoc_ColorSurf, c) || couleurs->GetColor(forme, XCAFDoc_ColorGen, c) ||
             couleurs->GetColor(forme, XCAFDoc_ColorSurf, c)));
    Standard_Real r, g, b;
    c.Values(r, g, b, Quantity_TOC_sRGB);
    CHECK(r == Approx(0x2E / 255.0).margin(0.003));
    CHECK(g == Approx(0x6B / 255.0).margin(0.003));
    CHECK(b == Approx(0x4F / 255.0).margin(0.003));
  }
  CHECK(plaques == 2);
  CHECK(symetriques == 1);
  CHECK(sousAssemblages == 2);

  // Géométrie : volume et centre de masse identiques à la composition de l'atelier (symétries comprises)
  const Masse attendue = masse(composer(p, 1, pieces));
  const Masse lue = masse(formes->GetShape(racine));
  CHECK(lue.volume == Approx(attendue.volume).epsilon(1e-6));
  CHECK(lue.centre.Distance(attendue.centre) < 1e-4);
  app->Close(doc);
}

TEST_CASE("EX-04 EX-17 EX-18 EX-19 fichiers de maillage et IGES", "[exports][EX-04]") {
  const std::string d = dossierEssai("maillages");
  const TopoDS_Shape s = BRepPrimAPI_MakeBox(10, 10, 10).Shape();
  exporter3mf({{"Bride & joint <2> \"A\"", s, 0.18, 0.42, 0.31}, {"Vis", s}}, d + "/p.3mf");
  std::ifstream f3(d + "/p.3mf", std::ios::binary);
  const std::string zip((std::istreambuf_iterator<char>(f3)), std::istreambuf_iterator<char>());
  CHECK(zip.find("Bride &amp; joint &lt;2&gt; &quot;A&quot;") != std::string::npos);
  CHECK(zip.find("displaycolor=\"#2E6B4F\"") != std::string::npos);
  CHECK(zip.find("Bride & joint") == std::string::npos);

  exporterObj({{"Vis M3", s}, {"Vis-M3", s, 1, 0, 0}}, d + "/Support moteur.obj");
  std::ifstream fo(d + "/Support moteur.obj");
  std::string premiere, mtllib;
  std::getline(fo, premiere);
  std::getline(fo, mtllib);
  CHECK(mtllib == "mtllib Support_moteur.mtl");
  CHECK(std::filesystem::exists(d + "/Support_moteur.mtl"));
  std::ifstream fm(d + "/Support_moteur.mtl");
  const std::string mtl((std::istreambuf_iterator<char>(fm)), std::istreambuf_iterator<char>());
  CHECK(mtl.find("newmtl Vis_M3\n") != std::string::npos);
  CHECK(mtl.find("newmtl Vis_M3_2\n") != std::string::npos);

  exporterPly({{"Piece", s}}, d + "/p.ply");
  std::ifstream fp(d + "/p.ply", std::ios::binary);
  const std::string ply((std::istreambuf_iterator<char>(fp)), std::istreambuf_iterator<char>());
  const std::string entete = ply.substr(0, ply.find("end_header"));
  bool ascii = true;
  for (unsigned char c : entete) ascii = ascii && c < 0x80;
  CHECK(ascii);

  exporterIges({{"Piece", s}}, d + "/p.igs");
  std::ifstream fi(d + "/p.igs");
  std::string ligne;
  bool surfacesDecoupees = false, solideBrep = false;
  while (std::getline(fi, ligne))
    if (ligne.size() >= 73 && ligne[72] == 'D') {
      const std::string type = ligne.substr(0, 8);
      surfacesDecoupees = surfacesDecoupees || type.find("144") != std::string::npos;
      solideBrep = solideBrep || type.find("186") != std::string::npos;
    }
  CHECK(surfacesDecoupees);
  CHECK_FALSE(solideBrep);
}

TEST_CASE("EX-21 vérification de fabrication : jamais de notation scientifique", "[exports][EX-21]") {
  // Trou de Ø 2 mm sur 1 500 mm : 750 fois le diamètre
  const TopoDS_Shape bloc = BRepPrimAPI_MakeBox(gp_Pnt(-10, -10, 0), 20, 20, 1500).Shape();
  const TopoDS_Shape trou = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, -1), gp::DZ()), 1, 1502).Shape();
  const TopoDS_Shape piece = BRepAlgoAPI_Cut(bloc, trou).Shape();
  bool trouve = false;
  for (const auto& a : verifierFabrication(piece, {})) {
    INFO(a.message);
    CHECK(a.message.find("e+") == std::string::npos);
    if (a.message.find("1500 mm") != std::string::npos && a.message.find("750 fois") != std::string::npos) trouve = true;
  }
  CHECK(trouve);
}

// ---------- M12 : STEP structuré, noms Unicode, couleurs par face, AP242 ; import avec structure ----------
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include "promethee/fichiers.hpp"
#include "promethee/import_cao.hpp"

namespace {
std::string lireTout(const std::string& chemin) {
  std::ifstream f(cheminFichier(chemin), std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
bool toutAscii(const std::string& s) {
  return std::all_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}
std::vector<TopoDS_Face> facesDe(const TopoDS_Shape& s) {
  std::vector<TopoDS_Face> r;
  for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) r.push_back(TopoDS::Face(ex.Current()));
  return r;
}
}  // namespace

TEST_CASE("M12 codage ISO 10303-21 des chaînes : \\X2\\ et \\X4\\, coupures de ligne", "[exports][step][M12]") {
  CHECK(texteStepAscii("#1=PRODUCT('Pièce','',$);") == "#1=PRODUCT('Pi\\X2\\00E8\\X0\\ce','',$);");
  CHECK(texteStepAscii("'Ø12 — 日本'") == "'\\X2\\00D8\\X0\\12 \\X2\\2014\\X0\\ \\X2\\65E5672C\\X0\\'");
  CHECK(texteStepAscii("'a😀b'") == "'a\\X4\\0001F600\\X0\\b'");
  // Caractère coupé par un retour à la ligne de l'écrivain : recollé
  CHECK(texteStepAscii("'\xC3\n\xA9t\xC3\xA9'") == "'\\X2\\00E9\\X0\\t\\X2\\00E9\\X0\\'");
  CHECK(texteStepAscii("'C:\\\\x ''q'''") == "'C:\\\\x ''q'''");  // l'ASCII n'est pas touché
  CHECK(toutAscii(texteStepAscii("'\xFF\xC3'")));                   // octets invalides : codés tels quels
}

TEST_CASE("M12 STEP d'une pièce : nom Unicode, couleurs par face, AP242, en-tête, relecture structurée", "[exports][step][M12]") {
  const std::string d = dossierEssai("m12");
  const TopoDS_Shape boite = BRepPrimAPI_MakeBox(40, 30, 10).Shape();
  const auto faces = facesDe(boite);
  REQUIRE(faces.size() == 6);
  PieceNommee pn{"Équerre Ø12 — 日本", boite, 0.18, 0.42, 0.31};
  pn.couleursFaces = {{faces[1], {1.0, 0.0, 0.0}}, {faces[4], {0.0, 0.0, 1.0}}};
  OptionsStep o;
  o.schema = SchemaStep::AP242;
  o.auteur = "Évan";
  o.organisation = "Atelier";
  const std::string chemin = d + "/équerre.step";
  exporterStep({pn}, chemin, o);
  const std::string t = lireTout(chemin);
  CHECK(toutAscii(t));
  CHECK(t.find("AP242_MANAGED_MODEL_BASED_3D_ENGINEERING") != std::string::npos);
  CHECK(t.find("\\X2\\00C9\\X0\\querre \\X2\\00D8\\X0\\12") != std::string::npos);
  CHECK(t.find("Promethee") != std::string::npos);
  CHECK(t.find("\\X2\\00C9\\X0\\van") != std::string::npos);  // auteur
  CHECK(!std::filesystem::exists(cheminFichier(chemin + ".ecriture")));

  const ImportCAO imp = lireCAOStructure(chemin);
  CHECK(imp.format == "STEP");
  REQUIRE(imp.produits.size() == 1);
  CHECK(imp.instances() == 1);
  CHECK_FALSE(imp.estAssemblage());
  CHECK(imp.produits[0].nom == "Équerre Ø12 — 日本");
  CHECK(imp.produits[0].couleur == "#2E6B4F");
  REQUIRE(imp.produits[0].couleursFaces.size() == 2);
  CHECK(imp.produits[0].couleursFaces.at(1) == "#FF0000");
  CHECK(imp.produits[0].couleursFaces.at(4) == "#0000FF");

  // AP214 par défaut, et le réglage global n'est pas laissé à AP242
  exporterStep({pn}, d + "/defaut.step");
  CHECK(lireTout(d + "/defaut.step").find("AUTOMOTIVE_DESIGN") != std::string::npos);

  // IGES : noms translittérés, couleurs gardées
  exporterIges({pn}, d + "/équerre.igs");
  const ImportCAO ig = lireCAOStructure(d + "/équerre.igs");
  CHECK(ig.format == "IGES");
  REQUIRE(!ig.produits.empty());
  bool vert = false, rouge = false;
  for (const auto& p : ig.produits) {
    vert = vert || p.couleur == "#2E6B4F";
    for (const auto& [k, c] : p.couleursFaces) rouge = rouge || c == "#FF0000";
  }
  CHECK(vert);
  CHECK(rouge);
}

TEST_CASE("M12 STEP d'assemblage relu avec sa structure : produits partagés, symétrie, placements, couleurs de faces", "[exports][step][M12]") {
  Projet p;
  p.pieces = {pieceNommee("Plaque d’appui", "#2E6B4F"), pieceNommee("Patte", "#C2402E")};
  p.pieces[0]["couleursFaces"] = {{"1:dessus", "#FFCC00"}};
  std::vector<ResultatPiece> pieces(2);
  pieces[0].forme = BRepPrimAPI_MakeBox(gp_Pnt(10, 0, 0), 60, 40, 5).Shape();
  pieces[1].forme = BRepPrimAPI_MakeBox(gp_Pnt(5, 2, 1), 8, 3, 20).Shape();
  const auto f0 = facesDe(pieces[0].forme);
  for (std::size_t k = 0; k < f0.size(); ++k) pieces[0].faces.push_back({f0[k], k == 5 ? std::string("1:dessus") : "1:f" + std::to_string(k)});
  Assemblage sous, ens;
  sous.nom = "Coin";
  sous.composants = {composantPiece(0), composantPiece(1, 20)};
  sous.composants[1].rz = 30;
  ens.nom = "Bâti « R&D »";
  ens.composants = {composantPiece(0), composantPiece(0, 100), composantPiece(0, -100, 3), composantSous(0, 0, 0)};
  ens.composants[3].z = 40;
  ens.composants[3].rx = 15;
  p.assemblages = {sous, ens};
  const std::string chemin = dossierEssai("m12") + "/bâti.step";
  CHECK(exporterStepAssemblage(p, 1, [&](int i) -> const ResultatPiece& { return pieces[static_cast<std::size_t>(i)]; }, chemin) == 0);
  CHECK(toutAscii(lireTout(chemin)));

  const ImportCAO imp = lireCAOStructure(chemin);
  CHECK(imp.racine.nom == "Bâti « R&D »");
  CHECK(imp.estAssemblage());
  CHECK(imp.instances() == 5);
  CHECK(imp.niveaux() == 2);
  // Produits : la plaque (deux instances directes et une dans le coin), sa symétrique, la patte
  int plaque = -1, symetrique = -1, patte = -1;
  for (std::size_t i = 0; i < imp.produits.size(); ++i) {
    if (imp.produits[i].nom == "Plaque d’appui") plaque = static_cast<int>(i);
    if (imp.produits[i].nom == "Plaque d’appui (symétrique)") symetrique = static_cast<int>(i);
    if (imp.produits[i].nom == "Patte") patte = static_cast<int>(i);
  }
  REQUIRE(plaque >= 0);
  REQUIRE(symetrique >= 0);
  REQUIRE(patte >= 0);
  CHECK(imp.produits.size() == 3);
  CHECK(imp.produits[static_cast<std::size_t>(plaque)].instances == 3);
  CHECK(imp.produits[static_cast<std::size_t>(plaque)].couleur == "#2E6B4F");
  CHECK(imp.produits[static_cast<std::size_t>(patte)].couleur == "#C2402E");
  // La face colorée (« dessus », sixième face) est retrouvée, aussi dans la pièce symétrique
  REQUIRE(imp.produits[static_cast<std::size_t>(plaque)].couleursFaces.size() == 1);
  CHECK(imp.produits[static_cast<std::size_t>(plaque)].couleursFaces.begin()->second == "#FFCC00");
  CHECK(imp.produits[static_cast<std::size_t>(symetrique)].couleursFaces.size() == 1);
  // Sous-assemblage nommé, avec ses deux instances
  int coins = 0;
  for (const auto& e : imp.racine.enfants)
    if (e.assemblage()) { ++coins; CHECK(e.enfants.size() == 2); }
  CHECK(coins == 1);
  // Géométrie posée : volume et centre de masse de l'assemblage d'origine
  const ProduitImporte plat = imp.aplati();
  const Masse attendue = masse(composer(p, 1, pieces));
  const Masse lue = masse(plat.forme);
  CHECK(lue.volume == Approx(attendue.volume).epsilon(1e-6));
  CHECK(lue.centre.Distance(attendue.centre) < 1e-4);
  // À plat : la patte (autre couleur) garde la sienne face par face, la face jaune aussi
  int jaunes = 0, rouges = 0;
  for (const auto& [k, c] : plat.couleursFaces) { jaunes += c == "#FFCC00"; rouges += c == "#C2402E"; }
  CHECK(jaunes == 4);   // trois plaques et la plaque symétrique
  CHECK(rouges == 6);   // la patte du coin
}

TEST_CASE("M12 composant placé par une transformation quelconque, symétrie comprise", "[assemblage][M12]") {
  gp_Trsf r, t, s;
  r.SetRotation(gp_Ax1(gp::Origin(), gp_Dir(1, 2, 3)), 0.7);
  t.SetTranslation(gp_Vec(12, -4, 30));
  s.SetMirror(gp::ZOX());
  for (const gp_Trsf& x : {t * r, t * r * s, gp_Trsf()}) {
    const ComposantAssemblage c = composantDepuisPlacement(x);
    CHECK(c.miroir == (x.IsNegative() ? 3 : 0));
    const gp_Trsf y = placementComposant(c);
    for (const gp_Pnt q : {gp_Pnt(1, 0, 0), gp_Pnt(0, 5, 0), gp_Pnt(3, -2, 7)}) CHECK(q.Transformed(x).Distance(q.Transformed(y)) < 1e-9);
  }
}

// ---------- M10 : nomenclature à niveaux, repères stables, propriétés, masses, CSV et XLSX ----------
#include "promethee/archive.hpp"
#include "promethee/nomenclature_meca.hpp"

TEST_CASE("M10 nomenclature à niveaux : arbre regroupé, numéros, quantités totales, symétries", "[nomenclature][M10]") {
  Projet p;
  p.pieces = {pieceNommee("Vis M5"), pieceNommee("Plaque"), pieceNommee("Patte")};
  Assemblage fixation, ens;
  fixation.nom = "Fixation";
  fixation.composants = {composantPiece(2), composantPiece(0), composantPiece(0)};
  ens.nom = "Ensemble";
  ComposantAssemblage suspendu = composantPiece(1, 500);
  suspendu.supprime = true;
  ens.composants = {composantPiece(1), composantSous(0), composantSous(0, 100), composantSous(0, -100, 3), suspendu};
  p.assemblages = {fixation, ens};
  const auto l = nomenclatureNiveaux(p, 1);
  REQUIRE(l.size() == 7);
  CHECK((l[0].numero == "1" && !l[0].assemblage && l[0].indice == 1 && l[0].quantite == 1));
  CHECK((l[1].numero == "2" && l[1].assemblage && l[1].quantite == 2 && l[1].quantiteTotale == 2 && !l[1].symetrique));
  CHECK((l[2].numero == "2.1" && l[2].niveau == 1 && l[2].indice == 2 && l[2].quantiteTotale == 2));
  CHECK((l[3].numero == "2.2" && l[3].indice == 0 && l[3].quantite == 2 && l[3].quantiteTotale == 4));
  CHECK((l[4].numero == "3" && l[4].assemblage && l[4].symetrique && l[4].quantite == 1));
  CHECK((l[5].numero == "3.1" && l[5].symetrique && l[5].indice == 2));
  CHECK((l[6].numero == "3.2" && l[6].symetrique && l[6].quantiteTotale == 2));

  // Repères stables : attribués une fois, gardés quand une pièce disparaît ; versions symétriques numérotées
  CHECK(assurerReperes(p) > 0);
  const int reperePatte = repereArticle(p, false, 2, false), repereVisS = repereArticle(p, false, 0, true);
  CHECK(reperePatte > 0);
  CHECK(repereVisS > 0);
  CHECK(repereArticle(p, true, 0, false) > 0);
  CHECK(assurerReperes(p) == 0);  // rien à refaire
  std::set<int> tous;
  for (int i = 0; i < 3; ++i) { tous.insert(repereArticle(p, false, i, false)); }
  for (int a = 0; a < 2; ++a) tous.insert(repereArticle(p, true, a, false));
  CHECK(tous.size() == 5);
  // La plaque supprimée : la patte garde son repère (le rang, lui, change)
  supprimerPiece(p, 1);
  assurerReperes(p);
  CHECK(repereArticle(p, false, 1, false) == reperePatte);
  // Une pièce copiée avec son repère reçoit un nouveau numéro
  p.pieces.push_back(p.pieces[0]);
  CHECK(assurerReperes(p) == 1);  // le repère en double ; le repère symétrique copié, inutile, est retiré
  CHECK(repereArticle(p, false, 2, false) != repereArticle(p, false, 0, false));
  CHECK(repereArticle(p, false, 2, true) == 0);
  CHECK(repereArticle(p, false, 0, true) == repereVisS);
}

TEST_CASE("M10 tableau de nomenclature : colonnes, propriétés, masses, total ; CSV et XLSX", "[nomenclature][M10]") {
  Projet p;
  Piece vis;
  vis.nom = "Vis CHC M5×12";
  vis.reference = "ISO 4762";
  vis.materiau = "Acier";
  vis.proprietes = {{"Révision", "B"}, {"Traitement", "Zingué"}};
  Piece plaque;
  plaque.nom = "Plaque <support> & co";
  plaque.reference = "PL-01";
  plaque.proprietes = {{"révision", "C"}};
  p.pieces = {pieceVersJson(vis), pieceVersJson(plaque)};
  Assemblage fix, ens;
  fix.nom = "Fixation";
  fix.composants = {composantPiece(0), composantPiece(0)};
  ens.nom = "Bâti : R&D [v2]";
  ens.composants = {composantPiece(1), composantSous(0), composantSous(0, 50)};
  p.assemblages = {fix, ens};
  assurerReperes(p);
  // Colonnes : fixes puis propriétés rencontrées
  const auto cols = colonnesNomenclature(p);
  CHECK(std::any_of(cols.begin(), cols.end(), [](const ColonneNomenclature& c) { return c.cle == "prop:Révision"; }));
  CHECK(std::any_of(cols.begin(), cols.end(), [](const ColonneNomenclature& c) { return c.cle == "prop:Traitement"; }));
  auto masse = [](int i) { return MassePiece{i == 0 ? 2.5 : 850.0, true, i == 1}; };
  const auto t = tableauNomenclature(p, 1, VueNomenclature::Niveaux, {"numero", "quantite", "quantiteTotale", "designation", "prop:Révision", "masseUnitaire", "masseTotale"}, masse);
  REQUIRE(t.lignes.size() == 3);
  CHECK(t.lignes[0][3].texte == "Plaque <support> & co");
  CHECK(t.lignes[0][4].texte == "C");            // « révision » trouvée sans tenir compte de la casse
  CHECK(t.lignes[1][3].texte == "Fixation");
  CHECK(t.assemblages[1] == 1);
  CHECK(t.lignes[1][5].nombre == Approx(5.0));   // un sous-assemblage : deux vis
  CHECK(t.lignes[2][2].nombre == Approx(4));     // vis : 2 par fixation × 2 fixations
  CHECK(t.lignes[2][6].nombre == Approx(10.0));  // 4 × 2,5 g
  CHECK(t.niveaux[2] == 1);
  CHECK(t.quantitePieces == 5);
  CHECK(t.masseTotale == Approx(860.0));
  CHECK(t.masseProvisoire);
  CHECK(t.texteTotal() == "5 pièces · 860 g (provisoire : densité à confirmer)");
  // Pièces seules, triées par repère
  const auto tp = tableauNomenclature(p, 1, VueNomenclature::Pieces, {"repere", "quantite", "designation"}, masse);
  REQUIRE(tp.lignes.size() == 2);
  CHECK(tp.lignes[0][0].nombre < tp.lignes[1][0].nombre);
  // Premier niveau : deux articles (la plaque, la fixation ×2)
  const auto t1 = tableauNomenclature(p, 1, VueNomenclature::PremierNiveau, colonnesParDefaut(VueNomenclature::PremierNiveau), masse);
  CHECK(t1.lignes.size() == 2);
  // CSV : marque, séparateur, champs protégés, retrait, total
  const std::string csv = csvNomenclature(t);
  CHECK(csv.rfind("\xEF\xBB\xBF", 0) == 0);
  CHECK(csv.find("  Vis CHC M5×12") != std::string::npos);
  CHECK(csv.find("Total : 5 pièces") != std::string::npos);
  // XLSX : archive valide (parties attendues), texte échappé, nom de feuille nettoyé, nombres en nombres
  const std::string x = xlsxNomenclature(t);
  CHECK(x.rfind("PK\x03\x04", 0) == 0);
  for (const char* part : {"[Content_Types].xml", "xl/workbook.xml", "xl/worksheets/sheet1.xml", "xl/styles.xml", "_rels/.rels"}) CHECK(x.find(part) != std::string::npos);
  CHECK(x.find("Plaque &lt;support&gt; &amp; co") != std::string::npos);
  CHECK(x.find("name=\"Bâti  R&amp;D v2\"") != std::string::npos);
  CHECK(x.find("<v>860</v>") != std::string::npos);
  const std::string d = dossierEssai("m10");
  ecrireFichierAtomique(cheminFichier(d + "/nomenclature.xlsx"), x);
  CHECK(std::filesystem::file_size(cheminFichier(d + "/nomenclature.xlsx")) == x.size());
}
