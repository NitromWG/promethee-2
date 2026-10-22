# Refonte de Prométhée — proposition

Version 2, 7 octobre 2026 : proposition validée par Evan, avec ses précisions (ci-dessous) ; fondations livrées en 0.12. Ce document fixe l'organisation du logiciel avant de la coder : il touche le modèle de données, la fenêtre principale et tous les ateliers.

## Ce qui ne va pas aujourd'hui

On arrive dans le logiciel sans savoir ce qu'il permet de faire. Deux ateliers, « Carte et boîtier » et « Pièces », vivent côte à côte sans lien visible, alors que la force de Prométhée est justement de relier la carte, le boîtier et les pièces. L'arbre ne montre qu'une pièce à la fois, sans structure de produit et sans repère de métier. Les barres et les textes s'accumulent au détriment du dessin.

## Cinq principes

**Un projet, c'est un produit.** Tout ce qui le compose (pièces, cartes, schémas, câbles, plans, nomenclature) vit dans le même projet et reste relié : déplacer un connecteur sur la carte met à jour le boîtier, le plan et la nomenclature. L'utilisateur ne manipule jamais de fichiers isolés à garder synchronisés.

**On entre par l'intention, pas par la structure.** L'écran d'accueil demande ce qu'on veut concevoir (une pièce, une carte, un schéma, un produit complet) ; le projet se crée tout seul autour.

**Un seul navigateur, à l'image du produit réel.** L'arbre montre le produit tel qu'il est physiquement : ensembles, sous-ensembles, pièces, cartes, câbles. Chaque élément porte toujours la même couleur et la même icône selon son métier, et se déplie jusqu'à son contenu : fonctions d'une pièce, composants d'une carte, folios d'un schéma.

**Le document ouvert décide des outils.** Une seule barre d'outils, celle du métier en cours ; on passe d'un document à l'autre par des onglets colorés, comme dans un navigateur web.

**Symboles d'abord, canevas maximal.** Le texte passe en infobulle ; rien n'est affiché qui ne serve pas maintenant ; les commandes viennent près du curseur (barre contextuelle, menu radial, ligne de commande).

## Les métiers et leurs documents

Chaque métier a une couleur et une icône, les mêmes partout (accueil, arbre, onglets, liseré de la barre d'outils). La couleur n'est jamais seule porteuse de sens : l'icône suffit à reconnaître le métier, pour les daltoniens comme sur un écran en noir et blanc.

| Métier | Couleur | Icône | Documents |
|---|---|---|---|
| Mécanique | bleu acier | engrenage | Pièce, Assemblage, Mise en plan ; tôlerie plus tard |
| Électronique | violet | puce | Schéma électronique, Circuit imprimé ; import et lien KiCad |
| Électrique | ambre | éclair | Schéma de câblage, Armoire, Faisceau |
| Produit mécatronique | cuivre (couleur de Prométhée) | cubes empilés | Produit complet : cartes, boîtier, pièces et câbles reliés |
| Fabrication | ardoise | règle et équerre | Mises en plan, dossier de fabrication, nomenclature |

## L'écran d'accueil

À gauche, les projets récents avec leur vignette et les pastilles des métiers qu'ils contiennent, puis « Ouvrir » et « Exemples ». Au centre, une seule question : « Que veux-tu concevoir ? », et cinq grandes tuiles, une par métier. Un clic sur une tuile la déplie en ses choix, chacun avec une icône et une phrase : pour la mécanique, « Pièce », « Assemblage », « Mise en plan » ; pour le produit mécatronique, « Partir d'une carte KiCad » et « Produit vide ». Ce qui n'existe pas encore est montré grisé avec la mention « bientôt » : on voit où va le logiciel sans être trompé. Le choix crée le projet et ouvre directement le bon atelier, prêt à travailler (pour une pièce : plans visibles, « Nouvelle esquisse » en évidence).

## La fenêtre de projet

En haut, une barre fine : menu, nom du projet, onglets des documents ouverts (liseré de la couleur du métier), annuler et rétablir en flèches, palette de commandes (Ctrl+K). Juste dessous, la barre d'outils du document actif, en icônes. À gauche, le navigateur, repliable. Au centre, le canevas, qui prend tout le reste. À droite, le panneau des propriétés, qui n'apparaît que pendant un réglage. Plus de bandeau d'atelier : passer de la carte au boîtier, c'est cliquer leur ligne dans l'arbre ou leur onglet.

## Le navigateur

Une seule arborescence, dans cet ordre :

```
▾ Station météo ESP32                       projet
  ▾ Produit                                 assemblage · 4 éléments
    ▾ Boîtier                     méca      ● à jour
      ▸ Origine
      ▾ Extrusion 1
          Esquisse 1
        Congé 1
    ▸ Carte principale            PCB       42 composants
    ▸ Support capteur             méca
    ▸ Câble d'alimentation        électrique
  ▸ Schéma électronique           électronique   3 folios
  ▸ Mises en plan                 fabrication    2
  ▸ Nomenclature
```

Chaque ligne porte la flèche de dépliage, l'icône du métier dans sa couleur, le nom, une précision discrète (type, nombre d'éléments) et, à droite, des pastilles d'état : rouge pour une erreur, orange pour une esquisse sous-contrainte, un œil au survol pour masquer. Au-dessus, un champ de recherche et des puces de filtre : Tout, Méca, PCB, Électronique, Électrique. Les esquisses se rangent sous la fonction qui les utilise (Extrusion 1 ▸ Esquisse 1), comme dans SolidWorks : l'historique se lit d'un coup d'œil. Un clic sélectionne dans le canevas ; un double-clic ouvre ou modifie ; le clic droit offre un menu en icônes ; un glisser déplace un élément d'un sous-ensemble à l'autre.

