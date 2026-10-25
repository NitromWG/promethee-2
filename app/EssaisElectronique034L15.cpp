// Prométhée : essais d'interface de la 0.34, lot L15 (microcontrôleurs, FPGA et logique) — onglet programme : écart
// d'une broche en entrée câblée à une LED, un clic montre la broche ; programme rangé dans le projet (15.3) ;
// co-simulation : la LED change toutes les 500 ms et « bonjour » arrive au terminal (15.4) ; point d'arrêt sur la
// ligne qui allume la LED, un pas et elle s'allume (15.5) ; BME280 posé, organigramme converti et compilé (15.6) ;
// compteur Verilog co-simulé contre le compteur XSPICE (15.7) ; robot sur la piste ovale, et la commande sans écran
// qui rend 0 (15.8) ; feux tricolores en machine, bascules et GHDL (15.10) ; additionneur exporté et simulé par GHDL,
// JEDEC du GAL16V8 (15.12). Appelés seuls avec PROMETHEE_ESSAIS_LOTS=034L15. Licence GPL-3.0-only.
#include <cmath>
#include <filesystem>
#include <functional>
#include <random>
#include <string>

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "Embarque034.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "Laboratoire034.hpp"
#include "VueSchema.hpp"
#include "promethee/electronique.hpp"
#include "promethee/embarque.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hdl.hpp"
#include "promethee/micro_simulation.hpp"
#include "promethee/modele.hpp"
#include "promethee/processus.hpp"
#include "promethee/simulation.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;
namespace fs = std::filesystem;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
bool attendreQue(const std::function<bool()>& c, int ms) {
  for (int t = 0; t < ms && !c(); t += 20) attendre(20);
  return c();
}
struct Montage {
  ConceptionElec c;
  double x = 25.4, y = 50.8;
  explicit Montage(const std::string& nom) : c(nouvelleConception(nom)) {}
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
  ComposantElec* poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, y));
    x += 45.72;
    if (x > 260) x = 25.4, y += 60.96;
    for (const auto& p : std::vector<SymbolePlace>(c.feuilles[0].symboles))
      if (p.uuid == s) {
        ComposantElec* r = c.composant(p.composant);
        r->reference = ref;
        if (!valeur.empty()) r->valeur = valeur;
        relier(p, liaisons);
        return c.composant(p.composant);
      }
    return nullptr;
  }
};
std::string dossierEssai(const std::string& nom) {
  std::random_device r;
  const fs::path p = fs::temp_directory_path() / ("promethee-essai-l15-" + nom + "-" + std::to_string(r()));
  fs::create_directories(p);
  return prom::utf8(p);
}
const char* CLIGNOTANT =
    "#include <avr/io.h>\n#include <util/delay.h>\nstatic void envoyer(const char* s) {\n  while (*s) {\n    while (!(UCSR0A & (1 << UDRE0))) {\n    }\n    UDR0 = *s++;\n  }\n}\n"
    "int main(void) {\n  DDRB |= (1 << PB5);\n  UBRR0H = 0;\n  UBRR0L = 103;\n  UCSR0B = (1 << TXEN0);\n  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);\n  envoyer(\"bonjour\\r\\n\");\n"
    "  for (;;) {\n    PORTB |= (1 << PB5);  /* LED allumée */\n    _delay_ms(500);\n    PORTB &= ~(1 << PB5);\n    _delay_ms(500);\n  }\n}\n";
