// Prométhée : programmes du poste lancés par le noyau. Voir processus.hpp. Licence GPL-3.0-only.
#include "promethee/processus.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include "promethee/fichiers.hpp"

namespace fs = std::filesystem;

namespace prom {

namespace {
std::mutex g_journal;
std::vector<std::vector<std::string>> g_lances;
void noter(const std::vector<std::string>& a) {
  const std::lock_guard<std::mutex> v(g_journal);
  g_lances.push_back(a);
}
#ifndef _WIN32
// L'enfant est lancé par posix_spawn, sans copie de la mémoire du parent : un parent qui a réservé beaucoup de
// mémoire (simulation continue de ngspice, grandes images) ne fait plus échouer le lancement, ce que fork faisait.
// Entrée, sortie et erreurs sur les tubes donnés ; « fermer » : descripteurs du parent à ne pas transmettre.
// Rend le pid, ou −1 avec la raison dans « raison ».
#if defined(__APPLE__) || (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29)))
#define PROMETHEE_SPAWN_CHDIR 1
#endif
pid_t lancerEnfant(const std::string& chemin, std::vector<char*>& av, std::vector<char*>& envp, const std::string& dossier, int in, int out, int err,
                   std::initializer_list<int> fermer, std::string* raison) {
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_adddup2(&fa, in, 0);
  posix_spawn_file_actions_adddup2(&fa, out, 1);
  posix_spawn_file_actions_adddup2(&fa, err, 2);
  for (int f : fermer)
    if (f > 2) posix_spawn_file_actions_addclose(&fa, f);
#ifdef PROMETHEE_SPAWN_CHDIR
  if (!dossier.empty()) posix_spawn_file_actions_addchdir_np(&fa, dossier.c_str());
#endif
  pid_t pid = -1;
  int e = 0;
#ifndef PROMETHEE_SPAWN_CHDIR
  // Sans chdir dans posix_spawn : fork, comme avant
  if (!dossier.empty()) {
    pid = ::fork();
    if (pid == 0) {
      ::dup2(in, 0);
      ::dup2(out, 1);
      ::dup2(err, 2);
      for (int f : fermer)
        if (f > 2) ::close(f);
      if (::chdir(dossier.c_str()) != 0) ::_exit(126);
      ::execve(chemin.c_str(), av.data(), envp.data());
      ::_exit(127);
    }
    e = pid < 0 ? errno : 0;
  } else
#endif
    e = posix_spawn(&pid, chemin.c_str(), &fa, nullptr, av.data(), envp.data());
  posix_spawn_file_actions_destroy(&fa);
  if (e != 0) {
    if (raison) *raison = std::string("processus impossible à créer (") + std::strerror(e) + ")";
    return -1;
  }
  return pid;
}
#endif
}  // namespace

std::vector<std::vector<std::string>> programmesLances() {
  const std::lock_guard<std::mutex> v(g_journal);
  return g_lances;
}
void effacerJournalProgrammes() {
  const std::lock_guard<std::mutex> v(g_journal);
  g_lances.clear();
}

std::string programmeDuPoste(const std::string& nom) {
  if (nom.empty()) return {};
  std::error_code ec;
  const fs::path p = cheminFichier(nom);
  if (p.has_parent_path()) return fs::is_regular_file(p, ec) ? nom : std::string();
  const char* path = std::getenv("PATH");
  if (!path) return {};
#ifdef _WIN32
  const char separateur = ';';
  const std::vector<std::string> suffixes{".exe", ".com", ".bat", ".cmd", ""};
#else
  const char separateur = ':';
  const std::vector<std::string> suffixes{""};
#endif
  const std::string tout = path;
  std::size_t debut = 0;
  while (debut <= tout.size()) {
    const std::size_t fin = tout.find(separateur, debut);
    const std::string d = tout.substr(debut, fin == std::string::npos ? std::string::npos : fin - debut);
    if (!d.empty())
      for (const auto& s : suffixes) {
        const fs::path c = cheminFichier(d) / cheminFichier(nom + s);
        if (!fs::is_regular_file(c, ec)) continue;
#ifndef _WIN32
        if (::access(c.c_str(), X_OK) != 0) continue;
#endif
        return utf8(c);
      }
    if (fin == std::string::npos) break;
    debut = fin + 1;
  }
  return {};
}

#ifdef _WIN32
namespace {
std::wstring large(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}
// Argument cité selon les règles de CommandLineToArgvW
std::wstring citer(const std::wstring& a) {
  if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
  std::wstring r = L"\"";
  for (std::size_t i = 0;; ++i) {
    std::size_t barres = 0;
    while (i < a.size() && a[i] == L'\\') ++i, ++barres;
    if (i == a.size()) {
      r.append(barres * 2, L'\\');
      break;
    }
    if (a[i] == L'"') r.append(barres * 2 + 1, L'\\');
    else r.append(barres, L'\\');
    r += a[i];
  }
  return r + L"\"";
}
void lireTout(HANDLE h, std::string* dest) {
  char tampon[8192];
  DWORD lu = 0;
  while (ReadFile(h, tampon, sizeof tampon, &lu, nullptr) && lu > 0) dest->append(tampon, lu);
}
}  // namespace

ResultatProcessus executerProcessus(const std::vector<std::string>& argv, const OptionsProcessus& o) {
  ResultatProcessus r;
  if (argv.empty()) {
    r.erreurs = "commande vide";
    return r;
  }
  noter(argv);
  const std::string chemin = programmeDuPoste(argv[0]);
  if (chemin.empty()) {
    r.erreurs = "« " + argv[0] + " » est introuvable sur ce poste";
    return r;
  }
  SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE entreeL = nullptr, entreeE = nullptr, sortieL = nullptr, sortieE = nullptr, erreurL = nullptr, erreurE = nullptr;
  if (!CreatePipe(&entreeL, &entreeE, &sa, 0) || !CreatePipe(&sortieL, &sortieE, &sa, 0) || !CreatePipe(&erreurL, &erreurE, &sa, 0)) {
    r.erreurs = "tubes impossibles à créer";
    return r;
  }
  SetHandleInformation(entreeE, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(sortieL, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(erreurL, HANDLE_FLAG_INHERIT, 0);
  std::wstring ligne = citer(large(chemin));
  for (std::size_t i = 1; i < argv.size(); ++i) ligne += L" " + citer(large(argv[i]));
  // Environnement : celui du poste, complété
  std::vector<std::wstring> env;
  if (wchar_t* bloc = GetEnvironmentStringsW()) {
    for (const wchar_t* e = bloc; *e; e += wcslen(e) + 1) env.emplace_back(e);
    FreeEnvironmentStringsW(bloc);
  }
  auto cle = [](const std::wstring& v) {
    std::wstring k = v.substr(0, v.find(L'=', 1));
    std::transform(k.begin(), k.end(), k.begin(), ::towupper);
    return k;
  };
  for (const auto& [k, v] : o.environnement) {
    const std::wstring kv = large(k) + L"=" + large(v);
    const std::wstring ck = cle(kv);
    env.erase(std::remove_if(env.begin(), env.end(), [&](const std::wstring& x) { return cle(x) == ck; }), env.end());
    env.push_back(kv);
  }
  std::sort(env.begin(), env.end(), [&](const std::wstring& a, const std::wstring& b) { return cle(a) < cle(b); });
  std::wstring bloc;
  for (const auto& e : env) bloc += e + L'\0';
  bloc += L'\0';
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = entreeL;
  si.hStdOutput = sortieE;
  si.hStdError = erreurE;
  PROCESS_INFORMATION pi{};
  const std::wstring dossier = large(o.dossier);
  const BOOL ok = CreateProcessW(nullptr, ligne.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, bloc.data(),
                                 dossier.empty() ? nullptr : dossier.c_str(), &si, &pi);
  CloseHandle(entreeL);
  CloseHandle(sortieE);
  CloseHandle(erreurE);
  if (!ok) {
    CloseHandle(entreeE);
    CloseHandle(sortieL);
    CloseHandle(erreurL);
    r.erreurs = "« " + argv[0] + " » n’a pas pu être lancé";
    return r;
  }
  r.lance = true;
  std::thread lecteurSortie(lireTout, sortieL, &r.sortie), lecteurErreurs(lireTout, erreurL, &r.erreurs);
  std::thread ecrivain([entreeE, texte = o.entree] {
    DWORD ecrit = 0;
    std::size_t fait = 0;
    while (fait < texte.size() && WriteFile(entreeE, texte.data() + fait, static_cast<DWORD>(std::min<std::size_t>(texte.size() - fait, 65536)), &ecrit, nullptr) && ecrit > 0) fait += ecrit;
    CloseHandle(entreeE);
  });
  const DWORD attente = static_cast<DWORD>(std::clamp(o.delai, 0.0, 86400.0) * 1000);
  if (WaitForSingleObject(pi.hProcess, attente) == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    r.delaiDepasse = true;
  }
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  r.code = static_cast<int>(code);
  ecrivain.join();
  lecteurSortie.join();
  lecteurErreurs.join();
  CloseHandle(sortieL);
  CloseHandle(erreurL);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return r;
}
#else
ResultatProcessus executerProcessus(const std::vector<std::string>& argv, const OptionsProcessus& o) {
  ResultatProcessus r;
  if (argv.empty()) {
    r.erreurs = "commande vide";
    return r;
  }
  noter(argv);
  const std::string chemin = programmeDuPoste(argv[0]);
  if (chemin.empty()) {
    r.erreurs = "« " + argv[0] + " » est introuvable sur ce poste";
    return r;
  }
  static const bool sansSigpipe = [] {
    ::signal(SIGPIPE, SIG_IGN);  // un programme qui ferme son entrée ne doit pas arrêter Prométhée
    return true;
  }();
  (void)sansSigpipe;
  // Tout est préparé avant fork : l'enfant n'appelle que dup2, close, chdir, execve et _exit
  std::map<std::string, std::string> env;
  for (char** e = environ; e && *e; ++e) {
    const std::string s = *e;
    if (const std::size_t i = s.find('='); i != std::string::npos) env[s.substr(0, i)] = s.substr(i + 1);
  }
  for (const auto& [k, v] : o.environnement) env[k] = v;
  std::vector<std::string> textesEnv, args(argv);
  for (const auto& [k, v] : env) textesEnv.push_back(k + "=" + v);
  std::vector<char*> envp, av;
  for (auto& s : textesEnv) envp.push_back(s.data());
  envp.push_back(nullptr);
  for (auto& s : args) av.push_back(s.data());
  av.push_back(nullptr);
  int entree[2] = {-1, -1}, sortie[2] = {-1, -1}, erreur[2] = {-1, -1};
  if (::pipe(entree) != 0 || ::pipe(sortie) != 0 || ::pipe(erreur) != 0) {
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]})
      if (f >= 0) ::close(f);
    r.erreurs = "tubes impossibles à créer";
    return r;
  }
  const pid_t pid = lancerEnfant(chemin, av, envp, o.dossier, entree[0], sortie[1], erreur[1], {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]}, &r.erreurs);
  if (pid < 0) {
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]}) ::close(f);
    return r;
  }
  ::close(entree[0]);
  ::close(sortie[1]);
  ::close(erreur[1]);
  r.lance = true;
  for (int f : {entree[1], sortie[0], erreur[0]}) ::fcntl(f, F_SETFL, ::fcntl(f, F_GETFL) | O_NONBLOCK);
  int fdEntree = entree[1], fdSortie = sortie[0], fdErreur = erreur[0];
  std::size_t ecrit = 0;
  if (o.entree.empty()) {
    ::close(fdEntree);
    fdEntree = -1;
  }
  const auto t0 = std::chrono::steady_clock::now();
  char tampon[8192];
  while (fdSortie >= 0 || fdErreur >= 0 || fdEntree >= 0) {
    pollfd fds[3];
    int n = 0;
    if (fdSortie >= 0) fds[n++] = {fdSortie, POLLIN, 0};
    if (fdErreur >= 0) fds[n++] = {fdErreur, POLLIN, 0};
    if (fdEntree >= 0) fds[n++] = {fdEntree, POLLOUT, 0};
    ::poll(fds, static_cast<nfds_t>(n), 100);
    for (int i = 0; i < n; ++i) {
      if (!fds[i].revents) continue;
      if (fds[i].fd == fdEntree) {
        const ssize_t k = ::write(fdEntree, o.entree.data() + ecrit, std::min<std::size_t>(o.entree.size() - ecrit, 65536));
        if (k > 0) ecrit += static_cast<std::size_t>(k);
        if (k < 0 && errno != EAGAIN && errno != EINTR) ecrit = o.entree.size();
        if (ecrit >= o.entree.size()) {
          ::close(fdEntree);
          fdEntree = -1;
        }
        continue;
      }
      int& fd = fds[i].fd == fdSortie ? fdSortie : fdErreur;
      std::string& dest = fds[i].fd == fdSortie ? r.sortie : r.erreurs;
      for (;;) {
        const ssize_t k = ::read(fd, tampon, sizeof tampon);
        if (k > 0) {
          dest.append(tampon, static_cast<std::size_t>(k));
          continue;
        }
        if (k < 0 && (errno == EAGAIN || errno == EINTR)) break;
        ::close(fd);
        fd = -1;
        break;
      }
    }
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > o.delai) {
      ::kill(pid, SIGKILL);
      r.delaiDepasse = true;
      for (int f : {fdEntree, fdSortie, fdErreur})
        if (f >= 0) ::close(f);
      break;
    }
  }
  int statut = 0;
  while (::waitpid(pid, &statut, 0) < 0 && errno == EINTR) {
  }
  r.code = WIFEXITED(statut) ? WEXITSTATUS(statut) : -1;
  if (r.code == 127 && r.erreurs.empty()) {
    r.lance = false;
    r.erreurs = "« " + argv[0] + " » n’a pas pu être lancé";
  }
  return r;
}
#endif

