// Prométhée : atelier électrique — rapports (appareils, nomenclature, borniers, câbles, carnet de câblage, entrées
// et sorties, folios, bilan de puissance) et leurs exports, boîte à calculs, assistant de tableau de logement
// selon la NF C 15-100. Licence GPL-3.0-only.
#include <algorithm>
#include <cmath>
#include <iterator>
#include <map>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "AtelierElectrique.hpp"
#include "Document.hpp"
#include "EtiquetteEtat.hpp"
#include "FormulaireLie.hpp"
#include "Icones.hpp"
#include "VueArmoire.hpp"
#include "VueSchemaElectrique.hpp"
#include "promethee/electrique_calculateurs.hpp"
#include "promethee/electrique_fabrication.hpp"
#include "promethee/modele.hpp"

using namespace prom::elec;
using prom::ecad::ComposantElec;

namespace {
constexpr int COLONNE_PRIX = 4;  // prix unitaire, dans la nomenclature
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.trimmed().toStdString(); }
QString nombre(double v, int decimales = -1) {
  if (decimales < 0) return QLocale().toString(v, 'g', 6);
  return QLocale().toString(v, 'f', decimales);
}
const char* NOMS_RAPPORTS[] = {"Appareils", "Nomenclature", "Borniers",           "Câbles",         "Carnet de câblage",
                               "Entrées / sorties", "Folios", "Bilan de puissance", "Liste de coupe", "Chiffrage"};
const char* ICONES_RAPPORTS[] = {"rapport", "composants", "borne", "cablage", "fil", "composants", "plan", "calcul", "fil", "calcul"};
const char* AIDES_RAPPORTS[] = {
    "Un appareil par ligne : repère, désignation, fonction, emplacement, référence et folios où il est posé. Double-clic : l’appareil au schéma.",
    "Les appareils regroupés par référence, avec leurs quantités et leur prix : la liste d’achat chiffrée. Double-clique un prix unitaire pour saisir ton tarif.",
    "Chaque borne de chaque bornier : ce qui arrive côté armoire, ce qui part côté machine, son fil et son câble.",
    "Les câbles qui sortent de l’armoire : type, conducteurs, longueur, extrémités et potentiel porté par chaque conducteur.",
    "Liaison par liaison, de borne à borne, comme sur le dessin : la liste pour câbler l’armoire, avec section, couleur et longueur estimée.",
    "Les entrées et sorties d’automate, câblées ou en réserve, avec ce qui y est raccordé.",
    "La liste des folios du projet.",
    "Les récepteurs qui ont une puissance (moteurs, chauffages…), avec facteurs d’utilisation et de simultanéité, et le courant appelé.",
    "Les fils de l’armoire à couper, par section et couleur, du plus long au plus court : longueur par les goulottes, embout de chaque bout, filerie à commander.",
    "Le prix du tableau : matériel de la nomenclature, filerie et embouts, main d’œuvre en temps unitaires de câbleur, contrôle et essais."};
static_assert(std::size(NOMS_RAPPORTS) == AtelierElectrique::NB_RAPPORTS && std::size(AIDES_RAPPORTS) == AtelierElectrique::NB_RAPPORTS &&
              std::size(ICONES_RAPPORTS) == AtelierElectrique::NB_RAPPORTS);
// « embout 0,75 mm² bleu » → « 0,75 mm² bleu » : le titre de la colonne dit déjà ce que c'est
QString embout(const std::string& e) { return QString::fromStdString(e.rfind("embout ", 0) == 0 ? e.substr(7) : e); }
QString nomCouleur(const std::string& code) {
  for (const auto& [k, nom] : couleursConducteurs())
    if (k == code) return QString::fromStdString(nom);
  return QString::fromStdString(code);
}
QString euros(double v) { return QLocale().toString(v, 'f', 2) + QStringLiteral(" €"); }
}  // namespace

QString AtelierElectrique::nomRapport(int rapport) { return QString::fromUtf8(NOMS_RAPPORTS[std::clamp(rapport, 0, NB_RAPPORTS - 1)]); }
QString AtelierElectrique::iconeRapport(int rapport) { return QString::fromLatin1(ICONES_RAPPORTS[std::clamp(rapport, 0, NB_RAPPORTS - 1)]); }

// ---------- Rapports ----------
QWidget* AtelierElectrique::pageRapports() {
  auto* w = new QWidget;
  auto* col = new QVBoxLayout(w);
  col->setContentsMargins(12, 10, 12, 10);
  // Titre et explication au-dessus (ils passent à la ligne), boutons compacts dessous : la page tient à l'étroit
  m_titreRapport = new QLabel;
  m_titreRapport->setObjectName(QStringLiteral("titreRapport"));
  m_titreRapport->setWordWrap(true);
  col->addWidget(m_titreRapport);
  auto* tete = new QHBoxLayout;
  tete->setSpacing(4);
  auto compact = [](QPushButton* b) {
    b->setStyleSheet(QStringLiteral("QPushButton { padding: 4px 8px; }"));
    b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  };
  auto* cable = new QPushButton(icone(QStringLiteral("cablage")), QStringLiteral("Câble +"));
  cable->setObjectName(QStringLiteral("nouveauCable"));
  cable->setToolTip(QStringLiteral("Nouveau câble : repère, type, conducteurs, extrémités"));
  cable->setVisible(false);  // seulement dans le rapport des câbles
  connect(cable, &QPushButton::clicked, this, [this] { ajouterCable(); });
  compact(cable);
  tete->addWidget(cable);
  auto* csv = new QPushButton(icone(QStringLiteral("exporter")), QStringLiteral("CSV…"));
  csv->setToolTip(QStringLiteral("Exporter ce rapport en CSV (tableur)"));
  connect(csv, &QPushButton::clicked, this, [this] {
    const QString nom = QString::fromUtf8(NOMS_RAPPORTS[m_rapport]).toLower().replace(QLatin1Char(' '), QLatin1Char('-')).remove(QLatin1Char('/'));
    const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter le rapport"), nom + QStringLiteral(".csv"), QStringLiteral("CSV (*.csv)"));
    if (!c.isEmpty()) m_etat->setText(exporterCsv(m_rapport, c) ? QStringLiteral("Rapport écrit : %1").arg(c) : QStringLiteral("<span style='color:#C2402E'>Écriture impossible (fichier ouvert ailleurs ?)</span>"));
  });
  compact(csv);
  tete->addWidget(csv);
  auto* tout = new QPushButton(icone(QStringLiteral("dossier")), QStringLiteral("Tout exporter…"));
  tout->setToolTip(QStringLiteral("Folios en PDF, tous les rapports en CSV, plans de perçage en DXF, dans un dossier"));
  connect(tout, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier du projet électrique"));
    if (d.isEmpty()) return;
    const QStringList f = exporterDossier(d);
    m_etat->setText(QStringLiteral("%1 fichier(s) écrit(s) dans %2").arg(f.size()).arg(d));
  });
  compact(tout);
  tete->addWidget(tout);
  tete->addStretch(1);
  col->addLayout(tete);
  m_tableRapport = new QTableWidget;
  m_tableRapport->setObjectName(QStringLiteral("tableRapport"));
  m_tableRapport->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_tableRapport->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_tableRapport->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tableRapport->setAlternatingRowColors(true);
  m_tableRapport->verticalHeader()->setVisible(false);
  m_tableRapport->horizontalHeader()->setStretchLastSection(true);
  connect(m_tableRapport, &QTableWidget::cellDoubleClicked, this, [this](int r, int colonne) {
    const ProjetElectrique* pe = projetCourant();
    if (!pe || (m_rapport == 1 && colonne == COLONNE_PRIX)) return;  // le prix s'édite sur place
    for (int c = 0; c < std::min(2, m_tableRapport->columnCount()); ++c) {
      const QTableWidgetItem* it = m_tableRapport->item(r, c);
      if (!it) continue;
      std::string ref = s(it->text().section(QLatin1Char(','), 0, 0).section(QLatin1Char(':'), 0, 0));
      while (!ref.empty() && ref.front() == '-') ref.erase(ref.begin());
      if (const ComposantElec* x = pe->schema.parReference(ref)) {
        montrerAppareil(x->uuid);
        return;
      }
    }
  });
  // Prix saisi dans la nomenclature : gardé dans le projet (annulable) ; vide, la ligne revient au prix indicatif
  connect(m_tableRapport, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
    if (m_rapport != 1 || !it || it->column() != COLONNE_PRIX) return;
    const std::string cle = s(it->data(Qt::UserRole).toString());
    if (cle.empty()) return;
    QString t = it->text().trimmed();
    t.remove(QStringLiteral("€")).remove(QLatin1Char(' ')).remove(QChar(0x202F)).remove(QChar(0xA0));
    bool ok = t.isEmpty();
    double v = -1;
    if (!t.isEmpty()) {
      v = QLocale().toDouble(t, &ok);
      if (!ok) v = QString(t).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    }
    if (!ok || (!t.isEmpty() && v < 0)) {
      m_etat->setText(QStringLiteral("<span style='color:#C2402E'>Prix non compris</span> : un nombre en euros hors taxes, ou rien pour revenir au prix indicatif."));
      QTimer::singleShot(0, this, [this] { majRapport(); });
      return;
    }
    QTimer::singleShot(0, this, [this, cle, v] {
      modifierProjet([&](ProjetElectrique& y) { fixerPrix(y, cle, v); });
      m_etat->setText(v < 0 ? QStringLiteral("Prix indicatif rétabli.") : QStringLiteral("Prix saisi : %1 HT l’unité.").arg(euros(v)));
    });
  });
  connect(m_tableRapport, &QTableWidget::itemSelectionChanged, this, [this] {
    if (m_rapport != 3) return;
    const auto sel = m_tableRapport->selectedItems();
    m_cableChoisi = sel.isEmpty() ? -1 : m_tableRapport->item(sel.front()->row(), 0)->data(Qt::UserRole).toInt();
    majPanneau();
  });
  col->addWidget(m_tableRapport, 1);
  return w;
}

