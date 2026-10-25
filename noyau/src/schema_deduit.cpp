// Prométhée : schéma déduit d'une carte et calque des retouches (lot L17 de la 0.34 ; 17.6). Voir schema_deduit.hpp.
// Licence GPL-3.0-only.
#include "promethee/schema_deduit.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <numeric>
#include <set>
#include <sstream>

namespace prom::ecad {

using Json = nlohmann::ordered_json;

namespace {
struct Ensembles {
  std::vector<int> p;
  explicit Ensembles(std::size_t n) : p(n) { std::iota(p.begin(), p.end(), 0); }
  int trouver(int x) {
    while (p[static_cast<std::size_t>(x)] != x) x = p[static_cast<std::size_t>(x)] = p[static_cast<std::size_t>(p[static_cast<std::size_t>(x)])];
    return x;
  }
  void unir(int a, int b) {
    a = trouver(a), b = trouver(b);
    if (a != b) p[static_cast<std::size_t>(a)] = b;
  }
};
struct ObjetCuivre {
  Couches couches = 0;
  Polygones forme;   // pastille, piste, via : un contour ; îlot de zone : extérieur puis trous
  bool zone = false;
  Boite boite;
  std::string reseau;
  int pastille = -1;  // rang dans « pastilles »
};
bool touchent(const ObjetCuivre& a, const ObjetCuivre& b) {
  if (!(a.couches & b.couches) || !a.boite.touche(b.boite)) return false;
  if (a.zone || b.zone) return aireMm2(intersecter(a.forme, b.forme)) > 1e-6;
  for (const auto& x : a.forme)
    for (const auto& y : b.forme)
      if (distancePolygones(x, y) <= 0) return true;
  return false;
}
// Îlots du cuivre : rang d'îlot de chaque pastille numérotée, et noms portés par le cuivre de chaque îlot
struct IlotsCuivre {
  std::vector<PastillePlacee> pastilles;
  std::vector<int> ilotDePastille;                     // même rang que « pastilles », −1 sans numéro
  std::map<int, std::map<std::string, int>> nomsParIlot;  // îlot → nom → nombre d'objets
};
IlotsCuivre ilotsDuCuivre(const ConceptionElec& c) {
  IlotsCuivre r;
  const Connectivite k = connectivite(c);
  r.pastilles = pastilles(c, k);
  const Remplissages fills = remplirZones(c, k);
  const int n = c.circuit.empilage.couchesCuivre;
  std::vector<ObjetCuivre> o;
  for (std::size_t i = 0; i < r.pastilles.size(); ++i) {
    const auto& p = r.pastilles[i];
    if (p.forme.size() < 3) continue;
    o.push_back({p.couches, {p.forme}, false, boite(p.forme), {}, static_cast<int>(i)});
  }
  for (const auto& t : c.circuit.pistes) {
    Polygones f = formePiste(t);
    if (f.empty()) continue;
    o.push_back({bit(t.couche), f, false, boite(f), t.reseau, -1});
  }
  for (const auto& v : c.circuit.vias) {
    const Polygone f = cercle(v.p, v.diametre / 2);
    o.push_back({couchesVia(v, n), {f}, false, boite(f), v.reseau, -1});
  }
  for (const auto& z : c.circuit.zones) {
    const auto it = fills.find(z.uuid);
    if (it == fills.end()) continue;
    for (const auto& il : ilots(it->second)) {
      Polygones f{orienter(il.exterieur)};
      for (Polygone t : il.trous) {
        t = orienter(t);
        std::reverse(t.begin(), t.end());
        f.push_back(t);
      }
      o.push_back({bit(z.couche), f, true, boite(il.exterieur), z.reseau, -1});
    }
  }
  Ensembles e(o.size());
  for (std::size_t i = 0; i < o.size(); ++i)
    for (std::size_t j = i + 1; j < o.size(); ++j)
      if (e.trouver(static_cast<int>(i)) != e.trouver(static_cast<int>(j)) && touchent(o[i], o[j])) e.unir(static_cast<int>(i), static_cast<int>(j));
  r.ilotDePastille.assign(r.pastilles.size(), -1);
  for (std::size_t i = 0; i < o.size(); ++i) {
    const int racine = e.trouver(static_cast<int>(i));
    if (o[i].pastille >= 0 && !r.pastilles[static_cast<std::size_t>(o[i].pastille)].numero.empty()) r.ilotDePastille[static_cast<std::size_t>(o[i].pastille)] = racine;
    if (!o[i].reseau.empty()) ++r.nomsParIlot[racine][o[i].reseau];
  }
  return r;
}
std::string brocheTexte(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  const ComposantElec* x = c.composant(composant);
  return (x ? x->reference : composant) + "." + numero;
}
// Fil de 2,54 mm tiré de la broche vers l'extérieur, terminé par une étiquette globale au nom du réseau
bool etiqueter(ConceptionElec& c, std::size_t feuille, const SymbolePlace& sp, const BrochePlacee& b, const std::string& reseau) {
  if (feuille >= c.feuilles.size()) return false;
  const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
  const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
  c.feuilles[feuille].fils.push_back({nouvelUuid(), b.p, bout});
  Etiquette e;
  e.uuid = nouvelUuid();
  e.texte = reseau;
  e.p = bout;
  e.type = TypeEtiquette::Globale;
  c.feuilles[feuille].etiquettes.push_back(e);
  (void)sp;
  return true;
}
// Broche désignée « R1.2 » : composant, numéro
std::optional<std::pair<std::string, std::string>> broche(const ConceptionElec& c, const std::string& texte) {
  const auto p = texte.rfind('.');
  if (p == std::string::npos || p == 0 || p + 1 >= texte.size()) return std::nullopt;
  const ComposantElec* x = c.parReference(texte.substr(0, p));
  if (!x) return std::nullopt;
  return std::pair{x->uuid, texte.substr(p + 1)};
}
// Broche posée : sa feuille, sa pose, sa géométrie
struct Pose {
  std::size_t feuille = 0;
  SymbolePlace sp;
  BrochePlacee b;
};
std::optional<Pose> poseDe(const ConceptionElec& c, const std::string& composant, const std::string& numero) {
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    for (const auto& sp : c.feuilles[f].symboles) {
      if (sp.composant != composant) continue;
      const DefSymbole* d = c.symbole(sp.symbole);
      if (!d) continue;
      for (const auto& b : brochesPlacees(sp, *d))
        if ((b.numero.empty() ? b.broche->numero : b.numero) == numero) return Pose{f, sp, b};
    }
  return std::nullopt;
}
// Broche détachée de son réseau : les fils qui touchent son extrémité retirés, puis une étiquette au nouveau nom
void deplacerBroche(ConceptionElec& c, const std::string& composant, const std::string& numero, const std::string& reseau) {
  const auto p = poseDe(c, composant, numero);
  if (!p) return;
  auto& fils = c.feuilles[p->feuille].fils;
  fils.erase(std::remove_if(fils.begin(), fils.end(), [&](const Fil& f) { return f.a == p->b.p || f.b == p->b.p; }), fils.end());
  auto& et = c.feuilles[p->feuille].etiquettes;
  const Pt d = p->b.direction == 0 ? Pt{1, 0} : p->b.direction == 1 ? Pt{0, 1} : p->b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
  const Pt bout{p->b.p.x - d.x * 2'540'000, p->b.p.y - d.y * 2'540'000};
  et.erase(std::remove_if(et.begin(), et.end(), [&](const Etiquette& e) { return e.p == bout || e.p == p->b.p; }), et.end());
  etiqueter(c, p->feuille, p->sp, p->b, reseau);
}
const char* codeRetouche(GenreRetouche g) { return g == GenreRetouche::FilDeReprise ? "fil" : g == GenreRetouche::PisteCoupee ? "coupure" : "composant"; }
GenreRetouche retoucheDepuis(const std::string& s) { return s == "coupure" ? GenreRetouche::PisteCoupee : s == "composant" ? GenreRetouche::ComposantChange : GenreRetouche::FilDeReprise; }
}  // namespace

std::map<std::pair<std::string, std::string>, std::string> reseauxDuCuivre(const ConceptionElec& c) {
  std::map<std::pair<std::string, std::string>, std::string> r;
  const IlotsCuivre il = ilotsDuCuivre(c);
  // Nom de chaque îlot : le plus porté par son cuivre ; à défaut, d'après sa première pastille
  std::map<int, std::vector<std::size_t>> pastillesParIlot;
  for (std::size_t i = 0; i < il.pastilles.size(); ++i)
    if (il.ilotDePastille[i] >= 0) pastillesParIlot[il.ilotDePastille[i]].push_back(i);
  for (const auto& [ilot, rangs] : pastillesParIlot) {
    std::string nom;
    if (const auto it = il.nomsParIlot.find(ilot); it != il.nomsParIlot.end()) {
      int meilleur = 0;
      for (const auto& [n, k] : it->second)
        if (k > meilleur) meilleur = k, nom = n;
    }
    if (nom.empty()) {
      std::vector<std::string> noms;
      for (std::size_t i : rangs) noms.push_back(il.pastilles[i].reference + "-" + il.pastilles[i].numero);
      std::sort(noms.begin(), noms.end());
      nom = "N-" + noms.front();
    }
    for (std::size_t i : rangs) r[{il.pastilles[i].composant, il.pastilles[i].numero}] = nom;
  }
  return r;
}

BilanSchemaDeduit schemaDepuisCircuit(ConceptionElec& c, std::size_t feuille) {
  BilanSchemaDeduit b;
  while (c.feuilles.size() <= feuille) {
    Feuille f;
    f.uuid = nouvelUuid();
    f.nom = "Feuille " + std::to_string(c.feuilles.size() + 1);
    c.feuilles.push_back(f);
  }
  const auto reseaux = reseauxDuCuivre(c);
  std::map<std::string, int> taille;
  for (const auto& [cle, n] : reseaux) ++taille[n];
  std::set<std::string> surFeuille;
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles)
      if (!s.composant.empty()) surFeuille.insert(s.composant);
  // Composants à poser, dans l'ordre des repères (R1 avant R10)
  std::vector<const EmpreintePlacee*> aPoser;
  for (const auto& e : c.circuit.empreintes)
    if (const ComposantElec* x = c.composant(e.composant); x && !surFeuille.count(x->uuid)) aPoser.push_back(&e);
  std::sort(aPoser.begin(), aPoser.end(), [&](const EmpreintePlacee* a, const EmpreintePlacee* z) { return ordreNaturel(c.composant(a->composant)->reference, c.composant(z->composant)->reference); });
  double x = 25.4, y = 38.1, ligne = 0;
  std::set<std::string> nomsReseaux;
  for (const EmpreintePlacee* e : aPoser) {
    ComposantElec* comp = c.composant(e->composant);
    const DefEmpreinte* d = c.empreinte(e->empreinte);
    if (!comp || !d) continue;
    std::vector<std::string> numeros;
    for (const auto& p : d->pastilles)
      if (!p.numero.empty() && std::find(numeros.begin(), numeros.end(), p.numero) == numeros.end()) numeros.push_back(p.numero);
    if (numeros.empty()) continue;  // trou de fixation, repère optique
    std::string id = comp->symbole;
    const DefSymbole* s = id.empty() ? nullptr : c.symbole(id);
    bool couvre = s != nullptr && !s->alimentation;
    if (couvre)
      for (const auto& n : numeros) {
        bool trouve = false;
        for (const auto& br : s->broches) trouve |= br.numero == n;
        couvre &= trouve;
      }
    if (!couvre) {
      // Symbole dessiné d'après l'empreinte : une broche par pastille
      SpecSymbole sp;
      sp.id = "carte:" + e->empreinte;
      sp.nom = e->empreinte;
      std::string prefixe;
      for (char ch : comp->reference)
        if (std::isalpha(static_cast<unsigned char>(ch))) prefixe += ch;
        else break;
      sp.prefixe = prefixe.empty() ? std::string("U") : prefixe;
      sp.categorie = "Déduits de la carte";
      for (const auto& n : numeros) {
        SpecBroche br;
        br.numero = n;
        br.nom = n;
        br.type = TypeBroche::Passive;
        sp.broches.push_back(br);
      }
      if (!c.symboles.count(sp.id)) {
        c.symboles[sp.id] = genererSymbole(sp);
        ++b.symbolesCrees;
      }
      id = sp.id;
      if (!comp->symbole.empty()) b.alertes.push_back(comp->reference + " : le symbole « " + comp->symbole + " » ne couvre pas les pastilles de l'empreinte, un symbole est dessiné d'après elle");
    }
    const DefSymbole* def = c.symbole(id);
    SymbolePlace sp;
    sp.uuid = nouvelUuid();
    sp.symbole = id;
    sp.composant = comp->uuid;
    sp.position = ptMm(x, y);
    const Boite bs = boiteSymbole(sp, *def);
    // La pose se range dans une grille : le symbole entier à droite du précédent, une ligne plus bas quand la
    // feuille est pleine
    const Nm pas = 2'540'000;
    sp.position = {((sp.position.x + (ptMm(x, y).x - bs.x0) + 5'080'000) / pas) * pas, (sp.position.y / pas) * pas};
    c.feuilles[feuille].symboles.push_back(sp);
    comp->symbole = id;
    comp->carteSeule = false;
    ++b.symboles;
    for (const auto& br : brochesPlacees(sp, *def)) {
      const std::string num = br.numero.empty() ? br.broche->numero : br.numero;
      const auto it = reseaux.find({comp->uuid, num});
      if (it == reseaux.end() || taille[it->second] < 2) {
        c.feuilles[feuille].nonConnexions.push_back({br.p, nouvelUuid()});
        continue;
      }
      etiqueter(c, feuille, sp, br, it->second);
      nomsReseaux.insert(it->second);
      ++b.etiquettes;
    }
    const Boite bp = boiteSymbole(sp, *def);
    x = mm(bp.x1) + 20.32;
    ligne = std::max(ligne, mm(bp.hauteur()));
    if (x > 250) x = 25.4, y += ligne + 25.4, ligne = 0;
    for (auto it = c.nomsReseaux.begin(); it != c.nomsReseaux.end();)
      it = it->first.rfind(comp->uuid + ":", 0) == 0 ? c.nomsReseaux.erase(it) : std::next(it);
  }
  b.reseaux = static_cast<int>(nomsReseaux.size());
  return b;
}

