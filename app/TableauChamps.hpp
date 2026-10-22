// Prométhée : tableau d'édition des champs des composants d'une conception, comme l'éditeur de champs de symboles
// de KiCad. Valeurs, empreintes, fabricants, références fabricant, fournisseurs, descriptions, fiches, « ne pas
// monter », « hors carte » et champs libres, en une seule vue : tri par colonne, filtre, copier-coller de blocs de
// cellules (tabulations, comme un tableur), Ctrl+D pour recopier vers le bas, repères en double signalés. Depuis le
// lot L3 (3.9, 3.15 ; B13, P14, SAI-16) : un panneau ancré sous le canevas, utilisable sur un écran de 1 024 px, et
// non plus une fenêtre ; chaque modification s'écrit aussitôt (un collage ou un Ctrl+D font un seul pas) ; une ligne
// par composant ou par valeur regroupée ; vues nommées (colonnes montrées) et portée (conception, feuille, sélection).
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <QWidget>

#include "promethee/electronique.hpp"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QTableWidget;
class QLabel;

class TableauChamps : public QWidget {
  Q_OBJECT
 public:
  explicit TableauChamps(QWidget* parent = nullptr);
  explicit TableauChamps(const prom::ecad::ConceptionElec& c, QWidget* parent = nullptr);
  // Le modèle a changé : le tableau se relit (sauf pendant une saisie) ; « portee » : les composants montrés (tous si
  // nul)
  void recharger(const prom::ecad::ConceptionElec& c, const std::set<std::string>* portee = nullptr);
  enum class Portee : std::uint8_t { Conception, Feuille, Selection };
  Portee portee() const;
  void setPortee(Portee p);
  void setRegroupe(bool oui);
  bool regroupe() const;
  // Vues nommées : les colonnes montrées (« Approvisionnement », « Montage », et celles enregistrées)
  QStringList vues() const;
  void choisirVue(const QString& nom);
  void enregistrerVue(const QString& nom);  // les colonnes montrées maintenant, gardées sous ce nom
  QString vue() const;
  // Composants avec les valeurs du tableau (dans l'ordre de la conception)
  std::vector<prom::ecad::ComposantElec> composants() const;
  const std::vector<prom::ecad::ComposantElec>& base() const { return m_composants; }  // tels que le tableau les a lus
  // Essais : écrire une cellule comme le ferait l'utilisateur, coller un bloc
  bool ecrire(const std::string& reference, const QString& colonne, const QString& valeur);
  void collerTexte(int ligne, int colonne, const QString& texte);
  int colonne(const QString& titre) const;
  int ligne(const std::string& reference) const;
  void ajouterChamp(const QString& nom);
  QTableWidget* table() const { return m_table; }

 signals:
  // Une modification de l'utilisateur (cellule, collage, Ctrl+D) : à écrire dans le modèle, en un pas
  void modifie();
  void porteeChangee();

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void remplir();
  void majDoubles();
  void filtrer(const QString& texte);
  void copierSelection() const;
  void repeterSurSelection();

  void signaler();
  void appliquerVue();
  void remplirVues();
  void majCellules();
  std::vector<std::vector<std::string>> lignesVoulues() const;

  std::vector<prom::ecad::ComposantElec> m_composants;
  std::vector<std::string> m_champsLibres;
  std::set<std::string> m_champsAjoutes;  // colonnes ajoutées, gardées même vides
  std::vector<std::vector<std::string>> m_lignes;  // composants de chaque ligne, dans l'ordre du remplissage
  std::map<std::string, std::string> m_cles;      // clé de regroupement de chaque composant
  std::set<std::string> m_portee;
  bool m_porteeActive = false;
  QTableWidget* m_table = nullptr;
  QLineEdit* m_filtre = nullptr;
  QLabel* m_bilan = nullptr;
  QComboBox* m_choixPortee = nullptr;
  QComboBox* m_choixVue = nullptr;
  QCheckBox* m_regrouper = nullptr;
  QString m_vue, m_titre;
  bool m_signalPrevu = false;
};
