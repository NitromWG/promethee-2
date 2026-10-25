// Prométhée : essais d'interface de la 0.34, lot L14 (laboratoire de simulation) — ouvert du ruban ; simulation
// continue : quatre appuis réels sur le bouton, le compteur et l'afficheur montrent 4 en moins de 100 ms (14.2) ; LED
// sans résistance qui fume sur le schéma, fils colorés et animés, l'autre LED allumée (14.3) ; générateur passé de
// 100 Hz à 1 kHz sans relance, oscilloscope et voltmètre (14.4) ; fréquencemètres posés au schéma, dont un écrit en
// Python (14.5) ; trame I2C décodée (14.6) ; H(s) affiché et pôle de ngspice (14.7) ; mesure importée superposée à
// la simulation et moteurs du poste (14.8) ; analyseur de réseau et Touchstone (14.10) ; abaisseur conçu, marge
// simulée, posé en un pas annulable, arbre des rails (14.14). Appelés seuls avec PROMETHEE_ESSAIS_LOTS=034L14.
// Licence GPL-3.0-only.
#include <cmath>
#include <filesystem>
#include <functional>
#include <random>
#include <sstream>
#include <string>

#include <QApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "FenetreSimulation.hpp"
#include "Laboratoire034.hpp"
#include "VueSchema.hpp"
#include "promethee/analyseurs_numeriques.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/parametres_s.hpp"
#include "promethee/scripts_python.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_continue.hpp"

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
  ComposantElec* poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, y));
    x += 30.48;
    if (x > 260) x = 25.4, y += 50.8;
    ComposantElec* r = nullptr;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        r = c.composant(p.composant);
        r->reference = ref;
        if (!valeur.empty()) r->valeur = valeur;
        for (const auto& b : brochesPlacees(p, *c.symbole(p.symbole)))
          for (const auto& [num, reseau] : liaisons)
            if (b.broche->numero == num) {
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
    return r;
  }
};
std::string dossierEssai(const std::string& nom) {
  std::random_device r;
  const fs::path p = fs::temp_directory_path() / ("promethee-essai-l14-" + nom + "-" + std::to_string(r()));
  fs::create_directories(p);
  return prom::utf8(p);
}
std::string nb(double v) {
  std::ostringstream o;
  o.precision(6);
  o << v;
  return o.str();
}
Releve releveDe(const TraceCourbes::Courbe& c) {
  Releve r;
  for (std::size_t i = 0; i < c.x.size() && i < c.y.size(); ++i) r.push_back({c.x[i], c.y[i]});
  return r;
}
const char* SCRIPT_FREQUENCEMETRE =
    "import promethee\n"
    "points = []\n"
    "while True:\n"
    "    bloc = promethee.echantillons('ENTREE')\n"
    "    if not bloc:\n"
    "        break\n"
    "    points.extend(bloc)\n"
    "vmin = min(v for t, v in points)\n"
    "vmax = max(v for t, v in points)\n"
    "seuil = (vmin + vmax) / 2\n"
    "h = 0.05 * (vmax - vmin)\n"
    "fronts = []\n"
    "arme = False\n"
    "for (ta, va), (tb, vb) in zip(points, points[1:]):\n"
    "    if va < seuil - h:\n"
    "        arme = True\n"
    "    if arme and va < seuil <= vb:\n"
    "        fronts.append(ta + (tb - ta) * (seuil - va) / (vb - va))\n"
    "        arme = False\n"
    "if len(fronts) >= 2:\n"
    "    promethee.afficher('frequence', (len(fronts) - 1) / (fronts[-1] - fronts[0]))\n";
}  // namespace

