// Prométhée : archive zip sans compression. Licence GPL-3.0-only.
#include "promethee/archive.hpp"

#include <array>

namespace prom {

namespace {
void le16(std::string& s, uint32_t v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>((v >> 8) & 0xFF); }
void le32(std::string& s, uint32_t v) { le16(s, v & 0xFFFF); le16(s, v >> 16); }
}  // namespace

uint32_t crc32(const std::string& s) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char o : s) c = table[(c ^ o) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

std::string zipStocke(const std::vector<std::pair<std::string, std::string>>& fichiers) {
  std::string sortie, central;
  for (const auto& [nom, contenu] : fichiers) {
    const uint32_t crc = crc32(contenu), taille = static_cast<uint32_t>(contenu.size()), decalage = static_cast<uint32_t>(sortie.size());
    std::string entete;
    le32(entete, 0x04034b50); le16(entete, 20); le16(entete, 0x0800); le16(entete, 0); le16(entete, 0); le16(entete, 0x21);
    le32(entete, crc); le32(entete, taille); le32(entete, taille); le16(entete, static_cast<uint32_t>(nom.size())); le16(entete, 0);
    sortie += entete + nom + contenu;
    le32(central, 0x02014b50); le16(central, 20); le16(central, 20); le16(central, 0x0800); le16(central, 0); le16(central, 0); le16(central, 0x21);
    le32(central, crc); le32(central, taille); le32(central, taille); le16(central, static_cast<uint32_t>(nom.size()));
    le16(central, 0); le16(central, 0); le16(central, 0); le16(central, 0); le32(central, 0); le32(central, decalage);
    central += nom;
  }
  const uint32_t debut = static_cast<uint32_t>(sortie.size());
  sortie += central;
  le32(sortie, 0x06054b50); le16(sortie, 0); le16(sortie, 0);
  le16(sortie, static_cast<uint32_t>(fichiers.size())); le16(sortie, static_cast<uint32_t>(fichiers.size()));
  le32(sortie, static_cast<uint32_t>(central.size())); le32(sortie, debut); le16(sortie, 0);
  return sortie;
}

}  // namespace prom