## Ce que cela change dans le modèle de données

Le projet passe au format 4 : une liste de documents typés (pièce, assemblage, carte, schéma, schéma électrique, mise en plan) et un produit racine, qui est un assemblage plaçant ces documents. Un projet au format 3 est converti à l'ouverture, sans perte : sa carte devient un document « carte » (avec son fichier KiCad source), son boîtier une pièce dérivée de la carte, ses pièces des documents « pièce », et le produit les réunit. Le fichier reste unique ; plus tard, un document pourra être référencé depuis un autre projet (bibliothèque de pièces).

## Les étapes

La version 0.12 pose les fondations : écran d'accueil, navigateur unifié avec ses couleurs de métier et l'historique imbriqué, onglets de documents, nouvelle barre fine, thème visuel revu, format 4 et sa conversion. Les ateliers existants (carte et boîtier, pièces) y entrent tels quels. La 0.13 apporte l'assemblage (placement, contraintes de base, interférences) et la mise en plan. Les éditeurs de schéma électronique et électrique viennent ensuite ; d'ici là, leurs tuiles mènent à l'import KiCad ou sont annoncées « bientôt ».

## Décisions (réponses d'Evan, 7 octobre 2026)

Un projet peut ne contenir qu'une seule chose (une pièce à imprimer, une carte, un caisson en tôle, un circuit) ou n'importe quel mélange de métiers : le logiciel ne sert pas qu'aux projets complets. Le projet reste le conteneur, mais il peut être minimal, et l'arbre n'affiche que les métiers présents.

Chaque métier peut avoir plusieurs parties (plusieurs pièces, plusieurs cartes, plusieurs schémas électriques), chacune avec ses sous-ensembles qui se plient et se déplient ; les documents qui décrivent une partie (ses plans, ses exports) se rangent sous elle.

« Fabrication » disparaît de l'accueil : ce n'était pas un métier. Les mises en plan se rangent sous la pièce ou l'assemblage qu'elles décrivent ; le dossier de fabrication est une action.

Électronique et circuit imprimé : KiCad d'abord, comme moteur électronique (lien vivant avec ses fichiers : import, renvoi des positions, ouverture dans KiCad depuis l'arbre) ; des éditeurs propres viendront ensuite, avec l'objectif d'être aussi complets que l'existant, voire plus.

Électrique (choix laissé à Claude) : même logique. QElectroTech, le logiciel libre de référence pour les schémas électriques (sous licence GPL, et d'origine française), sert de premier moteur pour les schémas de câblage, relié au projet comme KiCad. Le module propre de Prométhée se concentre sur ce qu'aucun outil libre ne fait : armoires implantées en 3D dans l'assemblage, faisceaux routés dans la mécanique avec leurs longueurs réelles, nomenclature commune à tous les métiers.

## Ce que livre la 0.12

L'écran d'accueil (quatre métiers aux couleurs franches, choix dévoilés au clic, « Bientôt » pour ce qui arrive, projets récents) ; le navigateur unifié à gauche (projet, dossiers par métier présent, cartes avec leurs composants, boîtier, pièces dépliables jusqu'à leurs fonctions, esquisses rangées sous la fonction qui les utilise, liseré de couleur par métier, « Ajouter une pièce », « Ajouter une carte KiCad ») ; les projets sans carte. Le format 4 (documents typés, plusieurs cartes, assemblages, mises en plan) vient avec l'assemblage et la mise en plan en 0.13.
