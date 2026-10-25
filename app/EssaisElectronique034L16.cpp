// Prométhée : essais d'interface de la 0.34, lot L16 (platine d'essai) — fil tiré à la souris entre R1 et la LED :
// liaison au schéma et au chevelu, en un pas d'annulation (16.2) ; clignotant à NE555 monté de lui-même sans écart,
// patte décalée en rouge et écart listé (16.3) ; interrupteur cliqué sur la platine en 3D pendant la simulation : la
// LED s'allume, composant déplacé gardant ses liaisons (16.4) ; positions du sélecteur d'une guitare et documents
// de câblage écrits (16.6) ; poste neuf vérifié par ses essais intégrés, réglages de l'établissement appliqués
// (16.10) ; consigne de trois étapes validées une à une sur le projet de l'élève (16.11) ; panne cachée « R2 coupée »
// lue au voltmètre, partie verrouillée refusée, sceau relevé (16.12) ; montage de l'élève reconnu, résistance en trop
// signalée (16.13) ; page HTML du filtre RC ouverte hors ligne, devoir publié, rendu, noté (16.14). Appelés seuls avec
// PROMETHEE_ESSAIS_LOTS=034L16. Licence GPL-3.0-only.
#include <filesystem>
#include <functional>
#include <random>
#include <set>
#include <string>

#include <QApplication>
#include <QCoreApplication>
#include <QDirIterator>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QListWidget>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "Enseignement034.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "Laboratoire034.hpp"
#include "Platine034.hpp"
#include "promethee/archive.hpp"
#include "promethee/electronique.hpp"
#include "promethee/enseignement.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/platine.hpp"
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
  void poser(const std::string& symbole, const std::string& ref, const std::string& valeur, const std::vector<std::pair<std::string, std::string>>& liaisons) {
    const std::string s = poserSymbole(c, 0, symbole, ptMm(x, y));
    x += 45.72;
    if (x > 260) x = 25.4, y += 60.96;
    for (const auto& p : std::vector<SymbolePlace>(c.feuilles[0].symboles))
      if (p.uuid == s) {
        ComposantElec* r = c.composant(p.composant);
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
  }
};
std::string dossierEssai(const std::string& nom) {
  std::random_device r;
  const fs::path p = fs::temp_directory_path() / ("promethee-essai-l16-" + nom + "-" + std::to_string(r()));
  fs::create_directories(p);
  return prom::utf8(p);
}
// Liaison du chevelu entre deux pastilles
bool auChevelu(const ConceptionElec& c, const std::string& a, const std::string& na, const std::string& b, const std::string& nb) {
  const Connectivite k = connectivite(c);
  Pt pa{}, pb{};
  bool ta = false, tb = false;
  for (const auto& p : pastilles(c, k)) {
    if (p.composant == a && p.numero == na) pa = p.pastille.position, ta = true;
    if (p.composant == b && p.numero == nb) pb = p.pastille.position, tb = true;
  }
  if (!ta || !tb) return false;
  for (const auto& l : etatRoutage(c, k, remplirZones(c, k)).chevelu)
    if ((l.a == pa && l.b == pb) || (l.a == pb && l.b == pa)) return true;
  return false;
}
}  // namespace

