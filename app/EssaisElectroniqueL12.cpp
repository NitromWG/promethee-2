// Prométhée : essais d'interface des lots L12 à L19 : outils de conception (variante active en un pas, revue, coût,
// mise en service, logique), interface JSON-RPC sur le canal local (dix vias, un seul Ctrl+Z), verrou partagé
// (projet tenu par un autre poste ouvert en consultation), historique local tenu à chaque enregistrement.
// Appelés à la fin de « Promethee --essais-electronique », ou seuls avec PROMETHEE_ESSAIS_LOTS=L12.
// Licence GPL-3.0-only.
#include <functional>
#include <string>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalSocket>
#include <QSpinBox>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "EvenementsReels.hpp"
#include "FenetreOutilsConception.hpp"
#include "FenetrePrincipale.hpp"
#include "ServeurApi.hpp"
#include "promethee/collaboration.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/historique_local.hpp"
#include "promethee/modele.hpp"
#include "promethee/variantes.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
AtelierElectronique* ouvrirExemple(FenetrePrincipale& f) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = "Clignotant 555";
  p.electronique.push_back(exempleElectronique());
  f.document()->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Schema);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (a)
    for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur})
      if (QDockWidget* d = a->panneau(x)) d->hide();
  return a;
}
QString dossierEssai(const QString& nom) {
  const QString d = QDir(QDir::tempPath()).filePath(QStringLiteral("promethee-essais-l12-") + nom);
  QDir(d).removeRecursively();
  QDir().mkpath(d);
  return d;
}
}  // namespace

