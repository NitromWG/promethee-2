// Prométhée : routeur interactif (lot L5, tâches 5.7, 5.9, 5.21 ; ROU-01, ROU-02, ROU-11, C2).
//
// Contourner : un chemin à 45° de a à b sur une couche, qui garde partout l'isolement des autres réseaux (pastilles,
// pistes, vias) et la marge du bord de la carte. Recherche A* sur une grille alignée sur le départ, dont les cases
// gênées sont marquées par les obstacles gonflés de l'isolement et d'une demi-diagonale de case (un pas de grille
// libre l'est sur toute sa longueur), puis chemin simplifié en segments à 45° vérifiés exactement.
// Pousser : le tracé direct ; les pistes des autres réseaux qu'il gêne s'écartent, retracées autour de lui entre
// leurs mêmes bouts, sans violer de règle ; rien ne change si l'une ne trouve pas de place.
// Router une liaison du chevelu d'office : contournement sur une couche commune aux deux pastilles ; ce qui ne passe
// pas reste au chevelu, avec la raison.
// Licence GPL-3.0-only.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct OptionsTrace {
  std::string reseau;
  Couche couche = Couche::CuivreDessus;
  Nm largeur = 0;     // 0 : la largeur du réseau dans les règles
  Nm pas = 0;         // pas de la grille de recherche ; 0 : d'après la largeur et l'isolement
  Nm etendue = 12 * NM_PAR_MM;  // marge de recherche autour des deux bouts
  std::set<std::string> ignorees;     // pistes à ne pas compter comme obstacles (celles qu'on retrace)
  std::vector<Piste> obstaclesEnPlus;  // pistes à compter en plus (le tracé qui pousse)
  const std::vector<PastillePlacee>* pastilles = nullptr;  // pastilles déjà calculées (l'interface), sinon recalculées
};
struct ResultatTrace {
  bool ok = false;
  std::vector<Pt> points;  // a, coins…, b
  std::string raison;
  double longueur() const;
};
ResultatTrace contourner(const ConceptionElec& c, Pt a, Pt b, const OptionsTrace& o);
// Segment libre à l'isolement sur la couche (vérification exacte, celle du routeur)
bool segmentLibre(const ConceptionElec& c, Pt a, Pt b, const OptionsTrace& o, std::string* obstacle = nullptr);
std::vector<Piste> pistesDuTrace(const std::vector<Pt>& points, const std::string& reseau, Couche couche, Nm largeur);

struct ResultatPoussee {
  bool ok = false;
  std::vector<Pt> points;               // tracé posé
  std::vector<std::string> deplacees;   // réseaux des pistes écartées
  std::string raison;
};
// Pose la piste de a à b en poussant les pistes gênantes ; la conception ne change que si tout passe
ResultatPoussee pousser(ConceptionElec& c, Pt a, Pt b, const OptionsTrace& o);

// Route une liaison du chevelu (menu contextuel, 5.21) : vide si elle est posée, sinon la raison
std::string routerLiaison(ConceptionElec& c, const Liaison& l);

}  // namespace prom::ecad
