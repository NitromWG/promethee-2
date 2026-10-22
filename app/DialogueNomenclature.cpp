// Prométhée : nomenclature d'un assemblage (M10). Licence GPL-3.0-only.
#include "DialogueNomenclature.hpp"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Document.hpp"
#include "Icones.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/structure.hpp"

using namespace prom;

namespace {
QString cleVue(VueNomenclature v) {
  return v == VueNomenclature::Pieces ? QStringLiteral("pieces") : v == VueNomenclature::PremierNiveau ? QStringLiteral("premier") : QStringLiteral("niveaux");
}
}  // namespace

DialogueNomenclature::DialogueNomenclature(Document* doc, int assemblage, std::function<MassePiece(int)> masse, QWidget* parent)
    : QDialog(parent), m_doc(doc), m_assemblage(assemblage), m_masse(std::move(masse)) {
  setWindowTitle(QStringLiteral("Nomenclature"));
  setObjectName(QStringLiteral("dialogueNomenclature"));
  auto* col = new QVBoxLayout(this);
  auto* haut = new QHBoxLayout;
  const auto& p = m_doc->projet();
  const QString nom = rangAssemblageValide(p, assemblage) ? QString::fromStdString(p.assemblages[static_cast<std::size_t>(assemblage)].nom) : QString::fromStdString(p.nom);
  haut->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(nom.toHtmlEscaped())), 1);
  m_choixVue = new QComboBox;
  m_choixVue->setObjectName(QStringLiteral("vueNomenclature"));
  m_choixVue->addItem(QStringLiteral("À niveaux (arbre)"), static_cast<int>(VueNomenclature::Niveaux));
  m_choixVue->addItem(QStringLiteral("Premier niveau"), static_cast<int>(VueNomenclature::PremierNiveau));
  m_choixVue->addItem(QStringLiteral("Pièces seules"), static_cast<int>(VueNomenclature::Pieces));
  haut->addWidget(m_choixVue);
  m_boutonColonnes = new QToolButton;
  m_boutonColonnes->setObjectName(QStringLiteral("colonnesNomenclature"));
  m_boutonColonnes->setText(QStringLiteral("Colonnes"));
  m_boutonColonnes->setPopupMode(QToolButton::InstantPopup);
  m_boutonColonnes->setMenu(new QMenu(m_boutonColonnes));
  haut->addWidget(m_boutonColonnes);
  col->addLayout(haut);
  m_arbre = new QTreeWidget;
  m_arbre->setObjectName(QStringLiteral("tableNomenclature"));
  m_arbre->setRootIsDecorated(true);
  m_arbre->setAlternatingRowColors(true);
  m_arbre->setUniformRowHeights(true);
  col->addWidget(m_arbre, 1);
  auto* bas = new QHBoxLayout;
  m_total = new QLabel;
  m_total->setObjectName(QStringLiteral("totalNomenclature"));
  bas->addWidget(m_total, 1);
  auto* exporterBouton = new QPushButton(icone(QStringLiteral("exporter")), QStringLiteral("Exporter…"));
  exporterBouton->setToolTip(QStringLiteral("Classeur Excel (XLSX) ou CSV, avec les colonnes affichées"));
  connect(exporterBouton, &QPushButton::clicked, this, [this, nom] {
    QString filtre;
    const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter la nomenclature"), nom + QStringLiteral(" - nomenclature.xlsx"),
                                                        QStringLiteral("Classeur Excel (*.xlsx);;CSV (*.csv)"), &filtre);
    if (chemin.isEmpty()) return;
    QString c = chemin;
    const QString s = QFileInfo(c).suffix().toLower();
    if (s != QLatin1String("xlsx") && s != QLatin1String("csv")) c += filtre.contains(QLatin1String("csv")) ? QStringLiteral(".csv") : QStringLiteral(".xlsx");
    QString err;
    if (!exporter(c, &err)) QMessageBox::warning(this, QStringLiteral("Exporter la nomenclature"), err);
  });
  bas->addWidget(exporterBouton);
  auto* fermer = new QPushButton(QStringLiteral("Fermer"));
  connect(fermer, &QPushButton::clicked, this, &QDialog::accept);
  bas->addWidget(fermer);
  col->addLayout(bas);

  const QSettings reglages;
  const int vue = reglages.value(QStringLiteral("nomenclature/vue"), static_cast<int>(VueNomenclature::Niveaux)).toInt();
  m_vue = vue == static_cast<int>(VueNomenclature::Pieces) ? VueNomenclature::Pieces : vue == static_cast<int>(VueNomenclature::PremierNiveau) ? VueNomenclature::PremierNiveau : VueNomenclature::Niveaux;
  m_choixVue->setCurrentIndex(std::max(0, m_choixVue->findData(static_cast<int>(m_vue))));
  connect(m_choixVue, &QComboBox::activated, this, [this] { setVue(static_cast<VueNomenclature>(m_choixVue->currentData().toInt())); });
  setVue(m_vue);
  resize(820, 440);
}

