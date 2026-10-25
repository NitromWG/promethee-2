// Prométhée : essais du lot L15 de la 0.34 (programmes embarqués, co-simulation du microcontrôleur, mise au point,
// modules et organigrammes, composants décrits en HDL, robot sur une piste, machines à états, logique
// programmable). Les chaînes du poste (avr-gcc, simavr lié, avr-gdb, Icarus Verilog, GHDL, Yosys) manquantes font
// passer les essais qui en dépendent. Licence GPL-3.0-only.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "promethee/electronique.hpp"
#include "promethee/embarque.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hdl.hpp"
#include "promethee/logique.hpp"
#include "promethee/micro_simulation.hpp"
#include "promethee/processus.hpp"
#include "promethee/scripts_python.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"

using namespace prom;
using namespace prom::ecad;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {
struct Montage15 {
  ConceptionElec c = nouvelleConception("Montage");
  double x = 25.4, y = 50.8;
  ComposantElec* poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, y));
    x += 45.72;
    if (x > 260) x = 25.4, y += 60.96;
    ComposantElec* r = nullptr;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        r = c.composant(p.composant);
        r->reference = ref;
        if (!valeur.empty()) r->valeur = valeur;
        relier(p, liaisons);
      }
    return r;
  }
  // Étiquettes posées au bout de broches désignées par leur numéro ou leur nom
  void relier(const SymbolePlace& p, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    for (const auto& b : brochesPlacees(p, *c.symbole(p.symbole)))
      for (const auto& [num, reseau] : liaisons)
        if (b.broche->numero == num || b.broche->nom == num) {
          const Pt d = b.direction == 0 ? Pt{1, 0} : b.direction == 1 ? Pt{0, 1} : b.direction == 2 ? Pt{-1, 0} : Pt{0, -1};
          const Pt bout{b.p.x - d.x * 2'540'000, b.p.y - d.y * 2'540'000};
          c.feuilles[0].fils.push_back({nouvelUuid(), b.p, bout});
          Etiquette e;
          e.uuid = nouvelUuid();
          e.texte = reseau;
          e.p = bout;
          e.type = TypeEtiquette::Globale;
          c.feuilles[0].etiquettes.push_back(e);
        }
  }
  void relierComposant(const std::string& uuid, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    for (const auto& p : std::vector<SymbolePlace>(c.feuilles[0].symboles))
      if (p.composant == uuid) relier(p, liaisons);
  }
};
std::string dossierEssai(const std::string& nom) {
  std::random_device r;
  const fs::path p = fs::temp_directory_path() / ("promethee-l15-" + nom + "-" + std::to_string(r()));
  fs::create_directories(p);
  return utf8(p);
}
bool attendreTemps(const SimulationContinue& s, double t, double delai = 60) {
  const auto t0 = std::chrono::steady_clock::now();
  while (s.temps() < t && s.enCours()) {
    if (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() > delai) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (s.temps() >= t) return true;
  std::fprintf(stderr, "session : en cours %d, temps %g, erreur « %s »\n", s.enCours() ? 1 : 0, s.temps(), s.erreur().c_str());
  for (const auto& l : s.console()) std::fprintf(stderr, "  %s\n", l.c_str());
  return false;
}
bool avrGcc() { return !programmeDuPoste("avr-gcc").empty() && !programmeDuPoste("avr-objcopy").empty(); }
// Clignotant de l'ATmega328P : « bonjour » à 9 600 bauds, puis la LED de PB5 change d'état toutes les 500 ms
const char* CLIGNOTANT =
    "#include <avr/io.h>\n"
    "#include <util/delay.h>\n"
    "volatile unsigned char tours = 0;\n"
    "static void envoyer(const char* s) {\n"
    "  while (*s) {\n"
    "    while (!(UCSR0A & (1 << UDRE0))) {\n"
    "    }\n"
    "    UDR0 = *s++;\n"
    "  }\n"
    "}\n"
    "int main(void) {\n"
    "  DDRB |= (1 << PB5);\n"
    "  UBRR0H = 0;\n"
    "  UBRR0L = 103;  /* 9 600 bauds à 16 MHz */\n"
    "  UCSR0B = (1 << TXEN0);\n"
    "  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);\n"
    "  envoyer(\"bonjour\\r\\n\");\n"
    "  for (;;) {\n"
    "    PORTB |= (1 << PB5);  /* LED allumée */\n"
    "    _delay_ms(500);\n"
    "    PORTB &= ~(1 << PB5);\n"
    "    _delay_ms(500);\n"
    "    ++tours;\n"
    "  }\n"
    "}\n";
int ligneDe(const std::string& source, const std::string& morceau) {
  std::istringstream in(source);
  int n = 0;
  for (std::string l; std::getline(in, l);) {
    ++n;
    if (l.find(morceau) != std::string::npos) return n;
  }
  return -1;
}
// Clignotant posé : microcontrôleur, pile, résistance et LED sur PB5
Montage15 montageClignotant() {
  Montage15 m;
  m.poser("Pile", "BT1", "5V", {{"1", "+5V"}, {"2", "GND"}});
  m.poser("ATmega328P", "U1", "ATmega328P", {{"7", "+5V"}, {"8", "GND"}, {"20", "+5V"}, {"22", "GND"}, {"19", "LED"}});
  m.poser("R", "R1", "330", {{"1", "LED"}, {"2", "A"}});
  m.poser("LED", "D1", "rouge", {{"2", "A"}, {"1", "GND"}});
  return m;
}
std::string vecteurCourantLed(const NetlistSpice& n, const std::string& ref) {
  for (const auto& [inst, r] : n.instances)
    if (r == ref && !inst.empty() && inst[0] == 'd') return "@" + inst + "[id]";
  return {};
}
}  // namespace

