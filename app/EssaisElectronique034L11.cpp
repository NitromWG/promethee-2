// Prométhée : essais d'interface de la 0.34, lot L11 (simulation) — courbe calculée tracée, exports, clic sur une
// courbe qui désigne son fil (11.13) ; classeur de trois analyses enregistré puis rouvert (11.14) ; référence et
// verdict (11.15) ; remèdes après un échec et préparation qui mène au composant (11.9) ; étage simulé seul (11.8) ;
// calculateurs et inspecteur du circuit (11.7, 11.17 à 11.19) ; modèle de simulation d'un composant : fiche,
// ajustement sur son fil, IBIS, fichier PSpice, rattaché à l'article en un pas (11.5, 11.11, 11.12).
// Appelés seuls avec PROMETHEE_ESSAIS_LOTS=034L11. Licence GPL-3.0-only.
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <set>
#include <string>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "FenetreSimulation.hpp"
#include "InspecteurElectronique.hpp"
#include "OutilsSimulation034.hpp"
#include "SelectionElectronique.hpp"
#include "VueCircuit.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/simulation.hpp"
#include "promethee/simulation_projet.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
QString donnees() {
  QString dossier = qEnvironmentVariable("PROMETHEE_DONNEES");
  for (const QString& essai : {QCoreApplication::applicationDirPath() + QStringLiteral("/../../tests/donnees"), QStringLiteral("tests/donnees")})
    if (dossier.isEmpty() && QFileInfo::exists(essai + QStringLiteral("/ibis/ibis_v5_1.ibs"))) dossier = essai;
  return dossier;
}
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
// Montage : symboles posés en ligne, chaque broche reliée par une étiquette globale au réseau voulu
struct Montage {
  ConceptionElec c = nouvelleConception("Montage");
  double x = 25.4;
  ComposantElec* poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, 50.8));
    x += 25.4;
    ComposantElec* r = nullptr;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        r = c.composant(p.composant);
        r->reference = ref;
        r->valeur = valeur;
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
  std::string uuid(const std::string& ref) const { return c.parReference(ref) ? c.parReference(ref)->uuid : std::string(); }
};
AtelierElectronique* ouvrir(FenetrePrincipale& f, ConceptionElec c, AtelierElectronique::Mode vue = AtelierElectronique::Schema) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = c.nom;
  p.electronique.push_back(std::move(c));
  f.document()->creer(p);
  f.montrerElectronique(0, vue);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return nullptr;
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);
  a->recalculer();
  return a;
}
// Lance le calcul (ou les remèdes) et attend sa fin
void calculer(FenetreSimulation* s, bool remedes = false) {
  QEventLoop boucle;
  QObject::connect(s, &FenetreSimulation::calculTermine, &boucle, &QEventLoop::quit);
  QTimer::singleShot(90'000, &boucle, &QEventLoop::quit);
  if (remedes) s->essayerRemedes();
  else s->lancer();
  if (s->enCours()) boucle.exec();
}
FenetreSimulation* simulation(AtelierElectronique* a) {
  a->executerAction("simulation.ouvrir");
  attendre(300);
  return a->simulation();
}
double tension(FenetreSimulation* s, const std::string& reseau) {
  const auto* v = s->resultat().reel(vecteurTension(s->netlist(), reseau));
  return v && !v->empty() ? v->back() : std::nan("");
}
const TraceCourbes::Courbe* courbe(FenetreSimulation* s, const QString& nom) {
  for (const auto& k : s->trace()->courbes())
    if (k.nom == nom) return &k;
  return nullptr;
}
std::string nombre(double v, const char* format = "%.4g") {
  char t[64];
  std::snprintf(t, sizeof t, format, v);
  return t;
}
const char* LM358_PSPICE = R"(* LM358 en syntaxe PSpice (essais de Prométhée)
.SUBCKT LM358 1 2 3 4 5
RIN 1 2 2MEG
GA 0 10 VALUE={1E-3*V(1,2)}
RA 10 0 100MEG TC=0.0005,0
CA 10 0 227P
EOUT 11 0 VALUE={LIMIT(V(10), V(4)+0.02, V(3)-1.5)}
RO 11 5 75
.ENDS LM358
)";
}  // namespace

