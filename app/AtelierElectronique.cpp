// Prométhée : atelier électronique (schéma, circuit imprimé, 3D, fabrication) : construction, moteurs de calcul,
// actions, bibliothèque et inspecteur. Le ruban (RubanElectronique.cpp), les exports (ExportsElectronique.cpp) et la
// publication (PublicationElectronique.cpp) ont leurs fichiers (1.15, A1). Licence GPL-3.0-only.
#include "FilDeFond.hpp"
#include "AtelierElectronique.hpp"
#include "Automatisation034.hpp"
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include "FenetresBibliotheque.hpp"
#include "FenetresMeca034.hpp"
#include "promethee/distances3d.hpp"
#include "ChienDeGarde.hpp"

#include <algorithm>
#include <array>
#include <future>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <utility>

#include <QApplication>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QRegularExpression>
#include <QInputDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QHideEvent>
#include <QShowEvent>
#include <QMessageBox>
#include <QPointer>
#include <QStandardPaths>
#include <QFileDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QDockWidget>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "ChampValeur.hpp"
#include "DialogueComposant.hpp"
#include "DialogueEmpreinte.hpp"
#include "DescriptionRuban.hpp"
#include "DispositionsAtelier.hpp"
#include "FiltreSelection.hpp"
#include "NavigationVue.hpp"
#include "promethee/kicad_electronique.hpp"
#include "Document.hpp"
#include "SelectionElectronique.hpp"
#include "Icones.hpp"
#include "InspecteurElectronique.hpp"
#include "PanneauRecherche.hpp"
#include "ParcoursElectronique.hpp"
#include "DialogueRemplacer.hpp"
#include "PanneauEcarts.hpp"
#include "promethee/remplacement.hpp"
#include "promethee/synchronisation.hpp"
#include "PanneauVerifications.hpp"
#include "CadreVue.hpp"
#include "PeripheriquesEntree.hpp"
#include "ZoneMessages.hpp"
#include "PanneauBibliotheque.hpp"
#include "PanneauAPlacer.hpp"
#include "PanneauReseaux.hpp"
#include "DialogueCercle.hpp"
#include "FenetreRapport.hpp"
#include "promethee/placement_circuit.hpp"
#include "ListeRaccourcis.hpp"
#include "RegistreActions.hpp"
#include "RemplacerPage.hpp"
#include "TableauChamps.hpp"
#include "TableauObjets.hpp"
#include "MosaiqueVues.hpp"
#include "VuePiece.hpp"
#include "Theme.hpp"
#include "VueSchema.hpp"
#include "promethee/bibliotheque.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/hauteurs.hpp"
#include "promethee/verification.hpp"
#include "promethee/carte3d.hpp"
#include "promethee/construction.hpp"
#include "promethee/modele.hpp"

using namespace prom::ecad;

