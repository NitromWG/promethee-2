// Prométhée : échange incrémental avec la CAO mécanique au format IDX (ProSTEP iViP EDMD 3.0 et 4.0 ; lot L10 de la
// 0.34 ; 10.10, 10.24 ; MEC-09). La ligne de base décrit la carte telle que la mécanique la voit : contour et
// découpes extrudés à l'épaisseur, trous, composants (boîtier, position, angle, face, hauteur, verrou), zones
// interdites, pièces montées, et en option le cuivre, en décalques (contours plats sur leur couche) ou en volumes
// (extrudés à l'épaisseur du cuivre). Une proposition de la mécanique (SendChanges : composant déplacé, tourné ou
// retourné, zone interdite ajoutée ou retirée, pièce posée, verrou mis, contour changé) se lit changement par
// changement ; chacun est accepté ou refusé, les acceptés s'appliquent en une commande, une réponse IDX part vers la
// mécanique et l'échange s'inscrit à l'historique de la conception. Le lecteur ignore les préfixes d'espaces de noms :
// il lit les fichiers des modeleurs qui l'écrivent chacun à sa façon. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

struct OptionsIdx {
  enum class Cuivre { Aucun, Decalques, Volumes } cuivre = Cuivre::Aucun;
  std::string auteur, societe;
  int version = 3;  // 3 ou 4 : espaces de noms d'EDMD 3.0 ou 4.0
};
std::string idxLigneDeBase(const ConceptionElec& c, const OptionsIdx& o = {});

struct ChangementIdx {
  enum class Genre { Deplacement, ZoneInterdite, ZoneRetiree, PieceMecanique, PieceRetiree, Verrou, Contour, Autre } genre = Genre::Autre;
  std::string identifiant;   // identifiant de l'objet dans le fichier
  std::string reference;     // repère du composant, nom de la zone ou de la pièce
  Pt position;               // nm, repère de la carte (y vers le bas)
  double rotation = 0;
  bool dessous = false, verrouille = false;
  Polygone contour;          // zone interdite, contour de carte, empreinte d'une pièce
  Nm hauteurMax = 0, hauteur = 0;
  std::string nature;        // zone : « placement », « routage », « vias », « tout » ; pièce : son nom
  std::string description;   // une ligne lisible : « J1 déplacé de 2,00 mm »
};
struct PropositionIdx {
  std::string identifiant, logiciel, auteur, date;
  std::vector<ChangementIdx> changements;
  std::vector<std::string> alertes;  // ce qui n'a pas été compris
};
// « c » sert à décrire les changements (écart avec la carte actuelle) ; rien n'est modifié
PropositionIdx lirePropositionIdx(const std::string& xml, const ConceptionElec& c);
// Applique les changements acceptés (même ordre que p.changements) ; rend leur nombre. Inscrit l'échange à l'historique.
int appliquerPropositionIdx(ConceptionElec& c, const PropositionIdx& p, const std::vector<bool>& acceptes, const std::string& fichier = {});
// Réponse à la mécanique : chaque changement accepté ou refusé
std::string idxReponse(const PropositionIdx& p, const std::vector<bool>& acceptes);

struct EchangeIdx {
  std::string date, sens, fichier, logiciel, resume;  // sens : « envoyé » ou « reçu »
  int acceptes = 0, refuses = 0;
};
std::vector<EchangeIdx> historiqueIdx(const ConceptionElec& c);
void inscrireEchangeIdx(ConceptionElec& c, const EchangeIdx& e);

}  // namespace prom::ecad
