# Relecture indépendante des folios électriques exportés en DXF par Prométhée (« Folio en DXF », dossier complet,
# captures de --essais-electrique : exemple-commande.dxf, grafcet-feux.dxf). Chaque fichier est relu par ezdxf
# (mode de récupération, puis audit), et l'on vérifie la version R12, les unités, les calques attendus et la
# présence des textes. Usage : python3 verif_dxf_folios.py fichier.dxf [autres.dxf…]
import sys

import ezdxf
from ezdxf import recover

defauts = 0
for chemin in sys.argv[1:]:
    try:
        doc, audit = recover.readfile(chemin)
    except Exception as ex:  # fichier illisible : défaut
        print(f"DÉFAUT {chemin} : lecture impossible ({ex})")
        defauts += 1
        continue
    msp = doc.modelspace()
    calques = sorted({e.dxf.layer for e in msp})
    textes = [e.dxf.text for e in msp.query("TEXT")]
    types = {}
    for e in msp:
        types[e.dxftype()] = types.get(e.dxftype(), 0) + 1
    ok = doc.dxfversion == "AC1009" and len(audit.errors) == 0 and textes and "CADRE" in calques
    print(f"{'OK    ' if ok else 'DÉFAUT'} {chemin} : version {doc.dxfversion}, audit {len(audit.errors)} erreur(s), "
          f"{len(audit.fixes)} correction(s), calques {calques}, entités {types}, {len(textes)} texte(s)")
    defauts += 0 if ok else 1
sys.exit(1 if defauts else 0)
