// Prométhée : générateur IPC étendu et séries d'empreintes par nom (lot L4, tâches 4.25 et 4.26 ; EMP-03, EMP-04,
// EMP-05, EMP-20). Quatorze gabarits nouveaux : BGA, LGA, WLCSP, DFN, SON, SOD, MELF, SMA/SMB/SMC, SOT-89, DPAK,
// D²PAK, quartz CMS à quatre plots, PLCC et connecteur de bord ; cotes JEDEC des boîtiers courants lues dans leur nom
// (SOIC, TSSOP, SSOP, MSOP, QFN, TQFP, LQFP, SOT-23, puces) ; pattes en aile de mouette rognées pour garder l'écart
// entre pastilles, pastilles écartées du plot central, pâte du plot découpée en motif ; une liste de noms donne une
// série d'empreintes à une ou plusieurs densités, contrôlée ; procédé de brasage et densité de la carte choisissent
// la variante d'empreinte de chaque article et les échangent en place.
// Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "promethee/articles.hpp"
#include "promethee/electronique.hpp"

namespace prom::ecad {

enum class GenreGabarit : std::uint8_t {
  IPC,          // CotesBoitier du générateur 4.7 (puces, aile de mouette, sans patte)
  BGA, LGA, WLCSP, DFN, SON, SOD, MELF, SMx, SOT89, DPAK, D2PAK, QuartzCMS, PLCC, BordCarte
};
struct ParamsGabarit {
  GenreGabarit genre = GenreGabarit::IPC;
  CotesBoitier cotes;                     // genre IPC
  int rangees = 0, colonnes = 0;          // BGA, LGA, WLCSP
  double pas = 1.0;                       // mm
  double corpsL = 0, corpsW = 0, hauteur = 0;
  double bille = 0;                       // BGA, WLCSP : diamètre de bille (0 : la moitié du pas)
  double pastille = 0;                    // diamètre ou côté imposé (0 : d'après la densité)
  int broches = 0;                        // DFN, SON, PLCC, bord de carte, quartz
  double plot = 0, plotL = 0;             // plot central ou languette (largeur, longueur)
  std::string variante;                   // SMA, SMB, SMC ; MELF, MiniMELF ; SOD-123… ; taille du quartz
  std::vector<std::string> sansBille;     // billes absentes (« A1 »)
  friend bool operator==(const ParamsGabarit&, const ParamsGabarit&) = default;
};
// Lettres des rangées d'une matrice JEDEC (JEP95) : A … H, J … N, P, R, T, U, V, W, Y, AA… (sans I, O, Q, S, X, Z)
std::string lettreRangee(int rang);
// Paramètres lus dans un nom : « BGA-256_17.0x17.0mm_Layout16x16_P1.0mm », « DFN-8-3x3-0.65 », « SOIC-8 »,
// « TSSOP-14 », « QFN-24-4x4-0.5 », « TQFP-64-10x10-0.5 », « SOT-23-5 », « R_0603 », « SMB », « SOD-123 »,
// « MiniMELF », « Crystal_SMD_3225-4Pin », « PLCC-44 », « Edge-2x10_P2.54 », et les noms IPC « SOIC127P600X175-8N »
std::optional<ParamsGabarit> gabaritDepuisNom(const std::string& nom);
// Empreinte du gabarit à la densité donnée ; identifiant : le nom IPC suffixé de la densité (« BGA256C100P16X16_1700X1700X160N »)
DefEmpreinte empreinteGabarit(const ParamsGabarit& p, DensiteIPC d = DensiteIPC::Nominale, const ProfilIPC& profil = {});
std::string nomGabarit(const ParamsGabarit& p, DensiteIPC d);
std::optional<DefEmpreinte> empreinteParNom(const std::string& nom, DensiteIPC d = DensiteIPC::Nominale);

// Contrôles d'une empreinte (CE-01 à CE-07) : pastilles qui se recouvrent, numéros en double, pastille hors de
// l'encombrement, pâte hors du cuivre, trou hors de sa pastille, corps absent, écart entre pastilles sous 0,1 mm
std::vector<std::string> controlerEmpreinte(const DefEmpreinte& d);

// Série par noms (4.26) : chaque nom à chaque densité ; les noms refusés vont au journal
std::vector<DefEmpreinte> serieEmpreintes(const std::vector<std::string>& noms, const std::vector<DensiteIPC>& densites, std::vector<std::string>* refus = nullptr);

// Variante d'empreinte d'après la densité (« M », « N », « L ») et le procédé (« refusion », « vague », « fer ») de la
// carte : le même gabarit à une autre densité (le brasage à la vague prend la densité M) ; vide si l'identifiant
// n'est pas d'un gabarit connu
std::string varianteEmpreinte(const std::string& id, char densite, const std::string& procede);
// Toutes les empreintes de la carte passées à leur variante, en place (positions, faces et pistes gardées) ; rend
// le nombre d'empreintes changées
int appliquerVariantes(ConceptionElec& c);

}  // namespace prom::ecad
