# Refonte de l'électronique : ce que livrent la 0.33 et la 0.34, et ce qui reste

Le plan de refonte du module électronique (audit du 15 octobre 2026) découpe le travail en lots L0 à L19. Les lots L0 à L3 sont livrés en 0.29 à 0.32. La 0.33 livre un socle de chaque lot L4 à L19 ; la 0.34 livre toutes les tâches que la 0.33 laissait, chacune avec ses essais du noyau (`tests/test_034_*.cpp`) et, quand elle a des gestes, ses parcours d'interface (`app/EssaisElectronique034*.cpp`, lots `034L4` à `034L19`). Ce fichier dit, lot par lot, ce que chaque version livre, puis ce qui reste en partie.

## L4 · Empreintes et bibliothèque

Livré en 0.33 : Articles du projet (symbole, empreinte, modèle 3D, sources, paramètres) avec version, héritage d'un article de base champ par champ et mise à jour proposée aux composants qui en sont tirés ; recherche riche (« Résistance>1k -0402 ») sur 10 000 articles ; « 10k 0603 » pose la bonne résistance avec sa hauteur et son corps ; brochage MOSFET vérifié par boîtier ; pastilles IPC-7351B nominales calculées ; fenêtre des empreintes filtrée par famille ; ERC-08 et code LCSC ; archives SnapEDA reprises en un article contrôlé ; bibliothèques citées par variable (${BIBLIO_EQUIPE}) ; standardisation d'un composant générique.

Livré en 0.34 : Fentes en Excellon détourées (G00, M15, G01, M16) ou en G85 selon le profil du fabricant, trous décalés et trous en plus, tolérances, tableau et plan de perçage, forets du profil, sept jeux d'options relus aux mêmes trous (4.13, 4.27) ; approvisionnement hors ligne, besoins par carte et stock en tiroir (4.18) ; échange de symboles et de valeurs en masse, en une commande (4.20) ; bibliothèque en tableau, série E24 de 1 Ω à 1 MΩ générée et contrôlée (4.23) ; pastilles chanfreinées, trapèzes et libres, vernis et pâte propres, motifs de pâte, gabarits de piles de pastilles suivis par toutes les pastilles qui les citent (4.24) ; générateur IPC étendu jusqu'aux BGA, à 0,05 mm de l'empreinte KiCad équivalente (4.25), et par nom en série dans les densités M, N et L (4.26) ; affectation des empreintes en tableau non modal, par règles d'équivalence (4.28).

## L5 · Circuit imprimé

Livré en 0.33 : Routeur interactif qui contourne à l'isolement et à 45°, et mode Pousser ; liaison routée d'office ou laissée au chevelu avec sa raison ; violations exclues qui reviennent « à revoir » quand l'objet bouge ; « À placer » groupé par feuille ; couronne de LED ; cartouche de tracé près de la tête avec auréole d'isolement ; repère de sérigraphie déplacé et gardé ; rapport de vérification HTML relié aux objets.

Livré en 0.34 : Contour modifié par ses sommets et saisi dans le repère de travail, collisions d'encombrement en direct, zones de règles et de placement (5.4, 5.31) ; renumérotation selon la carte et chevelu mesuré (5.6, 5.19) ; nettoyage prévu puis choisi, segment glissé à 45°, via déplacée, déroutage, pinceau et lissage (5.9, 5.23) ; plusieurs liaisons routées d'une commande, paires à écart constant, motifs d'accord de longueur refaits quand la règle change, gouttes (5.9, 5.10, 5.21, 5.22) ; zones : plan du réseau, opérations booléennes, création depuis des pistes, hachures, remplissage figé ou périmé, couture de vias, plan partagé entre trois rails sans recouvrement (5.11, 5.25, 5.26) ; gestionnaire de contraintes à valeurs minimale, préférée et maximale résolues par priorité puis par sévérité, tables d'isolement sourcées (IPC-2221B, CEI 60664-1), règles en fichier, quinze contrôles nouveaux de la DRC (5.12, 5.27, 5.28) ; noms de réseaux à chemin (5.14) ; QR code, Micro QR, Code 128, logos vectorisés, cotes et gabarits de carte (5.15) ; presse-papiers du circuit et collage spécial (5.16) ; carte sans schéma (5.17) ; placement repris d'un CSV de KiCad ou d'un classeur d'Altium, découplage placé au plus près de sa broche (5.19, 5.24) ; placement et pistes guidés par les règles (5.30) ; réglages repris d'une carte à l'autre (5.32).

