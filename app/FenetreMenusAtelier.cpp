// Prométhée : menus de la fenêtre selon l'atelier actif, et réouverture sur le dernier atelier (lot L1, tâche 1.14,
// G1, G7). Dans l'atelier électronique, les actions de la mécanique (import et export KiCad du boîtier, exemple,
// dossier de fabrication du boîtier, Ajouter du boîtier, vues 3D, couvercle et boîtier) se cachent ; celles de
// l'atelier prennent leur place dans Fichier, Édition, Ajouter et Affichage. Ctrl+I et Ctrl+E passent d'un sens à
// l'autre, jamais deux à la fois. Les entrées qui reprennent une touche du canevas la montrent sans la déclarer : le
// canevas la garde, la fenêtre ne la lui dispute pas (ERG-03).
// Licence GPL-3.0-only.
#include <QActionGroup>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStackedWidget>

#include "AtelierElectronique.hpp"
#include "DispositionsAtelier.hpp"
#include "AtelierElectrique.hpp"
#include "Document.hpp"
#include "FenetrePrincipale.hpp"
#include "RegistreActions.hpp"

namespace {
// « Copier » et sa première touche, montrée dans la colonne des raccourcis du menu
QString avecTouche(const QString& libelle, std::string_view action) {
  const QString touches = RegistreActions::atelierElectronique().touches(action);
  return touches.isEmpty() ? libelle : libelle + QLatin1Char('\t') + touches.section(QStringLiteral(", "), 0, 0);
}
constexpr int ATELIERS_RETENUS = 30;
}  // namespace

bool FenetrePrincipale::atelierElectroniqueActif() const {
  return m_electronique && m_pages && m_pages->currentIndex() == 1 && m_ateliers && m_ateliers->currentWidget() == m_electronique;
}

