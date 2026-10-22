// Prométhée : tableau de tous les objets de la conception, ouvert comme une vue de plus de l'atelier (lot L3, tâche
// 3.21 ; ERG-41). Une ligne par objet, ses enfants à la suite (un composant, ses symboles posés, son empreinte), puis
// les fils, étiquettes et textes de chaque feuille, les pistes, vias, zones et textes de la carte ; une colonne par
// propriété. On écrit une cellule, on colle un bloc venu d'un tableur, ou on applique une règle aux cellules choisies
// (formule en « x » : « x + 2,54 » ; remplacement « de → à » ; préfixe ; suffixe) : chaque fois un seul pas
// d'annulation. Une ligne choisie choisit son objet dans les autres vues.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QWidget>

#include "promethee/objets.hpp"

class AtelierElectronique;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

class TableauObjets : public QWidget {
  Q_OBJECT
 public:
  explicit TableauObjets(AtelierElectronique& a, QWidget* parent = nullptr);
  void recharger();
  QTableWidget* table() const { return m_table; }
  // Essais : la ligne d'un objet, la colonne d'une propriété ; écrire une cellule, coller un bloc, appliquer la règle
  int ligne(prom::ecad::GenreObjet g, const std::string& id) const;
  static int colonne(prom::ecad::ProprieteObjet p);
  void ecrire(int ligne, int colonne, const QString& texte);
  void collerTexte(int ligne, int colonne, const QString& texte);
  // Règle sur les cellules choisies ; rend le nombre de cellules transformées
  int appliquerRegle(prom::ecad::RegleTexte::Genre genre, const QString& de, const QString& a);
  QString bilan() const;

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void remplir();
  void majCellules();
  void filtrer();
  void ecrireEnAttente();
  void choisirObjet(int ligne);
  AtelierElectronique& m_atelier;
  std::vector<prom::ecad::LigneObjet> m_lignes;
  std::vector<std::pair<int, int>> m_enAttente;  // (indice de l'objet, colonne) écrits par l'utilisateur
  bool m_ecriturePrevue = false;
  QTableWidget* m_table;
  QLineEdit* m_filtre;
  QComboBox* m_genres;
  QComboBox* m_regle;
  QLineEdit *m_de, *m_a;
  QLabel* m_bilan;
};
