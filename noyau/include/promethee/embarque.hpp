// Prométhée : programmes embarqués de la conception (lot L15 de la 0.34 ; 15.3, 15.6 ; EMB-03 à EMB-07).
//
// Onglet programme : les sources Arduino, MicroPython ou C sont rangées dans un sous-dossier du projet et citées par
// chemin relatif et SHA-256, comme les modèles SPICE (une source changée hors de Prométhée est « à revoir ») ; elles
// se compilent par la chaîne du poste (avr-gcc, arduino-cli s'il est installé, sinon le petit noyau Arduino écrit
// dans Prométhée ; MicroPython relu par python3) et se téléversent par avrdude, arduino-cli ou mpremote. La
// configuration des broches est lue dans le programme (pinMode, registres DDRx et PORTx, Pin(…) de MicroPython,
// périphériques ouverts) et comparée au schéma : chaque écart nomme la broche, la ligne et ce qui y est câblé.
// L'initialisation des temporisateurs, de l'I2C, du SPI, de la liaison série et du convertisseur s'écrit d'après les
// broches câblées.
//
// Modules et organigrammes : un module de la galerie (capteur, carte fille) apporte son symbole, son pilote et ses
// blocs ; l'organigramme (blocs reliés : action, décision, attente, boucle, bloc de module) se convertit en croquis
// Arduino ou en MicroPython, avec la table qui relie chaque bloc à ses lignes. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "promethee/electronique.hpp"

