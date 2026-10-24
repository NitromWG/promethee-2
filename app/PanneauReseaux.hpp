// Prométhée : navigateur de réseaux (lot L5, tâches 5.15 et 5.32 ; ERG-14, CAR-21). Tous les réseaux de la
// conception, groupés par classe, par motif (préfixe du nom) ou sans groupe, avec leur nombre de broches et la
// longueur routée ; un clic surligne le réseau partout (schéma et circuit), un double-clic le cadre ; « Renommer »
// change un réseau de la carte seule et son cuivre, « Nouveau réseau » en crée un pour le pinceau de réseau.
// Licence GPL-3.0-only.
#pragma once

#include <string>

#include <QWidget>

class AtelierElectronique;
class QComboBox;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

class PanneauReseaux : public QWidget {
  Q_OBJECT
 public:
  explicit PanneauReseaux(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  QTreeWidget* arbre() const { return m_arbre; }
  QLineEdit* recherche() const { return m_recherche; }
  QComboBox* groupement() const { return m_groupement; }
  int nombre() const { return m_nombre; }
  // Ligne d'un réseau (essais) ; nullptr s'il n'est pas montré
  QTreeWidgetItem* ligne(const std::string& reseau) const;

 private:
  void choisir(QTreeWidgetItem* it);
  void renommer();
  AtelierElectronique& m_atelier;
  QLineEdit* m_recherche;
  QComboBox* m_groupement;
  QTreeWidget* m_arbre;
  QLabel* m_bilan;
  int m_nombre = 0;
};