void FenetrePrincipale::construireMenusElectronique(QMenu* fichier, QMenu* edition, QMenu* affichage) {
  Q_UNUSED(fichier);
  // Une action de l'atelier : sans effet hors de lui, cachée ailleurs
  auto agir = [this](std::string_view id) {
    return [this, id] {
      if (atelierElectroniqueActif()) m_electronique->executerAction(id);
    };
  };
  struct Entree {
    const char* id;
    const char* libelle;
  };
  // Édition : le presse-papiers et la sélection du canevas ouvert
  m_actionsElec << edition->addSeparator();
  static constexpr Entree editer[] = {{"edition.couper", "Couper"},         {"edition.copier", "Copier"},           {"edition.coller", "Coller"},
                                      {"edition.dupliquer", "Dupliquer"},   {"selection.supprimer", "Supprimer"},   {"selection.tout", "Tout choisir"},
                                      {"objet.tourner", "Tourner"},         {"schema.poserCopie", "Poser une copie"}, {"schema.repeter", "Répéter la pose"},
                                      {"edition.chercher", "Chercher et remplacer…"}, {"schema.relierListe", "Relier une liste de paires…"},
                                      {"schema.decalerCroisements", "Décaler les croisements…"}, {"edition.collerMatrice", "Coller en matrice…"},
                                      {"edition.collageSpecial", "Collage spécial…"}, {"edition.copierTexte", "Copier comme texte"},
                                      {"selection.inverser", "Inverser la sélection"}, {"selection.etendre", "Étendre la sélection"},
                                      {"selection.reduire", "Réduire la sélection"}, {"selection.semblables", "Choisir les semblables…"},
                                      {"selection.memoires", "Mémoires de sélection"}, {"selection.relieSuivant", "Objet relié suivant"}};
  for (const auto& [id, libelle] : editer) {
    QAction* a = edition->addAction(avecTouche(QString::fromUtf8(libelle), id), this, agir(id));
    a->setObjectName(QStringLiteral("menu:") + QLatin1String(id));
    marquerAction(a, id);
    m_actionsElec << a;
  }
  // Ajouter : les outils du schéma ou du circuit, selon le mode
  m_menuAjouterElec = new QMenu(QStringLiteral("&Ajouter"), this);
  m_menuAjouterElec->setObjectName(QStringLiteral("menuAjouterElectronique"));
  static constexpr Entree schema[] = {{"schema.choisirComposant", "Composant…"}, {"schema.fil", "Fil"},        {"schema.etiquette", "Étiquette"},
                                      {"schema.etiquetteGlobale", "Étiquette globale"}, {"schema.alimentation", "Alimentation"},
                                      {"schema.nonConnexion", "Non-connexion"}, {"schema.jonction", "Jonction"}, {"schema.texte", "Texte"}};
  static constexpr Entree circuit[] = {{"circuit.router", "Piste"}, {"circuit.via", "Via"}};
  m_menuAjouterElec->addSection(QStringLiteral("Schéma"));
  QList<QAction*> actionsSchema, actionsCircuit;
  for (const auto& [id, libelle] : schema) {
    QAction* a = m_menuAjouterElec->addAction(avecTouche(QString::fromUtf8(libelle), id), this, agir(id));
    a->setObjectName(QStringLiteral("menu:") + QLatin1String(id));
    marquerAction(a, id);
    actionsSchema << a;
  }
  m_menuAjouterElec->addSection(QStringLiteral("Circuit imprimé"));
  for (const auto& [id, libelle] : circuit) {
    QAction* a = m_menuAjouterElec->addAction(avecTouche(QString::fromUtf8(libelle), id), this, agir(id));
    a->setObjectName(QStringLiteral("menu:") + QLatin1String(id));
    marquerAction(a, id);
    actionsCircuit << a;
  }
  connect(m_menuAjouterElec, &QMenu::aboutToShow, this, [this, actionsSchema, actionsCircuit] {
    const int mode = atelierElectroniqueActif() ? m_electronique->mode() : -1;
    for (QAction* a : actionsSchema) a->setEnabled(mode == AtelierElectronique::Schema);
    for (QAction* a : actionsCircuit) a->setEnabled(mode == AtelierElectronique::Circuit);
  });
  m_actionsElec << m_menuAjouterElec->menuAction();
  // Affichage : les quatre modes, la vue, la bibliothèque, les raccourcis, les messages
  m_actionsElec << affichage->addSection(QStringLiteral("Atelier électronique"));
  const std::pair<int, const char*> modes[] = {{AtelierElectronique::Schema, "Schéma"}, {AtelierElectronique::Circuit, "Circuit imprimé"},
                                               {AtelierElectronique::Vue3D, "Vue 3D"}, {AtelierElectronique::Fabrication, "Fabrication"}};
  QList<QAction*> actionsModes;
  for (const auto& [mode, libelle] : modes) {
    QAction* a = affichage->addAction(QString::fromUtf8(libelle), this, [this, m = mode] {
      if (atelierElectroniqueActif()) m_electronique->setMode(m);
    });
    a->setObjectName(QStringLiteral("menu:mode%1").arg(mode));
    actionsModes << a;
    m_actionsElec << a;
  }
  connect(affichage, &QMenu::aboutToShow, this, [this, actionsModes] {
    // Schéma seul : ni circuit, ni 3D, ni fabrication avant que le circuit soit créé (STR-13)
    const prom::ecad::ConceptionElec* c = atelierElectroniqueActif() ? m_electronique->conceptionCourante() : nullptr;
    for (int i = 1; i < actionsModes.size(); ++i) actionsModes[i]->setEnabled(!(c && c->schemaSeul));
  });
  QAction* toutVoir = affichage->addAction(avecTouche(QStringLiteral("Tout voir"), "vue.toutVoir"), this, agir("vue.toutVoir"));
  QAction* ensemble = affichage->addAction(avecTouche(QStringLiteral("Vue d’ensemble"), "vue.vueEnsemble"), this, agir("vue.vueEnsemble"));
  ensemble->setObjectName(QStringLiteral("menu:vue.vueEnsemble"));
  marquerAction(ensemble, "vue.vueEnsemble");
  m_actionsElec << ensemble;
  // Plein écran, consultation, cadrages nommés, étalonnage (2.29)
  for (const auto& [id, libelle] : std::initializer_list<std::pair<const char*, const char*>>{
           {"vue.pleinEcran", "Plein écran"}, {"vue.consultation", "Consultation (lecture seule)"}, {"vue.cadrageEnregistrer", "Enregistrer ce cadrage…"}, {"vue.etalonner", "Étalonner l’écran…"},
           {"atelier.mosaique", "Mosaïque des feuilles…"}}) {
    QAction* x = affichage->addAction(avecTouche(QString::fromUtf8(libelle), id), this, agir(id));
    x->setObjectName(QStringLiteral("menu:") + QLatin1String(id));
    marquerAction(x, id);
    m_actionsElec << x;
  }
  QAction* apprendre = affichage->addAction(QStringLiteral("Apprendre…"), this, agir("atelier.apprendre"));
  apprendre->setObjectName(QStringLiteral("menu:atelier.apprendre"));
  marquerAction(apprendre, "atelier.apprendre");
  m_actionsElec << apprendre;
  QAction* biblio = affichage->addAction(QStringLiteral("Bibliothèque"), this, [this] {
    if (atelierElectroniqueActif()) m_electronique->montrerBibliotheque();
  });
  QAction* raccourcis = affichage->addAction(QStringLiteral("Raccourcis\tF1"), this, [this] {
    if (atelierElectroniqueActif()) m_electronique->montrerRaccourcis();
  });
  QAction* messages = affichage->addAction(QStringLiteral("Messages"), this, [this] {
    if (atelierElectroniqueActif()) m_electronique->barreEtat()->ouvrirMessages();
  });
  // Panneaux et dispositions (1.21, ERG-23) : le même menu que le bouton « Panneaux » de la barre d'état
  QMenu* panneaux = affichage->addMenu(QStringLiteral("Panneaux et dispositions"));
  panneaux->setObjectName(QStringLiteral("menuDispositions"));
  connect(panneaux, &QMenu::aboutToShow, this, [this, panneaux] {
    if (atelierElectroniqueActif()) m_electronique->dispositions()->remplir(panneaux);
    else panneaux->clear();
  });
  m_actionsElec << panneaux->menuAction();
  toutVoir->setObjectName(QStringLiteral("menu:vue.toutVoir"));
  biblio->setObjectName(QStringLiteral("menu:bibliotheque"));
  raccourcis->setObjectName(QStringLiteral("menu:raccourcis"));
  messages->setObjectName(QStringLiteral("menu:messages"));
  marquerAction(toutVoir, "vue.toutVoir");
  marquerAction(raccourcis, "atelier.raccourcis");
  m_actionsElec << toutVoir << biblio << raccourcis << messages;
}

