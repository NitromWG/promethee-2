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
#include <set>
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
  friend bool operator==(const AnalyseSpice&, const AnalyseSpice&) = default;
};

// ---------- Réglages d'un calcul (11.8, 11.9) ----------
// Paramètre global du projet (« Rcharge » = « 47 », « gain » = « 1 + R2/R1 ») : écrit en .param dans l'ordre de ses
// dépendances, repris entre accolades par les valeurs ({Rcharge}) et les balayages
struct ParametreGlobal {
  std::string nom, expression, unite;
  friend bool operator==(const ParametreGlobal&, const ParametreGlobal&) = default;
};
// Partie du schéma simulée seule : chaque réseau coupé (qui relie la partie au reste) reçoit rien, une source
// (tension continue « valeur » et amplitude alternative « ac ») ou une charge vers la masse (« valeur » en ohms)
enum class BouclageLiaison : std::uint8_t { Rien, Source, Charge };
struct LiaisonCoupee {
  std::string reseau;
  BouclageLiaison bouclage = BouclageLiaison::Rien;
  double valeur = 0, ac = 0;
  friend bool operator==(const LiaisonCoupee&, const LiaisonCoupee&) = default;
};
struct OptionsSimulation {
  std::string compatibilite;                          // « » ngspice, « ps », « lt », « hs », « ltps » (ngbehavior)
  std::map<std::string, double> conditionsInitiales;  // réseau → V (.ic)
  std::map<std::string, double> pointDepart;          // réseau → V (.nodeset)
  bool sansPointRepos = false;                        // transitoire sans point de repos (uic)
  std::map<std::string, std::string> moteur;          // options du moteur : reltol, abstol, vntol, gmin, method…
  double pasMax = 0;                                  // transitoire : pas maximal (s) ; 0 : celui de ngspice
  bool rampeSources = false;                          // sources continues montées en rampe au début du transitoire
  std::vector<std::string> remedes;                   // remèdes retenus après un échec, notés
  std::set<std::string> composants;                   // partie simulée seule (UUID) ; vide : tout le schéma
  std::vector<LiaisonCoupee> liaisons;                // bouclage des réseaux coupés de la partie
  bool autoriserControle = false;                     // bloc .control des directives permis (jamais « shell »)
  std::vector<ParametreGlobal> parametres;            // paramètres du projet, repris de son classeur
  friend bool operator==(const OptionsSimulation&, const OptionsSimulation&) = default;
};

// ---------- Netlist ----------
struct NetlistSpice {
  std::string texte;                           // netlist complète, analyse comprise
  std::map<std::string, std::string> noeuds;   // nom du réseau → nœud SPICE (« 0 » pour la masse)
  std::vector<std::string> erreurs;            // empêchent le calcul (masse absente, nœud flottant…)
  std::vector<std::string> avertissements;     // composant écarté, source implicite…
  std::string compatibilite;                   // mode de compatibilité de ngspice retenu (options, modèles)
  std::map<std::string, std::string> instances;  // élément SPICE en minuscules (« rr1 », « xu1 ») → repère
  std::vector<std::string> coupees;            // réseaux coupés d'une partie simulée seule
  std::vector<std::string> chargements;        // bibliothèques OSDI à charger avant le circuit (Verilog-A)
  bool valide() const { return erreurs.empty(); }
};
NetlistSpice netlistSpice(const ConceptionElec& c, const AnalyseSpice& a);
// Avec les réglages : partie seule, directives des feuilles, paramètres, modèles des articles, conditions initiales,
// point de départ, options du moteur, rampe des sources, compatibilité
NetlistSpice netlistSpice(const ConceptionElec& c, const AnalyseSpice& a, const OptionsSimulation& o);
// Nœud SPICE d'un réseau (« v(n_sortie) ») ; vide s'il n'est pas dans la netlist
std::string vecteurTension(const NetlistSpice& n, const std::string& reseau);
// Modèles fournis (555, LED, 1N4148, BC547, amplificateur opérationnel, régulateur) : texte SPICE
const std::map<std::string, std::string>& modelesFournis();
// Modèle d'un autre simulateur ramené à ce que lit ngspice (11.5) : IF(c, a, b) en (c ? a : b), LIMIT(x, a, b) en
// min(max(x, a), b), TC=a,b en tc1/tc2, interrupteurs VSWITCH et ISWITCH de PSpice en sw et csw ; ce qui ne passe
// pas (modèle chiffré, primitives A de LTspice, .STIMULUS…) est refusé avec sa ligne
struct AdaptationModele {
  std::string texte;
  std::vector<std::string> notes;  // changements faits, avec leur ligne
  std::vector<std::string> refus;  // constructions que ngspice ne lit pas, avec leur ligne
  bool lisible() const { return refus.empty(); }
};
AdaptationModele adapterModele(const std::string& texte, const std::string& compatibilite = {});
// Nom du premier sous-circuit (ou à défaut du premier modèle) d'un texte, et nombre de nœuds de ce sous-circuit
std::string nomModele(const std::string& texte);
int noeudsSousCircuit(const std::string& texte, const std::string& nom);

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
// Compatibilité (« ps », « lt »… : ngbehavior réglé avant la lecture du circuit) et modèles compilés à charger
// (bibliothèques OSDI de Verilog-A)
struct OptionsMoteur {
  std::string compatibilite;
  std::vector<std::string> chargements;
};
ResultatSimulation simuler(const std::string& netlist, const OptionsMoteur& o, const std::atomic<bool>* arret = nullptr, double delai = 120);
ResultatSimulation simuler(const NetlistSpice& n, const std::atomic<bool>* arret = nullptr, double delai = 120);
// Console de ngspice (11.8) : une commande sur le circuit et les résultats du dernier calcul ; rend les lignes
// écrites par ngspice. Seules les commandes de lecture et de calcul passent (print, let, meas, display, show,
// listing, setplot, echo, version, op, tran, ac, dc, run, alter, altermod) ; « shell », « source », « write »… sont
// refusées, la raison dans « refus »
std::vector<std::string> commandeConsole(const std::string& commande, std::string* refus = nullptr);

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