// ======================================================================================================================
// Retouches
// ======================================================================================================================
std::vector<Retouche> retouchesDe(const ConceptionElec& c) {
  std::vector<Retouche> r;
  const auto it = c.extensions.find("retouches");
  if (it == c.extensions.end() || !it->second.is_array()) return r;
  for (const auto& j : it->second) {
    Retouche x;
    x.uuid = j.value("uuid", std::string());
    x.genre = retoucheDepuis(j.value("genre", std::string("fil")));
    x.revision = j.value("revision", std::string());
    if (j.contains("numerosSerie") && j["numerosSerie"].is_array())
      for (const auto& n : j["numerosSerie"])
        if (n.is_string()) x.numerosSerie.push_back(n.get<std::string>());
    x.a = j.value("a", std::string());
    x.b = j.value("b", std::string());
    x.piste = j.value("piste", std::string());
    x.composant = j.value("composant", std::string());
    x.valeur = j.value("valeur", std::string());
    x.description = j.value("description", std::string());
    x.appliquee = j.value("appliquee", false);
    r.push_back(x);
  }
  return r;
}

void ecrireRetouches(ConceptionElec& c, const std::vector<Retouche>& v) {
  Json a = Json::array();
  for (const auto& x : v)
    a.push_back({{"uuid", x.uuid},
                 {"genre", codeRetouche(x.genre)},
                 {"revision", x.revision},
                 {"numerosSerie", x.numerosSerie},
                 {"a", x.a},
                 {"b", x.b},
                 {"piste", x.piste},
                 {"composant", x.composant},
                 {"valeur", x.valeur},
                 {"description", x.description},
                 {"appliquee", x.appliquee}});
  c.extensions["retouches"] = a;
}