// ---------- Dialogue ligne à ligne ----------
namespace {
// Une ligne complète de la sortie : réponse au programme, ou texte gardé
void traiterLigne(std::string ligne, const std::function<std::optional<std::string>(const std::string&)>& repondre, std::string& sortie, std::string& aEcrire) {
  if (!ligne.empty() && ligne.back() == '\r') ligne.pop_back();
  if (std::optional<std::string> rep = repondre(ligne)) aEcrire += *rep + "\n";
  else sortie += ligne + "\n";
}
}  // namespace

#ifdef _WIN32
ResultatProcessus dialoguerProcessus(const std::vector<std::string>& argv, const OptionsProcessus& o, const std::function<std::optional<std::string>(const std::string&)>& repondre,
                                     const std::atomic<bool>* arret) {
  ResultatProcessus r;
  if (argv.empty()) {
    r.erreurs = "commande vide";
    return r;
  }
  noter(argv);
  const std::string chemin = programmeDuPoste(argv[0]);
  if (chemin.empty()) {
    r.erreurs = "« " + argv[0] + " » est introuvable sur ce poste";
    return r;
  }
  SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE entreeL = nullptr, entreeE = nullptr, sortieL = nullptr, sortieE = nullptr, erreurL = nullptr, erreurE = nullptr;
  if (!CreatePipe(&entreeL, &entreeE, &sa, 0) || !CreatePipe(&sortieL, &sortieE, &sa, 0) || !CreatePipe(&erreurL, &erreurE, &sa, 0)) {
    r.erreurs = "tubes impossibles à créer";
    return r;
  }
  SetHandleInformation(entreeE, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(sortieL, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(erreurL, HANDLE_FLAG_INHERIT, 0);
  std::wstring ligne = citer(large(chemin));
  for (std::size_t i = 1; i < argv.size(); ++i) ligne += L" " + citer(large(argv[i]));
  std::vector<std::wstring> env;
  if (wchar_t* bloc = GetEnvironmentStringsW()) {
    for (const wchar_t* e = bloc; *e; e += wcslen(e) + 1) env.emplace_back(e);
    FreeEnvironmentStringsW(bloc);
  }
  auto cle = [](const std::wstring& v) {
    std::wstring k = v.substr(0, v.find(L'=', 1));
    std::transform(k.begin(), k.end(), k.begin(), ::towupper);
    return k;
  };
  for (const auto& [k, v] : o.environnement) {
    const std::wstring kv = large(k) + L"=" + large(v);
    const std::wstring ck = cle(kv);
    env.erase(std::remove_if(env.begin(), env.end(), [&](const std::wstring& x) { return cle(x) == ck; }), env.end());
    env.push_back(kv);
  }
  std::sort(env.begin(), env.end(), [&](const std::wstring& a, const std::wstring& b) { return cle(a) < cle(b); });
  std::wstring bloc;
  for (const auto& e : env) bloc += e + L'\0';
  bloc += L'\0';
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = entreeL;
  si.hStdOutput = sortieE;
  si.hStdError = erreurE;
  PROCESS_INFORMATION pi{};
  const std::wstring dossier = large(o.dossier);
  const BOOL ok = CreateProcessW(nullptr, ligne.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, bloc.data(),
                                 dossier.empty() ? nullptr : dossier.c_str(), &si, &pi);
  CloseHandle(entreeL);
  CloseHandle(sortieE);
  CloseHandle(erreurE);
  if (!ok) {
    CloseHandle(entreeE);
    CloseHandle(sortieL);
    CloseHandle(erreurL);
    r.erreurs = "« " + argv[0] + " » n’a pas pu être lancé";
    return r;
  }
  r.lance = true;
  std::thread lecteurErreurs(lireTout, erreurL, &r.erreurs);
  std::atomic<bool> fini{false}, depasse{false};
  std::thread garde([&] {
    const auto t0 = std::chrono::steady_clock::now();
    while (!fini) {
      const bool tard = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > o.delai;
      if (tard || (arret && *arret)) {
        depasse = tard;
        TerminateProcess(pi.hProcess, 1);
        break;
      }
      Sleep(50);
    }
  });
  auto ecrire = [&](const std::string& t) {
    DWORD ecrit = 0;
    std::size_t fait = 0;
    while (fait < t.size() && WriteFile(entreeE, t.data() + fait, static_cast<DWORD>(std::min<std::size_t>(t.size() - fait, 65536)), &ecrit, nullptr) && ecrit > 0) fait += ecrit;
  };
  if (!o.entree.empty()) ecrire(o.entree);
  char tampon[8192];
  DWORD lu = 0;
  std::string reste;
  while (ReadFile(sortieL, tampon, sizeof tampon, &lu, nullptr) && lu > 0) {
    reste.append(tampon, lu);
    std::size_t i;
    while ((i = reste.find('\n')) != std::string::npos) {
      std::string aEcrire;
      traiterLigne(reste.substr(0, i), repondre, r.sortie, aEcrire);
      reste.erase(0, i + 1);
      if (!aEcrire.empty()) ecrire(aEcrire);
    }
  }
  if (!reste.empty()) {
    std::string aEcrire;
    traiterLigne(reste, repondre, r.sortie, aEcrire);
  }
  CloseHandle(entreeE);
  WaitForSingleObject(pi.hProcess, INFINITE);
  fini = true;
  garde.join();
  lecteurErreurs.join();
  r.delaiDepasse = depasse;
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  r.code = static_cast<int>(code);
  CloseHandle(sortieL);
  CloseHandle(erreurL);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return r;
}
#else
ResultatProcessus dialoguerProcessus(const std::vector<std::string>& argv, const OptionsProcessus& o, const std::function<std::optional<std::string>(const std::string&)>& repondre,
                                     const std::atomic<bool>* arret) {
  ResultatProcessus r;
  if (argv.empty()) {
    r.erreurs = "commande vide";
    return r;
  }
  noter(argv);
  const std::string chemin = programmeDuPoste(argv[0]);
  if (chemin.empty()) {
    r.erreurs = "« " + argv[0] + " » est introuvable sur ce poste";
    return r;
  }
  static const bool sansSigpipe = [] {
    ::signal(SIGPIPE, SIG_IGN);
    return true;
  }();
  (void)sansSigpipe;
  std::map<std::string, std::string> env;
  for (char** e = environ; e && *e; ++e) {
    const std::string s = *e;
    if (const std::size_t i = s.find('='); i != std::string::npos) env[s.substr(0, i)] = s.substr(i + 1);
  }
  for (const auto& [k, v] : o.environnement) env[k] = v;
  std::vector<std::string> textesEnv, args(argv);
  for (const auto& [k, v] : env) textesEnv.push_back(k + "=" + v);
  std::vector<char*> envp, av;
  for (auto& s : textesEnv) envp.push_back(s.data());
  envp.push_back(nullptr);
  for (auto& s : args) av.push_back(s.data());
  av.push_back(nullptr);
  int entree[2] = {-1, -1}, sortie[2] = {-1, -1}, erreur[2] = {-1, -1};
  if (::pipe(entree) != 0 || ::pipe(sortie) != 0 || ::pipe(erreur) != 0) {
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]})
      if (f >= 0) ::close(f);
    r.erreurs = "tubes impossibles à créer";
    return r;
  }
  const pid_t pid = lancerEnfant(chemin, av, envp, o.dossier, entree[0], sortie[1], erreur[1], {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]}, &r.erreurs);
  if (pid < 0) {
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]}) ::close(f);
    return r;
  }
  ::close(entree[0]);
  ::close(sortie[1]);
  ::close(erreur[1]);
  r.lance = true;
  for (int f : {entree[1], sortie[0], erreur[0]}) ::fcntl(f, F_SETFL, ::fcntl(f, F_GETFL) | O_NONBLOCK);
  int fdEntree = entree[1], fdSortie = sortie[0], fdErreur = erreur[0];
  std::string aEcrire = o.entree, reste;
  const auto t0 = std::chrono::steady_clock::now();
  char tampon[8192];
  while (fdSortie >= 0 || fdErreur >= 0) {
    pollfd fds[3];
    int n = 0;
    if (fdSortie >= 0) fds[n++] = {fdSortie, POLLIN, 0};
    if (fdErreur >= 0) fds[n++] = {fdErreur, POLLIN, 0};
    if (fdEntree >= 0 && !aEcrire.empty()) fds[n++] = {fdEntree, POLLOUT, 0};
    ::poll(fds, static_cast<nfds_t>(n), 50);
    for (int i = 0; i < n; ++i) {
      if (!fds[i].revents) continue;
      if (fds[i].fd == fdEntree) {
        const ssize_t k = ::write(fdEntree, aEcrire.data(), std::min<std::size_t>(aEcrire.size(), 65536));
        if (k > 0) aEcrire.erase(0, static_cast<std::size_t>(k));
        else if (k < 0 && errno != EAGAIN && errno != EINTR) {
          ::close(fdEntree);
          fdEntree = -1;
        }
        continue;
      }
      const bool estSortie = fds[i].fd == fdSortie;
      int& fd = estSortie ? fdSortie : fdErreur;
      for (;;) {
        const ssize_t k = ::read(fd, tampon, sizeof tampon);
        if (k > 0) {
          if (!estSortie) {
            r.erreurs.append(tampon, static_cast<std::size_t>(k));
            continue;
          }
          reste.append(tampon, static_cast<std::size_t>(k));
          std::size_t j;
          while ((j = reste.find('\n')) != std::string::npos) {
            traiterLigne(reste.substr(0, j), repondre, r.sortie, aEcrire);
            reste.erase(0, j + 1);
          }
          continue;
        }
        if (k < 0 && (errno == EAGAIN || errno == EINTR)) break;
        ::close(fd);
        fd = -1;
        break;
      }
    }
    // Réponses écrites aussitôt que possible : le programme les attend
    while (fdEntree >= 0 && !aEcrire.empty()) {
      const ssize_t k = ::write(fdEntree, aEcrire.data(), std::min<std::size_t>(aEcrire.size(), 65536));
      if (k > 0) aEcrire.erase(0, static_cast<std::size_t>(k));
      else break;
    }
    const bool tard = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > o.delai;
    if (tard || (arret && *arret)) {
      ::kill(pid, SIGKILL);
      r.delaiDepasse = tard;
      break;
    }
  }
  if (!reste.empty()) traiterLigne(reste, repondre, r.sortie, aEcrire);
  for (int f : {fdEntree, fdSortie, fdErreur})
    if (f >= 0) ::close(f);
  int statut = 0;
  while (::waitpid(pid, &statut, 0) < 0 && errno == EINTR) {
  }
  r.code = WIFEXITED(statut) ? WEXITSTATUS(statut) : -1;
  if (r.code == 127 && r.erreurs.empty()) {
    r.lance = false;
    r.erreurs = "« " + argv[0] + " » n’a pas pu être lancé";
  }
  return r;
}
#endif