namespace {
// Ruban aux couleurs du thème (P15) : relu à chaque changement de thème, jamais retraduit par le filtre
QString styleRuban() {
  const CouleursInterface& c = Theme::instance().c();
  return QStringLiteral("QTabWidget::pane { border: none; }"
                        "QTabBar::tab { padding: 6px 14px; font-weight: 600; color: %1; border: none; }"
                        "QTabBar::tab:selected { color: %2; border-bottom: 3px solid %3; }"
                        "QToolBar { border: none; spacing: 2px; background: %4; }"
                        "QToolButton { border-radius: 6px; padding: 3px; color: %2; }"
                        "QToolButton:hover { background: %5; }"
                        "QToolButton:checked { background: %6; }"
                        "QToolButton::menu-button { border: none; background: transparent; width: 14px; }")
      .arg(c.texteSecondaire.name(), c.texte.name(), c.accentTexte.name(), c.surface.name(), c.survol.name(), c.choisi.name());
}
QString q(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

AtelierElectronique::AtelierElectronique(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(0, 0, 0, 0);
  col->setSpacing(0);
  m_ruban = new QTabWidget;
  m_ruban->setDocumentMode(true);
  m_ruban->setProperty("themeFixe", true);
  m_ruban->setStyleSheet(styleRuban());
  connect(&Theme::instance(), &Theme::change, m_ruban, [r = m_ruban] { r->setStyleSheet(styleRuban()); });
  m_ruban->setMaximumHeight(108);
  m_ruban->installEventFilter(this);  // sa largeur replie ses groupes (2.15)
  col->addWidget(m_ruban);
  // Panneaux (1.21, ERG-23) : une fenêtre principale imbriquée, les vues au centre
  m_fenetre = new QMainWindow;
  m_fenetre->setObjectName(QStringLiteral("fenetrePanneaux"));
  m_fenetre->setWindowFlags(Qt::Widget);
  m_fenetre->setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
  // Les messages en bas restent sous le canevas : les côtés gardent toute la hauteur
  m_fenetre->setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  m_fenetre->setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
  m_vues = new MosaiqueVues;
  m_schema = new VueSchema(m_doc);
  m_schema->setVerification(&m_ercVerifiee, false);  // ERC à trois niveaux (S9)
  m_circuit = new VueCircuit(m_doc);
  // Noms accessibles des canevas (G11, ERG-05)
  m_schema->setAccessibleName(QStringLiteral("Feuille du schéma"));
  m_schema->setAccessibleDescription(QStringLiteral("Schéma électronique. Au clavier : flèches pour mener le composant à poser, Entrée pour le poser, Ctrl+K pour toutes les commandes."));
  m_circuit->setAccessibleName(QStringLiteral("Circuit imprimé"));
  m_circuit->setAccessibleDescription(QStringLiteral("Carte : empreintes, pistes, vias et zones. Ctrl+K pour toutes les commandes."));
  // Une sélection pour le schéma, le circuit et le panneau (1.5)
  m_selection = new SelectionElectronique(this);
  m_schema->setSelectionPartagee(m_selection);
  m_circuit->setSelectionPartagee(m_selection);
  connect(m_selection, &SelectionElectronique::surlignageChange, this, [this] { annoncerSurlignage(); });
  // Pinceau armé (SAI-41) : chaque composant choisi ensuite reçoit les propriétés du modèle, en un pas
  connect(m_selection, &SelectionElectronique::changee, this, [this] {
    if (m_pinceau.empty()) return;
    std::vector<std::string> cibles;
    for (const auto& id : m_selection->composants())
      if (id != m_pinceau) cibles.push_back(id);
    if (cibles.empty()) return;
    const ComposantElec modele = m_modelePinceau;
    QTimer::singleShot(0, this, [this, cibles, modele] {
      int n = 0;
      modifierConception([&](ConceptionElec& c) {
        for (const auto& id : cibles) n += prom::ecad::appliquerPinceau(c, modele, id, {});
      }, QStringLiteral("Pinceau : propriétés de %1").arg(q(modele.reference)));
      (void)n;
    });
  });
  m_3d = new VuePiece;
  m_3d->setPlansVisibles(false);
  m_vues->ajouter(m_schema);
  m_vues->ajouter(m_circuit);
  m_vues->ajouter(m_3d);
  m_tableauObjets = new TableauObjets(*this);
  m_vues->ajouter(m_tableauObjets, false);
  {  // Ligne de commande sous le canevas (13.8)
    auto* centre = new QWidget;
    auto* pile = new QVBoxLayout(centre);
    pile->setContentsMargins(0, 0, 0, 0);
    pile->setSpacing(0);
    pile->addWidget(m_vues, 1);
    m_ligneCommandes = new LigneCommandes(*this, centre);
    pile->addWidget(m_ligneCommandes);
    m_fenetre->setCentralWidget(centre);
  }
  // Mosaïque : un clic dans la vue d'un autre mode y passe (le ruban suit) ; la sélection croisée y cadre l'objet
  connect(m_vues, &MosaiqueVues::activeDemandee, this, [this](QWidget* v) {
    if (v == m_schema) setMode(Schema);
    else if (v == m_circuit) setMode(Circuit);
    else if (v == m_3d) setMode(Vue3D);
  });
  connect(m_selection, &SelectionElectronique::changee, this, [this] {
    if (m_vues->mosaique()) QTimer::singleShot(0, this, [this] { cadrerVuesPassives(); });
  });
  auto panneau = [this](const char* nom, const QString& titre, QWidget* contenu) {
    auto* d = new QDockWidget(titre, m_fenetre);
    d->setObjectName(QLatin1String(nom));
    d->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    d->setWidget(contenu);
    return d;
  };
  // Bibliothèque ancrée à gauche, repliable (B5) ; inspecteur stable, une page par genre de sélection (1.9, ERG-01)
  m_panneauBibliotheque = new PanneauBibliotheque(*this);
  connect(m_panneauBibliotheque, &PanneauBibliotheque::replier, this, &AtelierElectronique::replierBibliotheque);
  m_inspecteur = new InspecteurElectronique(*this);
  m_bibliothequeOuverte = QSettings().value(QStringLiteral("electronique/bibliothequeOuverte"), true).toBool();
  // Messages (1.11) : bilan permanent dans la barre d'état de l'atelier, messages passagers empilés en bas à
  // gauche du canevas, panneau Messages ; aucun n'écrase les autres (S29), et la barre reste à l'atelier dans la
  // fenêtre à onglets (G8)
  m_journal = new JournalMessages(this);
  auto* messages = new PanneauMessages(*m_journal);
  QLabel* attente = inspecteur::texte(QStringLiteral("L’arbre du projet paraît ici quand l’atelier est ouvert."));
  attente->setAlignment(Qt::AlignCenter);
  m_attenteProjet = attente;
  m_dockProjet = panneau("dockProjet", QStringLiteral("Projet"), m_attenteProjet);
  m_dockBibliotheque = panneau("dockBibliotheque", QStringLiteral("Bibliothèque"), m_panneauBibliotheque);
  m_dockInspecteur = panneau("dockInspecteur", QStringLiteral("Inspecteur"), m_inspecteur);
  m_dockMessages = panneau("dockMessages", QStringLiteral("Messages"), messages);
  // Vérifications (2.11) : ERC et DRC groupées par cause, en bas avec les messages, fermé jusqu'à « Vérifier »
  m_verifications = new PanneauVerifications(*this);
  m_dockVerifications = panneau("dockVerifications", QStringLiteral("Vérifications"), m_verifications);
  connect(m_dockVerifications, &QDockWidget::visibilityChanged, this, [this](bool oui) {
    if (oui) m_verifications->majPlusTard();
  });
  // Chercher (2.12) : en bas, avec les messages et les vérifications, fermé jusqu'à Ctrl+F
  m_recherche = new PanneauRecherche(*this);
  m_dockRecherche = panneau("dockRecherche", QStringLiteral("Chercher"), m_recherche);
  // Écarts avec le schéma (3.8) : en bas, avec les vérifications ; la pastille de la barre d'état l'ouvre
  m_ecarts = new PanneauEcarts(*this);
  m_dockEcarts = panneau("dockEcarts", QStringLiteral("Écarts"), m_ecarts);
  connect(m_ecarts, &PanneauEcarts::ouvrirDemande, this, &AtelierElectronique::montrerEcarts);
  connect(m_dockEcarts, &QDockWidget::visibilityChanged, this, [this](bool oui) {
    if (oui) m_ecarts->majPlusTard();
  });
  // Tableau des champs (3.9) : sous le canevas, avec les écarts ; chaque modification s'écrit aussitôt, en un pas
  m_tableauChamps = new TableauChamps;
  m_dockChamps = panneau("dockChamps", QStringLiteral("Champs"), m_tableauChamps);
  connect(m_tableauChamps, &TableauChamps::modifie, this, [this] {
    const std::vector<ComposantElec> base = m_tableauChamps->base();
    if (const int n = appliquerChamps(m_tableauChamps->composants(), &base); n == 0) majTableauChamps();
  });
  connect(m_tableauChamps, &TableauChamps::porteeChangee, this, &AtelierElectronique::majTableauChamps);
  connect(m_dockChamps, &QDockWidget::visibilityChanged, this, [this](bool oui) {
    if (oui) majTableauChamps();
  });
  connect(m_selection, &SelectionElectronique::changee, this, [this] {
    if (m_tableauChamps->portee() == TableauChamps::Portee::Selection) majTableauChamps();
  });
  // « À placer » (5.2) : à gauche, avec le projet ; les empreintes rangées hors de la carte, par feuille
  m_aPlacer = new PanneauAPlacer(*this);
  m_dockAPlacer = panneau("dockAPlacer", QStringLiteral("À placer"), m_aPlacer);
  connect(m_dockAPlacer, &QDockWidget::visibilityChanged, this, [this](bool oui) {
    if (oui) m_aPlacer->refaire();
  });
  connect(m_doc, &Document::change, m_aPlacer, [this] {
    if (m_dockAPlacer && m_dockAPlacer->isVisible()) m_aPlacer->refaire();
  });
  // Réseaux (5.15, 5.32) : à gauche, avec le projet ; un réseau cliqué est surligné au schéma et au circuit
  m_reseaux = new PanneauReseaux(*this);
  m_dockReseaux = panneau("dockReseaux", QStringLiteral("Réseaux"), m_reseaux);
  connect(m_dockReseaux, &QDockWidget::visibilityChanged, this, [this](bool oui) {
    if (oui) m_reseaux->refaire();
  });
  // Apprendre (2.17) : à droite, avec l'inspecteur, fermé sauf au premier lancement
  m_apprendre = new PanneauApprendre(*this);
  m_dockApprendre = panneau("dockApprendre", QStringLiteral("Apprendre"), m_apprendre);
  m_dockBibliotheque->installEventFilter(this);
  m_fenetre->addDockWidget(Qt::LeftDockWidgetArea, m_dockProjet);
  m_fenetre->splitDockWidget(m_dockProjet, m_dockBibliotheque, Qt::Horizontal);
  m_fenetre->addDockWidget(Qt::RightDockWidgetArea, m_dockInspecteur);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockMessages);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockVerifications);
  m_fenetre->tabifyDockWidget(m_dockMessages, m_dockVerifications);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockRecherche);
  m_fenetre->tabifyDockWidget(m_dockVerifications, m_dockRecherche);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockEcarts);
  m_fenetre->tabifyDockWidget(m_dockRecherche, m_dockEcarts);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockChamps);
  m_fenetre->tabifyDockWidget(m_dockEcarts, m_dockChamps);
  m_fenetre->addDockWidget(Qt::RightDockWidgetArea, m_dockApprendre);
  m_fenetre->tabifyDockWidget(m_dockInspecteur, m_dockApprendre);
  m_fenetre->addDockWidget(Qt::LeftDockWidgetArea, m_dockAPlacer);
  m_fenetre->tabifyDockWidget(m_dockProjet, m_dockAPlacer);
  m_fenetre->addDockWidget(Qt::LeftDockWidgetArea, m_dockReseaux);
  m_fenetre->tabifyDockWidget(m_dockAPlacer, m_dockReseaux);
  m_dockMessages->hide();
  m_dockVerifications->hide();
  m_dockRecherche->hide();
  m_dockEcarts->hide();
  m_dockChamps->hide();
  m_dockApprendre->hide();
  m_dockAPlacer->hide();
  m_dockReseaux->hide();
  m_dockInspecteur->raise();
  m_dispositions = new DispositionsAtelier(m_fenetre, m_doc, QStringLiteral("electronique"), [this] { dispositionOrigine(); }, this);
  for (QDockWidget* d : {m_dockProjet, m_dockBibliotheque, m_dockInspecteur, m_dockMessages, m_dockVerifications, m_dockRecherche, m_dockApprendre, m_dockEcarts, m_dockChamps, m_dockAPlacer, m_dockReseaux}) m_dispositions->suivre(d);
  // La bibliothèque, que le mode cache hors du schéma : « ouverte » est le choix de l'utilisateur, gardé avec la
  // disposition
  m_dispositions->setComplement([this] { return QJsonObject{{QStringLiteral("bibliotheque"), m_bibliothequeOuverte}}; },
                                [this](const QJsonObject& o) {
                                  if (o.contains(QStringLiteral("bibliotheque"))) {
                                    m_bibliothequeOuverte = o.value(QStringLiteral("bibliotheque")).toBool(true);
                                    QSettings().setValue(QStringLiteral("electronique/bibliothequeOuverte"), m_bibliothequeOuverte);
                                  }
                                  // La place de la bibliothèque est rendue ; sa visibilité suit le mode
                                  majBibliotheque();
                                });
  m_dispositions->setOuverture([this](const QDockWidget* d) { return d == m_dockBibliotheque ? m_bibliothequeOuverte : d->isVisible(); },
                               [this](QDockWidget* d, bool oui) {
                                 if (d == m_dockBibliotheque) oui ? montrerBibliotheque() : replierBibliotheque();
                                 else if (d == m_dockMessages && oui) ouvrirMessages();
                                 else {
                                   d->setVisible(oui);
                                   if (oui) d->raise();
                                 }
                               });
  col->addWidget(m_fenetre, 1);
  m_barreEtat = new BarreEtatAtelier(*m_journal);
  m_barreEtat->setPanneau(messages);
  connect(m_barreEtat, &BarreEtatAtelier::messagesDemandes, this, &AtelierElectronique::ouvrirMessages);
  m_barreEtat->ajouter(new BoutonFiltre(m_selection));
  m_barreEtat->ajouter(m_ecarts->pastille());  // « 3 écarts avec le schéma » (3.8)
  {
    // Indicateur de zoom (2.12, S14) : le taux de la taille réelle ; un clic offre les cadrages
    m_zoom = new QToolButton;
    m_zoom->setObjectName(QStringLiteral("indicateurZoom"));
    m_zoom->setAutoRaise(true);
    m_zoom->setPopupMode(QToolButton::InstantPopup);
    m_zoom->setToolTip(QStringLiteral("Zoom, en pourcentage de la taille réelle ; molette, + et −"));
    auto* menu = new QMenu(m_zoom);
    menu->setObjectName(QStringLiteral("menuZoom"));
    auto ligne = [this, menu](const QString& texte, const char* action) {
      QAction* x = menu->addAction(texte, this, [this, action] { executerAction(action); });
      marquerAction(x, action);
    };
    ligne(QStringLiteral("Tout voir\tOrigine"), "vue.toutVoir");
    ligne(QStringLiteral("Zoom sur la sélection\tZ"), "vue.zoomSelection");
    menu->addAction(QStringLiteral("Taille réelle (100 %)"), this, [this] {
      const double reel = pixelsParMm(this);
      if (mode() == Schema && m_schema->echelle() > 0) m_schema->zoomer(reel / m_schema->echelle());
    });
    ligne(QStringLiteral("Vue d’ensemble"), "vue.vueEnsemble");
    // Cadrages nommés (2.29) : enregistrer celui-ci, rappeler les autres
    menu->addSeparator();
    ligne(QStringLiteral("Enregistrer ce cadrage…"), "vue.cadrageEnregistrer");
    auto* cadrages = menu->addMenu(QStringLiteral("Cadrages"));
    cadrages->setObjectName(QStringLiteral("menuCadrages"));
    connect(cadrages, &QMenu::aboutToShow, this, [this, cadrages] {
      cadrages->clear();
      const QStringList noms = m_schema->cadrages();
      if (noms.isEmpty()) cadrages->addAction(QStringLiteral("(aucun cadrage enregistré)"))->setEnabled(false);
      for (const QString& n : noms) cadrages->addAction(n, this, [this, n] { m_schema->rappelerCadrage(n); });
    });
    ligne(QStringLiteral("Étalonner l’écran…"), "vue.etalonner");
    m_zoom->setMenu(menu);
    m_barreEtat->ajouter(m_zoom);
  }
  {
    // Coordonnées (2.28, ERG-24) : le point visé et sa place relative ; un clic offre l'origine, le repère, le
    // réticule et les grilles
    m_coordonnees = new QToolButton;
    m_coordonnees->setObjectName(QStringLiteral("coordonnees"));
    m_coordonnees->setAutoRaise(true);
    m_coordonnees->setPopupMode(QToolButton::InstantPopup);
    m_coordonnees->setToolTip(QStringLiteral("Coordonnées du point visé ; relatives à l’origine posée d’un clic, ou au dernier point du tracé"));
    auto* menu = new QMenu(m_coordonnees);
    menu->setObjectName(QStringLiteral("menuCoordonnees"));
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
      menu->clear();
      menu->addAction(QStringLiteral("Poser l’origine relative d’un clic"), this, [this] { m_schema->armerOrigineRelative(); })->setObjectName(QStringLiteral("poserOrigine"));
      if (m_schema->origineRelative()) menu->addAction(QStringLiteral("Effacer l’origine relative"), this, [this] { m_schema->effacerOrigineRelative(); });
      menu->addSeparator();
      for (const bool polaire : {false, true}) {
        QAction* x = menu->addAction(polaire ? QStringLiteral("Relatif en polaire (longueur et angle)") : QStringLiteral("Relatif en cartésien (ΔX, ΔY)"), this, [this, polaire] {
          VueSchema::setCoordonneesPolaires(polaire);
          majCoordonnees();
        });
        x->setCheckable(true);
        x->setChecked(VueSchema::coordonneesPolaires() == polaire);
      }
      QMenu* reticule = menu->addMenu(QStringLiteral("Réticule"));
      const char* styles[] = {"Petit", "Sur toute la vue", "Aucun"};
      for (int i = 0; i < 3; ++i) {
        QAction* x = reticule->addAction(QString::fromUtf8(styles[i]), this, [this, i] {
          VueSchema::setStyleReticule(static_cast<VueSchema::Reticule>(i));
          m_schema->update();
        });
        x->setCheckable(true);
        x->setChecked(static_cast<int>(VueSchema::styleReticule()) == i);
      }
      QMenu* grille = menu->addMenu(QStringLiteral("Grille"));
      QMenu* textes = menu->addMenu(QStringLiteral("Grille des textes"));
      for (const auto& g : VueSchema::grillesNommees()) {
        QAction* x = grille->addAction(g.nom, this, [this, n = g.nom] { m_schema->choisirGrille(n); });
        x->setCheckable(true);
        x->setChecked(m_schema->grilleActive().nom == g.nom);
        QAction* y = textes->addAction(g.nom, this, [this, n = g.nom] { m_schema->choisirGrilleTextes(n); });
        y->setCheckable(true);
        y->setChecked(m_schema->grilleTextes().nom == g.nom);
      }
      for (int i = 0; i < 2; ++i) {
        QMenu* rapide = menu->addMenu(QStringLiteral("Grille rapide %1 : %2").arg(i + 1).arg(VueSchema::grilleRapide(i)));
        for (const auto& g : VueSchema::grillesNommees()) rapide->addAction(g.nom, this, [i, n = g.nom] { VueSchema::setGrilleRapide(i, n); });
      }
      menu->addAction(QStringLiteral("Grilles…"), this, [this] { m_schema->dialogueGrilles(); });
    });
    m_coordonnees->setMenu(menu);
    m_barreEtat->ajouter(m_coordonnees);
    connect(m_schema, &VueSchema::coordonneesChangees, this, [this] { majCoordonnees(); });
  }
  {
    // Disposition des panneaux : les montrer ou les cacher, enregistrer, rappeler, partager
    auto* bouton = new QToolButton;
    bouton->setObjectName(QStringLiteral("boutonDispositions"));
    bouton->setText(QStringLiteral("Panneaux"));
    bouton->setToolTip(QStringLiteral("Montrer ou cacher les panneaux, enregistrer leur disposition et la rappeler"));
    bouton->setAutoRaise(true);
    bouton->setPopupMode(QToolButton::InstantPopup);
    // Les vues côte à côte et le tableau des objets : des actions du registre, au menu des panneaux
    m_actionMosaique = new QAction(QStringLiteral("Schéma et circuit côte à côte"), this);
    m_actionMosaique->setObjectName(QStringLiteral("actionMosaique"));
    m_actionMosaique->setCheckable(true);
    marquerAction(m_actionMosaique, "vue.mosaique");
    connect(m_actionMosaique, &QAction::toggled, this, [this](bool oui) { setMosaique(oui); });
    m_actionTableauObjets = new QAction(QStringLiteral("Tableau de tous les objets"), this);
    m_actionTableauObjets->setObjectName(QStringLiteral("actionTableauObjets"));
    m_actionTableauObjets->setCheckable(true);
    marquerAction(m_actionTableauObjets, "vue.tableauObjets");
    connect(m_actionTableauObjets, &QAction::toggled, this, [this](bool oui) { montrerTableauObjets(oui); });
    auto* menu = new QMenu(bouton);
    menu->setObjectName(QStringLiteral("menuPanneaux"));
    connect(menu, &QMenu::aboutToShow, this, [this, menu] {
      m_dispositions->remplir(menu);
      // Les vues de l'atelier (3.21) : une à la fois, ou côte à côte
      menu->addSection(QStringLiteral("Vues"));
      {
        const QSignalBlocker b1(m_actionMosaique), b2(m_actionTableauObjets);
        m_actionMosaique->setChecked(m_vues->mosaique());
        m_actionTableauObjets->setChecked(m_vues->visibleDansMosaique(m_tableauObjets));
      }
      menu->addAction(m_actionMosaique);
      QAction* d = menu->addAction(QStringLiteral("La 3D aussi"), this, [this](bool oui) { montrerVue3DDansMosaique(oui); });
      d->setCheckable(true);
      d->setChecked(m_vues->mosaique() && m_vues->montree(m_3d));
      menu->addAction(m_actionTableauObjets);
    });
    bouton->setMenu(menu);
    m_barreEtat->ajouter(bouton);
  }
  connect(m_barreEtat, &BarreEtatAtelier::lienBilan, this, &AtelierElectronique::allerAuProbleme);
  col->addWidget(m_barreEtat);
  m_pile = new PileMessages(m_vues);
  m_minuterie = new QTimer(this);
  m_minuterie->setSingleShot(true);
  m_minuterie->setInterval(40);
  connect(m_minuterie, &QTimer::timeout, this, [this] { recalculerApresModification(); });
  construireRuban();
  // Le ruban suit sa description : changée dans les réglages, il se reconstruit
  connect(&RubanEnCours::instance(), &RubanEnCours::change, this, [this] { construireRuban(); });
  connect(m_ruban, &QTabWidget::currentChanged, this, [this](int i) {
    majBibliotheque();
    if (i == Schema) m_vues->setCurrentWidget(m_schema);
    else if (i == Vue3D) { m_vues->setCurrentWidget(m_3d); majVue3D(); }
    else m_vues->setCurrentWidget(m_circuit);
    // Le calcul ne repart que si le dernier résultat n'est plus celui de la conception montrée : sans modification
    // depuis, passer d'une vue à l'autre est immédiat (ERG-10 : F8 sous 100 ms). Le schéma ne lit que la
    // connectivité et l'ERC ; le circuit d'une grande carte se montre aussitôt avec son dernier état, le calcul
    // complet (zones, DRC) part en fond comme après un geste (9.4)
    const ConceptionElec* c = conceptionCourante();
    const bool rienEnAttente = c && c->uuid == m_conceptionCalculee && m_enAttente.vide() && !m_calculEnCours && !m_minuterie->isActive();
    const bool grande = c && estGrandeCarte(*c);
    if ((i == Schema && rienEnAttente) || (i != Schema && rienEnAttente && !m_circuitPerime && m_niveauCalcul == 2)) {
      majEtatCalcul();  // résultats à jour : le bilan de la barre d'état suit la vue montrée
    } else if (grande && i == Schema) {
      recalculerSchema();
    } else if (grande) {
      ++m_generation;
      if (!m_calculEnCours) lancerCalcul();
      majEtatCalcul();
    } else {
      recalculer();
    }
    // Le passage au circuit refait la vérification complète du schéma (S9)
    if (i == Circuit) retenirVerification();
    majZoom();
    majPanneau();
  });
  // Changement du projet entier (ouverture, annulation d'un pas du projet, autre atelier) : tout est à refaire
  connect(m_doc, &Document::change, this, [this] {
    m_3dAJour = false;
    m_enAttente.entiere = true;
    perimerVerification();
    if (isVisible()) m_minuterie->start(40);
  });
  // Changement d'une conception (A12) : seuls les moteurs qu'il touche repartent ; une valeur changée ne relance ni
  // la connectivité ni les zones
  connect(m_doc, &Document::conceptionChangee, this, [this](const ChangementConception& ch) {
    const ConceptionElec* c = conceptionCourante();
    if (!c || (!ch.conception.empty() && ch.conception != c->uuid)) return;
    m_enAttente.fusionner(ch);
    perimerVerification();
    if (ch.demandeCuivre() || ch.proprietes.count("hauteur") || ch.proprietes.count("empreinte")) m_3dAJour = false;
    // Des appuis sur les flèches font un seul pas ; le calcul attend qu'ils s'arrêtent (300 ms, C18)
    if (isVisible()) m_minuterie->start(m_doc->fusionOuverte().startsWith(QLatin1String("fleches")) ? 300 : 40);
  });
  // Ce qui a disparu (suppression, annulation, autre vue) sort de la sélection ; après l'annulation d'un pas, la
  // sélection d'avant ce pas revient ; un autre projet repart de l'état neutre (S24)
  connect(m_doc, &Document::conceptionChangee, this, [this](const ChangementConception& ch) {
    const ConceptionElec* c = conceptionCourante();
    // Pendant un glissement rien ne disparaît : sélection et inspecteur attendent la fin du geste (9.4)
    if (m_doc->enGeste()) return;
    if (c && (ch.conception.empty() || ch.conception == c->uuid)) {
      m_selection->filtrer(*c);
      // L'inspecteur relit ses champs, sans toucher à celui qui a le focus (P1)
      m_inspecteur->relire();
      // Symboles propres au projet : la bibliothèque les range dans « Dans ce projet »
      if (ch.definitions || ch.entiere) m_panneauBibliotheque->refaire();
      if (ch.proprietes.count("schemaSeul") || ch.entiere) majSchemaSeul();
    }
  });
  connect(m_doc, &Document::change, this, [this] {
    if (const ConceptionElec* c = conceptionCourante()) m_selection->filtrer(*c);
    else m_selection->neutre();
    m_inspecteur->montrer();
    m_panneauBibliotheque->refaire();
    majSchemaSeul();
  });
  connect(m_doc, &Document::selectionRendue, this, [this](const std::string& conception, const std::vector<std::string>& sel) {
    const ConceptionElec* c = conceptionCourante();
    if (!c || c->uuid != conception) return;
    m_selection->restaurer(sel, [c](const SelectionElectronique::Ref& r) { return SelectionElectronique::existe(*c, r); });
  });
  connect(m_doc, &Document::projetCharge, this, [this] {
    m_selection->neutre();
    m_schema->setOutil(VueSchema::Outil::Selection);
    m_circuit->setOutil(VueCircuit::Outil::Selection);
  });
  m_doc->setSuiviSelection([this](const std::string& conception) {
    const ConceptionElec* c = conceptionCourante();
    return c && c->uuid == conception ? m_selection->instantane() : std::vector<std::string>{};
  });
  // Un clic sur la feuille ne referme plus la bibliothèque (B5) : la sélection ne touche que l'inspecteur
  connect(m_schema, &VueSchema::selectionChangee, this, [this] { majPanneau(); });
  connect(m_schema, &VueSchema::vueChangee, this, [this] {
    majZoom();
    if (m_tableauChamps && m_tableauChamps->portee() == TableauChamps::Portee::Feuille) majTableauChamps();  // une autre feuille
  });
  // Le panneau Chercher ouvert suit la conception (au retour à la boucle : un clic dans sa liste n'est pas défait) ;
  // l'étape du parcours faite passe à la suivante
  connect(m_doc, &Document::conceptionChangee, this, [this] {
    if (m_dockRecherche && m_dockRecherche->isVisible()) QMetaObject::invokeMethod(m_recherche, [this] { m_recherche->majResultats(); }, Qt::QueuedConnection);
    if (m_apprendre && !m_apprendre->parcours().empty()) QMetaObject::invokeMethod(m_apprendre, [this] { m_apprendre->majEtape(); }, Qt::QueuedConnection);
  });
  connect(m_schema, &VueSchema::message, this, [this](const QString& t) { afficherMessage(t); });
  connect(m_schema, &VueSchema::proprietesDemandees, this, [this] { majPanneau(); });
  connect(m_schema, &VueSchema::bibliothequeDemandee, this, [this] { montrerBibliotheque(true); });
  // « Montrer sur le circuit » (menu contextuel) : l'empreinte choisie, la vue centrée sur elle
  connect(m_schema, &VueSchema::circuitDemande, this, [this](const QString& composant) {
    setMode(Circuit);
    const std::string id = composant.toStdString();
    m_circuit->choisir(VueCircuit::Genre::Empreinte, id);
    if (const ConceptionElec* c = conceptionCourante())
      for (const auto& e : c->circuit.empreintes)
        if (e.composant == id) m_circuit->centrerSur(e.position);
  });
  connect(m_schema, &VueSchema::proposition, this, [this](const QString& html, std::function<void()> faire) { afficherMessage(html, NiveauMessage::Information, std::move(faire)); });
  connect(m_schema, &VueSchema::inspecteurDemande, this, &AtelierElectronique::ouvrirInspecteur);
  connect(m_circuit, &VueCircuit::inspecteurDemande, this, &AtelierElectronique::ouvrirInspecteur);
  // Une ligne d'un menu ouvert au clavier (Maj+A : Poser) : l'atelier fait l'action, commande ou outil (ERG-03)
  connect(m_schema, &VueSchema::actionDemandee, this, [this](const QString& id) { executerAction(id.toStdString()); });
  connect(m_circuit, &VueCircuit::actionDemandee, this, [this](const QString& id) { executerAction(id.toStdString()); });
  connect(m_circuit, &VueCircuit::selectionChangee, this, [this] { majPanneau(); });
  connect(m_circuit, &VueCircuit::message, this, [this](const QString& t) { afficherMessage(t); });
  connect(m_doc, &Document::refus, this, [this](const QString& t) {
    if (isVisible()) afficherMessage(t.toHtmlEscaped(), NiveauMessage::Erreur);
  });
  majBibliotheque();
  // Touches de l'atelier, que les canevas ne gardent jamais (S22) : F8 et F1, aux raccourcis du registre
  const RegistreActions& registre = RegistreActions::atelierElectronique();
  auto raccourcisAtelier = [&](const std::string& id, std::function<void()> f) {
    if (const ActionAtelier* a = registre.action(id))
      for (const QKeySequence& k : a->raccourcis)
        if (!k.isEmpty()) {
          auto* s = new QShortcut(k, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
          s->setObjectName(QString::fromStdString(id));
          connect(s, &QShortcut::activated, this, f);
        }
  };
  raccourcisAtelier("atelier.synchroniser", [this] { mettreAJourCircuit(); });
  raccourcisAtelier("atelier.raccourcis", [this] { montrerRaccourcis(); });
  // F7 « Vérifier », F4 « Problème suivant » (2.11), où que soit le focus dans l'atelier
  raccourcisAtelier("schema.erc", [this] { executerAction("schema.erc"); });
  raccourcisAtelier("verif.suivant", [this] { executerAction("verif.suivant"); });
  raccourcisAtelier("edition.chercher", [this] { executerAction("edition.chercher"); });
  // Échap où que soit le focus dans l'atelier (S21, charte) ; le canevas qui a le focus le garde pour lui
  raccourcisAtelier("commun.echap", [this] { echap(); });
  // Suppr et R agissent aussi quand le focus est ailleurs que dans un champ de saisie (S21) ; un champ garde ses
  // frappes (il les réclame avant les raccourcis), le canevas qui a le focus les traite lui-même
  for (const ActionAtelier& a : registre.actions())
    if (a.horsCanevas)
      for (const QKeySequence& k : a.raccourcis)
        if (!k.isEmpty()) {
          auto* s = new QShortcut(k, this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
          s->setObjectName(QString::fromStdString(a.identifiant));
          connect(s, &QShortcut::activated, this, [this, id = a.identifiant, k] { actionHorsCanevas(id, k); });
        }
}

void AtelierElectronique::actionHorsCanevas(const std::string& action, const QKeySequence& touche) {
  // Un tableau ou une liste (champs de l'inspecteur, bibliothèque) garde le sens qu'il donne à la touche
  // La touche rendue à la liste ne repasse pas par ce raccourci (la liste qui l'ignore la ferait remonter ici)
  static bool rendue = false;
  QWidget* f = QApplication::focusWidget();
  for (QWidget* w = f; w && w != this; w = w->parentWidget())
    if (qobject_cast<QAbstractItemView*>(w)) {
      if (rendue) return;
      const QKeyCombination c = touche[0];
      QKeyEvent appui(QEvent::KeyPress, c.key(), c.keyboardModifiers());
      rendue = true;
      QApplication::sendEvent(f, &appui);
      rendue = false;
      return;
    }
  if (mode() == Schema) m_schema->executerAction(action);
  else if (mode() == Circuit) m_circuit->executerAction(action);
}

ListeRaccourcis* AtelierElectronique::listeRaccourcis() const { return m_raccourcis; }

// F1 : ce qui sert d'abord en tête, l'outil en cours, puis la vue montrée, les deux canevas, l'atelier et Prométhée
void AtelierElectronique::montrerRaccourcis() {
  const RegistreActions& r = RegistreActions::atelierElectronique();
  const bool circuit = mode() == Circuit;
  const PorteeAction vue = circuit ? PorteeAction::Circuit : PorteeAction::Schema;
  auto prend = [&](const std::string& id) { return circuit ? m_circuit->outilPrend(id) : m_schema->outilPrend(id); };
  ListeRaccourcis::Section outil{QStringLiteral("Outil en cours · %1").arg(circuit ? m_circuit->nomOutil() : m_schema->nomOutil()), {}};
  ListeRaccourcis::Section propre{circuit ? QStringLiteral("Circuit") : QStringLiteral("Schéma"), {}};
  ListeRaccourcis::Section commun{QStringLiteral("Schéma et circuit"), {}};
  ListeRaccourcis::Section atelier{QStringLiteral("Atelier électronique"), {}};
  ListeRaccourcis::Section partout{QStringLiteral("Partout dans Prométhée"), {}};
  for (const ActionAtelier& a : r.actions()) {
    if (a.cachee) continue;
    if ((a.portee == PorteeAction::Canevas || a.portee == vue) && prend(a.identifiant)) outil.actions.push_back(&a);
    else if (a.portee == vue) propre.actions.push_back(&a);
    else if (a.portee == PorteeAction::Canevas) commun.actions.push_back(&a);
    else if (a.portee == PorteeAction::Atelier) atelier.actions.push_back(&a);
    else if (a.portee == PorteeAction::Partout) partout.actions.push_back(&a);
  }
  if (!m_raccourcis) m_raccourcis = new ListeRaccourcis(this);
  m_raccourcis->montrer({outil, propre, commun, atelier, partout});
  // La carte du menu à marques (1.19) : le bouton des gestes tenu, ou un tracé rapide dans une direction
  const char* boutons[] = {"le bouton droit avec Ctrl", "un bouton latéral", "le bouton du milieu"};
  QStringList directions;
  const char* fleches[] = {"→", "↗", "↑", "↖", "←", "↙", "↓", "↘"};
  const auto secteurs = NavigationVue::secteursOrigine(circuit ? "circuit.changerFace" : "objet.miroirX");
  for (std::size_t i = 0; i < secteurs.size(); ++i) directions << QString::fromUtf8(fleches[i]) + QLatin1Char(' ') + secteurs[i].libelle;
  m_raccourcis->setNote(QStringLiteral("<b>Menu à marques</b> : %1 tenu immobile montre le menu ; un tracé rapide lance la commande sans lui. Le bouton droit tenu "
                                       "immobile le montre aussi.<br>%2")
                            .arg(QString::fromUtf8(boutons[static_cast<int>(Navigation::instance().reglages().gestes)]), directions.join(QStringLiteral(" · "))));
}

// E : l'inspecteur montre la sélection, le curseur dans son premier champ modifiable
void AtelierElectronique::ouvrirInspecteur() {
  majPanneau();
  QTimer::singleShot(0, this, [this] {
    if (!m_inspecteur->page()) return;
    for (QWidget* w : m_inspecteur->page()->findChildren<QWidget*>()) {
      if (!w->isVisible() || !w->isEnabled()) continue;
      auto* l = qobject_cast<QLineEdit*>(w);
      if ((l && !l->isReadOnly()) || qobject_cast<QAbstractSpinBox*>(w)) {
        w->setFocus(Qt::ShortcutFocusReason);
        if (l) l->selectAll();
        return;
      }
    }
  });
}

// Dans la fenêtre à onglets aussi, la barre d'état reste à l'atelier : rien n'y est coupé sur une ligne (G8)
void AtelierElectronique::setEtatIntegre(bool) {}

// Échap (charte) : un champ de saisie rend d'abord la main au canevas, sans rien changer ; sinon l'état neutre de
// la vue montrée (outil Sélection, rien de choisi, rien de surligné)
void AtelierElectronique::echap() {
  if (!m_pinceau.empty()) terminerPinceau();
  QWidget* f = QApplication::focusWidget();
  for (QWidget* w = f; w && w != this; w = w->parentWidget())
    if (qobject_cast<QLineEdit*>(w) || qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QPlainTextEdit*>(w) || qobject_cast<QTextEdit*>(w)) {
      // Un champ de l'inspecteur reprend la valeur d'avant la saisie (P2)
      if (auto* champ = qobject_cast<ChampTexteLie*>(w)) champ->renoncer();
      if (mode() == Schema) m_schema->setFocus(Qt::OtherFocusReason);
      else if (mode() == Circuit) m_circuit->setFocus(Qt::OtherFocusReason);
      return;
    }
  if (mode() == Schema) m_schema->echap();
  else if (mode() == Circuit) m_circuit->echap();
  else m_selection->neutre();
}
int AtelierElectronique::mode() const { return m_ruban->currentIndex(); }

const ConceptionElec* AtelierElectronique::conceptionCourante() const {
  const auto& p = m_doc->projet();
  return m_conception >= 0 && m_conception < static_cast<int>(p.electronique.size()) ? &p.electronique[static_cast<std::size_t>(m_conception)] : nullptr;
}

// Une modification de la conception ouverte, en un pas qui ne photographie qu'elle (A2)
bool AtelierElectronique::modifierConception(const std::function<void(ConceptionElec&)>& f, const QString& nom, const QString& fusion) {
  return m_doc->modifierConception(m_conception, f, nom, fusion);
}

void AtelierElectronique::ouvrir(int conception, int mode, int feuille) {
  const bool autre = conception != m_conception;
  m_conception = conception;
  if (autre) m_selection->neutre();
  m_schema->setConception(conception, feuille);
  m_circuit->setConception(conception);
  m_3dAJour = false;
  recalculer();
  // À l'ouverture, les écarts se calculent et se montrent ; rien n'est appliqué (« 5 écarts avec le schéma »)
  if (m_ecarts) m_ecarts->majPlusTard();
  majTableauChamps();
  if (const ConceptionElec* c = conceptionCourante(); c && c->schemaSeul) mode = Schema;  // STR-13 : pas de circuit à montrer
  majSchemaSeul();
  if (m_ruban->currentIndex() != mode) m_ruban->setCurrentIndex(mode);
  else majPanneau();
  majCouches();
  if (autre) m_panneauBibliotheque->refaire();
  majBibliotheque();
  if (autre) QTimer::singleShot(0, this, [this] { m_schema->recadrer(); m_circuit->recadrer(); });
}

// ---------- Moteurs ----------
// Résultat d'un calcul : niveau 0 schéma (connectivité, ERC), 1 geste (chevelu sur les zones données), 2 complet
namespace {
constexpr std::size_t GRANDE_CARTE = 200;  // empreintes : au-delà, le calcul d'un geste part en fond (9.4)
}

struct AtelierElectronique::Resultat {
  int niveau = 2;
  std::uint64_t generation = 0;
  VueCircuit::Calculs calculs;
  std::vector<Violation> erc;
  // Écarts schéma ↔ circuit d'une grande carte, comptés sur le fil du calcul (9.2)
  std::optional<std::vector<EcartSynchro>> ecarts;
};

namespace {
AtelierElectronique::Resultat calculerConception(const ConceptionElec& c, int niveau, const Remplissages* zones, std::uint64_t generationDoc = ~std::uint64_t{0}) {
  AtelierElectronique::Resultat r;
  r.niveau = niveau;
  r.calculs.k = connectivite(c);
  if (niveau == 0) {
    r.erc = verifierERC(c, r.calculs.k, NiveauErc::Complet);  // gravités réglées et exclusions du projet appliquées
    return r;
  }
  // L'ERC sur un autre fil pendant le cuivre (9.2) : elle ne lit que la conception et la connectivité
  auto erc = std::async(std::launch::async, [&c, &r] { return verifierERC(c, r.calculs.k, NiveauErc::Complet); });
  if (niveau == 2 && c.circuit.empreintes.size() >= GRANDE_CARTE) {
    r.ecarts = c.schemaSeul ? std::vector<EcartSynchro>{} : ecartsSynchronisation(c, r.calculs.k);
    if (generationDoc != ~std::uint64_t{0}) {
      r.calculs.textes = textesSerigraphie(c);
      r.calculs.textesValides = true;
      r.calculs.generationDoc = generationDoc;
    }
  }
  r.calculs.pastilles = pastilles(c, r.calculs.k);
  r.calculs.zones = niveau == 2 || !zones ? remplirZones(c, r.calculs.k) : *zones;
  if (niveau == 2) {
    // La DRC calcule l'état du routage en même temps que ses contrôles et le rend : il n'est pas refait
    const EtatRoutageRendu rendu(r.calculs.etat);
    r.calculs.drc = verifierDRCReglee(c, r.calculs.k, r.calculs.zones);  // gravités et exclusions du projet (2.33)
    // Distances exactes en 3D (6.7, 6.12) : boîtier du commerce importé et écarts entre corps, sur ce même fil
    for (auto& v : prom::verifierDistances3D(c)) r.calculs.drc.push_back(std::move(v));
  } else
    r.calculs.etat = etatRoutage(c, r.calculs.k, r.calculs.zones);
  r.erc = erc.get();
  return r;
}
}  // namespace

AtelierElectronique::~AtelierElectronique() {
  if (m_fil.joinable()) m_fil.join();
}

void AtelierElectronique::recalculer() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  ++m_generation;  // un calcul en fond encore en route arrivera périmé
  Resultat r = calculerConception(*c, 2, nullptr);
  r.generation = m_generation;
  appliquerResultat(std::move(r));
}

