#!/usr/bin/env python3
# Prométhée : serveur PLM simulé pour les essais du lot L19 (19.14). Il tient des articles en mémoire : dépôt
# (révision A), réservation (check-out), restitution (check-in, révision suivante sans I, O, Q, S, X, Z) et
# consultation, et offres d'un distributeur ; chaque connexion reçue s'inscrit au journal, qui montre aussi qu'aucune n'a eu lieu.
# Utilisation : plm_simule.py --journal connexions.log --fichier-port port.txt [--port 0] [--jeton secret]
# Licence GPL-3.0-only.
import argparse
import json
import re
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

REVISIONS = "ABCDEFGHJKLMNPRTUVWY"
articles = {}
tickets = []
verrou = threading.Lock()


def suivante(r):
    i = REVISIONS.find(r)
    return REVISIONS[i + 1] if 0 <= i < len(REVISIONS) - 1 else r + "A"


class Gestionnaire(BaseHTTPRequestHandler):
    jeton = ""
    journal = ""

    def log_message(self, format, *args):  # le journal tient lieu de trace
        pass

    def noter(self):
        with verrou, open(self.journal, "a", encoding="utf-8") as f:
            f.write(f"{self.command} {self.path}\n")

    def repondre(self, statut, corps):
        donnees = json.dumps(corps, ensure_ascii=False).encode("utf-8")
        self.send_response(statut)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(donnees)))
        self.end_headers()
        self.wfile.write(donnees)

    def corps(self):
        n = int(self.headers.get("Content-Length") or 0)
        try:
            return json.loads(self.rfile.read(n).decode("utf-8")) if n else {}
        except ValueError:
            return None

    def autorise(self):
        return not self.jeton or self.headers.get("Authorization") == "Bearer " + self.jeton

    def do_GET(self):
        self.noter()
        if not self.autorise():
            return self.repondre(401, {"erreur": "jeton refusé"})
        if self.path == "/api/tickets":
            with verrou:
                return self.repondre(200, {"tickets": tickets})
        o = re.fullmatch(r"/api/offres/([^/]+)", self.path)
        if o:  # distributeur simulé : deux offres par référence, l'une en fin de vie
            return self.repondre(200, {"mpn": o.group(1), "offres": [
                {"distributeur": "Mouser", "stock": 12000, "delai": 3, "agree": True, "risque": "faible", "prix": 0.0021},
                {"distributeur": "Courtier", "stock": 500, "delai": 10, "agree": False, "risque": "élevé", "prix": 0.0015}]})
        m = re.fullmatch(r"/api/articles/([^/]+)", self.path)
        if not m:
            return self.repondre(404, {"erreur": "inconnu"})
        with verrou:
            a = articles.get(m.group(1))
        return self.repondre(200, a) if a else self.repondre(404, {"erreur": "article inconnu"})

    def do_POST(self):
        self.noter()
        if not self.autorise():
            return self.repondre(401, {"erreur": "jeton refusé"})
        c = self.corps()
        if c is None:
            return self.repondre(400, {"erreur": "corps illisible"})
        with verrou:
            if self.path == "/api/tickets":
                t = {"id": "T-%d" % (len(tickets) + 1), "reference": c.get("reference", ""), "titre": c.get("titre", ""), "etat": "ouvert"}
                tickets.append(t)
                return self.repondre(201, t)
            m = re.fullmatch(r"/api/tickets/([^/]+)/clore", self.path)
            if m:
                for t in tickets:
                    if t["id"] == m.group(1):
                        t["etat"] = "clos"
                        return self.repondre(200, t)
                return self.repondre(404, {"erreur": "ticket inconnu"})
            if self.path == "/api/articles":
                n = c.get("numero", "")
                if not n:
                    return self.repondre(400, {"erreur": "numéro manquant"})
                if n in articles:
                    return self.repondre(409, {"erreur": "déjà déposé", **articles[n]})
                articles[n] = {"numero": n, "revision": c.get("revision") or "A", "designation": c.get("designation", ""), "etat": "déposé", "reservePar": "",
                               "fichiers": [f.get("nom") for f in c.get("fichiers", [])]}
                return self.repondre(201, articles[n])
            m = re.fullmatch(r"/api/articles/([^/]+)/(reserver|restituer)", self.path)
            if not m or m.group(1) not in articles:
                return self.repondre(404, {"erreur": "article inconnu"})
            a = articles[m.group(1)]
            if m.group(2) == "reserver":
                if a["etat"] == "réservé":
                    return self.repondre(409, {"erreur": "déjà réservé par " + a["reservePar"], **a})
                a["etat"], a["reservePar"] = "réservé", c.get("utilisateur", "")
                return self.repondre(200, a)
            if a["etat"] != "réservé":
                return self.repondre(409, {"erreur": "restitution sans réservation", **a})
            a["revision"], a["etat"], a["reservePar"] = suivante(a["revision"]), "restitué", ""
            a["fichiers"] = [f.get("nom") for f in c.get("fichiers", [])] or a["fichiers"]
            return self.repondre(200, a)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=0)
    p.add_argument("--journal", required=True)
    p.add_argument("--fichier-port", required=True)
    p.add_argument("--jeton", default="")
    a = p.parse_args()
    Gestionnaire.jeton = a.jeton
    Gestionnaire.journal = a.journal
    open(a.journal, "a", encoding="utf-8").close()
    serveur = ThreadingHTTPServer(("127.0.0.1", a.port), Gestionnaire)
    with open(a.fichier_port, "w", encoding="utf-8") as f:
        f.write(str(serveur.server_address[1]))
    try:
        serveur.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
