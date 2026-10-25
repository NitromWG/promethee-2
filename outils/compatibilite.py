#!/usr/bin/env python3
"""Prométhée : formats de fichier et compatibilité entre versions (lot L1, tâche 1.22, ECH-10, ECH-11).

  compatibilite.py schemas <promethee> <docs> <donnees> <travail>
      Écrit les projets d'essai avec l'outil en ligne de commande, puis les fait valider par un lecteur JSON Schema
      indépendant (le paquet Python jsonschema) : chaque projet contre le schéma de la version courante, chacun des
      fichiers écrits pour une version précédente contre le schéma de cette version, que le fichier de la version
      courante ne suit pas. Chaque perte attendue doit être dite.

  compatibilite.py version <promethee> <promethee-precedent> <version> <docs> <donnees> <travail>
      La même chose, puis chaque projet écrit pour <version> est relu par l'outil de cette version, qui en écrit le
      dossier de fabrication : la liste des réseaux (IPC-D-356) doit être celle que la version courante écrit pour
      le projet d'origine, broche par broche.

Code de sortie 0 si tout est conforme, 1 sinon ; 77 si jsonschema manque (essai sauté).
Licence GPL-3.0-only.
"""
import json
import pathlib
import subprocess
import sys

VERSIONS = ["0.33", "0.32", "0.31", "0.30", "0.29", "0.28"]
COURANTE = "0.34"
ECHECS = []


def echec(texte):
    ECHECS.append(texte)
    print("ÉCHEC   " + texte)


def ok(texte):
    print("OK      " + texte)


def lancer(*args):
    r = subprocess.run([str(a) for a in args], capture_output=True, text=True, encoding="utf-8")
    return r.returncode, r.stdout + r.stderr


def validateur(docs):
    """Un validateur par version, les schémas reliés par leur $id (lecteur indépendant de Prométhée)."""
    import jsonschema

    schemas = {}
    for f in [docs / "schema_conception.json", docs / "schema_projet.json", *sorted((docs / "schemas").glob("*.schema.json"))]:
        s = json.loads(f.read_text(encoding="utf-8"))
        schemas[s["$id"]] = s
    try:
        from referencing import Registry, Resource

        registre = Registry().with_resources([(i, Resource.from_contents(s)) for i, s in schemas.items()])

        def pour(version):
            s = next(x for i, x in schemas.items() if i.endswith("projet-" + version + ".schema.json"))
            jsonschema.Draft202012Validator.check_schema(s)
            return jsonschema.Draft202012Validator(s, registry=registre)

    except ImportError:  # jsonschema d'avant 4.18
        def pour(version):
            s = next(x for i, x in schemas.items() if i.endswith("projet-" + version + ".schema.json"))
            jsonschema.Draft202012Validator.check_schema(s)
            return jsonschema.Draft202012Validator(s, resolver=jsonschema.RefResolver.from_schema(s, store=schemas))

    return pour


def valider(pour, version, chemin):
    v = pour(version)
    donnees = json.loads(pathlib.Path(chemin).read_text(encoding="utf-8"))
    erreurs = sorted(v.iter_errors(donnees), key=lambda e: list(e.absolute_path))
    if erreurs:
        for e in erreurs[:5]:
            print("        " + "/".join(str(x) for x in e.absolute_path) + " : " + e.message[:200])
        echec(f"{pathlib.Path(chemin).name} ne suit pas le schéma de la {version} ({len(erreurs)} écart(s))")
    else:
        ok(f"{pathlib.Path(chemin).name} suit le schéma de la {version}")


def refuse(pour, version, chemin):
    """Le schéma d'une version précédente refuse le fichier de la courante : les schémas distinguent les versions."""
    donnees = json.loads(pathlib.Path(chemin).read_text(encoding="utf-8"))
    if any(True for _ in pour(version).iter_errors(donnees)):
        ok(f"le schéma de la {version} refuse {pathlib.Path(chemin).name}, écrit par la version courante")
    else:
        echec(f"le schéma de la {version} accepte {pathlib.Path(chemin).name}, écrit par la version courante")


