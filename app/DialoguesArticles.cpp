// Prométhée : standardisation et mises à jour des articles (lot L4). Licence GPL-3.0-only.
#include "DialoguesArticles.hpp"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

// ---------- Standardisation ----------
DialogueStandardisation::DialogueStandardisation(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueStandardisation"));
  setWindowTitle(QStringLiteral("Standardiser les composants"));
  setModal(false);
  resize(860, 420);
  auto* col = new QVBoxLayout(this);
  col->addWidget(inspecteur::texte(QStringLiteral("Composants sans article réel : l’article trouvé par la recherche paramétrique, avec sa confiance. Décoche ceux à garder tels quels.")));
  m_table = new QTableWidget(0, 6);
  m_table->setObjectName(QStringLiteral("propositionsStandard"));
  m_table->setHorizontalHeaderLabels({QString(), QStringLiteral("Composant"), QStringLiteral("Aujourd’hui"), QStringLiteral("Article proposé"), QStringLiteral("Confiance"), QStringLiteral("Raison")});
  m_table->verticalHeader()->hide();
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  col->addWidget(m_table, 1);
  auto* ligne = new QHBoxLayout;
  m_remplacer = new QPushButton;
  m_remplacer->setObjectName(QStringLiteral("remplacerStandard"));
  m_bilan = inspecteur::texte();
  ligne->addWidget(m_remplacer);
  ligne->addStretch();
  ligne->addWidget(m_bilan);
  col->addLayout(ligne);
  connect(m_remplacer, &QPushButton::clicked, this, [this] { appliquer(); });
  connect(m_table, &QTableWidget::itemChanged, this, [this] { majBouton(); });
  refaire();
}

void DialogueStandardisation::refaire() {
  m_propositions.clear();
  if (const ConceptionElec* c = m_atelier.conceptionCourante()) {
    const IndexArticles index(m_atelier.articlesDisponibles());
    m_propositions = propositionsStandardisation(*c, index);
  }
  const ConceptionElec* c = m_atelier.conceptionCourante();
  m_table->blockSignals(true);
  m_table->setRowCount(static_cast<int>(m_propositions.size()));
  for (int i = 0; i < static_cast<int>(m_propositions.size()); ++i) {
    const PropositionStandard& p = m_propositions[static_cast<std::size_t>(i)];
    const ComposantElec* k = c ? c->composant(p.composant) : nullptr;
    auto* coche = new QTableWidgetItem;
    coche->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    coche->setCheckState(p.confiance >= 0.3 ? Qt::Checked : Qt::Unchecked);
    m_table->setItem(i, 0, coche);
    m_table->setItem(i, 1, new QTableWidgetItem(k ? q(k->reference) : QString()));
    QString actuel = k ? q(k->valeur) : QString();
    if (k && c->articles.count(k->article) && c->articles.at(k->article).generique) actuel += QStringLiteral(" (générique)");
    m_table->setItem(i, 2, new QTableWidgetItem(actuel));
    m_table->setItem(i, 3, new QTableWidgetItem(q(p.article)));
    m_table->setItem(i, 4, new QTableWidgetItem(QStringLiteral("%1 %").arg(qRound(p.confiance * 100))));
    m_table->setItem(i, 5, new QTableWidgetItem(q(p.raison)));
  }
  m_table->blockSignals(false);
  m_table->resizeColumnsToContents();
  majBouton();
}

void DialogueStandardisation::majBouton() {
  int n = 0;
  for (int i = 0; i < m_table->rowCount(); ++i) n += m_table->item(i, 0) && m_table->item(i, 0)->checkState() == Qt::Checked;
  m_remplacer->setText(n == 1 ? QStringLiteral("Remplacer le composant coché") : QStringLiteral("Remplacer les %1 composants cochés").arg(n));
  m_remplacer->setEnabled(n > 0);
  m_bilan->setText(m_propositions.empty() ? QStringLiteral("Tous les composants ont un article réel.") : QStringLiteral("%1 proposition(s)").arg(m_propositions.size()));
}

int DialogueStandardisation::appliquer() {
  std::vector<PropositionStandard> choisies;
  for (int i = 0; i < m_table->rowCount() && i < static_cast<int>(m_propositions.size()); ++i)
    if (m_table->item(i, 0)->checkState() == Qt::Checked) choisies.push_back(m_propositions[static_cast<std::size_t>(i)]);
  if (choisies.empty()) return 0;
  int n = 0;
  const std::vector<Article> articles = m_atelier.articlesDisponibles();
  m_atelier.modifierConception(
      [&](ConceptionElec& c) {
        const IndexArticles index(articles);
        n = appliquerStandardisation(c, index, choisies);
      },
      QStringLiteral("Standardisation de %1 composant(s)").arg(choisies.size()));
  m_atelier.afficherMessage(QStringLiteral("<b>%1 composant(s)</b> remplacé(s) par leur article.").arg(n), NiveauMessage::Succes);
  refaire();
  return n;
}

