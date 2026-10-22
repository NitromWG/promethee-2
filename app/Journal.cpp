// Prométhée : journal de démarrage et rapport de plantage. Licence GPL-3.0-only.
#include "Journal.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QString>
#include <QSysInfo>
#include <QtGlobal>

#include <Standard_Version.hxx>

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
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {
std::string g_chemin, g_precedent, g_vidage;
std::string g_maison, g_maisonNative;
FinPrecedente g_finPrecedente = FinPrecedente::Inconnue;
#ifndef _WIN32
int g_fd = -1;  // ouvert d'avance : seul write() est permis dans un gestionnaire de signal
#endif

std::string dossierTemporaire() {
  // Mode portable (9.11) : le journal reste à côté du programme
  if (const char* p = std::getenv("PROMETHEE_DOSSIER_JOURNAL"); p && *p) return std::string(p) + "/";
#ifdef _WIN32
  wchar_t tampon[MAX_PATH + 1];
  const DWORD n = GetTempPathW(MAX_PATH, tampon);
  if (n > 0) return QString::fromWCharArray(tampon, static_cast<int>(n)).toStdString();
#endif
  const char* t = std::getenv("TMPDIR");
  return std::string(t ? t : "/tmp") + "/";
}

FILE* ouvrir(const std::string& chemin, const char* mode) {
#ifdef _WIN32
  return _wfopen(QString::fromStdString(chemin).toStdWString().c_str(), QString::fromLatin1(mode).toStdWString().c_str());
#else
  return std::fopen(chemin.c_str(), mode);
#endif
}

// Le dossier personnel (qui porte souvent le nom de la personne) devient « ~ ».
std::string abreger(std::string s) {
  for (const std::string* m : {&g_maison, &g_maisonNative}) {
    if (m->size() < 3) continue;
    for (std::size_t i = s.find(*m); i != std::string::npos; i = s.find(*m, i + 1)) s.replace(i, m->size(), "~");
  }
  return s;
}

void afficherErreur(const std::string& texte) {
#ifdef _WIN32
  const std::wstring w = QString::fromStdString(texte).toStdWString();
  MessageBoxW(nullptr, w.c_str(), L"Prométhée", MB_ICONERROR | MB_OK);
#else
  std::fprintf(stderr, "%s\n", texte.c_str());
#endif
}

void messageQt(QtMsgType type, const QMessageLogContext&, const QString& message) {
  const char* niveau = type == QtDebugMsg ? "qt" : type == QtInfoMsg ? "qt-info" : type == QtWarningMsg ? "qt-alerte" : "qt-erreur";
  journal(std::string(niveau) + " : " + message.toStdString());
  if (type == QtFatalMsg) {
    afficherErreur("Prométhée s'est arrêté (erreur Qt) :\n\n" + message.toStdString() + "\n\nJournal : " + g_chemin);
    std::abort();
  }
}

void terminaison() {
  std::string quoi = "exception C++ inconnue";
  if (const std::exception_ptr e = std::current_exception()) {
    try {
      std::rethrow_exception(e);
    } catch (const std::exception& x) {
      quoi = x.what();
    } catch (...) {
    }
  }
  journal("plantage : exception non rattrapée : " + quoi);
  afficherErreur("Prométhée s'est arrêté sur une erreur imprévue :\n\n" + quoi + "\n\nJournal : " + g_chemin +
                 "\nEnvoie ce message (ou le fichier) pour que le défaut soit corrigé.");
  std::abort();
}

#ifdef _WIN32
// Vidage mémoire réduit (piles des fils d'exécution) pour retrouver la cause sur une autre machine.
bool ecrireVidage(EXCEPTION_POINTERS* info) {
  using Fonction = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
                                 PMINIDUMP_CALLBACK_INFORMATION);
  HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!dbghelp) return false;
  auto ecrire = reinterpret_cast<Fonction>(reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));
  if (!ecrire) return false;
  HANDLE f = CreateFileW(QString::fromStdString(g_vidage).toStdWString().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return false;
  MINIDUMP_EXCEPTION_INFORMATION e{GetCurrentThreadId(), info, FALSE};
  const BOOL ok = ecrire(GetCurrentProcess(), GetCurrentProcessId(), f, static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo), &e, nullptr, nullptr);
  CloseHandle(f);
  return ok != FALSE;
}

LONG WINAPI filtre(EXCEPTION_POINTERS* info) {
  void* adresse = info->ExceptionRecord->ExceptionAddress;
  char module[MAX_PATH] = "inconnu";
  HMODULE h = nullptr;
  unsigned long long decalage = 0;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(adresse), &h)) {
    GetModuleFileNameA(h, module, MAX_PATH);
    decalage = reinterpret_cast<unsigned long long>(adresse) - reinterpret_cast<unsigned long long>(h);
  }
  const char* nomModule = std::strrchr(module, '\\') ? std::strrchr(module, '\\') + 1 : module;
  char texte[1400];
  std::snprintf(texte, sizeof texte, "Code 0x%08lX, module %s, décalage 0x%llX", static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode), nomModule,
                decalage);
  journal(std::string("plantage : ") + texte + " (" + prom::infosVersion().identifiant + ")");
  const bool vidage = ecrireVidage(info);
  if (vidage) journal("plantage : vidage mémoire écrit dans promethee-plantage.dmp");
  afficherErreur(std::string("Prométhée s'est arrêté brutalement.\n\n") + texte + "\n\nJournal : " + g_chemin + (vidage ? "\nVidage : " + g_vidage : std::string()) +
                 "\nEnvoie ce message (ou ces fichiers) pour que le défaut soit corrigé.\nAu prochain lancement, les projets modifiés seront proposés à la récupération.");
  return EXCEPTION_EXECUTE_HANDLER;
}
#else
void ecrireBrut(const char* s) {
  if (g_fd >= 0) {
    const ssize_t r = ::write(g_fd, s, std::strlen(s));
    (void)r;
  }
}

