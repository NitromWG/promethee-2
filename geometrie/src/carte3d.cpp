// Prométhée : vue 3D d'une carte électronique. Licence GPL-3.0-only.
#include "promethee/carte3d.hpp"

#include <cmath>
#include <numbers>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <ShapeFix_Face.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

namespace prom {

namespace {
using namespace ecad;
gp_Pnt P(Pt p, double z) { return gp_Pnt(mm(p.x), -mm(p.y), z); }
TopoDS_Wire fil(const Polygone& poly, double z) {
  BRepBuilderAPI_MakePolygon m;
  for (const Pt& p : poly) m.Add(P(p, z));
  m.Close();
  return m.Wire();
}
// Plaque mince à l'altitude z (épaisseur e, vers le haut si e > 0), trous compris
TopoDS_Shape plaque(const Polygone& exterieur, const Polygones& trous, double z, double e) {
  if (exterieur.size() < 3) return {};
  try {
    BRepBuilderAPI_MakeFace mf(gp_Pln(gp_Pnt(0, 0, z), gp::DZ()), fil(exterieur, z));
    for (const auto& t : trous)
      if (t.size() >= 3) mf.Add(TopoDS::Wire(fil(t, z).Reversed()));
    if (!mf.IsDone()) return {};
    ShapeFix_Face fixe(mf.Face());
    fixe.Perform();
    return BRepPrimAPI_MakePrism(fixe.Face(), gp_Vec(0, 0, e)).Shape();
  } catch (const Standard_Failure&) {
    return {};
  }
}
std::string couleurVernis(const std::string& v) {
  if (v == "bleu") return "#1F4FA0";
  if (v == "rouge") return "#A32626";
  if (v == "noir") return "#1A1B20";
  if (v == "blanc") return "#E6E8EE";
  if (v == "violet") return "#4B2C8F";
  return "#1F6B45";
}
}  // namespace

Carte3D modele3DCarte(const ConceptionElec& c) {
  Carte3D r;
  BRep_Builder b;
  TopoDS_Compound tout;
  b.MakeCompound(tout);
  auto ajouter = [&](const TopoDS_Shape& s, const std::string& couleur) {
    if (s.IsNull()) return;
    b.Add(tout, s);
    r.couleurs.push_back({s, couleur});
  };
  const double t = c.circuit.empilage.epaisseur, cu = std::max(0.035, c.circuit.empilage.cuivre);
  const std::string vernis = couleurVernis(c.circuit.empilage.vernis);
  Polygones decoupes;
  for (const auto& d : c.circuit.contour.decoupes)
    if (d.size() >= 3) decoupes.push_back(orienter(d));
  ajouter(plaque(c.circuit.contour.polygone(), decoupes, -t, t), vernis);
  const Connectivite k = connectivite(c);
  const Remplissages zones = remplirZones(c, k);
  // Zones et pistes (sous le vernis : légèrement plus claires), pastilles étamées
  for (const auto& z : c.circuit.zones) {
    const auto it = zones.find(z.uuid);
    if (it == zones.end() || (z.couche != Couche::CuivreDessus && z.couche != Couche::CuivreDessous)) continue;
    const bool dessus = z.couche == Couche::CuivreDessus;
    for (const Ilot& il : ilots(it->second)) ajouter(plaque(il.exterieur, il.trous, dessus ? 0 : -t - cu, cu), "#2E8A57");
  }
  for (const auto& p : c.circuit.pistes) {
    if (p.couche != Couche::CuivreDessus && p.couche != Couche::CuivreDessous) continue;
    const bool dessus = p.couche == Couche::CuivreDessus;
    ajouter(plaque(oblong(p.a, p.b, p.largeur), {}, dessus ? 0 : -t - cu, cu), "#3A9A66");
  }
  for (const auto& p : pastilles(c, k)) {
    if (p.pastille.type == TypePastille::NonMetallisee) {
      ajouter(BRepPrimAPI_MakeCylinder(gp_Ax2(P(p.pastille.position, -t - 0.01), gp::DZ()), mm(p.pastille.percage) / 2, t + 0.02).Shape(), "#0B0F24");
      continue;
    }
    const bool dessus = !(p.pastille.type == TypePastille::CMS && p.dessous);
    const Polygones trou = p.pastille.percage > 0 ? Polygones{cercle(p.pastille.position, p.pastille.percage / 2)} : Polygones{};
    ajouter(plaque(p.forme, trou, dessus ? 0 : -t - cu, cu + 0.01), "#C8CCD4");
    if (p.pastille.type == TypePastille::Traversante) ajouter(plaque(p.forme, trou, -t - cu, cu + 0.01), "#C8CCD4");
  }
  for (const auto& v : c.circuit.vias) ajouter(plaque(cercle(v.p, v.diametre / 2), {cercle(v.p, v.percage / 2)}, 0, cu + 0.005), "#3A9A66");
  // Corps des composants, à la hauteur saisie quand elle est donnée (P4) ; une empreinte sans corps en reçoit un,
  // à son encombrement, dès qu'une hauteur est connue (T2)
  for (const auto& e : c.circuit.empreintes) {
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!d) continue;
    const double h = hauteurCorpsMm(c.composant(e.composant), *d);
    if (h <= 0) continue;
    const Boite emprise = empriseCorps(*d);
    const double l = mm(emprise.largeur()), p = mm(emprise.hauteur());
    // Centre de l'emprise dans le repère local, retourné comme versCarte le fait pour le dessous ; repère 3D : Y vers le haut
    const double cx = (e.dessous ? -1.0 : 1.0) * mm(emprise.centre().x), cy = -mm(emprise.centre().y);
    const bool rond = d->corps.rond && d->corps.largeur > 0;
    TopoDS_Shape corps = rond ? BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(cx, cy, 0), gp::DZ()), l / 2, h).Shape() : BRepPrimAPI_MakeBox(gp_Pnt(cx - l / 2, cy - p / 2, 0), l, p, h).Shape();
    gp_Trsf rot, pose, retourne;
    rot.SetRotation(gp::OZ(), e.rotation * std::numbers::pi / 180);
    if (e.dessous) retourne.SetMirror(gp_Ax2(gp_Pnt(0, 0, -t / 2), gp::DZ()));
    pose.SetTranslation(gp_Vec(mm(e.position.x), -mm(e.position.y), cu));
    corps = BRepBuilderAPI_Transform(corps, rot, true).Shape();
    corps = BRepBuilderAPI_Transform(corps, pose, true).Shape();
    if (e.dessous) corps = BRepBuilderAPI_Transform(corps, retourne, true).Shape();
    ajouter(corps, d->corps.couleur);
  }
  r.forme = tout;
  return r;
}

}  // namespace prom
