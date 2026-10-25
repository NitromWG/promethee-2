#!/usr/bin/env python3
# Prométhée : vérification indépendante d'un dossier de fabrication (matrice FAB-01, audit E-14).
# Licence GPL-3.0-only.
#
# Relit les fichiers Gerber RS-274X, les perçages Excellon et les fichiers CSV avec un analyseur écrit ici, sans
# rien partager avec le code C++ qui les a produits, puis contrôle :
#   - le format (4.6, millimètres, zéros de tête omis, coordonnées absolues) et la fin de chaque fichier ;
#   - les fonctions X2 des couches et la cohérence du nombre de couches de cuivre avec les perçages ;
#   - le contour : fermé, d'aire non nulle ; toute la géométrie de cuivre, de vernis, de pâte et tous les trous
#     sont dans la carte (même origine, mêmes unités) ;
#   - chaque trou métallisé tombe sur une pastille des deux faces, plus grande que le trou, du même réseau ;
#   - chaque ouverture de pâte et de vernis correspond à une pastille de cuivre de la même face ;
#   - placement et nomenclature : positions dans la carte, faces et angles valides, références cohérentes ;
#   - la liste de réseaux IPC-D-356A colonne par colonne (gabarit de la norme, codes d'accès et de vernis), puis
#     chaque ligne contre les fichiers : trou de même diamètre au même endroit, pastille de même repère et de même
#     réseau sur la couche annoncée (audit Q4) ;
#   - le fichier de travail gbrjob champ par champ contre la norme du Gerber Job Format : fichiers listés avec leur
#     vraie fonction et leur polarité, dimensions, nombre de couches, finition, couleurs, empilage complet.
# Simple face : une seule couche de cuivre (L1, dessous), aucun trou métallisé.
# Code de sortie : 0 conforme, 1 défauts trouvés, 2 dossier illisible.
import argparse
import csv
import io
import json
import math
import re
import sys
from pathlib import Path

TOL = 0.001  # mm : 1 µm


class Defauts:
    def __init__(self):
        self.liste = []

    def __call__(self, texte):
        self.liste.append(texte)


# ---------------------------------------------------------------------------------------------------------
# Gerber
class Ouverture:
    def __init__(self, forme, params):
        self.forme = forme  # C, R, O, ou M (macro : rectangle arrondi, dimensions calculées)
        self.params = params

    def dimensions(self):
        if self.forme == "C":
            return self.params[0], self.params[0]
        return self.params[0], self.params[1]


    def plus_petit_cote(self):
        return min(self.dimensions())


def dimensions_roundrect(params):
    """Macro RoundRect (rayon, puis les centres des quatre arrondis) : côtés du rectangle, arrondis compris."""
    r = params[0]
    c = [(params[1 + 2 * i], params[2 + 2 * i]) for i in range(4)]
    a = math.hypot(c[1][0] - c[0][0], c[1][1] - c[0][1]) + 2 * r
    b = math.hypot(c[2][0] - c[1][0], c[2][1] - c[1][1]) + 2 * r
    return a, b


