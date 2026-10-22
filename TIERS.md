# Composants tiers

Prométhée est distribué sous GPL-3.0. Il s'appuie sur les composants suivants, chacun sous sa propre licence.

| Composant | Usage | Licence |
| --- | --- | --- |
| Open CASCADE Technology | Géométrie exacte, échanges STEP et STL, visualisation 3D | LGPL 2.1 avec exception |
| Qt 6 | Interface de l'application de bureau | LGPL 3 |
| nlohmann/json | Lecture et écriture des projets | MIT |
| Catch2 | Tests | Boost Software License 1.0 |
| Clipper2 (Angus Johnson), copie dans `noyau/externe/clipper2` | Booléens, décalages et triangulation des polygones du circuit imprimé : zones de cuivre, isolements, îlots | Boost Software License 1.0 |
| ngspice (bibliothèque partagée installée séparément, chargée au moment du calcul, jamais livrée) | Simulation des circuits ; déclarations de `sharedspice.h` reprises dans `noyau/src/moteur_ngspice.cpp` | BSD modifiée |
| three.js | Vue 3D du prototype web | MIT |
| occt-samples-qopenglwidget (Kirill Gavrilov) | Principe d'intégration d'OpenCascade dans Qt, repris et adapté dans `app/OcctGlTools.*` | MIT, notice reproduite dans ces fichiers |

Le paquet Windows contient aussi le rendu OpenGL logiciel de Qt (`opengl32sw.dll`, Mesa llvmpipe, licence MIT), utilisé seulement si on le copie sous le nom `opengl32.dll`. Prométhée ne se connecte à aucun réseau : de Qt Network, il n'utilise que le canal local (QLocalServer, tube nommé ou socket Unix réservés à l'utilisateur) par lequel un programme du même poste pilote l'atelier ; les extensions TLS et d'état du réseau ne sont pas livrées.

Chaque paquet Windows liste ses composants, leurs versions exactes et leurs licences dans un inventaire SPDX (dossier `sbom`) et dans `VERSIONS.txt` ; les textes des licences sont dans le dossier `licences`, et la source correspondante de Prométhée dans le dossier `source`.

## Bibliothèques d'exécution Microsoft Visual C++ (paquet Windows)

Le paquet Windows contient les bibliothèques d'exécution Visual C++ (msvcp140, vcruntime140 et voisines) du compilateur qui l'a produit, copiées depuis son dossier Redist : déploiement local prévu par les conditions de redistribution de Microsoft Visual Studio (« code distribuable »). Elles restent sous la licence de Microsoft, pas sous GPL, et ne sont pas mises à jour par Windows Update ; installer le redistribuable officiel puis les retirer du dossier fait utiliser la version du système. Le script de paquet vérifie que chaque bibliothèque importée par le programme est soit livrée, soit une bibliothèque de Windows.
