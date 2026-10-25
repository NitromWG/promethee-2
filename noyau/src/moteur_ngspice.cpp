// Prométhée : moteur de simulation ngspice, chargé comme bibliothèque partagée au moment du calcul (lot L11, 11.1).
// Les déclarations de l'interface partagée de ngspice (sharedspice.h, licence BSD modifiée) sont reprises ici : la
// bibliothèque n'est pas nécessaire pour compiler Prométhée, seulement pour simuler. Licence GPL-3.0-only.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>

#include <deque>
#include <map>

#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"

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
// Valeurs de tous les vecteurs à chaque pas accepté (SendData) et sources externes (ngSpice_Init_Sync)
struct ValeurVecteur {
  char* nom;
  double reel, imaginaire;
  bool echelle, complexe;
};
struct ValeursPas {
  int nombre, indice;
  ValeurVecteur** valeurs;
};
using GetSourceData = int(double*, double, char*, int, void*);
using GetSyncData = int(double, double*, double, int, int, int, void*);
using FnInitSync = int (*)(GetSourceData*, GetSourceData*, GetSyncData*, int*, void*);
using SendChar = int(char*, int, void*);
using SendStat = int(char*, int, void*);
using ControlledExit = int(int, bool, bool, int, void*);
using SendData = int(ValeursPas*, int, int, void*);
using SendInitData = int(void*, int, void*);
// Sans ce rappel, ngspice ne rend pas non plus les valeurs de chaque pas (SendData)
int recevoirInit(void*, int, void*) { return 0; }
using BGThreadRunning = int(bool, int, void*);
using FnInit = int (*)(SendChar*, SendStat*, ControlledExit*, SendData*, SendInitData*, BGThreadRunning*, void*);
using FnCirc = int (*)(char**);
using FnCommande = int (*)(char*);
using FnVecteur = InfoVecteur* (*)(char*);
using FnPlotCourant = char* (*)();
using FnTousVecteurs = char** (*)(char*);
using FnEnCours = bool (*)();
using FnTousTraces = char** (*)();

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
  FnInitSync initSync = nullptr;
  FnTousTraces tousTraces = nullptr;
  // Session continue (14.2) : une à la fois ; les rappels de ngspice la lisent
  std::mutex sessionVerrou;
  std::shared_ptr<SimulationContinue::Etat> session;
  std::atomic<bool> sessionActive{false};
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
void finSession(bool arrete);
int filDeFond(bool arrete, int, void*) {
  // « arrete » vrai : le fil de calcul de ngspice vient de finir
  Moteur& m = moteur();
  {
    std::lock_guard<std::mutex> g(m.etat);
    m.fondEnCours = !arrete;
    m.fin.notify_all();
  }
  if (arrete) finSession(true);
  return 0;
}

// ---------- Session continue (14.2) ----------
std::shared_ptr<SimulationContinue::Etat> sessionCourante() {
  Moteur& m = moteur();
  if (!m.sessionActive.load()) return nullptr;
  std::lock_guard<std::mutex> g(m.sessionVerrou);
  return m.session;
}
std::string nomNormalise(std::string n) {
  for (auto& ch : n) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  while (!n.empty() && std::isspace(static_cast<unsigned char>(n.back()))) n.pop_back();
  // « v(n_x) » et « n_x » désignent la même tension ; « i(v1) » est « v1#branch »
  if (n.size() > 3 && n.rfind("v(", 0) == 0 && n.back() == ')' && n.find(',') == std::string::npos) n = n.substr(2, n.size() - 3);
  else if (n.size() > 3 && n.rfind("i(", 0) == 0 && n.back() == ')') n = n.substr(2, n.size() - 3) + "#branch";
  return n;
}
}  // namespace

