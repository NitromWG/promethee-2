// Prométhée : empreinte cryptographique SHA-256 (FIPS 180-4). Licence GPL-3.0-only.
// Sert à identifier une conception publiée et chacun de ses fichiers de fabrication, et à reconnaître un
// fichier modifié sur le disque.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace prom {

class Sha256 {
public:
  Sha256();
  void ajouter(std::string_view donnees);
  void ajouter(const void* donnees, std::size_t n);
  std::array<std::uint8_t, 32> terminer();
  std::string terminerHex();  // 64 caractères hexadécimaux minuscules

private:
  void bloc(const std::uint8_t* p);
  std::array<std::uint32_t, 8> m_h;
  std::array<std::uint8_t, 64> m_tampon{};
  std::size_t m_rempli = 0;
  std::uint64_t m_longueur = 0;  // en octets
};

std::string sha256Hex(std::string_view donnees);
// HMAC-SHA-256 (RFC 2104), en hexadécimal minuscule : sceau d'un contenu par une clé secrète
std::string hmacSha256Hex(std::string_view cle, std::string_view message);

}  // namespace prom
