// Prométhée : tableau d'édition des champs des composants. Licence GPL-3.0-only.
#include "TableauChamps.hpp"

#include <algorithm>
#include <map>
#include <set>

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace prom::ecad;

namespace {
enum Colonne { REF, VALEUR, EMPREINTE, FABRICANT, MPN, FOURNISSEUR, DESCRIPTION, FICHE, NPM, HORS_CARTE, LIBRES };
const char* const TITRES[] = {"Référence", "Valeur", "Empreinte", "Fabricant", "Réf. fabricant", "Fournisseur", "Description", "Fiche technique", "Ne pas monter", "Hors carte"};

// Repères triés dans l'ordre naturel : R2 avant R10
class CelluleRepere : public QTableWidgetItem {
 public:
  using QTableWidgetItem::QTableWidgetItem;
  bool operator<(const QTableWidgetItem& o) const override {
    const QString a = text(), b = o.text();
    int i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
      if (a[i].isDigit() && b[j].isDigit()) {
        int fi = i, fj = j;
        while (fi < a.size() && a[fi].isDigit()) ++fi;
        while (fj < b.size() && b[fj].isDigit()) ++fj;
        const qlonglong na = a.mid(i, fi - i).left(15).toLongLong(), nb = b.mid(j, fj - j).left(15).toLongLong();
        if (na != nb) return na < nb;
        i = fi;
        j = fj;
      } else {
        if (a[i] != b[j]) return a[i] < b[j];
        ++i;
        ++j;
      }
    }
    return a.size() - i < b.size() - j;
  }
};
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

