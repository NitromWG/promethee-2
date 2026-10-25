# Feuille de route

Le logiciel se construit par lots. Un lot regroupe les étapes qui dépendent les unes des autres : on les mène de front, parce que les faire l'une après l'autre obligerait à défaire puis refaire. Chaque lot se termine par un rendez-vous où Evan essaie le résultat en vrai, sur sa machine ou en atelier, avant qu'on attaque le suivant. Les phases et les jalons sont ceux du cahier des charges (§22).

## Phase 0 : cadrage, jusqu'au jalon 0

| Lot | Étapes menées de front | Pourquoi ensemble | Rendez-vous | État |
| --- | --- | --- | --- | --- |
| 0.A | Cahier des charges ; prototype web du lien carte-boîtier | Le prototype valide la promesse centrale du CDC | Essai du prototype sur téléphone | Fait |
| 0.B | Socle natif C++20 et OpenCascade ; portage vérifié du modèle, des vérifications et des corrections ; pièces exactes, STEP et STL ; outil en ligne de commande ; CI sur Linux, Windows et macOS | Un socle qui ne se compile que sur une plateforme ferait tourner en rond dès l'interface | Pousser le dépôt sur GitHub, la CI passe au vert sur les trois systèmes | Fait le 2 octobre 2026 |
| 0.C | Application de bureau Qt 6 : vue 3D OpenCascade, vue carte 2D, panneau, annuler et rétablir, ouvrir et enregistrer ; installeurs Windows, macOS et Linux produits par la CI | L'interface et ses dépendances (Qt, OpenGL, visualisation OpenCascade) doivent s'installer sur Windows dès la première version | Installer sur son PC et refaire le geste du jalon 0 : déplacer un trou, tirer une paroi | Premier essai réussi le 2 octobre 2026 ; version 0.3 avec les retours en cours d'essai |
| 0.D | Décisions d'architecture sur les points ouverts du CDC (§26), publiées en ADR | Elles conditionnent les lots de la phase 1 | Relecture et arbitrage des ADR | En cours (ADR 0001 et 0002) |

**Jalon 0 natif** : carte et boîtier se modifient l'un l'autre en direct dans le vrai logiciel, installé sous Windows.

## Phase 1 : MVP, jusqu'au jalon 1

| Lot | Étapes menées de front | Pourquoi ensemble | Rendez-vous |
| --- | --- | --- | --- |
| 1.A | Import KiCad (.kicad_pcb) ; cartes de forme quelconque et boîtier qui épouse le contour ; export vers KiCad ; format de projet versionné avec migrations — fait en 0.6 | L'aller-retour avec KiCad dicte ce que le format doit stocker : empreintes, modèles 3D, hauteurs, connecteurs de bord | Une vraie carte KiCad importée, son boîtier généré puis imprimé |
| 1.B | Esquisses paramétriques avec solveur, extrusion, enlèvement, révolution, congé, chanfrein, coque, arbre de construction et nommage topologique (faits en 0.7) ; esquisse sur face, perçages, répétitions, paramètres et formules ; boîtier généré rendu modifiable ; contraintes entre éléments de la carte | Le nommage topologique se conçoit avec l'arbre dès le départ, sinon tout est à reprendre (FreeCAD y a laissé des années) | Modéliser une pièce réelle et ajouter ouvertures et nervures au boîtier |
| 1.C | Mise en plan (vues, coupes, cotation, cartouche) ; exports de fabrication (PDF, DXF, SVG, STEP, STL, 3MF, dossier complet) | Ils partagent la projection à lignes cachées et le modèle de calques | Un plan coté conforme aux habitudes d'un bureau d'études |
| 1.D | Nomenclature unique ; structure du répertoire fournisseurs ; scripts Python et mode sans écran | Mêmes accès au modèle, même interface de programmation | Un script qui génère une variante de produit |
| 1.E | Sauvegarde automatique, récupération après plantage, performances ; documentation ; projet témoin — en 0.25 : enregistrement atomique, conflits et double ouverture signalés, copies de récupération, journal et rapport de plantage, solveur d'assemblage en profil (200 pièces en 0,3 s), mesures de performance | La robustesse se mesure sur le projet témoin | Le projet témoin du jalon 1 de bout en bout |

**Jalon 1** : une carte et son boîtier imprimé conçus, mis en plan et exportés.

## Phase 2 : v1.0 et parité, jusqu'au jalon 2