std::string ajouterRetouche(ConceptionElec& c, Retouche r) {
  if (r.uuid.empty()) r.uuid = nouvelUuid();
  if (r.revision.empty()) r.revision = c.cartouche.revision;
  auto v = retouchesDe(c);
  v.push_back(r);
  ecrireRetouches(c, v);
  return r.uuid;
}

std::vector<Retouche> retouchesDeCarte(const ConceptionElec& c, const std::string& numeroSerie) {
  std::vector<Retouche> r;
  for (const auto& x : retouchesDe(c))
    if (x.numerosSerie.empty() || std::find(x.numerosSerie.begin(), x.numerosSerie.end(), numeroSerie) != x.numerosSerie.end()) r.push_back(x);
  return r;
}

std::string listeRetouches(const ConceptionElec& c, const std::string& numeroSerie) {
  std::ostringstream o;
  o << "Carte;Révision;Retouche;Description\n";
  auto ligne = [&](const std::string& carte, const Retouche& x) {
    std::string d = x.description;
    if (d.empty()) d = x.genre == GenreRetouche::FilDeReprise ? "fil de " + x.a + " à " + x.b : x.genre == GenreRetouche::ComposantChange ? x.composant + " → " + x.valeur : "piste coupée";
    o << carte << ";" << x.revision << ";" << (x.genre == GenreRetouche::FilDeReprise ? "fil de reprise" : x.genre == GenreRetouche::PisteCoupee ? "piste coupée" : "composant changé") << ";" << d << "\n";
  };
  if (!numeroSerie.empty()) {
    for (const auto& x : retouchesDeCarte(c, numeroSerie)) ligne(numeroSerie, x);
    return o.str();
  }
  for (const auto& x : retouchesDe(c)) {
    if (x.numerosSerie.empty()) ligne("toutes", x);
    for (const auto& n : x.numerosSerie) ligne(n, x);
  }
  return o.str();
}

