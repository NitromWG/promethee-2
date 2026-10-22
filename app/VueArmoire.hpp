// Prométhée : vue d'une armoire électrique — plaque de fond (rails DIN, goulottes, appareils à leur taille réelle)
// ou porte (boutons, voyants) ; un appareil se déplace à la souris et s'accroche au rail le plus proche (au module
// de 18 mm dans un coffret de logement). Chaque déplacement passe par le document (annulable).
// Licence GPL-3.0-only.
#pragma once

#include <string>

#include <QWidget>

#include "promethee/electrique.hpp"

class Document;

class VueArmoire : public QWidget {
  Q_OBJECT
 public:
  explicit VueArmoire(Document* doc, QWidget* parent = nullptr);
  void setArmoire(int projet, int armoire);
  int armoire() const { return m_armoire; }
  void setPorte(bool porte);
  bool porte() const { return m_porte; }
  const std::string& selection() const { return m_selection; }
  void selectionner(const std::string& composant);
  void recadrer();
  void retirerSelection();  // l'appareil quitte l'armoire (il reste au schéma)
  QPointF versEcran(double xmm, double ymm) const { return {m_origine.x() + xmm * m_echelle, m_origine.y() + ymm * m_echelle}; }
  // Essais : glisser un appareil jusqu'à ce point (mm), comme à la souris
  void deplacerVers(const std::string& composant, double xmm, double ymm);

 signals:
  void selectionChangee();
  void message(const QString& html);

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void resizeEvent(QResizeEvent*) override;

 private:
  const prom::elec::ProjetElectrique* projet() const;
  const prom::elec::Armoire* armoireCourante() const;
  std::string appareilSous(QPointF q) const;
  QPointF monde(QPointF q) const { return {(q.x() - m_origine.x()) / m_echelle, (q.y() - m_origine.y()) / m_echelle}; }
  // Position accrochée (mm, coin haut gauche) et rail porteur pour un appareil amené en (x, y)
  std::pair<QPointF, int> accrocher(const std::string& composant, QPointF coin) const;
  void appliquer(const std::string& composant, QPointF coin, int rail);

  Document* m_doc;
  int m_projet = -1, m_armoire = -1;
  bool m_porte = false;
  std::string m_selection;
  double m_echelle = 1;
  QPointF m_origine{20, 20};
  QPointF m_souris, m_pression, m_decalage;
  bool m_glisse = false, m_panoramique = false, m_appuye = false, m_cadre = false;
};
