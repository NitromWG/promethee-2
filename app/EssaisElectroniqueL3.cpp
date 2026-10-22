// Prométhée : essais d'interface du lot L3 de l'audit du 15 octobre 2026 (« Vérité du composant »), sur l'exemple
// livré, par de vrais gestes quand le geste est le sujet : hauteur saisie qui arrive en 3D et au boîtier, empreinte
// changée qui se remplace en place sur le circuit sans F8, huit condensateurs à « 100n » en un pas, sélection
// croisée, composant routé supprimé qui laisse un écart, réseau renommé au circuit, valeur calculée, remplacement
// d'un composant, carte sans liens appariée, tableau des champs ancré, schéma et circuit côte à côte, tableau des
// objets. Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L3.
// Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include "AtelierElectronique.hpp"
#include "DialogueAppariement.hpp"
#include "DialogueRemplacer.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetrePrincipale.hpp"
#include "InspecteurElectronique.hpp"
#include "MosaiqueVues.hpp"
#include "Navigateur.hpp"
#include "PanneauEcarts.hpp"
#include "SelectionElectronique.hpp"
#include "TableauChamps.hpp"
#include "TableauObjets.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/modele.hpp"
#include "promethee/objets.hpp"
#include "promethee/remplacement.hpp"
#include "promethee/synchronisation.hpp"
#include "promethee/valeurs.hpp"

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
// Le symbole posé d'un composant : sa feuille et son centre
struct Pose {
  int feuille = -1;
  std::string uuid;
  Pt centre;
};
Pose poseDe(const ConceptionElec& c, const std::string& reference) {
  const ComposantElec* k = c.parReference(reference);
  if (!k) return {};
  for (std::size_t f = 0; f < c.feuilles.size(); ++f)
    for (const auto& s : c.feuilles[f].symboles)
      if (s.composant == k->uuid)
        if (const DefSymbole* d = c.symbole(s.symbole)) return {static_cast<int>(f), s.uuid, boiteSymbole(s, *d).centre()};
  return {};
}
const EmpreintePlacee* empreinteDe(const ConceptionElec& c, const std::string& reference) {
  const ComposantElec* k = c.parReference(reference);
  if (!k) return nullptr;
  for (const auto& e : c.circuit.empreintes)
    if (e.composant == k->uuid) return &e;
  return nullptr;
}
// Un clic sur le symbole du composant, la vue centrée sur lui
bool cliquerSymbole(AtelierElectronique* a, Document* doc, const std::string& reference) {
  const Pose p = poseDe(*conception(doc), reference);
  if (p.feuille < 0) return false;
  VueSchema* vs = a->vueSchema();
  if (vs->feuille() != p.feuille) vs->setConception(a->conception(), p.feuille);
  vs->centrerSur(p.centre);
  attendre(80);
  reel::cliquer(vs, vs->versEcran(p.centre).toPoint());
  attendre(150);
  return vs->selection().count(p.uuid) > 0;
}
// Saisie dans un champ comme l'utilisateur : clic, tout choisir, frappe, Entrée
void saisir(QWidget* champ, const QString& texte) {
  // Le champ amené à l'écran, sous le pointeur : le panneau a pu se remettre en page (une section refaite)
  for (int essai = 0; essai < 6; ++essai) {
    for (QWidget* x = champ->parentWidget(); x; x = x->parentWidget())
      if (auto* zone = qobject_cast<QScrollArea*>(x)) {
        zone->ensureWidgetVisible(champ);
        break;
      }
    attendre(60);
    const QPoint ici = champ->mapTo(champ->window(), champ->rect().center());
    QWidget* dessus = champ->window()->childAt(ici);
    if (dessus && (dessus == champ || champ->isAncestorOf(dessus))) break;
  }
  if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL")) {
    const QPoint ici = champ->mapTo(champ->window(), champ->rect().center());
    QWidget* dessus = champ->window()->childAt(ici);
    std::printf("  (champ %s en %d,%d de la fenêtre %dx%d ; dessus : %s %s)\n", champ->objectName().toStdString().c_str(), ici.x(), ici.y(), champ->window()->width(),
                champ->window()->height(), dessus ? dessus->metaObject()->className() : "-", dessus ? dessus->objectName().toStdString().c_str() : "");
    for (QWidget* x = champ; x; x = x->parentWidget())
      if (x->objectName() == QStringLiteral("inspecteurElectronique") || x->inherits("PageInspecteur")) {
        for (QWidget* y : x->findChildren<QWidget*>())
          if (y->isVisible() && y->minimumSizeHint().width() > 280)
            std::printf("    large : %s %s %d (%s)\n", y->metaObject()->className(), y->objectName().toStdString().c_str(), y->minimumSizeHint().width(),
                        qobject_cast<QAbstractButton*>(y) ? qobject_cast<QAbstractButton*>(y)->text().toStdString().c_str() : qobject_cast<QLabel*>(y) ? qobject_cast<QLabel*>(y)->text().left(60).toStdString().c_str() : "");
        break;
      }
    if (const QByteArray chemin = qgetenv("PROMETHEE_CAPTURE_L3"); !chemin.isEmpty()) champ->window()->grab().save(QString::fromLocal8Bit(chemin) + champ->objectName().replace(QLatin1Char(':'), QLatin1Char('_')) + QStringLiteral(".png"));
  }
  reel::cliquer(champ, champ->rect().center());
  if (auto* l = qobject_cast<QLineEdit*>(champ)) l->selectAll();
  if (auto* b = qobject_cast<QAbstractSpinBox*>(champ)) b->selectAll();
  reel::taper(champ, texte);
  if (qEnvironmentVariableIsSet("PROMETHEE_ESSAIS_DETAIL")) {
    QWidget* f = QApplication::focusWidget();
    auto* l = qobject_cast<QLineEdit*>(champ);
    std::printf("  (saisie « %s » dans %s : focus %s, texte « %s », lecture seule %d, actif %d)\n", texte.toStdString().c_str(), champ->objectName().toStdString().c_str(),
                f ? f->objectName().toStdString().c_str() : "-", l ? l->text().toStdString().c_str() : "?", l ? l->isReadOnly() : -1, champ->isEnabled());
  }
  reel::touche(champ, Qt::Key_Return);
  attendre(150);
}
double ohms(const std::string& v) { return lireValeurComposant(v, FamilleValeur::Resistance).nombre; }
}  // namespace