void essaisElectroniqueL12(FenetrePrincipale& f, const Verifier& verifier0) {
  const auto verifier = [&verifier0, &f](bool ok, const std::string& quoi, const std::string& detail) {
    verifier0(ok, quoi, detail);
    if (!QApplication::activeWindow() && !QApplication::activeModalWidget()) reel::activer(&f);
  };
  Document* doc = f.document();
  AtelierElectronique* a = ouvrirExemple(f);
  if (!a) {
    verifier(false, "L12 : l'exemple s'ouvre", {});
    return;
  }

  // ---------- 12.1, 12.7 · Outils de conception : variante active choisie en un pas ----------
  {
    doc->modifierConception(0, [](ConceptionElec& c) {
      ajouterVariante(c, "sans LED");
      SurchargeVariante nm;
      nm.montage = MontageVariante::NonMonte;
      reglerSurcharge(c, "D1", "sans LED", nm);
    }, QStringLiteral("Variante « sans LED »"));
    a->executerAction("outils.conception");
    attendre(300);
    FenetreOutilsConception* o = a->outilsConception();
    if (!o) {
      verifier(false, "12.1 : la fenêtre des outils de conception s'ouvre", {});
      return;
    }
    const QString variantes = o->rapport(QStringLiteral("variantes"));
    const int pas = doc->nomAnnuler().isEmpty() ? 0 : 1;
    o->varianteActive()->setCurrentIndex(1);
    emit o->varianteActive()->activated(1);
    attendre(150);
    const bool active = conception(doc)->varianteActive == "sans LED";
    const QString annuler = doc->nomAnnuler();
    o->cartes()->setValue(100);
    attendre(150);
    const bool cout = o->rapport(QStringLiteral("variantes")).contains(QStringLiteral("Coût pour 100 cartes (variante « sans LED »)"));
    doc->annuler();
    attendre(150);
    verifier(variantes.contains(QStringLiteral("D1")) && variantes.contains(QStringLiteral("non monté")) && active && pas && annuler.contains(QStringLiteral("Variante active")) && cout &&
                 conception(doc)->varianteActive.empty(),
             "12.1, 12.7 : variante « sans LED » : D1 non monté au tableau, variante active choisie en un pas, coût par variante",
             annuler.toStdString());
    // Les autres onglets se lisent sur l'exemple
    bool tous = true;
    std::string vides;
    for (const char* id : {"revue", "systeme", "notes", "historique", "essais", "integrite", "logique", "atelier", "exigences"})
      if (!o->montrer(QString::fromLatin1(id)) || o->rapport(QString::fromLatin1(id)).trimmed().isEmpty()) {
        tous = false;
        vides += std::string(id) + " ";
      }
    const bool plan = o->rapport(QStringLiteral("essais")).contains(QStringLiteral("ohmmètre entre +5V"));
    verifier(tous && plan, "L12 à L19 : chaque onglet des outils rend son rapport (revue, système, notes, historique, mise en service, intégrité, logique, atelier, exigences)", vides);
    o->close();
  }

  // ---------- 13.10 · Dix vias posées par un programme extérieur, un seul Ctrl+Z les retire ----------
  {
    ServeurApi* api = f.findChild<ServeurApi*>(QStringLiteral("serveurApi"));
    const std::size_t avant = conception(doc)->circuit.vias.size();
    QLocalSocket s;
    bool connecte = false, reponse = false;
    std::string detail;
    if (api) {
      s.connectToServer(api->nom());
      connecte = s.waitForConnected(2000);
      if (connecte) {
        QString lot = QStringLiteral("{\"jsonrpc\":\"2.0\",\"id\":42,\"method\":\"lot\",\"params\":[");
        for (int i = 0; i < 10; ++i) lot += QStringLiteral("%1{\"method\":\"via.poser\",\"params\":{\"x\":%2,\"y\":4,\"reseau\":\"GND\"}}").arg(i ? "," : "").arg(10 + 2 * i);
        lot += QStringLiteral("]}\n");
        s.write(lot.toUtf8());
        s.flush();
        // La réponse arrive par la boucle d'événements de l'application, qui tourne pendant l'attente
        QElapsedTimer t;
        t.start();
        while (!s.canReadLine() && t.elapsed() < 5000) {
          QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
          s.waitForReadyRead(20);
        }
        const QByteArray r = s.readLine();
        reponse = r.contains("\"id\":42") && r.contains("\"result\"");
        detail = r.left(120).toStdString();
      }
    }
    attendre(100);
    const std::size_t apres = conception(doc)->circuit.vias.size();
    const QString pas = doc->nomAnnuler();
    doc->annuler();
    attendre(100);
    char t[160];
    std::snprintf(t, sizeof t, "canal %s, vias %zu → %zu → %zu, pas « %s »", api ? api->nom().toStdString().c_str() : "absent", avant, apres, conception(doc)->circuit.vias.size(),
                  pas.toStdString().c_str());
    verifier(connecte && reponse && apres == avant + 10 && conception(doc)->circuit.vias.size() == avant && pas.startsWith(QStringLiteral("API")),
             "13.10 : programme extérieur, canal local : dix vias posées en un lot, un seul Ctrl+Z les retire", std::string(t) + " ; " + detail);
  }

  // ---------- 12.10 · Projet tenu par un autre poste : ouvert en consultation, avec son nom ----------
  // Une exception (fichier refusé) est un échec de l'essai, jamais une boîte modale qui bloquerait le parcours
  try {
    const QString d = dossierEssai(QStringLiteral("verrou"));
    const QString chemin = QDir(d).filePath(QStringLiteral("partage.prom"));
    prom::Projet p = prom::projetVide();
    p.electronique.push_back(exempleElectronique());
    prom::ecrireProjet(p, chemin.toStdString());
    prom::poserVerrou(chemin.toStdString(), "Lou", "ATELIER-2");
    doc->ouvrir(chemin);
    attendre(200);
    const bool consultation = doc->consultation();
    const QString tenu = doc->tenuPar();
    // Le signal de vie de l'autre poste s'arrête : à la réouverture, le verrou périmé est repris
    {
      nlohmann::ordered_json o = nlohmann::ordered_json::parse(prom::lireFichier(prom::cheminFichier(prom::cheminVerrou(chemin.toStdString()))));
      o["vie"] = prom::secondesMaintenant() - 600;
      prom::ecrireFichierAtomique(prom::cheminFichier(prom::cheminVerrou(chemin.toStdString())), o.dump());
    }
    doc->ouvrir(chemin);
    attendre(200);
    const auto v = prom::lireVerrou(chemin.toStdString());
    const bool repris = !doc->consultation() && v && v->personne == Document::personne().toStdString();
    verifier(consultation && tenu.contains(QStringLiteral("Lou sur ATELIER-2")) && repris, "12.10 : projet tenu par un autre poste : ouvert en consultation avec son nom ; verrou périmé repris",
             tenu.toStdString());

    // ---------- 12.9 · Historique local : un instantané par enregistrement, le fichier restitué à l'octet ----------
    const auto valeurR1 = [doc](const char* v) {
      doc->modifierConception(0, [v](ConceptionElec& c) {
        for (auto& k : c.composants)
          if (k.reference == "R1") k.valeur = v;
      }, QStringLiteral("Valeur de R1"));
    };
    valeurR1("2k2");
    doc->enregistrer(chemin);
    valeurR1("3k3");
    doc->enregistrer(chemin);
    const prom::HistoriqueLocal h(doc->dossierHistorique().toStdString());
    const auto c = h.chronologie();
    bool exact = c.size() >= 2;
    if (exact) exact = h.restaurer(c.back().id) == prom::lireFichier(prom::cheminFichier(chemin.toStdString()));
    char t[96];
    std::snprintf(t, sizeof t, "%zu instantané(s)", c.size());
    verifier(exact, "12.9 : historique local : un instantané par enregistrement, le dernier restitué à l'octet", t);
    doc->creer(prom::projetVide());  // le verrou du fichier d'essai est levé
  } catch (const std::exception& e) {
    verifier(false, "12.10, 12.9 : projet partagé ouvert, historique local tenu", e.what());
    doc->creer(prom::projetVide());
  }
}
