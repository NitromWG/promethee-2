// Prométhée : gestes du schéma faits par le noyau (lot L2) — presse-papiers en JSON et collages, tracé des fils
// à l'équerre par recherche A*, « Relier à… », coupe, fusion et décalage des fils, renommage d'un réseau, broches
// libres terminées, alignement, échange et déplacement à fils élastiques. L'interface, la ligne de commande et
// les scripts passent tous par ces fonctions. Licence GPL-3.0-only.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>

#include "promethee/electronique.hpp"

namespace prom::ecad {

namespace {
using J = nlohmann::ordered_json;

J pt(Pt p) { return J::array({p.x, p.y}); }
Pt lirePt(const J& j) {
  if (j.is_array() && j.size() >= 2 && j[0].is_number() && j[1].is_number()) return {j[0].get<Nm>(), j[1].get<Nm>()};
  return {};
}

Feuille* feuilleDe(ConceptionElec& c, std::size_t feuille) { return feuille < c.feuilles.size() ? &c.feuilles[feuille] : nullptr; }

// Points de connexion d'une feuille : broches, bouts de fils, étiquettes
struct Points {
  std::map<Pt, int> broches, boutsDeFil, etiquettes;
};
Points pointsDe(const ConceptionElec& c, const Feuille& f) {
  Points r;
  for (const auto& s : f.symboles)
    if (const DefSymbole* d = c.symbole(s.symbole))
      for (const auto& b : brochesPlacees(s, *d)) ++r.broches[b.p];
  for (const auto& w : f.fils) {
    ++r.boutsDeFil[w.a];
    ++r.boutsDeFil[w.b];
  }
  for (const auto& e : f.etiquettes) ++r.etiquettes[e.p];
  return r;
}
}  // namespace

// ---------- Presse-papiers en JSON (2.13) ----------
J presseVersJson(const PressePapiersSchema& pp) {
  ConceptionElec t;
  t.uuid = "presse-papiers";
  t.nom = "Presse-papiers";
  t.composants = pp.composants;
  t.symboles = pp.definitions;
  t.empreintes = pp.empreintes;
  Feuille f;
  f.uuid = "presse-papiers";
  f.symboles = pp.symboles;
  f.fils = pp.fils;
  f.etiquettes = pp.etiquettes;
  f.textes = pp.textes;
  f.jonctions = pp.jonctions;
  f.nonConnexions = pp.nonConnexions;
  t.feuilles = {f};
  J j = versJson(t);
  j["genre"] = "presse-papiers-schema";
  j["reference"] = pt(pp.reference);
  return j;
}

PressePapiersSchema presseDepuisJson(const J& j) {
  PressePapiersSchema pp;
  if (!j.is_object() || !j.contains("genre") || j["genre"] != "presse-papiers-schema") return pp;
  const ConceptionElec t = conceptionDepuisJson(j);
  if (t.feuilles.empty()) return pp;
  const Feuille& f = t.feuilles.front();
  pp.symboles = f.symboles;
  pp.fils = f.fils;
  pp.etiquettes = f.etiquettes;
  pp.textes = f.textes;
  pp.jonctions = f.jonctions;
  pp.nonConnexions = f.nonConnexions;
  pp.composants = t.composants;
  pp.definitions = t.symboles;
  pp.empreintes = t.empreintes;
  pp.reference = lirePt(j.contains("reference") ? j["reference"] : J());
  return pp;
}

std::set<std::string> collerSchemaAvec(ConceptionElec& c, std::size_t feuille, const PressePapiersSchema& pp, Pt ou, ReperesCollage reperes) {
  // Les définitions du presse-papiers arrivent figées, par collerSchema (S32)
  const std::size_t avant = c.composants.size();
  std::set<std::string> crees = collerSchema(c, feuille, pp, ou);
  // collerSchema ajoute les composants neufs dans l'ordre de pp.composants
  for (std::size_t i = avant, n = 0; i < c.composants.size() && n < pp.composants.size(); ++i, ++n) {
    ComposantElec& k = c.composants[i];
    const std::string& ancien = pp.composants[n].reference;
    switch (reperes) {
      case ReperesCollage::Gardes:
        // Même en double (SAI-27) : c'est ce qui est demandé ; l'ERC signale le repère répété
        if (!ancien.empty() && ancien.find('?') == std::string::npos) k.reference = ancien;
        else annoterALaPose(c, k.uuid);
        break;
      case ReperesCollage::Libres: annoterALaPose(c, k.uuid); break;
      case ReperesCollage::Effaces: break;
    }
  }
  return crees;
}

std::set<std::string> collerEnMatrice(ConceptionElec& c, std::size_t feuille, const PressePapiersSchema& pp, Pt origine, int lignes, int colonnes, Pt pas, bool ligneDabord) {
  exigerModifiable(c);
  std::set<std::string> crees;
  if (pp.vide() || lignes < 1 || colonnes < 1 || feuille >= c.feuilles.size()) return crees;
  const int n1 = ligneDabord ? lignes : colonnes, n2 = ligneDabord ? colonnes : lignes;
  for (int i = 0; i < n1; ++i)
    for (int k = 0; k < n2; ++k) {
      const int ligne = ligneDabord ? i : k, colonne = ligneDabord ? k : i;
      const Pt ou = origine + Pt{pas.x * colonne, pas.y * ligne};
      // Chaque copie reçoit ses repères avant la suivante : la numérotation suit l'ordre choisi
      const std::size_t avant = c.composants.size();
      const auto ici = collerSchema(c, feuille, pp, ou);
      for (std::size_t j = avant; j < c.composants.size(); ++j) annoterALaPose(c, c.composants[j].uuid);
      crees.insert(ici.begin(), ici.end());
    }
  return crees;
}

// ---------- Tracé A* d'un fil à l'équerre (2.7, 2.18) ----------
namespace {
struct Noeud {
  int x, y;
};
constexpr int DX[4] = {1, 0, -1, 0};
constexpr int DY[4] = {0, 1, 0, -1};
}  // namespace

std::vector<Pt> tracerFil(const ConceptionElec& c, std::size_t feuille, Pt depart, Pt arrivee, const ReglagesTraceFil& r) {
  if (feuille >= c.feuilles.size()) return {};
  const Feuille& f = c.feuilles[feuille];
  const Nm g = GRILLE_SCHEMA;
  depart = surGrille(depart);
  arrivee = surGrille(arrivee);
  if (depart == arrivee) return {depart};
  // Zone de recherche : la boîte des deux bouts, élargie de dix pas, plus « marge » (la feuille entière pour
  // « Relier à… »)
  const Nm bord = ((10 * g + std::max<Nm>(0, r.marge - g)) + g - 1) / g * g;  // un nombre entier de pas : les nœuds restent sur la grille
  const Nm x0 = std::min(depart.x, arrivee.x) - bord, y0 = std::min(depart.y, arrivee.y) - bord;
  const int W = static_cast<int>((std::max(depart.x, arrivee.x) + bord - x0) / g) + 1, H = static_cast<int>((std::max(depart.y, arrivee.y) + bord - y0) / g) + 1;
  auto versPt = [&](int x, int y) { return Pt{x0 + static_cast<Nm>(x) * g, y0 + static_cast<Nm>(y) * g}; };
  auto versNoeud = [&](Pt p) { return Noeud{static_cast<int>((p.x - x0) / g), static_cast<int>((p.y - y0) / g)}; };
  auto indice = [&](int x, int y) { return static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x); };
  std::vector<std::uint8_t> bloque(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), 0), broche(bloque.size(), 0);
  // Corps des symboles (leurs traits, sans les broches), gonflés d'un pas : intérieur bloqué
  for (const auto& s : f.symboles) {
    const DefSymbole* d = c.symbole(s.symbole);
    if (!d) continue;
    Boite corps;
    for (const auto& t : d->traces) {
      if (t.forme == Trace::Forme::Texte || !dansUnite(t.unite, s, *d)) continue;
      if (t.forme == Trace::Forme::Cercle || t.forme == Trace::Forme::Arc) {
        const Pt o = t.points.empty() ? Pt{} : t.points.front();
        corps.etendre(transformer(s, o + Pt{t.rayon, t.rayon}));
        corps.etendre(transformer(s, o - Pt{t.rayon, t.rayon}));
      } else
        for (const Pt& p : t.points) corps.etendre(transformer(s, p));
    }
    if (corps.vide()) continue;
    const Boite b = corps.gonflee(g);
    const Noeud a = versNoeud(Pt{std::max(b.x0, x0), std::max(b.y0, y0)});
    for (int y = std::max(0, a.y); y < H; ++y) {
      if (versPt(0, y).y > b.y1) break;
      for (int x = std::max(0, a.x); x < W; ++x) {
        const Pt p = versPt(x, y);
        if (p.x > b.x1) break;
        if (p.x > b.x0 && p.x < b.x1 && p.y > b.y0 && p.y < b.y1 && p != arrivee && p != depart) bloque[indice(x, y)] = 1;
      }
    }
  }
  // Points de connexion d'autres objets : passer sur un bout de fil ou une étiquette les relierait (bloqué) ; sur une
  // extrémité de broche, cela coûte 100 pas. Seule l'arrivée est libre.
  const Points pts = pointsDe(c, f);
  auto marquer = [&](Pt p, std::vector<std::uint8_t>& v) {
    if (p == arrivee || p == depart || p.x < x0 || p.y < y0) return;
    const Noeud n = versNoeud(p);
    if (n.x < W && n.y < H && versPt(n.x, n.y) == p) v[indice(n.x, n.y)] = 1;
  };
  for (const auto& [p, n] : pts.broches) marquer(p, broche);
  for (const auto* m : {&pts.boutsDeFil, &pts.etiquettes})
    for (const auto& [p, n] : *m) marquer(p, bloque);
  // Fils existants : les longer les relierait (arêtes interdites) ; les croiser coûte « croisement »
  std::vector<std::uint8_t> filH(bloque.size(), 0), filV(bloque.size(), 0);
  for (const auto& w : f.fils) {
    if (w.a.y == w.b.y) {
      const Nm xa = std::min(w.a.x, w.b.x), xb = std::max(w.a.x, w.b.x);
      if (w.a.y < y0 || (w.a.y - y0) % g) continue;
      const int y = static_cast<int>((w.a.y - y0) / g);
      if (y >= H) continue;
      for (int x = std::max(0, static_cast<int>((xa - x0 + g - 1) / g)); x < W && versPt(x, y).x <= xb; ++x) filH[indice(x, y)] = 1;
    } else if (w.a.x == w.b.x) {
      const Nm ya = std::min(w.a.y, w.b.y), yb = std::max(w.a.y, w.b.y);
      if (w.a.x < x0 || (w.a.x - x0) % g) continue;
      const int x = static_cast<int>((w.a.x - x0) / g);
      if (x >= W) continue;
      for (int y = std::max(0, static_cast<int>((ya - y0 + g - 1) / g)); y < H && versPt(x, y).y <= yb; ++y) filV[indice(x, y)] = 1;
    }
  }
  // A* sur (nœud, direction d'arrivée) : coût d'un pas 1, d'un coude « coude », d'un fil croisé « croisement »
  const Noeud nd = versNoeud(depart), na = versNoeud(arrivee);
  const std::size_t N = bloque.size() * 5;  // 4 directions + aucune (départ)
  std::vector<double> cout(N, std::numeric_limits<double>::infinity());
  std::vector<std::int64_t> precedent(N, -1);
  auto etat = [&](int x, int y, int dir) { return indice(x, y) * 5 + static_cast<std::size_t>(dir); };
  auto heuristique = [&](int x, int y) { return static_cast<double>(std::abs(x - na.x) + std::abs(y - na.y)); };
  using Entree = std::pair<double, std::size_t>;
  std::priority_queue<Entree, std::vector<Entree>, std::greater<>> file;
  const std::size_t e0 = etat(nd.x, nd.y, 4);
  cout[e0] = 0;
  file.push({heuristique(nd.x, nd.y), e0});
  std::size_t fin = SIZE_MAX;
  int visites = 0;
  while (!file.empty() && visites < r.maxNoeuds) {
    const auto [prio, e] = file.top();
    file.pop();
    const std::size_t i = e / 5;
    const int dir = static_cast<int>(e % 5), x = static_cast<int>(i % static_cast<std::size_t>(W)), y = static_cast<int>(i / static_cast<std::size_t>(W));
    if (prio > cout[e] + heuristique(x, y) + 1e-9) continue;
    ++visites;
    if (x == na.x && y == na.y) {
      fin = e;
      break;
    }
    for (int d = 0; d < 4; ++d) {
      if (dir < 4 && d == (dir + 2) % 4) continue;  // pas de demi-tour
      const int nx = x + DX[d], ny = y + DY[d];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
      const std::size_t j = indice(nx, ny);
      if (bloque[j]) continue;
      // Longer un fil existant : interdit (les deux points sur un même fil, dans son axe)
      const bool horizontal = d == 0 || d == 2;
      if (horizontal ? (filH[i] && filH[j]) : (filV[i] && filV[j])) continue;
      double pas = 1.0 + (broche[j] ? 100.0 : 0.0);
      if (dir < 4 && d != dir) pas += r.coude;
      if (horizontal ? filV[j] : filH[j]) pas += r.croisement;
      const std::size_t ne = etat(nx, ny, d);
      if (cout[e] + pas < cout[ne]) {
        cout[ne] = cout[e] + pas;
        precedent[ne] = static_cast<std::int64_t>(e);
        file.push({cout[ne] + heuristique(nx, ny), ne});
      }
    }
  }
  if (fin == SIZE_MAX) return {};
  std::vector<Pt> chemin;
  for (std::int64_t e = static_cast<std::int64_t>(fin); e >= 0; e = precedent[static_cast<std::size_t>(e)]) {
    const std::size_t i = static_cast<std::size_t>(e) / 5;
    chemin.push_back(versPt(static_cast<int>(i % static_cast<std::size_t>(W)), static_cast<int>(i / static_cast<std::size_t>(W))));
  }
  std::reverse(chemin.begin(), chemin.end());
  // Les seuls coudes
  std::vector<Pt> sommets{chemin.front()};
  for (std::size_t k = 1; k + 1 < chemin.size(); ++k) {
    const Pt a = chemin[k - 1], b = chemin[k], d = chemin[k + 1];
    const bool aligne = (a.x == b.x && b.x == d.x) || (a.y == b.y && b.y == d.y);
    if (!aligne) sommets.push_back(b);
  }
  sommets.push_back(chemin.back());
  return sommets;
}

