// Prométhée : IDF 3.0 en lecture et en écriture (lot L6, tâche 6.17). Voir idf.hpp.
// Licence GPL-3.0-only.
#include "promethee/idf.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>

#include "promethee/hauteurs.hpp"

namespace prom::ecad {

namespace {
std::string nombre(double v, int decimales) {
  char t[48];
  std::snprintf(t, sizeof t, "%.*f", decimales, v);
  std::string s = t;
  if (s == "-0.0000" || s == "-0.000" || s == "-0.0") s.erase(0, 1);
  return s;
}
std::string guillemets(const std::string& s) {
  std::string r = "\"";
  for (char ch : s) r += ch == '"' ? '\'' : ch;
  return r + "\"";
}
// Une ligne découpée en mots, les chaînes entre guillemets gardées entières
std::vector<std::string> mots(const std::string& ligne) {
  std::vector<std::string> r;
  std::string m;
  bool dans = false, eu = false;
  for (char ch : ligne) {
    if (ch == '"') {
      dans = !dans;
      eu = true;
      continue;
    }
    if (!dans && (ch == ' ' || ch == '\t' || ch == '\r')) {
      if (!m.empty() || eu) r.push_back(m);
      m.clear();
      eu = false;
    } else
      m += ch;
  }
  if (!m.empty() || eu) r.push_back(m);
  return r;
}
double lire(const std::string& s) { return std::atof(s.c_str()); }
// Segments d'un arc de (x0, y0) à (x1, y1) balayant « angle » degrés (sens direct si positif)
void arc(std::vector<std::pair<double, double>>& b, double x0, double y0, double x1, double y1, double angle) {
  const double a = angle * 3.14159265358979323846 / 180;
  const double cx2 = (x0 + x1) / 2, cy2 = (y0 + y1) / 2, dx = x1 - x0, dy = y1 - y0, corde = std::hypot(dx, dy);
  if (corde < 1e-12 || std::abs(a) < 1e-12) {
    b.emplace_back(x1, y1);
    return;
  }
  // Centre sur la médiatrice de la corde, à la distance qui donne l'angle voulu
  const double h = corde / 2 / std::tan(a / 2);
  const double cx = cx2 - dy / corde * h, cy = cy2 + dx / corde * h;
  const double r = std::hypot(x0 - cx, y0 - cy), t0 = std::atan2(y0 - cy, x0 - cx);
  const int n = std::max(4, static_cast<int>(std::ceil(std::abs(angle) / 5)));
  for (int i = 1; i <= n; ++i) {
    const double t = t0 + a * i / n;
    b.emplace_back(i == n ? x1 : cx + r * std::cos(t), i == n ? y1 : cy + r * std::sin(t));
  }
}
// Composant de carte seule posé d'après une empreinte de l'IDF
std::string poserEmpreinteIdf(ConceptionElec& c, const std::string& empreinte, Pt p, double rotation, bool dessous, const std::string& reference) {
  ComposantElec x;
  x.uuid = nouvelUuid();
  x.reference = reference;
  x.empreinte = empreinte;
  x.carteSeule = true;
  c.composants.push_back(x);
  EmpreintePlacee e;
  e.composant = x.uuid;
  e.empreinte = empreinte;
  e.position = p;
  e.rotation = rotation;
  e.dessous = dessous;
  c.circuit.empreintes.push_back(e);
  return x.uuid;
}
}  // namespace

FichiersIdf ecrireIdf(const ConceptionElec& c, const OptionsIdf& o) {
  FichiersIdf f;
  const double u = o.milliemesPouce ? 1000.0 / 25.4 : 1.0;  // mm → unité du fichier
  const int dec = o.milliemesPouce ? 2 : 4;
  // Repère : x à droite, y vers le haut, origine au coin bas gauche de la carte ou à l'origine de travail
  Boite r = boite(c.circuit.contour.polygone());
  if (o.origineTravail && c.circuit.affichage.origineTravail) {
    const Pt ot = *c.circuit.affichage.origineTravail;
    r = Boite{ot.x, ot.y - (r.y1 - r.y0), ot.x + (r.x1 - r.x0), ot.y};
  }
  auto X = [&](Nm x) { return nombre(mm(x - r.x0) * u, dec); };
  auto Y = [&](Nm y) { return nombre(mm(r.y1 - y) * u, dec); };
  const std::string date = !o.date.empty() ? o.date : (c.cartouche.date.size() >= 10 ? c.cartouche.date.substr(0, 4) + "/" + c.cartouche.date.substr(5, 2) + "/" + c.cartouche.date.substr(8, 2) + ".00:00:00" : "2026/01/01.00:00:00");
  std::ostringstream e;
  e << ".HEADER\nBOARD_FILE 3.0 " << guillemets(o.auteur) << " " << date << " 1\n" << guillemets(asciiFabrication(c.nom)) << " " << (o.milliemesPouce ? "THOU" : "MM") << "\n.END_HEADER\n";
  e << ".BOARD_OUTLINE ECAD\n" << nombre(c.circuit.empilage.epaisseur * u, dec) << "\n";
  auto boucle = [&](const Polygone& p, int etiquette) {
    if (p.size() < 3) return;
    for (const Pt& q : p) e << etiquette << " " << X(q.x) << " " << Y(q.y) << " 0.000\n";
    e << etiquette << " " << X(p.front().x) << " " << Y(p.front().y) << " 0.000\n";  // boucle fermée
  };
  boucle(c.circuit.contour.polygone(), 0);
  int n = 1;
  for (const auto& d : c.circuit.contour.decoupes) boucle(d, n++);
  e << ".END_BOARD_OUTLINE\n";
  // Trous percés : pastilles traversantes et non métallisées, vias (les plus grandes), trous de fixation
  e << ".DRILLED_HOLES\n";
  const Connectivite k = connectivite(c);
  for (const auto& p : pastilles(c, k)) {
    if (p.pastille.percage <= 0) continue;
    const ComposantElec* x = c.composant(p.composant);
    if (x && x->nePasMonter) continue;
    const bool npth = p.pastille.type == TypePastille::NonMetallisee;
    const DefEmpreinte* d = nullptr;
    for (const auto& ep : c.circuit.empreintes)
      if (ep.composant == p.composant) d = c.empreinte(ep.empreinte);
    const bool fixation = d && trouDeFixation(*d).has_value();
    e << nombre(mm(p.pastille.percage) * u, dec) << " " << X(p.pastille.position.x) << " " << Y(p.pastille.position.y) << " " << (npth ? "NPTH" : "PTH") << " "
      << (fixation ? std::string("BOARD") : guillemets(x ? x->reference : std::string("NOREFDES"))) << " " << (fixation ? "MTG" : "PIN") << " ECAD\n";
  }
  e << ".END_DRILLED_HOLES\n";
  // Placement et bibliothèque : une géométrie par empreinte, un numéro de pièce par valeur
  e << ".PLACEMENT\n";
  std::ostringstream b;
  b << ".HEADER\nLIBRARY_FILE 3.0 " << guillemets(o.auteur) << " " << date << " 1\n.END_HEADER\n";
  std::set<std::pair<std::string, std::string>> decrits;
  std::map<std::pair<std::string, std::string>, double> hauteurs;
  for (const auto& ep : c.circuit.empreintes) {
    const ComposantElec* x = c.composant(ep.composant);
    if (!x || x->nePasMonter) continue;
    const auto cp = corpsPose(c, ep);
    const DefEmpreinte* d = c.empreinte(ep.empreinte);
    if (!cp || !d) continue;
    const std::string geometrie = asciiFabrication(ep.empreinte);
    std::string piece = asciiFabrication(x->valeur.empty() ? ep.empreinte : x->valeur);
    // La hauteur va avec le couple (géométrie, numéro de pièce) : une autre hauteur fait un autre numéro de pièce
    if (const auto h = hauteurs.find({geometrie, piece}); h != hauteurs.end() && std::abs(h->second - cp->hauteur.mm) > 1e-6) piece += "-H" + nombre(cp->hauteur.mm, 3);
    hauteurs.emplace(std::pair{geometrie, piece}, cp->hauteur.mm);
    // Place du composant : le centre de son corps, angle compté dans le sens direct de la CAO mécanique
    const double angle = std::fmod(std::fmod(-ep.rotation, 360.0) + 360.0, 360.0);
    e << guillemets(geometrie) << " " << guillemets(piece) << " " << asciiFabrication(x->reference) << "\n"
      << X(cp->centre.x) << " " << Y(cp->centre.y) << " 0.000 " << nombre(angle, 3) << " " << (ep.dessous ? "BOTTOM" : "TOP") << " PLACED\n";
    if (!decrits.insert({geometrie, piece}).second) continue;
    b << ".ELECTRICAL\n" << guillemets(geometrie) << " " << guillemets(piece) << " " << (o.milliemesPouce ? "THOU" : "MM") << " " << nombre(cp->hauteur.mm * u, dec) << "\n";
    const double hx = cp->largeur / 2 * u, hy = cp->profondeur / 2 * u;
    if (cp->rond) {
      // Cercle : deux arcs de 180°
      b << "0 " << nombre(-hx, dec) << " 0.0000 0.000\n0 " << nombre(hx, dec) << " 0.0000 180.000\n0 " << nombre(-hx, dec) << " 0.0000 180.000\n";
    } else {
      // Contour dans le repère du composant (y vers le haut)
      const double pts[5][2] = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}, {-hx, -hy}};
      for (const auto& q : pts) b << "0 " << nombre(q[0], dec) << " " << nombre(q[1], dec) << " 0.000\n";
    }
    b << ".END_ELECTRICAL\n";
  }
  e << ".END_PLACEMENT\n";
  f.emn = e.str();
  f.emp = b.str();
  return f;
}