const char* ROBOT =
    "#include <avr/io.h>\nstatic void envoyer(const char* s) {\n  while (*s) {\n    while (!(UCSR0A & (1 << UDRE0))) {\n    }\n    UDR0 = *s++;\n  }\n}\n"
    "int main(void) {\n  DDRD |= (1 << PD6) | (1 << PD5) | (1 << PD7);\n  DDRB |= (1 << PB0);\n  TCCR0A = (1 << COM0A1) | (1 << COM0B1) | (1 << WGM01) | (1 << WGM00);\n  TCCR0B = (1 << CS01);\n"
    "  UBRR0 = 51;\n  UCSR0B = (1 << TXEN0);\n  UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);\n  unsigned char barres = 0, surBarre = 0;\n"
    "  for (;;) {\n    const unsigned char c = PINC & 7, g = c & 1, m = (c >> 1) & 1, d = (c >> 2) & 1;\n"
    "    if (g && m && d) {\n      if (!surBarre) {\n        surBarre = 1;\n        if (++barres == 2) envoyer(\"tour fini\\r\\n\");\n      }\n    } else if (!g && !d)\n      surBarre = 0;\n"
    "    if (g && !d) {\n      OCR0A = 40;\n      OCR0B = 170;\n    } else if (d && !g) {\n      OCR0A = 170;\n      OCR0B = 40;\n    } else {\n      OCR0A = 150;\n      OCR0B = 150;\n    }\n  }\n}\n";
int ligneDe(const QString& texte, const QString& morceau) {
  const QStringList l = texte.split(QLatin1Char('\n'));
  for (int i = 0; i < l.size(); ++i)
    if (l[i].contains(morceau)) return i + 1;
  return -1;
}
// Fronts du courant de la LED (seuil de 1 mA) dans les échantillons du laboratoire
std::vector<double> frontsLed(FenetreLaboratoire* lab, const std::string& ref) {
  std::string v;
  for (const auto& [inst, r] : lab->netlist().instances)
    if (r == ref && !inst.empty() && inst[0] == 'd') v = "@" + inst + "[id]";
  std::vector<double> f;
  if (v.empty()) return f;
  const Releve r = lab->session().echantillons(v);
  for (std::size_t i = 1; i < r.size(); ++i)
    if ((r[i - 1].v > 1e-3) != (r[i].v > 1e-3)) f.push_back(r[i].t);
  return f;
}
double courantLed(FenetreLaboratoire* lab, const std::string& ref) {
  for (const auto& [inst, r] : lab->netlist().instances)
    if (r == ref && !inst.empty() && inst[0] == 'd') return lab->session().valeur("@" + inst + "[id]");
  return 0;
}
}  // namespace

