// Prométhée : microcontrôleur simulé (lot L15 de la 0.34 ; 15.4, 15.5, 15.8 ; EMB-08 à EMB-12, EMB-15, EMB-16,
// SIM-47).
//
// Co-simulation : le programme compilé (ELF) tourne sur simavr, l'émulateur libre de l'AVR (celui qu'emploie
// SimulIDE), lié à Prométhée quand il est installé ; il avance pas à pas avec ngspice, en participant de la
// simulation continue : chaque broche reliée du microcontrôleur est une source externe de 25 Ω validée par son
// registre de direction, avec son tirage de 35 kΩ ; les entrées sont lues aux seuils de l'AVR (0,3 et 0,6 VCC), les
// voies analogiques en millivolts. Le terminal série est relié à l'USART simulée : débit, parité et bits d'arrêt lus
// dans ses registres et comparés aux réglages du terminal, messages préparés.
//
// Mise au point : Prométhée pilote le GDB du poste en GDB/MI pour placer les points d'arrêt sur les lignes et
// trouver les variables, et lit la table des lignes de l'ELF (avr-objdump) ; l'émulateur s'arrête sur le point
// d'arrêt, le circuit avec lui ; pas à pas en C (ligne) ou en assembleur (instruction), registres et variables lus
// dans l'émulateur ; arrêt sur une condition du circuit ou au changement d'une variable surveillée ; traces en texte
// des broches et de la liaison série.
//
// Programme éprouvé sans matériel : robot à deux roues sur une piste dessinée en image (1 pixel par millimètre),
// capteurs de ligne qui lisent les pixels sous eux, capteurs de distance par lancer de rayons ; simulation sans écran
// en ligne de commande, verdict d'après le texte attendu sur la liaison série ; deux versions du programme comparées
// signal par signal avec une tolérance sur les fronts. Licence GPL-3.0-only.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "promethee/electronique.hpp"
#include "promethee/processus.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"

