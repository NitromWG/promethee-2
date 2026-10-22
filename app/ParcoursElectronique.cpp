// Prométhée : apprendre l'atelier électronique (lot L2, tâche 2.17). Licence GPL-3.0-only.
#include "ParcoursElectronique.hpp"

#include <algorithm>

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include "AideElectronique.hpp"
#include "AtelierElectronique.hpp"
#include "Document.hpp"
#include "InspecteurElectronique.hpp"
#include "promethee/electronique_parcours.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;

namespace {
QString q(const std::string& s) { return QString::fromStdString(s); }
QString h(const std::string& s) { return QString::fromStdString(s).toHtmlEscaped(); }
const QString CLE_PREMIER = QStringLiteral("electronique/premierLancement");
const QString CLE_UNITE = QStringLiteral("electronique/uniteCartesNeuves");
const QString CLE_PROFIL = QStringLiteral("electronique/profilCartesNeuves");
const QString CLE_DOSSIER = QStringLiteral("electronique/dossierBibliotheque");
QLabel* texte(const QString& t = {}) {
  auto* l = inspecteur::texte(t);
  l->setWordWrap(true);
  return l;
}
}  // namespace

PanneauApprendre::PanneauApprendre(AtelierElectronique& a, QWidget* parent) : QWidget(parent), m_atelier(a) {
  setObjectName(QStringLiteral("panneauApprendre"));
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(4, 4, 4, 4);
  m_onglets = new QTabWidget;
  m_onglets->setObjectName(QStringLiteral("ongletsApprendre"));
  col->addWidget(m_onglets);

  // ----- Parcours -----
  auto* page = new QWidget;
  auto* pc = new QVBoxLayout(page);
  // Premier lancement : les préférences des cartes neuves (ERG-16)
  m_premier = new QGroupBox(QStringLiteral("Bienvenue : tes préférences pour les cartes neuves"));
  m_premier->setObjectName(QStringLiteral("premierLancement"));
  auto* f = new QFormLayout(m_premier);
  m_unite = new QComboBox;
  m_unite->setObjectName(QStringLiteral("uniteCartesNeuves"));
  m_unite->addItem(QStringLiteral("millimètres"), QStringLiteral("mm"));
  m_unite->addItem(QStringLiteral("mil (millième de pouce)"), QStringLiteral("mil"));
  m_unite->setCurrentIndex(std::max(0, m_unite->findData(QSettings().value(CLE_UNITE, QStringLiteral("mm")))));
  f->addRow(QStringLiteral("Unité"), m_unite);
  m_profil = new QComboBox;
  m_profil->setObjectName(QStringLiteral("profilCartesNeuves"));
  m_profil->addItem(QStringLiteral("Aucun (règles par défaut)"), QString());
  for (const auto& p : profilsFabricants()) m_profil->addItem(q(p.nom), q(p.nom));
  m_profil->setCurrentIndex(std::max(0, m_profil->findData(QSettings().value(CLE_PROFIL, QString()))));
  m_profil->setToolTip(QStringLiteral("Les règles de dessin d’une carte neuve suivent les capacités de ce fabricant"));
  f->addRow(QStringLiteral("Fabricant"), m_profil);
  auto* dossier = new QHBoxLayout;
  m_dossier = new QLineEdit(QSettings().value(CLE_DOSSIER).toString());
  m_dossier->setObjectName(QStringLiteral("dossierBibliotheque"));
  m_dossier->setPlaceholderText(QStringLiteral("dossier des données de l’application"));
  dossier->addWidget(m_dossier, 1);
  auto* parcourir = new QToolButton;
  parcourir->setText(QStringLiteral("…"));
  parcourir->setToolTip(QStringLiteral("Choisir le dossier de ta bibliothèque personnelle"));
  connect(parcourir, &QToolButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier de la bibliothèque personnelle"), m_dossier->text());
    if (!d.isEmpty()) m_dossier->setText(d);
  });
  dossier->addWidget(parcourir);
  f->addRow(QStringLiteral("Bibliothèque"), dossier);
  auto* valider = new QPushButton(QStringLiteral("Valider"));
  valider->setObjectName(QStringLiteral("validerPremierLancement"));
  connect(valider, &QPushButton::clicked, this, [this] {
    QSettings s;
    s.setValue(CLE_UNITE, m_unite->currentData());
    s.setValue(CLE_PROFIL, m_profil->currentData());
    if (m_dossier->text().trimmed().isEmpty()) s.remove(CLE_DOSSIER);
    else s.setValue(CLE_DOSSIER, QDir::cleanPath(m_dossier->text().trimmed()));
    s.setValue(CLE_PREMIER, true);
    m_premier->hide();
    m_atelier.afficherMessage(QStringLiteral("Préférences gardées : elles valent pour les cartes neuves. Elles se changent ici à tout moment (Apprendre)."));
  });
  f->addRow(QString(), valider);
  m_premier->setVisible(!premierLancementFait());
  pc->addWidget(m_premier);
  // Choix du parcours
  auto* choix = new QHBoxLayout;
  auto* nom = texte();
  if (const ParcoursApprendre* p = parcoursElectronique("premiere-carte"))
    nom->setText(QStringLiteral("<b>%1</b><br>%2").arg(h(p->nom), h(p->description)));
  choix->addWidget(nom, 1);
  m_commencer = new QPushButton(QStringLiteral("Commencer"));
  m_commencer->setObjectName(QStringLiteral("commencerParcours"));
  m_commencer->setToolTip(QStringLiteral("Une carte neuve s’ajoute au projet ; chaque étape se valide toute seule"));
  connect(m_commencer, &QPushButton::clicked, this, [this] { commencer("premiere-carte"); });
  choix->addWidget(m_commencer);
  pc->addLayout(choix);
  // La carte de l'étape
  m_carte = new QWidget;
  m_carte->setObjectName(QStringLiteral("carteEtape"));
  auto* cc = new QVBoxLayout(m_carte);
  cc->setContentsMargins(0, 6, 0, 0);
  m_avancement = texte();
  m_barre = new QProgressBar;
  m_barre->setTextVisible(false);
  m_barre->setMaximumHeight(6);
  m_titre = texte();
  m_titre->setObjectName(QStringLiteral("titreEtape"));
  m_pourquoi = texte();
  m_comment = texte();
  m_mot = texte();
  m_etat = texte();
  for (QWidget* w : {static_cast<QWidget*>(m_avancement), static_cast<QWidget*>(m_barre), static_cast<QWidget*>(m_titre), static_cast<QWidget*>(m_pourquoi),
                     static_cast<QWidget*>(m_comment), static_cast<QWidget*>(m_mot), static_cast<QWidget*>(m_etat)})
    cc->addWidget(w);
  auto* boutons = new QHBoxLayout;
  m_precedent = new QPushButton(QStringLiteral("Précédente"));
  m_suivant = new QPushButton(QStringLiteral("Suivante"));
  m_suivant->setObjectName(QStringLiteral("etapeSuivante"));
  m_quitter = new QPushButton(QStringLiteral("Quitter le parcours"));
  connect(m_precedent, &QPushButton::clicked, this, [this] {
    m_etape = std::max(0, m_etape - 1);
    montrer();
  });
  connect(m_suivant, &QPushButton::clicked, this, [this] {
    ++m_etape;
    montrer();
  });
  connect(m_quitter, &QPushButton::clicked, this, [this] { quitter(); });
  boutons->addWidget(m_precedent);
  boutons->addWidget(m_suivant);
  boutons->addStretch(1);
  boutons->addWidget(m_quitter);
  cc->addLayout(boutons);
  pc->addWidget(m_carte);
  pc->addStretch(1);
  m_carte->hide();
  m_onglets->addTab(page, QStringLiteral("Parcours"));

  // ----- Lexique (ERG-13) -----
  auto* lex = new QWidget;
  auto* lc = new QVBoxLayout(lex);
  m_cherche = new QLineEdit;
  m_cherche->setObjectName(QStringLiteral("chercheLexique"));
  m_cherche->setPlaceholderText(QStringLiteral("Chercher un mot : via, réseau, chevelu…"));
  m_cherche->setClearButtonEnabled(true);
  lc->addWidget(m_cherche);
  m_lexique = new QListWidget;
  m_lexique->setObjectName(QStringLiteral("listeLexique"));
  m_lexique->setWordWrap(true);
  lc->addWidget(m_lexique, 1);
  connect(m_cherche, &QLineEdit::textChanged, this, [this](const QString& t) { remplirLexique(t); });
  remplirLexique(QString());
  m_onglets->addTab(lex, QStringLiteral("Lexique"));
}

