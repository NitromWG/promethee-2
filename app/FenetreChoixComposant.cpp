// Prométhée : fenêtre « Choisir un composant » (lot L4). Licence GPL-3.0-only.
#include "FenetreChoixComposant.hpp"

#include <algorithm>

#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "ApercuArticle.hpp"
#include "AtelierElectronique.hpp"
#include "InspecteurElectronique.hpp"
#include "ZoneMessages.hpp"
#include "promethee/articles.hpp"
#include "promethee/fichiers.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
enum Colonne { Nom, Valeur, Empreinte, Fabricant, Reference, Lcsc, Description, NB };
}  // namespace

FenetreChoixComposant::FenetreChoixComposant(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreChoixComposant"));
  setWindowTitle(QStringLiteral("Choisir un composant"));
  setAcceptDrops(true);
  resize(980, 620);
  auto* col = new QVBoxLayout(this);
  m_recherche = new QLineEdit;
  m_recherche->setObjectName(QStringLiteral("rechercheArticles"));
  m_recherche->setPlaceholderText(QStringLiteral("10k 0603, self 1210, soic8, « Résistance>1k -0402 », \"texte exact\", R_*06*, /regex/…"));
  m_recherche->setClearButtonEnabled(true);
  col->addWidget(m_recherche);
  auto* ligneFiltres = new QHBoxLayout;
  for (const char* t : {"avec empreinte", "avec modèle 3D", "avec modèle de simulation", "avec fiche", "avec référence"}) {
    auto* c = new QCheckBox(QString::fromUtf8(t));
    m_filtres.push_back(c);
    ligneFiltres->addWidget(c);
    connect(c, &QCheckBox::toggled, this, [this] { chercher(m_recherche->text()); });
  }
  ligneFiltres->addStretch();
  col->addLayout(ligneFiltres);

  m_onglets = new QTabWidget;
  col->addWidget(m_onglets, 1);
  // ---------- Bibliothèque ----------
  auto* page = new QWidget;
  auto* colPage = new QVBoxLayout(page);
  colPage->setContentsMargins(0, 4, 0, 0);
  auto* partage = new QSplitter(Qt::Horizontal);
  m_table = new QTableWidget(0, NB);
  m_table->setObjectName(QStringLiteral("resultatsArticles"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("Article"), QStringLiteral("Valeur"), QStringLiteral("Empreinte"), QStringLiteral("Fabricant"), QStringLiteral("Référence"),
                                      QStringLiteral("LCSC"), QStringLiteral("Description")});
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->verticalHeader()->hide();
  m_table->horizontalHeader()->setStretchLastSection(true);
  partage->addWidget(m_table);
  auto* droite = new QWidget;
  auto* colDroite = new QVBoxLayout(droite);
  m_apercu = new ApercuArticle;
  colDroite->addWidget(m_apercu, 1);
  m_details = inspecteur::texte();
  m_details->setWordWrap(true);
  colDroite->addWidget(m_details);
  partage->addWidget(droite);
  partage->setStretchFactor(0, 3);
  partage->setStretchFactor(1, 2);
  colPage->addWidget(partage, 1);
  auto* boutons = new QHBoxLayout;
  m_poser = new QPushButton(QStringLiteral("Poser"));
  m_poser->setObjectName(QStringLiteral("poserArticle"));
  m_poser->setDefault(true);
  m_semblables = new QPushButton(QStringLiteral("Chercher des semblables"));
  m_generique = new QPushButton(QStringLiteral("Composant générique"));
  m_generique->setToolTip(QStringLiteral("Pose un composant provisoire décrit par le texte de la recherche (« LDO 3,3 V 500 mA ») ; l’ERC le signale jusqu’à son remplacement."));
  auto* archive = new QPushButton(QStringLiteral("Importer une archive…"));
  archive->setToolTip(QStringLiteral("Archive ZIP de SnapEDA, d’Ultra Librarian ou de SamacSys : symbole, empreinte et modèle 3D en un seul article. On peut aussi la déposer sur la fenêtre."));
  auto* standard = new QPushButton(QStringLiteral("Standardiser…"));
  auto* majs = new QPushButton(QStringLiteral("Mises à jour…"));
  for (QPushButton* b : {m_poser, m_semblables, m_generique, archive, standard, majs}) boutons->addWidget(b);
  boutons->addStretch();
  m_bilan = inspecteur::texte();
  boutons->addWidget(m_bilan);
  colPage->addLayout(boutons);
  m_onglets->addTab(page, QStringLiteral("Bibliothèque"));

  // ---------- Portails et distributeurs : grisés tant que l'accès en ligne n'est pas activé (D13) ----------
  auto* pagePortails = new QWidget;
  auto* colPortails = new QVBoxLayout(pagePortails);
  m_raisonPortails = inspecteur::texte(QStringLiteral(
      "<b>Accès en ligne désactivé.</b> Prométhée n’ouvre aucune connexion tant que tu ne l’as pas activé dans les préférences : les portails de modèles "
      "(SnapEDA, Ultra Librarian, SamacSys) et les distributeurs (LCSC, Mouser, Digi-Key, Farnell) restent grisés. Une archive téléchargée à la main "
      "se dépose toujours sur cette fenêtre."));
  m_raisonPortails->setObjectName(QStringLiteral("raisonPortails"));
  m_raisonPortails->setWordWrap(true);
  colPortails->addWidget(m_raisonPortails);
  m_portails = new QWidget;
  m_portails->setObjectName(QStringLiteral("portails"));
  auto* fl = new QFormLayout(m_portails);
  for (const char* t : {"LCSC", "Mouser", "Digi-Key", "Farnell", "SnapEDA", "Ultra Librarian", "SamacSys"}) {
    auto* cle = new QLineEdit;
    cle->setPlaceholderText(QStringLiteral("clé d’accès"));
    fl->addRow(QString::fromUtf8(t), cle);
  }
  auto* devise = new QLineEdit(QStringLiteral("EUR"));
  fl->addRow(QStringLiteral("Devise"), devise);
  m_portails->setEnabled(false);
  m_portails->setToolTip(QStringLiteral("Accès en ligne désactivé : aucune connexion n’est ouverte."));
  colPortails->addWidget(m_portails);
  colPortails->addStretch();
  m_onglets->addTab(pagePortails, QStringLiteral("Portails et distributeurs"));

  connect(m_recherche, &QLineEdit::textChanged, this, [this](const QString& t) { chercher(t); });
  connect(m_recherche, &QLineEdit::returnPressed, this, [this] { poser(); });
  connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { montrerApercu(); });
  connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int l, int) { poser(l); });
  connect(m_poser, &QPushButton::clicked, this, [this] { poser(); });
  connect(m_semblables, &QPushButton::clicked, this, [this] { semblables(); });
  connect(m_generique, &QPushButton::clicked, this, [this] { poserGenerique(); });
  connect(archive, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Archive d’un portail"), {}, QStringLiteral("Archives (*.zip)"));
    if (!f.isEmpty()) importerArchive(f);
  });
  connect(standard, &QPushButton::clicked, this, [this] { m_atelier.ouvrirStandardisation(); });
  connect(majs, &QPushButton::clicked, this, [this] { m_atelier.ouvrirMisesAJourArticles(); });
  refaire();
}

