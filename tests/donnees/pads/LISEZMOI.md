# Fichiers PADS d'essai (10.17, 10.22)

Repris des données d'essai de KiCad (dépôt sous GPL-3.0 ou ultérieure, `qa/data/…/plugins/pads/`) :

- `LCORE_2.asc` : carte réelle de 30 composants (PADS PowerPCB V2005, unités BASIC), sous licence CERN-OHL-W 2.0
  (`LCORE_2.LICENSE`). Son import doit donner une carte entièrement routée sans court-circuit : le cuivre relu relie
  exactement les broches que chaque signal du fichier nomme.
- `signals_schematic.txt` : schéma de PADS Logic (V9.0) à quatre signaux.
