// Prométhée : SHA-256 (FIPS 180-4). Licence GPL-3.0-only.
#include "promethee/hachage.hpp"

#include <algorithm>
#include <cstring>

namespace prom {

namespace {

constexpr std::array<std::uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74,
    0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d,
    0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e,
    0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotd(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

}  // namespace

Sha256::Sha256() : m_h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

void Sha256::bloc(const std::uint8_t* p) {
  std::array<std::uint32_t, 64> w{};
  for (int i = 0; i < 16; ++i)
    w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) | (std::uint32_t(p[4 * i + 2]) << 8) | std::uint32_t(p[4 * i + 3]);
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotd(w[i - 15], 7) ^ rotd(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = rotd(w[i - 2], 17) ^ rotd(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
  for (int i = 0; i < 64; ++i) {
    const std::uint32_t S1 = rotd(e, 6) ^ rotd(e, 11) ^ rotd(e, 25);
    const std::uint32_t ch = (e & f) ^ (~e & g);
    const std::uint32_t t1 = h + S1 + ch + K[i] + w[i];
    const std::uint32_t S0 = rotd(a, 2) ^ rotd(a, 13) ^ rotd(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t t2 = S0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  m_h[0] += a;
  m_h[1] += b;
  m_h[2] += c;
  m_h[3] += d;
  m_h[4] += e;
  m_h[5] += f;
  m_h[6] += g;
  m_h[7] += h;
}

void Sha256::ajouter(const void* donnees, std::size_t n) {
  const auto* p = static_cast<const std::uint8_t*>(donnees);
  m_longueur += n;
  while (n > 0) {
    const std::size_t pris = std::min<std::size_t>(n, 64 - m_rempli);
    std::memcpy(m_tampon.data() + m_rempli, p, pris);
    m_rempli += pris;
    p += pris;
    n -= pris;
    if (m_rempli == 64) {
      bloc(m_tampon.data());
      m_rempli = 0;
    }
  }
}

void Sha256::ajouter(std::string_view donnees) { ajouter(donnees.data(), donnees.size()); }

std::array<std::uint8_t, 32> Sha256::terminer() {
  const std::uint64_t bits = m_longueur * 8;
  const std::uint8_t un = 0x80, zero = 0;
  ajouter(&un, 1);
  while (m_rempli != 56) ajouter(&zero, 1);
  std::uint8_t longueur[8];
  for (int i = 0; i < 8; ++i) longueur[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
  ajouter(longueur, 8);
  std::array<std::uint8_t, 32> r{};
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 4; ++j) r[4 * i + j] = static_cast<std::uint8_t>(m_h[i] >> (24 - 8 * j));
  return r;
}

std::string Sha256::terminerHex() {
  static const char* chiffres = "0123456789abcdef";
  std::string s;
  for (std::uint8_t o : terminer()) {
    s += chiffres[o >> 4];
    s += chiffres[o & 15];
  }
  return s;
}

std::string sha256Hex(std::string_view donnees) {
  Sha256 h;
  h.ajouter(donnees);
  return h.terminerHex();
}

std::string hmacSha256Hex(std::string_view cle, std::string_view message) {
  // RFC 2104 : clé ramenée à un bloc de 64 octets, H((K ⊕ opad) ‖ H((K ⊕ ipad) ‖ message))
  std::array<std::uint8_t, 64> k{};
  if (cle.size() > 64) {
    Sha256 h;
    h.ajouter(cle);
    const auto d = h.terminer();
    std::copy(d.begin(), d.end(), k.begin());
  } else
    std::copy(cle.begin(), cle.end(), k.begin());
  std::array<std::uint8_t, 64> ipad{}, opad{};
  for (std::size_t i = 0; i < 64; ++i) ipad[i] = static_cast<std::uint8_t>(k[i] ^ 0x36), opad[i] = static_cast<std::uint8_t>(k[i] ^ 0x5c);
  Sha256 interieur;
  interieur.ajouter(ipad.data(), ipad.size());
  interieur.ajouter(message);
  const auto d = interieur.terminer();
  Sha256 exterieur;
  exterieur.ajouter(opad.data(), opad.size());
  exterieur.ajouter(d.data(), d.size());
  return exterieur.terminerHex();
}

}  // namespace prom
