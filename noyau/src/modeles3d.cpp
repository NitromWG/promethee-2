// Prométhée : modèles 3D exacts (lot L6). Licence GPL-3.0-only.
#include "promethee/modeles3d.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "promethee/hachage.hpp"
#include "promethee/hauteurs.hpp"

namespace prom::ecad {

Modele3D calerModele(const DefEmpreinte& d, Modele3D m) {
  const Boite emprise = d.encombrement.vide() ? empriseCorps(d) : d.encombrement;
  const double W = mm(emprise.largeur()), D = mm(emprise.hauteur());
  const double w = m.x1 - m.x0, p = m.y1 - m.y0;
  const double droit = std::abs(w - W) + std::abs(p - D), tourne = std::abs(w - D) + std::abs(p - W);
  m.quarts = tourne + 1e-9 < droit ? 1 : 0;
  return m;
}

std::string cleModele(const std::string& nom) {
  std::string n = nom;
  if (const auto barre = n.find_last_of("/\\"); barre != std::string::npos) n = n.substr(barre + 1);
  if (const auto point = n.find_last_of('.'); point != std::string::npos) n = n.substr(0, point);
  std::string r;
  for (char ch : n)
    if (std::isalnum(static_cast<unsigned char>(ch))) r += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  for (const char* suffixe : {"3d", "model", "modele"})
    if (r.size() > std::string(suffixe).size() + 2 && r.compare(r.size() - std::string(suffixe).size(), std::string::npos, suffixe) == 0) r.resize(r.size() - std::string(suffixe).size());
  return r;
}

std::map<std::string, std::string> rapprocherModeles(const ConceptionElec& c, const std::vector<std::string>& fichiers) {
  std::map<std::string, std::string> parCle;
  for (const auto& f : fichiers) parCle.emplace(cleModele(f), f);
  std::map<std::string, std::string> r;
  for (const auto& k : c.composants) {
    for (const std::string& candidat : {k.mpn, k.article, k.empreinte, k.valeur}) {
      if (candidat.empty()) continue;
      std::string id = candidat;
      if (const auto deux = id.find(':'); deux != std::string::npos) id = id.substr(deux + 1);  // « kicad:… », « perso:… »
      if (const auto it = parCle.find(cleModele(id)); it != parCle.end()) {
        r[k.uuid] = it->second;
        break;
      }
    }
  }
  return r;
}

std::string associerModele(ConceptionElec& c, const std::string& composant, Modele3D m) {
  exigerModifiable(c);
  ComposantElec* k = c.composant(composant);
  if (!k) return {};
  const DefEmpreinte* d = c.empreinte(k->empreinte);
  if (d) m = calerModele(*d, m);
  // Un même fichier n'est rangé qu'une fois (sa forme sert à tous les composants qui l'emploient)
  const std::string id = "m:" + sha256Hex(m.brep.empty() ? m.nom : m.brep).substr(0, 16) + (m.quarts ? "q" + std::to_string(m.quarts) : std::string());
  if (!c.modeles3D.count(id)) c.modeles3D[id] = m;
  k->modele3D = id;
  return id;
}

}  // namespace prom::ecad
