// Prométhée : simulation continue et instruments (lot L14 de la 0.34 ; 14.2 à 14.5 ; SIM-20, SIM-42 à SIM-44,
// SIM-50 à SIM-54).
//
// La simulation continue fait avancer ngspice partagé au rythme du temps réel (vitesse réglable) : un transitoire long
// tourne sur le fil de ngspice, chaque pas accepté est relevé (sondes gardées dans des tampons circulaires) puis
// retenu jusqu'à l'heure voulue ; les sources « external » de la netlist (boutons, interrupteurs, générateurs) sont
// lues à chaque pas, réglées par l'interface pendant le calcul. Le retour visuel tire le courant de chaque segment de
// fil des courants des broches (loi des nœuds sur l'arbre du réseau) et compare les grandeurs aux limites des
// articles (LED, résistance, électrolytique, pile). Les instruments lisent les échantillons : oscilloscope déclenché,
// multimètre, wattmètre, fréquencemètre, compteur ; un générateur est une source externe dont la phase reste continue
// quand on change sa fréquence. Un instrument écrit en Python lit des blocs d'échantillons par l'API de scripts et
// rend ses valeurs ; une façade est un SVG dont les éléments nommés (bouton:, calibre:, menu:, affichage:) se lient
// aux réglages de l'instrument. Licence GPL-3.0-only.
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "promethee/simulation.hpp"

namespace prom::ecad {

// ---------- Générateurs (14.4) ----------
struct Generateur {
  enum class Forme : std::uint8_t { Continu, Sinus, Carre, Triangle, DentDeScie, Impulsions, Arbitraire, Mots };
  Forme forme = Forme::Sinus;
  double frequence = 1000, amplitude = 1, decalage = 0, rapport = 0.5;  // Hz, V crête, V, rapport cyclique
  enum class Modulation : std::uint8_t { Aucune, Amplitude, Frequence };
  Modulation modulation = Modulation::Aucune;
  double frequenceModulante = 10, profondeur = 0.5, excursion = 100;  // AM : indice ; FM : excursion (Hz)
  std::vector<std::pair<double, double>> trace;  // arbitraire : (fraction de période 0..1, valeur −1..1), dessiné
  std::vector<std::uint8_t> mots;               // générateur de mots : une valeur de 8 bits par période
  int bit = 0;                                    // bit rendu par cette sortie (0 à 7)
  double niveauHaut = 5;                          // mots : tension d'un 1
  friend bool operator==(const Generateur&, const Generateur&) = default;
};
const char* nomForme(Generateur::Forme f);  // « sinus »…
// Valeur d'une forme à la phase donnée (en tours, la partie entière ignorée), sans décalage ni modulation
double valeurForme(const Generateur& g, double phase, std::size_t periode = 0);
// Générateur réglé pendant le calcul : chaque réglage ouvre un tronçon qui part de la phase atteinte, si bien que la
// phase reste continue ; un temps antérieur au dernier réglage (pas refusé par ngspice) relit le tronçon d'avant
class SourceGeneree {
 public:
  explicit SourceGeneree(Generateur g = {});
  void regler(const Generateur& g, double t);
  double valeur(double t) const;
  double phase(double t) const;  // tours
  const Generateur& generateur() const { return m_troncons.back().g; }

