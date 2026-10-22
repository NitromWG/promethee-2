// Prométhée : variantes de montage et coût de la nomenclature (lot L12). Licence GPL-3.0-only.
#include "promethee/variantes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

#include "promethee/modele.hpp"

namespace prom::ecad {

namespace {
bool declaree(const ConceptionElec& c, const std::string& nom) {
  return std::any_of(c.variantesMontage.begin(), c.variantesMontage.end(), [&](const VarianteMontage& v) { return v.nom == nom; });
}
bool vide(const SurchargeVariante& s) { return s.montage == MontageVariante::Herite && s.valeur.empty() && s.article.empty() && s.empreinte.empty(); }
std::set<std::string> composantsPoses(const ConceptionElec& c) {
  std::set<std::string> r;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) r.insert(s.composant);
  return r;
}
// Article d'un composant : celui qu'il cite, sinon celui de même référence fabricant
const Article* articleDe(const ConceptionElec& c, const ComposantElec& k) {
  if (!k.article.empty())
    if (const auto it = c.articles.find(k.article); it != c.articles.end()) return &it->second;
  if (!k.mpn.empty())
    for (const auto& [id, a] : c.articles)
      if (a.mpn == k.mpn) return &a;
  return nullptr;
}
std::string cleLigne(const ComposantElec& k, const Article* a) {
  if (a) return "article:" + a->id;
  if (!k.mpn.empty()) return "mpn:" + k.fabricant + "|" + k.mpn;
  return "valeur:" + k.valeur + "|" + k.empreinte;
}
// Meilleur prix d'une quantité sur toutes les offres d'un article
bool meilleurPrix(const Article& a, int quantite, LigneCout& l) {
  bool trouve = false;
  double meilleur = std::numeric_limits<double>::max();
  for (const auto& o : a.offres) {
    double pu = 0;
    int cmd = 0;
    if (!prixPourQuantite(o, quantite, pu, cmd)) continue;
    const double total = pu * cmd;
    if (total < meilleur) {
      meilleur = total;
      l.prixUnitaire = pu;
      l.commandee = cmd;
      l.total = total;
      l.distributeur = o.distributeur;
      l.devise = o.devise;
      trouve = true;
    }
  }
  return trouve;
}
}  // namespace

ComposantResolu resoudre(const ComposantElec& k, const std::string& variante) {
  ComposantResolu r;
  r.monte = !k.nePasMonter;
  r.valeur = k.valeur;
  r.article = k.article;
  r.empreinte = k.empreinte;
  if (variante.empty()) return r;
  const auto it = k.variantes.find(variante);
  if (it == k.variantes.end()) return r;
  const SurchargeVariante& s = it->second;
  if (s.montage != MontageVariante::Herite) r.monte = s.montage == MontageVariante::Monte;
  if (!s.valeur.empty() && s.valeur != k.valeur) {
    r.valeur = s.valeur;
    r.valeurChangee = true;
  }
  if (!s.article.empty() && s.article != k.article) {
    r.article = s.article;
    r.articleChange = true;
  }
  if (!s.empreinte.empty() && s.empreinte != k.empreinte) {
    r.empreinte = s.empreinte;
    r.empreinteChangee = true;
  }
  return r;
}

ConceptionElec conceptionPourVariante(const ConceptionElec& c, const std::string& variante) {
  ConceptionElec r = c;
  r.varianteActive = variante;
  if (variante.empty()) return r;
  std::map<std::string, std::string> empreintes;  // composant → empreinte de remplacement
  for (auto& k : r.composants) {
    const ComposantResolu x = resoudre(k, variante);
    k.nePasMonter = !x.monte;
    k.valeur = x.valeur;
    if (x.articleChange) {
      k.article = x.article;
      if (const auto it = r.articles.find(x.article); it != r.articles.end()) {
        k.fabricant = it->second.fabricant;
        k.mpn = it->second.mpn;
        k.lcsc = it->second.lcsc;
        if (!x.valeurChangee && !it->second.valeur.empty()) k.valeur = it->second.valeur;
      }
    }
    if (x.empreinteChangee) {
      k.empreinte = x.empreinte;
      empreintes[k.uuid] = x.empreinte;
    }
  }
  // L'empreinte de remplacement se pose sous le même repère, à la même place (12.7)
  for (auto& e : r.circuit.empreintes)
    if (const auto it = empreintes.find(e.composant); it != empreintes.end()) e.empreinte = it->second;
  return r;
}

