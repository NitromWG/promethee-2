// Prométhée : exports de l'atelier électronique (lot L1, tâche 1.15, A1) : dossier de fabrication, dessins du
// schéma et du plan de montage, typon. Le reste de l'atelier est dans AtelierElectronique.cpp.
// Licence GPL-3.0-only.
#include "AtelierElectronique.hpp"

#include <cctype>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPushButton>
#include <QPainterPath>
#include <QPdfWriter>

#include "Document.hpp"
#include "VueSchema.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/version.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

namespace {
// Nom de fichier tiré du nom de la conception, comme le fait le dossier de fabrication
QString baseFichier(const std::string& nom) {
  QString r;
  for (unsigned char ch : nom) r += std::isalnum(ch) || ch == '-' || ch == '_' ? QChar(ch) : QChar('_');
  return r.isEmpty() ? QStringLiteral("carte") : r;
}
}  // namespace

// Les dessins (schéma, plan de montage) rejoignent le dossier : il est complet pour l'atelier de câblage
QStringList AtelierElectronique::ajouterDessins(const QString& dossier) {
  QStringList r;
  const ConceptionElec* c = conceptionCourante();
  if (!c) return r;
  const QString base = baseFichier(c->nom);
  QDir().mkpath(QDir(dossier).filePath(QStringLiteral("Documentation")));
  QDir().mkpath(QDir(dossier).filePath(QStringLiteral("Assemblage")));
  const QString schema = QStringLiteral("Documentation/") + base + QStringLiteral("-schema.pdf");
  const QString montage = QStringLiteral("Assemblage/") + base + QStringLiteral("-plan-de-montage.pdf");
  if (exporterDessin(QDir(dossier).filePath(schema))) r << schema;
  if (exporterPlanMontage(QDir(dossier).filePath(montage))) r << montage;
  return r;
}

QStringList AtelierElectronique::exporterFabrication(const QString& dossier) {
  QStringList r;
  const ConceptionElec* c = conceptionCourante();
  if (!c) return r;
  try {
    for (const auto& f : ecrireDossierFabrication(*c, dossier.toStdString(), prom::infosVersion().identifiant)) r << q(f);
    r << ajouterDessins(dossier);
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("%1").arg(QString::fromUtf8(e.what())), NiveauMessage::Erreur);
  }
  return r;
}

bool AtelierElectronique::exporterDessin(const QString& chemin) { return m_schema->exporterPdf(chemin); }
bool AtelierElectronique::exporterPlanMontage(const QString& chemin) { return m_circuit->exporterPlanMontage(chemin); }

void AtelierElectronique::exporterFabricationInteractif() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier de fabrication"));
  if (d.isEmpty()) return;
  const QStringList f = exporterFabrication(QDir(d).filePath(q(c->nom).replace(QLatin1Char('/'), QLatin1Char('_')) + QStringLiteral(" - fabrication")));
  if (!f.isEmpty()) afficherMessage(QStringLiteral("<b>Dossier de fabrication écrit</b> : %1 fichiers.").arg(f.size()), NiveauMessage::Succes);
}

