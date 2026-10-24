// Prométhée : approvisionnement hors ligne (lot L4, tâche 4.18). Licence GPL-3.0-only.
#include "promethee/approvisionnement.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "promethee/circuit_edition.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
std::string minuscules(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  s.erase(std::remove_if(s.begin(), s.end(), [](char ch) { return ch == ' ' || ch == '_' || ch == '-' || ch == '.'; }), s.end());
  return s;
}
double nombre(const std::string& s) {
  std::string t;
  for (char ch : s)
    if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '.' || ch == ',' || ch == '-') t += ch == ',' ? '.' : ch;
  return t.empty() ? 0.0 : std::atof(t.c_str());
}
FamilleValeur familleDeLigne(const LigneNomenclature& l) {
  std::string pre;
  if (!l.references.empty())
    for (char ch : l.references.front()) {
      if (std::isdigit(static_cast<unsigned char>(ch))) break;
      pre += ch;
    }
  return familleDepuisPrefixe(pre);
}
}  // namespace

SourceStock lireSourceStock(const std::string& contenu, const std::string& nom, bool stockPropre, std::string* erreur) {
  SourceStock s;
  s.nom = nom;
  s.stockPropre = stockPropre;
  const bool xlsx = contenu.size() > 4 && contenu.compare(0, 2, "PK") == 0;
  const auto lignes = xlsx ? lireXlsx(contenu) : lireTableau(contenu);
  if (lignes.empty()) {
    if (erreur) *erreur = "fichier vide ou illisible";
    return s;
  }
  std::map<std::string, int> col;
  std::size_t entete = 0;
  for (std::size_t li = 0; li < std::min<std::size_t>(lignes.size(), 10) && col.empty(); ++li) {
    for (std::size_t i = 0; i < lignes[li].size(); ++i) {
      const std::string h = minuscules(lignes[li][i]);
      auto si = [&](std::initializer_list<const char*> noms, const char* cle) {
        for (const char* n : noms)
          if (h == n && !col.count(cle)) col[cle] = static_cast<int>(i);
      };
      si({"mpn", "referencefabricant", "référencefabricant", "manufacturerpart", "manufacturerpartnumber", "mfrpart", "partnumber", "reference"}, "mpn");
      si({"fabricant", "manufacturer", "mfr", "marque"}, "fabricant");
      si({"valeur", "value", "comment"}, "valeur");
      si({"boitier", "boîtier", "empreinte", "footprint", "package"}, "empreinte");
      si({"lcsc", "lcscpart", "lcscpartnumber", "jlcpcbpart"}, "lcsc");
      si({"quantite", "quantité", "stock", "qty", "quantity", "enstock"}, "quantite");
      si({"prix", "price", "unitprice", "prixunitaire"}, "prix");
      si({"emplacement", "location", "tiroir", "casier"}, "emplacement");
      si({"type", "librarytype", "categorie", "catégorie"}, "type");
      si({"description", "designation", "désignation"}, "description");
    }
    if (!col.empty()) entete = li;
  }
  if (!col.count("mpn") && !col.count("valeur") && !col.count("lcsc")) {
    if (erreur) *erreur = "en-têtes non reconnus : il faut une référence, une valeur ou une référence LCSC";
    return s;
  }
  for (std::size_t li = entete + 1; li < lignes.size(); ++li) {
    const auto& l = lignes[li];
    auto champ = [&](const char* cle) {
      const auto it = col.find(cle);
      return it != col.end() && static_cast<std::size_t>(it->second) < l.size() ? l[static_cast<std::size_t>(it->second)] : std::string();
    };
    LigneStock x;
    x.mpn = champ("mpn");
    x.fabricant = champ("fabricant");
    x.valeur = champ("valeur");
    x.empreinte = champ("empreinte");
    x.lcsc = champ("lcsc");
    x.emplacement = champ("emplacement");
    x.description = champ("description");
    const std::string q = champ("quantite");
    x.quantite = q.empty() ? (stockPropre ? 0 : -1) : static_cast<int>(nombre(q));
    x.prix = nombre(champ("prix"));
    x.basique = minuscules(champ("type")).find("basic") != std::string::npos || minuscules(champ("type")).find("base") != std::string::npos;
    if (x.mpn.empty() && x.valeur.empty() && x.lcsc.empty()) continue;
    s.lignes.push_back(std::move(x));
  }
  return s;
}