## L6 · Mécatronique

Livré en 0.33 : Modèles STEP exacts qui remplacent la boîte et donnent la hauteur ; dossier de modèles associé d'un coup ; carte équipée en STEP nommé ; contrôle de la carte dans son boîtier (DRC qui vise le composant) ; découpes de paroi qui suivent les connecteurs ; façades (découpes, légendes, échelles) en SVG et DXF ; contour piloté par une esquisse ; coques du boîtier en STL posés à plat, avec volume, masse et coût d'impression.

Livré en 0.34 : Boîtier du commerce importé en STEP et contrôlé contre la carte (6.7) ; pièces montées sur la carte, comptées dans la nomenclature commune (6.8) ; points de vue nommés communs aux deux vues 3D (6.11) ; distances exactes entre corps, écart de la DRC qui mène aux deux (6.12) ; échanges IDF avec la CAO mécanique (6.17) ; rendus à fond transparent, interruptibles, et images clés (6.18).

## L7 · Schéma avancé et éditeurs

Livré en 0.33 : Bus D[0..7] ; feuilles de canal répétées avec valeurs par canal ; ERC de polarité des électrolytiques ; composants routés déplacés vers une autre feuille avec leurs pistes ; congés et chanfreins au circuit ; contour décalé de pastilles proches en un polygone.

Livré en 0.34 : Hiérarchie par blocs de feuille, feuilles à plat migrées sans changer un réseau, ports synchronisés (7.2, 7.3) ; variables de texte et expressions, cartouches et formats modifiables, renvois entre feuilles sous les bobines, textes riches, images et tableaux (7.6, 7.10, 7.11) ; éditeur de symboles en tableau, symboles collés depuis une fiche, contrôles CS, broches et leurs fonctions (7.7, 7.18) ; échange de portes et de broches avec rétroannotation (7.8) ; éditeur d'empreintes et contrôles CE, BGA renuméroté (7.9, 7.21) ; faisceaux et bus sur plusieurs niveaux (7.13) ; directives compilées en classes et règles (7.14) ; disposition d'une voie recopiée sur les autres (7.16) ; formes importées en DXF et en SVG (7.19) ; dessin normalisé des symboles par l'assistant (7.20) ; cuivre propre à la carte sans toucher la bibliothèque (7.24) ; styles et réglages par défaut en jeux (7.25).

## L8 · Fabrication et cartes complexes

Livré en 0.33 : Panneau en languettes contrôlé ; routage complet de l'exemple ; impédance IPC-2141 et paires différentielles proposées pour l'empilage ; connectivité relue dans les fichiers écrits avant l'envoi ; devis JLCPCB et PCBWay hors ligne ; visionneuse HTML autonome.

Livré en 0.34 : Gerber à arcs et macros, Gerber X3 des composants (8.4, 8.7) ; nomenclature configurable aux formats JLCPCB et PCBWay (8.5) ; archive pour le fabricant avec récapitulatif et statistiques (8.6) ; lignes de fuite et taux de cuivre (8.10) ; PDF complétés par mise à jour incrémentale, signets, liens, recherche, typon compensé (8.11, 8.29) ; demi-trous (8.12) ; sérigraphie préparée et numéro de série par copie (8.13) ; règles d'assemblage à condition, pâte, colle et vernis (8.14) ; positions communes et panneau de copies liées à leur carte source (8.15, 8.16) ; contrôles de fabrication décrits en données, pièges à acide compris (8.17) ; empilage à 32 cuivres typés, impair, asymétrique ou sur âme (8.20) ; lignes de transmission en formules fermées, pertes de Huray et de Djordjevic et Sarkar, à 2 % du calculateur de KiCad (8.21) ; vias en piles de pastilles, micro-vias laser, vias remplies, contre-perçage (8.22, 8.23) ; contraintes du noyau et cinq familles de règles ajoutées à la DRC (8.24, 8.25) ; signaux, bus et gabarits d'interface (8.26) ; paires complètes et accords en retard (8.27) ; routeur automatique à stratégies comparées, échanges de broches par l'algorithme hongrois (8.28) ; dossier de plans dont le tableau de perçage suit la carte (8.30).

