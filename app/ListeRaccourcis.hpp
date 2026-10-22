// Prométhée : liste des raccourcis montrée par F1 (lot L1, tâche 1.8, ERG-03, G11). Elle se lit dans le registre des
// actions et se range par ce qui sert d'abord : l'outil en cours, la vue montrée, les deux canevas, l'atelier, puis
// Prométhée entier ; un champ filtre les lignes. Fenêtre sans mode : elle reste ouverte pendant le travail.
// Licence GPL-3.0-only.
#pragma once

#include <vector>

#include <QDialog>
#include <QString>
#include <QStringList>

struct ActionAtelier;
class QLabel;
class QLineEdit;
class QTreeWidget;

class ListeRaccourcis : public QDialog {
  Q_OBJECT
 public:
  struct Section {
    QString titre;
    std::vector<const ActionAtelier*> actions;
  };
  explicit ListeRaccourcis(QWidget* parent = nullptr);
  void montrer(const std::vector<Section>& sections);
  QTreeWidget* arbre() const { return m_arbre; }  // essais
  // Lignes d'une section, « libellé · touches » (essais)
  QStringList lignes(const QString& section) const;
  // Note sous la liste : la carte du menu à marques (1.19)
  void setNote(const QString& html);
  QString note() const;

 private:
  void filtrer(const QString& texte);
  QLineEdit* m_filtre = nullptr;
  QTreeWidget* m_arbre = nullptr;
  QLabel* m_note = nullptr;
};
