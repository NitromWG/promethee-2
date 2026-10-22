// Prométhée : comparaison et fusion de conceptions (lot L12). Licence GPL-3.0-only.
#include "promethee/comparaison.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace prom::ecad {

namespace {
using J = nlohmann::ordered_json;

// Clé d'identité d'un tableau d'objets : la première de ces clés présente, texte et unique dans chaque élément
std::string cleIdentite(const J& a, const J& b = J()) {
  for (const char* k : {"uuid", "id", "composant", "nom"}) {
    bool ok = true, vu = false;
    for (const J* t : {&a, &b}) {
      if (!t->is_array()) continue;
      std::set<std::string> ids;
      for (const auto& x : *t) {
        if (!x.is_object() || !x.contains(k) || !x[k].is_string() || !ids.insert(x[k].get<std::string>()).second) {
          ok = false;
          break;
        }
        vu = true;
      }
      if (!ok) break;
    }
    if (ok && vu) return k;
  }
  return {};
}
// Tableaux d'ensembles (points de jonction, croix de non-connexion) : l'ordre n'a pas de sens
bool ensemble(const std::string& nom) { return nom == "jonctions" || nom == "nonConnexions"; }
std::string dernierNom(const std::string& chemin) {
  std::size_t fin = chemin.size();
  if (!chemin.empty() && chemin.back() == ']') fin = chemin.rfind('[');
  const std::size_t debut = chemin.find_last_of('.', fin == std::string::npos ? std::string::npos : fin - 1);
  return chemin.substr(debut == std::string::npos ? 0 : debut + 1, fin - (debut == std::string::npos ? 0 : debut + 1));
}
std::string compact(const J& v) {
  if (v.is_null()) return "(absent)";
  if (v.is_string()) return v.get<std::string>();
  std::string s = v.dump();
  if (s.size() > 60) s = s.substr(0, 57) + "…";
  return s;
}

// ---------- Catégories lisibles ----------
struct Lecteur {
  std::map<std::string, std::string> references;  // UUID de composant → repère
  void lire(const J& o) {
    if (o.is_object() && o.contains("composants") && o["composants"].is_array())
      for (const auto& k : o["composants"])
        if (k.is_object() && k.contains("uuid") && k["uuid"].is_string()) references[k["uuid"].get<std::string>()] = k.value("ref", std::string());
  }
  std::string ref(const std::string& uuid) const {
    const auto it = references.find(uuid);
    return it == references.end() || it->second.empty() ? uuid.substr(0, 8) : it->second;
  }
  // Catégorie d'un élément de tableau, d'après le nom du tableau et son parent
  static std::string categorie(const std::string& chemin, const std::string& tableau) {
    const bool circuit = chemin.rfind("circuit.", 0) == 0;
    if (tableau == "composants") return "composant";
    if (tableau == "feuilles") return "feuille";
    if (tableau == "symboles") return chemin.rfind("feuilles", 0) == 0 ? "symbole posé" : "définition de symbole";
    if (tableau == "fils") return "fil";
    if (tableau == "etiquettes") return "étiquette";
    if (tableau == "textes") return circuit ? "texte de carte" : "texte du schéma";
    if (tableau == "empreintes") return "empreinte placée";
    if (tableau == "pistes") return "piste";
    if (tableau == "vias") return "via";
    if (tableau == "zones") return "zone";
    if (tableau == "dessins") return "dessin de carte";
    if (tableau == "articles") return "article";
    if (tableau == "publications") return "publication";
    if (tableau == "notes") return "note";
    if (tableau == "variantesMontage") return "variante";
    if (tableau == "jeuxSorties") return "jeu de sorties";
    if (tableau == "facades") return "façade";
    if (tableau == "modeles3D") return "modèle 3D";
    if (tableau == "bibliothequeEmpreintes") return "définition d'empreinte";
    return tableau;
  }
  std::string repere(const std::string& categorie, const J& o) const {
    if (!o.is_object()) return {};
    if (categorie == "composant") return o.value("ref", std::string());
    if ((categorie == "symbole posé" || categorie == "empreinte placée") && o.contains("composant") && o["composant"].is_string()) return ref(o["composant"].get<std::string>());
    if (categorie == "symbole posé" && o.contains("alim")) return o.value("alim", std::string());
    if (categorie == "piste" || categorie == "via" || categorie == "zone") return o.value("reseau", std::string());
    if (categorie == "étiquette" || categorie == "texte du schéma" || categorie == "texte de carte") return o.value("texte", std::string());
    for (const char* k : {"nom", "id", "ref"})
      if (o.contains(k) && o[k].is_string()) return o[k].get<std::string>();
    return {};
  }
};

// ---------- Différences ----------
struct Brut {
  std::string chemin;        // de la feuille modifiée ou de l'élément ajouté ou retiré
  std::string objet;         // chemin de l'élément de tableau qui la contient (vide : la conception)
  std::string tableau;       // nom de ce tableau
  std::string genre;         // « ajouté », « retiré », « modifié »
  const J* avant = nullptr;  // l'élément (ajouté, retiré) ou la valeur
  const J* apres = nullptr;
  const J* elementAvant = nullptr;
  const J* elementApres = nullptr;
};

void differer(const J& a, const J& b, const std::string& chemin, const std::string& objet, const std::string& tableau, const J* elA, const J* elB, std::vector<Brut>& out) {
  if (a == b) return;
  if (a.is_object() && b.is_object()) {
    std::set<std::string> cles;
    for (auto it = a.begin(); it != a.end(); ++it) cles.insert(it.key());
    for (auto it = b.begin(); it != b.end(); ++it) cles.insert(it.key());
    static const J nul;
    for (const auto& k : cles) {
      const J& x = a.contains(k) ? a[k] : nul;
      const J& y = b.contains(k) ? b[k] : nul;
      differer(x, y, chemin.empty() ? k : chemin + "." + k, objet, tableau, elA, elB, out);
    }
    return;
  }
  if (a.is_array() && b.is_array()) {
    const std::string nom = dernierNom(chemin);
    if (const std::string cle = cleIdentite(a, b); !cle.empty()) {
      std::map<std::string, const J*> ia, ib;
      for (const auto& x : a) ia[x[cle].get<std::string>()] = &x;
      for (const auto& x : b) ib[x[cle].get<std::string>()] = &x;
      for (const auto& [id, x] : ia) {
        const std::string c = chemin + "[" + cle + "=" + id + "]";
        if (const auto it = ib.find(id); it == ib.end()) out.push_back({c, c, nom, "retiré", x, nullptr, x, nullptr});
        else differer(*x, *it->second, c, c, nom, x, it->second, out);
      }
      for (const auto& [id, y] : ib)
        if (!ia.count(id)) {
          const std::string c = chemin + "[" + cle + "=" + id + "]";
          out.push_back({c, c, nom, "ajouté", nullptr, y, nullptr, y});
        }
      return;
    }
    if (ensemble(nom)) {
      std::set<std::string> sa, sb;
      for (const auto& x : a) sa.insert(x.dump());
      for (const auto& x : b) sb.insert(x.dump());
      for (const auto& x : a)
        if (!sb.count(x.dump())) out.push_back({chemin, chemin + "[" + x.dump() + "]", nom, "retiré", &x, nullptr, &x, nullptr});
      for (const auto& x : b)
        if (!sa.count(x.dump())) out.push_back({chemin, chemin + "[" + x.dump() + "]", nom, "ajouté", nullptr, &x, nullptr, &x});
      return;
    }
  }
  out.push_back({chemin, objet, tableau, "modifié", &a, &b, elA, elB});
}

ConceptionElec relire(const J& o) { return conceptionDepuisJson(o); }
}  // namespace

