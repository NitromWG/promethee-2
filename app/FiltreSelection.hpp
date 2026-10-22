// Prométhée : filtre de sélection de l'atelier électronique (lot L1, tâche 1.18, SAI-18). Un bouton de la barre
// d'état ouvre la liste des genres d'objets, au schéma et au circuit ; un genre décoché ne se prend plus ni au clic
// ni au cadre. Quand un clic ne touche que des objets exclus, le bouton clignote pour dire que le filtre l'a bloqué.
// Licence GPL-3.0-only.
#pragma once

#include <QToolButton>

class QTimer;
class SelectionElectronique;

class BoutonFiltre : public QToolButton {
  Q_OBJECT
 public:
  explicit BoutonFiltre(SelectionElectronique* selection, QWidget* parent = nullptr);
  int clignotements() const { return m_clignotements; }  // essais : clics bloqués signalés
  bool allume() const { return m_allume; }

 private:
  void majTexte();
  void clignoter();
  void allumer(bool oui);
  SelectionElectronique* m_selection;
  QTimer* m_minuterie;
  int m_etapes = 0, m_clignotements = 0;
  bool m_allume = false;
};