bool AtelierElectronique::estGrandeCarte(const ConceptionElec& c) { return c.circuit.empreintes.size() >= GRANDE_CARTE; }

void AtelierElectronique::recalculerSchema() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  ++m_generation;
  Resultat r = calculerConception(*c, 0, nullptr);
  r.generation = m_generation;
  m_circuitPerime = true;
  appliquerResultat(std::move(r));
}

void AtelierElectronique::recalculerApresModification() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  // Pendant un glissement, écarts, tableaux et liste « À placer » attendent la fin du geste (9.4)
  if (!m_doc->enGeste()) {
    // La pastille des écarts suit chaque modification ; ceux d'une grande carte sont comptés avec le calcul en fond
    if (m_ecarts && (mode() == Schema || c->circuit.empreintes.size() < GRANDE_CARTE)) m_ecarts->majPlusTard();
    majTableauChamps();
    if (m_tableauObjets && m_vues->visibleDansMosaique(m_tableauObjets)) m_tableauObjets->recharger();
    if (m_aPlacer && m_dockAPlacer && !m_dockAPlacer->isHidden()) m_aPlacer->refaire();
    if (m_affectation && m_affectation->isVisible()) m_affectation->refaire();
  }
  const ChangementConception ch = std::exchange(m_enAttente, ChangementConception{});
  // Une valeur, un fabricant, une hauteur, le cartouche : la connectivité, le cuivre et la DRC restent valables ;
  // seule l'ERC peut voir un brochage vérifié ou une empreinte (A12). Un calcul en fond part d'un état plus ancien :
  // il est alors relancé normalement.
  if (!ch.vide() && !ch.demandeConnectivite() && !ch.demandeCuivre() && !m_calculEnCours) {
    if (m_ecarts && !(mode() == Schema || c->circuit.empreintes.size() < GRANDE_CARTE)) m_ecarts->majPlusTard();
    if (ch.demandeErc()) {
      m_erc = verifierERC(*c, m_calculs.k, NiveauErc::Complet);
      m_schema->setViolations(&m_erc);
      if (m_verifications && m_dockVerifications->isVisible()) m_verifications->majPlusTard();
    }
    afficherEtat(m_niveauCalcul);
    return;
  }
  ++m_generation;
  if (mode() == Schema) {
    // Le schéma ne lit que la connectivité et l'ERC : le cuivre attend le retour au circuit
    Resultat r = calculerConception(*c, 0, nullptr);
    r.generation = m_generation;
    m_circuitPerime = true;
    appliquerResultat(std::move(r));
    return;
  }
  if (m_doc->enGeste()) {
    // Pendant un glissement : chevelu et pastilles à jour, zones et DRC à la fin du geste. Sur une grande carte, ce
    // calcul part en fond (9.4) : la vue garde ses 60 images par seconde, le chevelu suit quand il arrive.
    if (c->circuit.empreintes.size() >= GRANDE_CARTE) {
      if (!m_calculEnCours) lancerCalcul(1);
      return;
    }
    Resultat r = calculerConception(*c, 1, &m_calculs.zones);
    r.generation = m_generation;
    appliquerResultat(std::move(r));
    return;
  }
  if (!m_calculEnCours) lancerCalcul();
  majEtatCalcul();
}

