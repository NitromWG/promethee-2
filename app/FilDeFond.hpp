// Prométhée : priorité des calculs en fond (lot L9, 9.2). Les fils de calcul (et ceux qu'ils lancent, qui en héritent)
// passent sous le fil de l'interface : sur une machine à deux cœurs, la saisie garde la main pendant la DRC.
// Licence GPL-3.0-only.
#pragma once

void prioriteDeFond();  // à appeler au début du fil
