// Prométhée : boîte à calculs de l'électricien. Chaque calculateur est une petite fiche : des champs (nombres ou
// listes de choix) avec leurs valeurs usuelles, un calcul qui rend ses résultats, la formule appliquée, ce que dit
// le résultat en mots simples, et les avis (conforme, à surveiller, à corriger) avec la règle qui s'y rattache.
// Sections, chutes de tension, courants, courts-circuits, longueurs protégées, contraintes thermiques, conducteur
// de protection, compensation, transformateur, démarrage des moteurs, pertes et leur coût, autonomie d'une
// batterie, prise de terre, éclairage, éclairage de sécurité, loi d'Ohm, jauges AWG, échauffement d'une armoire.
// Aucune dépendance à l'interface. Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "promethee/electrique_distribution.hpp"

namespace prom::elec {

struct ChampCalcul {
  std::string id, nom, unite;
  double valeur = 0, min = 0, max = 1e9;
  int decimales = 2;
  std::vector<std::string> choix;  // liste de choix (la valeur est l'indice) ; vide : un nombre
  std::string aide;
};
struct LigneResultat {
  std::string nom, valeur;
  bool principal = false;  // le résultat que l'on cherche, mis en avant
};
struct ResultatCalcul {
  std::vector<LigneResultat> lignes;
  std::vector<AvisCalcul> avis;
  std::string formule;      // la formule appliquée, lisible
  std::string explication;  // ce que dit le résultat, en mots simples
};
struct Calculateur {
  std::string id, nom, famille, description, regle;
  std::vector<ChampCalcul> champs;
  std::function<ResultatCalcul(const std::vector<double>&)> calculer;
};
const std::vector<Calculateur>& calculateursElectriques();
const Calculateur* calculateur(const std::string& id);
// Familles, dans l'ordre d'affichage
const std::vector<std::string>& famillesCalculateurs();
// Calcule avec ces valeurs (dans l'ordre des champs ; les manquantes prennent leur valeur par défaut, toutes sont
// ramenées dans leurs bornes)
ResultatCalcul executer(const Calculateur& c, std::vector<double> valeurs = {});

}  // namespace prom::elec
