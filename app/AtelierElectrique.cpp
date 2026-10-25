// Prométhée : atelier électrique — schéma en folios, palette CEI, pose guidée, propriétés, problèmes, armoire,
// faisceau. Les rapports, calculs et l'assistant logement sont dans AtelierElectriquePages.cpp.
// Licence GPL-3.0-only.
#include "AtelierElectrique.hpp"

#include <algorithm>
#include <map>
#include <set>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QCursor>
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
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "Document.hpp"
#include "EtiquetteEtat.hpp"
#include "FormulaireLie.hpp"
#include "Icones.hpp"
#include "Theme.hpp"
#include "VueArmoire.hpp"
#include "VueFaisceau.hpp"
#include "VueGrafcet.hpp"
#include "VueSchemaElectrique.hpp"
#include "promethee/electrique_calculateurs.hpp"
#include "promethee/electrique_fabrication.hpp"
#include "promethee/electrique_glossaire.hpp"
#include "promethee/electrique_parcours.hpp"
#include "promethee/modele.hpp"

using namespace prom::elec;
using prom::ecad::ComposantElec;
using prom::ecad::DefSymbole;
using prom::ecad::Pt;

namespace {
const char* STYLE =
    "#panneauElectrique { background: #FAFBFE; }"
    "#panneauElectrique QGroupBox { background: #F3F5FB; border: 1px solid #E1E5F0; border-radius: 9px; margin-top: 16px; padding: 12px 8px 8px 8px; }"
    "#panneauElectrique QGroupBox::title { subcontrol-origin: margin; left: 10px; top: 2px; padding: 0 4px; color: #6B7393; font-weight: 600; }"
    "#panneauElectrique QPushButton { text-align: left; padding: 6px 9px; border: 1px solid #CDD3E4; border-radius: 7px; background: white; color: #232A45; }"
    "#panneauElectrique QPushButton:hover { border-color: #E0A040; }"
    "#panneauElectrique QPushButton#erreur { color: #C2402E; } #panneauElectrique QPushButton#alerte { color: #A8620E; }"
    "#panneauElectrique QPushButton#principal { background: #C26E00; color: white; border-color: #C26E00; font-weight: 600; }";
const char* STYLE_RUBAN =
    "QTabWidget::pane { border: none; }"
    "QTabBar::tab { padding: 6px 14px; font-weight: 600; color: #4A5270; border: none; }"
    "QTabBar::tab:selected { color: #10142A; border-bottom: 3px solid #C26E00; }"
    "QToolBar { border: none; spacing: 2px; background: #FAFBFE; }"
    "QToolButton { border-radius: 6px; padding: 3px; color: #10142A; }"
    "QToolButton:hover { background: #FFF1D6; }"
    "QToolButton:checked { background: #FBE2B5; }"
    "QToolButton::menu-button { border: none; background: transparent; width: 14px; }";
const char* STYLE_GAUCHE =
    "#gaucheElectrique { background: #FAFBFE; }"
    "#gaucheElectrique QListWidget, #gaucheElectrique QTreeWidget { border: 1px solid #E1E5F0; border-radius: 8px; background: white; }"
    "#gaucheElectrique QTreeWidget::item, #gaucheElectrique QListWidget::item { padding: 3px; }"
    "#gaucheElectrique QLineEdit { padding: 5px 8px; border: 1px solid #CDD3E4; border-radius: 7px; background: white; }";
QString q(const std::string& s) { return QString::fromStdString(s); }
std::string s(const QString& t) { return t.trimmed().toStdString(); }

QLabel* titre(const QString& t) {
  auto* l = new QLabel(QStringLiteral("<span style='font-weight:700; color:#232A45; font-size:13px'>%1</span>").arg(t));
  l->setContentsMargins(2, 6, 2, 2);
  return l;
}

QString iconeFolio(TypeFolio t) {
  switch (t) {
    case TypeFolio::Garde: return QStringLiteral("plan");
    case TypeFolio::Puissance: return QStringLiteral("disjoncteur");
    case TypeFolio::Commande: return QStringLiteral("contacteur");
    case TypeFolio::Automate: return QStringLiteral("composants");
    case TypeFolio::Distribution:
    case TypeFolio::Unifilaire: return QStringLiteral("logement");
    case TypeFolio::Borniers: return QStringLiteral("borne");
    case TypeFolio::Implantation: return QStringLiteral("armoire");
    case TypeFolio::Nomenclature: return QStringLiteral("rapport");
    case TypeFolio::Grafcet: return QStringLiteral("grafcet");
    case TypeFolio::Cablage: return QStringLiteral("cablage");
    case TypeFolio::Libre: break;
  }
  return QStringLiteral("schema");
}

// Aperçu d'un symbole pour la palette, gardé une fois dessiné
QIcon iconeSymbole(const DefSymbole& d) {
  static std::map<std::string, QIcon> cache;
  const auto it = cache.find(d.id);
  if (it != cache.end()) return it->second;
  return cache[d.id] = QIcon(VueSchema::apercu(d, QSize(64, 44)));
}

bool estContact(RoleSymbole r) {
  switch (r) {
    case RoleSymbole::ContactNO:
    case RoleSymbole::ContactNF:
    case RoleSymbole::ContactInverseur:
    case RoleSymbole::ContactNOTempo:
    case RoleSymbole::ContactNFTempo:
    case RoleSymbole::PolesPuissance:
    case RoleSymbole::ContactThermiqueNF:
    case RoleSymbole::ContactThermiqueNO: return true;
    default: return false;
  }
}

const char* CANCEL = "\x01";  // choix abandonné
}  // namespace

AtelierElectrique::AtelierElectrique(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  chargerMesAppareils();
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(0, 0, 0, 0);
  col->setSpacing(0);
  m_ruban = new QTabWidget;
  m_ruban->setObjectName(QStringLiteral("rubanElectrique"));
  m_ruban->setDocumentMode(true);
  m_ruban->setStyleSheet(QString::fromLatin1(STYLE_RUBAN));
  m_ruban->setMaximumHeight(108);
  col->addWidget(m_ruban);
  m_corps = new QSplitter(Qt::Horizontal);
  m_gauche = new QStackedWidget;
  m_gauche->setObjectName(QStringLiteral("gaucheElectrique"));
  m_gauche->setStyleSheet(QString::fromLatin1(STYLE_GAUCHE));
  m_gauche->setMinimumWidth(180);
  m_vues = new QStackedWidget;
  m_schema = new VueSchemaElectrique(m_doc);
  m_schema->setObjectName(QStringLiteral("vueSchemaElectrique"));
  m_armoire = new VueArmoire(m_doc);
  m_armoire->setObjectName(QStringLiteral("vueArmoire"));
  m_faisceau = new VueFaisceau(m_doc);
  m_faisceau->setObjectName(QStringLiteral("vueFaisceau"));
  m_pageRapports = pageRapports();
  m_pageCalculs = pageCalculs();
  m_pageLogement = pageLogement();
  m_pageDistribution = pageDistribution();
  m_pagePV = pagePV();
  m_pageGrafcet = pageGrafcet();
  // Une vue par mode, dans l'ordre des identifiants de mode
  m_vues->addWidget(m_schema);            // Schema
  m_vues->addWidget(m_armoire);           // Armoire
  m_vues->addWidget(m_faisceau);          // Faisceau
  m_vues->addWidget(m_pageRapports);      // Rapports
  m_vues->addWidget(m_pageCalculs);       // Calculs
  m_vues->addWidget(m_pageLogement);      // Logement
  m_vues->addWidget(m_pageDistribution);  // Distribution
  m_vues->addWidget(m_pagePV);            // Photovoltaique
  m_vues->addWidget(m_pageGrafcet);       // Grafcet
  m_gauche->addWidget(construireGauche());
  {
    QWidget* g = gaucheDistribution();
    m_pileGauche->addWidget(g);
    m_pagesGauche[Distribution] = g;
    QWidget* c = gaucheCalculs();
    m_pileGauche->addWidget(c);
    m_pagesGauche[Calculs] = c;
    QWidget* gg = gaucheGrafcet();
    m_pileGauche->addWidget(gg);
    m_pagesGauche[Grafcet] = gg;
    choisirCalcul(m_calculChoisi);  // la liste montre la fiche ouverte
  }
  m_panneau = new QScrollArea;
  m_panneau->setObjectName(QStringLiteral("defilementPanneauElectrique"));
  m_panneau->setWidgetResizable(true);
  m_panneau->setMinimumWidth(240);
  m_panneau->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);  // le contenu suit la largeur du panneau
  m_panneau->setFrameShape(QFrame::NoFrame);
  m_corps->addWidget(m_gauche);
  m_corps->addWidget(m_vues);
  m_corps->addWidget(m_panneau);
  m_corps->setStretchFactor(1, 1);
  m_corps->setSizes({215, 1000, 280});
  m_corps->setCollapsible(1, false);
  col->addWidget(m_corps, 1);
  m_etat = new EtiquetteEtat;
  m_etat->setContentsMargins(10, 4, 10, 4);
  connect(m_etat, &EtiquetteEtat::texteChange, this, &AtelierElectrique::messageChange);
  col->addWidget(m_etat);
  m_minuterie = new QTimer(this);
  m_minuterie->setSingleShot(true);
  m_minuterie->setInterval(60);
  connect(m_minuterie, &QTimer::timeout, this, [this] {
    recalculer();
    suivreParcours();
    majFolios();
    const int m = mode();
    if (m == Rapports) majRapport();
    if (m == Calculs) majCalculs();
    if (m == Distribution) majDistribution();
    if (m == Photovoltaique) majPV();
    if (m == Grafcet) majGrafcet();
    // Pendant la saisie dans le panneau, il ne se reconstruit pas sous les doigts
    QWidget* focus = QApplication::focusWidget();
    const bool saisie = focus && m_panneau->isAncestorOf(focus) && (qobject_cast<QLineEdit*>(focus) || qobject_cast<QAbstractSpinBox*>(focus));
    if (!saisie) majPanneau();
  });
  construireRuban();
  connect(m_ruban, &QTabWidget::currentChanged, this, [this](int onglet) {
    const int i = modeOnglet(onglet);
    m_vues->setCurrentIndex(i);
    // La colonne de gauche sert au schéma, à l'armoire, au faisceau, aux rapports et à la distribution ; le logement
    // et les calculs prennent toute la place
    const auto page = m_pagesGauche.find(i);
    m_gauche->setVisible(page != m_pagesGauche.end());
    if (page != m_pagesGauche.end()) m_pileGauche->setCurrentWidget(page->second);
    if (i == Rapports) majRapport();
    if (i == Calculs) majCalculs();
    if (i == Logement) majLogement();
    if (i == Distribution) majDistribution();
    if (i == Photovoltaique) majPV();
    if (i == Grafcet) majGrafcet();
    if (i == Armoire) m_armoire->recadrer();
    if (i == Faisceau) m_faisceau->recadrer();
    majPanneau();
  });
  connect(m_doc, &Document::change, this, [this] {
    if (isVisible()) m_minuterie->start();
  });
  connect(m_doc, &Document::projetRemplace, this, [this] {
    m_schema->selectionner({});
    m_armoire->selectionner({});
    m_faisceau->selectionner({});
    m_parentPose.clear();
  });
  m_schema->setPoseur([this](Pt p, int r) { poser(p, r); });
  connect(m_schema, &VueSchema::selectionChangee, this, [this] { majPanneau(); });
  connect(m_schema, &VueSchema::message, m_etat, &EtiquetteEtat::setText);
  connect(m_schema, &VueSchema::proprietesDemandees, this, [this] { majPanneau(); });
  connect(m_schema, &VueSchema::inspecteurDemande, this, [this] { majPanneau(); });
  connect(m_schema, &VueSchema::bibliothequeDemandee, this, [this] {
    m_recherche->setFocus();
    m_recherche->selectAll();
  });
  connect(m_armoire, &VueArmoire::selectionChangee, this, [this] { majPanneau(); });
  connect(m_armoire, &VueArmoire::message, m_etat, &EtiquetteEtat::setText);
  connect(m_faisceau, &VueFaisceau::selectionChangee, this, [this] { majPanneau(); });
  connect(m_faisceau, &VueFaisceau::message, m_etat, &EtiquetteEtat::setText);
}

void AtelierElectrique::setEtatIntegre(bool oui) { m_etat->setVisible(!oui); }
int AtelierElectrique::modeOnglet(int index) const {
  return index >= 0 && index < static_cast<int>(m_ordreModes.size()) ? m_ordreModes[static_cast<std::size_t>(index)] : Schema;
}
int AtelierElectrique::ongletMode(int m) const {
  const auto it = std::find(m_ordreModes.begin(), m_ordreModes.end(), m);
  return it == m_ordreModes.end() ? 0 : static_cast<int>(it - m_ordreModes.begin());
}
int AtelierElectrique::mode() const { return modeOnglet(m_ruban->currentIndex()); }
void AtelierElectrique::choisirMode(int m) { m_ruban->setCurrentIndex(ongletMode(m)); }
int AtelierElectrique::folio() const { return m_schema->feuille(); }

const ProjetElectrique* AtelierElectrique::projetCourant() const {
  const auto& p = m_doc->projet();
  return m_projet >= 0 && m_projet < static_cast<int>(p.electrique.size()) ? &p.electrique[static_cast<std::size_t>(m_projet)] : nullptr;
}

void AtelierElectrique::modifierProjet(const std::function<void(ProjetElectrique&)>& f) {
  const int i = m_projet;
  m_doc->modifier([&](prom::Projet& p) {
    if (i >= 0 && i < static_cast<int>(p.electrique.size())) f(p.electrique[static_cast<std::size_t>(i)]);
  });
}

void AtelierElectrique::modifierProjetNomme(const std::function<void(ProjetElectrique&)>& f, const QString& nom) {
  const int i = m_projet;
  m_doc->modifier(
      [&](prom::Projet& p) {
        if (i >= 0 && i < static_cast<int>(p.electrique.size())) f(p.electrique[static_cast<std::size_t>(i)]);
      },
      nom);
}

void AtelierElectrique::ouvrir(int projet, int mode, int folio) {
  // Autre projet : autre indice, ou même indice dans un autre document (projet neuf, exemple, fichier ouvert)
  const auto& tous = m_doc->projet().electrique;
  const std::string uuid = projet >= 0 && projet < static_cast<int>(tous.size()) ? tous[static_cast<std::size_t>(projet)].uuid : std::string();
  const bool autre = projet != m_projet || uuid != m_uuidProjet;
  m_projet = projet;
  m_uuidProjet = uuid;
  if (autre) m_formAppareil = false;
  const ProjetElectrique* pe = projetCourant();
  const int n = pe ? static_cast<int>(pe->schema.feuilles.size()) : 0;
  // Un projet neuf s'ouvre sur son premier folio utile (puissance), pas sur la page de garde
  if (autre && folio == 0 && pe && n > 1 && pe->folio(pe->schema.feuilles[0].uuid).type == TypeFolio::Garde && mode == Schema) folio = 1;
  m_schema->setConception(projet, std::clamp(folio, 0, std::max(0, n - 1)));
  m_armoire->setArmoire(projet, pe && !pe->armoires.empty() ? std::clamp(m_armoire->armoire(), 0, static_cast<int>(pe->armoires.size()) - 1) : -1);
  m_faisceau->setFaisceau(projet, pe && !pe->faisceaux.empty() ? std::clamp(m_faisceau->faisceau(), 0, static_cast<int>(pe->faisceaux.size()) - 1) : -1);
  recalculer();
  majFolios();
  majPalette();
  if (this->mode() != mode) choisirMode(mode);
  else {
    if (mode == Rapports) majRapport();
    if (mode == Logement) majLogement();
    if (mode == Distribution) majDistribution();
    if (mode == Photovoltaique) majPV();
    if (mode == Grafcet) majGrafcet();
    majPanneau();
  }
  if (autre) {
    QTimer::singleShot(0, this, [this] { m_schema->recadrer(); });
    if (pe) {
      int poses = 0;
      for (const auto& f : pe->schema.feuilles) poses += static_cast<int>(f.symboles.size());
      const QString nom = q(pe->nom).toHtmlEscaped();
      // Premier message selon ce qui s'ouvre : ce qu'il y a à faire ici
      if (mode == Logement)
        m_etat->setText(QStringLiteral("<b>%1</b> : décris les pièces et les équipements, puis « Créer le schéma unifilaire et le coffret ».").arg(nom));
      else if (mode == Distribution)
        m_etat->setText(pe->distribution ? QStringLiteral("<b>%1</b> : choisis un départ dans l’arbre pour le régler ; tout se recalcule à chaque modification.").arg(nom)
                                         : QStringLiteral("<b>%1</b> : pars d’un modèle d’installation ou d’une installation vide, puis ajoute tableaux et départs.").arg(nom));
      else if (mode == Photovoltaique)
        m_etat->setText(pe->photovoltaique ? QStringLiteral("<b>%1</b> : règle le matériel et le site à droite ; le calepinage et les protections se recalculent.").arg(nom)
                                           : QStringLiteral("<b>%1</b> : choisis une taille d’installation au centre, puis remplace le matériel générique par le tien.").arg(nom));
      else if (mode == Armoire && poses == 0)
        m_etat->setText(QStringLiteral("<b>%1</b> : pose les appareils au schéma (onglet Schéma), ils viennent ensuite dans l’armoire d’un clic.").arg(nom));
      else if (mode == Faisceau)
        m_etat->setText(QStringLiteral("<b>%1</b> : pose des nœuds (N), relie-les par des branches (B), puis donne un connecteur aux extrémités.").arg(nom));
      else if (mode == Grafcet)
        m_etat->setText(pe->grafcets.empty() ? QStringLiteral("<b>%1</b> : crée un grafcet vierge ou pars d’un exemple de cours (ruban « Nouveau »).").arg(nom)
                                             : QStringLiteral("<b>%1</b> : clique une étape ou une transition pour la modifier ; « Simuler » pour la voir tourner.").arg(nom));
      else if (poses == 0)
        m_etat->setText(QStringLiteral("<b>%1</b> : choisis un appareil ou un circuit type dans la palette à gauche (ou tape son nom), puis clique sur le folio.").arg(nom));
      else
        m_etat->setText(QStringLiteral("<b>%1</b> : %2 folio(s), %3 appareil(s). Molette : zoom · clic droit glissé : déplacer la vue.")
                            .arg(nom)
                            .arg(pe->schema.feuilles.size())
                            .arg(pe->schema.composants.size()));
    }
  }
}

void AtelierElectrique::recalculer() {
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return;
  m_k = prom::ecad::connectivite(pe->schema);
  m_renvois = renvois(*pe);
  m_violations = verifierElectrique(*pe);
  m_schema->setConnectivite(&m_k);
  m_schema->setRenvois(&m_renvois);
  m_schema->setViolations(&m_violations);
}

