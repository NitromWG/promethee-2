// Prométhée : calculateurs de l'électronique (lot L11 de la 0.34 ; 11.7, 11.17, 11.18, 11.19 ; SIM-07, SIM-09, SIM-64,
// SIM-66 à SIM-74). Même cadre que la boîte à calculs de l'électricien (electrique_calculateurs.hpp) : fiche à champs
// bornés, résultat avec formule, explication et avis sourcés, valeurs normalisées les plus proches proposées par le
// moteur des séries E. Quatre familles : « Courants » (largeur de piste, diviseur, LED, régulateur réglable, filtre
// RC, valeur normalisée), « Carte » (vias, courant de fusion, diaphonie, longueur critique, impédance cible du réseau
// d'alimentation, isolement), « Établi » (atténuateurs, bobines, résonance, adaptation en pi, petits calculs, code des
// couleurs dans les deux sens, forets et filetages, aide-mémoire IPC-6011 et IPC-6012, potentiels électrochimiques)
// et « Énergie et chaleur » (fils et nappes, autonomie par modes, chaîne thermique et radiateur). L'inspecteur du
// circuit en tire la longueur, le délai, la résistance et le courant admissible d'une piste ou d'une via.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include "promethee/electrique_calculateurs.hpp"
#include "promethee/electronique.hpp"

namespace prom::ecad {

const std::vector<prom::elec::Calculateur>& calculateursElectroniques();
const prom::elec::Calculateur* calculateurElectronique(const std::string& id);
const std::vector<std::string>& famillesCalculateursElectroniques();  // « Courants », « Carte », « Établi », « Énergie et chaleur »

// Couleurs des anneaux d'une résistance (CEI 60062) : valeur et tolérance → couleurs ; et l'inverse. « anneaux » : 4 ou 5
std::vector<std::string> anneauxDe(double ohms, double tolerance, int anneaux = 4);
// Rend la valeur (Ω) et la tolérance (relative) ; faux si une couleur n'est pas permise à sa place
bool valeurDesAnneaux(const std::vector<std::string>& couleurs, double& ohms, double& tolerance);

// Atténuateur résistif : affaiblissement (dB) et impédance (Ω) → résistances
struct Attenuateur {
  double serie = 0, parallele = 0;  // pi : parallèle aux deux bouts, série au milieu ; T : séries aux bouts, parallèle au milieu
};
Attenuateur attenuateurPi(double db, double z);
Attenuateur attenuateurT(double db, double z);

// Autonomie par modes : courant moyen pondéré, part de chaque mode, heures (capacité en mAh)
struct ModeConsommation {
  double courant = 0, duree = 0;  // A, s
};
struct Autonomie {
  double courantMoyen = 0;           // A
  std::vector<double> parts;         // part de la charge de chaque mode
  double heures = 0;                 // hors autodécharge et rendement
  double heuresReelles = 0;          // avec l'autodécharge (par mois) et le rendement du régulateur
};
Autonomie autonomie(const std::vector<ModeConsommation>& modes, double capaciteMah, double autodechargeMois = 0, double rendement = 1);

// Piste et via choisies (11.17) : longueur, délai, résistance, courant admissible pour l'échauffement donné
struct ProprietesCuivre {
  double longueur = 0, largeur = 0, epaisseur = 0;  // mm
  double resistance = 0;                           // Ω à 20 °C
  double delai = 0;                                // s
  double courantAdmissible = 0;                    // A pour « echauffement » (IPC-2221)
  double inductance = 0, capacite = 0;             // via : H, F
  bool externe = true;
  std::string resume() const;  // « 12,7 mm · 81 ps · 6,2 mΩ · 1,1 A (ΔT 10 °C) »
};
ProprietesCuivre proprietesPiste(const ConceptionElec& c, const Piste& p, double echauffement = 10);
ProprietesCuivre proprietesVia(const ConceptionElec& c, const Via& v, double echauffement = 10);

}  // namespace prom::ecad
