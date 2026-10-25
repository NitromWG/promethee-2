// Prométhée : analyseurs de réseau et de spectre, paramètres S (lot L14 de la 0.34 ; 14.10 ; SIM-19, SIM-37, SIM-48,
// SIM-49). L'analyseur posé entre deux bornes du montage lance sa propre analyse : chaque port est attaqué à son tour
// par une source de résistance interne Z0, l'autre chargé par Z0 (analyses alternatives de ngspice), d'où S11, S21,
// S12, S22, puis Z, Y et H. Un composant décrit par un fichier Touchstone (.s1p, .s2p ; MA, DB ou RI ; Hz à GHz) est
// lu, écrit, et s'ouvre seul. Tracés : abaque de Smith, polaire, Nyquist, Black (gain en dB selon la phase), temps de
// groupe, histogramme, diagramme de l'œil. Licence GPL-3.0-only.
#pragma once

#include <array>
#include <complex>
#include <optional>
#include <string>
#include <vector>

#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"

namespace prom::ecad {

using Complexe = std::complex<double>;
struct ParametresS {
  int ports = 2;
  double z0 = 50;
  std::vector<double> frequences;
  // Par fréquence : S11, S12, S21, S22 (un port : S11 seul)
  std::vector<std::array<Complexe, 4>> s;
};
struct PortsAnalyseur {
  std::string port1, port2;   // réseaux attaqués
  std::string masse = "GND";
  double z0 = 50;
};
std::optional<ParametresS> parametresS(const ConceptionElec& c, const PortsAnalyseur& p, double fDebut, double fFin, int pointsParDecade, std::string* erreur = nullptr);
// Conversions (matrices 2 × 2) ; dB et phase en degrés
std::array<Complexe, 4> versZ(const std::array<Complexe, 4>& s, double z0);
std::array<Complexe, 4> versY(const std::array<Complexe, 4>& s, double z0);
std::array<Complexe, 4> versH(const std::array<Complexe, 4>& s, double z0);
double enDecibels(Complexe x);
double phaseDegres(Complexe x);
// Touchstone v1 : « # HZ S DB R 50 » (format MA, DB ou RI)
std::string ecrireTouchstone(const ParametresS& p, const std::string& format = "DB", const std::string& unite = "HZ");
std::optional<ParametresS> lireTouchstone(const std::string& texte, int ports, std::string* erreur = nullptr);
// Tracés
std::vector<std::pair<double, double>> pointsSmith(const std::vector<Complexe>& gamma);  // plan du coefficient de réflexion
std::vector<std::pair<double, double>> pointsBlack(const std::vector<Complexe>& h);      // (phase °, gain dB)
std::vector<double> tempsDeGroupe(const std::vector<double>& f, const std::vector<Complexe>& h);  // s
std::vector<double> phaseDeroulee(const std::vector<Complexe>& h);                             // degrés, sans saut de 360°
struct Histogramme {
  double min = 0, max = 0;
  std::vector<int> classes;
};
Histogramme histogramme(const std::vector<double>& v, int classes = 50);
struct DiagrammeOeil {
  std::vector<Releve> traces;  // une trace par fenêtre de deux bits, temps de 0 à 2 T
  double ouvertureVerticale = 0, ouvertureHorizontale = 0;  // V ; fraction de bit
};
DiagrammeOeil diagrammeOeil(const Releve& s, double periodeBit, double decalage = 0, double seuil = std::numeric_limits<double>::quiet_NaN());

}  // namespace prom::ecad
