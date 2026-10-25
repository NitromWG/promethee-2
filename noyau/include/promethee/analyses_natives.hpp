// Prométhée : analyses natives de ngspice et analyse symbolique (lot L14 de la 0.34 ; 14.7 ; SIM-05, SIM-23).
// Bruit (densités de sortie et d'entrée, totaux), fonction de transfert petits signaux (gain, impédances), pôles et
// zéros, Fourier du dernier cycle avec le taux de distorsion harmonique, FFT d'une courbe, enchaînement d'analyses
// en lot. L'analyse symbolique écrit les équations de l'analyse nodale modifiée sur les résistances, condensateurs,
// inductances et sources du schéma, chaque valeur gardée en lettre (« littérale ») ou en nombre (« semi-littérale »),
// et rend H(s) = N(s)/D(s) : polynômes en s dont les coefficients sont des monômes des grandeurs (exposants négatifs
// permis : R⁻¹), ramenés à un dénominateur de terme constant 1, avec ses pôles et ses zéros (valeurs posées) et la
// formule en LaTeX. Le calcul est écrit dans Prométhée (aucune bibliothèque de calcul formel). Licence GPL-3.0-only.
#pragma once

#include <complex>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"

namespace prom::ecad {

// ---------- Analyses natives ----------
struct AnalyseNative {
  enum class Genre : std::uint8_t { Bruit, FonctionTransfert, PolesZeros, Fourier };
  Genre genre = Genre::FonctionTransfert;
  std::string sortie, reference = "GND";  // réseaux de la sortie
  std::string entree;                     // repère de la source d'entrée (« V1 ») ; pôles et zéros : son réseau
  double fDebut = 1, fFin = 1e6;
  int pointsParDecade = 10;
  double fondamentale = 1000;            // Fourier : Hz
  int harmoniques = 9, periodes = 10;    // Fourier : périodes simulées, la dernière analysée
  bool polesSeulement = false;
};
struct ResultatNatif {
  bool ok = false;
  std::string erreur;
  // Fonction de transfert
  double gain = 0, impedanceEntree = 0, impedanceSortie = 0;
  // Pôles et zéros (rad/s)
  std::vector<std::complex<double>> poles, zeros;
  // Bruit : densités (V/√Hz) et totaux (V efficaces)
  std::vector<double> frequences, bruitSortie, bruitEntree;
  double bruitTotalSortie = 0, bruitTotalEntree = 0;
  // Fourier : amplitudes et phases des harmoniques (la première est la fondamentale), taux de distorsion (%)
  std::vector<double> amplitudes, phases;
  double distorsion = 0;
  std::vector<std::string> console;
};
ResultatNatif analyseNative(const ConceptionElec& c, const AnalyseNative& a);
// Taux de distorsion harmonique d'une courbe sur ses dernières périodes (%), harmoniques par transformée discrète
double tauxDistorsion(const Releve& s, double fondamentale, int harmoniques = 9, std::vector<double>* amplitudes = nullptr, int periodes = 1);
// FFT : ré-échantillonnée régulièrement (puissance de 2), fenêtre de Hann au choix ; amplitudes crête
struct Spectre {
  std::vector<double> frequences, amplitudes;
};
Spectre fft(const Releve& s, bool hann = true);
// Plusieurs analyses de suite sur la même conception (lot), chacune son résultat
std::vector<ResultatSimulation> enchainer(const ConceptionElec& c, const std::vector<AnalyseSpice>& analyses, const OptionsSimulation& o = {});

// ---------- Analyse symbolique ----------
// Monôme : coefficient et exposants des grandeurs (même ordre que FonctionSymbolique::symboles)
struct Monome {
  double coefficient = 0;
  std::vector<int> exposants;
};
// Polynôme en s : pour chaque puissance de s (de 0 au degré), une somme de monômes
using PolynomeS = std::map<int, std::vector<Monome>>;
struct FonctionSymbolique {
  std::vector<std::string> symboles;  // « R1 », « C1 »…
  PolynomeS numerateur, denominateur;
  std::string texte() const;  // « 1 / (1 + s·R1·C1) »
  std::string latex() const;  // « \frac{1}{1 + s R_{1} C_{1}} »
  // Coefficients numériques en s (valeurs des grandeurs posées), du degré 0 au plus haut
  std::vector<double> coefficients(bool numerateur, const std::map<std::string, double>& valeurs) const;
  std::vector<std::complex<double>> poles(const std::map<std::string, double>& valeurs) const;
  std::vector<std::complex<double>> zeros(const std::map<std::string, double>& valeurs) const;
  std::complex<double> evaluer(std::complex<double> s, const std::map<std::string, double>& valeurs) const;
};
// H(s) = V(sortie) / V(source d'entrée) ; « numeriques » : repères des composants gardés en nombres (semi-littérale)
std::optional<FonctionSymbolique> transfertSymbolique(const ConceptionElec& c, const std::string& entree, const std::string& sortie, std::string* erreur = nullptr,
                                                      const std::set<std::string>& numeriques = {});
// Valeurs des grandeurs du schéma (R1 → 1000…), pour poser la fonction
std::map<std::string, double> valeursSymboles(const ConceptionElec& c);
// Racines d'un polynôme à coefficients réels (degré 0 d'abord), par la méthode de Durand-Kerner affinée par Newton
std::vector<std::complex<double>> racines(const std::vector<double>& coefficients);

}  // namespace prom::ecad