TEST_CASE("15.3 · broche déclarée en entrée par le programme et câblée à une LED : écart listé, qui mène à la broche", "[034][L15][15.3]") {
  Montage15 m;
  m.poser("ATmega328P", "U1", "ATmega328P", {{"7", "+5V"}, {"8", "GND"}, {"20", "+5V"}, {"22", "GND"}, {"19", "LED"}, {"4", "BP"}});
  m.poser("R", "R1", "330", {{"1", "LED"}, {"2", "A"}});
  ComposantElec* d1 = m.poser("LED", "D1", "rouge", {{"2", "A"}, {"1", "GND"}});
  const std::string uuidD1 = d1->uuid;
  ComposantElec* sw = m.poser("Bouton", "SW1", "", {{"1", "BP"}, {"2", "GND"}});
  const std::string uuidSw = sw->uuid;
  const std::string croquis = "const int led = LED_BUILTIN;\nvoid setup() {\n  pinMode(led, INPUT);\n  pinMode(2, OUTPUT);\n  analogWrite(4, 128);\n}\nvoid loop() {}\n";
  ProgrammeEmbarque p;
  p.langage = langageDuFichier("programmes/essai.ino");
  p.cible = "U1";
  p.chemin = "programmes/essai.ino";
  CHECK(p.langage == "arduino");
  ajouterProgramme(m.c, p, croquis);
  REQUIRE(programmesDe(m.c).size() == 1);
  const ProgrammeEmbarque pr = programmesDe(m.c)[0];
  CHECK(!pr.uuid.empty());
  CHECK(pr.sha256.size() == 64);
  CHECK(!programmeModifie(pr, croquis));
  CHECK(programmeModifie(pr, croquis + "// changé ailleurs\n"));
  // Configuration lue : constantes résolues
  const auto config = lireConfiguration(croquis, "arduino");
  REQUIRE(config.size() >= 2);
  CHECK(config[0].port == "PB5");
  CHECK(config[0].ecrit == "led");
  CHECK(config[0].mode == ModeBroche::Entree);
  CHECK(config[0].ligne == 3);
  CHECK(portArduinoUno("A0") == "PC0");
  CHECK(brocheArduinoUno("PB5") == "D13");
  CHECK(numeroDuPort(*m.c.symbole("ATmega328P"), "PB5") == "19");
  // Écarts : chacun mène à la broche et à ce qui y est câblé
  const auto ecarts = comparerProgramme(m.c, pr, croquis);
  const EcartProgramme* led = nullptr;
  const EcartProgramme* bouton = nullptr;
  const EcartProgramme* mli = nullptr;
  for (const auto& e : ecarts) {
    UNSCOPED_INFO(e.message);
    if (e.genre == "entrée vers une charge") led = &e;
    if (e.genre == "sortie sur une commande") bouton = &e;
    if (e.genre == "MLI impossible") mli = &e;
  }
  REQUIRE(led);
  CHECK(led->numero == "19");
  CHECK(led->port == "PB5");
  CHECK(led->composant == m.c.parReference("U1")->uuid);
  CHECK(led->autre == uuidD1);
  CHECK(led->ligne == 3);
  CHECK(led->message.find("D13") != std::string::npos);
  CHECK(led->message.find("LED D1") != std::string::npos);
  REQUIRE(bouton);
  CHECK(bouton->numero == "4");
  CHECK(bouton->autre == uuidSw);
  CHECK(mli);
  // Corrigé : plus d'écart sur la LED
  const std::string juste = "void setup() {\n  pinMode(13, OUTPUT);\n  pinMode(2, INPUT_PULLUP);\n}\nvoid loop() {\n  digitalWrite(13, digitalRead(2));\n}\n";
  bool encore = false;
  for (const auto& e : comparerProgramme(m.c, pr, juste)) encore |= e.port == "PB5" || e.port == "PD2";
  CHECK(!encore);
  // En C, sur les registres : DDRB laissé en entrée
  ProgrammeEmbarque pc = pr;
  pc.langage = "c";
  const std::string enC = "#include <avr/io.h>\nint main(void) {\n  DDRB &= ~(1 << PB5);\n  PORTD |= (1 << PD2);\n  for (;;) {}\n}\n";
  bool ecartC = false;
  for (const auto& e : comparerProgramme(m.c, pc, enC)) ecartC |= e.port == "PB5" && e.genre == "entrée vers une charge" && e.ligne == 3;
  CHECK(ecartC);
  const auto cfgC = lireConfiguration(enC, "c");
  REQUIRE(cfgC.size() == 2);
  CHECK(cfgC[1].port == "PD2");
  CHECK(cfgC[1].mode == ModeBroche::EntreeTirage);
  // MicroPython
  const auto cfgPy = lireConfiguration("from machine import Pin\nled = Pin(25, Pin.OUT)\nbp = Pin(14, Pin.IN, Pin.PULL_UP)\n", "micropython");
  REQUIRE(cfgPy.size() == 2);
  CHECK(cfgPy[0].port == "GP25");
  CHECK(cfgPy[1].mode == ModeBroche::EntreeTirage);
  // Initialisation écrite d'après le câblage
  const std::string init = initialisationPeripheriques(m.c, "U1", "arduino");
  INFO(init);
  CHECK(init.find("pinMode(13, OUTPUT)") != std::string::npos);
  CHECK(init.find("pinMode(2, INPUT_PULLUP)") != std::string::npos);
  const std::string initC = initialisationPeripheriques(m.c, "U1", "c");
  CHECK(initC.find("DDRB |= 0x20") != std::string::npos);
  CHECK(initC.find("PORTD |= 0x4") != std::string::npos);
  // Chaînes du poste, compilation et téléversement
  bool avr = false;
  for (const auto& ch : chainesDuPoste()) avr |= ch.nom == "avr-gcc" && ch.presente;
  CHECK(avr == avrGcc());
  if (!avrGcc()) {
    WARN("avr-gcc absent : compilation non essayée");
    return;
  }
  const std::string dossier = dossierEssai("compilation");
  const ResultatCompilation rc = compilerProgramme(juste, "arduino", dossier);
  INFO(rc.journal);
  REQUIRE(rc.reussi);
  CHECK(fs::exists(cheminFichier(rc.elf)));
  CHECK(fs::exists(cheminFichier(rc.hex)));
  CHECK(rc.tailleProgramme > 200);
  if (programmeDuPoste("arduino-cli").empty()) CHECK(rc.chaine == "noyau Arduino de Prométhée");
  // Une faute à la ligne 3 du croquis : l'erreur la nomme
  const ResultatCompilation faux = compilerProgramme("void setup() {\n  pinMode(13, OUTPUT);\n  digitalWrit(13, HIGH);\n}\nvoid loop() {}\n", "arduino", dossierEssai("faute"));
  CHECK(!faux.reussi);
  bool ligne3 = false;
  for (const auto& e : faux.erreurs) ligne3 |= e.ligne == 3 && !e.avertissement && e.fichier == "croquis.ino";
  CHECK(ligne3);
  // Téléversement : la commande du poste, sur le port donné
  const auto cmd = commandeTeleversement(pr, rc.hex, "/dev/ttyACM0");
  if (!programmeDuPoste("avrdude").empty()) {
    REQUIRE(!cmd.empty());
    CHECK(cmd.back().find(rc.hex) != std::string::npos);
  }
}

