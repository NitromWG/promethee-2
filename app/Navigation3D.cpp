// Prométhée : navigation commune aux deux vues 3D (lot L6, tâche 6.11). Voir Navigation3D.hpp.
// Licence GPL-3.0-only.
#include "Navigation3D.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <Aspect_GridDrawMode.hxx>
#include <Aspect_GridType.hxx>
#include <Graphic3d_Camera.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Ax1.hxx>
#include <gp_Dir.hxx>

Navigation3D::Navigation3D(std::function<Handle(V3d_View)()> vue, std::function<void()> redessiner, std::function<void(const gp_Pnt&)> pivot)
    : m_vue(std::move(vue)), m_redessiner(std::move(redessiner)), m_pivot(std::move(pivot)) {}

V3d_TypeOfOrientation Navigation3D::orientation(Face f) {
  switch (f) {
    case Face::Dessus: return V3d_Zpos;
    case Face::Dessous: return V3d_Zneg;
    case Face::Avant: return V3d_Yneg;
    case Face::Arriere: return V3d_Ypos;
    case Face::Gauche: return V3d_Xneg;
    case Face::Droite: return V3d_Xpos;
    case Face::Iso: return V3d_XposYnegZpos;
  }
  return V3d_XposYnegZpos;
}

void Navigation3D::vueStandard(Face f) {
  const Handle(V3d_View) v = m_vue();
  if (v.IsNull()) return;
  v->SetProj(orientation(f));
  v->FitAll(0.05, false);
  if (m_redessiner) m_redessiner();
}

void Navigation3D::quartDeTour(bool horaire) {
  const Handle(V3d_View) v = m_vue();
  if (v.IsNull()) return;
  const Handle(Graphic3d_Camera) c = v->Camera();
  // Le « haut » de la caméra tourne de 90° autour de l'axe de visée
  const gp_Dir axe = c->Direction();
  gp_Dir haut = c->Up();
  haut.Rotate(gp_Ax1(gp::Origin(), axe), (horaire ? -1 : 1) * std::numbers::pi / 2);
  c->SetUp(haut);
  v->Redraw();
  if (m_redessiner) m_redessiner();
}

void Navigation3D::pivoterSur(const gp_Pnt& p) {
  const Handle(V3d_View) v = m_vue();
  if (v.IsNull()) return;
  if (m_pivot) m_pivot(p);
  if (m_redessiner) m_redessiner();
}

void Navigation3D::setOrthographique(bool oui) {
  const Handle(V3d_View) v = m_vue();
  if (v.IsNull()) return;
  v->Camera()->SetProjectionType(oui ? Graphic3d_Camera::Projection_Orthographic : Graphic3d_Camera::Projection_Perspective);
  v->Redraw();
  if (m_redessiner) m_redessiner();
}

bool Navigation3D::orthographique() const {
  const Handle(V3d_View) v = m_vue();
  return !v.IsNull() && v->Camera()->ProjectionType() == Graphic3d_Camera::Projection_Orthographic;
}

void Navigation3D::setGrille(double pasMm) {
  const Handle(V3d_View) v = m_vue();
  m_grille = pasMm <= 0 ? 0 : std::clamp(pasMm, 1.0, 10.0);
  if (v.IsNull()) return;
  const Handle(V3d_Viewer) visu = v->Viewer();
  if (m_grille <= 0) visu->DeactivateGrid();
  else {
    visu->SetRectangularGridValues(0, 0, m_grille, m_grille, 0);
    visu->SetRectangularGridGraphicValues(500, 500, 0);
    visu->ActivateGrid(Aspect_GT_Rectangular, Aspect_GDM_Lines);
  }
  v->Redraw();
  if (m_redessiner) m_redessiner();
}

prom::ecad::PointDeVue Navigation3D::capturer(const std::string& nom) const {
  prom::ecad::PointDeVue p;
  p.nom = nom;
  const Handle(V3d_View) v = m_vue();
  if (v.IsNull()) return p;
  const Handle(Graphic3d_Camera) c = v->Camera();
  const gp_Pnt cible = c->Center();
  const gp_Dir d = c->Direction();  // de l'œil vers la cible
  p.cible[0] = cible.X(), p.cible[1] = cible.Y(), p.cible[2] = cible.Z();
  p.azimut = std::atan2(-d.Y(), -d.X()) * 180 / std::numbers::pi;
  p.elevation = std::asin(std::clamp(-d.Z(), -1.0, 1.0)) * 180 / std::numbers::pi;
  p.distance = c->Distance();
  p.orthographique = c->ProjectionType() == Graphic3d_Camera::Projection_Orthographic;
  p.champ = c->FOVy();
  // En orthographique, l'échelle est la hauteur vue : gardée par la distance et le champ
  if (p.orthographique) p.distance = c->Scale() / 2 / std::tan(p.champ * std::numbers::pi / 360);
  return p;
}

void Navigation3D::appliquer(const prom::ecad::PointDeVue& p) {
  const Handle(V3d_View) v = m_vue();
  if (v.IsNull()) return;
  const Handle(Graphic3d_Camera) c = v->Camera();
  const double a = p.azimut * std::numbers::pi / 180, e = p.elevation * std::numbers::pi / 180;
  const gp_Pnt cible(p.cible[0], p.cible[1], p.cible[2]);
  const gp_Vec versOeil(std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e));
  c->SetProjectionType(p.orthographique ? Graphic3d_Camera::Projection_Orthographic : Graphic3d_Camera::Projection_Perspective);
  c->SetFOVy(std::clamp(p.champ, 5.0, 120.0));
  c->SetCenter(cible);
  c->SetEye(cible.Translated(versOeil * std::max(1e-3, p.distance)));
  // Le haut : le z du monde, sauf en vue d'aplomb
  c->SetUp(std::abs(std::sin(e)) > 0.999 ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1));
  if (p.orthographique) c->SetScale(2 * p.distance * std::tan(c->FOVy() * std::numbers::pi / 360));
  v->Redraw();
  if (m_redessiner) m_redessiner();
}
