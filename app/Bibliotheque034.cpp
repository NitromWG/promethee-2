// Prométhée : bibliothèque d'équipe dans l'atelier électronique (lot L19 de la 0.34 ; 19.4). Voir Bibliotheque034.hpp.
// Licence GPL-3.0-only.
#include "Bibliotheque034.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "FenetreOutilsConception.hpp"
#include "promethee/articles.hpp"
#include "promethee/collaboration.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.toStdString(); }
QString nomEtat(EtatDemande e) {
  switch (e) {
    case EtatDemande::Prise: return QStringLiteral("prise");
    case EtatDemande::Publiee: return QStringLiteral("publiée");
    case EtatDemande::Refusee: return QStringLiteral("refusée");
    default: return QStringLiteral("déposée");
  }
}
// Champ affiché de la comparaison → champ corrigeable de l'article
QString champArticle(const QString& affiche) {
  static const std::map<QString, QString> t = {{QStringLiteral("Nom"), QStringLiteral("nom")},
                                               {QStringLiteral("Fabricant"), QStringLiteral("fabricant")},
                                               {QStringLiteral("Référence fabricant"), QStringLiteral("mpn")},
                                               {QStringLiteral("Valeur"), QStringLiteral("valeur")},
                                               {QStringLiteral("Symbole"), QStringLiteral("symbole")},
                                               {QStringLiteral("Empreinte"), QStringLiteral("empreinte")},
                                               {QStringLiteral("Modèle 3D"), QStringLiteral("modele3D")},
                                               {QStringLiteral("Cycle de vie"), QStringLiteral("etatCycle")},
                                               {QStringLiteral("Numéro interne"), QStringLiteral("numeroInterne")}};
  if (const auto it = t.find(affiche); it != t.end()) return it->second;
  if (affiche.startsWith(QStringLiteral("Paramètre "))) return QStringLiteral("parametre:") + affiche.mid(10);
  return {};
}
// Opération de bibliothèque faite, défaite et refaite par la pile de la fenêtre
class CommandeBibliotheque : public QUndoCommand {
 public:
  CommandeBibliotheque(OperationBibliotheque faite, const QString& nom, std::function<void()> apres) : QUndoCommand(nom), m_op(std::move(faite)), m_apres(std::move(apres)) {}
  void undo() override {
    annulerOperation(m_op);
    m_apres();
  }
  void redo() override {
    if (m_premiere) {  // déjà faite quand la commande entre dans la pile
      m_premiere = false;
      return;
    }
    m_op = refaireOperation(m_op);
    m_apres();
  }

 private:
  OperationBibliotheque m_op;
  std::function<void()> m_apres;
  bool m_premiere = true;
};
}  // namespace