void essaisElectronique034L11(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  QTemporaryDir dossier;

  // ---------- 11.7, 11.17 à 11.19 · Calculateurs : fiches bornées, résultats des spécifications ----------
  {
    Montage m;
    m.poser("R", "R1", "1k", {{"1", "A"}, {"2", "GND"}});
    AtelierElectronique* a = ouvrir(f, m.c);
    if (!a) {
      verifier(false, "034 L11 : le montage d'essai s'ouvre", {});
      return;
    }
    a->executerAction("simulation.calculateurs");
    attendre(200);
    FenetreCalculateurs* w = a->calculateurs();
    auto* liste = w ? w->findChild<QTreeWidget*>(QStringLiteral("listeCalculateurs")) : nullptr;
    bool isolement = false, refus = false, couleurs = false, autonomie = false;
    std::string detail;
    if (w && liste) {
      isolement = w->choisir("isolement") && w->regler({600, 0, 1, 2}) && w->resultat().contains(QStringLiteral("0,50 mm"));
      refus = !w->regler({1e12}) && w->resultat().contains(QStringLiteral("hors des bornes"));
      couleurs = w->choisir("couleurs") && w->regler({0, 4, 7, 2, 10, 1}) && w->resultat().contains(QStringLiteral("4,7"));
      autonomie = w->choisir("autonomie") && w->regler({0.02, 59, 15, 1, 0, 0, 2000, 0, 100}) && w->resultat().contains(QStringLiteral("7416")) && w->resultat().contains(QStringLiteral("92,7"));
      detail = std::to_string(liste->topLevelItemCount()) + " familles ; isolement " + (isolement ? "0,50 mm" : "?") + (refus ? ", hors bornes refusé" : ", hors bornes accepté") +
               (couleurs ? ", anneaux 4,7 kΩ" : "") + (autonomie ? ", 7416 h et 92,7 %" : ", autonomie ?");
    }
    verifier(liste && liste->topLevelItemCount() == 4 && isolement && refus && couleurs && autonomie,
             "11.7, 11.17 à 11.19 : calculateurs en quatre familles ; 600 V en couche interne : 0,50 mm ; anneaux ; autonomie de 7 416 h ; valeur hors bornes refusée", detail);
    if (w) w->close();
  }

  // ---------- 11.17 · Inspecteur du circuit : longueur, délai, résistance, courant de la piste et de la via ----------
  {
    ConceptionElec c0 = nouvelleConception("Cuivre");
    c0.circuit.contour.rayon = 0;
    c0.circuit.contour.largeur = nm(50);
    c0.circuit.contour.hauteur = nm(30);
    c0.circuit.pistes = {{"s1", "N", Couche::CuivreDessus, ptMm(5, 10), ptMm(45, 10), nm(0.25)}};
    Via v;
    v.uuid = "v1";
    v.reseau = "N";
    v.p = ptMm(45, 10);
    c0.circuit.vias.push_back(v);
    AtelierElectronique* a = ouvrir(f, c0, AtelierElectronique::Circuit);
    VueCircuit* vc = a ? a->vueCircuit() : nullptr;
    QString piste, via;
    if (vc) {
      a->montrerInspecteur();
      if (QDockWidget* d = a->panneau(AtelierElectronique::Panneau::Inspecteur)) d->show();
      vc->choisir(VueCircuit::Genre::Piste, "s1");
      attendre(250);
      if (auto* l = a->inspecteur()->findChild<QLabel*>(QStringLiteral("proprietesPiste"))) piste = l->text();
      vc->choisir(VueCircuit::Genre::Via, "v1");
      attendre(250);
      if (auto* l = a->inspecteur()->findChild<QLabel*>(QStringLiteral("proprietesVia"))) via = l->text();
    }
    verifier(piste.contains(QStringLiteral("40")) && piste.contains(QStringLiteral("Ω")) && piste.contains(QStringLiteral("A")) && !via.isEmpty(),
             "11.17 : l'inspecteur du circuit donne longueur, délai, résistance et courant admissible de la piste et de la via choisies",
             (piste + QStringLiteral(" | ") + via).toStdString());
  }

  // Sans ngspice sur le poste, les essais de calcul ne sont pas faits : c'est dit, sans compter comme un échec
  if (!ngspiceDisponible()) {
    std::printf("SAUTÉ   034 L11 : ngspice n'est pas installé sur ce poste : essais de simulation non faits\n");
    std::fflush(stdout);
    return;
  }

  // ---------- 11.13 · Courbe calculée en dB sur le filtre RC, exports, clic sur une courbe ----------
  const QString expr = QStringLiteral("db(V(SORTIE)/V(ENTREE))");
  AnalyseSpice ac;
  ac.type = AnalyseSpice::Type::Alternatif;
  ac.fDebut = 10;
  ac.fFin = 1e6;
  ac.pointsParDecade = 100;
  const double fc = 1 / (2 * std::numbers::pi * 1000 * 100e-9);
  {
    Montage m;
    m.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
    m.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
    m.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
    AtelierElectronique* a = ouvrir(f, m.c);
    FenetreSimulation* s = a ? simulation(a) : nullptr;
    if (!s) {
      verifier(false, "034 L11 : la fenêtre de simulation s'ouvre", {});
      return;
    }
    s->setAnalyse(ac);
    s->sonder({"SORTIE"});
    s->ajouterExpression(expr);
    calculer(s);
    const TraceCourbes::Courbe* k = courbe(s, expr);
    const double db = k ? valeurA(k->x, k->y, fc) : std::nan("");
    const std::string csv = s->trace()->csv(), svg = s->trace()->svg();
    const QString png = dossier.filePath(QStringLiteral("courbes.png"));
    const bool image = s->trace()->exporterPng(png) && QFileInfo(png).size() > 1000;
    verifier(std::abs(db + 3.0) < 0.05 && csv.find("db(V(SORTIE)/V(ENTREE))") != std::string::npos && svg.find("<polyline") != std::string::npos && image,
             "11.13 : V(sortie)/V(entrée) en dB tracée par expression : −3,0 dB à 1/(2πRC) ; tracé exporté en CSV, SVG et PNG",
             "à " + nombre(fc) + " Hz : " + nombre(db) + " dB ; CSV " + std::to_string(csv.size()) + " o, SVG " + std::to_string(svg.size()) + " o");

    // Clic sur la courbe de SORTIE : son fil est surligné
    bool designe = false;
    std::string detail;
    if (auto* onglets = s->findChild<QTabWidget*>()) onglets->setCurrentIndex(0);
    s->show();
    reel::activer(s);
    attendre(200);
    if (const TraceCourbes::Courbe* ks = courbe(s, QStringLiteral("SORTIE")); ks && ks->x.size() > 10) {
      const std::size_t i = ks->x.size() / 2;
      const QPointF p = s->trace()->versEcran(ks->x[i], ks->y[i]);
      reel::cliquer(s->trace(), p.toPoint());
      attendre(200);
      designe = a->selection()->reseau() == "SORTIE";
      detail = "réseau surligné « " + a->selection()->reseau() + " »";
    }
    verifier(designe, "11.14 : un clic sur une courbe désigne son fil dans le schéma", detail);

    // ---------- 11.14 · Classeur de trois analyses, enregistré avec le projet puis rouvert ----------
    bool enregistre = s->enregistrerOnglet(QStringLiteral("Réponse en fréquence")) && s->figerResultat(QStringLiteral("Référence"));
    AnalyseSpice repos;
    repos.type = AnalyseSpice::Type::PointDeRepos;
    s->setAnalyse(repos);
    enregistre = s->enregistrerOnglet(QStringLiteral("Point de repos")) && enregistre;
    AnalyseSpice tr;
    tr.type = AnalyseSpice::Type::Transitoire;
    tr.pas = 1e-6, tr.duree = 1e-3;
    s->setAnalyse(tr);
    enregistre = s->enregistrerOnglet(QStringLiteral("Échelon")) && enregistre;
    const ClasseurSimulation avant = s->classeur();
    const QString fichier = dossier.filePath(QStringLiteral("classeur.promethee"));
    doc->enregistrer(fichier, true);
    doc->ouvrir(fichier, true);
    f.montrerElectronique(0, AtelierElectronique::Schema);
    attendre(400);
    a = f.atelierElectronique();
    s = a ? simulation(a) : nullptr;
    bool relu = false;
    std::string d14;
    if (s) {
      const ClasseurSimulation apres = s->classeur();
      const bool ouvert = s->ouvrirOnglet(QStringLiteral("Réponse en fréquence"));
      auto* liste = s->findChild<QListWidget*>(QStringLiteral("expressions"));
      const AnalyseSpice an = s->analyse();
      relu = enregistre && ouvert && apres == avant && apres.onglets.size() == 3 && apres.fige("Référence") && an.type == AnalyseSpice::Type::Alternatif && an.fDebut == 10 &&
             an.pointsParDecade == 100 && liste && liste->count() == 1 && liste->item(0)->text() == expr;
      d14 = std::to_string(apres.onglets.size()) + " analyse(s), " + std::to_string(apres.figes.size()) + " figé(s)" + (apres == avant ? ", identiques" : ", différents");
    }
    verifier(relu, "11.14 : classeur de trois analyses enregistré puis rouvert : mêmes réglages, mêmes courbes figées", d14);

    // ---------- 11.15 · Référence dans une bande de 1 % : réussite ; R1 augmentée de 10 % : échec, écart surligné ----------
    bool reussi = false, echoue = false;
    std::string d15;
    if (s) {
      s->sonder({"SORTIE"});
      s->prendreReference(QStringLiteral("Référence"), QStringLiteral("SORTIE"), 0.01);
      calculer(s);
      const Comparaison ok = s->comparer();
      reussi = ok.reussie && ok.verdict() == "réussi";
      a->modifierConception([](ConceptionElec& c) {
        for (auto& k2 : c.composants)
          if (k2.reference == "R1") k2.valeur = "1.1k";
      }, QStringLiteral("R1 à 1,1 kΩ"));
      s->refaire();
      calculer(s);
      const Comparaison ko = s->comparer();
      bool surligne = false;
      for (const auto& k2 : s->trace()->courbes()) surligne |= k2.nom.endsWith(QStringLiteral("(comparée)")) && !k2.surlignes.empty();
      auto* verdict = s->findChild<QLabel*>(QStringLiteral("verdictReference"));
      std::string garde;
      for (const auto& o : s->classeur().onglets)
        if (o.nom == "Réponse en fréquence") garde = o.verdict;
      echoue = !ko.reussie && surligne && verdict && verdict->text().contains(QStringLiteral("échoué")) && garde == "échoué";
      d15 = "même filtre : " + ok.verdict() + " ; R1 +10 % : " + ko.verdict() + ", écart " + nombre(ko.ecartMax) + ", verdict gardé « " + garde + " »";
    }
    verifier(reussi && echoue, "11.15 : filtre RC comparé à sa référence dans une bande de 1 % : réussite ; R1 augmentée de 10 % : échec, écart surligné, verdict gardé", d15);
    if (s) s->close();
  }

  // ---------- 11.9 · Pas effondré : cause en français, remèdes essayés, calcul abouti et noté ----------
  {
    Montage m;
    m.poser("Source_tension", "V1", "SIN(0 325 50)", {{"1", "SECTEUR"}, {"2", "GND"}});
    const std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> diodes = {
        {"D1", {{"2", "SECTEUR"}, {"1", "P"}}}, {"D2", {{"2", "GND"}, {"1", "P"}}}, {"D3", {{"2", "M"}, {"1", "SECTEUR"}}}, {"D4", {{"2", "M"}, {"1", "GND"}}}};
    for (const auto& [ref, l] : diodes) {
      ComposantElec* k = m.poser("D", ref, "DX", l);
      k->champs["Sim.Modele"] = "D{ref} {2} {1} DX";
      k->champs["Sim.Bibliotheque"] = ".model DX D(IS=1e-30 N=0.3 RS=0)";
    }
    m.poser("C", "C1", "1000u", {{"1", "P"}, {"2", "M"}});
    m.poser("R", "R1", "100", {{"1", "P"}, {"2", "M"}});
    AtelierElectronique* a = ouvrir(f, m.c);
    FenetreSimulation* s = a ? simulation(a) : nullptr;
    bool remede = false;
    std::string detail;
    if (s) {
      AnalyseSpice an;
      an.type = AnalyseSpice::Type::Transitoire;
      an.pas = 10e-6, an.duree = 40e-3;
      s->setAnalyse(an);
      s->options().sansPointRepos = true;
      calculer(s);
      const bool echec = !s->resultat().reussi() && s->diagnostic().genre == "pas";
      auto* etat = s->findChild<QLabel*>(QStringLiteral("etatSimulation"));
      auto* bouton = s->findChild<QPushButton*>(QStringLiteral("essayerRemedes"));
      const bool dit = etat && etat->text().contains(QStringLiteral("Le pas de calcul s’est effondré")) && bouton && bouton->isVisible();
      if (echec && dit) calculer(s, true);
      remede = echec && dit && s->resultat().reussi() && !s->remedeRetenu().isEmpty() && s->options().remedes.size() == 1;
      detail = std::string(echec ? "échec « pas » analysé" : "pas d'échec analysé") + (dit ? ", cause dite, remèdes proposés" : ", cause ou bouton absents") + " ; retenu « " +
               s->remedeRetenu().toStdString() + " »" + (s->options().remedes.empty() ? "" : ", noté « " + s->options().remedes.front() + " »");
      s->close();
    }
    verifier(remede, "11.9 : circuit arrêté sur « timestep too small » : cause dite en français, remèdes essayés dans l'ordre, calcul abouti et changement noté", detail);
  }

  // ---------- 11.9 · Préparation : chaque ligne mène à son composant ----------
  {
    Montage m;
    m.poser("Source_tension", "V1", "5", {{"1", "A"}, {"2", "GND"}});
    m.poser("Source_tension", "V2", "5", {{"1", "A"}, {"2", "GND"}});
    m.poser("R", "R1", "1k", {{"1", "A"}, {"2", "GND"}});
    AtelierElectronique* a = ouvrir(f, m.c);
    FenetreSimulation* s = a ? simulation(a) : nullptr;
    bool mene = false;
    std::string detail;
    if (s) {
      AnalyseSpice op;
      op.type = AnalyseSpice::Type::PointDeRepos;
      s->setAnalyse(op);
      const auto lignes = s->preparer();
      auto* t = s->findChild<QTableWidget*>(QStringLiteral("tablePreparation"));
      if (auto* onglets = s->findChild<QTabWidget*>(); onglets && t) onglets->setCurrentWidget(t->parentWidget());
      attendre(150);
      for (int r = 0; t && r < t->rowCount() && !mene; ++r)
        if (auto* it = t->item(r, 1); it && !it->data(Qt::UserRole).toString().isEmpty() && it->text().contains(QStringLiteral("boucle"), Qt::CaseInsensitive)) {
          const std::string u = it->data(Qt::UserRole).toString().toStdString();
          t->scrollToItem(it);
          reel::doubleCliquer(t->viewport(), t->visualItemRect(it).center());
          attendre(250);
          mene = a->selection()->composants().count(u) > 0;
          detail = it->text().toStdString();
        }
      if (detail.empty()) detail = std::to_string(lignes.size()) + " ligne(s), aucune boucle";
      s->close();
    }
    verifier(mene, "11.9 : la préparation trouve la boucle de sources idéales et sa ligne mène au composant", detail);
  }

  // ---------- 11.8 · Étage d'entrée simulé seul, sortie chargée par 10 kΩ : même gain que le schéma complet ----------
  {
    Montage m;
    m.poser("Source_tension", "V1", "DC 1 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
    m.poser("AOP", "U1", "AOP", {{"1", "ENTREE"}, {"3", "FB"}, {"4", "S1"}, {"5", "+12V"}, {"2", "-12V"}});
    m.poser("R", "R1", "10k", {{"1", "FB"}, {"2", "GND"}});
    m.poser("R", "R2", "20k", {{"1", "S1"}, {"2", "FB"}});
    m.poser("R", "R5", "1k", {{"1", "S1"}, {"2", "MILIEU"}});
    m.poser("R", "R3", "10k", {{"1", "MILIEU"}, {"2", "FB2"}});
    m.poser("AOP", "U2", "AOP", {{"1", "GND"}, {"3", "FB2"}, {"4", "SORTIE"}, {"5", "+12V"}, {"2", "-12V"}});
    m.poser("R", "R4", "10k", {{"1", "SORTIE"}, {"2", "FB2"}});
    const std::set<std::string> etage{m.uuid("V1"), m.uuid("U1"), m.uuid("R1"), m.uuid("R2"), m.uuid("R5")};
    AtelierElectronique* a = ouvrir(f, m.c);
    FenetreSimulation* s = a ? simulation(a) : nullptr;
    bool meme = false;
    std::string detail;
    if (s) {
      AnalyseSpice op;
      op.type = AnalyseSpice::Type::PointDeRepos;
      s->setAnalyse(op);
      calculer(s);
      const double complet = tension(s, "MILIEU");
      std::vector<SelectionElectronique::Ref> refs;
      for (const auto& p : conception(doc)->feuilles[0].symboles)
        if (etage.count(p.composant)) refs.push_back({SelectionElectronique::Genre::Symbole, p.uuid, p.composant});
      a->selection()->choisir(refs);
      attendre(100);
      s->setPartie(2);
      const std::size_t coupees = s->options().liaisons.size();
      const bool charge = s->setBouclage("MILIEU", BouclageLiaison::Charge, 10e3);
      auto* table = s->findChild<QTableWidget*>(QStringLiteral("tableLiaisons"));
      calculer(s);
      const double seul = tension(s, "MILIEU");
      const bool sansSuivant = s->netlist().texte.find("XU2") == std::string::npos && s->netlist().texte.find("RR3") == std::string::npos;
      meme = std::abs(complet - 3.0 * 10 / 11) < 0.03 && std::abs(seul - complet) / complet < 0.001 && coupees == 3 && charge && table && table->rowCount() == 3 && sansSuivant;
      detail = "complet " + nombre(complet, "%.5f") + " V, étage seul " + nombre(seul, "%.5f") + " V ; " + std::to_string(coupees) + " liaison(s) coupée(s)" + (sansSuivant ? "" : ", étage suivant encore dans la netlist");
      s->close();
    }
    verifier(meme, "11.8 : étage d'entrée choisi et simulé seul, sortie chargée par les 10 kΩ de l'étage suivant : même gain que le schéma complet à 0,1 %", detail);
  }

  // ---------- 11.5, 11.11, 11.12 · Modèle de simulation d'un composant, rattaché à son article ----------
  {
    Montage m;
    m.poser("Source_tension", "V1", "5", {{"1", "A"}, {"2", "GND"}});
    m.poser("R", "R1", "1k", {{"1", "A"}, {"2", "DIODE"}});
    m.poser("D", "D1", "1N4148", {{"2", "DIODE"}, {"1", "GND"}});
    const std::string d1 = m.uuid("D1");
    AtelierElectronique* a = ouvrir(f, m.c);
    DialogueModeleSimulation* d = a ? a->modeleSimulation(d1) : nullptr;
    bool fiche = false, borne = false, ajuste = false, ibis = false, pspice = false, rattache = false, simule = false, defait = false;
    std::string detail;
    if (d) {
      fiche = d->ecrireFiche("diode", {}).contains(QStringLiteral(".model D1_diode D("));
      QString refus;
      borne = d->ecrireFiche("diode", {1e10}, &refus).isEmpty() && refus.contains(QStringLiteral("hors bornes"));
      const AjustementModele aj = d->ajusterDiode({{1e-3, 0.6}, {10e-3, 0.72}, {100e-3, 0.93}}, true);
      ajuste = aj.reussi && aj.ecartMax < 0.02 && d->propose().texte.find(".model D1_FICHE") != std::string::npos;
      const ModeleSpice diode = d->propose();
      const QString chemin = donnees() + QStringLiteral("/ibis/ibis_v5_1.ibs");
      ibis = d->lireIbis(chemin) && d->tamponIbis(QStringLiteral("AC40"), 0).reussi && d->propose().texte.find(".subckt AC40_TYP out") != std::string::npos;
      const QString lib = dossier.filePath(QStringLiteral("lm358.lib"));
      try {
        prom::ecrireFichierAtomique(prom::cheminFichier(lib.toStdString()), LM358_PSPICE);
        pspice = d->lireModele(lib, QStringLiteral("ps")) && d->propose().nom == "LM358" && d->propose().compatibilite == "ps";
      } catch (const std::exception&) {
      }
      const int pas = doc->positionHistorique();
      rattache = d->rattacher(diode) && doc->positionHistorique() == pas + 1;
      const ConceptionElec* c = conception(doc);
      const ComposantElec* k = c ? c->composant(d1) : nullptr;
      rattache = rattache && k && c->articles.count(k->article) && c->articles.at(k->article).modeleSpice.nom == "D1_FICHE";
      d->close();
      if (FenetreSimulation* s = simulation(a)) {
        AnalyseSpice op;
        op.type = AnalyseSpice::Type::PointDeRepos;
        s->setAnalyse(op);
        calculer(s);
        const double vd = tension(s, "DIODE");
        simule = s->resultat().reussi() && s->netlist().texte.find("DD1 n_diode 0 D1_FICHE") != std::string::npos && vd > 0.6 && vd < 0.85;
        detail = "diode simulée à " + nombre(vd, "%.3f") + " V avec son modèle ajusté (écart " + nombre(100 * aj.ecartMax, "%.2f") + " %)";
        s->close();
      }
      doc->annuler();
      attendre(150);
      const ConceptionElec* c2 = conception(doc);
      const ComposantElec* k2 = c2 ? c2->composant(d1) : nullptr;
      defait = k2 && (k2->article.empty() || !c2->articles.count(k2->article) || c2->articles.at(k2->article).modeleSpice.vide());
    }
    detail += std::string(fiche ? " ; fiche écrite" : " ; fiche ?") + (borne ? ", hors bornes refusé" : ", hors bornes accepté") + (ibis ? ", IBIS" : ", IBIS ?") + (pspice ? ", PSpice" : ", PSpice ?") +
              (rattache ? ", rattaché en un pas" : ", rattachement ?") + (defait ? ", défait" : ", pas défait");
    verifier(fiche && borne && ajuste && ibis && pspice && rattache && simule && defait,
             "11.5, 11.11, 11.12 : modèle d'un composant tiré de sa fiche (sur son fil), d'un IBIS ou d'un fichier PSpice, rattaché à l'article en un pas annulable, simulé", detail);
  }
}
