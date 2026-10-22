// Prométhée : glossaire de l'électricien, en mots simples. Chaque grandeur ou notion affichée par l'atelier
// électrique (Ib, Iz, Icc, DDR, régime de neutre, sélectivité…) a sa fiche : ce que c'est, à quoi ça sert, un ordre
// de grandeur, et la règle qui s'y rattache. Les fiches servent aux bulles d'aide, aux « ? » des résultats et au
// parcours d'apprentissage. Aucune dépendance à l'interface. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

namespace prom::elec {

struct TermeGlossaire {
  std::string cle;         // « ib », « iz », « icc »…
  std::string terme;       // « Courant d'emploi Ib »
  std::string definition;  // ce que c'est, en une ou deux phrases
  std::string exemple;     // ordre de grandeur ou exemple concret
  std::string regle;       // norme ou règle de l'art qui s'y rattache
};
const std::vector<TermeGlossaire>& glossaireElectrique();
const TermeGlossaire* termeGlossaire(const std::string& cle);
// Fiches dont le terme, la définition ou la clé contiennent tous les mots cherchés (sans accents ni casse)
std::vector<const TermeGlossaire*> chercherGlossaire(const std::string& texte);

// Fiche « C'est quoi ? » d'un symbole de la bibliothèque : ce qu'est l'appareil, puis ce qu'il faut savoir pour
// s'en servir (raccordement, réglage, règle) ; nullptr pour un symbole sans fiche
struct AideSymbole {
  std::string quoi, usage;
};
const AideSymbole* aideSymbole(const std::string& id);

}  // namespace prom::elec