FenetreBibliothequeEquipe::FenetreBibliothequeEquipe(AtelierElectronique& a, QWidget* parent) : QWidget(parent, Qt::Window), m_atelier(a) {
  setObjectName(QStringLiteral("fenetreBibliothequeEquipe"));
  setWindowTitle(QStringLiteral("Bibliothèque d’équipe"));
  resize(1000, 700);
  m_pile = new QUndoStack(this);
  auto* l = new QVBoxLayout(this);
  auto* haut = new QHBoxLayout;
  m_lieu = new QLabel;
  m_lieu->setWordWrap(true);
  haut->addWidget(m_lieu, 1);
  auto* dossier = new QPushButton(QStringLiteral("Dossier d’équipe…"));
  connect(dossier, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier d’équipe"), m_dossier);
    if (!d.isEmpty()) choisir(d, m_fichier);
  });
  auto* fichier = new QPushButton(QStringLiteral("Bibliothèque…"));
  connect(fichier, &QPushButton::clicked, this, [this] {
    const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Bibliothèque d’équipe"), m_dossier, QStringLiteral("Bibliothèque (*.prombib *.json)"));
    if (!f.isEmpty()) choisir(m_dossier.isEmpty() ? QFileInfo(f).absolutePath() : m_dossier, f);
  });
  auto* annuler = new QPushButton(QStringLiteral("Annuler"));
  annuler->setObjectName(QStringLiteral("annulerBibliotheque"));
  auto* retablir = new QPushButton(QStringLiteral("Rétablir"));
  retablir->setObjectName(QStringLiteral("retablirBibliotheque"));
  connect(annuler, &QPushButton::clicked, m_pile, &QUndoStack::undo);
  connect(retablir, &QPushButton::clicked, m_pile, &QUndoStack::redo);
  connect(m_pile, &QUndoStack::canUndoChanged, annuler, &QPushButton::setEnabled);
  connect(m_pile, &QUndoStack::canRedoChanged, retablir, &QPushButton::setEnabled);
  annuler->setEnabled(false);
  retablir->setEnabled(false);
  for (QPushButton* b : {dossier, fichier, annuler, retablir}) haut->addWidget(b);
  l->addLayout(haut);
  m_onglets = new QTabWidget;
  l->addWidget(m_onglets, 1);
  auto* demandes = new QWidget;
  demandes->setObjectName(QStringLiteral("onglet-demandes"));
  construireDemandes(demandes);
  m_onglets->addTab(demandes, QStringLiteral("Demandes"));
  auto* sante = new QWidget;
  sante->setObjectName(QStringLiteral("onglet-sante"));
  construireSante(sante);
  m_onglets->addTab(sante, QStringLiteral("Santé"));
  auto* comparer = new QWidget;
  comparer->setObjectName(QStringLiteral("onglet-comparer"));
  construireComparer(comparer);
  m_onglets->addTab(comparer, QStringLiteral("Comparer et fusionner"));
  // Le dossier surveillé : une demande servie ou une bibliothèque republiée se voit tout de suite
  connect(&m_veille, &QFileSystemWatcher::directoryChanged, this, [this] { relireDemandes(); });
  connect(&m_veille, &QFileSystemWatcher::fileChanged, this, [this](const QString& f) {
    if (QFileInfo::exists(f) && !m_veille.files().contains(f)) m_veille.addPath(f);  // un fichier réécrit d'un coup quitte la veille
    relireDemandes();
    majCandidats();
  });
  QSettings r;
  QString d = r.value(QStringLiteral("equipe/dossier")).toString(), f = r.value(QStringLiteral("bibliotheque/equipe")).toString();
  if (d.isEmpty() && !f.isEmpty()) d = QFileInfo(f).absolutePath();
  choisir(d, f);
}

FenetreBibliothequeEquipe::~FenetreBibliothequeEquipe() {
  if (m_fil) {
    *m_arret = true;
    m_fil->wait();
  }
}

void FenetreBibliothequeEquipe::choisir(const QString& dossierEquipe, const QString& fichierBibliotheque) {
  m_dossier = dossierEquipe;
  m_fichier = fichierBibliotheque;
  if (m_fichier.isEmpty() && !m_dossier.isEmpty()) {
    // Première bibliothèque du dossier d'équipe
    const QStringList v = QDir(m_dossier).entryList({QStringLiteral("*.prombib")}, QDir::Files, QDir::Name);
    if (!v.isEmpty()) m_fichier = QDir(m_dossier).filePath(v.front());
  }
  QSettings r;
  if (!m_dossier.isEmpty()) r.setValue(QStringLiteral("equipe/dossier"), m_dossier);
  if (!m_fichier.isEmpty()) r.setValue(QStringLiteral("bibliotheque/equipe"), m_fichier);
  if (!m_veille.directories().isEmpty()) m_veille.removePaths(m_veille.directories());
  if (!m_veille.files().isEmpty()) m_veille.removePaths(m_veille.files());
  if (!m_dossier.isEmpty()) {
    QDir().mkpath(QDir(m_dossier).filePath(QStringLiteral("demandes")));
    m_veille.addPath(QDir(m_dossier).filePath(QStringLiteral("demandes")));
  }
  if (!m_fichier.isEmpty() && QFileInfo::exists(m_fichier)) m_veille.addPath(m_fichier);
  m_lieu->setText(m_dossier.isEmpty() ? QStringLiteral("<span style='color:#B45309'>Choisis le dossier d’équipe : les demandes et le journal de la bibliothèque y vivent.</span>")
                                      : QStringLiteral("Dossier d’équipe : <b>%1</b><br>Bibliothèque : <b>%2</b>")
                                            .arg(m_dossier.toHtmlEscaped(), m_fichier.isEmpty() ? QStringLiteral("aucune") : QFileInfo(m_fichier).fileName().toHtmlEscaped()));
  majChampsImposes();
  relireDemandes();
  majCandidats();
}

bool FenetreBibliothequeEquipe::montrer(const QString& onglet) {
  for (int i = 0; i < m_onglets->count(); ++i)
    if (m_onglets->widget(i)->objectName() == QStringLiteral("onglet-") + onglet) {
      m_onglets->setCurrentIndex(i);
      return true;
    }
  return false;
}

