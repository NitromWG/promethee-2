# Prométhée

Logiciel libre d'ingénierie intégrée : CAO mécanique, mise en plan, CAO électronique et électrique dans un seul modèle. Le cahier des charges complet décrit le produit visé ; ce dépôt en est l'implémentation, construite étape par étape.

Le principe fondateur, déjà démontré : **la carte électronique et son boîtier forment un seul modèle**. Les piliers suivent les trous de fixation, les découpes de paroi suivent les connecteurs, les perçages du couvercle suivent les LED et les boutons, la hauteur du boîtier suit le composant le plus haut, et les vérifications tournent à chaque modification.

## État

Phase 0 (cadrage), voir [ETAT.md](ETAT.md) et [FEUILLE_DE_ROUTE.md](FEUILLE_DE_ROUTE.md).

| Partie | Contenu |
| --- | --- |
| `noyau/` | Modèle unique carte + boîtier, grandeurs dérivées, vérifications et corrections ; conception électronique (schéma, connectivité, ERC, circuit imprimé, zones, DRC, routage, fabrication, publication) (C++20) |
| `geometrie/` | Pièces exactes en B-rep OpenCascade, export STEP (noms, couleurs) et STL |
| `app/` | Application de bureau Qt 6 : vue Carte, vue Boîtier 3D OpenCascade, panneau, vérifications, nomenclature |
| `outils/` | `promethee`, l'outil en ligne de commande (mode sans écran) |
| `tests/` | Tests unitaires, géométriques, et comparaison cas par cas avec le prototype |
| `prototype-web/` | Prototype du jalon 0, implémentation de référence en JavaScript |
| `docs/adr/` | Décisions d'architecture |

## Essayer l'application sous Windows

Ouvrir l'onglet Actions du dépôt, cliquer sur la dernière compilation au vert, télécharger le paquet `Promethee-Windows`, le décompresser et lancer `Promethee.exe`. Aucune installation n'est nécessaire. Le paquet contient son LISEZMOI (lancement, journal, récupération, limites connues), la source correspondante, les licences, un inventaire SPDX et la somme SHA-256 de chaque fichier.

Apparence : Affichage › Thème, ou le choix « Apparence » de l'accueil. Prométhée (recommandé, par défaut) garde une interface bleu nuit autour de documents sur papier clair ; Clair, Sombre et Contraste élevé suivent ; Selon le système reprend le réglage clair ou sombre de Windows. Le changement est immédiat et gardé d'une session à l'autre ; l'impression et les PDF restent clairs.

Vue Carte : glisser un composant, un trou, ou une poignée du bord de la carte après avoir cliqué dessus ; molette pour zoomer ; double-clic pour pivoter un composant. Vue Boîtier : bouton gauche pour tourner autour, bouton droit pour déplacer, molette pour zoomer, cube de vue pour les vues normalisées (touches 0, 1, 3 et 7), boîtier transparent (T). La vue Carte montre la face avant ou la face arrière (B). Le boîtier se montre visible, transparent ou masqué ; le clic droit dans la 3D masque, rend transparent ou supprime un élément. Fichier propose l'import et l'export KiCad (.kicad_pcb), en mettant à jour la carte d'origine.