class Gerber:
    def __init__(self, chemin, defaut):
        self.chemin = chemin
        self.nom = chemin.name
        self.fonction = None
        self.format_ok = False
        self.unite = None
        self.termine = False
        self.polarite = None
        self.ouvertures = {}
        self.flashes = []   # (x, y, ouverture, réseau, (repère, broche) ou None)
        self.traits = []    # (x0, y0, x1, y1, ouverture, réseau)
        self.regions = []   # ([(x, y)...], réseau, (repère, broche) ou None)
        self._lire(chemin.read_text(encoding="utf-8"), defaut)

    def _commandes(self, texte):
        i, n = 0, len(texte)
        while i < n:
            c = texte[i]
            if c in "\r\n ":
                i += 1
                continue
            if c == "%":
                fin = texte.index("%", i + 1)
                morceaux = [m.strip() for m in texte[i + 1:fin].split("*") if m.strip()]
                if morceaux and morceaux[0].startswith("AM"):
                    # Définition de macro : son nom, ses primitives (lignes « 0 » de commentaire comprises)
                    yield True, ("AM", morceaux[0][2:], morceaux[1:])
                else:
                    for morceau in morceaux:
                        yield True, morceau
                i = fin + 1
            else:
                fin = texte.index("*", i)
                yield False, texte[i:fin].strip()
                i = fin + 1

    def _coord(self, texte, lettre, courant):
        m = re.search(lettre + r"([+-]?\d+)", texte)
        if not m:
            return courant
        return int(m.group(1)) / 1e6  # format 4.6 : entier en millionièmes de millimètre

    def _lire(self, texte, defaut):
        x = y = 0.0
        ouverture = None
        reseau = ""
        broche = None
        region = None
        contour = []
        mode_lineaire = True
        mode_arc = None  # "G02" (horaire) ou "G03" (antihoraire)
        quadrant_multiple = False
        self.macros = {}
        for etendue, cmd in self._commandes(texte):
            if etendue and isinstance(cmd, tuple):
                _, nom, primitives = cmd
                self.macros[nom] = primitives
                continue
            if etendue:
                if cmd.startswith("FS"):
                    self.format_ok = cmd == "FSLAX46Y46"
                    if not self.format_ok:
                        defaut(f"{self.nom} : format de coordonnées inattendu « {cmd} »")
                elif cmd.startswith("MO"):
                    self.unite = cmd[2:]
                elif cmd.startswith("TF.FileFunction,"):
                    self.fonction = cmd[len("TF.FileFunction,"):]
                elif cmd.startswith("TF.FilePolarity,"):
                    self.polarite = cmd[len("TF.FilePolarity,"):]
                elif cmd.startswith("TO.N,"):
                    reseau = cmd[5:]
                elif cmd.startswith("TO.P,"):
                    champs = cmd[5:].split(",")
                    if len(champs) < 2:
                        defaut(f"{self.nom} : attribut %TO.P incomplet « {cmd} »")
                    broche = (champs[0], champs[1]) if len(champs) >= 2 else None
                elif cmd.startswith("TD"):
                    # %TD* retire tous les attributs d'objet, %TD.N* et %TD.P* un seul
                    if cmd in ("TD", "TD.N"):
                        reseau = ""
                    if cmd in ("TD", "TD.P"):
                        broche = None
                elif cmd.startswith("AD"):
                    mm_ = re.fullmatch(r"ADD(\d+)([A-Za-z_][A-Za-z0-9_]+),([-\d.X]+)", cmd)
                    if mm_ and mm_.group(2) not in ("C", "R", "O"):
                        if mm_.group(2) not in self.macros:
                            defaut(f"{self.nom} : ouverture D{mm_.group(1)} d'une macro inconnue « {mm_.group(2)} »")
                            continue
                        params = [float(v) for v in mm_.group(3).split("X")]
                        if mm_.group(2) == "RoundRect" and len(params) == 9:
                            self.ouvertures[int(mm_.group(1))] = Ouverture("M", list(dimensions_roundrect(params)))
                        else:
                            defaut(f"{self.nom} : macro « {mm_.group(2)} » aux paramètres inattendus")
                        continue
                    m = re.fullmatch(r"ADD(\d+)([CRO]),([\d.]+)(?:X([\d.]+))?", cmd)
                    if not m:
                        defaut(f"{self.nom} : ouverture illisible « {cmd} »")
                        continue
                    params = [float(m.group(3))] + ([float(m.group(4))] if m.group(4) else [])
                    if m.group(2) != "C" and len(params) != 2:
                        defaut(f"{self.nom} : ouverture {m.group(2)} sans hauteur")
                        continue
                    self.ouvertures[int(m.group(1))] = Ouverture(m.group(2), params)
                elif cmd.startswith("LP"):
                    if cmd != "LPD":
                        defaut(f"{self.nom} : polarité de tracé « {cmd} » inattendue")
                continue
            if not cmd:
                continue
            if cmd.startswith("G04"):
                continue
            if cmd == "M02":
                self.termine = True
                break
            if cmd == "G01":
                mode_lineaire = True
                mode_arc = None
                continue
            if cmd == "G75":
                quadrant_multiple = True
                continue
            if cmd == "G74":
                defaut(f"{self.nom} : G74 (quadrant unique) est retiré de la norme")
                continue
            if cmd in ("G02", "G03"):
                mode_lineaire, mode_arc = False, cmd
                continue
            ma = re.fullmatch(r"(G0[23])(X[+-]?\d+)?(Y[+-]?\d+)?I([+-]?\d+)J([+-]?\d+)D01", cmd)
            if ma:
                # Arc (8.4) : du point courant au point donné, centre en I, J relatif au départ ; découpé en cordes
                if not quadrant_multiple:
                    defaut(f"{self.nom} : arc sans G75")
                nx = self._coord(cmd, "X", x)
                ny = self._coord(cmd, "Y", y)
                cx, cy = x + int(ma.group(4)) / 1e6, y + int(ma.group(5)) / 1e6
                r0, r1 = math.hypot(x - cx, y - cy), math.hypot(nx - cx, ny - cy)
                if abs(r0 - r1) > 0.002:
                    defaut(f"{self.nom} : arc incohérent (rayons {r0:.4f} et {r1:.4f} mm)")
                t0, t1 = math.atan2(y - cy, x - cx), math.atan2(ny - cy, nx - cx)
                horaire = ma.group(1) == "G02"
                balayage = (t0 - t1) % (2 * math.pi) if horaire else (t1 - t0) % (2 * math.pi)
                if balayage == 0:
                    balayage = 2 * math.pi
                pas = max(2, int(balayage / 0.1) + 1)
                px, py = x, y
                for k in range(1, pas + 1):
                    t = t0 - balayage * k / pas if horaire else t0 + balayage * k / pas
                    qx, qy = (nx, ny) if k == pas else (cx + r0 * math.cos(t), cy + r0 * math.sin(t))
                    if region is not None:
                        if not contour:
                            contour = [(px, py)]
                        contour.append((qx, qy))
                    elif ouverture is not None:
                        self.traits.append((px, py, qx, qy, ouverture, reseau))
                    px, py = qx, qy
                x, y = nx, ny
                mode_lineaire = True
                continue
            if cmd == "G36":
                region, contour = [], []
                continue
            if cmd == "G37":
                if contour:
                    region.append(contour)
                for c in region or []:
                    if len(c) >= 4:
                        self.regions.append((c, reseau, broche))
                region = None
                continue
            m = re.fullmatch(r"(?:G54)?D(\d+)", cmd)
            if m and int(m.group(1)) >= 10:
                n = int(m.group(1))
                if n not in self.ouvertures:
                    defaut(f"{self.nom} : ouverture D{n} utilisée sans être définie")
                ouverture = self.ouvertures.get(n)
                continue
            m = re.fullmatch(r"(X[+-]?\d+)?(Y[+-]?\d+)?D0([123])", cmd)
            if not m:
                defaut(f"{self.nom} : commande inconnue « {cmd} »")
                continue
            nx = self._coord(cmd, "X", x)
            ny = self._coord(cmd, "Y", y)
            op = m.group(3)
            if region is not None:
                if op == "2":
                    if contour:
                        region.append(contour)
                    contour = [(nx, ny)]
                elif op == "1":
                    if not contour:
                        contour = [(x, y)]
                    contour.append((nx, ny))
                else:
                    defaut(f"{self.nom} : éclair dans une région")
            else:
                if op == "1":
                    if ouverture is None:
                        defaut(f"{self.nom} : tracé sans ouverture choisie")
                    elif mode_lineaire:
                        self.traits.append((x, y, nx, ny, ouverture, reseau))
                elif op == "3":
                    if ouverture is None:
                        defaut(f"{self.nom} : éclair sans ouverture choisie")
                    else:
                        self.flashes.append((nx, ny, ouverture, reseau, broche))
            x, y = nx, ny
        if not self.format_ok:
            defaut(f"{self.nom} : déclaration de format %FSLAX46Y46*% absente")
        if self.unite != "MM":
            defaut(f"{self.nom} : unité « {self.unite} » au lieu de MM")
        if not self.termine:
            defaut(f"{self.nom} : fichier non terminé (M02 absent)")
        if not self.fonction:
            defaut(f"{self.nom} : fonction de couche X2 (%TF.FileFunction) absente")

    def points(self):
        for x, y, *_ in self.flashes:
            yield x, y
        for x0, y0, x1, y1, _o, _r in self.traits:
            yield x0, y0
            yield x1, y1
        for c, *_ in self.regions:
            yield from c