std::vector<Article> FenetreBibliothequeEquipe::articles() const {
  if (m_fichier.isEmpty()) return {};
  return lireFichierBibliotheque(s(m_fichier)).articles;
}

// ======================================================================================================================
// Demandes
// ======================================================================================================================
void FenetreBibliothequeEquipe::construireDemandes(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* f = new QFormLayout;
  m_reference = new QLineEdit;
  m_reference->setObjectName(QStringLiteral("referenceDemande"));
  m_reference->setPlaceholderText(QStringLiteral("référence fabricant (obligatoire)"));
  m_fabricant = new QLineEdit;
  m_fabricant->setObjectName(QStringLiteral("fabricantDemande"));
  m_fiche = new QLineEdit;
  m_fiche->setObjectName(QStringLiteral("ficheDemande"));
  auto* parcourir = new QPushButton(QStringLiteral("…"));
  connect(parcourir, &QPushButton::clicked, this, [this] {
    const QString x = QFileDialog::getOpenFileName(this, QStringLiteral("Fiche technique"), {}, QStringLiteral("Fiches (*.pdf *.html *.htm);;Tous (*)"));
    if (!x.isEmpty()) m_fiche->setText(x);
  });
  auto* ficheL = new QHBoxLayout;
  ficheL->addWidget(m_fiche, 1);
  ficheL->addWidget(parcourir);
  m_commentaire = new QLineEdit;
  m_commentaire->setObjectName(QStringLiteral("commentaireDemande"));
  f->addRow(QStringLiteral("Référence"), m_reference);
  f->addRow(QStringLiteral("Fabricant"), m_fabricant);
  f->addRow(QStringLiteral("Fiche jointe"), ficheL);
  f->addRow(QStringLiteral("Commentaire"), m_commentaire);
  l->addLayout(f);
  m_champs = new QWidget;
  m_champs->setObjectName(QStringLiteral("champsImposes"));
  new QFormLayout(m_champs);
  l->addWidget(m_champs);
  auto* boutons = new QHBoxLayout;
  auto* deposerB = new QPushButton(QStringLiteral("Déposer la demande"));
  deposerB->setObjectName(QStringLiteral("deposerDemande"));
  connect(deposerB, &QPushButton::clicked, this, [this] {
    DemandeArticle d;
    d.reference = s(m_reference->text().trimmed());
    d.fabricant = s(m_fabricant->text().trimmed());
    d.fiche = s(m_fiche->text().trimmed());
    d.commentaire = s(m_commentaire->text().trimmed());
    for (QLineEdit* e : m_champs->findChildren<QLineEdit*>()) d.champs[s(e->property("champ").toString())] = s(e->text().trimmed());
    QString erreur;
    if (deposer(d, &erreur)) {
      m_reference->clear();
      m_fiche->clear();
      m_commentaire->clear();
    } else
      m_etatDemandes->setText(QStringLiteral("<span style='color:#B91C1C'>%1</span>").arg(erreur.toHtmlEscaped()));
  });
  auto* imposer = new QPushButton(QStringLiteral("Champs imposés…"));
  connect(imposer, &QPushButton::clicked, this, [this] {
    QStringList actuels;
    for (const auto& c : champsImposes(s(m_dossier))) actuels << q(c);
    bool ok = false;
    const QString t = QInputDialog::getText(this, QStringLiteral("Champs imposés"), QStringLiteral("Champs que chaque demande doit remplir (séparés par des virgules) :"), QLineEdit::Normal,
                                            actuels.join(QStringLiteral(", ")), &ok);
    if (!ok) return;
    std::vector<std::string> v;
    for (const QString& x : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) v.push_back(s(x.trimmed()));
    imposerChamps(s(m_dossier), v);
    majChampsImposes();
  });
  boutons->addWidget(deposerB);
  boutons->addWidget(imposer);
  boutons->addStretch(1);
  // Bibliothécaire : suivre la demande choisie
  for (const auto& [texte, etat] : std::vector<std::pair<QString, EtatDemande>>{{QStringLiteral("Prendre"), EtatDemande::Prise}, {QStringLiteral("Publier…"), EtatDemande::Publiee},
                                                                              {QStringLiteral("Refuser…"), EtatDemande::Refusee}}) {
    auto* b = new QPushButton(texte);
    const EtatDemande e = etat;
    connect(b, &QPushButton::clicked, this, [this, e] {
      const int r = m_tableDemandes->currentRow();
      if (r < 0 || r >= static_cast<int>(m_demandes.size())) return;
      QString article, raison;
      bool ok = true;
      if (e == EtatDemande::Publiee) article = QInputDialog::getText(this, QStringLiteral("Publier"), QStringLiteral("Identifiant de l’article publié :"), QLineEdit::Normal, {}, &ok);
      if (e == EtatDemande::Refusee) raison = QInputDialog::getText(this, QStringLiteral("Refuser"), QStringLiteral("Raison du refus :"), QLineEdit::Normal, {}, &ok);
      if (ok) suivre(q(m_demandes[static_cast<std::size_t>(r)].uuid), e, article, raison);
    });
    boutons->addWidget(b);
  }
  l->addLayout(boutons);
  m_tableDemandes = new QTableWidget(0, 7);
  m_tableDemandes->setObjectName(QStringLiteral("tableDemandes"));
  m_tableDemandes->setHorizontalHeaderLabels({QStringLiteral("Date"), QStringLiteral("Demandeur"), QStringLiteral("Référence"), QStringLiteral("Fabricant"), QStringLiteral("État"),
                                             QStringLiteral("Bibliothécaire"), QStringLiteral("Article ou raison")});
  m_tableDemandes->horizontalHeader()->setStretchLastSection(true);
  m_tableDemandes->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_tableDemandes->setEditTriggers(QAbstractItemView::NoEditTriggers);
  l->addWidget(m_tableDemandes, 1);
  m_etatDemandes = new QLabel;
  m_etatDemandes->setObjectName(QStringLiteral("etatDemandes"));
  m_etatDemandes->setWordWrap(true);
  l->addWidget(m_etatDemandes);
}

