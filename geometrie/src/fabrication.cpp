// Prométhée : vérification de fabrication. Licence GPL-3.0-only.
#include "promethee/fabrication.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp_Face.hxx>
#include <BRep_Tool.hxx>
#include <ElSLib.hxx>
#include <gp_Pnt2d.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>

namespace prom {

namespace {
// Écriture décimale française, jamais scientifique (EX-21) : deux décimales sous 10, une sous 100, aucune au-delà,
// zéros de queue retirés (1 500 mm, 0,35 mm, 12,5 mm).
std::string decimal(double v) {
  const int dec = std::abs(v) < 10 ? 2 : (std::abs(v) < 100 ? 1 : 0);
  std::ostringstream s;
  s << std::fixed;
  s.precision(dec);
  s << v;
  std::string t = s.str();
  if (t.find('.') != std::string::npos) {
    while (t.back() == '0') t.pop_back();
    if (t.back() == '.') t.pop_back();
  }
  std::replace(t.begin(), t.end(), '.', ',');
  return t == "-0" ? "0" : t;
}
std::string mm(double v) { return decimal(v) + " mm"; }

// Normale sortante d'une face en un point de ses paramètres
gp_Dir normaleSortante(const TopoDS_Face& f, double u, double v) {
  BRepGProp_Face g(f);
  gp_Pnt p;
  gp_Vec n;
  g.Normal(u, v, p, n);
  if (n.Magnitude() < 1e-12) return gp_Dir(0, 0, 1);
  return gp_Dir(n);
}
}  // namespace

std::vector<AvisFabrication> verifierFabrication(const TopoDS_Shape& piece, const SeuilsFabrication& s) {
  std::vector<AvisFabrication> avis;
  if (piece.IsNull()) return avis;
  std::vector<TopoDS_Face> faces;
  for (TopExp_Explorer ex(piece, TopAbs_FACE); ex.More(); ex.Next()) faces.push_back(TopoDS::Face(ex.Current()));
  // 1. Parois minces : faces planes face à face (normales opposées), proches l'une de l'autre, la matière entre elles
  {
    struct Plan { size_t i; gp_Pnt p; gp_Dir n; };
    std::vector<Plan> plans;
    for (size_t i = 0; i < faces.size(); ++i) {
      BRepAdaptor_Surface su(faces[i]);
      if (su.GetType() != GeomAbs_Plane) continue;
      const double u = (su.FirstUParameter() + su.LastUParameter()) / 2, v = (su.FirstVParameter() + su.LastVParameter()) / 2;
      plans.push_back({i, su.Value(u, v), normaleSortante(faces[i], u, v)});
    }
    int compte = 0;
    for (size_t a = 0; a < plans.size() && compte < 8; ++a)
      for (size_t b = a + 1; b < plans.size() && compte < 8; ++b) {
        if (plans[a].n.Dot(plans[b].n) > -0.999) continue;
        // Distance entre les plans, mesurée à l'intérieur de la matière (depuis a, contre sa normale sortante)
        const double d = gp_Vec(plans[b].p, plans[a].p).Dot(gp_Vec(plans[a].n));
        if (d <= 1e-6 || d >= s.paroiMin) continue;
        BRepExtrema_DistShapeShape dist(faces[plans[a].i], faces[plans[b].i]);
        if (!dist.IsDone() || std::abs(dist.Value() - d) > 1e-4) continue;  // les faces ne se font pas vraiment face
        avis.push_back({1, "Paroi de " + mm(d) + " : trop mince (moins de " + mm(s.paroiMin) + "), fragile ou impossible à produire.", {faces[plans[a].i], faces[plans[b].i]}});
        ++compte;
      }
  }
  // 2. Petits rayons et 3. trous profonds : faces cylindriques et toriques
  for (const auto& f : faces) {
    BRepAdaptor_Surface su(f);
    if (su.GetType() == GeomAbs_Torus && su.Torus().MinorRadius() < s.rayonMin)
      avis.push_back({1, "Congé de " + mm(su.Torus().MinorRadius()) + " : rayon trop petit pour la plupart des outils.", {f}});
    if (su.GetType() != GeomAbs_Cylinder) continue;
    const double r = su.Cylinder().Radius();
    if (r < s.rayonMin) { avis.push_back({1, "Rayon de " + mm(r) + " : trop petit pour être usiné ou imprimé proprement.", {f}}); continue; }
    // Trou : face cylindrique dont la normale sortante pointe vers l'axe (matière autour)
    const double u = (su.FirstUParameter() + su.LastUParameter()) / 2, v = (su.FirstVParameter() + su.LastVParameter()) / 2;
    const gp_Pnt p = su.Value(u, v);
    const gp_Ax1 axe = su.Cylinder().Axis();
    const gp_Vec versAxe = gp_Vec(p, axe.Location()) - gp_Vec(axe.Direction()) * gp_Vec(p, axe.Location()).Dot(gp_Vec(axe.Direction()));
    const bool trou = versAxe.Magnitude() > 1e-9 && gp_Vec(normaleSortante(f, u, v)).Dot(versAxe) > 0;
    const bool tourComplet = su.LastUParameter() - su.FirstUParameter() > 2 * std::numbers::pi - 1e-6;
    if (!trou || !tourComplet) continue;
    const double longueur = su.LastVParameter() - su.FirstVParameter();
    if (longueur / (2 * r) > s.profondeurMax) {
      const std::string ld = decimal(longueur / (2 * r));
      avis.push_back({1, "Trou de Ø " + mm(2 * r) + " sur " + mm(longueur) + " : profondeur " + ld + " fois le diamètre, difficile à percer (plus de " + std::to_string(static_cast<int>(s.profondeurMax)) + ").", {f}});
    }
  }
  // 4. Angles intérieurs vifs : arête droite entre deux faces planes, concave (la matière occupe plus de 180°)
  {
    TopTools_IndexedDataMapOfShapeListOfShape aretesFaces;
    TopExp::MapShapesAndAncestors(piece, TopAbs_EDGE, TopAbs_FACE, aretesFaces);
    std::vector<TopoDS_Shape> vives;
    for (int i = 1; i <= aretesFaces.Extent(); ++i) {
      const TopoDS_Edge e = TopoDS::Edge(aretesFaces.FindKey(i));
      const TopTools_ListOfShape& fs = aretesFaces.FindFromIndex(i);
      if (fs.Extent() != 2) continue;
      const TopoDS_Face f1 = TopoDS::Face(fs.First()), f2 = TopoDS::Face(fs.Last());
      BRepAdaptor_Surface s1(f1), s2(f2);
      if (s1.GetType() != GeomAbs_Plane || s2.GetType() != GeomAbs_Plane) continue;
      BRepAdaptor_Curve c(e);
      if (c.GetType() != GeomAbs_Line) continue;
      const double tm = (c.FirstParameter() + c.LastParameter()) / 2;
      const gp_Pnt m = c.Value(tm);
      // Normales sortantes des deux faces au milieu de l'arête
      auto normaleEn = [&](const TopoDS_Face& f) {
        BRepAdaptor_Surface su(f);
        double u, v;
        const gp_Pln pl = su.Plane();
        ElSLib::Parameters(pl, m, u, v);
        return normaleSortante(f, u, v);
      };
      const gp_Dir n1 = normaleEn(f1), n2 = normaleEn(f2);
      if (std::abs(n1.Dot(n2)) > 0.999) continue;  // faces coplanaires ou opposées
      // Direction qui entre dans la face 2 depuis l'arête : perpendiculaire à l'arête, dans le plan de la face 2
      gp_Vec t(c.Line().Direction());
      gp_Vec b2 = gp_Vec(n2).Crossed(t);
      const gp_Pnt essai = m.Translated(b2.Normalized() * 0.01);
      double u2, v2;
      ElSLib::Parameters(s2.Plane(), essai, u2, v2);
      BRepClass_FaceClassifier cl(f2, gp_Pnt2d(u2, v2), 1e-7);
      if (cl.State() != TopAbs_IN) b2.Reverse();
      // Concave si la face 2 part du côté où pointe la normale sortante de la face 1
      if (b2.Dot(gp_Vec(n1)) > 1e-6) vives.push_back(e);
    }
    if (!vives.empty())
      avis.push_back({0, std::to_string(vives.size()) + " angle(s) intérieur(s) vif(s) : une fraise laisse toujours un rayon ; prévoir un congé au moins égal au rayon de l'outil.", vives});
  }
  return avis;
}

}  // namespace prom
