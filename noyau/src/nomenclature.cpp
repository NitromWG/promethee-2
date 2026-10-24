// Prométhée : nomenclature unique. Licence GPL-3.0-only.
#include "promethee/csv.hpp"
#include "promethee/nomenclature.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace prom {

namespace {
// Tri naturel des repères : D2 avant D10.
bool avantNaturel(const std::string& a, const std::string& b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
      size_t fi = i, fj = j;
      while (fi < a.size() && std::isdigit(static_cast<unsigned char>(a[fi]))) ++fi;
      while (fj < b.size() && std::isdigit(static_cast<unsigned char>(b[fj]))) ++fj;
      // Suites de chiffres comparées comme des chaînes (longueur sans les zéros de tête, puis ordre) : aucune
      // conversion, donc aucun débordement quelle que soit leur longueur (EX-14)
      std::size_t si = i, sj = j;
      while (si + 1 < fi && a[si] == '0') ++si;
      while (sj + 1 < fj && b[sj] == '0') ++sj;
      if (fi - si != fj - sj) return fi - si < fj - sj;
      if (const int c = a.compare(si, fi - si, b, sj, fj - sj); c != 0) return c < 0;
      i = fi; j = fj;
    } else {
      if (a[i] != b[j]) return a[i] < b[j];
      ++i; ++j;
    }
  }
  return a.size() - i < b.size() - j;
}

}  // namespace

std::string champCsv(const std::string& s) { return champCsvRegle(s); }

bool avantNaturelRepere(const std::string& a, const std::string& b) { return avantNaturel(a, b); }

std::vector<LigneNomenclature> nomenclature(const Projet& p, const Derive& d) {
  std::vector<LigneNomenclature> l;
  std::vector<const Composant*> comps;
  for (const auto& k : p.composants) comps.push_back(&k);
  std::sort(comps.begin(), comps.end(), [](const Composant* a, const Composant* b) { return avantNaturel(a->ref, b->ref); });
  for (const auto* k : comps) {
    const TypeComposant* T = typeComposant(k->type);
    l.push_back({"Électronique", k->ref, (T ? T->nom : k->type) + (k->valeur.empty() ? "" : ", " + k->valeur), "Achat", 1});
  }
  // Pièces montées sur les cartes électroniques (6.8, nomenclature commune) : dissipateurs, entretoises, blindages
  const ecad::BoitierImporte* commerce = nullptr;
  for (const auto& ce : p.electronique) {
    for (const auto& m : ce.piecesMontees) {
      std::string designation = m.nom;
      if (!m.fabricant.empty() || !m.mpn.empty()) designation += " (" + m.fabricant + (m.fabricant.empty() || m.mpn.empty() ? "" : " ") + m.mpn + ")";
      l.push_back({"Pièces montées", m.reference.empty() ? m.nom : m.reference, designation, m.fournisseur.empty() ? "Achat" : "Achat (" + m.fournisseur + ")", std::max(1, m.quantite)});
    }
    if (ce.pilotageBoitier && ce.boitierImporte) commerce = &*ce.boitierImporte;
  }
  const auto& c = p.carte;
  l.push_back({"Pièces fabriquées", "PCB1", "Carte " + fmt(c.L, 2) + " × " + fmt(c.W, 2) + " mm, épaisseur " + fmt(c.t, 2) + " mm", "Fabrication de circuits imprimés", 1});
  if (commerce)
    // Boîtier du commerce importé (6.7) : acheté, pas imprimé
    l.push_back({"Pièces achetées", "B1", "Boîtier " + (commerce->nom.empty() ? std::string("du commerce") : commerce->nom) + (commerce->mpn.empty() ? std::string() : " (" + commerce->fabricant + " " + commerce->mpn + ")"), "Achat", 1});
  else {
    l.push_back({"Pièces fabriquées", "B1", "Boîtier " + fmt(d.Lo, 2) + " × " + fmt(d.Wo, 2) + " × " + fmt(d.zt, 2) + " mm", "Impression 3D", 1});
    l.push_back({"Pièces fabriquées", "B2", "Couvercle " + fmt(d.Lo, 2) + " × " + fmt(d.Wo, 2) + " × " + fmt(p.boitier.couvercle + Levre::h, 2) + " mm", "Impression 3D", 1});
  }
  // Visserie regroupée par taille.
  std::map<std::string, int> vis, inserts, visInserts;
  for (const auto& t : d.trous) {
    const std::string taille = t.nomVis + " × " + std::to_string(t.longVis);
    if (t.insert) {
      ++inserts[t.nomVis + " × " + fmt(t.longInsert)];
      ++visInserts[taille];
    } else {
      ++vis[taille];
    }
  }
  int n = 0;
  for (const auto& [taille, q] : vis) l.push_back({"Visserie", "V" + std::to_string(++n), "Vis autotaraudeuse pour plastique " + taille, "Achat", q});
  for (const auto& [taille, q] : visInserts) l.push_back({"Visserie", "V" + std::to_string(++n), "Vis à métaux tête cylindrique " + taille, "Achat", q});
  for (const auto& [taille, q] : inserts) l.push_back({"Visserie", "I" + std::to_string(++n), "Insert laiton à poser à chaud " + taille, "Achat", q});
  return l;
}

std::string versCsv(const std::vector<LigneNomenclature>& lignes) {
  EcrivainCsv w;  // écrivain du poste (10.20)
  w.ligne({"Groupe", "Repère", "Désignation", "Quantité", "Approvisionnement"});
  for (const auto& x : lignes) w.champ(x.groupe).champ(x.ref).champ(x.designation).entier(x.qte).champ(x.appro).fin();
  return w.texte();
}

}  // namespace prom