void FenetreBibliothequeEquipe::majChampsImposes() {
  auto* f = qobject_cast<QFormLayout*>(m_champs->layout());
  while (f->rowCount() > 0) f->removeRow(0);
  if (m_dossier.isEmpty()) return;
  for (const auto& c : champsImposes(s(m_dossier))) {
    auto* e = new QLineEdit;
    e->setObjectName(QStringLiteral("champ-") + q(c));
    e->setProperty("champ", q(c));
    e->setPlaceholderText(QStringLiteral("imposé par l’équipe"));
    f->addRow(q(c), e);
  }
}

bool FenetreBibliothequeEquipe::deposer(DemandeArticle d, QString* erreur) {
  if (m_dossier.isEmpty()) {
    if (erreur) *erreur = QStringLiteral("aucun dossier d’équipe");
    return false;
  }
  if (d.demandeur.empty()) d.demandeur = s(Document::personne());
  std::string e;
  if (!deposerDemande(s(m_dossier), d, &e)) {
    if (erreur) *erreur = QStringLiteral("Demande refusée : ") + q(e);
    return false;
  }
  m_etatDemandes->setText(QStringLiteral("Demande déposée : %1 (%2)").arg(q(d.reference).toHtmlEscaped(), q(d.uuid)));
  relireDemandes();
  return true;
}

void FenetreBibliothequeEquipe::relireDemandes() {
  m_demandes = m_dossier.isEmpty() ? std::vector<DemandeArticle>{} : demandesDeposees(s(m_dossier));
  majTableDemandes();
  // Notification au demandeur : ses demandes publiées dont l'article est apparu
  if (m_dossier.isEmpty()) return;
  for (const auto& d : demandesServies(s(m_dossier), s(Document::personne()), articles())) {
    QSettings r;
    QStringList deja = r.value(QStringLiteral("equipe/notifiees")).toStringList();
    if (deja.contains(q(d.uuid)) || m_dejaNotifiees.contains(q(d.uuid))) continue;
    m_dejaNotifiees << q(d.uuid);
    deja << q(d.uuid);
    r.setValue(QStringLiteral("equipe/notifiees"), deja);
    const QString texte = QStringLiteral("L’article demandé %1 est publié dans la bibliothèque d’équipe%2.").arg(q(d.reference), d.article.empty() ? QString() : QStringLiteral(" (") + q(d.article) + QLatin1Char(')'));
    m_notifications << texte;
    emit notification(texte);
    m_atelier.afficherMessage(texte.toHtmlEscaped(), NiveauMessage::Succes);
  }
}