void AtelierElectrique::montrerRapport(int r) {
  m_rapport = std::clamp(r, 0, NB_RAPPORTS - 1);
  m_cableChoisi = -1;
  if (mode() != Rapports) choisirMode(Rapports);
  else {
    majRapport();
    majPanneau();
  }
  if (m_rapports) {
    const QSignalBlocker b(m_rapports);
    m_rapports->setCurrentRow(m_rapport);
  }
}

void AtelierElectrique::majRapport() {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !m_tableRapport) return;
  m_titreRapport->setText(QStringLiteral("<span style='font-size:16px; font-weight:700; color:#10142A'>%1</span><br><span style='color:#6B7393'>%2</span>")
                              .arg(QString::fromUtf8(NOMS_RAPPORTS[m_rapport]), QString::fromUtf8(AIDES_RAPPORTS[m_rapport])));
  if (auto* b = m_pageRapports->findChild<QPushButton*>(QStringLiteral("nouveauCable"))) b->setVisible(m_rapport == 3);
  std::vector<QStringList> lignes;
  QStringList entete;
  std::vector<LigneNomenclatureElec> nomenclature;
  switch (m_rapport) {
    case 0:
      entete = {QStringLiteral("Repère"), QStringLiteral("Désignation"), QStringLiteral("Fonction"), QStringLiteral("Emplacement"), QStringLiteral("Référence"), QStringLiteral("Folios"),
                QStringLiteral("Qté")};
      for (const auto& a : listeAppareils(*pe))
        lignes.push_back({q(a.repere), q(a.designation), q(a.fonction), q(a.emplacement), q(a.fabricant.empty() ? a.reference : a.fabricant + " " + a.reference), q(a.folios),
                          QString::number(a.quantite)});
      break;
    case 1: {
      // Les prix d'abord visibles : fabricant et référence en une colonne, avant le prix unitaire et le montant
      entete = {QStringLiteral("Qté"), QStringLiteral("Repères"), QStringLiteral("Désignation"), QStringLiteral("Référence"), QStringLiteral("PU HT (€)"), QStringLiteral("Montant HT")};
      nomenclature = nomenclatureElectrique(*pe);
      for (const auto& n : nomenclature) {
        QString reps;
        for (const auto& r : n.reperes) reps += (reps.isEmpty() ? QString() : QStringLiteral(", ")) + q(r);
        const bool connu = n.prixSaisi || n.prixUnitaire > 0;
        const QString reference = n.reference.empty() ? q(n.fabricant) : n.fabricant.empty() ? q(n.reference) : q(n.fabricant + " " + n.reference);
        lignes.push_back({QString::number(n.quantite), reps, q(n.designation), reference, connu ? nombre(n.prixUnitaire, 2) : QString(), connu ? nombre(n.montant(), 2) : QString()});
      }
      const ChiffrageNomenclature c = chiffrage(nomenclature);
      lignes.push_back({QString(), QString(), QStringLiteral("Total HT"), QString(), c.sansPrix > 0 ? QStringLiteral("%1 sans prix").arg(c.sansPrix) : QString(), nombre(c.total, 2)});
      break;
    }
    case 2:
      entete = {QStringLiteral("Bornier"), QStringLiteral("Borne"), QStringLiteral("Côté armoire"), QStringLiteral("Côté extérieur"), QStringLiteral("Fil"), QStringLiteral("Câble"),
                QStringLiteral("Folio"), QStringLiteral("PE")};
      for (const auto& b : planBorniers(*pe))
        lignes.push_back({q(b.bornier), q(b.borne), q(b.interieur), q(b.exterieur), q(b.fil), q(b.cable), q(b.folio), b.protection ? QStringLiteral("PE") : QString()});
      break;
    case 3:
      entete = {QStringLiteral("Câble"), QStringLiteral("Type"), QStringLiteral("De"), QStringLiteral("Vers"), QStringLiteral("Longueur"), QStringLiteral("Conducteurs")};
      for (std::size_t i = 0; i < pe->cables.size(); ++i) {
        const auto& c = pe->cables[i];
        QString aff;
        for (const auto& a : c.affectation) aff += (aff.isEmpty() ? QString() : QStringLiteral("  ")) + q(a.repere) + QStringLiteral(":") + (a.fil.empty() ? QStringLiteral("—") : q(a.fil));
        lignes.push_back({q(c.reference), QStringLiteral("%1 %2G%3").arg(q(c.designation)).arg(c.conducteurs).arg(nombre(c.section)), q(c.de), q(c.vers),
                          c.longueur > 0 ? nombre(c.longueur) + QStringLiteral(" m") : QStringLiteral("à mesurer"), aff});
      }
      break;
    case 4:
      entete = {QStringLiteral("Fil"), QStringLiteral("De"), QStringLiteral("Vers"), QStringLiteral("Section"), QStringLiteral("Couleur"), QStringLiteral("Longueur"), QStringLiteral("Folio")};
      for (const auto& l : carnetCablage(*pe))
        lignes.push_back({q(l.fil), q(l.de), q(l.vers), q(l.section) + QStringLiteral(" mm²"), q(l.couleur),
                          l.longueur > 0 ? QStringLiteral("%1 mm").arg(nombre(l.longueur, 0)) : QStringLiteral("câble"), q(l.folio)});
      break;
    case 8: {
      entete = {QStringLiteral("N°"), QStringLiteral("Fil"), QStringLiteral("Section"), QStringLiteral("Couleur"), QStringLiteral("Longueur"), QStringLiteral("De"),
                QStringLiteral("Embout"), QStringLiteral("Vers"), QStringLiteral("Embout"), QStringLiteral("Folio")};
      for (const auto& c : listeDeCoupe(*pe).lignes)
        lignes.push_back({QString::number(c.numero), q(c.fil), QStringLiteral("%1 mm² %2").arg(nombre(c.section), q(c.type)), nomCouleur(c.couleur),
                          QStringLiteral("%1 mm").arg(nombre(c.longueur, 0)), q(c.de), embout(c.emboutDe), q(c.vers), embout(c.emboutVers), q(c.folio)});
      break;
    }
    case 9: {
      entete = {QStringLiteral("Poste"), QStringLiteral("Quantité"), QStringLiteral("Heures"), QStringLiteral("Montant HT")};
      const Chiffrage c = chiffrer(*pe);
      lignes.push_back({QStringLiteral("Matériel (nomenclature)"), QString(), QString(), euros(c.materiel)});
      lignes.push_back({QStringLiteral("Filerie et embouts (liste de coupe)"), QString(), QString(), euros(c.filerie)});
      for (const auto& x : c.postes) lignes.push_back({q(x.nom), nombre(x.quantite), nombre(x.heures, 2), QString()});
      lignes.push_back({QStringLiteral("Main d’œuvre à %1 de l’heure").arg(euros(pe->tauxHoraire)), QString(), nombre(c.heures, 1), euros(c.mainOeuvre)});
      lignes.push_back({QStringLiteral("Total HT"), QString(), QString(), euros(c.total)});
      break;
    }
    case 5:
      entete = {QStringLiteral("Automate"), QStringLiteral("Adresse"), QStringLiteral("Sens"), QStringLiteral("Raccordé à"), QStringLiteral("Désignation"), QStringLiteral("Folio")};
      for (const auto& x : listeEntreesSorties(*pe))
        lignes.push_back({q(x.automate), q(x.adresse), x.sens == "E" ? QStringLiteral("Entrée") : QStringLiteral("Sortie"), x.repere.empty() ? QStringLiteral("réserve") : q(x.repere),
                          q(x.designation), q(x.folio)});
      break;
    case 6:
      entete = {QStringLiteral("Folio"), QStringLiteral("Titre"), QStringLiteral("Type"), QStringLiteral("Appareils"), QStringLiteral("Potentiels")};
      for (const auto& f : listeFolios(*pe)) lignes.push_back({QString::number(f.numero), q(f.titre), q(f.type), QString::number(f.appareils), QString::number(f.fils)});
      break;
    default: {
      entete = {QStringLiteral("Repère"), QStringLiteral("Désignation"), QStringLiteral("P (kW)"), QStringLiteral("ku"), QStringLiteral("ks"), QStringLiteral("Absorbée (kW)")};
      const BilanPuissance b = bilanPuissance(*pe);
      for (const auto& x : b.lignes) lignes.push_back({q(x.repere), q(x.designation), nombre(x.puissance), nombre(x.ku, 2), nombre(x.ks, 2), nombre(x.absorbee(), 3)});
      lignes.push_back({QStringLiteral("Total"), QStringLiteral("Courant appelé : %1 A sous %2 V").arg(nombre(b.courant, 1)).arg(pe->reglages.tension), nombre(b.installee), QString(),
                        QString(), nombre(b.absorbee, 3)});
      break;
    }
  }
  // Reconstruite sans signaler de changement de sélection : le câble choisi reste choisi
  const QSignalBlocker bloque(m_tableRapport);
  m_tableRapport->setSortingEnabled(false);
  m_tableRapport->clear();
  m_tableRapport->setColumnCount(static_cast<int>(entete.size()));
  m_tableRapport->setHorizontalHeaderLabels(entete);
  m_tableRapport->setRowCount(static_cast<int>(lignes.size()));
  for (int r = 0; r < static_cast<int>(lignes.size()); ++r)
    for (int c = 0; c < lignes[static_cast<std::size_t>(r)].size(); ++c) {
      auto* it = new QTableWidgetItem(lignes[static_cast<std::size_t>(r)][c]);
      if (c == 0 && m_rapport == 3) it->setData(Qt::UserRole, r);
      m_tableRapport->setItem(r, c, it);
    }
  // Nomenclature : prix unitaire modifiable sur place (saisi en noir, indicatif en gris italique), total en gras
  m_tableRapport->setEditTriggers(m_rapport == 1 ? QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed
                                                 : QAbstractItemView::NoEditTriggers);
  for (int r = 0; r < m_tableRapport->rowCount(); ++r)
    for (int c = 0; c < m_tableRapport->columnCount(); ++c)
      if (QTableWidgetItem* it = m_tableRapport->item(r, c)) it->setFlags(it->flags() & ~Qt::ItemIsEditable);
  if (m_rapport == 1)
    for (std::size_t i = 0; i < nomenclature.size(); ++i) {
      QTableWidgetItem* it = m_tableRapport->item(static_cast<int>(i), COLONNE_PRIX);
      if (!it) continue;
      it->setFlags(it->flags() | Qt::ItemIsEditable);
      it->setData(Qt::UserRole, q(nomenclature[i].cle));
      it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      if (QTableWidgetItem* m = m_tableRapport->item(static_cast<int>(i), COLONNE_PRIX + 1)) m->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      if (!nomenclature[i].prixSaisi) {
        QFont f = it->font();
        f.setItalic(true);
        it->setFont(f);
        it->setForeground(QColor(0x6B, 0x73, 0x93));
        it->setToolTip(nomenclature[i].prixUnitaire > 0 ? QStringLiteral("Prix public indicatif du catalogue : double-clique pour saisir ton tarif")
                                                       : QStringLiteral("Pas de prix : double-clique pour saisir ton tarif"));
      } else {
        it->setToolTip(QStringLiteral("Ton prix : vide la case pour revenir au prix indicatif"));
      }
    }
  const bool totalEnBas = m_rapport == 1 || m_rapport == 7 || m_rapport == 9;
  if (totalEnBas && m_tableRapport->rowCount() > 0)
    for (int c = 0; c < m_tableRapport->columnCount(); ++c)
      if (QTableWidgetItem* it = m_tableRapport->item(m_tableRapport->rowCount() - 1, c)) {
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
      }
  if (m_rapport == 9)
    for (int r = 0; r < m_tableRapport->rowCount(); ++r)
      for (int c = 1; c < m_tableRapport->columnCount(); ++c)
        if (QTableWidgetItem* it = m_tableRapport->item(r, c)) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  m_tableRapport->resizeColumnsToContents();
  // Colonnes resserrées quand le panneau de droite est ouvert : les prix et les totaux restent en vue
  const int largeurMax = m_rapport == 1 || m_rapport == 8 || m_rapport == 9 ? 300 : 380;
  for (int c = 0; c < m_tableRapport->columnCount(); ++c) m_tableRapport->setColumnWidth(c, std::min(m_tableRapport->columnWidth(c) + 12, largeurMax));
  // Nomenclature : la désignation prend la place qui reste (coupée au besoin), prix et montant toujours visibles
  QHeaderView* entetes = m_tableRapport->horizontalHeader();
  for (int c = 0; c < m_tableRapport->columnCount(); ++c) entetes->setSectionResizeMode(c, QHeaderView::Interactive);
  entetes->setStretchLastSection(m_rapport != 1);
  if (m_rapport == 1 && m_tableRapport->columnCount() > 2) entetes->setSectionResizeMode(2, QHeaderView::Stretch);
  // Tri au clic sur l'en-tête, sauf où l'ordre a un sens (câbles, totaux, ordre de coupe)
  m_tableRapport->setSortingEnabled(m_rapport != 3 && !totalEnBas && m_rapport != 8);
  if (m_rapport == 3 && m_cableChoisi >= 0 && m_cableChoisi < m_tableRapport->rowCount()) m_tableRapport->selectRow(m_cableChoisi);
}