 private:
  struct Troncon {
    double debut = 0, phase0 = 0;
    Generateur g;
  };
  std::vector<Troncon> m_troncons;
};

// ---------- Échantillons et mesures ----------
struct Echantillon {
  double t = 0, v = 0;
};
using Releve = std::vector<Echantillon>;
// Valeur interpolée au temps t (bornée aux extrémités)
double valeurA(const Releve& s, double t);
struct MesureSignal {
  double moyenne = 0, efficace = 0, efficaceAlternatif = 0, min = 0, max = 0, creteCrete = 0, duree = 0;
  bool valide = false;
};
// Moyenne et valeur efficace intégrées dans le temps (pas variables) entre « debut » et « fin »
MesureSignal mesurerSignal(const Releve& s, double debut = -1e300, double fin = 1e300);
// Fréquence : fronts montants à travers le seuil (milieu de l'excursion si NaN), avec hystérésis, instants
// interpolés ; nombre de fronts dans « fronts »
double frequenceSignal(const Releve& s, double seuil = std::numeric_limits<double>::quiet_NaN(), int* fronts = nullptr, double debut = -1e300);
int compterFronts(const Releve& s, double seuil, bool montants = true, double debut = -1e300);
double puissanceMoyenne(const Releve& tension, const Releve& courant, double debut = -1e300, double fin = 1e300);

// ---------- Oscilloscope (14.4) ----------
enum class CouplageVoie : std::uint8_t { Continu, Alternatif, Masse };
struct VoieOscillo {
  std::string sonde;  // vecteur (« v(n_sortie) »)
  double voltsParDivision = 1, decalage = 0;
  CouplageVoie couplage = CouplageVoie::Continu;
  bool active = true;
};
struct ReglagesOscillo {
  std::vector<VoieOscillo> voies = std::vector<VoieOscillo>(2);
  double secondesParDivision = 1e-3;
  int divisions = 10;
  enum class Mode : std::uint8_t { Auto, Normal, Unique } mode = Mode::Auto;
  int voieDeclenchement = 0;
  bool frontMontant = true;
  double niveau = 0, preDeclenchement = 0.1;  // V ; part de l'écran avant le déclenchement
  bool xy = false;                            // voie 2 en fonction de la voie 1
};
struct TraceOscillo {
  std::vector<Releve> voies;  // temps rapporté au déclenchement
  double instant = 0;         // temps simulé du déclenchement
  bool declenche = false;
};
// Dernier balayage complet : déclenché sur le front voulu le plus récent qui laisse la fenêtre entière, sinon (Auto)
// la fin des échantillons
TraceOscillo traceOscilloscope(const std::vector<Releve>& voies, const ReglagesOscillo& r);

// ---------- Session continue (14.2) ----------
struct OptionsContinue {
  double pas = 1e-4;          // pas maximal (s)
  double duree = 600;         // temps simulé au plus (s)
  double vitesse = 1;         // secondes simulées par seconde réelle ; 0 : au plus vite
  std::size_t tampon = 200'000;
};
// Netlist d'une simulation continue : boutons et sources externes commandés, transitoire long au pas voulu, seules
// les sondes gardées (sinon toutes les tensions)
NetlistSpice netlistContinue(const ConceptionElec& c, const OptionsContinue& oc, OptionsSimulation o = {}, const std::vector<std::string>& sondes = {});
// Participant de la session (15.4, 15.7) : microcontrôleur émulé, composant décrit en HDL. À chaque pas accepté, sur
// le fil de ngspice, il reçoit la dernière valeur de ses sondes et avance jusqu'à ce temps ; il tient les sources
// externes qu'il reconnaît (noms en minuscules, « vu1_19d »). Un participant arrêté (point d'arrêt) retient le fil :
// le circuit s'arrête avec lui. « interrompre » : la session s'arrête, il ne doit plus attendre.
class ParticipantContinu {
 public:
  virtual ~ParticipantContinu() = default;
  virtual std::vector<std::string> sondes() const = 0;
  virtual void pas(double t, const std::map<std::string, double>& valeurs) = 0;
  virtual bool source(const std::string& nom, double t, double& valeur) = 0;
  virtual void interrompre() {}
};
class SimulationContinue {
 public:
  SimulationContinue();
  ~SimulationContinue();
  SimulationContinue(const SimulationContinue&) = delete;
  SimulationContinue& operator=(const SimulationContinue&) = delete;
  // La netlist (transitoire compris), les sondes relevées à chaque pas ; une seule session à la fois, et aucun autre
  // calcul pendant qu'elle tourne. Rend faux et l'erreur si ngspice manque ou refuse le circuit.
  bool demarrer(const std::string& netlist, const std::vector<std::string>& sondes, const OptionsContinue& o = {}, std::string* erreur = nullptr);
  // Participants pris au prochain démarrage (leurs sondes s'ajoutent à celles de la session)
  void ajouter(std::shared_ptr<ParticipantContinu> p);
  void retirerParticipants();
  void arreter();
  void setPause(bool oui);
  bool enPause() const;
  void setVitesse(double v);
  double vitesse() const;
  bool enCours() const;
  double temps() const;        // temps simulé atteint (s)
  std::uint64_t pas() const;   // pas acceptés
  // Sources externes de la netlist : valeur fixe, ou générateur réglé à partir du temps atteint
  void regler(const std::string& source, double valeur);
  void regler(const std::string& source, const Generateur& g);
  double valeur(const std::string& sonde) const;  // dernière valeur (NaN inconnue)
  Releve echantillons(const std::string& sonde, double depuis = -1) const;
  std::vector<std::string> console() const;
  std::string erreur() const;
  struct Etat;

