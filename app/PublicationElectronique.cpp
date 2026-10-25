// Prométhée : validation et publication d'une révision dans l'atelier électronique (lot L1, tâche 1.15, A1).
// Le reste de l'atelier est dans AtelierElectronique.cpp.
// Licence GPL-3.0-only.
#include "AtelierElectronique.hpp"


#include <QDateTime>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>

#include "Document.hpp"
#include "promethee/version.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

// ---------- Validation et publication ----------
Validation AtelierElectronique::validation(bool complete) const {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return {};
  if (complete && m_calculs.generationFabrication != m_generation)
    return valider(*c, m_calculs.k, m_calculs.etat, m_erc, m_calculs.drc, controlerFabrication(*c, m_calculs.k, m_calculs.zones).constats);
  return valider(*c, m_calculs.k, m_calculs.etat, m_erc, m_calculs.drc, m_calculs.fabrication);
}

bool AtelierElectronique::publier(const QString& dossierParent, const QString& auteur, const QString& derogation, QString* erreur) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return false;
  ConceptionElec copie = *c;
  try {
    std::vector<std::string> fichiers;
    // Les dessins (schéma, plan de montage) sont écrits dans le dossier en cours de publication : ils sont
    // contrôlés et ont leur empreinte SHA-256 comme les autres fichiers.
    const auto dessins = [this](const std::string& dossier) {
      std::vector<std::string> r;
      for (const QString& d : ajouterDessins(QString::fromStdString(dossier))) r.push_back(d.toStdString());
      return r;
    };
    const Publication p = prom::ecad::publier(copie, dossierParent.toStdString(), auteur.toStdString(), QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")).toStdString(),
                                              derogation.toStdString(), prom::infosVersion().identifiant, &fichiers, dessins);
    const int i = m_conception;
    // La publication ne s'annule pas (le dossier est écrit, la révision est verrouillée), mais elle n'efface plus
    // l'historique : le reste du projet s'annule normalement, la révision publiée reste telle quelle (EL-18)
    m_doc->modifier([&](prom::Projet& pr) {
      if (i >= 0 && i < static_cast<int>(pr.electronique.size())) pr.electronique[static_cast<std::size_t>(i)] = copie;
    });
    afficherMessage(QStringLiteral("<b>Publiée %1</b> (révision %2) : %3 fichiers dans « %4 ». La conception est verrouillée.")
                        .arg(q(p.id), q(p.revision)).arg(fichiers.size()).arg(q(p.dossier).toHtmlEscaped()), NiveauMessage::Succes);
    return true;
  } catch (const std::exception& e) {
    if (erreur) *erreur = QString::fromUtf8(e.what());
    afficherMessage(QStringLiteral("%1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
}

void AtelierElectronique::nouvelleRevision() {
  modifierConception([](ConceptionElec& c) { prom::ecad::nouvelleRevision(c); });
  if (const ConceptionElec* c = conceptionCourante())
    afficherMessage(QStringLiteral("<b>Révision %1</b> ouverte : la conception se modifie de nouveau ; la publication précédente reste dans l’historique.").arg(q(c->cartouche.revision)), NiveauMessage::Information);
}

void AtelierElectronique::publierInteractif() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  if (c->verrouillee) {
    QMessageBox::information(this, QStringLiteral("Déjà publiée"),
                             QStringLiteral("« %1 » est publiée et verrouillée. Ouvre une nouvelle révision pour la modifier et la publier de nouveau.").arg(q(c->nom)));
    return;
  }
  recalculer();
  const Validation v = validation(true);
  QString derogation;
  if (v.critiques() > 0) {
    // Erreurs critiques : aucune dérogation possible (courts-circuits, liaisons non routées, empreintes inconnues…)
    QString l;
    for (const auto& e : v.etapes)
      for (const auto& d : e.details)
        if (d.gravite == Gravite::Erreur && !derogeable(d.code)) l += QStringLiteral("<li><b>%1</b> %2</li>").arg(q(d.code), q(d.message).toHtmlEscaped());
    QMessageBox::warning(this, QStringLiteral("Publication impossible"),
                         QStringLiteral("<p><b>%1 erreur(s) critique(s)</b> empêchent la publication ; aucune dérogation ne peut les lever :</p><ul>%2</ul>"
                                        "<p>Corrige-les, puis publie de nouveau.</p>")
                             .arg(v.critiques())
                             .arg(l));
    return;
  }
  if (!v.publiable()) {
    // Porte de publication : erreurs dérogeables, avec une raison écrite gardée dans la fiche de publication
    QString l;
    for (const auto& e : v.etapes)
      for (const auto& d : e.details)
        if (d.gravite == Gravite::Erreur) l += QStringLiteral("\n  %1 %2").arg(q(d.code), q(d.message));
    bool ok = false;
    derogation = QInputDialog::getMultiLineText(this, QStringLiteral("Publier malgré les erreurs"),
                                                QStringLiteral("%1 erreur(s) dérogeable(s) bloquent la publication :%2\n\nPour publier quand même, écris la raison : elle sera gardée, "
                                                               "avec ton nom, la date et la liste des erreurs acceptées, dans la fiche de publication.")
                                                    .arg(v.bloquants())
                                                    .arg(l),
                                                QString(), &ok)
                     .trimmed();
    if (!ok || derogation.isEmpty()) return;
  }
  if (QMessageBox::question(this, QStringLiteral("Publier pour la fabrication"),
                            QStringLiteral("Publier « %1 », révision %2 ?\n\nLe dossier complet est écrit, la publication est notée avec l’empreinte de la conception et la révision "
                                           "est verrouillée. Cela ne s’annule pas : pour modifier ensuite, on ouvre une nouvelle révision.")
                                .arg(q(c->nom), q(c->cartouche.revision))) != QMessageBox::Yes)
    return;
  const QString dossier = QFileDialog::getExistingDirectory(this, QStringLiteral("Où écrire le dossier de publication ?"));
  if (dossier.isEmpty()) return;
  const QString auteur = c->cartouche.auteur.empty() ? qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER")) : q(c->cartouche.auteur);
  QString erreur;
  if (!publier(dossier, auteur, derogation, &erreur)) QMessageBox::warning(this, QStringLiteral("Publication impossible"), erreur);
}