// ---------- Programme lancé qui dialogue à la demande ----------
struct ProcessusInteractif::Etat {
  std::mutex verrou;
  std::condition_variable arrivee;
  std::deque<std::string> lignes;
  std::string reste, erreurs;
  bool sortieFermee = false;
  std::thread lecteur, lecteurErreurs;
#ifdef _WIN32
  HANDLE entree = nullptr, sortie = nullptr, erreur = nullptr, processus = nullptr, fil = nullptr;
#else
  int entree = -1, sortie = -1, erreur = -1;
  pid_t pid = -1;
#endif
  bool lance = false;
  int code = -1;
  void recevoir(const char* t, std::size_t n) {
    std::lock_guard<std::mutex> g(verrou);
    reste.append(t, n);
    std::size_t i;
    while ((i = reste.find('\n')) != std::string::npos) {
      std::string l = reste.substr(0, i);
      if (!l.empty() && l.back() == '\r') l.pop_back();
      lignes.push_back(std::move(l));
      reste.erase(0, i + 1);
    }
    arrivee.notify_all();
  }
  void fin() {
    std::lock_guard<std::mutex> g(verrou);
    if (!reste.empty()) lignes.push_back(reste), reste.clear();
    sortieFermee = true;
    arrivee.notify_all();
  }
};

