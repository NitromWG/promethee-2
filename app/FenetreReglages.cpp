// Prométhée : fenêtre des réglages. Voir FenetreReglages.hpp. Licence GPL-3.0-only.
#include "FenetreReglages.hpp"

#include <algorithm>

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "EditeurRuban.hpp"
#include "Icones.hpp"
#include "NavigationVue.hpp"
#include "RegistreActions.hpp"
#include "RegistreReglages.hpp"
#include "Theme.hpp"
#include "VueSchema.hpp"

namespace {
// Nom d'objet d'une page : « page:souris », « page:apparence »
QString nomPage(const QString& titre) {
  if (titre.startsWith(QStringLiteral("Souris"))) return QStringLiteral("page:souris");
  QString t = titre.toLower().normalized(QString::NormalizationForm_D);
  t.remove(QRegularExpression(QStringLiteral("[^a-z]")));
  return QStringLiteral("page:") + t;
}
// Nom d'objet d'un champ : la fin de sa clé (« navigation/deuxDoigts » → « deuxDoigts »)
QString nomChamp(const QString& cle) { return cle.section(QLatin1Char('/'), -1); }
}  // namespace

FenetreReglages::FenetreReglages(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("fenetreReglages"));
  setWindowTitle(QStringLiteral("Réglages"));
  setModal(false);
  resize(780, 540);
  auto* col = new QVBoxLayout(this);
  // Chercher un réglage (2.32, ERG-38) : par son nom, ses mots voisins ou sa page ; un clic l'ouvre, surligné
  m_recherche = new QLineEdit;
  m_recherche->setObjectName(QStringLiteral("rechercheReglages"));
  m_recherche->setPlaceholderText(QStringLiteral("Chercher un réglage : zoom, réticule, thème, daltonien…"));
  m_recherche->setClearButtonEnabled(true);
  col->addWidget(m_recherche);
  m_trouves = new QListWidget;
  m_trouves->setObjectName(QStringLiteral("reglagesTrouves"));
  m_trouves->setMaximumHeight(130);
  m_trouves->hide();
  col->addWidget(m_trouves);
  connect(m_recherche, &QLineEdit::textChanged, this, [this](const QString& t) {
    m_trouves->clear();
    for (const Reglage* r : RegistreReglages::instance().chercher(t)) {
      auto* it = new QListWidgetItem(QStringLiteral("%1 — %2").arg(r->libelle, r->page), m_trouves);
      it->setData(Qt::UserRole, r->cle);
    }
    m_trouves->setVisible(m_trouves->count() > 0);
  });
  connect(m_trouves, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { montrerReglage(it->data(Qt::UserRole).toString()); });
  connect(m_recherche, &QLineEdit::returnPressed, this, [this] {
    if (m_trouves->count() > 0) montrerReglage(m_trouves->item(0)->data(Qt::UserRole).toString());
  });
  auto* corps = new QHBoxLayout;
  m_liste = new QListWidget;
  m_liste->setObjectName(QStringLiteral("pagesReglages"));
  m_liste->setFixedWidth(200);
  m_pages = new QStackedWidget;
  corps->addWidget(m_liste);
  corps->addWidget(m_pages, 1);
  col->addLayout(corps, 1);
  auto* boutons = new QDialogButtonBox(QDialogButtonBox::Close);
  boutons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Fermer"));
  connect(boutons, &QDialogButtonBox::rejected, this, &QDialog::close);
  col->addWidget(boutons);
  connect(m_liste, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
  // Les pages des réglages déclarés, dans leur ordre ; le ruban après le clavier
  for (const QString& p : RegistreReglages::instance().pages()) {
    ajouterPage(p, pageDeclaree(p));
    if (p == QLatin1String("Clavier")) ajouterPage(QStringLiteral("Ruban"), pageRuban());
  }
  m_liste->setCurrentRow(0);
}

void FenetreReglages::ajouterPage(const QString& titre, QWidget* page) {
  m_liste->addItem(titre);
  if (page->objectName().isEmpty()) page->setObjectName(nomPage(titre));
  auto* defile = new QScrollArea;
  defile->setObjectName(page->objectName());
  defile->setWidgetResizable(true);
  defile->setFrameShape(QFrame::NoFrame);
  page->setObjectName(page->objectName() + QStringLiteral(":contenu"));
  defile->setWidget(page);
  m_pages->addWidget(defile);
}

QWidget* FenetreReglages::pageRuban() {
  auto* e = new EditeurRuban;
  e->setObjectName(QStringLiteral("page:ruban"));
  return e;
}

