// Prométhée : réutilisation (lot L7, tâche 7.16). Voir reutilisation.hpp.
// Licence GPL-3.0-only.
#include "promethee/reutilisation.hpp"

#include <algorithm>
#include <functional>
#include <queue>

#include "promethee/circuit_edition.hpp"

namespace prom::ecad {

Extrait extraitSchema(const ConceptionElec& c, std::size_t feuille, const std::set<std::string>& selection, const std::string& nom) {
  Extrait e;
  e.nom = nom;
  e.genre = "schema";
  e.contenu = presseVersJson(copierSchema(c, feuille, selection));
  return e;
}

Extrait extraitCircuit(const ConceptionElec& c, const std::set<std::string>& selection, const std::string& nom) {
  Extrait e;
  e.nom = nom;
  e.genre = "circuit";
  e.contenu = presseCircuitVersJson(copierCircuit(c, selection));
  return e;
}

std::string texteExtrait(const Extrait& e) {
  nlohmann::ordered_json j;
  j["format"] = "promethee-extrait";
  j["version"] = 1;
  j["nom"] = e.nom;
  j["genre"] = e.genre;
  j["description"] = e.description;
  j["etat"] = e.etat;
  j["revision"] = e.revision;
  j["contenu"] = e.contenu;
  return j.dump(1);
}

std::optional<Extrait> lireExtrait(const std::string& texte, std::string* erreur) {
  try {
    const auto j = nlohmann::ordered_json::parse(texte);
    if (j.value("format", std::string()) != "promethee-extrait") throw std::runtime_error("ce n'est pas un extrait Prométhée");
    Extrait e;
    e.nom = j.value("nom", std::string());
    e.genre = j.value("genre", std::string("schema"));
    e.description = j.value("description", std::string());
    e.etat = j.value("etat", std::string("brouillon"));
    e.revision = j.value("revision", 1);
    if (j.contains("contenu")) e.contenu = j["contenu"];
    return e;
  } catch (const std::exception& x) {
    if (erreur) *erreur = x.what();
    return std::nullopt;
  }
}

std::set<std::string> poserExtrait(ConceptionElec& c, const Extrait& e, std::size_t feuille, Pt ou) {
  if (e.genre == "circuit") return collerCircuit(c, presseCircuitDepuisJson(e.contenu), ou);
  return collerSchema(c, feuille, presseDepuisJson(e.contenu), ou);
}

std::vector<std::string> retouchesLocales(const Feuille& posee, const Feuille& fichier) {
  std::vector<std::string> r;
  auto compter = [&](const char* quoi, std::size_t a, std::size_t b) {
    if (a != b) r.push_back(std::string(quoi) + " : " + std::to_string(b) + " dans le fichier, " + std::to_string(a) + " ici");
  };
  compter("symboles", posee.symboles.size(), fichier.symboles.size());
  compter("fils", posee.fils.size(), fichier.fils.size());
  compter("étiquettes", posee.etiquettes.size(), fichier.etiquettes.size());
  compter("textes", posee.textes.size(), fichier.textes.size());
  // Symboles déplacés ou tournés (même UUID)
  std::map<std::string, const SymbolePlace*> f;
  for (const auto& s : fichier.symboles) f[s.uuid] = &s;
  for (const auto& s : posee.symboles)
    if (const auto it = f.find(s.uuid); it != f.end() && (it->second->position != s.position || it->second->rotation != s.rotation))
      r.push_back("symbole " + s.uuid.substr(0, 8) + " déplacé ou tourné");
  return r;
}

std::vector<CasEmploi> casEmploi(const std::vector<ConceptionElec>& conceptions, const std::string& fichier) {
  std::vector<CasEmploi> r;
  for (const auto& c : conceptions)
    for (const auto& f : c.feuilles)
      if (f.fichier == fichier) r.push_back({c.nom, f.nom, f.revisionFichier});
  return r;
}

namespace {
// Graphe composants-réseaux : pour chaque composant, ses (numéro de broche, réseau) ; pour chaque réseau, ses
// (composant, numéro)
struct GrapheCanaux {
  std::vector<std::string> composants;
  std::map<std::string, std::size_t> indice;
  std::vector<std::vector<std::pair<std::string, int>>> brochesDe;  // composant → (numéro, réseau)
  std::vector<std::vector<std::pair<std::size_t, std::string>>> membres;  // réseau → (composant, numéro)
};
GrapheCanaux construire(const ConceptionElec& c, const Connectivite& k) {
  GrapheCanaux g;
  for (const auto& x : c.composants) {
    g.indice[x.uuid] = g.composants.size();
    g.composants.push_back(x.uuid);
  }
  g.brochesDe.resize(g.composants.size());
  g.membres.resize(k.reseaux.size());
  for (const auto& [cle, r] : k.parBroche) {
    const auto it = g.indice.find(cle.first);
    if (it == g.indice.end() || r < 0) continue;
    g.brochesDe[it->second].push_back({cle.second, r});
    g.membres[static_cast<std::size_t>(r)].push_back({it->second, cle.second});
  }
  for (auto& v : g.brochesDe) std::sort(v.begin(), v.end());
  return g;
}
}  // namespace

std::vector<std::map<std::string, std::string>> apparierCanaux(const ConceptionElec& c, const std::vector<std::string>& source) {
  std::vector<std::map<std::string, std::string>> r;
  const Connectivite k = connectivite(c);
  const GrapheCanaux g = construire(c, k);
  const std::size_t nc = g.composants.size(), nr = k.reseaux.size();
  // Couleurs de départ : symbole, empreinte, valeur ; réseaux : leur nombre de broches
  std::vector<std::uint64_t> cc(nc), cr(nr);
  for (std::size_t i = 0; i < nc; ++i) {
    const ComposantElec* x = c.composant(g.composants[i]);
    cc[i] = std::hash<std::string>{}(x->symbole + '|' + x->empreinte + '|' + x->valeur);
  }
  for (std::size_t r2 = 0; r2 < nr; ++r2) cr[r2] = std::hash<std::string>{}("reseau|" + std::to_string(g.membres[r2].size()));
  // Raffinement : chaque couleur reçoit le multiensemble des couleurs voisines (avec le numéro de broche)
  for (int tour = 0; tour < 6; ++tour) {
    std::vector<std::uint64_t> nc2(nc), nr2(nr);
    for (std::size_t i = 0; i < nc; ++i) {
      std::vector<std::uint64_t> v;
      for (const auto& [num, res] : g.brochesDe[i]) v.push_back(std::hash<std::string>{}(num) * 31 + cr[static_cast<std::size_t>(res)]);
      std::sort(v.begin(), v.end());
      std::uint64_t h = cc[i];
      for (auto x : v) h = h * 1'000'003 ^ x;
      nc2[i] = h;
    }
    for (std::size_t r2 = 0; r2 < nr; ++r2) {
      std::vector<std::uint64_t> v;
      for (const auto& [comp, num] : g.membres[r2]) v.push_back(cc[comp] * 31 + std::hash<std::string>{}(num));
      std::sort(v.begin(), v.end());
      std::uint64_t h = cr[r2];
      for (auto x : v) h = h * 1'000'003 ^ x;
      nr2[r2] = h;
    }
    cc = std::move(nc2);
    cr = std::move(nr2);
  }
  // Réseaux internes à un canal : leur couleur revient plusieurs fois (un par canal) ; un réseau partagé (masse,
  // alimentation, entrée commune) a une couleur unique
  std::map<std::uint64_t, int> nombreCouleurReseau;
  for (std::size_t r2 = 0; r2 < nr; ++r2) ++nombreCouleurReseau[cr[r2]];
  std::set<std::size_t> dansSource;
  for (const auto& s : source)
    if (const auto it = g.indice.find(s); it != g.indice.end()) dansSource.insert(it->second);
  if (dansSource.empty()) return r;
  // Canaux : parties connexes du graphe réduit aux réseaux internes
  std::vector<int> canal(nc, -1);
  int ncanaux = 0;
  for (std::size_t depart = 0; depart < nc; ++depart) {
    if (canal[depart] >= 0) continue;
    std::queue<std::size_t> file;
    file.push(depart);
    canal[depart] = ncanaux;
    while (!file.empty()) {
      const std::size_t i = file.front();
      file.pop();
      for (const auto& [num, res] : g.brochesDe[i]) {
        if (nombreCouleurReseau[cr[static_cast<std::size_t>(res)]] < 2) continue;
        for (const auto& [j, n2] : g.membres[static_cast<std::size_t>(res)])
          if (canal[j] < 0) {
            canal[j] = ncanaux;
            file.push(j);
          }
      }
    }
    ++ncanaux;
  }
  // Le canal de la source ; les autres canaux qui ont les mêmes couleurs de composants
  std::set<int> canauxSource;
  for (std::size_t i : dansSource) canauxSource.insert(canal[i]);
  std::multiset<std::uint64_t> signature;
  for (std::size_t i : dansSource) signature.insert(cc[i]);
  for (int k2 = 0; k2 < ncanaux; ++k2) {
    if (canauxSource.count(k2)) continue;
    std::vector<std::size_t> membres;
    std::multiset<std::uint64_t> sig;
    for (std::size_t i = 0; i < nc; ++i)
      if (canal[i] == k2) {
        membres.push_back(i);
        sig.insert(cc[i]);
      }
    if (sig != signature) continue;
    // Correspondance couleur par couleur ; à couleur égale, l'ordre des repères départage
    std::map<std::string, std::string> m;
    std::map<std::uint64_t, std::vector<std::size_t>> libres;
    for (std::size_t j : membres) libres[cc[j]].push_back(j);
    for (auto& [col, v] : libres)
      std::sort(v.begin(), v.end(), [&](std::size_t a, std::size_t b) { return ordreNaturel(c.composant(g.composants[a])->reference, c.composant(g.composants[b])->reference); });
    std::vector<std::size_t> src(dansSource.begin(), dansSource.end());
    std::sort(src.begin(), src.end(), [&](std::size_t a, std::size_t b) { return ordreNaturel(c.composant(g.composants[a])->reference, c.composant(g.composants[b])->reference); });
    for (std::size_t i : src) {
      auto& v = libres[cc[i]];
      if (v.empty()) break;
      m[g.composants[i]] = g.composants[v.front()];
      v.erase(v.begin());
    }
    if (m.size() == dansSource.size()) r.push_back(m);
  }
  return r;
}

int recopierDisposition(ConceptionElec& c, const std::vector<std::string>& source, const std::vector<std::map<std::string, std::string>>& canaux, Pt decalage, bool pistes) {
  exigerModifiable(c);
  std::map<std::string, const EmpreintePlacee*> placees;
  for (const auto& e : c.circuit.empreintes) placees[e.composant] = &e;
  int n = 0;
  const Connectivite k = connectivite(c);
  std::vector<Piste> nouvelles;
  for (std::size_t ic = 0; ic < canaux.size(); ++ic) {
    const Pt d{decalage.x * static_cast<Nm>(ic + 1), decalage.y * static_cast<Nm>(ic + 1)};
    for (const auto& [s, t] : canaux[ic]) {
      const auto ps = placees.find(s);
      if (ps == placees.end()) continue;
      const EmpreintePlacee src = *ps->second;
      for (auto& e : c.circuit.empreintes)
        if (e.composant == t) {
          e.position = src.position + d;
          e.rotation = src.rotation;
          e.dessous = src.dessous;
          ++n;
        }
    }
    if (!pistes) continue;
    // Réseaux appariés : celui d'une broche de la source → celui de la même broche de sa cible
    std::map<std::string, std::string> reseaux;
    for (const auto& [s, t] : canaux[ic])
      for (const auto& [cle, r] : k.parBroche)
        if (cle.first == s && r >= 0)
          if (const auto it = k.parBroche.find({t, cle.second}); it != k.parBroche.end() && it->second >= 0)
            reseaux[k.reseaux[static_cast<std::size_t>(r)].nom] = k.reseaux[static_cast<std::size_t>(it->second)].nom;
    for (const auto& p : c.circuit.pistes) {
      const auto it = reseaux.find(p.reseau);
      // Réseaux internes seulement : un réseau partagé (masse) se route à part
      if (it == reseaux.end() || it->second == it->first) continue;
      Piste q = p;
      q.uuid = nouvelUuid();
      q.reseau = it->second;
      q.a = p.a + d;
      q.b = p.b + d;
      if (q.milieu) *q.milieu = *q.milieu + d;
      nouvelles.push_back(q);
    }
  }
  c.circuit.pistes.insert(c.circuit.pistes.end(), nouvelles.begin(), nouvelles.end());
  return n;
}

}  // namespace prom::ecad