def projets(outil, donnees, travail):
    """Les projets d'essai : mécanique, carte d'exemple, cartes KiCad, et un projet qui porte ce qui se perd."""
    l = []
    code, sortie = lancer(outil, "exemple", travail / "mecanique.prom.json")
    if code != 0:
        echec("exemple mécanique : " + sortie)
    l.append((travail / "mecanique.prom.json", False, {}))
    lancer(outil, "exemple-electronique", travail / "clignotant.prom")
    l.append((travail / "clignotant.prom", True, {}))
    for k in sorted((donnees / "kicad").glob("*.kicad_pcb")):
        sortie = travail / (k.stem + ".prom")
        code, texte = lancer(outil, "importer-kicad-electronique", k, sortie)
        if code != 0:
            echec(f"import de {k.name} : {texte}")
            continue
        l.append((sortie, True, {}))
    # Le projet riche : ce que la 0.34, la 0.33, la 0.32 et la 0.31 ajoutent (réglages de l'ERC, champ bloqué, connecteur hors feuille), ce que la
    # 0.30 ajoutait (dispositions), ce que la 0.29 ajoutait (profil de fabricant personnalisé, brochage vérifié,
    # exclusion de la nomenclature), un texte de carte tourné
    j = json.loads((travail / "clignotant.prom").read_text(encoding="utf-8"))
    j["dispositions"] = {"Routage": json.dumps({"format": "promethee-disposition", "version": 1, "nom": "Routage", "etat": ""})}
    c = j["electronique"][0]
    c["circuit"]["regles"]["capacitesPerso"] = {"isolement": 150000, "largeurMin": 150000, "percageMin": 300000, "anneauMin": 130000, "bord": 300000}
    c["composants"][0]["brochageVerifie"] = c["composants"][0]["empreinte"]
    c["composants"][0]["horsNomenclature"] = True
    c["erc"] = {"gravites": {"DRC-43": "ignorer"}, "exclusions": [{"code": "ERC-26", "objets": [], "raison": "essai", "auteur": "essai", "date": "2026-10-22"}]}
    for s in c["feuilles"][0].get("symboles", [])[:1]:
        s["champVal"] = {"d": [2540000, 5080000], "bloque": True}
    c["feuilles"][0].setdefault("etiquettes", []).append({"uuid": "hors-feuille-essai", "texte": "HORS", "p": [10160000, 10160000], "rot": 0, "globale": True, "horsFeuille": True, "sens": "entree"})
    c["circuit"]["textes"].append({"uuid": "texte-essai-compatibilite", "texte": "V1", "couche": "F.SilkS", "p": [10000000, 10000000], "hauteur": 1000000, "rot": 90.0})
    # Ce que la 0.32 ajoute : valeur calculée, paramètres typés, genre de composant, exclusion de la simulation,
    # variables, synchronisation sur confirmation, corps décalé, fabrication par pastille, volume libre
    k0 = c["composants"][0]
    k0["formule"] = "1k"
    k0["parametres"] = {"tolerance": "1 %"}
    k0["genre"] = "horsNomenclature"
    k0["horsSimulation"] = True
    c["variables"] = {"Vcc": "5V"}
    c["synchroSurConfirmation"] = True
    for d in c.get("bibliothequeEmpreintes", [])[:1]:
        d.setdefault("corps", {"l": 1000000, "p": 1000000, "h": 1000000, "rond": False, "couleur": "#303030"})["dx"] = 500000
        d["corps"]["dy"] = 0
        for pa in d.get("pastilles", [])[:1]:
            pa["fab"] = "bga"
    c["circuit"]["volumes"] = [{"contour": [[0, 0], [5000000, 0], [5000000, 5000000]], "z0": 0, "z1": 2000000, "couleur": "#888888"}]
    # Ce que la 0.33 ajoute : variantes de montage, notes attachées, jeux de sorties, dessins de carte ; au projet,
    # exigences et réglages d'impression du boîtier
    c["variantesMontage"] = [{"nom": "sans LED"}]
    c["notes"] = [{"uuid": "note-essai", "genre": "commentaire", "texte": "essai de compatibilité"}]
    c["jeuxSorties"] = [{"nom": "atelier", "dossier": "sorties/{projet}", "sorties": ["gerber", "percages"]}]
    c["circuit"]["dessins"] = [{"uuid": "dessin-essai", "forme": "trait", "couche": "F.SilkS", "points": [[0, 0], [5000000, 0]], "epaisseur": 150000}]
    j["exigences"] = [{"id": "EX-1", "texte": "coût pour 100 cartes", "grandeur": "cout", "seuil": "40"}]
    j.setdefault("boitier", {})["impression"] = {"matiere": "PLA"}
    # Ce que la 0.34 ajoute : bloc hiérarchique, symbole à deux vues, cuivre plein, point de vue 3D, données d'atelier,
    # licence d'un article ; au projet, licence et liens entre disciplines
    f0 = c["feuilles"][0]
    f0["blocs"] = [{"uuid": "bloc-essai", "nom": "Alim", "feuille": "feuille-fille-essai", "p": [50800000, 50800000], "largeur": 10160000, "hauteur": 5080000,
                    "ports": [{"uuid": "port-essai", "nom": "VCC", "p": [50800000, 53340000], "cote": 0}]}]
    s0 = c["symboles"][0]
    s0["vues"] = 2
    if s0.get("broches"):
        b2 = dict(s0["broches"][0])
        b2["vue"] = 2
        s0["broches"].append(b2)
    s0.setdefault("traces", []).append({"forme": 0, "points": [[0, 0], [2540000, 0]], "vue": 2})
    # Cuivre plein de la masse dans le plan de masse du dessous, loin des pastilles : aucun réseau ne change
    c["circuit"]["regions"] = [{"uuid": "region-essai", "reseau": "GND", "couche": "B.Cu", "forme": [[1000000, 1000000], [1500000, 1000000], [1500000, 1500000], [1000000, 1500000]]}]
    c["pointsDeVue"] = [{"nom": "Face", "cible": [0, 0, 0], "azimut": 0, "elevation": 30, "distance": 100, "orthographique": False, "champ": 45}]
    c["extensions"] = {"platine": {"rangees": 30}}
    c.setdefault("articles", []).append({"id": "ART-LIBRE-ESSAI", "nom": "Carte libre", "licence": "CERN-OHL-S-2.0"})
    j["licence"] = "CERN-OHL-S-2.0"
    j["liensDisciplines"] = [{"id": "L1", "de": "carte", "vers": "boitier", "quoi": "contour", "empreinte": "a1", "releve": "a1", "etat": "à jour"}]
    riche = travail / "riche.prom"
    riche.write_text(json.dumps(j, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    l32 = ["valeur calculée", "paramètre typé", "genre de", "de la simulation", "variable", "synchronisation sur confirmation", "corps d'empreinte",
           "propriété de fabrication", "volumes libres"]
    l31 = l32 + ["gravités réglées", "alerte exclue", "champ de symbole", "connecteur hors feuille"]
    l33 = ["variante de montage", "note attachée", "jeu de sorties", "dessin de carte", "exigence", "impression 3D"]
    l34 = ["bloc hiérarchique", "seconde vue (De Morgan)", "région de cuivre plein", "point de vue 3D", "platine d'essai", "licence de 1 article", "licence du projet",
           "lien entre disciplines"]
    attendues = {"0.33": l34, "0.32": l33, "0.31": l32, "0.30": l31, "0.29": l31 + ["disposition des panneaux"],
                 "0.28": l31 + ["disposition des panneaux", "profil de fabricant personnalisé", "brochage vérifié", "nomenclature"]}
    l.append((riche, True, attendues))
    return l


def reseaux_ipc(dossier):
    """(réseau, repère, broche) de la liste IPC-D-356 d'un dossier de fabrication."""
    fichiers = sorted(pathlib.Path(dossier).glob("Test/*netlist.ipc"))
    if not fichiers:
        return None
    r = set()
    for ligne in fichiers[0].read_text(encoding="utf-8", errors="replace").splitlines():
        if ligne[:3] in ("317", "327", "367"):
            r.add((ligne[3:17].strip(), ligne[20:26].strip(), ligne[27:31].strip()))
    return r


def main(argv):
    if len(argv) < 2 or argv[1] not in ("schemas", "version"):
        print(__doc__)
        return 2
    try:
        import jsonschema  # noqa: F401
    except ImportError:
        print("jsonschema absent (pip install jsonschema, ou paquet python3-jsonschema) : essai sauté")
        return 77
    if argv[1] == "schemas":
        outil, docs, donnees, travail = pathlib.Path(argv[2]), pathlib.Path(argv[3]), pathlib.Path(argv[4]), pathlib.Path(argv[5])
        ancien, version_ancienne = None, None
    else:
        outil, ancien, version_ancienne = pathlib.Path(argv[2]), pathlib.Path(argv[3]), argv[4]
        docs, donnees, travail = pathlib.Path(argv[5]), pathlib.Path(argv[6]), pathlib.Path(argv[7])
    travail.mkdir(parents=True, exist_ok=True)
    pour = validateur(docs)
    for projet, electronique, attendues in projets(outil, donnees, travail):
        valider(pour, COURANTE, projet)
        if not electronique:
            continue
        refuse(pour, VERSIONS[0], projet)
        for v in VERSIONS:
            sortie = projet.with_name(projet.name.replace(".prom", ".pour-" + v.replace(".", "-") + ".prom"))
            code, texte = lancer(outil, "pour-version", projet, sortie, v)
            if code != 0:
                echec(f"{projet.name} pour la {v} : {texte}")
                continue
            pertes = [l[len("perte : "):] for l in texte.splitlines() if l.startswith("perte : ")]
            valider(pour, v, sortie)
            manquantes = [a for a in attendues.get(v, []) if not any(a in p for p in pertes)]
            if manquantes:
                echec(f"{projet.name} pour la {v} : pertes non dites : {', '.join(manquantes)}")
            elif attendues.get(v):
                ok(f"{projet.name} pour la {v} : {len(pertes)} perte(s) dite(s), dont toutes celles attendues")
            if v != version_ancienne or not ancien:
                continue
            # Relu par la version précédente elle-même : mêmes réseaux, broche par broche
            code_n, texte_n = lancer(outil, "fabrication", projet, travail / (projet.stem + "-fab-courante"))
            code_a, texte_a = lancer(ancien, "fabrication", sortie, travail / (projet.stem + "-fab-" + v))
            if code_a not in (0, 1) or code_n not in (0, 1):
                echec(f"{projet.name} : dossier de fabrication refusé ({code_n}, {code_a}) : {texte_a[-400:]}")
                continue
            a, n = reseaux_ipc(travail / (projet.stem + "-fab-" + v)), reseaux_ipc(travail / (projet.stem + "-fab-courante"))
            if a is None or n is None:
                echec(f"{projet.name} : liste des réseaux IPC-D-356 absente")
            elif a != n:
                ecarts = sorted(a ^ n)[:8]
                echec(f"{projet.name} relu par la {v} : {len(a ^ n)} écart(s) de réseaux, par exemple {ecarts}")
            else:
                ok(f"{projet.name} écrit pour la {v}, relu par la {v} : {len({x[0] for x in n})} réseaux et {len(n)} broches identiques")
    print(f"{len(ECHECS)} échec(s)")
    return 1 if ECHECS else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