// ---------- Ruban ----------
void AtelierElectrique::construireRuban() {
  struct V {
    QString icone, texte, bulle;
    std::function<void()> f;
  };
  // Onglets créés dans l'ordre des modes, montrés ensuite dans l'ordre du travail
  std::vector<std::pair<int, std::pair<QToolBar*, QString>>> onglets;
  auto onglet = [&onglets](const QString& titreOnglet, int modeOnglet) {
    auto* b = new QToolBar;
    b->setIconSize(QSize(30, 30));
    onglets.push_back({modeOnglet, {b, titreOnglet}});
    return b;
  };
  auto gros = [this](QToolBar* b, const QString& titreBouton, const std::vector<V>& vs, const QString& nomObjet = {}) {
    auto* t = new QToolButton;
    if (!nomObjet.isEmpty()) t->setObjectName(nomObjet);
    t->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    t->setIconSize(QSize(30, 30));
    t->setAutoRaise(true);
    t->setMinimumWidth(std::max(62, QFontMetrics(t->font()).horizontalAdvance(titreBouton) + (vs.size() > 1 ? 30 : 16)));
    std::vector<QAction*> acts;
    for (const auto& v : vs) {
      auto* a = new QAction(icone(v.icone), v.texte, t);
      a->setIconText(titreBouton);
      a->setToolTip(v.bulle.isEmpty() ? v.texte : v.texte + QStringLiteral(" — ") + v.bulle);
      const auto f = v.f;
      connect(a, &QAction::triggered, this, [f] { f(); });
      acts.push_back(a);
    }
    t->setDefaultAction(acts.front());
    if (acts.size() > 1) {
      auto* m = new QMenu(t);
      for (QAction* a : acts) m->addAction(a);
      t->setMenu(m);
      t->setPopupMode(QToolButton::MenuButtonPopup);
    }
    b->addWidget(t);
    return t;
  };
  auto grille = [this](QToolBar* b, const QString& legende, const std::vector<V>& cases) {
    auto* boite = new QWidget;
    auto* v = new QVBoxLayout(boite);
    v->setContentsMargins(4, 0, 4, 0);
    v->setSpacing(1);
    auto* g = new QGridLayout;
    g->setSpacing(1);
    for (std::size_t k = 0; k < cases.size(); ++k) {
      auto* t = new QToolButton;
      t->setIconSize(QSize(20, 20));
      t->setAutoRaise(true);
      auto* a = new QAction(icone(cases[k].icone), cases[k].texte, boite);
      a->setToolTip(cases[k].bulle.isEmpty() ? cases[k].texte : QStringLiteral("<b>%1</b><br>%2").arg(cases[k].texte, cases[k].bulle));
      const auto f = cases[k].f;
      connect(a, &QAction::triggered, this, [f] { f(); });
      t->setDefaultAction(a);
      g->addWidget(t, static_cast<int>(k) % 2, static_cast<int>(k) / 2);
    }
    v->addLayout(g);
    auto* l = new QLabel(legende);
    l->setAlignment(Qt::AlignHCenter);
    l->setStyleSheet(QStringLiteral("color: #4A5270; font-size: 11px;"));
    v->addWidget(l);
    b->addWidget(boite);
  };
  using O = VueSchema::Outil;
  // Parcours guidés, proposés dans les onglets où l'on débute
  auto apprendre = [&](QToolBar* b) {
    std::vector<V> liste;
    for (const auto& m : parcoursElectriques()) {
      const QString id = q(m.id);
      liste.push_back({QStringLiteral("apprendre"), q(m.nom), q(m.description), [this, id] { emit parcoursDemande(id); }});
    }
    gros(b, QStringLiteral("Apprendre"), liste, QStringLiteral("boutonApprendre"))->setPopupMode(QToolButton::InstantPopup);
  };
  // ----- Schéma -----
  QToolBar* bS = onglet(QStringLiteral("Schéma"), Schema);
  gros(bS, QStringLiteral("Appareil"),
       {{QStringLiteral("disjoncteur"), QStringLiteral("Chercher un appareil"), QStringLiteral("dans la palette de gauche : tape un mot (contacteur, moteur, bouton…) (A)"),
         [this] {
           m_recherche->setFocus();
           m_recherche->selectAll();
         }},
        {QStringLiteral("disjoncteur"), QStringLiteral("Disjoncteur 1P+N"), QString(), [this] { choisirSymbole("iec:disj-1pn"); }},
        {QStringLiteral("disjoncteur"), QStringLiteral("Disjoncteur moteur"), QString(), [this] { choisirSymbole("iec:disj-moteur-3p"); }},
        {QStringLiteral("contacteur"), QStringLiteral("Bobine de contacteur"), QString(), [this] { choisirSymbole("iec:bobine-km"); }},
        {QStringLiteral("contacteur"), QStringLiteral("Bobine de relais"), QString(), [this] { choisirSymbole("iec:bobine-ka"); }},
        {QStringLiteral("moteur"), QStringLiteral("Moteur triphasé"), QString(), [this] { choisirSymbole("iec:moteur-3"); }}},
       QStringLiteral("boutonAppareil"));
  gros(bS, QStringLiteral("Contact"),
       {{QStringLiteral("contacteur"), QStringLiteral("Contact à fermeture"), QStringLiteral("rattaché à sa bobine : 13-14, 23-24…"), [this] { choisirSymbole("iec:contact-no"); }},
        {QStringLiteral("contacteur"), QStringLiteral("Contact à ouverture"), QStringLiteral("rattaché à sa bobine : 11-12, 21-22…"), [this] { choisirSymbole("iec:contact-nf"); }},
        {QStringLiteral("contacteur"), QStringLiteral("Contact inverseur"), QString(), [this] { choisirSymbole("iec:contact-inverseur"); }},
        {QStringLiteral("contacteur"), QStringLiteral("Pôles de contacteur"), QStringLiteral("1-2, 3-4, 5-6 sur le folio de puissance"), [this] { choisirSymbole("iec:poles-km-3p"); }},
        {QStringLiteral("contacteur"), QStringLiteral("Contact du relais thermique (95-96)"), QString(), [this] { choisirSymbole("iec:contact-thermique-nf"); }}},
       QStringLiteral("boutonContact"));
  gros(bS, QStringLiteral("Fil"), {{QStringLiteral("fil"), QStringLiteral("Fil"), QStringLiteral("en équerre, d’une borne à l’autre (W)"), [this] { m_schema->setOutil(O::Fil); m_schema->setFocus(); }}});
  std::vector<V> pots;
  for (const char* n : {"L1", "L2", "L3", "N", "PE", "24V", "0V"})
    pots.push_back({QStringLiteral("potentiel"), QString::fromUtf8(n), QStringLiteral("potentiel posé au bout d’un fil ; le même nom ailleurs fait le renvoi"),
                    [this, n] { choisirPotentiel(QString::fromUtf8(n)); }});
  pots.push_back({QStringLiteral("potentiel"), QStringLiteral("Autre potentiel ou renvoi…"), QStringLiteral("nom demandé à la pose (P)"), [this] { choisirPotentiel(QString()); }});
  gros(bS, QStringLiteral("Potentiel"), pots, QStringLiteral("boutonPotentiel"));
  gros(bS, QStringLiteral("Borne"),
       {{QStringLiteral("borne"), QStringLiteral("Borne de bornier"), QStringLiteral("numérotée à la suite dans son bornier (-X1:1, -X1:2…)"), [this] { choisirSymbole("iec:borne"); }},
        {QStringLiteral("borne"), QStringLiteral("Borne de protection (PE)"), QString(), [this] { choisirSymbole("iec:borne-pe"); }},
        {QStringLiteral("borne"), QStringLiteral("Contact de connecteur"), QString(), [this] { choisirSymbole("iec:connecteur"); }}});
  {
    // Circuits types : un schéma courant posé câblé et repéré, en un clic
    std::vector<V> circuits;
    const auto& liste = circuitsTypes();
    for (std::size_t i = 0; i < liste.size(); ++i)
      circuits.push_back({liste[i].batiment ? QStringLiteral("logement") : QStringLiteral("moteur"), q(liste[i].nom), q(liste[i].description),
                          [this, i] { insererCircuit(static_cast<int>(i)); }});
    auto* bouton = gros(bS, QStringLiteral("Circuit type"), circuits, QStringLiteral("boutonCircuitType"));
    bouton->setPopupMode(QToolButton::InstantPopup);  // un clic ouvre la liste : aucun circuit n'est posé par mégarde
  }
  grille(bS, QStringLiteral("Poser"),
         {{QStringLiteral("cote"), QStringLiteral("Texte"), QStringLiteral("note sur le folio (T)"), [this] { m_schema->setOutil(O::Texte); m_schema->setFocus(); }},
          {QStringLiteral("annuler"), QStringLiteral("Non connecté"), QStringLiteral("borne laissée libre volontairement (Q)"), [this] { m_schema->setOutil(O::NonConnexion); m_schema->setFocus(); }},
          {QStringLiteral("point"), QStringLiteral("Jonction"), QStringLiteral("relie deux fils qui se croisent (J)"), [this] { m_schema->setOutil(O::Jonction); m_schema->setFocus(); }},
          {QStringLiteral("selection"), QStringLiteral("Choisir"), QStringLiteral("Échap"), [this] { m_schema->setOutil(O::Selection); m_schema->setFocus(); }}});
  grille(bS, QStringLiteral("Modifier"),
         {{QStringLiteral("pivoter"), QStringLiteral("Tourner"), QStringLiteral("R : les fils accrochés suivent"), [this] { m_schema->tournerSelection(); }},
          {QStringLiteral("symetrie"), QStringLiteral("Miroir gauche-droite"), QStringLiteral("X"), [this] { m_schema->miroirSelection(false); }},
          {QStringLiteral("symetrie"), QStringLiteral("Miroir haut-bas"), QStringLiteral("Y"), [this] { m_schema->miroirSelection(true); }},
          {QStringLiteral("supprimer"), QStringLiteral("Supprimer"), QStringLiteral("Suppr"), [this] { m_schema->supprimerSelection(); }}});
  grille(bS, QStringLiteral("Presse-papiers"),
         {{QStringLiteral("repetition-lineaire"), QStringLiteral("Copier"), QStringLiteral("Ctrl+C"), [this] { m_schema->copier(); }},
          {QStringLiteral("repetition-lineaire"), QStringLiteral("Coller"), QStringLiteral("Ctrl+V : sous la souris, les appareils neufs sont repérés"), [this] { m_schema->coller(); }},
          {QStringLiteral("repetition-lineaire"), QStringLiteral("Dupliquer"), QStringLiteral("Ctrl+D"), [this] { m_schema->dupliquer(); }},
          {QStringLiteral("supprimer"), QStringLiteral("Couper"), QStringLiteral("Ctrl+X"), [this] { m_schema->couper(); }}});
  bS->addSeparator();
  gros(bS, QStringLiteral("Repérer"),
       {{QStringLiteral("annoter"), QStringLiteral("Repérer les nouveaux"), QStringLiteral("appareils sans numéro (-KM?) : lettre et numéro suivant"), [this] { reperer(false); }},
        {QStringLiteral("annoter"), QStringLiteral("Tout renuméroter"), QStringLiteral("dans l’ordre de lecture : folio, colonne, de haut en bas"), [this] { reperer(true); }}});
  gros(bS, QStringLiteral("Numéroter"),
       {{QStringLiteral("numeroter"), QStringLiteral("Numéroter les fils"), QStringLiteral("chaque potentiel sans nom reçoit un numéro, qui ne bouge plus ensuite"), [this] { numeroterFils(false); }},
        {QStringLiteral("numeroter"), QStringLiteral("Tout renuméroter"), QStringLiteral("dans l’ordre de lecture des folios"), [this] { numeroterFils(true); }}},
       QStringLiteral("boutonNumeroter"));
  gros(bS, QStringLiteral("Vérifier"), {{QStringLiteral("contraintes"), QStringLiteral("Vérifier le projet"), QStringLiteral("bornes en l’air, contacts sans bobine, PE, sections, moteurs…"), [this] { verifier(); }}});
  apprendre(bS);
  gros(bS, QStringLiteral("Folio"),
       {{QStringLiteral("plan"), QStringLiteral("Nouveau folio de commande"), QString(), [this] { ajouterFolio(TypeFolio::Commande); }},
        {QStringLiteral("plan"), QStringLiteral("Nouveau folio de puissance"), QString(), [this] { ajouterFolio(TypeFolio::Puissance); }},
        {QStringLiteral("plan"), QStringLiteral("Nouveau folio d’automate"), QString(), [this] { ajouterFolio(TypeFolio::Automate); }},
        {QStringLiteral("plan"), QStringLiteral("Nouveau folio de distribution"), QString(), [this] { ajouterFolio(TypeFolio::Distribution); }},
        {QStringLiteral("plan"), QStringLiteral("Nouveau folio unifilaire"), QString(), [this] { ajouterFolio(TypeFolio::Unifilaire); }},
        {QStringLiteral("borne"), QStringLiteral("Folio des borniers"), QStringLiteral("dessiné d’office : chaque borne, ses deux côtés, son fil et son câble"),
         [this] { ajouterFolio(TypeFolio::Borniers); }},
        {QStringLiteral("armoire"), QStringLiteral("Folio d’implantation"), QStringLiteral("dessiné d’office : la plaque de fond et la porte de l’armoire, à l’échelle"),
         [this] { ajouterFolio(TypeFolio::Implantation); }},
        {QStringLiteral("rapport"), QStringLiteral("Folio de nomenclature"), QStringLiteral("dessiné d’office : le matériel regroupé par référence, avec les quantités"),
         [this] { ajouterFolio(TypeFolio::Nomenclature); }},
        {QStringLiteral("plan"), QStringLiteral("Nouveau folio libre"), QString(), [this] { ajouterFolio(TypeFolio::Libre); }}});
  gros(bS, QStringLiteral("Exporter"),
       {{QStringLiteral("exporter"), QStringLiteral("Folios en PDF…"), QStringLiteral("un folio par page, au format du folio, cartouche compris"),
         [this] {
           const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Folios en PDF"), q(projetCourant() ? projetCourant()->nom : std::string("schema")) + QStringLiteral(".pdf"),
                                                          QStringLiteral("PDF (*.pdf)"));
           if (!c.isEmpty()) m_etat->setText(exporterPdf(c) ? QStringLiteral("Folios exportés : %1").arg(c) : QStringLiteral("<span style='color:#C2402E'>Export impossible.</span>"));
         }},
        {QStringLiteral("dxf"), QStringLiteral("Folio en DXF…"), QStringLiteral("le folio affiché, pour AutoCAD, DraftSight, LibreCAD : calques cadre, fils, symboles, repères"),
         [this] {
           const ProjetElectrique* pe = projetCourant();
           if (!pe) return;
           const int n0 = folio();
           const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Folio en DXF"), q(pe->nom) + QStringLiteral("-folio-%1.dxf").arg(n0 + 1), QStringLiteral("DXF (*.dxf)"));
           if (c.isEmpty()) return;
           const int n = exporterDxf(c, n0);
           m_etat->setText(n >= 0 ? QStringLiteral("Folio %1 écrit en DXF (%2 entités) : %3").arg(n0 + 1).arg(n).arg(c)
                                  : QStringLiteral("<span style='color:#C2402E'>Écriture impossible (fichier ouvert ailleurs ?)</span>"));
         }},
        {QStringLiteral("dossier"), QStringLiteral("Dossier complet…"), QStringLiteral("folios en PDF et en DXF, tous les rapports en CSV (nomenclature chiffrée, liste de coupe…), plans de perçage"), [this] {
           const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Dossier du projet électrique"));
           if (d.isEmpty()) return;
           const QStringList f = exporterDossier(d);
           m_etat->setText(QStringLiteral("%1 fichier(s) écrit(s) dans %2").arg(f.size()).arg(d));
         }}});
  // ----- Armoire -----
  QToolBar* bA = onglet(QStringLiteral("Armoire"), Armoire);
  gros(bA, QStringLiteral("Implanter"),
       {{QStringLiteral("armoire"), QStringLiteral("Implanter ce qui manque"), QStringLiteral("les appareils pas encore placés vont dans la première place libre"), [this] { implanter(false); }},
        {QStringLiteral("armoire"), QStringLiteral("Tout réimplanter"), QStringLiteral("par famille : protection, alimentation, commande, automate ; borniers en bas"), [this] { implanter(true); }}},
       QStringLiteral("boutonImplanter"));
  gros(bA, QStringLiteral("Armoire"), {{QStringLiteral("armoire"), QStringLiteral("Nouvelle armoire"), QStringLiteral("plaque de fond équipée de rails et de goulottes"), [this] { nouvelleArmoire(); }}});
  gros(bA, QStringLiteral("Vue"),
       {{QStringLiteral("afficher"), QStringLiteral("Plaque de fond"), QString(), [this] { m_armoire->setPorte(false); }},
        {QStringLiteral("afficher"), QStringLiteral("Porte"), QStringLiteral("boutons, voyants, ventilation"), [this] { m_armoire->setPorte(true); }}});
  gros(bA, QStringLiteral("Retirer"), {{QStringLiteral("supprimer"), QStringLiteral("Retirer de l’armoire"), QStringLiteral("l’appareil reste au schéma (Suppr)"), [this] { m_armoire->retirerSelection(); }}});
  gros(bA, QStringLiteral("Perçage"), {{QStringLiteral("dxf"), QStringLiteral("Plan de perçage (DXF)…"), QStringLiteral("plaque, rails, goulottes, appareils, perçages"), [this] {
         const ProjetElectrique* pe = projetCourant();
         const int a = m_armoire->armoire();
         if (!pe || a < 0) return;
         const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Plan de perçage"), q(pe->armoires[static_cast<std::size_t>(a)].nom) + QStringLiteral(".dxf"), QStringLiteral("DXF (*.dxf)"));
         if (c.isEmpty()) return;
         QFile f(c);
         if (f.open(QIODevice::WriteOnly)) {
           f.write(QByteArray::fromStdString(dxfPlaque(*pe, static_cast<std::size_t>(a))));
           m_etat->setText(QStringLiteral("Plan de perçage écrit : %1").arg(c));
         }
       }}});
  // ----- Faisceau -----
  QToolBar* bF = onglet(QStringLiteral("Faisceau"), Faisceau);
  gros(bF, QStringLiteral("Faisceau"), {{QStringLiteral("faisceau"), QStringLiteral("Nouveau faisceau"), QString(), [this] { nouveauFaisceau(); }}});
  gros(bF, QStringLiteral("Nœud"), {{QStringLiteral("point"), QStringLiteral("Poser un nœud"), QStringLiteral("connecteur ou dérivation (N)"), [this] { m_faisceau->setOutil(VueFaisceau::Outil::Noeud); m_faisceau->setFocus(); }}});
  gros(bF, QStringLiteral("Branche"), {{QStringLiteral("cablage"), QStringLiteral("Tirer une branche"), QStringLiteral("d’un nœud à l’autre (B)"), [this] { m_faisceau->setOutil(VueFaisceau::Outil::Branche); m_faisceau->setFocus(); }}});
  gros(bF, QStringLiteral("Choisir"), {{QStringLiteral("selection"), QStringLiteral("Choisir et déplacer"), QStringLiteral("Échap"), [this] { m_faisceau->setOutil(VueFaisceau::Outil::Selection); }}});
  gros(bF, QStringLiteral("Supprimer"), {{QStringLiteral("supprimer"), QStringLiteral("Supprimer"), QStringLiteral("nœud ou branche (Suppr)"), [this] { m_faisceau->supprimerSelection(); }}});
  // ----- Grafcet -----
  QToolBar* bG = onglet(QStringLiteral("Grafcet"), Grafcet);
  {
    std::vector<V> nouveaux{{QStringLiteral("grafcet"), QStringLiteral("Grafcet vierge"), QStringLiteral("l’étape initiale 0, à compléter"), [this] { nouveauGrafcet(); }}};
    for (const auto& [id, nom] : exemplesGrafcet()) {
      const std::string ex = id;
      nouveaux.push_back({QStringLiteral("apprendre"), QStringLiteral("Exemple : %1").arg(q(nom)), QStringLiteral("un grafcet de cours à simuler et à modifier"),
                          [this, ex] { nouveauGrafcet(ex); }});
    }
    gros(bG, QStringLiteral("Nouveau"), nouveaux, QStringLiteral("boutonNouveauGrafcet"))->setPopupMode(QToolButton::InstantPopup);
  }
  gros(bG, QStringLiteral("Étape"),
       {{QStringLiteral("grafcet"), QStringLiteral("Étape suivante"), QStringLiteral("après l’étape ou la transition choisie, avec sa transition"), [this] { etapeSuivanteGrafcet(); }}},
       QStringLiteral("boutonEtapeSuivante"));
  gros(bG, QStringLiteral("Branche"),
       {{QStringLiteral("grafcet"), QStringLiteral("Choix en OU"), QStringLiteral("un autre chemin depuis l’étape choisie"), [this] { brancheGrafcet(false); }},
        {QStringLiteral("grafcet"), QStringLiteral("Parallèle en ET"), QStringLiteral("une séquence de plus, depuis la transition choisie"), [this] { brancheGrafcet(true); }}},
       QStringLiteral("boutonBrancheGrafcet"));
  {
    // Retour vers une étape : la liste des étapes du grafcet, à l'ouverture du menu
    auto* t = new QToolButton;
    t->setObjectName(QStringLiteral("boutonRetourGrafcet"));
    t->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    t->setIconSize(QSize(30, 30));
    t->setAutoRaise(true);
    t->setText(QStringLiteral("Retour"));
    t->setIcon(icone(QStringLiteral("annuler-fleche")));
    t->setToolTip(QStringLiteral("Retour de l’étape choisie vers une étape existante (fermer le cycle)"));
    t->setMinimumWidth(62);
    t->setPopupMode(QToolButton::InstantPopup);
    auto* m = new QMenu(t);
    connect(m, &QMenu::aboutToShow, this, [this, m] {
      m->clear();
      const prom::elec::Grafcet* g = grafcetChoisi();
      if (!g || m_etapeGrafcet < 0) {
        m->addAction(QStringLiteral("Choisis d’abord l’étape de départ"))->setEnabled(false);
        return;
      }
      for (const auto& e : g->etapes) {
        const int vers = e.numero;
        m->addAction(QStringLiteral("Vers l’étape %1%2").arg(vers).arg(e.initiale ? QStringLiteral(" (initiale)") : QString()), this, [this, vers] { bouclerGrafcet(vers); });
      }
    });
    t->setMenu(m);
    bG->addWidget(t);
  }
  gros(bG, QStringLiteral("Supprimer"), {{QStringLiteral("supprimer"), QStringLiteral("Supprimer"), QStringLiteral("l’étape ou la transition choisie (Ctrl+Z la rend)"), [this] { supprimerDansGrafcet(); }}});
  bG->addSeparator();
  gros(bG, QStringLiteral("Simuler"),
       {{QStringLiteral("simuler"), QStringLiteral("Simuler"), QStringLiteral("étapes actives, entrées à cocher, temps qui passe ; un autre clic arrête"), [this] { simulerGrafcet(!m_simulation); }}},
       QStringLiteral("boutonSimuler"));
  gros(bG, QStringLiteral("Folio"), {{QStringLiteral("plan"), QStringLiteral("Dessiner sur un folio"), QStringLiteral("le grafcet normalisé, tenu à jour, dans le PDF et le DXF des folios"), [this] {
         const int i = dessinerFolioGrafcet();
         if (i >= 0) {
           choisirMode(Schema);
           allerAuFolio(i);
         }
       }}});
  gros(bG, QStringLiteral("Automate"),
       {{QStringLiteral("code"), QStringLiteral("Copier le texte structuré"), QStringLiteral("CEI 61131-3 : à coller dans CODESYS, TIA Portal (SCL), Control Expert"), [this] {
          if (const prom::elec::Grafcet* g = grafcetChoisi()) {
            QApplication::clipboard()->setText(q(texteStructureGrafcet(*g)));
            m_etat->setText(QStringLiteral("Programme en texte structuré copié."));
          }
        }},
        {QStringLiteral("exporter"), QStringLiteral("Enregistrer en .st…"), QStringLiteral("fichier texte structuré du grafcet"), [this] {
          const prom::elec::Grafcet* g = grafcetChoisi();
          if (!g) return;
          const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Programme du grafcet"), q(g->nom) + QStringLiteral(".st"), QStringLiteral("Texte structuré (*.st *.txt)"));
          if (!c.isEmpty()) m_etat->setText(exporterTexteStructure(c) ? QStringLiteral("Programme écrit : %1").arg(c) : QStringLiteral("<span style='color:#C2402E'>Écriture impossible.</span>"));
        }}},
       QStringLiteral("boutonTexteStructure"));
  apprendre(bG);
  // ----- Rapports -----
  QToolBar* bR = onglet(QStringLiteral("Rapports"), Rapports);
  for (int i : {0, 1, 2, 3, 5, 6})
    gros(bR, nomRapport(i), {{iconeRapport(i), nomRapport(i), QString(), [this, i] { montrerRapport(i); }}});
  // Fabrication et bilans : un bouton chacun, leurs rapports dans la liste
  gros(bR, QStringLiteral("Câblage"),
       {{iconeRapport(4), nomRapport(4), QStringLiteral("de borne à borne, avec section, couleur et longueur"), [this] { montrerRapport(4); }},
        {iconeRapport(8), nomRapport(8), QStringLiteral("les fils à couper, longueur, embouts, filerie à commander"), [this] { montrerRapport(8); }}});
  gros(bR, QStringLiteral("Bilans"),
       {{iconeRapport(7), nomRapport(7), QStringLiteral("puissance installée et absorbée, courant"), [this] { montrerRapport(7); }},
        {iconeRapport(9), nomRapport(9), QStringLiteral("matériel, filerie, main d’œuvre : le prix du tableau"), [this] { montrerRapport(9); }}});
  // ----- Calculs -----
  QToolBar* bC = onglet(QStringLiteral("Calculs"), Calculs);
  {
    // Un bouton par famille de calculs, chacun avec la liste de ses fiches
    const std::vector<std::pair<std::string, QString>> familles = {{"Câbles et protections", QStringLiteral("Câbles")},
                                                                   {"Puissance et réseau", QStringLiteral("Réseau")},
                                                                   {"Terre et sécurité", QStringLiteral("Terre, armoire")},
                                                                   {"Éclairage", QStringLiteral("Éclairage")},
                                                                   {"Bases et conversions", QStringLiteral("Bases")}};
    for (const auto& [famille, titreFamille] : familles) {
      std::vector<V> fiches;
      for (const auto& c : calculateursElectriques())
        if (c.famille == famille) {
          const std::string id = c.id;
          fiches.push_back({QStringLiteral("calcul"), q(c.nom), q(c.description), [this, id] { choisirCalcul(id); }});
        }
      if (!fiches.empty()) gros(bC, titreFamille, fiches)->setPopupMode(QToolButton::InstantPopup);
    }
  }
  // ----- Logement -----
  QToolBar* bL = onglet(QStringLiteral("Logement"), Logement);
  gros(bL, QStringLiteral("Tableau"), {{QStringLiteral("logement"), QStringLiteral("Tableau de logement"), QStringLiteral("NF C 15-100 : circuits, différentiels, coffret"), [this] { majLogement(); }}});
  gros(bL, QStringLiteral("Logement type"), {{QStringLiteral("plan"), QStringLiteral("Logement type…"), QStringLiteral("surface et nombre de chambres : les pièces se remplissent"), [this] {
         if (auto* b = m_pageLogement->findChild<QPushButton*>(QStringLiteral("logementType"))) b->click();
       }}});
  gros(bL, QStringLiteral("Créer"), {{QStringLiteral("valider"), QStringLiteral("Créer le schéma unifilaire et le coffret"), QStringLiteral("remplace le schéma du projet ; Ctrl+Z revient en arrière"), [this] {
         if (auto* b = m_pageLogement->findChild<QPushButton*>(QStringLiteral("genererLogement"))) b->click();
       }}});
  apprendre(bL);
  // ----- Distribution -----
  QToolBar* bD = onglet(QStringLiteral("Distribution"), Distribution);
  {
    std::vector<V> sources;
    for (TypeSourceBT t : {TypeSourceBT::Transformateur, TypeSourceBT::ReseauPublic, TypeSourceBT::BranchementLimite, TypeSourceBT::Groupe})
      sources.push_back({QStringLiteral("transformateur"), QString::fromUtf8(nomTypeSource(t)), QStringLiteral("puis ses caractéristiques dans le panneau de droite"), [this, t] {
                           modifierReseau([t](ReseauBT& r) {
                             r.source.type = t;
                             if (t == TypeSourceBT::BranchementLimite || t == TypeSourceBT::ReseauPublic) r.source.regime = RegimeNeutre::TT;
                             if (t == TypeSourceBT::BranchementLimite) r.source.puissance = std::min(r.source.puissance, 36.0);
                             if (t == TypeSourceBT::ReseauPublic) r.source.puissance = std::clamp(r.source.puissance, 36.0, 250.0);
                           });
                           choisirDepart({});
                         }});
    gros(bD, QStringLiteral("Source"), sources, QStringLiteral("boutonSource"))->setPopupMode(QToolButton::InstantPopup);
  }
  gros(bD, QStringLiteral("Tableau"),
       {{QStringLiteral("armoire"), QStringLiteral("Ajouter un tableau"), QStringLiteral("tableau divisionnaire alimenté par le tableau choisi"),
         [this] { ajouterDepart(TypeCharge::Tableau); }}},
       QStringLiteral("boutonTableau"));
  {
    std::vector<V> departs;
    const std::pair<TypeCharge, const char*> types[] = {{TypeCharge::Eclairage, "eclairage"}, {TypeCharge::Prises, "prise"},        {TypeCharge::Chauffage, "chauffage"},
                                                         {TypeCharge::Moteur, "moteur"},       {TypeCharge::IRVE, "irve"},          {TypeCharge::Condensateurs, "condensateur"},
                                                         {TypeCharge::Autre, "disjoncteur"}};
    for (const auto& [t, ic] : types)
      departs.push_back({QString::fromLatin1(ic), QString::fromUtf8(nomTypeCharge(t)), QStringLiteral("départ ajouté au tableau choisi, aux valeurs usuelles"),
                         [this, t = t] { ajouterDepart(t); }});
    gros(bD, QStringLiteral("Départ"), departs, QStringLiteral("boutonDepart"))->setPopupMode(QToolButton::InstantPopup);
  }
  gros(bD, QStringLiteral("Supprimer"),
       {{QStringLiteral("supprimer"), QStringLiteral("Supprimer le départ choisi"), QStringLiteral("un tableau part avec ses départs (Ctrl+Z revient)"), [this] { supprimerDepartChoisi(); }}});
  bD->addSeparator();
  {
    std::vector<V> modeles;
    for (ModeleReseau m : {ModeleReseau::Vide, ModeleReseau::Tertiaire, ModeleReseau::Atelier, ModeleReseau::ParkingIRVE, ModeleReseau::Maison})
      modeles.push_back({QStringLiteral("plan"), QString::fromUtf8(nomModeleReseau(m)), QStringLiteral("remplace l’installation (Ctrl+Z revient)"), [this, m] { appliquerModele(m); }});
    gros(bD, QStringLiteral("Modèle"), modeles, QStringLiteral("boutonModele"))->setPopupMode(QToolButton::InstantPopup);
  }
  gros(bD, QStringLiteral("Unifilaire"),
       {{QStringLiteral("schema"), QStringLiteral("Dessiner l’unifilaire"), QStringLiteral("un folio par tableau, annoté des résultats ; redessiné à chaque fois"), [this] {
          const int f = genererUnifilaire();
          if (f >= 0) {
            choisirMode(Schema);
            allerAuFolio(f);
          }
        }}},
       QStringLiteral("boutonUnifilaire"));
  gros(bD, QStringLiteral("Note de calcul"),
       {{QStringLiteral("exporter"), QStringLiteral("Note de calcul en PDF…"), QStringLiteral("source, bilan, départs, justifications, avis, hypothèses"),
         [this] {
           const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Note de calcul"), q(projetCourant() ? projetCourant()->nom : std::string("installation")) + QStringLiteral(" - note de calcul.pdf"),
                                                          QStringLiteral("PDF (*.pdf);;Page web (*.html)"));
           if (!c.isEmpty()) m_etat->setText(exporterNoteDeCalcul(c) ? QStringLiteral("Note de calcul écrite : %1").arg(c) : QStringLiteral("<span style='color:#C2402E'>Écriture impossible.</span>"));
         }},
        {QStringLiteral("rapport"), QStringLiteral("Tableau des départs en CSV…"), QStringLiteral("toutes les grandeurs, un départ par ligne (tableur)"), [this] {
           const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Note de calcul (tableur)"), QStringLiteral("note-de-calcul.csv"), QStringLiteral("CSV (*.csv)"));
           if (!c.isEmpty()) m_etat->setText(exporterNoteDeCalcul(c) ? QStringLiteral("Tableau écrit : %1").arg(c) : QStringLiteral("<span style='color:#C2402E'>Écriture impossible.</span>"));
         }}},
       QStringLiteral("boutonNoteCalcul"));
  apprendre(bD);
  // ----- Photovoltaïque -----
  QToolBar* bP = onglet(QStringLiteral("Photovoltaïque"), Photovoltaique);
  {
    std::vector<V> tailles;
    for (double k : {3.0, 6.0, 9.0, 36.0, 100.0, 250.0})
      tailles.push_back({QStringLiteral("photovoltaique"), QStringLiteral("%1 kWc").arg(QLocale().toString(k, 'f', 0)), QStringLiteral("installation type : modules, onduleur, site (Ctrl+Z revient)"),
                         [this, k] { appliquerPV(k); }});
    gros(bP, QStringLiteral("Installation"), tailles, QStringLiteral("boutonInstallationPV"))->setPopupMode(QToolButton::InstantPopup);
    std::vector<V> modules;
    for (std::size_t k = 0; k < modulesPV().size(); ++k)
      modules.push_back({QStringLiteral("photovoltaique"), q(modulesPV()[k].nom), QStringLiteral("puis ses valeurs à droite, d’après sa fiche technique"),
                         [this, k] { modifierPV([k](InstallationPV& z) { z.module = modulesPV()[k]; }); }});
    gros(bP, QStringLiteral("Module"), modules, QStringLiteral("boutonModulePV"))->setPopupMode(QToolButton::InstantPopup);
    std::vector<V> onduleurs;
    for (std::size_t k = 0; k < onduleursPV().size(); ++k)
      onduleurs.push_back({QStringLiteral("disjoncteur"), q(onduleursPV()[k].nom), QStringLiteral("puis ses valeurs à droite, d’après sa notice"),
                           [this, k] { modifierPV([k](InstallationPV& z) { z.onduleur = onduleursPV()[k]; }); }});
    gros(bP, QStringLiteral("Onduleur"), onduleurs, QStringLiteral("boutonOnduleurPV"))->setPopupMode(QToolButton::InstantPopup);
  }
  bP->addSeparator();
  gros(bP, QStringLiteral("Unifilaire"),
       {{QStringLiteral("schema"), QStringLiteral("Dessiner l’unifilaire"), QStringLiteral("chaînes, protections, onduleurs, branchement ; redessiné à chaque fois"), [this] {
          const int f = genererUnifilairePV();
          if (f >= 0) {
            choisirMode(Schema);
            allerAuFolio(f);
          }
        }}},
       QStringLiteral("boutonUnifilairePV"));
  gros(bP, QStringLiteral("Note de calcul"),
       {{QStringLiteral("exporter"), QStringLiteral("Note de calcul en PDF…"), QStringLiteral("matériel, calepinage, côtés continu et alternatif, production, avis"), [this] {
          const QString c = QFileDialog::getSaveFileName(this, QStringLiteral("Note de calcul photovoltaïque"),
                                                         q(projetCourant() ? projetCourant()->nom : std::string("photovoltaique")) + QStringLiteral(" - note de calcul.pdf"),
                                                         QStringLiteral("PDF (*.pdf);;Page web (*.html)"));
          if (!c.isEmpty()) m_etat->setText(exporterNotePV(c) ? QStringLiteral("Note de calcul écrite : %1").arg(c) : QStringLiteral("<span style='color:#C2402E'>Écriture impossible.</span>"));
        }}},
       QStringLiteral("boutonNotePV"));
  apprendre(bP);
  // Onglets dans l'ordre du travail
  for (int m : {static_cast<int>(Schema), static_cast<int>(Armoire), static_cast<int>(Faisceau), static_cast<int>(Grafcet), static_cast<int>(Distribution),
                static_cast<int>(Photovoltaique), static_cast<int>(Logement), static_cast<int>(Calculs), static_cast<int>(Rapports)})
    for (const auto& [id, o] : onglets)
      if (id == m) {
        m_ruban->addTab(o.first, o.second);
        m_ordreModes.push_back(m);
      }
}