void AtelierElectronique::lancerCalcul(int niveau) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  if (m_fil.joinable()) m_fil.join();  // le fil précédent a déjà rendu son résultat
  m_calculEnCours = true;
  // Au-delà de 300 ms, la vue montre la flèche avec sablier (charte) ; la saisie reste possible
  QTimer::singleShot(300, this, [this, g = m_generation] {
    if (m_calculEnCours && m_generation == g) {
      m_schema->setCalculLong(true);
      m_circuit->setCalculLong(true);
    }
  });
  const std::uint64_t generation = m_generation;
  QPointer<AtelierElectronique> moi(this);
  auto zones = niveau == 1 ? std::make_shared<Remplissages>(m_calculs.zones) : std::shared_ptr<Remplissages>();
  m_fil = std::thread([copie = *c, generation, moi, niveau, zones, generationDoc = m_circuit->generationDoc()] {
    prioriteDeFond();  // la saisie garde la main pendant la DRC (9.2)
    auto r = std::make_shared<Resultat>(calculerConception(copie, niveau, zones.get(), generationDoc));
    r->generation = generation;
    QMetaObject::invokeMethod(qApp, [moi, r] {
      if (!moi) return;
      moi->m_calculEnCours = false;
      moi->m_schema->setCalculLong(false);
      moi->m_circuit->setCalculLong(false);
      const bool geste = moi->m_doc->enGeste();
      // Un calcul de geste arrive pendant le geste : il s'applique s'il est le dernier ; sinon on repart du dernier état
      if (r->generation == moi->m_generation && (r->niveau == 2 || geste)) moi->appliquerResultat(std::move(*r));
      else {
        if (r->niveau == 1 && geste) moi->appliquerResultat(std::move(*r));  // chevelu presque à jour, mieux que rien
        moi->lancerCalcul(geste ? 1 : 2);
      }
    }, Qt::QueuedConnection);
  });
}

void AtelierElectronique::majEtatCalcul() { afficherEtat(m_niveauCalcul); }

void AtelierElectronique::appliquerResultat(Resultat&& r) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  m_calculs.k = std::move(r.calculs.k);
  m_erc = std::move(r.erc);
  m_conceptionCalculee = c->uuid;
  if (r.niveau >= 1) {
    m_calculs.pastilles = std::move(r.calculs.pastilles);
    m_calculs.zones = std::move(r.calculs.zones);
    m_calculs.etat = std::move(r.calculs.etat);
  }
  m_calculs.textes = std::move(r.calculs.textes);
  m_calculs.textesValides = r.calculs.textesValides;
  m_calculs.generationDoc = r.calculs.generationDoc;
  if (r.niveau == 2) {
    m_calculs.drc = std::move(r.calculs.drc);
    // La carte dans le boîtier qu'elle pilote (6.4) : hauteur sous le couvercle, collisions
    for (auto& v : prom::verifierCarteDansBoitier(m_doc->projet(), static_cast<std::size_t>(std::max(0, m_conception)))) m_calculs.drc.push_back(std::move(v));
    m_circuitPerime = false;
  }
  m_schema->setConnectivite(&m_calculs.k);
  m_schema->setViolations(&m_erc);
  // Une autre conception : sa vérification complète reste à faire
  if (c->uuid != m_conceptionVerifiee && m_verificationFaite) {
    m_ercVerifiee.clear();
    m_verificationFaite = m_verificationPerimee = false;
    m_schema->setVerification(&m_ercVerifiee, false);
  }
  m_circuit->setCalculs(&m_calculs);
  if (r.ecarts && m_ecarts) m_ecarts->setEcarts(std::move(*r.ecarts));
  majCouches();
  m_niveauCalcul = r.niveau;
  afficherEtat(r.niveau);
  if (r.niveau == 2 && mode() == Vue3D && !m_3dAJour) majVue3D();
  // L'inspecteur lit les réseaux, l'ERC et la DRC : ce qui en dépend se relit, sans reconstruire la page (P1)
  m_inspecteur->resultats();
  if (m_verifications && m_dockVerifications->isVisible()) m_verifications->majPlusTard();
  if (m_reseaux && m_dockReseaux && m_dockReseaux->isVisible() && r.niveau >= 1) m_reseaux->refaire();
}

// ---------- Vérification à trois niveaux (2.11, S9) ----------
void AtelierElectronique::retenirVerification() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  m_ercVerifiee.clear();
  for (const auto& v : m_erc)
    if (v.niveau == NiveauErc::Complet) m_ercVerifiee.push_back(v);
  m_conceptionVerifiee = c->uuid;
  m_verificationFaite = true;
  m_verificationPerimee = false;
  m_schema->setVerification(&m_ercVerifiee, false);
}

void AtelierElectronique::perimerVerification() {
  if (!m_verificationFaite || m_verificationPerimee) return;
  m_verificationPerimee = true;
  m_schema->setVerification(&m_ercVerifiee, true);
  if (m_verifications && m_dockVerifications->isVisible()) m_verifications->majPlusTard();
}

void AtelierElectronique::verifier() {
  if (!conceptionCourante()) return;
  recalculer();  // ERC et DRC complètes, sur-le-champ
  retenirVerification();
  montrerVerifications();
  afficherEtat(m_niveauCalcul);
}

void AtelierElectronique::montrerApprendre() {
  m_dockApprendre->show();
  m_dockApprendre->raise();
}

void AtelierElectronique::chercher() {
  m_dockRecherche->show();
  m_dockRecherche->raise();
  m_recherche->majResultats();
  m_recherche->focaliser();
}

// Le zoom du schéma, en pourcentage de la taille réelle (densité de l'écran) ; caché hors du schéma
double AtelierElectronique::pixelsParMm(const QWidget* w) {
  // La densité que l'écran déclare, corrigée une fois à la règle (2.29, ERG-30)
  const QScreen* e = w ? w->screen() : nullptr;
  return (e ? e->physicalDotsPerInch() : 96.0) / 25.4 * QSettings().value(QStringLiteral("ecran/etalonnage"), 1.0).toDouble();
}

void AtelierElectronique::majCoordonnees() {
  if (!m_coordonnees) return;
  m_coordonnees->setVisible(mode() == Schema);
  if (mode() == Schema) m_coordonnees->setText(m_schema->texteCoordonnees());
}

void AtelierElectronique::majZoom() {
  if (!m_zoom) return;
  m_zoom->setVisible(mode() == Schema);
  const double reel = pixelsParMm(this);
  m_zoom->setText(QStringLiteral("%1 %").arg(std::lround(100.0 * m_schema->echelle() / std::max(reel, 1e-6))));
  majCoordonnees();
}

void AtelierElectronique::montrerVerifications() {
  m_dockVerifications->show();
  m_dockVerifications->raise();
  m_verifications->majResultats();
}

