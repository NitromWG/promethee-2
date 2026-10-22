// Prométhée : périphériques d'entrée au-delà de la souris (lot L2, tâche 2.29, ERG-31). La souris 3D (3Dconnexion
// SpaceMouse et compatibles) par la bibliothèque de chaque système, chargée à l'exécution : libspnav sous Linux, le
// cadre 3DconnexionClient sous macOS, l'entrée brute (Raw Input, page HID 1, usage 8) sous Windows. Sans pilote ni
// appareil, rien ne change et la raison est dite. Les vues 2D suivent le mouvement (translation : défilement,
// poussée : zoom) ; la vue 3D tourne.
// Licence GPL-3.0-only.
#pragma once

#include <QObject>
#include <QString>

class QPointF;
class QWidget;

class SourisEspace : public QObject {
  Q_OBJECT
 public:
  static SourisEspace& instance();
  // Une fois, avec la fenêtre principale (sous Windows, l'entrée brute s'inscrit auprès d'elle)
  void demarrer(QWidget* fenetre);
  bool disponible() const { return m_disponible; }
  QString description() const { return m_description; }
  // Pour les essais : un mouvement comme en rendrait l'appareil
  void simuler(double tx, double ty, double tz, double rx, double ry, double rz) { emit mouvement(tx, ty, tz, rx, ry, rz); }
  // Une vue 2D suit le mouvement : la translation fait défiler, la poussée zoome autour du centre
  static void appliquer2D(QWidget* vue, QPointF& origine, double& echelle, double tx, double ty, double tz, double min, double max);

 signals:
  // Axes normalisés de −1 à 1 : X vers la droite, Y vers l'avant, Z vers le haut ; rotations autour de ces axes
  void mouvement(double tx, double ty, double tz, double rx, double ry, double rz);
  void bouton(int numero, bool appuye);

 private:
  SourisEspace() = default;
  void lireLinux();
  bool m_demarre = false, m_disponible = false;
  QString m_description = QStringLiteral("aucune souris 3D détectée");
  void* m_bibliotheque = nullptr;  // fonctions de libspnav ou de 3DconnexionClient
  friend class FiltreEntreeBrute;
};
