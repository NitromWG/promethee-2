// Prométhée : panneau des écarts entre le schéma et le circuit (lot L3, tâches 3.8 et 3.19). Voir PanneauEcarts.hpp.
// Licence GPL-3.0-only.
#include "PanneauEcarts.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QMenu>
#include <QFileInfo>
#include <QFileDialog>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/fichiers.hpp"
#include "Document.hpp"
#include "DialogueAppariement.hpp"
#include "InspecteurElectronique.hpp"
#include "SelectionElectronique.hpp"
#include "VueCircuit.hpp"
#include "VueSchema.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
// Filtre : tous, ceux qui attendent une décision, ceux sans risque, les liaisons tracées au circuit
enum Filtre { Tous, EnAttente, SansRisque, Liaisons };
bool passe(const EcartSynchro& e, int f) {
  switch (f) {
    case EnAttente: return !e.sansRisque;
    case SansRisque: return e.sansRisque;
    case Liaisons: return e.genre == GenreEcart::LiaisonCircuit;
    default: return true;
  }
}
QString signe(const EcartSynchro& e) {
  switch (e.genre) {
    case GenreEcart::Nouveau:
    case GenreEcart::HorsCarteDecoche: return QStringLiteral("＋");
    case GenreEcart::SupprimeLibre:
    case GenreEcart::SupprimeRoute:
    case GenreEcart::HorsCarteLibre:
    case GenreEcart::HorsCarteRoute: return QStringLiteral("−");
    case GenreEcart::EmpreinteRemplacee:
    case GenreEcart::PastillePerdue:
    case GenreEcart::Verrouillee: return QStringLiteral("⇄");
    case GenreEcart::ReseauRenomme:
    case GenreEcart::BrocheRoutee: return QStringLiteral("≈");
    case GenreEcart::LiaisonCircuit: return QStringLiteral("↰");
  }
  return QStringLiteral("·");
}
}  // namespace

