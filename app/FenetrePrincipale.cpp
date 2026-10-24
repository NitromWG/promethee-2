// Prométhée : fenêtre principale. Licence GPL-3.0-only.
#include <cstring>
#include <QDir>
#include <Standard_Version.hxx>
#include "Journal.hpp"
#include "ServeurApi.hpp"
#include "promethee/structure.hpp"
#include "promethee/version.hpp"
#include "promethee/fichiers.hpp"
#include "FenetrePrincipale.hpp"

#include "Theme.hpp"

#include <algorithm>
#include <functional>

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QSignalBlocker>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QWindow>
#include <QPainter>
#include <QMouseEvent>
#include <QLineEdit>
#include <QDialog>
#include <QInputDialog>
#include <QTimer>
#include <QRegularExpression>
#include <QListWidget>
#include <QShortcut>
#include <QUrl>
#include <QSettings>
#include <QDesktopServices>
#include <QToolButton>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

#include <filesystem>
#include <sstream>
#include <fstream>

#include "DialogueExport.hpp"
#include "DialogueVersionPrecedente.hpp"
#include "Document.hpp"
#include "AtelierPiece.hpp"
#include "Icones.hpp"
#include "Qet.hpp"
#include "AtelierAssemblage.hpp"
#include "AtelierElectronique.hpp"
#include "VueSchema.hpp"
#include "ParcoursElectronique.hpp"
#include "RegistreActions.hpp"
#include "FenetreReglages.hpp"
#include "RegistreReglages.hpp"
#include "AtelierElectrique.hpp"
#include "promethee/electrique_parcours.hpp"
#include "BarreTitre.hpp"
#include "Navigateur.hpp"
#include "SelectionElectronique.hpp"
#include "Vignettes.hpp"
#include "Nomenclature.hpp"
#include "Accueil.hpp"
#include "Journal.hpp"
#include "Panneau.hpp"
#include "Vue3D.hpp"
#include "VueCarte.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/import_cao.hpp"
#include "promethee/kicad.hpp"
#include "promethee/kicad_electronique.hpp"
#include "promethee/import_commun.hpp"
#include "promethee/placement.hpp"

using namespace prom;

namespace {
QWidget* cadre(const QString& titre, QWidget* vue, QToolBar* outils) {
  auto* w = new QWidget;
  auto* l = new QVBoxLayout(w);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(0);
  auto* t = new QLabel(QStringLiteral("  ") + titre + QStringLiteral("  "));
  t->setStyleSheet(QStringLiteral("font-weight: 600;"));
  outils->insertWidget(outils->actions().isEmpty() ? nullptr : outils->actions().first(), t);
  l->addWidget(outils);
  l->addWidget(vue, 1);
  return w;
}

QLabel* etiquette(const QString& texte) {
  auto* e = new QLabel(texte);
  e->setStyleSheet(QStringLiteral("color: #565E7D; padding: 0 4px 0 10px;"));
  return e;
}
}  // namespace

