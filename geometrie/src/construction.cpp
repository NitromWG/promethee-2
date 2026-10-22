// Prométhée : construction des pièces paramétriques et nommage topologique. Licence GPL-3.0-only.
#include "promethee/construction.hpp"
#include "promethee/geometrie.hpp"
#include "mince.hpp"
#include "tolerie_interne.hpp"

#include <algorithm>
#include <cstdio>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <tuple>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <gp_Cylinder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_ListOfStatus.hxx>
#include <BRepCheck_Result.hxx>
#include <GeomAPI_IntCS.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Line.hxx>
#include <Geom_OffsetSurface.hxx>
#include <BRepAdaptor_CompCurve.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepOffsetAPI_MakePipeShell.hxx>
#include <gp.hxx>
#include <IntAna_QuadQuadGeo.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepOffset_MakeOffset.hxx>
#include <gp_Pnt2d.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <Geom_Surface.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <Poly_Triangulation.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <GProp_PrincipalProps.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <ShapeFix_Face.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

namespace prom {

namespace {

using CarteFaces = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

// Volume d'une forme (mm³), 0 si elle n'en a pas
double volumeForme(const TopoDS_Shape& s) {
  GProp_GProps g;
  BRepGProp::VolumeProperties(s, g);
  return g.Mass();
}

// Une coque doit enlever de la matière. Selon la version d'OpenCascade, une épaisseur plus grande que la pièce
// est refusée ou rend un solide valide mais inchangé : les deux cas sont des échecs pour l'utilisateur.
bool coqueSansEffet(const TopoDS_Shape& avant, const TopoDS_Shape& apres) {
  const double va = volumeForme(avant), vb = volumeForme(apres);
  return vb <= 0 || vb >= va * (1 - 1e-9);
}
using CarteAncetres = NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>;
using Noms = std::vector<FaceNommee>;

void silencer() {
  static const bool fait = [] {
    for (const auto& imprimante : Message::DefaultMessenger()->Printers()) imprimante->SetTraceLevel(Message_Fail);
    return true;
  }();
  (void)fait;
}

gp_Pnt vers3D(const gp_Ax3& ax, double u, double v) {
  return ax.Location().Translated(gp_Vec(ax.XDirection()) * u + gp_Vec(ax.YDirection()) * v);
}

gp_Pnt milieuArete(const TopoDS_Edge& e) {
  BRepAdaptor_Curve c(e);
  return c.Value((c.FirstParameter() + c.LastParameter()) / 2);
}

gp_Pnt centreFace(const TopoDS_Shape& f) {
  GProp_GProps g;
  BRepGProp::SurfaceProperties(f, g);
  return g.CentreOfMass();
}

bool avant(const gp_Pnt& a, const gp_Pnt& b) {
  const auto r = [](double v) { return std::llround(v * 1e5); };
  return std::make_tuple(r(a.X()), r(a.Y()), r(a.Z())) < std::make_tuple(r(b.X()), r(b.Y()), r(b.Z()));
}

// ---------- Profil d'une esquisse : faces planes, et origine de chacune de leurs arêtes ----------
struct Profil {
  std::vector<TopoDS_Face> faces;
  std::vector<std::string> cles;                     // clé de chaque région : éléments qui la bordent
  std::vector<bool> parDefaut;                       // région retenue quand on n'a rien choisi (pair-impair)
  std::vector<gp_Pnt2d> interieurs;                  // point intérieur de chaque région (repère de l'esquisse)
  std::vector<std::pair<TopoDS_Edge, int>> origine;  // arête d'une face -> élément d'esquisse
  TopoDS_Shape trace;                                // arêtes du profil (affichage)
  TopoDS_Shape traceConstruction;                    // arêtes de construction, affichées en trait d'axe
  std::string erreur;
};

// Point strictement intérieur d'une face plane, en paramètres (u, v). La face est maillée et le centre du plus
// grand triangle est retenu : contrairement à une grille de points d'essai, la méthode trouve aussi l'intérieur
// des profils à paroi mince (cornière, U, tube) dont toute la matière longe le bord de la boîte englobante (FO-01).
gp_Pnt2d pointInterieur(const TopoDS_Face& fc) {
  double fu0, fu1, fv0, fv1;
  BRepTools::UVBounds(fc, fu0, fu1, fv0, fv1);
  gp_Pnt2d dedans((fu0 + fu1) / 2, (fv0 + fv1) / 2);
  try {
    const TopoDS_Face copie = TopoDS::Face(BRepBuilderAPI_Copy(fc).Shape());
    const double diag = std::max(1e-6, std::hypot(fu1 - fu0, fv1 - fv0));
    BRepMesh_IncrementalMesh maillage(copie, diag * 1e-3, false, 0.2);
    TopLoc_Location loc;
    const Handle(Poly_Triangulation) tr = BRep_Tool::Triangulation(copie, loc);
    if (!tr.IsNull() && tr->HasUVNodes()) {
      double meilleure = -1;
      for (int k = 1; k <= tr->NbTriangles(); ++k) {
        int a, b, c;
        tr->Triangle(k).Get(a, b, c);
        const gp_Pnt2d pa = tr->UVNode(a), pb = tr->UVNode(b), pc = tr->UVNode(c);
        const double aire = std::abs((pb.X() - pa.X()) * (pc.Y() - pa.Y()) - (pc.X() - pa.X()) * (pb.Y() - pa.Y()));
        if (aire <= meilleure) continue;
        const gp_Pnt2d g((pa.X() + pb.X() + pc.X()) / 3, (pa.Y() + pb.Y() + pc.Y()) / 3);
        BRepClass_FaceClassifier cl(fc, g, 1e-9);
        if (cl.State() == TopAbs_IN) { meilleure = aire; dedans = g; }
      }
      if (meilleure > 0) return dedans;
    }
  } catch (const Standard_Failure&) {
    // repli ci-dessous
  }
  for (int i = 1; i < 40; ++i)
    for (int j = 1; j < 40; ++j) {
      const gp_Pnt2d q(fu0 + (fu1 - fu0) * (i + 0.37) / 40.6, fv0 + (fv1 - fv0) * (j + 0.41) / 40.6);
      BRepClass_FaceClassifier cl(fc, q, 1e-9);
      if (cl.State() == TopAbs_IN) return q;
    }
  return dedans;
}

Profil profil(const FonctionPiece& f, const gp_Ax3& ax) {
  Profil r;
  const Esquisse& e = f.esquisse;
  auto P = [&](int id) { const PointEsquisse* p = e.point(id); return p ? vers3D(ax, p->x, p->y) : ax.Location(); };
  std::map<int, TopoDS_Edge> aretes;
  BRep_Builder bb;
  TopoDS_Compound trace, traceC;
  bb.MakeCompound(trace);
  bb.MakeCompound(traceC);
  bool aConstruction = false;
  for (const auto& el : e.elements) {
    TopoDS_Edge ed;
    try {
      if (el.type == TypeElement::Ligne) {
        if (P(el.p1).Distance(P(el.p2)) < 1e-7) continue;
        ed = BRepBuilderAPI_MakeEdge(P(el.p1), P(el.p2)).Edge();
      } else if (el.type == TypeElement::Cercle) {
        if (el.rayon < 1e-7) continue;
        ed = BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(P(el.centre), ax.Direction(), ax.XDirection()), el.rayon)).Edge();
      } else {
        const gp_Pnt c = P(el.centre), a = P(el.p1), b = P(el.p2);
        const double rr = c.Distance(a);
        if (rr < 1e-7 || a.Distance(b) < 1e-7) continue;
        const gp_Circ cercle(gp_Ax2(c, ax.Direction(), gp_Dir(gp_Vec(c, a))), rr);
        ed = BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(cercle, a, b, true).Value()).Edge();
      }
    } catch (const Standard_Failure&) {
      continue;
    }
    aretes[el.id] = ed;
    if (el.construction) { bb.Add(traceC, ed); aConstruction = true; }
    else bb.Add(trace, ed);
  }
  r.trace = trace;
  if (aConstruction) r.traceConstruction = traceC;
  // Régions de l'esquisse : on découpe un grand rectangle du plan par toutes les courbes (hors construction),
  // comme le font les grands logiciels ; chaque morceau fermé est une région qu'on peut choisir d'un clic,
  // même quand les contours se croisent. Par défaut, règle pair-impair : une région entourée un nombre impair
  // de fois est de la matière (un cercle dans un rectangle fait un trou).
  std::map<int, TopoDS_Edge> utiles;
  for (const auto& el : e.elements)
    if (!el.construction && aretes.count(el.id)) utiles[el.id] = aretes[el.id];
  if (utiles.empty()) { r.erreur = "L'esquisse ne contient aucun profil fermé."; return r; }
  double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
  auto etendre = [&](double u, double v) { u0 = std::min(u0, u); u1 = std::max(u1, u); v0 = std::min(v0, v); v1 = std::max(v1, v); };
  for (const auto& p : e.points) etendre(p.x, p.y);
  // Les cercles et les arcs entiers : un arc dépasse souvent ses extrémités (le « ventre » d'une rainure en arc).
  // Le cadre doit tout englober, sinon les régions qu'il couperait passeraient pour l'extérieur et seraient perdues.
  for (const auto& el : e.elements)
    if (el.type != TypeElement::Ligne)
      if (const PointEsquisse* c = e.point(el.centre)) {
        const double rr = e.rayonDe(el);
        etendre(c->x - rr, c->y - rr);
        etendre(c->x + rr, c->y + rr);
      }
  const double marge = std::max(5.0, 0.2 * std::hypot(u1 - u0, v1 - v0));
  const TopoDS_Face cadre = BRepBuilderAPI_MakeFace(gp_Pln(ax), u0 - marge, u1 + marge, v0 - marge, v1 + marge).Face();
  BRepAlgoAPI_Splitter decoupe;
  NCollection_List<TopoDS_Shape> arguments, outils;
  arguments.Append(cadre);
  for (const auto& [id, ed] : utiles) outils.Append(ed);
  decoupe.SetArguments(arguments);
  decoupe.SetTools(outils);
  decoupe.Build();
  if (!decoupe.IsDone() || decoupe.HasErrors()) { r.erreur = "Les courbes de l'esquisse n'ont pas pu être découpées en régions."; return r; }
  // Morceaux d'arêtes : de quel élément viennent-ils ? Et lesquels forment le bord du cadre ?
  std::vector<std::pair<TopoDS_Shape, int>> morceaux;
  for (const auto& [id, ed] : utiles) {
    const NCollection_List<TopoDS_Shape>& m = decoupe.Modified(ed);
    if (m.IsEmpty()) morceaux.push_back({ed, id});
    for (const TopoDS_Shape& x : m) morceaux.push_back({x, id});
  }
  std::vector<TopoDS_Shape> bordCadre;
  for (TopExp_Explorer ex(cadre, TopAbs_EDGE); ex.More(); ex.Next()) {
    const NCollection_List<TopoDS_Shape>& m = decoupe.Modified(ex.Current());
    if (m.IsEmpty()) bordCadre.push_back(ex.Current());
    for (const TopoDS_Shape& x : m) bordCadre.push_back(x);
  }
  auto elementDe = [&](const TopoDS_Shape& ed) {
    for (const auto& [x, id] : morceaux) if (x.IsSame(ed)) return id;
    return 0;
  };
  // Matière par défaut : profondeur d'imbrication de chaque région dans le graphe d'adjacence des régions
  // (deux régions sont voisines si elles partagent un morceau de courbe). L'extérieur vaut 0, une région qui le
  // touche 1, une région qui ne touche que des régions de profondeur 1 vaut 2, etc. ; une profondeur impaire est
  // de la matière (un cercle dans un rectangle fait un trou). Contrairement au comptage de croisements d'un rayon,
  // une courbe ouverte (axe tracé en trait plein, trait isolé, ligne qui traverse un contour) ne change rien :
  // elle ne sépare aucune région de l'extérieur (FO-03).
  std::vector<TopoDS_Face> toutes;
  for (TopExp_Explorer ex(decoupe.Shape(), TopAbs_FACE); ex.More(); ex.Next()) toutes.push_back(TopoDS::Face(ex.Current()));
  auto indiceFace = [&](const TopoDS_Shape& f) {
    for (size_t i = 0; i < toutes.size(); ++i) if (toutes[i].IsSame(f)) return static_cast<int>(i);
    return -1;
  };
  std::vector<int> profondeur(toutes.size(), -1);
  std::vector<std::vector<int>> voisins(toutes.size());
  {
    TopTools_IndexedDataMapOfShapeListOfShape aretesFaces;
    TopExp::MapShapesAndAncestors(decoupe.Shape(), TopAbs_EDGE, TopAbs_FACE, aretesFaces);
    for (int k = 1; k <= aretesFaces.Extent(); ++k) {
      std::vector<int> fs;
      for (const TopoDS_Shape& f : aretesFaces.FindFromIndex(k)) {
        const int i = indiceFace(f);
        if (i >= 0 && std::find(fs.begin(), fs.end(), i) == fs.end()) fs.push_back(i);
      }
      for (int x : fs) for (int y : fs) if (x != y) voisins[static_cast<size_t>(x)].push_back(y);
    }
    std::vector<int> file;
    for (size_t i = 0; i < toutes.size(); ++i)
      for (TopExp_Explorer ee(toutes[i], TopAbs_EDGE); ee.More(); ee.Next())
        if (std::any_of(bordCadre.begin(), bordCadre.end(), [&](const TopoDS_Shape& b) { return b.IsSame(ee.Current()); })) {
          profondeur[i] = 0;
          file.push_back(static_cast<int>(i));
          break;
        }
    for (size_t q = 0; q < file.size(); ++q)
      for (int v : voisins[static_cast<size_t>(file[q])])
        if (profondeur[static_cast<size_t>(v)] < 0) {
          profondeur[static_cast<size_t>(v)] = profondeur[static_cast<size_t>(file[q])] + 1;
          file.push_back(v);
        }
  }
  struct R { TopoDS_Face face; std::vector<int> ids; gp_Pnt2d interieur; bool defaut; };
  std::vector<R> regions;
  for (TopExp_Explorer ex(decoupe.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
    TopoDS_Face fc = TopoDS::Face(ex.Current());
    bool exterieur = false;
    std::vector<int> ids;
    bool interne = false;
    for (TopExp_Explorer ee(fc, TopAbs_EDGE); ee.More(); ee.Next()) {
      if (std::any_of(bordCadre.begin(), bordCadre.end(), [&](const TopoDS_Shape& b) { return b.IsSame(ee.Current()); })) exterieur = true;
      if (ee.Current().Orientation() == TopAbs_INTERNAL || ee.Current().Orientation() == TopAbs_EXTERNAL) interne = true;
      else if (const int id = elementDe(ee.Current())) ids.push_back(id);
    }
    if (exterieur || ids.empty()) continue;
    if (interne) {
      // Un bout de courbe qui ne ferme rien reste dans la face : on le retire.
      BRepBuilderAPI_MakeFace mf(BRep_Tool::Surface(fc), BRepTools::OuterWire(fc));
      for (TopExp_Explorer ew(fc, TopAbs_WIRE); ew.More(); ew.Next()) {
        const TopoDS_Wire w = TopoDS::Wire(ew.Current());
        if (w.IsSame(BRepTools::OuterWire(fc))) continue;
        bool garde = true;
        for (TopExp_Explorer ee(w, TopAbs_EDGE); ee.More(); ee.Next())
          if (ee.Current().Orientation() == TopAbs_INTERNAL || ee.Current().Orientation() == TopAbs_EXTERNAL) garde = false;
        if (garde) mf.Add(w);
      }
      if (mf.IsDone()) fc = mf.Face();
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    // Point intérieur de la région, pour départager les clés et retrouver une région (voir pointInterieur).
    const gp_Pnt2d dedans = pointInterieur(fc);
    // Paramètres (u, v) de la face plane = coordonnées dans le repère de l'esquisse.
    const gp_Pnt q3 = BRep_Tool::Surface(fc)->Value(dedans.X(), dedans.Y());
    const gp_Vec w(ax.Location(), q3);
    const double qu = w.Dot(gp_Vec(ax.XDirection())), qv = w.Dot(gp_Vec(ax.YDirection()));
    const int iF = indiceFace(ex.Current());
    const int prof = iF >= 0 && profondeur[static_cast<size_t>(iF)] > 0 ? profondeur[static_cast<size_t>(iF)] : 1;
    regions.push_back({fc, ids, gp_Pnt2d(qu, qv), prof % 2 == 1});
  }
  if (regions.empty()) { r.erreur = "L'esquisse ne contient aucun contour fermé : vérifie que ses éléments se rejoignent."; return r; }
  // Clé de chaque région : les éléments qui la bordent ; à égalité, la position départage.
  std::sort(regions.begin(), regions.end(), [](const R& a, const R& b) {
    if (a.ids != b.ids) return a.ids < b.ids;
    return std::make_pair(std::llround(a.interieur.X() * 1e4), std::llround(a.interieur.Y() * 1e4)) <
           std::make_pair(std::llround(b.interieur.X() * 1e4), std::llround(b.interieur.Y() * 1e4));
  });
  for (size_t i = 0; i < regions.size(); ++i) {
    std::string cle = "r:";
    for (size_t k = 0; k < regions[i].ids.size(); ++k) cle += (k ? "," : "") + std::to_string(regions[i].ids[k]);
    int rang = 0;
    for (size_t j = 0; j < i; ++j) if (regions[j].ids == regions[i].ids) ++rang;
    if (rang) cle += "#" + std::to_string(rang);
    r.faces.push_back(regions[i].face);
    r.cles.push_back(cle);
    r.parDefaut.push_back(regions[i].defaut);
    r.interieurs.push_back(regions[i].interieur);
    for (TopExp_Explorer ee(regions[i].face, TopAbs_EDGE); ee.More(); ee.Next())
      if (const int id = elementDe(ee.Current())) r.origine.push_back({TopoDS::Edge(ee.Current()), id});
  }
  return r;
}

// Fait suivre les noms des faces à travers une opération qui garde un historique (booléen, congé, coque…).
template <class Op>
Noms suivre(Op& op, const Noms& anciens, const TopoDS_Shape& resultat) {
  CarteFaces faces;
  TopExp::MapShapes(resultat, TopAbs_FACE, faces);
  Noms nouveaux;
  for (const auto& [f, nom] : anciens) {
    if (op.IsDeleted(f)) continue;
    const NCollection_List<TopoDS_Shape>& mods = op.Modified(f);
    if (mods.IsEmpty()) {
      if (faces.Contains(f)) nouveaux.push_back({f, nom});
      continue;
    }
    for (const TopoDS_Shape& m : mods)
      if (m.ShapeType() == TopAbs_FACE && faces.Contains(m)) nouveaux.push_back({TopoDS::Face(m), nom});
  }
  return nouveaux;
}

// Les faces restées sans nom (rares) en reçoivent un, rattaché à la fonction qui les a créées.
void nommerReste(const TopoDS_Shape& forme, Noms& noms, int fonction) {
  std::vector<TopoDS_Face> sansNom;
  for (TopExp_Explorer ex(forme, TopAbs_FACE); ex.More(); ex.Next()) {
    const TopoDS_Face f = TopoDS::Face(ex.Current());
    if (std::none_of(noms.begin(), noms.end(), [&](const FaceNommee& n) { return n.face.IsSame(f); })) sansNom.push_back(f);
  }
  std::sort(sansNom.begin(), sansNom.end(), [](const TopoDS_Face& a, const TopoDS_Face& b) { return avant(centreFace(a), centreFace(b)); });
  // « F:face.i:n » : i-ième (par position) des n faces sans origine. Si n change, les références à ces noms
  // cassent explicitement au lieu de glisser sur une autre face.
  for (size_t i = 0; i < sansNom.size(); ++i)
    noms.push_back({sansNom[i], std::to_string(fonction) + ":face." + std::to_string(i) + ":" + std::to_string(sansNom.size())});
}

TopoDS_Shape solideUnique(const TopoDS_Shape& s) {
  int n = 0;
  TopoDS_Shape dernier;
  for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) { ++n; dernier = ex.Current(); }
  return n == 1 ? dernier : s;
}

