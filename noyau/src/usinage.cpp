// Prométhée : fabrication à l'atelier (lot L16). Licence GPL-3.0-only.
#include "promethee/usinage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace prom::ecad {

namespace {
double mmD(Nm v) { return static_cast<double>(v) * 1e-6; }
std::string f3(double v) {
  char s[32];
  std::snprintf(s, sizeof s, "%.3f", v);
  return s;
}
// Point machine : repère de fabrication (origine au coin bas gauche, Y vers le haut), X retourné pour le dessous
std::pair<double, double> machine(const Boite& carte, Pt p, bool miroir) {
  const Pt q = versRepereFabrication(carte, p);
  const double x = mmD(q.x), y = mmD(q.y);
  return {miroir ? mmD(carte.largeur()) - x : x, y};
}
void entete(std::ostringstream& s, const std::string& titre, const OptionsUsinage& o) {
  s << "(" << titre << ", écrit par Prométhée)\n(outil " << f3(o.diametreOutil) << " mm, avance " << o.avance << " mm/min)\nG21\nG90\nG94\nG0 Z" << f3(o.securite) << "\nM3 S" << o.broche
    << "\nG4 P2\n";
}
void fin(std::ostringstream& s, const OptionsUsinage& o) { s << "G0 Z" << f3(o.securite + 3) << "\nM5\nM2\n"; }
}  // namespace

std::map<std::string, Polygones> cuivreParReseau(const ConceptionElec& c, Couche couche) {
  const Connectivite k = connectivite(c);
  std::map<std::string, Polygones> brut;
  for (const auto& p : pastilles(c, k))
    if (p.couches & bit(couche)) brut[p.reseau].push_back(p.forme);
  for (const auto& t : c.circuit.pistes)
    if (t.couche == couche) brut[t.reseau].push_back(oblong(t.a, t.b, t.largeur));
  for (const auto& v : c.circuit.vias) brut[v.reseau].push_back(cercle(v.p, v.diametre / 2));
  const Remplissages r = remplirZones(c, k);
  for (const auto& z : c.circuit.zones)
    if (z.couche == couche)
      if (const auto it = r.find(z.uuid); it != r.end())
        for (const auto& poly : it->second) brut[z.reseau].push_back(poly);
  std::map<std::string, Polygones> res;
  for (auto& [n, p] : brut) res[n] = unir(p);
  return res;
}

