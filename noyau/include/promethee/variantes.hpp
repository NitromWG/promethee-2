// Prométhée : variantes de montage et coût de la nomenclature (lot L12 ; 12.1, 12.5, 12.7, 12.18 ; STR-07, BIB-12,
// FAB-16, FAB-17). Une variante change, composant par composant, le montage, la valeur, l'article ou l'empreinte ;
// la conception « résolue » d'une variante sert à la nomenclature, au placement et aux vues. Le coût se calcule
// aux prix par palier des offres des articles, pour un nombre de cartes donné. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom {
struct Projet;
}

namespace prom::ecad {

// Composant tel qu'une variante le monte
struct ComposantResolu {
  bool monte = true;
  std::string valeur, article, empreinte;
  bool valeurChangee = false, articleChange = false, empreinteChangee = false;
};
ComposantResolu resoudre(const ComposantElec& k, const std::string& variante);

// La conception telle que la variante la monte : valeurs, articles (et leurs références fabricant) et empreintes
// remplacés, non-montés marqués « ne pas monter ». Variante vide : la carte de base, inchangée.
ConceptionElec conceptionPourVariante(const ConceptionElec& c, const std::string& variante);

// Déclaration des variantes ; renommer et supprimer font suivre les surcharges des composants
bool ajouterVariante(ConceptionElec& c, const std::string& nom, const std::string& description = {});
bool renommerVariante(ConceptionElec& c, const std::string& ancien, const std::string& nouveau);
bool supprimerVariante(ConceptionElec& c, const std::string& nom);
// Surcharge d'un composant (repère) dans une variante ; une surcharge vide est retirée
bool reglerSurcharge(ConceptionElec& c, const std::string& reference, const std::string& variante, const SurchargeVariante& s);

// Incohérences : variante citée par un composant mais non déclarée, variante active inconnue, article ou empreinte
// de remplacement introuvables
std::vector<std::string> verifierVariantes(const ConceptionElec& c);

// Tableau des variantes : une ligne par composant que change au moins une variante, une cellule par variante
// (« non monté », « 4k7 », « = » quand rien ne change)
struct LigneVariantes {
  std::string reference, base;
  std::vector<std::string> cellules;
};
std::vector<LigneVariantes> tableauVariantes(const ConceptionElec& c);

// ---------- Coût (12.5, 12.18) ----------
struct LigneCout {
  std::vector<std::string> references;
  std::string article, mpn, valeur, distributeur, devise = "EUR";
  int parCarte = 0, commandee = 0;  // commandée : arrondie au conditionnement et au premier palier
  double prixUnitaire = 0, total = 0;
  bool prixConnu = false;
};
struct CoutNomenclature {
  int cartes = 1;
  std::vector<LigneCout> lignes;
  double total = 0, parCarte = 0;
  int lignesSansPrix = 0;
  std::string devise = "EUR";
};
// Prix d'une quantité dans une offre : palier le plus haut atteint ; sous le premier palier, on en commande le
// minimum. Rend faux si l'offre n'a aucun prix.
bool prixPourQuantite(const OffreArticle& o, int quantite, double& prixUnitaire, int& commandee);
CoutNomenclature coutNomenclature(const ConceptionElec& c, int cartes, const std::string& variante = {});

// Nomenclature commune du projet (12.18) : une ligne par article (ou valeur et empreinte), une colonne de quantité
// par carte ; coût pour n systèmes
struct LigneNomenclatureProjet {
  std::string cle, valeur, empreinte, mpn, article;
  std::vector<int> parCarte;  // dans l'ordre des conceptions du projet
  int total = 0;
  double prixUnitaire = 0;
  bool prixConnu = false;
};
struct NomenclatureProjet {
  std::vector<std::string> cartes;
  std::vector<LigneNomenclatureProjet> lignes;
  double coutSysteme = 0;  // pour un système, aux prix du palier de « systemes »
  int systemes = 1;
};
NomenclatureProjet nomenclatureProjet(const Projet& p, int systemes = 1);

}  // namespace prom::ecad
