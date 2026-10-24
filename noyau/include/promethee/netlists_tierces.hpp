// Prométhée : netlists des autres logiciels (lot L10 de la 0.34 ; 10.20 ; ECH-08). Un lecteur et un écrivain par
// format, sur la table réseau-broche de 10.7 (NetlistLue) : OrCAD PCB II, PADS, Allegro (Telesis), CADSTAR, comme les
// écrivent les exporteurs de netlist de KiCad ; EDIF 2 0 0, SPICE et LTspice, gEDA (gschem, format PCB), TinyCAD,
// comme les lisent les importeurs de pcb-rnd. Un exporteur maison se décrit par un gabarit de texte. La netlist d'un
// schéma tiers met la carte à jour par mettreAJourDepuisNetlist, avec son rapport CSV. Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "promethee/echanges_electronique.hpp"

namespace prom::ecad {

enum class FormatNetlist { OrcadPcb2, Pads, Allegro, Cadstar, Edif, Spice, Geda, TinyCad };
struct InfoFormatNetlist {
  FormatNetlist format;
  const char* nom;
  const char* extension;
};
const std::vector<InfoFormatNetlist>& formatsNetlist();
std::optional<FormatNetlist> reconnaitreNetlist(const std::string& texte);
NetlistLue lireNetlistTierce(const std::string& texte, FormatNetlist f);
std::string ecrireNetlist(const NetlistLue& n, FormatNetlist f, const std::string& titre = "Prométhée");
// Gabarit d'exporteur maison : les lignes avant la première commande sont l'en-tête, après la dernière la fin ;
// « @composant <texte> » se répète par composant ({ref} {valeur} {empreinte}), « @reseau <texte> » par réseau
// ({nom} {nombre}), suivi de « @broche <texte> » par broche du réseau ({ref} {broche} {nom}) ; « {date} » et
// « {titre} » partout
std::string netlistGabarit(const NetlistLue& n, const std::string& gabarit, const std::string& titre = "Prométhée");

}  // namespace prom::ecad
