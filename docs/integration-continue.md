# Vérifier un projet Prométhée à chaque envoi

L'outil `promethee` se lance sans écran : un dépôt qui contient un projet `.prom` peut le faire vérifier, et sortir son dossier de fabrication, à chaque envoi. Les commandes rendent 0 quand tout est conforme et 1 quand il reste une erreur, ce qui suffit à faire échouer l'étape ; `--json` donne le détail à un autre programme.

```yaml
name: Projet
on: [push, pull_request]
jobs:
  verifier:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - name: Prométhée (outil en ligne de commande)
        run: |
          git clone --depth 1 -b refonte https://github.com/NitromWG/promethee-2 promethee
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends cmake ninja-build g++ libtbb-dev nlohmann-json3-dev libfontconfig-dev \
            libocct-modeling-algorithms-dev libocct-modeling-data-dev libocct-data-exchange-dev libocct-foundation-dev libocct-visualization-dev
          cmake -S promethee -B promethee/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPROMETHEE_TESTS=OFF -DPROMETHEE_APP=OFF
          cmake --build promethee/build --target promethee
      - name: ERC, DRC, revue et exigences
        run: |
          P=promethee/build/outils/promethee
          $P erc carte.prom
          $P drc carte.prom
          $P revue carte.prom
          $P exigences carte.prom
      - name: Dossier de fabrication
        run: promethee/build/outils/promethee sorties carte.prom jlcpcb -D REVISION=${GITHUB_SHA::7}
      - uses: actions/upload-artifact@v4
        with:
          name: fabrication
          path: sorties/
```

Le jeu « jlcpcb » écrit Gerber, perçages, placement et nomenclature dans `sorties/{carte}-{revision}-jlcpcb`, avec son archive zip, après avoir contrôlé la nomenclature et les empreintes ; il refuse d'écrire, et rend 1, si un contrôle échoue. Un jeu enregistré dans la conception (fenêtre « Outils de conception ») se lance de la même façon par son nom ; `-D CLE=VALEUR` remplace une variable du nom de dossier ({projet}, {carte}, {revision}, {variante}, {date}).

`promethee valider carte.prom` enchaîne en une commande l'ERC, la DRC, la revue, les variantes, les contrôles des sorties et les exigences. `promethee comparer ancien.prom carte.prom --json` décrit ce qu'une demande de fusion change, catégorie par catégorie ; `promethee fusionner ancetre.prom nous.prom eux.prom sortie.prom` fait la fusion à trois voies et rend 1 s'il reste des conflits, ce qui permet de le déclarer comme pilote de fusion de Git pour les fichiers `.prom` :

```sh
git config merge.promethee.driver "promethee fusionner %O %A %B %A"
echo "*.prom merge=promethee" >> .gitattributes
```