ComparaisonConceptions comparerConceptions(const ConceptionElec& ca, const ConceptionElec& cb) {
  const J a = versJson(ca), b = versJson(cb);
  std::vector<Brut> bruts;
  differer(a, b, "", "", "", nullptr, nullptr, bruts);
  Lecteur lect;
  lect.lire(a);
  lect.lire(b);
  ComparaisonConceptions r;
  // Une différence par objet : les champs modifiés d'un même élément se réunissent
  std::map<std::string, std::size_t> parObjet;
  for (const Brut& x : bruts) {
    if (x.chemin.find(".valeurLue") != std::string::npos) continue;  // tirée de la valeur, comptée avec elle
    std::string cat, rep;
    if (!x.objet.empty() && !x.tableau.empty()) {
      cat = Lecteur::categorie(x.objet, x.tableau);
      rep = lect.repere(cat, x.elementApres ? *x.elementApres : x.elementAvant ? *x.elementAvant : J());
    } else {
      // Réglage de la conception : « circuit.regles.… » → « règles », « cartouche.… » → « cartouche »
      const std::string premier = x.chemin.substr(0, x.chemin.find('.'));
      if (premier == "circuit") {
        const std::string second = x.chemin.size() > 8 ? x.chemin.substr(8, x.chemin.find('.', 8) - 8) : std::string();
        cat = second == "regles" ? "règles" : second == "contour" ? "contour" : second == "empilage" ? "empilage" : second.empty() ? "circuit" : second;
      } else
        cat = premier;
    }
    if (x.genre != "modifié") {
      r.differences.push_back({cat, x.genre, x.chemin, rep, {}});
      continue;
    }
    const std::string propriete = x.chemin.substr(x.objet.size() + (x.objet.empty() ? 0 : 1));
    const std::string ligne = (propriete.empty() ? std::string("valeur") : propriete) + " : " + compact(*x.avant) + " → " + compact(*x.apres);
    const std::string cle = x.objet.empty() ? cat : x.objet;
    if (const auto it = parObjet.find(cle); it != parObjet.end()) {
      r.differences[it->second].detail += " ; " + ligne;
      continue;
    }
    parObjet[cle] = r.differences.size();
    r.differences.push_back({cat, "modifié", x.objet.empty() ? x.chemin : x.objet, rep, ligne});
  }
  for (const auto& d : r.differences) r.parCategorie[d.categorie]++;
  return r;
}