bool ajouterVariante(ConceptionElec& c, const std::string& nom, const std::string& description) {
  if (nom.empty() || declaree(c, nom)) return false;
  c.variantesMontage.push_back({nom, description});
  return true;
}

bool renommerVariante(ConceptionElec& c, const std::string& ancien, const std::string& nouveau) {
  if (nouveau.empty() || !declaree(c, ancien) || declaree(c, nouveau)) return false;
  for (auto& v : c.variantesMontage)
    if (v.nom == ancien) v.nom = nouveau;
  for (auto& k : c.composants)
    if (const auto it = k.variantes.find(ancien); it != k.variantes.end()) {
      k.variantes[nouveau] = it->second;
      k.variantes.erase(ancien);
    }
  if (c.varianteActive == ancien) c.varianteActive = nouveau;
  for (auto& j : c.jeuxSorties)
    if (j.variante == ancien) j.variante = nouveau;
  return true;
}

bool supprimerVariante(ConceptionElec& c, const std::string& nom) {
  if (!declaree(c, nom)) return false;
  c.variantesMontage.erase(std::remove_if(c.variantesMontage.begin(), c.variantesMontage.end(), [&](const VarianteMontage& v) { return v.nom == nom; }), c.variantesMontage.end());
  for (auto& k : c.composants) k.variantes.erase(nom);
  if (c.varianteActive == nom) c.varianteActive.clear();
  for (auto& j : c.jeuxSorties)
    if (j.variante == nom) j.variante.clear();
  return true;
}

bool reglerSurcharge(ConceptionElec& c, const std::string& reference, const std::string& variante, const SurchargeVariante& s) {
  if (!declaree(c, variante)) return false;
  for (auto& k : c.composants)
    if (k.reference == reference) {
      if (vide(s)) k.variantes.erase(variante);
      else k.variantes[variante] = s;
      return true;
    }
  return false;
}

std::vector<std::string> verifierVariantes(const ConceptionElec& c) {
  std::vector<std::string> r;
  if (!c.varianteActive.empty() && !declaree(c, c.varianteActive)) r.push_back("Variante active « " + c.varianteActive + " » inconnue : la carte de base est montrée.");
  std::set<std::string> signalees;
  for (const auto& k : c.composants)
    for (const auto& [nom, s] : k.variantes) {
      if (!declaree(c, nom) && signalees.insert(nom).second) r.push_back("Variante « " + nom + " » citée par " + k.reference + " mais non déclarée.");
      if (!s.article.empty() && !c.articles.count(s.article)) r.push_back(k.reference + " (variante « " + nom + " ») : article « " + s.article + " » introuvable dans le projet.");
      if (!s.empreinte.empty() && !c.empreinte(s.empreinte)) r.push_back(k.reference + " (variante « " + nom + " ») : empreinte « " + s.empreinte + " » introuvable.");
    }
  return r;
}

std::vector<LigneVariantes> tableauVariantes(const ConceptionElec& c) {
  std::vector<LigneVariantes> r;
  for (const auto& k : c.composants) {
    bool change = false;
    LigneVariantes l;
    l.reference = k.reference;
    l.base = (k.nePasMonter ? "non monté · " : "") + k.valeur;
    for (const auto& v : c.variantesMontage) {
      const ComposantResolu x = resoudre(k, v.nom);
      std::string cellule;
      if (x.monte != !k.nePasMonter) cellule = x.monte ? "monté" : "non monté";
      if (x.valeurChangee) cellule += (cellule.empty() ? "" : " · ") + x.valeur;
      if (x.articleChange) cellule += (cellule.empty() ? "" : " · ") + std::string("article ") + x.article;
      if (x.empreinteChangee) cellule += (cellule.empty() ? "" : " · ") + x.empreinte;
      if (cellule.empty()) cellule = "=";
      else change = true;
      l.cellules.push_back(cellule);
    }
    if (change) r.push_back(l);
  }
  std::sort(r.begin(), r.end(), [](const LigneVariantes& a, const LigneVariantes& b) { return ordreNaturel(a.reference, b.reference); });
  return r;
}