PanneauEcarts::PanneauEcarts(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("panneauEcarts"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(6, 6, 6, 6);
  m_resume = inspecteur::texte();
  m_resume->setObjectName(QStringLiteral("resumeEcarts"));
  m_resume->setWordWrap(true);
  col->addWidget(m_resume);
  auto* haut = new QHBoxLayout;
  m_filtre = new QComboBox;
  m_filtre->setObjectName(QStringLiteral("filtreEcarts"));
  m_filtre->addItem(QStringLiteral("Tous les écarts"), Tous);
  m_filtre->addItem(QStringLiteral("En attente d’une décision"), EnAttente);
  m_filtre->addItem(QStringLiteral("Sans risque"), SansRisque);
  m_filtre->addItem(QStringLiteral("Liaisons tracées au circuit"), Liaisons);
  connect(m_filtre, &QComboBox::activated, this, [this](int) { remplir(); });
  haut->addWidget(m_filtre, 1);
  m_cochees = new QPushButton(QStringLiteral("Appliquer les cochés"));
  m_cochees->setObjectName(QStringLiteral("appliquerCoches"));
  connect(m_cochees, &QPushButton::clicked, this, [this] { appliquer(false); });
  haut->addWidget(m_cochees);
  m_tout = new QPushButton(QStringLiteral("Tout appliquer"));
  m_tout->setObjectName(QStringLiteral("toutAppliquer"));
  connect(m_tout, &QPushButton::clicked, this, [this] { appliquer(true); });
  haut->addWidget(m_tout);
  col->addLayout(haut);
  m_liste = new QTreeWidget;
  m_liste->setObjectName(QStringLiteral("listeEcarts"));
  m_liste->setColumnCount(4);
  m_liste->setHeaderLabels({QString(), QStringLiteral("Composant"), QStringLiteral("Écart"), QString()});
  m_liste->setRootIsDecorated(false);
  m_liste->header()->setStretchLastSection(false);
  m_liste->header()->setSectionResizeMode(2, QHeaderView::Stretch);
  m_liste->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  connect(m_liste, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int colonne) {
    if (colonne != 0) return;
    const std::string cle = it->data(0, Qt::UserRole).toString().toStdString();
    if (it->checkState(0) == Qt::Checked) m_decoches.erase(cle);
    else m_decoches.insert(cle);
  });
  connect(m_liste, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
    const std::string cle = it->data(0, Qt::UserRole).toString().toStdString();
    for (const auto& e : m_ecarts)
      if (e.cle() == cle) voir(e);
  });
  col->addWidget(m_liste, 1);
  // Mode de la synchronisation, réglé pour la conception
  auto* mode = new QWidget;
  auto* lm = new QHBoxLayout(mode);
  lm->setContentsMargins(0, 0, 0, 0);
  m_auto = new QRadioButton(QStringLiteral("Automatique pour les changements sans risque"));
  m_auto->setObjectName(QStringLiteral("synchroAutomatique"));
  m_confirmation = new QRadioButton(QStringLiteral("Toujours sur confirmation"));
  m_confirmation->setObjectName(QStringLiteral("synchroConfirmation"));
  auto* groupe = new QButtonGroup(this);
  groupe->addButton(m_auto);
  groupe->addButton(m_confirmation);
  lm->addWidget(m_auto);
  lm->addWidget(m_confirmation);
  lm->addStretch(1);
  col->addWidget(mode);
  auto regler = [this](bool confirmation) {
    const ConceptionElec* c = m_atelier.conceptionCourante();
    if (!c || c->synchroSurConfirmation == confirmation) return;
    m_atelier.modifierConception([confirmation](ConceptionElec& x) { x.synchroSurConfirmation = confirmation; },
                                 confirmation ? QStringLiteral("Synchronisation sur confirmation") : QStringLiteral("Synchronisation automatique"));
  };
  connect(m_auto, &QRadioButton::toggled, this, [regler](bool on) {
    if (on) regler(false);
  });
  connect(m_confirmation, &QRadioButton::toggled, this, [regler](bool on) {
    if (on) regler(true);
  });
  // Liens perdus (3.19) : une carte sans liens, appariée au schéma dans un tableau à deux colonnes
  auto* relier = new QToolButton;
  relier->setObjectName(QStringLiteral("relierCarte"));
  relier->setText(QStringLiteral("Relier une carte sans liens…"));
  relier->setPopupMode(QToolButton::InstantPopup);
  auto* menu = new QMenu(relier);
  connect(menu, &QMenu::aboutToShow, this, [this, menu] {
    menu->clear();
    menu->addAction(QStringLiteral("Carte KiCad (.kicad_pcb)…"), this, [this] {
      const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Carte à relier au schéma"), QString(), QStringLiteral("Carte KiCad (*.kicad_pcb)"));
      if (chemin.isEmpty()) return;
      try {
        const ConceptionElec source = conceptionDepuisKicad(prom::lireFichier(prom::cheminFichier(chemin.toStdString())), QFileInfo(chemin).completeBaseName().toStdString());
        DialogueAppariement d(m_atelier, source, this);
        d.exec();
      } catch (const std::exception& e) {
        m_atelier.afficherMessage(QStringLiteral("Carte illisible : %1").arg(QString::fromUtf8(e.what()).toHtmlEscaped()), NiveauMessage::Avertissement);
      }
    });
    const auto& toutes = m_atelier.document()->projet().electronique;
    for (std::size_t i = 0; i < toutes.size(); ++i) {
      if (static_cast<int>(i) == m_atelier.conception() || toutes[i].circuit.empreintes.empty()) continue;
      const ConceptionElec source = toutes[i];
      menu->addAction(QStringLiteral("Circuit de « %1 »").arg(q(source.nom)), this, [this, source] {
        DialogueAppariement d(m_atelier, source, this);
        d.exec();
      });
    }
  });
  relier->setMenu(menu);
  col->addWidget(relier);
  auto* titreRapport = inspecteur::texte(QStringLiteral("<b>Rapport</b> : ce qui a été appliqué"));
  col->addWidget(titreRapport);
  m_rapport = new QListWidget;
  m_rapport->setObjectName(QStringLiteral("rapportEcarts"));
  m_rapport->setMaximumHeight(110);
  col->addWidget(m_rapport);
  // Pastille de la barre d'état
  m_pastille = new QToolButton;
  m_pastille->setObjectName(QStringLiteral("pastilleEcarts"));
  m_pastille->setAutoRaise(true);
  m_pastille->setCursor(Qt::PointingHandCursor);
  m_pastille->hide();
  connect(m_pastille, &QToolButton::clicked, this, [this] { emit ouvrirDemande(); });
}

void PanneauEcarts::majPlusTard() {
  if (m_majPrevue) return;
  m_majPrevue = true;
  QTimer::singleShot(0, this, [this] {
    m_majPrevue = false;
    maj();
  });
}

void PanneauEcarts::maj() {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  setEcarts(c && !c->schemaSeul ? ecartsSynchronisation(*c) : std::vector<EcartSynchro>{});
}

void PanneauEcarts::setEcarts(std::vector<EcartSynchro> e) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  m_ecarts = std::move(e);
  {
    const QSignalBlocker b1(m_auto), b2(m_confirmation);
    m_auto->setChecked(c && !c->synchroSurConfirmation);
    m_confirmation->setChecked(c && c->synchroSurConfirmation);
  }
  const int n = static_cast<int>(m_ecarts.size());
  m_pastille->setVisible(n > 0);
  m_pastille->setText(n == 1 ? QStringLiteral("● 1 écart avec le schéma") : QStringLiteral("● %1 écarts avec le schéma").arg(n));
  m_pastille->setStyleSheet(QStringLiteral("QToolButton { color: %1; font-weight: 600; }").arg(teintes::alerte()));
  m_pastille->setToolTip(QStringLiteral("Le schéma et le circuit diffèrent : ouvrir le panneau des écarts"));
  int attente = 0;
  for (const auto& e : m_ecarts) attente += !e.sansRisque;
  if (n == 0) m_resume->setText(QStringLiteral("Le circuit suit le schéma : aucun écart."));
  else
    m_resume->setText(QStringLiteral("%1 · dont %2 qui attendent une décision (du cuivre touche ce qui changerait, ou un verrou le protège).")
                          .arg(n == 1 ? QStringLiteral("1 écart avec le schéma") : QStringLiteral("%1 écarts avec le schéma").arg(n))
                          .arg(attente));
  remplir();
}