| Lot | Étapes menées de front | Pourquoi ensemble |
| --- | --- | --- |
| 2.A | Assemblages, contraintes, détection d'interférences | Les contraintes et les interférences portent sur les mêmes liaisons |
| 2.B | Schéma électronique ; routage du circuit imprimé ; co-conception avancée (pont KiCad) — socle livré en 0.24 (modèle unifié, schéma, bibliothèque et composants créés, ERC, circuit 2 ou 4 couches, routage interactif et automatique simple, zones, DRC, 3D, Gerber X2 et Excellon, validation et publication verrouillée) ; restent les feuilles hiérarchiques, les paires différentielles, l'accord de longueurs, un vrai routeur automatique et l'import des schémas KiCad. Refonte selon l'audit du 15 octobre 2026, vingt lots de L0 à L19 (0.29 à 0.48) : L0, la sûreté de ce qui sort (cuivre, porte de publication, empreintes, dossier, import KiCad), livré en 0.29 ; L1, les fondations de l'interaction (historique par conception, sélection partagée, outils et Échap, registre des actions, inspecteur stable, panneaux ancrables, migrations réversibles, parcours d'interface par de vrais gestes), livré en 0.30 ; L2, le schéma interactif (glisser-déposer, survol, menus contextuels, édition sur place, fil à l'équerre qui évite les symboles, fils élastiques, ERC à trois niveaux et panneau Vérifications, recherche, sélections avancées, accessibilité du canevas, jetons de thème, réglages déclarés), livré en 0.31 ; L3, la vérité du composant (valeurs comprises et formules, hauteur à une seule source, synchronisation continue du schéma et du circuit avec panneau des écarts, empreinte remplacée en place, remplacement d'un composant, appariement d'une carte sans liens, inspecteur à plusieurs composants, tableau des champs ancré, vues côte à côte et tableau des objets), livré en 0.32 ; plan de refonte de l'électronique : lots L0 à L3 en 0.29 à 0.32, socles des lots L4 à L19 en 0.33, toutes leurs tâches en 0.34 (ce qui reste en partie dans docs/REFONTE_ELECTRONIQUE.md) | Schéma, circuit et mécanique doivent partager les mêmes composants |
| 2.C | CAO électrique : schémas, borniers, câblage, armoires, faisceaux — livrée en 0.27 (folios CEI 61082, symboles CEI 60617 et catalogue, contacts rattachés et renvois croisés, bornes, numéros de fils, circuits types, vérification, rapports et dossier, armoire, faisceau, calculs de section, tableau de logement NF C 15-100, import QElectroTech), étendue en 0.28 (installation basse tension calculée selon l'UTE C 15-105, photovoltaïque selon l'UTE C 15-712-1, boîte à calculs, 139 symboles expliqués, parcours d'apprentissage, fabrication de l'armoire : longueurs, liste de coupe, bilan thermique, chiffrage ; révisions, DXF des folios ; Grafcet CEI 60848 vérifié, simulé et traduit en texte structuré ; appareils personnels dessinés d'après leurs bornes) ; restent l'éditeur de symboles dessinés à main levée, les courbes de déclenchement, le Grafcet en langage à contacts, les liens vers les connecteurs des cartes du lot 2.B et l'export vers QElectroTech | Liée au lot 2.B par les composants et les connecteurs |
| 2.D | Simulation mécanique et thermique (CalculiX), électronique (ngspice) — électronique en 0.33 : ngspice depuis le schéma, Monte-Carlo, pire cas, balayage, assistants | Maillage, matériaux et conditions aux limites communs |
| 2.E | Fabrication (usinage, impression, découpe) ; interopérabilité étendue (IGES, DWG, 3MF, imports Altium et EAGLE) | Les sorties de fabrication sont aussi des formats d'échange |
| 2.F | Versions et collaboration ; accessibilité ; traductions ; gros projets ; projet témoin complet — en 0.25 : thèmes de couleur dont un contraste élevé, contrastes vérifiés par les essais — en 0.33 : variantes, comparaison et fusion à trois voies, historique local, verrou partagé, archive, notes, ordres de modification | La parité se mesure sur le projet témoin complet (§24) |

**Jalon 2** : projet témoin complet, parité d'au moins 90 %.

## Phase 3 : v2, dépassement, jusqu'au jalon 3

| Lot | Étapes menées de front |
| --- | --- |
| 3.A | Assistant IA local branché sur le modèle unique ; répertoire fournisseurs rempli (procédés, coûts, délais) | — en 0.33, une interface JSON-RPC locale ouvre la voie ; en 0.34, console Python, propositions MCP et assistant local au choix, sans connexion sortante
| 3.B | Conception générative, multiphysique couplée, fonctions sans équivalent |

**Jalon 3** : fonctions sans équivalent validées par des utilisateurs.

## Phase 4 : vision

En continu, selon les retours de la communauté.
