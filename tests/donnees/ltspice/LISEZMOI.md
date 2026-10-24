# Circuit LTspice d'essai (10.14)

`rc.asc` : filtre RC (source V1, R1 tournée d'un quart de tour, C1) chargé par R2 posée retournée et tournée (M90),
deux masses (drapeau « 0 »), deux étiquettes (IN, OUT), une directive SPICE (`.tran 10m`) et un commentaire.
Symboles de base d'LTspice (res, cap, voltage), sans .asy à côté.

`rc.net` est la netlist qu'LTspice écrit pour ce schéma : les réseaux de l'import de Prométhée doivent lui être
identiques (nœud « 0 » : la masse GND).
