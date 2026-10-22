// Prométhée : éditeur du ruban, page « Ruban » des réglages (lot L1, tâche 1.20, ERG-22). À gauche, l'arbre du
// ruban : onglets, groupes, actions ; à droite, toutes les actions du registre. On glisse une action de la liste dans
// un groupe, une action ou un groupe à une autre place ; « Retirer », « Nouveau groupe », « Rétablir l'original ».
// Chaque changement vaut aussitôt : la description est gardée dans les réglages et le ruban se reconstruit.
// Licence GPL-3.0-only.
#pragma once

#include <QTreeWidget>
#include <QWidget>

#include "DescriptionRuban.hpp"

class QListWidget;

// L'arbre du ruban : seuls les déplacements qui gardent la forme d'un ruban sont acceptés (une action dans un
// groupe, un groupe dans un onglet ; les onglets ne bougent pas)
class ArbreRuban : public QTreeWidget {
  Q_OBJECT
 public:
  static constexpr int ROLE_ID = Qt::UserRole + 1;     // identifiant de l'action, ou de l'onglet
  static constexpr int ROLE_FORME = Qt::UserRole + 2;  // forme du groupe
  explicit ArbreRuban(QWidget* parent = nullptr);
  void remplir(const DescriptionRuban& d);
  DescriptionRuban description() const;
  // Une action déposée dans un groupe, à cet indice (-1 : à la fin) ; faux si la place ne convient pas
  bool deposer(const QString& action, QTreeWidgetItem* groupe, int indice = -1);
  // Un élément déplacé sous un parent de son niveau, à cet indice
  bool deplacer(QTreeWidgetItem* element, QTreeWidgetItem* parent, int indice);
  QTreeWidgetItem* groupe(const QString& onglet, const QString& titre) const;  // essais et scripts

 signals:
  void modifie();

 protected:
  void dragMoveEvent(QDragMoveEvent* e) override;
  void dropEvent(QDropEvent* e) override;

 private:
  static int niveau(const QTreeWidgetItem* it);
  QTreeWidgetItem* nouvelleAction(const QString& id);
  // Où irait un dépôt de ce niveau au point visé : le parent et l'indice
  bool cible(QPoint p, int niveauDepose, QTreeWidgetItem*& parent, int& indice) const;
};

class EditeurRuban : public QWidget {
  Q_OBJECT
 public:
  explicit EditeurRuban(QWidget* parent = nullptr);
  ArbreRuban* arbre() const { return m_arbre; }

 private:
  void enregistrer();
  ArbreRuban* m_arbre;
  QListWidget* m_disponibles;
  bool m_remplissage = false;
};
