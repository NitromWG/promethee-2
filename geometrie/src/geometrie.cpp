// Prométhée : pièces mécaniques exactes (B-rep OpenCascade). Licence GPL-3.0-only.
#include "promethee/depot.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/assemblage.hpp"
#include "promethee/fichiers.hpp"
#include "document_xcaf.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <mutex>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepTools.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <ShapeFix_Solid.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopoDS_Shell.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <Quantity_Color.hxx>
#include <APIHeaderSection_MakeHeader.hxx>
#include <Interface_Static.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Version.hxx>
#include <TCollection_HAsciiString.hxx>
#include <StepData_StepModel.hxx>
#include <STEPControl_Reader.hxx>
#include <sstream>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <StlAPI_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <StlAPI_Writer.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_List.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

namespace prom {

namespace {

constexpr double TOL = 1e-7;

// Repère plan : un point d'origine et deux directions, pour tracer un profil dans n'importe quel plan.
struct Repere {
  gp_Pnt o;
  gp_Vec u, v;
  gp_Pnt point(double a, double b) const { return o.Translated(u * a + v * b); }
};

TopoDS_Wire filRectArrondi(const Repere& R, double cu, double cv, double hu, double hv, double r) {
  r = std::clamp(r, 0.0, std::min(hu, hv));
  BRepBuilderAPI_MakeWire fil;
  auto ligne = [&](double a1, double b1, double a2, double b2) {
    const gp_Pnt P = R.point(a1, b1), Q = R.point(a2, b2);
    if (P.Distance(Q) > TOL) fil.Add(BRepBuilderAPI_MakeEdge(P, Q).Edge());
  };
  auto arc = [&](double ca, double cb, double a0) {
    if (r < TOL) return;
    const double a1 = a0 + std::numbers::pi / 4, a2 = a0 + std::numbers::pi / 2;
    const gp_Pnt P = R.point(ca + r * std::cos(a0), cb + r * std::sin(a0));
    const gp_Pnt M = R.point(ca + r * std::cos(a1), cb + r * std::sin(a1));
    const gp_Pnt Q = R.point(ca + r * std::cos(a2), cb + r * std::sin(a2));
    fil.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(P, M, Q).Value()).Edge());
  };
  ligne(cu - hu + r, cv - hv, cu + hu - r, cv - hv);
  arc(cu + hu - r, cv - hv + r, -std::numbers::pi / 2);
  ligne(cu + hu, cv - hv + r, cu + hu, cv + hv - r);
  arc(cu + hu - r, cv + hv - r, 0);
  ligne(cu + hu - r, cv + hv, cu - hu + r, cv + hv);
  arc(cu - hu + r, cv + hv - r, std::numbers::pi / 2);
  ligne(cu - hu, cv + hv - r, cu - hu, cv - hv + r);
  arc(cu - hu + r, cv - hv + r, std::numbers::pi);
  if (!fil.IsDone()) throw std::runtime_error("Contour arrondi impossible à construire.");
  return fil.Wire();
}

TopoDS_Shape prisme(const Repere& R, double cu, double cv, double hu, double hv, double r, const gp_Vec& profondeur) {
  const TopoDS_Face f = BRepBuilderAPI_MakeFace(filRectArrondi(R, cu, cv, hu, hv, r), true).Face();
  return BRepPrimAPI_MakePrism(f, profondeur).Shape();
}

// Prisme vertical à base de rectangle arrondi, de z0 à z0 + h.
TopoDS_Shape prismeRR(double cx, double cy, double hx, double hy, double r, double z0, double h) {
  const Repere R{gp_Pnt(0, 0, z0), gp_Vec(1, 0, 0), gp_Vec(0, 1, 0)};
  return prisme(R, cx, cy, hx, hy, r, gp_Vec(0, 0, h));
}

TopoDS_Shape cylindre(double x, double y, double z0, double r, double h) {
  return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x, y, z0), gp_Dir(0, 0, 1)), r, h).Shape();
}

