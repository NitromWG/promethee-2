// Prométhée : essais d'interface du lot L11 de l'audit (« Simulation ») : filtre RC simulé depuis la fenêtre de
// simulation, coupure mesurée à 1 % de 1/(2πRC) sans figer l'interface ; astable à 555, fréquence à 5 % ; un
// composant réglé pendant la simulation (courbe précédente gardée), sa valeur écrite dans le schéma en un pas ; nœud
// flottant signalé avant le calcul.
// Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L11.
// Licence GPL-3.0-only.
#include <chrono>
#include <cmath>
#include <functional>
#include <numbers>
#include <string>

#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QLabel>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "FenetreSimulation.hpp"
#include "promethee/modele.hpp"
#include "promethee/simulation.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
using H = std::chrono::steady_clock;
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
struct Montage {
  ConceptionElec c = nouvelleConception("Montage");
  double x = 25.4;
  void poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, 50.8));
    x += 25.4;
    for (auto& p : c.feuilles[0].symboles)
      if (p.uuid == s) {
        ComposantElec* k = c.composant(p.composant);
        k->reference = ref;
        k->valeur = valeur;
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
  }
};
AtelierElectronique* ouvrir(FenetrePrincipale& f, ConceptionElec c) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = c.nom;
  p.electronique.push_back(std::move(c));
  f.document()->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Schema);
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
// Lance le calcul et attend sa fin ; rend sa durée et le plus long silence de la boucle d'événements
std::pair<double, double> calculer(FenetreSimulation* s) {
  double pire = 0;
  auto dernier = H::now();
  QTimer battement;
  QObject::connect(&battement, &QTimer::timeout, [&] {
    pire = std::max(pire, std::chrono::duration<double, std::milli>(H::now() - dernier).count());
    dernier = H::now();
  });
  battement.start(5);
  const auto t0 = H::now();
  QEventLoop boucle;
  QObject::connect(s, &FenetreSimulation::calculTermine, &boucle, &QEventLoop::quit);
  QTimer::singleShot(60'000, &boucle, &QEventLoop::quit);
  s->lancer();
  if (s->enCours()) boucle.exec();
  battement.stop();
  return {std::chrono::duration<double, std::milli>(H::now() - t0).count(), pire};
}
}  // namespace

