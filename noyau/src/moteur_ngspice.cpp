// Prométhée : moteur de simulation ngspice, chargé comme bibliothèque partagée au moment du calcul (lot L11, 11.1).
// Les déclarations de l'interface partagée de ngspice (sharedspice.h, licence BSD modifiée) sont reprises ici : la
// bibliothèque n'est pas nécessaire pour compiler Prométhée, seulement pour simuler. Licence GPL-3.0-only.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#include "promethee/simulation.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace prom::ecad {

namespace {
// ---------- Interface de ngspice (sharedspice.h) ----------
struct NgComplexe {
  double reel, imaginaire;
};
struct InfoVecteur {
  char* nom;
  int type;
  short drapeaux;
  double* reels;
  NgComplexe* complexes;
  int longueur;
};
using SendChar = int(char*, int, void*);
using SendStat = int(char*, int, void*);
using ControlledExit = int(int, bool, bool, int, void*);
using SendData = int(void*, int, int, void*);
using SendInitData = int(void*, int, void*);
using BGThreadRunning = int(bool, int, void*);
using FnInit = int (*)(SendChar*, SendStat*, ControlledExit*, SendData*, SendInitData*, BGThreadRunning*, void*);
using FnCirc = int (*)(char**);
using FnCommande = int (*)(char*);
using FnVecteur = InfoVecteur* (*)(char*);
using FnPlotCourant = char* (*)();
using FnTousVecteurs = char** (*)(char*);
using FnEnCours = bool (*)();

struct Moteur {
  bool charge = false, initialise = false;
  std::string version;
  FnInit init = nullptr;
  FnCirc circ = nullptr;
  FnCommande commande = nullptr;
  FnVecteur vecteur = nullptr;
  FnPlotCourant plotCourant = nullptr;
  FnTousVecteurs tousVecteurs = nullptr;
  FnEnCours enCours = nullptr;
  // État d'un calcul
  std::mutex verrou;  // un seul calcul à la fois : ngspice partagé n'est pas réentrant
  std::mutex etat;
  std::condition_variable fin;
  bool fondEnCours = false, sorti = false;
  std::vector<std::string> console;
};

Moteur& moteur() {
  static Moteur m;
  return m;
}

int recevoirTexte(char* t, int, void*) {
  Moteur& m = moteur();
  std::lock_guard<std::mutex> g(m.etat);
  if (t) m.console.emplace_back(t);
  if (m.console.size() > 4000) m.console.erase(m.console.begin(), m.console.begin() + 1000);
  return 0;
}
int recevoirEtat(char*, int, void*) { return 0; }
int sortieControlee(int, bool, bool, int, void*) {
  Moteur& m = moteur();
  std::lock_guard<std::mutex> g(m.etat);
  m.sorti = true;
  m.fondEnCours = false;
  m.fin.notify_all();
  return 0;
}
int filDeFond(bool arrete, int, void*) {
  // « arrete » vrai : le fil de calcul de ngspice vient de finir
  Moteur& m = moteur();
  std::lock_guard<std::mutex> g(m.etat);
  m.fondEnCours = !arrete;
  m.fin.notify_all();
  return 0;
}

void* symbole(void* bib, const char* nom) {
#ifdef _WIN32
  return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(bib), nom));
#else
  return dlsym(bib, nom);
#endif
}

