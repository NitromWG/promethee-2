// Prométhée : disposition en cercle ou en arc (lot L5, tâche 5.18 ; PLA-21). Centre, rayon, angle de départ, balayage,
// empreintes tournées vers le centre, repères renumérotés dans l'ordre du cercle ; un seul pas d'annulation.
// Licence GPL-3.0-only.
#pragma once

#include <QDialog>

#include "promethee/placement_circuit.hpp"

class QCheckBox;
class QDoubleSpinBox;

class DialogueCercle : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueCercle(prom::ecad::Pt centre, QWidget* parent = nullptr);
  prom::ecad::OptionsCercle options() const;
  QDoubleSpinBox *centreX, *centreY, *rayon, *depart, *balayage;
  QCheckBox *versCentre, *renumeroter;
};
