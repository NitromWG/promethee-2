// Prométhée : schéma de lignes et topologie des réseaux routés (lot L18 de la 0.34 ; 18.4). Voir topologie_lignes.hpp.
// Licence GPL-3.0-only.
#include "promethee/topologie_lignes.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <tuple>

#include "promethee/piles_pastilles.hpp"

namespace prom::ecad {

using Json = nlohmann::ordered_json;

namespace {
const char* codeTopologie(TopologieSignal t) {
  switch (t) {
    case TopologieSignal::Chaine: return "chaine";
    case TopologieSignal::FlyBy: return "flyby";
    case TopologieSignal::Etoile: return "etoile";
    case TopologieSignal::T: return "t";
    default: return "libre";
  }
}
TopologieSignal topologieDepuis(const std::string& s) {
  if (s == "chaine") return TopologieSignal::Chaine;
  if (s == "flyby") return TopologieSignal::FlyBy;
  if (s == "etoile") return TopologieSignal::Etoile;
  if (s == "t") return TopologieSignal::T;
  return TopologieSignal::Libre;
}
GenreElementLigne genreDepuis(const std::string& s) {
  if (s == "pilote") return GenreElementLigne::Pilote;
  if (s == "via") return GenreElementLigne::Via;
  if (s == "terminaison") return GenreElementLigne::Terminaison;
  if (s == "recepteur") return GenreElementLigne::Recepteur;
  return GenreElementLigne::Troncon;
}
std::string mmTexte(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.1f", v);
  std::string s = b;
  std::replace(s.begin(), s.end(), '.', ',');
  return s;
}
std::string noeudSpice(const std::string& id) {
  std::string r = "n_";
  for (char c : id) r += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
  return r;
}
// Moignon d'une via : la part du fût au-delà des couches qu'elle relie vraiment (mm), contre-perçage compris
double moignonVia(const ConceptionElec& c, const Via& v, Couches utilisees) {
  const Empilage& e = c.circuit.empilage;
  const int n = e.couchesCuivre;
  const auto ordre = couchesCuivre(n);
  std::vector<std::pair<double, double>> cu;
  double z = 0;
  for (const auto& x : pileEmpilage(e)) {
    if (x.genre == GenreCoucheEmpilage::Cuivre) cu.push_back({z, z + x.epaisseur});
    z += x.epaisseur;
  }
  if (static_cast<int>(cu.size()) != n || n < 2) return 0;
  int r0 = rangCuivre(v.de, n), r1 = rangCuivre(v.vers, n);
  if (r0 < 0 || r1 < 0) return 0;
  if (r0 > r1) std::swap(r0, r1);
  int u0 = INT_MAX, u1 = -1;
  for (int i = r0; i <= r1; ++i)
    if (utilisees & bit(ordre[static_cast<std::size_t>(i)])) u0 = std::min(u0, i), u1 = std::max(u1, i);
  if (u1 < 0) return 0;
  double haut = cu[static_cast<std::size_t>(u0)].first - cu[static_cast<std::size_t>(r0)].first;
  double bas = cu[static_cast<std::size_t>(r1)].second - cu[static_cast<std::size_t>(u1)].second;
  if (v.contrePercage) {
    const BilanContrePercage b = bilanContrePercage(v, e);
    if (b.possible) (v.contrePercage->dessous ? bas : haut) = std::min(v.contrePercage->dessous ? bas : haut, b.talon);
  }
  return std::max(haut, bas);
}
}  // namespace

const char* codeElementLigne(GenreElementLigne g) {
  switch (g) {
    case GenreElementLigne::Pilote: return "pilote";
    case GenreElementLigne::Via: return "via";
    case GenreElementLigne::Terminaison: return "terminaison";
    case GenreElementLigne::Recepteur: return "recepteur";
    default: return "troncon";
  }
}

std::vector<SchemaLignes> schemasLignes(const ConceptionElec& c) {
  std::vector<SchemaLignes> r;
  const auto it = c.extensions.find("lignes");
  if (it == c.extensions.end() || !it->second.is_array()) return r;
  for (const auto& j : it->second) {
    SchemaLignes s;
    s.reseau = j.value("reseau", std::string());
    s.topologie = topologieDepuis(j.value("topologie", std::string("libre")));
    if (j.contains("ordre") && j["ordre"].is_array())
      for (const auto& o : j["ordre"])
        if (o.is_string()) s.ordre.push_back(o.get<std::string>());
    s.moignonMax = j.value("moignonMax", 1.0);
    s.moignonViaMax = j.value("moignonViaMax", 0.5);
    s.moignonPastilleMax = j.value("moignonPastilleMax", 0.2);
    s.provenance = j.value("provenance", std::string());
    if (j.contains("elements") && j["elements"].is_array())
      for (const auto& x : j["elements"]) {
        ElementLigne e;
        e.genre = genreDepuis(x.value("genre", std::string("troncon")));
        e.id = x.value("id", std::string());
        e.a = x.value("a", std::string());
        e.b = x.value("b", std::string());
        e.impedance = x.value("impedance", 50.0);
        e.longueur = x.value("longueur", 0.0);
        e.vitesse = x.value("vitesse", 150.0);
        e.resistance = x.value("resistance", 0.0);
        e.tension = x.value("tension", 0.0);
        e.capacite = x.value("capacite", 0.0);
        e.ibis = x.value("ibis", std::string());
        e.modele = x.value("modele", std::string());
        s.elements.push_back(e);
      }
    r.push_back(s);
  }
  return r;
}

void ecrireSchemasLignes(ConceptionElec& c, const std::vector<SchemaLignes>& v) {
  Json a = Json::array();
  for (const auto& s : v) {
    Json el = Json::array();
    for (const auto& e : s.elements) {
      Json x = {{"genre", codeElementLigne(e.genre)}, {"id", e.id}, {"a", e.a}};
      if (!e.b.empty()) x["b"] = e.b;
      x["impedance"] = e.impedance;
      x["longueur"] = e.longueur;
      x["vitesse"] = e.vitesse;
      if (e.resistance != 0) x["resistance"] = e.resistance;
      if (e.tension != 0) x["tension"] = e.tension;
      if (e.capacite != 0) x["capacite"] = e.capacite;
      if (!e.ibis.empty()) x["ibis"] = e.ibis, x["modele"] = e.modele;
      el.push_back(x);
    }
    a.push_back({{"reseau", s.reseau},
                 {"topologie", codeTopologie(s.topologie)},
                 {"ordre", s.ordre},
                 {"moignonMax", s.moignonMax},
                 {"moignonViaMax", s.moignonViaMax},
                 {"moignonPastilleMax", s.moignonPastilleMax},
                 {"provenance", s.provenance},
                 {"elements", el}});
  }
  c.extensions["lignes"] = a;
}

SchemaLignes extraireTopologie(const ConceptionElec& c, const std::string& reseau, const SchemaLignes* prevue) {
  SchemaLignes s;
  s.reseau = reseau;
  s.provenance = "extraite du cuivre routé";
  if (prevue) {
    s.topologie = prevue->topologie;
    s.moignonMax = prevue->moignonMax;
    s.moignonViaMax = prevue->moignonViaMax;
    s.moignonPastilleMax = prevue->moignonPastilleMax;
  }
  const Connectivite k = connectivite(c);
  const int nc = c.circuit.empilage.couchesCuivre;
  struct Noeud {
    std::string id, genre;  // « broche », « via », « jonction »
    Pt p;
    Couches couches = 0;
    Polygone forme;
    Nm rayon = 0;
    int via = -1;
  };
  std::vector<Noeud> noeuds;
  for (const auto& p : pastilles(c, k))
    if (p.reseau == reseau && !p.numero.empty()) noeuds.push_back({p.reference + "." + p.numero, "broche", p.pastille.position, p.couches, p.forme, 0, -1});
  for (std::size_t i = 0; i < c.circuit.vias.size(); ++i)
    if (const Via& v = c.circuit.vias[i]; v.reseau == reseau) noeuds.push_back({"V" + std::to_string(i + 1), "via", v.p, couchesVia(v, nc), {}, v.diametre / 2, static_cast<int>(i)});
  // Pistes du réseau, coupées là où une autre y aboutit (jonction en T)
  struct Seg {
    Pt a, b;
    Couche couche;
    Nm largeur;
  };
  std::vector<Seg> segs;
  for (const auto& t : c.circuit.pistes)
    if (t.reseau == reseau && t.a != t.b) segs.push_back({t.a, t.b, t.couche, t.largeur});
  for (bool coupe = true; coupe;) {
    coupe = false;
    for (std::size_t i = 0; i < segs.size() && !coupe; ++i)
      for (const Pt q : {segs[i].a, segs[i].b})
        for (std::size_t j = 0; j < segs.size() && !coupe; ++j) {
          if (i == j || segs[j].couche != segs[i].couche) continue;
          if (distancePointSegment(q, segs[j].a, segs[j].b) < 2'000 && distance(q, segs[j].a) > 2'000 && distance(q, segs[j].b) > 2'000) {
            const Seg x = segs[j];
            segs[j].b = q;
            segs.push_back({q, x.b, x.couche, x.largeur});
            coupe = true;
          }
        }
  }
  std::map<std::tuple<int, long long, long long>, int> jonctions;
  std::vector<double> pastilleMoignon;  // moignon de pastille par nœud
  auto noeudDe = [&](Pt q, Couche L) -> int {
    for (std::size_t i = 0; i < noeuds.size(); ++i)
      if (noeuds[i].genre == "broche" && (noeuds[i].couches & bit(L)) && dedans(noeuds[i].forme, q)) return static_cast<int>(i);
    for (std::size_t i = 0; i < noeuds.size(); ++i)
      if (noeuds[i].genre == "via" && (noeuds[i].couches & bit(L)) && distance(q, noeuds[i].p) <= static_cast<double>(noeuds[i].rayon) + 1'000) return static_cast<int>(i);
    const auto cle = std::make_tuple(static_cast<int>(L), std::llround(q.x / 1000.0), std::llround(q.y / 1000.0));
    if (const auto it = jonctions.find(cle); it != jonctions.end()) return it->second;
    noeuds.push_back({"jn" + std::to_string(jonctions.size() + 1), "jonction", q, bit(L), {}, 0, -1});
    return jonctions[cle] = static_cast<int>(noeuds.size()) - 1;
  };
  struct Arete {
    int a, b;
    double longueur;  // mm
    Couche couche;
  };
  std::vector<Arete> aretes;
  std::map<int, Couches> couchesUtilisees;
  std::map<int, double> moignonsPastille;
  for (const auto& sg : segs) {
    const int a = noeudDe(sg.a, sg.couche), b = noeudDe(sg.b, sg.couche);
    aretes.push_back({a, b, distance(sg.a, sg.b) * 1e-6, sg.couche});
    couchesUtilisees[a] |= bit(sg.couche);
    couchesUtilisees[b] |= bit(sg.couche);
    // Moignon de pastille : la piste passe le centre de la pastille et va au-delà
    for (const auto& [ix, bout] : {std::pair{a, sg.a}, std::pair{b, sg.b}}) {
      const Noeud& n = noeuds[static_cast<std::size_t>(ix)];
      if (n.genre != "broche") continue;
      if (distancePointSegment(n.p, sg.a, sg.b) < static_cast<double>(sg.largeur) / 2) moignonsPastille[ix] = std::max(moignonsPastille[ix], distance(bout, n.p) * 1e-6);
    }
  }
  // Jonctions de passage (deux arêtes) fondues : un tronçon de bout en bout
  std::vector<std::vector<int>> incidentes;
  for (bool fondu = true; fondu;) {
    fondu = false;
    incidentes.assign(noeuds.size(), {});
    for (std::size_t i = 0; i < aretes.size(); ++i)
      if (aretes[i].a >= 0) incidentes[static_cast<std::size_t>(aretes[i].a)].push_back(static_cast<int>(i)), incidentes[static_cast<std::size_t>(aretes[i].b)].push_back(static_cast<int>(i));
    for (std::size_t j = 0; j < noeuds.size() && !fondu; ++j) {
      if (noeuds[j].genre != "jonction" || incidentes[j].size() != 2) continue;
      Arete& x = aretes[static_cast<std::size_t>(incidentes[j][0])];
      Arete& y = aretes[static_cast<std::size_t>(incidentes[j][1])];
      if (x.couche != y.couche) continue;
      const int u = x.a == static_cast<int>(j) ? x.b : x.a, v = y.a == static_cast<int>(j) ? y.b : y.a;
      if (u == v) continue;
      x = {u, v, x.longueur + y.longueur, x.couche};
      y.a = y.b = -1;
      fondu = true;
    }
    aretes.erase(std::remove_if(aretes.begin(), aretes.end(), [](const Arete& e) { return e.a < 0; }), aretes.end());
  }
  // Pilote et récepteurs
  std::vector<std::string> broches;
  for (const auto& n : noeuds)
    if (n.genre == "broche") broches.push_back(n.id);
  std::sort(broches.begin(), broches.end(), [](const std::string& a, const std::string& b) { return ordreNaturel(a, b); });
  std::string pilote = prevue && !prevue->ordre.empty() ? prevue->ordre.front() : broches.empty() ? std::string() : broches.front();
  auto indice = [&](const std::string& id) {
    for (std::size_t i = 0; i < noeuds.size(); ++i)
      if (noeuds[i].id == id) return static_cast<int>(i);
    return -1;
  };
  const int depart = indice(pilote);
  // Distances le long du cuivre depuis le pilote (Dijkstra)
  std::vector<double> dist(noeuds.size(), 1e18);
  std::vector<int> avant(noeuds.size(), -1);
  if (depart >= 0) {
    std::vector<std::vector<std::pair<int, double>>> adj(noeuds.size());
    for (const auto& e : aretes) adj[static_cast<std::size_t>(e.a)].push_back({e.b, e.longueur}), adj[static_cast<std::size_t>(e.b)].push_back({e.a, e.longueur});
    using Q = std::pair<double, int>;
    std::priority_queue<Q, std::vector<Q>, std::greater<Q>> file;
    dist[static_cast<std::size_t>(depart)] = 0;
    file.push({0, depart});
    while (!file.empty()) {
      const auto [d, u] = file.top();
      file.pop();
      if (d > dist[static_cast<std::size_t>(u)]) continue;
      for (const auto& [v, l] : adj[static_cast<std::size_t>(u)])
        if (d + l < dist[static_cast<std::size_t>(v)]) dist[static_cast<std::size_t>(v)] = d + l, avant[static_cast<std::size_t>(v)] = u, file.push({d + l, v});
    }
  }
  std::vector<std::string> recepteurs;
  if (prevue && prevue->ordre.size() > 1) recepteurs.assign(prevue->ordre.begin() + 1, prevue->ordre.end());
  else
    for (const auto& b : broches)
      if (b != pilote) recepteurs.push_back(b);
  // Ligne principale : du pilote au récepteur le plus loin ; chaque récepteur s'y rattache par sa branche
  int bout = -1;
  for (const auto& r : recepteurs)
    if (const int i = indice(r); i >= 0 && dist[static_cast<std::size_t>(i)] < 1e17 && (bout < 0 || dist[static_cast<std::size_t>(i)] > dist[static_cast<std::size_t>(bout)])) bout = i;
  std::set<int> principale;
  for (int u = bout; u >= 0; u = avant[static_cast<std::size_t>(u)]) principale.insert(u);
  ElementLigne p;
  p.genre = GenreElementLigne::Pilote;
  p.id = p.a = pilote;
  if (depart >= 0) p.position = noeuds[static_cast<std::size_t>(depart)].p;
  s.elements.push_back(p);
  std::vector<std::pair<std::pair<double, double>, std::string>> rangs;
  for (const auto& r : recepteurs) {
    ElementLigne e;
    e.genre = GenreElementLigne::Recepteur;
    e.id = e.a = r;
    const int i = indice(r);
    if (i < 0 || dist[static_cast<std::size_t>(i)] > 1e17) {
      e.distance = -1;  // pas relié au pilote par le cuivre
      s.elements.push_back(e);
      continue;
    }
    int u = i;
    while (u >= 0 && !principale.count(u)) u = avant[static_cast<std::size_t>(u)];
    e.distance = u >= 0 ? dist[static_cast<std::size_t>(u)] : dist[static_cast<std::size_t>(i)];
    e.longueur = dist[static_cast<std::size_t>(i)] - e.distance;  // branche jusqu'à la ligne principale
    e.position = noeuds[static_cast<std::size_t>(i)].p;
    rangs.push_back({{e.distance, e.longueur}, r});
    s.elements.push_back(e);
  }
  std::sort(rangs.begin(), rangs.end());
  s.ordre = {pilote};
  for (const auto& [d, r] : rangs) s.ordre.push_back(r);
  int t = 0;
  for (const auto& e : aretes) {
    ElementLigne x;
    x.genre = GenreElementLigne::Troncon;
    x.id = "T" + std::to_string(++t);
    x.a = noeuds[static_cast<std::size_t>(e.a)].id;
    x.b = noeuds[static_cast<std::size_t>(e.b)].id;
    x.longueur = e.longueur;
    x.distance = std::min(dist[static_cast<std::size_t>(e.a)], dist[static_cast<std::size_t>(e.b)]);
    x.position = noeuds[static_cast<std::size_t>(e.a)].p;
    s.elements.push_back(x);
  }
  for (std::size_t i = 0; i < noeuds.size(); ++i) {
    const Noeud& n = noeuds[i];
    if (n.genre != "via") continue;
    ElementLigne x;
    x.genre = GenreElementLigne::Via;
    x.id = x.a = n.id;
    x.longueur = moignonVia(c, c.circuit.vias[static_cast<std::size_t>(n.via)], couchesUtilisees[static_cast<int>(i)]);
    x.distance = dist[i] < 1e17 ? dist[i] : -1;
    x.position = n.p;
    s.elements.push_back(x);
  }
  for (const auto& [i, l] : moignonsPastille) {
    if (l <= 1e-6) continue;
    ElementLigne x;
    x.genre = GenreElementLigne::Terminaison;  // repère du moignon de pastille : un bout de piste ouvert
    x.id = "P:" + noeuds[static_cast<std::size_t>(i)].id;
    x.a = noeuds[static_cast<std::size_t>(i)].id;
    x.longueur = l;
    x.resistance = 1e12;
    x.position = noeuds[static_cast<std::size_t>(i)].p;
    s.elements.push_back(x);
  }
  return s;
}

std::vector<EcartTopologie> comparerTopologie(const SchemaLignes& prevue, const SchemaLignes& extraite) {
  std::vector<EcartTopologie> r;
  // Ordre de raccordement (fly-by, chaîne)
  if (prevue.ordre.size() > 1 && (prevue.topologie == TopologieSignal::FlyBy || prevue.topologie == TopologieSignal::Chaine) && extraite.ordre != prevue.ordre) {
    std::string a, b;
    for (const auto& x : prevue.ordre) a += (a.empty() ? "" : " → ") + x;
    for (const auto& x : extraite.ordre) b += (b.empty() ? "" : " → ") + x;
    r.push_back({"ordre", extraite.reseau, "ordre de raccordement suivi par le cuivre : " + b + " ; prévu : " + a, 0, 0, {}});
  }
  for (const auto& e : extraite.elements) {
    if (e.genre == GenreElementLigne::Recepteur) {
      if (e.distance < 0) {
        r.push_back({"broche absente", e.id, e.id + " n'est pas reliée au pilote par le cuivre", 0, 0, e.position});
        continue;
      }
      if (prevue.topologie != TopologieSignal::Etoile && e.longueur > prevue.moignonMax + 1e-9)
        r.push_back({"moignon de chaîne", e.id, "branche de " + e.id + " de " + mmTexte(e.longueur) + " mm, au-delà de " + mmTexte(prevue.moignonMax) + " mm", e.longueur, prevue.moignonMax, e.position});
    }
    if (e.genre == GenreElementLigne::Via && e.longueur > prevue.moignonViaMax + 1e-9)
      r.push_back({"moignon de via", e.id, "moignon de via de " + mmTexte(e.longueur) + " mm, au-delà de " + mmTexte(prevue.moignonViaMax) + " mm (contre-perçage ou couche plus proche)", e.longueur,
                   prevue.moignonViaMax, e.position});
    if (e.genre == GenreElementLigne::Terminaison && e.id.rfind("P:", 0) == 0 && e.longueur > prevue.moignonPastilleMax + 1e-9)
      r.push_back({"moignon de pastille", e.a, "piste qui dépasse la pastille " + e.a + " de " + mmTexte(e.longueur) + " mm", e.longueur, prevue.moignonPastilleMax, e.position});
  }
  return r;
}

std::string svgTopologies(const SchemaLignes& prevue, const SchemaLignes& extraite) {
  // Ligne principale à l'échelle (abscisse : distance au pilote), branches vers le bas, vias en rond rouge
  double total = 1;
  for (const auto& e : extraite.elements) total = std::max(total, e.distance + (e.genre == GenreElementLigne::Recepteur ? 0 : e.longueur));
  const double W = 640, H = 240, x0 = 40, k = (W - 80) / total;
  std::ostringstream o;
  o << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << W << " " << H << "\" font-family=\"sans-serif\" font-size=\"11\">\n";
  o << "<text x=\"10\" y=\"16\">" << extraite.reseau << " : extraite (trait plein) sur prévue (tireté)</text>\n";
  // Prévue : récepteurs régulièrement espacés dans l'ordre prévu
  const double yp = 60, ye = 140;
  o << "<line x1=\"" << x0 << "\" y1=\"" << yp << "\" x2=\"" << W - 40 << "\" y2=\"" << yp << "\" stroke=\"#9E9E9E\" stroke-dasharray=\"6 4\"/>\n";
  for (std::size_t i = 0; i < prevue.ordre.size(); ++i) {
    const double x = x0 + (W - 80) * (prevue.ordre.size() > 1 ? static_cast<double>(i) / static_cast<double>(prevue.ordre.size() - 1) : 0);
    o << "<line x1=\"" << x << "\" y1=\"" << yp << "\" x2=\"" << x << "\" y2=\"" << yp + 18 << "\" stroke=\"#9E9E9E\" stroke-dasharray=\"3 3\"/><text x=\"" << x - 14 << "\" y=\"" << yp - 6 << "\" fill=\"#757575\">"
      << prevue.ordre[i] << "</text>\n";
  }
  o << "<line x1=\"" << x0 << "\" y1=\"" << ye << "\" x2=\"" << x0 + total * k << "\" y2=\"" << ye << "\" stroke=\"#1565C0\" stroke-width=\"3\"/>\n";
  for (const auto& e : extraite.elements) {
    const double x = x0 + std::max(0.0, e.distance) * k;
    if (e.genre == GenreElementLigne::Pilote) o << "<rect x=\"" << x0 - 8 << "\" y=\"" << ye - 8 << "\" width=\"16\" height=\"16\" fill=\"#2E7D32\"/><text x=\"" << x0 - 20 << "\" y=\"" << ye + 24 << "\">" << e.id << "</text>\n";
    if (e.genre == GenreElementLigne::Recepteur && e.distance >= 0) {
      const double l = std::max(6.0, e.longueur * k);
      const bool trop = e.longueur > prevue.moignonMax + 1e-9;
      o << "<line x1=\"" << x << "\" y1=\"" << ye << "\" x2=\"" << x << "\" y2=\"" << ye + l << "\" stroke=\"" << (trop ? "#D32F2F" : "#1565C0") << "\" stroke-width=\"2\"/><circle cx=\"" << x << "\" cy=\""
        << ye + l << "\" r=\"5\" fill=\"#1565C0\"/><text x=\"" << x - 14 << "\" y=\"" << ye + l + 18 << "\">" << e.id << "</text>\n";
    }
    if (e.genre == GenreElementLigne::Via) {
      const bool trop = e.longueur > prevue.moignonViaMax + 1e-9;
      o << "<circle cx=\"" << x << "\" cy=\"" << ye << "\" r=\"6\" fill=\"none\" stroke=\"" << (trop ? "#D32F2F" : "#424242") << "\" stroke-width=\"2\"/><text x=\"" << x - 10 << "\" y=\"" << ye - 12 << "\" fill=\""
        << (trop ? "#D32F2F" : "#424242") << "\">" << e.id << " " << mmTexte(e.longueur) << " mm</text>\n";
    }
  }
  o << "</svg>\n";
  return o.str();
}

std::string netlistLignes(const SchemaLignes& s, double fronts, double duree, double balayageMoignon) {
  std::ostringstream o;
  char b[64];
  auto nb = [&](double v) {
    std::snprintf(b, sizeof b, "%.6g", v);
    return std::string(b);
  };
  o << "* Schéma de lignes " << s.reseau << ", écrit par Prométhée\n";
  std::string pilote;
  int k = 0;
  // Pas maximal : la moitié du plus court retard des lignes, sans quoi le moteur perd le pas sur leurs ruptures
  double tdMin = fronts;
  for (const auto& e : s.elements) {
    const double l = e.genre == GenreElementLigne::Via && balayageMoignon >= 0 ? balayageMoignon : e.longueur;
    if ((e.genre == GenreElementLigne::Troncon && !e.b.empty()) || e.genre == GenreElementLigne::Via)
      if (l >= 0.1) tdMin = std::min(tdMin, l / std::max(1.0, e.vitesse) * 1e-9);
  }
  for (const auto& e : s.elements) {
    const std::string a = noeudSpice(e.a), bn = noeudSpice(e.b);
    switch (e.genre) {
      case GenreElementLigne::Pilote:
        pilote = a;
        o << "Vpil" << k << " pil_src 0 PULSE(0 1 0 " << nb(fronts) << " " << nb(fronts) << " " << nb(duree / 2) << " " << nb(duree) << ")\n";
        o << "Rpil" << k << " pil_src " << a << " " << nb(e.resistance > 0 ? e.resistance : 25) << "\n";
        break;
      case GenreElementLigne::Troncon:
        if (e.b.empty()) break;
        // Un tronçon plus court qu'un dixième de millimètre n'est qu'une liaison : une ligne de retard nul gênerait le pas
        if (e.longueur < 0.1) o << "Rc" << ++k << " " << a << " " << bn << " 1m\n";
        else o << "T" << ++k << " " << a << " 0 " << bn << " 0 Z0=" << nb(e.impedance) << " TD=" << nb(e.longueur / std::max(1.0, e.vitesse) * 1e-9) << "\n";
        break;
      case GenreElementLigne::Via: {
        const double l = balayageMoignon >= 0 ? balayageMoignon : e.longueur;
        if (l < 0.1) break;
        const std::string fin = "via_fin" + std::to_string(++k);
        o << "Tv" << k << " " << a << " 0 " << fin << " 0 Z0=" << nb(e.impedance) << " TD=" << nb(std::max(1e-13, l / std::max(1.0, e.vitesse) * 1e-9)) << "\nRv" << k << " " << fin << " 0 1e9\n";
        break;
      }
      case GenreElementLigne::Terminaison:
        if (e.resistance <= 0 || e.resistance >= 1e9) break;
        o << "Rt" << ++k << " " << a << " " << (e.tension != 0 ? "vtt" + std::to_string(k) : std::string("0")) << " " << nb(e.resistance) << "\n";
        if (e.tension != 0) o << "Vtt" << k << " vtt" << k << " 0 " << nb(e.tension) << "\n";
        break;
      case GenreElementLigne::Recepteur:
        o << "Cr" << ++k << " " << a << " 0 " << nb(e.capacite > 0 ? e.capacite : 2e-12) << "\nRr" << k << " " << a << " 0 1e6\n";
        break;
    }
  }
  const double pas = std::min(fronts / 20, tdMin / 2);
  o << ".tran " << nb(pas) << " " << nb(duree) << " 0 " << nb(pas) << "\n.end\n";
  return o.str();
}

int appliquerAuxRegles(ConceptionElec& c, const SchemaLignes& s) {
  auto& signaux = c.circuit.regles.avancees.signaux;
  auto it = std::find_if(signaux.begin(), signaux.end(), [&](const Signal& x) { return x.nom == s.reseau; });
  if (it == signaux.end()) {
    Signal x;
    x.nom = s.reseau;
    x.reseaux = {s.reseau};
    signaux.push_back(x);
    it = signaux.end() - 1;
  }
  it->topologie = s.topologie;
  it->ordre = s.ordre;
  it->troncons.clear();
  // Chaque tronçon prévu entre deux broches de l'ordre, borné à sa longueur prévue (+ 10 %)
  int n = 0;
  for (const auto& e : s.elements)
    if (e.genre == GenreElementLigne::Troncon && e.longueur > 0 && !e.b.empty()) {
      TronconSignal t;
      t.de = e.a;
      t.vers = e.b;
      t.longueurMax = nm(e.longueur * 1.1);
      it->troncons.push_back(t);
      ++n;
    }
  Json prov = c.extensions.count("provenanceRegles") ? c.extensions["provenanceRegles"] : Json::object();
  prov[s.reseau] = {{"source", "schéma de lignes"}, {"provenance", s.provenance}, {"moignonViaMax", s.moignonViaMax}, {"moignonMax", s.moignonMax}};
  c.extensions["provenanceRegles"] = prov;
  return n;
}

std::vector<Violation> verifierMoignons(const ConceptionElec& c) {
  std::vector<Violation> v;
  for (const auto& prevue : schemasLignes(c)) {
    if (prevue.reseau.empty()) continue;
    const SchemaLignes extraite = extraireTopologie(c, prevue.reseau, &prevue);
    for (const auto& e : comparerTopologie(prevue, extraite)) {
      Violation x;
      x.gravite = Gravite::Avertissement;
      x.code = e.genre == "moignon de via" ? "DRC-91" : e.genre == "moignon de chaîne" ? "DRC-92" : e.genre == "moignon de pastille" ? "DRC-94" : "DRC-93";
      x.message = prevue.reseau + " : " + e.message;
      x.conseil = e.genre == "moignon de via"     ? "Contre-perce la via, ou passe la ligne sur une couche plus proche du bout du fût."
                  : e.genre == "moignon de chaîne" ? "Rapproche le récepteur de la ligne principale."
                  : e.genre == "ordre"             ? "Reprends le routage dans l'ordre de raccordement prévu."
                                                   : "Raccourcis la piste au centre de la pastille.";
      x.position = e.position;
      v.push_back(x);
    }
  }
  return v;
}

}  // namespace prom::ecad
