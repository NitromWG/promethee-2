// Prométhée : conception électronique — connectivité, vérification électrique (ERC), annotation, nomenclature,
// liste des réseaux, mise à jour du circuit imprimé depuis le schéma, pastilles placées. Licence GPL-3.0-only.
#include "promethee/electronique.hpp"
#include "promethee/articles.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/valeurs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <set>
#include <sstream>

namespace prom::ecad {

const ComposantElec* ConceptionElec::composant(const std::string& id) const {
  for (const auto& c : composants)
    if (c.uuid == id) return &c;
  return nullptr;
}
ComposantElec* ConceptionElec::composant(const std::string& id) {
  for (auto& c : composants)
    if (c.uuid == id) return &c;
  return nullptr;
}
const ComposantElec* ConceptionElec::parReference(const std::string& ref) const {
  for (const auto& c : composants)
    if (c.reference == ref) return &c;
  return nullptr;
}
const DefSymbole* ConceptionElec::symbole(const std::string& id) const {
  const auto it = symboles.find(id);
  return it != symboles.end() ? &it->second : symboleBibliotheque(id);
}

const DefEmpreinte* ConceptionElec::empreinte(const std::string& id) const {
  const auto it = empreintes.find(id);
  return it != empreintes.end() ? &it->second : empreinteEnCache(id);
}

ConceptionElec nouvelleConception(const std::string& nom) {
  ConceptionElec c;
  c.uuid = nouvelUuid();
  c.nom = nom;
  Feuille f;
  f.uuid = nouvelUuid();
  c.feuilles.push_back(f);
  c.cartouche.titre = nom;
  return c;
}

ConceptionVerrouillee::ConceptionVerrouillee(const ConceptionElec& c)
    : std::runtime_error("« " + c.nom + " » est publiée" + (c.publications.empty() ? std::string() : " (" + c.publications.back().id + ")") +
                         " et verrouillée : passe à une nouvelle révision, dans l’onglet Fabrication, pour la modifier.") {}

void exigerModifiable(const ConceptionElec& c) {
  if (c.verrouillee) throw ConceptionVerrouillee(c);
}

std::string poserSymbole(ConceptionElec& c, std::size_t feuille, const std::string& idSymbole, Pt position, int rotation) {
  exigerModifiable(c);
  const DefSymbole* d = c.symbole(idSymbole);
  if (!d || feuille >= c.feuilles.size()) return {};
  if (!c.symboles.count(idSymbole)) c.symboles[idSymbole] = *d;  // la version utilisée est verrouillée dans le projet
  SymbolePlace s;
  s.uuid = nouvelUuid();
  s.symbole = idSymbole;
  s.position = position;
  s.rotation = rotation;
  if (d->alimentation) s.nomAlim = d->nom;
  else if (!d->drapeau) {
    ComposantElec k;
    k.uuid = nouvelUuid();
    k.reference = d->prefixe + "?";
    k.valeur = d->id == "R" ? "10k" : d->id == "C" ? "100nF" : d->id == "C_Pol" ? "10µF" : d->id == "L" ? "10µH" : d->id == "Regulateur" ? "AMS1117-3.3" : d->id == "LED" ? "Rouge" : d->nom;
    k.symbole = idSymbole;
    k.empreinte = d->empreinte;
    k.description = d->description;
    k.fabricant = d->fabricant;
    k.mpn = d->mpn;
    k.fiche = d->fiche;
    // Genre du symbole (3.17) ; un symbole mécanique sans empreinte ne va qu'à la nomenclature
    k.genre = d->genre;
    if (k.genre == GenreComposant::Mecanique && d->empreinte.empty()) k.horsCarte = true;
    remplirDepuisArticle(c, k);  // symbole d'un article (« art:<id> ») : le composant réel
    c.composants.push_back(k);
    s.composant = k.uuid;
  }
  c.feuilles[feuille].symboles.push_back(s);
  return s.uuid;
}

std::set<int> unitesPosees(const ConceptionElec& c, const std::string& composant) {
  std::set<int> u;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.composant == composant) u.insert(s.unite);
  return u;
}

std::string prochainRepere(const ConceptionElec& c, const std::string& prefixe) {
  std::set<int> pris;
  for (const auto& x : c.composants) {
    const std::string& r = x.reference;
    if (r.size() <= prefixe.size() || r.compare(0, prefixe.size(), prefixe) != 0) continue;
    std::size_t j = prefixe.size();
    while (j < r.size() && std::isdigit(static_cast<unsigned char>(r[j]))) ++j;
    // « R12 », ou « U1A » (lettre d'unité) ; « RV1 » n'est pas un R
    const bool unite = j + 1 == r.size() && std::isupper(static_cast<unsigned char>(r[j]));
    if (j > prefixe.size() && j - prefixe.size() <= 9 && (j == r.size() || unite)) pris.insert(std::stoi(r.substr(prefixe.size(), j - prefixe.size())));
  }
  int n = 1;
  while (pris.count(n)) ++n;
  return prefixe + std::to_string(n);
}

std::string annoterALaPose(ConceptionElec& c, const std::string& composant) {
  exigerModifiable(c);
  ComposantElec* k = c.composant(composant);
  if (!k) return {};
  if (!k->reference.empty() && k->reference.find('?') == std::string::npos) return k->reference;
  std::string prefixe = k->reference.substr(0, k->reference.find('?'));
  while (!prefixe.empty() && std::isdigit(static_cast<unsigned char>(prefixe.back()))) prefixe.pop_back();
  if (prefixe.empty())
    if (const DefSymbole* d = c.symbole(k->symbole)) prefixe = d->prefixe;
  if (prefixe.empty()) prefixe = "U";
  k->reference = prochainRepere(c, prefixe);
  return k->reference;
}

int uniteSuivante(const ConceptionElec& c, const std::string& composant) {
  const ComposantElec* k = c.composant(composant);
  const DefSymbole* d = k ? c.symbole(k->symbole) : nullptr;
  if (!d || d->unites <= 1) return 0;
  const std::set<int> poses = unitesPosees(c, composant);
  for (int u = 1; u <= d->unites; ++u)
    if (!poses.count(u)) return u;
  return 0;
}

std::string poserUniteEn(ConceptionElec& c, std::size_t feuille, const std::string& composant, int unite, Pt position, int rotation) {
  exigerModifiable(c);
  const ComposantElec* k = c.composant(composant);
  const DefSymbole* d = k ? c.symbole(k->symbole) : nullptr;
  if (!d || feuille >= c.feuilles.size() || unite < 1 || unite > d->unites || unitesPosees(c, composant).count(unite)) return {};
  SymbolePlace s;
  s.uuid = nouvelUuid();
  s.symbole = k->symbole;
  s.composant = composant;
  s.position = position;
  s.rotation = rotation;
  s.unite = unite;
  c.feuilles[feuille].symboles.push_back(s);
  return s.uuid;
}

bool insererDansFil(ConceptionElec& c, std::size_t feuille, const std::string& symbole) {
  exigerModifiable(c);
  if (feuille >= c.feuilles.size()) return false;
  Feuille& f = c.feuilles[feuille];
  const SymbolePlace* s = nullptr;
  for (const auto& x : f.symboles)
    if (x.uuid == symbole) s = &x;
  const DefSymbole* d = s ? c.symbole(s->symbole) : nullptr;
  if (!d) return false;
  const auto bp = brochesPlacees(*s, *d);
  if (bp.size() != 2 || bp[0].p == bp[1].p) return false;
  for (std::size_t i = 0; i < f.fils.size(); ++i) {
    const Fil w = f.fils[i];
    if (!surSegment(bp[0].p, w.a, w.b) || !surSegment(bp[1].p, w.a, w.b)) continue;
    // Les deux broches dans l'ordre du fil : a, la plus proche de a, l'autre, b
    const bool ordre = distance(w.a, bp[0].p) <= distance(w.a, bp[1].p);
    const Pt p1 = ordre ? bp[0].p : bp[1].p, p2 = ordre ? bp[1].p : bp[0].p;
    f.fils.erase(f.fils.begin() + static_cast<std::ptrdiff_t>(i));
    if (w.a != p1) f.fils.push_back({nouvelUuid(), w.a, p1});
    if (p2 != w.b) f.fils.push_back({nouvelUuid(), p2, w.b});
    return true;
  }
  return false;
}