TopoDS_Shape booleen(bool fusion, const TopoDS_Shape& a, const std::vector<TopoDS_Shape>& outils) {
  if (outils.empty()) return a;
  NCollection_List<TopoDS_Shape> args, tools;  // TopTools_ListOfShape, obsolète depuis OpenCascade 8
  args.Append(a);
  for (const auto& o : outils) tools.Append(o);
  if (fusion) {
    BRepAlgoAPI_Fuse op;
    op.SetArguments(args); op.SetTools(tools); op.SetRunParallel(true); op.Build();
    if (!op.IsDone() || op.HasErrors()) throw std::runtime_error("Union de solides impossible.");
    return op.Shape();
  }
  BRepAlgoAPI_Cut op;
  op.SetArguments(args); op.SetTools(tools); op.SetRunParallel(true); op.Build();
  if (!op.IsDone() || op.HasErrors()) throw std::runtime_error("Soustraction de solides impossible.");
  return op.Shape();
}

// Fusionne les faces coplanaires et arêtes alignées laissées par les opérations booléennes.
TopoDS_Shape unifier(const TopoDS_Shape& s) {
  ShapeUpgrade_UnifySameDomain u(s, true, true, false);
  u.Build();
  return u.Shape();
}

// Volume qui traverse une paroi à l'emplacement d'une découpe (dépasse de 1 mm de chaque côté).
// ---------- Cartes de forme quelconque ----------
TopoDS_Wire filContour(const Contour& c, double z) {
  BRepBuilderAPI_MakeWire mw;
  for (const auto& e : c.elements) {
    const gp_Pnt a(e.debut.x, e.debut.y, z), b(e.fin.x, e.fin.y, z);
    if (a.Distance(b) < 1e-7) continue;
    if (e.milieu) {
      GC_MakeArcOfCircle arc(a, gp_Pnt(e.milieu->x, e.milieu->y, z), b);
      if (arc.IsDone()) { mw.Add(BRepBuilderAPI_MakeEdge(arc.Value()).Edge()); continue; }
    }
    mw.Add(BRepBuilderAPI_MakeEdge(a, b).Edge());
  }
  if (!mw.IsDone()) throw std::runtime_error("Le contour de la carte n’est pas une boucle fermée valide.");
  return mw.Wire();
}

double aireFil(const TopoDS_Wire& w) {
  GProp_GProps g;
  BRepGProp::SurfaceProperties(BRepBuilderAPI_MakeFace(w, true).Face(), g);
  return std::abs(g.Mass());
}

// Décalage d'un contour plan, raccords arrondis ; positif vers l'extérieur.
TopoDS_Wire decalerFil(const TopoDS_Wire& w, double distance) {
  if (std::abs(distance) < 1e-9) return w;
  const double a0 = aireFil(w);
  for (double signe : {1.0, -1.0}) {
    BRepOffsetAPI_MakeOffset decalage(w, GeomAbs_Arc);
    decalage.Perform(signe * distance);
    if (!decalage.IsDone()) continue;
    TopoDS_Wire meilleur;
    double aire = -1;
    for (TopExp_Explorer ex(decalage.Shape(), TopAbs_WIRE); ex.More(); ex.Next()) {
      const TopoDS_Wire& r = TopoDS::Wire(ex.Current());
      const double a = aireFil(r);
      if (a > aire) { aire = a; meilleur = r; }
    }
    if (aire > 0 && (distance > 0) == (aire > a0)) return meilleur;
  }
  throw std::runtime_error("Impossible de décaler le contour de la carte (forme trop fine pour ce jeu).");
}

TopoDS_Shape prismeFil(const TopoDS_Wire& w, double z0, double h) {
  gp_Trsf t;
  t.SetTranslation(gp_Vec(0, 0, z0));
  const TopoDS_Shape face = BRepBuilderAPI_Transform(BRepBuilderAPI_MakeFace(w, true).Face(), t, true).Shape();
  return BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, h)).Shape();
}

