// Prométhée : mesure (M11). Licence GPL-3.0-only.
#include "promethee/mesure.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Lin.hxx>

#include "promethee/construction.hpp"

namespace prom {

namespace {

constexpr double DEG = 180 / std::numbers::pi;

using G = GrandeurMesure::Nature;

void ajouter(DescriptionMesure& d, G nature, const std::string& nom, double v) { d.grandeurs.push_back({nature, nom, v}); }

// Longueur d'arête
double longueurArete(const TopoDS_Edge& e) {
  GProp_GProps g;
  BRepGProp::LinearProperties(e, g);
  return g.Mass();
}

// Forme qui sert à calculer les distances : plan et axe prolongés loin, point en sommet
TopoDS_Shape formeDistance(const ElementMesure& e, double portee) {
  switch (e.genre) {
    case ElementMesure::Genre::Plan: return BRepBuilderAPI_MakeFace(e.plan, -portee, portee, -portee, portee).Face();
    case ElementMesure::Genre::Axe: return BRepBuilderAPI_MakeEdge(gp_Lin(e.axe), -portee, portee).Edge();
    case ElementMesure::Genre::Point: return BRepBuilderAPI_MakeVertex(e.point).Vertex();
    default: return e.forme;
  }
}

}  // namespace

DescriptionMesure decrireElement(const ElementMesure& e) {
  DescriptionMesure d;
  switch (e.genre) {
    case ElementMesure::Genre::Plan:
      d.type = e.nom.empty() ? std::string("Plan") : e.nom;
      d.aNormale = true;
      d.normale = e.plan.Axis().Direction();
      d.aCentre = false;
      d.centre = e.plan.Location();
      return d;
    case ElementMesure::Genre::Axe:
      d.type = e.nom.empty() ? std::string("Axe") : e.nom;
      d.aAxe = true;
      d.axe = e.axe;
      return d;
    case ElementMesure::Genre::Point:
      d.type = e.nom.empty() ? std::string("Point") : e.nom;
      d.aCentre = true;
      d.centre = e.point;
      ajouter(d, G::Longueur, "X", e.point.X());
      ajouter(d, G::Longueur, "Y", e.point.Y());
      ajouter(d, G::Longueur, "Z", e.point.Z());
      return d;
    default: break;
  }
  const TopoDS_Shape& s = e.forme;
  if (s.IsNull()) { d.type = "Élément"; return d; }
  if (s.ShapeType() == TopAbs_FACE) {
    const TopoDS_Face f = TopoDS::Face(s);
    GProp_GProps g;
    BRepGProp::SurfaceProperties(f, g);
    d.aire = g.Mass();
    const gp_Pnt cm = g.CentreOfMass();
    // Périmètre : chaque arête une fois, couture des surfaces fermées exclue
    double perimetre = 0;
    TopTools_IndexedMapOfShape aretes;
    TopExp::MapShapes(f, TopAbs_EDGE, aretes);
    for (int i = 1; i <= aretes.Extent(); ++i) {
      const TopoDS_Edge a = TopoDS::Edge(aretes(i));
      if (BRep_Tool::IsClosed(a, f) || BRep_Tool::Degenerated(a)) continue;
      perimetre += longueurArete(a);
    }
    BRepAdaptor_Surface su(f);
    switch (su.GetType()) {
      case GeomAbs_Plane: {
        d.type = "Face plane";
        gp_Pnt p;
        gp_Dir n;
        if (!e.solide.IsNull() && normaleSortante(f, e.solide, p, n)) d.normale = n;
        else {
          n = su.Plane().Axis().Direction();
          if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
          d.normale = n;
        }
        d.aNormale = true;
        d.aCentre = true;
        d.centre = cm;
        break;
      }
      case GeomAbs_Cylinder: {
        d.type = "Face cylindrique";
        const gp_Cylinder c = su.Cylinder();
        ajouter(d, G::Longueur, "Ø", 2 * c.Radius());
        ajouter(d, G::Longueur, "rayon", c.Radius());
        d.aAxe = true;
        d.axe = c.Axis();
        // Centre : sur l'axe, à la hauteur du centre de la face
        const gp_Lin l(c.Axis());
        const gp_Vec v(l.Location(), cm);
        d.centre = l.Location().Translated(gp_Vec(l.Direction()) * v.Dot(gp_Vec(l.Direction())));
        d.aCentre = true;
        break;
      }
      case GeomAbs_Cone: {
        d.type = "Face conique";
        const gp_Cone c = su.Cone();
        ajouter(d, G::Angle, "demi-angle", std::abs(c.SemiAngle()) * DEG);
        d.aAxe = true;
        d.axe = c.Axis();
        break;
      }
      case GeomAbs_Sphere: {
        d.type = "Face sphérique";
        const gp_Sphere sp = su.Sphere();
        ajouter(d, G::Longueur, "Ø", 2 * sp.Radius());
        d.aCentre = true;
        d.centre = sp.Location();
        break;
      }
      case GeomAbs_Torus: {
        d.type = "Face torique";
        const gp_Torus t = su.Torus();
        ajouter(d, G::Longueur, "rayon majeur", t.MajorRadius());
        ajouter(d, G::Longueur, "rayon mineur", t.MinorRadius());
        d.aAxe = true;
        d.axe = t.Axis();
        break;
      }
      default:
        d.type = "Face";
        d.aCentre = true;
        d.centre = cm;
        break;
    }
    ajouter(d, G::Aire, "aire", d.aire);
    ajouter(d, G::Longueur, "périmètre", perimetre);
    return d;
  }
  if (s.ShapeType() == TopAbs_EDGE) {
    const TopoDS_Edge a = TopoDS::Edge(s);
    d.longueur = longueurArete(a);
    BRepAdaptor_Curve c(a);
    switch (c.GetType()) {
      case GeomAbs_Line: {
        d.type = "Arête droite";
        gp_Pnt p0 = c.Value(c.FirstParameter()), p1 = c.Value(c.LastParameter());
        if (a.Orientation() == TopAbs_REVERSED) std::swap(p0, p1);
        if (p0.Distance(p1) > 1e-9) { d.aDirection = true; d.direction = gp_Dir(gp_Vec(p0, p1)); }
        d.aAxe = true;
        d.axe = gp_Ax1(p0, c.Line().Direction());
        d.aCentre = true;
        d.centre = gp_Pnt((p0.XYZ() + p1.XYZ()) / 2);
        ajouter(d, G::Longueur, "longueur", d.longueur);
        break;
      }
      case GeomAbs_Circle: {
        const gp_Circ ci = c.Circle();
        const double ouverture = std::abs(c.LastParameter() - c.FirstParameter()) * DEG;
        const bool complet = std::abs(ouverture - 360) < 1e-6;
        d.type = complet ? "Arête circulaire (cercle)" : "Arête circulaire (arc)";
        ajouter(d, G::Longueur, "Ø", 2 * ci.Radius());
        ajouter(d, G::Longueur, "rayon", ci.Radius());
        ajouter(d, G::Longueur, "longueur", d.longueur);
        if (!complet) ajouter(d, G::Angle, "angle de l'arc", ouverture);
        d.aCentre = true;
        d.centre = ci.Location();
        d.aAxe = true;
        d.axe = ci.Axis();
        break;
      }
      case GeomAbs_Ellipse: {
        d.type = "Arête elliptique";
        const gp_Elips el = c.Ellipse();
        ajouter(d, G::Longueur, "grand rayon", el.MajorRadius());
        ajouter(d, G::Longueur, "petit rayon", el.MinorRadius());
        ajouter(d, G::Longueur, "longueur", d.longueur);
        d.aCentre = true;
        d.centre = el.Location();
        d.aAxe = true;
        d.axe = el.Axis();
        break;
      }
      default:
        d.type = "Arête";
        ajouter(d, G::Longueur, "longueur", d.longueur);
        break;
    }
    return d;
  }
  if (s.ShapeType() == TopAbs_VERTEX) {
    const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(s));
    d.type = "Sommet";
    d.aCentre = true;
    d.centre = p;
    ajouter(d, G::Longueur, "X", p.X());
    ajouter(d, G::Longueur, "Y", p.Y());
    ajouter(d, G::Longueur, "Z", p.Z());
    return d;
  }
  // Corps : volume, aire et centre de masse ; un tracé (esquisse, fil) : sa longueur
  GProp_GProps v, a;
  BRepGProp::VolumeProperties(s, v);
  if (std::abs(v.Mass()) < 1e-9) {
    GProp_GProps lg;
    BRepGProp::LinearProperties(s, lg);
    d.type = e.nom.empty() ? std::string("Tracé") : e.nom;
    d.longueur = lg.Mass();
    ajouter(d, G::Longueur, "longueur", d.longueur);
    return d;
  }
  d.type = e.nom.empty() ? std::string("Corps") : e.nom;
  BRepGProp::SurfaceProperties(s, a);
  d.aire = a.Mass();
  ajouter(d, G::Volume, "volume", v.Mass());
  ajouter(d, G::Aire, "aire", d.aire);
  d.aCentre = true;
  d.centre = v.CentreOfMass();
  return d;
}

