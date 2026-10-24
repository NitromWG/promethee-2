// Prométhée : navigation commune aux deux vues 3D (lot L6, tâche 6.11 ; MEC-19, MEC-20), partagée par Vue3D (boîtier)
// et VuePiece (pièces, carte équipée) : six vues normalisées, quart de tour autour de l'axe de visée, pivot au point
// visé, perspective ou orthographique, grille de 1 à 10 mm, points de vue nommés capturés et rendus à 0,1° près.
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <string>

#include <V3d_View.hxx>
#include <gp_Pnt.hxx>

#include "promethee/electronique.hpp"

class Navigation3D {
 public:
  enum class Face { Dessus, Dessous, Avant, Arriere, Gauche, Droite, Iso };
  // « vue » rend la vue OpenCascade (nulle tant qu'elle n'est pas créée) ; « redessiner » est appelé après chaque
  // changement de caméra ; « pivot » reçoit le nouveau centre de rotation de la vue
  Navigation3D(std::function<Handle(V3d_View)()> vue, std::function<void()> redessiner, std::function<void(const gp_Pnt&)> pivot = {});

  void vueStandard(Face f);
  void quartDeTour(bool horaire);  // 90° autour de l'axe de visée
  void pivoterSur(const gp_Pnt& p);  // la caméra tourne désormais autour de ce point, sans saut
  void setOrthographique(bool oui);
  bool orthographique() const;
  void basculerProjection() { setOrthographique(!orthographique()); }
  // Grille au sol (plan XY) au pas donné, de 1 à 10 mm ; 0 la retire
  void setGrille(double pasMm);
  double grille() const { return m_grille; }
  // Point de vue de la caméra actuelle, et sa restitution exacte
  prom::ecad::PointDeVue capturer(const std::string& nom) const;
  void appliquer(const prom::ecad::PointDeVue& v);
  static V3d_TypeOfOrientation orientation(Face f);

 private:
  std::function<Handle(V3d_View)()> m_vue;
  std::function<void()> m_redessiner;
  std::function<void(const gp_Pnt&)> m_pivot;
  double m_grille = 0;
};