namespace {
bool appliquerUne(ConceptionElec& c, const Retouche& r, std::string* erreur) {
  auto refuser = [&](const std::string& e) {
    if (erreur) *erreur = e;
    return false;
  };
  switch (r.genre) {
    case GenreRetouche::ComposantChange: {
      const ComposantElec* x0 = c.parReference(r.composant);
      if (!x0) return refuser("composant « " + r.composant + " » introuvable");
      c.composant(x0->uuid)->valeur = r.valeur;
      return true;
    }
    case GenreRetouche::FilDeReprise: {
      const auto a = broche(c, r.a), b = broche(c, r.b);
      if (!a || !b) return refuser("broche « " + (a ? r.b : r.a) + " » introuvable");
      const Connectivite k = connectivite(c);
      const int ra = k.reseau(a->first, a->second), rb = k.reseau(b->first, b->second);
      if (ra >= 0 && ra == rb) return true;
      std::string nom = ra >= 0 && k.reseaux[static_cast<std::size_t>(ra)].nomme ? k.reseaux[static_cast<std::size_t>(ra)].nom
                        : rb >= 0 && k.reseaux[static_cast<std::size_t>(rb)].nomme ? k.reseaux[static_cast<std::size_t>(rb)].nom
                                                                                     : "REPRISE_" + r.a;
      for (const auto& [comp, num] : {*a, *b})
        if (const auto p = poseDe(c, comp, num)) etiqueter(c, p->feuille, p->sp, p->b, nom);
      return true;
    }
    case GenreRetouche::PisteCoupee: {
      const auto it = std::find_if(c.circuit.pistes.begin(), c.circuit.pistes.end(), [&](const Piste& p) { return p.uuid == r.piste; });
      if (it == c.circuit.pistes.end()) return refuser("piste coupée introuvable sur cette révision");
      // Îlots du cuivre avant et après la coupure : les broches d'un même îlot que la coupure sépare se partagent
      const IlotsCuivre avant = ilotsDuCuivre(c);
      const std::string reseau = it->reseau;
      c.circuit.pistes.erase(it);
      const IlotsCuivre apres = ilotsDuCuivre(c);
      std::map<std::pair<std::string, std::string>, std::pair<int, int>> ilot;  // broche → (avant, après)
      for (std::size_t i = 0; i < avant.pastilles.size(); ++i)
        if (avant.ilotDePastille[i] >= 0) ilot[{avant.pastilles[i].composant, avant.pastilles[i].numero}].first = avant.ilotDePastille[i];
      for (std::size_t i = 0; i < apres.pastilles.size(); ++i)
        if (apres.ilotDePastille[i] >= 0) ilot[{apres.pastilles[i].composant, apres.pastilles[i].numero}].second = apres.ilotDePastille[i];
      // Dans chaque îlot d'avant, le plus grand morceau d'après garde le réseau ; les autres prennent un nom à eux
      std::map<int, std::map<int, std::vector<std::pair<std::string, std::string>>>> morceaux;
      for (const auto& [b, ia] : ilot) morceaux[ia.first][ia.second].push_back(b);
      int rang = 0;
      for (const auto& [ia, parApres] : morceaux) {
        if (parApres.size() < 2) continue;
        auto plus = std::max_element(parApres.begin(), parApres.end(), [](const auto& x, const auto& y) { return x.second.size() < y.second.size(); });
        for (const auto& [ib, broches] : parApres) {
          if (ib == plus->first) continue;
          const std::string nom = (reseau.empty() ? std::string("COUPE") : reseau) + "_COUPE" + std::to_string(++rang);
          for (const auto& [comp, num] : broches) deplacerBroche(c, comp, num, nom);
        }
      }
      return true;
    }
  }
  return false;
}
}  // namespace

