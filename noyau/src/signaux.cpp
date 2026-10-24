// Prométhée : modèle des signaux (8.26). Voir signaux.hpp. Licence GPL-3.0-only.
#include "promethee/signaux.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>

#include "promethee/circuit_edition.hpp"
#include "promethee/lignes_transmission.hpp"
#include "promethee/nomenclature_meca.hpp"

namespace prom::ecad {

namespace {
std::string mmTexte(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.2f", v);
  std::string s = b;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
std::string prefixeRepere(const std::string& r) {
  std::string p;
  for (char ch : r) {
    if (std::isdigit(static_cast<unsigned char>(ch))) break;
    p += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  return p;
}
bool prefixeSerie(const std::string& p) { return p == "R" || p == "C" || p == "L" || p == "FB" || p == "RN" || p == "FL"; }

// Broches posées sur la carte, par composant : numéro → réseau (pastilles ; une broche à plusieurs pastilles compte une
// fois)
struct VueBroches {
  std::map<std::string, std::map<std::string, std::string>> parComposant;
  std::map<std::string, int> broches;   // réseau → broches
  std::map<std::string, int> passifs;   // réseau → broches de passifs à deux broches
  std::set<std::string> alimentations;  // réseaux portés par un symbole d'alimentation
};
VueBroches vueBroches(const ConceptionElec& c, const Connectivite& k, const std::vector<PastillePlacee>& pp) {
  VueBroches v;
  for (const auto& p : pp)
    if (!p.reseau.empty()) v.parComposant[p.composant][p.numero] = p.reseau;
  for (const auto& [comp, broches] : v.parComposant) {
    const ComposantElec* x = c.composant(comp);
    const bool passif2 = x && broches.size() == 2 && prefixeSerie(prefixeRepere(x->reference));
    for (const auto& [num, r] : broches) {
      ++v.broches[r];
      if (passif2) ++v.passifs[r];
    }
  }
  for (const auto& r : k.reseaux)
    if (!r.alimentations.empty()) {
      v.alimentations.insert(r.nom);
      for (const auto& a : r.alias) v.alimentations.insert(a);
    }
  return v;
}
bool rail(const VueBroches& v, const std::string& r) {
  if (r.empty() || v.alimentations.count(r)) return true;
  const auto it = v.passifs.find(r);
  return it != v.passifs.end() && it->second >= 3;
}

std::pair<std::string, std::string> broche(const std::string& s) {
  const auto p = s.rfind('.');
  return p == std::string::npos ? std::pair{s, std::string()} : std::pair{s.substr(0, p), s.substr(p + 1)};
}
std::string remplacer(std::string s, const std::string& de, const std::string& par) {
  for (std::size_t i = s.find(de); i != std::string::npos; i = s.find(de, i + par.size())) s.replace(i, de.size(), par);
  return s;
}
double mmJ(const nlohmann::json& o, const char* cle, double defaut = 0) { return o.contains(cle) && o[cle].is_number() ? o[cle].get<double>() : defaut; }
}  // namespace

std::vector<Signal> signauxSerie(const ConceptionElec& c) {
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const VueBroches v = vueBroches(c, k, pp);
  // Graphe des réseaux reliés par un composant série
  std::map<std::string, std::vector<std::string>> voisins;
  for (const auto& [comp, broches] : v.parComposant) {
    const ComposantElec* x = c.composant(comp);
    if (!x || broches.size() != 2 || !prefixeSerie(prefixeRepere(x->reference))) continue;
    const std::string a = broches.begin()->second, b = std::next(broches.begin())->second;
    if (a == b || rail(v, a) || rail(v, b)) continue;
    voisins[a].push_back(b);
    voisins[b].push_back(a);
  }
  std::vector<Signal> r;
  std::set<std::string> vus;
  for (const auto& [depart, _] : voisins) {
    if (vus.count(depart)) continue;
    // Le groupe entier, puis une extrémité de la chaîne (un seul voisin) comme départ
    std::vector<std::string> groupe;
    std::vector<std::string> pile{depart};
    std::set<std::string> dans;
    while (!pile.empty()) {
      const std::string n = pile.back();
      pile.pop_back();
      if (!dans.insert(n).second) continue;
      groupe.push_back(n);
      for (const auto& m : voisins[n]) pile.push_back(m);
    }
    vus.insert(dans.begin(), dans.end());
    std::sort(groupe.begin(), groupe.end(), ordreNaturel);
    std::string bout = groupe.front();
    for (const auto& n : groupe)
      if (voisins[n].size() == 1) {
        bout = n;
        break;
      }
    Signal s;
    std::set<std::string> parcourus;
    std::function<void(const std::string&)> suivre = [&](const std::string& n) {
      if (!parcourus.insert(n).second) return;
      s.reseaux.push_back(n);
      for (const auto& m : voisins[n]) suivre(m);
    };
    suivre(bout);
    s.nom = std::min(s.reseaux.front(), s.reseaux.back(), ordreNaturel);
    r.push_back(std::move(s));
  }
  std::sort(r.begin(), r.end(), [](const Signal& a, const Signal& b) { return ordreNaturel(a.nom, b.nom); });
  return r;
}

int creerSignauxSerie(ConceptionElec& c) {
  exigerModifiable(c);
  int n = 0;
  auto& S = c.circuit.regles.avancees.signaux;
  for (auto& s : signauxSerie(c)) {
    const std::set<std::string> ens(s.reseaux.begin(), s.reseaux.end());
    const bool deja = std::any_of(S.begin(), S.end(), [&](const Signal& x) { return std::set<std::string>(x.reseaux.begin(), x.reseaux.end()) == ens; });
    if (deja) continue;
    S.push_back(std::move(s));
    ++n;
  }
  return n;
}

double longueurBoitier(const ConceptionElec& c, const std::string& reference, const std::string& numero) {
  const auto it = c.extensions.find("longueursBoitier");
  if (it == c.extensions.end() || !it->second.contains(reference)) return 0;
  const auto& t = it->second[reference];
  return t.contains(numero) && t[numero].is_number() ? t[numero].get<double>() * 1e6 : 0;
}

int importerLongueursBoitier(ConceptionElec& c, const std::string& reference, const std::string& csv) {
  exigerModifiable(c);
  const auto lignes = lireTableau(csv);
  if (lignes.empty()) return 0;
  double echelle = 1;  // mm
  std::size_t debut = 0;
  {
    std::string tete;
    for (const auto& x : lignes.front()) tete += x + " ";
    for (char& ch : tete) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    const bool entete = std::none_of(lignes.front().size() > 1 ? lignes.front()[1].begin() : tete.end(), lignes.front().size() > 1 ? lignes.front()[1].end() : tete.end(),
                                     [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
    if (entete) {
      debut = 1;
      if (tete.find("mil") != std::string::npos) echelle = 0.0254;
      else if (tete.find("µm") != std::string::npos || tete.find("um") != std::string::npos) echelle = 0.001;
    }
  }
  auto& t = c.extensions["longueursBoitier"][reference];
  int n = 0;
  for (std::size_t i = debut; i < lignes.size(); ++i) {
    if (lignes[i].size() < 2 || lignes[i][0].empty()) continue;
    std::string v = lignes[i][1];
    std::replace(v.begin(), v.end(), ',', '.');
    t[lignes[i][0]] = std::atof(v.c_str()) * echelle;
    ++n;
  }
  return n;
}

double longueurSignal(const ConceptionElec& c, const Signal& s) {
  double l = 0;
  for (const auto& r : s.reseaux) l += longueurReseau(c, r);
  // Longueurs internes des boîtiers, aux broches qui ne sont pas celles d'un composant série du signal
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const std::set<std::string> nets(s.reseaux.begin(), s.reseaux.end());
  std::set<std::pair<std::string, std::string>> comptees;
  std::map<std::string, std::set<std::string>> netsDe;
  for (const auto& p : pp)
    if (nets.count(p.reseau)) netsDe[p.composant].insert(p.reseau);
  for (const auto& p : pp) {
    if (!nets.count(p.reseau) || netsDe[p.composant].size() >= 2) continue;  // composant série : traversé
    if (comptees.insert({p.reference, p.numero}).second) l += longueurBoitier(c, p.reference, p.numero);
  }
  return l;
}

double retardSignal(const ConceptionElec& c, const Signal& s) {
  double t = 0;
  for (const auto& r : s.reseaux) t += retardReseau(c, r);
  return t;
}

std::optional<double> longueurTroncon(const ConceptionElec& c, const std::string& de, const std::string& vers) {
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const auto [refA, numA] = broche(de);
  const auto [refB, numB] = broche(vers);
  const PastillePlacee *pa = nullptr, *pb = nullptr;
  for (const auto& p : pp) {
    if (p.reference == refA && p.numero == numA) pa = &p;
    if (p.reference == refB && p.numero == numB) pb = &p;
  }
  if (!pa || !pb || pa->reseau.empty() || k.canonique(pa->reseau) != k.canonique(pb->reseau)) return std::nullopt;
  const std::string reseau = k.canonique(pa->reseau);
  const int nc = c.circuit.empilage.couchesCuivre;
  struct Noeud {
    Pt p;
    Couches couches;
    const Polygone* forme = nullptr;  // pastille
    Nm rayon = 0;                     // via
  };
  std::vector<Noeud> N;
  std::vector<std::vector<std::pair<std::size_t, double>>> adj;
  auto noeud = [&](Noeud n) {
    N.push_back(std::move(n));
    adj.emplace_back();
    return N.size() - 1;
  };
  auto lier = [&](std::size_t a, std::size_t b, double d) {
    adj[a].push_back({b, d});
    adj[b].push_back({a, d});
  };
  const std::size_t ia = noeud({pa->pastille.position, pa->couches, &pa->forme, 0});
  const std::size_t ib = noeud({pb->pastille.position, pb->couches, &pb->forme, 0});
  for (const auto& p : pp)
    if (&p != pa && &p != pb && !p.reseau.empty() && k.canonique(p.reseau) == reseau) noeud({p.pastille.position, p.couches, &p.forme, 0});
  for (const auto& v : c.circuit.vias)
    if (k.canonique(v.reseau) == reseau) noeud({v.p, couchesVia(v, nc), nullptr, v.diametre / 2});
  const std::size_t fixes = N.size();
  for (const auto& t : c.circuit.pistes) {
    if (k.canonique(t.reseau) != reseau) continue;
    const std::size_t a = noeud({t.a, bit(t.couche), nullptr, t.largeur / 2}), b = noeud({t.b, bit(t.couche), nullptr, t.largeur / 2});
    lier(a, b, longueurPiste(t));
  }
  // Contacts : extrémités confondues, extrémité dans une pastille ou une via de la même couche
  for (std::size_t i = 0; i < N.size(); ++i)
    for (std::size_t j = i + 1; j < N.size(); ++j) {
      if (!(N[i].couches & N[j].couches)) continue;
      bool touche = distance(N[i].p, N[j].p) < 1'000;
      if (!touche && N[i].forme) touche = dedans(*N[i].forme, N[j].p);
      if (!touche && N[j].forme) touche = dedans(*N[j].forme, N[i].p);
      if (!touche && i < fixes && !N[i].forme && N[i].rayon > 0) touche = distance(N[i].p, N[j].p) <= static_cast<double>(N[i].rayon);
      if (!touche && j < fixes && !N[j].forme && N[j].rayon > 0) touche = distance(N[i].p, N[j].p) <= static_cast<double>(N[j].rayon);
      if (touche) lier(i, j, 0);
    }
  // Dijkstra
  std::vector<double> d(N.size(), std::numeric_limits<double>::infinity());
  using E = std::pair<double, std::size_t>;
  std::priority_queue<E, std::vector<E>, std::greater<>> q;
  d[ia] = 0;
  q.push({0, ia});
  while (!q.empty()) {
    const auto [dist, n] = q.top();
    q.pop();
    if (dist > d[n]) continue;
    for (const auto& [m, w] : adj[n])
      if (dist + w < d[m]) {
        d[m] = dist + w;
        q.push({d[m], m});
      }
  }
  if (!std::isfinite(d[ib])) return std::nullopt;
  return d[ib] + longueurBoitier(c, refA, numA) + longueurBoitier(c, refB, numB);
}

std::vector<Violation> verifierSignaux(const ConceptionElec& c) {
  std::vector<Violation> v;
  for (const auto& s : c.circuit.regles.avancees.signaux) {
    for (const auto& t : s.troncons) {
      if (t.longueurMax <= 0 && t.longueurMin <= 0) continue;
      const auto l = longueurTroncon(c, t.de, t.vers);
      if (!l) continue;
      const bool long_ = t.longueurMax > 0 && *l > static_cast<double>(t.longueurMax), court = t.longueurMin > 0 && *l < static_cast<double>(t.longueurMin);
      if (long_ || court)
        v.push_back({Gravite::Avertissement, "DRC-86",
                     "Tronçon " + t.de + " → " + t.vers + " du signal « " + s.nom + " » : " + mmTexte(*l * 1e-6) + " mm (" + (long_ ? "au plus " + mmTexte(mm(t.longueurMax)) : "au moins " + mmTexte(mm(t.longueurMin))) + " mm)",
                     long_ ? "Raccourcis le tracé de ce tronçon." : "Allonge ce tronçon par un motif d'accord.", {}, {}, {}});
    }
    // Chaîne ou fly-by : le cuivre passe par chaque broche dans l'ordre (la longueur de bout en bout vaut la somme
    // des tronçons successifs)
    if ((s.topologie == TopologieSignal::Chaine || s.topologie == TopologieSignal::FlyBy) && s.ordre.size() >= 3) {
      double somme = 0;
      bool complet = true;
      for (std::size_t i = 1; i < s.ordre.size() && complet; ++i) {
        const auto l = longueurTroncon(c, s.ordre[i - 1], s.ordre[i]);
        complet = l.has_value();
        if (l) somme += *l;
      }
      const auto total = complet ? longueurTroncon(c, s.ordre.front(), s.ordre.back()) : std::nullopt;
      if (total && std::abs(*total - somme) > std::max(10'000.0, 0.01 * somme))
        v.push_back({Gravite::Avertissement, "DRC-87", "Signal « " + s.nom + " » : le cuivre ne suit pas l'ordre de raccordement imposé (" + s.ordre.front() + " … " + s.ordre.back() + ")",
                     "Reprends le routage broche après broche, dans l'ordre que montre le chevelu.", {}, {}, {}});
    }
  }
  return v;
}

// ---------- Gabarits d'interface ----------
namespace {
const char* const GABARITS[] = {
    R"JSON({"nom":"USB 2.0","description":"Paire D+/D− de 90 Ω, accordée à 0,15 mm, non couplée au plus 3 mm",
"classes":[{"nom":"USB2","largeur":0.2,"isolement":0.2,"impedance":90,"differentiel":true,"ecart":0.15,"reseaux":["{P}D_P","{P}D_N"]}],
"paires":[{"nom":"{P}D","positif":"{P}D_P","negatif":"{P}D_N","classe":"USB2","impedance":90,"nonCoupleMax":3}],
"accords":[{"nom":"{P}D","reseaux":["{P}D_P","{P}D_N"],"tolerance":0.15}],
"contraintes":[{"nom":"USB 2.0 : longueur","genre":"longueur","portee":"reseau ~ '{P}D_?'","max":75}]})JSON",
    R"JSON({"nom":"USB 3.x","description":"Paires SuperSpeed TX et RX de 90 Ω, accordées à 0,1 mm",
"classes":[{"nom":"USB3","largeur":0.15,"isolement":0.2,"impedance":90,"differentiel":true,"ecart":0.15,"reseaux":["{P}SSTX_P","{P}SSTX_N","{P}SSRX_P","{P}SSRX_N"]}],
"paires":[{"nom":"{P}SSTX","positif":"{P}SSTX_P","negatif":"{P}SSTX_N","classe":"USB3","impedance":90,"nonCoupleMax":2},
          {"nom":"{P}SSRX","positif":"{P}SSRX_P","negatif":"{P}SSRX_N","classe":"USB3","impedance":90,"nonCoupleMax":2}],
"accords":[{"nom":"{P}SSTX","reseaux":["{P}SSTX_P","{P}SSTX_N"],"tolerance":0.1},{"nom":"{P}SSRX","reseaux":["{P}SSRX_P","{P}SSRX_N"],"tolerance":0.1}]})JSON",
    R"JSON({"nom":"HDMI","description":"Trois paires de données et l'horloge TMDS de 100 Ω, accordées entre elles à 2,5 mm",
"classes":[{"nom":"TMDS","largeur":0.15,"isolement":0.25,"impedance":100,"differentiel":true,"ecart":0.15,"reseaux":["{P}D{i}_P","{P}D{i}_N","{P}CK_P","{P}CK_N"],"repeter":[0,2]}],
"paires":[{"nom":"{P}D{i}","positif":"{P}D{i}_P","negatif":"{P}D{i}_N","classe":"TMDS","impedance":100,"nonCoupleMax":2,"repeter":[0,2]},
          {"nom":"{P}CK","positif":"{P}CK_P","negatif":"{P}CK_N","classe":"TMDS","impedance":100,"nonCoupleMax":2}],
"accords":[{"nom":"{P}TMDS","reseaux":["{P}D{i}_P","{P}D{i}_N","{P}CK_P","{P}CK_N"],"tolerance":2.5,"repeter":[0,2]}]})JSON",
    R"JSON({"nom":"PCIe","description":"Paires TX et RX de 85 Ω par voie, horloge de référence ; accord intra-paire à 0,13 mm",
"classes":[{"nom":"PCIE","largeur":0.13,"isolement":0.2,"impedance":85,"differentiel":true,"ecart":0.13,"reseaux":["{P}TX{i}_P","{P}TX{i}_N","{P}RX{i}_P","{P}RX{i}_N"],"repeter":[0,0]}],
"paires":[{"nom":"{P}TX{i}","positif":"{P}TX{i}_P","negatif":"{P}TX{i}_N","classe":"PCIE","impedance":85,"repeter":[0,0]},
          {"nom":"{P}RX{i}","positif":"{P}RX{i}_P","negatif":"{P}RX{i}_N","classe":"PCIE","impedance":85,"repeter":[0,0]},
          {"nom":"{P}REFCLK","positif":"{P}REFCLK_P","negatif":"{P}REFCLK_N","classe":"PCIE","impedance":85}],
"accords":[{"nom":"{P}TX{i}","reseaux":["{P}TX{i}_P","{P}TX{i}_N"],"tolerance":0.13,"repeter":[0,0]},{"nom":"{P}RX{i}","reseaux":["{P}RX{i}_P","{P}RX{i}_N"],"tolerance":0.13,"repeter":[0,0]}]})JSON",
    R"JSON({"nom":"MIPI CSI-2","description":"Paires de données et d'horloge D-PHY de 100 Ω, accordées à 0,5 mm",
"classes":[{"nom":"MIPI","largeur":0.12,"isolement":0.2,"impedance":100,"differentiel":true,"ecart":0.12,"reseaux":["{P}D{i}_P","{P}D{i}_N","{P}CLK_P","{P}CLK_N"],"repeter":[0,1]}],
"paires":[{"nom":"{P}D{i}","positif":"{P}D{i}_P","negatif":"{P}D{i}_N","classe":"MIPI","impedance":100,"repeter":[0,1]},{"nom":"{P}CLK","positif":"{P}CLK_P","negatif":"{P}CLK_N","classe":"MIPI","impedance":100}],
"accords":[{"nom":"{P}CSI","reseaux":["{P}D{i}_P","{P}D{i}_N","{P}CLK_P","{P}CLK_N"],"tolerance":0.5,"repeter":[0,1]}]})JSON",
    R"JSON({"nom":"Ethernet 100BASE-TX","description":"Paires TX et RX de 100 Ω entre le PHY et le transformateur",
