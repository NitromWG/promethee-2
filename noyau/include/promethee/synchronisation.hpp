// Prométhée : synchronisation continue du schéma et du circuit (lot L3, tâches 3.6, 3.7, 3.18 et 3.19 ; A4, P6, C22,
// S18 ; ERC-13, PLA-16 à PLA-18, ROU-34). Pour chaque composant, le schéma dit ce que la carte devrait porter (état
// attendu : présence, empreinte, réseau de chaque pastille) ; le circuit dit ce qu'elle porte (état réel : empreinte
// placée et cuivre qui la touche). Chaque différence est un écart, classé selon le tableau de la spécification :
// les écarts sans risque s'appliquent dans la commande qui les a causés, les autres attendent, visibles, qu'on les
// applique ou qu'on les voie. Rien n'est jamais défait en silence : un composant routé supprimé du schéma laisse
// un écart en attente, pas des pistes orphelines. Une empreinte changée se remplace en place : même point de
// placement, même rotation, pistes et vias ramenées sur les pastilles de la même broche.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Numéros du tableau de la spécification (1 à 17), plus la liaison tracée au circuit entre des pastilles que le
// schéma ne relie pas (ROU-34)
enum class GenreEcart : std::uint8_t {
  Nouveau = 1,              // 1 : composant nouveau, empreinte créée à placer
  SupprimeLibre = 2,        // 2 : composant supprimé, aucun cuivre : empreinte retirée
  SupprimeRoute = 3,        // 3 : composant supprimé, du cuivre touche ses pastilles
  EmpreinteRemplacee = 4,   // 4 : empreinte changée, chaque pastille routée a son pendant : remplacement en place
  PastillePerdue = 5,       // 5 : empreinte changée, une pastille routée n'a pas de pendant
  ReseauRenomme = 8,        // 8 : réseau renommé, mêmes liaisons : cuivre renommé
  BrocheRoutee = 10,        // 10 : broche passée à un autre réseau, du cuivre porte l'ancien
  HorsCarteLibre = 11,      // 11 : hors carte coché, aucun cuivre : empreinte retirée
  HorsCarteRoute = 12,      // 12 : hors carte coché, du cuivre
  HorsCarteDecoche = 13,    // 13 : hors carte décoché : empreinte à placer
  Verrouillee = 16,         // 16 : empreinte verrouillée sur la carte, changée au schéma
  LiaisonCircuit = 18,      // piste entre pastilles que le schéma ne relie pas : réseau nouveau proposé au schéma
};
const char* nomGenreEcart(GenreEcart g);  // « Composant nouveau », « Empreinte changée »…

struct EcartSynchro {
  GenreEcart genre = GenreEcart::Nouveau;
  std::string composant, reference;  // UUID et repère
  std::string description;           // « R3 supprimé du schéma · 4 pistes touchent ses pastilles »
  bool sansRisque = false;
  std::string avant, apres;          // empreinte, réseau ou nom, selon le genre
  std::string broche;                // pastille concernée (5, 10)
  std::vector<std::string> pastilles;  // liaison au circuit : « R1:1 », « R2:2 »
  int cuivre = 0;                    // pistes et vias qui touchent
  std::string cle() const;           // identité stable de l'écart (genre, composant, broche, avant)
};

// Mode de la conception : automatique pour les changements sans risque (par défaut), ou tout sur confirmation
// (ConceptionElec::synchroSurConfirmation)
std::vector<EcartSynchro> ecartsSynchronisation(const ConceptionElec& c);
std::vector<EcartSynchro> ecartsSynchronisation(const ConceptionElec& c, const Connectivite& k);
// Applique des écarts (ceux du panneau, cochés, ou tous) ; rend le nombre d'écarts appliqués. Tout se fait sur la
// conception passée : l'appelant en fait une seule commande.
int appliquerEcarts(ConceptionElec& c, const std::vector<EcartSynchro>& ecarts);
// Après une commande : applique les écarts sans risque, sauf en mode « sur confirmation » ; rend ceux qui attendent
std::vector<EcartSynchro> synchroniser(ConceptionElec& c);

// Suites d'une modification (3.6, 3.14) : valeurs calculées recalculées, puis écarts sans risque appliqués quand la
// modification touche le circuit, les réseaux ou les empreintes ; rend vrai si la conception a changé
struct ChangementConception;
bool appliquerSuites(ConceptionElec& c, const ChangementConception& ch);

