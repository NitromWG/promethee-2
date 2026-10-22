// Prométhée : souris et clavier réels pour les essais de l'interface (lot L1, tâche 1.1, Q1). Chaque geste passe
// par le système de fenêtres de Qt, comme celui d'un utilisateur : le widget sous le pointeur le reçoit, l'appui
// capture la souris, deux clics rapprochés font un double-clic, et les touches passent par les raccourcis de la
// fenêtre (Ctrl+Z, F8…) avant d'aller au widget qui a le focus. Les essais ne déclenchent plus les actions ni
// n'émettent les signaux à la main. Positions en coordonnées du widget visé.
// Licence GPL-3.0-only.
#pragma once

#include <QPoint>
#include <QString>
#include <QStringList>

class QWidget;

namespace reel {

void appuyer(QWidget* w, QPoint p, Qt::MouseButton bouton = Qt::LeftButton, Qt::KeyboardModifiers mods = Qt::NoModifier);
void relacher(QWidget* w, QPoint p, Qt::MouseButton bouton = Qt::LeftButton, Qt::KeyboardModifiers mods = Qt::NoModifier);
// Déplacement, boutons tenus ou non (ceux d'un appui en cours restent tenus)
void deplacer(QWidget* w, QPoint p, Qt::KeyboardModifiers mods = Qt::NoModifier);
void cliquer(QWidget* w, QPoint p, Qt::MouseButton bouton = Qt::LeftButton, Qt::KeyboardModifiers mods = Qt::NoModifier);
void doubleCliquer(QWidget* w, QPoint p, Qt::KeyboardModifiers mods = Qt::NoModifier);
// Appui en a, déplacement par étapes jusqu'à b, relâcher en b
void glisser(QWidget* w, QPoint a, QPoint b, Qt::KeyboardModifiers mods = Qt::NoModifier, int etapes = 8);
// Touche appuyée puis relâchée, envoyée à la fenêtre de w : les raccourcis passent d'abord, puis le widget qui a
// le focus la reçoit
void touche(QWidget* w, int cle, Qt::KeyboardModifiers mods = Qt::NoModifier, const QString& texte = {});
// Texte tapé caractère par caractère, dans le widget qui a le focus
void taper(QWidget* w, const QString& texte);
// Fenêtre de w au premier plan et active (sans gestionnaire de fenêtres sous Xvfb aussi) ; vrai si elle l'est
bool activer(QWidget* w);
// Position du pointeur, telle que le système la connaît (ERG-18 : aucune vue ne la déplace)
QPoint pointeur();
// Le pointeur est relevé avant et après chaque geste : les gestes après lesquels il n'était plus où il était
// (« appui gauche sur VueCircuit ») ; vide si aucune vue ne l'a déplacé. Et le nombre de gestes relevés.
QStringList pointeurDeplace();
int gestesReleves();
int touchesEnvoyees();  // touches envoyées depuis le lancement (un parcours « sans une touche » n'en ajoute aucune)

}  // namespace reel