TEST_CASE("15.4 · clignotant pour ATmega328P : la LED du schéma change d'état toutes les 500 ms de temps simulé, et « bonjour » arrive au terminal à 9 600 bauds", "[034][L15][15.4]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  if (!avrGcc()) SKIP("avr-gcc n'est pas installé");
  if (!emulateurDisponible("atmega328p")) SKIP("Prométhée construit sans simavr");
  const ResultatCompilation rc = compilerProgramme(CLIGNOTANT, "c", dossierEssai("clignotant"));
  INFO(rc.journal);
  REQUIRE(rc.reussi);
  Montage15 m = montageClignotant();
  OptionsContinue oc;
  oc.pas = 1e-4;
  oc.duree = 1.3;
  oc.vitesse = 0;
  OptionsSimulation o;
  o.cosimulation["U1"] = "micro";
  const NetlistSpice n0 = netlistContinue(m.c, oc, o);
  INFO(n0.texte);
  REQUIRE(n0.valide());
  CHECK(n0.texte.find("SWBROCHE") != std::string::npos);
  std::string bas = n0.texte;
  for (auto& ch : bas) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  CHECK(bas.find(sourceBrocheCosimulee("U1", "19", 'd')) != std::string::npos);
  std::string erreur;
  auto emu = creerEmulateur("atmega328p", 16e6, &erreur);
  REQUIRE(emu);
  REQUIRE(emu->charger(rc.elf, &erreur));
  auto micro = std::make_shared<CosimulationMicro>(m.c, "U1", n0, std::move(emu));
  bool pb5 = false;
  for (const auto& b : micro->broches()) pb5 |= b.port == "PB5" && b.numero == "19" && b.reseau == "LED";
  CHECK(pb5);
  const std::string led = vecteurCourantLed(n0, "D1");
  REQUIRE(!led.empty());
  std::vector<std::string> sondes = micro->sondes();
  sondes.push_back(led);
  const NetlistSpice n = netlistContinue(m.c, oc, o, sondes);
  SimulationContinue s;
  s.ajouter(micro);
  REQUIRE(s.demarrer(n.texte, sondes, oc, &erreur));
  INFO(erreur);
  REQUIRE(attendreTemps(s, 1.25, 120));
  s.arreter();
  // Changements d'état de la LED : seuil de 1 mA
  const Releve r = s.echantillons(led);
  REQUIRE(r.size() > 100);
  std::vector<double> fronts;
  for (std::size_t i = 1; i < r.size(); ++i)
    if ((r[i - 1].v > 1e-3) != (r[i].v > 1e-3)) fronts.push_back(r[i].t);
  INFO("fronts : " << fronts.size());
  REQUIRE(fronts.size() >= 3);
  for (std::size_t i = 1; i < fronts.size(); ++i) CHECK(fronts[i] - fronts[i - 1] == Approx(0.5).epsilon(0.01));
  // Allumée : un courant de LED, au-delà de 5 mA sous 330 Ω
  double imax = 0;
  for (const auto& e : r) imax = std::max(imax, e.v);
  CHECK(imax > 5e-3);
  // Terminal : « bonjour » à 9 600 bauds, un octet toutes les 1,04 ms
  micro->terminal.debit = 9600;
  CHECK(micro->reglagesCompatibles());
  CHECK(micro->emulateur().formatSerie().debit == Approx(9600).epsilon(0.01));
  const std::string texte = micro->texteRecu();
  CHECK(texte.rfind("bonjour", 0) == 0);
  const auto octets = micro->recus();
  REQUIRE(octets.size() >= 7);
  // Un octet par trame de l'USART simulée (10 bits utiles, simavr compte aussi le temps de reprise) : environ 1,1 ms
  CHECK(octets[1].t - octets[0].t > 9.5 / 9615);
  CHECK(octets[1].t - octets[0].t < 12.0 / 9615);
  // Mal réglé à 4 800 bauds : le terminal ne lit pas « bonjour »
  micro->terminal.debit = 4800;
  CHECK(!micro->reglagesCompatibles());
  CHECK(micro->texteRecu().find("bonjour") == std::string::npos);
  // Traces : les changements de PB5 et les octets
  bool tracePb5 = false, traceSerie = false;
  for (const auto& t : micro->traces()) {
    tracePb5 |= t.texte.rfind("PB5 → 1", 0) == 0;
    traceSerie |= t.texte.find("'b'") != std::string::npos;
  }
  CHECK(tracePb5);
  CHECK(traceSerie);
}

