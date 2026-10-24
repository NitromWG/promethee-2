// Prométhée : modèle des signaux (lot L8 de la 0.34 ; 8.26 ; ROU-39 à ROU-42). Un signal se suit de broche à broche à
// travers les composants série (résistances, condensateurs de liaison, ferrites), avec les longueurs internes des
// boîtiers (tableau par broche, ou CSV du fabricant) ; l'ordre de raccordement d'un réseau s'impose (chaîne,
// fly-by, étoile, T) et le chevelu le suit ; des règles par tronçon (From-To) bornent la longueur de broche à broche,
// mesurée sur le cuivre. Des gabarits d'interface en données (USB, HDMI, PCIe, MIPI, Ethernet, DDR3, DDR4) créent
// d'un geste signaux, paires, classes, groupes d'accord et règles, dont le récapitulatif sort en XLSX.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// Signaux trouvés à travers les composants série (deux broches, préfixe R, C, L ou FB) : chaque signal réunit les
// réseaux qu'ils relient, dans l'ordre de la chaîne. Les rails (alimentations, réseaux à trois passifs ou plus) ne
// sont jamais traversés.
std::vector<Signal> signauxSerie(const ConceptionElec& c);
// Ajoute les signaux trouvés à ceux de la carte (un signal qui couvre déjà les mêmes réseaux est gardé) ; rend le
// nombre ajouté
int creerSignauxSerie(ConceptionElec& c);

// Longueurs internes des boîtiers (« pin package length ») : extensions["longueursBoitier"][repère][broche] en mm
double longueurBoitier(const ConceptionElec& c, const std::string& reference, const std::string& numero);  // nm
// CSV du fabricant : « broche ; longueur » (mm, ou mil si l'en-tête le dit) ; rend le nombre de broches lues
int importerLongueursBoitier(ConceptionElec& c, const std::string& reference, const std::string& csv);

// Longueur d'un signal (nm) : ses réseaux sur le cuivre, plus les longueurs internes des boîtiers à ses extrémités
double longueurSignal(const ConceptionElec& c, const Signal& s);
double retardSignal(const ConceptionElec& c, const Signal& s);  // ps
// Longueur d'un tronçon de broche à broche (« U1.7 », « R3.1 ») : plus court chemin dans le cuivre de leur réseau
// (pistes et vias), longueurs internes des boîtiers comprises ; nullopt si le cuivre ne les relie pas
std::optional<double> longueurTroncon(const ConceptionElec& c, const std::string& de, const std::string& vers);
// Contrôle des signaux : tronçons hors de leurs bornes (DRC-86), ordre de raccordement non suivi par le cuivre (DRC-87)
std::vector<Violation> verifierSignaux(const ConceptionElec& c);

// ---------- Gabarits d'interface ----------
struct GabaritInterface {
  std::string nom, description;
  nlohmann::json donnees;  // signaux, paires, classes, accords, contraintes ; « {P} » : préfixe, « {i} » : rang
};
// Gabarits intégrés (USB 2.0, USB 3.x, HDMI, PCIe, MIPI CSI-2, Ethernet 100BASE-TX, DDR3, DDR4)
const std::vector<GabaritInterface>& gabaritsInterface();
std::optional<GabaritInterface> lireGabaritInterface(const std::string& json, std::string* erreur = nullptr);
struct BilanGabarit {
  int signaux = 0, paires = 0, classes = 0, accords = 0, contraintes = 0;
  std::vector<std::string> journal;
};
// Applique un gabarit, les noms de réseaux formés avec « prefixe » ; un réseau absent de la conception est signalé
// au journal, la règle est posée quand même
BilanGabarit appliquerGabarit(ConceptionElec& c, const GabaritInterface& g, const std::string& prefixe);

// Récapitulatif des signaux, paires et groupes en classeur XLSX (une feuille)
std::string xlsxSignaux(const ConceptionElec& c);

}  // namespace prom::ecad