// Enveloppe d'une carte libre : son contour, élargi autour des composants qui en débordent.
TopoDS_Wire filEnveloppe(const Projet& p, const Derive& d) {
  const TopoDS_Wire carte = filContour(p.carte.contours.front(), 0);
  if (d.debords.empty()) return carte;
  TopoDS_Shape u = BRepBuilderAPI_MakeFace(carte, true).Face();
  for (const auto& r : d.debords) {
    BRepBuilderAPI_MakePolygon poly(gp_Pnt(r.x1, r.y1, 0), gp_Pnt(r.x2, r.y1, 0), gp_Pnt(r.x2, r.y2, 0), gp_Pnt(r.x1, r.y2, 0), true);
    u = BRepAlgoAPI_Fuse(u, BRepBuilderAPI_MakeFace(poly.Wire(), true).Face()).Shape();
  }
  ShapeUpgrade_UnifySameDomain uni(u, true, true, false);
  uni.Build();
  TopoDS_Face meilleure;
  double aire = -1;
  for (TopExp_Explorer ex(uni.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
    GProp_GProps gp;
    BRepGProp::SurfaceProperties(ex.Current(), gp);
    if (std::abs(gp.Mass()) > aire) { aire = std::abs(gp.Mass()); meilleure = TopoDS::Face(ex.Current()); }
  }
  return meilleure.IsNull() ? carte : BRepTools::OuterWire(meilleure);
}


TopoDS_Shape prismeDecoupe(const Derive& d, double paroi, Bord mur, const DecoupeValide& o) {
  Repere R;
  gp_Vec sens;
  switch (mur) {
    case Bord::N: R = {gp_Pnt(0, d.bordCarte(mur, o.s) + d.jeu - 1, 0), gp_Vec(1, 0, 0), gp_Vec(0, 0, 1)}; sens = gp_Vec(0, paroi + 2, 0); break;
    case Bord::S: R = {gp_Pnt(0, d.bordCarte(mur, o.s) - d.jeu + 1, 0), gp_Vec(1, 0, 0), gp_Vec(0, 0, 1)}; sens = gp_Vec(0, -(paroi + 2), 0); break;
    case Bord::E: R = {gp_Pnt(d.bordCarte(mur, o.s) + d.jeu - 1, 0, 0), gp_Vec(0, 1, 0), gp_Vec(0, 0, 1)}; sens = gp_Vec(paroi + 2, 0, 0); break;
    default:      R = {gp_Pnt(d.bordCarte(mur, o.s) - d.jeu + 1, 0, 0), gp_Vec(0, 1, 0), gp_Vec(0, 0, 1)}; sens = gp_Vec(-(paroi + 2), 0, 0); break;
  }
  return prisme(R, o.s, o.t, o.w / 2, o.h / 2, o.r, sens);
}

// Les échanges STEP d'OpenCascade sont bavards : on ne garde que les échecs.
void silencer() {
  static const bool fait = [] {
    for (const auto& imprimante : Message::DefaultMessenger()->Printers()) imprimante->SetTraceLevel(Message_Fail);
    return true;
  }();
  (void)fait;
}

}  // namespace

TopoDS_Shape construireCorps(const Projet& p, const Derive& d) {
  const auto& b = p.boitier;
  const TopoDS_Wire env = d.libre ? filEnveloppe(p, d) : TopoDS_Wire();
  const TopoDS_Shape exterieur = d.libre ? prismeFil(decalerFil(env, b.jeu + b.paroi), 0, d.zt) : prismeRR(d.cx, d.cy, d.Lo / 2, d.Wo / 2, d.ro, 0, d.zt);
  const TopoDS_Shape cavite = d.libre ? prismeFil(decalerFil(env, b.jeu), d.zf, d.zt - d.zf + 1) : prismeRR(d.cx, d.cy, d.Li / 2, d.Wi / 2, d.ri, d.zf, d.zt - d.zf + 1);
  TopoDS_Shape corps = booleen(false, exterieur, {cavite});

  std::vector<TopoDS_Shape> futs, retraits;
  for (const auto& q : piliersValides(d)) {
    futs.push_back(cylindre(q.x, q.y, d.zf - 0.5, q.Rb, d.zpb - d.zf + 0.5));   // ancré dans le fond
    retraits.push_back(cylindre(q.x, q.y, d.zf, q.rp, d.zpb - d.zf + 1));         // avant-trou ou logement d'insert
  }
  corps = booleen(true, corps, futs);
  for (const auto& [mur, liste] : decoupesValides(d))
    for (const auto& o : liste) retraits.push_back(prismeDecoupe(d, p.boitier.paroi, mur, o));
  corps = booleen(false, corps, retraits);
  return unifier(corps);
}

