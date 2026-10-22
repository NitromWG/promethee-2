// Prométhée : description d'un objet du schéma écrite une fois (lot L2, tâche 2.30, SAI-19, ERG-34), lue par
// l'infobulle et par l'interface d'accessibilité du canevas : « R3, 4,7 kΩ, réseaux SDA et +3V3 ». La valeur d'un
// passif est dite comme elle se lit (« 4k7 » → « 4,7 kΩ », « 100n » → « 100 nF »).
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QString>

namespace description {
// La valeur d'un composant dite comme elle se lit, d'après le préfixe de son repère (R : Ω, C : F, L : H) ; une
// valeur qui n'est pas un nombre (« BC547 », « Rouge ») reste telle quelle
QString valeurLisible(const std::string& valeur, const std::string& prefixe);
// « A », « A et B », « A, B et C »
QString liste(const std::vector<std::string>& noms);
}  // namespace description