TableauChamps::TableauChamps(const ConceptionElec& c, QWidget* parent) : QDialog(parent), m_composants(c.composants) {
  setWindowTitle(QStringLiteral("Champs des composants — %1").arg(q(c.nom)));
  resize(1180, 620);
  std::set<std::string> libres;
  for (const auto& k : m_composants)
    for (const auto& [nom, valeur] : k.champs) libres.insert(nom);
  m_champsLibres.assign(libres.begin(), libres.end());
  auto* col = new QVBoxLayout(this);
  auto* aide = new QLabel(QStringLiteral("Double-clic pour modifier une cellule. Ctrl+C et Ctrl+V copient des blocs de cellules (comme un tableur), "
                                         "Ctrl+D répète la première valeur choisie sur toute la sélection d’une colonne. Un clic sur un titre trie."));
  aide->setWordWrap(true);
  col->addWidget(aide);
  m_filtre = new QLineEdit;
  m_filtre->setPlaceholderText(QStringLiteral("Filtrer : repère, valeur, empreinte, fabricant…"));
  m_filtre->setClearButtonEnabled(true);
  connect(m_filtre, &QLineEdit::textChanged, this, &TableauChamps::filtrer);
  col->addWidget(m_filtre);
  m_table = new QTableWidget;
  m_table->setColumnCount(LIBRES + static_cast<int>(m_champsLibres.size()));
  QStringList titres;
  for (const char* t : TITRES) titres << QString::fromUtf8(t);
  for (const auto& n : m_champsLibres) titres << q(n);
  m_table->setHorizontalHeaderLabels(titres);
  m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_table->setAlternatingRowColors(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  m_table->installEventFilter(this);
  col->addWidget(m_table, 1);
  remplir();
  connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
    if (it && it->column() == REF) majDoubles();
  });
  m_bilan = new QLabel;
  auto* nouveau = new QPushButton(QStringLiteral("＋ Champ…"));
  nouveau->setToolTip(QStringLiteral("Ajouter une colonne de champ libre (tension, tolérance, boîtier…)"));
  connect(nouveau, &QPushButton::clicked, this, [this] {
    bool ok = false;
    const QString nom = QInputDialog::getText(this, QStringLiteral("Nouveau champ"), QStringLiteral("Nom du champ :"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (ok && !nom.isEmpty()) ajouterChamp(nom);
  });
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
  boutons->button(QDialogButtonBox::Apply)->setText(QStringLiteral("Appliquer"));
  boutons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Annuler"));
  connect(boutons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &QDialog::accept);
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  auto* bas = new QHBoxLayout;
  bas->addWidget(nouveau);
  bas->addWidget(m_bilan, 1);
  bas->addWidget(boutons);
  col->addLayout(bas);
  majDoubles();
}

void TableauChamps::remplir() {
  const QSignalBlocker b(m_table);
  m_table->setSortingEnabled(false);
  m_table->setRowCount(static_cast<int>(m_composants.size()));
  for (int r = 0; r < static_cast<int>(m_composants.size()); ++r) {
    const ComposantElec& k = m_composants[static_cast<std::size_t>(r)];
    auto* ref = new CelluleRepere(q(k.reference));
    ref->setData(Qt::UserRole, q(k.uuid));
    m_table->setItem(r, REF, ref);
    const std::string textes[] = {k.valeur, k.empreinte, k.fabricant, k.mpn, k.fournisseur, k.description, k.fiche};
    for (int c = VALEUR; c <= FICHE; ++c) m_table->setItem(r, c, new QTableWidgetItem(q(textes[c - VALEUR])));
    for (int c : {NPM, HORS_CARTE}) {
      auto* it = new QTableWidgetItem;
      it->setFlags((it->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
      it->setCheckState((c == NPM ? k.nePasMonter : k.horsCarte) ? Qt::Checked : Qt::Unchecked);
      m_table->setItem(r, c, it);
    }
    for (std::size_t i = 0; i < m_champsLibres.size(); ++i) {
      const auto f = k.champs.find(m_champsLibres[i]);
      m_table->setItem(r, LIBRES + static_cast<int>(i), new QTableWidgetItem(f == k.champs.end() ? QString() : q(f->second)));
    }
  }
  m_table->setSortingEnabled(true);
  m_table->sortItems(REF);
  m_table->resizeColumnsToContents();
  for (int c = 0; c < m_table->columnCount(); ++c) m_table->setColumnWidth(c, std::min(m_table->columnWidth(c) + 12, 260));
}

void TableauChamps::ajouterChamp(const QString& nom) {
  if (colonne(nom) >= 0) return;
  const QSignalBlocker b(m_table);
  const int c = m_table->columnCount();
  m_table->setColumnCount(c + 1);
  m_table->setHorizontalHeaderItem(c, new QTableWidgetItem(nom));
  for (int r = 0; r < m_table->rowCount(); ++r) m_table->setItem(r, c, new QTableWidgetItem);
  m_champsLibres.push_back(nom.toStdString());
}

int TableauChamps::colonne(const QString& titre) const {
  for (int c = 0; c < m_table->columnCount(); ++c)
    if (m_table->horizontalHeaderItem(c)->text() == titre) return c;
  return -1;
}

int TableauChamps::ligne(const std::string& reference) const {
  for (int r = 0; r < m_table->rowCount(); ++r)
    if (m_table->item(r, REF)->text().toStdString() == reference) return r;
  return -1;
}

bool TableauChamps::ecrire(const std::string& reference, const QString& titre, const QString& valeur) {
  const int r = ligne(reference), c = colonne(titre);
  if (r < 0 || c < 0) return false;
  if (c == NPM || c == HORS_CARTE) m_table->item(r, c)->setCheckState(valeur == QStringLiteral("oui") ? Qt::Checked : Qt::Unchecked);
  else m_table->item(r, c)->setText(valeur);
  return true;
}

void TableauChamps::majDoubles() {
  std::map<QString, int> nombre;
  for (int r = 0; r < m_table->rowCount(); ++r) {
    const QString t = m_table->item(r, REF)->text().trimmed();
    if (!t.isEmpty() && !t.endsWith(QLatin1Char('?'))) ++nombre[t];
  }
  int doubles = 0;
  const QSignalBlocker b(m_table);
  for (int r = 0; r < m_table->rowCount(); ++r) {
    QTableWidgetItem* it = m_table->item(r, REF);
    const bool d = nombre[it->text().trimmed()] > 1;
    doubles += d;
    it->setBackground(d ? QColor(0xFF, 0xD9, 0xD4) : QBrush());
    it->setToolTip(d ? QStringLiteral("Repère en double") : QString());
  }
  if (m_bilan)
    m_bilan->setText(doubles ? QStringLiteral("%1 composant(s) · <span style='color:#C2402E'><b>%2 repère(s) en double</b></span>").arg(m_table->rowCount()).arg(doubles)
                             : QStringLiteral("%1 composant(s)").arg(m_table->rowCount()));
}

void TableauChamps::filtrer(const QString& texte) {
  for (int r = 0; r < m_table->rowCount(); ++r) {
    bool garde = texte.trimmed().isEmpty();
    for (int c = 0; c < m_table->columnCount() && !garde; ++c)
      if (QTableWidgetItem* it = m_table->item(r, c); it && it->text().contains(texte.trimmed(), Qt::CaseInsensitive)) garde = true;
    m_table->setRowHidden(r, !garde);
  }
}

void TableauChamps::copierSelection() const {
  const auto plages = m_table->selectedRanges();
  if (plages.isEmpty()) return;
  const auto& p = plages.front();
  QStringList lignes;
  for (int r = p.topRow(); r <= p.bottomRow(); ++r) {
    if (m_table->isRowHidden(r)) continue;
    QStringList cellules;
    for (int c = p.leftColumn(); c <= p.rightColumn(); ++c) {
      const QTableWidgetItem* it = m_table->item(r, c);
      cellules << (c == NPM || c == HORS_CARTE ? (it->checkState() == Qt::Checked ? QStringLiteral("oui") : QStringLiteral("non")) : it->text());
    }
    lignes << cellules.join(QLatin1Char('\t'));
  }
  QApplication::clipboard()->setText(lignes.join(QLatin1Char('\n')));
}

void TableauChamps::collerTexte(int ligne0, int colonne0, const QString& texte) {
  QStringList lignes = texte.split(QLatin1Char('\n'));
  while (!lignes.isEmpty() && lignes.back().trimmed().isEmpty()) lignes.removeLast();
  // Une seule valeur collée sur une sélection : elle remplit toute la sélection
  const auto plages = m_table->selectedRanges();
  if (lignes.size() == 1 && !lignes.front().contains(QLatin1Char('\t')) && !plages.isEmpty() && (plages.front().rowCount() > 1 || plages.front().columnCount() > 1)) {
    for (const auto& p : plages)
      for (int r = p.topRow(); r <= p.bottomRow(); ++r)
        for (int c = p.leftColumn(); c <= p.rightColumn(); ++c)
          if (!m_table->isRowHidden(r) && c != REF) {
            if (c == NPM || c == HORS_CARTE) m_table->item(r, c)->setCheckState(lignes.front().trimmed() == QStringLiteral("oui") ? Qt::Checked : Qt::Unchecked);
            else m_table->item(r, c)->setText(lignes.front());
          }
    majDoubles();
    return;
  }
  int r = ligne0;
  for (const QString& l : lignes) {
    while (r < m_table->rowCount() && m_table->isRowHidden(r)) ++r;
    if (r >= m_table->rowCount()) break;
    const QStringList cellules = l.split(QLatin1Char('\t'));
    for (int i = 0; i < cellules.size() && colonne0 + i < m_table->columnCount(); ++i) {
      const int c = colonne0 + i;
      QString v = cellules[i];
      v.remove(QLatin1Char('\r'));
      if (c == NPM || c == HORS_CARTE) m_table->item(r, c)->setCheckState(v.trimmed() == QStringLiteral("oui") ? Qt::Checked : Qt::Unchecked);
      else m_table->item(r, c)->setText(v);
    }
    ++r;
  }
  majDoubles();
}

void TableauChamps::repeterSurSelection() {
  const auto plages = m_table->selectedRanges();
  if (plages.isEmpty()) return;
  const auto& p = plages.front();
  for (int c = p.leftColumn(); c <= p.rightColumn(); ++c) {
    const QTableWidgetItem* source = m_table->item(p.topRow(), c);
    for (int r = p.topRow() + 1; r <= p.bottomRow(); ++r) {
      if (m_table->isRowHidden(r) || c == REF) continue;
      if (c == NPM || c == HORS_CARTE) m_table->item(r, c)->setCheckState(source->checkState());
      else m_table->item(r, c)->setText(source->text());
    }
  }
}

bool TableauChamps::eventFilter(QObject* o, QEvent* e) {
  if (o == m_table && e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    if (k->matches(QKeySequence::Copy)) { copierSelection(); return true; }
    if (k->matches(QKeySequence::Paste)) {
      collerTexte(std::max(0, m_table->currentRow()), std::max(0, m_table->currentColumn()), QApplication::clipboard()->text());
      return true;
    }
    if (k->key() == Qt::Key_D && k->modifiers() == Qt::ControlModifier) { repeterSurSelection(); return true; }
  }
  return QDialog::eventFilter(o, e);
}

std::vector<ComposantElec> TableauChamps::composants() const {
  std::vector<ComposantElec> r = m_composants;
  std::map<std::string, ComposantElec*> parUuid;
  for (auto& k : r) parUuid[k.uuid] = &k;
  for (int l = 0; l < m_table->rowCount(); ++l) {
    const auto it = parUuid.find(m_table->item(l, REF)->data(Qt::UserRole).toString().toStdString());
    if (it == parUuid.end()) continue;
    ComposantElec& k = *it->second;
    auto t = [&](int c) { return m_table->item(l, c)->text().trimmed().toStdString(); };
    k.reference = t(REF);
    k.valeur = t(VALEUR);
    k.empreinte = t(EMPREINTE);
    k.fabricant = t(FABRICANT);
    k.mpn = t(MPN);
    k.fournisseur = t(FOURNISSEUR);
    k.description = t(DESCRIPTION);
    k.fiche = t(FICHE);
    k.nePasMonter = m_table->item(l, NPM)->checkState() == Qt::Checked;
    k.horsCarte = m_table->item(l, HORS_CARTE)->checkState() == Qt::Checked;
    for (std::size_t i = 0; i < m_champsLibres.size(); ++i) {
      const std::string v = t(LIBRES + static_cast<int>(i));
      if (v.empty()) k.champs.erase(m_champsLibres[i]);
      else k.champs[m_champsLibres[i]] = v;
    }
  }
  return r;
}
