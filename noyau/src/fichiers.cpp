// Prométhée : lecture et écriture sûres des fichiers. Licence GPL-3.0-only.
#include "promethee/fichiers.hpp"

#include <cctype>

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

#include "promethee/hachage.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace prom {

namespace essais {
std::function<void(const fs::path&)> g_avantRenommage;
}

fs::path cheminFichier(std::string_view s) {
  std::u8string u(s.size(), u8'\0');
  for (std::size_t i = 0; i < s.size(); ++i) u[i] = static_cast<char8_t>(s[i]);
  return fs::path(u);
}

std::string utf8(const fs::path& p) {
  const std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}

namespace {

std::string nomAffiche(const fs::path& p) { return "« " + utf8(p.filename()) + " »"; }

#ifdef _WIN32
std::string causeSysteme(DWORD code) {
  switch (code) {
    case ERROR_ACCESS_DENIED: return "accès refusé (fichier protégé, en lecture seule ou ouvert par un autre programme)";
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION: return "fichier utilisé par un autre programme";
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL: return "disque plein";
    case ERROR_PATH_NOT_FOUND: return "dossier introuvable";
    case ERROR_WRITE_PROTECT: return "support protégé en écriture";
    case ERROR_NETWORK_UNREACHABLE:
    case ERROR_BAD_NETPATH:
    case ERROR_UNEXP_NET_ERR: return "chemin réseau interrompu";
    default: break;
  }
  return "erreur système " + std::to_string(code);
}
#else
std::string causeSysteme(int code) {
  switch (code) {
    case EACCES:
    case EPERM: return "accès refusé (droits insuffisants ou fichier en lecture seule)";
    case ENOSPC: return "disque plein";
    case EDQUOT: return "quota de disque dépassé";
    case EROFS: return "support en lecture seule";
    case ENOENT: return "dossier introuvable";
    case EIO: return "erreur d’entrée-sortie du support";
    default: break;
  }
  return std::strerror(code);
}
#endif

fs::path nomTemporaire(const fs::path& cible) {
  static std::mt19937_64 alea{std::random_device{}()};
  char suffixe[24];
  std::snprintf(suffixe, sizeof suffixe, ".%016llx.tmp", static_cast<unsigned long long>(alea()));
  fs::path t = cible;
  t.replace_filename(u8"." + cible.filename().u8string() + std::u8string(suffixe, suffixe + std::strlen(suffixe)));
  return t;
}

}  // namespace

std::string lireFichier(const fs::path& p) {
  std::error_code ec;
  if (!fs::exists(p, ec)) throw ErreurFichier("Le fichier " + nomAffiche(p) + " est introuvable.");
  if (fs::is_directory(p, ec)) throw ErreurFichier(nomAffiche(p) + " est un dossier, pas un fichier.");
  std::ifstream f(p, std::ios::binary);
  if (!f) throw ErreurFichier("Impossible d’ouvrir " + nomAffiche(p) + " : accès refusé ou fichier verrouillé.");
  std::ostringstream tampon;
  tampon << f.rdbuf();
  if (f.bad()) throw ErreurFichier("Lecture interrompue de " + nomAffiche(p) + " (erreur du support).");
  return tampon.str();
}

void ecrireFichierAtomique(const fs::path& p, std::string_view contenu) {
  const fs::path dossier = p.has_parent_path() ? p.parent_path() : fs::path(".");
  std::error_code ec;
  if (!fs::is_directory(dossier, ec)) throw ErreurFichier("Enregistrement impossible de " + nomAffiche(p) + " : dossier introuvable.");
  const fs::path tmp = nomTemporaire(p);
  auto echec = [&](const std::string& cause) {
    std::error_code e2;
    fs::remove(tmp, e2);
    throw ErreurFichier("Enregistrement impossible de " + nomAffiche(p) + " : " + cause + ". Le fichier précédent n’a pas été modifié.");
  };
#ifdef _WIN32
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) echec(causeSysteme(GetLastError()));
  std::size_t ecrit = 0;
  while (ecrit < contenu.size()) {
    const DWORD morceau = static_cast<DWORD>(std::min<std::size_t>(contenu.size() - ecrit, 1u << 24));
    DWORD n = 0;
    if (!WriteFile(h, contenu.data() + ecrit, morceau, &n, nullptr) || n == 0) {
      const DWORD e = GetLastError();
      CloseHandle(h);
      echec(causeSysteme(e));
    }
    ecrit += n;
  }
  if (!FlushFileBuffers(h)) {
    const DWORD e = GetLastError();
    CloseHandle(h);
    echec(causeSysteme(e));
  }
  CloseHandle(h);