namespace prom::ecad {

// ---------- Émulateur ----------
enum class EtatBroche : std::uint8_t { Bas, Haut, Entree, Tirage };  // sortie basse, haute ; entrée ; entrée tirée
struct OctetSerie {
  double t = 0;
  std::uint8_t octet = 0;
};
struct FormatSerie {
  int debit = 0;          // bauds (0 : fermée)
  int bits = 8;
  char parite = 'N';      // « N », « E », « O »
  int arret = 1;
  bool emission = false, reception = false;
};
class EmulateurMicro {
 public:
  virtual ~EmulateurMicro() = default;
  virtual std::string nom() const = 0;  // « simavr (atmega328p) »
  virtual bool charger(const std::string& elf, std::string* erreur = nullptr) = 0;
  // Exécute jusqu'au temps simulé « jusqua » (s) ; s'arrête avant une instruction marquée d'un point d'arrêt
  // (« arreteSurPoint »), sauf la première après une reprise
  virtual void avancer(double jusqua) = 0;
  virtual void pas() = 0;  // une instruction
  virtual double temps() const = 0;
  virtual double frequence() const = 0;
  virtual std::uint64_t cycles() const = 0;
  virtual EtatBroche broche(const std::string& port) const = 0;  // « PB5 »
  virtual void entree(const std::string& port, bool niveau) = 0;
  virtual void analogique(int voie, double volts) = 0;  // ADC0 à ADC7
  virtual void alimentation(double volts) = 0;           // VCC, AVCC et AREF
  virtual void envoyerSerie(std::uint8_t octet) = 0;     // vers l'USART0
  virtual FormatSerie formatSerie() const = 0;           // lu dans UBRR0 et UCSR0A à UCSR0C
  virtual double rapportCyclique(const std::string& port) const = 0;  // sortie MLI d'un temporisateur, sinon 0 ou 1
  std::function<void(std::uint8_t octet, double t)> surSerie;         // octet émis par l'USART0
  // Mise au point
  virtual std::uint32_t pc() const = 0;  // octets
  virtual std::vector<std::uint8_t> registres() const = 0;  // r0 à r31
  virtual std::uint8_t sreg() const = 0;
  virtual std::uint16_t sp() const = 0;
  virtual std::uint8_t memoire(std::uint32_t adresse) const = 0;  // espace des données (SRAM à partir de 0x100)
  virtual void ecrireMemoire(std::uint32_t adresse, std::uint8_t v) = 0;
  virtual bool termine() const = 0;  // programme arrêté (sommeil sans interruption) ou planté
  std::set<std::uint32_t> pointsArret;
  bool arreteSurPoint = false;
  bool reprise = false;  // la prochaine instruction passe son point d'arrêt
};
bool emulateurDisponible(const std::string& micro = "atmega328p");
std::unique_ptr<EmulateurMicro> creerEmulateur(const std::string& micro, double frequence, std::string* erreur = nullptr);
std::string microDuSymbole(const std::string& symbole);  // « ATmega328P » → « atmega328p » ; vide si inconnu

// ---------- Table des lignes et symboles de l'ELF ----------
struct LigneSource {
  std::string fichier;
  int ligne = 0;
  std::uint32_t adresse = 0;
};
struct TableLignes {
  std::vector<LigneSource> lignes;  // triées par adresse
  // Première adresse d'une ligne (ou de la suivante du même fichier qui porte du code)
  std::optional<std::uint32_t> adresse(const std::string& fichier, int ligne) const;
  std::optional<LigneSource> ligneDe(std::uint32_t adresse) const;  // ligne qui contient l'adresse
};
TableLignes tableLignes(const std::string& elf, std::string* erreur = nullptr);  // avr-objdump --dwarf=decodedline
TableLignes lireTableLignes(const std::string& sortieObjdump);
struct SymboleElf {
  std::string nom;
  std::uint32_t adresse = 0, taille = 0;
  char genre = 0;  // « T » code, « D », « B » données
};
std::vector<SymboleElf> symbolesElf(const std::string& elf);  // avr-nm -S

// ---------- GDB/MI ----------
// Enregistrement GDB/MI (« ^done,bkpt={number="1",addr="0x000000a6"} ») : classe (« done », « error »…) et valeurs
bool lireEnregistrementMi(const std::string& ligne, std::string& classe, nlohmann::ordered_json& valeurs);
class PiloteGdb {
 public:
  bool demarrer(const std::string& elf, std::string* erreur = nullptr, const std::string& gdb = "avr-gdb");
  void arreter();
  bool enCours() const;
  struct Reponse {
    bool ok = false;
    std::string classe, brut;
    nlohmann::ordered_json valeurs;
    std::vector<std::string> console;
  };
  Reponse commande(const std::string& mi, double delai = 15);
  std::optional<std::uint32_t> adresseLigne(const std::string& fichier, int ligne);
  std::optional<std::uint32_t> adresseVariable(const std::string& nom);  // espace des données (0x800100 → 0x100)
  std::string typeVariable(const std::string& nom);
  const std::vector<std::string>& journal() const { return m_journal; }

 private:
  ProcessusInteractif m_p;
  std::vector<std::string> m_journal;
  int m_jeton = 0;
};

// ---------- Mise au point ----------
struct VariableSurveillee {
  std::string nom;
  std::uint32_t adresse = 0;
  int taille = 1;
  std::int64_t valeur = 0;
};
class DebogueurMicro {
 public:
  explicit DebogueurMicro(EmulateurMicro& e) : m_e(e) {}
  void setTable(TableLignes t) { m_table = std::move(t); }
  const TableLignes& table() const { return m_table; }
  // Point d'arrêt sur une ligne (ou la suivante qui porte du code) ; rend l'adresse
  std::optional<std::uint32_t> ajouterPoint(const std::string& fichier, int ligne);
  void ajouterAdresse(std::uint32_t adresse) { m_e.pointsArret.insert(adresse); }
  void retirerPoints() { m_e.pointsArret.clear(); }
  void surveiller(const std::string& nom, std::uint32_t adresse, int taille = 1);
  std::vector<VariableSurveillee> variables() const;
  bool variableChangee();  // depuis le dernier appel
  // Pas : jusqu'à la ligne suivante (instructions sans ligne comprises) ou une instruction
  void pasLigne(int maxInstructions = 1'000'000);
  void pasInstruction() { m_e.pas(); }
  std::optional<LigneSource> ligneCourante() const { return m_table.ligneDe(m_e.pc()); }
  std::int64_t lireVariable(std::uint32_t adresse, int taille) const;

