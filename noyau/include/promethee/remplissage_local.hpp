// Prométhée : remplissage d'une zone recalculé dans une fenêtre (lot L9, tâche 9.9 ; CUI-02, ERG-09). Pendant le
// glissement d'une piste près d'un plan, la zone suit à chaque image : les obstacles (isolements des autres réseaux,
// anneaux et rayons thermiques, perçages, zones prioritaires) sont préparés et indexés une fois au début du geste,
// sans les objets qui bougent ; chaque image ne regarde que la fenêtre touchée. Les îlots ne sont pas retirés ici :
// le remplissage complet, refait à la fin du geste, le fait.
// Licence GPL-3.0-only.
#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

class RemplissageLocal {
 public:
  // « mobiles » : UUID des pistes et vias qui bougent ; « remplis » : remplissages courants (zones prioritaires)
  RemplissageLocal(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis, const std::string& zone, const std::set<std::string>& mobiles);
  ~RemplissageLocal();
  RemplissageLocal(const RemplissageLocal&) = delete;
  RemplissageLocal& operator=(const RemplissageLocal&) = delete;
  bool valide() const;
  Couche couche() const;
  const std::string& zone() const;
  const std::string& reseau() const;
  // Cuivre de la zone dans la fenêtre (rogné à elle), les pistes et vias mobiles à leur place courante
  Polygones dans(const Boite& fenetre, const std::vector<Piste>& pistes, const std::vector<Via>& vias) const;
  // Marge à ajouter autour d'un objet mobile pour que sa fenêtre couvre tout ce qu'il change
  Nm portee() const;

 private:
  struct Donnees;
  std::unique_ptr<Donnees> d;
};

}  // namespace prom::ecad
