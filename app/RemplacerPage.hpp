// Prométhée : remplace la page d'une zone défilante sans détruire l'ancienne sur-le-champ. Un réglage qui
// reconstruit son panneau depuis le signal d'un de ses propres boutons (case cochée, liste déroulante) détruirait
// sinon ce bouton pendant qu'il signale encore : accès à de la mémoire libérée, plantage sous Windows.
// Licence GPL-3.0-only.
#pragma once

#include <QScrollArea>

inline void remplacerPage(QScrollArea* zone, QWidget* page) {
  if (QWidget* ancien = zone->takeWidget()) {
    ancien->hide();
    ancien->deleteLater();
  }
  zone->setWidget(page);
}