TEST_CASE("15.5 · point d'arrêt sur la ligne qui allume la LED : la simulation s'arrête LED éteinte, et un pas plus loin elle s'allume", "[034][L15][15.5]") {
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  if (!avrGcc()) SKIP("avr-gcc n'est pas installé");
  if (!emulateurDisponible("atmega328p")) SKIP("Prométhée construit sans simavr");
  const std::string dossier = dossierEssai("mise-au-point");
  const ResultatCompilation rc = compilerProgramme(CLIGNOTANT, "c", dossier);
  REQUIRE(rc.reussi);
  const int ligne = ligneDe(CLIGNOTANT, "LED allumée");
  REQUIRE(ligne > 0);
  // Table des lignes de l'ELF et GDB du poste en GDB/MI : même adresse
  std::string erreur;
  const TableLignes table = tableLignes(rc.elf, &erreur);
  INFO(erreur);
  REQUIRE(!table.lignes.empty());
  const auto adresse = table.adresse("programme.c", ligne);
  REQUIRE(adresse);
  CHECK(table.ligneDe(*adresse)->ligne == ligne);
  std::optional<std::uint32_t> variable;
  if (!programmeDuPoste("avr-gdb").empty()) {
    PiloteGdb gdb;
    REQUIRE(gdb.demarrer(rc.elf, &erreur));
    const auto a = gdb.adresseLigne("programme.c", ligne);
    REQUIRE(a);
    CHECK(*a == *adresse);
    variable = gdb.adresseVariable("tours");
    REQUIRE(variable);
    CHECK(*variable >= 0x100);
    CHECK(gdb.typeVariable("tours").find("char") != std::string::npos);
    gdb.arreter();
  } else
    WARN("avr-gdb absent : GDB/MI non essayé");
  // Symboles de l'ELF : la variable « tours » en mémoire de données
  bool tours = false;
  for (const auto& sy : symbolesElf(rc.elf)) tours |= sy.nom == "tours" && sy.adresse >= 0x100 && (!variable || sy.adresse == *variable);
  CHECK(tours);
  Montage15 m = montageClignotant();
  OptionsContinue oc;
  oc.pas = 1e-4;
  oc.duree = 2;
  oc.vitesse = 0;
  OptionsSimulation o;
  o.cosimulation["U1"] = "micro";
  const NetlistSpice n0 = netlistContinue(m.c, oc, o);
  auto emu = creerEmulateur("atmega328p", 16e6, &erreur);
  REQUIRE(emu);
  REQUIRE(emu->charger(rc.elf, &erreur));
  auto micro = std::make_shared<CosimulationMicro>(m.c, "U1", n0, std::move(emu));
  micro->debogueur().setTable(table);
  REQUIRE(micro->debogueur().ajouterPoint("programme.c", ligne));
  const std::string led = vecteurCourantLed(n0, "D1");
  std::vector<std::string> sondes = micro->sondes();
  sondes.push_back(led);
  const NetlistSpice n = netlistContinue(m.c, oc, o, sondes);
  SimulationContinue s;
  s.ajouter(micro);
  REQUIRE(s.demarrer(n.texte, sondes, oc, &erreur));
  // Arrêt sur le point d'arrêt : la LED est éteinte, le circuit attend
  REQUIRE(micro->attendreArret(60));
  INFO(micro->raisonArret());
  CHECK(micro->raisonArret().find("ligne " + std::to_string(ligne)) != std::string::npos);
  CHECK(micro->emulateur().broche("PB5") == EtatBroche::Bas);
  CHECK(s.valeur(led) < 1e-4);
  const double tArret = s.temps();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  CHECK(s.temps() == tArret);  // le circuit est arrêté avec le programme
  CHECK(micro->debogueur().ligneCourante()->ligne == ligne);
  CHECK(micro->emulateur().registres().size() == 32);
  if (variable) CHECK(micro->debogueur().lireVariable(*variable, 1) == 0);
  // Un pas : la ligne s'exécute, le circuit fait un pas, la LED s'allume
  micro->pasLigne();
  REQUIRE(micro->attendreArret(30));
  CHECK(micro->emulateur().broche("PB5") == EtatBroche::Haut);
  CHECK(s.valeur(led) > 5e-3);
  CHECK(micro->debogueur().ligneCourante()->ligne > ligne);
  // Une instruction de plus, puis on repart jusqu'au prochain passage
  micro->pasInstruction();
  REQUIRE(micro->attendreArret(30));
  micro->continuer();
  REQUIRE(micro->attendreArret(60));
  CHECK(micro->debogueur().ligneCourante()->ligne == ligne);
  CHECK(s.temps() == Approx(tArret + 1.0).margin(0.02));
  if (variable) CHECK(micro->debogueur().lireVariable(*variable, 1) == 1);
  s.arreter();
  CHECK(!s.enCours());
  // Arrêt sur une condition du circuit : la tension de la broche de la LED dépasse 2,5 V
  auto emu2 = creerEmulateur("atmega328p", 16e6, &erreur);
  REQUIRE(emu2->charger(rc.elf, &erreur));
  auto micro2 = std::make_shared<CosimulationMicro>(m.c, "U1", n0, std::move(emu2));
  const std::string vLed = vecteurTension(n0, "LED");
  micro2->conditionCircuit = [vLed](const std::map<std::string, double>& v) {
    const auto it = v.find(vLed);
    return it != v.end() && it->second > 2.5;
  };
  SimulationContinue s2;
  s2.ajouter(micro2);
  REQUIRE(s2.demarrer(n.texte, sondes, oc, &erreur));
  REQUIRE(micro2->attendreArret(60));
  CHECK(micro2->raisonArret() == "condition du circuit vraie");
  CHECK(micro2->emulateur().broche("PB5") == EtatBroche::Haut);
  // Variable surveillée : arrêt quand « tours » change
  if (variable) {
    micro2->debogueur().surveiller("tours", *variable, 1);
    micro2->arretSurVariable = true;
    micro2->continuer();
    REQUIRE(micro2->attendreArret(60));
    CHECK(micro2->raisonArret() == "variable surveillée changée");
    CHECK(micro2->debogueur().variables()[0].valeur == 1);
  }
  s2.arreter();
}

