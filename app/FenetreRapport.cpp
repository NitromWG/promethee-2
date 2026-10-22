// Prométhée : rapport de vérification (lot L5). Licence GPL-3.0-only.
#include "FenetreRapport.hpp"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "PanneauVerifications.hpp"
#include "promethee/fichiers.hpp"

using namespace prom::ecad;

FenetreRapport::FenetreRapport(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreRapport"));
  setWindowTitle(QStringLiteral("Rapport de vérification"));
  resize(860, 640);
  auto* col = new QVBoxLayout(this);
  m_texte = new QTextBrowser;
  m_texte->setObjectName(QStringLiteral("rapportVerification"));
  m_texte->setOpenLinks(false);
  col->addWidget(m_texte, 1);
  auto* ligne = new QHBoxLayout;
  auto* actualiser = new QPushButton(QStringLiteral("Actualiser"));
  auto* html = new QPushButton(QStringLiteral("Enregistrer en HTML…"));
  auto* texte = new QPushButton(QStringLiteral("Enregistrer en texte…"));
  ligne->addWidget(actualiser);
  ligne->addStretch();
  ligne->addWidget(html);
  ligne->addWidget(texte);
  col->addLayout(ligne);
  connect(m_texte, &QTextBrowser::anchorClicked, this, [this](const QUrl& u) { suivre(u); });
  connect(actualiser, &QPushButton::clicked, this, [this] { refaire(); });
  connect(html, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Rapport HTML"), QStringLiteral("verification.html"), QStringLiteral("Pages (*.html)"));
    if (!f.isEmpty()) prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), m_html.toStdString());
  });
  connect(texte, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getSaveFileName(this, QStringLiteral("Rapport texte"), QStringLiteral("verification.txt"), QStringLiteral("Texte (*.txt)"));
    if (const ConceptionElec* c = m_atelier.conceptionCourante(); c && !f.isEmpty())
      prom::ecrireFichierAtomique(prom::cheminFichier(f.toStdString()), rapportVerificationTexte(*c, m_erc, m_drc));
  });
  refaire();
}

void FenetreRapport::refaire() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  const Connectivite k = connectivite(*c);
  m_erc = verifierERC(*c, k, NiveauErc::Complet);
  m_drc = bilanDRC(*c, k, remplirZones(*c, k));
  m_html = QString::fromStdString(rapportVerificationHtml(*c, m_erc, m_drc));
  m_texte->setHtml(m_html);
}

void FenetreRapport::suivre(const QUrl& lien) {
  const QString t = lien.toString();
  const QString prefixe = QStringLiteral("promethee:violation/");
  if (!t.startsWith(prefixe)) return;
  bool ok = false;
  const int n = t.mid(prefixe.size()).toInt(&ok);
  if (!ok || n < 0) return;
  // Le rapport numérote les violations dans leur ordre : celles de l'ERC, puis celles de la DRC
  std::vector<std::pair<Violation, bool>> ordre;
  for (const auto& x : m_erc) ordre.push_back({x, true});
  for (const auto& x : m_drc.actives) ordre.push_back({x, false});
  if (n >= static_cast<int>(ordre.size())) return;
  if (PanneauVerifications* p = m_atelier.verifications()) p->aller(ordre[static_cast<std::size_t>(n)].first, ordre[static_cast<std::size_t>(n)].second);
}