FenetrePrincipale::FenetrePrincipale(bool dansCoquille) : m_dansCoquille(dansCoquille), m_doc(new Document(this)) {
  setWindowIcon(iconeMarque());
  // Interface JSON-RPC sur un canal local du poste (13.10) : un programme extérieur lit et modifie le projet ouvert
  if (qEnvironmentVariable("PROMETHEE_API") != QLatin1String("0")) {
    auto* api = new ServeurApi(m_doc, this);
    api->setObjectName(QStringLiteral("serveurApi"));
    api->demarrer();
  }
  m_carte = new VueCarte(m_doc);
  m_vue3d = new Vue3D(m_doc);
  m_panneau = new Panneau(m_doc);

  auto* outilsCarte = new QToolBar(this);
  auto* outilsBoitier = new QToolBar(this);
  outilsCarte->addWidget(etiquette(QStringLiteral("Face :")));
  auto* groupeFace = new QActionGroup(this);
  for (const auto& [nom, dessous] : {std::pair{QStringLiteral("Dessus"), false}, std::pair{QStringLiteral("Dessous"), true}}) {
    QAction* a = outilsCarte->addAction(nom);
    a->setCheckable(true);
    a->setChecked(!dessous);
    a->setToolTip(dessous ? QStringLiteral("Montrer et modifier la face arrière de la carte (B)") : QStringLiteral("Montrer et modifier la face avant de la carte (B)"));
    groupeFace->addAction(a);
    const bool d = dessous;
    connect(a, &QAction::triggered, this, [this, d] { m_carte->setFaceArriere(d); });
  }
  auto* basculerFace = new QAction(QStringLiteral("Changer de face"), this);
  basculerFace->setShortcut(QKeySequence(Qt::Key_B));
  connect(basculerFace, &QAction::triggered, this, [this] { m_carte->setFaceArriere(!m_carte->faceArriere()); });
  connect(m_carte, &VueCarte::faceChangee, this, [groupeFace](bool d) { groupeFace->actions().at(d ? 1 : 0)->setChecked(true); });
  addAction(basculerFace);
  m_raccourcisCarte.append(basculerFace);
  QAction* recadrerCarte = outilsCarte->addAction(QStringLiteral("Recadrer"));
  connect(recadrerCarte, &QAction::triggered, m_carte, &VueCarte::recadrer);
  auto* groupeCouvercle = new QActionGroup(this);
  outilsBoitier->addWidget(etiquette(QStringLiteral("Couvercle :")));
  const std::pair<QString, Vue3D::Couvercle> modes[] = {{QStringLiteral("Ouvert"), Vue3D::Couvercle::Masque}, {QStringLiteral("Fermé"), Vue3D::Couvercle::Pose},
                                                        {QStringLiteral("Éclaté"), Vue3D::Couvercle::Souleve}};
  auto* choixCouvercle = new QComboBox;
  for (const auto& [nom, mode] : modes) {
    auto* a = new QAction(nom, this);
    a->setCheckable(true);
    a->setChecked(mode == Vue3D::Couvercle::Souleve);
    groupeCouvercle->addAction(a);
    choixCouvercle->addItem(nom);
    const Vue3D::Couvercle m = mode;
    const int indice = choixCouvercle->count() - 1;
    connect(a, &QAction::triggered, this, [this, m, choixCouvercle, indice] {
      m_vue3d->setCouvercle(m);
      const QSignalBlocker b(choixCouvercle);
      choixCouvercle->setCurrentIndex(indice);
    });
  }
  choixCouvercle->setCurrentIndex(2);
  connect(choixCouvercle, &QComboBox::activated, this, [groupeCouvercle](int i) { groupeCouvercle->actions().at(i)->trigger(); });
  outilsBoitier->addWidget(choixCouvercle);
  outilsBoitier->addWidget(etiquette(QStringLiteral("Boîtier :")));
  auto* groupeBoitier = new QActionGroup(this);
  const std::tuple<QString, Vue3D::Boitier, QString> modesBoitier[] = {
      {QStringLiteral("Visible"), Vue3D::Boitier::Visible, QStringLiteral("Boîtier opaque")},
      {QStringLiteral("Transparent"), Vue3D::Boitier::Transparent, QStringLiteral("Boîtier transparent : la carte et sa face arrière se voient à travers")},
      {QStringLiteral("Masqué"), Vue3D::Boitier::Masque, QStringLiteral("Boîtier et couvercle masqués : on tourne autour de la carte seule, dessous compris")}};
  auto* choixBoitier = new QComboBox;
  choixBoitier->setToolTip(QStringLiteral("Visible, transparent (la carte se voit à travers), ou masqué avec le couvercle pour tourner autour de la carte seule. Touche T."));
  for (const auto& [nom, mode, aide] : modesBoitier) {
    auto* a = new QAction(nom, this);
    a->setCheckable(true);
    a->setChecked(mode == Vue3D::Boitier::Visible);
    a->setToolTip(aide);
    groupeBoitier->addAction(a);
    choixBoitier->addItem(nom);
    choixBoitier->setItemData(choixBoitier->count() - 1, aide, Qt::ToolTipRole);
    const Vue3D::Boitier m = mode;
    const int indice = choixBoitier->count() - 1;
    connect(a, &QAction::triggered, this, [this, m, choixBoitier, indice] {
      m_vue3d->setModeBoitier(m);
      const QSignalBlocker b(choixBoitier);
      choixBoitier->setCurrentIndex(indice);
    });
  }
  connect(choixBoitier, &QComboBox::activated, this, [groupeBoitier](int i) { groupeBoitier->actions().at(i)->trigger(); });
  connect(m_vue3d, &Vue3D::modeBoitierChange, this, [choixBoitier, groupeBoitier](int mode) {
    const QSignalBlocker b(choixBoitier);
    choixBoitier->setCurrentIndex(mode);
    groupeBoitier->actions().at(mode)->setChecked(true);
  });
  outilsBoitier->addWidget(choixBoitier);
  auto* suivantBoitier = new QAction(QStringLiteral("Mode du boîtier suivant"), this);
  suivantBoitier->setShortcut(QKeySequence(Qt::Key_T));
  connect(suivantBoitier, &QAction::triggered, this, [this, groupeBoitier] {
    const int i = (static_cast<int>(m_vue3d->modeBoitier()) + 1) % 3;
    groupeBoitier->actions().at(i)->trigger();
  });
  addAction(suivantBoitier);
  m_raccourcisCarte.append(suivantBoitier);
  outilsBoitier->addSeparator();
  QAction* recadrer3d = outilsBoitier->addAction(QStringLiteral("Recadrer"));
  connect(recadrer3d, &QAction::triggered, m_vue3d, &Vue3D::recadrer);

  auto* separation = new QSplitter(Qt::Horizontal);
  separation->addWidget(cadre(QStringLiteral("Carte"), m_carte, outilsCarte));
  separation->addWidget(cadre(QStringLiteral("Boîtier"), m_vue3d, outilsBoitier));
  separation->setStretchFactor(0, 1);
  separation->setStretchFactor(1, 1);
  // Deux ateliers : carte et boîtier électronique, ou conception de pièces (lot 1.B).
  m_ateliers = new QStackedWidget;
  m_ateliers->addWidget(separation);
  m_ateliers->addWidget(new QWidget);  // l'atelier Pièces est créé à sa première ouverture
  // Deux pages : l'accueil (que veux-tu concevoir ?), puis le projet avec son navigateur à gauche.
  m_navigateur = new Navigateur;
  m_navigateur->setVignette([this](int c, int f) { return Vignettes::instance().fichier(m_doc, c, f); });
  auto* zoneProjet = new QSplitter(Qt::Horizontal);
  m_zoneProjet = zoneProjet;
  zoneProjet->addWidget(m_navigateur);
  zoneProjet->addWidget(m_ateliers);
  zoneProjet->setStretchFactor(1, 1);
  zoneProjet->setSizes({310, 1200});
  m_accueil = new Accueil;
  m_pages = new QStackedWidget;
  m_pages->addWidget(m_accueil);
  m_pages->addWidget(zoneProjet);
  setCentralWidget(m_pages);
  connect(m_accueil, &Accueil::choisi, this, [this](const QString& code) { choixAccueil(code); });
  connect(m_accueil, &Accueil::ouvrirRecent, this, [this](const QString& chemin) {
    if (!confirmerAbandon()) return;
    ouvrirFichier(chemin);
    montrerProjet();
  });
  connect(m_navigateur, &Navigateur::ouvrirCarte, this, [this] { montrerAtelierPiece(false); });
  connect(m_navigateur, &Navigateur::ouvrirPiece, this, [this](int i) {
    montrerAtelierPiece(true);
    m_atelier->activerPiece(i);
    majNavigateur();
  });
  connect(m_navigateur, &Navigateur::choisirFonctionPiece, this, [this](int i, int id) {
    montrerAtelierPiece(true);
    m_atelier->activerPiece(i);
    majNavigateur();
    m_atelier->choisirFonctionPublique(id);
  });
  connect(m_navigateur, &Navigateur::nouvellePiece, this, [this] {
    montrerAtelierPiece(true);
    m_atelier->nouvellePiece();
  });
  connect(m_navigateur, &Navigateur::importerKicad, this, [this] { importerKicad(true); });
  // Import de l'arbre (G4) : la carte KiCad va à l'électronique ; l'import pour le boîtier reste au menu Fichier
  connect(m_navigateur, &Navigateur::importerKicadElectronique, this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Importer une carte KiCad"), QString(), QStringLiteral("Cartes KiCad (*.kicad_pcb)"));
    if (!chemin.isEmpty()) importerKicadElectronique(chemin, false);
  });
  connect(m_navigateur, &Navigateur::ouvrirAssemblage, this, [this](int a, int k) { montrerAssemblage(a, k); });
  connect(m_navigateur, &Navigateur::nouvelAssemblage, this, [this] {
    m_doc->modifier([](Projet& p) { Assemblage a; a.nom = "Assemblage " + std::to_string(p.assemblages.size() + 1); p.assemblages.push_back(a); });
    montrerAssemblage(static_cast<int>(m_doc->projet().assemblages.size()) - 1);
  });
  connect(m_navigateur, &Navigateur::supprimerAssemblage, this, [this](int a) {
    // Un assemblage utilisé comme sous-assemblage ne se supprime pas ; les rangs des suivants sont tenus à jour (AS-02)
    {
      Projet essai = m_doc->projet();
      std::string raison;
      if (!supprimerAssemblage(essai, a, &raison)) { QMessageBox::information(this, QStringLiteral("Supprimer l’assemblage"), QString::fromStdString(raison)); return; }
    }
    const QString nom = QString::fromStdString(m_doc->projet().assemblages[static_cast<size_t>(a)].nom);
    if (QMessageBox::question(this, QStringLiteral("Supprimer l’assemblage"), QStringLiteral("Supprimer « %1 » ? Les pièces restent dans le projet.").arg(nom)) != QMessageBox::Yes) return;
    m_doc->modifier([a](Projet& p) { supprimerAssemblage(p, a); });
    montrerAtelierPiece(true);
  });
  connect(m_navigateur, &Navigateur::ouvrirSchema, this, [this](int k) { montrerSchema(k); });
  connect(m_navigateur, &Navigateur::ouvrirContrainteAssemblage, this, [this](int a, int k) { montrerAssemblage(a); m_assemblage->choisirContrainte(k); });
  connect(m_navigateur, &Navigateur::basculerContrainteAssemblage, this, [this](int a, int k) {
    m_doc->modifier([a, k](Projet& p) {
      if (a < static_cast<int>(p.assemblages.size()) && k < static_cast<int>(p.assemblages[static_cast<size_t>(a)].contraintes.size())) {
        auto& c = p.assemblages[static_cast<size_t>(a)].contraintes[static_cast<size_t>(k)];
        c.suspendue = !c.suspendue;
      }
    });
  });
  connect(m_navigateur, &Navigateur::supprimerContrainteAssemblage, this, [this](int a, int k) {
    m_doc->modifier([a, k](Projet& p) { if (a < static_cast<int>(p.assemblages.size()) && k < static_cast<int>(p.assemblages[a].contraintes.size())) p.assemblages[a].contraintes.erase(p.assemblages[a].contraintes.begin() + k); });
  });
  connect(m_navigateur, &Navigateur::ouvrirIntegration, this, [this] {
    if (!m_integration) { m_integration = new PageIntegration(m_doc); m_ateliers->addWidget(m_integration); }
    m_pages->setCurrentIndex(1);
    m_ateliers->setCurrentWidget(m_integration);
    if (m_dock) m_dock->hide();
    m_integration->actualiser();
    majNavigateur();
  });
  connect(m_navigateur, &Navigateur::ouvrirNomenclature, this, [this] {
    if (!m_nomenclature) { m_nomenclature = new PageNomenclature(m_doc); m_ateliers->addWidget(m_nomenclature); }
    m_pages->setCurrentIndex(1);
    m_ateliers->setCurrentWidget(m_nomenclature);
    if (m_dock) m_dock->hide();
    m_nomenclature->actualiser();
    majNavigateur();
  });
  connect(m_navigateur, &Navigateur::ajouterSchema, this, [this] { ajouterSchemaQet(false); });
  connect(m_navigateur, &Navigateur::retirerSchema, this, [this](int k) {
    m_doc->modifier([k](Projet& p) { if (k < static_cast<int>(p.schemasElectriques.size())) p.schemasElectriques.erase(p.schemasElectriques.begin() + k); });
  });
  connect(m_navigateur, &Navigateur::ouvrirSchemaExterne, this, [this](int k) {
    const auto& s = m_doc->projet().schemasElectriques;
    if (k < static_cast<int>(s.size())) QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(s[k].fichier)));
  });
  connect(m_navigateur, &Navigateur::ouvrirPlan, this, [this](int i, int k) {
    montrerAtelierPiece(true);
    m_atelier->activerPiece(i);
    majNavigateur();
    m_atelier->ouvrirMiseEnPlan(k);
  });
  connect(m_navigateur, &Navigateur::renommerPlan, this, [this](int i, int k) {
    montrerAtelierPiece(true);
    m_atelier->activerPiece(i);
    m_atelier->renommerMiseEnPlan(k);
    majNavigateur();
  });
  connect(m_navigateur, &Navigateur::supprimerPlan, this, [this](int i, int k) {
    montrerAtelierPiece(true);
    m_atelier->activerPiece(i);
    m_atelier->supprimerMiseEnPlan(k);
    majNavigateur();
  });
  connect(m_navigateur, &Navigateur::nouveauPlan, this, [this](int i) {
    montrerAtelierPiece(true);
    m_atelier->activerPiece(i);
    majNavigateur();
    m_atelier->nouvelleMiseEnPlan();
  });
  connect(m_navigateur, &Navigateur::ouvrirDansKicad, this, [this] {
    if (m_doc->projet().kicad) QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(m_doc->projet().kicad->fichier)));
  });
  connect(m_navigateur, &Navigateur::supprimerPiece, this, [this](int i) {
    const auto& ps = m_doc->projet().pieces;
    if (i < 0 || i >= static_cast<int>(ps.size())) return;
    const QString nom = QString::fromStdString(ps[i].value("nom", std::string("cette pièce")));
    // Les assemblages qui l'utilisent sont nommés ; ses composants y sont retirés avec leurs contraintes, et les
    // rangs des autres pièces suivent (AS-01)
    QString usages;
    for (int a : assemblagesUtilisantPiece(m_doc->projet(), i)) usages += QStringLiteral("<li>%1</li>").arg(QString::fromStdString(m_doc->projet().assemblages[static_cast<size_t>(a)].nom).toHtmlEscaped());
    const QString texte = usages.isEmpty() ? QStringLiteral("Supprimer « %1 » et toutes ses fonctions ?").arg(nom.toHtmlEscaped())
                                           : QStringLiteral("« %1 » est utilisée dans :<ul>%2</ul>Ses composants y seront retirés, avec les contraintes qui les visent. Supprimer la pièce et toutes ses fonctions ?")
                                                 .arg(nom.toHtmlEscaped(), usages);
    if (QMessageBox::question(this, QStringLiteral("Supprimer la pièce"), texte) != QMessageBox::Yes) return;
    if (m_atelier) m_atelier->terminerEdition();
    m_doc->modifier([i](Projet& p) { supprimerPiece(p, i); });
  });
  connect(m_navigateur, &Navigateur::menuDansPieceActive, this, [this](const QPoint& p) { if (m_atelier) m_atelier->menuArbreExterne(p); });
  // Électronique : conceptions (schéma, circuit imprimé, 3D, fabrication) rangées dans l'arbre
  connect(m_navigateur, &Navigateur::ouvrirElectronique, this, [this](int i, int mode, int feuille) { montrerElectronique(i, mode, feuille); });
  connect(m_navigateur, &Navigateur::ouvrirComposantElec, this, [this](int i, const QString& uuid) {
    montrerElectronique(i, AtelierElectronique::Schema);
    if (m_electronique) m_electronique->montrerComposant(uuid.toStdString());
  });
  connect(m_navigateur, &Navigateur::ouvrirEmpreinteElec, this, [this](int i, const QString& uuid) {
    montrerElectronique(i, AtelierElectronique::Circuit);
    if (!m_electronique) return;
    const std::string id = uuid.toStdString();
    m_electronique->selection()->choisir({{SelectionElectronique::Genre::Empreinte, id, id}});
    if (const prom::ecad::ConceptionElec* c = m_electronique->conceptionCourante())
      for (const auto& e : c->circuit.empreintes)
        if (e.composant == id) m_electronique->vueCircuit()->centrerSur(e.position);
  });
  connect(m_navigateur, &Navigateur::surlignerReseauElec, this, [this](int i, const QString& reseau) {
    if (!m_electronique || m_electronique->conception() != i) montrerElectronique(i, AtelierElectronique::Schema);
    if (m_electronique) m_electronique->surligner(reseau);
  });
  connect(m_navigateur, &Navigateur::supprimerComposantElec, this, [this](int i, const QString& uuid) {
    montrerElectronique(i, AtelierElectronique::Schema);
    if (m_electronique) m_electronique->supprimerComposant(uuid.toStdString());
  });
  connect(m_navigateur, &Navigateur::supprimerFeuilleElec, this, [this](int i, int f) {
    montrerElectronique(i, AtelierElectronique::Schema, f);
    if (m_electronique) m_electronique->supprimerFeuille(f);
  });
  connect(m_navigateur, &Navigateur::nouvelleConceptionElec, this, [this] { nouvelleConceptionElec(false, false); });
  // Renommer sur place dans l'arbre, supprimer sans fenêtre : l'un et l'autre s'annulent (G7)
  connect(m_navigateur, &Navigateur::renommerConceptionElec, this, [this](int i, const QString& nom) {
    const auto& e = m_doc->projet().electronique;
    if (i < 0 || i >= static_cast<int>(e.size()) || nom.isEmpty() || nom.toStdString() == e[static_cast<size_t>(i)].nom) return;
    m_doc->modifier([i, nom](Projet& p) { if (i < static_cast<int>(p.electronique.size())) p.electronique[static_cast<size_t>(i)].nom = nom.toStdString(); },
                    QStringLiteral("Renommer la carte"));
  });
  connect(m_navigateur, &Navigateur::supprimerConceptionElec, this, [this](int i) {
    const auto& e = m_doc->projet().electronique;
    if (i < 0 || i >= static_cast<int>(e.size())) return;
    const QString nom = QString::fromStdString(e[static_cast<size_t>(i)].nom);
    const bool ouverte = atelierElectroniqueActif() && m_electronique->conception() == i;
    const int avant = m_electronique ? m_electronique->conception() : -1;
    m_doc->modifier([i](Projet& p) { if (i < static_cast<int>(p.electronique.size())) p.electronique.erase(p.electronique.begin() + i); },
                    QStringLiteral("Supprimer la carte"));
    // L'atelier suit : la carte voisine si celle qui était ouverte part, l'accueil s'il n'en reste aucune
    const int reste = static_cast<int>(m_doc->projet().electronique.size());
    if (atelierElectroniqueActif()) {
      if (reste == 0) montrerAtelierSelonProjet();
      else if (ouverte) montrerElectronique(std::min(i, reste - 1), AtelierElectronique::Schema);
      else if (avant > i) montrerElectronique(avant - 1, m_electronique->mode());
    }
    const QString texte = QStringLiteral("« %1 » supprimée. Ctrl+Z la rétablit.").arg(nom.toHtmlEscaped());
    if (atelierElectroniqueActif()) m_electronique->afficherMessage(texte, NiveauMessage::Information);
    else statusBar()->showMessage(QStringLiteral("« %1 » supprimée. Ctrl+Z la rétablit.").arg(nom), 8000);
  });
  // Électrique : projets conçus ici (folios, armoire, faisceau, rapports, calculs, logement)
  connect(m_navigateur, &Navigateur::ouvrirElectrique, this, [this](int i, int mode, int folio) { montrerElectrique(i, mode, folio); });
  connect(m_navigateur, &Navigateur::nouveauProjetElectrique, this, [this] { nouveauProjetElectrique(QStringLiteral("schema"), false); });
  connect(m_navigateur, &Navigateur::importerQetElectrique, this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Importer un projet QElectroTech"), QString(), QStringLiteral("Projets QElectroTech (*.qet)"));
    if (!chemin.isEmpty()) importerQetElectrique(chemin, false);
  });
  connect(m_navigateur, &Navigateur::renommerProjetElectrique, this, [this](int i) {
    const auto& e = m_doc->projet().electrique;
    if (i < 0 || i >= static_cast<int>(e.size())) return;
    bool ok = false;
    const QString nom = QInputDialog::getText(this, QStringLiteral("Renommer"), QStringLiteral("Nom du projet électrique :"), QLineEdit::Normal, QString::fromStdString(e[static_cast<size_t>(i)].nom), &ok).trimmed();
    if (!ok || nom.isEmpty()) return;
    m_doc->modifier([i, nom](Projet& p) {
      if (i < static_cast<int>(p.electrique.size())) {
        p.electrique[static_cast<size_t>(i)].nom = nom.toStdString();
        p.electrique[static_cast<size_t>(i)].schema.cartouche.titre = nom.toStdString();
      }
    });
  });
  connect(m_navigateur, &Navigateur::supprimerProjetElectrique, this, [this](int i) {
    const auto& e = m_doc->projet().electrique;
    if (i < 0 || i >= static_cast<int>(e.size())) return;
    if (QMessageBox::question(this, QStringLiteral("Supprimer le projet électrique"),
                              QStringLiteral("Supprimer « %1 », ses folios, son armoire et ses rapports ?").arg(QString::fromStdString(e[static_cast<size_t>(i)].nom))) != QMessageBox::Yes)
      return;
    m_doc->modifier([i](Projet& p) { if (i < static_cast<int>(p.electrique.size())) p.electrique.erase(p.electrique.begin() + i); });
  });
  connect(m_doc, &Document::change, this, [this] { QTimer::singleShot(0, this, [this] { suivreConceptionsElec(); suivreProjetsElectriques(); }); });
  connect(m_doc, &Document::change, this, [this] { m_arbrePerime = true; });
  connect(m_doc, &Document::conceptionChangee, this, [this](const prom::ecad::ChangementConception& ch) {
    if (m_doc->enGeste()) return;  // pendant un glissement, l'arbre attend la fin du geste (9.4)
    const auto it = m_signaturesArbre.find(ch.conception);
    const prom::ecad::ConceptionElec* c = nullptr;
    for (const auto& x : m_doc->projet().electronique)
      if (x.uuid == ch.conception) c = &x;
    if (!c || it == m_signaturesArbre.end() || it->second != Navigateur::signatureConception(*c)) m_arbrePerime = true;
    else if (m_navigateur) {
      // Même structure : repères et valeurs écrits sur place, l'arbre n'est pas refait (3.11, G5)
      for (std::size_t i = 0; i < m_doc->projet().electronique.size(); ++i)
        if (&m_doc->projet().electronique[i] == c) m_navigateur->majTextes(static_cast<int>(i), *c);
    }
  });
  connect(m_doc, &Document::etatChange, this, [this] {
    if (m_arbrePerime || m_doc->chemin() != m_cheminArbre) majNavigateur();
  });
  auto* palette = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
  connect(palette, &QShortcut::activated, this, [this] { paletteCommandes(); });

  auto* dock = new QDockWidget(QStringLiteral("Détails"), this);
  m_dock = dock;
  dock->setObjectName(QStringLiteral("details"));
  dock->setWidget(m_panneau);
  dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
  addDockWidget(Qt::RightDockWidgetArea, dock);

  // Menus
  m_cadreIntegre = !m_dansCoquille && qEnvironmentVariableIsEmpty("PROMETHEE_CADRE_SYSTEME");
  QMenuBar* barreMenus = menuBar();
  if (m_cadreIntegre) {
    setWindowFlag(Qt::FramelessWindowHint);
    m_barreTitre = new BarreTitre(this);
    barreMenus = m_barreTitre->menus();
    setMenuWidget(m_barreTitre);
    connect(m_barreTitre, &BarreTitre::rechercheDemandee, this, [this] { paletteCommandes(); });
    setMouseTracking(true);
    setContentsMargins(5, 5, 5, 5);  // bords pour redimensionner la fenêtre
  }
  QMenu* fichier = barreMenus->addMenu(QStringLiteral("&Fichier"));
  marquerAction(fichier->addAction(icone(QStringLiteral("dossier")), QStringLiteral("&Nouveau projet…"), QKeySequence::New, this, [this] { nouveauProjetDepuisMenu(); }), "projet.nouveau");
  {
    // Créer directement, sans passer par l'accueil
    QMenu* creer = fichier->addMenu(QStringLiteral("Créer"));
    const std::tuple<const char*, const char*, const char*> choix[] = {
        {"piece", "Pièce 3D", "piece"},
        {"assemblage", "Assemblage", "assemblage"},
        {"tolerie", "Tôlerie", "tolerie"},
        {"carte-nouvelle", "Carte électronique", "pcb"},
        {"schema-elec", "Schéma électronique", "schema"},
        {"electrique-schema", "Schéma électrique (armoire, machine)", "contacteur"},
        {"electrique-distribution", "Installation électrique (calcul)", "distribution"},
        {"electrique-logement", "Tableau de logement (NF C 15-100)", "logement"},
        {"electrique-irve", "Bornes de recharge (IRVE)", "irve"},
        {"electrique-photovoltaique", "Installation photovoltaïque", "photovoltaique"},
        {"electrique-grafcet", "Grafcet (automatisme)", "grafcet"},
        {"electrique-apprendre", "Apprendre pas à pas", "apprendre"},
        {"electrique-armoire", "Armoire électrique", "armoire"},
        {"electrique-faisceau", "Faisceau", "faisceau"},
    };
    for (const auto& [code, texte, ic] : choix) {
      const QString c = QString::fromLatin1(code);
      creer->addAction(icone(QString::fromLatin1(ic)), QString::fromUtf8(texte), this, [this, c] { choixAccueil(c); });
    }
  }
  QAction* ouvrir = fichier->addAction(QStringLiteral("&Ouvrir…"), QKeySequence::Open, this, [this] {
    if (!confirmerAbandon()) return;
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir un projet"), QString(), QStringLiteral("Fichiers Prométhée (*.prom *.json)"));
    if (!chemin.isEmpty()) ouvrirFichier(chemin);
  });
  m_importKicadMeca = fichier->addAction(QStringLiteral("&Importer une carte KiCad (boîtier)…"), QKeySequence(Qt::CTRL | Qt::Key_I), this, [this] { importerKicad(); });
  m_importKicadElec = fichier->addAction(QStringLiteral("Importer une carte KiCad (électronique)…"), this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Importer une carte KiCad"), QString(), QStringLiteral("Cartes KiCad (*.kicad_pcb)"));
    if (!chemin.isEmpty()) importerKicadElectronique(chemin, false);
  });
  m_importKicadMeca->setObjectName(QStringLiteral("menu:importKicadMeca"));
  // 0.34 (10.5, 10.12) : projets, schémas et cartes des autres logiciels, avec le tableau des couches
  QAction* importTiers = fichier->addAction(QStringLiteral("Importer d’un autre logiciel (KiCad, EAGLE)…"), this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Importer d’un autre logiciel"), QString(),
                                                        QStringLiteral("Projets, schémas et cartes (%1)").arg(QString::fromStdString(prom::ecad::filtreConceptionsTierces())));
    if (!chemin.isEmpty()) importerConceptionTierce(chemin, 1);
  });
  importTiers->setObjectName(QStringLiteral("menu:importTiers"));
  m_importKicadElec->setObjectName(QStringLiteral("menu:importKicadElec"));
  m_actionsMeca << m_importKicadMeca;
  fichier->addAction(icone(QStringLiteral("cablage")), QStringLiteral("Importer un projet QElectroTech (.qet)…"), this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Importer un projet QElectroTech"), QString(), QStringLiteral("Projets QElectroTech (*.qet)"));
    if (!chemin.isEmpty()) importerQetElectrique(chemin, m_doc->estVierge());
  });
  fichier->addAction(icone(QStringLiteral("importer-cao")), QStringLiteral("Importer un fichier CAO (STEP, IGES, BREP, STL)…"), this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Importer un fichier CAO"), QString(), QStringLiteral("CAO (*.step *.stp *.igs *.iges *.brep *.brp *.stl)"));
    if (!chemin.isEmpty()) importerFichierCAO(chemin);
  });
  m_actionsMeca << fichier->addAction(QStringLiteral("Exporter vers &KiCad…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), this, [this] { exporterKicad(); });
  marquerAction(ouvrir, "projet.ouvrir");
  marquerAction(fichier->addAction(QStringLiteral("&Enregistrer"), QKeySequence::Save, this, [this] { enregistrer(false); }), "projet.enregistrer");
  marquerAction(fichier->addAction(QStringLiteral("Enregistrer &sous…"), QKeySequence::SaveAs, this, [this] { enregistrer(true); }), "projet.enregistrerSous");
  // Pour un collègue qui n'a pas migré : un fichier neuf lisible par une version précédente, pertes dites d'abord (1.22)
  fichier->addAction(QStringLiteral("Enregistrer pour une version précédente…"), this, [this] { enregistrerPourVersionPrecedente(); })
      ->setObjectName(QStringLiteral("menu:pourVersionPrecedente"));
  fichier->addSeparator();
  m_actionsMeca << fichier->addAction(QStringLiteral("Recharger l’exemple"), this, [this] { if (confirmerAbandon()) m_doc->exemple(); });
  m_dossierMeca = fichier->addAction(QStringLiteral("E&xporter le dossier de fabrication…"), QKeySequence(Qt::CTRL | Qt::Key_E), this, [this] { exporterDossier(); });
  m_dossierMeca->setObjectName(QStringLiteral("menu:dossierMeca"));
  m_actionsMeca << m_dossierMeca;
  m_dossierElec = fichier->addAction(QStringLiteral("E&xporter le dossier de fabrication électronique…"), this, [this] {
    if (m_electronique) m_electronique->exporterFabricationInteractif();
  });
  m_dossierElec->setObjectName(QStringLiteral("menu:dossierElec"));
  m_actionsElec << m_dossierElec;
  fichier->addSeparator();
  // Réglages (1.19) : souris et pavé tactile d'abord ; les pages suivantes viennent avec leurs fonctions
  QAction* reglages = fichier->addAction(QStringLiteral("Réglages…"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this] { ouvrirReglage({}); });
  reglages->setObjectName(QStringLiteral("menu:reglages"));
  fichier->addAction(QStringLiteral("&Quitter"), QKeySequence::Quit, this, &QWidget::close);

  QMenu* edition = barreMenus->addMenu(QStringLiteral("É&dition"));
  // Annuler et rétablir : pendant une esquisse, ils agissent sur l'esquisse elle-même.
  QAction* annuler = edition->addAction(icone(QStringLiteral("annuler-fleche")), QStringLiteral("&Annuler"), this, [this] {
    if (m_atelier && m_ateliers->currentIndex() == 1 && m_atelier->enEsquisse()) m_atelier->annulerDansEsquisse();
    else m_doc->annuler();
  });
  // Raccourcis sans doublon : sous Windows, Qt donne déjà Ctrl+Y à « Rétablir » ; une même touche déclarée deux
  // fois est jugée ambiguë et ne fait plus rien.
  auto sansDoublon = [](QList<QKeySequence> l) {
    QList<QKeySequence> r;
    for (const QKeySequence& k : l) if (!k.isEmpty() && !r.contains(k)) r.append(k);
    return r;
  };
  annuler->setShortcuts(sansDoublon(QKeySequence::keyBindings(QKeySequence::Undo)));
  QAction* retablir = edition->addAction(icone(QStringLiteral("retablir-fleche")), QStringLiteral("&Rétablir"), this, [this] {
    if (m_atelier && m_ateliers->currentIndex() == 1 && m_atelier->enEsquisse()) m_atelier->retablirDansEsquisse();
    else m_doc->retablir();
  });
  retablir->setShortcuts(sansDoublon(QKeySequence::keyBindings(QKeySequence::Redo) + QList<QKeySequence>{QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)}));
  annuler->setObjectName(QStringLiteral("actionAnnuler"));
  retablir->setObjectName(QStringLiteral("actionRetablir"));
  marquerAction(annuler, "historique.annuler");
  marquerAction(retablir, "historique.retablir");
  annuler->setToolTip(QStringLiteral("Annuler (Ctrl+Z)"));
  retablir->setToolTip(QStringLiteral("Rétablir (Ctrl+Y)"));
  edition->addSeparator();
  m_raccourcisCarte.append(edition->addAction(QStringLiteral("Masquer la sélection dans la 3D"), QKeySequence(Qt::Key_H), this, [this] { m_vue3d->masquerSelection(); }));
  m_raccourcisCarte.append(edition->addAction(QStringLiteral("Tout afficher dans la 3D"), QKeySequence(Qt::SHIFT | Qt::Key_H), this, [this] { m_vue3d->toutAfficher(); }));
  m_actionsMeca << m_raccourcisCarte;

  QMenu* ajouter = barreMenus->addMenu(QStringLiteral("&Ajouter"));
  m_menuAjouter = ajouter;
  ajouter->addAction(QStringLiteral("Trou de fixation"), this, [this] {
    std::string id;
    m_doc->modifier([&](Projet& p) { id = ajouterTrou(p); });
    m_doc->selectionner({Cible::Trou, id});
  });
  ajouter->addSeparator();
  for (const auto& T : typesComposants()) {
    const std::string cle = T.cle;
    QString nom = QString::fromStdString(T.nom);
    if (T.bord) nom += QStringLiteral(" (perce la paroi)");
    else if (T.couvercle > 0) nom += QStringLiteral(" (perce le couvercle)");
    ajouter->addAction(nom, this, [this, cle] {
      std::string id;
      m_doc->modifier([&](Projet& p) { id = ajouterComposant(p, cle); });
      m_doc->selectionner({Cible::Composant, id});
    });
  }

  QMenu* affichage = barreMenus->addMenu(QStringLiteral("Affic&hage"));
  m_raccourcisCarte.append(affichage->addAction(QStringLiteral("Recadrer les deux vues"), QKeySequence(Qt::Key_F), this, [this] { m_carte->recadrer(); m_vue3d->recadrer(); }));
  m_actionsMeca << m_raccourcisCarte.back() << affichage->addSection(QStringLiteral("Vues de la 3D"));
  const std::tuple<QString, V3d_TypeOfOrientation, QKeySequence> vues[] = {
      {QStringLiteral("Isométrique"), V3d_XnegYnegZpos, QKeySequence(Qt::Key_0)}, {QStringLiteral("Face"), V3d_Yneg, QKeySequence(Qt::Key_1)},
      {QStringLiteral("Dessus"), V3d_Zpos, QKeySequence(Qt::Key_7)}, {QStringLiteral("Droite"), V3d_Xpos, QKeySequence(Qt::Key_3)},
      {QStringLiteral("Gauche"), V3d_Xneg, QKeySequence()}, {QStringLiteral("Arrière"), V3d_Ypos, QKeySequence()}, {QStringLiteral("Dessous"), V3d_Zneg, QKeySequence()}};
  for (const auto& [nom, orientation, raccourci] : vues) {
    const V3d_TypeOfOrientation o = orientation;
    QAction* a = affichage->addAction(nom, this, [this, o] { m_vue3d->vueStandard(o); });
    a->setShortcut(raccourci);
    m_raccourcisCarte.append(a);
    m_actionsMeca << a;
  }
  m_actionsMeca << affichage->addSection(QStringLiteral("Couvercle"));
  affichage->addActions(groupeCouvercle->actions());
  m_actionsMeca << groupeCouvercle->actions();
  m_actionsMeca << affichage->addSection(QStringLiteral("Boîtier"));
  affichage->addActions(groupeBoitier->actions());
  m_actionsMeca << groupeBoitier->actions();
  construireMenusElectronique(fichier, edition, affichage);
  affichage->addSection(QStringLiteral("Apparence"));
  affichage->addMenu(Theme::instance().menu(affichage));

  QMenu* aide = barreMenus->addMenu(QStringLiteral("Ai&de"));
  m_menus = {fichier, edition, ajouter, m_menuAjouterElec, affichage, aide};
  barreMenus->insertMenu(affichage->menuAction(), m_menuAjouterElec);
  aide->addAction(QStringLiteral("À propos de Prométhée"), this, [this] {
    QMessageBox apropos(this);
    apropos.setWindowTitle(QStringLiteral("À propos de Prométhée"));
    apropos.setIconPixmap(logoComplet(128));
    const prom::InfosVersion& v = prom::infosVersion();
    apropos.setText(QStringLiteral("<h3>Prométhée %1</h3><p>Plateforme libre d’ingénierie intégrée : pièces, assemblages, mise en plan, schéma et circuit imprimé "
                                   "dans un seul modèle.</p>"
                                   "<p>Build <b>%2</b><br>Révision des sources %3%4<br>Qt %5 · Open CASCADE %6</p>"
                                   "<p>Logiciel libre sous licence GPL-3.0 : la source correspondante accompagne chaque paquet (dossier « source »). "
                                   "Prométhée ne se connecte à aucun réseau.</p><p>Journal de ce lancement : %7</p><pre>%8</pre>")
                        .arg(QString::fromLatin1(v.version), QString::fromLatin1(v.identifiant), QString::fromLatin1(v.revision),
                             std::strlen(v.dateRevision) >= 10 ? QStringLiteral(" du ") + QString::fromLatin1(v.dateRevision).left(10) : QString(),
                             QString::fromLatin1(qVersion()), QStringLiteral(OCC_VERSION_COMPLETE), QDir::toNativeSeparators(QString::fromUtf8(cheminJournal())).toHtmlEscaped(),
                             m_vue3d->infoGl().toHtmlEscaped()));
    apropos.exec();
  });

  auto* barre = addToolBar(QStringLiteral("Principale"));
  {
    auto* groupeAtelier = new QActionGroup(this);
    QAction* accueil = barre->addAction(icone(QStringLiteral("accueil")), QStringLiteral("Accueil"), this, [this] { montrerAccueil(); });
    accueil->setToolTip(QStringLiteral("Accueil : créer ou ouvrir un projet"));
    if (QToolButton* b = qobject_cast<QToolButton*>(barre->widgetForAction(accueil))) b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    QAction* boitier = new QAction(QStringLiteral("Carte et boîtier"), this);
    QAction* pieces = new QAction(QStringLiteral("Pièces"), this);
    addAction(boitier);
    addAction(pieces);
    for (QAction* a : {boitier, pieces}) { a->setCheckable(true); groupeAtelier->addAction(a); }
    boitier->setChecked(true);
    m_actionBoitier = boitier;
    m_actionPieces = pieces;
    boitier->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    pieces->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    boitier->setToolTip(QStringLiteral("Atelier carte électronique et boîtier (Ctrl+1)"));
    pieces->setToolTip(QStringLiteral("Atelier de conception de pièces : esquisses, extrusions, congés… (Ctrl+2)"));
    connect(boitier, &QAction::triggered, this, [this] { montrerAtelierPiece(false); });
    connect(pieces, &QAction::triggered, this, [this] { montrerAtelierPiece(true); });
    // Atelier électronique : la carte ouverte en dernier, ou une nouvelle si le projet n'en a pas
    QAction* electronique = new QAction(icone(QStringLiteral("pcb")), QStringLiteral("Atelier électronique"), this);
    addAction(electronique);
    electronique->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    electronique->setToolTip(QStringLiteral("Atelier électronique : schéma, circuit imprimé, 3D, fabrication (Ctrl+3)"));
    connect(electronique, &QAction::triggered, this, [this] {
      const int n = static_cast<int>(m_doc->projet().electronique.size());
      if (n == 0) {
        // Rien n'est créé (G7) : l'accueil, ouvert sur l'électronique ; la carte choisie s'ajoute au projet ouvert
        montrerAccueil();
        m_accueil->choisirMetier(QStringLiteral("elec"));
        m_accueilAjoutElec = !m_doc->estVierge();
        return;
      }
      montrerProjet();
      montrerElectronique(m_electronique ? std::clamp(m_electronique->conception(), 0, n - 1) : 0, m_electronique ? m_electronique->mode() : AtelierElectronique::Schema);
    });
    // Atelier électrique : le projet électrique ouvert en dernier, ou un nouveau si le projet n'en a pas
    QAction* electrique = new QAction(icone(QStringLiteral("contacteur")), QStringLiteral("Atelier électrique"), this);
    addAction(electrique);
    electrique->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_4));
    electrique->setToolTip(QStringLiteral("Atelier électrique : folios, armoire, faisceau, rapports, calculs (Ctrl+4)"));
    connect(electrique, &QAction::triggered, this, [this] {
      const int n = static_cast<int>(m_doc->projet().electrique.size());
      if (n == 0) {
        nouveauProjetElectrique(QStringLiteral("schema"), false);
        return;
      }
      montrerProjet();
      montrerElectrique(m_electrique ? std::clamp(m_electrique->projet(), 0, n - 1) : 0, m_electrique ? m_electrique->mode() : AtelierElectrique::Schema);
    });
    barre->addSeparator();
  }
  barre->setObjectName(QStringLiteral("principale"));
  barre->addAction(annuler);
  barre->addAction(retablir);
  barre->setIconSize(QSize(22, 22));
  if (QToolButton* b = qobject_cast<QToolButton*>(barre->widgetForAction(annuler))) b->setToolButtonStyle(Qt::ToolButtonIconOnly);
  if (QToolButton* b = qobject_cast<QToolButton*>(barre->widgetForAction(retablir))) b->setToolButtonStyle(Qt::ToolButtonIconOnly);
  barre->addSeparator();
  QAction* verrouiller = barre->addAction(QStringLiteral("Verrouiller"));
  verrouiller->setCheckable(true);
  verrouiller->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
  verrouiller->setToolTip(QStringLiteral("Verrouiller la position du composant sélectionné (Ctrl+L)"));
  connect(verrouiller, &QAction::triggered, this, [this](bool on) {
    const Cible s = m_doc->selection();
    if (s.type != Cible::Composant) return;
    m_doc->modifier([&](Projet& p) { for (auto& k : p.composants) if (k.id == s.id) k.verrou = on; });
  });
  auto majVerrou = [this, verrouiller] {
    const Composant* k = m_doc->composantSelectionne();
    verrouiller->setEnabled(k != nullptr);
    verrouiller->setChecked(k && k->verrou);
    verrouiller->setText(k && k->verrou ? QStringLiteral("Verrouillé") : QStringLiteral("Verrouiller"));
  };
  connect(m_doc, &Document::selectionChangee, this, majVerrou);
  connect(m_doc, &Document::change, this, majVerrou);
  majVerrou();
  edition->addSeparator();
  edition->addAction(verrouiller);
  barre->addSeparator();
  QAction* exporter = barre->addAction(QStringLiteral("Dossier de fabrication"));
  connect(exporter, &QAction::triggered, this, [this] { exporterDossier(); });
  fichier->addAction(exporter);
  for (QAction* x : barre->actions()) if (x->text() == QStringLiteral("Accueil")) m_actionsRapides << x;
  m_actionsRapides << annuler << retablir;
  if (m_dansCoquille) {
    // Dans un onglet : la barre de titre de la fenêtre à onglets porte les menus et les accès rapides ; la ligne
    // d'état (message de l'atelier, validité du modèle) monte dans la barre des onglets
    statusBar()->hide();
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString& m) { if (!m.isEmpty()) { m_messageAtelier = m.toHtmlEscaped(); emit etatTexteChange(); } });
    menuBar()->hide();
    barre->hide();
    barre->toggleViewAction()->setVisible(false);
    setWindowFlags(Qt::Widget);
  }
  if (m_barreTitre) {
    for (QAction* x : barre->actions()) if (x->text() == QStringLiteral("Accueil")) m_barreTitre->ajouterAction(x);
    m_barreTitre->ajouterSeparateur();
    m_barreTitre->ajouterAction(annuler);
    m_barreTitre->ajouterAction(retablir);
    barre->hide();
    barre->toggleViewAction()->setVisible(false);
  }

  m_etat = new QLabel;
  m_etat->setTextFormat(Qt::RichText);
  connect(m_etat, &QLabel::linkActivated, this, [this] { if (m_atelier) m_atelier->montrerPremiereErreur(); });
  statusBar()->addPermanentWidget(m_etat);
  // La validité du modèle mécanique (carte, boîtier, pièces) n'a pas de sens dans l'atelier électronique, qui a la sienne
  connect(m_ateliers, &QStackedWidget::currentChanged, this, [this] {
    // Quitter l'atelier Pièces termine l'esquisse ou le réglage ouverts : rien ne reste suspendu, navigateur grisé
    // compris (AP-21)
    if (m_atelier && m_ateliers->currentWidget() != m_atelier && m_atelier->enEdition()) m_atelier->terminerEdition();
    if (m_majHistorique) m_majHistorique();
    placerArbre();
    m_etat->setVisible((!m_electronique || m_ateliers->currentWidget() != m_electronique) && (!m_electrique || m_ateliers->currentWidget() != m_electrique));
    majRaccourcisCarte();
    emit etatTexteChange();
  });
  connect(m_pages, &QStackedWidget::currentChanged, this, [this](int page) {
    if (page != 0) m_accueilAjoutElec = false;
    majRaccourcisCarte();
  });
  majRaccourcisCarte();
  m_majHistorique = [annuler, retablir, this] {
    const bool esquisse = m_atelier && m_ateliers->currentIndex() == 1 && m_atelier->enEsquisse();
    // Réglage de fonction ouvert : l'historique du projet attend sa fermeture (OK ou Annuler), sans quoi une
    // annulation serait aussitôt écrasée par le brouillon (AP-03)
    const bool reglage = m_atelier && m_ateliers->currentIndex() == 1 && m_atelier->enReglage();
    annuler->setEnabled(!reglage && (esquisse ? m_atelier->peutAnnulerDansEsquisse() : m_doc->peutAnnuler()));
    retablir->setEnabled(!reglage && (esquisse ? m_atelier->peutRetablirDansEsquisse() : m_doc->peutRetablir()));
    // Ce que feront Annuler et Rétablir, nommé (SAI-24) : « Annuler : Valeur de R3 : 10k → 4k7 »
    auto nommer = [](QAction* a, const QString& verbe, const QString& nom, const QString& touche) {
      QString court = nom.size() > 48 ? nom.left(47) + QChar(0x2026) : nom;
      court.replace(QLatin1Char('&'), QStringLiteral("&&"));
      a->setText(court.isEmpty() ? QLatin1Char('&') + verbe : QStringLiteral("&%1 : %2").arg(verbe, court));
      a->setToolTip(nom.isEmpty() ? QStringLiteral("%1 (%2)").arg(verbe, touche) : QStringLiteral("%1 : %2 (%3)").arg(verbe, nom, touche));
    };
    nommer(annuler, QStringLiteral("Annuler"), esquisse || reglage ? QString() : m_doc->nomAnnuler(), QStringLiteral("Ctrl+Z"));
    nommer(retablir, QStringLiteral("Rétablir"), esquisse || reglage ? QString() : m_doc->nomRetablir(), QStringLiteral("Ctrl+Y"));
    majEtat();
  };
  connect(m_doc, &Document::etatChange, this, [this] { m_majHistorique(); });
  connect(m_doc, &Document::change, this, [this] { majEtat(); });
  m_majHistorique();
  resize(1440, 860);
}

