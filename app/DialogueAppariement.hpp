// Prométhée : liens perdus entre un schéma et une carte (lot L3, tâche 3.19 ; PLA-17, PLA-18). Une carte sans liens
// (un circuit KiCad, une autre carte du projet) s'apparie au schéma ouvert dans un tableau à deux colonnes : par
// repère, puis par valeur et empreinte, puis les orphelins au coût minimal (algorithme hongrois) ; une ligne glissée
// sur une autre échange leurs empreintes, à la main. « Appliquer » reporte placement et cuivre en un seul pas
// d'annulation ; les écarts qui restent passent au panneau des écarts.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QDialog>
#include <QTableWidget>

#include "promethee/synchronisation.hpp"

class AtelierElectronique;
class QLabel;
class QPushButton;

// Tableau où l'on glisse une ligne sur une autre pour échanger leur colonne de droite
class TableauPaires : public QTableWidget {
  Q_OBJECT
 public:
  using QTableWidget::QTableWidget;
 signals:
  void echanger(int de, int vers);

 protected:
  void mousePressEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;

 private:
  int m_depart = -1;
};

class DialogueAppariement : public QDialog {
  Q_OBJECT
 public:
  DialogueAppariement(AtelierElectronique& a, prom::ecad::ConceptionElec source, QWidget* parent = nullptr);
  TableauPaires* table() const { return m_table; }
  const std::vector<prom::ecad::Appariement>& paires() const { return m_paires; }
  // Échange les empreintes de deux lignes (glisser une ligne sur l'autre)
  void echanger(int ligneA, int ligneB);
  int appliquer();  // rend le nombre d'empreintes reportées

 private:
  void remplir();
  AtelierElectronique& m_atelier;
  prom::ecad::ConceptionElec m_source;
  std::vector<prom::ecad::EmpreinteSansLien> m_carte;
  std::vector<std::string> m_composants;   // une ligne par composant du schéma (UUID), puis les empreintes seules
  std::vector<int> m_empreinteDeLigne;     // indice dans la carte, −1 sans empreinte
  std::vector<prom::ecad::Appariement::Par> m_par;
  std::vector<prom::ecad::Appariement> m_paires;
  TableauPaires* m_table;
  QLabel* m_bilan;
};
