// Prométhée : périphérique de dessin qui écrit du DXF (R12, millimètres, origine en bas à gauche). Tout ce que Qt
// peint dessus devient une entité DXF : traits et polygones en polylignes, cercles, aplats en solides, textes en
// TEXT ajustés à leur place. Un folio s'exporte ainsi avec le code même qui le dessine à l'écran et en PDF. Le
// dessinateur choisit le calque des entités qui suivent (cadre, fils, symboles…). Licence GPL-3.0-only.
#pragma once

#include <memory>

#include <QByteArray>
#include <QPaintDevice>
#include <QString>

class QPainter;
class QPaintEngine;

class PeintreDxf : public QPaintDevice {
 public:
  PeintreDxf(double largeurMm, double hauteurMm, double pixelsParMm = 10);
  ~PeintreDxf() override;
  PeintreDxf(const PeintreDxf&) = delete;
  PeintreDxf& operator=(const PeintreDxf&) = delete;

  QPaintEngine* paintEngine() const override;
  double pixelsParMm() const { return m_pixelsParMm; }
  // Calque des entités suivantes (« CADRE », « FILS »…) ; les textes posés sur « SYMBOLES » vont sur « REPERES »
  void choisirCalque(const QString& nom);
  // Sans effet quand le peintre ne dessine pas en DXF : le code de dessin l'appelle sans se soucier de la cible
  static void calque(QPainter& g, const QString& nom);
  QByteArray document() const;  // en-tête, types de ligne, calques, entités
  int entites() const;

 protected:
  int metric(PaintDeviceMetric m) const override;

 private:
  class Moteur;
  double m_largeur, m_hauteur, m_pixelsParMm;
  std::unique_ptr<Moteur> m_moteur;
};
