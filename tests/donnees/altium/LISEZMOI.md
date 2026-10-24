# Documents Altium d'essai (10.15, 10.18)

Fichiers repris des données d'essai de KiCad (dépôt sous GPL-3.0 ou ultérieure, `qa/data/…/plugins/altium/`) :

- `overview.SchDoc`, `pagea.SchDoc`, `pageb.SchDoc` : schémas Altium au format ASCII, une feuille de tête dont le bloc
  nomme deux pages, et deux pages qui se renvoient l'une à l'autre (renvoi croisé que l'import doit couper).
- `ESP32-WROOM-32.PcbLib` : bibliothèque d'empreintes Altium (module ESP32-WROOM-32 d'Espressif, avec sa pastille de
  masse faite de pastilles, de vias et de surfaces de cuivre) ; `ESP32-WROOM-32.kicad_mod` est l'empreinte que
  l'importeur Altium de KiCad en tire : l'import de Prométhée doit donner les mêmes pastilles.

Le projet complet `eDP_adapter_dvt1` des mêmes données d'essai (deux schémas, une carte de 112 composants) n'est pas
copié ici ; l'essai qui le lit s'exécute quand la variable `PROMETHEE_DONNEES_ALTIUM` désigne son dossier.
