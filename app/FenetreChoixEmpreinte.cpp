// Prométhée : fenêtre « Choisir une empreinte » et générateur IPC-7351B (lot L4). Licence GPL-3.0-only.
#include "FenetreChoixEmpreinte.hpp"

#include <algorithm>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ApercuArticle.hpp"
#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// Élément trié comme un nombre
class ElementNombre : public QTableWidgetItem {
 public:
  explicit ElementNombre(int n) : QTableWidgetItem(QString::number(n)) { setData(Qt::UserRole + 1, n); }
  bool operator<(const QTableWidgetItem& o) const override { return data(Qt::UserRole + 1).toInt() < o.data(Qt::UserRole + 1).toInt(); }
};
}  // namespace

FenetreChoixEmpreinte::FenetreChoixEmpreinte(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreChoixEmpreinte"));
  setWindowTitle(QStringLiteral("Choisir une empreinte"));
  resize(900, 560);
  auto* col = new QVBoxLayout(this);
  m_titre = inspecteur::texte();
  col->addWidget(m_titre);
  auto* ligne = new QHBoxLayout;
  m_compatibles = new QCheckBox(QStringLiteral("Compatibles seulement"));
  m_compatibles->setObjectName(QStringLiteral("filtreCompatibles"));
  m_compatibles->setChecked(true);
  m_compatibles->setToolTip(QStringLiteral("Les empreintes de la même sorte que celle du symbole, au même nombre de pastilles. Décoche pour voir toutes celles au même nombre de pastilles."));
  m_texte = new QLineEdit;
  m_texte->setObjectName(QStringLiteral("filtreEmpreintes"));
  m_texte->setPlaceholderText(QStringLiteral("Filtrer (0603, SOIC, pas 2,54…)"));
  m_texte->setClearButtonEnabled(true);
  ligne->addWidget(m_compatibles);
  ligne->addWidget(m_texte, 1);
  col->addLayout(ligne);
  auto* partage = new QSplitter(Qt::Horizontal);
  m_table = new QTableWidget(0, 4);
  m_table->setObjectName(QStringLiteral("listeEmpreintes"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Empreinte"), QStringLiteral("Description"), QStringLiteral("Pastilles"), QStringLiteral("Sorte")});
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->verticalHeader()->hide();
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->setSortingEnabled(true);
  partage->addWidget(m_table);
  m_apercu = new QLabel;
  m_apercu->setMinimumSize(280, 260);
  m_apercu->setAlignment(Qt::AlignCenter);
  partage->addWidget(m_apercu);
  partage->setStretchFactor(0, 3);
  partage->setStretchFactor(1, 2);
  col->addWidget(partage, 1);
  auto* boutons = new QHBoxLayout;
  m_appliquer = new QPushButton(QStringLiteral("Appliquer"));
  m_appliquer->setObjectName(QStringLiteral("appliquerEmpreinte"));
  m_appliquer->setDefault(true);
  auto* generer = new QPushButton(QStringLiteral("Générer (IPC-7351B)…"));
  generer->setObjectName(QStringLiteral("genererIPC"));
  m_bilan = inspecteur::texte();
  boutons->addWidget(m_appliquer);
  boutons->addWidget(generer);
  boutons->addStretch();
  boutons->addWidget(m_bilan);
  col->addLayout(boutons);
  connect(m_compatibles, &QCheckBox::toggled, this, [this] { refaire(); });
  connect(m_texte, &QLineEdit::textChanged, this, [this] { refaire(); });
  connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { montrerApercu(); });
  connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int l, int) { appliquer(l); });
  connect(m_appliquer, &QPushButton::clicked, this, [this] { appliquer(); });
  connect(generer, &QPushButton::clicked, this, [this] { ouvrirGenerateur(); });
}

void FenetreChoixEmpreinte::viser(const std::vector<std::string>& composants) {
  m_vises = composants;
  refaire();
}