// Touches simples de la carte et du boîtier (F, 0, 1, 3, 7, H, Maj+H, B, T) : actives seulement quand leur page
// est affichée. Ailleurs, elles visaient une vue cachée et rendaient ambiguës les mêmes touches de l'atelier Pièces
// et de l'esquisseur, qui n'en déclenchaient plus aucune (AP-05, EI-05).
void FenetrePrincipale::majRaccourcisCarte() {
  majMenusAtelier();
  const bool carte = m_pages && m_pages->currentIndex() == 1 && m_ateliers && m_ateliers->currentIndex() == 0;
  for (QAction* a : m_raccourcisCarte) a->setEnabled(carte);
  retenirAtelier();
}

void FenetrePrincipale::majEtat() {
  const int erreurs = m_doc->nombreErreurs();
  const int alertes = static_cast<int>(m_doc->problemes().size()) - erreurs;
  int erreursPiece = 0;
  QString premiere;
  if (m_atelier && m_ateliers->currentIndex() == 1) { erreursPiece = m_atelier->nombreErreurs(); premiere = m_atelier->premiereErreur(); }
  const int toutes = erreurs + erreursPiece;
  if (toutes)
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'><b>✖ %1 erreur%2</b></span> · %3 avertissement%4%5")
                        .arg(toutes).arg(toutes > 1 ? "s" : "").arg(alertes).arg(alertes > 1 ? "s" : "")
                        .arg(premiere.isEmpty() ? QString() : QStringLiteral(" — %1 <a href='erreur'>Afficher</a>").arg(premiere.toHtmlEscaped())));
  else if (const ControleBoitier controle = controleBoitier(m_doc->projet()); !controle.complet)
    // Jamais de « modèle valide » quand une partie du contrôle repose sur des hauteurs non vérifiées
    m_etat->setText(QStringLiteral("<span style='color:#A87410'><b>⚠ Contrôle incomplet</b></span> · 0 erreur · %1 hauteur%2 à vérifier · %3 avertissement%4")
                        .arg(controle.nonVerifies.size())
                        .arg(controle.nonVerifies.size() > 1 ? "s" : "")
                        .arg(alertes)
                        .arg(alertes > 1 ? "s" : ""));
  else m_etat->setText(QStringLiteral("<span style='color:#2F7D53'><b>✓ Modèle valide</b></span> · 0 erreur · %1 avertissement%2").arg(alertes).arg(alertes > 1 ? "s" : ""));
  const QString nom = m_doc->chemin().isEmpty() ? QString::fromStdString(m_doc->projet().nom) : QFileInfo(m_doc->chemin()).completeBaseName();
  setWindowTitle(m_doc->estVierge() ? QStringLiteral("Prométhée") : nom + QStringLiteral("[*] – Prométhée"));
  setWindowModified(m_doc->modifie() && !m_doc->estVierge());
  emit titreChange();
  emit etatTexteChange();
}