void DialogueNomenclature::setVue(VueNomenclature vue) {
  m_vue = vue;
  if (m_choixVue->currentData().toInt() != static_cast<int>(vue)) m_choixVue->setCurrentIndex(std::max(0, m_choixVue->findData(static_cast<int>(vue))));
  QSettings reglages;
  reglages.setValue(QStringLiteral("nomenclature/vue"), static_cast<int>(vue));
  // Colonnes retenues pour cette vue, sinon celles par défaut
  const QStringList gardees = reglages.value(QStringLiteral("nomenclature/colonnes/") + cleVue(vue)).toStringList();
  m_colonnes.clear();
  for (const auto& c : gardees) m_colonnes.push_back(c.toStdString());
  if (m_colonnes.empty()) m_colonnes = colonnesParDefaut(vue);
  majMenuColonnes();
  actualiser();
}

void DialogueNomenclature::setColonnes(const std::vector<std::string>& colonnes) {
  m_colonnes = colonnes;
  QStringList l;
  for (const auto& c : colonnes) l << QString::fromStdString(c);
  QSettings().setValue(QStringLiteral("nomenclature/colonnes/") + cleVue(m_vue), l);
  majMenuColonnes();
  actualiser();
}

void DialogueNomenclature::majMenuColonnes() {
  QMenu* menu = m_boutonColonnes->menu();
  menu->clear();
  for (const auto& c : colonnesNomenclature(m_doc->projet())) {
    QAction* a = menu->addAction(QString::fromStdString(c.titre));
    a->setCheckable(true);
    a->setChecked(std::find(m_colonnes.begin(), m_colonnes.end(), c.cle) != m_colonnes.end());
    a->setData(QString::fromStdString(c.cle));
    connect(a, &QAction::toggled, this, [this, cle = c.cle](bool oui) {
      std::vector<std::string> cols = m_colonnes;
      const auto it = std::find(cols.begin(), cols.end(), cle);
      if (oui && it == cols.end()) {
        // Place dans l'ordre des colonnes proposées
        const auto toutes = colonnesNomenclature(m_doc->projet());
        std::vector<std::string> rangees;
        for (const auto& x : toutes) if (x.cle == cle || std::find(cols.begin(), cols.end(), x.cle) != cols.end()) rangees.push_back(x.cle);
        cols = rangees;
      } else if (!oui && it != cols.end()) {
        cols.erase(it);
      }
      if (!cols.empty()) setColonnes(cols);
    });
  }
  menu->addSeparator();
  menu->addAction(QStringLiteral("Colonnes par défaut"), this, [this] { setColonnes(colonnesParDefaut(m_vue)); });
}

void DialogueNomenclature::actualiser() {
  m_tableau = tableauNomenclature(m_doc->projet(), m_assemblage, m_vue, m_colonnes, m_masse);
  m_arbre->clear();
  QStringList titres;
  for (const auto& c : m_tableau.colonnes) titres << QString::fromStdString(c.titre);
  m_arbre->setColumnCount(static_cast<int>(titres.size()));
  m_arbre->setHeaderLabels(titres);
  std::vector<QTreeWidgetItem*> parents;  // dernier élément posé à chaque niveau
  for (std::size_t i = 0; i < m_tableau.lignes.size(); ++i) {
    QStringList v;
    for (const auto& x : m_tableau.lignes[i]) v << QString::fromStdString(x.texte);
    auto* it = new QTreeWidgetItem(v);
    for (int c = 0; c < static_cast<int>(m_tableau.colonnes.size()); ++c)
      if (m_tableau.colonnes[static_cast<std::size_t>(c)].numerique) it->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
    if (m_tableau.assemblages[i]) {
      QFont f = it->font(0);
      f.setBold(true);
      for (int c = 0; c < v.size(); ++c) it->setFont(c, f);
      it->setIcon(0, icone(QStringLiteral("assemblage")));
    } else {
      it->setIcon(0, icone(QStringLiteral("piece")));
    }
    const int niveau = m_tableau.niveaux[i];
    parents.resize(static_cast<std::size_t>(niveau));
    if (niveau > 0 && !parents.empty() && parents.back()) parents.back()->addChild(it);
    else m_arbre->addTopLevelItem(it);
    parents.push_back(it);
  }
  m_arbre->expandAll();
  for (int c = 0; c < m_arbre->columnCount(); ++c) m_arbre->resizeColumnToContents(c);
  m_total->setText(QStringLiteral("<b>Total</b> : %1").arg(QString::fromStdString(m_tableau.texteTotal()).toHtmlEscaped()));
}

bool DialogueNomenclature::exporter(const QString& chemin, QString* erreur) {
  const bool xlsx = QFileInfo(chemin).suffix().toLower() == QLatin1String("xlsx");
  try {
    ecrireFichierAtomique(cheminFichier(chemin.toStdString()), xlsx ? xlsxNomenclature(m_tableau) : csvNomenclature(m_tableau));
    return true;
  } catch (const std::exception& e) {
    if (erreur) *erreur = QStringLiteral("Le fichier n’a pas pu être écrit (ouvert dans un tableur ?) : %1").arg(QString::fromUtf8(e.what()));
    return false;
  }
}