double diagonale(const TopoDS_Shape& s) {
  if (s.IsNull()) return 100;
  Bnd_Box b;
  BRepBndLib::Add(s, b);
  if (b.IsVoid()) return 100;
  return std::sqrt(b.SquareExtent());
}

std::string cleArete(const std::vector<std::string>& nomsFaces) {
  std::vector<std::string> n = nomsFaces;
  std::sort(n.begin(), n.end());
  n.erase(std::unique(n.begin(), n.end()), n.end());
  if (n.empty()) return {};
  return n.size() == 1 ? n[0] + "|" + n[0] : n[0] + "|" + n[1];
}

// Arêtes du solide groupées par paire de faces nommées, triées par position (pour l'indice #k).
std::map<std::string, std::vector<TopoDS_Edge>> aretesParCle(const TopoDS_Shape& forme, const Noms& noms) {
  std::map<std::string, std::vector<TopoDS_Edge>> res;
  if (forme.IsNull()) return res;
  CarteAncetres anc;
  TopExp::MapShapesAndAncestors(forme, TopAbs_EDGE, TopAbs_FACE, anc);
  for (int i = 1; i <= anc.Extent(); ++i) {
    std::vector<std::string> n;
    for (const TopoDS_Shape& f : anc.FindFromIndex(i))
      for (const auto& x : noms) if (x.face.IsSame(f)) { n.push_back(x.nom); break; }
    const std::string cle = cleArete(n);
    if (!cle.empty()) res[cle].push_back(TopoDS::Edge(anc.FindKey(i)));
  }
  for (auto& [cle, l] : res)
    std::sort(l.begin(), l.end(), [](const TopoDS_Edge& a, const TopoDS_Edge& b) { return avant(milieuArete(a), milieuArete(b)); });
  return res;
}

// Référence « nom », « nom#k » (ancien format) ou « nom#k/n » : k-ième morceau sur n.
struct RefIndicee {
  std::string nom;
  int k = -1, n = -1;
};
RefIndicee separerIndice(const std::string& ref) {
  RefIndicee r{ref, -1, -1};
  const size_t p = ref.rfind('#');
  if (p == std::string::npos) return r;
  const std::string suite = ref.substr(p + 1);
  const size_t barre = suite.find('/');
  try {
    r.k = std::stoi(suite.substr(0, barre));
    if (barre != std::string::npos) r.n = std::stoi(suite.substr(barre + 1));
    r.nom = ref.substr(0, p);
  } catch (...) {
    return {ref, -1, -1};
  }
  return r;
}

// Choix strict parmi les candidats portant le nom de la référence (déjà rangés par position).
template <class T>
ResolutionReference choisir(const RefIndicee& ri, const std::vector<T>& candidats, bool unique, std::vector<T>& sortie) {
  ResolutionReference res;
  sortie.clear();
  const int n = static_cast<int>(candidats.size());
  if (n == 0) {
    res.etat = EtatReference::Introuvable;
    res.message = "« " + ri.nom + " » n'existe plus (supprimé ou transformé par une modification en amont)";
    return res;
  }
  if (ri.k >= 0) {
    if (ri.n >= 0 && n != ri.n) {
      res.etat = EtatReference::Ambigue;
      res.message = "« " + ri.nom + " » était en " + std::to_string(ri.n) + " morceaux et en compte maintenant " + std::to_string(n) +
                    " : le morceau choisi ne peut plus être retrouvé avec certitude";
      return res;
    }
    if (ri.k >= n) {
      res.etat = EtatReference::Introuvable;
      res.message = "le morceau " + std::to_string(ri.k + 1) + " de « " + ri.nom + " » n'existe plus";
      return res;
    }
    sortie = {candidats[static_cast<size_t>(ri.k)]};
    res.etat = EtatReference::Trouvee;
    return res;
  }
  if (unique && n > 1) {
    res.etat = EtatReference::Ambigue;
    res.message = "« " + ri.nom + " » est maintenant coupé en " + std::to_string(n) + " morceaux : il faut choisir lequel";
    return res;
  }
  sortie = candidats;
  res.etat = EtatReference::Trouvee;
  return res;
}

std::string avecIndice(const std::string& nom, std::size_t k, std::size_t n) {
  return n <= 1 ? nom : nom + "#" + std::to_string(k) + "/" + std::to_string(n);
}


// Longueur utile dans la direction d depuis O pour sortir de toute la pièce : plus grande avancée d'un coin de sa
// boîte, plus une marge. « À travers tout » part ainsi du plan d'esquisse quel que soit son éloignement (FO-15).
double longueurUtile(const TopoDS_Shape& corps, const gp_Pnt& O, const gp_Dir& d) {
  if (corps.IsNull()) return 0;
  Bnd_Box b;
  BRepBndLib::Add(corps, b);
  if (b.IsVoid()) return 0;
  double x0, y0, z0, x1, y1, z1;
  b.Get(x0, y0, z0, x1, y1, z1);
  double m = -1e300;
  for (double x : {x0, x1})
    for (double y : {y0, y1})
      for (double z : {z0, z1}) m = std::max(m, gp_Vec(O, gp_Pnt(x, y, z)).Dot(gp_Vec(d)));
  return m + 1 + 1e-3 * std::sqrt(b.SquareExtent());
}

// Nom de face sans les caractères qui ont un sens dans une référence (« | » sépare les faces d'une arête, « # »
// indique un morceau) : sert à nommer par identité une face tirée d'une autre référence (FO-05).
std::string jetonNom(const std::string& s) {
  std::string r;
  for (char c : s) r += c == '|' ? '+' : c == '#' ? '~' : c;
  return r;
}
// Clé de région « r:3,5#1 » → « 3_5~1 »
std::string jetonRegion(const std::string& cle) {
  std::string r;
  for (std::size_t i = cle.rfind("r:", 0) == 0 ? 2 : 0; i < cle.size(); ++i) r += cle[i] == ',' ? '_' : cle[i] == '#' ? '~' : cle[i];
  return r;
}

struct Outil {
  TopoDS_Shape forme;
  Noms noms;
  std::string erreur;
};

// Prisme d'une face dans un sens (signe ±1 le long de la normale de l'esquisse), avec sa condition de fin.
Outil prisme(const TopoDS_Face& face, const std::vector<std::pair<TopoDS_Edge, int>>& origine, const gp_Ax3& ax, double signe, const std::string& condition,
             double longueur, const std::string& ref, double decalage, double depouille, bool enlever, const TopoDS_Shape& corps, const Noms& nomsCorps,
             const std::string& id, const std::string& suffixe) {
  Outil r;
  const gp_Dir d = signe > 0 ? ax.Direction() : ax.Direction().Reversed();
  const gp_Pnt O = ax.Location();
  const double grand = 2 * diagonale(corps) + 4 * diagonale(face) + 50;
  ResultatPiece etat;
  etat.forme = corps;
  etat.faces = nomsCorps;
  double L = longueur;
  bool rogner = false;
  TopoDS_Face bornante;
  gp_Pnt auDela;
  bool prochain = false;
  if (condition == "traversant") {
    // Rien devant (seconde direction d'un « à travers tout » des deux côtés…) : un prisme court, sans effet
    L = corps.IsNull() ? grand : std::max(1.0, longueurUtile(corps, O, d));
  }
  else if (condition == "sommet") {
    TopoDS_Vertex v;
    if (ref.empty()) { r.erreur = "Choisis le sommet où s'arrêter (clic sur un sommet de la pièce)."; return r; }
    if (const ResolutionReference rs = etat.resoudreSommet(ref, v); !rs.trouvee()) { r.erreur = "Référence cassée : " + rs.message + ". Choisis à nouveau le sommet d'arrêt."; return r; }
    L = gp_Vec(O, BRep_Tool::Pnt(v)).Dot(gp_Vec(d));
    if (L <= 1e-6) { r.erreur = "Le sommet choisi est derrière le plan d'esquisse : inverse le sens."; return r; }
  } else if (condition == "face" || condition == "decale") {
    std::vector<TopoDS_Face> fs;
    if (ref.empty()) { r.erreur = "Choisis la face où s'arrêter (clic sur une face de la pièce)."; return r; }
    if (const ResolutionReference rs = etat.resoudreFaces(ref, fs, true); !rs.trouvee()) { r.erreur = "Référence cassée : " + rs.message + ". Choisis à nouveau la face d'arrêt."; return r; }
    BRepAdaptor_Surface s(fs.front());
    if (s.GetType() == GeomAbs_Plane) {
      gp_Pln pl = s.Plane();
      gp_Dir m = pl.Axis().Direction();
      if (gp_Vec(m).Dot(gp_Vec(d)) < 0) m.Reverse();
      gp_Pnt p0 = pl.Location();
      if (condition == "decale") p0.Translate(gp_Vec(m) * decalage);
      const double cosinus = gp_Vec(m).Dot(gp_Vec(d));
      if (std::abs(cosinus) < 1e-9) { r.erreur = "La face choisie est parallèle au sens d'extrusion : elle ne peut pas l'arrêter."; return r; }
      if (cosinus > 1 - 1e-9) {
        L = gp_Vec(O, p0).Dot(gp_Vec(m)) / cosinus;
      } else {
        // Face inclinée : on prolonge, puis on rogne exactement par son plan.
        L = grand;
        rogner = true;
        bornante = BRepBuilderAPI_MakeFace(gp_Pln(p0, m), -grand, grand, -grand, grand).Face();
        auDela = p0.Translated(gp_Vec(m) * grand);
      }
      if (!rogner && L <= 1e-6) { r.erreur = "La face choisie est derrière le plan d'esquisse : inverse le sens."; return r; }
    } else {
      // Face courbe : la fonction épouse sa surface (ou la surface décalée de « décalage » le long de sa normale,
      // orientée dans le sens de l'extrusion), prolongée au-delà de ses bords.
      L = grand;
      rogner = true;
      Handle(Geom_Surface) surf = BRep_Tool::Surface(fs.front());
      // Repère du profil : son centre, d'où part la ligne d'extrusion qui doit croiser la face
      GProp_GProps gprofil;
      BRepGProp::SurfaceProperties(face, gprofil);
      const gp_Pnt centreProfil = gprofil.CentreOfMass();
      if (condition == "decale" && std::abs(decalage) > 1e-9) {
        GeomAPI_ProjectPointOnSurf proj(centreProfil, surf);
        if (!proj.IsDone() || proj.NbPoints() == 0) { r.erreur = "Décalage impossible : la face choisie n'est pas atteinte depuis l'esquisse."; return r; }
        double u = 0, v = 0;
        proj.LowerDistanceParameters(u, v);
        gp_Pnt pp;
        gp_Vec du, dv;
        surf->D1(u, v, pp, du, dv);
        const gp_Vec normale = du.Crossed(dv);
        if (normale.Magnitude() < 1e-12) { r.erreur = "Décalage impossible : la face choisie est dégénérée à cet endroit."; return r; }
        const double cote = normale.Dot(gp_Vec(d)) >= 0 ? 1.0 : -1.0;
        try {
          surf = new Geom_OffsetSurface(surf, cote * decalage);
        } catch (const Standard_Failure&) {
          r.erreur = "Décalage impossible depuis cette face (distance plus grande que son rayon de courbure ?).";
          return r;
        }
      }
      double u0, u1, v0, v1;
      surf->Bounds(u0, u1, v0, v1);
      u0 = std::max(u0, -grand); u1 = std::min(u1, grand); v0 = std::max(v0, -grand); v1 = std::min(v1, grand);
      BRepBuilderAPI_MakeFace mf(surf, u0, u1, v0, v1, 1e-6);
      if (!mf.IsDone()) { r.erreur = "Cette face ne peut pas servir de limite."; return r; }
      bornante = mf.Face();
      // Côté « au-delà » : juste après le premier passage de la ligne d'extrusion à travers la surface. (Le point
      // lointain ne convient pas à une surface fermée, cylindre ou sphère, que la ligne traverse deux fois.)
      Handle(Geom_Line) ligne = new Geom_Line(centreProfil, d);
      GeomAPI_IntCS inter(ligne, surf);
      double premier = -1;
      if (inter.IsDone())
        for (int i = 1; i <= inter.NbPoints(); ++i) {
          double uu = 0, vv = 0, t = 0;
          inter.Parameters(i, uu, vv, t);
          if (t > 1e-7 && (premier < 0 || t < premier)) premier = t;
        }
      if (premier < 0) { r.erreur = "La face choisie n'est pas sur le chemin de l'extrusion (vérifie le sens)."; return r; }
      auDela = centreProfil.Translated(gp_Vec(d) * (premier + std::min(0.01, premier / 2)));
    }
  } else if (condition == "prochain") {
    // Prisme qui sort de toute la pièce, puis réduit à ce qu'il rencontre en premier (voir plus bas, FO-07)
    if (corps.IsNull()) { r.erreur = "« Jusqu'au prochain » demande de la matière déjà présente."; return r; }
    L = longueurUtile(corps, O, d);
    if (L <= 1e-6) { r.erreur = "Aucune matière rencontrée dans ce sens."; return r; }
    prochain = true;
  }
  // Dépouille jusqu'à une face inclinée ou courbe : le prisme ne va pas plus loin que la pièce, sans quoi ses flancs
  // dépouillés se croiseraient bien avant la face (FO-12)
  if (rogner && std::abs(depouille) > 1e-9 && !corps.IsNull()) L = std::max(1.0, longueurUtile(corps, O, d));
  BRepPrimAPI_MakePrism mk(face, gp_Vec(d) * L, true);
  if (!mk.IsDone()) { r.erreur = "L'extrusion a échoué."; return r; }
  r.forme = mk.Shape();
  std::vector<TopoDS_Face> flancs;
  for (const auto& [ed, el] : origine)
    for (const TopoDS_Shape& gf : mk.Generated(ed))
      if (gf.ShapeType() == TopAbs_FACE) { r.noms.push_back({TopoDS::Face(gf), id + ":cote:" + std::to_string(el) + suffixe}); flancs.push_back(TopoDS::Face(gf)); }
  // Flancs sans origine connue (fonction mince) : nommés dans l'ordre.
  int k = 0;
  for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
    bool connu = false;
    for (const auto& [ed, el] : origine) if (ed.IsSame(ex.Current())) connu = true;
    if (connu) continue;
    for (const TopoDS_Shape& gf : mk.Generated(ex.Current()))
      if (gf.ShapeType() == TopAbs_FACE) { r.noms.push_back({TopoDS::Face(gf), id + ":paroi." + std::to_string(k++) + suffixe}); flancs.push_back(TopoDS::Face(gf)); }
  }
  r.noms.push_back({TopoDS::Face(mk.FirstShape()), id + ":base" + suffixe});
  r.noms.push_back({TopoDS::Face(mk.LastShape()), id + ":sommet" + suffixe});
  if (std::abs(depouille) > 1e-9) {
    BRepOffsetAPI_DraftAngle dr(r.forme);
    const gp_Pln neutre(O, d);
    for (const auto& fl : flancs) {
      dr.Add(fl, d, depouille * std::numbers::pi / 180, neutre);
      if (!dr.AddDone()) { r.erreur = "Dépouille impossible sur ce contour (angle trop fort ou face non réglée)."; return r; }
    }
    dr.Build();
    if (!dr.IsDone()) { r.erreur = "La dépouille a échoué."; return r; }
    const TopoDS_Shape s = dr.Shape();
    r.noms = suivre(dr, r.noms, s);
    r.forme = s;
  }
  if (prochain) {
    // Enlèvement : la première couche de matière traversée, exactement (le commun du prisme et de la pièce, réduit
    // au morceau le plus proche du profil). Ajout : le prisme jusqu'à la première surface rencontrée, quelle que soit
    // sa forme (le prisme moins la pièce, réduit au morceau qui part du profil).
    std::unique_ptr<BRepAlgoAPI_BooleanOperation> op;
    if (enlever) op = std::make_unique<BRepAlgoAPI_Common>(r.forme, corps);
    else op = std::make_unique<BRepAlgoAPI_Cut>(r.forme, corps);
    if (!op->IsDone() || op->HasErrors()) { r.erreur = "« Jusqu'au prochain » n'a pas pu être calculé."; return r; }
    TopoDS_Shape choisi;
    double meilleure = 1e300, volumeChoisi = 0;
    for (TopExp_Explorer ex(op->Shape(), TopAbs_SOLID); ex.More(); ex.Next()) {
      const double v = volumeForme(ex.Current());
      if (v <= 1e-9) continue;
      BRepExtrema_DistShapeShape dist(ex.Current(), face);
      const double dd = dist.IsDone() ? dist.Value() : 1e300;
      if (dd < meilleure - 1e-9) { meilleure = dd; choisi = ex.Current(); volumeChoisi = v; }
    }
    if (choisi.IsNull() || (!enlever && meilleure > 1e-6)) { r.erreur = "Aucune matière rencontrée dans ce sens."; return r; }
    if (!enlever && volumeChoisi >= volumeForme(r.forme) * (1 - 1e-9)) { r.erreur = "Aucune matière rencontrée dans ce sens : le bossage ne touche pas la pièce."; return r; }
    r.noms = suivre(*op, r.noms, choisi);
    r.forme = choisi;
  }
  if (rogner) {
    const TopoDS_Shape demi = BRepPrimAPI_MakeHalfSpace(bornante, auDela).Solid();
    BRepAlgoAPI_Cut coupe(r.forme, demi);
    if (!coupe.IsDone() || coupe.HasErrors()) { r.erreur = "Impossible d'arrêter l'extrusion sur cette face."; return r; }
    // Seul le morceau qui part du profil compte : une surface fermée (cylindre, sphère) traversée de part en
    // part laisserait sinon un second tronçon au-delà.
    TopoDS_Shape garde = coupe.Shape();
    {
      std::vector<TopoDS_Shape> morceaux;
      for (TopExp_Explorer ex(coupe.Shape(), TopAbs_SOLID); ex.More(); ex.Next()) {
        BRepExtrema_DistShapeShape dist(ex.Current(), face);
        if (dist.IsDone() && dist.Value() < 1e-6) morceaux.push_back(ex.Current());
      }
      if (morceaux.size() == 1) garde = morceaux.front();
      else if (morceaux.empty()) { r.erreur = "La face choisie arrête l'extrusion avant même son départ : inverse le sens ou choisis une autre face."; return r; }
    }
    Noms n = suivre(coupe, r.noms, garde);
    CarteFaces facesGardees;
    TopExp::MapShapes(garde, TopAbs_FACE, facesGardees);
    for (TopExp_Explorer ex(demi, TopAbs_FACE); ex.More(); ex.Next())
      for (const TopoDS_Shape& m : coupe.Modified(ex.Current()))
        if (m.ShapeType() == TopAbs_FACE && facesGardees.Contains(m)) n.push_back({TopoDS::Face(m), id + ":sommet" + suffixe});
    r.forme = solideUnique(garde);
    r.noms = n;
  }
  return r;
}

Outil reunir(const Outil& a, const Outil& b) {
  if (a.forme.IsNull()) return b;
  if (b.forme.IsNull()) return a;
  Outil r;
  BRepAlgoAPI_Fuse fu(a.forme, b.forme);
  if (!fu.IsDone()) { r.erreur = "Les volumes ne peuvent pas être réunis."; return r; }
  r.noms = suivre(fu, a.noms, fu.Shape());
  const Noms n2 = suivre(fu, b.noms, fu.Shape());
  r.noms.insert(r.noms.end(), n2.begin(), n2.end());
  r.forme = fu.Shape();
  return r;
}