int appliquerRetouches(ConceptionElec& c) {
  auto v = retouchesDe(c);
  int n = 0;
  for (auto& r : v) {
    if (r.appliquee) continue;
    if (appliquerUne(c, r, nullptr)) r.appliquee = true, ++n;
  }
  ecrireRetouches(c, v);
  return n;
}

std::vector<Retouche> retouchesProposees(const ConceptionElec& fabriquee, const ConceptionElec& suivante) {
  std::set<std::string> recues;
  if (const auto it = suivante.extensions.find("retouchesRecues"); it != suivante.extensions.end() && it->second.is_array())
    for (const auto& u : it->second)
      if (u.is_string()) recues.insert(u.get<std::string>());
  std::vector<Retouche> r;
  for (const auto& x : retouchesDe(fabriquee))
    if (!recues.count(x.uuid)) r.push_back(x);
  return r;
}

bool accepterRetouche(ConceptionElec& suivante, const Retouche& r, std::string* erreur) {
  ConceptionElec essai = suivante;
  if (!appliquerUne(essai, r, erreur)) return false;
  Json recues = essai.extensions.count("retouchesRecues") ? essai.extensions["retouchesRecues"] : Json::array();
  recues.push_back(r.uuid);
  essai.extensions["retouchesRecues"] = recues;
  suivante = std::move(essai);
  return true;
}

}  // namespace prom::ecad
