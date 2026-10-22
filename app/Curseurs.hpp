// Prométhée : curseurs des canevas (charte, lot L1, tâche 1.7). Flèche pour l'outil Sélection, croix à quatre
// flèches sur un objet choisi qu'on peut déplacer et pendant un glisser, main fermée pour déplacer la vue, réticule
// fin pour poser ou tracer, réticule cerclé sur un point d'accroche valide, cercle barré pour une action refusée,
// flèche avec sablier pendant un calcul long. Licence GPL-3.0-only.
#pragma once

#include <QCursor>
#include <QPainter>
#include <QPixmap>

namespace curseurs {
// Réticule cerclé : le point visé est un point d'accroche (broche, bout de fil, pastille)
inline QCursor accroche() {
  static const QCursor c = [] {
    QPixmap p(25, 25);
    p.fill(Qt::transparent);
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    for (const auto& [couleur, epaisseur] : {std::pair{QColor(Qt::white), 3.0}, std::pair{QColor(Qt::black), 1.2}}) {
      g.setPen(QPen(couleur, epaisseur));
      g.drawLine(QPointF(12.5, 1), QPointF(12.5, 9));
      g.drawLine(QPointF(12.5, 16), QPointF(12.5, 24));
      g.drawLine(QPointF(1, 12.5), QPointF(9, 12.5));
      g.drawLine(QPointF(16, 12.5), QPointF(24, 12.5));
      g.setBrush(Qt::NoBrush);
      g.drawEllipse(QPointF(12.5, 12.5), 5.5, 5.5);
    }
    return QCursor(p, 12, 12);
  }();
  return c;
}
}  // namespace curseurs
