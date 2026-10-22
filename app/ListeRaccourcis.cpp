// Prométhée : liste des raccourcis montrée par F1 (lot L1, tâche 1.8). Voir ListeRaccourcis.hpp.
// Licence GPL-3.0-only.
#include "ListeRaccourcis.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "RegistreActions.hpp"

ListeRaccourcis::ListeRaccourcis(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("listeRaccourcis"));
  setWindowTitle(QStringLiteral("Raccourcis de l’atelier"));
  setModal(false);
  resize(560, 640);
  auto* col = new QVBoxLayout(this);
  m_filtre = new QLineEdit;
  m_filtre->setObjectName(QStringLiteral("filtreRaccourcis"));
  m_filtre->setPlaceholderText(QStringLiteral("Chercher une action ou une touche"));
  m_filtre->setClearButtonEnabled(true);
  col->addWidget(m_filtre);
  m_arbre = new QTreeWidget;
  m_arbre->setColumnCount(2);
  m_arbre->setHeaderLabels({QStringLiteral("Action"), QStringLiteral("Touches")});
  m_arbre->setRootIsDecorated(false);
  m_arbre->setSelectionMode(QAbstractItemView::NoSelection);
  m_arbre->setFocusPolicy(Qt::NoFocus);
  m_arbre->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_arbre->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_arbre->header()->setStretchLastSection(false);
  col->addWidget(m_arbre, 1);
  m_note = new QLabel;
  m_note->setObjectName(QStringLiteral("noteRaccourcis"));
  m_note->setWordWrap(true);
  m_note->setTextFormat(Qt::RichText);
  m_note->hide();
  col->addWidget(m_note);
  connect(m_filtre, &QLineEdit::textChanged, this, &ListeRaccourcis::filtrer);
}

void ListeRaccourcis::setNote(const QString& html) {
  m_note->setText(html);
  m_note->setVisible(!html.isEmpty());
}

QString ListeRaccourcis::note() const { return m_note->text(); }

void ListeRaccourcis::montrer(const std::vector<Section>& sections) {
  const RegistreActions& r = RegistreActions::atelierElectronique();
  m_arbre->clear();
  for (const Section& s : sections) {
    if (s.actions.empty()) continue;
    auto* titre = new QTreeWidgetItem(m_arbre, {s.titre});
    titre->setFirstColumnSpanned(true);
    QFont f = titre->font(0);
    f.setBold(true);
    titre->setFont(0, f);
    titre->setFlags(Qt::ItemIsEnabled);
    for (const ActionAtelier* a : s.actions) {
      auto* l = new QTreeWidgetItem(titre, {a->libelle, r.touches(a->identifiant)});
      if (!a->souris.isEmpty()) l->setToolTip(0, QStringLiteral("À la souris : ") + a->souris);
      l->setData(0, Qt::UserRole, QString::fromStdString(a->identifiant));
      l->setToolTip(0, QString::fromStdString(a->identifiant));
    }
    titre->setExpanded(true);
  }
  filtrer(m_filtre->text());
  show();
  raise();
  activateWindow();
  m_filtre->setFocus(Qt::OtherFocusReason);
}

QStringList ListeRaccourcis::lignes(const QString& section) const {
  QStringList r;
  for (int i = 0; i < m_arbre->topLevelItemCount(); ++i) {
    const QTreeWidgetItem* t = m_arbre->topLevelItem(i);
    if (!t->text(0).startsWith(section)) continue;
    for (int j = 0; j < t->childCount(); ++j) r << t->child(j)->text(0) + QStringLiteral(" · ") + t->child(j)->text(1);
  }
  return r;
}

void ListeRaccourcis::filtrer(const QString& texte) {
  const QString t = texte.trimmed();
  for (int i = 0; i < m_arbre->topLevelItemCount(); ++i) {
    QTreeWidgetItem* s = m_arbre->topLevelItem(i);
    int visibles = 0;
    for (int j = 0; j < s->childCount(); ++j) {
      QTreeWidgetItem* l = s->child(j);
      const bool garde = t.isEmpty() || l->text(0).contains(t, Qt::CaseInsensitive) || l->text(1).contains(t, Qt::CaseInsensitive);
      l->setHidden(!garde);
      visibles += garde;
    }
    s->setHidden(visibles == 0);
  }
}
