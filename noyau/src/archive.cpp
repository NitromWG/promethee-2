// Prométhée : archive zip sans compression. Licence GPL-3.0-only.
#include "promethee/archive.hpp"

#include <array>
#include <stdexcept>

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

// ---------- Lecture : DEFLATE (RFC 1951) et archives zip ----------
namespace prom {

namespace {
class Bits {
 public:
  explicit Bits(const std::string& s, std::size_t debut = 0) : m_s(s), m_pos(debut) {}
  int bit() {
    if (m_n == 0) {
      if (m_pos >= m_s.size()) throw std::runtime_error("flux DEFLATE tronqué");
      m_octet = static_cast<unsigned char>(m_s[m_pos++]);
      m_n = 8;
    }
    const int b = m_octet & 1;
    m_octet >>= 1;
    --m_n;
    return b;
  }
  uint32_t bits(int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; ++i) v |= static_cast<uint32_t>(bit()) << i;
    return v;
  }
  void aligner() { m_n = 0; }
  std::size_t position() const { return m_pos; }
  void avancer(std::size_t n) { m_pos += n; }

 private:
  const std::string& m_s;
  std::size_t m_pos;
  unsigned char m_octet = 0;
  int m_n = 0;
};

// Code de Huffman canonique : nombres de codes par longueur et symboles rangés
struct Huffman {
  std::array<uint16_t, 16> compte{};
  std::vector<uint16_t> symboles;
  explicit Huffman(const std::vector<uint8_t>& longueurs) {
    for (uint8_t l : longueurs) ++compte[l];
    compte[0] = 0;
    std::array<uint16_t, 16> offs{};
    for (int i = 1; i < 16; ++i) offs[i] = static_cast<uint16_t>(offs[i - 1] + compte[i - 1]);
    symboles.assign(longueurs.size(), 0);
    for (std::size_t s = 0; s < longueurs.size(); ++s)
      if (longueurs[s]) symboles[offs[longueurs[s]]++] = static_cast<uint16_t>(s);
  }
  int decoder(Bits& b) const {
    int code = 0, premier = 0, index = 0;
    for (int l = 1; l < 16; ++l) {
      code |= b.bit();
      const int n = compte[l];
      if (code - n < premier) return symboles[static_cast<std::size_t>(index + (code - premier))];
      index += n;
      premier += n;
      premier <<= 1;
      code <<= 1;
    }
    throw std::runtime_error("code de Huffman invalide");
  }
};

constexpr uint16_t BASE_LONG[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t EXTRA_LONG[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t BASE_DIST[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t EXTRA_DIST[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void bloc(Bits& b, std::string& sortie, const Huffman& lit, const Huffman& dist) {
  for (;;) {
    const int s = lit.decoder(b);
    if (s < 256) sortie += static_cast<char>(s);
    else if (s == 256) return;
    else {
      const int i = s - 257;
      if (i >= 29) throw std::runtime_error("longueur DEFLATE invalide");
      const std::size_t longueur = BASE_LONG[i] + b.bits(EXTRA_LONG[i]);
      const int d = dist.decoder(b);
      if (d >= 30) throw std::runtime_error("distance DEFLATE invalide");
      const std::size_t distance = BASE_DIST[d] + b.bits(EXTRA_DIST[d]);
      if (distance > sortie.size()) throw std::runtime_error("distance DEFLATE hors du flux");
      const std::size_t depart = sortie.size() - distance;
      for (std::size_t k = 0; k < longueur; ++k) sortie += sortie[depart + k];
    }
  }
}
}  // namespace

std::string inflerBrut(const std::string& donnees) {
  Bits b(donnees);
  std::string sortie;
  for (bool dernier = false; !dernier;) {
    dernier = b.bit();
    const uint32_t type = b.bits(2);
    if (type == 0) {
      b.aligner();
      const std::size_t p = b.position();
      if (p + 4 > donnees.size()) throw std::runtime_error("bloc stocké tronqué");
      const uint16_t len = static_cast<uint16_t>(static_cast<unsigned char>(donnees[p]) | (static_cast<unsigned char>(donnees[p + 1]) << 8));
      if (p + 4 + len > donnees.size()) throw std::runtime_error("bloc stocké tronqué");
      sortie.append(donnees, p + 4, len);
      b.avancer(4 + len);
    } else if (type == 1) {
      std::vector<uint8_t> l(288);
      for (int i = 0; i < 144; ++i) l[static_cast<std::size_t>(i)] = 8;
      for (int i = 144; i < 256; ++i) l[static_cast<std::size_t>(i)] = 9;
      for (int i = 256; i < 280; ++i) l[static_cast<std::size_t>(i)] = 7;
      for (int i = 280; i < 288; ++i) l[static_cast<std::size_t>(i)] = 8;
      static const Huffman fixeLit(l);
      static const Huffman fixeDist(std::vector<uint8_t>(30, 5));
      bloc(b, sortie, fixeLit, fixeDist);
    } else if (type == 2) {
      const int hlit = static_cast<int>(b.bits(5)) + 257, hdist = static_cast<int>(b.bits(5)) + 1, hclen = static_cast<int>(b.bits(4)) + 4;
      static const int ordre[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      std::vector<uint8_t> lc(19, 0);
      for (int i = 0; i < hclen; ++i) lc[static_cast<std::size_t>(ordre[i])] = static_cast<uint8_t>(b.bits(3));
      const Huffman codes(lc);
      std::vector<uint8_t> l;
      while (static_cast<int>(l.size()) < hlit + hdist) {
        const int s = codes.decoder(b);
        if (s < 16) l.push_back(static_cast<uint8_t>(s));
        else if (s == 16) {
          if (l.empty()) throw std::runtime_error("répétition sans longueur");
          const uint8_t prec = l.back();
          for (uint32_t k = 3 + b.bits(2); k > 0; --k) l.push_back(prec);
        } else if (s == 17)
          for (uint32_t k = 3 + b.bits(3); k > 0; --k) l.push_back(0);
        else
          for (uint32_t k = 11 + b.bits(7); k > 0; --k) l.push_back(0);
      }
      l.resize(static_cast<std::size_t>(hlit + hdist));
      const Huffman lit(std::vector<uint8_t>(l.begin(), l.begin() + hlit)), dist(std::vector<uint8_t>(l.begin() + hlit, l.end()));
      bloc(b, sortie, lit, dist);
    } else
      throw std::runtime_error("bloc DEFLATE de type inconnu");
  }
  return sortie;
}

std::vector<std::pair<std::string, std::string>> lireZip(const std::string& octets) {
  auto u16 = [&](std::size_t p) -> uint32_t { return p + 2 <= octets.size() ? static_cast<unsigned char>(octets[p]) | (static_cast<unsigned char>(octets[p + 1]) << 8) : 0; };
  auto u32 = [&](std::size_t p) -> uint32_t { return u16(p) | (u16(p + 2) << 16); };
  // Fin du répertoire central, cherchée depuis la fin (un commentaire peut la suivre)
  std::size_t fin = std::string::npos;
  for (std::size_t p = octets.size() >= 22 ? octets.size() - 22 : 0; p != std::string::npos && p + 22 <= octets.size(); --p) {
    if (u32(p) == 0x06054b50) {
      fin = p;
      break;
    }
    if (p == 0) break;
  }
  if (fin == std::string::npos) throw std::runtime_error("archive zip illisible : pas de répertoire central");
  const uint32_t nombre = u16(fin + 10);
  std::size_t p = u32(fin + 16);
  std::vector<std::pair<std::string, std::string>> r;
  for (uint32_t i = 0; i < nombre; ++i) {
    if (u32(p) != 0x02014b50) throw std::runtime_error("répertoire central abîmé");
    const uint32_t methode = u16(p + 10), taille = u32(p + 20), tailleReelle = u32(p + 24);
    const uint32_t lNom = u16(p + 28), lExtra = u16(p + 30), lCom = u16(p + 32), local = u32(p + 42);
    const std::string nom = octets.substr(p + 46, lNom);
    p += 46 + lNom + lExtra + lCom;
    if (u32(local) != 0x04034b50) throw std::runtime_error("entrée locale abîmée : " + nom);
    const std::size_t donnees = local + 30 + u16(local + 26) + u16(local + 28);
    if (donnees + taille > octets.size()) throw std::runtime_error("entrée tronquée : " + nom);
    if (!nom.empty() && nom.back() == '/') continue;  // dossier
    const std::string brut = octets.substr(donnees, taille);
    std::string contenu;
    if (methode == 0) contenu = brut;
    else if (methode == 8) contenu = inflerBrut(brut);
    else throw std::runtime_error("méthode de compression non prise en charge (" + std::to_string(methode) + ") : " + nom);
    if (contenu.size() != tailleReelle) throw std::runtime_error("taille inattendue après décompression : " + nom);
    r.push_back({nom, contenu});
  }
  return r;
}

}  // namespace prom