// ---------- Colonne de gauche ----------
QWidget* AtelierElectrique::construireGauche() {
  auto* pile = new QStackedWidget;
  m_pileGauche = pile;
  // Schéma : folios et palette
  {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(8, 6, 6, 6);
    col->setSpacing(4);
    auto* tete = new QHBoxLayout;
    tete->setSpacing(2);
    tete->addWidget(titre(QStringLiteral("Folios")));
    tete->addStretch(1);
    auto* plus = new QToolButton;
    plus->setObjectName(QStringLiteral("ajouterFolio"));
    plus->setText(QStringLiteral("+"));
    plus->setToolTip(QStringLiteral("Nouveau folio"));
    plus->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(plus);
    for (TypeFolio t : {TypeFolio::Puissance, TypeFolio::Commande, TypeFolio::Automate, TypeFolio::Distribution, TypeFolio::Unifilaire, TypeFolio::Borniers, TypeFolio::Implantation,
                        TypeFolio::Nomenclature, TypeFolio::Grafcet, TypeFolio::Cablage, TypeFolio::Garde, TypeFolio::Libre})
      menu->addAction(icone(iconeFolio(t)), QString::fromUtf8(nomTypeFolio(t)), this, [this, t] { ajouterFolio(t); });
    plus->setMenu(menu);
    tete->addWidget(plus);
    for (auto [texte, sens] : {std::pair{"▲", -1}, std::pair{"▼", 1}}) {
      auto* b = new QToolButton;
      b->setText(QString::fromUtf8(texte));
      b->setToolTip(sens < 0 ? QStringLiteral("Monter le folio") : QStringLiteral("Descendre le folio"));
      connect(b, &QToolButton::clicked, this, [this, sens = sens] {
        const int f = folio(), j = f + sens;
        const ProjetElectrique* pe = projetCourant();
        if (!pe || j < 0 || j >= static_cast<int>(pe->schema.feuilles.size())) return;
        modifierProjet([&](ProjetElectrique& x) { std::swap(x.schema.feuilles[static_cast<std::size_t>(f)], x.schema.feuilles[static_cast<std::size_t>(j)]); });
        allerAuFolio(j);
      });
      tete->addWidget(b);
    }
    auto* moins = new QToolButton;
    moins->setIcon(icone(QStringLiteral("supprimer")));
    moins->setToolTip(QStringLiteral("Supprimer le folio"));
    connect(moins, &QToolButton::clicked, this, [this] {
      const ProjetElectrique* pe = projetCourant();
      const int f = folio();
      if (!pe || f < 0 || f >= static_cast<int>(pe->schema.feuilles.size())) return;
      if (pe->schema.feuilles.size() < 2) {
        m_etat->setText(QStringLiteral("Le dernier folio ne se supprime pas."));
        return;
      }
      // Pas de question : la suppression s'annule (Ctrl+Z), la barre d'état dit ce qui est parti
      const auto& F = pe->schema.feuilles[static_cast<std::size_t>(f)];
      const QString bilan = QStringLiteral("Folio « %1 » supprimé (%2 symbole(s), %3 fil(s)) — Ctrl+Z le rétablit.").arg(q(F.nom).toHtmlEscaped()).arg(F.symboles.size()).arg(F.fils.size());
      modifierProjet([&](ProjetElectrique& x) { supprimerFolio(x, static_cast<std::size_t>(f)); });
      allerAuFolio(std::max(0, f - 1));
      m_etat->setText(bilan);
    });
    tete->addWidget(moins);
    // Boutons de la liste des folios serrés : la colonne reste étroite
    for (int i = 0; i < tete->count(); ++i)
      if (auto* b = qobject_cast<QToolButton*>(tete->itemAt(i)->widget())) {
        b->setAutoRaise(true);
        b->setFixedHeight(26);
        b->setMinimumWidth(26);
      }
    col->addLayout(tete);
    m_folios = new QListWidget;
    m_folios->setObjectName(QStringLiteral("foliosElectriques"));
    m_folios->setIconSize(QSize(20, 20));
    m_folios->setMaximumHeight(190);
    connect(m_folios, &QListWidget::currentRowChanged, this, [this](int r) {
      if (r >= 0 && r != folio()) {
        m_schema->setConception(m_projet, r);
        majPanneau();
      }
    });
    // Double-clic : le titre du folio se modifie dans le panneau de droite, prêt à la frappe
    connect(m_folios, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
      const int r = m_folios->row(it);
      if (!projetCourant() || r < 0) return;
      if (mode() != Schema) choisirMode(Schema);
      if (r != folio()) allerAuFolio(r);
      m_schema->selectionner({});
      majPanneau();
      if (QWidget* contenu = m_panneau->widget())
        if (auto* titre = contenu->findChild<QLineEdit*>(QStringLiteral("titreFolio"))) {
          titre->setFocus(Qt::MouseFocusReason);
          titre->selectAll();
        }
    });
    col->addWidget(m_folios);
    col->addWidget(titre(QStringLiteral("Appareils et symboles")));
    m_recherche = new QLineEdit;
    m_recherche->setObjectName(QStringLiteral("rechercheSymbole"));
    m_recherche->setPlaceholderText(QStringLiteral("Chercher : contacteur, moteur, KM1…"));
    m_recherche->setToolTip(QStringLiteral("Un symbole de la bibliothèque, un circuit type, ou un appareil déjà posé (repère, fonction)"));
    m_recherche->setClearButtonEnabled(true);
    connect(m_recherche, &QLineEdit::textChanged, this, [this] { majPalette(); });
    connect(m_recherche, &QLineEdit::returnPressed, this, [this] {
      // Entrée : le premier symbole trouvé est prêt à poser
      for (QTreeWidgetItemIterator it(m_palette, QTreeWidgetItemIterator::NotHidden); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == QLatin1String("symbole")) {
          choisirSymbole(s((*it)->data(0, Qt::UserRole + 1).toString()));
          m_schema->setFocus();
          return;
        }
    });
    col->addWidget(m_recherche);
    m_palette = new QTreeWidget;
    m_palette->setObjectName(QStringLiteral("paletteElectrique"));
    m_palette->setHeaderHidden(true);
    m_palette->setIconSize(QSize(48, 33));
    m_palette->setIndentation(12);
    m_palette->setAnimated(true);
    connect(m_palette, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
      const QString genre = it->data(0, Qt::UserRole).toString();
      const QString id = it->data(0, Qt::UserRole + 1).toString();
      if (genre == QLatin1String("symbole")) choisirSymbole(s(id));
      else if (genre == QLatin1String("circuit")) {
        insererCircuit(it->data(0, Qt::UserRole + 1).toInt());
        return;
      } else if (genre == QLatin1String("appareil")) {
        montrerAppareil(s(id));
        return;
      } else if (genre == QLatin1String("potentiel")) choisirPotentiel(id);
      else if (genre == QLatin1String("nouvelAppareil")) {
        nouvelAppareil();
        return;
      } else if (genre == QLatin1String("outil")) {
        if (id == QLatin1String("fil")) m_schema->setOutil(VueSchema::Outil::Fil);
        else if (id == QLatin1String("texte")) m_schema->setOutil(VueSchema::Outil::Texte);
        else if (id == QLatin1String("nc")) m_schema->setOutil(VueSchema::Outil::NonConnexion);
        majPanneau();
      } else
        return;
      m_schema->setFocus();
    });
    m_palette->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_palette, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& ou) { menuPalette(ou); });
    col->addWidget(m_palette, 1);
    pile->addWidget(w);
    m_pagesGauche[Schema] = w;
  }
  // Armoire : armoires du projet
  {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(8, 6, 6, 6);
    col->addWidget(titre(QStringLiteral("Armoires")));
    m_armoires = new QListWidget;
    m_armoires->setObjectName(QStringLiteral("listeArmoires"));
    connect(m_armoires, &QListWidget::currentRowChanged, this, [this](int r) {
      if (r >= 0 && r != m_armoire->armoire()) {
        m_armoire->setArmoire(m_projet, r);
        majPanneau();
      }
    });
    col->addWidget(m_armoires);
    auto* b = new QPushButton(icone(QStringLiteral("armoire")), QStringLiteral("Nouvelle armoire"));
    connect(b, &QPushButton::clicked, this, [this] { nouvelleArmoire(); });
    col->addWidget(b);
    auto* aide = new QLabel(QStringLiteral("<span style='color:#6B7393'>Les appareils viennent du schéma. Glisse-les sur la plaque : ils s’accrochent au rail. "
                                           "La molette zoome, le clic droit déplace la vue.</span>"));
    aide->setWordWrap(true);
    col->addWidget(aide);
    col->addStretch(1);
    pile->addWidget(w);
    m_pagesGauche[Armoire] = w;
  }
  // Faisceau
  {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(8, 6, 6, 6);
    col->addWidget(titre(QStringLiteral("Faisceaux")));
    m_faisceaux = new QListWidget;
    m_faisceaux->setObjectName(QStringLiteral("listeFaisceaux"));
    connect(m_faisceaux, &QListWidget::currentRowChanged, this, [this](int r) {
      if (r >= 0 && r != m_faisceau->faisceau()) {
        m_faisceau->setFaisceau(m_projet, r);
        majPanneau();
      }
    });
    col->addWidget(m_faisceaux);
    auto* b = new QPushButton(icone(QStringLiteral("faisceau")), QStringLiteral("Nouveau faisceau"));
    connect(b, &QPushButton::clicked, this, [this] { nouveauFaisceau(); });
    col->addWidget(b);
    auto* aide = new QLabel(QStringLiteral("<span style='color:#6B7393'>Pose des nœuds (N), relie-les par des branches (B), puis donne à chaque nœud d’extrémité "
                                           "son connecteur : la longueur de chaque fil se calcule, surlongueurs comprises.</span>"));
    aide->setWordWrap(true);
    col->addWidget(aide);
    col->addStretch(1);
    pile->addWidget(w);
    m_pagesGauche[Faisceau] = w;
  }
  // Rapports
  {
    auto* w = new QWidget;
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(8, 6, 6, 6);
    col->addWidget(titre(QStringLiteral("Rapports")));
    m_rapports = new QListWidget;
    m_rapports->setObjectName(QStringLiteral("listeRapports"));
    for (int i = 0; i < NB_RAPPORTS; ++i) m_rapports->addItem(new QListWidgetItem(icone(iconeRapport(i)), nomRapport(i)));
    connect(m_rapports, &QListWidget::currentRowChanged, this, [this](int r) {
      if (r >= 0 && r != m_rapport) montrerRapport(r);
    });
    col->addWidget(m_rapports, 1);  // tous les rapports visibles d'un coup
    pile->addWidget(w);
    m_pagesGauche[Rapports] = w;
  }
  return pile;
}