std::string codeTaille(const std::string& empreinte) {
  // Le premier groupe de quatre chiffres d'une taille impériale connue
  static const char* tailles[] = {"01005", "0201", "0402", "0603", "0805", "1206", "1210", "1812", "2010", "2512"};
  for (const char* t : tailles)
    if (empreinte.find(t) != std::string::npos) return t;
  return {};
}

bool couvre(const LigneStock& s, const LigneNomenclature& l) {
  if (!s.mpn.empty() && !l.mpn.empty()) return minuscules(s.mpn) == minuscules(l.mpn);
  if (!s.lcsc.empty() && !l.lcsc.empty()) return minuscules(s.lcsc) == minuscules(l.lcsc);
  if (s.valeur.empty() || l.valeur.empty()) return false;
  const FamilleValeur f = familleDeLigne(l);
  if (f == FamilleValeur::Aucune) return minuscules(s.valeur) == minuscules(l.valeur) && codeTaille(s.empreinte) == codeTaille(l.empreinte);
  const ValeurComprise a = lireValeurComposant(s.valeur, f), b = lireValeurComposant(l.valeur, f);
  if (!a.comprise() || !b.comprise() || std::abs(a.nombre - b.nombre) > 1e-3 * std::max(std::abs(a.nombre), std::abs(b.nombre))) return false;
  const std::string ta = codeTaille(s.empreinte), tb = codeTaille(l.empreinte);
  return ta.empty() || tb.empty() ? minuscules(s.empreinte) == minuscules(l.empreinte) || s.empreinte.empty() : ta == tb;
}

ChoixOffre meilleureOffre(const std::vector<OffreArticle>& offres, int quantite) {
  ChoixOffre r;
  if (quantite <= 0) return r;
  for (const auto& o : offres) {
    if (o.prix.empty()) continue;
    const int cond = std::max(1, o.conditionnement);
    int q = (quantite + cond - 1) / cond * cond;
    // Palier atteint pour cette quantité (quantité minimale ≤ q) ; sous le premier palier, on monte jusqu'à lui
    auto paliers = o.prix;
    std::sort(paliers.begin(), paliers.end());
    if (q < paliers.front().first) q = (paliers.front().first + cond - 1) / cond * cond;
    if (o.stock >= 0 && o.stock < q) continue;
    double pu = paliers.front().second;
    for (const auto& [min, p] : paliers)
      if (q >= min) pu = p;
    // Un palier plus haut peut coûter moins au total (100 pièces moins chères que 80)
    double total = pu * q;
    for (const auto& [min, p] : paliers)
      if (min > q && p * ((min + cond - 1) / cond * cond) < total && (o.stock < 0 || o.stock >= min)) {
        total = p * ((min + cond - 1) / cond * cond);
        q = (min + cond - 1) / cond * cond;
        pu = p;
      }
    if (!r.offre || total < r.total) r = {&o, q, pu, total};
  }
  return r;
}

