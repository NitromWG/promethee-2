// Prométhée : façade calée sur la carte (lot L6). Licence GPL-3.0-only.
#include "promethee/facade.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <sstream>

namespace prom::ecad {

namespace {
std::string f3(double v) {
  char b[40];
  std::snprintf(b, sizeof b, "%.3f", v);
  return b;
}
std::string xml(const std::string& s) {
  std::string r;
  for (char ch : s) {
    if (ch == '<') r += "&lt;";
    else if (ch == '>') r += "&gt;";
    else if (ch == '&') r += "&amp;";
    else r += ch;
  }
  return r;
}
// Forme d'une découpe sur la façade (nm, repère de la carte)
Polygone formeDecoupe(const ElementFacade& e, PositionFacade p) {
  const Pt c = ptMm(p.x, p.y);
  if (e.genre == ElementFacade::Genre::DecoupeRect) return rectangle(c, nm(e.largeur), nm(e.hauteur));
  return cercle(c, nm(e.diametre / 2));
}
bool estDecoupe(const ElementFacade& e) { return e.genre == ElementFacade::Genre::DecoupeRonde || e.genre == ElementFacade::Genre::DecoupeRect; }
// Traits d'une échelle graduée : du rayon au rayon + longueur, de l'angle de début à l'angle de fin (degrés, sens
// direct à l'écran, 0 à droite)
std::vector<std::pair<PositionFacade, PositionFacade>> traitsEchelle(const ElementFacade& e, PositionFacade p) {
  std::vector<std::pair<PositionFacade, PositionFacade>> r;
  const int n = std::max(2, e.graduations);
  for (int i = 0; i < n; ++i) {
    const double a = (e.angleDebut + (e.angleFin - e.angleDebut) * i / (n - 1)) * std::numbers::pi / 180;
    const double l = e.longueurTrait * (i % 5 == 0 ? 1.5 : 1.0);
    // y vers le bas : un angle positif monte à l'écran
    r.push_back({{p.x + e.rayon * std::cos(a), p.y - e.rayon * std::sin(a)}, {p.x + (e.rayon + l) * std::cos(a), p.y - (e.rayon + l) * std::sin(a)}});
  }
  return r;
}
}  // namespace

PositionFacade positionElement(const ConceptionElec& c, const ElementFacade& e) {
  if (e.composant.empty()) return {e.dx, e.dy};
  for (const auto& x : c.circuit.empreintes)
    if (x.composant == e.composant) return {mm(x.position.x) + e.dx, mm(x.position.y) + e.dy};
  return {e.dx, e.dy};
}

std::vector<ElementFacade> elementsPour(const ConceptionElec& c, const std::string& composant, bool echelle) {
  std::vector<ElementFacade> r;
  const ComposantElec* k = c.composant(composant);
  if (!k) return r;
  std::string e = k->empreinte;
  std::transform(e.begin(), e.end(), e.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  const double d = e.find("led") != std::string::npos ? 3.2 : e.find("sw") != std::string::npos ? 4.2 : 7.0;
  ElementFacade trou;
  trou.uuid = nouvelUuid();
  trou.genre = ElementFacade::Genre::DecoupeRonde;
  trou.composant = composant;
  trou.diametre = d;
  r.push_back(trou);
  double haut = d / 2 + 2;
  if (echelle) {
    ElementFacade ech;
    ech.uuid = nouvelUuid();
    ech.genre = ElementFacade::Genre::Echelle;
    ech.composant = composant;
    ech.rayon = d / 2 + 2.5;
    r.push_back(ech);
    haut = ech.rayon + ech.longueurTrait * 1.5 + 1.5;
  }
  ElementFacade legende;
  legende.uuid = nouvelUuid();
  legende.genre = ElementFacade::Genre::Legende;
  legende.composant = composant;
  legende.texte = k->valeur.empty() ? k->reference : k->valeur;
  legende.dy = -(haut + legende.taille / 2);
  r.push_back(legende);
  return r;
}

std::vector<DefautFacade> controlerFacade(const ConceptionElec& c, const Facade& f) {
  std::vector<DefautFacade> r;
  const Polygone face = c.circuit.contour.polygone();
  std::vector<std::pair<const ElementFacade*, Polygone>> decoupes;
  for (const auto& e : f.elements) {
    if (!e.composant.empty() && !c.composant(e.composant)) {
      const PositionFacade p = positionElement(c, e);
      r.push_back({"un élément suit un composant qui n’existe plus", {e.uuid}, p.x, p.y});
      continue;
    }
    if (!estDecoupe(e)) continue;
    const PositionFacade p = positionElement(c, e);
    const Polygone forme = formeDecoupe(e, p);
    decoupes.push_back({&e, forme});
    if (!dedans(face, ptMm(p.x, p.y))) {
      r.push_back({"une découpe tombe hors de la face", {e.uuid}, p.x, p.y});
      continue;
    }
    double bord = 1e18;
    for (std::size_t i = 0; i < face.size(); ++i) bord = std::min(bord, distancePolygoneSegment(forme, face[i], face[(i + 1) % face.size()]));
    if (bord < nm(f.marge)) {
      char t[200];
      std::snprintf(t, sizeof t, "une découpe est à %.2f mm du bord de la face (minimum %.2f mm)", mm(static_cast<Nm>(bord)), f.marge);
      r.push_back({t, {e.uuid}, p.x, p.y});
    }
  }
  for (std::size_t i = 0; i < decoupes.size(); ++i)
    for (std::size_t j = i + 1; j < decoupes.size(); ++j) {
      const double d = distancePolygones(decoupes[i].second, decoupes[j].second);
      if (d < nm(f.ecartMin)) {
        const PositionFacade a = positionElement(c, *decoupes[i].first), b = positionElement(c, *decoupes[j].first);
        char t[200];
        std::snprintf(t, sizeof t, "deux découpes à %.2f mm l’une de l’autre (minimum %.2f mm)", mm(static_cast<Nm>(d)), f.ecartMin);
        r.push_back({t, {decoupes[i].first->uuid, decoupes[j].first->uuid}, (a.x + b.x) / 2, (a.y + b.y) / 2});
      }
    }
  return r;
}

std::string svgFacade(const ConceptionElec& c, const Facade& f) {
  const Polygone face = c.circuit.contour.polygone();
  const Boite b = boite(face);
  std::ostringstream o;
  o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << f3(mm(b.largeur())) << "mm\" height=\"" << f3(mm(b.hauteur()))
    << "mm\" viewBox=\"" << f3(mm(b.x0)) << ' ' << f3(mm(b.y0)) << ' ' << f3(mm(b.largeur())) << ' ' << f3(mm(b.hauteur())) << "\">\n";
  o << "<title>" << xml(f.nom) << "</title>\n<g id=\"contour\" fill=\"none\" stroke=\"#000\" stroke-width=\"0.2\"><polygon points=\"";
  for (const Pt q : face) o << f3(mm(q.x)) << ',' << f3(mm(q.y)) << ' ';
  o << "\"/></g>\n<g id=\"decoupes\" fill=\"none\" stroke=\"#D00\" stroke-width=\"0.1\">\n";
  for (const auto& e : f.elements) {
    const PositionFacade p = positionElement(c, e);
    if (e.genre == ElementFacade::Genre::DecoupeRonde)
      o << "<circle id=\"" << e.uuid << "\" cx=\"" << f3(p.x) << "\" cy=\"" << f3(p.y) << "\" r=\"" << f3(e.diametre / 2) << "\"/>\n";
    else if (e.genre == ElementFacade::Genre::DecoupeRect)
      o << "<rect id=\"" << e.uuid << "\" x=\"" << f3(p.x - e.largeur / 2) << "\" y=\"" << f3(p.y - e.hauteur / 2) << "\" width=\"" << f3(e.largeur) << "\" height=\"" << f3(e.hauteur) << "\"/>\n";
  }
  o << "</g>\n<g id=\"gravure\" fill=\"#000\" stroke=\"#000\" stroke-width=\"0.25\">\n";
  for (const auto& e : f.elements) {
    const PositionFacade p = positionElement(c, e);
    if (e.genre == ElementFacade::Genre::Legende)
      o << "<text id=\"" << e.uuid << "\" x=\"" << f3(p.x) << "\" y=\"" << f3(p.y + e.taille * 0.35) << "\" font-family=\"sans-serif\" font-size=\"" << f3(e.taille)
        << "\" text-anchor=\"middle\" stroke=\"none\">" << xml(e.texte) << "</text>\n";
    else if (e.genre == ElementFacade::Genre::Echelle) {
      o << "<g id=\"" << e.uuid << "\">";
      for (const auto& [a, z] : traitsEchelle(e, p)) o << "<line x1=\"" << f3(a.x) << "\" y1=\"" << f3(a.y) << "\" x2=\"" << f3(z.x) << "\" y2=\"" << f3(z.y) << "\"/>";
      o << "</g>\n";
    }
  }
  o << "</g>\n</svg>\n";
  return o.str();
}

std::string dxfFacade(const ConceptionElec& c, const Facade& f) {
  // DXF R12, millimètres, y vers le haut (le repère de la carte est retourné)
  std::ostringstream o;
  auto g = [&](int code, const std::string& v) { o << code << "\n" << v << "\n"; };
  g(0, "SECTION");
  g(2, "HEADER");
  g(9, "$INSUNITS");
  g(70, "4");
  g(0, "ENDSEC");
  g(0, "SECTION");
  g(2, "ENTITIES");
  auto ligne = [&](const char* calque, double x1, double y1, double x2, double y2) {
    g(0, "LINE");
    g(8, calque);
    g(10, f3(x1));
    g(20, f3(-y1));
    g(11, f3(x2));
    g(21, f3(-y2));
  };
  const Polygone face = c.circuit.contour.polygone();
  for (std::size_t i = 0; i < face.size(); ++i) ligne("CONTOUR", mm(face[i].x), mm(face[i].y), mm(face[(i + 1) % face.size()].x), mm(face[(i + 1) % face.size()].y));
  for (const auto& e : f.elements) {
    const PositionFacade p = positionElement(c, e);
    switch (e.genre) {
      case ElementFacade::Genre::DecoupeRonde:
        g(0, "CIRCLE");
        g(8, "DECOUPES");
        g(10, f3(p.x));
        g(20, f3(-p.y));
        g(40, f3(e.diametre / 2));
        break;
      case ElementFacade::Genre::DecoupeRect: {
        const double x0 = p.x - e.largeur / 2, x1 = p.x + e.largeur / 2, y0 = p.y - e.hauteur / 2, y1 = p.y + e.hauteur / 2;
        ligne("DECOUPES", x0, y0, x1, y0);
        ligne("DECOUPES", x1, y0, x1, y1);
        ligne("DECOUPES", x1, y1, x0, y1);
        ligne("DECOUPES", x0, y1, x0, y0);
        break;
      }
      case ElementFacade::Genre::Legende:
        g(0, "TEXT");
        g(8, "GRAVURE");
        g(10, f3(p.x));
        g(20, f3(-p.y));
        g(40, f3(e.taille));
        g(1, e.texte);
        g(72, "1");
        g(11, f3(p.x));
        g(21, f3(-p.y));
        break;
      case ElementFacade::Genre::Echelle:
        for (const auto& [a, z] : traitsEchelle(e, p)) ligne("GRAVURE", a.x, a.y, z.x, z.y);
        break;
    }
  }
  g(0, "ENDSEC");
  g(0, "EOF");
  return o.str();
}

}  // namespace prom::ecad
