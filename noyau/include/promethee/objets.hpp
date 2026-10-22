// Prométhée : tableau de tous les objets d'une conception électronique (lot L3, tâche 3.21 ; ERG-41). Chaque objet
// est une ligne, ses enfants à la suite (un composant, puis ses symboles posés et son empreinte) ; chaque propriété
// est une colonne, lue et écrite en texte : un nom, une valeur, une position en millimètres, un angle en degrés, une
// largeur, un réseau. Une colonne entière se modifie par une règle (formule en « x », remplacement « de → à »,
// préfixe, suffixe) ou par un collage depuis un tableur ; l'interface met le tout en une seule commande.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/remplacement.hpp"

namespace prom::ecad {

enum class GenreObjet : std::uint8_t { Composant, Symbole, Empreinte, Fil, Etiquette, TexteSchema, Piste, Via, Zone, TexteCarte };
const char* nomGenreObjet(GenreObjet g);  // « Composant », « Symbole »…

enum class ProprieteObjet : std::uint8_t { Nom, Lieu, X, Y, Rotation, Largeur, Valeur, Reseau };
inline constexpr int NOMBRE_PROPRIETES_OBJET = 8;
const char* nomProprieteObjet(ProprieteObjet p);  // « Nom », « Feuille ou couche », « X (mm) »…
bool proprieteNumerique(ProprieteObjet p);          // X, Y, Rotation, Largeur

struct LigneObjet {
  GenreObjet genre = GenreObjet::Composant;
  std::string id;      // UUID de l'objet ; pour une empreinte, celui de son composant
  int feuille = -1;    // objets du schéma : rang de la feuille
  int profondeur = 0;  // 1 : enfant d'un composant
  std::string parent;  // repère du composant parent
  std::map<ProprieteObjet, std::string> textes;  // propriétés que l'objet a, en texte
  std::set<ProprieteObjet> modifiables;
};
std::vector<LigneObjet> objetsConception(const ConceptionElec& c);

// Nombres du tableau : virgule décimale, quatre décimales au plus ; la lecture accepte aussi le point, une unité
// (« mm », « ° ») et une formule après « = »
std::string ecrireNombreObjet(double v);
std::optional<double> lireNombreObjet(const std::string& texte);

// Écrit une propriété d'un objet ; faux, avec la raison, si le texte ne convient pas ou si la propriété ne se modifie
// pas. Les objets du schéma se déplacent fils accrochés (comme un glissement), une empreinte verrouillée refuse.
bool ecrireProprieteObjet(ConceptionElec& c, const LigneObjet& l, ProprieteObjet p, const std::string& texte, std::string* erreur = nullptr);
// La règle appliquée au texte d'une cellule : pour une propriété numérique, « x » est le nombre de la cellule
std::optional<std::string> transformerCellule(const std::string& texte, ProprieteObjet p, const RegleTexte& r, std::string* erreur = nullptr);

}  // namespace prom::ecad
