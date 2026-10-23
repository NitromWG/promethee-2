// Prométhée : essais d'interface du lot L4 de l'audit (« Empreintes et bibliothèque »), sur l'exemple livré et une
// feuille d'essai : fenêtre des empreintes d'une inductance (six empreintes d'inductance, le fusible seulement filtre
// décoché), « 10k 0603 » posé depuis la fenêtre de choix restée ouverte, avec sa hauteur et son corps 3D, archive
// SnapEDA déposée, portails grisés avec leur raison, article modifié qui propose la mise à jour de ses six
// composants, composant générique signalé par l'ERC puis standardisé en une commande.
// Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L4.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QCoreApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "DialoguesArticles.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetreChoixComposant.hpp"
#include "FenetreChoixEmpreinte.hpp"
#include "FenetrePrincipale.hpp"
#include "SelectionElectronique.hpp"
#include "VueSchema.hpp"
#include "promethee/articles.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/modele.hpp"
#include "promethee/synchronisation.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
void repondre(QMessageBox::ButtonRole role) {
  QTimer::singleShot(300, [role] {
    if (auto* m = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
      for (QAbstractButton* b : m->buttons())
        if (m->buttonRole(b) == role) {
          b->click();
          return;
        }
  });
}
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
// Le dernier composant posé sur la feuille fi
const ComposantElec* dernierPose(const ConceptionElec& c, int fi) {
  const auto& f = c.feuilles[static_cast<std::size_t>(fi)];
  for (auto it = f.symboles.rbegin(); it != f.symboles.rend(); ++it)
    if (const ComposantElec* k = c.composant(it->composant)) return k;
  return nullptr;
}
bool aCode(const std::vector<Violation>& v, const std::string& code) {
  return std::any_of(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; });
}
}  // namespace

