// Prométhée : vue de dessus de la carte dans son boîtier. Licence GPL-3.0-only.
#pragma once

#include <optional>

#include <QPainterPath>
#include <QPointF>
#include <QWidget>

#include "Document.hpp"

class VueCarte : public QWidget {
  Q_OBJECT
public:
  explicit VueCarte(Document* doc, QWidget* parent = nullptr);
  void recadrer();
  void setFaceArriere(bool dessous);  // montre et édite la face arrière (vue par transparence depuis le dessus)
  bool faceArriere() const { return m_dessous; }

signals:
  void faceChangee(bool dessous);

public:
  void garderEnVue();
  QSize sizeHint() const override { return {640, 480}; }

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void leaveEvent(QEvent*) override;

private:
  struct Touche {
    enum Type { Rien, Carte, Composant, Trou, Poignee } type = Rien;
    std::string id;
    char bord = 0;
  };
  QPointF ecran(double x, double y) const { return {m_ox + x * m_s, m_oy - y * m_s}; }
  QPointF monde(const QPointF& p) const { return {(p.x() - m_ox) / m_s, (m_oy - p.y()) / m_s}; }
  Touche toucher(const QPointF& ecranPt) const;
  QPointF positionObjet(const Touche& t) const;
  void deplacer(prom::Projet& p, const Touche& t, double x, double y, bool fin) const;
  void redimensionner(prom::Projet& p, char bord, double delta, const prom::Carte& depart) const;
  bool verrouille(const std::string& id) const;
  QPainterPath chemin(const std::vector<prom::Point>& poly) const;
  QPainterPath cheminCarte() const;
  void actualiserParois();
  void menuContextuel(const QPoint& ou);

  Document* m_doc;
  double m_s = 6, m_ox = 0, m_oy = 0;
  bool m_cadre = false;
  // geste en cours
  enum class Geste { Aucun, Objet, Poignee, Deplacement } m_geste = Geste::Aucun;
  Touche m_cible;
  QPointF m_depart, m_departMonde, m_origineObjet;
  prom::Carte m_carteDepart;
  double m_oxDepart = 0, m_oyDepart = 0;
  bool m_bouge = false;
  Touche m_survol;
  Touche m_cibleMenu;
  bool m_dessous = false;
  std::string m_cleParois;
  std::vector<prom::Point> m_paroiInt, m_paroiExt;
};
