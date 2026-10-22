// Prométhée : insertion d'un dessin DXF dans l'esquisse en cours (M1). Unité lue dans le fichier et modifiable,
// échelle, placement (comme dans le fichier, centré ou coin bas-gauche sur l'origine, décalage), calques à
// reprendre (couleur, nombre de traits, éteints et gelés décochés), relations horizontales et verticales,
// géométrie fixée ou en construction. L'aperçu montre ce qui sera inséré, à l'échelle, autour de l'origine.
// Licence GPL-3.0-only.
#pragma once

#include <QDialog>

#include "promethee/dxf_dessin.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;

class DialogueImportDxf : public QDialog {
  Q_OBJECT
 public:
  DialogueImportDxf(prom::dxf::DessinDxf dessin, const QString& nomFichier, QWidget* parent = nullptr);
  prom::dxf::OptionsImportDxf options() const;
  void setOptions(const prom::dxf::OptionsImportDxf& o);
  // Ce que l'insertion donnerait (essai sur une esquisse vide)
  prom::dxf::RapportImportDxf essai() const;
  const prom::dxf::DessinDxf& dessin() const { return m_dessin; }

 private:
  void majApercu();
  prom::dxf::DessinDxf m_dessin;
  QComboBox *m_unite = nullptr, *m_placement = nullptr;
  QDoubleSpinBox *m_echelle = nullptr, *m_dx = nullptr, *m_dy = nullptr;
  QListWidget* m_calques = nullptr;
  QCheckBox *m_calquesEsquisse = nullptr, *m_relations = nullptr, *m_fixer = nullptr, *m_construction = nullptr;
  QLabel* m_bilan = nullptr;
  class ApercuDxf* m_apercu = nullptr;
};
