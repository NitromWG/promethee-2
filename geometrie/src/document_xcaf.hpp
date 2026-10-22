// Prométhée : document XCAF commun aux écrivains STEP et IGES (interne à la bibliothèque de géométrie).
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <APIHeaderSection_MakeHeader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESCAFControl_Writer.hxx>
#include <IGESControl_Controller.hxx>
#include <Interface_Static.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Controller.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Version.hxx>
#include <StepData_StepModel.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include "promethee/fichiers.hpp"
#include "promethee/geometrie.hpp"

namespace prom::xcaf {

// Document XCAF : prototypes nommés et colorés (couleurs en sRGB, comme les donne l'utilisateur : EX-15), couleurs
// par face, racine d'assemblage, instances placées. Les noms sont gardés en entier (UTF-8) puis codés à l'écriture.
using CouleursFaces = std::vector<std::pair<TopoDS_Shape, std::array<double, 3>>>;
struct DocumentStep {
  Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
  Handle(TDocStd_Document) doc;
  Handle(XCAFDoc_ShapeTool) formes;
  Handle(XCAFDoc_ColorTool) couleurs;
  DocumentStep() {
    app->NewDocument("MDTV-XCAF", doc);
    formes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    couleurs = XCAFDoc_DocumentTool::ColorTool(doc->Main());
  }
  ~DocumentStep() { app->Close(doc); }
  static void nommer(const TDF_Label& l, const std::string& nom) { TDataStd_Name::Set(l, TCollection_ExtendedString(nom.c_str(), true)); }
  TDF_Label prototype(const TopoDS_Shape& forme, const std::string& nom, double r, double g, double b, const CouleursFaces& faces = {}) {
    const TDF_Label l = formes->AddShape(forme, false);
    nommer(l, nom);
    couleurs->SetColor(l, Quantity_Color(r, g, b, Quantity_TOC_sRGB), XCAFDoc_ColorGen);
    // Couleurs par face : la face du prototype est retrouvée par sa géométrie partagée (TShape), placement ignoré
    if (!faces.empty()) {
      std::map<const void*, TopoDS_Shape> parTShape;
      for (TopExp_Explorer ex(forme, TopAbs_FACE); ex.More(); ex.Next()) parTShape.emplace(ex.Current().TShape().get(), ex.Current());
      for (const auto& [face, c] : faces) {
        if (face.IsNull()) continue;
        const auto it = parTShape.find(face.TShape().get());
        if (it == parTShape.end()) continue;
        const TDF_Label sl = formes->AddSubShape(l, it->second);
        if (!sl.IsNull()) couleurs->SetColor(sl, Quantity_Color(c[0], c[1], c[2], Quantity_TOC_sRGB), XCAFDoc_ColorSurf);
      }
    }
    return l;
  }
  TDF_Label assemblage(const std::string& nom) {
    const TDF_Label l = formes->NewShape();
    nommer(l, nom);
    return l;
  }
  void instance(const TDF_Label& parent, const TDF_Label& proto, const gp_Trsf& placement, const std::string& nom) {
    const TDF_Label c = formes->AddComponent(parent, proto, TopLoc_Location(placement));
    if (!c.IsNull()) nommer(c, nom);
  }
  // Écrit le fichier dans le schéma demandé, avec un en-tête qui nomme le logiciel, puis code les caractères non
  // ASCII selon l'ISO 10303-21 (\X2\…\X0\) ; le fichier final est remplacé d'un coup (écriture atomique).
  void ecrire(const std::string& chemin, const OptionsStep& o) {
    formes->UpdateAssemblies();
    // Le schéma est lu à la création du modèle de l'écrivain : il est réglé avant, contrôleur STEP initialisé
    STEPControl_Controller::Init();
    const std::string schemaAvant = Interface_Static::CVal("write.step.schema") ? Interface_Static::CVal("write.step.schema") : "AP214IS";
    Interface_Static::SetCVal("write.step.schema", o.schema == SchemaStep::AP242 ? "AP242DIS" : "AP214IS");
    struct Retour { std::string v; ~Retour() { Interface_Static::SetCVal("write.step.schema", v.c_str()); } } retour{schemaAvant};
    STEPCAFControl_Writer w;
    w.SetNameMode(true);
    w.SetColorMode(true);
    if (!w.Transfer(doc, STEPControl_AsIs)) throw std::runtime_error("Écriture STEP impossible : " + chemin);
    APIHeaderSection_MakeHeader entete(w.ChangeWriter().Model());
    const std::filesystem::path p = cheminFichier(chemin);
    entete.SetName(new TCollection_HAsciiString(utf8(p.filename()).c_str()));
    entete.SetOriginatingSystem(new TCollection_HAsciiString("Promethee"));
    entete.SetPreprocessorVersion(new TCollection_HAsciiString("Promethee, Open CASCADE " OCC_VERSION_COMPLETE));
    entete.SetAuthorValue(1, new TCollection_HAsciiString(o.auteur.c_str()));
    entete.SetOrganizationValue(1, new TCollection_HAsciiString(o.organisation.c_str()));
    entete.SetDescriptionValue(1, new TCollection_HAsciiString(o.schema == SchemaStep::AP242 ? "Promethee, STEP AP242" : "Promethee, STEP AP214"));
    // Écrit d'abord à côté (même dossier, même volume), puis code et remplace
    const std::filesystem::path brouillon = p.parent_path() / (p.filename().native() + cheminFichier(".ecriture").native());
    if (w.Write(utf8(brouillon).c_str()) != IFSelect_RetDone) {
      std::error_code ec;
      std::filesystem::remove(brouillon, ec);
      throw std::runtime_error("Écriture STEP impossible : " + chemin);
    }
    std::string texte;
    try { texte = lireFichier(brouillon); } catch (...) {}
    std::error_code ec;
    std::filesystem::remove(brouillon, ec);
    if (texte.empty()) throw std::runtime_error("Écriture STEP impossible : " + chemin);
    ecrireFichierAtomique(p, texteStepAscii(texte));
  }
  // IGES : surfaces découpées (entités 144, lues partout) ou solide B-rep (186), en millimètres, avec noms et
  // couleurs. Les noms IGES sont des chaînes ASCII à compte d'octets : ils sont translittérés.
  void ecrireIges(const std::string& chemin, bool modeBrep) {
    formes->UpdateAssemblies();
    IGESControl_Controller::Init();
    const int modeAvant = Interface_Static::IVal("write.iges.brep.mode");
    const std::string uniteAvant = Interface_Static::CVal("write.iges.unit") ? Interface_Static::CVal("write.iges.unit") : "MM";
    Interface_Static::SetIVal("write.iges.brep.mode", modeBrep ? 1 : 0);
    Interface_Static::SetCVal("write.iges.unit", "MM");
    struct Retour {
      int m; std::string u;
      ~Retour() { Interface_Static::SetIVal("write.iges.brep.mode", m); Interface_Static::SetCVal("write.iges.unit", u.c_str()); }
    } retour{modeAvant, uniteAvant};
    // Noms ramenés à l'ASCII le temps de l'écriture
    TDF_LabelSequence tout;
    formes->GetShapes(tout);
    for (int i = 1; i <= tout.Length(); ++i) {
      Handle(TDataStd_Name) n;
      if (tout.Value(i).FindAttribute(TDataStd_Name::GetID(), n))
        nommer(tout.Value(i), translitterer(TCollection_AsciiString(n->Get()).ToCString()));
    }
    IGESCAFControl_Writer w;
    w.SetNameMode(true);
    w.SetColorMode(true);
    w.SetLayerMode(false);
    if (!w.Transfer(doc) || !w.Write(chemin.c_str())) throw std::runtime_error("Écriture IGES impossible : " + chemin);
  }
};


// Pièces nommées posées dans le document : un prototype seul, ou une racine au nom du fichier et une instance
// placée par pièce (prototypes partagés par forme et par couleur)
void remplir(DocumentStep& d, const std::vector<PieceNommee>& pieces, const std::string& chemin);

}  // namespace prom::xcaf
