// Prométhée : esquisses paramétriques et solveur de contraintes géométriques. Licence GPL-3.0-only.
// Une esquisse vit dans un plan (repère local u, v en millimètres). Le point d'identifiant 0 est l'origine,
// toujours fixe en (0, 0). Les éléments relient des points : deux lignes qui se suivent partagent le même point.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "promethee/modele.hpp"

namespace prom {

struct PointEsquisse {
  int id = 0;
  double x = 0, y = 0;
};

enum class TypeElement { Ligne, Cercle, Arc };

struct ElementEsquisse {
  int id = 0;
  TypeElement type = TypeElement::Ligne;
  int p1 = -1, p2 = -1;      // ligne : extrémités ; arc : début et fin, parcouru dans le sens direct
  int centre = -1;           // cercle et arc
  double rayon = 5;          // cercle (pour un arc, le rayon découle de ses points)
  bool construction = false; // géométrie d'aide : ne fait pas partie des profils
  std::string calque;        // calque de l'élément (vide : calque par défaut)
};

// Calque d'esquisse : regroupe des éléments, avec leur couleur et leur visibilité.
struct Calque {
  std::string nom;
  std::string couleur = "#1E7BE0";
  bool visible = true;
};

enum class TypeContrainte {
  Coincidence,      // deux points confondus
  Horizontal,       // une ligne, ou deux points à la même hauteur
  Vertical,         // une ligne, ou deux points sur la même verticale
  Parallele,        // deux lignes
  Perpendiculaire,  // deux lignes
  Tangente,         // ligne et cercle/arc, ou deux cercles/arcs (valeur 0 : extérieure, 1 : intérieure)
  Egal,             // deux lignes de même longueur, ou deux cercles/arcs de même rayon
  Fixe,             // un point bloqué en (valeur, valeur2)
  Distance,         // deux points, ou un point et une ligne
  DistanceX,        // écart horizontal signé entre deux points (ou abscisse d'un point)
  DistanceY,        // écart vertical signé entre deux points (ou ordonnée d'un point)
  Longueur,         // une ligne
  Rayon,            // un cercle ou un arc
  Diametre,         // un cercle ou un arc
  Angle,            // deux lignes, en degrés ; valeur2 choisit le secteur mesuré (S2) : 1 première ligne prise à
                    // rebours, 2 la seconde, 3 les deux (angle orienté de l'une à l'autre)
  PointSur,         // un point sur une ligne (prolongée) ou sur un cercle/arc
  Milieu,           // un point au milieu d'une ligne
  Symetrie,         // deux points symétriques par rapport à une ligne
  Concentrique,     // deux cercles ou arcs de même centre
  Colineaire        // deux lignes sur une même droite
};

// Références spéciales utilisables dans les contraintes : l'origine (point 0) et les deux axes de l'esquisse.
inline constexpr int AXE_H = -1;  // axe horizontal (X) passant par l'origine
inline constexpr int AXE_V = -2;  // axe vertical (Y) passant par l'origine

struct ContrainteEsquisse {
  int id = 0;
  TypeContrainte type = TypeContrainte::Coincidence;
  std::vector<int> refs;     // identifiants de points, d'éléments ou d'axes, selon le type
  double valeur = 0, valeur2 = 0;
  bool reference = false;    // cote de référence (pilotée) : affichée, jamais imposée
  bool place = false;        // position de l'étiquette choisie par l'utilisateur
  double lx = 0, ly = 0;     // position de l'étiquette d'une cote, dans le repère de l'esquisse
  std::string expression;    // cote pilotée par une expression (« L/2 », « 2*e + 3 mm ») ; vide sinon
};

// Référence externe associative (S3) : un point de l'esquisse repris d'une arête de la pièce. À chaque reconstruction,
// l'arête est retrouvée par son nom (« A|B », faces qui la bordent) dans le corps tel qu'il est avant l'esquisse ; le
// point fixe qui la représente suit (extrémité la plus proche, ou centre) et, pour un cercle, le rayon aussi.
struct LienExterne {
  int point = 0;          // point de l'esquisse, tenu par une contrainte Fixe
  std::string arete;      // nom de l'arête dans la pièce
  bool centre = false;    // le point est le centre de l'arête circulaire (sinon l'extrémité la plus proche)
  int cercle = 0;         // élément cercle dont le rayon suit celui de l'arête (0 : aucun)
};

struct Esquisse {
  std::vector<PointEsquisse> points;  // le point 0 (origine) est ajouté automatiquement
  std::vector<ElementEsquisse> elements;
  std::vector<ContrainteEsquisse> contraintes;
  std::vector<Calque> calques;        // calques nommés (le calque par défaut, sans nom, n'y figure pas)
  std::vector<LienExterne> liensExternes;  // arêtes de la pièce reprises, qui suivent la pièce (S3)
  int prochainId = 1;
  // Ce que cette version ne sait pas interpréter, gardé pour être réécrit tel quel : champs inconnus de
  // l'esquisse et contraintes d'un type inconnu (non résolues, signalées à la lecture).
  Json autres = Json::object();
  std::vector<Json> contraintesInconnues;