L'atelier électronique (Ctrl+3) conçoit une carte de bout en bout, sans autre logiciel : schéma (bibliothèque avec aperçu, fils en équerre, étiquettes, alimentations, annotation, vérification électrique en direct, dessin PDF), composants créés à partir de leur liste de broches, valeurs comprises (« 4k7 » = 4,7 kΩ) ou calculées (« 2*R1 »), circuit imprimé tenu à jour à chaque modification du schéma, les écarts risqués attendant au panneau des écarts (F8 synchronise tout), empreinte changée remplacée en place, composant remplacé par un autre article sans perdre ses fils, schéma et circuit côte à côte, placement à la souris, routage à 45° avec vérification d'isolement en direct (X piste, V via, / angle) ou automatique pour les liaisons simples, zones de cuivre, DRC, vue 3D, puis l'onglet Fabrication : santé de la conception, dossier Gerber X2 et Excellon, placement, nomenclature, publication R001… qui verrouille la révision. Ce qui ne se fabrique pas juste ne se publie pas, dérogation ou non : cuivre coupé par le bord, CMS sur la face sans cuivre d'une simple face, profil de fabricant inconnu, brochage non vérifié après un changement de boîtier. Un exemple complet (clignotant à NE555) s'ouvre depuis l'accueil. Schéma : W fil, L étiquette (Maj+L globale), P alimentation, Q non connecté, J jonction, T texte, A bibliothèque, R et Maj+R tourner, X et Y miroir, Origine tout voir ; circuit : F change de face. F1 donne la liste des raccourcis de l'atelier et de l'outil en cours. La souris suffit : chaque touche a son bouton au ruban (groupe Vue : Tout voir, zoom, inspecteur, raccourcis) ou au bandeau de l'outil (pendant une piste : Via, Dessus, Dessous, Retirer le coude, Retirer le tracé, Coude, Terminer), et un essai fait échouer la construction si une action n'a qu'un raccourci. Pendant un fil ou une piste, une autre commande (une jonction, une via à part, une piste gênante supprimée par le clic droit) suspend le tracé, qui reprend ensuite au dernier coude ; la barre du routeur choisit la paire de couches, la largeur, et le type et l'arrivée de la prochaine via. Le bandeau de sélection donne à la souris les modes Remplacer, Ajouter, Basculer et Retirer (Maj, Ctrl et Ctrl+Maj au clavier) ; un appui long immobile sur des objets superposés en ouvre la liste, comme Alt+clic ; le filtre de sélection de la barre d'état exclut des genres d'objets, et clignote quand il bloque un clic. Fichier › Réglages… (Ctrl+,) règle la souris et le pavé tactile : zoom par cran, sens de la molette, zoom autour du curseur ou du centre, défilement au bord pendant un glisser, deux doigts qui zooment ou font défiler, bouton des gestes. Menu à marques dans le schéma et le circuit : le bouton droit tenu immobile montre huit commandes (← Annuler, → Rétablir, ↑ Zoom sur la sélection, ↓ Tout voir, et en diagonale Tourner, Miroir, Supprimer, Dupliquer) ; avec Ctrl, un tracé rapide dans une direction lance la commande sans attendre. Le ruban est décrit par un fichier qui ne cite que des actions du registre (app/ressources/ruban_electronique.json) ; Réglages › Ruban le modifie en glissant une action dans un groupe, et « Rétablir l'original » le rend. Réglages › Clavier choisit si une touche d'outil l'arme ou agit au curseur. Les suites de deux touches existent (P puis V : poser une via au circuit), la première s'annonçant près du curseur ; Maj+A ouvre au curseur le menu Poser ; une bascule faite au clavier (/ : coude) s'annonce près du curseur. Dans l'atelier, les menus de la fenêtre sont les siens : Ctrl+I importe une carte KiCad vers l'électronique, Ctrl+E écrit le dossier de fabrication de la carte. « Schéma électronique » ouvre un schéma seul, dont le circuit imprimé se crée au premier besoin (bouton Circuit du ruban). Ctrl+3 dans un projet sans carte ouvre l'accueil électronique sans rien créer ; une carte se renomme sur place dans l'arbre, se supprime sans fenêtre (Ctrl+Z la rend), et un projet se rouvre sur le dernier atelier utilisé. Les panneaux de l'atelier (Projet, Bibliothèque, Inspecteur, Messages) flottent, s'ancrent, se groupent en onglets ou se ferment ; le bouton Panneaux de la barre d'état (ou Affichage › Panneaux et dispositions) enregistre leur disposition sous un nom, pour soi, dans le projet ou dans un fichier .json à partager, la rappelle et rend la disposition d'origine. La bibliothèque se trie par catégorie, par pertinence (accents ignorés) ou par ordre alphabétique, montre en colonnes les champs choisis d'un clic droit sur son en-tête, et son aperçu passe du symbole à l'empreinte, à la 3D et à la photo (propriété « photo » du composant). Fichier › Enregistrer pour une version précédente… écrit, pour un collègue qui n'a pas migré, un fichier neuf lisible par la 0.30, la 0.29 ou la 0.28 (« carte.pour-0-30.prom »), après la liste de ce qui s'y perd ; chaque migration du format a son inverse, et les schémas JSON de chaque version sont dans docs (schema_projet.json, schema_conception.json, docs/schemas). En ligne de commande : promethee pour-version <projet> <sortie> [version]. Le schéma se fait à la souris : un article se glisse de la bibliothèque sur la feuille (ou sur le circuit pour un trou de fixation), le survol montre l'objet visé et son infobulle, chaque objet a son menu au clic droit, le repère et la valeur s'éditent sur place, le fil part à l'équerre en évitant les symboles et les fils suivent un symbole déplacé ; la vérification électrique ne crie pas pendant le dessin et F7 la fait complète, son panneau groupant les problèmes par cause. Ctrl+F cherche dans tout le projet, schéma et circuit ; « Choisir les semblables » prend les composants de même valeur sur toutes les feuilles et estompe le reste ; une longueur et un angle se tapent pendant un fil (un chiffre ouvre la saisie). Les réglages se cherchent, s'exportent dans un fichier et se relisent, raccourcis, ruban et thème compris ; les couleurs du canevas se règlent une à une, avec une palette vérifiée pour les daltoniens.

