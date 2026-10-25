// Prométhée : essais d'interface de la 0.34, lot L16 (platine d'essai) — fil tiré à la souris entre R1 et la LED :
// liaison au schéma et au chevelu, en un pas d'annulation (16.2) ; clignotant à NE555 monté de lui-même sans écart,
// patte décalée en rouge et écart listé (16.3) ; interrupteur cliqué sur la platine en 3D pendant la simulation : la
// LED s'allume, composant déplacé gardant ses liaisons (16.4) ; positions du sélecteur d'une guitare et documents
// de câblage écrits (16.6). Appelés seuls avec PROMETHEE_ESSAIS_LOTS=034L16. Licence GPL-3.0-only.
#include <filesystem>
#include <functional>
#include <random>
#include <set>
#include <string>

#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "Laboratoire034.hpp"
#include "Platine034.hpp"
#include "promethee/electronique.hpp"
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
}
