// Prométhée : essais d'interface de la 0.34, lot L8 — sérigraphie préparée depuis le ruban (8.13), PDF par couches
// (8.29), signaux série et récapitulatif (8.26), gabarit d'interface (8.26), profil d'impédance (8.21), routage par
// stratégies en une commande annulable (8.28). Chaque essai passe par la commande du ruban et ses boîtes de dialogue.
// Licence GPL-3.0-only.
#include <algorithm>
#include <functional>
#include <string>

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "FenetrePrincipale.hpp"
#include "VueCircuit.hpp"
#include "ZoneMessages.hpp"
#include "promethee/circuit_edition.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/lignes_transmission.hpp"
#include "promethee/pdf_documents.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;
using Verifier = std::function<void(bool, const std::string&, const std::string&)>;

namespace {
void attendre(int ms) {
  QEventLoop boucle;
  QTimer::singleShot(ms, &boucle, &QEventLoop::quit);
  boucle.exec();
}
const ConceptionElec* conception(Document* doc) { return doc->projet().electronique.empty() ? nullptr : &doc->projet().electronique[0]; }
ConceptionElec carte(const std::string& nom, double l = 50, double h = 30) {
  ConceptionElec c = nouvelleConception(nom);
  c.circuit.contour.rayon = 0;
  c.circuit.contour.largeur = nm(l);
  c.circuit.contour.hauteur = nm(h);
  return c;
}
AtelierElectronique* ouvrir(FenetrePrincipale& f, ConceptionElec c) {
  prom::Projet p = prom::projetVide();
  p.sansCarte = true;
  p.nom = c.nom;
  p.electronique.push_back(std::move(c));
  f.document()->creer(p);
  f.montrerElectronique(0, AtelierElectronique::Circuit);
  attendre(400);
  AtelierElectronique* a = f.atelierElectronique();
  if (!a) return nullptr;
  for (const auto x : {AtelierElectronique::Panneau::Projet, AtelierElectronique::Panneau::Bibliotheque, AtelierElectronique::Panneau::Inspecteur, AtelierElectronique::Panneau::Verifications,
                       AtelierElectronique::Panneau::Recherche, AtelierElectronique::Panneau::Apprendre, AtelierElectronique::Panneau::Messages, AtelierElectronique::Panneau::Ecarts,
                       AtelierElectronique::Panneau::Champs, AtelierElectronique::Panneau::APlacer, AtelierElectronique::Panneau::Reseaux})
    if (QDockWidget* d = a->panneau(x)) d->hide();
  attendre(150);
  a->recalculer();
  attendre(250);
  return a;
}
std::string derniersMessages(AtelierElectronique* a, int n = 2) {
  std::string r;
  if (!a || !a->journal()) return r;
  const auto& m = a->journal()->messages();
  for (int i = std::max(0, static_cast<int>(m.size()) - n); i < static_cast<int>(m.size()); ++i) {
    QString t = m[static_cast<std::size_t>(i)].html;
    t.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    r += " | " + t.toStdString();
  }
  return r;
}
// Les boîtes de dialogue modales ouvertes par une commande, dans l'ordre : chaque réponse attend la suivante
struct Reponse {
  enum class Genre { Saisie, Question, Fichier } genre;
  std::function<void(QInputDialog*)> saisie;
  bool oui = true;
  QString fichier;
};
void repondreDans(const std::vector<Reponse>& suite, std::size_t i = 0, int essais = 0) {
  if (i >= suite.size() || essais > 80) return;
  QTimer::singleShot(100, [suite, i, essais] {
    QWidget* w = QApplication::activeModalWidget();
    const Reponse& r = suite[i];
    bool fait = false;
    if (r.genre == Reponse::Genre::Saisie)
      if (auto* d = qobject_cast<QInputDialog*>(w)) {
        if (r.saisie) r.saisie(d);
        d->accept();
        fait = true;
      }
    if (r.genre == Reponse::Genre::Question)
      if (auto* m = qobject_cast<QMessageBox*>(w)) {
        m->done(r.oui ? QMessageBox::Yes : QMessageBox::No);
        fait = true;
      }
    if (r.genre == Reponse::Genre::Fichier)
      if (auto* d = qobject_cast<QFileDialog*>(w)) {
        d->selectFile(r.fichier);
        static_cast<QDialog*>(d)->accept();  // protégé dans QFileDialog, public dans QDialog
        fait = true;
      }
    repondreDans(suite, fait ? i + 1 : i, fait ? 0 : essais + 1);
  });
}
Reponse saisie(std::function<void(QInputDialog*)> f) { return {Reponse::Genre::Saisie, std::move(f), true, {}}; }
Reponse oui() { return {Reponse::Genre::Question, {}, true, {}}; }
Reponse fichier(const QString& f) { return {Reponse::Genre::Fichier, {}, true, f}; }
int compter(const ConceptionElec& c, const std::string& code) {
  const Connectivite k = connectivite(c);
  const auto v = verifierDRC(c, k, remplirZones(c, k));
  return static_cast<int>(std::count_if(v.begin(), v.end(), [&](const Violation& x) { return x.code == code; }));
}
}  // namespace