// Extrusion d'une face selon les conditions de fin de la fonction (une ou deux directions, plan milieu).
Outil extruder(const FonctionPiece& f, const TopoDS_Face& face, std::vector<std::pair<TopoDS_Edge, int>> origine, const gp_Ax3& ax, const TopoDS_Shape& corps,
               const Noms& noms, const std::string& id, const std::string& suffixe) {
  // Compatibilité : « symétrique » et « traversant » des versions 0.7 et 0.8.
  const std::string fin1 = f.traversant ? "traversant" : f.symetrique ? "milieu" : f.fin;
  const double s1 = f.inverse ? -1 : 1;
  if (fin1 == "milieu") {
    if (std::abs(f.depouille) < 1e-9) {
      gp_Trsf t;
      t.SetTranslation(gp_Vec(ax.Direction()) * (-0.5 * f.longueur));
      BRepBuilderAPI_Transform tr(face, t, true);
      for (auto& [ed, el] : origine) ed = TopoDS::Edge(tr.ModifiedShape(ed));
      return prisme(TopoDS::Face(tr.Shape()), origine, ax, 1, "borgne", f.longueur, "", 0, 0, f.enlever, corps, noms, id, suffixe);
    }
    // Avec dépouille : deux demi-extrusions symétriques dont le plan neutre est le plan d'esquisse, comme deux
    // directions égales (FO-20) ; la face d'arrivée de la seconde reste « base », comme sans dépouille.
    Outil a = prisme(face, origine, ax, 1, "borgne", f.longueur / 2, "", 0, f.depouille, f.enlever, corps, noms, id, suffixe);
    if (!a.erreur.empty()) return a;
    Outil b = prisme(face, origine, ax, -1, "borgne", f.longueur / 2, "", 0, f.depouille, f.enlever, corps, noms, id, suffixe + ".d2");
    if (!b.erreur.empty()) return b;
    for (auto& n : b.noms)
      if (n.nom == id + ":sommet" + suffixe + ".d2") n.nom = id + ":base" + suffixe;
    return reunir(a, b);
  }
  Outil morceau = prisme(face, origine, ax, s1, fin1, f.longueur, f.refFin, f.decalageFin, f.depouille, f.enlever, corps, noms, id, suffixe);
  if (morceau.erreur.empty() && !f.fin2.empty()) {
    const Outil autre = prisme(face, origine, ax, -s1, f.fin2, f.longueur2, f.refFin2, f.decalageFin2, f.depouille, f.enlever, corps, noms, id, suffixe + ".d2");
    if (!autre.erreur.empty()) morceau.erreur = "Direction 2 : " + autre.erreur;
    else morceau = reunir(morceau, autre);
  }
  return morceau;
}

// Révolution d'une face autour d'une ligne de l'esquisse ou d'un axe de base, qui doit être dans le plan de
// l'esquisse : un axe perpendiculaire donnerait un solide plat accepté sans rien dire (FO-22).
Outil revolutionner(const FonctionPiece& f, const FonctionPiece& fe, TopoDS_Face face, std::vector<std::pair<TopoDS_Edge, int>> origine, const gp_Ax3& ax,
                    const std::string& id, const std::string& suffixe, const gp_Ax1* axeChoisi = nullptr) {
  Outil morceau;
  gp_Ax1 axe;
  if (f.axeBase == "X" || f.axeBase == "Y" || f.axeBase == "Z" || axeChoisi) {
    // Axe de base, ou axe choisi au clic (axe de référence, arête droite, face cylindrique, ligne d'une autre esquisse)
    axe = axeChoisi ? *axeChoisi : gp_Ax1(gp_Pnt(0, 0, 0), f.axeBase == "X" ? gp_Dir(1, 0, 0) : f.axeBase == "Y" ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1));
    const double horsPlan = std::abs(gp_Vec(axe.Direction()).Dot(gp_Vec(ax.Direction())));
    const double distance = std::abs(gp_Vec(ax.Location(), axe.Location()).Dot(gp_Vec(ax.Direction())));
    if (horsPlan > 1e-9 || distance > 1e-6) {
      morceau.erreur = (axeChoisi ? std::string("L'axe choisi") : "L'axe " + f.axeBase) +
                       " n'est pas dans le plan de l'esquisse : choisis une ligne de l'esquisse, ou un axe contenu dans son plan.";
      return morceau;
    }
  } else {
    const ElementEsquisse* el = fe.esquisse.element(f.axe);
    if (!el || el->type != TypeElement::Ligne) { morceau.erreur = "Choisis l'axe de révolution : une ligne de l'esquisse ou un axe X, Y, Z."; return morceau; }
    const PointEsquisse* pa = fe.esquisse.point(el->p1);
    const PointEsquisse* pb = fe.esquisse.point(el->p2);
    if (!pa || !pb) { morceau.erreur = "La ligne choisie comme axe est incomplète."; return morceau; }
    const gp_Pnt A = vers3D(ax, pa->x, pa->y), B = vers3D(ax, pb->x, pb->y);
    if (A.Distance(B) < 1e-7) { morceau.erreur = "L'axe de révolution est de longueur nulle."; return morceau; }
    axe = gp_Ax1(A, gp_Dir(gp_Vec(A, B)));
  }
  if (f.inverse) axe.Reverse();  // sens de rotation inversé
  double a1 = std::min(f.angle, 360.0), depart = 0;
  if (!(a1 > 1e-9)) { morceau.erreur = "L'angle de révolution doit être positif."; return morceau; }
  if (f.milieu) depart = -a1 / 2;
  else if (f.angle2 > 0) { depart = -f.angle2; a1 = std::min(360.0, a1 + f.angle2); }
  if (depart != 0) {
    gp_Trsf t;
    t.SetRotation(axe, depart * std::numbers::pi / 180);
    BRepBuilderAPI_Transform tr(face, t, true);
    for (auto& [ed, el] : origine) ed = TopoDS::Edge(tr.ModifiedShape(ed));
    face = TopoDS::Face(tr.Shape());
  }
  // Le profil ne doit pas traverser l'axe : tous ses points du même côté (au sens large)
  {
    double mini = 1e300, maxi = -1e300;
    const gp_Vec n = gp_Vec(axe.Direction()).Crossed(gp_Vec(ax.Direction()));
    for (TopExp_Explorer ex(face, TopAbs_VERTEX); ex.More(); ex.Next()) {
      const double c = gp_Vec(axe.Location(), BRep_Tool::Pnt(TopoDS::Vertex(ex.Current()))).Dot(n);
      mini = std::min(mini, c);
      maxi = std::max(maxi, c);
    }
    if (mini < -1e-6 && maxi > 1e-6) { morceau.erreur = "La révolution est impossible : le profil traverse l'axe."; return morceau; }
  }
  BRepPrimAPI_MakeRevol mk(face, axe, a1 * std::numbers::pi / 180, true);
  if (!mk.IsDone()) { morceau.erreur = "La révolution a échoué (profil dégénéré ou axe mal placé)."; return morceau; }
  morceau.forme = mk.Shape();
  for (const auto& [ed, el] : origine)
    for (const TopoDS_Shape& gf : mk.Generated(ed))
      if (gf.ShapeType() == TopAbs_FACE) morceau.noms.push_back({TopoDS::Face(gf), id + ":cote:" + std::to_string(el) + suffixe});
  // Faces sans origine (fonction mince) : nommées dans l'ordre
  int k = 0;
  for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
    bool connu = false;
    for (const auto& [ed, el] : origine) if (ed.IsSame(ex.Current())) connu = true;
    if (connu) continue;
    for (const TopoDS_Shape& gf : mk.Generated(ex.Current()))
      if (gf.ShapeType() == TopAbs_FACE) morceau.noms.push_back({TopoDS::Face(gf), id + ":paroi." + std::to_string(k++) + suffixe});
  }
  if (a1 < 359.999) {
    morceau.noms.push_back({TopoDS::Face(mk.FirstShape()), id + ":debut" + suffixe});
    morceau.noms.push_back({TopoDS::Face(mk.LastShape()), id + ":fin" + suffixe});
  }
  return morceau;
}

}  // namespace

// ---------- Résultat : noms et résolution des références ----------
std::string ResultatPiece::nomFace(const TopoDS_Face& f) const {
  std::string nom;
  for (const auto& x : faces) if (x.face.IsSame(f)) nom = x.nom;
  if (nom.empty()) return {};
  std::vector<TopoDS_Face> memes;
  for (const auto& x : faces) if (x.nom == nom) memes.push_back(x.face);
  if (memes.size() == 1) return nom;
  std::sort(memes.begin(), memes.end(), [](const TopoDS_Face& a, const TopoDS_Face& b) { return avant(centreFace(a), centreFace(b)); });
  for (size_t i = 0; i < memes.size(); ++i) if (memes[i].IsSame(f)) return avecIndice(nom, i, memes.size());
  return nom;
}

std::vector<std::pair<TopoDS_Face, std::string>> facesColorees(const std::map<std::string, std::string>& couleurs, const ResultatPiece& r) {
  std::vector<std::pair<TopoDS_Face, std::string>> res;
  for (const auto& [nom, couleur] : couleurs) {
    std::vector<TopoDS_Face> fs;
    if (!r.resoudreFaces(nom, fs, false).trouvee()) continue;
    for (const auto& f : fs) res.emplace_back(f, couleur);
  }
  return res;
}

bool ResultatPiece::obsolete(int fonction) const { return std::find(obsoletes.begin(), obsoletes.end(), fonction) != obsoletes.end(); }

ResolutionReference ResultatPiece::resoudreFaces(const std::string& reference, std::vector<TopoDS_Face>& sortie, bool unique) const {
  RefIndicee ri = separerIndice(reference);
  std::vector<TopoDS_Face> memes;
  for (const auto& x : faces) if (x.nom == ri.nom) memes.push_back(x.face);
  // Ancien nom « F:face.i » (versions 0.24 et avant) : accepté s'il désigne une seule face « F:face.i:n ».
  if (memes.empty() && ri.nom.find(":face.") != std::string::npos && std::count(ri.nom.begin(), ri.nom.end(), ':') == 1) {
    std::string trouve;
    int nombre = 0;
    for (const auto& x : faces)
      if (x.nom.rfind(ri.nom + ":", 0) == 0 && x.nom.find(':', ri.nom.size() + 1) == std::string::npos) { trouve = x.nom; ++nombre; }
    if (nombre == 1)
      for (const auto& x : faces) if (x.nom == trouve) memes.push_back(x.face);
  }
  std::sort(memes.begin(), memes.end(), [](const TopoDS_Face& a, const TopoDS_Face& b) { return avant(centreFace(a), centreFace(b)); });
  ResolutionReference res = choisir(ri, memes, unique, sortie);
  if (!res.trouvee()) {
    res.message = "face " + res.message;
    referenceCassee = res.message;
  }
  return res;
}

std::vector<TopoDS_Face> ResultatPiece::facesNommees(const std::string& reference) const {
  std::vector<TopoDS_Face> r;
  resoudreFaces(reference, r, false);
  return r;
}

std::string ResultatPiece::nomArete(const TopoDS_Edge& e) const {
  for (const auto& [cle, l] : aretesParCle(forme, faces))
    for (size_t i = 0; i < l.size(); ++i)
      if (l[i].IsSame(e)) return avecIndice(cle, i, l.size());
  return {};
}

ResolutionReference ResultatPiece::resoudreAretes(const std::string& reference, std::vector<TopoDS_Edge>& sortie) const {
  const RefIndicee ri = separerIndice(reference);
  const auto groupes = aretesParCle(forme, faces);
  const auto it = groupes.find(ri.nom);
  static const std::vector<TopoDS_Edge> aucune;
  ResolutionReference res = choisir(ri, it == groupes.end() ? aucune : it->second, false, sortie);
  if (!res.trouvee()) {
    res.message = "arête " + res.message;
    referenceCassee = res.message;
  }
  return res;
}

std::vector<TopoDS_Edge> ResultatPiece::aretes(const std::string& reference) const {
  std::vector<TopoDS_Edge> r;
  resoudreAretes(reference, r);
  return r;
}

namespace {
std::map<std::string, std::vector<TopoDS_Vertex>> sommetsParCle(const TopoDS_Shape& forme, const Noms& noms) {
  std::map<std::string, std::vector<TopoDS_Vertex>> res;
  if (forme.IsNull()) return res;
  CarteAncetres anc;
  TopExp::MapShapesAndAncestors(forme, TopAbs_VERTEX, TopAbs_FACE, anc);
  for (int i = 1; i <= anc.Extent(); ++i) {
    std::vector<std::string> n;
    for (const TopoDS_Shape& f : anc.FindFromIndex(i))
      for (const auto& x : noms) if (x.face.IsSame(f)) { n.push_back(x.nom); break; }
    std::sort(n.begin(), n.end());
    n.erase(std::unique(n.begin(), n.end()), n.end());
    std::string cle;
    for (const auto& s : n) cle += (cle.empty() ? "" : "|") + s;
    if (!cle.empty()) res[cle].push_back(TopoDS::Vertex(anc.FindKey(i)));
  }
  for (auto& [cle, l] : res)
    std::sort(l.begin(), l.end(), [](const TopoDS_Vertex& a, const TopoDS_Vertex& b) { return avant(BRep_Tool::Pnt(a), BRep_Tool::Pnt(b)); });
  return res;
}
}  // namespace

std::string ResultatPiece::nomSommet(const TopoDS_Vertex& v) const {
  for (const auto& [cle, l] : sommetsParCle(forme, faces))
    for (size_t i = 0; i < l.size(); ++i)
      if (l[i].IsSame(v)) return "S:" + avecIndice(cle, i, l.size());
  return {};
}

ResolutionReference ResultatPiece::resoudreSommet(const std::string& reference, TopoDS_Vertex& sommet) const {
  ResolutionReference res;
  if (reference.rfind("S:", 0) != 0) {
    res.message = "« " + reference + " » n'est pas un sommet";
    referenceCassee = res.message;
    return res;
  }
  const RefIndicee ri = separerIndice(reference.substr(2));
  const auto g = sommetsParCle(forme, faces);
  const auto it = g.find(ri.nom);
  static const std::vector<TopoDS_Vertex> aucun;
  std::vector<TopoDS_Vertex> l;
  res = choisir(ri, it == g.end() ? aucun : it->second, true, l);
  if (res.trouvee()) sommet = l.front();
  else {
    res.message = "sommet " + res.message;
    referenceCassee = res.message;
  }
  return res;
}

std::vector<TopoDS_Vertex> ResultatPiece::sommets(const std::string& reference) const {
  TopoDS_Vertex v;
  if (!resoudreSommet(reference, v).trouvee()) return {};
  return {v};
}

std::vector<RegionEsquisse> regionsEsquisse(const FonctionPiece& esquisse, const gp_Ax3& repere) {
  std::vector<RegionEsquisse> res;
  const Profil pr = profil(esquisse, repere);
  for (size_t i = 0; i < pr.faces.size() && i < pr.cles.size(); ++i)
    res.push_back({pr.cles[i], pr.faces[i], pr.parDefaut[i], pr.interieurs[i].X(), pr.interieurs[i].Y()});
  return res;
}

std::string ResultatPiece::erreur(int fonction) const {
  for (const auto& [id, m] : erreurs) if (id == fonction) return m;
  return {};
}

std::string ResultatPiece::avertissement(int fonction) const {
  for (const auto& [id, m] : avertissements) if (id == fonction) return m;
  return {};
}

gp_Ax3 ResultatPiece::repere(int esquisse) const {
  for (const auto& [id, ax] : reperes) if (id == esquisse) return ax;
  return repereEsquisse(PlanBase::XY, 0);
}

bool normaleSortante(const TopoDS_Face& f0, const TopoDS_Shape& solide, gp_Pnt& point, gp_Dir& n) {
  // La face telle qu'elle est orientée dans le solide
  TopoDS_Face f = f0;
  if (!solide.IsNull())
    for (TopExp_Explorer ex(solide, TopAbs_FACE); ex.More(); ex.Next())
      if (ex.Current().IsSame(f0)) { f = TopoDS::Face(ex.Current()); break; }
  BRepAdaptor_Surface s(f);
  if (s.GetType() != GeomAbs_Plane) return false;
  const gp_Pln pl = s.Plane();
  // Normale géométrique de la surface (Xdir ^ Ydir : opposée à l'axe principal pour un repère indirect),
  // retournée pour une face REVERSED
  n = pl.Axis().Direction();
  if (!pl.Position().Direct()) n.Reverse();
  if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
  const gp_Pnt2d uv = pointInterieur(f);
  point = s.Value(uv.X(), uv.Y());
  if (solide.IsNull()) return true;
  // Contrôle indépendant de l'historique : le point voisin côté normale doit être hors de la matière
  try {
    Bnd_Box b;
    BRepBndLib::Add(solide, b);
    const double taille = b.IsVoid() ? 1.0 : std::sqrt(b.SquareExtent());
    const double eps = std::max(1e-5, 1e-5 * taille);
    BRepClass3d_SolidClassifier dehors(solide, point.Translated(gp_Vec(n) * eps), 1e-9);
    BRepClass3d_SolidClassifier dedans(solide, point.Translated(gp_Vec(n) * -eps), 1e-9);
    if (dehors.State() == TopAbs_IN && dedans.State() == TopAbs_OUT) n.Reverse();
  } catch (const Standard_Failure&) {
    // l'orientation de la face fait foi
  }
  return true;
}

bool repereFace(const TopoDS_Face& f, gp_Ax3& repere, const TopoDS_Shape& solide, const std::array<double, 3>* axeX) {
  gp_Pnt p;
  gp_Dir n;
  if (!normaleSortante(f, solide, p, n)) return false;
  // Origine : projection de l'origine globale sur le plan
  const double d = gp_Vec(n).Dot(gp_Vec(p.XYZ()));
  const gp_Pnt origine(n.X() * d, n.Y() * d, n.Z() * d);
  gp_Vec x;
  if (axeX && std::hypot((*axeX)[0], (*axeX)[1], (*axeX)[2]) > 1e-12) {
    x = gp_Vec((*axeX)[0], (*axeX)[1], (*axeX)[2]);
    x -= gp_Vec(n) * gp_Vec(n).Dot(x);
  }
  if (x.Magnitude() < 1e-9) {
    x = gp_Vec(0, 0, 1).Crossed(gp_Vec(n));
    if (x.Magnitude() < 1e-9) x = gp_Vec(1, 0, 0) - gp_Vec(n) * n.X();
  }
  repere = gp_Ax3(origine, n, gp_Dir(x));
  return true;
}

std::string defautGeometrie(const TopoDS_Shape& forme) {
  if (forme.IsNull()) return "aucune géométrie";
  try {
    BRepCheck_Analyzer analyse(forme);
    if (analyse.IsValid()) return {};
    // Premier défaut trouvé, du plus grossier au plus fin
    const std::pair<TopAbs_ShapeEnum, const char*> niveaux[] = {
        {TopAbs_SOLID, "solide"}, {TopAbs_SHELL, "coque"}, {TopAbs_FACE, "face"}, {TopAbs_WIRE, "contour"}, {TopAbs_EDGE, "arête"}, {TopAbs_VERTEX, "sommet"}};
    for (const auto& [type, nom] : niveaux) {
      int fautifs = 0;
      BRepCheck_Status premier = BRepCheck_NoError;
      for (TopExp_Explorer ex(forme, type); ex.More(); ex.Next()) {
        const Handle(BRepCheck_Result)& res = analyse.Result(ex.Current());
        if (res.IsNull()) continue;
        for (const BRepCheck_Status st : res->Status())
          if (st != BRepCheck_NoError) {
            if (premier == BRepCheck_NoError) premier = st;
            ++fautifs;
            break;
          }
      }
      if (fautifs == 0) continue;
      std::string cause;
      switch (premier) {
        case BRepCheck_NotClosed: cause = "non fermé"; break;
        case BRepCheck_BadOrientation:
        case BRepCheck_BadOrientationOfSubshape: cause = "orientation incohérente"; break;
        case BRepCheck_SelfIntersectingWire: cause = "qui se recoupe"; break;
        case BRepCheck_FreeEdge: cause = "avec une arête libre"; break;
        case BRepCheck_InvalidMultiConnexity:
        case BRepCheck_NotConnected: cause = "mal raccordé"; break;
        case BRepCheck_InvalidToleranceValue: cause = "tolérance invalide"; break;
        default: cause = "défaut " + std::to_string(static_cast<int>(premier)); break;
      }
      return std::to_string(fautifs) + " " + nom + (fautifs > 1 ? "s" : "") + " " + cause;
    }
    return "contrôle B-Rep en échec";
  } catch (const Standard_Failure&) {
    return "contrôle B-Rep impossible";
  }
}

