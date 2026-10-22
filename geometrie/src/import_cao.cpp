// Prométhée : import structuré des fichiers de CAO (STEP, IGES, BREP, STL). Licence GPL-3.0-only.
#include "promethee/import_cao.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <stdexcept>

#include <BRep_Builder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TColStd_HSequenceOfExtendedString.hxx>
#include <TDF_Label.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_Tool.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_LayerTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include "promethee/assemblage.hpp"
#include "promethee/depot.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/geometrie.hpp"

namespace prom {

namespace {

void silencieux() {
  static const bool fait = [] {
    for (const auto& imprimante : Message::DefaultMessenger()->Printers()) imprimante->SetTraceLevel(Message_Fail);
    return true;
  }();
  (void)fait;
}

std::string hex(const Quantity_Color& c) {
  double r, g, b;
  c.Values(r, g, b, Quantity_TOC_sRGB);  // les fichiers d'échange donnent des couleurs sRGB (EX-15)
  auto octet = [](double v) { return static_cast<int>(std::clamp(v, 0.0, 1.0) * 255 + 0.5); };
  char t[8];
  std::snprintf(t, sizeof t, "#%02X%02X%02X", octet(r), octet(g), octet(b));
  return t;
}

// Lecture d'un document XCAF : chaque prototype simple devient un produit (un par couleur d'instance), chaque
// assemblage un nœud, chaque composant une instance placée
class LecteurXcaf {
 public:
  LecteurXcaf(const Handle(TDocStd_Document)& doc, ImportCAO& sortie)
      : m_formes(XCAFDoc_DocumentTool::ShapeTool(doc->Main())),
        m_couleurs(XCAFDoc_DocumentTool::ColorTool(doc->Main())),
        m_calques(XCAFDoc_DocumentTool::LayerTool(doc->Main())),
        m_sortie(sortie) {}

  void lire(const std::string& nomFichier) {
    TDF_LabelSequence libres;
    m_formes->GetFreeShapes(libres);
    NoeudImporte& racine = m_sortie.racine;
    racine.nom = nomFichier;
    racine.produit = -1;
    // Une seule forme libre qui est un assemblage : elle devient la racine (son nom l'emporte sur celui du fichier)
    if (libres.Length() == 1 && m_formes->IsAssembly(libres.Value(1))) {
      const std::string n = nom(libres.Value(1));
      if (!n.empty()) racine.nom = n;
      remplir(libres.Value(1), racine, {}, 0);
      return;
    }
    for (int i = 1; i <= libres.Length(); ++i) {
      NoeudImporte enfant;
      enfant.nom = nom(libres.Value(i));
      if (m_formes->IsAssembly(libres.Value(i))) {
        // Placement propre de l'assemblage libre (la forme d'un produit simple garde le sien en elle)
        const TopoDS_Shape s = m_formes->GetShape(libres.Value(i));
        if (!s.Location().IsIdentity()) enfant.placement = s.Location().Transformation();
        remplir(libres.Value(i), enfant, couleur(libres.Value(i)), 1);
        if (enfant.enfants.empty()) continue;
      } else {
        enfant.produit = produit(libres.Value(i), {});
        if (enfant.produit < 0) continue;
      }
      racine.enfants.push_back(std::move(enfant));
    }
  }

