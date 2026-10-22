// Prométhée : huit mémoires de sélection dans un petit panneau posé sur le canevas (lot L2, tâche 2.26, ERG-29).
// Un clic rappelle la sélection gardée (avec Maj, l'ajoute), un clic sur une mémoire vide ou Ctrl+clic y garde la
// sélection en cours ; le clic droit propose les trois. Les mémoires sont portées par la sélection partagée : le
// schéma et le circuit les voient.
// Licence GPL-3.0-only.
#pragma once

#include <array>

#include <QFrame>

class QToolButton;
class SelectionElectronique;

class PanneauMemoires : public QFrame {
  Q_OBJECT
 public:
  PanneauMemoires(SelectionElectronique* sel, QWidget* parent);
  void setSelection(SelectionElectronique* sel);
  QToolButton* bouton(int i) const { return i >= 0 && i < static_cast<int>(m_boutons.size()) ? m_boutons[static_cast<std::size_t>(i)] : nullptr; }
  void cliquer(int i, bool garder, bool ajouter);  // la mémoire i : gardée, ou rappelée (ajoutée)

 private:
  void maj();
  SelectionElectronique* m_sel = nullptr;
  std::array<QToolButton*, 8> m_boutons{};
};