ResultatIsolement sillonsIsolement(const ConceptionElec& c, Couche couche, const OptionsUsinage& o) {
  ResultatIsolement r;
  const auto cuivre = cuivreParReseau(c, couche);
  const Nm rayon = nm(o.diametreOutil / 2), pas = nm(o.diametreOutil * (1 - std::clamp(o.recouvrement, 0.0, 0.9)));
  // Écarts trop étroits pour l'outil, réseau contre réseau : le cuivre de l'un, gonflé de la largeur de l'outil, touche
  // celui de l'autre (les trous des plans comptent : un anneau intérieur n'est pas du cuivre)
  std::vector<std::pair<std::string, const Polygones*>> liste;
  for (const auto& [n, p] : cuivre) liste.emplace_back(n, &p);
  for (std::size_t i = 0; i < liste.size(); ++i) {
    const Boite bi = boite(*liste[i].second).gonflee(nm(o.diametreOutil));
    const Polygones gi = gonfler(*liste[i].second, nm(o.diametreOutil) - 1'000);
    for (std::size_t j = i + 1; j < liste.size(); ++j) {
      if (!bi.touche(boite(*liste[j].second))) continue;
      if (aireMm2(intersecter(gi, *liste[j].second)) <= 1e-6) continue;
      r.alertes.push_back("Isolement impossible entre « " + (liste[i].first.empty() ? std::string("sans réseau") : liste[i].first) + " » et « " +
                          (liste[j].first.empty() ? std::string("sans réseau") : liste[j].first) + " » : écart plus étroit que l'outil de " + f3(o.diametreOutil) + " mm.");
    }
  }
  for (const auto& [n, p] : cuivre) {
    // Zone que l'outil ne doit pas toucher : le cuivre des autres réseaux, gonflé du rayon de l'outil
    Polygones autres;
    for (const auto& [m, q] : cuivre)
      if (m != n) autres.insert(autres.end(), q.begin(), q.end());
    const Polygones interdit = autres.empty() ? Polygones{} : gonfler(unir(autres), rayon);
    for (int passe = 0; passe < std::max(1, o.passes); ++passe) {
      const Polygones g = gonfler(p, rayon + passe * pas);
      const Polygones contour = interdit.empty() ? g : soustraire(g, interdit);
      for (const auto& anneau : contour) {
        if (anneau.size() < 3) continue;
        Sillon s;
        s.points = anneau;
        s.points.push_back(anneau.front());
        s.passe = passe;
        s.reseau = n;
        for (std::size_t i = 1; i < s.points.size(); ++i) r.longueur += distance(s.points[i - 1], s.points[i]) * 1e-6;
        r.sillons.push_back(std::move(s));
      }
    }
  }
  return r;
}

std::string gcodeIsolement(const ConceptionElec& c, const ResultatIsolement& r, const OptionsUsinage& o) {
  const Boite carte = boite(c.circuit.contour.polygone());
  std::ostringstream s;
  entete(s, "Isolement" + std::string(o.miroir ? " (dessous, miroir)" : ""), o);
  for (const auto& x : r.sillons) {
    if (x.points.size() < 2) continue;
    const auto [x0, y0] = machine(carte, x.points.front(), o.miroir);
    s << "(" << (x.reseau.empty() ? std::string("sans réseau") : x.reseau) << ", passe " << x.passe + 1 << ")\nG0 X" << f3(x0) << " Y" << f3(y0) << "\nG1 Z" << f3(-o.profondeur) << " F"
      << o.plongee << "\n";
    bool premier = true;
    for (std::size_t i = 1; i < x.points.size(); ++i) {
      const auto [px, py] = machine(carte, x.points[i], o.miroir);
      s << "G1 X" << f3(px) << " Y" << f3(py);
      if (premier) s << " F" << o.avance;
      premier = false;
      s << "\n";
    }
    s << "G0 Z" << f3(o.securite) << "\n";
  }
  fin(s, o);
  return s.str();
}

std::string gcodePercages(const ConceptionElec& c, const OptionsUsinage& o) {
  const Connectivite k = connectivite(c);
  const ModeleFabrication m = modeleFabrication(c, k, remplirZones(c, k));
  std::map<Nm, std::vector<Pt>> parDiametre;
  for (const auto& t : m.trous) parDiametre[t.diametre].push_back(t.p);
  std::ostringstream s;
  entete(s, "Perçages", o);
  int outil = 0;
  for (auto& [d, points] : parDiametre) {
    // Le plus court chemin approché : le plus proche voisin à chaque fois
    std::vector<Pt> ordre;
    Pt courant = {m.boite.x0, m.boite.y1};
    while (!points.empty()) {
      auto it = std::min_element(points.begin(), points.end(), [&](Pt a, Pt b) { return distance(a, courant) < distance(b, courant); });
      courant = *it;
      ordre.push_back(*it);
      points.erase(it);
    }
    s << "(T" << ++outil << " : foret de " << f3(mmD(d)) << " mm, " << ordre.size() << " trou(s))\nM5\nG0 Z" << f3(o.securite + 10) << "\nM0 (changer le foret : " << f3(mmD(d))
      << " mm)\nM3 S" << o.broche << "\nG4 P2\n";
    for (const Pt& p : ordre) {
      const auto [x, y] = machine(m.boite, p, o.miroir);
      s << "G0 X" << f3(x) << " Y" << f3(y) << "\nG1 Z" << f3(-o.profondeurPercage) << " F" << o.plongee << "\nG0 Z" << f3(o.securite) << "\n";
    }
  }
  fin(s, o);
  return s.str();
}

std::string gcodeDecoupe(const ConceptionElec& c, const OptionsUsinage& o) {
  const Polygone contour = c.circuit.contour.polygone();
  const Boite carte = boite(contour);
  const Polygones g = gonfler({contour}, nm(o.diametreDecoupe / 2));
  std::ostringstream s;
  entete(s, "Découpe du contour", o);
  if (g.empty()) {
    fin(s, o);
    return s.str();
  }
  // Le plus grand anneau : le tour extérieur
  const Polygone& tour = *std::max_element(g.begin(), g.end(), [](const Polygone& a, const Polygone& b) { return aireMm2(a) < aireMm2(b); });
  double perimetre = 0;
  for (std::size_t i = 0; i < tour.size(); ++i) perimetre += distance(tour[i], tour[(i + 1) % tour.size()]) * 1e-6;
  const double largeurAttache = o.largeurAttache + o.diametreDecoupe;
  const auto dansAttache = [&](double abscisse) {
    for (int a = 0; a < o.attaches; ++a) {
      const double centre = perimetre * (a + 0.5) / std::max(1, o.attaches);
      if (std::abs(abscisse - centre) < largeurAttache / 2) return true;
    }
    return false;
  };
  const auto [x0, y0] = machine(carte, tour.front(), o.miroir);
  s << "G0 X" << f3(x0) << " Y" << f3(y0) << "\nG1 Z" << f3(-o.profondeurDecoupe) << " F" << o.plongee << "\n";
  double abscisse = 0;
  bool haut = false;
  for (std::size_t i = 1; i <= tour.size(); ++i) {
    const Pt a = tour[i - 1], b = tour[i % tour.size()];
    const double l = distance(a, b) * 1e-6;
    // Pas de 0,5 mm le long du côté pour placer les attaches
    const int n = std::max(1, static_cast<int>(std::ceil(l / 0.5)));
    for (int j = 1; j <= n; ++j) {
      const double t = static_cast<double>(j) / n;
      const Pt p{a.x + static_cast<Nm>((b.x - a.x) * t), a.y + static_cast<Nm>((b.y - a.y) * t)};
      const bool attache = dansAttache(abscisse + l * t);
      if (attache != haut) {
        s << "G1 Z" << f3(attache ? -o.profondeurDecoupe * 0.4 : -o.profondeurDecoupe) << " F" << o.plongee << "\n";
        haut = attache;
      }
      const auto [px, py] = machine(carte, p, o.miroir);
      s << "G1 X" << f3(px) << " Y" << f3(py) << " F" << o.avance << "\n";
    }
    abscisse += l;
  }
  fin(s, o);
  return s.str();
}

double dureeGcode(const std::string& g) {
  std::istringstream s(g);
  std::string l;
  double x = 0, y = 0, z = 0, f = 300, minutes = 0;
  while (std::getline(s, l)) {
    if (l.empty() || l[0] == '(') continue;
    const bool rapide = l.rfind("G0 ", 0) == 0;
    const bool lent = l.rfind("G1 ", 0) == 0;
    if (!rapide && !lent) continue;
    double nx = x, ny = y, nz = z;
    std::istringstream m(l);
    std::string mot;
    while (m >> mot) {
      const double v = mot.size() > 1 ? std::atof(mot.c_str() + 1) : 0;
      if (mot[0] == 'X') nx = v;
      else if (mot[0] == 'Y') ny = v;
      else if (mot[0] == 'Z') nz = v;
      else if (mot[0] == 'F') f = v;
    }
    const double d = std::sqrt((nx - x) * (nx - x) + (ny - y) * (ny - y) + (nz - z) * (nz - z));
    minutes += d / (rapide ? 1500.0 : std::max(1.0, f));
    x = nx, y = ny, z = nz;
  }
  return minutes;
}

Polygones ouverturesPochoir(const ConceptionElec& c, bool dessous, double reduction) {
  const Connectivite k = connectivite(c);
  Polygones r;
  const Couche pate = dessous ? Couche::PateDessous : Couche::PateDessus;
  for (const auto& p : pastilles(c, k)) {
    if (p.pastille.type != TypePastille::CMS || !(p.couches & bit(pate))) continue;  // sans pâte : doigt de connecteur, autre face
    const Polygones o = reduction == 0 ? Polygones{p.forme} : gonfler({p.forme}, -nm(reduction));
    r.insert(r.end(), o.begin(), o.end());
  }
  return r;
}

std::string pochoirSvg(const ConceptionElec& c, bool dessous, double reduction) {
  const Boite carte = boite(c.circuit.contour.polygone());
  const double l = mmD(carte.largeur()), h = mmD(carte.hauteur());
  std::ostringstream s;
  s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << f3(l) << "mm\" height=\"" << f3(h) << "mm\" viewBox=\"0 0 " << f3(l) << " " << f3(h)
    << "\">\n<title>Pochoir " << (dessous ? "dessous" : "dessus") << " — " << c.nom << "</title>\n";
  s << "<rect x=\"0\" y=\"0\" width=\"" << f3(l) << "\" height=\"" << f3(h) << "\" fill=\"none\" stroke=\"#c00\" stroke-width=\"0.1\"/>\n<path fill=\"#000\" d=\"";
  for (const auto& p : ouverturesPochoir(c, dessous, reduction)) {
    for (std::size_t i = 0; i < p.size(); ++i) {
      const auto [x, y] = machine(carte, p[i], dessous);
      s << (i == 0 ? "M" : "L") << f3(x) << " " << f3(h - y) << " ";
    }
    s << "Z ";
  }
  s << "\"/>\n</svg>\n";
  return s.str();
}

std::string pochoirDxf(const ConceptionElec& c, bool dessous, double reduction) {
  const Boite carte = boite(c.circuit.contour.polygone());
  std::ostringstream s;
  s << "0\nSECTION\n2\nENTITIES\n";
  const auto polyligne = [&](const std::vector<std::pair<double, double>>& pts, const char* calque) {
    s << "0\nPOLYLINE\n8\n" << calque << "\n66\n1\n70\n1\n";
    for (const auto& [x, y] : pts) s << "0\nVERTEX\n8\n" << calque << "\n10\n" << f3(x) << "\n20\n" << f3(y) << "\n";
    s << "0\nSEQEND\n";
  };
  polyligne({{0, 0}, {mmD(carte.largeur()), 0}, {mmD(carte.largeur()), mmD(carte.hauteur())}, {0, mmD(carte.hauteur())}}, "CADRE");
  for (const auto& p : ouverturesPochoir(c, dessous, reduction)) {
    std::vector<std::pair<double, double>> pts;
    for (const Pt& q : p) pts.push_back(machine(carte, q, dessous));
    polyligne(pts, "OUVERTURES");
  }
  s << "0\nENDSEC\n0\nEOF\n";
  return s.str();
}


// ======================================================================================================================
// Compléments d'usinage (16.8)
// ======================================================================================================================
double largeurFraiseV(double angleDegres, double pointe, double profondeur) {
  return pointe + 2 * std::max(0.0, profondeur) * std::tan(angleDegres * 3.14159265358979323846 / 360);
}
double profondeurFraiseV(double angleDegres, double pointe, double largeur) {
  const double t = std::tan(angleDegres * 3.14159265358979323846 / 360);
  return t <= 0 ? 0 : std::max(0.0, (largeur - pointe) / (2 * t));
}

ResultatIsolement nettoyageCuivre(const ConceptionElec& c, Couche couche, const OptionsUsinage& o) {
  ResultatIsolement r;
  const Polygone carte = c.circuit.contour.polygone();
  if (carte.size() < 3) return r;
  // Cuivre gardé et ses sillons d'isolement (les passes de 16.7), puis l'outil n'approche pas le bord de la carte
  Polygones garde;
  for (const auto& [n, p] : cuivreParReseau(c, couche)) garde.insert(garde.end(), p.begin(), p.end());
  const Nm rayon = nm(o.diametreOutil / 2), pas = nm(o.diametreOutil * (1 - std::clamp(o.recouvrement, 0.0, 0.9)));
  const Nm isolement = rayon + std::max(0, o.passes - 1) * pas + rayon;
  Polygones zone = soustraire(gonfler({orienter(carte)}, -rayon), garde.empty() ? Polygones{} : gonfler(unir(garde), isolement));
  // Poche : chaque passe décale la précédente vers l'intérieur, jusqu'à ce qu'il ne reste rien
  for (int passe = 0; passe < 2000 && !zone.empty(); ++passe) {
    bool ajoute = false;
    for (const auto& anneau : zone) {
      if (anneau.size() < 3 || aireMm2(anneau) < 1e-4) continue;
      Sillon s;
      s.points = anneau;
      s.points.push_back(anneau.front());
      s.passe = passe;
      s.reseau = "nettoyage";
      for (std::size_t i = 1; i < s.points.size(); ++i) r.longueur += distance(s.points[i - 1], s.points[i]) * 1e-6;
      r.sillons.push_back(std::move(s));
      ajoute = true;
    }
    if (!ajoute) break;
    zone = gonfler(zone, -pas);
  }
  return r;
}

double GrilleRelief::hauteur(double x, double y) const {
  if (!valide()) return 0;
  const double u = std::clamp((x - x0) / pasX, 0.0, static_cast<double>(nx - 1)), v = std::clamp((y - y0) / pasY, 0.0, static_cast<double>(ny - 1));
  const int i = std::min(nx - 2, static_cast<int>(std::floor(u))), j = std::min(ny - 2, static_cast<int>(std::floor(v)));
  const double a = u - i, b = v - j;
  auto Z = [&](int ii, int jj) { return z[static_cast<std::size_t>(jj * nx + ii)]; };
  return Z(i, j) * (1 - a) * (1 - b) + Z(i + 1, j) * a * (1 - b) + Z(i, j + 1) * (1 - a) * b + Z(i + 1, j + 1) * a * b;
}

std::string gcodePalpage(const OptionsPalpage& o) {
  std::ostringstream s;
  const int nx = std::max(2, o.nx), ny = std::max(2, o.ny);
  const double px = (o.x1 - o.x0) / (nx - 1), py = (o.y1 - o.y0) / (ny - 1);
  const bool grbl = o.machine == "grbl";
  s << "(Palpage en grille " << nx << " × " << ny << ", écrit par Prométhée)\n(Z0 réglé au contact de la plaque au premier point)\nG21\nG90\nG94\n";
  if (!grbl) s << "(PROBEOPEN releve.txt)\n";
  s << "G0 Z" << f3(o.securite) << "\n";
  for (int j = 0; j < ny; ++j)
    for (int k = 0; k < nx; ++k) {
      const int i = j % 2 == 0 ? k : nx - 1 - k;  // serpentin
      s << "G0 X" << f3(o.x0 + i * px) << " Y" << f3(o.y0 + j * py) << "\nG38.2 Z" << f3(o.descente) << " F" << o.avance << "\nG0 Z" << f3(o.securite) << "\n";
    }
  if (!grbl) s << "(PROBECLOSE)\n";
  s << "G0 Z" << f3(o.securite + 3) << "\nM2\n";
  return s.str();
}

std::optional<GrilleRelief> lireRelevePalpage(const std::string& texte, std::string* erreur) {
  auto refuser = [&](const std::string& e) -> std::optional<GrilleRelief> {
    if (erreur) *erreur = e;
    return std::nullopt;
  };
  std::vector<std::array<double, 3>> points;
  std::istringstream in(texte);
  std::string l;
  static const std::regex grbl(R"(PRB:\s*(-?[0-9.]+),\s*(-?[0-9.]+),\s*(-?[0-9.]+)(:1)?)");
  static const std::regex marlin(R"(X:\s*(-?[0-9.]+)\s+Y:\s*(-?[0-9.]+)\s+Z:\s*(-?[0-9.]+))");
  while (std::getline(in, l)) {
    std::smatch m;
    if (std::regex_search(l, m, grbl)) {
      if (m[4].matched || l.find(":1]") != std::string::npos || l.find(":1") != std::string::npos) points.push_back({std::stod(m[1]), std::stod(m[2]), std::stod(m[3])});
      continue;
    }
    if (std::regex_search(l, m, marlin)) {
      points.push_back({std::stod(m[1]), std::stod(m[2]), std::stod(m[3])});
      continue;
    }
    // LinuxCNC : « x y z a b c u v w » (les trois premiers nombres)
    std::istringstream ls(l);
    double x, y, z;
    if (ls >> x >> y >> z) points.push_back({x, y, z});
  }
  if (points.size() < 4) return refuser("relevé trop court : " + std::to_string(points.size()) + " point(s)");
  auto arrondi = [](double v) { return std::llround(v * 100); };  // au centième de mm
  std::set<long long> xs, ys;
  for (const auto& p : points) xs.insert(arrondi(p[0])), ys.insert(arrondi(p[1]));
  GrilleRelief g;
  g.nx = static_cast<int>(xs.size());
  g.ny = static_cast<int>(ys.size());
  if (g.nx < 2 || g.ny < 2) return refuser("les points ne forment pas une grille");
  if (static_cast<std::size_t>(g.nx) * static_cast<std::size_t>(g.ny) != points.size()) return refuser("grille incomplète : " + std::to_string(points.size()) + " points pour " + std::to_string(g.nx) + " × " + std::to_string(g.ny));
  const std::vector<long long> vx(xs.begin(), xs.end()), vy(ys.begin(), ys.end());
  g.x0 = vx.front() / 100.0;
  g.y0 = vy.front() / 100.0;
  g.pasX = (vx.back() - vx.front()) / 100.0 / (g.nx - 1);
  g.pasY = (vy.back() - vy.front()) / 100.0 / (g.ny - 1);
  for (std::size_t i = 1; i < vx.size(); ++i)
    if (std::abs((vx[i] - vx[i - 1]) / 100.0 - g.pasX) > 0.05) return refuser("pas irrégulier en X");
  for (std::size_t j = 1; j < vy.size(); ++j)
    if (std::abs((vy[j] - vy[j - 1]) / 100.0 - g.pasY) > 0.05) return refuser("pas irrégulier en Y");
  g.z.assign(points.size(), 0);
  std::vector<bool> vu(points.size(), false);
  for (const auto& p : points) {
    const int i = static_cast<int>(std::lower_bound(vx.begin(), vx.end(), arrondi(p[0])) - vx.begin());
    const int j = static_cast<int>(std::lower_bound(vy.begin(), vy.end(), arrondi(p[1])) - vy.begin());
    const std::size_t k = static_cast<std::size_t>(j * g.nx + i);
    if (vu[k]) return refuser("point palpé deux fois");
    vu[k] = true;
    g.z[k] = p[2];
  }
  return g;
}

std::string corrigerProfondeur(const std::string& gcode, const GrilleRelief& g, double pasMax) {
  std::istringstream in(gcode);
  std::ostringstream out;
  std::string l;
  double x = 0, y = 0, z = 0;  // position programmée (avant correction)
  int mode = 0;                // 0 : G0, 1 : G1
  static const std::regex mot(R"(([GXYZF])\s*(-?[0-9]*\.?[0-9]+))", std::regex::icase);
  pasMax = std::max(0.05, pasMax);
  while (std::getline(in, l)) {
    // Commentaires et lignes sans mouvement : recopiés
    const std::string code = l.substr(0, l.find('('));
    std::optional<double> nx, ny, nz, f;
    std::optional<int> g01;
    bool autreG = false;
    for (auto it = std::sregex_iterator(code.begin(), code.end(), mot); it != std::sregex_iterator(); ++it) {
      const char c = static_cast<char>(std::toupper(static_cast<unsigned char>((*it)[1].str()[0])));
      const double v = std::stod((*it)[2]);
      if (c == 'G') {
        if (v == 0 || v == 1) g01 = static_cast<int>(v);
        else autreG = true;
      } else if (c == 'X') nx = v;
      else if (c == 'Y') ny = v;
      else if (c == 'Z') nz = v;
      else if (c == 'F') f = v;
    }
    if (autreG || (!nx && !ny && !nz)) {
      out << l << "\n";
      if (g01) mode = *g01;
      continue;
    }
    if (g01) mode = *g01;
    const double x1 = nx.value_or(x), y1 = ny.value_or(y), z1 = nz.value_or(z);
    if (mode == 0) {
      out << "G0";
      if (nx) out << " X" << f3(x1);
      if (ny) out << " Y" << f3(y1);
      if (nz) out << " Z" << f3(z1 + g.hauteur(x1, y1));
      out << "\n";
    } else {
      const double longueur = std::hypot(x1 - x, y1 - y);
      const int n = std::max(1, static_cast<int>(std::ceil(longueur / pasMax)));
      for (int i = 1; i <= n; ++i) {
        const double t = static_cast<double>(i) / n, xi = x + (x1 - x) * t, yi = y + (y1 - y) * t, zi = z + (z1 - z) * t;
        out << "G1 X" << f3(xi) << " Y" << f3(yi) << " Z" << f3(zi + g.hauteur(xi, yi));
        if (i == 1 && f) out << " F" << *f;
        out << "\n";
      }
    }
    x = x1, y = y1, z = z1;
  }
  return out.str();
}

}  // namespace prom::ecad