void FenetreBibliothequeEquipe::majTableDemandes() {
  m_tableDemandes->setRowCount(static_cast<int>(m_demandes.size()));
  for (int r = 0; r < static_cast<int>(m_demandes.size()); ++r) {
    const auto& d = m_demandes[static_cast<std::size_t>(r)];
    const QStringList v = {q(d.date), q(d.demandeur), q(d.reference), q(d.fabricant), nomEtat(d.etat), q(d.bibliothecaire), q(d.etat == EtatDemande::Refusee ? d.raison : d.article)};
    for (int c = 0; c < v.size(); ++c) m_tableDemandes->setItem(r, c, new QTableWidgetItem(v[c]));
  }
  m_tableDemandes->resizeColumnsToContents();
}

bool FenetreBibliothequeEquipe::suivre(const QString& uuid, EtatDemande etat, const QString& article, const QString& raison) {
  const bool ok = suivreDemande(s(m_dossier), s(uuid), etat, s(Document::personne()), s(article), s(raison));
  relireDemandes();
  return ok;
}

// ======================================================================================================================
// Santé
// ======================================================================================================================
void FenetreBibliothequeEquipe::construireSante(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* barre = new QHBoxLayout;
  m_lancer = new QPushButton(QStringLiteral("Analyser la bibliothèque"));
  m_lancer->setObjectName(QStringLiteral("lancerSante"));
  m_arreter = new QPushButton(QStringLiteral("Interrompre"));
  m_arreter->setObjectName(QStringLiteral("interrompreSante"));
  m_arreter->setEnabled(false);
  m_progres = new QProgressBar;
  m_progres->setObjectName(QStringLiteral("progresSante"));
  connect(m_lancer, &QPushButton::clicked, this, [this] { lancerSante(); });
  connect(m_arreter, &QPushButton::clicked, this, [this] { interrompreSante(); });
  barre->addWidget(m_lancer);
  barre->addWidget(m_arreter);
  barre->addWidget(m_progres, 1);
  l->addLayout(barre);
  m_resultats = new QTreeWidget;
  m_resultats->setObjectName(QStringLiteral("resultatsSante"));
  m_resultats->setHeaderLabels({QStringLiteral("Article"), QStringLiteral("Genre"), QStringLiteral("Message")});
  connect(m_resultats, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int) {
    const QStringList paire = it->data(0, Qt::UserRole).toStringList();
    if (paire.size() >= 2) {
      comparer(paire[0], paire[1]);
      montrer(QStringLiteral("comparer"));
    }
  });
  l->addWidget(m_resultats, 1);
}

void FenetreBibliothequeEquipe::lancerSante() {
  if (m_fil || m_fichier.isEmpty()) return;
  std::string erreur;
  const ContenuBibliotheque contenu = lireFichierBibliotheque(s(m_fichier), &erreur);
  m_arret = std::make_shared<std::atomic<bool>>(false);
  const auto arret = m_arret;
  const QPointer<FenetreBibliothequeEquipe> moi(this);
  m_progres->setRange(0, std::max<int>(1, static_cast<int>(contenu.articles.size())));
  m_progres->setValue(0);
  m_lancer->setEnabled(false);
  m_arreter->setEnabled(true);
  // Sur son fil : la fenêtre reste vivante, « Interrompre » arrête entre deux articles
  m_fil = QThread::create([contenu, arret, moi] {
    const SanteBibliotheque r = santeBibliotheque(contenu, arret.get(), [moi](int fait, int) {
      QMetaObject::invokeMethod(qApp, [moi, fait] {
        if (moi) moi->m_progres->setValue(fait);
      }, Qt::QueuedConnection);
    });
    QMetaObject::invokeMethod(qApp, [moi, r] {
      if (!moi) return;
      moi->m_sante = r;
      moi->majSante();
    }, Qt::QueuedConnection);
  });
  connect(m_fil, &QThread::finished, this, [this] {
    m_fil->deleteLater();
    m_fil = nullptr;
    m_lancer->setEnabled(true);
    m_arreter->setEnabled(false);
    emit santeTerminee();
  });
  m_fil->start();
}

void FenetreBibliothequeEquipe::interrompreSante() {
  if (m_arret) *m_arret = true;
}