// ---------- Alignement de deux fichiers sans ancêtre commun ----------
ConceptionElec aligner(const ConceptionElec& ref, const ConceptionElec& autre, Nm tol, int* apparies) {
  std::map<std::string, std::string> renommer;  // UUID de « autre » → UUID de « reference »
  const auto proche = [tol](Pt p, Pt q) { return std::llabs(p.x - q.x) <= tol && std::llabs(p.y - q.y) <= tol; };
  const auto lier = [&](const std::string& de, const std::string& vers) {
    if (!de.empty() && !vers.empty() && de != vers) renommer[de] = vers;
  };
  // Composants par repère
  std::map<std::string, const ComposantElec*> parRef;
  for (const auto& k : ref.composants)
    if (!k.reference.empty()) parRef[k.reference] = &k;
  std::map<std::string, std::string> composant;  // UUID autre → UUID référence
  int n = 0;
  for (const auto& k : autre.composants)
    if (const auto it = parRef.find(k.reference); it != parRef.end()) {
      composant[k.uuid] = it->second->uuid;
      lier(k.uuid, it->second->uuid);
      ++n;
    }
  const auto composantRef = [&](const std::string& u) {
    const auto it = composant.find(u);
    return it == composant.end() ? u : it->second;
  };
  // Feuilles par nom, sinon par rang
  for (std::size_t i = 0; i < autre.feuilles.size(); ++i) {
    const Feuille& f = autre.feuilles[i];
    const Feuille* g = nullptr;
    for (const auto& x : ref.feuilles)
      if (x.nom == f.nom) g = &x;
    if (!g && i < ref.feuilles.size()) g = &ref.feuilles[i];
    if (!g) continue;
    lier(f.uuid, g->uuid);
    ++n;
    std::set<std::string> pris;
    for (const auto& s : f.symboles)
      for (const auto& t : g->symboles)
        if (!pris.count(t.uuid) && composantRef(s.composant) == t.composant && s.unite == t.unite && s.nomAlim == t.nomAlim &&
            (!s.composant.empty() || proche(s.position, t.position))) {
          lier(s.uuid, t.uuid);
          pris.insert(t.uuid);
          ++n;
          break;
        }
    for (const auto& w : f.fils)
      for (const auto& v : g->fils)
        if (!pris.count(v.uuid) && ((proche(w.a, v.a) && proche(w.b, v.b)) || (proche(w.a, v.b) && proche(w.b, v.a)))) {
          lier(w.uuid, v.uuid);
          pris.insert(v.uuid);
          ++n;
          break;
        }
    for (const auto& e : f.etiquettes)
      for (const auto& h : g->etiquettes)
        if (!pris.count(h.uuid) && e.texte == h.texte && proche(e.p, h.p)) {
          lier(e.uuid, h.uuid);
          pris.insert(h.uuid);
          ++n;
          break;
        }
    for (const auto& t : f.textes)
      for (const auto& u : g->textes)
        if (!pris.count(u.uuid) && t.texte == u.texte && proche(t.p, u.p)) {
          lier(t.uuid, u.uuid);
          pris.insert(u.uuid);
          ++n;
          break;
        }
  }
  // Cuivre par position et couche
  std::set<std::string> pris;
  for (const auto& t : autre.circuit.pistes)
    for (const auto& u : ref.circuit.pistes)
      if (!pris.count(u.uuid) && t.couche == u.couche && ((proche(t.a, u.a) && proche(t.b, u.b)) || (proche(t.a, u.b) && proche(t.b, u.a)))) {
        lier(t.uuid, u.uuid);
        pris.insert(u.uuid);
        ++n;
        break;
      }
  for (const auto& v : autre.circuit.vias)
    for (const auto& w : ref.circuit.vias)
      if (!pris.count(w.uuid) && proche(v.p, w.p)) {
        lier(v.uuid, w.uuid);
        pris.insert(w.uuid);
        ++n;
        break;
      }
  for (const auto& t : autre.circuit.textes)
    for (const auto& u : ref.circuit.textes)
      if (!pris.count(u.uuid) && t.texte == u.texte && t.couche == u.couche && proche(t.p, u.p)) {
        lier(t.uuid, u.uuid);
        pris.insert(u.uuid);
        ++n;
        break;
      }
  for (const auto& d : autre.circuit.dessins)
    for (const auto& e : ref.circuit.dessins)
      if (!pris.count(e.uuid) && d.forme == e.forme && d.couche == e.couche && d.points.size() == e.points.size() &&
          std::equal(d.points.begin(), d.points.end(), e.points.begin(), proche)) {
        lier(d.uuid, e.uuid);
        pris.insert(e.uuid);
        ++n;
        break;
      }
  for (const auto& z : autre.circuit.zones)
    for (const auto& y : ref.circuit.zones)
      if (!pris.count(y.uuid) && z.couche == y.couche && z.reseau == y.reseau) {
        lier(z.uuid, y.uuid);
        pris.insert(y.uuid);
        ++n;
        break;
      }
  if (apparies) *apparies = n;
  if (renommer.empty()) return autre;
  // Les UUID sont des textes uniques : les remplacer dans le fichier les remplace partout où ils sont cités
  std::function<void(J&)> remplacer = [&](J& v) {
    if (v.is_string()) {
      if (const auto it = renommer.find(v.get<std::string>()); it != renommer.end()) v = it->second;
    } else if (v.is_array())
      for (auto& x : v) remplacer(x);
    else if (v.is_object()) {
      // Les clés aussi : « UUID:broche » des noms de réseaux figés
      J r = J::object();
      for (auto it = v.begin(); it != v.end(); ++it) {
        std::string cle = it.key();
        const std::size_t deux = cle.find(':');
        if (const auto x = renommer.find(cle.substr(0, deux)); x != renommer.end()) cle = x->second + (deux == std::string::npos ? std::string() : cle.substr(deux));
        J valeur = it.value();
        remplacer(valeur);
        r[cle] = std::move(valeur);
      }
      v = std::move(r);
    }
  };
  J o = versJson(autre);
  remplacer(o);
  if (o.contains("uuid")) o["uuid"] = ref.uuid;
  return relire(o);
}