ProcessusInteractif::ProcessusInteractif() : m_etat(std::make_shared<Etat>()) {}
ProcessusInteractif::~ProcessusInteractif() { arreter(); }

std::optional<std::string> ProcessusInteractif::lireLigne(double delai) {
  std::unique_lock<std::mutex> g(m_etat->verrou);
  m_etat->arrivee.wait_for(g, std::chrono::duration<double>(std::max(0.0, delai)), [&] { return !m_etat->lignes.empty() || m_etat->sortieFermee; });
  if (m_etat->lignes.empty()) return std::nullopt;
  std::string l = std::move(m_etat->lignes.front());
  m_etat->lignes.pop_front();
  return l;
}

std::string ProcessusInteractif::erreurs() const {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  return m_etat->erreurs;
}

#ifdef _WIN32
bool ProcessusInteractif::demarrer(const std::vector<std::string>& argv, const OptionsProcessus& o, std::string* erreur) {
  arreter();
  m_etat = std::make_shared<Etat>();
  auto refuser = [&](const std::string& e) {
    if (erreur) *erreur = e;
    return false;
  };
  if (argv.empty()) return refuser("commande vide");
  noter(argv);
  const std::string chemin = programmeDuPoste(argv[0]);
  if (chemin.empty()) return refuser("« " + argv[0] + " » est introuvable sur ce poste");
  SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE entreeL = nullptr, entreeE = nullptr, sortieL = nullptr, sortieE = nullptr, erreurL = nullptr, erreurE = nullptr;
  if (!CreatePipe(&entreeL, &entreeE, &sa, 0) || !CreatePipe(&sortieL, &sortieE, &sa, 0) || !CreatePipe(&erreurL, &erreurE, &sa, 0)) return refuser("tubes impossibles à créer");
  SetHandleInformation(entreeE, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(sortieL, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(erreurL, HANDLE_FLAG_INHERIT, 0);
  std::wstring ligne = citer(large(chemin));
  for (std::size_t i = 1; i < argv.size(); ++i) ligne += L" " + citer(large(argv[i]));
  for (const auto& [k, v] : o.environnement) SetEnvironmentVariableW(large(k).c_str(), large(v).c_str());
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = entreeL;
  si.hStdOutput = sortieE;
  si.hStdError = erreurE;
  PROCESS_INFORMATION pi{};
  const std::wstring dossier = large(o.dossier);
  const BOOL ok = CreateProcessW(nullptr, ligne.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dossier.empty() ? nullptr : dossier.c_str(), &si, &pi);
  CloseHandle(entreeL);
  CloseHandle(sortieE);
  CloseHandle(erreurE);
  if (!ok) {
    CloseHandle(entreeE);
    CloseHandle(sortieL);
    CloseHandle(erreurL);
    return refuser("« " + argv[0] + " » n’a pas pu être lancé");
  }
  Etat& e = *m_etat;
  e.entree = entreeE;
  e.sortie = sortieL;
  e.erreur = erreurL;
  e.processus = pi.hProcess;
  e.fil = pi.hThread;
  e.lance = true;
  e.lecteur = std::thread([&e] {
    char t[8192];
    DWORD lu = 0;
    while (ReadFile(e.sortie, t, sizeof t, &lu, nullptr) && lu > 0) e.recevoir(t, lu);
    e.fin();
  });
  e.lecteurErreurs = std::thread([&e] {
    char t[4096];
    DWORD lu = 0;
    while (ReadFile(e.erreur, t, sizeof t, &lu, nullptr) && lu > 0) {
      std::lock_guard<std::mutex> g(e.verrou);
      e.erreurs.append(t, lu);
    }
  });
  return true;
}

bool ProcessusInteractif::enCours() const { return m_etat->lance && m_etat->processus && WaitForSingleObject(m_etat->processus, 0) == WAIT_TIMEOUT; }

bool ProcessusInteractif::ecrire(const std::string& texte) {
  if (!m_etat->entree) return false;
  DWORD ecrit = 0;
  std::size_t fait = 0;
  while (fait < texte.size()) {
    if (!WriteFile(m_etat->entree, texte.data() + fait, static_cast<DWORD>(std::min<std::size_t>(texte.size() - fait, 65536)), &ecrit, nullptr) || ecrit == 0) return false;
    fait += ecrit;
  }
  return true;
}

void ProcessusInteractif::fermerEntree() {
  if (m_etat->entree) CloseHandle(m_etat->entree), m_etat->entree = nullptr;
}

int ProcessusInteractif::arreter() {
  Etat& e = *m_etat;
  if (!e.lance) return e.code;
  fermerEntree();
  if (WaitForSingleObject(e.processus, 300) == WAIT_TIMEOUT) TerminateProcess(e.processus, 1);
  WaitForSingleObject(e.processus, INFINITE);
  DWORD code = 0;
  GetExitCodeProcess(e.processus, &code);
  e.code = static_cast<int>(code);
  if (e.lecteur.joinable()) e.lecteur.join();
  if (e.lecteurErreurs.joinable()) e.lecteurErreurs.join();
  for (HANDLE* h : {&e.sortie, &e.erreur, &e.processus, &e.fil})
    if (*h) CloseHandle(*h), *h = nullptr;
  e.lance = false;
  return e.code;
}
#else
bool ProcessusInteractif::demarrer(const std::vector<std::string>& argv, const OptionsProcessus& o, std::string* erreur) {
  arreter();
  m_etat = std::make_shared<Etat>();
  auto refuser = [&](const std::string& e) {
    if (erreur) *erreur = e;
    return false;
  };
  if (argv.empty()) return refuser("commande vide");
  noter(argv);
  const std::string chemin = programmeDuPoste(argv[0]);
  if (chemin.empty()) return refuser("« " + argv[0] + " » est introuvable sur ce poste");
  static const bool sansSigpipe = [] {
    ::signal(SIGPIPE, SIG_IGN);
    return true;
  }();
  (void)sansSigpipe;
  std::map<std::string, std::string> env;
  for (char** e = environ; e && *e; ++e) {
    const std::string s = *e;
    if (const std::size_t i = s.find('='); i != std::string::npos) env[s.substr(0, i)] = s.substr(i + 1);
  }
  for (const auto& [k, v] : o.environnement) env[k] = v;
  std::vector<std::string> textesEnv, args(argv);
  for (const auto& [k, v] : env) textesEnv.push_back(k + "=" + v);
  std::vector<char*> envp, av;
  for (auto& s : textesEnv) envp.push_back(s.data());
  envp.push_back(nullptr);
  for (auto& s : args) av.push_back(s.data());
  av.push_back(nullptr);
  int entree[2] = {-1, -1}, sortie[2] = {-1, -1}, err[2] = {-1, -1};
  if (::pipe(entree) != 0 || ::pipe(sortie) != 0 || ::pipe(err) != 0) {
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], err[0], err[1]})
      if (f >= 0) ::close(f);
    return refuser("tubes impossibles à créer");
  }
  std::string raison;
  const pid_t pid = lancerEnfant(chemin, av, envp, o.dossier, entree[0], sortie[1], err[1], {entree[0], entree[1], sortie[0], sortie[1], err[0], err[1]}, &raison);
  if (pid < 0) {
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], err[0], err[1]}) ::close(f);
    return refuser(raison);
  }
  ::close(entree[0]);
  ::close(sortie[1]);
  ::close(err[1]);
  Etat& e = *m_etat;
  e.entree = entree[1];
  e.sortie = sortie[0];
  e.erreur = err[0];
  e.pid = pid;
  e.lance = true;
  e.lecteur = std::thread([&e] {
    char t[8192];
    for (;;) {
      const ssize_t k = ::read(e.sortie, t, sizeof t);
      if (k > 0) e.recevoir(t, static_cast<std::size_t>(k));
      else if (k < 0 && errno == EINTR) continue;
      else break;
    }
    e.fin();
  });
  e.lecteurErreurs = std::thread([&e] {
    char t[4096];
    for (;;) {
      const ssize_t k = ::read(e.erreur, t, sizeof t);
      if (k > 0) {
        std::lock_guard<std::mutex> g(e.verrou);
        e.erreurs.append(t, static_cast<std::size_t>(k));
      } else if (k < 0 && errno == EINTR) continue;
      else break;
    }
  });
  return true;
}