void FenetrePrincipale::majMenusAtelier() {
  const bool elec = atelierElectroniqueActif();
  for (QAction* a : m_actionsMeca) a->setVisible(!elec);
  for (QAction* a : m_actionsElec) a->setVisible(elec);
  if (m_menuAjouter) m_menuAjouter->menuAction()->setVisible(!elec);
  // Une touche, un sens : Ctrl+I et Ctrl+E vont à l'atelier électronique quand il est ouvert
  if (m_importKicadMeca && m_importKicadElec) {
    m_importKicadMeca->setShortcut(elec ? QKeySequence() : QKeySequence(Qt::CTRL | Qt::Key_I));
    m_importKicadElec->setShortcut(elec ? QKeySequence(Qt::CTRL | Qt::Key_I) : QKeySequence());
    m_importKicadElec->setText(elec ? QStringLiteral("&Importer une carte KiCad…") : QStringLiteral("Importer une carte KiCad (électronique)…"));
  }
  if (m_dossierMeca && m_dossierElec) {
    m_dossierMeca->setShortcut(elec ? QKeySequence() : QKeySequence(Qt::CTRL | Qt::Key_E));
    m_dossierElec->setShortcut(elec ? QKeySequence(Qt::CTRL | Qt::Key_E) : QKeySequence());
  }
  // Verrou d'un composant du boîtier et dossier du boîtier : avec une carte mécanique, hors de l'atelier électronique
  const bool carte = m_doc && !m_doc->projet().sansCarte;
  for (QAction* a : findChildren<QAction*>())
    if (a->text() == QStringLiteral("Verrouiller") || a->text() == QStringLiteral("Verrouillé") || a->text() == QStringLiteral("Dossier de fabrication")) a->setVisible(carte && !elec);
}

