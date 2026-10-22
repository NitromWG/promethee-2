// Prométhée : cartouche du tracé, près de la tête de la piste (lot L5, tâche 5.20 ; ROU-04, ROU-20). Réseau, couche,
// largeur, via et mode du routeur ; chaque valeur se change d'un clic : la largeur passe à la suivante (sa flèche
// les liste toutes), la via se pose, le mode change. Il suit la tête du tracé, pas la souris : on peut l'atteindre.
// Licence GPL-3.0-only.
#pragma once

#include <vector>

#include <QFrame>

#include "VueCircuit.hpp"

class QLabel;
class QToolButton;

class CartoucheRoutage : public QFrame {
  Q_OBJECT
 public:
  explicit CartoucheRoutage(VueCircuit& v);
  void maj(const QString& reseau, prom::ecad::Couche couche, prom::ecad::Nm largeur, prom::ecad::Nm classe, prom::ecad::Nm via, VueCircuit::ModeRouteur mode);
  QToolButton* boutonLargeur() const { return m_largeur; }
  QToolButton* boutonVia() const { return m_via; }
  QToolButton* boutonMode() const { return m_mode; }
  QLabel* texteReseau() const { return m_reseau; }
  // Largeurs proposées : celle de la classe, puis les courantes
  static std::vector<prom::ecad::Nm> largeurs(prom::ecad::Nm classe);

 private:
  VueCircuit& m_vue;
  QLabel* m_reseau;
  QToolButton *m_largeur, *m_via, *m_mode;
  prom::ecad::Nm m_valeur = 0, m_classe = 0;
};
