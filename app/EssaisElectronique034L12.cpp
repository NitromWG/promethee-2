// Prométhée : essais d'interface de la 0.34, lot L12 (travail à plusieurs) — onglets des outils de conception :
// liens entre disciplines et boîtier marqué « à revoir » dans l'arbre jusqu'à l'accusé de lecture (12.20) ; licence
// du projet (12.11) ; revue formelle ouverte puis défaite d'un Ctrl+Z (12.13) ; nomenclature du système (12.18) ;
// Git (12.16). Appelés seuls avec PROMETHEE_ESSAIS_LOTS=034L12. Licence GPL-3.0-only.
#include <functional>
#include <string>

#include <QApplication>
#include <QDockWidget>
#include <QEventLoop>
#include <QTimer>
#include <QTreeWidgetItemIterator>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetreOutilsConception.hpp"
#include "FenetrePrincipale.hpp"
#include "Navigateur.hpp"
#include "promethee/electronique.hpp"
#include "promethee/modele.hpp"
#include "promethee/projet_partage.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
QTreeWidgetItem* item(Navigateur* n, const QString& cle) {
  if (!n) return nullptr;
  for (QTreeWidgetItemIterator it(n); *it; ++it)
    if ((*it)->data(0, Navigateur::ROLE_CLE).toString() == cle) return *it;
  return nullptr;
}
}  // namespace

void essaisElectronique034L12(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  // Un projet avec sa carte mécanique et son boîtier, et la conception électronique de la carte
  prom::Projet p = prom::projetVide();
  p.nom = "Boîtier et carte";
  ConceptionElec c = exempleElectronique();
  c.circuit.contour.libre.clear();
  c.circuit.contour.largeur = nm(50);
  c.circuit.contour.hauteur = nm(40);
  p.electronique.push_back(c);
  doc->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Circuit);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) {
    verifier(false, "034 L12 : la conception s'ouvre", {});
    return;
  }
  for (const auto x : {AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  a->executerAction("outils.conception");
  attendre(300);
  FenetreOutilsConception* o = a->outilsConception();
  if (!o) {
    verifier(false, "034 L12 : la fenêtre des outils de conception s'ouvre", {});
    return;
  }

  // ---------- 12.20 · Contour agrandi de 2 mm : boîtier « à revoir » avec ce seul changement, jusqu'à l'accusé ----------
  {
    const std::string uuid = doc->projet().electronique[0].uuid;
    doc->modifier([&](prom::Projet& x) { x.liensDisciplines.push_back(prom::lierDisciplines(x, "carte:" + uuid, "boitier", "contour", "Lou", "2026-10-25")); },
                  QStringLiteral("Carte liée au boîtier"));
    attendre(200);
    Navigateur* nav = f.findChild<Navigateur*>();
    QTreeWidgetItem* b0 = item(nav, QStringLiteral("boitier"));
    const bool aJour = b0 && b0->data(0, Navigateur::ROLE_DETAIL).toString() == QStringLiteral("lié à la carte");
    a->modifierConception([](ConceptionElec& x) { x.circuit.contour.largeur = nm(52); }, QStringLiteral("Contour agrandi"));
    attendre(400);
    o->refaire();
    QTreeWidgetItem* b1 = item(nav, QStringLiteral("boitier"));
    const bool marque = b1 && b1->data(0, Navigateur::ROLE_DETAIL).toString() == QStringLiteral("à revoir") && b1->toolTip(0).contains(QStringLiteral("contour.largeur : 50 → 52 mm"));
    const QString rapport = o->rapport(QStringLiteral("liens"));
    const bool seul = rapport.contains(QStringLiteral("à revoir")) && rapport.contains(QStringLiteral("contour.largeur : 50 → 52 mm")) && rapport.count(QStringLiteral(" → ")) == 1;
    // Accusé de lecture, en un pas d'annulation
    doc->modifier([](prom::Projet& x) {
      for (auto& l : x.liensDisciplines) prom::accuserLecture(x, l.id, "Évan", "2026-10-26");
    }, QStringLiteral("Changements lus"));
    attendre(300);
    o->refaire();
    QTreeWidgetItem* b2 = item(nav, QStringLiteral("boitier"));
    const bool lu = b2 && b2->data(0, Navigateur::ROLE_DETAIL).toString() == QStringLiteral("lié à la carte") && o->rapport(QStringLiteral("liens")).contains(QStringLiteral("à jour"));
    verifier(aJour && marque && seul && lu, "12.20 : contour de la carte agrandi de 2 mm : boîtier marqué « à revoir » dans l'arbre avec ce seul changement, jusqu'à l'accusé de lecture",
             std::string(aJour ? "à jour au départ" : "pas à jour au départ") + (marque ? ", marqué « à revoir »" : ", pas marqué") + (seul ? ", un seul changement" : ", changements ?") + (lu ? ", lu" : ", accusé ?"));
  }

  // ---------- 12.11 · Licence du projet déclarée ----------
  {
    doc->modifier([](prom::Projet& x) { x.licence = "CERN-OHL-S-2.0"; }, QStringLiteral("Licence du projet"));
    o->refaire();
    const bool montree = o->montrer(QStringLiteral("partage")) && o->rapport(QStringLiteral("partage")).contains(QStringLiteral("Strongly Reciprocal"));
    verifier(montree, "12.11 : la licence déclarée du projet est montrée avec les trois sorties de partage", o->rapport(QStringLiteral("partage")).left(160).toStdString());
  }

  // ---------- 12.13 · Revue formelle ouverte, puis défaite d'un seul Ctrl+Z ----------
  {
    const int pas = doc->positionHistorique();
    a->modifierConception([](ConceptionElec& x) { prom::ouvrirRevue(x, "Revue de la révision A", "2026-10-25"); }, QStringLiteral("Revue formelle ouverte"));
    o->refaire();
    const bool ouverte = o->montrer(QStringLiteral("revueFormelle")) && o->rapport(QStringLiteral("revueFormelle")).contains(QStringLiteral("Revue n° 1")) && doc->positionHistorique() == pas + 1;
    doc->annuler();
    attendre(150);
    o->refaire();
    const bool defaite = !o->rapport(QStringLiteral("revueFormelle")).contains(QStringLiteral("Revue n° 1"));
    verifier(ouverte && defaite, "12.13 : une revue formelle s'ouvre en un pas, avec l'empreinte SHA-256 de la révision, et se défait d'un Ctrl+Z",
             o->rapport(QStringLiteral("revueFormelle")).left(160).toStdString());
  }

  // ---------- 12.18, 12.16 · Nomenclature du système et Git ----------
  {
    const bool nomenclature = o->montrer(QStringLiteral("nomenclatureSysteme")) && o->rapport(QStringLiteral("nomenclatureSysteme")).contains(QStringLiteral("Par système"));
    const QString git = o->rapport(QStringLiteral("git"));
    const bool gitDit = git.contains(QStringLiteral("git")) || git.contains(QStringLiteral("Enregistre d’abord"));
    verifier(nomenclature && gitDit, "12.16, 12.18 : nomenclature du système en colonnes par carte ; état Git du projet, réseau coupé par défaut", (git.left(120)).toStdString());
  }
  o->close();
}
