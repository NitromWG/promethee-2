// Prométhée : liste d'achat par conditionnements et tableau de bord de la nomenclature (lot L19 de la 0.34 ; 19.7).
// Voir liste_achat.hpp. Licence GPL-3.0-only.
#include "promethee/liste_achat.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

#include "promethee/articles.hpp"
#include "promethee/hachage.hpp"

namespace prom::ecad {

using Json = nlohmann::ordered_json;

namespace {
std::string euros(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.2f", v);
  std::string s = b;
  std::replace(s.begin(), s.end(), '.', ',');
  return s + " €";
}
std::string echapper(const std::string& s) {
  std::string r;
  for (char c : s) r += c == '<' ? std::string("&lt;") : c == '>' ? std::string("&gt;") : c == '&' ? std::string("&amp;") : std::string(1, c);
  return r;
}
double prixA(const std::vector<std::pair<int, double>>& prix, int quantite) {
  double p = -1;
  int seuil = -1;
  for (const auto& [q, v] : prix)
    if (q <= quantite && q > seuil) seuil = q, p = v;
  if (p < 0 && !prix.empty()) p = std::min_element(prix.begin(), prix.end())->second;  // sous le premier palier : le premier
  return p;
}
bool utilisable(const SourceAchat& s) { return s.cycleVie != "obsolète" && s.cycleVie != "obsolete"; }
}  // namespace

std::optional<FraisAchat> lireFraisAchat(const std::string& json, std::string* erreur) {
  try {
    const Json j = Json::parse(json);
    FraisAchat d;
    d.livraison = j.value("livraison", std::string("FR"));
    if (j.contains("pays") && j["pays"].is_object())
      for (const auto& [pays, v] : j["pays"].items()) d.pays[pays] = {v.value("port", 0.0), v.value("franco", 0.0), v.value("douane", 0.0)};
    if (j.contains("fournisseurs") && j["fournisseurs"].is_object())
      for (const auto& [f, v] : j["fournisseurs"].items())
        if (v.is_string()) d.fournisseurs[f] = v.get<std::string>();
    return d;
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
}

std::vector<LigneAchat> lignesAchat(const std::vector<std::pair<const ConceptionElec*, int>>& projets, double pertes, const FraisAchat* donnees) {
  // Un besoin par carte de chaque projet : la ligne cumulée porte le besoin total, cartes comprises (cartes = 1)
  struct Cumul {
    LigneAchat l;
    double pieces = 0;
  };
  std::map<std::string, Cumul> parCle;
  std::vector<std::string> ordre;
  for (const auto& [c, cartes] : projets) {
    if (!c) continue;
    for (const auto& k : c->composants) {
      if (k.nePasMonter || k.horsNomenclature) continue;
      const std::string idArticle = !k.article.empty() ? k.article : k.symbole.rfind("art:", 0) == 0 ? k.symbole.substr(4) : std::string();
      const Article* a = nullptr;
      if (const auto it = c->articles.find(idArticle); !idArticle.empty() && it != c->articles.end()) a = &it->second;
      const std::string mpn = !k.mpn.empty() ? k.mpn : a ? a->mpn : std::string();
      if (mpn.empty() && !a) continue;  // pièce sans référence : rien à acheter
      const std::string cle = mpn.empty() ? idArticle : mpn;
      auto [it, nouveau] = parCle.try_emplace(cle);
      if (nouveau) {
        ordre.push_back(cle);
        it->second.l.cle = cle;
        it->second.l.designation = k.valeur + (k.empreinte.empty() ? std::string() : " " + k.empreinte);
        it->second.l.pertes = pertes;
        it->second.l.cartes = 1;
        if (a)
          for (const auto& o : a->offres) {
            SourceAchat s;
            s.fournisseur = o.distributeur;
            s.reference = o.reference;
            s.stock = o.stock;
            if (donnees)
              if (const auto p = donnees->fournisseurs.find(o.distributeur); p != donnees->fournisseurs.end()) s.pays = p->second;
            Conditionnement kd;
            kd.genre = o.conditionnement > 1 ? "bobine" : "bande coupée";
            kd.pieces = std::max(1, o.conditionnement);
            kd.prix = o.prix;
            s.conditionnements.push_back(kd);
            const std::string cycle = a->etatCycle;
            if (cycle == "obsolète" || cycle == "obsolete") s.cycleVie = "obsolète";
            it->second.l.sources.push_back(s);
          }
      }
      it->second.pieces += cartes;
    }
  }
  std::vector<LigneAchat> r;
  for (const auto& cle : ordre) {
    Cumul& x = parCle[cle];
    x.l.parCarte = static_cast<int>(std::lround(x.pieces));
    r.push_back(x.l);
  }
  return r;
}

int besoinLigne(const LigneAchat& l, int cartes) {
  const double n = static_cast<double>(l.parCarte) * (l.cartes > 0 ? l.cartes : cartes) * (1 + std::max(0.0, l.pertes));
  return static_cast<int>(std::ceil(n - 1e-9));
}

std::optional<ChoixLigne> coutConditionnement(const Conditionnement& k, int besoin) {
  if (besoin <= 0 || k.prix.empty()) return std::nullopt;
  ChoixLigne c;
  c.besoin = besoin;
  c.conditionnement = k.genre;
  int q = std::max(besoin, std::max(1, k.minimum));
  const int pas = std::max(1, k.multiple);
  q = ((q + pas - 1) / pas) * pas;
  const int par = std::max(1, k.pieces);
  c.lots = (q + par - 1) / par;
  c.quantite = c.lots * par;
  c.prixUnitaire = prixA(k.prix, c.quantite);
  c.cout = c.quantite * c.prixUnitaire;
  c.raison = std::to_string(c.quantite) + " pièce(s) en " + k.genre + (c.lots > 1 && par > 1 ? " (" + std::to_string(c.lots) + " × " + std::to_string(par) + ")" : std::string()) + " à " + std::to_string(c.prixUnitaire) + " €";
  return c;
}

namespace {
// Meilleur choix d'une ligne parmi les fournisseurs permis (vide : tous)
std::optional<ChoixLigne> meilleurChoix(const LigneAchat& l, int besoin, const std::set<std::string>* permis, bool repli) {
  std::optional<ChoixLigne> r;
  for (int passe = 0; passe < (repli ? 2 : 1) && !r; ++passe)  // seconde passe : les sources obsolètes ou sans stock, faute de mieux
    for (const auto& s : l.sources) {
      if (permis && !permis->count(s.fournisseur)) continue;
      if (passe == 0 && !utilisable(s)) continue;
      for (const auto& k : s.conditionnements) {
        auto c = coutConditionnement(k, besoin);
        if (!c) continue;
        if (passe == 0 && s.stock >= 0 && s.stock < c->quantite) continue;
        c->cle = l.cle;
        c->designation = l.designation;
        c->fournisseur = s.fournisseur;
        c->reference = s.reference;
        if (passe == 1) c->raison += " (faute de mieux : " + std::string(utilisable(s) ? "stock insuffisant" : "source obsolète") + ")";
        if (!r || c->cout < r->cout - 1e-12 || (std::abs(c->cout - r->cout) < 1e-12 && c->quantite < r->quantite)) r = c;
      }
    }
  return r;
}
struct Total {
  double sousTotal = 0, port = 0, douane = 0;
  std::vector<ChoixLigne> lignes;
  std::set<std::string> fournisseurs;
  bool complet = true;
};
Total evaluer(const std::vector<LigneAchat>& lignes, const OptionsAchat& o, const std::set<std::string>* permis, bool repli) {
  Total t;
  std::map<std::string, double> parFournisseur;
  std::map<std::string, std::string> pays;
  for (const auto& l : lignes)
    for (const auto& s : l.sources) pays[s.fournisseur] = s.pays;
  for (const auto& [f, p] : o.donnees.fournisseurs)
    if (pays.count(f)) pays[f] = p;
  for (const auto& l : lignes) {
    const auto c = meilleurChoix(l, besoinLigne(l, o.cartes), permis, repli);
    if (!c) {
      t.complet = false;
      continue;
    }
    t.sousTotal += c->cout;
    parFournisseur[c->fournisseur] += c->cout;
    t.fournisseurs.insert(c->fournisseur);
    t.lignes.push_back(*c);
  }
  for (const auto& [f, montant] : parFournisseur) {
    const auto it = o.donnees.pays.find(pays[f]);
    const FraisPays fp = it == o.donnees.pays.end() ? FraisPays{} : it->second;
    if (!(fp.franco > 0 && montant >= fp.franco)) t.port += fp.port;
    if (pays[f] != o.donnees.livraison) t.douane += montant * fp.douane;
  }
  return t;
}
}  // namespace

ListeAchat calculerListeAchat(const std::vector<LigneAchat>& lignes, const OptionsAchat& o) {
  ListeAchat r;
  std::vector<std::string> fournisseurs;
  for (const auto& l : lignes)
    for (const auto& s : l.sources)
      if (std::find(fournisseurs.begin(), fournisseurs.end(), s.fournisseur) == fournisseurs.end()) fournisseurs.push_back(s.fournisseur);
  Total meilleur;
  bool trouve = false;
  if (static_cast<int>(fournisseurs.size()) <= o.fournisseursMax) {
    // Programme en nombres entiers résolu exactement : chaque ensemble de fournisseurs, chaque ligne au mieux dans
    // l'ensemble, port et douane comptés par fournisseur retenu
    r.methode = "exacte (ensembles de fournisseurs)";
    const std::size_t n = fournisseurs.size();
    // Sources en stock et actives d'abord ; faute d'ensemble complet, les autres en repli
    for (int repli = 0; repli < 2 && !trouve; ++repli)
      for (std::size_t masque = 1; masque < (std::size_t{1} << n); ++masque) {
        std::set<std::string> permis;
        for (std::size_t i = 0; i < n; ++i)
          if (masque & (std::size_t{1} << i)) permis.insert(fournisseurs[i]);
        Total t = evaluer(lignes, o, &permis, repli == 1);
        if (!t.complet) continue;
        const double total = t.sousTotal + t.port + t.douane, record = meilleur.sousTotal + meilleur.port + meilleur.douane;
        // À total égal, le moins de fournisseurs
        if (!trouve || total < record - 1e-9 || (std::abs(total - record) <= 1e-9 && t.fournisseurs.size() < meilleur.fournisseurs.size())) meilleur = std::move(t), trouve = true;
      }
  }
  if (!trouve) {
    r.methode = "ligne par ligne";
    meilleur = evaluer(lignes, o, nullptr, true);
  }
  r.lignes = meilleur.lignes;
  r.fournisseurs.assign(meilleur.fournisseurs.begin(), meilleur.fournisseurs.end());
  r.sousTotal = meilleur.sousTotal;
  r.port = meilleur.port;
  r.douane = meilleur.douane;
  r.total = r.sousTotal + r.port + r.douane;
  for (const auto& l : lignes)
    if (std::none_of(r.lignes.begin(), r.lignes.end(), [&](const ChoixLigne& c) { return c.cle == l.cle; })) r.alertes.push_back(l.cle + " : aucune source ne la couvre");
  for (const auto& c : r.lignes)
    if (c.raison.find("faute de mieux") != std::string::npos) r.alertes.push_back(c.cle + " : " + c.raison);
  return r;
}

std::string csvListeAchat(const ListeAchat& l) {
  std::ostringstream o;
  o << "Article;Fournisseur;Conditionnement;Besoin;Quantité;Lots;Prix unitaire;Montant\n";
  for (const auto& c : l.lignes) {
    std::string pu = std::to_string(c.prixUnitaire), m = std::to_string(c.cout);
    std::replace(pu.begin(), pu.end(), '.', ',');
    std::replace(m.begin(), m.end(), '.', ',');
    o << c.cle << ";" << c.fournisseur << ";" << c.conditionnement << ";" << c.besoin << ";" << c.quantite << ";" << c.lots << ";" << pu << ";" << m << "\n";
  }
  o << "Port;;;;;;;" << euros(l.port) << "\nDouane;;;;;;;" << euros(l.douane) << "\nTotal;;;;;;;" << euros(l.total) << "\n";
  return o.str();
}

namespace {
std::string sansCasse(const std::string& s) {
  std::string r;
  for (unsigned char c : s)
    if (std::isalnum(c)) r += static_cast<char>(std::tolower(c));
  return r;
}
std::string cellule(std::string v, char sep) {
  if (v.find(sep) != std::string::npos || v.find('"') != std::string::npos) {
    std::string r = "\"";
    for (char c : v) r += c == '"' ? std::string("\"\"") : std::string(1, c);
    return r + "\"";
  }
  return v;
}
}  // namespace

const std::vector<GabaritPanier>& gabaritsPanier() {
  // Colonnes reconnues par l'outil d'envoi de nomenclature de chaque distributeur ; le gabarit se modifie en fichier
  static const std::vector<GabaritPanier> g = {
      {"Mouser", ',', {{"Mouser Part Number", "reference"}, {"Manufacturer Part Number", "mpn"}, {"Quantity", "quantite"}, {"Customer Part Number", "designation"}}},
      {"Digi-Key", ',', {{"Digi-Key Part Number", "reference"}, {"Manufacturer Part Number", "mpn"}, {"Quantity", "quantite"}, {"Customer Reference", "designation"}}},
      {"LCSC", ',', {{"LCSC Part Number", "reference"}, {"Manufacture Part Number", "mpn"}, {"Quantity", "quantite"}, {"Comment", "designation"}}}};
  return g;
}

std::optional<GabaritPanier> lireGabaritPanier(const std::string& json, std::string* erreur) {
  try {
    const Json j = Json::parse(json);
    GabaritPanier g;
    g.distributeur = j.value("distributeur", std::string());
    const std::string sep = j.value("separateur", std::string(","));
    g.separateur = sep.empty() ? ',' : sep[0];
    for (const auto& c : j.at("colonnes")) g.colonnes.push_back({c.at("entete").get<std::string>(), c.at("champ").get<std::string>()});
    if (g.distributeur.empty() || g.colonnes.empty()) throw std::runtime_error("distributeur ou colonnes manquants");
    return g;
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
}

std::string texteGabaritPanier(const GabaritPanier& g) {
  Json j = {{"format", "promethee-gabarit-panier"}, {"distributeur", g.distributeur}, {"separateur", std::string(1, g.separateur)}, {"colonnes", Json::array()}};
  for (const auto& [e, c] : g.colonnes) j["colonnes"].push_back({{"entete", e}, {"champ", c}});
  return j.dump(2) + "\n";
}

std::string panierDistributeur(const ListeAchat& l, const GabaritPanier& g) {
  std::ostringstream o;
  for (std::size_t i = 0; i < g.colonnes.size(); ++i) o << (i ? std::string(1, g.separateur) : std::string()) << cellule(g.colonnes[i].first, g.separateur);
  o << "\n";
  for (const auto& c : l.lignes) {
    if (sansCasse(c.fournisseur) != sansCasse(g.distributeur)) continue;
    for (std::size_t i = 0; i < g.colonnes.size(); ++i) {
      const std::string& champ = g.colonnes[i].second;
      const std::string v = champ == "reference" ? c.reference : champ == "mpn" || champ == "cle" ? c.cle : champ == "quantite" ? std::to_string(c.quantite) : champ == "designation" ? c.designation : std::string();
      o << (i ? std::string(1, g.separateur) : std::string()) << cellule(v, g.separateur);
    }
    o << "\n";
  }
  return o.str();
}

// ======================================================================================================================
// Tableau de bord
// ======================================================================================================================
TableauBordNomenclature tableauBordNomenclature(const ConceptionElec& c, const TableauBordNomenclature* avant) {
  TableauBordNomenclature t;
  std::map<std::string, const NoteLigneNomenclature*> anciennes;
  if (avant)
    for (const auto& l : avant->lignes) anciennes[l.cle] = &l;
  for (const auto& l : nomenclature(c)) {
    NoteLigneNomenclature n;
    n.cle = l.mpn.empty() ? l.valeur + " " + l.empreinte : l.mpn;
    n.designation = l.valeur + (l.description.empty() ? std::string() : " — " + l.description);
    n.quantite = l.quantite;
    // L'article de la ligne, s'il y en a un : prix, stock, cycle de vie, équivalents
    const Article* a = nullptr;
    for (const auto& ref : l.references)
      if (const ComposantElec* x = c.parReference(ref))
        if (const auto it = c.articles.find(x->article.empty() && x->symbole.rfind("art:", 0) == 0 ? x->symbole.substr(4) : x->article); it != c.articles.end()) {
          a = &it->second;
          break;
        }
    std::ostringstream sig;
    sig << l.quantite << "|" << l.valeur << "|" << l.empreinte << "|" << l.fabricant << "|" << l.mpn << "|" << l.lcsc << "|" << l.fournisseur;
    if (a) sig << "|" << articleVersJson(*a).dump();
    for (const auto& ref : l.references)
      if (const ComposantElec* x = c.parReference(ref))
        for (const auto& [cle, v] : x->parametres) sig << "|" << ref << "." << cle << "=" << v;
    n.empreinte = sha256Hex(sig.str());
    if (const auto it = anciennes.find(n.cle); it != anciennes.end() && it->second->empreinte == n.empreinte) {
      t.lignes.push_back(*it->second);  // ligne inchangée : reprise telle quelle
      continue;
    }
    ++t.recalculees;
    // Complétude : référence fabricant, fabricant, empreinte, valeur, fournisseur
    n.completude = (l.mpn.empty() ? 0 : 3) + (l.fabricant.empty() ? 0 : 2) + (l.empreinte.empty() ? 0 : 2) + (l.valeur.empty() ? 0 : 1) + ((l.fournisseur.empty() && l.lcsc.empty()) ? 0 : 2);
    if (l.mpn.empty()) n.remarques.push_back("référence fabricant manquante");
    // Coût et délai : d'après les offres de l'article
    int stock = -1;
    if (a && !a->offres.empty()) {
      double p = -1;
      for (const auto& o : a->offres) {
        const double x = prixA(o.prix, l.quantite);
        if (x >= 0 && (p < 0 || x < p)) p = x;
        stock = std::max(stock, o.stock);
      }
      n.prix = p;
    }
    n.cout = n.prix >= 0 ? 10 : 0;
    if (n.prix < 0) n.remarques.push_back("prix inconnu");
    n.delai = stock < 0 ? 5 : stock >= l.quantite ? 10 : 2;
    if (stock >= 0 && stock < l.quantite) n.remarques.push_back("stock insuffisant chez les sources connues");
    // Risque : cycle de vie et équivalents
    const std::string cycle = a ? a->etatCycle : std::string();
    double r = cycle == "obsolète" ? 0 : cycle == "fin de série" ? 3 : cycle == "pré-série" ? 6 : 8;
    if (a && !a->secondes.empty()) r = std::min(10.0, r + 2);
    else n.remarques.push_back("aucun équivalent déclaré");
    // États RoHS et REACH (17.10) : paramètres du composant ou de son article
    for (const char* exigence : {"rohs", "reach"}) {
      std::string etat;
      for (const auto& ref : l.references)
        if (const ComposantElec* x = c.parReference(ref)) {
          if (const auto it = x->parametres.find(exigence); it != x->parametres.end()) etat = it->second;
          else if (a)
            if (const auto jt = a->parametres.find(exigence); jt != a->parametres.end()) etat = jt->second;
          break;
        }
      const std::string nom = std::string(exigence) == "rohs" ? "RoHS" : "REACH";
      if (etat == "non") {
        r = std::max(0.0, r - 5);
        n.remarques.push_back(nom + " non conforme");
      } else if (etat != "oui") {
        r = std::max(0.0, r - 1);
        n.remarques.push_back("état " + nom + " inconnu");
      }
    }
    n.risque = r;
    if (a && !a->secondes.empty() && n.prix > 0) n.remarques.push_back("économie possible : comparer les " + std::to_string(a->secondes.size()) + " équivalent(s)");
    t.lignes.push_back(n);
  }
  double somme = 0;
  for (const auto& l : t.lignes) {
    somme += 0.4 * l.completude + 0.2 * l.cout + 0.2 * l.delai + 0.2 * l.risque;
    if (l.prix >= 0) t.coutTotal += l.prix * l.quantite;
    t.lignesCompletes += l.completude >= 10;
  }
  t.note = t.lignes.empty() ? 0 : somme / static_cast<double>(t.lignes.size());
  return t;
}

std::string htmlTableauBord(const TableauBordNomenclature& t, const std::string& titre) {
  std::ostringstream o;
  char note[16];
  std::snprintf(note, sizeof note, "%.1f", t.note);
  std::string n = note;
  std::replace(n.begin(), n.end(), '.', ',');
  o << "<!doctype html>\n<html lang=\"fr\"><head><meta charset=\"utf-8\"><title>" << echapper(titre) << " — nomenclature</title>\n<style>body{font-family:sans-serif;margin:16px}"
       "table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:4px 8px;text-align:left}th{background:#eee}.note{font-size:2em;font-weight:bold}</style></head><body>\n<h1>"
    << echapper(titre) << "</h1>\n<p class=\"note\">Note de la nomenclature : " << n << " / 10</p>\n<p>" << t.lignes.size() << " ligne(s), " << t.lignesCompletes << " complète(s), coût connu : " << euros(t.coutTotal)
    << "</p>\n";
  // Graphique en SVG : note de chaque ligne, en barres
  const double h = 22.0 * static_cast<double>(t.lignes.size()) + 30;
  o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"" << h << "\" font-size=\"11\">\n";
  for (std::size_t i = 0; i < t.lignes.size(); ++i) {
    const auto& l = t.lignes[i];
    const double v = 0.4 * l.completude + 0.2 * l.cout + 0.2 * l.delai + 0.2 * l.risque;
    const double y = 10 + 22.0 * static_cast<double>(i);
    o << "<text x=\"0\" y=\"" << y + 13 << "\">" << echapper(l.cle.substr(0, 28)) << "</text><rect x=\"200\" y=\"" << y << "\" width=\"" << v * 40 << "\" height=\"16\" fill=\"" << (v >= 7 ? "#2E7D32" : v >= 4 ? "#F9A825" : "#C62828")
      << "\"/>\n";
  }
  o << "</svg>\n<table><tr><th>Ligne</th><th>Quantité</th><th>Complétude</th><th>Coût</th><th>Délai</th><th>Risque</th><th>Remarques</th></tr>\n";
  for (const auto& l : t.lignes) {
    std::string rq;
    for (const auto& x : l.remarques) rq += (rq.empty() ? "" : " ; ") + x;
    o << "<tr><td>" << echapper(l.cle) << "</td><td>" << l.quantite << "</td><td>" << l.completude << "</td><td>" << l.cout << "</td><td>" << l.delai << "</td><td>" << l.risque << "</td><td>" << echapper(rq) << "</td></tr>\n";
  }
  o << "</table>\n</body></html>\n";
  return o.str();
}

}  // namespace prom::ecad
