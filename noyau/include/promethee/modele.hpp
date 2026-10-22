// Prométhée : modèle unique carte + boîtier. Licence GPL-3.0-only.
// Port fidèle du noyau du prototype web (jalon 0) : mêmes règles, mêmes valeurs, même format de fichier.
#pragma once

#include <array>
#include <map>
#include <optional>

#include "promethee/electrique.hpp"
#include "promethee/electronique.hpp"
#include "promethee/exigences.hpp"
#include "promethee/systeme.hpp"
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace prom {

using Json = nlohmann::ordered_json;

// ---------- Données de référence ----------
struct Vis { double d, trou, avant, tete; };
const Vis& vis(const std::string& nom);              // "M2", "M2.5", "M3", "M4", "M5", "M6"
bool visConnue(const std::string& nom);
const std::vector<std::string>& nomsVis();

// Lamage d'une vis à tête cylindrique à six pans creux (ISO 4762), pour la tête d'une vis posée par le dessous du
// boîtier (3.20, MEC-12) : diamètre et profondeur, ceux de la table des trous de la mécanique (série moyenne)
struct Lamage { double d, h; };
std::optional<Lamage> lamageISO4762(const std::string& nomVis);

// Insert laiton posé à chaud dans un pilier imprimé : diamètre de perçage et longueur.
struct Insert { double trou, longueur; };
const Insert& insert(const std::string& nomVis);

struct Levre {
  static constexpr double jeu = 0.25;  // jeu entre lèvre du couvercle et paroi
  static constexpr double ep = 1.2;    // épaisseur de la lèvre
  static constexpr double h = 2.0;     // hauteur de la lèvre
};
inline constexpr double PAROI_PILIER = 2.4;
inline constexpr std::array<double, 5> EPAISSEURS_PCB{0.8, 1.0, 1.2, 1.6, 2.0};
inline constexpr std::array<int, 10> LONGUEURS_VIS{4, 5, 6, 8, 10, 12, 16, 20, 25, 30};

struct Decoupe { double w = 0, h = 0, r = 0; };

struct TypeComposant {
  std::string cle, nom, prefixe;
  double w = 1, d = 1, h = 1;
  bool bord = false;        // connecteur de bord : perce une paroi
  Decoupe decoupe{};
  double zc = 0;            // hauteur de l'axe au-dessus de la carte
  bool rond = false;
  double couvercle = 0;     // diamètre du perçage dans le couvercle (0 = aucun)
  bool bouton = false;      // doit affleurer le couvercle
  std::string valeur;
};
const TypeComposant* typeComposant(const std::string& cle);   // nullptr si inconnu
const std::vector<TypeComposant>& typesComposants();

enum class Bord { N, S, E, W };
char lettre(Bord b);
std::optional<Bord> bordDe(const std::string& s);
inline bool horizontal(Bord b) { return b == Bord::N || b == Bord::S; }

// ---------- Projet ----------
struct Point { double x, y; };

// Élément de contour : segment, ou arc passant par un point milieu (comme dans KiCad).
struct ElementContour {
  Point debut{}, fin{};
  std::optional<Point> milieu;  // présent pour un arc
};
struct Contour {
  std::vector<ElementContour> elements;  // dans l'ordre de parcours
  bool ferme = false;
  double aire() const;                   // aire signée du polygone approché (positive dans le sens direct)
  std::vector<Point> polygone(double pas = 0.25) const;  // discrétisation, arcs compris
};

// Carte : rectangle à angles arrondis (L, W, r), ou contour quelconque si « contours » n'est pas vide
// (le premier est l'extérieur, les suivants des découpes) ; L, W, x0 et y0 décrivent alors son encombrement.
struct Carte {
  double x0 = 0, y0 = 0, L = 60, W = 40, r = 2, t = 1.6;
  std::vector<Contour> contours;
  bool libre() const { return !contours.empty(); }
};

// Impression 3D des pièces du boîtier (6.16, MEC-28) : matière, densité, prix au kilo, débit et prix de la machine
struct Impression {
  std::string matiere = "PETG";
  double densite = 1.27;   // g/cm³
  double prixKg = 25;      // € par kilo de filament
  double debit = 8;        // mm³/s déposés
  double prixHeure = 1.5;  // € par heure de machine
  friend bool operator==(const Impression&, const Impression&) = default;
};
struct Boitier {
  double jeu = 1, paroi = 2, fond = 2, entretoise = 5, hauteur = 20, couvercle = 2;
  std::string vis = "M3";
  bool hauteurAuto = true;  // la hauteur suit le composant le plus haut
  double margeDebord = 1;   // marge ajoutée au jeu autour des composants qui dépassent de la carte
  // Type (6.13) : « couvercle » (fond et couvercle posé), « coques » (coques haute et basse, coupées à mi-hauteur)
  std::string type = "couvercle";
  Impression impression;
};

struct Trou {
  std::string id, ref;
  std::optional<std::string> coin;  // "NE", "NW", "SE", "SW" : suit ce coin de la carte
  double ox = 4, oy = 4;            // distances au coin (trou ancré)
  double x = 0, y = 0;              // position libre (trou non ancré)
  std::string ancrage = "auto";     // "auto" (coin le plus proche à moins de 15 mm), "libre", ou un coin imposé
  std::optional<std::string> vis;   // vis propre à ce trou ; sinon celle du projet
  std::string fixation = "autotaraudeuse";  // ou "insert" (insert laiton posé à chaud)
  // Sens de la vis (3.20, MEC-12) : « dessus » (vis dans la carte, vissée dans le pilier), ou « dessous » (vis passée
  // par le fond du boîtier, tête dans un lamage ISO 4762) ; nervures de renfort au pied du pilier
  std::string pose = "dessus";
  bool nervures = false;
  // Dimensions personnalisées (diamètres en mm) ; à défaut, celles de la vis et de l'insert choisis.
  std::optional<double> diamTrou, diamPastille, diamPilier, diamLogement, longueurInsert;
};

struct Composant {
  std::string id, ref, type, valeur;
  double w = 1, d = 1, h = 1;
  double x = 0, y = 0;              // composant libre : centre
  int rot = 0;                      // 0, 90, 180, 270
  std::optional<Bord> bord;         // connecteur de bord
  double le_long = 0, ecart = 0.4, zc = 0;
  Decoupe decoupe{};
  std::optional<double> trou_couvercle;
  // Contrainte de position quand la carte change de taille.
  // Composant libre : "libre", "NE", "NW", "SE", "SW" (suit ce coin), "centre" (suit le centre).
  // Connecteur de bord : "libre", "debut", "fin" (suit une extrémité du bord), "milieu".
  std::string ancrage = "libre";
  double ax = 0, ay = 0;            // décalages d'ancrage
  bool verrou = false;              // ne se déplace pas à la souris ni au clavier
  std::string origine;              // « kicad » : placé par la CAO électronique, qui vérifie déjà l'implantation
  bool dessous = false;             // posé sur la face arrière, sous la carte
  // D'où vient la hauteur : « type » (valeur typique du type), « estimee » (nom d'empreinte KiCad), « nominale »
  // (corps générique de la bibliothèque d'empreintes), « saisie » (tapée pour ce composant, non confirmée) ou
  // « verifiee » (fiche technique, mesure ou modèle exact).
  // Seule une hauteur vérifiée rend le contrôle du boîtier complet.
  std::string sourceHauteur = "type";
};

// Carte importée de KiCad : de quoi y réécrire le contour et les trous (aller-retour).
struct SourceKicad {
  std::string fichier;   // chemin du .kicad_pcb d'origine
  double dx = 0, dy = 0; // décalage appliqué à l'import (repère KiCad, axe Y retourné) pour recentrer la carte
};

// Version du format .prom.json écrit par ce logiciel ; les versions antérieures sont migrées à la lecture.
inline constexpr int VERSION_FORMAT = 3;

// Schéma électrique relié : fichier QElectroTech (.qet), relu à l'ouverture.
struct SchemaElectrique {
  std::string fichier, titre;
};

// Assemblage : pièces du projet placées dans l'espace (translation en mm, rotations en degrés autour de X, Y, Z).
struct ComposantAssemblage {
  int piece = 0;   // indice de la pièce dans le projet
  double x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0;
  bool fixe = false;      // le premier composant est fixe d'office
  int assemblage = -1;    // ≥ 0 : ce composant est un sous-assemblage (indice dans le projet), et non une pièce
  int miroir = 0;         // symétrie avant placement : 0 aucune, 1 plan XY, 2 plan XZ, 3 plan YZ
  bool masque = false;    // caché dans la vue (reste dans l'assemblage)
  bool supprime = false;  // suspendu : ni affiché ni résolu, mais gardé dans l'arbre
};
// Contrainte entre deux composants : « coincidence » (faces planes face à face, ou alignées si « inverse »,
// à « valeur » mm l'une de l'autre), « concentrique » (axes de deux faces cylindriques), « parallele »,
// « perpendiculaire », « angle » (faces planes, « valeur » en degrés), « tangente » (face plane et face
// cylindrique). Une contrainte « suspendue » reste dans l'arbre sans être résolue.
struct ContrainteAssemblage {
  std::string type = "coincidence";
  int a = 0, b = 1;            // composants
  std::string refA, refB;      // noms de faces dans leurs pièces
  double valeur = 0;
  bool inverse = false;
  bool suspendue = false;
  // Sens (A1) : parallèle et concentrique, 0 le plus proche, 1 directions de même sens, 2 opposées ; angle, 0 non
  // orienté (0 à 180°), 1 orienté autour de l'axe mémorisé dans « parametres » (repère du composant a), 2 l'inverse
  int alignement = 0;
  // Liaisons mécaniques : « pivot », « glissiere », « rotule », « engrenage », « vis ». Une liaison « pilotee »
  // impose sa valeur (angle du pivot ou de la vis en degrés, course de la glissière en mm). « parametres » garde
  // l'état de départ (rotations initiales des deux composants, position axiale, rapport, pas).
  bool pilotee = false;
  std::vector<double> parametres{};
  // Angles (degrés) atteints à la dernière résolution réussie, déroulés d'un tour à l'autre : rotation relative d'un
  // pivot, d'une glissière ou d'une vis ; rotations des deux roues d'un engrenage. Ils lèvent l'ambiguïté du tour
  // au-delà de 180° (AS-06).
  std::array<double, 2> angleSuivi{0, 0};
};
struct Assemblage {
  std::string nom = "Assemblage";
  int repere = 0;  // repère de nomenclature stable, comme ceux des pièces (sous-assemblage dans une nomenclature à niveaux)
  std::vector<ComposantAssemblage> composants;
  std::vector<ContrainteAssemblage> contraintes;
};

struct Projet {
  std::string nom = "Projet sans nom";
  Carte carte;
  Boitier boitier;
  std::vector<Trou> trous;
  std::vector<Composant> composants;
  std::optional<SourceKicad> kicad;
  std::vector<Json> pieces;   // pièces paramétriques du projet (voir piece.hpp), gardées sous leur forme JSON
  bool sansCarte = false;     // projet sans carte électronique (une pièce seule, par exemple)
  std::vector<SchemaElectrique> schemasElectriques;  // schémas QElectroTech reliés au projet
  std::vector<Assemblage> assemblages;
  // Cartes électroniques conçues dans Prométhée : schéma et circuit imprimé, un seul modèle (voir electronique.hpp)
  std::vector<ecad::ConceptionElec> electronique;
  // Conceptions électroniques qui n'ont pas pu être relues du tout : gardées telles quelles et réécrites à
  // l'enregistrement, pour qu'un fichier abîmé ne perde jamais une carte en silence (EL-07)
  std::vector<Json> electroniqueIllisible;
  // Projets électriques conçus dans Prométhée (armoires, machines, logements) : folios, appareils, borniers,
  // câbles, armoires, faisceaux (voir electrique.hpp) ; ceux qui n'ont pas pu être relus sont gardés tels quels
  std::vector<elec::ProjetElectrique> electrique;
  std::vector<Json> electriqueIllisible;
  // Dispositions des panneaux de l'atelier électronique enregistrées avec le projet (lot L1, tâche 1.21, ERG-23) :
  // nom → description JSON (format « promethee-disposition », état des panneaux en base64) ; c'est un réglage de
  // travail, sans pas d'annulation
  std::map<std::string, std::string> dispositions;
  // Exigences du projet et ordres de modification (lot L19, 19.6, 19.8, 19.9 ; exigences.hpp)
  std::vector<Exigence> exigences;
  std::vector<OrdreModification> ordresModification;
  // Schéma de système (lot L12, 12.19) : cartes du projet reliées par des câbles, connecteur à connecteur
  std::vector<LiaisonSysteme> liaisons;
  // Champs de premier niveau inconnus de cette version (fichier écrit par une version voisine) : gardés tels
  // quels et réécrits à l'enregistrement, pour ne rien perdre en silence.
  Json inconnus = Json::object();
};

// Compte rendu d'une lecture : format du fichier et tout ce qui a été ignoré, borné ou migré.
struct RapportLecture {
  int versionFormat = 0;  // 1 : prototype web (sans numéro de format)
  std::vector<std::string> avertissements;
};

// Lecture tolérante : toute valeur absente, illisible ou hors bornes est remplacée ou bornée ; ce qui est
// écarté (type de composant inconnu, nombre maximal dépassé) est consigné dans « avertissements ».
Projet normaliser(const Json& source, std::vector<std::string>* avertissements = nullptr);
// « avecGeometries » : textes des géométries importées joints (fichier) ; sans eux, l'instantané d'annulation ne
// porte que leurs références (EX-06)
Json versJson(const Projet& p, bool avecGeometries = true);
// Lit un projet (chemin UTF-8). Lève ErreurFichier (fichier.hpp) avec un message précis : fichier vide,
// tronqué (position de la coupure), JSON invalide, autre format, format plus récent que ce logiciel.
Projet lireProjet(const std::string& chemin, RapportLecture* rapport = nullptr);
// Écriture atomique (voir fichiers.hpp). Refuse d'écraser un projet d'un format plus récent ; avant de
// réécrire au format courant un projet d'un format plus ancien, en garde une copie « <fichier>.formatN.bak ».
void ecrireProjet(const Projet& p, const std::string& chemin);
// Format d'un contenu de projet : numéro de version, 1 pour le prototype web, 0 si ce n'est pas un projet lisible.
int formatProjet(const std::string& contenu);
Projet exemple();
Projet projetVide();
// Recale L, W, x0, y0 sur l'encombrement du contour d'une carte libre.
void recalerEncombrement(Carte& c);
// Oriente les contours dans le sens direct et met l'extérieur (le plus grand) en premier.
void orienterContours(std::vector<Contour>& contours);
// Lien électronique-mécanique : le contour, les trous de fixation et les composants (avec la hauteur de leur corps)
// du circuit imprimé n° « indice » deviennent la carte du projet, d'où le boîtier est dérivé.
// Rend les remarques à faire (composant tourné hors quart de tour…). Les réglages faits côté boîtier (fixation,
// vis, perçage de couvercle, découpe) sont gardés par identifiant de composant.
std::vector<std::string> appliquerCarteElectronique(Projet& p, std::size_t indice);
// La carte n° « indice » dans le boîtier qu'elle pilote (6.4, DRC-13 du catalogue) : hauteur sous le couvercle
// (DRC-45) et collisions avec les piliers, les parois et le fond (DRC-46), en violations de la DRC qui visent le
// composant ; vide si la carte ne pilote pas le boîtier
std::vector<ecad::Violation> verifierCarteDansBoitier(const Projet& p, std::size_t indice);

// ---------- Outils géométriques ----------
Point posTrou(const Projet& p, const Trou& t);
void ancrerTrou(const Projet& p, Trou& t, double x, double y);  // ancre au coin le plus proche si à moins de 15 mm
void placerSurBord(const Projet& p, Composant& k, double x, double y);
void changerAncrageTrou(const Projet& p, Trou& t, const std::string& ancrage);  // garde la position actuelle
// Position d'un composant selon son ancrage, et placement qui respecte cet ancrage.
Point posComposant(const Projet& p, const Composant& k);
double leLongComposant(const Projet& p, const Composant& k);
void placerLeLong(const Projet& p, Composant& k, double u);  // position le long du bord, selon l'ancrage
void placerComposant(const Projet& p, Composant& k, double x, double y);
void changerAncrage(const Projet& p, Composant& k, const std::string& ancrage);  // garde la position actuelle
void contraindreBords(Projet& p);  // ramène les connecteurs de bord sur la longueur du bord
bool ancrageValide(const Composant& k, const std::string& ancrage);
int longueurVis(double epaisseurCarte, const Vis& v);
// Distance signée d'un point à un rectangle arrondi (négative à l'intérieur).
double sdRR(double px, double py, double cx, double cy, double hx, double hy, double r);
std::string prochaineRef(const Projet& p, const std::string& prefixe, const void* exclure = nullptr);
std::string nouvelId(const std::string& prefixe, const std::vector<std::string>& existants);

// ---------- Texte ----------
// Nombre au format français : virgule décimale, zéros inutiles retirés, signe moins typographique.
std::string fmt(double v, int decimales = 1);
double arrondiJs(double x);  // arrondi identique à Math.round

}  // namespace prom
