// Prométhée : lecture tolérante bornée (lot L9). Licence GPL-3.0-only.
#include "promethee/robustesse.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>

namespace prom::ecad {

std::vector<std::string> reparerDoublons(ConceptionElec& c) {
  std::vector<std::string> notes;
  // Identifiants simples : le premier garde le sien, les suivants en reçoivent un neuf
  std::map<std::string, int> renouveles;
  auto unique = [&renouveles](auto& objets, const char* quoi) {
    std::set<std::string> vus;
    for (auto& o : objets) {
      if (o.uuid.empty() || vus.insert(o.uuid).second) continue;
      o.uuid = nouvelUuid();
      vus.insert(o.uuid);
      ++renouveles[quoi];
    }
  };
  unique(c.feuilles, "feuille");
  // Symboles : uniques sur toutes les feuilles
  {
    std::set<std::string> vus;
    for (auto& f : c.feuilles)
      for (auto& s : f.symboles)
        if (!s.uuid.empty() && !vus.insert(s.uuid).second) {
          s.uuid = nouvelUuid();
          vus.insert(s.uuid);
          ++renouveles["symbole"];
        }
  }
  for (auto& f : c.feuilles) {
    unique(f.fils, "fil");
    unique(f.jonctions, "jonction");
    unique(f.nonConnexions, "non-connexion");
    unique(f.etiquettes, "étiquette");
    unique(f.textes, "texte du schéma");
  }
  unique(c.circuit.pistes, "piste");
  unique(c.circuit.vias, "via");
  unique(c.circuit.zones, "zone");
  unique(c.circuit.textes, "texte du circuit");
  unique(c.circuit.dessins, "dessin du circuit");
  // Composants en double : le premier garde l'identifiant ; chaque copie en reçoit un neuf et reprend un symbole
  // en trop (même unité déjà posée) et une empreinte en trop de l'original
  std::set<std::string> vus;
  for (std::size_t i = 0; i < c.composants.size(); ++i) {
    ComposantElec& x = c.composants[i];
    if (x.uuid.empty() || vus.insert(x.uuid).second) continue;
    const std::string ancien = x.uuid, neuf = nouvelUuid();
    x.uuid = neuf;
    vus.insert(neuf);
    // Symboles : la deuxième pose d'une même unité de l'original passe à la copie
    std::set<int> unites;
    bool repris = false;
    for (auto& f : c.feuilles)
      for (auto& s : f.symboles) {
        if (s.composant != ancien) continue;
        if (!unites.insert(s.unite).second && !repris) {
          s.composant = neuf;
          repris = true;
        }
      }
    // Empreintes : la deuxième de l'original passe à la copie
    int n = 0;
    for (auto& e : c.circuit.empreintes)
      if (e.composant == ancien && ++n == 2) e.composant = neuf;
    notes.push_back("composant " + (x.reference.empty() ? std::string("sans repère") : x.reference) + " : identifiant en double, renouvelé" +
                    (repris ? " ; il reprend son symbole" : " ; aucun symbole ne le pose") + (n >= 2 ? " et son empreinte" : ""));
  }
  // Empreintes posées deux fois pour le même composant (ou pour un composant disparu) : la première reste
  {
    std::set<std::string> poses;
    auto& v = c.circuit.empreintes;
    const auto fin = std::remove_if(v.begin(), v.end(), [&poses](const EmpreintePlacee& e) { return !e.composant.empty() && !poses.insert(e.composant).second; });
    const auto retirees = std::distance(fin, v.end());
    v.erase(fin, v.end());
    if (retirees) {
      notes.push_back(std::to_string(retirees) + " empreinte(s) posée(s) en double pour un même composant, retirée(s) ; la première est gardée");
    }
  }
  for (const auto& [quoi, n] : renouveles) notes.push_back(std::to_string(n) + " " + quoi + "(s) à l'identifiant en double : identifiant renouvelé");
  return notes;
}

}  // namespace prom::ecad