FenetreChoixComposant::~FenetreChoixComposant() = default;

void FenetreChoixComposant::refaire() {
  m_index = std::make_unique<IndexArticles>(m_atelier.articlesDisponibles());
  chercher(m_recherche->text());
}

void FenetreChoixComposant::chercher(const QString& requete) {
  if (!m_index) return;
  FiltresRecherche f;
  f.avecEmpreinte = m_filtres[0]->isChecked();
  f.avec3D = m_filtres[1]->isChecked();
  f.avecSimulation = m_filtres[2]->isChecked();
  f.avecFiche = m_filtres[3]->isChecked();
  f.avecReference = m_filtres[4]->isChecked();
  const auto r = m_index->chercher(requete.toStdString(), 200, f);
  std::vector<Article> articles;
  std::vector<bool> composes;
  for (const auto& x : r) {
    articles.push_back(x.article);
    composes.push_back(x.compose);
  }
  remplir(articles, composes);
  m_bilan->setText(requete.trimmed().isEmpty() ? QStringLiteral("%1 articles").arg(m_index->articles().size())
                                               : QStringLiteral("%1 résultat(s)").arg(articles.size()));
}

void FenetreChoixComposant::remplir(const std::vector<Article>& articles, const std::vector<bool>& composes) {
  m_resultats = articles;
  m_table->setUpdatesEnabled(false);
  m_table->setRowCount(static_cast<int>(articles.size()));
  for (int i = 0; i < static_cast<int>(articles.size()); ++i) {
    const Article& a = articles[static_cast<std::size_t>(i)];
    const QString nom = q(a.nom.empty() ? a.id : a.nom) + (composes[static_cast<std::size_t>(i)] ? QStringLiteral("  (composé)") : QString());
    const QStringList t{nom, q(a.valeur), q(a.empreinte), q(a.fabricant), q(a.mpn), q(a.lcsc), q(a.description)};
    for (int c = 0; c < NB; ++c) {
      auto* it = m_table->item(i, c);
      if (!it) {
        it = new QTableWidgetItem;
        m_table->setItem(i, c, it);
      }
      it->setText(t[c]);
    }
    m_table->item(i, 0)->setData(Qt::UserRole, q(a.id));
  }
  m_table->setUpdatesEnabled(true);
  if (!articles.empty()) m_table->selectRow(0);
  else montrerApercu();
}

std::string FenetreChoixComposant::articleChoisi() const {
  const int l = m_table->currentRow();
  return l >= 0 && l < static_cast<int>(m_resultats.size()) ? m_resultats[static_cast<std::size_t>(l)].id : std::string();
}

