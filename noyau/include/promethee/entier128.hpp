// Prométhée : entier signé de 128 bits pour les calculs géométriques exacts (orientation, aire, inclusion) sur des
// coordonnées en nanomètres de 64 bits, dont les produits dépassent 64 bits. GCC et Clang ont __int128 ; MSVC ne
// l'a pas : un type à deux mots, limité à ce que ces calculs demandent (somme, différence, produit, comparaisons),
// le remplace. PROMETHEE_ENTIER128_PORTABLE force ce type partout, pour l'essayer sous Linux.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>

namespace prom {

#if defined(__SIZEOF_INT128__) && !defined(PROMETHEE_ENTIER128_PORTABLE)
__extension__ typedef __int128 Entier128;
#else
class Entier128 {
 public:
  constexpr Entier128() = default;
  constexpr Entier128(std::int64_t v) : m_bas(static_cast<std::uint64_t>(v)), m_haut(v < 0 ? ~std::uint64_t{0} : 0) {}  // NOLINT : conversion voulue

  friend constexpr Entier128 operator+(Entier128 a, Entier128 b) {
    Entier128 r;
    r.m_bas = a.m_bas + b.m_bas;
    r.m_haut = a.m_haut + b.m_haut + (r.m_bas < a.m_bas ? 1 : 0);
    return r;
  }
  friend constexpr Entier128 operator-(Entier128 a, Entier128 b) {
    Entier128 r;
    r.m_bas = a.m_bas - b.m_bas;
    r.m_haut = a.m_haut - b.m_haut - (a.m_bas < b.m_bas ? 1 : 0);
    return r;
  }
  friend constexpr Entier128 operator-(Entier128 a) { return Entier128{} - a; }
  // Produit tronqué à 128 bits, en complément à deux (exact quand le résultat tient sur 128 bits)
  friend constexpr Entier128 operator*(Entier128 a, Entier128 b) {
    Entier128 r = produit(a.m_bas, b.m_bas);
    r.m_haut += a.m_bas * b.m_haut + a.m_haut * b.m_bas;
    return r;
  }
  constexpr Entier128& operator+=(Entier128 b) { return *this = *this + b; }
  constexpr Entier128& operator-=(Entier128 b) { return *this = *this - b; }
  constexpr Entier128& operator*=(Entier128 b) { return *this = *this * b; }

  friend constexpr bool operator==(Entier128 a, Entier128 b) { return a.m_bas == b.m_bas && a.m_haut == b.m_haut; }
  friend constexpr bool operator!=(Entier128 a, Entier128 b) { return !(a == b); }
  friend constexpr bool operator<(Entier128 a, Entier128 b) {
    const auto ha = static_cast<std::int64_t>(a.m_haut), hb = static_cast<std::int64_t>(b.m_haut);
    return ha != hb ? ha < hb : a.m_bas < b.m_bas;
  }
  friend constexpr bool operator>(Entier128 a, Entier128 b) { return b < a; }
  friend constexpr bool operator<=(Entier128 a, Entier128 b) { return !(b < a); }
  friend constexpr bool operator>=(Entier128 a, Entier128 b) { return !(a < b); }

  explicit constexpr operator double() const {
    const bool negatif = static_cast<std::int64_t>(m_haut) < 0;
    const Entier128 v = negatif ? -*this : *this;
    const double d = static_cast<double>(v.m_haut) * 18446744073709551616.0 + static_cast<double>(v.m_bas);
    return negatif ? -d : d;
  }

 private:
  // Produit de deux mots de 64 bits non signés, sur 128 bits, par moitiés de 32 bits
  static constexpr Entier128 produit(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const std::uint64_t milieu = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    Entier128 r;
    r.m_bas = (milieu << 32) | (p00 & 0xFFFFFFFFu);
    r.m_haut = p11 + (p01 >> 32) + (p10 >> 32) + (milieu >> 32);
    return r;
  }
  std::uint64_t m_bas = 0;
  std::uint64_t m_haut = 0;  // mot haut, en complément à deux
};
#endif

}  // namespace prom
