// Prométhée : écrivain CSV unique (10.20). Voir csv.hpp. Licence GPL-3.0-only.
#include "promethee/csv.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <sstream>

namespace prom {

namespace {
std::mutex& verrou() {
  static std::mutex m;
  return m;
}
ReglagesCsv& global() {
  static ReglagesCsv r;
  return r;
}
}  // namespace

ReglagesCsv reglagesCsv() {
  std::lock_guard<std::mutex> l(verrou());
  return global();
}

void reglerCsv(const ReglagesCsv& r) {
  std::lock_guard<std::mutex> l(verrou());
  global() = r;
}

ReglagesCsv reglagesCsvDepuisTexte(const std::string& t) {
  ReglagesCsv r;
  std::istringstream in(t);
  std::string mot;
  int rang = 0;
  while (in >> mot) {
    if (mot == "bom") r.marqueUtf8 = true;
    else if (mot == "sansbom") r.marqueUtf8 = false;
    else if (mot == "crlf") r.finLigne = "\r\n";
    else if (mot == "lf") r.finLigne = "\n";
    else if (mot == "guillemets") r.guillemetsToujours = true;
    else if (mot == "tab") (rang++ == 0 ? r.separateur : r.decimale) = '\t';
    else if (mot.size() == 1) (rang++ == 0 ? r.separateur : r.decimale) = mot[0];
  }
  return r;
}

std::string texteReglagesCsv(const ReglagesCsv& r) {
  std::string s = r.separateur == '\t' ? std::string("tab") : std::string(1, r.separateur);
  s += ' ';
  s += r.decimale;
  s += r.marqueUtf8 ? " bom" : " sansbom";
  s += r.finLigne == "\n" ? " lf" : " crlf";
  if (r.guillemetsToujours) s += " guillemets";
  return s;
}

std::string champCsvRegle(const std::string& s, const ReglagesCsv& r) {
  const bool proteger = r.guillemetsToujours || s.find(r.separateur) != std::string::npos || s.find_first_of("\"\r\n") != std::string::npos;
  if (!proteger) return s;
  std::string o = "\"";
  for (char ch : s) {
    if (ch == '"') o += '"';
    o += ch;
  }
  return o + "\"";
}

std::string nombreCsv(double v, int decimales, const ReglagesCsv& r) {
  if (!std::isfinite(v)) return {};
  char t[64];
  std::snprintf(t, sizeof t, "%.*f", std::max(0, decimales), std::abs(v) < 0.5 * std::pow(10.0, -decimales) ? 0.0 : v);
  std::string s = t;
  if (s.find('.') != std::string::npos) {
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  for (char& ch : s)
    if (ch == '.') ch = r.decimale;
  return s;
}

EcrivainCsv::EcrivainCsv(ReglagesCsv r) : m_r(std::move(r)) {}

EcrivainCsv& EcrivainCsv::champ(const std::string& s) {
  if (!m_debutLigne) m_texte += m_r.separateur;
  m_texte += champCsvRegle(s, m_r);
  m_debutLigne = false;
  return *this;
}

EcrivainCsv& EcrivainCsv::nombre(double v, int decimales) {
  // Un nombre avec la marque décimale du séparateur doit être protégé (virgule et virgule)
  return champ(nombreCsv(v, decimales, m_r));
}

EcrivainCsv& EcrivainCsv::fixe(double v, int decimales) {
  char t[64];
  std::snprintf(t, sizeof t, "%.*f", std::max(0, decimales), v);
  std::string s = t;
  for (char& ch : s)
    if (ch == '.') ch = m_r.decimale;
  return champ(s);
}

EcrivainCsv& EcrivainCsv::entier(long long v) { return champ(std::to_string(v)); }

EcrivainCsv& EcrivainCsv::ligne(const std::vector<std::string>& champs) {
  for (const auto& c : champs) champ(c);
  return fin();
}

EcrivainCsv& EcrivainCsv::ligne(std::initializer_list<std::string> champs) {
  for (const auto& c : champs) champ(c);
  return fin();
}

EcrivainCsv& EcrivainCsv::fin() {
  m_texte += m_r.finLigne;
  m_debutLigne = true;
  return *this;
}

std::string EcrivainCsv::texte() const {
  std::string t = m_texte;
  if (!m_debutLigne) t += m_r.finLigne;
  return m_r.marqueUtf8 ? "\xEF\xBB\xBF" + t : t;
}

}  // namespace prom
