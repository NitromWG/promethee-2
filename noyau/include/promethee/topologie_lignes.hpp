// Prométhée : schéma de lignes et topologie des réseaux routés (lot L18 de la 0.34 ; 18.4 ; SIP-01, SIP-09).
//
// Un schéma de lignes décrit un réseau rapide en éléments (pilote IBIS, tronçon d'impédance et de longueur donnés,
// via et son moignon, terminaison, récepteur), simulé par ngspice en lignes sans pertes avec balayage d'une valeur ;
// les valeurs retenues (ordre, longueurs, topologie) passent d'un clic aux règles, provenance notée. Un réseau routé
// s'extrait au même format (pastilles, pistes fusionnées de jonction en jonction, vias et moignons d'après
// l'empilage) et se compare à la topologie prévue : ordre de raccordement, moignons de chaîne (branche d'un récepteur
// sur la ligne principale), de via et de pastille, chacun borné.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "promethee/contraintes.hpp"
#include "promethee/electronique.hpp"

namespace prom::ecad {

enum class GenreElementLigne : std::uint8_t { Pilote, Troncon, Via, Terminaison, Recepteur };
const char* codeElementLigne(GenreElementLigne g);  // « pilote », « troncon », « via », « terminaison », « recepteur »
struct ElementLigne {
  GenreElementLigne genre = GenreElementLigne::Troncon;
  std::string id;          // « U1.7 », « T3 », « V1 »
  std::string a, b;        // nœuds du schéma de lignes (b vide pour une via, un pilote, un récepteur)
  double impedance = 50;   // Ω : tronçon, moignon de via
  double longueur = 0;     // mm : tronçon ; moignon d'une via
  double vitesse = 150;    // mm/ns : propagation sur le tronçon
  double resistance = 0;   // Ω : résistance de sortie du pilote, terminaison
  double tension = 0;      // V : terminaison (VTT)
  double capacite = 0;     // F : entrée du récepteur
  double distance = 0;     // mm : extraite, distance au pilote le long du cuivre
  std::string ibis, modele;
  Pt position;
  friend bool operator==(const ElementLigne&, const ElementLigne&) = default;
};
struct SchemaLignes {
  std::string reseau;
  TopologieSignal topologie = TopologieSignal::Libre;
  std::vector<std::string> ordre;  // pilote puis récepteurs, dans l'ordre de raccordement
  std::vector<ElementLigne> elements;
  double moignonMax = 1.0;          // mm : branche d'un récepteur sur la ligne principale
  double moignonViaMax = 0.5;       // mm
  double moignonPastilleMax = 0.2;  // mm : piste qui dépasse le centre de sa pastille
  std::string provenance;
  friend bool operator==(const SchemaLignes&, const SchemaLignes&) = default;
};
// Schémas de lignes de la carte : c.extensions["lignes"]
std::vector<SchemaLignes> schemasLignes(const ConceptionElec& c);
void ecrireSchemasLignes(ConceptionElec& c, const std::vector<SchemaLignes>& v);

// Réseau routé au même format. « prevue » donne le pilote et les récepteurs (son ordre) ; sans elle, le pilote est la
// première broche. Les récepteurs y sont rangés dans l'ordre où la ligne principale les rejoint.
SchemaLignes extraireTopologie(const ConceptionElec& c, const std::string& reseau, const SchemaLignes* prevue = nullptr);
struct EcartTopologie {
  std::string genre;  // « ordre », « moignon de chaîne », « moignon de via », « moignon de pastille », « broche absente »
  std::string element, message;
  double valeur = 0, limite = 0;  // mm
  Pt position;
};
std::vector<EcartTopologie> comparerTopologie(const SchemaLignes& prevue, const SchemaLignes& extraite);
// La topologie extraite superposée à la prévue (SVG) : la ligne principale à l'échelle de sa longueur, chaque
// récepteur sur sa branche, chaque via et son moignon ; la prévue en tireté
std::string svgTopologies(const SchemaLignes& prevue, const SchemaLignes& extraite);
// Netlist ngspice (lignes sans pertes « T ») ; « balayage » remplace la longueur du moignon de chaque via (mm)
std::string netlistLignes(const SchemaLignes& s, double fronts = 0.3e-9, double duree = 20e-9, double balayageMoignon = -1);
// Valeurs retenues versées aux règles : signal du réseau (topologie, ordre, tronçons bornés à leur longueur prévue),
// sa provenance notée dans c.extensions["provenanceRegles"] ; rend le nombre de tronçons bornés
int appliquerAuxRegles(ConceptionElec& c, const SchemaLignes& s);
// Toute la carte : chaque schéma de lignes comparé à son réseau routé (DRC-91 moignon de via, DRC-92 moignon de
// chaîne, DRC-93 ordre de raccordement, DRC-94 moignon de pastille)
std::vector<Violation> verifierMoignons(const ConceptionElec& c);

}  // namespace prom::ecad