std::vector<Violation> AtelierElectronique::ercMontree() const {
  std::vector<Violation> r;
  for (const auto& v : m_erc)
    if (v.niveau != NiveauErc::Complet) r.push_back(v);
  r.insert(r.end(), m_ercVerifiee.begin(), m_ercVerifiee.end());
  std::stable_sort(r.begin(), r.end(), [](const Violation& a, const Violation& b) { return a.gravite < b.gravite; });
  return r;
}

// Bilan permanent (1.11) : compteurs cliquables, les erreurs en tête ; « niveau » < 2 : DRC en attente. Il n'est
// jamais remplacé par un message passager (S29).
void AtelierElectronique::afficherEtat(int niveau) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  auto compte = [](const std::vector<Violation>& v, Gravite g) { return static_cast<int>(std::count_if(v.begin(), v.end(), [g](const Violation& x) { return x.gravite == g; })); };
  const CouleursInterface& t = Theme::instance().c();
  // Un compteur de violations : « ERC ✓ », « ERC 2 ● 1 ▲ » ; lien vers la première
  auto compteur = [&](const QString& nom, const QString& lien, const std::vector<Violation>& v) {
    const int e = compte(v, Gravite::Erreur), a = compte(v, Gravite::Avertissement);
    QString r = QStringLiteral("<a href='%1' style='text-decoration:none; color:%2'><b>%3</b>").arg(lien, e ? t.erreur.name() : t.succes.name(), nom);
    if (e) r += QStringLiteral(" %1 ●").arg(e);
    else r += QStringLiteral(" ✓");
    if (a) r += QStringLiteral(" <span style='color:%1'>%2 ▲</span>").arg(t.alerte.name()).arg(a);
    return r + QStringLiteral("</a>");
  };
  const QString sep = QStringLiteral(" · ");
  int reseaux = 0;
  for (const auto& x : m_calculs.k.reseaux) reseaux += x.broches.size() >= 2;
  QString bilan;
  if (mode() == Schema) {
    bilan = compteur(QStringLiteral("ERC"), QStringLiteral("erc"), m_erc) + sep + QStringLiteral("%1 composant(s)").arg(c->composants.size()) + sep + QStringLiteral("%1 réseau(x)").arg(reseaux) + sep +
            QStringLiteral("<b>%1</b>").arg(q(c->nom).toHtmlEscaped());
  } else {
    const auto& e = m_calculs.etat;
    const int pourcent = e.reseauxTotal ? static_cast<int>(std::lround(100.0 * e.reseauxComplets / e.reseauxTotal)) : 100;
    const QString drc = niveau < 2 ? QStringLiteral("<span style='color:%1'>DRC à la fin du geste</span>").arg(t.texteSecondaire.name())
                        : m_calculEnCours ? QStringLiteral("<span style='color:%1'>⏳ DRC en cours</span>").arg(t.texteSecondaire.name())
                                          : compteur(QStringLiteral("DRC"), QStringLiteral("drc"), m_calculs.drc);
    bilan = drc + sep + QStringLiteral("Routage %1 % (%2 / %3 réseaux)").arg(pourcent).arg(e.reseauxComplets).arg(e.reseauxTotal) + sep +
            QStringLiteral("%1 liaison(s) à faire").arg(e.chevelu.size()) + sep + QStringLiteral("<b>%1</b>").arg(q(c->nom).toHtmlEscaped());
  }
  m_barreEtat->setBilan(bilan);
}

// Un compteur du bilan cliqué : la première violation, montrée dans sa vue
void AtelierElectronique::allerAuProbleme(const QString& lien) {
  const std::vector<Violation>& v = lien == QLatin1String("drc") ? m_calculs.drc : m_erc;
  if (v.empty()) return;
  const auto it = std::find_if(v.begin(), v.end(), [](const Violation& x) { return x.gravite == Gravite::Erreur; });
  const Violation& x = it != v.end() ? *it : v.front();
  if (lien == QLatin1String("drc")) {
    setMode(Circuit);
    if (x.position.x != 0 || x.position.y != 0) m_circuit->centrerSur(x.position);
    if (!x.objets.empty()) m_circuit->choisir(VueCircuit::Genre::Empreinte, x.objets.front());
  } else {
    setMode(Schema);
    if (const ConceptionElec* c = conceptionCourante())
      for (std::size_t i = 0; i < c->feuilles.size(); ++i)
        if (c->feuilles[i].uuid == x.feuille) m_schema->setConception(m_conception, static_cast<int>(i));
    if (x.position.x != 0 || x.position.y != 0) m_schema->centrerSur(x.position);
  }
  afficherMessage(QStringLiteral("<b>%1</b> — %2").arg(q(x.message).toHtmlEscaped(), q(x.conseil).toHtmlEscaped()), x.gravite == Gravite::Erreur ? NiveauMessage::Erreur : NiveauMessage::Avertissement);
}

void AtelierElectronique::majVue3D() {
  const ConceptionElec* c = conceptionCourante();
  if (!c || m_3dAJour) return;
  // Ce que la 3D montre : le circuit (cuivre, contour, empreintes, empilage), les corps et leurs modèles ; une valeur,
  // un fabricant ou une note changés ne la refont pas (9.5)
  std::string sig = prom::ecad::versJson(*c)["circuit"].dump();
  for (const auto& x : c->composants) sig += x.uuid + ':' + std::to_string(hauteurComposant(*c, x).mm) + ':' + x.modele3D + ':' + x.empreinte + ';';
  for (const auto& [id, m] : c->modeles3D) sig += id + ',' + std::to_string(m.dx) + ',' + std::to_string(m.dy) + ',' + std::to_string(m.dz) + ',' + std::to_string(m.quarts) + ';';
  // Apparence (6.11) : vernis du rendu, familles masquées, boîtes à la place des modèles, pièces et boîtier importé
  const ApparenceCarte3D& ap = apparence3D();
  sig += "|" + ap.vernis + "|" + std::to_string(ap.boites) + std::to_string(ap.pieces) + std::to_string(ap.boitierImporte);
  for (const auto& f : ap.famillesMasquees) sig += "|" + f;
  for (const auto& pm : c->piecesMontees) sig += pm.uuid + std::to_string(pm.x) + std::to_string(pm.y) + std::to_string(pm.z) + ';';
  if (c->boitierImporte) sig += "boitier" + std::to_string(c->boitierImporte->x) + std::to_string(c->boitierImporte->z);
  if (sig == m_signature3D) {
    m_3dAJour = true;
    return;
  }
  m_signature3D = std::move(sig);
  ++m_constructions3D;
  ConceptionElec vue = *c;
  if (!ap.vernis.empty()) vue.circuit.empilage.vernis = ap.vernis;
  if (!ap.famillesMasquees.empty())
    std::erase_if(vue.circuit.empreintes, [&](const EmpreintePlacee& e) {
      const DefEmpreinte* d = vue.empreinte(e.empreinte);
      return d && ap.famillesMasquees.count(d->famille.empty() ? std::string("Autres") : d->famille);
    });
  if (ap.boites)
    for (auto& k : vue.composants) k.modele3D.clear();
  const prom::Carte3D m = prom::modele3DCarte(vue);
  std::vector<std::pair<TopoDS_Shape, QColor>> couleurs;
  for (const auto& [forme, couleur] : m.couleurs) couleurs.push_back({forme, QColor(QString::fromStdString(couleur))});
  // Pièces montées et boîtier importé, ajoutés au composé montré
  TopoDS_Compound tout;
  BRep_Builder bb;
  bb.MakeCompound(tout);
  bb.Add(tout, m.forme);
  if (ap.pieces)
    for (const auto& pm : vue.piecesMontees)
      if (const TopoDS_Shape f = prom::formePieceMontee(vue, pm); !f.IsNull()) {
        bb.Add(tout, f);
        couleurs.push_back({f, QColor(0xB0, 0xB4, 0xBA)});
      }
  if (ap.boitierImporte)
    if (const TopoDS_Shape f = prom::formeBoitierImporte(vue); !f.IsNull()) {
      bb.Add(tout, f);
      couleurs.push_back({f, QColor(0xD8, 0xDC, 0xE2)});
    }
  m_3d->setCouleursSousFormes(couleurs);
  m_3d->setApparence(QColor(0x1F, 0x6B, 0x45), 0);
  prom::ResultatPiece r;
  r.forme = tout;
  m_3d->afficher(r, true);
  m_3dAJour = true;
}

void AtelierElectronique::surligner(const QString& reseau) { m_selection->surligner(reseau.toStdString()); }

void AtelierElectronique::annoncerSurlignage() {
  const std::string reseau = m_selection->reseau();
  if (reseau.empty()) return;
  const int i = m_calculs.k.indice(reseau);
  if (i >= 0)
    afficherMessage(QStringLiteral("Réseau <b>%1</b> : %2 broche(s), surligné dans le schéma et sur le circuit.").arg(q(reseau).toHtmlEscaped()).arg(m_calculs.k.reseaux[static_cast<std::size_t>(i)].broches.size()), NiveauMessage::Information);
}

// ---------- Actions ----------
void AtelierElectronique::annoter(bool tout) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  if (c->verrouillee) {
    afficherMessage(QString::fromStdString(prom::ecad::ConceptionVerrouillee(*c).what()).toHtmlEscaped(), NiveauMessage::Information);
    return;
  }
  // Les changements sont montrés avant d'être appliqués (EL-16)
  const auto prevus = prom::ecad::annotationPrevue(*c, tout);
  if (prevus.empty()) { afficherMessage(QStringLiteral("<b>Annotation</b> : rien à changer."), NiveauMessage::Information); return; }
  QStringList lignes;
  for (const auto& ch : prevus)
    lignes << QStringLiteral("%1 → %2").arg(ch.avant.empty() ? QStringLiteral("(sans repère)") : QString::fromStdString(ch.avant), QString::fromStdString(ch.apres));
  if (tout) {
    QMessageBox q(QMessageBox::Question, QStringLiteral("Tout renuméroter"),
                  QStringLiteral("%1 repère(s) vont changer. Les réseaux nommés d’après un repère suivent le changement.").arg(prevus.size()), QMessageBox::Ok | QMessageBox::Cancel, this);
    q.setDetailedText(lignes.join(QLatin1Char('\n')));
    if (q.exec() != QMessageBox::Ok) return;
  }
  int n = 0;
  modifierConception([&](ConceptionElec& x) { n = prom::ecad::annoter(x, tout); });
  const QString detail = lignes.mid(0, 12).join(QStringLiteral(", ")) + (lignes.size() > 12 ? QStringLiteral("…") : QString());
  afficherMessage(QStringLiteral("<b>Annotation</b> : %1 référence(s) attribuée(s) : %2.").arg(n).arg(detail.toHtmlEscaped()), NiveauMessage::Succes);
}

void AtelierElectronique::mettreAJourCircuit() {
  if (const ConceptionElec* c0 = conceptionCourante(); c0 && c0->verrouillee) {
    afficherMessage(QString::fromStdString(prom::ecad::ConceptionVerrouillee(*c0).what()).toHtmlEscaped(), NiveauMessage::Information);
    return;
  }
  // « Synchroniser maintenant » (3.8) : le panneau des écarts montre la liste quand il y en a une, puis tout
  // s'applique en un pas. Les écarts sont comptés une fois, montrés puis appliqués (ERG-10 : F8 sous 100 ms)
  const ConceptionElec* cs = conceptionCourante();
  const bool schemaSeul = cs && cs->schemaSeul;
  std::vector<prom::ecad::EcartSynchro> ecarts0 = cs && !schemaSeul ? prom::ecad::ecartsSynchronisation(*cs) : std::vector<prom::ecad::EcartSynchro>{};
  if (m_ecarts) {
    m_ecarts->setEcarts(ecarts0);
    if (!ecarts0.empty()) montrerEcarts();
  }
  BilanMiseAJour b;
  int ecarts = 0;
  const bool change = modifierConception([&](ConceptionElec& c) {
    c.schemaSeul = false;  // le circuit est demandé : il existe désormais (STR-13)
    const bool annote = std::any_of(c.composants.begin(), c.composants.end(), [](const ComposantElec& x) { return x.reference.find('?') != std::string::npos; });
    if (annote) prom::ecad::annoter(c, false);
    // liaisons tracées, broches routées compris ; recomptés si l'annotation ou le premier circuit les change
    ecarts = prom::ecad::appliquerEcarts(c, annote || schemaSeul ? prom::ecad::ecartsSynchronisation(c) : ecarts0);
    b = prom::ecad::mettreAJourCircuit(c);
  }, QStringLiteral("Synchroniser le circuit et le schéma"));
  if (m_ecarts && change) m_ecarts->maj();
  (void)ecarts;
  afficherMessage(QStringLiteral("<b>Circuit mis à jour</b> : %1 empreinte(s) ajoutée(s), %2 changée(s), %3 retirée(s). Les nouvelles attendent à droite de la carte, et dans la liste « À placer ».%4")
                      .arg(b.ajoutees)
                      .arg(b.changees)
                      .arg(b.retirees)
                      .arg((b.cuivreRenomme ? QStringLiteral(" %1 piste(s), via(s), zone(s) ou classe(s) suivent le nouveau nom de leur réseau.").arg(b.cuivreRenomme) : QString()) +
                           // Conflits d'affectation de classes après un renommage, jamais tranchés en silence (R11)
                           (b.conflitsClasses.empty() ? QString()
                                                      : QStringLiteral(" <span style='color:#A8620E'>%1</span>").arg(QString::fromStdString(b.conflitsClasses.front()).toHtmlEscaped() +
                                                                                                                     (b.conflitsClasses.size() > 1 ? QStringLiteral(" (et %1 autre(s))").arg(b.conflitsClasses.size() - 1) : QString())))), NiveauMessage::Avertissement,
                  b.ajoutees > 0 ? std::function<void()>([this] { montrerAPlacer(); }) : std::function<void()>());
  if (m_aPlacer && m_dockAPlacer->isVisible()) m_aPlacer->refaire();
  if (mode() == Schema) m_ruban->setCurrentIndex(Circuit);
  m_circuit->recadrer();
}

