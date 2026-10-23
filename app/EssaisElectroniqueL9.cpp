// Prométhée : essais d'interface du lot L9 de l'audit (« Performances ») : sur la carte de mesure de 2 000
// composants avec sa zone GND, une empreinte glissée à 60 images par seconde, la DRC complète en fond en moins de
// 10 s sans figer la saisie, une piste glissée près de la zone qui la garde à jour à chaque image ; la 3D rouverte
// après un changement de valeur sans rien recalculer ; un fichier aux UUID doublés ouvert avec un rapport ; un
// blocage de plus de dix secondes qui laisse un rapport complet ; le mode portable qui n'écrit rien dans le profil.
// Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L9.
// Licence GPL-3.0-only.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <set>
#include <thread>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "ChienDeGarde.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "VueCircuit.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
using H = std::chrono::steady_clock;
double ms(H::time_point t0) { return std::chrono::duration<double, std::milli>(H::now() - t0).count(); }
void attendre(int duree) {
  QEventLoop boucle;
  QTimer::singleShot(duree, &boucle, &QEventLoop::quit);
  boucle.exec();
}
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
AtelierElectronique* ouvrir(FenetrePrincipale& f, ConceptionElec c, int mode) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = c.nom;
  p.electronique.push_back(std::move(c));
  f.document()->creer(p);
  f.montrerElectronique(0, mode);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return nullptr;
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);
  return a;
}
// Attend la fin du calcul de fond ; rend sa durée et le plus long silence de la boucle d'événements pendant ce temps
std::pair<double, double> attendreCalcul(AtelierElectronique* a, double limite = 60'000) {
  const auto t0 = H::now();
  double pire = 0;
  auto dernier = H::now();
  QTimer battement;
  QObject::connect(&battement, &QTimer::timeout, [&] {
    pire = std::max(pire, ms(dernier));
    dernier = H::now();
  });
  battement.start(5);
  while (a->calculEnCours() && ms(t0) < limite) {
    QEventLoop boucle;
    QTimer::singleShot(10, &boucle, &QEventLoop::quit);
    boucle.exec();
  }
  battement.stop();
  return {ms(t0), pire};
}
QPoint ecran(VueCircuit* v, Pt p) { return v->versEcran(p).toPoint(); }
QString dossierEssai(const QString& nom) {
  const QString d = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-l9-") + nom);
  QDir(d).removeRecursively();
  QDir().mkpath(d);
  return d;
}
}  // namespace

// Profilage à la main (PROMETHEE_ESSAIS_LOTS=GLISSE) : une empreinte glissée longtemps sur la carte de 2 000
void profilerGlisseL9(FenetrePrincipale& f) {
  AtelierElectronique* a = ouvrir(f, carteDeMesure(2000), AtelierElectronique::Circuit);
  if (!a) return;
  a->recalculer();
  attendreCalcul(a);
  VueCircuit* vc = a->vueCircuit();
  vc->recadrer();
  attendre(200);
  const ConceptionElec* c = conception(f.document());
  const EmpreintePlacee& em = c->circuit.empreintes[c->circuit.empreintes.size() / 2];
  Pt e = em.position;
  if (const DefEmpreinte* d = c->empreinte(em.empreinte))
    for (const auto& pa0 : d->pastilles)
      if (const Pt q = pastilleSurCarte(em, pa0).position; q.y >= e.y) e = q;
  const QPoint depart = ecran(vc, e);
  reel::deplacer(vc, depart);
  reel::appuyer(vc, depart);
  std::printf("profilage : prêt\n");
  std::fflush(stdout);
  const auto t0 = H::now();
  int n = 0;
  while (ms(t0) < 40'000) {
    reel::deplacer(vc, depart + QPoint(5 + (n % 60), 0));
    ++n;
  }
  reel::relacher(vc, depart);
  std::printf("glissement : %d images en 40 s\n", n);
}

