// Prométhée : routes des câbles et circuits continus du faisceau (lot L19 de la 0.34 ; 19.12). Plus court chemin
// sur les feuilles de topologie, route 3D par points d'attache raccordée au rayon de courbure, analyse nodale des
// circuits continus sur les longueurs des fils. Voir cablage.hpp. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <queue>

#include "electrique_interne.hpp"
#include "promethee/cablage.hpp"

namespace prom::elec {

namespace {
constexpr double PI = 3.14159265358979323846;
using V3 = std::array<double, 3>;
V3 moins(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
V3 plus(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
V3 fois(const V3& a, double k) { return {a[0] * k, a[1] * k, a[2] * k}; }
double scal(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double norme(const V3& a) { return std::sqrt(scal(a, a)); }
V3 unitaire(const V3& a) {
  const double n = norme(a);
  return n > 0 ? fois(a, 1 / n) : V3{0, 0, 0};
}
}  // namespace

std::optional<Topologie> lireTopologie(const std::string& json, std::string* erreur) {
  try {
    const auto j = nlohmann::ordered_json::parse(json);
    Topologie t;
    for (const auto& e : j.value("espaces", nlohmann::ordered_json::array()))
      if (e.is_string()) t.espaces.push_back(e.get<std::string>());
    for (const auto& p : j.value("points", nlohmann::ordered_json::array())) t.points.push_back({p.at("id").get<std::string>(), p.value("espace", std::string()), p.value("x", 0.0), p.value("y", 0.0)});
    for (const auto& p : j.value("passages", nlohmann::ordered_json::array())) t.passages.push_back({p.at("a").get<std::string>(), p.at("b").get<std::string>(), p.value("longueur", 0.0), p.value("ouvert", true)});
    return t;
  } catch (const std::exception& e) {
    if (erreur) *erreur = e.what();
    return std::nullopt;
  }
}

std::string texteTopologie(const Topologie& t) {
  nlohmann::ordered_json j = {{"format", "promethee-topologie"}, {"version", 1}, {"espaces", t.espaces}, {"points", nlohmann::ordered_json::array()}, {"passages", nlohmann::ordered_json::array()}};
  for (const auto& p : t.points) j["points"].push_back({{"id", p.id}, {"espace", p.espace}, {"x", p.x}, {"y", p.y}});
  for (const auto& p : t.passages) j["passages"].push_back({{"a", p.a}, {"b", p.b}, {"longueur", p.longueur}, {"ouvert", p.ouvert}});
  return j.dump(1) + "\n";
}

RouteTopologie routerTopologie(const Topologie& t, const std::string& de, const std::string& vers, const std::vector<std::string>& imposes) {
  RouteTopologie r;
  std::map<std::string, const PointTopologie*> points;
  for (const auto& p : t.points) points[p.id] = &p;
  std::map<std::string, std::vector<std::pair<std::string, double>>> voisins;
  for (const auto& p : t.passages) {
    if (!p.ouvert || !points.count(p.a) || !points.count(p.b)) continue;
    const double l = p.longueur > 0 ? p.longueur : std::hypot(points[p.a]->x - points[p.b]->x, points[p.a]->y - points[p.b]->y);
    voisins[p.a].push_back({p.b, l});
    voisins[p.b].push_back({p.a, l});
  }
  auto troncon = [&](const std::string& a, const std::string& b, std::vector<std::string>& chemin) -> double {
    std::map<std::string, double> d;
    std::map<std::string, std::string> avant;
    using E = std::pair<double, std::string>;
    std::priority_queue<E, std::vector<E>, std::greater<E>> file;
    d[a] = 0;
    file.push({0, a});
    while (!file.empty()) {
      const auto [dist, n] = file.top();
      file.pop();
      if (dist > d[n]) continue;
      if (n == b) break;
      for (const auto& [m, l] : voisins[n])
        if (!d.count(m) || dist + l < d[m] - 1e-12) {
          d[m] = dist + l;
          avant[m] = n;
          file.push({d[m], m});
        }
    }
    if (!d.count(b)) return -1;
    std::vector<std::string> c;
    for (std::string n = b; n != a; n = avant[n]) c.insert(c.begin(), n);
    if (chemin.empty()) chemin.push_back(a);
    chemin.insert(chemin.end(), c.begin(), c.end());
    return d[b];
  };
  std::vector<std::string> etapes = {de};
  etapes.insert(etapes.end(), imposes.begin(), imposes.end());
  etapes.push_back(vers);
  for (std::size_t i = 1; i < etapes.size(); ++i) {
    const double l = troncon(etapes[i - 1], etapes[i], r.points);
    if (l < 0) {
      r.points.clear();
      r.longueur = 0;
      return r;
    }
    r.longueur += l;
  }
  r.trouvee = true;
  return r;
}

bool longueurDepuisTopologie(ProjetElectrique& p, const std::string& cable, const RouteTopologie& r, double surlongueur) {
  if (!r.trouvee) return false;
  const double m = (r.longueur + 2 * surlongueur) / 1000;
  bool fait = false;
  for (auto& c : p.cables)
    if (c.reference == cable || c.uuid == cable) {
      c.longueur = m;
      fait = true;
    }
  std::vector<Cablage> v = cablages(p);
  bool touche = false;
  for (auto& h : v)
    for (auto& w : h.cables)
      if (w.reference == cable || w.uuid == cable) {
        w.longueur = m;
        w.provenance = ProvenanceLongueur::Topologie;
        for (auto& f : h.fils)
          if (f.cable == w.uuid && f.longueurForcee <= 0) {
            f.longueur = m;
            f.provenance = ProvenanceLongueur::Topologie;
          }
        touche = true;
      }
  if (touche) ecrireCablages(p, v);
  return fait || touche;
}

Route3D routerEn3D(const std::vector<std::array<double, 3>>& attaches, double rayon) {
  Route3D r;
  if (attaches.size() < 2) return r;
  const std::size_t n = attaches.size();
  std::vector<double> longueurs(n - 1), tangentes(n, 0), rayons(n, rayon), angles(n, 0);
  for (std::size_t i = 0; i + 1 < n; ++i) longueurs[i] = norme(moins(attaches[i + 1], attaches[i]));
  // Coudes : angle de déviation et longueur de tangente du raccord
  for (std::size_t i = 1; i + 1 < n; ++i) {
    const V3 u = unitaire(moins(attaches[i], attaches[i - 1])), w = unitaire(moins(attaches[i + 1], attaches[i]));
    const double phi = std::acos(std::clamp(scal(u, w), -1.0, 1.0));
    angles[i] = phi;
    if (phi < 1e-9 || rayon <= 0) continue;
    const double t = rayon * std::tan(phi / 2);
    // Place disponible : la moitié de chaque segment partagé avec un autre coude, tout le segment sinon
    const double avant = i == 1 ? longueurs[i - 1] : longueurs[i - 1] / 2, apres = i + 2 == n ? longueurs[i] : longueurs[i] / 2;
    const double dispo = std::min(avant, apres);
    if (t > dispo + 1e-9) {
      rayons[i] = dispo / std::tan(phi / 2);
      r.alertes.push_back("coude au point " + std::to_string(i + 1) + " trop serré pour le rayon minimal de " + detail::nombreFr(rayon) + " mm (rayon possible : " + detail::nombreFr(std::floor(rayons[i] * 10) / 10) +
                          " mm)");
    }
    tangentes[i] = rayons[i] * std::tan(phi / 2);
  }
  double total = 0;
  for (const double l : longueurs) total += l;
  for (std::size_t i = 1; i + 1 < n; ++i) total += -2 * tangentes[i] + rayons[i] * angles[i];
  r.longueur = total;
  // Courbe échantillonnée : droites et arcs
  r.points.push_back(attaches.front());
  for (std::size_t i = 1; i + 1 < n; ++i) {
    const V3 u = unitaire(moins(attaches[i], attaches[i - 1])), w = unitaire(moins(attaches[i + 1], attaches[i]));
    if (angles[i] < 1e-9 || tangentes[i] <= 0) {
      r.points.push_back(attaches[i]);
      continue;
    }
    const V3 a = moins(attaches[i], fois(u, tangentes[i]));
    const V3 normal = unitaire(moins(w, fois(u, scal(u, w))));
    const V3 centre = plus(a, fois(normal, rayons[i]));
    const int pas = std::max(2, static_cast<int>(std::ceil(angles[i] / (PI / 18))));
    for (int k = 0; k <= pas; ++k) {
      const double th = angles[i] * k / pas;
      r.points.push_back(plus(centre, plus(fois(normal, -rayons[i] * std::cos(th)), fois(u, rayons[i] * std::sin(th)))));
    }
  }
  r.points.push_back(attaches.back());
  return r;
}

int longueurDepuis3D(Cablage& h, const std::string& cable, double longueurMm) {
  int n = 0;
  for (auto& w : h.cables)
    if (w.reference == cable || w.uuid == cable) {
      w.longueur = longueurMm / 1000;
      w.provenance = ProvenanceLongueur::Trace3D;
      for (auto& f : h.fils)
        if (f.cable == w.uuid && f.longueurForcee <= 0) {
          f.longueur = longueurMm / 1000;
          f.provenance = ProvenanceLongueur::Trace3D;
          ++n;
        }
    }
  // Un fil seul peut aussi porter la route
  if (n == 0)
    if (FilCablage* f = h.fil(cable)) {
      f->longueur = longueurMm / 1000;
      f->provenance = ProvenanceLongueur::Trace3D;
      n = 1;
    }
  return n;
}

std::vector<double> analyseNodale(int noeuds, const std::vector<ElementNodal>& elements, int noeudSource, double tensionSource) {
  // Analyse nodale modifiée : n tensions de nœud et le courant de la source de tension
  const int n = noeuds + (noeudSource > 0 ? 1 : 0);
  if (noeuds <= 0) return {};
  std::vector<std::vector<double>> A(static_cast<std::size_t>(n), std::vector<double>(static_cast<std::size_t>(n) + 1, 0.0));
  auto G = [&](int i, int j) -> double& { return A[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(j - 1)]; };
  auto J = [&](int i) -> double& { return A[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(n)]; };
  for (const auto& e : elements) {
    if (e.resistance > 0) {
      const double g = 1 / e.resistance;
      if (e.a > 0) G(e.a, e.a) += g;
      if (e.b > 0) G(e.b, e.b) += g;
      if (e.a > 0 && e.b > 0) G(e.a, e.b) -= g, G(e.b, e.a) -= g;
    } else {
      if (e.a > 0) J(e.a) -= e.courant;
      if (e.b > 0) J(e.b) += e.courant;
    }
  }
  if (noeudSource > 0) {
    const int k = noeuds + 1;
    G(noeudSource, k) += 1;
    G(k, noeudSource) += 1;
    J(k) = tensionSource;
  }
  // Élimination de Gauss avec pivot partiel
  for (int c = 0; c < n; ++c) {
    int p = c;
    for (int r = c + 1; r < n; ++r)
      if (std::abs(A[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)]) > std::abs(A[static_cast<std::size_t>(p)][static_cast<std::size_t>(c)])) p = r;
    if (std::abs(A[static_cast<std::size_t>(p)][static_cast<std::size_t>(c)]) < 1e-15) return {};
    std::swap(A[static_cast<std::size_t>(p)], A[static_cast<std::size_t>(c)]);
    for (int r = 0; r < n; ++r) {
      if (r == c) continue;
      const double f = A[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)] / A[static_cast<std::size_t>(c)][static_cast<std::size_t>(c)];
      if (f == 0) continue;
      for (int k = c; k <= n; ++k) A[static_cast<std::size_t>(r)][static_cast<std::size_t>(k)] -= f * A[static_cast<std::size_t>(c)][static_cast<std::size_t>(k)];
    }
  }
  std::vector<double> v(static_cast<std::size_t>(noeuds) + 1, 0.0);
  for (int i = 1; i <= noeuds; ++i) v[static_cast<std::size_t>(i)] = A[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(n)] / A[static_cast<std::size_t>(i - 1)][static_cast<std::size_t>(i - 1)];
  return v;
}

namespace {
const EntreeCable* entreeDuFil(const Cablage& h, const BibliothequeCablage& b, const FilCablage& f) {
  for (const auto& w : h.cables)
    if (w.uuid == f.cable) return b.cable(w.designation);
  return nullptr;
}
double resistanceFil(const Cablage& h, const BibliothequeCablage& b, const FilCablage& f) {
  const EntreeCable* e = entreeDuFil(h, b, f);
  const double rl = e ? resistanceLineique(*e) : resistanceCuivre(f.section);  // mΩ/m
  return rl * f.longueur / 1000;                                               // Ω
}
double admissibleFil(const Cablage& h, const BibliothequeCablage& b, const FilCablage& f) {
  const EntreeCable* e = entreeDuFil(h, b, f);
  return e ? courantAdmissibleCable(*e) : courantAdmissibleFil(f.section);
}
}  // namespace

BilanContinu resoudreContinu(const Cablage& h, const BibliothequeCablage& b, const CircuitContinu& c) {
  BilanContinu r;
  double raller = 0, rretour = 0, admissible = 1e9;
  std::vector<const FilCablage*> fils;
  for (const auto& n : c.aller)
    if (const FilCablage* f = h.fil(n)) raller += resistanceFil(h, b, *f), fils.push_back(f), admissible = std::min(admissible, admissibleFil(h, b, *f));
  for (const auto& n : c.retour)
    if (const FilCablage* f = h.fil(n)) rretour += resistanceFil(h, b, *f), fils.push_back(f), admissible = std::min(admissible, admissibleFil(h, b, *f));
  // Nœuds : 1 source, 2 entrée de la charge, 3 sortie de la charge ; 0 : retour de la source
  const double ra = std::max(raller, 1e-9), rr = std::max(rretour, 1e-9);
  auto resoudre = [&](const ChargeContinue& k) {
    std::vector<ElementNodal> e = {{1, 2, ra, 0}, {3, 0, rr, 0}};
    if (k.resistance > 0) e.push_back({2, 3, k.resistance, 0});
    else e.push_back({2, 3, 0, k.courant});
    return analyseNodale(3, e, 1, c.tension);
  };
  ChargeContinue k = c.charge;
  std::vector<double> v;
  if (k.puissance > 0) {
    // Puissance constante : courant recalculé sur la tension de la charge jusqu'à stabilité
    double i = k.puissance / c.tension;
    for (r.iterations = 1; r.iterations <= 100; ++r.iterations) {
      k.courant = i;
      k.resistance = 0;
      v = resoudre(k);
      if (v.empty()) break;
      const double u = std::max(1e-6, v[2] - v[3]);
      const double suivant = k.puissance / u;
      if (std::abs(suivant - i) < 1e-9 * std::max(1.0, i)) {
        i = suivant;
        break;
      }
      i = suivant;
      if (i > 1e6) break;  // la charge ne peut être tenue (tension effondrée)
    }
    k.courant = i;
    v = resoudre(k);
  } else {
    r.iterations = 1;
    v = resoudre(k);
  }
  if (v.empty()) {
    r.alertes.push_back("circuit insoluble");
    return r;
  }
  r.tensionCharge = v[2] - v[3];
  r.courant = k.resistance > 0 ? r.tensionCharge / k.resistance : k.courant;
  r.chute = c.tension - r.tensionCharge;
  r.pourcent = c.tension > 0 ? 100 * r.chute / c.tension : 0;
  r.depasse = r.pourcent > c.chuteMax + 1e-9;
  if (r.depasse)
    r.alertes.push_back("chute de tension de " + detail::nombreFr(std::round(r.chute * 100) / 100) + " V (" + detail::nombreFr(std::round(r.pourcent * 10) / 10) + " %) au-delà de " +
                        detail::nombreFr(c.chuteMax) + " %");
  for (const FilCablage* f : fils)
    if (r.courant > admissibleFil(h, b, *f) + 1e-9)
      r.alertes.push_back("fil " + f->numero + " : " + detail::nombreFr(std::round(r.courant * 100) / 100) + " A au-delà de son courant admissible (" + detail::nombreFr(admissibleFil(h, b, *f)) + " A)");
  if (c.fusible > 0) {
    if (!fils.empty() && c.fusible > admissible + 1e-9)
      r.alertes.push_back("fusible de " + detail::nombreFr(c.fusible) + " A trop fort pour le fil le plus fin (" + detail::nombreFr(admissible) + " A admissibles)");
    if (r.courant > c.fusible + 1e-9) r.alertes.push_back("le courant de la charge (" + detail::nombreFr(std::round(r.courant * 100) / 100) + " A) dépasse le calibre du fusible");
  }
  return r;
}

}  // namespace prom::elec
