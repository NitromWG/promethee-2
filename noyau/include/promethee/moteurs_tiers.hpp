// Prométhée : autres moteurs de simulation et mesures réelles (lot L14 de la 0.34 ; 14.8 ; SIM-21, SIM-41).
// Xyce et QucsatorRF tournent comme programmes du poste, en processus à part, sur la netlist traduite dans leur
// dialecte ; LTspice se pilote en ligne de commande s'il est installé (fichier .raw relu) ; PSpice et HSPICE sont
// reconnus mais leurs formats de résultats ne sont pas relus. Les résultats rentrent dans les mêmes courbes que ceux
// de ngspice. Les mesures d'un oscilloscope (CSV de Rigol, Keysight, Tektronix ou générique) et les captures de
// sigrok (fichier .sr) se superposent à la simulation ou servent de source (PWL) ; un appareil reconnu par sigrok ne
// se capture en direct que si l'utilisateur l'active. Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "promethee/analyseurs_numeriques.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"

namespace prom::ecad {

// ---------- Moteurs du poste ----------
struct MoteurTiers {
  std::string nom, programme;  // « Xyce », « Xyce » ; « QucsatorRF », « qucsator »…
  bool present = false;
  bool resultatsRelus = false;  // vrai pour Xyce, Qucsator, LTspice
};
std::vector<MoteurTiers> moteursTiers();
// Traductions : netlist de ngspice → dialecte du moteur, avec les sorties demandées (« v(n_sortie) »)
std::string netlistXyce(const NetlistSpice& n, const AnalyseSpice& a, const std::vector<std::string>& sorties, const std::string& fichierSortie = "sortie.csv");
std::string netlistQucsator(const NetlistSpice& n, const AnalyseSpice& a, std::vector<std::string>* refus = nullptr);
// Lecture des résultats : CSV de Xyce, données de Qucsator (.dat), fichier brut de LTspice ou de ngspice (.raw)
ResultatSimulation lireCsvXyce(const std::string& texte);
ResultatSimulation lireDatQucs(const std::string& texte);
ResultatSimulation lireRaw(const std::string& octets);
// Simulation par un autre moteur (programme du poste) ; « erreur » si absent ou en échec
ResultatSimulation simulerAvec(const std::string& moteur, const NetlistSpice& n, const AnalyseSpice& a, const std::vector<std::string>& sorties, double delai = 120);

// ---------- Mesures réelles ----------
struct CourbeMesuree {
  std::string nom, unite;
  Releve points;
};
// CSV d'oscilloscope : Rigol (X, Start, Increment), Keysight (x-axis), Tektronix (colonnes 4 et 5), générique
// (temps puis voies) ; séparateurs virgule, point-virgule ou tabulation
std::optional<std::vector<CourbeMesuree>> lireCsvOscilloscope(const std::string& texte, std::string* erreur = nullptr);
// Capture de sigrok (.sr) : voies logiques nommées
struct CaptureSigrok {
  double frequence = 0;  // Hz
  std::vector<std::pair<std::string, VoieLogique>> voies;
};
std::optional<CaptureSigrok> lireSigrok(const std::string& octets, std::string* erreur = nullptr);
// Source PWL tirée d'une mesure (une courbe prise comme source du schéma)
std::string pwlDepuisMesure(const Releve& r, std::size_t pointsMax = 2000);
// Écart entre deux courbes sur une fenêtre : maximal et moyen, rapportés à l'amplitude de la première
struct EcartCourbes {
  double maximal = 0, moyen = 0;
  bool valide = false;
};
EcartCourbes comparerCourbes(const Releve& a, const Releve& b, double debut = -1e300, double fin = 1e300);
// Capture en direct par sigrok-cli (appareil branché) : refusée tant que « active » est faux
struct CaptureDirecte {
  bool active = false;
  std::string pilote = "demo", voies;
  long echantillons = 1000;
};
std::optional<CaptureSigrok> capturerSigrok(const CaptureDirecte& c, std::string* erreur = nullptr);

}  // namespace prom::ecad