bool prixPourQuantite(const OffreArticle& o, int quantite, double& prixUnitaire, int& commandee) {
  if (o.prix.empty()) return false;
  std::vector<std::pair<int, double>> paliers = o.prix;
  std::sort(paliers.begin(), paliers.end());
  int q = std::max(quantite, paliers.front().first);
  const int lot = std::max(1, o.conditionnement);
  if (q % lot) q += lot - q % lot;
  double pu = paliers.front().second;
  for (const auto& [min, prix] : paliers)
    if (q >= min) pu = prix;
  prixUnitaire = pu;
  commandee = q;
  return true;
}

CoutNomenclature coutNomenclature(const ConceptionElec& c0, int cartes, const std::string& variante) {
  const ConceptionElec c = conceptionPourVariante(c0, variante);
  CoutNomenclature r;
  r.cartes = std::max(1, cartes);
  const std::set<std::string> poses = composantsPoses(c);
  std::map<std::string, LigneCout> lignes;
  std::map<std::string, const Article*> articles;
  for (const auto& k : c.composants) {
    if (!poses.count(k.uuid) || k.nePasMonter || !dansNomenclature(c, k)) continue;
    const Article* a = articleDe(c, k);
    const std::string cle = cleLigne(k, a);
    LigneCout& l = lignes[cle];
    l.references.push_back(k.reference);
    l.parCarte++;
    l.valeur = k.valeur;
    l.mpn = a ? a->mpn : k.mpn;
    l.article = a ? a->id : std::string();
    articles[cle] = a;
  }
  for (auto& [cle, l] : lignes) {
    std::sort(l.references.begin(), l.references.end(), ordreNaturel);
    l.commandee = l.parCarte * r.cartes;
    if (const Article* a = articles[cle]; a && meilleurPrix(*a, l.parCarte * r.cartes, l)) {
      l.prixConnu = true;
      r.total += l.total;
      r.devise = l.devise;
    } else
      ++r.lignesSansPrix;
    r.lignes.push_back(l);
  }
  std::sort(r.lignes.begin(), r.lignes.end(), [](const LigneCout& a, const LigneCout& b) { return ordreNaturel(a.references.front(), b.references.front()); });
  r.parCarte = r.total / r.cartes;
  return r;
}

NomenclatureProjet nomenclatureProjet(const Projet& p, int systemes) {
  NomenclatureProjet r;
  r.systemes = std::max(1, systemes);
  std::map<std::string, LigneNomenclatureProjet> lignes;
  std::map<std::string, Article> articles;  // copies : chaque conception résolue ne vit que le temps de sa boucle
  const std::size_t n = p.electronique.size();
  for (std::size_t i = 0; i < n; ++i) {
    const ConceptionElec c = conceptionPourVariante(p.electronique[i], p.electronique[i].varianteActive);
    r.cartes.push_back(c.nom);
    const std::set<std::string> poses = composantsPoses(c);
    for (const auto& k : c.composants) {
      if (!poses.count(k.uuid) || k.nePasMonter || !dansNomenclature(c, k)) continue;
      const Article* a = articleDe(c, k);
      const std::string cle = cleLigne(k, a);
      LigneNomenclatureProjet& l = lignes[cle];
      if (l.parCarte.empty()) {
        l.parCarte.assign(n, 0);
        l.cle = cle;
        l.valeur = k.valeur;
        l.empreinte = k.empreinte;
        l.mpn = a ? a->mpn : k.mpn;
        l.article = a ? a->id : std::string();
      }
      l.parCarte[i]++;
      l.total++;
      if (a) articles[cle] = *a;
    }
  }
  for (auto& [cle, l] : lignes) {
    if (const auto it = articles.find(cle); it != articles.end()) {
      LigneCout lc;
      if (meilleurPrix(it->second, l.total * r.systemes, lc)) {
        l.prixUnitaire = lc.prixUnitaire;
        l.prixConnu = true;
        r.coutSysteme += lc.total / r.systemes;
      }
    }
    r.lignes.push_back(l);
  }
  std::sort(r.lignes.begin(), r.lignes.end(), [](const LigneNomenclatureProjet& a, const LigneNomenclatureProjet& b) { return a.cle < b.cle; });
  return r;
}

}  // namespace prom::ecad
