// Prométhée : archive zip, flux zlib, fichiers composés CFB. Licence GPL-3.0-only.
#include "promethee/archive.hpp"

#include <array>
#include <cstring>
#include <functional>
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

// ======================================================================================================================
// Flux zlib et fichiers composés (CFB)
namespace prom {

std::string inflerZlib(const std::string& donnees) {
  if (donnees.size() < 2 || (static_cast<unsigned char>(donnees[0]) & 0x0F) != 8) throw std::runtime_error("flux zlib invalide");
  return inflerBrut(donnees.substr(2));
}

namespace {
constexpr std::uint32_t CFB_FIN = 0xFFFFFFFE, CFB_LIBRE = 0xFFFFFFFF, CFB_FAT = 0xFFFFFFFD;
std::uint16_t lu16(const std::string& s, std::size_t p) {
  if (p + 2 > s.size()) throw std::runtime_error("fichier composé tronqué");
  return static_cast<std::uint16_t>(static_cast<unsigned char>(s[p]) | static_cast<unsigned char>(s[p + 1]) << 8);
}
std::uint32_t lu32(const std::string& s, std::size_t p) {
  if (p + 4 > s.size()) throw std::runtime_error("fichier composé tronqué");
  return static_cast<std::uint32_t>(static_cast<unsigned char>(s[p])) | static_cast<std::uint32_t>(static_cast<unsigned char>(s[p + 1])) << 8 |
         static_cast<std::uint32_t>(static_cast<unsigned char>(s[p + 2])) << 16 | static_cast<std::uint32_t>(static_cast<unsigned char>(s[p + 3])) << 24;
}
void ec16(std::string& s, std::size_t p, std::uint16_t v) {
  s[p] = static_cast<char>(v & 0xFF);
  s[p + 1] = static_cast<char>(v >> 8);
}
void ec32(std::string& s, std::size_t p, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) s[p + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF);
}
}  // namespace

bool estCfb(const std::string& o) { return o.size() >= 512 && o.compare(0, 8, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8) == 0; }