void AtelierElectrique::majFolios() {
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return;
  {
    const QSignalBlocker b(m_folios);
    m_folios->clear();
    for (std::size_t i = 0; i < pe->schema.feuilles.size(); ++i) {
      const auto& f = pe->schema.feuilles[i];
      const TypeFolio t = pe->folio(f.uuid).type;
      auto* it = new QListWidgetItem(icone(iconeFolio(t)), QStringLiteral("%1 · %2").arg(i + 1).arg(q(f.nom)));
      it->setToolTip(QStringLiteral("%1 — %2, %3 symbole(s), %4 fil(s) · double-clic : renommer").arg(QString::fromUtf8(nomTypeFolio(t)), q(f.format)).arg(f.symboles.size()).arg(f.fils.size()));
      m_folios->addItem(it);
    }
    m_folios->setCurrentRow(folio());
  }
  {
    const QSignalBlocker b(m_armoires);
    m_armoires->clear();
    for (const auto& a : pe->armoires) m_armoires->addItem(new QListWidgetItem(icone(QStringLiteral("armoire")), q(a.nom)));
    m_armoires->setCurrentRow(m_armoire->armoire());
  }
  {
    const QSignalBlocker b(m_faisceaux);
    m_faisceaux->clear();
    for (const auto& f : pe->faisceaux) m_faisceaux->addItem(new QListWidgetItem(icone(QStringLiteral("faisceau")), q(f.nom)));
    m_faisceaux->setCurrentRow(m_faisceau->faisceau());
  }
  {
    const QSignalBlocker b(m_rapports);
    m_rapports->setCurrentRow(m_rapport);
  }
}

void AtelierElectrique::majPalette() {
  const QString filtre = m_recherche ? m_recherche->text().trimmed().toLower() : QString();
  m_palette->clear();
  auto correspond = [&](const QString& t) {
    if (filtre.isEmpty()) return true;
    for (const QString& mot : filtre.split(QLatin1Char(' '), Qt::SkipEmptyParts))
      if (!t.toLower().contains(mot)) return false;
    return true;
  };
  auto famille = [&](const QString& nom, const QString& ic) {
    auto* f = new QTreeWidgetItem(m_palette);
    f->setText(0, nom);
    f->setIcon(0, icone(ic));
    QFont fo = f->font(0);
    fo.setBold(true);
    f->setFont(0, fo);
    f->setFlags(Qt::ItemIsEnabled);
    return f;
  };
  // Potentiels et renvois, puis outils
  {
    QTreeWidgetItem* f = famille(QStringLiteral("Potentiels et renvois"), QStringLiteral("potentiel"));
    const std::pair<const char*, const char*> pots[] = {{"L1", "phase 1"}, {"L2", "phase 2"}, {"L3", "phase 3"}, {"N", "neutre"}, {"PE", "protection, vert-jaune"},
                                                        {"24V", "commande, + 24 V"}, {"0V", "commande, 0 V"}, {"", "autre potentiel ou renvoi de folio…"}};
    for (const auto& [n, d] : pots) {
      const QString texte = *n ? QStringLiteral("%1 — %2").arg(QString::fromUtf8(n), QString::fromUtf8(d)) : QString::fromUtf8(d);
      if (!correspond(texte + QStringLiteral(" potentiel renvoi"))) continue;
      auto* it = new QTreeWidgetItem(f);
      it->setText(0, texte);
      it->setIcon(0, icone(QStringLiteral("potentiel")));
      it->setData(0, Qt::UserRole, QStringLiteral("potentiel"));
      it->setData(0, Qt::UserRole + 1, QString::fromUtf8(n));
      it->setToolTip(0, QStringLiteral("Posé au bout d’un fil. Le même nom posé ailleurs relie les fils et fait le renvoi de folio."));
    }
    if (f->childCount() == 0) delete f;
  }
  // Recherche d'un appareil déjà posé (« KM1 », « pompe ») : un clic le montre sur son folio
  if (const ProjetElectrique* pe = projetCourant(); pe && !filtre.isEmpty()) {
    QTreeWidgetItem* f = famille(QStringLiteral("Dans ce projet"), QStringLiteral("selection"));
    int n = 0;
    for (const auto& c : pe->schema.composants) {
      const QString repere = q(repereAffiche(*pe, c));
      const QString fonction = q(c.champs.count("fonction") ? c.champs.at("fonction") : std::string());
      if (!correspond(repere + QStringLiteral(" ") + q(c.reference) + QStringLiteral(" ") + q(c.description) + QStringLiteral(" ") + fonction)) continue;
      if (++n > 25) break;
      auto* it = new QTreeWidgetItem(f);
      it->setText(0, QStringLiteral("%1 — %2").arg(repere, fonction.isEmpty() ? q(c.description) : fonction));
      it->setIcon(0, icone(QStringLiteral("selection")));
      it->setToolTip(0, QStringLiteral("Montrer %1 sur son folio").arg(repere));
      it->setData(0, Qt::UserRole, QStringLiteral("appareil"));
      it->setData(0, Qt::UserRole + 1, q(c.uuid));
    }
    if (f->childCount() == 0) delete f;
  }
  {
    // Circuits types, prêts à poser câblés et repérés
    QTreeWidgetItem* f = famille(QStringLiteral("Circuits types (câblés, prêts à poser)"), QStringLiteral("moteur"));
    const auto& liste = circuitsTypes();
    for (std::size_t i = 0; i < liste.size(); ++i) {
      if (!correspond(q(liste[i].nom + " " + liste[i].description) + QStringLiteral(" circuit type schéma"))) continue;
      auto* it = new QTreeWidgetItem(f);
      it->setText(0, q(liste[i].nom));
      it->setIcon(0, icone(liste[i].batiment ? QStringLiteral("logement") : QStringLiteral("moteur")));
      it->setToolTip(0, QStringLiteral("<b>%1</b><br>%2<br><i>Un clic le pose sur le folio qui convient, à droite de ce qui est dessiné.</i>").arg(q(liste[i].nom).toHtmlEscaped(), q(liste[i].description).toHtmlEscaped()));
      it->setData(0, Qt::UserRole, QStringLiteral("circuit"));
      it->setData(0, Qt::UserRole + 1, static_cast<int>(i));
    }
    if (f->childCount() == 0) delete f;
  }
  for (const std::string& nomFamille : famillesElectriques()) {
    QTreeWidgetItem* f = nullptr;
    for (const auto& d : bibliothequeElectrique()) {
      if (d.categorie != nomFamille) continue;
      if (!correspond(q(d.nom + " " + d.description + " " + d.motsCles))) continue;
      if (!f) f = famille(q(nomFamille), QStringLiteral("dossier"));
      auto* it = new QTreeWidgetItem(f);
      it->setText(0, q(d.nom));
      it->setIcon(0, iconeSymbole(d));
      QString bulle = QStringLiteral("<b>%1</b><br>%2").arg(q(d.nom).toHtmlEscaped(), q(d.description).toHtmlEscaped());
      if (const AideSymbole* a = aideSymbole(d.id))
        bulle += QStringLiteral("<p style='margin-top:6px'><b>C’est quoi ?</b> %1</p><p><b>À savoir :</b> %2</p>").arg(q(a->quoi).toHtmlEscaped(), q(a->usage).toHtmlEscaped());
      it->setToolTip(0, QStringLiteral("<div style='max-width:360px'>%1</div>").arg(bulle));
      it->setData(0, Qt::UserRole, QStringLiteral("symbole"));
      it->setData(0, Qt::UserRole + 1, q(d.id));
    }
  }
  {
    // Mes appareils : « Nouvel appareil… », puis ceux du projet et ceux de la bibliothèque de l'utilisateur
    QTreeWidgetItem* f = famille(QStringLiteral("Mes appareils"), QStringLiteral("appareil-perso"));
    if (correspond(QStringLiteral("nouvel appareil personnel créer symbole personnalisé autre manque bornes mes appareils"))) {
      auto* it = new QTreeWidgetItem(f);
      it->setText(0, QStringLiteral("Nouvel appareil…"));
      it->setIcon(0, icone(QStringLiteral("appareil-perso")));
      it->setToolTip(0, QStringLiteral("<div style='max-width:340px'><b>Un appareil manque ?</b><br>Relais de sécurité, régulateur, module, appareil maison : donne son nom et "
                                       "ses bornes, le symbole se dessine tout seul et sert dans tous tes projets.</div>"));
      it->setData(0, Qt::UserRole, QStringLiteral("nouvelAppareil"));
      QFont fo = it->font(0);
      fo.setItalic(true);
      it->setFont(0, fo);
    }
    std::vector<const DefSymbole*> liste;
    std::set<std::string> vus;
    const ProjetElectrique* pe = projetCourant();
    if (pe)
      for (const DefSymbole* d : appareilsPersonnels(*pe))
        if (vus.insert(d->id).second) liste.push_back(d);
    for (const auto& d : m_mesAppareils)
      if (vus.insert(d.id).second) liste.push_back(&d);
    std::stable_sort(liste.begin(), liste.end(), [](const DefSymbole* a, const DefSymbole* b) { return a->nom < b->nom; });
    for (const DefSymbole* d : liste) {
      if (!correspond(q(d->nom + " " + d->description + " " + d->motsCles) + QStringLiteral(" mes appareils personnel"))) continue;
      const bool range = std::any_of(m_mesAppareils.begin(), m_mesAppareils.end(), [&](const DefSymbole& x) { return x.id == d->id; });
      const SpecAppareil a = specAppareil(*d);
      QString bornes;
      for (const auto& [cote, liste2] : {std::pair{QStringLiteral("en haut"), &a.haut}, std::pair{QStringLiteral("en bas"), &a.bas},
                                         std::pair{QStringLiteral("à gauche"), &a.gauche}, std::pair{QStringLiteral("à droite"), &a.droite}})
        if (!liste2->empty()) bornes += QStringLiteral("<br>%1 : %2").arg(cote, q(texteBornes(*liste2)).toHtmlEscaped());
      auto* it = new QTreeWidgetItem(f);
      it->setText(0, q(d->nom));
      it->setIcon(0, iconeSymbole(*d));
      it->setToolTip(0, QStringLiteral("<div style='max-width:360px'><b>%1</b> (%2)%3%4%5<p style='color:#6B7393'>%6</p></div>")
                            .arg(q(d->nom).toHtmlEscaped(), q(d->prefixe).toHtmlEscaped(),
                                 d->description.empty() ? QString() : QStringLiteral("<br>%1").arg(q(d->description).toHtmlEscaped()),
                                 d->fabricant.empty() && d->mpn.empty() ? QString() : QStringLiteral("<br><i>%1 %2</i>").arg(q(d->fabricant).toHtmlEscaped(), q(d->mpn).toHtmlEscaped()),
                                 QStringLiteral("<br><b>Bornes</b>%1").arg(bornes),
                                 range ? QStringLiteral("Clic droit : modifier, retirer de ta bibliothèque.")
                                       : QStringLiteral("Dans ce projet seulement. Clic droit : modifier, ranger dans ta bibliothèque.")));
      it->setData(0, Qt::UserRole, QStringLiteral("symbole"));
      it->setData(0, Qt::UserRole + 1, q(d->id));
    }
    if (f->childCount() == 0) delete f;
  }
  {
    QTreeWidgetItem* f = famille(QStringLiteral("Outils"), QStringLiteral("fil"));
    const std::tuple<const char*, const char*, const char*> outils[] = {{"fil", "Fil (W)", "fil"}, {"texte", "Texte (T)", "cote"}, {"nc", "Non connecté (Q)", "annuler"}};
    for (const auto& [id, texte, ic] : outils) {
      if (!correspond(QString::fromUtf8(texte))) continue;
      auto* it = new QTreeWidgetItem(f);
      it->setText(0, QString::fromUtf8(texte));
      it->setIcon(0, icone(QString::fromLatin1(ic)));
      it->setData(0, Qt::UserRole, QStringLiteral("outil"));
      it->setData(0, Qt::UserRole + 1, QString::fromLatin1(id));
    }
    if (f->childCount() == 0) delete f;
  }
  // Sans recherche : les familles courantes ouvertes (et les circuits types tant que le projet est presque vide) ;
  // avec recherche : tout ce qui est trouvé
  int poses = 0;
  if (const ProjetElectrique* pe = projetCourant())
    for (const auto& fe : pe->schema.feuilles) poses += static_cast<int>(fe.symboles.size());
  for (int i = 0; i < m_palette->topLevelItemCount(); ++i) {
    QTreeWidgetItem* f = m_palette->topLevelItem(i);
    const QString n = f->text(0);
    f->setExpanded(!filtre.isEmpty() || n == QLatin1String("Potentiels et renvois") || n == QLatin1String("Protection et sectionnement") ||
                   n == QLatin1String("Commande et relayage") || (poses < 6 && n.startsWith(QLatin1String("Circuits types"))) ||
                   (n == QLatin1String("Mes appareils") && f->childCount() > 1));
  }
}

// ---------- Pose ----------
void AtelierElectrique::choisirSymbole(const std::string& id, const std::string& catalogue, const std::string& parent) {
  const DefSymbole* d = definitionSymbole(id);
  if (!d) return;
  m_formAppareil = false;
  if (mode() != Schema) choisirMode(Schema);
  // Appareil de ma bibliothèque pas encore dans ce projet : sa définition y entre (le projet s'ouvre partout)
  if (const ProjetElectrique* pe = projetCourant(); pe && estAppareilPersonnel(id) && !pe->schema.symbole(id)) {
    const DefSymbole copie = *d;
    modifierProjet([&](ProjetElectrique& y) { y.schema.symboles[id] = copie; });
    d = definitionSymbole(id);
    if (!d) return;
  }
  // Depuis « Ajouter » d'un appareil, chaque élément va sur le folio qui lui revient : les pôles sur la puissance,
  // les contacts sur la commande (s'il en existe un et qu'on n'y est pas déjà)
  QString folioChoisi;
  if (const ProjetElectrique* pe = projetCourant(); pe && !parent.empty()) {
    const RoleSymbole r = roleSymbole(id);
    const int n = static_cast<int>(pe->schema.feuilles.size()), f = folio();
    auto typeDe = [&](int i) { return pe->folio(pe->schema.feuilles[static_cast<std::size_t>(i)].uuid).type; };
    const bool poles = r == RoleSymbole::PolesPuissance;
    if ((poles || estContact(r)) && f >= 0 && f < n) {
      const TypeFolio voulu = poles ? TypeFolio::Puissance : TypeFolio::Commande;
      const bool deja = typeDe(f) == voulu || (!poles && typeDe(f) == TypeFolio::Automate);
      for (int i = 0; i < n && !deja; ++i)
        if (typeDe(i) == voulu) {
          allerAuFolio(i);
          folioChoisi = QStringLiteral(" · sur le folio %1 « %2 »").arg(i + 1).arg(q(pe->schema.feuilles[static_cast<std::size_t>(i)].nom).toHtmlEscaped());
          break;
        }
    }
  }
  m_symbolePose = id;
  // Une seule référence possible : prise d'office ; plusieurs : la première par défaut, à changer avant de poser
  const auto proposees = entreesPourSymbole(id);
  m_cataloguePose = !catalogue.empty() ? catalogue : proposees.empty() ? std::string() : proposees.front()->id;
  m_parentPose = parent;
  m_schema->setOutil(VueSchema::Outil::Placer, id);
  m_schema->setFocus();
  const RoleSymbole role = roleSymbole(id);
  const ProjetElectrique* pe = projetCourant();
  const ComposantElec* porteur = pe && !parent.empty() ? pe->schema.composant(parent) : nullptr;
  QString texte;
  if (porteur && role == RoleSymbole::Borne)
    texte = QStringLiteral("<b>Borne de %1</b> : clique où la poser ; elle prend le numéro suivant · <b>R</b> tourne · Échap pour finir").arg(q(repereAffiche(*pe, *porteur)));
  else if (porteur)
    texte = QStringLiteral("<b>%1</b> de %2 : clique où le poser%3 · <b>R</b> tourne · Échap pour finir").arg(q(d->nom), q(repereAffiche(*pe, *porteur)), folioChoisi);
  else if (estContact(role))
    texte = QStringLiteral("<b>%1</b> : clique où le poser ; tu choisiras ensuite l’appareil qui le porte · <b>R</b> tourne · Échap pour finir").arg(q(d->nom));
  else if (role == RoleSymbole::Borne)
    texte = QStringLiteral("<b>%1</b> : clique où la poser ; elle prend le numéro suivant de son bornier · <b>R</b> tourne · Échap pour finir").arg(q(d->nom));
  else
    texte = QStringLiteral("<b>%1</b> : clique sur le folio pour le poser, autant de fois que tu veux · <b>R</b> tourne · Échap pour finir").arg(q(d->nom));
  m_etat->setText(texte);
  majPanneau();
}

