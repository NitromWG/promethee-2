// Prométhée : icônes vectorielles de l'interface, dessinées à la taille demandée (nettes sur tout écran).
// Licence GPL-3.0-only.
#pragma once

#include <QIcon>
#include <QString>

QIcon icone(const QString& nom);
// Logo du logiciel : monogramme EI sur fond bleu nuit (icône), et logo complet (écran de démarrage, À propos)
QIcon iconeMarque();
QPixmap logoComplet(int largeur);
