// Prométhée : outils du cuivre (lot L5, 0.34) — paires à écart constant, motifs d'accord, gouttes, opérations sur les
// zones, couture, découplages et placement poussé, codes 2D, codes-barres et logos, gabarits de carte.
// Licence GPL-3.0-only.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <optional>
#include <set>
#include <tuple>
#include <map>
#include <numbers>
#include <sstream>

#include "promethee/lignes_transmission.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/hachage.hpp"
#include "promethee/revue_conception.hpp"
#include "promethee/routeur.hpp"
#include "promethee/valeurs.hpp"

namespace prom::ecad {

namespace {
Pt arrondi(double x, double y) { return {static_cast<Nm>(std::llround(x)), static_cast<Nm>(std::llround(y))}; }
double aireSignee(const Polygone& p) {
  double s = 0;
  for (std::size_t i = 0, n = p.size(); i < n; ++i) {
    const Pt a = p[i], b = p[(i + 1) % n];
    s += static_cast<double>(a.x) * static_cast<double>(b.y) - static_cast<double>(b.x) * static_cast<double>(a.y);
  }
  return s / 2;
}
bool estExterieur(const Polygone& p) { return aireSignee(p) * aireSignee(orienter(p)) > 0; }
// Pièces d'un résultat de découpe : chaque contour extérieur, ses trous ramenés en un seul contour par des coupures
std::vector<Polygone> pieces(const Polygones& r) {
  std::vector<Polygone> ext, trous;
  for (const auto& p : r) (estExterieur(p) ? ext : trous).push_back(p);
  std::vector<Polygone> out;
  for (const auto& e : ext) {
    Polygones siens;
    for (const auto& t : trous)
      if (!t.empty() && dedans(e, t.front())) siens.push_back(t);
    out.push_back(siens.empty() ? e : regionAvecCoupures(e, siens));
  }
  std::sort(out.begin(), out.end(), [](const Polygone& a, const Polygone& b) { return aireMm2(a) > aireMm2(b); });
  return out;
}
Polygone contourDeLaCarte(const ConceptionElec& c) { return c.circuit.contour.libre.size() >= 3 ? c.circuit.contour.libre : c.circuit.contour.polygone(); }
Zone* zoneParId(ConceptionElec& c, const std::string& id) {
  const auto it = std::find_if(c.circuit.zones.begin(), c.circuit.zones.end(), [&](const Zone& z) { return z.uuid == id; });
  return it == c.circuit.zones.end() ? nullptr : &*it;
}
std::string prefixeDe(const std::string& ref) {
  std::string p;
  for (char ch : ref) {
    if (std::isdigit(static_cast<unsigned char>(ch)) || ch == '?') break;
    p += ch;
  }
  return p;
}
// Via posée en p libre sur tous les cuivres de l'empilage (isolement des autres réseaux, bord de la carte)
bool viaLibre(const ConceptionElec& c, Pt p, Nm diametre, const std::string& reseau, const std::vector<PastillePlacee>& pp) {
  for (const Couche k : couchesCuivre(c.circuit.empilage.couchesCuivre)) {
    OptionsTrace o;
    o.reseau = reseau;
    o.couche = k;
    o.largeur = diametre;
    o.pastilles = &pp;
    if (!segmentLibre(c, p, p, o)) return false;
  }
  return true;
}
}  // namespace

// ---------- 5.10, 5.22, 8.27 · Longueurs et retards ----------
double longueurReseau(const ConceptionElec& c, const std::string& reseau) {
  const Connectivite k = connectivite(c);
  const std::string n = k.canonique(reseau);
  double s = 0;
  for (const auto& p : c.circuit.pistes)
    if (k.canonique(p.reseau) == n) s += longueurPiste(p);
  return s;
}

double psParMm(const ConceptionElec& c, Couche couche, Nm largeur) {
  // Vitesse de propagation tirée du moteur de lignes (8.21) : ruban entre deux plans dans εr, microruban dans la
  // permittivité effective (Hammerstad et Jensen, dispersion à 1 GHz), d'après l'empilage
  return retardCouche(c, couche, largeur);
}

double retardReseau(const ConceptionElec& c, const std::string& reseau) {
  const Connectivite k = connectivite(c);
  const std::string n = k.canonique(reseau);
  double s = 0;
  for (const auto& p : c.circuit.pistes)
    if (k.canonique(p.reseau) == n) s += mm(static_cast<Nm>(longueurPiste(p))) * psParMm(c, p.couche, p.largeur);
  // Vias (8.21) : le fût parcouru, entre la plus haute et la plus basse des couches où le réseau la quitte
  const int nc = c.circuit.empilage.couchesCuivre;
  for (const auto& v : c.circuit.vias) {
    if (k.canonique(v.reseau) != n) continue;
    int haut = 1 << 20, bas = -1;
    for (const auto& p : c.circuit.pistes)
      if (k.canonique(p.reseau) == n && (distance(p.a, v.p) < static_cast<double>(v.diametre) / 2 || distance(p.b, v.p) < static_cast<double>(v.diametre) / 2)) {
        const int r = rangCuivre(p.couche, nc);
        haut = std::min(haut, r);
        bas = std::max(bas, r);
      }
    if (bas > haut) {
      const auto couches = couchesCuivre(nc);
      s += retardVia(c, couches[static_cast<std::size_t>(haut)], couches[static_cast<std::size_t>(bas)]);
    }
  }
  return s;
}

// ---------- 5.10 · Paire à écart constant ----------
namespace {
std::vector<Pt> decalerPolyligne(const std::vector<Pt>& p, double d) {
  std::vector<Pt> r;
  const std::size_t n = p.size();
  auto normale = [&](std::size_t i) {  // normale gauche du segment i → i+1
    const double dx = static_cast<double>(p[i + 1].x - p[i].x), dy = static_cast<double>(p[i + 1].y - p[i].y), l = std::hypot(dx, dy);
    return l < 1 ? std::pair{0.0, 0.0} : std::pair{-dy / l, dx / l};
  };
  for (std::size_t i = 0; i < n; ++i) {
    double nx = 0, ny = 0;
    if (i == 0) std::tie(nx, ny) = normale(0);
    else if (i + 1 == n) std::tie(nx, ny) = normale(n - 2);
    else {
      const auto [ax, ay] = normale(i - 1);
      const auto [bx, by] = normale(i);
      const double k = 1 + ax * bx + ay * by;
      if (k < 1e-6) std::tie(nx, ny) = std::pair{ax, ay};
      else nx = (ax + bx) / k, ny = (ay + by) / k;  // onglet : la distance d est tenue aux deux segments
    }
    r.push_back(arrondi(static_cast<double>(p[i].x) + nx * d, static_cast<double>(p[i].y) + ny * d));
  }
  return r;
}
}  // namespace

BilanPaire routerPaire(ConceptionElec& c, const PaireDifferentielle& paire, Couche couche) {
  exigerModifiable(c);
  BilanPaire b;
  const Connectivite k = connectivite(c);
  const std::string P = k.canonique(paire.positif), N = k.canonique(paire.negatif);
  const auto pp = pastilles(c, k);
  std::vector<Pt> pP, pN;
  for (const auto& x : pp) {
    if (x.inaccessible || !(x.couches & bit(couche))) continue;
    if (x.reseau == P) pP.push_back(x.pastille.position);
    else if (x.reseau == N) pN.push_back(x.pastille.position);
  }
  if (pP.size() < 2 || pN.size() < 2) {
    b.raison = "chaque réseau de la paire doit avoir deux pastilles sur " + std::string(codeCouche(couche));
    return b;
  }
  // Bouts de « P » : ses deux pastilles les plus éloignées ; ceux de « N » : les plus proches de chacun
  Pt p1 = pP[0], p2 = pP[1];
  for (std::size_t i = 0; i < pP.size(); ++i)
    for (std::size_t j = i + 1; j < pP.size(); ++j)
      if (distance(pP[i], pP[j]) > distance(p1, p2)) p1 = pP[i], p2 = pP[j];
  auto plusProche = [&](Pt q) { return *std::min_element(pN.begin(), pN.end(), [&](Pt a, Pt b2) { return distance(a, q) < distance(b2, q); }); };
  const Pt n1 = plusProche(p1), n2 = plusProche(p2);
  const Regles& R = c.circuit.regles;
  const Nm w = paire.largeur > 0 ? paire.largeur : largeurPiste(R, P);
  const Nm s = paire.ecart > 0 ? paire.ecart : std::max(isolementEntre(R, P, N), paire.ecartMin);
  b.ecart = s;
  OptionsTrace o;
  o.reseau = P;
  o.couche = couche;
  o.largeur = w;
  o.pastilles = &pp;
  const ResultatTrace tp = contourner(c, p1, p2, o);
  if (!tp.ok || tp.points.size() < 2) {
    b.raison = "« " + P + " » : " + tp.raison;
    return b;
  }
  const std::vector<Piste> pistesP = pistesDuTrace(tp.points, P, couche, w);
  // « N » suit « P » à écart constant, du côté où se trouve sa première pastille
  const double ux = static_cast<double>(tp.points[1].x - tp.points[0].x), uy = static_cast<double>(tp.points[1].y - tp.points[0].y);
  const double cote = ux * static_cast<double>(n1.y - p1.y) - uy * static_cast<double>(n1.x - p1.x) >= 0 ? 1.0 : -1.0;
  std::vector<Pt> chemin{n1};
  for (const Pt q : decalerPolyligne(tp.points, cote * static_cast<double>(w + s))) chemin.push_back(q);
  chemin.push_back(n2);
  chemin.erase(std::unique(chemin.begin(), chemin.end()), chemin.end());
  OptionsTrace on = o;
  on.reseau = N;
  on.obstaclesEnPlus = pistesP;
  bool libre = chemin.size() >= 2;
  for (std::size_t i = 1; i < chemin.size() && libre; ++i) libre = segmentLibre(c, chemin[i - 1], chemin[i], on);
  std::vector<Pt> tn = chemin;
  if (!libre) {
    const ResultatTrace r = contourner(c, n1, n2, on);
    if (!r.ok) {
      b.raison = "« " + N + " » : " + r.raison;
      return b;
    }
    tn = r.points;
    b.raison = "écart constant impossible sur tout le trajet : « " + N + " » contourne seul";
  }
  for (const auto& x : pistesP) {
    b.pistes.push_back(x.uuid);
    c.circuit.pistes.push_back(x);
  }
  for (const auto& x : pistesDuTrace(tn, N, couche, w)) {
    b.pistes.push_back(x.uuid);
    c.circuit.pistes.push_back(x);
  }
  b.ok = true;
  return b;
}

// ---------- 5.22 · Motifs d'accord ----------
namespace {
// Polyligne dont les coins sont arrondis d'un rayon r (arcs de piste), en pistes
std::vector<Piste> pistesArrondies(const std::vector<Pt>& p, Nm r, const MotifAccord& m) {
  std::vector<Piste> out;
  auto ajouter = [&](Pt a, Pt b, std::optional<Pt> milieu) {
    if (a == b) return;
    Piste x;
    x.uuid = nouvelUuid();
    x.reseau = m.reseau;
    x.couche = m.couche;
    x.a = a;
    x.b = b;
    x.largeur = m.largeur;
    x.milieu = milieu;
    x.motif = m.uuid;
    out.push_back(std::move(x));
  };
  if (p.size() < 2) return out;
  Pt courant = p.front();
  for (std::size_t i = 1; i + 1 < p.size(); ++i) {
    const Pt v = p[i];
    const double ax = static_cast<double>(v.x - p[i - 1].x), ay = static_cast<double>(v.y - p[i - 1].y), la = std::hypot(ax, ay);
    const double bx = static_cast<double>(p[i + 1].x - v.x), by = static_cast<double>(p[i + 1].y - v.y), lb = std::hypot(bx, by);
    if (r <= 0 || la < 1 || lb < 1) {
      ajouter(courant, v, std::nullopt);
      courant = v;
      continue;
    }
    const double d1x = ax / la, d1y = ay / la, d2x = bx / lb, d2y = by / lb;
    const double cosT = std::clamp(d1x * d2x + d1y * d2y, -1.0, 1.0), theta = std::acos(cosT);
    if (theta < 1e-3) {
      ajouter(courant, v, std::nullopt);
      courant = v;
      continue;
    }
    // Tangente : t = r·tan(θ/2), tenue à la moitié des segments voisins
    double t = static_cast<double>(r) * std::tan(theta / 2);
    t = std::min({t, la / 2, lb / 2});
    const double rr = t / std::tan(theta / 2);
    const Pt t1 = arrondi(static_cast<double>(v.x) - d1x * t, static_cast<double>(v.y) - d1y * t);
    const Pt t2 = arrondi(static_cast<double>(v.x) + d2x * t, static_cast<double>(v.y) + d2y * t);
    double bisx = d2x - d1x, bisy = d2y - d1y;
    const double lbis = std::hypot(bisx, bisy);
    bisx /= lbis, bisy /= lbis;
    const double dc = rr / std::cos(theta / 2);
    const Pt mil = arrondi(static_cast<double>(v.x) + bisx * (dc - rr), static_cast<double>(v.y) + bisy * (dc - rr));
    ajouter(courant, t1, std::nullopt);
    ajouter(t1, t2, mil);
    courant = t2;
  }
  ajouter(courant, p.back(), std::nullopt);
  return out;
}
}  // namespace

std::vector<Piste> segmentsMotif(const MotifAccord& m) {
  const double L = distance(m.a, m.b);
  const Nm w = m.largeur;
  const double pas = static_cast<double>(m.pas > 0 ? m.pas : std::max<Nm>(4 * w, 800'000));
  const int bosses = L > 0 ? static_cast<int>(std::floor((L - pas) / pas)) : 0;  // une demi-période d'entrée et de sortie
  if (bosses < 1 || m.amplitude <= 0) {
    MotifAccord seul = m;
    return pistesArrondies({m.a, m.b}, 0, seul);
  }
  const double ux = static_cast<double>(m.b.x - m.a.x) / L, uy = static_cast<double>(m.b.y - m.a.y) / L;
  const double nx = -uy, ny = ux;
  const double A = static_cast<double>(m.amplitude);
  const double debut = (L - bosses * pas) / 2;
  auto point = [&](double le, double tr) { return arrondi(static_cast<double>(m.a.x) + ux * le + nx * tr, static_cast<double>(m.a.y) + uy * le + ny * tr); };
  std::vector<Pt> p{m.a};
  std::vector<Piste> out;
  if (m.genre == GenreMotifAccord::DentsDeScie) {
    for (int i = 0; i < bosses; ++i) {
      const double s = m.deuxCotes && i % 2 ? -1.0 : 1.0, x0 = debut + i * pas;
      p.push_back(point(x0, 0));
      p.push_back(point(x0 + pas / 2, s * A));
    }
    p.push_back(point(debut + bosses * pas, 0));
    p.push_back(m.b);
    return pistesArrondies(p, 0, m);
  }
  if (m.genre == GenreMotifAccord::Trombone) {
    // Branches montantes et descendantes réunies par un demi-cercle de diamètre pas/2
    const double rayon = pas / 4;
    std::vector<Piste> r;
    Pt courant = m.a;
    auto droit = [&](Pt q) {
      for (auto& x : pistesArrondies({courant, q}, 0, m)) r.push_back(std::move(x));
      courant = q;
    };
    for (int i = 0; i < bosses; ++i) {
      const double s = m.deuxCotes && i % 2 ? -1.0 : 1.0, x0 = debut + i * pas;
      droit(point(x0 + pas / 4 - rayon, 0));
      droit(point(x0 + pas / 4 - rayon, s * A));
      Piste arc;
      arc.uuid = nouvelUuid();
      arc.reseau = m.reseau;
      arc.couche = m.couche;
      arc.largeur = m.largeur;
      arc.motif = m.uuid;
      arc.a = courant;
      arc.b = point(x0 + pas / 4 + rayon, s * A);
      arc.milieu = point(x0 + pas / 4, s * (A + rayon));
      r.push_back(arc);
      courant = arc.b;
      droit(point(x0 + pas / 4 + rayon, 0));
      droit(point(x0 + pas, 0));
    }
    droit(m.b);
    return r;
  }
  // Accordéon : créneaux rectangulaires, coins arrondis de « rayon »
  for (int i = 0; i < bosses; ++i) {
    const double s = m.deuxCotes && i % 2 ? -1.0 : 1.0, x0 = debut + i * pas;
    p.push_back(point(x0 + pas / 4, 0));
    p.push_back(point(x0 + pas / 4, s * A));
    p.push_back(point(x0 + 3 * pas / 4, s * A));
    p.push_back(point(x0 + 3 * pas / 4, 0));
  }
  p.push_back(m.b);
  return pistesArrondies(p, m.rayon, m);
}

double longueurMotif(const MotifAccord& m) {
  double s = 0;
  for (const auto& p : segmentsMotif(m)) s += longueurPiste(p);
  return s;
}

namespace {
// Amplitude qui donne au motif la longueur « voulue » (bissection : la longueur croît avec l'amplitude)
Nm amplitudePour(MotifAccord m, double voulue, Nm amplitudeMax) {
  m.amplitude = amplitudeMax;
  if (longueurMotif(m) <= voulue) return amplitudeMax;
  Nm bas = 0, haut = amplitudeMax;
  for (int i = 0; i < 40 && haut - bas > 100; ++i) {
    const Nm milieu = (bas + haut) / 2;
    m.amplitude = milieu;
    (longueurMotif(m) < voulue ? bas : haut) = milieu;
  }
  return haut;
}
bool motifLibre(const ConceptionElec& c, const MotifAccord& m, const std::vector<PastillePlacee>& pp) {
  OptionsTrace o;
  o.reseau = m.reseau;
  o.couche = m.couche;
  o.largeur = m.largeur;
  o.pastilles = &pp;
  for (const auto& x : segmentsMotif(m))
    for (const auto& [a, b] : segmentsPiste(x))
      if (!segmentLibre(c, a, b, o)) return false;
  return true;
}
}  // namespace

std::string accorderReseau(ConceptionElec& c, const std::string& reseau, Nm cible, GenreMotifAccord genre, Nm amplitudeMax) {
  exigerModifiable(c);
  const Connectivite k = connectivite(c);
  const std::string n = k.canonique(reseau);
  double L = 0;
  for (const auto& p : c.circuit.pistes)
    if (k.canonique(p.reseau) == n) L += longueurPiste(p);
  const double manque = static_cast<double>(cible) - L;
  if (manque <= 1000) return {};
  // Le plus long segment droit, libre de tout motif et de tout verrou
  auto it = c.circuit.pistes.end();
  double plusLong = 0;
  for (auto i = c.circuit.pistes.begin(); i != c.circuit.pistes.end(); ++i)
    if (k.canonique(i->reseau) == n && !i->milieu && i->motif.empty() && i->verrou == Verrou::Aucun && i->largeurFin == 0 && distance(i->a, i->b) > plusLong) {
      plusLong = distance(i->a, i->b);
      it = i;
    }
  if (it == c.circuit.pistes.end()) return {};
  const Piste origine = *it;
  const Regles& R = c.circuit.regles;
  const Nm iso = std::max(isolementEntre(R, n, n), R.isolement);
  MotifAccord m;
  m.uuid = nouvelUuid();
  m.reseau = origine.reseau;
  m.genre = genre;
  m.couche = origine.couche;
  m.largeur = origine.largeur;
  m.pas = 2 * (origine.largeur + iso);
  m.rayon = genre == GenreMotifAccord::Accordeon ? origine.largeur : 0;
  m.longueurCible = cible;
  const auto pp = pastilles(c, k);
  // Portée du motif : juste ce qu'il faut de bosses à l'amplitude maximale, centrée sur le segment
  const double gainParBosse = 2.0 * static_cast<double>(amplitudeMax) * 0.9;
  const int bossesVoulues = std::max(1, static_cast<int>(std::ceil(manque / gainParBosse)) + 1);
  const double portee = std::min(plusLong, static_cast<double>(m.pas) * (bossesVoulues + 1));
  const double ux = static_cast<double>(origine.b.x - origine.a.x) / plusLong, uy = static_cast<double>(origine.b.y - origine.a.y) / plusLong;
  const double avant = (plusLong - portee) / 2;
  const Pt debut = arrondi(static_cast<double>(origine.a.x) + ux * avant, static_cast<double>(origine.a.y) + uy * avant);
  const Pt fin = arrondi(static_cast<double>(origine.a.x) + ux * (avant + portee), static_cast<double>(origine.a.y) + uy * (avant + portee));
  // Variantes essayées : des deux côtés, puis d'un seul côté (gauche, droite), à amplitude réduite si la place manque
  struct Variante {
    bool deuxCotes;
    bool inverse;
  };
  for (Nm amax = amplitudeMax; amax >= 2 * origine.largeur; amax /= 2)
    for (const Variante v : {Variante{true, false}, Variante{false, false}, Variante{false, true}}) {
      MotifAccord e = m;
      e.deuxCotes = v.deuxCotes;
      e.a = v.inverse ? fin : debut;
      e.b = v.inverse ? debut : fin;
      e.amplitude = amplitudePour(e, distance(debut, fin) + manque, amax);
      if (!motifLibre(c, e, pp)) continue;
      // Pose : le segment d'origine laisse la place à l'entrée, au motif et à la sortie
      c.circuit.pistes.erase(std::find_if(c.circuit.pistes.begin(), c.circuit.pistes.end(), [&](const Piste& p) { return p.uuid == origine.uuid; }));
      if (debut != origine.a) {
        Piste x = origine;
        x.uuid = nouvelUuid();
        x.b = debut;
        c.circuit.pistes.push_back(x);
      }
      for (auto& x : segmentsMotif(e)) c.circuit.pistes.push_back(std::move(x));
      if (fin != origine.b) {
        Piste x = origine;
        x.uuid = nouvelUuid();
        x.a = fin;
        c.circuit.pistes.push_back(x);
      }
      c.circuit.motifs.push_back(e);
      return e.uuid;
    }
  return {};
}

int regenererMotifs(ConceptionElec& c) {
  exigerModifiable(c);
  int n = 0;
  const Connectivite k = connectivite(c);
  for (auto& m : c.circuit.motifs) {
    std::erase_if(c.circuit.pistes, [&](const Piste& p) { return p.motif == m.uuid; });
    // Règle changée : largeur et pas d'après la classe du réseau, puis amplitude refaite pour la cible
    const Regles& R = c.circuit.regles;
    m.largeur = largeurPiste(R, k.canonique(m.reseau));
    m.pas = 2 * (m.largeur + std::max(isolementEntre(R, m.reseau, m.reseau), R.isolement));
    if (m.longueurCible > 0) {
      double reste = 0;
      for (const auto& p : c.circuit.pistes)
        if (k.canonique(p.reseau) == k.canonique(m.reseau)) reste += longueurPiste(p);
      const double voulue = static_cast<double>(m.longueurCible) - reste;
      if (voulue > distance(m.a, m.b)) m.amplitude = amplitudePour(m, voulue, std::max<Nm>(m.amplitude * 2, 1'000'000));
    }
    for (auto& x : segmentsMotif(m)) c.circuit.pistes.push_back(std::move(x));
    ++n;
  }
  return n;
}

int accorderGroupe(ConceptionElec& c, const GroupeAccord& g, std::vector<std::string>* nonTenus) {
  exigerModifiable(c);
  // En retard, cible et tolérance sont en femtosecondes ; en longueur, en nanomètres
  auto mesure = [&](const std::string& r) { return g.retard ? retardReseau(c, r) * 1000 : longueurReseau(c, r); };
  double cible = static_cast<double>(g.cible);
  if (cible <= 0)
    for (const auto& r : g.reseaux) cible = std::max(cible, mesure(r));
  int n = 0;
  for (const auto& r : g.reseaux) {
    const double v = mesure(r);
    if (v >= cible - static_cast<double>(g.tolerance)) {
      if (v > cible + static_cast<double>(g.tolerance) && nonTenus) nonTenus->push_back(r);
      continue;
    }
    double cibleLongueur = cible;
    if (g.retard) {
      const double L = longueurReseau(c, r), fsParNm = L > 0 ? v / L : psParMm(c, Couche::CuivreDessus, c.circuit.regles.largeur) * 1e-3;
      cibleLongueur = L + (cible - v) / fsParNm;
    }
    if (!accorderReseau(c, r, static_cast<Nm>(std::llround(cibleLongueur)), GenreMotifAccord::Accordeon).empty()) ++n;
    if (std::abs(mesure(r) - cible) > static_cast<double>(g.tolerance) && nonTenus) nonTenus->push_back(r);
  }
  return n;
}

// ---------- 5.22 · Gouttes ----------
namespace {
// Distance du centre au bord de la forme le long de u
double rayonVers(const Polygone& f, Pt centre, double ux, double uy) {
  double t = 0;
  for (std::size_t i = 0, n = f.size(); i < n; ++i) {
    const double ax = static_cast<double>(f[i].x - centre.x), ay = static_cast<double>(f[i].y - centre.y);
    const double bx = static_cast<double>(f[(i + 1) % n].x - centre.x), by = static_cast<double>(f[(i + 1) % n].y - centre.y);
    const double ex = bx - ax, ey = by - ay;
    const double den = ux * ey - uy * ex;
    if (std::abs(den) < 1e-12) continue;
    const double s = (ax * uy - ay * ux) / den;  // position sur l'arête
    const double tt = (ax * ey - ay * ex) / den;  // distance le long du rayon
    if (s >= -1e-9 && s <= 1 + 1e-9 && tt > t) t = tt;
  }
  return t;
}
}  // namespace

int poserGouttes(ConceptionElec& c, const OptionsGouttes& o) {
  exigerModifiable(c);
  retirerGouttes(c);
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  struct Cible {
    Pt centre;
    double dmin;
    std::string reseau, objet;
    Couches couches;
    Polygone forme;
  };
  std::vector<Cible> cibles;
  for (const auto& x : pp) {
    if (x.inaccessible || x.reseau.empty() || x.pastille.type == TypePastille::NonMetallisee) continue;
    if ((x.pastille.type == TypePastille::CMS && !o.cms) || (x.pastille.type == TypePastille::Traversante && !o.traversantes)) continue;
    cibles.push_back({x.pastille.position, static_cast<double>(std::min(x.pastille.largeur, x.pastille.hauteur)), x.reseau, x.composant + ":" + x.numero, x.couches, x.forme});
  }
  if (o.vias)
    for (const auto& v : c.circuit.vias) cibles.push_back({v.p, static_cast<double>(v.diametre), k.canonique(v.reseau), v.uuid, couchesVia(v, c.circuit.empilage.couchesCuivre), cercle(v.p, v.diametre / 2)});
  int n = 0;
  for (const auto& t : cibles)
    for (const auto& p : c.circuit.pistes) {
      if (!(t.couches & bit(p.couche)) || p.milieu || k.canonique(p.reseau) != t.reseau) continue;
      Pt autre;
      if (dedans(t.forme, p.a)) autre = p.b;
      else if (dedans(t.forme, p.b)) autre = p.a;
      else continue;
      if (static_cast<double>(p.largeur) >= t.dmin * o.largeur) continue;  // piste déjà aussi large que la goutte
      const double L = distance(t.centre, autre);
      if (L < 1) continue;
      const double ux = static_cast<double>(autre.x - t.centre.x) / L, uy = static_cast<double>(autre.y - t.centre.y) / L, nx = -uy, ny = ux;
      const double rb = rayonVers(t.forme, t.centre, ux, uy);
      const double longueur = std::min(o.pourcentage * t.dmin, static_cast<double>(o.longueurMax));
      const double pointe = rb + longueur;
      if (pointe >= L - static_cast<double>(p.largeur) / 2) continue;  // piste trop courte pour porter la goutte
      const double wb = o.largeur * t.dmin / 2, wt = static_cast<double>(p.largeur) / 2, xb = rb * 0.6;
      auto surCarte = [&](double x, double y) { return arrondi(static_cast<double>(t.centre.x) + ux * x + nx * y, static_cast<double>(t.centre.y) + uy * x + ny * y); };
      Polygone gauche, droite;
      gauche.push_back(surCarte(0, wb));
      for (int i = 0; i <= 8; ++i) {
        const double s = i / 8.0, x = xb + (pointe - xb) * s, y = wt + (wb - wt) * (1 - s) * (1 - s);
        gauche.push_back(surCarte(x, y));
        droite.push_back(surCarte(x, -y));
      }
      droite.push_back(surCarte(0, -wb));
      Polygone forme = gauche;
      forme.insert(forme.end(), droite.rbegin(), droite.rend());
      // La goutte garde l'isolement des autres réseaux sur tout son pourtour
      OptionsTrace ot;
      ot.reseau = t.reseau;
      ot.couche = p.couche;
      ot.largeur = 2'000;
      ot.pastilles = &pp;
      bool libre = true;
      for (std::size_t i = 0; i < forme.size() && libre; ++i) libre = segmentLibre(c, forme[i], forme[(i + 1) % forme.size()], ot);
      if (!libre) continue;
      RegionCuivre r;
      r.uuid = nouvelUuid();
      r.reseau = p.reseau;
      r.genre = "goutte";
      r.couche = p.couche;
      r.forme = orienter(forme);
      r.objet = t.objet;
      c.circuit.regions.push_back(std::move(r));
      ++n;
    }
  return n;
}

int retirerGouttes(ConceptionElec& c) {
  exigerModifiable(c);
  return static_cast<int>(std::erase_if(c.circuit.regions, [](const RegionCuivre& r) { return r.genre == "goutte" && r.verrou == Verrou::Aucun; }));
}

// ---------- 5.11, 5.25, 5.26 · Zones ----------
int nommerZones(ConceptionElec& c) {
  exigerModifiable(c);
  std::map<std::string, int> vus;
  for (const auto& z : c.circuit.zones)
    if (!z.nom.empty()) ++vus[z.nom];
  std::set<std::string> pris;
  int n = 0;
  for (auto& z : c.circuit.zones) {
    if (!z.nom.empty() && vus[z.nom] == 1) {
      pris.insert(z.nom);
      continue;
    }
    if (!z.nom.empty() && !pris.count(z.nom)) {  // le premier de plusieurs homonymes garde son nom
      pris.insert(z.nom);
      continue;
    }
    const std::string base = (z.reseau.empty() ? std::string("sans réseau") : z.reseau) + " · " + codeCouche(z.couche);
    std::string nom = base;
    for (int i = 2; pris.count(nom); ++i) nom = base + " (" + std::to_string(i) + ")";
    z.nom = nom;
    pris.insert(nom);
    ++n;
  }
  return n;
}

std::string zoneSurToutLaCarte(ConceptionElec& c, const std::string& reseau, Couche couche) {
  exigerModifiable(c);
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = reseau;
  z.couche = couche;
  z.contour = orienter(contourDeLaCarte(c));
  c.circuit.zones.push_back(z);
  nommerZones(c);
  return z.uuid;
}

std::vector<std::string> planDuReseau(ConceptionElec& c, const std::string& reseau, const std::vector<Couche>& couches) {
  std::vector<std::string> r;
  for (const Couche k : couches) {
    if (!estCuivre(k)) continue;
    // Une zone du réseau qui couvre déjà toute la carte sur cette couche suffit
    const bool deja = std::any_of(c.circuit.zones.begin(), c.circuit.zones.end(), [&](const Zone& z) {
      return z.reseau == reseau && z.couche == k && std::abs(aireMm2(z.contour) - aireMm2(contourDeLaCarte(c))) < 0.01;
    });
    if (!deja) r.push_back(zoneSurToutLaCarte(c, reseau, k));
  }
  return r;
}

int coudreZone(ConceptionElec& c, const std::string& id, Nm pas, Nm diametre, Nm percage) {
  exigerModifiable(c);
  Zone* z = zoneParId(c, id);
  if (!z) return 0;
  const std::string groupe = "couture:" + id;
  std::erase_if(c.circuit.vias, [&](const Via& v) { return v.groupe == groupe; });
  const Regles& R = c.circuit.regles;
  pas = std::max<Nm>(pas, diametre + R.isolement);
  pas = std::max<Nm>(pas, percage + R.ecartTrous);
  z->pasCouture = pas;
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  const auto f = r.find(id);
  if (f == r.end() || f->second.empty()) return 0;
  const Polygones interieur = gonfler(f->second, -(diametre / 2 + 50'000));
  const auto pp = pastilles(c, k);
  const std::string reseau = z->reseau;
  const Boite b = boite(f->second);
  int n = 0;
  for (Nm y = b.y0 + pas / 2; y < b.y1; y += pas)
    for (Nm x = b.x0 + pas / 2; x < b.x1; x += pas) {
      const Pt p{x, y};
      if (!dedans(interieur, p) || !viaLibre(c, p, diametre, reseau, pp)) continue;
      Via v;
      v.uuid = nouvelUuid();
      v.reseau = reseau;
      v.p = p;
      v.diametre = diametre;
      v.percage = percage;
      v.groupe = groupe;
      c.circuit.vias.push_back(v);
      ++n;
    }
  return n;
}

int relierIlotsParVias(ConceptionElec& c, const std::string& id) {
  exigerModifiable(c);
  Zone* z = zoneParId(c, id);
  if (!z) return 0;
  const std::string groupe = "ilot:" + id;
  std::erase_if(c.circuit.vias, [&](const Via& v) { return v.groupe == groupe; });
  const Connectivite k = connectivite(c);
  const Remplissages r = remplirZones(c, k);
  // Le remplissage retire les îlots qu'aucun cuivre du réseau ne touche : ils se cherchent sur une copie où la zone
  // n'a pas de réseau (tous ses îlots gardés)
  ConceptionElec sansReseau = c;
  for (auto& y : sansReseau.circuit.zones)
    if (y.uuid == id) y.reseau.clear();
  const Remplissages tous = remplirZones(sansReseau, connectivite(sansReseau));
  const auto f = tous.find(id);
  if (f == tous.end()) return 0;
  const std::string reseau = k.canonique(z->reseau);
  const auto pp = pastilles(c, k);
  const Regles& R = c.circuit.regles;
  const Nm d = viaDiametre(R, reseau), perc = viaPercage(R, reseau);
  // Cuivre du réseau qui ancre un îlot : pastilles, vias, bouts de pistes
  std::vector<Pt> ancres;
  for (const auto& x : pp)
    if (x.reseau == reseau && (x.couches & bit(z->couche))) ancres.push_back(x.pastille.position);
  for (const auto& v : c.circuit.vias)
    if (k.canonique(v.reseau) == reseau) ancres.push_back(v.p);
  for (const auto& p : c.circuit.pistes)
    if (p.couche == z->couche && k.canonique(p.reseau) == reseau) {
      ancres.push_back(p.a);
      ancres.push_back(p.b);
    }
  // Autres zones du même réseau, sur d'autres couches : là où la via descend
  std::vector<Polygones> ailleurs;
  for (const auto& y : c.circuit.zones)
    if (y.uuid != id && y.couche != z->couche && k.canonique(y.reseau) == reseau)
      if (const auto g = r.find(y.uuid); g != r.end() && !g->second.empty()) ailleurs.push_back(gonfler(g->second, -(d / 2 + 50'000)));
  if (ailleurs.empty()) return 0;
  const Polygones rentre = gonfler(f->second, -(d / 2 + 50'000));
  int n = 0;
  for (const auto& ilot : f->second) {
    if (!estExterieur(ilot)) continue;
    if (std::any_of(ancres.begin(), ancres.end(), [&](Pt a) { return dedans(ilot, a); })) continue;
    const Boite b = boite(ilot);
    const Nm pas = std::max<Nm>(d, std::max(b.largeur(), b.hauteur()) / 24);
    bool pose = false;
    for (Nm y = b.y0 + pas / 2; y < b.y1 && !pose; y += pas)
      for (Nm x = b.x0 + pas / 2; x < b.x1 && !pose; x += pas) {
        const Pt p{x, y};
        if (!dedans(ilot, p) || !dedans(rentre, p)) continue;
        if (std::none_of(ailleurs.begin(), ailleurs.end(), [&](const Polygones& a) { return dedans(a, p); })) continue;
        if (!viaLibre(c, p, d, z->reseau, pp)) continue;
        Via v;
        v.uuid = nouvelUuid();
        v.reseau = z->reseau;
        v.p = p;
        v.diametre = d;
        v.percage = perc;
        v.groupe = groupe;
        c.circuit.vias.push_back(v);
        pose = true;
        ++n;
      }
  }
  return n;
}

std::string dupliquerZone(ConceptionElec& c, const std::string& id, Couche couche) {
  exigerModifiable(c);
  const Zone* z = zoneParId(c, id);
  if (!z) return {};
  Zone y = *z;
  y.uuid = nouvelUuid();
  y.couche = couche;
  y.nom.clear();
  y.etat = EtatZone::Normale;
  y.remplissageFige.clear();
  y.signature.clear();
  c.circuit.zones.push_back(y);
  nommerZones(c);
  return y.uuid;
}

std::vector<std::string> couperZone(ConceptionElec& c, const std::string& id, Pt a, Pt b) {
  exigerModifiable(c);
  Zone* z = zoneParId(c, id);
  if (!z || a == b) return {};
  // Deux demi-plans de part et d'autre de la droite (a, b), assez grands pour couvrir la zone
  const Boite bz = boite(z->contour);
  const double M = static_cast<double>(std::max(bz.largeur(), bz.hauteur())) * 4 + 1e7 + distance(a, bz.centre()) * 2;
  const double L = distance(a, b), ux = static_cast<double>(b.x - a.x) / L, uy = static_cast<double>(b.y - a.y) / L, nx = -uy, ny = ux;
  auto pt = [&](double le, double tr) { return arrondi(static_cast<double>(a.x) + ux * le + nx * tr, static_cast<double>(a.y) + uy * le + ny * tr); };
  const Polygone gauche{pt(-M, 0), pt(M, 0), pt(M, M), pt(-M, M)}, droite{pt(-M, 0), pt(-M, -M), pt(M, -M), pt(M, 0)};
  const auto pg = pieces(intersecter({z->contour}, {orienter(gauche)}));
  const auto pd = pieces(intersecter({z->contour}, {orienter(droite)}));
  if (pg.empty() || pd.empty()) return {};
  std::vector<Polygone> toutes = pg;
  toutes.insert(toutes.end(), pd.begin(), pd.end());
  const Zone modele = *z;
  z->contour = toutes.front();
  z->etat = EtatZone::Normale;
  z->remplissageFige.clear();
  std::vector<std::string> r{z->uuid};
  for (std::size_t i = 1; i < toutes.size(); ++i) {
    Zone y = modele;
    y.uuid = nouvelUuid();
    y.nom.clear();
    y.contour = toutes[i];
    y.etat = EtatZone::Normale;
    y.remplissageFige.clear();
    y.signature.clear();
    c.circuit.zones.push_back(y);
    r.push_back(y.uuid);
  }
  nommerZones(c);
  return r;
}

std::string fusionnerZones(ConceptionElec& c, const std::vector<std::string>& ids) {
  exigerModifiable(c);
  if (ids.size() < 2) return {};
  std::vector<Zone*> zs;
  for (const auto& id : ids)
    if (Zone* z = zoneParId(c, id)) zs.push_back(z);
  if (zs.size() != ids.size()) return {};
  Polygones tous;
  for (const Zone* z : zs) {
    if (z->reseau != zs.front()->reseau || z->couche != zs.front()->couche) return {};
    tous.push_back(z->contour);
  }
  const Polygones u = unir(tous);
  if (u.size() != 1) return {};  // zones disjointes, ou réunion trouée : elles restent séparées
  Zone& garde = *zs.front();
  garde.contour = u.front();
  for (const Zone* z : zs) garde.priorite = std::max(garde.priorite, z->priorite);
  garde.etat = EtatZone::Normale;
  garde.remplissageFige.clear();
  const std::string id = garde.uuid;
  const std::set<std::string> autres(ids.begin() + 1, ids.end());
  std::erase_if(c.circuit.zones, [&](const Zone& z) { return autres.count(z.uuid) > 0; });
  return id;
}

bool soustraireZone(ConceptionElec& c, const std::string& id, const std::string& autre) {
  exigerModifiable(c);
  Zone* z = zoneParId(c, id);
  const Zone* y = zoneParId(c, autre);
  if (!z || !y || z == y) return false;
  const auto p = pieces(soustraire({z->contour}, {y->contour}));
  if (p.empty()) return false;
  const Zone modele = *z;
  z->contour = p.front();
  z->etat = EtatZone::Normale;
  z->remplissageFige.clear();
  for (std::size_t i = 1; i < p.size(); ++i) {
    Zone x = modele;
    x.uuid = nouvelUuid();
    x.nom.clear();
    x.contour = p[i];
    x.remplissageFige.clear();
    x.etat = EtatZone::Normale;
    c.circuit.zones.push_back(x);
  }
  nommerZones(c);
  return true;
}

std::string zoneDepuisPistes(ConceptionElec& c, const std::set<std::string>& ids, Nm marge) {
  exigerModifiable(c);
  Polygones formes;
  const Piste* premiere = nullptr;
  for (const auto& p : c.circuit.pistes)
    if (ids.count(p.uuid)) {
      if (!premiere) premiere = &p;
      for (auto& f : formePiste(p, marge)) formes.push_back(std::move(f));
    }
  if (!premiere) return {};
  const auto p = pieces(unir(formes));
  if (p.empty()) return {};
  Zone z;
  z.uuid = nouvelUuid();
  z.reseau = premiere->reseau;
  z.couche = premiere->couche;
  z.contour = p.front();
  c.circuit.zones.push_back(z);
  nommerZones(c);
  return z.uuid;
}

bool arrondirZone(ConceptionElec& c, const std::string& id, Nm rayon, bool chanfrein) {
  exigerModifiable(c);
  Zone* z = zoneParId(c, id);
  if (!z || rayon <= 0 || z->contour.size() < 3) return false;
  if (chanfrein) {
    Polygone r;
    const auto& p = z->contour;
    for (std::size_t i = 0, n = p.size(); i < n; ++i) {
      const Pt v = p[i], a = p[(i + n - 1) % n], b = p[(i + 1) % n];
      const double la = distance(v, a), lb = distance(v, b);
      const double t = std::min({static_cast<double>(rayon), la / 2, lb / 2});
      if (t < 1) {
        r.push_back(v);
        continue;
      }
      r.push_back(arrondi(static_cast<double>(v.x) + static_cast<double>(a.x - v.x) / la * t, static_cast<double>(v.y) + static_cast<double>(a.y - v.y) / la * t));
      r.push_back(arrondi(static_cast<double>(v.x) + static_cast<double>(b.x - v.x) / lb * t, static_cast<double>(v.y) + static_cast<double>(b.y - v.y) / lb * t));
    }
    z->contour = r;
  } else {
    // Ouverture puis fermeture : coins saillants puis rentrants arrondis du même rayon
    const Polygones ouvert = gonfler(gonfler({z->contour}, -rayon), rayon);
    const auto p = pieces(gonfler(gonfler(ouvert, rayon), -rayon));
    if (p.empty()) return false;
    z->contour = p.front();
  }
  z->etat = EtatZone::Normale;
  z->remplissageFige.clear();
  return true;
}

std::string signatureZone(const ConceptionElec& c, const Zone& z) {
  // Ce dont dépend le remplissage : la zone, le contour de la carte, les règles, et le cuivre qui l'approche
  std::ostringstream s;
  auto pt = [&](Pt p) { s << p.x << ',' << p.y << ';'; };
  s << z.reseau << '|' << static_cast<int>(z.couche) << '|' << z.priorite << '|' << z.isolement << '|' << z.epaisseurMin << '|' << z.thermique << '|' << z.largeurRayon << '|'
    << z.ecartThermique << '|' << static_cast<int>(z.remplissage) << '|' << z.hachureLargeur << '|' << z.hachureEcart << '|' << z.hachureAngle << '|' << z.thermiquesTraversantsSeuls
    << '|' << z.aireIlotMin << '|' << z.partition << '|' << z.garde << '#';
  for (const Pt p : z.contour) pt(p);
  s << "#carte:";
  for (const Pt p : contourDeLaCarte(c)) pt(p);
  for (const auto& d : c.circuit.contour.decoupes)
    for (const Pt p : d) pt(p);
  const Regles& R = c.circuit.regles;
  s << "#regles:" << R.isolement << ',' << R.bord << ',' << R.cuivreTrou << ',' << R.classes.size();
  for (const auto& k : R.classes) s << k.nom << ',' << k.isolement << ';';
  const Boite b = boite(z.contour).gonflee(5 * NM_PAR_MM);
  s << "#pistes:";
  for (const auto& p : c.circuit.pistes)
    if (p.couche == z.couche && (b.contient(p.a) || b.contient(p.b))) {
      s << p.reseau << ':' << p.largeur << ':';
      pt(p.a);
      pt(p.b);
    }
  s << "#vias:";
  for (const auto& v : c.circuit.vias)
    if (b.contient(v.p)) {
      s << v.reseau << ':' << v.diametre << ':';
      pt(v.p);
    }
  s << "#empreintes:";
  for (const auto& e : c.circuit.empreintes)
    if (b.contient(e.position)) {
      s << e.composant << ':' << e.empreinte << ':' << e.rotation << ':' << e.dessous << ':';
      pt(e.position);
    }
  s << "#zones:";
  for (const auto& y : c.circuit.zones)
    if (y.uuid != z.uuid && y.couche == z.couche && boite(y.contour).touche(b)) {
      s << y.reseau << ':' << y.priorite << ':' << static_cast<int>(y.etat) << ':';
      for (const Pt p : y.contour) pt(p);
    }
  s << "#regions:";
  for (const auto& g : c.circuit.regions)
    if (g.couche == z.couche && boite(g.forme).touche(b)) {
      s << g.reseau << ':';
      for (const Pt p : g.forme) pt(p);
    }
  return sha256Hex(s.str()).substr(0, 16);
}

std::vector<std::string> remplissagesPerimes(const ConceptionElec& c) {
  std::vector<std::string> r;
  for (const auto& z : c.circuit.zones)
    if (z.etat == EtatZone::Figee && z.signature != signatureZone(c, z)) r.push_back(z.uuid);
  return r;
}

int etatZones(ConceptionElec& c, const std::set<std::string>& ids, EtatZone etat) {
  exigerModifiable(c);
  int n = 0;
  std::optional<Remplissages> r;
  for (auto& z : c.circuit.zones) {
    if (!ids.count(z.uuid)) continue;
    if (etat == EtatZone::Figee) {
      if (!r) r = remplirZones(c, connectivite(c));
      const auto f = r->find(z.uuid);
      z.remplissageFige = f == r->end() ? Polygones{} : f->second;
      z.signature = signatureZone(c, z);
    } else {
      z.remplissageFige.clear();
      z.signature.clear();
    }
    z.etat = etat;
    ++n;
  }
  return n;
}

// ---------- 5.24 · Placement assisté ----------
namespace {
bool recouvre(const ConceptionElec& c, const EmpreintePlacee& e, Nm marge = 0) {
  const DefEmpreinte* d = c.empreinte(e.empreinte);
  if (!d) return false;
  const Polygones a = marge > 0 ? gonfler({encombrementSurCarte(e, *d)}, marge) : Polygones{encombrementSurCarte(e, *d)};
  for (const auto& f : c.circuit.empreintes) {
    if (f.composant == e.composant || f.dessous != e.dessous) continue;
    const DefEmpreinte* g = c.empreinte(f.empreinte);
    if (!g) continue;
    const Polygones commun = intersecter(a, {encombrementSurCarte(f, *g)});
    if (!commun.empty() && aireMm2(commun) > 1e-6) return true;
  }
  return false;
}
bool dansLaCarte(const ConceptionElec& c, const EmpreintePlacee& e) {
  const DefEmpreinte* d = c.empreinte(e.empreinte);
  if (!d) return true;
  for (const Pt p : encombrementSurCarte(e, *d))
    if (!c.circuit.contour.contient(p)) return false;
  return true;
}
}  // namespace

int placerDecouplages(ConceptionElec& c, const std::string& ci, Nm distanceMax) {
  exigerModifiable(c);
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  const auto ie = std::find_if(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == ci; });
  if (ie == c.circuit.empreintes.end()) return 0;
  const bool dessous = ie->dessous;
  const DefEmpreinte* dci = c.empreinte(ie->empreinte);
  const Pt centreCI = dci ? versCarte(*ie, pointDePlacement(*dci)) : ie->position;
  std::vector<const PastillePlacee*> broches;
  for (const auto& x : pp)
    if (x.composant == ci && !x.reseau.empty() && !estMasse(x.reseau)) broches.push_back(&x);
  struct Candidat {
    std::string composant, alim;
    double valeur;
  };
  std::vector<Candidat> cand;
  for (const auto& x : c.composants) {
    if (x.uuid == ci || familleDepuisPrefixe(prefixeDe(x.reference)) != FamilleValeur::Condensateur) continue;
    std::vector<const PastillePlacee*> siennes;
    for (const auto& p : pp)
      if (p.composant == x.uuid) siennes.push_back(&p);
    if (siennes.size() != 2) continue;
    const PastillePlacee *alim = nullptr, *masse = nullptr;
    for (const auto* p : siennes) (estMasse(p->reseau) ? masse : alim) = p;
    if (!alim || !masse || alim->reseau.empty()) continue;
    if (std::none_of(broches.begin(), broches.end(), [&](const PastillePlacee* b) { return b->reseau == alim->reseau; })) continue;
    // Déjà au plus près d'une broche de son réseau : il reste où il est
    const bool proche = std::any_of(broches.begin(), broches.end(), [&](const PastillePlacee* b) { return b->reseau == alim->reseau && distance(b->pastille.position, alim->pastille.position) <= static_cast<double>(distanceMax); });
    if (proche) continue;
    const auto v = lireValeurComposant(x.valeur, FamilleValeur::Condensateur);
    cand.push_back({x.uuid, alim->reseau, v.comprise() ? v.nombre : 1.0});
  }
  std::stable_sort(cand.begin(), cand.end(), [](const Candidat& a, const Candidat& b) { return a.valeur < b.valeur; });
  std::set<const PastillePlacee*> servies;
  int n = 0;
  for (const auto& k2 : cand) {
    auto ie2 = std::find_if(c.circuit.empreintes.begin(), c.circuit.empreintes.end(), [&](const EmpreintePlacee& e) { return e.composant == k2.composant; });
    if (ie2 == c.circuit.empreintes.end() || ie2->verrou) continue;
    const DefEmpreinte* d = c.empreinte(ie2->empreinte);
    if (!d) continue;
    // Pastilles du condensateur dans son repère : celle de l'alimentation, celle de la masse
    Pt la{}, lm{};
    std::string numAlim, numMasse;
    for (const auto& p : pp)
      if (p.composant == k2.composant) (p.reseau == k2.alim ? numAlim : numMasse) = p.numero;
    for (const auto& p : d->pastilles) {
      if (p.numero == numAlim) la = p.position;
      if (p.numero == numMasse) lm = p.position;
    }
    // La broche d'alimentation du circuit qui n'a pas encore son condensateur, la plus proche de son centre d'abord
    std::vector<const PastillePlacee*> libres;
    for (const auto* b : broches)
      if (b->reseau == k2.alim && !servies.count(b)) libres.push_back(b);
    if (libres.empty())
      for (const auto* b : broches)
        if (b->reseau == k2.alim) libres.push_back(b);
    if (libres.empty()) continue;
    const PastillePlacee* broche = libres.front();
    const Pt P = broche->pastille.position;
    double vx = static_cast<double>(P.x - centreCI.x), vy = static_cast<double>(P.y - centreCI.y);
    if (std::abs(vx) >= std::abs(vy)) vx = vx >= 0 ? 1 : -1, vy = 0;  // sortie perpendiculaire au côté du boîtier
    else vy = vy >= 0 ? 1 : -1, vx = 0;
    const EmpreintePlacee avant = *ie2;
    bool place = false;
    const Nm pasRecherche = 250'000;
    const double rayonBroche = static_cast<double>(std::max(broche->pastille.largeur, broche->pastille.hauteur)) / 2;
    for (Nm dist = static_cast<Nm>(rayonBroche) + c.circuit.regles.isolement + pasRecherche; dist <= distanceMax + 2'000'000 && !place; dist += pasRecherche)
      for (const Nm lateral : {Nm{0}, Nm{500'000}, Nm{-500'000}, Nm{1'000'000}, Nm{-1'000'000}}) {
        if (place) break;
        // Pastille d'alimentation tournée vers la broche, masse vers l'extérieur
        ie2->dessous = dessous;
        double meilleur = -2;
        double rot = 0;
        for (const double r : {0.0, 90.0, 180.0, 270.0}) {
          ie2->rotation = r;
          ie2->position = {};
          const Pt da = versCarte(*ie2, la), dm = versCarte(*ie2, lm);
          const double dx = static_cast<double>(da.x - dm.x), dy = static_cast<double>(da.y - dm.y), l = std::hypot(dx, dy);
          const double alignement = l > 0 ? -(dx * vx + dy * vy) / l : 0;
          if (alignement > meilleur) meilleur = alignement, rot = r;
        }
        ie2->rotation = rot;
        ie2->position = {};
        const Pt cible = arrondi(static_cast<double>(P.x) + vx * static_cast<double>(dist) - vy * static_cast<double>(lateral), static_cast<double>(P.y) + vy * static_cast<double>(dist) + vx * static_cast<double>(lateral));
        ie2->position = cible - versCarte(*ie2, la);
        if (recouvre(c, *ie2) || !dansLaCarte(c, *ie2)) continue;
        place = true;
      }
    if (!place) {
      *ie2 = avant;
      continue;
    }
    servies.insert(broche);
    ++n;
    // Liaison de la broche au condensateur, par le contournement
    OptionsTrace o;
    o.reseau = k2.alim;
    o.couche = dessous ? Couche::CuivreDessous : Couche::CuivreDessus;
    const ResultatTrace t = contourner(c, versCarte(*ie2, la), P, o);
    if (t.ok)
      for (auto& x : pistesDuTrace(t.points, k2.alim, o.couche, largeurPiste(c.circuit.regles, k2.alim))) c.circuit.pistes.push_back(std::move(x));
  }
  return n;
}

bool pousserEmpreintes(ConceptionElec& c, const std::string& comp, Pt position) {
  exigerModifiable(c);
  auto& E = c.circuit.empreintes;
  const auto sauvegarde = E;
  const auto it = std::find_if(E.begin(), E.end(), [&](const EmpreintePlacee& e) { return e.composant == comp; });
  if (it == E.end() || it->verrou) return false;
  const std::size_t depart = static_cast<std::size_t>(it - E.begin());
  E[depart].position = position;
  std::deque<std::size_t> file{depart};
  int tours = 0;
  while (!file.empty()) {
    if (++tours > 600) {
      E = sauvegarde;
      return false;
    }
    const std::size_t i = file.front();
    file.pop_front();
    const DefEmpreinte* di = c.empreinte(E[i].empreinte);
    if (!di) continue;
    for (std::size_t j = 0; j < E.size(); ++j) {
      if (j == i || E[j].dessous != E[i].dessous) continue;
      const DefEmpreinte* dj = c.empreinte(E[j].empreinte);
      if (!dj) continue;
      auto touche = [&] {
        const Polygones commun = intersecter({encombrementSurCarte(E[i], *di)}, {encombrementSurCarte(E[j], *dj)});
        return !commun.empty() && aireMm2(commun) > 1e-6;
      };
      if (!touche()) continue;
      if (E[j].verrou || j == depart) {
        E = sauvegarde;
        return false;
      }
      const Pt ci = boite(encombrementSurCarte(E[i], *di)).centre(), cj = boite(encombrementSurCarte(E[j], *dj)).centre();
      double dx = static_cast<double>(cj.x - ci.x), dy = static_cast<double>(cj.y - ci.y);
      const double l = std::hypot(dx, dy);
      if (l < 1) dx = 1, dy = 0;
      else dx /= l, dy /= l;
      for (int pas = 0; pas < 2000 && touche(); ++pas) E[j].position = E[j].position + arrondi(dx * 50'000, dy * 50'000);
      file.push_back(j);
    }
  }
  return true;
}

// ---------- 5.15, 5.31 · Codes 2D ----------
namespace {
std::uint8_t gfMul(std::uint8_t x, std::uint8_t y) {
  int z = 0;
  for (int i = 7; i >= 0; --i) {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((y >> i) & 1) * x;
  }
  return static_cast<std::uint8_t>(z);
}
// Polynôme générateur de Reed-Solomon de degré n (racines α⁰ … αⁿ⁻¹), coefficients sans le terme dominant
std::vector<std::uint8_t> generateurRS(int n) {
  std::vector<std::uint8_t> r(static_cast<std::size_t>(n), 0);
  r.back() = 1;
  std::uint8_t racine = 1;
  for (int i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < r.size(); ++j) {
      r[j] = gfMul(r[j], racine);
      if (j + 1 < r.size()) r[j] ^= r[j + 1];
    }
    racine = gfMul(racine, 0x02);
  }
  return r;
}
std::vector<std::uint8_t> resteRS(const std::vector<std::uint8_t>& donnees, int n) {
  const auto g = generateurRS(n);
  std::vector<std::uint8_t> r(static_cast<std::size_t>(n), 0);
  for (const std::uint8_t b : donnees) {
    const std::uint8_t f = static_cast<std::uint8_t>(b ^ r.front());
    r.erase(r.begin());
    r.push_back(0);
    for (std::size_t i = 0; i < r.size(); ++i) r[i] ^= gfMul(g[i], f);
  }
  return r;
}
struct Grille {
  int n;
  std::vector<std::vector<bool>> m, fonction;
  explicit Grille(int taille) : n(taille), m(static_cast<std::size_t>(taille), std::vector<bool>(static_cast<std::size_t>(taille))), fonction(m) {}
  void poser(int x, int y, bool v) {
    m[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = v;
    fonction[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = true;
  }
  void reperes(int cx, int cy) {  // motif de position 7×7 et son séparateur
    for (int dy = -4; dy <= 4; ++dy)
      for (int dx = -4; dx <= 4; ++dx) {
        const int x = cx + dx, y = cy + dy, d = std::max(std::abs(dx), std::abs(dy));
        if (x >= 0 && x < n && y >= 0 && y < n) poser(x, y, d != 2 && d != 4);
      }
  }
  // Données en zigzag par paires de colonnes, de la droite vers la gauche (colonne de synchronisation 6 sautée en QR)
  void placer(const std::vector<bool>& bits, bool micro) {
    std::size_t i = 0;
    for (int droite = n - 1; droite >= 1; droite -= 2) {
      if (!micro && droite == 6) droite = 5;
      for (int v = 0; v < n; ++v)
        for (int j = 0; j < 2; ++j) {
          const int x = droite - j;
          const bool montant = micro ? ((n - 1 - droite) / 2) % 2 == 0 : ((droite + 1) & 2) == 0;
          const int y = montant ? n - 1 - v : v;
          if (!fonction[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]) {
            m[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = i < bits.size() && bits[i];
            ++i;
          }
        }
    }
  }
};
void ajouterBits(std::vector<bool>& b, unsigned v, int n) {
  for (int i = n - 1; i >= 0; --i) b.push_back(((v >> i) & 1U) != 0);
}
bool masqueQR(int k, int x, int y) {
  switch (k) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
  }
}
bool masqueMicro(int k, int x, int y) {
  switch (k) {
    case 0: return y % 2 == 0;
    case 1: return (y / 2 + x / 3) % 2 == 0;
    case 2: return ((x * y) % 2 + (x * y) % 3) % 2 == 0;
    default: return ((x + y) % 2 + (x * y) % 3) % 2 == 0;
  }
}
unsigned bch15(unsigned donnees, unsigned masque) {
  unsigned r = donnees;
  for (int i = 0; i < 10; ++i) r = (r << 1) ^ ((r >> 9) * 0x537U);
  return ((donnees << 10) | (r & 0x3FFU)) ^ masque;
}
int penaliteQR(const std::vector<std::vector<bool>>& m) {
  const int n = static_cast<int>(m.size());
  int p = 0;
  auto at = [&](int x, int y) { return static_cast<bool>(m[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]); };
  for (int pass = 0; pass < 2; ++pass)
    for (int a = 0; a < n; ++a) {
      int suite = 1;
      std::vector<bool> ligne;
      for (int b = 0; b < n; ++b) ligne.push_back(pass ? at(a, b) : at(b, a));
      for (int b = 1; b < n; ++b) {
        if (ligne[static_cast<std::size_t>(b)] == ligne[static_cast<std::size_t>(b - 1)]) ++suite;
        else {
          if (suite >= 5) p += 3 + suite - 5;
          suite = 1;
        }
      }
      if (suite >= 5) p += 3 + suite - 5;
      // Motif 1:1:3:1:1 bordé de 4 clairs, hors symbole compté clair
      std::vector<bool> e(4, false);
      e.insert(e.end(), ligne.begin(), ligne.end());
      e.insert(e.end(), 4, false);
      static const bool f1[11] = {true, false, true, true, true, false, true, false, false, false, false};
      static const bool f2[11] = {false, false, false, false, true, false, true, true, true, false, true};
      for (std::size_t s = 0; s + 11 <= e.size(); ++s) {
        bool a1 = true, a2 = true;
        for (std::size_t t = 0; t < 11; ++t) {
          a1 = a1 && e[s + t] == f1[t];
          a2 = a2 && e[s + t] == f2[t];
        }
        p += (a1 ? 40 : 0) + (a2 ? 40 : 0);
      }
    }
  for (int y = 0; y + 1 < n; ++y)
    for (int x = 0; x + 1 < n; ++x)
      if (at(x, y) == at(x + 1, y) && at(x, y) == at(x, y + 1) && at(x, y) == at(x + 1, y + 1)) p += 3;
  int sombres = 0;
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) sombres += at(x, y) ? 1 : 0;
  const int total = n * n;
  const int k = (std::abs(sombres * 20 - total * 10) + total - 1) / total - 1;
  return p + std::max(0, k) * 10;
}
}  // namespace

std::vector<std::vector<bool>> codeQR(const std::string& texte) {
  // Correction M, versions 1 à 10 : mots de données, blocs, mots de correction par bloc, motifs d'alignement
  static const int donnees[10] = {16, 28, 44, 64, 86, 108, 124, 154, 182, 216};
  static const int blocs[10] = {1, 1, 1, 2, 2, 4, 4, 4, 5, 5};
  static const int correction[10] = {10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
  static const std::vector<int> alignement[10] = {{}, {6, 18}, {6, 22}, {6, 26}, {6, 30}, {6, 34}, {6, 22, 38}, {6, 24, 42}, {6, 26, 46}, {6, 28, 50}};
  const int len = static_cast<int>(texte.size());
  int v = 0;
  for (int i = 1; i <= 10 && !v; ++i)
    if (4 + (i <= 9 ? 8 : 16) + 8 * len <= donnees[i - 1] * 8) v = i;
  if (!v) return {};
  // Flux de bits : mode octet, longueur, données, fin, bourrage
  std::vector<bool> b;
  ajouterBits(b, 0b0100, 4);
  ajouterBits(b, static_cast<unsigned>(len), v <= 9 ? 8 : 16);
  for (const char ch : texte) ajouterBits(b, static_cast<unsigned char>(ch), 8);
  const std::size_t capacite = static_cast<std::size_t>(donnees[v - 1]) * 8;
  ajouterBits(b, 0, static_cast<int>(std::min<std::size_t>(4, capacite - b.size())));
  while (b.size() % 8) b.push_back(false);
  for (unsigned bourrage = 0xEC; b.size() < capacite; bourrage ^= 0xEC ^ 0x11) ajouterBits(b, bourrage, 8);
  std::vector<std::uint8_t> octets(capacite / 8, 0);
  for (std::size_t i = 0; i < b.size(); ++i)
    if (b[i]) octets[i / 8] = static_cast<std::uint8_t>(octets[i / 8] | (0x80 >> (i % 8)));
  // Blocs : les courts d'abord, les longs ont un mot de plus ; entrelacement des données puis des corrections
  const int nb = blocs[v - 1], ec = correction[v - 1], total = donnees[v - 1];
  const int courts = nb - total % nb, lc = total / nb;
  std::vector<std::vector<std::uint8_t>> d, e;
  for (int i = 0, pos = 0; i < nb; ++i) {
    const int l = lc + (i >= courts ? 1 : 0);
    d.emplace_back(octets.begin() + pos, octets.begin() + pos + l);
    e.push_back(resteRS(d.back(), ec));
    pos += l;
  }
  std::vector<bool> flux;
  for (int i = 0; i <= lc; ++i)
    for (int j = 0; j < nb; ++j)
      if (static_cast<std::size_t>(i) < d[static_cast<std::size_t>(j)].size()) ajouterBits(flux, d[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)], 8);
  for (int i = 0; i < ec; ++i)
    for (int j = 0; j < nb; ++j) ajouterBits(flux, e[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)], 8);
  // Motifs de fonction
  const int n = 17 + 4 * v;
  Grille g(n);
  for (int i = 0; i < n; ++i) {
    g.poser(6, i, i % 2 == 0);
    g.poser(i, 6, i % 2 == 0);
  }
  g.reperes(3, 3);
  g.reperes(n - 4, 3);
  g.reperes(3, n - 4);
  const auto& al = alignement[v - 1];
  for (std::size_t i = 0; i < al.size(); ++i)
    for (std::size_t j = 0; j < al.size(); ++j) {
      if ((i == 0 && j == 0) || (i == 0 && j + 1 == al.size()) || (i + 1 == al.size() && j == 0)) continue;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) g.poser(al[i] + dx, al[j] + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }
  auto format = [&](Grille& h, int masque) {
    const unsigned bits = bch15((0U << 3) | static_cast<unsigned>(masque), 0x5412);  // M : 00
    auto bt = [&](int i) { return ((bits >> i) & 1U) != 0; };
    for (int i = 0; i <= 5; ++i) h.poser(8, i, bt(i));
    h.poser(8, 7, bt(6));
    h.poser(8, 8, bt(7));
    h.poser(7, 8, bt(8));
    for (int i = 9; i < 15; ++i) h.poser(14 - i, 8, bt(i));
    for (int i = 0; i < 8; ++i) h.poser(n - 1 - i, 8, bt(i));
    for (int i = 8; i < 15; ++i) h.poser(8, n - 15 + i, bt(i));
    h.poser(8, n - 8, true);
  };
  format(g, 0);  // zones réservées
  if (v >= 7) {
    unsigned r = static_cast<unsigned>(v);
    for (int i = 0; i < 12; ++i) r = (r << 1) ^ ((r >> 11) * 0x1F25U);
    const unsigned bits = (static_cast<unsigned>(v) << 12) | (r & 0xFFFU);
    for (int i = 0; i < 18; ++i) {
      const bool bt = ((bits >> i) & 1U) != 0;
      const int a = n - 11 + i % 3, bb = i / 3;
      g.poser(a, bb, bt);
      g.poser(bb, a, bt);
    }
  }
  g.placer(flux, false);
  // Le masque de moindre pénalité
  int meilleur = 0, pMin = -1;
  Grille choisie = g;
  for (int k = 0; k < 8; ++k) {
    Grille h = g;
    for (int y = 0; y < n; ++y)
      for (int x = 0; x < n; ++x)
        if (!h.fonction[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] && masqueQR(k, x, y)) h.m[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = !h.m[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
    format(h, k);
    const int p = penaliteQR(h.m);
    if (pMin < 0 || p < pMin) pMin = p, meilleur = k, choisie = h;
  }
  (void)meilleur;
  return choisie.m;
}

std::vector<std::vector<bool>> codeMicroQR(const std::string& texte) {
  // M2 (numérique, alphanumérique), M3 et M4 (octet aussi), correction L
  static const std::string alnum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
  const bool chiffres = !texte.empty() && std::all_of(texte.begin(), texte.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
  const bool alpha = !texte.empty() && std::all_of(texte.begin(), texte.end(), [](char ch) { return alnum.find(ch) != std::string::npos; });
  struct Version {
    int m, taille, bitsDonnees, motsDonnees, correction, symbole, bitsMode, ccNum, ccAlnum, ccOctet, fin;
  };
  static const Version versions[3] = {{2, 13, 40, 5, 5, 1, 1, 4, 3, 0, 5}, {3, 15, 84, 11, 6, 3, 2, 5, 4, 4, 7}, {4, 17, 128, 16, 8, 5, 3, 6, 5, 5, 9}};
  const int len = static_cast<int>(texte.size());
  for (const Version& V : versions) {
    // Mode : numérique (0), alphanumérique (1), octet (2)
    int mode = chiffres ? 0 : alpha ? 1 : 2;
    if (mode == 2 && V.ccOctet == 0) continue;
    const int cc = mode == 0 ? V.ccNum : mode == 1 ? V.ccAlnum : V.ccOctet;
    if (len >= (1 << cc)) continue;
    std::vector<bool> b;
    ajouterBits(b, static_cast<unsigned>(mode), V.bitsMode);
    ajouterBits(b, static_cast<unsigned>(len), cc);
    if (mode == 0) {
      for (int i = 0; i < len; i += 3) {
        const int k = std::min(3, len - i);
        ajouterBits(b, static_cast<unsigned>(std::stoi(texte.substr(static_cast<std::size_t>(i), static_cast<std::size_t>(k)))), k == 3 ? 10 : k == 2 ? 7 : 4);
      }
    } else if (mode == 1) {
      for (int i = 0; i < len; i += 2) {
        const unsigned a = static_cast<unsigned>(alnum.find(texte[static_cast<std::size_t>(i)]));
        if (i + 1 < len) ajouterBits(b, a * 45 + static_cast<unsigned>(alnum.find(texte[static_cast<std::size_t>(i + 1)])), 11);
        else ajouterBits(b, a, 6);
      }
    } else
      for (const char ch : texte) ajouterBits(b, static_cast<unsigned char>(ch), 8);
    if (static_cast<int>(b.size()) > V.bitsDonnees) continue;
    ajouterBits(b, 0, std::min(V.fin, V.bitsDonnees - static_cast<int>(b.size())));
    // Bourrage : jusqu'à l'octet, puis mots 0xEC / 0x11 ; M3 finit par un mot de 4 bits nul
    const int pleins = V.bitsDonnees / 8 * 8;
    if (static_cast<int>(b.size()) < pleins) {
      while (b.size() % 8) b.push_back(false);
      for (unsigned bourrage = 0xEC; static_cast<int>(b.size()) < pleins; bourrage ^= 0xEC ^ 0x11) ajouterBits(b, bourrage, 8);
    }
    while (static_cast<int>(b.size()) < V.bitsDonnees) b.push_back(false);
    std::vector<std::uint8_t> mots(static_cast<std::size_t>(V.motsDonnees), 0);
    for (std::size_t i = 0; i < b.size(); ++i)
      if (b[i]) mots[i / 8] = static_cast<std::uint8_t>(mots[i / 8] | (0x80 >> (i % 8)));
    const auto ec = resteRS(mots, V.correction);
    std::vector<bool> flux(b.begin(), b.end());
    for (const std::uint8_t x : ec) ajouterBits(flux, x, 8);
    const int n = V.taille;
    Grille g(n);
    g.reperes(3, 3);
    for (int i = 8; i < n; ++i) {
      g.poser(i, 0, i % 2 == 0);
      g.poser(0, i, i % 2 == 0);
    }
    auto format = [&](Grille& h, int masque) {
      const unsigned bits = bch15((static_cast<unsigned>(V.symbole) << 2) | static_cast<unsigned>(masque), 0x4445);
      for (int i = 0; i < 8; ++i) h.poser(8, i + 1, ((bits >> i) & 1U) != 0);
      for (int i = 0; i < 7; ++i) h.poser(i + 1, 8, ((bits >> (14 - i)) & 1U) != 0);
    };
    format(g, 0);
    g.placer(flux, true);
    int meilleur = -1;
    Grille choisie = g;
    for (int k = 0; k < 4; ++k) {
      Grille h = g;
      for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
          if (!h.fonction[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] && masqueMicro(k, x, y)) h.m[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = !h.m[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
      format(h, k);
      int s1 = 0, s2 = 0;
      for (int i = 1; i < n; ++i) {
        s1 += h.m[static_cast<std::size_t>(i)][static_cast<std::size_t>(n - 1)] ? 1 : 0;
        s2 += h.m[static_cast<std::size_t>(n - 1)][static_cast<std::size_t>(i)] ? 1 : 0;
      }
      const int score = s1 <= s2 ? s1 * 16 + s2 : s2 * 16 + s1;
      if (score > meilleur) meilleur = score, choisie = h;
    }
    return choisie.m;
  }
  return {};
}

std::vector<int> code128(const std::string& texte) {
  static const char* motifs[107] = {
      "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312", "132212", "221213", "221312", "231212", "112232", "122132", "122231", "113222",
      "123122", "123221", "223211", "221132", "221231", "213212", "223112", "312131", "311222", "321122", "321221", "312212", "322112", "322211", "212123", "212321",
      "232121", "111323", "131123", "131321", "112313", "132113", "132311", "211313", "231113", "231311", "112133", "112331", "132131", "113123", "113321", "133121",
      "313121", "211331", "231131", "213113", "213311", "213131", "311123", "311321", "331121", "312113", "312311", "332111", "314111", "221411", "431111", "111224",
      "111422", "121124", "121421", "141122", "141221", "112214", "112412", "122114", "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111",
      "111242", "121142", "121241", "114212", "124112", "124211", "411212", "421112", "421211", "212141", "214121", "412121", "111143", "111341", "131141", "114113",
      "114311", "411113", "411311", "113141", "114131", "311141", "411131", "211412", "211214", "211232", "2331112"};
  std::vector<int> valeurs{104};  // départ B
  for (const char ch : texte) {
    const int x = static_cast<unsigned char>(ch);
    if (x < 32 || x > 127) return {};
    valeurs.push_back(x - 32);
  }
  int somme = valeurs.front();
  for (std::size_t i = 1; i < valeurs.size(); ++i) somme += valeurs[i] * static_cast<int>(i);
  valeurs.push_back(somme % 103);
  valeurs.push_back(106);
  std::vector<int> r;
  for (const int v : valeurs)
    for (const char* p = motifs[v]; *p; ++p) r.push_back(*p - '0');
  return r;
}

Polygones modulesEnPolygones(const std::vector<std::vector<bool>>& m, Pt o, Nm pas) {
  Polygones carres;
  for (std::size_t y = 0; y < m.size(); ++y)
    for (std::size_t x = 0; x < m[y].size();) {
      if (!m[y][x]) {
        ++x;
        continue;
      }
      std::size_t fin = x;
      while (fin < m[y].size() && m[y][fin]) ++fin;
      const Nm x0 = o.x + static_cast<Nm>(x) * pas, x1 = o.x + static_cast<Nm>(fin) * pas, y0 = o.y + static_cast<Nm>(y) * pas, y1 = y0 + pas;
      carres.push_back(orienter({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}));
      x = fin;
    }
  return carres.empty() ? carres : unir(carres);
}

Polygones barresEnPolygones(const std::vector<int>& largeurs, Pt o, Nm module, Nm hauteur) {
  Polygones r;
  Nm x = o.x;
  for (std::size_t i = 0; i < largeurs.size(); ++i) {
    const Nm l = static_cast<Nm>(largeurs[i]) * module;
    if (i % 2 == 0) r.push_back(orienter({{x, o.y}, {x + l, o.y}, {x + l, o.y + hauteur}, {x, o.y + hauteur}}));
    x += l;
  }
  return r;
}

namespace {
// Douglas-Peucker sur une boucle fermée
std::vector<std::pair<double, double>> simplifier(const std::vector<std::pair<double, double>>& p, double tol) {
  if (p.size() < 8) return p;
  std::vector<bool> garde(p.size(), false);
  std::function<void(std::size_t, std::size_t)> dp = [&](std::size_t i, std::size_t j) {
    double dmax = -1;
    std::size_t k = i;
    const auto [ax, ay] = p[i];
    const auto [bx, by] = p[j % p.size()];
    const double l = std::hypot(bx - ax, by - ay);
    for (std::size_t t = i + 1; t < j; ++t) {
      const auto [px, py] = p[t];
      const double d = l < 1e-12 ? std::hypot(px - ax, py - ay) : std::abs((bx - ax) * (ay - py) - (ax - px) * (by - ay)) / l;
      if (d > dmax) dmax = d, k = t;
    }
    if (dmax > tol) {
      garde[k] = true;
      dp(i, k);
      dp(k, j);
    }
  };
  // Deux points de départ opposés sur la boucle
  const std::size_t moitie = p.size() / 2;
  garde[0] = garde[moitie] = true;
  dp(0, moitie);
  dp(moitie, p.size());
  std::vector<std::pair<double, double>> r;
  for (std::size_t i = 0; i < p.size(); ++i)
    if (garde[i]) r.push_back(p[i]);
  return r;
}
}  // namespace

Polygones logoDepuisImage(const std::vector<std::uint8_t>& gris, int w, int h, int seuil, double mmParPixel, Pt o, bool vectoriser) {
  if (w <= 0 || h <= 0 || gris.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) return {};
  auto val = [&](int x, int y) -> double { return x < 0 || y < 0 || x >= w || y >= h ? 255.0 : static_cast<double>(gris[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)]); };
  const double px = mmParPixel * 1e6;
  auto versCarte = [&](double x, double y) { return arrondi(static_cast<double>(o.x) + x * px, static_cast<double>(o.y) + y * px); };
  if (!vectoriser) {
    Polygones carres;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w;) {
        if (val(x, y) >= seuil) {
          ++x;
          continue;
        }
        int fin = x;
        while (fin < w && val(fin, y) < seuil) ++fin;
        carres.push_back(orienter({versCarte(x, y), versCarte(fin, y), versCarte(fin, y + 1), versCarte(x, y + 1)}));
        x = fin;
      }
    return carres.empty() ? carres : unir(carres);
  }
  // Carrés marchants : les échantillons sont les centres des pixels, la courbe passe au seuil par interpolation
  const double s = static_cast<double>(seuil);
  // Arêtes : horizontale (x, y)→(x+1, y) clé 2·id, verticale (x, y)→(x, y+1) clé 2·id+1 ; grille de −1 à w, −1 à h
  const int W = w + 2;
  auto id = [&](int x, int y) { return static_cast<long long>(y + 1) * W + (x + 1); };
  auto point = [&](long long cle) {
    const long long base = cle / 2;
    const int x = static_cast<int>(base % W) - 1, y = static_cast<int>(base / W) - 1;
    const bool horiz = cle % 2 == 0;
    const double va = val(x, y), vb = horiz ? val(x + 1, y) : val(x, y + 1);
    const double t = std::abs(vb - va) < 1e-9 ? 0.5 : std::clamp((s - va) / (vb - va), 0.0, 1.0);
    return horiz ? std::pair{x + 0.5 + t, y + 0.5} : std::pair{x + 0.5, y + 0.5 + t};
  };
  std::map<long long, long long> suivant;  // arête d'entrée → arête de sortie, matière (sombre) à gauche
  for (int y = -1; y < h; ++y)
    for (int x = -1; x < w; ++x) {
      const bool a = val(x, y) < s, b = val(x + 1, y) < s, cc = val(x + 1, y + 1) < s, d = val(x, y + 1) < s;
      const int cas = (a ? 1 : 0) | (b ? 2 : 0) | (cc ? 4 : 0) | (d ? 8 : 0);
      const long long haut = 2 * id(x, y), bas = 2 * id(x, y + 1), gauche = 2 * id(x, y) + 1, droite = 2 * id(x + 1, y) + 1;
      auto lier = [&](long long de, long long vers) { suivant[de] = vers; };
      const bool centre = (val(x, y) + val(x + 1, y) + val(x + 1, y + 1) + val(x, y + 1)) / 4 < s;
      switch (cas) {
        case 1: lier(gauche, haut); break;
        case 2: lier(haut, droite); break;
        case 3: lier(gauche, droite); break;
        case 4: lier(droite, bas); break;
        case 5:
          if (centre) lier(gauche, bas), lier(droite, haut);
          else lier(gauche, haut), lier(droite, bas);
          break;
        case 6: lier(haut, bas); break;
        case 7: lier(gauche, bas); break;
        case 8: lier(bas, gauche); break;
        case 9: lier(bas, haut); break;
        case 10:
          if (centre) lier(haut, gauche), lier(bas, droite);
          else lier(haut, droite), lier(bas, gauche);
          break;
        case 11: lier(bas, droite); break;
        case 12: lier(droite, gauche); break;
        case 13: lier(droite, haut); break;
        case 14: lier(haut, gauche); break;
        default: break;
      }
    }
  std::vector<Polygone> boucles;
  std::set<long long> vues;
  for (const auto& [depart, _] : suivant) {
    if (vues.count(depart)) continue;
    std::vector<std::pair<double, double>> pts;
    long long e = depart;
    while (!vues.count(e)) {
      vues.insert(e);
      pts.push_back(point(e));
      const auto it = suivant.find(e);
      if (it == suivant.end()) break;
      e = it->second;
    }
    if (pts.size() < 3) continue;
    Polygone p;
    for (const auto& [x, y] : simplifier(pts, 0.2)) p.push_back(versCarte(x, y));
    if (p.size() >= 3) boucles.push_back(p);
  }
  // Emboîtement : de la plus grande à la plus petite, chaque boucle ajoute ou retire selon son sens
  std::sort(boucles.begin(), boucles.end(), [](const Polygone& a, const Polygone& b) { return aireMm2(a) > aireMm2(b); });
  Polygones r;
  const double sensMatiere = boucles.empty() ? 1 : aireSignee(boucles.front());
  for (const auto& b : boucles) {
    const bool plein = aireSignee(b) * sensMatiere > 0;
    r = plein ? unir([&] { Polygones t = r; t.push_back(orienter(b)); return t; }()) : soustraire(r, {orienter(b)});
  }
  return r;
}

std::vector<std::string> poserMotifGraphique(ConceptionElec& c, const Polygones& motif, Couche couche, const std::string& reseau) {
  exigerModifiable(c);
  std::vector<std::string> r;
  for (const Polygone& p : pieces(motif)) {
    if (p.size() < 3) continue;
    if (estCuivre(couche)) {
      RegionCuivre g;
      g.uuid = nouvelUuid();
      g.reseau = reseau;
      g.genre = "logo";
      g.couche = couche;
      g.forme = p;
      r.push_back(g.uuid);
      c.circuit.regions.push_back(std::move(g));
    } else {
      DessinCircuit d;
      d.uuid = nouvelUuid();
      d.forme = DessinCircuit::Forme::Polygone;
      d.couche = couche;
      d.points = p;
      d.plein = true;
      d.epaisseur = 0;
      r.push_back(d.uuid);
      c.circuit.dessins.push_back(std::move(d));
    }
  }
  return r;
}

// ---------- 5.15, 5.31 · Cotes ----------
GeometrieCote geometrieCote(const Cote& c) {
  GeometrieCote g;
  g.valeur = texteCote(c);
  if (c.points.empty()) return g;
  const Pt a = c.points.front(), b = c.points.size() > 1 ? c.points[1] : c.points.front();
  const double d = static_cast<double>(c.decalage), ht = static_cast<double>(c.hauteurTexte);
  auto P = [](double x, double y) { return arrondi(x, y); };
  auto fleche = [&](Pt bout, double ux, double uy) {  // pointe en « bout », dirigée selon u
    const double l = 0.8e6, ang = 0.4;
    for (const double s : {ang, -ang}) {
      const double cx = ux * std::cos(s) - uy * std::sin(s), cy = ux * std::sin(s) + uy * std::cos(s);
      g.traits.push_back({bout, P(static_cast<double>(bout.x) - cx * l, static_cast<double>(bout.y) - cy * l)});
    }
  };
  auto ligneCote = [&](Pt p1, Pt p2, Pt q1, Pt q2) {  // attaches p→q, ligne de cote q1–q2, flèches, texte au milieu
    const double ex = static_cast<double>(q1.x - p1.x), ey = static_cast<double>(q1.y - p1.y), el = std::hypot(ex, ey);
    const double dep = 0.6e6;
    if (el > 1) {
      g.traits.push_back({p1, P(static_cast<double>(q1.x) + ex / el * dep, static_cast<double>(q1.y) + ey / el * dep)});
      g.traits.push_back({p2, P(static_cast<double>(q2.x) + ex / el * dep, static_cast<double>(q2.y) + ey / el * dep)});
    }
    g.traits.push_back({q1, q2});
    const double L = distance(q1, q2);
    if (L > 1) {
      const double ux = static_cast<double>(q2.x - q1.x) / L, uy = static_cast<double>(q2.y - q1.y) / L;
      fleche(q2, ux, uy);
      fleche(q1, -ux, -uy);
      double ang = std::atan2(uy, ux) * 180 / std::numbers::pi;
      if (ang > 90.5) ang -= 180;  // texte lisible, jamais à l'envers
      if (ang < -89.5) ang += 180;
      g.angle = -ang;
      // Texte au-dessus de la ligne (côté opposé aux points mesurés)
      const double nx = el > 1 ? ex / el : -uy, ny = el > 1 ? ey / el : ux;
      g.texte = P((static_cast<double>(q1.x) + static_cast<double>(q2.x)) / 2 + nx * ht * 0.8, (static_cast<double>(q1.y) + static_cast<double>(q2.y)) / 2 + ny * ht * 0.8);
    }
  };
  switch (c.genre) {
    case GenreCote::Alignee: {
      const double L = distance(a, b);
      if (L < 1) break;
      const double nx = -static_cast<double>(b.y - a.y) / L, ny = static_cast<double>(b.x - a.x) / L;
      ligneCote(a, b, P(static_cast<double>(a.x) + nx * d, static_cast<double>(a.y) + ny * d), P(static_cast<double>(b.x) + nx * d, static_cast<double>(b.y) + ny * d));
      break;
    }
    case GenreCote::Horizontale: {
      const Nm y = std::min(a.y, b.y) - c.decalage;
      ligneCote(a, b, {a.x, y}, {b.x, y});
      break;
    }
    case GenreCote::Verticale: {
      const Nm x = std::max(a.x, b.x) + c.decalage;
      ligneCote(a, b, {x, a.y}, {x, b.y});
      break;
    }
    case GenreCote::Rayon:
    case GenreCote::Diametre: {
      const double L = distance(a, b);
      if (L < 1) break;
      const double ux = static_cast<double>(b.x - a.x) / L, uy = static_cast<double>(b.y - a.y) / L;
      const Pt debut = c.genre == GenreCote::Diametre ? P(static_cast<double>(a.x) - ux * L, static_cast<double>(a.y) - uy * L) : a;
      const Pt fin = P(static_cast<double>(b.x) + ux * d, static_cast<double>(b.y) + uy * d);
      g.traits.push_back({debut, fin});
      fleche(b, ux, uy);
      if (c.genre == GenreCote::Diametre) fleche(debut, -ux, -uy);
      g.texte = P(static_cast<double>(fin.x) + ux * ht, static_cast<double>(fin.y) + uy * ht);
      break;
    }
    case GenreCote::Ordonnee: {
      // Ordonnée : le point repéré, une attache vers la ligne de cote décalée, la distance à l'origine (premier point)
      const Pt bout{b.x, b.y - c.decalage};
      g.traits.push_back({b, bout});
      g.texte = {b.x, bout.y - c.hauteurTexte};
      g.angle = -90;
      break;
    }
    case GenreCote::Angulaire: {
      if (c.points.size() < 3) break;
      const Pt o = c.points[0], p1 = c.points[1], p2 = c.points[2];
      double t1 = std::atan2(static_cast<double>(p1.y - o.y), static_cast<double>(p1.x - o.x));
      double t2 = std::atan2(static_cast<double>(p2.y - o.y), static_cast<double>(p2.x - o.x));
      double dt = t2 - t1;
      while (dt > std::numbers::pi) dt -= 2 * std::numbers::pi;
      while (dt < -std::numbers::pi) dt += 2 * std::numbers::pi;
      const double r = std::max(d, 1e6);
      Pt prec = P(static_cast<double>(o.x) + r * std::cos(t1), static_cast<double>(o.y) + r * std::sin(t1));
      for (int i = 1; i <= 24; ++i) {
        const double t = t1 + dt * i / 24;
        const Pt q = P(static_cast<double>(o.x) + r * std::cos(t), static_cast<double>(o.y) + r * std::sin(t));
        g.traits.push_back({prec, q});
        prec = q;
      }
      g.traits.push_back({o, P(static_cast<double>(o.x) + (r + 0.6e6) * std::cos(t1), static_cast<double>(o.y) + (r + 0.6e6) * std::sin(t1))});
      g.traits.push_back({o, P(static_cast<double>(o.x) + (r + 0.6e6) * std::cos(t2), static_cast<double>(o.y) + (r + 0.6e6) * std::sin(t2))});
      const double tm = t1 + dt / 2;
      g.texte = P(static_cast<double>(o.x) + (r + ht) * std::cos(tm), static_cast<double>(o.y) + (r + ht) * std::sin(tm));
      break;
    }
    case GenreCote::Centre: {
      const double r = c.points.size() > 1 ? distance(a, b) : 1e6;
      const double l = std::max(r * 0.3, 0.5e6);
      g.traits.push_back({P(static_cast<double>(a.x) - l, static_cast<double>(a.y)), P(static_cast<double>(a.x) + l, static_cast<double>(a.y))});
      g.traits.push_back({P(static_cast<double>(a.x), static_cast<double>(a.y) - l), P(static_cast<double>(a.x), static_cast<double>(a.y) + l)});
      break;
    }
  }
  return g;
}

std::string poserCote(ConceptionElec& c, GenreCote genre, const std::vector<Pt>& points, Nm decalage) {
  exigerModifiable(c);
  Cote x;
  x.uuid = nouvelUuid();
  x.genre = genre;
  x.points = points;
  x.decalage = decalage;
  c.circuit.cotes.push_back(x);
  return x.uuid;
}

// ---------- 5.15 · Gabarits de carte ----------
const std::vector<GabaritCarte>& gabaritsCarte() {
  static const std::vector<GabaritCarte> g = [] {
    std::vector<GabaritCarte> r;
    auto rect = [](double l, double h, double rayon) {
      return rayon > 0 ? rectangleArrondi(ptMm(l / 2, h / 2), nm(l), nm(h), nm(rayon)) : rectangle(ptMm(l / 2, h / 2), nm(l), nm(h));
    };
    r.push_back({"Shield Arduino Uno", "68,58 × 53,34 mm, quatre trous de 3,2 mm aux places de la carte Uno R3", rect(68.58, 53.34, 0),
                 {ptMm(13.97, 50.8), ptMm(15.24, 2.54), ptMm(66.04, 45.72), ptMm(66.04, 17.78)}, nm(3.2)});
    r.push_back({"HAT Raspberry Pi", "65 × 56,5 mm, coins de 3 mm, quatre trous de 2,75 mm à 3,5 mm des bords", rect(65, 56.5, 3),
                 {ptMm(3.5, 3.5), ptMm(61.5, 3.5), ptMm(3.5, 52.5), ptMm(61.5, 52.5)}, nm(2.75)});
    r.push_back({"pHAT Raspberry Pi (Zero)", "65 × 30 mm, coins de 3 mm, quatre trous de 2,75 mm à 3,5 mm des bords", rect(65, 30, 3),
                 {ptMm(3.5, 3.5), ptMm(61.5, 3.5), ptMm(3.5, 26.5), ptMm(61.5, 26.5)}, nm(2.75)});
    r.push_back({"Eurocarte", "160 × 100 mm (DIN 41494), sans trou : la carte glisse dans les rails", rect(160, 100, 0), {}, nm(3.2)});
    r.push_back({"Demi-eurocarte", "80 × 100 mm (DIN 41494)", rect(80, 100, 0), {}, nm(3.2)});
    r.push_back({"Carte 50 × 50 mm", "Quatre trous M3 à 4 mm des bords, coins de 2 mm", rect(50, 50, 2), {ptMm(4, 4), ptMm(46, 4), ptMm(4, 46), ptMm(46, 46)}, nm(3.2)});
    r.push_back({"Carte 100 × 100 mm", "Format d'essai courant des fabricants ; quatre trous M3 à 4 mm des bords, coins de 2 mm", rect(100, 100, 2),
                 {ptMm(4, 4), ptMm(96, 4), ptMm(4, 96), ptMm(96, 96)}, nm(3.2)});
    return r;
  }();
  return g;
}

void appliquerGabaritCarte(ConceptionElec& c, const GabaritCarte& g) {
  exigerModifiable(c);
  const Pt o = c.circuit.contour.origine;
  Polygone p;
  for (const Pt q : g.contour) p.push_back(q + o);
  c.circuit.contour.libre = orienter(p);
  const Boite b = boite(p);
  c.circuit.contour.largeur = b.largeur();
  c.circuit.contour.hauteur = b.hauteur();
  char id[48];
  std::snprintf(id, sizeof id, "MountingHole_%g", mm(g.diametreTrous));
  for (const Pt t : g.trous) poserEmpreinteSeule(c, id, t + o, 0, false, "H");
}

}  // namespace prom::ecad