void FenetreBibliothequeEquipe::majSante() {
  m_resultats->clear();
  auto* doublons = new QTreeWidgetItem(m_resultats, {QStringLiteral("Doublons (%1)").arg(m_sante.doublons.size())});
  for (const auto& d : m_sante.doublons) {
    QStringList ids;
    for (const auto& x : d.articles) ids << q(x);
    auto* it = new QTreeWidgetItem(doublons, {ids.join(QStringLiteral(", ")), QStringLiteral("doublon"), q(d.raison)});
    it->setData(0, Qt::UserRole, ids);
    it->setToolTip(0, QStringLiteral("Double-clic : comparer côte à côte"));
  }
  std::map<std::string, QTreeWidgetItem*> genres;
  for (const auto& p : m_sante.problemes) {
    QTreeWidgetItem*& g = genres[p.genre];
    if (!g) g = new QTreeWidgetItem(m_resultats, {q(p.genre)});
    new QTreeWidgetItem(g, {q(p.article), q(p.genre), q(p.message)});
  }
  for (auto& [genre, g] : genres) g->setText(0, QStringLiteral("%1 (%2)").arg(q(genre)).arg(g->childCount()));
  doublons->setExpanded(true);
  m_resultats->resizeColumnToContents(0);
  m_atelier.afficherMessage(QStringLiteral("Santé de la bibliothèque : %1 article(s), %2 doublon(s), %3 problème(s)%4")
                                .arg(m_sante.articles)
                                .arg(m_sante.doublons.size())
                                .arg(m_sante.problemes.size())
                                .arg(m_sante.interrompu ? QStringLiteral(" (analyse interrompue)") : QString()),
                            m_sante.doublons.empty() ? NiveauMessage::Succes : NiveauMessage::Avertissement);
}

// ======================================================================================================================
// Comparer, corriger, fusionner, supprimer
// ======================================================================================================================
void FenetreBibliothequeEquipe::construireComparer(QWidget* page) {
  auto* l = new QVBoxLayout(page);
  auto* choix = new QHBoxLayout;
  m_a = new QComboBox;
  m_a->setObjectName(QStringLiteral("articleA"));
  m_b = new QComboBox;
  m_b->setObjectName(QStringLiteral("articleB"));
  for (QComboBox* c : {m_a, m_b}) {
    c->setEditable(true);
    c->setMinimumContentsLength(24);
  }
  auto* comparerB = new QPushButton(QStringLiteral("Comparer"));
  connect(comparerB, &QPushButton::clicked, this, [this] { comparer(m_a->currentText().section(QLatin1Char(' '), 0, 0), m_b->currentText().section(QLatin1Char(' '), 0, 0)); });
  choix->addWidget(new QLabel(QStringLiteral("A")));
  choix->addWidget(m_a, 1);
  choix->addWidget(new QLabel(QStringLiteral("B")));
  choix->addWidget(m_b, 1);
  choix->addWidget(comparerB);
  l->addLayout(choix);
  m_comparaison = new QTableWidget(0, 3);
  m_comparaison->setObjectName(QStringLiteral("tableComparaison"));
  m_comparaison->setHorizontalHeaderLabels({QStringLiteral("Champ"), QStringLiteral("A"), QStringLiteral("B")});
  m_comparaison->horizontalHeader()->setStretchLastSection(true);
  m_comparaison->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_comparaison->setEditTriggers(QAbstractItemView::NoEditTriggers);
  l->addWidget(m_comparaison, 1);
  auto* actions = new QHBoxLayout;
  auto idA = [this] { return m_a->currentText().section(QLatin1Char(' '), 0, 0); };
  auto idB = [this] { return m_b->currentText().section(QLatin1Char(' '), 0, 0); };
  auto prendre = [this](bool versA) {
    const int r = m_comparaison->currentRow();
    if (r < 0) return;
    const QString champ = champArticle(m_comparaison->item(r, 0)->text());
    if (champ.isEmpty()) return;
    const QString cible = (versA ? m_a : m_b)->currentText().section(QLatin1Char(' '), 0, 0), valeur = m_comparaison->item(r, versA ? 2 : 1)->text();
    corriger(cible, champ, valeur);
  };
  auto* versA = new QPushButton(QStringLiteral("A ← B (champ choisi)"));
  connect(versA, &QPushButton::clicked, this, [prendre] { prendre(true); });
  auto* versB = new QPushButton(QStringLiteral("A → B (champ choisi)"));
  connect(versB, &QPushButton::clicked, this, [prendre] { prendre(false); });
  auto* garderA = new QPushButton(QStringLiteral("Fusionner : garder A"));
  garderA->setObjectName(QStringLiteral("fusionnerGarderA"));
  connect(garderA, &QPushButton::clicked, this, [this, idA, idB] { fusionner(idA(), idB()); });
  auto* garderB = new QPushButton(QStringLiteral("Fusionner : garder B"));
  connect(garderB, &QPushButton::clicked, this, [this, idA, idB] { fusionner(idB(), idA()); });
  auto* supprimerB = new QPushButton(QStringLiteral("Supprimer B"));
  connect(supprimerB, &QPushButton::clicked, this, [this, idB] { supprimer(idB()); });
  for (QPushButton* b : {versA, versB, garderA, garderB, supprimerB}) actions->addWidget(b);
  l->addLayout(actions);
}