int AtelierElectrique::ajouterCable() {
  int i = -1;
  modifierProjet([&](ProjetElectrique& x) {
    Cable c;
    c.uuid = prom::ecad::nouvelUuid();
    int n = 1;
    for (bool libre = false; !libre; ++n) {
      libre = true;
      for (const auto& y : x.cables) libre = libre && y.reference != "W" + std::to_string(n);
      if (libre) c.reference = "W" + std::to_string(n);
    }
    dimensionnerCable(c, 3, true);
    x.cables.push_back(c);
    i = static_cast<int>(x.cables.size()) - 1;
  });
  m_rapport = 3;
  m_cableChoisi = i;
  montrerRapport(3);
  m_cableChoisi = i;
  majRapport();
  majPanneau();
  return i;
}

void AtelierElectrique::panneauRapport(QVBoxLayout* col) {
  const ProjetElectrique& pe = *projetCourant();
  auto ligne = [](QVBoxLayout* v, const QString& texte) {
    auto* l = new QLabel(texte);
    l->setWordWrap(true);
    v->addWidget(l);
    return l;
  };
  auto lien = [this](QVBoxLayout* v, const QString& icon, const QString& texte, int rapport) {
    auto* b = new QPushButton(icone(icon), texte);
    connect(b, &QPushButton::clicked, this, [this, rapport] { montrerRapport(rapport); });
    v->addWidget(b);
  };
  if (m_rapport == 1) {
    // Prix : ce qui est saisi, ce qui est indicatif, ce qui manque
    const ChiffrageNomenclature c = chiffrage(nomenclatureElectrique(pe));
    auto* g = new QGroupBox(QStringLiteral("Prix"));
    g->setObjectName(QStringLiteral("groupePrix"));
    auto* v = new QVBoxLayout(g);
    auto* total = ligne(v, QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span> HT").arg(euros(c.total)));
    total->setObjectName(QStringLiteral("totalNomenclature"));
    ligne(v, QStringLiteral("<span style='color:#4A5270'>%1 ligne(s) à ton prix · %2 au prix indicatif%3</span>")
                 .arg(c.saisies)
                 .arg(c.indicatives)
                 .arg(c.sansPrix > 0 ? QStringLiteral(" · <span style='color:#A8620E'>%1 sans prix</span>").arg(c.sansPrix) : QString()));
    ligne(v, QStringLiteral("Double-clique un prix unitaire pour saisir ton tarif ; vide la case pour revenir au prix indicatif (prix public moyen, sans remise)."));
    if (c.saisies > 0) {
      auto* effacer = new QPushButton(icone(QStringLiteral("annuler")), QStringLiteral("Revenir aux prix indicatifs"));
      effacer->setToolTip(QStringLiteral("Oublie les %1 prix saisis (annulable)").arg(c.saisies));
      connect(effacer, &QPushButton::clicked, this, [this] { modifierProjet([](ProjetElectrique& y) { y.prix.clear(); }); });
      v->addWidget(effacer);
    }
    lien(v, QStringLiteral("calcul"), QStringLiteral("Chiffrage complet"), 9);
    col->addWidget(g);
    return;
  }
  if (m_rapport == 8) {
    // Ce qu'il faut commander pour câbler : filerie par section et couleur, embouts
    const ListeDeCoupe l = listeDeCoupe(pe);
    auto* g = new QGroupBox(QStringLiteral("À commander"));
    g->setObjectName(QStringLiteral("groupeFilerie"));
    auto* v = new QVBoxLayout(g);
    if (l.lignes.empty())
      ligne(v, pe.armoires.empty() ? QStringLiteral("Les longueurs viennent de l’implantation : crée l’armoire (onglet Armoire), les fils se mesurent tout seuls.")
                                   : QStringLiteral("Aucun fil entre deux appareils implantés dans la même armoire."));
    for (const auto& b : l.bobines)
      ligne(v, QStringLiteral("<b>%1 m</b> de %2 %3 mm² %4 <span style='color:#6B7393'>(%5 fils)</span>")
                   .arg(nombre(b.metres, 1), q(b.type), nombre(b.section), nomCouleur(b.couleur))
                   .arg(b.fils));
    if (!l.embouts.empty()) {
      QString t = QStringLiteral("<b>Embouts</b>");
      for (const auto& [e, n] : l.embouts) t += QStringLiteral("<br>%1 × %2").arg(n).arg(embout(e).toHtmlEscaped());
      ligne(v, t);
    }
    ligne(v, QStringLiteral("<span style='color:#6B7393'>Longueur par les goulottes, réserve de raccordement comprise, aux 50 mm ; filerie avec 10 % de chute.</span>"));
    col->addWidget(g);
    if (!l.remarques.empty()) {
      auto* gr = new QGroupBox(QStringLiteral("À voir"));
      auto* vr = new QVBoxLayout(gr);
      for (const auto& r : l.remarques) ligne(vr, QStringLiteral("• %1").arg(q(r).toHtmlEscaped()));
      col->addWidget(gr);
    }
    return;
  }
  if (m_rapport == 9) {
    const Chiffrage c = chiffrer(pe);
    FormulaireLie fl(this, col);
    QFormLayout* f = fl.groupe(QStringLiteral("Main d’œuvre"), QStringLiteral("groupeChiffrage"));
    fl.reel(f, QStringLiteral("Taux horaire"), pe.tauxHoraire, 0, 1000, 2, QStringLiteral(" € HT"), QStringLiteral("tauxHoraire"),
            [this](double v) { modifierProjet([v](ProjetElectrique& y) { y.tauxHoraire = v; }); }, QStringLiteral("prix de l’heure de câblage facturée"));
    auto* g = new QGroupBox(QStringLiteral("Total"));
    auto* v = new QVBoxLayout(g);
    auto* t = ligne(v, QStringLiteral("<span style='font-size:15px; font-weight:700'>%1</span> HT<br><span style='color:#4A5270'>matériel %2 · filerie %3 · %4 h de main d’œuvre</span>")
                           .arg(euros(c.total), euros(c.materiel), euros(c.filerie), nombre(c.heures, 1)));
    t->setObjectName(QStringLiteral("totalChiffrage"));
    if (c.lignesSansPrix > 0)
      ligne(v, QStringLiteral("<span style='color:#A8620E'>%1 ligne(s) de nomenclature sans prix : saisis-les pour un total juste.</span>").arg(c.lignesSansPrix));
    ligne(v, QStringLiteral("<span style='color:#6B7393'>Temps unitaires courants de tableautier ; ajuste le taux à ton entreprise. Les prix se saisissent dans la nomenclature.</span>"));
    lien(v, QStringLiteral("composants"), QStringLiteral("Saisir les prix"), 1);
    col->addWidget(g);
    return;
  }
  if (m_rapport != 3 || m_cableChoisi < 0 || m_cableChoisi >= static_cast<int>(pe.cables.size())) {
    auto* g = new QGroupBox(QString::fromUtf8(NOMS_RAPPORTS[m_rapport]));
    auto* v = new QVBoxLayout(g);
    auto* l = new QLabel(QString::fromUtf8(AIDES_RAPPORTS[m_rapport]));
    l->setWordWrap(true);
    v->addWidget(l);
    if (m_rapport == 3) {
      auto* b = new QPushButton(icone(QStringLiteral("cablage")), QStringLiteral("Nouveau câble"));
      b->setObjectName(QStringLiteral("principal"));
      connect(b, &QPushButton::clicked, this, [this] { ajouterCable(); });
      v->addWidget(b);
    }
    col->addWidget(g);
    return;
  }
  const int ci = m_cableChoisi;
  const Cable& c = pe.cables[static_cast<std::size_t>(ci)];
  auto modifierCable = [this, ci](const std::function<void(Cable&)>& f) {
    modifierProjet([&](ProjetElectrique& y) {
      if (ci < static_cast<int>(y.cables.size())) f(y.cables[static_cast<std::size_t>(ci)]);
    });
  };
  auto* g = new QGroupBox(QStringLiteral("Câble %1").arg(q(c.reference)));
  g->setObjectName(QStringLiteral("groupeCable"));
  auto* f = new QFormLayout(g);
  auto* ref = new QLineEdit(q(c.reference));
  ref->setObjectName(QStringLiteral("repereCable"));
  connect(ref, &QLineEdit::editingFinished, this, [ref, modifierCable] {
    if (!ref->text().trimmed().isEmpty()) modifierCable([&](Cable& x) { x.reference = s(ref->text()); });
  });
  f->addRow(QStringLiteral("Repère"), ref);
  auto* type = new QComboBox;
  type->setEditable(true);
  type->addItems({QStringLiteral("H07RN-F"), QStringLiteral("H05VV-F"), QStringLiteral("H05RR-F"), QStringLiteral("U-1000 R2V"), QStringLiteral("U-1000 AR2V"), QStringLiteral("LiYY"),
                  QStringLiteral("LiYCY (blindé)"), QStringLiteral("FR-N1X1G1")});
  type->setCurrentText(q(c.designation));
  connect(type, &QComboBox::currentTextChanged, this, [modifierCable](const QString& t) { modifierCable([&](Cable& x) { x.designation = s(t); x.blinde = t.contains(QStringLiteral("blindé")); }); });
  f->addRow(QStringLiteral("Type"), type);
  auto* nb = new QSpinBox;
  nb->setObjectName(QStringLiteral("conducteursCable"));
  nb->setRange(1, 61);
  nb->setValue(c.conducteurs);
  bool avecPE = false;
  for (const auto& a : c.affectation) avecPE = avecPE || a.repere == "GNYE";
  connect(nb, &QSpinBox::editingFinished, this, [nb, avecPE, modifierCable] { modifierCable([&](Cable& x) { dimensionnerCable(x, nb->value(), avecPE); }); });
  f->addRow(QStringLiteral("Conducteurs"), nb);
  auto* pe2 = new QCheckBox(QStringLiteral("dont un vert-jaune (PE)"));
  pe2->setChecked(avecPE);
  connect(pe2, &QCheckBox::toggled, this, [modifierCable](bool oui) { modifierCable([&](Cable& x) { dimensionnerCable(x, x.conducteurs, oui); }); });
  f->addRow(QString(), pe2);
  auto* section = new QComboBox;
  for (double v : {0.5, 0.75, 1.0, 1.5, 2.5, 4.0, 6.0, 10.0, 16.0, 25.0, 35.0, 50.0}) section->addItem(QStringLiteral("%1 mm²").arg(nombre(v)), v);
  for (int k = 0; k < section->count(); ++k)
    if (std::abs(section->itemData(k).toDouble() - c.section) < 1e-6) section->setCurrentIndex(k);
  connect(section, &QComboBox::currentIndexChanged, this, [section, modifierCable](int) { modifierCable([&](Cable& x) { x.section = section->currentData().toDouble(); }); });
  f->addRow(QStringLiteral("Section"), section);
  auto* longueur = new QDoubleSpinBox;
  longueur->setRange(0, 10000);
  longueur->setDecimals(1);
  longueur->setSuffix(QStringLiteral(" m"));
  longueur->setSpecialValueText(QStringLiteral("à mesurer"));
  longueur->setValue(c.longueur);
  connect(longueur, &QDoubleSpinBox::editingFinished, this, [longueur, modifierCable] { modifierCable([&](Cable& x) { x.longueur = longueur->value(); }); });
  f->addRow(QStringLiteral("Longueur"), longueur);
  QStringList reperes{QString()};
  for (const auto& x : pe.schema.composants) reperes << q(repereAffiche(pe, x));
  std::sort(reperes.begin(), reperes.end(), [](const QString& a, const QString& b) { return prom::ecad::ordreNaturel(a.toStdString(), b.toStdString()); });
  for (auto [etiquette, valeur, estDe] : {std::tuple{"De", c.de, true}, std::tuple{"Vers", c.vers, false}}) {
    auto* e = new QComboBox;
    e->setObjectName(estDe ? QStringLiteral("deCable") : QStringLiteral("versCable"));
    e->setEditable(true);
    e->addItems(reperes);
    e->setCurrentText(q(valeur));
    connect(e, &QComboBox::currentTextChanged, this, [estDe = estDe, modifierCable](const QString& t) { modifierCable([&](Cable& x) { (estDe ? x.de : x.vers) = s(t); }); });
    f->addRow(QString::fromUtf8(etiquette), e);
  }
  col->addWidget(g);
  // Conducteurs et potentiels portés
  auto* ga = new QGroupBox(QStringLiteral("Conducteurs"));
  auto* fa = new QFormLayout(ga);
  QStringList potentiels{QString()};
  for (const auto& r : m_k.reseaux)
    if (r.nom.rfind("N-", 0) != 0) potentiels << q(r.nom);
  for (std::size_t k = 0; k < c.affectation.size(); ++k) {
    auto* e = new QComboBox;
    e->setEditable(true);
    e->addItems(potentiels);
    e->setCurrentText(q(c.affectation[k].fil));
    connect(e, &QComboBox::currentTextChanged, this, [k, modifierCable](const QString& t) { modifierCable([&](Cable& x) { if (k < x.affectation.size()) x.affectation[k].fil = s(t); }); });
    fa->addRow(q(c.affectation[k].repere), e);
  }
  auto* auto_ = new QPushButton(icone(QStringLiteral("numeroter")), QStringLiteral("Affecter selon le schéma"));
  auto_->setObjectName(QStringLiteral("affecterCable"));
  auto_->setToolTip(QStringLiteral("les potentiels qui relient « De » et « Vers » : PE au vert-jaune, les autres dans l’ordre"));
  connect(auto_, &QPushButton::clicked, this, [this, ci] {
    int n = 0;
    modifierProjet([&](ProjetElectrique& y) { n = affecterCable(y, static_cast<std::size_t>(ci)); });
    m_etat->setText(n > 0 ? QStringLiteral("%1 conducteur(s) affecté(s).").arg(n) : QStringLiteral("Aucun potentiel ne relie « De » et « Vers » au schéma."));
  });
  fa->addRow(auto_);
  col->addWidget(ga);
  auto* suppr = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer le câble"));
  connect(suppr, &QPushButton::clicked, this, [this, ci] {
    modifierProjet([&](ProjetElectrique& y) {
      if (ci < static_cast<int>(y.cables.size())) y.cables.erase(y.cables.begin() + ci);
    });
    m_cableChoisi = -1;
  });
  col->addWidget(suppr);
}

bool AtelierElectrique::exporterCsv(int rapport, const QString& chemin) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return false;
  std::string texte;
  switch (rapport) {
    case 0: texte = csvAppareils(*pe); break;
    case 1: texte = csvNomenclatureElectrique(*pe); break;
    case 2: texte = csvBorniers(*pe); break;
    case 3: texte = csvCables(*pe); break;
    case 4: texte = csvCarnetCablage(*pe); break;
    case 5: texte = csvEntreesSorties(*pe); break;
    case 6: texte = csvFolios(*pe); break;
    case 8: texte = csvListeDeCoupe(*pe); break;
    case 9: texte = csvChiffrage(*pe); break;
    default: texte = csvBilanPuissance(*pe); break;
  }
  QFile f(chemin);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  return f.write(QByteArray::fromStdString(texte)) == static_cast<qint64>(texte.size());
}