namespace {
// « U1:PA9 », « J2:3 », « U1A:1 » : la broche posée (feuille, point), par numéro réel ou par nom
struct BrocheTrouvee {
  std::size_t feuille = 0;
  Pt p;
  bool ok = false;
};
BrocheTrouvee trouverBroche(const ConceptionElec& c, const std::string& designation) {
  BrocheTrouvee r;
  const std::size_t deux = designation.rfind(':');
  if (deux == std::string::npos) return r;
  std::string ref = designation.substr(0, deux);
  const std::string broche = designation.substr(deux + 1);
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    for (const auto& s : c.feuilles[f].symboles) {
      const ComposantElec* k = c.composant(s.composant);
      const DefSymbole* d = c.symbole(s.symbole);
      if (!k || !d) continue;
      const std::string avecUnite = k->reference + (d->unites > 1 ? lettreUnite(s.unite) : std::string());
      if (k->reference != ref && avecUnite != ref) continue;
      for (const auto& b : brochesPlacees(s, *d))
        if (b.numero == broche || b.broche->nom == broche) return {f, b.p, true};
    }
  return r;
}
}  // namespace

int relierBroches(ConceptionElec& c, const std::string& de, const std::string& vers, const ReglagesTraceFil& r) {
  exigerModifiable(c);
  const BrocheTrouvee a = trouverBroche(c, de), b = trouverBroche(c, vers);
  if (!a.ok || !b.ok || a.feuille != b.feuille) return -1;
  // La recherche couvre la feuille entière (2.18)
  ReglagesTraceFil large = r;
  const Boite cadre = cadreFeuille(c.feuilles[a.feuille].format);
  large.marge = std::max(r.marge, std::max(cadre.largeur(), cadre.hauteur()) / 2);
  const std::vector<Pt> chemin = tracerFil(c, a.feuille, a.p, b.p, large);
  if (chemin.size() < 2) return -1;
  Feuille& f = c.feuilles[a.feuille];
  for (std::size_t i = 0; i + 1 < chemin.size(); ++i) f.fils.push_back({nouvelUuid(), chemin[i], chemin[i + 1]});
  return static_cast<int>(chemin.size()) - 1;
}