void FenetreReglages::montrerPage(const QString& nom) {
  for (int i = 0; i < m_pages->count(); ++i)
    if (m_pages->widget(i)->objectName() == nom) m_liste->setCurrentRow(i);
}

void FenetreReglages::montrerReglage(const QString& cle) {
  const Reglage* r = RegistreReglages::instance().reglage(cle);
  if (!r) return;
  montrerPage(nomPage(r->page));
  // Le champ du réglage, surligné deux secondes et amené dans la vue
  for (QWidget* w : findChildren<QWidget*>())
    if (w->property("cleReglage").toString() == cle) {
      if (auto* defile = qobject_cast<QScrollArea*>(m_pages->currentWidget())) defile->ensureWidgetVisible(w);
      w->setFocus(Qt::OtherFocusReason);
      w->setProperty("surligne", true);
      const QString avant = w->styleSheet();
      w->setStyleSheet(avant + QStringLiteral(" * { background: #FFF2B3; border: 2px solid #E0A800; border-radius: 4px; }"));
      m_surligne = w;
      QTimer::singleShot(2000, w, [w, avant] {
        w->setStyleSheet(avant);
        w->setProperty("surligne", false);
      });
      return;
    }
}

QWidget* FenetreReglages::champ(const Reglage& r) {
  RegistreReglages& reg = RegistreReglages::instance();
  const QVariant v = reg.valeur(r.cle);
  QWidget* w = nullptr;
  switch (r.genre) {
    case Reglage::Genre::Booleen: {
      auto* c = new QCheckBox(r.libelle);
      c->setChecked(v.toBool());
      connect(c, &QCheckBox::toggled, this, [cle = r.cle](bool oui) { RegistreReglages::instance().ecrire(cle, oui); });
      w = c;
      break;
    }
    case Reglage::Genre::Entier: {
      auto* s = new QSpinBox;
      s->setRange(static_cast<int>(r.min), static_cast<int>(r.max));
      s->setValue(v.toInt());
      s->setSuffix(r.suffixe);
      connect(s, &QSpinBox::valueChanged, this, [cle = r.cle](int x) { RegistreReglages::instance().ecrire(cle, x); });
      w = s;
      break;
    }
    case Reglage::Genre::Reel: {
      auto* s = new QDoubleSpinBox;
      s->setRange(r.min, r.max);
      s->setSingleStep(r.pas);
      s->setDecimals(2);
      s->setSuffix(r.suffixe);
      s->setValue(v.toDouble());
      connect(s, &QDoubleSpinBox::valueChanged, this, [cle = r.cle](double x) { RegistreReglages::instance().ecrire(cle, x); });
      w = s;
      break;
    }
    case Reglage::Genre::Choix: {
      auto* c = new QComboBox;
      c->addItems(r.choix);
      c->setCurrentIndex(r.valeurs.isEmpty() ? std::clamp(v.toInt(), 0, static_cast<int>(r.choix.size()) - 1) : std::max(0, static_cast<int>(r.valeurs.indexOf(v.toString()))));
      connect(c, &QComboBox::currentIndexChanged, this, [cle = r.cle, valeurs = r.valeurs](int i) {
        RegistreReglages::instance().ecrire(cle, valeurs.isEmpty() ? QVariant(i) : QVariant(valeurs.value(i)));
      });
      w = c;
      break;
    }
    case Reglage::Genre::Texte: {
      auto* e = new QLineEdit(v.toString());
      connect(e, &QLineEdit::editingFinished, this, [cle = r.cle, e] { RegistreReglages::instance().ecrire(cle, e->text().trimmed()); });
      w = e;
      break;
    }
  }
  w->setObjectName(nomChamp(r.cle));
  w->setProperty("cleReglage", r.cle);
  w->setToolTip(r.bulle.isEmpty() ? r.libelle : r.bulle);
  return w;
}