// ---------- Mises à jour des articles ----------
DialogueMisesAJour::DialogueMisesAJour(AtelierElectronique& a, QWidget* parent) : QDialog(parent), m_atelier(a) {
  setObjectName(QStringLiteral("dialogueMisesAJour"));
  setWindowTitle(QStringLiteral("Articles modifiés dans la bibliothèque"));
  setModal(false);
  resize(860, 400);
  auto* col = new QVBoxLayout(this);
  col->addWidget(inspecteur::texte(QStringLiteral("Ces articles ont changé dans la bibliothèque depuis leur pose : les différences, puis la mise à jour des composants posés d’après eux.")));
  m_table = new QTableWidget(0, 3);
  m_table->setObjectName(QStringLiteral("misesAJourArticles"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Article"), QStringLiteral("Différences"), QString()});
  m_table->verticalHeader()->hide();
  m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setWordWrap(true);
  col->addWidget(m_table, 1);
  m_bilan = inspecteur::texte();
  col->addWidget(m_bilan);
  refaire();
}

void DialogueMisesAJour::refaire() {
  m_articles.clear();
  m_composants.clear();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (c)
    for (const auto& a : m_atelier.articlesDisponibles()) {
      if (!c->articles.count(a.id)) continue;
      auto ids = composantsAMettreAJour(*c, a);
      if (ids.empty()) continue;
      m_articles.push_back(a);
      m_composants.push_back(std::move(ids));
    }
  m_table->setRowCount(static_cast<int>(m_articles.size()));
  for (int i = 0; i < static_cast<int>(m_articles.size()); ++i) {
    const Article& a = m_articles[static_cast<std::size_t>(i)];
    m_table->setItem(i, 0, new QTableWidgetItem(q(a.nom.empty() ? a.id : a.nom)));
    QStringList lignes;
    if (const ComposantElec* k = c->composant(m_composants[static_cast<std::size_t>(i)].front()))
      for (const auto& d : differencesArticle(*c, *k, a)) lignes << QStringLiteral("%1 : %2 → %3").arg(q(d.champ), q(d.avant.empty() ? "—" : d.avant), q(d.apres.empty() ? "—" : d.apres));
    m_table->setItem(i, 1, new QTableWidgetItem(lignes.join(QLatin1Char('\n'))));
    const int n = static_cast<int>(m_composants[static_cast<std::size_t>(i)].size());
    auto* b = new QPushButton(n == 1 ? QStringLiteral("Mettre à jour le composant") : QStringLiteral("Mettre à jour les %1 composants").arg(n));
    b->setObjectName(QStringLiteral("mettreAJour:") + q(a.id));
    connect(b, &QPushButton::clicked, this, [this, i] { mettreAJour(i); });
    m_table->setCellWidget(i, 2, b);
  }
  m_table->resizeRowsToContents();
  m_table->resizeColumnToContents(2);
  m_bilan->setText(m_articles.empty() ? QStringLiteral("Les composants suivent leurs articles.") : QStringLiteral("%1 article(s) modifié(s)").arg(m_articles.size()));
}

QString DialogueMisesAJour::texteBouton(int ligne) const {
  auto* b = qobject_cast<QPushButton*>(m_table->cellWidget(ligne, 2));
  return b ? b->text() : QString();
}

QString DialogueMisesAJour::differences(int ligne) const { return m_table->item(ligne, 1) ? m_table->item(ligne, 1)->text() : QString(); }

int DialogueMisesAJour::mettreAJour(int ligne) {
  if (ligne < 0 || ligne >= static_cast<int>(m_articles.size())) return 0;
  const Article a = m_articles[static_cast<std::size_t>(ligne)];
  const std::vector<std::string> ids = m_composants[static_cast<std::size_t>(ligne)];
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& c) { n = mettreAJourDepuisArticle(c, a, ids); }, QStringLiteral("Mise à jour de %1 composant(s) depuis %2").arg(ids.size()).arg(q(a.id)));
  m_atelier.afficherMessage(QStringLiteral("<b>%1 composant(s)</b> mis à jour depuis l’article %2.").arg(n).arg(q(a.id).toHtmlEscaped()), NiveauMessage::Succes);
  refaire();
  return n;
}
