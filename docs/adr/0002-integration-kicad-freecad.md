# ADR 0002 : intégration de KiCad et de FreeCAD

Statut : acceptée pour la phase 1, à réévaluer au lancement de la phase 2. 2 octobre 2026.

## Contexte

Le cahier des charges (§20) réutilise KiCad pour l'électronique et FreeCAD pour une partie de la mécanique, et demande d'évaluer en phase 0 une variante plus modeste qui les unifierait sans fusionner leurs codes (§26).

## Décision

En phase 1, Prométhée s'intègre à KiCad et FreeCAD par leurs formats de fichiers, pas par leur code : lecture et écriture des `.kicad_pcb` (lot 1.A), échange de pièces en STEP avec FreeCAD et les logiciels du marché. Les bibliothèques de base (OpenCascade aujourd'hui, le solveur d'esquisse PlaneGCS au lot 1.B) sont réutilisées directement. Le modèle unique, l'interface commune et les liens entre domaines, qui manquent à tous, sont écrits dans Prométhée.

## Raisons

Le lien carte-boîtier en direct, qui fait l'intérêt du projet, ne dépend pas du code de KiCad. Fusionner des bases de plusieurs millions de lignes avant d'avoir des utilisateurs reviendrait à tourner en rond. Passer par les formats garde la compatibilité avec les versions de KiCad que les gens utilisent déjà.

## Points ouverts restants

Les autres points du §26 du cahier des charges seront tranchés par des ADR au fil du lot 0.D, dont le choix entre C++ seul et C++ avec Rust pour les importeurs, à décider avant le lot 1.A.
