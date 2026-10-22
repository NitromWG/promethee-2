// Prométhée : parcours d'apprentissage de l'atelier électrique. Un parcours est une suite d'étapes : chacune dit
// ce qu'on apprend et pourquoi, où cliquer, renvoie à une fiche du glossaire, et se vérifie toute seule sur le
// projet (l'étape est faite quand le projet le montre). Les étapes sans vérification se lisent. Aucune dépendance
// à l'interface. Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "promethee/electrique.hpp"

namespace prom::elec {

struct EtapeParcours {
  std::string titre;      // « Pose le disjoncteur moteur »
  std::string pourquoi;   // ce qu'on apprend, en mots simples
  std::string comment;    // où cliquer, quoi régler
  std::string glossaire;  // clé d'une fiche du glossaire (vide : aucune)
  int mode = 0;           // onglet de l'atelier où se passe l'étape (identifiants des modes de l'atelier électrique)
  std::function<bool(const ProjetElectrique&)> faite;  // vide : étape de lecture, « Compris » passe à la suivante
};
struct Parcours {
  std::string id, nom, description;
  std::string genre;  // projet à créer pour le suivre : « schema », « distribution », « photovoltaique », « logement », « grafcet »
  std::vector<EtapeParcours> etapes;
};
const std::vector<Parcours>& parcoursElectriques();
const Parcours* parcoursElectrique(const std::string& id);

}  // namespace prom::elec