void FenetrePrincipale::ouvrirFichier(const QString& chemin) {
  try {
    try {
      m_doc->ouvrir(chemin);
    } catch (const FichierDejaOuvert& e) {
      QMessageBox q(QMessageBox::Warning, QStringLiteral("Fichier déjà ouvert"), QString::fromUtf8(e.what()), QMessageBox::NoButton, this);
      q.setInformativeText(QStringLiteral("L’ouvrir ici aussi ? Les deux fenêtres pourraient enregistrer des versions différentes : "
                                          "Prométhée prévient avant d’écraser des modifications faites ailleurs."));
      QPushButton* quandMeme = q.addButton(QStringLiteral("Ouvrir quand même"), QMessageBox::AcceptRole);
      q.addButton(QStringLiteral("Annuler"), QMessageBox::RejectRole);
      q.exec();
      if (q.clickedButton() != quandMeme) return;
      m_doc->ouvrir(chemin, true);
    }
    m_carte->recadrer();
    m_vue3d->recadrer();
    retenirRecent();
    montrerAtelierSelonProjet();
    if (const auto& av = m_doc->avertissementsLecture(); !av.empty()) {
      QString liste;
      for (const auto& a : av) liste += QStringLiteral("<li>%1</li>").arg(QString::fromStdString(a).toHtmlEscaped());
      QMessageBox::information(this, QStringLiteral("Lecture du projet"),
                               QStringLiteral("<p>Le projet est ouvert. À la lecture :</p><ul>%1</ul>").arg(liste));
    }
  } catch (const std::exception& e) {
    QMessageBox::warning(this, QStringLiteral("Ouverture impossible"), QString::fromUtf8(e.what()));
  }
}