TopoDS_Shape construireCouvercle(const Projet& p, const Derive& d) {
  const double e = p.boitier.couvercle;
  const auto& b = p.boitier;
  const TopoDS_Wire env = d.libre ? filEnveloppe(p, d) : TopoDS_Wire();
  const TopoDS_Shape plaque = d.libre ? prismeFil(decalerFil(env, b.jeu + b.paroi), d.zt, e) : prismeRR(d.cx, d.cy, d.Lo / 2, d.Wo / 2, d.ro, d.zt, e);
  // La lèvre monte de 0,5 mm dans la plaque pour une union franche, sans face commune.
  const TopoDS_Shape levreExt = d.libre ? prismeFil(decalerFil(env, b.jeu - Levre::jeu), d.zLevre, Levre::h + 0.5)
                                        : prismeRR(d.cx, d.cy, d.lo.hx, d.lo.hy, d.lo.r, d.zLevre, Levre::h + 0.5);
  const TopoDS_Shape levreInt = d.libre ? prismeFil(decalerFil(env, b.jeu - Levre::jeu - Levre::ep), d.zLevre - 1, Levre::h + 1.5)
                                        : prismeRR(d.cx, d.cy, d.li.hx, d.li.hy, d.li.r, d.zLevre - 1, Levre::h + 1.5);
  TopoDS_Shape couvercle = booleen(true, plaque, {booleen(false, levreExt, {levreInt})});
  std::vector<TopoDS_Shape> percages;
  for (const auto& o : percagesValides(d)) percages.push_back(cylindre(o.x, o.y, d.zt - 1, o.d / 2, e + 2));
  couvercle = booleen(false, couvercle, percages);
  return unifier(couvercle);
}

TopoDS_Shape construireCarte(const Projet& p, const Derive& d) {
  const auto& c = p.carte;
  TopoDS_Shape plaque = c.libre() ? prismeFil(filContour(c.contours.front(), 0), d.zpb, c.t) : prismeRR(c.x0, c.y0, c.L / 2, c.W / 2, c.r, d.zpb, c.t);
  std::vector<TopoDS_Shape> trous;
  for (size_t i = 1; i < c.contours.size(); ++i) trous.push_back(prismeFil(filContour(c.contours[i], 0), d.zpb - 1, c.t + 2));  // découpes intérieures
  for (const auto& t : d.trous)
    if (d.sdCarte(t.x, t.y) < -(t.vis.trou / 2 + 0.05)) trous.push_back(cylindre(t.x, t.y, d.zpb - 1, t.vis.trou / 2, c.t + 2));
  return booleen(false, plaque, trous);
}

std::vector<Point> contourDecale(const Projet& p, const Derive& d, double decalage, double pas) {
  std::vector<Point> res;
  if (!p.carte.libre()) return res;
  const TopoDS_Wire w = decalerFil(filEnveloppe(p, d), decalage);
  for (BRepTools_WireExplorer ex(w); ex.More(); ex.Next()) {
    BRepAdaptor_Curve courbe(ex.Current());
    const double f = courbe.FirstParameter(), l = courbe.LastParameter();
    const double longueur = GCPnts_AbscissaPoint::Length(courbe);
    const int n = std::max(1, static_cast<int>(std::ceil(longueur / pas)));
    const bool inverse = ex.Current().Orientation() == TopAbs_REVERSED;
    for (int i = 0; i < n; ++i) {
      const double t = inverse ? l - (l - f) * i / n : f + (l - f) * i / n;
      const gp_Pnt q = courbe.Value(t);
      res.push_back({q.X(), q.Y()});
    }
  }
  return res;
}

TopoDS_Shape construireComposant(const Derive& d, const GeoComp& g) {
  const TypeComposant* T = typeComposant(g.type);
  const double h = std::max(0.2, g.h);
  const double z0 = g.dessous ? d.zpb - h : d.zpt;  // face arrière : sous la carte
  if (T && T->rond) return cylindre(g.x, g.y, z0, std::max(0.1, std::min(g.hx, g.hy)), h);
  return BRepPrimAPI_MakeBox(gp_Pnt(g.x - g.hx, g.y - g.hy, z0), std::max(0.1, 2 * g.hx), std::max(0.1, 2 * g.hy), h).Shape();
}

