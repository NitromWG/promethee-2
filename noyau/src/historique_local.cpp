// Prométhée : historique local du projet (lot L12, 12.9). Licence GPL-3.0-only.
#include "promethee/historique_local.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "promethee/archive.hpp"
#include "promethee/compression.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hachage.hpp"

namespace fs = std::filesystem;

namespace prom {

namespace {
using J = nlohmann::ordered_json;

// Table de l'empreinte roulante « gear » : 256 nombres tirés d'un générateur fixe, les mêmes partout
const std::array<std::uint64_t, 256>& table() {
  static const std::array<std::uint64_t, 256> t = [] {
    std::array<std::uint64_t, 256> r{};
    std::uint64_t x = 0x9E3779B97F4A7C15ull;
    for (auto& v : r) {
      x ^= x << 13;
      x ^= x >> 7;
      x ^= x << 17;
      v = x;
    }
    return r;
  }();
  return t;
}

std::string horodatage(std::string& iso) {
  const auto maintenant = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(maintenant);
  const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(maintenant.time_since_epoch()).count() % 1000);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char id[32], date[40];
  std::strftime(id, sizeof id, "%Y%m%d-%H%M%S", &tm);
  std::strftime(date, sizeof date, "%Y-%m-%dT%H:%M:%S", &tm);
  char suite[8];
  std::snprintf(suite, sizeof suite, "-%03d", ms);
  iso = date;
  return std::string(id) + suite;
}

fs::path cheminMorceau(const std::string& dossier, const std::string& h) { return cheminFichier(dossier) / "morceaux" / h.substr(0, 2) / h; }
fs::path dossierInstantanes(const std::string& dossier) { return cheminFichier(dossier) / "instantanes"; }

J lireListe(const fs::path& p) { return J::parse(lireFichier(p)); }
}  // namespace

HistoriqueLocal::HistoriqueLocal(std::string dossier, std::uint64_t plafond) : m_dossier(std::move(dossier)), m_plafond(plafond) {
  fs::create_directories(dossierInstantanes(m_dossier));
  fs::create_directories(cheminFichier(m_dossier) / "morceaux");
}

std::vector<std::pair<std::size_t, std::size_t>> HistoriqueLocal::decouper(const std::string& s) {
  constexpr std::size_t MIN = 1024, MAX = 16384;
  // Douze bits de poids fort : ils dépendent des 64 derniers octets (ceux de poids faible, des 12 derniers seulement,
  // trop peu dans un texte indenté) ; une coupe tous les 4 Kio en moyenne
  constexpr std::uint64_t MASQUE = 0xFFF0000000000000ull;
  std::vector<std::pair<std::size_t, std::size_t>> r;
  const auto& t = table();
  std::size_t debut = 0;
  std::uint64_t h = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    h = (h << 1) + t[static_cast<unsigned char>(s[i])];
    const std::size_t longueur = i + 1 - debut;
    if ((longueur >= MIN && (h & MASQUE) == 0) || longueur >= MAX) {
      r.emplace_back(debut, longueur);
      debut = i + 1;
      h = 0;
    }
  }
  if (debut < s.size()) r.emplace_back(debut, s.size() - debut);
  return r;
}

std::string HistoriqueLocal::enregistrer(const std::string& texte, const std::string& nom) {
  const std::string empreinte = sha256Hex(texte);
  const std::vector<Instantane> avant = chronologie();
  if (!avant.empty() && avant.back().empreinte == empreinte) {
    if (!nom.empty()) nommer(avant.back().id, nom);
    return avant.back().id;
  }
  J liste = J::array();
  for (const auto& [debut, n] : decouper(texte)) {
    const std::string morceau = texte.substr(debut, n);
    const std::string h = sha256Hex(morceau);
    const fs::path p = cheminMorceau(m_dossier, h);
    if (!fs::exists(p)) {
      fs::create_directories(p.parent_path());
      ecrireFichierAtomique(p, deflerBrut(morceau));
    }
    liste.push_back(h);
  }
  std::string date;
  std::string id = horodatage(date);
  for (int i = 2; fs::exists(dossierInstantanes(m_dossier) / (id + ".json")); ++i) id = horodatage(date) + "-" + std::to_string(i);
  J o = {{"id", id}, {"date", date}, {"taille", texte.size()}, {"empreinte", empreinte}, {"morceaux", liste}};
  if (!nom.empty()) o["nom"] = nom;
  ecrireFichierAtomique(dossierInstantanes(m_dossier) / (id + ".json"), o.dump());
  appliquerPlafond();
  return id;
}

