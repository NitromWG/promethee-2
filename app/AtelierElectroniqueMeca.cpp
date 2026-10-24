// Prométhée : mécatronique dans l'atelier électronique (lot L6) : modèles 3D exacts, dossier de modèles, export STEP
// de la carte équipée, façade, contour piloté par une esquisse, pièces du boîtier pour l'impression 3D.
// Licence GPL-3.0-only.
#include <map>

#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QLocale>
#include <QRegularExpression>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "FenetreFacade.hpp"
#include "FenetresMeca034.hpp"
#include "SelectionElectronique.hpp"
#include "VuePiece.hpp"
#include "promethee/distances3d.hpp"
#include "promethee/idf.hpp"
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

// ---------- 0.34, lot L6 ----------
void AtelierElectronique::importerBoitierCommerce() {
  auto* d = new DialogueBoitierImporte(*this, this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->show();
}

void AtelierElectronique::ajouterPieceMontee() {
  std::string composant;
  for (const auto& id : m_selection->composants()) composant = id;
  auto* d = new DialoguePieceMontee(*this, composant, this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->show();
}

const ApparenceCarte3D& AtelierElectronique::apparence3D() const {
  static const ApparenceCarte3D defaut;
  return m_apparence3D ? *m_apparence3D : defaut;
}

void AtelierElectronique::reglerApparence3D(const ApparenceCarte3D& a) {
  m_apparence3D = std::make_unique<ApparenceCarte3D>(a);
  m_3dAJour = false;
  m_signature3D.clear();
  if (mode() == Vue3D) majVue3D();
}

prom::ecad::PointDeVue AtelierElectronique::pointDeVue3D(const std::string& nom) const {
  prom::ecad::PointDeVue v;
  v.distance = 0;
  if (m_3d) v = m_3d->navigation().capturer(nom);
  return v;
}

void AtelierElectronique::appliquerPointDeVue3D(const prom::ecad::PointDeVue& v) {
  if (m_3d) m_3d->navigation().appliquer(v);
}

void AtelierElectronique::enregistrerPointDeVue3D(const QString& nom) {
  if (!m_3d || nom.trimmed().isEmpty()) return;
  const prom::ecad::PointDeVue v = m_3d->navigation().capturer(nom.trimmed().toStdString());
  modifierConception([&](ConceptionElec& c) {
    std::erase_if(c.pointsDeVue, [&](const prom::ecad::PointDeVue& x) { return x.nom == v.nom; });
    c.pointsDeVue.push_back(v);
  }, QStringLiteral("Point de vue « %1 »").arg(nom.trimmed()));
  afficherMessage(QStringLiteral("Point de vue <b>%1</b> gardé dans le projet.").arg(nom.trimmed().toHtmlEscaped()), NiveauMessage::Succes);
}

QString AtelierElectronique::mesurerCorps3D(const std::string& a, const std::string& b) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return {};
  const prom::MesureCorps m = prom::mesurerCorps(*c, a, b);
  if (!m.mesure.distance) return {};
  const ComposantElec* ka = c->composant(a);
  const ComposantElec* kb = c->composant(b);
  const QString t = QStringLiteral("%1 → %2 : %3 mm (ΔX %4, ΔY %5, ΔZ %6) ; à la carte : %7 et %8 mm")
                        .arg(q(ka ? ka->reference : a), q(kb ? kb->reference : b), nombre(m.mesure.distanceMin, 3), nombre(m.mesure.delta.X(), 3), nombre(-m.mesure.delta.Y(), 3),
                             nombre(m.mesure.delta.Z(), 3), nombre(m.distanceCarteA, 3), nombre(m.distanceCarteB, 3));
  if (m_3d) m_3d->setTracesMesure({{m.mesure.pa, m.mesure.pb, nombre(m.mesure.distanceMin, 3) + QStringLiteral(" mm")}});
  afficherMessage(t.toHtmlEscaped(), NiveauMessage::Information);
  return t;
}

bool AtelierElectronique::exporterIdf(const QString& cheminEmn, bool milliemesPouce) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return false;
  prom::ecad::OptionsIdf o;
  o.milliemesPouce = milliemesPouce;
  o.origineTravail = c->circuit.affichage.origineFabrication;
  const prom::ecad::FichiersIdf f = prom::ecad::ecrireIdf(*c, o);
  QString emp = cheminEmn;
  emp.replace(QRegularExpression(QStringLiteral("\\.emn$"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(".emp"));
  if (emp == cheminEmn) emp += QStringLiteral(".emp");
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(cheminEmn.toStdString()), f.emn);
    prom::ecrireFichierAtomique(prom::cheminFichier(emp.toStdString()), f.emp);
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("IDF non écrit : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  afficherMessage(QStringLiteral("IDF 3.0 écrit : « %1 » et « %2 » (non-montés exclus).").arg(QFileInfo(cheminEmn).fileName().toHtmlEscaped(), QFileInfo(emp).fileName().toHtmlEscaped()), NiveauMessage::Succes);
  return true;
}

bool AtelierElectronique::importerIdf(const QString& cheminEmn) {
  QString emp = cheminEmn;
  emp.replace(QRegularExpression(QStringLiteral("\\.emn$"), QRegularExpression::CaseInsensitiveOption), QStringLiteral(".emp"));
  std::string emn, bib;
  try {
    emn = prom::lireFichier(prom::cheminFichier(cheminEmn.toStdString()));
    if (QFileInfo::exists(emp)) bib = prom::lireFichier(prom::cheminFichier(emp.toStdString()));
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("IDF illisible : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  const prom::ecad::DonneesIdf d = prom::ecad::lireIdf(emn, bib);
  ConceptionElec c = prom::ecad::conceptionDepuisIdf(d);
  m_doc->modifier([&c](prom::Projet& p) { p.electronique.push_back(c); });
  ouvrir(static_cast<int>(m_doc->projet().electronique.size()) - 1, Circuit);
  afficherMessage(QStringLiteral("IDF relu : contour, %1 trou(s), %2 composant(s) à leur hauteur.%3").arg(d.trous.size()).arg(d.composants.size()).arg(d.alertes.empty() ? QString() : QStringLiteral(" ") + q(d.alertes.front()).toHtmlEscaped()),
                  d.alertes.empty() ? NiveauMessage::Succes : NiveauMessage::Avertissement);
  return true;
}

FenetreRendu* AtelierElectronique::rendu() const { return m_rendu; }

void AtelierElectronique::ouvrirRendu() {
  if (!m_rendu) m_rendu = new FenetreRendu(*this, this);
  m_rendu->show();
  m_rendu->raise();
}