## L9 · Performances

Livré en 0.33 : DRC de 2 000 composants avec zone sous 10 s, en fond ; glisser à 60 images par seconde ; connectivité linéaire ; remplissage local d'une zone ; 3D sans recalcul ; UUID doublés réparés à la lecture ; chien de garde du fil principal ; version portable.

Livré en 0.34 : Calcul géométrique robuste aux cas dégénérés (9.6) ; routeur d'appoint sur l'index spatial, sans recalcul complet entre passes (9.8).

## L10 · Échanges

Livré en 0.33 : Netlist KiCad lue et carte mise à jour depuis une netlist ; DXF de la mécanique de la carte ; Specctra (DSN, SES relue) pour Freerouting ; dossier Gerber relu en carte modifiable aux mêmes réseaux ; BoardView et IPC-2581.

Livré en 0.34 : KiCad complet : cartes importées et exportées avec leur .kicad_pro, schémas hiérarchiques, bibliothèques entières (10.1 à 10.4) ; EAGLE, EasyEDA et LCSC (10.5, 10.6) ; carte équipée en GLB, VRML, STL, COLLADA, AMF, OpenSCAD, XAO et STEP compressé (10.10) ; chaîne d'import commune avec tableau des couches (10.12) ; schémas KiCad 5, LTspice et synoptiques Visio (10.14) ; projets Altium (schémas binaires et ASCII, cartes, bibliothèques, conteneur CFB) (10.15) ; PADS et CADSTAR (10.17) ; formats anciens : Sprint-Layout, Protel Autotrax, gEDA PCB, gschem, P-CAD 2000 (10.18) ; reprise de l'existant en série, fiches IPC/DAC-2552 et boîtiers JEP30 (10.19) ; netlists tierces et écrivain CSV unique (10.20) ; dessins DXF et SVG (10.21) ; exports PADS, P-CAD et archive pour Altium (10.22) ; échanges IDX avec la mécanique (10.24).

## L11 · Simulation

Livré en 0.33 : Netlist SPICE tirée du schéma avec contrôles avant calcul ; ngspice chargé comme bibliothèque partagée au moment du calcul, sur son fil, interruptible (libngspice doit être installée sur le poste) ; filtre RC à 1 %, astable 555 à 5 % ; valeur réglée pendant la simulation et réécrite au schéma en un pas ; sources en français.

Livré en 0.34 : Modèles des articles compatibles PSpice, LTspice et HSPICE (11.5) ; calculateurs en quatre familles, dont la carte, l'établi et l'énergie (11.7, 11.17 à 11.19) ; netlist ouverte et partie simulée seule (11.8) ; préparation et remèdes en français (11.9) ; modèles intégrés ajustés sur la fiche, IBIS et Verilog-A (11.11, 11.12) ; courbes calculées par expression et exports (11.13) ; classeur gardé dans le projet (11.14) ; mesures automatiques comparées à une référence (11.15).

## L12 · Variantes et travail à plusieurs

Livré en 0.33 : Variantes de montage (non monté, valeur, article, empreinte) et variante active en un pas ; coût par palier pour N cartes ; comparaison par catégorie et fusion à trois voies ; historique local par instantanés compressés, restauré à l'octet ; verrou à côté du projet avec signal de vie ; archive du projet ; notes attachées aux objets ; modèles de projet et réglages par sections ; schéma de système entre cartes avec son ERC.