bool ProcessusInteractif::enCours() const {
  if (!m_etat->lance || m_etat->pid <= 0) return false;
  int statut = 0;
  const pid_t r = ::waitpid(m_etat->pid, &statut, WNOHANG);
  if (r == 0) return true;
  if (r == m_etat->pid) {
    m_etat->code = WIFEXITED(statut) ? WEXITSTATUS(statut) : -1;
    m_etat->pid = -1;
  }
  return false;
}

bool ProcessusInteractif::ecrire(const std::string& texte) {
  if (m_etat->entree < 0) return false;
  std::size_t fait = 0;
  while (fait < texte.size()) {
    const ssize_t k = ::write(m_etat->entree, texte.data() + fait, std::min<std::size_t>(texte.size() - fait, 65536));
    if (k > 0) fait += static_cast<std::size_t>(k);
    else if (k < 0 && errno == EINTR) continue;
    else return false;
  }
  return true;
}

void ProcessusInteractif::fermerEntree() {
  if (m_etat->entree >= 0) ::close(m_etat->entree), m_etat->entree = -1;
}

int ProcessusInteractif::arreter() {
  Etat& e = *m_etat;
  if (!e.lance) return e.code;
  fermerEntree();
  if (e.pid > 0) {
    int statut = 0;
    pid_t r = 0;
    for (int i = 0; i < 30 && (r = ::waitpid(e.pid, &statut, WNOHANG)) == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (r == 0) {
      ::kill(e.pid, SIGKILL);
      while (::waitpid(e.pid, &statut, 0) < 0 && errno == EINTR) {
      }
      e.code = -1;
    } else if (r == e.pid)
      e.code = WIFEXITED(statut) ? WEXITSTATUS(statut) : -1;
    e.pid = -1;
  }
  if (e.lecteur.joinable()) e.lecteur.join();
  if (e.lecteurErreurs.joinable()) e.lecteurErreurs.join();
  for (int* f : {&e.sortie, &e.erreur})
    if (*f >= 0) ::close(*f), *f = -1;
  e.lance = false;
  return e.code;
}
#endif

}  // namespace prom