QWidget* FenetreReglages::pageDeclaree(const QString& titre) {
  auto* page = new QWidget;
  page->setObjectName(nomPage(titre));
  auto* col = new QVBoxLayout(page);
  col->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(titre.toHtmlEscaped())));
  auto* f = new QFormLayout;
  for (const Reglage& r : RegistreReglages::instance().tous()) {
    if (r.page != titre) continue;
    QWidget* w = champ(r);
    const QString suffixe = r.redemarrer ? QStringLiteral(" (au prochain lancement)") : QString();
    if (r.genre == Reglage::Genre::Booleen) {
      static_cast<QCheckBox*>(w)->setText(r.libelle + suffixe);
      f->addRow(QString(), w);
    } else
      f->addRow(r.libelle + suffixe, w);
  }
  col->addLayout(f);
  if (titre == QLatin1String("Souris et pavé tactile")) ajouterCarteMarques(col);
  if (titre == QLatin1String("Clavier")) {
    auto* aide = new QLabel(QStringLiteral("Une suite de deux touches (P puis V : poser une via) s’annonce près du curseur après la première ; Maj+A ouvre au curseur "
                                           "le menu Poser. F1 donne toutes les touches de l’atelier et de l’outil en cours."));
    aide->setWordWrap(true);
    col->addWidget(aide);
  }
  if (titre == QLatin1String("Apparence")) ajouterCouleurs(col);
  if (titre == QLatin1String("Session")) ajouterFichiers(col);
  col->addStretch(1);
  return page;
}

// La carte du menu à marques, qui sert d'aide
void FenetreReglages::ajouterCarteMarques(QVBoxLayout* col) {
  const char* fleches[] = {"→", "↗", "↑", "↖", "←", "↙", "↓", "↘"};
  QStringList directions;
  const auto secteurs = NavigationVue::secteursOrigine("objet.miroirX");
  for (std::size_t i = 0; i < secteurs.size(); ++i) directions << QString::fromUtf8(fleches[i]) + QLatin1Char(' ') + secteurs[i].libelle;
  auto* carte = new QLabel(QStringLiteral("<b>Menu à marques</b> : le bouton des gestes tenu immobile montre le menu radial ; un tracé rapide dans une direction "
                                          "lance sa commande sans lui. Le bouton droit tenu immobile le montre aussi ; glissé, il déplace la vue.<br>%1")
                               .arg(directions.join(QStringLiteral(" · "))));
  carte->setObjectName(QStringLiteral("carteMarques"));
  carte->setWordWrap(true);
  col->addWidget(carte);
}

// Apparence (2.31) : chaque couleur des canevas réglable, les fichiers de thème, la lisibilité pour les daltoniens
void FenetreReglages::ajouterCouleurs(QVBoxLayout* col) {
  col->addWidget(new QLabel(QStringLiteral("<b>Couleurs des canevas, une à une</b> — double-clic pour en choisir une autre")));
  auto* liste = new QListWidget;
  liste->setObjectName(QStringLiteral("jetonsCouleur"));
  liste->setMinimumHeight(180);
  auto remplir = [liste] {
    liste->clear();
    for (const auto& j : Theme::jetons()) {
      QPixmap p(16, 16);
      p.fill(theme().jeton(j.cle));
      auto* it = new QListWidgetItem(QIcon(p), j.libelle, liste);
      it->setData(Qt::UserRole, j.cle);
    }
  };
  remplir();
  connect(liste, &QListWidget::itemDoubleClicked, this, [this, remplir](QListWidgetItem* it) {
    const QString cle = it->data(Qt::UserRole).toString();
    const QColor c = QColorDialog::getColor(theme().jeton(cle), this, it->text());
    if (!c.isValid()) return;
    Theme::instance().setJeton(cle, c);
    remplir();
  });
  col->addWidget(liste);
  auto* daltonisme = new QLabel;
  daltonisme->setObjectName(QStringLiteral("verificationDaltonisme"));
  daltonisme->setWordWrap(true);
  auto verifier = [daltonisme] {
    // La sélection du circuit sur chaque cuivre et sur la carte, vue par chaque œil daltonien
    const EncresCircuit& e = theme().circuit();
    const char* noms[] = {"protanopie", "deutéranopie", "tritanopie"};
    double pire = 21;
    QStringList detail;
    for (int d = 0; d < 3; ++d) {
      const auto t = static_cast<Theme::Daltonisme>(d);
      double m = 21;
      for (const QColor& c : {e.cuivreDessus, e.cuivreDessous, e.cuivreInt1, e.cuivreInt2, e.carte}) m = std::min(m, contraste(Theme::simuler(e.choix, t), Theme::simuler(c, t)));
      pire = std::min(pire, m);
      detail << QStringLiteral("%1 %2:1").arg(QString::fromUtf8(noms[d]), QLocale().toString(m, 'f', 1));
    }
    daltonisme->setText(QStringLiteral("<b>Daltonismes</b> : la sélection tient %1:1 au moins sur chaque cuivre (%2) %3")
                            .arg(QLocale().toString(pire, 'f', 1), detail.join(QStringLiteral(", ")), pire >= 3 ? QStringLiteral("✓") : QStringLiteral("— choisis la palette pour daltoniens")));
  };
  verifier();
  connect(&Theme::instance(), &Theme::change, daltonisme, [verifier, remplir] {
    verifier();
    remplir();
  });
  col->addWidget(daltonisme);
  auto* l = new QHBoxLayout;
  auto* effacer = new QPushButton(QStringLiteral("Revenir à la palette"));
  effacer->setObjectName(QStringLiteral("effacerJetons"));
  connect(effacer, &QPushButton::clicked, this, [] { Theme::instance().effacerJetons(); });
  auto* exporter = new QPushButton(QStringLiteral("Exporter le thème…"));
  exporter->setObjectName(QStringLiteral("exporterTheme"));
  connect(exporter, &QPushButton::clicked, this, [this] {
    const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter le thème"), QStringLiteral("mon-theme.promethee-theme"), QStringLiteral("Thèmes de Prométhée (*.promethee-theme)"));
    if (chemin.isEmpty()) return;
    QFile f(chemin);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(theme().exporterTheme()).toJson());
  });
  auto* importer = new QPushButton(QStringLiteral("Importer un thème…"));
  importer->setObjectName(QStringLiteral("importerTheme"));
  connect(importer, &QPushButton::clicked, this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Importer un thème"), QString(), QStringLiteral("Thèmes de Prométhée (*.promethee-theme *.json)"));
    if (chemin.isEmpty()) return;
    QFile f(chemin);
    QString erreur;
    if (!f.open(QIODevice::ReadOnly) || !Theme::instance().importerTheme(QJsonDocument::fromJson(f.readAll()).object(), &erreur))
      QMessageBox::warning(this, QStringLiteral("Importer un thème"), erreur.isEmpty() ? f.errorString() : erreur);
  });
  for (QPushButton* b : {effacer, exporter, importer}) l->addWidget(b);
  l->addStretch(1);
  col->addLayout(l);
}