std::vector<std::pair<std::string, std::string>> reseauxRenommes(const ConceptionElec& a, const ConceptionElec& b) {
  const Connectivite ka = connectivite(a), kb = connectivite(b);
  const auto broches = [](const ConceptionElec& c, const Connectivite& k) {
    std::map<std::string, std::set<std::string>> r;
    for (const auto& n : k.reseaux)
      for (const auto& x : n.broches)
        if (const ComposantElec* comp = c.composant(x.composant)) r[n.nom].insert(comp->reference + "." + x.broche);
    return r;
  };
  const auto ba = broches(a, ka), bb = broches(b, kb);
  std::vector<std::pair<std::string, std::string>> r;
  for (const auto& [na, sa] : ba) {
    if (sa.size() < 2 || bb.count(na)) continue;
    std::string meilleur;
    double score = 0;
    for (const auto& [nb, sb] : bb) {
      if (ba.count(nb)) continue;
      std::size_t commun = 0;
      for (const auto& x : sa) commun += sb.count(x);
      const double j = static_cast<double>(commun) / static_cast<double>(sa.size() + sb.size() - commun);
      if (j > score) {
        score = j;
        meilleur = nb;
      }
    }
    if (score >= 0.6) r.emplace_back(na, meilleur);
  }
  return r;
}

