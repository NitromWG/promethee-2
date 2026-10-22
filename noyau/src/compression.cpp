// Prométhée : compression DEFLATE à codes fixes et archive zip compressée. Licence GPL-3.0-only.
#include "promethee/compression.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include "promethee/archive.hpp"

namespace prom {

namespace {
// Bits écrits du poids faible au poids fort, comme DEFLATE les lit
class Ecrivain {
 public:
  void bits(std::uint32_t v, int n) {
    m_acc |= static_cast<std::uint64_t>(v) << m_n;
    m_n += n;
    while (m_n >= 8) {
      m_s.push_back(static_cast<char>(m_acc & 0xFF));
      m_acc >>= 8;
      m_n -= 8;
    }
  }
  // Code de Huffman : écrit du bit de poids fort au bit de poids faible
  void code(std::uint32_t c, int n) {
    std::uint32_t r = 0;
    for (int i = 0; i < n; ++i) r |= ((c >> i) & 1u) << (n - 1 - i);
    bits(r, n);
  }
  std::string finir() {
    if (m_n > 0) m_s.push_back(static_cast<char>(m_acc & 0xFF));
    m_acc = 0;
    m_n = 0;
    return std::move(m_s);
  }

 private:
  std::string m_s;
  std::uint64_t m_acc = 0;
  int m_n = 0;
};

void litteral(Ecrivain& e, int v) {
  if (v <= 143) e.code(0x30 + static_cast<std::uint32_t>(v), 8);
  else if (v <= 255) e.code(0x190 + static_cast<std::uint32_t>(v - 144), 9);
  else if (v <= 279) e.code(static_cast<std::uint32_t>(v - 256), 7);
  else e.code(0xC0 + static_cast<std::uint32_t>(v - 280), 8);
}

constexpr std::array<int, 29> BASE_LONGUEUR = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<int, 29> EXTRA_LONGUEUR = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<int, 30> BASE_DISTANCE = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<int, 30> EXTRA_DISTANCE = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void repetition(Ecrivain& e, int longueur, int distance) {
  int i = 28;
  while (BASE_LONGUEUR[static_cast<std::size_t>(i)] > longueur) --i;
  litteral(e, 257 + i);
  if (EXTRA_LONGUEUR[static_cast<std::size_t>(i)]) e.bits(static_cast<std::uint32_t>(longueur - BASE_LONGUEUR[static_cast<std::size_t>(i)]), EXTRA_LONGUEUR[static_cast<std::size_t>(i)]);
  int j = 29;
  while (BASE_DISTANCE[static_cast<std::size_t>(j)] > distance) --j;
  e.code(static_cast<std::uint32_t>(j), 5);
  if (EXTRA_DISTANCE[static_cast<std::size_t>(j)]) e.bits(static_cast<std::uint32_t>(distance - BASE_DISTANCE[static_cast<std::size_t>(j)]), EXTRA_DISTANCE[static_cast<std::size_t>(j)]);
}

void le16(std::string& s, std::uint32_t v) {
  s.push_back(static_cast<char>(v & 0xFF));
  s.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void le32(std::string& s, std::uint32_t v) {
  le16(s, v & 0xFFFF);
  le16(s, v >> 16);
}
}  // namespace

std::string deflerBrut(const std::string& d) {
  constexpr int FENETRE = 32768, MIN = 3, MAX = 258, ESSAIS = 48, BITS_HACHE = 15;
  const int n = static_cast<int>(d.size());
  const auto octet = [&d](int i) { return static_cast<unsigned char>(d[static_cast<std::size_t>(i)]); };
  std::vector<int> tete(std::size_t{1} << BITS_HACHE, -1), precedent(static_cast<std::size_t>(std::max(n, 1)), -1);
  const auto hache = [&](int i) { return ((static_cast<std::uint32_t>(octet(i)) << 10) ^ (static_cast<std::uint32_t>(octet(i + 1)) << 5) ^ octet(i + 2)) & ((1u << BITS_HACHE) - 1); };
  const auto inserer = [&](int i) {
    if (i + 2 >= n) return;
    const std::uint32_t h = hache(i);
    precedent[static_cast<std::size_t>(i)] = tete[h];
    tete[h] = i;
  };
  // Plus longue répétition qui commence en i, dans la fenêtre
  const auto chercher = [&](int i, int& distance) {
    int meilleure = 0;
    if (i + 2 >= n) return 0;
    const int limite = std::min(MAX, n - i);
    int essais = ESSAIS;
    for (int j = tete[hache(i)]; j >= 0 && i - j <= FENETRE && essais-- > 0; j = precedent[static_cast<std::size_t>(j)]) {
      if (octet(j + meilleure) != octet(i + meilleure)) continue;
      int l = 0;
      while (l < limite && d[static_cast<std::size_t>(j + l)] == d[static_cast<std::size_t>(i + l)]) ++l;
      if (l > meilleure) {
        meilleure = l;
        distance = i - j;
        if (l == limite) break;
      }
    }
    return meilleure >= MIN ? meilleure : 0;
  };
  Ecrivain e;
  e.bits(1, 1);  // dernier bloc
  e.bits(1, 2);  // codes fixes
  int i = 0;
  while (i < n) {
    int distance = 0;
    int l = chercher(i, distance);
    if (l > 0 && i + 1 < n) {
      // Évaluation paresseuse : une répétition plus longue un octet plus loin vaut mieux
      int d2 = 0;
      inserer(i);
      const int l2 = chercher(i + 1, d2);
      if (l2 > l + 1) {
        litteral(e, octet(i));
        ++i;
        l = l2;
        distance = d2;
      } else {
        repetition(e, l, distance);
        for (int k = 1; k < l; ++k) inserer(i + k);
        i += l;
        continue;
      }
    }
    if (l > 0) {
      repetition(e, l, distance);
      for (int k = 0; k < l; ++k) inserer(i + k);
      i += l;
    } else {
      litteral(e, octet(i));
      inserer(i);
      ++i;
    }
  }
  litteral(e, 256);
  return e.finir();
}

std::string zipComprime(const std::vector<std::pair<std::string, std::string>>& fichiers) {
  std::string sortie, central;
  for (const auto& [nom, contenu] : fichiers) {
    const std::string compresse = deflerBrut(contenu);
    const bool defle = compresse.size() < contenu.size();
    const std::string& corps = defle ? compresse : contenu;
    const std::uint32_t crc = crc32(contenu), taille = static_cast<std::uint32_t>(contenu.size()), tailleCorps = static_cast<std::uint32_t>(corps.size()),
                        decalage = static_cast<std::uint32_t>(sortie.size()), methode = defle ? 8 : 0;
    std::string entete;
    le32(entete, 0x04034b50);
    le16(entete, 20);
    le16(entete, 0x0800);
    le16(entete, methode);
    le16(entete, 0);
    le16(entete, 0x21);
    le32(entete, crc);
    le32(entete, tailleCorps);
    le32(entete, taille);
    le16(entete, static_cast<std::uint32_t>(nom.size()));
    le16(entete, 0);
    sortie += entete + nom + corps;
    le32(central, 0x02014b50);
    le16(central, 20);
    le16(central, 20);
    le16(central, 0x0800);
    le16(central, methode);
    le16(central, 0);
    le16(central, 0x21);
    le32(central, crc);
    le32(central, tailleCorps);
    le32(central, taille);
    le16(central, static_cast<std::uint32_t>(nom.size()));
    le16(central, 0);
    le16(central, 0);
    le16(central, 0);
    le16(central, 0);
    le32(central, 0);
    le32(central, decalage);
    central += nom;
  }
  const std::uint32_t debut = static_cast<std::uint32_t>(sortie.size());
  sortie += central;
  le32(sortie, 0x06054b50);
  le16(sortie, 0);
  le16(sortie, 0);
  le16(sortie, static_cast<std::uint32_t>(fichiers.size()));
  le16(sortie, static_cast<std::uint32_t>(fichiers.size()));
  le32(sortie, static_cast<std::uint32_t>(central.size()));
  le32(sortie, debut);
  le16(sortie, 0);
  return sortie;
}

}  // namespace prom
