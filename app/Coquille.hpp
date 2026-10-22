// Prométhée : fenêtre principale à onglets. Une barre de titre intégrée en haut (menus du projet actif, accès
// rapides, recherche, boutons de fenêtre) et, en bas, une barre d'onglets comme dans un navigateur : chaque onglet
// porte un projet (ou l'accueil), avec son propre historique ; « + » ouvre un onglet, la croix le ferme.
// Licence GPL-3.0-only.
#pragma once

#include <QWidget>

class BarreTitre;
class FenetrePrincipale;
class QStackedWidget;
class QTabBar;

class Coquille : public QWidget {
  Q_OBJECT
public:
  Coquille();
  FenetrePrincipale* active() const;
  FenetrePrincipale* onglet(int i) const;
  int nombreOnglets() const;
  int indexActif() const;
  FenetrePrincipale* nouvelOnglet();           // un onglet sur l'accueil
  void ouvrirFichier(const QString& chemin);   // dans l'onglet actif s'il est vierge, sinon dans un nouvel onglet
  bool fermerOnglet(int index);                // demande d'enregistrer si besoin ; faux si l'utilisateur renonce
  void activer(int index);
  // Copies de récupération : démarre la session (copie des projets modifiés toutes les deux minutes) et, si
  // « proposer », propose les copies laissées par un lancement interrompu.
  void demarrerRecuperation(bool proposer);
  void sauvegarderRecuperation();              // copie maintenant les projets modifiés
  int proposerRecuperation();                  // rend le nombre de projets rouverts

protected:
  void closeEvent(class QCloseEvent* e) override;
  bool eventFilter(QObject* o, class QEvent* e) override;

private:
  void brancher(FenetrePrincipale* f);
  void majOnglet(FenetrePrincipale* f);
  Qt::Edges bordsSous(const QPoint& global) const;
  BarreTitre* m_barre = nullptr;
  QStackedWidget* m_pile;
  QTabBar* m_onglets;
  bool m_cadreIntegre = true;
  bool m_agrandi = false;
  QRect m_geometrieNormale;
  void basculerAgrandi();
  class QLabel* m_etat = nullptr;
  class QTimer* m_minuterieRecuperation = nullptr;
};