DonneesIdf lireIdf(const std::string& emn, const std::string& emp) {
  DonneesIdf d;
  // Bibliothèque : géométrie et numéro de pièce → contour et hauteur
  struct Bib {
    std::vector<std::pair<double, double>> contour;
    double hauteur = 0;
  };
  std::map<std::pair<std::string, std::string>, Bib> bib;
  {
    std::istringstream in(emp);
    std::string l;
    std::string section;
    std::pair<std::string, std::string> cle;
    double echelle = 1;
    int ligne = 0;
    while (std::getline(in, l)) {
      if (!l.empty() && l.back() == '\r') l.pop_back();
      if (l.empty() || l[0] == '#') continue;
      if (l[0] == '.') {
        section = l.substr(0, l.find(' '));
        ligne = 0;
        continue;
      }
      if (section != ".ELECTRICAL" && section != ".MECHANICAL") continue;
      const auto m = mots(l);
      if (ligne++ == 0) {
        if (m.size() < 4) {
          d.alertes.push_back("en-tête de composant illisible : « " + l + " »");
          continue;
        }
        cle = {m[0], m[1]};
        echelle = m[2] == "THOU" ? 0.0254 : 1.0;
        bib[cle].hauteur = lire(m[3]) * echelle;
        continue;
      }
      if (m.size() < 4) continue;
      auto& c = bib[cle].contour;
      const double x = lire(m[1]) * echelle, y = lire(m[2]) * echelle, a = lire(m[3]);
      if (c.empty() || std::abs(a) < 1e-9) c.emplace_back(x, y);
      else arc(c, c.back().first, c.back().second, x, y, a);
    }
  }
  std::istringstream in(emn);
  std::string l, section;
  int ligne = 0;
  double echelle = 1;
  ComposantIdf courant;
  while (std::getline(in, l)) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    if (l.empty() || l[0] == '#') continue;
    if (l[0] == '.') {
      section = l.substr(0, l.find(' '));
      ligne = 0;
      continue;
    }
    const auto m = mots(l);
    const int n = ligne++;
    if (section == ".HEADER") {
      if (n == 1 && m.size() >= 2) {
        d.nom = m[0];
        d.unite = m[1];
        echelle = m[1] == "THOU" ? 0.0254 : 1.0;
      }
    } else if (section == ".BOARD_OUTLINE") {
      if (n == 0) {
        d.epaisseur = lire(m.empty() ? "1.6" : m[0]) * echelle;
        continue;
      }
      if (m.size() < 4) continue;
      const std::size_t e = static_cast<std::size_t>(std::max(0, std::atoi(m[0].c_str())));
      if (d.boucles.size() <= e) d.boucles.resize(e + 1);
      auto& b = d.boucles[e];
      const double x = lire(m[1]) * echelle, y = lire(m[2]) * echelle, a = lire(m[3]);
      if (b.empty() || std::abs(a) < 1e-9) b.emplace_back(x, y);
      else arc(b, b.back().first, b.back().second, x, y, a);
    } else if (section == ".DRILLED_HOLES") {
      if (m.size() < 6) {
        d.alertes.push_back("trou illisible : « " + l + " »");
        continue;
      }
      TrouIdf t;
      t.diametre = lire(m[0]) * echelle;
      t.x = lire(m[1]) * echelle;
      t.y = lire(m[2]) * echelle;
      t.metallise = m[3] == "PTH";
      t.reference = m[4];
      t.type = m[5];
      d.trous.push_back(t);
    } else if (section == ".PLACEMENT") {
      if (n % 2 == 0) {
        courant = {};
        if (m.size() >= 3) {
          courant.geometrie = m[0];
          courant.numeroPiece = m[1];
          courant.reference = m[2];
        }
      } else if (m.size() >= 5) {
        courant.x = lire(m[0]) * echelle;
        courant.y = lire(m[1]) * echelle;
        courant.z = lire(m[2]) * echelle;
        courant.rotation = lire(m[3]);
        courant.dessous = m[4] == "BOTTOM";
        if (const auto it = bib.find({courant.geometrie, courant.numeroPiece}); it != bib.end()) {
          courant.contour = it->second.contour;
          courant.hauteur = it->second.hauteur;
        } else
          d.alertes.push_back(courant.reference + " : « " + courant.geometrie + " » absent de la bibliothèque");
        d.composants.push_back(courant);
      }
    }
  }
  // Boucles fermées : le dernier point qui répète le premier est retiré
  for (auto& b : d.boucles)
    if (b.size() > 1 && std::abs(b.front().first - b.back().first) < 1e-9 && std::abs(b.front().second - b.back().second) < 1e-9) b.pop_back();
  if (d.boucles.empty() || d.boucles.front().size() < 3) d.alertes.push_back("contour de carte absent");
  return d;
}