Livré en 0.34 : Carte posée comme appareil du projet électrique et bornier câblé sur un folio (12.6) ; matériel libre sous CERN-OHL, paquet rouvert à l'identique (12.11) ; revue formelle signée et rapport PDF figé sur l'empreinte SHA-256 (12.13) ; bibliothèque d'équipe hors réseau reversée sans doublon (12.15) ; Git du poste, sans réseau tant qu'il n'est pas activé, contributions d'articles (12.16) ; nomenclature commune du système et données d'achat (12.18) ; liens entre disciplines « à revoir » jusqu'à l'accusé de lecture, faisceaux entre cartes (12.20).

## L13 · Automatisation

Livré en 0.33 : Ligne de commande (erc, drc, revue, valider, exigences, sorties, cout, comparer, fusionner, archiver, extraire, placer, api, isolement, mise-en-service) ; jeux de sorties avec contrôles avant publication ; interface JSON-RPC 2.0 sur un canal local réservé à l'utilisateur, chaque requête ou lot étant un pas d'annulation ; placement automatique ; revue de conception par règles (LED sans résistance, régulateur sans condensateur, I2C sans tirage).

Livré en 0.34 : API Python sur les commandes annulables et console ancrée (13.3, 13.9) ; propositions par le protocole MCP, refusables (13.4) ; brochages lus en BSDL (13.5) ; ligne de commande sans écran (13.6) ; macros sous le canevas (13.8) ; langage d'expressions unique pour les requêtes et les règles (13.11) ; circuits types posés par les règles (13.14) ; assistant local au choix, sans connexion sortante (13.15) ; fiches techniques interrogées avec l'extrait qui répond (13.16) ; simulation confiée à l'assistant (13.17).

## L14 · Simulation avancée

Livré en 0.33 : Monte-Carlo, pire cas et balayage ; assistants qui calculent en valeurs normalisées puis posent sur la feuille une résistance de LED, un diviseur, un astable à 555, un filtre RC ou de Sallen-Key (coupure simulée à 3 % de la prévision).

Livré en 0.34 : Simulation continue et mixte avec instruments, générateurs réglés pendant le calcul (14.2, 14.4) ; retour visuel sur le schéma, LED qui fument (14.3) ; instruments écrits en Python (14.5) ; décodage I2C et déclenchement (14.6) ; fonctions de transfert symboliques (14.7) ; Xyce à côté de ngspice (14.8) ; analyseur de réseau et paramètres S (14.10) ; mécanique du projet dans la simulation (14.11) ; alimentations à découpage posées avec leur nomenclature (14.14).

## L15 · Microcontrôleurs, FPGA et logique

Livré en 0.33 : Logique à quatre états (portes, bascules, oscillateur en anneau, diviseur) avec table de vérité ; export Verilog structurel ; symbole de FPGA tiré de son brochage, une unité par banc.

Livré en 0.34 : Onglet programme et écarts avec le schéma (15.3) ; co-simulation de l'ATmega328P par simavr et terminal série (15.4) ; mise au point par avr-gdb (15.5) ; modules et organigrammes convertis en croquis Arduino (15.6) ; composants décrits en Verilog ou en VHDL, co-simulés par Icarus, GHDL ou Verilator (15.7) ; robot suiveur de ligne éprouvé sans matériel (15.8) ; machines à états (15.10) ; logique programmable et GAL16V8 (15.12).

## L16 · Prototypage et atelier

Livré en 0.33 : Gravure à l'anglaise (sillons d'isolement en G-code), perçage, découpe avec attaches et pochoir de pâte.

Livré en 0.34 : Platine d'essai synchronisée avec le schéma, montée d'elle-même, imprimée et en 3D (16.2 à 16.4) ; documents de câblage et guitare (16.6) ; palpage en grille, profondeur corrigée par interpolation bilinéaire, nettoyage en poche, plaque à bandes (16.8) ; salle de TP : réglages de l'établissement, essais du poste, recueil d'exemples (16.10) ; consignes en données vérifiées sur le projet de l'élève (16.11) ; couche de l'enseignant scellée, parties verrouillées, pannes cachées (16.12) ; évaluation par isomorphisme de graphes (16.13) ; classe sans service en ligne, IMS Common Cartridge, page HTML autonome qui simule hors ligne (16.14).