TEST_CASE("15.6 · capteur BME280 de la galerie : relié en I2C, bloc « lire la température » proposé, organigramme converti en croquis Arduino qui compile", "[034][L15][15.6]") {
  Montage15 m;
  m.poser("ATmega328P", "U1", "ATmega328P", {{"7", "+5V"}, {"8", "GND"}, {"20", "+5V"}, {"22", "GND"}, {"19", "LED"}});
  m.poser("R", "R1", "330", {{"1", "LED"}, {"2", "A"}});
  m.poser("LED", "D1", "rouge", {{"2", "A"}, {"1", "GND"}});
  std::string erreur;
  const auto poses = poserModuleEmbarque(m.c, "bme280", "U1", &erreur);
  INFO(erreur);
  REQUIRE(poses.size() == 3);  // le capteur et ses deux tirages
  const ComposantElec* capteur = m.c.parReference(poses[0]);
  REQUIRE(capteur);
  CHECK(capteur->champs.at("Module") == "bme280");
  const Connectivite k = connectivite(m.c);
  const std::string u1 = m.c.parReference("U1")->uuid;
  CHECK(k.nom(capteur->uuid, "4") == k.nom(u1, "27"));  // SDA sur PC4
  CHECK(k.nom(capteur->uuid, "3") == k.nom(u1, "28"));  // SCL sur PC5
  CHECK(k.nom(capteur->uuid, "1") == "+5V");
  CHECK(k.nom(capteur->uuid, "2") == "GND");
  // Les tirages vers l'alimentation : plus d'écart « I2C sans tirage »
  ProgrammeEmbarque p;
  p.langage = "arduino";
  p.cible = "U1";
  bool sansTirage = false;
  for (const auto& e : comparerProgramme(m.c, p, "#include <Wire.h>\nvoid setup() { Wire.begin(); }\nvoid loop() {}\n")) sansTirage |= e.genre == "I2C sans tirage";
  CHECK(!sansTirage);
  // Blocs proposés, avec complétion
  const auto blocs = blocsProposes(m.c, "temp");
  REQUIRE(!blocs.empty());
  CHECK(blocs[0].texte == "lire la température");
  CHECK(blocs[0].module == "bme280");
  CHECK(blocsProposes(m.c, "lire").size() >= 3);
  // Organigramme converti en croquis, chaque bloc relié à ses lignes
  Organigramme o = organigrammeExemple("bme280", "13");
  ecrireOrganigrammes(m.c, {o});
  REQUIRE(organigrammesDe(m.c).size() == 1);
  o = organigrammesDe(m.c)[0];
  CHECK(o.blocs.size() == 8);
  const CodeOrganigramme code = versArduino(o, m.c);
  INFO(code.code);
  for (const auto& e : code.erreurs) UNSCOPED_INFO(e);
  REQUIRE(code.ok);
  CHECK(code.code.find("#include \"bme280.h\"") != std::string::npos);
  CHECK(code.code.find("temperature = bme.readTemperature();") != std::string::npos);
  CHECK(code.code.find("if (temperature > 25) {") != std::string::npos);
  CHECK(code.code.find("delay(1000);") != std::string::npos);
  CHECK(code.fichiers.count("bme280.cpp"));
  const int lLire = ligneDe(code.code, "bme.readTemperature()");
  CHECK(code.blocDeLigne(lLire) == "lire");
  CHECK(code.lignes.count("seuil"));
  CHECK(code.blocDeLigne(ligneDe(code.code, "digitalWrite(13, HIGH)")) == "allumer");
  // MicroPython
  const CodeOrganigramme py = versPython(o, m.c);
  INFO(py.code);
  REQUIRE(py.ok);
  CHECK(py.code.find("while True:") != std::string::npos);
  CHECK(py.code.find("temperature = bme.temperature()") != std::string::npos);
  CHECK(py.fichiers.count("bme280.py"));
  if (pythonDisponible()) CHECK(compilerProgramme(py.code, "micropython", dossierEssai("python")).reussi);
  if (!avrGcc()) {
    WARN("avr-gcc absent : croquis non compilé");
    return;
  }
  const ResultatCompilation rc = compilerProgramme(code.code, "arduino", dossierEssai("bme280"), 16e6, code.fichiers);
  INFO(rc.journal);
  CHECK(rc.reussi);
  CHECK(rc.tailleProgramme > 1000);
}