namespace prom::ecad {

// ---------- Programmes de la conception (15.3) ----------
struct ProgrammeEmbarque {
  std::string uuid, nom;
  std::string langage = "c";      // « c », « arduino », « micropython »
  std::string chemin;             // relatif au dossier du projet (« programmes/clignotant.c »)
  std::string sha256;             // du contenu, au dernier enregistrement par Prométhée
  std::string cible;              // repère du microcontrôleur (« U1 »)
  std::string carte = "arduino:avr:uno";  // carte d'arduino-cli
  double frequence = 16e6;        // Hz
  friend bool operator==(const ProgrammeEmbarque&, const ProgrammeEmbarque&) = default;
};
// Rangés dans c.extensions["programmes"]
std::vector<ProgrammeEmbarque> programmesDe(const ConceptionElec& c);
void ecrireProgrammes(ConceptionElec& c, const std::vector<ProgrammeEmbarque>& p);
// Ajoute ou remplace (même uuid ou même chemin) ; le SHA-256 est calculé sur « contenu »
ProgrammeEmbarque ajouterProgramme(ConceptionElec& c, ProgrammeEmbarque p, const std::string& contenu);
// Source changée hors de Prométhée : son SHA-256 ne correspond plus
bool programmeModifie(const ProgrammeEmbarque& p, const std::string& contenuActuel);
std::string langageDuFichier(const std::string& chemin);  // « .ino » arduino, « .py » micropython, sinon c

// ---------- Broches du microcontrôleur ----------
// Port d'une broche Arduino Uno (« 13 », « D13 », « A0 », « LED_BUILTIN ») : « PB5 », « PC0 » ; vide si inconnue
std::string portArduinoUno(const std::string& broche);
std::string brocheArduinoUno(const std::string& port);  // « PB5 » → « D13 », « PC0 » → « A0 »
// Numéro de broche du symbole dont le nom commence par le port (« PB5/SCK » → « 19 ») ; vide sinon
std::string numeroDuPort(const DefSymbole& s, const std::string& port);
std::string portDuNumero(const DefSymbole& s, const std::string& numero);  // « 19 » → « PB5 »

enum class ModeBroche : std::uint8_t { Entree, EntreeTirage, Sortie, Analogique, Pwm, I2C, SPI, Serie };
const char* nomMode(ModeBroche m);  // « entrée », « entrée avec tirage », « sortie »…
struct ConfigBroche {
  std::string port;      // « PB5 » (ou « GP2 » pour MicroPython)
  std::string ecrit;     // tel qu'écrit dans le programme (« LED_BUILTIN », « 13 », « PB5 »)
  ModeBroche mode = ModeBroche::Entree;
  int ligne = 0;
};
// Configuration lue dans le programme ; une broche configurée deux fois garde la dernière configuration
std::vector<ConfigBroche> lireConfiguration(const std::string& source, const std::string& langage);

// ---------- Écarts entre le programme et le schéma ----------
struct EcartProgramme {
  std::string message;        // « PB5 (D13) déclarée en entrée par le programme (ligne 4), câblée à la LED D1 »
  std::string port, numero;   // broche du microcontrôleur
  std::string composant;      // UUID du microcontrôleur (un clic montre la broche)
  std::string autre;          // UUID de ce qui est câblé (LED, bouton…), vide sinon
  int ligne = 0;
  std::string genre;          // « entrée vers une charge », « sortie sur une commande », « broche en l'air »,
                              // « analogique impossible », « MLI impossible », « I2C sans tirage »
};
// Ce qui est câblé à une broche : composants du réseau, et ceux d'au-delà d'une résistance en série
struct Raccordement {
  std::string composant, reference, symbole, broche;
  bool parResistance = false;
};
std::vector<Raccordement> raccordements(const ConceptionElec& c, const std::string& composant, const std::string& numero);
std::vector<EcartProgramme> comparerProgramme(const ConceptionElec& c, const ProgrammeEmbarque& p, const std::string& source);

// Initialisation écrite d'après les broches câblées : « arduino » (corps de setup()) ou « c » (registres de l'AVR)
std::string initialisationPeripheriques(const ConceptionElec& c, const std::string& micro, const std::string& langage);

// ---------- Compilation et téléversement ----------
struct ChaineEmbarquee {
  std::string nom, programme;  // « avr-gcc », chemin
  bool presente = false;
};
std::vector<ChaineEmbarquee> chainesDuPoste();  // avr-gcc, avr-objcopy, arduino-cli, mpremote, avrdude, avr-gdb
struct ErreurCompilation {
  std::string fichier, message;
  int ligne = 0, colonne = 0;
  bool avertissement = false;
};
struct ResultatCompilation {
  bool reussi = false;
  std::string elf, hex;      // fichiers produits
  std::string journal;       // sorties du compilateur
  std::string chaine;        // « avr-gcc », « arduino-cli », « noyau Arduino de Prométhée », « python3 »
  std::vector<ErreurCompilation> erreurs;
  std::size_t tailleProgramme = 0;  // octets de mémoire programme (texte + données initialisées)
};
// Compile la source (texte) dans « dossierSortie », pour l'ATmega328P à « frequence » ; « fichiers » : sources
// annexes (pilotes des modules) écrites à côté
ResultatCompilation compilerProgramme(const std::string& source, const std::string& langage, const std::string& dossierSortie, double frequence = 16e6,
                                      const std::map<std::string, std::string>& fichiers = {}, const std::string& carte = "arduino:avr:uno");
std::vector<ErreurCompilation> lireErreursGcc(const std::string& sortie);
// Ligne de commande du téléversement (avrdude, arduino-cli upload ou mpremote) ; vide si la chaîne manque
std::vector<std::string> commandeTeleversement(const ProgrammeEmbarque& p, const std::string& fichier, const std::string& port);
// Le petit noyau Arduino de Prométhée (Arduino.h, Wire.h, cœur), utilisé quand arduino-cli manque
const std::map<std::string, std::string>& noyauArduino();

// ---------- Modules de la galerie et organigrammes (15.6) ----------
struct BlocModule {
  std::string id, texte;          // « bme280.temperature », « lire la température »
  std::string code, codePython;   // « float {variable} = bme.readTemperature(); »
  std::string variable, type;     // « temperature », « float »
};
struct ModuleEmbarque {
  std::string id, nom, description, interface;  // « bme280 », « BME280 », …, « i2c »
  int adresse = -1;                              // I2C
  std::vector<std::pair<std::string, std::string>> broches;  // numéro → nom (« 1 » → « VCC »)
  std::map<std::string, std::string> pilote;    // fichiers du pilote (« bme280.h » → texte)
  std::string inclusion, declaration, initialisation;  // « #include "bme280.h" », « BME280 bme; », « bme.begin(0x76); »
  std::string inclusionPython, initialisationPython;
  std::vector<BlocModule> blocs;
};
const std::vector<ModuleEmbarque>& galerieEmbarquee();
const ModuleEmbarque* moduleEmbarque(const std::string& id);
DefSymbole symboleModuleEmbarque(const ModuleEmbarque& m);
// Pose le module près du microcontrôleur et le relie : SDA et SCL (ou MOSI, MISO, SCK, CS) aux broches du
// périphérique, VCC et GND aux alimentations du microcontrôleur, tirages de 4,7 kΩ ajoutés s'il n'y en a pas.
// Rend les repères posés ; « erreur » si le microcontrôleur manque
std::vector<std::string> poserModuleEmbarque(ConceptionElec& c, const std::string& module, const std::string& micro, std::string* erreur = nullptr);
// Modules embarqués posés sur la conception (champ « Module » du composant)
std::vector<const ModuleEmbarque*> modulesDeLaConception(const ConceptionElec& c);

struct BlocOrganigramme {
  std::string id;
  std::string genre;  // « début », « action », « décision », « attente », « boucle », « principal », « module », « fin »
  std::string texte;  // affiché
  std::map<std::string, std::string> parametres;  // broche, niveau, duree, condition, fois, module, bloc, variable, texte
  std::string suivant, siVrai, siFaux;            // décision : siVrai, siFaux puis suivant ; boucle : siVrai (corps) puis suivant
  double x = 0, y = 0;                             // position dans l'éditeur (mm)
};
struct Organigramme {
  std::string nom = "Programme";
  std::vector<BlocOrganigramme> blocs;
  const BlocOrganigramme* bloc(const std::string& id) const;
  BlocOrganigramme* bloc(const std::string& id);
  std::string debut() const;  // id du bloc « début »
};
// Organigrammes rangés dans c.extensions["organigrammes"]
std::vector<Organigramme> organigrammesDe(const ConceptionElec& c);
void ecrireOrganigrammes(ConceptionElec& c, const std::vector<Organigramme>& o);
// Blocs proposés : ceux des modules posés, puis les blocs de base (écrire une broche, lire une broche, attendre,
// écrire sur la liaison série, répéter, si…) ; « debut » filtre par le début du texte, sans égard aux accents
struct BlocPropose {
  std::string genre, texte, module, bloc;
  std::map<std::string, std::string> parametres;
};
std::vector<BlocPropose> blocsProposes(const ConceptionElec& c, const std::string& debut = {});
struct CodeOrganigramme {
  bool ok = false;
  std::string code;
  std::map<std::string, std::pair<int, int>> lignes;  // bloc → première et dernière lignes (à partir de 1)
  std::map<std::string, std::string> fichiers;         // pilotes des modules à compiler avec
  std::vector<std::string> erreurs;
  std::string blocDeLigne(int ligne) const;            // bloc qui a écrit cette ligne
};
CodeOrganigramme versArduino(const Organigramme& o, const ConceptionElec& c);
CodeOrganigramme versPython(const Organigramme& o, const ConceptionElec& c);
// Organigramme d'exemple : capteur lu toutes les secondes, valeur écrite sur la liaison série, LED allumée au-dessus
// d'un seuil
Organigramme organigrammeExemple(const std::string& module = "bme280", const std::string& broche = "13");

}  // namespace prom::ecad
