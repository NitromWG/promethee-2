# Relecture indépendante des fichiers exportés par Prométhée (sorties de test_F_exports.cpp).
import io, os, re, sys, zipfile, struct
import xml.etree.ElementTree as ET
D = sys.argv[1] if len(sys.argv) > 1 else "sorties"
res = []
def note(id, ok, msg): res.append((id, "OK " if ok else "DÉFAUT", msg))

# DXF de la mise en plan
import ezdxf
try:
    doc = ezdxf.readfile(f"{D}/plan.dxf")
    textes = [e.dxf.text for e in doc.modelspace().query("TEXT")]
    types = {}
    for e in doc.modelspace(): types[e.dxftype()] = types.get(e.dxftype(), 0) + 1
    note("MP-04", any(t == "Échelle" for t in textes), f"DXF version {doc.dxfversion}, $INSUNITS={doc.header.get('$INSUNITS')}, entités {types}, textes lus : {textes[:6]}")
    aud = doc.audit()
    note("OK-DXF-audit", len(aud.errors) == 0, f"audit ezdxf : {len(aud.errors)} erreur(s), {len(aud.fixes)} correction(s)")
except Exception as ex:
    note("MP-04", False, f"lecture ezdxf impossible : {ex}")

# SVG de la mise en plan (titre « Équerre R&D <V2> »)
try:
    ET.parse(f"{D}/plan.svg"); note("MP-01", True, "SVG bien formé")
except ET.ParseError as ex:
    note("MP-01", False, f"SVG invalide : {ex}")

# 3MF : nom « Bride & joint »
for nom in ["piece.3mf", "piece_simple.3mf"]:
    try:
        z = zipfile.ZipFile(f"{D}/{nom}")
        bad = z.testzip()
        modele = z.read("3D/3dmodel.model")
        try:
            r = ET.fromstring(modele)
            ns = {"m": "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"}
            obj = r.findall(".//m:object", ns)
            note("EX-04" if nom == "piece.3mf" else "OK-3MF", True, f"{nom} : zip {'OK' if bad is None else bad}, XML bien formé, {len(obj)} objet(s), unité {r.get('unit')}")
        except ET.ParseError as ex:
            note("EX-04" if nom == "piece.3mf" else "OK-3MF", False, f"{nom} : 3dmodel.model invalide ({ex})")
    except Exception as ex:
        note("EX-04", False, f"{nom} : {ex}")

# STL : étanchéité, volume
import trimesh
for nom in ["piece.stl", "piece_texte.stl"]:
    m = trimesh.load(f"{D}/{nom}", force="mesh")
    note("OK-STL", m.is_watertight and m.is_winding_consistent and m.volume > 0, f"{nom} : {len(m.faces)} triangles, étanche {m.is_watertight}, orientation cohérente {m.is_winding_consistent}, volume {m.volume:.2f} mm³")
taille = os.path.getsize(f"{D}/piece.stl"); n = struct.unpack("<I", open(f"{D}/piece.stl","rb").read()[80:84])[0]
note("OK-STL-bin", taille == 84 + 50*n, f"STL binaire : {n} triangles, taille {taille} (attendu {84+50*n})")

# 3MF par trimesh
try:
    m = trimesh.load(f"{D}/piece_simple.3mf", force="mesh")
    note("OK-3MF-trimesh", m.is_watertight, f"3MF relu par trimesh : étanche {m.is_watertight}, volume {m.volume:.2f}")
except Exception as ex:
    note("OK-3MF-trimesh", False, f"3MF illisible par trimesh : {ex}")

# OBJ : mtllib avec espace
obj = open(f"{D}/Support moteur.obj", encoding="utf-8").read()
ligne = [l for l in obj.splitlines() if l.startswith("mtllib")]
note("EX-17", not (ligne and " " in ligne[0][7:].strip()), f"ligne « {ligne[0] if ligne else '—'} » (la spécification OBJ sépare plusieurs fichiers .mtl par des espaces)")

# PLY : en-tête ASCII ?
from plyfile import PlyData
try:
    p = PlyData.read(f"{D}/piece.ply"); note("EX-18", True, f"PLY relu : {len(p['vertex'])} sommets, propriétés {[x.name for x in p['vertex'].properties]}")
except Exception as ex:
    note("EX-18", False, f"PLY illisible par plyfile : {type(ex).__name__} {ex}")

# STEP couleurs et noms
st = open(f"{D}/couleurs.step", encoding="latin-1").read()
cols = re.findall(r"COLOUR_RGB\('[^']*',([0-9.Ee+-]+),([0-9.Ee+-]+),([0-9.Ee+-]+)\)", st)
note("EX-15", any(abs(float(r)-0.18) < 0.01 and abs(float(g)-0.42) < 0.01 for r,g,b in cols), f"COLOUR_RGB lues : {cols[:4]} (demandé 0,18 / 0,42 / 0,31)")
noms = re.findall(r"PRODUCT\('([^']*)'", st)
def x2(t):  # ISO 10303-21 : \X2\hhhh…\X0\ (plan de base) et \X4\hhhhhhhh…\X0\
    t = re.sub(r"\\X2\\((?:[0-9A-F]{4})+)\\X0\\", lambda m: "".join(chr(int(m.group(1)[i:i+4], 16)) for i in range(0, len(m.group(1)), 4)), t)
    return re.sub(r"\\X4\\((?:[0-9A-F]{8})+)\\X0\\", lambda m: "".join(chr(int(m.group(1)[i:i+8], 16)) for i in range(0, len(m.group(1)), 8)), t)
noms_lus = [x2(n) for n in noms]
note("EX-16", "_6" not in st and any("Ø6" in n for n in noms_lus) and all(ord(c) < 128 for c in st),
     f"noms de produits décodés : {noms_lus} ; fichier en ASCII pur : {all(ord(c) < 128 for c in st)}")
schema = re.findall(r"FILE_SCHEMA\s*\(\s*\(\s*'([^']*)'", st)
note("OK-STEP-schema", True, f"schéma : {schema}")
# Assemblage STEP
st2 = open(f"{D}/assemblage.step", encoding="latin-1").read()
# trois instances (deux copies et une symétrie) : il faut un assemblage racine et trois liens NAUO
note("EX-07", st2.count("NEXT_ASSEMBLY_USAGE_OCCURRENCE") >= 3, f"assemblage.step : {st2.count('NEXT_ASSEMBLY_USAGE_OCCURRENCE')} liens d'assemblage (NAUO), {len(re.findall(r'PRODUCT\(', st2))} produits, {st2.count('MANIFOLD_SOLID_BREP')} solides")
# IGES : types d'entités
ig = open(f"{D}/piece.igs", encoding="latin-1").read().splitlines()
types = {}
for l in ig:
    if len(l) >= 73 and l[72] == "D":
        t = l[0:8].strip()
        if t: types[t] = types.get(t, 0) + 1
note("EX-19", "144" in types or "143" in types, f"types d'entités IGES (section D, une sur deux) : {dict(sorted(types.items()))}")

for id, etat, msg in res: print(f"{etat} {id:14s} {msg}")