int replacerChamps(ConceptionElec& c, std::size_t feuille, const std::string& symbole) {
  exigerModifiable(c);
  if (feuille >= c.feuilles.size()) return 0;
  Feuille& f = c.feuilles[feuille];
  SymbolePlace* s = nullptr;
  for (auto& x : f.symboles)
    if (x.uuid == symbole) s = &x;
  const DefSymbole* d = s ? c.symbole(s->symbole) : nullptr;
  if (!d || d->alimentation || d->drapeau) return 0;
  const ComposantElec* k = c.composant(s->composant);
  // Longueur approchée d'un texte (chasse de 0,62 fois la hauteur) et sa boîte, ancré à gauche, à droite ou centré
  auto boiteTexte = [](Pt ancre, std::size_t n, double hauteur, int alignement) {
    const Nm l = static_cast<Nm>(0.62 * hauteur * 1e6 * static_cast<double>(n)), h = static_cast<Nm>(hauteur * 1e6);
    const Nm x0 = alignement < 0 ? ancre.x : alignement > 0 ? ancre.x - l : ancre.x - l / 2;
    return Boite{x0, ancre.y - h / 2, x0 + l, ancre.y + h / 2};
  };
  const Boite corps = boiteSymbole(*s, *d);
  const std::size_t nRef = k ? k->reference.size() + (d->unites > 1 ? 1 : 0) : 2, nVal = k ? k->valeur.size() : 2;
  // Obstacles : les autres symboles, les fils, les étiquettes et les textes de la feuille
  std::vector<Boite> obstacles;
  for (const auto& x : f.symboles)
    if (x.uuid != s->uuid)
      if (const DefSymbole* dx = c.symbole(x.symbole)) obstacles.push_back(boiteSymbole(x, *dx));
  for (const auto& w : f.fils) {
    Boite b;
    b.etendre(w.a);
    b.etendre(w.b);
    obstacles.push_back(b.gonflee(300'000));
  }
  for (const auto& e : f.etiquettes) obstacles.push_back(Boite{e.p.x, e.p.y - 900'000, e.p.x + static_cast<Nm>(e.texte.size()) * 1'100'000, e.p.y + 900'000});
  // Les bouts de broches, les siennes comprises : un texte posé dessus cacherait où le fil s'accroche
  for (const auto& x : f.symboles)
    if (const DefSymbole* dx = c.symbole(x.symbole))
      for (const auto& b : brochesPlacees(x, *dx)) obstacles.push_back(Boite{b.p.x - 900'000, b.p.y - 900'000, b.p.x + 900'000, b.p.y + 900'000});
  for (const auto& t : f.textes) obstacles.push_back(boiteTexte(t.p, t.texte.size(), mm(t.taille), 0));
  auto recouvrement = [&](const Boite& b) {
    double aire = 0;
    for (const Boite& o : obstacles)
      if (o.touche(b)) {
        const double l = static_cast<double>(std::min(o.x1, b.x1) - std::max(o.x0, b.x0)), h = static_cast<double>(std::min(o.y1, b.y1) - std::max(o.y0, b.y0));
        aire += std::max(0.0, l) * std::max(0.0, h) + 1.0;
      }
    // Le corps du symbole ne doit jamais être couvert
    if (corps.gonflee(-200'000).touche(b)) aire += 1e18;
    return aire;
  };
  // Quatre places : repère au-dessus et valeur au-dessous du corps, ou les deux à droite, à gauche, en dessous
  const Nm m = GRILLE_SCHEMA, ligne = 2 * GRILLE_SCHEMA;
  const Pt cc = surGrille(corps.centre());
  struct Place {
    Pt ref, val;
    int alignement;
  };
  const Place places[] = {{surGrille(Pt{cc.x, corps.y0 - m}), surGrille(Pt{cc.x, corps.y1 + m}), 0},
                          {surGrille(Pt{corps.x1 + m, cc.y - m}), surGrille(Pt{corps.x1 + m, cc.y + m}), -1},
                          {surGrille(Pt{corps.x0 - m, cc.y - m}), surGrille(Pt{corps.x0 - m, cc.y + m}), 1},
                          {surGrille(Pt{cc.x, corps.y1 + m}), surGrille(Pt{cc.x, corps.y1 + m + ligne}), 0}};
  double meilleur = 1e300;
  const Place* choisie = &places[0];
  for (const Place& p : places) {
    const double note = recouvrement(boiteTexte(p.ref, nRef, 1.4, p.alignement)) + recouvrement(boiteTexte(p.val, nVal, 1.3, p.alignement));
    if (note < meilleur) {
      meilleur = note;
      choisie = &p;
    }
  }
  int n = 0;
  // L'ancre d'un champ aligné à gauche est son début ; le dessin aligne selon le côté du symbole où il tombe
  for (const auto& [champ, ancre] : {std::pair{&s->champReference, choisie->ref}, std::pair{&s->champValeur, choisie->val}}) {
    if (*champ && (*champ)->bloque) continue;
    const ChampPlace neuf{ancre - s->position, false};
    if (!*champ || **champ != neuf) {
      *champ = neuf;
      ++n;
    }
  }
  return n;
}

std::string poserArticleCircuit(ConceptionElec& c, const std::string& idSymbole, Pt position, double rotation) {
  exigerModifiable(c);
  const DefSymbole* d = c.symbole(idSymbole);
  if (!d || d->empreinte.empty()) return {};
  // Le symbole va sur la feuille « Mécanique », créée au besoin, rangé en grille au pas de 10,16 mm
  std::size_t fi = c.feuilles.size();
  for (std::size_t i = 0; i < c.feuilles.size(); ++i)
    if (c.feuilles[i].nom == "Mécanique") fi = i;
  if (fi == c.feuilles.size()) {
    Feuille f;
    f.uuid = nouvelUuid();
    f.nom = "Mécanique";
    c.feuilles.push_back(f);
  }
  auto occupe = [&](Pt p) {
    for (const auto& s : c.feuilles[fi].symboles)
      if (distance(s.position, p) < 5e6) return true;
    return false;
  };
  Pt ou = ptMm(25.4, 25.4);
  for (int i = 0; i < 400 && occupe(ou); ++i) ou = ptMm(25.4 + 10.16 * ((i + 1) % 20), 25.4 + 10.16 * ((i + 1) / 20));
  const std::string pose = poserSymbole(c, fi, idSymbole, ou);
  std::string composant;
  for (const auto& s : c.feuilles[fi].symboles)
    if (s.uuid == pose) composant = s.composant;
  ComposantElec* k = c.composant(composant);
  if (!k) return {};
  k->reference = prochainRepere(c, d->prefixe.empty() ? "H" : d->prefixe);
  EmpreintePlacee e;
  e.composant = composant;
  e.empreinte = k->empreinte;
  e.position = position;
  e.rotation = rotation;
  c.circuit.empreintes.push_back(e);
  figerEmpreintes(c);
  return composant;
}

std::string poserUnite(ConceptionElec& c, std::size_t feuille, const std::string& uuidSymbole, int unite) {
  exigerModifiable(c);
  if (feuille >= c.feuilles.size()) return {};
  const SymbolePlace* modele = nullptr;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (s.uuid == uuidSymbole) modele = &s;
  if (!modele || modele->composant.empty()) return {};
  const DefSymbole* d = c.symbole(modele->symbole);
  if (!d || d->unites <= 1) return {};
  const std::set<int> poses = unitesPosees(c, modele->composant);
  if (unite <= 0)
    for (int k = 1; k <= d->unites && unite <= 0; ++k)
      if (!poses.count(k)) unite = k;
  if (unite <= 0 || unite > d->unites) return {};
  SymbolePlace s = *modele;
  s.uuid = nouvelUuid();
  s.unite = unite;
  // À droite du symbole modèle, sur la grille, sans recouvrir aucun symbole déjà là : les boîtes comptent, pas
  // seulement les positions (S33)
  const Boite b = boiteSymbole(*modele, *d);
  const Nm pas = ((b.largeur() + 5'080'000) / GRILLE_SCHEMA + 1) * GRILLE_SCHEMA;
  s.position = modele->position + Pt{pas, 0};
  auto occupe = [&](Pt p) {
    SymbolePlace essai = s;
    essai.position = p;
    const Boite ici = boiteSymbole(essai, *d).gonflee(GRILLE_SCHEMA);
    for (const auto& x : c.feuilles[feuille].symboles)
      if (const DefSymbole* dx = c.symbole(x.symbole); dx && boiteSymbole(x, *dx).touche(ici)) return true;
    return false;
  };
  for (int i = 0; i < 32 && occupe(s.position); ++i) s.position.x += pas;
  c.feuilles[feuille].symboles.push_back(s);
  return s.uuid;
}

std::string idJonction(const Jonction& j) { return !j.uuid.empty() ? j.uuid : "j:" + std::to_string(j.p.x) + ":" + std::to_string(j.p.y); }
std::string idNonConnexion(const NonConnexion& n) { return !n.uuid.empty() ? n.uuid : "nc:" + std::to_string(n.p.x) + ":" + std::to_string(n.p.y); }

PressePapiersSchema copierSchema(const ConceptionElec& c, std::size_t feuille, const std::set<std::string>& sel) {
  PressePapiersSchema pp;
  if (feuille >= c.feuilles.size()) return pp;
  const Feuille& f = c.feuilles[feuille];
  Boite b;
  std::set<std::string> composants;
  for (const auto& s : f.symboles)
    if (sel.count(s.uuid)) {
      pp.symboles.push_back(s);
      if (const DefSymbole* d = c.symbole(s.symbole)) {
        pp.definitions[s.symbole] = *d;
        b.etendre(boiteSymbole(s, *d));
      }
      if (!s.composant.empty() && composants.insert(s.composant).second)
        if (const ComposantElec* k = c.composant(s.composant)) {
          pp.composants.push_back(*k);
          if (const auto it = c.empreintes.find(k->empreinte); it != c.empreintes.end()) pp.empreintes[it->first] = it->second;
        }
    }
  for (const auto& w : f.fils) if (sel.count(w.uuid)) { pp.fils.push_back(w); b.etendre(w.a); b.etendre(w.b); }
  for (const auto& e : f.etiquettes) if (sel.count(e.uuid)) { pp.etiquettes.push_back(e); b.etendre(e.p); }
  for (const auto& t : f.textes) if (sel.count(t.uuid)) { pp.textes.push_back(t); b.etendre(t.p); }
  for (const auto& j : f.jonctions) if (sel.count(idJonction(j))) { pp.jonctions.push_back(j); b.etendre(j.p); }
  for (const auto& n : f.nonConnexions) if (sel.count(idNonConnexion(n))) { pp.nonConnexions.push_back(n); b.etendre(n.p); }
  if (!b.vide()) pp.reference = surGrille(Pt{b.x0, b.y0});
  return pp;
}

std::set<std::string> collerSchema(ConceptionElec& c, std::size_t feuille, const PressePapiersSchema& pp, Pt ou) {
  exigerModifiable(c);
  std::set<std::string> crees;
  if (feuille >= c.feuilles.size() || pp.vide()) return crees;
  const Pt d = surGrille(ou) - pp.reference;
  // Définitions figées (S32) : recopiées telles quelles ; un identifiant déjà pris par une autre définition en
  // reçoit un à lui, « id~2 », et la copie identique d'un collage précédent resert
  auto integrer = [](auto& dans, const auto& venues) {
    std::map<std::string, std::string> renomme;
    for (const auto& [id, def] : venues) {
      const auto it = dans.find(id);
      if (it == dans.end()) {
        dans[id] = def;
        continue;
      }
      if (it->second == def) continue;
      for (int n = 2;; ++n) {
        auto copie = def;
        copie.id = id + "~" + std::to_string(n);
        const auto jt = dans.find(copie.id);
        if (jt != dans.end() && !(jt->second == copie)) continue;
        renomme[id] = copie.id;
        if (jt == dans.end()) dans[copie.id] = copie;
        break;
      }
    }
    return renomme;
  };
  const std::map<std::string, std::string> idSymbole = integrer(c.symboles, pp.definitions), idEmpreinte = integrer(c.empreintes, pp.empreintes);
  auto nouvelId = [](const std::map<std::string, std::string>& m, const std::string& id) {
    const auto it = m.find(id);
    return it == m.end() ? id : it->second;
  };
  std::map<std::string, std::string> nouveauComposant;
  for (const auto& k : pp.composants) {
    ComposantElec n = k;
    n.uuid = nouvelUuid();
    n.symbole = nouvelId(idSymbole, n.symbole);
    n.empreinte = nouvelId(idEmpreinte, n.empreinte);
    const DefSymbole* def = c.symbole(k.symbole);
    n.reference = (def ? def->prefixe : std::string(k.reference.empty() ? "U" : k.reference.substr(0, 1))) + "?";
    nouveauComposant[k.uuid] = n.uuid;
    c.composants.push_back(n);
  }
  Feuille& f = c.feuilles[feuille];
  for (SymbolePlace s : pp.symboles) {
    s.uuid = nouvelUuid();
    s.symbole = nouvelId(idSymbole, s.symbole);
    s.position = s.position + d;
    if (const auto it = nouveauComposant.find(s.composant); it != nouveauComposant.end()) s.composant = it->second;
    crees.insert(s.uuid);
    f.symboles.push_back(s);
  }
  for (Fil w : pp.fils) { w.uuid = nouvelUuid(); w.a = w.a + d; w.b = w.b + d; crees.insert(w.uuid); f.fils.push_back(w); }
  for (Etiquette e : pp.etiquettes) { e.uuid = nouvelUuid(); e.p = e.p + d; crees.insert(e.uuid); f.etiquettes.push_back(e); }
  for (TexteSchema t : pp.textes) { t.uuid = nouvelUuid(); t.p = t.p + d; crees.insert(t.uuid); f.textes.push_back(t); }
  for (Jonction j : pp.jonctions) { j.p = j.p + d; j.uuid = nouvelUuid(); crees.insert(j.uuid); f.jonctions.push_back(j); }
  for (NonConnexion n : pp.nonConnexions) { n.p = n.p + d; n.uuid = nouvelUuid(); crees.insert(n.uuid); f.nonConnexions.push_back(n); }
  return crees;
}

void transformerSelectionSchema(ConceptionElec& c, std::size_t feuille, const std::set<std::string>& sel, OperationSchema op) {
  exigerModifiable(c);
  if (feuille >= c.feuilles.size() || sel.empty()) return;
  Feuille& f = c.feuilles[feuille];
  // Centre : le symbole lui-même s'il est seul, sinon le milieu de l'encombrement de la sélection, sur la grille
  Boite b;
  int symboles = 0, autres = 0;
  Pt seul;
  for (const auto& s2 : f.symboles)
    if (sel.count(s2.uuid)) {
      ++symboles;
      seul = s2.position;
      if (const DefSymbole* d = c.symbole(s2.symbole)) b.etendre(boiteSymbole(s2, *d)); else b.etendre(s2.position);
    }
  for (const auto& w : f.fils) if (sel.count(w.uuid)) { ++autres; b.etendre(w.a); b.etendre(w.b); }
  for (const auto& e : f.etiquettes) if (sel.count(e.uuid)) { ++autres; b.etendre(e.p); }
  for (const auto& t : f.textes) if (sel.count(t.uuid)) { ++autres; b.etendre(t.p); }
  for (const auto& j : f.jonctions) if (sel.count(idJonction(j))) { ++autres; b.etendre(j.p); }
  for (const auto& n : f.nonConnexions) if (sel.count(idNonConnexion(n))) { ++autres; b.etendre(n.p); }
  if (b.vide()) return;
  const Pt centre = symboles == 1 && autres == 0 ? seul : surGrille(b.centre());
  auto point = [&](Pt q) {
    const Pt r = q - centre;
    switch (op) {
      case OperationSchema::Tourner: return tournerQuarts(r, 1) + centre;
      case OperationSchema::MiroirGaucheDroite: return Pt{-r.x, r.y} + centre;
      case OperationSchema::MiroirHautBas: return Pt{r.x, -r.y} + centre;
    }
    return q;
  };
  // Broches des symboles transformés : position d'avant, position d'après, direction (vers le corps) d'après
  struct Deplacement { Pt avant, apres; int direction; Boite corps; };
  std::vector<Deplacement> broches;
  for (auto& s2 : f.symboles) {
    if (!sel.count(s2.uuid)) continue;
    const DefSymbole* d = c.symbole(s2.symbole);
    std::vector<Pt> avant;
    if (d) for (const auto& bp : brochesPlacees(s2, *d)) avant.push_back(bp.p);
    s2.position = point(s2.position);
    // T = R(rot)·Mx^m ; un miroir vu à l'écran s'écrit Mx·R(θ)·Mx^m = R(−θ)·Mx^(m+1), et My = R(180°)·Mx
    if (op == OperationSchema::Tourner) s2.rotation = (s2.rotation + 1) % 4;
    else {
      s2.rotation = op == OperationSchema::MiroirGaucheDroite ? (4 - s2.rotation % 4) % 4 : (6 - s2.rotation % 4) % 4;
      s2.miroir = !s2.miroir;
    }
    if (d) {
      const auto apres = brochesPlacees(s2, *d);
      const Boite corps = boiteSymbole(s2, *d);
      for (std::size_t k = 0; k < avant.size() && k < apres.size(); ++k)
        if (avant[k] != apres[k].p) broches.push_back({avant[k], apres[k].p, apres[k].direction, corps});
    }
  }
  std::map<Pt, Pt> etiquettesDeplacees;
  for (auto& e : f.etiquettes)
    if (sel.count(e.uuid)) {
      etiquettesDeplacees[e.p] = point(e.p);
      e.p = point(e.p);
      if (op == OperationSchema::Tourner) e.rotation = (e.rotation + 1) % 4;
      else if (op == OperationSchema::MiroirGaucheDroite) e.rotation = ((2 - e.rotation) % 4 + 4) % 4;
      else e.rotation = (4 - e.rotation % 4) % 4;
    }
  for (auto& t : f.textes)
    if (sel.count(t.uuid)) {
      t.p = point(t.p);
      if (op == OperationSchema::Tourner) t.rotation = (t.rotation + 1) % 4;
    }
  // Étiquettes et jonctions choisies : les fils non choisis qui y aboutissent les suivent aussi (S31)
  std::map<Pt, Pt> autresPoints;
  for (const auto& [avant, apres] : etiquettesDeplacees) autresPoints[avant] = apres;
  for (auto& j : f.jonctions)
    if (sel.count(idJonction(j))) {
      autresPoints[j.p] = point(j.p);
      j.p = point(j.p);
    }
  // Marques de non-connexion choisies, ou posées sur une broche qui a bougé, la suivent (S17)
  for (auto& n : f.nonConnexions) {
    if (sel.count(idNonConnexion(n))) {
      n.p = point(n.p);
      continue;
    }
    const auto it = std::find_if(broches.begin(), broches.end(), [&](const Deplacement& x) { return x.avant == n.p; });
    if (it != broches.end()) n.p = it->apres;
  }
  // Fil non choisi accroché à une broche qui a bougé : il la rejoint à l'équerre, en partant de la broche vers
  // l'extérieur du symbole (jamais à travers son corps). Le fil d'origine devient le premier tronçon.
  std::vector<Fil> ajouts;
  for (auto& w : f.fils) {
    if (sel.count(w.uuid)) { w.a = point(w.a); w.b = point(w.b); continue; }
    for (const bool cote : {true, false}) {
      Pt& bout = cote ? w.a : w.b;
      const Pt autre = cote ? w.b : w.a;
      const auto it = std::find_if(broches.begin(), broches.end(), [&](const Deplacement& x) { return x.avant == bout; });
      if (it == broches.end()) continue;
      const Pt dirCorps = it->direction == 0 ? Pt{1, 0} : it->direction == 1 ? Pt{0, 1} : it->direction == 2 ? Pt{-1, 0} : Pt{0, -1};
      const Pt n = it->apres;
      // Cas direct : l'autre bout est dans l'axe de la broche, du côté extérieur
      const Nm le_long = (autre.x - n.x) * -dirCorps.x + (autre.y - n.y) * -dirCorps.y;
      const bool aligne = dirCorps.x != 0 ? autre.y == n.y : autre.x == n.x;
      if (aligne && le_long >= 0) { bout = n; continue; }
      // Sinon : amorce d'un pas de grille vers l'extérieur ; si l'autre bout est devant la broche, une équerre ;
      // s'il est derrière, un détour qui contourne le corps du symbole par le côté le plus proche
      const Pt amorce{n.x - dirCorps.x * GRILLE_SCHEMA, n.y - dirCorps.y * GRILLE_SCHEMA};
      const Nm devant = (autre.x - amorce.x) * -dirCorps.x + (autre.y - amorce.y) * -dirCorps.y;
      std::vector<Pt> chemin;
      if (devant >= 0) {
        const Pt coin = dirCorps.x != 0 ? Pt{amorce.x, autre.y} : Pt{autre.x, amorce.y};
        chemin = {autre, coin, amorce, n};
      } else {
        // Contour : côté le plus proche d'abord, puis l'autre, en s'écartant d'un pas de plus tant qu'un tronçon
        // se superposerait à un fil existant (ce qui relierait deux réseaux)
        auto superpose = [&](Pt a, Pt b2) {
          auto chevauche = [&](const Fil& x) {
            if (a.x == b2.x && x.a.x == a.x && x.b.x == a.x)
              return std::max(std::min(a.y, b2.y), std::min(x.a.y, x.b.y)) <= std::min(std::max(a.y, b2.y), std::max(x.a.y, x.b.y));
            if (a.y == b2.y && x.a.y == a.y && x.b.y == a.y)
              return std::max(std::min(a.x, b2.x), std::min(x.a.x, x.b.x)) <= std::min(std::max(a.x, b2.x), std::max(x.a.x, x.b.x));
            return false;
          };
          return std::any_of(f.fils.begin(), f.fils.end(), [&](const Fil& x) { return &x != &w && chevauche(x); }) ||
                 std::any_of(ajouts.begin(), ajouts.end(), chevauche);
        };
        const Boite b = it->corps;
        for (int essai = 0; essai < 8 && chemin.empty(); ++essai) {
          const bool premierCote = essai % 2 == 0;
          const Nm ecart = GRILLE_SCHEMA * (1 + essai / 2);
          std::vector<Pt> c2;
          if (dirCorps.x != 0) {  // broche horizontale : contour par le haut ou le bas
            const bool haut = (autre.y < (b.y0 + b.y1) / 2) == premierCote;
            const Nm y = haut ? surGrille(Pt{0, b.y0}).y - ecart : surGrille(Pt{0, b.y1}).y + ecart;
            c2 = {autre, Pt{autre.x, y}, Pt{amorce.x, y}, amorce, n};
          } else {  // broche verticale : contour par la gauche ou la droite
            const bool gauche = (autre.x < (b.x0 + b.x1) / 2) == premierCote;
            const Nm x = gauche ? surGrille(Pt{b.x0, 0}).x - ecart : surGrille(Pt{b.x1, 0}).x + ecart;
            c2 = {autre, Pt{x, autre.y}, Pt{x, amorce.y}, amorce, n};
          }
          bool libre = true;
          for (std::size_t k = 1; k + 1 < c2.size() && libre; ++k) libre = c2[k] == c2[k + 1] || !superpose(c2[k], c2[k + 1]);
          if (libre || essai == 7) chemin = c2;
        }
      }
      chemin.erase(std::unique(chemin.begin(), chemin.end()), chemin.end());
      bout = chemin[1];
      for (std::size_t k = 1; k + 1 < chemin.size(); ++k) ajouts.push_back({nouvelUuid(), chemin[k], chemin[k + 1]});
    }
  }
  // Fils non choisis accrochés à une étiquette ou à une jonction qui a bougé : le bout suit, un coude garde l'équerre
  for (auto& w : f.fils) {
    if (sel.count(w.uuid)) continue;
    for (const bool cote : {true, false}) {
      Pt& bout = cote ? w.a : w.b;
      const Pt autre = cote ? w.b : w.a;
      const auto it = autresPoints.find(bout);
      if (it == autresPoints.end()) continue;
      const Pt n = it->second;
      if (n.x == autre.x || n.y == autre.y) {
        bout = n;
        continue;
      }
      const Pt coin{n.x, autre.y};
      bout = coin;
      ajouts.push_back({nouvelUuid(), coin, n});
    }
  }
  f.fils.insert(f.fils.end(), ajouts.begin(), ajouts.end());
  std::erase_if(f.fils, [](const Fil& w) { return w.a == w.b; });
}

bool ordreNaturel(const std::string& a, const std::string& b) {
  std::size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    if (std::isdigit(static_cast<unsigned char>(a[i])) && std::isdigit(static_cast<unsigned char>(b[j]))) {
      std::size_t i2 = i, j2 = j;
      while (i2 < a.size() && std::isdigit(static_cast<unsigned char>(a[i2]))) ++i2;
      while (j2 < b.size() && std::isdigit(static_cast<unsigned char>(b[j2]))) ++j2;
      const long long na = std::stoll(a.substr(i, std::min<std::size_t>(i2 - i, 17))), nb = std::stoll(b.substr(j, std::min<std::size_t>(j2 - j, 17)));
      if (na != nb) return na < nb;
      i = i2;
      j = j2;
    } else {
      if (a[i] != b[j]) return a[i] < b[j];
      ++i;
      ++j;
    }
  }
  return a.size() - i < b.size() - j;
}

// ---------- Connectivité ----------
namespace {
struct Unions {
  std::vector<int> p;
  int ajouter() { p.push_back(static_cast<int>(p.size())); return static_cast<int>(p.size()) - 1; }
  int racine(int x) {
    while (p[static_cast<std::size_t>(x)] != x) { p[static_cast<std::size_t>(x)] = p[static_cast<std::size_t>(p[static_cast<std::size_t>(x)])]; x = p[static_cast<std::size_t>(x)]; }
    return x;
  }
  void unir(int a, int b) {
    a = racine(a);
    b = racine(b);
    if (a != b) p[static_cast<std::size_t>(std::max(a, b))] = std::min(a, b);
  }
};
enum class Genre { Fil, Broche, Alim, Drapeau, Locale, Globale };
struct Element {
  int feuille = 0;
  Genre genre = Genre::Fil;
  std::string uuid, composant, broche, nom;
  TypeBroche type = TypeBroche::Passive;
};
}  // namespace

int Connectivite::reseau(const std::string& composant, const std::string& broche) const {
  const auto it = parBroche.find({composant, broche});
  return it == parBroche.end() ? -1 : it->second;
}
std::string Connectivite::nom(const std::string& composant, const std::string& broche) const {
  const int r = reseau(composant, broche);
  return r < 0 ? std::string() : reseaux[static_cast<std::size_t>(r)].nom;
}
int Connectivite::indice(const std::string& n) const {
  const auto it = parNom.find(n);
  if (it != parNom.end()) return it->second;
  for (std::size_t i = 0; i < reseaux.size(); ++i)
    if (reseaux[i].nom == n) return static_cast<int>(i);
  return -1;
}

std::string Connectivite::canonique(const std::string& n) const {
  const auto it = parNom.find(n);
  return it == parNom.end() ? n : reseaux[static_cast<std::size_t>(it->second)].nom;
}

Regles reglesResolues(const Regles& r, const Connectivite& k) {
  if (r.classeReseau.empty()) return r;
  Regles x = r;
  auto classeNommee = [&r](const std::string& nom) -> const ClasseReseau* {
    for (const auto& cl : r.classes)
      if (cl.nom == nom) return &cl;
    return nullptr;
  };
  for (const auto& [n, cl] : r.classeReseau) {
    if (motifReseau(n)) continue;
    const std::string actuel = k.canonique(n);
    if (actuel == n || r.classeReseau.count(actuel)) continue;
    // Deux anciens noms d'un même réseau vers deux classes : la plus contraignante s'applique (R11)
    const auto deja = x.classeReseau.find(actuel);
    if (deja == x.classeReseau.end()) x.classeReseau[actuel] = cl;
    else if (const ClasseReseau* g = plusStricte(classeNommee(deja->second), classeNommee(cl))) deja->second = g->nom;
  }
  return x;
}

namespace {
std::string cleBroche(const std::string& composant, const std::string& broche) { return composant + ":" + broche; }
}  // namespace

int synchroniserReseaux(ConceptionElec& c, std::vector<std::string>* conflits) {
  exigerModifiable(c);
  const Connectivite k = connectivite(c);
  // Noms figés : chaque broche garde le nom de son réseau ; les broches disparues sont oubliées
  std::map<std::string, std::string> figes;
  for (const auto& r : k.reseaux)
    for (const auto& b : r.broches) figes[cleBroche(b.composant, b.broche)] = r.nom;
  c.nomsReseaux = std::move(figes);
  int n = 0;
  auto suivre = [&](std::string& reseau) {
    if (reseau.empty()) return;
    const std::string actuel = k.canonique(reseau);
    if (actuel != reseau) { reseau = actuel; ++n; }
  };
  for (auto& p : c.circuit.pistes) suivre(p.reseau);
  for (auto& v : c.circuit.vias) suivre(v.reseau);
  for (auto& z : c.circuit.zones) suivre(z.reseau);
  // Affectations ramenées sur les noms actuels, dans leur ordre ; deux affectations qui visent désormais le même
  // réseau vers deux classes différentes gardent la plus contraignante, et le conflit est dit (R11)
  const Regles& R = c.circuit.regles;
  auto classeNommee = [&R](const std::string& nom) -> const ClasseReseau* {
    for (const auto& cl : R.classes)
      if (cl.nom == nom) return &cl;
    return nullptr;
  };
  AffectationsClasses classes;
  for (const auto& [reseau, cl] : R.classeReseau) {
    const std::string actuel = motifReseau(reseau) ? reseau : k.canonique(reseau);
    if (actuel != reseau) ++n;
    const auto deja = classes.find(actuel);
    if (deja == classes.end()) {
      classes[actuel] = cl;
      continue;
    }
    if (deja->second == cl) continue;
    const ClasseReseau* garde = plusStricte(classeNommee(deja->second), classeNommee(cl));
    const std::string retenue = garde ? garde->nom : deja->second;
    if (conflits)
      conflits->push_back("Le réseau « " + actuel + " » était affecté à « " + deja->second + " » et à « " + cl + " » : « " + retenue + " », la plus contraignante, est gardée.");
    deja->second = retenue;
  }
  c.circuit.regles.classeReseau = std::move(classes);
  return n;
}

std::vector<std::string> diagnosticAffectations(const Regles& r, const Connectivite& k) {
  std::vector<std::string> d;
  auto classeExiste = [&r](const std::string& nom) { return std::any_of(r.classes.begin(), r.classes.end(), [&](const ClasseReseau& c) { return c.nom == nom; }); };
  std::map<std::string, std::string> parReseauActuel;  // nom actuel → première classe qui le vise par son nom
  std::vector<std::string> motifsPrecedents;
  for (const auto& [nom, classe] : r.classeReseau) {
    if (!classeExiste(classe)) {
      d.push_back("l’affectation de « " + nom + " » vise la classe « " + classe + " », qui n’existe plus : les motifs s’appliquent à sa place");
      continue;
    }
    if (!motifReseau(nom)) {
      const std::string actuel = k.canonique(nom);
      if (k.indice(actuel) < 0) d.push_back("classe « " + classe + " » : le réseau « " + nom + " » n’existe pas");
      const auto [it, neuf] = parReseauActuel.emplace(actuel, classe);
      if (!neuf && it->second != classe)
        d.push_back("le réseau « " + actuel + " » est affecté à « " + it->second + " » et à « " + classe + " » (sous un ancien nom) : la mise à jour du circuit gardera la plus contraignante");
      continue;
    }
    // Motif : il doit prendre au moins un réseau qu'aucun nom exact ni aucun motif placé avant lui ne prend
    int pris = 0, libres = 0;
    for (const auto& res : k.reseaux) {
      if (!correspond(nom, res.nom)) continue;
      ++pris;
      const bool exact = r.classeReseau.count(res.nom) > 0;
      const bool avant = std::any_of(motifsPrecedents.begin(), motifsPrecedents.end(), [&](const std::string& m) { return correspond(m, res.nom); });
      if (!exact && !avant) ++libres;
    }
    if (pris == 0) d.push_back("classe « " + classe + " » : le motif « " + nom + " » ne désigne aucun réseau");
    else if (libres == 0) {
      std::string masquant;
      for (const auto& m : motifsPrecedents)
        for (const auto& res : k.reseaux)
          if (masquant.empty() && correspond(nom, res.nom) && correspond(m, res.nom)) masquant = m;
      d.push_back("classe « " + classe + " » : le motif « " + nom + " » est masqué" + (masquant.empty() ? std::string(" par des noms exacts") : " par « " + masquant + " », placé avant lui") +
                  " ; place-le plus haut dans la liste");
    }
    motifsPrecedents.push_back(nom);
  }
  return d;
}

Connectivite connectivite(const ConceptionElec& c) {
  Connectivite k;
  Unions u;
  std::vector<Element> elements;
  for (std::size_t f = 0; f < c.feuilles.size(); ++f) {
    const Feuille& F = c.feuilles[f];
    std::map<Pt, std::vector<int>> parPoint;
    std::vector<std::pair<int, const Fil*>> fils;
    auto nouveau = [&](Element e, Pt p) {
      const int id = u.ajouter();
      e.feuille = static_cast<int>(f);
      elements.push_back(std::move(e));
      parPoint[p].push_back(id);
      return id;
    };
    for (const auto& w : F.fils) {
      if (w.a == w.b) continue;
      Element fil;
      fil.genre = Genre::Fil;
      fil.uuid = w.uuid;
      const int id = nouveau(fil, w.a);
      parPoint[w.b].push_back(id);
      fils.push_back({id, &w});
    }
    for (const auto& s : F.symboles) {
      const DefSymbole* d = c.symbole(s.symbole);
      if (!d) continue;
      for (const auto& bp : brochesPlacees(s, *d)) {
        Element e;
        e.genre = d->alimentation ? Genre::Alim : d->drapeau ? Genre::Drapeau : Genre::Broche;
        e.uuid = s.uuid;
        e.composant = s.composant;
        e.broche = bp.numero;
        e.type = bp.broche->type;
        if (d->alimentation) e.nom = s.nomAlim.empty() ? d->nom : s.nomAlim;
        nouveau(e, bp.p);
      }
    }
    for (const auto& e : F.etiquettes) {
      Element x;
      x.genre = e.type == TypeEtiquette::Locale ? Genre::Locale : Genre::Globale;  // un connecteur hors feuille relie comme une globale
      x.uuid = e.uuid;
      x.nom = e.texte;
      nouveau(x, e.p);
    }
    for (auto& [p, v] : parPoint)
      for (std::size_t i = 1; i < v.size(); ++i) u.unir(v[0], v[i]);
    // Un point de connexion posé sur le milieu d'un fil s'y raccorde (jonction en T) ; deux fils qui se croisent
    // sans jonction restent séparés.
    IndexSpatial index(5 * NM_PAR_MM);
    for (std::size_t i = 0; i < fils.size(); ++i) {
      Boite b;
      b.etendre(fils[i].second->a);
      b.etendre(fils[i].second->b);
      index.inserer(static_cast<int>(i), b);
    }
    auto traversants = [&](Pt p) {
      std::vector<int> r;
      Boite b;
      b.etendre(p);
      for (int i : index.chercher(b)) {
        const Fil& w = *fils[static_cast<std::size_t>(i)].second;
        if (p != w.a && p != w.b && surSegment(p, w.a, w.b)) r.push_back(i);
      }
      return r;
    };
    std::vector<Pt> jonctionsAuto, libres;
    for (auto& [p, v] : parPoint) {
      const std::vector<int> t = traversants(p);
      for (int i : t) u.unir(v.front(), fils[static_cast<std::size_t>(i)].first);
      int branches = 2 * static_cast<int>(t.size()), finsDeFil = 0;
      for (int id : v) {
        if (elements[static_cast<std::size_t>(id)].genre == Genre::Fil) ++finsDeFil;
        ++branches;
      }
      if (finsDeFil + static_cast<int>(t.size()) > 0 && branches >= 3) jonctionsAuto.push_back(p);
      if (v.size() == 1 && t.empty() && elements[static_cast<std::size_t>(v.front())].genre == Genre::Fil) libres.push_back(p);
    }
    for (const auto& j : F.jonctions) {
      std::vector<int> ici = traversants(j.p);
      int premier = ici.empty() ? -1 : fils[static_cast<std::size_t>(ici.front())].first;
      if (const auto it = parPoint.find(j.p); it != parPoint.end()) premier = it->second.front();
      for (int i : ici) if (premier >= 0) u.unir(premier, fils[static_cast<std::size_t>(i)].first);
      libres.erase(std::remove(libres.begin(), libres.end(), j.p), libres.end());
    }
    for (const auto& n : F.nonConnexions) libres.erase(std::remove(libres.begin(), libres.end(), n.p), libres.end());
    k.jonctions[F.uuid] = jonctionsAuto;
    k.extremitesLibres[F.uuid] = libres;
  }
  // Noms : alimentations et étiquettes globales dans tout le projet, étiquettes locales dans leur feuille
  {
    std::map<std::string, int> globaux;
    std::map<std::pair<int, std::string>, int> locaux;
    for (std::size_t i = 0; i < elements.size(); ++i) {
      const Element& e = elements[i];
      if (e.genre == Genre::Alim || e.genre == Genre::Globale) {
        const auto [it, neuf] = globaux.emplace(e.nom, static_cast<int>(i));
        if (!neuf) u.unir(it->second, static_cast<int>(i));
      } else if (e.genre == Genre::Locale) {
        const auto [it, neuf] = locaux.emplace(std::make_pair(e.feuille, e.nom), static_cast<int>(i));
        if (!neuf) u.unir(it->second, static_cast<int>(i));
      }
    }
  }
  // Une même broche posée plusieurs fois (broche commune des unités d'un symbole, alimentation d'un circuit
  // logique) est un seul point électrique. Si deux de ses poses tombent sur des réseaux déjà formés (chacun avec
  // une autre broche ou un nom), elles ne sont pas réunies en silence : le conflit est relevé (2.33, ERC-42), et
  // les poses restées seules rejoignent la première
  std::vector<std::pair<NoeudBroche, std::vector<int>>> conflitsCommuns;
  {
    std::map<std::pair<std::string, std::string>, std::vector<int>> broches;
    for (std::size_t i = 0; i < elements.size(); ++i) {
      const Element& e = elements[i];
      if (e.genre == Genre::Broche && !e.composant.empty()) broches[{e.composant, e.broche}].push_back(static_cast<int>(i));
    }
    // Ce que chaque partie déjà formée relie : broches, alimentations, drapeaux, étiquettes
    std::vector<int> racine(elements.size());
    std::map<int, int> poids;
    for (std::size_t i = 0; i < elements.size(); ++i) {
      racine[i] = u.racine(static_cast<int>(i));
      if (elements[i].genre != Genre::Fil) ++poids[racine[i]];
    }
    // Seule la broche commune d'un symbole à plusieurs unités est gardée de l'union silencieuse : une borne posée
    // deux fois (atelier électrique, plan de bornier) ou deux points de connexion d'un même numéro sont un seul
    // conducteur, réunis comme avant
    auto plusieursUnites = [&c](const std::string& composant) {
      const ComposantElec* x = c.composant(composant);
      const DefSymbole* d = x ? c.symbole(x->symbole) : nullptr;
      return d && d->unites > 1;
    };
    for (const auto& [cle, ids] : broches) {
      if (ids.size() < 2) continue;
      if (!plusieursUnites(cle.first)) {
        for (std::size_t j = 1; j < ids.size(); ++j) u.unir(ids.front(), ids[j]);
        continue;
      }
      std::map<int, int> propres;  // racine → poses de cette broche dans la partie
      for (int id : ids) ++propres[racine[static_cast<std::size_t>(id)]];
      std::vector<int> reliees;  // une pose par partie qui relie autre chose
      for (const auto& [r, n] : propres)
        if (poids[r] > n)
          for (int id : ids)
            if (racine[static_cast<std::size_t>(id)] == r) {
              reliees.push_back(id);
              break;
            }
      if (reliees.size() >= 2) {
        conflitsCommuns.push_back({{cle.first, cle.second}, reliees});
        for (int id : ids)
          if (poids[racine[static_cast<std::size_t>(id)]] <= propres[racine[static_cast<std::size_t>(id)]]) u.unir(reliees.front(), id);
        continue;
      }
      for (std::size_t j = 1; j < ids.size(); ++j) u.unir(ids.front(), ids[j]);
    }
  }
  // Réseaux : une partie connexe par racine
  std::map<int, std::size_t> parRacine;
  struct Brut {
    Reseau r;
    std::set<std::string> alims, globales, locales;
    std::vector<int> elements;
  };
  std::vector<Brut> bruts;
  // Un connecteur amène l'alimentation de l'extérieur : ses broches passives sont des sources
  std::map<std::string, bool> connecteurs;
  auto estConnecteur = [&](const std::string& composant) {
    const auto it = connecteurs.find(composant);
    if (it != connecteurs.end()) return it->second;
    const ComposantElec* x = c.composant(composant);
    const DefSymbole* d = x ? c.symbole(x->symbole) : nullptr;
    const bool oui = d && (d->categorie == "Connecteurs" || d->prefixe == "J");
    connecteurs.emplace(composant, oui);
    return oui;
  };
  for (std::size_t i = 0; i < elements.size(); ++i) {
    const int rac = u.racine(static_cast<int>(i));
    auto [it, neuf] = parRacine.emplace(rac, bruts.size());
    if (neuf) bruts.emplace_back();
    Brut& b = bruts[it->second];
    b.elements.push_back(static_cast<int>(i));
    const Element& e = elements[i];
    switch (e.genre) {
      case Genre::Broche:
        if (!e.composant.empty()) b.r.broches.push_back({e.composant, e.broche});
        if (e.type == TypeBroche::AlimSortie || (e.type == TypeBroche::Passive && estConnecteur(e.composant))) b.r.alimente = true;
        break;
      case Genre::Drapeau: b.r.alimente = true; break;
      case Genre::Alim: b.alims.insert(e.nom); break;
      case Genre::Globale: b.globales.insert(e.nom); break;
      case Genre::Locale: b.locales.insert(e.nom); break;
      case Genre::Fil: break;
    }
  }
  auto refDe = [&](const NoeudBroche& n) {
    const ComposantElec* x = c.composant(n.composant);
    return x ? x->reference : std::string("?");
  };
  std::set<std::string> pris, explicites;
  for (const Brut& b : bruts)
    for (const auto* ens : {&b.alims, &b.globales, &b.locales}) explicites.insert(ens->begin(), ens->end());
  for (Brut& b : bruts) {
    // Noms distincts (EL-09) : le même nom posé par une alimentation et une étiquette, ou sur deux feuilles, compte une fois
    for (const auto* ens : {&b.alims, &b.globales, &b.locales})
      for (const auto& n : *ens)
        if (std::find(b.r.noms.begin(), b.r.noms.end(), n) == b.r.noms.end()) b.r.noms.push_back(n);
    b.r.alimentations.assign(b.alims.begin(), b.alims.end());
    std::sort(b.r.broches.begin(), b.r.broches.end(), [&](const NoeudBroche& x, const NoeudBroche& y) {
      const std::string rx = refDe(x), ry = refDe(y);
      if (rx != ry) return ordreNaturel(rx, ry);
      return ordreNaturel(x.broche, y.broche);
    });
    b.r.broches.erase(std::unique(b.r.broches.begin(), b.r.broches.end(), [](const NoeudBroche& x, const NoeudBroche& y) { return x.composant == y.composant && x.broche == y.broche; }),
                      b.r.broches.end());
    std::string nom;
    if (!b.alims.empty()) nom = *b.alims.begin();
    else if (!b.globales.empty()) nom = *b.globales.begin();
    else if (!b.locales.empty()) nom = *b.locales.begin();
    b.r.nomme = !nom.empty();
    // Noms figés de ses broches (EL-01) : le réseau garde le nom que le cuivre connaît, même si un repère change
    std::vector<std::string> figes;
    for (const auto& br : b.r.broches) {
      const auto it = c.nomsReseaux.find(br.composant + ":" + br.broche);
      if (it != c.nomsReseaux.end() && !it->second.empty() && std::find(figes.begin(), figes.end(), it->second) == figes.end()) figes.push_back(it->second);
    }
    std::sort(figes.begin(), figes.end(), ordreNaturel);
    if (nom.empty())
      for (const auto& f : figes)
        if (!explicites.count(f) && !pris.count(f)) { nom = f; break; }
    if (nom.empty()) nom = b.r.broches.empty() ? "N-libre" : "N-" + refDe(b.r.broches.front()) + "-" + b.r.broches.front().broche;
    for (const auto& f : figes)
      if (f != nom) b.r.alias.push_back(f);
    std::string unique = nom;
    for (int n = 2; pris.count(unique); ++n) unique = nom + "#" + std::to_string(n);
    pris.insert(unique);
    b.r.nom = unique;
  }
  std::vector<std::size_t> ordre(bruts.size());
  for (std::size_t i = 0; i < ordre.size(); ++i) ordre[i] = i;
  std::sort(ordre.begin(), ordre.end(), [&](std::size_t a, std::size_t b) { return ordreNaturel(bruts[a].r.nom, bruts[b].r.nom); });
  for (std::size_t n = 0; n < ordre.size(); ++n) k.parNom[bruts[ordre[n]].r.nom] = static_cast<int>(n);
  for (std::size_t n = 0; n < ordre.size(); ++n)
    for (const auto& a : bruts[ordre[n]].r.alias) k.parNom.emplace(a, static_cast<int>(n));  // un nom actuel prime sur un alias
  std::vector<int> reseauElement(elements.size(), -1);
  for (std::size_t n = 0; n < ordre.size(); ++n) {
    const Brut& b = bruts[ordre[n]];
    k.reseaux.push_back(b.r);
    for (int id : b.elements) {
      reseauElement[static_cast<std::size_t>(id)] = static_cast<int>(n);
      const Element& e = elements[static_cast<std::size_t>(id)];
      if (e.genre == Genre::Fil) k.parFil[e.uuid] = static_cast<int>(n);
      else if (e.genre == Genre::Broche && !e.composant.empty()) k.parBroche[{e.composant, e.broche}] = static_cast<int>(n);
      else if (e.genre == Genre::Locale || e.genre == Genre::Globale) k.parEtiquette[e.uuid] = static_cast<int>(n);
      else k.parSymbole[e.uuid] = static_cast<int>(n);
    }
  }
  for (const auto& [noeud, ids] : conflitsCommuns) {
    Connectivite::ConflitCommun x{noeud.composant, noeud.broche, {}};
    for (int id : ids)
      if (const int n = reseauElement[static_cast<std::size_t>(id)]; n >= 0) {
        const std::string& nom = k.reseaux[static_cast<std::size_t>(n)].nom;
        if (std::find(x.reseaux.begin(), x.reseaux.end(), nom) == x.reseaux.end()) x.reseaux.push_back(nom);
      }
    if (x.reseaux.size() >= 2) k.conflitsCommuns.push_back(std::move(x));
  }
  return k;
}

// ---------- Vérification électrique ----------
std::vector<Violation> verifierERC(const ConceptionElec& c, const Connectivite& k) {
  std::vector<Violation> v;
  auto ajouter = [&](Gravite g, const char* code, std::string message, std::string conseil, const std::string& feuille, Pt p, std::vector<std::string> objets = {}) {
    v.push_back({g, code, std::move(message), std::move(conseil), feuille, p, std::move(objets)});
  };
  // Où sont les broches (chaque pose, y compris les broches communes posées avec chaque unité, E8), et les marques
  // de non-connexion
  struct Lieu {
    std::string feuille;
    Pt p;
    const Broche* broche;
  };
  std::map<std::pair<std::string, std::string>, std::vector<Lieu>> lieux;
  std::map<std::string, std::set<Pt>> nc;
  std::set<std::string> poses;
  std::map<std::string, std::map<int, int>> unitesPosees;  // composant → unité → nombre de poses
  for (const auto& f : c.feuilles) {
    for (const auto& n : f.nonConnexions) nc[f.uuid].insert(n.p);
    for (const auto& s : f.symboles) {
      const DefSymbole* d = c.symbole(s.symbole);
      if (!d) {
        ajouter(Gravite::Erreur, "ERC-01", "Symbole inconnu : " + s.symbole, "Remplace ce symbole par un symbole de la bibliothèque.", f.uuid, s.position, {s.uuid});
        continue;
      }
      if (s.composant.empty()) continue;
      poses.insert(s.composant);
      ++unitesPosees[s.composant][d->unites > 1 ? s.unite : 1];
      for (const auto& bp : brochesPlacees(s, *d)) lieux[{s.composant, bp.numero}].push_back({f.uuid, bp.p, bp.broche});
    }
  }
  auto premierLieu = [&](const std::string& composant, std::string& feuille, Pt& ou) {
    for (const auto& [cle, ls] : lieux)
      if (cle.first == composant && !ls.empty()) {
        ou = ls.front().p;
        feuille = ls.front().feuille;
        return;
      }
  };
  // Références
  std::map<std::string, std::vector<const ComposantElec*>> parRef;
  for (const auto& x : c.composants) {
    if (!poses.count(x.uuid)) continue;
    parRef[x.reference].push_back(&x);
    Pt ou;
    std::string feuille;
    premierLieu(x.uuid, feuille, ou);
    const DefSymbole* d = c.symbole(x.symbole);
    if (x.reference.empty() || x.reference.find('?') != std::string::npos)
      ajouter(Gravite::Erreur, "ERC-02", "Composant non annoté : " + (x.reference.empty() ? std::string("sans référence") : x.reference), "Lance « Annoter » : chaque composant reçoit sa référence (R1, R2…).", feuille, ou, {x.uuid});
    // Symbole à plusieurs unités : chacune posée une fois ; un composant à une seule unité, une fois aussi (E7)
    if (d && d->unites > 1) {
      const auto& posees = unitesPosees[x.uuid];
      for (int u = 1; u <= d->unites; ++u) {
        const auto it = posees.find(u);
        if (it == posees.end()) {
          // Une unité qui porte des entrées ou des alimentations est une erreur (2.33, ERC-01) : ses entrées
          // flotteraient, ou une partie du circuit ne serait pas alimentée ; sans elles, un avertissement
          bool grave = false;
          for (const auto& b : d->broches)
            grave |= b.unite == u && (b.type == TypeBroche::Entree || b.type == TypeBroche::AlimEntree);
          ajouter(grave ? Gravite::Erreur : Gravite::Avertissement, "ERC-40", x.reference + lettreUnite(u) + " : unité non posée" + (grave ? std::string(" (elle porte des entrées ou des alimentations)") : std::string()),
                  grave ? "Pose-la (« Poser l’unité suivante ») et relie ses entrées : une entrée d’ampli ou de porte laissée en l’air oscille ou consomme ; ou relie-les à un potentiel fixe."
                        : "Pose-la (« Poser l’unité suivante » dans le panneau du composant) : ses broches resteraient en l’air.",
                  feuille, ou, {x.uuid});
        }
        else if (it->second > 1)
          ajouter(Gravite::Erreur, "ERC-41", x.reference + lettreUnite(u) + " posée " + std::to_string(it->second) + " fois", "Supprime le double, ou change l’unité de l’un d’eux dans le panneau.", feuille, ou, {x.uuid});
      }
    } else if (const int n = unitesPosees[x.uuid][1]; n > 1)
      ajouter(Gravite::Erreur, "ERC-41", x.reference + " posé " + std::to_string(n) + " fois", "Un composant à une seule unité n’a qu’un symbole : supprime le double, ses broches seraient reliées en silence.",
              feuille, ou, {x.uuid});
    // Bloquant (EL-04) : un composant sans empreinte serait dans la nomenclature mais pas sur la carte
    if (d && !d->mecanique && x.empreinte.empty() && !x.horsCarte)
      ajouter(Gravite::Erreur, "ERC-03", x.reference + " n’a pas d’empreinte", "Choisis une empreinte dans le panneau du composant, ou coche « hors carte » s’il ne va pas sur la carte.", feuille, ou, {x.uuid});
    if (d && !x.empreinte.empty()) {
      if (const DefEmpreinte* e = c.empreinte(x.empreinte)) {
        // Les numéros réels de chaque pose, pas ceux de la définition (E9)
        std::set<std::string> numeros;
        for (const auto& p : e->pastilles) numeros.insert(p.numero);
        for (const auto& [cle, ls] : lieux)
          if (cle.first == x.uuid && !numeros.count(cle.second))
            ajouter(Gravite::Erreur, "ERC-04", x.reference + " : l’empreinte " + x.empreinte + " n’a pas de pastille « " + cle.second + " »", "Choisis une empreinte qui a autant de broches que le symbole.", feuille, ou,
                    {x.uuid});
        // ERC-07 (E4) : pastille numérotée sans broche au symbole ; elle ne serait reliée à rien (pastille thermique
        // d'un QFN, troisième pastille d'un SOT-23 donné à une diode). Pastilles mécaniques (sans numéro, « MP »,
        // « SH », non métallisées) exceptées.
        std::set<std::string> broches;
        for (const auto& b : d->broches) broches.insert(b.numero);
        for (const auto& [cle, ls] : lieux)
          if (cle.first == x.uuid) broches.insert(cle.second);
        std::set<std::string> dites;
        for (const auto& p : e->pastilles) {
          if (p.numero.empty() || p.numero == "MP" || p.numero == "SH" || p.numero == "~" || p.type == TypePastille::NonMetallisee) continue;
          if (broches.count(p.numero) || !dites.insert(p.numero).second) continue;
          ajouter(Gravite::Avertissement, "ERC-07", x.reference + " : la pastille « " + p.numero + " » de " + x.empreinte + " n’a pas de broche au symbole : elle ne sera reliée à rien",
                  "Ajoute la broche au symbole (pastille thermique à la masse, le plus souvent), ou choisis une empreinte qui a autant de pastilles que le symbole de broches.", feuille, ou,
                  {x.uuid});
        }
      } else
        ajouter(Gravite::Erreur, "ERC-05", x.reference + " : empreinte inconnue « " + x.empreinte + " »", "Choisis une empreinte du catalogue.", feuille, ou, {x.uuid});
      // Garde-fou du brochage (B17) : broche et pastille ne se correspondent que par leur numéro ; une autre famille
      // de boîtier peut inverser drain et source sans que rien d'autre ne le voie
      if (brochageAVerifier(c, x)) {
        const std::string famille = familleBoitier(d->empreinte);
        ajouter(Gravite::Erreur, "ERC-09",
                x.reference + " : le symbole « " + d->nom + " » est numéroté pour " + d->empreinte + (famille.empty() ? std::string() : " (" + famille + ")") + ", son empreinte « " +
                    x.empreinte + " » est d’une autre famille de boîtier : une broche peut arriver sur la mauvaise pastille",
                "Vérifie sur la fiche technique que chaque broche du symbole arrive sur la pastille de même fonction, puis coche « Brochage vérifié » dans le panneau du composant ; sinon reprends l’empreinte " +
                    d->empreinte + ".",
                feuille, ou, {x.uuid});
      }
    }
    // ERC-08 (4.11) : un passif posé sur l'empreinte d'une autre sorte (une inductance sur un fusible) ; le
    // brochage tient, la fonction non
    if (d && !x.empreinte.empty() && !d->empreinte.empty()) {
      static const std::set<std::string> passifs{"R", "C", "CP", "L", "Fuse", "LED"};
      const std::string sa = sorteEmpreinte(d->empreinte), sb = sorteEmpreinte(x.empreinte);
      const bool condensateurs = (sa == "C" || sa == "CP") && (sb == "C" || sb == "CP");
      if (passifs.count(sa) && passifs.count(sb) && sa != sb && !condensateurs)
        ajouter(Gravite::Avertissement, "ERC-08",
                x.reference + " : « " + d->nom + " » posé sur l’empreinte « " + x.empreinte + " », écrite pour une autre sorte de composant",
                "Choisis une empreinte de sa sorte (filtre « compatibles » de la fenêtre de choix), ou confirme l’exception en l’excluant de l’ERC.", feuille, ou, {x.uuid});
    }
    // ERC-44 (4.21) : composant générique provisoire, jusqu'à son remplacement par un article réel
    if (const auto ia = c.articles.find(x.article); ia != c.articles.end() && ia->second.generique)
      ajouter(Gravite::Erreur, "ERC-44", x.reference + " : composant générique « " + ia->second.exigences + " » à remplacer par un article réel",
              "Ouvre la standardisation : elle propose l’article que la recherche trouve pour ces exigences et le pose en une commande.", feuille, ou, {x.uuid});
    // Valeur vide sur un composant à monter (E10, relevé dès le schéma)
    if (d && !d->mecanique && !d->alimentation && !d->drapeau && !x.nePasMonter && !x.horsNomenclature && x.valeur.empty())
      ajouter(Gravite::Avertissement, "ERC-26", x.reference + " n’a pas de valeur", "Donne-lui sa valeur (double-clic sur le symbole) : la nomenclature en a besoin.", feuille, ou, {x.uuid});
  }
  for (const auto& [ref, liste] : parRef)
    if (liste.size() > 1 && ref.find('?') == std::string::npos && !ref.empty())
      ajouter(Gravite::Erreur, "ERC-06", "Référence en double : " + ref + " (" + std::to_string(liste.size()) + " composants)", "Lance « Annoter » en renumérotant tout, ou corrige une des références.", {}, {}, {liste.front()->uuid});
  // Broches non connectées : une broche commune compte toutes ses poses (E8)
  for (const auto& [cle, ls] : lieux) {
    const Lieu& l = ls.front();
    if (l.broche->type == TypeBroche::NonConnectee) {
      // Broche que le fabricant déclare « non connectée » mais reliée à autre chose (EL-14)
      const int r = k.reseau(cle.first, cle.second);
      if (r >= 0 && (k.reseaux[static_cast<std::size_t>(r)].broches.size() > 1 || k.reseaux[static_cast<std::size_t>(r)].nomme)) {
        const ComposantElec* x = c.composant(cle.first);
        ajouter(Gravite::Avertissement, "ERC-12", "Broche de type « non connectée » reliée : " + (x ? x->reference : std::string("?")) + " broche " + cle.second +
                (l.broche->nom.empty() || l.broche->nom == "~" ? std::string() : " (" + l.broche->nom + ")"),
                "Le fabricant la déclare sans liaison interne : retire le fil, ou vérifie le brochage du composant.", l.feuille, l.p, {cle.first});
      }
      continue;
    }
    const int r = k.reseau(cle.first, cle.second);
    const bool seule = r < 0 || (k.reseaux[static_cast<std::size_t>(r)].broches.size() <= 1 && !k.reseaux[static_cast<std::size_t>(r)].nomme);
    const Lieu* marque = nullptr;
    for (const Lieu& x : ls)
      if (nc.count(x.feuille) && nc.at(x.feuille).count(x.p)) marque = &x;
    const ComposantElec* x = c.composant(cle.first);
    const std::string nom = (x ? x->reference : std::string("?")) + " broche " + cle.second + (l.broche->nom.empty() || l.broche->nom == "~" ? std::string() : " (" + l.broche->nom + ")");
    if (seule && !marque) {
      const bool grave = l.broche->type == TypeBroche::Entree || l.broche->type == TypeBroche::AlimEntree;
      ajouter(grave ? Gravite::Erreur : Gravite::Avertissement, "ERC-10", "Broche non connectée : " + nom, "Relie-la, ou pose une marque de non-connexion (touche Q) si elle doit rester libre.", l.feuille, l.p, {cle.first});
    } else if (!seule && marque)
      ajouter(Gravite::Avertissement, "ERC-11", "Broche marquée « non connectée » mais reliée : " + nom, "Retire la marque, ou le fil.", marque->feuille, marque->p, {cle.first});
  }
  // Broche commune à plusieurs unités reliée à deux réseaux (2.33, ERC-04) : nommée, jamais réunie en silence
  for (const auto& x : k.conflitsCommuns) {
    const ComposantElec* comp = c.composant(x.composant);
    std::string feuille;
    Pt ou;
    premierLieu(x.composant, feuille, ou);
    std::string noms;
    for (const auto& n : x.reseaux) noms += (noms.empty() ? "" : " et ") + n;
    ajouter(Gravite::Erreur, "ERC-42", "Broche commune " + (comp ? comp->reference : std::string("?")) + "." + x.broche + " reliée à deux réseaux : " + noms,
            "Une broche commune à plusieurs unités est un seul point du boîtier : relie-la au même réseau sur chaque unité, ou sur une seule.", feuille, ou, {x.composant});
  }
  // Projet (2.33) : une même référence fabricant posée avec deux empreintes (un passif sans valeur : ERC-26)
  {
    std::map<std::string, std::set<std::string>> empreintesParMpn;
    std::map<std::string, const ComposantElec*> premierParMpn;
    for (const auto& x : c.composants) {
      if (!poses.count(x.uuid)) continue;
      if (!x.mpn.empty() && !x.empreinte.empty()) {
        empreintesParMpn[x.mpn].insert(x.empreinte);
        premierParMpn.emplace(x.mpn, &x);
      }
    }
    for (const auto& [mpn, empreintes] : empreintesParMpn)
      if (empreintes.size() >= 2) {
        std::string liste, feuille;
        for (const auto& e : empreintes) liste += (liste.empty() ? "" : ", ") + e;
        Pt ou;
        premierLieu(premierParMpn[mpn]->uuid, feuille, ou);
        ajouter(Gravite::Avertissement, "ERC-43", "La référence " + mpn + " est posée avec plusieurs empreintes (" + liste + ")",
                "Une même pièce achetée n’a qu’un boîtier : corrige l’empreinte ou la référence fabricant des composants concernés.", feuille, ou, {premierParMpn[mpn]->uuid});
      }
  }
  // Réseaux alimentés : un passif déclaré traversant (ferrite, shunt, 0 Ω) ou une liaison de réseaux fait passer
  // l'alimentation (E3)
  std::vector<bool> alimente(k.reseaux.size(), false);
  for (std::size_t i = 0; i < k.reseaux.size(); ++i) alimente[i] = k.reseaux[i].alimente || !k.reseaux[i].alimentations.empty();
  for (bool encore = true; encore;) {
    encore = false;
    for (const auto& x : c.composants) {
      const DefSymbole* d = c.symbole(x.symbole);
      if (!d || !(d->traversant || d->liaisonReseaux) || d->broches.size() != 2 || !poses.count(x.uuid)) continue;
      const int a = k.reseau(x.uuid, d->broches[0].numero), b = k.reseau(x.uuid, d->broches[1].numero);
      if (a < 0 || b < 0) continue;
      if (alimente[static_cast<std::size_t>(a)] != alimente[static_cast<std::size_t>(b)]) {
        alimente[static_cast<std::size_t>(a)] = alimente[static_cast<std::size_t>(b)] = true;
        encore = true;
      }
    }
  }
  // Pilotage (2.33, ERC-08) : une sortie, une alimentation, une broche bidirectionnelle ou à trois états pilote son
  // réseau, une broche de connecteur aussi (le signal arrive de l'extérieur), un passif qui n'est pas en série
  // (condensateur, diode) comme avant ; un passif en série (résistance, inductance, fusible, interrupteur,
  // traversant) ne pilote pas, il transmet : le réseau qu'il relie à un réseau piloté l'est aussi
  std::set<std::string> enSerie;
  for (const auto& x : c.composants) {
    const DefSymbole* d = c.symbole(x.symbole);
    static const std::set<std::string> prefixes{"R", "L", "F", "FB", "SW", "S", "JP"};
    if (d && poses.count(x.uuid) && d->broches.size() == 2 && (d->traversant || prefixes.count(d->prefixe))) enSerie.insert(x.uuid);
  }
  std::vector<bool> pilote(k.reseaux.size(), false);
  for (std::size_t ir = 0; ir < k.reseaux.size(); ++ir) {
    const Reseau& r = k.reseaux[ir];
    bool p = alimente[ir] || !r.alimentations.empty();
    for (const auto& n : r.broches) {
      const auto it = lieux.find({n.composant, n.broche});
      if (it == lieux.end() || it->second.empty()) continue;
      switch (it->second.front().broche->type) {
        case TypeBroche::Entree:
        case TypeBroche::AlimEntree:
        case TypeBroche::NonConnectee: break;
        case TypeBroche::Passive: p |= !enSerie.count(n.composant); break;
        default: p = true; break;
      }
    }
    pilote[ir] = p;
  }
  for (bool encore = true; encore;) {
    encore = false;
    for (const std::string& id : enSerie) {
      const ComposantElec* x = c.composant(id);
      const DefSymbole* d = x ? c.symbole(x->symbole) : nullptr;
      if (!d) continue;
      const int a = k.reseau(id, d->broches[0].numero), b = k.reseau(id, d->broches[1].numero);
      if (a < 0 || b < 0) continue;
      if (pilote[static_cast<std::size_t>(a)] != pilote[static_cast<std::size_t>(b)]) {
        pilote[static_cast<std::size_t>(a)] = pilote[static_cast<std::size_t>(b)] = true;
        encore = true;
      }
    }
  }
  // Réseaux : conflits de broches, entrées sans pilote, alimentation absente, courts-circuits, alias
  for (std::size_t ir = 0; ir < k.reseaux.size(); ++ir) {
    const Reseau& r = k.reseaux[ir];
    int entrees = 0, alimEntrees = 0, pilotes = 0;
    Lieu premier{};
    bool trouve = false;
    std::vector<TypeBroche> types;
    for (const auto& n : r.broches) {
      const auto it = lieux.find({n.composant, n.broche});
      if (it == lieux.end() || it->second.empty()) continue;
      const Lieu& l = it->second.front();
      if (!trouve) {
        premier = l;
        trouve = true;
      }
      if (l.broche->type != TypeBroche::NonConnectee) types.push_back(l.broche->type);
      switch (l.broche->type) {
        case TypeBroche::Sortie:
        case TypeBroche::AlimSortie: ++pilotes; break;
        case TypeBroche::Entree: ++entrees; break;
        case TypeBroche::AlimEntree: ++alimEntrees; break;
        case TypeBroche::NonConnectee: break;
        default: ++pilotes; break;
      }
    }
    if (!trouve) continue;
    // Conflits de types de broches, deux à deux, selon la matrice (E1) ; la broche « non connectée » relève d'ERC-12
    {
      char pire = '.';
      TypeBroche ta = TypeBroche::Passive, tb = TypeBroche::Passive;
      for (std::size_t i = 0; i < types.size(); ++i)
        for (std::size_t j = i + 1; j < types.size(); ++j) {
          const char x = conflitBroches(c, types[i], types[j]);
          if ((x == 'E' && pire != 'E') || (x == 'A' && pire == '.')) {
            pire = x;
            ta = types[i];
            tb = types[j];
          }
        }
      if (pire != '.') {
        std::string a = nomTypeBroche(ta), b = nomTypeBroche(tb);
        for (auto* t : {&a, &b})
          if (!t->empty()) (*t)[0] = static_cast<char>(std::tolower(static_cast<unsigned char>((*t)[0])));
        if (ta == TypeBroche::Sortie && tb == TypeBroche::Sortie)
          ajouter(pire == 'E' ? Gravite::Erreur : Gravite::Avertissement, "ERC-20", "Plusieurs sorties reliées entre elles sur « " + r.nom + " »", "Deux sorties qui se contredisent peuvent se détruire : sépare-les.", premier.feuille, premier.p);
        else
          ajouter(pire == 'E' ? Gravite::Erreur : Gravite::Avertissement, "ERC-20", "Conflit de broches sur « " + r.nom + " » : " + a + " contre " + b,
                  "Vérifie le type des broches reliées ; si le montage est voulu, règle la case de la matrice des conflits du projet.", premier.feuille, premier.p);
      }
    }
    // Un nom ne pilote rien : deux entrées reliées par une étiquette restent sans pilote (EL-14) ; un symbole
    // d'alimentation, lui, amène une tension
    if (entrees > 0 && alimEntrees == 0 && !pilote[ir])
      ajouter(Gravite::Erreur, "ERC-21", "Entrée non pilotée sur « " + r.nom + " »" + (pilotes > 0 ? std::string(" : le passif en série ne mène à rien qui pilote") : std::string()),
              "Relie-la à une sortie, à une résistance de tirage ou à une alimentation.", premier.feuille, premier.p);
    if (alimEntrees > 0 && !alimente[ir])
      ajouter(Gravite::Erreur, "ERC-22", "Rien n’alimente le réseau « " + r.nom + " »", "Relie un connecteur, un régulateur ou une pile ; si l’alimentation arrive d’ailleurs, pose un drapeau d’alimentation.", premier.feuille, premier.p);
    // Deux alimentations différentes sur un même réseau : vrai court-circuit, bloquant. Deux noms différents dont
    // au plus une alimentation : un alias (VCC_5V posé sur +5V), signalé mais dérogeable (EL-09)
    auto listeNoms = [](const std::vector<std::string>& l) {
      std::string t;
      for (const auto& n : l) t += (t.empty() ? "" : ", ") + n;
      return t;
    };
    if (r.alimentations.size() >= 2)
      ajouter(Gravite::Erreur, "ERC-23", "Court-circuit : des alimentations différentes sont reliées (" + listeNoms(r.alimentations) + ")",
              "Vérifie les fils et les symboles d’alimentation : deux tensions ne peuvent pas partager un réseau ; pour une masse en étoile voulue, pose une liaison de réseaux.", premier.feuille, premier.p);
    else if (r.noms.size() >= 2)
      ajouter(Gravite::Avertissement, "ERC-24", "Réseau à plusieurs noms (" + listeNoms(r.noms) + ") : il s’appelle « " + r.nom + " »",
              "Si c’est voulu (alias), rien à faire ; sinon, une étiquette relie par erreur deux réseaux.", premier.feuille, premier.p);
  }
  // Contrôles de dessin, feuille par feuille
  std::map<std::string, std::vector<std::pair<std::size_t, const Etiquette*>>> parTexte;  // étiquettes par texte
  for (std::size_t fi = 0; fi < c.feuilles.size(); ++fi)
    for (const auto& e : c.feuilles[fi].etiquettes) parTexte[e.texte].push_back({fi, &e});
  for (std::size_t fi = 0; fi < c.feuilles.size(); ++fi) {
    const Feuille& f = c.feuilles[fi];
    if (const auto it = k.extremitesLibres.find(f.uuid); it != k.extremitesLibres.end())
      for (const Pt& p : it->second) ajouter(Gravite::Avertissement, "ERC-30", "Fil qui ne mène nulle part", "Prolonge-le jusqu’à une broche, ou supprime-le.", f.uuid, p);
    // Points de connexion de la feuille
    std::map<Pt, std::vector<std::string>> brochesIci;  // point → symboles dont une broche y tombe
    std::set<Pt> boutsDeFil;
    for (const auto& w : f.fils) {
      boutsDeFil.insert(w.a);
      boutsDeFil.insert(w.b);
    }
    auto surUnFil = [&](Pt p, bool interieur) {
      for (const auto& w : f.fils)
        if (surSegment(p, w.a, w.b) && (!interieur || (p != w.a && p != w.b))) return true;
      return false;
    };
    const Boite cadre = cadreFeuille(f.format);
    auto horsGrille = [](Pt p) { return p.x % GRILLE_SCHEMA != 0 || p.y % GRILLE_SCHEMA != 0; };
    std::size_t nHorsGrille = 0;
    Pt premierHors{};
    for (const auto& s : f.symboles) {
      const DefSymbole* d = c.symbole(s.symbole);
      if (!d) continue;
      const ComposantElec* x = c.composant(s.composant);
      const std::string ref = x ? x->reference : d->alimentation ? (s.nomAlim.empty() ? d->nom : s.nomAlim) : d->nom;
      const auto bp = brochesPlacees(s, *d);
      for (const auto& b : bp) {
        brochesIci[b.p].push_back(s.uuid);
        // Broche raccordée au milieu d'un fil (S30)
        if (!d->alimentation && surUnFil(b.p, true))
          ajouter(Gravite::Avertissement, "ERC-14", "Broche " + b.numero + " de " + ref + " reliée au milieu d’un fil", "Arrête le fil sur la broche, ou déplace " + ref + " : une connexion au milieu d’un fil se voit mal.",
                  f.uuid, b.p, {s.composant.empty() ? s.uuid : s.composant});
      }
      // Symbole d'alimentation qui ne touche rien (E10)
      if (d->alimentation || d->drapeau) {
        bool touche = false;
        for (const auto& b : bp) touche |= boutsDeFil.count(b.p) || surUnFil(b.p, false);
        if (!touche)
          for (const auto& b : bp)
            for (const auto& s2 : f.symboles)
              if (&s2 != &s)
                if (const DefSymbole* d2 = c.symbole(s2.symbole))
                  for (const auto& b2 : brochesPlacees(s2, *d2)) touche |= b2.p == b.p;
        if (!touche)
          ajouter(Gravite::Avertissement, "ERC-17", "Symbole d’alimentation « " + ref + " » posé dans le vide", "Relie-le par un fil à une broche, ou supprime-le.", f.uuid, s.position, {s.uuid});
      }
      // Hors du cadre de la feuille
      if (!cadre.contient(boiteSymbole(s, *d)))
        ajouter(Gravite::Avertissement, "ERC-15", ref + " est hors du cadre de la feuille", "Ramène-le dans le cadre : il ne serait pas imprimé.", f.uuid, s.position, {s.composant.empty() ? s.uuid : s.composant});
      if (horsGrille(s.position) && nHorsGrille++ == 0) premierHors = s.position;
    }
    for (const auto& w : f.fils)
      for (const Pt p : {w.a, w.b})
        if (horsGrille(p) && nHorsGrille++ == 0) premierHors = p;
    for (const auto& e : f.etiquettes)
      if (horsGrille(e.p) && nHorsGrille++ == 0) premierHors = e.p;
    if (nHorsGrille > 0)
      ajouter(Gravite::Avertissement, "ERC-19", std::to_string(nHorsGrille) + " point(s) hors de la grille de 1,27 mm sur « " + f.nom + " »",
              "Un point hors de la grille ne rejoint pas les broches : déplace l’objet d’un pas, il se recalera.", f.uuid, premierHors);
    // Broches de deux symboles au même point (E10) : reliées sans fil, souvent par mégarde
    for (const auto& [p, symboles] : brochesIci) {
      std::set<std::string> distincts(symboles.begin(), symboles.end());
      if (distincts.size() >= 2) ajouter(Gravite::Avertissement, "ERC-18", "Broches de deux symboles au même point", "Écarte les symboles et relie-les par un fil : la liaison se verra.", f.uuid, p);
    }
    for (const auto& e : f.etiquettes) {
      // Étiquette qui ne touche ni fil ni broche
      const bool touche = boutsDeFil.count(e.p) || surUnFil(e.p, true) || brochesIci.count(e.p);
      if (!touche) ajouter(Gravite::Avertissement, "ERC-16", "Étiquette « " + e.texte + " » posée dans le vide", "Pose-la sur un fil ou sur le bout d’une broche.", f.uuid, e.p, {e.uuid});
      const auto it = k.parEtiquette.find(e.uuid);
      if (it == k.parEtiquette.end()) continue;
      const Reseau& r = k.reseaux[static_cast<std::size_t>(it->second)];
      // Comptées par portée (E6) : une locale sur sa feuille, une globale dans tout le projet
      int memes = 0;
      const bool locale = e.type == TypeEtiquette::Locale;
      for (const auto& [fj, x] : parTexte[e.texte]) memes += locale ? (fj == fi && x->type == TypeEtiquette::Locale) : x->type != TypeEtiquette::Locale;
      // Une étiquette locale posée une fois pour nommer un réseau relié est normale (EL-32)
      if (touche && (r.broches.empty() || (memes == 1 && (!locale || r.broches.size() <= 1))))
        ajouter(Gravite::Avertissement, "ERC-31", "Étiquette « " + e.texte + " » utilisée une seule fois", "Une étiquette relie les fils qui portent le même nom : pose-la aussi à l’autre bout.", f.uuid, e.p, {e.uuid});
      // Même texte, portées différentes, réseaux différents (E6)
      if (locale)
        for (const auto& [fj, x] : parTexte[e.texte])
          if (x->type != TypeEtiquette::Locale)
            if (const auto jt = k.parEtiquette.find(x->uuid); jt != k.parEtiquette.end() && jt->second != it->second) {
              ajouter(Gravite::Avertissement, "ERC-25", "« " + e.texte + " » locale et « " + e.texte + " » globale ne sont pas reliées",
                      "Une étiquette locale ne rejoint pas une globale du même nom : rends-les du même genre, ou renomme l’une d’elles.", f.uuid, e.p, {e.uuid, x->uuid});
              break;
            }
    }
  }
  // Marques de non-connexion qui ne touchent ni broche ni bout de fil (EL-14)
  for (const auto& f : c.feuilles) {
    if (f.nonConnexions.empty()) continue;
    std::set<Pt> ancrages;
    for (const auto& [cle, ls] : lieux)
      for (const Lieu& l : ls)
        if (l.feuille == f.uuid) ancrages.insert(l.p);
    for (const auto& w : f.fils) { ancrages.insert(w.a); ancrages.insert(w.b); }
    for (const auto& n : f.nonConnexions)
      if (!ancrages.count(n.p))
        ajouter(Gravite::Avertissement, "ERC-13", "Marque de non-connexion posée dans le vide", "Pose-la sur le bout de la broche à laisser libre, ou supprime-la.", f.uuid, n.p);
  }
  for (auto& x : v) x.niveau = niveauErc(x.code);
  std::stable_sort(v.begin(), v.end(), [](const Violation& a, const Violation& b) { return a.gravite < b.gravite; });
  return v;
}

// ---------- ERC par niveau, matrice des conflits, gravités réglées, exclusions (2.11) ----------
NiveauErc niveauErc(const std::string& code) {
  static const std::set<std::string> immediat{"ERC-01", "ERC-05", "ERC-06", "ERC-23", "ERC-41", "ERC-42"};
  static const std::set<std::string> dessin{"ERC-02", "ERC-10", "ERC-11", "ERC-13", "ERC-14", "ERC-16", "ERC-17", "ERC-18", "ERC-30", "ERC-31"};
  if (immediat.count(code)) return NiveauErc::Immediat;
  if (dessin.count(code)) return NiveauErc::Dessin;
  return NiveauErc::Complet;
}

char conflitBroches(const ConceptionElec& c, TypeBroche a, TypeBroche b) {
  // Matrice par défaut (E1) ; ordre : entrée, sortie, bidirectionnelle, trois états, passive, alimentation entrée,
  // alimentation sortie, collecteur ouvert, non connectée, non spécifiée
  static constexpr const char* M[10] = {".", ".E", "...", ".A..", ".....", ".A.A..", ".EAE..E", ".E.A..E.", "AAAAAAAAA", "AAAAAAAAAA"};
  const auto it = c.erc.matrice.find(std::string(codeTypeBroche(a)) + "/" + codeTypeBroche(b));
  const auto jt = it != c.erc.matrice.end() ? it : c.erc.matrice.find(std::string(codeTypeBroche(b)) + "/" + codeTypeBroche(a));
  if (jt != c.erc.matrice.end()) return jt->second == "erreur" ? 'E' : jt->second == "avertissement" ? 'A' : '.';
  const int i = static_cast<int>(a), j = static_cast<int>(b);
  return i >= j ? M[i][j] : M[j][i];
}

const ExclusionErc* exclusionDe(const ConceptionElec& c, const Violation& v) {
  for (const auto& e : c.erc.exclusions) {
    if (e.code != v.code && e.code != "*") continue;
    if (e.objets.empty()) return &e;
    // Exclusion d'une feuille entière (STR-08) : toute violation posée sur elle
    if (!v.feuille.empty() && std::find(e.objets.begin(), e.objets.end(), v.feuille) != e.objets.end()) return &e;
    // Exclusion d'un composant pour tous les contrôles (« * ») : toute violation qui le vise
    if (e.code == "*" && std::any_of(v.objets.begin(), v.objets.end(), [&](const std::string& o) { return std::find(e.objets.begin(), e.objets.end(), o) != e.objets.end(); })) return &e;
    // L'exclusion vise des objets : la violation en porte au moins un, et tous les siens sont visés
    bool tous = !v.objets.empty();
    for (const auto& o : v.objets) tous &= std::find(e.objets.begin(), e.objets.end(), o) != e.objets.end();
    if (tous) return &e;
  }
  return nullptr;
}

std::vector<Violation> verifierERC(const ConceptionElec& c, const Connectivite& k, NiveauErc jusqua) {
  std::vector<Violation> r;
  for (Violation v : verifierERC(c, k)) {
    v.niveau = niveauErc(v.code);
    if (v.niveau > jusqua) continue;
    if (const auto it = c.erc.gravites.find(v.code); it != c.erc.gravites.end()) {
      if (it->second == GraviteReglee::Ignorer) continue;
      v.gravite = it->second == GraviteReglee::Erreur ? Gravite::Erreur : Gravite::Avertissement;
    }
    if (exclusionDe(c, v)) continue;
    r.push_back(std::move(v));
  }
  return r;
}

std::vector<Violation> verifierDRCReglee(const ConceptionElec& c, const Connectivite& k, const Remplissages& remplis) {
  std::vector<Violation> r;
  for (Violation v : verifierDRC(c, k, remplis)) {
    if (const auto it = c.erc.gravites.find(v.code); it != c.erc.gravites.end()) {
      if (it->second == GraviteReglee::Ignorer) continue;
      v.gravite = it->second == GraviteReglee::Erreur ? Gravite::Erreur : Gravite::Avertissement;
    }
    if (exclusionDe(c, v)) continue;
    r.push_back(std::move(v));
  }
  return r;
}

bool brochageAVerifier(const ConceptionElec& c, const ComposantElec& x) {
  if (x.empreinte.empty() || x.brochageVerifie == x.empreinte) return false;
  // Les tables de l'article (4.11) : son brochage est écrit pour son empreinte
  if (const auto ia = c.articles.find(x.article); ia != c.articles.end() && ia->second.empreinte == x.empreinte && !ia->second.generique) return false;
  const DefSymbole* d = c.symbole(x.symbole);
  if (!d || d->empreinte.empty() || d->empreinte == x.empreinte || d->alimentation || d->drapeau || d->mecanique || d->broches.size() < 2) return false;
  auto numerotees = [&c](const std::string& id) {
    std::set<std::string> n;
    if (const DefEmpreinte* e = c.empreinte(id))
      for (const auto& p : e->pastilles)
        if (!p.numero.empty()) n.insert(p.numero);
    return n.size();
  };
  const std::string fa = familleBoitier(d->empreinte), fb = familleBoitier(x.empreinte);
  if (fa.empty() || fa != fb) return true;
  // Même famille : le brochage ne suit que si le nombre de pastilles est le même (une LED RGB à quatre pastilles
  // n'est pas une LED à deux) ; une empreinte introuvable ne se compte pas
  const std::size_t na = numerotees(d->empreinte), nb = numerotees(x.empreinte);
  return na > 0 && nb > 0 && na != nb;
}

std::vector<ChangementReference> annotationPrevue(const ConceptionElec& c, bool tout) {
  struct Ordre {
    std::size_t feuille;
    Nm y, x;
  };
  std::map<std::string, Ordre> ordre;
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    for (const auto& s : c.feuilles[f].symboles)
      if (!s.composant.empty() && !ordre.count(s.composant)) {
        // Rangées de 5,08 mm, arrondies par défaut : la rangée de l'axe n'est pas deux fois plus haute (E11)
        const Nm rangee = GRILLE_SCHEMA * 4;
        const Nm y = s.position.y >= 0 ? s.position.y / rangee : -((-s.position.y + rangee - 1) / rangee);
        ordre[s.composant] = {f, y, s.position.x};
      }
  std::vector<const ComposantElec*> liste;
  for (const auto& x : c.composants)
    if (ordre.count(x.uuid)) liste.push_back(&x);
  std::stable_sort(liste.begin(), liste.end(), [&](const ComposantElec* a, const ComposantElec* b) {
    const Ordre &oa = ordre[a->uuid], &ob = ordre[b->uuid];
    return std::tie(oa.feuille, oa.y, oa.x) < std::tie(ob.feuille, ob.y, ob.x);
  });
  auto prefixeSymbole = [&](const ComposantElec& x) {
    const DefSymbole* d = c.symbole(x.symbole);
    return d && !d->prefixe.empty() ? d->prefixe : std::string("U");
  };
  // « R12 » → (« R », 12) ; −1 si le repère n'est pas un préfixe de lettres suivi d'un numéro simple
  auto decouper = [](const std::string& ref, std::string& prefixe) {
    std::size_t i = 0;
    while (i < ref.size() && !std::isdigit(static_cast<unsigned char>(ref[i]))) ++i;
    prefixe = ref.substr(0, i);
    if (i == 0 || i >= ref.size() || ref.size() - i > 9) return -1;
    for (std::size_t j = i; j < ref.size(); ++j)
      if (!std::isdigit(static_cast<unsigned char>(ref[j]))) return -1;
    const int n = std::stoi(ref.substr(i));
    return n > 0 ? n : -1;
  };
  // Seuls les repères vides ou qui contiennent « ? » sont nouveaux (EL-16) : TP_VCC, LED_PWR ou U1A, choisis à
  // la main, ne sont jamais touchés par « Annoter les nouveaux »
  auto nouveau = [](const ComposantElec& x) { return x.reference.empty() || x.reference.find('?') != std::string::npos; };
  std::map<std::string, std::set<int>> pris;
  // Repères réservés : tous ceux qui existent pour « nouveaux » ; ceux des composants non posés pour « tout » (E11)
  for (const auto& x : c.composants) {
    if (tout && ordre.count(x.uuid)) continue;
    {
      // Numéros déjà pris, y compris celui d'un repère d'unité (U1A prend le 1 des U)
      const std::string& ref = x.reference;
      std::size_t i = 0;
      while (i < ref.size() && !std::isdigit(static_cast<unsigned char>(ref[i]))) ++i;
      std::size_t j = i;
      while (j < ref.size() && std::isdigit(static_cast<unsigned char>(ref[j]))) ++j;
      if (i > 0 && j > i && j - i <= 9) pris[ref.substr(0, i)].insert(std::stoi(ref.substr(i, j - i)));
    }
  }
  std::vector<ChangementReference> r;
  for (const ComposantElec* x : liste) {
    if (!tout && !nouveau(*x)) continue;
    std::string p;
    if (nouveau(*x)) {
      p = x->reference.substr(0, x->reference.find('?'));
      while (!p.empty() && std::isdigit(static_cast<unsigned char>(p.back()))) p.pop_back();
      if (p.empty()) p = prefixeSymbole(*x);
    } else if (decouper(x->reference, p) < 0 || p.empty()) {
      p = prefixeSymbole(*x);  // « Tout renuméroter » : un repère libre reprend le préfixe de son symbole
    }
    int m = 1;
    while (pris[p].count(m)) ++m;
    pris[p].insert(m);
    const std::string neuve = p + std::to_string(m);
    if (neuve != x->reference) r.push_back({x->uuid, x->reference, neuve});
  }
  return r;
}

int annoter(ConceptionElec& c, bool tout) {
  exigerModifiable(c);
  const auto changements = annotationPrevue(c, tout);
  for (const auto& ch : changements)
    if (ComposantElec* x = c.composant(ch.composant)) x->reference = ch.apres;
  return static_cast<int>(changements.size());
}

std::vector<LigneNomenclature> nomenclature(const ConceptionElec& c) {
  std::set<std::string> poses;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) poses.insert(s.composant);
  std::map<std::string, LigneNomenclature> lignes;
  for (const auto& x : c.composants) {
    const DefSymbole* d = c.symbole(x.symbole);
    (void)d;
    if (!poses.count(x.uuid) || x.nePasMonter || !dansNomenclature(c, x)) continue;
    // « 4k7 », « 4,7 kΩ » et « 4.7k » font une seule ligne (3.2) ; la valeur affichée est celle du premier repère
    const std::string cle = cleValeur(lireValeurComposant(x.valeur, familleDe(c, x)), x.valeur);
    LigneNomenclature& l = lignes[cle + "|" + x.empreinte + "|" + x.mpn + "|" + x.fabricant];
    l.quantite++;
    const bool premier = l.references.empty() || std::none_of(l.references.begin(), l.references.end(), [&](const std::string& r) { return ordreNaturel(r, x.reference); });
    l.references.push_back(x.reference);
    if (premier) l.valeur = x.valeur;
    l.empreinte = x.empreinte;
    l.fabricant = x.fabricant;
    l.mpn = x.mpn;
    if (l.fournisseur.empty()) l.fournisseur = x.fournisseur;
    if (l.lcsc.empty()) l.lcsc = x.lcsc.empty() && x.champs.count("LCSC") ? x.champs.at("LCSC") : x.lcsc;
    if (l.description.empty()) l.description = x.description;
  }
  std::vector<LigneNomenclature> r;
  for (auto& [cle, l] : lignes) {
    std::sort(l.references.begin(), l.references.end(), ordreNaturel);
    r.push_back(l);
  }
  std::sort(r.begin(), r.end(), [](const LigneNomenclature& a, const LigneNomenclature& b) { return ordreNaturel(a.references.front(), b.references.front()); });
  return r;
}

GenreComposant genreEffectif(const ConceptionElec& c, const ComposantElec& x) {
  if (x.genre != GenreComposant::Standard) return x.genre;
  const DefSymbole* d = c.symbole(x.symbole);
  return d && d->liaisonReseaux ? GenreComposant::LiaisonReseaux : GenreComposant::Standard;
}

bool dansNomenclature(const ConceptionElec& c, const ComposantElec& x) {
  const DefSymbole* d = c.symbole(x.symbole);
  if (x.horsNomenclature || (d && (d->alimentation || d->drapeau))) return false;
  switch (genreEffectif(c, x)) {
    case GenreComposant::Graphique:
    case GenreComposant::LiaisonReseaux:
    case GenreComposant::HorsNomenclature: return false;
    case GenreComposant::Mecanique:
    case GenreComposant::Cavalier: return true;
    case GenreComposant::Standard: break;
  }
  return !(d && d->mecanique);
}

bool dansPlacement(const ConceptionElec& c, const ComposantElec& x) {
  if (x.nePasMonter || x.horsCarte || x.horsPlacement) return false;
  switch (genreEffectif(c, x)) {
    case GenreComposant::Mecanique:
    case GenreComposant::Graphique:
    case GenreComposant::LiaisonReseaux: return false;
    case GenreComposant::HorsNomenclature:
    case GenreComposant::Cavalier:
    case GenreComposant::Standard: break;
  }
  const DefSymbole* d = c.symbole(x.symbole);
  return !(d && d->mecanique);
}

int exclureFeuille(ConceptionElec& c, std::size_t feuille, const std::string& quoi, bool oui) {
  exigerModifiable(c);
  if (feuille >= c.feuilles.size()) return 0;
  std::set<std::string> ids;
  for (const auto& s : c.feuilles[feuille].symboles)
    if (!s.composant.empty()) ids.insert(s.composant);
  int n = 0;
  for (auto& x : c.composants) {
    if (!ids.count(x.uuid)) continue;
    bool* b = quoi == "nePasMonter" ? &x.nePasMonter : quoi == "horsNomenclature" ? &x.horsNomenclature : quoi == "horsCarte" ? &x.horsCarte : quoi == "horsPlacement" ? &x.horsPlacement : quoi == "horsSimulation" ? &x.horsSimulation : nullptr;
    if (b && *b != oui) {
      *b = oui;
      ++n;
    }
  }
  return n;
}

std::string netlistTexte(const ConceptionElec& c, const Connectivite& k) {
  std::ostringstream s;
  s << "# Liste des réseaux — " << c.nom << "\n# Prométhée\n\nComposants\n";
  std::vector<const ComposantElec*> v;
  for (const auto& x : c.composants) v.push_back(&x);
  std::sort(v.begin(), v.end(), [](const ComposantElec* a, const ComposantElec* b) { return ordreNaturel(a->reference, b->reference); });
  for (const auto* x : v) s << "  " << x->reference << "\t" << x->valeur << "\t" << x->empreinte << "\n";
  s << "\nRéseaux\n";
  for (const auto& r : k.reseaux) {
    if (r.broches.empty()) continue;
    s << "  " << r.nom << " :";
    for (const auto& n : r.broches) {
      const ComposantElec* x = c.composant(n.composant);
      s << " " << (x ? x->reference : std::string("?")) << "." << n.broche;
    }
    s << "\n";
  }
  return s.str();
}

// ---------- Circuit imprimé : empreintes ----------
const DefEmpreinte* empreinteEnCache(const std::string& id) {
  static std::mutex verrou;
  static std::map<std::string, std::optional<DefEmpreinte>> cache;
  std::lock_guard<std::mutex> g(verrou);
  auto it = cache.find(id);
  if (it == cache.end()) it = cache.emplace(id, genererEmpreinte(id)).first;
  return it->second ? &*it->second : nullptr;
}

int figerEmpreintes(ConceptionElec& c) {
  exigerModifiable(c);
  int n = 0;
  std::set<std::string> utilisees;
  for (const auto& x : c.composants)
    if (!x.empreinte.empty()) utilisees.insert(x.empreinte);
  for (const auto& e : c.circuit.empreintes) utilisees.insert(e.empreinte);
  for (const auto& id : utilisees)
    if (!c.empreintes.count(id))
      if (const DefEmpreinte* d = empreinteEnCache(id)) { c.empreintes[id] = *d; ++n; }
  return n;
}

namespace {
bool memeEmpreinte(const DefEmpreinte& a, const DefEmpreinte& b) { return empreinteVersJson(a) == empreinteVersJson(b); }
}  // namespace

std::vector<std::string> empreintesPerimees(const ConceptionElec& c) {
  std::vector<std::string> r;
  for (const auto& [id, d] : c.empreintes)
    if (const DefEmpreinte* g = empreinteEnCache(id); g && !memeEmpreinte(*g, d)) r.push_back(id);
  return r;
}

int actualiserEmpreintes(ConceptionElec& c, const std::vector<std::string>& ids) {
  exigerModifiable(c);
  int n = 0;
  for (const auto& id : ids)
    if (const DefEmpreinte* g = empreinteEnCache(id)) { c.empreintes[id] = *g; ++n; }
  return n;
}

BilanMiseAJour mettreAJourCircuit(ConceptionElec& c) {
  exigerModifiable(c);
  BilanMiseAJour b;
  std::set<std::string> poses;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (!s.composant.empty()) poses.insert(s.composant);
  auto& E = c.circuit.empreintes;
  const std::size_t avant = E.size();
  E.erase(std::remove_if(E.begin(), E.end(), [&](const EmpreintePlacee& e) {
            const ComposantElec* x = c.composant(e.composant);
            return !x || !poses.count(e.composant) || x->empreinte.empty() || x->horsCarte;
          }),
          E.end());
  b.retirees = static_cast<int>(avant - E.size());
  // Nouvelles empreintes : rangées à droite de la carte, par lignes
  const Boite carte = boite(c.circuit.contour.polygone());
  Nm x = carte.x1 + 8 * NM_PAR_MM, y = carte.y0, hauteurLigne = 0;
  const Nm largeurRangement = 60 * NM_PAR_MM;
  for (const auto& comp : c.composants) {
    if (!poses.count(comp.uuid) || comp.empreinte.empty() || comp.horsCarte) continue;
    auto it = std::find_if(E.begin(), E.end(), [&](const EmpreintePlacee& e) { return e.composant == comp.uuid; });
    if (it != E.end()) {
      // Empreinte changée : remplacée en place, pistes accrochées (3.7, C22)
      if (it->empreinte != comp.empreinte && c.empreinte(comp.empreinte) && remplacerEmpreinteEnPlace(c, comp.uuid, comp.empreinte)) ++b.changees;
      continue;
    }
    const DefEmpreinte* d = c.empreinte(comp.empreinte);
    const Nm l = d ? d->encombrement.largeur() : 5 * NM_PAR_MM, h = d ? d->encombrement.hauteur() : 5 * NM_PAR_MM;
    if (x + l > carte.x1 + 8 * NM_PAR_MM + largeurRangement) { x = carte.x1 + 8 * NM_PAR_MM; y += hauteurLigne + 2 * NM_PAR_MM; hauteurLigne = 0; }
    EmpreintePlacee e;
    e.composant = comp.uuid;
    e.empreinte = comp.empreinte;
    e.position = surGrille({x + l / 2 - (d ? d->encombrement.centre().x : 0), y + h / 2 - (d ? d->encombrement.centre().y : 0)}, 50'000);
    E.push_back(e);
    x += l + 2 * NM_PAR_MM;
    hauteurLigne = std::max(hauteurLigne, h);
    ++b.ajoutees;
  }
  figerEmpreintes(c);
  b.cuivreRenomme = synchroniserReseaux(c, &b.conflitsClasses);
  // Empreintes figées que plus rien n'utilise : retirées du projet
  std::set<std::string> utilisees;
  for (const auto& e : E) utilisees.insert(e.empreinte);
  for (const auto& x : c.composants) utilisees.insert(x.empreinte);
  std::erase_if(c.empreintes, [&](const auto& kv) { return !utilisees.count(kv.first); });
  return b;
}

Boite empriseCorps(const DefEmpreinte& d) {
  const Pt o = d.corps.decalage;  // centre du corps (T5)
  if (d.corps.largeur > 0 && d.corps.profondeur > 0) return {o.x - d.corps.largeur / 2, o.y - d.corps.profondeur / 2, o.x + d.corps.largeur / 2, o.y + d.corps.profondeur / 2};
  if (d.encombrement.vide() || d.encombrement.largeur() <= 0) return {-250'000, -250'000, 250'000, 250'000};
  const Boite b = d.encombrement.gonflee(-250'000);
  return b.largeur() > 0 && b.hauteur() > 0 ? b : d.encombrement;
}

std::vector<PastillePlacee> pastilles(const ConceptionElec& c, const Connectivite& k) {
  std::vector<PastillePlacee> r;
  for (std::size_t i = 0; i < c.circuit.empreintes.size(); ++i) {
    const EmpreintePlacee& e = c.circuit.empreintes[i];
    const DefEmpreinte* d = c.empreinte(e.empreinte);
    if (!d) continue;
    const ComposantElec* x = c.composant(e.composant);
    for (const auto& p : d->pastilles) {
      PastillePlacee pp;
      pp.composant = e.composant;
      pp.reference = x ? x->reference : std::string();
      pp.numero = p.numero;
      pp.reseau = p.numero.empty() ? std::string() : k.nom(e.composant, p.numero);
      pp.pastille = pastilleSurCarte(e, p);
      pp.dessous = e.dessous;
      pp.couches = couchesPastille(p, e.dessous, c.circuit.empilage.couchesCuivre);
      pp.forme = formePastille(pp.pastille);
      pp.empreinte = static_cast<int>(i);
      // CMS sur une face sans cuivre (dessus d'une carte simple face) : aucune liaison possible (R2)
      pp.inaccessible = p.type == TypePastille::CMS && !(pp.couches & tousLesCuivres(c.circuit.empilage.couchesCuivre));
      r.push_back(std::move(pp));
    }
  }
  return r;
}

}  // namespace prom::ecad
