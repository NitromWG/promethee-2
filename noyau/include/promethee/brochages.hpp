// Prométhée : brochages lus (lot L13 de la 0.34 ; 13.5 ; SYM-11, BIB-15). Fichier BSDL (IEEE 1149.1) d'un circuit :
// entité, boîtier, ports (direction, vecteurs) et chaîne PIN_MAP_STRING qui donne la broche physique de chaque port ;
// tableau de brochage d'une fiche technique en CSV (numéro, nom, type) ; le symbole en est tiré, une unité par banc
// de ports (PA, PB…) et une pour l'alimentation quand le circuit est grand. Consolidation des passifs : les articles
// d'une même valeur, d'un même boîtier et d'une même tolérance se regroupent. Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "promethee/schema.hpp"

namespace prom::ecad {

struct BrocheLue {
  std::string numero, nom;
  std::string direction;  // « in », « out », « inout », « buffer », « linkage » (BSDL) ; type écrit (tableau)
  friend bool operator==(const BrocheLue&, const BrocheLue&) = default;
};
struct Brochage {
  std::string entite, boitier;
  std::vector<BrocheLue> broches;  // triées par numéro
  std::vector<std::string> alertes;
};
// BSDL ; std::nullopt et la raison si l'entité, les ports ou la chaîne de brochage manquent
std::optional<Brochage> lireBsdl(const std::string& texte, std::string* erreur = nullptr);
// Tableau d'une fiche (CSV, point-virgule, tabulations) : colonnes numéro (ou broche), nom (ou signal), type
std::optional<Brochage> lireTableauBrochage(const std::string& texte, std::string* erreur = nullptr);
TypeBroche typeDeBrocheLue(const BrocheLue& b);  // linkage VDD… → alimentation, in → entrée…
// Symbole tiré du brochage : « unitesParBanc » une unité par banc de ports et une pour l'alimentation
DefSymbole symboleDepuisBrochage(const Brochage& b, const std::string& nom, bool unitesParBanc = true);

// Consolidation des passifs (BIB-15) : articles de même famille, valeur normalisée, boîtier et tolérance
struct GroupePassifs {
  std::string cle;                    // « R 10k 0603 1 % »
  std::vector<std::string> articles;  // identifiants, le premier gardé
};
std::vector<GroupePassifs> consoliderPassifs(const std::vector<Article>& articles);

}  // namespace prom::ecad