void FenetreBibliothequeEquipe::majCandidats() {
  const QString a = m_a->currentText(), b = m_b->currentText();
  m_a->clear();
  m_b->clear();
  for (const auto& x : articles()) {
    const QString t = q(x.id) + QStringLiteral(" — ") + q(x.mpn.empty() ? x.nom : x.mpn);
    m_a->addItem(t);
    m_b->addItem(t);
  }
  m_a->setCurrentText(a);
  m_b->setCurrentText(b);
}

void FenetreBibliothequeEquipe::comparer(const QString& a, const QString& b) {
  const auto v = articles();
  const auto ia = std::find_if(v.begin(), v.end(), [&](const Article& x) { return q(x.id) == a; });
  const auto ib = std::find_if(v.begin(), v.end(), [&](const Article& x) { return q(x.id) == b; });
  m_comparaison->setRowCount(0);
  if (ia == v.end() || ib == v.end()) return;
  for (int i = 0; i < m_a->count(); ++i) {
    if (m_a->itemText(i).section(QLatin1Char(' '), 0, 0) == a) m_a->setCurrentIndex(i);
    if (m_b->itemText(i).section(QLatin1Char(' '), 0, 0) == b) m_b->setCurrentIndex(i);
  }
  const auto lignes = comparerArticles(*ia, *ib);
  m_comparaison->setRowCount(static_cast<int>(lignes.size()));
  for (int r = 0; r < static_cast<int>(lignes.size()); ++r) {
    const auto& l = lignes[static_cast<std::size_t>(r)];
    const QStringList v2 = {q(l.champ), q(l.a), q(l.b)};
    for (int c = 0; c < 3; ++c) {
      auto* it = new QTableWidgetItem(v2[c]);
      if (l.differe) it->setBackground(QColor(0xFE, 0xF3, 0xC7));
      m_comparaison->setItem(r, c, it);
    }
  }
  m_comparaison->resizeColumnsToContents();
}

QStringList FenetreBibliothequeEquipe::projetsCitant(const QString& article) const {
  QStringList r;
  if (m_dossier.isEmpty()) return r;
  const prom::IndexUsages index = prom::indexerUsages({s(m_dossier)});
  const auto it = index.articles.find(s(article));
  if (it == index.articles.end()) return r;
  const QString ouvert = m_atelier.document() ? QFileInfo(m_atelier.document()->chemin()).absoluteFilePath() : QString();
  for (const auto& u : it->second) {
    const QString p = QFileInfo(q(u.projet)).absoluteFilePath();
    if (p != ouvert && !r.contains(p)) r << p;  // le projet ouvert se repointe dans le document, annulable
  }
  return r;
}

bool FenetreBibliothequeEquipe::executer(const OperationBibliotheque& o, const QString& nom) {
  if (!o.fait) {
    m_atelier.afficherMessage(QStringLiteral("%1 impossible : %2").arg(nom, q(o.erreur).toHtmlEscaped()), NiveauMessage::Avertissement);
    return false;
  }
  m_pile->push(new CommandeBibliotheque(o, nom, [this] {
    majCandidats();
    comparer(m_a->currentText().section(QLatin1Char(' '), 0, 0), m_b->currentText().section(QLatin1Char(' '), 0, 0));
  }));
  majCandidats();
  m_atelier.afficherMessage(QStringLiteral("%1 : %2 projet(s) réécrit(s), %3 composant(s) repointé(s) ; inscrit au journal de la bibliothèque").arg(nom).arg(o.projets.size()).arg(o.composants),
                            NiveauMessage::Succes);
  return true;
}

bool FenetreBibliothequeEquipe::corriger(const QString& id, const QString& champ, const QString& valeur) {
  return executer(corrigerArticle(s(m_fichier), s(id), s(champ), s(valeur), s(Document::personne())), QStringLiteral("Correction de %1").arg(id));
}