void AtelierElectronique::lierAuBoitier(bool montrer) {
  const int i = m_conception;
  m_doc->modifier([i](prom::Projet& p) {
    if (i < 0 || i >= static_cast<int>(p.electronique.size())) return;
    for (auto& c : p.electronique) c.pilotageBoitier = false;
    p.electronique[static_cast<std::size_t>(i)].pilotageBoitier = true;
    p.sansCarte = false;
    prom::appliquerCarteElectronique(p, static_cast<std::size_t>(i));
  });
  if (montrer) emit boitierDemande();
  else
    afficherMessage(QStringLiteral("<b>Boîtier</b> : le boîtier du projet suit cette carte (contour, trous et hauteurs). Un clic sur ce message l’ouvre."), NiveauMessage::Succes,
                    [this] { emit boitierDemande(); });
}

// ---------- Bibliothèque (PanneauBibliotheque, 1.10) ----------
void AtelierElectronique::montrerBibliotheque(bool chercher) {
  if (m_ruban->currentIndex() != Schema) m_ruban->setCurrentIndex(Schema);
  m_bibliothequeOuverte = true;
  QSettings().setValue(QStringLiteral("electronique/bibliothequeOuverte"), true);
  majBibliotheque();
  if (chercher) m_panneauBibliotheque->chercher();
}

void AtelierElectronique::replierBibliotheque() {
  m_bibliothequeOuverte = false;
  QSettings().setValue(QStringLiteral("electronique/bibliothequeOuverte"), false);
  majBibliotheque();
  if (mode() == Schema) m_schema->setFocus(Qt::OtherFocusReason);
}

bool AtelierElectronique::bibliothequeMontree() const { return m_panneauBibliotheque && m_panneauBibliotheque->isVisible(); }

void AtelierElectronique::majBibliotheque() {
  if (!m_dockBibliotheque) return;
  const bool voulue = m_bibliothequeOuverte && mode() == Schema;
  if (voulue == !m_dockBibliotheque->isHidden()) return;
  m_dockBibliotheque->setVisible(voulue);
}

// ---------- Panneaux (1.21, ERG-23) ----------
QDockWidget* AtelierElectronique::panneau(Panneau p) const {
  switch (p) {
    case Panneau::Projet: return m_dockProjet;
    case Panneau::Bibliotheque: return m_dockBibliotheque;
    case Panneau::Inspecteur: return m_dockInspecteur;
    case Panneau::Messages: return m_dockMessages;
    case Panneau::Verifications: return m_dockVerifications;
    case Panneau::Recherche: return m_dockRecherche;
    case Panneau::Apprendre: return m_dockApprendre;
    case Panneau::Ecarts: return m_dockEcarts;
    case Panneau::Champs: return m_dockChamps;
    case Panneau::APlacer: return m_dockAPlacer;
    case Panneau::Reseaux: return m_dockReseaux;
  }
  return nullptr;
}

void AtelierElectronique::armerPinceau(const std::string& modele) {
  const ConceptionElec* c = conceptionCourante();
  const ComposantElec* x = c ? c->composant(modele) : nullptr;
  if (!x) return;
  m_pinceau = modele;
  m_modelePinceau = *x;
  afficherMessage(QStringLiteral("<b>Pinceau</b> : les propriétés de %1 (valeur, paramètres, exclusions) iront sur chaque composant choisi ensuite. "
                                 "<a href=\"terminer\">Terminer</a> ou Échap.")
                      .arg(q(x->reference).toHtmlEscaped()),
                  NiveauMessage::Information, [this] { terminerPinceau(); });
}

void AtelierElectronique::terminerPinceau() {
  if (m_pinceau.empty()) return;
  m_pinceau.clear();
  afficherMessage(QStringLiteral("Pinceau reposé."));
}

void AtelierElectronique::renumeroterSelection() {
  const ConceptionElec* c0 = conceptionCourante();
  if (!c0) return;
  // Ordre de lecture : feuille, puis de haut en bas, puis de gauche à droite
  struct Cle {
    std::size_t feuille;
    Nm y, x;
    std::string id;
  };
  std::vector<Cle> ordre;
  for (const auto& id : m_selection->composants()) {
    bool vu = false;
    for (std::size_t f = 0; f < c0->feuilles.size() && !vu; ++f)
      for (const auto& s : c0->feuilles[f].symboles)
        if (s.composant == id) {
          ordre.push_back({f, s.position.y, s.position.x, id});
          vu = true;
          break;
        }
  }
  if (ordre.empty()) return;
  std::sort(ordre.begin(), ordre.end(), [](const Cle& a, const Cle& b) { return std::tie(a.feuille, a.y, a.x) < std::tie(b.feuille, b.y, b.x); });
  auto prefixe = [](const std::string& r) {
    std::string p;
    for (char ch : r) {
      if (!std::isalpha(static_cast<unsigned char>(ch))) break;
      p += ch;
    }
    return p;
  };
  std::set<std::string> choisis(m_selection->composants().begin(), m_selection->composants().end()), pris;
  std::map<std::string, int> premier;  // par préfixe : le plus petit numéro des choisis
  for (const auto& x : c0->composants) {
    if (!choisis.count(x.uuid)) {
      pris.insert(x.reference);
      continue;
    }
    const std::string p = prefixe(x.reference);
    const std::string n = x.reference.substr(p.size());
    const int v = !n.empty() && std::all_of(n.begin(), n.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); }) ? std::stoi(n) : 1;
    premier[p] = premier.count(p) ? std::min(premier[p], v) : v;
  }
  std::map<std::string, std::string> nouveaux;
  std::map<std::string, int> prochain = premier;
  for (const Cle& k : ordre) {
    const ComposantElec* x = c0->composant(k.id);
    const std::string p = prefixe(x->reference);
    int& n = prochain[p];
    if (n < 1) n = 1;
    while (pris.count(p + std::to_string(n))) ++n;
    nouveaux[k.id] = p + std::to_string(n);
    pris.insert(nouveaux[k.id]);
    ++n;
  }
  modifierConception([&](ConceptionElec& c) {
    for (const auto& [id, r] : nouveaux)
      if (ComposantElec* x = c.composant(id)) x->reference = r;
  }, QStringLiteral("Renuméroter %1 composants").arg(nouveaux.size()));
}

void AtelierElectronique::remplacerComposants(std::vector<std::string> composants) {
  if (composants.empty()) composants.assign(m_selection->composants().begin(), m_selection->composants().end());
  if (composants.empty() || !conceptionCourante()) return;
  DialogueRemplacer d(*this, composants, this);
  d.exec();
}

void AtelierElectronique::montrerEcarts() {
  if (!m_dockEcarts) return;
  m_ecarts->maj();
  m_dockEcarts->show();
  m_dockEcarts->raise();
}

void AtelierElectronique::dispositionOrigine() {
  const std::array<QDockWidget*, 10> tous{m_dockProjet, m_dockBibliotheque, m_dockInspecteur, m_dockMessages, m_dockVerifications, m_dockRecherche, m_dockApprendre, m_dockEcarts, m_dockChamps, m_dockAPlacer};
  for (QDockWidget* d : tous) {
    d->setFloating(false);
    m_fenetre->removeDockWidget(d);
  }
  m_fenetre->addDockWidget(Qt::LeftDockWidgetArea, m_dockProjet);
  m_fenetre->splitDockWidget(m_dockProjet, m_dockBibliotheque, Qt::Horizontal);
  m_fenetre->addDockWidget(Qt::RightDockWidgetArea, m_dockInspecteur);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockMessages);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockVerifications);
  m_fenetre->tabifyDockWidget(m_dockMessages, m_dockVerifications);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockRecherche);
  m_fenetre->tabifyDockWidget(m_dockVerifications, m_dockRecherche);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockEcarts);
  m_fenetre->tabifyDockWidget(m_dockRecherche, m_dockEcarts);
  m_fenetre->addDockWidget(Qt::BottomDockWidgetArea, m_dockChamps);
  m_fenetre->tabifyDockWidget(m_dockEcarts, m_dockChamps);
  m_fenetre->addDockWidget(Qt::RightDockWidgetArea, m_dockApprendre);
  m_fenetre->tabifyDockWidget(m_dockInspecteur, m_dockApprendre);
  m_fenetre->addDockWidget(Qt::LeftDockWidgetArea, m_dockAPlacer);
  m_fenetre->tabifyDockWidget(m_dockProjet, m_dockAPlacer);
  m_dockProjet->show();
  m_dockInspecteur->show();
  m_dockMessages->hide();
  m_dockVerifications->hide();
  m_dockRecherche->hide();
  m_dockEcarts->hide();
  m_dockChamps->hide();
  m_dockApprendre->hide();
  m_dockAPlacer->hide();
  m_dockInspecteur->raise();
  majBibliotheque();
  // Les largeurs d'avant les panneaux, si elles ont été choisies (P25)
  const QSettings r;
  const int bib = std::max(240, r.value(QStringLiteral("electronique/largeurBibliotheque"), 270).toInt());
  const int ins = std::max(300, r.value(QStringLiteral("electronique/largeurInspecteur"), 340).toInt());
  m_fenetre->resizeDocks({m_dockProjet, m_dockBibliotheque, m_dockInspecteur}, {240, bib, ins}, Qt::Horizontal);
}

void AtelierElectronique::accueillirArbre(QWidget* arbre) {
  if (!arbre || m_dockProjet->widget() == arbre) return;
  m_dockProjet->setWidget(arbre);
  arbre->show();
}

QWidget* AtelierElectronique::rendreArbre() {
  QWidget* w = m_dockProjet->widget();
  if (!w || w == m_attenteProjet) return nullptr;
  m_dockProjet->setWidget(m_attenteProjet);
  m_attenteProjet->show();
  w->setParent(nullptr);
  return w;
}

void AtelierElectronique::ouvrirMessages() {
  m_dockMessages->show();
  m_dockMessages->raise();
}

void AtelierElectronique::hideEvent(QHideEvent* e) {
  QWidget::hideEvent(e);
  if (e->spontaneous() || !m_dispositions) return;
  m_dispositions->retenirMaintenant();
  m_dispositions->geler(true);
  for (QDockWidget* d : m_dispositions->panneaux())
    if (d->isFloating() && !d->isHidden()) {
      m_flottantsCaches.emplace_back(d);
      d->hide();
    }
  m_dispositions->geler(false);
}

void AtelierElectronique::showEvent(QShowEvent* e) {
  QWidget::showEvent(e);
  if (e->spontaneous() || !m_dispositions) return;
  SourisEspace::instance().demarrer(window());  // une fois (2.29)
  // Premier lancement (ERG-16) : les préférences des cartes neuves, une fois ; pas pendant les essais
  static const bool essais = QCoreApplication::arguments().join(QLatin1Char(' ')).contains(QStringLiteral("--essais"));
  if (!essais && !PanneauApprendre::premierLancementFait()) {
    montrerApprendre();
    m_apprendre->montrerPremierLancement();
  }
  m_dispositions->geler(true);
  for (const QPointer<QDockWidget>& d : m_flottantsCaches)
    if (d) d->show();
  m_flottantsCaches.clear();
  m_dispositions->geler(false);
}

// La bibliothèque fermée par la croix de son panneau : repliée, comme par son bouton
bool AtelierElectronique::eventFilter(QObject* o, QEvent* e) {
  if (o == m_ruban && e->type() == QEvent::Resize) QMetaObject::invokeMethod(this, [this] { ajusterRuban(); }, Qt::QueuedConnection);
  if (o == m_dockBibliotheque && e->type() == QEvent::Close) {
    m_bibliothequeOuverte = false;
    QSettings().setValue(QStringLiteral("electronique/bibliothequeOuverte"), false);
  }
  return QWidget::eventFilter(o, e);
}

// ---------- Inspecteur (InspecteurElectronique, 1.9) ----------
// La page qui convient à la vue et à la sélection ; ses champs sont remplis, rien n'est reconstruit (P1)
void AtelierElectronique::majPanneau() { m_inspecteur->montrer(); }
void AtelierElectronique::montrerInspecteur() { m_inspecteur->montrer(); }
// Message passager : empilé en bas à gauche du canevas, gardé dans le panneau Messages ; le bilan ne bouge pas (S29)
void AtelierElectronique::afficherMessage(const QString& html, NiveauMessage niveau, std::function<void()> aller) {
  if (html.trimmed().isEmpty()) return;
  MessageAtelier m{QDateTime::currentDateTime(), niveau, html, std::move(aller)};
  m_journal->ajouter(m);
  m_pile->montrer(m);
  emit messageChange(html);
}
void AtelierElectronique::setMode(int mode) {
  // Un schéma seul n'a ni circuit, ni 3D, ni fabrication tant qu'on ne les demande pas (STR-13)
  if (const ConceptionElec* c = conceptionCourante(); c && c->schemaSeul && mode != Schema) mode = Schema;
  if (m_ruban->currentIndex() != mode) m_ruban->setCurrentIndex(mode);
}

// Action du registre lancée depuis le ruban, les menus de la fenêtre (G1) ou un script : une commande de l'atelier,
// sinon la vue de sa portée, sinon la vue montrée
bool AtelierElectronique::executerAction(std::string_view action) {
  if (const auto it = m_commandes.find(action); it != m_commandes.end()) {
    const auto faire = it->second;  // la commande peut reconstruire le ruban
    faire();
    return true;
  }
  const ActionAtelier* a = RegistreActions::atelierElectronique().action(action);
  const PorteeAction portee = a ? a->portee : PorteeAction::Canevas;
  if (portee == PorteeAction::Schema) setMode(Schema);
  if (portee == PorteeAction::Circuit) setMode(Circuit);
  bool fait = false;
  if (mode() == Schema) {
    fait = m_schema->executerAction(action);
    m_schema->setFocus();
  } else if (mode() == Circuit) {
    fait = m_circuit->executerAction(action);
    m_circuit->setFocus();
  }
  return fait;
}

bool AtelierElectronique::actionPossible(std::string_view action) {
  if (m_commandes.find(action) != m_commandes.end()) return true;
  const ActionAtelier* a = RegistreActions::atelierElectronique().action(action);
  if (!a) return false;
  const bool schema = a->portee == PorteeAction::Canevas || a->portee == PorteeAction::Schema;
  const bool circuit = a->portee == PorteeAction::Canevas || a->portee == PorteeAction::Circuit;
  if (mode() == Schema && schema) return m_schema->actionPossible(action);
  if (mode() == Circuit && circuit) return m_circuit->actionPossible(action);
  return false;
}