void signalFatal(int sig) {
  // Uniquement des appels permis dans un gestionnaire de signal (write, backtrace sur une pile déjà chargée).
  ecrireBrut("plantage : signal ");
  char n[8] = {0};
  int i = 6;
  int v = sig;
  do { n[i--] = static_cast<char>('0' + v % 10); v /= 10; } while (v && i >= 0);
  ecrireBrut(n + i + 1);
  ecrireBrut(sig == SIGSEGV ? " (accès mémoire invalide)\n" : sig == SIGABRT ? " (abandon)\n" : sig == SIGFPE ? " (erreur arithmétique)\n" : "\n");
  void* pile[64];
  const int profondeur = backtrace(pile, 64);
  if (g_fd >= 0) backtrace_symbols_fd(pile, profondeur, g_fd);
  std::signal(sig, SIG_DFL);
  std::raise(sig);
}
#endif

void lireFinPrecedente() {
  std::ifstream f(prom::cheminFichier(g_precedent));
  if (!f) return;
  std::string ligne, derniere;
  bool plantage = false;
  while (std::getline(f, ligne)) {
    if (ligne.empty()) continue;
    derniere = ligne;
    if (ligne.find("plantage") != std::string::npos) plantage = true;
  }
  if (derniere.empty()) return;
  g_finPrecedente = !plantage && derniere.find("fermeture normale") != std::string::npos ? FinPrecedente::Normale : FinPrecedente::Interrompue;
}
}  // namespace

const char* cheminJournal() { return g_chemin.c_str(); }
std::string cheminJournalPrecedent() { return g_precedent; }
FinPrecedente finSessionPrecedente() { return g_finPrecedente; }

void journal(const std::string& texte) { journal(texte.c_str()); }

void journal(const char* texte) {
  if (g_chemin.empty()) return;
  if (FILE* f = ouvrir(g_chemin, "a")) {
    const std::time_t t = std::time(nullptr);
    char heure[32];
    std::strftime(heure, sizeof heure, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::fprintf(f, "%s  %s\n", heure, abreger(texte).c_str());
    std::fclose(f);
  }
}

void installerJournal() {
  const std::string dossier = dossierTemporaire();
  g_chemin = dossier + "promethee-journal.txt";
  g_precedent = dossier + "promethee-journal-precedent.txt";
  g_vidage = dossier + "promethee-plantage.dmp";
  g_maison = QDir::homePath().toStdString();
  g_maisonNative = QDir::toNativeSeparators(QDir::homePath()).toStdString();
  // Le journal du lancement précédent est gardé : c'est lui qui décrit un éventuel plantage.
  std::error_code ec;
  std::filesystem::remove(prom::cheminFichier(g_precedent), ec);
  std::filesystem::rename(prom::cheminFichier(g_chemin), prom::cheminFichier(g_precedent), ec);
  lireFinPrecedente();
  if (FILE* f = ouvrir(g_chemin, "w")) std::fclose(f);
  journal("démarrage de " + prom::ligneVersion());
  journal(std::string("identifiant de build ") + prom::infosVersion().identifiant + ", OpenCascade " + OCC_VERSION_COMPLETE + ", Qt " + qVersion());
  if (g_finPrecedente == FinPrecedente::Interrompue) journal("le lancement précédent ne s'est pas terminé normalement (journal précédent gardé)");
  qInstallMessageHandler(messageQt);
  std::set_terminate(terminaison);
#ifdef _WIN32
  SetUnhandledExceptionFilter(filtre);
#else
  g_fd = ::open(g_chemin.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
  // Charge le code de backtrace maintenant : un premier appel dans le gestionnaire pourrait allouer.
  void* amorce[1];
  backtrace(amorce, 1);
  for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) std::signal(sig, signalFatal);
#endif
}

void completerJournal() {
  journal("système : " + QSysInfo::prettyProductName().toStdString() + " (" + QSysInfo::kernelType().toStdString() + " " +
          QSysInfo::kernelVersion().toStdString() + ", " + QSysInfo::currentCpuArchitecture().toStdString() + ")");
  if (const QScreen* e = QGuiApplication::primaryScreen())
    journal("écran principal : " + std::to_string(e->size().width()) + " × " + std::to_string(e->size().height()) + " points, échelle " +
            QString::number(e->devicePixelRatio()).toStdString() + ", plateforme " + QGuiApplication::platformName().toStdString());
}
