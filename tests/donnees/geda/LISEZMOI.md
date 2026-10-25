# Cartes gEDA PCB et schémas gschem d'essai

Fichiers réels repris des données d'essai de KiCad (dépôt KiCad, licence GPL-3.0-or-later, révision 855da262) :

- cartes `goodfet50.pcb` (GoodFET 50, auteurs nommés dans le fichier), `powermeter.pcb`, `onsolder_test.pcb`
  (`qa/data/pcbnew/plugins/geda`) ;
- schémas `goodfet50.sch` et `powermeter.sch`, dont les cartes ci-dessus portent la netlist (écrite par les outils
  de gEDA), `multislot_test.sch` et `7400-1.sym` (portes), `multipage_page1.sch` et `multipage_page2.sch`,
  `hierarchy_test.sch` et `hierarchy_sub.sch` (feuille fille), et le projet `ayab/` (schéma, `gafrc`, symboles du
  projet et de la bibliothèque standard) (`qa/data/eeschema/io/geda`).

Ils servent aux essais 10.18 de la 0.34 (`tests/test_034_l10c.cpp`) : la netlist lue dans chaque schéma est
comparée à celle que gEDA a écrite dans la carte.
