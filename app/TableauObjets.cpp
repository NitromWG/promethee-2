// Prométhée : tableau de tous les objets de la conception (lot L3, tâche 3.21). Voir TableauObjets.hpp.
// Licence GPL-3.0-only.
#include "TableauObjets.hpp"

#include <algorithm>
#include <set>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"
#include "SelectionElectronique.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
constexpr int COL_GENRE = 0, COL_PARENT = 1, COL_PROPRIETES = 2;
bool auSchema(GenreObjet g) { return g == GenreObjet::Fil || g == GenreObjet::Etiquette || g == GenreObjet::TexteSchema; }
bool aLaCarte(GenreObjet g) { return g == GenreObjet::Piste || g == GenreObjet::Via || g == GenreObjet::Zone || g == GenreObjet::TexteCarte; }
}  // namespace

TableauObjets::TableauObjets(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("tableauObjets"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(4, 4, 4, 4);
  col->setSpacing(4);
  auto* haut = new QHBoxLayout;
  m_filtre = new QLineEdit;
  m_filtre->setObjectName(QStringLiteral("filtreObjets"));
  m_filtre->setPlaceholderText(QStringLiteral("Filtrer : repère, réseau, texte, feuille…"));
  m_filtre->setClearButtonEnabled(true);
  connect(m_filtre, &QLineEdit::textChanged, this, &TableauObjets::filtrer);
  haut->addWidget(m_filtre, 1);
  m_genres = new QComboBox;
  m_genres->setObjectName(QStringLiteral("genresObjets"));
  m_genres->addItems({QStringLiteral("Tous les objets"), QStringLiteral("Composants et leurs enfants"), QStringLiteral("Schéma : fils, étiquettes, textes"), QStringLiteral("Carte : pistes, vias, zones, textes")});
  connect(m_genres, &QComboBox::currentIndexChanged, this, &TableauObjets::filtrer);
  haut->addWidget(m_genres);
  col->addLayout(haut);
  // Règle appliquée aux cellules choisies
  auto* regle = new QHBoxLayout;
  m_regle = new QComboBox;
  m_regle->setObjectName(QStringLiteral("regleObjets"));
  m_regle->addItem(QStringLiteral("Formule en x"), static_cast<int>(RegleTexte::Genre::Formule));
  m_regle->addItem(QStringLiteral("Remplacer"), static_cast<int>(RegleTexte::Genre::Remplacer));
  m_regle->addItem(QStringLiteral("Préfixe"), static_cast<int>(RegleTexte::Genre::Prefixe));
  m_regle->addItem(QStringLiteral("Suffixe"), static_cast<int>(RegleTexte::Genre::Suffixe));
  regle->addWidget(m_regle);
  m_de = new QLineEdit;
  m_de->setObjectName(QStringLiteral("regleObjetsDe"));
  m_de->setPlaceholderText(QStringLiteral("de"));
  m_a = new QLineEdit;
  m_a->setObjectName(QStringLiteral("regleObjetsA"));
  m_a->setPlaceholderText(QStringLiteral("x + 2,54"));
  auto majRegle = [this] {
    const auto g = static_cast<RegleTexte::Genre>(m_regle->currentData().toInt());
    m_de->setVisible(g == RegleTexte::Genre::Remplacer);
    m_a->setPlaceholderText(g == RegleTexte::Genre::Formule ? QStringLiteral("x + 2,54") : g == RegleTexte::Genre::Remplacer ? QStringLiteral("à") : QStringLiteral("texte"));
  };
  connect(m_regle, &QComboBox::currentIndexChanged, this, majRegle);
  regle->addWidget(m_de, 1);
  regle->addWidget(m_a, 1);
  auto* appliquer = new QPushButton(QStringLiteral("Appliquer aux cellules choisies"));
  appliquer->setObjectName(QStringLiteral("appliquerRegleObjets"));
  connect(appliquer, &QPushButton::clicked, this, [this] {
    appliquerRegle(static_cast<RegleTexte::Genre>(m_regle->currentData().toInt()), m_de->text(), m_a->text());
  });
  regle->addWidget(appliquer);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanObjets"));
  regle->addWidget(m_bilan, 1);
  col->addLayout(regle);
  majRegle();
  m_table = new QTableWidget;
  m_table->setObjectName(QStringLiteral("tableObjets"));
  m_table->setColumnCount(COL_PROPRIETES + NOMBRE_PROPRIETES_OBJET);
  QStringList titres{QStringLiteral("Objet"), QStringLiteral("Parent")};
  for (int p = 0; p < NOMBRE_PROPRIETES_OBJET; ++p) titres << QString::fromUtf8(nomProprieteObjet(static_cast<ProprieteObjet>(p)));
  m_table->setHorizontalHeaderLabels(titres);
  m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_table->setAlternatingRowColors(true);
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 6);
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->installEventFilter(this);
  m_table->setToolTip(QStringLiteral("Écrire une cellule, coller un bloc venu d’un tableur (Ctrl+V), ou appliquer une règle aux cellules choisies : chaque fois un seul pas d’annulation"));
  connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
    if (!it || it->column() < COL_PROPRIETES) return;
    m_enAttente.emplace_back(it->row(), it->column());
    if (m_ecriturePrevue) return;
    m_ecriturePrevue = true;
    QTimer::singleShot(0, this, [this] { ecrireEnAttente(); });
  });
  connect(m_table, &QTableWidget::currentCellChanged, this, [this](int l, int, int ancienne, int) {
    if (l != ancienne) choisirObjet(l);
  });
  col->addWidget(m_table, 1);
  recharger();
}