void FenetreChoixEmpreinte::refaire() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const ComposantElec* k = c && !m_vises.empty() ? c->composant(m_vises.front()) : nullptr;
  const DefSymbole* d = k ? c->symbole(k->symbole) : nullptr;
  m_table->setSortingEnabled(false);
  m_table->setRowCount(0);
  if (!d) {
    m_titre->setText(QStringLiteral("Choisis un composant sur la feuille."));
    m_appliquer->setEnabled(false);
    return;
  }
  QStringList refs;
  for (const auto& id : m_vises)
    if (const ComposantElec* x = c->composant(id)) refs << q(x->reference);
  m_titre->setText(QStringLiteral("<b>Empreinte de %1</b> · %2").arg(refs.mid(0, 8).join(QStringLiteral(", ")).toHtmlEscaped() + (refs.size() > 8 ? QStringLiteral("…") : QString()), q(d->nom).toHtmlEscaped()));
  m_appliquer->setText(m_vises.size() > 1 ? QStringLiteral("Appliquer aux %1 composants").arg(m_vises.size()) : QStringLiteral("Appliquer"));
  m_appliquer->setEnabled(true);
  std::vector<InfoEmpreinte> liste = empreintesCompatibles(*d, m_compatibles->isChecked());
  // Celles du projet et de la bibliothèque personnelle au bon nombre de pastilles
  std::set<std::string> numeros;
  for (const auto& b : d->broches)
    if (!b.numero.empty()) numeros.insert(b.numero);
  auto proposer = [&](const DefEmpreinte& e) {
    std::set<std::string> p;
    for (const auto& pa : e.pastilles)
      if (!pa.numero.empty()) p.insert(pa.numero);
    if (p.size() != numeros.size()) return;
    if (std::any_of(liste.begin(), liste.end(), [&](const InfoEmpreinte& i) { return i.id == e.id; })) return;
    liste.push_back({e.id, e.famille, e.description, static_cast<int>(p.size())});
  };
  for (const auto& [id, e] : c->empreintes)
    if (id.rfind("perso:", 0) == 0 || id.rfind("kicad:", 0) == 0 || !genererEmpreinte(id)) proposer(e);
  for (const auto& e : AtelierElectronique::empreintesPerso()) proposer(e);
  const QString filtre = m_texte->text().trimmed();
  int ligneCourante = -1;
  for (const auto& i : liste) {
    if (!filtre.isEmpty() && !q(i.id + " " + i.description).contains(filtre, Qt::CaseInsensitive)) continue;
    const int l = m_table->rowCount();
    m_table->insertRow(l);
    auto* nom = new QTableWidgetItem(q(i.id));
    nom->setData(Qt::UserRole, q(i.id));
    m_table->setItem(l, 0, nom);
    m_table->setItem(l, 1, new QTableWidgetItem(q(i.description)));
    m_table->setItem(l, 2, new ElementNombre(i.pastilles));
    m_table->setItem(l, 3, new QTableWidgetItem(q(sorteEmpreinte(i.id))));
    if (i.id == k->empreinte) ligneCourante = l;
  }
  m_table->setSortingEnabled(true);
  for (int l = 0; l < m_table->rowCount(); ++l)
    if (m_table->item(l, 0)->data(Qt::UserRole).toString().toStdString() == k->empreinte) ligneCourante = l;
  if (ligneCourante >= 0) m_table->selectRow(ligneCourante);
  m_bilan->setText(QStringLiteral("%1 empreinte(s)").arg(m_table->rowCount()));
}

std::vector<std::string> FenetreChoixEmpreinte::empreintesMontrees() const {
  std::vector<std::string> r;
  for (int l = 0; l < m_table->rowCount(); ++l) r.push_back(m_table->item(l, 0)->data(Qt::UserRole).toString().toStdString());
  return r;
}

void FenetreChoixEmpreinte::montrerApercu() {
  const int l = m_table->currentRow();
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (l < 0 || !c) {
    m_apercu->clear();
    return;
  }
  const std::string id = m_table->item(l, 0)->data(Qt::UserRole).toString().toStdString();
  std::optional<DefEmpreinte> e;
  if (const DefEmpreinte* p = c->empreinte(id)) e = *p;
  if (!e)
    for (const auto& x : AtelierElectronique::empreintesPerso())
      if (x.id == id) e = x;
  if (e) m_apercu->setPixmap(ApercuArticle::dessinEmpreinte(*e, m_apercu->size().boundedTo(QSize(420, 420))));
  else m_apercu->clear();
}

