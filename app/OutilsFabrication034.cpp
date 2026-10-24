// Prométhée : commandes du lot L8 de la 0.34 dans l'atelier électronique — sérigraphie préparée (8.13), règles
// d'assemblage et pâte (8.14), profils d'impédance (8.21), signaux et gabarits d'interface (8.26), routage par
// stratégies et échanges de broches (8.28), PDF du circuit par couches (8.29). Chaque changement est une commande,
// annulable en un pas ; une liste des changements précède toujours la commande. Licence GPL-3.0-only.
#include <algorithm>
#include <functional>

#include <QApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>

#include "AtelierElectronique.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/lignes_transmission.hpp"
#include "promethee/pdf_documents.hpp"
#include "promethee/routage_auto.hpp"
#include "promethee/serigraphie_preparee.hpp"
#include "promethee/signaux.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString liste(const std::vector<std::string>& v, std::size_t max = 25) {
  QString r;
  for (std::size_t i = 0; i < v.size() && i < max; ++i) r += QStringLiteral("• ") + q(v[i]).toHtmlEscaped() + QStringLiteral("<br>");
  if (v.size() > max) r += QStringLiteral("… et %1 autre(s)").arg(v.size() - max);
  return r;
}
}  // namespace

void AtelierElectronique::enregistrerCommandesL8() {
  auto& c = m_commandes;
  auto faire = [this](const QString& nom, const std::function<QString(ConceptionElec&)>& f) {
    QString bilan;
    if (modifierConception([&](ConceptionElec& x) { bilan = f(x); }, nom)) {
      if (!bilan.isEmpty()) afficherMessage(bilan, NiveauMessage::Succes);
    } else if (!bilan.isEmpty())
      afficherMessage(bilan, NiveauMessage::Information);
  };
  auto confirmer = [this](const QString& titre, const QString& texte) {
    return QMessageBox::question(this, titre, texte, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
  };
  auto enregistrer = [this](const QString& titre, const QString& nom, const QString& filtre, const std::function<std::string(const ConceptionElec&)>& produire) {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const QString f = QFileDialog::getSaveFileName(this, titre, q(x->nom) + nom, filtre);
    if (f.isEmpty()) return;
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), produire(*x));
      afficherMessage(QStringLiteral("<b>%1</b> enregistré.").arg(titre), NiveauMessage::Succes);
    } catch (const std::exception& e) {
      afficherMessage(QStringLiteral("%1 : %2").arg(titre, QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    }
  };

  // ---------- 8.13 · Sérigraphie préparée : la liste, puis la commande ----------
  c["fabrication.preparerSerigraphie"] = [this, faire, confirmer] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const auto prevus = preparerSerigraphie(*x);
    if (prevus.empty()) {
      afficherMessage(QStringLiteral("<b>Sérigraphie déjà prête</b> : aucun trait ni repère sur une pastille."), NiveauMessage::Information);
      return;
    }
    std::vector<std::string> lignes;
    for (const auto& p : prevus) lignes.push_back(p.description);
    if (!confirmer(QStringLiteral("Préparer la sérigraphie"), QStringLiteral("<b>%1 changement(s)</b> :<br>%2<br>Appliquer ?").arg(prevus.size()).arg(liste(lignes)))) return;
    faire(QStringLiteral("Préparer la sérigraphie"), [](ConceptionElec& y) {
      return QStringLiteral("<b>Sérigraphie préparée</b> : %1 changement(s) ; les traits rognés vivent dans des copies locales des empreintes.").arg(appliquerPreparationSerigraphie(y).size());
    });
  };
  // ---------- 8.29 · PDF du circuit par couches ----------
  c["fabrication.pdfCouches"] = [enregistrer] {
    enregistrer(QStringLiteral("PDF du circuit par couches"), QStringLiteral(" - couches.pdf"), QStringLiteral("PDF (*.pdf)"), [](const ConceptionElec& y) { return pdfCircuit(y); });
  };
  // ---------- 8.26 · Signaux, gabarits d'interface, récapitulatif ----------
  c["fabrication.signauxXlsx"] = [enregistrer] {
    enregistrer(QStringLiteral("Récapitulatif des signaux"), QStringLiteral(" - signaux.xlsx"), QStringLiteral("Classeur (*.xlsx)"), [](const ConceptionElec& y) { return xlsxSignaux(y); });
  };
  c["regles.signauxSerie"] = [faire] {
    faire(QStringLiteral("Signaux à travers les composants série"), [](ConceptionElec& y) {
      const int n = creerSignauxSerie(y);
      return n ? QStringLiteral("<b>%1 signal(aux) créé(s)</b> à travers les résistances, condensateurs et ferrites série.").arg(n)
               : QStringLiteral("Aucun nouveau signal : pas de composant série entre deux réseaux de signal.");
    });
  };
  c["regles.gabaritInterface"] = [this, faire] {
    QStringList noms;
    for (const auto& g : gabaritsInterface()) noms << q(g.nom);
    noms << QStringLiteral("Lire un gabarit (JSON)…");
    bool ok = false;
    const QString choix = QInputDialog::getItem(this, QStringLiteral("Gabarit d'interface"), QStringLiteral("Interface :"), noms, 0, false, &ok);
    if (!ok) return;
    std::optional<GabaritInterface> g;
    if (choix == noms.back()) {
      const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Gabarit d'interface"), {}, QStringLiteral("JSON (*.json)"));
      if (f.isEmpty()) return;
      std::string err;
      try {
        g = lireGabaritInterface(prom::lireFichier(prom::cheminFichier(f.toStdString())), &err);
      } catch (const std::exception& e) {
        err = e.what();
      }
      if (!g) {
        afficherMessage(QStringLiteral("Gabarit illisible : %1").arg(q(err).toHtmlEscaped()), NiveauMessage::Erreur);
        return;
      }
    } else
      for (const auto& x : gabaritsInterface())
        if (q(x.nom) == choix) g = x;
    const QString prefixe = QInputDialog::getText(this, QStringLiteral("Gabarit d'interface"), QStringLiteral("Préfixe des réseaux (« USB_ » donne USB_D_P…) :"), QLineEdit::Normal, {}, &ok);
    if (!ok || !g) return;
    faire(QStringLiteral("Gabarit « %1 »").arg(q(g->nom)), [&](ConceptionElec& y) {
      const BilanGabarit b = appliquerGabarit(y, *g, prefixe.toStdString());
      return QStringLiteral("<b>%1</b><br>%2").arg(q(b.journal.front()).toHtmlEscaped(), liste({b.journal.begin() + 1, b.journal.end()}, 8));
    });
  };
  // ---------- 8.21 · Profil d'impédance d'une classe ----------
  c["regles.profilImpedance"] = [this, faire] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    QStringList classes;
    for (const auto& k : x->circuit.regles.classes) classes << q(k.nom);
    if (classes.isEmpty()) {
      afficherMessage(QStringLiteral("Crée d'abord une classe de réseaux : le profil porte son nom et ses réseaux en suivent la largeur."), NiveauMessage::Information);
      return;
    }
    bool ok = false;
    const QString classe = QInputDialog::getItem(this, QStringLiteral("Profil d'impédance"), QStringLiteral("Classe de réseaux :"), classes, 0, false, &ok);
    if (!ok) return;
    const QStringList genres{QStringLiteral("Simple"), QStringLiteral("Différentiel")};
    const QString genre = QInputDialog::getItem(this, QStringLiteral("Profil d'impédance"), QStringLiteral("Ligne :"), genres, 0, false, &ok);
    if (!ok) return;
    const double z = QInputDialog::getDouble(this, QStringLiteral("Profil d'impédance"), QStringLiteral("Impédance visée (Ω) :"), genre == genres[1] ? 90 : 50, 10, 300, 1, &ok);
    if (!ok) return;
    faire(QStringLiteral("Profil d'impédance « %1 »").arg(classe), [&](ConceptionElec& y) {
      ProfilImpedance p;
      p.nom = classe.toStdString();
      p.impedance = z;
      p.differentiel = genre == genres[1];
      calculerProfil(y, p, p.differentiel ? 150'000 : 0);
      QString r = QStringLiteral("<b>Profil « %1 » à %2 Ω</b> :").arg(classe).arg(z);
      for (const auto& [couche, lw] : p.parCouche) r += QStringLiteral("<br>%1 : %2 mm").arg(q(couche)).arg(QLocale().toString(mm(lw.first), 'f', 3));
      auto& v = y.circuit.regles.avancees.impedances;
      std::erase_if(v, [&](const ProfilImpedance& a) { return a.nom == p.nom; });
      v.push_back(std::move(p));
      return r;
    });
  };
  // ---------- 8.28 · Routage par stratégies, échanges de broches ----------
  c["circuit.routerStrategies"] = [this, faire, confirmer] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto rapport = rapportPreparation(*x);
    auto essais = comparerStrategies(*x, strategiesRoutage(*x));
    QApplication::restoreOverrideCursor();
    if (essais.empty()) return;
    QString t = QStringLiteral("<b>Préparation</b><br>%1<br><b>Essais comparés</b><br>").arg(liste(rapport, 6));
    for (const auto& e : essais)
      t += QStringLiteral("• %1 : %2/%3 liaison(s), %4 via(s), %5 mm, %6 s<br>")
               .arg(q(e.strategie).toHtmlEscaped())
               .arg(e.routees)
               .arg(e.liaisons)
               .arg(e.vias)
               .arg(QLocale().toString(e.longueur, 'f', 1))
               .arg(QLocale().toString(e.duree, 'f', 2));
    t += QStringLiteral("Appliquer « %1 » ? Les pistes déjà posées restent.").arg(q(essais.front().strategie).toHtmlEscaped());
    if (!confirmer(QStringLiteral("Routage par stratégies"), t)) return;
    const EssaiRoutage meilleur = std::move(essais.front());
    faire(QStringLiteral("Routage « %1 »").arg(q(meilleur.strategie)), [&](ConceptionElec& y) {
      const int n = appliquerEssai(y, meilleur);
      return QStringLiteral("<b>Routage « %1 »</b> : %2 objet(s) de cuivre posé(s), %3 liaison(s) routée(s) sur %4.").arg(q(meilleur.strategie)).arg(n).arg(meilleur.routees).arg(meilleur.liaisons);
    });
  };
  c["circuit.echangesBroches"] = [this, faire, confirmer] {
    const ConceptionElec* x = conceptionCourante();
    if (!x) return;
    const auto e = optimiserEchanges(*x);
    if (e.empty()) {
      afficherMessage(QStringLiteral("Aucun échange de broches ne raccourcit le chevelu (broches d'un même groupe)."), NiveauMessage::Information);
      return;
    }
    std::vector<std::string> lignes;
    double gain = 0;
    for (const auto& y : e) {
      lignes.push_back(y.reference + " : " + y.numeroA + " ↔ " + y.numeroB);
      gain += y.gain;
    }
    if (!confirmer(QStringLiteral("Échanges de broches"), QStringLiteral("<b>%1 échange(s)</b>, chevelu raccourci d'environ %2 mm :<br>%3<br>Reporter au schéma ?").arg(e.size()).arg(QLocale().toString(gain, 'f', 1)).arg(liste(lignes))))
      return;
    faire(QStringLiteral("Échanges de broches"), [&](ConceptionElec& y) { return QStringLiteral("<b>%1 échange(s) de broches</b> reporté(s) au schéma.").arg(appliquerEchanges(y, e)); });
  };
}
