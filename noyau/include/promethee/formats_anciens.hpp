// Prométhée : formats anciens de CAO électronique (lot L10 de la 0.34 ; 10.18 ; ECH-23, ECH-24). Lecteurs écrits
// d'après les formats que lisent les importeurs de KiCad et de pcb-rnd (GPL), servant de référence, sans en reprendre
// le code, et éprouvés sur des fichiers réels :
// - Protel Autotrax (.PCB, DOS) : carte sans netlist, réseaux tirés du cuivre connecté ;
// - gEDA PCB et Lepton (.pcb), empreintes .fp ; schémas gschem et lepton-schematic (.sch, symboles .sym) ;
// - Sprint-Layout d'ABACOM (.lay6, .lay, versions 3 à 6) et ses macros (.lmk) : carte sans netlist, réseaux tirés du
//   cuivre et du chevelu gardé par le fichier, plans de masse repris en zones.
// Ce qui ne passe pas est inscrit au rapport. Licence GPL-3.0-only.
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/formats_cao.hpp"

namespace prom::ecad {

// ---------- Protel Autotrax ----------
bool estAutotrax(const std::string& texte);  // « PCB FILE 4 » ou « PCB FILE 5 »
ConceptionElec conceptionDepuisAutotrax(const std::string& texte, const std::string& nom, RapportLectureCao* rapport = nullptr);

// ---------- gEDA PCB, Lepton ----------
bool estGedaPcb(const std::string& texte);  // « PCB[ », « FileVersion[ » ou un « Element[ » en tête
ConceptionElec conceptionDepuisGedaPcb(const std::string& texte, const std::string& nom, RapportLectureCao* rapport = nullptr);
// Empreinte d'un fichier .fp (un « Element[…] ») ; « nom » : nom du fichier sans extension
std::optional<DefEmpreinte> empreinteDepuisGedaFp(const std::string& texte, const std::string& nom);

// ---------- gschem, lepton-schematic ----------
using LecteurSymboles = std::function<std::optional<std::string>(const std::string&)>;  // nom du .sym → texte
bool estSchemaGschem(const std::string& texte);  // « v 2… » puis des composants (C) ou des fils (N)
// Netlist du schéma (broches au bout des fils, attributs net= des symboles, netname= des fils, portes « slot= »,
// feuilles filles « source= » préfixées comme le fait gnetlist), posée en schéma ; « lire » trouve aussi les feuilles
ConceptionElec conceptionDepuisGschem(const std::string& texte, const std::string& nom, const LecteurSymboles& lire, RapportLectureCao* rapport = nullptr);
// Symbole de la bibliothèque standard de gEDA et Lepton (« resistor-1.sym »…), incorporé, réduit à ses broches et
// attributs : il sert quand le projet ne le fournit pas
std::optional<std::string> symboleStandardGeda(const std::string& nom);
// Symbole .sym en symbole de Prométhée (bibliothèque) : broches (numéro, étiquette, type, côté), une unité par porte
// (« slotdef= »), broches d'alimentation cachées (« net= ») communes ; refusé (raison dite) pour un symbole
// d'alimentation, graphique ou sans broche
std::optional<DefSymbole> symboleDepuisGedaSym(const std::string& texte, const std::string& nom, std::string* raison = nullptr);
// Plusieurs pages d'un même schéma (nom du fichier, texte), reliées par les noms de réseaux
ConceptionElec conceptionDepuisGschem(const std::vector<std::pair<std::string, std::string>>& pages, const std::string& nom, const LecteurSymboles& lire, RapportLectureCao* rapport = nullptr);

// ---------- Sprint-Layout ----------
bool estSprintLayout(const std::string& octets);  // version (0 à 6) puis 33 AA FF
// « carte » : rang de la carte à lire dans un fichier qui en porte plusieurs (onglets de Sprint-Layout)
ConceptionElec conceptionDepuisSprintLayout(const std::string& octets, const std::string& nom, RapportLectureCao* rapport = nullptr, std::size_t carte = 0);
std::size_t nombreCartesSprintLayout(const std::string& octets);  // 0 : fichier illisible
// Macro .lmk (bibliothèque de Sprint-Layout) en empreinte ; « nom » : nom du fichier sans extension
std::optional<DefEmpreinte> empreinteDepuisMacroSprint(const std::string& octets, const std::string& nom, std::string* alerte = nullptr);

}  // namespace prom::ecad