QStringList AtelierElectrique::exporterDossier(const QString& dossier) {
  QStringList ecrits;
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return ecrits;
  const QDir d(dossier);
  const QString pdf = d.filePath(QStringLiteral("folios.pdf"));
  if (exporterPdf(pdf)) ecrits << pdf;
  const char* fichiers[] = {"appareils.csv",       "nomenclature.csv", "borniers.csv",           "cables.csv",         "carnet-de-cablage.csv",
                            "entrees-sorties.csv", "folios.csv",       "bilan-de-puissance.csv", "liste-de-coupe.csv", "chiffrage.csv"};
  static_assert(std::size(fichiers) == NB_RAPPORTS);
  for (int i = 0; i < NB_RAPPORTS; ++i) {
    const QString c = d.filePath(QString::fromLatin1(fichiers[i]));
    if (exporterCsv(i, c)) ecrits << c;
  }
  // Chaque folio en DXF, pour qui reprend les plans dans un logiciel de DAO
  if (d.mkpath(QStringLiteral("dxf")))
    for (std::size_t i = 0; i < pe->schema.feuilles.size(); ++i) {
      const QString c = d.filePath(QStringLiteral("dxf/folio-%1.dxf").arg(i + 1, 2, 10, QLatin1Char('0')));
      if (exporterDxf(c, static_cast<int>(i)) >= 0) ecrits << c;
    }
  for (std::size_t a = 0; a < pe->armoires.size(); ++a) {
    QString nom = q(pe->armoires[a].nom).toLower();
    nom.replace(QRegularExpression(QStringLiteral("[^a-z0-9àâçéèêëîïôûùüÿñæœ-]+")), QStringLiteral("-"));
    const QString c = d.filePath(QStringLiteral("percage-%1.dxf").arg(nom));
    QFile f(c);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      f.write(QByteArray::fromStdString(dxfPlaque(*pe, a)));
      ecrits << c;
    }
  }
  return ecrits;
}