void essaisElectroniqueL3(FenetrePrincipale& f, const Verifier& verifier0) {
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
    verifier(false, "L3 : l'exemple livré s'ouvre pour les essais du lot", {});
    return;
  }
  verifier(reel::activer(&f), "L3 : la fenêtre est active", {});
  a->ouvrir(0, AtelierElectronique::Schema, 0);
  attendre(300);
  VueSchema* vs = a->vueSchema();
  VueCircuit* vc = a->vueCircuit();
  InspecteurElectronique* ins = a->inspecteur();
  // L'inspecteur montré et devant (un lot précédent a pu le laisser fermé, ou derrière « Apprendre »)
  for (const auto p : {AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::Ecarts})
    if (QDockWidget* d = a->panneau(p)) d->hide();
  if (QDockWidget* d = a->panneau(AtelierElectronique::Panneau::Inspecteur)) {
    d->setFloating(false);  // un lot précédent a pu le détacher (disposition « Routage »)
    d->show();
    d->raise();
  }
  attendre(100);

  // ---------- 3.4 · 12 mm saisis : corps 3D et boîtier à 12 mm, provenance « saisie » (MEC-02, P4, P5, T2) ----------
  {
    // Un inspecteur étroit, comme sur un petit écran : chaque champ doit y rester atteignable
    if (QDockWidget* d = a->panneau(AtelierElectronique::Panneau::Inspecteur))
      if (auto* fenetre = qobject_cast<QMainWindow*>(d->parentWidget())) {
        fenetre->resizeDocks({d}, {260}, Qt::Horizontal);
        attendre(100);
      }
    const bool choisi = cliquerSymbole(a, doc, "R1");
    auto* h = ins->page() ? ins->page()->findChild<QDoubleSpinBox*>(QStringLiteral("hauteurComposant")) : nullptr;
    if (h) {
      ins->ensureWidgetVisible(h);
      attendre(50);
      saisir(h, QStringLiteral("12"));
      if (auto* confirmer = ins->page()->findChild<QPushButton*>(QStringLiteral("confirmerHauteur")); confirmer && confirmer->isVisible()) {
        reel::cliquer(confirmer, confirmer->rect().center());
        attendre(150);
      }
    }
    const ConceptionElec* c = conception(doc);
    const ComposantElec* r1 = c->parReference("R1");
    const EmpreintePlacee* e = empreinteDe(*c, "R1");
    const auto corps = e ? corpsPose(*c, *e) : std::nullopt;
    prom::Projet p = doc->projet();
    appliquerCarteElectronique(p, 0);
    double boitier = 0;
    std::string source;
    for (const auto& k : p.composants)
      if (k.ref == "R1") {
        boitier = k.h;
        source = k.sourceHauteur;
      }
    auto* provenance = ins->page() ? ins->page()->findChild<QLabel*>(QStringLiteral("provenanceHauteur")) : nullptr;
    verifier(choisi && h && r1 && r1->hauteur == 12 && r1->provenanceHauteur == ProvenanceHauteur::Saisie && corps && std::abs(corps->hauteur.mm - 12) < 1e-6 &&
                 std::abs(boitier - 12) < 1e-6 && source == "saisie" && provenance && provenance->text().contains(QStringLiteral("saisie")),
             "3.4 : 12 mm saisis dans l'inspecteur donnent un corps de 12 mm en 3D et dans le boîtier ; la provenance montrée est « saisie »",
             "corps " + std::to_string(corps ? corps->hauteur.mm : -1.0) + " mm, boîtier " + std::to_string(boitier) + " mm (" + source + ")" + (choisi ? "" : ", R1 pas choisi") +
                 (h ? h->isVisible() ? "" : ", champ caché" : ", pas de champ") + ", page " + ins->clePage().toStdString());
    reel::touche(vs, Qt::Key_Escape);
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(200);
  }

  // ---------- 3.7 · L'empreinte de R3 changée dans l'inspecteur se remplace en place, sans F8 (P6) ----------
  {
    const ConceptionElec* c0 = conception(doc);
    const EmpreintePlacee* avant = empreinteDe(*c0, "R3");
    const EmpreintePlacee r3avant = avant ? *avant : EmpreintePlacee{};
    const Pt point = avant ? versCarte(r3avant, pointDePlacement(*c0->empreinte(r3avant.empreinte))) : Pt{};
    const int pas = doc->positionHistorique();
    const bool choisi = cliquerSymbole(a, doc, "R3");
    auto* emp = ins->page() ? ins->page()->findChild<QComboBox*>(QStringLiteral("empreinteComposant")) : nullptr;
    const int i = emp ? emp->findData(QStringLiteral("R_0805")) : -1;
    if (emp && i >= 0) {
      emp->setCurrentIndex(i);
      emit emp->activated(i);
      attendre(200);
    }
    const ConceptionElec* c = conception(doc);
    const EmpreintePlacee* r3 = empreinteDe(*c, "R3");
    bool accroche = r3 != nullptr;
    if (r3)
      for (const auto& pa : c->empreinte(r3->empreinte)->pastilles) accroche &= cuivreSurPastille(*c, *r3, pa).nombre() > 0;
    const Pt apres = r3 && c->empreinte(r3->empreinte) ? versCarte(*r3, pointDePlacement(*c->empreinte(r3->empreinte))) : Pt{};
    verifier(choisi && i >= 0 && r3 && r3->empreinte == "R_0805" && r3->rotation == r3avant.rotation && std::abs(apres.x - point.x) <= 1 && std::abs(apres.y - point.y) <= 1 &&
                 accroche && doc->positionHistorique() == pas + 1,
             "3.7 : R3 passé en 0805 depuis l'inspecteur : même place, même rotation, pistes accrochées, en un pas et sans F8",
             r3 ? r3->empreinte : std::string("—"));
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(200);
    verifier(empreinteDe(*conception(doc), "R3") && empreinteDe(*conception(doc), "R3")->empreinte == r3avant.empreinte,
             "3.7 : Ctrl+Z rend l'empreinte d'avant au schéma et au circuit", {});
  }

  // ---------- 3.10 · Valeur modifiée depuis le circuit (P12, P13) ----------
  {
    a->setMode(AtelierElectronique::Circuit);
    attendre(300);
    const ConceptionElec* c = conception(doc);
    const EmpreintePlacee* r3 = empreinteDe(*c, "R3");
    const bool r3pose = r3 != nullptr;
    const Pt posR3 = r3 ? r3->position : Pt{};
    const std::string id = c->parReference("R3") ? c->parReference("R3")->uuid : std::string();
    if (r3pose) {
      vc->centrerSur(posR3);
      attendre(80);
      reel::cliquer(vc, vc->versEcran(posR3).toPoint());
      attendre(200);
    }
    const bool page = ins->clePage() == QStringLiteral("circuit.empreinte");
    auto* valeur = ins->page() ? ins->page()->findChild<QLineEdit*>(QStringLiteral("champ:valeur")) : nullptr;
    if (valeur) {
      ins->ensureWidgetVisible(valeur);
      attendre(50);
      saisir(valeur, QStringLiteral("470"));
    }
    verifier(page && valeur && conception(doc)->composant(id) && conception(doc)->composant(id)->valeur == "470",
             "3.10 : R3 choisi au circuit montre la page du composant ; sa valeur s'y modifie",
             ins->clePage().toStdString() + (valeur ? valeur->isVisible() ? ", champ visible" : ", champ caché" : ", pas de champ") + ", valeur " +
                 (conception(doc)->composant(id) ? conception(doc)->composant(id)->valeur : std::string("?")));

    // P22 : la case « Le boîtier du projet suit cette carte » lie sans quitter l'atelier
    {
      vc->setFocus(Qt::OtherFocusReason);
      reel::touche(vc, Qt::Key_Escape);
      attendre(150);
      auto* suit = ins->page() ? ins->page()->findChild<QCheckBox*>(QStringLiteral("boitierSuitCarte")) : nullptr;
      const bool avant = conception(doc)->pilotageBoitier;
      if (suit && !suit->isChecked()) {
        ins->ensureWidgetVisible(suit);
        attendre(50);
        reel::cliquer(suit, QPoint(8, suit->height() / 2));
        attendre(250);
      }
      verifier(suit && !avant && conception(doc)->pilotageBoitier && a->isVisible() && a->mode() == AtelierElectronique::Circuit,
               "P22 : cocher « Le boîtier du projet suit cette carte » lie le boîtier sans quitter l'atelier", ins->clePage().toStdString());
      vc->setFocus(Qt::OtherFocusReason);
      reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
      attendre(200);
      if (r3pose) {
        reel::cliquer(vc, vc->versEcran(posR3).toPoint());
        attendre(200);
      }
    }

    // 3.11 · Sélection croisée : R3 choisi au circuit l'est au schéma et dans l'arbre (C5, PLA-03, G5)
    const Pose pose = poseDe(*conception(doc), "R3");
    auto* nav = f.findChild<Navigateur*>();
    bool arbre = false;
    if (nav)
      for (QTreeWidgetItem* it : nav->selectedItems()) arbre |= it->text(0).startsWith(QStringLiteral("R3"));
    verifier(a->selection()->composantChoisi(id) && vs->selection().count(pose.uuid) && arbre,
             "3.11 : R3 choisi au circuit est choisi au schéma et dans l'arbre du projet",
             std::string(a->selection()->composantChoisi(id) ? "choisi" : "pas choisi") + (vs->selection().count(pose.uuid) ? ", au schéma" : ", pas au schéma") +
                 (arbre ? ", dans l'arbre" : ", pas dans l'arbre"));
    vc->setFocus(Qt::OtherFocusReason);
    reel::touche(vc, Qt::Key_Escape);
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    attendre(200);
    a->setMode(AtelierElectronique::Schema);
    attendre(200);
  }

  // ---------- 3.9 · Huit condensateurs choisis, « 100n » tapé une fois : un seul pas (SAI-15, P11) ----------
  {
    a->modifierConception([](ConceptionElec& c) {
      Feuille fe;
      fe.uuid = nouvelUuid();
      fe.nom = "Essais L3";
      fe.format = "A3";
      c.feuilles.push_back(fe);
      const std::size_t fi = c.feuilles.size() - 1;
      for (int i = 0; i < 8; ++i) {
        const std::string s = poserSymbole(c, fi, "C", ptMm(50.8 + 12.7 * i, 50.8));
        for (const auto& x : c.feuilles[fi].symboles)
          if (x.uuid == s)
            if (ComposantElec* k = c.composant(x.composant)) {
              k->reference = "C" + std::to_string(20 + i);
              k->valeur = "10n";
            }
      }
    }, QStringLiteral("Huit condensateurs d’essai"));
    attendre(300);
    const ConceptionElec* c = conception(doc);
    const int fi = static_cast<int>(c->feuilles.size()) - 1;
    std::vector<SelectionElectronique::Ref> refs;
    std::vector<std::string> ids;
    for (const auto& s : c->feuilles.back().symboles) {
      refs.push_back({SelectionElectronique::Genre::Symbole, s.uuid, s.composant});
      ids.push_back(s.composant);
    }
    a->ouvrir(0, AtelierElectronique::Schema, fi);
    attendre(200);
    a->selection()->choisir(refs);
    attendre(250);
    const bool page = ins->clePage() == QStringLiteral("schema.plusieurs");
    auto* champ = ins->page() ? ins->page()->findChild<QLineEdit*>(QStringLiteral("valeurPlusieurs")) : nullptr;
    const int pas = doc->positionHistorique();
    if (champ) {
      ins->ensureWidgetVisible(champ);
      attendre(50);
      saisir(champ, QStringLiteral("100n"));
    }
    int ok = 0;
    for (const auto& id : ids)
      if (const ComposantElec* k = conception(doc)->composant(id); k && k->valeur == "100n") ++ok;
    verifier(refs.size() == 8 && page && champ && ok == 8 && doc->positionHistorique() == pas + 1,
             "3.9 : huit condensateurs choisis, « 100n » tapé une fois : les huit changent, en un seul pas d'annulation",
             std::to_string(ok) + " sur 8, " + std::to_string(refs.size()) + " choisis, page " + ins->clePage().toStdString() + (champ ? "" : ", pas de champ") + ", " +
                 std::to_string(doc->positionHistorique() - pas) + " pas");
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(200);
    int retour = 0;
    for (const auto& id : ids)
      if (const ComposantElec* k = conception(doc)->composant(id); k && k->valeur == "10n") ++retour;
    verifier(retour == 8, "3.9 : un seul Ctrl+Z rend les huit valeurs", std::to_string(retour) + " sur 8");
    reel::touche(vs, Qt::Key_Escape);
  }

  // ---------- 3.14 · R2 = 2·R1, puis R1 passe à 10k (SAI-44) ----------
  {
    const bool choisi = cliquerSymbole(a, doc, "R2");
    // ƒx ouvre la ligne de la formule
    if (auto* fx = ins->page() ? ins->page()->findChild<QToolButton*>(QStringLiteral("ouvrirFormule")) : nullptr) {
      ins->ensureWidgetVisible(fx);
      reel::cliquer(fx, fx->rect().center());
      attendre(100);
    }
    auto* formule = ins->page() ? ins->page()->findChild<QLineEdit*>(QStringLiteral("formuleValeur")) : nullptr;
    if (formule) {
      ins->ensureWidgetVisible(formule);
      saisir(formule, QStringLiteral("2*R1"));
    }
    const double r2 = conception(doc)->parReference("R2") ? ohms(conception(doc)->parReference("R2")->valeur) : 0;
    const double r1 = conception(doc)->parReference("R1") ? ohms(conception(doc)->parReference("R1")->valeur) : 0;
    const std::string id1 = conception(doc)->parReference("R1") ? conception(doc)->parReference("R1")->uuid : std::string();
    a->modifierConception([&](ConceptionElec& c) {
      if (ComposantElec* k = c.composant(id1)) k->valeur = "10k";
    }, QStringLiteral("R1 à 10k"));
    attendre(200);
    const double r2b = conception(doc)->parReference("R2") ? ohms(conception(doc)->parReference("R2")->valeur) : 0;
    verifier(choisi && formule && std::abs(r2 - 2 * r1) < 1e-6 && std::abs(r2b - 20000) < 1e-6,
             "3.14 : R2 = 2·R1 vaut le double de R1, puis 20 kΩ quand R1 passe à 10k", conception(doc)->parReference("R2") ? conception(doc)->parReference("R2")->valeur : std::string());
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Escape);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(150);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(200);
  }

  // ---------- 3.6, 3.8 · R3 routé supprimé au schéma : un écart visible, aucune piste orpheline en silence (A4, S18) ----------
  {
    const std::size_t pistes = conception(doc)->circuit.pistes.size();
    const std::string idR3 = conception(doc)->parReference("R3") ? conception(doc)->parReference("R3")->uuid : std::string();
    const bool choisi = cliquerSymbole(a, doc, "R3");
    reel::touche(vs, Qt::Key_Delete);
    attendre(300);
    a->ecarts()->maj();
    bool ecart = false;
    for (const auto& e : a->ecarts()->ecarts()) ecart |= e.genre == GenreEcart::SupprimeRoute && e.reference == "R3";
    bool empreinte = false;
    for (const auto& e : conception(doc)->circuit.empreintes) empreinte |= e.composant == idR3;
    QToolButton* pastille = a->ecarts()->pastille();
    const Pose apres = poseDe(*conception(doc), "R3");
    verifier(choisi && apres.feuille < 0 && ecart && empreinte && conception(doc)->circuit.pistes.size() == pistes && pastille && pastille->isVisible() &&
                 pastille->text().contains(QStringLiteral("écart")),
             "3.6, 3.8 : R3 routé supprimé au schéma laisse un écart en attente, visible dans la barre d'état ; son empreinte et ses pistes restent",
             pastille ? pastille->text().toStdString() : std::string());
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(300);
  }

  // ---------- 3.18 · Réseau renommé au circuit : l'étiquette du schéma change dans le même pas (PLA-16) ----------
  {
    a->setMode(AtelierElectronique::Circuit);
    attendre(300);
    const ConceptionElec* c = conception(doc);
    std::string piste;
    for (const auto& t : c->circuit.pistes)
      if (t.reseau == "DECH" && piste.empty()) piste = t.uuid;
    a->selection()->choisir({{SelectionElectronique::Genre::Piste, piste, {}}});
    attendre(250);
    auto* nom = ins->page() ? ins->page()->findChild<QLineEdit*>(QStringLiteral("renommerReseauCircuit")) : nullptr;
    const int pas = doc->positionHistorique();
    if (nom) saisir(nom, QStringLiteral("DECHARGE"));
    int etiquettes = 0, anciennes = 0;
    for (const auto& fe : conception(doc)->feuilles)
      for (const auto& e : fe.etiquettes) {
        etiquettes += e.texte == "DECHARGE";
        anciennes += e.texte == "DECH";
      }
    bool cuivre = false;
    for (const auto& t : conception(doc)->circuit.pistes) cuivre |= t.uuid == piste && t.reseau == "DECHARGE";
    verifier(!piste.empty() && nom && etiquettes == 2 && anciennes == 0 && cuivre && doc->positionHistorique() == pas + 1,
             "3.18 : le réseau DECH renommé au circuit renomme ses étiquettes au schéma et son cuivre, en un seul pas d'annulation",
             std::to_string(etiquettes) + " étiquette(s)");
    vc->setFocus(Qt::OtherFocusReason);
    reel::touche(vc, Qt::Key_Escape);
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    attendre(250);
    int retour = 0;
    for (const auto& e : conception(doc)->feuilles[0].etiquettes) retour += e.texte == "DECH";
    verifier(retour == 2, "3.18 : un Ctrl+Z rend le nom au circuit et au schéma", {});
    a->setMode(AtelierElectronique::Schema);
    attendre(200);
  }

  // ---------- 3.16 · LM358 remplacé par un TL072, broches par nom : fils accrochés, empreinte changée (SAI-42) ----------
  {
    std::string idU;
    std::map<std::string, std::string> etiquetteParNumero;
    a->modifierConception([&](ConceptionElec& c) {
      const std::size_t fi = c.feuilles.size() - 1;
      const std::string s = poserSymbole(c, fi, "LM358", ptMm(101.6, 127));
      for (auto& x : c.feuilles[fi].symboles)
        if (x.uuid == s) idU = x.composant;
      if (ComposantElec* k = c.composant(idU)) k->reference = "U9";
      for (const auto& sp : c.feuilles[fi].symboles)
        if (sp.uuid == s)
          for (const auto& b : brochesPlacees(sp, *c.symbole("LM358"))) {
            const Pt dir = b.direction == 0 ? Pt{-1, 0} : b.direction == 2 ? Pt{1, 0} : b.direction == 1 ? Pt{0, -1} : Pt{0, 1};
            const Pt bout{b.p.x + dir.x * 5'080'000, b.p.y + dir.y * 5'080'000};
            c.feuilles[fi].fils.push_back({nouvelUuid(), b.p, bout});
            c.feuilles[fi].etiquettes.push_back({nouvelUuid(), "U9_" + b.broche->numero, bout, 0, TypeEtiquette::Locale});
            etiquetteParNumero[b.broche->numero] = "U9_" + b.broche->numero;
          }
      SpecSymbole sp;
      sp.id = "perso:TL072";
      sp.nom = "TL072";
      sp.prefixe = "U";
      sp.empreinte = "DIP-8";
      sp.broches = lireListeBroches("8 V+ alim\n4 V- alim\n3 +INA in\n2 -INA in\n1 OUTA out\n5 +INB in\n6 -INB in\n7 OUTB out");
      c.symboles[sp.id] = genererSymbole(sp);
    }, QStringLiteral("LM358 d’essai"));
    attendre(300);
    const std::string avant = conception(doc)->composant(idU) ? conception(doc)->composant(idU)->empreinte : std::string();
    const int pas = doc->positionHistorique();
    DialogueRemplacer d(*a, {idU}, a);
    d.choisirArticle("perso:TL072");
    const int lignes = d.apercu()->topLevelItemCount();
    const int n = d.appliquer();
    attendre(300);
    const ConceptionElec* c = conception(doc);
    const Connectivite k = connectivite(*c);
    int gardees = 0;
    for (const auto& [num, nom] : etiquetteParNumero) gardees += k.nom(idU, num) == nom;
    std::string empreinteCircuit;
    for (const auto& e : c->circuit.empreintes)
      if (e.composant == idU) empreinteCircuit = e.empreinte;
    verifier(etiquetteParNumero.size() == 8 && lignes == 8 && n == 1 && gardees == 8 && c->composant(idU) && c->composant(idU)->symbole == "perso:TL072" &&
                 empreinteCircuit == "DIP-8" && avant != "DIP-8" && doc->positionHistorique() == pas + 1,
             "3.16 : LM358 remplacé par un TL072 (broches par nom) : les huit fils restent accrochés, l'empreinte change sur le circuit, en un pas",
             std::to_string(gardees) + " broche(s) gardée(s), empreinte « " + empreinteCircuit + " »");
  }

  // ---------- 3.9, 3.15 · Tableau des champs : panneau ancré, écrit aussitôt, regroupé, vues, portée (SAI-16, B13) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema, 0);
    attendre(200);
    a->ouvrirTableauChamps();
    attendre(300);
    QDockWidget* dock = a->panneau(AtelierElectronique::Panneau::Champs);
    TableauChamps* t = a->tableauChamps();
    const bool ancre = dock && dock->isVisible() && !dock->isFloating() && t && t->window() == f.window();
    const int pas = doc->positionHistorique();
    const bool ecrit = t && t->ecrire("R1", QStringLiteral("Fabricant"), QStringLiteral("Vishay"));
    attendre(200);
    const bool aussitot = conception(doc)->parReference("R1") && conception(doc)->parReference("R1")->fabricant == "Vishay" && doc->positionHistorique() == pas + 1;
    // Un bloc collé (deux lignes, deux colonnes) : un seul pas
    const int l1 = t ? t->ligne("R1") : -1;
    const int col = t ? t->colonne(QStringLiteral("Réf. fabricant")) : -1;
    if (t && l1 >= 0 && col >= 0) {
      t->table()->sortItems(0);
      t->collerTexte(t->ligne("R1"), col, QStringLiteral("CRCW06031K00\tMouser\nCRCW060347K0\tFarnell\n"));
    }
    attendre(200);
    const bool bloc = conception(doc)->parReference("R1") && conception(doc)->parReference("R1")->mpn == "CRCW06031K00" && conception(doc)->parReference("R1")->fournisseur == "Mouser" &&
                      doc->positionHistorique() == pas + 2;
    verifier(ancre && ecrit && aussitot && bloc, "3.9 : le tableau des champs est un panneau ancré sous le canevas ; une cellule, puis un bloc collé, s'écrivent aussitôt, chacun en un pas",
             std::to_string(doc->positionHistorique() - pas) + " pas");
    // Regroupé par valeur : moins de lignes que de composants ; la vue « Approvisionnement » cache l'empreinte
    int lignesSeules = t ? t->table()->rowCount() : 0;
    if (t) t->setRegroupe(true);
    attendre(100);
    const int lignesGroupees = t ? t->table()->rowCount() : 0;
    if (t) t->choisirVue(QStringLiteral("Approvisionnement"));
    const bool vue = t && t->table()->isColumnHidden(t->colonne(QStringLiteral("Empreinte"))) && !t->table()->isColumnHidden(t->colonne(QStringLiteral("Fabricant")));
    verifier(t && lignesGroupees > 0 && lignesGroupees < lignesSeules && vue, "3.15 : regroupé par valeur, une ligne par groupe ; la vue « Approvisionnement » ne montre que ses colonnes",
             std::to_string(lignesSeules) + " → " + std::to_string(lignesGroupees) + " lignes");
    if (t) {
      t->setRegroupe(false);
      t->choisirVue(QStringLiteral("Toutes les colonnes"));
    }
    // Portée : la sélection seule
    cliquerSymbole(a, doc, "R2");
    if (t) t->setPortee(TableauChamps::Portee::Selection);
    attendre(200);
    verifier(t && t->table()->rowCount() == 1 && t->ligne("R2") == 0, "3.15 : la portée « La sélection » ne montre que le composant choisi", {});
    if (t) t->setPortee(TableauChamps::Portee::Conception);
    if (dock) dock->hide();
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Escape);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(150);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(200);
  }

  // ---------- 3.21 · Schéma et circuit côte à côte : R3 choisi à gauche est choisi et cadré à droite (ERG-40) ----------
  {
    a->ouvrir(0, AtelierElectronique::Schema, 0);
    attendre(200);
    a->setMosaique(true);
    attendre(400);
    const bool coteACote = vs->isVisible() && vc->isVisible() && vs->mapTo(a, QPoint(0, 0)).x() < vc->mapTo(a, QPoint(0, 0)).x();
    vc->centrerSur(ptMm(400, 400));  // le circuit regarde ailleurs
    attendre(100);
    const bool choisi = cliquerSymbole(a, doc, "R3");
    attendre(300);
    const EmpreintePlacee* r3 = empreinteDe(*conception(doc), "R3");
    const bool cadre = r3 && vc->rect().contains(vc->versEcran(r3->position).toPoint());
    bool auCircuit = false;
    for (const auto& o : vc->selection()) auCircuit |= r3 && o.id == r3->composant;
    verifier(coteACote && choisi && auCircuit && cadre, "3.21 : schéma et circuit côte à côte ; R3 choisi à gauche est choisi et cadré à droite", {});
    // Un clic dans le circuit le rend actif : le ruban suit
    reel::cliquer(vc, QPoint(vc->width() - 20, vc->height() - 20));
    attendre(250);
    verifier(a->mode() == AtelierElectronique::Circuit && a->mosaique()->active() == vc, "3.21 : un clic dans le circuit de la mosaïque y passe, le ruban suit", {});

    // Tableau de tous les objets, vue de plus : un bloc collé et une formule, chacun en un pas (ERG-41)
    a->montrerTableauObjets(true);
    attendre(300);
    TableauObjets* to = a->tableauObjets();
    const std::string idR1 = conception(doc)->parReference("R1") ? conception(doc)->parReference("R1")->uuid : std::string();
    const int ligne = to ? to->ligne(GenreObjet::Empreinte, idR1) : -1;
    const int pas = doc->positionHistorique();
    if (to && ligne >= 0) to->collerTexte(ligne, TableauObjets::colonne(ProprieteObjet::X), QStringLiteral("30,48\t20,32"));
    attendre(250);
    const EmpreintePlacee* r1 = empreinteDe(*conception(doc), "R1");
    const bool colle = r1 && r1->position == ptMm(30.48, 20.32) && doc->positionHistorique() == pas + 1;
    if (to && ligne >= 0) {
      to->table()->clearSelection();
      to->table()->item(ligne, TableauObjets::colonne(ProprieteObjet::X))->setSelected(true);
      to->appliquerRegle(RegleTexte::Genre::Formule, {}, QStringLiteral("x + 2,54"));
    }
    attendre(250);
    r1 = empreinteDe(*conception(doc), "R1");
    const bool formule = r1 && r1->position.x == nm(33.02) && doc->positionHistorique() == pas + 2;
    verifier(to && to->isVisible() && ligne >= 0 && colle && formule, "3.21 : le tableau des objets, ouvert comme une vue, prend un bloc collé puis une formule « x + 2,54 », chacun en un pas",
             r1 ? std::to_string(mm(r1->position.x)) + " mm" : std::string());
    vc->setFocus(Qt::OtherFocusReason);
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    attendre(150);
    reel::touche(vc, Qt::Key_Z, Qt::ControlModifier);
    attendre(200);
    a->montrerTableauObjets(false);
    a->setMosaique(false);
    a->setMode(AtelierElectronique::Schema);
    attendre(300);
    verifier(!vc->isVisible() && vs->isVisible(), "3.21 : une vue à la fois de nouveau", {});
  }

  // ---------- 3.19 · Carte sans liens appariée au schéma : par repère, deux à la main, en un pas (PLA-17, PLA-18) ----------
  {
    // La carte de l'exemple, déliée (identifiants neufs), décalée de 1 mm, deux repères changés
    ConceptionElec source = *conception(doc);
    std::map<std::string, std::string> neufs;
    for (auto& k : source.composants) {
      const std::string ancien = k.uuid;
      k.uuid = nouvelUuid();
      neufs[ancien] = k.uuid;
    }
    for (auto& e : source.circuit.empreintes) {
      e.composant = neufs[e.composant];
      e.position = e.position + ptMm(1, 1);
    }
    for (auto& fe : source.feuilles)
      for (auto& s : fe.symboles) s.composant = neufs.count(s.composant) ? neufs[s.composant] : s.composant;
    for (auto& k : source.composants)
      if (k.reference == "R1" || k.reference == "R2") k.reference += "0";
    DialogueAppariement d(*a, source, a);
    d.show();
    attendre(300);
    const std::size_t paires = d.paires().size();
    int parRepere = 0;
    for (const auto& p : d.paires()) parRepere += p.par == Appariement::Par::Repere;
    // Les lignes de R1 et R2, glissées l'une sur l'autre : leurs empreintes s'échangent
    int lR1 = -1, lR2 = -1;
    for (int l = 0; l < d.table()->rowCount(); ++l) {
      if (d.table()->item(l, 0)->text().startsWith(QStringLiteral("R1 "))) lR1 = l;
      if (d.table()->item(l, 0)->text().startsWith(QStringLiteral("R2 "))) lR2 = l;
    }
    const QString carteR1 = lR1 >= 0 ? d.table()->item(lR1, 1)->text() : QString();
    if (lR1 >= 0 && lR2 >= 0) {
      d.table()->scrollToItem(d.table()->item(lR2, 0));
      d.table()->scrollToItem(d.table()->item(lR1, 0));
      attendre(50);
      QWidget* vp = d.table()->viewport();
      const QPoint de = d.table()->visualItemRect(d.table()->item(lR1, 0)).center(), vers = d.table()->visualItemRect(d.table()->item(lR2, 0)).center();
      reel::appuyer(vp, de);
      reel::deplacer(vp, (de + vers) / 2);
      reel::deplacer(vp, vers);
      reel::relacher(vp, vers);
      attendre(150);
    }
    const bool echange = lR2 >= 0 && d.table()->item(lR2, 1)->text() == carteR1;
    const int pas = doc->positionHistorique();
    const int n = d.appliquer();
    d.close();
    attendre(300);
    const std::size_t total = conception(doc)->circuit.empreintes.size();
    verifier(paires == total && parRepere == static_cast<int>(total) - 2 && echange && n > 0 && doc->positionHistorique() == pas + 1,
             "3.19 : une carte sans liens s'apparie au schéma (toutes sauf deux par leur repère), deux lignes glissées l'une sur l'autre, appliquée en un pas",
             std::to_string(parRepere) + " par repère sur " + std::to_string(total) + ", " + std::to_string(paires) + " paire(s), échange " + (echange ? "fait" : "non fait") + ", " +
                 std::to_string(n) + " reportée(s), " + std::to_string(doc->positionHistorique() - pas) + " pas");
    vs->setFocus(Qt::OtherFocusReason);
    reel::touche(vs, Qt::Key_Z, Qt::ControlModifier);
    attendre(250);
  }
}
