// Prométhée : essais automatiques de l'interface, qui cliquent réellement dans la vue 3D et l'esquisseur.
// Licence GPL-3.0-only. Lancés par « Promethee --essais-interface » ; le code de sortie est le nombre d'échecs.
#pragma once

class FenetrePrincipale;
void lancerEssaisInterface(FenetrePrincipale& fenetre);
void lancerEssaisOnglets(class Coquille& coquille);
void lancerEssaisElectronique(FenetrePrincipale& fenetre);  // « Promethee --essais-electronique »
void lancerEssaisAudit(FenetrePrincipale& fenetre);  // audit indépendant
void lancerEssaisElectrique(FenetrePrincipale& fenetre);  // « Promethee --essais-electrique »
// Veille des essais : une boîte modale restée ouverte plus de trois secondes (question imprévue) est notée dans la
// sortie, puis fermée par son bouton Oui, OK ou par défaut, au lieu de bloquer l'essai jusqu'au délai maximal.
void installerVeilleBoites();