int relierPaires(ConceptionElec& c, const std::vector<std::pair<std::string, std::string>>& paires, std::vector<std::string>* echecs, const ReglagesTraceFil& r) {
  exigerModifiable(c);
  // Les plus courtes d'abord : elles laissent la place aux longues
  std::vector<std::pair<double, std::size_t>> ordre;
  for (std::size_t i = 0; i < paires.size(); ++i) {
    const BrocheTrouvee a = trouverBroche(c, paires[i].first), b = trouverBroche(c, paires[i].second);
    ordre.push_back({a.ok && b.ok ? static_cast<double>(std::abs(a.p.x - b.p.x) + std::abs(a.p.y - b.p.y)) : 1e300, i});
  }
  std::stable_sort(ordre.begin(), ordre.end());
  int segments = 0;
  for (const auto& [l, i] : ordre) {
    const int n = relierBroches(c, paires[i].first, paires[i].second, r);
    if (n < 0) {
      if (echecs) echecs->push_back(paires[i].first + " → " + paires[i].second);
    } else
      segments += n;
  }
  return segments;
}

// ---------- Fils : couper, trancher, fusionner, décaler les croisements (2.7, 2.19) ----------
bool couperFil(ConceptionElec& c, std::size_t feuille, const std::string& fil, Pt p) {
  exigerModifiable(c);
  Feuille* f = feuilleDe(c, feuille);
  if (!f) return false;
  for (std::size_t i = 0; i < f->fils.size(); ++i) {
    const Fil w = f->fils[i];
    if (w.uuid != fil || p == w.a || p == w.b || !surSegment(p, w.a, w.b)) continue;
    f->fils[i].b = p;
    f->fils.push_back({nouvelUuid(), p, w.b});
    return true;
  }
  return false;
}

