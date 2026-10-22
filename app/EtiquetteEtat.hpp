// Prométhée : étiquette d'état qui signale chaque changement de texte (pour la reprendre ailleurs, par exemple
// dans la ligne du bas de la fenêtre à onglets). Licence GPL-3.0-only.
#pragma once

#include <QLabel>

class EtiquetteEtat : public QLabel {
  Q_OBJECT
public:
  // Un message long passe à la ligne au lieu d'élargir la fenêtre (elle doit tenir sur un petit écran)
  explicit EtiquetteEtat(QWidget* parent = nullptr) : QLabel(parent) { setWordWrap(true); }
  void setText(const QString& t) {
    QLabel::setText(t);
    emit texteChange(t);
  }

signals:
  void texteChange(const QString& t);
};