TEST_CASE("15.7 · compteur écrit en Verilog et câblé au schéma : mêmes sorties que le compteur XSPICE de l'essai de 14.2", "[034][L15][15.7]") {
  const std::string verilog = "// Compteur de 4 bits, remise à zéro asynchrone\nmodule compteur(input clk, input raz, output reg [3:0] q);\n"
                              "  initial q = 0;\n  always @(posedge clk or posedge raz)\n    if (raz) q <= 0;\n    else q <= q + 1;\nendmodule\n";
  const auto mod = lireModuleHdl(verilog, "verilog");
  REQUIRE(mod);
  CHECK(mod->nom == "compteur");
  REQUIRE(mod->ports.size() == 3);
  CHECK(mod->ports[2].sortie);
  CHECK(mod->ports[2].largeur == 4);
  CHECK(mod->bitsSortie() == std::vector<std::string>{"q[0]", "q[1]", "q[2]", "q[3]"});
  const DefSymbole sym = symboleHdl(*mod);
  CHECK(sym.broches.size() == 8);  // clk, raz, q0 à q3, VCC, GND
  // VHDL : l'entité lue
  const std::string vhdl = "library ieee;\nuse ieee.std_logic_1164.all;\nuse ieee.numeric_std.all;\nentity compteur is\n  port (clk, raz : in std_logic; q : out std_logic_vector(3 downto 0));\nend entity;\n"
                           "architecture rtl of compteur is\n  signal n : unsigned(3 downto 0) := (others => '0');\nbegin\n  process (clk, raz)\n  begin\n    if raz = '1' then n <= (others => '0');\n"
                           "    elsif rising_edge(clk) then n <= n + 1;\n    end if;\n  end process;\n  q <= std_logic_vector(n);\nend architecture;\n";
  const auto modVhdl = lireModuleHdl(vhdl, "vhdl");
  REQUIRE(modVhdl);
  CHECK(modVhdl->ports.size() == 3);
  CHECK(modVhdl->ports[2].largeur == 4);
  // GHDL pas à pas : cinq fronts, cinq
  if (!outilHdl("vhdl").empty()) {
    SimulateurHdl g;
    std::string erreur;
    REQUIRE(g.demarrer(*modVhdl, dossierEssai("ghdl"), &erreur));
    INFO(erreur);
    std::optional<std::vector<int>> q;
    for (int i = 0; i < 5; ++i) {
      g.evaluer({1, 0});
      q = g.evaluer({0, 0});
    }
    REQUIRE(q);
    CHECK(valeurBinaire({static_cast<double>((*q)[0]), static_cast<double>((*q)[1]), static_cast<double>((*q)[2]), static_cast<double>((*q)[3])}, 0.5) == 5);
    g.arreter();
  } else
    WARN("GHDL absent : composant VHDL non essayé");
  if (!ngspiceDisponible()) SKIP("ngspice n'est pas installé");
  if (outilHdl("verilog").empty()) SKIP("ni Icarus Verilog ni Verilator");
  // Le schéma : horloge commune, compteur XSPICE de 14.2 et compteur Verilog
  Montage15 m;
  m.poser("Pile", "BT1", "5V", {{"1", "VCC"}, {"2", "GND"}});
  m.poser("Source_tension", "VG1", "externe", {{"1", "CLK"}, {"2", "GND"}});
  m.poser("Compteur_4bits", "U1", "", {{"1", "CLK"}, {"2", "GND"}, {"3", "Q0"}, {"4", "Q1"}, {"5", "Q2"}, {"6", "Q3"}, {"7", "VCC"}, {"8", "GND"}});
  const std::string u3 = poserComposantHdl(m.c, *mod, 0, ptMm(150, 120), "U3");
  REQUIRE(!u3.empty());
  m.relierComposant(u3, {{"clk", "CLK"}, {"raz", "GND"}, {"q0", "H0"}, {"q1", "H1"}, {"q2", "H2"}, {"q3", "H3"}, {"VCC", "VCC"}, {"GND", "GND"}});
  REQUIRE(moduleDuComposant(*m.c.parReference("U3")));
  OptionsContinue oc;
  oc.pas = 1e-4;
  oc.duree = 0.26;
  oc.vitesse = 0;
  OptionsSimulation o;
  o.cosimulation["U3"] = "hdl";
  const NetlistSpice n0 = netlistContinue(m.c, oc, o);
  INFO(n0.texte);
  REQUIRE(n0.valide());
  auto hdl = std::make_shared<CosimulationHdl>(m.c, "U3", n0);
  std::string erreur;
  REQUIRE(hdl->demarrer(dossierEssai("verilog"), &erreur));
  INFO(erreur);
  std::vector<std::string> q, h;
  for (const char* r : {"Q0", "Q1", "Q2", "Q3"}) q.push_back(vecteurTension(n0, r));
  for (const char* r : {"H0", "H1", "H2", "H3"}) h.push_back(vecteurTension(n0, r));
  std::vector<std::string> sondes = hdl->sondes();
  sondes.insert(sondes.end(), q.begin(), q.end());
  sondes.insert(sondes.end(), h.begin(), h.end());
  const NetlistSpice n = netlistContinue(m.c, oc, o, sondes);
  SimulationContinue s;
  Generateur g;
  g.forme = Generateur::Forme::Carre;
  g.frequence = 50;
  g.amplitude = -2.5;  // l'horloge part basse : aucun front à t = 0, ni pour l'un ni pour l'autre
  g.decalage = 2.5;
  s.regler(sourceExterne("VG1"), g);
  s.ajouter(hdl);
  REQUIRE(s.demarrer(n.texte, sondes, oc, &erreur));
  REQUIRE(attendreTemps(s, 0.255, 120));
  s.arreter();
  CHECK(hdl->evaluations() >= 20);
  // Au milieu de chaque demi-période basse : les deux compteurs montrent la même valeur
  std::vector<Releve> rq, rh;
  for (const auto& v : q) rq.push_back(s.echantillons(v));
  for (const auto& v : h) rh.push_back(s.echantillons(v));
  int derniere = -1, egaux = 0;
  for (int k = 0; k < 12; ++k) {
    const double t = k * 0.02 + 0.015;
    std::vector<double> vq, vh;
    for (const auto& r : rq) vq.push_back(valeurA(r, t));
    for (const auto& r : rh) vh.push_back(valeurA(r, t));
    const int a = valeurBinaire(vq), b = valeurBinaire(vh);
    INFO("t = " << t << " s : XSPICE " << a << ", Verilog " << b);
    CHECK(a == b);
    egaux += a == b;
    derniere = b;
  }
  CHECK(egaux == 12);
  CHECK(derniere >= 10);
}

