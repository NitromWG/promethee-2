// Prométhée : tableau d'édition des champs des composants. Licence GPL-3.0-only.
#include "TableauChamps.hpp"

#include <algorithm>
#include <map>
#include <set>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "promethee/valeurs.hpp"

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

// Rôles des cellules : les composants d'une ligne (sur la cellule du repère), cellule modifiée par l'utilisateur,
// valeurs différentes dans le groupe
constexpr int ROLE_COMPOSANTS = Qt::UserRole;
constexpr int ROLE_MODIFIEE = Qt::UserRole + 1;
constexpr int ROLE_DIVERS = Qt::UserRole + 2;
const QString DIVERS = QStringLiteral("‹ divers ›");
const QString CLE_VUES = QStringLiteral("electronique/vuesTableauChamps");

QStringList titresDe(std::initializer_list<int> colonnes) {
  QStringList r;
  for (int c : colonnes) r << QString::fromUtf8(TITRES[c]);
  return r;
}

// Vues livrées : les colonnes montrées
const std::vector<std::pair<QString, QStringList>>& vuesLivrees() {
  static const std::vector<std::pair<QString, QStringList>> v{
      {QStringLiteral("Toutes les colonnes"), {}},
      {QStringLiteral("Approvisionnement"), titresDe({REF, VALEUR, FABRICANT, MPN, FOURNISSEUR, NPM})},
      {QStringLiteral("Montage"), titresDe({REF, VALEUR, EMPREINTE, NPM, HORS_CARTE})},
  };
  return v;
}

bool estCase(int c) { return c == NPM || c == HORS_CARTE; }
}  // namespace

TableauChamps::TableauChamps(const ConceptionElec& c, QWidget* parent) : TableauChamps(parent) { recharger(c); }

