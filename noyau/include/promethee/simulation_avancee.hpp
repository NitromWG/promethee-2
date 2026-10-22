// Prométhée : simulation par passes et assistants de calcul (lot L14 ; 14.9, 14.12, 14.13 ; SIM-17 à SIM-24,
// SIM-41 à SIM-46). Monte-Carlo (valeurs tirées dans leurs tolérances), pire cas (sensibilités puis les deux coins),
// balayage d'une valeur ; chaque passe modifie la netlist, jamais le schéma, et rend une mesure. Les assistants
// calculent un montage (LED, diviseur, astable à 555, filtres RC et Sallen-Key de Butterworth), ramènent ses valeurs
// à une série E, disent ce que donnent les valeurs normalisées et posent le montage dans le schéma, prêt à simuler.
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "promethee/simulation.hpp"

namespace prom::ecad {

// ---------- Passes (14.9) ----------
using MesureSimulation = std::function<double(const ResultatSimulation&, const NetlistSpice&)>;
MesureSimulation mesureCoupure(const std::string& reseau);    // analyse alternative : fréquence à −3 dB
MesureSimulation mesureFrequence(const std::string& reseau);  // transitoire : fréquence du signal
MesureSimulation mesureTension(const std::string& reseau);    // point de repos : tension du réseau

struct Dispersion {
  std::string reference;
  char element = 'R';      // R, C ou L
  double nominale = 0;     // unité de base
  double tolerance = 0.05; // relative
};
// R, C et L de la netlist, avec leur tolérance : paramètre « tolerance » du composant, sinon celle de sa valeur
// (« 4k7 1 % »), sinon celle de « tolerances » (repère → relative), sinon 5 % (R) ou 10 % (C, L)
std::vector<Dispersion> dispersions(const ConceptionElec& c, const NetlistSpice& n, const std::map<std::string, double>& tolerances = {});
// Netlist dont les valeurs des repères donnés sont remplacées (repère → valeur en unité de base)
std::string netlistAvecValeurs(const std::string& netlist, const std::map<std::string, double>& valeurs);

struct ResultatPasses {
  double nominale = 0, min = 0, max = 0, moyenne = 0, ecartType = 0;
  std::vector<double> mesures;
  std::vector<std::map<std::string, double>> tirages;  // valeurs de chaque passe
  std::map<std::string, double> sensibilites;          // pire cas : variation relative de la mesure pour +1 % du composant
  int echecs = 0;
  bool interrompu = false;
  std::string erreur;
};
ResultatPasses monteCarlo(const ConceptionElec& c, const AnalyseSpice& a, const MesureSimulation& m, int passes = 50, unsigned graine = 1,
                          const std::map<std::string, double>& tolerances = {}, const std::atomic<bool>* arret = nullptr);
ResultatPasses pireCas(const ConceptionElec& c, const AnalyseSpice& a, const MesureSimulation& m, const std::map<std::string, double>& tolerances = {},
                       const std::atomic<bool>* arret = nullptr);
// Balayage : la mesure pour chaque valeur du composant (unité de base)
std::vector<std::pair<double, double>> balayer(const ConceptionElec& c, const AnalyseSpice& a, const std::string& reference, const std::vector<double>& valeurs,
                                               const MesureSimulation& m, const std::atomic<bool>* arret = nullptr);

// ---------- Assistants (14.12, 14.13) ----------
struct ElementPropose {
  std::string reference, symbole, valeur;  // « R1 », « R », « 4k7 » (série E)
  double exacte = 0;                       // valeur calculée avant normalisation
  std::vector<std::pair<std::string, std::string>> liaisons;  // numéro de broche → réseau
};
struct Proposition {
  std::string titre;
  std::vector<ElementPropose> elements;
  std::map<std::string, double> resultats;  // ce que donnent les valeurs normalisées (« fc », « f », « I »…)
  std::vector<std::string> notes;
};
Proposition assistantLed(double vAlim, double vf, double courant, int serie = 24);
Proposition assistantDiviseur(double vin, double vout, double rTotal = 10e3, int serie = 24);
Proposition assistantAstable555(double frequence, double rapportCyclique, double c = 10e-9, int serie = 24);
Proposition assistantPasseBasRC(double fc, double c = 100e-9, int serie = 24);
// Sallen-Key de Butterworth d'ordre 2 à gain unité, passe-bas ou passe-haut ; entrée ENTREE, sortie SORTIE,
// alimentations VCC et VEE
Proposition assistantSallenKey(double fc, bool passeHaut = false, double c = 10e-9, int serie = 24);
// Pose la proposition sur une feuille, en ligne à partir de « origine », chaque broche reliée par une étiquette
// globale à son réseau ; les repères déjà pris sont décalés au suivant libre. Rend les repères posés.
std::vector<std::string> poserProposition(ConceptionElec& c, const Proposition& p, std::size_t feuille, Pt origine);

}  // namespace prom::ecad
