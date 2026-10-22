// Prométhée : mise en plan. Vues projetées d'une pièce (arêtes vues et cachées, par élimination des parties
// cachées), disposées sur une feuille normalisée avec cadre et cartouche ; écriture en DXF et en SVG (le PDF est
// produit par l'interface). Licence GPL-3.0-only.
//
// Traits selon l'ISO 128 : contours vus en trait fort ; arêtes cachées en tirets fins (tiret 3 mm, intervalle
// 0,75 mm), sans jamais recouvrir un trait vu (priorité des traits, MP-03) ; axes en trait mixte fin sur les
// cylindres et les trous (ligne d'axe ou croix de centre) ; arêtes tangentes (raccords de congés) omises par défaut
// ou tracées en trait fin (MP-08). La mise en page raisonne en millimètres de papier : l'échelle normalisée la plus
// grande pour laquelle les vues, leurs écarts et leurs étiquettes tiennent dans le cadre sans se toucher ni toucher
// le cartouche est retenue (MP-02, MP-05).
#pragma once

#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

namespace prom {

// Cote : lignes de cote, de rappel et d'attache (trait continu fin) ; Hachure : hachures de coupe (ISO 128-50) ;
// Fin : interruptions, contour d'un détail, cercle qui l'appelle (trait continu fin)
enum class TypeTrait { Vu, Cache, Axe, Tangente, Cote, Hachure, Fin };

struct Trait2D {
  std::vector<std::pair<double, double>> points;  // polyligne, en millimètres sur la feuille
  TypeTrait type = TypeTrait::Vu;
  bool rempli = false;  // contour fermé rempli (pointes de flèche)
  [[nodiscard]] bool cache() const { return type == TypeTrait::Cache; }
};

// Motifs normalisés (mm sur le papier), communs au PDF, au SVG et au DXF (MP-10)
struct MotifTrait {
  double epaisseur;
  std::vector<double> motif;  // longueurs alternées trait / vide ; vide : trait continu
};
MotifTrait motifTrait(TypeTrait t);
constexpr double EPAISSEUR_CADRE = 0.7;

struct VueDessin {
  std::string nom;
  double cx = 0, cy = 0;     // centre de la vue sur la feuille (mm, origine en bas à gauche)
  double largeur = 0, hauteur = 0;
  double echelle = 1;        // échelle propre de la vue (l'isométrique peut être réduite, et le dit)
  std::vector<Trait2D> traits;
  // Passage du modèle à la feuille : projection selon « repere », point (ox, oy) de la projection au centre, et
  // interruption éventuelle (abscisses projetées de coupureDebut à coupureFin retirées, ecart laissé entre les morceaux)
  gp_Ax2 repere;
  double ox = 0, oy = 0;
  double coupureDebut = 0, coupureFin = 0, ecart = 0;
  [[nodiscard]] bool interrompue() const { return coupureFin > coupureDebut; }
};
// Point du modèle sur la feuille ; faux s'il tombe dans la partie retirée d'une vue interrompue
bool versFeuille(const VueDessin& v, const gp_Pnt& p, double& x, double& y);

struct Feuille {
  std::string format = "A4";          // A0 à A4
  double largeur = 297, hauteur = 210;
  bool troisiemeDiedre = false;
  double echelle = 1;                 // 2 : « 2:1 », 0.5 : « 1:2 »
  std::string titre, materiau, auteur, date, reference, masse;
  // Propriétés personnalisées de la pièce reprises au cartouche (M10) : indice de révision sous le numéro de pièce,
  // désignation sous le titre, projet à côté de la tolérance générale
  std::string revision, designation, projet;
  std::vector<VueDessin> vues;
  std::vector<Trait2D> cadre;         // cadre et cartouche
  struct Texte {
    double x, y, taille;
    std::string texte;
    bool gras = false;
    bool centre = false;              // x est le milieu du texte
    double angle = 0;                 // degrés, sens direct (cote verticale : 90)
  };
  std::vector<Texte> textes;
  bool tientDansLeCadre = true;       // faux si l'échelle imposée fait sortir une vue
  std::vector<std::string> alertes;   // ce qui n'a pas pu être placé (détail, bulles…)
};

// Projection d'une forme selon un repère de vue (sa direction pointe vers l'observateur ; X vers la droite).
// tangentes : garder les arêtes de continuité tangente (type Tangente) ; axes : ajouter les axes des cylindres.
std::vector<Trait2D> projeter(const TopoDS_Shape& forme, const gp_Ax2& vue, bool cachees, bool tangentes = false, bool axes = true);

// Feuille de mise en plan : vue de face choisie, une vue projetée verticale et une latérale (premier ou troisième
// dièdre) et isométrique au choix ; échelle normalisée choisie pour que tout tienne si echelle vaut 0.
struct OptionsMiseEnPlan {
  std::string format = "A4";      // A0, A1, A2, A3 ou A4
  bool portrait = false;          // à la française (sinon à l'italienne)
  // Projection (D1) : européenne (premier dièdre : vue de dessus sous la face, vue de gauche à sa droite) ou
  // américaine (troisième dièdre : vue de dessus au-dessus, vue de droite à droite)
  bool troisiemeDiedre = false;
  // Vue de face : côté d'où l'on regarde la pièce (« avant », « arriere », « dessus », « dessous », « gauche »,
  // « droite »), tournée de rotationFace degrés (0, 90, 180 ou 270) ; vues projetées voulues : « dessus » ou
  // « dessous », « gauche » ou « droite » (par rapport à la vue de face)
  std::string vueFace = "avant";
  int rotationFace = 0;
  std::string vueVerticale = "dessus", vueLaterale = "gauche";
  // Cartouche ISO 7200 (D2) : propriétaire légal, approbateur, type de document, numéro de feuille
  std::string proprietaire, approbateur, typeDocument = "Dessin de définition", numeroFeuille = "1/1";
  double echelle = 0;
  bool face = true, dessus = true, gauche = true, iso = true, cachees = true;
  bool axes = true;              // axes des cylindres et des trous
  bool tangentes = false;        // arêtes tangentes en trait fin
  std::string titre, materiau, auteur, date, reference, masse;
  // Propriétés personnalisées de la pièce reprises au cartouche (M10) : indice de révision sous le numéro de pièce,
  // désignation sous le titre, projet à côté de la tolérance générale
  std::string revision, designation, projet;
  // Coupe (M6) : « face », « dessus » ou « gauche » remplace cette vue par la coupe A-A, par un plan parallèle à
  // son plan de projection, passant par le centre de la pièce décalé de « decalageCoupe » mm ; le plan de coupe
  // est tracé dans une vue voisine (trait mixte, extrémités fortes, flèches et lettres), la matière coupée hachurée
  std::string coupe;
  double decalageCoupe = 0;
  bool demiCoupe = false;  // D4 : demi-coupe, la moitié droite de la vue en coupe, l'autre vue de l'extérieur
  // Vue de détail (M6) : cercle dans une vue (« face », « dessus », « gauche »), centré à (detailX, detailY) de
  // la vue (fractions de sa largeur et de sa hauteur depuis son centre), de rayon detailRayon × sa plus grande
  // dimension, agrandi detailEchelle fois et placé à la première place libre de la feuille
  bool detail = false;
  std::string vueDetail = "face";
  double detailX = 0.25, detailY = 0.25, detailRayon = 0.15, detailEchelle = 2;
  // Vue interrompue (M6) : face et dessus privées de la bande de interruptionDebut à interruptionFin (fractions de
  // la longueur en X), raccordées par des traits d'interruption ; les cotes gardent la vraie longueur
  bool interrompue = false;
  double interruptionDebut = 0.3, interruptionFin = 0.7;
  // Cotes automatiques (M6) : encombrement (longueur, hauteur, profondeur) et diamètres des trous et cylindres
  // (« 4× Ø5 »), une fois chacun, dans la vue où ils se voient en vrai cercle
  bool cotes = false;
  std::vector<std::string> notes;  // notes générales, au-dessus du cartouche (« Arêtes cassées 0,5 × 45° »)
  std::string toleranceGenerale;   // « ISO 2768-mK » : écrite dans le cartouche
  // Plan d'assemblage (M6) : nomenclature au-dessus du cartouche (repère, quantité, désignation, matière,
  // référence) et bulles reliées à un point de chaque pièce dans la vue isométrique (la vue de face sans elle)
  struct LigneNomenclature {
    int repere = 0, quantite = 1;
    std::string designation, materiau, reference;
  };
  std::vector<LigneNomenclature> nomenclature;
  struct Bulle {
    int repere = 0;
    gp_Pnt ancre;  // point de la pièce (modèle) d'où part la ligne de repère
  };
  std::vector<Bulle> bulles;
  // Filetages (M4), représentés selon l'ISO 6410 : en bout, trois quarts de cercle en trait fin (fond de filet d'une
  // vis, diamètre nominal d'un taraudage) ; de côté, deux traits fins le long du filet et sa limite en trait fort
  // (en tirets pour un taraudage caché) ; une indication « 2× M5×0,8 - 8 » dans la vue où le filet se voit en bout
  struct FiletageDessin {
    gp_Ax1 axe;  // début du filet, dirigé le long de lui
    double nominal = 0, mineur = 0, longueur = 0;
    bool exterieur = false;
    std::string designation;
  };
  std::vector<FiletageDessin> filetages;
  // Cotes du modèle (D3) : cotes des esquisses de la pièce, recalculées avec elle (associatives), posées dans la
  // vue qui voit leur plan en vraie grandeur ; une cote de référence est écrite entre parenthèses. Celles qu'aucune
  // vue ne montre, et les cotes orphelines (esquisse en échec, élément disparu), sont dites dans les alertes.
  struct CoteModele {
    enum class Genre { Lineaire, Diametre, Rayon, Angle } genre = Genre::Lineaire;
    gp_Pnt a, b;              // linéaire : points mesurés ; diamètre et rayon : centre (a), point du cercle (b) ; angle : sommet (a)
    gp_Pnt c;                 // angle : un point sur chacun des côtés (b et c)
    bool direction = false;   // linéaire : mesurée le long de « sens » (cote horizontale ou verticale de l'esquisse)
    gp_Dir sens;
    gp_Dir normale;           // normale du plan de l'esquisse
    bool place = false;       // position de l'étiquette choisie dans l'esquisse
    gp_Pnt etiquette;
    double valeur = 0;        // mm ou degrés
    bool reference = false;
    std::string nom;          // « Esquisse 3 · cote 12 », pour les alertes
  };
  std::vector<CoteModele> cotesModele;
  std::vector<std::string> cotesOrphelines;
};
Feuille miseEnPlan(const TopoDS_Shape& forme, const OptionsMiseEnPlan& o);
// Dimensions d'une feuille ISO 216 (mm) : A0 à A4, à l'italienne ou à la française ; A4 pour un format inconnu
void dimensionsFeuille(const std::string& format, bool portrait, double& largeur, double& hauteur);
// Échelle écrite exactement (« 2:5 », « 3:2 ») ; les échelles de l'ISO 5455 donnent « 1:2 », « 5:1 »… (MP-09)
std::string texteEchelle(double echelle);
// Échelle la plus proche de la série ISO 5455 (une échelle relue d'un fichier y est ramenée)
double echelleNormalisee(double echelle);

// Écriture atomique : le fichier n'est remplacé que s'il a été entièrement écrit (MP-07). Textes échappés en SVG
// (MP-01), codés en Windows-1252 avec l'en-tête R12 qui le déclare en DXF (MP-04).
bool ecrireDxf(const Feuille& f, const std::string& chemin);
bool ecrireSvg(const Feuille& f, const std::string& chemin);
std::string echapperXml(const std::string& texte);
std::string versCp1252(const std::string& utf8);

}  // namespace prom
