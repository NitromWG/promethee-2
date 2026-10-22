// Prométhée : panneau de standardisation et mises à jour des articles (lot L4, tâches 4.10 et 4.21 ; BIB-10,
// BIB-20, BIB-21, BIB-26). Fenêtres non modales.
//
// Standardisation : pour chaque composant sans article réel (générique, ou posé d'un symbole nu), l'article que la
// recherche paramétrique trouve, avec un indice de confiance et la raison ; les lignes cochées se remplacent en une
// commande, les autres restent exclues.
// Mises à jour : un article de la bibliothèque (personnelle ou intégrée) qui diffère des composants posés d'après
// lui se propose, avec la liste des différences, en « Mettre à jour les 6 composants ».
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QDialog>

#include "promethee/articles.hpp"

class AtelierElectronique;
class QLabel;
class QPushButton;
class QTableWidget;

class DialogueStandardisation : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueStandardisation(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  const std::vector<prom::ecad::PropositionStandard>& propositions() const { return m_propositions; }
  QTableWidget* table() const { return m_table; }
  QPushButton* boutonRemplacer() const { return m_remplacer; }
  // Remplace les composants des lignes cochées, en un pas ; rend le nombre remplacé
  int appliquer();

 private:
  void majBouton();
  AtelierElectronique& m_atelier;
  std::vector<prom::ecad::PropositionStandard> m_propositions;
  QTableWidget* m_table;
  QPushButton* m_remplacer;
  QLabel* m_bilan;
};

class DialogueMisesAJour : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueMisesAJour(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  QTableWidget* table() const { return m_table; }
  // Texte du bouton de la ligne (« Mettre à jour les 6 composants ») et différences montrées
  QString texteBouton(int ligne) const;
  QString differences(int ligne) const;
  int mettreAJour(int ligne);

 private:
  AtelierElectronique& m_atelier;
  std::vector<prom::ecad::Article> m_articles;
  std::vector<std::vector<std::string>> m_composants;
  QTableWidget* m_table;
  QLabel* m_bilan;
};
