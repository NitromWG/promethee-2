// Prométhée : tableau d'édition des champs de tous les composants d'une conception, comme l'éditeur de champs
// de symboles de KiCad. Valeurs, empreintes, fabricants, références fabricant, fournisseurs, descriptions, fiches,
// « ne pas monter », « hors carte » et champs libres, en une seule vue : tri par colonne, filtre, copier-coller
// de blocs de cellules (tabulations, comme un tableur), répétition d'une valeur sur la sélection, repères en
// double signalés. Rien n'est écrit avant « Appliquer », qui fait un seul pas d'annulation.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QDialog>

#include "promethee/electronique.hpp"

class QLineEdit;
class QTableWidget;
class QLabel;

class TableauChamps : public QDialog {
  Q_OBJECT
 public:
  explicit TableauChamps(const prom::ecad::ConceptionElec& c, QWidget* parent = nullptr);
  // Composants avec les valeurs du tableau (dans l'ordre de la conception)
  std::vector<prom::ecad::ComposantElec> composants() const;
  // Essais : écrire une cellule comme le ferait l'utilisateur, coller un bloc
  bool ecrire(const std::string& reference, const QString& colonne, const QString& valeur);
  void collerTexte(int ligne, int colonne, const QString& texte);
  int colonne(const QString& titre) const;
  int ligne(const std::string& reference) const;
  void ajouterChamp(const QString& nom);
  QTableWidget* table() const { return m_table; }

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void remplir();
  void majDoubles();
  void filtrer(const QString& texte);
  void copierSelection() const;
  void repeterSurSelection();

  std::vector<prom::ecad::ComposantElec> m_composants;
  std::vector<std::string> m_champsLibres;
  QTableWidget* m_table = nullptr;
  QLineEdit* m_filtre = nullptr;
  QLabel* m_bilan = nullptr;
};