# ---------------------------------------------------------------------------------------------------------
# Excellon
class Excellon:
    """Perçage Excellon. strict : le format du dossier de fabrication (M48, METRIC, décimales, fonction X2) ;
    sinon toutes les variantes que Prométhée sait écrire (4.27) : INCH, zéros de tête gardés (LZ, entiers 3.3 en
    millimètres, 2.4 en pouces), ruban EIA sans en-tête, XNC, sans attributs. Les cotes sont rendues en mm."""

    def __init__(self, chemin, defaut, strict=True):
        self.nom = chemin.name
        self.fonction = None
        self.outils = {}
        self.trous = []  # (x, y, diamètre)
        self.fentes = []  # (x0, y0, x1, y1, diamètre)
        depart, plonge = None, False
        texte = chemin.read_text(encoding="utf-8")
        lignes = [l.strip() for l in texte.splitlines()]
        eia = not lignes or lignes[0] != "M48"
        if eia and strict:
            defaut(f"{self.nom} : en-tête M48 absent")
        unites = [l.split(",") for l in lignes if l.split(",")[0] in ("METRIC", "INCH")]
        pouces = bool(unites) and unites[0][0] == "INCH"
        zeros_tete = bool(unites) and "LZ" in unites[0][1:]
        if strict and (not unites or pouces):
            defaut(f"{self.nom} : unité METRIC absente")
        if "M30" not in lignes:
            defaut(f"{self.nom} : fin M30 absente")
        echelle = 25.4 if pouces else 1.0
        entiers = 2 if pouces else 3

        def valeur(v):
            # Point décimal : la valeur telle quelle ; sans point (LZ) : entiers fixes, les décimales suivent
            if "." in v:
                return float(v) * echelle
            if not zeros_tete:
                defaut(f"{self.nom} : coordonnée sans point décimal « {v} » hors du format LZ")
                return float(v) * echelle
            signe = -1 if v.startswith("-") else 1
            chiffres = v.lstrip("+-")
            return signe * int(chiffres) / 10 ** (len(chiffres) - entiers) * echelle

        NB = r"([+-]?[\d.]+)"
        entete = not eia
        outil = None
        for l in lignes:
            if l.startswith("; #@! TF.FileFunction,"):
                self.fonction = l.split("TF.FileFunction,", 1)[1]
            if not l or l.startswith(";"):
                continue
            if l == "%":
                entete = False
                continue
            m = re.fullmatch(r"T(\d+)C([\d.]+)", l)
            if m and (entete or eia):
                self.outils[int(m.group(1))] = float(m.group(2)) * echelle
                continue
            if entete:
                continue
            m = re.fullmatch(r"T(\d+)", l)
            if m:
                outil = int(m.group(1))
                if outil != 0 and outil not in self.outils:
                    defaut(f"{self.nom} : outil T{outil} non défini")
                continue
            m = re.fullmatch(f"X{NB}Y{NB}G85X{NB}Y{NB}", l)
            if m:
                # Fente en G85 (4.13) : ses deux bouts ; le trou est compté au milieu
                x0, y0, x1, y1 = (valeur(v) for v in m.groups())
                self.fentes.append((x0, y0, x1, y1, self.outils.get(outil, 0)))
                self.trous.append(((x0 + x1) / 2, (y0 + y1) / 2, self.outils.get(outil, 0)))
                continue
            m = re.fullmatch(f"G0([01])X{NB}Y{NB}", l)
            if m:
                # Fente détourée (4.13) : G00 au départ, M15 (plongée), G01 à l'arrivée, M16 (remontée)
                if m.group(1) == "0":
                    depart = (valeur(m.group(2)), valeur(m.group(3)))
                elif depart is None or not plonge:
                    defaut(f"{self.nom} : G01 sans G00 ni M15 « {l} »")
                else:
                    x1, y1 = valeur(m.group(2)), valeur(m.group(3))
                    self.fentes.append((depart[0], depart[1], x1, y1, self.outils.get(outil, 0)))
                    self.trous.append(((depart[0] + x1) / 2, (depart[1] + y1) / 2, self.outils.get(outil, 0)))
                continue
            if l == "M15":
                plonge = True
                continue
            if l == "M16":
                plonge, depart = False, None
                continue
            m = re.fullmatch(f"X{NB}Y{NB}", l)
            if m:
                if strict and ("." not in m.group(1) or "." not in m.group(2)):
                    defaut(f"{self.nom} : coordonnée sans point décimal « {l} »")
                if outil in self.outils:
                    self.trous.append((valeur(m.group(1)), valeur(m.group(2)), self.outils[outil]))
                else:
                    defaut(f"{self.nom} : trou sans outil « {l} »")
                continue
            if l in ("G90", "G05", "M30") or (not strict and l.startswith("FMAT")):
                continue
            defaut(f"{self.nom} : ligne inconnue « {l} »")
        if self.fonction is None and strict:
            defaut(f"{self.nom} : fonction X2 (TF.FileFunction) absente")


def comparer_jeux_excellon(dossier, defaut):
    """4.27 : chaque fichier du dossier, écrit avec un jeu d'options différent, relu ici ; tous doivent donner les
    mêmes trous, les mêmes fentes et les mêmes diamètres que le premier, à la résolution de leur format près."""
    fichiers = sorted(dossier.glob("*.drl"))
    if len(fichiers) < 2:
        defaut(f"{dossier} : moins de deux fichiers de perçage à comparer")
        return {}
    lus = [(f, Excellon(f, defaut, strict=False)) for f in fichiers]

    def cle(t):
        return tuple(round(v, 2) for v in t)

    ref_f, ref = lus[0]
    if not ref.trous:
        defaut(f"{ref_f.name} : aucun trou")
    resume = {"jeux": len(lus), "trous": len(ref.trous), "fentes": len(ref.fentes)}
    for f, e in lus[1:]:
        texte = f.read_text(encoding="utf-8")
        # Résolution du format : 0,0001 pouce (2,54 µm) ou 1 µm ; la tolérance en prend la moitié, plus 1 µm
        tol = (0.00254 if "INCH" in texte else 0.001) / 2 + 0.001
        for genre, a, b in (("trous", ref.trous, e.trous), ("fentes", ref.fentes, e.fentes)):
            if len(a) != len(b):
                defaut(f"{f.name} : {len(b)} {genre} au lieu de {len(a)}")
                continue
            for x, y in zip(sorted(a, key=cle), sorted(b, key=cle)):
                if any(abs(u - v) > tol for u, v in zip(x, y)):
                    defaut(f"{f.name} : {genre[:-1]} {tuple(round(v, 4) for v in y)} au lieu de {tuple(round(v, 4) for v in x)}")
                    break
    return resume


# ---------------------------------------------------------------------------------------------------------
# Géométrie
def dans_polygone(p, poly):
    x, y = p
    dedans = False
    n = len(poly)
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        if (y0 > y) != (y1 > y):
            xi = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            if xi > x:
                dedans = not dedans
    return dedans


def distance_segment(p, a, b):
    (px, py), (ax, ay), (bx, by) = p, a, b
    dx, dy = bx - ax, by - ay
    l2 = dx * dx + dy * dy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / l2))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def distance_bord(p, poly):
    return min(distance_segment(p, poly[i], poly[(i + 1) % len(poly)]) for i in range(len(poly)))


def dans_carte(p, contour, decoupes=()):
    """Dans la matière de la carte : dans le contour et hors des découpes, bords compris."""
    if not (dans_polygone(p, contour) or distance_bord(p, contour) <= TOL):
        return False
    return not any(dans_polygone(p, d) and distance_bord(p, d) > TOL for d in decoupes)


def aire(poly):
    return 0.5 * sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly)))


