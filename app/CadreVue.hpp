// Prométhée : cadre d'une vue 2D (lot L2, tâche 2.12 ; SAI-25, S14). Des barres de défilement fines aux bords de
// la vue, sous le bandeau de l'outil, qui disent où l'on est dans le dessin et le font défiler ; une vue
// d'ensemble repliable en bas à droite, qui montre le dessin entier et la zone affichée, et déplace la vue d'un clic
// ou d'un glisser. Le cadre ne connaît la vue que par ses accès : il sert au schéma et pourra servir au circuit.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <functional>

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;
class QScrollBar;
class QToolButton;
class QWidget;

class CadreVue : public QObject {
  Q_OBJECT
 public:
  struct Acces {
    std::function<QRectF()> contenu;                                           // le dessin entier, en millimètres
    std::function<QPointF()> origine;                                          // le point (0, 0) du dessin, à l'écran
    std::function<double()> echelle;                                           // pixels par millimètre
    std::function<void(QPointF)> deplacer;                                     // nouvelle origine de la vue
    std::function<std::uint64_t()> generation;                                 // change avec le dessin
    std::function<void(QPainter&, QPointF origine, double echelle)> peindre;  // le dessin, à cette origine et cette échelle
  };
  // « cle » : où se garde le choix de montrer la vue d'ensemble (réglages de l'application)
  CadreVue(QWidget* vue, Acces a, const QString& cle, int haut);
  void maj();  // après un changement d'origine, d'échelle, de taille ou de dessin
  void setEnsembleVisible(bool oui);
  bool ensembleVisible() const;
  void basculerEnsemble() { setEnsembleVisible(!ensembleVisible()); }
  QScrollBar* barreH() const { return m_h; }
  QScrollBar* barreV() const { return m_v; }
  QWidget* ensemble() const { return m_ensemble; }
  QToolButton* boutonEnsemble() const { return m_bouton; }
  // La vue d'ensemble : le point du dessin (mm) sous un point de la vignette, et la zone affichée dans la vignette
  QPointF versDessin(QPointF vignette) const;
  QRectF zoneAffichee() const;
  bool eventFilter(QObject* o, QEvent* e) override;

  static constexpr int EPAISSEUR = 12;  // des barres

 private:
  friend class VueEnsemble;
  void placer();
  void defiler();  // une barre a bougé : la vue suit
  QRectF etendue() const;  // le dessin, une marge autour, et ce qui est affiché (mm)
  QWidget* m_vue;
  Acces m_a;
  QString m_cle;
  int m_haut;  // place du bandeau de l'outil, en haut de la vue
  QScrollBar *m_h = nullptr, *m_v = nullptr;
  QWidget* m_ensemble = nullptr;
  QToolButton* m_bouton = nullptr;
  bool m_maj = false;  // les barres sont réglées par le cadre : leur signal ne déplace pas la vue
};