std::vector<std::pair<std::string, std::string>> lireCfb(const std::string& o) {
  if (!estCfb(o)) throw std::runtime_error("ce n'est pas un fichier composé (CFB)");
  const unsigned decalage = lu16(o, 0x1E), decalageMini = lu16(o, 0x20);
  if (decalage < 7 || decalage > 16 || decalageMini > decalage) throw std::runtime_error("taille de secteur invalide");
  const std::size_t secteur = std::size_t{1} << decalage, mini = std::size_t{1} << decalageMini;
  const std::uint32_t nFat = lu32(o, 0x2C), premierRep = lu32(o, 0x30), coupure = lu32(o, 0x38), premierMiniFat = lu32(o, 0x3C), premierDifat = lu32(o, 0x44);
  auto debut = [&](std::uint32_t n) { return (static_cast<std::size_t>(n) + 1) * secteur; };
  // Secteurs de la FAT : 109 dans l'en-tête, la suite dans la chaîne DIFAT
  std::vector<std::uint32_t> secteursFat;
  for (int i = 0; i < 109 && secteursFat.size() < nFat; ++i) {
    const std::uint32_t s = lu32(o, 0x4C + 4 * static_cast<std::size_t>(i));
    if (s < CFB_FAT) secteursFat.push_back(s);
  }
  for (std::uint32_t d = premierDifat, garde = 0; d < CFB_FAT && secteursFat.size() < nFat && garde < 1u << 20; ++garde) {
    const std::size_t p = debut(d);
    for (std::size_t i = 0; i + 1 < secteur / 4 && secteursFat.size() < nFat; ++i) {
      const std::uint32_t s = lu32(o, p + 4 * i);
      if (s < CFB_FAT) secteursFat.push_back(s);
    }
    d = lu32(o, p + secteur - 4);
  }
  std::vector<std::uint32_t> fat;
  fat.reserve(secteursFat.size() * secteur / 4);
  for (const std::uint32_t s : secteursFat) {
    const std::size_t p = debut(s);
    if (p + secteur > o.size()) throw std::runtime_error("FAT hors du fichier");
    for (std::size_t i = 0; i < secteur / 4; ++i) fat.push_back(lu32(o, p + 4 * i));
  }
  auto chaine = [&](std::uint32_t s, std::uint64_t taille) {
    std::string r;
    std::size_t tours = 0;
    while (s < CFB_FAT && r.size() < taille) {
      if (s >= fat.size() || ++tours > fat.size()) throw std::runtime_error("chaîne de secteurs abîmée");
      const std::size_t p = debut(s);
      if (p >= o.size()) throw std::runtime_error("secteur hors du fichier");
      r.append(o, p, std::min(secteur, o.size() - p));
      s = fat[s];
    }
    if (r.size() > taille) r.resize(static_cast<std::size_t>(taille));
    return r;
  };
  const std::string repertoire = chaine(premierRep, ~std::uint64_t{0} >> 1);
  struct Entree {
    std::string nom;
    int type = 0;
    std::uint32_t gauche = CFB_LIBRE, droite = CFB_LIBRE, enfant = CFB_LIBRE, depart = 0;
    std::uint64_t taille = 0;
  };
  std::vector<Entree> entrees;
  for (std::size_t p = 0; p + 128 <= repertoire.size(); p += 128) {
    Entree e;
    const std::size_t longueur = std::min<std::size_t>(lu16(repertoire, p + 64), 64);
    // Nom en UTF-16LE (ASCII en pratique ; le reste passe en UTF-8)
    for (std::size_t i = 0; i + 1 < longueur; i += 2) {
      const std::uint16_t ch = lu16(repertoire, p + i);
      if (ch == 0) break;
      if (ch < 0x80) e.nom += static_cast<char>(ch);
      else if (ch < 0x800) e.nom += static_cast<char>(0xC0 | (ch >> 6)), e.nom += static_cast<char>(0x80 | (ch & 0x3F));
      else e.nom += static_cast<char>(0xE0 | (ch >> 12)), e.nom += static_cast<char>(0x80 | ((ch >> 6) & 0x3F)), e.nom += static_cast<char>(0x80 | (ch & 0x3F));
    }
    e.type = static_cast<unsigned char>(repertoire[p + 66]);
    e.gauche = lu32(repertoire, p + 68);
    e.droite = lu32(repertoire, p + 72);
    e.enfant = lu32(repertoire, p + 76);
    e.depart = lu32(repertoire, p + 116);
    e.taille = lu32(repertoire, p + 120) | (decalage == 12 ? static_cast<std::uint64_t>(lu32(repertoire, p + 124)) << 32 : 0);
    entrees.push_back(e);
  }
  if (entrees.empty() || entrees[0].type != 5) throw std::runtime_error("entrée racine absente");
  // Mini-flux (porté par la racine) et mini-FAT
  const std::string miniFlux = chaine(entrees[0].depart, entrees[0].taille);
  std::vector<std::uint32_t> miniFat;
  {
    const std::string m = chaine(premierMiniFat, ~std::uint64_t{0} >> 1);
    for (std::size_t i = 0; i + 4 <= m.size(); i += 4) miniFat.push_back(lu32(m, i));
  }
  auto chaineMini = [&](std::uint32_t s, std::uint64_t taille) {
    std::string r;
    std::size_t tours = 0;
    while (s < CFB_FAT && r.size() < taille) {
      if (s >= miniFat.size() || ++tours > miniFat.size()) throw std::runtime_error("chaîne du mini-flux abîmée");
      const std::size_t p = static_cast<std::size_t>(s) * mini;
      if (p >= miniFlux.size()) throw std::runtime_error("mini-secteur hors du mini-flux");
      r.append(miniFlux, p, std::min(mini, miniFlux.size() - p));
      s = miniFat[s];
    }
    if (r.size() > taille) r.resize(static_cast<std::size_t>(taille));
    return r;
  };
  // Parcours de l'arbre : frères à gauche et à droite, enfants des dossiers
  std::vector<std::pair<std::string, std::string>> r;
  std::vector<char> vus(entrees.size(), 0);
  std::function<void(std::uint32_t, const std::string&)> parcourir = [&](std::uint32_t i, const std::string& dossier) {
    if (i >= entrees.size() || vus[i]) return;
    vus[i] = 1;
    const Entree& e = entrees[i];
    parcourir(e.gauche, dossier);
    const std::string chemin = dossier.empty() ? e.nom : dossier + "/" + e.nom;
    if (e.type == 2) r.push_back({chemin, e.taille < coupure ? chaineMini(e.depart, e.taille) : chaine(e.depart, e.taille)});
    else if (e.type == 1) parcourir(e.enfant, chemin);
    parcourir(e.droite, dossier);
  };
  parcourir(entrees[0].enfant, "");
  return r;
}