bool trancherFil(ConceptionElec& c, std::size_t feuille, const std::string& fil, Pt p) {
  exigerModifiable(c);
  Feuille* f = feuilleDe(c, feuille);
  if (!f) return false;
  for (std::size_t i = 0; i < f->fils.size(); ++i) {
    const Fil w = f->fils[i];
    if (w.uuid != fil || !surSegment(p, w.a, w.b)) continue;
    // Un pas de grille retiré après le point, vers b : deux morceaux disjoints
    const double l = distance(w.a, w.b);
    if (l < 1) return false;
    const Pt dir{static_cast<Nm>(std::llround(static_cast<double>(w.b.x - w.a.x) / l * static_cast<double>(GRILLE_SCHEMA))),
                 static_cast<Nm>(std::llround(static_cast<double>(w.b.y - w.a.y) / l * static_cast<double>(GRILLE_SCHEMA)))};
    const Pt suite = p + dir;
    f->fils.erase(f->fils.begin() + static_cast<std::ptrdiff_t>(i));
    if (p != w.a) f->fils.push_back({nouvelUuid(), w.a, p});
    if (surSegment(suite, w.a, w.b) && suite != w.b) f->fils.push_back({nouvelUuid(), suite, w.b});
    std::erase_if(f->jonctions, [&](const Jonction& j) { return j.p == p; });
    return true;
  }
  return false;
}