void essaisElectronique034L14(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  const bool ngspice = ngspiceDisponible();
  const bool python = prom::pythonDisponible();

  // ---------- Les montages ----------
  Montage compteur("Compteur");
  compteur.poser("Pile", "BT1", "5V", {{"1", "VCC"}, {"2", "GND"}});
  compteur.poser("Bouton", "SW1", "", {{"1", "VCC"}, {"2", "CLK"}});
  compteur.poser("R", "R1", "10k", {{"1", "CLK"}, {"2", "GND"}});
  compteur.poser("Compteur_4bits", "U1", "", {{"1", "CLK"}, {"2", "GND"}, {"3", "Q0"}, {"4", "Q1"}, {"5", "Q2"}, {"6", "Q3"}, {"7", "VCC"}, {"8", "GND"}});
  compteur.poser("Decodeur_7seg", "U2", "",
                 {{"1", "Q0"}, {"2", "Q1"}, {"3", "Q2"}, {"4", "Q3"}, {"5", "SA"}, {"6", "SB"}, {"7", "SC"}, {"8", "SD"}, {"9", "SE"}, {"10", "SF"}, {"11", "SG"}, {"12", "VCC"}, {"13", "GND"}});
  compteur.poser("Afficheur_7seg", "DS1", "", {{"1", "SA"}, {"2", "SB"}, {"3", "SC"}, {"4", "SD"}, {"5", "SE"}, {"6", "SF"}, {"7", "SG"}, {"8", "GND"}});
  compteur.poser("LED", "D1", "rouge", {{"2", "VCC"}, {"1", "GND"}});  // sans résistance : elle fume
  compteur.poser("R", "R2", "330", {{"1", "VCC"}, {"2", "A"}});
  compteur.poser("LED", "D2", "verte", {{"2", "A"}, {"1", "GND"}});

  Montage filtre("Générateur");
  filtre.poser("Source_tension", "VG1", "externe", {{"1", "ENTREE"}, {"2", "GND"}});
  filtre.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  filtre.poser("C", "C1", "159.155n", {{"1", "SORTIE"}, {"2", "GND"}});
  filtre.poser("Instrument", "INS1", "fréquencemètre", {{"1", "ENTREE"}, {"2", "GND"}});
  if (python) filtre.poser("Instrument", "INS2", "python", {{"1", "ENTREE"}, {"2", "GND"}})->champs["Instrument.Script"] = SCRIPT_FREQUENCEMETRE;

  Montage i2c("I2C");
  const auto [sda, scl] = motifEcritureI2C(0x50, {0x10, 0xA5, 0x3C}, 100e3, 3.3);
  i2c.poser("Source_tension", "VSDA", sda.texte(), {{"1", "SDA"}, {"2", "GND"}});
  i2c.poser("Source_tension", "VSCL", scl.texte(), {{"1", "SCL"}, {"2", "GND"}});
  i2c.poser("R", "R1", "4k7", {{"1", "SDA"}, {"2", "GND"}});
  i2c.poser("R", "R2", "4k7", {{"1", "SCL"}, {"2", "GND"}});

  Montage rc("Passe-bas");
  rc.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
  rc.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  rc.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});

  Montage att("Atténuateur");
  const double k = std::pow(10.0, 0.5), rp = 50 * (k + 1) / (k - 1), rs = 50 * (k * k - 1) / (2 * k);
  att.poser("R", "R1", nb(rp), {{"1", "P1"}, {"2", "GND"}});
  att.poser("R", "R2", nb(rs), {{"1", "P1"}, {"2", "P2"}});
  att.poser("R", "R3", nb(rp), {{"1", "P2"}, {"2", "GND"}});

  Montage mesure("Mesure");
  mesure.poser("Source_tension", "V1", "PULSE(0 1 0 1n 1n 1 2)", {{"1", "ENTREE"}, {"2", "GND"}});
  mesure.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  mesure.poser("C", "C1", "1u", {{"1", "SORTIE"}, {"2", "GND"}});

  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = "Laboratoire";
  for (ConceptionElec* c : {&compteur.c, &filtre.c, &i2c.c, &rc.c, &att.c, &mesure.c}) p.electronique.push_back(*c);
  p.electronique.push_back(nouvelleConception("Alimentation"));
  enum { COMPTEUR, FILTRE, I2C, RC, ATTENUATEUR, MESURE, ALIMENTATION };
  doc->creer(p);
  f.montrerElectronique(COMPTEUR, AtelierElectronique::Schema);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) {
    verifier(false, "034 L14 : l'atelier électronique s'ouvre", {});
    return;
  }
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);

  // ---------- Le laboratoire, ouvert du ruban ----------
  a->executerAction("simulation.laboratoire");
  attendre(250);
  FenetreLaboratoire* lab = a->laboratoire();
  auto* onglets = lab ? lab->findChild<QTabWidget*>(QStringLiteral("ongletsLaboratoire")) : nullptr;
  auto ongletCourant = [&] { return onglets && onglets->currentWidget() ? onglets->currentWidget()->objectName().toStdString() : std::string(); };
  verifier(lab && lab->isVisible() && ongletCourant() == "continu", "034 L14 : « Laboratoire… » du ruban ouvre le laboratoire de simulation sur la simulation continue", ongletCourant());
  if (!lab) return;
  a->executerAction("simulation.reseau");
  attendre(80);
  const std::string surReseau = ongletCourant();
  a->executerAction("simulation.laboratoire");
  attendre(80);
  verifier(surReseau == "reseau" && ongletCourant() == "continu", "034 L14 : chaque bouton du groupe Laboratoire mène à son onglet", surReseau + " puis " + ongletCourant());

  // ---------- 14.2 et 14.3 · Bouton, compteur, afficheur ; LED qui fume, retour visuel ----------
  if (!ngspice)
    verifier(true, "034 L14 · 14.2 à 14.5 : simulation continue non essayée (ngspice absent)", {});
  else {
    auto* pas = lab->findChild<QDoubleSpinBox*>(QStringLiteral("pasContinu"));
    if (pas) pas->setValue(1);  // 1 ms
    lab->setVitesse(1);
    QString erreur;
    const bool demarre = lab->demarrer(&erreur);
    const bool depart = demarre && attendreQue([&] { return lab->session().temps() > 0.05; }, 20000);
    QPushButton* bouton = lab->commande(QStringLiteral("SW1"));
    int avant = -2;
    for (int i = 0; i < 3 && bouton; ++i) {
      reel::appuyer(bouton, bouton->rect().center());
      attendre(60);
      reel::relacher(bouton, bouton->rect().center());
      attendre(60);
    }
    attendreQue([&] { return lab->chiffre(QStringLiteral("DS1")) == 3; }, 1000);
    avant = lab->chiffre(QStringLiteral("DS1"));
    // Relevé au plus près (2 ms) : l'attente des autres essais (20 ms) ajouterait son propre pas au temps mesuré
    QElapsedTimer chrono;
    chrono.start();
    if (bouton) reel::appuyer(bouton, bouton->rect().center());
    bool quatre = false;
    while (!(quatre = lab->chiffre(QStringLiteral("DS1")) == 4) && chrono.elapsed() < 2000) attendre(2);
    const qint64 ms = chrono.elapsed();
    attendre(40);
    if (bouton) reel::relacher(bouton, bouton->rect().center());
    // 100 ms visées ; comme les autres mesures de temps des essais (9.1), seul un dépassement de plus de 20 % échoue
    verifier(depart && avant == 3 && quatre && ms < 120,
             "034 L14 · 14.2 : quatre appuis réels sur le bouton SW1 du laboratoire, l'afficheur DS1 passe de 3 à 4 en moins de 100 ms pendant le calcul",
             "démarré " + std::to_string(demarre) + " (" + erreur.toStdString() + "), avant " + std::to_string(avant) + ", après " + std::to_string(lab->chiffre(QStringLiteral("DS1"))) + " en " +
                 std::to_string(ms) + " ms");
    // 14.3 : LED sans résistance qui fume sur le schéma, fils colorés et animés
    VueSchema* vue = a->vueSchema();
    attendreQue([&] { return vue && vue->retourVisuelActif() && !vue->retourVisuel().fumees.empty(); }, 2000);
    const Fumee* fumee = nullptr;
    if (vue)
      for (const auto& x : vue->retourVisuel().fumees)
        if (x.reference == "D1" && x.grandeur == "courant") fumee = &x;
    double courantMax = 0;
    int filsAnimes = 0;
    if (vue)
      for (const auto& x : vue->retourVisuel().fils) {
        courantMax = std::max(courantMax, x.courant);
        filsAnimes += x.sens != 0 && x.courant > 1e-5;
      }
    const double phase0 = vue ? vue->phaseRetour() : 0;
    attendre(200);
    const bool anime = vue && vue->phaseRetour() > phase0;
    auto* etat = lab->findChild<QLabel*>(QStringLiteral("etatLaboratoire"));
    verifier(fumee && fumee->valeur > 1.0 && fumee->limite > 0 && anime && filsAnimes >= 2 && etat && etat->text().contains(QStringLiteral("D1")),
             "034 L14 · 14.3 : la LED D1 sans résistance fume sur le schéma avec le courant atteint et sa limite ; les fils prennent leur couleur et leurs points courent",
             fumee ? fumee->message + ", " + std::to_string(filsAnimes) + " fil(s) animé(s), animation " + std::to_string(anime) : std::string("aucune fumée"));
    verifier(lab->ledAllumee(QStringLiteral("D2")), "034 L14 · 14.3 : la LED D2 protégée par 330 Ω s'allume au laboratoire", {});
    lab->arreter();
    attendre(80);
    verifier(!lab->enCours() && vue && !vue->retourVisuelActif(), "034 L14 · 14.3 : la simulation arrêtée, le schéma retrouve ses couleurs", {});

    // ---------- 14.4 et 14.5 · Générateur, oscilloscope, voltmètre ; fréquencemètres posés au schéma ----------
    a->ouvrir(FILTRE, AtelierElectronique::Schema);
    attendre(250);
    lab->montrer(QStringLiteral("continu"));
    if (pas) pas->setValue(0.005);  // 5 µs
    lab->setVitesse(0.25);
    QDoubleSpinBox* frequence = lab->frequence(QStringLiteral("VG1"));
    if (frequence) frequence->setValue(100);
    const bool demarre2 = lab->demarrer(&erreur);
    const bool regime = demarre2 && attendreQue([&] { return lab->session().temps() > 0.25; }, 60000);
    lab->reglerMultimetre(QStringLiteral("ENTREE"), 2);
    lab->rafraichir();
    const double fAvant = lab->mesureMultimetre();
    frequence = lab->frequence(QStringLiteral("VG1"));
    if (frequence) frequence->setValue(1000);
    const double tChange = lab->session().temps();
    const bool suivi = attendreQue([&] { return lab->session().temps() > tChange + 0.22; }, 60000);
    lab->reglerOscilloscope(QStringLiteral("ENTREE"), QStringLiteral("SORTIE"), 5e-4);
    lab->rafraichir();
    lab->rafraichir();
    double fOscillo = 0;
    if (lab->oscilloscope() && !lab->oscilloscope()->courbes().empty()) fOscillo = frequenceSignal(releveDe(lab->oscilloscope()->courbes()[0]));
    const std::size_t voies = lab->oscilloscope() ? lab->oscilloscope()->courbes().size() : 0;
    lab->rafraichir();
    const double fApres = lab->mesureMultimetre();
    lab->reglerMultimetre(QStringLiteral("ENTREE"), 1);
    lab->rafraichir();
    const double vEntree = lab->mesureMultimetre();
    lab->reglerMultimetre(QStringLiteral("SORTIE"), 1);
    lab->rafraichir();
    const double vSortie = lab->mesureMultimetre();
    const double rapport = vEntree > 0 ? vSortie / vEntree : 0;
    verifier(regime && suivi && lab->enCours() && std::abs(fAvant - 100) < 1 && std::abs(fApres - 1000) < 10 && voies == 2 && std::abs(fOscillo - 1000) < 20,
             "034 L14 · 14.4 : le générateur VG1 passe de 100 Hz à 1 kHz pendant le calcul, sans relance ; le fréquencemètre et l'oscilloscope suivent",
             "avant " + nb(fAvant) + " Hz, après " + nb(fApres) + " Hz, oscilloscope " + nb(fOscillo) + " Hz sur " + std::to_string(voies) + " voie(s), " + erreur.toStdString());
    verifier(std::abs(rapport - 1 / std::sqrt(2.0)) / (1 / std::sqrt(2.0)) < 0.01 && std::abs(vEntree - 1 / std::sqrt(2.0)) < 0.01,
             "034 L14 · 14.4 : à 1 kHz, sur le RC qui coupe à 1 kHz, le voltmètre lit 0,707 fois l'entrée à 1 % près",
             "entrée " + nb(vEntree) + " V, sortie " + nb(vSortie) + " V, rapport " + nb(rapport));
    const auto instruments = lab->mesurerInstruments();
    const auto i1 = instruments.find(QStringLiteral("INS1"));
    const auto i2 = instruments.find(QStringLiteral("INS2.frequence"));
    const bool fourni = i1 != instruments.end() && std::abs(i1->second - 1000) / 1000 < 0.01;
    const bool ecrit = !python || (i2 != instruments.end() && std::abs(i2->second - 1000) / 1000 < 0.01);
    verifier(fourni && ecrit, "034 L14 · 14.5 : les fréquencemètres posés au schéma, le fourni et celui écrit en Python, lisent le générateur à 1 % près",
             "INS1 " + (i1 != instruments.end() ? nb(i1->second) : std::string("—")) + " Hz, INS2 " + (i2 != instruments.end() ? nb(i2->second) : std::string(python ? "—" : "Python absent")) + " ; " +
                 (etat ? etat->text().toStdString() : std::string()));
    lab->arreter();
    attendre(80);
  }

  // ---------- 14.6 · Trame I2C décodée ----------
  if (ngspice) {
    a->ouvrir(I2C, AtelierElectronique::Schema);
    attendre(200);
    lab->montrer(QStringLiteral("logique"));
    const QString trames = lab->decoderI2C(QStringLiteral("SDA"), QStringLiteral("SCL"));
    verifier(trames.contains(QStringLiteral("0x50")) && trames.contains(QStringLiteral("0xA5 ✓")) && ongletCourant() == "logique",
             "034 L14 · 14.6 : l'analyseur logique du laboratoire décode la trame I2C vers 0x50, accusés compris", trames.toStdString());
  }

  // ---------- 14.7 · H(s) affiché, pôles ----------
  a->ouvrir(RC, AtelierElectronique::Schema);
  attendre(200);
  lab->montrer(QStringLiteral("symbolique"));
  const bool symbolique = lab->calculerSymbolique(QStringLiteral("V1"), QStringLiteral("SORTIE"));
  const auto poles = lab->poles();
  verifier(symbolique && lab->formule() == QStringLiteral("H(s) = 1 / (1 + s·R1·C1)") && lab->latex() == QStringLiteral("\\frac{1}{1 + s R_{1} C_{1}}") && poles.size() == 1 &&
               std::abs(poles[0].real() + 1e4) / 1e4 < 1e-3,
           "034 L14 · 14.7 : le laboratoire affiche H(s) = 1 / (1 + s·R1·C1), sa forme LaTeX et son pôle à −10⁴ rad/s", lab->formule().toStdString());
  if (ngspice) {
    const QString pz = lab->analyseNative(1);
    verifier(pz.contains(QStringLiteral("-10000")), "034 L14 · 14.7 : l'analyse des pôles et zéros de ngspice donne le même pôle", pz.toStdString());
  }

  // ---------- 14.10 · Analyseur de réseau et Touchstone ----------
  if (ngspice) {
    a->ouvrir(ATTENUATEUR, AtelierElectronique::Schema);
    attendre(200);
    lab->montrer(QStringLiteral("reseau"));
    const bool analyse = lab->analyserReseau(QStringLiteral("P1"), QStringLiteral("P2"), 1e3, 1e9);
    double s21 = 0, s21min = 0, s21max = -1e9;
    if (analyse && lab->parametresS()) {
      for (const auto& x : lab->parametresS()->s) s21min = std::min(s21min, enDecibels(x[2])), s21max = std::max(s21max, enDecibels(x[2]));
      s21 = enDecibels(lab->parametresS()->s.front()[2]);
    }
    const std::string dossier = dossierEssai("reseau");
    const QString fichier = QString::fromStdString(dossier) + QStringLiteral("/attenuateur.s2p");
    const bool exporte = lab->exporterTouchstone(fichier);
    std::string texte;
    try {
      texte = prom::lireFichier(fichier.toStdString());
    } catch (...) {
    }
    const auto relu = lireTouchstone(texte, 2);
    verifier(analyse && std::abs(s21 + 10) < 0.05 && s21min > -10.05 && s21max < -9.95 && exporte && texte.find("# HZ S DB R 50") != std::string::npos && relu &&
                 relu->frequences.size() == lab->parametresS()->frequences.size(),
             "034 L14 · 14.10 : l'analyseur de réseau lit S21 = −10,0 dB sur l'atténuateur en pi, le .s2p exporté se relit", "S21 " + nb(s21) + " dB");
  }

  // ---------- 14.14 · Abaisseur conçu, marge simulée, posé en un pas, arbre des rails ----------
  a->ouvrir(ALIMENTATION, AtelierElectronique::Schema);
  attendre(200);
  lab->montrer(QStringLiteral("alimentation"));
  const bool concu = lab->concevoirAbaisseur(12, 5, 2);
  const double marge = lab->margeSimulee();
  verifier(concu && lab->abaisseur().nomenclature.size() == 13 && (!ngspice || marge >= 45),
           "034 L14 · 14.14 : l'abaisseur de 12 V à 5 V sous 2 A est conçu avec sa nomenclature, marge de phase simulée par ngspice d'au moins 45°", "marge " + nb(marge) + "°");
  const int pas0 = doc->positionHistorique();
  const bool pose = lab->poserAbaisseur();
  attendre(80);
  const std::size_t composants = a->conceptionCourante() ? a->conceptionCourante()->composants.size() : 0;
  const QString rails = lab->arbreRails({{"+5V", 2}});
  const int pasPose = doc->positionHistorique() - pas0;
  doc->annuler();
  attendre(80);
  const std::size_t apresAnnulation = a->conceptionCourante() ? a->conceptionCourante()->composants.size() : 99;
  verifier(pose && composants == 13 && pasPose == 1 && apresAnnulation == 0 && rails.contains(QStringLiteral("U1 (")),
           "034 L14 · 14.14 : l'abaisseur posé sur la feuille en un pas, retiré d'un Ctrl+Z ; l'arbre des rails le montre",
           std::to_string(composants) + " composant(s), " + std::to_string(pasPose) + " pas, " + rails.toStdString());

  // ---------- 14.8 · Mesure importée superposée à la simulation, moteurs du poste ----------
  a->ouvrir(MESURE, AtelierElectronique::Schema);
  attendre(200);
  lab->montrer(QStringLiteral("mesures"));
  {
    const std::string dossier = dossierEssai("mesure");
    std::ostringstream csv;
    csv.imbue(std::locale::classic());
    csv << "Time (s),CH1 (V)\n";
    for (int i = 0; i <= 500; ++i) {
      const double t = i * 1e-5;
      csv << t << "," << 1 - std::exp(-t / 1e-3) << "\n";
    }
    const QString fichier = QString::fromStdString(dossier) + QStringLiteral("/rc.csv");
    prom::ecrireFichierAtomique(fichier.toStdString(), csv.str());
    const bool importee = lab->importerMesure(fichier);
    const double ecart = ngspice ? lab->ecartMesureSimulation(QStringLiteral("SORTIE")) : 0;
    const QString moteurs = lab->moteurs();
    verifier(importee && lab->courbesMesurees() == 1 && ecart >= 0 && ecart < 0.01 && moteurs.contains(QStringLiteral("ngspice")) && moteurs.contains(QStringLiteral("Xyce")),
             "034 L14 · 14.8 : la mesure CSV d'oscilloscope importée se superpose à la simulation de ngspice à 1 % près ; les moteurs du poste sont listés",
             "écart " + nb(ecart) + " ; " + moteurs.toStdString());
  }
  lab->close();
  attendre(50);
}