 private:
  std::string nom(const TDF_Label& l) const {
    Handle(TDataStd_Name) n;
    if (!l.FindAttribute(TDataStd_Name::GetID(), n)) return {};
    const TCollection_AsciiString a(n->Get());  // UTF-8
    std::string s = a.ToCString();
    // Noms d'instance fabriqués par OpenCascade (« =>[0:1:1:2] ») : sans intérêt
    if (s.rfind("=>", 0) == 0) return {};
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
  }
  std::string couleur(const TDF_Label& l) const {
    Quantity_Color c;
    if (m_couleurs->GetColor(l, XCAFDoc_ColorGen, c) || m_couleurs->GetColor(l, XCAFDoc_ColorSurf, c)) return hex(c);
    return {};
  }
  void remplir(const TDF_Label& assemblage, NoeudImporte& noeud, const std::string& couleurHeritee, int profondeur) {
    if (profondeur > 64) { m_sortie.avertissements.push_back("Arbre d'assemblage trop profond : la suite est ignorée."); return; }
    TDF_LabelSequence composants;
    m_formes->GetComponents(assemblage, composants, false);
    for (int i = 1; i <= composants.Length(); ++i) {
      const TDF_Label c = composants.Value(i);
      TDF_Label ref;
      if (!m_formes->GetReferredShape(c, ref)) continue;
      NoeudImporte enfant;
      enfant.nom = nom(c);
      if (enfant.nom.empty()) enfant.nom = nom(ref);
      enfant.placement = m_formes->GetLocation(c).Transformation();
      std::string coul = couleur(c);
      if (coul.empty()) coul = couleurHeritee;
      if (m_formes->IsAssembly(ref)) {
        const std::string propre = couleur(ref);
        remplir(ref, enfant, coul.empty() ? propre : coul, profondeur + 1);
        if (enfant.enfants.empty()) continue;
      } else {
        enfant.produit = produit(ref, coul);
        if (enfant.produit < 0) continue;
      }
      noeud.enfants.push_back(std::move(enfant));
    }
  }
  // Produit d'un prototype simple, pour une couleur d'instance (vide : la sienne)
  int produit(const TDF_Label& proto, const std::string& couleurInstance) {
    TCollection_AsciiString entree;
    TDF_Tool::Entry(proto, entree);
    const std::string cle = std::string(entree.ToCString()) + "|" + couleurInstance;
    if (const auto it = m_produits.find(cle); it != m_produits.end()) {
      ++m_sortie.produits[static_cast<std::size_t>(it->second)].instances;
      return it->second;
    }
    TopoDS_Shape forme = m_formes->GetShape(proto);
    if (forme.IsNull()) return -1;
    bool matiere = false;
    for (TopExp_Explorer ex(forme, TopAbs_FACE); ex.More() && !matiere; ex.Next()) matiere = true;
    if (!matiere) for (TopExp_Explorer ex(forme, TopAbs_EDGE); ex.More() && !matiere; ex.Next()) matiere = true;
    if (!matiere) return -1;
    ProduitImporte p;
    p.nom = nom(proto);
    p.forme = forme;
    p.instances = 1;
    const std::string propre = couleur(proto);
    p.couleur = couleurInstance.empty() ? propre : couleurInstance;
    // Couleurs des sous-formes (faces, coques, solides) : reportées sur les faces, rangées dans l'ordre d'exploration
    std::map<const void*, std::string> parFace;
    TDF_LabelSequence sous;
    m_formes->GetSubShapes(proto, sous);
    for (int i = 1; i <= sous.Length(); ++i) {
      const std::string c = couleur(sous.Value(i));
      if (c.empty()) continue;
      const TopoDS_Shape s = m_formes->GetShape(sous.Value(i));
      if (s.IsNull()) continue;
      if (s.ShapeType() == TopAbs_FACE) parFace[s.TShape().get()] = c;
      else for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) parFace.emplace(ex.Current().TShape().get(), c);
    }
    if (!parFace.empty()) {
      int k = 0;
      for (TopExp_Explorer ex(forme, TopAbs_FACE); ex.More(); ex.Next(), ++k)
        if (const auto it = parFace.find(ex.Current().TShape().get()); it != parFace.end() && it->second != p.couleur) p.couleursFaces[k] = it->second;
      // Sans couleur propre, la couleur la plus fréquente des faces devient celle du produit
      if (p.couleur.empty()) {
        std::map<std::string, int> compte;
        for (const auto& [rang, c] : p.couleursFaces) ++compte[c];
        const auto top = std::max_element(compte.begin(), compte.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        if (top != compte.end()) {
          p.couleur = top->first;
          for (auto it = p.couleursFaces.begin(); it != p.couleursFaces.end();) it = it->second == p.couleur ? p.couleursFaces.erase(it) : std::next(it);
        }
      }
    }
    // Calques
    if (!m_calques.IsNull()) {
      Handle(TColStd_HSequenceOfExtendedString) noms = m_calques->GetLayers(proto);
      if (!noms.IsNull())
        for (int i = 1; i <= noms->Length(); ++i) p.calques.emplace_back(TCollection_AsciiString(noms->Value(i)).ToCString());
    }
    m_sortie.produits.push_back(std::move(p));
    const int indice = static_cast<int>(m_sortie.produits.size()) - 1;
    m_produits[cle] = indice;
    return indice;
  }