bool charger() {
  Moteur& m = moteur();
  if (m.charge) return true;
  static bool essaye = false;
  if (essaye) return false;
  essaye = true;
  std::vector<std::string> noms;
  if (const char* force = std::getenv("PROMETHEE_NGSPICE"); force && *force) noms.push_back(force);
#ifdef _WIN32
  noms.insert(noms.end(), {"ngspice.dll", "libngspice-0.dll", "libngspice.dll"});
#elif defined(__APPLE__)
  noms.insert(noms.end(), {"libngspice.dylib", "libngspice.0.dylib", "/opt/homebrew/lib/libngspice.dylib", "/usr/local/lib/libngspice.dylib"});
#else
  noms.insert(noms.end(), {"libngspice.so.0", "libngspice.so"});
#endif
  void* bib = nullptr;
  for (const auto& n : noms) {
#ifdef _WIN32
    bib = LoadLibraryA(n.c_str());
#else
    bib = dlopen(n.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (bib) break;
  }
  if (!bib) return false;
  m.init = reinterpret_cast<FnInit>(symbole(bib, "ngSpice_Init"));
  m.circ = reinterpret_cast<FnCirc>(symbole(bib, "ngSpice_Circ"));
  m.commande = reinterpret_cast<FnCommande>(symbole(bib, "ngSpice_Command"));
  m.vecteur = reinterpret_cast<FnVecteur>(symbole(bib, "ngGet_Vec_Info"));
  m.plotCourant = reinterpret_cast<FnPlotCourant>(symbole(bib, "ngSpice_CurPlot"));
  m.tousVecteurs = reinterpret_cast<FnTousVecteurs>(symbole(bib, "ngSpice_AllVecs"));
  m.enCours = reinterpret_cast<FnEnCours>(symbole(bib, "ngSpice_running"));
  m.charge = m.init && m.circ && m.commande && m.vecteur && m.plotCourant && m.tousVecteurs;
  return m.charge;
}

bool initialiser() {
  Moteur& m = moteur();
  if (!charger()) return false;
  if (m.initialise) return true;
  m.init(recevoirTexte, recevoirEtat, sortieControlee, nullptr, nullptr, filDeFond, nullptr);
  m.initialise = true;
  // La version, annoncée au premier « version »
  m.commande(const_cast<char*>("version -s"));
  {
    std::lock_guard<std::mutex> g(m.etat);
    for (const auto& l : m.console)
      if (const auto p = l.find("ngspice-"); p != std::string::npos) {
        m.version = l.substr(p);
        while (!m.version.empty() && std::isspace(static_cast<unsigned char>(m.version.back()))) m.version.pop_back();
        break;
      }
    m.console.clear();
  }
  return true;
}
}  // namespace

bool ngspiceDisponible() { return initialiser(); }

std::string versionNgspice() {
  if (!initialiser()) return {};
  return moteur().version.empty() ? std::string("ngspice") : moteur().version;
}

ResultatSimulation simuler(const std::string& netlist, const std::atomic<bool>* arret, double delai) { return simuler(netlist, OptionsMoteur{}, arret, delai); }

ResultatSimulation simuler(const NetlistSpice& n, const std::atomic<bool>* arret, double delai) {
  return simuler(n.texte, OptionsMoteur{n.compatibilite, n.chargements}, arret, delai);
}