"classes":[{"nom":"ETH","largeur":0.2,"isolement":0.25,"impedance":100,"differentiel":true,"ecart":0.2,"reseaux":["{P}TX_P","{P}TX_N","{P}RX_P","{P}RX_N"]}],
"paires":[{"nom":"{P}TX","positif":"{P}TX_P","negatif":"{P}TX_N","classe":"ETH","impedance":100},{"nom":"{P}RX","positif":"{P}RX_P","negatif":"{P}RX_N","classe":"ETH","impedance":100}],
"accords":[{"nom":"{P}TX","reseaux":["{P}TX_P","{P}TX_N"],"tolerance":0.5},{"nom":"{P}RX","reseaux":["{P}RX_P","{P}RX_N"],"tolerance":0.5}]})JSON",
    R"JSON({"nom":"DDR3","description":"Octet de données (DQ0 à DQ7, DM, DQS) accordé sur DQS ; adresses et commandes en fly-by sur l'horloge",
"classes":[{"nom":"DDR_DQ","largeur":0.12,"isolement":0.15,"impedance":50,"reseaux":["{P}DQ{i}"],"repeter":[0,7]},
           {"nom":"DDR_CK","largeur":0.12,"isolement":0.2,"impedance":100,"differentiel":true,"ecart":0.12,"reseaux":["{P}CK_P","{P}CK_N","{P}DQS_P","{P}DQS_N"]}],
