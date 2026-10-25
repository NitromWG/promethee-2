// Prométhée : la simulation dans le projet (lot L11 de la 0.34 ; 11.8, 11.9, 11.13, 11.14, 11.15 ; SIM-01, SIM-02,
// SIM-12 à SIM-16, SIM-33 à SIM-35, SIM-38 à SIM-40).
// Une partie du schéma se simule seule, chaque réseau coupé bouclé sur une source, une charge ou rien. Le tableau de
// préparation contrôle masse, modèles, nœuds flottants, boucles de sources idéales et sources, chaque ligne menant au
// composant ; des modèles se déduisent du repère. Un échec de ngspice est analysé et dit en français, et le calcul
// repart avec le remède choisi, noté dans les réglages. Les courbes calculées sont des expressions évaluées par
// Prométhée sur les vecteurs ; les mesures automatiques et la comparaison à une référence dans une bande se refont à
// chaque calcul. Le classeur (onglets d'analyse, paramètres du projet, résultats figés) est gardé dans la conception,
// sous la clé « simulation » de ses extensions, avec son propre numéro de format. Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <complex>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/simulation.hpp"

namespace prom::ecad {

// ---------- Partie du schéma (11.8) ----------
std::set<std::string> composantsDeFeuille(const ConceptionElec& c, std::size_t feuille);  // UUID des composants posés
// Réseaux qui relient la partie au reste, chacun avec un bouclage proposé : un réseau d'alimentation nommé (« +5V »)
// prend sa tension, les autres rien
std::vector<LiaisonCoupee> liaisonsDeLaPartie(const ConceptionElec& c, const std::set<std::string>& composants);
// Paramètres dans l'ordre de leurs dépendances ; vide et « erreur » si l'un se définit par lui-même
std::vector<ParametreGlobal> ordonnerParametres(const std::vector<ParametreGlobal>& p, std::string* erreur = nullptr);

// ---------- Préparation (11.9) ----------
struct LignePreparation {
  std::string genre;   // « masse », « modèle », « flottant », « boucle », « source », « partie »
  std::string niveau;  // « erreur », « avertissement », « information »
  std::string message;
  std::string composant;  // UUID du composant en cause, s'il y en a un
  std::string reseau;
  std::vector<std::string> remedes;
};
std::vector<LignePreparation> preparerSimulation(const ConceptionElec& c, const AnalyseSpice& a, const OptionsSimulation& o = {});
// Modèle idéal déduit du repère (R, C, L ; D : diode 1N4148) posé en champ Sim.Modele sur les composants choisis qui
// n'en ont pas ; rend le nombre posé, et dans « aSaisir » les repères dont la valeur manque. Retirer fait l'inverse.
int deduireModeles(ConceptionElec& c, const std::vector<std::string>& uuids, std::vector<std::string>* aSaisir = nullptr);
int retirerModeles(ConceptionElec& c, const std::vector<std::string>& uuids);

// ---------- Échecs et remèdes (11.9) ----------
struct DiagnosticEchec {
  std::string genre;  // « pas », « repos », « singulier », « modèle », « syntaxe », « autre »
  std::string cause;  // en français, avec le réseau et le composant en cause
  std::string noeud, reseau, composant;  // nœud SPICE, réseau du schéma, repère
  double instant = -1;                   // s, pour un transitoire arrêté en route
  std::vector<std::string> remedes;      // dans l'ordre où les essayer
};
DiagnosticEchec analyserEchec(const ResultatSimulation& r, const NetlistSpice& n);
// « gminsteps », « srcsteps » (montée progressive des sources), « rshunt », « gear », « pas », « reltol », « uic »
std::string libelleRemede(const std::string& remede);
// Le remède entre dans les réglages ; faux s'il y était déjà
bool appliquerRemede(OptionsSimulation& o, const std::string& remede, const AnalyseSpice& a);
struct EssaiRemedes {
  ResultatSimulation resultat;
  NetlistSpice netlist;
  DiagnosticEchec diagnostic;        // celui du premier échec
  std::vector<std::string> essayes;  // remèdes essayés, dans l'ordre
  std::string retenu;                // celui qui a fait aboutir le calcul ; vide si aucun
};
// Calcul ; en cas d'échec, chaque remède proposé est essayé seul, dans l'ordre, jusqu'au premier succès, qui entre
// dans « o » avec sa note (« remedes »)
EssaiRemedes simulerAvecRemedes(const ConceptionElec& c, const AnalyseSpice& a, OptionsSimulation& o, const std::atomic<bool>* arret = nullptr);

// ---------- Courbes calculées (11.13) ----------
// Expression sur les vecteurs : V(réseau), V(a,b), I(V1), I(R1) (courants enregistrés), nombres, + − * / ^,
// fonctions db, mag, ph (degrés), re, im, abs, sqrt, log10, ln, exp, deriv, integ, moy(x ; durée), max, min
// (arguments séparés par « ; »), et les vecteurs du résultat par leur nom (« frequency », « time »)
struct CourbeCalculee {
  std::vector<double> x;
  std::vector<std::complex<double>> y;
  bool complexe = false;
  std::string erreur;
  bool valide() const { return erreur.empty() && !y.empty(); }
  std::vector<double> reels() const;   // parties réelles
  std::vector<double> modules() const;
};
CourbeCalculee evaluerCourbe(const std::string& expression, const ResultatSimulation& r, const NetlistSpice& n);

// ---------- Mesures automatiques (11.15) ----------
enum class GenreMesure : std::uint8_t { Coupure, Bande, Q, Periode, Frequence, Montee, Descente, Depassement, Maximum, Minimum, Moyenne, Efficace, ValeurA };
struct MesureAuto {
  GenreMesure genre = GenreMesure::Coupure;
  std::string courbe;     // expression ou réseau
  double parametre = 0;   // ValeurA : l'abscisse
  friend bool operator==(const MesureAuto&, const MesureAuto&) = default;
};
struct ResultatMesure {
  std::string nom, unite;
  double valeur = 0;
  bool valide = false;
};
ResultatMesure mesurer(const MesureAuto& m, const ResultatSimulation& r, const NetlistSpice& n);
std::string nomMesure(GenreMesure g);
std::optional<GenreMesure> mesureDepuisNom(const std::string& nom);

// ---------- Comparaison à une référence (11.15, SIM-40) ----------
enum class GenreBande : std::uint8_t { Relative, Absolue, Enveloppe };
struct Comparaison {
  bool reussie = false;
  double ecartMax = 0;   // relatif (Relative) ou absolu
  double xPire = 0;      // abscisse du plus grand écart
  std::vector<std::pair<std::size_t, std::size_t>> horsBande;  // tronçons d'indices de la courbe hors de la bande
  std::string verdict() const;  // « réussi » ou « échoué »
};
// La référence (xr, yr) est interpolée aux abscisses de la courbe, sur leur partie commune ; l'enveloppe prend yr pour
// borne basse et « haut » pour borne haute
Comparaison comparerCourbes(const std::vector<double>& x, const std::vector<double>& y, const std::vector<double>& xr, const std::vector<double>& yr, double bande,
                            GenreBande g = GenreBande::Relative, const std::vector<double>* haut = nullptr);

// ---------- Classeur (11.14, SIM-38, SIM-39) ----------
struct OngletClasseur {
  std::string nom;
  AnalyseSpice analyse;
  OptionsSimulation options;
  std::vector<std::string> sondes;       // réseaux tracés
  std::vector<std::string> expressions;  // courbes calculées
  std::vector<MesureAuto> mesures;
  std::string reference, courbeReference;  // résultat figé de référence, et courbe comparée
  double bande = 0.01;
  GenreBande genreBande = GenreBande::Relative;
  std::string verdict;                    // dernier verdict de la comparaison
  friend bool operator==(const OngletClasseur&, const OngletClasseur&) = default;
};
struct ResultatFige {
  std::string nom, onglet, date, echelle;
  std::map<std::string, std::vector<double>> reels;
  std::map<std::string, std::vector<std::complex<double>>> complexes;
  std::map<std::string, std::string> noeuds;  // réseau → nœud SPICE, pour retrouver les sondes
  friend bool operator==(const ResultatFige&, const ResultatFige&) = default;
};
struct ClasseurSimulation {
  std::vector<ParametreGlobal> parametres;
  std::vector<OngletClasseur> onglets;
  std::vector<ResultatFige> figes;
  bool vide() const { return parametres.empty() && onglets.empty() && figes.empty(); }
  const ResultatFige* fige(const std::string& nom) const;
  friend bool operator==(const ClasseurSimulation&, const ClasseurSimulation&) = default;
};
inline constexpr int FORMAT_CLASSEUR = 1;
nlohmann::ordered_json classeurVersJson(const ClasseurSimulation& k);
// Lecture tolérante et migrée ; un format plus récent est lu au mieux, signalé dans « alertes »
ClasseurSimulation classeurDepuisJson(const nlohmann::ordered_json& j, std::vector<std::string>* alertes = nullptr);
ClasseurSimulation classeurDe(const ConceptionElec& c, std::vector<std::string>* alertes = nullptr);  // extension « simulation »
void ecrireClasseur(ConceptionElec& c, const ClasseurSimulation& k);
ResultatFige figer(const ResultatSimulation& r, const NetlistSpice& n, const std::string& nom, const std::string& onglet, const std::string& date = {});
ResultatSimulation resultatDe(const ResultatFige& f);
NetlistSpice noeudsDe(const ResultatFige& f);  // les nœuds seuls, pour évaluer une courbe sur un résultat figé
// CSV : l'échelle puis une colonne par vecteur (complexes en module et phase), au réglage CSV du poste ; et lecture
// d'un CSV (séparateur et marque décimale reconnus) en résultat figé
std::string csvResultat(const ResultatSimulation& r, const std::vector<std::string>& vecteurs = {});
std::optional<ResultatFige> resultatDepuisCsv(const std::string& texte, const std::string& nom, std::string* erreur = nullptr);

}  // namespace prom::ecad