TEST_CASE("15.8 · suivi de ligne sur une piste ovale de 2 m : tour complet en simulation, et la commande rend 0 quand « tour fini » arrive sur la liaison série", "[034][L15][15.8]") {
  // La piste : 2 m de ligne, barre de départ ; relue de son image
  const PisteDepart d = pisteOvale();
  CHECK(d.piste.largeur > 800);
  CHECK(d.piste.pixel(d.x + 80, d.y) < 128);       // la barre
  CHECK(d.piste.pixel(d.x, d.y - 30) == 255);      // à côté de la ligne
  const auto relue = lirePiste(ecrirePgm(d.piste));
  REQUIRE(relue);
  CHECK(relue->gris == d.piste.gris);
  CHECK(distanceObstacle(d.piste, 10, 10, 0, 300) == 300);  // aucun obstacle
  if (!avrGcc()) SKIP("avr-gcc n'est pas installé");
  if (!emulateurDisponible("atmega328p")) SKIP("Prométhée construit sans simavr");
  const std::string robot =
      "#include <avr/io.h>\n"
      "/* Suivi de ligne : capteurs PC0 gauche, PC1 milieu, PC2 droite (hauts sur le noir) ; moteurs OC0A (PD6) à gauche,\n"
      "   OC0B (PD5) à droite ; deux passages sur la barre de départ : un tour */\n"
      "static void envoyer(const char* s) {\n  while (*s) {\n    while (!(UCSR0A & (1 << UDRE0))) {\n    }\n    UDR0 = *s++;\n  }\n}\n"
      "int main(void) {\n  DDRD |= (1 << PD6) | (1 << PD5) | (1 << PD7);\n  DDRB |= (1 << PB0);\n"
      "  TCCR0A = (1 << COM0A1) | (1 << COM0B1) | (1 << WGM01) | (1 << WGM00);\n  TCCR0B = (1 << CS01);\n"
      "  UBRR0 = 51;  /* 9 600 bauds à 8 MHz */\n  UCSR0B = (1 << TXEN0);\n  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);\n"
      "  unsigned char barres = 0, surBarre = 0;\n"
      "  for (;;) {\n    const unsigned char c = PINC & 7, g = c & 1, m = (c >> 1) & 1, d = (c >> 2) & 1;\n"
      "    if (g && m && d) {\n      if (!surBarre) {\n        surBarre = 1;\n        if (++barres == 2) envoyer(\"tour fini\\r\\n\");\n      }\n    } else if (!g && !d)\n      surBarre = 0;\n"
      "    if (g && !d) {\n      OCR0A = 40;\n      OCR0B = 170;\n    } else if (d && !g) {\n      OCR0A = 170;\n      OCR0B = 40;\n    } else {\n      OCR0A = 150;\n      OCR0B = 150;\n    }\n  }\n}\n";
  const std::string dossier = dossierEssai("robot");
  const ResultatCompilation rc = compilerProgramme(robot, "c", dossier, 8e6);
  INFO(rc.journal);
  REQUIRE(rc.reussi);
  // Simulation directe : un tour, sans quitter la ligne
  std::string erreur;
  auto e = creerEmulateur("atmega328p", 8e6, &erreur);
  REQUIRE(e->charger(rc.elf, &erreur));
  RobotDeuxRoues r;
  r.x = d.x;
  r.y = d.y;
  r.cap = d.cap;
  r.capteursLigne = {{40, 14}, {40, 0}, {40, -14}};
  OptionsRobot op;
  op.duree = 40;
  op.attendu = "tour fini";
  const ResultatRobot res = simulerRobot(*e, d.piste, r, BrochesRobot{}, op);
  INFO("temps " << res.temps << " s, distance " << res.distance << " mm, hors piste " << res.horsPiste << ", série « " << res.serie << " »");
  REQUIRE(res.attenduRecu);
  CHECK(res.distance == Approx(2080).epsilon(0.08));  // 80 mm jusqu'à la barre, puis le tour
  CHECK(res.horsPiste < 50);
  // La commande sans écran : 0 quand le texte arrive
  std::string sortie;
  const int code = commandeRobot({"--robot", rc.elf, "--attendre", "tour fini", "--duree", "40", "--frequence", "8000000"}, &sortie);
  INFO(sortie);
  CHECK(code == 0);
  CHECK(sortie.find("attendu reçu") != std::string::npos);
  // Sur une durée trop courte : 1
  CHECK(commandeRobot({"--robot", rc.elf, "--attendre", "tour fini", "--duree", "2", "--frequence", "8000000"}) == 1);
  // La piste lue d'un fichier
  const std::string fPiste = dossier + "/piste.pgm";
  ecrireFichierAtomique(cheminFichier(fPiste), ecrirePgm(d.piste));
  CHECK(commandeRobot({"--robot", rc.elf, "--piste", fPiste, "--attendre", "tour fini", "--duree", "40", "--frequence", "8000000"}) == 0);
  // Deux versions comparées signal par signal : même programme identique, un délai changé repéré
  const ResultatCompilation a = compilerProgramme(CLIGNOTANT, "c", dossierEssai("version-a"));
  std::string autre = CLIGNOTANT;
  autre.replace(autre.find("_delay_ms(500);"), 15, "_delay_ms(510);");
  const ResultatCompilation b = compilerProgramme(autre, "c", dossierEssai("version-b"));
  REQUIRE(a.reussi);
  REQUIRE(b.reussi);
  auto ea = creerEmulateur("atmega328p", 16e6), ea2 = creerEmulateur("atmega328p", 16e6), eb = creerEmulateur("atmega328p", 16e6);
  REQUIRE(ea->charger(a.elf));
  REQUIRE(ea2->charger(a.elf));
  REQUIRE(eb->charger(b.elf));
  const TraceBroches ta = tracerBroches(*ea, {"PB5"}, 2.2, 1e-4), ta2 = tracerBroches(*ea2, {"PB5"}, 2.2, 1e-4), tb = tracerBroches(*eb, {"PB5"}, 2.2, 1e-4);
  CHECK(ta.at("PB5").size() >= 4);
  CHECK(comparerTraces(ta, ta2, 1e-3).identiques);
  const EcartTraces ec = comparerTraces(ta, tb, 1e-3);
  CHECK(!ec.identiques);
  REQUIRE(!ec.differences.empty());
  CHECK(ec.differences[0].find("PB5") != std::string::npos);
  CHECK(comparerTraces(ta, tb, 0.05).identiques);  // à 50 ms près, les deux versions se valent
}

TEST_CASE("15.10 · feux tricolores : même séquence en machine à états, en circuit à bascules et en VHDL simulé par GHDL", "[034][L15][15.10]") {
  const MachineEtats m = machineFeuxTricolores();
  std::vector<std::map<std::string, bool>> entrees(24);
  for (int p : {6, 7, 15}) entrees[static_cast<std::size_t>(p)]["pieton"] = true;
  const SequenceSorties machine = simulerMachine(m, entrees, 24);
  REQUIRE(machine.size() == 24);
  // rouge trois périodes, vert jusqu'à l'appel du piéton, orange une période
  CHECK(machine[0].at("rouge"));
  CHECK(machine[2].at("rouge"));
  CHECK(machine[3].at("vert"));
  CHECK(machine[6].at("vert"));
  CHECK(machine[7].at("orange"));
  CHECK(machine[8].at("rouge"));
  for (const auto& l : machine) CHECK((l.at("rouge") + l.at("orange") + l.at("vert")) == 1);
  // Table des transitions
  const auto table = tableTransitions(m);
  CHECK(table.size() == 6);
  CHECK(table[4].condition == "pieton");
  // Aller et retour par le GRAFCET
  const prom::elec::Grafcet g = grafcetDepuisMachine(m);
  CHECK(g.etapes.size() == 6);
  const auto relue = machineDepuisGrafcet(g);
  REQUIRE(relue);
  CHECK(simulerMachine(*relue, entrees, 24).size() == 24);
  bool memes = true;
  const SequenceSorties parGrafcet = simulerMachine(*relue, entrees, 24);
  for (std::size_t i = 0; i < 24; ++i) memes &= parGrafcet[i] == machine[i];
  CHECK(memes);
  // Circuit à bascules : équations réduites, portes et bascules D posées, simulées par le simulateur logique
  EquationsMachine eq;
  const ConceptionElec c = circuitBascules(m, &eq);
  CHECK(eq.bits == 3);
  for (const auto& [k, t] : eq.textes) UNSCOPED_INFO(k << " = " << t);
  int bascules = 0;
  for (const auto& x : c.composants) bascules += x.symbole == "Bascule_D";
  CHECK(bascules == 3);
  const SequenceSorties circuit = simulerCircuitBascules(c, m, entrees, 24);
  REQUIRE(circuit.size() == 24);
  for (std::size_t i = 0; i < 24; ++i) {
    INFO("période " << i);
    CHECK(circuit[i] == machine[i]);
  }
  // VHDL simulé par GHDL
  const std::string vhdl = vhdlMachine(m);
  CHECK(vhdl.find("entity feux is") != std::string::npos);
  CHECK(vhdl.find("rising_edge(horloge)") != std::string::npos);
  if (programmeDuPoste("ghdl").empty()) {
    WARN("GHDL absent : VHDL non simulé");
  } else {
    const ResultatGhdl r = simulerGhdl({{"feux.vhd", vhdl}, {"banc_feux.vhd", bancVhdlMachine(m, entrees, 24)}}, "banc_feux", dossierEssai("feux"));
    INFO(r.journal);
    REQUIRE(r.ok);
    const SequenceSorties parGhdl = lireSequenceGhdl(r.lignes, m.sorties);
    REQUIRE(parGhdl.size() == 24);
    for (std::size_t i = 0; i < 24; ++i) {
      INFO("période " << i);
      CHECK(parGhdl[i] == machine[i]);
    }
  }
  // Réduction : trois variables, impairs → a
  const auto impl = reduire(3, {1, 3, 5, 7});
  REQUIRE(impl.size() == 1);
  CHECK(sommeDeProduits(impl, {"a", "b", "c"}, 3) == "a");
  CHECK(sommeDeProduits(reduire(2, {3}), {"a", "b"}, 2) == "b·a");
  // Schéma à contacts : marche maintenue, temporisation de 2 s, arrêt
  ProgrammeContacts pc;
  pc.echelons.push_back({{{{"contact", "marche"}, {"contactInverse", "arret"}}, {{"contact", "KM"}, {"contactInverse", "arret"}}}, {"bobine", "KM"}});
  pc.echelons.push_back({{{{"contact", "KM"}}}, {"tempo", "T1", 2.0}});
  pc.echelons.push_back({{{{"contact", "T1"}}}, {"bobine", "lampe"}});
  pc.echelons.push_back({{{{"contact", "KM"}}}, {"compteur", "C1", 0, 3}});
  AutomateContacts a(pc);
  a.fixer("marche", true);
  a.cycle(0.1);
  CHECK(a.valeur("KM"));
  a.fixer("marche", false);
  for (int i = 0; i < 15; ++i) a.cycle(0.1);
  CHECK(a.valeur("KM"));
  CHECK(!a.valeur("lampe"));
  for (int i = 0; i < 6; ++i) a.cycle(0.1);
  CHECK(a.valeur("lampe"));
  a.fixer("arret", true);
  a.cycle(0.1);
  CHECK(!a.valeur("KM"));
  a.cycle(0.1);
  CHECK(!a.valeur("lampe"));
  CHECK(a.compte("C1") == 1);
}

