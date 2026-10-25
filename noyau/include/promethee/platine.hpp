// Prométhée : platine d'essai et documents de câblage (lot L16 de la 0.34 ; 16.2 à 16.4, 16.6 ; PRO-01 à PRO-09,
// PRO-12, PRO-14).
//
// Troisième vue de la conception : la platine d'essai sans soudure (830 points : 63 colonnes de deux demi-bandes de
// cinq trous, quatre rails d'alimentation), synchronisée avec le schéma et le circuit. Chaque patte d'un composant
// est enfoncée dans un trou ; les fils relient deux trous, couleur et section par fil. La connectivité se tire des
// bandes internes par ensembles disjoints ; elle se compare à celle du schéma : patte verte quand son trou rejoint
// les broches voulues, rouge sur une liaison ratée, écarts listés qui mènent au trou. Le montage se pose de lui-même
// (circuits intégrés à cheval sur la rainure centrale), en entier ou étape par étape. Un fil ajouté sur la platine
// entre deux pattes relie leurs broches au schéma (et donc au chevelu du circuit), en un seul pas.
//
// Documents de câblage : liste des fils et plan de chargement de la platine ; câblage d'une guitare dont l'analyse
// parcourt les positions du sélecteur et donne la combinaison de micros de chacune (seul, en parallèle, en série,
// hors phase). Rangé dans c.extensions["platine"]. Licence GPL-3.0-only.
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Trous de la platine ----------
// Colonne de 1 à 63 ; rangée « a » à « j », ou rail « +h », « -h » (en haut), « +b », « -b » (en bas)
struct Trou {
  int colonne = 0;
  std::string rangee;
  friend auto operator<=>(const Trou&, const Trou&) = default;
};
std::string texteTrou(const Trou& t);  // « 12c », « +h5 »
std::optional<Trou> lireTrou(const std::string& s);
struct ModelePlatine {
  std::string nom = "830 points";
  int colonnes = 63;
  friend bool operator==(const ModelePlatine&, const ModelePlatine&) = default;
};
bool trouValide(const ModelePlatine& m, const Trou& t);
// Bande d'un trou : « 12-haut » (a à e), « 12-bas » (f à j), « +h »… (un rail entier)
std::string bandeDuTrou(const Trou& t);
// Position du trou dans le dessin de la platine (origine en haut à gauche, pas de 2,54 mm)
Pt positionTrou(const Trou& t);
std::optional<Trou> trouProche(const ModelePlatine& m, Pt p, Nm tolerance = 1'270'000);

struct PosePlatine {
  std::string composant;                // UUID
  std::map<std::string, Trou> broches;  // numéro de broche → trou
  friend bool operator==(const PosePlatine&, const PosePlatine&) = default;
};
struct FilPlatine {
  std::string uuid;
  Trou a, b;
  std::string couleur = "#D32F2F";
  double section = 0.5;  // mm²
  bool courbe = true;    // courbé, sinon coudé
  friend bool operator==(const FilPlatine&, const FilPlatine&) = default;
};
struct VuePlatine {
  ModelePlatine modele;
  std::vector<PosePlatine> poses;
  std::vector<FilPlatine> fils;
  const PosePlatine* pose(const std::string& composant) const;
  PosePlatine* pose(const std::string& composant);
  friend bool operator==(const VuePlatine&, const VuePlatine&) = default;
};
VuePlatine platineDe(const ConceptionElec& c);
void ecrirePlatine(ConceptionElec& c, const VuePlatine& v);

// ---------- Connexions (16.3) ----------
struct ConnectivitePlatine {
  std::vector<std::vector<NoeudBroche>> noeuds;              // broches reliées par les bandes et les fils
  std::map<std::pair<std::string, std::string>, int> parBroche;  // (composant, broche) → nœud
  std::map<std::string, int> parBande;                       // bande → nœud
  int noeudDuTrou(const Trou& t) const;
};
ConnectivitePlatine connectivitePlatine(const VuePlatine& v);
// Trous du potentiel d'un trou (clic maintenu : tout le potentiel s'allume)
std::vector<Trou> trousDuPotentiel(const VuePlatine& v, const Trou& t);
struct EcartPlatine {
  std::string genre;  // « liaison manquante », « liaison en trop », « composant absent de la platine », « patte hors de la platine »
  std::string composant, broche, autreComposant, autreBroche, reseau;
  Trou trou;
  std::string message;
};
std::vector<EcartPlatine> comparerPlatine(const ConceptionElec& c, const VuePlatine& v);
// Patte verte (enfoncée, son trou rejoint les broches voulues), rouge (liaison ratée ou en trop), libre (broche
// que le schéma ne relie à rien)
enum class EtatPatte : std::uint8_t { Verte, Rouge, Libre };
std::map<std::pair<std::string, std::string>, EtatPatte> etatsPattes(const ConceptionElec& c, const VuePlatine& v);
// Montage posé de lui-même : circuits intégrés à cheval sur la rainure, composants à deux pattes sur deux bandes
// voisines, fils pour les liaisons qui restent, masse et alimentations sur les rails ; « etapes » : composants posés
// au plus (montage étape par étape, −1 : tous)
VuePlatine monterSurPlatine(const ConceptionElec& c, int etapes = -1);

