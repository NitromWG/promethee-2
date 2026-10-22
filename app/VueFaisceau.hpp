// Prométhée : planche de faisceau. Nœuds (connecteurs du schéma ou dérivations) et branches, longueurs cotées
// (dessinées ou imposées) ; outils : choisir et déplacer, poser un nœud, tirer une branche d'un nœud à l'autre.
// Chaque modification passe par le document (annulable). Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <string>

#include <QWidget>

#include "promethee/electrique.hpp"

class Document;

class VueFaisceau : public QWidget {
  Q_OBJECT
 public:
  enum class Outil { Selection, Noeud, Branche };
  explicit VueFaisceau(Document* doc, QWidget* parent = nullptr);
  void setFaisceau(int projet, int faisceau);
  int faisceau() const { return m_faisceau; }
  void setOutil(Outil o);
  Outil outil() const { return m_outil; }
  // Nœud (UUID) ou branche (« b:indice ») choisi
  const std::string& selection() const { return m_selection; }
  void selectionner(const std::string& s);
  void supprimerSelection();
  void recadrer();
  // Essais : clic à cette position de la planche (mm)
  void cliquerEn(double xmm, double ymm);

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

 private:
  const prom::elec::ProjetElectrique* projet() const;
  const prom::elec::Faisceau* faisceauCourant() const;
  QPointF ecran(prom::ecad::Pt p) const;
  prom::ecad::Pt monde(QPointF q) const;
  std::string objetSous(QPointF q) const;
  void clic(QPointF q);
  void modifier(const std::function<void(prom::elec::Faisceau&)>& f);

  Document* m_doc;
  int m_projet = -1, m_faisceau = -1;
  Outil m_outil = Outil::Selection;
  std::string m_selection, m_debutBranche;
  double m_echelle = 1.5;  // pixels par mm
  QPointF m_origine{40, 40};
  QPointF m_souris, m_pression;
  prom::ecad::Pt m_depart;
  bool m_glisse = false, m_appuye = false, m_panoramique = false;
};