// ---------- Fusion à trois voies ----------
namespace {
struct Fusion {
  const std::map<std::string, ChoixFusion>& choix;
  Lecteur lecteur;
  std::vector<ConflitFusion> conflits;
  int deNous = 0, dEux = 0;

  void conflit(const std::string& chemin, const std::string& objet, const std::string& tableau, const J* el, const J& o, const J& n, const J& e) {
    ConflitFusion c;
    c.chemin = chemin;
    if (!objet.empty()) {
      c.categorie = Lecteur::categorie(objet, tableau);
      c.repere = el ? lecteur.repere(c.categorie, *el) : std::string();
      c.propriete = chemin.size() > objet.size() ? chemin.substr(objet.size() + 1) : std::string();
    } else
      c.categorie = chemin.substr(0, chemin.find('.'));
    c.ancetre = compact(o);
    c.nous = compact(n);
    c.eux = compact(e);
    conflits.push_back(c);
  }
  bool prendreEux(const std::string& chemin) const {
    const auto it = choix.find(chemin);
    return it != choix.end() && it->second == ChoixFusion::Eux;
  }

  // Valeur fusionnée ; « o » l'ancêtre, « n » nous, « e » eux (null : absent)
  J fondre(const J& o, const J& n, const J& e, const std::string& chemin, const std::string& objet, const std::string& tableau, const J* el) {
    if (n == e) return n;
    if (n == o) {
      ++dEux;
      return e;
    }
    if (e == o) {
      ++deNous;
      return n;
    }
    if (n.is_object() && e.is_object() && (o.is_object() || o.is_null())) {
      J r = J::object();
      std::vector<std::string> cles;
      for (const J* x : {&n, &e, &o})
        if (x->is_object())
          for (auto it = x->begin(); it != x->end(); ++it)
            if (std::find(cles.begin(), cles.end(), it.key()) == cles.end()) cles.push_back(it.key());
      static const J nul;
      for (const auto& k : cles) {
        const J& xo = o.is_object() && o.contains(k) ? o[k] : nul;
        const J& xn = n.contains(k) ? n[k] : nul;
        const J& xe = e.contains(k) ? e[k] : nul;
        J v = fondre(xo, xn, xe, chemin.empty() ? k : chemin + "." + k, objet, tableau, el);
        if (!v.is_null()) r[k] = v;
      }
      return r;
    }
    if (n.is_array() && e.is_array() && (o.is_array() || o.is_null())) {
      const J vide = J::array();
      const J& oa = o.is_array() ? o : vide;
      const std::string nom = dernierNom(chemin);
      if (const std::string cle = cleIdentite(n, e); !cle.empty() && (oa.empty() || !cleIdentite(oa).empty())) {
        std::map<std::string, const J*> io, in, ie;
        for (const auto& x : oa)
          if (x.is_object() && x.contains(cle)) io[x[cle].get<std::string>()] = &x;
        for (const auto& x : n) in[x[cle].get<std::string>()] = &x;
        for (const auto& x : e) ie[x[cle].get<std::string>()] = &x;
        static const J nul;
        J r = J::array();
        auto fondreUn = [&](const std::string& id) {
          const J& xo = io.count(id) ? *io[id] : nul;
          const J& xn = in.count(id) ? *in[id] : nul;
          const J& xe = ie.count(id) ? *ie[id] : nul;
          const std::string c = chemin + "[" + cle + "=" + id + "]";
          J v = fondre(xo, xn, xe, c, c, nom, &(xn.is_null() ? (xe.is_null() ? xo : xe) : xn));
          if (!v.is_null()) r.push_back(v);
        };
        // L'ordre de « nous », puis ce qu'« eux » ajoute
        for (const auto& x : n) fondreUn(x[cle].get<std::string>());
        for (const auto& x : e)
          if (!in.count(x[cle].get<std::string>())) fondreUn(x[cle].get<std::string>());
        return r;
      }
      if (ensemble(nom)) {
        std::set<std::string> so, sn, se;
        for (const auto& x : oa) so.insert(x.dump());
        for (const auto& x : n) sn.insert(x.dump());
        for (const auto& x : e) se.insert(x.dump());
        J r = J::array();
        std::set<std::string> mis;
        for (const J* t : {&n, &e})
          for (const auto& x : *t) {
            const std::string d = x.dump();
            const bool retire = so.count(d) && (!sn.count(d) || !se.count(d));
            if (!retire && mis.insert(d).second) r.push_back(x);
          }
        ++deNous;
        ++dEux;
        return r;
      }
    }
    // Valeur tirée d'une autre (la lecture d'une valeur de composant) : elle suit sa source, relue à l'ouverture
    if (chemin.find(".valeurLue") != std::string::npos) return n;
    // Les deux côtés ont changé la même valeur autrement : un conflit, tranché par le choix ou en gardant « nous »
    conflit(chemin, objet, tableau, el, o, n, e);
    return prendreEux(chemin) ? e : n;
  }
};
}  // namespace

ResultatFusion fusionner(const ConceptionElec& ancetre, const ConceptionElec& nous, const ConceptionElec& eux, const std::map<std::string, ChoixFusion>& choix) {
  const J o = versJson(ancetre), n = versJson(nous), e = versJson(eux);
  Fusion f{choix, {}, {}, 0, 0};
  f.lecteur.lire(o);
  f.lecteur.lire(n);
  f.lecteur.lire(e);
  J r = f.fondre(o, n, e, "", "", "", nullptr);
  ResultatFusion res;
  res.conception = relire(r);
  res.conflits = std::move(f.conflits);
  res.deNous = f.deNous;
  res.dEux = f.dEux;
  return res;
}

}  // namespace prom::ecad