// ---------- Calculs : la boîte à calculs ----------
QWidget* AtelierElectrique::gaucheCalculs() {
  auto* w = new QWidget;
  auto* col = new QVBoxLayout(w);
  col->setContentsMargins(8, 6, 6, 6);
  col->setSpacing(4);
  auto* t = new QLabel(QStringLiteral("<span style='font-weight:700; color:#232A45; font-size:13px'>Calculs</span>"));
  t->setContentsMargins(2, 6, 2, 2);
  col->addWidget(t);
  m_rechercheCalcul = new QLineEdit;
  m_rechercheCalcul->setObjectName(QStringLiteral("rechercheCalcul"));
  m_rechercheCalcul->setPlaceholderText(QStringLiteral("Chercher : section, terre, moteur…"));
  m_rechercheCalcul->setClearButtonEnabled(true);
  col->addWidget(m_rechercheCalcul);
  m_listeCalculs = new QTreeWidget;
  m_listeCalculs->setObjectName(QStringLiteral("listeCalculs"));
  m_listeCalculs->setHeaderHidden(true);
  m_listeCalculs->setIndentation(10);
  m_listeCalculs->setIconSize(QSize(18, 18));
  for (const std::string& famille : famillesCalculateurs()) {
    auto* f = new QTreeWidgetItem(m_listeCalculs);
    f->setText(0, q(famille));
    f->setFlags(Qt::ItemIsEnabled);
    QFont gras = f->font(0);
    gras.setBold(true);
    f->setFont(0, gras);
    for (const auto& c : calculateursElectriques())
      if (c.famille == famille) {
        auto* it = new QTreeWidgetItem(f);
        it->setText(0, q(c.nom));
        it->setToolTip(0, q(c.description));
        it->setData(0, Qt::UserRole, q(c.id));
        it->setIcon(0, icone(QStringLiteral("calcul")));
      }
  }
  m_listeCalculs->expandAll();
  connect(m_listeCalculs, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
    if (it && !it->data(0, Qt::UserRole).toString().isEmpty()) choisirCalcul(s(it->data(0, Qt::UserRole).toString()));
  });
  connect(m_rechercheCalcul, &QLineEdit::textChanged, this, [this](const QString& texte) {
    // Mots cherchés dans le nom, la description et la règle, sans se soucier des accents
    auto simple = [](QString x) {
      x = x.normalized(QString::NormalizationForm_D);
      QString r;
      for (QChar c : x)
        if (c.category() != QChar::Mark_NonSpacing) r += c.toLower();
      return r;
    };
    const QStringList mots = simple(texte).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (int i = 0; i < m_listeCalculs->topLevelItemCount(); ++i) {
      QTreeWidgetItem* f = m_listeCalculs->topLevelItem(i);
      int visibles = 0;
      for (int j = 0; j < f->childCount(); ++j) {
        QTreeWidgetItem* it = f->child(j);
        const Calculateur* c = calculateur(s(it->data(0, Qt::UserRole).toString()));
        const QString tout = c ? simple(q(c->nom + " " + c->description + " " + c->regle + " " + c->id)) : QString();
        bool ok = true;
        for (const QString& m : mots) ok = ok && tout.contains(m);
        it->setHidden(!ok);
        visibles += ok ? 1 : 0;
      }
      f->setHidden(visibles == 0);
    }
  });
  auto* aide = new QLabel(QStringLiteral("<span style='color:#6B7393'>Chaque calcul montre sa formule, ce que dit le résultat et la règle qui s’y rattache. "
                                         "Les valeurs proposées sont les plus courantes.</span>"));
  aide->setWordWrap(true);
  col->addWidget(m_listeCalculs, 1);
  col->addWidget(aide);
  return w;
}

