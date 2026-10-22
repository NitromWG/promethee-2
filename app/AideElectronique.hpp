// Prométhée : aide de l'atelier électronique (lot L2, tâche 2.17 ; ERG-13, ERG-16, AUT-11). « C'est quoi ? » de
// chaque règle de vérification (ERC, DRC, fabrication, intégrité, nomenclature), en mots simples, avec ce qui la
// déclenche et comment s'en sortir ; lexique des mots du métier (réseau, empreinte, pastille, via, plan de
// masse…) ; un essai vérifie que chaque code que le noyau peut rendre a son explication.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QString>

namespace aide {

// Titre court d'une règle (« Broche non connectée ») et son explication ; vides pour un code inconnu
QString titreRegle(const std::string& code);
QString explicationRegle(const std::string& code);
std::vector<std::string> codesExpliques();

// Lexique : le mot, sa définition ; un mot inconnu rend une chaîne vide
struct Mot {
  const char* mot;
  const char* definition;
};
const std::vector<Mot>& lexique();
QString definition(const QString& mot);

}  // namespace aide
