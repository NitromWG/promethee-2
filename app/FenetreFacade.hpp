// Prométhée : façade du boîtier (lot L6, tâches 6.14 et 6.15 ; MEC-24 à MEC-27) et cotes du contour piloté par une
// esquisse (6.13, MEC-29). Fenêtres non modales.
//
// La façade : la liste de ses éléments (découpes, légendes, échelles graduées) et son dessin à l'échelle ; chaque
// élément suit son composant. « Pour la sélection » pose la découpe et la légende des composants choisis (avec une
// échelle graduée au choix) ; « Contrôler » montre les défauts ; la sortie en SVG et en DXF est refusée tant qu'il en
// reste.
// Licence GPL-3.0-only.
#pragma once

#include <vector>

#include <QDialog>
#include <QWidget>

#include "promethee/facade.hpp"

class AtelierElectronique;
class QLabel;
class QTableWidget;
class QTreeWidget;

class ApercuFacade : public QWidget {
 public:
  explicit ApercuFacade(AtelierElectronique& a, QWidget* parent = nullptr);
  void setDefauts(std::vector<prom::ecad::DefautFacade> d) { m_defauts = std::move(d); update(); }

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  AtelierElectronique& m_atelier;
  std::vector<prom::ecad::DefautFacade> m_defauts;
};

class FenetreFacade : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreFacade(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  QTreeWidget* liste() const { return m_liste; }
  // Découpe et légende des composants choisis (avec leur échelle graduée) ; rend le nombre d'éléments posés
  int ajouterPourSelection(bool echelle);
  std::vector<prom::ecad::DefautFacade> controler();
  // Sortie : refusée tant que le contrôle trouve un défaut ; vrai si les deux fichiers sont écrits
  bool exporter(const QString& svg, const QString& dxf, QString* raison = nullptr);
  const prom::ecad::Facade* facade() const;

 private:
  AtelierElectronique& m_atelier;
  QTreeWidget* m_liste;
  ApercuFacade* m_apercu;
  QLabel* m_bilan;
};

class DialogueContourEsquisse : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueContourEsquisse(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  QTableWidget* table() const { return m_table; }
  bool changerCote(int contrainte, double valeur);

 private:
  AtelierElectronique& m_atelier;
  QTableWidget* m_table;
  QLabel* m_bilan;
};