// Atelier choisi d'après le contenu d'un projet ouvert (AP-12) : Pièces pour un projet mécanique sans carte, avec
// sa première pièce active ; électronique pour un projet qui n'a que des conceptions électroniques ; la carte sinon
void FenetrePrincipale::montrerAtelierSelonProjet() {
  if (rouvrirAtelier()) return;  // le dernier atelier utilisé sur ce fichier (G7)
  const auto& p = m_doc->projet();
  const bool meca = !p.pieces.empty() || !p.assemblages.empty();
  if (p.sansCarte && meca) {
    montrerAtelierPiece(true);
    if (m_atelier && !p.pieces.empty()) m_atelier->activerPiece(0);
  } else if (p.sansCarte && !p.electronique.empty()) {
    montrerElectronique(0, 0);
  } else {
    montrerAtelierPiece(false);
  }
}

void FenetrePrincipale::restaurerCopie(const std::string& contenu, const QString& cheminOrigine) {
  m_doc->restaurerCopie(contenu, cheminOrigine);
  montrerAtelierSelonProjet();
  montrerProjet();
  m_carte->recadrer();
  m_vue3d->recadrer();
}

bool FenetrePrincipale::enregistrer(bool sousNouveauNom) {
  QString chemin = m_doc->chemin();
  if (sousNouveauNom || chemin.isEmpty()) {
    // Nom proposé : celui du projet, ou de sa pièce unique
    const auto& pr = m_doc->projet();
    QString nom = QString::fromStdString(pr.nom);
    if (pr.sansCarte && pr.pieces.size() == 1 && pr.assemblages.empty()) nom = QString::fromStdString(pr.pieces.front().value("nom", pr.nom));
    chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Enregistrer"), nom + QStringLiteral(".prom"),
                                          QStringLiteral("Fichier Prométhée (*.prom);;Ancien format (*.json)"));
    if (chemin.isEmpty()) return false;
    if (!chemin.endsWith(QLatin1String(".prom"), Qt::CaseInsensitive) && !chemin.endsWith(QLatin1String(".json"), Qt::CaseInsensitive)) chemin += QStringLiteral(".prom");
  }
  try {
    try {
      m_doc->enregistrer(chemin);
    } catch (const ConflitEnregistrement& e) {
      QMessageBox q(QMessageBox::Warning, QStringLiteral("Fichier modifié ailleurs"), QString::fromUtf8(e.what()), QMessageBox::NoButton, this);
      QPushButton* ecraser = q.addButton(QStringLiteral("Écraser avec ma version"), QMessageBox::DestructiveRole);
      QPushButton* sous = q.addButton(QStringLiteral("Enregistrer sous…"), QMessageBox::ActionRole);
      q.addButton(QStringLiteral("Annuler"), QMessageBox::RejectRole);
      q.setDefaultButton(sous);
      q.exec();
      if (q.clickedButton() == sous) return enregistrer(true);
      if (q.clickedButton() != ecraser) return false;
      m_doc->enregistrer(chemin, true);
    } catch (const FichierDejaOuvert& e) {
      const auto r = QMessageBox::question(this, QStringLiteral("Fichier ouvert ailleurs"),
                                           QString::fromUtf8(e.what()) + QStringLiteral("\n\nL’écraser quand même avec ce projet ?"));
      if (r != QMessageBox::Yes) return false;
      m_doc->enregistrer(chemin, true);
    }
    retenirRecent();
    retenirAtelier();
    statusBar()->showMessage(QStringLiteral("Projet enregistré."), 3000);
    return true;
  } catch (const std::exception& e) {
    QMessageBox::warning(this, QStringLiteral("Enregistrement impossible"), QString::fromUtf8(e.what()));
    return false;
  }
}

void FenetrePrincipale::enregistrerPourVersionPrecedente() {
  DialogueVersionPrecedente d(m_doc->projet(), m_doc->chemin(), this);
  if (d.exec() != QDialog::Accepted) return;
  const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Enregistrer pour Prométhée %1").arg(d.version()), d.cheminPropose(),
                                                      QStringLiteral("Fichier Prométhée (*.prom);;Ancien format (*.json)"));
  if (chemin.isEmpty()) return;
  if (QFileInfo(chemin).absoluteFilePath() == QFileInfo(m_doc->chemin()).absoluteFilePath()) {
    QMessageBox::warning(this, QStringLiteral("Enregistrer pour une version précédente"),
                         QStringLiteral("Ce fichier est celui du projet ouvert : choisis un autre nom, pour garder le projet au format courant."));
    return;
  }
  QString erreur;
  if (!d.ecrire(chemin, &erreur)) {
    QMessageBox::warning(this, QStringLiteral("Enregistrement impossible"), erreur);
    return;
  }
  statusBar()->showMessage(QStringLiteral("Projet écrit pour la %1 : %2").arg(d.version(), d.pertes().isEmpty() ? QStringLiteral("rien ne s’y perd.") : QStringLiteral("%1 perte(s), listées avant l’écriture.").arg(d.pertes().size())),
                           5000);
}

bool FenetrePrincipale::confirmerAbandon() {
  // Une esquisse ou un réglage ouverts sont d'abord écrits dans leur pièce : ils comptent dans les
  // modifications à enregistrer, et ne peuvent plus s'écrire dans le projet suivant (AP-02, EI-03)
  if (m_atelier && !m_atelier->terminerEdition()) return false;
  if (!m_doc->modifie() || m_doc->estVierge()) return true;
  QMessageBox q(this);
  q.setIcon(QMessageBox::Question);
  q.setWindowTitle(QStringLiteral("Modifications non enregistrées"));
  q.setText(QStringLiteral("Enregistrer les modifications de « %1 » ?").arg(QString::fromStdString(m_doc->projet().nom)));
  q.setInformativeText(QStringLiteral("Sans enregistrement, elles seront perdues."));
  QPushButton* oui = q.addButton(QStringLiteral("Enregistrer"), QMessageBox::AcceptRole);
  QPushButton* non = q.addButton(QStringLiteral("Ne pas enregistrer"), QMessageBox::DestructiveRole);
  q.addButton(QStringLiteral("Annuler"), QMessageBox::RejectRole);
  q.setDefaultButton(oui);
  q.exec();
  if (q.clickedButton() == oui) return enregistrer(false);
  return q.clickedButton() == non;
}

void FenetrePrincipale::closeEvent(QCloseEvent* e) {
  retenirAtelier();
  if (confirmerAbandon()) {
    retenirSession();
    e->accept();
  } else
    e->ignore();
}

// Session (2.32, ERG-35) : le projet, l'atelier, la feuille et le cadrage, retenus à la fermeture
void FenetrePrincipale::retenirSession() {
  if (m_doc->chemin().isEmpty()) return;
  QSettings s;
  s.setValue(QStringLiteral("session/projet"), m_doc->chemin());
  const bool elec = atelierElectroniqueActif() && m_electronique;
  s.setValue(QStringLiteral("session/atelier"), elec ? QStringLiteral("electronique") : QStringLiteral("projet"));
  if (!elec) return;
  VueSchema* v = m_electronique->vueSchema();
  s.setValue(QStringLiteral("session/conception"), m_electronique->conception());
  s.setValue(QStringLiteral("session/mode"), m_electronique->mode());
  s.setValue(QStringLiteral("session/feuille"), v->feuille());
  s.setValue(QStringLiteral("session/echelle"), v->echelle());
  s.setValue(QStringLiteral("session/x"), v->origineVue().x());
  s.setValue(QStringLiteral("session/y"), v->origineVue().y());
}

bool FenetrePrincipale::rouvrirSession() {
  const QSettings s;
  const QString projet = s.value(QStringLiteral("session/projet")).toString();
  if (projet.isEmpty() || !QFileInfo::exists(projet)) return false;
  ouvrirFichier(projet);
  montrerProjet();
  if (s.value(QStringLiteral("session/atelier")).toString() != QLatin1String("electronique")) return true;
  const int c = s.value(QStringLiteral("session/conception")).toInt(), mode = s.value(QStringLiteral("session/mode")).toInt(), feuille = s.value(QStringLiteral("session/feuille")).toInt();
  const double echelle = s.value(QStringLiteral("session/echelle"), 0).toDouble();
  const QPointF origine(s.value(QStringLiteral("session/x")).toDouble(), s.value(QStringLiteral("session/y")).toDouble());
  if (c < 0 || c >= static_cast<int>(m_doc->projet().electronique.size())) return true;
  montrerElectronique(c, mode, feuille);
  // La vue, une fois l'atelier posé et dimensionné
  QTimer::singleShot(400, this, [this, c, mode, feuille, echelle, origine] {
    if (!m_electronique) return;
    m_electronique->ouvrir(c, mode, feuille);
    if (echelle > 0) m_electronique->vueSchema()->deplacerVue(origine, echelle);
  });
  return true;
}

void FenetrePrincipale::importerKicad(bool garderPieces) {
  if (!garderPieces && !confirmerAbandon()) return;
  const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Importer une carte KiCad"), QString(), QStringLiteral("Cartes KiCad (*.kicad_pcb)"));
  if (chemin.isEmpty()) return;
  importerFichierKicad(chemin, true, garderPieces);
  montrerProjet();
  montrerAtelierPiece(false);
}

void FenetrePrincipale::importerFichierKicad(const QString& chemin, bool messages, bool garderPieces) {
  try {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    RapportImport r;
    Projet p = projetDepuisKicad(lireFichierKicad(chemin.toStdString()), QFileInfo(chemin).completeBaseName().toStdString(), &r);
    if (p.kicad) p.kicad->fichier = QFileInfo(chemin).absoluteFilePath().toStdString();
    p.sansCarte = false;
    if (garderPieces) {
      // La carte rejoint le projet en cours : ses pièces et son nom restent.
      p.pieces = m_doc->projet().pieces;
      p.nom = m_doc->projet().nom;
    }
    m_doc->remplacer(p);
    m_doc->selectionner({});
    m_carte->recadrer();
    m_vue3d->recadrer();
    QApplication::restoreOverrideCursor();
    if (!messages) return;
    QString texte = QStringLiteral("Carte importée : contour réel, %1 trous de fixation, %2 composants dont %3 connecteurs de bord.").arg(r.trous).arg(r.composants).arg(r.connecteursBord);
    if (r.dessous) texte += QStringLiteral("\n%1 composants sont sur la face arrière (bouton Dessous de la vue Carte, touche B).").arg(r.dessous);
    texte += QStringLiteral("\n\nLes hauteurs des composants sont estimées d’après le nom de leur empreinte : le contrôle du boîtier reste « incomplet » "
                            "tant que chacune n’est pas vérifiée (fiche technique) et cochée dans le panneau du composant.");
    QMessageBox::information(this, QStringLiteral("Import KiCad"), texte);
  } catch (const std::exception& e) {
    QApplication::restoreOverrideCursor();
    QMessageBox::warning(this, QStringLiteral("Import impossible"), QString::fromUtf8(e.what()));
  }
}

bool FenetrePrincipale::importerFichierCAO(const QString& chemin, bool messages) {
  ImportCAO imp;
  try {
    imp = lireCAOStructure(chemin.toStdString());
  } catch (const std::exception& e) {
    if (messages) QMessageBox::warning(this, QStringLiteral("Import impossible"), QStringLiteral("« %1 » : %2").arg(QFileInfo(chemin).fileName(), QString::fromUtf8(e.what())));
    return false;
  }
  const std::string source = QFileInfo(chemin).fileName().toStdString();
  if (imp.estAssemblage()) {
    int racine = -1;
    m_doc->modifier([&](Projet& p) { racine = ajouterImportAuProjet(p, imp, source); });
    montrerAssemblage(racine);
    m_messageAtelier = QStringLiteral("« %1 » importé : %2 pièce(s), %3 instance(s).").arg(QString::fromStdString(imp.racine.nom).toHtmlEscaped()).arg(imp.produits.size()).arg(imp.instances());
    emit etatTexteChange();
    return true;
  }
  const Piece pc = pieceDepuisProduit(imp.aplati(imp.produits.front().nom), source);
  int indice = 0;
  m_doc->modifier([&](Projet& p) { p.pieces.push_back(pieceVersJson(pc)); indice = static_cast<int>(p.pieces.size()) - 1; });
  montrerAtelierPiece(true);
  m_atelier->activerPiece(indice);
  majNavigateur();
  return true;
}