std::vector<HistoriqueLocal::Instantane> HistoriqueLocal::chronologie() const {
  std::vector<Instantane> r;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dossierInstantanes(m_dossier), ec)) {
    if (e.path().extension() != ".json") continue;
    try {
      const J o = lireListe(e.path());
      Instantane x;
      x.id = o.value("id", std::string());
      x.date = o.value("date", std::string());
      x.nom = o.value("nom", std::string());
      x.taille = o.value("taille", std::uint64_t{0});
      x.empreinte = o.value("empreinte", std::string());
      if (!x.id.empty()) r.push_back(x);
    } catch (const std::exception&) {
      // liste illisible : ignorée, comme si l'instantané était perdu
    }
  }
  std::sort(r.begin(), r.end(), [](const Instantane& a, const Instantane& b) { return a.id < b.id; });
  return r;
}

std::string HistoriqueLocal::restaurer(const std::string& id) const {
  const fs::path p = dossierInstantanes(m_dossier) / (id + ".json");
  if (!fs::exists(p)) throw std::runtime_error("instantané « " + id + " » introuvable");
  const J o = lireListe(p);
  std::string texte;
  texte.reserve(o.value("taille", std::size_t{0}));
  for (const auto& h : o["morceaux"]) {
    const std::string cle = h.get<std::string>();
    const std::string morceau = inflerBrut(lireFichier(cheminMorceau(m_dossier, cle)));
    if (sha256Hex(morceau) != cle) throw std::runtime_error("morceau abîmé dans l'historique : " + cle);
    texte += morceau;
  }
  if (sha256Hex(texte) != o.value("empreinte", std::string())) throw std::runtime_error("instantané « " + id + " » abîmé");
  return texte;
}

bool HistoriqueLocal::nommer(const std::string& id, const std::string& nom) {
  const fs::path p = dossierInstantanes(m_dossier) / (id + ".json");
  if (!fs::exists(p)) return false;
  J o = lireListe(p);
  if (nom.empty()) o.erase("nom");
  else o["nom"] = nom;
  ecrireFichierAtomique(p, o.dump());
  return true;
}

std::uint64_t HistoriqueLocal::occupation() const {
  std::uint64_t n = 0;
  std::error_code ec;
  for (const auto& e : fs::recursive_directory_iterator(cheminFichier(m_dossier), ec))
    if (e.is_regular_file(ec)) n += e.file_size(ec);
  return n;
}

int HistoriqueLocal::morceaux() const {
  int n = 0;
  std::error_code ec;
  for (const auto& e : fs::recursive_directory_iterator(cheminFichier(m_dossier) / "morceaux", ec))
    if (e.is_regular_file(ec)) ++n;
  return n;
}

void HistoriqueLocal::appliquerPlafond() {
  if (occupation() <= m_plafond) return;
  std::vector<Instantane> c = chronologie();
  // Les plus anciens partent d'abord ; le dernier et les instantanés nommés restent
  bool retire = false;
  for (std::size_t i = 0; i + 1 < c.size() && occupation() > m_plafond; ++i) {
    if (!c[i].nom.empty()) continue;
    fs::remove(dossierInstantanes(m_dossier) / (c[i].id + ".json"));
    retire = true;
    // Morceaux qui ne servent plus à aucun instantané
    std::set<std::string> utiles;
    for (const auto& x : chronologie()) {
      const J liste = lireListe(dossierInstantanes(m_dossier) / (x.id + ".json"));  // nommée : elle vit pendant la boucle
      for (const auto& h : liste["morceaux"]) utiles.insert(h.get<std::string>());
    }
    std::error_code ec;
    std::vector<fs::path> inutiles;
    for (const auto& e : fs::recursive_directory_iterator(cheminFichier(m_dossier) / "morceaux", ec))
      if (e.is_regular_file(ec) && !utiles.count(utf8(e.path().filename()))) inutiles.push_back(e.path());
    for (const auto& p : inutiles) fs::remove(p, ec);
  }
  (void)retire;
}

}  // namespace prom
