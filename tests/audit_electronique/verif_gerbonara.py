# Prométhée : relecture des dossiers de fabrication par gerbonara, lecteur Gerber et Excellon tiers (audit Q4).
# Utilisation : python3 verif_gerbonara.py <dossier> [<dossier>…] ; code 1 si un fichier est illisible ou si la
# pile de couches n'est pas reconnue. Les avertissements sont affichés sans bloquer (gerbonara signale par exemple
# « G90 » après l'en-tête Excellon, que KiCad écrit aussi). Licence GPL-3.0-only.
import glob
import os
import sys
import warnings

from gerbonara import ExcellonFile, GerberFile, LayerStack

erreurs = 0


def relire(chemin, lecteur, compte):
    global erreurs
    with warnings.catch_warnings(record=True) as w:
        warnings.simplefilter('always')
        try:
            objet = lecteur(chemin)
            print(f"  {os.path.basename(chemin)} : {compte(objet)} ; avertissements : {len(w)}")
            for x in w[:2]:
                print('     ', str(x.message)[:140])
        except Exception as e:
            erreurs += 1
            print(f"  {os.path.basename(chemin)} : ERREUR {type(e).__name__}: {e}")


for d in sys.argv[1:]:
    print(f"{d} :")
    for f in sorted(glob.glob(os.path.join(d, 'Gerber', '*.gbr'))):
        relire(f, GerberFile.open, lambda g: f"{len(g.objects)} objets, fonction {getattr(g, 'file_attrs', {}).get('.FileFunction')}")
    for f in sorted(glob.glob(os.path.join(d, 'Percage', '*.drl'))):
        relire(f, ExcellonFile.open, lambda e: f"{len(list(e.drills()))} trous")
    with warnings.catch_warnings(record=True):
        warnings.simplefilter('always')
        try:
            pile = LayerStack.open(d)
            couches = sorted(str(k) for k in pile.graphic_layers.keys())
            print(f"  pile reconnue : {len(couches)} couches, {len(list(pile.drill_layers))} perçage(s)")
            if not any('copper' in c for c in couches) or not any('outline' in c for c in couches):
                erreurs += 1
                print("  ERREUR : pile sans cuivre ou sans contour")
        except Exception as e:
            erreurs += 1
            print(f"  pile : ERREUR {type(e).__name__}: {e}")

print(f"{erreurs} erreur(s)")
sys.exit(1 if erreurs else 0)