  Handle(XCAFDoc_ShapeTool) m_formes;
  Handle(XCAFDoc_ColorTool) m_couleurs;
  Handle(XCAFDoc_LayerTool) m_calques;
  ImportCAO& m_sortie;
  std::map<std::string, int> m_produits;
};

void compter(const NoeudImporte& n, int& feuilles, int& profondeur, int niveau) {
  if (!n.assemblage()) { ++feuilles; profondeur = std::max(profondeur, niveau); return; }
  for (const auto& e : n.enfants) compter(e, feuilles, profondeur, niveau + 1);
}

}  // namespace

int ImportCAO::instances() const {
  int f = 0, p = 0;
  compter(racine, f, p, 0);
  return f;
}

int ImportCAO::niveaux() const {
  int f = 0, p = 0;
  compter(racine, f, p, 0);
  return p;
}

ProduitImporte ImportCAO::aplati(const std::string& nom) const {
  ProduitImporte r;
  r.nom = nom.empty() ? racine.nom : nom;
  // Couleur dominante : celle de la première instance colorée
  std::function<void(const NoeudImporte&)> premiere = [&](const NoeudImporte& n) {
    if (!r.couleur.empty()) return;
    if (!n.assemblage()) { r.couleur = produits[static_cast<std::size_t>(n.produit)].couleur; return; }
    for (const auto& e : n.enfants) premiere(e);
  };
  premiere(racine);
  BRep_Builder b;
  TopoDS_Compound c;
  b.MakeCompound(c);
  int decalage = 0, morceaux = 0;
  TopoDS_Shape seul;
  std::function<void(const NoeudImporte&, const gp_Trsf&)> poser = [&](const NoeudImporte& n, const gp_Trsf& parent) {
    const gp_Trsf t = parent * n.placement;
    if (n.assemblage()) { for (const auto& e : n.enfants) poser(e, t); return; }
    const ProduitImporte& p = produits[static_cast<std::size_t>(n.produit)];
    const TopoDS_Shape placee = t.Form() == gp_Identity ? p.forme : p.forme.Moved(TopLoc_Location(t));
    b.Add(c, placee);
    seul = placee;
    ++morceaux;
    int k = 0;
    for (TopExp_Explorer ex(placee, TopAbs_FACE); ex.More(); ex.Next(), ++k) {
      std::string coul;
      if (const auto it = p.couleursFaces.find(k); it != p.couleursFaces.end()) coul = it->second;
      else if (!p.couleur.empty() && p.couleur != r.couleur) coul = p.couleur;
      if (!coul.empty()) r.couleursFaces[decalage + k] = coul;
    }
    decalage += k;
    for (const auto& cal : p.calques) if (std::find(r.calques.begin(), r.calques.end(), cal) == r.calques.end()) r.calques.push_back(cal);
  };
  poser(racine, gp_Trsf());
  // Une seule instance : sa forme telle quelle (pas de composé d'un seul élément), les rangs de faces sont les mêmes
  r.forme = morceaux == 1 ? seul : TopoDS_Shape(c);
  r.instances = 1;
  return r;
}

ImportCAO lireCAOStructure(const std::string& chemin) {
  silencieux();
  std::string ext = chemin.substr(chemin.find_last_of('.') + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  ImportCAO r;
  const std::string nomFichier = utf8(cheminFichier(chemin).stem());
  try {
    if (ext == "step" || ext == "stp" || ext == "iges" || ext == "igs") {
      const bool step = ext == "step" || ext == "stp";
      r.format = step ? "STEP" : "IGES";
      Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
      Handle(TDocStd_Document) doc;
      app->NewDocument("MDTV-XCAF", doc);
      struct Fermer { Handle(XCAFApp_Application) a; Handle(TDocStd_Document) d; ~Fermer() { a->Close(d); } } fermer{app, doc};
      bool lu = false;
      if (step) {
        STEPCAFControl_Reader lecteur;
        lecteur.SetNameMode(true);
        lecteur.SetColorMode(true);
        lecteur.SetLayerMode(true);
        if (lecteur.ReadFile(chemin.c_str()) != IFSelect_RetDone) throw std::runtime_error("Fichier STEP illisible.");
        lu = lecteur.Transfer(doc);
      } else {
        IGESCAFControl_Reader lecteur;
        lecteur.SetNameMode(true);
        lecteur.SetColorMode(true);
        lecteur.SetLayerMode(true);
        if (lecteur.ReadFile(chemin.c_str()) != IFSelect_RetDone) throw std::runtime_error("Fichier IGES illisible.");
        lu = lecteur.Transfer(doc);
      }
      if (!lu) throw std::runtime_error("Fichier " + r.format + " illisible.");
      LecteurXcaf(doc, r).lire(nomFichier);
    } else {
      std::string err;
      const std::string brep = lireFichierCAO(chemin, &err);
      if (brep.empty()) throw std::runtime_error(err.empty() ? std::string("Fichier illisible.") : err);
      r.format = ext == "stl" ? "STL" : "BREP";
      ProduitImporte p;
      p.nom = nomFichier;
      p.forme = formeDepuisBrep(brep);
      p.instances = 1;
      r.produits.push_back(std::move(p));
      r.racine.nom = nomFichier;
      NoeudImporte f;
      f.nom = nomFichier;
      f.produit = 0;
      r.racine.enfants.push_back(f);
    }
  } catch (const Standard_Failure& e) {
#if OCC_VERSION_HEX >= 0x080000
    throw std::runtime_error(std::string("Fichier illisible : ") + e.what());
#else
    throw std::runtime_error(std::string("Fichier illisible : ") + e.GetMessageString());
#endif
  }
  if (r.produits.empty() || r.instances() == 0) throw std::runtime_error("Le fichier ne contient aucune géométrie.");
  // Produits sans nom : celui du fichier, numéroté
  int sansNom = 0;
  for (auto& p : r.produits) if (p.nom.empty()) p.nom = nomFichier + (r.produits.size() > 1 ? " " + std::to_string(++sansNom) : std::string());
  std::function<void(NoeudImporte&)> nommer = [&](NoeudImporte& n) {
    if (n.nom.empty()) n.nom = n.assemblage() ? nomFichier : r.produits[static_cast<std::size_t>(n.produit)].nom;
    for (auto& e : n.enfants) nommer(e);
  };
  nommer(r.racine);
  return r;
}

Piece pieceDepuisProduit(const ProduitImporte& produit, const std::string& source) {
  Piece pc;
  pc.nom = produit.nom.empty() ? std::string("Pièce importée") : produit.nom;
  FonctionPiece f;
  f.type = TypeFonction::Import;
  f.donnees = deposerGeometrie(brepDepuisForme(produit.forme));  // le texte au dépôt, la pièce n'en garde que la référence (EX-06)
  f.source = source;
  const int id = pc.ajouter(f);
  if (!produit.couleur.empty()) pc.couleur = produit.couleur;
  for (const auto& [k, c] : produit.couleursFaces) pc.couleursFaces[std::to_string(id) + ":import." + std::to_string(k)] = c;
  return pc;
}

int ajouterImportAuProjet(Projet& projet, const ImportCAO& import, const std::string& source) {
  std::vector<int> pieces(import.produits.size(), -1);
  for (std::size_t i = 0; i < import.produits.size(); ++i) {
    projet.pieces.push_back(pieceVersJson(pieceDepuisProduit(import.produits[i], source)));
    pieces[i] = static_cast<int>(projet.pieces.size()) - 1;
  }
  std::function<int(const NoeudImporte&)> assembler = [&](const NoeudImporte& n) -> int {
    Assemblage a;
    a.nom = n.nom.empty() ? std::string("Assemblage importé") : n.nom;
    for (const auto& e : n.enfants) {
      ComposantAssemblage c = composantDepuisPlacement(e.placement);
      c.fixe = true;  // la position du fichier fait foi
      if (e.assemblage()) c.assemblage = assembler(e);
      else c.piece = pieces[static_cast<std::size_t>(e.produit)];
      a.composants.push_back(c);
    }
    projet.assemblages.push_back(std::move(a));
    return static_cast<int>(projet.assemblages.size()) - 1;
  };
  return assembler(import.racine);
}

}  // namespace prom