// Remplacement en place (3.7, P6) : la nouvelle empreinte garde le point de placement (centre des pastilles), la
// rotation, la face et le verrou ; chaque bout de piste et chaque via qui touchait une pastille reste s'il tombe
// dans la nouvelle pastille de la même broche, sinon il vient en son centre. Faux si l'empreinte est inconnue.
bool remplacerEmpreinteEnPlace(ConceptionElec& c, const std::string& composant, const std::string& nouvelle);

// Cuivre qui touche une pastille posée : pistes (par un bout ou le long) et vias, sur une couche commune
struct CuivreTouchant {
  std::vector<std::size_t> pistes, vias;
  int nombre() const { return static_cast<int>(pistes.size() + vias.size()); }
};
CuivreTouchant cuivreSurPastille(const ConceptionElec& c, const EmpreintePlacee& e, const Pastille& p);

// Composants qui n'ont plus de symbole au schéma (suppression) : retirés de la conception, sauf ceux dont l'empreinte
// touche du cuivre, gardés pour que l'écart en attente nomme leur repère ; l'empreinte, elle, est l'affaire de la
// synchronisation (retirée sans risque, ou en attente). Rend le nombre de composants retirés.
int retirerComposantsSansSymbole(ConceptionElec& c, const std::set<std::string>& composants);

// Empreintes « à placer » (PLA-01) : posées hors du contour de la carte, en attente d'une place
bool aPlacer(const ConceptionElec& c, const EmpreintePlacee& e);

// Réseau renommé au circuit (3.18, PLA-16) : le cuivre prend le nouveau nom, et le schéma aussi, dans la même
// commande : étiquettes et alimentations renommées, ou, pour un réseau qui n'avait pas de nom, une étiquette posée
// sur une de ses broches. Rend le nombre d'objets renommés ou posés au schéma.
int renommerReseauDepuisCircuit(ConceptionElec& c, const std::string& ancien, const std::string& nouveau);

// Appariement d'une carte importée sans liens (3.19, PLA-17, PLA-18) : chaque empreinte placée est rattachée à un
// composant du schéma par son repère, puis par sa valeur et son empreinte ; les orphelins restants par
// appariement de coût minimal (algorithme hongrois sur la distance des repères, valeurs et empreintes)
struct Appariement {
  std::size_t empreinte = 0;   // indice dans circuit.empreintes
  std::string composant;       // UUID du composant du schéma
  enum class Par : std::uint8_t { Repere, ValeurEmpreinte, CoutMinimal, Main } par = Par::Repere;
  double cout = 0;
};
struct ResultatAppariement {
  std::vector<Appariement> paires;
  std::vector<std::size_t> empreintesSeules;  // sans composant
  std::vector<std::string> composantsSeuls;   // UUID sans empreinte
};
// « carte » : empreintes placées d'un circuit dont les liens sont perdus (UUID qui ne désignent plus les
// composants), avec, pour chacune, son repère, sa valeur et son empreinte lus à l'import
struct EmpreinteSansLien {
  std::string reference, valeur, empreinte;
};
ResultatAppariement apparier(const ConceptionElec& c, const std::vector<EmpreinteSansLien>& carte, bool coutMinimal = true);
// Repère, valeur et empreinte de chaque empreinte placée d'une carte (importée), dans l'ordre de ses empreintes
std::vector<EmpreinteSansLien> empreintesSansLien(const ConceptionElec& source);
// Report d'une carte sans liens (3.19) : chaque empreinte appariée de « source » prend place dans « cible », sur le
// composant de sa paire (position, rotation, face, verrou ; définition de l'empreinte recopiée) ; contour, empilage,
// règles, pistes, vias, zones et textes de la source sont repris. Les noms de réseaux du cuivre restent ceux de la
// source : la synchronisation montre ensuite ce qui diffère. Rend le nombre d'empreintes reportées.
int reporterCarte(ConceptionElec& cible, const ConceptionElec& source, const std::vector<Appariement>& paires);

// Algorithme hongrois (Kuhn-Munkres) sur une matrice de coûts rectangulaire ; rend, pour chaque ligne, la colonne
// choisie (−1 si aucune)
std::vector<int> affectationCoutMinimal(const std::vector<std::vector<double>>& couts);

}  // namespace prom::ecad