QWidget* AtelierElectrique::pageCalculs() {
  auto* defil = new QScrollArea;
  defil->setWidgetResizable(true);
  defil->setFrameShape(QFrame::NoFrame);
  auto* w = new QWidget;
  w->setObjectName(QStringLiteral("pageCalculs"));
  auto* col = new QVBoxLayout(w);
  col->setContentsMargins(24, 16, 24, 16);
  col->setSpacing(10);
  m_titreCalcul = new QLabel;
  m_titreCalcul->setObjectName(QStringLiteral("titreCalcul"));
  m_titreCalcul->setWordWrap(true);
  col->addWidget(m_titreCalcul);
  m_formCalcul = new QGroupBox(QStringLiteral("Données"));
  m_formCalcul->setObjectName(QStringLiteral("donneesCalcul"));
  m_formCalcul->setMaximumWidth(620);
  new QFormLayout(m_formCalcul);
  col->addWidget(m_formCalcul);
  m_resultatCalcul = new QLabel;
  m_resultatCalcul->setObjectName(QStringLiteral("resultatCalcul"));
  m_resultatCalcul->setWordWrap(true);
  m_resultatCalcul->setTextFormat(Qt::RichText);
  m_resultatCalcul->setStyleSheet(QStringLiteral("QLabel { background: #FFF6E6; border: 1px solid #F0D29A; border-radius: 10px; padding: 12px; color: #232A45; }"));
  col->addWidget(m_resultatCalcul);
  col->addStretch(1);
  defil->setWidget(w);
  choisirCalcul(m_calculChoisi);
  return defil;
}

void AtelierElectrique::choisirCalcul(const std::string& id) {
  const Calculateur* c = calculateur(id);
  if (!c || !m_formCalcul) return;
  m_calculChoisi = id;
  m_titreCalcul->setText(QStringLiteral("<span style='font-size:18px; font-weight:700'>%1</span><br><span style='color:#6B7393'>%2</span><br>"
                                        "<span style='color:#8A91AD; font-size:11px'>%3</span>")
                             .arg(q(c->nom).toHtmlEscaped(), q(c->description).toHtmlEscaped(), q(c->regle).toHtmlEscaped()));
  // Les champs du calcul, refaits à chaque changement de fiche
  auto* f = qobject_cast<QFormLayout*>(m_formCalcul->layout());
  while (f->rowCount() > 0) f->removeRow(0);
  std::vector<std::function<double()>> lire;
  for (const auto& ch : c->champs) {
    const QString nom = QStringLiteral("champ-%1").arg(q(ch.id));
    if (!ch.choix.empty()) {
      auto* b = new QComboBox;
      b->setObjectName(nom);
      for (const auto& x : ch.choix) b->addItem(q(x));
      b->setCurrentIndex(static_cast<int>(ch.valeur));
      if (!ch.aide.empty()) b->setToolTip(q(ch.aide));
      connect(b, &QComboBox::currentIndexChanged, this, [this] { if (m_calculer) m_calculer(); });
      f->addRow(q(ch.nom), b);
      lire.push_back([b] { return static_cast<double>(b->currentIndex()); });
    } else {
      auto* sp = new QDoubleSpinBox;
      sp->setObjectName(nom);
      sp->setRange(ch.min, ch.max);
      sp->setDecimals(ch.decimales);
      if (!ch.unite.empty()) sp->setSuffix(QStringLiteral(" ") + q(ch.unite));
      sp->setValue(ch.valeur);
      sp->setSingleStep(ch.decimales == 0 ? 1.0 : ch.max <= 1.0001 ? 0.05 : 1.0);
      if (!ch.aide.empty()) sp->setToolTip(q(ch.aide));
      connect(sp, &QDoubleSpinBox::valueChanged, this, [this] { if (m_calculer) m_calculer(); });
      f->addRow(q(ch.nom), sp);
      lire.push_back([sp] { return sp->value(); });
    }
  }
  m_calculer = [this, c, lire] {
    std::vector<double> v;
    for (const auto& l : lire) v.push_back(l());
    const ResultatCalcul r = executer(*c, v);
    QString h;
    for (const auto& l : r.lignes)
      if (l.principal) h += QStringLiteral("<span style='font-size:20px; font-weight:700; color:#10142A'>%1</span>&nbsp; <span style='color:#4A5270'>%2</span><br>").arg(q(l.valeur).toHtmlEscaped(), q(l.nom).toHtmlEscaped());
    for (const auto& l : r.lignes)
      if (!l.principal) h += QStringLiteral("%1 : <b>%2</b><br>").arg(q(l.nom).toHtmlEscaped(), q(l.valeur).toHtmlEscaped());
    if (!r.explication.empty()) h += QStringLiteral("<p style='margin-top:8px'>%1</p>").arg(q(r.explication).toHtmlEscaped());
    for (const auto& a : r.avis) {
      const QString couleur = a.niveau == NiveauAvis::Erreur ? QStringLiteral("#C2402E") : a.niveau == NiveauAvis::Alerte ? QStringLiteral("#A8620E") : QStringLiteral("#2E7D32");
      const QString signe = a.niveau == NiveauAvis::Erreur ? QStringLiteral("⛔") : a.niveau == NiveauAvis::Alerte ? QStringLiteral("⚠") : QStringLiteral("✓");
      h += QStringLiteral("<span style='color:%1'>%2 %3</span> <span style='color:#8A91AD'>(%4)</span><br>").arg(couleur, signe, q(a.message).toHtmlEscaped(), q(a.regle).toHtmlEscaped());
    }
    if (!r.formule.empty()) h += QStringLiteral("<p style='color:#6B7393; margin-top:8px'><i>Comment c’est calculé :</i> %1</p>").arg(q(r.formule).toHtmlEscaped());
    m_resultatCalcul->setText(h);
  };
  m_calculer();
  if (m_listeCalculs) {
    const QSignalBlocker b(m_listeCalculs);
    for (QTreeWidgetItemIterator it(m_listeCalculs); *it; ++it)
      if ((*it)->data(0, Qt::UserRole).toString() == q(id)) m_listeCalculs->setCurrentItem(*it);
  }
}

void AtelierElectrique::majCalculs() {
  if (m_calculer) m_calculer();
}

