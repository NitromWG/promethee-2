// Prométhée : chien de garde du fil principal et mode portable (lot L9). Licence GPL-3.0-only.
#include "ChienDeGarde.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

#include "promethee/fichiers.hpp"
#include "promethee/version.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <cxxabi.h>
#include <execinfo.h>
#include <pthread.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {
using Horloge = std::chrono::steady_clock;
std::int64_t maintenant() { return std::chrono::duration_cast<std::chrono::milliseconds>(Horloge::now().time_since_epoch()).count(); }

std::atomic<std::int64_t> g_battement{0};
std::atomic<bool> g_fin{false};
std::thread g_fil;
std::mutex g_mutex;  // gestes, dernier rapport
std::condition_variable g_reveil;
std::deque<std::string> g_gestes;
std::string g_dernier;
std::string g_dossier;
QTimer* g_minuterie = nullptr;

#ifndef _WIN32
pthread_t g_principal;
void* g_pile[96];
volatile std::sig_atomic_t g_profondeur = 0;
volatile std::sig_atomic_t g_pret = 0;
// Dans le fil principal, interrompu : seule la pile est relevée (backtrace est déjà chargé par le journal)
void relever(int) {
  g_profondeur = backtrace(g_pile, 96);
  g_pret = 1;
}
std::string lisible(const char* ligne) {
  // « module(_ZN4prom…+0x12) [0x…] » : le nom C++ démêlé
  std::string s = ligne;
  const auto a = s.find('('), p = s.find('+', a == std::string::npos ? 0 : a);
  if (a == std::string::npos || p == std::string::npos || p <= a + 1) return s;
  int etat = 0;
  char* d = abi::__cxa_demangle(s.substr(a + 1, p - a - 1).c_str(), nullptr, nullptr, &etat);
  if (etat == 0 && d) {
    s = s.substr(0, a + 1) + d + s.substr(p);
    std::free(d);
  }
  return s;
}
#endif

std::string horodatage(const char* format) {
  const std::time_t t = std::time(nullptr);
  char h[64];
  std::strftime(h, sizeof h, format, std::localtime(&t));
  return h;
}

// Dans le fil du chien de garde
void ecrireRapport(std::int64_t fige) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::create_directories(prom::cheminFichier(g_dossier), ec);
  const std::string base = g_dossier + "/fige-" + horodatage("%Y%m%d-%H%M%S");
  std::ofstream f(prom::cheminFichier(base + ".txt"));
  if (!f) return;
  f << "Rapport de blocage de Prométhée\n";
  f << "Le fil principal ne répond plus depuis " << fige / 1000 << " s (" << horodatage("%Y-%m-%d %H:%M:%S") << ").\n";
  f << prom::ligneVersion() << "\nidentifiant " << prom::infosVersion().identifiant << "\n";
  f << "Système : " << QSysInfo::prettyProductName().toStdString() << ", " << QSysInfo::currentCpuArchitecture().toStdString() << "\n\n";
#ifdef _WIN32
  // Vidage de tout le processus, piles de tous les fils comprises
  {
    using Fonction = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    bool ecrit = false;
    if (HMODULE dbg = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
      if (auto ecrire = reinterpret_cast<Fonction>(reinterpret_cast<void*>(GetProcAddress(dbg, "MiniDumpWriteDump")))) {
        HANDLE h = CreateFileW(QString::fromStdString(base + ".dmp").toStdWString().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
          ecrit = ecrire(GetCurrentProcess(), GetCurrentProcessId(), h, static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo), nullptr, nullptr, nullptr) != FALSE;
          CloseHandle(h);
        }
      }
    f << "Pile du fil principal : dans le vidage du processus " << (ecrit ? base + ".dmp" : std::string("(non écrit)")) << "\n";
  }
#else
  // Pile du fil principal, relevée par un signal qui l'interrompt
  g_pret = 0;
  pthread_kill(g_principal, SIGUSR2);
  for (int i = 0; i < 200 && !g_pret; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  f << "Pile du fil principal :\n";
  if (g_pret) {
    char** lignes = backtrace_symbols(g_pile, g_profondeur);
    for (int i = 0; i < g_profondeur; ++i) f << "  #" << i << " " << (lignes ? lisible(lignes[i]) : std::string("?")) << "\n";
    std::free(lignes);
  } else
    f << "  (le fil principal n'a pas répondu au relevé)\n";
  // Fils du processus
  f << "\nFils du processus :\n";
  for (const auto& e : fs::directory_iterator("/proc/self/task", ec)) {
    std::ifstream nom(e.path() / "comm");
    std::string n;
    std::getline(nom, n);
    f << "  " << e.path().filename().string() << " " << n << "\n";
  }
#endif
  f << "\nVingt derniers gestes (du plus ancien au plus récent) :\n";
  {
    std::lock_guard<std::mutex> v(g_mutex);
    if (g_gestes.empty()) f << "  (aucun)\n";
    for (const auto& x : g_gestes) f << "  " << x << "\n";
  }
  f << "\nRien n'a été envoyé : ce rapport reste dans ce dossier. Joins-le à un signalement si tu le souhaites.\n";
  f.close();
  std::lock_guard<std::mutex> v(g_mutex);
  g_dernier = base + ".txt";
}

