// Prométhée : champ de saisie d'une longueur ou d'un angle avec unités et opérations (« 15 in », « 10+5 »).
// Licence GPL-3.0-only.
#pragma once

#include <QLineEdit>

#include "promethee/unites.hpp"

class ChampValeur : public QLineEdit {
  Q_OBJECT
public:
  explicit ChampValeur(bool angle, const prom::Unites& unites, QWidget* parent = nullptr) : QLineEdit(parent), m_angle(angle), m_unites(unites) {
    setToolTip(angle ? QStringLiteral("Angle : tape par exemple 30, 30°, 0,5 rad ou 90/4")
                     : QStringLiteral("Longueur : tape par exemple 50, 2,5 cm, 1 in ou 10 + 2*3"));
    connect(this, &QLineEdit::editingFinished, this, &ChampValeur::lire);
  }
  void setValeur(double interne) {
    m_valeur = interne;
    m_ecrit = QString::fromStdString(prom::ecrireValeur(interne, m_angle, m_unites));
    setText(m_ecrit);
    setStyleSheet(QString());
  }
  double valeur() const { return m_valeur; }
  void setBornes(double min, double max) { m_min = min; m_max = max; }
  void setUnites(const prom::Unites& u) { m_unites = u; setValeur(m_valeur); }  // même valeur, réécrite dans ces unités

signals:
  void valeurChangee(double interne);

private:
  void lire() {
    // Rien de tapé depuis le dernier affichage : relu, le texte (six décimales) redonnerait une valeur arrondie, à
    // peine différente de la vraie, et la reposerait. Sous Windows, le panneau qui se reconstruit ôte le focus au
    // champ X ouvert par « Placer par valeurs » : un « Déplacer de » était aussitôt annulé.
    if (text() == m_ecrit) {
      setStyleSheet(QString());
      return;
    }
    double v = 0;
    std::string err;
    if (!prom::lireValeur(text().toStdString(), m_angle, m_unites, v, &err)) {
      setStyleSheet(QStringLiteral("QLineEdit { border: 2px solid #C2402E; }"));
      setToolTip(QString::fromStdString(err));
      return;
    }
    if (v < m_min || v > m_max) {
      setStyleSheet(QStringLiteral("QLineEdit { border: 2px solid #C2402E; }"));
      setToolTip(QStringLiteral("Valeur hors limites (%1 à %2).")
                     .arg(QString::fromStdString(prom::ecrireValeur(m_min, m_angle, m_unites)), QString::fromStdString(prom::ecrireValeur(m_max, m_angle, m_unites))));
      return;
    }
    const bool change = std::abs(v - m_valeur) > 1e-12;
    setValeur(v);
    if (change) emit valeurChangee(v);
  }
  bool m_angle;
  prom::Unites m_unites;
  double m_valeur = 0, m_min = -1e9, m_max = 1e9;
  QString m_ecrit;  // texte écrit par setValeur : tant qu'il est là, rien n'a été tapé
};