void FenetrePrincipale::exporterKicad() {
  const Projet& p = m_doc->projet();
  std::string original;
  std::filesystem::path cheminOriginal;
  if (p.kicad && !p.kicad->fichier.empty()) {
    cheminOriginal = prom::cheminFichier(p.kicad->fichier);
    try {
      original = prom::lireFichier(cheminOriginal);
    } catch (const prom::ErreurFichier&) {
      original.clear();  // carte d'origine déplacée ou supprimée : on proposera une nouvelle carte
    }
  }
  try {
    if (!original.empty()) {
      // Carte venue de KiCad : on propose de modifier le fichier d'origine lui-même (avec une sauvegarde à côté).
      QMessageBox choix(QMessageBox::Question, QStringLiteral("Export vers KiCad"),
                        QStringLiteral("Cette carte vient de KiCad :\n%1\n\nMettre à jour ce fichier avec le contour et les trous de fixation de Prométhée ? "
                                       "L’ancienne version est gardée à côté sous le nom « …-avant-promethee.kicad_pcb ». Si la carte est ouverte dans KiCad, recharge-la ensuite.")
                            .arg(QString::fromStdU16String(cheminOriginal.u16string())),
                        QMessageBox::NoButton, this);
      QPushButton* maj = choix.addButton(QStringLiteral("Mettre à jour la carte d’origine"), QMessageBox::AcceptRole);
      QPushButton* copie = choix.addButton(QStringLiteral("Enregistrer une copie…"), QMessageBox::ActionRole);
      QPushButton* neuve = choix.addButton(QStringLiteral("Nouvelle carte…"), QMessageBox::ActionRole);
      choix.addButton(QStringLiteral("Annuler"), QMessageBox::RejectRole);
      choix.setDefaultButton(maj);
      choix.exec();
      const QAbstractButton* clic = choix.clickedButton();
      if (clic == maj) {
        const std::string texte = kicadMisAJour(original, p, m_doc->derive());
        std::filesystem::path sauvegarde = cheminOriginal;
        sauvegarde.replace_filename(cheminOriginal.stem().u16string() + u"-avant-promethee.kicad_pcb");
        std::filesystem::copy_file(cheminOriginal, sauvegarde, std::filesystem::copy_options::overwrite_existing);
        prom::ecrireFichierAtomique(cheminOriginal, texte);
        statusBar()->showMessage(QStringLiteral("Carte KiCad mise à jour : %1").arg(QString::fromStdU16String(cheminOriginal.u16string())), 6000);
        return;
      }
      if (clic != copie && clic != neuve) return;
      const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter vers KiCad"), QString::fromStdString(p.nom) + QStringLiteral("-promethee.kicad_pcb"),
                                                          QStringLiteral("Cartes KiCad (*.kicad_pcb)"));
      if (chemin.isEmpty()) return;
      const std::string texte = clic == copie ? kicadMisAJour(original, p, m_doc->derive()) : kicadDepuisProjet(p, m_doc->derive());
      prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), texte);
      statusBar()->showMessage(QStringLiteral("Carte KiCad écrite : %1").arg(chemin), 6000);
      return;
    }
    const QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter vers KiCad"), QString::fromStdString(p.nom) + QStringLiteral(".kicad_pcb"),
                                                        QStringLiteral("Cartes KiCad (*.kicad_pcb)"));
    if (chemin.isEmpty()) return;
    prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), kicadDepuisProjet(p, m_doc->derive()));
    statusBar()->showMessage(QStringLiteral("Carte KiCad écrite : %1").arg(chemin), 6000);
  } catch (const std::exception& e) {
    QMessageBox::warning(this, QStringLiteral("Export impossible"), QString::fromUtf8(e.what()));
  }
}

void FenetrePrincipale::exporterDossier() {
  if (m_doc->nombreErreurs() > 0 &&
      QMessageBox::question(this, QStringLiteral("Le projet contient des erreurs"), QStringLiteral("Les fichiers seront produits tels quels. Continuer ?")) != QMessageBox::Yes)
    return;
  DialogueExport dialogue(m_doc, this);
  if (dialogue.exec() != QDialog::Accepted) return;
  try {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QStringList ecrits = dialogue.exporter();
    QApplication::restoreOverrideCursor();
    QMessageBox::information(this, QStringLiteral("Dossier de fabrication"),
                             ecrits.isEmpty() ? QStringLiteral("Aucun format choisi.") : QStringLiteral("Fichiers écrits :\n") + ecrits.join(QStringLiteral("\n")));
  } catch (const std::exception& e) {
    QApplication::restoreOverrideCursor();
    QMessageBox::warning(this, QStringLiteral("Export impossible"), QString::fromUtf8(e.what()));
  }
}

// Palette de commandes : on tape un mot (« extru », « congé », « masse »…), Entrée lance la commande.
void FenetrePrincipale::paletteCommandes() {
  QDialog d(this, Qt::Popup | Qt::FramelessWindowHint);
  d.setObjectName(QStringLiteral("palette"));
  d.setStyleSheet(QStringLiteral("#palette { background: white; border: 1px solid #CDD3E4; border-radius: 10px; }"
                                 "QLineEdit { border: none; border-bottom: 1px solid #DEE2EE; padding: 10px; font-size: 15px; }"
                                 "QListWidget { border: none; font-size: 13px; } QListWidget::item { padding: 6px 10px; } QListWidget::item:selected { background: #DDE5FF; color: #10142A; }"));
  auto* l = new QVBoxLayout(&d);
  l->setContentsMargins(4, 4, 4, 4);
  auto* champ = new QLineEdit;
  champ->setPlaceholderText(QStringLiteral("Rechercher une commande…"));
  auto* liste = new QListWidget;
  l->addWidget(champ);
  l->addWidget(liste);
  d.resize(520, 380);
  d.move(geometry().center() - QPoint(260, 220));
  // Toutes les actions nommées, visibles et actives, sans doublon de nom. « Visible » au sens de l'interface :
  // un bouton, un menu ou une barre qui la porte est à l'écran et actif, sinon la commande n'a pas de sens dans
  // l'état présent (barres de l'esquisse cachées hors esquisse, ruban de pièce caché en esquisse : AP-01)
  std::function<bool(QAction*, int)> accessible = [&](QAction* a, int profondeur) -> bool {
    if (!a || !a->isEnabled() || !a->isVisible() || profondeur > 8) return false;
    for (QObject* o : a->associatedObjects()) {
      auto* w = qobject_cast<QWidget*>(o);
      if (!w) continue;
      if (auto* m = qobject_cast<QMenu*>(w)) {
        // Menu de la barre ou sous-menu : accessible si l'action qui l'ouvre l'est ; menu d'un bouton : si le bouton l'est
        if (accessible(m->menuAction(), profondeur + 1)) return true;
        QWidget* porteur = m->parentWidget();
        if (porteur && !qobject_cast<QMenu*>(porteur) && porteur->isVisible() && porteur->isEnabled() && porteur->window() == window()) return true;
        continue;
      }
      if (w->isVisible() && w->isEnabled() && w->window() == window()) return true;
    }
    return false;
  };
  std::vector<QAction*> actions;
  QStringList vus;
  for (QAction* a : findChildren<QAction*>()) {
    QString t = a->text().section(QLatin1Char('\t'), 0, 0);  // « Copier\tCtrl+C » : la touche, montrée par le menu, n'est pas au nom
    t.remove(QLatin1Char('&'));
    if (t.isEmpty() || !accessible(a, 0) || vus.contains(t) || t.startsWith(QStringLiteral("✔")) || t.startsWith(QStringLiteral("✖"))) continue;
    vus << t;
    actions.push_back(a);
  }
  // Atelier électronique (2.14, SAI-21) : toutes les actions de son registre possibles maintenant, celles du menu
  // contextuel et de l'inspecteur comprises, sous leur nom, avec leur raccourci ; le libellé long sert à la recherche
  if (atelierElectroniqueActif()) {
    const RegistreActions& r = RegistreActions::atelierElectronique();
    for (const ActionAtelier& x : r.actions()) {
      if (x.cachee || !m_electronique->actionPossible(x.identifiant)) continue;
      const QString nom = x.nom.isEmpty() ? x.libelle : x.nom;
      if (vus.contains(nom)) continue;
      vus << nom;
      auto* a = new QAction(nom, &d);
      a->setProperty("motsCles", x.libelle + QLatin1Char(' ') + x.bulle);
      const QString touche = r.touches(x.identifiant).section(QStringLiteral(", "), 0, 0);
      if (!touche.isEmpty()) a->setText(nom + QLatin1Char('\t') + touche);
      if (!x.icone.isEmpty()) a->setIcon(icone(x.icone));
      connect(a, &QAction::triggered, this, [this, id = x.identifiant] {
        if (atelierElectroniqueActif()) m_electronique->executerAction(id);
      });
      actions.push_back(a);
    }
  }
  // Les réglages déclarés (2.32, ERG-38) : « Réglage : … » ouvre la fenêtre des réglages sur lui, surligné
  for (const Reglage& x : RegistreReglages::instance().tous()) {
    auto* a = new QAction(QStringLiteral("Réglage : %1").arg(x.libelle), &d);
    a->setProperty("motsCles", x.motsVoisins + QLatin1Char(' ') + x.page + QStringLiteral(" reglage"));
    a->setIcon(icone(QStringLiteral("proprietes")));
    connect(a, &QAction::triggered, this, [this, cle = x.cle] { ouvrirReglage(cle); });
    actions.push_back(a);
  }
  // Sans accents, sans casse, apostrophes confondues ; les mots dans le désordre (SAI-21)
  auto simple = [](QString t) {
    t.replace(QChar(0x2019), QLatin1Char('\''));
    return t.normalized(QString::NormalizationForm_D).remove(QRegularExpression(QStringLiteral("[\\x{0300}-\\x{036f}]"))).toLower();
  };
  auto remplir = [&](const QString& filtre) {
    liste->clear();
    QStringList mots = simple(filtre).split(QRegularExpression(QStringLiteral("[\\s']+")), Qt::SkipEmptyParts);
    mots.removeIf([](const QString& m) { return m.size() < 2; });  // « d' », « l' » ne départagent rien
    for (QAction* a : actions) {
      QString t = a->text().section(QLatin1Char('\t'), 0, 0);
      const QString touche = a->shortcut().isEmpty() ? a->text().section(QLatin1Char('\t'), 1) : a->shortcut().toString(QKeySequence::NativeText);
      t.remove(QLatin1Char('&'));
      const QString n = simple(t + QLatin1Char(' ') + a->property("motsCles").toString());
      if (!std::all_of(mots.begin(), mots.end(), [&](const QString& m) { return n.contains(m); })) continue;
      auto* it = new QListWidgetItem(a->icon(), t + (touche.isEmpty() ? QString() : QStringLiteral("   (%1)").arg(touche)));
      it->setData(Qt::UserRole, QVariant::fromValue(static_cast<void*>(a)));
      liste->addItem(it);
    }
    liste->setCurrentRow(0);
  };
  remplir(QString());
  connect(champ, &QLineEdit::textChanged, &d, [&](const QString& t) { remplir(t); });
  auto lancer = [&] {
    if (QListWidgetItem* it = liste->currentItem()) {
      QAction* a = static_cast<QAction*>(it->data(Qt::UserRole).value<void*>());
      d.accept();
      if (a) a->trigger();
    }
  };
  connect(champ, &QLineEdit::returnPressed, &d, lancer);
  connect(liste, &QListWidget::itemActivated, &d, lancer);
  champ->installEventFilter(&d);
  champ->setFocus();
  d.exec();
}

void FenetrePrincipale::montrerAssemblage(int a, int composant) {
  if (!m_assemblage) {
    m_assemblage = new AtelierAssemblage(m_doc);
    m_assemblage->setEtatIntegre(m_dansCoquille);
    connect(m_assemblage, &AtelierAssemblage::messageChange, this, [this](const QString& t) { m_messageAtelier = t; emit etatTexteChange(); });
    while (m_ateliers->count() < 2) m_ateliers->addWidget(new QWidget);
    m_ateliers->insertWidget(2, m_assemblage);
    connect(m_assemblage, &AtelierAssemblage::ouvrirPiece, this, [this](int piece, int a) {
      // Modifier une pièce depuis l'assemblage : l'atelier Pièce s'ouvre, un bouton ramène à l'assemblage
      montrerAtelierPiece(true);
      m_atelier->activerPiece(piece);
      const QString nomAss = QString::fromStdString(m_doc->projet().assemblages[static_cast<size_t>(a)].nom);
      m_atelier->afficherRetour(QStringLiteral("Revenir à « %1 »").arg(nomAss), [this, a] { m_atelier->afficherRetour(QString(), {}); montrerAssemblage(a); });
      majNavigateur();
    });
    connect(m_assemblage, &AtelierAssemblage::nouvellePieceDemandee, this, [this](int a) {
      montrerAtelierPiece(true);
      m_atelier->nouvellePiece();
      const int indice = static_cast<int>(m_doc->projet().pieces.size()) - 1;
      m_doc->modifier([a, indice](Projet& p) {
        if (a < 0 || a >= static_cast<int>(p.assemblages.size())) return;
        double x = 0;
        x = 80.0 * static_cast<double>(p.assemblages[a].composants.size());
        p.assemblages[a].composants.push_back({indice, x, 0, 0, 0, 0, 0});
      });
      const QString nomAss = QString::fromStdString(m_doc->projet().assemblages[a].nom);
      m_atelier->afficherRetour(QStringLiteral("Revenir à « %1 »").arg(nomAss), [this, a] { m_atelier->afficherRetour(QString(), {}); montrerAssemblage(a); });
      majNavigateur();
    });
  }
  m_pages->setCurrentIndex(1);
  m_ateliers->setCurrentWidget(m_assemblage);
  if (m_dock) m_dock->hide();
  m_assemblage->ouvrir(a, composant);
  majNavigateur();
}

void FenetrePrincipale::montrerElectronique(int conception, int mode, int feuille) {
  const auto& e = m_doc->projet().electronique;
  if (conception < 0 || conception >= static_cast<int>(e.size())) return;
  if (!m_electronique) {
    journal("création de l'atelier électronique");
    m_electronique = new AtelierElectronique(m_doc);
    m_electronique->setEtatIntegre(m_dansCoquille);
    // L'arbre suit la sélection de l'atelier (3.11, PLA-03)
    connect(m_electronique->selection(), &SelectionElectronique::changee, this, [this] {
      if (m_navigateur && m_electronique) m_navigateur->suivreSelection(m_electronique->conception(), m_electronique->selection()->composants());
    });
    connect(m_electronique, &AtelierElectronique::boitierDemande, this, [this] { montrerAtelierPiece(false); });
    m_ateliers->addWidget(m_electronique);
  }
  m_pages->setCurrentIndex(1);
  m_ateliers->setCurrentWidget(m_electronique);
  if (m_dock) m_dock->hide();
  m_electronique->ouvrir(conception, mode, feuille);
  majNavigateur();
  retenirAtelier();
}

void FenetrePrincipale::placerArbre() {
  if (!m_electronique || !m_navigateur) return;
  if (m_ateliers->currentWidget() == m_electronique) {
    if (m_navigateur->parentWidget() == m_zoneProjet) m_taillesZoneProjet = m_zoneProjet->sizes();
    m_electronique->accueillirArbre(m_navigateur);
  } else if (m_navigateur->parentWidget() != m_zoneProjet) {
    QWidget* arbre = m_electronique->rendreArbre();
    if (!arbre) return;
    m_zoneProjet->insertWidget(0, arbre);
    arbre->show();
    if (m_taillesZoneProjet.size() == 2) m_zoneProjet->setSizes(m_taillesZoneProjet);
  }
}

