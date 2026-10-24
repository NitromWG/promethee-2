// Prométhée : vue 3D d'une carte électronique conçue dans Prométhée — plaque à la couleur de son vernis, cuivre des
// deux faces (pastilles étamées, pistes et zones sous le vernis), corps des composants à leur hauteur réelle.
// Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <TopoDS_Shape.hxx>

#include "promethee/electronique.hpp"
#include "promethee/geometrie.hpp"

namespace prom {

struct Carte3D {
  TopoDS_Shape forme;
  std::vector<std::pair<TopoDS_Shape, std::string>> couleurs;  // sous-formes et couleur « #RRGGBB »
};
Carte3D modele3DCarte(const ecad::ConceptionElec& c);
// Pièces de la carte équipée (6.9, MEC-07, T10) : la carte (plaque et cuivre), puis un produit par composant, nommé de
// son repère et de sa valeur, à sa couleur ; un modèle exact remplace la boîte
std::vector<PieceNommee> piecesCarte(const ecad::ConceptionElec& c);
// Mêmes pièces, la carte avec ses couleurs par face (vernis, pastilles, sérigraphie) : exports 3D de 10.10
std::vector<PieceNommee> piecesCarteCouleurs(const ecad::ConceptionElec& c);
void exporterStepCarte(const ecad::ConceptionElec& c, const std::string& chemin, const OptionsStep& options = {});
// Corps d'un composant posé, dans le repère de la carte : son modèle exact s'il en a un, calé et réglé, sinon sa boîte
TopoDS_Shape corpsComposant(const ecad::ConceptionElec& c, const ecad::EmpreintePlacee& e);
// Modèle 3D lu d'un fichier (STEP, IGES, BREP) : forme gardée en BRep, boîte brute ; vide et erreur renseignée sinon
std::optional<ecad::Modele3D> lireModele3D(const std::string& chemin, std::string* erreur = nullptr);

// Pièces du boîtier pour l'impression 3D (6.16, MEC-28) : posées à plat, avec leur volume, leur masse selon la matière
// et le coût estimé (filament au kilo, temps de machine) ; un boîtier en coques donne la coque basse et la coque haute
struct PieceImprimee {
  std::string nom;
  TopoDS_Shape forme;
  double volumeMm3 = 0, masseG = 0, dureeH = 0, cout = 0;
};
std::vector<PieceImprimee> piecesImpression(const Projet& p);

}  // namespace prom
