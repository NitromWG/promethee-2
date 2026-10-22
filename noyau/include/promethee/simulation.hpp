// Prométhée : simulation du schéma par ngspice (lot L11 ; SIM-01 à SIM-04, SIM-06, SIM-07, SIM-09, SIM-12 à SIM-16,
// SIM-28, SIM-33 à SIM-35, SIM-63, SIM-65).
//
// La netlist SPICE se tire du schéma : chaque composant de la bibliothèque a son élément (R, C, L, diodes, transistors,
// 555, amplificateur opérationnel, régulateur…), un champ « Sim.Modele » remplace l'élément tiré, « Sim.Exclure »
// l'écarte ; les sources se posent avec les symboles de la catégorie Simulation, et un réseau d'alimentation nommé
// (« +5V », « +3V3 », « VCC ») sans source reçoit la sienne. Avant le calcul, la netlist est contrôlée : masse
// présente, modèles connus, nœuds qui ont un chemin vers la masse.
//
// ngspice est chargé comme bibliothèque partagée au moment du calcul (libngspice, licence BSD modifiée) : sans elle,
// la simulation est grisée, le reste de Prométhée n'en dépend pas. Le calcul tourne sur son fil et s'arrête à la
// demande. Les résultats sont des vecteurs nommés (« v(sortie) », « frequency », « time »), réels ou complexes.
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <complex>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Analyses ----------
struct AnalyseSpice {
  enum class Type : std::uint8_t { PointDeRepos, Continu, Alternatif, Transitoire } type = Type::Transitoire;
  // Transitoire : pas et durée (s), début de l'enregistrement
  double pas = 1e-5, duree = 1e-2, debut = 0;
  // Continu : source balayée, bornes et pas (V ou A)
  std::string source;
  double dcDebut = 0, dcFin = 5, dcPas = 0.1;
  // Alternatif : points par décade, bornes (Hz)
  int pointsParDecade = 20;
  double fDebut = 1, fFin = 1e6;
  std::string carte() const;  // « .tran 10u 10m 0 » …
};

// ---------- Netlist ----------
struct NetlistSpice {
  std::string texte;                           // netlist complète, analyse comprise
  std::map<std::string, std::string> noeuds;   // nom du réseau → nœud SPICE (« 0 » pour la masse)
  std::vector<std::string> erreurs;            // empêchent le calcul (masse absente, nœud flottant…)
  std::vector<std::string> avertissements;     // composant écarté, source implicite…
  bool valide() const { return erreurs.empty(); }
};
NetlistSpice netlistSpice(const ConceptionElec& c, const AnalyseSpice& a);
// Nœud SPICE d'un réseau (« v(n_sortie) ») ; vide s'il n'est pas dans la netlist
std::string vecteurTension(const NetlistSpice& n, const std::string& reseau);
// Modèles fournis (555, LED, 1N4148, BC547, amplificateur opérationnel, régulateur) : texte SPICE
const std::map<std::string, std::string>& modelesFournis();

// ---------- Résultats ----------
struct ResultatSimulation {
  std::string echelle;  // « time », « frequency », « v-sweep »… (vide au point de repos)
  std::map<std::string, std::vector<double>> reels;
  std::map<std::string, std::vector<std::complex<double>>> complexes;
  std::vector<std::string> console;  // sortie de ngspice
  std::string erreur;
  bool interrompu = false;
  bool reussi() const { return erreur.empty() && !interrompu; }
  const std::vector<double>* reel(const std::string& nom) const;            // nom sans égard à la casse
  const std::vector<std::complex<double>>* complexe(const std::string& nom) const;
};

// ---------- Moteur ----------
bool ngspiceDisponible();               // la bibliothèque se charge
std::string versionNgspice();           // « ngspice-42 » ; vide si absente
// Calcul sur le fil appelant (le moteur n'en lance qu'un à la fois) ; « arret » interrompt ; « delai » en secondes
ResultatSimulation simuler(const std::string& netlist, const std::atomic<bool>* arret = nullptr, double delai = 120);

// ---------- Mesures (11.4, 11.13, 11.15) ----------
// Fréquence d'un signal périodique : moyenne des périodes entre fronts montants au milieu de sa plage, sur la
// seconde moitié de l'enregistrement (régime établi) ; 0 sans deux fronts
double frequenceMesuree(const std::vector<double>& t, const std::vector<double>& v);
// Fréquence où le gain (complexe, sortie/entrée) passe 3 dB sous sa valeur en basse fréquence ; 0 sans passage
double frequenceCoupure(const std::vector<double>& f, const std::vector<std::complex<double>>& h);
double valeurA(const std::vector<double>& x, const std::vector<double>& y, double x0);  // interpolation linéaire
std::vector<double> gainDb(const std::vector<std::complex<double>>& h);

// ---------- Boîte à calculs (11.16) ----------
// Deux valeurs de la série En, en série ou en parallèle, au plus près d'une cible (recherche « rencontre au milieu »)
struct CombinaisonE {
  double a = 0, b = 0;
  bool parallele = false;
  double valeur = 0, ecart = 0;  // ecart relatif (0,001 = 0,1 %)
};
CombinaisonE combinaisonE(double cible, int serie = 24);

// ---------- Sources décrites en français (11.10) ----------
// « 0 V pendant 1 ms, puis rampe à 5 V en 10 µs » → « PWL(0 0 1m 0 1.001m 5) » ; vide et « erreur » si non compris
std::string sourcePwl(const std::string& description, std::string* erreur = nullptr);

}  // namespace prom::ecad