struct SimulationContinue::Etat {
  mutable std::mutex verrou;
  std::condition_variable reveil;
  std::vector<std::string> sondes;
  std::map<std::string, std::size_t> indices;
  std::vector<std::deque<Echantillon>> tampons;
  std::vector<double> dernieres;
  std::vector<int> correspondance;  // vecteur du pas → sonde (−1 : non relevé, −2 : l'échelle)
  bool correspondanceFaite = false;
  std::map<std::string, double> fixes;
  std::map<std::string, SourceGeneree> generateurs;
  // Participants (15.4, 15.7) et, pour chacun, ses sondes : nom donné → indice du tampon
  std::vector<std::shared_ptr<ParticipantContinu>> participants;
  std::vector<std::vector<std::pair<std::string, std::size_t>>> sondesParticipants;
  std::size_t taille = 200'000;
  double temps = 0, vitesse = 1, sim0 = 0;
  std::chrono::steady_clock::time_point mur0 = std::chrono::steady_clock::now();
  std::uint64_t pas = 0;
  std::atomic<bool> enCours{false}, pause{false}, arret{false};
  std::string erreur;
  double valeurSource(const std::string& nom, double t) const {
    auto chercher = [&](const std::string& n, double& v) {
      if (const auto it = generateurs.find(n); it != generateurs.end()) {
        v = it->second.valeur(t);
        return true;
      }
      if (const auto it = fixes.find(n); it != fixes.end()) {
        v = it->second;
        return true;
      }
      return false;
    };
    double v = 0;
    const std::string n = nomNormalise(nom);
    if (chercher(n, v)) return v;
    if (!n.empty() && (n[0] == 'v' || n[0] == 'i') && chercher(n.substr(1), v)) return v;
    if (chercher("v" + n, v)) return v;
    return 0;
  }
};

namespace {
// Un circuit refusé : la ligne « Error on line … » ne dit que l'endroit. Un modèle numérique introuvable (MIF-ERROR)
// veut dire, presque toujours, que ngspice a été installé sans ses modèles XSPICE (sous Linux, libngspice0 seul) :
// le message le dit, avec ce qu'il faut installer
std::string expliquerErreurCircuit(const std::vector<std::string>& console, const std::string& erreur) {
  for (const auto& l : console)
    if (const auto p = l.find("MIF-ERROR"); p != std::string::npos) {
      std::string detail = l.substr(p);
      while (!detail.empty() && std::isspace(static_cast<unsigned char>(detail.back()))) detail.pop_back();
      return "les modèles numériques de ngspice (XSPICE : portes, bascules, ponts) ne sont pas chargés (" + detail +
             ") ; installer ngspice en entier : sous Linux, le paquet « ngspice » en plus de « libngspice0 »";
    }
  return erreur;
}

int recevoirPas(ValeursPas* v, int, int, void*) {
  const std::shared_ptr<SimulationContinue::Etat> s = sessionCourante();
  if (!s || !v || !v->valeurs) return 0;
  double t = 0, vitesse = 1, sim0 = 0;
  std::chrono::steady_clock::time_point mur0;
  {
    std::unique_lock<std::mutex> g(s->verrou);
    if (!s->correspondanceFaite) {
      s->correspondance.assign(static_cast<std::size_t>(std::max(0, v->nombre)), -1);
      for (int i = 0; i < v->nombre; ++i) {
        const ValeurVecteur* x = v->valeurs[i];
        if (!x || !x->nom) continue;
        if (x->echelle) {
          s->correspondance[static_cast<std::size_t>(i)] = -2;
          continue;
        }
        if (const auto it = s->indices.find(nomNormalise(x->nom)); it != s->indices.end()) s->correspondance[static_cast<std::size_t>(i)] = static_cast<int>(it->second);
      }
      s->correspondanceFaite = true;
    }
    for (int i = 0; i < v->nombre && i < static_cast<int>(s->correspondance.size()); ++i)
      if (s->correspondance[static_cast<std::size_t>(i)] == -2 && v->valeurs[i]) t = v->valeurs[i]->reel;
    for (int i = 0; i < v->nombre && i < static_cast<int>(s->correspondance.size()); ++i) {
      const int k = s->correspondance[static_cast<std::size_t>(i)];
      if (k < 0 || !v->valeurs[i]) continue;
      auto& tampon = s->tampons[static_cast<std::size_t>(k)];
      tampon.push_back({t, v->valeurs[i]->reel});
      while (tampon.size() > s->taille) tampon.pop_front();
      s->dernieres[static_cast<std::size_t>(k)] = v->valeurs[i]->reel;
    }
    s->temps = t;
    ++s->pas;
  }
  // Participants : hors du verrou (un participant arrêté sur un point d'arrêt retient le fil, la lecture des
  // tampons reste possible)
  if (!s->participants.empty()) {
    std::vector<std::map<std::string, double>> valeurs(s->participants.size());
    {
      std::lock_guard<std::mutex> g(s->verrou);
      for (std::size_t i = 0; i < s->participants.size(); ++i)
        for (const auto& [nom, k] : s->sondesParticipants[i]) valeurs[i][nom] = s->dernieres[k];
    }
    const auto avant = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < s->participants.size() && !s->arret; ++i) s->participants[i]->pas(t, valeurs[i]);
    // Un long arrêt (point d'arrêt) ne se rattrape pas : l'horloge de référence se décale d'autant
    const auto duree = std::chrono::steady_clock::now() - avant;
    if (duree > std::chrono::milliseconds(50)) {
      std::lock_guard<std::mutex> g(s->verrou);
      s->mur0 += duree;
    }
  }
  {
    std::unique_lock<std::mutex> g(s->verrou);
    // Pause : le fil de ngspice attend ici
    while (s->pause && !s->arret) s->reveil.wait(g);
    vitesse = s->vitesse;
    sim0 = s->sim0;
    mur0 = s->mur0;
  }
  // Temps réel : le pas attend son heure (par tranches courtes, pour répondre à la pause et à l'arrêt)
  if (vitesse > 0) {
    const auto cible = mur0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>((t - sim0) / vitesse));
    for (auto maintenant = std::chrono::steady_clock::now(); maintenant < cible && !s->arret && !s->pause; maintenant = std::chrono::steady_clock::now())
      std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(cible - maintenant, std::chrono::milliseconds(20)));
  }
  return 0;
}
int sourceExterneTension(double* valeur, double t, char* nom, int, void*) {
  if (!valeur) return 0;
  *valeur = 0;
  const std::shared_ptr<SimulationContinue::Etat> s = sessionCourante();
  if (!s || !nom) return 0;
  // Sources tenues par un participant (broches co-simulées), puis réglées par l'interface
  if (!s->participants.empty()) {
    const std::string n = nomNormalise(nom);
    for (const auto& p : s->participants)
      if (p->source(n, t, *valeur)) return 0;
  }
  std::lock_guard<std::mutex> g(s->verrou);
  *valeur = s->valeurSource(nom, t);
  return 0;
}
int sourceExterneCourant(double* valeur, double t, char* nom, int i, void* u) { return sourceExterneTension(valeur, t, nom, i, u); }
void finSession(bool) {
  Moteur& m = moteur();
  std::shared_ptr<SimulationContinue::Etat> s;
  {
    std::lock_guard<std::mutex> g(m.sessionVerrou);
    s = m.session;
  }
  if (s) {
    s->enCours = false;
    s->reveil.notify_all();
  }
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
  m.initSync = reinterpret_cast<FnInitSync>(symbole(bib, "ngSpice_Init_Sync"));
  m.tousTraces = reinterpret_cast<FnTousTraces>(symbole(bib, "ngSpice_AllPlots"));
  m.charge = m.init && m.circ && m.commande && m.vecteur && m.plotCourant && m.tousVecteurs;
  return m.charge;
}