void AtelierElectrique::choisirPotentiel(const QString& nom) {
  if (mode() != Schema) choisirMode(Schema);
  m_schema->setOutil(VueSchema::Outil::EtiquetteGlobale, s(nom));
  m_schema->setFocus();
  m_etat->setText(nom.isEmpty() ? QStringLiteral("<b>Potentiel ou renvoi</b> : clique au bout d’un fil, le nom est demandé · <b>R</b> tourne · Échap")
                                : QStringLiteral("<b>Potentiel %1</b> : clique au bout d’un fil ; le même nom posé ailleurs fait le renvoi · <b>R</b> tourne · Échap").arg(nom));
  majPanneau();
}

std::string AtelierElectrique::choisirParent(RoleSymbole role, const std::string& symbole) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return CANCEL;
  const std::vector<std::string> candidats = appareilsPour(*pe, role);
  const DefSymbole* d = definitionSymbole(symbole);
  if (candidats.empty()) {
    const QString quoi = role == RoleSymbole::PolesPuissance ? QStringLiteral("une bobine de contacteur") : role == RoleSymbole::ContactThermiqueNF || role == RoleSymbole::ContactThermiqueNO
                                                                                                                 ? QStringLiteral("un relais thermique")
                                                                                                                 : QStringLiteral("une bobine (contacteur, relais) ou un appareil");
    m_etat->setText(QStringLiteral("<span style='color:#A8620E'>Aucun appareil pour porter ce contact : pose d’abord %1, puis ses contacts s’y rattachent.</span>").arg(quoi));
    return CANCEL;
  }
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("menuRattacher"));
  QAction* entete = menu.addAction(QStringLiteral("Rattacher « %1 » à :").arg(d ? q(d->nom) : QString()));
  entete->setEnabled(false);
  std::map<QAction*, std::string> choix;
  for (const auto& id : candidats) {
    const ComposantElec* c = pe->schema.composant(id);
    if (!c) continue;
    int no = 0, nf = 0;
    if (const auto it = m_renvois.parComposant.find(id); it != m_renvois.parComposant.end())
      for (const auto& e : it->second) {
        if (e.role == RoleSymbole::ContactNO || e.role == RoleSymbole::ContactNOTempo) ++no;
        if (e.role == RoleSymbole::ContactNF || e.role == RoleSymbole::ContactNFTempo) ++nf;
      }
    const std::string fonction = c->champs.count("fonction") ? c->champs.at("fonction") : c->description;
    QAction* a = menu.addAction(QStringLiteral("%1 — %2  (%3 NO, %4 NF posés)").arg(q(repereAffiche(*pe, *c)), q(fonction)).arg(no).arg(nf));
    choix[a] = id;
    if (id == m_parentPose) menu.setActiveAction(a);
  }
  menu.addSeparator();
  QAction* sans = menu.addAction(QStringLiteral("Sans appareil (à rattacher plus tard)"));
  QAction* r = menu.exec(QCursor::pos());
  if (!r) return CANCEL;
  if (r == sans) return {};
  const auto it = choix.find(r);
  return it == choix.end() ? std::string(CANCEL) : it->second;
}

std::string AtelierElectrique::choisirBornier() {
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return CANCEL;
  const bool connecteur = m_symbolePose == "iec:connecteur";
  std::vector<const ComposantElec*> borniers;
  for (const auto& c : pe->schema.composants)
    if (roleSymbole(c.symbole) == RoleSymbole::Borne && (c.symbole == "iec:connecteur") == connecteur) borniers.push_back(&c);
  if (borniers.empty()) return {};
  std::sort(borniers.begin(), borniers.end(), [](const ComposantElec* a, const ComposantElec* b) { return prom::ecad::ordreNaturel(a->reference, b->reference); });
  QMenu menu(this);
  menu.setObjectName(QStringLiteral("menuBornier"));
  QAction* entete = menu.addAction(connecteur ? QStringLiteral("Contact de quel connecteur ?") : QStringLiteral("Borne de quel bornier ?"));
  entete->setEnabled(false);
  std::map<QAction*, std::string> choix;
  for (const ComposantElec* c : borniers) {
    const auto suivants = numerosSuivants(*pe, c->uuid, RoleSymbole::Borne, connecteur ? "iec:connecteur" : "iec:borne");
    const QString n = suivants.empty() ? QString() : q(suivants.begin()->second);
    QAction* a = menu.addAction(QStringLiteral("%1 — prochaine : %1:%2").arg(q(repereAffiche(*pe, *c)), n));
    choix[a] = c->uuid;
    if (c->uuid == m_parentPose) menu.setActiveAction(a);
  }
  menu.addSeparator();
  QAction* neuf = menu.addAction(connecteur ? QStringLiteral("Nouveau connecteur") : QStringLiteral("Nouveau bornier"));
  QAction* r = menu.exec(QCursor::pos());
  if (!r) return CANCEL;
  if (r == neuf) return {};
  const auto it = choix.find(r);
  return it == choix.end() ? std::string(CANCEL) : it->second;
}

void AtelierElectrique::poser(Pt p, int rotation) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe || m_symbolePose.empty()) return;
  const std::size_t f = static_cast<std::size_t>(folio());
  const std::string id = m_symbolePose;
  const RoleSymbole role = roleSymbole(id);
  std::string pose;
  if (role == RoleSymbole::Borne) {
    std::string bornier = m_parentPose;
    if (bornier.empty() || !pe->schema.composant(bornier)) bornier = choisirBornier();
    if (bornier == CANCEL) return;
    modifierProjet([&](ProjetElectrique& x) {
      if (id == "iec:connecteur") pose = bornier.empty() ? poserAppareil(x, f, id, p, rotation, {}) : poserElement(x, f, id, p, rotation, bornier);
      else pose = poserBorne(x, f, p, rotation, bornier, id == "iec:borne-pe");
    });
  } else if (estContact(role)) {
    std::string parent = m_parentPose;
    if (parent.empty() || !pe->schema.composant(parent)) parent = choisirParent(role, id);
    if (parent == CANCEL) return;
    modifierProjet([&](ProjetElectrique& x) { pose = parent.empty() ? poserAppareil(x, f, id, p, rotation, {}) : poserElement(x, f, id, p, rotation, parent); });
  } else {
    const std::string catalogue = m_cataloguePose;
    modifierProjet([&](ProjetElectrique& x) { pose = poserAppareil(x, f, id, p, rotation, catalogue); });
  }
  // Ce qui a été posé, dit en clair ; la prochaine borne va dans le même bornier
  pe = projetCourant();
  if (!pe || pose.empty()) return;
  for (const auto& sp : pe->schema.feuilles[f].symboles) {
    if (sp.uuid != pose) continue;
    const ComposantElec* c = pe->schema.composant(sp.composant);
    if (role == RoleSymbole::Borne && c) {
      m_parentPose = c->uuid;
      const DefSymbole* d = pe->schema.symbole(sp.symbole);
      const std::string n = d && !d->broches.empty() ? prom::ecad::numeroBroche(sp, d->broches.front().numero) : std::string();
      m_etat->setText(QStringLiteral("Borne <b>%1:%2</b> posée · clique pour la suivante du même bornier, Échap pour finir").arg(q(repereAffiche(*pe, *c)), q(n)));
    } else if (estContact(role) && c) {
      QString bornes;
      if (const DefSymbole* d = pe->schema.symbole(sp.symbole))
        for (const auto& b : prom::ecad::brochesPlacees(sp, *d)) bornes += (bornes.isEmpty() ? QString() : QStringLiteral("-")) + q(b.numero);
      m_etat->setText(QStringLiteral("Contact <b>%1</b> (%2) posé · clique pour un autre, Échap pour finir").arg(q(repereAffiche(*pe, *c)), bornes));
    } else if (c) {
      m_etat->setText(QStringLiteral("<b>%1</b> posé%2 · clique pour en poser un autre, Échap pour finir ; ses fils se tirent avec <b>W</b>")
                          .arg(q(repereAffiche(*pe, *c)), c->description.empty() ? QString() : QStringLiteral(" (%1)").arg(q(c->description))));
    } else if (estContact(role)) {
      m_etat->setText(QStringLiteral("<span style='color:#A8620E'>Contact posé sans appareil : rattache-le dans ses propriétés.</span>"));
    }
  }
}

// ---------- Actions ----------
void AtelierElectrique::numeroterFils(bool tout) {
  int n = 0;
  modifierProjet([&](ProjetElectrique& x) { n = prom::elec::numeroterFils(x, tout); });
  m_etat->setText(n > 0 ? QStringLiteral("%1 potentiel(s) numéroté(s)%2.").arg(n).arg(tout ? QStringLiteral(" dans l’ordre de lecture") : QString())
                        : QStringLiteral("Tous les fils ont déjà leur numéro."));
}

void AtelierElectrique::reperer(bool tout) {
  int n = 0;
  modifierProjet([&](ProjetElectrique& x) { n = annoterElectrique(x, tout); });
  m_etat->setText(n > 0 ? QStringLiteral("%1 appareil(s) repéré(s).").arg(n) : QStringLiteral("Tous les appareils ont déjà leur repère."));
}

void AtelierElectrique::verifier() {
  recalculer();
  int erreurs = 0, alertes = 0;
  for (const auto& v : m_violations) (v.gravite == prom::ecad::Gravite::Erreur ? erreurs : alertes) += 1;
  m_schema->selectionner({});
  m_etat->setText(erreurs + alertes == 0 ? QStringLiteral("<span style='color:#2E8B57'>✓ Aucun problème trouvé.</span>")
                                         : QStringLiteral("%1 erreur(s), %2 avertissement(s) : la liste est à droite, un clic mène à l’endroit.").arg(erreurs).arg(alertes));
  majPanneau();
}

int AtelierElectrique::ajouterFolio(TypeFolio t, const QString& titreFolio) {
  std::size_t i = 0;
  const std::size_t apres = static_cast<std::size_t>(folio()) + 1;
  modifierProjet([&](ProjetElectrique& x) { i = prom::elec::ajouterFolio(x, t, s(titreFolio), apres); });
  allerAuFolio(static_cast<int>(i));
  return static_cast<int>(i);
}

int AtelierElectrique::insererCircuit(int indice) {
  const auto& liste = circuitsTypes();
  if (!projetCourant() || indice < 0 || indice >= static_cast<int>(liste.size())) return -1;
  const InfosCircuitType info = liste[static_cast<std::size_t>(indice)];
  const std::size_t avant = projetCourant()->schema.composants.size();
  int f = -1;
  modifierProjet([&](ProjetElectrique& x) { f = insererCircuitType(x, info.type); });
  const ProjetElectrique* pe = projetCourant();
  if (f < 0 || !pe) return -1;
  m_schema->setOutil(VueSchema::Outil::Selection);
  allerAuFolio(f);
  QTimer::singleShot(0, this, [this] { m_schema->ajusterAuDessin(); });
  QStringList reperes;
  for (std::size_t i = avant; i < pe->schema.composants.size(); ++i) reperes << q(repereAffiche(*pe, pe->schema.composants[i]));
  m_etat->setText(QStringLiteral("<b>%1</b> posé : %2. Fils numérotés ; ses potentiels le relient au reste du projet. Ctrl+Z le retire.")
                      .arg(q(info.nom).toHtmlEscaped(), reperes.join(QStringLiteral(", "))));
  return f;
}

void AtelierElectrique::allerAuFolio(int f) {
  if (mode() != Schema) choisirMode(Schema);
  m_schema->setConception(m_projet, f);
  majFolios();
  majPanneau();
}

void AtelierElectrique::implanter(bool toutReplacer) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return;
  if (pe->armoires.empty()) nouvelleArmoire();
  const int a = std::max(0, m_armoire->armoire());
  int n = 0;
  modifierProjet([&](ProjetElectrique& x) {
    if (a < static_cast<int>(x.armoires.size())) n = toutReplacer ? implanterArmoire(x, static_cast<std::size_t>(a)) : implanterManquants(x, static_cast<std::size_t>(a));
  });
  m_armoire->setArmoire(m_projet, a);
  m_armoire->recadrer();
  m_etat->setText(QStringLiteral("%1 appareil(s) implanté(s).").arg(n));
  majPanneau();
}

int AtelierElectrique::nouvelleArmoire() {
  int i = -1;
  modifierProjet([&](ProjetElectrique& x) {
    prom::elec::Armoire a;
    a.uuid = prom::ecad::nouvelUuid();
    a.nom = x.armoires.empty() ? std::string("Armoire") : "Armoire " + std::to_string(x.armoires.size() + 1);
    equiperArmoire(a);
    x.armoires.push_back(a);
    i = static_cast<int>(x.armoires.size()) - 1;
    implanterManquants(x, static_cast<std::size_t>(i));
  });
  if (mode() != Armoire) choisirMode(Armoire);
  m_armoire->setArmoire(m_projet, i);
  majFolios();
  majPanneau();
  return i;
}

int AtelierElectrique::nouveauFaisceau() {
  int i = -1;
  modifierProjet([&](ProjetElectrique& x) {
    prom::elec::Faisceau f;
    f.uuid = prom::ecad::nouvelUuid();
    f.nom = x.faisceaux.empty() ? std::string("Faisceau") : "Faisceau " + std::to_string(x.faisceaux.size() + 1);
    x.faisceaux.push_back(f);
    i = static_cast<int>(x.faisceaux.size()) - 1;
  });
  if (mode() != Faisceau) choisirMode(Faisceau);
  m_faisceau->setFaisceau(m_projet, i);
  m_faisceau->setOutil(VueFaisceau::Outil::Noeud);
  majFolios();
  majPanneau();
  return i;
}

bool AtelierElectrique::exporterPdf(const QString& chemin) { return m_schema->exporterPdf(chemin); }

int AtelierElectrique::exporterDxf(const QString& chemin, int folio) { return m_schema->exporterDxf(chemin, folio); }

void AtelierElectrique::montrerAppareil(const std::string& composant) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return;
  const ComposantElec* c = pe->schema.composant(composant);
  if (!c) return;
  // Le symbole principal d'abord, sinon la première pose
  int fBest = -1;
  const prom::ecad::SymbolePlace* sBest = nullptr;
  for (std::size_t f = 0; f < pe->schema.feuilles.size(); ++f)
    for (const auto& sp : pe->schema.feuilles[f].symboles)
      if (sp.composant == composant && (!sBest || (sp.symbole == c->symbole && sBest->symbole != c->symbole))) {
        sBest = &sp;
        fBest = static_cast<int>(f);
      }
  if (!sBest) {
    m_etat->setText(QStringLiteral("%1 n’est posé sur aucun folio.").arg(q(repereAffiche(*pe, *c))));
    return;
  }
  const std::string uuid = sBest->uuid;
  const Pt pos = sBest->position;
  allerAuFolio(fBest);
  m_schema->selectionner({uuid});
  m_schema->centrerSur(pos);
}

void AtelierElectrique::allerA(const prom::ecad::Violation& v) {
  const ProjetElectrique* pe = projetCourant();
  if (!pe) return;
  int f = -1;
  for (std::size_t i = 0; i < pe->schema.feuilles.size(); ++i)
    if (pe->schema.feuilles[i].uuid == v.feuille) f = static_cast<int>(i);
  if (f < 0) {
    if (!v.objets.empty() && pe->schema.composant(v.objets.front())) montrerAppareil(v.objets.front());
    return;
  }
  allerAuFolio(f);
  std::set<std::string> sel;
  for (const auto& o : v.objets) {
    if (pe->schema.composant(o)) {
      for (const auto& sp : pe->schema.feuilles[static_cast<std::size_t>(f)].symboles)
        if (sp.composant == o) sel.insert(sp.uuid);
    } else
      sel.insert(o);
  }
  m_schema->selectionner(sel);
  if (v.position.x != 0 || v.position.y != 0) m_schema->centrerSur(v.position);
}

// ---------- Panneau de droite ----------
void AtelierElectrique::majPanneau() {
  auto* contenu = new QWidget;
  contenu->setObjectName(QStringLiteral("panneauElectrique"));
  contenu->setStyleSheet(QString::fromLatin1(STYLE));
  auto* col = new QVBoxLayout(contenu);
  col->setContentsMargins(10, 8, 10, 10);
  col->setSpacing(6);
  if (projetCourant()) {
    if (!m_parcours.empty()) carteParcours(col);
    switch (mode()) {
      case Schema: panneauSchema(col); break;
      case Armoire: panneauArmoire(col); break;
      case Faisceau: panneauFaisceau(col); break;
      case Rapports: panneauRapport(col); break;
      case Distribution: panneauDistribution(col); break;
      case Photovoltaique: panneauPV(col); break;
      case Grafcet: panneauGrafcet(col); break;
      default: break;
    }
  }
  col->addStretch(1);
  // Le panneau suit sa largeur : listes resserrées, étiquettes au-dessus des champs quand la place manque
  for (QComboBox* c : contenu->findChildren<QComboBox*>()) {
    c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    c->setMinimumContentsLength(std::max(c->minimumContentsLength(), 6));
  }
  for (QFormLayout* f : contenu->findChildren<QFormLayout*>())
    if (!f->property("dispositionGardee").toBool()) f->setRowWrapPolicy(QFormLayout::WrapLongRows);
  for (QCheckBox* c : contenu->findChildren<QCheckBox*>()) {
    c->setSizePolicy(QSizePolicy::Ignored, c->sizePolicy().verticalPolicy());
    if (c->toolTip().isEmpty()) c->setToolTip(c->text());
  }
  // Un bouton au long libellé ne rend pas le panneau plus large que sa colonne
  for (QPushButton* b : contenu->findChildren<QPushButton*>()) {
    b->setMinimumWidth(std::min(b->minimumSizeHint().width(), 90));
    if (b->toolTip().isEmpty()) b->setToolTip(b->text());
  }
  QWidget* ancien = m_panneau->takeWidget();
  m_panneau->setWidget(contenu);
  // Le logement et les calculs prennent toute la place ; les rapports aussi, sauf ceux qui ont un réglage ou une
  // synthèse à droite : prix de la nomenclature, fiche du câble, filerie de la liste de coupe, taux du chiffrage
  m_panneau->setVisible(!m_parcours.empty() ||
                        (mode() != Logement && mode() != Calculs && (mode() != Rapports || m_rapport == 1 || m_rapport == 3 || m_rapport == 8 || m_rapport == 9)));
  if (ancien) ancien->deleteLater();
}

void AtelierElectrique::listeProblemes(QVBoxLayout* col) {
  int erreurs = 0, alertes = 0;
  for (const auto& v : m_violations) (v.gravite == prom::ecad::Gravite::Erreur ? erreurs : alertes) += 1;
  auto* g = new QGroupBox(erreurs + alertes == 0 ? QStringLiteral("Vérification") : QStringLiteral("Problèmes : %1 ⛔ · %2 ⚠").arg(erreurs).arg(alertes));
  g->setToolTip(QStringLiteral("%1 erreur(s), %2 avertissement(s)").arg(erreurs).arg(alertes));
  g->setObjectName(QStringLiteral("groupeProblemes"));
  auto* v = new QVBoxLayout(g);
  if (erreurs + alertes == 0) {
    v->addWidget(new QLabel(QStringLiteral("<span style='color:#2E8B57'>✓ Aucun problème : bornes raccordées, contacts rattachés, potentiels renvoyés, PE en vert-jaune.</span>")));
    qobject_cast<QLabel*>(v->itemAt(0)->widget())->setWordWrap(true);
  }
  int n = 0;
  for (const auto& x : m_violations) {
    if (++n > 60) {
      v->addWidget(new QLabel(QStringLiteral("… et %1 autre(s).").arg(m_violations.size() - 60)));
      break;
    }
    auto* b = new QPushButton(QStringLiteral("%1  %2").arg(x.gravite == prom::ecad::Gravite::Erreur ? QStringLiteral("⛔") : QStringLiteral("⚠"), q(x.message)));
    b->setObjectName(x.gravite == prom::ecad::Gravite::Erreur ? QStringLiteral("erreur") : QStringLiteral("alerte"));
    b->setToolTip(q(x.code + " — " + x.conseil));
    b->setStyleSheet(QStringLiteral("text-align:left;"));
    b->setMinimumHeight(28);
    QSizePolicy sp = b->sizePolicy();
    sp.setHorizontalPolicy(QSizePolicy::Ignored);
    b->setSizePolicy(sp);
    const prom::ecad::Violation copie = x;
    connect(b, &QPushButton::clicked, this, [this, copie] {
      allerA(copie);
      m_etat->setText(QStringLiteral("%1 — %2").arg(q(copie.message).toHtmlEscaped(), q(copie.conseil).toHtmlEscaped()));
    });
    v->addWidget(b);
  }
  col->addWidget(g);
}

