# Projet KiCad 5 d'essai (10.14)

Schéma au format d'avant KiCad 6 (« EESchema Schematic File Version 4 ») : `filtre.sch` (racine) et sa feuille
hiérarchique `etage.sch`, symboles tirés du cache du projet `filtre-cache.lib` (comme KiCad 5 l'écrit à côté du
schéma). On y trouve un connecteur retourné, des résistances tournées, des symboles d'alimentation tournés (GND, VCC),
un drapeau d'alimentation, des étiquettes locales et globales, une feuille fille reliée par ses étiquettes
hiérarchiques, une jonction en T et un inverseur dont les broches d'alimentation sont cachées (VCC 14, GND 7) :
KiCad 5 les relie aux réseaux de leur nom.

`filtre.net` est la netlist que KiCad 5 écrit pour ce schéma (format « (export (version D) … ») : les réseaux de
l'import de Prométhée doivent lui être identiques.