ResultatMesure mesurer(const std::vector<ElementMesure>& elements) {
  ResultatMesure r;
  for (const auto& e : elements) {
    r.elements.push_back(decrireElement(e));
    if (e.genre == ElementMesure::Genre::Forme && !e.forme.IsNull()) {
      const auto t = e.forme.ShapeType();
      if (t == TopAbs_FACE) { ++r.faces; r.aireTotale += r.elements.back().aire; }
      else if (t == TopAbs_EDGE) { ++r.aretes; r.longueurTotale += r.elements.back().longueur; }
      else if (t == TopAbs_VERTEX) ++r.sommets;
    }
  }
  if (elements.size() != 2) return r;
  const DescriptionMesure &a = r.elements[0], &b = r.elements[1];
  // Portée des plans et axes : bien au-delà des formes mesurées
  Bnd_Box boite;
  for (const auto& e : elements) if (!e.forme.IsNull()) BRepBndLib::Add(e.forme, boite);
  for (const auto& e : elements) if (e.genre == ElementMesure::Genre::Point) boite.Add(e.point);
  const double portee = boite.IsVoid() ? 1e4 : 10 * std::sqrt(boite.SquareExtent()) + 1000;
  const TopoDS_Shape sa = formeDistance(elements[0], portee), sb = formeDistance(elements[1], portee);
  if (!sa.IsNull() && !sb.IsNull()) {
    BRepExtrema_DistShapeShape dist(sa, sb);
    if (dist.IsDone() && dist.NbSolution() > 0) {
      r.distance = true;
      r.distanceMin = dist.Value();
      r.pa = dist.PointOnShape1(1);
      r.pb = dist.PointOnShape2(1);
      r.delta = gp_Vec(r.pa, r.pb);
    }
  }
  if (a.aCentre && b.aCentre) {
    r.centres = true;
    r.ca = a.centre;
    r.cb = b.centre;
    r.distanceCentres = a.centre.Distance(b.centre);
    r.deltaCentres = gp_Vec(a.centre, b.centre);
  }
  // Entraxe : deux axes parallèles (cylindres, cercles, droites)
  if (a.aAxe && b.aAxe && a.axe.Direction().IsParallel(b.axe.Direction(), 1e-9)) {
    r.entraxe = true;
    r.valeurEntraxe = gp_Lin(a.axe).Distance(b.axe.Location());
  }
  // Écart de deux plans parallèles, mesuré le long de la normale
  if (a.aNormale && b.aNormale && a.normale.IsParallel(b.normale, 1e-9)) {
    r.ecartPlans = true;
    const gp_Pnt pa = a.aCentre ? a.centre : (elements[0].genre == ElementMesure::Genre::Plan ? elements[0].plan.Location() : r.pa);
    const gp_Pnt pb = b.aCentre ? b.centre : (elements[1].genre == ElementMesure::Genre::Plan ? elements[1].plan.Location() : r.pb);
    r.valeurEcartPlans = std::abs(gp_Vec(pa, pb).Dot(gp_Vec(a.normale)));
  }
  // Angle vrai : entre normales (angle des plans, 0 à 180° selon les normales sortantes), entre directions d'arêtes
  // (0 à 180°), entre axes (non orientés : 0 à 90°) ; entre une droite et un plan, 0 à 90°
  struct Sens { bool present = false, normale = false, oriente = false; gp_Dir d; };
  auto sens = [](const DescriptionMesure& x) {
    Sens s;
    if (x.aNormale) { s = {true, true, true, x.normale}; }
    else if (x.aDirection) { s = {true, false, true, x.direction}; }
    else if (x.aAxe) { s = {true, false, false, x.axe.Direction()}; }
    return s;
  };
  const Sens u = sens(a), v = sens(b);
  if (u.present && v.present) {
    double ang = u.d.Angle(v.d) * DEG;
    if (u.normale != v.normale) { ang = std::abs(90 - ang); r.natureAngle = ResultatMesure::Angle::DroitePlan; }
    else if (!u.oriente || !v.oriente) { ang = std::min(ang, 180 - ang); r.natureAngle = ResultatMesure::Angle::Axes; }
    else r.natureAngle = u.normale ? ResultatMesure::Angle::Normales : ResultatMesure::Angle::Directions;
    r.angle = true;
    r.angleDeg = ang;
  }
  return r;
}

