// Prométhée : mise en panneau de copies liées (lot L8 de la 0.34 ; 8.16 ; FAB-12, FAB-22, FAB-23). Le panneau ne
// recopie pas les cartes : chaque copie désigne sa carte source (de ce projet ou d'un autre fichier) et le modèle de
// fabrication du panneau est refait d'après elle ; une piste déplacée dans la source se retrouve dans toutes ses
// copies. Rangées tête-bêche, languettes, mires, trous d'outillage, coupons, marque de rebut ; contrôle du panneau ;
// empilages comparés ; grille tarifaire datée du fabricant et recherche de la disposition la moins chère par carte.
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct CopiePanneau {
  std::string carte;        // UUID de la conception source
  double x = 0, y = 0;      // mm, coin bas gauche de la copie dans le panneau (repère de fabrication, Y vers le haut)
  bool teteBeche = false;   // tournée d'un demi-tour
};
struct ElementPanneau {
  std::string genre;        // « mire », « trouOutillage », « coupon », « rebut »
  double x = 0, y = 0;      // mm, centre
  double diametre = 1.0;    // mire : cuivre ; trou d'outillage : perçage
  double largeur = 0, hauteur = 0;  // coupon, marque de rebut
  std::string texte;        // coupon : « impédance 50 Ω » ; marque de rebut : vide
};
struct MiseEnPanneau {
  std::string nom = "Panneau";
  double largeur = 0, hauteur = 0;   // mm
  double rail = 5, ecart = 2;        // mm
  double largeurLanguette = 3, diametrePerforation = 0.5, pasPerforations = 0.8;
  int languettesParBord = 2;
  std::vector<CopiePanneau> copies;
  std::vector<ElementPanneau> elements;
};
nlohmann::ordered_json panneauVersJson(const MiseEnPanneau& p);
MiseEnPanneau panneauDepuisJson(const nlohmann::ordered_json& j);
// Le panneau d'une conception, gardé dans ses extensions (« panneau »)
std::optional<MiseEnPanneau> panneauDe(const ConceptionElec& c);
void reglerPanneau(ConceptionElec& c, const std::optional<MiseEnPanneau>& p);

// Grille de copies de la carte, rangées tête-bêche une sur deux au choix, cadre, trois mires et quatre trous
// d'outillage de 3 mm aux coins
MiseEnPanneau panneauEnGrille(const ConceptionElec& c, int colonnes, int lignes, double ecart = 2, double rail = 5, bool teteBeche = false);
// Modèle de fabrication du panneau, refait d'après les cartes sources (« cartes » : celles qu'il désigne)
struct ModelePanneau {
  ModeleFabrication modele;
  int copies = 0, languettes = 0, perforations = 0;
  std::vector<std::string> defauts;
};
ModelePanneau construirePanneau(const std::vector<const ConceptionElec*>& cartes, const MiseEnPanneau& p, const std::string& version = {});
// Contrôle en direct : copies hors du cadre ou qui se touchent (écart minimal), élément sur une copie, source
// introuvable, DRC des sources en erreur ; vide : le panneau sort
std::vector<std::string> controlerPanneau(const std::vector<const ConceptionElec*>& cartes, const MiseEnPanneau& p);
// Empilages des cartes du panneau comparés : une ligne par écart (vide : tous identiques)
std::vector<std::string> comparerEmpilages(const std::vector<const ConceptionElec*>& cartes);

// ---------- Grille tarifaire (8.16, FAB-23) ----------
struct FormatPanneau {
  std::string nom;
  double largeur = 0, hauteur = 0;     // mm, utiles
  double prixFixe = 0;                 // par commande (outillage, frais)
  double prixParDm2 = 0;               // par panneau
  std::vector<std::pair<int, double>> remises;  // à partir de N panneaux : facteur (0,9…)
};
struct GrilleTarifaire {
  std::string fabricant, date;  // date de la grille (AAAA-MM-JJ)
  std::vector<FormatPanneau> formats;
};
GrilleTarifaire grilleDepuisJson(const nlohmann::ordered_json& j);
nlohmann::ordered_json grilleVersJson(const GrilleTarifaire& g);
struct OffrePanneau {
  std::string format;
  int colonnes = 0, lignes = 0, cartesParPanneau = 0, panneaux = 0;
  bool tournee = false;  // carte tournée d'un quart de tour dans le format
  double prixTotal = 0, prixParCarte = 0;
};
// Toutes les dispositions possibles (formats, grilles, quart de tour), la moins chère par carte en tête ; « arret »
// interrompt la recherche menée sur un fil
std::vector<OffrePanneau> dispositions(const ConceptionElec& c, const GrilleTarifaire& g, int quantite, double ecart = 2, double rail = 5, const std::atomic<bool>* arret = nullptr);

}  // namespace prom::ecad