// ---------- Synchronisation (16.2) ----------
// Fil ajouté sur la platine : la liaison qu'il fait entre les broches de ses deux trous passe au schéma (un fil
// tiré de l'une à une étiquette du réseau de l'autre) ; le fil est rangé dans la vue. Rend le réseau commun.
std::string ajouterFilPlatine(ConceptionElec& c, FilPlatine f, std::string* erreur = nullptr);

// Composant déplacé de « colonnes » et « rangees » : ses pattes suivent, et les bouts de fils posés dans les
// demi-bandes qu'il est seul à occuper aussi, pour qu'il garde ses liaisons. Refusé hors de la platine.
bool deplacerSurPlatine(VuePlatine& v, const std::string& composant, int colonnes, int rangees, std::string* erreur = nullptr);

// ---------- Impression et 3D (16.4) ----------
// Platine à l'échelle exacte par le chemin du typon (page à la taille du dessin, règle de 50 mm, compensation de
// l'imprimante) : bandes de cuivre d'une plaque au dessin de la platine, trous, fils. « coteCuivre » : vue côté
// cuivre, en miroir, coupures des bandes comprises (plaque à bandes continues de a à j, coupée entre e et f)
ModeleTypon modeleTyponPlatine(const VuePlatine& v, const OptionsTypon& o = {}, bool coteCuivre = false);
// Coupures d'une plaque à bandes continues de a à j qui reproduit la platine : une par colonne dont les deux moitiés
// portent des potentiels différents
std::vector<Trou> coupuresBandes(const VuePlatine& v);
// Perçages, sillons entre bandes et coupures de la plaque, en HPGL (1 unité = 0,025 mm) pour une fraiseuse ou un
// traceur à outil (PRO-13)
std::string hpglPlaque(const VuePlatine& v);
// Plan miroir des coupures : côté cuivre de la plaque à bandes, coupures marquées
std::string svgCoupures(const VuePlatine& v);
// Scène de la platine équipée : boîtes des corps, pattes et fils en tubes, en mm (x vers la droite, y vers le bas
// du dessin, z vers le haut, surface de la platine à z = 0)
struct Objet3DPlatine {
  std::string genre;      // « platine », « corps », « patte », « fil », « led », « interrupteur »
  std::string composant;  // UUID (vide pour la platine et les fils)
  std::string reference;
  std::vector<std::array<double, 3>> points;  // corps : deux coins opposés ; patte, fil : axe du tube
  double rayon = 0;                           // tube
  std::string couleur;
};
std::vector<Objet3DPlatine> scenePlatine3D(const ConceptionElec& c, const VuePlatine& v);

// ---------- Documents ----------
struct LigneFil {
  std::string de, a, couleur;
  double longueur = 0;  // mm, d'un trou à l'autre plus 10 mm de pattes
};
std::vector<LigneFil> listeFils(const VuePlatine& v);
std::string planDeChargement(const ConceptionElec& c, const VuePlatine& v);  // composants par trous, en texte
std::string svgPlatine(const ConceptionElec& c, const VuePlatine& v);

// ---------- Guitare (16.6) ----------
// Sélecteur à cinq positions d'une guitare (symbole « Selecteur_5 » : commun C, contacts 1 à 3) : position 1 le
// contact 1, 2 les contacts 1 et 2, 3 le contact 2, 4 les contacts 2 et 3, 5 le contact 3
std::vector<std::vector<std::string>> contactsSelecteur5();
struct PositionGuitare {
  int position = 0;
  std::vector<std::string> micros;  // repères des micros entendus
  std::string couplage;             // « seul », « parallèle », « série », « aucun »
  bool horsPhase = false;
  std::string texte;                // « manche et milieu en parallèle »
};
// Micros : symbole « Micro_guitare » (1 chaud, 2 froid), leur nom (valeur) « chevalet », « milieu », « manche » ;
// sortie : jack (1 pointe, 2 corps) ; le potentiomètre de volume laisse passer de son entrée (3) à son curseur (2)
std::vector<PositionGuitare> analyserGuitare(const ConceptionElec& c, const std::string& selecteur, const std::string& sortie);
ConceptionElec exempleGuitareTroisMicros();

}  // namespace prom::ecad