void AtelierElectrique::panneauSchema(QVBoxLayout* col) {
  if (m_formAppareil) {
    panneauAppareil(col);
    return;
  }
  const ProjetElectrique& pe = *projetCourant();
  const prom::ecad::ConceptionElec& c = pe.schema;
  const int fi = folio();
  if (fi < 0 || fi >= static_cast<int>(c.feuilles.size())) return;
  const prom::ecad::Feuille& F = c.feuilles[static_cast<std::size_t>(fi)];
  const auto sel = m_schema->selection();
  // ----- Pose en cours : ce qui va être posé, et sa référence -----
  if (m_schema->outil() == VueSchema::Outil::Placer && !m_symbolePose.empty()) {
    if (const DefSymbole* d = definitionSymbole(m_symbolePose)) {
      auto* g = new QGroupBox(QStringLiteral("À poser"));
      auto* v = new QVBoxLayout(g);
      auto* image = new QLabel;
      image->setPixmap(VueSchema::apercu(*d, QSize(260, 150)));
      image->setAlignment(Qt::AlignCenter);
      v->addWidget(image);
      auto* t = new QLabel(QStringLiteral("<b>%1</b><br><span style='color:#4A5270'>%2</span>").arg(q(d->nom).toHtmlEscaped(), q(d->description).toHtmlEscaped()));
      t->setWordWrap(true);
      v->addWidget(t);
      if (const AideSymbole* a = aideSymbole(m_symbolePose)) {
        auto* aideSymb = new QLabel(QStringLiteral("<b>C’est quoi ?</b> %1<br><b>À savoir :</b> %2").arg(q(a->quoi).toHtmlEscaped(), q(a->usage).toHtmlEscaped()));
        aideSymb->setObjectName(QStringLiteral("cestQuoiPose"));
        aideSymb->setWordWrap(true);
        v->addWidget(aideSymb);
      }
      if (estAppareilPersonnel(m_symbolePose)) {
        // Appareil personnel : ses bornes, côté par côté, et son repère
        const SpecAppareil a = specAppareil(*d);
        QString t = QStringLiteral("<b>Repère</b> %1…").arg(q(d->prefixe).toHtmlEscaped());
        for (const auto& [cote, liste] : {std::pair{QStringLiteral("en haut"), &a.haut}, std::pair{QStringLiteral("en bas"), &a.bas},
                                          std::pair{QStringLiteral("à gauche"), &a.gauche}, std::pair{QStringLiteral("à droite"), &a.droite}})
          if (!liste->empty()) t += QStringLiteral("<br><b>Bornes %1</b> : %2").arg(cote, q(texteBornes(*liste)).toHtmlEscaped());
        auto* bornes = new QLabel(t);
        bornes->setObjectName(QStringLiteral("bornesPose"));
        bornes->setWordWrap(true);
        v->addWidget(bornes);
        auto* modifier = new QPushButton(icone(QStringLiteral("appareil-perso")), QStringLiteral("Modifier cet appareil"));
        const std::string idSymbole = m_symbolePose;
        connect(modifier, &QPushButton::clicked, this, [this, idSymbole] { modifierAppareilPersonnel(idSymbole); });
        v->addWidget(modifier);
      }
      const auto proposees = entreesPourSymbole(m_symbolePose);
      if (!proposees.empty()) {
        v->addWidget(new QLabel(QStringLiteral("Référence")));
        auto* ref = new QComboBox;
        ref->setObjectName(QStringLiteral("referencePose"));
        ref->addItem(QStringLiteral("— à choisir plus tard —"), QString());
        for (const EntreeCatalogue* e : proposees) ref->addItem(q(e->designation), q(e->id));
        ref->setCurrentIndex(std::max(0, ref->findData(q(m_cataloguePose))));
        connect(ref, &QComboBox::currentIndexChanged, this, [this, ref](int) { m_cataloguePose = s(ref->currentData().toString()); });
        v->addWidget(ref);
      }
      auto* aide = new QLabel(estContact(roleSymbole(m_symbolePose))
                                  ? QStringLiteral("<span style='color:#6B7393'>Clique sur le folio : le menu propose l’appareil qui porte ce contact ; ses bornes prennent les numéros "
                                                   "suivants (13-14, 23-24…) et la bobine montre où il est.</span>")
                                  : QStringLiteral("<span style='color:#6B7393'>Clique sur le folio pour le poser ; <b>R</b> le tourne avant de poser ; Échap termine.</span>"));
      aide->setWordWrap(true);
      v->addWidget(aide);
      col->addWidget(g);
    }
  }
  // ----- Rien de choisi : le folio, le projet, et comment démarrer -----
  if (sel.empty()) {
    auto* g = new QGroupBox(QStringLiteral("Folio %1").arg(fi + 1));
    auto* f = new QFormLayout(g);
    auto* titreFolio = new QLineEdit(q(F.nom));
    titreFolio->setObjectName(QStringLiteral("titreFolio"));
    connect(titreFolio, &QLineEdit::editingFinished, this, [this, titreFolio, fi] {
      if (titreFolio->text().trimmed().isEmpty()) return;
      modifierProjet([&](ProjetElectrique& x) {
        if (fi < static_cast<int>(x.schema.feuilles.size())) x.schema.feuilles[static_cast<std::size_t>(fi)].nom = s(titreFolio->text());
      });
    });
    f->addRow(QStringLiteral("Titre"), titreFolio);
    auto* type = new QComboBox;
    for (TypeFolio t : {TypeFolio::Garde, TypeFolio::Puissance, TypeFolio::Commande, TypeFolio::Automate, TypeFolio::Distribution, TypeFolio::Unifilaire, TypeFolio::Borniers,
                        TypeFolio::Implantation, TypeFolio::Nomenclature, TypeFolio::Grafcet, TypeFolio::Cablage, TypeFolio::Libre})
      type->addItem(icone(iconeFolio(t)), QString::fromUtf8(nomTypeFolio(t)), static_cast<int>(t));
    type->setCurrentIndex(type->findData(static_cast<int>(pe.folio(F.uuid).type)));
    const std::string uuidFolio = F.uuid;
    connect(type, &QComboBox::currentIndexChanged, this, [this, type, uuidFolio](int) {
      modifierProjet([&](ProjetElectrique& x) { x.folios[uuidFolio].type = static_cast<TypeFolio>(type->currentData().toInt()); });
    });
    f->addRow(QStringLiteral("Type"), type);
    auto* format = new QComboBox;
    for (const char* x : {"A4", "A3", "A2"}) format->addItem(QString::fromLatin1(x));
    format->setCurrentText(q(F.format));
    connect(format, &QComboBox::currentTextChanged, this, [this, fi](const QString& t) {
      modifierProjet([&](ProjetElectrique& x) {
        if (fi < static_cast<int>(x.schema.feuilles.size())) x.schema.feuilles[static_cast<std::size_t>(fi)].format = s(t);
      });
    });
    f->addRow(QStringLiteral("Format"), format);
    if (pe.folio(F.uuid).type == TypeFolio::Grafcet) {
      // Le grafcet que dessine ce folio, et le chemin vers son onglet
      auto* quel = new QComboBox;
      quel->setObjectName(QStringLiteral("grafcetDuFolio"));
      for (const auto& gr : pe.grafcets) quel->addItem(icone(QStringLiteral("grafcet")), q(gr.nom), q(gr.uuid));
      if (pe.grafcets.empty()) quel->addItem(QStringLiteral("— aucun grafcet —"), QString());
      const int k = quel->findData(q(pe.folio(F.uuid).grafcet));
      quel->setCurrentIndex(std::max(0, k));
      connect(quel, &QComboBox::currentIndexChanged, this, [this, quel, uuidFolio](int) {
        modifierProjet([&](ProjetElectrique& x) { x.folios[uuidFolio].grafcet = s(quel->currentData().toString()); });
      });
      f->addRow(QStringLiteral("Grafcet"), quel);
      auto* ouvrirGrafcet = new QPushButton(icone(QStringLiteral("grafcet")), QStringLiteral("Modifier ce grafcet"));
      connect(ouvrirGrafcet, &QPushButton::clicked, this, [this, quel] {
        const ProjetElectrique* p2 = projetCourant();
        if (!p2) return;
        for (std::size_t i = 0; i < p2->grafcets.size(); ++i)
          if (q(p2->grafcets[i].uuid) == quel->currentData().toString() || quel->currentData().toString().isEmpty()) {
            m_grafcet = static_cast<int>(i);
            break;
          }
        choisirMode(Grafcet);
      });
      f->addRow(ouvrirGrafcet);
    }
    if (folioDessineDOffice(pe.folio(F.uuid).type)) {
      auto* auto_ = new QLabel(QStringLiteral("<span style='color:#2E8B57'>Dessiné d’office à partir du projet : ce folio se met à jour tout seul.</span>"));
      auto_->setWordWrap(true);
      f->addRow(auto_);
    }
    col->addWidget(g);
    // Pour bien démarrer, tant que le projet est presque vide
    int poses = 0;
    for (const auto& x : c.feuilles) poses += static_cast<int>(x.symboles.size());
    if (poses < 6) {
      auto* aide = new QGroupBox(QStringLiteral("Pour bien démarrer"));
      auto* v = new QVBoxLayout(aide);
      auto* t = new QLabel(QStringLiteral(
          "<ol style='margin-left:-20px'>"
          "<li><b>Choisis un appareil</b> dans la palette à gauche (ou tape son nom : <i>disjoncteur</i>, <i>contacteur</i>, <i>moteur</i>…) puis clique sur le folio.</li>"
          "<li><b>Tire les fils</b> avec <b>W</b> : clique sur une borne, puis sur une autre.</li>"
          "<li><b>Pose les potentiels</b> (L1, N, PE, 24V, 0V) au bout des fils : le même nom ailleurs fait le renvoi.</li>"
          "<li><b>Contacts</b> : pose un « contact à fermeture » sur le folio de commande, il se rattache à sa bobine et prend 13-14, 23-24…</li>"
          "<li><b>Numérote les fils</b>, puis regarde l’armoire, les borniers et la nomenclature : ils se remplissent seuls.</li></ol>"
          "Plus vite : le bouton <b>Circuit type</b> pose un démarrage de moteur, un inverseur, un éclairage ou des prises, câblés et repérés."));
      t->setWordWrap(true);
      v->addWidget(t);
      auto* exemple = new QLabel(QStringLiteral("<span style='color:#6B7393'>Un exemple complet s’ouvre depuis l’accueil : « Exemple : démarrage d’un moteur ». "
                                                "Un appareil manque à la bibliothèque ? « Mes appareils › Nouvel appareil… », dans la palette, le dessine d’après ses bornes.</span>"));
      exemple->setWordWrap(true);
      v->addWidget(exemple);
      col->addWidget(aide);
    }
    panneauReglages(col);
    listeProblemes(col);
    return;
  }
  // ----- Un objet choisi -----
  if (sel.size() == 1) {
    const std::string id = *sel.begin();
    const prom::ecad::SymbolePlace* sp = nullptr;
    for (const auto& x : F.symboles)
      if (x.uuid == id) sp = &x;
    if (sp) {
      const ComposantElec* x = c.composant(sp->composant);
      const RoleSymbole role = roleSymbole(sp->symbole);
      const std::string symboleId = sp->symbole;
      const std::string poseId = sp->uuid;
      // Borne de bornier
      if (x && roleSymbole(x->symbole) == RoleSymbole::Borne) {
        const DefSymbole* d = c.symbole(sp->symbole);
        const std::string numero = d && !d->broches.empty() ? prom::ecad::numeroBroche(*sp, d->broches.front().numero) : std::string();
        auto* g = new QGroupBox(QStringLiteral("Borne %1:%2").arg(q(repereAffiche(pe, *x)), q(numero)));
        auto* f = new QFormLayout(g);
        auto* bornier = new QComboBox;
        bornier->setObjectName(QStringLiteral("bornierBorne"));
        for (const auto& y : c.composants)
          if (roleSymbole(y.symbole) == RoleSymbole::Borne) bornier->addItem(q(repereAffiche(pe, y)), q(y.uuid));
        bornier->setCurrentIndex(bornier->findData(q(x->uuid)));
        connect(bornier, &QComboBox::currentIndexChanged, this, [this, bornier, poseId](int) { modifierProjet([&](ProjetElectrique& y) { rattacher(y, poseId, s(bornier->currentData().toString())); }); });
        f->addRow(QStringLiteral("Bornier"), bornier);
        auto* num = new QLineEdit(q(numero));
        num->setObjectName(QStringLiteral("numeroBorne"));
        connect(num, &QLineEdit::editingFinished, this, [this, num, poseId] {
          const std::string n = s(num->text());
          if (n.empty()) return;
          modifierProjet([&](ProjetElectrique& y) {
            for (auto& fe : y.schema.feuilles)
              for (auto& z : fe.symboles)
                if (z.uuid == poseId)
                  if (const DefSymbole* d2 = y.schema.symbole(z.symbole))
                    for (const auto& b : d2->broches) z.numeros[b.numero] = n;
          });
        });
        f->addRow(QStringLiteral("Numéro"), num);
        auto* repereBornier = new QLineEdit(q(x->reference));
        const std::string bornierId = x->uuid;
        connect(repereBornier, &QLineEdit::editingFinished, this, [this, repereBornier, bornierId] {
          if (repereBornier->text().trimmed().isEmpty()) return;
          modifierProjet([&](ProjetElectrique& y) {
            if (auto* z = y.schema.composant(bornierId)) z->reference = s(repereBornier->text()).substr(s(repereBornier->text())[0] == '-' ? 1 : 0);
          });
        });
        f->addRow(QStringLiteral("Repère du bornier"), repereBornier);
        auto* pe2 = new QCheckBox(QStringLiteral("Borne de protection (vert-jaune)"));
        pe2->setChecked(sp->symbole == "iec:borne-pe");
        connect(pe2, &QCheckBox::toggled, this, [this, poseId](bool oui) {
          modifierProjet([&](ProjetElectrique& y) {
            const std::string cible = oui ? "iec:borne-pe" : "iec:borne";
            if (const DefSymbole* d2 = symboleElectrique(cible)) y.schema.symboles[cible] = *d2;
            for (auto& fe : y.schema.feuilles)
              for (auto& z : fe.symboles)
                if (z.uuid == poseId) z.symbole = cible;
          });
        });
        f->addRow(QString(), pe2);
        col->addWidget(g);
        listeProblemes(col);
        return;
      }
      // Contact rattaché (ou à rattacher)
      if (estContact(role) || (x && sp->symbole != x->symbole)) {
        QString bornes;
        if (const DefSymbole* d = c.symbole(sp->symbole))
          for (const auto& b : prom::ecad::brochesPlacees(*sp, *d)) bornes += (bornes.isEmpty() ? QString() : QStringLiteral("-")) + q(b.numero);
        const DefSymbole* d = c.symbole(sp->symbole);
        auto* g = new QGroupBox(QStringLiteral("%1 %2").arg(d ? q(d->nom) : QStringLiteral("Contact"), bornes));
        auto* f = new QFormLayout(g);
        auto* parent = new QComboBox;
        parent->setObjectName(QStringLiteral("parentContact"));
        if (!x) parent->addItem(QStringLiteral("— aucun —"), QString());
        for (const auto& idApp : appareilsPour(pe, role == RoleSymbole::Autre ? RoleSymbole::ContactNO : role))
          if (const ComposantElec* y = c.composant(idApp))
            parent->addItem(QStringLiteral("%1 — %2").arg(q(repereAffiche(pe, *y)), q(y->champs.count("fonction") ? y->champs.at("fonction") : y->description)), q(y->uuid));
        if (x && parent->findData(q(x->uuid)) < 0) parent->addItem(q(repereAffiche(pe, *x)), q(x->uuid));
        parent->setCurrentIndex(x ? parent->findData(q(x->uuid)) : 0);
        connect(parent, &QComboBox::currentIndexChanged, this, [this, parent, poseId](int) {
          const std::string cible = s(parent->currentData().toString());
          if (!cible.empty()) modifierProjet([&](ProjetElectrique& y) { rattacher(y, poseId, cible); });
        });
        f->addRow(QStringLiteral("Appareil"), parent);
        auto* nums = new QLineEdit(bornes);
        nums->setObjectName(QStringLiteral("bornesContact"));
        nums->setToolTip(QStringLiteral("Numéros des bornes, séparés par un tiret (13-14)"));
        connect(nums, &QLineEdit::editingFinished, this, [this, nums, poseId] {
          const QStringList l = nums->text().split(QLatin1Char('-'), Qt::SkipEmptyParts);
          modifierProjet([&](ProjetElectrique& y) {
            for (auto& fe : y.schema.feuilles)
              for (auto& z : fe.symboles)
                if (z.uuid == poseId)
                  if (const DefSymbole* d2 = y.schema.symbole(z.symbole))
                    for (std::size_t k = 0; k < d2->broches.size() && k < static_cast<std::size_t>(l.size()); ++k) z.numeros[d2->broches[k].numero] = s(l[static_cast<int>(k)]);
          });
        });
        f->addRow(QStringLiteral("Bornes"), nums);
        if (x) {
          auto* aller = new QPushButton(icone(QStringLiteral("contacteur")), QStringLiteral("Aller à %1").arg(q(repereAffiche(pe, *x))));
          const std::string idx = x->uuid;
          connect(aller, &QPushButton::clicked, this, [this, idx] { montrerAppareil(idx); });
          f->addRow(QString(), aller);
        }
        col->addWidget(g);
        listeProblemes(col);
        return;
      }
      // Appareil
      if (x) {
        const std::string idx = x->uuid;
        auto* g = new QGroupBox(QStringLiteral("Appareil %1").arg(q(repereAffiche(pe, *x))));
        g->setObjectName(QStringLiteral("groupeAppareil"));
        auto* f = new QFormLayout(g);
        auto ligne = [&](const QString& etiquette, const QString& valeur, const QString& nom, std::function<void(ComposantElec&, const std::string&)> appliquer) {
          auto* e = new QLineEdit(valeur);
          e->setObjectName(nom);
          connect(e, &QLineEdit::editingFinished, this, [this, e, idx, appliquer] {
            const std::string v = s(e->text());
            modifierProjet([&](ProjetElectrique& y) {
              if (ComposantElec* z = y.schema.composant(idx)) appliquer(*z, v);
            });
          });
          f->addRow(etiquette, e);
          return e;
        };
        ligne(QStringLiteral("Repère"), q(x->reference), QStringLiteral("repereAppareil"), [](ComposantElec& z, const std::string& v) {
          if (!v.empty()) z.reference = v[0] == '-' ? v.substr(1) : v;
        });
        ligne(QStringLiteral("Désignation"), q(x->description), QStringLiteral("designationAppareil"), [](ComposantElec& z, const std::string& v) {
          z.description = v;
          z.valeur = v;
        });
        ligne(QStringLiteral("Fonction"), q(x->champs.count("fonction") ? x->champs.at("fonction") : std::string()), QStringLiteral("fonctionAppareil"),
              [](ComposantElec& z, const std::string& v) {
                if (v.empty()) z.champs.erase("fonction");
                else z.champs["fonction"] = v;
              });
        auto* emplacement = new QComboBox;
        emplacement->setEditable(true);
        emplacement->addItems({QString(), QStringLiteral("Armoire"), QStringLiteral("Porte"), QStringLiteral("Pupitre"), QStringLiteral("Machine"), QStringLiteral("Extérieur")});
        emplacement->setCurrentText(q(x->champs.count("emplacement") ? x->champs.at("emplacement") : std::string()));
        connect(emplacement, &QComboBox::currentTextChanged, this, [this, idx](const QString& t) {
          modifierProjet([&](ProjetElectrique& y) {
            if (ComposantElec* z = y.schema.composant(idx)) {
              if (t.trimmed().isEmpty()) z->champs.erase("emplacement");
              else z->champs["emplacement"] = s(t);
            }
          });
        });
        f->addRow(QStringLiteral("Emplacement"), emplacement);
        const auto proposees = entreesPourSymbole(x->symbole);
        if (!proposees.empty()) {
          auto* ref = new QComboBox;
          ref->setObjectName(QStringLiteral("referenceAppareil"));
          ref->addItem(QStringLiteral("— aucune —"), QString());
          for (const EntreeCatalogue* e : proposees) ref->addItem(q(e->designation), q(e->id));
          ref->setCurrentIndex(std::max(0, ref->findData(q(x->champs.count("catalogue") ? x->champs.at("catalogue") : std::string()))));
          ref->setMinimumContentsLength(18);
          ref->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
          connect(ref, &QComboBox::currentIndexChanged, this, [this, ref, idx](int) { modifierProjet([&](ProjetElectrique& y) { choisirReference(y, idx, s(ref->currentData().toString())); }); });
          f->addRow(QStringLiteral("Référence"), ref);
        }
        ligne(QStringLiteral("Fabricant"), q(x->fabricant), QStringLiteral("fabricantAppareil"), [](ComposantElec& z, const std::string& v) { z.fabricant = v; });
        ligne(QStringLiteral("Réf. fabricant"), q(x->mpn), QStringLiteral("mpnAppareil"), [](ComposantElec& z, const std::string& v) { z.mpn = v; });
        // Caractéristiques modifiables
        for (const auto& [cle, nom] : std::vector<std::pair<std::string, QString>>{{"calibre", QStringLiteral("Calibre (A)")},
                                                                                     {"courbe", QStringLiteral("Courbe")},
                                                                                     {"sensibilite", QStringLiteral("Sensibilité (mA)")},
                                                                                     {"typeDifferentiel", QStringLiteral("Type de différentiel")},
                                                                                     {"tension", QStringLiteral("Tension")},
                                                                                     {"puissance", QStringLiteral("Puissance (kW)")}}) {
          if (!x->champs.count(cle)) continue;
          const std::string k = cle;
          ligne(nom, q(x->champs.at(cle)), QString::fromStdString("champ-" + cle), [k](ComposantElec& z, const std::string& v) {
            if (v.empty()) z.champs.erase(k);
            else z.champs[k] = v;
          });
        }
        if (estAppareilPersonnel(x->symbole)) {
          // Appareil personnel : son symbole, ses bornes et ses données d'armoire se modifient pour tous ses exemplaires
          auto* modifier = new QPushButton(icone(QStringLiteral("appareil-perso")), QStringLiteral("Modifier l’appareil (bornes, symbole, armoire)"));
          modifier->setObjectName(QStringLiteral("modifierAppareilPerso"));
          const std::string idSymbole = x->symbole;
          connect(modifier, &QPushButton::clicked, this, [this, idSymbole] { modifierAppareilPersonnel(idSymbole); });
          f->addRow(modifier);
        }
        col->addWidget(g);
        // C'est quoi ? Ce qu'est l'appareil et ce qu'il faut savoir pour s'en servir
        if (const AideSymbole* a = aideSymbole(x->symbole)) {
          auto* gq = new QGroupBox(QStringLiteral("C’est quoi ?"));
          gq->setObjectName(QStringLiteral("cestQuoiAppareil"));
          auto* vq = new QVBoxLayout(gq);
          auto* l = new QLabel(QStringLiteral("%1<br><span style='color:#6B7393'><b>À savoir :</b> %2</span>").arg(q(a->quoi).toHtmlEscaped(), q(a->usage).toHtmlEscaped()));
          l->setWordWrap(true);
          vq->addWidget(l);
          col->addWidget(gq);
        }
        // Ses éléments posés : bobine, contacts, pôles, avec leur place
        if (const auto it = m_renvois.parComposant.find(idx); it != m_renvois.parComposant.end() && it->second.size() > 1) {
          auto* ge = new QGroupBox(QStringLiteral("Éléments posés (%1)").arg(it->second.size()));
          auto* v = new QVBoxLayout(ge);
          for (const auto& e : it->second) {
            const DefSymbole* d = definitionSymbole(e.definition);
            auto* b = new QPushButton(QStringLiteral("%1  %2  →  folio %3").arg(d ? q(d->nom) : QStringLiteral("Symbole"), q(e.bornes), q(e.ou.texteComplet())));
            const std::string symbole = e.symbole;
            const std::size_t feuille = e.feuille;
            connect(b, &QPushButton::clicked, this, [this, symbole, feuille] {
              allerAuFolio(static_cast<int>(feuille));
              m_schema->selectionner({symbole});
            });
            v->addWidget(b);
          }
          col->addWidget(ge);
        }
        // Ajouter ce qui va avec
        {
          auto* ga = new QGroupBox(QStringLiteral("Ajouter"));
          auto* v = new QVBoxLayout(ga);
          auto bouton = [&](const QString& texte, const std::string& symbole) {
            auto* b = new QPushButton(icone(QStringLiteral("contacteur")), texte);
            connect(b, &QPushButton::clicked, this, [this, symbole, idx] { choisirSymbole(symbole, {}, idx); });
            v->addWidget(b);
          };
          if (x->symbole == "iec:bobine-km") bouton(QStringLiteral("Pôles de puissance (1-2 3-4 5-6)"), "iec:poles-km-3p");
          if (x->symbole == "iec:relais-thermique-3p") {
            bouton(QStringLiteral("Contact 95-96 (coupe la commande)"), "iec:contact-thermique-nf");
            bouton(QStringLiteral("Contact 97-98 (signale le défaut)"), "iec:contact-thermique-no");
          } else if (const auto porteurs = appareilsPour(pe, RoleSymbole::ContactNO);
                     role == RoleSymbole::Principal && std::find(porteurs.begin(), porteurs.end(), idx) != porteurs.end()) {
            bouton(QStringLiteral("Contact à fermeture"), "iec:contact-no");
            bouton(QStringLiteral("Contact à ouverture"), "iec:contact-nf");
            if (x->symbole.find("tempo") != std::string::npos) {
              bouton(QStringLiteral("Contact temporisé à fermeture"), "iec:contact-no-tempo");
              bouton(QStringLiteral("Contact temporisé à ouverture"), "iec:contact-nf-tempo");
            }
            if (x->symbole == "iec:bobine-ka") bouton(QStringLiteral("Contact inverseur"), "iec:contact-inverseur");
          }
          if (v->count() > 0) col->addWidget(ga);
          else delete ga;
        }
        auto* suppr = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer l’appareil et tous ses contacts"));
        connect(suppr, &QPushButton::clicked, this, [this, idx] {
          modifierProjet([&](ProjetElectrique& y) { supprimerAppareil(y, idx); });
          m_schema->selectionner({});
        });
        col->addWidget(suppr);
        listeProblemes(col);
        return;
      }
    }
    // Fil : son potentiel, son numéro, son conducteur
    for (const auto& w : F.fils) {
      if (w.uuid != id) continue;
      const auto it = m_k.parFil.find(id);
      if (it == m_k.parFil.end()) break;
      const prom::ecad::Reseau& r = m_k.reseaux[static_cast<std::size_t>(it->second)];
      const std::string nom = r.nom;
      const bool automatique = nom.rfind("N-", 0) == 0;
      auto* g = new QGroupBox(automatique ? QStringLiteral("Fil sans numéro") : QStringLiteral("Fil %1").arg(q(nom)));
      g->setObjectName(QStringLiteral("groupeFil"));
      auto* f = new QFormLayout(g);
      auto* numero = new QLineEdit(automatique ? QString() : q(nom));
      numero->setObjectName(QStringLiteral("numeroFil"));
      numero->setPlaceholderText(QStringLiteral("numéro du fil"));
      if (r.nomme) {
        numero->setReadOnly(true);
        numero->setToolTip(QStringLiteral("Nommé par un potentiel posé sur le folio : renomme le potentiel."));
      }
      std::vector<std::string> broches;
      for (const auto& b : r.broches) broches.push_back(b.composant + ":" + b.broche);
      connect(numero, &QLineEdit::editingFinished, this, [this, numero, broches, nom] {
        const std::string n = s(numero->text());
        if (n.empty() || n == nom) return;
        modifierProjet([&](ProjetElectrique& y) {
          for (const auto& b : broches) y.schema.nomsReseaux[b] = n;
          if (const auto itc = y.conducteurs.find(nom); itc != y.conducteurs.end()) {
            y.conducteurs[n] = itc->second;
            y.conducteurs.erase(nom);
          }
        });
      });
      f->addRow(QStringLiteral("Numéro"), numero);
      const ProprietesConducteur defaut = proprietesEffectives(pe, nom);
      const ProprietesConducteur propre = pe.conducteurs.count(nom) ? pe.conducteurs.at(nom) : ProprietesConducteur{};
      auto* section = new QComboBox;
      section->setObjectName(QStringLiteral("sectionFil"));
      section->addItem(QStringLiteral("par défaut (%1 mm²)").arg(QLocale().toString(defaut.section)), 0.0);
      for (double v : {0.5, 0.75, 1.0, 1.5, 2.5, 4.0, 6.0, 10.0, 16.0, 25.0, 35.0, 50.0, 70.0, 95.0}) section->addItem(QStringLiteral("%1 mm²").arg(QLocale().toString(v)), v);
      for (int k = 1; k < section->count(); ++k)
        if (std::abs(section->itemData(k).toDouble() - propre.section) < 1e-6) section->setCurrentIndex(k);
      connect(section, &QComboBox::currentIndexChanged, this, [this, section, nom](int) { modifierProjet([&](ProjetElectrique& y) { y.conducteurs[nom].section = section->currentData().toDouble(); }); });
      f->addRow(QStringLiteral("Section"), section);
      auto* couleur = new QComboBox;
      couleur->setObjectName(QStringLiteral("couleurFil"));
      couleur->addItem(QStringLiteral("par défaut (%1)").arg(q(defaut.couleur)), QString());
      for (const auto& [code, nomCouleur] : couleursConducteurs()) couleur->addItem(QStringLiteral("%1 — %2").arg(q(code), q(nomCouleur)), q(code));
      couleur->setCurrentIndex(std::max(0, couleur->findData(q(propre.couleur))));
      connect(couleur, &QComboBox::currentIndexChanged, this, [this, couleur, nom](int) { modifierProjet([&](ProjetElectrique& y) { y.conducteurs[nom].couleur = s(couleur->currentData().toString()); }); });
      f->addRow(QStringLiteral("Couleur"), couleur);
      auto* type = new QComboBox;
      type->setEditable(true);
      type->addItems({QString(), QStringLiteral("H07V-K"), QStringLiteral("H05V-K"), QStringLiteral("H07V-U"), QStringLiteral("H07V-R"), QStringLiteral("H07RN-F")});
      type->setCurrentText(q(propre.type));
      type->setToolTip(QStringLiteral("Par défaut : %1").arg(q(defaut.type)));
      connect(type, &QComboBox::currentTextChanged, this, [this, nom](const QString& t) { modifierProjet([&](ProjetElectrique& y) { y.conducteurs[nom].type = s(t); }); });
      f->addRow(QStringLiteral("Type"), type);
      QString relie;
      for (const auto& b : r.broches)
        if (const ComposantElec* y = c.composant(b.composant)) relie += (relie.isEmpty() ? QString() : QStringLiteral(", ")) + q(repereAffiche(pe, *y) + ":" + b.broche);
      auto* l = new QLabel(QStringLiteral("<span style='color:#4A5270'>Relie : %1</span>").arg(relie.toHtmlEscaped()));
      l->setWordWrap(true);
      f->addRow(l);
      col->addWidget(g);
      listeProblemes(col);
      return;
    }
    // Potentiel (étiquette)
    for (const auto& e : F.etiquettes) {
      if (e.uuid != id) continue;
      auto* g = new QGroupBox(QStringLiteral("Potentiel %1").arg(q(e.texte)));
      auto* f = new QFormLayout(g);
      auto* nom = new QLineEdit(q(e.texte));
      nom->setObjectName(QStringLiteral("nomPotentiel"));
      const std::string ancien = e.texte;
      auto* partout = new QCheckBox(QStringLiteral("Renommer partout"));
      partout->setChecked(true);
      connect(nom, &QLineEdit::editingFinished, this, [this, nom, partout, ancien, id] {
        const std::string n = s(nom->text());
        if (n.empty() || n == ancien) return;
        const bool tous = partout->isChecked();
        modifierProjet([&](ProjetElectrique& y) {
          for (auto& fe : y.schema.feuilles)
            for (auto& z : fe.etiquettes)
              if (z.uuid == id || (tous && z.texte == ancien)) z.texte = n;
        });
      });
      f->addRow(QStringLiteral("Nom"), nom);
      f->addRow(QString(), partout);
      if (const auto it = m_renvois.parPotentiel.find(e.texte); it != m_renvois.parPotentiel.end())
        for (const auto& x : it->second) {
          if (x.symbole == e.uuid) continue;
          auto* b = new QPushButton(QStringLiteral("Aussi au folio %1").arg(q(x.ou.texteComplet())));
          const std::string cible = x.symbole;
          const std::size_t feuille = x.feuille;
          connect(b, &QPushButton::clicked, this, [this, cible, feuille] {
            allerAuFolio(static_cast<int>(feuille));
            m_schema->selectionner({cible});
          });
          f->addRow(b);
        }
      col->addWidget(g);
      listeProblemes(col);
      return;
    }
  }
  // Plusieurs objets
  auto* g = new QGroupBox(QStringLiteral("%1 objet(s) choisi(s)").arg(sel.size()));
  auto* v = new QVBoxLayout(g);
  auto* suppr = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer (Suppr)"));
  connect(suppr, &QPushButton::clicked, this, [this] { m_schema->supprimerSelection(); });
  v->addWidget(suppr);
  auto* tourner = new QPushButton(icone(QStringLiteral("pivoter")), QStringLiteral("Tourner (R)"));
  connect(tourner, &QPushButton::clicked, this, [this] { m_schema->tournerSelection(); });
  v->addWidget(tourner);
  col->addWidget(g);
  listeProblemes(col);
}