ResultatSimulation simuler(const std::string& netlist, const OptionsMoteur& o, const std::atomic<bool>* arret, double delai) {
  ResultatSimulation r;
  Moteur& m = moteur();
  if (!initialiser()) {
    r.erreur = "ngspice n'est pas installé : la bibliothèque partagée libngspice est introuvable (PROMETHEE_NGSPICE peut donner son chemin)";
    return r;
  }
  std::lock_guard<std::mutex> unique(m.verrou);
  {
    std::lock_guard<std::mutex> g(m.etat);
    m.console.clear();
    m.sorti = false;
  }
  m.commande(const_cast<char*>("destroy all"));
  m.commande(const_cast<char*>("remcirc"));
  // Compatibilité (ngbehavior, lue avec le circuit) et modèles compilés à charger avant lui
  {
    std::string c;
    for (const char ch : o.compatibilite)
      if (std::isalnum(static_cast<unsigned char>(ch))) c += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::string cmd = c.empty() ? std::string("unset ngbehavior") : "set ngbehavior=" + c;
    m.commande(cmd.data());
    for (const auto& f : o.chargements) {
      std::string x = "osdi '" + f + "'";
      m.commande(x.data());
    }
  }
  // Le circuit, ligne par ligne
  std::vector<std::string> lignes;
  std::istringstream in(netlist);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    lignes.push_back(l);
  }
  std::vector<char*> c;
  for (auto& l : lignes) c.push_back(l.data());
  c.push_back(nullptr);
  m.circ(c.data());
  {
    std::lock_guard<std::mutex> g(m.etat);
    for (const auto& l : m.console)
      if (l.find("stderr Error") != std::string::npos || l.find("Error on line") != std::string::npos) r.erreur = l.substr(l.find("Error"));
  }
  if (!r.erreur.empty()) {
    std::lock_guard<std::mutex> g(m.etat);
    r.console = m.console;
    return r;
  }
  // Calcul sur le fil de ngspice ; on attend sa fin, l'arrêt demandé ou le délai
  {
    std::lock_guard<std::mutex> g(m.etat);
    m.fondEnCours = true;
  }
  m.commande(const_cast<char*>("bg_run"));
  const auto t0 = std::chrono::steady_clock::now();
  {
    std::unique_lock<std::mutex> g(m.etat);
    while (m.fondEnCours && !m.sorti) {
      m.fin.wait_for(g, std::chrono::milliseconds(50));
      const bool stop = (arret && arret->load()) || std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > delai;
      if (stop && m.fondEnCours) {
        g.unlock();
        m.commande(const_cast<char*>("bg_halt"));
        g.lock();
        r.interrompu = true;
        // Attendre que le fil de ngspice ait vraiment rendu la main
        for (int i = 0; i < 100 && m.fondEnCours; ++i) m.fin.wait_for(g, std::chrono::milliseconds(50));
        break;
      }
    }
  }
  if (r.interrompu) {
    r.erreur = arret && arret->load() ? "calcul arrêté" : "calcul trop long : arrêté au bout du délai";
    std::lock_guard<std::mutex> g(m.etat);
    r.console = m.console;
    return r;
  }
  // Vecteurs du tracé courant
  char* plot = m.plotCourant();
  if (char** noms = plot ? m.tousVecteurs(plot) : nullptr)
    for (char** v = noms; *v; ++v) {
      const std::string nom = *v;
      const std::string complet = std::string(plot) + "." + nom;
      InfoVecteur* info = m.vecteur(const_cast<char*>(complet.c_str()));
      if (!info || info->longueur <= 0) continue;
      if (info->reels) r.reels[nom].assign(info->reels, info->reels + info->longueur);
      else if (info->complexes) {
        auto& z = r.complexes[nom];
        for (int i = 0; i < info->longueur; ++i) z.emplace_back(info->complexes[i].reel, info->complexes[i].imaginaire);
      }
    }
  for (const char* e : {"time", "frequency", "v-sweep", "i-sweep", "temp-sweep", "res-sweep"})
    if (r.reels.count(e) || r.complexes.count(e)) {
      r.echelle = e;
      break;
    }
  // Une analyse alternative rend l'échelle des fréquences en complexes : sa partie réelle
  if (r.echelle == "frequency" && !r.reels.count("frequency")) {
    auto& f = r.reels["frequency"];
    for (const auto& z : r.complexes["frequency"]) f.push_back(z.real());
  }
  std::lock_guard<std::mutex> g(m.etat);
  r.console = m.console;
  // Un calcul arrêté en route (pas effondré, matrice singulière) laisse des vecteurs partiels : c'est un échec
  for (const auto& l : m.console)
    if (l.find("simulation(s) aborted") != std::string::npos || l.find("Timestep too small") != std::string::npos || l.find("timestep too small") != std::string::npos) {
      for (auto it = m.console.rbegin(); it != m.console.rend(); ++it)
        if (it->find("too small") != std::string::npos || it->find("singular") != std::string::npos || it->find("rror") != std::string::npos) {
          r.erreur = *it;
          break;
        }
      if (r.erreur.empty()) r.erreur = l;
      break;
    }
  if (r.reels.empty() && r.complexes.empty()) {
    for (auto it = m.console.rbegin(); it != m.console.rend(); ++it)
      if (it->find("rror") != std::string::npos || it->find("too small") != std::string::npos || it->find("singular") != std::string::npos) {
        r.erreur = *it;
        break;
      }
    if (r.erreur.empty()) r.erreur = "ngspice n'a rendu aucun résultat";
  }
  return r;
}

std::vector<std::string> commandeConsole(const std::string& commande, std::string* refus) {
  std::string mot;
  for (const char ch : commande) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (mot.empty()) continue;
      break;
    }
    mot += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  static const std::vector<std::string> permises = {"print", "let", "meas", "measure", "display", "show", "showmod", "listing", "setplot", "echo", "version",
                                                    "op", "tran", "ac", "dc", "run", "alter", "altermod", "status", "noise", "tf", "four"};
  if (std::find(permises.begin(), permises.end(), mot) == permises.end() || commande.find_first_of(";`|&<>") != std::string::npos) {
    if (refus) *refus = mot.empty() ? std::string("commande vide") : "« " + mot + " » n’est pas permise dans la console (lecture et calcul seulement)";
    return {};
  }
  Moteur& m = moteur();
  if (!initialiser()) {
    if (refus) *refus = "ngspice n'est pas installé";
    return {};
  }
  std::lock_guard<std::mutex> unique(m.verrou);
  std::size_t avant = 0;
  {
    std::lock_guard<std::mutex> g(m.etat);
    avant = m.console.size();
  }
  std::string c = commande;
  m.commande(c.data());
  std::lock_guard<std::mutex> g(m.etat);
  std::vector<std::string> r;
  for (std::size_t i = std::min(avant, m.console.size()); i < m.console.size(); ++i) {
    std::string l = m.console[i];
    for (const char* p : {"stdout ", "stderr "})
      if (l.rfind(p, 0) == 0) l = l.substr(7);
    r.push_back(l);
  }
  return r;
}

}  // namespace prom::ecad
