// Prométhée : lecture d'un schéma QElectroTech. Licence GPL-3.0-only.
#include "Qet.hpp"

#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>

InfosQet lireQet(const QString& chemin) {
  InfosQet r;
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) { r.erreur = QStringLiteral("Fichier introuvable : %1").arg(chemin); return r; }
  QXmlStreamReader x(&f);
  QString folio;
  InfosQet::Appareil courant;
  bool dansElement = false;
  QString nomInfo;
  while (!x.atEnd()) {
    x.readNext();
    if (x.isStartElement()) {
      const auto n = x.name();
      if (n == QLatin1String("project")) r.titre = x.attributes().value(QLatin1String("title")).toString();
      else if (n == QLatin1String("diagram")) {
        folio = x.attributes().value(QLatin1String("title")).toString();
        if (folio.isEmpty()) folio = QStringLiteral("Folio %1").arg(r.folios.size() + 1);
        r.folios << folio;
      } else if (n == QLatin1String("element") && !folio.isNull()) {
        dansElement = true;
        courant = {};
        courant.folio = folio;
      } else if (n == QLatin1String("elementInformation") && dansElement) {
        nomInfo = x.attributes().value(QLatin1String("name")).toString();
        const QString v = x.readElementText().trimmed();
        if (nomInfo == QLatin1String("label") && courant.repere.isEmpty()) courant.repere = v;
        else if ((nomInfo == QLatin1String("designation") || nomInfo == QLatin1String("comment") || nomInfo == QLatin1String("description")) && courant.designation.isEmpty()) courant.designation = v;
      } else if (n == QLatin1String("conductor")) {
        ++r.conducteurs;
      }
    } else if (x.isEndElement() && x.name() == QLatin1String("element") && dansElement) {
      dansElement = false;
      if (!courant.repere.isEmpty()) r.appareils << courant;
    }
  }
  if (x.hasError()) { r.erreur = QStringLiteral("Schéma illisible : %1").arg(x.errorString()); return r; }
  if (r.titre.isEmpty()) r.titre = QFileInfo(chemin).completeBaseName();
  r.lu = true;
  return r;
}

#include <QDesktopServices>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

PageSchema::PageSchema(QWidget* parent) : QWidget(parent) {
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(24, 18, 24, 18);
  m_titre = new QLabel;
  m_titre->setWordWrap(true);
  col->addWidget(m_titre);
  auto* ligne = new QHBoxLayout;
  auto* ouvrir = new QPushButton(QStringLiteral("Ouvrir dans QElectroTech"));
  connect(ouvrir, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_chemin)); });
  auto* relire = new QPushButton(QStringLiteral("Relire le schéma"));
  connect(relire, &QPushButton::clicked, this, [this] { montrer(m_chemin); });
  ligne->addWidget(ouvrir);
  ligne->addWidget(relire);
  ligne->addStretch(1);
  col->addLayout(ligne);
  m_table = new QTableWidget(0, 3);
  m_table->setHorizontalHeaderLabels({QStringLiteral("Repère"), QStringLiteral("Désignation"), QStringLiteral("Folio")});
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_table->verticalHeader()->hide();
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  col->addWidget(m_table, 1);
}

void PageSchema::montrer(const QString& chemin) {
  m_chemin = chemin;
  const InfosQet q = lireQet(chemin);
  if (!q.lu) { m_titre->setText(QStringLiteral("<h2>Schéma électrique</h2><p style='color:#C2402E'>%1</p>").arg(q.erreur.toHtmlEscaped())); m_table->setRowCount(0); return; }
  m_titre->setText(QStringLiteral("<h2>%1</h2><p>Schéma QElectroTech · %2 folio(s) : %3 · %4 appareil(s) · %5 conducteur(s)</p><p style='color:#6B7393'>%6</p>")
                       .arg(q.titre.toHtmlEscaped()).arg(q.folios.size()).arg(q.folios.join(QStringLiteral(", ")).toHtmlEscaped()).arg(q.appareils.size()).arg(q.conducteurs).arg(chemin.toHtmlEscaped()));
  m_table->setRowCount(static_cast<int>(q.appareils.size()));
  for (int i = 0; i < q.appareils.size(); ++i) {
    m_table->setItem(i, 0, new QTableWidgetItem(q.appareils[i].repere));
    m_table->setItem(i, 1, new QTableWidgetItem(q.appareils[i].designation));
    m_table->setItem(i, 2, new QTableWidgetItem(q.appareils[i].folio));
  }
}