bool initialiser() {
  Moteur& m = moteur();
  if (!charger()) return false;
  if (m.initialise) return true;
  m.init(recevoirTexte, recevoirEtat, sortieControlee, recevoirPas, recevoirInit, filDeFond, nullptr);
  if (m.initSync) {
    static int identifiant = 0;
    m.initSync(sourceExterneTension, sourceExterneCourant, nullptr, &identifiant, nullptr);
  }
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
  if (m.sessionActive.load()) {
    r.erreur = "une simulation continue tourne : arrête-la avant un autre calcul";
    return r;
  }
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
    if (!r.erreur.empty()) r.erreur = expliquerErreurCircuit(m.console, r.erreur);
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
  // Les autres tracés du calcul (bruit : densités dans noise1, totaux dans noise2), sous « tracé.vecteur »
  if (m.tousTraces && plot)
    if (char** traces = m.tousTraces())
      for (char** p = traces; *p; ++p) {
        const std::string nomTrace = *p;
        if (nomTrace == plot || nomTrace == "const") continue;
        if (char** noms = m.tousVecteurs(*p))
          for (char** v = noms; *v; ++v) {
            const std::string complet = nomTrace + "." + *v;
            InfoVecteur* info = m.vecteur(const_cast<char*>(complet.c_str()));
            if (!info || info->longueur <= 0) continue;
            if (info->reels) r.reels[complet].assign(info->reels, info->reels + info->longueur);
            else if (info->complexes) {
              auto& z = r.complexes[complet];
              for (int i = 0; i < info->longueur; ++i) z.emplace_back(info->complexes[i].reel, info->complexes[i].imaginaire);
            }
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
  if (m.sessionActive.load()) {
    if (refus) *refus = "une simulation continue tourne";
    return {};
  }
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

// ---------- Session continue (14.2) ----------
SimulationContinue::SimulationContinue() : m_etat(std::make_shared<Etat>()) {}
SimulationContinue::~SimulationContinue() { arreter(); }

bool SimulationContinue::demarrer(const std::string& netlist, const std::vector<std::string>& sondes, const OptionsContinue& o, std::string* erreur) {
  arreter();
  auto refuser = [&](const std::string& e) {
    m_etat->erreur = e;
    if (erreur) *erreur = e;
    return false;
  };
  Moteur& m = moteur();
  if (!initialiser()) return refuser("ngspice n'est pas installé : la bibliothèque partagée libngspice est introuvable");
  if (!m.initSync) return refuser("cette bibliothèque ngspice n'offre pas les sources externes (ngSpice_Init_Sync)");
  std::unique_lock<std::mutex> unique(m.verrou, std::try_to_lock);
  if (!unique.owns_lock()) return refuser("un autre calcul tourne : attends sa fin");
  // État neuf, sources réglées d'avance gardées ; chaque générateur repart de t = 0 avec son dernier réglage (les
  // tronçons d'une session précédente suivaient l'ancienne échelle de temps)
  auto e = std::make_shared<Etat>();
  {
    std::lock_guard<std::mutex> g(m_etat->verrou);
    e->fixes = m_etat->fixes;
    for (const auto& [nom, gen] : m_etat->generateurs) e->generateurs.emplace(nom, SourceGeneree(gen.generateur()));
    e->participants = m_etat->participants;
  }
  e->taille = std::max<std::size_t>(16, o.tampon);
  e->vitesse = std::max(0.0, o.vitesse);
  auto ajouterSonde = [&](const std::string& x) -> std::size_t {
    const std::string n = nomNormalise(x);
    if (const auto it = e->indices.find(n); it != e->indices.end()) return it->second;
    e->indices[n] = e->sondes.size();
    e->sondes.push_back(n);
    return e->sondes.size() - 1;
  };
  for (const auto& x : sondes)
    if (!nomNormalise(x).empty()) ajouterSonde(x);
  for (const auto& p : e->participants) {
    std::vector<std::pair<std::string, std::size_t>> v;
    for (const auto& x : p->sondes())
      if (!nomNormalise(x).empty()) v.emplace_back(x, ajouterSonde(x));
    e->sondesParticipants.push_back(v);
  }
  e->tampons.resize(e->sondes.size());
  e->dernieres.assign(e->sondes.size(), std::numeric_limits<double>::quiet_NaN());
  {
    std::lock_guard<std::mutex> g(m.etat);
    m.console.clear();
    m.sorti = false;
  }
  m.commande(const_cast<char*>("destroy all"));
  m.commande(const_cast<char*>("remcirc"));
  m.commande(const_cast<char*>("unset ngbehavior"));
  std::vector<std::string> lignes;
  std::istringstream in(netlist);
  for (std::string l; std::getline(in, l);) {
    if (!l.empty() && l.back() == '\r') l.pop_back();
    lignes.push_back(l);
  }
  std::vector<char*> c;
  for (auto& l : lignes) c.push_back(l.data());
  c.push_back(nullptr);
  {
    std::lock_guard<std::mutex> g(m.sessionVerrou);
    m.session = e;
  }
  m.sessionActive = true;
  m.circ(c.data());
  {
    std::lock_guard<std::mutex> g(m.etat);
    for (const auto& l : m.console)
      if (l.find("stderr Error") != std::string::npos || l.find("Error on line") != std::string::npos) e->erreur = l.substr(l.find("Error"));
    if (!e->erreur.empty()) e->erreur = expliquerErreurCircuit(m.console, e->erreur);
  }
  if (!e->erreur.empty()) {
    m.sessionActive = false;
    std::lock_guard<std::mutex> g(m.sessionVerrou);
    m.session.reset();
    m_etat = e;
    if (erreur) *erreur = e->erreur;
    return false;
  }
  e->mur0 = std::chrono::steady_clock::now();
  e->sim0 = 0;
  e->enCours = true;
  {
    std::lock_guard<std::mutex> g(m.etat);
    m.fondEnCours = true;
  }
  m.commande(const_cast<char*>("bg_run"));
  m_etat = e;
  return true;
}

void SimulationContinue::arreter() {
  const std::shared_ptr<Etat> e = m_etat;
  Moteur& m = moteur();
  {
    std::lock_guard<std::mutex> g(m.sessionVerrou);
    if (!e || m.session != e) return;
  }
  e->arret = true;
  e->reveil.notify_all();
  for (const auto& p : e->participants) p->interrompre();
  if (m.commande) {
    bool tourne = false;
    {
      std::lock_guard<std::mutex> g(m.etat);
      tourne = m.fondEnCours;
    }
    if (tourne) {
      m.commande(const_cast<char*>("bg_halt"));
      std::unique_lock<std::mutex> g(m.etat);
      for (int i = 0; i < 200 && m.fondEnCours; ++i) m.fin.wait_for(g, std::chrono::milliseconds(25));
    }
  }
  e->enCours = false;
  m.sessionActive = false;
  {
    std::lock_guard<std::mutex> g(m.sessionVerrou);
    m.session.reset();
  }
  if (m.commande) {
    std::lock_guard<std::mutex> unique(m.verrou);
    m.commande(const_cast<char*>("destroy all"));
  }
}

void SimulationContinue::ajouter(std::shared_ptr<ParticipantContinu> p) {
  if (!p) return;
  std::lock_guard<std::mutex> g(m_etat->verrou);
  m_etat->participants.push_back(std::move(p));
}
void SimulationContinue::retirerParticipants() {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  m_etat->participants.clear();
}

void SimulationContinue::setPause(bool oui) {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  if (!oui && m_etat->pause) {
    m_etat->mur0 = std::chrono::steady_clock::now();
    m_etat->sim0 = m_etat->temps;
  }
  m_etat->pause = oui;
  m_etat->reveil.notify_all();
}
bool SimulationContinue::enPause() const { return m_etat->pause; }
void SimulationContinue::setVitesse(double v) {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  m_etat->mur0 = std::chrono::steady_clock::now();
  m_etat->sim0 = m_etat->temps;
  m_etat->vitesse = std::max(0.0, v);
}
double SimulationContinue::vitesse() const {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  return m_etat->vitesse;
}
bool SimulationContinue::enCours() const { return m_etat->enCours; }
double SimulationContinue::temps() const {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  return m_etat->temps;
}
std::uint64_t SimulationContinue::pas() const {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  return m_etat->pas;
}
void SimulationContinue::regler(const std::string& source, double valeur) {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  const std::string n = nomNormalise(source);
  m_etat->generateurs.erase(n);
  m_etat->fixes[n] = valeur;
}
void SimulationContinue::regler(const std::string& source, const Generateur& gen) {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  const std::string n = nomNormalise(source);
  m_etat->fixes.erase(n);
  if (auto it = m_etat->generateurs.find(n); it != m_etat->generateurs.end()) it->second.regler(gen, m_etat->temps);
  else m_etat->generateurs.emplace(n, SourceGeneree(gen));
}
double SimulationContinue::valeur(const std::string& sonde) const {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  const auto it = m_etat->indices.find(nomNormalise(sonde));
  return it == m_etat->indices.end() ? std::numeric_limits<double>::quiet_NaN() : m_etat->dernieres[it->second];
}
Releve SimulationContinue::echantillons(const std::string& sonde, double depuis) const {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  Releve r;
  const auto it = m_etat->indices.find(nomNormalise(sonde));
  if (it == m_etat->indices.end()) return r;
  const auto& t = m_etat->tampons[it->second];
  auto debut = std::lower_bound(t.begin(), t.end(), depuis, [](const Echantillon& e, double x) { return e.t <= x; });
  r.assign(debut, t.end());
  return r;
}
std::vector<std::string> SimulationContinue::console() const {
  Moteur& m = moteur();
  std::lock_guard<std::mutex> g(m.etat);
  return m.console;
}
std::string SimulationContinue::erreur() const {
  std::lock_guard<std::mutex> g(m_etat->verrou);
  return m_etat->erreur;
}

}  // namespace prom::ecad
