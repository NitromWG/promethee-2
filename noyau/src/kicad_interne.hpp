// Prométhée : lecture des expressions S des fichiers KiCad, commune aux imports mécanique et électronique.
// En-tête interne au noyau. Licence GPL-3.0-only.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "promethee/modele.hpp"

namespace prom::kicad_interne {

// ---------- Expressions S ----------
struct Expr {
  bool liste = false;
  bool guillemets = false;  // atome écrit entre guillemets dans le fichier
  std::string atome;
  std::vector<Expr> elements;
  const std::string& nom() const { static const std::string vide; return liste && !elements.empty() && !elements[0].liste ? elements[0].atome : vide; }
  const Expr* enfant(const std::string& n) const {
    for (const auto& e : elements) if (e.nom() == n) return &e;
    return nullptr;
  }
  std::string valeur(size_t i) const { return i < elements.size() && !elements[i].liste ? elements[i].atome : std::string(); }
  double nombre(size_t i, double def = 0) const {
    const std::string v = valeur(i);
    if (v.empty()) return def;
    char* fin = nullptr;
    const double x = std::strtod(v.c_str(), &fin);
    return fin == v.c_str() ? def : x;
  }
};

class Lecteur {
public:
  explicit Lecteur(const std::string& t) : m_t(t) {}
  Expr lire() {
    blancs();
    if (m_i >= m_t.size()) throw std::runtime_error("Fichier KiCad vide.");
    return expression();
  }

private:
  void blancs() { while (m_i < m_t.size() && std::isspace(static_cast<unsigned char>(m_t[m_i]))) ++m_i; }
  Expr expression() {
    blancs();
    Expr e;
    if (m_t[m_i] == '(') {
      e.liste = true;
      ++m_i;
      for (;;) {
        blancs();
        if (m_i >= m_t.size()) throw std::runtime_error("Fichier KiCad tronqué : parenthèse non fermée.");
        if (m_t[m_i] == ')') { ++m_i; break; }
        e.elements.push_back(expression());
      }
    } else if (m_t[m_i] == '"') {
      e.guillemets = true;
      ++m_i;
      while (m_i < m_t.size() && m_t[m_i] != '"') {
        if (m_t[m_i] == '\\' && m_i + 1 < m_t.size()) e.atome += m_t[m_i++];  // échappement conservé tel quel
        e.atome += m_t[m_i++];
      }
      ++m_i;
    } else {
      while (m_i < m_t.size() && !std::isspace(static_cast<unsigned char>(m_t[m_i])) && m_t[m_i] != '(' && m_t[m_i] != ')') e.atome += m_t[m_i++];
    }
    return e;
  }
  const std::string& m_t;
  size_t m_i = 0;
};

// KiCad : Y vers le bas ; Prométhée : Y vers le haut.
inline Point pt(const Expr& e) { return {e.nombre(1), -e.nombre(2)}; }

inline std::string couche(const Expr& e) {
  const Expr* c = e.enfant("layer");
  return c ? c->valeur(1) : std::string();
}

inline bool proches(const Point& a, const Point& b) { return std::hypot(a.x - b.x, a.y - b.y) < 1e-3; }


inline ElementContour inverse(const ElementContour& e) { return {e.fin, e.debut, e.milieu}; }

// Reconstitue des boucles à partir d'éléments donnés dans le désordre.
inline std::vector<Contour> chainer(std::vector<ElementContour> morceaux) {
  std::vector<Contour> res;
  while (!morceaux.empty()) {
    Contour c;
    c.elements.push_back(morceaux.back());
    morceaux.pop_back();
    bool progres = true;
    while (progres && !proches(c.elements.front().debut, c.elements.back().fin)) {
      progres = false;
      for (size_t i = 0; i < morceaux.size(); ++i) {
        if (proches(morceaux[i].debut, c.elements.back().fin)) c.elements.push_back(morceaux[i]);
        else if (proches(morceaux[i].fin, c.elements.back().fin)) c.elements.push_back(inverse(morceaux[i]));
        else continue;
        morceaux.erase(morceaux.begin() + static_cast<long>(i));
        progres = true;
        break;
      }
    }
    c.ferme = proches(c.elements.front().debut, c.elements.back().fin);
    res.push_back(c);
  }
  // Le contour extérieur, le plus grand, en premier ; tous orientés dans le sens direct.
  for (auto& c : res)
    if (c.aire() < 0) {
      std::reverse(c.elements.begin(), c.elements.end());
      for (auto& e : c.elements) e = inverse(e);
    }
  std::sort(res.begin(), res.end(), [](const Contour& a, const Contour& b) { return std::abs(a.aire()) > std::abs(b.aire()); });
  return res;
}


}  // namespace prom::kicad_interne
