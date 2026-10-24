// Prométhée : routage automatique par stratégies et optimiseur d'échange de broches (8.28). Voir routage_auto.hpp.
// Licence GPL-3.0-only.
#include "promethee/routage_auto.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>

#include "promethee/routeur.hpp"
#include "promethee/schema_avance.hpp"

namespace prom::ecad {

// ---------- Stratégies ----------
nlohmann::ordered_json strategieVersJson(const StrategieRoutage& s) {
  nlohmann::ordered_json o{{"nom", s.nom}, {"passes", s.passes}, {"courtesDabord", s.courtesDabord}, {"couches", s.couches}, {"coutVia", s.coutVia}, {"portee", s.portee}, {"reseaux", s.reseaux}, {"vias", s.vias}};
  nlohmann::ordered_json d = nlohmann::ordered_json::object();
  for (const auto& [k, v] : s.directions) d[k] = std::string(1, v);
  o["directions"] = d;
  return o;
}

StrategieRoutage strategieDepuisJson(const nlohmann::ordered_json& o) {
  StrategieRoutage s;
  s.nom = o.value("nom", s.nom);
  s.passes = o.value("passes", s.passes);
  s.courtesDabord = o.value("courtesDabord", s.courtesDabord);
  if (o.contains("couches"))
    for (const auto& x : o["couches"]) s.couches.push_back(x.get<std::string>());
  s.coutVia = o.value("coutVia", s.coutVia);
  s.portee = o.value("portee", s.portee);
  s.reseaux = o.value("reseaux", s.reseaux);
  s.vias = o.value("vias", s.vias);
  if (o.contains("directions"))
    for (auto it = o["directions"].begin(); it != o["directions"].end(); ++it)
      if (it.value().is_string() && !it.value().get<std::string>().empty()) s.directions[it.key()] = it.value().get<std::string>()[0];
  return s;
}

std::vector<StrategieRoutage> strategiesRoutage(const ConceptionElec& c) {
  std::vector<StrategieRoutage> r;
  if (const auto it = c.extensions.find("strategiesRoutage"); it != c.extensions.end() && it->second.is_array())
    for (const auto& o : it->second) r.push_back(strategieDepuisJson(o));
  if (!r.empty()) return r;
  // Deux stratégies d'office : courtes liaisons d'abord, dessus horizontal ; longues d'abord, dessus vertical
  StrategieRoutage a;
  a.nom = "Courtes d'abord, dessus horizontal";
  a.directions = {{"F.Cu", 'H'}, {"B.Cu", 'V'}};
  StrategieRoutage b;
  b.nom = "Longues d'abord, dessus vertical";
  b.courtesDabord = false;
  b.directions = {{"F.Cu", 'V'}, {"B.Cu", 'H'}};
  b.passes = 3;
  return {a, b};
}

void enregistrerStrategies(ConceptionElec& c, const std::vector<StrategieRoutage>& s) {
  exigerModifiable(c);
  nlohmann::ordered_json t = nlohmann::ordered_json::array();
  for (const auto& x : s) t.push_back(strategieVersJson(x));
  c.extensions["strategiesRoutage"] = t;
}

// ---------- Routeur pas à pas ----------
RouteurAuto::RouteurAuto(const ConceptionElec& c, StrategieRoutage s) : m_c(c), m_s(std::move(s)) {
  m_c.verrouillee = false;
  m_pistesAvant = m_c.circuit.pistes.size();
  // Pastilles et réseaux ne changent pas pendant le routage : calculés une fois
  m_pp = pastilles(m_c, connectivite(m_c));
  preparerPasse();
}

void RouteurAuto::preparerPasse() {
  const Connectivite k = connectivite(m_c);
  m_file = etatRoutage(m_c, k, remplirZones(m_c, k)).chevelu;
  std::erase_if(m_file, [&](const Liaison& l) {
    if (!m_s.reseaux.empty() && !correspond(m_s.reseaux, l.reseau)) return true;
    return m_s.portee > 0 && distance(l.a, l.b) > m_s.portee * 1e6;
  });
  std::sort(m_file.begin(), m_file.end(), [&](const Liaison& a, const Liaison& b) {
    const double da = distance(a.a, a.b), db = distance(b.a, b.b);
    return m_s.courtesDabord ? da < db : da > db;
  });
  m_indice = 0;
  m_poseesPasse = 0;
  if (m_file.empty()) m_termine = true;
}

int RouteurAuto::avancer(int n) {
  int faits = 0;
  while (!m_termine && faits < n) {
    if (m_indice >= m_file.size()) {
      ++m_passe;
      if (m_passe >= m_s.passes || m_poseesPasse == 0) {
        m_termine = true;
        break;
      }
      preparerPasse();
      continue;
    }
    const Liaison l = m_file[m_indice++];
    if (router(l)) {
      ++m_routees;
      ++m_poseesPasse;
    }
    ++faits;
  }
  return faits;
}

bool RouteurAuto::router(const Liaison& l) {
  const auto& pp = m_pp;
  const PastillePlacee *pa = nullptr, *pb = nullptr;
  for (const auto& p : pp) {
    if (p.reseau != l.reseau) continue;
    if (p.pastille.position == l.a) pa = &p;
    if (p.pastille.position == l.b) pb = &p;
  }
  if (!pa || !pb) return false;
  // Couches dans l'ordre de la stratégie ; une direction contraire à la liaison passe en dernier
  std::vector<Couche> couches;
  const auto toutes = couchesCuivre(m_c.circuit.empilage.couchesCuivre);
  if (m_s.couches.empty()) couches = toutes;
  else
    for (const auto& code : m_s.couches)
      if (const auto x = coucheDepuisCode(code); x && std::find(toutes.begin(), toutes.end(), *x) != toutes.end()) couches.push_back(*x);
  const bool horizontale = std::abs(l.b.x - l.a.x) >= std::abs(l.b.y - l.a.y);
  std::stable_sort(couches.begin(), couches.end(), [&](Couche x, Couche y) {
    auto contraire = [&](Couche z) {
      const auto it = m_s.directions.find(codeCouche(z));
      return it != m_s.directions.end() && ((it->second == 'H') != horizontale);
    };
    return !contraire(x) && contraire(y);
  });
  const Nm largeur = largeurPiste(m_c.circuit.regles, l.reseau);
  auto options = [&](Couche L) {
    OptionsTrace o;
    o.reseau = l.reseau;
    o.couche = L;
    o.largeur = largeur;
    o.etendue = 15 * NM_PAR_MM;
    o.pastilles = &pp;
    return o;
  };
  // Tracé sur une couche : droit ou en L s'il est libre, sinon contourné
  auto tracer = [&](Pt a, Pt b, Couche L) -> std::vector<Pt> {
    const OptionsTrace o = options(L);
    if (segmentLibre(m_c, a, b, o)) return {a, b};
    for (const Pt coude : {Pt{b.x, a.y}, Pt{a.x, b.y}})
      if (segmentLibre(m_c, a, coude, o) && segmentLibre(m_c, coude, b, o)) return {a, coude, b};
    const ResultatTrace r = contourner(m_c, a, b, o);
    return r.ok ? r.points : std::vector<Pt>{};
  };
  for (const Couche L : couches) {
    if (!(pa->couches & bit(L)) || !(pb->couches & bit(L))) continue;
    const auto pts = tracer(l.a, l.b, L);
    if (pts.size() >= 2) {
      for (const auto& p : pistesDuTrace(pts, l.reseau, L, largeur)) m_c.circuit.pistes.push_back(p);
      return true;
    }
  }
  if (!m_s.vias || toutes.size() < 2) return false;
  // Par une via : un tronçon sur une couche de chaque pastille, la via sur la ligne qui les joint
  const Nm diametre = viaDiametre(m_c.circuit.regles, l.reseau), percage = viaPercage(m_c.circuit.regles, l.reseau);
  const double d = distance(l.a, l.b);
  if (d < 1e-6) return false;
  std::vector<Pt> candidats;
  for (const double u : {0.5, 0.25, 0.75})
    candidats.push_back({l.a.x + static_cast<Nm>(std::llround(static_cast<double>(l.b.x - l.a.x) * u)), l.a.y + static_cast<Nm>(std::llround(static_cast<double>(l.b.y - l.a.y) * u))});
  for (const double recul : {1.5e6, 3e6})
    if (recul < d) {
      candidats.push_back({l.a.x + static_cast<Nm>(std::llround(static_cast<double>(l.b.x - l.a.x) * recul / d)), l.a.y + static_cast<Nm>(std::llround(static_cast<double>(l.b.y - l.a.y) * recul / d))});
      candidats.push_back({l.b.x - static_cast<Nm>(std::llround(static_cast<double>(l.b.x - l.a.x) * recul / d)), l.b.y - static_cast<Nm>(std::llround(static_cast<double>(l.b.y - l.a.y) * recul / d))});
    }
  for (const Couche La : couches) {
    if (!(pa->couches & bit(La))) continue;
    for (const Couche Lb : couches) {
      if (Lb == La || !(pb->couches & bit(Lb))) continue;
      for (const Pt v : candidats) {
        // La via tient à l'isolement sur toutes les couches qu'elle traverse
        bool libre = true;
        for (const Couche L : toutes) {
          OptionsTrace o = options(L);
          o.largeur = diametre;
          libre = libre && segmentLibre(m_c, v, v, o);
        }
        if (!libre) continue;
        const auto t1 = tracer(l.a, v, La);
        if (t1.size() < 2) continue;
        const auto t2 = tracer(v, l.b, Lb);
        if (t2.size() < 2) continue;
        for (const auto& p : pistesDuTrace(t1, l.reseau, La, largeur)) m_c.circuit.pistes.push_back(p);
        for (const auto& p : pistesDuTrace(t2, l.reseau, Lb, largeur)) m_c.circuit.pistes.push_back(p);
        Via via;
        via.uuid = nouvelUuid();
        via.reseau = l.reseau;
        via.p = v;
        via.diametre = diametre;
        via.percage = percage;
        m_c.circuit.vias.push_back(via);
        ++m_vias;
        return true;
      }
    }
  }
  return false;
}

int RouteurAuto::restantes() const {
  const Connectivite k = connectivite(m_c);
  return static_cast<int>(etatRoutage(m_c, k, remplirZones(m_c, k)).chevelu.size());
}

double RouteurAuto::longueur() const {
  double l = 0;
  for (std::size_t i = m_pistesAvant; i < m_c.circuit.pistes.size(); ++i) l += longueurPiste(m_c.circuit.pistes[i]);
  return l * 1e-6;
}

EssaiRoutage essayerStrategie(const ConceptionElec& c, const StrategieRoutage& s) {
  const auto t0 = std::chrono::steady_clock::now();
  EssaiRoutage e;
  e.strategie = s.nom;
  {
    const Connectivite k = connectivite(c);
    e.liaisons = static_cast<int>(etatRoutage(c, k, remplirZones(c, k)).chevelu.size());
  }
  RouteurAuto r(c, s);
  while (!r.termine()) r.avancer(64);
  e.vias = r.vias();
  e.longueur = r.longueur();
  e.routees = e.liaisons - r.restantes();
  e.duree = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  e.score = e.routees * 1e6 - e.longueur - s.coutVia * e.vias;
  e.resultat = r.resultat();
  e.resultat.verrouillee = c.verrouillee;
  return e;
}

std::vector<EssaiRoutage> comparerStrategies(const ConceptionElec& c, const std::vector<StrategieRoutage>& s) {
  std::vector<EssaiRoutage> r;
  for (const auto& x : s) r.push_back(essayerStrategie(c, x));
  std::stable_sort(r.begin(), r.end(), [](const EssaiRoutage& a, const EssaiRoutage& b) { return a.score > b.score; });
  return r;
}

int appliquerEssai(ConceptionElec& c, const EssaiRoutage& e) {
  exigerModifiable(c);
  // Les pistes et vias de l'essai qui n'existent pas encore ; rien n'est retiré
  std::set<std::string> pistes, vias;
  for (const auto& p : c.circuit.pistes) pistes.insert(p.uuid);
  for (const auto& v : c.circuit.vias) vias.insert(v.uuid);
  int n = 0;
  for (const auto& p : e.resultat.circuit.pistes)
    if (!pistes.count(p.uuid)) {
      c.circuit.pistes.push_back(p);
      ++n;
    }
  for (const auto& v : e.resultat.circuit.vias)
    if (!vias.count(v.uuid)) {
      c.circuit.vias.push_back(v);
      ++n;
    }
  return n;
}

std::vector<std::string> rapportPreparation(const ConceptionElec& c) {
  std::vector<std::string> r;
  const Connectivite k = connectivite(c);
  const EtatRoutage e = etatRoutage(c, k, remplirZones(c, k));
  std::set<std::string> reseaux;
  for (const auto& l : e.chevelu) reseaux.insert(l.reseau);
  r.push_back(std::to_string(e.chevelu.size()) + " liaison(s) à router sur " + std::to_string(reseaux.size()) + " réseau(x), " + std::to_string(c.circuit.empilage.couchesCuivre) + " couche(s) de cuivre");
  const auto pp = pastilles(c, k);
  int sansCommune = 0;
  for (const auto& l : e.chevelu) {
    Couches ca = 0, cb = 0;
    for (const auto& p : pp) {
      if (p.reseau != l.reseau) continue;
      if (p.pastille.position == l.a) ca = p.couches;
      if (p.pastille.position == l.b) cb = p.couches;
    }
    if (ca && cb && !(ca & cb)) ++sansCommune;
  }
  if (sansCommune) r.push_back(std::to_string(sansCommune) + " liaison(s) sans couche commune : une via par liaison");
  const Polygone carte = c.circuit.contour.polygone();
  int dehors = 0;
  for (const auto& p : pp) dehors += !dedans(carte, p.pastille.position);
  if (dehors) r.push_back(std::to_string(dehors) + " pastille(s) hors de la carte : à placer d'abord");
  int verrouillees = 0;
  for (const auto& t : c.circuit.pistes) verrouillees += t.verrou != Verrou::Aucun;
  r.push_back(std::to_string(c.circuit.pistes.size()) + " piste(s) déjà posée(s), gardées telles quelles (" + std::to_string(verrouillees) + " verrouillée(s))");
  return r;
}

// ---------- Algorithme hongrois (Kuhn et Munkres, potentiels) ----------
std::vector<int> affectationHongroise(const std::vector<std::vector<double>>& a) {
  const int n = static_cast<int>(a.size());
  if (n == 0) return {};
  const double INF = std::numeric_limits<double>::infinity();
  std::vector<double> u(static_cast<std::size_t>(n + 1)), v(static_cast<std::size_t>(n + 1));
  std::vector<int> p(static_cast<std::size_t>(n + 1)), voie(static_cast<std::size_t>(n + 1));
  for (int i = 1; i <= n; ++i) {
    p[0] = i;
    int j0 = 0;
    std::vector<double> mini(static_cast<std::size_t>(n + 1), INF);
    std::vector<char> utilise(static_cast<std::size_t>(n + 1), 0);
    do {
      utilise[static_cast<std::size_t>(j0)] = 1;
      const int i0 = p[static_cast<std::size_t>(j0)];
      double delta = INF;
      int j1 = 0;
      for (int j = 1; j <= n; ++j)
        if (!utilise[static_cast<std::size_t>(j)]) {
          const double cur = a[static_cast<std::size_t>(i0 - 1)][static_cast<std::size_t>(j - 1)] - u[static_cast<std::size_t>(i0)] - v[static_cast<std::size_t>(j)];
          if (cur < mini[static_cast<std::size_t>(j)]) {
            mini[static_cast<std::size_t>(j)] = cur;
            voie[static_cast<std::size_t>(j)] = j0;
          }
          if (mini[static_cast<std::size_t>(j)] < delta) {
            delta = mini[static_cast<std::size_t>(j)];
            j1 = j;
          }
        }
      for (int j = 0; j <= n; ++j)
        if (utilise[static_cast<std::size_t>(j)]) {
          u[static_cast<std::size_t>(p[static_cast<std::size_t>(j)])] += delta;
          v[static_cast<std::size_t>(j)] -= delta;
        } else
          mini[static_cast<std::size_t>(j)] -= delta;
      j0 = j1;
    } while (p[static_cast<std::size_t>(j0)] != 0);
    do {
      const int j1 = voie[static_cast<std::size_t>(j0)];
      p[static_cast<std::size_t>(j0)] = p[static_cast<std::size_t>(j1)];
      j0 = j1;
    } while (j0);
  }
  std::vector<int> r(static_cast<std::size_t>(n), -1);
  for (int j = 1; j <= n; ++j)
    if (p[static_cast<std::size_t>(j)] > 0) r[static_cast<std::size_t>(p[static_cast<std::size_t>(j)] - 1)] = j - 1;
  return r;
}

std::vector<EchangePropose> optimiserEchanges(const ConceptionElec& c) {
  std::vector<EchangePropose> r;
  const Connectivite k = connectivite(c);
  const auto pp = pastilles(c, k);
  // Position des pastilles par (composant, numéro) ; centre des autres broches de chaque réseau
  std::map<std::pair<std::string, std::string>, Pt> position;
  std::map<std::string, std::vector<std::pair<std::string, Pt>>> parReseau;  // réseau → (composant, position)
  for (const auto& p : pp) {
    position[{p.composant, p.numero}] = p.pastille.position;
    if (!p.reseau.empty()) parReseau[p.reseau].push_back({p.composant, p.pastille.position});
  }
  auto cible = [&](const std::string& reseau, const std::string& composant) -> std::optional<Pt> {
    double sx = 0, sy = 0;
    int n = 0;
    for (const auto& [comp, q] : parReseau[reseau])
      if (comp != composant) {
        sx += static_cast<double>(q.x);
        sy += static_cast<double>(q.y);
        ++n;
      }
    if (n == 0) return std::nullopt;
    return Pt{static_cast<Nm>(std::llround(sx / n)), static_cast<Nm>(std::llround(sy / n))};
  };
  for (const auto& f : c.feuilles)
    for (const auto& s : f.symboles) {
      const DefSymbole* d = c.symbole(s.symbole);
      const ComposantElec* x = c.composant(s.composant);
      if (!d || !x) continue;
      // Groupes d'échange : broches d'une même unité et d'un même groupe
      std::map<std::string, std::vector<std::string>> groupes;  // groupe → numéros réels
      for (const auto& bp : brochesPlacees(s, *d))
        if (!bp.broche->groupe.empty()) groupes[bp.broche->groupe].push_back(bp.numero);
      for (const auto& [nomGroupe, numeros] : groupes) {
        if (numeros.size() < 2) continue;
        std::vector<std::string> reseaux;
        std::vector<std::optional<Pt>> cibles;
        bool complet = true;
        for (const auto& n : numeros) {
          reseaux.push_back(k.nom(s.composant, n));
          cibles.push_back(reseaux.back().empty() ? std::nullopt : cible(reseaux.back(), s.composant));
          complet &= position.count({s.composant, n}) > 0;
        }
        if (!complet) continue;
        // Coût : distance de la broche j à la cible du réseau de la broche i (réseau i posé sur la broche j)
        const std::size_t n = numeros.size();
        std::vector<std::vector<double>> cout(n, std::vector<double>(n, 0));
        for (std::size_t i = 0; i < n; ++i)
          for (std::size_t j = 0; j < n; ++j)
            cout[i][j] = cibles[i] ? distance(position[{s.composant, numeros[j]}], *cibles[i]) * 1e-6 : 0;
        const auto aff = affectationHongroise(cout);
        double avant = 0, apres = 0;
        for (std::size_t i = 0; i < n; ++i) {
          avant += cout[i][i];
          apres += cout[i][static_cast<std::size_t>(aff[i])];
        }
        if (avant - apres < 0.01) continue;
        // La permutation en échanges successifs de deux broches
        std::vector<std::size_t> ou(n);  // réseau i est actuellement sur la broche ou[i]
        std::vector<std::size_t> qui(n);
        for (std::size_t i = 0; i < n; ++i) ou[i] = qui[i] = i;
        for (std::size_t i = 0; i < n; ++i) {
          const std::size_t voulu = static_cast<std::size_t>(aff[i]);
          if (ou[i] == voulu) continue;
          const std::size_t bA = ou[i], bB = voulu, autre = qui[voulu];
          EchangePropose e;
          e.composant = s.composant;
          e.symbolePlace = s.uuid;
          e.reference = x->reference;
          e.numeroA = numeros[bA];
          e.numeroB = numeros[bB];
          e.gain = (cout[i][bA] + cout[autre][bB]) - (cout[i][bB] + cout[autre][bA]);
          r.push_back(e);
          std::swap(ou[i], ou[autre]);
          qui[bA] = autre;
          qui[bB] = i;
        }
      }
    }
  return r;
}

int appliquerEchanges(ConceptionElec& c, const std::vector<EchangePropose>& e) {
  exigerModifiable(c);
  int n = 0;
  for (const auto& x : e) n += echangerBroches(c, x.symbolePlace, x.numeroA, x.numeroB).fait;
  if (n) mettreAJourCircuit(c);
  return n;
}

}  // namespace prom::ecad