"paires":[{"nom":"{P}CK","positif":"{P}CK_P","negatif":"{P}CK_N","classe":"DDR_CK","impedance":100},{"nom":"{P}DQS","positif":"{P}DQS_P","negatif":"{P}DQS_N","classe":"DDR_CK","impedance":100}],
"accords":[{"nom":"{P}OCTET0","reseaux":["{P}DQ{i}","{P}DM","{P}DQS_P","{P}DQS_N"],"tolerance":0.25,"repeter":[0,7]}],
"signaux":[{"nom":"{P}A{i}","reseaux":["{P}A{i}"],"topologie":"flyby","repeter":[0,14]}]})JSON",
    R"JSON({"nom":"DDR4","description":"Octet de données (DQ0 à DQ7, DM/DBI, DQS) accordé sur DQS à 0,2 mm ; adresses en fly-by",
"classes":[{"nom":"DDR4_DQ","largeur":0.1,"isolement":0.15,"impedance":40,"reseaux":["{P}DQ{i}"],"repeter":[0,7]},
           {"nom":"DDR4_CK","largeur":0.1,"isolement":0.2,"impedance":80,"differentiel":true,"ecart":0.12,"reseaux":["{P}CK_P","{P}CK_N","{P}DQS_P","{P}DQS_N"]}],