std::string texteGrandeur(const GrandeurMesure& g, const Unites& u) {
  auto arrondi = [](double x) { return std::round(x * 1000) / 1000; };
  switch (g.nature) {
    case G::Aire: return ecrireAire(arrondi(g.valeur), u);
    case G::Angle: return ecrireValeur(arrondi(g.valeur), true, u);
    case G::Volume: {
      const double l = depuisMillimetres(1, u.longueur);
      return ecrireValeur(arrondi(g.valeur * l * l * l), false, Unites{UniteLongueur::Millimetre, u.angle}, false) + " " + symbole(u.longueur) + "³";
    }
    case G::Nombre: return ecrireValeur(arrondi(g.valeur), false, u, false);
    default: return ecrireValeur(arrondi(g.valeur), false, u);
  }
}

std::vector<std::string> lignesMesure(const ResultatMesure& r, const Unites& u) {
  std::vector<std::string> l;
  auto L = [&](double mm) { return texteGrandeur({G::Longueur, "", mm}, u); };
  auto delta = [&](const gp_Vec& d) { return "ΔX " + L(d.X()) + " · ΔY " + L(d.Y()) + " · ΔZ " + L(d.Z()); };
  for (std::size_t i = 0; i < r.elements.size() && i < 8; ++i) {
    const auto& e = r.elements[i];
    std::string t = std::to_string(i + 1) + ". " + e.type;
    for (const auto& g : e.grandeurs) t += " · " + (g.nom == "Ø" || g.nom == "X" || g.nom == "Y" || g.nom == "Z" ? g.nom + " " : g.nom + " ") + texteGrandeur(g, u);
    l.push_back(t);
  }
  if (r.elements.size() > 8) l.push_back("… et " + std::to_string(r.elements.size() - 8) + " autre(s)");
  if (r.elements.size() == 2) {
    if (r.distance) l.push_back("Distance minimale " + L(r.distanceMin) + " (" + delta(r.delta) + ")");
    if (r.centres && (r.distanceCentres > 1e-9 || !r.distance)) l.push_back("Centre à centre " + L(r.distanceCentres) + " (" + delta(r.deltaCentres) + ")");
    if (r.entraxe) l.push_back("Entraxe " + L(r.valeurEntraxe));
    if (r.ecartPlans) l.push_back("Écart des plans parallèles " + L(r.valeurEcartPlans));
    if (r.angle) {
      const std::string a = texteGrandeur({G::Angle, "", r.angleDeg}, u);
      switch (r.natureAngle) {
        case ResultatMesure::Angle::Normales: l.push_back("Angle entre les normales " + a + " (angle des faces " + texteGrandeur({G::Angle, "", 180 - r.angleDeg}, u) + ")"); break;
        case ResultatMesure::Angle::DroitePlan: l.push_back("Angle avec le plan " + a); break;
        default: l.push_back("Angle " + a); break;
      }
    }
  } else if (r.elements.size() > 2) {
    std::string t = "Total : ";
    if (r.aretes) t += std::to_string(r.aretes) + " arête(s), longueur " + L(r.longueurTotale);
    if (r.faces) t += std::string(r.aretes ? " ; " : "") + std::to_string(r.faces) + " face(s), aire " + texteGrandeur({G::Aire, "", r.aireTotale}, u);
    if (!r.aretes && !r.faces) t += std::to_string(r.elements.size()) + " éléments";
    l.push_back(t);
  }
  return l;
}

}  // namespace prom