std::string libelleStatut(StatutMasse s) {
  switch (s) {
    case StatutMasse::Exact: return "exact : solide valide et fermé";
    case StatutMasse::PlusieursSolides: return "exact : plusieurs solides valides, valeurs cumulées";
    case StatutMasse::Approche: return "approché : la géométrie présente un défaut, valeurs indicatives seulement";
    case StatutMasse::Vide: return "aucun solide";
    case StatutMasse::Echec: return "échec du calcul";
  }
  return {};
}

ProprietesMasse proprietesMasse(const TopoDS_Shape& forme, double densite) {
  ProprietesMasse r;
  if (forme.IsNull()) return r;
  try {
    const double rho = densite / 1000.0;  // g/mm³
    GProp_GProps total;
    bool premier = true;
    for (TopExp_Explorer ex(forme, TopAbs_SOLID); ex.More(); ex.Next()) {
      TopoDS_Shape s = ex.Current();
      GProp_GProps v;
      BRepGProp::VolumeProperties(s, v);
      if (v.Mass() < 0) {  // solide retourné : on le remet à l'endroit
        s.Reverse();
        v = GProp_GProps();
        BRepGProp::VolumeProperties(s, v);
      }
      ++r.solides;
      if (premier) { total = v; premier = false; } else total.Add(v);
    }
    if (premier) return r;  // aucun solide (surfaces seules)
    GProp_GProps surf;
    BRepGProp::SurfaceProperties(forme, surf);
    r.volume = total.Mass();
    r.aire = surf.Mass();
    r.masse = r.volume * rho;
    const gp_Pnt c = total.CentreOfMass();
    r.cx = c.X(); r.cy = c.Y(); r.cz = c.Z();
    const gp_Mat I = total.MatrixOfInertia();
    r.ixx = I(1, 1) * rho; r.iyy = I(2, 2) * rho; r.izz = I(3, 3) * rho;
    // Produits d'inertie Pxy = ∫(x − x̄)(y − ȳ) dm : opposés des termes hors diagonale du tenseur (FO-23)
    r.ixy = -I(1, 2) * rho; r.ixz = -I(1, 3) * rho; r.iyz = -I(2, 3) * rho;
    // Moments principaux triés, avec leurs axes
    const GProp_PrincipalProps pp = total.PrincipalProperties();
    double a = 0, b = 0, cc = 0;
    pp.Moments(a, b, cc);
    std::array<std::pair<double, gp_Vec>, 3> principaux = {{{a, pp.FirstAxisOfInertia()}, {b, pp.SecondAxisOfInertia()}, {cc, pp.ThirdAxisOfInertia()}}};
    std::sort(principaux.begin(), principaux.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    r.i1 = principaux[0].first * rho; r.i2 = principaux[1].first * rho; r.i3 = principaux[2].first * rho;
    for (std::size_t k = 0; k < 3; ++k) {
      gp_Vec v = principaux[k].second;
      if (v.Magnitude() > 1e-12) v.Normalize();
      // Sens conventionnel : la plus grande composante positive (axes stables d'un calcul à l'autre)
      const double m = std::abs(v.X()) >= std::abs(v.Y()) && std::abs(v.X()) >= std::abs(v.Z()) ? v.X() : std::abs(v.Y()) >= std::abs(v.Z()) ? v.Y() : v.Z();
      if (m < 0) v.Reverse();
      r.axesPrincipaux[k] = {v.X(), v.Y(), v.Z()};
    }
    r.defaut = defautGeometrie(forme);
    if (r.defaut.empty() && !(r.volume > 0 && r.ixx >= 0 && r.iyy >= 0 && r.izz >= 0)) r.defaut = "volume ou inertie négatifs";
    r.statut = !r.defaut.empty() ? StatutMasse::Approche : r.solides > 1 ? StatutMasse::PlusieursSolides : StatutMasse::Exact;
  } catch (const Standard_Failure& e) {
    r = ProprietesMasse{};
    r.statut = StatutMasse::Echec;
#if OCC_VERSION_HEX >= 0x080000
    r.defaut = e.what();
#else
    r.defaut = e.GetMessageString() ? e.GetMessageString() : "erreur interne";
#endif
  }
  r.valide = r.statut == StatutMasse::Exact || r.statut == StatutMasse::PlusieursSolides;
  return r;
}

// Tables de perçage (mm). ISO : filetage métrique à pas gros, passages ISO 273, lamages pour vis CHC ISO 4762,
// fraisures à 90° pour vis FHC ISO 10642. ANSI : filetages UNC, convertis en millimètres, fraisures à 82° (têtes
// fraisées en pouces, ASME B18.3).
const std::vector<DonneesTrou>& tableTrous() {
  static const std::vector<DonneesTrou> t = {
      {"ISO", "M2", 0.4, 1.6, 2.2, 2.4, 2.6, 4.3, 2.3, 4.4},       {"ISO", "M2.5", 0.45, 2.05, 2.7, 2.9, 3.1, 5.0, 2.8, 5.5},
      {"ISO", "M3", 0.5, 2.5, 3.2, 3.4, 3.6, 6.5, 3.3, 6.3},       {"ISO", "M4", 0.7, 3.3, 4.3, 4.5, 4.8, 8.0, 4.4, 9.4},
      {"ISO", "M5", 0.8, 4.2, 5.3, 5.5, 5.8, 10.0, 5.4, 10.4},     {"ISO", "M6", 1.0, 5.0, 6.4, 6.6, 7.0, 11.0, 6.4, 12.6},
      {"ISO", "M8", 1.25, 6.8, 8.4, 9.0, 10.0, 15.0, 8.6, 17.3},   {"ISO", "M10", 1.5, 8.5, 10.5, 11.0, 12.0, 18.0, 10.6, 20.0},
      {"ISO", "M12", 1.75, 10.2, 13.0, 13.5, 14.5, 20.0, 12.6, 24.0}, {"ISO", "M14", 2.0, 12.0, 15.0, 15.5, 16.5, 24.0, 14.6, 27.0},
      {"ISO", "M16", 2.0, 14.0, 17.0, 17.5, 18.5, 26.0, 16.6, 31.0}, {"ISO", "M20", 2.5, 17.5, 21.0, 22.0, 24.0, 33.0, 20.6, 38.0},
      {"ISO", "M24", 3.0, 21.0, 25.0, 26.0, 28.0, 40.0, 24.8, 45.0},
      {"ANSI", "#4-40", 0.635, 2.26, 2.95, 3.26, 3.57, 5.56, 2.84, 5.84, 82}, {"ANSI", "#6-32", 0.794, 2.69, 3.66, 3.80, 4.04, 6.35, 3.51, 7.11, 82},
      {"ANSI", "#8-32", 0.794, 3.45, 4.32, 4.50, 4.83, 7.14, 4.17, 8.38, 82}, {"ANSI", "#10-24", 1.058, 3.80, 4.98, 5.11, 5.56, 7.92, 4.83, 9.65, 82},
      {"ANSI", "1/4-20", 1.27, 5.11, 6.63, 6.75, 7.14, 10.31, 6.35, 12.70, 82}, {"ANSI", "5/16-18", 1.411, 6.53, 8.33, 8.43, 8.74, 12.70, 7.92, 15.88, 82},
      {"ANSI", "3/8-16", 1.588, 7.94, 9.92, 10.08, 10.32, 15.09, 9.53, 19.05, 82}, {"ANSI", "1/2-13", 1.954, 10.72, 13.10, 13.49, 13.89, 20.62, 12.70, 25.40, 82}};
  return t;
}

const DonneesTrou* donneesTrou(const std::string& norme, const std::string& taille) {
  for (const auto& d : tableTrous()) if (d.norme == norme && d.taille == taille) return &d;
  return nullptr;
}

double diametreNominal(const DonneesTrou& d) {
  const std::string& t = d.taille;
  if (!t.empty() && t[0] == 'M') return std::atof(t.c_str() + 1);
  if (!t.empty() && t[0] == '#') return (0.060 + 0.013 * std::atoi(t.c_str() + 1)) * 25.4;  // numéros UNC
  const auto barre = t.find('/');
  if (barre != std::string::npos) return std::atof(t.c_str()) / std::atof(t.c_str() + barre + 1) * 25.4;
  return d.forageTaraud + 1.0825 * d.pas;
}

const DonneesTrou* filetagePour(double diametre, bool exterieur, const std::string& norme) {
  const DonneesTrou* meilleur = nullptr;
  double ecart = 0.3;
  for (const auto& d : tableTrous()) {
    if (d.norme != norme) continue;
    const double e = std::abs((exterieur ? diametreNominal(d) : d.forageTaraud) - diametre);
    if (e < ecart) { ecart = e; meilleur = &d; }
  }
  return meilleur;
}

std::string designationFiletage(const std::string& taille, double pas, const std::string& norme) {
  if (norme == "ANSI") return taille + " UNC";
  char t[32];
  std::snprintf(t, sizeof t, "%.3g", pas);
  std::string p = t;
  for (char& c : p)
    if (c == '.') c = ',';
  return taille + "×" + p;
}

TopoDS_Shape traceFiletages(const ResultatPiece& r) {
  BRep_Builder bb;
  TopoDS_Compound c;
  bb.MakeCompound(c);
  bool vide = true;
  std::size_t points = 0;
  for (const auto& f : r.filetages) {
    if (!(f.pas > 0) || !(f.longueur > 0)) continue;
    // Rayon montré : sur l'arbre (juste au-dessus) ou sur la paroi du trou (juste en dedans), là où l'œil le voit
    const double rho = f.exterieur ? f.nominal / 2 + 0.01 : std::max(0.05, f.mineur / 2 - 0.01);
    for (const auto& axe : f.axes) {
      const gp_Ax3 rep(axe.Location(), axe.Direction());
      const double tours = f.longueur / f.pas;
      const int n = std::clamp(static_cast<int>(tours * 36), 12, 4000);
      if (points + static_cast<std::size_t>(n) > 60000) break;  // une pièce à mille trous taraudés reste fluide
      points += static_cast<std::size_t>(n);
      BRepBuilderAPI_MakePolygon helice;
      for (int k = 0; k <= n; ++k) {
        const double a = 2 * std::numbers::pi * tours * k / n, z = f.longueur * k / n;
        helice.Add(axe.Location().Translated(gp_Vec(rep.XDirection()) * (rho * std::cos(a)) + gp_Vec(rep.YDirection()) * (rho * std::sin(a)) + gp_Vec(axe.Direction()) * z));
      }
      if (helice.IsDone()) { bb.Add(c, helice.Wire()); vide = false; }
      for (const double z : {0.0, f.longueur})
        bb.Add(c, BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(axe.Location().Translated(gp_Vec(axe.Direction()) * z), axe.Direction()), rho)).Edge());
    }
  }
  return vide ? TopoDS_Shape() : TopoDS_Shape(c);
}