// ---------- Logement ----------
QWidget* AtelierElectrique::pageLogement() {
  auto* racine = new QSplitter(Qt::Horizontal);
  racine->setObjectName(QStringLiteral("pageLogement"));
  // Saisie
  auto* defil = new QScrollArea;
  defil->setWidgetResizable(true);
  defil->setFrameShape(QFrame::NoFrame);
  auto* w = new QWidget;
  auto* col = new QVBoxLayout(w);
  col->setContentsMargins(18, 14, 12, 14);
  col->setSpacing(8);
  auto* intro = new QLabel(QStringLiteral("<span style='font-size:18px; font-weight:700; color:#10142A'>Tableau électrique d’un logement</span><br>"
                                          "<span style='color:#6B7393'>Décris les pièces et les équipements : les circuits, les différentiels 30 mA et le coffret se "
                                          "calculent selon les règles de la NF C 15-100 (édition 2024), au fil de la saisie.</span>"));
  intro->setWordWrap(true);
  col->addWidget(intro);
  auto* g = new QGroupBox(QStringLiteral("Logement"));
  auto* f = new QFormLayout(g);
  auto* nom = new QLineEdit;
  nom->setObjectName(QStringLiteral("nomLogement"));
  f->addRow(QStringLiteral("Nom"), nom);
  auto* surface = new QDoubleSpinBox;
  surface->setObjectName(QStringLiteral("surfaceLogement"));
  surface->setRange(10, 1000);
  surface->setSuffix(QStringLiteral(" m²"));
  f->addRow(QStringLiteral("Surface habitable"), surface);
  auto* tri = new QCheckBox(QStringLiteral("Alimentation triphasée"));
  f->addRow(QString(), tri);
  auto* kva = new QComboBox;
  for (int k : {3, 6, 9, 12, 15, 18, 24, 30, 36}) kva->addItem(QStringLiteral("%1 kVA").arg(k), k);
  f->addRow(QStringLiteral("Abonnement"), kva);
  col->addWidget(g);
  auto* gp = new QGroupBox(QStringLiteral("Pièces"));
  auto* vp = new QVBoxLayout(gp);
  m_pieces = new QTableWidget;
  m_pieces->setObjectName(QStringLiteral("piecesLogement"));
  m_pieces->setColumnCount(7);
  m_pieces->setHorizontalHeaderLabels({QStringLiteral("Pièce"), QStringLiteral("Nom"), QStringLiteral("m²"), QStringLiteral("Prises"), QStringLiteral("Points lum."), QStringLiteral("Volet"),
                                       QStringLiteral("Chauffage (W)")});
  m_pieces->verticalHeader()->setVisible(false);
  m_pieces->setMinimumHeight(240);
  m_pieces->horizontalHeaderItem(3)->setToolTip(QStringLiteral("0 : le minimum de la norme"));
  vp->addWidget(m_pieces);
  auto* boutons = new QHBoxLayout;
  auto* plus = new QPushButton(QStringLiteral("+ Pièce"));
  auto* moins = new QPushButton(QStringLiteral("− Pièce"));
  // Logement type : surface (plus haut) et chambres sur place, sans fenêtre à part
  auto* chambres = new QSpinBox;
  chambres->setObjectName(QStringLiteral("chambresLogementType"));
  chambres->setRange(0, 12);
  chambres->setValue(2);
  chambres->setSuffix(QStringLiteral(" chambre(s)"));
  chambres->setToolTip(QStringLiteral("Nombre de chambres du logement type"));
  auto* type = new QPushButton(QStringLiteral("Pièces types"));
  type->setObjectName(QStringLiteral("logementType"));
  type->setToolTip(QStringLiteral("Remplit les pièces d’un logement courant de cette surface et de ce nombre de chambres"));
  boutons->addWidget(plus);
  boutons->addWidget(moins);
  boutons->addStretch(1);
  boutons->addWidget(chambres);
  boutons->addWidget(type);
  vp->addLayout(boutons);
  col->addWidget(gp);
  auto* ge = new QGroupBox(QStringLiteral("Équipements"));
  auto* grille = new QGridLayout(ge);
  struct Case {
    const char* nom;
    QCheckBox* c;
  };
  std::vector<Case> cases;
  const char* noms[] = {"Plaque de cuisson", "Four", "Lave-vaisselle", "Lave-linge", "Sèche-linge", "Congélateur", "Chauffe-eau électrique", "VMC", "Borne de recharge", "Climatisation"};
  for (int i = 0; i < 10; ++i) {
    auto* c = new QCheckBox(QString::fromUtf8(noms[i]));
    c->setObjectName(QStringLiteral("equipement%1").arg(i));
    grille->addWidget(c, i / 2, i % 2);
    cases.push_back({noms[i], c});
  }
  auto puissance = [&](const QString& etiquette, double defaut) {
    auto* p = new QSpinBox;
    p->setRange(0, 30000);
    p->setSingleStep(100);
    p->setSuffix(QStringLiteral(" W"));
    p->setValue(static_cast<int>(defaut));
    p->setToolTip(etiquette);
    return p;
  };
  auto* pChauffeEau = puissance(QStringLiteral("Chauffe-eau"), 3000);
  auto* pBorne = puissance(QStringLiteral("Borne de recharge"), 7400);
  auto* pClim = puissance(QStringLiteral("Climatisation"), 2500);
  grille->addWidget(new QLabel(QStringLiteral("Chauffe-eau")), 5, 0);
  grille->addWidget(pChauffeEau, 5, 1);
  grille->addWidget(new QLabel(QStringLiteral("Borne de recharge")), 6, 0);
  grille->addWidget(pBorne, 6, 1);
  grille->addWidget(new QLabel(QStringLiteral("Climatisation")), 7, 0);
  grille->addWidget(pClim, 7, 1);
  col->addWidget(ge);
  col->addStretch(1);
  defil->setWidget(w);
  racine->addWidget(defil);
  // Résultats
  auto* droite = new QScrollArea;
  droite->setWidgetResizable(true);
  droite->setFrameShape(QFrame::NoFrame);
  auto* wr = new QWidget;
  auto* cr = new QVBoxLayout(wr);
  cr->setContentsMargins(12, 14, 18, 14);
  cr->setSpacing(8);
  auto* generer = new QPushButton(icone(QStringLiteral("logement")), QStringLiteral("  Créer le schéma unifilaire et le coffret"));
  generer->setObjectName(QStringLiteral("genererLogement"));
  generer->setStyleSheet(QStringLiteral("QPushButton { background: #C26E00; color: white; border: none; border-radius: 8px; padding: 10px 14px; font-size: 14px; font-weight: 600; }"
                                        "QPushButton:hover { background: #A85F00; }"));
  cr->addWidget(generer);
  m_avisLogement = new QLabel;
  m_avisLogement->setObjectName(QStringLiteral("avisLogement"));
  m_avisLogement->setWordWrap(true);
  m_avisLogement->setTextFormat(Qt::RichText);
  cr->addWidget(m_avisLogement);
  m_circuits = new QTableWidget;
  m_circuits->setObjectName(QStringLiteral("circuitsLogement"));
  m_circuits->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_circuits->verticalHeader()->setVisible(false);
  m_circuits->setMinimumHeight(320);
  cr->addWidget(m_circuits, 1);
  droite->setWidget(wr);
  racine->addWidget(droite);
  racine->setSizes({640, 560});  // la saisie des pièces a besoin de ses sept colonnes

  // Lecture et écriture du formulaire
  auto ligneVide = [this](int r) {
    auto* type = new QComboBox;
    for (Piece p : {Piece::Sejour, Piece::Chambre, Piece::Cuisine, Piece::SalleDeBain, Piece::WC, Piece::Entree, Piece::Couloir, Piece::Garage, Piece::Buanderie, Piece::Bureau,
                    Piece::Exterieur, Piece::Autre})
      type->addItem(QString::fromUtf8(nomPiece(p)), static_cast<int>(p));
    m_pieces->setCellWidget(r, 0, type);
    auto* s2 = new QDoubleSpinBox;
    s2->setRange(0, 500);
    s2->setDecimals(1);
    m_pieces->setCellWidget(r, 2, s2);
    for (int c : {3, 4}) {
      auto* n = new QSpinBox;
      n->setRange(0, 40);
      // Prises à 0 : le nombre minimal de la norme pour cette pièce, affiché comme tel
      if (c == 3) {
        n->setSpecialValueText(QStringLiteral("minimum"));
        n->setToolTip(QStringLiteral("« minimum » : le nombre de prises exigé par la NF C 15-100 pour cette pièce ; ou le nombre voulu"));
      }
      m_pieces->setCellWidget(r, c, n);
    }
    auto* v = new QCheckBox;
    m_pieces->setCellWidget(r, 5, v);
    auto* ch = new QSpinBox;
    ch->setRange(0, 20000);
    ch->setSingleStep(250);
    m_pieces->setCellWidget(r, 6, ch);
    m_pieces->setItem(r, 1, new QTableWidgetItem);
    for (QWidget* x : {static_cast<QWidget*>(type), static_cast<QWidget*>(s2), m_pieces->cellWidget(r, 3), m_pieces->cellWidget(r, 4), static_cast<QWidget*>(v), static_cast<QWidget*>(ch)}) {
      if (auto* c = qobject_cast<QComboBox*>(x)) connect(c, &QComboBox::currentIndexChanged, this, [this] { if (m_calculLogement) m_calculLogement(); });
      if (auto* d = qobject_cast<QDoubleSpinBox*>(x)) connect(d, &QDoubleSpinBox::valueChanged, this, [this] { if (m_calculLogement) m_calculLogement(); });
      if (auto* n = qobject_cast<QSpinBox*>(x)) connect(n, &QSpinBox::valueChanged, this, [this] { if (m_calculLogement) m_calculLogement(); });
      if (auto* c = qobject_cast<QCheckBox*>(x)) connect(c, &QCheckBox::toggled, this, [this] { if (m_calculLogement) m_calculLogement(); });
    }
  };
  m_ecrireLogement = [=, this](const prom::elec::Logement& l) {
    const QSignalBlocker b1(m_pieces);
    nom->setText(q(l.nom));
    surface->setValue(l.surface);
    tri->setChecked(l.triphase);
    kva->setCurrentIndex(std::max(0, kva->findData(static_cast<int>(std::lround(l.abonnementKVA)))));
    m_pieces->setRowCount(static_cast<int>(l.pieces.size()));
    for (int r = 0; r < static_cast<int>(l.pieces.size()); ++r) {
      const PieceLogement& p = l.pieces[static_cast<std::size_t>(r)];
      ligneVide(r);
      auto* t = qobject_cast<QComboBox*>(m_pieces->cellWidget(r, 0));
      const QSignalBlocker bt(t);
      t->setCurrentIndex(t->findData(static_cast<int>(p.type)));
      m_pieces->item(r, 1)->setText(q(p.nom));
      auto* sf = qobject_cast<QDoubleSpinBox*>(m_pieces->cellWidget(r, 2));
      const QSignalBlocker bs(sf);
      sf->setValue(p.surface);
      auto* pr = qobject_cast<QSpinBox*>(m_pieces->cellWidget(r, 3));
      const QSignalBlocker bp(pr);
      pr->setValue(p.prises);
      auto* pl = qobject_cast<QSpinBox*>(m_pieces->cellWidget(r, 4));
      const QSignalBlocker bl(pl);
      pl->setValue(p.pointsLumineux);
      auto* vo = qobject_cast<QCheckBox*>(m_pieces->cellWidget(r, 5));
      const QSignalBlocker bv(vo);
      vo->setChecked(p.voletRoulant);
      auto* ch = qobject_cast<QSpinBox*>(m_pieces->cellWidget(r, 6));
      const QSignalBlocker bc(ch);
      ch->setValue(static_cast<int>(p.chauffage));
    }
    const bool valeurs[] = {l.plaqueCuisson, l.four, l.laveVaisselle, l.laveLinge, l.secheLinge, l.congelateur, l.chauffeEau, l.vmc, l.borneRecharge, l.climatisation};
    for (std::size_t i = 0; i < cases.size(); ++i) {
      const QSignalBlocker bq(cases[i].c);
      cases[i].c->setChecked(valeurs[i]);
    }
    pChauffeEau->setValue(static_cast<int>(l.puissanceChauffeEau));
    pBorne->setValue(static_cast<int>(l.puissanceBorne));
    pClim->setValue(static_cast<int>(l.puissanceClim));
    m_pieces->resizeColumnsToContents();
  };
  m_lireLogement = [=, this] {
    prom::elec::Logement l;
    l.nom = s(nom->text()).empty() ? std::string("Logement") : s(nom->text());
    l.surface = surface->value();
    l.triphase = tri->isChecked();
    l.abonnementKVA = kva->currentData().toDouble();
    for (int r = 0; r < m_pieces->rowCount(); ++r) {
      PieceLogement p;
      if (auto* t = qobject_cast<QComboBox*>(m_pieces->cellWidget(r, 0))) p.type = static_cast<Piece>(t->currentData().toInt());
      if (const QTableWidgetItem* it = m_pieces->item(r, 1)) p.nom = s(it->text());
      if (auto* sf = qobject_cast<QDoubleSpinBox*>(m_pieces->cellWidget(r, 2))) p.surface = sf->value();
      if (auto* pr = qobject_cast<QSpinBox*>(m_pieces->cellWidget(r, 3))) p.prises = pr->value();
      if (auto* pl = qobject_cast<QSpinBox*>(m_pieces->cellWidget(r, 4))) p.pointsLumineux = pl->value();
      if (auto* vo = qobject_cast<QCheckBox*>(m_pieces->cellWidget(r, 5))) p.voletRoulant = vo->isChecked();
      if (auto* ch = qobject_cast<QSpinBox*>(m_pieces->cellWidget(r, 6))) p.chauffage = ch->value();
      l.pieces.push_back(p);
    }
    bool* champs[] = {&l.plaqueCuisson, &l.four, &l.laveVaisselle, &l.laveLinge, &l.secheLinge, &l.congelateur, &l.chauffeEau, &l.vmc, &l.borneRecharge, &l.climatisation};
    for (std::size_t i = 0; i < cases.size(); ++i) *champs[i] = cases[i].c->isChecked();
    l.puissanceChauffeEau = pChauffeEau->value();
    l.puissanceBorne = pBorne->value();
    l.puissanceClim = pClim->value();
    return l;
  };
  m_calculLogement = [this] {
    const prom::elec::Logement l = m_lireLogement();
    const TableauLogement t = tableauLogement(l);
    const auto avis = verifierLogement(l, t);
    m_circuits->clear();
    m_circuits->setColumnCount(6);
    m_circuits->setHorizontalHeaderLabels({QStringLiteral("Différentiel"), QStringLiteral("Circuit"), QStringLiteral("Calibre"), QStringLiteral("Section"), QStringLiteral("Points"),
                                           QStringLiteral("Puissance")});
    m_circuits->setRowCount(static_cast<int>(t.circuits.size()));
    for (int r = 0; r < static_cast<int>(t.circuits.size()); ++r) {
      const auto& c = t.circuits[static_cast<std::size_t>(r)];
      const auto& d = t.differentiels.at(static_cast<std::size_t>(c.differentiel));
      const QStringList v = {QStringLiteral("ID %1 · %2 A type %3").arg(c.differentiel + 1).arg(nombre(d.calibre)).arg(q(d.type)), q(c.nom), nombre(c.calibre) + QStringLiteral(" A"),
                             nombre(c.section) + QStringLiteral(" mm²"), c.points > 0 ? QString::number(c.points) : QString(), c.puissance > 0 ? nombre(c.puissance) + QStringLiteral(" W") : QString()};
      for (int k = 0; k < v.size(); ++k) m_circuits->setItem(r, k, new QTableWidgetItem(v[k]));
    }
    m_circuits->resizeColumnsToContents();
    m_circuits->horizontalHeader()->setStretchLastSection(true);
    QString texte = QStringLiteral("<b>%1 circuits</b>, <b>%2 interrupteurs différentiels 30 mA</b>, coffret de <b>%3 rangée(s)</b> (%4 modules occupés sur %5).")
                        .arg(t.circuits.size())
                        .arg(t.differentiels.size())
                        .arg(t.rangees)
                        .arg(t.modulesOccupes)
                        .arg(t.modulesTotal);
    if (avis.empty()) texte += QStringLiteral("<br><span style='color:#2E8B57; font-weight:600'>✓ Conforme aux règles appliquées.</span>");
    for (const auto& a : avis)
      texte += QStringLiteral("<br>%1 <b>%2</b> : %3").arg(a.exigence ? QStringLiteral("<span style='color:#C2402E'>⛔</span>") : QStringLiteral("<span style='color:#A8620E'>⚠</span>"),
                                                          q(a.regle).toHtmlEscaped(), q(a.message).toHtmlEscaped());
    for (const auto& r : t.remarques) texte += QStringLiteral("<br><span style='color:#4A5270'>• %1</span>").arg(q(r).toHtmlEscaped());
    m_avisLogement->setText(texte);
  };
  connect(plus, &QPushButton::clicked, this, [this, ligneVide] {
    const int r = m_pieces->rowCount();
    m_pieces->insertRow(r);
    ligneVide(r);
    qobject_cast<QComboBox*>(m_pieces->cellWidget(r, 0))->setCurrentIndex(1);
    qobject_cast<QDoubleSpinBox*>(m_pieces->cellWidget(r, 2))->setValue(10);
    qobject_cast<QSpinBox*>(m_pieces->cellWidget(r, 4))->setValue(1);
    m_calculLogement();
  });
  connect(moins, &QPushButton::clicked, this, [this] {
    const int r = m_pieces->currentRow() >= 0 ? m_pieces->currentRow() : m_pieces->rowCount() - 1;
    if (r >= 0) m_pieces->removeRow(r);
    m_calculLogement();
  });
  connect(type, &QPushButton::clicked, this, [this, surface, chambres] {
    m_ecrireLogement(logementType(surface->value(), chambres->value()));
    m_calculLogement();
    m_etat->setText(QStringLiteral("Pièces d’un logement type de %1 m², %2 chambre(s) : ajuste-les au besoin.").arg(nombre(surface->value())).arg(chambres->value()));
  });
  connect(m_pieces, &QTableWidget::itemChanged, this, [this] { m_calculLogement(); });
  for (const auto& c : cases) connect(c.c, &QCheckBox::toggled, this, [this] { m_calculLogement(); });
  for (auto* p : {pChauffeEau, pBorne, pClim}) connect(p, &QSpinBox::valueChanged, this, [this] { m_calculLogement(); });
  connect(surface, &QDoubleSpinBox::valueChanged, this, [this] { m_calculLogement(); });
  connect(tri, &QCheckBox::toggled, this, [this] { m_calculLogement(); });
  connect(kva, &QComboBox::currentIndexChanged, this, [this] { m_calculLogement(); });
  connect(nom, &QLineEdit::editingFinished, this, [this] { m_calculLogement(); });
  connect(generer, &QPushButton::clicked, this, [this] { appliquerLogement(m_lireLogement()); });
  return racine;
}

