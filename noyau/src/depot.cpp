// Prométhée : dépôt des géométries importées. Licence GPL-3.0-only.
#include "promethee/depot.hpp"

#include <map>
#include <mutex>

#include "promethee/hachage.hpp"

namespace prom {
namespace {
std::mutex& verrou() {
  static std::mutex v;
  return v;
}
std::map<std::string, std::shared_ptr<const std::string>>& depot() {
  static std::map<std::string, std::shared_ptr<const std::string>> d;
  return d;
}
constexpr const char* PREFIXE = "@sha256:";
}  // namespace

bool estReferenceGeometrie(const std::string& s) { return s.size() == 8 + 64 && s.compare(0, 8, PREFIXE) == 0; }

std::string deposerGeometrie(const std::string& texte) {
  if (texte.size() < TAILLE_DEPOT_MIN || estReferenceGeometrie(texte)) return texte;
  const std::string ref = PREFIXE + sha256Hex(texte);
  const std::lock_guard<std::mutex> g(verrou());
  auto& d = depot();
  if (!d.count(ref)) d[ref] = std::make_shared<const std::string>(texte);
  return ref;
}

std::shared_ptr<const std::string> geometrieDeposee(const std::string& s) {
  if (!estReferenceGeometrie(s)) return std::make_shared<const std::string>(s);
  const std::lock_guard<std::mutex> g(verrou());
  const auto it = depot().find(s);
  return it == depot().end() ? nullptr : it->second;
}

}  // namespace prom