void FenetrePrincipale::montrerElectrique(int projet, int mode, int folio) {
  const auto& e = m_doc->projet().electrique;
  if (projet < 0 || projet >= static_cast<int>(e.size())) return;
  if (!m_electrique) {
    journal("création de l'atelier électrique");
    m_electrique = new AtelierElectrique(m_doc);
    m_electrique->setEtatIntegre(m_dansCoquille);
    connect(m_electrique, &AtelierElectrique::messageChange, this, [this](const QString& t) { m_messageAtelier = t; emit etatTexteChange(); });
    connect(m_electrique, &AtelierElectrique::parcoursDemande, this, [this](const QString& id) { lancerParcours(id); });
    m_ateliers->addWidget(m_electrique);
  }
  m_pages->setCurrentIndex(1);
  m_ateliers->setCurrentWidget(m_electrique);
  if (m_dock) m_dock->hide();
  m_electrique->ouvrir(projet, mode, folio);
  majNavigateur();
  retenirAtelier();
}

// Nouveau projet électrique : un schéma (garde, puissance, commande), une installation à calculer, des bornes de
// recharge, un tableau de logement, une armoire ou un faisceau prêts à remplir, ou l'exemple du démarrage moteur ;
// dans un projet neuf ou ajouté au projet ouvert
bool FenetrePrincipale::nouveauProjetElectrique(const QString& genre, bool nouveauProjet) {
  if (genre == QLatin1String("apprendre")) {
    // Parcours d'apprentissage : le premier schéma ; la carte du parcours propose les autres
    lancerParcours(QStringLiteral("moteur"));
    return true;
  }
  prom::elec::ProjetElectrique pe;
  int mode = AtelierElectrique::Schema;
  if (genre == QLatin1String("exemple")) pe = prom::elec::exempleDemarrageMoteur();
  else if (genre == QLatin1String("distribution") || genre == QLatin1String("irve")) {
    // Installation calculée : vide (le modèle se choisit au centre) ou parking de bornes de recharge
    const bool irve = genre == QLatin1String("irve");
    pe = prom::elec::nouveauProjetElectrique(irve ? "Bornes de recharge" : "Installation électrique", false);
    if (irve) pe.distribution = prom::elec::reseauModele(prom::elec::ModeleReseau::ParkingIRVE);
    mode = AtelierElectrique::Distribution;
  } else if (genre == QLatin1String("photovoltaique")) {
    // Installation photovoltaïque : la taille se choisit au centre
    pe = prom::elec::nouveauProjetElectrique("Installation photovoltaïque", false);
    mode = AtelierElectrique::Photovoltaique;
  } else if (genre == QLatin1String("grafcet")) {
    // Automatisme : le grafcet d'abord (étape initiale posée), le schéma de l'armoire ensuite
    pe = prom::elec::nouveauProjetElectrique("Automatisme", true);
    prom::elec::Grafcet g;
    g.uuid = prom::ecad::nouvelUuid();
    g.nom = "G1";
    prom::elec::EtapeGrafcet e;
    e.numero = 0;
    e.initiale = true;
    g.etapes.push_back(e);
    pe.grafcets.push_back(g);
    mode = AtelierElectrique::Grafcet;
  } else if (genre == QLatin1String("logement")) {
    pe = prom::elec::nouveauProjetElectrique("Tableau de logement", false);
    pe.reglages.tension = 230;
    pe.reglages.tensionCommande = 230;
    pe.reglages.commandeContinue = false;
    mode = AtelierElectrique::Logement;
  } else {
    const bool armoire = genre == QLatin1String("armoire"), faisceau = genre == QLatin1String("faisceau");
    pe = prom::elec::nouveauProjetElectrique(armoire ? "Armoire électrique" : faisceau ? "Faisceau" : "Schéma électrique", true);
    if (armoire) {
      prom::elec::Armoire a;
      a.uuid = prom::ecad::nouvelUuid();
      a.nom = "Armoire";
      prom::elec::equiperArmoire(a);
      pe.armoires.push_back(a);
      mode = AtelierElectrique::Armoire;  // on voit l'armoire choisie ; ses appareils viendront du schéma
    }
    if (faisceau) {
      prom::elec::Faisceau f;
      f.uuid = prom::ecad::nouvelUuid();
      f.nom = "Faisceau";
      pe.faisceaux.push_back(f);
      mode = AtelierElectrique::Faisceau;
    }
  }
  if (nouveauProjet) {
    if (!confirmerAbandon()) return false;
    Projet p = prom::projetVide();
    p.sansCarte = true;
    p.nom = pe.nom;
    p.electrique.push_back(pe);
    m_doc->creer(p);
  } else {
    m_doc->modifier([&](Projet& p) {
      if (!p.electrique.empty() && genre != QLatin1String("exemple")) pe.nom += " " + std::to_string(p.electrique.size() + 1);
      p.electrique.push_back(pe);
    });
  }
  montrerProjet();
  montrerElectrique(static_cast<int>(m_doc->projet().electrique.size()) - 1, mode);
  return true;
}

void FenetrePrincipale::lancerParcours(const QString& id) {
  const prom::elec::Parcours* m = prom::elec::parcoursElectrique(id.toStdString());
  if (!m || !nouveauProjetElectrique(QString::fromStdString(m->genre), true)) return;
  if (m_electrique) m_electrique->demarrerParcours(m->id);
}

bool FenetrePrincipale::importerQetElectrique(const QString& chemin, bool nouveauProjet) {
  std::vector<std::string> alertes;
  prom::elec::ProjetElectrique pe;
  try {
    pe = prom::elec::importerQet(prom::lireFichier(prom::cheminFichier(chemin.toStdString())), &alertes);
  } catch (const std::exception& e) {
    QMessageBox::warning(this, QStringLiteral("Projet QElectroTech"), QStringLiteral("Import impossible : %1").arg(QString::fromUtf8(e.what())));
    return false;
  }
  if (pe.nom == "Projet importé") pe.nom = QFileInfo(chemin).completeBaseName().toStdString();
  if (nouveauProjet) {
    if (!confirmerAbandon()) return false;
    Projet p = prom::projetVide();
    p.sansCarte = true;
    p.nom = pe.nom;
    p.electrique.push_back(pe);
    m_doc->creer(p);
  } else
    m_doc->modifier([&](Projet& p) { p.electrique.push_back(pe); });
  montrerProjet();
  montrerElectrique(static_cast<int>(m_doc->projet().electrique.size()) - 1, AtelierElectrique::Schema);
  QStringList details;
  for (const auto& a : alertes) details << QString::fromStdString(a);
  auto* b = new QMessageBox(QMessageBox::Information, QStringLiteral("Projet QElectroTech importé"), details.isEmpty() ? QStringLiteral("Import terminé.") : details.front(), QMessageBox::Ok, this);
  if (details.size() > 1) b->setInformativeText(details.mid(1).join(QLatin1Char('\n')));
  b->setAttribute(Qt::WA_DeleteOnClose);
  b->setModal(false);
  b->show();
  return true;
}

// Après une annulation ou une suppression, l'atelier électrique ne montre jamais un projet disparu
void FenetrePrincipale::suivreProjetsElectriques() {
  if (!m_electrique || m_ateliers->currentWidget() != m_electrique) return;
  const int n = static_cast<int>(m_doc->projet().electrique.size());
  if (m_electrique->projet() < n) return;
  if (n > 0) m_electrique->ouvrir(n - 1, m_electrique->mode());
  else montrerAtelierPiece(!m_doc->projet().pieces.empty() || m_doc->projet().sansCarte);
  majNavigateur();
}

// Fichier › Nouveau projet : l'accueil, où tout ce qui se crée est rangé par métier ; dans la fenêtre à onglets,
// un onglet neuf quand celui-ci porte déjà un projet
void FenetrePrincipale::nouveauProjetDepuisMenu() {
  if (m_dansCoquille && !m_doc->estVierge()) {
    emit demandeNouvelOnglet(QStringLiteral("accueil"));
    return;
  }
  montrerAccueil();
}

// Nouvelle carte électronique : dans un projet neuf (accueil) ou dans le projet ouvert (arbre).
void FenetrePrincipale::nouvelleConceptionElec(bool nouveauProjet, bool schemaSeul) {
  if (nouveauProjet) {
    if (!confirmerAbandon()) return;
    Projet p = prom::projetVide();
    p.sansCarte = true;
    p.nom = schemaSeul ? "Schéma sans nom" : "Carte sans nom";
    p.electronique.push_back(prom::ecad::nouvelleConception(schemaSeul ? "Schéma" : "Carte 1"));
    p.electronique.back().schemaSeul = schemaSeul;  // STR-13 : le circuit se crée au premier besoin
    PanneauApprendre::appliquerPreferences(p.electronique.back());  // unité et fabricant du premier lancement
    m_doc->creer(p);
  } else {
    m_doc->modifier([schemaSeul](Projet& p) {
      p.electronique.push_back(prom::ecad::nouvelleConception((schemaSeul ? "Schéma " : "Carte ") + std::to_string(p.electronique.size() + 1)));
      p.electronique.back().schemaSeul = schemaSeul;
      PanneauApprendre::appliquerPreferences(p.electronique.back());
    }, schemaSeul ? QStringLiteral("Nouveau schéma") : QStringLiteral("Nouvelle carte"));
  }
  montrerProjet();
  montrerElectronique(static_cast<int>(m_doc->projet().electronique.size()) - 1, AtelierElectronique::Schema);
  if (m_electronique) m_electronique->montrerBibliotheque();
}

bool FenetrePrincipale::importerKicadElectronique(const QString& chemin, bool nouveauProjet) {
  prom::ecad::RapportImportKicad r;
  prom::ecad::ConceptionElec c;
  try {
    const std::string texte = prom::lireFichier(prom::cheminFichier(chemin.toStdString()));
    // Règles et classes : le projet KiCad voisin (même nom, extension .kicad_pro), s'il existe
    std::string projet;
    const QString pro = QFileInfo(chemin).dir().filePath(QFileInfo(chemin).completeBaseName() + QStringLiteral(".kicad_pro"));
    if (QFileInfo::exists(pro)) projet = prom::lireFichier(prom::cheminFichier(pro.toStdString()));
    c = prom::ecad::conceptionDepuisKicad(texte, QFileInfo(chemin).completeBaseName().toStdString(), &r, projet);
  } catch (const std::exception& e) {
    QMessageBox::warning(this, QStringLiteral("Carte KiCad"), QStringLiteral("Import impossible : %1").arg(QString::fromUtf8(e.what())));
    return false;
  }
  if (nouveauProjet) {
    if (!confirmerAbandon()) return false;
    Projet p = prom::projetVide();
    p.sansCarte = true;
    p.nom = c.nom;
    p.electronique.push_back(c);
    m_doc->creer(p);
  } else
    m_doc->modifier([&c](Projet& p) { p.electronique.push_back(c); });
  montrerProjet();
  montrerElectronique(static_cast<int>(m_doc->projet().electronique.size()) - 1, AtelierElectronique::Circuit);
  QStringList details;
  for (const auto& a : r.alertes) details << QString::fromStdString(a);
  QMessageBox* b = new QMessageBox(QMessageBox::Information, QStringLiteral("Carte KiCad importée"),
                                   QStringLiteral("%1 empreintes, %2 pistes, %3 vias, %4 zones et %5 réseaux importés de « %6 ».\n\n"
                                                  "Le fichier de carte ne contient pas le schéma d’origine : un schéma de liaison a été créé (un symbole par composant, "
                                                  "broches reliées par des étiquettes au nom des réseaux KiCad).")
                                       .arg(r.empreintes).arg(r.pistes).arg(r.vias).arg(r.zones).arg(r.reseaux).arg(QFileInfo(chemin).fileName()),
                                   QMessageBox::Ok, this);
  b->setDetailedText(details.join(QLatin1Char('\n')));
  b->setAttribute(Qt::WA_DeleteOnClose);
  b->setModal(false);
  b->show();
  return true;
}

// Après une annulation ou une suppression, l'atelier électronique ne montre jamais une conception disparue.
void FenetrePrincipale::suivreConceptionsElec() {
  if (!m_electronique || m_ateliers->currentWidget() != m_electronique) return;
  const int n = static_cast<int>(m_doc->projet().electronique.size());
  if (m_electronique->conception() < n) return;
  if (n > 0) m_electronique->ouvrir(n - 1, m_electronique->mode());
  else montrerAtelierPiece(!m_doc->projet().pieces.empty() || m_doc->projet().sansCarte);
  majNavigateur();
}

void FenetrePrincipale::montrerSchema(int k) {
  const auto& s = m_doc->projet().schemasElectriques;
  if (k < 0 || k >= static_cast<int>(s.size())) return;
  if (!m_pageSchema) {
    m_pageSchema = new PageSchema;
    m_ateliers->addWidget(m_pageSchema);
  }
  m_pages->setCurrentIndex(1);
  m_ateliers->setCurrentWidget(m_pageSchema);
  if (m_dock) m_dock->hide();
  m_pageSchema->montrer(QString::fromStdString(s[k].fichier));
  majNavigateur();
}

// Schéma électrique relié : fichier QElectroTech, lu pour l'arbre (folios, appareils) et ouvert dans QElectroTech.
void FenetrePrincipale::ajouterSchemaQet(bool nouveauProjet) {
  if (nouveauProjet && !confirmerAbandon()) return;
  const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir un schéma QElectroTech"), QString(), QStringLiteral("Schémas QElectroTech (*.qet)"));
  if (chemin.isEmpty()) return;
  const InfosQet q = lireQet(chemin);
  if (!q.lu) { QMessageBox::warning(this, QStringLiteral("Schéma électrique"), q.erreur); return; }
  if (nouveauProjet) {
    m_doc->nouveau();
    m_doc->modifier([&](Projet& p) { p.sansCarte = true; p.nom = q.titre.toStdString(); });
  }
  m_doc->modifier([&](Projet& p) { p.schemasElectriques.push_back({chemin.toStdString(), q.titre.toStdString()}); });
  montrerProjet();
  montrerSchema(static_cast<int>(m_doc->projet().schemasElectriques.size()) - 1);
}