TableauChamps::TableauChamps(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("tableauChamps"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(4, 4, 4, 4);
  col->setSpacing(4);
  // Une seule ligne d'outils, pour tenir sous le canevas d'un écran de 1 024 px
  auto* haut = new QHBoxLayout;
  m_filtre = new QLineEdit;
  m_filtre->setObjectName(QStringLiteral("filtreChamps"));
  m_filtre->setPlaceholderText(QStringLiteral("Filtrer : repère, valeur, empreinte, fabricant…"));
  m_filtre->setClearButtonEnabled(true);
  connect(m_filtre, &QLineEdit::textChanged, this, &TableauChamps::filtrer);
  haut->addWidget(m_filtre, 1);
  m_choixPortee = new QComboBox;
  m_choixPortee->setObjectName(QStringLiteral("porteeChamps"));
  m_choixPortee->addItems({QStringLiteral("Toute la conception"), QStringLiteral("Cette feuille"), QStringLiteral("La sélection")});
  m_choixPortee->setToolTip(QStringLiteral("Les composants montrés : tous, ceux de la feuille ouverte, ou ceux qui sont choisis"));
  connect(m_choixPortee, &QComboBox::currentIndexChanged, this, [this] { emit porteeChangee(); });
  haut->addWidget(m_choixPortee);
  m_choixVue = new QComboBox;
  m_choixVue->setObjectName(QStringLiteral("vueChamps"));
  m_choixVue->setToolTip(QStringLiteral("Vue : les colonnes montrées. Un clic droit sur les titres montre ou cache une colonne ; « Enregistrer la vue… » la garde sous un nom."));
  connect(m_choixVue, &QComboBox::activated, this, [this](int i) {
    if (m_choixVue->itemData(i).toString() == QStringLiteral("enregistrer")) {
      bool ok = false;
      const QString nom = QInputDialog::getText(this, QStringLiteral("Enregistrer la vue"), QStringLiteral("Nom de la vue :"), QLineEdit::Normal, QString(), &ok).trimmed();
      if (ok && !nom.isEmpty()) enregistrerVue(nom);
      else {
        const QSignalBlocker b(m_choixVue);
        m_choixVue->setCurrentText(m_vue);
      }
      return;
    }
    choisirVue(m_choixVue->itemText(i));
  });
  haut->addWidget(m_choixVue);
  m_regrouper = new QCheckBox(QStringLiteral("Regrouper par valeur"));
  m_regrouper->setObjectName(QStringLiteral("regrouperChamps"));
  m_regrouper->setToolTip(QStringLiteral("Une ligne par valeur et empreinte : ce qu'on y écrit vaut pour tous les composants du groupe"));
  connect(m_regrouper, &QCheckBox::toggled, this, [this] { remplir(); });
  haut->addWidget(m_regrouper);
  auto* nouveau = new QToolButton;
  nouveau->setObjectName(QStringLiteral("ajouterChampLibre"));
  nouveau->setText(QStringLiteral("＋ Champ…"));
  nouveau->setToolTip(QStringLiteral("Ajouter une colonne de champ libre (tension, tolérance, boîtier…)"));
  connect(nouveau, &QToolButton::clicked, this, [this] {
    bool ok = false;
    const QString nom = QInputDialog::getText(this, QStringLiteral("Nouveau champ"), QStringLiteral("Nom du champ :"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (ok && !nom.isEmpty()) ajouterChamp(nom);
  });
  haut->addWidget(nouveau);
  m_bilan = new QLabel;
  m_bilan->setObjectName(QStringLiteral("bilanChamps"));
  haut->addWidget(m_bilan);
  col->addLayout(haut);
  m_table = new QTableWidget;
  m_table->setObjectName(QStringLiteral("tableChamps"));
  m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_table->setAlternatingRowColors(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 6);
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
  m_table->setToolTip(QStringLiteral("Double-clic ou frappe pour modifier : c'est écrit aussitôt. Ctrl+C et Ctrl+V copient des blocs de cellules (comme un tableur), "
                                     "Ctrl+D recopie la première ligne choisie vers le bas. Un clic sur un titre trie."));
  connect(m_table->horizontalHeader(), &QHeaderView::customContextMenuRequested, this, [this](const QPoint& p) {
    QMenu m(this);
    for (int c = 1; c < m_table->columnCount(); ++c) {
      QAction* a = m.addAction(m_table->horizontalHeaderItem(c)->text());
      a->setCheckable(true);
      a->setChecked(!m_table->isColumnHidden(c));
      connect(a, &QAction::toggled, this, [this, c](bool oui) { m_table->setColumnHidden(c, !oui); });
    }
    m.exec(m_table->horizontalHeader()->mapToGlobal(p));
  });
  m_table->installEventFilter(this);
  connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
    if (!it) return;
    {
      const QSignalBlocker b(m_table);
      it->setData(ROLE_MODIFIEE, true);
      it->setData(ROLE_DIVERS, QVariant());
      it->setFont(m_table->font());
      it->setForeground(QBrush());
    }
    if (it->column() == REF) majDoubles();
    signaler();
  });
  col->addWidget(m_table, 1);
  // Vues : les livrées, celles qu'on a enregistrées, puis « Enregistrer la vue… »
  remplirVues();
  choisirVue(vuesLivrees().front().first);
}

void TableauChamps::remplirVues() {
  const QSignalBlocker b(m_choixVue);
  m_choixVue->clear();
  for (const auto& [nom, colonnes] : vuesLivrees()) m_choixVue->addItem(nom);
  const QVariantMap enregistrees = QSettings().value(CLE_VUES).toMap();
  for (auto it = enregistrees.begin(); it != enregistrees.end(); ++it) m_choixVue->addItem(it.key());
  m_choixVue->insertSeparator(m_choixVue->count());
  m_choixVue->addItem(QStringLiteral("Enregistrer la vue…"), QStringLiteral("enregistrer"));
  if (!m_vue.isEmpty()) m_choixVue->setCurrentText(m_vue);
}

QStringList TableauChamps::vues() const {
  QStringList r;
  for (int i = 0; i < m_choixVue->count(); ++i)
    if (!m_choixVue->itemText(i).isEmpty() && m_choixVue->itemData(i).toString() != QStringLiteral("enregistrer")) r << m_choixVue->itemText(i);
  return r;
}

void TableauChamps::choisirVue(const QString& nom) {
  m_vue = nom;
  {
    const QSignalBlocker b(m_choixVue);
    m_choixVue->setCurrentText(nom);
  }
  appliquerVue();
}

void TableauChamps::enregistrerVue(const QString& nom) {
  QStringList montrees;
  for (int c = 0; c < m_table->columnCount(); ++c)
    if (!m_table->isColumnHidden(c)) montrees << m_table->horizontalHeaderItem(c)->text();
  QSettings r;
  QVariantMap enregistrees = r.value(CLE_VUES).toMap();
  enregistrees.insert(nom, montrees);
  r.setValue(CLE_VUES, enregistrees);
  m_vue = nom;
  remplirVues();
}

QString TableauChamps::vue() const { return m_vue; }

void TableauChamps::appliquerVue() {
  QStringList montrees;
  bool trouvee = false;
  for (const auto& [nom, colonnes] : vuesLivrees())
    if (nom == m_vue) {
      montrees = colonnes;
      trouvee = true;
    }
  if (!trouvee) montrees = QSettings().value(CLE_VUES).toMap().value(m_vue).toStringList();
  for (int c = 0; c < m_table->columnCount(); ++c)
    m_table->setColumnHidden(c, c != REF && !montrees.isEmpty() && !montrees.contains(m_table->horizontalHeaderItem(c)->text()));
}

TableauChamps::Portee TableauChamps::portee() const { return static_cast<Portee>(std::max(0, m_choixPortee->currentIndex())); }

void TableauChamps::setPortee(Portee p) { m_choixPortee->setCurrentIndex(static_cast<int>(p)); }

void TableauChamps::setRegroupe(bool oui) { m_regrouper->setChecked(oui); }

bool TableauChamps::regroupe() const { return m_regrouper->isChecked(); }

void TableauChamps::signaler() {
  if (m_signalPrevu) return;
  m_signalPrevu = true;
  // Un collage, un Ctrl+D : toutes leurs cellules partent ensemble, en un pas
  QTimer::singleShot(0, this, [this] {
    m_signalPrevu = false;
    emit modifie();
  });
}

std::vector<std::vector<std::string>> TableauChamps::lignesVoulues() const {
  std::vector<const ComposantElec*> montres;
  for (const auto& k : m_composants)
    if (!m_porteeActive || m_portee.count(k.uuid)) montres.push_back(&k);
  std::sort(montres.begin(), montres.end(), [](const auto* a, const auto* b) { return ordreNaturel(a->reference, b->reference); });
  std::vector<std::vector<std::string>> r;
  if (!regroupe()) {
    for (const auto* k : montres) r.push_back({k->uuid});
    return r;
  }
  std::map<std::string, std::size_t> groupe;
  for (const auto* k : montres) {
    const auto f = m_cles.find(k->uuid);
    const std::string cle = f == m_cles.end() ? k->uuid : f->second;
    const auto [it, nouveau] = groupe.try_emplace(cle, r.size());
    if (nouveau) r.emplace_back();
    r[it->second].push_back(k->uuid);
  }
  return r;
}

void TableauChamps::recharger(const ConceptionElec& c, const std::set<std::string>* portee) {
  m_composants = c.composants;
  m_porteeActive = portee != nullptr;
  m_portee = portee ? *portee : std::set<std::string>{};
  m_titre = q(c.nom);
  // Clés de regroupement : la valeur comprise (10k = 10 kΩ = 10000), l'empreinte, la référence fabricant
  m_cles.clear();
  for (const auto& k : m_composants)
    m_cles[k.uuid] = cleValeur(lireValeurComposant(k.valeur, familleDe(c, k)), k.valeur) + '\x1f' + k.empreinte + '\x1f' + k.mpn;
  std::set<std::string> libres(m_champsAjoutes.begin(), m_champsAjoutes.end());
  for (const auto& k : m_composants)
    for (const auto& [nom, valeur] : k.champs) libres.insert(nom);
  const std::vector<std::string> champs(libres.begin(), libres.end());
  // Mêmes lignes et mêmes colonnes : les cellules se mettent à jour en place (le tri, la sélection et le défilement
  // restent) ; sinon le tableau se refait
  if (champs != m_champsLibres || lignesVoulues() != m_lignes) {
    m_champsLibres = champs;
    remplir();
    return;
  }
  majCellules();
}

void TableauChamps::remplir() {
  m_lignes = lignesVoulues();
  const QSignalBlocker b(m_table);
  const int triColonne = m_table->horizontalHeader()->sortIndicatorSection();
  const Qt::SortOrder triOrdre = m_table->horizontalHeader()->sortIndicatorOrder();
  m_table->setSortingEnabled(false);
  m_table->clear();
  m_table->setColumnCount(LIBRES + static_cast<int>(m_champsLibres.size()));
  QStringList titres;
  for (const char* t : TITRES) titres << QString::fromUtf8(t);
  for (const auto& n : m_champsLibres) titres << q(n);
  m_table->setHorizontalHeaderLabels(titres);
  m_table->setRowCount(static_cast<int>(m_lignes.size()));
  for (int r = 0; r < static_cast<int>(m_lignes.size()); ++r) {
    auto* ref = new CelluleRepere;
    QStringList ids;
    for (const auto& id : m_lignes[static_cast<std::size_t>(r)]) ids << q(id);
    ref->setData(ROLE_COMPOSANTS, ids);
    if (ids.size() > 1) ref->setFlags(ref->flags() & ~Qt::ItemIsEditable);  // plusieurs repères : on ne les renomme pas ici
    m_table->setItem(r, REF, ref);
    for (int c = VALEUR; c < m_table->columnCount(); ++c) {
      auto* it = new QTableWidgetItem;
      if (estCase(c)) it->setFlags((it->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
      m_table->setItem(r, c, it);
    }
  }
  majCellules();
  m_table->setSortingEnabled(true);
  m_table->sortItems(triColonne >= 0 && triColonne < m_table->columnCount() ? triColonne : REF, triOrdre);
  m_table->resizeColumnsToContents();
  for (int c = 0; c < m_table->columnCount(); ++c) m_table->setColumnWidth(c, std::min(m_table->columnWidth(c) + 12, 220));
  appliquerVue();
  filtrer(m_filtre->text());
}

void TableauChamps::majCellules() {
  std::map<std::string, const ComposantElec*> parUuid;
  for (const auto& k : m_composants) parUuid[k.uuid] = &k;
  const QSignalBlocker b(m_table);
  const bool tri = m_table->isSortingEnabled();
  m_table->setSortingEnabled(false);
  QFont italique = m_table->font();
  italique.setItalic(true);
  for (int r = 0; r < m_table->rowCount(); ++r) {
    std::vector<const ComposantElec*> ks;
    for (const QString& id : m_table->item(r, REF)->data(ROLE_COMPOSANTS).toStringList())
      if (const auto f = parUuid.find(id.toStdString()); f != parUuid.end()) ks.push_back(f->second);
    if (ks.empty()) continue;
    QStringList refs;
    for (const auto* k : ks) refs << q(k->reference);
    QTableWidgetItem* ref = m_table->item(r, REF);
    ref->setText(refs.join(QStringLiteral(", ")));
    ref->setData(ROLE_MODIFIEE, QVariant());
    if (ks.size() > 1) ref->setToolTip(QStringLiteral("%1 composants").arg(ks.size()));
    for (int c = VALEUR; c < m_table->columnCount(); ++c) {
      QTableWidgetItem* it = m_table->item(r, c);
      it->setData(ROLE_MODIFIEE, QVariant());
      auto texte = [&](const ComposantElec& k) -> std::string {
        switch (c) {
          case VALEUR: return k.valeur;
          case EMPREINTE: return k.empreinte;
          case FABRICANT: return k.fabricant;
          case MPN: return k.mpn;
          case FOURNISSEUR: return k.fournisseur;
          case DESCRIPTION: return k.description;
          case FICHE: return k.fiche;
          case NPM: return k.nePasMonter ? "oui" : "non";
          case HORS_CARTE: return k.horsCarte ? "oui" : "non";
          default: {
            const auto f = k.champs.find(m_champsLibres[static_cast<std::size_t>(c - LIBRES)]);
            return f == k.champs.end() ? std::string() : f->second;
          }
        }
      };
      const std::string v = texte(*ks.front());
      bool divers = false;
      for (const auto* k : ks) divers |= texte(*k) != v;
      if (estCase(c)) {
        it->setCheckState(divers ? Qt::PartiallyChecked : v == "oui" ? Qt::Checked : Qt::Unchecked);
        continue;
      }
      it->setText(divers ? DIVERS : q(v));
      it->setData(ROLE_DIVERS, divers ? QVariant(true) : QVariant());
      it->setFont(divers ? italique : m_table->font());
      it->setForeground(divers ? palette().color(QPalette::PlaceholderText) : QBrush());
    }
  }
  m_table->setSortingEnabled(tri);
  majDoubles();
}

void TableauChamps::ajouterChamp(const QString& nom) {
  if (colonne(nom) >= 0) return;
  const QSignalBlocker b(m_table);
  const int c = m_table->columnCount();
  m_table->setColumnCount(c + 1);
  m_table->setHorizontalHeaderItem(c, new QTableWidgetItem(nom));
  for (int r = 0; r < m_table->rowCount(); ++r) m_table->setItem(r, c, new QTableWidgetItem);
  m_champsLibres.push_back(nom.toStdString());
  m_champsAjoutes.insert(nom.toStdString());
}

int TableauChamps::colonne(const QString& titre) const {
  for (int c = 0; c < m_table->columnCount(); ++c)
    if (m_table->horizontalHeaderItem(c)->text() == titre) return c;
  return -1;
}

int TableauChamps::ligne(const std::string& reference) const {
  for (int r = 0; r < m_table->rowCount(); ++r)
    for (const QString& ref : m_table->item(r, REF)->text().split(QStringLiteral(", ")))
      if (ref.toStdString() == reference) return r;
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
  // Les repères de toute la conception comptent, même hors de la portée montrée ; ceux de la ligne en cours de saisie
  // remplacent ceux du modèle
  std::map<std::string, std::string> repere;
  for (const auto& k : m_composants) repere[k.uuid] = k.reference;
  for (int r = 0; r < m_table->rowCount(); ++r) {
    const QStringList ids = m_table->item(r, REF)->data(ROLE_COMPOSANTS).toStringList();
    if (ids.size() == 1) repere[ids.front().toStdString()] = m_table->item(r, REF)->text().trimmed().toStdString();
  }
  std::map<std::string, int> nombre;
  for (const auto& [id, t] : repere)
    if (!t.empty() && t.back() != '?') ++nombre[t];
  int doubles = 0, composants = 0;
  const QSignalBlocker b(m_table);
  for (int r = 0; r < m_table->rowCount(); ++r) {
    QTableWidgetItem* it = m_table->item(r, REF);
    const QStringList ids = it->data(ROLE_COMPOSANTS).toStringList();
    composants += static_cast<int>(ids.size());
    bool d = false;
    for (const QString& id : ids) d |= nombre[repere[id.toStdString()]] > 1;
    doubles += d;
    it->setBackground(d ? QColor(0xFF, 0xD9, 0xD4) : QBrush());
    if (d) it->setToolTip(QStringLiteral("Repère en double"));
  }
  if (!m_bilan) return;
  const QString lignes = regroupe() ? QStringLiteral("%1 composant(s) en %2 ligne(s)").arg(composants).arg(m_table->rowCount()) : QStringLiteral("%1 composant(s)").arg(composants);
  m_bilan->setText(doubles ? lignes + QStringLiteral(" · <span style='color:#C2402E'><b>%1 repère(s) en double</b></span>").arg(doubles) : lignes);
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
          if (!m_table->isRowHidden(r) && c != REF && !m_table->isColumnHidden(c)) {
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
    for (int i = 0, c = colonne0; i < cellules.size() && c < m_table->columnCount(); ++i, ++c) {
      while (c < m_table->columnCount() && m_table->isColumnHidden(c)) ++c;  // les colonnes cachées ne prennent rien
      if (c >= m_table->columnCount()) break;
      if (c == REF && !(m_table->item(r, c)->flags() & Qt::ItemIsEditable)) continue;
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
  return QWidget::eventFilter(o, e);
}

std::vector<ComposantElec> TableauChamps::composants() const {
  std::vector<ComposantElec> r = m_composants;
  std::map<std::string, ComposantElec*> parUuid;
  for (auto& k : r) parUuid[k.uuid] = &k;
  // Seules les cellules modifiées s'écrivent : une ligne regroupée n'écrase pas les valeurs diverses de son groupe
  for (int l = 0; l < m_table->rowCount(); ++l) {
    const QStringList ids = m_table->item(l, REF)->data(ROLE_COMPOSANTS).toStringList();
    for (const QString& id : ids) {
      const auto it = parUuid.find(id.toStdString());
      if (it == parUuid.end()) continue;
      ComposantElec& k = *it->second;
      for (int c = 0; c < m_table->columnCount(); ++c) {
        const QTableWidgetItem* cel = m_table->item(l, c);
        if (!cel || !cel->data(ROLE_MODIFIEE).toBool()) continue;
        if (c == REF && ids.size() > 1) continue;
        const std::string t = cel->text().trimmed().toStdString();
        switch (c) {
          case REF: k.reference = t; break;
          case VALEUR: k.valeur = t; break;
          case EMPREINTE: k.empreinte = t; break;
          case FABRICANT: k.fabricant = t; break;
          case MPN: k.mpn = t; break;
          case FOURNISSEUR: k.fournisseur = t; break;
          case DESCRIPTION: k.description = t; break;
          case FICHE: k.fiche = t; break;
          case NPM:
            if (cel->checkState() != Qt::PartiallyChecked) k.nePasMonter = cel->checkState() == Qt::Checked;
            break;
          case HORS_CARTE:
            if (cel->checkState() != Qt::PartiallyChecked) k.horsCarte = cel->checkState() == Qt::Checked;
            break;
          default: {
            const std::string& nom = m_champsLibres[static_cast<std::size_t>(c - LIBRES)];
            if (t.empty()) k.champs.erase(nom);
            else k.champs[nom] = t;
          }
        }
      }
    }
  }
  return r;
}
