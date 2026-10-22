# Prototype web du jalon 0

Implémentation de référence en JavaScript : la carte et le boîtier forment un seul modèle, dans une page web autonome (vue carte 2D, vue boîtier 3D avec three.js, vérifications, nomenclature, dossier de fabrication).

`core.js` est le noyau (modèle, vérifications, maillages, exports), `ui.js` l'interface et `shell.html` la page. `python3 assembler.py promethee-jalon0.html` produit la page unique. `npm install` puis `npm run reference` régénère les cas de référence utilisés par les tests C++ (`../tests/donnees/reference_js.json`).
