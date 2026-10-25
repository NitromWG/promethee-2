// Prométhée : alimentations (lot L14 de la 0.34 ; 14.14 ; SIM-59 à SIM-61). L'arbre des rails est relevé sur le
// schéma comme l'arbre de distribution de l'atelier électrique : sources, régulateurs linéaires et à découpage,
// charges, avec courant, rendement et dissipation par étage, refait à chaque changement ; sa puissance totale se
// reporte au bilan de l'armoire. L'assistant d'abaisseur (buck en mode tension) choisit l'inductance, les
// condensateurs et la diode, estime le rendement, et compense la boucle par un correcteur de type III (méthode du
// facteur K), vérifié par une analyse alternative de ngspice sur le modèle moyen. L'assistant des magnétiques calcule
// spires, induction, pertes fer (Steinmetz) et pertes cuivre (Dowell), et dessine un transformateur planar en Gerber.
// Licence GPL-3.0-only.
#pragma once

#include <complex>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"
#include "promethee/simulation.hpp"

namespace prom::ecad {

// ---------- Arbre des rails ----------
struct EtageRail {
  std::string composant, reference, genre;  // « source », « linéaire », « découpage »
  std::string entree, sortie;               // réseaux
  double vEntree = 0, vSortie = 0, iEntree = 0, iSortie = 0, rendement = 1, dissipation = 0;  // V, A, W
  double charges = 0;                       // A pris par les charges directes du rail de sortie
  std::vector<int> enfants;                 // étages alimentés par sa sortie
};
struct ArbreRails {
  std::vector<EtageRail> etages;
  std::vector<int> racines;
  double puissanceEntree = 0, dissipationTotale = 0;  // W
  std::string texte() const;                          // l'arbre en lignes indentées
};
// « charges » : courant pris sur chaque rail par ses charges (réseau → A) ; le rendement d'un régulateur à découpage
// est son champ Sim.Rendement (0,9 sinon), son courant de repos Sim.Iq
ArbreRails arbreRails(const ConceptionElec& c, const std::map<std::string, double>& charges);

// ---------- Abaisseur ----------
struct DemandeAbaisseur {
  double vEntree = 12, vSortie = 5, courant = 2;
  double frequence = 500e3, ondulationCourant = 0.3, ondulationTension = 0.01;  // ΔI/I, ΔV/V
  double vReference = 0.8, vRampe = 1;  // référence du contrôleur, amplitude de la rampe de la MLI
  double margeVoulue = 60;              // degrés
};
struct ConceptionAbaisseur {
  bool ok = false;
  double rapport = 0, inductance = 0, capacite = 0, esr = 0, dcr = 0, iCrete = 0, iEfficace = 0;
  double rCharge = 0, f0 = 0, frequenceCoupure = 0;
  // Correcteur de type III (ampli inverseur) et diviseur de mesure
  double r1 = 10e3, r2 = 0, r3 = 0, c1 = 0, c2 = 0, c3 = 0, rBas = 0;
  double margePrevue = 0;  // degrés, d'après les équations
  double rendement = 0, pertesConduction = 0, pertesCommutation = 0, pertesDiode = 0, pertesInductance = 0;
  std::vector<std::pair<std::string, std::string>> nomenclature;  // repère, valeur et description
  std::vector<std::string> notes;
};
ConceptionAbaisseur concevoirAbaisseur(const DemandeAbaisseur& d);
// Netlist alternative du modèle moyen, boucle ouverte à l'entrée du correcteur : L(jω) = −v(sortie)/v(test)
std::string netlistBoucle(const ConceptionAbaisseur& c, const DemandeAbaisseur& d);
struct MargesBoucle {
  bool ok = false;
  double frequenceCoupure = 0, margePhase = 0, margeGain = 0;  // Hz, degrés, dB
  std::string erreur;
};
MargesBoucle marges(const std::vector<double>& f, const std::vector<std::complex<double>>& boucle);
MargesBoucle simulerBoucle(const ConceptionAbaisseur& c, const DemandeAbaisseur& d);
// Pose de l'abaisseur sur la feuille (contrôleur générique, inductance, diode, condensateurs, diviseur, correcteur)
std::vector<std::string> poserAbaisseur(ConceptionElec& c, const ConceptionAbaisseur& a, const DemandeAbaisseur& d, std::size_t feuille = 0, Pt origine = ptMm(40, 40));

// ---------- Magnétiques ----------
struct Noyau {
  std::string nom, materiau;
  double ae = 0, le = 0, ve = 0, al = 0, bsat = 0.35;  // m², m, m³, H/spire², T
  double k = 0, alpha = 1.5, beta = 2.5;               // Steinmetz : P (W/m³) = k f^α B^β (f en Hz, B crête en T)
  double fenetre = 0, longueurSpire = 0;               // m², m
};
const std::vector<Noyau>& noyaux();
double pertesSteinmetz(const Noyau& n, double frequence, double bCrete);  // W
double profondeurPeau(double frequence, double resistivite = 1.72e-8);   // m
double facteurDowell(double epaisseur, double profondeur, int couches);  // Rac / Rdc
struct Bobinage {
  std::string noyau;
  int spires = 0, couches = 1;
  double bCrete = 0, pertesFer = 0, pertesCuivre = 0, rdc = 0, rac = 0, facteur = 1;
  bool sature = false;
};
// « ondulation » : courant crête à crête alternatif (induction alternative pour les pertes fer)
Bobinage calculerBobine(double inductance, double iCrete, double iEfficace, double ondulation, double frequence, const Noyau& n, double diametreFil);
// Transformateur planar : spirales du primaire (cuivre du dessus) et du secondaire (cuivre du dessous), en Gerber
// RS-274X par couche ; rapport de bobinage tiré du nombre de tours
std::map<std::string, std::string> transformateurPlanar(int primaire, int secondaire, double diametreExterieur, double largeur, double ecart);

}  // namespace prom::ecad
