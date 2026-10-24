// Prométhée : nomenclature en entrée (10.6, ECH-13). Voir nomenclature_entree.hpp. Licence GPL-3.0-only.
#include "promethee/nomenclature_entree.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "promethee/articles.hpp"

namespace prom::ecad {

namespace {
std::string sansBlancs(std::string s) {
  s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char ch) { return std::isspace(ch); }), s.end());
  return s;
}
std::string garni(const std::string& s) {
  const auto a = s.find_first_not_of(" \t\r\n\"");
  if (a == std::string::npos) return {};
  const auto b = s.find_last_not_of(" \t\r\n\"");
  return s.substr(a, b - a + 1);
}
// Champ d'après l'en-tête de colonne
std::string champ(const std::string& entete) {
  const std::string n = sansBlancs(normaliserRecherche(entete));
  static const std::vector<std::pair<std::vector<std::string>, std::string>> table{
      {{"designator", "designators", "reference", "references", "repere", "reperes", "refdes", "ref", "refs", "partreferences"}, "reperes"},
      {{"value", "valeur", "comment", "commentaire", "val"}, "valeur"},
      {{"footprint", "empreinte", "pattern", "pcbfootprint", "package", "boitier"}, "empreinte"},
      {{"manufacturer", "fabricant", "mfr", "mfg", "manufacturername", "constructeur"}, "fabricant"},
      {{"mpn", "manufacturerpart", "manufacturerpartnumber", "mfrpart", "mfrpartnumber", "partnumber", "referencefabricant", "pn", "mfgpartnumber"}, "mpn"},
      {{"lcsc", "lcscpart", "lcscpartnumber", "jlcpcbpart", "jlcpcbpartnumber", "supplierpart", "lcscpart#", "codelcsc"}, "lcsc"},
      {{"description", "desc", "designation"}, "description"},
      {{"datasheet", "fiche", "fichetechnique", "lien"}, "fiche"},
      {{"dnp", "donotpopulate", "nonmonte", "nepasmonter", "notfitted", "fitted"}, "nonMonte"},
      {{"quantity", "qty", "quantite", "qte", "nombre"}, "quantite"},
      {{"supplier", "fournisseur", "distributeur"}, "fournisseur"},
  };
  for (const auto& [noms, c] : table)
    if (std::find(noms.begin(), noms.end(), n) != noms.end()) return c;
  return {};
}
bool vrai(const std::string& s) {
  const std::string n = normaliserRecherche(s);
  return n == "1" || n == "x" || n == "oui" || n == "yes" || n == "true" || n == "vrai" || n == "dnp" || n == "nonmonte" || n == "non monte";
}
}  // namespace

std::vector<std::string> developperReperes(const std::string& texte) {
  std::vector<std::string> r;
  std::string courant;
  std::vector<std::string> morceaux;
  for (char ch : texte) {
    if (ch == ',' || ch == ';' || std::isspace(static_cast<unsigned char>(ch))) {
      if (!courant.empty()) morceaux.push_back(courant);
      courant.clear();
    } else
      courant += ch;
  }
  if (!courant.empty()) morceaux.push_back(courant);
  auto decouper = [](const std::string& s, std::string& prefixe, long& n) {
    std::size_t i = s.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1]))) --i;
    if (i == s.size() || s.size() - i > 9) return false;
    prefixe = s.substr(0, i);
    n = std::stol(s.substr(i));
    return true;
  };
  for (const auto& m : morceaux) {
    const auto tiret = m.find('-', 1);
    std::string pa, pb;
    long a = 0, b = 0;
    if (tiret != std::string::npos && decouper(m.substr(0, tiret), pa, a)) {
      std::string fin = m.substr(tiret + 1);
      if (!fin.empty() && std::all_of(fin.begin(), fin.end(), [](unsigned char ch) { return std::isdigit(ch); })) fin = pa + fin;  // « R1-4 »
      if (decouper(fin, pb, b) && pa == pb && b >= a && b - a < 10000) {
        for (long i = a; i <= b; ++i) r.push_back(pa + std::to_string(i));
        continue;
      }
    }
    r.push_back(m);
  }
  return r;
}