def chainer_contour(traits, defaut, nom):
    """Assemble les segments du profil en boucles fermées (dans l'ordre d'écriture) : le contour, la plus grande, et
    les découpes internes, chacune dedans. Rend (contour, découpes)."""
    if not traits:
        defaut(f"{nom} : contour vide")
        return [], []
    boucles, boucle = [], None
    for i, (x0, y0, x1, y1, _o, _r) in enumerate(traits):
        if boucle is None:
            boucle = [(x0, y0)]
        elif math.hypot(x0 - boucle[-1][0], y0 - boucle[-1][1]) > TOL:
            defaut(f"{nom} : contour interrompu au segment {i + 1}")
            return [], []
        boucle.append((x1, y1))
        if len(boucle) > 3 and math.hypot(boucle[-1][0] - boucle[0][0], boucle[-1][1] - boucle[0][1]) <= TOL:
            boucles.append(boucle[:-1])
            boucle = None
    if boucle is not None:
        defaut(f"{nom} : contour non fermé")
        return [], []
    contour = max(boucles, key=lambda b: abs(aire(b)))
    decoupes = [b for b in boucles if b is not contour]
    for d in decoupes:
        if not all(dans_polygone(q, contour) for q in d):
            defaut(f"{nom} : boucle de profil hors du contour de la carte")
    return contour, decoupes


# ---------------------------------------------------------------------------------------------------------
# IPC-D-356A : enregistrements 317 (traversant) et 327 (CMS) au gabarit de la norme, colonnes 1 à 74 :
# code (1-3), réseau (4-17), blancs (18-20), repère (21-26), tiret (27), broche (28-31), milieu M (32),
# perçage D + diamètre + P ou U (33-38), accès A + couche (39-41), X (42-49), Y (50-57), largeur X (58-62),
# hauteur Y (63-67), rotation R (68-71), blanc (72), vernis S (73-74) : faces où le vernis couvre l'objet.
GABARIT_IPC = re.compile(
    r"(?P<code>3[12]7)(?P<reseau>.{14})   (?P<ref>.{6})-(?P<broche>.{4})(?P<milieu>[ M])(?P<percage>D\d{4}[PU]| {6})"
    r"(?P<acces>A\d\d)X(?P<x>[+-]\d{6})Y(?P<y>[+-]\d{6})X(?P<l>\d{4})Y(?P<h>\d{4})R(?P<rot>\d{3}) S(?P<vernis>[0-3]) {0,6}")


def nom_ascii(nom):
    """Nom de réseau ramené à ce qu'en gardent les deux formats : ASCII imprimable, sans séparateur Gerber."""
    return "".join(chr(b) if 32 <= b <= 126 and chr(b) not in ",*%" else "_" for b in nom.encode("utf-8"))


def verifier_ipc(dossier, contour, decoupes, gerbers, cuivres, n, perces, defaut):
    fichiers = sorted((dossier / "Test").glob("*.ipc"))
    if len(fichiers) != 1:
        defaut(f"liste de réseaux IPC-D-356 : {len(fichiers)} fichier(s) dans Test/ au lieu d'un")
        return {}
    f = fichiers[0]
    brut = f.read_bytes()
    if any(b > 126 or (b < 32 and b not in (10, 13)) for b in brut):
        defaut(f"{f.name} : caractères hors de l'ASCII imprimable")
    lignes = brut.decode("ascii", "replace").splitlines()
    alias, plafonnes, enregistrements = {}, set(), []
    unites, fin = None, False
    for i, l in enumerate(lignes, 1):
        if fin:
            if l.strip():
                defaut(f"{f.name}, ligne {i} : texte après la fin « 999 »")
            continue
        if len(l) > 80:
            defaut(f"{f.name}, ligne {i} : {len(l)} caractères (80 au plus)")
        if l.startswith("C"):
            m = re.match(r"C  (.+?) : \d+ um, plafonne a 9999 um", l)
            if m:
                plafonnes.add(m.group(1))
        elif l.startswith("P  "):
            m = re.fullmatch(r"P  UNITS CUST (\d)", l.rstrip())
            if m:
                unites = m.group(1)
            m = re.fullmatch(r"P  (NNAME\d+)\s+(.+)", l.rstrip())
            if m:
                alias[m.group(1)] = m.group(2)
        elif l.rstrip() == "999":
            fin = True
        elif l[:3] in ("317", "327"):
            enregistrements.append((i, l))
        elif l.strip():
            defaut(f"{f.name}, ligne {i} : enregistrement inconnu « {l[:24]} »")
    if not fin:
        defaut(f"{f.name} : fin « 999 » absente")
    if unites != "1":
        defaut(f"{f.name} : unités « P  UNITS CUST 1 » (millimètres au micromètre) absentes")
        return {}
    trous = {"P": [t for e in perces.values() if e.fonction and e.fonction.startswith("Plated") for t in e.trous],
             "U": [t for e in perces.values() if e.fonction and e.fonction.startswith("NonPlated") for t in e.trous]}
    masques = {"Top": gerbers.get("Soldermask,Top"), "Bot": gerbers.get("Soldermask,Bot")}

    def ouverture(g, x, y):
        if g is None:
            return False
        return any(math.hypot(fx - x, fy - y) <= TOL for fx, fy, *_ in g.flashes) or any(dans_polygone((x, y), poly) for poly, *_ in g.regions)

    pastilles = vias = 0
    vus, sondes = set(), []
    for i, l in enregistrements:
        m = GABARIT_IPC.fullmatch(l)
        if not m:
            defaut(f"{f.name}, ligne {i} : hors du gabarit de la norme : « {l} »")
            continue
        traversant = m["code"] == "317"
        reseau = m["reseau"].rstrip()
        reseau = alias.get(reseau, reseau)
        reseau = "" if reseau == "N/C" else reseau
        ref, broche = m["ref"].rstrip(), m["broche"].rstrip()
        x, y = int(m["x"]) / 1000, int(m["y"]) / 1000
        larg, haut = int(m["l"]) / 1000, int(m["h"]) / 1000
        acces, code_vernis = int(m["acces"][1:]), int(m["vernis"])
        via = ref == "VIA"
        lieu = f"{f.name}, ligne {i} ({ref}-{broche})"
        if int(m["rot"]) >= 360:
            defaut(f"{lieu} : rotation {m['rot']} hors de [0 ; 360[")
        if traversant != (m["percage"].strip() != ""):
            defaut(f"{lieu} : le champ de perçage ne correspond pas au type {m['code']}")
        if acces > n or (acces == 0 and not traversant):
            defaut(f"{lieu} : accès A{acces:02d} impossible sur {n} couche(s) de cuivre")
            continue
        if not dans_carte((x, y), contour, decoupes):
            defaut(f"{lieu} : point ({x:.3f} ; {y:.3f}) hors de la carte")
        if via:
            vias += 1
        else:
            pastilles += 1
            if not ref or not broche:
                defaut(f"{lieu} : repère ou broche absent")
            vus.add((ref, broche))
        # Trou : au même endroit, du même diamètre, métallisé ou non comme annoncé
        if traversant:
            sondes.append((x, y))
            d, sorte = int(m["percage"][1:5]) / 1000, m["percage"][5]
            proches = [t for t in trous[sorte] if math.hypot(t[0] - x, t[1] - y) <= 0.0015]
            if not proches:
                defaut(f"{lieu} : aucun trou {'métallisé' if sorte == 'P' else 'non métallisé'} en ({x:.3f} ; {y:.3f})")
            elif abs(proches[0][2] - d) > 0.0015 and not (m["percage"][1:5] == "9999" and f"{ref}-{broche} (percage)" in plafonnes):
                defaut(f"{lieu} : perçage de {d} mm, trou de {proches[0][2]} mm dans le fichier de perçage")
        # Cuivre : l'objet est sur la couche annoncée (les deux faces pour un accès 00), du même réseau, de la même taille
        for k in sorted({1, n}) if acces == 0 else [acces]:
            g = cuivres[k][1]
            trouve = None
            for fx, fy, fo, fr, fb in g.flashes:
                if math.hypot(fx - x, fy - y) <= 0.0015 and (via or fb == (ref, broche)):
                    trouve = (fr, fo)
                    break
            if trouve is None:
                for poly, fr, fb in g.regions:
                    if (via or fb == (ref, broche)) and (dans_polygone((x, y), poly) or distance_bord((x, y), poly) <= TOL):
                        trouve = (fr, None)
                        break
            if trouve is None:
                defaut(f"{lieu} : aucun cuivre {'de via' if via else 'de cette broche'} en ({x:.3f} ; {y:.3f}) sur {g.nom}")
                continue
            if nom_ascii(trouve[0]) != nom_ascii(reseau):
                defaut(f"{lieu} : réseau « {reseau} », « {trouve[0]} » dans {g.nom}")
            if trouve[1] is not None and 9.999 not in (larg, haut):
                attendues, lues = sorted(trouve[1].dimensions()), sorted((larg, haut))
                if any(abs(a - b) > 0.0015 for a, b in zip(attendues, lues)):
                    defaut(f"{lieu} : taille {larg} × {haut} mm, ouverture de {attendues[0]} × {attendues[1]} mm sur {g.nom}")
        # Vernis : S donne les faces où l'objet ne se sonde pas (1 dessus, 2 dessous) parce que le vernis le couvre ;
        # une CMS ne s'atteint que par sa face, seul son bit compte (l'autre face n'a pas de cuivre à sonder)
        attendu = (0 if ouverture(masques["Top"], x, y) else 1) | (0 if ouverture(masques["Bot"], x, y) else 2)
        bits = 3 if traversant else (1 if cuivres[acces][0] == "Top" else 2 if cuivres[acces][0] == "Bot" else 0)
        if (code_vernis & bits) != (attendu & bits):
            defaut(f"{lieu} : vernis S{code_vernis}, les fichiers de vernis donnent S{attendu}")
        # Une pastille CMS se brase par sa face : son vernis y est forcément ouvert
        if not traversant and not via and attendu & bits:
            defaut(f"{lieu} : pastille CMS couverte de vernis sur sa face ({cuivres[acces][0]})")
    # Rien d'oublié : chaque trou métallisé est sondé, chaque broche de cuivre des faces extérieures est décrite
    for x, y, d in trous["P"]:
        if not any(math.hypot(sx - x, sy - y) <= 0.0015 for sx, sy in sondes):
            defaut(f"{f.name} : le trou métallisé de {d} mm en ({x:.3f} ; {y:.3f}) manque à la liste de réseaux")
    for k in sorted({1, n}):
        g = cuivres[k][1]
        for b in {fb for *_, fb in g.flashes if fb} | {fb for *_, fb in g.regions if fb}:
            if b not in vus:
                defaut(f"{f.name} : la broche {b[0]}-{b[1]} de {g.nom} manque à la liste de réseaux")
    return {"pastilles": pastilles, "vias": vias}


