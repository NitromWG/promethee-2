# ADR 0003 : le noyau C++ devient la référence

Statut : acceptée, 2 octobre 2026.

## Contexte

Jusqu'à la version 0.2, le prototype web du jalon 0 servait d'implémentation de référence : chaque fonction portée en C++ était comparée cas par cas avec lui. Les retours d'essai de la version 0.2 demandent des fonctions que le prototype n'a pas : trous paramétrables (vis, insert laiton, ancrage imposé), contraintes de position des composants, export multi-format.

## Décision

À partir de la version 0.3, le noyau C++ est la référence et le prototype web est figé comme trace du jalon 0. Les nouveautés ne sont écrites qu'en C++, avec leurs propres tests. Les champs ajoutés au format `.prom.json` sont facultatifs et n'apparaissent que lorsqu'ils s'écartent des valeurs par défaut : un projet sans eux se lit et se calcule exactement comme avant, et les comparaisons avec le prototype restent dans la suite de tests pour le garantir.

## Conséquences

Un projet enregistré par la version 0.3 avec ces nouveaux réglages s'ouvre encore dans le prototype, qui ignore simplement ce qu'il ne connaît pas (les positions absolues restent écrites). L'inverse est vrai sans réserve.
