// Prométhée : bibliothèques des autres logiciels converties en bibliothèque de Prométhée (lot L10 de la 0.34 ; 10.4,
// 10.18 ; X10, SYM-10, EMP-12, ECH-04, ECH-23, ECH-24). Une seule entrée pour tous les formats lus : bibliothèques
// KiCad entières (fichiers .kicad_sym et dossiers .pretty de .kicad_mod), bibliothèques EAGLE (.lbr), et les formats
// des importeurs suivants du lot. Chaque objet lu (symbole, empreinte, article) a sa ligne au rapport : importé,
// remplacé, gardé ou refusé, avec la raison. Un article est créé par symbole (par variante d'un ensemble EAGLE), relié
// à son empreinte quand elle est lue avec lui, déjà dans la destination ou dans le catalogue de Prométhée.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/gestion_bibliotheque.hpp"

namespace prom::ecad {

struct FichierBibliotheque {
  std::string chemin;   // chemin relatif (« Device.pretty/R_0603.kicad_mod ») : le dossier .pretty nomme la bibliothèque
  std::string contenu;
};
// Extensions lues, pour le filtre du dialogue (« *.kicad_sym *.kicad_mod *.lbr … »)
std::string filtreBibliotheques();
bool fichierBibliothequeLu(const std::string& chemin);
RapportBibliotheque importerBibliotheques(const std::vector<FichierBibliotheque>& fichiers, ContenuBibliotheque& destination, bool remplacer = false);

}  // namespace prom::ecad