void essaisElectroniqueL11(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  if (!ngspiceDisponible()) {
    verifier(false, "L11 : ngspice est installé sur le poste d'essai", "bibliothèque libngspice introuvable");
    return;
  }

  // ---------- 11.3 · Filtre RC : coupure à 1 % de 1/(2πRC), interface jamais figée ----------
  {
    Montage m;
    m.poser("Source_tension", "V1", "DC 0 AC 1", {{"1", "ENTREE"}, {"2", "GND"}});
    m.poser("R", "R1", "1k", {{"1", "ENTREE"}, {"2", "SORTIE"}});
    m.poser("C", "C1", "100n", {{"1", "SORTIE"}, {"2", "GND"}});
    AtelierElectronique* a = ouvrir(f, m.c);
    if (!a) {
      verifier(false, "L11 : le montage d'essai s'ouvre", {});
      return;
    }
    verifier(reel::activer(&f), "L11 : la fenêtre est active", {});
    a->executerAction("simulation.ouvrir");
    attendre(300);
    FenetreSimulation* s = a->simulation();
    if (!s) {
      verifier(false, "L11 : la fenêtre de simulation s'ouvre", {});
      return;
    }
    AnalyseSpice an;
    an.type = AnalyseSpice::Type::Alternatif;
    an.fDebut = 10;
    an.fFin = 1e6;
    an.pointsParDecade = 50;
    s->setAnalyse(an);
    s->sonder({"SORTIE"});
    const auto [duree, silence] = calculer(s);
    double fc = 0;
    if (const auto* fr = s->resultat().reel("frequency"))
      if (const auto* h = s->resultat().complexe(vecteurTension(s->netlist(), "SORTIE"))) fc = frequenceCoupure(*fr, *h);
    const double attendue = 1 / (2 * std::numbers::pi * 1000 * 100e-9);
    char t[200];
    std::snprintf(t, sizeof t, "coupure %.1f Hz pour %.1f Hz ; calcul %.0f ms, boucle jamais muette plus de %.0f ms ; %zu courbe(s)", fc, attendue, duree, silence, s->trace()->courbes().size());
    verifier(std::abs(fc - attendue) / attendue < 0.01 && silence < 150 && !s->trace()->courbes().empty() && s->mesures().contains(QStringLiteral("−3 dB")),
             "11.3 : filtre RC simulé depuis la fenêtre : coupure à 1 % de 1/(2πRC), interface jamais figée", t);

    // ---------- 11.6 · R1 réglée ×2 : nouvelle coupure, courbe précédente gardée ; valeur écrite en un pas ----------
    QEventLoop boucle;
    QObject::connect(s, &FenetreSimulation::calculTermine, &boucle, &QEventLoop::quit);
    QTimer::singleShot(60'000, &boucle, &QEventLoop::quit);
    s->regler("R1", 2.0);
    if (s->enCours()) boucle.exec();
    double fc2 = 0;
    if (const auto* fr = s->resultat().reel("frequency"))
      if (const auto* h = s->resultat().complexe(vecteurTension(s->netlist(), "SORTIE"))) fc2 = frequenceCoupure(*fr, *h);
    bool attenuee = false;
    for (const auto& k : s->trace()->courbes()) attenuee |= k.attenuee;
    const bool schemaIntact = conception(doc)->parReference("R1")->valeur == "1k";
    const bool ecrite = s->appliquerReglage() && conception(doc)->parReference("R1")->valeur != "1k";
    doc->annuler();
    attendre(150);
    std::snprintf(t, sizeof t, "coupure %.1f Hz (attendue %.1f Hz) ; R1 écrite « %s » puis défaite", fc2, attendue / 2, conception(doc)->parReference("R1")->valeur.c_str());
    verifier(std::abs(fc2 - attendue / 2) / (attendue / 2) < 0.02 && attenuee && schemaIntact && ecrite && conception(doc)->parReference("R1")->valeur == "1k",
             "11.6 : R1 réglée pendant la simulation : courbes mises à jour, l'ancienne gardée ; la valeur s'écrit dans le schéma en un pas", t);
    s->close();
  }

  // ---------- 11.4 · Astable à 555 : fréquence à 5 % de 1,44/((R1 + 2R2)C) ----------
  {
    Montage m;
    m.poser("Source_tension", "V1", "5", {{"1", "VCC"}, {"2", "GND"}});
    m.poser("NE555", "U1", "NE555", {{"8", "VCC"}, {"4", "VCC"}, {"1", "GND"}, {"7", "DIS"}, {"6", "THR"}, {"2", "THR"}, {"3", "SORTIE"}, {"5", "CV"}});
    m.poser("R", "R1", "1k", {{"1", "VCC"}, {"2", "DIS"}});
    m.poser("R", "R2", "10k", {{"1", "DIS"}, {"2", "THR"}});
    m.poser("C", "C1", "10n", {{"1", "THR"}, {"2", "GND"}});
    m.poser("C", "C2", "10n", {{"1", "CV"}, {"2", "GND"}});
    m.poser("R", "R3", "1k", {{"1", "SORTIE"}, {"2", "GND"}});
    AtelierElectronique* a = ouvrir(f, m.c);
    if (!a) return;
    a->ouvrirSimulation();
    attendre(300);
    FenetreSimulation* s = a->simulation();
    AnalyseSpice an;
    an.pas = 0.5e-6;
    an.duree = 3e-3;
    s->setAnalyse(an);
    s->sonder({"SORTIE", "THR"});
    calculer(s);
    double fr = 0;
    if (const auto* tt = s->resultat().reel("time"))
      if (const auto* v = s->resultat().reel(vecteurTension(s->netlist(), "SORTIE"))) fr = frequenceMesuree(*tt, *v);
    const double attendue = 1.44 / ((1000 + 2 * 10'000) * 10e-9);
    char t[160];
    std::snprintf(t, sizeof t, "%.0f Hz pour %.0f Hz", fr, attendue);
    verifier(std::abs(fr - attendue) / attendue < 0.05 && s->mesures().contains(QStringLiteral("fréquence")), "11.4 : astable à 555 : fréquence mesurée à 5 % de 1,44/((R1 + 2R2)C)", t);
    s->close();
  }

  // ---------- 11.2 · Nœud sans chemin vers la masse : signalé avant le calcul ----------
  {
    Montage m;
    m.poser("Source_tension", "V1", "5", {{"1", "A"}, {"2", "GND"}});
    m.poser("C", "C1", "1u", {{"1", "A"}, {"2", "FLOTTANT"}});
    m.poser("C", "C2", "1u", {{"1", "FLOTTANT"}, {"2", "GND"}});
    AtelierElectronique* a = ouvrir(f, m.c);
    if (!a) return;
    a->ouvrirSimulation();
    attendre(300);
    FenetreSimulation* s = a->simulation();
    calculer(s);
    QLabel* etat = s->findChild<QLabel*>(QStringLiteral("etatSimulation"));
    verifier(!s->resultat().reussi() && etat && etat->text().contains(QStringLiteral("FLOTTANT")), "11.2 : un nœud sans chemin vers la masse est signalé avant le calcul",
             etat ? etat->text().left(120).toStdString() : std::string());
    s->close();
  }
}
