// Prométhée : fabrication et cartes complexes dans l'atelier électronique (lot L8) : gestionnaire de contraintes,
// panneau en languettes, devis hors ligne, contrôle des fichiers avant l'envoi, visionneuse HTML.
// Licence GPL-3.0-only.
#include <algorithm>

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "FenetreDevis.hpp"
#include "GestionnaireContraintes.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/version.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
void montrer(QWidget* w) {
  w->show();
  w->raise();
  w->activateWindow();
}
// Liste de lignes dans une petite fenêtre (défauts du panneau, écarts des fichiers)
void listeEnFenetre(QWidget* parent, const QString& titre, const QString& texte, const QStringList& lignes) {
  auto* d = new QDialog(parent);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->setWindowTitle(titre);
  auto* col = new QVBoxLayout(d);
  auto* t = new QLabel(texte);
  t->setWordWrap(true);
  col->addWidget(t);
  auto* l = new QListWidget;
  l->addItems(lignes);
  l->setWordWrap(true);
  col->addWidget(l);
  auto* ok = new QDialogButtonBox(QDialogButtonBox::Close);
  QObject::connect(ok, &QDialogButtonBox::rejected, d, &QDialog::close);
  col->addWidget(ok);
  d->resize(720, 360);
  d->show();
}
}  // namespace

GestionnaireContraintes* AtelierElectronique::contraintes() const { return m_contraintes; }
FenetreDevis* AtelierElectronique::fenetreDevis() const { return m_devis; }

void AtelierElectronique::ouvrirContraintes() {
  if (!conceptionCourante()) return;
  if (!m_contraintes) m_contraintes = new GestionnaireContraintes(*this, this);
  else m_contraintes->refaire();
  montrer(m_contraintes);
}

void AtelierElectronique::ouvrirDevis() {
  if (!conceptionCourante()) return;
  if (!m_devis) m_devis = new FenetreDevis(*this, this);
  else m_devis->refaire();
  montrer(m_devis);
}

QStringList AtelierElectronique::ecrirePanneau(const QString& dossier, const OptionsPanneau& o, QStringList* defauts) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return {};
  const PanneauFabrication p = panneauFabrication(*c, o);
  if (defauts) {
    defauts->clear();
    for (const auto& x : p.defauts) *defauts << q(x);
  }
  if (!p.defauts.empty()) return {};
  QDir().mkpath(dossier);
  QStringList ecrits;
  const std::string base = prom::nomFichierAscii(c->nom, "carte") + "-panneau";
  auto ecrire = [&](const std::string& nom, const std::string& contenu) {
    const QString f = QDir(dossier).filePath(q(nom));
    prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), contenu);
    ecrits << f;
  };
  for (const auto& cf : p.modele.couches) {
    std::string code = codeCouche(cf.couche);
    std::replace(code.begin(), code.end(), '.', '_');
    ecrire(base + "-" + code + ".gbr", gerber(p.modele, cf.couche, prom::infosVersion().identifiant));
  }
  ecrire(base + "-PTH.drl", excellon(p.modele, true, prom::infosVersion().identifiant));
  ecrire(base + "-NPTH.drl", excellon(p.modele, false, prom::infosVersion().identifiant));
  afficherMessage(QStringLiteral("<b>Panneau</b> %1 × %2 écrit : %3 fichier(s), %4 languette(s), %5 perforation(s).")
                      .arg(o.colonnes).arg(o.lignes).arg(ecrits.size()).arg(p.languettes).arg(p.perforations),
                  NiveauMessage::Succes);
  return ecrits;
}

void AtelierElectronique::exporterPanneau() {
  if (!conceptionCourante()) return;
  QDialog d(this);
  d.setWindowTitle(QStringLiteral("Mise en panneau"));
  auto* f = new QFormLayout(&d);
  auto* colonnes = new QSpinBox;
  colonnes->setRange(1, 20);
  colonnes->setValue(3);
  auto* lignes = new QSpinBox;
  lignes->setRange(1, 20);
  lignes->setValue(2);
  auto* ecart = new QDoubleSpinBox;
  ecart->setRange(1, 10);
  ecart->setValue(2);
  ecart->setSuffix(QStringLiteral(" mm"));
  auto* rail = new QDoubleSpinBox;
  rail->setRange(0, 20);
  rail->setValue(5);
  rail->setSuffix(QStringLiteral(" mm"));
  f->addRow(QStringLiteral("Colonnes"), colonnes);
  f->addRow(QStringLiteral("Lignes"), lignes);
  f->addRow(QStringLiteral("Écart fraisé"), ecart);
  f->addRow(QStringLiteral("Cadre"), rail);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(boutons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  f->addRow(boutons);
  if (d.exec() != QDialog::Accepted) return;
  const QString dossier = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier du panneau"));
  if (dossier.isEmpty()) return;
  OptionsPanneau o;
  o.colonnes = colonnes->value();
  o.lignes = lignes->value();
  o.ecart = ecart->value();
  o.rail = rail->value();
  QStringList defauts;
  if (ecrirePanneau(dossier, o, &defauts).isEmpty() && !defauts.isEmpty())
    listeEnFenetre(this, QStringLiteral("Panneau refusé"), QStringLiteral("<b>Le panneau ne sort pas</b> : son contrôle a trouvé %1 défaut(s).").arg(defauts.size()), defauts);
}

QStringList AtelierElectronique::controlerFichiers(const QString& dossier) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return {};
  QStringList r;
  for (const auto& x : controlerDossierFabrication(*c, dossier.toStdString())) r << q(x);
  return r;
}

void AtelierElectronique::controlerFichiersEcrits() {
  const QString dossier = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier de fabrication à contrôler"));
  if (dossier.isEmpty()) return;
  const QStringList d = controlerFichiers(dossier);
  if (d.isEmpty()) {
    afficherMessage(QStringLiteral("<b>Fichiers contrôlés</b> : relus, ils disent la même chose que la conception ; ils peuvent partir."), NiveauMessage::Succes);
    return;
  }
  afficherMessage(QStringLiteral("<b style='color:#c53030'>Fichiers à revoir</b> : %1 écart(s) avec la conception.").arg(d.size()), NiveauMessage::Erreur);
  listeEnFenetre(this, QStringLiteral("Fichiers de fabrication"), QStringLiteral("<b>Ne pas envoyer</b> : les fichiers relus diffèrent de la conception."), d);
}

bool AtelierElectronique::exporterVisionneuse(const QString& chemin) {
  const ConceptionElec* c = conceptionCourante();
  if (!c || chemin.isEmpty()) return false;
  try {
    prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), visionneuseHtml(*c));
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("Visionneuse non écrite : %1").arg(q(e.what()).toHtmlEscaped()), NiveauMessage::Erreur);
    return false;
  }
  afficherMessage(QStringLiteral("<b>Visionneuse</b> écrite : une page qui s’ouvre sans réseau ni installation."), NiveauMessage::Succes);
  return true;
}