double volume(const TopoDS_Shape& s) {
  GProp_GProps props;
  BRepGProp::VolumeProperties(s, props);
  return props.Mass();
}

bool estValide(const TopoDS_Shape& s) { return !s.IsNull() && BRepCheck_Analyzer(s).IsValid(); }

int nombreSolides(const TopoDS_Shape& s) {
  int n = 0;
  for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) ++n;
  return n;
}

Boite encombrement(const TopoDS_Shape& s) {
  Boite r;
  r.vide = true;
  if (s.IsNull()) return r;
  Bnd_Box b;
  try {
    BRepBndLib::AddOptimal(s, b, false, false);
  } catch (const Standard_Failure&) {
    b.SetVoid();
    BRepBndLib::Add(s, b);
  }
  if (b.IsVoid()) return r;
  b.Get(r.xmin, r.ymin, r.zmin, r.xmax, r.ymax, r.zmax);
  r.vide = false;
  return r;
}

TopoDS_Shape pourImpression(const TopoDS_Shape& s, bool retourner, double cx, double cy) {
  gp_Trsf t;
  t.SetTranslation(gp_Vec(-cx, -cy, 0));
  TopoDS_Shape r = BRepBuilderAPI_Transform(s, t, true).Shape();
  if (retourner) {
    gp_Trsf rot;
    rot.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), std::numbers::pi);
    r = BRepBuilderAPI_Transform(r, rot, true).Shape();
  }
  const Boite b = encombrement(r);
  if (b.vide) return r;
  gp_Trsf pose;
  pose.SetTranslation(gp_Vec(0, 0, -b.zmin));
  return BRepBuilderAPI_Transform(r, pose, true).Shape();
}


// Couleur « #RRGGBB » d'une pièce (gris par défaut)
void couleurDe(const std::string& hex, double& r, double& g, double& b) {
  r = g = b = 0.8;
  if (hex.size() != 7 || hex[0] != '#') return;
  try {
    const unsigned long v = std::stoul(hex.substr(1), nullptr, 16);
    r = static_cast<double>((v >> 16) & 0xFF) / 255;
    g = static_cast<double>((v >> 8) & 0xFF) / 255;
    b = static_cast<double>(v & 0xFF) / 255;
  } catch (...) {
  }
}

namespace xcaf {
void remplir(DocumentStep& d, const std::vector<PieceNommee>& pieces, const std::string& chemin) {
  if (pieces.size() == 1 && pieces.front().forme.Location().IsIdentity()) {
    d.prototype(pieces.front().forme, pieces.front().nom, pieces.front().r, pieces.front().g, pieces.front().b, pieces.front().couleursFaces);
    return;
  }
  // Plusieurs pièces : un assemblage racine, une instance placée par pièce (EX-07)
  const std::filesystem::path p = cheminFichier(chemin);
  const TDF_Label racine = d.assemblage(utf8(p.stem()).empty() ? std::string("Assemblage") : utf8(p.stem()));
  // Un prototype par forme et par couleur : les instances d'une même forme de même couleur le partagent ; une
  // même forme demandée dans une autre couleur en reçoit une copie, sans quoi XCAF rendrait le prototype existant
  // et la seconde pièce perdrait sa couleur. Chaque instance garde le nom de sa pièce.
  struct Proto { TDF_Label label; double r, g, b; };
  std::map<const void*, std::vector<Proto>> deja;
  for (const auto& piece : pieces) {
    const gp_Trsf place = piece.forme.Location().Transformation();
    const TopoDS_Shape nue = piece.forme.Located(TopLoc_Location());
    auto& liste = deja[nue.TShape().get()];
    TDF_Label proto;
    for (const auto& q : liste)
      if (std::abs(q.r - piece.r) < 1e-6 && std::abs(q.g - piece.g) < 1e-6 && std::abs(q.b - piece.b) < 1e-6) proto = q.label;
    if (proto.IsNull()) {
      TopoDS_Shape forme = nue;
      CouleursFaces faces = piece.couleursFaces;
      if (!liste.empty()) {
        // Copie : les faces colorées sont suivies dans la copie (retrouvées dans la forme d'origine par leur TShape)
        BRepBuilderAPI_Copy copie(nue);
        forme = copie.Shape();
        std::map<const void*, TopoDS_Shape> parTShape;
        for (TopExp_Explorer ex(nue, TopAbs_FACE); ex.More(); ex.Next()) parTShape.emplace(ex.Current().TShape().get(), ex.Current());
        for (auto& fc : faces)
          if (const auto it = parTShape.find(fc.first.TShape().get()); it != parTShape.end()) fc.first = copie.ModifiedShape(it->second);
      }
      proto = d.prototype(forme, piece.nom, piece.r, piece.g, piece.b, faces);
      liste.push_back({proto, piece.r, piece.g, piece.b});
    }
    d.instance(racine, proto, place, piece.nom);
  }
}
}  // namespace xcaf

