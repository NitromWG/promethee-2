# Essais de l'audit de Prométhée 0.25

Ces 114 essais Catch2 ont été écrits pendant l'audit de la modélisation mécanique de Prométhée 0.25. Ils appellent directement le noyau et la géométrie (esquisses, fonctions 3D, assemblages, mise en plan, exports), sans passer par l'interface.

Chaque essai porte le numéro du défaut du rapport qu'il vérifie : `ES-01`, `FO-02`, `AS-05`… Un `CHECK` décrit toujours le comportement correct attendu. Un essai en échec confirme donc le défaut, et il passera de lui-même quand le défaut sera corrigé : il devient alors un essai de non-régression. Les essais nommés « OK … » vérifient ce qui fonctionne déjà, « PERF … » et « P-01 » à « P-03 » mesurent des temps, « PARTIEL … » relève une fonction incomplète.

## Résultat sur la 0.25

Compilation Linux du code de la 0.25.0+9aaf18843479.55 avec OpenCascade 7.6 : 113 essais lancés (tous sauf `[gel]`), 85 en échec, 28 réussis. L'essai `ES-26b` (étiquette `[gel]`) ne rend jamais la main tant que ES-26 n'est pas corrigé ; il est écarté par défaut.

Six essais liés à un défaut réussissent pour une raison connue : `ES-19` demande une compilation avec AddressSanitizer pour montrer la lecture en mémoire libérée ; `ES-36`, `ES-38` et `FO-25` relèvent seulement des valeurs (messages « warning ») et attendent qu'on fixe le comportement voulu pour recevoir une assertion ; `ES-02b` couvre une variante du scénario de ES-02 qui se comporte bien ; `FO-12` correspond au seul constat non reproduit.

## Installation dans le dépôt (recommandé)

Copier ce dossier en `tests/audit/`, puis ajouter à la fin de `tests/CMakeLists.txt` :

```cmake
add_subdirectory(audit)
```

Les cibles `Promethee::noyau` et `Promethee::geometrie` sont reprises, sous Linux, Windows et macOS. CTest découvre les essais avec le préfixe `audit:` et écarte `[gel]`.

## Compilation seule, contre un dépôt déjà compilé (Linux)

```sh
cmake -S . -B b -G Ninja -DPROMETHEE_SRC=<racine du dépôt> -DPROMETHEE_BUILD=<dossier de compilation>
cmake --build b
```

## Lancer

```sh
./essais_audit "~[gel]"                 # tout, sauf l'essai qui fige
./essais_audit "[ES-01]"                # les essais d'un défaut
./essais_audit "ES-*"                   # un domaine (ES, EI, FO, AP, AS, MP, EX)
./essais_audit "[perf]"                 # les mesures de temps
./essais_audit "~[gel]" --reporter junit::out=resultats.xml
```

Les essais d'export écrivent leurs fichiers dans `sorties/` (dossier courant) ou dans le dossier donné par la variable `PROMETHEE_ESSAIS_SORTIES`. Leur relecture par des lecteurs indépendants se lance ensuite ainsi (Python 3 avec ezdxf, trimesh, numpy et plyfile) :

```sh
python3 py/verif_fichiers.py sorties
```

Chaque ligne commence par « OK » ou « DÉFAUT », suivi du numéro du défaut concerné.

Les folios électriques exportés en DXF (« Folio en DXF », dossier complet, ou les captures de `--essais-electrique` quand `PROMETHEE_CAPTURES_ELECTRIQUE` désigne un dossier : `exemple-commande.dxf`, `grafcet-feux.dxf`) se relisent de même, version, audit, calques et textes compris :

```sh
python3 py/verif_dxf_folios.py captures/exemple-commande.dxf captures/grafcet-feux.dxf
```

## À faire en les intégrant

Ajouter une compilation AddressSanitizer et UBSan en intégration continue, qui rejoue ces essais (c'est elle qui confirmera `ES-19`). Quand un défaut est corrigé, garder son essai tel quel. Pour les trois essais qui ne font que relever des valeurs, écrire l'assertion du comportement retenu.