  Esquisse();
  int ajouterPoint(double x, double y);
  int ajouterLigne(int p1, int p2, bool construction = false);
  int ajouterCercle(int centre, double rayon, bool construction = false);
  int ajouterArc(int centre, int debut, int fin, bool construction = false);
  int contraindre(TypeContrainte t, std::vector<int> refs, double valeur = 0, double valeur2 = 0);
  // Rectangle de quatre lignes jointives, horizontales et verticales ; renvoie les identifiants des lignes.
  std::vector<int> ajouterRectangle(double x1, double y1, double x2, double y2);
  // Rectangle par son centre et un coin : diagonales de construction, centre au milieu des deux.
  std::vector<int> ajouterRectangleCentre(double cx, double cy, double coinX, double coinY, int* centre = nullptr);
  // Rectangle incliné par trois points : un côté (a, b), puis la largeur donnée par le troisième point.
  std::vector<int> ajouterRectangle3Points(double ax, double ay, double bx, double by, double cx, double cy);
  // Parallélogramme : deux côtés consécutifs a → b → c, le quatrième sommet s'en déduit.
  std::vector<int> ajouterParallelogramme(double ax, double ay, double bx, double by, double cx, double cy);
  // Cercle passant par trois points (périmétrique) ; renvoie l'identifiant du cercle (0 si les points sont alignés).
  int ajouterCercle3Points(double ax, double ay, double bx, double by, double cx, double cy);
  // Ligne par son milieu : le point « milieu » reste au milieu de la ligne.
  int ajouterLigneMilieu(int milieu, double fx, double fy);
  // Arc tangent à la ligne (ou à l'arc) qui se termine au point « depart », jusqu'à (fx, fy).
  // prefere : élément auquel l'arc doit être tangent (le dernier tracé) ; 0 : celui des éléments qui aboutissent au
  // départ dont le prolongement va le plus vers l'arrivée (ES-34).
  int ajouterArcTangent(int depart, double fx, double fy, int prefere = 0);
  // Géométrie de cet arc tangent (pour l'aperçu) : centre, et sens direct ou non. Faux si impossible.
  bool calculerArcTangent(int depart, double fx, double fy, Point& centre, bool& sensDirect, int* precedent = nullptr, int prefere = 0) const;
  // Symétrie : copie les éléments de part et d'autre d'une ligne (ou d'un axe, AXE_H / AXE_V), les points copiés
  // restant symétriques des originaux. Renvoie les éléments créés.
  std::vector<int> symetriser(const std::vector<int>& elements, int axe);
  // Répétition linéaire : « nombre » exemplaires en tout, décalés de (dx, dy) ; chaque copie reste à cet écart de
  // l'original (cotes horizontale et verticale entre points), les cercles gardent le même rayon.
  std::vector<int> repeterLineaire(const std::vector<int>& elements, double dx, double dy, int nombre);
  // Répétition circulaire autour de (cx, cy) : « nombre » exemplaires répartis sur « angle » degrés (360 : tour complet).
  std::vector<int> repeterCirculaire(const std::vector<int>& elements, double cx, double cy, int nombre, double angle);
  // Déplacer, pivoter, mettre à l'échelle des éléments (t transforme les points, facteur s'applique aux rayons et
  // aux cotes de longueur qui ne portent que sur eux). Avec copie : les éléments transformés sont des copies.
  std::vector<int> transformer(const std::vector<int>& elements, const std::function<Point(Point)>& t, double facteur, bool copie);
  // Décaler : cercles, ou une chaîne de lignes et d'arcs (ouverte ou fermée). Distance positive : vers l'extérieur
  // d'un contour fermé, à droite du sens de parcours pour une chaîne ouverte. Renvoie les éléments créés.
  std::vector<int> decaler(const std::vector<int>& elements, double distance);
  // Ajuster : retire le morceau de l'élément autour de (x, y), entre ses intersections avec les autres éléments
  // (tout l'élément s'il n'en croise aucun). Prolonger : allonge une ligne, par l'extrémité la plus proche de
  // (x, y), jusqu'à la prochaine intersection. Faux si rien n'a changé.
  bool ajuster(int element, double x, double y);
  bool prolonger(int element, double x, double y);
  // Arc passant par trois points (début, fin, et un point de passage) ; renvoie l'identifiant de l'arc.
  int ajouterArc3Points(int debut, int fin, double xPassage, double yPassage);
  // Rainure (oblong) entre deux centres, de rayon donné : deux arcs et deux lignes tangentes.
  std::vector<int> ajouterRainure(double x1, double y1, double x2, double y2, double rayon);
  // Polygone régulier inscrit dans un cercle de construction ; renvoie les identifiants des côtés.
  std::vector<int> ajouterPolygone(double cx, double cy, double sx, double sy, int cotes);
  // Polygone circonscrit : (sx, sy) est le milieu d'un côté (le cercle de construction est inscrit).
  std::vector<int> ajouterPolygoneCirconscrit(double cx, double cy, double mx, double my, int cotes);
  // Chanfrein d'esquisse au coin « point » entre deux lignes : chaque ligne est raccourcie de « distance ».
  // Chanfrein au coin « point » : distances « distance » sur la première ligne et « distance2 » sur la seconde
  // (≤ 0 : égale à la première). Le coin reste comme point virtuel, les cotes suivent. 0 si refusé (rien n'est
  // alors modifié).
  int chanfreinEsquisse(int point, double distance, double cx = 1e300, double cy = 1e300, double distance2 = -1);
  // Rainure en arc : arc médian de centre (cx, cy), du point (ax, ay) au point (bx, by) dans le sens direct,
  // de largeur totale « largeur ». Renvoie les quatre arcs.
  std::vector<int> ajouterRainureArc(double cx, double cy, double ax, double ay, double bx, double by, double largeur);
  // Congé d'esquisse : arrondit l'angle formé par deux lignes qui partagent ce point. Faux si impossible.
  // (cx, cy) : position du clic, pour choisir l'angle à arrondir quand plus de deux lignes partent du point
  bool congeEsquisse(int point, double rayon, std::string* erreur = nullptr, double cx = 1e300, double cy = 1e300);
  void supprimer(int id);    // point, élément ou contrainte, avec ce qui en dépend