void AtelierElectronique::relierListe() {
  if (!conceptionCourante()) return;
  bool ok = false;
  const QString texte = QInputDialog::getMultiLineText(this, QStringLiteral("Relier une liste de paires"),
                                                       QStringLiteral("Une paire de broches par ligne, repère:numéro ou repère:nom :\nU1:PA9 → J2:3"), QString(), &ok);
  if (ok && !texte.trimmed().isEmpty()) relierTexte(texte);
}

int AtelierElectronique::relierTexte(const QString& texte) {
  std::vector<std::pair<std::string, std::string>> paires;
  for (QString l : texte.split(QLatin1Char('\n'))) {
    l.replace(QStringLiteral("→"), QStringLiteral(" ")).replace(QStringLiteral("->"), QStringLiteral(" ")).replace(QLatin1Char(';'), QLatin1Char(' '));
    const QStringList m = l.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    if (m.size() == 2) paires.push_back({m[0].toStdString(), m[1].toStdString()});
  }
  if (paires.empty()) return 0;
  std::vector<std::string> echecs;
  int n = -1;
  modifierConception([&](ConceptionElec& c) { n = relierPaires(c, paires, &echecs); }, QStringLiteral("Relier %1 paire(s)").arg(paires.size()));
  QStringList noms;
  for (const auto& e : echecs) noms << q(e);
  afficherMessage(echecs.empty() ? QStringLiteral("%1 paire(s) reliée(s), %2 segment(s).").arg(paires.size()).arg(n)
                                 : QStringLiteral("Pas de chemin libre pour : %1 · Trace ces fils à la main.").arg(noms.join(QStringLiteral(", ")).toHtmlEscaped()),
                  echecs.empty() ? NiveauMessage::Information : NiveauMessage::Avertissement);
  return echecs.empty() ? n : -1;
}

void AtelierElectronique::changerEmpreinte() {
  // La fenêtre de choix des empreintes (4.6), non modale, pour les composants choisis
  if (!conceptionCourante() || m_selection->composants().empty()) {
    afficherMessage(QStringLiteral("Choisis d’abord un ou plusieurs composants : la fenêtre des empreintes compatibles s’ouvre pour eux."));
    return;
  }
  ouvrirChoixEmpreinte();
}

void AtelierElectronique::ouvrirTableauChamps() {
  if (!m_dockChamps) return;
  m_dockChamps->show();
  m_dockChamps->raise();
  majTableauChamps();
  m_tableauChamps->table()->setFocus(Qt::OtherFocusReason);
}

void AtelierElectronique::setMosaique(bool oui) {
  m_vues->setMosaique(oui);
  if (oui) {
    m_vues->montrer(m_schema, true);
    m_vues->montrer(m_circuit, true);
    QTimer::singleShot(0, this, [this] { cadrerVuesPassives(); });
  }
  majZoom();
}

void AtelierElectronique::montrerVue3DDansMosaique(bool oui) {
  if (oui && !m_vues->mosaique()) setMosaique(true);
  m_vues->montrer(m_3d, oui);
  if (oui) majVue3D();
}

void AtelierElectronique::montrerTableauObjets(bool oui) {
  // Une vue de plus : en mosaïque à côté des autres (la mosaïque s'ouvre pour elle)
  if (oui && !m_vues->mosaique()) {
    m_vues->setMosaique(true);
    m_vues->montrer(m_circuit, m_vues->active() == m_circuit);
    m_vues->montrer(m_schema, m_vues->active() == m_schema);
  }
  m_vues->montrer(m_tableauObjets, oui);
  if (oui) m_tableauObjets->recharger();
}

void AtelierElectronique::cadrerVuesPassives() {
  const ConceptionElec* c = conceptionCourante();
  if (!c || !m_vues->mosaique()) return;
  const auto* tenu = m_selection->tenu();
  if (!tenu) return;
  const std::string composant = tenu->composant.empty() && tenu->genre == SelectionElectronique::Genre::Empreinte ? tenu->id : tenu->composant;
  auto horsVue = [](QWidget* v, QPointF p) { return !v->rect().adjusted(30, 30, -30, -30).contains(p.toPoint()); };
  // Au circuit : l'empreinte du composant, ou la piste, la via
  if (m_vues->active() != m_circuit && m_vues->visibleDansMosaique(m_circuit)) {
    std::optional<Pt> p;
    if (!composant.empty())
      for (const auto& e : c->circuit.empreintes)
        if (e.composant == composant) p = e.position;
    if (p && horsVue(m_circuit, m_circuit->versEcran(*p))) m_circuit->centrerSur(*p);
    m_circuit->update();
  }
  // Au schéma : le symbole du composant, sur sa feuille
  if (m_vues->active() != m_schema && m_vues->visibleDansMosaique(m_schema) && !composant.empty()) {
    for (std::size_t f = 0; f < c->feuilles.size(); ++f)
      for (const auto& sp : c->feuilles[f].symboles)
        if (sp.composant == composant) {
          if (m_schema->feuille() != static_cast<int>(f)) m_schema->setConception(m_conception, static_cast<int>(f));
          if (horsVue(m_schema, m_schema->versEcran(sp.position))) m_schema->centrerSur(sp.position);
          m_schema->update();
          return;
        }
  }
}

void AtelierElectronique::majTableauChamps() {
  const ConceptionElec* c = conceptionCourante();
  if (!c || !m_tableauChamps || !m_dockChamps || m_dockChamps->isHidden()) return;
  std::set<std::string> portee;
  switch (m_tableauChamps->portee()) {
    case TableauChamps::Portee::Conception: m_tableauChamps->recharger(*c); return;
    case TableauChamps::Portee::Feuille: {
      const auto f = static_cast<std::size_t>(std::max(0, m_schema->feuille()));
      if (f < c->feuilles.size())
        for (const auto& sp : c->feuilles[f].symboles)
          if (!sp.composant.empty()) portee.insert(sp.composant);
      break;
    }
    case TableauChamps::Portee::Selection: portee = m_selection->composants(); break;
  }
  m_tableauChamps->recharger(*c, &portee);
}

int AtelierElectronique::appliquerChamps(const std::vector<ComposantElec>& nouveaux, const std::vector<ComposantElec>* base) {
  std::map<std::string, const ComposantElec*> avant;
  if (base)
    for (const auto& k : *base) avant[k.uuid] = &k;
  int changes = 0;
  modifierConception([&](ConceptionElec& c2) {
    for (const auto& n : nouveaux)
      if (ComposantElec* k = c2.composant(n.uuid)) {
        const auto b = avant.find(n.uuid);
        const ComposantElec* o = b == avant.end() ? nullptr : b->second;
        const ComposantElec copie = *k;
        // Un champ s'écrit s'il diffère de ce que le tableau montrait (ou toujours, sans base)
        auto ecrire = [&](auto ComposantElec::*champ) {
          if (!o || o->*champ != n.*champ) k->*champ = n.*champ;
        };
        ecrire(&ComposantElec::reference);
        ecrire(&ComposantElec::empreinte);
        ecrire(&ComposantElec::fabricant);
        ecrire(&ComposantElec::mpn);
        ecrire(&ComposantElec::fournisseur);
        ecrire(&ComposantElec::description);
        ecrire(&ComposantElec::fiche);
        ecrire(&ComposantElec::nePasMonter);
        ecrire(&ComposantElec::horsCarte);
        ecrire(&ComposantElec::champs);
        if (!o || o->valeur != n.valeur) {
          if (k->valeur != n.valeur) k->formule.clear();  // une valeur tapée remplace la formule
          k->valeur = n.valeur;
        }
        changes += !(copie == *k);
      }
  }, QStringLiteral("Tableau des champs"));
  return changes;
}

// ---------- Navigation ----------
void AtelierElectronique::montrerComposant(const std::string& uuid) {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  for (std::size_t i = 0; i < c->feuilles.size(); ++i)
    for (const auto& sp : c->feuilles[i].symboles)
      if (sp.composant == uuid) {
        if (m_ruban->currentIndex() != Schema) m_ruban->setCurrentIndex(Schema);
        m_schema->setConception(m_conception, static_cast<int>(i));
        const std::string id = sp.uuid;
        const Pt p = sp.position;
        m_schema->selectionner({id});
        QTimer::singleShot(0, this, [this, p] { m_schema->centrerSur(p); });
        return;
      }
}

// ---------- Composants de l'utilisateur ----------
QString AtelierElectronique::cheminBibliothequePerso() {
  const QString env = qEnvironmentVariable("PROMETHEE_BIBLIOTHEQUE");
  if (!env.isEmpty()) return env;
  // Le dossier choisi au premier lancement (ERG-16)
  if (const QString d = QSettings().value(QStringLiteral("electronique/dossierBibliotheque")).toString(); !d.isEmpty())
    return QDir(d).filePath(QStringLiteral("bibliotheque-electronique.json"));
  return QDir(portable::dossierDonneesAppli()).filePath(QStringLiteral("bibliotheque-electronique.json"));
}

prom::ecad::LectureBibliotheque AtelierElectronique::lectureBibliothequePerso() {
  try {
    return prom::ecad::lireFichierBibliotheque(prom::cheminFichier(cheminBibliothequePerso().toStdString()));
  } catch (const std::exception& e) {
    prom::ecad::LectureBibliotheque l;
    l.abimee = true;
    l.erreur = e.what();
    return l;
  }
}

std::vector<DefSymbole> AtelierElectronique::bibliothequePerso() { return lectureBibliothequePerso().symboles; }

void AtelierElectronique::ajouterSymbolePerso(const DefSymbole& d) { ajouterSymbolesPerso({d}); }

void AtelierElectronique::ajouterSymbolesPerso(const std::vector<DefSymbole>& ds) {
  if (ds.empty()) return;
  const DefSymbole& d = ds.front();
  // Le fichier de l'utilisateur ne se perd jamais en silence (EL-08) : abîmé, il est mis de côté (ce qui s'en lit
  // encore est repris), puis la nouvelle version s'écrit par fichier temporaire, la précédente gardée en .bak
  const std::filesystem::path chemin = prom::cheminFichier(cheminBibliothequePerso().toStdString());
  prom::ecad::LectureBibliotheque l = lectureBibliothequePerso();
  QString note;
  try {
    if (l.abimee && std::filesystem::exists(chemin)) {
      const std::filesystem::path cote = prom::ecad::mettreDeCoteBibliotheque(chemin);
      note = QStringLiteral(" La bibliothèque était abîmée (%1) : mise de côté sous « %2 », %3 composant(s) récupéré(s).")
                 .arg(QString::fromStdString(l.erreur).toHtmlEscaped(), QString::fromStdString(prom::utf8(cote.filename())).toHtmlEscaped())
                 .arg(l.symboles.size());
    }
    std::vector<DefSymbole> v = l.symboles;
    for (const DefSymbole& n : ds) {
      bool remplace = false;
      for (auto& x : v)
        if (x.id == n.id) { x = n; remplace = true; }
      if (!remplace) v.push_back(n);
    }
    prom::ecad::ecrireFichierBibliotheque(chemin, v, l.empreintes);
    afficherMessage(ds.size() == 1 ? QStringLiteral("<b>%1</b> rangé dans la bibliothèque personnelle.%2").arg(QString::fromStdString(d.nom).toHtmlEscaped(), note)
                                   : QStringLiteral("<b>%1 composants</b> rangés dans la bibliothèque personnelle.%2").arg(ds.size()).arg(note), NiveauMessage::Succes);
  } catch (const std::exception& e) {
    afficherMessage(QStringLiteral("Bibliothèque personnelle non écrite : %1%2").arg(QString::fromUtf8(e.what()).toHtmlEscaped(), note), NiveauMessage::Erreur);
  }
  // Le projet garde sa copie : il s'ouvre partout, même sans la bibliothèque de son auteur
  if (conceptionCourante()) modifierConception([&](ConceptionElec& c) { for (const DefSymbole& n : ds) c.symboles[n.id] = n; });
  m_panneauBibliotheque->refaire();
}

std::vector<DefEmpreinte> AtelierElectronique::empreintesPerso() { return lectureBibliothequePerso().empreintes; }

void AtelierElectronique::ajouterEmpreintePerso(const DefEmpreinte& d, const std::string& composant) {
  const std::filesystem::path chemin = prom::cheminFichier(cheminBibliothequePerso().toStdString());
  prom::ecad::LectureBibliotheque l = lectureBibliothequePerso();
  // Une empreinte qui porte le nom d'une autre ne la remplace jamais en silence (B10) : les composants qui
  // l'utilisent sont nommés, et le remplacement ne se fait qu'après accord ; il s'annule dans le projet (Ctrl+Z)
  {
    const ConceptionElec* c = conceptionCourante();
    const bool dansProjet = c && c->empreintes.count(d.id);
    const bool dansBibliotheque = std::any_of(l.empreintes.begin(), l.empreintes.end(), [&](const DefEmpreinte& x) { return x.id == d.id; });
    if (dansProjet || dansBibliotheque) {
      QStringList touches;
      if (c)
        for (const auto& k : c->composants)
          if (k.empreinte == d.id) touches << q(k.reference);
      QMessageBox question(QMessageBox::Warning, QStringLiteral("Empreinte déjà existante"),
                           QStringLiteral("Une empreinte « %1 » existe déjà%2. La remplacer change la géométrie de %3 composant(s)%4.")
                               .arg(q(d.id), dansBibliotheque ? QStringLiteral(" dans ta bibliothèque personnelle") : QString())
                               .arg(touches.size())
                               .arg(touches.isEmpty() ? QString() : QStringLiteral(" : ") + touches.mid(0, 12).join(QStringLiteral(", ")) + (touches.size() > 12 ? QStringLiteral("…") : QString())),
                           QMessageBox::Cancel, this);
      QPushButton* remplacer = question.addButton(QStringLiteral("Remplacer"), QMessageBox::DestructiveRole);
      question.setDefaultButton(QMessageBox::Cancel);
      question.exec();
      if (question.clickedButton() != remplacer) {
        afficherMessage(QStringLiteral("Empreinte <b>%1</b> non créée : ce nom est déjà pris. Donne-lui un autre nom.").arg(q(d.id).toHtmlEscaped()), NiveauMessage::Information);
        return;
      }
    }
  }
  QString note;
  try {
    if (l.abimee && std::filesystem::exists(chemin)) {
      const std::filesystem::path cote = prom::ecad::mettreDeCoteBibliotheque(chemin);
      note = QStringLiteral(" La bibliothèque était abîmée : mise de côté sous « %1 ».").arg(QString::fromStdString(prom::utf8(cote.filename())).toHtmlEscaped());
    }
    std::vector<DefEmpreinte> v = l.empreintes;
    bool remplace = false;
    for (auto& x : v)
      if (x.id == d.id) { x = d; remplace = true; }
    if (!remplace) v.push_back(d);
    prom::ecad::ecrireFichierBibliotheque(chemin, l.symboles, v);
  } catch (const std::exception& e) {
    note += QStringLiteral(" <span style='color:#C2402E'>Bibliothèque personnelle non écrite : %1</span>").arg(QString::fromUtf8(e.what()).toHtmlEscaped());
  }
  // Le projet fige sa copie ; le composant la reçoit
  if (conceptionCourante())
    modifierConception([&](ConceptionElec& c) {
      c.empreintes[d.id] = d;
      if (ComposantElec* k = composant.empty() ? nullptr : c.composant(composant)) k->empreinte = d.id;
    });
  afficherMessage(QStringLiteral("Empreinte <b>%1</b> (%2 pastilles) rangée dans la bibliothèque personnelle et le projet.%3")
                      .arg(QString::fromStdString(d.id).toHtmlEscaped()).arg(d.pastilles.size()).arg(note), NiveauMessage::Succes);
}

