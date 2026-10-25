// Prométhée : simulation nourrie par le projet (lot L14 de la 0.34 ; 14.11 ; SIM-24, SIM-11). Les pistes routées
// d'un réseau à deux broches entrent dans la netlist en cellule en pi (résistance du cuivre d'après sa largeur et son
// épaisseur, inductance et capacité de la ligne microruban d'après l'empilage), les autres réseaux reçoivent leur
// capacité : les courbes avant et après routage se superposent. Un moteur à courant continu reçoit la charge de la
// pièce qu'il entraîne : inertie tirée des propriétés de masse de la mécanique (moment autour de l'axe), couple
// résistant saisi. Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"

namespace prom::ecad {

// ---------- Pistes routées ----------
struct ParasitesReseau {
  std::string reseau;
  int pistes = 0, broches = 0;
  double longueur = 0;    // m
  double resistance = 0;  // Ω
  double inductance = 0;  // H
  double capacite = 0;    // F
};
std::vector<ParasitesReseau> parasitesPistes(const ConceptionElec& c);
// Netlist où chaque réseau routé porte ses parasites (cellule en pi entre ses deux broches ; capacité à la masse
// pour les réseaux de plus de deux broches)
NetlistSpice netlistAvecPistes(const ConceptionElec& c, const AnalyseSpice& a, OptionsSimulation o = {}, std::vector<ParasitesReseau>* parasites = nullptr);
// Permittivité effective et impédance d'une ligne microruban (Hammerstad et Jensen), largeur et hauteur en mm
double permittiviteEffective(double largeur, double hauteur, double er);

// ---------- Moteur et charge mécanique ----------
struct ParametresMoteur {
  double r = 2, l = 1e-3;        // Ω, H
  double ke = 0.01, kt = 0.01;   // V·s/rad, N·m/A
  double j = 1e-5, b = 1e-6;     // kg·m², N·m·s/rad
  double couple = 0;             // N·m résistant
};
ParametresMoteur parametresMoteur(const ComposantElec& k);
void reglerMoteur(ComposantElec& k, const ParametresMoteur& p);
// Charge de la pièce entraînée : inertie du moteur plus celle de la pièce (kg·m²), couple résistant (N·m)
void appliquerCharge(ComposantElec& moteur, double inertiePiece, double coupleResistant, double inertieRotor = 0);
// Inertie d'une pièce (kg·m²) depuis son moment autour de l'axe en g·mm² (propriétés de masse de la mécanique)
inline double inertieDepuisMasse(double gmm2) { return gmm2 * 1e-9; }
double constanteTempsMecanique(const ParametresMoteur& p);  // J·R / (Ke·Kt)
std::string vecteurVitesse(const std::string& reference);  // « v(w_m1) » : vitesse en rad/s
// Instant où la courbe atteint 63,2 % de sa valeur finale, depuis « depart »
double constanteDeTemps(const Releve& r, double finale, double depart = 0);

}  // namespace prom::ecad