void essaisElectronique034L8(FenetrePrincipale& f, const Verifier& verifier) {
  Document* doc = f.document();
  const QString dossier = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("promethee-essais-034L8"));
  QDir(dossier).removeRecursively();
  QDir().mkpath(dossier);

  // ---------- 8.13 · Sérigraphie préparée : la liste, puis une commande annulable ----------
  {
    ConceptionElec c0 = carte("Sérigraphie");
    const std::string r1 = poserEmpreinteSeule(c0, "R_0603", ptMm(20, 15));
    // Un texte de carte posé sur la pastille 1
    Pt p1;
    for (const auto& p : pastilles(c0, connectivite(c0)))
      if (p.composant == r1 && p.numero == "1") p1 = p.pastille.position;
    TexteCircuit t;
    t.uuid = nouvelUuid();
    t.texte = "TP";
    t.couche = Couche::SerigraphieDessus;
    t.p = p1;
    t.hauteur = 800'000;
    c0.circuit.textes.push_back(t);
    AtelierElectronique* a = ouvrir(f, c0);
    bool ok = false;
    std::string detail;
    if (a) {
      const int avant = compter(*conception(doc), "DRC-13");
      repondreDans({oui()});
      a->executerAction("fabrication.preparerSerigraphie");
      attendre(400);
      const int apres = compter(*conception(doc), "DRC-13");
      doc->annuler();
      attendre(200);
      const int annule = compter(*conception(doc), "DRC-13");
      ok = avant > 0 && apres == 0 && annule == avant;
      detail = "DRC-13 : " + std::to_string(avant) + " → " + std::to_string(apres) + ", annulé : " + std::to_string(annule) + derniersMessages(a);
    }
    verifier(ok, "8.13 : sérigraphie préparée depuis le ruban, liste confirmée, plus aucune sérigraphie sur une pastille ; un pas d'annulation", detail);
  }

  // ---------- 8.29 · PDF du circuit par couches, 8.26 signaux série et récapitulatif ----------
  {
    ConceptionElec c0 = carte("Bus série", 60, 30);
    const std::string u1 = poserEmpreinteSeule(c0, "PinHeader_1x02_P2.54", ptMm(8, 10));
    const std::string r1 = poserEmpreinteSeule(c0, "R_0603", ptMm(30, 10));
    const std::string u2 = poserEmpreinteSeule(c0, "PinHeader_1x02_P2.54", ptMm(52, 10));
    c0.nomsReseaux[u1 + ":1"] = "SDA_A";
    c0.nomsReseaux[r1 + ":1"] = "SDA_A";
    c0.nomsReseaux[r1 + ":2"] = "SDA_B";
    c0.nomsReseaux[u2 + ":1"] = "SDA_B";
    AtelierElectronique* a = ouvrir(f, c0);
    bool pdfOk = false, signauxOk = false, xlsxOk = false;
    std::string detail;
    if (a) {
      const QString pdf = QDir(dossier).filePath(QStringLiteral("couches.pdf"));
      repondreDans({fichier(pdf)});
      a->executerAction("fabrication.pdfCouches");
      attendre(500);
      if (QFile::exists(pdf)) {
        const LecturePdf l = lirePdf(prom::lireFichier(prom::cheminFichier(pdf.toStdString())));
        const Connectivite k = connectivite(*conception(doc));
        const ModeleFabrication m = modeleFabrication(*conception(doc), k, remplirZones(*conception(doc), k));
        pdfOk = l.ok && l.pages == static_cast<int>(m.couches.size()) - 1 && !l.signets.empty();
        detail = std::to_string(l.pages) + " page(s), " + std::to_string(l.signets.size()) + " signet(s)";
      } else
        detail = "PDF absent";
      a->executerAction("regles.signauxSerie");
      attendre(300);
      const auto& S = conception(doc)->circuit.regles.avancees.signaux;
      signauxOk = S.size() == 1 && S.front().reseaux.size() == 2;
      detail += ", " + std::to_string(S.size()) + " signal(aux)";
      const QString xlsx = QDir(dossier).filePath(QStringLiteral("signaux.xlsx"));
      repondreDans({fichier(xlsx)});
      a->executerAction("fabrication.signauxXlsx");
      attendre(400);
      xlsxOk = QFileInfo(xlsx).size() > 1000 && lireXlsx(prom::lireFichier(prom::cheminFichier(xlsx.toStdString()))).size() == 2;
      detail += xlsxOk ? ", classeur écrit" : ", classeur absent";
    }
    verifier(pdfOk, "8.29 : PDF du circuit par couches depuis le ruban, une page et un signet par couche", detail);
    verifier(signauxOk && xlsxOk, "8.26 : signal suivi à travers la résistance série, récapitulatif en XLSX", detail);
  }

  // ---------- 8.26 · Gabarit d'interface, 8.21 profil d'impédance d'une classe ----------
  {
    ConceptionElec c0 = carte("Interfaces", 60, 40);
    c0.circuit.empilage.couchesCuivre = 4;
    AtelierElectronique* a = ouvrir(f, c0);
    bool gabarit = false, profil = false;
    std::string detail;
    if (a) {
      repondreDans({saisie([](QInputDialog* d) { d->setTextValue(QStringLiteral("USB 2.0")); }), saisie([](QInputDialog* d) { d->setTextValue(QStringLiteral("USB_")); })});
      a->executerAction("regles.gabaritInterface");
      attendre(500);
      const auto& A = conception(doc)->circuit.regles.avancees;
      gabarit = A.paires.size() == 1 && A.paires.front().positif == "USB_D_P" && A.paires.front().largeur > 0;
      detail = std::to_string(A.paires.size()) + " paire(s), " + std::to_string(conception(doc)->circuit.regles.classes.size()) + " classe(s)";
      repondreDans({saisie([](QInputDialog* d) { d->setTextValue(QStringLiteral("USB2")); }), saisie([](QInputDialog* d) { d->setTextValue(QStringLiteral("Simple")); }),
                    saisie([](QInputDialog* d) { d->setDoubleValue(50); })});
      a->executerAction("regles.profilImpedance");
      attendre(500);
      const ProfilImpedance* p = nullptr;
      for (const auto& x : conception(doc)->circuit.regles.avancees.impedances)
        if (x.nom == "USB2") p = &x;
      profil = p && !p->differentiel && p->parCouche.size() == 4 && largeurSurCouche(*conception(doc), "USB_D_P", Couche::CuivreInt1) != largeurSurCouche(*conception(doc), "USB_D_P", Couche::CuivreDessus);
      detail += p ? ", profil sur " + std::to_string(p->parCouche.size()) + " couche(s)" : ", profil absent";
    }
    verifier(gabarit, "8.26 : gabarit d'interface USB 2.0 appliqué depuis le ruban : paire, classe et règles d'un geste", detail);
    verifier(profil, "8.21 : profil d'impédance d'une classe calculé par couche, largeur différente dehors et dedans", detail);
  }

  // ---------- 8.28 · Routage par stratégies : essais comparés, le meilleur appliqué en un pas ----------
  {
    ConceptionElec c0 = carte("Stratégies", 80, 30);
    std::vector<std::string> r;
    for (int i = 0; i < 8; ++i) r.push_back(poserEmpreinteSeule(c0, "R_0603", ptMm(8 + 9 * i, 15)));
    for (int i = 0; i + 1 < 8; ++i) {
      c0.nomsReseaux[r[static_cast<std::size_t>(i)] + ":2"] = "N" + std::to_string(i);
      c0.nomsReseaux[r[static_cast<std::size_t>(i + 1)] + ":1"] = "N" + std::to_string(i);
    }
    AtelierElectronique* a = ouvrir(f, c0);
    bool ok = false;
    std::string detail;
    if (a) {
      repondreDans({oui()});
      a->executerAction("circuit.routerStrategies");
      attendre(800);
      const ConceptionElec* c = conception(doc);
      const Connectivite k = connectivite(*c);
      const std::size_t restantes = etatRoutage(*c, k, remplirZones(*c, k)).chevelu.size(), pistes = c->circuit.pistes.size();
      doc->annuler();
      attendre(200);
      ok = restantes == 0 && pistes > 0 && conception(doc)->circuit.pistes.empty();
      detail = std::to_string(pistes) + " piste(s), " + std::to_string(restantes) + " liaison(s) restante(s), après annulation : " + std::to_string(conception(doc)->circuit.pistes.size()) + derniersMessages(a);
    }
    verifier(ok, "8.28 : deux stratégies comparées, la meilleure appliquée en une commande, annulée en un pas", detail);
  }
}
