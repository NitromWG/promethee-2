// Prométhée : formulaire d'un appareil électrique personnel, dans le panneau de droite (jamais une fenêtre à part).
// Nom, lettre de repère, marquage, bornes côté par côté ; l'aperçu du symbole suit chaque frappe, les défauts
// (borne en double, lettre invalide…) s'affichent dessous et retiennent le bouton. Fabricant, référence, prix,
// montage, dimensions et pertes alimentent la nomenclature, l'armoire et son bilan thermique. Licence GPL-3.0-only.
#pragma once

#include <string>

#include <QGroupBox>

#include "promethee/electrique.hpp"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

class FormulaireAppareil : public QGroupBox {
  Q_OBJECT
 public:
  // « poses » : appareils déjà posés de ce type (modification), pour le dire avant d'appliquer
  FormulaireAppareil(const prom::elec::SpecAppareil& depart, bool modification, int poses, QWidget* parent = nullptr);
  prom::elec::SpecAppareil spec() const;
  // Lettres de repère proposées, avec leur sens (CEI 81346-2) : « K — relais, automate, régulateur »…
  static QStringList lettresProposees();

 signals:
  void modifie(const prom::elec::SpecAppareil& a);  // à chaque frappe : l'atelier garde la saisie
  void valide(const prom::elec::SpecAppareil& a);
  void annule();

 private:
  QLineEdit* ligne(const QString& nom, const QString& valeur, const QString& indice);
  void changement();

  std::string m_id;
  QLineEdit* m_nom = nullptr;
  QComboBox* m_lettre = nullptr;
  QLineEdit* m_marquage = nullptr;
  QLineEdit* m_haut = nullptr;
  QLineEdit* m_bas = nullptr;
  QLineEdit* m_gauche = nullptr;
  QLineEdit* m_droite = nullptr;
  QLineEdit* m_fabricant = nullptr;
  QLineEdit* m_reference = nullptr;
  QLineEdit* m_description = nullptr;
  QDoubleSpinBox* m_prix = nullptr;
  QComboBox* m_montage = nullptr;
  QDoubleSpinBox* m_largeur = nullptr;
  QDoubleSpinBox* m_hauteur = nullptr;
  QDoubleSpinBox* m_profondeur = nullptr;
  QDoubleSpinBox* m_pertes = nullptr;
  QLabel* m_apercu = nullptr;
  QLabel* m_defauts = nullptr;
  QPushButton* m_valider = nullptr;
};
