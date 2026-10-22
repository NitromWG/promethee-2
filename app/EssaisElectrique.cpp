// Prométhée : essais de l'atelier électrique, dans la vraie fenêtre. Un parcours d'électricien complet : schéma
// électrique créé depuis l'accueil, appareils pris dans la palette et le ruban, contact rattaché à sa bobine par le
// menu de pose, potentiels, fils tirés d'une borne à l'autre, numérotation, vérification et clic sur un problème,
// pôles posés depuis le panneau (folio de puissance choisi seul), propriétés modifiées puis annulées, repérage
// CEI 81346, recherche d'un appareil posé, bornier, rapports et CSV, câble, armoire créée et implantée, appareil
// glissé, porte, calcul de section, tableau de logement NF C 15-100 généré, exemple livré et ses folios dessinés
// d'office, faisceau, import QElectroTech, enregistrement et réouverture, arbre du projet, Ctrl+4, nouveau projet,
// circuits types. Licence GPL-3.0-only. Lancés par « Promethee --essais-electrique » ; le code de sortie est le
// nombre d'échecs. Captures des étapes (et PDF des folios) dans le dossier PROMETHEE_CAPTURES_ELECTRIQUE s'il est
// donné ; PROMETHEE_ESSAIS_TAILLES y ajoute les largeurs minimales des panneaux.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include "Accueil.hpp"
#include "AtelierElectrique.hpp"
#include "Document.hpp"
#include "EssaisInterface.hpp"
#include "FenetrePrincipale.hpp"
#include "Journal.hpp"
#include "Navigateur.hpp"
#include "VueArmoire.hpp"
#include "VueFaisceau.hpp"
#include "VueGrafcet.hpp"
#include "VueSchema.hpp"
#include "promethee/electrique.hpp"
#include "promethee/electrique_distribution.hpp"
#include "promethee/electrique_grafcet.hpp"
#include "promethee/electrique_parcours.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using namespace prom::elec;

namespace {
int g_echecs = 0;

void verifier(bool ok, const std::string& quoi, const std::string& detail = {}) {
  const std::string ligne = (ok ? "OK      " : "ÉCHEC   ") + quoi + (detail.empty() ? "" : "  (" + detail + ")");
  std::printf("%s\n", ligne.c_str());
  std::fflush(stdout);
  journal(("essai électrique : " + ligne).c_str());
  if (!ok) ++g_echecs;
}

void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}

void touche(QWidget* w, int k, const QString& texte = {}) {
  QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier, texte);
  QCoreApplication::sendEvent(w, &e);
}

void cliquerWidget(QWidget* w) {
  const QPointF p(w->width() / 2.0, w->height() / 2.0);
  for (auto [t, b, bs] : {std::tuple{QEvent::MouseButtonPress, Qt::LeftButton, Qt::MouseButtons(Qt::LeftButton)},
                          std::tuple{QEvent::MouseButtonRelease, Qt::LeftButton, Qt::MouseButtons(Qt::NoButton)}}) {
    QMouseEvent e(t, p, w->mapToGlobal(p), b, bs, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &e);
  }
  attendre(150);
}

// Répond à la prochaine boîte de message par le bouton de ce rôle (Oui, Ne pas enregistrer…)
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

// Choisit, dans le prochain menu qui s'ouvre (« Rattacher à », « Borne de quel bornier »), l'entrée dont le texte
// contient « texte », comme au clavier ; le menu est refermé si l'entrée n'y est pas
void choisirDansMenu(const QString& texte, const QString& nomMenu, QString* vu = nullptr) {
  auto* t = new QTimer(qApp);
  t->setInterval(80);
  auto essais = std::make_shared<int>(0);
  QObject::connect(t, &QTimer::timeout, qApp, [t, essais, texte, nomMenu, vu] {
    for (QWidget* w : QApplication::topLevelWidgets())
      if (auto* m = qobject_cast<QMenu*>(w); m && m->isVisible() && m->objectName() == nomMenu) {
        if (vu)
          for (QAction* a : m->actions()) *vu += a->text() + QStringLiteral(" | ");
        for (QAction* a : m->actions())
          if (a->isEnabled() && !a->isSeparator() && a->text().contains(texte)) {
            m->setActiveAction(a);
            QKeyEvent e(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QCoreApplication::sendEvent(m, &e);
            t->stop();
            t->deleteLater();
            return;
          }
        m->close();
        t->stop();
        t->deleteLater();
        return;
      }
    if (++*essais > 60) {
      t->stop();
      t->deleteLater();
    }
  });
  t->start();
}

bool declencher(QWidget* racine, const QString& texte) {
  for (QAction* a : racine->findChildren<QAction*>())
    if (a->text() == texte && a->isEnabled()) {
      a->trigger();
      attendre(250);
      return true;
    }
  return false;
}

QPushButton* bouton(QWidget* racine, const QString& texte) {
  for (QPushButton* b : racine->findChildren<QPushButton*>())
    if (b->isVisible() && b->text().trimmed() == texte.trimmed()) return b;
  return nullptr;
}

template <class T>
T* visible(QWidget* racine, const QString& nom) {
  for (T* w : racine->findChildren<T*>(nom))
    if (w->isVisible()) return w;
  return nullptr;
}

void capturer(QWidget* w, const QString& nom) {
  const QByteArray dossier = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE");
  if (dossier.isEmpty()) return;
  QDir().mkpath(QString::fromLocal8Bit(dossier));
  attendre(200);
  w->grab().save(QDir(QString::fromLocal8Bit(dossier)).filePath(nom + QStringLiteral(".png")));
  if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_TAILLES")) {
    std::printf("TAILLE  %s : %dx%d, minimum %dx%d\n", nom.toStdString().c_str(), w->width(), w->height(), w->minimumSizeHint().width(), w->minimumSizeHint().height());
    if (auto* a = w->findChild<AtelierElectrique*>())
      for (QWidget* x : a->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        std::printf("        %s %s : min %d (%d)\n", x->metaObject()->className(), x->objectName().toStdString().c_str(), x->minimumSizeHint().width(), x->minimumWidth());
    if (auto* a = w->findChild<AtelierElectrique*>())
      for (const char* n : {"rubanElectrique", "gaucheElectrique", "vueSchemaElectrique", "defilementPanneauElectrique", "pageLogement", "pageCalculs", "vueArmoire", "vueFaisceau"})
        if (auto* x = a->findChild<QWidget*>(QString::fromLatin1(n))) std::printf("        %s : min %d, hint %d\n", n, x->minimumSizeHint().width(), x->sizeHint().width());
    if (auto* a = w->findChild<AtelierElectrique*>())
      if (auto* pan = a->findChild<QWidget*>(QStringLiteral("panneauElectrique"))) {
        std::printf("        panneau : min %d, largeur %d\n", pan->minimumSizeHint().width(), pan->width());
        for (QWidget* x : pan->findChildren<QWidget*>())
          if (x->isVisible() && x->minimumSizeHint().width() > 200)
            std::printf("          %s %s : min %d (%s)\n", x->metaObject()->className(), x->objectName().toStdString().c_str(), x->minimumSizeHint().width(),
                        qobject_cast<QLabel*>(x) ? qobject_cast<QLabel*>(x)->text().left(40).toStdString().c_str() : "");
      }
    if (auto* a = w->findChild<AtelierElectrique*>())
      for (QStackedWidget* pile : a->findChildren<QStackedWidget*>())
        for (int i = 0; i < pile->count(); ++i)
          std::printf("        pile %s[%d] %s : min %d\n", pile->objectName().toStdString().c_str(), i, pile->widget(i)->metaObject()->className(), pile->widget(i)->minimumSizeHint().width());
    std::fflush(stdout);
  }
}

Pt P(double x, double y) { return ptMm(x * 2.54, y * 2.54); }
std::string nombre(double v) {
  char b[32];
  std::snprintf(b, sizeof b, "%.3g", v);
  return b;
}

// Capture de la vue du schéma seule, agrandie autour d'un point (lisibilité des symboles, renvois, numéros)
void capturerZoom(VueSchema* vs, Pt centre, const QString& nom) {
  if (qEnvironmentVariableIsEmpty("PROMETHEE_CAPTURES_ELECTRIQUE")) return;
  vs->centrerSur(centre);
  capturer(vs, nom);
  vs->recadrer();
}

const ProjetElectrique* projet(FenetrePrincipale& f, int i = 0) {
  const auto& e = f.document()->projet().electrique;
  return i >= 0 && i < static_cast<int>(e.size()) ? &e[static_cast<std::size_t>(i)] : nullptr;
}

const SymbolePlace* pose(const ProjetElectrique& p, const std::string& uuid, std::size_t* feuille = nullptr) {
  for (std::size_t f = 0; f < p.schema.feuilles.size(); ++f)
    for (const auto& s : p.schema.feuilles[f].symboles)
      if (s.uuid == uuid) {
        if (feuille) *feuille = f;
        return &s;
      }
  return nullptr;
}

// Pose d'un appareil par son symbole (le symbole principal, ou un contact, des pôles…)
std::string poseDe(const ProjetElectrique& p, const std::string& composant, const std::string& symbole) {
  for (const auto& f : p.schema.feuilles)
    for (const auto& s : f.symboles)
      if (s.composant == composant && s.symbole == symbole) return s.uuid;
  return {};
}

Pt borne(const ProjetElectrique& p, const std::string& uuidPose, const std::string& numero) {
  const SymbolePlace* s = pose(p, uuidPose);
  if (!s) return {};
  const DefSymbole* d = p.schema.symbole(s->symbole);
  if (!d) return {};
  for (const auto& b : brochesPlacees(*s, *d))
    if (b.numero == numero) return b.p;
  return {};
}

std::string bornes(const ProjetElectrique& p, const std::string& uuidPose) {
  const SymbolePlace* s = pose(p, uuidPose);
  if (!s) return {};
  const DefSymbole* d = p.schema.symbole(s->symbole);
  std::string r;
  if (d)
    for (const auto& b : brochesPlacees(*s, *d)) r += (r.empty() ? "" : "-") + b.numero;
  return r;
}

int compte(const std::vector<Violation>& v, const std::string& code) {
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
}
int erreurs(const std::vector<Violation>& v, std::string* detail = nullptr) {
  int n = 0;
  for (const auto& x : v)
    if (x.gravite == Gravite::Erreur) {
      ++n;
      if (detail) *detail += x.code + " " + x.message + " ; ";
    }
  return n;
}

// Synthèse d'un projet QElectroTech : une bobine maîtresse, un contact esclave, une borne, deux conducteurs
const char* QET_ESSAI = R"(<?xml version="1.0" encoding="UTF-8"?>
<project title="Essai QElectroTech" version="0.80">
  <diagram title="Commande" order="1" cols="17" colsize="60" rows="8" rowsize="80" author="Essai" date="20260101">
    <elements>
      <element type="embed://import/elec/bobine.elmt" x="100" y="200" orientation="0" uuid="{M}">
        <terminals><terminal id="1" x="0" y="-16" orientation="0"/><terminal id="2" x="0" y="16" orientation="2"/></terminals>
        <links_uuids><link_uuid uuid="{S}"/></links_uuids>
        <elementInformations><elementInformation name="label" show="1">-KM1</elementInformation>
        <elementInformation name="function" show="1">Pompe</elementInformation></elementInformations>
      </element>
      <element type="embed://import/elec/contact_no.elmt" x="200" y="200" orientation="0" uuid="{S}">
        <terminals><terminal id="3" x="0" y="-16" orientation="0"/><terminal id="4" x="0" y="16" orientation="2"/></terminals>
        <links_uuids><link_uuid uuid="{M}"/></links_uuids>
      </element>
      <element type="embed://import/elec/borne.elmt" x="200" y="100" orientation="0" uuid="{X}">
        <terminals><terminal id="5" x="0" y="-6" orientation="0"/><terminal id="6" x="0" y="6" orientation="2"/></terminals>
        <elementInformations><elementInformation name="label" show="1">X1:7</elementInformation></elementInformations>
      </element>
    </elements>
    <conductors>
      <conductor terminal1="6" terminal2="3" num="12" conductor_section="1.5" conductor_color="Rouge"><segment orientation="vertical" length="70"/></conductor>
      <conductor terminal1="4" terminal2="2" num="13"/>
    </conductors>
  </diagram>
  <collection><category name="import"><category name="elec">
    <element name="bobine.elmt"><definition type="element" link_type="master" width="40" height="40" hotspot_x="20" hotspot_y="20">
      <names><name lang="fr">Bobine</name></names>
      <description><rect x="-10" y="-10" width="20" height="20"/><line x1="0" y1="-20" x2="0" y2="-10"/><line x1="0" y1="10" x2="0" y2="20"/>
      <terminal x="0" y="-20" orientation="n"/><terminal x="0" y="20" orientation="s"/></description></definition></element>
    <element name="contact_no.elmt"><definition type="element" link_type="slave" width="20" height="40" hotspot_x="10" hotspot_y="20">
      <names><name lang="fr">Contact NO</name></names>
      <kindInformations><kindInformation name="state">NO</kindInformation><kindInformation name="type">simple</kindInformation><kindInformation name="number">1</kindInformation></kindInformations>
      <description><line x1="0" y1="-20" x2="0" y2="-8"/><line x1="0" y1="8" x2="-6" y2="-6"/>
      <terminal x="0" y="-20" orientation="n"/><terminal x="0" y="20" orientation="s"/></description></definition></element>
    <element name="borne.elmt"><definition type="element" link_type="terminal" width="10" height="20" hotspot_x="5" hotspot_y="10">
      <names><name lang="fr">Borne</name></names>
      <description><ellipse x="-3" y="-3" width="6" height="6"/><terminal x="0" y="-10" orientation="n"/><terminal x="0" y="10" orientation="s"/></description></definition></element>
  </category></category></collection>
</project>)";
}  // namespace

