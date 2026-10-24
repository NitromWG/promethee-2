// Prométhée : approvisionnement hors ligne (lot L4, tâche 4.18 ; BIB-11, BIB-30, BIB-31, BIB-32). Offres des
// distributeurs comparées au conditionnement et aux paliers de prix ; secondes sources validées, compatibilité
// d'empreinte et de brochage vérifiée, remplaçants proposés avec un indice de confiance ; stock de l'atelier et
// catalogue de l'assembleur lus d'un CSV ou d'un XLSX, qui classent la recherche et donnent le reste à acheter pour
// N cartes. Rien ne passe par le réseau : les offres sont celles que porte la bibliothèque ou un fichier importé.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct LigneStock {
  std::string mpn, fabricant, valeur, empreinte, lcsc, emplacement, description;
  int quantite = 0;     // −1 : catalogue sans limite connue
  double prix = 0;      // prix unitaire, 0 inconnu
  bool basique = false; // catalogue d'assembleur : pièce « de base », sans frais de bobine
};
struct SourceStock {
  std::string nom;           // « Tiroirs de l'atelier », « Catalogue JLCPCB »
  bool stockPropre = true;   // stock de l'atelier : consommé d'abord ; sinon catalogue, qui classe la recherche
  std::vector<LigneStock> lignes;
};
// Lit un stock ou un catalogue (CSV, point-virgule, tabulations ou XLSX) ; en-têtes reconnus en français et en anglais :
// MPN / Référence fabricant / Manufacturer Part, Fabricant / Manufacturer, Valeur / Value / Comment, Boîtier /
// Empreinte / Footprint / Package, LCSC / LCSC Part, Quantité / Stock / Qty, Prix / Price, Emplacement / Location,
// Type (« Basic »)
SourceStock lireSourceStock(const std::string& contenu, const std::string& nom, bool stockPropre, std::string* erreur = nullptr);

// Taille d'un boîtier de puce lue dans un identifiant (« R_0603 », « C_0603_1608Metric », « 0603 » → « 0603 »)
std::string codeTaille(const std::string& empreinte);
// Une ligne de stock couvre une ligne de nomenclature : même référence fabricant, même référence LCSC, ou, pour un
// passif, même valeur (comprise comme un nombre) et même taille de boîtier
bool couvre(const LigneStock& s, const LigneNomenclature& l);

// Meilleure offre pour une quantité : le total le plus bas, quantité arrondie au conditionnement, palier atteint
struct ChoixOffre {
  const OffreArticle* offre = nullptr;
  int quantite = 0;        // quantité commandée (multiple du conditionnement)
  double prixUnitaire = 0, total = 0;
};
ChoixOffre meilleureOffre(const std::vector<OffreArticle>& offres, int quantite);

struct BesoinAchat {
  LigneNomenclature ligne;
  int besoin = 0;          // quantité par carte × cartes
  int enStock = 0;         // pris dans le stock de l'atelier
  int aAcheter = 0;
  std::string source;      // stock ou catalogue qui la couvre ; offre retenue (« Mouser 652-CR0603… »)
  int quantiteCommandee = 0;
  double prixUnitaire = 0, montant = 0;
};
// Reste à acheter pour N cartes : le stock de l'atelier d'abord, puis la meilleure offre de l'article (ou d'une ligne
// de catalogue) pour le reste
std::vector<BesoinAchat> besoinsAchat(const ConceptionElec& c, int cartes, const std::vector<SourceStock>& sources, const std::vector<Article>& articles = {});
std::string csvBesoins(const std::vector<BesoinAchat>& b);

// Compatibilité d'un remplaçant : même famille de boîtier et même nombre de pastilles, brochage qui envoie les mêmes
// broches sur les mêmes pastilles ; la raison dit ce qui diffère
bool remplacantCompatible(const Article& a, const Article& b, std::string* raison = nullptr);
// « Chercher des semblables » : candidats classés par un indice de confiance (0 à 1) tiré de la catégorie, de la
// valeur, du boîtier, des paramètres et du brochage ; les sources classées (stock, catalogue basique) d'abord
struct Semblable {
  std::string id;
  double confiance = 0;
  std::string raison;
  bool enStock = false;
};
std::vector<Semblable> chercherSemblables(const Article& a, const std::vector<Article>& candidats, const std::vector<SourceStock>& sources = {});
// Secondes sources d'un article, rangées par rang puis par cycle de vie (actif, NRND, fin de vie)
std::vector<SecondeSource> secondesClassees(const Article& a);

}  // namespace prom::ecad
