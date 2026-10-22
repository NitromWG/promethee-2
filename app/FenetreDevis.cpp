// Prométhée : devis hors ligne (lot L8). Licence GPL-3.0-only.
#include "FenetreDevis.hpp"

#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"

using namespace prom::ecad;

FenetreDevis::FenetreDevis(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreDevis"));
  setWindowTitle(QStringLiteral("Devis de fabrication"));
  resize(640, 360);
  auto* col = new QVBoxLayout(this);
  auto* f = new QFormLayout;
  m_quantite = new QSpinBox;
  m_quantite->setObjectName(QStringLiteral("quantiteDevis"));
  m_quantite->setRange(1, 100000);
  m_quantite->setValue(10);
  f->addRow(QStringLiteral("Cartes"), m_quantite);
  col->addLayout(f);
  m_table = new QTableWidget(4, 0);
  m_table->setObjectName(QStringLiteral("tableDevis"));
  m_table->setVerticalHeaderLabels({QStringLiteral("Prix"), QStringLiteral("Délai"), QStringLiteral("Grille du"), QStringLiteral("Détails")});
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  col->addWidget(m_table, 1);
  auto* note = new QLabel(QStringLiteral("Prix publics relevés à la date de chaque grille, hors port et taxes ; calculés ici, sans réseau. "
                                         "La commande se passe ensuite sur la page du fabricant, avec l’archive publiée."));
  note->setWordWrap(true);
  col->addWidget(note);
  connect(m_quantite, &QSpinBox::valueChanged, this, [this] { refaire(); });
  refaire();
}

void FenetreDevis::refaire() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  m_offres = devis(*c, m_quantite->value());
  m_table->setColumnCount(static_cast<int>(m_offres.size()));
  QStringList titres;
  const QLocale l;
  for (std::size_t i = 0; i < m_offres.size(); ++i) {
    const auto& o = m_offres[i];
    titres << QString::fromStdString(o.fabricant);
    const int k = static_cast<int>(i);
    m_table->setItem(0, k, new QTableWidgetItem(QStringLiteral("%1 %2").arg(l.toString(o.prix, 'f', 2), QString::fromStdString(o.devise))));
    m_table->setItem(1, k, new QTableWidgetItem(QStringLiteral("%1 jours").arg(l.toString(o.delaiJours, 'f', 0))));
    m_table->setItem(2, k, new QTableWidgetItem(QString::fromStdString(o.date)));
    QStringList d;
    for (const auto& x : o.details) d << QString::fromStdString(x);
    m_table->setItem(3, k, new QTableWidgetItem(d.join(QStringLiteral(" ; "))));
  }
  m_table->setHorizontalHeaderLabels(titres);
}