TEST_CASE("15.12 · additionneur complet exporté en VHDL et simulé par GHDL : même table de vérité que le schéma", "[034][L15][15.12]") {
  Montage15 m;
  m.poser("Porte_OU_X", "U1", "", {{"1", "A"}, {"2", "B"}, {"3", "T1"}});
  m.poser("Porte_OU_X", "U2", "", {{"1", "T1"}, {"2", "CIN"}, {"3", "S"}});
  m.poser("Porte_ET", "U3", "", {{"1", "A"}, {"2", "B"}, {"3", "T2"}});
  m.poser("Porte_ET", "U4", "", {{"1", "T1"}, {"2", "CIN"}, {"3", "T3"}});
  m.poser("Porte_OU", "U5", "", {{"1", "T2"}, {"2", "T3"}, {"3", "COUT"}});
  const CircuitLogique cl = circuitLogique(m.c);
  const std::vector<std::string> e{"A", "B", "CIN"}, s{"S", "COUT"};
  const auto table = tableVerite(cl, e, s);
  REQUIRE(table.size() == 8);
  CHECK(table[7].second == "11");
  CHECK(table[3].second == "01");
  const std::string vhdl = vhdlStructurel(cl, "circuit");
  INFO(vhdl);
  CHECK(vhdl.find("entity circuit is") != std::string::npos);
  CHECK(vhdl.find("xor") != std::string::npos);
  if (programmeDuPoste("ghdl").empty()) {
    WARN("GHDL absent : VHDL non simulé");
  } else {
    std::string erreur;
    const auto parGhdl = tableVeriteGhdl(cl, e, s, dossierEssai("additionneur"), &erreur);
    INFO(erreur);
    REQUIRE(parGhdl.size() == 8);
    CHECK(parGhdl == table);
  }
  // Synthèse par Yosys du Verilog structurel
  if (!programmeDuPoste("yosys").empty()) {
    const SyntheseYosys y = synthetiserYosys(verilogStructurel(cl, "additionneur"), "additionneur", dossierEssai("yosys"));
    INFO(y.journal);
    CHECK(y.ok);
    CHECK(y.cellules > 0);
  } else
    WARN("Yosys absent : synthèse non essayée");
  // GAL16V8 : sommes de produits placées, JEDEC relu, même table
  std::vector<std::uint32_t> unsS, unsC;
  for (std::uint32_t x = 0; x < 8; ++x) {
    // variable 0 : A, 1 : B, 2 : CIN
    const int a = x & 1, b = (x >> 1) & 1, c = (x >> 2) & 1;
    if ((a + b + c) % 2) unsS.push_back(x);
    if (a + b + c >= 2) unsC.push_back(x);
  }
  const PlacementGal gal = placerGal16V8(e, {{"S", reduire(3, unsS)}, {"COUT", reduire(3, unsC)}}, "ADD");
  for (const auto& x : gal.erreurs) UNSCOPED_INFO(x);
  REQUIRE(gal.ok);
  CHECK(gal.jedec.find("*QF2194*") != std::string::npos);
  const auto f = lireJedec(gal.jedec);
  REQUIRE(f);
  CHECK(f->nombre == 2194);
  CHECK(f->sommeJuste);
  for (std::uint32_t x = 0; x < 8; ++x) {
    const std::map<int, bool> en{{gal.brochesEntrees.at("A"), (x & 1) != 0}, {gal.brochesEntrees.at("B"), (x & 2) != 0}, {gal.brochesEntrees.at("CIN"), (x & 4) != 0}};
    const int a = x & 1, b = (x >> 1) & 1, c = (x >> 2) & 1;
    CHECK(evaluerGal16V8(*f, gal.brochesSorties.at("S"), en) == ((a + b + c) % 2 == 1));
    CHECK(evaluerGal16V8(*f, gal.brochesSorties.at("COUT"), en) == (a + b + c >= 2));
  }
}