int TableauObjets::colonne(ProprieteObjet p) { return COL_PROPRIETES + static_cast<int>(p); }

int TableauObjets::ligne(GenreObjet g, const std::string& id) const {
  for (std::size_t i = 0; i < m_lignes.size(); ++i)
    if (m_lignes[i].genre == g && m_lignes[i].id == id) return static_cast<int>(i);
  return -1;
}

void TableauObjets::recharger() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  std::vector<LigneObjet> nouvelles = c ? objetsConception(*c) : std::vector<LigneObjet>{};
  bool memes = nouvelles.size() == m_lignes.size();
  for (std::size_t i = 0; memes && i < nouvelles.size(); ++i) memes = nouvelles[i].genre == m_lignes[i].genre && nouvelles[i].id == m_lignes[i].id;
  m_lignes = std::move(nouvelles);
  // Mêmes objets : les cellules changent en place (sélection et défilement gardés)
  if (memes && m_table->rowCount() == static_cast<int>(m_lignes.size())) majCellules();
  else remplir();
}

void TableauObjets::remplir() {
  const QSignalBlocker b(m_table);
  m_table->setRowCount(static_cast<int>(m_lignes.size()));
  for (int r = 0; r < m_table->rowCount(); ++r)
    for (int col = 0; col < m_table->columnCount(); ++col) m_table->setItem(r, col, new QTableWidgetItem);
  majCellules();
  m_table->resizeColumnsToContents();
  for (int col = 0; col < m_table->columnCount(); ++col) m_table->setColumnWidth(col, std::min(m_table->columnWidth(col) + 12, 200));
  filtrer();
}

