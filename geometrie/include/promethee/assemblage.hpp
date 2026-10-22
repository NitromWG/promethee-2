// Prométhée : résolution des contraintes d'assemblage. Licence GPL-3.0-only.
//
// Chaque composant mobile a six degrés de liberté. Le fichier les garde en position (mm) et angles d'Euler (degrés,
// rotations X puis Y puis Z) ; le solveur, lui, tient l'orientation en matrice et avance par petites rotations
// autour des axes globaux passant par le centre du composant : pas de blocage de cardan (AS-05), des libertés
// décrites dans le repère de l'assemblage (AS-07). Les contraintes (faces coïncidentes ou à distance, axes
// concentriques, faces parallèles, perpendiculaires ou à un angle, tangence, liaisons) sont satisfaites par
// moindres carrés ; une orientation de départ où le gradient est nul (point selle : faces perpendiculaires qu'on
// veut parallèles…) est d'abord corrigée analytiquement (AS-04). Une résolution en échec n'écrit rien (AS-20).
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>

#include "promethee/construction.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/modele.hpp"

namespace prom {

gp_Trsf placementComposant(const ComposantAssemblage& c);
// Tourne un composant autour d'un axe de l'assemblage (0 X, 1 Y, 2 Z) passant par son origine : la rotation se compose
// à gauche, puis se réécrit en angles d'Euler (AS-23).
// Composant placé par une transformation quelconque (import d'un assemblage) : position, angles d'Euler et, si la
// transformation retourne l'espace, symétrie YZ (miroir = 3). placementComposant(resultat) la redonne.
ComposantAssemblage composantDepuisPlacement(const gp_Trsf& t);
void pivoterAutourAxeGlobal(ComposantAssemblage& c, int axe, double degres);
// Rotation du composant autour d'une droite quelconque de l'assemblage (A4) : position et angles réécrits
void pivoterAutour(ComposantAssemblage& c, const gp_Ax1& axe, double degres);
// Copie symétrique d'un composant par un plan de l'assemblage passant par l'origine (1 XY, 2 XZ, 3 YZ) : symétrie
// composée avec le placement de l'original, réécrite en placement rigide et symétrie locale ; deux symétries se
// composent en une rotation (AS-09).
ComposantAssemblage composantSymetrique(const ComposantAssemblage& c, int plan);
// La face « nom » de la nouvelle pièce peut-elle remplacer celle de l'ancienne dans une contrainte ? Même genre de
// surface, même rayon, même direction dans le repère de la pièce (AS-18). Sinon, la raison.
bool facesCompatibles(const ResultatPiece& ancienne, const ResultatPiece& nouvelle, const std::string& nom, std::string* raison = nullptr);

struct EtatAssemblage {
  bool resolu = false;
  int contraintesNonSatisfaites = 0;
  std::string message;
  // Contraintes écartées (face introuvable, face d'un type que la contrainte n'accepte pas, type inconnu, composant
  // sous-assemblage) ou violées, avec la raison
  std::vector<std::pair<int, std::string>> problemes;
};

// pieces[i] : résultat de construction de la pièce d'indice i du projet. Les composants fixes ne bougent pas.
// « sous » : assemblages du projet, pour les contraintes qui visent une face d'un sous-assemblage par son chemin
// « @k/face » (composant k du sous-assemblage, récursif) : le sous-assemblage est alors un corps rigide (AS-16).
EtatAssemblage resoudreAssemblage(Assemblage& a, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous = nullptr);

// Déplacement d'un composant dans le respect des contraintes (A4) : le composant k est amené au plus près de « cible ».
// Ordre : tenu à la cible, les composants liés suivent ; sinon ramené sur ses contraintes envers ce qui ne bouge pas
// (composants fixes, assemblage), puis tenu là, les autres suivent ; sinon tout se résout depuis là. Rend les composants
// placés, ou rien si aucune solution (l'appelant garde l'état d'avant).
std::optional<std::vector<ComposantAssemblage>> deplacerDansContraintes(const Assemblage& a, int k, const ComposantAssemblage& cible, const std::vector<ResultatPiece>& pieces,
                                                                         const std::vector<Assemblage>* sous = nullptr);

// Mise à jour (A3, AS-12) : résout de bas en haut les assemblages du projet, chaque sous-assemblage avant ceux qui
// l'utilisent, avec les pièces telles qu'elles sont construites ; racine ≥ 0 : seulement cet assemblage et ceux qu'il
// contient (récursivement), sinon tous. Un assemblage en échec garde ses positions (rien n'est écrit). Rend, par
// assemblage traité et dans l'ordre de résolution, son indice et son état ; une boucle d'imbrication est ignorée.
struct MiseAJourAssemblage {
  int assemblage = -1;
  EtatAssemblage etat;
  bool deplace = false;  // une position ou un angle suivi a changé
};
std::vector<MiseAJourAssemblage> mettreAJourAssemblages(std::vector<Assemblage>& assemblages, const std::vector<ResultatPiece>& pieces, int racine = -1);
// Ordre de bas en haut (sous-assemblages d'abord) des assemblages atteints depuis racine (−1 : tous)
std::vector<int> ordreAssemblages(const std::vector<Assemblage>& assemblages, int racine = -1);

// Analyse : pour chaque composant, ses six mouvements (translations X, Y, Z, rotations X, Y, Z) encore libres,
// et les contraintes non satisfaites (contradictoires) après résolution.
struct AnalyseAssemblage {
  // Par composant : translations selon X, Y, Z du monde, rotations autour d'un axe parallèle à X, Y, Z du monde
  // (passant par n'importe quel point) encore possibles. Un composant fixe n'a rien de libre.
  std::vector<std::array<bool, 6>> libres;
  std::vector<int> libertes;                         // par composant : nombre de mouvements indépendants encore possibles
  std::vector<std::vector<std::string>> mouvements;  // par composant : chaque mouvement décrit comme un torseur
  std::vector<int> nonSatisfaites;                   // indices de contraintes
  int degresLiberte = 0;
};
AnalyseAssemblage analyserAssemblage(const Assemblage& a, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous = nullptr);

// Faces admises par un type de contrainte (AS-08, AS-30) : vide si les deux références conviennent, sinon la raison.
// genres : « plan », « cylindre », « cone », « sphere », et (A2) « droite » (arête droite, axe), « cercle » (arête
// circulaire), « point » (sommet, origine, point de référence) ; autre : « ».
// Références admises, en plus des faces : arêtes « A|B », sommets « S:A|B|C », repère de la pièce « base:XY », « base:X »,
// « base:O »…, géométrie de référence « plan:id », « axe:id », « point:id », et repère de l'assemblage
// « assemblage:XY »… (voir referenceAssemblage).
std::string facesAdmises(const std::string& type, const std::string& genreA, const std::string& genreB);
// Genre de la face de référence d'une pièce (« plan », « cylindre », « cone », « sphere », vide si introuvable)
std::string genreFace(const ResultatPiece& piece, const std::string& nom);
// Même chose pour la face d'un composant, pièce ou sous-assemblage (chemin « @k/face »)
std::string genreFaceComposant(const ComposantAssemblage& c, const std::string& nom, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous);

// Crée une liaison mécanique entre deux composants, en mémorisant leur état de départ (rotations, position axiale).
// type : « pivot », « glissiere », « rotule », « vis » (refA, refB : faces cylindriques, sphériques pour la rotule),
// « engrenage » (refA, refB : faces cylindriques des deux axes ; valeur : rapport dents de B / dents de A, négatif
// pour une couronne intérieure : la roue B tourne « rapport » fois moins vite que A, en sens opposé pour un engrenage
// extérieur, quel que soit le sens de modélisation des arbres, AS-10), valeur de la vis : pas en mm par tour.
ContrainteAssemblage creerLiaison(const Assemblage& a, const std::vector<ResultatPiece>& pieces, const std::string& type, int ca, const std::string& refA, int cb,
                                  const std::string& refB, double valeur = 0, const std::vector<Assemblage>* sous = nullptr);

// Valeur actuelle d'une contrainte de distance ou d'angle (U1) : écart mesuré le long de la normale de la première
// référence (coïncidente : plans, ou point ou droite et plan), angle entre les deux directions en degrés (0 à 180) ;
// rien si elle ne se mesure pas. Sert de valeur de départ : la contrainte posée, rien ne bouge.
std::optional<double> valeurMesuree(const Assemblage& a, const ContrainteAssemblage& c, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous = nullptr);

// Angle orienté (A1) : la contrainte d'angle k est orientée autour de l'axe commun actuel de ses deux faces (gardé dans
// le repère du composant a), à sa valeur actuelle, de sorte que rien ne bouge ; faux si les faces sont parallèles
bool orienterAngle(Assemblage& a, int contrainte, const std::vector<ResultatPiece>& pieces, const std::vector<Assemblage>* sous = nullptr);

// Export STEP d'un assemblage du projet, construit depuis le modèle (pas depuis l'affichage) : un prototype par pièce,
// nommé et coloré (une pièce symétrique est un prototype distinct), une racine au nom de l'assemblage, une instance
// placée par composant non suspendu, sous-assemblages imbriqués (EX-02, EX-07, EX-09). piece(i) : pièce construite.
// Un sous-assemblage symétrique devient un assemblage distinct de prototypes symétriques. Rend le nombre de
// composants qui n'ont pas pu être écrits (géométrie absente, renvoi circulaire).
// Les couleurs de faces (« couleursFaces » de la pièce) sont écrites face par face.
int exporterStepAssemblage(const Projet& projet, int assemblage, const std::function<const ResultatPiece&(int)>& piece, const std::string& chemin,
                           const OptionsStep& options = {});

// ---------- Contacts, interférences et jeux ----------
// Tolérances documentées : deux pièces à moins de « contact » (1 µm par défaut) l'une de l'autre se touchent ;
// elles interfèrent si leur volume commun dépasse « volume » (0,001 mm³) ; un jeu inférieur à « jeuFaible »
// (0,1 mm) est signalé. Ces seuils servent la géométrie nominale : ce n'est pas une analyse de tolérances.
struct TolerancesContact {
  double contact = 1e-3;    // mm
  double volume = 1e-3;     // mm³
  double jeuFaible = 0.1;   // mm
};
enum class EtatContact { Separees, JeuFaible, Contact, Interference, Indetermine };
std::string libelleContact(EtatContact e);
struct AnalyseContact {
  EtatContact etat = EtatContact::Indetermine;
  double distance = 0;      // mm, distance minimale entre les deux solides (0 s'ils se touchent ou se pénètrent)
  double volumeCommun = 0;  // mm³
  gp_Pnt pointA, pointB;    // où se trouve la distance minimale (sur chaque solide)
  std::string detail;       // cause d'un résultat indéterminé (géométrie invalide, calcul en échec)
};
// a et b déjà placés dans le même repère.
AnalyseContact analyserContact(const TopoDS_Shape& a, const TopoDS_Shape& b, const TolerancesContact& t = {});
// Analyse de toutes les paires d'un ensemble de formes déjà placées. Les paires dont les boîtes englobantes,
// élargies du jeu faible, ne se recouvrent pas sont séparées sans calcul exact, et chaque forme n'est validée
// qu'une fois : un grand assemblage se vérifie en un temps proche du nombre de paires voisines.
struct PaireContact {
  int a = 0, b = 0;
  AnalyseContact c;
};
// Paires en interférence, en contact, à jeu faible ou indéterminées (toutes celles qui ne sont pas séparées)
std::vector<PaireContact> contactsEntre(const std::vector<TopoDS_Shape>& formes, const TolerancesContact& t = {});
// Les « nombre » plus petits jeux exacts, croissants, entre paires SÉPARÉES (distance au-delà de la tolérance de
// contact) : contacts, interférences et paires indéterminées n'y figurent pas, ils sont comptés à part (AS-29). Les
// paires sont prises dans l'ordre de la distance de leurs boîtes, et la recherche s'arrête dès que la boîte suivante
// est plus loin que le dernier jeu retenu.
struct BilanJeux {
  std::vector<PaireContact> jeux;
  int contacts = 0, interferences = 0, indetermines = 0;
};
BilanJeux plusPetitsJeux(const std::vector<TopoDS_Shape>& formes, std::size_t nombre, const TolerancesContact& t = {});

// ---------- Propriétés physiques d'un assemblage ----------
// Masse et volume : somme des pièces (sous-assemblages développés, composants suspendus exclus) ; le volume
// commun des pièces qui interfèrent n'est pas retiré (il est signalé par l'analyse des interférences).
// Centre de gravité : barycentre des centres de gravité placés, pondérés par les masses.
struct ProprietesAssemblage {
  double masse = 0, volume = 0;  // g, mm³
  gp_Pnt centre;                 // mm, repère de l'assemblage
  int pieces = 0;                // pièces comptées
  int provisoires = 0;           // pièces dont la densité est provisoire (matière non confirmée)
  std::vector<std::string> exclues;  // pièces écartées du calcul, avec la raison
  bool fiable() const { return exclues.empty() && provisoires == 0; }
};
// definitions[i] et resultats[i] : pièce d'indice i du projet.
ProprietesAssemblage proprietesAssemblage(const std::vector<Assemblage>& assemblages, int indice, const std::vector<Piece>& definitions,
                                          const std::vector<ResultatPiece>& resultats);

}  // namespace prom
