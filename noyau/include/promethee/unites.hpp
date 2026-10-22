// Prométhée : unités et saisie des valeurs (« 15 in », « 20 rad », « 10 + 2*3 mm »). Licence GPL-3.0-only.
// En interne, les longueurs sont toujours en millimètres et les angles en degrés ; l'unité du document ne
// change que l'affichage et l'unité prise par défaut à la saisie.
#pragma once

#include <map>
#include <string>

namespace prom {

enum class UniteLongueur { Millimetre, Centimetre, Metre, Pouce, Pied, Mil };  // mil : millième de pouce (cartes électroniques)
enum class UniteAngle { Degre, Radian };

struct Unites {
  UniteLongueur longueur = UniteLongueur::Millimetre;
  UniteAngle angle = UniteAngle::Degre;
};

const char* symbole(UniteLongueur u);
const char* symbole(UniteAngle u);
double versMillimetres(double v, UniteLongueur u);
double depuisMillimetres(double mm, UniteLongueur u);
double versDegres(double v, UniteAngle u);
double depuisDegres(double deg, UniteAngle u);
UniteLongueur uniteLongueurDepuis(const std::string& s);  // « mm », « cm », « m », « in », « ft »
UniteAngle uniteAngleDepuis(const std::string& s);        // « deg », « rad »

// Lit une saisie : nombres (virgule ou point), opérations + - * / et parenthèses, unités collées ou
// séparées (mm, cm, m, in, ", ft, ', °, deg, rad). Sans unité, celle du document. Résultat en mm ou en
// degrés. Faux, avec une explication, si la saisie est illisible ou mélange longueurs et angles.
// Grandeur lue avec sa dimension : 0 nombre pur, 1 longueur (mm), 2 angle (degrés)
struct Grandeur {
  double valeur = 0;
  int dimension = 0;
};
bool lireGrandeur(const std::string& texte, const Unites& document, Grandeur& resultat, std::string* erreur = nullptr,
                  const std::map<std::string, Grandeur>* variables = nullptr);
bool lireValeur(const std::string& texte, bool angle, const Unites& document, double& resultat, std::string* erreur,
                const std::map<std::string, Grandeur>* variables);  // variables avec leur dimension
bool lireValeur(const std::string& texte, bool angle, const Unites& document, double& resultat, std::string* erreur = nullptr,
                const std::map<std::string, double>* variables = nullptr);  // variables de la pièce, en mm ou en degrés
// Écrit une valeur interne (mm ou degrés) dans l'unité du document, avec son symbole.
std::string ecrireValeur(double interne, bool angle, const Unites& document, bool avecSymbole = true);
// Écrit une aire (mm² en interne) dans l'unité du document au carré, avec son symbole (« 1 cm² »).
std::string ecrireAire(double mm2, const Unites& document);

}  // namespace prom