int FenetreChoixEmpreinte::appliquer(int ligne) {
  if (ligne < 0) ligne = m_table->currentRow();
  if (ligne < 0 || ligne >= m_table->rowCount() || m_vises.empty()) return 0;
  const std::string v = m_table->item(ligne, 0)->data(Qt::UserRole).toString().toStdString();
  std::optional<DefEmpreinte> perso;
  if (const ConceptionElec* c0 = m_atelier.conceptionCourante(); c0 && !c0->empreintes.count(v))
    for (const auto& de : AtelierElectronique::empreintesPerso())
      if (de.id == v) perso = de;
  int n = 0;
  const std::vector<std::string> vises = m_vises;
  m_atelier.modifierConception(
      [&](ConceptionElec& c2) {
        if (perso) c2.empreintes[v] = *perso;
        for (const auto& id : vises)
          if (auto* k = c2.composant(id); k && k->empreinte != v) {
            k->empreinte = v;
            ++n;
          }
      },
      vises.size() > 1 ? QStringLiteral("Empreinte %1 pour %2 composants").arg(q(v)).arg(vises.size()) : QStringLiteral("Empreinte %1").arg(q(v)));
  m_bilan->setText(QStringLiteral("%1 composant(s) passé(s) en %2").arg(n).arg(q(v)));
  return n;
}

void FenetreChoixEmpreinte::ouvrirGenerateur() {
  auto* d = new DialogueIPC(this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  connect(d, &QDialog::accepted, this, [this, d] {
    const ConceptionElec* c = m_atelier.conceptionCourante();
    m_atelier.ajouterEmpreintePerso(d->empreinte(), c && !m_vises.empty() ? m_vises.front() : std::string());
    refaire();
  });
  d->show();
}

// ---------- Générateur IPC-7351B ----------
DialogueIPC::DialogueIPC(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("dialogueIPC"));
  setWindowTitle(QStringLiteral("Générer une empreinte (IPC-7351B)"));
  auto* ligne = new QHBoxLayout(this);
  auto* fl = new QFormLayout;
  m_gabarit = new QComboBox;
  m_gabarit->addItems({QStringLiteral("Puce (résistance, condensateur)"), QStringLiteral("Aile de mouette (SOIC, TSSOP, QFP)"), QStringLiteral("Sans patte (QFN, DFN)")});
  m_gabarit->setCurrentIndex(1);
  m_densite = new QComboBox;
  m_densite->addItems({QStringLiteral("M : la plus grande"), QStringLiteral("N : nominale"), QStringLiteral("L : la plus petite")});
  m_densite->setCurrentIndex(1);
  m_broches = new QSpinBox;
  m_broches->setRange(2, 400);
  m_broches->setValue(8);
  m_quatre = new QCheckBox(QStringLiteral("sur les quatre côtés"));
  auto mm = [](double v, double max = 100) {
    auto* b = new QDoubleSpinBox;
    b->setDecimals(3);
    b->setRange(0, max);
    b->setSingleStep(0.05);
    b->setSuffix(QStringLiteral(" mm"));
    b->setValue(v);
    return b;
  };
  m_pas = mm(1.27, 10);
  m_lMin = mm(5.80);
  m_lMax = mm(6.20);
  m_tMin = mm(0.40);
  m_tMax = mm(1.27);
  m_wMin = mm(0.31);
  m_wMax = mm(0.51);
  m_corpsL = mm(4.90);
  m_corpsW = mm(3.90);
  m_hauteur = mm(1.75);
  m_plot = mm(0);
  fl->addRow(QStringLiteral("Gabarit"), m_gabarit);
  fl->addRow(QStringLiteral("Densité"), m_densite);
  fl->addRow(QStringLiteral("Broches"), m_broches);
  fl->addRow(QString(), m_quatre);
  fl->addRow(QStringLiteral("Pas"), m_pas);
  fl->addRow(QStringLiteral("Portée L min"), m_lMin);
  fl->addRow(QStringLiteral("Portée L max"), m_lMax);
  fl->addRow(QStringLiteral("Patte T min"), m_tMin);
  fl->addRow(QStringLiteral("Patte T max"), m_tMax);
  fl->addRow(QStringLiteral("Largeur W min"), m_wMin);
  fl->addRow(QStringLiteral("Largeur W max"), m_wMax);
  fl->addRow(QStringLiteral("Corps (longueur)"), m_corpsL);
  fl->addRow(QStringLiteral("Corps (largeur)"), m_corpsW);
  fl->addRow(QStringLiteral("Hauteur"), m_hauteur);
  fl->addRow(QStringLiteral("Plot thermique"), m_plot);
  auto* droite = new QVBoxLayout;
  m_apercu = new QLabel;
  m_apercu->setMinimumSize(300, 260);
  m_apercu->setAlignment(Qt::AlignCenter);
  m_bilan = inspecteur::texte();
  m_bilan->setObjectName(QStringLiteral("bilanIPC"));
  m_bilan->setWordWrap(true);
  droite->addWidget(m_apercu, 1);
  droite->addWidget(m_bilan);
  auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Ranger dans la bibliothèque"));
  connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
  droite->addWidget(bb);
  ligne->addLayout(fl);
  ligne->addLayout(droite, 1);
  for (auto* b : {m_pas, m_lMin, m_lMax, m_tMin, m_tMax, m_wMin, m_wMax, m_corpsL, m_corpsW, m_hauteur, m_plot})
    connect(b, &QDoubleSpinBox::valueChanged, this, [this] { majApercu(); });
  connect(m_broches, &QSpinBox::valueChanged, this, [this] { majApercu(); });
  connect(m_gabarit, &QComboBox::currentIndexChanged, this, [this] { majApercu(); });
  connect(m_densite, &QComboBox::currentIndexChanged, this, [this] { majApercu(); });
  connect(m_quatre, &QCheckBox::toggled, this, [this] { majApercu(); });
  majApercu();
}

