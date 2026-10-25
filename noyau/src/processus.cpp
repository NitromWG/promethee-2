// Prométhée : programmes du poste lancés par le noyau. Voir processus.hpp. Licence GPL-3.0-only.
#include "promethee/processus.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
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
  const std::string dossier = o.dossier;
  const pid_t pid = ::fork();
  if (pid < 0) {
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]}) ::close(f);
    r.erreurs = "processus impossible à créer";
    return r;
  }
  if (pid == 0) {
    ::dup2(entree[0], 0);
    ::dup2(sortie[1], 1);
    ::dup2(erreur[1], 2);
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]}) ::close(f);
    if (!dossier.empty() && ::chdir(dossier.c_str()) != 0) ::_exit(126);
    ::execve(chemin.c_str(), av.data(), envp.data());
    ::_exit(127);
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
  const std::string dossier = o.dossier;
  const pid_t pid = ::fork();
  if (pid < 0) {
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]}) ::close(f);
    r.erreurs = "processus impossible à créer";
    return r;
  }
  if (pid == 0) {
    ::dup2(entree[0], 0);
    ::dup2(sortie[1], 1);
    ::dup2(erreur[1], 2);
    for (int f : {entree[0], entree[1], sortie[0], sortie[1], erreur[0], erreur[1]}) ::close(f);
    if (!dossier.empty() && ::chdir(dossier.c_str()) != 0) ::_exit(126);
    ::execve(chemin.c_str(), av.data(), envp.data());
    ::_exit(127);
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

}  // namespace prom
