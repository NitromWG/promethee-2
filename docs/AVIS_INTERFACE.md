# Avis d'interface d'Evan — suivi

Avis en 39 points remis le 7 octobre 2026 (après la 0.12), repris ici comme feuille de route de l'interface, avec l'état de chaque point.

| Point | Demande | État |
|---|---|---|
| 1 | Plans de base masqués une fois la pièce commencée, bouton pour les afficher | Fait (0.13) |
| 2 | Ajuster la vue (F), vues standard | Fait (0.13 : F, 1 face, 2 dessus, 3 droite, 0 isométrique) ; recentrage automatique après chaque opération à affiner |
| 3 | Panneau d'une esquisse terminée : état, puis Extrusion et Révolution en avant | Fait (0.13) |
| 4, 14 | Arbre : esquisse 2D, fonction, pièce bien distinctes ; esquisse rangée sous sa fonction | Fait (0.12) |
| 5, 6, 13 | Commandes selon la sélection (esquisse, face, arête, sommet), mini-barre flottante | Fait (0.11–0.14 : barres flottantes dans l'esquisse et dans la vue 3D) |
| 7 | Filtre de sélection automatique, icônes | Fait (0.14 : bande d'icônes dans la vue) |
| 8 | Barre d'état de sélection avec mesure | Fait (0.13 : aire, longueur, coordonnées, distance et angle entre deux éléments) |
| 9, 10 | État de l'esquisse et contours fermés en couleur | Fait (0.13 : vert fermé, orange ouvert, rouge problème, dans la vue et le panneau ; inspecteur dans l'esquisse) |
| 11 | Panneau d'extrusion explicite, aperçu en direct | Fait (0.8–0.9) |
| 12 | Glisser la flèche d'extrusion dans la vue | Fait (0.14) |
| 15 | Contraintes par type et barre de progression | Fait (0.13) |
| 16 | Géométrie de construction distincte | Fait (0.11–0.13 : tirets bleu clair dans l'esquisse, trait d'axe gris dans la vue 3D) |
| 17 | Cotation intelligente, double-clic pour modifier | Fait (0.8) |
| 18 | Géométrie de référence (plans, axes, points) | Fait (0.14) ; repères prévus |
| 19, 20 | Balayage, lissage, dépouille, miroir et répétitions de fonctions | Fait (0.13 dans l'esquisse, 0.14 en 3D) |
| 21 | Assistance au perçage (simple, fraisé, lamé, taraudé) | Fait (0.14, ISO et ANSI) |
| 22 | Raccourcis clavier | En partie (vue : F, 1, 2, 3, 0, E, R, X ; esquisse : L, R, C, A, D, X, H, V, Espace…) ; M (mesurer) et masquer : prévus |
| 23 | Caméra : rotation, déplacement, zoom, cube cliquable | Fait |
| 24 | Afficher ou masquer plans, esquisses, construction, arêtes, trièdre | Fait (0.14, menu Affichage de la vue) |
| 25 | Charte de couleurs | Fait dans l'esquisse (0.13 : libre gris foncé, contraint vert, sélection bleue, aperçu orange, construction bleu clair en tirets, conflit rouge) |
| 26 | Clic droit complet | Arbre : fait ; à enrichir (dupliquer, masquer, propriétés) |
| 27 | Annuler et rétablir paramétriques, solides | Fait (historique du projet et de l'esquisse) ; Ctrl+Y corrigé (0.13) |
| 28 | Panneau de propriétés modifiables | En partie (gestionnaire de fonction) |
| 29 | Unités partout, conversions | Fait (0.9) |
| 30 | Évaluer : masse, volume, surface, centre, inerties, mesure, section ; exports | Fait (0.14 : vue en coupe, mise en plan exportée en PDF, SVG, DXF) |
| 31 | Matériaux | Fait (bibliothèque de base, densité, masse) |
| 32 | Apparence (couleur, transparence) | Fait (0.14) |
| 33 | Mesurer complet | Fait (0.14 : mode Mesurer, touche M) |
| 34 | Messages d'erreur compréhensibles | Fait (en français, sans jargon) ; à poursuivre |
| 35 | Recherche de commandes (Ctrl+K) | Fait (0.13) |
| 36 à 39 | Garder le ruban, l'arbre, le panneau, le cube ; simplifier ; flux guidé de la pièce | En cours |

Demandes du même jour : symétrie et répétitions d'esquisse (fait), convertir les entités (fait : contour de la face d'appui, ou arêtes de la pièce dans le plan), calques d'esquisse (fait : couleur, visibilité, calque actif), bouton Construction qui convertit la sélection (fait), flèches d'annulation redessinées (fait).
