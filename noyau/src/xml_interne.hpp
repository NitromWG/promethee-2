// Prométhée : lecteur XML minimal du noyau (éléments, attributs, texte, entités, CDATA, commentaires), commun aux
// imports QElectroTech, EAGLE, Visio et aux fiches IPC/DAC-2552. En-tête interne au noyau. Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace prom::xml_interne {

// ---------- Lecteur XML minimal (éléments, attributs, texte, entités, CDATA, commentaires) ----------
struct NoeudXml {
  std::string nom;
  std::vector<std::pair<std::string, std::string>> attributs;
  std::vector<NoeudXml> enfants;
  std::string texte;
  std::string attr(const char* n, const std::string& defaut = {}) const {
    for (const auto& [k, v] : attributs)
      if (k == n) return v;
    return defaut;
  }
  bool a(const char* n) const {
    for (const auto& [k, v] : attributs)
      if (k == n) return true;
    return false;
  }
  const NoeudXml* enfant(const char* n) const {
    for (const auto& e : enfants)
      if (e.nom == n) return &e;
    return nullptr;
  }
  std::vector<const NoeudXml*> tous(const char* n) const {
    std::vector<const NoeudXml*> v;
    for (const auto& e : enfants)
      if (e.nom == n) v.push_back(&e);
    return v;
  }
};

inline void utf8(std::string& s, std::uint32_t c) {
  if (c < 0x80) s += static_cast<char>(c);
  else if (c < 0x800) {
    s += static_cast<char>(0xC0 | (c >> 6));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    s += static_cast<char>(0xE0 | (c >> 12));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x110000) {
    s += static_cast<char>(0xF0 | (c >> 18));
    s += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (c & 0x3F));
  }
}

inline std::string entites(const std::string& s) {
  if (s.find('&') == std::string::npos) return s;
  std::string r;
  r.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      r += s[i];
      continue;
    }
    const std::size_t f = s.find(';', i);
    if (f == std::string::npos || f - i > 12) {
      r += s[i];
      continue;
    }
    const std::string e = s.substr(i + 1, f - i - 1);
    if (e == "lt") r += '<';
    else if (e == "gt") r += '>';
    else if (e == "amp") r += '&';
    else if (e == "quot") r += '"';
    else if (e == "apos") r += '\'';
    else if (!e.empty() && e[0] == '#') {
      try {
        const std::uint32_t c = (e.size() > 1 && (e[1] == 'x' || e[1] == 'X')) ? static_cast<std::uint32_t>(std::stoul(e.substr(2), nullptr, 16))
                                                                                : static_cast<std::uint32_t>(std::stoul(e.substr(1)));
        utf8(r, c);
      } catch (...) {
        r += s.substr(i, f - i + 1);
      }
    } else {
      r += s.substr(i, f - i + 1);
    }
    i = f;
  }
  return r;
}

inline NoeudXml lireXml(const std::string& s) {
  NoeudXml document;
  std::vector<NoeudXml*> pile{&document};
  std::size_t i = 0;
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) i = 3;
  auto espace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  while (i < s.size()) {
    if (s[i] != '<') {
      const std::size_t f = s.find('<', i);
      const std::string t = s.substr(i, f == std::string::npos ? std::string::npos : f - i);
      if (pile.size() > 1) pile.back()->texte += entites(t);
      if (f == std::string::npos) break;
      i = f;
      continue;
    }
    if (s.compare(i, 4, "<!--") == 0) {
      const std::size_t f = s.find("-->", i + 4);
      if (f == std::string::npos) throw std::runtime_error("commentaire XML non fermé");
      i = f + 3;
      continue;
    }
    if (s.compare(i, 9, "<![CDATA[") == 0) {
      const std::size_t f = s.find("]]>", i + 9);
      if (f == std::string::npos) throw std::runtime_error("section CDATA non fermée");
      if (pile.size() > 1) pile.back()->texte += s.substr(i + 9, f - i - 9);
      i = f + 3;
      continue;
    }
    if (s.compare(i, 2, "<?") == 0) {
      const std::size_t f = s.find("?>", i + 2);
      if (f == std::string::npos) throw std::runtime_error("instruction XML non fermée");
      i = f + 2;
      continue;
    }
    if (s.compare(i, 2, "<!") == 0) {
      const std::size_t f = s.find('>', i + 2);
      if (f == std::string::npos) throw std::runtime_error("déclaration XML non fermée");
      i = f + 1;
      continue;
    }
    if (s.compare(i, 2, "</") == 0) {
      const std::size_t f = s.find('>', i + 2);
      if (f == std::string::npos) throw std::runtime_error("balise fermante incomplète");
      std::string nom = s.substr(i + 2, f - i - 2);
      while (!nom.empty() && espace(nom.back())) nom.pop_back();
      if (pile.size() <= 1 || pile.back()->nom != nom) throw std::runtime_error("balise « " + nom + " » fermée sans avoir été ouverte");
      pile.pop_back();
      i = f + 1;
      continue;
    }
    // Balise ouvrante
    std::size_t j = i + 1;
    while (j < s.size() && !espace(s[j]) && s[j] != '>' && s[j] != '/') ++j;
    NoeudXml n;
    n.nom = s.substr(i + 1, j - i - 1);
    if (n.nom.empty()) throw std::runtime_error("balise sans nom");
    bool fermee = false;
    while (j < s.size()) {
      while (j < s.size() && espace(s[j])) ++j;
      if (j >= s.size()) throw std::runtime_error("balise « " + n.nom + " » incomplète");
      if (s[j] == '>') {
        ++j;
        break;
      }
      if (s[j] == '/' && j + 1 < s.size() && s[j + 1] == '>') {
        fermee = true;
        j += 2;
        break;
      }
      std::size_t k = j;
      while (k < s.size() && s[k] != '=' && !espace(s[k]) && s[k] != '>' && s[k] != '/') ++k;
      const std::string cle = s.substr(j, k - j);
      while (k < s.size() && espace(s[k])) ++k;
      if (k >= s.size() || s[k] != '=') {
        // Attribut sans valeur (HTML) : toléré
        n.attributs.push_back({cle, {}});
        j = k;
        continue;
      }
      ++k;
      while (k < s.size() && espace(s[k])) ++k;
      if (k >= s.size() || (s[k] != '"' && s[k] != '\'')) throw std::runtime_error("valeur d'attribut sans guillemets dans « " + n.nom + " »");
      const char q = s[k];
      const std::size_t f = s.find(q, k + 1);
      if (f == std::string::npos) throw std::runtime_error("valeur d'attribut non fermée dans « " + n.nom + " »");
      n.attributs.push_back({cle, entites(s.substr(k + 1, f - k - 1))});
      j = f + 1;
    }
    pile.back()->enfants.push_back(std::move(n));
    if (!fermee) pile.push_back(&pile.back()->enfants.back());
    i = j;
  }
  if (pile.size() > 1) throw std::runtime_error("balise « " + pile.back()->nom + " » jamais fermée");
  for (auto& e : document.enfants)
    if (!e.nom.empty()) return std::move(e);
  throw std::runtime_error("document XML vide");
}


}  // namespace prom::xml_interne