bool FenetreBibliothequeEquipe::fusionner(const QString& garde, const QString& supprime) {
  std::vector<std::string> projets;
  for (const QString& p : projetsCitant(supprime)) projets.push_back(s(p));
  const auto v = articles();
  const auto ig = std::find_if(v.begin(), v.end(), [&](const Article& x) { return q(x.id) == garde; });
  const OperationBibliotheque o = fusionnerArticles(s(m_fichier), s(garde), s(supprime), projets, s(Document::personne()));
  if (!executer(o, QStringLiteral("Fusion de %1 dans %2").arg(supprime, garde))) return false;
  // Le projet ouvert : repointé dans le document, en un pas d'annulation
  if (ig != v.end() && m_atelier.document()) {
    const Article gardee = *ig;
    bool cite = false;
    for (const auto& c : m_atelier.document()->projet().electronique)
      for (const auto& k : c.composants) cite = cite || k.article == s(supprime) || k.symbole == "art:" + s(supprime);
    if (cite) m_atelier.document()->modifier([&](prom::Projet& p) { repointerArticle(p, gardee, s(supprime)); }, QStringLiteral("Fusion d’articles"));
  }
  return true;
}

bool FenetreBibliothequeEquipe::supprimer(const QString& id) {
  if (m_atelier.document())
    for (const auto& c : m_atelier.document()->projet().electronique)
      for (const auto& k : c.composants)
        if (k.article == s(id) || k.symbole == "art:" + s(id)) {
          m_atelier.afficherMessage(QStringLiteral("Suppression impossible : le projet ouvert cite encore %1 (%2) ; le fusionner d’abord").arg(id, q(k.reference)), NiveauMessage::Avertissement);
          return false;
        }
  std::vector<std::string> projets;
  for (const QString& p : projetsCitant(id)) projets.push_back(s(p));
  if (m_atelier.document() && !m_atelier.document()->chemin().isEmpty()) projets.push_back(s(m_atelier.document()->chemin()));
  return executer(supprimerArticle(s(m_fichier), s(id), projets, s(Document::personne())), QStringLiteral("Suppression de %1").arg(id));
}

// ======================================================================================================================
// Atelier
// ======================================================================================================================
FenetreBibliothequeEquipe* AtelierElectronique::bibliothequeEquipe(const QString& onglet) {
  if (!m_bibliothequeEquipe) m_bibliothequeEquipe = new FenetreBibliothequeEquipe(*this, this);
  if (!onglet.isEmpty()) m_bibliothequeEquipe->montrer(onglet);
  m_bibliothequeEquipe->show();
  m_bibliothequeEquipe->raise();
  return m_bibliothequeEquipe;
}

void AtelierElectronique::verifierDemandesServies() {
  QSettings r;
  const QString dossier = r.value(QStringLiteral("equipe/dossier")).toString(), fichier = r.value(QStringLiteral("bibliotheque/equipe")).toString();
  if (dossier.isEmpty() || fichier.isEmpty() || !QFileInfo::exists(QDir(dossier).filePath(QStringLiteral("demandes")))) return;
  QStringList deja = r.value(QStringLiteral("equipe/notifiees")).toStringList();
  for (const auto& d : demandesServies(s(dossier), s(Document::personne()), lireFichierBibliotheque(s(fichier)).articles)) {
    if (deja.contains(q(d.uuid))) continue;
    deja << q(d.uuid);
    afficherMessage(QStringLiteral("L’article demandé %1 est publié dans la bibliothèque d’équipe.").arg(q(d.reference).toHtmlEscaped()), NiveauMessage::Succes,
                    [this] { bibliothequeEquipe(QStringLiteral("demandes")); });
  }
  r.setValue(QStringLiteral("equipe/notifiees"), deja);
}

void AtelierElectronique::enregistrerCommandesL19() {
  auto& c = m_commandes;
  c["bibliotheque.equipe"] = [this] { bibliothequeEquipe(); };
  c["fabrication.achats"] = [this] {
    ouvrirOutilsConception();
    if (outilsConception()) outilsConception()->montrer(QStringLiteral("achats"));
  };
  c["fabrication.plm"] = [this] {
    ouvrirOutilsConception();
    if (outilsConception()) outilsConception()->montrer(QStringLiteral("plm"));
  };
  // Relu à l'ouverture : les demandes servies depuis la dernière fois
  QTimer::singleShot(0, this, [this] { verifierDemandesServies(); });
}