void FenetreChoixComposant::montrerApercu() {
  const int l = m_table->currentRow();
  if (l < 0 || l >= static_cast<int>(m_resultats.size())) {
    m_apercu->vider();
    m_details->clear();
    return;
  }
  const Article& a = m_resultats[static_cast<std::size_t>(l)];
  ConceptionElec essai;
  if (const ConceptionElec* c = m_atelier.conceptionCourante()) {
    essai.symboles = c->symboles;
    essai.empreintes = c->empreintes;
    essai.articles = c->articles;
  }
  const std::string id = installerArticle(essai, a);
  const DefSymbole* d = id.empty() ? nullptr : essai.symbole(id);
  const DefEmpreinte* e = a.empreinte.empty() ? nullptr : essai.empreinte(a.empreinte);
  if (d) m_apercu->montrer(*d, e);
  else m_apercu->vider();
  QString html = QStringLiteral("<b>%1</b>").arg(q(a.nom.empty() ? a.id : a.nom).toHtmlEscaped());
  if (!a.description.empty()) html += QStringLiteral("<br>") + q(a.description).toHtmlEscaped();
  if (a.hauteur > 0) html += QStringLiteral("<br>Hauteur : %1 mm").arg(a.hauteur);
  for (const auto& [k, v] : a.parametres) html += QStringLiteral("<br>%1 : %2").arg(q(k).toHtmlEscaped(), q(v).toHtmlEscaped());
  if (a.generique) html += QStringLiteral("<br><span style='color:%1'>Composant générique : l’ERC le signale jusqu’à son remplacement.</span>").arg(teintes::secondaire());
  m_details->setText(html);
}

bool FenetreChoixComposant::poser(int ligne) {
  if (ligne < 0) ligne = m_table->currentRow() >= 0 ? m_table->currentRow() : 0;
  if (ligne < 0 || ligne >= static_cast<int>(m_resultats.size())) return false;
  m_atelier.armerArticle(m_resultats[static_cast<std::size_t>(ligne)]);
  return true;
}

void FenetreChoixComposant::semblables() {
  const int l = m_table->currentRow();
  if (!m_index || l < 0 || l >= static_cast<int>(m_resultats.size())) return;
  const auto r = m_index->semblables(m_resultats[static_cast<std::size_t>(l)], 100);
  std::vector<Article> articles;
  for (const auto& x : r) articles.push_back(x.article);
  remplir(articles, std::vector<bool>(articles.size(), false));
  m_bilan->setText(QStringLiteral("%1 semblable(s)").arg(articles.size()));
}

bool FenetreChoixComposant::poserGenerique() {
  const QString t = m_recherche->text().trimmed();
  if (t.isEmpty()) {
    m_bilan->setText(QStringLiteral("Décris le composant dans la recherche (« LDO 3,3 V 500 mA »)."));
    return false;
  }
  m_atelier.armerArticle(articleGenerique(t.toStdString(), 0));
  return true;
}

std::string FenetreChoixComposant::importerArchive(const QString& chemin) {
  std::string contenu;
  try {
    contenu = prom::lireFichier(prom::cheminFichier(chemin.toStdString()));
  } catch (const std::exception& e) {
    m_atelier.afficherMessage(QStringLiteral("Archive <b>%1</b> illisible : %2").arg(QFileInfo(chemin).fileName().toHtmlEscaped(), QString::fromUtf8(e.what()).toHtmlEscaped()),
                              NiveauMessage::Erreur);
    return {};
  }
  const ImportArchive imp = importerArchiveComposant(contenu, QFileInfo(chemin).fileName().toStdString());
  if (imp.symboles.empty() || !imp.empreinte) {
    QStringList raisons;
    for (const auto& x : imp.alertes) raisons << q(x).toHtmlEscaped();
    m_atelier.afficherMessage(QStringLiteral("Archive <b>%1</b> refusée : %2").arg(QFileInfo(chemin).fileName().toHtmlEscaped(), raisons.isEmpty() ? QStringLiteral("ni symbole ni empreinte lisibles") : raisons.join(QStringLiteral(" ; "))),
                              NiveauMessage::Erreur);
    return {};
  }
  if (!imp.controles.empty()) {
    QStringList raisons;
    for (const auto& x : imp.controles) raisons << q(x).toHtmlEscaped();
    m_atelier.afficherMessage(QStringLiteral("Archive <b>%1</b> refusée par les contrôles de bibliothèque : %2").arg(QFileInfo(chemin).fileName().toHtmlEscaped(), raisons.join(QStringLiteral(" ; "))),
                              NiveauMessage::Erreur);
    return {};
  }
  m_atelier.rangerArchive(imp);
  refaire();
  m_recherche->setText(q(imp.article.id));
  return imp.article.id;
}

void FenetreChoixComposant::dragEnterEvent(QDragEnterEvent* e) {
  if (e->mimeData()->hasUrls())
    for (const QUrl& u : e->mimeData()->urls())
      if (u.isLocalFile() && u.toLocalFile().endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        e->acceptProposedAction();
        return;
      }
}

void FenetreChoixComposant::dropEvent(QDropEvent* e) {
  for (const QUrl& u : e->mimeData()->urls())
    if (u.isLocalFile() && u.toLocalFile().endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) importerArchive(u.toLocalFile());
  e->acceptProposedAction();
}