# ---------------------------------------------------------------------------------------------------------
# Gerber Job Format (Ucamco) : valeurs prévues par la norme
FINITIONS = {"HAL SnPb", "HAL lead-free", "Immersion tin", "Immersion nickel", "Immersion silver", "Immersion gold (ENIG)", "ENEPIG", "Hard gold",
             "OSP", "HT_OSP", "None"}
REGLES_JOB = {"Layers", "PadToPad", "PadToTrack", "PadToRegion", "TrackToTrack", "TrackToRegion", "RegionToRegion", "MinLineWidth", "MinRing",
              "MinClearanceToProfile", "Notes"}
MATERIAUX = {"Copper", "Dielectric", "Legend", "SolderMask", "CoverLay", "PeelableMask", "Carbon"}
COULEUR = re.compile(r"(Red|Yellow|Green|Blue|White|Black|R\d{1,3}G\d{1,3}B\d{1,3})(,(Gloss|Semi-matte|Matte))?")
GUID = re.compile(r"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[14][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}")


def nombre(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def verifier_job(dossier, gerbers, n, largeur, hauteur, defaut):
    fichiers = sorted((dossier / "Gerber").glob("*.gbrjob"))
    if len(fichiers) != 1:
        defaut(f"fichier de travail gbrjob : {len(fichiers)} fichier(s) dans Gerber/ au lieu d'un")
        return {}
    f = fichiers[0]
    try:
        j = json.loads(f.read_text(encoding="utf-8"))
    except (ValueError, UnicodeDecodeError) as e:
        defaut(f"{f.name} : JSON illisible ({e})")
        return {}
    h = j.get("Header")
    if not isinstance(h, dict) or not isinstance(h.get("GenerationSoftware"), dict):
        defaut(f"{f.name} : Header.GenerationSoftware absent")
    else:
        for cle in ("Vendor", "Application", "Version"):
            if not isinstance(h["GenerationSoftware"].get(cle), str) or not h["GenerationSoftware"][cle]:
                defaut(f"{f.name} : Header.GenerationSoftware.{cle} absent ou vide")
        if "CreationDate" in h and not (isinstance(h["CreationDate"], str) and re.fullmatch(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(Z|[+-]\d\d:\d\d)", h["CreationDate"])):
            defaut(f"{f.name} : Header.CreationDate « {h['CreationDate']} » n'est pas une date ISO 8601 avec fuseau")
    g = j.get("GeneralSpecs", {})
    p = g.get("ProjectId")
    if p is not None:
        if not isinstance(p.get("Name"), str) or not p["Name"]:
            defaut(f"{f.name} : ProjectId.Name absent")
        if not isinstance(p.get("GUID"), str) or not GUID.fullmatch(p["GUID"]):
            defaut(f"{f.name} : ProjectId.GUID « {p.get('GUID')} » n'est pas un UUID de version 1 ou 4")
        if not isinstance(p.get("Revision"), str) or not p["Revision"]:
            defaut(f"{f.name} : ProjectId.Revision absente ou vide")
    taille = g.get("Size", {})
    if not (nombre(taille.get("X")) and nombre(taille.get("Y"))) or abs(taille["X"] - largeur) > 0.001 or abs(taille["Y"] - hauteur) > 0.001:
        defaut(f"{f.name} : Size {taille} pour une carte de {largeur:.3f} × {hauteur:.3f} mm")
    if g.get("LayerNumber") != n or not isinstance(g.get("LayerNumber"), int):
        defaut(f"{f.name} : LayerNumber {g.get('LayerNumber')} pour {n} couche(s) de cuivre")
    epaisseur = g.get("BoardThickness")
    if not nombre(epaisseur) or epaisseur <= 0:
        defaut(f"{f.name} : BoardThickness absente ou nulle")
    if "Finish" in g and g["Finish"] not in FINITIONS:
        defaut(f"{f.name} : finition « {g['Finish']} » hors des valeurs de la norme")
    for r in j.get("DesignRules", []):
        if not isinstance(r, dict) or not re.fullmatch(r"Inner|Outer|L\d+", str(r.get("Layers"))):
            defaut(f"{f.name} : règle sans « Layers » valable : {r}")
            continue
        for cle, v in r.items():
            if cle not in REGLES_JOB:
                defaut(f"{f.name} : règle « {cle} » inconnue de la norme")
            elif cle not in ("Layers", "Notes") and (not nombre(v) or v <= 0):
                defaut(f"{f.name} : règle {cle} = {v}")
    # Fichiers : chacun des Gerber, une fois, avec sa vraie fonction et sa vraie polarité
    listes = {}
    for a in j.get("FilesAttributes", []):
        chemin = a.get("Path")
        if not isinstance(chemin, str) or not (f.parent / chemin).is_file():
            defaut(f"{f.name} : fichier « {chemin} » introuvable")
            continue
        if chemin in listes:
            defaut(f"{f.name} : « {chemin} » listé deux fois")
        listes[chemin] = a
        lu = next((x for x in gerbers.values() if x.nom == Path(chemin).name), None)
        if lu is None:
            continue
        if a.get("FileFunction") != lu.fonction:
            defaut(f"{f.name} : « {chemin} » annoncé « {a.get('FileFunction')} », le fichier dit « {lu.fonction} »")
        if a.get("FilePolarity") not in ("Positive", "Negative") or (lu.polarite and a.get("FilePolarity") != lu.polarite):
            defaut(f"{f.name} : « {chemin} » de polarité « {a.get('FilePolarity')} », le fichier dit « {lu.polarite} »")
    for x in gerbers.values():
        if x.nom not in {Path(c).name for c in listes}:
            defaut(f"{f.name} : {x.nom} n'est pas listé")
    # Empilage : complet, de haut en bas, cuivres et diélectriques alternés, épaisseurs cohérentes
    pile = j.get("MaterialStackup")
    if pile is not None:
        types = []
        for m in pile:
            t = m.get("Type")
            if t not in MATERIAUX:
                defaut(f"{f.name} : matériau « {t} » inconnu de la norme")
            if "Color" in m and not COULEUR.fullmatch(str(m["Color"])):
                defaut(f"{f.name} : couleur « {m['Color']} » hors de la norme")
            if "Thickness" in m and (not nombre(m["Thickness"]) or m["Thickness"] <= 0):
                defaut(f"{f.name} : épaisseur {m['Thickness']} de « {t} »")
            types.append(t)
        coeur = [t for t in types if t in ("Copper", "Dielectric")]
        if coeur.count("Copper") != n:
            defaut(f"{f.name} : {coeur.count('Copper')} cuivre(s) dans l'empilage pour {n} couche(s)")
        if any(a == b == "Copper" for a, b in zip(coeur, coeur[1:])) or (n > 1 and (coeur[0] != "Copper" or coeur[-1] != "Copper")):
            defaut(f"{f.name} : empilage {coeur} : cuivres et diélectriques doivent alterner, cuivre aux deux faces")
        indices = [i for i, t in enumerate(types) if t in ("Copper", "Dielectric")]
        faces = {"Top": types[: indices[0]] if indices else [], "Bot": types[indices[-1] + 1:] if indices else []}
        for face, cote in faces.items():
            for fonction, materiau in (("Soldermask", "SolderMask"), ("Legend", "Legend")):
                if f"{fonction},{face}" in gerbers and materiau not in cote:
                    defaut(f"{f.name} : empilage sans « {materiau} » côté {face}, alors que le fichier existe")
        total = sum(m.get("Thickness", 0) for m in pile if m.get("Type") in ("Copper", "Dielectric"))
        if nombre(epaisseur) and abs(total - epaisseur) > 0.1:
            defaut(f"{f.name} : empilage de {total:.3f} mm pour une carte de {epaisseur} mm")
    return {"fichiers": len(listes)}


# ---------------------------------------------------------------------------------------------------------
def verifier(dossier, attendu, defaut):
    gerbers = {}
    for f in sorted((dossier / "Gerber").glob("*.gbr")):
        g = Gerber(f, defaut)
        if g.fonction:
            if g.fonction in gerbers:
                defaut(f"{f.name} : fonction {g.fonction} déjà donnée par {gerbers[g.fonction].nom}")
            gerbers[g.fonction] = g
    if not gerbers:
        defaut("aucun fichier Gerber dans Gerber/")
        return {}

    # Couches de cuivre : L1 Top … Ln Bot, sans trou dans la numérotation ; une simple face n'a que L1, d'un côté
    cuivres = {}
    for fonction, g in gerbers.items():
        m = re.fullmatch(r"Copper,L(\d+),(Top|Inr|Bot)", fonction)
        if m:
            cuivres[int(m.group(1))] = (m.group(2), g)
    n = len(cuivres)
    if n < 1 or sorted(cuivres) != list(range(1, n + 1)):
        defaut(f"couches de cuivre mal numérotées : {sorted(cuivres)}")
        return {}
    if n == 1:
        if cuivres[1][0] == "Inr":
            defaut("simple face : L1 doit être Top ou Bot")
        dessus = cuivres[1][1] if cuivres[1][0] == "Top" else None
        dessous = cuivres[1][1] if cuivres[1][0] == "Bot" else None
    else:
        if cuivres[1][0] != "Top" or cuivres[n][0] != "Bot" or any(cuivres[i][0] != "Inr" for i in range(2, n)):
            defaut("couches de cuivre : L1 doit être Top, Ln Bot, les autres Inr")
        dessus, dessous = cuivres[1][1], cuivres[n][1]

    profil = gerbers.get("Profile,NP")
    if profil is None:
        defaut("contour de carte (Profile,NP) absent")
        return {}
    contour, decoupes = chainer_contour(profil.traits, defaut, profil.nom)
    if not contour:
        return {}
    if abs(aire(contour)) < 1.0:
        defaut(f"{profil.nom} : contour d'aire nulle")
    xs = [p[0] for p in contour]
    ys = [p[1] for p in contour]
    largeur, hauteur = max(xs) - min(xs), max(ys) - min(ys)

    # Toute la géométrie fabriquée dans la carte (même origine et mêmes unités que le contour)
    for fonction, g in gerbers.items():
        if g is profil:
            continue
        hors = [p for p in g.points() if not dans_carte(p, contour, decoupes)]
        if hors:
            message = f"{g.nom} : {len(hors)} point(s) hors du contour, par exemple ({hors[0][0]:.4f} ; {hors[0][1]:.4f})"
            # Une légende peut déborder exprès (connecteur en bord de carte) : on la signale sans bloquer
            if fonction.startswith("Legend"):
                print("  remarque : " + message)
            else:
                defaut(message)

    # Segment effilé à bouts carrés (5.22) : une région de cuivre au contour exact du trapèze, à 1 µm près
    if attendu.get("trapeze"):
        ax, ay, bx, by, wa, wb = attendu["trapeze"]
        L = math.hypot(bx - ax, by - ay)
        nx, ny = -(by - ay) / L, (bx - ax) / L
        coins = [(ax + nx * wa / 2, ay + ny * wa / 2), (bx + nx * wb / 2, by + ny * wb / 2), (bx - nx * wb / 2, by - ny * wb / 2), (ax - nx * wa / 2, ay - ny * wa / 2)]
        trouve = False
        for _cote, g in cuivres.values():
            for poly, *_ in g.regions:
                pts = [q for i, q in enumerate(poly) if i == 0 or math.hypot(q[0] - poly[i - 1][0], q[1] - poly[i - 1][1]) > 1e-9]
                if len(pts) > 1 and math.hypot(pts[0][0] - pts[-1][0], pts[0][1] - pts[-1][1]) <= 1e-9:
                    pts = pts[:-1]
                if len(pts) == 4 and all(min(math.hypot(c[0] - q[0], c[1] - q[1]) for q in pts) <= 0.001 for c in coins):
                    trouve = True
        if not trouve:
            defaut("segment effilé de %.3f à %.3f mm : aucune région au contour exact du trapèze attendu" % (wa, wb))

    # Perçages
    perces = {}
    for f in sorted((dossier / "Percage").glob("*.drl")):
        e = Excellon(f, defaut)
        perces[f.name] = e
        if e.fonction:
            m = re.fullmatch(r"(Plated|NonPlated),(\d+),(\d+),(PTH|NPTH)", e.fonction)
            if not m:
                defaut(f"{f.name} : fonction de perçage illisible « {e.fonction} »")
            elif (int(m.group(2)), int(m.group(3))) != (1, n):
                defaut(f"{f.name} : perçage déclaré de L{m.group(2)} à L{m.group(3)} pour {n} couches de cuivre")
        for x, y, d in e.trous:
            if not dans_carte((x, y), contour, decoupes) or min(distance_bord((x, y), b) for b in [contour] + decoupes) < d / 2 - TOL:
                defaut(f"{f.name} : trou de {d} mm en ({x:.4f} ; {y:.4f}) hors de la carte")
    # Fente attendue (4.13) : relue en G85 ou détourée, de la longueur hors tout et du diamètre de la conception
    if attendu.get("fente"):
        longueur, diametre = attendu["fente"]
        fentes = [f for e in perces.values() for f in e.fentes]
        if not any(abs(f[4] - diametre) <= TOL and abs(math.hypot(f[2] - f[0], f[3] - f[1]) + f[4] - longueur) <= TOL for f in fentes):
            defaut("fente de %.3f × %.3f mm attendue : %s" % (longueur, diametre, ", ".join("%.3f × %.3f" % (math.hypot(f[2] - f[0], f[3] - f[1]) + f[4], f[4]) for f in fentes) or "aucune fente lue"))
    pth = [t for e in perces.values() if e.fonction and e.fonction.startswith("Plated") for t in e.trous]
    npth = [t for e in perces.values() if e.fonction and e.fonction.startswith("NonPlated") for t in e.trous]
    if not perces:
        defaut("aucun fichier de perçage dans Percage/")

    def pastille_sous(g, x, y, d):
        for fx, fy, o, r, _b in g.flashes:
            if math.hypot(fx - x, fy - y) <= TOL:
                return ("éclair", o.plus_petit_cote(), r)
        for poly, r, _b in g.regions:
            if dans_polygone((x, y), poly) and distance_bord((x, y), poly) >= d / 2:
                return ("région", 2 * distance_bord((x, y), poly), r)
        return None

    if n == 1 and pth:
        defaut(f"carte simple face : {len(pth)} trou(s) métallisé(s), qu'aucun fabricant de simple face ne fait")
    for x, y, d in pth if n > 1 else []:
        reseaux = set()
        for g in (dessus, dessous):
            p = pastille_sous(g, x, y, d)
            if p is None:
                defaut(f"trou métallisé de {d} mm en ({x:.4f} ; {y:.4f}) sans pastille sur {g.nom}")
                continue
            if p[1] <= d + TOL:
                defaut(f"trou métallisé de {d} mm en ({x:.4f} ; {y:.4f}) : pastille de {p[1]:.3f} mm sur {g.nom}, pas de couronne")
            reseaux.add(p[2])
        if len(reseaux) > 1:
            defaut(f"trou métallisé en ({x:.4f} ; {y:.4f}) : réseaux différents selon la face {sorted(reseaux)}")

    # Pâte et vernis : chaque ouverture correspond à une pastille de cuivre de la même face ; une face sans cuivre
    # (simple face) n'a ni pâte ni ouverture de vernis, sauf au droit d'un trou
    for face, cuivre in (("Top", dessus), ("Bot", dessous)):
        flashes_cuivre = cuivre.flashes if cuivre else []
        regions_cuivre = cuivre.regions if cuivre else []
        nom_cuivre = cuivre.nom if cuivre else f"la face {face}, sans cuivre"
        pate = gerbers.get(f"Paste,{face}")
        if pate:
            for x, y, o, *_ in pate.flashes:
                if not any(math.hypot(fx - x, fy - y) <= TOL and fo.forme == o.forme and fo.dimensions() == o.dimensions() for fx, fy, fo, *_ in flashes_cuivre):
                    # Marge de pâte propre à la pastille (4.24) : ouverture réduite au même endroit, qui tient dans la pastille
                    reduite = any(math.hypot(fx - x, fy - y) <= TOL and all(a <= b + TOL for a, b in zip(sorted(o.dimensions()), sorted(fo.dimensions())))
                                  for fx, fy, fo, *_ in flashes_cuivre)
                    if not reduite:
                        defaut(f"{pate.nom} : ouverture en ({x:.4f} ; {y:.4f}) sans pastille identique sur {nom_cuivre}")
            for poly, *_ in pate.regions:
                # Fenêtre de pâte découpée (grande pastille, F2) : dans une pastille de cuivre de la même face
                identique = any(len(cp) == len(poly) and all(math.hypot(a[0] - b[0], a[1] - b[1]) <= TOL for a, b in zip(cp, poly)) for cp, *_ in regions_cuivre)
                def dans_eclair(q, fx, fy, fo):
                    if fo.forme == "C":
                        return math.hypot(q[0] - fx, q[1] - fy) <= fo.params[0] / 2 + TOL
                    return abs(q[0] - fx) <= fo.dimensions()[0] / 2 + TOL and abs(q[1] - fy) <= fo.dimensions()[1] / 2 + TOL

                contenue = any(all(dans_polygone(q, cp) or distance_bord(q, cp) <= TOL for q in poly) for cp, *_ in regions_cuivre) or any(
                    all(dans_eclair(q, fx, fy, fo) for q in poly) for fx, fy, fo, *_ in flashes_cuivre)
                if not identique and not contenue:
                    defaut(f"{pate.nom} : région de pâte hors de toute pastille de cuivre de {nom_cuivre}")
        vernis = gerbers.get(f"Soldermask,{face}")
        if vernis:
            for x, y, o, *_ in vernis.flashes:
                pad = [fo for fx, fy, fo, *_ in flashes_cuivre if math.hypot(fx - x, fy - y) <= TOL]
                # Un trou non métallisé (fixation) est dégagé du vernis sans pastille de cuivre
                trou = [d for tx, ty, d in npth if math.hypot(tx - x, ty - y) <= TOL]
                if trou:
                    if o.plus_petit_cote() < trou[0] - TOL:
                        defaut(f"{vernis.nom} : ouverture en ({x:.4f} ; {y:.4f}) plus petite que le trou de {trou[0]} mm")
                elif not pad:
                    defaut(f"{vernis.nom} : ouverture en ({x:.4f} ; {y:.4f}) sans pastille sur {nom_cuivre}")
                elif o.plus_petit_cote() < pad[0].plus_petit_cote() - TOL:
                    defaut(f"{vernis.nom} : ouverture en ({x:.4f} ; {y:.4f}) plus petite que sa pastille")

    # Placement
    placement = sorted((dossier / "Assemblage").glob("*-placement.csv"))
    refs_placees = []
    if not placement:
        defaut("fichier de placement absent")
    for f in placement:
        lignes = list(csv.DictReader(io.StringIO(f.read_text(encoding="utf-8"))))
        for l in lignes:
            try:
                p = (float(l["PosX"]), float(l["PosY"]))
                rot = float(l["Rot"])
            except (KeyError, ValueError):
                defaut(f"{f.name} : ligne illisible {l}")
                continue
            refs_placees.append(l["Ref"])
            if not dans_carte(p, contour):
                defaut(f"{f.name} : {l['Ref']} placé en ({p[0]:.4f} ; {p[1]:.4f}), hors de la carte")
            if not 0 <= rot < 360:
                defaut(f"{f.name} : {l['Ref']} : angle {rot} hors de [0 ; 360[")
            if l["Face"] not in ("dessus", "dessous"):
                defaut(f"{f.name} : {l['Ref']} : face « {l['Face']} » inconnue")
        if len(set(refs_placees)) != len(refs_placees):
            defaut(f"{f.name} : référence en double")

    # Nomenclature
    nomenclature = sorted((dossier / "Nomenclature").glob("*-nomenclature.csv"))
    refs_nomenclature = []
    if not nomenclature:
        defaut("nomenclature absente")
    for f in nomenclature:
        brut = f.read_bytes()
        if not brut.startswith(b"\xef\xbb\xbf"):
            defaut(f"{f.name} : marque UTF-8 absente (les tableurs liraient mal les accents)")
        lignes = list(csv.reader(io.StringIO(brut.decode("utf-8-sig")), delimiter=";"))
        for l in lignes[1:]:
            if not l:
                continue
            refs = [r.strip() for r in l[1].split(",") if r.strip()]
            if int(l[0]) != len(refs):
                defaut(f"{f.name} : quantité {l[0]} pour {len(refs)} référence(s) ({l[1]})")
            refs_nomenclature += refs
    if refs_placees and refs_nomenclature:
        manquantes = sorted(set(refs_placees) - set(refs_nomenclature))
        if manquantes:
            defaut(f"composants placés absents de la nomenclature : {', '.join(manquantes)}")

    ipc = verifier_ipc(dossier, contour, decoupes, gerbers, cuivres, n, perces, defaut)
    job = verifier_job(dossier, gerbers, n, largeur, hauteur, defaut)

    resume = {
        "couches_cuivre": n,
        "carte_mm": (round(largeur, 3), round(hauteur, 3)),
        "trous_metallises": len(pth),
        "trous_non_metallises": len(npth),
        "composants_places": len(refs_placees),
        "lignes_nomenclature": len(refs_nomenclature),
        "eclairs_dessus": len(dessus.flashes) if dessus else 0,
        "pistes_dessus": len(dessus.traits) if dessus else 0,
        "regions_dessous": len(dessous.regions) if dessous else 0,
        "ipc_pastilles": ipc.get("pastilles"),
        "ipc_vias": ipc.get("vias"),
        "gbrjob_fichiers": job.get("fichiers"),
    }
    for cle, valeur in attendu.items():
        if cle in ("trapeze", "fente"):
            continue
        if valeur is not None and resume.get(cle) != valeur:
            defaut(f"attendu {cle} = {valeur}, trouvé {resume.get(cle)}")
    return resume


def main():
    a = argparse.ArgumentParser(description="Vérification indépendante d'un dossier de fabrication Prométhée")
    a.add_argument("dossier", type=Path)
    a.add_argument("--couches", type=int)
    a.add_argument("--composants", type=int)
    a.add_argument("--trous-npth", type=int)
    a.add_argument("--carte", type=float, nargs=2, metavar=("LARGEUR", "HAUTEUR"))
    a.add_argument("--trapeze", type=float, nargs=6, metavar=("AX", "AY", "BX", "BY", "LA", "LB"))
    a.add_argument("--fente", type=float, nargs=2, metavar=("LONGUEUR", "DIAMETRE"))
    a.add_argument("--jeux-excellon", action="store_true", help="dossier de perçages écrits avec des options différentes (4.27)")
    o = a.parse_args()
    if not o.dossier.is_dir():
        print(f"dossier introuvable : {o.dossier}", file=sys.stderr)
        return 2
    defauts = Defauts()
    if o.jeux_excellon:
        resume = comparer_jeux_excellon(o.dossier, defauts)
        for cle, valeur in resume.items():
            print(f"  {cle} : {valeur}")
        if defauts.liste:
            print(f"{len(defauts.liste)} défaut(s) :", file=sys.stderr)
            for d in defauts.liste:
                print("  - " + d, file=sys.stderr)
            return 1
        print("Jeux d'options Excellon relus à l'identique (vérification indépendante).")
        return 0
    attendu = {
        "couches_cuivre": o.couches,
        "composants_places": o.composants,
        "trous_non_metallises": o.trous_npth,
        "carte_mm": tuple(round(v, 3) for v in o.carte) if o.carte else None,
        "trapeze": tuple(o.trapeze) if o.trapeze else None,
        "fente": tuple(o.fente) if o.fente else None,
    }
    try:
        resume = verifier(o.dossier, attendu, defauts)
    except (OSError, ValueError, UnicodeDecodeError) as e:
        print(f"dossier illisible : {e}", file=sys.stderr)
        return 2
    for cle, valeur in resume.items():
        print(f"  {cle} : {valeur}")
    if defauts.liste:
        print(f"{len(defauts.liste)} défaut(s) :", file=sys.stderr)
        for d in defauts.liste:
            print("  - " + d, file=sys.stderr)
        return 1
    print("Dossier de fabrication conforme (vérification indépendante).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
