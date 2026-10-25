#!/usr/bin/env python3
# Prométhée : essai de 19.14. Le serveur PLM simulé tourne ; le programme séparé d'accès en ligne ne s'y connecte
# pas tant que l'accès n'est pas activé (journal du serveur vide), puis, activé, dépose le dossier, le réserve et le
# restitue en révision B ; le retour arrive dans « Depuis le PLM/etats.json » du dossier d'équipe.
# Utilisation : plm_essai.py --programme chemin/promethee-en-ligne --dossier dossier-de-travail
# Licence GPL-3.0-only.
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

ICI = os.path.dirname(os.path.abspath(__file__))


def depot(equipe, numero, revision):
    d = os.path.join(equipe, "Vers le PLM", f"{numero}-{revision}")
    os.makedirs(d, exist_ok=True)
    fichiers = {
        "nomenclature.csv": "Parent;Numéro;Révision;Désignation;Genre;Fabricant;Référence fabricant;Quantité;Unité\r\n;%s;%s;Essai;produit;;;1;pièce\r\n" % (numero, revision),
        "structure.plmxml": '<?xml version="1.0" encoding="utf-8"?>\n<PLMXML xmlns="http://www.plmxml.org/Schemas/PLMXMLSchema" schemaVersion="6">\n'
                            '  <Product id="id1" productId="%s" name="Essai"/>\n  <ProductRevision id="id2" masterRef="#id1" revision="%s" name="Essai"/>\n</PLMXML>\n' % (numero, revision),
    }
    for nom, contenu in fichiers.items():
        with open(os.path.join(d, nom), "w", encoding="utf-8", newline="") as f:
            f.write(contenu)
    manifeste = {"format": "promethee-depot-plm", "version": 1, "produit": "Essai", "numero": numero, "revision": revision, "date": "2026-10-25", "auteur": "essai",
                 "fichiers": [{"nom": n, "sha256": hashlib.sha256(c.encode("utf-8")).hexdigest(), "taille": len(c.encode("utf-8"))} for n, c in fichiers.items()]}
    with open(os.path.join(d, "manifeste.json"), "w", encoding="utf-8") as f:
        json.dump(manifeste, f, ensure_ascii=False)
    return d


def reglages(chemin, port, actif, plm, base=""):
    with open(chemin, "w", encoding="utf-8") as f:
        json.dump({"format": "promethee-acces-en-ligne", "version": 1, "actif": actif, "journal": "journal-connexions.csv",
                   "fonctions": {"plm": {"actif": plm, "adaptateur": "simule", "adresse": f"http://127.0.0.1:{port}", "utilisateur": "evan", "jeton": "secret"},
                                 "distributeurs": {"actif": plm, "adaptateur": "distributeur-simule", "adresse": f"http://127.0.0.1:{port}", "jeton": "secret"},
                                 "erp": {"actif": plm, "pilote": "QSQLITE", "base": base, "requete": "SELECT mpn, distributeur, stock, delai, agree, risque, prix FROM achats"},
                                 "tickets": {"actif": plm, "adaptateur": "tickets-simule", "adresse": f"http://127.0.0.1:{port}", "jeton": "secret"}}}, f)


