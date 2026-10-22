# Refonte de l'électronique : ce que livre la 0.33 et ce qui reste

Le plan de refonte du module électronique (audit du 15 octobre 2026) découpe le travail en lots L0 à L19. Les lots L0 à L3 sont livrés en 0.29 à 0.32. La 0.33 livre en une fois les lots L4 à L19 : pour chacun, un socle complet et vérifié (essais du noyau `tests/test_electronique_lN.cpp`, parcours d'interface `app/EssaisElectroniqueLN.cpp`), pas la totalité des tâches du plan. Ce fichier dit, lot par lot, ce qui est livré et ce qui reste, par numéro de tâche du plan ; une tâche listée « reste » n'est pas faite, ou seulement en partie, dans la 0.33.

## L4 · Empreintes et bibliothèque

Livré : Articles du projet (symbole, empreinte, modèle 3D, sources, paramètres) avec version, héritage d'un article de base champ par champ et mise à jour proposée aux composants qui en sont tirés ; recherche riche (« Résistance>1k -0402 ») sur 10 000 articles ; « 10k 0603 » pose la bonne résistance avec sa hauteur et son corps ; brochage MOSFET vérifié par boîtier ; pastilles IPC-7351B nominales calculées ; fenêtre des empreintes filtrée par famille ; ERC-08 et code LCSC ; archives SnapEDA reprises en un article contrôlé ; bibliothèques citées par variable (${BIBLIO_EQUIPE}) ; standardisation d'un composant générique.

Reste :

- 4.13 Fentes en Excellon, détourées (G00, M15, G01, M16) et compatibles XNC, G85 en option du profil
- 4.18 Approvisionnement hors ligne
- 4.20 Mise à jour et échange en masse, en une commande
- 4.23 Tableur et gestion de bibliothèque
- 4.24 Pastilles complètes, suite
- 4.25 Générateur IPC étendu
- 4.26 Générateur par nom en série
- 4.27 Perçage complet
- 4.28 Affectation des empreintes en tableau, non modale

## L5 · Circuit imprimé

Livré : Routeur interactif qui contourne à l'isolement et à 45°, et mode Pousser ; liaison routée d'office ou laissée au chevelu avec sa raison ; violations exclues qui reviennent « à revoir » quand l'objet bouge ; « À placer » groupé par feuille ; couronne de LED ; cartouche de tracé près de la tête avec auréole d'isolement ; repère de sérigraphie déplacé et gardé ; rapport de vérification HTML relié aux objets.

Reste :

- 5.4 Zones de règles et de placement, collisions en direct, contour et découpes modifiables par poignées
- 5.6 Chevelu utile, renumérotation selon la carte, corps en couche de fabrication, survol, mesure accrochée
- 5.8 Largeur et via pendant le tracé, couche active synchronisée
- 5.9 Glisser une piste posée, nettoyage, routage de plusieurs pistes
- 5.10 Paires différentielles, accord de longueur, gouttes
- 5.11 Zones
- 5.12 Gestionnaire de contraintes
- 5.14 Noms de réseaux à chemin, fin des suffixes « #2 », migration par alias (E2)
- 5.15 Navigateur de réseaux, cotations, gabarits de carte liés au boîtier, codes 2D et logos
- 5.16 Liste des liaisons restantes utile, presse-papiers du circuit partagé entre vues (C23, C24)
- 5.17 Amener et choisir
- 5.19 Changements en masse, chacun en un pas d’annulation et avec son journal
- 5.22 Cuivre calculé autour des pistes
- 5.23 Édition des pistes posées
- 5.24 Placement assisté
- 5.25 Remplissage enrichi et cuivre généré, dans le moteur Clipper2
- 5.26 Zones et vernis
- 5.27 Règles du gestionnaire de contraintes
- 5.28 Contrôles nouveaux de la DRC, numérotés à la suite des règles
- 5.30 Placement et pistes guidés par les règles
- 5.31 Dessin de la carte
- 5.32 Voir et retrouver

## L6 · Mécatronique

Livré : Modèles STEP exacts qui remplacent la boîte et donnent la hauteur ; dossier de modèles associé d'un coup ; carte équipée en STEP nommé ; contrôle de la carte dans son boîtier (DRC qui vise le composant) ; découpes de paroi qui suivent les connecteurs ; façades (découpes, légendes, échelles) en SVG et DXF ; contour piloté par une esquisse ; coques du boîtier en STL posés à plat, avec volume, masse et coût d'impression.

Reste :

- 6.7 Boîtier du commerce importé en STEP pour les contrôles
- 6.8 Pièces montées sur la carte, nomenclature commune
- 6.11 Navigation et apparence communes aux deux vues 3D, dans une classe partagée par Vue3D et VuePiece
- 6.12 Distances exactes
- 6.17 Échanges avec la CAO mécanique
- 6.18 Images et vidéos de la carte

## L7 · Schéma avancé et éditeurs

Livré : Bus D[0..7] ; feuilles de canal répétées avec valeurs par canal ; ERC de polarité des électrolytiques ; composants routés déplacés vers une autre feuille avec leurs pistes ; congés et chanfreins au circuit ; contour décalé de pastilles proches en un polygone.

Reste :

- 7.2 Modèle hiérarchique
- 7.3 Navigation dans la hiérarchie, fil d’Ariane, synchronisation des ports, ERC-33
- 7.6 Renvois entre feuilles, formats et cartouches modifiables, variables de texte, images et tableaux,…
- 7.7 Éditeur graphique de symboles, tableau des broches, contrôles CS
- 7.8 Échange de portes et de broches avec rétroannotation du schéma
- 7.9 Éditeur graphique d’empreintes, contrôles CE
- 7.10 Cadre, pages et renvois
- 7.11 Textes riches
- 7.13 Connectivité étendue
- 7.14 Conception compilée
- 7.16 Réutilisation
- 7.18 Broches dans l’éditeur de symboles
- 7.19 Formes au schéma, sur le module de formes 2D de 6.14
- 7.20 Dessin du symbole
- 7.21 Éditeur d’empreintes
- 7.24 Cuivre propre à la carte
- 7.25 Styles et réglages par défaut

## L8 · Fabrication et cartes complexes

Livré : Panneau en languettes contrôlé ; routage complet de l'exemple ; impédance IPC-2141 et paires différentielles proposées pour l'empilage ; connectivité relue dans les fichiers écrits avant l'envoi ; devis JLCPCB et PCBWay hors ligne ; visionneuse HTML autonome.

Reste :

- 8.4 Gerber
- 8.5 Nomenclature configurable
- 8.6 Archive pour le fabricant, récapitulatif et statistiques de fabrication
- 8.7 Gerber X3 et plan de perçage
- 8.10 Ligne de fuite
- 8.11 Documents
- 8.12 Usinages de la carte nue dans le modèle
- 8.13 Sérigraphie préparée au circuit et dans l’éditeur d’empreinte
- 8.14 Règles d’assemblage à condition dans le gestionnaire de contraintes (type de boîtier, classe de…
- 8.15 Modèle commun des positions (composants, mires, points de test, avec face et point de référence) et…
- 8.16 Panneau de copies liées à leur carte source, de ce projet ou d’un autre
- 8.17 Moteur de contrôles de fabrication et d’assemblage décrits en données (procédé, fabricant, gravité,…
- 8.20 Empilage de 8.3 porté à 32 cuivres typés (signal, plan, mixte, cavalier), impair, asymétrique ou sur âme…
- 8.21 Moteur de lignes de transmission en formules fermées (Hammerstad et Jensen, Wadell, Kirschning et…
- 8.22 Via en pile de pastilles
- 8.23 Couches internes
- 8.24 Modèle des contraintes dans le noyau
- 8.25 Cinq familles de règles ajoutées à la DRC sur l’index spatial de 5.7
- 8.26 Modèle de signaux dans le noyau
- 8.27 Paires complètes
- 8.28 Routeur automatique sans grille et topologique
- 8.29 Sorties PDF de 8.11 complétées
- 8.30 Éditeur de dossiers de plans sur le moteur de dessin.cpp et de MiseEnPlan.cpp (cadre, cartouche, coupe,…

## L9 · Performances

Livré : DRC de 2 000 composants avec zone sous 10 s, en fond ; glisser à 60 images par seconde ; connectivité linéaire ; remplissage local d'une zone ; 3D sans recalcul ; UUID doublés réparés à la lecture ; chien de garde du fil principal ; version portable.

Reste :

- 9.6 Calcul numérique robuste
- 9.8 Routeur d’appoint sur l’index et sans recalcul complet entre passes (A8)

## L10 · Échanges

Livré : Netlist KiCad lue et carte mise à jour depuis une netlist ; DXF de la mécanique de la carte ; Specctra (DSN, SES relue) pour Freerouting ; dossier Gerber relu en carte modifiable aux mêmes réseaux ; BoardView et IPC-2581.

Reste :

- 10.1 Import de cartes KiCad complet
- 10.2 Export de cartes KiCad avec .kicad_pro, noms actuels, attributs (X5, ECH-02)
- 10.3 Import et export de schémas .kicad_sch (X6, ECH-03)
- 10.4 Bibliothèques KiCad entières, symboles et empreintes (X10, SYM-10, EMP-12, ECH-04)
- 10.5 Import EAGLE, schéma et carte
- 10.6 Import EasyEDA et LCSC
- 10.10 GLB, STL, VRML, PDF 3D
- 10.12 Chaîne d’import commune à tous les formats de cartes
- 10.14 Imports côté schéma
- 10.15 Projets Altium Designer (.PrjPcb) importés d’un coup, schéma ASCII ou binaire et carte reliés,…
- 10.17 Formats de Siemens et de Zuken
- 10.18 Formats anciens
- 10.19 Reprise de l’existant
- 10.20 Netlists tierces
- 10.21 Dessins 2D
- 10.22 Exports vers d’autres outils de CAO
- 10.24 Exports 3D de la carte par les écrivains de geometrie/src/echanges.cpp (OBJ avec matières, PLY, BREP),…

## L11 · Simulation

Livré : Netlist SPICE tirée du schéma avec contrôles avant calcul ; ngspice chargé comme bibliothèque partagée au moment du calcul, sur son fil, interruptible (libngspice doit être installée sur le poste) ; filtre RC à 1 %, astable 555 à 5 % ; valeur réglée pendant la simulation et réécrite au schéma en un pas ; sources en français.

Reste :

- 11.5 Modèles par article, compatibilité LTspice et PSpice
- 11.7 Calculateurs
- 11.8 Netlist ouverte à l’utilisateur
- 11.9 Tableau de bord avant le calcul
- 11.11 Modèles intégrés réglés dans une fiche à champs bornés, comme les calculateurs, qui écrit la ligne…
- 11.12 Modèles Verilog-A compilés par OpenVAF et chargés par ngspice au format OSDI, rattachés à l’article…
- 11.13 Panneau des courbes complété
- 11.14 Classeur de simulation gardé dans le projet, format migré
- 11.15 Mesures automatiques calculées par Prométhée sur les vecteurs à chaque calcul, de la bande passante au…
- 11.17 Famille « Carte », nourrie par l’empilage et l’objet choisi
- 11.18 Famille « Établi »
- 11.19 Famille « Énergie et chaleur », qui prolonge les fiches awg, batterie et armoire de l’atelier électrique

## L12 · Variantes et travail à plusieurs

Livré : Variantes de montage (non monté, valeur, article, empreinte) et variante active en un pas ; coût par palier pour N cartes ; comparaison par catégorie et fusion à trois voies ; historique local par instantanés compressés, restauré à l'octet ; verrou à côté du projet avec signal de vie ; archive du projet ; notes attachées aux objets ; modèles de projet et réglages par sections ; schéma de système entre cartes avec son ERC. Git intégré (12.3, 12.16) n'est pas fait : seuls la fusion et le verrou le sont.

Reste :

- 12.6 Historique des révisions au cartouche
- 12.11 Commande Partager à trois sorties
- 12.13 Rapports PDF de la vie du projet sur un même générateur
- 12.15 Bibliothèque d’équipe hors réseau
- 12.16 Git de 12.3 complété
- 12.18 Données d’achat par ligne de nomenclature (délai d’usine, stock par distributeur, distributeurs agréés,…
- 12.20 Faisceaux intérieurs et entre cartes tirés des connecteurs, des brochages et des réseaux des cartes,…

## L13 · Automatisation

Livré : Ligne de commande (erc, drc, revue, valider, exigences, sorties, cout, comparer, fusionner, archiver, extraire, placer, api, isolement, mise-en-service) ; jeux de sorties avec contrôles avant publication ; interface JSON-RPC 2.0 sur un canal local réservé à l'utilisateur, chaque requête ou lot étant un pas d'annulation ; placement automatique ; revue de conception par règles (LED sans résistance, régulateur sans condensateur, I2C sans tirage).

Reste :

- 13.3 API Python sur les commandes annulables
- 13.4 Serveur MCP
- 13.5 Brochage lu dans un fichier BSDL ou un tableau de fiche
- 13.6 Ligne de commande complétée
- 13.8 Ligne de commande sous le canevas, généralisée depuis celle de l’esquisseur (Esquisseur.cpp
- 13.9 Console Python ancrée dans l’atelier et éditeur de scripts intégré, points d’arrêt et pas à pas par le…
- 13.11 Langage d’expressions unique sur les propriétés des objets, compilé une fois, pour les requêtes comme…
- 13.14 Circuits types posés par le même moteur de règles
- 13.15 Moteur de l’assistant au choix
- 13.16 Fiches techniques interrogées
- 13.17 Simulation confiée à l’assistant

## L14 · Simulation avancée

Livré : Monte-Carlo, pire cas et balayage ; assistants qui calculent en valeurs normalisées puis posent sur la feuille une résistance de LED, un diviseur, un astable à 555, un filtre RC ou de Sallen-Key (coupure simulée à 3 % de la prévision).

Reste :

- 14.2 Simulation continue et mixte
- 14.3 Retour visuel
- 14.4 Instruments de base
- 14.5 Façades et instruments personnalisés
- 14.6 Instruments numériques
- 14.7 Analyses natives de ngspice dans les profils
- 14.8 Autres moteurs et mesures réelles
- 14.10 Analyseurs posés sur le montage, chacun lançant sa propre analyse sur ce qui est branché entre ses bornes
- 14.11 Simulation nourrie par le projet
- 14.14 Alimentations

## L15 · Microcontrôleurs, FPGA et logique

Livré : Logique à quatre états (portes, bascules, oscillateur en anneau, diviseur) avec table de vérité ; export Verilog structurel ; symbole de FPGA tiré de son brochage, une unité par banc.

Reste :

- 15.3 Onglet programme
- 15.4 Co-simulation du microcontrôleur
- 15.5 Mise au point
- 15.6 Modules et organigrammes
- 15.7 Composants décrits par du code et matériel réel
- 15.8 Programme éprouvé sans matériel
- 15.10 Machines à états et schémas à contacts sur la base du GRAFCET (noyau/src/electrique_grafcet.cpp)
- 15.12 Logique programmable

## L16 · Prototypage et atelier

Livré : Gravure à l'anglaise (sillons d'isolement en G-code), perçage, découpe avec attaches et pochoir de pâte.

Reste :

- 16.2 Vue platine d’essai
- 16.3 Connexions de la platine
- 16.4 Platine imprimée et en 3D
- 16.6 Documents de câblage
- 16.8 Compléments d’usinage
- 16.10 Salle de TP
- 16.11 Consignes et séries
- 16.12 Ce que l’enseignant impose
- 16.13 Évaluation et tuteur
- 16.14 Classe et diffusion

## L17 · Essais, conformité et fiabilité

Livré : Plan de mise sous tension (ohmmètre puis rails dans l'ordre des régulateurs) ; points de test, couverture et procès-verbal par numéro de série ; contraintes d'emploi, durée de vie à la chaleur, MTBF, modes de défaillance et conformité.

Reste :

- 17.6 Le schéma de liaison de l’import KiCad (kicad_electronique.cpp) devient une commande du noyau valable…

## L18 · Cartes rapides

Livré : Plan de référence sous chaque tronçon de piste, diaphonie de pistes parallèles, chute de tension d'un rail et inductance de montage des découplages, en formules fermées. Aucun solveur de champ (openEMS, Elmer, FastHenry) n'est branché.

Reste :

- 18.4 Schéma de lignes

## L19 · Entreprise

Livré : Équipe et rôles, cycle de vie des articles, schémas de révision (sans I, O, Q, S, X, Z), numéros internes ; ordres de modification avec relecteur et responsable ; exigences chiffrées vérifiées sur les valeurs réelles. Les dépôts signés Ed25519, le PLM et l'ERP ne sont pas faits.

Reste :

- 19.4 Une demande d’article manquant se remplit dans un formulaire (référence, fiche jointe, champs imposés…
- 19.7 Le tableau de bord de la nomenclature se recalcule à chaque commande, sur les seules lignes touchées
- 19.10 Le schéma de câblage de faisceau est un nouveau folio de l’atelier électrique, qui garde fils numérotés,…
- 19.11 La planche existante se dessine depuis la connectivité de 19.10
- 19.12 Les feuilles de topologie dessinent à l’échelle, avec l’esquisseur de la mécanique, les espaces d’une…
- 19.13 Le plan de fabrication du faisceau suit un gabarit de plan de L8
- 19.14 Les liaisons d’entreprise passent d’abord par des fichiers

## Ce qui reste en dehors des lots

La console Python, le serveur MCP et l'assistant local (llama.cpp) attendent que l'interface JSON-RPC de la 0.33 serve de base ; la co-simulation de microcontrôleur, la vue platine d'essai, les importeurs Altium, EAGLE, PADS et Cadence, les solveurs de champ, le PLM et les faisceaux entre cartes restent à faire. Une variante active n'est pas encore barrée dans les vues ni dans le PDF du schéma.
