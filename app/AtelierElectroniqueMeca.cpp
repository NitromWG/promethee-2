// Prométhée : mécatronique dans l'atelier électronique (lot L6) : modèles 3D exacts, dossier de modèles, export STEP
// de la carte équipée, façade, contour piloté par une esquisse, pièces du boîtier pour l'impression 3D.
// Licence GPL-3.0-only.
#include <map>

#include <QDir>
#include <QFileInfo>
#include <QLocale>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "FenetreFacade.hpp"
#include "promethee/carte3d.hpp"
#include "promethee/contour_esquisse.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/modeles3d.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString nombre(double v, int d = 2) { return QLocale(QLocale::French).toString(v, 'f', d); }
}  // namespace

FenetreFacade* AtelierElectronique::facade() const { return m_facade; }
DialogueContourEsquisse* AtelierElectronique::contourEsquisse() const { return m_contourEsquisse; }

bool AtelierElectronique::associerModele3D(const std::string& composant, const QString& chemin) {
  std::string erreur;
  const auto m = prom::lireModele3D(chemin.toStdString(), &erreur);
  if (!m) {
    afficherMessage(QStringLiteral("Modèle <b>%1</b> illisible : %2").arg(QFileInfo(chemin).fileName().toHtmlEscaped(), q(erreur).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  std::string ref;
  modifierConception([&](ConceptionElec& c) {
    associerModele(c, composant, *m);
    if (const ComposantElec* k = c.composant(composant)) ref = k->reference;
  }, QStringLiteral("Modèle 3D de %1").arg(QFileInfo(chemin).fileName()));
  if (const ConceptionElec* c = conceptionCourante())
    if (const ComposantElec* k = c->composant(composant)) {
      const HauteurComposant h = hauteurComposant(*c, *k);
      afficherMessage(QStringLiteral("<b>%1</b> prend le modèle %2 : %3 mm, %4.").arg(q(ref).toHtmlEscaped(), QFileInfo(chemin).fileName().toHtmlEscaped(), nombre(h.mm), QString::fromUtf8(nomProvenance(h.provenance))),
                      NiveauMessage::Succes);
    }
  return true;
}

int AtelierElectronique::associerDossierModeles(const QString& dossier) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return 0;
  std::vector<std::string> fichiers;
  for (const QFileInfo& f : QDir(dossier).entryInfoList({QStringLiteral("*.step"), QStringLiteral("*.stp"), QStringLiteral("*.STEP"), QStringLiteral("*.STP"), QStringLiteral("*.iges"), QStringLiteral("*.igs"), QStringLiteral("*.brep")}, QDir::Files))
    fichiers.push_back(f.absoluteFilePath().toStdString());
  const auto paires = rapprocherModeles(*c, fichiers);
  // Chaque fichier lu une seule fois
  std::map<std::string, Modele3D> lus;
  QStringList illisibles;
  for (const auto& [composant, fichier] : paires) {
    if (lus.count(fichier)) continue;
    std::string erreur;
    if (const auto m = prom::lireModele3D(fichier, &erreur)) lus[fichier] = *m;
    else illisibles << QFileInfo(q(fichier)).fileName();
  }
  int n = 0;
  modifierConception([&](ConceptionElec& cc) {
    for (const auto& [composant, fichier] : paires)
      if (const auto it = lus.find(fichier); it != lus.end() && !associerModele(cc, composant, it->second).empty()) ++n;
  }, QStringLiteral("Modèles 3D du dossier %1").arg(QDir(dossier).dirName()));
  afficherMessage(QStringLiteral("<b>%1 composant(s)</b> ont pris leur modèle 3D dans « %2 », calé sur leur empreinte%3.")
                      .arg(n)
                      .arg(QDir(dossier).dirName().toHtmlEscaped())
                      .arg(illisibles.isEmpty() ? QString() : QStringLiteral(" ; illisibles : %1").arg(illisibles.join(QStringLiteral(", ")).toHtmlEscaped())),
                  n > 0 ? NiveauMessage::Succes : NiveauMessage::Information);
  return n;
}

bool AtelierElectronique::exporterStepCarte(const QString& chemin) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return false;
  try {
    prom::exporterStepCarte(*c, chemin.toStdString());
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("STEP non écrit : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  afficherMessage(QStringLiteral("Carte équipée écrite en STEP : un produit nommé par composant."), NiveauMessage::Succes);
  return true;
}

QString AtelierElectronique::exporterImpression(const QString& dossier) {
  const auto pieces = prom::piecesImpression(m_doc->projet());
  QStringList lignes;
  double total = 0;
  for (const auto& x : pieces) {
    const QString f = QDir(dossier).filePath(q(x.nom) + QStringLiteral(".stl"));
    try {
      prom::exporterStl(x.forme, f.toStdString());
    } catch (const std::exception& e) {
      lignes << QStringLiteral("%1 : %2").arg(q(x.nom), QString::fromUtf8(e.what()));
      continue;
    }
    total += x.cout;
    lignes << QStringLiteral("%1 : %2 cm³, %3 g, %4 h, %5 €").arg(q(x.nom), nombre(x.volumeMm3 / 1000, 1), nombre(x.masseG, 1), nombre(x.dureeH, 1), nombre(x.cout, 2));
  }
  const QString texte = lignes.join(QStringLiteral(" ; ")) + QStringLiteral(" — total %1 €").arg(nombre(total, 2));
  afficherMessage(QStringLiteral("<b>Boîtier pour l’impression</b> (%1) : %2").arg(q(m_doc->projet().boitier.impression.matiere).toHtmlEscaped(), texte.toHtmlEscaped()), NiveauMessage::Succes);
  return texte;
}

void AtelierElectronique::ouvrirFacade() {
  if (!m_facade) m_facade = new FenetreFacade(*this, this);
  else m_facade->refaire();
  m_facade->show();
  m_facade->raise();
}

void AtelierElectronique::ouvrirContourEsquisse() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  if (c->esquisseContour.is_null())
    modifierConception([](ConceptionElec& cc) {
      cc.esquisseContour = prom::esquisseVersJson(prom::esquisseDeCarte(cc));
      prom::appliquerEsquisseContour(cc);
    }, QStringLiteral("Contour piloté par une esquisse"));
  if (!m_contourEsquisse) m_contourEsquisse = new DialogueContourEsquisse(*this, this);
  else m_contourEsquisse->refaire();
  m_contourEsquisse->show();
  m_contourEsquisse->raise();
}
