// Prométhée : essais d'interface de la 0.34, lot L13 (automatisation et assistant) — ligne de commande sous le canevas,
// macro enregistrée, relue de son fichier et rejouée sur une carte neuve, annulée d'un Ctrl+Z (13.8) ; console Python :
// dix résistances retirées d'un Ctrl+Z, point d'arrêt et pas à pas (13.3), fréquence de coupure tracée en fonction
// de R2 (13.9) ; propositions MCP refusée puis acceptée (13.4) ; assistant local : « Expliquer ce bloc », violation,
// fiche technique, simulation confiée (13.15 à 13.17) ; éditeur de requêtes et règle tirée de la requête (13.11) ;
// conception guidée (13.14). Appelés seuls avec PROMETHEE_ESSAIS_LOTS=034L13. Licence GPL-3.0-only.
#include <cmath>
#include <filesystem>
#include <functional>
#include <numbers>
#include <random>
#include <string>

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFileInfo>
#include <QLineEdit>
#include <QStringListModel>
#include <QTimer>
#include <QToolButton>

#include "AtelierElectronique.hpp"
#include "Automatisation034.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "FenetreSimulation.hpp"
#include "RegistreActions.hpp"
#include "promethee/electronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/pdf_documents.hpp"
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
// Attend une condition (au plus « ms ») en laissant tourner la boucle d'événements
bool attendreQue(const std::function<bool()>& c, int ms) {
  for (int t = 0; t < ms && !c(); t += 20) attendre(20);
  return c();
}
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
};
std::string dossierEssai(const std::string& nom) {
  std::random_device r;
  const fs::path p = fs::temp_directory_path() / ("promethee-essai-l13-" + nom + "-" + std::to_string(r()));
  fs::create_directories(p);
  return prom::utf8(p);
}
int trous(const ConceptionElec& c) {
  int n = 0;
  for (const auto& e : c.circuit.empreintes) n += e.empreinte.rfind("MountingHole", 0) == 0;
  return n;
}
// Ce que la macro « format de l'école » règle : contour, trous, texte, règles, cartouche
std::string formatEcole(const Document* doc, std::size_t i) {
  if (i >= doc->projet().electronique.size()) return "conception " + std::to_string(i) + " absente (" + std::to_string(doc->projet().electronique.size()) + " en tout)";
  const ConceptionElec& c = doc->projet().electronique[i];
  std::string t = std::to_string(c.circuit.contour.largeur) + "×" + std::to_string(c.circuit.contour.hauteur) + " r" + std::to_string(c.circuit.contour.rayon);
  t += " ; " + std::to_string(trous(c)) + " trous ; isolement " + std::to_string(c.circuit.regles.isolement) + ", largeur " + std::to_string(c.circuit.regles.largeur);
  for (const auto& x : c.circuit.textes) t += " ; « " + x.texte + " »";
  t += " ; " + c.cartouche.societe + " / " + c.cartouche.auteur;
  return t;
}
}  // namespace

