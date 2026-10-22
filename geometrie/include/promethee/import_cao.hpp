// Prométhée : import des fichiers de CAO avec leur structure (produits, assemblages, placements), leurs noms et
// leurs couleurs (par produit et par face). STEP et IGES passent par les lecteurs XCAF d'OpenCascade ; BREP et STL,
// sans structure, donnent un seul produit. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <gp_Trsf.hxx>

#include "promethee/modele.hpp"
#include "promethee/piece.hpp"

namespace prom {

// Produit (pièce) du fichier : géométrie sans placement, partagée par toutes ses instances
struct ProduitImporte {
  std::string nom;
  TopoDS_Shape forme;
  std::string couleur;                         // « #RRGGBB », vide si le fichier n'en donne pas
  std::map<int, std::string> couleursFaces;    // rang de la face (ordre de TopExp_Explorer) → « #RRGGBB »
  std::vector<std::string> calques;            // calques (IGES surtout)
  int instances = 0;                           // nombre d'occurrences dans l'arbre
};

// Nœud de l'arbre : une instance de produit (feuille) ou un assemblage, placé dans son parent
struct NoeudImporte {
  std::string nom;
  int produit = -1;                    // ≥ 0 : feuille
  gp_Trsf placement;                   // dans le repère du parent
  std::vector<NoeudImporte> enfants;   // assemblage
  bool assemblage() const { return produit < 0; }
};

struct ImportCAO {
  std::string format;                  // « STEP », « IGES », « BREP », « STL »
  std::vector<ProduitImporte> produits;
  NoeudImporte racine;                 // toujours un assemblage, nommé d'après le fichier ou son produit racine
  std::vector<std::string> avertissements;
  int instances() const;               // feuilles de l'arbre
  int niveaux() const;                 // profondeur (1 : produits posés directement dans la racine)
  // Vrai si l'arbre a plus d'une instance : à importer comme assemblage plutôt qu'en une seule pièce
  bool estAssemblage() const { return instances() > 1; }
  // Une seule géométrie : toutes les instances placées dans un composé. Les couleurs par face sont reportées dans
  // l'ordre des faces du composé ; une instance d'une autre couleur que la première garde la sienne face par face.
  ProduitImporte aplati(const std::string& nom = {}) const;
};

// Lit STEP, IGES, BREP ou STL. Lève std::runtime_error (« Fichier STEP illisible. »…) si rien n'est lisible.
ImportCAO lireCAOStructure(const std::string& chemin);

// Pièce Prométhée d'un produit importé : un corps importé (géométrie au dépôt), le nom, la couleur et les couleurs
// de faces du fichier (« id:import.k »). « source » : nom du fichier, gardé par la fonction.
Piece pieceDepuisProduit(const ProduitImporte& produit, const std::string& source);
// Ajoute au projet une pièce par produit et un assemblage par nœud de l'arbre (sa racine comprise), composants fixes
// placés comme dans le fichier ; un produit répété est une seule pièce, instanciée plusieurs fois. Rend l'indice de
// l'assemblage racine.
int ajouterImportAuProjet(Projet& projet, const ImportCAO& import, const std::string& source);

}  // namespace prom
