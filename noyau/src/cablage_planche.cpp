// Prométhée : planche de faisceau (lot L19 de la 0.34 ; 19.11). Empilement des fils, topologie, tronc et branches,
// longueurs des fils, SVG à l'échelle 1, pages assemblées sur repères, coupe d'un tronçon. Voir cablage.hpp.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>

#include "cablage_interne.hpp"
#include "electrique_interne.hpp"
#include "pdf_vectoriel.hpp"
#include "promethee/cablage.hpp"

namespace prom::elec {

namespace {
constexpr double PI = 3.14159265358979323846;
std::string n3(double v) {
  char b[48];
  std::snprintf(b, sizeof b, "%.3f", v);
  std::string s = b;
  while (!s.empty() && s.back() == '0') s.pop_back();
  if (!s.empty() && s.back() == '.') s.pop_back();
  return s == "-0" || s.empty() ? std::string("0") : s;
}
std::string xml(const std::string& s) {
  std::string r;
  for (const char c : s) r += c == '<' ? std::string("&lt;") : c == '>' ? std::string("&gt;") : c == '&' ? std::string("&amp;") : c == '"' ? std::string("&quot;") : std::string(1, c);
  return r;
}
}  // namespace

double diametreFilSeul(const BibliothequeCablage& b, double section) {
  // Un fil seul de la bibliothèque à cette section, s'il donne son diamètre ; sinon le fil à isolant mince
  for (const auto& e : b.cables)
    if (e.nombreConducteurs == 1 && std::abs(e.section - section) < 1e-9 && e.diametreConducteur > 0) return e.diametreConducteur;
  return diametreFil(section);
}

std::string couleurAffichage(const std::string& code) {
  const std::string c = code.substr(0, code.find_first_of("/+"));
  static const std::map<std::string, std::string> t = {{"BK", "#1A1A1A"}, {"BN", "#8B4A1C"}, {"RD", "#D62828"}, {"OG", "#F77F00"}, {"YE", "#F2C500"}, {"GN", "#2A9D3F"},
                                                       {"BU", "#1D4ED8"}, {"VT", "#7B2CBF"}, {"GY", "#8D99AE"}, {"WH", "#F4F4F4"}, {"PK", "#FF8FAB"}, {"TQ", "#2EC4B6"},
                                                       {"LBU", "#5FA8D3"}, {"DBU", "#1E3A8A"}, {"GNYE", "#2A9D3F"}, {"GD", "#C9A227"}, {"SR", "#C0C0C0"}};
  const auto it = t.find(c);
  return it == t.end() ? std::string("#B0B0B0") : it->second;
}

// ======================================================================================================================
// Empilement
// ======================================================================================================================
double rapportEmpilement(int n) {
  // Rayon du plus petit cercle contenant n cercles de rayon 1 (empilements optimaux connus, tables de Friedman)
  static const double t[] = {0,      1,      2,      2.1547, 2.4142, 2.7013, 3,      3,      3.3048, 3.6131, 3.8130, 3.9238, 4.0296, 4.2361, 4.3284, 4.5214, 4.6154, 4.7920, 4.8637, 4.8637,
                             5.1223, 5.2523, 5.4397, 5.5452, 5.6517, 5.7528, 5.8281, 5.9063, 6.0149, 6.1385, 6.1977, 6.2915, 6.4295, 6.4867, 6.6017, 6.6826, 6.7462, 6.7588};
  if (n <= 0) return 0;
  if (n < static_cast<int>(std::size(t))) return t[n];
  // Au-delà : densité de l'empilement hexagonal (π/√12), corrigée du bord
  const double densite = PI / std::sqrt(12.0);
  return std::sqrt(n / densite) * (1 + 0.36 / std::sqrt(static_cast<double>(n)));
}

double diametreFaisceau(const std::vector<double>& diametres, double foisonnement) {
  if (diametres.empty()) return 0;
  const int n = static_cast<int>(diametres.size());
  double somme = 0, plusGrand = 0;
  for (const double d : diametres) somme += d * d, plusGrand = std::max(plusGrand, d);
  const double equivalent = std::sqrt(somme / n);  // exact pour des fils égaux
  return std::max(plusGrand, rapportEmpilement(n) * equivalent) * std::max(0.1, foisonnement);
}

double longueurRuban(double diametre, double longueur, double largeur, double recouvrement) {
  if (largeur <= 0 || longueur <= 0) return 0;
  const double pas = largeur * std::clamp(1 - recouvrement, 0.05, 1.0);  // avance par tour
  const double tours = longueur / pas;
  return tours * std::hypot(PI * diametre, pas);
}

// ======================================================================================================================
// Topologie
// ======================================================================================================================
void topologieParDefaut(Cablage& h, double longueurDefaut) {
  h.noeuds.clear();
  h.troncons.clear();
  std::map<std::string, std::string> noeudDe;
  for (const auto& k : h.connecteurs) {
    NoeudCablage n{ecad::nouvelUuid(), k.repere, k.repere, 0, 0};
    noeudDe[k.repere] = n.uuid;
    h.noeuds.push_back(n);
  }
  // Connecteurs reliés par un fil ou un câble
  std::set<std::string> relies;
  for (const auto& f : h.fils) {
    if (!f.de.empty()) relies.insert(f.de);
    if (!f.vers.empty()) relies.insert(f.vers);
  }
  std::vector<std::string> v(relies.begin(), relies.end());
  auto longueurCable = [&](const std::string& a, const std::string& b) {
    for (const auto& w : h.cables)
      if (((w.de == a && w.vers == b) || (w.de == b && w.vers == a)) && w.longueur > 0) return std::max(10.0, w.longueur * 1000 - 2 * h.surlongueur);
    return longueurDefaut;
  };
  if (v.size() == 2) {
    h.troncons.push_back({ecad::nouvelUuid(), noeudDe[v[0]], noeudDe[v[1]], longueurCable(v[0], v[1]), false});
    return;
  }
  if (v.size() > 2) {
    NoeudCablage d{ecad::nouvelUuid(), "N1", {}, 0, 0};
    h.noeuds.push_back(d);
    for (const auto& k : v) h.troncons.push_back({ecad::nouvelUuid(), d.uuid, noeudDe[k], longueurDefaut / 2, false});
  }
}

double longueurTroncon(const Cablage& h, const TronconCablage& t) {
  if (t.longueur > 0) return t.longueur;
  const NoeudCablage* a = nullptr;
  const NoeudCablage* b = nullptr;
  for (const auto& n : h.noeuds) {
    if (n.uuid == t.a) a = &n;
    if (n.uuid == t.b) b = &n;
  }
  return a && b ? std::hypot(a->x - b->x, a->y - b->y) : 0;
}

namespace {
struct Graphe {
  std::map<std::string, std::vector<std::pair<std::string, std::size_t>>> voisins;  // nœud → (voisin, tronçon)
  explicit Graphe(const Cablage& h) {
    for (std::size_t i = 0; i < h.troncons.size(); ++i) {
      voisins[h.troncons[i].a].push_back({h.troncons[i].b, i});
      voisins[h.troncons[i].b].push_back({h.troncons[i].a, i});
    }
  }
};
// Distances depuis « de » et tronçon d'arrivée de chaque nœud
std::map<std::string, std::pair<double, std::pair<std::string, std::size_t>>> dijkstra(const Cablage& h, const Graphe& g, const std::string& de, const std::function<double(std::size_t)>& poids) {
  std::map<std::string, std::pair<double, std::pair<std::string, std::size_t>>> d;
  using E = std::pair<double, std::string>;
  std::priority_queue<E, std::vector<E>, std::greater<E>> file;
  d[de] = {0, {std::string(), 0}};
  file.push({0, de});
  while (!file.empty()) {
    const auto [dist, n] = file.top();
    file.pop();
    if (dist > d[n].first) continue;
    const auto it = g.voisins.find(n);
    if (it == g.voisins.end()) continue;
    for (const auto& [m, t] : it->second) {
      const double nd = dist + poids(t);
      if (!d.count(m) || nd < d[m].first - 1e-12) {
        d[m] = {nd, {n, t}};
        file.push({nd, m});
      }
    }
  }
  (void)h;
  return d;
}
std::string noeudDuConnecteur(const Cablage& h, const std::string& repere) {
  for (const auto& n : h.noeuds)
    if (n.connecteur == repere) return n.uuid;
  return {};
}
}  // namespace

std::vector<std::string> cheminTroncons(const Cablage& h, const std::string& a, const std::string& b) {
  const Graphe g(h);
  const auto d = dijkstra(h, g, a, [&](std::size_t t) { return longueurTroncon(h, h.troncons[t]); });
  if (!d.count(b)) return {};
  std::vector<std::string> r;
  for (std::string n = b; n != a;) {
    const auto& [p, t] = d.at(n).second;
    r.insert(r.begin(), h.troncons[t].uuid);
    n = p;
  }
  return r;
}

int longueursDepuisPlanche(Cablage& h) {
  std::map<std::string, const TronconCablage*> parUuid;
  for (const auto& t : h.troncons) parUuid[t.uuid] = &t;
  std::map<std::string, std::string> epissureDe;
  for (const auto& e : h.epissures)
    for (const auto& f : e.fils) epissureDe[f] = e.noeud;
  int n = 0;
  for (auto& f : h.fils) {
    if (f.longueurForcee > 0) {
      f.longueur = f.longueurForcee;
      f.provenance = ProvenanceLongueur::Saisie;
      ++n;
      continue;
    }
    if (f.cavalier) continue;  // sa longueur saisie
    const std::string na = noeudDuConnecteur(h, f.de);
    std::string nb = noeudDuConnecteur(h, f.vers);
    if (nb.empty() && epissureDe.count(f.numero)) nb = epissureDe[f.numero];
    if (na.empty() && f.drain) {
      // Drain isolé d'un côté : il court tout le câble
      for (const auto& w : h.cables)
        if (w.uuid == f.cable) {
          const std::string a = noeudDuConnecteur(h, w.de);
          if (!a.empty() && !nb.empty()) {
            double l = 0;
            for (const auto& t : cheminTroncons(h, a, nb)) l += longueurTroncon(h, *parUuid[t]);
            f.longueur = (l + 2 * h.surlongueur) / 1000;
            f.provenance = ProvenanceLongueur::Planche;
            ++n;
          }
        }
      continue;
    }
    if (na.empty() || nb.empty()) continue;
    double l = 0;
    if (!f.chemin.empty()) {
      for (const auto& t : f.chemin)
        if (parUuid.count(t)) l += longueurTroncon(h, *parUuid[t]);
    } else {
      if (na != nb && cheminTroncons(h, na, nb).empty()) continue;  // nœuds non reliés
      for (const auto& t : cheminTroncons(h, na, nb)) l += longueurTroncon(h, *parUuid[t]);
    }
    f.longueur = (l + 2 * h.surlongueur) / 1000;
    f.provenance = ProvenanceLongueur::Planche;
    ++n;
  }
  for (auto& w : h.cables) {
    double l = 0;
    for (const auto& f : h.fils)
      if (f.cable == w.uuid) l = std::max(l, f.longueur);
    if (l > 0) {
      w.longueur = l;
      w.provenance = ProvenanceLongueur::Planche;
    }
  }
  return n;
}

std::vector<OccupationTroncon> occupationTroncons(const Cablage& h, const BibliothequeCablage& b) {
  std::map<std::string, OccupationTroncon> occ;
  for (const auto& t : h.troncons) occ[t.uuid].troncon = t.uuid;
  std::map<std::string, std::set<std::string>> cablesSur;  // tronçon → câbles (comptés une fois, à leur diamètre)
  std::map<std::string, std::string> epissureDe;
  for (const auto& e : h.epissures)
    for (const auto& f : e.fils) epissureDe[f] = e.noeud;
  for (const auto& f : h.fils) {
    std::string na = noeudDuConnecteur(h, f.de), nb = noeudDuConnecteur(h, f.vers);
    if (nb.empty() && epissureDe.count(f.numero)) nb = epissureDe[f.numero];
    if (na.empty() && !f.cable.empty())
      for (const auto& w : h.cables)
        if (w.uuid == f.cable) na = noeudDuConnecteur(h, w.de);
    if (na.empty() || nb.empty() || na == nb) continue;
    const std::vector<std::string> chemin = !f.chemin.empty() ? f.chemin : cheminTroncons(h, na, nb);
    for (const auto& t : chemin) {
      if (!occ.count(t)) continue;
      occ[t].fils.push_back(f.numero);
      if (!f.cable.empty()) cablesSur[t].insert(f.cable);
    }
  }
  std::vector<OccupationTroncon> r;
  for (const auto& t : h.troncons) {
    OccupationTroncon o = occ[t.uuid];
    std::vector<double> d;
    for (const auto& numero : o.fils)
      if (const FilCablage* f = h.fil(numero); f && f->cable.empty()) d.push_back(diametreFilSeul(b, f->section));
    for (const auto& c : cablesSur[t.uuid])
      for (const auto& w : h.cables)
        if (w.uuid == c) {
          const EntreeCable* e = b.cable(w.designation);
          d.push_back(e ? diametreCable(*e) : 5.0);
        }
    o.diametre = diametreFaisceau(d, h.foisonnement);
    r.push_back(o);
  }
  return r;
}

// ======================================================================================================================
// Planche
// ======================================================================================================================
PlancheDessinee dessinerPlanche(Cablage& h, const BibliothequeCablage& b, bool garder) {
  PlancheDessinee d;
  if (h.troncons.empty()) topologieParDefaut(h);
  if (h.noeuds.empty()) return d;
  const Graphe g(h);
  std::map<std::string, NoeudCablage*> noeud;
  for (auto& n : h.noeuds) noeud[n.uuid] = &n;
  auto reelle = [&](std::size_t t) {
    const double l = h.troncons[t].longueur > 0 ? h.troncons[t].longueur : longueurTroncon(h, h.troncons[t]);
    return l > 0 ? l : 200.0;
  };
  auto dessinee = [&](std::size_t t) { return h.troncons[t].coupure ? std::min(reelle(t), 150.0) : reelle(t); };
  // Tronc : plus long chemin de l'arbre (deux passes de plus longues distances)
  auto plusLoin = [&](const std::string& de) {
    const auto dist = dijkstra(h, g, de, reelle);
    std::string m = de;
    double best = -1;
    for (const auto& [n, v] : dist)
      if (v.first > best) best = v.first, m = n;
    return std::make_pair(m, dist);
  };
  const std::string u = plusLoin(h.noeuds.front().uuid).first;
  const auto loin = plusLoin(u);
  const std::string& v = loin.first;
  const auto& depuisU = loin.second;
  std::vector<std::string> tronc;
  std::vector<std::size_t> tronconsTronc;
  for (std::string n = v;;) {
    tronc.insert(tronc.begin(), n);
    if (n == u) break;
    const auto& [p, t] = depuisU.at(n).second;
    tronconsTronc.insert(tronconsTronc.begin(), t);
    n = p;
  }
  std::set<std::string> place;
  std::map<std::string, std::pair<double, double>> pos;
  const bool garde = garder && std::any_of(h.noeuds.begin(), h.noeuds.end(), [](const NoeudCablage& n) { return n.x != 0 || n.y != 0; });
  if (garde)
    for (const auto& n : h.noeuds) pos[n.uuid] = {n.x, n.y};
  // Tronc à l'horizontale
  double x = 0;
  for (std::size_t i = 0; i < tronc.size(); ++i) {
    if (i > 0) x += dessinee(tronconsTronc[i - 1]);
    if (!garde) pos[tronc[i]] = {x, 0};
    place.insert(tronc[i]);
  }
  std::set<std::size_t> tronconsDuTronc(tronconsTronc.begin(), tronconsTronc.end());
  // Branches de part et d'autre, à leur longueur ; sous-branches écartées de 35°
  int cote = 1;
  std::function<void(const std::string&, double)> deployer = [&](const std::string& n, double angle) {
    std::vector<std::pair<std::string, std::size_t>> enfants;
    for (const auto& [m, t] : g.voisins.at(n))
      if (!place.count(m)) enfants.push_back({m, t});
    for (std::size_t k = 0; k < enfants.size(); ++k) {
      const auto& [m, t] = enfants[k];
      const double ecart = enfants.size() == 1 ? 0 : (static_cast<double>(k) - (static_cast<double>(enfants.size()) - 1) / 2) * 35.0 * PI / 180;
      const double a = angle + ecart;
      place.insert(m);
      if (!garde || pos[m] == std::make_pair(0.0, 0.0)) pos[m] = {pos[n].first + dessinee(t) * std::cos(a), pos[n].second + dessinee(t) * std::sin(a)};
      if (g.voisins.count(m)) deployer(m, a);
    }
  };
  for (const auto& n : tronc) {
    if (!g.voisins.count(n)) continue;
    for (const auto& [m, t] : g.voisins.at(n)) {
      if (place.count(m)) continue;
      const double a = cote * PI / 2;  // y vers le bas : +90° sous le tronc, −90° au-dessus
      cote = -cote;
      place.insert(m);
      if (!garde || pos[m] == std::make_pair(0.0, 0.0)) pos[m] = {pos[n].first + dessinee(t) * std::cos(a), pos[n].second + dessinee(t) * std::sin(a)};
      if (g.voisins.count(m)) deployer(m, a);
    }
  }
  // Nœuds isolés (sans tronçon) : à la suite
  for (const auto& n : h.noeuds)
    if (!place.count(n.uuid)) pos[n.uuid] = {x + 100.0 * static_cast<double>(place.size()), 120}, place.insert(n.uuid);
  // Cadrage : marges pour les tableaux des connecteurs
  double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = -x0;
  for (const auto& [id, p] : pos) x0 = std::min(x0, p.first), y0 = std::min(y0, p.second), x1 = std::max(x1, p.first), y1 = std::max(y1, p.second);
  const double marge = 70;
  for (auto& [id, p] : pos) p.first += marge - x0, p.second += marge - y0;
  d.largeur = (x1 - x0) + 2 * marge;
  d.hauteur = (y1 - y0) + 2 * marge;
  for (auto& n : h.noeuds) {
    n.x = pos[n.uuid].first;
    n.y = pos[n.uuid].second;
    d.noeuds.push_back({n.uuid, n.nom, n.connecteur, n.x, n.y});
  }
  const auto occ = occupationTroncons(h, b);
  for (std::size_t i = 0; i < h.troncons.size(); ++i) {
    const auto& t = h.troncons[i];
    PlancheDessinee::Troncon r;
    r.uuid = t.uuid;
    r.x0 = pos[t.a].first;
    r.y0 = pos[t.a].second;
    r.x1 = pos[t.b].first;
    r.y1 = pos[t.b].second;
    r.longueur = reelle(i);
    r.diametre = i < occ.size() ? occ[i].diametre : 0;
    r.tronc = tronconsDuTronc.count(i) > 0;
    r.coupure = t.coupure;
    d.troncons.push_back(r);
  }
  for (const auto& n : tronc) d.tronc.push_back(n);
  return d;
}

namespace {
// Liste d'affichage commune au SVG et au PDF (mm, y vers le bas)
struct Trace {
  enum class Genre : std::uint8_t { Ligne, Cercle, Texte, Rect } genre = Genre::Ligne;
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0, r = 0, epaisseur = 0.25;
  std::string couleur = "#222222", texte, classe, donnees;
  double taille = 3, angle = 0, opacite = 1;
  bool tirets = false, remplir = false, gras = false;
  int alignement = -1;
};
std::vector<Trace> tracer(const Cablage& h, const BibliothequeCablage& b, const PlancheDessinee& d) {
  std::vector<Trace> v;
  auto ligne = [&](double x0, double y0, double x1, double y1, double ep, const std::string& c, const std::string& classe = {}, const std::string& donnees = {}) {
    Trace t;
    t.x0 = x0, t.y0 = y0, t.x1 = x1, t.y1 = y1, t.epaisseur = ep, t.couleur = c, t.classe = classe, t.donnees = donnees;
    v.push_back(t);
  };
  auto texte = [&](double x, double y, const std::string& s, double taille, double angle = 0, int alignement = 0, bool gras = false, const std::string& c = "#222222") {
    Trace t;
    t.genre = Trace::Genre::Texte;
    t.x0 = x, t.y0 = y, t.texte = s, t.taille = taille, t.angle = angle, t.alignement = alignement, t.gras = gras, t.couleur = c;
    v.push_back(t);
  };
  std::map<std::string, const TronconCablage*> troncons;
  for (const auto& t : h.troncons) troncons[t.uuid] = &t;
  // Tronçons à leur diamètre, protections par-dessus
  for (const auto& t : d.troncons) ligne(t.x0, t.y0, t.x1, t.y1, std::max(1.0, t.diametre), t.tronc ? "#4B5563" : "#6B7280", "troncon");
  for (const auto& p : h.protections) {
    const auto it = std::find_if(d.troncons.begin(), d.troncons.end(), [&](const PlancheDessinee::Troncon& t) { return t.uuid == p.troncon; });
    if (it == d.troncons.end()) continue;
    const EntreeProtection* e = b.protection(p.reference);
    const double L = std::hypot(it->x1 - it->x0, it->y1 - it->y0), reel = std::max(1e-6, it->longueur);
    if (L <= 0) continue;
    const double ux = (it->x1 - it->x0) / L, uy = (it->y1 - it->y0) / L;
    const double debut = std::clamp(p.debut / reel, 0.0, 1.0) * L, fin = (p.fin > 0 ? std::clamp(p.fin / reel, 0.0, 1.0) : 1.0) * L;
    const double ep = std::max(1.0, it->diametre) + 2 * ((e ? e->epaisseur : 0.5) + p.decalage);
    const std::string genre = e ? e->genre : std::string("gaine");
    const std::string couleur = genre == "ruban" ? "#1F2937" : genre == "manchon" ? "#B91C1C" : genre == "étiquette" ? "#FFFFFF" : "#0F766E";
    Trace t;
    t.x0 = it->x0 + ux * debut, t.y0 = it->y0 + uy * debut, t.x1 = it->x0 + ux * fin, t.y1 = it->y0 + uy * fin, t.epaisseur = ep, t.couleur = couleur, t.opacite = 0.45, t.classe = "protection";
    v.push_back(t);
    const double angle = std::atan2(uy, ux) * 180 / PI;
    const double mx = (t.x0 + t.x1) / 2, my = (t.y0 + t.y1) / 2;
    if (!p.texte.empty()) {
      // Étiquette sur plusieurs lignes, le long du tronçon
      std::vector<std::string> lignes;
      std::stringstream ss(p.texte);
      for (std::string l; std::getline(ss, l);) lignes.push_back(l);
      for (std::size_t i = 0; i < lignes.size(); ++i) {
        const double dec = ep / 2 + 3 + 3.2 * static_cast<double>(i);
        texte(mx - uy * dec, my + ux * dec, lignes[i], 2.5, angle, 0);
      }
    } else
      texte(mx - uy * (ep / 2 + 3), my + ux * (ep / 2 + 3), p.reference, 2.2, angle, 0, false, "#0F766E");
  }
  // Cotes : longueur réelle de chaque tronçon, parallèle à lui
  for (const auto& t : d.troncons) {
    const double L = std::hypot(t.x1 - t.x0, t.y1 - t.y0);
    if (L <= 0) continue;
    const double ux = (t.x1 - t.x0) / L, uy = (t.y1 - t.y0) / L, nx = -uy, ny = ux;
    const double off = std::max(1.0, t.diametre) / 2 + 9;
    const double ax = t.x0 + nx * off, ay = t.y0 + ny * off, bx = t.x1 + nx * off, by = t.y1 + ny * off;
    ligne(t.x0 + nx * (off - 6), t.y0 + ny * (off - 6), ax + nx * 1.5, ay + ny * 1.5, 0.18, "#374151");
    ligne(t.x1 + nx * (off - 6), t.y1 + ny * (off - 6), bx + nx * 1.5, by + ny * 1.5, 0.18, "#374151");
    ligne(ax, ay, bx, by, 0.25, "#111827", "cote", n3(t.longueur));
    // Traits obliques aux extrémités (cote de câblage)
    for (const auto& [px, py] : {std::make_pair(ax, ay), std::make_pair(bx, by)}) ligne(px - (ux + nx) * 1.2, py - (uy + ny) * 1.2, px + (ux + nx) * 1.2, py + (uy + ny) * 1.2, 0.3, "#111827");
    const double angle = std::atan2(uy, ux) * 180 / PI;
    const bool retourne = angle > 90.0001 || angle < -90.0001;
    texte((ax + bx) / 2 + nx * 2.2, (ay + by) / 2 + ny * 2.2, detail::nombreFr(std::round(t.longueur)) + (t.coupure ? " (coupure)" : ""), 3, retourne ? angle + 180 : angle, 0, true);
    if (t.coupure) {
      // Symbole de coupure : deux traits obliques au milieu du tronçon
      const double mx = (t.x0 + t.x1) / 2, my = (t.y0 + t.y1) / 2, e = std::max(2.0, t.diametre);
      for (const double k : {-1.5, 1.5}) ligne(mx + ux * k - nx * e, my + uy * k - ny * e, mx + ux * k + ux * 2 + nx * e, my + uy * k + uy * 2 + ny * e, 0.35, "#111827", "coupure");
    }
  }
  // Nœuds : connecteurs (boîte, repère, tableau de cavités), dérivations
  std::map<std::string, std::vector<const FilCablage*>> filsDe;
  for (const auto& f : h.fils) {
    if (!f.de.empty()) filsDe[f.de].push_back(&f);
    if (!f.vers.empty() && f.vers != f.de) filsDe[f.vers].push_back(&f);
  }
  for (const auto& n : d.noeuds) {
    if (n.connecteur.empty()) {
      Trace c;
      c.genre = Trace::Genre::Cercle;
      c.x0 = n.x, c.y0 = n.y, c.r = 1.6, c.remplir = true, c.couleur = "#111827";
      v.push_back(c);
      texte(n.x + 3, n.y - 3, n.nom, 2.5, 0, -1);
      continue;
    }
    const ConnecteurCablage* k = h.connecteur(n.connecteur);
    if (k && k->dessin && !k->dessin->vus.empty()) {
      // Projection du modèle 3D, tournée et répétée, numéros de cavités compris
      const DessinConnecteur& d = *k->dessin;
      const double a = d.rotation * PI / 180, ca = std::cos(a), sa = std::sin(a);
      const double pas = std::max(d.largeur, 1.0) + 3;
      for (int rep = 0; rep < std::max(1, d.repetitions); ++rep) {
        const double dx = (rep - (std::max(1, d.repetitions) - 1) / 2.0) * pas;
        auto P = [&](const std::pair<double, double>& p) {
          // Dessin en y vers le haut (celui de la projection), tourné dans le sens direct ; la planche a y vers le bas
          const double x = p.first + dx, y = p.second;
          return std::make_pair(n.x + x * ca - y * sa, n.y - (x * sa + y * ca));
        };
        for (const bool cache : {true, false})
          for (const auto& l : cache ? d.caches : d.vus)
            for (std::size_t i = 1; i < l.size(); ++i) {
              const auto p0 = P(l[i - 1]), p1 = P(l[i]);
              Trace t;
              t.x0 = p0.first, t.y0 = p0.second, t.x1 = p1.first, t.y1 = p1.second, t.epaisseur = cache ? 0.18 : 0.35, t.couleur = cache ? "#6B7280" : "#111827";
              t.tirets = cache;
              t.classe = "dessinConnecteur";
              v.push_back(t);
            }
        for (const auto& [numero, p] : d.cavites) {
          const auto q = P(p);
          texte(q.first, q.second + 0.8, numero, 1.8, 0, 0, false, "#1D4ED8");
        }
      }
      texte(n.x, n.y - std::max(d.hauteur, 6.0) / 2 - 2.5, n.connecteur, 3.2, 0, 0, true);
    } else {
      Trace r;
      r.genre = Trace::Genre::Rect;
      r.x0 = n.x - 7, r.y0 = n.y - 5, r.x1 = n.x + 7, r.y1 = n.y + 5, r.couleur = "#111827", r.epaisseur = 0.35, r.classe = "connecteur";
      v.push_back(r);
      texte(n.x, n.y + 1.2, n.connecteur, 3.2, 0, 0, true);
    }
    // Tableau des cavités : à gauche d'un nœud de gauche, à droite sinon
    if (!k) continue;
    std::vector<std::vector<std::string>> lignes;
    for (const auto& c : k->cavites) {
      const FilCablage* f = nullptr;
      for (const FilCablage* x : filsDe[k->repere])
        if ((x->de == k->repere && x->deCavite == c.numero) || (x->vers == k->repere && x->versCavite == c.numero)) f = x;
      lignes.push_back({c.numero, f ? f->numero : std::string(), f ? f->couleur : std::string(), f ? detail::nombreFr(f->section) : std::string(), c.contact});
    }
    const double largeurs[] = {8, 9, 13, 9, 22}, haut = 3.6;
    double total = 0;
    for (double w : largeurs) total += w;
    const bool aGauche = n.x < d.largeur / 2;
    const double tx = aGauche ? n.x - 10 - total : n.x + 10, ty = n.y + 7;
    const char* entetes[] = {"Cav.", "Fil", "Couleur", "mm²", "Contact"};
    for (std::size_t r2 = 0; r2 <= lignes.size(); ++r2) {
      double cx = tx;
      for (std::size_t c = 0; c < 5; ++c) {
        Trace cell;
        cell.genre = Trace::Genre::Rect;
        cell.x0 = cx, cell.y0 = ty + haut * static_cast<double>(r2), cell.x1 = cx + largeurs[c], cell.y1 = cell.y0 + haut, cell.epaisseur = 0.15, cell.couleur = "#6B7280";
        v.push_back(cell);
        texte(cx + 1, cell.y0 + haut - 1, r2 == 0 ? std::string(entetes[c]) : lignes[r2 - 1][c], 2.1, 0, -1, r2 == 0);
        cx += largeurs[c];
      }
    }
    texte(tx, ty - 1.5, k->repere + (k->reference.empty() ? std::string() : " — " + k->reference), 2.6, 0, -1, true);
  }
  return v;
}
std::string svgTraces(const std::vector<Trace>& v) {
  std::ostringstream o;
  for (const auto& t : v) {
    switch (t.genre) {
      case Trace::Genre::Ligne:
        o << "<line" << (t.classe.empty() ? "" : " class=\"" + t.classe + "\"") << (t.donnees.empty() ? "" : " data-longueur=\"" + t.donnees + "\"") << " x1=\"" << n3(t.x0) << "\" y1=\"" << n3(t.y0)
          << "\" x2=\"" << n3(t.x1) << "\" y2=\"" << n3(t.y1) << "\" stroke=\"" << t.couleur << "\" stroke-width=\"" << n3(t.epaisseur) << "\" stroke-linecap=\"" << (t.classe == "troncon" ? "round" : "butt")
          << "\"" << (t.opacite < 1 ? " stroke-opacity=\"" + n3(t.opacite) + "\"" : std::string()) << (t.tirets ? " stroke-dasharray=\"1.5 0.6\"" : "") << "/>\n";
        break;
      case Trace::Genre::Cercle:
        o << "<circle cx=\"" << n3(t.x0) << "\" cy=\"" << n3(t.y0) << "\" r=\"" << n3(t.r) << "\" fill=\"" << (t.remplir ? t.couleur : std::string("none")) << "\" stroke=\"" << t.couleur
          << "\" stroke-width=\"" << n3(t.epaisseur) << "\"/>\n";
        break;
      case Trace::Genre::Rect:
        o << "<rect" << (t.classe.empty() ? "" : " class=\"" + t.classe + "\"") << " x=\"" << n3(t.x0) << "\" y=\"" << n3(t.y0) << "\" width=\"" << n3(t.x1 - t.x0) << "\" height=\"" << n3(t.y1 - t.y0)
          << "\" fill=\"" << (t.remplir ? t.couleur : std::string("white")) << "\" stroke=\"" << t.couleur << "\" stroke-width=\"" << n3(t.epaisseur) << "\"/>\n";
        break;
      case Trace::Genre::Texte:
        o << "<text x=\"" << n3(t.x0) << "\" y=\"" << n3(t.y0) << "\" font-family=\"Helvetica, Arial, sans-serif\" font-size=\"" << n3(t.taille) << "\"" << (t.gras ? " font-weight=\"bold\"" : "")
          << " fill=\"" << t.couleur << "\" text-anchor=\"" << (t.alignement < 0 ? "start" : t.alignement == 0 ? "middle" : "end") << "\""
          << (t.angle != 0 ? " transform=\"rotate(" + n3(t.angle) + " " + n3(t.x0) + " " + n3(t.y0) + ")\"" : std::string()) << ">" << xml(t.texte) << "</text>\n";
        break;
    }
  }
  return o.str();
}
void pdfTraces(pdfv::Document& p, const std::vector<Trace>& v) {
  for (const auto& t : v) {
    p.couleurHex(t.couleur);
    p.epaisseur(t.epaisseur);
    switch (t.genre) {
      case Trace::Genre::Ligne:
        if (t.tirets) p.tirets(true);
        p.ligne(t.x0, t.y0, t.x1, t.y1);
        if (t.tirets) p.tirets(false);
        break;
      case Trace::Genre::Cercle: p.cercle(t.x0, t.y0, t.r, t.remplir); break;
      case Trace::Genre::Rect: p.rectangle(t.x0, t.y0, t.x1 - t.x0, t.y1 - t.y0, false); break;
      case Trace::Genre::Texte: p.texte(t.x0, t.y0, t.texte, t.taille, t.gras, -t.angle, t.alignement); break;
    }
  }
}
}  // namespace

void detail::plancheDansPdf(pdfv::Document& p, const Cablage& h, const BibliothequeCablage& b, const PlancheDessinee& d, double k, double dx, double dy) {
  p.enregistrer();
  p.transformer(k, dx, dy);
  pdfTraces(p, tracer(h, b, d));
  p.restaurer();
}

std::string svgPlanche(const Cablage& h, const BibliothequeCablage& b, const PlancheDessinee& d, double echelle) {
  std::ostringstream o;
  o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << n3(d.largeur * echelle) << "mm\" height=\"" << n3(d.hauteur * echelle) << "mm\" viewBox=\"0 0 "
    << n3(d.largeur) << " " << n3(d.hauteur) << "\">\n<title>" << xml(h.nom) << " — planche à l'échelle " << n3(echelle) << "</title>\n<rect x=\"0\" y=\"0\" width=\"" << n3(d.largeur) << "\" height=\""
    << n3(d.hauteur) << "\" fill=\"white\"/>\n";
  o << svgTraces(tracer(h, b, d));
  o << "<text x=\"10\" y=\"" << n3(d.hauteur - 8) << "\" font-family=\"Helvetica, Arial, sans-serif\" font-size=\"3.5\" fill=\"#374151\">" << xml(h.nom) << " — longueurs en mm"
    << (echelle == 1 ? " — échelle 1:1" : "") << "</text>\n</svg>\n";
  return o.str();
}

PagesPlanche pagesPlanche(const Cablage& h, const BibliothequeCablage& b, const PlancheDessinee& d, const std::string& format, double recouvrement) {
  PagesPlanche r;
  const double pw = format == "A3" ? 420 : format == "A2" ? 594 : 297, ph = format == "A3" ? 297 : format == "A2" ? 420 : 210;  // à l'italienne
  const double marge = 10, zw = pw - 2 * marge, zh = ph - 2 * marge;
  const double pasX = zw - recouvrement, pasY = zh - recouvrement;
  r.colonnes = std::max(1, static_cast<int>(std::ceil((d.largeur - recouvrement) / pasX - 1e-9)));
  r.lignes = std::max(1, static_cast<int>(std::ceil((d.hauteur - recouvrement) / pasY - 1e-9)));
  const auto traces = tracer(h, b, d);
  pdfv::Document p;
  for (int i = 0; i < r.lignes; ++i)
    for (int j = 0; j < r.colonnes; ++j) {
      p.page(pw, ph);
      p.decoupe(marge, marge, zw, zh);
      p.decaler(marge - j * pasX, marge - i * pasY);
      pdfTraces(p, traces);
      p.finDecoupe();
      // Croix de repérage aux coins de la zone, pour l'assemblage ; bandes de recouvrement en tirets
      p.couleur(0, 0, 0);
      p.epaisseur(0.2);
      for (const auto& [cx, cy] : {std::make_pair(marge, marge), std::make_pair(marge + zw, marge), std::make_pair(marge, marge + zh), std::make_pair(marge + zw, marge + zh)}) {
        p.ligne(cx - 5, cy, cx + 5, cy);
        p.ligne(cx, cy - 5, cx, cy + 5);
        p.cercle(cx, cy, 2);
      }
      p.tirets(true);
      if (j + 1 < r.colonnes) p.ligne(marge + zw - recouvrement, marge, marge + zw - recouvrement, marge + zh);
      if (i + 1 < r.lignes) p.ligne(marge, marge + zh - recouvrement, marge + zw, marge + zh - recouvrement);
      p.tirets(false);
      p.texte(marge, ph - 3.5, h.nom + " — page ligne " + std::to_string(i + 1) + "/" + std::to_string(r.lignes) + ", colonne " + std::to_string(j + 1) + "/" + std::to_string(r.colonnes) +
                                    " — échelle 1:1 — recouvrement " + detail::nombreFr(recouvrement) + " mm",
              3, false);
    }
  r.pdf = p.terminer(h.nom + " — planche");
  return r;
}

Faisceau planchePourAtelier(const Cablage& h, const ProjetElectrique& p) {
  Faisceau f;
  f.uuid = h.uuid.empty() ? ecad::nouvelUuid() : h.uuid;
  f.nom = h.nom;
  f.surlongueur = h.surlongueur;
  for (const auto& n : h.noeuds) {
    NoeudFaisceau x;
    x.uuid = n.uuid;
    x.nom = n.nom;
    x.p = ecad::ptMm(n.x, n.y);
    if (const ConnecteurCablage* k = h.connecteur(n.connecteur); k && !k->carte && !k->composant.empty() && p.schema.composant(k->composant)) x.composant = k->composant;
    f.noeuds.push_back(x);
  }
  for (const auto& t : h.troncons) {
    BrancheFaisceau b{t.a, t.b, t.longueur, {}};
    for (const auto& pr : h.protections)
      if (pr.troncon == t.uuid) b.protection += (b.protection.empty() ? "" : ", ") + pr.reference;
    f.branches.push_back(b);
  }
  return f;
}

int reprendrePlanche(Cablage& h, const Faisceau& f) {
  int n = 0;
  for (auto& x : h.noeuds)
    for (const auto& y : f.noeuds)
      if (y.uuid == x.uuid) {
        const double nx = static_cast<double>(y.p.x) / ecad::NM_PAR_MM, ny = static_cast<double>(y.p.y) / ecad::NM_PAR_MM;
        if (std::abs(nx - x.x) > 1e-6 || std::abs(ny - x.y) > 1e-6) x.x = nx, x.y = ny, ++n;
      }
  for (auto& t : h.troncons)
    for (const auto& b : f.branches)
      if (((b.a == t.a && b.b == t.b) || (b.a == t.b && b.b == t.a)) && b.longueur > 0 && std::abs(b.longueur - t.longueur) > 1e-6) t.longueur = b.longueur, ++n;
  return n;
}

std::string svgCoupe(const Cablage& h, const BibliothequeCablage& b, const std::string& troncon) {
  const auto occ = occupationTroncons(h, b);
  const auto it = std::find_if(occ.begin(), occ.end(), [&](const OccupationTroncon& o) { return o.troncon == troncon; });
  if (it == occ.end()) return {};
  // Cercles : fils seuls à leur couleur, câbles en gris à leur diamètre ; disposés sur une grille hexagonale
  struct Disque {
    double d = 0;
    std::string couleur, texte;
  };
  std::vector<Disque> disques;
  std::set<std::string> cables;
  for (const auto& n : it->fils)
    if (const FilCablage* f = h.fil(n)) {
      if (f->cable.empty()) disques.push_back({diametreFilSeul(b, f->section), couleurAffichage(f->couleur), f->numero});
      else if (cables.insert(f->cable).second)
        for (const auto& w : h.cables)
          if (w.uuid == f->cable) {
            const EntreeCable* e = b.cable(w.designation);
            disques.push_back({e ? diametreCable(*e) : 5.0, "#9CA3AF", w.reference});
          }
    }
  std::sort(disques.begin(), disques.end(), [](const Disque& a, const Disque& c) { return a.d > c.d; });
  const double D = it->diametre;
  double epaisseurs = 0;
  std::vector<std::pair<const EntreeProtection*, double>> couches;
  for (const auto& p : h.protections)
    if (p.troncon == troncon)
      if (const EntreeProtection* e = b.protection(p.reference)) couches.push_back({e, p.decalage}), epaisseurs += e->epaisseur + p.decalage;
  const double R = D / 2 + epaisseurs + 4, taille = 2 * R;
  std::ostringstream o;
  o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << n3(taille * 10) << "mm\" height=\"" << n3(taille * 10 + 20) << "mm\" viewBox=\"" << n3(-R) << " "
    << n3(-R) << " " << n3(taille) << " " << n3(taille + 2) << "\">\n";
  double r = D / 2;
  for (const auto& [e, dec] : couches) {
    o << "<circle cx=\"0\" cy=\"0\" r=\"" << n3(r + dec + e->epaisseur / 2) << "\" fill=\"none\" stroke=\"#0F766E\" stroke-width=\"" << n3(e->epaisseur) << "\"><title>" << xml(e->reference) << "</title></circle>\n";
    r += dec + e->epaisseur;
  }
  o << "<circle class=\"enveloppe\" cx=\"0\" cy=\"0\" r=\"" << n3(D / 2) << "\" fill=\"none\" stroke=\"#111827\" stroke-width=\"0.05\" stroke-dasharray=\"0.2 0.15\"/>\n";
  // Grille hexagonale au pas du plus gros disque, points les plus proches du centre d'abord
  if (!disques.empty()) {
    const double pas = disques.front().d;
    std::vector<std::pair<double, double>> points;
    const int n = static_cast<int>(std::ceil(D / pas)) + 2;
    for (int i = -n; i <= n; ++i)
      for (int j = -n; j <= n; ++j) points.push_back({(i + (j % 2 != 0 ? 0.5 : 0.0)) * pas, j * pas * std::sqrt(3.0) / 2});
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& c) { return std::hypot(a.first, a.second) < std::hypot(c.first, c.second); });
    for (std::size_t k = 0; k < disques.size() && k < points.size(); ++k) {
      const auto& [x, y] = points[k];
      o << "<circle cx=\"" << n3(x) << "\" cy=\"" << n3(y) << "\" r=\"" << n3(disques[k].d / 2) << "\" fill=\"" << disques[k].couleur << "\" stroke=\"#111827\" stroke-width=\"0.05\"><title>"
        << xml(disques[k].texte) << "</title></circle>\n";
    }
  }
  o << "<text x=\"0\" y=\"" << n3(R + 1.5) << "\" font-family=\"Helvetica, Arial, sans-serif\" font-size=\"1.2\" text-anchor=\"middle\">Ø " << detail::nombreFr(std::round(D * 10) / 10) << " mm, "
    << it->fils.size() << " fil(s)</text>\n</svg>\n";
  return o.str();
}

}  // namespace prom::elec