bool AtelierElectronique::exporterTypon(const QString& chemin, const OptionsTypon& o) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return false;
  const Connectivite k = connectivite(*c);
  const ModeleTypon m = modeleTypon(*c, k, remplirZones(*c, k), o);
  const std::string titre = c->nom + " — révision " + c->cartouche.revision;
  if (chemin.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
    try {
      prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), svgTypon(m, o, titre));
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }
  QPdfWriter pdf(chemin);
  pdf.setResolution(1200);
  pdf.setTitle(QString::fromStdString(titre) + QStringLiteral(" — typon ") + QString::fromUtf8(codeCouche(o.couche)));
  pdf.setCreator(QStringLiteral("Prométhée"));
  // Page à la taille du typon : rien ne peut le réduire « pour tenir dans la page »
  pdf.setPageLayout(QPageLayout(QPageSize(QSizeF(m.largeur, m.hauteur), QPageSize::Millimeter), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0)));
  QPainter g;
  if (!g.begin(&pdf)) return false;
  g.setRenderHint(QPainter::Antialiasing);
  const double u = g.viewport().width() / m.largeur;  // points par mm
  auto P = [u](Pt p) { return QPointF(mm(p.x) * u, mm(p.y) * u); };
  const QColor encre = o.negatif ? Qt::white : Qt::black, fond = o.negatif ? Qt::black : Qt::white;
  const double hc = m.hauteurCarte;
  g.fillRect(QRectF(0, 0, m.largeur * u, hc * u), fond);
  // Chaque région en un tracé orienté : les trous des zones restent vides
  for (const auto& region : m.cuivre) {
    QPainterPath x;
    x.setFillRule(Qt::WindingFill);
    for (const auto& poly : region) {
      if (poly.size() < 3) continue;
      x.moveTo(P(poly.front()));
      for (std::size_t i = 1; i < poly.size(); ++i) x.lineTo(P(poly[i]));
      x.closeSubpath();
    }
    g.fillPath(x, encre);
  }
  for (const auto& t : m.traits) {
    g.setPen(QPen(encre, mm(t.largeur) * u, Qt::SolidLine, Qt::RoundCap));
    g.drawLine(P(t.a), P(t.b));
  }
  g.setPen(Qt::NoPen);
  g.setBrush(fond);
  for (const auto& [p, d] : m.trous) g.drawEllipse(P(p), mm(d) / 2 * u, mm(d) / 2 * u);
  g.setBrush(Qt::NoBrush);
  g.setPen(QPen(QColor(0x80, 0x80, 0x80), 0.1 * u));
  for (const Polygone* poly : std::vector<const Polygone*>{&m.contour}) {
    QPolygonF x;
    for (const Pt& p : *poly) x << P(p);
    g.drawPolygon(x);
  }
  for (const auto& d : m.decoupes) {
    QPolygonF x;
    for (const Pt& p : d) x << P(p);
    g.drawPolygon(x);
  }
  // Règle de contrôle : 50 mm entre les traits extrêmes
  g.setPen(QPen(Qt::black, 0.2 * u));
  const double y = (hc + 4) * u;
  g.drawLine(QPointF(2 * u, y), QPointF(52 * u, y));
  for (int k2 = 0; k2 <= 50; k2 += 10) g.drawLine(QPointF((2 + k2) * u, y - 1.5 * u), QPointF((2 + k2) * u, y + 1.5 * u));
  QFont f(QStringLiteral("Sans"));
  f.setPixelSize(static_cast<int>(2 * u));
  g.setFont(f);
  g.drawText(QPointF(2 * u, y + 6 * u), QStringLiteral("%1 · %2%3%4 · 50 mm entre les traits : imprime à 100 %")
                                           .arg(QString::fromStdString(titre), QString::fromUtf8(codeCouche(o.couche)), m.miroir ? QStringLiteral(" · miroir") : QString(),
                                                o.negatif ? QStringLiteral(" · négatif") : QString()));
  g.end();
  return true;
}

void AtelierElectronique::exporterTyponInteractif() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("Typon de gravure"));
  auto* fl = new QFormLayout(&dlg);
  auto* couche = new QComboBox;
  for (Couche x : couchesCuivre(c->circuit.empilage.couchesCuivre))
    if (x == Couche::CuivreDessus || x == Couche::CuivreDessous) couche->addItem(QString::fromUtf8(nomCouche(x)), static_cast<int>(x));
  couche->setCurrentIndex(couche->count() - 1);
  fl->addRow(QStringLiteral("Couche"), couche);
  auto* miroir = new QCheckBox(QStringLiteral("Miroir pour le transfert de toner (face imprimée contre le cuivre)"));
  fl->addRow(miroir);
  auto* negatif = new QCheckBox(QStringLiteral("Négatif (résine négative)"));
  fl->addRow(negatif);
  auto* centrage = new QDoubleSpinBox;
  centrage->setRange(0, 1.5);
  centrage->setDecimals(2);
  centrage->setSingleStep(0.1);
  centrage->setValue(0.6);
  centrage->setSuffix(QStringLiteral(" mm"));
  centrage->setSpecialValueText(QStringLiteral("trous réels"));
  fl->addRow(QStringLiteral("Point de centrage des perçages"), centrage);
  auto* aide = new QLabel(QStringLiteral("La face dessous se lit côté cuivre, donc déjà en miroir. Imprime à 100 % et vérifie la règle de 50 mm."));
  aide->setWordWrap(true);
  fl->addRow(aide);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  boutons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Enregistrer…"));
  boutons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(boutons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  fl->addRow(boutons);
  if (dlg.exec() != QDialog::Accepted) return;
  OptionsTypon o;
  o.couche = static_cast<Couche>(couche->currentData().toInt());
  o.miroirTransfert = miroir->isChecked();
  o.negatif = negatif->isChecked();
  o.trouCentrage = centrage->value();
  const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Typon"), q(c->nom) + QStringLiteral(" - typon ") + QString::fromUtf8(codeCouche(o.couche)) + QStringLiteral(".pdf"),
                                                 QStringLiteral("PDF (*.pdf);;SVG (*.svg)"));
  if (f.isEmpty()) return;
  if (exporterTypon(f, o)) afficherMessage(QStringLiteral("<b>Typon enregistré</b> à l’échelle 1 : imprime-le à 100 %."), NiveauMessage::Succes);
  else afficherMessage(QStringLiteral("Typon non écrit."), NiveauMessage::Erreur);
}