void exporterStep(const std::vector<PieceNommee>& pieces, const std::string& chemin, const OptionsStep& options) {
  silencer();
  xcaf::DocumentStep d;
  xcaf::remplir(d, pieces, chemin);
  d.ecrire(chemin, options);
}

int exporterStepAssemblage(const Projet& projet, int racine, const std::function<const ResultatPiece&(int)>& piece, const std::string& chemin,
                           const OptionsStep& options) {
  silencer();
  if (racine < 0 || racine >= static_cast<int>(projet.assemblages.size())) throw std::runtime_error("Assemblage introuvable.");
  xcaf::DocumentStep d;
  int manquants = 0;
  std::map<std::pair<int, bool>, TDF_Label> prototypes;  // (pièce, symétrique) ; la symétrie de référence est YZ
  gp_Trsf symetrieYZ;
  symetrieYZ.SetMirror(gp::YOZ());
  auto prototypePiece = [&](int ip, bool symetrique) -> TDF_Label {
    const auto cle = std::pair{ip, symetrique};
    if (const auto it = prototypes.find(cle); it != prototypes.end()) return it->second;
    const TopoDS_Shape f = piece(ip).forme;
    if (f.IsNull()) return {};
    const Json& j = projet.pieces[static_cast<std::size_t>(ip)];
    const std::string nom = (j.contains("nom") && j["nom"].is_string() ? j["nom"].get<std::string>() : std::string("Pièce")) + (symetrique ? " (symétrique)" : "");
    double r, g, b;
    couleurDe(j.contains("couleur") && j["couleur"].is_string() ? j["couleur"].get<std::string>() : std::string(), r, g, b);
    // Apparences par face de la pièce
    std::map<std::string, std::string> parNom;
    if (j.contains("couleursFaces") && j["couleursFaces"].is_object())
      for (const auto& [n, v] : j["couleursFaces"].items()) if (v.is_string()) parNom[n] = v.get<std::string>();
    xcaf::CouleursFaces faces;
    for (const auto& [face, hex] : facesColorees(parNom, piece(ip))) {
      std::array<double, 3> c{};
      couleurDe(hex, c[0], c[1], c[2]);
      faces.emplace_back(face, c);
    }
    // Pièce symétrique : prototype distinct, géométrie symétrique par YZ (une autre symétrie = celle-ci tournée) ;
    // ses faces colorées sont celles que la symétrie a produites
    TopoDS_Shape forme = f;
    if (symetrique) {
      BRepBuilderAPI_Transform tr(f, symetrieYZ, true);
      forme = tr.Shape();
      for (auto& fc : faces) fc.first = tr.ModifiedShape(fc.first);
    }
    const TDF_Label l = d.prototype(forme, nom, r, g, b, faces);
    prototypes[cle] = l;
    return l;
  };
  // Chaque composant pèse T = P · R · S^m (P placement, R rotation qui ramène la symétrie demandée à celle de YZ,
  // S symétrie YZ, m = 1 si symétrique). Dans un sous-assemblage lui-même symétrique (s = 1), S · Q · S^m s'écrit
  // (S · Q · S) · S^(s xor m) : le placement est conjugué par S et la symétrie passe au niveau inférieur. Un
  // sous-assemblage symétrique devient ainsi un assemblage distinct, fait de prototypes symétriques (EX-07, EX-11).
  std::map<std::pair<int, bool>, TDF_Label> assemblagesFaits;
  std::vector<char> enCours(projet.assemblages.size(), 0);
  std::function<TDF_Label(int, bool, int)> assembler = [&](int a, bool sym, int profondeur) -> TDF_Label {
    if (const auto it = assemblagesFaits.find({a, sym}); it != assemblagesFaits.end()) return it->second;
    const Assemblage& as = projet.assemblages[static_cast<std::size_t>(a)];
    const TDF_Label l = d.assemblage(as.nom + (sym ? " (symétrique)" : ""));
    enCours[static_cast<std::size_t>(a)] = 1;
    for (std::size_t k = 0; k < as.composants.size(); ++k) {
      const ComposantAssemblage& c = as.composants[k];
      if (c.supprime) continue;
      ComposantAssemblage rigide = c;
      rigide.miroir = 0;
      gp_Trsf place = placementComposant(rigide);
      // Symétrie locale M = (rotation) · S_YZ : la rotation passe dans le placement, S_YZ dans le prototype
      if (c.miroir == 1) { gp_Trsf q; q.SetRotation(gp::OY(), M_PI); place = place * q; }
      else if (c.miroir == 2) { gp_Trsf q; q.SetRotation(gp::OZ(), M_PI); place = place * q; }
      if (sym) place = symetrieYZ * place * symetrieYZ;
      const bool symEnfant = sym != (c.miroir != 0);
      TDF_Label proto;
      std::string nom;
      if (c.assemblage >= 0) {
        if (c.assemblage >= static_cast<int>(projet.assemblages.size()) || enCours[static_cast<std::size_t>(c.assemblage)] || profondeur > 32) { ++manquants; continue; }
        proto = assembler(c.assemblage, symEnfant, profondeur + 1);
        nom = projet.assemblages[static_cast<std::size_t>(c.assemblage)].nom;
      } else {
        if (c.piece < 0 || c.piece >= static_cast<int>(projet.pieces.size())) { ++manquants; continue; }
        proto = prototypePiece(c.piece, symEnfant);
        const Json& j = projet.pieces[static_cast<std::size_t>(c.piece)];
        nom = j.contains("nom") && j["nom"].is_string() ? j["nom"].get<std::string>() : std::string("Pièce");
      }
      if (proto.IsNull()) { ++manquants; continue; }
      d.instance(l, proto, place, nom + ":" + std::to_string(k + 1));
    }
    enCours[static_cast<std::size_t>(a)] = 0;
    assemblagesFaits[{a, sym}] = l;
    return l;
  };
  assembler(racine, false, 0);
  d.ecrire(chemin, options);
  return manquants;
}

