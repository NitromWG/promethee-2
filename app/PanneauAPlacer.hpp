// Prométhée : liste « À placer » du circuit (lot L5, tâche 5.2 ; PLA-01, PLA-02, C1). Les empreintes que la
// synchronisation a rangées hors de la carte, groupées par feuille du schéma ; un groupe ou un composant se glisse sur
// la carte et s'y pose en rangées serrées, sans recouvrement ; « Poser sur la carte » le pose au coin libre.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QWidget>

class AtelierElectronique;
class QLabel;
class QMimeData;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class PanneauAPlacer : public QWidget {
  Q_OBJECT
 public:
  explicit PanneauAPlacer(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  QTreeWidget* arbre() const { return m_arbre; }
  int nombre() const { return m_nombre; }
  // Composants d'une ligne (un groupe : tous les siens)
  std::vector<std::string> composantsDe(const QTreeWidgetItem* it) const;
  // Données du glisser vers la carte (format « application/x-promethee-aplacer »)
  static QMimeData* donnees(const std::vector<std::string>& composants);
  int poserChoisis();  // les lignes choisies, au coin libre de la carte

 private:
  AtelierElectronique& m_atelier;
  QTreeWidget* m_arbre;
  QLabel* m_bilan;
  QPushButton* m_poser;
  int m_nombre = 0;
};