## L17 · Essais, conformité et fiabilité

Livré en 0.33 : Plan de mise sous tension (ohmmètre puis rails dans l'ordre des régulateurs) ; points de test, couverture et procès-verbal par numéro de série ; contraintes d'emploi, durée de vie à la chaleur, MTBF, modes de défaillance et conformité.

Livré en 0.34 : Schéma déduit d'une carte sans schéma, aux mêmes réseaux, et calque des retouches par numéro de série proposé à la révision suivante (17.6).

## L18 · Cartes rapides

Livré en 0.33 : Plan de référence sous chaque tronçon de piste, diaphonie de pistes parallèles, chute de tension d'un rail et inductance de montage des découplages, en formules fermées.

Livré en 0.34 : Schéma de lignes, topologie extraite superposée à la prévue, moignons de via signalés (18.4).

## L19 · Entreprise

Livré en 0.33 : Équipe et rôles, cycle de vie des articles, schémas de révision (sans I, O, Q, S, X, Z), numéros internes ; ordres de modification avec relecteur et responsable ; exigences chiffrées vérifiées sur les valeurs réelles.

Livré en 0.34 : Demandes d'articles suivies jusqu'à la notification, santé de la bibliothèque d'équipe sur son fil, fusion et correction annulables (19.4) ; liste d'achat au conditionnement le moins cher, port et douane compris, tableau de bord recalculé sur les seules lignes touchées, page pour les acheteurs et paniers des distributeurs (19.7) ; faisceaux : folio de câblage, câbles, contacts, épissures (19.10), planche à l'échelle 1 et connecteurs dessinés depuis leur modèle 3D (19.11), longueurs par la topologie, en 3D ou mises à plat, circuits continus (19.12), plan de fabrication, classeur, fichiers des machines et devis (19.13) ; dépôt vers le PLM et programme séparé d'accès en ligne : PLM, distributeurs, ERP et tickets dans les deux sens (19.14).

## Ce qui reste en partie

- Les approbations et les dépôts de l'équipe portent le nom, la date et l'empreinte SHA-256 de ce qu'ils approuvent ; leur signature par une clé Ed25519 (libsodium) n'est pas faite (19.2).
- Aucun solveur de champ (openEMS, Elmer, FastHenry) n'est branché : l'intégrité du signal et de l'alimentation reste en formules fermées, validées contre les calculateurs de référence (L18).
- La topologie d'un faisceau se dessine à l'échelle dans sa propre vue ; son tracé avec l'esquisseur de la mécanique reste à faire (19.12).
- Le tracé 3D d'un faisceau relie ses points d'attache par des droites et des arcs au rayon minimal ; il ne suit pas encore une courbe dessinée dans la mécanique (19.12).
- La carte s'exporte en GLB, VRML, STL et dans les autres formats 3D, pas encore en PDF 3D (U3D ou PRC) (10.10).
- Parmi les formats anciens, Protel PCB (autre qu'Autotrax), Tango, Pulsonix et CADint ne sont pas lus (10.18).
- Xyce est employé quand il est installé sur le poste ; son essai n'a pas encore tourné sur une machine qui l'a (14.8).
- La carte des fusibles du GAL16V8 suit le format JEDEC, sans avoir été programmée dans un vrai circuit (15.12).
- La co-simulation passe par des processus à part (simavr, Icarus, GHDL, Verilator) plutôt que par le modèle d_cosim de ngspice (15.4, 15.7).
- La page HTML de classe simule par son propre calcul nodal en JavaScript, pas par ngspice compilé en WebAssembly (16.14).
- Le programme d'accès en ligne (promethee-en-ligne) se compile avec Prométhée mais n'est pas dans le paquet Windows, qui reste sans extension TLS ni accès réseau.