L'atelier électrique (Ctrl+4) mène un projet d'électricien de bout en bout, sans autre logiciel. Schéma sur folios CEI 61082 (139 symboles CEI 60617 avec leur fiche « C'est quoi ? », contacts rattachés à leur bobine, renvois croisés, bornes, numéros de fils, circuits types, vérification qui mène au problème d'un clic, appareils personnels dessinés d'après leurs bornes), armoire implantée à l'échelle avec son bilan thermique, faisceau, Grafcet (NF EN CEI 60848 : édition, vérification, simulation, folio, texte structuré pour l'automate), installation basse tension calculée (UTE C 15-105, note de calcul), photovoltaïque (UTE C 15-712-1), tableau de logement (NF C 15-100), boîte à calculs, rapports (nomenclature chiffrée, borniers, câbles, carnet de câblage, liste de coupe, chiffrage) et dossier complet (folios en PDF et en DXF, CSV, plans de perçage). Le bouton « Apprendre » ouvre des parcours guidés dont chaque étape se valide sur le projet. Un exemple (démarrage d'un moteur) s'ouvre depuis l'accueil, et les projets QElectroTech s'importent.

L'atelier Pièces (Ctrl+2) sert à concevoir une pièce de zéro : esquisses cotées et contraintes, extrusion, enlèvement, révolution, congé, chanfrein et coque, dans un arbre de construction modifiable à tout moment. Dans l'esquisseur : L ligne, R rectangle, C cercle, A arc, Maj+A arc par trois points, O rainure, Maj+P polygone, K ligne de construction, J congé, D cote (on clique les éléments, on place la cote, on tape la valeur), H et V. L'inventaire des fonctions prévues et faites est dans docs/FONCTIONNALITES.md. Le panneau règle la vis et l'insert de chaque trou, l'ancrage et le verrou de chaque composant. Le dossier de fabrication s'exporte en STEP, IGES, BREP, STL, 3MF, OBJ, PLY, DXF, SVG, CSV et projet. Fichier, Importer une carte KiCad (Ctrl+I) reprend une vraie carte (.kicad_pcb) avec son contour, ses trous et ses composants, et le boîtier épouse sa forme ; en ligne de commande : `promethee importer-kicad carte.kicad_pcb projet.prom.json`.

## Compiler sous Linux (Ubuntu 24.04)

```sh
sudo apt install cmake ninja-build g++ libtbb-dev catch2 nlohmann-json3-dev libfontconfig-dev libx11-dev \
  qt6-base-dev libgl-dev libglu1-mesa-dev \
  libocct-modeling-algorithms-dev libocct-modeling-data-dev libocct-data-exchange-dev \
  libocct-foundation-dev libocct-visualization-dev
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
build/app/Promethee
```

Sans Qt 6, tout le reste se compile et l'application de bureau est simplement ignorée. La CI construit aussi Windows (MSVC, vcpkg) et macOS (Homebrew).

## Utiliser l'outil

```sh
promethee exemple station.prom.json          # écrit le projet d'exemple
promethee verifier station.prom.json         # vérifie la carte et le boîtier
promethee corriger station.prom.json         # applique les corrections automatiques
promethee exporter station.prom.json sortie  # boitier.step, boitier.stl, couvercle.stl
promethee fabrication projet.prom sortie     # dossier de fabrication de la carte (Gerber, perçages, placement, nomenclature), contrôlé
promethee fabrication-exemple sortie         # la même chose pour la carte d'exemple
python3 tests/verifier_fabrication.py sortie # relecture indépendante du dossier (format, unités, origine, couches, trous)
```

Les fichiers `.prom.json` sont communs au prototype web et au logiciel natif.

## Licence et contributions

GPL-3.0 seule (voir [LICENSE](LICENSE)), sans double licence. Composants tiers et leurs licences : [TIERS.md](TIERS.md). Les contributions sont acceptées sous certificat d'origine du développeur (DCO) : chaque commit porte une ligne `Signed-off-by`. Détails dans [CONTRIBUER.md](CONTRIBUER.md).


## Essais de l'interface

« Promethee --essais-interface » rejoue un parcours complet en cliquant réellement dans la vue 3D et dans l'esquisseur (plans, esquisse, régions, esquisse sur face, coque, congé) et rend le nombre d'échecs. « Promethee --essais-onglets » rejoue les scénarios de documents et d'onglets, « Promethee --essais-electronique » le parcours de l'atelier électronique (schéma, circuit, routage, zone, DRC, composant créé, publication, révision), par de vrais gestes de souris et de clavier, sans signal émis ni action déclenchée à la main ; avec --onglets, le même parcours tourne dans la fenêtre à onglets, et l'intégration continue le lance aussi en 1 024 × 768. L'intégration continue les lance sous Linux à chaque envoi, et sous Windows sur le paquet tel qu'il est livré (rendu OpenGL logiciel). Le dossier de fabrication d'un corpus de sept cartes (`tests/audit_electronique/corpus_fabrication.cpp`) est relu à chaque envoi par des lecteurs écrits sans le code C++ : `tests/verifier_fabrication.py` (Gerber, Excellon, placement, nomenclature, IPC-D-356 et gbrjob champ par champ), gerbv et, s'il est installé, gerbonara. PROMETHEE_THEME=promethee|clair|sombre|contraste|systeme impose un thème sans le garder (captures, essais). Les mesures de performance se lancent à la demande : `build/tests/promethee_tests "[mesures]"`. Les cartes de mesure de l'atelier électronique s'écrivent par `promethee carte-mesure 300 carte.prom` (1 000, 2 000…) ; `build/tests/mesures_electronique` relève sur celles de 300, 1 000 et 2 000 composants le temps de chaque moteur (connectivité, ERC, plan de masse, DRC, synchronisation, lecture et écriture), et le parcours de l'atelier ajoute ceux des gestes sur la carte de 300 composants (lignes « MESURE ») ; l'intégration continue les écrit dans le résumé de chaque construction. « Promethee --electronique-demo schema|circuit|3d|fabrication|bibliotheque|composant » ouvre l'exemple électronique (avec --capture image.png pour une capture).


## Esquisse : raccourcis

Espace ouvre la ligne de commande (« c 50 » trace un cercle de Ø 50 coté, « r 40 30 » un rectangle, « ? » donne la liste) ; le clic droit maintenu ouvre le menu radial (haut : cote, bas : ligne, gauche : rectangle, droite : cercle) ; X bascule le mode construction ; Ctrl+Z et Ctrl+Y annulent et rétablissent dans l'esquisse ; Ctrl+Entrée ou la coche verte la terminent.