int fusionnerFils(ConceptionElec& c, std::size_t feuille) {
  exigerModifiable(c);
  Feuille* f = feuilleDe(c, feuille);
  if (!f) return 0;
  int n = 0;
  for (bool encore = true; encore;) {
    encore = false;
    const Points pts = pointsDe(c, *f);
    std::set<Pt> jonctions;
    for (const auto& j : f->jonctions) jonctions.insert(j.p);
    for (std::size_t i = 0; i < f->fils.size() && !encore; ++i)
      for (std::size_t k = i + 1; k < f->fils.size() && !encore; ++k) {
        Fil& a = f->fils[i];
        const Fil& b = f->fils[k];
        // Un bout commun où ne se touchent que ces deux fils, et les deux dans le même axe
        for (const Pt commun : {a.a, a.b}) {
          if (commun != b.a && commun != b.b) continue;
          const auto bouts = pts.boutsDeFil.find(commun);
          if (bouts == pts.boutsDeFil.end() || bouts->second != 2 || pts.broches.count(commun) || pts.etiquettes.count(commun) || jonctions.count(commun)) continue;
          const Pt autreA = commun == a.a ? a.b : a.a, autreB = commun == b.a ? b.b : b.a;
          const bool aligne = (autreA.x == commun.x && autreB.x == commun.x) || (autreA.y == commun.y && autreB.y == commun.y);
          if (!aligne) continue;
          // Un point au milieu du segment fusionné (bout d'un autre fil, broche) le couperait : rien à craindre,
          // il n'y en a pas puisque seuls ces deux fils touchent le point commun
          a.a = autreA;
          a.b = autreB;
          a.rayon = std::max(a.rayon, b.rayon);  // un fil courbe le reste (2.19)
          f->fils.erase(f->fils.begin() + static_cast<std::ptrdiff_t>(k));
          ++n;
          encore = true;
          break;
        }
      }
  }
  return n;
}

int decalerCroisements(ConceptionElec& c, std::size_t feuille, const std::set<std::string>* selection) {
  exigerModifiable(c);
  Feuille* f = feuilleDe(c, feuille);
  if (!f) return 0;
  const Nm g = GRILLE_SCHEMA;
  int n = 0;
  // Croisements avec jonction posée ou déduite : deux fils qui se traversent sont d'abord coupés au point
  std::set<Pt> points;
  for (const auto& j : f->jonctions) points.insert(j.p);
  for (const auto& w : f->fils) {
    points.insert(w.a);
    points.insert(w.b);
  }
  for (const Pt p : points) {
    std::vector<std::size_t> traversants;
    for (std::size_t i = 0; i < f->fils.size(); ++i)
      if (p != f->fils[i].a && p != f->fils[i].b && surSegment(p, f->fils[i].a, f->fils[i].b)) traversants.push_back(i);
    const bool jonction = std::any_of(f->jonctions.begin(), f->jonctions.end(), [&](const Jonction& j) { return j.p == p; });
    std::size_t bouts = 0;
    for (const auto& w : f->fils) bouts += (w.a == p) + (w.b == p);
    // Quatre branches : quatre bouts, ou un fil traversant et deux bouts, ou deux traversants avec une jonction
    if (bouts + 2 * traversants.size() != 4 || (traversants.size() == 2 && !jonction)) continue;
    if (selection) {
      bool vise = false;
      for (const auto& w : f->fils)
        if ((w.a == p || w.b == p || surSegment(p, w.a, w.b)) && selection->count(w.uuid)) vise = true;
      if (!vise) continue;
    }
    for (std::size_t i : traversants) couperFil(c, feuille, f->fils[i].uuid, p);
    // Les quatre branches, par direction depuis p
    std::array<int, 4> branche{-1, -1, -1, -1};  // +x, +y, −x, −y
    for (std::size_t i = 0; i < f->fils.size(); ++i) {
      const Fil& w = f->fils[i];
      if (w.a != p && w.b != p) continue;
      const Pt o = w.a == p ? w.b : w.a;
      const int d = o.y == p.y ? (o.x > p.x ? 0 : 2) : o.x == p.x ? (o.y > p.y ? 1 : 3) : -1;
      if (d >= 0) branche[static_cast<std::size_t>(d)] = static_cast<int>(i);
    }
    if (std::any_of(branche.begin(), branche.end(), [](int x) { return x < 0; })) continue;
    auto longueur = [&](int d) {
      const Fil& w = f->fils[static_cast<std::size_t>(branche[static_cast<std::size_t>(d)])];
      return static_cast<Nm>(std::llround(distance(w.a, w.b)));
    };
    // La branche du bas (+y) rejoint la ligne horizontale un pas à droite (+x), par un petit crochet : deux T
    if (longueur(0) <= g || longueur(1) <= g) continue;
    Fil& bas = f->fils[static_cast<std::size_t>(branche[1])];
    (bas.a == p ? bas.a : bas.b) = p + Pt{0, g};
    f->fils.push_back({nouvelUuid(), p + Pt{0, g}, p + Pt{g, g}});
    f->fils.push_back({nouvelUuid(), p + Pt{g, g}, p + Pt{g, 0}});
    std::erase_if(f->jonctions, [&](const Jonction& j) { return j.p == p; });
    ++n;
  }
  return n;
}