  PointEsquisse* point(int id);
  const PointEsquisse* point(int id) const;
  ElementEsquisse* element(int id);
  const ElementEsquisse* element(int id) const;
  double rayonDe(const ElementEsquisse& e) const;
  // Vrai si tous les points que l'élément désigne (extrémités, centre) existent ; un élément qui en vise un
  // absent est ignoré par les opérations (ajuster, prolonger, décaler, diagnostiquer) plutôt que lu (EI-02)
  bool elementValide(const ElementEsquisse& e) const;
};

// Résultat d'une résolution : degrés de liberté restants, contraintes en conflit ou redondantes,
// et pour chaque point s'il peut encore bouger (pour l'affichage : contraint en vert, libre en bleu).
// Diagnostic d'une esquisse : chaque degré de liberté restant expliqué (ce qui peut bouger, et comment le bloquer),
// et les extrémités libres qui laissent un profil ouvert.
struct LiberteEsquisse {
  std::string description;   // « Ligne 12 : longueur libre », « Déplacement horizontal de l'ensemble »
  std::string suggestion;    // « Cote sa longueur (D, puis clic sur la ligne) »
  std::vector<int> elements, points;
};
struct DiagnosticEsquisse {
  int degresLiberte = 0;
  std::vector<LiberteEsquisse> libertes;
  std::vector<int> extremitesLibres;
};
DiagnosticEsquisse diagnostiquer(const Esquisse& e);
// Nouvelle valeur d'une cote réécrite (éditeur, tableau, variable) : les cotes orientées (horizontale, verticale,
// angle) gardent le signe de leur valeur actuelle, que l'affichage ne montre pas (ES-04)
double valeurReecrite(const ContrainteEsquisse& c, double saisie);

struct EtatEsquisse {
  bool resolue = true;
  int degresLiberte = 0;
  double residu = 0;
  std::vector<int> enConflit;     // contraintes impossibles à satisfaire ensemble
  std::vector<int> redondantes;   // contraintes qui n'apportent rien (déjà imposées par les autres)
  std::vector<int> pointsLibres;  // points qui peuvent encore bouger
  std::vector<int> elementsLibres;
  std::string message;            // résumé lisible
};

// Résout l'esquisse en déplaçant le moins possible la géométrie. « glisser » tire des points vers des
// positions souhaitées (la souris) sans jamais violer les contraintes.
EtatEsquisse resoudre(Esquisse& e, const std::vector<std::pair<int, Point>>& glisser = {});

// Valeur actuelle mesurée par une cote (mm, ou degrés pour un angle), qu'elle soit pilotante ou de référence.
double mesurer(const Esquisse& e, const ContrainteEsquisse& c);
bool estCote(TypeContrainte t);

// Boucles fermées formées par les éléments hors construction (profils des fonctions volumiques).
struct Boucle {
  std::vector<std::pair<int, bool>> elements;  // identifiant, parcouru à l'envers
  std::vector<Point> polygone;                 // discrétisation (sens de parcours de la boucle)
  double aire = 0;                             // signée
};
std::vector<Boucle> boucles(const Esquisse& e, std::string* erreur = nullptr);

Json esquisseVersJson(const Esquisse& e);
// Relecture tolérante : ce qui est illisible est écarté ou remplacé par son défaut et noté dans « alertes » ;
// aucune valeur lisible n'est bornée.
Esquisse esquisseDepuisJson(const Json& j, std::vector<std::string>* alertes = nullptr);
const char* nomContrainte(TypeContrainte t);

}  // namespace prom