gp_Ax3 repereEsquisse(PlanBase plan, double decalage) {
  switch (plan) {
    case PlanBase::XZ: return gp_Ax3(gp_Pnt(0, -decalage, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
    case PlanBase::YZ: return gp_Ax3(gp_Pnt(decalage, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
    case PlanBase::XY: break;
  }
  return gp_Ax3(gp_Pnt(0, 0, decalage), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
}

ResultatPiece construirePiece(const Piece& pieceDonnee, int jusquA) {
  silencer();
  // Esquisses à références externes (S3) : elles sont mises à jour en cours de route ; on travaille alors sur une
  // copie de la pièce, que les fonctions suivantes lisent
  const bool aLiens = std::any_of(pieceDonnee.fonctions.begin(), pieceDonnee.fonctions.end(),
                                  [](const FonctionPiece& f) { return f.type == TypeFonction::Esquisse && !f.esquisse.liensExternes.empty(); });
  Piece copieSuivie;
  if (aLiens) copieSuivie = pieceDonnee;
  const Piece& p = aLiens ? copieSuivie : pieceDonnee;
  ResultatPiece r;
  TopoDS_Shape corps;
  Noms noms;
  std::map<int, gp_Ax3> plansRef;
  std::map<int, gp_Ax1> axesRef;
  std::map<int, gp_Pnt> pointsRef;
  struct OutilMemo { TopoDS_Shape forme; bool enlever = false; Noms noms; };
  std::map<int, OutilMemo> outils;
  std::map<int, TopoDS_Wire> chemins3d;  // esquisses 3D, chemins de balayage
  ResultatPiece etatRef;  // vue courante du corps, pour résoudre les références (et garder la cause d'un échec)
  auto etatCourant = [&]() -> const ResultatPiece& {
    etatRef.forme = corps;
    etatRef.faces = noms;
    return etatRef;
  };
  // Plan de référence : plan de base, autre plan de référence (« F:12 ») ou face plane nommée.
  auto resoudrePlan = [&](const std::string& ref, gp_Ax3& ax) -> bool {
    if (ref == "XY") { ax = repereEsquisse(PlanBase::XY, 0); return true; }
    if (ref == "XZ") { ax = repereEsquisse(PlanBase::XZ, 0); return true; }
    if (ref == "YZ") { ax = repereEsquisse(PlanBase::YZ, 0); return true; }
    if (ref.rfind("F:", 0) == 0) {
      const auto it = plansRef.find(std::atoi(ref.c_str() + 2));
      if (it == plansRef.end()) { etatRef.referenceCassee = "le plan de référence « " + ref + " » n'existe plus ou est en échec"; return false; }
      ax = it->second;
      return true;
    }
    std::vector<TopoDS_Face> fs;
    if (!etatCourant().resoudreFaces(ref, fs, true).trouvee()) return false;
    if (!repereFace(fs.front(), ax, corps)) { etatRef.referenceCassee = "la face « " + ref + " » n'est plus plane"; return false; }
    return true;
  };
  // Axe : X, Y, Z, axe de référence, arête droite, ou face cylindrique (son axe).
  auto resoudreAxe = [&](const std::string& ref, gp_Ax1& a) -> bool {
    if (ref == "X") { a = gp::OX(); return true; }
    if (ref == "Y") { a = gp::OY(); return true; }
    if (ref == "Z") { a = gp::OZ(); return true; }
    if (ref.rfind("F:", 0) == 0) { const auto it = axesRef.find(std::atoi(ref.c_str() + 2)); if (it == axesRef.end()) return false; a = it->second; return true; }
    if (ref.rfind("E:", 0) == 0) {
      // Ligne d'une esquisse (« E:12:7 ») : dans le plan de cette esquisse
      const int idE = std::atoi(ref.c_str() + 2);
      const auto deux = ref.find(':', 2);
      const FonctionPiece* fe = p.fonction(idE);
      if (!fe || fe->type != TypeFonction::Esquisse || deux == std::string::npos) { etatRef.referenceCassee = "l'esquisse de l'axe « " + ref + " » n'existe plus"; return false; }
      const ElementEsquisse* el = fe->esquisse.element(std::atoi(ref.c_str() + deux + 1));
      if (!el || el->type != TypeElement::Ligne) { etatRef.referenceCassee = "la ligne de l'axe « " + ref + " » n'existe plus"; return false; }
      const gp_Ax3 axE = r.repere(idE);
      const PointEsquisse *pa = fe->esquisse.point(el->p1), *pb = fe->esquisse.point(el->p2);
      if (!pa || !pb) return false;
      const gp_Pnt A = vers3D(axE, pa->x, pa->y), B = vers3D(axE, pb->x, pb->y);
      if (A.Distance(B) < 1e-9) return false;
      a = gp_Ax1(A, gp_Dir(gp_Vec(A, B)));
      return true;
    }
    const ResultatPiece& e = etatCourant();
    if (ref.find('|') != std::string::npos) {
      std::vector<TopoDS_Edge> as;
      if (!e.resoudreAretes(ref, as).trouvee()) return false;
      if (as.size() > 1) { etatRef.referenceCassee = "l'arête « " + ref + " » désigne maintenant plusieurs arêtes"; return false; }
      for (const auto& ar : as) {
        BRepAdaptor_Curve c(ar);
        if (c.GetType() == GeomAbs_Line) { a = gp_Ax1(c.Value(c.FirstParameter()), c.Line().Direction()); return true; }
        if (c.GetType() == GeomAbs_Circle) { a = c.Circle().Axis(); return true; }
      }
      return false;
    }
    std::vector<TopoDS_Face> fsAxe;
    if (!e.resoudreFaces(ref, fsAxe, true).trouvee()) return false;
    for (const auto& fc : fsAxe) {
      BRepAdaptor_Surface s(fc);
      if (s.GetType() == GeomAbs_Cylinder) { a = s.Cylinder().Axis(); return true; }
      if (s.GetType() == GeomAbs_Cone) { a = s.Cone().Axis(); return true; }
    }
    return false;
  };
  auto resoudrePoint = [&](const std::string& ref, gp_Pnt& q) -> bool {
    if (ref == "O") { q = gp::Origin(); return true; }
    if (ref.rfind("F:", 0) == 0) { const auto it = pointsRef.find(std::atoi(ref.c_str() + 2)); if (it == pointsRef.end()) return false; q = it->second; return true; }
    TopoDS_Vertex v;
    if (!etatCourant().resoudreSommet(ref, v).trouvee()) return false;
    q = BRep_Tool::Pnt(v);
    return true;
  };
  // Fonctions déjà rejouées, dans l'ordre de l'arbre : une fonction ne peut utiliser que celles-là (FO-04).
  std::set<int> rejouees;
  // Esquisse utilisée par une fonction : elle doit exister, être une esquisse active, précéder la fonction et avoir
  // été construite. Un message par cause (FO-04, FO-19, FO-30).
  auto esquisseUtilisable = [&](int idEsq, int fid, bool accepte3D = false, const char* role = "l'esquisse à utiliser") -> const FonctionPiece* {
    const FonctionPiece* fe = idEsq ? p.fonction(idEsq) : nullptr;
    std::string m;
    if (!idEsq) m = std::string("Choisis ") + role + " (clic dans la vue ou dans l'arbre).";
    else if (!fe) m = "L'esquisse utilisée n'existe plus : choisis-en une autre.";
    else if (fe->type != TypeFonction::Esquisse && !(accepte3D && fe->type == TypeFonction::Esquisse3D)) m = "« " + fe->nom + " » n'est pas une esquisse.";
    else if (fe->desactivee) m = "L'esquisse « " + fe->nom + " » est désactivée : réactive-la ou choisis-en une autre.";
    else if (!rejouees.count(fe->id))
      m = "L'esquisse « " + fe->nom + " » est placée après cette fonction dans l'arbre : elle doit la précéder (remonte l'esquisse ou descends la fonction).";
    else if (!r.erreur(fe->id).empty()) m = "L'esquisse « " + fe->nom + " » est en échec : répare-la d'abord.";
    if (m.empty()) return fe;
    r.erreurs.push_back({fid, m});
    return nullptr;
  };
  // Repère d'une esquisse : plan de base, plan de référence ou face de la pièce (sur le corps courant).
  auto repereDEsquisse = [&](const FonctionPiece& f, gp_Ax3& ax, std::string& erreur) -> bool {
    ax = repereEsquisse(f.plan, f.decalage);
    if (f.face.rfind("F:", 0) == 0) {
      if (!resoudrePlan(f.face, ax)) { erreur = "Le plan de référence de cette esquisse n'existe plus."; return false; }
      if (f.decalage != 0) ax.Translate(gp_Vec(ax.Direction()) * f.decalage);
    } else if (!f.face.empty()) {
      std::vector<TopoDS_Face> fs;
      const ResolutionReference rs = etatCourant().resoudreFaces(f.face, fs, true);
      if (!rs.trouvee() || !repereFace(fs.front(), ax, corps, &f.axeX)) {
        erreur = rs.trouvee() ? "La face qui porte cette esquisse n'est plus plane." : "Référence cassée : " + rs.message + ". Pose à nouveau l'esquisse sur une face.";
        ax = repereEsquisse(f.plan, f.decalage);
        return false;
      }
      if (f.decalage != 0) ax.Translate(gp_Vec(ax.Direction()) * f.decalage);
    }
    return true;
  };
  // Ajoute ou enlève un outil au corps en suivant les noms de ses faces.
  // « negligeable » : volume retiré qui ne compte pas (amorce d'un perçage au-dessus de la face) ; négatif : pas de
  // contrôle du volume retiré (copies d'une répétition, dont certaines peuvent tomber hors de la pièce).
  auto appliquer = [&](const TopoDS_Shape& outil, const Noms& nomsOutil, bool enlever, int fid, double negligeable = 0) -> bool {
    if (outil.IsNull()) return false;
    if (corps.IsNull()) {
      if (enlever) { r.erreurs.push_back({fid, "Il n'y a pas encore de matière à enlever."}); return false; }
      corps = solideUnique(outil);
      noms = nomsOutil;
      return true;
    }
    const double volumeAvant = volume(corps);
    if (enlever) {
      BRepAlgoAPI_Cut op(corps, outil);
      if (!op.IsDone() || op.HasErrors()) { r.erreurs.push_back({fid, "L'enlèvement de matière a échoué."}); return false; }
      // Un enlèvement qui ne retire rien est presque toujours une erreur de sens ; un enlèvement qui retire tout ne
      // laisse pas de pièce (FO-13, FO-14) : les deux sont dits au lieu d'être acceptés.
      if (nombreSolides(op.Shape()) == 0) { r.erreurs.push_back({fid, "Cette fonction retirerait toute la matière : elle est refusée."}); return false; }
      const double retire = volumeAvant - volume(op.Shape());
      if (negligeable >= 0 && retire <= std::max(1e-9 * std::max(1.0, volumeAvant), negligeable * (1 + 1e-6) + 1e-9)) {
        r.erreurs.push_back({fid, "L'enlèvement ne retire aucune matière : la forme est hors de la pièce, ou dans le mauvais sens (inverse le sens)."});
        return false;
      }
      Noms n = suivre(op, noms, op.Shape());
      const Noms n2 = suivre(op, nomsOutil, op.Shape());
      n.insert(n.end(), n2.begin(), n2.end());
      corps = solideUnique(op.Shape());
      noms = n;
    } else {
      BRepAlgoAPI_Fuse op(corps, outil);
      if (!op.IsDone() || op.HasErrors()) { r.erreurs.push_back({fid, "L'ajout de matière a échoué."}); return false; }
      if (volume(op.Shape()) - volumeAvant <= 1e-9 * std::max(1.0, volumeAvant))
        r.avertissements.push_back({fid, "Cet ajout ne donne aucune matière nouvelle : la forme est déjà dans la pièce, ou dans le mauvais sens."});
      Noms n = suivre(op, noms, op.Shape());
      const Noms n2 = suivre(op, nomsOutil, op.Shape());
      n.insert(n.end(), n2.begin(), n2.end());
      corps = solideUnique(op.Shape());
      noms = n;
    }
    return true;
  };
  // Copie transformée d'un outil, ses faces renommées « préfixe + nom ».
  auto transformer = [&](const OutilMemo& o, const gp_Trsf& t, const std::string& prefixe, TopoDS_Shape& forme, Noms& nn) {
    BRepBuilderAPI_Transform tr(o.forme, t, true);
    forme = tr.Shape();
    nn.clear();
    for (const auto& [fc, nom] : o.noms) {
      const TopoDS_Shape m = tr.ModifiedShape(fc);
      if (!m.IsNull() && m.ShapeType() == TopAbs_FACE) nn.push_back({TopoDS::Face(m), prefixe + nom});
    }
  };
  tolerie::Constructeur tole;  // tôlerie : parois, plis et déplié, construits avec la pièce
  for (const auto& f : p.fonctions) {
    const TopoDS_Shape corpsAvant = corps;
    const Noms nomsAvant = noms;
    const bool fonctionTole = f.type == TypeFonction::Tole || f.type == TypeFonction::BordTombe;
    std::optional<tolerie::Constructeur> toleAvant;
    if (fonctionTole && !f.desactivee) toleAvant = tole;
    // Une fonction en amont est en échec : les suivantes ne sont pas calculées sur un résultat incomplet. Elles
    // sont marquées obsolètes ; une esquisse garde le repère de son plan de base pour rester modifiable.
    if (r.premiereErreur != 0 && !f.desactivee) {
      r.obsoletes.push_back(f.id);
      // Esquisse obsolète : son repère sur le dernier solide valide si sa face s'y trouve encore, sinon son plan de
      // base (FO-29)
      if (f.type == TypeFonction::Esquisse) {
        gp_Ax3 ax;
        std::string ignore;
        try {
          repereDEsquisse(f, ax, ignore);
        } catch (const Standard_Failure&) {
          ax = repereEsquisse(f.plan, f.decalage);
        }
        etatRef.referenceCassee.clear();
        r.reperes.push_back({f.id, ax});
      }
      if (jusquA > 0 && f.id == jusquA) break;
      continue;
    }
    etatRef.referenceCassee.clear();
    if (!f.desactivee && tole.actif() && !fonctionTole && f.type != TypeFonction::Esquisse && f.type != TypeFonction::Esquisse3D && f.type != TypeFonction::Plan &&
        f.type != TypeFonction::Axe && f.type != TypeFonction::Point && f.type != TypeFonction::Repere && f.type != TypeFonction::Inconnu)
      tole.ignorer(f);
    if (!f.desactivee) {
      try {
        const std::string id = std::to_string(f.id);
        switch (f.type) {
          case TypeFonction::Esquisse: {
            gp_Ax3 ax;
            std::string erreurRepere;
            const bool repereOk = repereDEsquisse(f, ax, erreurRepere);
            r.reperes.push_back({f.id, ax});
            if (!repereOk) { r.erreurs.push_back({f.id, erreurRepere}); break; }
            if (aLiens && !f.esquisse.liensExternes.empty()) {
              // Références externes associatives (S3) : chaque point lié suit son arête, retrouvée par son nom dans le
              // corps tel qu'il est ici ; l'esquisse est résolue de nouveau, puis lue par les fonctions suivantes
              Esquisse maj = f.esquisse;
              int cassees = 0;
              const ResultatPiece& etat = etatCourant();
              const gp_Pnt O = ax.Location();
              const gp_Vec X(ax.XDirection()), Y(ax.YDirection());
              auto uv = [&](const gp_Pnt& q) { const gp_Vec w(O, q); return std::pair<double, double>{w.Dot(X), w.Dot(Y)}; };
              auto fixer = [&](int point, double u, double v) {
                for (auto& k : maj.contraintes)
                  if (k.type == TypeContrainte::Fixe && !k.refs.empty() && k.refs[0] == point) { k.valeur = u; k.valeur2 = v; }
                for (auto& q : maj.points) if (q.id == point) { q.x = u; q.y = v; }
              };
              // Liens regroupés par arête : les deux extrémités d'une ligne sont appariées au plus près
              std::map<std::string, std::vector<const LienExterne*>> parArete;
              for (const auto& l : maj.liensExternes) parArete[l.arete].push_back(&l);
              for (const auto& [nom, liens] : parArete) {
                std::vector<TopoDS_Edge> aretes;
                if (corps.IsNull() || !etat.resoudreAretes(nom, aretes).trouvee() || aretes.size() != 1) { cassees += static_cast<int>(liens.size()); continue; }
                BRepAdaptor_Curve c(aretes.front());
                const auto debut = uv(c.Value(c.FirstParameter())), fin = uv(c.Value(c.LastParameter()));
                std::vector<const LienExterne*> bouts;
                for (const LienExterne* l : liens) {
                  if (l->centre) {
                    if (c.GetType() != GeomAbs_Circle) { ++cassees; continue; }
                    const auto [u, v] = uv(c.Circle().Location());
                    fixer(l->point, u, v);
                  } else {
                    bouts.push_back(l);
                  }
                  if (l->cercle && c.GetType() == GeomAbs_Circle)
                    for (auto& k : maj.contraintes)
                      if ((k.type == TypeContrainte::Rayon || k.type == TypeContrainte::Diametre) && !k.refs.empty() && k.refs[0] == l->cercle)
                        k.valeur = (k.type == TypeContrainte::Diametre ? 2 : 1) * c.Circle().Radius();
                }
                auto distance2 = [&](const LienExterne* l, const std::pair<double, double>& b) {
                  const PointEsquisse* q = maj.point(l->point);
                  return q ? (q->x - b.first) * (q->x - b.first) + (q->y - b.second) * (q->y - b.second) : 0.0;
                };
                if (bouts.size() == 1) {
                  const auto& b = distance2(bouts[0], debut) <= distance2(bouts[0], fin) ? debut : fin;
                  fixer(bouts[0]->point, b.first, b.second);
                } else if (bouts.size() >= 2) {
                  const bool droit = distance2(bouts[0], debut) + distance2(bouts[1], fin) <= distance2(bouts[0], fin) + distance2(bouts[1], debut);
                  fixer(bouts[0]->point, droit ? debut.first : fin.first, droit ? debut.second : fin.second);
                  fixer(bouts[1]->point, droit ? fin.first : debut.first, droit ? fin.second : debut.second);
                }
              }
              resoudre(maj);
              for (auto& g : copieSuivie.fonctions)
                if (g.id == f.id) g.esquisse = maj;
              r.esquissesSuivies.push_back({f.id, maj});
              if (cassees)
                r.avertissements.push_back({f.id, std::to_string(cassees) + " référence(s) à une arête de la pièce introuvable(s) : la géométrie reprise garde sa dernière position."});
            }
            const Profil pr = profil(f, ax);
            r.esquisses.push_back({f.id, pr.trace});
            r.etatsEsquisses.push_back({f.id, pr.erreur.empty() && !pr.faces.empty() ? 0 : (pr.erreur.find("aucun contour fermé") != std::string::npos || pr.erreur.find("aucun profil") != std::string::npos ? 1 : 2)});
            if (!pr.traceConstruction.IsNull()) r.esquissesConstruction.push_back({f.id, pr.traceConstruction});
            break;
          }
          case TypeFonction::Extrusion:
          case TypeFonction::Revolution: {
            const FonctionPiece* fe = esquisseUtilisable(f.esquisseId, f.id);
            if (!fe) break;
            const gp_Ax3 ax = r.repere(fe->id);
            Profil pr = profil(*fe, ax);
            bool ouvert = false;
            if (!pr.erreur.empty()) {
              // Contour ouvert : seule une fonction mince sait l'épaissir (FO-11)
              TopoDS_Face bande;
              std::string e2;
              if (f.mince && mince::bandeOuverte(fe->esquisse, ax, f.epaisseurMince, f.sensMince, bande, e2)) ouvert = true;
              if (!ouvert) { r.erreurs.push_back({f.id, fe->nom + " : " + pr.erreur + (f.mince && !e2.empty() ? " " + e2 : "")}); break; }
              pr.faces = {bande};
              pr.cles = {""};
              pr.parDefaut = {true};
              pr.interieurs = {gp_Pnt2d(0, 0)};
              pr.origine.clear();
            }
            if (f.enlever && corps.IsNull()) { r.erreurs.push_back({f.id, "Rien à enlever : commence par une fonction qui ajoute de la matière."}); break; }
            // Régions retenues : celles choisies, sinon celles par défaut. Une région choisie dont la clé a disparu
            // (retouche de l'esquisse) est retrouvée par son point intérieur mémorisé, avec un avertissement ; sans
            // point, ou si plus aucune région ne le contient, la fonction échoue au lieu d'omettre la région (FO-06).
            std::vector<size_t> retenues;
            std::string erreurRegions;
            bool retrouvee = false;
            if (ouvert) retenues = {0};
            else if (f.regions.empty()) {
              for (size_t i = 0; i < pr.faces.size(); ++i)
                if (pr.parDefaut[i]) retenues.push_back(i);
            } else {
              int perdues = 0;
              for (size_t k = 0; k < f.regions.size(); ++k) {
                const auto it = std::find(pr.cles.begin(), pr.cles.end(), f.regions[k]);
                if (it != pr.cles.end()) { retenues.push_back(static_cast<size_t>(it - pr.cles.begin())); continue; }
                bool trouve = false;
                if (k < f.pointsRegions.size()) {
                  const gp_Pnt q3 = vers3D(ax, f.pointsRegions[k][0], f.pointsRegions[k][1]);
                  for (size_t i = 0; i < pr.faces.size() && !trouve; ++i) {
                    BRepClass_FaceClassifier cl(pr.faces[i], q3, 1e-7);
                    if (cl.State() == TopAbs_IN) { retenues.push_back(i); trouve = true; }
                  }
                }
                if (trouve) retrouvee = true;
                else ++perdues;
              }
              if (perdues > 0)
                erreurRegions = std::to_string(perdues) + (perdues > 1 ? " régions choisies n'existent plus" : " région choisie n'existe plus") +
                                " dans l'esquisse (sa forme a changé) : choisis à nouveau les contours à utiliser.";
              std::sort(retenues.begin(), retenues.end());
              retenues.erase(std::unique(retenues.begin(), retenues.end()), retenues.end());
            }
            if (!erreurRegions.empty()) { r.erreurs.push_back({f.id, erreurRegions}); break; }
            if (retenues.empty()) { r.erreurs.push_back({f.id, "Aucun contour retenu : choisis au moins une région de l'esquisse."}); break; }
            if (retrouvee) r.avertissements.push_back({f.id, "Une région choisie a été retrouvée par sa position après une retouche de l'esquisse : vérifie le résultat."});
            Outil outil;
            bool echec = false;
            for (size_t k = 0; k < retenues.size() && !echec; ++k) {
              const size_t i = retenues[k];
              // Noms par identité : la clé de la région (ses éléments), pas son rang, pour qu'une région supprimée
              // ne fasse pas glisser les références vers sa voisine (FO-05) ; aucun suffixe s'il n'y a qu'une région.
              const std::string suffixeRegion = retenues.size() > 1 ? "." + jetonRegion(pr.cles[i]) : std::string();
              std::vector<TopoDS_Face> morceauxFace = {pr.faces[i]};
              std::vector<std::pair<TopoDS_Edge, int>> origine;
              if (f.mince && !ouvert) {
                std::string e2;
                if (!mince::bandeRegion(pr.faces[i], ax, f.epaisseurMince, f.sensMince, f.minceArrondi, morceauxFace, e2)) { r.erreurs.push_back({f.id, e2}); echec = true; break; }
              } else if (!f.mince) {
                for (const auto& [ed, el] : pr.origine)
                  for (TopExp_Explorer ex(pr.faces[i], TopAbs_EDGE); ex.More(); ex.Next()) if (ex.Current().IsSame(ed)) { origine.push_back({ed, el}); break; }
              }
              for (size_t j = 0; j < morceauxFace.size() && !echec; ++j) {
                const std::string suffixe = suffixeRegion + (morceauxFace.size() > 1 ? ".m" + std::to_string(j) : std::string());
                TopoDS_Face face = morceauxFace[j];
                Outil morceau;
                if (f.type == TypeFonction::Extrusion) morceau = extruder(f, face, origine, ax, corps, noms, id, suffixe);
                else if (!f.axeBase.empty() && f.axeBase != "X" && f.axeBase != "Y" && f.axeBase != "Z") {
                  gp_Ax1 axeChoisi;
                  if (!resoudreAxe(f.axeBase, axeChoisi)) { r.erreurs.push_back({f.id, "L'axe de révolution choisi n'existe plus : choisis-en un autre."}); echec = true; break; }
                  morceau = revolutionner(f, *fe, face, origine, ax, id, suffixe, &axeChoisi);
                } else
                  morceau = revolutionner(f, *fe, face, origine, ax, id, suffixe);
                if (!morceau.erreur.empty()) { r.erreurs.push_back({f.id, morceau.erreur}); echec = true; break; }
                outil = reunir(outil, morceau);
                if (!outil.erreur.empty()) { r.erreurs.push_back({f.id, outil.erreur}); echec = true; break; }
              }
            }
            if (echec || outil.forme.IsNull()) {
              if (!echec) r.erreurs.push_back({f.id, "Aucun volume construit à partir des contours retenus."});
              break;
            }
            outils[f.id] = {outil.forme, f.enlever, outil.noms};
            if (!appliquer(outil.forme, outil.noms, f.enlever, f.id)) break;
            nommerReste(corps, noms, f.id);
            break;
          }
          case TypeFonction::Tole: {
            const FonctionPiece* fe = esquisseUtilisable(f.esquisseId, f.id, false, "l'esquisse du contour de la tôle");
            if (!fe) break;
            if (tole.actif()) { r.erreurs.push_back({f.id, "Une pièce n'a qu'une tôle de base : ajoute des bords tombés à celle-ci."}); break; }
            const gp_Ax3 ax = r.repere(fe->id);
            const Profil pr = profil(*fe, ax);
            if (!pr.erreur.empty()) { r.erreurs.push_back({f.id, fe->nom + " : " + pr.erreur + " Une tôle de base part d'un contour fermé."}); break; }
            std::vector<size_t> retenues;
            if (f.regions.empty()) {
              for (size_t i = 0; i < pr.faces.size(); ++i)
                if (pr.parDefaut[i]) retenues.push_back(i);
            } else
              for (const auto& cle : f.regions)
                if (const auto it = std::find(pr.cles.begin(), pr.cles.end(), cle); it != pr.cles.end()) retenues.push_back(static_cast<size_t>(it - pr.cles.begin()));
            if (retenues.empty()) { r.erreurs.push_back({f.id, "Aucun contour retenu pour la tôle : choisis une région de l'esquisse."}); break; }
            // La plaque est une extrusion de l'épaisseur : mêmes noms de faces (côtés « id:cote:élément »)
            FonctionPiece fx = f;
            fx.type = TypeFonction::Extrusion;
            fx.longueur = f.epaisseur;
            fx.fin = "borgne";
            fx.fin2.clear();
            fx.symetrique = fx.traversant = fx.mince = fx.enlever = false;
            fx.depouille = 0;
            Outil outil;
            std::vector<TopoDS_Face> faces;
            std::vector<std::pair<TopoDS_Edge, int>> origineRetenue;
            bool echec = false;
            for (size_t k = 0; k < retenues.size() && !echec; ++k) {
              const size_t i = retenues[k];
              std::vector<std::pair<TopoDS_Edge, int>> origine;
              for (const auto& [ed, el] : pr.origine)
                for (TopExp_Explorer ex(pr.faces[i], TopAbs_EDGE); ex.More(); ex.Next())
                  if (ex.Current().IsSame(ed)) { origine.push_back({ed, el}); break; }
              origineRetenue.insert(origineRetenue.end(), origine.begin(), origine.end());
              faces.push_back(pr.faces[i]);
              const std::string suffixe = retenues.size() > 1 ? "." + jetonRegion(pr.cles[i]) : std::string();
              Outil morceau = extruder(fx, pr.faces[i], origine, ax, corps, noms, id, suffixe);
              if (!morceau.erreur.empty()) { r.erreurs.push_back({f.id, morceau.erreur}); echec = true; break; }
              outil = reunir(outil, morceau);
            }
            if (echec || outil.forme.IsNull()) break;
            if (const std::string e = tole.poserBase(f, faces, origineRetenue, ax); !e.empty()) { r.erreurs.push_back({f.id, e}); break; }
            if (!appliquer(outil.forme, outil.noms, false, f.id)) break;
            nommerReste(corps, noms, f.id);
            break;
          }
          case TypeFonction::Nervure: {
            const FonctionPiece* fe = esquisseUtilisable(f.esquisseId, f.id, false, "l'esquisse du trait de la nervure");
            if (!fe) break;
            if (corps.IsNull()) { r.erreurs.push_back({f.id, "Une nervure s'appuie sur la pièce : construis d'abord le volume qu'elle raidit."}); break; }
            if (!(f.epaisseur > 0)) { r.erreurs.push_back({f.id, "L'épaisseur de la nervure doit être positive."}); break; }
            const gp_Ax3 ax = r.repere(fe->id);
            const Esquisse& e = fe->esquisse;
            const gp_Dir n = ax.Direction();
            auto P = [&](int pid) { const PointEsquisse* q = e.point(pid); return q ? gp_Pnt(ax.Location().Translated(gp_Vec(ax.XDirection()) * q->x + gp_Vec(ax.YDirection()) * q->y)) : ax.Location(); };
            Outil outil;
            if (f.mode == "normale") {
              // Bande de la nervure dans le plan, poussée le long de la normale jusqu'à la pièce
              TopoDS_Face bande;
              std::string err;
              if (!mince::bandeOuverte(e, ax, f.epaisseur, f.sensMince.empty() ? std::string("milieu") : f.sensMince, bande, err)) {
                r.erreurs.push_back({f.id, err.empty() ? std::string("Le trait de la nervure doit être un contour ouvert d'un seul tenant.") : err});
                break;
              }
              FonctionPiece fx = f;
              fx.type = TypeFonction::Extrusion;
              fx.fin = "prochain";
              fx.fin2.clear();
              fx.enlever = fx.mince = fx.symetrique = fx.traversant = false;
              fx.depouille = 0;
              outil = extruder(fx, bande, {}, ax, corps, noms, id, "");
              if (!outil.erreur.empty()) {
                // Dans le vide de ce côté : l'autre sens, en le disant
                fx.inverse = !fx.inverse;
                Outil autre = extruder(fx, bande, {}, ax, corps, noms, id, "");
                if (!autre.erreur.empty()) { r.erreurs.push_back({f.id, "La nervure ne rencontre la pièce dans aucun sens : prolonge le trait ou déplace l'esquisse."}); break; }
                outil = autre;
                r.avertissements.push_back({f.id, "La nervure partait dans le vide : elle a été dirigée vers la pièce (coche « Inverser » pour figer ce sens)."});
              }
            } else {
              // Parallèle au plan : la région fermée entre le trait (prolongé à ses bouts) et la pièce
              const double grand = 4 * diagonale(corps) + 50;
              BRep_Builder bt;
              TopoDS_Compound trait;
              bt.MakeCompound(trait);
              std::map<int, int> usage;
              for (const auto& el : e.elements)
                if (!el.construction && e.elementValide(el)) {
                  if (el.type == TypeElement::Cercle) continue;
                  ++usage[el.p1];
                  ++usage[el.p2];
                }
              std::vector<TopoDS_Edge> morceaux;
              bool cercle = false;
              for (const auto& el : e.elements) {
                if (el.construction || !e.elementValide(el)) continue;
                if (el.type == TypeElement::Cercle) { cercle = true; continue; }
                gp_Pnt a = P(el.p1), b = P(el.p2);
                if (a.Distance(b) < 1e-9) continue;
                if (el.type == TypeElement::Ligne) {
                  const gp_Vec u(a, b);
                  const gp_Vec un = u.Normalized();
                  if (usage[el.p1] == 1) a.Translate(un * -grand);
                  if (usage[el.p2] == 1) b.Translate(un * grand);
                  morceaux.push_back(BRepBuilderAPI_MakeEdge(a, b).Edge());
                } else {
                  const gp_Pnt c = P(el.centre);
                  const gp_Circ ci(gp_Ax2(c, n, gp_Dir(gp_Vec(c, a))), c.Distance(a));
                  morceaux.push_back(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(ci, a, b, true).Value()).Edge());
                }
              }
              if (morceaux.empty() || cercle) { r.erreurs.push_back({f.id, "Le trait de la nervure doit être une ligne (ou une suite de lignes et d'arcs) ouverte."}); break; }
              const TopoDS_Face cadre = BRepBuilderAPI_MakeFace(gp_Pln(ax), -grand, grand, -grand, grand).Face();
              BRepAlgoAPI_Cut hors(cadre, corps);
              if (!hors.IsDone() || hors.HasErrors()) { r.erreurs.push_back({f.id, "Le plan de l'esquisse n'a pas pu être coupé par la pièce."}); break; }
              BRepAlgoAPI_Splitter decoupe;
              NCollection_List<TopoDS_Shape> args, outilsDecoupe;
              args.Append(hors.Shape());
              for (const auto& m : morceaux) outilsDecoupe.Append(m);
              decoupe.SetArguments(args);
              decoupe.SetTools(outilsDecoupe);
              decoupe.Build();
              if (!decoupe.IsDone() || decoupe.HasErrors()) { r.erreurs.push_back({f.id, "Le trait de la nervure n'a pas pu découper le plan."}); break; }
              // Régions fermées (loin du cadre) qui longent le trait, de la plus petite à la plus grande
              std::vector<std::pair<double, TopoDS_Face>> candidates;
              for (TopExp_Explorer ex(decoupe.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
                const TopoDS_Face fc = TopoDS::Face(ex.Current());
                Bnd_Box b;
                BRepBndLib::Add(fc, b);
                double x0, y0, z0, x1, y1, z1;
                b.Get(x0, y0, z0, x1, y1, z1);
                if (std::max({std::abs(x0), std::abs(y0), std::abs(z0), std::abs(x1), std::abs(y1), std::abs(z1)}) > 0.9 * grand + gp_Vec(gp::Origin(), ax.Location()).Magnitude()) continue;
                bool longe = false;
                for (TopExp_Explorer ee(fc, TopAbs_EDGE); ee.More() && !longe; ee.Next()) {
                  BRepAdaptor_Curve cv(TopoDS::Edge(ee.Current()));
                  const gp_Pnt m = cv.Value((cv.FirstParameter() + cv.LastParameter()) / 2);
                  for (const auto& mo : morceaux) {
                    BRepExtrema_DistShapeShape dd(BRepBuilderAPI_MakeVertex(m).Vertex(), mo);
                    if (dd.IsDone() && dd.Value() < 1e-6) { longe = true; break; }
                  }
                }
                if (!longe) continue;
                GProp_GProps g;
                BRepGProp::SurfaceProperties(fc, g);
                candidates.push_back({g.Mass(), fc});
              }
              std::sort(candidates.begin(), candidates.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
              const std::size_t choix = f.inverse ? 1 : 0;
              if (candidates.size() <= choix) {
                r.erreurs.push_back({f.id, candidates.empty() ? "La nervure ne ferme aucune région avec la pièce : prolonge le trait jusqu'aux parois, ou pose l'esquisse dans le plan des parois."
                                                              : "Il n'y a pas d'autre côté à remplir : décoche « Inverser »."});
                break;
              }
              TopoDS_Face region = candidates[choix].second;
              const std::string sens = f.sensMince.empty() ? std::string("milieu") : f.sensMince;
              const double t0 = sens == "milieu" ? -f.epaisseur / 2 : sens == "interieur" ? -f.epaisseur : 0;
              gp_Trsf tr;
              tr.SetTranslation(gp_Vec(n) * t0);
              region = TopoDS::Face(BRepBuilderAPI_Transform(region, tr, true).Shape());
              BRepPrimAPI_MakePrism pr(region, gp_Vec(n) * f.epaisseur);
              if (!pr.IsDone()) { r.erreurs.push_back({f.id, "Le volume de la nervure n'a pas pu être construit."}); break; }
              outil.forme = pr.Shape();
              outil.noms.push_back({TopoDS::Face(pr.FirstShape()), id + ":flanc1"});
              outil.noms.push_back({TopoDS::Face(pr.LastShape()), id + ":flanc2"});
            }
            if (!appliquer(outil.forme, outil.noms, false, f.id)) break;
            outils[f.id] = {outil.forme, false, outil.noms};
            nommerReste(corps, noms, f.id);
            break;
          }
          case TypeFonction::Filetage: {
            if (corps.IsNull()) { r.erreurs.push_back({f.id, "Un filetage se pose sur un trou ou un arbre de la pièce."}); break; }
            if (f.references.empty()) { r.erreurs.push_back({f.id, "Choisis la face cylindrique à fileter (trou ou arbre)."}); break; }
            std::vector<TopoDS_Face> fs;
            if (!etatCourant().resoudreFaces(f.references.front(), fs, false).trouvee()) { r.erreurs.push_back({f.id, "La face à fileter n'existe plus."}); break; }
            BRepAdaptor_Surface sf(fs.front());
            if (sf.GetType() != GeomAbs_Cylinder) { r.erreurs.push_back({f.id, "Choisis une face cylindrique (trou ou arbre)."}); break; }
            const gp_Cylinder cy = sf.Cylinder();
            const gp_Ax1 axe = cy.Axis();
            const double rayon = cy.Radius();
            // Étendue de la face le long de l'axe
            double tmin = 1e300, tmax = -1e300;
            for (const auto& fc : fs) {
              Bnd_Box b;
              BRepBndLib::Add(fc, b);
              double x0, y0, z0, x1, y1, z1;
              b.Get(x0, y0, z0, x1, y1, z1);
              for (double x : {x0, x1})
                for (double y : {y0, y1})
                  for (double z : {z0, z1}) {
                    const double tt = gp_Vec(axe.Location(), gp_Pnt(x, y, z)).Dot(gp_Vec(axe.Direction()));
                    tmin = std::min(tmin, tt);
                    tmax = std::max(tmax, tt);
                  }
            }
            // Arbre ou trou : la matière est-elle à l'intérieur du cylindre (arbre) ou autour (trou) ?
            auto dansMatiere = [&](const gp_Pnt& q) {
              BRepClass3d_SolidClassifier cl(corps, q, 1e-7);
              return cl.State() == TopAbs_IN;
            };
            const double tmil = (tmin + tmax) / 2;
            gp_Pnt surFace;
            {
              const double u = (sf.FirstUParameter() + sf.LastUParameter()) / 2, v = (sf.FirstVParameter() + sf.LastVParameter()) / 2;
              surFace = sf.Value(u, v);
            }
            const gp_Pnt surAxe = axe.Location().Translated(gp_Vec(axe.Direction()) * gp_Vec(axe.Location(), surFace).Dot(gp_Vec(axe.Direction())));
            const gp_Vec radial = gp_Vec(surAxe, surFace).Normalized();
            const double eps = std::min(0.05, 0.1 * rayon);
            const bool exterieur = dansMatiere(surFace.Translated(radial * -eps)) || !dansMatiere(surFace.Translated(radial * eps));
            (void)tmil;
            const std::string norme = f.norme.empty() ? std::string("ISO") : f.norme;
            const DonneesTrou* d = (!f.taille.empty() && f.taille != "auto") ? donneesTrou(norme, f.taille) : filetagePour(2 * rayon, exterieur, norme);
            ResultatPiece::Filetage ft;
            ft.fonction = f.id;
            ft.exterieur = exterieur;
            ft.pas = f.pasFiletage > 0 ? f.pasFiletage : d ? d->pas : 0;
            if (!(ft.pas > 0)) {
              r.erreurs.push_back({f.id, "Ø" + std::to_string(2 * rayon).substr(0, 5) + " mm : aucune taille normalisée ne correspond ; choisis une taille ou indique le pas."});
              break;
            }
            ft.taille = d ? d->taille : "M" + std::to_string(static_cast<int>(std::lround(2 * rayon)));
            if (exterieur) {
              ft.nominal = 2 * rayon;
              ft.mineur = 2 * rayon - 1.2269 * ft.pas;
            } else {
              ft.nominal = d ? diametreNominal(*d) : 2 * rayon + 1.0825 * ft.pas;
              ft.mineur = 2 * rayon;
            }
            const double etendue = tmax - tmin;
            ft.longueur = f.longueurFiletee > 0 ? std::min(f.longueurFiletee, etendue) : etendue;
            // Le filet part d'un bout libre (l'air au-delà : bout d'arbre, entrée de trou) ; si les deux le sont, du plus
            // haut (Z, puis Y, puis X) ; « inverse » : de l'autre
            auto boutLibre = [&](double tt, double sens) {
              const gp_Pnt bout = axe.Location().Translated(gp_Vec(axe.Direction()) * (tt + sens * std::max(0.05, 0.02 * rayon)));
              return !dansMatiere(exterieur ? bout.Translated(radial * (-0.5 * rayon)) : bout);
            };
            const bool libreMax = boutLibre(tmax, 1), libreMin = boutLibre(tmin, -1);
            bool depuisFin = libreMax;
            if (libreMax == libreMin) {
              const gp_Dir& dd = axe.Direction();
              depuisFin = std::abs(dd.Z()) > 1e-9 ? dd.Z() > 0 : std::abs(dd.Y()) > 1e-9 ? dd.Y() > 0 : dd.X() > 0;
            }
            if (f.inverse) depuisFin = !depuisFin;
            const gp_Pnt depart = axe.Location().Translated(gp_Vec(axe.Direction()) * (depuisFin ? tmax : tmin));
            ft.axes.push_back(gp_Ax1(depart, depuisFin ? axe.Direction().Reversed() : axe.Direction()));
            ft.designation = designationFiletage(ft.taille, ft.pas, norme);
            if (d && d->pas != ft.pas) ft.designation = designationFiletage(ft.taille, ft.pas, "ISO");
            if (exterieur && d && std::abs(diametreNominal(*d) - 2 * rayon) > 0.05)
              r.avertissements.push_back({f.id, "L'arbre mesure Ø" + std::to_string(2 * rayon).substr(0, 5) + " : la taille la plus proche est " + d->taille + "."});
            r.filetages.push_back(ft);
            break;
          }
          case TypeFonction::BordTombe: {
            tolerie::OutilTole o = tole.bordTombe(f, corps);
            if (!o.erreur.empty()) { r.erreurs.push_back({f.id, o.erreur}); break; }
            if (!o.retrait.IsNull() && !appliquer(o.retrait, o.nomsRetrait, true, f.id, -1)) break;
            if (!appliquer(o.ajout, o.nomsAjout, false, f.id)) break;
            tole.valider();
            nommerReste(corps, noms, f.id);
            break;
          }
          case TypeFonction::Plan: {
            gp_Ax3 base;
            gp_Ax3 res;
            bool ok = false;
            if (f.mode == "troisPoints") {
              gp_Pnt a, b, c;
              if (f.supports.size() >= 3 && resoudrePoint(f.supports[0], a) && resoudrePoint(f.supports[1], b) && resoudrePoint(f.supports[2], c)) {
                const gp_Vec ab(a, b), ac(a, c);
                const gp_Vec n = ab.Crossed(ac);
                if (n.Magnitude() > 1e-9 && ab.Magnitude() > 1e-9) { res = gp_Ax3(a, gp_Dir(n), gp_Dir(ab)); ok = true; }
              }
            } else if (!f.supports.empty() && resoudrePlan(f.supports[0], base)) {
              res = base;
              ok = true;
              if (f.mode == "angle") {
                gp_Ax1 a;
                if (f.supports.size() < 2 || !resoudreAxe(f.supports[1], a)) ok = false;
                else res.Rotate(a, f.angle * std::numbers::pi / 180 * (f.inverse ? -1 : 1));
              } else if (f.mode == "parallelePoint") {
                gp_Pnt q;
                if (f.supports.size() < 2 || !resoudrePoint(f.supports[1], q)) ok = false;
                else res.SetLocation(q);
              } else {
                res.Translate(gp_Vec(base.Direction()) * f.distance * (f.inverse ? -1 : 1));
              }
            }
            if (!ok) { r.erreurs.push_back({f.id, "Choisis les références du plan (plan ou face, et selon le cas une arête, un axe ou des sommets)."}); break; }
            plansRef[f.id] = res;
            r.plansReference.push_back({f.id, res});
            break;
          }
          case TypeFonction::Axe: {
            gp_Ax1 a;
            bool ok = false;
            if (f.mode == "deuxPoints") {
              gp_Pnt p1, p2;
              if (f.supports.size() >= 2 && resoudrePoint(f.supports[0], p1) && resoudrePoint(f.supports[1], p2) && p1.Distance(p2) > 1e-9) { a = gp_Ax1(p1, gp_Dir(gp_Vec(p1, p2))); ok = true; }
            } else if (f.mode == "deuxPlans") {
              gp_Ax3 a1, a2;
              if (f.supports.size() >= 2 && resoudrePlan(f.supports[0], a1) && resoudrePlan(f.supports[1], a2)) {
                const gp_Vec d = gp_Vec(a1.Direction()).Crossed(gp_Vec(a2.Direction()));
                if (d.Magnitude() > 1e-9) {
                  const gp_Pln p1(a1), p2(a2);
                  IntAna_QuadQuadGeo inter(p1, p2, 1e-9, 1e-9);
                  if (inter.IsDone() && inter.NbSolutions() > 0) { a = gp_Ax1(inter.Line(1).Location(), inter.Line(1).Direction()); ok = true; }
                }
              }
            } else if (!f.supports.empty()) {
              ok = resoudreAxe(f.supports[0], a);
            }
            if (!ok) { r.erreurs.push_back({f.id, "Choisis la référence de l'axe (arête droite, face cylindrique, deux sommets ou deux plans)."}); break; }
            axesRef[f.id] = a;
            r.axesReference.push_back({f.id, a});
            break;
          }
          case TypeFonction::Point: {
            gp_Pnt q(f.x, f.y, f.z);
            bool ok = true;
            if (f.mode == "sommet") ok = !f.supports.empty() && resoudrePoint(f.supports[0], q);
            else if (f.mode == "centre") {
              ok = false;
              const ResultatPiece& e = etatCourant();
              if (!f.supports.empty()) {
                if (f.supports[0].find('|') != std::string::npos) {
                  std::vector<TopoDS_Edge> as;
                  if (e.resoudreAretes(f.supports[0], as).trouvee() && as.size() == 1) {
                    BRepAdaptor_Curve c(as.front());
                    if (c.GetType() == GeomAbs_Circle) { q = c.Circle().Location(); ok = true; }
                  }
                } else {
                  std::vector<TopoDS_Face> fs;
                  if (e.resoudreFaces(f.supports[0], fs, true).trouvee()) { GProp_GProps pr; BRepGProp::SurfaceProperties(fs.front(), pr); q = pr.CentreOfMass(); ok = true; }
                }
              }
            }
            if (!ok) { r.erreurs.push_back({f.id, "Choisis le sommet, l'arête circulaire ou la face qui donne le point."}); break; }
            pointsRef[f.id] = q;
            r.pointsReference.push_back({f.id, q});
            break;
          }
          case TypeFonction::Repere: {
            // Repère : origine (point), plan dont la normale donne Z, axe X facultatif ; sans support, le repère
            // global placé aux coordonnées x, y, z. Il sert de plan (« F:id »), d'axe (son Z) et de point (FO-17).
            gp_Pnt o(f.x, f.y, f.z);
            gp_Dir z(0, 0, 1);
            gp_Vec xv(1, 0, 0);
            bool ok = true;
            if (!f.supports.empty() && !f.supports[0].empty()) ok = resoudrePoint(f.supports[0], o);
            if (ok && f.supports.size() > 1 && !f.supports[1].empty()) {
              gp_Ax3 pl;
              ok = resoudrePlan(f.supports[1], pl);
              if (ok) { z = pl.Direction(); xv = gp_Vec(pl.XDirection()); }
            }
            if (ok && f.supports.size() > 2 && !f.supports[2].empty()) {
              gp_Ax1 a;
              ok = resoudreAxe(f.supports[2], a);
              if (ok) xv = gp_Vec(a.Direction());
            }
            if (f.inverse) z.Reverse();
            xv -= gp_Vec(z) * gp_Vec(z).Dot(xv);
            if (ok && xv.Magnitude() < 1e-9) { r.erreurs.push_back({f.id, "L'axe X du repère est parallèle à sa normale : choisis un autre axe."}); break; }
            if (!ok) { r.erreurs.push_back({f.id, "Choisis les références du repère : origine (sommet ou point), plan (normale Z), et au besoin l'axe X."}); break; }
            const gp_Ax3 rep(o, z, gp_Dir(xv));
            plansRef[f.id] = rep;
            axesRef[f.id] = gp_Ax1(o, z);
            pointsRef[f.id] = o;
            r.reperesReference.push_back({f.id, rep});
            break;
          }
          case TypeFonction::Percage: {
            const FonctionPiece* fe = esquisseUtilisable(f.esquisseId, f.id, false, "l'esquisse dont les points donnent les centres des trous");
            if (!fe) break;
            if (corps.IsNull()) { r.erreurs.push_back({f.id, "Il faut une pièce à percer."}); break; }
            const gp_Ax3 ax = r.repere(fe->id);
            // Centres : points libres de l'esquisse, sinon centres de ses cercles ; chacun garde l'identifiant de son
            // point, qui nomme les faces du trou (FO-05)
            std::vector<std::pair<int, gp_Pnt>> centres;
            for (const auto& pt : fe->esquisse.points) {
              if (pt.id == 0) continue;
              bool utilise = false;
              for (const auto& el : fe->esquisse.elements) if (el.p1 == pt.id || el.p2 == pt.id || el.centre == pt.id) utilise = true;
              if (!utilise) centres.push_back({pt.id, vers3D(ax, pt.x, pt.y)});
            }
            if (centres.empty())
              for (const auto& el : fe->esquisse.elements)
                if (el.type == TypeElement::Cercle && !el.construction)
                  if (const PointEsquisse* c = fe->esquisse.point(el.centre))
                    if (std::none_of(centres.begin(), centres.end(), [&](const auto& x) { return x.first == c->id; })) centres.push_back({c->id, vers3D(ax, c->x, c->y)});
            if (centres.empty()) { r.erreurs.push_back({f.id, "L'esquisse ne contient ni point ni cercle pour placer les trous."}); break; }
            // Dimensions du trou
            const DonneesTrou* d = donneesTrou(f.norme, f.taille);
            double rayon = f.diametre / 2, rLamage = 0, hLamage = 0, rFraisure = 0;
            if (f.typePercage != "simple") {
              if (!d) { r.erreurs.push_back({f.id, "Taille de vis inconnue pour cette norme."}); break; }
              const double passage = f.ajustement == "serre" ? d->passageSerre : f.ajustement == "large" ? d->passageLarge : d->passageNormal;
              if (f.typePercage == "taraude") rayon = d->forageTaraud / 2;
              else rayon = passage / 2;
              if (f.typePercage == "lame") { rLamage = d->lamageD / 2; hLamage = d->lamageH; }
              if (f.typePercage == "fraise") rFraisure = d->fraisureD / 2;
            }
            if (!(rayon > 0)) { r.erreurs.push_back({f.id, "Le diamètre du trou doit être positif."}); break; }
            // Fraisure : cône à l'angle de la norme (90° ISO, 82° pour les vis en pouces, FO-18)
            const double tanDemi = std::tan((d ? d->angleFraisure : 90.0) / 2 * std::numbers::pi / 180);
            const double hFraisure = rFraisure > rayon ? (rFraisure - rayon) / tanDemi : 0;
            // Un trou borgne doit dépasser son lamage ou sa fraisure, sinon son profil se replie sur lui-même (FO-26)
            if (!f.traversant && (hLamage > 0 || hFraisure > 0) && f.profondeur <= std::max(hLamage, hFraisure) + 1e-6) {
              r.erreurs.push_back({f.id, "La profondeur du trou doit dépasser celle du " + std::string(hLamage > 0 ? "lamage" : "cône de fraisure") + " (" +
                                         std::to_string(std::max(hLamage, hFraisure)).substr(0, 5) + " mm)."});
              break;
            }
            if (!f.traversant && !(f.profondeur > 0)) { r.erreurs.push_back({f.id, "La profondeur du trou doit être positive."}); break; }
            const double eps = 0.02;
            // Trous dans une direction : profil (rayon, profondeur) de chaque segment avec son rôle, révolution ;
            // faces nommées « trou.p<point>.<rôle> ». Renvoie faux (et le message) si un trou ne se construit pas.
            auto construireTrous = [&](const gp_Dir& dir, TopoDS_Compound& trous, Noms& nomsTrous, double& amorce, std::string& erreurTrou) -> bool {
              double prof = f.profondeur;
              if (f.traversant) {
                prof = 0;
                for (const auto& [pid, c] : centres) prof = std::max(prof, longueurUtile(corps, c, dir));
                if (prof <= 1e-6) prof = 1;  // pièce entièrement derrière : l'enlèvement le dira
              }
              std::vector<std::pair<std::pair<double, double>, const char*>> pr;  // point de départ du segment, rôle du segment
              const double rHaut = rLamage > 0 ? rLamage : rFraisure > rayon ? rFraisure + eps * tanDemi : rayon;
              pr.push_back({{0, -eps}, "entree"});
              if (rLamage > 0) {
                pr.push_back({{rLamage, -eps}, "lamage"});
                pr.push_back({{rLamage, hLamage}, "epaulement"});
                pr.push_back({{rayon, hLamage}, "paroi"});
              } else if (rFraisure > rayon) {
                pr.push_back({{rHaut, -eps}, "fraisure"});
                pr.push_back({{rayon, hFraisure}, "paroi"});
              } else {
                pr.push_back({{rayon, -eps}, "paroi"});
              }
              pr.push_back({{rayon, prof}, "fond"});
              pr.push_back({{0, f.pointeForet && !f.traversant ? prof + rayon / std::tan(59.0 * std::numbers::pi / 180) : prof}, "axe"});
              amorce = 0;
              BRep_Builder bt;
              bt.MakeCompound(trous);
              nomsTrous.clear();
              for (const auto& [pid, c] : centres) {
                const gp_Dir x = gp_Ax3(c, dir).XDirection();
                auto P3 = [&](const std::pair<double, double>& q) { return c.Translated(gp_Vec(x) * q.first + gp_Vec(dir) * q.second); };
                BRepBuilderAPI_MakePolygon poly;
                std::vector<const char*> roles;
                for (std::size_t k = 0; k < pr.size(); ++k) {
                  if (P3(pr[k].first).Distance(P3(pr[(k + 1) % pr.size()].first)) < 1e-9) continue;
                  poly.Add(P3(pr[k].first));
                  roles.push_back(pr[k].second);
                }
                poly.Close();
                if (!poly.IsDone()) { erreurTrou = "Le profil du trou au point " + std::to_string(pid) + " est incohérent."; return false; }
                // Arêtes du profil dans l'ordre des points, chacune avec son rôle
                std::vector<std::pair<TopoDS_Edge, const char*>> segments;
                std::size_t k = 0;
                for (BRepTools_WireExplorer we(poly.Wire()); we.More() && k < roles.size(); we.Next(), ++k) segments.push_back({we.Current(), roles[k]});
                const TopoDS_Face fa = BRepBuilderAPI_MakeFace(poly.Wire(), true).Face();
                BRepPrimAPI_MakeRevol rev(fa, gp_Ax1(c, dir), 2 * std::numbers::pi);
                if (!rev.IsDone()) { erreurTrou = "Le trou au point " + std::to_string(pid) + " n'a pas pu être construit."; return false; }
                bt.Add(trous, rev.Shape());
                for (const auto& [ed, role] : segments)
                  for (const TopoDS_Shape& g : rev.Generated(ed))
                    if (g.ShapeType() == TopAbs_FACE) nomsTrous.push_back({TopoDS::Face(g), id + ":trou.p" + std::to_string(pid) + "." + role});
                amorce += std::numbers::pi * rHaut * rHaut * eps;
              }
              return true;
            };
            // Volume que le perçage retirerait dans une direction (sans toucher au corps)
            auto retrait = [&](const TopoDS_Shape& trous) {
              BRepAlgoAPI_Cut essai(corps, trous);
              return essai.IsDone() && !essai.HasErrors() ? volume(corps) - volume(essai.Shape()) : 0.0;
            };
            gp_Dir dir = ax.Direction().Reversed();  // vers l'intérieur de la matière
            if (f.inverse) dir.Reverse();
            TopoDS_Compound trous;
            Noms nomsTrous;
            double amorce = 0;
            std::string erreurTrou;
            if (!construireTrous(dir, trous, nomsTrous, amorce, erreurTrou)) { r.erreurs.push_back({f.id, erreurTrou}); break; }
            // Esquisse sur un plan (pas sur une face) dont le sens part dans le vide : on perce vers la matière, en le
            // disant (FO-13). Sur une face, le sens est celui de la face : pas de retournement silencieux.
            if (fe->face.empty() && retrait(trous) <= amorce * (1 + 1e-6) + 1e-9) {
              TopoDS_Compound autres;
              Noms nomsAutres;
              double amorce2 = 0;
              if (construireTrous(dir.Reversed(), autres, nomsAutres, amorce2, erreurTrou) && retrait(autres) > amorce2 * (1 + 1e-6) + 1e-9) {
                trous = autres;
                nomsTrous = nomsAutres;
                amorce = amorce2;
                r.avertissements.push_back({f.id, "Le perçage partait dans le vide : il a été dirigé vers la matière (coche « Inverser » pour figer ce sens)."});
              }
            }
            if (!appliquer(trous, nomsTrous, true, f.id, amorce)) {
              for (auto& [fid, m] : r.erreurs)
                if (fid == f.id && m.find("ne retire aucune matière") != std::string::npos)
                  m = "Le perçage ne traverse aucune matière : vérifie que l'esquisse est sur la pièce (bonne face), ou inverse le sens.";
              break;
            }
            outils[f.id] = {trous, true, nomsTrous};
            if (f.typePercage == "taraude" && d) {
              const double prof = f.traversant ? longueurUtile(corpsAvant, centres.front().second, dir) : f.profondeur;
              ResultatPiece::Filetage ft;
              ft.fonction = f.id;
              ft.taille = f.taille;
              ft.pas = d->pas;
              ft.longueur = f.longueurFiletee > 0 ? std::min(f.longueurFiletee, prof) : prof;
              ft.nombre = static_cast<int>(centres.size());
              for (const auto& [pid, c] : centres) ft.axes.push_back(gp_Ax1(c, dir));
              ft.nominal = diametreNominal(*d);
              ft.mineur = d->forageTaraud;
              ft.designation = designationFiletage(f.taille, d->pas, f.norme);
              r.filetages.push_back(ft);
            }
            nommerReste(corps, noms, f.id);
            break;
          }
          case TypeFonction::Repetition:
          case TypeFonction::Symetrie: {
            if (corps.IsNull()) { r.erreurs.push_back({f.id, "Il n'y a encore rien à répéter."}); break; }
            // Chaque copie porte un nom tiré de sa place dans la répétition, pas de son rang : indices (i, j) d'une
            // répétition linéaire à deux directions, point d'esquisse, rang angulaire (FO-05).
            std::vector<std::pair<gp_Trsf, std::string>> copies;
            bool ok = true;
            if (f.type == TypeFonction::Symetrie) {
              gp_Ax3 pl;
              if (f.supports.empty() || !resoudrePlan(f.supports[0], pl)) { r.erreurs.push_back({f.id, "Choisis le plan de symétrie (plan de base, plan de référence ou face plane)."}); break; }
              gp_Trsf t;
              t.SetMirror(gp_Ax2(pl.Location(), pl.Direction(), pl.XDirection()));
              copies.push_back({t, "0"});
            } else if (f.typeRepetition == "circulaire") {
              gp_Ax1 a;
              if (!resoudreAxe(f.direction1, a)) { r.erreurs.push_back({f.id, "Choisis l'axe de la répétition (X, Y, Z, une arête droite ou une face cylindrique)."}); break; }
              const int n = std::max(2, f.nombre1);
              const double total = f.angle >= 359.999 ? 2 * std::numbers::pi : f.angle * std::numbers::pi / 180;
              const double pas = f.angle >= 359.999 ? total / n : total / (n - 1);
              for (int k = 1; k < n; ++k) { gp_Trsf t; t.SetRotation(a, (f.inverse1 ? -1 : 1) * pas * k); copies.push_back({t, std::to_string(k - 1)}); }
            } else if (f.typeRepetition == "tableau") {
              for (std::size_t k = 0; k < f.tableau.size(); ++k) {
                gp_Trsf t;
                t.SetTranslation(gp_Vec(f.tableau[k][0], f.tableau[k][1], f.tableau[k][2]));
                copies.push_back({t, std::to_string(k)});
              }
            } else if (f.typeRepetition == "esquisse") {
              const FonctionPiece* fe = esquisseUtilisable(f.esquisseId, f.id, false, "l'esquisse dont les points placent les copies");
              if (!fe) break;
              const gp_Ax3 ax = r.repere(fe->id);
              std::vector<std::pair<int, gp_Pnt>> pts;
              for (const auto& pt : fe->esquisse.points)
                if (pt.id != 0) pts.push_back({pt.id, vers3D(ax, pt.x, pt.y)});
              for (std::size_t k = 1; k < pts.size(); ++k) {
                gp_Trsf t;
                t.SetTranslation(gp_Vec(pts[0].second, pts[k].second));
                copies.push_back({t, "p" + std::to_string(pts[k].first)});
              }
            } else {
              gp_Ax1 a1;
              if (!resoudreAxe(f.direction1, a1)) { r.erreurs.push_back({f.id, "Choisis la direction de la répétition (X, Y, Z ou une arête droite)."}); break; }
              const gp_Vec d1 = gp_Vec(a1.Direction()) * (f.inverse1 ? -1 : 1);
              gp_Vec d2;
              int n2 = 1;
              if (!f.direction2.empty()) {
                gp_Ax1 a2;
                // Seconde direction renseignée mais introuvable : erreur, pas une simple rangée (FO-16)
                if (!resoudreAxe(f.direction2, a2)) { r.erreurs.push_back({f.id, "La seconde direction de la répétition est introuvable : choisis-la à nouveau ou retire-la."}); break; }
                d2 = gp_Vec(a2.Direction()) * (f.inverse2 ? -1 : 1);
                n2 = std::max(1, f.nombre2);
              }
              const bool deuxDirections = !f.direction2.empty();
              for (int i = 0; i < std::max(1, f.nombre1); ++i)
                for (int j = 0; j < n2; ++j) {
                  if (i == 0 && j == 0) continue;
                  gp_Trsf t;
                  t.SetTranslation(d1 * (f.pas1 * i) + d2 * (f.pas2 * j));
                  copies.push_back({t, deuxDirections ? std::to_string(i) + "x" + std::to_string(j) : std::to_string(i - 1)});
                }
            }
            if (!ok) break;
            if (copies.empty()) { r.erreurs.push_back({f.id, "Aucune copie à faire : vérifie le nombre d'exemplaires."}); break; }
            if (f.sources.empty() && f.type == TypeFonction::Symetrie) {
              // Symétrie de tout le corps
              OutilMemo tout{corps, false, noms};
              TopoDS_Shape forme;
              Noms nn;
              transformer(tout, copies[0].first, id + ":", forme, nn);
              ok = appliquer(forme, nn, false, f.id);
            } else {
              if (f.sources.empty()) { r.erreurs.push_back({f.id, "Choisis les fonctions à répéter (extrusions, enlèvements, révolutions, perçages, balayages, lissages)."}); break; }
              // Sources : chacune doit exister, être active, précéder la répétition et avoir un volume (FO-30)
              for (int s : f.sources) {
                if (outils.count(s)) continue;
                const FonctionPiece* fs = p.fonction(s);
                std::string m;
                if (!fs) m = "Une fonction à répéter n'existe plus : retire-la de la liste.";
                else if (fs->desactivee) m = "« " + fs->nom + " » est désactivée : elle ne peut pas être répétée.";
                else if (!rejouees.count(s)) m = "« " + fs->nom + " » est placée après cette fonction : elle doit la précéder.";
                else if (!r.erreur(s).empty()) m = "« " + fs->nom + " » est en échec : répare-la d'abord.";
                else m = "« " + fs->nom + " » ne peut pas être répétée : seules les fonctions qui ajoutent ou enlèvent un volume le peuvent.";
                r.erreurs.push_back({f.id, m});
                ok = false;
                break;
              }
              for (std::size_t k = 0; k < copies.size() && ok; ++k)
                for (int s : f.sources) {
                  const OutilMemo& o = outils.at(s);
                  TopoDS_Shape forme;
                  Noms nn;
                  transformer(o, copies[k].first, id + "." + copies[k].second + ":", forme, nn);
                  if (!appliquer(forme, nn, o.enlever, f.id, -1)) { ok = false; break; }
                }
            }
            if (ok) nommerReste(corps, noms, f.id);
            break;
          }
          case TypeFonction::Import: {
            const TopoDS_Shape forme = formeDepuisBrep(f.donnees);
            if (forme.IsNull()) { r.erreurs.push_back({f.id, "La géométrie importée est illisible."}); break; }
            Noms nn;
            int k = 0;
            for (TopExp_Explorer ex(forme, TopAbs_FACE); ex.More(); ex.Next()) nn.push_back({TopoDS::Face(ex.Current()), id + ":import." + std::to_string(k++)});
            if (nombreSolides(forme) == 0)
              r.avertissements.push_back({f.id, "La géométrie importée n'est pas un solide fermé (surfaces ou maillage ouvert) : pas de masse ni d'enlèvement possible."});
            if (corps.IsNull()) { corps = forme; noms = nn; }
            else if (!appliquer(forme, nn, false, f.id)) break;
            outils[f.id] = {forme, false, nn};
            break;
          }
          case TypeFonction::Esquisse3D: {
            if (f.points3d.size() < 2) { r.erreurs.push_back({f.id, "Il faut au moins deux points."}); break; }
            TopoDS_Wire fil;
            if (f.lisse && f.points3d.size() >= 2) {
              // Courbe lisse passant par les points
              Handle(TColgp_HArray1OfPnt) pts = new TColgp_HArray1OfPnt(1, static_cast<int>(f.points3d.size()));
              for (size_t k = 0; k < f.points3d.size(); ++k) pts->SetValue(static_cast<int>(k) + 1, gp_Pnt(f.points3d[k][0], f.points3d[k][1], f.points3d[k][2]));
              GeomAPI_Interpolate interp(pts, f.fermee, 1e-6);
              interp.Perform();
              if (!interp.IsDone()) { r.erreurs.push_back({f.id, "Courbe impossible : deux points sont confondus ?"}); break; }
              fil = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(interp.Curve()).Edge()).Wire();
            } else {
              BRepBuilderAPI_MakePolygon poly;
              for (const auto& q : f.points3d) poly.Add(gp_Pnt(q[0], q[1], q[2]));
              if (f.fermee && f.points3d.size() >= 3) poly.Close();
              if (!poly.IsDone()) { r.erreurs.push_back({f.id, "Deux points consécutifs sont confondus."}); break; }
              fil = poly.Wire();
            }
            chemins3d[f.id] = fil;
            r.esquisses.push_back({f.id, fil});
            r.etatsEsquisses.push_back({f.id, f.fermee ? 0 : 1});
            break;
          }
          case TypeFonction::Balayage:
          case TypeFonction::Lissage: {
            // Profils : faces des esquisses (régions par défaut) ; chemin : arêtes de l'esquisse du chemin, reliées.
            // Profil d'une esquisse utilisable (FO-04, FO-19) : sa première région retenue par défaut
            auto faceDe = [&](int idEsq, TopoDS_Face& fc, const char* role) -> bool {
              const FonctionPiece* fe = esquisseUtilisable(idEsq, f.id, false, role);
              if (!fe) return false;
              const Profil pr = profil(*fe, r.repere(fe->id));
              for (size_t k = 0; k < pr.faces.size(); ++k)
                if (pr.parDefaut[k]) { fc = pr.faces[k]; return true; }
              r.erreurs.push_back({f.id, "L'esquisse « " + fe->nom + " » ne contient aucun contour fermé pour servir de profil."});
              return false;
            };
            TopoDS_Shape outil;
            if (f.type == TypeFonction::Balayage) {
              TopoDS_Face profilFace;
              const bool tube = f.tube;
              if (!tube && !faceDe(f.esquisseId, profilFace, "l'esquisse du profil (un contour fermé)")) break;
              if (tube && !(f.diametre > 0)) { r.erreurs.push_back({f.id, "Le diamètre du tube doit être positif."}); break; }
              // Épaisseur d'un tube creux : strictement inférieure au rayon ; 0 donne un tube plein (AP-24)
              if (tube && (f.epaisseur < 0 || (f.epaisseur > 0 && f.epaisseur >= f.diametre / 2))) {
                r.erreurs.push_back({f.id, "L'épaisseur du tube doit être inférieure à son rayon (" + std::to_string(f.diametre / 2).substr(0, 5) +
                                           " mm) ; une épaisseur nulle donne un tube plein."});
                break;
              }
              const FonctionPiece* fc = esquisseUtilisable(f.cheminId, f.id, true, "l'esquisse du chemin (2D ou 3D)");
              if (!fc) break;
              TopoDS_Wire cheminFil;
              if (fc->type == TypeFonction::Esquisse3D) {
                const auto it3 = chemins3d.find(fc->id);
                if (it3 == chemins3d.end()) { r.erreurs.push_back({f.id, "L'esquisse 3D du chemin n'a pas pu être construite."}); break; }
                cheminFil = it3->second;
              } else {
                const Profil chemin = profil(*fc, r.repere(fc->id));
                opencascade::handle<TopTools_HSequenceOfShape> aretes = new TopTools_HSequenceOfShape();
                for (TopExp_Explorer ex(chemin.trace, TopAbs_EDGE); ex.More(); ex.Next()) aretes->Append(ex.Current());
                opencascade::handle<TopTools_HSequenceOfShape> fils;
                ShapeAnalysis_FreeBounds::ConnectEdgesToWires(aretes, 1e-6, false, fils);
                if (fils.IsNull() || fils->Length() < 1) { r.erreurs.push_back({f.id, "Le chemin doit être une suite de lignes et d'arcs qui se touchent."}); break; }
                cheminFil = TopoDS::Wire(fils->Value(1));
              }
              if (tube) {
                // Cercle (ou couronne) au début du chemin, dans le plan perpendiculaire à sa tangente
                BRepAdaptor_CompCurve cc(cheminFil);
                gp_Pnt o;
                gp_Vec t;
                cc.D1(cc.FirstParameter(), o, t);
                if (t.Magnitude() < 1e-12 || f.diametre <= 0) { r.erreurs.push_back({f.id, "Tube impossible : chemin dégénéré ou diamètre nul."}); break; }
                const gp_Ax2 ax(o, gp_Dir(t));
                const double re = f.diametre / 2;
                BRepBuilderAPI_MakeFace mf(gp_Pln(gp_Ax3(ax)), BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(ax, re)).Edge()).Wire());
                if (f.epaisseur > 0) {
                  TopoDS_Wire interieur = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(gp_Circ(ax, re - f.epaisseur)).Edge()).Wire();
                  interieur.Reverse();
                  mf.Add(interieur);
                }
                profilFace = mf.Face();
              }
              // Profil sans trou : enveloppe qui suit aussi les coins vifs du chemin ; avec des trous : balayage simple.
              int filsProfil = 0;
              for (TopExp_Explorer ex(profilFace, TopAbs_WIRE); ex.More(); ex.Next()) ++filsProfil;
              bool fait = false;
              if (filsProfil == 1) {
                BRepOffsetAPI_MakePipeShell enveloppe(cheminFil);
                enveloppe.SetMode(false);
                enveloppe.SetTransitionMode(BRepBuilderAPI_RightCorner);
                enveloppe.Add(BRepTools::OuterWire(profilFace));
                enveloppe.Build();
                if (enveloppe.IsDone() && enveloppe.MakeSolid()) { outil = enveloppe.Shape(); fait = true; }
              }
              if (!fait) {
                BRepOffsetAPI_MakePipe tuyau(cheminFil, profilFace);
                tuyau.Build();
                if (tuyau.IsDone()) { outil = tuyau.Shape(); fait = true; }
              }
              if (!fait) { r.erreurs.push_back({f.id, "Balayage impossible : le profil doit couper le début du chemin, sans que le chemin tourne plus serré que le profil."}); break; }
            } else {
              if (f.sources.size() < 2) { r.erreurs.push_back({f.id, "Choisis au moins deux esquisses de profil, dans l'ordre."}); break; }
              BRepOffsetAPI_ThruSections lissage(true, false);
              // Contrôle de compatibilité : OpenCascade recale le sommet de départ et le sens de chaque contour sur le
              // précédent ; sans lui, deux profils tracés dans un ordre différent donnent un solide vrillé (FO-09).
              lissage.CheckCompatibility(true);
              bool ok = true;
              for (int s : f.sources) {
                TopoDS_Face fc;
                if (!faceDe(s, fc, "chaque esquisse de profil")) { ok = false; break; }
                lissage.AddWire(BRepTools::OuterWire(fc));
              }
              if (!ok) break;
              lissage.Build();
              if (!lissage.IsDone()) { r.erreurs.push_back({f.id, "Lissage impossible entre ces profils."}); break; }
              outil = lissage.Shape();
            }
            // Profils confondus ou plats : selon la version d'OpenCascade, refus ou solide d'épaisseur nulle
            // (OpenCascade 8), qui passerait ensuite pour une fonction réussie sans rien changer.
            if (std::abs(volumeForme(outil)) < 1e-6) {
              r.erreurs.push_back({f.id, f.type == TypeFonction::Balayage ? "Balayage impossible : il ne donne aucun volume."
                                                                          : "Lissage impossible : les profils sont confondus ou ne donnent aucun volume."});
              break;
            }
            Noms nn;
            int k = 0;
            for (TopExp_Explorer ex(outil, TopAbs_FACE); ex.More(); ex.Next()) nn.push_back({TopoDS::Face(ex.Current()), id + (f.type == TypeFonction::Balayage ? ":balayage." : ":lissage.") + std::to_string(k++)});
            if (!appliquer(outil, nn, f.enlever, f.id)) break;
            outils[f.id] = {outil, f.enlever, nn};
            nommerReste(corps, noms, f.id);
            break;
          }
          case TypeFonction::Depouille: {
            if (corps.IsNull()) { r.erreurs.push_back({f.id, "Il n'y a pas de pièce à incliner."}); break; }
            gp_Ax3 pl;
            if (!resoudrePlan(f.planNeutre, pl)) { r.erreurs.push_back({f.id, "Choisis le plan neutre (plan ou face plane, qui ne bouge pas)."}); break; }
            const ResultatPiece& e = etatCourant();
            std::vector<TopoDS_Face> faces;
            std::string cassee;
            for (const auto& ref : f.references) {
              std::vector<TopoDS_Face> l;
              if (const ResolutionReference rs = e.resoudreFaces(ref, l, false); !rs.trouvee()) { cassee = rs.message; break; }
              faces.insert(faces.end(), l.begin(), l.end());
            }
            if (!cassee.empty()) { r.erreurs.push_back({f.id, "Référence cassée : " + cassee + ". Choisis à nouveau les faces à incliner."}); break; }
            if (faces.empty()) { r.erreurs.push_back({f.id, "Clique les faces à incliner."}); break; }
            gp_Dir tirage = pl.Direction();
            if (f.inverse) tirage.Reverse();
            BRepOffsetAPI_DraftAngle dp(corps);
            for (const auto& fc : faces) {
              dp.Add(fc, tirage, f.angle * std::numbers::pi / 180, gp_Pln(pl));
              if (!dp.AddDone()) { r.erreurs.push_back({f.id, "Une des faces ne peut pas être inclinée (elle doit couper le plan neutre)."}); break; }
            }
            if (!dp.AddDone()) break;
            dp.Build();
            if (!dp.IsDone()) { r.erreurs.push_back({f.id, "La dépouille a échoué."}); break; }
            const TopoDS_Shape res = dp.Shape();
            noms = suivre(dp, noms, res);
            corps = res;
            nommerReste(corps, noms, f.id);
            break;
          }
          case TypeFonction::Conge:
          case TypeFonction::Chanfrein: {
            if (corps.IsNull()) { r.erreurs.push_back({f.id, "Aucun solide à retoucher."}); break; }
            ResultatPiece etat;
            etat.forme = corps;
            etat.faces = noms;
            std::vector<TopoDS_Edge> aretes;
            std::vector<std::string> nomsAretes;  // identité de chaque arête : sa référence (et son rang dans celle-ci)
            std::string cassee;
            int nbCassees = 0;
            for (const auto& ref : f.references) {
              std::vector<TopoDS_Edge> l;
              if (const ResolutionReference rs = etat.resoudreAretes(ref, l); !rs.trouvee()) {
                if (cassee.empty()) cassee = rs.message;
                ++nbCassees;
                continue;
              }
              for (std::size_t k = 0; k < l.size(); ++k) {
                if (std::any_of(aretes.begin(), aretes.end(), [&](const TopoDS_Edge& a) { return a.IsSame(l[k]); })) continue;
                aretes.push_back(l[k]);
                nomsAretes.push_back(jetonNom(ref) + (l.size() > 1 ? "." + std::to_string(k) : std::string()));
              }
            }
            // Jamais d'application partielle : une arête perdue fait échouer toute la fonction, qui le dit.
            if (nbCassees > 0) {
              r.erreurs.push_back({f.id, "Référence cassée (" + std::to_string(nbCassees) + " arête(s) sur " + std::to_string(f.references.size()) + ") : " + cassee +
                                         ". Sélectionne à nouveau les arêtes."});
              break;
            }
            if (aretes.empty()) { r.erreurs.push_back({f.id, "Aucune arête trouvée : sélectionne les arêtes à arrondir."}); break; }
            const bool conge = f.type == TypeFonction::Conge;
            std::unique_ptr<BRepFilletAPI_MakeFillet> mf;
            std::unique_ptr<BRepFilletAPI_MakeChamfer> mc;
            BRepBuilderAPI_MakeShape* op = nullptr;
            if (conge) {
              mf = std::make_unique<BRepFilletAPI_MakeFillet>(corps);
              for (const auto& a : aretes) mf->Add(f.rayon, a);
              mf->Build();
              op = mf.get();
            } else {
              mc = std::make_unique<BRepFilletAPI_MakeChamfer>(corps);
              if (f.typeChanfrein == "distances" || f.typeChanfrein == "angle") {
                // La première distance se mesure sur l'une des deux faces voisines (la première par son nom,
                // l'autre si « inverser » est coché).
                CarteAncetres anc;
                TopExp::MapShapesAndAncestors(corps, TopAbs_EDGE, TopAbs_FACE, anc);
                for (const auto& a : aretes) {
                  std::vector<std::pair<std::string, TopoDS_Face>> voisines;
                  if (anc.Contains(a))
                    for (const TopoDS_Shape& fc : anc.FindFromKey(a)) {
                      const TopoDS_Face face = TopoDS::Face(fc);
                      if (std::none_of(voisines.begin(), voisines.end(), [&](const auto& v) { return v.second.IsSame(face); })) voisines.push_back({etat.nomFace(face), face});
                    }
                  // Première distance : sur la face désignée (faceChanfrein) si elle borde l'arête ; sinon sur la face
                  // de la fonction la plus ancienne (numéro de fonction, pas ordre alphabétique) ; « inverse » échange
                  // (FO-25)
                  auto rangFonction = [](const std::string& nom) {
                    const auto deux = nom.find(':');
                    try { return deux == std::string::npos ? 1 << 30 : std::stoi(nom.substr(0, deux)); } catch (...) { return 1 << 30; }
                  };
                  std::stable_sort(voisines.begin(), voisines.end(), [&](const auto& x, const auto& y) {
                    const int a = rangFonction(x.first), b = rangFonction(y.first);
                    return a != b ? a < b : x.first < y.first;
                  });
                  if (voisines.empty()) continue;
                  if (!f.faceChanfrein.empty() && voisines.size() > 1) {
                    std::vector<TopoDS_Face> designees;
                    etat.resoudreFaces(f.faceChanfrein, designees, false);
                    if (std::any_of(designees.begin(), designees.end(), [&](const TopoDS_Face& x) { return x.IsSame(voisines[1].second); }))
                      std::swap(voisines[0], voisines[1]);
                  }
                  const TopoDS_Face& ref = voisines[f.inverse && voisines.size() > 1 ? 1 : 0].second;
                  if (f.typeChanfrein == "distances") mc->Add(f.distance, f.distance2, a, ref);
                  else mc->AddDA(f.distance, f.angleChanfrein * std::numbers::pi / 180, a, ref);
                }
              } else {
                for (const auto& a : aretes) mc->Add(f.distance, a);
              }
              mc->Build();
              op = mc.get();
            }
            if (!op->IsDone()) {
              r.erreurs.push_back({f.id, conge ? "Congé impossible : le rayon est sans doute trop grand pour les faces voisines." : "Chanfrein impossible : la distance est sans doute trop grande."});
              break;
            }
            const TopoDS_Shape res = op->Shape();
            Noms n = suivre(*op, noms, res);
            CarteFaces facesRes;
            TopExp::MapShapes(res, TopAbs_FACE, facesRes);
            for (size_t i = 0; i < aretes.size(); ++i)
              for (const TopoDS_Shape& g : op->Generated(aretes[i]))
                if (g.ShapeType() == TopAbs_FACE && facesRes.Contains(g)) n.push_back({TopoDS::Face(g), id + (conge ? ":conge." : ":chanfrein.") + nomsAretes[i]});
            corps = res;
            noms = n;
            nommerReste(corps, noms, f.id);
            break;
          }
          case TypeFonction::Coque: {
            if (corps.IsNull()) { r.erreurs.push_back({f.id, "Aucun solide à évider."}); break; }
            ResultatPiece etat;
            etat.forme = corps;
            etat.faces = noms;
            NCollection_List<TopoDS_Shape> ouvertes;
            std::string cassee;
            for (const auto& ref : f.references) {
              std::vector<TopoDS_Face> l;
              if (const ResolutionReference rs = etat.resoudreFaces(ref, l, false); !rs.trouvee()) { cassee = rs.message; break; }
              for (const auto& fc : l) ouvertes.Append(fc);
            }
            for (const auto& [ref, ep] : f.epaisseursFaces) {
              std::vector<TopoDS_Face> l;
              if (const ResolutionReference rs = etat.resoudreFaces(ref, l, false); !rs.trouvee() && cassee.empty()) cassee = rs.message;
            }
            if (!cassee.empty()) { r.erreurs.push_back({f.id, "Référence cassée : " + cassee + ". Choisis à nouveau les faces de la coque."}); break; }
            if (ouvertes.IsEmpty()) { r.erreurs.push_back({f.id, "Clique dans la vue la ou les faces à ouvrir."}); break; }
            // Plusieurs façons de raccorder les parois : arrondie d'abord, puis par intersection des faces voisines
            // (utile quand l'épaisseur dépasse le rayon d'un congé, dont l'intérieur devient alors vif).
            if (!f.epaisseursFaces.empty()) {
              // Épaisseurs différentes selon les faces
              BRepOffset_MakeOffset mo;
              mo.Initialize(corps, -f.epaisseur, 1e-3, BRepOffset_Skin, false, false, GeomAbs_Intersection);
              for (const TopoDS_Shape& fc : ouvertes) mo.AddFace(TopoDS::Face(fc));
              for (const auto& [ref, ep] : f.epaisseursFaces)
                for (const auto& fc : etat.facesNommees(ref)) mo.SetOffsetOnFace(fc, -ep);
              mo.MakeThickSolid();
              if (!mo.IsDone() || mo.Shape().IsNull() || coqueSansEffet(corps, mo.Shape())) { r.erreurs.push_back({f.id, "Coque à épaisseurs multiples impossible : vérifie les épaisseurs choisies."}); break; }
              const TopoDS_Shape res = mo.Shape();
              Noms n = suivre(mo, noms, res);
              corps = res;
              noms = n;
              nommerReste(corps, noms, f.id);
              break;
            }
            BRepOffsetAPI_MakeThickSolid mk;
            bool reussi = false;
            for (int essai = 0; essai < 3 && !reussi; ++essai) {
              try {
                mk = BRepOffsetAPI_MakeThickSolid();
                mk.MakeThickSolidByJoin(corps, ouvertes, -f.epaisseur, 1e-3, BRepOffset_Skin, essai == 2, false, essai == 0 ? GeomAbs_Arc : GeomAbs_Intersection);
                reussi = mk.IsDone() && !mk.Shape().IsNull() && !coqueSansEffet(corps, mk.Shape());
              } catch (const Standard_Failure&) {
                reussi = false;
              }
            }
            if (!reussi) { r.erreurs.push_back({f.id, "Coque impossible : l'épaisseur est sans doute trop grande pour certaines faces (congés plus petits que l'épaisseur, parois trop fines)."}); break; }
            const TopoDS_Shape res = mk.Shape();
            Noms n = suivre(mk, noms, res);
            CarteFaces facesRes;
            TopExp::MapShapes(res, TopAbs_FACE, facesRes);
            for (const auto& [fc, nom] : noms)
              for (const TopoDS_Shape& g : mk.Generated(fc))
                if (g.ShapeType() == TopAbs_FACE && facesRes.Contains(g)) n.push_back({TopoDS::Face(g), nom + ":interieur"});
            corps = res;
            noms = n;
            nommerReste(corps, noms, f.id);
            break;
          }
          default:
            r.erreurs.push_back({f.id, "Type de fonction inconnu de cette version de Prométhée (projet écrit par une version plus récente) : "
                                       "elle est conservée dans le fichier mais pas calculée."});
            break;
        }
      } catch (const Standard_Failure& e) {
#if OCC_VERSION_HEX >= 0x080000
        const char* msg = e.what();
#else
        const char* msg = e.GetMessageString();
#endif
        r.erreurs.push_back({f.id, std::string("Échec du calcul géométrique : ") + (msg && *msg ? msg : "erreur interne")});
      } catch (const std::exception& e) {
        r.erreurs.push_back({f.id, e.what()});
      }
    }
    if (corps.IsNull() && !corpsAvant.IsNull()) {
      if (r.erreur(f.id).empty()) r.erreurs.push_back({f.id, "Cette fonction ne donne aucun solide : elle est refusée."});
    }
    // Résultat sans solide, ou de volume nul (révolution à plat…) : refusé, même si le contrôle B-Rep l'accepte
    // (FO-14, FO-22)
    if (!f.desactivee && r.erreur(f.id).empty() && !corps.IsNull() && !corps.IsSame(corpsAvant)) {
      if (nombreSolides(corps) == 0) r.erreurs.push_back({f.id, corpsAvant.IsNull() ? "Cette fonction ne donne aucun solide : elle est refusée." : "Cette fonction retirerait toute la matière : elle est refusée."});
      else if (!(std::abs(volume(corps)) > 1e-9)) r.erreurs.push_back({f.id, "Cette fonction donne un solide de volume nul (profil plat, axe hors du plan de l'esquisse ?) : elle est refusée."});
    }
    // Contrôle B-Rep du nouveau solide avant de l'accepter : une géométrie invalide est refusée, pas propagée.
    if (!f.desactivee && r.erreur(f.id).empty() && !corps.IsNull() && !corps.IsSame(corpsAvant)) {
      const std::string defaut = defautGeometrie(corps);
      if (!defaut.empty()) r.erreurs.push_back({f.id, "Le résultat n'est pas un solide valide (" + defaut + ") : la fonction est refusée. Change ses paramètres."});
    }
    if (!r.erreur(f.id).empty()) {
      // Échec non destructif : la pièce garde le dernier solide valide.
      corps = corpsAvant;
      noms = nomsAvant;
      if (toleAvant) tole = *toleAvant;
      if (!etatRef.referenceCassee.empty())
        for (auto& [id, m] : r.erreurs)
          if (id == f.id && m.find("Référence cassée") == std::string::npos) m += " Cause : " + etatRef.referenceCassee + ".";
      if (r.premiereErreur == 0) r.premiereErreur = f.id;
    }
    rejouees.insert(f.id);
    if (jusquA > 0 && f.id == jusquA) break;
  }
  r.forme = corps;
  // Un seul nom par face : celui de la fonction la plus ancienne, le premier rangé (FO-28)
  {
    CarteFaces vues;
    Noms uniques;
    for (const auto& n : noms)
      if (!vues.Contains(n.face)) { vues.Add(n.face); uniques.push_back(n); }
    noms = uniques;
  }
  r.faces = noms;
  r.tole = tole.deplie();
  return r;
}

}  // namespace prom