bool PanneauApprendre::premierLancementFait() { return QSettings().value(CLE_PREMIER, false).toBool(); }

void PanneauApprendre::montrerPremierLancement() {
  m_premier->show();
  m_onglets->setCurrentIndex(0);
}

void PanneauApprendre::appliquerPreferences(ConceptionElec& c) {
  const QSettings s;
  const QString unite = s.value(CLE_UNITE, QStringLiteral("mm")).toString();
  if (!unite.isEmpty()) c.circuit.affichage.unite = unite.toStdString();
  const std::string profil = s.value(CLE_PROFIL).toString().toStdString();
  for (const auto& p : profilsFabricants())
    if (p.nom == profil) {
      // Les minimums du fabricant relèvent ceux des règles par défaut qui sont en dessous
      Regles& R = c.circuit.regles;
      const Regles& M = p.regles;
      for (auto [valeur, minimum] : {std::pair{&R.isolement, M.isolement}, std::pair{&R.largeurMin, M.largeurMin}, std::pair{&R.percageMin, M.percageMin},
                                     std::pair{&R.anneauMin, M.anneauMin}, std::pair{&R.bord, M.bord}, std::pair{&R.ecartTrous, M.ecartTrous},
                                     std::pair{&R.cuivreTrou, M.cuivreTrou}})
        *valeur = std::max(*valeur, minimum);
      R.largeur = std::max(R.largeur, R.largeurMin);
      R.viaPercage = std::max(R.viaPercage, R.percageMin);
      R.viaDiametre = std::max(R.viaDiametre, R.viaPercage + 2 * R.anneauMin);
      R.profil = p.nom;
    }
}

