// Prométhée : « Enregistrer pour une version précédente… » (lot L1, tâche 1.22). Voir DialogueVersionPrecedente.hpp.
// Licence GPL-3.0-only.
#include "DialogueVersionPrecedente.hpp"

#include <utility>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

DialogueVersionPrecedente::DialogueVersionPrecedente(const prom::Projet& p, QString cheminProjet, QWidget* parent)
    : QDialog(parent), m_projet(p), m_chemin(std::move(cheminProjet)) {
  setObjectName(QStringLiteral("dialogueVersionPrecedente"));
  setWindowTitle(QStringLiteral("Enregistrer pour une version précédente"));
  resize(560, 380);
  auto* col = new QVBoxLayout(this);
  auto* f = new QFormLayout;
  m_version = new QComboBox;
  m_version->setObjectName(QStringLiteral("versionPrecedente"));
  for (const std::string& v : prom::versionsAnterieures()) m_version->addItem(QStringLiteral("Prométhée %1").arg(QString::fromStdString(v)), QString::fromStdString(v));
  f->addRow(QStringLiteral("Pour"), m_version);
  col->addLayout(f);
  m_resume = new QLabel;
  m_resume->setWordWrap(true);
  col->addWidget(m_resume);
  m_pertes = new QListWidget;
  m_pertes->setObjectName(QStringLiteral("pertesVersion"));
  m_pertes->setWordWrap(true);
  m_pertes->setSelectionMode(QAbstractItemView::NoSelection);
  col->addWidget(m_pertes, 1);
  auto* note = new QLabel(QStringLiteral("Le projet ouvert ne change pas : un fichier neuf est écrit à côté."));
  note->setWordWrap(true);
  col->addWidget(note);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  boutons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Choisir le fichier…"));
  connect(boutons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  col->addWidget(boutons);
  connect(m_version, &QComboBox::currentIndexChanged, this, [this] { calculer(); });
  calculer();
}

QString DialogueVersionPrecedente::version() const { return m_version->currentData().toString(); }

void DialogueVersionPrecedente::setVersion(const QString& v) {
  const int i = m_version->findData(v);
  if (i >= 0) m_version->setCurrentIndex(i);
}

// Ce qui se perd, calculé par l'inverse de chaque étape de migration, avant tout fichier écrit
void DialogueVersionPrecedente::calculer() {
  m_fichier = prom::pourVersion(m_projet, version().toStdString());
  m_pertes->clear();
  for (const auto& x : m_fichier->pertes) m_pertes->addItem(QString::fromStdString(x));
  m_pertes->setVisible(!m_fichier->pertes.empty());
  m_resume->setText(m_fichier->pertes.empty() ? QStringLiteral("Rien ne se perd : la %1 relira ce projet tel qu’il est.").arg(version())
                                              : QStringLiteral("La %1 relira ce projet sans ce qui suit :").arg(version()));
}

QStringList DialogueVersionPrecedente::pertes() const {
  QStringList r;
  if (m_fichier)
    for (const auto& x : m_fichier->pertes) r << QString::fromStdString(x);
  return r;
}

QString DialogueVersionPrecedente::cheminPropose() const {
  const QString base = m_chemin.isEmpty() ? QString::fromStdString(m_projet.nom) + QStringLiteral(".prom") : m_chemin;
  return QString::fromStdString(prom::nomPourVersion(base.toStdString(), version().toStdString()));
}

bool DialogueVersionPrecedente::ecrire(const QString& chemin, QString* erreur) const {
  if (!m_fichier) return false;
  try {
    prom::ecrirePourVersion(*m_fichier, chemin.toStdString());
    return true;
  } catch (const std::exception& e) {
    if (erreur) *erreur = QString::fromUtf8(e.what());
    return false;
  }
}
