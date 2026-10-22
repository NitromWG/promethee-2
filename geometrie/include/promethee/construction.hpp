// Prométhée : construction des pièces paramétriques avec OpenCascade, et nommage topologique.
// Licence GPL-3.0-only.
//
// Chaque face du solide reçoit un nom tiré de son origine : « 3:cote:12 » est la face latérale engendrée par
// l'élément 12 de l'esquisse lors de la fonction 3, « 3:sommet » la face d'arrivée de cette extrusion,
// « 5:conge:… » une face de congé. Ces noms suivent les faces à travers les opérations booléennes et les
// retouches grâce à l'historique d'OpenCascade. Une arête est nommée par les deux faces qu'elle sépare
// (« A|B ») : les congés, chanfreins et coques retrouvent ainsi leurs arêtes et leurs faces quand une cote
// change en amont.
//
// Références persistantes, sans rattachement silencieux : quand une opération coupe une face en morceaux,
// ses morceaux gardent le même nom et une référence en vise un par « #k/n » (k-ième de n morceaux, rangés par
// position). Si le nombre de morceaux change, la référence est déclarée ambiguë ; si le nom n'existe plus,
// elle est déclarée cassée. Dans les deux cas la fonction qui en dépend échoue explicitement (avec la
// référence en cause), les fonctions suivantes sont marquées obsolètes, et la pièce garde le dernier solide
// valide : rien n'est redirigé vers une autre face. Les faces sans origine connue (rares) sont nommées
// « F:face.i:n » (i-ième de n, par position) : un changement de leur nombre casse aussi la référence.
#pragma once

#include <array>
#include <map>

#include <string>
#include <utility>
#include <vector>

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>

#include "promethee/piece.hpp"
#include "promethee/tolerie.hpp"

namespace prom {

struct FaceNommee {
  TopoDS_Face face;
  std::string nom;
};

// Résolution d'une référence enregistrée dans le résultat recalculé.
enum class EtatReference { Trouvee, Introuvable, Ambigue };
struct ResolutionReference {
  EtatReference etat = EtatReference::Introuvable;
  std::string message;  // explication quand la référence est cassée ou ambiguë
  bool trouvee() const { return etat == EtatReference::Trouvee; }
};

struct ResultatPiece {
  TopoDS_Shape forme;                                     // dernier solide valide, vide tant qu'aucune matière n'existe
  std::vector<FaceNommee> faces;                          // nom de chaque face du solide
  std::vector<std::pair<int, std::string>> erreurs;       // fonction en échec et explication
  // Fonction calculée, mais avec une remarque à montrer (région retrouvée par son point, sens de perçage choisi…)
  std::vector<std::pair<int, std::string>> avertissements;
  // Fonctions non recalculées parce qu'une fonction en amont est en échec (la première : premiereErreur).
  std::vector<int> obsoletes;
  int premiereErreur = 0;
  bool obsolete(int fonction) const;
  std::vector<std::pair<int, TopoDS_Shape>> esquisses;    // tracé 3D de chaque esquisse active (profil)
  std::vector<std::pair<int, TopoDS_Shape>> esquissesConstruction;  // et sa géométrie de construction
  std::vector<std::pair<int, int>> etatsEsquisses;  // 0 : profils fermés, 1 : profil ouvert, 2 : problème
  // Géométrie de référence et filetages (affichage, annotation)
  std::vector<std::pair<int, gp_Ax3>> plansReference;
  std::vector<std::pair<int, gp_Ax1>> axesReference;
  std::vector<std::pair<int, gp_Pnt>> pointsReference;
  std::vector<std::pair<int, gp_Ax3>> reperesReference;   // fonctions « repère » (FO-17)
  // Filetages (perçages taraudés, filetages cosmétiques M4) : un axe par trou ou arbre, partant du début du filet
  // et dirigé le long de lui ; diamètres nominal et intérieur (fond de filet d'une vis, perçage d'un taraudage)
  struct Filetage {
    int fonction = 0;
    std::string taille;
    double pas = 0, longueur = 0;
    int nombre = 1;
    std::vector<gp_Ax1> axes;
    double nominal = 0, mineur = 0;
    bool exterieur = false;
    std::string designation;  // « M5×0,8 »
  };
  std::vector<Filetage> filetages;
  int etatEsquisse(int id) const { for (const auto& [i, e] : etatsEsquisses) if (i == id) return e; return 2; }
  std::vector<std::pair<int, gp_Ax3>> reperes;            // repère de chaque esquisse (plan de base ou face)
  DeplieTole tole;                                        // tôlerie : déplié, lignes de pliage (vide sans tôle de base)
  // Esquisses mises à jour par leurs références externes (S3), telles que la reconstruction les a lues
  std::vector<std::pair<int, Esquisse>> esquissesSuivies;
  const Esquisse* esquisseSuivie(int id) const { for (const auto& [i, e] : esquissesSuivies) if (i == id) return &e; return nullptr; }
  gp_Ax3 repere(int esquisse) const;