CotesBoitier DialogueIPC::cotes() const {
  CotesBoitier c;
  c.gabarit = static_cast<GabaritIPC>(m_gabarit->currentIndex());
  c.broches = m_broches->value();
  c.quatreCotes = m_quatre->isChecked();
  c.pas = m_pas->value();
  c.lMin = m_lMin->value();
  c.lMax = m_lMax->value();
  c.tMin = m_tMin->value();
  c.tMax = m_tMax->value();
  c.wMin = m_wMin->value();
  c.wMax = m_wMax->value();
  c.corpsL = m_corpsL->value();
  c.corpsW = m_corpsW->value();
  c.hauteur = m_hauteur->value();
  c.plotCentral = m_plot->value();
  return c;
}

void DialogueIPC::setCotes(const CotesBoitier& c) {
  m_gabarit->setCurrentIndex(static_cast<int>(c.gabarit));
  m_broches->setValue(c.broches);
  m_quatre->setChecked(c.quatreCotes);
  m_pas->setValue(c.pas);
  m_lMin->setValue(c.lMin);
  m_lMax->setValue(c.lMax);
  m_tMin->setValue(c.tMin);
  m_tMax->setValue(c.tMax);
  m_wMin->setValue(c.wMin);
  m_wMax->setValue(c.wMax);
  m_corpsL->setValue(c.corpsL);
  m_corpsW->setValue(c.corpsW);
  m_hauteur->setValue(c.hauteur);
  m_plot->setValue(c.plotCentral);
}

DensiteIPC DialogueIPC::densite() const { return static_cast<DensiteIPC>(m_densite->currentIndex()); }

void DialogueIPC::majApercu() {
  const CotesBoitier c = cotes();
  const PastillesIPC p = calculerIPC7351(c, densite());
  const DefEmpreinte e = empreinteIPC7351(c, densite());
  m_bilan->setText(QStringLiteral("Z = %1 mm · G = %2 mm · X = %3 mm<br>Pastille %4 × %5 mm, centre à %6 mm<br><b>%7</b> · %8")
                       .arg(p.z, 0, 'f', 2)
                       .arg(p.g, 0, 'f', 2)
                       .arg(p.x, 0, 'f', 2)
                       .arg(p.longueur, 0, 'f', 2)
                       .arg(p.largeur, 0, 'f', 2)
                       .arg(p.centre, 0, 'f', 3)
                       .arg(q(nomIPC7351(c, densite())).toHtmlEscaped(), q(nomKicadIPC(c)).toHtmlEscaped()));
  m_apercu->setPixmap(ApercuArticle::dessinEmpreinte(e, QSize(300, 260)));
}