TopoDS_Shape lireStep(const std::string& chemin) {
  silencer();
  STEPControl_Reader r;
  if (r.ReadFile(chemin.c_str()) != IFSelect_RetDone) throw std::runtime_error("Lecture STEP impossible : " + chemin);
  r.TransferRoots();
  return r.OneShape();
}

void exporterStl(const TopoDS_Shape& s, const std::string& chemin, double fleche) {
  BRepMesh_IncrementalMesh maillage(s, fleche, false, 0.25, true);
  StlAPI_Writer w;
  w.ASCIIMode() = false;
  if (!w.Write(s, chemin.c_str())) throw std::runtime_error("Écriture STL impossible : " + chemin);
}

}  // namespace prom

namespace prom {
std::string brepDepuisForme(const TopoDS_Shape& forme) {
  std::ostringstream s;
  BRepTools::Write(forme, s);
  return s.str();
}

// Géométries importées déjà relues (EX-06) : le texte BRep d'un gros import demande des secondes de lecture, et
// chaque reconstruction de la pièce le relisait. Les formes les plus récentes sont gardées, retrouvées par
// l'empreinte et la longueur de leur texte ; les petits textes, vite relus, ne sont pas gardés.
TopoDS_Shape formeDepuisBrep(const std::string& texteOuReference) {
  TopoDS_Shape f;
  if (texteOuReference.empty()) return f;
  // Référence au dépôt des géométries importées (EX-06) : le texte n'existe qu'une fois en mémoire
  const auto partage = geometrieDeposee(texteOuReference);
  if (!partage) return f;
  const std::string& texte = *partage;
  struct Entree { std::size_t empreinte, taille; TopoDS_Shape forme; };
  static std::mutex verrou;
  static std::list<Entree> recentes;
  constexpr std::size_t TAILLE_MIN = 64 * 1024, ENTREES_MAX = 12;
  const bool garder = texte.size() >= TAILLE_MIN;
  const std::size_t empreinte = garder ? std::hash<std::string>{}(texte) : 0;
  if (garder) {
    const std::lock_guard<std::mutex> v(verrou);
    for (auto it = recentes.begin(); it != recentes.end(); ++it)
      if (it->empreinte == empreinte && it->taille == texte.size()) {
        recentes.splice(recentes.begin(), recentes, it);
        return recentes.front().forme;
      }
  }
  std::istringstream s(texte);
  BRep_Builder b;
  BRepTools::Read(f, s, b);
  if (garder && !f.IsNull()) {
    const std::lock_guard<std::mutex> v(verrou);
    recentes.push_front({empreinte, texte.size(), f});
    if (recentes.size() > ENTREES_MAX) recentes.pop_back();
  }
  return f;
}

namespace {
// Maillage relu (STL) : coutures recousues, chaque coque fermée devient un solide bien orienté et ses triangles
// coplanaires sont fusionnés en faces ; une pièce importée se mesure et se perce alors comme une autre (EX-08).
// Une coque ouverte reste une coque (elle n'a pas de volume).
TopoDS_Shape solideDepuisMaillage(const TopoDS_Shape& maillage) {
  try {
    BRepBuilderAPI_Sewing couture(1e-6);
    couture.Add(maillage);
    couture.Perform();
    const TopoDS_Shape cousu = couture.SewedShape();
    BRep_Builder b;
    TopoDS_Compound res;
    b.MakeCompound(res);
    int solides = 0, coques = 0;
    for (TopExp_Explorer ex(cousu, TopAbs_SHELL); ex.More(); ex.Next()) {
      const TopoDS_Shell coque = TopoDS::Shell(ex.Current());
      ++coques;
      if (!BRep_Tool::IsClosed(coque)) { b.Add(res, coque); continue; }
      BRepBuilderAPI_MakeSolid ms(coque);
      if (!ms.IsDone()) { b.Add(res, coque); continue; }
      ShapeFix_Solid fix(ms.Solid());
      fix.Perform();
      TopoDS_Shape solide = fix.Solid();
      ShapeUpgrade_UnifySameDomain unifier(solide, true, true, false);
      unifier.Build();
      if (!unifier.Shape().IsNull()) solide = unifier.Shape();
      for (TopExp_Explorer es(solide, TopAbs_SOLID); es.More(); es.Next()) { b.Add(res, es.Current()); ++solides; }
    }
    if (coques == 0) return maillage;
    if (solides == 1 && coques == 1)
      for (TopExp_Explorer es(res, TopAbs_SOLID); es.More(); es.Next()) return es.Current();
    return res;
  } catch (const Standard_Failure&) {
    return maillage;
  }
}
}  // namespace

std::string lireFichierCAO(const std::string& chemin, std::string* erreur) {
  auto echec = [&](const std::string& m) { if (erreur) *erreur = m; return std::string(); };
  std::string ext = chemin.substr(chemin.find_last_of('.') + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  TopoDS_Shape forme;
  if (ext == "step" || ext == "stp") {
    STEPControl_Reader r;
    if (r.ReadFile(chemin.c_str()) != IFSelect_RetDone) return echec("Fichier STEP illisible.");
    r.TransferRoots();
    forme = r.OneShape();
  } else if (ext == "iges" || ext == "igs") {
    IGESControl_Reader r;
    if (r.ReadFile(chemin.c_str()) != IFSelect_RetDone) return echec("Fichier IGES illisible.");
    r.TransferRoots();
    forme = r.OneShape();
  } else if (ext == "brep" || ext == "brp") {
    BRep_Builder b;
    if (!BRepTools::Read(forme, chemin.c_str(), b)) return echec("Fichier BREP illisible.");
  } else if (ext == "stl") {
    StlAPI_Reader r;
    if (!r.Read(forme, chemin.c_str())) return echec("Fichier STL illisible.");
    forme = solideDepuisMaillage(forme);
  } else {
    return echec("Format non pris en charge : STEP, IGES, BREP ou STL.");
  }
  if (forme.IsNull()) return echec("Le fichier ne contient aucune géométrie.");
  return brepDepuisForme(forme);
}
}  // namespace prom