RapportNomenclatureEntree appliquerNomenclature(ConceptionElec& c, const std::vector<std::vector<std::string>>& tableau, bool appliquer) {
  RapportNomenclatureEntree rap;
  if (tableau.empty()) return rap;
  std::vector<std::string> champs;
  int colReperes = -1;
  for (std::size_t i = 0; i < tableau.front().size(); ++i) {
    const std::string ch = champ(tableau.front()[i]);
    champs.push_back(ch);
    if (ch == "reperes" && colReperes < 0) colReperes = static_cast<int>(i);
    rap.colonnes.push_back(tableau.front()[i] + " → " + (ch.empty() ? "champ « " + garni(tableau.front()[i]) + " »" : ch));
  }
  if (colReperes < 0) {
    rap.alertes.push_back("Aucune colonne de repères (« Designator », « Référence », « Repères »…) : rien ne désigne les composants.");
    return rap;
  }
  std::map<std::string, ComposantElec*> parRepere;
  for (auto& x : c.composants) parRepere[x.reference] = &x;
  std::set<std::string> touches;
  auto ecrire = [&](ComposantElec& x, const std::string& nomChamp, std::string& cible, const std::string& v) {
    if (cible == v) return;
    rap.changements.push_back({x.reference, nomChamp, cible, v});
    if (appliquer) cible = v;
  };
  // Champ libre du composant : créé seulement à l'application
  auto ecrireChamp = [&](ComposantElec& x, const std::string& cle, const std::string& v) {
    const auto it = x.champs.find(cle);
    const std::string avant = it == x.champs.end() ? std::string() : it->second;
    if (avant == v) return;
    rap.changements.push_back({x.reference, cle, avant, v});
    if (appliquer) x.champs[cle] = v;
  };
  for (std::size_t l = 1; l < tableau.size(); ++l) {
    const auto& ligne = tableau[l];
    if (static_cast<std::size_t>(colReperes) >= ligne.size() || garni(ligne[static_cast<std::size_t>(colReperes)]).empty()) continue;
    ++rap.lignes;
    const auto reperes = developperReperes(ligne[static_cast<std::size_t>(colReperes)]);
    for (std::size_t i = 0; i < ligne.size() && i < champs.size(); ++i)
      if (champs[i] == "quantite" && !garni(ligne[i]).empty()) {
        try {
          if (std::stoi(garni(ligne[i])) != static_cast<int>(reperes.size()))
            rap.alertes.push_back("Ligne " + std::to_string(l + 1) + " : quantité " + garni(ligne[i]) + " pour " + std::to_string(reperes.size()) + " repère(s)");
        } catch (const std::exception&) {
        }
      }
    for (const auto& ref : reperes) {
      const auto it = parRepere.find(ref);
      if (it == parRepere.end()) {
        rap.inconnus.push_back(ref);
        continue;
      }
      ComposantElec& x = *it->second;
      touches.insert(ref);
      for (std::size_t i = 0; i < ligne.size() && i < champs.size(); ++i) {
        const std::string v = garni(ligne[i]);
        const std::string& ch = champs[i];
        if (ch == "reperes" || ch == "quantite") continue;
        if (ch.empty()) {
          if (v.empty()) continue;
          ecrireChamp(x, garni(tableau.front()[i]), v);
          continue;
        }
        if (v.empty() && ch != "nonMonte") continue;
        if (ch == "valeur") ecrire(x, "valeur", x.valeur, v);
        else if (ch == "fabricant") ecrire(x, "fabricant", x.fabricant, v);
        else if (ch == "mpn") ecrire(x, "mpn", x.mpn, v);
        else if (ch == "lcsc") ecrire(x, "lcsc", x.lcsc, v);
        else if (ch == "description") ecrire(x, "description", x.description, v);
        else if (ch == "fiche") ecrire(x, "fiche", x.fiche, v);
        else if (ch == "fournisseur") ecrire(x, "fournisseur", x.fournisseur, v);
        else if (ch == "nonMonte") {
          const bool dnp = normaliserRecherche(tableau.front()[i]).find("fitted") != std::string::npos ? !vrai(v) && !v.empty() : vrai(v);
          if (dnp != x.nePasMonter) {
            rap.changements.push_back({x.reference, "non monté", x.nePasMonter ? "oui" : "non", dnp ? "oui" : "non"});
            if (appliquer) x.nePasMonter = dnp;
          }
        } else if (ch == "empreinte") {
          // Empreinte connue (de la conception ou du catalogue) : posée ; sinon gardée en champ, et signalée
          const bool connue = c.empreintes.count(v) || genererEmpreinte(v).has_value();
          if (connue) ecrire(x, "empreinte", x.empreinte, v);
          else if (x.empreinte != v) {
            ecrireChamp(x, "Empreinte (nomenclature)", v);
            rap.alertes.push_back(ref + " : empreinte « " + v + " » inconnue, gardée en champ");
          }
        }
      }
    }
  }
  rap.composants = static_cast<int>(touches.size());
  return rap;
}

}  // namespace prom::ecad