// ---------- Dernier atelier, par fichier (G7) ----------
QString FenetrePrincipale::atelierCourant() const {
  if (!m_pages || m_pages->currentIndex() != 1 || !m_ateliers) return {};
  QWidget* w = m_ateliers->currentWidget();
  if (m_electronique && w == m_electronique) return QStringLiteral("elec:%1:%2").arg(m_electronique->conception()).arg(m_electronique->mode());
  if (m_electrique && w == m_electrique) return QStringLiteral("electrique:%1:%2").arg(m_electrique->projet()).arg(m_electrique->mode());
  if (m_ateliers->currentIndex() == 1) return QStringLiteral("pieces");
  if (m_ateliers->currentIndex() == 0) return QStringLiteral("carte");
  return {};
}

void FenetrePrincipale::retenirAtelier() {
  if (!m_doc || m_doc->chemin().isEmpty()) return;
  const QString atelier = atelierCourant();
  if (atelier.isEmpty()) return;
  const QString chemin = QDir::cleanPath(QFileInfo(m_doc->chemin()).absoluteFilePath());
  QSettings reglages;
  QStringList r = reglages.value(QStringLiteral("derniersAteliers")).toStringList();
  const QString ligne = chemin + QLatin1Char('|') + atelier;
  if (!r.isEmpty() && r.front() == ligne) return;
  r.erase(std::remove_if(r.begin(), r.end(), [&](const QString& e) { return e.section(QLatin1Char('|'), 0, 0) == chemin; }), r.end());
  r.prepend(ligne);
  while (r.size() > ATELIERS_RETENUS) r.removeLast();
  reglages.setValue(QStringLiteral("derniersAteliers"), r);
}

bool FenetrePrincipale::rouvrirAtelier() {
  if (m_doc->chemin().isEmpty()) return false;
  const QString chemin = QDir::cleanPath(QFileInfo(m_doc->chemin()).absoluteFilePath());
  QString atelier;
  for (const QString& e : QSettings().value(QStringLiteral("derniersAteliers")).toStringList())
    if (e.section(QLatin1Char('|'), 0, 0) == chemin) atelier = e.section(QLatin1Char('|'), 1);
  if (atelier.isEmpty()) return false;
  const QStringList p = atelier.split(QLatin1Char(':'));
  const auto& projet = m_doc->projet();
  if (p.value(0) == QLatin1String("elec") && p.value(1).toInt() >= 0 && p.value(1).toInt() < static_cast<int>(projet.electronique.size())) {
    montrerElectronique(p.value(1).toInt(), std::clamp(p.value(2).toInt(), 0, static_cast<int>(AtelierElectronique::Fabrication)));
    return true;
  }
  if (p.value(0) == QLatin1String("electrique") && p.value(1).toInt() >= 0 && p.value(1).toInt() < static_cast<int>(projet.electrique.size())) {
    montrerElectrique(p.value(1).toInt(), std::max(0, p.value(2).toInt()));
    return true;
  }
  if (p.value(0) == QLatin1String("pieces") && (!projet.pieces.empty() || !projet.assemblages.empty())) {
    montrerAtelierPiece(true);
    return true;
  }
  if (p.value(0) == QLatin1String("carte") && !projet.sansCarte) {
    montrerAtelierPiece(false);
    return true;
  }
  return false;
}
