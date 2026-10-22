# Cahier des fonctions de CAO mécanique

Liste de référence fournie par Evan le 4 octobre 2026 (dix domaines, à couvrir entièrement), complétée par l'inventaire des grands logiciels (SolidWorks, Inventor, Fusion, CATIA, Creo, FreeCAD) et des outils électroniques et électriques. État : fait (version), en cours, ou prévu. Chaque version coche ce qu'elle livre ; rien n'est retiré de la liste.

## 1. Environnement et préparation

| Fonction | État |
| --- | --- |
| Unités du document (mm, cm, m, pouce, pied ; degré, radian) | Fait (0.9) |
| Saisie avec unité et conversion (« 15 in », « 20 rad », « 30 mm »), opérations (« 10 + 2*3 ») | Fait (0.9) |
| Systèmes complets MMGS, CGS, MKS, IPS (masse, temps, force) | Prévu (avec les propriétés de masse) |
| Plans de base visibles et sélectionnables (dans la vue et l'arbre), origine | Fait (0.9.2) |
| Plans décalés, inclinés, par trois points, tangents | Prévu (0.10) |
| Axes (par cylindre, intersection de plans, deux points), systèmes de coordonnées, points de référence | Prévu (0.10) |

## 2. Esquisse (2D et 3D)

| Fonction | État |
| --- | --- |
| Esquisse sur plan de base ou face plane, choisis d'un clic, en contexte dans la vue 3D | Fait (0.9) |
| Lignes continues et de construction | Fait (0.7, 0.8) |
| Ligne par le point milieu | Fait (0.10) |
| Cercle par le centre | Fait (0.7) |
| Cercle périmétrique (trois points) | Fait (0.10) |
| Arc par le centre, arc trois points | Fait (0.8) |
| Arc tangent | Fait (0.10) |
| Rectangle par les sommets | Fait (0.7) |
| Rectangle par le centre, rectangle incliné (trois points), parallélogramme | Fait (0.10) |
| Polygone régulier | Fait (0.8) |
| Rainure droite | Fait (0.8) |
| Rainure en arc | Prévu (0.10) |
| Splines à poignées de courbure, ellipses, paraboles, coniques | Prévu (0.11) |
| Texte d'esquisse | Prévu (0.11) |
| Points | Fait (0.7) |
| Ajuster (découpage intelligent), prolonger | Prévu (0.10) |
| Décaler (vers l'intérieur ou l'extérieur), symétrie, répétitions linéaire et circulaire | Prévu (0.10) |
| Déplacer, copier, pivoter, mettre à l'échelle | Prévu (0.10) |
| Congé d'esquisse | Fait (0.8) |
| Relations : fixé, horizontal, vertical, perpendiculaire, parallèle, égal, tangent, coïncident, milieu, symétrique, point sur courbe | Fait (0.7, 0.8) |
| Relations : colinéaire, concentrique, intersection, percer | Prévu (0.10) |
| Cotes internes, par rapport à l'origine et aux axes | Fait (0.8) |
| Cotes vers les arêtes, faces et sommets de la pièce (références externes) | Prévu (0.10) |
| Cotes linéaires (alignée, horizontale, verticale), angulaire, rayon, diamètre | Fait (0.8) |
| Longueur d'arc, cotes de coordonnées | Prévu (0.10) |
| Cotes pilotantes ou pilotées (référence) | Fait (0.8) |
| Saisie des cotes avec unité et opérations | Fait (0.9) |
| Esquisse 3D | Prévu (0.12) |

## 3. Fonctions de création (ajout de matière)

| Fonction | État |
| --- | --- |
| Extrusion borgne, à travers tout, jusqu'au prochain, jusqu'au sommet, jusqu'à la face, décalée de la face, plan milieu | Fait (0.9) |
| Jusqu'au corps | Prévu (avec le multicorps) |
| Deux directions avec valeurs et conditions distinctes | Fait (0.9) |
| Dépouille sur l'extrusion | Fait (0.9) |
| Fonction mince (contour fermé ou ouvert, intérieur, extérieur, centré) | Fait (0.9) |
| Sélection des contours à extruder, régions de contours croisés comprises | Fait (0.9.2) |
| Révolution autour d'une ligne ou d'un axe X, Y, Z, plan milieu, deux directions | Fait (0.9) |
| Balayage avec courbes guides (torsion, échelle, alignement) | Prévu (0.11) |
| Lissage avec guides et continuité G0, G1, G2 | Prévu (0.11) |
| Frontière | Prévu (0.12) |

## 4. Fonctions de modification

| Fonction | État |
| --- | --- |
| Enlèvement avec toutes les règles de l'extrusion et de la révolution | Fait (0.9) |
| Enlèvement par balayage et lissage | Prévu (0.11) |
| Assistance au perçage ISO, ANSI, DIN : simple, taraudé, chambré, fraisé ; pas, tolérance, profondeur, longueur filetée | Prévu (0.10) |
| Congé à rayon constant | Fait (0.7) |
| Congés à rayon variable, asymétrique, face à face, complet, retrait aux coins | Prévu (0.11) |
| Chanfrein distance égale | Fait (0.7) |
| Chanfreins distance-distance et distance-angle | Fait (0.10) ; chanfrein de sommet prévu (0.11) |
| Coque, faces ouvertes | Fait (0.7) |
| Coque à épaisseurs multiples | Prévu (0.10) |
| Dépouille par plan neutre | Prévu (0.10) |
| Nervure | Prévu (0.11) |
| Répétitions linéaire (1D, 2D), circulaire, pilotée par courbe, par esquisse, par tableau | Prévu (0.10) |
| Symétrie de fonctions, de faces, de corps | Prévu (0.10) |
| Opérations booléennes multicorps | Prévu (0.11) |
| Filetage 3D modélisé | Prévu (0.11) |

## 5. Tôlerie

| Fonction | État |
| --- | --- |
| Tôle de base pliée (facteur K), bords tombés, grugeages, ourlets, pli de transition, outils de forme, dépliage et DXF | Prévu (lot 2.A) |

## 6. Surfacique

| Fonction | État |
| --- | --- |
| Surfaces extrudée, de révolution, balayée, lissée, remplie, réglée, décalée ; ajuster, prolonger, coudre, épaissir, remplacer la face | Prévu (lot 2.A) |

## 7. Assemblage

| Fonction | État |
| --- | --- |
| Insertion (au curseur, fichier STEP ou IGES avec structure, autre projet), contraintes standard et mécaniques, interférences, jeux, vues éclatées, répétitions, états d'affichage | Fait (0.15 à 0.26) |
| Contraintes sur arêtes, sommets, axes, repère et géométrie de référence des pièces, plans, axes et origine de l'assemblage ; sens imposé, angle orienté | Fait (0.26) |
| Glisser et pivoter dans le respect des contraintes, mise à jour de bas en haut quand une pièce change | Fait (0.26) |
| Contraintes avancées (came, chemin, largeur, symétrie, limites), collisions dynamiques, configurations | Prévu |
| Carte électronique et boîtier dans un même modèle | Fait (0.1 à 0.6) |

## 8. Mise en plan

| Fonction | État |
| --- | --- |
| Vues de base au choix, premier ou troisième dièdre, formats A0 à A4, coupe, demi-coupe, détail, vues interrompues | Fait (0.14 à 0.26) |
| Cotes des esquisses reprises (associatives), cotes d'encombrement, notes et tolérances générales, cartouche ISO 7200, nomenclature et bulles | Fait (0.26) |
| GD&T, états de surface, tables de perçage, coupes brisée et locale, plusieurs feuilles | Prévu |

## 9. Analyse et simulation

| Fonction | État |
| --- | --- |
| Volume | Fait (0.7) |
| Masse, centre de gravité, inerties (au centre et principales), surface selon le matériau | Fait (0.9.2, fiabilisé en 0.10) |
| Analyses de courbure, de dépouille, d'épaisseur | Prévu (0.12) |
| Éléments finis, cinématique et dynamique | Prévu (lot 2.D) |

## 10. Rendu et export

| Fonction | État |
| --- | --- |
| STEP, IGES, BREP, STL, 3MF, OBJ (pièces et boîtier) | Fait (0.3, 0.7) |
| DXF, SVG du boîtier | Fait (0.3) |
| STEP AP214 ou AP242 aux noms Unicode et couleurs par face ; import STEP et IGES avec structure, noms et couleurs | Fait (0.26) |
| Nomenclature à niveaux, repères stables, propriétés personnalisées, XLSX et CSV | Fait (0.26) |
| Parasolid, AMF, DWG, PDF 3D | Prévu |
| Photoréalisme (textures, HDRI, lumières, caméras, lancer de rayons) | Prévu (lot 3) |

## Électronique et électrique

| Fonction | État |
| --- | --- |
| Import KiCad et mise à jour de la carte d'origine | Fait (0.4 à 0.7) |
| Saisie de schéma, routage de PCB, fichiers de fabrication | Fait (0.24 à 0.26) |
| Export KiCad (.kicad_pcb, liste de réseaux), IPC-D-356, .gbrjob, carte simple face et typon, symboles à plusieurs unités | Fait (0.26) |
| Simulation SPICE (ngspice installé sur le poste), Monte-Carlo, pire cas, balayage, assistants de calcul | Fait (0.33) |
| Schémas électriques normalisés, borniers, câblage, armoires, faisceaux | Fait (0.27) |
| Tableau de logement selon la NF C 15-100 (édition 2024) : circuits, différentiels, unifilaire, coffret | Fait (0.27) |
| Calcul d'une installation basse tension (UTE C 15-105, NF C 15-100) : sections, chutes de tension, courts-circuits, protection des personnes, sélectivité, note de calcul | Fait (0.28) |
| Photovoltaïque (UTE C 15-712-1, guide SER-ADEME) : chaînes, protections, câbles, parafoudres, production, unifilaire | Fait (0.28) |
| Grafcet (NF EN CEI 60848) : édition, vérification, simulation, folio, texte structuré CEI 61131-3 | Fait (0.28) |
| Fabrication d'armoire : longueurs de fils, liste de coupe, embouts, filerie, bilan thermique (CEI/TR 60890), chiffrage | Fait (0.28) |
| Courbes de déclenchement et sélectivité tracée, filiation | Prévu |
| Grafcet en langage à contacts (LD), macro-étapes et forçages | Prévu |


## Catalogue de l'esquisse (cahier d'Evan, 6 octobre 2026)

Référence complète des fonctions d'esquisse, avec leur état. Elle complète le domaine 2 ci-dessus.

### 1. Création géométrique

| Fonction | État |
|---|---|
| Ligne, ligne de construction, ligne par le milieu | Fait (0.10–0.11) |
| Droite infinie | Prévu |
| Cercle par le centre, par trois points | Fait (0.10) |
| Cercle tangent à des géométries existantes | Prévu (0.12) |
| Arc par le centre, par trois points, arc tangent | Fait (0.10), aperçu en direct (0.11) |
| Rectangle par deux coins, par le centre, incliné (trois points), parallélogramme | Fait (0.10) |
| Polygone inscrit ou circonscrit, nombre de côtés réglable | Fait (0.8, circonscrit 0.15) |
| Rainure droite par les centres, rainure en arc | Fait (0.8, arc 0.15) |
| Point | Fait (0.7) |
| Texte (libre ou le long d'une courbe), hachures, image de référence | Prévu |
| Esquisse 3D, repères locaux | Prévu |

### 2. Courbes complexes

| Fonction | État |
|---|---|
| Splines par points de passage ou pôles, tension | Prévu (0.13) |
| Ellipse et arc d'ellipse, parabole, hyperbole | Prévu (0.13) |
| Courbe pilotée par équation (paramétrique, explicite, polaire) | Prévu |
| Courbe de raccordement (G2 à G4), ajustement de courbe | Prévu |

### 3. Modification et édition

| Fonction | État |
|---|---|
| Congé d'esquisse avec coin virtuel | Fait (0.8) |
| Chanfrein d'esquisse | Fait (0.15) |
| Ajuster (gomme), prolonger | Prévu (0.12) |
| Décaler (offset), bidirectionnel avec bouchons | Prévu (0.12) |
| Symétrie (a posteriori, copies liées) | Fait (0.13) ; symétrie dynamique prévue |
| Répétitions linéaire et circulaire | Fait (0.13) ; le long d'une courbe prévue |
| Déplacer, copier, pivoter, échelle, étirer | Prévu (0.12) |
| Fractionner, fusionner des points | Prévu |
| Décalquage automatique d'image, reconstruire et lisser une spline | Prévu |

### 4. Contraintes et accrochages

| Fonction | État |
|---|---|
| Horizontale, verticale, fixe, coïncidente, milieu, parallèle, perpendiculaire, tangente, égale, symétrique | Fait (0.7–0.8) |
| Colinéaire, concentrique, co-radiale | Prévu (0.12) |
| Continuité G2, G3 | Prévu (avec les splines) |
| Accroches : extrémités, milieux, sur courbe, axes | Fait (0.8) ; quadrant, intersection, centre de gravité prévus (0.12) |
| Barre contextuelle de relations selon la sélection | Fait (0.11) |
| Calques (couleur, visibilité, calque actif) | Fait (0.13) ; styles de trait prévus |

### 5. Cotation et variables

| Fonction | État |
|---|---|
| Cotation intelligente (longueur, distance, angle, rayon, diamètre) | Fait (0.8) ; longueur d'arc prévue (0.12) |
| Cotes pilotantes et de référence | Fait (0.8) |
| Saisie avec unités et opérations | Fait (0.9) |
| Éditeur des cotes de l'esquisse | Fait (0.11) |
| Cotes ordonnées | Prévu (0.12) |
| Auto-cotation et auto-contrainte | Prévu |
| Variables de pièce et cotes pilotées par une expression | Fait (0.15) |
| Mesures dynamiques (aire, périmètre, centre de gravité de l'esquisse) | Prévu |

### 6. Cinématique et conception descendante

| Fonction | État |
|---|---|
| Blocs d'esquisse, courroies et chaînes, contraintes d'engrenage et de came, esquisse d'implantation | Prévu (après les assemblages) |

### 7. Projections et intersections

| Fonction | État |
|---|---|
| Esquisse sur une face plane, pièce visible derrière | Fait (0.8–0.9) |
| Convertir les entités (contour de la face d'appui, arêtes de la pièce dans le plan) | Fait (0.13) ; silhouette, intersection, enroulement, courbe composite, spline sur surface prévus |

### 8. Analyse et diagnostics

| Fonction | État |
|---|---|
| Couleurs d'état (libre, contraint, conflit, référence) | Fait (0.7–0.8) |
| Inspecteur de viabilité (extrémités libres, longueurs nulles, conflits, degrés de liberté) | Fait (0.11) |
| Flèches des degrés de liberté, peignes de courbure | Prévu |

### Organisation du logiciel

| Fonction | État |
|---|---|
| Écran d'accueil par métier, choix dévoilés au clic, projets récents | Fait (0.12) |
| Navigateur unifié : dossiers par métier, parties dépliables jusqu'à leur contenu | Fait (0.12) |
| Projets d'un seul métier (une pièce seule) ou mixtes | Fait (0.12) |
| Plusieurs cartes par projet, assemblages, mises en plan rangées sous leur pièce | Prévu (0.13, format 4) |
| Onglets de documents | Prévu (0.13) |
| Lien QElectroTech pour les schémas électriques | Fait (0.14 : lien ; 0.27 : import complet) |

### Interface de l'esquisse

| Fonction | État |
|---|---|
| Coin de validation dans la vue (coche verte, croix rouge) | Fait (0.11) |
| Barre contextuelle flottante près du curseur | Fait (0.11) |
| Ligne de commande à la barre d'espace (« c 50 », « r 40 30 »…) | Fait (0.11) |
| Menu radial à huit directions (clic droit maintenu) | Fait (0.11) |
| Historique de l'esquisse (Ctrl+Z, Ctrl+Y) | Fait (0.11) |
| Arbre en dossiers logiques, entités à problème en rouge | En partie (fonctions en erreur en rouge) ; dossiers prévus (0.12) |
| Thème adaptatif, calques de couleurs | Prévu |
| Commandes vocales, retour haptique, branches de versions, frise de construction | Prévu (long terme) |
| Esquisse sémantique assistée par IA | Prévu (long terme) |


## Version 0.14 : fonctions ajoutées

| Domaine | Fonction | État |
|---|---|---|
| Référence | Plans (décalé, incliné, trois points, parallèle par un point), axes, points ; esquisse sur un plan de référence | Fait |
| Fonctions | Perçage ISO et ANSI : simple, taraudé, lamé, fraisé ; traversant ou borgne avec fond de foret | Fait |
| Fonctions | Répétitions linéaire (une ou deux directions), circulaire, pilotée par une esquisse ; symétrie de fonctions ou du corps | Fait |
| Fonctions | Dépouille ; coque à épaisseurs multiples ; balayage ; lissage (ajout ou enlèvement) | Fait |
| Esquisse | Ajuster, prolonger, décaler, déplacer, pivoter, échelle ; relations concentrique et colinéaire | Fait |
| Évaluer | Vue en coupe ; mesure ; mise en plan (quatre vues, arêtes cachées, A4 ou A3, cartouche) ; exports PDF, SVG, DXF | Fait |
| Assemblage | Insertion et placement des pièces du projet, interférences, export STEP | Fait ; contraintes d'assemblage (coïncidence, alignement) prévues |
| Électrique | Schémas QElectroTech reliés : folios, appareils, ouverture dans QElectroTech | Fait ; nomenclature commune aux métiers prévue |

## Version 0.15 : fonctions ajoutées

| Domaine | Fonction | État |
|---|---|---|
| Assemblage | Contraintes coïncidente, alignée, distance, concentrique, parallèle, perpendiculaire ; composants fixes ; résolution automatique | Fait |
| Projet | Nomenclature commune (mécanique, électronique, électrique), export CSV | Fait |
| Esquisse | Polygone circonscrit, chanfrein d'esquisse, rainure en arc | Fait |
| Pièce | Variables (nom = expression) ; cotes qui suivent une variable | Fait ; profondeurs de fonctions pilotées par variable prévues |
| Interface | Dupliquer une fonction (clic droit) | Fait |

## Version 0.16 : ergonomie

| Domaine | Fonction | État |
|---|---|---|
| Esquisse | Panneaux de réglage avec aperçu : symétrie, réseaux linéaire et circulaire, décaler, déplacer, pivoter, échelle | Fait |
| Esquisse | Panneau « Gérer les relations » : relations possibles et existantes de la sélection | Fait |
| Esquisse | Congé et chanfrein à un coin où partent des lignes de construction ou plus de deux lignes | Fait |
| Fonctions | Plans de base en liste (symétrie, dépouille), fonctions à répéter choisies dans l'arbre | Fait |

## Version 0.26 : fonctions ajoutées

| Domaine | Fonction | État |
|---|---|---|
| Esquisse | Saisie dynamique (longueur, angle, largeur, hauteur, diamètre) pendant le tracé | Fait |
| Esquisse | Inférence pendant le tracé (H, V, parallèle, perpendiculaire, tangente, alignement, longueur égale) | Fait |
| Esquisse | Cote d'angle par quadrant, angle par trois points | Fait |
| Esquisse | Insertion d'un DXF ; convertir les entités sur une sélection ; références externes associatives | Fait |
| Esquisse | Chanfrein et rainure en arc réglés dans un champ posé sur la vue (unités du document) | Fait |
| Pièce | Nervure, filetage cosmétique, tôlerie (bords tombés, déplié, facteur K) | Fait |
| Pièce | Axes et origine cliquables, axe de révolution au clic | Fait |
| Pièce | Mesure complète (Ø, rayon, entraxe, écart de plans, angle vrai, centre à centre, totaux) | Fait |
| Pièce | Propriétés, masse, variables et vérification de fabrication dans un panneau latéral | Fait |
| Assemblage | Sens des contraintes, angle orienté et son inversion | Fait |
| Assemblage | Références : arêtes, sommets, axes, repère et géométrie de référence des pièces, repère de l'assemblage | Fait |
| Assemblage | Mise à jour de bas en haut (sous-assemblages d'abord), Reconstruire (Ctrl+B) | Fait |
| Assemblage | Glisser et pivoter à la souris dans le respect des contraintes ; placement, déplacement et pivotement par valeurs dans le panneau | Fait |
| Assemblage | Distance et angle posés à leur valeur actuelle ; engrenage, vis, répétitions, remplacement dans le panneau latéral | Fait |
| Mise en plan | Formats, dièdres, vue de face au choix, demi-coupe, cotes des esquisses, cartouche ISO 7200 | Fait |
| Échanges | STEP AP242, noms Unicode, couleurs par face ; import STEP et IGES avec structure | Fait |
| Électronique | Tableau des champs, empreintes par tableau de pastilles, import .kicad_mod et .kicad_sym, parité schéma-circuit | Fait |
| Électronique | Carte simple face, typon, textes sur la carte, export KiCad, IPC-D-356, .gbrjob, symboles à plusieurs unités | Fait |

## Version 0.27 : CAO électrique

| Domaine | Fonction | État |
|---|---|---|
| Accueil | Tout ce qui se crée visible d'emblée, rangé par métier ; « Nouveau projet vide », Ouvrir, Importer (KiCad, CAO, QElectroTech) ; Fichier › Nouveau projet (Ctrl+N) et Fichier › Créer | Fait |
| Projet électrique | Folios (page de garde, puissance, commande, automate, distribution, unifilaire, borniers, implantation, nomenclature, libre), A4 à A2, cadre CEI 61082 à colonnes et lignes repérées, cartouche pleine largeur, positions « folio.colonne » | Fait |
| Folios dessinés d'office | Page de garde (titre, renseignements, sommaire), plan des borniers, implantation de l'armoire à l'échelle (repérage des circuits), nomenclature, tenus à jour à chaque modification | Fait |
| Symboles | Bibliothèque CEI 60617 de plus de 75 symboles par famille, recherche (symbole, circuit type ou appareil posé), aperçu, catalogue de références (encombrement, caractéristiques, nomenclature) | Fait |
| Repérage | Appareils repérés dès la pose (-Q1, -KM1, -S1) ou selon la CEI 81346 (QA, SF, PF…), renumérotation dans l'ordre de lecture, tiret au choix | Fait |
| Contacts | Contacts et pôles rattachés à leur bobine par un menu, numéros CEI 60947 (13-14, 21-22, 1-2…, 95-96, 97-98), renvois croisés bobine ↔ contacts, « Ajouter » depuis l'appareil (folio de puissance ou de commande choisi seul) | Fait |
| Bornes et potentiels | Bornes numérotées à la suite dans leur bornier, potentiels L1 L2 L3 N PE 24V 0V et renvois de folio, numéros de fils figés (à la suite ou par folio), section, couleur et type par classe de conducteur | Fait |
| Circuits types | Démarrage direct, inverseur de sens de marche, alimentation 24 V, éclairage par télérupteur, va-et-vient, prises : posés câblés et repérés en un clic | Fait |
| Vérification | Bornes en l'air, contact sans bobine, PE pas en vert-jaune, section trop faible pour le calibre, moteur sans protection (cherchée en amont des contacteurs) ou sans PE, câbles mal affectés, renvois orphelins… ; un clic mène à l'endroit | Fait |
| Rapports | Appareils, nomenclature, borniers, câbles (affectation des conducteurs selon le schéma), carnet de câblage de borne à borne, entrées et sorties d'automate, folios, bilan de puissance ; CSV ; dossier complet (PDF, CSV, DXF) | Fait |
| Armoire | Plaque de fond, rails, goulottes, implantation par famille, glisser accroché au rail sans sortir de la plaque, porte, bilan de remplissage et réserve, plan de perçage DXF | Fait |
| Faisceau | Nœuds, branches cotées, connecteurs du schéma, longueur de chaque fil surlongueurs comprises | Fait |
| Calculs | Section selon la CEI 60364-5-52 (pose, isolant, température, groupement), chute de tension, calibre ; bilan de puissance | Fait |
| Logement | Assistant NF C 15-100 (2024) : pièces et équipements → prises et points lumineux minimaux, circuits spécialisés, différentiels 30 mA type A, coffret avec 20 % de réserve, avis de conformité ; unifilaire, coffret et matériel créés en un clic | Fait |
| Import | Projet QElectroTech (.qet) : folios, appareils, liens maître-esclave, bornes, conducteurs avec numéros, sections et couleurs | Fait |
| Interface | Commandes de vue (zoom, folio entier, dessin), messages qui passent à la ligne au lieu d'élargir la fenêtre, fenêtre utilisable dès 1024 × 768 | Fait |
| Symboles personnels | Appareils personnels dessinés d'après leurs bornes (fait en 0.28, voir plus bas) ; ceux de QElectroTech arrivent avec l'import | Fait |

## Version 0.28 : l'électricité de bout en bout

| Domaine | Fonction | État |
|---|---|---|
| Accueil | Un métier à la fois, gardé d'une fois à l'autre ; recherche sans accents ; aperçu « Pour commencer » | Fait |
| Installation calculée | Source (réseau public, transformateur, groupe), tableaux et départs, Ib, In ou Ir, Iz corrigé, sections Ph, N (harmoniques) et PE, chutes de tension cumulées, Ik3 max, Ik1 min et Ief (UTE C 15-105), pouvoir de coupure, protection des personnes TT, TN, IT, contrainte thermique, sélectivité ampèremétrique, compensation ; avis qui citent leur article ; modèles (tertiaire, atelier, parking IRVE, maison) ; unifilaire ; note de calcul PDF et CSV | Fait |
| Calculs | Boîte de 18 fiches : section, chute de tension, courts-circuits, longueurs protégées, contrainte thermique, conducteur PE, prise de terre, éclairage, moteurs, compensation, échauffement d'armoire, conversions ; formule, explication et norme | Fait |
| Photovoltaïque | Calepinage, chaînes au froid (Uoc) et au chaud (Vmp), fusibles de chaîne, câbles EN 50618, sectionneur, parafoudres (longueur critique), AGCP, découplage, production selon l'orientation, unifilaire, note de calcul ; 3 à 250 kWc | Fait |
| Bibliothèque | 139 symboles en 14 familles (HTA et postes, production et stockage, mesure et instrumentation, sécurité et courants faibles, bâtiment, unifilaire de tableau…), fiche « C'est quoi ? » de chaque symbole | Fait |
| Catalogue | 175 références génériques avec encombrement, caractéristiques et prix public indicatif | Fait |
| Apprendre | Glossaire de 60 termes relié aux résultats ; 5 parcours guidés (moteur, grafcet, installation, photovoltaïque, logement), 35 étapes validées sur le projet | Fait |
| Fabrication | Longueur de chaque fil par les goulottes et la lyre de porte ; liste de coupe par section et couleur, embouts au code couleur français, embouts doubles ; filerie à commander | Fait |
| Chiffrage | Prix saisi dans la nomenclature (sinon indicatif), total ; chiffrage du tableau (matériel, filerie, main d'œuvre en temps unitaires, taux horaire) ; CSV | Fait |
| Armoire | Bilan thermique CEI/TR 60890 : pertes de chaque appareil à la charge choisie, pose de l'enveloppe, ventilation ou climatisation nécessaire | Fait |
| Dossier | Révisions (indice, date, auteur, description) au cartouche et sur la page de garde ; chaque folio en DXF (calques cadre, fils, numéros, symboles, repères, renvois…) ; dossier complet avec les DXF, la liste de coupe et le chiffrage | Fait |
| Interface | Plus de fenêtre modale dans l'atelier : saisie sur le folio (textes, potentiels), suppression annulable, logement type en ligne | Fait |
| Grafcet | Étapes, transitions, actions continues, conditionnelles, mémorisées ; divergences et convergences OU et ET ; réceptivités lues au fil de la frappe (ET, OU, NON, fronts, temporisations) ; vérification ; simulation selon les cinq règles d'évolution ; folio dessiné d'office (PDF, DXF) ; texte structuré CEI 61131-3 | Fait |
| Appareils personnels | « Mes appareils › Nouvel appareil… » : nom, lettre de repère (CEI 81346-2), marquage, bornes côté par côté ; symbole dessiné d'office (cadre CEI 60617, bornes sur la grille), aperçu et défauts au fil de la frappe ; fabricant, référence, prix, montage, dimensions et pertes repris par la nomenclature, l'implantation, le chiffrage et le bilan thermique ; porte des contacts posés ailleurs ; rangé dans le projet et dans la bibliothèque de l'utilisateur ; modifié, les appareils posés suivent | Fait |

## Version 0.33 : électronique, lots L4 à L19 du plan de refonte

Le détail par tâche, avec ce qui reste, est dans [REFONTE_ELECTRONIQUE.md](REFONTE_ELECTRONIQUE.md).

| Domaine | Fonction | État |
|---|---|---|
| Bibliothèque | Articles du projet versionnés, héritage, mise à jour des composants ; recherche riche ; pose par « 10k 0603 » ; IPC-7351B nominal ; archives SnapEDA ; bibliothèques par variable ; standardisation | Fait |
| Bibliothèque | Tableur de bibliothèque, générateurs IPC étendus, échange en masse, approvisionnement hors ligne | Prévu |
| Circuit | Routeur qui contourne et pousse ; liaison routée d'office ou expliquée ; exclusions qui reviennent « à revoir » ; « À placer » ; couronne ; rapport HTML | Fait |
| Circuit | Gestionnaire de contraintes complet, paires et accord de longueur, édition des pistes posées, placement assisté | Prévu |
| Mécatronique | Modèles STEP exacts, carte équipée en STEP, contrôle dans le boîtier, façades, contour piloté par une esquisse, coques imprimables chiffrées | Fait |
| Schéma | Bus, feuilles de canal répétées, polarité des électrolytiques, déplacement vers une feuille, congés et chanfreins au circuit | Fait |
| Schéma | Hiérarchie complète, éditeurs graphiques de symboles et d'empreintes, textes riches | Prévu |
| Fabrication | Panneau en languettes, routage complet, impédance IPC-2141, connectivité relue avant l'envoi, devis hors ligne, visionneuse HTML | Fait |
| Performances | DRC de 2 000 composants en fond sous 10 s, 60 images par seconde au glisser, remplissage local, chien de garde, version portable | Fait |
| Échanges | Netlist KiCad, DXF, Specctra (Freerouting), Gerber relus en carte, BoardView, IPC-2581 | Fait |
| Échanges | Schémas KiCad, Altium, EAGLE, EasyEDA, PADS | Prévu |
| Simulation | ngspice depuis le schéma, valeur réglée pendant le calcul, Monte-Carlo, pire cas, balayage, assistants (LED, diviseur, 555, RC, Sallen-Key) | Fait |
| Simulation | Simulation continue, instruments, co-simulation de microcontrôleur | Prévu |
| Travail à plusieurs | Variantes de montage, coût par palier, comparaison, fusion à trois voies, historique local, verrou, archive, notes, schéma de système | Fait |
| Travail à plusieurs | Git intégré, rapports PDF de la vie du projet, bibliothèque d'équipe | Prévu |
| Automatisation | Quinze commandes de `promethee`, jeux de sorties contrôlés, placement automatique, revue par règles, interface JSON-RPC locale | Fait |
| Automatisation | Console Python, serveur MCP, assistant local | Prévu |
| Logique | Simulation à quatre états, Verilog structurel, symbole de FPGA depuis son brochage | Fait |
| Atelier | Gravure à l'anglaise, perçage, découpe et pochoir en G-code | Fait |
| Atelier | Vue platine d'essai, salle de TP | Prévu |
| Essais et fiabilité | Plan de mise sous tension, points de test et couverture, procès-verbal, contraintes d'emploi, durée de vie, MTBF, conformité | Fait |
| Cartes rapides | Plan de référence sous les pistes, diaphonie, chute de tension, inductance de montage (formules fermées) | Fait |
| Cartes rapides | Solveurs de champ (openEMS, Elmer, FastHenry), schéma de lignes | Prévu |
| Entreprise | Équipe et rôles, cycle de vie, révisions, numéros internes, ordres de modification, exigences vérifiées | Fait |
| Entreprise | Dépôts signés, PLM, ERP, faisceaux | Prévu |