std::vector<BesoinAchat> besoinsAchat(const ConceptionElec& c, int cartes, const std::vector<SourceStock>& sources, const std::vector<Article>& articles) {
  std::vector<BesoinAchat> r;
  // Stock restant de chaque ligne de stock de l'atelier, consommé ligne après ligne
  std::map<std::pair<std::size_t, std::size_t>, int> reste;
  for (std::size_t i = 0; i < sources.size(); ++i)
    for (std::size_t j = 0; j < sources[i].lignes.size(); ++j) reste[{i, j}] = sources[i].lignes[j].quantite;
  for (const auto& l : nomenclature(c)) {
    BesoinAchat b;
    b.ligne = l;
    b.besoin = l.quantite * std::max(cartes, 0);
    int manque = b.besoin;
    std::vector<std::string> lieux;
    for (std::size_t i = 0; i < sources.size() && manque > 0; ++i) {
      if (!sources[i].stockPropre) continue;
      for (std::size_t j = 0; j < sources[i].lignes.size() && manque > 0; ++j) {
        if (!couvre(sources[i].lignes[j], l)) continue;
        int& dispo = reste[{i, j}];
        const int pris = std::min(manque, std::max(dispo, 0));
        if (pris <= 0) continue;
        dispo -= pris;
        manque -= pris;
        b.enStock += pris;
        lieux.push_back(sources[i].nom + (sources[i].lignes[j].emplacement.empty() ? std::string() : " (" + sources[i].lignes[j].emplacement + ")"));
      }
    }
    b.aAcheter = manque;
    if (manque > 0) {
      // Offres de l'article de la ligne (même MPN), puis lignes de catalogue qui la couvrent
      const Article* art = nullptr;
      for (const auto& a : articles)
        if (!l.mpn.empty() && minuscules(a.mpn) == minuscules(l.mpn)) art = &a;
      const ChoixOffre o = art ? meilleureOffre(art->offres, manque) : ChoixOffre{};
      if (o.offre) {
        b.source = o.offre->distributeur + " " + o.offre->reference;
        b.quantiteCommandee = o.quantite;
        b.prixUnitaire = o.prixUnitaire;
        b.montant = o.total;
      } else
        for (const auto& s : sources) {
          if (s.stockPropre) continue;
          const auto it = std::find_if(s.lignes.begin(), s.lignes.end(), [&](const LigneStock& x) { return couvre(x, l) && (x.quantite < 0 || x.quantite >= manque); });
          if (it == s.lignes.end()) continue;
          b.source = s.nom + " " + (it->lcsc.empty() ? it->mpn : it->lcsc);
          b.quantiteCommandee = manque;
          b.prixUnitaire = it->prix;
          b.montant = it->prix * manque;
          break;
        }
    }
    if (!lieux.empty()) b.source = (b.source.empty() ? std::string() : b.source + " ; ") + "stock : " + lieux.front() + (lieux.size() > 1 ? "…" : "");
    r.push_back(std::move(b));
  }
  return r;
}

std::string csvBesoins(const std::vector<BesoinAchat>& b) {
  std::string s = "Repères;Valeur;Empreinte;Référence;Besoin;En stock;À acheter;Commandé;Source;Prix unitaire;Montant\n";
  double total = 0;
  for (const auto& x : b) {
    std::string refs;
    for (const auto& r : x.ligne.references) refs += (refs.empty() ? "" : ",") + r;
    char t[64];
    std::snprintf(t, sizeof t, "%.4f;%.2f", x.prixUnitaire, x.montant);
    s += refs + ";" + x.ligne.valeur + ";" + x.ligne.empreinte + ";" + x.ligne.mpn + ";" + std::to_string(x.besoin) + ";" + std::to_string(x.enStock) + ";" + std::to_string(x.aAcheter) + ";" +
         std::to_string(x.quantiteCommandee) + ";" + x.source + ";" + t + "\n";
    total += x.montant;
  }
  char t[48];
  std::snprintf(t, sizeof t, "%.2f", total);
  return s + ";;;;;;;;Total;;" + t + "\n";
}

bool remplacantCompatible(const Article& a, const Article& b, std::string* raison) {
  auto dire = [&](const std::string& r) {
    if (raison) *raison = r;
    return false;
  };
  if (!a.empreinte.empty() && !b.empreinte.empty() && a.empreinte != b.empreinte) {
    const std::string fa = familleBoitier(a.empreinte), fb = familleBoitier(b.empreinte);
    if (fa.empty() || fa != fb) return dire("boîtiers différents : " + a.empreinte + " et " + b.empreinte);
    const DefEmpreinte* ea = empreinteEnCache(a.empreinte);
    const DefEmpreinte* eb = empreinteEnCache(b.empreinte);
    if (ea && eb && ea->pastilles.size() != eb->pastilles.size()) return dire("nombre de pastilles différent");
  }
  // Même broche → même pastille ; un brochage absent vaut les mêmes numéros
  std::map<std::string, std::string> ba = a.brochage, bb = b.brochage;
  for (const auto& [broche, pastille] : ba) {
    const auto it = bb.find(broche);
    const std::string autre = it == bb.end() ? broche : it->second;
    if (autre != pastille) return dire("brochage différent : broche " + broche + " sur " + pastille + " et sur " + autre);
  }
  for (const auto& [broche, pastille] : bb)
    if (!ba.count(broche) && pastille != broche) return dire("brochage différent : broche " + broche + " sur " + pastille);
  if (raison) raison->clear();
  return true;
}

