// Prométhée : création d'une empreinte par tableau de pastilles (connecteurs, barrettes, borniers, boîtiers DIL),
// avec aperçu à l'échelle des pastilles, de leurs numéros, de la sérigraphie et de l'encombrement.
// Licence GPL-3.0-only.
#pragma once

#include <set>
#include <string>

#include <QDialog>

#include "promethee/circuit.hpp"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QLabel;

class DialogueEmpreinte : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueEmpreinte(int pastillesVoulues = 0, QWidget* parent = nullptr);
  // Identifiants déjà pris (projet et bibliothèque personnelle) : le nom proposé est toujours libre (« Connecteur-2 »)
  // et un nom pris est signalé avant de créer (B10) ; couronne minimale des règles, vérifiée à la création (B15)
  void setContexte(const std::set<std::string>& idsPris, prom::ecad::Nm couronneMin);
  prom::ecad::SpecEmpreinte spec() const;
  void setSpec(const prom::ecad::SpecEmpreinte& s);
  prom::ecad::DefEmpreinte empreinte() const { return prom::ecad::genererEmpreinteSpec(spec()); }

 private:
  void majApercu();
  QLineEdit* m_nom = nullptr;
  QComboBox *m_type = nullptr, *m_forme = nullptr, *m_numerotation = nullptr;
  QSpinBox *m_rangees = nullptr, *m_colonnes = nullptr;
  QDoubleSpinBox *m_pasX = nullptr, *m_pasY = nullptr, *m_largeur = nullptr, *m_hauteur = nullptr, *m_percage = nullptr, *m_corps = nullptr;
  QCheckBox* m_carree = nullptr;
  QLabel* m_bilan = nullptr;
  class ApercuEmpreinte* m_apercu = nullptr;
  class QPushButton* m_creer = nullptr;
  int m_voulues = 0;
  std::set<std::string> m_pris;
  prom::ecad::Nm m_couronneMin = 0;
};