def lignes(chemin):
    if not os.path.exists(chemin):
        return []
    with open(chemin, encoding="utf-8") as f:
        return [l for l in f.read().splitlines() if l.strip()]


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    p = argparse.ArgumentParser()
    p.add_argument("--programme", required=True)
    p.add_argument("--dossier", required=True)
    a = p.parse_args()
    shutil.rmtree(a.dossier, ignore_errors=True)
    os.makedirs(a.dossier)
    equipe = os.path.join(a.dossier, "equipe")
    depotA = depot(equipe, "PRM-P-Essai", "A")
    depotB = depot(equipe, "PRM-P-Essai", "B")
    journalServeur = os.path.join(a.dossier, "connexions.log")
    fichierPort = os.path.join(a.dossier, "port.txt")
    serveur = subprocess.Popen([sys.executable, os.path.join(ICI, "plm_simule.py"), "--journal", journalServeur, "--fichier-port", fichierPort, "--jeton", "secret"])
    try:
        for _ in range(200):
            if os.path.exists(fichierPort) and open(fichierPort).read().strip():
                break
            time.sleep(0.05)
        port = int(open(fichierPort).read().strip())
        fichierReglages = os.path.join(a.dossier, "acces-en-ligne.json")
        env = dict(os.environ, PROMETHEE_ACCES_EN_LIGNE=fichierReglages)

        def lancer(*args):
            # Le programme écrit en UTF-8, quelle que soit la page de code du poste (Windows)
            r = subprocess.run([a.programme, "--reglages", fichierReglages, *args], capture_output=True, encoding="utf-8", errors="replace", env=env, timeout=60)
            return r.returncode, r.stdout + r.stderr

        echecs = []
        # Sans activation : refus, aucune connexion relevée
        for actif, plm in ((False, True), (True, False), (False, False)):
            reglages(fichierReglages, port, actif, plm)
            for args in (("plm", "deposer", depotA), ("plm", "reserver", "PRM-P-Essai"), ("plm", "restituer", depotB), ("plm", "consulter", "PRM-P-Essai")):
                code, sortie = lancer(*args)
                if code != 3 or "Rien n'est parti" not in sortie:
                    echecs.append(f"désactivé ({actif}, {plm}) {args[1]} : code {code}, {sortie.strip()}")
        if lignes(journalServeur):
            echecs.append(f"connexions relevées sans activation : {lignes(journalServeur)}")
        # Activé : dépôt, réservation, restitution en révision B
        reglages(fichierReglages, port, True, True)
        for args, attendu in ((("plm", "deposer", depotA), "déposé"), (("plm", "reserver", "PRM-P-Essai"), "réservé"), (("plm", "restituer", depotB), "restitué"),
                              (("plm", "consulter", "PRM-P-Essai"), "révision B")):
            code, sortie = lancer("--equipe", equipe, *args)
            if code != 0 or attendu not in sortie:
                echecs.append(f"activé {args[1]} : code {code}, {sortie.strip()}")
        recu = lignes(journalServeur)
        if len(recu) != 4:
            echecs.append(f"le serveur a reçu {len(recu)} connexion(s) au lieu de 4 : {recu}")
        journal = lignes(os.path.join(a.dossier, "journal-connexions.csv"))
        if len(journal) != 5:  # en-tête et quatre connexions
            echecs.append(f"journal des connexions : {len(journal)} ligne(s)")
        etats = json.load(open(os.path.join(equipe, "Depuis le PLM", "etats.json"), encoding="utf-8"))["etats"]
        e = [x for x in etats if x["numero"] == "PRM-P-Essai"]
        if not e or e[0]["revision"] != "B" or e[0]["etat"] != "restitué":
            echecs.append(f"état du retour : {e}")
        # Distributeurs (stock, prix, fin de vie) et ERP par Qt SQL : sous « Depuis le PLM/donnees-achat.csv »
        base = os.path.join(a.dossier, "erp.sqlite")
        import sqlite3
        with sqlite3.connect(base) as c:
            c.execute("CREATE TABLE achats (mpn TEXT, distributeur TEXT, stock INTEGER, delai INTEGER, agree TEXT, risque TEXT, prix REAL)")
            c.execute("INSERT INTO achats VALUES ('RC0603FR-0710KL', 'Stock interne', 4000, 0, 'oui', 'faible', 0.0018)")
        reglages(fichierReglages, port, False, False, base)
        code, sortie = lancer("--equipe", equipe, "distributeurs", "offres", "RC0603FR-0710KL")
        if code != 3:
            echecs.append(f"distributeurs désactivés : code {code}, {sortie.strip()}")
        code, sortie = lancer("--equipe", equipe, "erp", "lire")
        if code != 3:
            echecs.append(f"ERP désactivé : code {code}, {sortie.strip()}")
        avant = len(lignes(journalServeur))
        reglages(fichierReglages, port, True, True, base)
        code, sortie = lancer("--equipe", equipe, "distributeurs", "offres", "RC0603FR-0710KL")
        achats = lignes(os.path.join(equipe, "Depuis le PLM", "donnees-achat.csv"))
        if code != 0 or len(achats) != 3 or len(lignes(journalServeur)) != avant + 1:
            echecs.append(f"distributeurs : code {code}, {achats}, {sortie.strip()}")
        code, sortie = lancer("--equipe", equipe, "erp", "lire")
        achats = lignes(os.path.join(equipe, "Depuis le PLM", "donnees-achat.csv"))
        if (code == 2 and "sans Qt SQL" in sortie) or (code == 4 and "Driver not loaded" in sortie):
            print("ERP ignoré : Qt SQL ou son pilote SQLite absent")
        elif code != 0 or len(achats) != 2 or "Stock interne" not in achats[1]:
            echecs.append(f"ERP : code {code}, {achats}, {sortie.strip()}")
        # Tickets dans les deux sens : créés chez le gestionnaire, un clos là-bas revient clos
        os.makedirs(os.path.join(equipe, "Vers le PLM"), exist_ok=True)
        with open(os.path.join(equipe, "Vers le PLM", "tickets.json"), "w", encoding="utf-8") as f:
            json.dump({"format": "promethee-tickets", "version": 1, "tickets": [{"id": "revue-1-c1", "titre": "R3 : valeur à revoir", "etat": "ouvert"},
                                                                                {"id": "revue-1-c2", "titre": "U1 : découplage manquant", "etat": "ouvert"}]}, f)
        reglages(fichierReglages, port, False, False, base)
        code, sortie = lancer("--equipe", equipe, "tickets", "synchroniser")
        if code != 3:
            echecs.append(f"tickets désactivés : code {code}, {sortie.strip()}")
        reglages(fichierReglages, port, True, True, base)
        code, sortie = lancer("--equipe", equipe, "tickets", "synchroniser")
        if code != 0 or "2 ticket(s) créé(s)" not in sortie:
            echecs.append(f"tickets : code {code}, {sortie.strip()}")
        import urllib.request
        urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{port}/api/tickets/T-2/clore", data=b"{}", method="POST", headers={"Authorization": "Bearer secret"})).read()
        code, sortie = lancer("--equipe", equipe, "tickets", "synchroniser")
        depuis = json.load(open(os.path.join(equipe, "Depuis le PLM", "tickets.json"), encoding="utf-8"))["tickets"]
        etats = {t["id"]: t["etat"] for t in depuis}
        locaux = json.load(open(os.path.join(equipe, "Vers le PLM", "tickets.json"), encoding="utf-8"))["tickets"]
        if code != 0 or "0 ticket(s) créé(s)" not in sortie or etats != {"revue-1-c1": "ouvert", "revue-1-c2": "clos"} or [t["distant"] for t in locaux] != ["T-1", "T-2"]:
            echecs.append(f"tickets relus : code {code}, {etats}, {sortie.strip()}")
        # Dépôt altéré : refusé avant toute connexion
        with open(os.path.join(depotA, "nomenclature.csv"), "a", encoding="utf-8") as f:
            f.write("ajout\r\n")
        code, sortie = lancer("plm", "deposer", depotA)
        if code != 5:
            echecs.append(f"dépôt altéré : code {code}, {sortie.strip()}")
        if len(lignes(journalServeur)) != 10:  # 4 PLM, 1 distributeur, 3 + 1 tickets, 1 clôture faite par l'essai
            echecs.append(f"un dépôt altéré est parti ({len(lignes(journalServeur))} connexions)")
    finally:
        serveur.terminate()
        serveur.wait(timeout=10)
    for x in echecs:
        print("ÉCHEC", x)
    print("OK" if not echecs else f"{len(echecs)} échec(s)")
    return 0 if not echecs else 1


if __name__ == "__main__":
    sys.exit(main())