// Session (2.32, ERG-39) : exporter, relire, remettre à zéro, dossier synchronisé
void FenetreReglages::ajouterFichiers(QVBoxLayout* col) {
  col->addWidget(new QLabel(QStringLiteral("<b>Tous les réglages</b> — raccourcis, ruban et thème compris")));
  auto* l = new QHBoxLayout;
  auto* exporter = new QPushButton(QStringLiteral("Exporter…"));
  exporter->setObjectName(QStringLiteral("exporterReglages"));
  connect(exporter, &QPushButton::clicked, this, [this] {
    const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter les réglages"), QStringLiteral("promethee-reglages.json"), QStringLiteral("Réglages (*.json)"));
    QString erreur;
    if (!chemin.isEmpty() && !RegistreReglages::instance().exporterFichier(chemin, &erreur)) QMessageBox::warning(this, QStringLiteral("Exporter les réglages"), erreur);
  });
  auto* relire = new QPushButton(QStringLiteral("Relire…"));
  relire->setObjectName(QStringLiteral("relireReglages"));
  connect(relire, &QPushButton::clicked, this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Relire des réglages"), QString(), QStringLiteral("Réglages (*.json)"));
    QString erreur;
    if (!chemin.isEmpty() && !RegistreReglages::instance().relireFichier(chemin, &erreur)) QMessageBox::warning(this, QStringLiteral("Relire des réglages"), erreur);
  });
  auto* zero = new QPushButton(QStringLiteral("Tout remettre à zéro…"));
  zero->setObjectName(QStringLiteral("remettreReglages"));
  connect(zero, &QPushButton::clicked, this, [this] {
    if (QMessageBox::question(this, QStringLiteral("Remettre à zéro"), QStringLiteral("Tous les réglages reviennent à leur valeur d’origine : raccourcis, ruban et thème compris. Continuer ?")) ==
        QMessageBox::Yes)
      RegistreReglages::instance().remettreAZero();
  });
  auto* dossier = new QPushButton(QStringLiteral("Dossier synchronisé…"));
  dossier->setObjectName(QStringLiteral("choisirDossierSynchronise"));
  connect(dossier, &QPushButton::clicked, this, [this] {
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier synchronisé des réglages"), RegistreReglages::instance().dossierSynchronise());
    if (d.isEmpty()) return;
    RegistreReglages::instance().setDossierSynchronise(d);
    for (auto* e : findChildren<QLineEdit*>())
      if (e->property("cleReglage").toString() == QLatin1String("reglages/dossierSynchronise")) e->setText(d);
  });
  for (QPushButton* b : {exporter, relire, zero, dossier}) l->addWidget(b);
  l->addStretch(1);
  col->addLayout(l);
}