ConceptionElec conceptionDepuisIdf(const DonneesIdf& d) {
  ConceptionElec c = nouvelleConception(d.nom.empty() ? "Carte IDF" : d.nom);
  c.circuit.empilage.epaisseur = d.epaisseur;
  // Repère : y du fichier vers le haut, y de la carte vers le bas ; le haut du contour devient y = 0
  double ymax = 0;
  for (const auto& b : d.boucles)
    for (const auto& [x, y] : b) ymax = std::max(ymax, y);
  auto pt = [&](double x, double y) { return ptMm(x, ymax - y); };
  if (!d.boucles.empty()) {
    Polygone p;
    for (const auto& [x, y] : d.boucles.front()) p.push_back(pt(x, y));
    c.circuit.contour.libre = p;
    for (std::size_t i = 1; i < d.boucles.size(); ++i) {
      Polygone q;
      for (const auto& [x, y] : d.boucles[i]) q.push_back(pt(x, y));
      if (q.size() >= 3) c.circuit.contour.decoupes.push_back(q);
    }
  }
  int trous = 0;
  for (const auto& t : d.trous) {
    if (t.type != "MTG" && t.type != "TOOL" && t.reference != "BOARD") continue;
    DefEmpreinte e;
    char id[64];
    std::snprintf(id, sizeof id, "idf:Trou_%.2f", t.diametre);
    e.id = id;
    e.famille = "Mécanique";
    Pastille p;
    p.numero = "";
    p.type = t.metallise ? TypePastille::Traversante : TypePastille::NonMetallisee;
    p.forme = FormePastille::Rond;
    p.largeur = p.hauteur = nm(t.metallise ? t.diametre + 1.0 : t.diametre);
    p.percage = nm(t.diametre);
    e.pastilles.push_back(p);
    const double r = t.diametre / 2 + (t.metallise ? 0.5 : 0);
    e.encombrement = Boite{nm(-r), nm(-r), nm(r), nm(r)};
    c.empreintes[e.id] = e;
    poserEmpreinteIdf(c, e.id, pt(t.x, t.y), 0, false, "MH" + std::to_string(++trous));
  }
  for (const auto& k : d.composants) {
    DefEmpreinte e;
    e.id = "idf:" + k.geometrie;
    if (!c.empreintes.count(e.id)) {
      e.famille = "IDF";
      double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
      bool premier = true;
      for (const auto& [x, y] : k.contour) {
        x0 = premier ? x : std::min(x0, x);
        x1 = premier ? x : std::max(x1, x);
        y0 = premier ? y : std::min(y0, y);
        y1 = premier ? y : std::max(y1, y);
        premier = false;
      }
      e.encombrement = Boite{nm(x0), nm(-y1), nm(x1), nm(-y0)};
      e.corps.largeur = nm(x1 - x0);
      e.corps.profondeur = nm(y1 - y0);
      e.corps.hauteur = nm(k.hauteur);
      c.empreintes[e.id] = e;
    }
    const std::string uuid = poserEmpreinteIdf(c, e.id, pt(k.x, k.y), std::fmod(360.0 - k.rotation, 360.0), k.dessous, k.reference);
    if (ComposantElec* x = c.composant(uuid)) {
      x->valeur = k.numeroPiece;
      x->hauteur = k.hauteur;
    }
  }
  return c;
}

}  // namespace prom::ecad