 private:
  EmulateurMicro& m_e;
  TableLignes m_table;
  std::vector<VariableSurveillee> m_surveillees;
};

// ---------- Co-simulation avec le circuit ----------
struct BrocheCosimulee {
  std::string numero, port, reseau, vecteur;  // « 19 », « PB5 », « LED », « v(n_led) »
};
struct ReglagesTerminal {
  int debit = 9600, bits = 8, arret = 1;
  char parite = 'N';
  bool controleFlux = false;
  std::vector<std::string> messages;  // préparés, envoyés d'un clic
};
struct TraceMicro {
  double t = 0;
  std::string texte;  // « PB5 → 1 », « série 0x62 'b' »
};
class CosimulationMicro : public ParticipantContinu {
 public:
  // « n » : netlist continue de la conception, le composant « reference » co-simulé (options.cosimulation)
  CosimulationMicro(const ConceptionElec& c, const std::string& reference, const NetlistSpice& n, std::unique_ptr<EmulateurMicro> e);
  ~CosimulationMicro() override;
  std::vector<std::string> sondes() const override;
  void pas(double t, const std::map<std::string, double>& valeurs) override;
  bool source(const std::string& nom, double t, double& valeur) override;
  void interrompre() override;
  const std::vector<BrocheCosimulee>& broches() const { return m_broches; }
  // Terminal
  ReglagesTerminal terminal;
  std::vector<OctetSerie> recus() const;
  std::string texteRecu() const;          // décodé aux réglages du terminal (un débit faux rend des caractères faux)
  bool reglagesCompatibles() const;        // terminal et USART d'accord (débit à 3 % près, format)
  void envoyer(const std::string& texte);  // octets vers le microcontrôleur
  // Mise au point : arrêté sur un point d'arrêt ou une condition, le circuit attend
  DebogueurMicro& debogueur() { return *m_debogueur; }
  EmulateurMicro& emulateur() { return *m_e; }
  bool arrete() const;
  bool attendreArret(double delai);  // vrai si arrêté dans le délai (secondes réelles)
  void continuer();
  void pasLigne();        // une ligne, puis un pas du circuit, puis arrêt
  void pasInstruction();  // une instruction, puis un pas du circuit, puis arrêt
  std::string raisonArret() const;
  // Condition du circuit (sur les valeurs des sondes de ce pas) : l'émulateur et le circuit s'arrêtent quand elle
  // devient vraie ; arrêt aussi au changement d'une variable surveillée
  std::function<bool(const std::map<std::string, double>&)> conditionCircuit;
  bool arretSurVariable = false;
  std::vector<TraceMicro> traces() const;
  double vcc() const;
  EtatBroche etat(const std::string& port) const;