// ---------- Réseaux et broches (2.20, 2.21) ----------
int renommerReseau(ConceptionElec& c, const std::string& ancien, const std::string& nouveau, int feuille) {
  exigerModifiable(c);
  if (ancien.empty() || nouveau.empty() || ancien == nouveau) return 0;
  int n = 0;
  for (std::size_t i = 0; i < c.feuilles.size(); ++i) {
    if (feuille >= 0 && static_cast<int>(i) != feuille) continue;
    Feuille& f = c.feuilles[i];
    for (auto& e : f.etiquettes)
      if (e.texte == ancien) {
        e.texte = nouveau;
        ++n;
      }
    for (auto& s : f.symboles)
      if (const DefSymbole* d = c.symbole(s.symbole); d && d->alimentation && (s.nomAlim.empty() ? d->nom : s.nomAlim) == ancien) {
        s.nomAlim = nouveau;
        ++n;
      }
  }
  // Sur toutes les feuilles, le cuivre suit le nouveau nom
  if (feuille < 0 && n > 0)
    for (auto& [cle, nom] : c.nomsReseaux)
      if (nom == ancien) nom = nouveau;
  return n;
}

int terminerBrochesLibres(ConceptionElec& c, std::size_t feuille, const std::set<std::string>& composants, bool etiquettes) {
  exigerModifiable(c);
  Feuille* f = feuilleDe(c, feuille);
  if (!f) return 0;
  const Connectivite k = connectivite(c);
  std::set<Pt> marques, bouts;
  for (const auto& m : f->nonConnexions) marques.insert(m.p);
  for (const auto& w : f->fils) {  // une broche où arrive un fil n'est pas libre, même si le fil ne mène nulle part
    bouts.insert(w.a);
    bouts.insert(w.b);
  }
  struct Ajout {
    Pt p, bout;
    std::string nom;
    int rotation;
  };
  std::vector<Ajout> ajouts;
  for (const auto& s : f->symboles) {
    if (!composants.count(s.composant)) continue;
    const DefSymbole* d = c.symbole(s.symbole);
    const ComposantElec* x = c.composant(s.composant);
    if (!d || !x) continue;
    for (const auto& b : brochesPlacees(s, *d)) {
      if (marques.count(b.p) || bouts.count(b.p) || b.broche->type == TypeBroche::NonConnectee) continue;
      const int r = k.reseau(s.composant, b.numero);
      const bool libre = r < 0 || (k.reseaux[static_cast<std::size_t>(r)].broches.size() <= 1 && !k.reseaux[static_cast<std::size_t>(r)].nomme);
      if (!libre) continue;
      // Le fil part vers l'extérieur du symbole (à l'opposé du corps), sur deux pas de grille
      const Pt versCorps = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
      const Pt bout{b.p.x - versCorps.x * 2 * GRILLE_SCHEMA, b.p.y - versCorps.y * 2 * GRILLE_SCHEMA};
      std::string nom = b.broche->nom;
      if (nom.empty() || nom == "~") nom = x->reference + "_" + b.numero;
      // Rotation de l'étiquette : son texte s'éloigne du symbole
      const int rotation = b.direction == 0 ? 2 : b.direction == 1 ? 1 : b.direction == 2 ? 0 : 3;
      ajouts.push_back({b.p, bout, nom, rotation});
    }
  }
  for (const auto& a : ajouts) {
    if (etiquettes) {
      f->fils.push_back({nouvelUuid(), a.p, a.bout});
      Etiquette e;
      e.uuid = nouvelUuid();
      e.texte = a.nom;
      e.p = a.bout;
      e.rotation = a.rotation;
      f->etiquettes.push_back(e);
    } else
      f->nonConnexions.push_back({a.p, nouvelUuid()});
  }
  return static_cast<int>(ajouts.size());
}

