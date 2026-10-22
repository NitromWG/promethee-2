// Prométhée : interface d'accessibilité du canevas du schéma (lot L2, tâche 2.30, ERG-34). Le lecteur d'écran lit
// la feuille montrée, puis l'objet tenu de la sélection par la description écrite une fois (« R3, 4,7 kΩ, réseaux
// SDA et +3V3 », DescriptionObjet) ; chaque changement de sélection l'annonce. « Objet relié suivant » passe d'un
// objet à l'objet relié suivant, au clavier.
// Licence GPL-3.0-only.
#pragma once

class QObject;
class QWidget;

namespace accessibilite {
void installer();  // une fois : la fabrique qui donne son interface à chaque canevas de schéma
void annoncer(QWidget* canevas);  // la sélection a changé : le lecteur d'écran relit la description
}  // namespace accessibilite
