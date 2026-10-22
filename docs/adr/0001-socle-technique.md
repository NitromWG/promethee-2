# ADR 0001 : socle technique

Statut : acceptée, 2 octobre 2026.

## Contexte

Le cahier des charges (§20) demande un cœur en C++20 pour réutiliser directement OpenCascade, KiCad et FreeCAD, Python pour les scripts et le mode sans écran, Qt 6 pour l'interface, et laisse Rust possible pour les modules exposés à des fichiers inconnus. Le jalon 0 a d'abord été prouvé par un prototype web en JavaScript, dont le format de fichier est déjà utilisé.

## Décision

Le logiciel natif est écrit en C++20 et construit avec CMake 3.22 ou plus. La géométrie exacte repose sur OpenCascade, de la version 7.6 (paquets Ubuntu 24.04) à la version 8.0 (vcpkg, Homebrew). Le format de projet `.prom.json` est commun avec le prototype et lu avec nlohmann/json. Les tests utilisent Catch2 v3. L'interface sera en Qt 6 (lot 0.C). Les dépendances viennent de vcpkg sous Windows, de Homebrew sous macOS et des paquets du système sous Linux.

Le prototype JavaScript reste l'implémentation de référence tant que la parité n'est pas totale : des cas générés par son noyau (`tests/donnees/reference_js.json`) sont comparés à chaque compilation avec le portage C++, cote par cote et message par message.

## Options écartées

Tout en Rust : les noyaux géométriques Truck et Fornjot ne sont pas encore au niveau d'OpenCascade. Tout en web : OpenCascade complet ne tient pas dans une page, et l'accès aux fichiers et aux performances est limité. Python pour le cœur : trop lent pour la reconstruction interactive.

## Conséquences

Le code doit compiler avec OpenCascade 7.6 comme 8.0 : les noms des boîtes à outils d'échange sont choisis à la configuration. Chaque fonction du prototype portée en C++ reçoit des cas de référence avant d'être considérée comme terminée.
