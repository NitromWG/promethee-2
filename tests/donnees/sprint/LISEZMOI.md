# Cartes Sprint-Layout d'essai

Fichiers réels repris des données d'essai de KiCad (`qa/data/pcbnew/plugins/sprint_layout`, dépôt KiCad,
licence GPL-3.0-or-later, révision 855da262), où ils servent à éprouver l'importeur Sprint-Layout de KiCad :

- `12F629_SM.lay6` : programmateur PIC à composants CMS et traversants, plans de masse dessus et dessous ;
- `gpio2nesc.lay6` : adaptateur à ATmega8, plans de masse sur les deux faces, polygones d'évidement ;
- `karpaty-rx-pcb5-buffer-freq-doubler-orig.lay6` : carte de récepteur, deux liaisons gardées au chevelu.

Ils servent aux essais 10.18 de la 0.34 (`tests/test_034_l10c.cpp`) : lecture jusqu'au dernier octet, réseaux
tirés du cuivre et du chevelu, plans repris en zones, empreintes partagées entre composants de même forme.
