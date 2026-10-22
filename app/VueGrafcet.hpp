// Prométhée : vue d'un grafcet. Le dessin normalisé (disposition calculée par le noyau), une étape ou une transition
// désignée à la souris, et pendant la simulation les étapes actives marquées d'un jeton, les transitions
// franchissables en vert. Le même dessin sert aux folios Grafcet (écran, PDF, DXF). Licence GPL-3.0-only.
#pragma once

#include <set>
#include <vector>

#include <QColor>
#include <QPointF>
#include <QWidget>

#include "promethee/electrique_grafcet.hpp"

class QPainter;

// Ce que le dessin marque : jetons des étapes actives, transitions franchissables, élément choisi
struct MarquesGrafcet {
  std::set<int> actives;
  std::set<std::size_t> franchissables;
  int etape = -1, transition = -1;
  bool simulation = false;
};

class VueGrafcet : public QWidget {
  Q_OBJECT
 public:
  explicit VueGrafcet(QWidget* parent = nullptr);
  void setGrafcet(const prom::elec::Grafcet* g);  // nullptr : rien à montrer
  void setChoix(int etape, int transition);
  void setSimulation(bool active, const std::set<int>& actives = {}, const std::set<std::size_t>& franchissables = {});
  int etapeChoisie() const { return m_etape; }
  int transitionChoisie() const { return m_transition; }
  using Marques = MarquesGrafcet;
  // Dessin commun : disposition en millimètres, posée en « origine » à « echelle » pixels par millimètre
  static void peindre(QPainter& g, const prom::elec::DispositionGrafcet& d, QPointF origine, double echelle, const QColor& encre, const QColor& discret,
                      const Marques& m = {});
  void cliquerEn(double xMm, double yMm, bool double_ = false);  // essais : clic au point du dessin (mm)
  QPointF versEcran(double xMm, double yMm) const;

 signals:
  void choisi(int etape, int transition);  // −1, −1 : rien
  void doubleClic(int etape, int transition);
  void supprimerDemande();  // Suppr sur l'élément choisi

 protected:
  void paintEvent(class QPaintEvent* e) override;
  void mousePressEvent(class QMouseEvent* e) override;
  void mouseDoubleClickEvent(class QMouseEvent* e) override;
  void keyPressEvent(class QKeyEvent* e) override;
  void resizeEvent(class QResizeEvent* e) override;

 private:
  void cadrer();
  std::pair<int, int> sous(double xMm, double yMm) const;
  prom::elec::DispositionGrafcet m_d;
  bool m_vide = true;
  int m_etape = -1, m_transition = -1;
  Marques m_marques;
  double m_echelle = 4;
  QPointF m_origine{20, 20};
};
