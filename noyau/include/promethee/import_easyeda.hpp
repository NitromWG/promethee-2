// Prométhée : composants EasyEDA et LCSC (lot L10 de la 0.34 ; 10.6 ; ECH-06, BIB-13). Lit la réponse JSON de l'API
// des composants d'EasyEDA (format standard : « dataStr » du symbole, « packageDetail » de l'empreinte, numéro LCSC),
// que l'utilisateur enregistre depuis son navigateur ou que le programme d'accès en ligne (D13) récupère : Prométhée
// n'ouvre lui-même aucune connexion. Symbole (broches, rectangles, polylignes, cercles), empreinte (pastilles
// rondes, rectangulaires, oblongues et polygonales, perçages et fentes, sérigraphie, trous), article (fabricant, MPN,
// code LCSC) et modèle 3D téléchargé à côté (OBJ d'EasyEDA changé en STL, ou STEP), posé selon le nœud 3D de
// l'empreinte. Licence GPL-3.0-only.
#pragma once

#include <string>

#include "promethee/articles.hpp"

namespace prom::ecad {

// « modele » : contenu du fichier 3D téléchargé à côté (OBJ ou STEP), vide s'il n'y en a pas ; « nomModele » son nom
ImportArchive importerComposantEasyEda(const std::string& json, const std::string& modele = {}, const std::string& nomModele = {});
// Identifiant du modèle 3D que nomme l'empreinte (attribut « uuid » du nœud 3D), pour le télécharger ; vide sinon
std::string uuidModeleEasyEda(const std::string& json);
// Maillage OBJ (en mm, y vers le haut, z au-dessus de la carte) en STL texte dans le repère de l'empreinte (y vers le
// bas), décalé de (dx, dy, dz) mm et tourné de « rz » degrés autour de z
std::string stlDepuisObj(const std::string& obj, double dx = 0, double dy = 0, double dz = 0, double rz = 0);

}  // namespace prom::ecad
