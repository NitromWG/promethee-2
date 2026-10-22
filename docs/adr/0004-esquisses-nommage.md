# ADR 0004 : solveur d'esquisses et nommage topologique

Statut : acceptée, 4 octobre 2026.

## Contexte

Le lot 1.B apporte la conception de pièces : esquisses contraintes, fonctions volumiques et arbre de construction rejouable. Deux choix structurent la suite : le solveur de contraintes 2D, et la manière de désigner les arêtes et faces visées par un congé, un chanfrein ou une coque, de sorte que ces désignations survivent à une modification en amont (le « problème du nommage topologique » qui a longtemps gêné FreeCAD).

## Décision

Solveur : un Levenberg-Marquardt écrit pour Prométhée, sur les coordonnées des points et les rayons des cercles, avec jacobienne numérique. Une première phase rappelle légèrement chaque paramètre vers sa valeur de départ (la géométrie bouge le moins possible et ne s'effondre pas), une seconde ne garde que les contraintes. Le rang de la jacobienne donne les degrés de liberté, les lignes dépendantes les contraintes redondantes ou incompatibles, le noyau les points encore libres. Pas de dépendance nouvelle (ni Eigen ni Boost), un comportement entièrement lisible ; il suffit largement pour des esquisses de quelques centaines de paramètres. On reconsidérera PlaneGCS (le solveur de FreeCAD, LGPL) si des esquisses beaucoup plus grandes ou des courbes libres l'exigent.

Nommage : chaque face reçoit un nom tiré de son origine (fonction, rôle, élément d'esquisse), propagé à travers les opérations grâce à l'historique d'OpenCascade (faces modifiées, engendrées, supprimées). Une arête est désignée par la paire de faces qu'elle sépare, avec un indice de position si plusieurs arêtes séparent les mêmes faces. Les références enregistrées dans l'arbre sont ces noms, jamais des indices internes.

## Conséquences

Une plaque dont on change la longueur garde ses congés sur les bonnes arêtes (vérifié par les tests). Les cas limites, comme une face coupée en plusieurs morceaux par une opération, partagent un nom et se départagent par leur position : rares, ils seront traités au fil des retours. Le même mécanisme servira au boîtier généré quand il deviendra retouchable par les fonctions de pièce.