"paires":[{"nom":"{P}CK","positif":"{P}CK_P","negatif":"{P}CK_N","classe":"DDR4_CK","impedance":80},{"nom":"{P}DQS","positif":"{P}DQS_P","negatif":"{P}DQS_N","classe":"DDR4_CK","impedance":80}],
"accords":[{"nom":"{P}OCTET0","reseaux":["{P}DQ{i}","{P}DM_DBI","{P}DQS_P","{P}DQS_N"],"tolerance":0.2,"repeter":[0,7]}],
"signaux":[{"nom":"{P}A{i}","reseaux":["{P}A{i}"],"topologie":"flyby","repeter":[0,13]}]})JSON"};

// Développe « {i} » d'un objet qui porte « repeter » : [de, a]. Dans une liste de noms, chaque nom à « {i} » se
// répète, les autres restent une fois ; un objet sans « {i} » dans son nom se garde en un seul exemplaire.
std::vector<nlohmann::json> developper(const nlohmann::json& o, const std::string& prefixe) {
  auto subst = [&](const std::string& s, int i) { return remplacer(remplacer(s, "{P}", prefixe), "{i}", std::to_string(i)); };
  std::function<nlohmann::json(const nlohmann::json&, int)> appliquer = [&](const nlohmann::json& x, int i) -> nlohmann::json {
    if (x.is_string()) return subst(x.get<std::string>(), i);
    if (x.is_array()) {
      nlohmann::json r = nlohmann::json::array();
      for (const auto& e : x) r.push_back(appliquer(e, i));
      return r;
    }
    if (x.is_object()) {
      nlohmann::json r = nlohmann::json::object();
      for (auto it = x.begin(); it != x.end(); ++it)
        if (it.key() != "repeter") r[it.key()] = appliquer(it.value(), i);
      return r;
    }
    return x;
  };
  if (!o.contains("repeter") || !o["repeter"].is_array() || o["repeter"].size() != 2) return {appliquer(o, 0)};
  const int de = o["repeter"][0].get<int>(), a = o["repeter"][1].get<int>();
  const std::string nom = o.value("nom", std::string());
  if (nom.find("{i}") != std::string::npos) {
    std::vector<nlohmann::json> r;
    for (int i = de; i <= a; ++i) r.push_back(appliquer(o, i));
    return r;
  }
  // Un seul objet : ses listes de réseaux développées
  nlohmann::json r = appliquer(o, de);
  for (const char* cle : {"reseaux"})
    if (o.contains(cle) && o[cle].is_array()) {
      nlohmann::json liste = nlohmann::json::array();
      std::set<std::string> vus;
      for (const auto& e : o[cle]) {
        const std::string s = e.get<std::string>();
        for (int i = de; i <= (s.find("{i}") != std::string::npos ? a : de); ++i)
          if (vus.insert(subst(s, i)).second) liste.push_back(subst(s, i));
      }
      r[cle] = liste;
    }
  return {r};
}
}  // namespace