  std::string nomFace(const TopoDS_Face& f) const;        // vide si inconnue ; « nom#k/n » pour un morceau
  std::string nomArete(const TopoDS_Edge& e) const;       // « A|B » ou « A|B#k/n »
  std::string nomSommet(const TopoDS_Vertex& v) const;   // faces qui s'y rejoignent, « S:A|B|C »
  // Résolution stricte. « unique » : la référence doit désigner exactement une face (plan d'esquisse, face
  // d'arrêt, contrainte d'assemblage) ; sinon une face coupée en morceaux est prise en entier (faces à ouvrir).
  ResolutionReference resoudreFaces(const std::string& reference, std::vector<TopoDS_Face>& faces, bool unique) const;
  ResolutionReference resoudreAretes(const std::string& reference, std::vector<TopoDS_Edge>& aretes) const;
  ResolutionReference resoudreSommet(const std::string& reference, TopoDS_Vertex& sommet) const;
  // Accès tolérants (sélection à l'écran, aperçus) : vide si la référence ne se résout pas strictement.
  std::vector<TopoDS_Edge> aretes(const std::string& reference) const;
  std::vector<TopoDS_Face> facesNommees(const std::string& reference) const;
  std::vector<TopoDS_Vertex> sommets(const std::string& reference) const;
  std::string erreur(int fonction) const;
  std::string avertissement(int fonction) const;
  // Dernière référence qui n'a pas pu être résolue (explication), pour compléter le message d'erreur.
  mutable std::string referenceCassee;
};

// Contrôle B-Rep (OpenCascade BRepCheck) d'un résultat : vide si le solide est valide, sinon le défaut.
std::string defautGeometrie(const TopoDS_Shape& forme);

gp_Ax3 repereEsquisse(PlanBase plan, double decalage);

// Propriétés de masse d'une pièce (mm, g) : volume, surface, masse, centre de gravité, moments d'inertie au
// centre (g·mm²) et moments principaux. Chaque solide est compté avec la bonne orientation. Méthode :
// intégration de Gauss sur la géométrie exacte (B-Rep) ; elle n'est exacte que sur un solide valide et fermé,
// d'où le statut, à afficher avec les valeurs.
enum class StatutMasse {
  Exact,             // un solide valide et fermé
  PlusieursSolides,  // plusieurs solides valides : valeurs exactes, cumulées
  Approche,          // géométrie défectueuse (contrôle B-Rep en échec) : valeurs indicatives seulement
  Vide,              // aucun solide : rien à calculer
  Echec              // le calcul lui-même a échoué
};
std::string libelleStatut(StatutMasse s);
// Dimensions normalisées d'un trou pour une taille de vis.
struct DonneesTrou {
  std::string norme, taille;
  double pas, forageTaraud, passageSerre, passageNormal, passageLarge, lamageD, lamageH, fraisureD;
  double angleFraisure = 90;  // angle au sommet du cône (degrés) : 90° ISO, 82° pour les vis en pouces (FO-18)
};
const std::vector<DonneesTrou>& tableTrous();
const DonneesTrou* donneesTrou(const std::string& norme, const std::string& taille);
// Diamètre nominal d'une taille (M5 → 5 ; #10-24 → 4,83 ; 1/4-20 → 6,35)
double diametreNominal(const DonneesTrou& d);
// Taille normalisée d'un cylindre : arbre de ce diamètre (nominal), ou trou percé à ce diamètre (avant taraudage) ;
// nul si aucune n'est à moins de 0,3 mm
const DonneesTrou* filetagePour(double diametre, bool exterieur, const std::string& norme = "ISO");
// Désignation d'un filetage : « M5×0,8 », « 1/4-20 UNC »
std::string designationFiletage(const std::string& taille, double pas, const std::string& norme);
// Filetages à montrer dans la vue 3D : une hélice au pas du filet sur sa longueur, et un cercle à chaque bout
TopoDS_Shape traceFiletages(const ResultatPiece& r);

struct ProprietesMasse {
  double volume = 0, aire = 0, masse = 0;
  double cx = 0, cy = 0, cz = 0;
  // Moments au centre de gravité, axes parallèles aux axes globaux (g·mm²). ixy, ixz, iyz sont les PRODUITS
  // d'inertie Pxy = ∫(x − x̄)(y − ȳ) dm : le terme hors diagonale du tenseur vaut −Pxy (FO-23).
  double ixx = 0, iyy = 0, izz = 0, ixy = 0, ixz = 0, iyz = 0;
  // Moments principaux, triés i1 ≤ i2 ≤ i3, et leurs axes (vecteurs unitaires, repère global)
  double i1 = 0, i2 = 0, i3 = 0;
  std::array<std::array<double, 3>, 3> axesPrincipaux{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  bool valide = false;  // statut Exact ou PlusieursSolides : valeurs fiables
  StatutMasse statut = StatutMasse::Vide;
  int solides = 0;
  std::string defaut;   // cause, si Approche ou Echec
};
ProprietesMasse proprietesMasse(const TopoDS_Shape& forme, double densiteGcm3);
// Normale sortante d'une face plane, prise telle que la face est orientée dans le solide qui la porte et vérifiée en
// classant un point voisin (juste) de part et d'autre : juste aussi pour une face issue d'une symétrie (repère
// indirect) ou rangée dans la liste des noms avec une autre orientation. Faux si la face n'est pas plane.
bool normaleSortante(const TopoDS_Face& f, const TopoDS_Shape& solide, gp_Pnt& point, gp_Dir& n);
// Repère d'une esquisse posée sur une face plane : normale sortante (Z), origine projetée de l'origine globale, axe X
// donné (axeX, reprojeté sur le plan, mémorisé à la pose de l'esquisse), sinon Z global × normale (X global si la
// face est horizontale) : une règle sans saut quand l'inclinaison de la face varie (FO-08).
bool repereFace(const TopoDS_Face& f, gp_Ax3& repere, const TopoDS_Shape& solide = TopoDS_Shape(), const std::array<double, 3>* axeX = nullptr);

// Rejoue l'arbre jusqu'à la fonction « jusquA » incluse (0 : toutes). Une fonction en échec est signalée et
// ignorée : la suite de l'arbre continue sur le dernier solide valide.
ResultatPiece construirePiece(const Piece& p, int jusquA = 0);
// Apparences par face (Piece::couleursFaces) : faces du résultat et leur couleur « #RRGGBB » ; une référence qui ne
// se résout plus (face disparue) est ignorée, une face coupée en morceaux est colorée en entier.
std::vector<std::pair<TopoDS_Face, std::string>> facesColorees(const std::map<std::string, std::string>& couleurs, const ResultatPiece& r);
// Régions fermées d'une esquisse (découpées par toutes ses courbes, même croisées), avec leur clé (pour les
// choisir à extruder), leur face, et si elles sont retenues par défaut (règle pair-impair).
struct RegionEsquisse {
  std::string cle;
  TopoDS_Face face;
  bool parDefaut = true;
  double u = 0, v = 0;  // point strictement intérieur, dans le repère de l'esquisse (à mémoriser avec la clé, FO-06)
};
std::vector<RegionEsquisse> regionsEsquisse(const FonctionPiece& esquisse, const gp_Ax3& repere);

}  // namespace prom