#else
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
  if (fd < 0) echec(causeSysteme(errno));
  std::size_t ecrit = 0;
  while (ecrit < contenu.size()) {
    const ssize_t n = ::write(fd, contenu.data() + ecrit, contenu.size() - ecrit);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      const int e = n < 0 ? errno : ENOSPC;
      ::close(fd);
      echec(causeSysteme(e));
    }
    ecrit += static_cast<std::size_t>(n);
  }
  if (::fsync(fd) != 0) {
    const int e = errno;
    ::close(fd);
    echec(causeSysteme(e));
  }
  if (::close(fd) != 0) echec(causeSysteme(errno));
#endif
  if (essais::g_avantRenommage) {
    try {
      essais::g_avantRenommage(tmp);
    } catch (...) {
      std::error_code e2;
      fs::remove(tmp, e2);
      throw;
    }
  }
#ifdef _WIN32
  // ReplaceFileW garde les attributs et les droits de l'ancien fichier ; MoveFileExW crée ou remplace.
  bool fait = false;
  if (fs::exists(p, ec)) fait = ReplaceFileW(p.c_str(), tmp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != 0;
  if (!fait && !MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) echec(causeSysteme(GetLastError()));
#else
  if (std::rename(tmp.c_str(), p.c_str()) != 0) echec(causeSysteme(errno));
  // Le renommage lui-même doit atteindre le disque.
  const int d = ::open(dossier.c_str(), O_RDONLY | O_CLOEXEC);
  if (d >= 0) {
    ::fsync(d);
    ::close(d);
  }
#endif
}

EtatDisque etatDisque(const fs::path& p) {
  EtatDisque e;
  std::error_code ec;
  if (!fs::is_regular_file(p, ec)) return e;
  e.existe = true;
  e.taille = fs::file_size(p, ec);
  try {
    e.sha256 = sha256Hex(lireFichier(p));
  } catch (const std::exception&) {
    e.sha256.clear();
  }
  return e;
}

// Lettres accentuées et signes courants ramenés à l'ASCII (noms STEP, noms de fichiers de fabrication) ; tout autre
// caractère non ASCII devient « _ » (EX-16, EL-46)
std::string translitterer(const std::string& s) {
  static const std::pair<const char*, const char*> table[] = {
      {"à", "a"},  {"á", "a"},  {"â", "a"},  {"ã", "a"},  {"ä", "a"},  {"å", "a"},  {"æ", "ae"}, {"ç", "c"},  {"è", "e"},  {"é", "e"},  {"ê", "e"},
      {"ë", "e"},  {"ì", "i"},  {"í", "i"},  {"î", "i"},  {"ï", "i"},  {"ñ", "n"},  {"ò", "o"},  {"ó", "o"},  {"ô", "o"},  {"õ", "o"},  {"ö", "o"},
      {"ø", "o"},  {"ù", "u"},  {"ú", "u"},  {"û", "u"},  {"ü", "u"},  {"ý", "y"},  {"ÿ", "y"},  {"œ", "oe"}, {"ß", "ss"}, {"À", "A"},  {"Á", "A"},
      {"Â", "A"},  {"Ã", "A"},  {"Ä", "A"},  {"Å", "A"},  {"Æ", "AE"}, {"Ç", "C"},  {"È", "E"},  {"É", "E"},  {"Ê", "E"},  {"Ë", "E"},  {"Ì", "I"},
      {"Í", "I"},  {"Î", "I"},  {"Ï", "I"},  {"Ñ", "N"},  {"Ò", "O"},  {"Ó", "O"},  {"Ô", "O"},  {"Õ", "O"},  {"Ö", "O"},  {"Ø", "D"},  {"Ù", "U"},
      {"Ú", "U"},  {"Û", "U"},  {"Ü", "U"},  {"Ý", "Y"},  {"Ÿ", "Y"},  {"Œ", "OE"}, {"’", "'"},  {"‘", "'"},  {"“", "\""}, {"”", "\""}, {"«", "\""},
      {"»", "\""}, {"–", "-"},  {"—", "-"},  {"…", "..."}, {"µ", "u"},  {"°", "deg"}, {"×", "x"},  {"±", "+-"}, {"²", "2"},  {"³", "3"},  {"½", "1/2"},
      {"¼", "1/4"}, {"€", "EUR"}, {"·", "."},  {"\u00A0", " "}};
  std::string r;
  for (size_t i = 0; i < s.size();) {
    if (static_cast<unsigned char>(s[i]) < 0x80) { r += s[i++]; continue; }
    bool remplace = false;
    for (const auto& [de, vers] : table) {
      const size_t n = std::char_traits<char>::length(de);
      if (s.compare(i, n, de) == 0) { r += vers; i += n; remplace = true; break; }
    }
    if (!remplace) { r += '_'; ++i; while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i; }
  }
  return r;
}