void surveiller(int secondes) {
  bool signale = false;
  std::unique_lock<std::mutex> attente(g_mutex);
  while (!g_fin) {
    g_reveil.wait_for(attente, std::chrono::milliseconds(250));
    if (g_fin) break;
    const std::int64_t fige = maintenant() - g_battement.load();
    if (fige > secondes * 1000LL && !signale) {
      attente.unlock();
      ecrireRapport(fige);
      attente.lock();
      signale = true;
    } else if (fige < 1000)
      signale = false;  // le fil principal bat de nouveau : un prochain blocage aura son rapport
  }
}

// ---------- Mode portable ----------
std::string g_portable;  // dossier des données portables, vide hors mode portable
std::string dossierProgramme(const char* argv0) {
  if (const char* force = std::getenv("PROMETHEE_DOSSIER_PROGRAMME"); force && *force) return force;
  namespace fs = std::filesystem;
  std::error_code ec;
#ifdef _WIN32
  wchar_t chemin[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, chemin, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    const std::u8string u = fs::path(std::wstring(chemin, n)).parent_path().u8string();  // UTF-8, comme le reste
    return std::string(u.begin(), u.end());
  }
#elif defined(__APPLE__)
  char chemin[4096];
  std::uint32_t taille = sizeof chemin;
  if (_NSGetExecutablePath(chemin, &taille) == 0) return fs::path(chemin).parent_path().string();
#else
  const fs::path p = fs::read_symlink("/proc/self/exe", ec);
  if (!ec) return p.parent_path().string();
#endif
  return argv0 ? fs::absolute(fs::path(argv0), ec).parent_path().string() : std::string();
}
void definir(const char* nom, const std::string& valeur) {
#ifdef _WIN32
  _putenv_s(nom, valeur.c_str());
#else
  setenv(nom, valeur.c_str(), 1);
#endif
}
}  // namespace

namespace chien {
void demarrer(const QString& dossier, int secondes) {
  if (g_fil.joinable()) return;
  g_dossier = dossier.toStdString();
  g_battement = maintenant();
#ifndef _WIN32
  g_principal = pthread_self();
  struct sigaction sa {};
  sa.sa_handler = relever;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGUSR2, &sa, nullptr);
#endif
  g_minuterie = new QTimer(QCoreApplication::instance());
  QObject::connect(g_minuterie, &QTimer::timeout, [] { g_battement = maintenant(); });
  g_minuterie->start(500);
  g_fin = false;
  g_fil = std::thread(surveiller, secondes);
}

void arreter() {
  if (!g_fil.joinable()) return;
  g_fin = true;
  g_reveil.notify_all();
  g_fil.join();
}

void geste(const QString& nom) {
  std::lock_guard<std::mutex> v(g_mutex);
  g_gestes.push_back(horodatage("%H:%M:%S") + "  " + nom.toStdString());
  while (g_gestes.size() > 20) g_gestes.pop_front();
}

QString dernierRapport() {
  std::lock_guard<std::mutex> v(g_mutex);
  return QString::fromStdString(g_dernier);
}
}  // namespace chien

namespace portable {
void initialiser(const char* argv0) {
  namespace fs = std::filesystem;
  const std::string prog = dossierProgramme(argv0);
  std::error_code ec;
  if (prog.empty() || !fs::exists(prom::cheminFichier(prog + "/promethee-portable.txt"), ec)) return;
  g_portable = prog + "/promethee-donnees";
  fs::create_directories(prom::cheminFichier(g_portable), ec);
  // Récupération et rapports (Recuperation::dossierDonnees), journal ; sous Linux, les dossiers XDG de Qt, de
  // fontconfig et des caches de Mesa
  definir("PROMETHEE_DOSSIER_DONNEES", g_portable);
  definir("PROMETHEE_DOSSIER_JOURNAL", g_portable);
#if !defined(_WIN32) && !defined(__APPLE__)
  definir("XDG_CONFIG_HOME", g_portable + "/config");
  definir("XDG_DATA_HOME", g_portable + "/donnees");
  definir("XDG_CACHE_HOME", g_portable + "/cache");
  definir("XDG_STATE_HOME", g_portable + "/etat");
#endif
}

bool actif() { return !g_portable.empty(); }
QString dossier() { return QString::fromStdString(g_portable); }

void appliquerReglages() {
  if (!actif()) return;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir(dossier()).filePath(QStringLiteral("reglages")));
}

QString fichierReglages() { return QDir(dossier()).filePath(QStringLiteral("reglages/Prométhée/Prométhée.ini")); }

QString dossierDonneesAppli() {
  if (actif()) return QDir(dossier()).filePath(QStringLiteral("bibliotheques"));
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
}  // namespace portable
