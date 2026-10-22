// Prométhée : formules des valeurs électroniques (lot L3, tâche 3.14 ; SAI-44, ERG-12). Un évaluateur sans
// dépendance nouvelle, dans l'esprit de la lecture des grandeurs de unites.cpp : notation ingénieur (4k7, 2u2,
// 100 nF), unités électriques suivies dans les calculs (Ω × F donne des secondes, leur inverse des hertz), nombres
// complexes (3 + 4j), matrices ([1, 2; 3, 4]), fonctions du métier (résistance d'une LED, arrondi à une série E,
// mise en parallèle). Les repères des composants (R1) et les variables de la conception y sont des grandeurs.
// Les valeurs calculées se recalculent dans l'ordre de leurs dépendances, cycles refusés ; un texte de feuille
// montre un calcul tenu à jour entre « {= » et « } ».
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "promethee/valeurs.hpp"

namespace prom::ecad {

struct ConceptionElec;

// Dimension en unités SI de base : kg, m, s, A
struct Dimension {
  std::array<std::int8_t, 4> e{};
  bool sans() const { return e == std::array<std::int8_t, 4>{}; }
  friend bool operator==(const Dimension&, const Dimension&) = default;
};
Dimension dimensionDeFamille(FamilleValeur f);
std::string symboleDimension(const Dimension& d);  // « Ω », « Hz »… ; composée sinon (« kg·m²·s⁻³ »)

// Grandeur : un scalaire ou une matrice de complexes, avec une dimension commune
struct Quantite {
  std::vector<std::complex<double>> v{std::complex<double>{}};
  int lignes = 1, colonnes = 1;
  Dimension dim{};
  bool scalaire() const { return lignes == 1 && colonnes == 1; }
  bool reelle() const;
  double reel() const { return v.front().real(); }
  static Quantite nombre(double x, Dimension d = {}) {
    Quantite q;
    q.v = {x};
    q.dim = d;
    return q;
  }
};

struct ResultatFormule {
  bool ok = false;
  Quantite q;
  std::string erreur;
  std::vector<std::string> noms;  // noms lus (repères, variables), pour l'ordre des dépendances
};
// Le contexte donne la grandeur d'un nom (repère, variable) ; vide : nom inconnu
using ContexteFormule = std::function<std::optional<Quantite>(const std::string& nom)>;
ResultatFormule evaluerFormule(const std::string& texte, const ContexteFormule& contexte = {});
// Noms lus par une formule, sans l'évaluer (repères, variables ; ni fonctions ni constantes)
std::vector<std::string> nomsDeFormule(const std::string& texte);
// « 1,59 kHz », « 3 + 4j Ω », « [1, 2; 3, 4] »
std::string ecrireQuantite(const Quantite& q, int chiffres = 3);

// Valeurs calculées de la conception, recalculées dans l'ordre de leurs dépendances ; une formule en cycle, qui
// lit un nom inconnu ou qui ne donne pas l'unité de sa famille garde sa dernière valeur et donne une erreur
struct ErreurFormule {
  std::string composant, reference, message;
};
std::vector<ErreurFormule> recalculerFormules(ConceptionElec& c);
// La grandeur d'un nom dans la conception : valeur comprise d'un composant (avec la dimension de sa famille), ou
// variable de la conception
std::optional<Quantite> grandeurDe(const ConceptionElec& c, const std::string& nom);
// Un texte de feuille où chaque « {=…} » est remplacé par son résultat (« fc = {=1/(2π·R3·C5)} » → « fc = 1,59 kHz ») ;
// une formule fausse s'écrit « {?…} »
std::string texteCalcule(const ConceptionElec& c, const std::string& texte);

}  // namespace prom::ecad