void essaisElectronique034L15(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  const bool avr = !prom::programmeDuPoste("avr-gcc").empty();
  const bool simavr = emulateurDisponible("atmega328p");
  const bool ngspice = ngspiceDisponible();

  // ---------- Les montages ----------
  Montage cl("Clignotant");
  cl.poser("Pile", "BT1", "5V", {{"1", "+5V"}, {"2", "GND"}});
  cl.poser("ATmega328P", "U1", "ATmega328P", {{"7", "+5V"}, {"8", "GND"}, {"20", "+5V"}, {"22", "GND"}, {"19", "LED"}});
  cl.poser("R", "R1", "330", {{"1", "LED"}, {"2", "A"}});
  cl.poser("LED", "D1", "rouge", {{"2", "A"}, {"1", "GND"}});

  Montage cpt("Compteurs");
  cpt.poser("Pile", "BT1", "5V", {{"1", "VCC"}, {"2", "GND"}});
  cpt.poser("Source_tension", "VG1", "externe", {{"1", "CLK"}, {"2", "GND"}});
  cpt.poser("Compteur_4bits", "U1", "", {{"1", "CLK"}, {"2", "GND"}, {"3", "Q0"}, {"4", "Q1"}, {"5", "Q2"}, {"6", "Q3"}, {"7", "VCC"}, {"8", "GND"}});
  const std::string verilog = "module compteur(input clk, input raz, output reg [3:0] q);\n  initial q = 0;\n  always @(posedge clk or posedge raz)\n    if (raz) q <= 0;\n    else q <= q + 1;\nendmodule\n";
  const auto mod = lireModuleHdl(verilog, "verilog");
  if (mod) {
    const std::string u = poserComposantHdl(cpt.c, *mod, 0, ptMm(150, 120), "U3");
    for (const auto& p : std::vector<SymbolePlace>(cpt.c.feuilles[0].symboles))
      if (p.composant == u) cpt.relier(p, {{"clk", "CLK"}, {"raz", "GND"}, {"q0", "H0"}, {"q1", "H1"}, {"q2", "H2"}, {"q3", "H3"}, {"VCC", "VCC"}, {"GND", "GND"}});
  }

  Montage add("Additionneur");
  add.poser("Porte_OU_X", "U1", "", {{"1", "A"}, {"2", "B"}, {"3", "T1"}});
  add.poser("Porte_OU_X", "U2", "", {{"1", "T1"}, {"2", "CIN"}, {"3", "S"}});
  add.poser("Porte_ET", "U3", "", {{"1", "A"}, {"2", "B"}, {"3", "T2"}});
  add.poser("Porte_ET", "U4", "", {{"1", "T1"}, {"2", "CIN"}, {"3", "T3"}});
  add.poser("Porte_OU", "U5", "", {{"1", "T2"}, {"2", "T3"}, {"3", "COUT"}});

  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = "Embarqué";
  p.electronique = {cl.c, cpt.c, add.c};
  enum { CLIGNOTANT_, COMPTEURS, ADDITIONNEUR };
  doc->creer(p);
  f.montrerElectronique(CLIGNOTANT_, AtelierElectronique::Schema);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) {
    verifier(false, "034 L15 : l'atelier électronique s'ouvre", {});
    return;
  }
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);

  // ---------- 15.3 · Écart montré sur le schéma, programme rangé dans le projet ----------
  a->executerAction("programme.ouvrir");
  attendre(250);
  FenetreEmbarque* e = a->embarque();
  auto* onglets = e ? e->findChild<QTabWidget*>(QStringLiteral("ongletsEmbarque")) : nullptr;
  verifier(e && e->isVisible() && onglets && onglets->currentWidget()->objectName() == QLatin1String("programme"), "034 L15 : « Programme… » du ruban ouvre l'onglet programme", {});
  if (!e) return;
  e->nouveauProgramme(QStringLiteral("essai"), QStringLiteral("arduino"), QStringLiteral("U1"));
  verifier(e->editeur()->toPlainText().contains(QStringLiteral("pinMode(13, OUTPUT)")), "034 L15 · 15.3 : le programme neuf porte l'initialisation écrite d'après le câblage (LED sur D13)",
           e->editeur()->toPlainText().toStdString());
  e->editeur()->setPlainText(QStringLiteral("void setup() {\n  pinMode(13, INPUT);\n}\nvoid loop() {\n}\n"));
  const auto& ecarts = e->ecarts();
  int iLed = -1;
  for (std::size_t i = 0; i < ecarts.size(); ++i)
    if (ecarts[i].genre == "entrée vers une charge" && ecarts[i].numero == "19") iLed = static_cast<int>(i);
  auto* liste = e->findChild<QListWidget*>(QStringLiteral("ecartsSchema"));
  bool montree = false;
  if (iLed >= 0 && liste && liste->item(iLed)) {
    a->vueSchema()->selectionner({});
    const QRect r = liste->visualItemRect(liste->item(iLed));
    reel::cliquer(liste->viewport(), r.center());
    attendre(150);
    const ConceptionElec* c = a->conceptionCourante();
    for (const auto& sp : c->feuilles[0].symboles)
      if (sp.composant == c->parReference("U1")->uuid) montree = a->vueSchema()->selection().count(sp.uuid) > 0;
  }
  verifier(iLed >= 0 && montree && e->editeur()->textCursor().blockNumber() == 1,
           "034 L15 · 15.3 : la broche D13 déclarée en entrée et câblée à la LED D1 est listée ; un clic choisit le microcontrôleur sur le schéma et mène à la ligne",
           iLed >= 0 ? ecarts[static_cast<std::size_t>(iLed)].message : std::string("aucun écart"));
  QString erreur;
  const bool enregistre = e->enregistrer(&erreur);
  const auto programmes = programmesDe(*a->conceptionCourante());
  verifier(enregistre && programmes.size() == 1 && programmes[0].chemin == "programmes/essai.ino" && programmes[0].sha256.size() == 64,
           "034 L15 · 15.3 : le programme est rangé dans le projet, cité par son chemin relatif et son SHA-256", erreur.toStdString());

  // ---------- 15.4 et 15.5 · Co-simulation et mise au point ----------
  if (!avr || !simavr || !ngspice)
    verifier(true, "034 L15 · 15.4 et 15.5 : co-simulation non essayée (avr-gcc, simavr ou ngspice absent)", {});
  else {
    e->nouveauProgramme(QStringLiteral("clignotant"), QStringLiteral("c"), QStringLiteral("U1"));
    e->editeur()->setPlainText(QString::fromUtf8(CLIGNOTANT));
    const bool compile = e->compiler();
    e->montrer(QStringLiteral("cosimulation"));
    const bool demarre = compile && e->demarrerCosimulation(&erreur);
    FenetreLaboratoire* lab = a->laboratoire();
    lab->setVitesse(4);
    const bool avance = demarre && attendreQue([&] { return lab->session().temps() > 1.15; }, 60000);
    e->rafraichir();
    const auto fronts = frontsLed(lab, "D1");
    bool demiSecondes = fronts.size() >= 3;
    for (std::size_t i = 1; i < fronts.size(); ++i) demiSecondes &= std::abs(fronts[i] - fronts[i - 1] - 0.5) < 0.005;
    verifier(avance && demiSecondes, "034 L15 · 15.4 : le clignotant tourne sur simavr avec le circuit : la LED D1 change d'état toutes les 500 ms de temps simulé",
             std::to_string(fronts.size()) + " front(s) ; " + erreur.toStdString());
    e->reglerTerminal(9600);
    e->rafraichir();
    const bool bonjour = e->terminal().startsWith(QStringLiteral("bonjour"));
    e->reglerTerminal(4800);
    const bool brouille = !e->terminal().contains(QStringLiteral("bonjour"));
    e->reglerTerminal(9600);
    verifier(bonjour && brouille, "034 L15 · 15.4 : « bonjour » arrive au terminal réglé à 9 600 bauds ; à 4 800 bauds, il est illisible", e->terminal().toStdString());
    e->arreterCosimulation();
    attendre(100);
    // 15.5 : point d'arrêt sur la ligne qui allume la LED
    e->montrer(QStringLiteral("programme"));
    const int ligne = ligneDe(e->editeur()->toPlainText(), QStringLiteral("LED allumée"));
    QTextCursor curseur(e->editeur()->document()->findBlockByNumber(ligne - 1));
    e->editeur()->setTextCursor(curseur);
    e->montrer(QStringLiteral("cosimulation"));
    if (auto* b = e->findChild<QPushButton*>(QStringLiteral("pointArretLigne"))) reel::cliquer(b, b->rect().center());
    attendre(50);
    const bool point = e->points().count(ligne) > 0;
    const bool redemarre = e->demarrerCosimulation(&erreur);
    const bool arrete = redemarre && attendreQue([&] { return e->micro() && e->micro()->arrete(); }, 60000);
    e->rafraichir();
    const bool eteinte = arrete && e->micro()->emulateur().broche("PB5") == EtatBroche::Bas && courantLed(lab, "D1") < 1e-4;
    const auto* etat = e->findChild<QLabel*>(QStringLiteral("etatDebogueur"));
    const bool ligneMontree = etat && etat->text().contains(QStringLiteral("ligne %1").arg(ligne));
    verifier(point && arrete && eteinte && ligneMontree, "034 L15 · 15.5 : le point d'arrêt sur la ligne qui allume la LED arrête le programme et le circuit, LED éteinte",
             etat ? etat->text().toStdString() : std::string());
    if (auto* b = e->findChild<QPushButton*>(QStringLiteral("pasLigne"))) reel::cliquer(b, b->rect().center());
    const bool repris = attendreQue([&] { return e->micro()->emulateur().broche("PB5") == EtatBroche::Haut && e->micro()->arrete(); }, 30000);
    attendre(100);
    const double i = courantLed(lab, "D1");
    e->rafraichir();
    auto* reg = e->findChild<QTableWidget*>(QStringLiteral("registresMicro"));
    verifier(repris && i > 5e-3 && reg && reg->item(34, 1), "034 L15 · 15.5 : un pas plus loin, la LED s'allume ; registres et compteur ordinal affichés",
             std::to_string(i * 1000) + " mA");
    e->continuer();
    attendre(100);
    e->arreterCosimulation();
    attendre(100);
  }

  // ---------- 15.6 · BME280 posé, organigramme, croquis compilé ----------
  {
    e->montrer(QStringLiteral("organigramme"));
    const std::size_t avant = a->conceptionCourante()->composants.size();
    const int pas0 = doc->positionHistorique();
    const bool pose = e->poserModule(QStringLiteral("bme280"));
    const std::size_t apres = a->conceptionCourante()->composants.size();
    verifier(pose && apres == avant + 3 && doc->positionHistorique() == pas0 + 1, "034 L15 · 15.6 : le BME280 de la galerie est posé et relié en I2C, ses tirages compris, en un pas",
             std::to_string(apres - avant) + " composant(s)");
    const QStringList prop = e->propositions(QStringLiteral("temp"));
    e->exempleOrganigramme();
    const CodeOrganigramme code = e->convertir(false);
    const bool compile = !avr || e->compilerOrganigramme();
    verifier(prop.contains(QStringLiteral("lire la température")) && code.ok && code.code.find("bme.readTemperature()") != std::string::npos && compile,
             "034 L15 · 15.6 : le bloc « lire la température » est proposé ; l'organigramme devient un croquis Arduino qui compile",
             e->compilation().journal.substr(0, 300));
  }

  // ---------- 15.7 · Compteur Verilog co-simulé contre le compteur XSPICE ----------
  if (!ngspice || outilHdl("verilog").empty())
    verifier(true, "034 L15 · 15.7 : co-simulation HDL non essayée (ngspice ou simulateur Verilog absent)", {});
  else {
    a->ouvrir(COMPTEURS, AtelierElectronique::Schema);
    attendre(250);
    FenetreLaboratoire* lab = a->laboratoire(QStringLiteral("continu"));
    lab->montrer(QStringLiteral("continu"));
    lab->setPas(1e-4);
    lab->setVitesse(0.5);
    if (auto* fr = lab->frequence(QStringLiteral("VG1"))) fr->setValue(50);
    if (auto* am = lab->findChild<QDoubleSpinBox*>(QStringLiteral("amplitude_VG1"))) am->setValue(2.5);
    if (auto* fo = lab->findChild<QComboBox*>(QStringLiteral("forme_VG1"))) fo->setCurrentIndex(1);  // carré
    // Le générateur du laboratoire est centré sur 0 : l'horloge part du niveau bas, réglée ici pour 0 à 5 V
    Generateur g;
    g.forme = Generateur::Forme::Carre;
    g.frequence = 50;
    g.amplitude = -2.5;
    g.decalage = 2.5;
    lab->session().regler(sourceExterne("VG1"), g);
    const bool demarre = e->cosimulerHdl(QStringLiteral("U3"), &erreur);
    lab->session().regler(sourceExterne("VG1"), g);
    const bool avance = demarre && attendreQue([&] { return lab->session().temps() > 0.25; }, 90000);
    lab->arreter();
    std::vector<Releve> rq, rh;
    for (const char* r : {"Q0", "Q1", "Q2", "Q3"}) rq.push_back(lab->session().echantillons(vecteurTension(lab->netlist(), r)));
    for (const char* r : {"H0", "H1", "H2", "H3"}) rh.push_back(lab->session().echantillons(vecteurTension(lab->netlist(), r)));
    int egaux = 0, derniere = 0;
    for (int k = 0; k < 12; ++k) {
      const double t = k * 0.02 + 0.015;
      std::vector<double> vq, vh;
      for (const auto& r : rq) vq.push_back(valeurA(r, t));
      for (const auto& r : rh) vh.push_back(valeurA(r, t));
      egaux += valeurBinaire(vq) == valeurBinaire(vh);
      derniere = valeurBinaire(vh);
    }
    verifier(avance && egaux == 12 && derniere >= 10, "034 L15 · 15.7 : le compteur écrit en Verilog et câblé au schéma donne les mêmes sorties que le compteur XSPICE",
             std::to_string(egaux) + "/12 échantillons égaux, dernier " + std::to_string(derniere) + " ; " + erreur.toStdString());
    // Poser un composant HDL depuis l'onglet : un pas
    e->montrer(QStringLiteral("hdl"));
    const int pas0 = doc->positionHistorique();
    const bool pose = e->poserHdl(QString::fromStdString(verilog), QStringLiteral("verilog"));
    verifier(pose && doc->positionHistorique() == pas0 + 1, "034 L15 · 15.7 : un composant décrit en Verilog se pose au schéma en un pas", {});
  }

  // ---------- 15.8 · Robot sur la piste ovale, commande sans écran ----------
  if (!avr || !simavr)
    verifier(true, "034 L15 · 15.8 : robot non essayé (avr-gcc ou simavr absent)", {});
  else {
    e->montrer(QStringLiteral("programme"));
    e->nouveauProgramme(QStringLiteral("robot"), QStringLiteral("c"), QStringLiteral("U1"));
    e->editeur()->setPlainText(QString::fromUtf8(ROBOT));
    e->setFrequence(8e6);
    const bool compile = e->compiler();
    e->montrer(QStringLiteral("robot"));
    const bool tour = compile && e->simulerRobot(40, QStringLiteral("tour fini"));
    verifier(tour && !e->vueRobot()->trajet().empty() && e->resultatRobot().distance > 1800, "034 L15 · 15.8 : le suiveur de ligne fait le tour de la piste ovale de 2 m en simulation",
             std::to_string(e->resultatRobot().distance) + " mm");
    QProcess commande;
    commande.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--robot"), QString::fromStdString(e->compilation().elf), QStringLiteral("--attendre"), QStringLiteral("tour fini"),
                                                             QStringLiteral("--duree"), QStringLiteral("40"), QStringLiteral("--frequence"), QStringLiteral("8000000")});
    const bool fini = commande.waitForFinished(120000);
    verifier(fini && commande.exitStatus() == QProcess::NormalExit && commande.exitCode() == 0 && commande.readAllStandardOutput().contains("attendu reçu"),
             "034 L15 · 15.8 : la commande « Promethee --robot » sans écran rend 0 quand « tour fini » arrive sur la liaison série", std::to_string(commande.exitCode()));
    e->setFrequence(16e6);
  }

  // ---------- 15.10 · Feux tricolores ----------
  {
    e->montrer(QStringLiteral("machine"));
    e->exempleFeux();
    const int accord = e->comparerMachine(24);
    const bool ghdl = !prom::programmeDuPoste("ghdl").empty();
    verifier(accord == 24 || (!ghdl && accord == -1), "034 L15 · 15.10 : les feux tricolores donnent la même séquence en machine à états, en circuit à bascules et en VHDL simulé par GHDL",
             std::to_string(accord) + "/24");
    const std::size_t avant = doc->projet().electronique.size();
    e->poserBascules();
    verifier(doc->projet().electronique.size() == avant + 1, "034 L15 · 15.10 : le circuit à bascules est posé dans une conception du projet", {});
  }

  // ---------- 15.12 · Additionneur en VHDL, GHDL, JEDEC ----------
  {
    a->ouvrir(ADDITIONNEUR, AtelierElectronique::Schema);
    attendre(250);
    e->montrer(QStringLiteral("logique"));
    const bool vhdl = e->vhdlLogique().contains(QStringLiteral("entity circuit is"));
    QString detail;
    const bool ghdl = prom::programmeDuPoste("ghdl").empty() || e->verifierTableGhdl(&detail);
    const QString jedec = QString::fromStdString(dossierEssai("jedec")) + QStringLiteral("/additionneur.jed");
    const bool ecrit = e->exporterJedec(jedec);
    std::string texte;
    try {
      texte = prom::lireFichier(jedec.toStdString());
    } catch (...) {
    }
    const auto fus = lireJedec(texte);
    verifier(vhdl && ghdl && ecrit && fus && fus->sommeJuste, "034 L15 · 15.12 : l'additionneur complet exporté en VHDL et simulé par GHDL a la table de vérité du schéma ; son JEDEC du GAL16V8 se relit",
             detail.toStdString().substr(0, 400));
  }
  e->close();
  attendre(50);
}
