// Prométhée : bandeau d'outil des canevas (lot L1, tâches 1.6 et 1.7, ERG-02, ERG-17). Une bande de 28 px posée
// en haut du canevas, sur le dessin, qui ne décale pas la vue : elle nomme l'outil armé et l'objet, dit ce qu'il y a
// à savoir, et donne chaque action en bouton, sa touche écrite à côté ; la sortie (« Terminer », « Annuler ») est
// toujours le dernier bouton à droite. Elle paraît dès qu'un outil est armé, qu'un réseau est surligné ou qu'un
// geste dure, et disparaît à l'état neutre. Son texte est annoncé aux lecteurs d'écran à chaque changement.
// Licence GPL-3.0-only.
#pragma once

#include <vector>

#include <QWidget>

#include "OutilVue.hpp"

class QHBoxLayout;
class QLabel;
class QToolButton;

class BandeauOutil : public QWidget {
  Q_OBJECT
 public:
  static constexpr int HAUTEUR = 28;
  explicit BandeauOutil(QWidget* canevas);
  // Contenu vide : le bandeau se masque
  void montrer(const ContenuBandeau& c);
  QString texte() const;  // « Pose : Résistance · clic pour poser » (essais, accessibilité)
  QToolButton* bouton(const QString& id) const;

 signals:
  void actionFaite();  // un bouton a agi : la vue redonne le bandeau de l'outil, qui a pu changer d'état

 protected:
  void paintEvent(QPaintEvent*) override;
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void appliquerTheme();
  void placer();
  QLabel* m_texte = nullptr;
  QHBoxLayout* m_ligne = nullptr;
  std::vector<QToolButton*> m_boutons;
  ContenuBandeau m_contenu;
};