void lancerEssaisElectrique(FenetrePrincipale& f) {
  installerVeilleBoites();
  Document* doc = f.document();
  const QString dossier = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-electrique"));
  QDir(dossier).removeRecursively();
  QDir().mkpath(dossier);
  // Bibliothèque d'appareils personnels propre aux essais : celle de l'utilisateur n'est ni lue ni touchée
  qputenv("PROMETHEE_BIBLIOTHEQUE_ELECTRIQUE", QDir(dossier).filePath(QStringLiteral("bibliotheque-electrique.json")).toUtf8());
  auto fin = [&] {
    std::printf("%d échec(s)\n", g_echecs);
    std::fflush(stdout);
    QDir(dossier).removeRecursively();
    QApplication::exit(g_echecs);
  };

  // ---------- 1. L'accueil montre d'emblée tout ce qui se crée en électricité ----------
  auto* acc = f.findChild<Accueil*>();
  verifier(acc && acc->isVisible(), "l'accueil s'ouvre au lancement");
  if (!acc) return fin();
  // Sans métier choisi : un aperçu court, pas toutes les créations d'un coup
  acc->choisirMetier(QString());
  attendre(150);
  auto visibleAcc = [acc](const QString& nom) {
    auto* w = acc->findChild<QWidget*>(nom);
    return w && w->isVisibleTo(acc);
  };
  verifier(visibleAcc(QStringLiteral("section-commencer")) && !visibleAcc(QStringLiteral("section-electrique")) && !visibleAcc(QStringLiteral("section-meca")),
           "sans métier choisi, l'accueil montre un aperçu « Pour commencer », pas toutes les créations d'un coup");
  // Un clic sur la tuile Électrique : ses créations seules
  if (auto* tuile = acc->findChild<QWidget*>(QStringLiteral("metier-electrique"))) cliquerWidget(tuile);
  attendre(150);
  bool tousLesChoix = true;
  for (const char* code : {"electrique-schema", "electrique-logement", "electrique-armoire", "electrique-faisceau", "electrique-exemple", "electrique-qet"})
    tousLesChoix = tousLesChoix && visibleAcc(QStringLiteral("choix-%1").arg(QString::fromLatin1(code)));
  verifier(tousLesChoix && !visibleAcc(QStringLiteral("section-meca")) && !visibleAcc(QStringLiteral("section-commencer")),
           "un clic sur la tuile Électrique montre ses créations, et elles seules");
  verifier(acc->findChild<QPushButton*>(QStringLiteral("nouveauProjet")) != nullptr, "le bouton « Nouveau projet vide » est en tête de l'accueil");
  // La recherche trouve une création sans connaître son métier
  if (auto* r = acc->findChild<QLineEdit*>(QStringLiteral("rechercheAccueil"))) {
    r->setText(QStringLiteral("differentiel"));
    attendre(150);
    const bool trouve = visibleAcc(QStringLiteral("resultat-electrique-logement")) && !visibleAcc(QStringLiteral("section-electrique"));
    r->clear();
    attendre(150);
    verifier(trouve && visibleAcc(QStringLiteral("section-electrique")), "chercher « differentiel » (sans accent) trouve le tableau de logement ; effacer revient au métier choisi");
  } else
    verifier(false, "la recherche de l'accueil existe");
  capturer(&f, QStringLiteral("01-accueil"));
  // Un vrai clic sur la carte « Schéma électrique »
  if (auto* carte = acc->findChild<QWidget*>(QStringLiteral("choix-electrique-schema"))) cliquerWidget(carte);
  attendre(600);
  AtelierElectrique* a = f.atelierElectrique();
  verifier(a && a->isVisible(), "un clic sur « Schéma électrique » ouvre l'atelier électrique");
  if (!a) return fin();
  const ProjetElectrique* pe = projet(f);
  verifier(pe && pe->schema.feuilles.size() == 3 && pe->folio(pe->schema.feuilles[0].uuid).type == TypeFolio::Garde, "le projet neuf a sa page de garde, un folio de puissance et un de commande");
  verifier(a->mode() == AtelierElectrique::Schema && a->folio() == 1, "il s'ouvre sur le folio de puissance, pas sur la page de garde");
  verifier(!doc->peutAnnuler(), "la création ne laisse pas de pas d'historique vide à annuler");
  verifier(visible<QWidget>(a, QStringLiteral("panneauElectrique")) != nullptr && visible<QWidget>(a, QStringLiteral("groupeReglages")) != nullptr,
           "le panneau de droite montre le folio et les réglages du projet (cartouche, repérage, réseau)");
  {
    bool guide = false;
    for (QLabel* l : a->findChildren<QLabel*>())
      if (l->isVisible() && l->text().contains(QStringLiteral("Choisis un appareil"))) guide = true;
    verifier(guide, "le panneau guide les premiers pas (palette, fils, potentiels, contacts, numéros)");
  }

  // ---------- 2. Palette : recherche, Entrée, clic ----------
  auto* recherche = a->findChild<QLineEdit*>(QStringLiteral("rechercheSymbole"));
  QTreeWidget* palette = a->palette();
  auto idsVisibles = [palette] {
    std::set<std::string> ids;
    for (QTreeWidgetItemIterator it(palette); *it; ++it)
      if ((*it)->data(0, Qt::UserRole).toString() == QLatin1String("symbole")) ids.insert((*it)->data(0, Qt::UserRole + 1).toString().toStdString());
    return ids;
  };
  if (recherche) {
    recherche->setText(QStringLiteral("contacteur"));
    attendre(150);
  }
  const auto trouves = idsVisibles();
  verifier(recherche && trouves.count("iec:bobine-km") && trouves.count("iec:poles-km-3p") && !trouves.count("iec:moteur-3"), "la recherche « contacteur » trouve la bobine et les pôles, pas le moteur",
           std::to_string(trouves.size()) + " symbole(s)");
  if (recherche) {
    recherche->setText(QStringLiteral("voyant"));
    attendre(150);
    emit recherche->returnPressed();
    attendre(150);
  }
  verifier(a->vueSchema()->outil() == VueSchema::Outil::Placer, "Entrée dans la recherche prend le premier symbole trouvé, prêt à poser");
  touche(a->vueSchema(), Qt::Key_Escape);
  if (recherche) recherche->clear();
  attendre(150);
  {
    bool circuits = false;
    for (QTreeWidgetItemIterator it(palette); *it; ++it) circuits |= (*it)->data(0, Qt::UserRole).toString() == QLatin1String("circuit") && !(*it)->isHidden();
    verifier(circuits, "la palette propose les circuits types sur un projet neuf");
  }

  // ---------- 3. Folio de commande : bouton marche, bobine, contact d'auto-maintien, potentiels ----------
  if (auto* folios = a->findChild<QListWidget*>(QStringLiteral("foliosElectriques"))) folios->setCurrentRow(2);
  attendre(200);
  verifier(a->folio() == 2, "un clic dans la liste des folios ouvre le folio de commande");
  VueSchema* vs = a->vueSchema();
  // Bouton marche pris dans la palette (clic sur son nom)
  QTreeWidgetItem* bp = nullptr;
  for (QTreeWidgetItemIterator it(palette); *it; ++it)
    if ((*it)->data(0, Qt::UserRole + 1).toString() == QLatin1String("iec:bp-no")) bp = *it;
  if (bp) emit palette->itemClicked(bp, 0);
  attendre(150);
  verifier(bp && vs->outil() == VueSchema::Outil::Placer && visible<QComboBox>(a, QStringLiteral("referencePose")), "un clic dans la palette arme la pose, le panneau propose la référence");
  vs->cliquerEn(P(20, 30));
  touche(vs, Qt::Key_Escape);
  attendre(150);
  // Bobine de contacteur prise dans le ruban
  verifier(declencher(a, QStringLiteral("Bobine de contacteur")), "le ruban propose la bobine de contacteur");
  vs->cliquerEn(P(20, 50));
  touche(vs, Qt::Key_Escape);
  attendre(200);
  pe = projet(f);
  const ComposantElec* s1 = pe->schema.parReference("S1");
  const ComposantElec* km1 = pe->schema.parReference("KM1");
  verifier(s1 && km1, "le bouton et la bobine sont repérés dès la pose : -S1, -KM1");
  if (!s1 || !km1) return fin();
  const std::string idS1 = s1->uuid, idKM1 = km1->uuid;
  const std::string poseS1 = poseDe(*pe, idS1, "iec:bp-no"), poseKM1 = poseDe(*pe, idKM1, "iec:bobine-km");
  // Contact à fermeture : le menu de pose propose la bobine
  verifier(declencher(a, QStringLiteral("Contact à fermeture")), "le ruban propose le contact à fermeture");
  QString menuVu;
  choisirDansMenu(QStringLiteral("KM1"), QStringLiteral("menuRattacher"), &menuVu);
  vs->cliquerEn(P(28, 30));
  attendre(300);
  touche(vs, Qt::Key_Escape);
  attendre(200);
  pe = projet(f);
  std::string poseContact;
  for (const auto& s : pe->schema.feuilles[2].symboles)
    if (s.symbole == "iec:contact-no") poseContact = s.uuid;
  verifier(!poseContact.empty() && pose(*pe, poseContact)->composant == idKM1 && bornes(*pe, poseContact) == "13-14",
           "le menu de pose rattache le contact à -KM1, il prend les bornes 13-14", menuVu.left(120).toStdString());
  // Potentiels du ruban
  verifier(declencher(a, QStringLiteral("24V")), "le ruban propose les potentiels courants");
  vs->cliquerEn(P(20, 14));
  touche(vs, Qt::Key_Escape);
  declencher(a, QStringLiteral("0V"));
  vs->cliquerEn(P(20, 66));
  touche(vs, Qt::Key_Escape);
  attendre(200);
  pe = projet(f);
  verifier(pe->schema.feuilles[2].etiquettes.size() == 2, "les potentiels 24V et 0V sont posés");

  // ---------- 4. Fils tirés d'une borne à l'autre ----------
  verifier(declencher(a, QStringLiteral("Fil")) && vs->outil() == VueSchema::Outil::Fil, "le bouton Fil du ruban arme le tracé");
  auto tirer = [&](Pt de, Pt vers) {
    vs->setOutil(VueSchema::Outil::Fil);
    vs->cliquerEn(de);
    vs->cliquerEn(vers);
    touche(vs, Qt::Key_Escape);
    attendre(80);
  };
  pe = projet(f);
  const Pt s13 = borne(*pe, poseS1, "13"), s14 = borne(*pe, poseS1, "14");
  const Pt a1 = borne(*pe, poseKM1, "A1"), a2 = borne(*pe, poseKM1, "A2");
  const Pt c13 = borne(*pe, poseContact, "13"), c14 = borne(*pe, poseContact, "14");
  tirer(s13, P(20, 14));
  tirer(s14, a1);
  tirer(a2, P(20, 66));
  tirer(c13, s13);
  tirer(c14, s14);
  vs->setOutil(VueSchema::Outil::Selection);
  attendre(250);
  pe = projet(f);
  {
    const Connectivite k = connectivite(pe->schema);
    const std::string nS13 = k.nom(idS1, "13"), nA2 = k.nom(idKM1, "A2"), nC13 = k.nom(idKM1, "13"), nC14 = k.nom(idKM1, "14"), nA1 = k.nom(idKM1, "A1");
    verifier(nS13 == "24V" && nC13 == "24V" && nA2 == "0V" && nC14 == nA1 && !nA1.empty() && nA1 != "24V",
             "les fils relient le bouton, son contact d'auto-maintien en parallèle et la bobine entre 24V et 0V", nS13 + " " + nC13 + " " + nA2 + " " + nC14 + "/" + nA1);
  }
  capturer(&f, QStringLiteral("02-commande-cablee"));
  capturerZoom(vs, P(26, 40), QStringLiteral("02b-commande-zoom"));

  // ---------- 5. Numéros de fils, vérification, clic sur un problème ----------
  if (auto* b = a->findChild<QToolButton*>(QStringLiteral("boutonNumeroter"))) b->click();
  attendre(250);
  pe = projet(f);
  {
    const Connectivite k = connectivite(pe->schema);
    const std::string n = k.nom(idKM1, "A1");
    verifier(!n.empty() && n.rfind("N-", 0) != 0 && !pe->schema.nomsReseaux.empty(), "« Numéroter les fils » donne un numéro au fil du bouton à la bobine", n);
  }
  verifier(declencher(a, QStringLiteral("Vérifier le projet")), "le bouton Vérifier répond");
  std::string detail;
  verifier(erreurs(a->violations(), &detail) == 0 && compte(a->violations(), "ELE-16") == 1, "la vérification ne trouve pas d'erreur, et signale les pôles de -KM1 pas encore posés", detail);
  {
    QPushButton* probleme = nullptr;
    for (QPushButton* b : a->findChildren<QPushButton*>())
      if (b->isVisible() && b->text().contains(QStringLiteral("pôles de puissance du contacteur"))) probleme = b;
    if (probleme) probleme->click();
    attendre(300);
    verifier(probleme && a->folio() == 2 && vs->selection().count(poseKM1), "un clic sur le problème mène à la bobine de -KM1, choisie");
  }

  // ---------- 6. Pôles posés depuis le panneau : le folio de puissance s'ouvre seul ----------
  {
    vs->selectionner({poseKM1});
    attendre(300);
    QPushButton* poles = bouton(a, QStringLiteral("Pôles de puissance (1-2 3-4 5-6)"));
    verifier(poles != nullptr && visible<QWidget>(a, QStringLiteral("groupeAppareil")), "le panneau de la bobine propose d'ajouter ses pôles de puissance");
    if (poles) poles->click();
    attendre(300);
    verifier(a->folio() == 1 && vs->outil() == VueSchema::Outil::Placer, "« Pôles de puissance » ouvre le folio de puissance, prêt à poser");
    vs->cliquerEn(P(24, 46));
    touche(vs, Qt::Key_Escape);
    attendre(300);
    pe = projet(f);
    const std::string polesKm = poseDe(*pe, idKM1, "iec:poles-km-3p");
    verifier(!polesKm.empty() && bornes(*pe, polesKm) == "1-2-3-4-5-6", "les pôles se posent rattachés à -KM1 : 1-2, 3-4, 5-6", bornes(*pe, polesKm));
    const Renvois r = renvois(*pe);
    const auto it = r.parComposant.find(idKM1);
    verifier(it != r.parComposant.end() && it->second.size() == 3, "la bobine connaît ses trois éléments posés (renvois croisés)");
    a->verifier();
    verifier(compte(a->violations(), "ELE-16") == 0, "le problème des pôles disparaît de la liste");
    capturer(&f, QStringLiteral("03-puissance-poles"));
    capturerZoom(vs, P(24, 46), QStringLiteral("03b-poles-zoom"));
  }

  // ---------- 7. Propriétés d'un appareil : désignation, référence, annuler, rétablir ----------
  a->allerAuFolio(2);
  attendre(200);
  vs->setOutil(VueSchema::Outil::Selection);
  vs->cliquerEn(pose(*projet(f), poseS1)->position);
  attendre(300);
  verifier(vs->selection().count(poseS1) == 1, "un clic sur le bouton le choisit");
  if (auto* e = visible<QLineEdit>(a, QStringLiteral("designationAppareil"))) {
    e->setText(QStringLiteral("Marche pompe"));
    emit e->editingFinished();
  }
  attendre(300);
  verifier(projet(f)->schema.composant(idS1)->description == "Marche pompe", "la désignation saisie dans le panneau arrive dans le projet");
  doc->annuler();
  attendre(200);
  verifier(projet(f)->schema.composant(idS1)->description != "Marche pompe", "annuler rend l'ancienne désignation");
  doc->retablir();
  attendre(200);
  verifier(projet(f)->schema.composant(idS1)->description == "Marche pompe", "rétablir redonne la nouvelle");
  vs->selectionner({poseS1});
  attendre(300);
  if (auto* ref = visible<QComboBox>(a, QStringLiteral("referenceAppareil"))) {
    const int i = ref->findData(QStringLiteral("bp-vert"));
    if (i >= 0) ref->setCurrentIndex(i);
  }
  attendre(300);
  {
    const ComposantElec* x = projet(f)->schema.composant(idS1);
    verifier(x && x->champs.count("catalogue") && x->champs.at("catalogue") == "bp-vert" && !x->description.empty(), "la référence choisie dans la liste du catalogue est retenue");
  }
  // Fil choisi : numéro imposé, section
  {
    pe = projet(f);
    const Pt milieu{(a1.x + s14.x) / 2, (a1.y + s14.y) / 2};
    vs->setOutil(VueSchema::Outil::Selection);
    vs->cliquerEn(milieu);
    attendre(300);
    auto* numero = visible<QLineEdit>(a, QStringLiteral("numeroFil"));
    verifier(numero && !numero->text().isEmpty() && visible<QComboBox>(a, QStringLiteral("sectionFil")), "un clic sur un fil montre son numéro, sa section et sa couleur");
    if (numero) {
      numero->setText(QStringLiteral("105"));
      emit numero->editingFinished();
    }
    attendre(300);
    verifier(connectivite(projet(f)->schema).nom(idKM1, "A1") == "105", "le numéro saisi est donné au fil, et y reste");
    vs->cliquerEn(milieu);
    attendre(300);
    if (auto* sec = visible<QComboBox>(a, QStringLiteral("sectionFil"))) {
      const int i = sec->findData(0.75);
      if (i >= 0) sec->setCurrentIndex(i);
    }
    attendre(300);
    pe = projet(f);
    verifier(pe->conducteurs.count("105") && std::abs(pe->conducteurs.at("105").section - 0.75) < 1e-9, "la section choisie est retenue pour ce fil");
  }
  // Repérage CEI 81346, puis retour aux lettres classiques
  {
    vs->selectionner({});
    attendre(300);
    QComboBox* norme = nullptr;
    if (auto* g = visible<QWidget>(a, QStringLiteral("groupeReglages")))
      for (QComboBox* c : g->findChildren<QComboBox*>())
        if (c->count() == 2 && c->itemText(1).contains(QStringLiteral("81346"))) norme = c;
    if (norme) norme->setCurrentIndex(1);
    attendre(300);
    pe = projet(f);
    verifier(norme && pe->schema.composant(idKM1)->reference == "QA1" && pe->schema.composant(idS1)->reference == "SF1", "le repérage CEI 81346 renomme -KM1 en -QA1 et -S1 en -SF1");
    doc->annuler();
    attendre(300);
    verifier(projet(f)->schema.composant(idKM1)->reference == "KM1", "annuler rend les lettres classiques");
  }
  // Recherche d'un appareil posé : « KM1 » le montre sur son folio
  if (recherche) {
    a->allerAuFolio(1);
    attendre(150);
    recherche->setText(QStringLiteral("KM1"));
    attendre(150);
    QTreeWidgetItem* trouve = nullptr;
    for (QTreeWidgetItemIterator it(palette); *it; ++it)
      if ((*it)->data(0, Qt::UserRole).toString() == QLatin1String("appareil") && (*it)->data(0, Qt::UserRole + 1).toString() == QString::fromStdString(idKM1)) trouve = *it;
    if (trouve) emit palette->itemClicked(trouve, 0);
    attendre(250);
    verifier(trouve && a->folio() == 2 && vs->selection().count(poseKM1), "chercher « KM1 » dans la palette le trouve parmi les appareils posés, et un clic le montre");
    recherche->clear();
    attendre(150);
  }

  // ---------- 8. Bornier : deux bornes numérotées à la suite ----------
  {
    verifier(declencher(a, QStringLiteral("Borne de bornier")), "le ruban propose la borne de bornier");
    vs->cliquerEn(P(70, 40));
    attendre(200);
    vs->cliquerEn(P(72, 40));
    touche(vs, Qt::Key_Escape);
    attendre(300);
    pe = projet(f);
    const ComposantElec* x1 = pe->schema.parReference("X1");
    std::set<std::string> numeros;
    if (x1)
      for (const auto& s : pe->schema.feuilles[2].symboles)
        if (s.composant == x1->uuid) numeros.insert(bornes(*pe, s.uuid).substr(0, bornes(*pe, s.uuid).find('-')));
    verifier(x1 && numeros == std::set<std::string>{"1", "2"}, "la première borne crée -X1, la suivante prend le numéro 2");
    // Troisième borne : le menu propose le bornier existant
    QString vu;
    declencher(a, QStringLiteral("Borne de bornier"));
    choisirDansMenu(QStringLiteral("X1"), QStringLiteral("menuBornier"), &vu);
    vs->cliquerEn(P(74, 40));
    attendre(300);
    touche(vs, Qt::Key_Escape);
    attendre(200);
    pe = projet(f);
    int nX1 = 0;
    for (const auto& s : pe->schema.feuilles[2].symboles)
      if (x1 && s.composant == pe->schema.parReference("X1")->uuid) ++nX1;
    verifier(nX1 == 3, "une nouvelle pose demande le bornier, -X1 prend sa troisième borne", vu.left(100).toStdString());
  }

  // ---------- 9. Rapports et exports ----------
  {
    a->montrerRapport(2);
    attendre(300);
    verifier(a->mode() == AtelierElectrique::Rapports && a->tableRapport()->rowCount() == 3, "le plan de bornier liste les trois bornes de -X1",
             std::to_string(a->tableRapport()->rowCount()) + " ligne(s)");
    capturer(&f, QStringLiteral("04-rapport-borniers"));
    a->montrerRapport(0);
    attendre(200);
    bool s1Liste = false;
    for (int r = 0; r < a->tableRapport()->rowCount(); ++r) s1Liste |= a->tableRapport()->item(r, 0) && a->tableRapport()->item(r, 0)->text() == QStringLiteral("-S1");
    verifier(s1Liste, "la liste des appareils montre -S1 avec sa désignation");
    a->montrerRapport(4);
    attendre(200);
    // 24V : -S1:13 → -KM1:13 ; fil 105 : -S1:14 → -KM1:A1 → -KM1:14 (le 0V ne va qu'au potentiel)
    verifier(a->tableRapport()->rowCount() == 3, "le carnet de câblage liste les liaisons de borne à borne", std::to_string(a->tableRapport()->rowCount()) + " liaison(s)");
    a->montrerRapport(6);
    attendre(200);
    verifier(a->tableRapport()->rowCount() == 3, "la liste des folios en compte trois");
    const QString csv = QDir(dossier).filePath(QStringLiteral("nomenclature.csv"));
    bool km = false;
    if (a->exporterCsv(1, csv)) {
      QFile fc(csv);
      if (fc.open(QIODevice::ReadOnly)) km = fc.readAll().contains("KM1");
    }
    verifier(km, "la nomenclature s'exporte en CSV");
    // Câble : créé dans le rapport des câbles, puis annulé
    a->montrerRapport(3);
    attendre(200);
    auto* nouveauCable = visible<QPushButton>(a, QStringLiteral("nouveauCable"));
    if (nouveauCable) nouveauCable->click();
    attendre(300);
    pe = projet(f);
    verifier(nouveauCable && pe->cables.size() == 1 && pe->cables[0].reference == "W1" && visible<QWidget>(a, QStringLiteral("groupeCable")), "« Nouveau câble » crée -W1 et ouvre ses propriétés",
             std::string(nouveauCable ? "bouton" : "pas de bouton") + ", " + std::to_string(pe->cables.size()) + " câble(s), panneau " +
                 (visible<QWidget>(a, QStringLiteral("groupeCable")) ? "ouvert" : "fermé"));
    doc->annuler();
    attendre(200);
  }

  // ---------- 10. Armoire : créée et implantée en un clic, appareil glissé, porte, perçage ----------
  {
    a->choisirMode(AtelierElectrique::Armoire);
    attendre(300);
    QPushButton* creer = bouton(a, QStringLiteral("Créer l’armoire et y implanter les appareils"));
    verifier(creer != nullptr, "l'onglet Armoire propose de créer l'armoire du projet");
    if (creer) creer->click();
    attendre(400);
    pe = projet(f);
    bool kmPlaque = false, s1Porte = false;
    if (!pe->armoires.empty())
      for (const auto& b : boitesArmoire(*pe, 0)) {
        if (b.composant == idKM1) kmPlaque = !b.porte;
        if (b.composant == idS1) s1Porte = b.porte;
      }
    verifier(pe->armoires.size() == 1 && kmPlaque && s1Porte && bilanArmoire(*pe, 0).horsPlaque == 0, "le contacteur va sur la plaque, le bouton sur la porte, rien ne déborde");
    capturer(&f, QStringLiteral("05-armoire"));
    VueArmoire* va = a->vueArmoire();
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (const auto& b : boitesArmoire(*projet(f), 0))
      if (b.composant == idKM1) {
        x0 = mm(b.b.x0);
        y0 = mm(b.b.y0);
      }
    va->deplacerVers(idKM1, x0 + 120, y0 + 8);
    attendre(300);
    for (const auto& b : boitesArmoire(*projet(f), 0))
      if (b.composant == idKM1) {
        x1 = mm(b.b.x0);
        y1 = mm(b.b.y0);
      }
    verifier(std::abs(x1 - x0 - 120) < 1.01 && std::abs(y1 - y0) < 1e-6 && bilanArmoire(*projet(f), 0).horsPlaque == 0, "le contacteur se glisse le long du rail et s'y raccroche",
             std::to_string(x0) + ", " + std::to_string(y0) + " → " + std::to_string(x1) + ", " + std::to_string(y1));
    // Lâché loin à droite : il reste entier sur la plaque
    va->deplacerVers(idKM1, 5000, y0);
    attendre(300);
    pe = projet(f);
    for (const auto& b : boitesArmoire(*pe, 0))
      if (b.composant == idKM1) x1 = mm(b.b.x1);
    verifier(x1 <= pe->armoires[0].largeur + 1e-6 && bilanArmoire(*pe, 0).horsPlaque == 0, "lâché trop loin, il s'arrête au bord de la plaque", std::to_string(x1));
    doc->annuler();
    doc->annuler();
    attendre(300);
    for (const auto& b : boitesArmoire(*projet(f), 0))
      if (b.composant == idKM1) x1 = mm(b.b.x0);
    verifier(std::abs(x1 - x0) < 1e-6, "chaque glissement s'annule en un pas");
    va->setPorte(true);
    attendre(200);
    capturer(&f, QStringLiteral("06-armoire-porte"));
    va->setPorte(false);
    const std::string dxf = dxfPlaque(*projet(f), 0);
    verifier(dxf.find("ENTITIES") != std::string::npos && dxf.find("EOF") != std::string::npos, "le plan de perçage de la plaque s'écrit en DXF");
    const QStringList ecrits = a->exporterDossier(dossier);
    verifier(ecrits.size() >= 10 && QFileInfo(QDir(dossier).filePath(QStringLiteral("folios.pdf"))).size() > 3000, "« Dossier complet » écrit folios PDF, rapports CSV et perçage DXF",
             std::to_string(ecrits.size()) + " fichier(s)");
  }

  // ---------- 11. Calcul de section ----------
  {
    a->choisirMode(AtelierElectrique::Calculs);
    attendre(300);
    a->choisirCalcul("section");
    attendre(150);
    auto* ib = visible<QDoubleSpinBox>(a, QStringLiteral("champ-ib"));
    auto* l = visible<QDoubleSpinBox>(a, QStringLiteral("champ-longueur"));
    if (ib) ib->setValue(32);
    if (l) l->setValue(40);
    attendre(200);
    auto* res = visible<QLabel>(a, QStringLiteral("resultatCalcul"));
    verifier(ib && l && res && res->text().contains(QStringLiteral("mm²")) && res->text().contains(QStringLiteral("protégé par")), "le calcul de section répond à la saisie (section, calibre, chute de tension)");
    capturer(&f, QStringLiteral("07-calcul-section"));
    // Toute la boîte à calculs : une fiche par calcul, la formule et l'explication avec le résultat
    a->choisirCalcul("eclairage");
    attendre(150);
    auto* res2 = visible<QLabel>(a, QStringLiteral("resultatCalcul"));
    verifier(res2 && res2->text().contains(QStringLiteral("Luminaires")) && res2->text().contains(QStringLiteral("Comment c’est calculé")),
             "la boîte à calculs ouvre la fiche de l'éclairage : nombre de luminaires, explication et formule");
    int fiches = 0, trouves = 0;
    if (auto* liste = a->findChild<QTreeWidget*>(QStringLiteral("listeCalculs")))
      for (QTreeWidgetItemIterator it(liste); *it; ++it) fiches += (*it)->data(0, Qt::UserRole).toString().isEmpty() ? 0 : 1;
    if (auto* r = visible<QLineEdit>(a, QStringLiteral("rechercheCalcul"))) {
      r->setText(QStringLiteral("terre"));
      if (auto* liste = a->findChild<QTreeWidget*>(QStringLiteral("listeCalculs")))
        for (QTreeWidgetItemIterator it(liste, QTreeWidgetItemIterator::NotHidden); *it; ++it) trouves += (*it)->data(0, Qt::UserRole).toString().isEmpty() ? 0 : 1;
      r->clear();
    }
    verifier(fiches >= 18 && trouves >= 1 && trouves < 4, "la liste propose toutes les fiches, la recherche « terre » les trie", std::to_string(fiches) + " fiches, " + std::to_string(trouves) + " trouvée(s)");
    capturer(&f, QStringLiteral("07b-calcul-eclairage"));
  }

  // ---------- 12. Enregistrer, rouvrir ----------
  {
    const QString fichier = QDir(dossier).filePath(QStringLiteral("armoire.prom"));
    doc->enregistrer(fichier);
    const std::string avant = versJson(*projet(f)).dump();
    Document relu;
    relu.ouvrir(fichier, true);
    verifier(relu.projet().electrique.size() == 1 && versJson(relu.projet().electrique[0]).dump() == avant, "enregistrer puis rouvrir rend exactement le même projet électrique");
  }

  // ---------- 13. Arbre du projet, Ctrl+4 ----------
  auto* nav = f.findChild<Navigateur*>();
  auto element = [nav](const QString& cle) -> QTreeWidgetItem* {
    for (QTreeWidgetItemIterator it(nav); *it; ++it)
      if ((*it)->data(0, Navigateur::ROLE_CLE).toString() == cle) return *it;
    return nullptr;
  };
  verifier(nav && element(QStringLiteral("electrique:0")) && element(QStringLiteral("electrique:0:folio:2")) && element(QStringLiteral("electrique:0:armoire")) &&
               element(QStringLiteral("electrique:0:rapports")),
           "l'arbre range le projet électrique : folios, armoire, rapports, calculs");
  if (QTreeWidgetItem* it = element(QStringLiteral("electrique:0:rapports"))) {
    emit nav->itemClicked(it, 0);
    attendre(300);
    verifier(a->mode() == AtelierElectrique::Rapports, "un clic sur « Rapports » dans l'arbre les ouvre");
  }
  if (QTreeWidgetItem* it = element(QStringLiteral("electrique:0:folio:1"))) {
    emit nav->itemClicked(it, 0);
    attendre(300);
    verifier(a->mode() == AtelierElectrique::Schema && a->folio() == 1, "un clic sur un folio dans l'arbre l'ouvre");
  }
  f.montrerAtelierPiece(true);
  attendre(300);
  verifier(declencher(&f, QStringLiteral("Atelier électrique")) && a->isVisible(), "Ctrl+4 (Atelier électrique) ramène au schéma depuis un autre atelier");

  // ---------- 14. Tableau de logement NF C 15-100 ----------
  {
    f.montrerAccueil();
    attendre(300);
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-logement"));
    attendre(800);
    pe = projet(f);
    verifier(pe && a->mode() == AtelierElectrique::Logement && pe->schema.feuilles.size() == 1, "« Tableau de logement » ouvre l'assistant sur un projet neuf");
    auto* pieces = visible<QTableWidget>(a, QStringLiteral("piecesLogement"));
    auto* circuits = visible<QTableWidget>(a, QStringLiteral("circuitsLogement"));
    auto* avis = visible<QLabel>(a, QStringLiteral("avisLogement"));
    verifier(pieces && pieces->rowCount() >= 6 && circuits && circuits->rowCount() >= 8 && avis && avis->text().contains(QStringLiteral("Conforme")),
             "un logement type (70 m², 2 chambres) est proposé, ses circuits calculés et conformes",
             pieces && circuits ? std::to_string(pieces->rowCount()) + " pièces, " + std::to_string(circuits->rowCount()) + " circuits" : std::string());
    capturer(&f, QStringLiteral("08-logement-assistant"));
    // Borne de recharge cochée : un circuit de plus
    const int avant = circuits ? circuits->rowCount() : 0;
    if (auto* c = visible<QCheckBox>(a, QStringLiteral("equipement8"))) c->setChecked(true);
    attendre(200);
    verifier(circuits && circuits->rowCount() == avant + 1, "cocher la borne de recharge ajoute son circuit dédié");
    if (auto* g = visible<QPushButton>(a, QStringLiteral("genererLogement"))) g->click();
    attendre(800);
    pe = projet(f);
    std::string d2;
    const auto v = verifierElectrique(*pe);
    verifier(pe->logement && pe->schema.feuilles.size() >= 2 && pe->armoires.size() == 1 && erreurs(v, &d2) == 0 && a->mode() == AtelierElectrique::Schema,
             "« Créer le schéma unifilaire et le coffret » : unifilaire, coffret implanté, aucune erreur", d2);
    verifier(v.empty(), "le projet du logement ne porte aucun avertissement",
             std::to_string(v.size()) + " avertissement(s)" + (v.empty() ? std::string() : " : " + v.front().code + " " + v.front().message));
    capturer(&f, QStringLiteral("09-logement-unifilaire"));
    if (const QByteArray d = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !d.isEmpty()) a->exporterPdf(QDir(QString::fromLocal8Bit(d)).filePath(QStringLiteral("logement-folios.pdf")));
    capturerZoom(a->vueSchema(), P(30, 28), QStringLiteral("09b-unifilaire-zoom"));
    a->choisirMode(AtelierElectrique::Armoire);
    attendre(400);
    capturer(&f, QStringLiteral("10-logement-coffret"));
    doc->annuler();
    attendre(300);
    verifier(projet(f) && projet(f)->schema.feuilles.size() == 1 && !projet(f)->logement, "la génération s'annule en un pas");
    doc->retablir();
    attendre(300);
  }

  // ---------- 15. Exemple livré, faisceau ----------
  {
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-exemple"));
    attendre(900);
    pe = projet(f);
    std::string d3;
    const auto v = pe ? verifierElectrique(*pe) : std::vector<Violation>{};
    verifier(pe && pe->nom == "Démarrage direct d'un moteur" && erreurs(v, &d3) == 0 && v.empty(), "l'exemple « démarrage d'un moteur » s'ouvre, sans erreur ni avertissement", d3);
    a->allerAuFolio(2);
    attendre(300);
    capturer(&f, QStringLiteral("11-exemple-commande"));
    capturerZoom(a->vueSchema(), P(32, 50), QStringLiteral("11b-exemple-commande-zoom"));
    a->allerAuFolio(1);
    attendre(300);
    capturer(&f, QStringLiteral("12-exemple-puissance"));
    capturerZoom(a->vueSchema(), P(28, 50), QStringLiteral("12b-exemple-puissance-zoom"));
    a->allerAuFolio(0);
    attendre(300);
    capturer(&f, QStringLiteral("12c-exemple-garde"));
    // Folios dessinés d'office : plan des borniers, implantation du coffret, nomenclature
    {
      const ProjetElectrique* ex = projet(f);
      const bool troisFolios = ex && ex->schema.feuilles.size() == 6 && ex->folio(ex->schema.feuilles[3].uuid).type == TypeFolio::Borniers &&
                               ex->folio(ex->schema.feuilles[4].uuid).type == TypeFolio::Implantation && ex->folio(ex->schema.feuilles[5].uuid).type == TypeFolio::Nomenclature;
      verifier(troisFolios, "l'exemple a ses folios dessinés d'office : borniers, implantation, nomenclature");
      const QString pdf = QDir(dossier).filePath(QStringLiteral("folios-office.pdf"));
      verifier(a->exporterPdf(pdf) && QFileInfo(pdf).size() > 8000, "ils partent dans le PDF des folios");
      if (const QByteArray d = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !d.isEmpty()) QFile::copy(pdf, QDir(QString::fromLocal8Bit(d)).filePath(QStringLiteral("exemple-folios.pdf")));
      for (int i = 3; i <= 5; ++i) {
        a->allerAuFolio(i);
        attendre(250);
        capturer(&f, QStringLiteral("12%1-exemple-folio-%2").arg(QChar(u'd' + (i - 3))).arg(i + 1));
      }
    }
    a->allerAuFolio(1);
    attendre(200);
    const ComposantElec* x1 = pe->schema.parReference("X1");
    const ComposantElec* m1 = pe->schema.parReference("M1");
    const int i = a->nouveauFaisceau();
    attendre(300);
    VueFaisceau* vf = a->vueFaisceau();
    verifier(i == 0 && a->mode() == AtelierElectrique::Faisceau && vf->outil() == VueFaisceau::Outil::Noeud, "« Nouveau faisceau » ouvre la planche, prête à poser des nœuds");
    vf->cliquerEn(40, 60);
    vf->cliquerEn(340, 60);
    vf->setOutil(VueFaisceau::Outil::Branche);
    vf->cliquerEn(40, 60);
    vf->cliquerEn(340, 60);
    vf->setOutil(VueFaisceau::Outil::Selection);
    attendre(300);
    pe = projet(f);
    const Faisceau* fx = pe->faisceaux.empty() ? nullptr : &pe->faisceaux[0];
    verifier(fx && fx->noeuds.size() == 2 && fx->branches.size() == 1 && std::abs(longueurBranche(*fx, fx->branches[0]) - 300) < 1, "deux nœuds et une branche de 300 mm tracés à la souris");
    // Connecteurs des extrémités choisis dans le panneau
    auto donner = [&](double x, const ComposantElec* c) {
      vf->cliquerEn(x, 60);
      attendre(300);
      if (auto* cb = visible<QComboBox>(a, QStringLiteral("connecteurNoeud")); cb && c) {
        const int k = cb->findData(QString::fromStdString(c->uuid));
        if (k >= 0) cb->setCurrentIndex(k);
      }
      attendre(300);
    };
    donner(40, x1);
    donner(340, m1);
    pe = projet(f);
    const auto fils = filsFaisceau(*pe, 0);
    bool longueurs = !fils.empty();
    for (const auto& w : fils) longueurs = longueurs && w.longueur >= 300;
    verifier(x1 && m1 && fils.size() == 4 && longueurs, "le faisceau de -X1 à -M1 porte les trois phases et le PE, chacun à sa longueur", std::to_string(fils.size()) + " fil(s)");
    capturer(&f, QStringLiteral("13-faisceau"));
    const QString pdf = QDir(dossier).filePath(QStringLiteral("exemple.pdf"));
    verifier(a->exporterPdf(pdf) && QFileInfo(pdf).size() > 5000, "les folios de l'exemple s'exportent en PDF");
  }

  // ---------- 16. Import d'un projet QElectroTech ----------
  {
    const QString qet = QDir(dossier).filePath(QStringLiteral("essai.qet"));
    QFile fq(qet);
    if (fq.open(QIODevice::WriteOnly)) fq.write(QET_ESSAI);
    fq.close();
    const std::size_t avant = doc->projet().electrique.size();
    const bool ok = f.importerQetElectrique(qet, false);
    attendre(500);
    for (QWidget* w : QApplication::topLevelWidgets())
      if (auto* b = qobject_cast<QMessageBox*>(w); b && b->windowTitle() == QStringLiteral("Projet QElectroTech importé")) b->close();
    pe = projet(f, static_cast<int>(avant));
    const ComposantElec* km = pe ? pe->schema.parReference("KM1") : nullptr;
    int posesKm = 0;
    if (km)
      for (const auto& s : pe->schema.feuilles[0].symboles) posesKm += s.composant == km->uuid;
    verifier(ok && doc->projet().electrique.size() == avant + 1 && a->projet() == static_cast<int>(avant) && km && posesKm == 2 && pe->schema.parReference("X1"),
             "un projet QElectroTech s'importe à côté : bobine et contact esclave réunis sous -KM1, borne -X1");
    // Supprimer ce projet depuis l'arbre, puis annuler
    repondre(QMessageBox::YesRole);
    emit nav->supprimerProjetElectrique(static_cast<int>(avant));
    attendre(700);
    verifier(doc->projet().electrique.size() == avant && a->projet() < static_cast<int>(avant), "supprimer le projet importé ramène à celui qui reste");
    doc->annuler();
    attendre(400);
    verifier(doc->projet().electrique.size() == avant + 1, "la suppression s'annule");
  }

  // ---------- 17. Ajouter un schéma au projet depuis l'arbre ; Fichier › Nouveau projet ----------
  {
    const std::size_t avant = doc->projet().electrique.size();
    if (QTreeWidgetItem* it = element(QStringLiteral("ajouter:electrique"))) {
      emit nav->itemClicked(it, 0);
      attendre(500);
    }
    verifier(doc->projet().electrique.size() == avant + 1 && a->projet() == static_cast<int>(avant) && a->isVisible(), "« Ajouter un schéma électrique » dans l'arbre en ajoute un au projet et l'ouvre");
    verifier(declencher(&f, QStringLiteral("&Nouveau projet…")) && acc->isVisible(), "Fichier › Nouveau projet (Ctrl+N) ramène à l'accueil, où tout se crée");
  }

  // ---------- 18. Circuits types : posés câblés et repérés, depuis le ruban et la palette ----------
  {
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-schema"));
    attendre(700);
    auto* bt = a->findChild<QToolButton*>(QStringLiteral("boutonCircuitType"));
    verifier(bt && bt->menu() && bt->menu()->actions().size() == 6 && bt->popupMode() == QToolButton::InstantPopup, "le ruban propose six circuits types, dans une liste");
    verifier(declencher(a, QStringLiteral("Démarrage direct d'un moteur")), "« Démarrage direct d'un moteur » se choisit dans la liste");
    attendre(300);
    pe = projet(f);
    std::string d4;
    const auto v = verifierElectrique(*pe);
    for (const auto& x : v) d4 += x.code + " " + x.message + " ; ";
    verifier(pe->schema.parReference("KM1") && pe->schema.parReference("M1") && pe->schema.parReference("Q1") && v.empty() && a->folio() == 2,
             "le démarrage direct arrive câblé et repéré (-Q1, -KM1, -M1…), sans erreur ni avertissement", d4);
    capturer(&f, QStringLiteral("14-circuit-type"));
    QTreeWidgetItem* item = nullptr;
    for (QTreeWidgetItemIterator it(palette); *it; ++it)
      if ((*it)->data(0, Qt::UserRole).toString() == QLatin1String("circuit") && (*it)->text(0) == QStringLiteral("Éclairage par télérupteur")) item = *it;
    if (item) emit palette->itemClicked(item, 0);
    attendre(300);
    pe = projet(f);
    verifier(item && pe->schema.parReference("KA1") && pe->folio(pe->schema.feuilles[static_cast<std::size_t>(a->folio())].uuid).type == TypeFolio::Distribution &&
                 verifierElectrique(*pe).empty(),
             "un clic dans la palette pose l'éclairage par télérupteur, sur un folio du bâtiment créé pour lui");
    capturer(&f, QStringLiteral("15-circuit-batiment"));
    doc->annuler();
    attendre(300);
    verifier(!projet(f)->schema.parReference("KA1") && projet(f)->schema.parReference("KM1"), "Ctrl+Z retire le circuit posé, et lui seul");
  }

  // ---------- 19. Installation calculée : modèles, départs, réglages, unifilaire, note de calcul ----------
  {
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-distribution"));
    attendre(700);
    pe = projet(f);
    verifier(pe && !pe->distribution && a->mode() == AtelierElectrique::Distribution && visible<QWidget>(a, QStringLiteral("distributionVide")),
             "« Installation électrique (calcul) » ouvre l'onglet Distribution sur les modèles d'installation");
    capturer(&f, QStringLiteral("16-distribution-modeles"));
    if (auto* b = a->findChild<QPushButton*>(QStringLiteral("modele-%1").arg(static_cast<int>(ModeleReseau::Tertiaire)))) cliquerWidget(b);
    attendre(400);
    pe = projet(f);
    verifier(pe && pe->distribution && pe->distribution->departs.size() == 14 && a->resultatDistribution().erreurs == 0 && a->tableDistribution()->rowCount() == 15 &&
                 a->arbreDistribution()->topLevelItemCount() == 1,
             "le modèle « Bâtiment tertiaire » se charge : 14 départs calculés sans erreur, sous la source, avec le tableau des résultats");
    capturer(&f, QStringLiteral("17-distribution-tertiaire"));
    auto itemDe = [&](const QString& repere) -> QTreeWidgetItem* {
      for (QTreeWidgetItemIterator it(a->arbreDistribution()); *it; ++it)
        if ((*it)->text(0).contains(QStringLiteral("%1 · ").arg(repere))) return *it;
      return nullptr;
    };
    auto resultat = [&](const std::string& repere) -> const ResultatDepartBT* {
      const ProjetElectrique* x = projet(f);
      if (!x || !x->distribution) return nullptr;
      for (std::size_t i = 0; i < x->distribution->departs.size(); ++i)
        if (x->distribution->departs[i].repere == repere && i < a->resultatDistribution().departs.size()) return &a->resultatDistribution().departs[i];
      return nullptr;
    };
    // Un départ choisi dans l'arbre : ses réglages et la justification de son calcul à droite
    if (QTreeWidgetItem* it = itemDe(QStringLiteral("Q10"))) a->arbreDistribution()->setCurrentItem(it);
    attendre(300);
    auto* detail = visible<QLabel>(a, QStringLiteral("detailDepart"));
    verifier(detail && detail->text().contains(QStringLiteral("Iz")) && detail->text().contains(QStringLiteral("aide:")) && visible<QDoubleSpinBox>(a, QStringLiteral("longueurDepart")),
             "un départ choisi dans l'arbre montre ses réglages et la justification de son calcul, chaque grandeur avec sa fiche d'aide");
    capturer(&f, QStringLiteral("18-distribution-depart"));
    // Ligne rallongée : la section suit ; Ctrl+Z revient
    const double sectionAvant = resultat("Q10") ? resultat("Q10")->section : 0;
    if (auto* l = visible<QDoubleSpinBox>(a, QStringLiteral("longueurDepart"))) {
      l->setValue(180);
      emit l->editingFinished();
    }
    attendre(400);
    const double sectionApres = resultat("Q10") ? resultat("Q10")->section : 0;
    verifier(sectionApres > sectionAvant && detail && visible<QLabel>(a, QStringLiteral("detailDepart")),
             "rallonger la ligne du groupe froid à 180 m fait grossir sa section", nombre(sectionAvant) + " → " + nombre(sectionApres) + " mm²");
    doc->annuler();
    attendre(400);
    verifier(resultat("Q10") && std::abs(resultat("Q10")->section - sectionAvant) < 1e-9, "Ctrl+Z rend la longueur et la section d'avant");
    // Un moteur ajouté depuis le ruban, au tableau du départ choisi (le tableau général)
    const std::size_t n0 = projet(f)->distribution->departs.size();
    std::string tgbt;
    for (const auto& d : projet(f)->distribution->departs)
      if (d.repere == "TGBT") tgbt = d.uuid;
    verifier(declencher(a, QStringLiteral("Moteur")), "le ruban propose les départs par type (éclairage, prises, moteur, borne…)");
    attendre(300);
    pe = projet(f);
    const DepartBT* nouveau = nullptr;
    for (const auto& d : pe->distribution->departs)
      if (d.uuid == a->departChoisi()) nouveau = &d;
    verifier(pe->distribution->departs.size() == n0 + 1 && nouveau && nouveau->charge == TypeCharge::Moteur && nouveau->parent == tgbt,
             "« Départ › Moteur » ajoute un moteur au tableau général, aussitôt choisi pour le régler");
    doc->annuler();
    attendre(300);
    // Des prises sans 30 mA : l'erreur est signalée, à sa place
    if (QTreeWidgetItem* it = itemDe(QStringLiteral("Q3"))) a->arbreDistribution()->setCurrentItem(it);
    attendre(300);
    if (auto* c = visible<QComboBox>(a, QStringLiteral("differentielDepart"))) c->setCurrentIndex(0);
    attendre(400);
    verifier(resultat("Q3") && resultat("Q3")->erreurs() > 0 && a->resultatDistribution().erreurs > 0 && visible<QGroupBox>(a, QStringLiteral("avisDistribution")),
             "des prises sans différentiel 30 mA sont en erreur, l'avis cite la règle (NF C 15-100 411.3.3)");
    capturer(&f, QStringLiteral("18b-distribution-erreur"));
    doc->annuler();
    attendre(300);
    // La source : un groupe électrogène abaisse l'Icc de toute l'installation
    const double ikTransfo = a->resultatDistribution().ik3Source;
    a->choisirDepart({});
    attendre(250);
    if (auto* c = visible<QComboBox>(a, QStringLiteral("typeSource"))) c->setCurrentIndex(static_cast<int>(TypeSourceBT::Groupe));
    attendre(400);
    verifier(projet(f)->distribution->source.type == TypeSourceBT::Groupe && a->resultatDistribution().ik3Source < ikTransfo / 2,
             "un groupe électrogène en source abaisse le courant de court-circuit de toute l'installation",
             nombre(ikTransfo) + " → " + nombre(a->resultatDistribution().ik3Source) + " kA");
    doc->annuler();
    attendre(300);
    // Unifilaire dessiné d'après le calcul
    if (auto* b = a->findChild<QPushButton*>(QStringLiteral("dessinerUnifilaire"))) cliquerWidget(b);
    attendre(600);
    pe = projet(f);
    int foliosDistribution = 0;
    for (const auto& x : pe->schema.feuilles) foliosDistribution += pe->folio(x.uuid).origine == "distribution" ? 1 : 0;
    std::string d5;
    verifier(foliosDistribution == 3 && a->mode() == AtelierElectrique::Schema && a->folio() >= 0 &&
                 pe->folio(pe->schema.feuilles[static_cast<std::size_t>(a->folio())].uuid).origine == "distribution" && erreurs(verifierElectrique(*pe), &d5) == 0,
             "« Dessiner l’unifilaire » ajoute un folio par tableau, annoté des résultats, sans erreur au schéma", d5);
    capturer(&f, QStringLiteral("19-distribution-unifilaire"));
    if (const QByteArray cap = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !cap.isEmpty()) a->exporterPdf(QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("distribution-folios.pdf")));
    capturerZoom(a->vueSchema(), P(38, 46), QStringLiteral("19b-unifilaire-zoom"));
    // Note de calcul
    a->choisirMode(AtelierElectrique::Distribution);
    attendre(250);
    const QString notePdf = QDir(dossier).filePath(QStringLiteral("note.pdf")), noteCsv = QDir(dossier).filePath(QStringLiteral("note.csv"));
    verifier(a->exporterNoteDeCalcul(notePdf) && QFileInfo(notePdf).size() > 10000 && a->exporterNoteDeCalcul(noteCsv) && QFileInfo(noteCsv).size() > 1000,
             "la note de calcul s'écrit en PDF et en CSV");
    if (const QByteArray cap = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !cap.isEmpty()) {
      QFile::remove(QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("note-de-calcul.pdf")));
      QFile::copy(notePdf, QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("note-de-calcul.pdf")));
    }
    // Bornes de recharge depuis l'accueil
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-irve"));
    attendre(800);
    pe = projet(f);
    int bornesIrve = 0;
    if (pe && pe->distribution)
      for (const auto& d : pe->distribution->departs) bornesIrve += d.charge == TypeCharge::IRVE ? 1 : 0;
    verifier(bornesIrve == 8 && a->mode() == AtelierElectrique::Distribution && a->resultatDistribution().erreurs == 0,
             "« Bornes de recharge » ouvre un parking de huit bornes, calculé sans erreur");
    capturer(&f, QStringLiteral("20-distribution-irve"));
  }

  // ---------- 20. Photovoltaïque : tailles types, site, matériel, unifilaire, note de calcul ----------
  {
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-photovoltaique"));
    attendre(700);
    pe = projet(f);
    verifier(pe && !pe->photovoltaique && a->mode() == AtelierElectrique::Photovoltaique && visible<QWidget>(a, QStringLiteral("pvVide")),
             "« Installation photovoltaïque » ouvre l'onglet Photovoltaïque sur les tailles d'installation");
    capturer(&f, QStringLiteral("21-pv-tailles"));
    if (auto* b = a->findChild<QPushButton*>(QStringLiteral("pv-6"))) cliquerWidget(b);
    attendre(500);
    pe = projet(f);
    verifier(pe && pe->photovoltaique && a->resultatPV().erreurs() == 0 && a->resultatPV().chaines >= 1 && visible<QLabel>(a, QStringLiteral("resultatsPV")) &&
                 visible<QWidget>(a, QStringLiteral("vueCalepinagePV")) && visible<QGroupBox>(a, QStringLiteral("groupeModulePV")),
             "la maison de 6 kWc se calcule sans erreur : calepinage dessiné, résultats justifiés, matériel et site à droite");
    capturer(&f, QStringLiteral("22-pv-maison"));
    // Grand froid de montagne : la tension à vide monte, les chaînes raccourcissent pour rester sous 600 V
    const int serieAvant = a->resultatPV().modulesParChaine;
    if (auto* t = visible<QDoubleSpinBox>(a, QStringLiteral("tMinPV"))) {
      t->setValue(-25);
      emit t->editingFinished();
    }
    attendre(400);
    verifier(a->resultatPV().modulesParChaine < serieAvant && a->resultatPV().uocChaine <= 600 && a->resultatPV().erreurs() == 0,
             "par −25 °C, les chaînes raccourcissent pour que leur tension à vide reste sous 600 V",
             std::to_string(serieAvant) + " → " + std::to_string(a->resultatPV().modulesParChaine) + " modules, " + nombre(a->resultatPV().uocChaine) + " V");
    doc->annuler();
    attendre(300);
    verifier(a->resultatPV().modulesParChaine == serieAvant, "Ctrl+Z rend la température et les chaînes d'avant");
    // Matériel tiré du ruban
    verifier(declencher(a, QStringLiteral("Module 500 Wc (monocristallin, 132 demi-cellules)")) && projet(f)->photovoltaique->module.puissance == 500,
             "le ruban change le module de toute l'installation en un clic");
    doc->annuler();
    attendre(300);
    // Vente totale : disjoncteur de branchement et compteur de production
    if (auto* c = visible<QCheckBox>(a, QStringLiteral("venteTotalePV"))) c->setChecked(true);
    attendre(400);
    verifier(projet(f)->photovoltaique->venteTotale && a->resultatPV().reglageBranchement > 0, "en vente totale, le disjoncteur de branchement est réglé d'après le courant des onduleurs");
    // Unifilaire dessiné d'après le calcul
    if (auto* b = a->findChild<QPushButton*>(QStringLiteral("dessinerUnifilairePV"))) cliquerWidget(b);
    attendre(600);
    pe = projet(f);
    int foliosPV = 0;
    for (const auto& x : pe->schema.feuilles) foliosPV += pe->folio(x.uuid).origine == "photovoltaique" ? 1 : 0;
    std::string dpv;
    verifier(foliosPV == 1 && a->mode() == AtelierElectrique::Schema && a->folio() >= 0 &&
                 pe->folio(pe->schema.feuilles[static_cast<std::size_t>(a->folio())].uuid).origine == "photovoltaique" && erreurs(verifierElectrique(*pe), &dpv) == 0 &&
                 compte(verifierElectrique(*pe), "ERC-30") == 0,
             "« Dessiner l’unifilaire » trace chaînes, protections, onduleur et branchement, sans erreur au schéma", dpv);
    capturer(&f, QStringLiteral("23-pv-unifilaire"));
    if (const QByteArray cap = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !cap.isEmpty()) a->exporterPdf(QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("photovoltaique-folios.pdf")));
    capturerZoom(a->vueSchema(), P(40, 40), QStringLiteral("23b-pv-unifilaire-zoom"));
    // Note de calcul
    a->choisirMode(AtelierElectrique::Photovoltaique);
    attendre(250);
    const QString notePV = QDir(dossier).filePath(QStringLiteral("note-pv.pdf"));
    verifier(a->exporterNotePV(notePV) && QFileInfo(notePV).size() > 10000, "la note de calcul photovoltaïque s'écrit en PDF");
    if (const QByteArray cap = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !cap.isEmpty()) {
      QFile::remove(QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("note-photovoltaique.pdf")));
      QFile::copy(notePV, QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("note-photovoltaique.pdf")));
    }
    // Une grande toiture : deux onduleurs, chaînes réparties, sans erreur
    verifier(declencher(a, QStringLiteral("250 kWc")) && projet(f)->photovoltaique->onduleurs == 2 && a->resultatPV().erreurs() == 0,
             "250 kWc depuis le ruban : deux onduleurs de 100 kVA, chaînes réparties, sans erreur");
    capturer(&f, QStringLiteral("24-pv-grande-toiture"));
    verifier(a->genererUnifilairePV() >= 0 && erreurs(verifierElectrique(*projet(f))) == 0 && compte(verifierElectrique(*projet(f)), "ERC-30") == 0,
             "son unifilaire (deux onduleurs, protection générale) se redessine à la place du précédent, sans erreur");
    if (const QByteArray cap = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !cap.isEmpty()) a->exporterPdf(QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("photovoltaique-250-folios.pdf")));
  }

  // ---------- 21. Apprendre : parcours guidé, étapes validées par le projet ----------
  {
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-apprendre"));
    attendre(700);
    verifier(a->parcours() == "moteur" && a->etapeParcours() == 0 && a->mode() == AtelierElectrique::Schema && visible<QGroupBox>(a, QStringLiteral("carteParcours")) &&
                 visible<QLabel>(a, QStringLiteral("texteEtape")),
             "« Apprendre pas à pas » ouvre un schéma neuf et la carte du premier parcours, étape 1");
    capturer(&f, QStringLiteral("25-apprendre"));
    // L'étape se fait comme on le lirait : disjoncteur moteur pris dans le ruban, posé sur le folio
    if (auto* folios = a->findChild<QListWidget*>(QStringLiteral("foliosElectriques"))) folios->setCurrentRow(1);
    attendre(200);
    verifier(declencher(a, QStringLiteral("Disjoncteur moteur")), "le ruban propose le disjoncteur moteur");
    a->vueSchema()->cliquerEn(P(30, 30));
    touche(a->vueSchema(), Qt::Key_Escape);
    attendre(400);
    verifier(a->etapeParcours() == 1 && visible<QLabel>(a, QStringLiteral("titreEtape")) &&
                 visible<QLabel>(a, QStringLiteral("titreEtape"))->text().contains(QStringLiteral("bobine")),
             "le disjoncteur posé, l'étape se valide toute seule et la suivante s'affiche (la bobine)");
    capturer(&f, QStringLiteral("25b-apprendre-etape-2"));
    // Une étape de lecture se passe par « Compris » ; la dernière termine le parcours
    const int n = static_cast<int>(prom::elec::parcoursElectrique("moteur")->etapes.size());
    a->allerEtapeParcours(n - 1);
    attendre(200);
    if (auto* b = visible<QPushButton>(a, QStringLiteral("etapeSuivante"))) cliquerWidget(b);
    attendre(250);
    verifier(a->etapeParcours() == n && visible<QPushButton>(a, QStringLiteral("fermerParcours")), "« Compris » sur la dernière étape termine le parcours");
    // Un autre parcours : le photovoltaïque, dans un projet neuf, sur son onglet
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    emit a->parcoursDemande(QStringLiteral("photovoltaique"));
    attendre(700);
    verifier(a->parcours() == "photovoltaique" && a->mode() == AtelierElectrique::Photovoltaique && a->etapeParcours() == 0 && visible<QGroupBox>(a, QStringLiteral("carteParcours")),
             "le parcours photovoltaïque s'ouvre dans un projet neuf, sur l'onglet Photovoltaïque");
    if (auto* b = a->findChild<QPushButton*>(QStringLiteral("pv-6"))) cliquerWidget(b);
    attendre(500);
    verifier(a->etapeParcours() == 1, "choisir une taille valide la première étape");
    capturer(&f, QStringLiteral("26-apprendre-pv"));
    a->quitterParcours();
    attendre(150);
    verifier(!visible<QGroupBox>(a, QStringLiteral("carteParcours")), "« Quitter le parcours » retire la carte");
  }

  // ---------- 22. Fabrication : folio en DXF, nomenclature chiffrée, liste de coupe, chiffrage, échauffement, révision ----------
  {
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-exemple"));
    attendre(900);
    // Le folio de commande en DXF : le dessin même du PDF, rangé par calques
    const QString dxf = QDir(dossier).filePath(QStringLiteral("commande.dxf"));
    const int entites = a->exporterDxf(dxf, 2);
    QByteArray contenu;
    {
      QFile fd(dxf);
      if (fd.open(QIODevice::ReadOnly)) contenu = fd.readAll();
    }
    verifier(entites > 50 && contenu.contains("AC1009") && contenu.contains("\r\nSYMBOLES\r\n") && contenu.contains("\r\nFILS\r\n") && contenu.contains("\r\nREPERES\r\n") &&
                 contenu.contains("\r\nTEXT\r\n") && contenu.contains("-KM1") && contenu.endsWith("EOF\r\n"),
             "le folio de commande s'écrit en DXF : calques du cadre, des fils, des symboles et des repères", std::to_string(entites) + " entité(s)");
    if (const QByteArray cap = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !cap.isEmpty()) QFile::copy(dxf, QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("exemple-commande.dxf")));
    // Nomenclature chiffrée : prix indicatif, prix saisi sur place, total
    a->montrerRapport(1);
    attendre(300);
    QTableWidget* t = a->tableRapport();
    int ligneKm = -1;
    for (int r = 0; r < t->rowCount(); ++r)
      if (t->item(r, 2) && t->item(r, 2)->text().startsWith(QStringLiteral("Contacteur"))) ligneKm = r;
    QTableWidgetItem* prix = ligneKm >= 0 ? t->item(ligneKm, 4) : nullptr;
    verifier(prix && (prix->flags() & Qt::ItemIsEditable) && !prix->text().isEmpty() && visible<QLabel>(a, QStringLiteral("totalNomenclature")),
             "la nomenclature montre le prix indicatif de chaque ligne, modifiable sur place, et le total");
    const std::string cle = prix ? prix->data(Qt::UserRole).toString().toStdString() : std::string();
    if (prix) prix->setText(QStringLiteral("31,50"));
    attendre(400);
    pe = projet(f);
    verifier(pe && pe->prix.count(cle) && std::abs(pe->prix.at(cle) - 31.5) < 1e-9, "un prix saisi dans le tableau est gardé dans le projet");
    capturer(&f, QStringLiteral("27-nomenclature-prix"));
    doc->annuler();
    attendre(300);
    verifier(projet(f)->prix.empty(), "la saisie d'un prix s'annule");
    // Liste de coupe : fils mesurés par les goulottes, embouts, filerie à commander
    a->montrerRapport(8);
    attendre(300);
    verifier(a->tableRapport()->rowCount() > 10 && visible<QGroupBox>(a, QStringLiteral("groupeFilerie")), "la liste de coupe donne chaque fil à couper, ses embouts et la filerie",
             std::to_string(a->tableRapport()->rowCount()) + " fil(s)");
    capturer(&f, QStringLiteral("28-liste-de-coupe"));
    // Chiffrage : taux horaire dans le panneau, total
    a->montrerRapport(9);
    attendre(300);
    auto* taux = visible<QDoubleSpinBox>(a, QStringLiteral("tauxHoraire"));
    if (taux) {
      taux->setValue(60);
      emit taux->editingFinished();
    }
    attendre(300);
    pe = projet(f);
    verifier(taux && pe && std::abs(pe->tauxHoraire - 60) < 1e-9 && visible<QLabel>(a, QStringLiteral("totalChiffrage")), "le chiffrage suit le taux horaire saisi");
    capturer(&f, QStringLiteral("29-chiffrage"));
    // Échauffement de l'armoire : pose et températures dans le panneau, ventilation calculée
    a->choisirMode(AtelierElectrique::Armoire);
    attendre(300);
    auto* ambiant = visible<QDoubleSpinBox>(a, QStringLiteral("temperatureExterieure"));
    if (ambiant) {
      ambiant->setValue(44);
      emit ambiant->editingFinished();
    }
    attendre(300);
    pe = projet(f);
    const auto* res = visible<QLabel>(a, QStringLiteral("resultatThermique"));
    verifier(ambiant && pe && !pe->armoires.empty() && std::abs(pe->armoires[0].temperatureExterieure - 44) < 1e-9 && res && res->text().contains(QStringLiteral("W")),
             "le bilan thermique de l'armoire suit la température du local");
    capturer(&f, QStringLiteral("30-armoire-thermique"));
    // Révision : nouvel indice au cartouche, historique sur la page de garde
    a->choisirMode(AtelierElectrique::Schema);
    a->allerAuFolio(0);
    attendre(300);
    a->vueSchema()->cliquerEn(P(5, 5));  // rien de choisi : le panneau montre le projet
    attendre(300);
    auto* quoi = visible<QLineEdit>(a, QStringLiteral("descriptionRevision"));
    auto* nouvelle = visible<QPushButton>(a, QStringLiteral("nouvelleRevision"));
    if (quoi) quoi->setText(QStringLiteral("Ajout du chiffrage"));
    if (nouvelle) cliquerWidget(nouvelle);
    attendre(400);
    pe = projet(f);
    verifier(quoi && nouvelle && pe && pe->revisions.size() == 1 && pe->revisions[0].description == "Ajout du chiffrage" &&
                 pe->schema.cartouche.revision == pe->revisions[0].indice,
             "« Révision » enregistre l'indice, la date et ce qui change ; le cartouche le porte");
    capturer(&f, QStringLiteral("31-revision-garde"));
    // Saisie sur place : un texte posé sur le folio sans fenêtre ; Entrée valide, Échap renonce
    a->allerAuFolio(2);
    attendre(200);
    VueSchema* vs = a->vueSchema();
    const std::size_t textesAvant = projet(f)->schema.feuilles[2].textes.size();
    vs->setOutil(VueSchema::Outil::Texte);
    vs->cliquerEn(P(150, 160));
    attendre(200);
    QLineEdit* saisie = vs->saisieEnCours();
    const bool champ = saisie && saisie->isVisible() && saisie->window()->focusWidget() == saisie;
    const std::string detailChamp = !saisie ? "pas de champ" : !saisie->isVisible() ? "champ caché" : saisie->window()->focusWidget() != saisie ? "champ sans le focus" : "champ prêt";
    if (saisie) {
      saisie->setText(QStringLiteral("Note de câblage"));
      touche(saisie, Qt::Key_Return);
    }
    attendre(250);
    pe = projet(f);
    verifier(champ && pe->schema.feuilles[2].textes.size() == textesAvant + 1 && pe->schema.feuilles[2].textes.back().texte == "Note de câblage" && !vs->saisieEnCours(),
             "l'outil Texte ouvre un champ sur le folio, au point cliqué ; Entrée pose le texte",
             detailChamp + ", " + std::to_string(pe->schema.feuilles[2].textes.size() - textesAvant) + " texte(s) posé(s)");
    vs->cliquerEn(P(150, 170));
    attendre(200);
    if (QLineEdit* s2 = vs->saisieEnCours()) {
      s2->setText(QStringLiteral("à jeter"));
      touche(s2, Qt::Key_Escape);
    }
    attendre(250);
    verifier(projet(f)->schema.feuilles[2].textes.size() == textesAvant + 1 && !vs->saisieEnCours(), "Échap referme le champ sans rien poser");
    vs->setOutil(VueSchema::Outil::Selection);
    doc->annuler();
    attendre(200);
    // Folio supprimé sans question : la barre d'état le dit, Ctrl+Z le rend
    a->allerAuFolio(5);
    attendre(200);
    QToolButton* supprimer = nullptr;
    for (auto* b : a->findChildren<QToolButton*>())
      if (b->toolTip() == QStringLiteral("Supprimer le folio") && b->isVisible()) supprimer = b;
    const std::size_t foliosAvant = projet(f)->schema.feuilles.size();
    if (supprimer) cliquerWidget(supprimer);
    attendre(300);
    const bool parti = projet(f)->schema.feuilles.size() + 1 == foliosAvant;
    doc->annuler();
    attendre(300);
    verifier(supprimer && parti && projet(f)->schema.feuilles.size() == foliosAvant, "un folio se supprime d'un clic, sans fenêtre, et Ctrl+Z le rend");
    // Double-clic sur un folio : son titre se modifie dans le panneau, prêt à la frappe
    if (auto* liste = a->findChild<QListWidget*>(QStringLiteral("foliosElectriques")); liste && liste->count() > 2) {
      QListWidgetItem* it = liste->item(2);
      emit liste->itemDoubleClicked(it);
      attendre(250);
    }
    auto* titreFolio = visible<QLineEdit>(a, QStringLiteral("titreFolio"));
    verifier(titreFolio && titreFolio->window()->focusWidget() == titreFolio && a->folio() == 2, "double-cliquer un folio met son titre en saisie dans le panneau",
             std::string(titreFolio ? "titre visible" : "pas de titre") + ", folio " + std::to_string(a->folio() + 1));
  }

  // ---------- 23. Grafcet : grafcet vierge, édition dans le panneau, simulation, folio, texte structuré ----------
  {
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-grafcet"));
    attendre(700);
    pe = projet(f);
    verifier(a->mode() == AtelierElectrique::Grafcet && pe && pe->grafcets.size() == 1 && pe->grafcets[0].etapes.size() == 1 && visible<VueGrafcet>(a, QStringLiteral("vueGrafcet")),
             "« Grafcet (automatisme) » ouvre l'onglet Grafcet, l'étape initiale 0 posée");
    capturer(&f, QStringLiteral("32-grafcet-vierge"));
    // L'étape 0 choisie au dessin, puis « Étape suivante » au ruban : la transition est prête à écrire
    {
      // Un clic au centre du carré de l'étape 0 la choisit (fiche à droite)
      const prom::elec::DispositionGrafcet d0 = prom::elec::disposerGrafcet(pe->grafcets[0]);
      for (const auto& z : d0.zones)
        if (z.etape == 0 && z.transition < 0) {
          a->vueGrafcet()->cliquerEn((z.x0 + z.x1) / 2, (z.y0 + z.y1) / 2);
          break;
        }
      attendre(200);
      verifier(visible<QGroupBox>(a, QStringLiteral("groupeEtape")) && visible<QCheckBox>(a, QStringLiteral("etapeInitiale")) &&
                   visible<QCheckBox>(a, QStringLiteral("etapeInitiale"))->isChecked(),
               "un clic sur l'étape 0 ouvre sa fiche : étape initiale");
    }
    verifier(declencher(a, QStringLiteral("Étape suivante")), "le ruban Grafcet propose « Étape suivante »");
    attendre(200);
    pe = projet(f);
    auto* rec = visible<QLineEdit>(a, QStringLiteral("receptivite"));
    verifier(pe->grafcets[0].etapes.size() == 2 && pe->grafcets[0].transitions.size() == 1 && rec, "« Étape suivante » ajoute l'étape 1 et sa transition, réceptivité prête à écrire");
    if (rec) {
      rec->setText(QStringLiteral("dcy · a"));
      emit rec->editingFinished();
    }
    attendre(300);
    const auto* lu = visible<QLabel>(a, QStringLiteral("lectureReceptivite"));
    verifier(projet(f)->grafcets[0].transitions[0].receptivite == "dcy · a" && lu && lu->text().contains(QStringLiteral("✓")),
             "la réceptivité se lit au fil de la frappe et s'enregistre");
    // Action de l'étape 1, écrite dans le panneau
    a->choisirDansGrafcet(1, -1);
    attendre(200);
    if (auto* actions = visible<QPlainTextEdit>(a, QStringLiteral("actionsEtape"))) actions->setPlainText(QStringLiteral("KM1"));
    if (auto* b = visible<QPushButton>(a, QStringLiteral("appliquerActions"))) cliquerWidget(b);
    attendre(300);
    const prom::elec::EtapeGrafcet* e1 = projet(f)->grafcets[0].etape(1);
    verifier(e1 && e1->actions.size() == 1 && e1->actions[0].sortie == "KM1", "l'action KM1 s'écrit dans le panneau de l'étape 1");
    // Retour vers l'étape 0 : le cycle se ferme, grafcet conforme
    a->choisirDansGrafcet(1, -1);
    a->bouclerGrafcet(0);
    attendre(250);
    rec = visible<QLineEdit>(a, QStringLiteral("receptivite"));
    if (rec) {
      rec->setText(QStringLiteral("b"));
      emit rec->editingFinished();
    }
    attendre(300);
    pe = projet(f);
    const auto avisG = verifierGrafcet(pe->grafcets[0]);
    std::string detailG;
    for (const auto& x : avisG) detailG += x.message + " ; ";
    verifier(pe->grafcets[0].transitions.size() == 2 &&
                 std::none_of(avisG.begin(), avisG.end(), [](const prom::elec::AvisCalcul& x) { return x.niveau != prom::elec::NiveauAvis::Info; }),
             "« Retour » vers l'étape 0 ferme le cycle : grafcet conforme", detailG);
    // Suppr sur la transition choisie : retirée, Ctrl+Z la rend
    a->choisirDansGrafcet(-1, 1);
    attendre(150);
    touche(a->vueGrafcet(), Qt::Key_Delete);
    attendre(250);
    const bool retiree = projet(f)->grafcets[0].transitions.size() == 1;
    doc->annuler();
    attendre(250);
    verifier(retiree && projet(f)->grafcets[0].transitions.size() == 2, "Suppr retire la transition choisie, Ctrl+Z la rend");
    a->choisirDansGrafcet(-1, -1);
    attendre(200);
    capturer(&f, QStringLiteral("33-grafcet-edite"));
    // Simulation : entrées cochées, l'étape 1 s'active, KM1 passe à 1
    a->simulerGrafcet(true);
    attendre(250);
    if (auto* ea = visible<QCheckBox>(a, QStringLiteral("entree-a"))) ea->setChecked(true);
    attendre(250);
    if (auto* dcy = visible<QCheckBox>(a, QStringLiteral("entree-dcy"))) dcy->setChecked(true);
    attendre(300);
    prom::elec::SimulateurGrafcet* sim = a->simulationGrafcet();
    verifier(sim && sim->actives() == std::set<int>{1} && sim->sorties().at("KM1") && visible<QGroupBox>(a, QStringLiteral("groupeSimulation")),
             "en simulation, a et dcy cochés : l'étape 1 s'active, KM1 passe à 1");
    capturer(&f, QStringLiteral("34-grafcet-simulation"));
    a->simulerGrafcet(false);
    // Exemple de cours à temporisations : le temps fait évoluer
    a->nouveauGrafcet("feux");
    a->simulerGrafcet(true);
    a->avancerSimulation(20);
    attendre(200);
    verifier(a->grafcetCourant() == 1 && a->simulationGrafcet() && a->simulationGrafcet()->active(1), "exemple des feux : après 20 s, l'orange s'allume (temporisation 20s/X0)");
    a->simulerGrafcet(false);
    // Folio Grafcet : dessiné d'office, dans le DXF
    const int fo = a->dessinerFolioGrafcet();
    attendre(300);
    pe = projet(f);
    verifier(fo >= 0 && pe->folio(pe->schema.feuilles[static_cast<std::size_t>(fo)].uuid).type == TypeFolio::Grafcet &&
                 pe->folio(pe->schema.feuilles[static_cast<std::size_t>(fo)].uuid).grafcet == pe->grafcets[1].uuid,
             "« Dessiner sur un folio » crée le folio Grafcet du grafcet choisi");
    a->choisirMode(AtelierElectrique::Schema);
    a->allerAuFolio(fo);
    attendre(400);
    capturer(&f, QStringLiteral("35-grafcet-folio"));
    const QString dxfG = QDir(dossier).filePath(QStringLiteral("grafcet.dxf"));
    QByteArray contenuG;
    if (a->exporterDxf(dxfG, fo) > 0) {
      QFile fd(dxfG);
      if (fd.open(QIODevice::ReadOnly)) contenuG = fd.readAll();
    }
    verifier(contenuG.contains("20s/X0") && contenuG.contains("\r\nTEXT\r\n"), "le folio Grafcet part en DXF, réceptivités comprises");
    if (const QByteArray cap = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !cap.isEmpty()) {
      QFile::copy(dxfG, QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("grafcet-feux.dxf")));
      a->exporterPdf(QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("grafcet-folios.pdf")));
    }
    // Texte structuré
    a->choisirMode(AtelierElectrique::Grafcet);
    attendre(200);
    const QString st = QDir(dossier).filePath(QStringLiteral("feux.st"));
    QByteArray contenuST;
    if (a->exporterTexteStructure(st)) {
      QFile fs(st);
      if (fs.open(QIODevice::ReadOnly)) contenuST = fs.readAll();
    }
    verifier(contenuST.contains("FUNCTION_BLOCK FB_Feux") && contenuST.contains("TON"), "le programme de l'automate s'écrit en texte structuré (bloc fonctionnel, temporisations)");
    // Parcours « Mon premier grafcet »
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    emit a->parcoursDemande(QStringLiteral("grafcet"));
    attendre(700);
    verifier(a->parcours() == "grafcet" && a->mode() == AtelierElectrique::Grafcet && visible<QGroupBox>(a, QStringLiteral("carteParcours")),
             "le parcours « Mon premier grafcet » s'ouvre sur l'onglet Grafcet");
    a->quitterParcours();
  }

  // ---------- 24. Appareil personnel : créé dans le panneau d'après ses bornes, posé, chiffré, modifié, repris ailleurs ----------
  {
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-schema"));
    attendre(600);
    a->choisirMode(AtelierElectrique::Schema);
    attendre(200);
    auto itemPalette = [&](const QString& genre, const QString& id) -> QTreeWidgetItem* {
      for (QTreeWidgetItemIterator it(a->palette()); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == genre && (id.isEmpty() || (*it)->data(0, Qt::UserRole + 1).toString() == id)) return *it;
      return nullptr;
    };
    QTreeWidgetItem* nouveau = itemPalette(QStringLiteral("nouvelAppareil"), QString());
    verifier(nouveau && nouveau->parent() && nouveau->parent()->text(0) == QStringLiteral("Mes appareils"), "la palette propose « Mes appareils › Nouvel appareil… »");
    if (nouveau) emit a->palette()->itemClicked(nouveau, 0);
    attendre(300);
    QGroupBox* form = visible<QGroupBox>(a, QStringLiteral("formulaireAppareil"));
    verifier(form && a->formulaireAppareilOuvert(), "« Nouvel appareil… » ouvre son formulaire dans le panneau, sans fenêtre à part");
    QPushButton* valider = form ? form->findChild<QPushButton*>(QStringLiteral("validerAppareilPerso")) : nullptr;
    verifier(valider && !valider->isEnabled(), "vierge, le formulaire retient « Créer et poser » et dit ce qui manque");
    auto saisir = [&](const char* nom, const QString& t) {
      if (!form) return;
      if (auto* e = form->findChild<QLineEdit*>(QString::fromLatin1(nom))) e->setText(t);
    };
    if (form) {
      saisir("nomAppareilPerso", QStringLiteral("Relais de sécurité"));
      saisir("marquageAppareilPerso", QStringLiteral("SÉCU"));
      saisir("bornesHaut", QStringLiteral("A1 S11 S12 13 23"));
      saisir("bornesBas", QStringLiteral("A2 A1"));
      if (auto* l = form->findChild<QComboBox*>(QStringLiteral("lettreAppareilPerso"))) l->setCurrentIndex(1);  // K — relais, automate
      attendre(100);
      auto* defauts = form->findChild<QLabel*>(QStringLiteral("defautsAppareilPerso"));
      verifier(valider && !valider->isEnabled() && defauts && defauts->text().contains(QStringLiteral("deux fois")), "une borne en double est signalée et retient le bouton");
      saisir("bornesBas", QStringLiteral("A2 S21 S22 14 24"));
      saisir("fabricantAppareilPerso", QStringLiteral("Pilz"));
      saisir("referenceAppareilPerso", QStringLiteral("PNOZ s3"));
      if (auto* sp = form->findChild<QDoubleSpinBox*>(QStringLiteral("prixAppareilPerso"))) sp->setValue(189.9);
      if (auto* sp = form->findChild<QDoubleSpinBox*>(QStringLiteral("largeurAppareilPerso"))) sp->setValue(22.5);
      if (auto* sp = form->findChild<QDoubleSpinBox*>(QStringLiteral("pertesAppareilPerso"))) sp->setValue(2.5);
      attendre(100);
      auto* apercu = form->findChild<QLabel*>(QStringLiteral("apercuAppareilPerso"));
      verifier(valider && valider->isEnabled() && apercu && !apercu->pixmap().isNull(), "nom, lettre et bornes donnés : l'aperçu du symbole suit, le bouton s'active");
      if (auto* details = form->findChild<QToolButton*>(QStringLiteral("detailsAppareilPerso"))) details->setChecked(true);
      attendre(100);
      capturer(&f, QStringLiteral("36-appareil-perso-formulaire"));
      valider->click();
      attendre(500);
    }
    pe = projet(f);
    const std::string id = "perso:Relais-de-securite";
    const DefSymbole* d = pe ? pe->schema.symbole(id) : nullptr;
    verifier(d && d->broches.size() == 10 && !a->formulaireAppareilOuvert() && a->vueSchema()->outil() == VueSchema::Outil::Placer,
             "« Créer et poser » range l'appareil dans le projet et le met en main pour la pose");
    verifier(QFile::exists(AtelierElectrique::cheminMesAppareils()) && a->mesAppareils().size() == 1 && a->mesAppareils().front().id == id,
             "l'appareil est aussi rangé dans la bibliothèque de l'utilisateur (tous ses projets)");
    verifier(itemPalette(QStringLiteral("symbole"), QString::fromStdString(id)) != nullptr, "la palette le montre sous « Mes appareils »");
    a->poser(P(60, 40), 0);
    attendre(300);
    pe = projet(f);
    const ComposantElec* k = nullptr;
    std::string poseK;
    for (const auto& c : pe->schema.composants)
      if (c.symbole == id) k = &c;
    if (k) poseK = poseDe(*pe, k->uuid, id);
    verifier(k && k->reference == "K1" && k->fabricant == "Pilz" && k->mpn == "PNOZ s3", "posé, il prend sa lettre (K1), son fabricant et sa référence", k ? k->reference : std::string());
    touche(a->vueSchema(), Qt::Key_Escape);
    attendre(150);
    capturerZoom(a->vueSchema(), P(60, 40), QStringLiteral("37-appareil-perso-pose"));
    const auto nomenclature = nomenclatureElectrique(*pe);
    verifier(std::any_of(nomenclature.begin(), nomenclature.end(), [](const LigneNomenclatureElec& l) { return l.reference == "PNOZ s3" && std::abs(l.prixUnitaire - 189.9) < 1e-6; }),
             "la nomenclature chiffrée reprend son prix");
    // Choisi sur le folio : « Modifier l'appareil » rouvre le formulaire ; les appareils posés suivent
    a->vueSchema()->selectionner({poseK});
    attendre(300);
    QPushButton* modifier = visible<QPushButton>(a, QStringLiteral("modifierAppareilPerso"));
    verifier(modifier != nullptr, "l'appareil choisi propose « Modifier l'appareil (bornes, symbole, armoire) »");
    if (modifier) {
      modifier->click();
      attendre(300);
      form = visible<QGroupBox>(a, QStringLiteral("formulaireAppareil"));
      valider = form ? form->findChild<QPushButton*>(QStringLiteral("validerAppareilPerso")) : nullptr;
      verifier(form && valider && valider->text() == QStringLiteral("Appliquer"), "le formulaire se rouvre rempli, pour appliquer");
      saisir("bornesBas", QStringLiteral("A2 S21 S22 14 24 34"));
      if (form)
        if (auto* l = form->findChild<QComboBox*>(QStringLiteral("lettreAppareilPerso"))) l->setEditText(QStringLiteral("KF"));
      attendre(100);
      if (valider) valider->click();
      attendre(500);
      pe = projet(f);
      d = pe->schema.symbole(id);
      const ComposantElec* k2 = nullptr;
      for (const auto& c : pe->schema.composants)
        if (c.symbole == id) k2 = &c;
      verifier(d && d->broches.size() == 11 && d->version == 2 && k2 && k2->reference == "KF1", "modifié : une borne de plus, nouvelle lettre ; l'appareil posé suit (KF1)",
               k2 ? k2->reference : std::string());
      doc->annuler();
      attendre(300);
      pe = projet(f);
      verifier(pe->schema.symbole(id) && pe->schema.symbole(id)->broches.size() == 10, "Ctrl+Z rend l'appareil d'avant");
    }
    // Un autre projet : l'appareil vient de la bibliothèque et se pose de même
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-schema"));
    attendre(600);
    a->choisirMode(AtelierElectrique::Schema);
    attendre(200);
    QTreeWidgetItem* mien = itemPalette(QStringLiteral("symbole"), QString::fromStdString(id));
    verifier(mien != nullptr && projet(f) && !projet(f)->schema.symbole(id), "dans un projet neuf, la palette le propose depuis la bibliothèque");
    if (mien) emit a->palette()->itemClicked(mien, 0);
    attendre(300);
    a->poser(P(40, 30), 0);
    attendre(300);
    pe = projet(f);
    int poses = 0;
    for (const auto& c : pe->schema.composants) poses += c.symbole == id ? 1 : 0;
    verifier(pe->schema.symbole(id) && poses == 1, "choisi, il entre dans le projet et s'y pose (le projet s'ouvre partout, même sans la bibliothèque)");
    touche(a->vueSchema(), Qt::Key_Escape);
    attendre(150);
  }

  // ---------- Planche de la bibliothèque (captures seulement) : chaque symbole posé et nommé, pour relire le dessin ----------
  if (const QByteArray cap = qgetenv("PROMETHEE_CAPTURES_ELECTRIQUE"); !cap.isEmpty()) {
    ProjetElectrique planche = nouveauProjetElectrique("Bibliothèque", false);
    std::size_t folioPlanche = 0, k = 0;
    for (const auto& d : bibliothequeElectrique()) {
      if (k % 40 == 0) {
        folioPlanche = k == 0 ? 0 : ajouterFolio(planche, TypeFolio::Libre, "Bibliothèque " + std::to_string(k / 40 + 1));
        if (k == 0) planche.schema.feuilles[0].nom = "Bibliothèque 1";
      }
      const double x = 14 + 19.0 * static_cast<double>(k % 8), y = 16 + 18.0 * static_cast<double>((k % 40) / 8);
      poserAppareil(planche, folioPlanche, d.id, P(x, y), 0);
      TexteSchema t;
      t.uuid = nouvelUuid();
      t.texte = d.id.substr(4);
      t.p = P(x - 4, y + 6.5);
      t.taille = 1'400'000;
      planche.schema.feuilles[folioPlanche].textes.push_back(t);
      ++k;
    }
    if (doc->modifie()) repondre(QMessageBox::DestructiveRole);
    f.choixAccueilPublic(QStringLiteral("electrique-schema"));
    attendre(500);
    doc->modifier([&](prom::Projet& pr) {
      if (!pr.electrique.empty()) pr.electrique.front() = planche;
    });
    a->ouvrir(0, AtelierElectrique::Schema, 0);
    attendre(300);
    a->exporterPdf(QDir(QString::fromLocal8Bit(cap)).filePath(QStringLiteral("bibliotheque.pdf")));
  }

  fin();
}
