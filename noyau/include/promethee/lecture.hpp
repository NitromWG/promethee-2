// Prométhée : lecture tolérante des fichiers JSON. Licence GPL-3.0-only.
//
// Règle commune à toutes les relectures (pièces, esquisses, assemblages, conceptions électroniques) : une valeur
// absente garde le défaut sans rien dire ; une valeur présente mais mal typée (texte à la place d'un nombre,
// « null » qu'écrit la bibliothèque JSON pour un NaN, nombre non fini) garde aussi le défaut, mais le lecteur le
// note dans ses alertes, que l'interface affiche à l'ouverture. Une valeur lisible n'est jamais bornée en silence :
// c'est la construction qui refuse ce qui est réellement impossible, avec un message.
#pragma once

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace prom {

class LecteurJson {
public:
  using J = nlohmann::ordered_json;
  // contexte : préfixe des alertes (« Pièce « Support », fonction 3 ») ; alertes : nullptr pour n'en garder aucune
  explicit LecteurJson(std::vector<std::string>* alertes = nullptr, std::string contexte = {})
      : m_alertes(alertes), m_contexte(std::move(contexte)) {}

  LecteurJson sous(const std::string& contexte) const { return LecteurJson(m_alertes, m_contexte.empty() ? contexte : m_contexte + ", " + contexte); }
  void alerter(const std::string& message) const {
    if (m_alertes) m_alertes->push_back(m_contexte.empty() ? message : m_contexte + " : " + message);
  }

  double nombre(const J& o, const char* cle, double defaut) const {
    const J* v = trouver(o, cle);
    if (!v) return defaut;
    if (v->is_number()) {
      const double x = v->get<double>();
      if (std::isfinite(x)) return x;
    }
    alerter(std::string("« ") + cle + " » n'est pas un nombre lisible, valeur par défaut gardée");
    return defaut;
  }
  int entier(const J& o, const char* cle, int defaut) const {
    const J* v = trouver(o, cle);
    if (!v) return defaut;
    if (v->is_number_integer()) {
      const auto x = v->get<long long>();
      if (x >= std::numeric_limits<int>::min() && x <= std::numeric_limits<int>::max()) return static_cast<int>(x);
    } else if (v->is_number_float()) {
      const double x = v->get<double>();
      if (std::isfinite(x) && x == std::floor(x) && std::abs(x) < 2e9) return static_cast<int>(x);
    }
    alerter(std::string("« ") + cle + " » n'est pas un entier lisible, valeur par défaut gardée");
    return defaut;
  }
  bool booleen(const J& o, const char* cle, bool defaut) const {
    const J* v = trouver(o, cle);
    if (!v) return defaut;
    if (v->is_boolean()) return v->get<bool>();
    if (v->is_number_integer()) return v->get<long long>() != 0;
    alerter(std::string("« ") + cle + " » n'est pas vrai ou faux, valeur par défaut gardée");
    return defaut;
  }
  std::string texte(const J& o, const char* cle, const std::string& defaut) const {
    const J* v = trouver(o, cle);
    if (!v) return defaut;
    if (v->is_string()) return v->get<std::string>();
    alerter(std::string("« ") + cle + " » n'est pas un texte, valeur par défaut gardée");
    return defaut;
  }
  // Liste de textes : les éléments mal typés sont écartés et signalés
  std::vector<std::string> textes(const J& o, const char* cle) const {
    std::vector<std::string> r;
    const J* v = trouver(o, cle);
    if (!v) return r;
    if (!v->is_array()) { alerter(std::string("« ") + cle + " » n'est pas une liste, ignorée"); return r; }
    for (const auto& x : *v) {
      if (x.is_string()) r.push_back(x.get<std::string>());
      else alerter(std::string("un élément de « ") + cle + " » n'est pas un texte, écarté");
    }
    return r;
  }
  std::vector<int> entiers(const J& o, const char* cle) const {
    std::vector<int> r;
    const J* v = trouver(o, cle);
    if (!v) return r;
    if (!v->is_array()) { alerter(std::string("« ") + cle + " » n'est pas une liste, ignorée"); return r; }
    for (const auto& x : *v) {
      if (x.is_number_integer() && x.get<long long>() >= std::numeric_limits<int>::min() && x.get<long long>() <= std::numeric_limits<int>::max())
        r.push_back(static_cast<int>(x.get<long long>()));
      else alerter(std::string("un élément de « ") + cle + " » n'est pas un entier, écarté");
    }
    return r;
  }
  // Triplet de nombres finis [x, y, z] ; faux (et alerte) s'il est mal formé
  bool triplet(const J& v, double& x, double& y, double& z) const {
    if (v.is_array() && v.size() == 3 && v[0].is_number() && v[1].is_number() && v[2].is_number()) {
      x = v[0].get<double>(); y = v[1].get<double>(); z = v[2].get<double>();
      if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) return true;
    }
    alerter("triplet de coordonnées illisible, écarté");
    return false;
  }

  // Champs d'un objet que le programme ne connaît pas : gardés tels quels pour être réécrits (fichier d'une
  // version plus récente ouvert puis enregistré sans perte).
  static J inconnus(const J& o, std::initializer_list<const char*> connus) {
    J r = J::object();
    if (!o.is_object()) return r;
    for (auto it = o.begin(); it != o.end(); ++it) {
      bool c = false;
      for (const char* k : connus) if (it.key() == k) { c = true; break; }
      if (!c) r[it.key()] = it.value();
    }
    return r;
  }
  // Recopie dans o les champs gardés, sans écraser ceux que le programme vient d'écrire
  static void reporter(J& o, const J& gardes) {
    if (!gardes.is_object()) return;
    for (auto it = gardes.begin(); it != gardes.end(); ++it)
      if (!o.contains(it.key())) o[it.key()] = it.value();
  }

private:
  static const J* trouver(const J& o, const char* cle) {
    if (!o.is_object()) return nullptr;
    const auto it = o.find(cle);
    return it == o.end() ? nullptr : &*it;
  }
  std::vector<std::string>* m_alertes;
  std::string m_contexte;
};

}  // namespace prom