void PanneauEcarts::remplir() {
  const QSignalBlocker b(m_liste);
  m_liste->clear();
  const int f = m_filtre->currentData().toInt();
  for (const auto& e : m_ecarts) {
    if (!passe(e, f)) continue;
    auto* it = new QTreeWidgetItem(m_liste);
    it->setData(0, Qt::UserRole, q(e.cle()));
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(0, m_decoches.count(e.cle()) ? Qt::Unchecked : Qt::Checked);
    it->setText(0, signe(e));
    it->setText(1, q(e.reference.empty() ? std::string("—") : e.reference));
    it->setText(2, q(e.description));
    it->setToolTip(2, QString::fromUtf8(nomGenreEcart(e.genre)) + (e.sansRisque ? QStringLiteral(" · sans risque") : QStringLiteral(" · attend une décision")));
    if (!e.sansRisque) it->setForeground(2, QColor(teintes::alerte()));
    auto* boutons = new QWidget;
    auto* lb = new QHBoxLayout(boutons);
    lb->setContentsMargins(0, 0, 0, 0);
    auto* appliquerB = new QToolButton;
    appliquerB->setText(QStringLiteral("Appliquer"));
    appliquerB->setObjectName(QStringLiteral("appliquerEcart"));
    auto* voirB = new QToolButton;
    voirB->setText(QStringLiteral("Voir"));
    voirB->setObjectName(QStringLiteral("voirEcart"));
    lb->addWidget(appliquerB);
    lb->addWidget(voirB);
    const EcartSynchro copie = e;
    connect(appliquerB, &QToolButton::clicked, this, [this, copie] { appliquerUn(copie); });
    connect(voirB, &QToolButton::clicked, this, [this, copie] { voir(copie); });
    m_liste->setItemWidget(it, 3, boutons);
  }
  m_cochees->setEnabled(!m_ecarts.empty());
  m_tout->setEnabled(!m_ecarts.empty());
}

void PanneauEcarts::consigner(const std::vector<EcartSynchro>& faits) {
  const QString heure = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
  for (const auto& e : faits) m_rapport->insertItem(0, QStringLiteral("%1 · %2").arg(heure, q(e.description)));
  if (!faits.empty())
    m_atelier.afficherMessage(faits.size() == 1 ? QStringLiteral("Écart appliqué : %1").arg(q(faits.front().description).toHtmlEscaped())
                                                : QStringLiteral("%1 écarts appliqués au circuit, en un pas d’annulation.").arg(faits.size()));
}

int PanneauEcarts::appliquerUn(const EcartSynchro& e) {
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& c) { n = appliquerEcarts(c, {e}); }, QStringLiteral("Écart appliqué : %1").arg(q(e.description)));
  if (n) consigner({e});
  maj();
  return n;
}

int PanneauEcarts::appliquer(bool tous) {
  std::vector<EcartSynchro> choisis;
  for (const auto& e : m_ecarts)
    if (tous || !m_decoches.count(e.cle())) choisis.push_back(e);
  if (choisis.empty()) return 0;
  int n = 0;
  m_atelier.modifierConception([&](ConceptionElec& c) { n = appliquerEcarts(c, choisis); },
                               choisis.size() == 1 ? QStringLiteral("Écart appliqué : %1").arg(q(choisis.front().description))
                                                   : QStringLiteral("%1 écarts appliqués au circuit").arg(choisis.size()));
  if (n) consigner(choisis);
  maj();
  return n;
}

void PanneauEcarts::voir(const EcartSynchro& e) {
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!c) return;
  // Choisi dans les deux vues : son symbole s'il en a un, son empreinte s'il en a une
  std::vector<SelectionElectronique::Ref> refs;
  bool symbole = false, empreinte = false;
  for (const auto& f : c->feuilles)
    for (const auto& s : f.symboles)
      if (s.composant == e.composant) symbole = true;
  for (const auto& x : c->circuit.empreintes)
    if (x.composant == e.composant) {
      empreinte = true;
      refs.push_back({SelectionElectronique::Genre::Empreinte, x.composant, x.composant});
    }
  if (symbole && m_atelier.mode() == AtelierElectronique::Schema) {
    m_atelier.montrerComposant(e.composant);
    return;
  }
  if (empreinte) {
    if (m_atelier.mode() != AtelierElectronique::Circuit) m_atelier.setMode(AtelierElectronique::Circuit);
    m_atelier.selection()->choisir(refs);
    for (const auto& x : c->circuit.empreintes)
      if (x.composant == e.composant) {
        const Pt p = x.position;
        QTimer::singleShot(0, this, [this, p] { m_atelier.vueCircuit()->centrerSur(p); });
      }
    return;
  }
  if (symbole) m_atelier.montrerComposant(e.composant);
}

int PanneauEcarts::synchroniserMaintenant() {
  maj();
  emit ouvrirDemande();
  return appliquer(true);
}