void essaisElectroniqueL4(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  if (!f.isWindow()) {
    prom::Projet vide = prom::projetVide();
    vide.sansCarte = true;
    doc->creer(vide);
    attendre(200);
  }
  repondre(QMessageBox::DestructiveRole);
  f.choixAccueilPublic(QStringLiteral("exemple-elec"));
  attendre(900);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a || !conception(doc)) {
    verifier(false, "L4 : l'exemple livré s'ouvre pour les essais du lot", {});
    return;
  }
  verifier(reel::activer(&f), "L4 : la fenêtre est active", {});
  a->modifierConception([](ConceptionElec& c) {
    Feuille fe;
    fe.uuid = nouvelUuid();
    fe.nom = "Essais L4";
    fe.format = "A3";
    c.feuilles.push_back(fe);
  }, QStringLiteral("Feuille d’essai"));
  const int fi = static_cast<int>(conception(doc)->feuilles.size()) - 1;
  a->ouvrir(0, AtelierElectronique::Schema, fi);
  attendre(300);
  VueSchema* vs = a->vueSchema();

  // ---------- 4.6 · Inductance : les six empreintes d'inductance, « fusible » seulement filtre décoché (EMP-01, P7) ----------
  {
    std::string l, sl;
    a->modifierConception([&](ConceptionElec& c) {
      sl = poserSymbole(c, static_cast<std::size_t>(fi), "L", ptMm(60, 60));
      for (const auto& x : c.feuilles[static_cast<std::size_t>(fi)].symboles)
        if (x.uuid == sl) l = x.composant;
    }, QStringLiteral("Inductance"));
    vs->selectionner({sl});
    attendre(100);
    a->executerAction("composant.empreinte");
    attendre(300);
    FenetreChoixEmpreinte* fe = a->choixEmpreinte();
    std::vector<std::string> montrees = fe ? fe->empreintesMontrees() : std::vector<std::string>{};
    const bool six = montrees.size() == 6 && std::all_of(montrees.begin(), montrees.end(), [](const std::string& id) { return id.rfind("L_", 0) == 0; });
    std::string liste;
    for (const auto& x : montrees) liste += x + " ";
    bool fusible = false;
    if (fe) {
      reel::cliquer(fe->filtreCompatibles(), fe->filtreCompatibles()->rect().center());
      attendre(150);
      for (const auto& x : fe->empreintesMontrees()) fusible |= x.rfind("Fuse_", 0) == 0;
    }
    verifier(fe && fe->isVisible() && six && fusible && !fe->isModal(),
             "4.6 : pour une inductance, la fenêtre des empreintes propose les six empreintes d'inductance ; « fusible » n'y vient qu'en décochant le filtre", liste);
    // Application en série depuis la fenêtre : L_1210, en un pas
    if (fe) {
      reel::cliquer(fe->filtreCompatibles(), fe->filtreCompatibles()->rect().center());
      attendre(100);
      int ligne = -1;
      for (int i = 0; i < fe->table()->rowCount(); ++i)
        if (fe->table()->item(i, 0)->text() == QStringLiteral("L_1210")) ligne = i;
      const int pas = doc->positionHistorique();
      if (ligne >= 0) {
        fe->table()->selectRow(ligne);
        reel::cliquer(fe->boutonAppliquer(), fe->boutonAppliquer()->rect().center());
        attendre(150);
      }
      const ComposantElec* k = conception(doc)->composant(l);
      verifier(k && k->empreinte == "L_1210" && doc->positionHistorique() == pas + 1, "4.6 : « Appliquer » donne L_1210 à l'inductance, en un pas", k ? k->empreinte : "—");
      fe->close();
    }
  }

  // ---------- 4.4, 4.5, 4.8 · « 10k 0603 » posé depuis la fenêtre de choix restée ouverte, hauteur et corps 3D ----------
  std::string r10k;
  {
    a->executerAction("bibliotheque.articles");
    attendre(300);
    FenetreChoixComposant* fc = a->choixComposant();
    bool premier = false;
    if (fc) {
      reel::cliquer(fc->recherche(), fc->recherche()->rect().center());
      fc->recherche()->selectAll();
      reel::taper(fc->recherche(), QStringLiteral("10k 0603"));
      attendre(150);
      premier = !fc->resultats().empty() && fc->resultats().front().valeur == "10k" && fc->resultats().front().empreinte == "R_0603";
      reel::cliquer(fc->boutonPoser(), fc->boutonPoser()->rect().center());
      attendre(150);
    }
    const Pt ou = ptMm(101.6, 60.96);
    vs->centrerSur(ou);
    attendre(100);
    reel::cliquer(vs, vs->versEcran(ou).toPoint());
    attendre(200);
    const ConceptionElec* c = conception(doc);
    const ComposantElec* k = dernierPose(*c, fi);
    if (k) r10k = k->uuid;
    const bool pose = k && k->valeur == "10k" && k->empreinte == "R_0603" && k->article == "R_10k_0603" && hauteurComposant(*c, *k).mm > 0.2;
    if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL")) std::printf("  (premier %d, posé %d, fenêtre %d)\n", premier, pose, fc && fc->isVisible());
    verifier(fc && premier && pose && fc->isVisible(),
             "4.4, 4.5 : « 10k 0603 » dans la fenêtre de choix, puis un clic sur la feuille : une résistance de 10 kΩ en 0603 posée, avec sa hauteur, la fenêtre restée ouverte",
             k ? k->valeur + " " + k->empreinte + " " + k->article : std::string("rien de posé"));
    // Au circuit : son corps 3D
    reel::touche(vs, Qt::Key_Escape);
    a->mettreAJourCircuit();
    attendre(300);
    c = conception(doc);
    bool corps = false;
    for (const auto& e : c->circuit.empreintes)
      if (e.composant == r10k)
        if (const auto cp = corpsPose(*c, e)) corps = cp->hauteur.mm > 0.2;
    verifier(corps, "4.8 : la résistance posée a son corps 3D au circuit, à la hauteur générique d'une 0603", {});
  }

  // ---------- 4.22 · Archive SnapEDA déposée : un seul article contrôlé, symbole, empreinte, modèle 3D ----------
  {
    FenetreChoixComposant* fc = a->choixComposant();
    // Données d'essai : la variable PROMETHEE_DONNEES, sinon le dépôt vu depuis l'exécutable (build/app) ou le dossier courant
    QString donnees = qEnvironmentVariable("PROMETHEE_DONNEES");
    for (const QString& essai : {QCoreApplication::applicationDirPath() + QStringLiteral("/../../tests/donnees"), QStringLiteral("tests/donnees")})
      if (donnees.isEmpty() && QFileInfo::exists(essai + QStringLiteral("/archives/ESP32-S3-WROOM-1-N8_SnapEDA.zip"))) donnees = essai;
    const QString zip = QDir(donnees).filePath(QStringLiteral("archives/ESP32-S3-WROOM-1-N8_SnapEDA.zip"));
    const std::string id = fc ? fc->importerArchive(zip) : std::string();
    attendre(200);
    const auto perso = AtelierElectronique::articlesPerso();
    const auto it = std::find_if(perso.begin(), perso.end(), [&](const Article& x) { return x.id == id; });
    const bool modele = it != perso.end() && !it->modele3D.empty() &&
                        QFile::exists(QFileInfo(AtelierElectronique::cheminArticlesPerso()).dir().filePath(QString::fromStdString(it->modele3D)));
    bool symbole = false, empreinte = false;
    for (const auto& s : AtelierElectronique::bibliothequePerso()) symbole |= it != perso.end() && s.id == it->symbole && s.broches.size() == 41;
    for (const auto& e : AtelierElectronique::empreintesPerso()) empreinte |= it != perso.end() && e.id == it->empreinte && e.pastilles.size() == 41;
    const bool liste = fc && !fc->resultats().empty() && fc->resultats().front().id == id;
    verifier(!id.empty() && it != perso.end() && symbole && empreinte && modele && liste,
             "4.22 : l'archive SnapEDA d'un ESP32-S3 devient un seul article, avec son symbole, son empreinte de 41 pastilles et son modèle 3D, contrôles sans erreur", id);
  }

  // ---------- 4.22 · Portails et distributeurs grisés, avec la raison, tant que l'accès en ligne est désactivé ----------
  {
    FenetreChoixComposant* fc = a->choixComposant();
    bool grise = false, raison = false;
    if (fc) {
      fc->onglets()->setCurrentIndex(1);
      attendre(100);
      grise = !fc->portails()->isEnabled();
      raison = fc->raisonPortails()->isVisible() && fc->raisonPortails()->text().contains(QStringLiteral("Accès en ligne désactivé"));
      fc->onglets()->setCurrentIndex(0);
    }
    verifier(grise && raison, "4.22 : sans accès en ligne, les portails et les distributeurs sont grisés, avec la raison", {});
  }

  // ---------- 4.10 · Article modifié dans la bibliothèque : « Mettre à jour les 6 composants », avec les différences ----------
  {
    a->modifierConception([&](ConceptionElec& c) {
      const Article* r = articleIntegre("R_4k7_0603");
      for (int i = 0; i < 6; ++i) poserArticle(c, static_cast<std::size_t>(fi), *r, ptMm(50.8 + 10.16 * i, 127));
    }, QStringLiteral("Six résistances"));
    Article modifie = *articleIntegre("R_4k7_0603");
    modifie.mpn = "RC0603JR-074K7L";
    modifie.parametres["tolerance"] = "5 %";
    modifie.version = 2;
    a->rangerArticlesPerso({modifie});
    attendre(200);
    a->executerAction("bibliotheque.misesAJour");
    attendre(300);
    DialogueMisesAJour* m = a->misesAJourArticles();
    int ligne = -1;
    if (m)
      for (int i = 0; i < m->table()->rowCount(); ++i)
        if (m->table()->item(i, 0)->text().contains(QStringLiteral("4k7"))) ligne = i;
    const QString bouton = m && ligne >= 0 ? m->texteBouton(ligne) : QString();
    const QString diff = m && ligne >= 0 ? m->differences(ligne) : QString();
    if (m && ligne >= 0)
      if (auto* b = qobject_cast<QPushButton*>(m->table()->cellWidget(ligne, 2))) {
        reel::cliquer(b, b->rect().center());
        attendre(200);
      }
    int suivis = 0;
    for (const auto& k : conception(doc)->composants) suivis += k.article == "R_4k7_0603" && k.mpn == "RC0603JR-074K7L" && k.versionArticle == 2;
    verifier(bouton == QStringLiteral("Mettre à jour les 6 composants") && diff.contains(QStringLiteral("référence fabricant")) && diff.contains(QStringLiteral("RC0603JR-074K7L")) && suivis == 6,
             "4.10 : l'article modifié dans la bibliothèque propose « Mettre à jour les 6 composants », avec les différences, et les met à jour",
             bouton.toStdString() + " / " + diff.left(120).toStdString() + " / " + std::to_string(suivis) + " suivis");
    if (m) m->close();
  }

  // ---------- 4.21 · « LDO 3,3 V 500 mA » générique : signalé par l'ERC, puis standardisé en une commande ----------
  {
    FenetreChoixComposant* fc = a->choixComposant();
    std::string gen;
    if (fc) {
      fc->show();
      reel::cliquer(fc->recherche(), fc->recherche()->rect().center());
      fc->recherche()->selectAll();
      reel::taper(fc->recherche(), QStringLiteral("LDO 3,3 V 500 mA"));
      attendre(100);
      fc->poserGenerique();
      attendre(100);
      const Pt ou = ptMm(203.2, 60.96);
      vs->centrerSur(ou);
      attendre(100);
      reel::cliquer(vs, vs->versEcran(ou).toPoint());
      attendre(200);
      reel::touche(vs, Qt::Key_Escape);
      if (const ComposantElec* k = dernierPose(*conception(doc), fi)) gen = k->uuid;
    }
    a->verifier();
    attendre(400);
    const bool signale = aCode(a->erc(), "ERC-44");
    a->executerAction("bibliotheque.standardiser");
    attendre(300);
    DialogueStandardisation* d = a->standardisation();
    bool propose = false;
    if (d)
      for (const auto& p : d->propositions()) propose |= p.composant == gen && p.article == "AMS1117-3.3";
    // Les autres propositions (composants de l'exemple posés d'un symbole nu) sont exclues : décochées
    if (d)
      for (int i = 0; i < d->table()->rowCount(); ++i)
        d->table()->item(i, 0)->setCheckState(d->propositions()[static_cast<std::size_t>(i)].composant == gen ? Qt::Checked : Qt::Unchecked);
    const int pas = doc->positionHistorique();
    if (d) {
      reel::cliquer(d->boutonRemplacer(), d->boutonRemplacer()->rect().center());
      attendre(200);
    }
    a->verifier();
    attendre(400);
    const ComposantElec* k = conception(doc)->composant(gen);
    verifier(!gen.empty() && signale && propose && k && k->article == "AMS1117-3.3" && k->empreinte == "SOT-223" && !aCode(a->erc(), "ERC-44") && doc->positionHistorique() == pas + 1,
             "4.21 : « LDO 3,3 V 500 mA » posé sans article reste signalé par l'ERC, puis la standardisation le remplace en une commande par l'article proposé",
             k ? k->article + " " + k->empreinte : std::string("—"));
    if (d) d->close();
    if (fc) fc->close();
  }
}
