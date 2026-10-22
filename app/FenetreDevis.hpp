// Prométhée : devis hors ligne (lot L8, tâche 8.18 ; FAB-30, FAB-31). Fenêtre non modale : JLCPCB et PCBWay côte à
// côte pour la quantité choisie, d'après les grilles datées des profils, sans réseau.
// Licence GPL-3.0-only.
#pragma once

#include <vector>

#include <QWidget>

#include "promethee/fabrication_avancee.hpp"

class AtelierElectronique;
class QSpinBox;
class QTableWidget;

class FenetreDevis : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreDevis(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  QSpinBox* quantite() const { return m_quantite; }
  QTableWidget* table() const { return m_table; }
  const std::vector<prom::ecad::OffreFabrication>& offres() const { return m_offres; }

 private:
  AtelierElectronique& m_atelier;
  QSpinBox* m_quantite;
  QTableWidget* m_table;
  std::vector<prom::ecad::OffreFabrication> m_offres;
};