std::string ecrireCfb(const std::vector<std::pair<std::string, std::string>>& flux) {
  // Arbre des dossiers et des flux ; chaque niveau chaîné par ses frères de droite (arbre dégénéré, tout en noir)
  struct Noeud {
    std::string nom;
    int type = 1;
    std::vector<std::size_t> enfants;
    const std::string* donnees = nullptr;
  };
  std::vector<Noeud> noeuds{{"Root Entry", 5, {}, nullptr}};
  for (const auto& [chemin, contenu] : flux) {
    std::size_t courant = 0, p = 0;
    while (true) {
      const std::size_t q = chemin.find('/', p);
      const std::string morceau = chemin.substr(p, q == std::string::npos ? std::string::npos : q - p);
      const bool dernier = q == std::string::npos;
      std::size_t trouve = SIZE_MAX;
      for (const std::size_t e : noeuds[courant].enfants)
        if (noeuds[e].nom == morceau) trouve = e;
      if (trouve == SIZE_MAX) {
        noeuds.push_back({morceau, dernier ? 2 : 1, {}, dernier ? &contenu : nullptr});
        trouve = noeuds.size() - 1;
        noeuds[courant].enfants.push_back(trouve);
      }
      if (dernier) break;
      courant = trouve;
      p = q + 1;
    }
  }
  constexpr std::size_t S = 512, M = 64, COUPURE = 4096;
  // Mini-flux : les flux de moins de 4096 octets, en mini-secteurs de 64 octets
  std::string miniFlux;
  std::vector<std::uint32_t> miniFat;
  std::vector<std::uint32_t> departs(noeuds.size(), CFB_FIN);
  for (std::size_t i = 1; i < noeuds.size(); ++i) {
    if (noeuds[i].type != 2 || noeuds[i].donnees->size() >= COUPURE || noeuds[i].donnees->empty()) continue;
    const std::string& d = *noeuds[i].donnees;
    const std::size_t n = (d.size() + M - 1) / M;
    departs[i] = static_cast<std::uint32_t>(miniFat.size());
    for (std::size_t k = 0; k < n; ++k) miniFat.push_back(k + 1 < n ? static_cast<std::uint32_t>(miniFat.size() + 1) : CFB_FIN);
    miniFlux += d;
    miniFlux.resize(miniFat.size() * M, '\0');
  }
  // Secteurs ordinaires : grands flux, mini-flux, mini-FAT, répertoire, puis la FAT
  std::vector<std::string> secteurs;
  std::vector<std::uint32_t> fat;
  auto poser = [&](const std::string& d) -> std::uint32_t {
    if (d.empty()) return CFB_FIN;
    const std::uint32_t premier = static_cast<std::uint32_t>(secteurs.size());
    const std::size_t n = (d.size() + S - 1) / S;
    for (std::size_t k = 0; k < n; ++k) {
      std::string s = d.substr(k * S, S);
      s.resize(S, '\0');
      secteurs.push_back(s);
      fat.push_back(k + 1 < n ? static_cast<std::uint32_t>(secteurs.size()) : CFB_FIN);
    }
    return premier;
  };
  for (std::size_t i = 1; i < noeuds.size(); ++i)
    if (noeuds[i].type == 2 && noeuds[i].donnees->size() >= COUPURE) departs[i] = poser(*noeuds[i].donnees);
  departs[0] = poser(miniFlux);
  std::string miniFatOctets(miniFat.size() * 4, '\0');
  for (std::size_t i = 0; i < miniFat.size(); ++i) ec32(miniFatOctets, 4 * i, miniFat[i]);
  if (!miniFatOctets.empty()) miniFatOctets.resize((miniFatOctets.size() + S - 1) / S * S, '\xFF');
  const std::uint32_t premierMiniFat = poser(miniFatOctets);
  std::string rep(noeuds.size() * 128, '\0');
  for (std::size_t i = 0; i < noeuds.size(); ++i) {
    const std::size_t p = 128 * i;
    const Noeud& n = noeuds[i];
    std::size_t k = 0;
    for (const char ch : n.nom) {
      if (k >= 31) break;
      ec16(rep, p + 2 * k++, static_cast<unsigned char>(ch));
    }
    ec16(rep, p + 64, static_cast<std::uint16_t>(2 * (k + 1)));
    rep[p + 66] = static_cast<char>(n.type);
    rep[p + 67] = 1;  // noir
    ec32(rep, p + 68, CFB_LIBRE);
    ec32(rep, p + 72, CFB_LIBRE);
    ec32(rep, p + 76, n.enfants.empty() ? CFB_LIBRE : static_cast<std::uint32_t>(n.enfants.front()));
    ec32(rep, p + 116, n.type == 1 ? 0 : departs[i]);
    ec32(rep, p + 120, n.type == 5 ? static_cast<std::uint32_t>(miniFlux.size()) : n.type == 2 ? static_cast<std::uint32_t>(n.donnees->size()) : 0);
  }
  // Frères d'un même dossier chaînés par leur pointeur de droite
  for (std::size_t i = 0; i < noeuds.size(); ++i)
    for (std::size_t j = 0; j + 1 < noeuds[i].enfants.size(); ++j) ec32(rep, 128 * noeuds[i].enfants[j] + 72, static_cast<std::uint32_t>(noeuds[i].enfants[j + 1]));
  rep.resize((rep.size() + S - 1) / S * S, '\0');
  for (std::size_t p = 128 * noeuds.size(); p + 128 <= rep.size(); p += 128) {
    ec32(rep, p + 68, CFB_LIBRE), ec32(rep, p + 72, CFB_LIBRE), ec32(rep, p + 76, CFB_LIBRE);
  }
  const std::uint32_t premierRep = poser(rep);
  // FAT : elle se compte elle-même (secteurs marqués FATSECT)
  std::size_t nFat = 1;
  while ((fat.size() + nFat) > nFat * (S / 4)) ++nFat;
  if (nFat > 109) throw std::runtime_error("fichier composé trop grand pour l'écrivain");
  const std::uint32_t premierFat = static_cast<std::uint32_t>(secteurs.size());
  for (std::size_t k = 0; k < nFat; ++k) fat.push_back(CFB_FAT);
  fat.resize(nFat * (S / 4), CFB_LIBRE);
  for (std::size_t k = 0; k < nFat; ++k) {
    std::string s(S, '\0');
    for (std::size_t i = 0; i < S / 4; ++i) ec32(s, 4 * i, fat[k * (S / 4) + i]);
    secteurs.push_back(s);
  }
  std::string en(S, '\0');
  std::memcpy(en.data(), "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
  ec16(en, 0x18, 0x003E);
  ec16(en, 0x1A, 0x0003);
  ec16(en, 0x1C, 0xFFFE);
  ec16(en, 0x1E, 9);
  ec16(en, 0x20, 6);
  ec32(en, 0x2C, static_cast<std::uint32_t>(nFat));
  ec32(en, 0x30, premierRep);
  ec32(en, 0x38, COUPURE);
  ec32(en, 0x3C, premierMiniFat);
  ec32(en, 0x40, static_cast<std::uint32_t>(miniFatOctets.size() / S));
  ec32(en, 0x44, CFB_FIN);
  for (std::size_t i = 0; i < 109; ++i) ec32(en, 0x4C + 4 * i, i < nFat ? premierFat + static_cast<std::uint32_t>(i) : CFB_LIBRE);
  std::string r = en;
  for (const auto& s : secteurs) r += s;
  return r;
}

}  // namespace prom