// ---------- Déplacer à fils élastiques (2.8), aligner, échanger (2.25) ----------
void deplacerSchema(ConceptionElec& c, std::size_t feuille, const std::set<std::string>& sel, Pt delta, bool elastique) {
  exigerModifiable(c);
  Feuille* f = feuilleDe(c, feuille);
  if (!f || (delta.x == 0 && delta.y == 0)) return;
  // Points qui bougent : broches des symboles choisis, étiquettes, jonctions, bouts des fils choisis
  std::set<Pt> bougent;
  for (auto& s : f->symboles)
    if (sel.count(s.uuid)) {
      if (const DefSymbole* d = c.symbole(s.symbole))
        for (const auto& b : brochesPlacees(s, *d)) bougent.insert(b.p);
      s.position = s.position + delta;
    }
  for (auto& e : f->etiquettes)
    if (sel.count(e.uuid)) {
      bougent.insert(e.p);
      e.p = e.p + delta;
    }
  for (auto& t : f->textes)
    if (sel.count(t.uuid)) t.p = t.p + delta;
  for (auto& j : f->jonctions)
    if (sel.count(idJonction(j))) {
      bougent.insert(j.p);
      j.p = j.p + delta;
    }
  for (auto& w : f->fils)
    if (sel.count(w.uuid)) {
      bougent.insert(w.a);
      bougent.insert(w.b);
      w.a = w.a + delta;
      w.b = w.b + delta;
    }
  // Marques de non-connexion et jonctions posées sur un point qui bouge le suivent (S17, S31)
  for (auto& n : f->nonConnexions)
    if (sel.count(idNonConnexion(n)) || bougent.count(n.p)) n.p = n.p + delta;
  for (auto& j : f->jonctions)
    if (!sel.count(idJonction(j)) && bougent.count(j.p)) j.p = j.p + delta;
  if (!elastique) return;
  // Fils élastiques : un fil non choisi accroché à un point qui bouge le suit. S'il était droit, il le reste : son
  // autre bout glisse avec lui quand c'est un coude libre (deux bouts de fil, rien d'autre), sinon un coude naît.
  const Points pts = pointsDe(c, *f);
  std::set<Pt> fixes;  // broches, étiquettes et jonctions : un coude n'y glisse pas
  for (const auto& [p, n] : pts.broches) fixes.insert(p);
  for (const auto& [p, n] : pts.etiquettes) fixes.insert(p);
  for (const auto& j : f->jonctions) fixes.insert(j.p);
  std::vector<Fil> ajouts;
  std::map<Pt, Pt> coudesDeplaces;  // coude libre déplacé : ancien point → nouveau
  for (auto& w : f->fils) {
    if (sel.count(w.uuid)) continue;
    const bool ma = bougent.count(w.a) > 0, mb = bougent.count(w.b) > 0;
    if (ma && mb) {
      w.a = w.a + delta;
      w.b = w.b + delta;
      continue;
    }
    if (!ma && !mb) continue;
    Pt& mobile = ma ? w.a : w.b;
    Pt& autre = ma ? w.b : w.a;
    const bool horizontal = w.a.y == w.b.y && w.a.x != w.b.x, vertical = w.a.x == w.b.x && w.a.y != w.b.y;
    const Pt ancienAutre = autre, ancienMobile = mobile;
    // Un point relié au milieu du fil (un fil en T, une broche, une étiquette, une jonction) : le fil ne quitte pas son
    // axe, le coude naît près du point mobile, et ce point reste relié (S16)
    auto auMilieu = [&](Pt q) { return q != ancienAutre && q != ancienMobile && surSegment(q, ancienAutre, ancienMobile); };
    bool milieu = false;
    for (const auto& [q, n] : pts.boutsDeFil) milieu |= auMilieu(q);
    for (const Pt& q : fixes) milieu |= auMilieu(q);
    if (milieu && (horizontal || vertical) && !((horizontal && delta.y == 0) || (vertical && delta.x == 0))) {
      const Pt arrivee = ancienMobile + delta;
      const Pt coude = horizontal ? Pt{arrivee.x, ancienAutre.y} : Pt{ancienAutre.x, arrivee.y};
      mobile = coude;
      if (coude != arrivee) ajouts.push_back({nouvelUuid(), coude, arrivee});
      continue;
    }
    mobile = mobile + delta;
    if (!horizontal && !vertical) continue;
    // Il reste droit si le décalage est dans son axe
    if ((horizontal && delta.y == 0) || (vertical && delta.x == 0)) continue;
    const auto bouts = pts.boutsDeFil.find(ancienAutre);
    // Coude libre : deux bouts de fil seulement, et l'autre fil perpendiculaire à celui-ci
    bool perpendiculaire = false;
    for (const auto& x : f->fils)
      if (&x != &w && (x.a == ancienAutre || x.b == ancienAutre)) perpendiculaire = horizontal ? (x.a.x == x.b.x) : (x.a.y == x.b.y);
    const bool coudeLibre = perpendiculaire && !fixes.count(ancienAutre) && bouts != pts.boutsDeFil.end() && bouts->second == 2 && !bougent.count(ancienAutre) &&
                            !coudesDeplaces.count(ancienAutre);
    if (coudeLibre) {
      // L'autre bout glisse perpendiculairement : le fil voisin, perpendiculaire, garde son axe
      const Pt nouveau = horizontal ? Pt{ancienAutre.x, ancienAutre.y + delta.y} : Pt{ancienAutre.x + delta.x, ancienAutre.y};
      autre = nouveau;
      coudesDeplaces[ancienAutre] = nouveau;
    } else {
      // Un coude naît près de l'autre bout : le fil approche le point mobile dans son axe d'origine
      const Pt coude = horizontal ? Pt{ancienAutre.x, mobile.y} : Pt{mobile.x, ancienAutre.y};
      autre = coude;
      ajouts.push_back({nouvelUuid(), ancienAutre, coude});
    }
  }
  // Les fils voisins des coudes déplacés suivent leur bout
  for (auto& w : f->fils) {
    if (sel.count(w.uuid)) continue;
    for (Pt* bout : {&w.a, &w.b})
      if (const auto it = coudesDeplaces.find(*bout); it != coudesDeplaces.end() && !bougent.count(*bout)) {
        // Seul l'autre fil du coude est concerné : celui qui n'a pas encore été déplacé à ce point
        const Pt autreBout = bout == &w.a ? w.b : w.a;
        if (autreBout != it->second) *bout = it->second;
      }
  }
  f->fils.insert(f->fils.end(), ajouts.begin(), ajouts.end());
  // Un fil devenu nul disparaît
  std::erase_if(f->fils, [](const Fil& w) { return w.a == w.b; });
}

