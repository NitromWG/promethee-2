// Prométhée : pièces paramétriques (arbre de construction). Licence GPL-3.0-only.
// Une pièce est une suite de fonctions rejouées dans l'ordre : esquisses, puis fonctions volumiques qui les
// utilisent (extrusion, révolution) ou qui retouchent le solide (congé, chanfrein, coque). Les arêtes et faces
// visées par ces retouches sont désignées par des noms stables, calculés à la construction (voir construction.hpp).
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "promethee/esquisse.hpp"
#include "promethee/unites.hpp"

namespace prom {

enum class PlanBase { XY, XZ, YZ };
// Inconnu : type écrit par une version plus récente ; la fonction est gardée telle quelle (« autres ») et réécrite
// sans perte, mais pas calculée.
enum class TypeFonction { Esquisse, Extrusion, Revolution, Conge, Chanfrein, Coque, Plan, Axe, Point, Repere, Percage, Depouille, Repetition, Symetrie, Balayage, Lissage, Esquisse3D, Import, Tole, BordTombe, Nervure, Filetage, Inconnu };

struct FonctionPiece {
  int id = 0;
  TypeFonction type = TypeFonction::Esquisse;
  std::string nom;
  bool desactivee = false;
  int visibilite = 0;   // esquisse : 0 automatique (masquée une fois utilisée), 1 montrée, 2 masquée
  // Esquisse : plan de base décalé le long de sa normale
  PlanBase plan = PlanBase::XY;
  double decalage = 0;
  std::string face;       // esquisse posée sur une face plane de la pièce (nom de la face), sinon sur le plan de base
  std::array<double, 3> axeX{0, 0, 0};  // esquisse sur face : axe X mémorisé à la pose (nul : règle par défaut)
  Esquisse esquisse;
  // Extrusion et révolution : esquisse utilisée ; ajout de matière ou enlèvement
  int esquisseId = 0;
  bool enlever = false;
  double longueur = 10;
  bool symetrique = false, inverse = false, traversant = false;
  int axe = 0;            // révolution : ligne de l'esquisse servant d'axe
  double angle = 360;     // degrés
  // Extrusion : condition de fin dans la direction 1 (« borgne », « traversant », « prochain », « sommet »,
  // « face », « decale », « milieu »), et seconde direction facultative (« » : aucune).
  std::string fin = "borgne";
  std::string refFin;               // face ou sommet visé (nom)
  double decalageFin = 0;           // « decale » : distance au-delà de la face visée
  std::string fin2;
  double longueur2 = 10;
  std::string refFin2;
  double decalageFin2 = 0;
  double depouille = 0;             // degrés, positif : les flancs se resserrent
  bool mince = false;               // fonction mince : paroi d'épaisseur constante le long du contour
  double epaisseurMince = 1;
  std::string sensMince = "interieur";  // « interieur », « exterieur », « milieu »
  bool minceArrondi = false;        // fonction mince vers l'extérieur : coins arrondis (défaut : vifs, FO-24)
  std::vector<std::string> regions; // contours retenus (clés des boucles), vide : tous
  // Point intérieur (u, v) de chaque région retenue, dans le même ordre que « regions » : retrouve la région quand
  // une retouche d'esquisse a changé sa clé (FO-06). Facultatif.
  std::vector<std::array<double, 2>> pointsRegions;
  // Révolution : axe de base (« X », « Y », « Z ») ou choisi au clic (axe de référence « F:12 », arête « A|B », face
  // cylindrique, ligne d'une autre esquisse « E:esquisse:élément ») à la place d'une ligne, plan milieu, seconde
  // direction ; « inverse » : sens de rotation inversé
  std::string axeBase;
  bool milieu = false;
  double angle2 = 0;
  // Congé, chanfrein, coque : arêtes ou faces nommées
  std::vector<std::string> references;
  double rayon = 1, distance = 1, epaisseur = 1.5;
  // Chanfrein : distances égales, deux distances, ou distance et angle (côté de la première face, sauf « inverse »)
  std::string typeChanfrein = "egal";
  double distance2 = 1, angleChanfrein = 45;
  // Chanfrein à deux distances ou distance-angle : face (nom) sur laquelle se mesure la première distance (FO-25) ;
  // vide : règle par défaut (face de la fonction la plus ancienne), « inverse » échange.
  std::string faceChanfrein;
  // Coque : épaisseurs particulières de certaines faces (nom de face, épaisseur)
  std::vector<std::pair<std::string, double>> epaisseursFaces;
  // Géométrie de référence. Plan : « decale », « angle », « troisPoints », « parallelePoint » ;
  // Axe : « arete », « cylindre », « deuxPoints », « deuxPlans » ; Point : « coordonnees », « sommet », « centre » ;
  // Repère : origine (point) et plan (Z), axe X facultatif. Les supports sont des noms de faces, d'arêtes, de
  // sommets, des plans et axes de base (« XY », « XZ », « YZ », « X », « Y », « Z ») ou d'autres fonctions (« F:12 »).
  std::string mode;
  std::vector<std::string> supports;
  double x = 0, y = 0, z = 0;
  // Perçage (centres : points de l'esquisse esquisseId, dans son plan, vers l'intérieur de la matière)
  std::string typePercage = "simple";     // « simple », « taraude », « lame », « fraise »
  std::string norme = "ISO";              // « ISO » (métrique), « ANSI » (pouces, UNC)
  std::string taille = "M5";
  std::string ajustement = "normal";      // trou de passage : « serre », « normal », « large »
  double diametre = 5, profondeur = 10;   // « simple » : diamètre libre ; profondeur (ignorée si « traversant »)
  double longueurFiletee = 8;
  bool pointeForet = true;                // fond conique à 118° pour un trou borgne
  // Dépouille : plan ou face neutre, faces à incliner (references), angle (angle)
  std::string planNeutre;
  // Répétition (« lineaire », « circulaire », « esquisse », « tableau ») et symétrie : fonctions sources
  // (vide pour la symétrie : tout le corps)
  std::string typeRepetition = "lineaire";
  std::vector<int> sources;
  std::string direction1, direction2;      // arête, axe ou « X », « Y », « Z » ; circulaire : l'axe en direction1
  int nombre1 = 3, nombre2 = 1;
  double pas1 = 10, pas2 = 10;
  bool inverse1 = false, inverse2 = false;
  std::vector<std::array<double, 3>> tableau;  // décalages (tableau)
  // Balayage : profil (esquisseId) le long d'un chemin (cheminId) ; lissage : profils successifs (sources)
  int cheminId = 0;
  // Esquisse 3D : points dans l'espace (mm), reliés par des segments ou une courbe lisse, ouverte ou fermée
  std::vector<std::array<double, 3>> points3d;
  bool lisse = false, fermee = false;
  // Import : géométrie d'un fichier STEP, IGES, BREP ou STL, gardée dans le projet (texte BRep), et le nom du fichier d'origine
  std::string donnees, source;
  // Balayage en tube : profil circulaire automatique au début du chemin (épaisseur nulle : tube plein)
  bool tube = false;
  // Tôlerie (M5). Tôle de base : profil fermé de l'esquisse esquisseId, épaisseur « epaisseur », rayon de pliage par
  // défaut « rayon », facteur K (≤ 0 : DIN 6935, d'après le rayon et l'épaisseur), dégagements de pli
  // (« rectangulaire », « oblong », « aucun ») ; « inverse » : la tôle s'épaissit sous l'esquisse.
  // Bord tombé : face mince (côté droit d'une paroi, references[0]) le long de laquelle la tôle se plie, de « angle »
  // degrés, rayon « rayon » (≤ 0 : celui de la tôle), bord de « longueur » mesurée « exterieure » (au sommet virtuel
  // extérieur), « interieure » ou « tangente » (depuis la fin du pli) ; position « matiere-interieure » (la face
  // extérieure du bord sur l'arête), « matiere-exterieure » ou « pli-exterieur » (le pli commence à l'arête) ;
  // « inverse » : plié vers le dessous ; retraits aux deux bouts de l'arête.
  double facteurK = 0;
  std::string degagement = "rectangulaire";
  std::string mesure = "exterieure";
  std::string positionPli = "matiere-interieure";
  double retraitDebut = 0, retraitFin = 0;
  // Nervure (M3) : contour ouvert de l'esquisse esquisseId, épaisseur « epaisseur », matière « parallele » au plan
  // de l'esquisse (elle remplit l'espace entre le trait et la pièce) ou « normale » à lui (jusqu'à la pièce), dans
  // « mode » ; épaisseur de part et d'autre du plan (« milieu ») ou d'un seul côté (« exterieur », « interieur »)
  // dans « sensMince » ; « inverse » : l'autre côté du trait (parallèle) ou l'autre sens (normale).
  // Filetage cosmétique (M4) : face cylindrique references[0] (trou ou arbre), « norme » et « taille » (vide :
  // déduite du diamètre), pas « pasFiletage » (0 : celui de la taille), longueur « longueurFiletee » (0 : toute la
  // face) depuis un bout (« inverse » : l'autre). Rien n'est modélisé : le filetage est montré et coté.
  double pasFiletage = 0;
  // Champs du fichier que cette version ne connaît pas (ou fonction entière d'un type inconnu) : réécrits tels quels
  Json autres = Json::object();
};

// Mise en plan d'une pièce : réglages de la feuille, rangée sous la pièce dans l'arbre.
struct PlanPiece {
  std::string nom = "Mise en plan";
  std::string format = "A4";   // A0 à A4
  bool portrait = false, troisiemeDiedre = false;
  // Vue de face (côté d'où l'on regarde, rotation) et vues projetées choisies (voir OptionsMiseEnPlan)
  std::string vueFace = "avant";
  int rotationFace = 0;
  std::string vueVerticale = "dessus", vueLaterale = "gauche";
  double echelle = 0;  // 0 : automatique
  bool face = true, dessus = true, gauche = true, iso = true, cachees = true;
  bool axes = true, tangentes = false;  // axes des trous et cylindres ; arêtes tangentes en trait fin
  // Coupe A-A (« face », « dessus », « gauche » ; vide : aucune), vue de détail, vue interrompue, cotes
  // automatiques, notes et tolérances générales (voir dessin.hpp, OptionsMiseEnPlan)
  std::string coupe;
  double decalageCoupe = 0;
  bool demiCoupe = false;
  bool detail = false;
  std::string vueDetail = "face";
  double detailX = 0.25, detailY = 0.25, detailRayon = 0.15, detailEchelle = 2;
  bool interrompue = false;
  double interruptionDebut = 0.3, interruptionFin = 0.7;
  bool cotes = false;
  bool cotesModele = false;  // cotes des esquisses reprises dans les vues (D3)
  std::vector<std::string> notes;
  std::string toleranceGenerale;
  Json autres = Json::object();
};

struct Piece {
  std::string id, nom = "Pièce";
  std::string uniteLongueur = "mm", uniteAngle = "deg";
  std::string materiau = "PLA";
  std::string couleur = "#C9CFD6";   // apparence de la pièce
  // Apparences par face : nom de face (« 3:cote:2 », « 5:import.12 ») → couleur « #RRGGBB » ; une face dont le nom
  // disparaît reprend la couleur de la pièce
  std::map<std::string, std::string> couleursFaces;
  std::string reference, fournisseur;  // numéro de pièce et fournisseur (nomenclature)
  // Propriétés personnalisées (« Désignation », « Auteur », « Révision », « Projet »…), dans l'ordre choisi :
  // reprises au cartouche des mises en plan et proposées comme colonnes de nomenclature
  std::vector<std::pair<std::string, std::string>> proprietes;
  // Repères de nomenclature stables (bulles, nomenclature) : attribués une fois, gardés quand l'ordre des pièces
  // change ; « repereSymetrique » numérote la version symétrique de la pièce si un assemblage l'emploie (0 : aucun)
  int repere = 0, repereSymetrique = 0;
  // Propriété personnalisée par son nom (sans tenir compte de la casse ni des accents en tête) ; vide si absente
  std::string propriete(const std::string& nom) const;
  double transparence = 0;          // 0 opaque, 1 invisible
  std::vector<PlanPiece> plans;
  std::vector<std::pair<std::string, std::string>> variables;  // nom, expression (peut citer les précédentes)
  double densite = 1.24;   // g/cm³
  // Faux tant que personne n'a choisi la matière : la densité est alors une valeur par défaut (PLA), et les
  // masses qui en découlent sont affichées « provisoires ». Une densité absente, nulle, négative ou illisible
  // dans le fichier revient à ce défaut, signalé de la même façon.
  bool densiteConfirmee = false;
  std::vector<FonctionPiece> fonctions;
  int prochainId = 1;
  Json autres = Json::object();  // champs inconnus de cette version, réécrits tels quels
  FonctionPiece* fonction(int id);
  const FonctionPiece* fonction(int id) const;
  int ajouter(FonctionPiece f);  // attribue l'identifiant et un nom par défaut (« Esquisse 2 »…), renvoie l'id
};

const char* nomTypeFonction(TypeFonction t);
// Fonctions dont dépend f : esquisses, chemin, sources, plans et axes « F:id », et fonctions qui ont créé les faces,
// arêtes ou sommets qu'elle désigne. Une fonction doit toujours suivre ses parents dans l'arbre (FO-04).
std::vector<int> parentsDe(const FonctionPiece& f);
// Explication si échanger la fonction id avec sa voisine (sens −1 : monter, +1 : descendre) la placerait avant un
// de ses parents ou après une fonction qui l'utilise ; vide si l'échange est permis.
std::string deplacementInterdit(const Piece& p, int id, int sens);
// Monter (−1) ou descendre (+1) une fonction comme l'arbre la montre : avec les esquisses rangées sous elle, en
// échangeant sa place avec le bloc voisin. Refusé, raison à l'appui, si une fonction passait avant l'une de ses
// dépendances (AP-06). Une esquisse rangée sous une fonction déplace cette fonction.
bool deplacerBloc(Piece& p, int id, int sens, std::string* raison = nullptr);
// Nom de variable citable dans une cote : lettres sans accent, chiffres et « _ », pas un nom d'unité (AP-23) ;
// vide s'il convient, sinon l'explication.
std::string defautNomVariable(const std::string& nom);
Json pieceVersJson(const Piece& p);
// Valeurs des variables de la pièce (mm ou degrés) ; erreur renseignée si l'une ne se calcule pas.
std::map<std::string, double> evaluerVariables(const Piece& p, std::string* erreur = nullptr);
// Les mêmes, avec leur dimension (nombre, longueur, angle)
std::map<std::string, Grandeur> evaluerGrandeurs(const Piece& p, std::string* erreur = nullptr);
Unites unitesDe(const Piece& p);  // unités de longueur et d'angle de la pièce
// Recalcule les cotes pilotées par une expression dans toutes les esquisses, puis les résout. Vrai si quelque chose a changé.
bool appliquerVariables(Piece& p, std::string* erreur = nullptr);
// Relecture tolérante : une valeur illisible garde son défaut et est notée dans « alertes » ; aucune valeur lisible
// n'est bornée (la construction refuse ce qui est impossible, avec un message).
Piece pieceDepuisJson(const Json& j, std::vector<std::string>* alertes = nullptr);

}  // namespace prom