void essaisElectronique034L13(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  // Le filtre RC de la simulation : V1, R2 de 1 kΩ, C1 de 100 nF
  Montage m;
  m.c.nom = "Filtre";
  m.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
  m.poser("R", "R2", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
  m.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = "Automatisation";
  p.electronique.push_back(m.c);
  doc->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Circuit);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a || !a->ligneCommandes()) {
    verifier(false, "034 L13 : l'atelier s'ouvre avec sa ligne de commande", {});
    return;
  }
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);

  // ---------- 13.8 · Ligne de commande, macro enregistrée, relue, rejouée sur une carte neuve, un Ctrl+Z ----------
  {
    LigneCommandes* l = a->ligneCommandes();
    const bool sous = l->isVisible() && l->mapTo(a->window(), QPoint(0, 0)).y() > a->vueCircuit()->mapTo(a->window(), QPoint(0, 0)).y();
    // Après les autres lots, une fenêtre de premier plan a pu garder l'activation : le focus et les raccourcis
    // n'arrivent qu'à la fenêtre active
    reel::activer(&f);
    attendre(50);
    a->executerAction("automatisation.ligne");
    attendre(80);
    const bool focus = l->champ()->hasFocus();
    reel::taper(l->champ(), QStringLiteral("carte.c"));
    attendre(60);
    auto* modele = l->champ()->completer() ? qobject_cast<QStringListModel*>(l->champ()->completer()->model()) : nullptr;
    const bool complete = modele && modele->stringList().contains(QStringLiteral("carte.contour"));
    reel::touche(l->champ(), Qt::Key_Escape);  // la liste de complétion se ferme
    if (l->champ()->completer() && l->champ()->completer()->popup()) l->champ()->completer()->popup()->hide();
    l->champ()->clear();
    // Enregistrement : les lignes tapées une à une, chacune un pas
    // Une carte pour la saisie à la main
    doc->modifier([](prom::Projet& x) { x.electronique.push_back(nouvelleConception("Carte à la main")); }, QStringLiteral("Carte à la main"));
    a->ouvrir(1, AtelierElectronique::Circuit);
    attendre(200);
    auto* bouton = l->findChild<QToolButton*>(QStringLiteral("enregistrerMacro"));
    if (bouton) reel::cliquer(bouton, bouton->rect().center());
    attendre(50);
    const bool enregistre = l->enregistrement();
    const QStringList lignes = {QStringLiteral("carte.contour largeur=100 hauteur=80 rayon=3"),
                                QStringLiteral("carte.trou x=4 y=4 diametre=3.2"),
                                QStringLiteral("carte.trou x=96 y=4 diametre=3.2"),
                                QStringLiteral("carte.trou x=4 y=76 diametre=3.2"),
                                QStringLiteral("carte.trou x=96 y=76 diametre=3.2"),
                                QStringLiteral("carte.texte texte=\"ESME Sudria\" x=10 y=70 hauteur=1.5"),
                                QStringLiteral("carte.regles isolement=0.2 largeur=0.3"),
                                QStringLiteral("cartouche societe=\"ESME Sudria\" auteur=Atelier")};
    const int pas0 = doc->positionHistorique();
    for (const QString& x : lignes) {
      l->champ()->setFocus();
      reel::taper(l->champ(), x);
      reel::touche(l->champ(), Qt::Key_Return);
      attendre(40);
    }
    const int pasManuels = doc->positionHistorique() - pas0;
    if (bouton) reel::cliquer(bouton, bouton->rect().center());
    attendre(50);
    const std::string manuel = formatEcole(doc, 1);
    const prom::Macro macro = l->macro();
    // Fichier texte de la macro, relu
    const std::string dossier = dossierEssai("macro");
    const QString fichier = QString::fromStdString(dossier) + QStringLiteral("/format-ecole.prom-macro");
    const bool ecrit = l->enregistrerMacro(fichier, QStringLiteral("format de l’école"));
    const prom::Macro relue = prom::lireMacro(prom::lireFichier(fichier.toStdString()));
    // Carte neuve, macro rejouée : un seul pas
    doc->modifier([](prom::Projet& x) { x.electronique.push_back(nouvelleConception("Carte neuve")); }, QStringLiteral("Carte neuve"));
    a->ouvrir(2, AtelierElectronique::Circuit);
    attendre(200);
    const std::string neuve = formatEcole(doc, 2);
    const int pas1 = doc->positionHistorique();
    QString message;
    const bool rejouee = l->rejouerFichier(fichier, &message);
    attendre(100);
    const int pasRejoues = doc->positionHistorique() - pas1;
    const std::string apres = formatEcole(doc, 2);
    doc->annuler();
    attendre(100);
    const std::string annulee = formatEcole(doc, 2);
    // Ligne fautive : refusée, le projet n'a pas bougé ; flèche haut : la dernière ligne revient
    const int pas2 = doc->positionHistorique();
    QString refus;
    const bool fautive = !l->executer(QStringLiteral("faire.nimporte quoi=1"), &refus) && doc->positionHistorique() == pas2;
    l->champ()->setFocus();
    l->champ()->clear();
    reel::touche(l->champ(), Qt::Key_Up);
    const bool historique = l->champ()->text() == lignes.back();
    l->champ()->clear();
    // Commande affectée à une touche
    l->affecter(QStringLiteral("Ctrl+Alt+1"), QStringLiteral("carte.regles isolement=0.25"));
    reel::activer(&f);
    a->vueCircuit()->setFocus();
    reel::touche(a->vueCircuit(), Qt::Key_1, Qt::ControlModifier | Qt::AltModifier);
    attendre(100);
    const bool touche = doc->projet().electronique.size() > 2 && doc->projet().electronique[2].circuit.regles.isolement == nm(0.25);
    // Commandes probables : après carte.contour, carte.trou a été vu
    l->executer(QStringLiteral("carte.contour largeur=60 hauteur=40"));
    const bool probable = l->propositions().contains(QStringLiteral("carte.trou"));
    fs::remove_all(fs::path(dossier));
    verifier(sous && focus && complete && enregistre && pasManuels == 8 && macro.lignes.size() == 8 && ecrit && relue.lignes == macro.lignes && rejouee && pasRejoues == 1 && apres == manuel &&
                 annulee == neuve && fautive && refus.contains(QStringLiteral("inconnue")) && historique && touche && probable,
             "13.8 : macro « format de l’école » enregistrée à la ligne de commande, relue de son fichier et rejouée sur une carte neuve : même résultat qu’à la main, annulée d’un seul Ctrl+Z",
             "sous le canevas " + std::to_string(sous) + ", focus " + std::to_string(focus) + ", complétion " + std::to_string(complete) + ", enregistrement " + std::to_string(enregistre) + ", " + std::to_string(pasManuels) + " pas à la main, macro de " +
                 std::to_string(macro.lignes.size()) + " ligne(s), rejouée en " + std::to_string(pasRejoues) + " pas (" + message.toStdString() + ") ; à la main « " + manuel + " », rejouée « " + apres +
                 " », annulée « " + annulee + " » ; refus « " + refus.toStdString() + " », historique " + std::to_string(historique) + ", touche " + std::to_string(touche) + ", probable " +
                 std::to_string(probable));
    a->ouvrir(0, AtelierElectronique::Circuit);
    attendre(150);
  }

  // ---------- 13.3 · Console Python : dix résistances, un Ctrl+Z ; point d'arrêt et pas à pas ----------
  ConsolePython* py = nullptr;
  std::string versionPython;
  const bool python = prom::pythonDisponible(&versionPython);
  if (!python) {
    std::printf("SAUTÉ   034 L13 : Python 3 n'est pas installé sur ce poste : console non essayée\n");
    std::fflush(stdout);
  } else {
    a->executerAction("automatisation.python");
    attendre(200);
    py = a->consolePython();
    const bool ancree = py && py->isVisible();
    const std::size_t avant = doc->projet().electronique[0].composants.size();
    const int pas0 = doc->positionHistorique();
    py->setCode(QStringLiteral("import promethee\n"
                               "for i in range(10):\n"
                               "    promethee.poser_symbole(\"R\", x=20 + 10 * i, y=120, ref=\"R%d\" % (i + 10), valeur=\"10k\")\n"
                               "print(\"posées :\", len(promethee.composants()))\n"));
    py->executer(true);
    const std::size_t poses = doc->projet().electronique[0].composants.size() - avant;
    const int pas = doc->positionHistorique() - pas0;
    const bool sortie = py->sortie().contains(QStringLiteral("posées : %1").arg(avant + 10));
    doc->annuler();
    attendre(100);
    const bool retirees = doc->projet().electronique[0].composants.size() == avant;
    verifier(ancree && poses == 10 && pas == 1 && sortie && retirees, "13.3 : un script de la console Python pose dix résistances, un Ctrl+Z les retire",
             std::to_string(poses) + " posée(s) en " + std::to_string(pas) + " pas ; sortie « " + py->sortie().toStdString() + " » ; " + py->resultat().erreurs);
    // Point d'arrêt ligne 2, un pas jusqu'à la ligne 3, puis la fin
    py->setCode(QStringLiteral("x = 1\ny = x + 1\nz = x + y\nprint(\"z =\", z)\n"));
    for (int n : py->pointsArret()) py->basculerPointArret(n);
    py->basculerPointArret(2);
    py->executer(false);
    const bool arret2 = attendreQue([py] { return py->ligneArretee() == 2; }, 20'000);
    py->reprendre("pas");
    const bool arret3 = attendreQue([py] { return py->ligneArretee() == 3; }, 20'000);
    py->reprendre("continuer");
    const bool fini = attendreQue([py] { return !py->enCours(); }, 20'000);
    py->basculerPointArret(2);
    verifier(arret2 && arret3 && fini && py->sortie().contains(QStringLiteral("z = 3")), "13.9 : console Python : point d’arrêt à la ligne 2, un pas jusqu’à la ligne 3, puis la fin (bdb)",
             "arrêts " + std::to_string(arret2) + "/" + std::to_string(arret3) + ", fini " + std::to_string(fini) + ", sortie « " + py->sortie().toStdString() + " »");
  }

  // ---------- 13.9 · Cinq valeurs de R2, fréquence de coupure tracée à 1 % ----------
  if (python && py) {
    if (!ngspiceDisponible()) {
      std::printf("SAUTÉ   034 L13 : ngspice n'est pas installé sur ce poste : 13.9 non essayé\n");
      std::fflush(stdout);
    } else {
      const int pas0 = doc->positionHistorique();
      py->setCode(QStringLiteral("import promethee\n"
                                 "valeurs = [1000, 2200, 4700, 10000, 22000]\n"
                                 "fc = []\n"
                                 "for r in valeurs:\n"
                                 "    promethee.valeur(\"R2\", r)\n"
                                 "    fc.append(promethee.mesurer(\"coupure\", \"SORTIE\", promethee.analyse_ac(1, 1e6, 200)))\n"
                                 "promethee.courbe(\"fc(R2)\", valeurs, fc, \"R2 (Ω)\", \"fc (Hz)\")\n"));
      py->executer(true);
      bool juste = false;
      std::string d;
      const auto& courbes = py->trace()->courbes();
      if (courbes.size() == 1 && courbes[0].x.size() == 5 && courbes[0].y.size() == 5) {
        juste = true;
        for (std::size_t i = 0; i < 5; ++i) {
          const double attendue = 1 / (2 * std::numbers::pi * courbes[0].x[i] * 100e-9);
          juste &= std::abs(courbes[0].y[i] - attendue) / attendue < 0.01;
          d += std::to_string(static_cast<int>(courbes[0].x[i])) + " Ω : " + std::to_string(courbes[0].y[i]) + " Hz ; ";
        }
      }
      // Les valeurs données à R2 font un pas, que l'on retire
      if (doc->positionHistorique() > pas0) doc->annuler();
      attendre(100);
      verifier(juste && doc->projet().electronique[0].parReference("R2")->valeur == "1k",
               "13.9 : script de la console qui donne cinq valeurs à R2 et relit V(sortie) : fréquence de coupure tracée en fonction de R2, juste à 1 % en chaque point",
               d + py->resultat().erreurs);
    }
  }

  // ---------- 13.4 · Propositions d'un assistant : refusée, projet inchangé ; acceptée, un pas ----------
  {
    const std::string dossier = dossierEssai("mcp");
    const QString chemin = QString::fromStdString(dossier) + QStringLiteral("/automatisation.prom");
    doc->enregistrer(chemin, true);
    attendre(100);
    prom::ServeurMcp s(doc->projet());
    s.traiter(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"essai","version":"1"}}})");
    s.traiter(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"proposer","arguments":{"methode":"composant.valeur","params":{"ref":"R2","valeur":"4k7"},"raison":"coupure à 340 Hz"}}})");
    s.traiter(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"proposer","arguments":{"methode":"composant.valeur","params":{"ref":"C1","valeur":"47n"},"raison":"coupure à 34 kHz"}}})");
    prom::ecrireFichierAtomique(prom::cheminPropositions(chemin.toStdString()), prom::textePropositions(s.propositions()));
    PanneauPropositions* panneau = a->propositions();
    attendre(200);
    const std::size_t lues = panneau ? panneau->propositions().size() : 0;
    const std::string avant = prom::versJson(doc->projet()).dump();
    const int pas0 = doc->positionHistorique();
    const bool refusee = panneau && !panneau->propositions().empty() && panneau->refuser(panneau->propositions()[0].numero, QStringLiteral("je garde 1 kΩ"));
    const bool inchange = prom::versJson(doc->projet()).dump() == avant && doc->positionHistorique() == pas0;
    const auto relues = prom::lirePropositions(prom::lireFichier(prom::cheminPropositions(chemin.toStdString())));
    const bool fichier = relues.size() == 2 && relues[0].etat == "refusée";
    const bool acceptee = panneau && panneau->propositions().size() == 2 && panneau->accepter(panneau->propositions()[1].numero);
    const bool change = doc->projet().electronique[0].parReference("C1")->valeur == "47n" && doc->positionHistorique() == pas0 + 1;
    doc->annuler();
    attendre(100);
    const bool annule = doc->projet().electronique[0].parReference("C1")->valeur == "100n";
    verifier(lues == 2 && refusee && inchange && fichier && acceptee && change && annule,
             "13.4 : propositions d’un assistant relié par MCP montrées dans le panneau : refusée, projet inchangé ; acceptée, un pas, retiré d’un Ctrl+Z",
             std::to_string(lues) + " lue(s), refus " + std::to_string(refusee) + ", inchangé " + std::to_string(inchange) + ", fichier " + std::to_string(fichier) + ", acceptée " + std::to_string(acceptee) +
                 ", changée " + std::to_string(change) + ", annulée " + std::to_string(annule));
    if (QDockWidget* d = a->findChild<QDockWidget*>(QStringLiteral("dockPropositions"))) d->hide();
    fs::remove_all(fs::path(dossier));
  }

  // ---------- 13.15 · Moteur local : « Expliquer ce bloc » sur l'alimentation, aucune connexion de Prométhée ----------
  if (python) {
    const std::string dossier = dossierEssai("moteur");
    const fs::path moteur = prom::cheminFichier(dossier) / "moteur_local.py";
    prom::ecrireFichierAtomique(moteur, "import sys, re\n"
                                        "invite = sys.stdin.read()\n"
                                        "refs = sorted(set(re.findall(r'^- (U\\d+|C\\d+) : ([^ ;(]+)', invite, re.M)))\n"
                                        "print('Ce bloc est une alimentation linéaire : ' + ', '.join(r + ' (' + v + ')' for r, v in refs) + '.')\n");
    Montage alim;
    alim.c.nom = "Alimentation";
    alim.poser("Regulateur", "U1", "LM7805", {{"1", "+12V"}, {"2", "GND"}, {"3", "+5V"}});
    alim.poser("C", "C1", "330n", {{"1", "+12V"}, {"2", "GND"}});
    alim.poser("C", "C2", "100n", {{"1", "+5V"}, {"2", "GND"}});
    alim.poser("LED", "D1", "rouge", {{"1", "+5V"}, {"2", "GND"}});
    doc->modifier([&alim](prom::Projet& x) { x.electronique.push_back(alim.c); }, QStringLiteral("Alimentation"));
    const int indice = static_cast<int>(doc->projet().electronique.size()) - 1;
    a->ouvrir(indice, AtelierElectronique::Schema);
    attendre(200);
    a->executerAction("assistant.ouvrir");
    attendre(150);
    PanneauAssistant* as = a->assistant();
    prom::MoteurAssistant mt;
    mt.genre = "commande";
    mt.programme = prom::programmeDuPoste("python3").empty() ? "python" : "python3";
    mt.arguments = {"-I", prom::utf8(moteur)};
    as->setMoteur(mt);
    const ConceptionElec& c = doc->projet().electronique[static_cast<std::size_t>(indice)];
    std::set<std::string> bloc;
    for (const char* r : {"U1", "C1", "C2"}) bloc.insert(c.parReference(r)->uuid);
    prom::effacerJournalProgrammes();
    const prom::ReponseAssistant r = as->expliquer(bloc);
    const auto lances = prom::programmesLances();
    const bool panneau = as->conversation().contains(QStringLiteral("U1 (LM7805)")) && as->conversation().contains(QStringLiteral("C2 (100n)"));
    // « Demander à l'assistant » sur une violation : la réponse vient dans le même panneau
    Violation v;
    v.code = "ERC-10";
    v.message = "Broche non connectée : D1.2";
    v.objets = {c.parReference("D1")->uuid};
    a->demanderAssistant(v);
    const bool violation = as->conversation().contains(QStringLiteral("ERC-10")) && as->conversation().contains(QStringLiteral("Une correction reste une proposition"));
    // Mémoire : montrée, suspendue
    prom::MemoireAssistant memo;
    memo.regles = {"Toujours un condensateur de 100 nF au plus près de chaque régulateur"};
    as->setMemoire(memo);
    const bool montree = as->memoire().regles.size() == 1;
    memo.suspendue = true;
    as->setMemoire(memo);
    const bool suspendue = as->memoire().suspendue;
    verifier(r.ok && panneau && lances.size() == 1 && lances[0].front() == mt.programme && violation && montree && suspendue,
             "13.15 : poste hors réseau, modèle local : « Expliquer ce bloc » sur l’alimentation répond dans le panneau, seul le moteur du poste est lancé ; violation confiée, mémoire suspendue",
             r.ok ? r.texte : r.erreur);

    // ---------- 13.16 · Fiche d'un LM317 : 1,5 A et la page qui le dit ----------
    {
      DocumentPdf d;
      d.meta.titre = "LM317 — fiche d'essai";
      using G = BlocPdf::Genre;
      d.blocs = {{G::Titre, "LM317 adjustable positive voltage regulator"},
                 {G::Paragraphe, "Features: output voltage adjustable from 1.25 V to 37 V. Output current in excess of 1.5 A. Internal thermal overload protection."},
                 {G::SautDePage},
                 {G::SousTitre, "Typical application"},
                 {G::Paragraphe, "Vout = 1.25 V x (1 + R2 / R1) + Iadj x R2. Use 240 ohm for R1."},
                 {G::SautDePage},
                 {G::SousTitre, "Electrical characteristics"},
                 {G::Paragraphe, "Reference voltage 1.25 V typical. Current limit 2.2 A typical when the input-output differential is below 15 V."}};
      const QString pdf = QString::fromStdString(dossier) + QStringLiteral("/LM317.pdf");
      prom::ecrireFichierAtomique(pdf.toStdString(), pdfDocument(d));
      const bool lue = as->lireFiche(pdf);
      const prom::ReponseFiche rf = as->questionFiche(QStringLiteral("courant de sortie maximal ?"));
      verifier(lue && rf.trouvee && rf.valeur == "1,5" && rf.unite == "A" && rf.extrait.page == 1 && as->conversation().contains(QStringLiteral("Page 1")),
               "13.16 : « courant de sortie maximal ? » sur la fiche d’un LM317, dans le panneau de l’assistant : 1,5 A, avec l’extrait de la page qui le dit", rf.texte);
    }
    fs::remove_all(fs::path(dossier));
  }

  // ---------- 13.17 · Simulation confiée : fréquence de coupure à 1 %, courbe jointe ----------
  if (ngspiceDisponible()) {
    a->ouvrir(0, AtelierElectronique::Schema);
    attendre(150);
    PanneauAssistant* as = a->assistant();
    const prom::ReponseSimulation r = as->questionSimulation(QStringLiteral("fréquence de coupure de ce filtre ?"));
    const double fc = 1 / (2 * std::numbers::pi * 1000 * 100e-9);
    const bool courbe = as->trace()->isVisible() && as->trace()->courbes().size() == 1 && as->trace()->courbes()[0].x.size() > 10;
    verifier(r.ok && std::abs(r.valeur - fc) / fc < 0.01 && courbe, "13.17 : « fréquence de coupure de ce filtre ? » dans le panneau : analyse lancée par l’assistant, réponse à 1 % de 1/(2πRC), courbe jointe",
             r.ok ? r.texte : r.erreur);
  } else {
    std::printf("SAUTÉ   034 L13 : ngspice n'est pas installé sur ce poste : 13.17 non essayé\n");
    std::fflush(stdout);
  }
  if (QDockWidget* d = a->findChild<QDockWidget*>(QStringLiteral("dockAssistant"))) d->hide();
  if (QDockWidget* d = a->findChild<QDockWidget*>(QStringLiteral("dockPython"))) d->hide();

  // ---------- 13.11 · Requête : objets comptés, requête invalide marquée, règle tirée de la requête ----------
  {
    ConceptionElec c = nouvelleConception("Secteur");
    c.circuit.contour.libre.clear();
    c.circuit.contour.largeur = nm(60);
    c.circuit.contour.hauteur = nm(40);
    c.circuit.pistes.push_back({nouvelUuid(), "SECTEUR", Couche::CuivreDessus, ptMm(5, 10), ptMm(50, 10), nm(0.25)});
    c.circuit.pistes.push_back({nouvelUuid(), "SECTEUR", Couche::CuivreDessus, ptMm(5, 30), ptMm(50, 30), nm(0.25)});
    c.circuit.pistes.push_back({nouvelUuid(), "SIGNAL", Couche::CuivreDessus, ptMm(5, 12.5), ptMm(50, 12.5), nm(0.25)});
    doc->modifier([&c](prom::Projet& x) { x.electronique.push_back(c); }, QStringLiteral("Carte au secteur"));
    const int indice = static_cast<int>(doc->projet().electronique.size()) - 1;
    a->ouvrir(indice, AtelierElectronique::Circuit);
    attendre(200);
    a->executerAction("regles.requetes");
    attendre(150);
    DialogueRequetes* dq = a->requetes();
    dq->setRequete(QStringLiteral("genre == 'piste' && reseau == 'SECTEUR'"));
    const bool compte = dq->valide() && dq->vises().pistes == 2 && dq->compte().contains(QStringLiteral("2 objet"));
    dq->setRequete(QStringLiteral("genre == && largeur"));
    const bool invalide = !dq->valide() && dq->compte().contains(QStringLiteral("invalide"));
    dq->setRequete(QStringLiteral("reseau == 'SECTEUR'"));
    const std::size_t avant = doc->projet().electronique[static_cast<std::size_t>(indice)].circuit.regles.avancees.contraintes.size();
    const bool regle = dq->creerRegle(QStringLiteral("Isolement secteur"), {}, 3.0, true);  // face à tout le reste
    const ConceptionElec& apres = doc->projet().electronique[static_cast<std::size_t>(indice)];
    const Connectivite k = connectivite(apres);
    int signalees = 0;
    for (const auto& v : verifierDRCReglee(apres, k, remplirZones(apres, k)))
      if (v.code == "DRC-01" && v.message.find("SIGNAL") != std::string::npos) ++signalees;
    dq->close();
    verifier(compte && invalide && regle && apres.circuit.regles.avancees.contraintes.size() == avant + 1 && signalees >= 1,
             "13.11 : éditeur de requêtes : objets visés comptés, requête invalide marquée ; règle « isolement 3 mm » tirée de la requête, piste posée à 2,5 mm signalée",
             dq->compte().toStdString() + " ; " + std::to_string(signalees) + " violation(s) SECTEUR/SIGNAL");
  }

  // ---------- 13.14 · Conception guidée : 50 × 30 mm, ESP32 et USB ----------
  {
    a->executerAction("conception.guidee");
    attendre(150);
    DialogueConceptionGuidee* g = a->conceptionGuidee();
    g->regler(50, 30, "esp32-s3-wroom-1", {"usb"});
    const std::size_t avant = doc->projet().electronique.size();
    const ResultatGuide r = g->concevoir();
    attendre(300);
    const bool ajoutee = doc->projet().electronique.size() == avant + 1 && a->conception() == static_cast<int>(avant);
    const ConceptionElec* c = a->conceptionCourante();
    const bool blocs = c && c->circuit.empreintes.size() == 10 && c->parReference("U1") && c->parReference("J1") && c->parReference("U3");
    const auto propositions = g->proposer();
    bool esd = false;
    for (const auto& x : propositions) esd |= x.regle == "esd";
    g->close();
    verifier(r.reussi() && ajoutee && blocs && !esd, "13.14 : carte de 50 × 30 mm, module ESP32 et port USB choisis dans le dialogue : blocs posés et reliés, ouverte, ERC et DRC sans erreur",
             g->journal().toStdString());
  }

  // ---------- Ruban : les actions du lot sont au registre ----------
  {
    const RegistreActions& reg = RegistreActions::atelierElectronique();
    bool toutes = true;
    for (const char* id : {"automatisation.ligne", "automatisation.macro", "automatisation.rejouer", "automatisation.python", "assistant.ouvrir", "assistant.expliquer", "assistant.propositions",
                           "assistant.fiche", "regles.requetes", "conception.guidee", "conception.circuitsTypes", "composant.brochage"})
      toutes &= reg.action(id) != nullptr;
    verifier(toutes, "034 L13 : les commandes de l’automatisation et de l’assistant sont au registre des actions et au ruban", {});
  }
}
