// Prométhée : conception guidée par modules et circuits types (lot L13 de la 0.34 ; 13.14 ; AUT-17, STR-27). On
// choisit un gabarit de carte, un module de la galerie (son symbole, son empreinte, ses broches d'interface) et des
// interfaces (port USB, connecteur I2C, liaison série…) : les blocs sont posés et reliés selon le type d'interface,
// avec les circuits types que demandent les attributs des signaux et des broches (découplage, tirage de la remise à
// zéro, protection des lignes USB contre les décharges, régulateur depuis le bus USB), placés par 13.12, routés,
// vérifiés (ERC et DRC), puis modifiables comme toute carte. Les dessins de la galerie suivent les fiches des
// fabricants ; ils restent à vérifier avant une fabrication. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Module de la galerie : broches (numéro, nom), interfaces qu'il offre (« usb » : broches D- et D+, « i2c »…),
// alimentation et dessin du boîtier (rangées de pastilles à pas régulier sur trois côtés et pastille centrale)
struct ModuleGalerie {
  std::string id, nom, description, fabricant, mpn;
  std::vector<std::pair<std::string, std::string>> broches;
  std::string alimentation = "3V3", masse = "GND", activation = "EN";
  std::vector<std::pair<std::string, std::vector<std::string>>> interfaces;  // « usb » → {broche D-, broche D+}
  double largeur = 18, hauteur = 25.5, pas = 1.27, antenne = 6.5;  // mm ; antenne : bande sans cuivre en haut
  int gauche = 14, bas = 12, droite = 14;                          // pastilles par côté (sens inverse des aiguilles)
  double centrale = 5;                                             // côté de la pastille centrale (0 : aucune)
};
const std::vector<ModuleGalerie>& galerieModules();
const ModuleGalerie* moduleGalerie(const std::string& id);
DefSymbole symboleModule(const ModuleGalerie& m);
DefEmpreinte empreinteModule(const ModuleGalerie& m);

// Interfaces de la carte : « usb » (micro-B, protection, régulateur), « i2c » (connecteur 4 points et tirages),
// « serie » (connecteur 3 points), « led » (témoin sur une broche libre)
const std::vector<std::pair<std::string, std::string>>& interfacesGuidees();

struct DemandeGuidee {
  std::string nom = "Carte guidée";
  double largeur = 50, hauteur = 30;  // gabarit de carte (mm)
  std::string module = "esp32-s3-wroom-1";
  std::vector<std::string> interfaces{"usb"};
  bool placer = true, router = true;
};
struct ResultatGuide {
  ConceptionElec conception;
  std::vector<std::string> journal, refus;
  int erreursErc = 0, erreursDrc = 0;
  std::vector<std::string> violations;  // erreurs restantes, en clair
  bool reussi() const { return refus.empty() && erreursErc == 0 && erreursDrc == 0; }
};
ResultatGuide concevoirParModules(const DemandeGuidee& d);

// Circuits types proposés d'après les attributs des signaux et des broches (sans rien poser) : « C 100 nF sur 3V3 de
// U1 », « tirage 4,7 kΩ sur SDA », « protection ESD sur D+ et D- »
struct PropositionCircuitType {
  std::string regle, cible, description;
  std::vector<std::string> reseaux;
};
std::vector<PropositionCircuitType> circuitsTypes(const ConceptionElec& c);
// Pose d'un clic (sur la première feuille, sous ce qui y est déjà) : condensateur de découplage, tirages, protection
// des lignes USB ; rend les UUID des composants posés, vide avec « erreur » si la proposition n'est pas comprise
std::vector<std::string> poserCircuitType(ConceptionElec& c, const PropositionCircuitType& p, std::string* erreur = nullptr);

}  // namespace prom::ecad