 private:
  std::shared_ptr<Etat> m_etat;
};

// ---------- Sept segments (14.2) ----------
// Segments allumés (bits a à g de 0 à 6) d'un chiffre de 0 à 9 ; 0 au-delà
std::uint8_t segmentsDuChiffre(int chiffre);
// Chiffre montré par des segments (−1 s'ils ne forment pas un chiffre)
int chiffreDesSegments(std::uint8_t segments);
// Valeur binaire lue sur des tensions (bit 0 d'abord), au seuil donné
int valeurBinaire(const std::vector<double>& tensions, double seuil = 2.5);

// ---------- Retour visuel (14.3) ----------
struct CourantFil {
  std::string fil;     // UUID du fil
  double courant = 0;  // A, valeur absolue
  int sens = 0;        // +1 : de a vers b ; −1 : de b vers a ; 0 : nul ou inconnu
  double tension = 0;  // V du réseau
};
struct Fumee {
  std::string composant, reference, grandeur;  // « courant », « puissance », « tension inverse », « court-circuit »
  double valeur = 0, limite = 0;
  std::string message;  // « D1 : 3,41 A pour 20 mA au plus »
};
struct RetourVisuel {
  std::vector<CourantFil> fils;
  std::vector<Fumee> fumees;
  std::map<std::string, double> tensions;   // réseau → V
  std::map<std::string, double> courants;   // composant (UUID) → A, de sa broche 1 vers sa broche 2
};
// Vecteurs à garder (.save) pour le retour visuel : courants des composants à deux bornes
std::vector<std::string> sondesRetourVisuel(const ConceptionElec& c, const NetlistSpice& n);
// Couleur d'une tension (bleu pour le négatif, gris pour 0, rouge pour le maximum)
std::array<int, 3> couleurTension(double v, double vmax);
RetourVisuel retourVisuel(const ConceptionElec& c, const NetlistSpice& n, const std::map<std::string, double>& valeurs);
RetourVisuel retourVisuel(const ConceptionElec& c, const NetlistSpice& n, const ResultatSimulation& r, std::size_t indice = static_cast<std::size_t>(-1));

// ---------- Façades et instruments écrits en Python (14.5) ----------
// Élément nommé d'une façade SVG : « bouton:marche », « calibre:volts », « menu:couplage », « affichage:mesure »
struct ElementFacadeInstrument {
  std::string genre, nom, id;
  double x = 0, y = 0, largeur = 0, hauteur = 0;
  std::vector<std::string> choix;  // calibre et menu : leurs valeurs (attribut data-choix « 1|2|5 »)
};
struct FacadeInstrument {
  std::string nom, svg;
  double largeur = 0, hauteur = 0;
  std::vector<ElementFacadeInstrument> elements;
  const ElementFacadeInstrument* element(const std::string& genre, const std::string& nom) const;
};
std::optional<FacadeInstrument> lireFacade(const std::string& svg, std::string* erreur = nullptr);
// Façades génériques de salle de TP : « oscilloscope », « multimetre », « generateur »
const FacadeInstrument& facadeGenerique(const std::string& instrument);
// Réglages d'un instrument liés aux éléments de sa façade : un clic sur un bouton bascule, un calibre ou un menu
// passe au choix suivant
struct ReglagesInstrument {
  std::map<std::string, std::string> valeurs;
  bool cliquer(const FacadeInstrument& f, double x, double y);  // vrai si un élément a été touché
};
// Instrument Python : script qui lit des blocs d'échantillons (promethee.echantillons(sonde)) et rend ses mesures
// (promethee.afficher(nom, valeur)) ; il tourne dans un processus à part, sans réseau ni fichiers
struct ResultatInstrument {
  bool reussi = false;
  std::string erreurs;
  std::map<std::string, double> valeurs;     // dernière valeur rendue par nom
  std::vector<std::pair<std::string, double>> journal;  // toutes, dans l'ordre
  std::string facade;                         // façade déclarée par le script (SVG), vide sinon
};
ResultatInstrument executerInstrument(const std::string& code, const std::map<std::string, Releve>& sondes, std::size_t bloc = 2048, double delai = 120);
// Fréquencemètre fourni (même méthode que celui de l'exemple Python : fronts montants à mi-excursion)
double frequencemetre(const Releve& s);
// Instruments posés au schéma (symbole « Instrument ») : genre (sa valeur), script Python (champ Instrument.Script),
// réseaux de ses deux entrées
struct InstrumentPose {
  std::string composant, reference, genre, script, plus, moins;
};
std::vector<InstrumentPose> instrumentsDuSchema(const ConceptionElec& c);

}  // namespace prom::ecad