const std::vector<GabaritInterface>& gabaritsInterface() {
  static const std::vector<GabaritInterface> v = [] {
    std::vector<GabaritInterface> r;
    for (const char* g : GABARITS)
      if (auto x = lireGabaritInterface(g)) r.push_back(std::move(*x));
    return r;
  }();
  return v;
}

std::optional<GabaritInterface> lireGabaritInterface(const std::string& json, std::string* erreur) {
  try {
    const nlohmann::json o = nlohmann::json::parse(json);
    if (!o.is_object() || !o.contains("nom")) {
      if (erreur) *erreur = "gabarit sans nom";
      return std::nullopt;
    }
    return GabaritInterface{o["nom"].get<std::string>(), o.value("description", std::string()), o};
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
}

BilanGabarit appliquerGabarit(ConceptionElec& c, const GabaritInterface& g, const std::string& prefixe) {
  exigerModifiable(c);
  BilanGabarit b;
  const Connectivite k = connectivite(c);
  std::set<std::string> connus;
  for (const auto& r : k.reseaux) connus.insert(r.nom);
  for (const auto& p : pastilles(c, k))
    if (!p.reseau.empty()) connus.insert(p.reseau);
  auto verifier = [&](const std::string& r) {
    if (!connus.count(r)) b.journal.push_back("réseau « " + r + " » absent de la conception : la règle l'attend");
  };
  Regles& R = c.circuit.regles;
  auto& A = R.avancees;
  const nlohmann::json& d = g.donnees;
  if (d.contains("classes"))
    for (const auto& o0 : d["classes"])
      for (const auto& o : developper(o0, prefixe)) {
        ClasseReseau kl;
        kl.nom = o.value("nom", std::string("Classe"));
        kl.largeur = nm(mmJ(o, "largeur", 0.2));
        kl.isolement = nm(mmJ(o, "isolement", 0.2));
        auto it = std::find_if(R.classes.begin(), R.classes.end(), [&](const ClasseReseau& x) { return x.nom == kl.nom; });
        if (it == R.classes.end()) {
          R.classes.push_back(kl);
          ++b.classes;
        }
        if (o.contains("reseaux"))
          for (const auto& r : o["reseaux"]) {
            R.classeReseau[r.get<std::string>()] = kl.nom;
            verifier(r.get<std::string>());
          }
        // Profil d'impédance de la classe, calculé sur l'empilage (8.21)
        if (mmJ(o, "impedance") > 0) {
          ProfilImpedance p;
          p.nom = kl.nom;
          p.impedance = mmJ(o, "impedance");
          p.differentiel = o.value("differentiel", false);
          calculerProfil(c, p, nm(mmJ(o, "ecart", 0.15)));
          std::erase_if(A.impedances, [&](const ProfilImpedance& x) { return x.nom == p.nom; });
          A.impedances.push_back(std::move(p));
        }
      }
  if (d.contains("paires"))
    for (const auto& o0 : d["paires"])
      for (const auto& o : developper(o0, prefixe)) {
        PaireDifferentielle p;
        p.nom = o.value("nom", std::string());
        p.positif = o.value("positif", std::string());
        p.negatif = o.value("negatif", std::string());
        p.classe = o.value("classe", std::string());
        p.impedance = mmJ(o, "impedance");
        p.nonCoupleMax = nm(mmJ(o, "nonCoupleMax"));
        // Largeur et écart du profil de la classe, sur la couche du dessus
        for (const auto& pr : A.impedances)
          if (pr.nom == p.classe)
            if (const auto it = pr.parCouche.find(codeCouche(Couche::CuivreDessus)); it != pr.parCouche.end()) {
              p.largeur = it->second.first;
              p.ecart = it->second.second;
            }
        verifier(p.positif);
        verifier(p.negatif);
        std::erase_if(A.paires, [&](const PaireDifferentielle& x) { return x.nom == p.nom; });
        A.paires.push_back(std::move(p));
        ++b.paires;
      }
  if (d.contains("accords"))
    for (const auto& o0 : d["accords"])
      for (const auto& o : developper(o0, prefixe)) {
        GroupeAccord a;
        a.nom = o.value("nom", std::string());
        if (o.contains("reseaux"))
          for (const auto& r : o["reseaux"]) a.reseaux.push_back(r.get<std::string>());
        a.tolerance = nm(mmJ(o, "tolerance", 0.25));
        std::erase_if(A.accords, [&](const GroupeAccord& x) { return x.nom == a.nom; });
        A.accords.push_back(std::move(a));
        ++b.accords;
      }
  if (d.contains("signaux"))
    for (const auto& o0 : d["signaux"])
      for (const auto& o : developper(o0, prefixe)) {
        Signal s;
        s.nom = o.value("nom", std::string());
        if (o.contains("reseaux"))
          for (const auto& r : o["reseaux"]) s.reseaux.push_back(r.get<std::string>());
        const std::string t = o.value("topologie", std::string());
        s.topologie = t == "chaine" ? TopologieSignal::Chaine : t == "flyby" ? TopologieSignal::FlyBy : t == "etoile" ? TopologieSignal::Etoile : t == "t" ? TopologieSignal::T : TopologieSignal::Libre;
        std::erase_if(A.signaux, [&](const Signal& x) { return x.nom == s.nom; });
        A.signaux.push_back(std::move(s));
        ++b.signaux;
      }
  if (d.contains("contraintes"))
    for (const auto& o0 : d["contraintes"])
      for (const auto& o : developper(o0, prefixe)) {
        Contrainte k2;
        k2.nom = o.value("nom", std::string());
        if (!genreContrainteDepuisCode(o.value("genre", std::string()), k2.genre)) continue;
        k2.portee = o.value("portee", std::string());
        k2.valeur.min = nm(mmJ(o, "min"));
        k2.valeur.pref = nm(mmJ(o, "pref"));
        k2.valeur.max = nm(mmJ(o, "max"));
        k2.source = "gabarit « " + g.nom + " »";
        std::erase_if(A.contraintes, [&](const Contrainte& x) { return x.nom == k2.nom && !k2.nom.empty(); });
        A.contraintes.push_back(std::move(k2));
        ++b.contraintes;
      }
  b.journal.insert(b.journal.begin(), "Gabarit « " + g.nom + " » : " + std::to_string(b.classes) + " classe(s), " + std::to_string(b.paires) + " paire(s), " + std::to_string(b.accords) + " groupe(s) d'accord, " +
                                          std::to_string(b.signaux) + " signal(aux), " + std::to_string(b.contraintes) + " règle(s)");
  return b;
}

std::string xlsxSignaux(const ConceptionElec& c) {
  prom::TableauNomenclature t;
  t.titre = "Signaux de " + c.nom;
  for (const auto& [cle, titre, num] : std::vector<std::tuple<const char*, const char*, bool>>{
           {"nom", "Nom", false}, {"genre", "Genre", false}, {"reseaux", "Réseaux", false}, {"topologie", "Topologie", false}, {"longueur", "Longueur (mm)", true}, {"retard", "Retard (ps)", true}, {"borne", "Bornes", false}})
    t.colonnes.push_back({cle, titre, num});
  auto texte = [](std::string s) { return prom::CelluleNomenclature{std::move(s), 0, false}; };
  auto nombre = [](double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    std::string s = b;
    std::replace(s.begin(), s.end(), '.', ',');
    return prom::CelluleNomenclature{s, v, true};
  };
  auto joindre = [](const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
  };
  const auto& A = c.circuit.regles.avancees;
  static const char* const TOPO[] = {"libre", "chaîne", "fly-by", "étoile", "T"};
  for (const auto& s : A.signaux)
    t.lignes.push_back({texte(s.nom), texte("signal"), texte(joindre(s.reseaux)), texte(TOPO[static_cast<int>(s.topologie)]), nombre(longueurSignal(c, s) * 1e-6), nombre(retardSignal(c, s)), texte({})});
  for (const auto& p : A.paires) {
    const double lp = longueurReseau(c, p.positif) * 1e-6, ln = longueurReseau(c, p.negatif) * 1e-6;
    t.lignes.push_back({texte(p.nom), texte("paire"), texte(p.positif + ", " + p.negatif), texte({}), nombre(std::max(lp, ln)), nombre(std::max(retardReseau(c, p.positif), retardReseau(c, p.negatif))),
                        texte(p.impedance > 0 ? mmTexte(p.impedance) + " Ω, écart " + mmTexte(mm(p.ecart)) + " mm" : std::string())});
  }
  for (const auto& g : A.accords) {
    double lmax = 0;
    for (const auto& r : g.reseaux) lmax = std::max(lmax, longueurReseau(c, r) * 1e-6);
    t.lignes.push_back({texte(g.nom), texte("groupe d'accord"), texte(joindre(g.reseaux)), texte({}), nombre(lmax), nombre(0), texte("± " + mmTexte(mm(g.tolerance)) + " mm")});
  }
  t.niveaux.assign(t.lignes.size(), 0);
  t.assemblages.assign(t.lignes.size(), 0);
  return prom::xlsxNomenclature(t);
}

}  // namespace prom::ecad