void PanneauApprendre::remplirLexique(const QString& filtre) {
  m_lexique->clear();
  for (const auto& m : aide::lexique()) {
    const QString mot = QString::fromUtf8(m.mot), def = QString::fromUtf8(m.definition);
    if (!filtre.isEmpty() && !mot.contains(filtre, Qt::CaseInsensitive) && !def.contains(filtre, Qt::CaseInsensitive)) continue;
    auto* it = new QListWidgetItem(QStringLiteral("%1 — %2").arg(mot, def));
    it->setData(Qt::UserRole, mot);
    m_lexique->addItem(it);
  }
}

void PanneauApprendre::commencer(const std::string& id) {
  if (!parcoursElectronique(id)) return;
  ConceptionElec c = nouvelleConception("Première carte");
  appliquerPreferences(c);
  const std::string uuid = c.uuid;
  int indice = -1;
  m_atelier.document()->modifier([&](prom::Projet& p) {
    p.electronique.push_back(c);
    indice = static_cast<int>(p.electronique.size()) - 1;
  }, QStringLiteral("Parcours « Ta première carte »"));
  if (indice < 0) return;
  m_parcours = id;
  m_conception = uuid;
  m_etape = 0;
  m_atelier.ouvrir(indice, AtelierElectronique::Schema);
  m_atelier.montrerBibliotheque();
  montrer();
}

void PanneauApprendre::quitter() {
  m_parcours.clear();
  m_conception.clear();
  m_carte->hide();
  m_commencer->show();
}

void PanneauApprendre::majEtape() {
  const ParcoursApprendre* p = parcoursElectronique(m_parcours);
  const ConceptionElec* c = m_atelier.conceptionCourante();
  if (!p || !c || c->uuid != m_conception || m_etape >= static_cast<int>(p->etapes.size())) return;
  // L'étape montrée, faite, laisse la place à la première qui ne l'est pas
  const EtapeApprendre& e = p->etapes[static_cast<std::size_t>(m_etape)];
  if (e.faite && e.faite(*c)) {
    m_etape = static_cast<int>(etapeEnCours(*p, *c));
    montrer();
    if (m_etape < static_cast<int>(p->etapes.size()) && p->etapes[static_cast<std::size_t>(m_etape)].mode != m_atelier.mode())
      m_atelier.setMode(p->etapes[static_cast<std::size_t>(m_etape)].mode);
  }
}

void PanneauApprendre::montrer() {
  const ParcoursApprendre* p = parcoursElectronique(m_parcours);
  if (!p) {
    quitter();
    return;
  }
  m_commencer->hide();
  m_carte->show();
  const int n = static_cast<int>(p->etapes.size());
  m_etape = std::clamp(m_etape, 0, n);
  m_barre->setRange(0, n);
  m_barre->setValue(m_etape);
  m_precedent->setEnabled(m_etape > 0);
  m_suivant->setEnabled(m_etape < n);
  if (m_etape >= n) {
    m_avancement->setText(QStringLiteral("Parcours terminé"));
    m_titre->setText(QStringLiteral("<b>Bravo : ta première carte est publiée.</b>"));
    m_pourquoi->setText(QStringLiteral("Le dossier de fabrication est prêt à envoyer au fabricant."));
    m_comment->clear();
    m_mot->clear();
    m_etat->clear();
    return;
  }
  const EtapeApprendre& e = p->etapes[static_cast<std::size_t>(m_etape)];
  m_avancement->setText(QStringLiteral("Étape %1 / %2").arg(m_etape + 1).arg(n));
  m_titre->setText(QStringLiteral("<b>%1</b>").arg(h(e.titre)));
  m_pourquoi->setText(h(e.pourquoi));
  m_comment->setText(QStringLiteral("<i>%1</i>").arg(h(e.comment)));
  const QString def = e.mot.empty() ? QString() : aide::definition(q(e.mot));
  m_mot->setText(def.isEmpty() ? QString() : QStringLiteral("<b>%1</b> : %2").arg(h(e.mot), def.toHtmlEscaped()));
  const ConceptionElec* c = m_atelier.conceptionCourante();
  const bool faite = c && c->uuid == m_conception && e.faite && e.faite(*c);
  m_etat->setText(faite ? QStringLiteral("✓ Faite") : QStringLiteral("À faire : l’étape se valide toute seule"));
}