void AtelierElectronique::supprimerComposant(const std::string& uuid) {
  const ConceptionElec* c = conceptionCourante();
  if (!c || !c->composant(uuid)) return;
  const std::string ref = c->composant(uuid)->reference;
  modifierConception([&](ConceptionElec& c2) {
    for (auto& f : c2.feuilles) std::erase_if(f.symboles, [&](const SymbolePlace& s) { return s.composant == uuid; });
    std::erase_if(c2.composants, [&](const ComposantElec& k) { return k.uuid == uuid; });
    std::erase_if(c2.circuit.empreintes, [&](const EmpreintePlacee& e) { return e.composant == uuid; });
  });
  afficherMessage(QStringLiteral("<b>%1</b> retiré du schéma et du circuit (Ctrl+Z pour le rendre).").arg(q(ref).toHtmlEscaped()), NiveauMessage::Information);
}

std::string AtelierElectronique::poserUniteSuivante(const std::string& uuidSymbole, int feuille) {
  std::string pose;
  if (const ConceptionElec* c = conceptionCourante(); !c || c->verrouillee) return pose;
  modifierConception([&](ConceptionElec& c2) { pose = poserUnite(c2, static_cast<std::size_t>(std::max(0, feuille)), uuidSymbole); });
  if (pose.empty()) {
    emit messageChange(QStringLiteral("Toutes les unités de ce composant sont déjà posées."));
    return pose;
  }
  if (m_schema->feuille() == feuille) m_schema->selectionner({pose});
  return pose;
}

void AtelierElectronique::creerEmpreinte(const std::string& composant) {
  int broches = 0;
  if (const ConceptionElec* c = conceptionCourante())
    if (const ComposantElec* k = c->composant(composant))
      if (const DefSymbole* d = c->symbole(k->symbole)) {
        std::set<std::string> n;
        for (const auto& b : d->broches) n.insert(b.numero);
        broches = static_cast<int>(n.size());
      }
  DialogueEmpreinte dlg(broches, this);
  // Noms déjà pris dans le projet et la bibliothèque personnelle : le nom proposé est libre (B10) ; la couronne
  // minimale des règles se vérifie à la création (B15)
  std::set<std::string> pris;
  Nm couronne = 0;
  if (const ConceptionElec* c = conceptionCourante()) {
    for (const auto& [ide, de] : c->empreintes) pris.insert(ide);
    couronne = c->circuit.regles.anneauMin;
  }
  for (const auto& de : empreintesPerso()) pris.insert(de.id);
  dlg.setContexte(pris, couronne);
  if (dlg.exec() != QDialog::Accepted) return;
  ajouterEmpreintePerso(dlg.empreinte(), composant);
}

void AtelierElectronique::importerEmpreinteKicad(const std::string& composant) {
  const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Empreinte KiCad"), QString(), QStringLiteral("Empreintes KiCad (*.kicad_mod)"));
  if (chemin.isEmpty()) return;
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) return;
  std::vector<std::string> alertes;
  const auto d = prom::ecad::empreinteDepuisKicadMod(f.readAll().toStdString(), &alertes);
  QStringList a;
  for (const auto& x : alertes) a << q(x);
  if (!d) {
    QMessageBox::warning(this, QStringLiteral("Empreinte KiCad"), QStringLiteral("Import impossible.\n%1").arg(a.join(QLatin1Char('\n'))));
    return;
  }
  ajouterEmpreintePerso(*d, composant);
  if (!a.isEmpty()) afficherMessage(a.join(QStringLiteral(" ; ")).toHtmlEscaped(), NiveauMessage::Avertissement);
}

void AtelierElectronique::importerSymbolesKicad() {
  const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Symboles KiCad"), QString(), QStringLiteral("Bibliothèques de symboles KiCad (*.kicad_sym)"));
  if (chemin.isEmpty()) return;
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) return;
  std::vector<std::string> alertes;
  const auto syms = prom::ecad::symbolesDepuisKicadSym(f.readAll().toStdString(), &alertes);
  QStringList a;
  for (const auto& x : alertes) a << q(x);
  if (syms.empty()) {
    QMessageBox::warning(this, QStringLiteral("Symboles KiCad"), QStringLiteral("Aucun symbole importé.\n%1").arg(a.join(QLatin1Char('\n'))));
    return;
  }
  ajouterSymbolesPerso(syms);
  afficherMessage(QStringLiteral("<b>%1 symbole(s) KiCad</b> importé(s) dans la catégorie « Importés de KiCad ».%2")
                      .arg(syms.size())
                      .arg(a.isEmpty() ? QString() : QStringLiteral(" <span style='color:#A8620E'>%1</span>").arg(a.join(QStringLiteral(" ; ")).toHtmlEscaped())), NiveauMessage::Avertissement);
}

void AtelierElectronique::creerComposant(const DefSymbole* base, bool remplacer) {
  DialogueComposant dlg(this);
  if (base) {
    SpecSymbole sp = specDepuisSymbole(*base);
    if (remplacer) sp.version = base->version + 1;
    else {
      sp.id.clear();
      sp.nom += " (variante)";
      sp.categorie = "Mes composants";
      sp.version = 1;
    }
    dlg.setSpec(sp);
  }
  if (dlg.exec() != QDialog::Accepted) return;
  DefSymbole d = dlg.symbole();
  if (!remplacer) {
    // Identifiant libre : ni intégré, ni personnel, ni déjà dans le projet
    const auto perso = bibliothequePerso();
    const ConceptionElec* c = conceptionCourante();
    auto pris = [&](const std::string& id) {
      return symboleBibliotheque(id) || (c && c->symboles.count(id)) || std::any_of(perso.begin(), perso.end(), [&](const DefSymbole& x) { return x.id == id; });
    };
    const std::string base0 = d.id;
    for (int n = 2; pris(d.id); ++n) d.id = base0 + "-" + std::to_string(n);
  }
  ajouterSymbolePerso(d);
  if (remplacer) {
    afficherMessage(QStringLiteral("<b>%1</b> mis à jour (version %2) : ses symboles suivent.").arg(q(d.nom).toHtmlEscaped()).arg(d.version), NiveauMessage::Succes);
    return;
  }
  if (m_ruban->currentIndex() != Schema) m_ruban->setCurrentIndex(Schema);
  m_schema->setOutil(VueSchema::Outil::Placer, d.id);
  m_schema->setFocus();
  afficherMessage(QStringLiteral("<b>%1</b> créé et rangé dans « Mes composants » : clique dans le schéma pour le poser.").arg(q(d.nom).toHtmlEscaped()), NiveauMessage::Succes);
  majPanneau();
}

void AtelierElectronique::routerAutomatiquement(bool complet) {
  if (!conceptionCourante()) return;
  QApplication::setOverrideCursor(Qt::WaitCursor);
  BilanRoutage b;
  modifierConception([&](ConceptionElec& c) { b = complet ? routerComplet(c) : routerSimple(c); }, complet ? QStringLiteral("Routage complet") : QString());
  QApplication::restoreOverrideCursor();
  if (mode() == Schema) m_ruban->setCurrentIndex(Circuit);
  if (b.complet()) {
    afficherMessage(QStringLiteral("<b>Routage automatique</b> : %1 liaison(s) posée(s), %2 via(s) ; toutes les liaisons sont tracées "
                                   "(la DRC reste à consulter).").arg(b.liaisons).arg(b.vias), NiveauMessage::Succes);
    return;
  }
  // Jamais présenté comme terminé : chaque liaison restante est listée avec la raison de l'échec.
  afficherMessage(QStringLiteral("<b style='color:#B7791F'>Routage incomplet</b> : %1 liaison(s) posée(s), %2 via(s) ; <b>%3</b> liaison(s) restent au chevelu, à router à la main.")
                      .arg(b.liaisons).arg(b.vias).arg(b.restantes), NiveauMessage::Avertissement);
  auto* d = new QDialog(this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->setWindowTitle(QStringLiteral("Routage incomplet"));
  auto* col = new QVBoxLayout(d);
  auto* t = new QLabel(((complet ? QStringLiteral("<b>%1 liaison(s) n’ont pas été routées.</b> Le routeur complet a cherché un chemin pour chacune, autour des obstacles "
                                                 "et sur les deux faces, sans jamais passer sous l’isolement ; il n’en a pas trouvé pour celles-ci. ")
                                : QStringLiteral("<b>%1 liaison(s) n’ont pas été routées.</b> Le routeur automatique est une aide : il ne pose que des tracés directs "
                                                 "(droits, à 45°, en équerre, avec une via si besoin) qui respectent l’isolement, sans chercher de détour. ")) +
                            QStringLiteral("La carte n’est pas terminée tant que ces liaisons restent au chevelu."))
                           .arg(b.restantes));
  t->setWordWrap(true);
  col->addWidget(t);
  auto* liste = new QListWidget;
  for (const auto& l : b.nonRoutees)
    liste->addItem(QStringLiteral("%1 : (%2 ; %3) → (%4 ; %5) mm — %6")
                       .arg(q(l.reseau), QLocale().toString(mm(l.a.x), 'f', 2), QLocale().toString(mm(l.a.y), 'f', 2), QLocale().toString(mm(l.b.x), 'f', 2),
                            QLocale().toString(mm(l.b.y), 'f', 2), q(l.raison)));
  liste->setWordWrap(true);
  liste->setObjectName(QStringLiteral("liaisonsRestantes"));
  // Un clic sur une liaison restante la montre : la vue du circuit la centre et son réseau est surligné (5.16, C23)
  const auto restantes = b.nonRoutees;
  connect(liste, &QListWidget::currentRowChanged, this, [this, restantes](int i) {
    if (i < 0 || i >= static_cast<int>(restantes.size())) return;
    const auto& l = restantes[static_cast<std::size_t>(i)];
    setMode(Circuit);
    m_circuit->centrerSur({(l.a.x + l.b.x) / 2, (l.a.y + l.b.y) / 2});
    surligner(q(l.reseau));
  });
  col->addWidget(liste);
  auto* ok = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(ok, &QDialogButtonBox::rejected, d, &QDialog::close);
  col->addWidget(ok);
  d->resize(720, 360);
  d->show();
}


// ---------- Circuit (lot L5) ----------
FenetreRapport* AtelierElectronique::rapport() const { return m_rapport; }

void AtelierElectronique::montrerAPlacer() {
  if (!m_dockAPlacer) return;
  m_aPlacer->refaire();
  m_dockAPlacer->show();
  m_dockAPlacer->raise();
}

void AtelierElectronique::disposerEnCercle() {
  const ConceptionElec* c = conceptionCourante();
  if (!c) return;
  // Les empreintes choisies, dans l'ordre de leurs repères (R2 avant R10)
  std::vector<std::pair<std::string, std::string>> tri;
  for (const auto& id : m_selection->composants())
    for (const auto& e : c->circuit.empreintes)
      if (e.composant == id)
        if (const ComposantElec* k = c->composant(id)) tri.push_back({k->reference, id});
  if (tri.size() < 2) {
    afficherMessage(QStringLiteral("Choisis au moins deux empreintes au circuit : elles se rangent en cercle."));
    return;
  }
  auto cle = [](const std::string& r) {
    std::string p;
    long n = 0;
    for (char ch : r)
      if (std::isdigit(static_cast<unsigned char>(ch))) n = n * 10 + (ch - '0');
      else p += ch;
    return std::make_pair(p, n);
  };
  std::sort(tri.begin(), tri.end(), [&](const auto& a, const auto& b) { return cle(a.first) < cle(b.first); });
  std::vector<std::string> ids;
  for (const auto& [r, id] : tri) ids.push_back(id);
  // Centre proposé : celui de la carte
  const Boite b = boite(c->circuit.contour.polygone());
  DialogueCercle d(b.vide() ? Pt{} : b.centre(), this);
  if (d.exec() != QDialog::Accepted) return;
  const OptionsCercle o = d.options();
  int n = 0;
  modifierConception([&](ConceptionElec& cc) { n = prom::ecad::disposerEnCercle(cc, ids, o); }, QStringLiteral("Disposer %1 composants en cercle").arg(ids.size()));
  afficherMessage(QStringLiteral("<b>%1 composant(s)</b> rangé(s) en cercle de %2 mm de rayon%3.")
                      .arg(n)
                      .arg(QLocale(QLocale::French).toString(prom::ecad::mm(o.rayon), 'g', 6))
                      .arg(o.renumeroter ? QStringLiteral(", renumérotés dans l’ordre du cercle") : QString()),
                  NiveauMessage::Succes);
}

void AtelierElectronique::ouvrirRapport() {
  if (!m_rapport) m_rapport = new FenetreRapport(*this, this);
  else m_rapport->refaire();
  m_rapport->show();
  m_rapport->raise();
  m_rapport->activateWindow();
}
