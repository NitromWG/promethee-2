// Prométhée : modèle des contraintes du circuit imprimé (lots L5 et L8 : 5.12, 5.27, 8.14, 8.24, 8.26, 8.27). Une
// contrainte porte un minimum (vérifié par la DRC), une valeur préférée (suivie par le routeur et les zones) et un
// maximum, avec des valeurs propres aux couches externes et internes (« 0,15/0,2 ») ; elle vise une portée écrite
// dans le langage d'expressions commun (« classe == 'Puissance' », « reseau ~ 'USB_D?' », « region == 'Secteur' »),
// et une seule fonction de résolution dit laquelle l'emporte, avec sa justification. Les classes de réseaux et leurs
// affectations (Regles) restent la forme courte des règles les plus courantes. Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "promethee/ecad.hpp"

namespace prom::ecad {

enum class GenreContrainte : std::uint8_t {
  Isolement,        // entre deux objets de cuivre (portée A, portée B)
  Largeur,          // largeur de piste
  ViaDiametre,
  ViaPercage,
  EcartPaire,       // écart entre les deux pistes d'une paire différentielle
  NonCouple,        // longueur non couplée d'une paire (maximum)
  Longueur,         // longueur d'un réseau ou d'un signal (minimum, maximum)
  Retard,           // retard d'un signal, en picosecondes (min, max)
  NombreVias,       // vias d'un réseau (maximum)
  CouchesPermises,  // couches où le routage est permis (« couches »), priorité dans l'ordre des couches
  Interdiction,     // objets interdits dans la portée (« objets » : pistes, vias, pastilles, zones, empreintes, textes)
  Bord,             // distance au contour de la carte
  CuivreTrou,       // distance entre un cuivre et un perçage
  ParalleleMax,     // longueur parallèle maximale pour un écart donné (8.25)
  Courant,          // courant d'une branche, vérifié tronçon par tronçon (8.25)
  EcartComposants,  // écart entre encombrements de composants (assemblage, 8.14)
  Orientations,     // orientations permises des composants (assemblage, 8.14), en degrés dans « texte »
  MargeVernis,      // marge du vernis autour des pastilles
  MargePate,        // marge de la pâte (négative : réduite)
  PontVernis,       // pont de vernis minimal entre deux pastilles
  HauteurMax,       // hauteur maximale des composants (zone de placement, 5.24)
  Proximite,        // distance maximale entre deux objets (« C12 à 2 mm au plus de U3.7 », 5.30)
  ViasParPastille,  // 8.25 sortie des pastilles CMS : longueur maximale de la sortie vers sa via
  PateNonMontes,    // 8.14 : pas de pâte sous les non-montés (booléen)
  IsolementCouches,  // 8.25 isolement entre couches à travers l'épaisseur du diélectrique
  Ecart3D           // 6.12 écart minimal entre deux corps en 3D (composants, pièces montées), sur les formes exactes
};
const char* codeGenreContrainte(GenreContrainte g);   // « isolement », « largeur »… (fichiers)
const char* nomGenreContrainte(GenreContrainte g);    // « Isolement »… (interface)
bool genreContrainteDepuisCode(const std::string& code, GenreContrainte& g);

// Valeur d'une contrainte : minimum, préférée, maximum (0 : non fixé) ; les couches internes peuvent avoir les leurs
struct ValeurContrainte {
  Nm min = 0, pref = 0, max = 0;
  bool interneDistincte = false;  // « 0,15/0,2 » : la seconde valeur vaut pour les couches internes
  Nm minInterne = 0, prefInterne = 0, maxInterne = 0;
  Nm minPour(bool interne) const { return interne && interneDistincte ? minInterne : min; }
  Nm prefPour(bool interne) const { return interne && interneDistincte ? prefInterne : pref; }
  Nm maxPour(bool interne) const { return interne && interneDistincte ? maxInterne : max; }
  friend bool operator==(const ValeurContrainte&, const ValeurContrainte&) = default;
};
// « 0,15/0,2 » (mm, externe/interne) ou « 0,2 » → valeur minimale ; faux si le texte ne se lit pas
bool lireValeurContrainte(const std::string& texte, ValeurContrainte& v);
std::string texteValeurContrainte(const ValeurContrainte& v);

struct Contrainte {
  std::string nom;
  GenreContrainte genre = GenreContrainte::Isolement;
  // Portées dans le langage d'expressions (13.11) : vide, tout objet ; « porteeB » pour les contraintes entre deux
  // objets (isolement, écart de composants, proximité)
  std::string portee, porteeB;
  ValeurContrainte valeur;
  std::uint64_t couches = 0;  // couches visées ou permises (0 : toutes)
  std::string texte;          // interdiction : objets visés ; orientations : « 0,90,180,270 » ; booléen : « oui »
  int priorite = 0;           // à portée égale, la plus haute l'emporte ; sinon la plus stricte
  std::string jeu;            // jeu nommé auquel elle appartient (« Secteur », « USB ») ; vide : règles générales
  std::string justification;  // pourquoi cette valeur (citée par l'explication de 5.29)
  std::string source;         // d'où elle vient (« directive du schéma », « IPC-2221 table 6-1 »…)
  bool active = true;
  friend bool operator==(const Contrainte&, const Contrainte&) = default;
};

// Jeu de contraintes nommé (8.24) : appliqué à un réseau, une classe, un groupe ou une région ; il hérite de son
// parent ce qu'il n'écrit pas
struct JeuContraintes {
  std::string nom, parent, description;
  friend bool operator==(const JeuContraintes&, const JeuContraintes&) = default;
};

// Classe de composants (5.12, 5.27) : membres nommés et règle (expression) qui en ajoute
struct ClasseComposants {
  std::string nom;
  std::vector<std::string> membres;  // repères
  std::string regle;                 // expression (« prefixe == 'C' && boitier == '0402' »)
  friend bool operator==(const ClasseComposants&, const ClasseComposants&) = default;
};

// Paire différentielle (5.10, 8.27) : deux réseaux, largeur et écart, bornes de l'écart, longueur non couplée
struct PaireDifferentielle {
  std::string nom, positif, negatif, classe;
  Nm largeur = 0, ecart = 0, ecartMin = 0, ecartMax = 0, nonCoupleMax = 0;
  double impedance = 0;  // Ω différentiels visés
  friend bool operator==(const PaireDifferentielle&, const PaireDifferentielle&) = default;
};

// Groupe accordé en longueur ou en retard (8.27) : cible 0 = le plus long du groupe ; en retard, cible et
// tolérance sont en femtosecondes (1 ps = 1 000), sinon en nanomètres
struct GroupeAccord {
  std::string nom;
  std::vector<std::string> reseaux;  // réseaux ou signaux
  Nm cible = 0, tolerance = 250'000;
  bool retard = false;  // accord en retard (ps, d'après la vitesse de chaque couche) plutôt qu'en longueur
  friend bool operator==(const GroupeAccord&, const GroupeAccord&) = default;
};

// Signal (8.26) : suivi de broche à broche à travers les composants série, avec sa topologie et ses tronçons
enum class TopologieSignal : std::uint8_t { Libre, Chaine, FlyBy, Etoile, T };
struct TronconSignal {
  std::string de, vers;  // broches « U1.7 » ou nœuds de la topologie
  Nm longueurMax = 0, longueurMin = 0;
  friend bool operator==(const TronconSignal&, const TronconSignal&) = default;
};
struct Signal {
  std::string nom;
  std::vector<std::string> reseaux;  // réseaux traversés, dans l'ordre (à travers les résistances série)
  std::vector<std::string> ordre;    // broches dans l'ordre de raccordement imposé
  TopologieSignal topologie = TopologieSignal::Libre;
  std::vector<TronconSignal> troncons;
  friend bool operator==(const Signal&, const Signal&) = default;
};

// Profil d'impédance (8.21) : par couche, la largeur (et l'écart d'une paire) qui donne l'impédance visée
struct ProfilImpedance {
  std::string nom;
  double impedance = 50;     // Ω (différentielle pour une paire)
  bool differentiel = false;
  std::map<std::string, std::pair<Nm, Nm>> parCouche;  // code de couche → (largeur, écart)
  friend bool operator==(const ProfilImpedance&, const ProfilImpedance&) = default;
};

// Toutes les contraintes d'une carte, rangées avec ses règles
struct ContraintesCarte {
  std::vector<Contrainte> contraintes;
  std::vector<JeuContraintes> jeux;
  std::vector<ClasseComposants> classesComposants;
  std::vector<PaireDifferentielle> paires;
  std::vector<GroupeAccord> accords;
  std::vector<Signal> signaux;
  std::vector<ProfilImpedance> impedances;
  std::string tableIsolement = "IPC-2221";  // ou « CEI 60664-1 » (5.27)
  bool vide() const { return contraintes.empty() && jeux.empty() && classesComposants.empty() && paires.empty() && accords.empty() && signaux.empty() && impedances.empty() && tableIsolement == "IPC-2221"; }
  friend bool operator==(const ContraintesCarte&, const ContraintesCarte&) = default;
};

}  // namespace prom::ecad