std::string texteStepAscii(const std::string& c) {
  std::string r;
  r.reserve(c.size() + c.size() / 16);
  std::size_t i = 0;
  const std::size_t n = c.size();
  auto hex = [&r](uint32_t v, int chiffres) {
    static const char* H = "0123456789ABCDEF";
    for (int k = chiffres - 1; k >= 0; --k) r += H[(v >> (4 * k)) & 0xF];
  };
  // Lit un caractère UTF-8 à partir de i (coupures de ligne ignorées entre ses octets) ; un octet isolé vaut son code
  auto lire = [&](std::size_t& j) -> uint32_t {
    const auto b0 = static_cast<unsigned char>(c[j++]);
    const int suite = b0 >= 0xF0 ? 3 : b0 >= 0xE0 ? 2 : b0 >= 0xC0 ? 1 : 0;
    uint32_t v = suite == 3 ? (b0 & 0x07u) : suite == 2 ? (b0 & 0x0Fu) : (b0 & 0x1Fu);
    if (suite == 0) return b0;
    std::size_t k = j;
    for (int m = 0; m < suite; ++m) {
      while (k < n && (c[k] == '\r' || c[k] == '\n')) ++k;
      if (k >= n || (static_cast<unsigned char>(c[k]) & 0xC0) != 0x80) return b0;  // séquence invalide : octet seul
      v = (v << 6) | (static_cast<unsigned char>(c[k++]) & 0x3Fu);
    }
    j = k;
    return v;
  };
  while (i < n) {
    const char ch = c[i];
    if (static_cast<unsigned char>(ch) < 0x80) { r += ch; ++i; continue; }
    // Suite de caractères non ASCII : un seul groupe \X2\ (ou \X4\ hors du plan de base)
    std::vector<uint32_t> groupe;
    std::size_t j = i;
    while (j < n) {
      std::size_t k = j;
      while (k < n && (c[k] == '\r' || c[k] == '\n')) ++k;
      if (k >= n || static_cast<unsigned char>(c[k]) < 0x80) break;
      j = k;
      groupe.push_back(lire(j));
    }
    i = j;
    for (std::size_t a = 0; a < groupe.size();) {
      const bool large = groupe[a] > 0xFFFF;
      std::size_t b = a;
      while (b < groupe.size() && (groupe[b] > 0xFFFF) == large) ++b;
      r += large ? "\\X4\\" : "\\X2\\";
      for (std::size_t m = a; m < b; ++m) hex(groupe[m], large ? 8 : 4);
      r += "\\X0\\";
      a = b;
    }
  }
  return r;
}

std::string nomFichierAscii(const std::string& utf8, const std::string& defaut) {
  const std::string t = translitterer(utf8);
  std::string r;
  for (unsigned char ch : t) {
    const bool garde = std::isalnum(ch) || ch == '-' || ch == '_';
    const char c = garde ? static_cast<char>(ch) : '_';
    if (c == '_' && !r.empty() && r.back() == '_') continue;  // pas de « __ » pour un seul signe
    r += c;
  }
  while (!r.empty() && r.back() == '_') r.pop_back();
  while (!r.empty() && r.front() == '_') r.erase(r.begin());
  return r.empty() ? defaut : r;
}

}  // namespace prom