void AtelierElectrique::panneauReglages(QVBoxLayout* col) {
  const ProjetElectrique& pe = *projetCourant();
  auto* g = new QGroupBox(QStringLiteral("Projet et cartouche"));
  g->setObjectName(QStringLiteral("groupeReglages"));
  auto* f = new QFormLayout(g);
  auto texte = [&](const QString& etiquette, const std::string& valeur, std::function<void(ProjetElectrique&, const std::string&)> appliquer) {
    auto* e = new QLineEdit(q(valeur));
    connect(e, &QLineEdit::editingFinished, this, [this, e, appliquer] {
      const std::string v = s(e->text());
      modifierProjet([&](ProjetElectrique& y) { appliquer(y, v); });
    });
    f->addRow(etiquette, e);
  };
  texte(QStringLiteral("Titre"), pe.schema.cartouche.titre.empty() ? pe.nom : pe.schema.cartouche.titre, [](ProjetElectrique& y, const std::string& v) {
    y.schema.cartouche.titre = v;
    if (!v.empty()) y.nom = v;
  });
  texte(QStringLiteral("Installation"), pe.reglages.installation, [](ProjetElectrique& y, const std::string& v) { y.reglages.installation = v; });
  texte(QStringLiteral("Client"), pe.reglages.client, [](ProjetElectrique& y, const std::string& v) { y.reglages.client = v; });
  texte(QStringLiteral("Société"), pe.reglages.societe.empty() ? pe.schema.cartouche.societe : pe.reglages.societe, [](ProjetElectrique& y, const std::string& v) { y.reglages.societe = v; });
  texte(QStringLiteral("Auteur"), pe.reglages.auteur.empty() ? pe.schema.cartouche.auteur : pe.reglages.auteur, [](ProjetElectrique& y, const std::string& v) { y.reglages.auteur = v; });
  texte(QStringLiteral("Date"), pe.schema.cartouche.date, [](ProjetElectrique& y, const std::string& v) { y.schema.cartouche.date = v; });
  texte(QStringLiteral("Révision"), pe.schema.cartouche.revision, [](ProjetElectrique& y, const std::string& v) { y.schema.cartouche.revision = v; });
  auto* norme = new QComboBox;
  norme->addItem(QStringLiteral("Lettres classiques (Q, KM, S, H…)"), 0);
  norme->addItem(QStringLiteral("CEI 81346 (QA, KF, SF, PF…)"), 1);
  norme->setCurrentIndex(pe.reglages.norme == NormeRepere::Cei81346 ? 1 : 0);
  connect(norme, &QComboBox::currentIndexChanged, this, [this](int i) {
    modifierProjet([&](ProjetElectrique& y) {
      y.reglages.norme = i == 1 ? NormeRepere::Cei81346 : NormeRepere::Classique;
      annoterElectrique(y, true);
    });
  });
  f->addRow(QStringLiteral("Repérage"), norme);
  auto* tiret = new QCheckBox(QStringLiteral("Tiret devant le repère (-KM1)"));
  tiret->setChecked(pe.reglages.tiret);
  connect(tiret, &QCheckBox::toggled, this, [this](bool oui) { modifierProjet([&](ProjetElectrique& y) { y.reglages.tiret = oui; }); });
  f->addRow(QString(), tiret);
  auto* fils = new QComboBox;
  fils->addItem(QStringLiteral("À la suite (1, 2, 3…)"), 0);
  fils->addItem(QStringLiteral("Par folio (3.1, 3.2…)"), 1);
  fils->setCurrentIndex(pe.reglages.fils == NumerotationFils::ParFolio ? 1 : 0);
  connect(fils, &QComboBox::currentIndexChanged, this, [this](int i) { modifierProjet([&](ProjetElectrique& y) { y.reglages.fils = i == 1 ? NumerotationFils::ParFolio : NumerotationFils::Sequentielle; }); });
  f->addRow(QStringLiteral("Numéros de fils"), fils);
  auto* reseau = new QComboBox;
  reseau->addItem(QStringLiteral("400 V triphasé + N"), 400);
  reseau->addItem(QStringLiteral("230 V monophasé"), 230);
  reseau->setCurrentIndex(pe.reglages.tension > 300 ? 0 : 1);
  connect(reseau, &QComboBox::currentIndexChanged, this, [this, reseau](int) { modifierProjet([&](ProjetElectrique& y) { y.reglages.tension = reseau->currentData().toDouble(); }); });
  f->addRow(QStringLiteral("Réseau"), reseau);
  auto* commande = new QComboBox;
  commande->addItem(QStringLiteral("24 V continu"), 0);
  commande->addItem(QStringLiteral("24 V alternatif"), 1);
  commande->addItem(QStringLiteral("230 V alternatif"), 2);
  commande->setCurrentIndex(pe.reglages.tensionCommande > 100 ? 2 : pe.reglages.commandeContinue ? 0 : 1);
  connect(commande, &QComboBox::currentIndexChanged, this, [this](int i) {
    modifierProjet([&](ProjetElectrique& y) {
      y.reglages.tensionCommande = i == 2 ? 230 : 24;
      y.reglages.commandeContinue = i == 0;
    });
  });
  f->addRow(QStringLiteral("Commande"), commande);
  auto* regime = new QComboBox;
  for (const char* r : {"TT", "TN-S", "TN-C", "IT"}) regime->addItem(QString::fromLatin1(r));
  regime->setCurrentIndex(static_cast<int>(pe.reglages.regime));
  connect(regime, &QComboBox::currentIndexChanged, this, [this](int i) { modifierProjet([&](ProjetElectrique& y) { y.reglages.regime = static_cast<RegimeNeutre>(i); }); });
  f->addRow(QStringLiteral("Régime de neutre"), regime);
  col->addWidget(g);
  // Révisions du dossier : la plus récente au cartouche de chaque folio, l'historique sur la page de garde
  auto* gr = new QGroupBox(QStringLiteral("Révisions"));
  gr->setObjectName(QStringLiteral("groupeRevisions"));
  auto* vr = new QVBoxLayout(gr);
  if (pe.revisions.empty()) {
    auto* l = new QLabel(QStringLiteral("<span style='color:#4A5270'>À chaque envoi du dossier, note ce qui a changé : l’indice passe au cartouche, l’historique à la page de garde.</span>"));
    l->setWordWrap(true);
    vr->addWidget(l);
  }
  for (std::size_t i = pe.revisions.size(); i-- > 0 && pe.revisions.size() - i <= 4;) {
    const auto& rv = pe.revisions[i];
    auto* l = new QLabel(QStringLiteral("<b>%1</b> · %2%3<br><span style='color:#4A5270'>%4</span>")
                             .arg(q(rv.indice).toHtmlEscaped(), q(rv.date).toHtmlEscaped(), rv.auteur.empty() ? QString() : QStringLiteral(" · ") + q(rv.auteur).toHtmlEscaped(),
                                  q(rv.description).toHtmlEscaped()));
    l->setWordWrap(true);
    vr->addWidget(l);
  }
  auto* quoi = new QLineEdit;
  quoi->setObjectName(QStringLiteral("descriptionRevision"));
  quoi->setPlaceholderText(QStringLiteral("Ce qui change (ex. : ajout du variateur)"));
  vr->addWidget(quoi);
  const std::string prochain = pe.revisions.empty() ? (pe.schema.cartouche.revision.empty() ? std::string("A") : pe.schema.cartouche.revision) : indiceSuivant(pe.revisions.back().indice);
  auto* nouvelle = new QPushButton(icone(QStringLiteral("annoter")), QStringLiteral("Révision %1").arg(q(prochain)));
  nouvelle->setObjectName(QStringLiteral("nouvelleRevision"));
  nouvelle->setToolTip(QStringLiteral("Enregistre la révision %1 à la date du jour ; le cartouche de chaque folio la porte").arg(q(prochain)));
  auto enregistrer = [this, quoi] {
    const std::string description = s(quoi->text()).empty() ? std::string("Mise à jour du dossier") : s(quoi->text());
    const std::string date = QDate::currentDate().toString(Qt::ISODate).toStdString();
    std::string indice;
    modifierProjet([&](ProjetElectrique& y) { indice = nouvelleRevision(y, date, description).indice; });
    m_etat->setText(QStringLiteral("Révision <b>%1</b> enregistrée : le cartouche et la page de garde la montrent.").arg(q(indice).toHtmlEscaped()));
  };
  connect(nouvelle, &QPushButton::clicked, this, enregistrer);
  connect(quoi, &QLineEdit::returnPressed, this, enregistrer);
  vr->addWidget(nouvelle);
  col->addWidget(gr);
}

