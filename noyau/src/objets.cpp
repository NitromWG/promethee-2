// Prométhée : tableau de tous les objets d'une conception (lot L3, tâche 3.21). Voir objets.hpp.
// Licence GPL-3.0-only.
#include "promethee/objets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "promethee/formules.hpp"

namespace prom::ecad {

namespace {
using P = ProprieteObjet;

std::string minuscules(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return s;
}

std::string sansEspaces(const std::string& s) {
  const auto d = s.find_first_not_of(" \t\r\n");
  if (d == std::string::npos) return {};
  return s.substr(d, s.find_last_not_of(" \t\r\n") - d + 1);
}

std::optional<Couche> coucheDepuisTexte(const std::string& t) {
  if (const auto c = coucheDepuisCode(sansEspaces(t))) return c;
  const std::string m = minuscules(sansEspaces(t));
  for (int i = 0; i < NB_COUCHES; ++i)
    if (minuscules(nomCouche(static_cast<Couche>(i))) == m) return static_cast<Couche>(i);
  return std::nullopt;
}

std::string nomFeuille(const ConceptionElec& c, int f) {
  return f >= 0 && f < static_cast<int>(c.feuilles.size()) ? c.feuilles[static_cast<std::size_t>(f)].nom : std::string();
}

void ajouterPosition(LigneObjet& l, Pt p, bool modifiable = true) {
  l.textes[P::X] = ecrireNombreObjet(mm(p.x));
  l.textes[P::Y] = ecrireNombreObjet(mm(p.y));
  if (modifiable) l.modifiables.insert({P::X, P::Y});
}

bool refuser(std::string* erreur, const std::string& m) {
  if (erreur) *erreur = m;
  return false;
}

// Déplace un objet du schéma, fils accrochés retracés (comme un glissement)
bool deplacerObjetSchema(ConceptionElec& c, const LigneObjet& l, Pt actuel, std::optional<double> x, std::optional<double> y) {
  if (l.feuille < 0 || l.feuille >= static_cast<int>(c.feuilles.size())) return false;
  const Pt vise{x ? nm(*x) : actuel.x, y ? nm(*y) : actuel.y};
  if (vise == actuel) return true;
  deplacerSchema(c, static_cast<std::size_t>(l.feuille), {l.id}, vise - actuel, true);
  return true;
}
}  // namespace

const char* nomGenreObjet(GenreObjet g) {
  switch (g) {
    case GenreObjet::Composant: return "Composant";
    case GenreObjet::Symbole: return "Symbole";
    case GenreObjet::Empreinte: return "Empreinte";
    case GenreObjet::Fil: return "Fil";
    case GenreObjet::Etiquette: return "Étiquette";
    case GenreObjet::TexteSchema: return "Texte du schéma";
    case GenreObjet::Piste: return "Piste";
    case GenreObjet::Via: return "Via";
    case GenreObjet::Zone: return "Zone";
    case GenreObjet::TexteCarte: return "Texte de la carte";
  }
  return "";
}

const char* nomProprieteObjet(ProprieteObjet p) {
  switch (p) {
    case P::Nom: return "Nom";
    case P::Lieu: return "Feuille ou couche";
    case P::X: return "X (mm)";
    case P::Y: return "Y (mm)";
    case P::Rotation: return "Rotation (°)";
    case P::Largeur: return "Largeur (mm)";
    case P::Valeur: return "Valeur";
    case P::Reseau: return "Réseau";
  }
  return "";
}

bool proprieteNumerique(ProprieteObjet p) { return p == P::X || p == P::Y || p == P::Rotation || p == P::Largeur; }

std::string ecrireNombreObjet(double v) {
  if (std::abs(v) < 5e-5) v = 0;
  char b[64];
  std::snprintf(b, sizeof b, "%.4f", v);
  std::string s = b;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  for (char& ch : s)
    if (ch == '.') ch = ',';
  return s;
}

std::optional<double> lireNombreObjet(const std::string& texte) {
  std::string t = sansEspaces(texte);
  if (t.empty()) return std::nullopt;
  if (t.front() == '=') {
    const ResultatFormule r = evaluerFormule(t.substr(1));
    if (!r.ok || !r.q.scalaire() || !r.q.reelle()) return std::nullopt;
    // Une longueur écrite avec son unité (« =2,54 mm ») est lue en millimètres
    return r.q.dim.sans() ? r.q.reel() : r.q.reel() * 1e3;
  }
  for (const char* unite : {"mm", "°", "deg"})
    if (const std::string u = unite; t.size() > u.size() && t.compare(t.size() - u.size(), u.size(), u) == 0) t = sansEspaces(t.substr(0, t.size() - u.size()));
  std::replace(t.begin(), t.end(), ',', '.');
  char* fin = nullptr;
  const double v = std::strtod(t.c_str(), &fin);
  if (fin == t.c_str() || *fin != '\0' || !std::isfinite(v)) return std::nullopt;
  return v;
}

std::vector<LigneObjet> objetsConception(const ConceptionElec& c) {
  std::vector<LigneObjet> r;
  // Composants, puis leurs symboles posés et leur empreinte
  std::vector<const ComposantElec*> tries;
  for (const auto& k : c.composants) tries.push_back(&k);
  std::sort(tries.begin(), tries.end(), [](const auto* a, const auto* b) { return ordreNaturel(a->reference, b->reference); });
  for (const auto* k : tries) {
    LigneObjet l;
    l.genre = GenreObjet::Composant;
    l.id = k->uuid;
    l.textes[P::Nom] = k->reference;
    l.textes[P::Valeur] = k->valeur;
    l.modifiables = {P::Nom, P::Valeur};
    r.push_back(l);
    for (std::size_t f = 0; f < c.feuilles.size(); ++f)
      for (const auto& sp : c.feuilles[f].symboles) {
        if (sp.composant != k->uuid) continue;
        LigneObjet s;
        s.genre = GenreObjet::Symbole;
        s.id = sp.uuid;
        s.feuille = static_cast<int>(f);
        s.profondeur = 1;
        s.parent = k->reference;
        const DefSymbole* d = c.symbole(sp.symbole);
        s.textes[P::Nom] = k->reference + (d && d->unites > 1 ? std::string(1, static_cast<char>('A' + std::max(0, sp.unite - 1))) : std::string());
        s.textes[P::Lieu] = c.feuilles[f].nom;
        ajouterPosition(s, sp.position);
        s.textes[P::Rotation] = ecrireNombreObjet(90.0 * sp.rotation);
        s.modifiables.insert(P::Rotation);
        r.push_back(s);
      }
    for (const auto& e : c.circuit.empreintes) {
      if (e.composant != k->uuid) continue;
      LigneObjet m;
      m.genre = GenreObjet::Empreinte;
      m.id = k->uuid;
      m.profondeur = 1;
      m.parent = k->reference;
      m.textes[P::Nom] = e.empreinte;
      m.textes[P::Lieu] = e.dessous ? "Dessous" : "Dessus";
      ajouterPosition(m, e.position, !e.verrou);
      m.textes[P::Rotation] = ecrireNombreObjet(e.rotation);
      if (!e.verrou) m.modifiables.insert({P::Rotation, P::Lieu});
      r.push_back(m);
    }
  }
  // Schéma : fils, étiquettes, textes de chaque feuille
  for (std::size_t f = 0; f < c.feuilles.size(); ++f) {
    const Feuille& fe = c.feuilles[f];
    for (const auto& x : fe.fils) {
      LigneObjet l;
      l.genre = GenreObjet::Fil;
      l.id = x.uuid;
      l.feuille = static_cast<int>(f);
      l.textes[P::Nom] = "Fil";
      l.textes[P::Lieu] = fe.nom;
      ajouterPosition(l, x.a);
      r.push_back(l);
    }
    for (const auto& x : fe.etiquettes) {
      LigneObjet l;
      l.genre = GenreObjet::Etiquette;
      l.id = x.uuid;
      l.feuille = static_cast<int>(f);
      l.textes[P::Nom] = x.texte;
      l.textes[P::Reseau] = x.texte;
      l.textes[P::Lieu] = fe.nom;
      ajouterPosition(l, x.p);
      l.textes[P::Rotation] = ecrireNombreObjet(90.0 * x.rotation);
      l.modifiables.insert({P::Nom, P::Rotation});
      r.push_back(l);
    }
    for (const auto& x : fe.textes) {
      LigneObjet l;
      l.genre = GenreObjet::TexteSchema;
      l.id = x.uuid;
      l.feuille = static_cast<int>(f);
      l.textes[P::Nom] = x.texte;
      l.textes[P::Lieu] = fe.nom;
      ajouterPosition(l, x.p);
      l.textes[P::Rotation] = ecrireNombreObjet(90.0 * x.rotation);
      l.textes[P::Largeur] = ecrireNombreObjet(mm(x.taille));
      l.modifiables.insert({P::Nom, P::Rotation, P::Largeur});
      r.push_back(l);
    }
  }
  // Carte : pistes, vias, zones, textes
  for (const auto& x : c.circuit.pistes) {
    LigneObjet l;
    l.genre = GenreObjet::Piste;
    l.id = x.uuid;
    l.textes[P::Nom] = "Piste";
    l.textes[P::Lieu] = nomCouche(x.couche);
    l.textes[P::Reseau] = x.reseau;
    ajouterPosition(l, x.a);
    l.textes[P::Largeur] = ecrireNombreObjet(mm(x.largeur));
    l.modifiables.insert({P::Lieu, P::Largeur, P::Reseau});
    r.push_back(l);
  }
  for (const auto& x : c.circuit.vias) {
    LigneObjet l;
    l.genre = GenreObjet::Via;
    l.id = x.uuid;
    l.textes[P::Nom] = "Via";
    l.textes[P::Reseau] = x.reseau;
    ajouterPosition(l, x.p);
    l.textes[P::Largeur] = ecrireNombreObjet(mm(x.diametre));
    l.modifiables.insert({P::Largeur, P::Reseau});
    r.push_back(l);
  }
  for (const auto& x : c.circuit.zones) {
    LigneObjet l;
    l.genre = GenreObjet::Zone;
    l.id = x.uuid;
    l.textes[P::Nom] = "Zone";
    l.textes[P::Lieu] = nomCouche(x.couche);
    l.textes[P::Reseau] = x.reseau;
    if (!x.contour.empty()) ajouterPosition(l, x.contour.front());
    l.modifiables.insert(P::Reseau);
    r.push_back(l);
  }
  for (const auto& x : c.circuit.textes) {
    LigneObjet l;
    l.genre = GenreObjet::TexteCarte;
    l.id = x.uuid;
    l.textes[P::Nom] = x.texte;
    l.textes[P::Lieu] = nomCouche(x.couche);
    ajouterPosition(l, x.p);
    l.textes[P::Rotation] = ecrireNombreObjet(x.rotation);
    l.textes[P::Largeur] = ecrireNombreObjet(mm(x.hauteur));
    l.modifiables.insert({P::Nom, P::Lieu, P::Rotation, P::Largeur});
    r.push_back(l);
  }
  return r;
}

bool ecrireProprieteObjet(ConceptionElec& c, const LigneObjet& l, ProprieteObjet p, const std::string& texte, std::string* erreur) {
  if (!l.modifiables.count(p)) return refuser(erreur, std::string("« ") + nomProprieteObjet(p) + " » ne se modifie pas sur " + nomGenreObjet(l.genre));
  const std::string t = sansEspaces(texte);
  std::optional<double> v;
  if (proprieteNumerique(p)) {
    v = lireNombreObjet(t);
    if (!v) return refuser(erreur, "« " + texte + " » n'est pas un nombre");
    if (p == P::Largeur && *v <= 0) return refuser(erreur, "une largeur est positive");
  }
  const bool x = p == P::X, y = p == P::Y;
  auto quart = [&](int& r) {
    const double q = *v / 90.0;
    if (std::abs(q - std::round(q)) > 1e-6) return refuser(erreur, "le schéma tourne par quarts de tour (0, 90, 180, 270)");
    r = ((static_cast<int>(std::lround(q)) % 4) + 4) % 4;
    return true;
  };
  switch (l.genre) {
    case GenreObjet::Composant: {
      ComposantElec* k = c.composant(l.id);
      if (!k) return refuser(erreur, "composant disparu");
      if (p == P::Nom) {
        if (t.empty()) return refuser(erreur, "un repère n'est pas vide");
        k->reference = t;
      } else if (!t.empty() && t.front() == '=') {
        k->formule = t.substr(1);
      } else {
        k->valeur = t;
        k->formule.clear();
      }
      return true;
    }
    case GenreObjet::Symbole: {
      if (l.feuille < 0 || l.feuille >= static_cast<int>(c.feuilles.size())) return refuser(erreur, "feuille disparue");
      auto& ss = c.feuilles[static_cast<std::size_t>(l.feuille)].symboles;
      const auto it = std::find_if(ss.begin(), ss.end(), [&](const SymbolePlace& s) { return s.uuid == l.id; });
      if (it == ss.end()) return refuser(erreur, "symbole disparu");
      if (p == P::Rotation) return quart(it->rotation);
      return deplacerObjetSchema(c, l, it->position, x ? v : std::nullopt, y ? v : std::nullopt);
    }
    case GenreObjet::Empreinte: {
      auto& es = c.circuit.empreintes;
      const auto it = std::find_if(es.begin(), es.end(), [&](const EmpreintePlacee& e) { return e.composant == l.id; });
      if (it == es.end()) return refuser(erreur, "empreinte disparue");
      if (it->verrou) return refuser(erreur, "empreinte verrouillée");
      if (x) it->position.x = nm(*v);
      else if (y) it->position.y = nm(*v);
      else if (p == P::Rotation) it->rotation = std::fmod(std::fmod(*v, 360.0) + 360.0, 360.0);
      else if (p == P::Lieu) {
        const std::string m = minuscules(t);
        if (m != "dessus" && m != "dessous") return refuser(erreur, "une empreinte est « dessus » ou « dessous »");
        it->dessous = m == "dessous";
      }
      return true;
    }
    case GenreObjet::Fil:
    case GenreObjet::Etiquette:
    case GenreObjet::TexteSchema: {
      if (l.feuille < 0 || l.feuille >= static_cast<int>(c.feuilles.size())) return refuser(erreur, "feuille disparue");
      Feuille& fe = c.feuilles[static_cast<std::size_t>(l.feuille)];
      if (l.genre == GenreObjet::Fil) {
        const auto it = std::find_if(fe.fils.begin(), fe.fils.end(), [&](const Fil& f) { return f.uuid == l.id; });
        if (it == fe.fils.end()) return refuser(erreur, "fil disparu");
        return deplacerObjetSchema(c, l, it->a, x ? v : std::nullopt, y ? v : std::nullopt);
      }
      if (l.genre == GenreObjet::Etiquette) {
        const auto it = std::find_if(fe.etiquettes.begin(), fe.etiquettes.end(), [&](const Etiquette& e) { return e.uuid == l.id; });
        if (it == fe.etiquettes.end()) return refuser(erreur, "étiquette disparue");
        if (p == P::Nom) {
          if (t.empty()) return refuser(erreur, "une étiquette porte un nom");
          it->texte = t;
          return true;
        }
        if (p == P::Rotation) return quart(it->rotation);
        return deplacerObjetSchema(c, l, it->p, x ? v : std::nullopt, y ? v : std::nullopt);
      }
      const auto it = std::find_if(fe.textes.begin(), fe.textes.end(), [&](const TexteSchema& e) { return e.uuid == l.id; });
      if (it == fe.textes.end()) return refuser(erreur, "texte disparu");
      if (p == P::Nom) it->texte = texte;
      else if (p == P::Rotation) return quart(it->rotation);
      else if (p == P::Largeur) it->taille = nm(*v);
      else return deplacerObjetSchema(c, l, it->p, x ? v : std::nullopt, y ? v : std::nullopt);
      return true;
    }
    case GenreObjet::Piste: {
      const auto it = std::find_if(c.circuit.pistes.begin(), c.circuit.pistes.end(), [&](const Piste& e) { return e.uuid == l.id; });
      if (it == c.circuit.pistes.end()) return refuser(erreur, "piste disparue");
      if (x || y) {
        const Pt d{x ? nm(*v) - it->a.x : 0, y ? nm(*v) - it->a.y : 0};
        it->a = it->a + d;
        it->b = it->b + d;
      } else if (p == P::Largeur) it->largeur = nm(*v);
      else if (p == P::Reseau) it->reseau = t;
      else if (p == P::Lieu) {
        const auto k = coucheDepuisTexte(t);
        if (!k || !estCuivre(*k)) return refuser(erreur, "une piste est sur une couche de cuivre (« Cuivre dessus », « B.Cu »…)");
        it->couche = *k;
      }
      return true;
    }
    case GenreObjet::Via: {
      const auto it = std::find_if(c.circuit.vias.begin(), c.circuit.vias.end(), [&](const Via& e) { return e.uuid == l.id; });
      if (it == c.circuit.vias.end()) return refuser(erreur, "via disparue");
      if (x) it->p.x = nm(*v);
      else if (y) it->p.y = nm(*v);
      else if (p == P::Largeur) {
        if (nm(*v) <= it->percage) return refuser(erreur, "le diamètre d'une via dépasse son perçage");
        it->diametre = nm(*v);
      } else if (p == P::Reseau) it->reseau = t;
      return true;
    }
    case GenreObjet::Zone: {
      const auto it = std::find_if(c.circuit.zones.begin(), c.circuit.zones.end(), [&](const Zone& e) { return e.uuid == l.id; });
      if (it == c.circuit.zones.end()) return refuser(erreur, "zone disparue");
      if ((x || y) && !it->contour.empty()) {
        const Pt d{x ? nm(*v) - it->contour.front().x : 0, y ? nm(*v) - it->contour.front().y : 0};
        for (Pt& q : it->contour) q = q + d;
      } else if (p == P::Reseau) it->reseau = t;
      return true;
    }
    case GenreObjet::TexteCarte: {
      const auto it = std::find_if(c.circuit.textes.begin(), c.circuit.textes.end(), [&](const TexteCircuit& e) { return e.uuid == l.id; });
      if (it == c.circuit.textes.end()) return refuser(erreur, "texte disparu");
      if (x) it->p.x = nm(*v);
      else if (y) it->p.y = nm(*v);
      else if (p == P::Rotation) it->rotation = std::fmod(std::fmod(*v, 360.0) + 360.0, 360.0);
      else if (p == P::Largeur) it->hauteur = nm(*v);
      else if (p == P::Nom) it->texte = texte;
      else if (p == P::Lieu) {
        const auto k = coucheDepuisTexte(t);
        if (!k) return refuser(erreur, "couche inconnue : « " + t + " »");
        it->couche = *k;
      }
      return true;
    }
  }
  return false;
}

std::optional<std::string> transformerCellule(const std::string& texte, ProprieteObjet p, const RegleTexte& r, std::string* erreur) {
  if (!proprieteNumerique(p) || r.genre != RegleTexte::Genre::Formule) return transformerTexte(texte, r, FamilleValeur::Aucune, erreur);
  const std::optional<double> x = lireNombreObjet(texte);
  if (!x) {
    if (erreur) *erreur = "« " + texte + " » n'est pas un nombre";
    return std::nullopt;
  }
  const ResultatFormule res = evaluerFormule(r.a, [&](const std::string& nom) -> std::optional<Quantite> {
    if (nom == "x") return Quantite::nombre(*x, {});
    return std::nullopt;
  });
  if (!res.ok || !res.q.scalaire() || !res.q.reelle()) {
    if (erreur) *erreur = res.ok ? "le résultat n'est pas un nombre réel" : res.erreur;
    return std::nullopt;
  }
  return ecrireNombreObjet(res.q.dim.sans() ? res.q.reel() : res.q.reel() * 1e3);
}

}  // namespace prom::ecad