void FenetrePrincipale::montrerAccueil() {
  m_accueilAjoutElec = false;
  majRecents();
  m_pages->setCurrentIndex(0);
  if (m_dock) m_dock->hide();  // le panneau de la carte n'a rien à faire sur l'accueil
}

void FenetrePrincipale::montrerProjet() {
  m_pages->setCurrentIndex(1);
  if (m_dock) m_dock->setVisible(m_ateliers->currentIndex() == 0);
  majNavigateur();
}

void FenetrePrincipale::retenirRecent() {
  if (m_doc->chemin().isEmpty()) return;
  QSettings reglages;
  QStringList r = reglages.value(QStringLiteral("recents")).toStringList();
  const QString chemin = m_doc->chemin();
  r.erase(std::remove_if(r.begin(), r.end(), [&](const QString& e) { return e.section(QLatin1Char('|'), 0, 0) == chemin; }), r.end());
  QStringList metiers;
  if (!m_doc->projet().sansCarte) metiers << QStringLiteral("elec") << QStringLiteral("meca");
  if (!m_doc->projet().pieces.empty() && !metiers.contains(QStringLiteral("meca"))) metiers << QStringLiteral("meca");
  if (!m_doc->projet().electronique.empty() && !metiers.contains(QStringLiteral("elec"))) metiers << QStringLiteral("elec");
  if (!m_doc->projet().electrique.empty() || !m_doc->projet().schemasElectriques.empty()) metiers << QStringLiteral("electrique");
  r.prepend(chemin + QLatin1Char('|') + metiers.join(QLatin1Char(',')));
  const int longueur = std::clamp(RegistreReglages::instance().valeur(QStringLiteral("recents/longueur")).toInt(), 3, 40);  // réglable (2.32)
  while (r.size() > longueur) r.removeLast();
  reglages.setValue(QStringLiteral("recents"), r);
}

void FenetrePrincipale::majRecents() {
  QSettings reglages;
  QStringList r = reglages.value(QStringLiteral("recents")).toStringList();
  r.erase(std::remove_if(r.begin(), r.end(), [](const QString& e) { return !QFileInfo::exists(e.section(QLatin1Char('|'), 0, 0)); }), r.end());
  m_accueil->setRecents(r);
}

void FenetrePrincipale::choixAccueil(const QString& code) {
  if (code == QLatin1String("ouvrir")) {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir un projet"), QString(), QStringLiteral("Fichiers Prométhée (*.prom *.json)"));
    if (chemin.isEmpty()) return;
    if (m_dansCoquille) { emit demandeOuvrirFichier(chemin); return; }
    if (!confirmerAbandon()) return;
    ouvrirFichier(chemin);
    montrerProjet();
    return;
  }
  // Un projet est déjà ouvert dans cet onglet : la nouvelle création part dans un autre onglet ; sauf la carte choisie
  // depuis l'accueil que Ctrl+3 ouvre dans un projet sans carte, qui s'ajoute à ce projet (G7, la fenêtre à onglets
  // comprise : 1.1)
  const bool ajoutElec = m_accueilAjoutElec && (code == QLatin1String("carte-nouvelle") || code == QLatin1String("schema-elec"));
  if (m_dansCoquille && !m_doc->estVierge() && !ajoutElec && code != QLatin1String("plan") && code != QLatin1String("assemblage-ici")) {
    emit demandeNouvelOnglet(code);
    return;
  }
  if (code == QLatin1String("cao-ouvrir")) {
    if (!confirmerAbandon()) return;
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir un fichier CAO"), QString(), QStringLiteral("CAO (*.step *.stp *.igs *.iges *.brep *.brp *.stl)"));
    if (chemin.isEmpty()) return;
    montrerProjet();
    importerFichierCAO(chemin);
    return;
  }
  if (code == QLatin1String("kicad-ouvrir")) {
    if (!confirmerAbandon()) return;
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir une carte KiCad"), QString(), QStringLiteral("Cartes KiCad (*.kicad_pcb)"));
    if (!chemin.isEmpty()) importerKicadElectronique(chemin, true);
    return;
  }
  if (code == QLatin1String("mixte-boitier")) {
    if (!confirmerAbandon()) return;
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir une carte KiCad"), QString(), QStringLiteral("Cartes KiCad (*.kicad_pcb)"));
    if (chemin.isEmpty()) return;
    importerFichierKicad(chemin, true);
    montrerProjet();
    montrerAtelierPiece(false);
    return;
  }
  if (code == QLatin1String("cablage")) { ajouterSchemaQet(true); return; }
  if (code.startsWith(QLatin1String("electrique-"))) {
    const QString genre = code.mid(11);
    if (genre == QLatin1String("qet")) {
      // Le fichier d'abord : l'abandon du projet ouvert n'est demandé qu'une fois, par l'import lui-même
      const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Ouvrir un projet QElectroTech"), QString(), QStringLiteral("Projets QElectroTech (*.qet)"));
      if (!chemin.isEmpty()) importerQetElectrique(chemin, true);
      return;
    }
    nouveauProjetElectrique(genre, true);
    return;
  }
  if (code == QLatin1String("carte-nouvelle") || code == QLatin1String("schema-elec")) {
    nouvelleConceptionElec(!m_accueilAjoutElec || m_doc->estVierge(), code == QLatin1String("schema-elec"));
    return;
  }
  if (code == QLatin1String("exemple-elec")) {
    if (!confirmerAbandon()) return;
    Projet p = prom::projetVide();
    p.sansCarte = true;
    p.nom = "Clignotant 555";
    p.electronique.push_back(prom::ecad::exempleElectronique());
    m_doc->creer(p);
    montrerProjet();
    montrerElectronique(0, AtelierElectronique::Schema);
    return;
  }
  if (code == QLatin1String("assemblage")) {
    // Dans le projet ouvert s'il a des pièces ; sinon un projet neuf, où l'on crée ou insère des pièces.
    if (m_doc->projet().pieces.empty()) {
      if (!confirmerAbandon()) return;
      m_doc->nouveau();
      m_doc->modifier([](Projet& p) { p.sansCarte = true; });
    }
    m_doc->modifier([](Projet& p) { Assemblage a; a.nom = "Assemblage " + std::to_string(p.assemblages.size() + 1); p.assemblages.push_back(a); });
    montrerProjet();
    montrerAssemblage(static_cast<int>(m_doc->projet().assemblages.size()) - 1);
    return;
  }
  if (code == QLatin1String("plan")) {
    if (m_doc->projet().pieces.empty()) { QMessageBox::information(this, QStringLiteral("Mise en plan"), QStringLiteral("Ouvre ou crée d’abord une pièce : la mise en plan se fait à partir d’elle et se range sous elle dans l’arbre.")); return; }
    montrerProjet();
    montrerAtelierPiece(true);
    m_atelier->nouvelleMiseEnPlan();
    return;
  }
  if (code == QLatin1String("tolerie")) {
    // Tôlerie : une pièce neuve, son contour esquissé sur le plan de dessus, puis la tôle de base
    if (!confirmerAbandon()) return;
    m_doc->nouveau();
    m_doc->modifier([](Projet& p) { p.sansCarte = true; p.nom = "Projet sans nom"; });
    montrerProjet();
    montrerAtelierPiece(true);
    m_atelier->nouvellePiece();
    m_atelier->esquisserTole();
    majNavigateur();
    return;
  }
  if (code == QLatin1String("piece") || code == QLatin1String("mixte-vide")) {
    if (!confirmerAbandon()) return;
    m_doc->nouveau();
    m_doc->modifier([](Projet& p) { p.sansCarte = true; p.nom = "Projet sans nom"; });
    montrerProjet();
    montrerAtelierPiece(true);
    if (code == QLatin1String("piece")) m_atelier->nouvellePiece();
    majNavigateur();
  }
}

// Le navigateur suit le projet ; la pièce active y reçoit ses fonctions, interactives, depuis l'atelier.
void FenetrePrincipale::majNavigateur() {
  if (!m_navigateur || m_majNavigateurEnCours) return;
  m_majNavigateurEnCours = true;
  const bool pieces = m_ateliers->currentIndex() == 1 && m_atelier;
  if (m_atelier) m_atelier->setRacineArbre(nullptr);
  const Projet& p = m_doc->projet();
  const QString nom = m_doc->chemin().isEmpty() ? QString::fromStdString(p.nom) : QFileInfo(m_doc->chemin()).completeBaseName().replace(QStringLiteral(".prom"), QString());
  const int elec = m_electronique && m_ateliers->currentWidget() == m_electronique ? m_electronique->conception() : -1;
  const int electrique = m_electrique && m_ateliers->currentWidget() == m_electrique ? m_electrique->projet() : -1;
  m_navigateur->maj(p, nom, m_atelier ? m_atelier->pieceActive() : -1, pieces, elec, electrique);
  m_arbrePerime = false;
  m_cheminArbre = m_doc->chemin();
  m_signaturesArbre.clear();
  for (const auto& c : p.electronique) m_signaturesArbre[c.uuid] = Navigateur::signatureConception(c);
  // L'arbre refait reprend la sélection de l'atelier (3.11)
  if (m_electronique && elec >= 0) m_navigateur->suivreSelection(elec, m_electronique->selection()->composants());
  majMenusAtelier();  // « Verrouiller » et « Dossier de fabrication » du boîtier suivent la carte et l'atelier
  if (m_atelier && pieces) {
    m_atelier->attacherArbre(m_navigateur);
    m_atelier->setRacineArbre(m_navigateur->itemPieceActive());
  }
  m_majNavigateurEnCours = false;
}

void FenetrePrincipale::montrerAtelierPiece(bool pieces) {
  if (m_pages && m_pages->currentIndex() != 1) m_pages->setCurrentIndex(1);
  if (pieces && !m_atelier) {
    journal("création de l'atelier Pièces");
    m_atelier = new AtelierPiece(m_doc);
    m_atelier->setEtatIntegre(m_dansCoquille);
    connect(m_atelier, &AtelierPiece::messageChange, this, [this](const QString& t) { m_messageAtelier = t; emit etatTexteChange(); });
    connect(m_atelier, &AtelierPiece::historiqueChange, this, [this] { m_majHistorique(); });
    QWidget* ancien = m_ateliers->widget(1);
    m_ateliers->insertWidget(1, m_atelier);
    m_ateliers->removeWidget(ancien);
    ancien->deleteLater();
    journal("atelier Pièces créé");
  }
  m_ateliers->setCurrentIndex(pieces ? 1 : 0);
  if (m_actionBoitier) (pieces ? m_actionPieces : m_actionBoitier)->setChecked(true);
  if (m_dock) m_dock->setVisible(!pieces);
  majNavigateur();
}

Qt::Edges FenetrePrincipale::bordsSous(const QPoint& p) const {
  Qt::Edges b;
  const int m = 6;
  if (p.x() <= m) b |= Qt::LeftEdge;
  if (p.x() >= width() - m) b |= Qt::RightEdge;
  if (p.y() <= m) b |= Qt::TopEdge;
  if (p.y() >= height() - m) b |= Qt::BottomEdge;
  return b;
}

void FenetrePrincipale::mouseMoveEvent(QMouseEvent* e) {
  if (m_cadreIntegre && !isMaximized()) {
    const Qt::Edges b = bordsSous(e->position().toPoint());
    if (b == (Qt::LeftEdge | Qt::TopEdge) || b == (Qt::RightEdge | Qt::BottomEdge)) setCursor(Qt::SizeFDiagCursor);
    else if (b == (Qt::RightEdge | Qt::TopEdge) || b == (Qt::LeftEdge | Qt::BottomEdge)) setCursor(Qt::SizeBDiagCursor);
    else if (b & (Qt::LeftEdge | Qt::RightEdge)) setCursor(Qt::SizeHorCursor);
    else if (b & (Qt::TopEdge | Qt::BottomEdge)) setCursor(Qt::SizeVerCursor);
    else unsetCursor();
  }
  QMainWindow::mouseMoveEvent(e);
}

void FenetrePrincipale::mousePressEvent(QMouseEvent* e) {
  if (m_cadreIntegre && !isMaximized() && e->button() == Qt::LeftButton && windowHandle()) {
    const Qt::Edges b = bordsSous(e->position().toPoint());
    if (b) { windowHandle()->startSystemResize(b); return; }
  }
  QMainWindow::mousePressEvent(e);
}

void FenetrePrincipale::changeEvent(QEvent* e) {
  if (m_cadreIntegre && e->type() == QEvent::WindowStateChange) setContentsMargins(isMaximized() ? QMargins() : QMargins(5, 5, 5, 5));
  QMainWindow::changeEvent(e);
}

void FenetrePrincipale::paintEvent(QPaintEvent* e) {
  QMainWindow::paintEvent(e);
  if (m_cadreIntegre && !isMaximized()) {
    QPainter g(this);
    g.setPen(theme().bordure(QColor(0xC9, 0xCE, 0xDD)));
    g.drawRect(rect().adjusted(0, 0, -1, -1));
  }
}

QString FenetrePrincipale::texteEtat() const {
  const bool projet = m_pages && m_pages->currentIndex() == 1;
  if (!projet) return QString();
  QString t = m_messageAtelier;
  // L'atelier électronique a sa propre barre d'état, bilan et messages (G8)
  if (m_electronique && m_ateliers->currentWidget() == m_electronique) return QString();
  if (m_electrique && m_ateliers->currentWidget() == m_electrique) return t;
  if (!t.isEmpty()) t += QStringLiteral("&nbsp;&nbsp;·&nbsp;&nbsp;");
  return t + m_etat->text();
}

void FenetrePrincipale::lienEtat(const QString& lien) {
  if (lien == QLatin1String("erreur") && m_atelier) m_atelier->montrerPremiereErreur();
}

// La fenêtre des réglages, ouverte sur un réglage surligné (2.32) ; sans clé, sur sa page d'accueil
void FenetrePrincipale::ouvrirReglage(const QString& cle) {
  if (!m_reglages) m_reglages = new FenetreReglages(this);
  m_reglages->show();
  m_reglages->raise();
  m_reglages->activateWindow();
  if (!cle.isEmpty()) m_reglages->montrerReglage(cle);
}