void AtelierElectrique::panneauArmoire(QVBoxLayout* col) {
  const ProjetElectrique& pe = *projetCourant();
  const int ai = m_armoire->armoire();
  if (ai < 0 || ai >= static_cast<int>(pe.armoires.size())) {
    auto* b = new QPushButton(icone(QStringLiteral("armoire")), QStringLiteral("Créer l’armoire et y implanter les appareils"));
    b->setObjectName(QStringLiteral("principal"));
    connect(b, &QPushButton::clicked, this, [this] { nouvelleArmoire(); });
    col->addWidget(b);
    return;
  }
  const prom::elec::Armoire& a = pe.armoires[static_cast<std::size_t>(ai)];
  // Armoire vide : ses appareils viennent du schéma
  if (a.implantations.empty()) {
    auto* g0 = new QGroupBox(QStringLiteral("Pour remplir l’armoire"));
    auto* v0 = new QVBoxLayout(g0);
    auto* t0 = new QLabel(pe.schema.composants.empty()
                              ? QStringLiteral("Pose d’abord les appareils au schéma (disjoncteurs, contacteurs, bornes…) : ils viennent ensuite ici d’un clic, chacun à sa taille.")
                              : QStringLiteral("Les appareils du schéma ne sont pas encore implantés : « Implanter ce qui manque » les range sur les rails."));
    t0->setWordWrap(true);
    v0->addWidget(t0);
    auto* b0 = new QPushButton(icone(pe.schema.composants.empty() ? QStringLiteral("schema") : QStringLiteral("armoire")),
                               pe.schema.composants.empty() ? QStringLiteral("Aller au schéma") : QStringLiteral("Implanter ce qui manque"));
    b0->setObjectName(QStringLiteral("principal"));
    const bool vide = pe.schema.composants.empty();
    connect(b0, &QPushButton::clicked, this, [this, vide] {
      if (!vide) {
        implanter(false);
        return;
      }
      choisirMode(Schema);
      m_recherche->setFocus();
    });
    v0->addWidget(b0);
    col->addWidget(g0);
  }
  // Appareil choisi
  if (!m_armoire->selection().empty())
    if (const ComposantElec* c = pe.schema.composant(m_armoire->selection())) {
      auto* g = new QGroupBox(QStringLiteral("Appareil %1").arg(q(repereAffiche(pe, *c))));
      auto* v = new QVBoxLayout(g);
      const Encombrement e = encombrement(pe, c->uuid);
      auto* t = new QLabel(QStringLiteral("%1<br><span style='color:#4A5270'>%2 × %3 × %4 mm%5</span>")
                               .arg(q(c->description).toHtmlEscaped())
                               .arg(e.largeur)
                               .arg(e.hauteur)
                               .arg(e.profondeur)
                               .arg(e.rail ? QStringLiteral(", sur rail DIN") : QStringLiteral(", fixé sur la plaque")));
      t->setWordWrap(true);
      v->addWidget(t);
      const std::string idc = c->uuid;
      auto* voir = new QPushButton(icone(QStringLiteral("schema")), QStringLiteral("Voir au schéma"));
      connect(voir, &QPushButton::clicked, this, [this, idc] { montrerAppareil(idc); });
      v->addWidget(voir);
      auto* retirer = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Retirer de l’armoire"));
      connect(retirer, &QPushButton::clicked, this, [this] { m_armoire->retirerSelection(); });
      v->addWidget(retirer);
      col->addWidget(g);
    }
  // Dimensions
  auto* g = new QGroupBox(QStringLiteral("Armoire"));
  g->setObjectName(QStringLiteral("groupeArmoire"));
  auto* f = new QFormLayout(g);
  auto* nom = new QLineEdit(q(a.nom));
  connect(nom, &QLineEdit::editingFinished, this, [this, nom, ai] {
    if (!nom->text().trimmed().isEmpty()) modifierProjet([&](ProjetElectrique& y) { y.armoires[static_cast<std::size_t>(ai)].nom = s(nom->text()); });
  });
  f->addRow(QStringLiteral("Nom"), nom);
  auto dimension = [&](const QString& etiquette, double valeur, double mini, std::function<void(prom::elec::Armoire&, double)> appliquer) {
    auto* d = new QSpinBox;
    d->setRange(static_cast<int>(mini), 4000);
    d->setSingleStep(50);
    d->setSuffix(QStringLiteral(" mm"));
    d->setValue(static_cast<int>(valeur));
    connect(d, &QSpinBox::editingFinished, this, [this, d, ai, appliquer] {
      modifierProjet([&](ProjetElectrique& y) {
        auto& ar = y.armoires[static_cast<std::size_t>(ai)];
        appliquer(ar, d->value());
        equiperArmoire(ar);
        implanterArmoire(y, static_cast<std::size_t>(ai));
      });
      m_armoire->recadrer();
    });
    f->addRow(etiquette, d);
  };
  dimension(QStringLiteral("Largeur"), a.largeur, 150, [](prom::elec::Armoire& ar, double v) { ar.largeur = v; });
  dimension(QStringLiteral("Hauteur"), a.hauteur, 150, [](prom::elec::Armoire& ar, double v) { ar.hauteur = v; });
  dimension(QStringLiteral("Profondeur"), a.profondeur, 60, [](prom::elec::Armoire& ar, double v) { ar.profondeur = v; });
  auto* modulaire = new QCheckBox(QStringLiteral("Coffret modulaire"));
  modulaire->setToolTip(QStringLiteral("Rangées de 13 modules de 18 mm, comme le tableau d’un logement"));
  modulaire->setChecked(a.tableauModulaire);
  connect(modulaire, &QCheckBox::toggled, this, [this, ai](bool oui) {
    modifierProjet([&](ProjetElectrique& y) {
      auto& ar = y.armoires[static_cast<std::size_t>(ai)];
      ar.tableauModulaire = oui;
      equiperArmoire(ar);
      implanterArmoire(y, static_cast<std::size_t>(ai));
    });
    m_armoire->recadrer();
  });
  f->addRow(QString(), modulaire);
  col->addWidget(g);
  // Bilan
  const BilanArmoire b = bilanArmoire(pe, static_cast<std::size_t>(ai));
  auto* gb = new QGroupBox(QStringLiteral("Bilan"));
  gb->setObjectName(QStringLiteral("bilanArmoire"));
  auto* v = new QVBoxLayout(gb);
  const QString couleur = b.reserve >= 0.2 ? QStringLiteral("#2E8B57") : QStringLiteral("#A8620E");
  auto* t = new QLabel(QStringLiteral("Rails remplis à <b>%1 %</b> · réserve <b style='color:%2'>%3 %</b>%4")
                           .arg(std::lround(100 * b.tauxRemplissage))
                           .arg(couleur)
                           .arg(std::lround(100 * b.reserve))
                           .arg(b.horsPlaque > 0 ? QStringLiteral("<br><span style='color:#C2402E'>%1 appareil(s) hors plaque ou superposé(s)</span>").arg(b.horsPlaque) : QString()));
  t->setWordWrap(true);
  v->addWidget(t);
  for (const auto& r : b.remarques) {
    auto* l = new QLabel(QStringLiteral("• %1").arg(q(r).toHtmlEscaped()));
    l->setWordWrap(true);
    v->addWidget(l);
  }
  col->addWidget(gb);
  // Bilan thermique (CEI/TR 60890) : où est l'armoire, quelles températures, quelle charge ; ce qui chauffe
  FormulaireLie fl(this, col);
  QFormLayout* ft = fl.groupe(QStringLiteral("Échauffement"), QStringLiteral("groupeThermique"));
  auto modifierArmoire = [this, ai](const std::function<void(prom::elec::Armoire&)>& f) {
    modifierProjet([&](ProjetElectrique& y) {
      if (ai < static_cast<int>(y.armoires.size())) f(y.armoires[static_cast<std::size_t>(ai)]);
    });
  };
  QStringList poses;
  for (const auto& x : posesArmoire()) poses << q(x);
  fl.choix(ft, QStringLiteral("Pose"), poses, a.pose, QStringLiteral("poseArmoire"), [modifierArmoire](int i) { modifierArmoire([i](prom::elec::Armoire& x) { x.pose = i; }); },
           QStringLiteral("plus l’armoire est enclavée, moins ses parois évacuent la chaleur"));
  fl.reel(ft, QStringLiteral("Air ambiant"), a.temperatureExterieure, -30, 60, 0, QStringLiteral(" °C"), QStringLiteral("temperatureExterieure"),
          [modifierArmoire](double t) { modifierArmoire([t](prom::elec::Armoire& x) { x.temperatureExterieure = t; }); }, QStringLiteral("température maximale du local"));
  fl.reel(ft, QStringLiteral("Admis dedans"), a.temperatureAdmise, 20, 70, 0, QStringLiteral(" °C"), QStringLiteral("temperatureAdmise"),
          [modifierArmoire](double t) { modifierArmoire([t](prom::elec::Armoire& x) { x.temperatureAdmise = t; }); },
          QStringLiteral("appareillage courant : 40 à 55 °C ; électronique, variateurs : 35 à 45 °C"));
  fl.entier(ft, QStringLiteral("Charge"), static_cast<int>(std::lround(a.charge * 100)), 10, 100, QStringLiteral("chargeArmoire"),
            [modifierArmoire](int c) { modifierArmoire([c](prom::elec::Armoire& x) { x.charge = c / 100.0; }); },
            QStringLiteral("courant moyen des départs, en part de leur courant assigné (facteur de diversité)"))
      ->setSuffix(QStringLiteral(" %"));
  const BilanThermique th = bilanThermique(pe, static_cast<std::size_t>(ai));
  const bool chaud = th.ventilation > 0 || th.climatisation > 0;
  auto* gth = new QGroupBox(QStringLiteral("Bilan thermique"));
  gth->setObjectName(QStringLiteral("bilanThermique"));
  auto* vth = new QVBoxLayout(gth);
  auto* res = new QLabel(QStringLiteral("<b>%1 W</b> dissipés · <b style='color:%2'>%3 °C</b> dedans<br><span style='color:#4A5270'>échauffement %4 K, surface d’échange %5 m²</span>")
                             .arg(QLocale().toString(th.dissipee, 'f', 0), chaud ? QStringLiteral("#C2402E") : QStringLiteral("#2E8B57"),
                                  QLocale().toString(th.temperatureInterieure, 'f', 0), QLocale().toString(th.echauffement, 'f', 1), QLocale().toString(th.surface, 'f', 2)));
  res->setObjectName(QStringLiteral("resultatThermique"));
  res->setWordWrap(true);
  vth->addWidget(res);
  for (const auto& r : th.remarques) {
    auto* l = new QLabel(QStringLiteral("• %1").arg(q(r).toHtmlEscaped()));
    l->setWordWrap(true);
    vth->addWidget(l);
  }
  if (!th.pertes.empty()) {
    QString t = QStringLiteral("<span style='color:#6B7393'>Ce qui chauffe le plus :</span>");
    for (std::size_t i = 0; i < std::min<std::size_t>(5, th.pertes.size()); ++i)
      t += QStringLiteral("<br>%1 · %2 W").arg(q(th.pertes[i].repere).toHtmlEscaped(), QLocale().toString(th.pertes[i].puissance, 'f', 1));
    auto* l = new QLabel(t);
    l->setWordWrap(true);
    vth->addWidget(l);
  }
  col->addWidget(gth);
}

void AtelierElectrique::panneauFaisceau(QVBoxLayout* col) {
  const ProjetElectrique& pe = *projetCourant();
  {
    // 0.34 : schéma de câblage du faisceau (connecteurs de carte, câbles, contacts), planche, longueurs, fabrication
    auto* c = new QPushButton(icone(QStringLiteral("cablage")), QStringLiteral("Câblage, planche et fabrication…"));
    c->setObjectName(QStringLiteral("ouvrirCablage"));
    c->setToolTip(QStringLiteral("Fils numérotés, câbles blindés et paires, connecteurs de carte, contacts ; planche à l’échelle 1 ; longueurs 3D et chutes de tension ; plan, classeur, machines, devis"));
    connect(c, &QPushButton::clicked, this, [this] { cablage(); });
    col->addWidget(c);
  }
  const int fi = m_faisceau->faisceau();
  if (fi < 0 || fi >= static_cast<int>(pe.faisceaux.size())) {
    auto* b = new QPushButton(icone(QStringLiteral("faisceau")), QStringLiteral("Créer un faisceau"));
    b->setObjectName(QStringLiteral("principal"));
    connect(b, &QPushButton::clicked, this, [this] { nouveauFaisceau(); });
    col->addWidget(b);
    return;
  }
  const prom::elec::Faisceau& fx = pe.faisceaux[static_cast<std::size_t>(fi)];
  const std::string sel = m_faisceau->selection();
  auto modifierFaisceau = [this, fi](const std::function<void(prom::elec::Faisceau&)>& f) {
    modifierProjet([&](ProjetElectrique& y) {
      if (fi < static_cast<int>(y.faisceaux.size())) f(y.faisceaux[static_cast<std::size_t>(fi)]);
    });
  };
  if (!sel.empty() && sel.rfind("b:", 0) == 0) {
    const std::size_t i = static_cast<std::size_t>(std::stoul(sel.substr(2)));
    if (i < fx.branches.size()) {
      auto* g = new QGroupBox(QStringLiteral("Branche"));
      auto* f = new QFormLayout(g);
      auto* l = new QDoubleSpinBox;
      l->setObjectName(QStringLiteral("longueurBranche"));
      l->setRange(0, 100000);
      l->setSuffix(QStringLiteral(" mm"));
      l->setSpecialValueText(QStringLiteral("dessinée (%1 mm)").arg(std::lround(longueurBranche(fx, BrancheFaisceau{fx.branches[i].a, fx.branches[i].b, 0, {}}))));
      l->setValue(fx.branches[i].longueur);
      connect(l, &QDoubleSpinBox::editingFinished, this, [l, i, modifierFaisceau] { modifierFaisceau([&](prom::elec::Faisceau& y) { if (i < y.branches.size()) y.branches[i].longueur = l->value(); }); });
      f->addRow(QStringLiteral("Longueur"), l);
      auto* protection = new QComboBox;
      protection->setEditable(true);
      protection->addItems({QString(), QStringLiteral("Gaine annelée"), QStringLiteral("Gaine tressée"), QStringLiteral("Ruban adhésif"), QStringLiteral("Gaine thermorétractable")});
      protection->setCurrentText(q(fx.branches[i].protection));
      connect(protection, &QComboBox::currentTextChanged, this, [i, modifierFaisceau](const QString& t) { modifierFaisceau([&](prom::elec::Faisceau& y) { if (i < y.branches.size()) y.branches[i].protection = s(t); }); });
      f->addRow(QStringLiteral("Protection"), protection);
      col->addWidget(g);
    }
  } else if (!sel.empty()) {
    for (const auto& n : fx.noeuds) {
      if (n.uuid != sel) continue;
      auto* g = new QGroupBox(QStringLiteral("Nœud %1").arg(q(n.nom)));
      auto* f = new QFormLayout(g);
      auto* nom = new QLineEdit(q(n.nom));
      connect(nom, &QLineEdit::editingFinished, this, [nom, sel, modifierFaisceau] {
        modifierFaisceau([&](prom::elec::Faisceau& y) {
          for (auto& z : y.noeuds)
            if (z.uuid == sel) z.nom = s(nom->text());
        });
      });
      f->addRow(QStringLiteral("Nom"), nom);
      auto* connecteur = new QComboBox;
      connecteur->setObjectName(QStringLiteral("connecteurNoeud"));
      connecteur->addItem(QStringLiteral("— dérivation —"), QString());
      for (const auto& c : pe.schema.composants)
        if (roleSymbole(c.symbole) == RoleSymbole::Borne || encombrement(pe, c.uuid).horsArmoire)
          connecteur->addItem(QStringLiteral("%1 — %2").arg(q(repereAffiche(pe, c)), q(c.description)), q(c.uuid));
      connecteur->setCurrentIndex(std::max(0, connecteur->findData(q(n.composant))));
      connect(connecteur, &QComboBox::currentIndexChanged, this, [connecteur, sel, modifierFaisceau](int) {
        modifierFaisceau([&](prom::elec::Faisceau& y) {
          for (auto& z : y.noeuds)
            if (z.uuid == sel) z.composant = s(connecteur->currentData().toString());
        });
      });
      f->addRow(QStringLiteral("Connecteur"), connecteur);
      col->addWidget(g);
    }
  }
  // Le faisceau et ses fils
  auto* g = new QGroupBox(QStringLiteral("Faisceau"));
  auto* f = new QFormLayout(g);
  auto* nom = new QLineEdit(q(fx.nom));
  connect(nom, &QLineEdit::editingFinished, this, [nom, modifierFaisceau] {
    if (!nom->text().trimmed().isEmpty()) modifierFaisceau([&](prom::elec::Faisceau& y) { y.nom = s(nom->text()); });
  });
  f->addRow(QStringLiteral("Nom"), nom);
  auto* sur = new QDoubleSpinBox;
  sur->setRange(0, 1000);
  sur->setSuffix(QStringLiteral(" mm"));
  sur->setValue(fx.surlongueur);
  connect(sur, &QDoubleSpinBox::editingFinished, this, [sur, modifierFaisceau] { modifierFaisceau([&](prom::elec::Faisceau& y) { y.surlongueur = sur->value(); }); });
  f->addRow(QStringLiteral("Surlongueur par bout"), sur);
  col->addWidget(g);
  const auto fils = filsFaisceau(pe, static_cast<std::size_t>(fi));
  auto* gf = new QGroupBox(QStringLiteral("Fils du faisceau (%1)").arg(fils.size()));
  gf->setObjectName(QStringLiteral("filsFaisceau"));
  auto* v = new QVBoxLayout(gf);
  if (fils.empty()) {
    auto* l = new QLabel(QStringLiteral("<span style='color:#6B7393'>Donne un connecteur du schéma à au moins deux nœuds reliés : les fils qui vont de l’un à l’autre apparaissent ici avec leur longueur.</span>"));
    l->setWordWrap(true);
    v->addWidget(l);
  }
  for (const auto& w : fils) {
    auto* l = new QLabel(QStringLiteral("<b>%1</b> %2 → %3 · %4 mm · %5 mm² %6")
                             .arg(q(w.fil.empty() ? std::string("?") : w.fil), q(w.de), q(w.vers))
                             .arg(std::lround(w.longueur))
                             .arg(QLocale().toString(w.section))
                             .arg(q(w.couleur)));
    l->setWordWrap(true);
    v->addWidget(l);
  }
  col->addWidget(gf);
}

VueSchema* AtelierElectrique::vueSchema() const { return m_schema; }
