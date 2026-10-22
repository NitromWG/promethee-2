// Prométhée : outils communs aux fichiers de la CAO électrique (non publics). Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electrique.hpp"

namespace prom::elec::detail {

// Nombre à la française : « 2,5 », « 16 » ; « decimales » < 0 : le moins de chiffres possible (6 au plus)
std::string nombreFr(double v, int decimales = -1);
// Premier nombre d'un texte (« 2,5 mm² » → 2,5 ; « 16 A » → 16), 0 sinon
double lireNombre(const std::string& s);
std::string majuscules(std::string s);
std::string champ(const ecad::ComposantElec& c, const std::string& nom);

// Définition d'un symbole : copie du projet, sinon bibliothèque électrique, sinon bibliothèque électronique
const ecad::DefSymbole* definitionDe(const ProjetElectrique& p, const std::string& id);
const EntreeCatalogue* catalogueDe(const ecad::ComposantElec& c);
// Bornier (ou connecteur) : appareil dont chaque pose est une borne numérotée
bool estBornier(const ecad::ComposantElec& c);
// Lettre de repère d'un appareil : celle de son repère actuel (posée à la main, importée), sinon celle de son
// symbole ; « normaliser » : celle de la norme choisie pour les symboles de la bibliothèque
std::string prefixeComposant(const ProjetElectrique& p, const ecad::ComposantElec& c, bool normaliser);

// Où se trouve chaque broche (appareil, numéro réel) : première pose dans l'ordre de lecture
struct LieuBroche {
  std::size_t feuille = 0;
  ecad::Pt p;
  std::string symbole;  // UUID de la pose
};
using LieuxBroches = std::map<std::pair<std::string, std::string>, LieuBroche>;
LieuxBroches lieuxBroches(const ProjetElectrique& p);
// « -KM1:13 »
std::string repereBroche(const ProjetElectrique& p, const std::string& composant, const std::string& broche);
// Nom de réseau donné automatiquement (« N-KM1-13 ») : fil sans numéro
bool nomAutomatique(const std::string& nom);

// Propriétés d'un conducteur avec une connectivité déjà calculée
ProprietesConducteur proprietes(const ProjetElectrique& p, const ecad::Connectivite& k, const std::string& fil);
// Calibre d'un appareil de protection (champ « calibre », sinon catalogue), 0 s'il n'en a pas
double calibreDe(const ecad::ComposantElec& c);
// Puissance (kW) d'un récepteur : champ « puissance », sinon catalogue
double puissanceDe(const ecad::ComposantElec& c);

// Retire les folios tirés d'un calcul (InfosFolio::origine), avec leurs appareils, et rend la place du premier
// (sinon la fin) pour ceux qui les remplacent. Le folio vierge d'un projet né pour ce calcul part aussi. Un projet
// garde toujours un folio : un folio vierge provisoire tient la place quand il n'y a que ceux-là.
std::size_t retirerFoliosCalcules(ProjetElectrique& p, const std::string& origine);

// CSV au point-virgule, UTF-8 avec BOM, fins de ligne CRLF (tableurs français)
std::string documentCsv(const std::vector<std::string>& entete, const std::vector<std::vector<std::string>>& lignes);

}  // namespace prom::elec::detail