 private:
  enum class Mode : std::uint8_t { Libre, Arret, PasLigne, PasInstruction, ApresPas };
  void noterSorties(double t);
  std::unique_ptr<EmulateurMicro> m_e;
  std::unique_ptr<DebogueurMicro> m_debogueur;
  std::vector<BrocheCosimulee> m_broches;
  std::string m_reference, m_vecteurVcc;
  mutable std::mutex m_verrou;
  std::condition_variable m_cv;
  Mode m_mode = Mode::Libre;
  bool m_interrompu = false;
  std::string m_raison;
  std::map<std::string, EtatBroche> m_etats;  // port → dernier état
  std::map<std::string, bool> m_entrees;      // dernier niveau lu
  double m_vcc = 5;
  std::vector<OctetSerie> m_recus;
  std::vector<TraceMicro> m_traces;
  bool m_conditionAvant = false;
};

// ---------- Robot sur une piste (15.8) ----------
struct PisteRobot {
  int largeur = 0, hauteur = 0;     // pixels (1 mm)
  std::vector<std::uint8_t> gris;   // 0 noir à 255 blanc ; obstacles : valeurs de 1 à 15
  std::uint8_t pixel(double x, double y) const;  // hors de l'image : blanc
};
// Ovale de « longueur » mm de ligne (deux droites, deux demi-cercles de « rayon »), ligne noire de « epaisseur » mm,
// barre de départ perpendiculaire sur la droite du bas ; rend aussi le point et le cap de départ
struct PisteDepart {
  PisteRobot piste;
  double x = 0, y = 0, cap = 0;  // mm, radians (0 : vers +x)
};
PisteDepart pisteOvale(double longueur = 2000, double epaisseur = 19, double rayon = 200);
std::optional<PisteRobot> lirePiste(const std::string& octets, std::string* erreur = nullptr);  // PGM (P2 ou P5)
std::string ecrirePgm(const PisteRobot& p);
double distanceObstacle(const PisteRobot& p, double x, double y, double angle, double portee);  // mm, lancer de rayon
struct RobotDeuxRoues {
  double x = 0, y = 0, cap = 0;  // mm, radians
  double entraxe = 85, vitesseMax = 400;  // mm, mm/s à pleine commande
  std::vector<std::pair<double, double>> capteursLigne{{40, 12}, {40, 0}, {40, -12}};  // (avant, gauche) mm
  std::vector<double> capteursDistance{0};  // angles (rad) des capteurs de distance, à l'avant
  double porteeDistance = 400;              // mm
};
struct BrochesRobot {
  std::string mliGauche = "PD6", sensGauche = "PD7", mliDroite = "PD5", sensDroite = "PB0";
  std::vector<std::string> capteursLigne{"PC0", "PC1", "PC2"};  // haut sur le noir
  std::vector<int> voiesDistance{3};                            // voies analogiques (5 V à 0 mm, 0 V à la portée)
};
struct OptionsRobot {
  double duree = 60, pas = 1e-3;  // s
  std::string attendu;            // texte attendu sur la liaison série (vide : jusqu'à la durée)
};
struct ResultatRobot {
  bool attenduRecu = false;
  double tempsAttendu = -1, temps = 0, distance = 0;
  std::string serie, erreur;
  int horsPiste = 0;  // pas sans aucun capteur sur la ligne
  std::vector<std::pair<double, double>> trajet;  // un point tous les 10 pas
};
ResultatRobot simulerRobot(EmulateurMicro& e, const PisteRobot& p, RobotDeuxRoues r, const BrochesRobot& b, const OptionsRobot& o);
// Commande sans écran : « --robot programme.elf [--piste piste.pgm] [--attendre texte] [--duree s] [--frequence Hz] »
// ; 0 quand le texte attendu arrive, 1 sinon, 2 sur une erreur ; le compte rendu va dans « sortie »
int commandeRobot(const std::vector<std::string>& arguments, std::string* sortie = nullptr);

// ---------- Deux versions comparées signal par signal ----------
using TraceBroches = std::map<std::string, std::vector<std::pair<double, bool>>>;  // port → fronts (temps, niveau)
TraceBroches tracerBroches(EmulateurMicro& e, const std::vector<std::string>& ports, double duree, double pas = 10e-6);
struct EcartTraces {
  bool identiques = true;
  std::vector<std::string> differences;  // « PB5 : front 3 à 1,503 s au lieu de 1,500 s »
};
EcartTraces comparerTraces(const TraceBroches& a, const TraceBroches& b, double tolerance);

}  // namespace prom::ecad