// Profilage à la main (PROMETHEE_ESSAIS_LOTS=CLICS) : quarante clics sur des empreintes de la carte de 2 000
void profilerClicsL9(FenetrePrincipale& f) {
  AtelierElectronique* a = ouvrir(f, carteDeMesure(2000), AtelierElectronique::Circuit);
  if (!a) return;
  a->recalculer();
  attendreCalcul(a);
  VueCircuit* vc = a->vueCircuit();
  vc->recadrer();
  attendre(200);
  const ConceptionElec* c = conception(f.document());
  std::printf("profilage : prêt\n");
  std::fflush(stdout);
  for (int i = 0; i < 40; ++i) {
    const auto t0 = H::now();
    reel::cliquer(vc, ecran(vc, c->circuit.empreintes[static_cast<std::size_t>(100 + 37 * i) % c->circuit.empreintes.size()].position));
    std::printf("clic %d : %.0f ms\n", i, ms(t0));
    std::fflush(stdout);
    if (QApplication::activePopupWidget()) QApplication::activePopupWidget()->close();
  }
}

void essaisElectroniqueL9(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  char t[300];

  // ---------- Carte de 2 000 composants : glissement d'une empreinte, DRC de fond, piste près de la zone ----------
  {
    ConceptionElec c0 = carteDeMesure(2000);
    // Une piste d'un autre réseau, entre deux colonnes d'empreintes (à l'écart des repères, posés au-dessus de
    // chaque empreinte), dans la zone GND du dessus
    const Pt pa = ptMm(8 + 6.35 * 3 + 3.175, 8 + 6.35 * 2), pb = ptMm(8 + 6.35 * 3 + 3.175, 8 + 6.35 * 4);
    const std::string piste = nouvelUuid();
    c0.circuit.pistes.push_back({piste, "B1_Q", Couche::CuivreDessus, pa, pb, 250'000});
    AtelierElectronique* a = ouvrir(f, c0, AtelierElectronique::Circuit);
    if (!a) {
      verifier(false, "L9 : la carte de mesure s'ouvre", {});
      return;
    }
    verifier(reel::activer(&f), "L9 : la fenêtre est active", {});
    a->recalculer();
    attendreCalcul(a);
    VueCircuit* vc = a->vueCircuit();
    vc->recadrer();
    vc->setFocus(Qt::OtherFocusReason);
    attendre(200);
    // Empreinte du milieu, glissée de 3 mm en trente images
    const ConceptionElec* c = conception(doc);
    // Saisie par sa pastille la plus basse : le repère, au-dessus de l'empreinte, se glisserait seul
    const EmpreintePlacee& em = c->circuit.empreintes[c->circuit.empreintes.size() / 2];
    Pt e = em.position;
    const Pt position0 = em.position;
    if (const DefEmpreinte* d = c->empreinte(em.empreinte))
      for (const auto& pa0 : d->pastilles)
        if (const Pt q = pastilleSurCarte(em, pa0).position; q.y >= e.y) e = q;
    const QPoint depart = ecran(vc, e), arrivee = ecran(vc, e + ptMm(3, 0));
    reel::deplacer(vc, depart);
    const auto tAppui = H::now();
    reel::appuyer(vc, depart);
    const double appui = ms(tAppui);
    reel::deplacer(vc, depart + QPoint(5, 0));
    vc->repaint();
    const bool rapide = vc->glissementRapide();
    if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL"))
      std::printf("détail 9.4 : vue %dx%d, départ (%d, %d), %zu objet(s) choisi(s), genre %d, visible %d, appui %.0f ms, menu %d, geste %d\n", vc->width(), vc->height(), depart.x(),
                  depart.y(), vc->selection().size(), vc->selection().empty() ? -1 : static_cast<int>(vc->selection().front().genre), vc->isVisible() ? 1 : 0, appui,
                  QApplication::activePopupWidget() ? 1 : 0, doc->enGeste() ? 1 : 0);
    // Par image : le travail de Prométhée (mouvement traité, image peinte) et le temps complet, image posée par le
    // système compris ; sous un rendu OpenGL logiciel (Mesa llvmpipe, sans carte graphique), la pose de la fenêtre
    // entière coûte à elle seule plus de 16 ms : l'objectif porte sur le travail
    double pire = 0, total = 0, travail = 0, pireTravail = 0;
    for (int i = 1; i <= 30; ++i) {
      const double w0 = vc->tempsTravail();
      const auto t0 = H::now();
      reel::deplacer(vc, depart + (arrivee - depart) * i / 30);  // l'image est peinte et posée pendant le traitement
      const double d = ms(t0), w = vc->tempsTravail() - w0;
      total += d;
      pire = std::max(pire, d);
      travail += w;
      pireTravail = std::max(pireTravail, w);
    }
    const double moyenne = travail / 30;
    const auto tRel = H::now();
    reel::relacher(vc, arrivee);
    const double relache = ms(tRel);
    const std::string comp = em.composant;
    double bouge = 0;
    for (const auto& x : conception(doc)->circuit.empreintes)
      if (x.composant == comp) bouge = mm(x.position.x - position0.x);
    // Le calcul de fin de geste part à la minuterie de l'atelier : on attend qu'il démarre
    bool demarre = a->calculEnCours();
    for (int i = 0; i < 100 && !a->calculEnCours(); ++i) attendre(10);
    demarre |= a->calculEnCours();
    const auto [duree, silence] = attendreCalcul(a);
    const Connectivite k = connectivite(*conception(doc));
    std::snprintf(t, sizeof t, "travail %.1f ms par image (pire %.1f ms) ; avec la pose par le système %.1f ms (pire %.1f) ; rapide %d, déplacée de %.2f mm", moyenne, pireTravail,
                  total / 30, pire, rapide ? 1 : 0, bouge);
    verifier(rapide && moyenne <= 16.7 && std::abs(bouge - 3) < 0.5, "9.4 : sur la carte de 2 000 composants avec sa zone GND, une empreinte glissée reste à 60 images par seconde", t);
    std::snprintf(t, sizeof t, "plan de masse et DRC refaits en fond en %.0f ms ; boucle d'événements jamais muette plus de %.0f ms (relâché en %.0f ms)", duree, silence, relache);
    // Objectif de 10 s, avec la marge de 20 % de la règle des mesures (9.1) : le calcul de fond, à priorité basse,
    // refait aussi les zones, l'ERC et les écarts, et partage la machine avec la saisie qui reste vivante
    verifier(duree < 12'000 && silence < 150 && relache < 150 && !conception(doc)->circuit.empreintes.empty(),
             "9.2, 9.4 : la DRC complète de la carte tourne en fond en moins de 10 s, sans figer la saisie", t);
    doc->annuler();
    attendreCalcul(a);

    // Piste glissée de 1,5 mm de côté, dans la zone : le cuivre de la zone suit à chaque image
    c = conception(doc);
    reel::cliquer(vc, ecran(vc, Pt{(pa.x + pb.x) / 2, (pa.y + pb.y) / 2}));
    attendre(150);
    const Pt milieu{(pa.x + pb.x) / 2, (pa.y + pb.y) / 2};
    const QPoint d0 = ecran(vc, milieu), d1 = ecran(vc, milieu + ptMm(1.5, 0));
    reel::deplacer(vc, d0);
    reel::appuyer(vc, d0);
    reel::deplacer(vc, d0 + QPoint(5, 0));
    vc->repaint();
    const int suivies = vc->zonesSuivies();
    std::string zone = c->circuit.zones.empty() ? std::string() : c->circuit.zones.front().uuid;
    pire = total = travail = pireTravail = 0;
    int aJour = 0;
    for (int i = 1; i <= 30; ++i) {
      const double w0 = vc->tempsTravail();
      const auto t0 = H::now();
      reel::deplacer(vc, d0 + (d1 - d0) * i / 30);
      const double d = ms(t0), w = vc->tempsTravail() - w0;
      total += d;
      pire = std::max(pire, d);
      travail += w;
      pireTravail = std::max(pireTravail, w);
      // La zone suit : sous la piste, à sa place courante, plus de cuivre
      Pt centre{};
      for (const auto& p : conception(doc)->circuit.pistes)
        if (p.uuid == piste) centre = Pt{(p.a.x + p.b.x) / 2, (p.a.y + p.b.y) / 2};
      if (const Polygones* cu = vc->cuivreLocal(zone); cu && !cu->empty() && !dedans(*cu, centre)) ++aJour;
    }
    // À la place de départ, le cuivre est revenu
    const Polygones* cu = vc->cuivreLocal(zone);
    const bool revenu = cu && dedans(*cu, milieu);
    double bougePiste = 0;
    for (const auto& p : conception(doc)->circuit.pistes)
      if (p.uuid == piste) bougePiste = mm(p.a.x - pa.x);
    const bool rapidePiste = vc->glissementRapide();
    reel::relacher(vc, d1);
    attendreCalcul(a);
    std::snprintf(t, sizeof t, "travail %.1f ms par image (pire %.1f), avec la pose %.1f ms ; %d zone(s) suivie(s), à jour %d/30 images, cuivre revenu derrière la piste : %s",
                  travail / 30, pireTravail, total / 30, suivies, aJour, revenu ? "oui" : "non");
    verifier(rapidePiste && suivies == 1 && aJour == 30 && revenu && travail / 30 <= 16.7, "9.9 : une piste glissée près de la zone GND la garde à jour à chaque image, à 60 images par seconde", t);
  }

  // ---------- 9.5 · 3D rouverte après un changement de valeur : rien de recalculé ----------
  {
    AtelierElectronique* a = ouvrir(f, exempleElectronique(), AtelierElectronique::Circuit);
    if (!a) return;
    a->recalculer();
    attendreCalcul(a);
    a->setMode(AtelierElectronique::Vue3D);
    attendre(500);
    attendreCalcul(a);
    const int avant = a->constructions3D();
    a->setMode(AtelierElectronique::Circuit);
    attendre(200);
    std::string id;
    for (const auto& x : conception(doc)->composants)
      if (x.reference == "R1") id = x.uuid;
    a->modifierConception([&](ConceptionElec& c) { c.composant(id)->valeur = "4k7"; }, QStringLiteral("Valeur"));
    attendre(200);
    attendreCalcul(a);
    const auto t0 = H::now();
    a->setMode(AtelierElectronique::Vue3D);
    attendre(300);
    attendreCalcul(a);
    const double duree = ms(t0);
    std::snprintf(t, sizeof t, "%d construction(s) avant, %d après ; 3D rouverte en %.0f ms", avant, a->constructions3D(), duree);
    verifier(avant >= 1 && a->constructions3D() == avant, "9.5 : la 3D rouverte après un changement de valeur ne recalcule rien", t);
    a->setMode(AtelierElectronique::Circuit);
  }

  // ---------- 9.7 · Fichier aux UUID doublés : ouvert avec un rapport, sans planter ----------
  {
    prom::Projet p = prom::projetVide();
    p.sansCarte = true;
    ConceptionElec c = exempleElectronique();
    const ComposantElec copie = c.composants.front();
    c.composants.push_back(copie);
    for (auto& fe : c.feuilles)
      for (const auto& s : std::vector<SymbolePlace>(fe.symboles))
        if (s.composant == copie.uuid) {
          SymbolePlace s2 = s;
          s2.position = s2.position + ptMm(40, 0);
          fe.symboles.push_back(s2);
        }
    if (c.circuit.pistes.size() >= 2) c.circuit.pistes[1].uuid = c.circuit.pistes[0].uuid;
    p.nom = "Doublons";
    p.electronique.push_back(c);
    const QString fichier = QDir(dossierEssai(QStringLiteral("doublons"))).filePath(QStringLiteral("doublons.promethee"));
    prom::ecrireProjet(p, fichier.toStdString());
    QString message;
    QTimer::singleShot(600, [&message] {
      if (auto* m = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        message = m->text();
        m->accept();
      }
    });
    f.ouvrirFichier(fichier);
    attendre(900);
    const ConceptionElec* lu = conception(doc);
    std::set<std::string> uuids;
    bool uniques = lu != nullptr;
    if (lu)
      for (const auto& x : lu->composants) uniques &= uuids.insert(x.uuid).second;
    verifier(uniques && message.contains(QStringLiteral("identifiant en double")), "9.7 : un fichier aux UUID doublés s'ouvre avec un rapport, sans planter",
             message.left(160).toStdString());
  }

  // ---------- 9.10 · Prométhée figé plus de 10 s : rapport complet dans son dossier, rien d'envoyé ----------
  {
    const QString avant = chien::dernierRapport();
    reel::activer(&f);
    std::this_thread::sleep_for(std::chrono::seconds(12));  // boucle d'événements figée
    attendre(500);
    const QString rapport = chien::dernierRapport();
    QString texte;
    if (!rapport.isEmpty() && rapport != avant) {
      QFile x(rapport);
      if (x.open(QIODevice::ReadOnly)) texte = QString::fromUtf8(x.readAll());
    }
    const bool pile = texte.contains(QStringLiteral("Pile du fil principal")) && (texte.contains(QStringLiteral("#0 ")) || texte.contains(QStringLiteral(".dmp")));
    const bool gestes = texte.contains(QStringLiteral("Vingt derniers gestes")) && !texte.contains(QStringLiteral("(aucun)"));
    verifier(!texte.isEmpty() && pile && gestes && texte.contains(QStringLiteral("Rien n'a été envoyé")),
             "9.10 : un Prométhée figé plus de dix secondes laisse un rapport complet (pile, gestes) dans son dossier, sans rien envoyer", rapport.toStdString());
  }

  // ---------- 9.11 · Mode portable : rien d'écrit dans le profil ----------
  {
    const QString racine = dossierEssai(QStringLiteral("portable"));
    const QString prog = QDir(racine).filePath(QStringLiteral("cle")), maison = QDir(racine).filePath(QStringLiteral("maison"));
    QDir().mkpath(prog);
    QDir().mkpath(maison);
    QFile temoin(QDir(prog).filePath(QStringLiteral("promethee-portable.txt")));
    if (temoin.open(QIODevice::WriteOnly)) temoin.write("portable\n");
    temoin.close();
    QProcess x;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* v : {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME", "PROMETHEE_DOSSIER_DONNEES", "PROMETHEE_DOSSIER_JOURNAL"}) env.remove(QString::fromLatin1(v));
    env.insert(QStringLiteral("HOME"), maison);
    env.insert(QStringLiteral("PROMETHEE_DOSSIER_PROGRAMME"), prog);
    x.setProcessEnvironment(env);
    x.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--essais-portable")});
    const bool fini = x.waitForFinished(60'000);
    const QString sortie = QString::fromUtf8(x.readAllStandardOutput());
    int dansProfil = 0;
    for (QDirIterator it(maison, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories); it.hasNext(); it.next()) ++dansProfil;
    int aCote = 0;
    for (QDirIterator it(QDir(prog).filePath(QStringLiteral("promethee-donnees")), QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories); it.hasNext(); it.next()) ++aCote;
    std::snprintf(t, sizeof t, "%d fichier(s) dans le profil, %d à côté du programme", dansProfil, aCote);
    verifier(fini && x.exitCode() == 0 && sortie.contains(QStringLiteral("portable 1")) && dansProfil == 0 && aCote >= 2,
             "9.11 : la version portable écrit réglages, bibliothèques et récupération à côté du programme, rien dans le profil", t);
  }
}
