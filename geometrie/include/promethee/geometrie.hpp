// Prométhée : pièces mécaniques exactes (B-rep OpenCascade) tirées du modèle unique.
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <TopoDS_Shape.hxx>

#include "promethee/derive.hpp"
#include "promethee/modele.hpp"

namespace prom {

// Corps du boîtier et couvercle, en position assemblée (coordonnées du modèle, en mm).
TopoDS_Shape construireCorps(const Projet& p, const Derive& d);
TopoDS_Shape construireCouvercle(const Projet& p, const Derive& d);

// Carte électronique percée de ses trous de fixation, et volume simplifié d'un composant (pour l'affichage 3D).
TopoDS_Shape construireCarte(const Projet& p, const Derive& d);
TopoDS_Shape construireComposant(const Derive& d, const GeoComp& g);
// Contour d'une carte libre décalé (parois du boîtier), discrétisé pour le dessin ; vide pour une carte rectangulaire.
std::vector<Point> contourDecale(const Projet& p, const Derive& d, double decalage, double pas = 0.4);

double volume(const TopoDS_Shape& s);           // mm³
bool estValide(const TopoDS_Shape& s);          // topologie et géométrie saines
int nombreSolides(const TopoDS_Shape& s);

// Boîte englobante ; « vide » pour une forme nulle ou sans géométrie (coordonnées alors nulles, FO-27).
struct Boite {
  double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
  bool vide = false;
};
Boite encombrement(const TopoDS_Shape& s);

// Pièce posée sur le plateau d'impression : centrée, face d'appui à z = 0, couvercle retourné.
TopoDS_Shape pourImpression(const TopoDS_Shape& s, bool retourner, double cx, double cy);

struct PieceNommee {
  std::string nom;
  TopoDS_Shape forme;
  double r = 0.8, g = 0.8, b = 0.8;   // couleur sRGB, composantes entre 0 et 1
  // Apparences par face (faces de « forme », comparées sans leur placement) : couleur sRGB propre à chaque face
  std::vector<std::pair<TopoDS_Shape, std::array<double, 3>>> couleursFaces{};
};
// STEP : schéma d'application (AP214 lu partout ; AP242, la norme actuelle d'échange 3D) et en-tête du fichier.
// Les noms gardent tous leurs caractères (codage \X2\…\X0\ de l'ISO 10303-21).
enum class SchemaStep { AP214, AP242 };
struct OptionsStep {
  SchemaStep schema = SchemaStep::AP214;
  std::string auteur, organisation;  // FILE_NAME de l'en-tête (vides : non renseignés)
};
// Couleur « #RRGGBB » d'une pièce en composantes sRGB entre 0 et 1 (gris 0,8 si le texte n'en est pas une)
void couleurDe(const std::string& hex, double& r, double& g, double& b);
// STEP avec noms, couleurs par pièce et par face, lisible par FreeCAD, KiCad, Fusion, Inventor, SolidWorks.
void exporterStep(const std::vector<PieceNommee>& pieces, const std::string& chemin, const OptionsStep& options = {});
// Lit un fichier de CAO (STEP, IGES, BREP, STL) et rend sa géométrie au format BRep texte (pour la garder dans le
// projet) ; chaîne vide et erreur renseignée si la lecture échoue.
std::string lireFichierCAO(const std::string& chemin, std::string* erreur = nullptr);
TopoDS_Shape formeDepuisBrep(const std::string& texte);
std::string brepDepuisForme(const TopoDS_Shape& forme);
TopoDS_Shape lireStep(const std::string& chemin);
void exporterStl(const TopoDS_Shape& s, const std::string& chemin, double fleche = 0.02);

}  // namespace prom