void TableauObjets::majCellules() {
  const QSignalBlocker b(m_table);
  for (int r = 0; r < static_cast<int>(m_lignes.size()); ++r) {
    const LigneObjet& l = m_lignes[static_cast<std::size_t>(r)];
    QTableWidgetItem* genre = m_table->item(r, COL_GENRE);
    genre->setText((l.profondeur > 0 ? QStringLiteral("    ↳ ") : QString()) + QString::fromUtf8(nomGenreObjet(l.genre)));
    genre->setFlags(genre->flags() & ~Qt::ItemIsEditable);
    QTableWidgetItem* parent = m_table->item(r, COL_PARENT);
    parent->setText(q(l.parent));
    parent->setFlags(parent->flags() & ~Qt::ItemIsEditable);
    for (int p = 0; p < NOMBRE_PROPRIETES_OBJET; ++p) {
      const auto prop = static_cast<ProprieteObjet>(p);
      QTableWidgetItem* it = m_table->item(r, colonne(prop));
      const auto t = l.textes.find(prop);
      it->setText(t == l.textes.end() ? QString() : q(t->second));
      const bool modifiable = l.modifiables.count(prop) > 0;
      it->setFlags(modifiable ? (it->flags() | Qt::ItemIsEditable) : (it->flags() & ~Qt::ItemIsEditable));
      it->setForeground(modifiable ? QBrush() : palette().color(QPalette::PlaceholderText));
      if (proprieteNumerique(prop)) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
  }
}

void TableauObjets::filtrer() {
  const QString f = m_filtre->text().trimmed();
  const int g = m_genres->currentIndex();
  for (int r = 0; r < m_table->rowCount() && r < static_cast<int>(m_lignes.size()); ++r) {
    const GenreObjet genre = m_lignes[static_cast<std::size_t>(r)].genre;
    bool garde = g == 0 || (g == 1 && !auSchema(genre) && !aLaCarte(genre)) || (g == 2 && auSchema(genre)) || (g == 3 && aLaCarte(genre));
    if (garde && !f.isEmpty()) {
      garde = false;
      for (int col = 0; col < m_table->columnCount() && !garde; ++col) garde = m_table->item(r, col)->text().contains(f, Qt::CaseInsensitive);
    }
    m_table->setRowHidden(r, !garde);
  }
}

void TableauObjets::ecrire(int ligne, int col, const QString& texte) {
  if (QTableWidgetItem* it = m_table->item(ligne, col); it && (it->flags() & Qt::ItemIsEditable)) it->setText(texte);
}

void TableauObjets::collerTexte(int ligne0, int colonne0, const QString& texte) {
  QStringList lignes = texte.split(QLatin1Char('\n'));
  while (!lignes.isEmpty() && lignes.back().trimmed().isEmpty()) lignes.removeLast();
  // Une seule valeur collée sur une sélection : toute la sélection la prend
  const auto plages = m_table->selectedRanges();
  if (lignes.size() == 1 && !lignes.front().contains(QLatin1Char('\t')) && !plages.isEmpty() && (plages.front().rowCount() > 1 || plages.front().columnCount() > 1)) {
    for (const auto& p : plages)
      for (int r = p.topRow(); r <= p.bottomRow(); ++r)
        for (int c = p.leftColumn(); c <= p.rightColumn(); ++c)
          if (!m_table->isRowHidden(r)) ecrire(r, c, lignes.front().trimmed());
    return;
  }
  int r = ligne0;
  for (const QString& l : lignes) {
    while (r < m_table->rowCount() && m_table->isRowHidden(r)) ++r;
    if (r >= m_table->rowCount()) break;
    const QStringList cellules = l.split(QLatin1Char('\t'));
    for (int i = 0; i < cellules.size() && colonne0 + i < m_table->columnCount(); ++i) {
      QString v = cellules[i];
      v.remove(QLatin1Char('\r'));
      ecrire(r, colonne0 + i, v.trimmed());
    }
    ++r;
  }
}

int TableauObjets::appliquerRegle(RegleTexte::Genre genre, const QString& de, const QString& a) {
  const RegleTexte regle{genre, de.toStdString(), a.toStdString()};
  int n = 0, refus = 0;
  std::string erreur;
  for (QTableWidgetItem* it : m_table->selectedItems()) {
    if (!it || it->column() < COL_PROPRIETES || !(it->flags() & Qt::ItemIsEditable) || m_table->isRowHidden(it->row())) continue;
    const auto prop = static_cast<ProprieteObjet>(it->column() - COL_PROPRIETES);
    std::string e;
    if (const auto t = transformerCellule(it->text().toStdString(), prop, regle, &e)) {
      if (q(*t) != it->text()) {
        it->setText(q(*t));
        ++n;
      }
    } else {
      ++refus;
      erreur = e;
    }
  }
  if (refus) m_bilan->setText(QStringLiteral("<span style='color:%1'>%2 cellule(s) refusée(s) : %3</span>").arg(teintes::erreur()).arg(refus).arg(q(erreur).toHtmlEscaped()));
  else if (n == 0) m_bilan->setText(QStringLiteral("Aucune cellule modifiable choisie, ou rien à changer."));
  return n;
}

QString TableauObjets::bilan() const { return m_bilan->text(); }

void TableauObjets::ecrireEnAttente() {
  m_ecriturePrevue = false;
  if (m_enAttente.empty()) return;
  struct Ecriture {
    LigneObjet ligne;
    ProprieteObjet propriete;
    std::string texte;
  };
  std::vector<Ecriture> ecritures;
  std::set<std::pair<int, int>> vues;
  for (const auto& [r, col] : std::exchange(m_enAttente, {})) {
    if (r < 0 || r >= static_cast<int>(m_lignes.size()) || !vues.insert({r, col}).second) continue;
    ecritures.push_back({m_lignes[static_cast<std::size_t>(r)], static_cast<ProprieteObjet>(col - COL_PROPRIETES), m_table->item(r, col)->text().toStdString()});
  }
  int faites = 0;
  std::vector<std::string> erreurs;
  m_atelier.modifierConception([&](ConceptionElec& c) {
    for (const auto& e : ecritures) {
      std::string erreur;
      if (ecrireProprieteObjet(c, e.ligne, e.propriete, e.texte, &erreur)) ++faites;
      else {
        const auto nom = e.ligne.textes.find(ProprieteObjet::Nom);
        erreurs.push_back(std::string(nomGenreObjet(e.ligne.genre)) + (nom == e.ligne.textes.end() ? std::string() : " " + nom->second) + " : " + erreur);
      }
    }
  }, ecritures.size() == 1 ? QStringLiteral("Tableau des objets : %1").arg(QString::fromUtf8(nomProprieteObjet(ecritures.front().propriete))) : QStringLiteral("Tableau des objets : %1 cellules").arg(ecritures.size()));
  if (!erreurs.empty())
    m_bilan->setText(QStringLiteral("<span style='color:%1'>%2 cellule(s) refusée(s) : %3</span>").arg(teintes::erreur()).arg(erreurs.size()).arg(q(erreurs.front()).toHtmlEscaped()));
  else
    m_bilan->setText(QStringLiteral("%1 cellule(s) écrite(s), en un pas d’annulation.").arg(faites));
  // Les cellules refusées reprennent la valeur du modèle
  recharger();
}

void TableauObjets::choisirObjet(int l) {
  if (l < 0 || l >= static_cast<int>(m_lignes.size()) || !m_atelier.selection()) return;
  const LigneObjet& o = m_lignes[static_cast<std::size_t>(l)];
  using G = SelectionElectronique::Genre;
  SelectionElectronique::Ref r;
  switch (o.genre) {
    case GenreObjet::Composant: {
      // Le premier symbole posé du composant
      const ConceptionElec* c = m_atelier.conceptionCourante();
      if (!c) return;
      for (const auto& f : c->feuilles)
        for (const auto& s : f.symboles)
          if (s.composant == o.id && r.id.empty()) r = {G::Symbole, s.uuid, o.id};
      if (r.id.empty()) return;
      break;
    }
    case GenreObjet::Symbole: {
      const ConceptionElec* c = m_atelier.conceptionCourante();
      std::string composant;
      if (c)
        for (const auto& f : c->feuilles)
          for (const auto& s : f.symboles)
            if (s.uuid == o.id) composant = s.composant;
      r = {G::Symbole, o.id, composant};
      break;
    }
    case GenreObjet::Empreinte: r = {G::Empreinte, o.id, o.id}; break;
    case GenreObjet::Fil: r = {G::Fil, o.id, {}}; break;
    case GenreObjet::Etiquette: r = {G::Etiquette, o.id, {}}; break;
    case GenreObjet::TexteSchema: r = {G::TexteSchema, o.id, {}}; break;
    case GenreObjet::Piste: r = {G::Piste, o.id, {}}; break;
    case GenreObjet::Via: r = {G::Via, o.id, {}}; break;
    case GenreObjet::Zone: r = {G::Zone, o.id, {}}; break;
    case GenreObjet::TexteCarte: r = {G::TexteCarte, o.id, {}}; break;
  }
  m_atelier.selection()->choisir({r});
}

bool TableauObjets::eventFilter(QObject* o, QEvent* e) {
  if (o == m_table && e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    if (k->matches(QKeySequence::Copy)) {
      const auto plages = m_table->selectedRanges();
      if (plages.isEmpty()) return true;
      const auto& p = plages.front();
      QStringList lignes;
      for (int r = p.topRow(); r <= p.bottomRow(); ++r) {
        if (m_table->isRowHidden(r)) continue;
        QStringList cellules;
        for (int c = p.leftColumn(); c <= p.rightColumn(); ++c) cellules << m_table->item(r, c)->text();
        lignes << cellules.join(QLatin1Char('\t'));
      }
      QApplication::clipboard()->setText(lignes.join(QLatin1Char('\n')));
      return true;
    }
    if (k->matches(QKeySequence::Paste)) {
      collerTexte(std::max(0, m_table->currentRow()), std::max(0, m_table->currentColumn()), QApplication::clipboard()->text());
      return true;
    }
  }
  return QWidget::eventFilter(o, e);
}