void AtelierElectrique::majLogement() {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || !m_ecrireLogement) return;
  if (m_logementPour != pe->uuid) {  // un autre projet (même au même indice, dans un autre document)
    m_logementPour = pe->uuid;
    m_ecrireLogement(pe->logement ? *pe->logement : logementType(70, 2));
  }
  m_calculLogement();
}

prom::elec::Logement AtelierElectrique::logementCourant() const { return m_lireLogement ? m_lireLogement() : prom::elec::Logement{}; }

void AtelierElectrique::appliquerLogement(const prom::elec::Logement& l) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return;
  int poses = 0;
  for (const auto& f : pe->schema.feuilles) poses += static_cast<int>(f.symboles.size());
  // Pas de question : le remplacement s'annule (Ctrl+Z), la barre d'état le dit
  const bool remplace = poses > 0 && !pe->logement;
  modifierProjet([&](ProjetElectrique& x) {
    ProjetElectrique n = projetLogement(l);
    n.uuid = x.uuid;
    n.nom = l.nom;
    x = n;
  });
  m_logementPour = projetCourant()->uuid;
  choisirMode(Schema);
  allerAuFolio(std::min(1, static_cast<int>(projetCourant()->schema.feuilles.size()) - 1));
  m_armoire->setArmoire(m_projet, projetCourant()->armoires.empty() ? -1 : 0);
  QTimer::singleShot(0, this, [this] { m_schema->recadrer(); });
  m_etat->setText(QStringLiteral("Unifilaire et coffret créés : %1 circuits. L’armoire, la nomenclature et les rapports sont à jour.%2")
                      .arg(tableauLogement(l).circuits.size())
                      .arg(remplace ? QStringLiteral(" Le schéma d’avant est remplacé : Ctrl+Z le rend.") : QString()));
}