std::vector<Semblable> chercherSemblables(const Article& a, const std::vector<Article>& candidats, const std::vector<SourceStock>& sources) {
  std::vector<Semblable> r;
  const FamilleValeur f = familleDepuisPrefixe(a.categorie == "Résistances" ? "R" : a.categorie == "Condensateurs" ? "C" : a.categorie == "Inductances" ? "L" : "");
  for (const auto& b : candidats) {
    if (b.id == a.id) continue;
    double s = 0;
    std::vector<std::string> pourquoi;
    if (!a.categorie.empty() && a.categorie == b.categorie) s += 0.25, pourquoi.push_back("même catégorie");
    if (!a.valeur.empty() && !b.valeur.empty()) {
      bool memeValeur = minuscules(a.valeur) == minuscules(b.valeur);
      if (!memeValeur && f != FamilleValeur::Aucune) {
        const auto va = lireValeurComposant(a.valeur, f), vb = lireValeurComposant(b.valeur, f);
        memeValeur = va.comprise() && vb.comprise() && std::abs(va.nombre - vb.nombre) <= 1e-3 * std::abs(va.nombre);
      }
      if (memeValeur) s += 0.3, pourquoi.push_back("même valeur");
      else continue;  // une autre valeur n'est pas un remplaçant
    }
    std::string r2;
    if (remplacantCompatible(a, b, &r2)) {
      s += 0.3;
      pourquoi.push_back(a.empreinte == b.empreinte ? "même empreinte" : "boîtier et brochage compatibles");
    } else
      s -= 0.2, pourquoi.push_back(r2);
    int communs = 0, egaux = 0;
    for (const auto& [k, v] : a.parametres)
      if (const auto it = b.parametres.find(k); it != b.parametres.end()) ++communs, egaux += it->second == v ? 1 : 0;
    if (communs) s += 0.15 * egaux / communs;
    if (b.etatCycle == "obsolète" || b.etatCycle == "fin de série") s -= 0.3, pourquoi.push_back("fin de vie");
    Semblable x;
    x.id = b.id;
    x.confiance = std::clamp(s, 0.0, 1.0);
    for (const auto& src : sources)
      for (const auto& l : src.lignes)
        if ((!l.mpn.empty() && minuscules(l.mpn) == minuscules(b.mpn)) || (!l.lcsc.empty() && minuscules(l.lcsc) == minuscules(b.lcsc))) x.enStock = x.enStock || src.stockPropre || l.basique;
    for (std::size_t i = 0; i < pourquoi.size(); ++i) x.raison += (i ? ", " : "") + pourquoi[i];
    if (x.confiance > 0) r.push_back(std::move(x));
  }
  // Les sources classent la recherche : en stock d'abord, à confiance proche
  std::stable_sort(r.begin(), r.end(), [](const Semblable& x, const Semblable& y) {
    const double ax = x.confiance + (x.enStock ? 0.1 : 0), ay = y.confiance + (y.enStock ? 0.1 : 0);
    return ax > ay;
  });
  return r;
}

std::vector<SecondeSource> secondesClassees(const Article& a) {
  std::vector<SecondeSource> r = a.secondes;
  auto vie = [](const std::string& c) { return c == "actif" ? 0 : c == "NRND" ? 1 : 2; };
  std::stable_sort(r.begin(), r.end(), [&](const SecondeSource& x, const SecondeSource& y) { return std::pair{x.rang, vie(x.cycleVie)} < std::pair{y.rang, vie(y.cycleVie)}; });
  return r;
}

}  // namespace prom::ecad
