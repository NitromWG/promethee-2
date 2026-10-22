// Prométhée : nomenclature mécanique en tableau (M10) — pièces seules, premier niveau ou à niveaux, colonnes au
// choix (propriétés personnalisées comprises), repères stables, masses unitaires et totales ; sorties CSV et XLSX.
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "promethee/modele.hpp"

namespace prom {

enum class VueNomenclature {
  Pieces,         // une ligne par pièce (et par version symétrique), sous-assemblages développés
  PremierNiveau,  // les composants directs, sous-assemblages compris comme articles
  Niveaux,        // l'arbre entier, en retrait, numéroté « 2.1.3 »
};

struct ColonneNomenclature {
  std::string cle;     // « repere », « numero », « niveau », « quantite », « quantiteTotale », « reference »,
                       // « designation », « materiau », « fournisseur », « masseUnitaire », « masseTotale »,
                       // « prop:<nom> » pour une propriété personnalisée
  std::string titre;
  bool numerique = false;
};
// Colonnes proposées pour le projet : les colonnes fixes, puis une par propriété personnalisée rencontrée
std::vector<ColonneNomenclature> colonnesNomenclature(const Projet& p);
std::vector<std::string> colonnesParDefaut(VueNomenclature vue);

// Masse d'une pièce (géométrie construite × densité), fournie par l'appelant
struct MassePiece {
  double grammes = 0;
  bool connue = false;      // faux : pièce sans volume (rien construit, géométrie illisible)
  bool provisoire = false;  // densité non confirmée
};

struct CelluleNomenclature {
  std::string texte;        // pour l'écran et le CSV (virgule décimale)
  double nombre = 0;
  bool estNombre = false;   // écrite en nombre dans le classeur
};
struct TableauNomenclature {
  std::string titre;
  std::vector<ColonneNomenclature> colonnes;
  std::vector<std::vector<CelluleNomenclature>> lignes;
  std::vector<int> niveaux;          // retrait de chaque ligne (vue à niveaux), 0 ailleurs
  std::vector<char> assemblages;     // la ligne est un sous-assemblage
  int quantitePieces = 0;            // pièces au total, quantités comprises
  double masseTotale = 0;            // g
  bool masseComplete = true;         // faux si une pièce n'a pas de masse connue
  bool masseProvisoire = false;      // une densité au moins n'est pas confirmée
  std::string texteTotal() const;    // « 12 pièces · 1,234 kg (provisoire) »
};
// assemblage ≥ 0 : cet assemblage ; −1 : tout le projet (vue « pièces » seulement : assemblages racines et pièces
// hors assemblage). « masse » peut être vide : les colonnes de masse restent alors vides.
TableauNomenclature tableauNomenclature(const Projet& p, int assemblage, VueNomenclature vue, const std::vector<std::string>& colonnes,
                                        const std::function<MassePiece(int piece)>& masse = {});

// CSV (UTF-8 avec marque, « ; », CRLF), dernière ligne : le total
std::string csvNomenclature(const TableauNomenclature& t);
// Classeur XLSX (Office Open XML) d'une feuille : en-tête gras figé, retraits des niveaux, sous-assemblages en gras,
// quantités et masses en nombres, ligne de total
std::string xlsxNomenclature(const TableauNomenclature& t);

// Masse lisible : « 850 g », « 1,25 kg »
std::string texteMasse(double grammes);

}  // namespace prom
