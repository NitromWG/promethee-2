// Prométhée : liste d'achat par conditionnements et tableau de bord de la nomenclature (lot L19 de la 0.34 ; 19.7).
//
// Pour N cartes de plusieurs projets, chaque ligne compte ses pertes, puis chaque conditionnement de chaque source
// (bobine, bande coupée, plateau, tube) arrondit au minimum et au multiple de commande ; le port (franco compris) et
// la douane se comptent selon le pays du fournisseur, d'après un fichier de données. Le choix des sources par prix,
// stock et cycle de vie se résout exactement par énumération des ensembles de fournisseurs quand ils sont peu
// nombreux (le solveur HiGHS, facultatif, n'est pas lié) ; sinon chaque ligne prend la source la moins chère et le
// port se compte après. Le tableau de bord de la nomenclature donne une note sur 10 (complétude, coût, délai,
// risque), ne recalcule que les lignes touchées, et se présente aux acheteurs en page HTML autonome.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct Conditionnement {
  std::string genre = "bande coupée";       // « bobine », « bande coupée », « plateau », « tube », « vrac »
  int pieces = 1;                           // pièces par conditionnement (5 000 pour une bobine, 1 à l'unité)
  int minimum = 1, multiple = 1;            // commande : au moins, par pas de (en pièces)
  std::vector<std::pair<int, double>> prix;  // quantité minimale → prix unitaire
  friend bool operator==(const Conditionnement&, const Conditionnement&) = default;
};
struct SourceAchat {
  std::string fournisseur, pays = "FR", reference;
  std::vector<Conditionnement> conditionnements;
  int stock = -1;                  // −1 : inconnu
  int delai = 0;                   // jours
  std::string cycleVie = "actif";  // « actif », « NRND », « fin de vie », « obsolète »
  friend bool operator==(const SourceAchat&, const SourceAchat&) = default;
};
struct LigneAchat {
  std::string cle, designation;  // cle : article ou référence fabricant
  int parCarte = 1;
  double pertes = 0;             // part en plus (0,02 : 2 %)
  int cartes = 0;                // 0 : celles des options ; une ligne d'un autre projet porte les siennes
  std::vector<SourceAchat> sources;
};
struct FraisPays {
  double port = 0, franco = 0;  // € ; port offert au-delà de « franco » (0 : jamais offert)
  double douane = 0;            // part du montant (0,04 : 4 %)
};
// Fichier de données : {"livraison":"FR","pays":{"FR":{"port":6,"franco":50},"US":{"port":20,"douane":0.04}},
// "fournisseurs":{"Mouser":"US","LCSC":"CN"}}
struct FraisAchat {
  std::string livraison = "FR";
  std::map<std::string, FraisPays> pays;            // pays → frais
  std::map<std::string, std::string> fournisseurs;  // fournisseur → pays (sinon celui de la source)
};
std::optional<FraisAchat> lireFraisAchat(const std::string& json, std::string* erreur = nullptr);
struct OptionsAchat {
  int cartes = 1;
  FraisAchat donnees;
  int fournisseursMax = 12;  // au-delà, résolution ligne par ligne
};
struct ChoixLigne {
  std::string cle, designation, fournisseur, reference, conditionnement;  // reference : chez le fournisseur
  int besoin = 0, quantite = 0, lots = 0;  // pièces nécessaires, achetées, conditionnements
  double prixUnitaire = 0, cout = 0;
  std::string raison;  // pourquoi cette source et ce conditionnement
};
struct ListeAchat {
  std::vector<ChoixLigne> lignes;
  std::vector<std::string> fournisseurs;
  double sousTotal = 0, port = 0, douane = 0, total = 0;
  std::string methode;  // « exacte (ensembles de fournisseurs) », « ligne par ligne »
  std::vector<std::string> alertes;
};
// Lignes d'achat de plusieurs projets réunis (19.6) : chaque conception avec son nombre de cartes ; les composants
// de même référence fabricant (ou de même article) se cumulent, les offres des articles deviennent les sources
// (conditionnement de plus d'une pièce : bobine, sinon bande coupée)
std::vector<LigneAchat> lignesAchat(const std::vector<std::pair<const ConceptionElec*, int>>& projets, double pertes = 0.02, const FraisAchat* donnees = nullptr);
// Besoin d'une ligne : cartes × par carte, pertes comprises, arrondi à la pièce supérieure
int besoinLigne(const LigneAchat& l, int cartes);
// Coût d'un conditionnement pour un besoin : quantité achetée (minimum, multiple, conditionnements entiers), prix
std::optional<ChoixLigne> coutConditionnement(const Conditionnement& k, int besoin);
ListeAchat calculerListeAchat(const std::vector<LigneAchat>& lignes, const OptionsAchat& o);
std::string csvListeAchat(const ListeAchat& l);
// Fichier de panier d'un distributeur, d'après un gabarit de colonnes modifiable : en-tête → champ (« reference »,
// « mpn », « quantite », « designation », « cle »)
struct GabaritPanier {
  std::string distributeur;
  char separateur = ',';
  std::vector<std::pair<std::string, std::string>> colonnes;
};
const std::vector<GabaritPanier>& gabaritsPanier();  // Mouser, Digi-Key, LCSC
std::optional<GabaritPanier> lireGabaritPanier(const std::string& json, std::string* erreur = nullptr);
std::string texteGabaritPanier(const GabaritPanier& g);
// Lignes de la liste chez ce distributeur (sans casse ni tiret : « digikey » vaut « Digi-Key »)
std::string panierDistributeur(const ListeAchat& l, const GabaritPanier& g);

// ---------- Tableau de bord de la nomenclature ----------
struct NoteLigneNomenclature {
  std::string cle, designation;
  int quantite = 0;
  double completude = 0, cout = 0, delai = 0, risque = 0;  // chacun sur 10
  double prix = -1;  // € l'unité, −1 inconnu
  std::vector<std::string> remarques;
  std::string empreinte;  // signature des données de la ligne : recalcul des seules lignes touchées
};
struct TableauBordNomenclature {
  double note = 0;  // sur 10
  std::vector<NoteLigneNomenclature> lignes;
  int recalculees = 0;  // lignes recalculées au dernier passage
  double coutTotal = 0;
  int lignesCompletes = 0;
};
// Recalcule les lignes dont les données ont changé (les autres sont reprises de « avant »)
TableauBordNomenclature tableauBordNomenclature(const ConceptionElec& c, const TableauBordNomenclature* avant = nullptr);
std::string htmlTableauBord(const TableauBordNomenclature& t, const std::string& titre);  // page autonome, graphiques en SVG

}  // namespace prom::ecad
