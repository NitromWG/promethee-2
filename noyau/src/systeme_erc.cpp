// Prométhée : netlist et vérification du système (lot L12, 12.19). Licence GPL-3.0-only.
#include "promethee/systeme_erc.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>

#include "promethee/modele.hpp"

namespace prom {

namespace {
using namespace ecad;

struct Contexte {
  const ConceptionElec* c = nullptr;
  Connectivite k;
};

const ConceptionElec* carte(const Projet& p, const std::string& id) {
  for (const auto& c : p.electronique)
    if (c.uuid == id || c.nom == id) return &c;
  return nullptr;
}

// Type de la broche « numero » d'un composant, lu dans son symbole
TypeBroche typeDe(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  const ComposantElec* k = c.composant(composant);
  const DefSymbole* d = k ? c.symbole(k->symbole) : nullptr;
  if (d)
    for (const auto& b : d->broches)
      if (b.numero == numero) return b.type;
  return TypeBroche::NonSpecifiee;
}

struct Ensembles {
  std::vector<int> parent;
  int trouver(int x) { return parent[static_cast<std::size_t>(x)] == x ? x : parent[static_cast<std::size_t>(x)] = trouver(parent[static_cast<std::size_t>(x)]); }
  void unir(int a, int b) { parent[static_cast<std::size_t>(trouver(a))] = trouver(b); }
};

struct Graphe {
  std::map<std::pair<std::string, std::string>, int> indice;  // (UUID de la carte, réseau) → nœud
  std::vector<std::pair<std::string, std::string>> noeuds;
  std::map<int, std::set<std::string>> liaisons;               // nœud → câbles qui le touchent
  Ensembles e;
  int noeud(const std::string& carte, const std::string& reseau) {
    const auto it = indice.find({carte, reseau});
    if (it != indice.end()) return it->second;
    const int n = static_cast<int>(noeuds.size());
    indice[{carte, reseau}] = n;
    noeuds.emplace_back(carte, reseau);
    e.parent.push_back(n);
    return n;
  }
};

Graphe construire(const Projet& p, std::map<std::string, Contexte>& ctx, std::vector<std::string>& alertes, std::vector<ViolationSysteme>* violations) {
  Graphe g;
  for (const auto& c : p.electronique) ctx[c.uuid] = {&c, connectivite(c)};
  for (const auto& l : p.liaisons) {
    const ConceptionElec* a = carte(p, l.carteA);
    const ConceptionElec* b = carte(p, l.carteB);
    const std::string nom = l.nom.empty() ? l.connecteurA + "–" + l.connecteurB : l.nom;
    if (!a || !b) {
      alertes.push_back("Liaison « " + nom + " » : carte introuvable.");
      continue;
    }
    const ComposantElec* ja = a->parReference(l.connecteurA);
    const ComposantElec* jb = b->parReference(l.connecteurB);
    if (!ja || !jb) {
      alertes.push_back("Liaison « " + nom + " » : connecteur " + (!ja ? l.connecteurA + " absent de « " + a->nom + " »" : l.connecteurB + " absent de « " + b->nom + " »") + ".");
      continue;
    }
    std::vector<std::pair<std::string, std::string>> paires = l.broches;
    if (paires.empty()) {
      // Broche à broche : les numéros communs aux deux connecteurs ; des nombres différents sont signalés
      const DefSymbole* sa = a->symbole(ja->symbole);
      const DefSymbole* sb = b->symbole(jb->symbole);
      std::set<std::string> na, nb;
      if (sa)
        for (const auto& x : sa->broches) na.insert(x.numero);
      if (sb)
        for (const auto& x : sb->broches) nb.insert(x.numero);
      for (const auto& x : na)
        if (nb.count(x)) paires.emplace_back(x, x);
      if (violations && na.size() != nb.size())
        violations->push_back({false, nom, {}, "Liaison « " + nom + " » : " + l.connecteurA + " a " + std::to_string(na.size()) + " broches, " + l.connecteurB + " en a " + std::to_string(nb.size()) + "."});
    }
    const Contexte& ca = ctx[a->uuid];
    const Contexte& cb = ctx[b->uuid];
    for (const auto& [pa, pb] : paires) {
      const std::string ra = ca.k.nom(ja->uuid, pa), rb = cb.k.nom(jb->uuid, pb);
      if (ra.empty() || rb.empty()) continue;  // broche libre d'un côté
      const int x = g.noeud(a->uuid, ra), y = g.noeud(b->uuid, rb);
      g.e.unir(x, y);
      g.liaisons[x].insert(nom);
      g.liaisons[y].insert(nom);
    }
  }
  return g;
}
}  // namespace

NetlistSysteme netlistSysteme(const Projet& p) {
  NetlistSysteme r;
  std::map<std::string, Contexte> ctx;
  Graphe g = construire(p, ctx, r.alertes, nullptr);
  std::map<int, ReseauSysteme> groupes;
  for (int i = 0; i < static_cast<int>(g.noeuds.size()); ++i) {
    ReseauSysteme& s = groupes[g.e.trouver(i)];
    const auto& [uuid, reseau] = g.noeuds[static_cast<std::size_t>(i)];
    s.membres.emplace_back(ctx[uuid].c->nom, reseau);
    for (const auto& l : g.liaisons[i])
      if (std::find(s.liaisons.begin(), s.liaisons.end(), l) == s.liaisons.end()) s.liaisons.push_back(l);
  }
  for (auto& [racine, s] : groupes) {
    std::set<std::string> noms;
    for (const auto& m : s.membres) noms.insert(m.second);
    s.nom = noms.size() == 1 ? *noms.begin() : s.membres.front().first + ":" + s.membres.front().second;
    r.reseaux.push_back(s);
  }
  return r;
}

std::vector<ViolationSysteme> verifierSysteme(const Projet& p) {
  std::vector<ViolationSysteme> v;
  std::map<std::string, Contexte> ctx;
  std::vector<std::string> alertes;
  Graphe g = construire(p, ctx, alertes, &v);
  for (const auto& a : alertes) v.push_back({true, {}, {}, a});
  std::map<int, std::vector<int>> groupes;
  for (int i = 0; i < static_cast<int>(g.noeuds.size()); ++i) groupes[g.e.trouver(i)].push_back(i);
  for (const auto& [racine, membres] : groupes) {
    if (membres.size() < 2) continue;
    // Sorties de chaque carte sur ce réseau du système, et alimentations nommées
    std::vector<std::string> sorties, alims;
    std::set<std::string> cables;
    std::set<std::string> nomsAlim;
    std::string libelle;
    for (int i : membres) {
      const auto& [uuid, reseau] = g.noeuds[static_cast<std::size_t>(i)];
      const Contexte& cx = ctx[uuid];
      for (const auto& l : g.liaisons[i]) cables.insert(l);
      libelle += (libelle.empty() ? "" : " = ") + cx.c->nom + ":" + reseau;
      const int ir = cx.k.indice(reseau);
      if (ir < 0) continue;
      const Reseau& r = cx.k.reseaux[static_cast<std::size_t>(ir)];
      for (const auto& a : r.alimentations) nomsAlim.insert(a);
      for (const auto& b : r.broches) {
        const TypeBroche t = typeDe(*cx.c, b.composant, b.broche);
        const ComposantElec* k = cx.c->composant(b.composant);
        const std::string qui = cx.c->nom + ":" + (k ? k->reference : b.composant) + "." + b.broche;
        if (t == TypeBroche::Sortie) sorties.push_back(qui);
        if (t == TypeBroche::AlimSortie) alims.push_back(qui);
      }
    }
    std::string liste;
    for (const auto& c : cables) liste += (liste.empty() ? "" : ", ") + c;
    if (sorties.size() >= 2) {
      std::string s;
      for (const auto& x : sorties) s += (s.empty() ? "" : ", ") + x;
      v.push_back({true, liste, libelle, "Sortie contre sortie sur le câble " + liste + " : " + s + "."});
    }
    if (alims.size() >= 2) {
      std::string s;
      for (const auto& x : alims) s += (s.empty() ? "" : ", ") + x;
      v.push_back({true, liste, libelle, "Deux sorties d'alimentation reliées par le câble " + liste + " : " + s + "."});
    }
    if (nomsAlim.size() >= 2) {
      std::string s;
      for (const auto& x : nomsAlim) s += (s.empty() ? "" : " et ") + x;
      v.push_back({true, liste, libelle, "Alimentations différentes jointes par le câble " + liste + " : " + s + "."});
    }
  }
  return v;
}

}  // namespace prom