int alignerSchema(ConceptionElec& c, std::size_t feuille, const std::set<std::string>& sel, Alignement a) {
  exigerModifiable(c);
  Feuille* f = feuilleDe(c, feuille);
  if (!f) return 0;
  struct Objet {
    std::string id;
    Boite b;
  };
  std::vector<Objet> objets;
  for (const auto& s : f->symboles)
    if (sel.count(s.uuid))
      if (const DefSymbole* d = c.symbole(s.symbole)) objets.push_back({s.uuid, boiteSymbole(s, *d)});
  for (const auto& e : f->etiquettes)
    if (sel.count(e.uuid)) objets.push_back({e.uuid, Boite{e.p.x, e.p.y, e.p.x, e.p.y}});
  if (objets.size() < 2) return 0;
  Boite tout;
  for (const auto& o : objets) tout.etendre(o.b);
  std::vector<std::pair<std::string, Pt>> deplacements;
  const bool repartir = a == Alignement::RepartirHorizontal || a == Alignement::RepartirVertical;
  if (repartir) {
    const bool h = a == Alignement::RepartirHorizontal;
    std::sort(objets.begin(), objets.end(), [h](const Objet& x, const Objet& y) { return h ? x.b.centre().x < y.b.centre().x : x.b.centre().y < y.b.centre().y; });
    const Nm debut = h ? objets.front().b.centre().x : objets.front().b.centre().y, fin = h ? objets.back().b.centre().x : objets.back().b.centre().y;
    const double pas = static_cast<double>(fin - debut) / static_cast<double>(objets.size() - 1);
    for (std::size_t i = 1; i + 1 < objets.size(); ++i) {
      const Nm cible = surGrille(Pt{static_cast<Nm>(std::llround(static_cast<double>(debut) + pas * static_cast<double>(i))), 0}).x;
      const Nm actuel = h ? objets[i].b.centre().x : objets[i].b.centre().y;
      const Nm d = surGrille(Pt{cible - actuel, 0}).x;
      deplacements.push_back({objets[i].id, h ? Pt{d, 0} : Pt{0, d}});
    }
  } else
    for (const auto& o : objets) {
      Pt d{};
      switch (a) {
        case Alignement::Gauche: d.x = tout.x0 - o.b.x0; break;
        case Alignement::Droite: d.x = tout.x1 - o.b.x1; break;
        case Alignement::Haut: d.y = tout.y0 - o.b.y0; break;
        case Alignement::Bas: d.y = tout.y1 - o.b.y1; break;
        case Alignement::CentreHorizontal: d.y = tout.centre().y - o.b.centre().y; break;
        case Alignement::CentreVertical: d.x = tout.centre().x - o.b.centre().x; break;
        default: break;
      }
      d = surGrille(d);
      if (d.x != 0 || d.y != 0) deplacements.push_back({o.id, d});
    }
  for (const auto& [id, d] : deplacements) deplacerSchema(c, feuille, {id}, d, true);
  return static_cast<int>(deplacements.size());
}

bool echangerSymboles(ConceptionElec& c, std::size_t feuille, const std::string& a, const std::string& b) {
  exigerModifiable(c);
  Feuille* f = feuilleDe(c, feuille);
  if (!f || a == b) return false;
  SymbolePlace *sa = nullptr, *sb = nullptr;
  for (auto& s : f->symboles) {
    if (s.uuid == a) sa = &s;
    if (s.uuid == b) sb = &s;
  }
  if (!sa || !sb) return false;
  const Pt pa = sa->position, pb = sb->position;
  // Chacun va à la place de l'autre, fils élastiques ; les orientations s'échangent ensuite
  deplacerSchema(c, feuille, {a}, pb - pa, true);
  for (auto& s : f->symboles)
    if (s.uuid == b) sb = &s;
  deplacerSchema(c, feuille, {b}, pa - pb, true);
  for (auto& s : f->symboles) {
    if (s.uuid == a) sa = &s;
    if (s.uuid == b) sb = &s;
  }
  std::swap(sa->rotation, sb->rotation);
  std::swap(sa->miroir, sb->miroir);
  return true;
}

}  // namespace prom::ecad