void essaisElectronique034L16(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();

  // ---------- Les montages ----------
  Montage led("Diode");
  led.poser("R", "R1", "330", {{"1", "+5V"}});
  led.poser("LED", "D1", "rouge", {{"1", "GND"}});
  mettreAJourCircuit(led.c);
  Montage inter("Interrupteur");
  inter.poser("Pile", "BT1", "5V", {{"1", "+5V"}, {"2", "GND"}});
  inter.poser("Interrupteur", "SW1", "", {{"1", "+5V"}, {"2", "CMD"}});
  inter.poser("R", "R1", "330", {{"1", "CMD"}, {"2", "A"}});
  inter.poser("LED", "D1", "rouge", {{"2", "A"}, {"1", "GND"}});
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = "Platine";
  p.electronique = {led.c, exempleElectronique(), inter.c, exempleGuitareTroisMicros()};
  enum { DIODE, NE555, INTERRUPTEUR, GUITARE };
  doc->creer(p);
  f.montrerElectronique(DIODE, AtelierElectronique::Schema);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) {
    verifier(false, "034 L16 : l'atelier électronique s'ouvre", {});
    return;
  }
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);

  // ---------- 16.2 · Fil tiré à la souris ----------
  a->executerAction("platine.ouvrir");
  attendre(300);
  FenetrePlatine* pl = a->platine();
  auto* onglets = pl ? pl->findChild<QTabWidget*>(QStringLiteral("ongletsPlatine")) : nullptr;
  verifier(pl && pl->isVisible() && onglets && onglets->currentWidget()->objectName() == QLatin1String("platine"), "034 L16 : « Platine… » du ruban ouvre la platine d'essai", {});
  if (!pl) return;
  if (auto* b = pl->findChild<QPushButton*>(QStringLiteral("monterPlatine"))) reel::cliquer(b, b->rect().center());
  attendre(250);
  {
    const ConceptionElec* c = a->conceptionCourante();
    const std::string r1 = c->parReference("R1")->uuid, d1 = c->parReference("D1")->uuid;
    const auto ha = pl->trouDe(QStringLiteral("R1"), QStringLiteral("2")), hb = pl->trouDe(QStringLiteral("D1"), QStringLiteral("2"));
    const auto ta = ha ? pl->trouLibre(*ha) : std::nullopt, tb = hb ? pl->trouLibre(*hb) : std::nullopt;
    const std::size_t fils0 = platineDe(*c).fils.size();
    const bool avant = auChevelu(*c, r1, "2", d1, "2");
    const int pas0 = doc->positionHistorique();
    if (ta && tb) reel::glisser(pl->vue(), pl->vue()->pointDuTrou(*ta).toPoint(), pl->vue()->pointDuTrou(*tb).toPoint());
    attendre(250);
    c = a->conceptionCourante();
    const Connectivite k = connectivite(*c);
    const bool relie = k.reseau(r1, "2") >= 0 && k.reseau(r1, "2") == k.reseau(d1, "2");
    const bool chevelu = auChevelu(*c, r1, "2", d1, "2");
    const bool unPas = doc->positionHistorique() == pas0 + 1;
    const bool fil = platineDe(*c).fils.size() == fils0 + 1;
    doc->annuler();
    attendre(250);
    c = a->conceptionCourante();
    const Connectivite k2 = connectivite(*c);
    const bool annule = (k2.reseau(r1, "2") < 0 || k2.reseau(r1, "2") != k2.reseau(d1, "2")) && platineDe(*c).fils.size() == fils0 && !auChevelu(*c, r1, "2", d1, "2");
    verifier(ta && tb && !avant && relie && chevelu && unPas && fil && annule,
             "034 L16 · 16.2 : fil ajouté sur la platine entre R1 et la LED : la liaison apparaît au schéma et au chevelu du circuit, en un seul pas d'annulation",
             std::string(relie ? "relié" : "non relié") + (chevelu ? ", au chevelu" : ", absent du chevelu") + (unPas ? ", un pas" : ", pas : " + std::to_string(doc->positionHistorique() - pas0)) +
                 (annule ? ", annulé" : ", annulation incomplète"));
  }

  // ---------- 16.3 · NE555 monté de lui-même ----------
  f.montrerElectronique(NE555, AtelierElectronique::Schema);
  attendre(300);
  pl = a->platine(QStringLiteral("platine"));
  if (auto* b = pl->findChild<QPushButton*>(QStringLiteral("monterPlatine"))) reel::cliquer(b, b->rect().center());
  attendre(300);
  {
    auto* liste = pl->findChild<QListWidget*>(QStringLiteral("ecartsPlatine"));
    const auto u1 = pl->trouDe(QStringLiteral("U1"), QStringLiteral("1")), u8 = pl->trouDe(QStringLiteral("U1"), QStringLiteral("8"));
    const bool cheval = u1 && u8 && u1->rangee == "f" && u8->rangee == "e" && u1->colonne == u8->colonne;
    const bool sansEcart = pl->ecarts().empty() && liste && liste->count() == 0;
    bool vertes = true;
    for (const auto& [b, e] : etatsPattes(*a->conceptionCourante(), pl->vue()->vue())) vertes &= e != EtatPatte::Rouge;
    verifier(cheval && sansEcart && vertes, "034 L16 · 16.3 : clignotant à NE555 monté de lui-même, le NE555 à cheval sur la rainure : aucun écart avec le schéma, pattes vertes",
             std::to_string(pl->ecarts().size()) + " écart(s)" + (pl->ecarts().empty() ? std::string() : " : " + pl->ecarts().front().message));
    // Patte de R3 décalée d'un trou, vers un trou libre d'un autre potentiel
    const VuePlatine v = pl->vue()->vue();
    const ConnectivitePlatine kp = connectivitePlatine(v);
    std::set<Trou> occupes;
    for (const auto& x : v.poses)
      for (const auto& [n, t] : x.broches) occupes.insert(t);
    for (const auto& w : v.fils) occupes.insert(w.a), occupes.insert(w.b);
    const auto avant = pl->trouDe(QStringLiteral("R3"), QStringLiteral("1"));
    std::optional<Trou> apres;
    if (avant) {
      const std::vector<std::string> moitie = avant->rangee < "f" ? std::vector<std::string>{"a", "b", "c", "d", "e"} : std::vector<std::string>{"f", "g", "h", "i", "j"};
      for (int d : {1, -1}) {
        for (const auto& r : moitie) {
          const Trou t{avant->colonne + d, r};
          if (trouValide(v.modele, t) && !occupes.count(t) && kp.noeudDuTrou(t) != kp.noeudDuTrou(*avant)) {
            apres = t;
            break;
          }
        }
        if (apres) break;
      }
    }
    const bool decale = apres && pl->deplacerPatte(QStringLiteral("R3"), QStringLiteral("1"), *apres);
    attendre(200);
    const bool rouge = pl->etatPatte(QStringLiteral("R3"), QStringLiteral("1")) == EtatPatte::Rouge;
    int ligne = -1;
    for (int i = 0; liste && i < liste->count(); ++i)
      if (liste->item(i)->text().contains(QStringLiteral("R3.1"))) ligne = i;
    if (liste && ligne >= 0) liste->setCurrentRow(ligne);
    attendre(150);
    verifier(decale && rouge && ligne >= 0, "034 L16 · 16.3 : une patte décalée d'un trou donne un point rouge et un écart listé, qui mène au trou",
             ligne >= 0 ? liste->item(ligne)->text().toStdString() : std::string("aucun écart listé"));
    doc->annuler();
    attendre(200);
  }

  // ---------- 16.4 · Interrupteur cliqué en 3D pendant la simulation ----------
  f.montrerElectronique(INTERRUPTEUR, AtelierElectronique::Schema);
  attendre(300);
  pl = a->platine(QStringLiteral("platine"));
  if (auto* b = pl->findChild<QPushButton*>(QStringLiteral("etapePlatine"))) reel::cliquer(b, b->rect().center());
  attendre(200);
  const std::size_t uneEtape = platineDe(*a->conceptionCourante()).poses.size();
  pl->monter();
  attendre(200);
  {
    QString e;
    const bool bouge = pl->deplacerComposant(QStringLiteral("R1"), 4, 0, &e);
    attendre(200);
    verifier(uneEtape == 1 && bouge && pl->ecarts().empty(), "034 L16 · 16.3, 16.4 : montage étape par étape ; un composant déplacé sur la platine garde ses liaisons",
             e.toStdString() + (pl->ecarts().empty() ? std::string() : pl->ecarts().front().message));
  }
  if (!ngspiceDisponible()) {
    verifier(true, "034 L16 · 16.4 : simulation de la platine en 3D non essayée (ngspice absent)", {});
  } else {
    FenetreLaboratoire* lab = a->laboratoire(QStringLiteral("continu"));
    QString erreur;
    const bool demarre = lab->demarrer(&erreur);
    attendre(500);
    pl = a->platine(QStringLiteral("3d"));
    attendre(400);
    const bool eteinte = !lab->ledAllumee(QStringLiteral("D1"));
    const auto centre = pl->vue3D()->centre("SW1");
    if (centre) reel::cliquer(pl->vue3D(), centre->toPoint());
    QPushButton* commande = lab->commande(QStringLiteral("SW1"));
    const auto touche = centre ? pl->vue3D()->objetSous(*centre) : std::nullopt;
    const std::string etatClic = (commande ? (commande->isChecked() ? " SW1 fermé" : " SW1 ouvert") : " commande SW1 absente") + std::string(touche ? " ; sous le clic : " + touche->reference : " ; rien sous le clic");
    const bool allumee = attendreQue([&] { return lab->ledAllumee(QStringLiteral("D1")); }, 4000);
    const bool vue = attendreQue([&] { return pl->vue3D()->ledsAllumees().count("D1") > 0; }, 2000);
    if (centre) reel::cliquer(pl->vue3D(), centre->toPoint());
    const bool rallumee = !attendreQue([&] { return !lab->ledAllumee(QStringLiteral("D1")); }, 4000);
    lab->arreter();
    verifier(demarre && centre && eteinte && allumee && vue && !rallumee, "034 L16 · 16.4 : interrupteur cliqué sur la platine en 3D pendant la simulation : la LED s'allume (et s'éteint au clic suivant)",
             erreur.toStdString() + (centre ? "" : " interrupteur introuvable en 3D") + (demarre ? " démarrée" : " non démarrée") + (eteinte ? ", éteinte avant" : ", allumée avant") +
                 (allumee ? ", allumée après le clic" : ", restée éteinte") + (vue ? ", vue en 3D" : ", pas vue en 3D") + (rallumee ? ", restée allumée au second clic" : "") + etatClic);
  }

  // ---------- 16.6 · Guitare et documents de câblage ----------
  f.montrerElectronique(GUITARE, AtelierElectronique::Schema);
  attendre(300);
  a->executerAction("platine.guitare");
  attendre(250);
  pl = a->platine();
  {
    if (auto* b = pl->findChild<QPushButton*>(QStringLiteral("analyserGuitare"))) reel::cliquer(b, b->rect().center());
    attendre(200);
    auto* t = pl->findChild<QTableWidget*>(QStringLiteral("positionsGuitare"));
    const std::vector<QString> attendu{QStringLiteral("chevalet seul"), QStringLiteral("milieu et chevalet en parallèle"), QStringLiteral("milieu seul"), QStringLiteral("manche et milieu en parallèle"),
                                       QStringLiteral("manche seul")};
    bool cinq = t && t->rowCount() == 5;
    std::string detail;
    for (int i = 0; cinq && i < 5; ++i) {
      cinq &= t->item(i, 1) && t->item(i, 1)->text() == attendu[static_cast<std::size_t>(i)];
      if (t->item(i, 1)) detail += t->item(i, 1)->text().toStdString() + " ; ";
    }
    verifier(cinq, "034 L16 · 16.6 : sélecteur à cinq positions d'une guitare à trois micros : les cinq combinaisons attendues, dont manche et milieu en parallèle", detail);
    // Documents : plan de chargement, impression à l'échelle, plaque à bandes
    pl->monter();
    attendre(200);
    a->executerAction("platine.documents");
    attendre(200);
    const std::string d = dossierEssai("documents");
    const QString pdf = QString::fromStdString(d + "/platine.pdf"), svg = QString::fromStdString(d + "/platine.svg"), plt = QString::fromStdString(d + "/plaque.plt"),
                  coupures = QString::fromStdString(d + "/coupures.svg");
    const bool ecrits = pl->exporter(pdf) && pl->exporter(svg) && pl->exporter(plt) && pl->exporter(coupures);
    QFile fp(pdf);
    const bool estPdf = fp.open(QIODevice::ReadOnly) && fp.read(4) == "%PDF";
    auto* fils = pl->findChild<QTableWidget*>(QStringLiteral("listeFils"));
    verifier(ecrits && estPdf && pl->plan().contains(QStringLiteral("SEL1")) && fils && fils->rowCount() == static_cast<int>(platineDe(*a->conceptionCourante()).fils.size()),
             "034 L16 · 16.6 : documents de câblage : plan de chargement, liste des fils, platine à l'échelle en PDF, plaque à bandes en HPGL et plan miroir des coupures", d);
  }
  pl->close();

  // ---------- 16.10 · Poste neuf : paquet portable, réglages de l'établissement, essais intégrés ----------
  {
    const std::string d = dossierEssai("poste");
    QFile temoin(QString::fromStdString(d + "/promethee-portable.txt"));
    if (temoin.open(QIODevice::WriteOnly)) temoin.write("paquet portable\n");
    temoin.close();
    ReglagesEtablissement r;
    r.nom = "Lycée d'essai";
    r.dossierBibliotheques = "bibliotheques";
    r.profilFabricant = profilsFabricants().front().nom;
    r.interfaceSimplifiee = true;
    r.interfaceVerrouillee = true;
    r.actionsMasquees = {"automatisation.python"};
    prom::ecrireFichierAtomique(prom::cheminFichier(d + "/" + kFichierEtablissement), ecrireReglagesEtablissement(r));
    QProcess poste;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PROMETHEE_DOSSIER_PROGRAMME"), QString::fromStdString(d));
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    poste.setProcessEnvironment(env);
    poste.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--verifier-poste")});
    const bool fini = poste.waitForFinished(180000);
    const QString sortie = QString::fromUtf8(poste.readAllStandardOutput());
    // Les réglages imposés sont écrits dans le paquet, pas dans le profil de l'utilisateur
    bool reglesDansPaquet = false;
    for (QDirIterator it(QString::fromStdString(d + "/promethee-donnees"), {QStringLiteral("*.ini"), QStringLiteral("*.conf")}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
      QFile f(it.next());
      reglesDansPaquet |= f.open(QIODevice::ReadOnly) && f.readAll().contains("dossierBibliotheque");
    }
    verifier(fini && poste.exitStatus() == QProcess::NormalExit && poste.exitCode() == 0 && sortie.contains(QStringLiteral("Réglages de l'établissement appliqués : Lycée d'essai")) &&
                 sortie.contains(QStringLiteral("Essais intégrés")) && sortie.contains(QStringLiteral(", 0 échec")) && reglesDansPaquet && QDir(QString::fromStdString(d + "/bibliotheques")).exists(),
             "034 L16 · 16.10 : poste neuf : paquet portable lancé sans droits d'administrateur, réglages de l'établissement appliqués, essais intégrés verts", sortie.toStdString());
  }

  // ---------- Le projet de la classe : la carte du professeur, celle de l'élève, le filtre RC ----------
  prom::Projet classe = prom::projetVide();
  classe.sansCarte = true;
  classe.nom = "Classe";
  ConceptionElec professeur = exempleDiviseur();
  professeur.nom = "Professeur";
  ConceptionElec eleve0 = nouvelleConception("Élève");
  classe.electronique = {professeur, eleve0, exempleFiltreRC()};
  enum { PROFESSEUR, ELEVE, FILTRE };
  doc->creer(classe);
  f.montrerElectronique(PROFESSEUR, AtelierElectronique::Schema);
  attendre(300);
  a = f.atelierElectronique();
  if (!a) return;
  auto remplacerEleve = [&](const Montage& m) {
    doc->modifierConception(ELEVE, [&](ConceptionElec& c) {
      const std::string u = c.uuid, nom = c.nom;
      c = m.c;
      c.uuid = u;
      c.nom = nom;
    });
  };

  // ---------- 16.11 · Consigne de trois étapes ----------
  a->executerAction("enseignement.consignes");
  attendre(250);
  FenetreEnseignement* en = a->enseignement();
  {
    EtapeConsigne e1, e2, e3;
    e1.titre = "Pose deux résistances de 10 kΩ";
    e1.condition.genre = GenreCondition::ComposantPose;
    e1.condition.symbole = "R";
    e1.condition.valeur = "10k";
    e1.condition.nombre = 2;
    e2.titre = "Relie les deux résistances";
    e2.condition.genre = GenreCondition::BrochesReliees;
    e2.condition.a = "R9.2";
    e2.condition.b = "R7.2";
    e3.titre = "Mesure 2,5 V au point milieu";
    e3.condition.genre = GenreCondition::MesureDansPlage;
    e3.condition.a = "M";
    e3.condition.min = 2.4;
    e3.condition.max = 2.6;
    en->nouvelleConsigne(QStringLiteral("diviseur"), QStringLiteral("Le diviseur de tension"));
    for (const auto& e : {e1, e2, e3}) en->ajouterEtape(e);
    const int pas0 = doc->positionHistorique();
    const bool range = en->enregistrerConsigne() && doc->positionHistorique() == pas0 + 1 && consignesDe(doc->projet().electronique[PROFESSEUR]).size() == 1;
    auto etats = [&] {
      std::string t;
      for (const auto& x : en->verifierSur(ELEVE)) t += x.faite ? '1' : '0';
      return t;
    };
    const std::string vide = etats();
    Montage m1("Élève");
    m1.poser("R", "R9", "10k", {});
    m1.poser("R", "R7", "10k", {});
    remplacerEleve(m1);
    const std::string poses = etats();
    // Reliées, mais sans alimentation (un réseau nommé « +5V » serait alimenté d'office par la simulation)
    Montage m2("Élève");
    m2.poser("R", "R9", "10k", {{"2", "M"}, {"1", "VIN"}});
    m2.poser("R", "R7", "10k", {{"2", "M"}, {"1", "GND"}});
    remplacerEleve(m2);
    const std::string relies = etats();
    Montage m3("Élève");
    m3.poser("R", "R9", "10k", {{"2", "M"}, {"1", "+5V"}});
    m3.poser("R", "R7", "10k", {{"2", "M"}, {"1", "GND"}});
    m3.poser("Pile", "B2", "5V", {{"1", "+5V"}, {"2", "GND"}});
    remplacerEleve(m3);
    const std::string complet = etats();
    const bool mesure = !ngspiceDisponible() || complet == "111";
    verifier(range && vide == "000" && poses == "100" && relies == "110" && mesure && (complet == "111" || complet == "110"),
             "034 L16 · 16.11 : consigne de trois étapes écrite sans programmer : chaque étape se valide seule sur le projet de l'élève", vide + " → " + poses + " → " + relies + " → " + complet);
  }

  // ---------- 16.12 · Panne cachée « R2 coupée », partie verrouillée, sceau ----------
  {
    en->montrer(QStringLiteral("enseignant"));
    en->recharger();
    en->setSecret(QStringLiteral("secret du professeur"));
    en->verrouiller(QStringLiteral("R1"), true);
    PanneCachee coupe;
    coupe.genre = GenrePanne::Coupure;
    coupe.composant = "R2";
    coupe.titre = "R2 coupée";
    en->ajouterPanne(coupe);
    en->masquerAction(QStringLiteral("simulation.calculateurs"), true);
    const auto feuilles = doc->projet().electronique[PROFESSEUR].feuilles;
    const bool scelle = en->sceller();
    const bool feuilleIntacte = doc->projet().electronique[PROFESSEUR].feuilles == feuilles;
    QString refus;
    const auto lien = QObject::connect(doc, &Document::refus, [&refus](const QString& r) { refus = r; });
    const bool modifie = a->modifierConception([](ConceptionElec& c) {
      for (auto& x : c.composants)
        if (x.reference == "R1") x.valeur = "1k";
    });
    QObject::disconnect(lien);
    const bool masquee = !a->executerAction("simulation.calculateurs");
    const bool intact = en->relever() == EtatSceau::Intact;
    double volts = std::nan("");
    if (ngspiceDisponible()) {
      FenetreLaboratoire* lab = a->laboratoire(QStringLiteral("continu"));
      lab->reglerMultimetre(QStringLiteral("MID"), 0);
      QString erreur;
      if (lab->demarrer(&erreur)) {
        attendre(800);
        lab->rafraichir();
        volts = lab->mesureMultimetre();
        lab->arreter();
      }
    }
    const bool lu = !ngspiceDisponible() || std::abs(volts) < 0.05;
    verifier(scelle && feuilleIntacte && !modifie && refus.contains(QStringLiteral("R1")) && masquee && intact && lu,
             "034 L16 · 16.12 : panne cachée « R2 coupée » : la feuille ne change pas, et le voltmètre lit 0 V là où le montage juste donne 2,5 V ; partie verrouillée refusée, sceau intact au relevé",
             refus.toStdString() + " ; voltmètre " + std::to_string(volts) + " V");
  }

  // ---------- 16.13 · Montage de l'élève comparé à la référence ----------
  f.montrerElectronique(ELEVE, AtelierElectronique::Schema);
  attendre(250);
  {
    en = a->enseignement(QStringLiteral("evaluation"));
    const ResultatEvaluation juste = en->evaluer(PROFESSEUR, false);
    Montage m4("Élève");
    m4.poser("R", "R9", "10k", {{"2", "M"}, {"1", "+5V"}});
    m4.poser("R", "R7", "10k", {{"2", "M"}, {"1", "GND"}});
    m4.poser("Pile", "B2", "5V", {{"1", "+5V"}, {"2", "GND"}});
    m4.poser("R", "R3", "10k", {{"1", "M"}, {"2", "GND"}});
    remplacerEleve(m4);
    const ResultatEvaluation trop = en->evaluer(PROFESSEUR, true);
    auto* liste = en->findChild<QListWidget*>(QStringLiteral("ecartsEvaluation"));
    const bool signale = liste && liste->count() > 0 && liste->item(0)->text().contains(QStringLiteral("résistance de trop"));
    verifier(juste.juste && juste.correspondance.count("B2") && !trop.juste && signale && en->controlesComptes() == 1,
             "034 L16 · 16.13 : montage d'élève équivalent, aux repères différents : reconnu juste ; avec une résistance en trop, l'écart est signalé",
             trop.ecarts.empty() ? std::string() : trop.ecarts.front());
  }

  // ---------- 16.14 · Page HTML hors ligne, devoir de la classe ----------
  f.montrerElectronique(FILTRE, AtelierElectronique::Schema);
  attendre(250);
  {
    en = a->enseignement(QStringLiteral("diffusion"));
    const std::string d = dossierEssai("diffusion");
    const QString page = QString::fromStdString(d + "/filtre.html");
    const bool ecrite = en->exporterPage(page);
    QString lecture = QStringLiteral("Playwright absent");
    bool coupure = true;
    QProcess verif;
    verif.start(QStringLiteral("python3"), {QStringLiteral("-c"), QStringLiteral("import playwright")});
    if (verif.waitForFinished(30000) && verif.exitCode() == 0) {
      const QString script = QString::fromStdString(d + "/navigateur.py");
      prom::ecrireFichierAtomique(prom::cheminFichier(script.toStdString()),
                                  "import sys\nfrom playwright.sync_api import sync_playwright\nwith sync_playwright() as p:\n    b = p.chromium.launch()\n"
                                  "    c = b.new_context(offline=True)\n    g = c.new_page()\n    g.goto('file://' + sys.argv[1])\n"
                                  "    g.wait_for_function('document.body.dataset.coupure !== undefined', timeout=20000)\n    print(g.inner_text('#resultat'))\n    b.close()\n");
      QProcess navigateur;
      navigateur.start(QStringLiteral("python3"), {script, page});
      navigateur.waitForFinished(120000);
      lecture = QString::fromUtf8(navigateur.readAllStandardOutput()).trimmed();
      coupure = navigateur.exitCode() == 0 && lecture.contains(QStringLiteral("1,00 kHz"));
    }
    // Classe : devoir publié, rendu déposé, relevé, noté, notes et paquet Common Cartridge
    en = a->enseignement(QStringLiteral("classe"));
    const std::string dossierClasse = dossierEssai("classe");
    en->setDossierClasse(QString::fromStdString(dossierClasse));
    Devoir dv;
    dv.id = "filtre-rc";
    dv.titre = "Filtre RC";
    dv.consigne = "Monte le filtre et mesure sa coupure.";
    dv.echeance = "2099-01-01T00:00";
    dv.interdits = {"simulation.calculateurs"};
    const bool publie = en->publier(dv);
    const bool depose = en->deposer(QStringLiteral("filtre-rc"), QStringLiteral("Alice Martin"));
    const bool note = en->noter(QStringLiteral("filtre-rc"), QStringLiteral("Alice Martin"), 16, QStringLiteral("bien"));
    const auto rendus = en->relever(QStringLiteral("filtre-rc"));
    const QString csv = QString::fromStdString(d + "/notes.csv"), cc = QString::fromStdString(d + "/devoir.imscc");
    const bool exports = en->exporterNotes(QStringLiteral("filtre-rc"), csv) && en->exporterCartouche(QStringLiteral("filtre-rc"), cc);
    bool manifeste = false;
    try {
      for (const auto& [nom, contenu] : prom::lireZip(prom::lireFichier(prom::cheminFichier(cc.toStdString())))) manifeste |= nom == "imsmanifest.xml";
    } catch (...) {
    }
    QFile notes(csv);
    const bool csvJuste = notes.open(QIODevice::ReadOnly) && notes.readAll().contains("Alice Martin;filtre-rc;16;bien");
    verifier(ecrite && coupure, "034 L16 · 16.14 : page HTML d'un filtre RC ouverte hors ligne dans un navigateur : la simulation tourne et donne la coupure à 1 kHz", lecture.toStdString());
    verifier(publie && depose && note && rendus.size() == 1 && !rendus[0].enRetard && exports && manifeste && csvJuste,
             "034 L16 · 16.14 : devoir publié dans le dossier de la classe, rendu relevé et noté, notes en CSV, paquet IMS Common Cartridge", dossierClasse);
  }
  en->close();
}
