// Prométhée : atelier de conception de pièces. Licence GPL-3.0-only.
#include "AtelierPiece.hpp"
#include "DialogueImportDxf.hpp"
#include "Theme.hpp"

#include <cstdio>
#include <functional>
#include <map>
#include <utility>
#include <numbers>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontMetrics>
#include <QDateTime>
#include <QFormLayout>
#include <QTimer>
#include <QSpinBox>
#include <QSlider>
#include <QColorDialog>
#include <QActionGroup>
#include <QFrame>
#include <QPixmap>
#include <QPainter>
#include <QProgressBar>
#include <QListWidget>
#include <QShortcut>
#include <QTableWidget>
#include <QDialogButtonBox>
#include <QDialog>
#include <QTabBar>
#include <QTabWidget>
#include <functional>
#include <filesystem>
#include <QLocale>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QColorDialog>
#include <QMenu>
#include <QRegularExpression>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <TopoDS.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <gp_Pln.hxx>
#include <gp_Lin.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <gp_Circ.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <GProp_GProps.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <TopoDS_Compound.hxx>
#include <BRepGProp.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <TopExp_Explorer.hxx>
#include <Standard_Failure.hxx>

#include "ChampNombre.hpp"
#include "ChampValeur.hpp"
#include "PanneauLateral.hpp"
#include "Document.hpp"
#include "Esquisseur.hpp"
#include "EtiquetteEtat.hpp"
#include "Icones.hpp"
#include "RemplacerPage.hpp"
#include "MiseEnPlan.hpp"
#include "Navigateur.hpp"
#include "VuePiece.hpp"
#include "promethee/depot.hpp"
#include "promethee/fabrication.hpp"
#include "promethee/echanges.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/import_cao.hpp"
#include "promethee/mesure.hpp"
#include "promethee/cotes_modele.hpp"

using namespace prom;

namespace {
// Feuille de style des panneaux de réglage (fonctions et commandes d'esquisse) : sections arrondies sur fond clair,
// champs de sélection en cartouche, actif en bleu.
const char* STYLE_PANNEAU =
    "#panneauProprietes { background: #FAFBFE; }"
    "#panneauProprietes QGroupBox { background: #F3F5FB; border: 1px solid #E1E5F0; border-radius: 9px; margin-top: 16px; padding: 12px 8px 8px 8px; }"
    "#panneauProprietes QGroupBox::title { subcontrol-origin: margin; left: 10px; top: 2px; padding: 0 4px; color: #6B7393; font-weight: 600; }"
    "#panneauProprietes QPushButton#champ { text-align: left; padding: 7px 10px; border: 1px solid #CDD3E4; border-radius: 7px; background: white; color: #232A45; }"
    "#panneauProprietes QPushButton#champ:hover { border-color: #6E9BFF; }"
    "#panneauProprietes QPushButton#champActif { text-align: left; padding: 7px 10px; border: 2px solid #2F6BF5; border-radius: 7px; background: #EDF2FF; color: #1B2F7A; font-weight: 600; }"
    "#panneauProprietes QPushButton#valider { background: #2E8B57; color: white; border: none; border-radius: 7px; padding: 7px 14px; font-weight: 600; }"
    "#panneauProprietes QPushButton#valider:hover { background: #267A4C; }"
    "#panneauProprietes QPushButton#abandonner { background: white; color: #C2402E; border: 1px solid #E3B5AE; border-radius: 7px; padding: 7px 14px; font-weight: 600; }"
    "#panneauProprietes QListWidget { border: 2px solid #2F6BF5; border-radius: 7px; background: #EDF2FF; }"
    "#panneauProprietes QListWidget#inactive { border: 1px solid #CDD3E4; background: white; }"
    "#panneauProprietes QLabel#consigne { background: #EDF2FF; color: #1B2F7A; border-radius: 7px; padding: 7px 9px; }";

// En-tête d'un panneau : icône et titre, puis les boutons OK et Annuler côte à côte.
QHBoxLayout* enTetePanneau(QVBoxLayout* col, const QString& icone_, const QString& titre, QPushButton*& ok, QPushButton*& annuler, bool avecOk = true) {
  auto* haut = new QHBoxLayout;
  auto* ic = new QLabel;
  ic->setPixmap(icone(icone_).pixmap(26, 26));
  haut->addWidget(ic);
  auto* t = new QLabel(QStringLiteral("<span style='font-size:15px; font-weight:700; color:#10142A'>%1</span>").arg(titre.toHtmlEscaped()));
  t->setWordWrap(true);
  haut->addWidget(t, 1);
  col->addLayout(haut);
  auto* boutons = new QHBoxLayout;
  ok = nullptr;
  if (avecOk) {
    ok = new QPushButton(QStringLiteral("✔ OK"));
    ok->setObjectName(QStringLiteral("valider"));
    boutons->addWidget(ok);
  }
  annuler = new QPushButton(avecOk ? QStringLiteral("✖ Annuler") : QStringLiteral("Fermer"));
  annuler->setObjectName(QStringLiteral("abandonner"));
  boutons->addWidget(annuler);
  boutons->addStretch(1);
  col->addLayout(boutons);
  return haut;
}
// Icône d'une fonction, la même dans l'arbre et en tête de son panneau.
QString iconeFonction(const FonctionPiece& f) {
  switch (f.type) {
    case TypeFonction::Esquisse: return QStringLiteral("esquisse");
    case TypeFonction::Extrusion: return f.enlever ? QStringLiteral("enlevement") : QStringLiteral("extrusion");
    case TypeFonction::Revolution: return QStringLiteral("revolution");
    case TypeFonction::Conge: return QStringLiteral("conge");
    case TypeFonction::Chanfrein: return QStringLiteral("chanfrein");
    case TypeFonction::Coque: return QStringLiteral("coque");
    case TypeFonction::Plan: return QStringLiteral("plans");
    case TypeFonction::Axe: return QStringLiteral("axe");
    case TypeFonction::Percage: return QStringLiteral("percage");
    case TypeFonction::Depouille: return QStringLiteral("depouille");
    case TypeFonction::Repetition: return f.typeRepetition == "circulaire" ? QStringLiteral("repetition-circulaire") : QStringLiteral("repetition-lineaire");
    case TypeFonction::Symetrie: return QStringLiteral("symetrie");
    case TypeFonction::Balayage: return QStringLiteral("balayage");
    case TypeFonction::Lissage: return QStringLiteral("lissage");
    case TypeFonction::Esquisse3D: return QStringLiteral("esquisse3d");
    case TypeFonction::Import: return QStringLiteral("ouvrir");
    case TypeFonction::Tole: return QStringLiteral("tolerie");
    case TypeFonction::BordTombe: return QStringLiteral("bord-tombe");
    case TypeFonction::Nervure: return QStringLiteral("nervure");
    case TypeFonction::Filetage: return QStringLiteral("filetage");
    default: return QStringLiteral("point");
  }
}
}  // namespace

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
}  // namespace

AtelierPiece::AtelierPiece(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  auto* colonne = new QVBoxLayout(this);
  colonne->setContentsMargins(0, 0, 0, 0);
  colonne->setSpacing(0);

  // ---------- Zone de commandes : la pièce en cours, puis les commandes rangées par catégorie ----------
  m_outilsPiece = new QWidget;
  auto* zone = new QVBoxLayout(m_outilsPiece);
  zone->setContentsMargins(8, 4, 8, 0);
  zone->setSpacing(2);
  // La pièce en cours, ses propriétés et le filtre de sélection tiennent dans le coin de la barre d'onglets.
  auto* coin = new QWidget;
  auto* ligne = new QHBoxLayout(coin);
  ligne->setContentsMargins(0, 0, 4, 0);
  ligne->setSpacing(4);
  m_choixPiece = new QComboBox;
  m_choixPiece->setMinimumWidth(140);
  m_choixPiece->setToolTip(QStringLiteral("Pièce en cours"));
  ligne->addWidget(m_choixPiece);
  m_choixPiece->hide();  // les pièces se choisissent dans le navigateur du projet
  auto* nouvelle = new QToolButton;
  nouvelle->setIcon(icone(QStringLiteral("nouvelle-piece")));
  nouvelle->setIconSize(QSize(20, 20));
  nouvelle->setToolTip(QStringLiteral("Nouvelle pièce"));
  connect(nouvelle, &QToolButton::clicked, this, [this] { nouvellePiece(); });
  ligne->addWidget(nouvelle);
  auto* proprietes = new QToolButton;
  proprietes->setIcon(icone(QStringLiteral("proprietes")));
  proprietes->setIconSize(QSize(20, 20));
  proprietes->setToolTip(QStringLiteral("Propriétés de la pièce : unités, matériau, densité"));
  connect(proprietes, &QToolButton::clicked, this, [this] { proprietesPiece(); });
  ligne->addWidget(proprietes);
  auto* modeSel = new QComboBox;
  modeSel->setObjectName(QStringLiteral("modeSelection"));
  modeSel->addItems({QStringLiteral("Sélection : tout"), QStringLiteral("Sélection : faces"), QStringLiteral("Sélection : arêtes"), QStringLiteral("Sélection : sommets")});
  modeSel->setToolTip(QStringLiteral("Filtre de sélection dans la vue 3D"));
  ligne->addWidget(modeSel);
  modeSel->hide();  // remplacé par les icônes de la bande de la vue
  // Commandes à la manière des grands logiciels : icône au-dessus du nom ; un bouton à variantes lance la
  // dernière utilisée, et sa flèche déroule les autres façons de faire.
  struct Variante {
    QString icone, texte, bulle;
    std::function<void()> action;
  };
  auto commande = [this](QToolBar* barre, const QString& titre, const std::vector<Variante>& variantes) {
    auto* b = new QToolButton;
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setIconSize(QSize(30, 30));
    b->setAutoRaise(true);
    b->setMinimumWidth(std::max(62, QFontMetrics(b->font()).horizontalAdvance(titre) + (variantes.size() > 1 ? 30 : 16)));
    std::vector<QAction*> actions;
    for (const auto& v : variantes) {
      auto* act = new QAction(icone(v.icone), v.texte, b);
      act->setIconText(titre);
      act->setToolTip(v.bulle.isEmpty() ? v.texte : v.texte + QStringLiteral(" — ") + v.bulle);
      const auto fn = v.action;
      connect(act, &QAction::triggered, this, [fn] { fn(); });
      actions.push_back(act);
    }
    b->setDefaultAction(actions.front());
    if (actions.size() > 1) {
      auto* menu = new QMenu(b);
      for (QAction* act : actions) menu->addAction(act);
      b->setMenu(menu);
      b->setPopupMode(QToolButton::MenuButtonPopup);
      connect(menu, &QMenu::triggered, b, [b](QAction* act) { b->setDefaultAction(act); });
    }
    barre->addWidget(b);
    return b;
  };
  m_onglets = new QTabWidget;
  m_onglets->setDocumentMode(true);
  auto onglet = [this](const QString& titre) {
    auto* barre = new QToolBar;
    barre->setIconSize(QSize(30, 30));
    m_onglets->addTab(barre, titre);
    return barre;
  };
  QToolBar* bEsquisse = onglet(QStringLiteral("Esquisse"));
  commande(bEsquisse, QStringLiteral("Esquisse"), {{QStringLiteral("esquisse"), QStringLiteral("Nouvelle esquisse"), QStringLiteral("clique un plan ou une face plane, puis ce bouton ; ou ce bouton, puis le support"), [this] { commandeEsquisse(); }}});
  // Groupe compact : petites icônes sur deux rangées, légende dessous ; chaque icône est une action (bulle, raccourci,
  // palette Ctrl+K), sans menu à dérouler.
  auto grille = [this](QToolBar* barre, const QString& legende, const std::vector<Variante>& items, int colonnes) {
    auto* boite = new QWidget;
    auto* v = new QVBoxLayout(boite);
    v->setContentsMargins(4, 0, 4, 0);
    v->setSpacing(1);
    auto* g = new QGridLayout;
    g->setSpacing(1);
    for (size_t k = 0; k < items.size(); ++k) {
      auto* act = new QAction(icone(items[k].icone), items[k].texte, boite);
      act->setToolTip(items[k].bulle.isEmpty() ? items[k].texte : QStringLiteral("<b>%1</b><br>%2").arg(items[k].texte, items[k].bulle));
      act->setStatusTip(items[k].bulle.isEmpty() ? items[k].texte : items[k].texte + QStringLiteral(" — ") + items[k].bulle);
      const auto f = items[k].action;
      connect(act, &QAction::triggered, this, [f] { f(); });
      auto* b = new QToolButton;
      b->setDefaultAction(act);
      b->setIconSize(QSize(20, 20));
      b->setAutoRaise(true);
      b->setToolButtonStyle(Qt::ToolButtonIconOnly);
      g->addWidget(b, static_cast<int>(k) % 2, static_cast<int>(k) / 2);
    }
    (void)colonnes;
    v->addLayout(g);
    auto* l = new QLabel(legende);
    l->setAlignment(Qt::AlignHCenter);
    l->setStyleSheet(QStringLiteral("color: #4A5270; font-size: 11px;"));
    v->addWidget(l);
    barre->addWidget(boite);
  };
  auto grilleV = [this](QToolBar* barre, const QString& legende, const std::vector<std::vector<Variante>>& cases) {
    auto* boite = new QWidget;
    auto* v = new QVBoxLayout(boite);
    v->setContentsMargins(4, 0, 4, 0);
    v->setSpacing(1);
    auto* g = new QGridLayout;
    g->setSpacing(1);
    for (size_t k = 0; k < cases.size(); ++k) {
      auto* b = new QToolButton;
      b->setIconSize(QSize(20, 20));
      b->setAutoRaise(true);
      QMenu* menu = cases[k].size() > 1 ? new QMenu(b) : nullptr;
      for (size_t j = 0; j < cases[k].size(); ++j) {
        const Variante& x = cases[k][j];
        auto* act = new QAction(icone(x.icone), x.texte, boite);
        act->setToolTip(x.bulle.isEmpty() ? x.texte : QStringLiteral("<b>%1</b><br>%2").arg(x.texte, x.bulle));
        act->setStatusTip(x.bulle.isEmpty() ? x.texte : x.texte + QStringLiteral(" — ") + x.bulle);
        const auto f = x.action;
        connect(act, &QAction::triggered, this, [f] { f(); });
        if (j == 0) b->setDefaultAction(act);
        if (menu) menu->addAction(act);
      }
      if (menu) {
        b->setMenu(menu);
        b->setPopupMode(QToolButton::MenuButtonPopup);
        // La dernière variante choisie devient celle du clic sur l'icône (AP-26)
        connect(menu, &QMenu::triggered, b, [b](QAction* act) { b->setDefaultAction(act); });
      }
      g->addWidget(b, static_cast<int>(k) % 2, static_cast<int>(k) / 2);
    }
    v->addLayout(g);
    auto* l = new QLabel(legende);
    l->setAlignment(Qt::AlignHCenter);
    l->setStyleSheet(QStringLiteral("color: #4A5270; font-size: 11px;"));
    v->addWidget(l);
    barre->addWidget(boite);
  };
  QToolBar* bFonctions = onglet(QStringLiteral("Fonctions"));
  commande(bFonctions, QStringLiteral("Extrusion"),
           {{QStringLiteral("extrusion"), QStringLiteral("Extrusion borgne"), QStringLiteral("sur une distance"), [this] { nouvelleFonction(TypeFonction::Extrusion, false, "borgne"); }},
            {QStringLiteral("extrusion-milieu"), QStringLiteral("Extrusion en plan milieu"), QStringLiteral("symétrique de part et d’autre du plan d’esquisse"), [this] { nouvelleFonction(TypeFonction::Extrusion, false, "milieu"); }},
            {QStringLiteral("extrusion-traversant"), QStringLiteral("Extrusion à travers tout"), QString(), [this] { nouvelleFonction(TypeFonction::Extrusion, false, "traversant"); }},
            {QStringLiteral("extrusion-face"), QStringLiteral("Extrusion jusqu’à une face"), QStringLiteral("la fonction épouse la face choisie"), [this] { nouvelleFonction(TypeFonction::Extrusion, false, "face"); }},
            {QStringLiteral("extrusion-mince"), QStringLiteral("Extrusion mince"), QStringLiteral("paroi d’épaisseur constante le long du contour, ouvert ou fermé"), [this] { nouvelleFonction(TypeFonction::Extrusion, false, "mince"); }}});
  commande(bFonctions, QStringLiteral("Enlèvement"),
           {{QStringLiteral("enlevement"), QStringLiteral("Enlèvement borgne"), QStringLiteral("poche d’une profondeur donnée"), [this] { nouvelleFonction(TypeFonction::Extrusion, true, "borgne"); }},
            {QStringLiteral("enlevement-traversant"), QStringLiteral("Enlèvement à travers tout"), QString(), [this] { nouvelleFonction(TypeFonction::Extrusion, true, "traversant"); }},
            {QStringLiteral("enlevement-face"), QStringLiteral("Enlèvement jusqu’à une face"), QString(), [this] { nouvelleFonction(TypeFonction::Extrusion, true, "face"); }},
            {QStringLiteral("extrusion-mince"), QStringLiteral("Enlèvement mince"), QStringLiteral("rainure d’épaisseur constante le long du contour"), [this] { nouvelleFonction(TypeFonction::Extrusion, true, "mince"); }}});
  commande(bFonctions, QStringLiteral("Esquisse 3D"), {{QStringLiteral("esquisse3d"), QStringLiteral("Esquisse 3D"), QStringLiteral("points dans l’espace, segments ou courbe lisse : chemin de balayage"), [this] { nouvelleFonction(TypeFonction::Esquisse3D, false, ""); }}});
  commande(bFonctions, QStringLiteral("Balayage"),
           {{QStringLiteral("balayage"), QStringLiteral("Balayage"), QStringLiteral("un profil le long d’un chemin"), [this] { nouvelleFonction(TypeFonction::Balayage, false, ""); }},
            {QStringLiteral("balayage"), QStringLiteral("Tube le long d’un chemin"), QStringLiteral("profil circulaire automatique, plein ou creux"), [this] { nouvelleFonction(TypeFonction::Balayage, false, "tube"); }},
            {QStringLiteral("balayage"), QStringLiteral("Enlèvement par balayage"), QString(), [this] { nouvelleFonction(TypeFonction::Balayage, true, ""); }}});
  commande(bFonctions, QStringLiteral("Lissage"),
           {{QStringLiteral("lissage"), QStringLiteral("Lissage"), QStringLiteral("forme passant par plusieurs profils"), [this] { nouvelleFonction(TypeFonction::Lissage, false, ""); }},
            {QStringLiteral("lissage"), QStringLiteral("Enlèvement par lissage"), QString(), [this] { nouvelleFonction(TypeFonction::Lissage, true, ""); }}});
  commande(bFonctions, QStringLiteral("Révolution"),
           {{QStringLiteral("revolution"), QStringLiteral("Révolution"), QStringLiteral("autour d’une ligne de l’esquisse ou d’un axe X, Y, Z"), [this] { nouvelleFonction(TypeFonction::Revolution, false, ""); }},
            {QStringLiteral("revolution"), QStringLiteral("Révolution en plan milieu"), QString(), [this] { nouvelleFonction(TypeFonction::Revolution, false, "milieu"); }},
            {QStringLiteral("revolution-enlevement"), QStringLiteral("Enlèvement par révolution"), QString(), [this] { nouvelleFonction(TypeFonction::Revolution, true, ""); }}});
  bFonctions->addSeparator();
  grilleV(bFonctions, QStringLiteral("Modifier"),
          {{{QStringLiteral("conge"), QStringLiteral("Congé"), QStringLiteral("arrondit des arêtes ; clique ensuite les arêtes ou les faces"), [this] { nouvelleFonction(TypeFonction::Conge, false, ""); }}},
           {{QStringLiteral("chanfrein"), QStringLiteral("Chanfrein à distances égales"), QStringLiteral("même retrait sur les deux faces"), [this] { nouvelleFonction(TypeFonction::Chanfrein, false, "egal"); }},
            {QStringLiteral("chanfrein"), QStringLiteral("Chanfrein à deux distances"), QString(), [this] { nouvelleFonction(TypeFonction::Chanfrein, false, "distances"); }},
            {QStringLiteral("chanfrein"), QStringLiteral("Chanfrein distance et angle"), QString(), [this] { nouvelleFonction(TypeFonction::Chanfrein, false, "angle"); }}},
           {{QStringLiteral("coque"), QStringLiteral("Coque"), QStringLiteral("évide la pièce ; clique ensuite les faces à ouvrir"), [this] { nouvelleFonction(TypeFonction::Coque, false, ""); }}},
           {{QStringLiteral("depouille"), QStringLiteral("Dépouille"), QStringLiteral("incline des faces pour le démoulage"), [this] { nouvelleFonction(TypeFonction::Depouille, false, ""); }}},
           {{QStringLiteral("nervure"), QStringLiteral("Nervure"), QStringLiteral("raidisseur depuis un trait ouvert d’esquisse, jusqu’à la pièce"), [this] { nouvelleFonction(TypeFonction::Nervure, false, "parallele"); }},
            {QStringLiteral("nervure"), QStringLiteral("Nervure normale au plan"), QStringLiteral("le trait est épaissi puis poussé jusqu’à la pièce"), [this] { nouvelleFonction(TypeFonction::Nervure, false, "normale"); }}}});
  bFonctions->addSeparator();
  commande(bFonctions, QStringLiteral("Perçage"),
           {{QStringLiteral("percage"), QStringLiteral("Perçage simple"), QStringLiteral("trous aux points d’une esquisse"), [this] { nouvelleFonction(TypeFonction::Percage, false, "simple"); }},
            {QStringLiteral("percage"), QStringLiteral("Perçage taraudé"), QStringLiteral("ISO ou ANSI"), [this] { nouvelleFonction(TypeFonction::Percage, false, "taraude"); }},
            {QStringLiteral("percage"), QStringLiteral("Perçage lamé"), QStringLiteral("pour vis à tête cylindrique"), [this] { nouvelleFonction(TypeFonction::Percage, false, "lame"); }},
            {QStringLiteral("percage"), QStringLiteral("Perçage fraisé"), QStringLiteral("pour vis à tête fraisée"), [this] { nouvelleFonction(TypeFonction::Percage, false, "fraise"); }}});
  commande(bFonctions, QStringLiteral("Filetage"),
           {{QStringLiteral("filetage"), QStringLiteral("Filetage cosmétique"), QStringLiteral("sur un trou ou un arbre : montré en 3D, dessiné et indiqué en mise en plan"), [this] { nouvelleFonction(TypeFonction::Filetage, false, ""); }}});
  grilleV(bFonctions, QStringLiteral("Répéter"),
          {{{QStringLiteral("repetition-lineaire"), QStringLiteral("Répétition linéaire de fonctions"), QStringLiteral("une ou deux directions"), [this] { nouvelleFonction(TypeFonction::Repetition, false, "lineaire"); }},
            {QStringLiteral("repetition-lineaire"), QStringLiteral("Répétition de fonctions pilotée par une esquisse"), QStringLiteral("une copie par point"), [this] { nouvelleFonction(TypeFonction::Repetition, false, "esquisse"); }}},
           {{QStringLiteral("repetition-circulaire"), QStringLiteral("Répétition circulaire de fonctions"), QStringLiteral("autour d’un axe"), [this] { nouvelleFonction(TypeFonction::Repetition, false, "circulaire"); }}},
           {{QStringLiteral("symetrie"), QStringLiteral("Symétrie de fonctions"), QStringLiteral("de fonctions ou de tout le corps, par rapport à un plan"), [this] { nouvelleFonction(TypeFonction::Symetrie, false, ""); }}}});
  QToolBar* bReference = onglet(QStringLiteral("Géométrie de référence"));
  commande(bReference, QStringLiteral("Plan"),
           {{QStringLiteral("plans"), QStringLiteral("Plan décalé"), QStringLiteral("d’un plan ou d’une face"), [this] { nouvelleFonction(TypeFonction::Plan, false, "decale"); }},
            {QStringLiteral("plans"), QStringLiteral("Plan incliné"), QStringLiteral("autour d’une arête ou d’un axe"), [this] { nouvelleFonction(TypeFonction::Plan, false, "angle"); }},
            {QStringLiteral("plans"), QStringLiteral("Plan par trois points"), QString(), [this] { nouvelleFonction(TypeFonction::Plan, false, "troisPoints"); }},
            {QStringLiteral("plans"), QStringLiteral("Plan parallèle par un point"), QString(), [this] { nouvelleFonction(TypeFonction::Plan, false, "parallelePoint"); }}});
  commande(bReference, QStringLiteral("Axe"),
           {{QStringLiteral("axe"), QStringLiteral("Axe d’une arête ou d’un cylindre"), QString(), [this] { nouvelleFonction(TypeFonction::Axe, false, "arete"); }},
            {QStringLiteral("axe"), QStringLiteral("Axe par deux points"), QString(), [this] { nouvelleFonction(TypeFonction::Axe, false, "deuxPoints"); }},
            {QStringLiteral("axe"), QStringLiteral("Axe à l’intersection de deux plans"), QString(), [this] { nouvelleFonction(TypeFonction::Axe, false, "deuxPlans"); }}});
  commande(bReference, QStringLiteral("Point"),
           {{QStringLiteral("point"), QStringLiteral("Point par coordonnées"), QString(), [this] { nouvelleFonction(TypeFonction::Point, false, "coordonnees"); }},
            {QStringLiteral("point"), QStringLiteral("Point sur un sommet"), QString(), [this] { nouvelleFonction(TypeFonction::Point, false, "sommet"); }},
            {QStringLiteral("point"), QStringLiteral("Point au centre"), QStringLiteral("d’un cercle ou d’une face"), [this] { nouvelleFonction(TypeFonction::Point, false, "centre"); }}});
  commande(bReference, QStringLiteral("Repère"),
           {{QStringLiteral("plans"), QStringLiteral("Repère"), QStringLiteral("origine, plan et axe X"), [this] { nouvelleFonction(TypeFonction::Repere, false, ""); }}});
  bReference->addSeparator();
  QAction* plans = bReference->addAction(icone(QStringLiteral("plans")), QStringLiteral("Afficher les plans de base"));
  m_actionPlans = plans;
  plans->setCheckable(true);
  if (QToolButton* tb = qobject_cast<QToolButton*>(bReference->widgetForAction(plans))) tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  connect(plans, &QAction::toggled, this, [this](bool on) { m_plansDemandes = on; majPlans(); });
  QToolBar* bEvaluer = onglet(QStringLiteral("Évaluer et exporter"));
  commande(bEvaluer, QStringLiteral("Mesurer"), {{QStringLiteral("cote"), QStringLiteral("Mesurer"), QStringLiteral("M : clique faces, arêtes, sommets ; distances, angles, aires, longueurs"), [this] { basculerMesure(); }}});
  commande(bEvaluer, QStringLiteral("Fabrication"), {{QStringLiteral("percage"), QStringLiteral("Vérifier la fabrication"), QStringLiteral("parois minces, petits rayons, trous profonds, angles intérieurs vifs"), [this] { verifierFabricationPiece(); }}});
  commande(bEvaluer, QStringLiteral("Variables"), {{QStringLiteral("proprietes"), QStringLiteral("Variables de la pièce"), QStringLiteral("nom = expression ; une cote qui cite une variable la suit"), [this] { variablesPiece(); }}});
  commande(bEvaluer, QStringLiteral("Plan"), {{QStringLiteral("plan"), QStringLiteral("Mise en plan"), QStringLiteral("vues normalisées sur une feuille, exports PDF, SVG, DXF"), [this] { miseEnPlan(); }},
                                              {QStringLiteral("plan"), QStringLiteral("Nouvelle mise en plan"), QStringLiteral("une feuille de plus pour cette pièce"), [this] { nouvelleMiseEnPlan(); }}});
  commande(bEvaluer, QStringLiteral("Coupe"), {{QStringLiteral("coupe"), QStringLiteral("Vue en coupe"), QStringLiteral("coupe la pièce par un plan réglable, matière en couleur"), [this] { vueEnCoupe(); }}});
  commande(bEvaluer, QStringLiteral("Masse"), {{QStringLiteral("masse"), QStringLiteral("Propriétés de masse"), QStringLiteral("volume, masse, centre de gravité, inerties"), [this] { afficherMasse(); }}});
  commande(bEvaluer, QStringLiteral("Recadrer"), {{QStringLiteral("recadrer"), QStringLiteral("Recadrer la vue"), QString(), [this] { m_vue->recadrer(); }}});
  commande(bEvaluer, QStringLiteral("Apparence"),
           {{QStringLiteral("couleur-face"), QStringLiteral("Couleur des faces…"), QStringLiteral("des faces choisies dans la vue ; gardée à l’export STEP et IGES"), [this] { choisirCouleurFaces(); }},
            {QStringLiteral("couleur-face"), QStringLiteral("Couleur d’origine des faces"), QStringLiteral("les faces choisies reprennent la couleur de la pièce"), [this] { colorerSelection(QString()); }}});
  commande(bEvaluer, QStringLiteral("Importer"), {{QStringLiteral("importer-cao"), QStringLiteral("Insérer un fichier CAO…"), QStringLiteral("STEP, IGES, BREP, STL : corps importé, avec son nom et ses couleurs"), [this] { insererFichierCAO(); }}});
  commande(bEvaluer, QStringLiteral("Exporter"), {{QStringLiteral("exporter"), QStringLiteral("Exporter la pièce…"), QStringLiteral("STEP AP214 ou AP242, STL, 3MF, IGES, OBJ, PLY, BREP"), [this] { exporter(); }}});
  QToolBar* bTole = onglet(QStringLiteral("Tôlerie"));
  commande(bTole, QStringLiteral("Tôle de base"),
           {{QStringLiteral("tolerie"), QStringLiteral("Tôle de base"), QStringLiteral("contour fermé d’une esquisse, épaisseur, rayon de pliage, facteur K"), [this] { nouvelleFonction(TypeFonction::Tole, false, ""); }},
            {QStringLiteral("esquisse"), QStringLiteral("Esquisser le contour de la tôle"), QStringLiteral("nouvelle esquisse sur le plan de dessus, la tôle suit à sa validation"), [this] { esquisserTole(); }}});
  commande(bTole, QStringLiteral("Bord tombé"),
           {{QStringLiteral("bord-tombe"), QStringLiteral("Bord tombé"), QStringLiteral("clique l’arête droite ou la face mince le long de laquelle plier"), [this] { nouvelleFonction(TypeFonction::BordTombe, false, ""); }}});
  bTole->addSeparator();
  {
    QAction* deplie = bTole->addAction(icone(QStringLiteral("deplie")), QStringLiteral("Déplié"));
    deplie->setObjectName(QStringLiteral("vueDepliee"));
    deplie->setCheckable(true);
    deplie->setToolTip(QStringLiteral("Montre la tôle à plat, avec ses lignes de pliage (vers le haut en rouge, vers le bas en bleu)"));
    if (QToolButton* tb = qobject_cast<QToolButton*>(bTole->widgetForAction(deplie))) tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    connect(deplie, &QAction::toggled, this, [this](bool on) { montrerDeplie(on); });
    m_actionDeplie = deplie;
  }
  commande(bTole, QStringLiteral("Exporter"),
           {{QStringLiteral("dxf"), QStringLiteral("Exporter le déplié en DXF…"), QStringLiteral("contour, lignes de pliage haut et bas, notes : pour la découpe laser ou la poinçonneuse"), [this] { exporterDeplie(QStringLiteral("dxf")); }},
            {QStringLiteral("exporter"), QStringLiteral("Exporter le déplié en SVG…"), QStringLiteral("à l’échelle 1, pour imprimer un gabarit"), [this] { exporterDeplie(QStringLiteral("svg")); }}});
  m_onglets->setCornerWidget(coin, Qt::TopRightCorner);
  m_onglets->setCurrentIndex(1);
  zone->addWidget(m_onglets);
  colonne->addWidget(m_outilsPiece);

  // ---------- Barre de l'esquisse : icônes et variantes ----------
  m_outilsEsquisse = new QToolBar;
  m_outilsEsquisse->setIconSize(QSize(28, 28));
  m_esq = new Esquisseur;
  auto outilV = [this](Esquisseur::Outil o) { return [this, o] { m_esq->setOutil(o); m_esq->setFocus(); }; };
  commande(m_outilsEsquisse, QStringLiteral("Sélection"), {{QStringLiteral("selection"), QStringLiteral("Sélection"), QStringLiteral("Échap"), outilV(Esquisseur::Outil::Selection)}});
  commande(m_outilsEsquisse, QStringLiteral("Ligne"),
           {{QStringLiteral("ligne"), QStringLiteral("Ligne"), QStringLiteral("L"), outilV(Esquisseur::Outil::Ligne)},
            {QStringLiteral("ligne-construction"), QStringLiteral("Ligne de construction"), QStringLiteral("K"), outilV(Esquisseur::Outil::LigneConstruction)},
            {QStringLiteral("ligne-milieu"), QStringLiteral("Ligne par le milieu"), QString(), outilV(Esquisseur::Outil::LigneMilieu)}});
  commande(m_outilsEsquisse, QStringLiteral("Rectangle"),
           {{QStringLiteral("rectangle"), QStringLiteral("Rectangle par deux coins"), QStringLiteral("R"), outilV(Esquisseur::Outil::Rectangle)},
            {QStringLiteral("rectangle-centre"), QStringLiteral("Rectangle par le centre"), QString(), outilV(Esquisseur::Outil::RectangleCentre)},
            {QStringLiteral("rectangle-3points"), QStringLiteral("Rectangle incliné (3 points)"), QString(), outilV(Esquisseur::Outil::Rectangle3Points)},
            {QStringLiteral("parallelogramme"), QStringLiteral("Parallélogramme"), QString(), outilV(Esquisseur::Outil::Parallelogramme)}});
  commande(m_outilsEsquisse, QStringLiteral("Cercle"),
           {{QStringLiteral("cercle"), QStringLiteral("Cercle par le centre"), QStringLiteral("C"), outilV(Esquisseur::Outil::Cercle)},
            {QStringLiteral("cercle-3points"), QStringLiteral("Cercle par trois points"), QString(), outilV(Esquisseur::Outil::Cercle3Points)}});
  commande(m_outilsEsquisse, QStringLiteral("Arc"),
           {{QStringLiteral("arc-centre"), QStringLiteral("Arc par le centre"), QStringLiteral("A"), outilV(Esquisseur::Outil::Arc)},
            {QStringLiteral("arc-3points"), QStringLiteral("Arc par trois points"), QStringLiteral("Maj+A"), outilV(Esquisseur::Outil::Arc3Points)},
            {QStringLiteral("arc-tangent"), QStringLiteral("Arc tangent"), QStringLiteral("depuis l’extrémité d’une ligne ou d’un arc"), outilV(Esquisseur::Outil::ArcTangent)}});
  // (formes en grille plus bas)
  m_outilsEsquisse->addSeparator();
  grilleV(m_outilsEsquisse, QStringLiteral("Formes"),
          {{{QStringLiteral("rainure"), QStringLiteral("Rainure droite"), QStringLiteral("O"), outilV(Esquisseur::Outil::Rainure)},
            {QStringLiteral("rainure"), QStringLiteral("Rainure en arc"), QStringLiteral("centre, début, fin, puis largeur"), outilV(Esquisseur::Outil::RainureArc)}},
           {{QStringLiteral("polygone"), QStringLiteral("Polygone régulier"), QStringLiteral("inscrit : centre puis un sommet (Maj+P)"), [this] { m_esq->setPolygoneCirconscrit(false); m_esq->setOutil(Esquisseur::Outil::Polygone); }},
            {QStringLiteral("polygone"), QStringLiteral("Polygone circonscrit"), QStringLiteral("centre puis milieu d’un côté"), [this] { m_esq->setPolygoneCirconscrit(true); m_esq->setOutil(Esquisseur::Outil::Polygone); }}},
           {{QStringLiteral("point"), QStringLiteral("Point"), QStringLiteral("P"), outilV(Esquisseur::Outil::Point)}},
           {{QStringLiteral("conge-esquisse"), QStringLiteral("Congé d’esquisse"), QStringLiteral("J : clic sur le coin, ou dans l’angle"), outilV(Esquisseur::Outil::Conge)},
            {QStringLiteral("chanfrein"), QStringLiteral("Chanfrein d’esquisse"), QStringLiteral("clic sur le coin, puis la distance"), outilV(Esquisseur::Outil::ChanfreinEsquisse)}}});
  grille(m_outilsEsquisse, QStringLiteral("Modifier"),
         {{QStringLiteral("ajuster"), QStringLiteral("Ajuster"), QStringLiteral("clique le morceau à retirer, entre deux intersections"), outilV(Esquisseur::Outil::Ajuster)},
          {QStringLiteral("prolonger"), QStringLiteral("Prolonger"), QStringLiteral("clique une ligne près de son extrémité"), outilV(Esquisseur::Outil::Prolonger)},
          {QStringLiteral("decaler"), QStringLiteral("Décaler"), QStringLiteral("cercles, chaînes de lignes et d’arcs"), [this] { ouvrirCommandeEsquisse(QStringLiteral("decaler")); }},
          {QStringLiteral("deplacer"), QStringLiteral("Déplacer"), QStringLiteral("translation des éléments ou d’une copie"), [this] { ouvrirCommandeEsquisse(QStringLiteral("deplacer")); }},
          {QStringLiteral("pivoter"), QStringLiteral("Pivoter"), QStringLiteral("autour de l’origine ou d’un point"), [this] { ouvrirCommandeEsquisse(QStringLiteral("pivoter")); }},
          {QStringLiteral("echelle"), QStringLiteral("Échelle"), QStringLiteral("les cotes suivent"), [this] { ouvrirCommandeEsquisse(QStringLiteral("echelle")); }}},
         3);
  grilleV(m_outilsEsquisse, QStringLiteral("Copier"),
          {{{QStringLiteral("symetrie"), QStringLiteral("Symétrie"), QStringLiteral("panneau : éléments, axe, aperçu"), [this] { ouvrirCommandeEsquisse(QStringLiteral("symetrie")); }}},
           {{QStringLiteral("repetition-lineaire"), QStringLiteral("Réseau linéaire"), QStringLiteral("panneau : éléments, direction, nombre, pas"), [this] { ouvrirCommandeEsquisse(QStringLiteral("repetition-lineaire")); }}},
           {{QStringLiteral("repetition-circulaire"), QStringLiteral("Réseau circulaire"), QStringLiteral("panneau : éléments, centre, nombre, angle"), [this] { ouvrirCommandeEsquisse(QStringLiteral("repetition-circulaire")); }}},
           {{QStringLiteral("convertir"), QStringLiteral("Convertir les entités"), QStringLiteral("arêtes de la pièce reprises dans le plan de l’esquisse"), [this] { convertirEntites(); }}},
           {{QStringLiteral("dxf"), QStringLiteral("Insérer un DXF…"), QStringLiteral("lignes, arcs, cercles, polylignes, ellipses, splines et blocs d’un dessin DXF"), [this] { insererDxf(); }}}});
  m_outilsEsquisse->addSeparator();
  commande(m_outilsEsquisse, QStringLiteral("Cote"), {{QStringLiteral("cote"), QStringLiteral("Cotation intelligente"), QStringLiteral("D : clique les éléments, place la cote, tape la valeur"), outilV(Esquisseur::Outil::Cote)}});
  auto relation = [this](TypeContrainte t) { return [this, t] { m_esq->contraindre(t); }; };
  grille(m_outilsEsquisse, QStringLiteral("Relations"),
         {{QStringLiteral("horizontal"), QStringLiteral("Horizontale"), QStringLiteral("une ligne, ou deux points (H)"), relation(TypeContrainte::Horizontal)},
          {QStringLiteral("vertical"), QStringLiteral("Verticale"), QStringLiteral("une ligne, ou deux points (V)"), relation(TypeContrainte::Vertical)},
          {QStringLiteral("coincident"), QStringLiteral("Coïncidente"), QStringLiteral("deux points, ou un point sur une courbe"), relation(TypeContrainte::Coincidence)},
          {QStringLiteral("parallele"), QStringLiteral("Parallèle"), QStringLiteral("deux lignes"), relation(TypeContrainte::Parallele)},
          {QStringLiteral("perpendiculaire"), QStringLiteral("Perpendiculaire"), QStringLiteral("deux lignes"), relation(TypeContrainte::Perpendiculaire)},
          {QStringLiteral("egal"), QStringLiteral("Égale"), QStringLiteral("lignes de même longueur, cercles de même rayon"), relation(TypeContrainte::Egal)},
          {QStringLiteral("arc-tangent"), QStringLiteral("Tangente"), QStringLiteral("ligne et arc, ou deux arcs"), relation(TypeContrainte::Tangente)},
          {QStringLiteral("concentrique"), QStringLiteral("Concentrique"), QStringLiteral("deux cercles ou arcs"), relation(TypeContrainte::Concentrique)},
          {QStringLiteral("colineaire"), QStringLiteral("Colinéaire"), QStringLiteral("deux lignes sur une même droite"), relation(TypeContrainte::Colineaire)},
          {QStringLiteral("ligne-milieu"), QStringLiteral("Milieu"), QStringLiteral("un point au milieu d’une ligne"), relation(TypeContrainte::Milieu)},
          {QStringLiteral("symetrique"), QStringLiteral("Symétrique"), QStringLiteral("deux points et une ligne ou un axe"), relation(TypeContrainte::Symetrie)},
          {QStringLiteral("fixe"), QStringLiteral("Fixe"), QStringLiteral("bloque un point ou un élément"), relation(TypeContrainte::Fixe)},
          {QStringLiteral("gerer-relations"), QStringLiteral("Gérer les relations"), QStringLiteral("relations possibles et existantes de la sélection, retrait"), [this] { ouvrirCommandeEsquisse(QStringLiteral("relations")); }}},
         7);
  m_outilsEsquisse->addSeparator();
  grille(m_outilsEsquisse, QStringLiteral("Fonctions"),
         {{QStringLiteral("extrusion"), QStringLiteral("Extrusion depuis l’esquisse"), QStringLiteral("termine l’esquisse et ouvre l’extrusion ; Annuler y ramène"), [this] { fonctionDepuisEsquisse(TypeFonction::Extrusion, false); }},
          {QStringLiteral("enlevement"), QStringLiteral("Enlèvement depuis l’esquisse"), QStringLiteral("termine l’esquisse et ouvre l’enlèvement"), [this] { fonctionDepuisEsquisse(TypeFonction::Extrusion, true); }},
          {QStringLiteral("revolution"), QStringLiteral("Révolution depuis l’esquisse"), QStringLiteral("termine l’esquisse et ouvre la révolution"), [this] { fonctionDepuisEsquisse(TypeFonction::Revolution, false); }},
          {QStringLiteral("tolerie"), QStringLiteral("Tôle de base depuis l’esquisse"), QStringLiteral("termine l’esquisse et ouvre la tôle de base"), [this] { fonctionDepuisEsquisse(TypeFonction::Tole, false); }}},
         2);
  {
    // Mode construction : bouton enfoncé tant qu'il est actif (touche X)
    QAction* modeC = m_outilsEsquisse->addAction(icone(QStringLiteral("construction")), QStringLiteral("Construction"));
    modeC->setCheckable(true);
    modeC->setToolTip(QStringLiteral("Mode construction (X) : ce que tu traces devient géométrie d'aide. Avec une sélection : Maj+C la convertit."));
    if (QToolButton* tb = qobject_cast<QToolButton*>(m_outilsEsquisse->widgetForAction(modeC))) tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    connect(modeC, &QAction::triggered, this, [this, modeC](bool on) {
      if (m_esq->aSelection()) {
        // Des éléments sont sélectionnés : on les passe en construction (ou on les en sort), le mode ne bouge pas.
        const QSignalBlocker b(modeC);
        modeC->setChecked(m_esq->modeConstruction());
        m_esq->basculerConstruction();
      } else if (m_esq->modeConstruction() != on) {
        m_esq->setModeConstruction(on);
      }
      m_esq->setFocus();
    });
    connect(m_esq, &Esquisseur::modeConstructionChange, modeC, [modeC](bool on) { const QSignalBlocker b(modeC); modeC->setChecked(on); });
  }
  commande(m_outilsEsquisse, QStringLiteral("Référence"), {{QStringLiteral("reference"), QStringLiteral("Cote de référence"), QStringLiteral("pilotante ↔ référence"), [this] { m_esq->basculerReference(); }}});
  commande(m_outilsEsquisse, QStringLiteral("Supprimer"), {{QStringLiteral("supprimer"), QStringLiteral("Supprimer la sélection"), QStringLiteral("Suppr"), [this] { m_esq->supprimerSelection(); }}});
  commande(m_outilsEsquisse, QStringLiteral("Recadrer"), {{QStringLiteral("recadrer"), QStringLiteral("Recadrer"), QStringLiteral("F"), [this] { m_esq->recadrer(); }}});
  m_outilsEsquisse->hide();
  colonne->addWidget(m_outilsEsquisse);

  // ---------- Arbre | centre | propriétés ----------
  auto* separation = new QSplitter(Qt::Horizontal);
  m_arbre = new QTreeWidget;
  m_arbre->setHeaderLabel(QStringLiteral("Arbre de construction"));
  m_arbre->setRootIsDecorated(false);
  m_arbre->setMinimumWidth(190);
  m_arbre->setContextMenuPolicy(Qt::CustomContextMenu);
  m_centre = new QStackedWidget;
  m_vue = new VuePiece;
  // L'esquisse se dessine par-dessus la vue 3D : la pièce et les esquisses précédentes restent visibles.
  auto* scene = new QWidget;
  auto* superpose = new QGridLayout(scene);
  superpose->setContentsMargins(0, 0, 0, 0);
  superpose->addWidget(m_vue, 0, 0);
  m_barre3D = new QFrame(scene);
  m_barre3D->setObjectName(QStringLiteral("barre3D"));
  m_barre3D->setStyleSheet(QStringLiteral("#barre3D { background: rgba(255,255,255,242); border: 1px solid #CDD3E4; border-radius: 10px; }"
                                          "QToolButton { border: none; border-radius: 6px; padding: 4px; } QToolButton:hover { background: #E9EEFF; }"
                                          "QLabel { color: #4A5270; font-size: 11px; padding: 0 4px; }"));
  m_barre3D->hide();
  m_panneauMesure = new QFrame(scene);
  m_panneauMesure->setObjectName(QStringLiteral("panneauMesure"));
  m_panneauMesure->setStyleSheet(QStringLiteral("#panneauMesure { background: rgba(255,255,255,242); border: 1px solid #CDD3E4; border-radius: 9px; }"
                                                "#panneauMesure QLabel { font-size: 12px; color: #10142A; }"));
  {
    auto* colMesure = new QVBoxLayout(m_panneauMesure);
    colMesure->setContentsMargins(10, 8, 10, 8);
    colMesure->setSpacing(6);
    m_texteMesure = new QLabel;
    m_texteMesure->setObjectName(QStringLiteral("texteMesure"));
    m_texteMesure->setTextFormat(Qt::RichText);
    colMesure->addWidget(m_texteMesure);
    auto* ligneMesure = new QHBoxLayout;
    m_uniteMesure = new QComboBox;
    m_uniteMesure->setObjectName(QStringLiteral("uniteMesure"));
    m_uniteMesure->setToolTip(QStringLiteral("Unités de la mesure (celles de la pièce par défaut)"));
    m_uniteMesure->addItem(QStringLiteral("Unités de la pièce"), QString());
    for (const auto& [code, titre] : {std::pair{"mm", "mm"}, std::pair{"cm", "cm"}, std::pair{"m", "m"}, std::pair{"in", "pouces"}, std::pair{"ft", "pieds"}})
      m_uniteMesure->addItem(QString::fromUtf8(titre), QString::fromLatin1(code));
    connect(m_uniteMesure, &QComboBox::activated, this, [this] { if (m_mesure) infoSelection(); });
    ligneMesure->addWidget(m_uniteMesure);
    auto* garder = new QToolButton;
    garder->setObjectName(QStringLiteral("garderMesure"));
    garder->setText(QStringLiteral("Garder"));
    garder->setToolTip(QStringLiteral("La mesure reste dessinée dans la vue après la sortie de la mesure"));
    connect(garder, &QToolButton::clicked, this, [this] { garderMesure(); });
    ligneMesure->addWidget(garder);
    auto* effacer = new QToolButton;
    effacer->setObjectName(QStringLiteral("effacerMesures"));
    effacer->setText(QStringLiteral("Effacer"));
    effacer->setToolTip(QStringLiteral("Retire les mesures gardées"));
    connect(effacer, &QToolButton::clicked, this, [this] { effacerMesures(); });
    ligneMesure->addWidget(effacer);
    ligneMesure->addStretch(1);
    colMesure->addLayout(ligneMesure);
  }
  m_panneauMesure->hide();
  m_bandeVue = new QFrame(scene);
  m_bandeVue->setObjectName(QStringLiteral("bandeVue"));
  m_bandeVue->setStyleSheet(QStringLiteral("#bandeVue { background: rgba(255,255,255,230); border: 1px solid #CDD3E4; border-radius: 9px; }"
                                           "QToolButton { border: none; border-radius: 6px; padding: 3px; } QToolButton:hover { background: #E9EEFF; }"
                                           "QToolButton:checked { background: #DDE5FF; }"));
  {
    auto* l = new QVBoxLayout(m_bandeVue);
    l->setContentsMargins(3, 4, 3, 4);
    l->setSpacing(2);
    auto* groupe = new QActionGroup(m_bandeVue);
    m_groupeFiltre = groupe;
    const struct { const char* icone; const char* bulle; int index; } filtres[] = {
        {"filtre-auto", "Sélection automatique : faces, arêtes, sommets, esquisses", 0}, {"filtre-faces", "Sélectionner des faces", 1},
        {"filtre-aretes", "Sélectionner des arêtes", 2}, {"filtre-sommets", "Sélectionner des sommets", 3}};
    for (const auto& fi : filtres) {
      auto* act = new QAction(icone(QString::fromLatin1(fi.icone)), QString::fromUtf8(fi.bulle), groupe);
      act->setCheckable(true);
      act->setChecked(fi.index == 0);
      act->setData(fi.index);
      auto* b = new QToolButton;
      b->setDefaultAction(act);
      b->setIconSize(QSize(24, 24));
      l->addWidget(b);
      const int index = fi.index;
      connect(act, &QAction::triggered, this, [this, index] {
        if (auto* c = m_outilsPiece->findChild<QComboBox*>(QStringLiteral("modeSelection"))) c->setCurrentIndex(index);
      });
    }
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: #DEE2EE;"));
    l->addWidget(sep);
    auto* voir = new QToolButton;
    voir->setIcon(icone(QStringLiteral("afficher")));
    voir->setIconSize(QSize(24, 24));
    voir->setToolTip(QStringLiteral("Affichage"));
    voir->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(voir);
    auto option = [this, menu](const QString& t, bool coche, std::function<void(bool)> f) {
      QAction* act = menu->addAction(t);
      act->setCheckable(true);
      act->setChecked(coche);
      connect(act, &QAction::toggled, this, [f](bool on) { f(on); });
    };
    menu->addAction(m_actionPlans);  // la même action que dans le ruban : les deux restent d'accord
    option(QStringLiteral("Esquisses"), true, [this](bool on) { auto x = m_vue->affichage(); x.esquisses = on; m_vue->setAffichage(x); });
    option(QStringLiteral("Géométrie de construction"), true, [this](bool on) { auto x = m_vue->affichage(); x.construction = on; m_vue->setAffichage(x); });
    option(QStringLiteral("Arêtes du solide"), true, [this](bool on) { auto x = m_vue->affichage(); x.aretes = on; m_vue->setAffichage(x); });
    option(QStringLiteral("Trièdre"), true, [this](bool on) { auto x = m_vue->affichage(); x.triedre = on; m_vue->setAffichage(x); });
    voir->setMenu(menu);
    l->addWidget(voir);
    m_bandeVue->adjustSize();
    m_bandeVue->move(10, 10);
    m_bandeVue->show();
  }
  superpose->addWidget(m_esq, 0, 0);
  // Coin de validation (comme dans SolidWorks) : coche verte pour valider, croix rouge pour abandonner, dans la vue.
  m_coinValidation = new QWidget;
  m_coinValidation->setAttribute(Qt::WA_TranslucentBackground);
  {
    auto* l = new QHBoxLayout(m_coinValidation);
    l->setContentsMargins(0, 10, 12, 0);
    l->setSpacing(8);
    auto* valider = new QToolButton;
    valider->setDefaultAction(new QAction(icone(QStringLiteral("valider")), QStringLiteral("✔ Terminer l’esquisse"), valider));
    auto* abandonner = new QToolButton;
    abandonner->setDefaultAction(new QAction(icone(QStringLiteral("abandonner")), QStringLiteral("✖ Annuler l’esquisse"), abandonner));
    for (QToolButton* b : {valider, abandonner}) {
      b->setIconSize(QSize(46, 46));
      b->setAutoRaise(true);
      b->setToolButtonStyle(Qt::ToolButtonIconOnly);
      b->setStyleSheet(QStringLiteral("QToolButton { border: none; background: transparent; } QToolButton:hover { background: rgba(255,255,255,0.6); border-radius: 26px; }"));
    }
    connect(valider->defaultAction(), &QAction::triggered, this, [this] {
      if (m_esquisseEditee) terminerEsquisse();
      else if (m_gestion) validerReglage();
    });
    connect(abandonner->defaultAction(), &QAction::triggered, this, [this] {
      if (m_esquisseEditee) annulerEsquisse();
      else if (m_gestion) fermerGestionnaire(false);
    });
    valider->setObjectName(QStringLiteral("coinValider"));
    abandonner->setObjectName(QStringLiteral("coinAbandonner"));
    l->addWidget(valider);
    l->addWidget(abandonner);
  }
  superpose->addWidget(m_coinValidation, 0, 0, Qt::AlignTop | Qt::AlignRight);
  m_coinValidation->hide();
  connect(m_esq, &Esquisseur::demandeTerminer, this, [this] { terminerEsquisse(); });
  connect(m_esq, &Esquisseur::demandeCommande, this, [this](const QString& c) { ouvrirCommandeEsquisse(c); });
  connect(m_esq, &Esquisseur::selectionAChange, this, [this] {
    if (m_commande.isEmpty()) return;
    if (m_commande == QLatin1String("relations")) { majCommandeEsquisse(); return; }
    if (m_cmdChamp == 0) {
      m_cmdElements = m_esq->elementsSelectionnes();
    } else {
      const bool pointVoulu = m_commande == QLatin1String("repetition-circulaire") || m_commande == QLatin1String("pivoter") || m_commande == QLatin1String("echelle");
      if (pointVoulu) { const int p = m_esq->pointSelectionne(); if (p < 0) return; m_cmdPoint = p; }
      else { const int l = m_esq->ligneOuAxeSelectionne(); if (!l) return; m_cmdAxe = l; }
      m_cmdChamp = 0;
      m_esq->selectionnerElements(m_cmdElements);
    }
    majCommandeEsquisse();
  });
  connect(m_esq, &Esquisseur::demandeAnnuler, this, [this] { annulerEsquisse(); });
  connect(m_esq, &Esquisseur::historiqueChange, this, [this] { emit historiqueChange(); });
  m_esq->hide();
  m_centre->addWidget(scene);
  connect(m_vue, &VuePiece::cameraChangee, m_esq, [this] { if (m_esq->isVisible()) m_esq->update(); });
  m_props = new QScrollArea;
  m_props->setWidgetResizable(true);
  m_props->setMinimumWidth(340);
  m_props->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  separation->addWidget(m_arbre);
  separation->addWidget(m_centre);
  separation->addWidget(m_props);
  separation->setCollapsible(separation->indexOf(m_props), false);
  // Panneau latéral des outils (U1) : propriétés, masse, variables, à droite de la vue qui reste manipulable
  m_panneau = new PanneauLateral;
  separation->addWidget(m_panneau);
  separation->setCollapsible(separation->indexOf(m_panneau), false);
  m_separation = separation;
  separation->setStretchFactor(1, 1);
  separation->setSizes({210, 820, 370});
  colonne->addWidget(separation, 1);
  m_etat = new EtiquetteEtat;
  connect(m_etat, &EtiquetteEtat::texteChange, this, &AtelierPiece::messageChange);
  m_etat->setStyleSheet(QStringLiteral("padding: 5px 10px; background: #FAFBFE; border-top: 1px solid #D2D7E6;"));
  colonne->addWidget(m_etat);

  connect(modeSel, &QComboBox::currentIndexChanged, this, [this](int i) {
    if (m_bandeVue)
      for (QAction* act : m_bandeVue->findChildren<QAction*>()) if (act->isCheckable() && act->data().isValid() && act->data().toInt() == i) { const QSignalBlocker b(act); act->setChecked(true); }
    m_vue->setMode(i == 1 ? VuePiece::Mode::Faces : i == 2 ? VuePiece::Mode::Aretes : i == 3 ? VuePiece::Mode::Sommets : VuePiece::Mode::Tout);
    m_vue->setFocus();
  });
  connect(m_vue, &VuePiece::selectionChangee, this, [this] { selectionVue(); });
  connect(m_vue, &VuePiece::pointClique, this, [this](double x, double y, double z, bool) {
    if (!m_gestion || m_champ != Champ::Points3D || m_brouillon.type != TypeFonction::Esquisse3D) return;
    m_brouillon.points3d.push_back({std::round(x * 1000) / 1000, std::round(y * 1000) / 1000, std::round(z * 1000) / 1000});
    m_vue->setPlanTravail(gp_Pnt(x, y, z));
    m_vue->viderSelection();
    apercu();
    majProprietes();
  });
  {
    // Un seul Échap pour tout l'atelier (AP-05) : il ferme le réglage, quitte la mesure, abandonne l'attente d'un
    // support d'esquisse (AP-11), puis désélectionne. Dans l'esquisse, l'esquisseur le prend avant ce raccourci.
    auto* echap = new QShortcut(QKeySequence(Qt::Key_Escape), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(echap, &QShortcut::activated, this, [this] {
      if (m_esquisseEditee) return;
      if (m_gestion) fermerGestionnaire(false);
      else if (m_mesure) basculerMesure();
      else if (m_attenteEsquisse) {
        m_attenteEsquisse = false;
        majPlans();
        majProprietes();
        m_etat->setText(QStringLiteral("Nouvelle esquisse abandonnée."));
      } else {
        m_vue->viderSelection();
        m_barre3D->hide();
      }
    });
    auto* f1 = new QShortcut(QKeySequence(Qt::Key_F1), this, nullptr, nullptr, Qt::WindowShortcut);
    connect(f1, &QShortcut::activated, this, [this] { aideRaccourcis(); });
  }
  connect(m_vue, &VuePiece::poigneeDeplacee, this, [this](double l, bool fini) {
    if (!m_gestion || m_brouillon.type != TypeFonction::Extrusion) return;
    m_brouillon.longueur = m_brouillon.fin == "milieu" ? 2 * l : l;
    const Unites u = unites();
    m_etat->setText(QStringLiteral("<b>Profondeur : %1</b> — relâche pour valider la valeur").arg(QString::fromStdString(ecrireValeur(m_brouillon.longueur, false, u))));
    const qint64 maintenant = QDateTime::currentMSecsSinceEpoch();
    if (fini || maintenant - m_dernierApercu > 160) {
      m_dernierApercu = maintenant;
      apercu();
    }
    if (fini) majProprietes();
  });
  {
    struct R { QKeySequence k; std::function<void()> f; };
    const std::vector<R> raccourcis = {
        {QKeySequence(Qt::Key_F), [this] { m_vue->recadrer(); }},
        {QKeySequence(Qt::Key_1), [this] { m_vue->vueStandard(V3d_Yneg); }},
        {QKeySequence(Qt::Key_2), [this] { m_vue->vueStandard(V3d_Zpos); }},
        {QKeySequence(Qt::Key_3), [this] { m_vue->vueStandard(V3d_Xpos); }},
        {QKeySequence(Qt::Key_0), [this] { m_vue->vueStandard(V3d_XposYnegZpos); }},
        {QKeySequence(Qt::Key_E), [this] { nouvelleFonction(TypeFonction::Extrusion, false, "borgne"); }},
        {QKeySequence(Qt::Key_X), [this] { nouvelleFonction(TypeFonction::Extrusion, true, "borgne"); }},
        {QKeySequence(Qt::Key_R), [this] { nouvelleFonction(TypeFonction::Revolution, false, ""); }},
        {QKeySequence(Qt::Key_M), [this] { basculerMesure(); }},
        {QKeySequence(Qt::Key_Space), [this] {
           const int id = m_vue->esquisseSelectionnee() ? m_vue->esquisseSelectionnee() : fonctionChoisie();
           basculerVisibiliteEsquisse(id);
         }}};
    for (const auto& r : raccourcis) {
      auto* s = new QShortcut(r.k, m_vue, nullptr, nullptr, Qt::WidgetShortcut);
      const auto f = r.f;
      connect(s, &QShortcut::activated, this, [this, f] { if (!m_gestion && !m_esquisseEditee) f(); });
    }
  }
  m_arbrePropre = m_arbre;
  brancherArbre(m_arbre, true);
  connect(m_choixPiece, &QComboBox::activated, this, [this](int i) { activerPiece(i); majChoixPieces(); });
  connect(m_esq, &Esquisseur::modifiee, this, [this] {
    if (!m_esquisseEditee) return;
    if (FonctionPiece* f = m_piece.fonction(m_esquisseEditee)) { f->esquisse = m_esq->esquisse(); enregistrer(); }
    majTableCotes();
  });
  connect(m_esq, &Esquisseur::etatChange, this, [this](const QString& m, int niveau) {
    const char* couleur = niveau == 2 ? "#C2402E" : niveau == 1 ? "#2F6BF5" : "#2F7D53";
    m_etat->setText(QStringLiteral("<span style='color:%1'><b>%2</b></span>").arg(QString::fromLatin1(couleur), m.toHtmlEscaped()));
  });
  connect(doc, &Document::change, this, [this] {
    const auto& pieces = m_doc->projet().pieces;
    // La pièce éditée est suivie par son identifiant : une suppression ou un ajout ailleurs ne la change pas (AP-02)
    const int rang = rangPiece(m_idPiece);
    if (rang < 0) {
      // Pièce disparue (supprimée, création annulée, autre projet) : l'édition en cours n'a plus de cible ; elle
      // est abandonnée sans rien écrire, plutôt que d'aller remplacer une autre pièce
      if (m_gestion) { m_gestion = false; m_retourEsquisse = 0; activerCommandes(true); }
      if (m_esquisseEditee) sortirEsquisse(0);
      m_indice = pieces.empty() ? -1 : std::clamp(m_indice, 0, static_cast<int>(pieces.size()) - 1);
      charger();
      return;
    }
    m_indice = rang;
    const bool changee = pieces[static_cast<size_t>(m_indice)].dump() != m_dernierJson;
    // La pièce a changé ailleurs (annulation depuis un autre atelier…) pendant un réglage : le brouillon partait d'un
    // état périmé ; le réglage est abandonné, puis la pièce rechargée (AP-03)
    if (changee && m_gestion && m_esquisseEditee == 0) {
      fermerGestionnaire(false);
      m_etat->setText(QStringLiteral("La pièce a changé pendant le réglage : réglage abandonné, pièce rechargée."));
    }
    if (changee && m_esquisseEditee == 0 && !m_gestion) charger();
    else majChoixPieces();
  });
  m_arbre->setIconSize(QSize(20, 20));
  setStyleSheet(QStringLiteral(
      "QTabWidget::pane { border: none; }"
      "QTabBar::tab { padding: 6px 14px; font-weight: 600; color: #4A5270; border: none; }"
      "QTabBar::tab:selected { color: #10142A; border-bottom: 3px solid #5B4BF5; }"
      "QToolBar { border: none; spacing: 2px; background: #FAFBFE; }"
      "QToolButton { border-radius: 6px; padding: 3px; color: #10142A; }"
      "QToolButton:hover { background: #E9EEFF; }"
      "QToolButton:pressed, QToolButton:checked { background: #DDE5FF; }"
      "QToolButton::menu-button { border: none; background: transparent; width: 14px; } QToolButton::menu-button:hover { background: #E0E8FF; border-radius: 4px; }"
      "QGroupBox { font-weight: 600; border: 1px solid #D7DCEA; border-radius: 8px; margin-top: 14px; padding: 8px 6px 6px 6px; background: #FAFBFE; }"
      "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #232A45; }"
      "QTreeWidget { border: none; background: #FAFBFE; } QTreeWidget::item { padding: 3px 2px; }"
      "QTreeWidget::item:selected { background: #DDE5FF; color: #10142A; }"
      "QScrollArea { border: none; background: #F3F5FA; }"));
  charger();
}

void AtelierPiece::charger() {
  const auto& pieces = m_doc->projet().pieces;
  if (pieces.empty()) {
    m_indice = -1;
    m_piece = Piece();
    m_res = ResultatPiece();
    m_dernierJson.clear();
    m_idPiece.clear();
  } else {
    m_indice = std::clamp(m_indice, 0, static_cast<int>(pieces.size()) - 1);
    m_piece = pieceDepuisJson(pieces[static_cast<size_t>(m_indice)]);
    m_dernierJson = pieces[static_cast<size_t>(m_indice)].dump();
  }
  m_idPiece = m_indice >= 0 ? m_piece.id : std::string();
  majChoixPieces();
  reconstruire(true);
}

void AtelierPiece::enregistrer() {
  // L'écriture vise la pièce par son identifiant : si elle n'existe plus (supprimée, autre projet ouvert),
  // rien n'est écrit, plutôt que d'écraser la pièce qui a pris son rang (AP-02)
  const int i = rangPiece(m_idPiece);
  if (i < 0) {
    if (m_indice >= 0) m_etat->setText(QStringLiteral("<span style='color:#C2402E'><b>Pièce introuvable</b> : la modification n’a pas été écrite.</span>"));
    return;
  }
  m_indice = i;
  const Json j = pieceVersJson(m_piece);
  m_dernierJson = j.dump();
  auto ecrire = [&](Projet& p) { if (i < static_cast<int>(p.pieces.size())) p.pieces[static_cast<size_t>(i)] = j; };
  // Pendant une session d'esquisse, chaque geste met le document à jour sans créer de pas d'historique : la
  // session entière en fera un seul, ou aucun si elle est annulée (AP-04)
  if (m_sessionEsquisse && m_doc->enGeste()) m_doc->pendantGeste(ecrire);
  else m_doc->modifier(ecrire);
}

void AtelierPiece::commencerSessionEsquisse() {
  if (m_sessionEsquisse) return;
  m_doc->finGeste();  // un autre geste resté ouvert est d'abord clos
  m_doc->debutGeste();
  m_sessionEsquisse = true;
}

void AtelierPiece::finirSessionEsquisse() {
  if (!m_sessionEsquisse) return;
  m_sessionEsquisse = false;
  m_doc->finGeste();  // un pas s'il y a un changement net, aucun sinon
}

int AtelierPiece::rangPiece(const std::string& id) const {
  const auto& ps = m_doc->projet().pieces;
  if (id.empty()) return m_indice >= 0 && m_indice < static_cast<int>(ps.size()) ? m_indice : -1;  // pièce sans identifiant (ne devrait plus arriver)
  for (size_t i = 0; i < ps.size(); ++i) {
    const auto it = ps[i].find("id");
    if (it != ps[i].end() && it->is_string() && it->get<std::string>() == id) return static_cast<int>(i);
  }
  return -1;
}

bool AtelierPiece::peutLancer() {
  if (m_panneau && m_panneau->page() == QStringLiteral("variables")) {
    // Les variables en cours d'édition se valident ou s'annulent d'abord
    m_panneau->setFocus();
    m_etat->setText(QStringLiteral("Termine d’abord les variables (Valider ou Annuler)."));
    return false;
  }
  if (m_esquisseEditee) terminerEsquisse();
  return m_esquisseEditee == 0;
}

bool AtelierPiece::terminerEdition() {
  if (m_esquisseEditee) terminerEsquisse();
  if (m_gestion && !m_esquisseEditee) fermerGestionnaire(true);
  // Une fonction lancée depuis l'esquisse puis validée ne ramène pas dans l'esquisse
  m_retourEsquisse = 0;
  return !enEdition();
}

void AtelierPiece::majChoixPieces() {
  const QSignalBlocker b(m_choixPiece);
  m_choixPiece->clear();
  for (const auto& j : m_doc->projet().pieces) m_choixPiece->addItem(qs(j.value("nom", std::string("Pièce"))));
  m_choixPiece->setCurrentIndex(m_indice);
  activerRuban(m_indice >= 0 && !m_gestion && m_esquisseEditee == 0);
}

// Une seule définition de « esquisse utilisée » (AP-22) : esquisse d'une extrusion, d'une révolution, d'un balayage
// ou d'un perçage actif, chemin d'un balayage, profil d'un lissage.
std::set<int> AtelierPiece::esquissesUtilisees(const Piece& p) {
  std::set<int> u;
  for (const auto& f : p.fonctions) {
    if (f.desactivee) continue;
    if ((f.type == TypeFonction::Extrusion || f.type == TypeFonction::Revolution || f.type == TypeFonction::Balayage || f.type == TypeFonction::Percage || f.type == TypeFonction::Tole || f.type == TypeFonction::Nervure) && f.esquisseId)
      u.insert(f.esquisseId);
    if (f.cheminId) u.insert(f.cheminId);
    if (f.type == TypeFonction::Lissage) u.insert(f.sources.begin(), f.sources.end());
  }
  return u;
}

// Visible : montrée à la demande, ou laissée au réglage automatique et non utilisée par une fonction
bool AtelierPiece::esquisseVisible(const FonctionPiece& f, const std::set<int>& utilisees) {
  return f.visibilite == 1 || (f.visibilite == 0 && !utilisees.count(f.id));
}

// Esquisses que le réglage en cours montre toujours : celle de la fonction, son chemin, ses profils (AP-34, AP-35)
std::set<int> AtelierPiece::esquissesDuReglage() const {
  std::set<int> r;
  if (!m_gestion) return r;
  if (m_brouillon.esquisseId) r.insert(m_brouillon.esquisseId);
  if (m_brouillon.cheminId) r.insert(m_brouillon.cheminId);
  if (m_brouillon.type == TypeFonction::Lissage) r.insert(m_brouillon.sources.begin(), m_brouillon.sources.end());
  return r;
}

void AtelierPiece::masquerEsquissesUtilisees(ResultatPiece& r, const Piece& p) const {
  const int choisie = fonctionChoisie();
  const std::set<int> utilisees = esquissesUtilisees(p), reglage = esquissesDuReglage();
  r.esquisses.erase(std::remove_if(r.esquisses.begin(), r.esquisses.end(),
                                   [&](const std::pair<int, TopoDS_Shape>& e) {
                                     if ((e.first == choisie && !m_gestion) || reglage.count(e.first)) return false;
                                     const FonctionPiece* f = p.fonction(e.first);
                                     return f && !esquisseVisible(*f, utilisees);
                                   }),
                    r.esquisses.end());
}

void AtelierPiece::reconstruire(bool recadrer) {
  {
    // Pas de la poignée : la puissance de dix de l'unité du document la plus proche de 0,1 mm, par excès (AP-41)
    const Unites u = unites();
    const double pas = std::pow(10.0, std::ceil(std::log10(depuisMillimetres(0.1, u.longueur)) - 1e-9));
    m_vue->setPasPoignee(versMillimetres(pas, u.longueur));
  }
  m_res = construirePiece(m_piece);
  m_affiche = m_res;
  ResultatPiece affiche = m_affiche;
  masquerEsquissesUtilisees(affiche, m_piece);
  if (m_vueDepliee && !m_gestion && !m_esquisseEditee && m_res.tole.present) {
    // Déplié : la tôle à plat à la place de la pièce pliée, ses lignes de pliage en traits d'esquisse
    affiche.forme = solideDeplie(m_res.tole);
    affiche.faces.clear();
    affiche.esquisses.clear();
    affiche.esquissesConstruction.clear();
    BRep_Builder bb;
    TopoDS_Compound haut, bas;
    bb.MakeCompound(haut);
    bb.MakeCompound(bas);
    const gp_Vec dessus = gp_Vec(m_res.tole.repere.Direction()) * (m_res.tole.epaisseur + 0.01);
    for (const auto& pl : m_res.tole.plis)
      for (const auto& [a, b] : {std::pair{pl.debutA, pl.debutB}, std::pair{pl.finA, pl.finB}})
        bb.Add(pl.versLeHaut ? haut : bas, BRepBuilderAPI_MakeEdge(a.Translated(dessus), b.Translated(dessus)).Edge());
    affiche.esquisses.push_back({-1, haut});
    affiche.esquissesConstruction.push_back({-1, bas});
  }
  m_vue->surligner({});
  majPlans();
  majVisibilite();
  colorerFaces(affiche);
  m_vue->afficher(affiche, recadrer);
  majArbre();
  majProprietes();
  if (m_indice < 0) m_etat->setText(QStringLiteral("Aucune pièce : clique sur « Nouvelle pièce » pour commencer par une esquisse."));
  else if (!m_res.erreurs.empty())
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'><b>%1 en échec</b>%2 : voir l’arbre (survol pour le détail).</span>")
                        .arg(m_res.erreurs.size())
                        .arg(m_res.obsoletes.empty() ? QString() : QStringLiteral(" · %1 non calculée(s)").arg(m_res.obsoletes.size())));
  else if (!m_res.forme.IsNull()) m_etat->setText(QStringLiteral("<span style='color:#2F7D53'><b>Pièce à jour</b></span> · volume %1 cm³").arg(QLocale().toString(volume(m_res.forme) / 1000, 'f', 2)));
  else m_etat->setText(QStringLiteral("Esquisse prête : clique-la dans la vue ou dans l’arbre, puis Extrusion ou Révolution."));
}

void AtelierPiece::brancherArbre(QTreeWidget* a, bool menu) {
  // Réglage ouvert : chaque clic compte, même sur l'élément déjà courant (AP-31) ; il remplit le champ actif
  connect(a, &QTreeWidget::itemClicked, this, [this, a](QTreeWidgetItem* it) {
    if (!m_gestion || m_esquisseEditee || !dansMaPiece(it) || m_arbre != a) return;
    clicArbreReglage(it->data(0, Qt::UserRole).toInt());
  });
  connect(a, &QTreeWidget::currentItemChanged, this, [this, a](QTreeWidgetItem* courant) {
    if (!dansMaPiece(courant) || m_arbre != a) return;
    if (m_gestion) return;  // traité au clic
    // Corps cliqué : surligné dans la vue, avec son volume et sa masse (AP-27)
    if (const QVariant k = courant->data(0, Qt::UserRole + 6); k.isValid() && !m_res.forme.IsNull()) {
      int n = 0;
      for (TopExp_Explorer ex(m_res.forme, TopAbs_SOLID); ex.More(); ex.Next(), ++n)
        if (n == k.toInt()) {
          m_vue->surligner({ex.Current()});
          const ProprietesMasse pm = proprietesMasse(ex.Current(), m_piece.densite);
          const Unites un = unites();
          const double k3 = std::pow(depuisMillimetres(1, un.longueur), 3);
          const QString vol = QLocale().toString(pm.volume * k3, 'f', un.longueur == UniteLongueur::Millimetre ? 1 : 4) + QStringLiteral(" ") + QString::fromUtf8(symbole(un.longueur)) + QStringLiteral("³");
          m_etat->setText(QStringLiteral("<b>%1</b> · volume %2 · masse %3%4 g")
                              .arg(courant->text(0), vol, m_piece.densiteConfirmee ? QString() : QStringLiteral("≈ "), QLocale().toString(pm.masse, 'f', 2)));
        }
      return;
    }
    const int c = fonctionChoisie();
    m_vue->mettreEnEvidencePlan(c == -1 ? "XY" : c == -2 ? "XZ" : c == -3 ? "YZ" : "");
    if (!m_esquisseEditee) majProprietes();
    if (!m_esquisseEditee && !m_res.forme.IsNull()) {
      // L'esquisse choisie se montre, même masquée ; la barre flottante propose ses commandes.
      majVisibilite();
      colorerFaces(m_res);
      m_vue->afficher(m_res, false);
      if (const FonctionPiece* fc = m_piece.fonction(c); fc && fc->type == TypeFonction::Esquisse) majBarre3D(QPoint(40, 40));
    }
    if (c <= -1 && c >= -8 && c != -4 && !m_vue->plansVisibles()) m_vue->setPlansVisibles(true);
    if (m_attenteEsquisse && c <= -1 && c >= -3) { creerEsquisseDepuisSelection(); return; }
    majProprietes();
  });
  connect(a, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) { if (dansMaPiece(it)) editerFonction(it->data(0, Qt::UserRole).toInt()); });
  if (menu) connect(a, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& p) { menuArbre(p); });
}

// Clic dans l'arbre pendant un réglage : plans de base, références (plan, axe, point, repère), esquisses et fonctions
// vont au champ qui les attend (AP-15, AP-31, AP-35)
void AtelierPiece::clicArbreReglage(int c) {
  const FonctionPiece* g = m_piece.fonction(c);
  const std::string plan = c == -1 ? "XY" : c == -2 ? "XZ" : c == -3 ? "YZ" : std::string();
  const std::string axeBase = c == -5 ? "X" : c == -6 ? "Y" : c == -7 ? "Z" : c == -8 ? "O" : std::string();
  const bool reference = g && (g->type == TypeFonction::Plan || g->type == TypeFonction::Axe || g->type == TypeFonction::Point || g->type == TypeFonction::Repere);
  const std::string ref = !plan.empty() ? plan : !axeBase.empty() ? axeBase : reference ? "F:" + std::to_string(g->id) : std::string();
  bool change = false;
  auto basculer = [&change](std::vector<int>& l, int id) {
    const auto it = std::find(l.begin(), l.end(), id);
    if (it != l.end()) l.erase(it); else l.push_back(id);
    change = true;
  };
  switch (m_champ) {
    case Champ::Sources:
      if (g && sourceRepetable(*g)) basculer(m_brouillon.sources, c);
      break;
    case Champ::Profils:
      if (g && g->type == TypeFonction::Esquisse) basculer(m_brouillon.sources, c);  // ordre des clics = ordre des profils
      break;
    case Champ::Chemin:
      if (g && (g->type == TypeFonction::Esquisse || g->type == TypeFonction::Esquisse3D)) { m_brouillon.cheminId = c; change = true; activerChamp(Champ::Aucun); }
      break;
    case Champ::Esquisse:
      if (g && g->type == TypeFonction::Esquisse) { m_brouillon.esquisseId = c; m_brouillon.regions.clear(); m_brouillon.pointsRegions.clear(); change = true; activerChamp(Champ::Aucun); }
      break;
    case Champ::Supports:
      if (!ref.empty()) {
        auto& sp = m_brouillon.supports;
        if (m_brouillon.type == TypeFonction::Symetrie) sp = {ref};
        else if (const auto it = std::find(sp.begin(), sp.end(), ref); it != sp.end()) sp.erase(it);
        else sp.push_back(ref);
        change = true;
      }
      break;
    case Champ::Neutre:
      if (!ref.empty() && (!plan.empty() || (g && g->type == TypeFonction::Plan))) { m_brouillon.planNeutre = ref; change = true; activerChamp(Champ::Aucun); }
      break;
    case Champ::Direction:
      if ((g && g->type == TypeFonction::Axe) || (!axeBase.empty() && axeBase != "O")) { m_brouillon.direction1 = ref; change = true; activerChamp(Champ::Aucun); }
      break;
    case Champ::AxeRevolution:
      if ((g && g->type == TypeFonction::Axe) || (!axeBase.empty() && axeBase != "O")) { m_brouillon.axeBase = ref; change = true; activerChamp(Champ::Aucun); }
      break;
    default:
      // Sans champ actif : une esquisse cliquée devient celle de l'extrusion ou de la révolution en cours
      if (g && g->type == TypeFonction::Esquisse && (m_brouillon.type == TypeFonction::Extrusion || m_brouillon.type == TypeFonction::Revolution || m_brouillon.type == TypeFonction::Tole || m_brouillon.type == TypeFonction::Nervure)) {
        m_brouillon.esquisseId = c;
        change = true;
      }
      break;
  }
  if (!plan.empty()) m_vue->mettreEnEvidencePlan(plan);
  if (!change) return;
  apercu();
  majProprietes();
}

bool AtelierPiece::dansMaPiece(const QTreeWidgetItem* it) const {
  if (!it) return false;
  if (!m_arbreExterne) return true;
  for (const QTreeWidgetItem* q = it->parent(); q; q = q->parent())
    if (q == m_racine) return true;
  return false;
}

QTreeWidgetItem* AtelierPiece::cibleArbre() const {
  if (m_arbreExterne) return m_racine;
  return m_arbre->invisibleRootItem();
}

int AtelierPiece::fonctionChoisie() const {
  const QTreeWidgetItem* it = m_arbre->currentItem();
  return it && dansMaPiece(it) ? it->data(0, Qt::UserRole).toInt() : 0;
}

void AtelierPiece::choisirFonction(int id) {
  if (id > 0) m_attenteEsquisse = false;  // une fonction choisie abandonne l'attente d'un support (AP-11)
  if (id == 0) {
    // Rien de choisi : l'arbre se désélectionne et le panneau des réglages disparaît
    m_arbre->setCurrentItem(nullptr);
    m_arbre->clearSelection();
    if (!m_gestion && !m_esquisseEditee) majProprietes();
    return;
  }
  QTreeWidgetItem* cible = cibleArbre();
  if (!cible) return;
  std::function<QTreeWidgetItem*(QTreeWidgetItem*)> chercher = [&](QTreeWidgetItem* p) -> QTreeWidgetItem* {
    for (int i = 0; i < p->childCount(); ++i) {
      QTreeWidgetItem* c = p->child(i);
      if (c->data(0, Qt::UserRole).toInt() == id && c->data(0, Qt::UserRole + 5).toBool()) return c;
      if (QTreeWidgetItem* r = chercher(c)) return r;
    }
    return nullptr;
  };
  if (QTreeWidgetItem* it = chercher(cible)) {
    for (QTreeWidgetItem* q = it->parent(); q && q != cible; q = q->parent()) q->setExpanded(true);
    m_arbre->setCurrentItem(it);
  }
}

void AtelierPiece::attacherArbre(QTreeWidget* arbre) {
  if (m_arbreExterne || !arbre) return;
  m_arbreExterne = true;
  m_arbre = arbre;
  m_arbrePropre->hide();
  brancherArbre(arbre, false);
}

void AtelierPiece::setRacineArbre(QTreeWidgetItem* racine) {
  m_racine = racine;
  if (m_racine) majArbre();
}

void AtelierPiece::activerPiece(int i) {
  if (i == m_indice || i < 0 || i >= static_cast<int>(m_doc->projet().pieces.size())) return;
  if (!terminerEdition()) return;  // le réglage ou l'esquisse ouverts sont écrits dans leur pièce d'abord (AP-02)
  m_indice = i;
  m_mesuresGardees.clear();  // mesures d'une autre pièce
  majTracesMesure();
  charger();
}

void AtelierPiece::majArbre() {
  QTreeWidgetItem* cible = cibleArbre();
  if (!cible) return;
  const int choisie = fonctionChoisie();
  const QSignalBlocker b(m_arbre);
  // Repli des fonctions conservé d'une reconstruction à l'autre
  std::function<void(QTreeWidgetItem*)> retenir = [&](QTreeWidgetItem* p) {
    for (int i = 0; i < p->childCount(); ++i) {
      QTreeWidgetItem* c = p->child(i);
      const int id = c->data(0, Qt::UserRole).toInt();
      if (c->childCount() > 0 && id != 0) { if (c->isExpanded()) m_fonctionsOuvertes.insert(id); else m_fonctionsOuvertes.erase(id); }
      retenir(c);
    }
  };
  retenir(cible);
  qDeleteAll(cible->takeChildren());
  auto element = [this](QTreeWidgetItem* parent, const QString& texte, const QString& ic, int id) {
    auto* it = new QTreeWidgetItem(parent);
    it->setText(0, texte);
    it->setIcon(0, icone(ic));
    it->setData(0, Qt::UserRole, id);
    it->setData(0, Qt::UserRole + 5, true);  // élément de fonction de la pièce
    if (m_arbreExterne) it->setData(0, Navigateur::ROLE_COULEUR, QColor(0x2F, 0x5F, 0xD0));
    return it;
  };
  if (m_indice >= 0) {
    // Origine et plans de base, rangés dans un dossier replié
    QTreeWidgetItem* origine = element(cible, QStringLiteral("Origine et plans"), QStringLiteral("point"), -4);
    origine->setForeground(0, QColor(0x3A, 0x6F, 0xE0));
    for (const auto& [nom, id] : {std::pair{QStringLiteral("Plan de dessus (XY)"), -1}, std::pair{QStringLiteral("Plan de face (XZ)"), -2}, std::pair{QStringLiteral("Plan de droite (YZ)"), -3}}) {
      QTreeWidgetItem* it = element(origine, nom, QStringLiteral("plans"), id);
      it->setForeground(0, QColor(0x3A, 0x6F, 0xE0));
    }
    // Axes et origine, choisissables comme les plans (M7)
    for (const auto& [nom, id] : {std::pair{QStringLiteral("Axe X"), -5}, std::pair{QStringLiteral("Axe Y"), -6}, std::pair{QStringLiteral("Axe Z"), -7}, std::pair{QStringLiteral("Origine"), -8}}) {
      QTreeWidgetItem* it = element(origine, nom, id == -8 ? QStringLiteral("point") : QStringLiteral("axe"), id);
      it->setForeground(0, QColor(0x3A, 0x6F, 0xE0));
    }
    origine->setExpanded(m_fonctionsOuvertes.count(-4) > 0);
  }
  // Une esquisse utilisée par une fonction se range sous elle (comme dans SolidWorks) ; les autres restent à leur place.
  std::map<int, int> consommateur;
  for (const auto& f : m_piece.fonctions)
    if ((f.type == TypeFonction::Extrusion || f.type == TypeFonction::Revolution || f.type == TypeFonction::Tole || f.type == TypeFonction::Nervure) && f.esquisseId && !consommateur.count(f.esquisseId)) consommateur[f.esquisseId] = f.id;
  std::map<int, QTreeWidgetItem*> items;
  auto decorer = [this](QTreeWidgetItem* it, const FonctionPiece& f) {
    const std::string err = m_res.erreur(f.id);
    if (!err.empty()) { it->setForeground(0, QColor(0xC2, 0x40, 0x2E)); it->setToolTip(0, qs(err)); it->setData(0, Navigateur::ROLE_DETAIL, QStringLiteral("⚠ erreur")); it->setData(0, Qt::UserRole + 4, QColor(0xC2, 0x40, 0x2E)); }
    else if (const std::string av = m_res.avertissement(f.id); !av.empty()) {
      it->setForeground(0, QColor(0xB5, 0x6A, 0x00));
      it->setToolTip(0, qs(av));
      it->setData(0, Navigateur::ROLE_DETAIL, QStringLiteral("⚠ à vérifier"));
    }
    if (f.desactivee) { QFont fo = it->font(0); fo.setItalic(true); it->setFont(0, fo); it->setForeground(0, QColor(0x9A, 0xA0, 0xB8)); }
    else if (m_res.obsolete(f.id)) {
      // Non calculée : une fonction en amont est en échec (AP-09)
      const FonctionPiece* fautive = m_piece.fonction(m_res.premiereErreur);
      it->setForeground(0, QColor(0x9A, 0xA0, 0xB8));
      it->setToolTip(0, QStringLiteral("Non calculée : dépend de « %1 », en échec.").arg(fautive ? qs(fautive->nom) : QStringLiteral("?")));
      it->setData(0, Navigateur::ROLE_DETAIL, QStringLiteral("⧗ non calculée"));
    }
  };
  auto nomIcone = [](const FonctionPiece& f) {
    return QString::fromLatin1(f.type == TypeFonction::Esquisse ? "esquisse" : f.type == TypeFonction::Extrusion ? (f.enlever ? "enlevement" : "extrusion")
                               : f.type == TypeFonction::Revolution ? (f.enlever ? "revolution-enlevement" : "revolution") : f.type == TypeFonction::Conge ? "conge"
                               : f.type == TypeFonction::Chanfrein ? "chanfrein" : f.type == TypeFonction::Coque ? "coque" : f.type == TypeFonction::Plan ? "plans"
                               : f.type == TypeFonction::Axe ? "axe" : f.type == TypeFonction::Percage ? "percage" : f.type == TypeFonction::Depouille ? "depouille"
                               : f.type == TypeFonction::Repetition ? (f.typeRepetition == "circulaire" ? "repetition-circulaire" : "repetition-lineaire")
                               : f.type == TypeFonction::Symetrie ? "symetrie" : f.type == TypeFonction::Balayage ? "balayage" : f.type == TypeFonction::Lissage ? "lissage" : f.type == TypeFonction::Esquisse3D ? "esquisse3d"
                               : f.type == TypeFonction::Tole ? "tolerie" : f.type == TypeFonction::BordTombe ? "bord-tombe" : f.type == TypeFonction::Import ? "ouvrir"
                               : f.type == TypeFonction::Nervure ? "nervure" : f.type == TypeFonction::Filetage ? "filetage" : "point");
  };
  for (const auto& f : m_piece.fonctions) {
    if (f.type == TypeFonction::Esquisse && consommateur.count(f.id)) continue;  // rangée sous sa fonction
    QTreeWidgetItem* it = element(cible, qs(f.nom), nomIcone(f), f.id);
    decorer(it, f);
    items[f.id] = it;
    if ((f.type == TypeFonction::Extrusion || f.type == TypeFonction::Revolution || f.type == TypeFonction::Tole || f.type == TypeFonction::Nervure) && consommateur.count(f.esquisseId) && consommateur[f.esquisseId] == f.id)
      if (const FonctionPiece* e = m_piece.fonction(f.esquisseId)) {
        QTreeWidgetItem* s = element(it, qs(e->nom), nomIcone(*e), e->id);
        decorer(s, *e);
        it->setExpanded(m_fonctionsOuvertes.count(f.id) > 0);
      }
  }
  // Corps : les solides de la pièce, avec leur volume ; un clic les montre dans la vue
  if (!m_res.forme.IsNull()) {
    std::vector<TopoDS_Shape> solides;
    for (TopExp_Explorer ex(m_res.forme, TopAbs_SOLID); ex.More(); ex.Next()) solides.push_back(ex.Current());
    QTreeWidgetItem* corps = element(cible, QStringLiteral("Corps (%1)").arg(solides.size()), QStringLiteral("piece"), 0);
    corps->setData(0, Qt::UserRole + 5, false);
    for (size_t k = 0; k < solides.size(); ++k) {
      GProp_GProps gp;
      BRepGProp::VolumeProperties(solides[k], gp);
      QTreeWidgetItem* c = element(corps, QStringLiteral("Corps %1").arg(k + 1), QStringLiteral("piece"), 0);
      c->setData(0, Qt::UserRole + 5, false);
      c->setData(0, Qt::UserRole + 6, static_cast<int>(k));
      c->setToolTip(0, QStringLiteral("Volume %1 cm³").arg(QLocale().toString(gp.Mass() / 1000, 'f', 2)));
    }
    if (cible) cible->insertChild(std::min(1, cible->childCount()), cible->takeChild(cible->indexOfChild(corps)));
  }
  // États des fonctions dans l'arbre : erreur en rouge (message en bulle), désactivée en gris italique
  for (QTreeWidgetItemIterator itx(cible); *itx; ++itx) {
    const int fid = (*itx)->data(0, Qt::UserRole).toInt();
    const FonctionPiece* fx = m_piece.fonction(fid);
    if (!fx) continue;
    const std::string err = m_res.erreur(fid);
    if (!err.empty()) { (*itx)->setForeground(0, QColor(0xC2, 0x40, 0x2E)); (*itx)->setToolTip(0, QStringLiteral("⚠ %1").arg(qs(err))); (*itx)->setText(0, QStringLiteral("⚠ ") + qs(fx->nom)); }
    else if (const std::string av = m_res.avertissement(fid); !av.empty()) { (*itx)->setForeground(0, QColor(0xB5, 0x6A, 0x00)); (*itx)->setToolTip(0, QStringLiteral("ⓘ %1").arg(qs(av))); }
    if (fx->desactivee) { QFont fo = (*itx)->font(0); fo.setItalic(true); (*itx)->setFont(0, fo); (*itx)->setForeground(0, QColor(0x9A, 0xA0, 0xB8)); }
  }
  // Mises en plan de la pièce, rangées sous elle
  if (!m_piece.plans.empty()) {
    QTreeWidgetItem* dossier = element(cible, QStringLiteral("Mises en plan"), QStringLiteral("dossier"), 0);
    dossier->setData(0, Qt::UserRole + 5, false);
    for (size_t k = 0; k < m_piece.plans.size(); ++k) {
      QTreeWidgetItem* it = element(dossier, qs(m_piece.plans[k].nom), QStringLiteral("plan"), 0);
      it->setData(0, Qt::UserRole + 5, false);
      it->setData(0, Navigateur::ROLE_CLE, QStringLiteral("plan:%1:%2").arg(m_indice).arg(k));
    }
    dossier->setExpanded(true);
  }
  if (choisie) choisirFonction(choisie);
}

void AtelierPiece::majProprietes() {
  // Hors réglage et hors esquisse, le panneau de droite s'efface : la barre flottante et le clic droit suffisent.
  m_props->setVisible(m_gestion || m_esquisseEditee != 0 || m_indice < 0 || (!m_attenteEsquisse && m_piece.fonction(fonctionChoisie()) != nullptr));
  if (m_props->isVisible() && m_separation && m_props->width() < 300) {
    QList<int> t = m_separation->sizes();
    const int i = m_separation->indexOf(m_props), manque = 340 - t[i];
    if (i > 0 && manque > 0) { t[i] += manque; t[i - 1] = std::max(200, t[i - 1] - manque); m_separation->setSizes(t); }
  }
  // Hors réglage, rien ne doit rester d'un aperçu de fonction (régions, flèche d'extrusion)
  if (!m_gestion) { m_vue->setPoignee(false); }
  // En esquisse, le panneau est celui de l'esquisse (cotes, état, calques) : il n'est jamais remplacé ici (EI-03)
  if (m_esquisseEditee) return;
  auto* page = new QWidget;
  auto* col = new QVBoxLayout(page);
  auto* form = new QFormLayout;
  if (m_gestion) {
    // ---------- Gestionnaire de fonction ----------
    FonctionPiece& f = m_brouillon;
    page->setObjectName(QStringLiteral("panneauProprietes"));
    page->setStyleSheet(QString::fromLatin1(STYLE_PANNEAU));
    QPushButton* ok = nullptr;
    QPushButton* annuler = nullptr;
    enTetePanneau(col, iconeFonction(f), m_nouvelle ? QString::fromUtf8(nomTypeFonction(f.type)) : qs(f.nom), ok, annuler);
    connect(ok, &QPushButton::clicked, this, [this] { validerReglage(); });
    connect(annuler, &QPushButton::clicked, this, [this] { fermerGestionnaire(false); });
    const int idApercu = m_nouvelle ? m_piece.prochainId : f.id;
    const std::string err = m_affiche.erreur(idApercu);
    if (!err.empty()) {
      auto* e = new QLabel(qs(err));
      e->setWordWrap(true);
      e->setStyleSheet(QStringLiteral("color: white; background: #C2402E; padding: 6px; border-radius: 5px;"));
      col->addWidget(e);
    }
    const Unites u = unites();
    auto valeur = [&](QFormLayout* fl, const QString& libelle, double v, bool angle, double min, double max, std::function<void(FonctionPiece&, double)> ecrire) {
      auto* c = new ChampValeur(angle, u);
      c->setBornes(min, max);
      c->setValeur(v);
      connect(c, &ChampValeur::valeurChangee, this, [this, ecrire](double x) { ecrire(m_brouillon, x); apercu(); });
      fl->addRow(libelle, c);
      return c;
    };
    auto caseACocher = [&](QFormLayout* fl, const QString& libelle, bool v, std::function<void(FonctionPiece&, bool)> ecrire) {
      auto* c = new QCheckBox(libelle);
      c->setChecked(v);
      connect(c, &QCheckBox::toggled, this, [this, ecrire](bool x) { ecrire(m_brouillon, x); apercu(); majProprietes(); });
      fl->addRow(QString(), c);
      return c;
    };
    auto aide = [&](QFormLayout* fl, const QString& t) {
      auto* l = new QLabel(t);
      l->setWordWrap(true);
      l->setStyleSheet(QStringLiteral("color: #2F6BF5; padding: 2px 0;"));
      fl->addRow(l);
    };
    // Bouton de sélection dans la vue : actif, il est mis en évidence et reçoit les clics.
    auto choisir = [&](QFormLayout* fl, const QString& libelle, const QString& valeur, Champ c) {
      auto* b = new QPushButton(icone(QStringLiteral("filtre-auto")), valeur.isEmpty() ? QStringLiteral("Cliquer dans la vue…") : valeur);
      b->setObjectName(m_champ == c ? QStringLiteral("champActif") : QStringLiteral("champ"));
      connect(b, &QPushButton::clicked, this, [this, c] { activerChamp(m_champ == c ? Champ::Aucun : c); apercu(); majProprietes(); });
      fl->addRow(libelle, b);
    };
    auto groupe = [&](const QString& titre) {
      auto* g = new QGroupBox(titre);
      auto* fl = new QFormLayout(g);
      col->addWidget(g);
      return fl;
    };
    if (f.type == TypeFonction::Extrusion || f.type == TypeFonction::Revolution) {
      const FonctionPiece* fe = m_piece.fonction(f.esquisseId);
      auto* fs = groupe(QStringLiteral("Esquisse et contours"));
      choisir(fs, QStringLiteral("Esquisse"), fe ? qs(fe->nom) : QString(), Champ::Esquisse);
      if (fe) {
        const auto regions = regionsEsquisse(*fe, m_res.repere(fe->id));
        if (!regions.empty())  // même une région unique se choisit (cornière, tube : FO-01)
        {
          int parDefaut = 0;
          for (const auto& rg : regions) parDefaut += rg.parDefaut ? 1 : 0;
          choisir(fs, QStringLiteral("Contours"),
                  f.regions.empty() ? QStringLiteral("Par défaut : %1 sur %2").arg(parDefaut).arg(regions.size()) : QStringLiteral("Choisis : %1 sur %2").arg(f.regions.size()).arg(regions.size()),
                  Champ::Regions);
          if (m_champ == Champ::Regions)
            aide(fs, QStringLiteral("Clique les régions à extruder (en bleu) ; un second clic retire. Sans choix, les régions entourées un nombre impair de fois."));
        }
      }
      auto* op = new QComboBox;
      op->addItem(QStringLiteral("Ajouter de la matière"), false);
      op->addItem(QStringLiteral("Enlever de la matière"), true);
      op->setCurrentIndex(f.enlever ? 1 : 0);
      connect(op, &QComboBox::activated, this, [this, op] { m_brouillon.enlever = op->currentData().toBool(); apercu(); majProprietes(); });
      fs->addRow(QStringLiteral("Opération"), op);
      if (f.type == TypeFonction::Extrusion) {
        // Compatibilité avec les anciens réglages
        if (f.traversant) { f.fin = "traversant"; f.traversant = false; }
        if (f.symetrique) { f.fin = "milieu"; f.symetrique = false; }
        const std::pair<const char*, const char*> conditions[] = {{"borgne", "Borgne (distance)"}, {"traversant", "À travers tout"}, {"prochain", "Jusqu’au prochain"},
                                                                  {"sommet", "Jusqu’au sommet"}, {"face", "Jusqu’à la face"}, {"decale", "Décalé de la face"},
                                                                  {"milieu", "Plan milieu"}};
        auto direction = [&](bool seconde) {
          auto* fl = groupe(seconde ? QStringLiteral("Direction 2") : QStringLiteral("Direction 1"));
          const std::string fin = seconde ? f.fin2 : f.fin;
          if (seconde) {
            caseACocher(fl, QStringLiteral("Extruder aussi dans l’autre sens"), !f.fin2.empty(), [](FonctionPiece& g, bool x) { g.fin2 = x ? "borgne" : ""; });
            if (f.fin2.empty()) return;
          }
          auto* cond = new QComboBox;
          for (const auto& [cle, nom] : conditions)
            if (!(seconde && std::string(cle) == "milieu")) cond->addItem(QString::fromUtf8(nom), QString::fromLatin1(cle));
          cond->setCurrentIndex(std::max(0, cond->findData(QString::fromStdString(fin))));
          connect(cond, &QComboBox::activated, this, [this, cond, seconde] {
            const std::string c = cond->currentData().toString().toStdString();
            (seconde ? m_brouillon.fin2 : m_brouillon.fin) = c;
            if (c == "face" || c == "decale" || c == "sommet") activerChamp(seconde ? Champ::Fin2 : Champ::Fin1);
            apercu();
            majProprietes();
          });
          fl->addRow(QStringLiteral("Condition"), cond);
          if (!seconde) caseACocher(fl, QStringLiteral("Inverser le sens"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
          if (fin == "borgne" || fin == "milieu")
            valeur(fl, QStringLiteral("Profondeur"), seconde ? f.longueur2 : f.longueur, false, 0.001, 1e6, [seconde](FonctionPiece& g, double x) { (seconde ? g.longueur2 : g.longueur) = x; });
          if (fin == "face" || fin == "decale" || fin == "sommet") {
            const std::string ref = seconde ? f.refFin2 : f.refFin;
            choisir(fl, fin == "sommet" ? QStringLiteral("Sommet") : QStringLiteral("Face"), ref.empty() ? QString() : QStringLiteral("Choisi ✔"), seconde ? Champ::Fin2 : Champ::Fin1);
          }
          if (fin == "decale")
            valeur(fl, QStringLiteral("Décalage"), seconde ? f.decalageFin2 : f.decalageFin, false, -1e6, 1e6, [seconde](FonctionPiece& g, double x) { (seconde ? g.decalageFin2 : g.decalageFin) = x; });
        };
        direction(false);
        if (f.fin != "milieu") direction(true);
        auto* fd = groupe(QStringLiteral("Dépouille"));
        caseACocher(fd, QStringLiteral("Incliner les flancs"), f.depouille != 0, [](FonctionPiece& g, bool x) { g.depouille = x ? 2 : 0; });
        if (f.depouille != 0) valeur(fd, QStringLiteral("Angle"), f.depouille, true, -89, 89, [](FonctionPiece& g, double x) { g.depouille = x; });
      } else {
        auto* fr = groupe(QStringLiteral("Révolution"));
        auto* axe = new QComboBox;
        axe->setObjectName(QStringLiteral("axeRevolution"));
        // Lignes décrites par leur allure (horizontale, verticale, inclinée) et leur longueur, pas par leur numéro (M8)
        auto decrire = [&](const Esquisse& e, const ElementEsquisse& el) {
          const PointEsquisse *a = e.point(el.p1), *b = e.point(el.p2);
          if (!a || !b) return QStringLiteral("Ligne %1").arg(el.id);
          const double dx = b->x - a->x, dy = b->y - a->y, l = std::hypot(dx, dy);
          const QString allure = std::abs(dy) < 1e-6 * std::max(1.0, l) ? QStringLiteral("horizontale") : std::abs(dx) < 1e-6 * std::max(1.0, l) ? QStringLiteral("verticale") : QStringLiteral("inclinée");
          return QStringLiteral("Ligne %1 de %2%3").arg(allure, qs(ecrireValeur(std::round(l * 100) / 100, false, unites())), el.construction ? QStringLiteral(" (construction)") : QString());
        };
        if (fe)
          for (const auto& el : fe->esquisse.elements)
            if (el.type == TypeElement::Ligne) axe->addItem(decrire(fe->esquisse, el), el.id);
        axe->addItem(QStringLiteral("Axe X"), QStringLiteral("X"));
        axe->addItem(QStringLiteral("Axe Y"), QStringLiteral("Y"));
        axe->addItem(QStringLiteral("Axe Z"), QStringLiteral("Z"));
        // Lignes des autres esquisses
        for (const auto& g2 : m_piece.fonctions)
          if (g2.type == TypeFonction::Esquisse && fe && g2.id != fe->id)
            for (const auto& el : g2.esquisse.elements)
              if (el.type == TypeElement::Ligne)
                axe->addItem(qs(g2.nom) + QStringLiteral(" · ") + decrire(g2.esquisse, el), QStringLiteral("E:%1:%2").arg(g2.id).arg(el.id));
        const bool choisiAuClic = !f.axeBase.empty() && axe->findData(qs(f.axeBase)) < 0;
        if (choisiAuClic) axe->addItem(QStringLiteral("Choisi dans la vue : %1").arg(qs(f.axeBase.size() > 28 ? f.axeBase.substr(0, 26) + "…" : f.axeBase)), qs(f.axeBase));
        axe->addItem(QStringLiteral("Choisir dans la vue (arête, axe, face cylindrique)…"), QStringLiteral("?vue"));
        axe->setCurrentIndex(f.axeBase.empty() ? axe->findData(f.axe) : axe->findData(QString::fromStdString(f.axeBase)));
        connect(axe, &QComboBox::activated, this, [this, axe] {
          const QVariant d = axe->currentData();
          if (d.typeId() == QMetaType::QString && d.toString() == QStringLiteral("?vue")) {
            activerChamp(Champ::AxeRevolution);
            majProprietes();
            return;
          }
          if (d.typeId() == QMetaType::QString) { m_brouillon.axeBase = d.toString().toStdString(); }
          else { m_brouillon.axeBase.clear(); m_brouillon.axe = d.toInt(); }
          apercu();
        });
        fr->addRow(QStringLiteral("Axe"), axe);
        if (m_champ == Champ::AxeRevolution) aide(fr, QStringLiteral("Clique un axe de base, un axe de référence, une arête droite ou une face cylindrique."));
        caseACocher(fr, QStringLiteral("Inverser le sens de rotation"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
        valeur(fr, QStringLiteral("Angle"), f.angle, true, 0.01, 360, [](FonctionPiece& g, double x) { g.angle = x; });
        caseACocher(fr, QStringLiteral("Plan milieu (moitié de chaque côté)"), f.milieu, [](FonctionPiece& g, bool x) { g.milieu = x; });
        if (!f.milieu) {
          caseACocher(fr, QStringLiteral("Seconde direction"), f.angle2 > 0, [](FonctionPiece& g, bool x) { g.angle2 = x ? 90 : 0; });
          if (f.angle2 > 0) valeur(fr, QStringLiteral("Angle 2"), f.angle2, true, 0.01, 360, [](FonctionPiece& g, double x) { g.angle2 = x; });
        }
      }
      auto* fm = groupe(QStringLiteral("Fonction mince"));
      caseACocher(fm, QStringLiteral("Paroi d’épaisseur constante le long du contour"), f.mince, [](FonctionPiece& g, bool x) { g.mince = x; });
      if (f.mince) {
        valeur(fm, QStringLiteral("Épaisseur"), f.epaisseurMince, false, 0.001, 1e5, [](FonctionPiece& g, double x) { g.epaisseurMince = x; });
        auto* sens = new QComboBox;
        sens->addItem(QStringLiteral("Vers l’intérieur"), QStringLiteral("interieur"));
        sens->addItem(QStringLiteral("Vers l’extérieur"), QStringLiteral("exterieur"));
        sens->addItem(QStringLiteral("Centrée sur le contour"), QStringLiteral("milieu"));
        sens->setCurrentIndex(std::max(0, sens->findData(QString::fromStdString(f.sensMince))));
        connect(sens, &QComboBox::activated, this, [this, sens] { m_brouillon.sensMince = sens->currentData().toString().toStdString(); apercu(); });
        fm->addRow(QStringLiteral("Sens"), sens);
        if (f.sensMince != "interieur")
          caseACocher(fm, QStringLiteral("Coins extérieurs arrondis"), f.minceArrondi, [](FonctionPiece& g, bool x) { g.minceArrondi = x; });
        aide(fm, QStringLiteral("La paroi longe tous les contours, trous compris. Un contour ouvert devient un mur à bouts droits : "
                                "« extérieur » à gauche du sens de tracé de sa première ligne, « intérieur » à droite."));
      }
    } else if (f.type == TypeFonction::Repere) {
      // Repère : origine, plan (normale Z) et axe X, utilisable comme plan d'esquisse, axe ou point (FO-17)
      auto* fl = groupe(QStringLiteral("Repère"));
      QStringList noms;
      for (const auto& s2 : f.supports) noms << qs(s2.rfind("F:", 0) == 0 ? std::string("référence ") + s2.substr(2) : s2.size() > 22 ? s2.substr(0, 20) + "…" : s2);
      choisir(fl, QStringLiteral("Références"), f.supports.empty() ? QStringLiteral("aucune : repère global") : noms.join(QStringLiteral(", ")), Champ::Supports);
      aide(fl, QStringLiteral("Clique dans l’ordre l’origine (sommet ou point), le plan ou la face qui donne Z, puis au besoin l’arête de l’axe X. "
                              "Sans référence, le repère global est placé aux coordonnées ci-dessous."));
      if (f.supports.empty()) {
        valeur(fl, QStringLiteral("X"), f.x, false, -1e6, 1e6, [](FonctionPiece& g, double v) { g.x = v; });
        valeur(fl, QStringLiteral("Y"), f.y, false, -1e6, 1e6, [](FonctionPiece& g, double v) { g.y = v; });
        valeur(fl, QStringLiteral("Z"), f.z, false, -1e6, 1e6, [](FonctionPiece& g, double v) { g.z = v; });
      }
      caseACocher(fl, QStringLiteral("Inverser Z"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
    } else if (f.type == TypeFonction::Plan || f.type == TypeFonction::Axe || f.type == TypeFonction::Point) {
      auto* fl = groupe(QString::fromUtf8(nomTypeFonction(f.type)));
      auto* mode = new QComboBox;
      if (f.type == TypeFonction::Plan) {
        mode->addItem(QStringLiteral("Décalé d’un plan ou d’une face"), QStringLiteral("decale"));
        mode->addItem(QStringLiteral("Incliné autour d’une arête ou d’un axe"), QStringLiteral("angle"));
        mode->addItem(QStringLiteral("Par trois points"), QStringLiteral("troisPoints"));
        mode->addItem(QStringLiteral("Parallèle, passant par un point"), QStringLiteral("parallelePoint"));
      } else if (f.type == TypeFonction::Axe) {
        mode->addItem(QStringLiteral("Arête droite ou face cylindrique"), QStringLiteral("arete"));
        mode->addItem(QStringLiteral("Par deux points"), QStringLiteral("deuxPoints"));
        mode->addItem(QStringLiteral("Intersection de deux plans"), QStringLiteral("deuxPlans"));
      } else {
        mode->addItem(QStringLiteral("Par coordonnées"), QStringLiteral("coordonnees"));
        mode->addItem(QStringLiteral("Sur un sommet"), QStringLiteral("sommet"));
        mode->addItem(QStringLiteral("Au centre d’un cercle ou d’une face"), QStringLiteral("centre"));
      }
      mode->setCurrentIndex(std::max(0, mode->findData(qs(f.mode))));
      connect(mode, &QComboBox::activated, this, [this, mode] { m_brouillon.mode = mode->currentData().toString().toStdString(); m_brouillon.supports.clear(); apercu(); majProprietes(); });
      fl->addRow(QStringLiteral("Méthode"), mode);
      if (!(f.type == TypeFonction::Point && f.mode == "coordonnees")) {
        QStringList noms;
        for (const auto& s : f.supports) noms << qs(s.rfind("F:", 0) == 0 ? std::string("référence ") + s.substr(2) : s.size() > 22 ? s.substr(0, 20) + "…" : s);
        choisir(fl, QStringLiteral("Références"), f.supports.empty() ? QString() : noms.join(QStringLiteral(", ")), Champ::Supports);
        const QString attendu = f.mode == "decale" ? QStringLiteral("un plan de base, un plan de référence ou une face plane")
                              : f.mode == "angle" ? QStringLiteral("un plan ou une face, puis l’arête ou l’axe de rotation")
                              : f.mode == "troisPoints" ? QStringLiteral("trois sommets ou points")
                              : f.mode == "parallelePoint" ? QStringLiteral("un plan ou une face, puis un sommet")
                              : f.mode == "deuxPoints" ? QStringLiteral("deux sommets ou points")
                              : f.mode == "deuxPlans" ? QStringLiteral("deux plans ou faces planes")
                              : f.mode == "arete" ? QStringLiteral("une arête droite ou une face cylindrique")
                              : f.mode == "sommet" ? QStringLiteral("un sommet") : QStringLiteral("une arête circulaire ou une face");
        aide(fl, QStringLiteral("Clique dans la vue %1 ; un second clic retire.").arg(attendu));
      }
      if (f.type == TypeFonction::Plan && f.mode == "decale") valeur(fl, QStringLiteral("Distance"), f.distance, false, -1e5, 1e5, [](FonctionPiece& g, double x) { g.distance = x; });
      if (f.type == TypeFonction::Plan && f.mode == "angle") valeur(fl, QStringLiteral("Angle"), f.angle, true, -360, 360, [](FonctionPiece& g, double x) { g.angle = x; });
      if (f.type == TypeFonction::Plan && (f.mode == "decale" || f.mode == "angle")) caseACocher(fl, QStringLiteral("Inverser"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
      if (f.type == TypeFonction::Point && f.mode == "coordonnees") {
        valeur(fl, QStringLiteral("X"), f.x, false, -1e6, 1e6, [](FonctionPiece& g, double v) { g.x = v; });
        valeur(fl, QStringLiteral("Y"), f.y, false, -1e6, 1e6, [](FonctionPiece& g, double v) { g.y = v; });
        valeur(fl, QStringLiteral("Z"), f.z, false, -1e6, 1e6, [](FonctionPiece& g, double v) { g.z = v; });
      }
    } else if (f.type == TypeFonction::Percage) {
      const FonctionPiece* fe = m_piece.fonction(f.esquisseId);
      auto* fs = groupe(QStringLiteral("Position"));
      choisir(fs, QStringLiteral("Esquisse"), fe ? qs(fe->nom) : QString(), Champ::Esquisse);
      aide(fs, QStringLiteral("Les points de l’esquisse (ou les centres de ses cercles) donnent les centres des trous, perpendiculaires à son plan."));
      auto* ft = groupe(QStringLiteral("Trou"));
      auto* type = new QComboBox;
      type->addItem(QStringLiteral("Simple"), QStringLiteral("simple"));
      type->addItem(QStringLiteral("Taraudé"), QStringLiteral("taraude"));
      type->addItem(QStringLiteral("Lamé (vis à tête cylindrique)"), QStringLiteral("lame"));
      type->addItem(QStringLiteral("Fraisé (vis à tête fraisée)"), QStringLiteral("fraise"));
      type->setCurrentIndex(std::max(0, type->findData(qs(f.typePercage))));
      connect(type, &QComboBox::activated, this, [this, type] { m_brouillon.typePercage = type->currentData().toString().toStdString(); apercu(); majProprietes(); });
      ft->addRow(QStringLiteral("Type"), type);
      if (f.typePercage == "simple") valeur(ft, QStringLiteral("Diamètre"), f.diametre, false, 0.01, 1e4, [](FonctionPiece& g, double x) { g.diametre = x; });
      else {
        auto* norme = new QComboBox;
        norme->addItem(QStringLiteral("ISO (métrique)"), QStringLiteral("ISO"));
        norme->addItem(QStringLiteral("ANSI (pouces, UNC)"), QStringLiteral("ANSI"));
        norme->setCurrentIndex(f.norme == "ANSI" ? 1 : 0);
        connect(norme, &QComboBox::activated, this, [this, norme] {
          m_brouillon.norme = norme->currentData().toString().toStdString();
          for (const auto& d : tableTrous()) if (d.norme == m_brouillon.norme) { m_brouillon.taille = d.taille; break; }
          apercu(); majProprietes();
        });
        ft->addRow(QStringLiteral("Norme"), norme);
        auto* taille = new QComboBox;
        for (const auto& d : tableTrous()) if (d.norme == f.norme) taille->addItem(qs(d.taille), qs(d.taille));
        taille->setCurrentIndex(std::max(0, taille->findData(qs(f.taille))));
        connect(taille, &QComboBox::activated, this, [this, taille] { m_brouillon.taille = taille->currentData().toString().toStdString(); apercu(); majProprietes(); });
        ft->addRow(QStringLiteral("Taille de vis"), taille);
        if (f.typePercage != "taraude") {
          auto* aj = new QComboBox;
          aj->addItem(QStringLiteral("Serré"), QStringLiteral("serre"));
          aj->addItem(QStringLiteral("Normal"), QStringLiteral("normal"));
          aj->addItem(QStringLiteral("Large"), QStringLiteral("large"));
          aj->setCurrentIndex(std::max(0, aj->findData(qs(f.ajustement))));
          connect(aj, &QComboBox::activated, this, [this, aj] { m_brouillon.ajustement = aj->currentData().toString().toStdString(); apercu(); });
          ft->addRow(QStringLiteral("Passage"), aj);
        }
        if (const DonneesTrou* d = donneesTrou(f.norme, f.taille)) {
          QString info;
          if (f.typePercage == "taraude") info = QStringLiteral("Forage Ø %1 mm, pas %2 mm").arg(QLocale().toString(d->forageTaraud, 'f', 2), QLocale().toString(d->pas, 'f', 3));
          else info = QStringLiteral("Passage Ø %1 mm").arg(QLocale().toString(f.ajustement == "serre" ? d->passageSerre : f.ajustement == "large" ? d->passageLarge : d->passageNormal, 'f', 2));
          if (f.typePercage == "lame") info += QStringLiteral(", lamage Ø %1 × %2 mm").arg(QLocale().toString(d->lamageD, 'f', 1), QLocale().toString(d->lamageH, 'f', 1));
          if (f.typePercage == "fraise") info += QStringLiteral(", fraisure Ø %1 mm à 90°").arg(QLocale().toString(d->fraisureD, 'f', 1));
          aide(ft, info);
        }
      }
      auto* fp = groupe(QStringLiteral("Profondeur"));
      caseACocher(fp, QStringLiteral("À travers tout"), f.traversant, [](FonctionPiece& g, bool x) { g.traversant = x; });
      if (!f.traversant) {
        valeur(fp, QStringLiteral("Profondeur"), f.profondeur, false, 0.01, 1e5, [](FonctionPiece& g, double x) { g.profondeur = x; });
        caseACocher(fp, QStringLiteral("Fond conique de foret (118°)"), f.pointeForet, [](FonctionPiece& g, bool x) { g.pointeForet = x; });
      }
      if (f.typePercage == "taraude") valeur(fp, QStringLiteral("Longueur filetée"), f.longueurFiletee, false, 0, 1e5, [](FonctionPiece& g, double x) { g.longueurFiletee = x; });
      caseACocher(fp, QStringLiteral("Inverser le sens"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
    } else if (f.type == TypeFonction::Repetition || f.type == TypeFonction::Symetrie) {
      auto* fl = groupe(QString::fromUtf8(nomTypeFonction(f.type)));
      QStringList noms;
      for (int s : f.sources) if (const FonctionPiece* g = m_piece.fonction(s)) noms << qs(g->nom);
      if (f.type == TypeFonction::Symetrie) {
        auto* plan = new QComboBox;
        plan->addItems({QStringLiteral("Plan de dessus (XY)"), QStringLiteral("Plan de face (XZ)"), QStringLiteral("Plan de droite (YZ)"), QStringLiteral("Plan ou face choisi dans la vue")});
        const std::string sp = f.supports.empty() ? std::string() : f.supports.front();
        plan->setCurrentIndex(sp == "XY" ? 0 : sp == "XZ" ? 1 : sp == "YZ" ? 2 : 3);
        connect(plan, &QComboBox::activated, this, [this](int i) {
          if (i < 3) m_brouillon.supports = {i == 0 ? "XY" : i == 1 ? "XZ" : "YZ"};
          else { m_brouillon.supports.clear(); activerChamp(Champ::Supports); }
          apercu();
          majProprietes();
        });
        fl->addRow(QStringLiteral("Plan de symétrie"), plan);
        if (plan->currentIndex() == 3) choisir(fl, QStringLiteral("Plan choisi"), sp.empty() ? QString() : qs(sp), Champ::Supports);
        // « Tout le corps » est un choix à part : le décocher ne dépend pas d'une liste déjà remplie (AP-08)
        if (!f.sources.empty()) m_symetrieParFonctions = true;
        auto* tout = new QCheckBox(QStringLiteral("Tout le corps"));
        tout->setChecked(!m_symetrieParFonctions);
        connect(tout, &QCheckBox::toggled, this, [this](bool on) {
          m_symetrieParFonctions = !on;
          if (on) {
            m_brouillon.sources.clear();
            activerChamp(Champ::Aucun);
          } else {
            activerChamp(Champ::Sources);
          }
          apercu();
          majProprietes();
        });
        fl->addRow(QString(), tout);
        if (m_symetrieParFonctions) choisir(fl, QStringLiteral("Fonctions"), noms.isEmpty() ? QString() : noms.join(QStringLiteral(", ")), Champ::Sources);
        aide(fl, QStringLiteral("Choisis le plan dans la liste (ou clique un plan de référence ou une face plane). Pour ne copier que certaines fonctions, décoche « Tout le corps » puis clique-les dans l’arbre ou une de leurs faces dans la vue."));
      } else {
        auto* type = new QComboBox;
        type->addItem(icone(QStringLiteral("repetition-lineaire")), QStringLiteral("Linéaire"), QStringLiteral("lineaire"));
        type->addItem(icone(QStringLiteral("repetition-circulaire")), QStringLiteral("Circulaire"), QStringLiteral("circulaire"));
        type->addItem(QStringLiteral("Pilotée par une esquisse"), QStringLiteral("esquisse"));
        type->setCurrentIndex(std::max(0, type->findData(qs(f.typeRepetition))));
        connect(type, &QComboBox::activated, this, [this, type] { m_brouillon.typeRepetition = type->currentData().toString().toStdString(); if (m_brouillon.typeRepetition == "circulaire") { m_brouillon.direction1 = "Z"; m_brouillon.angle = 360; } apercu(); majProprietes(); });
        fl->addRow(QStringLiteral("Type"), type);
        choisir(fl, QStringLiteral("Fonctions à répéter"), noms.join(QStringLiteral(", ")), Champ::Sources);
        if (f.typeRepetition == "esquisse") {
          const FonctionPiece* fe = m_piece.fonction(f.esquisseId);
          choisir(fl, QStringLiteral("Esquisse des positions"), fe ? qs(fe->nom) : QString(), Champ::Esquisse);
          aide(fl, QStringLiteral("La fonction est recopiée sur chaque point de l’esquisse, le premier point servant de référence."));
        } else {
          auto* dir = new QComboBox;
          dir->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
          const bool base = f.direction1 == "X" || f.direction1 == "Y" || f.direction1 == "Z";
          if (!base) dir->addItem(QStringLiteral("Arête ou axe choisi"));
          dir->setCurrentIndex(base ? (f.direction1 == "X" ? 0 : f.direction1 == "Y" ? 1 : 2) : 3);
          connect(dir, &QComboBox::activated, this, [this, dir](int i) { if (i < 3) m_brouillon.direction1 = dir->itemText(i).toStdString(); apercu(); majProprietes(); });
          fl->addRow(f.typeRepetition == "circulaire" ? QStringLiteral("Axe") : QStringLiteral("Direction"), dir);
          choisir(fl, QString(), QStringLiteral("…ou cliquer une arête, un axe, une face cylindrique"), Champ::Direction);
          auto* nombre = new QSpinBox;
          nombre->setRange(2, 1000);
          nombre->setValue(f.nombre1);
          connect(nombre, &QSpinBox::valueChanged, this, [this](int n) { m_brouillon.nombre1 = n; apercu(); });
          fl->addRow(QStringLiteral("Nombre d’exemplaires"), nombre);
          if (f.typeRepetition == "circulaire") valeur(fl, QStringLiteral("Angle total"), f.angle, true, 1, 360, [](FonctionPiece& g, double x) { g.angle = x; });
          else valeur(fl, QStringLiteral("Pas"), f.pas1, false, -1e5, 1e5, [](FonctionPiece& g, double x) { g.pas1 = x; });
          caseACocher(fl, QStringLiteral("Inverser le sens"), f.inverse1, [](FonctionPiece& g, bool x) { g.inverse1 = x; });
          if (f.typeRepetition == "lineaire") {
            auto* deux = new QCheckBox(QStringLiteral("Seconde direction"));
            deux->setChecked(!f.direction2.empty());
            connect(deux, &QCheckBox::toggled, this, [this](bool on) { m_brouillon.direction2 = on ? (m_brouillon.direction1 == "X" ? "Y" : "X") : ""; apercu(); majProprietes(); });
            fl->addRow(QString(), deux);
            if (!f.direction2.empty()) {
              auto* dir2 = new QComboBox;
              dir2->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
              dir2->setCurrentIndex(f.direction2 == "X" ? 0 : f.direction2 == "Y" ? 1 : 2);
              connect(dir2, &QComboBox::activated, this, [this, dir2](int i) { m_brouillon.direction2 = dir2->itemText(i).toStdString(); apercu(); });
              fl->addRow(QStringLiteral("Direction 2"), dir2);
              auto* n2 = new QSpinBox;
              n2->setRange(1, 1000);
              n2->setValue(f.nombre2);
              connect(n2, &QSpinBox::valueChanged, this, [this](int n) { m_brouillon.nombre2 = n; apercu(); });
              fl->addRow(QStringLiteral("Nombre (direction 2)"), n2);
              valeur(fl, QStringLiteral("Pas (direction 2)"), f.pas2, false, -1e5, 1e5, [](FonctionPiece& g, double x) { g.pas2 = x; });
            }
          }
        }
      }
    } else if (f.type == TypeFonction::Esquisse3D) {
      auto* fl = groupe(QStringLiteral("Points (%1)").arg(f.points3d.size()));
      auto* table = new QTableWidget(static_cast<int>(f.points3d.size()), 3);
      table->setHorizontalHeaderLabels({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
      table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
      table->setMinimumHeight(150);
      {
        const QSignalBlocker b(table);
        for (size_t i = 0; i < f.points3d.size(); ++i)
          for (int c = 0; c < 3; ++c) table->setItem(static_cast<int>(i), c, new QTableWidgetItem(QString::fromStdString(ecrireValeur(f.points3d[i][static_cast<size_t>(c)], false, u))));
      }
      connect(table, &QTableWidget::itemChanged, this, [this, table](QTableWidgetItem* it) {
        double v = 0;
        std::string err;
        if (!lireValeur(it->text().toStdString(), false, unites(), v, &err)) { m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(qs(err).toHtmlEscaped())); return; }
        const size_t r = static_cast<size_t>(it->row());
        if (r < m_brouillon.points3d.size()) m_brouillon.points3d[r][static_cast<size_t>(it->column())] = v;
        (void)table;
        apercu();
      });
      fl->addRow(table);
      auto* ligne = new QHBoxLayout;
      auto* plus = new QPushButton(icone(QStringLiteral("point")), QStringLiteral("Ajouter un point"));
      plus->setObjectName(QStringLiteral("champ"));
      connect(plus, &QPushButton::clicked, this, [this] {
        auto& pts = m_brouillon.points3d;
        std::array<double, 3> q = pts.empty() ? std::array<double, 3>{0, 0, 0} : pts.back();
        q[0] += 20;
        pts.push_back(q);
        apercu();
        majProprietes();
      });
      auto* moins = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Retirer"));
      moins->setObjectName(QStringLiteral("champ"));
      connect(moins, &QPushButton::clicked, this, [this, table] {
        const int r = table->currentRow();
        auto& pts = m_brouillon.points3d;
        if (r >= 0 && r < static_cast<int>(pts.size()) && pts.size() > 2) pts.erase(pts.begin() + r);
        apercu();
        majProprietes();
      });
      ligne->addWidget(plus);
      ligne->addWidget(moins);
      fl->addRow(ligne);
      choisir(fl, QStringLiteral("Placer à la souris"), m_champ == Champ::Points3D ? QStringLiteral("actif : chaque clic ajoute un point") : QStringLiteral("cliquer pour placer des points dans la vue"), Champ::Points3D);
      aide(fl, QStringLiteral("Un clic sur un sommet, une arête ou une face y pose le point exactement ; dans le vide, il se pose face à l’écran à la hauteur du dernier point. Rectifie ensuite les coordonnées dans le tableau."));
      caseACocher(fl, QStringLiteral("Courbe lisse passant par les points"), f.lisse, [](FonctionPiece& g, bool x) { g.lisse = x; });
      caseACocher(fl, QStringLiteral("Fermée"), f.fermee, [](FonctionPiece& g, bool x) { g.fermee = x; });
      aide(fl, QStringLiteral("Une esquisse 3D sert de chemin à un balayage (tube, câble, nervure) : tape les coordonnées ou prends des sommets de la pièce."));
    } else if (f.type == TypeFonction::Balayage || f.type == TypeFonction::Lissage) {
      auto* fl = groupe(QString::fromUtf8(nomTypeFonction(f.type)));
      if (f.type == TypeFonction::Balayage) {
        const FonctionPiece* fp = m_piece.fonction(f.esquisseId);
        const FonctionPiece* fc = m_piece.fonction(f.cheminId);
        caseACocher(fl, QStringLiteral("Tube : profil circulaire automatique"), f.tube, [](FonctionPiece& g, bool x) { g.tube = x; });
        if (f.tube) {
          valeur(fl, QStringLiteral("Diamètre extérieur"), f.diametre, false, 0.01, 1e5, [](FonctionPiece& g, double x) { g.diametre = x; });
          valeur(fl, QStringLiteral("Épaisseur de paroi (0 : plein)"), f.epaisseur, false, 0, 1e5, [](FonctionPiece& g, double x) { g.epaisseur = x; });
        } else {
          choisir(fl, QStringLiteral("Profil"), fp ? qs(fp->nom) : QString(), Champ::Esquisse);
        }
        choisir(fl, QStringLiteral("Chemin"), fc ? qs(fc->nom) : QString(), Champ::Chemin);
        aide(fl, f.tube ? QStringLiteral("Le cercle se place au début du chemin, perpendiculaire à lui, et le suit jusqu’au bout (esquisse 2D ou 3D).")
                        : QStringLiteral("Le profil (contour fermé) glisse le long du chemin (esquisse 2D ou 3D, qui part du plan du profil)."));
      } else {
        QStringList noms;
        for (int s : f.sources) if (const FonctionPiece* g = m_piece.fonction(s)) noms << qs(g->nom);
        choisir(fl, QStringLiteral("Profils, dans l’ordre"), noms.join(QStringLiteral(" → ")), Champ::Profils);
        // Liste ordonnée : chaque profil se remonte, se descend ou se retire (AP-35)
        for (std::size_t k = 0; k < f.sources.size(); ++k) {
          const FonctionPiece* g = m_piece.fonction(f.sources[k]);
          auto* ligne = new QWidget;
          auto* h = new QHBoxLayout(ligne);
          h->setContentsMargins(0, 0, 0, 0);
          h->setSpacing(2);
          h->addWidget(new QLabel(QStringLiteral("%1. %2").arg(k + 1).arg(g ? qs(g->nom) : QStringLiteral("?"))), 1);
          auto bouton = [&](const QString& texte, const QString& bulle, std::function<void()> fn) {
            auto* b = new QToolButton;
            b->setText(texte);
            b->setToolTip(bulle);
            b->setAutoRaise(true);
            connect(b, &QToolButton::clicked, this, [this, fn] { fn(); apercu(); majProprietes(); });
            h->addWidget(b);
          };
          if (k > 0) bouton(QStringLiteral("↑"), QStringLiteral("Monter"), [this, k] { if (k < m_brouillon.sources.size()) std::swap(m_brouillon.sources[k], m_brouillon.sources[k - 1]); });
          if (k + 1 < f.sources.size()) bouton(QStringLiteral("↓"), QStringLiteral("Descendre"), [this, k] { if (k + 1 < m_brouillon.sources.size()) std::swap(m_brouillon.sources[k], m_brouillon.sources[k + 1]); });
          bouton(QStringLiteral("✕"), QStringLiteral("Retirer"), [this, k] { if (k < m_brouillon.sources.size()) m_brouillon.sources.erase(m_brouillon.sources.begin() + static_cast<std::ptrdiff_t>(k)); });
          fl->addRow(QString(), ligne);
        }
        aide(fl, QStringLiteral("Clique les esquisses des profils l’une après l’autre (dans la vue ou dans l’arbre) ; un second clic retire, les flèches changent l’ordre."));
      }
      caseACocher(fl, QStringLiteral("Enlever de la matière"), f.enlever, [](FonctionPiece& g, bool x) { g.enlever = x; });
    } else if (f.type == TypeFonction::Import) {
      // Corps importé : son origine et ce qu'il contient, et le rechargement depuis un fichier (AP-25)
      auto* fl = groupe(QStringLiteral("Corps importé"));
      fl->addRow(QStringLiteral("Fichier d’origine"), new QLabel(f.source.empty() ? QStringLiteral("inconnu") : qs(f.source).toHtmlEscaped()));
      const TopoDS_Shape forme = formeDepuisBrep(f.donnees);
      if (!forme.IsNull()) {
        const Boite b = encombrement(forme);
        const Unites un = unites();
        auto Lg = [&un](double mm) { return QString::fromStdString(ecrireValeur(std::round(mm * 100) / 100, false, un)); };
        fl->addRow(QStringLiteral("Solides"), new QLabel(QString::number(nombreSolides(forme))));
        fl->addRow(QStringLiteral("Encombrement"), new QLabel(QStringLiteral("%1 × %2 × %3").arg(Lg(b.xmax - b.xmin), Lg(b.ymax - b.ymin), Lg(b.zmax - b.zmin))));
      } else {
        fl->addRow(QString(), new QLabel(QStringLiteral("<span style='color:#C2402E'>Géométrie illisible.</span>")));
      }
      auto* recharger = new QPushButton(QStringLiteral("Recharger depuis un fichier…"));
      connect(recharger, &QPushButton::clicked, this, [this] {
        const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Recharger le corps importé"), QString(),
                                                            QStringLiteral("CAO (*.step *.stp *.igs *.iges *.brep *.stl)"));
        if (chemin.isEmpty()) return;
        std::string err;
        const std::string brep = lireFichierCAO(chemin.toStdString(), &err);
        if (brep.empty()) { m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(qs(err).toHtmlEscaped())); return; }
        m_brouillon.donnees = deposerGeometrie(brep);
        m_brouillon.source = QFileInfo(chemin).fileName().toStdString();
        apercu();
        majProprietes();
      });
      fl->addRow(QString(), recharger);
      aide(fl, QStringLiteral("Le corps garde la géométrie lue à l’import ; les fonctions qui suivent (perçages, congés…) s’y appliquent comme à un corps modélisé."));
    } else if (f.type == TypeFonction::Tole) {
      const FonctionPiece* fe = m_piece.fonction(f.esquisseId);
      auto* fs = groupe(QStringLiteral("Contour"));
      choisir(fs, QStringLiteral("Esquisse"), fe ? qs(fe->nom) : QString(), Champ::Esquisse);
      if (fe && regionsEsquisse(*fe, m_res.repere(fe->id)).size() > 1)
        choisir(fs, QStringLiteral("Régions"), f.regions.empty() ? QStringLiteral("toutes (par défaut)") : QStringLiteral("%1 choisie(s)").arg(f.regions.size()), Champ::Regions);
      auto* fl = groupe(QStringLiteral("Tôle"));
      valeur(fl, QStringLiteral("Épaisseur"), f.epaisseur, false, 0.01, 100, [](FonctionPiece& g, double x) { g.epaisseur = x; });
      valeur(fl, QStringLiteral("Rayon de pliage"), f.rayon, false, 0.01, 1000, [](FonctionPiece& g, double x) { g.rayon = x; });
      auto* kAuto = caseACocher(fl, QStringLiteral("Facteur K selon DIN 6935"), f.facteurK <= 0, [](FonctionPiece& g, bool x) { g.facteurK = x ? 0 : facteurKDin6935(g.rayon, g.epaisseur); });
      kAuto->setObjectName(QStringLiteral("facteurKAuto"));
      if (f.facteurK > 0) {
        auto* k = new QDoubleSpinBox;
        k->setObjectName(QStringLiteral("facteurK"));
        k->setRange(0.01, 1);
        k->setDecimals(3);
        k->setSingleStep(0.01);
        k->setValue(f.facteurK);
        connect(k, &QDoubleSpinBox::valueChanged, this, [this](double x) { m_brouillon.facteurK = x; apercu(); });
        fl->addRow(QStringLiteral("Facteur K"), k);
      }
      auto* deg = new QComboBox;
      deg->setObjectName(QStringLiteral("degagement"));
      deg->addItem(QStringLiteral("Rectangulaires"), QStringLiteral("rectangulaire"));
      deg->addItem(QStringLiteral("Oblongs"), QStringLiteral("oblong"));
      deg->addItem(QStringLiteral("Aucun"), QStringLiteral("aucun"));
      deg->setCurrentIndex(std::max(0, deg->findData(qs(f.degagement))));
      connect(deg, &QComboBox::activated, this, [this, deg] { m_brouillon.degagement = deg->currentData().toString().toStdString(); apercu(); });
      fl->addRow(QStringLiteral("Dégagements de pli"), deg);
      caseACocher(fl, QStringLiteral("Épaissir sous l’esquisse"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
      const double k = facteurKTole(f);
      aide(fl, QStringLiteral("K = %1 : un pli à 90° développe %2 mm. Ensuite, « Bord tombé » sur une arête de la tôle.")
                   .arg(QLocale().toString(k, 'f', 3), QLocale().toString(longueurDeveloppee(90, f.rayon, f.epaisseur, k), 'f', 3)));
    } else if (f.type == TypeFonction::Nervure) {
      const FonctionPiece* fe = m_piece.fonction(f.esquisseId);
      auto* fl = groupe(QStringLiteral("Nervure"));
      choisir(fl, QStringLiteral("Esquisse du trait"), fe ? qs(fe->nom) : QString(), Champ::Esquisse);
      valeur(fl, QStringLiteral("Épaisseur"), f.epaisseur, false, 0.01, 1e4, [](FonctionPiece& g, double x) { g.epaisseur = x; });
      auto* mode = new QComboBox;
      mode->setObjectName(QStringLiteral("modeNervure"));
      mode->addItem(QStringLiteral("Parallèle au plan (remplit jusqu’à la pièce)"), QStringLiteral("parallele"));
      mode->addItem(QStringLiteral("Normale au plan (jusqu’à la pièce)"), QStringLiteral("normale"));
      mode->setCurrentIndex(std::max(0, mode->findData(qs(f.mode))));
      connect(mode, &QComboBox::activated, this, [this, mode] { m_brouillon.mode = mode->currentData().toString().toStdString(); apercu(); });
      fl->addRow(QStringLiteral("Direction"), mode);
      auto* sens = new QComboBox;
      sens->addItem(QStringLiteral("Des deux côtés du trait"), QStringLiteral("milieu"));
      sens->addItem(QStringLiteral("D’un côté"), QStringLiteral("exterieur"));
      sens->addItem(QStringLiteral("De l’autre côté"), QStringLiteral("interieur"));
      sens->setCurrentIndex(std::max(0, sens->findData(qs(f.sensMince))));
      connect(sens, &QComboBox::activated, this, [this, sens] { m_brouillon.sensMince = sens->currentData().toString().toStdString(); apercu(); });
      fl->addRow(QStringLiteral("Épaisseur"), sens);
      caseACocher(fl, f.mode == "normale" ? QStringLiteral("Inverser le sens") : QStringLiteral("Remplir l’autre côté du trait"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
      aide(fl, QStringLiteral("Trace un trait ouvert (ligne) d’une paroi à l’autre dans le plan de la nervure ; ses bouts sont prolongés jusqu’à la pièce."));
    } else if (f.type == TypeFonction::Filetage) {
      auto* fl = groupe(QStringLiteral("Filetage"));
      choisir(fl, QStringLiteral("Face cylindrique"), f.references.empty() ? QString() : qs(f.references.front()), Champ::Faces);
      if (m_champ == Champ::Faces) aide(fl, QStringLiteral("Clique la face d’un trou (taraudage) ou d’un arbre (filetage extérieur)."));
      auto* norme = new QComboBox;
      norme->addItem(QStringLiteral("ISO métrique"), QStringLiteral("ISO"));
      norme->addItem(QStringLiteral("UNC (pouces)"), QStringLiteral("ANSI"));
      norme->setCurrentIndex(std::max(0, norme->findData(qs(f.norme))));
      connect(norme, &QComboBox::activated, this, [this, norme] { m_brouillon.norme = norme->currentData().toString().toStdString(); m_brouillon.taille.clear(); apercu(); majProprietes(); });
      fl->addRow(QStringLiteral("Norme"), norme);
      auto* taille = new QComboBox;
      taille->setObjectName(QStringLiteral("tailleFiletage"));
      taille->addItem(QStringLiteral("D’après le diamètre"), QString());
      for (const auto& d : tableTrous())
        if (d.norme == (f.norme.empty() ? std::string("ISO") : f.norme)) taille->addItem(qs(d.taille), qs(d.taille));
      taille->setCurrentIndex(std::max(0, taille->findData(qs(f.taille))));
      connect(taille, &QComboBox::activated, this, [this, taille] { m_brouillon.taille = taille->currentData().toString().toStdString(); apercu(); });
      fl->addRow(QStringLiteral("Taille"), taille);
      valeur(fl, QStringLiteral("Pas (0 : celui de la taille)"), f.pasFiletage, false, 0, 10, [](FonctionPiece& g, double x) { g.pasFiletage = x; });
      valeur(fl, QStringLiteral("Longueur (0 : toute la face)"), f.longueurFiletee, false, 0, 1e5, [](FonctionPiece& g, double x) { g.longueurFiletee = x; });
      caseACocher(fl, QStringLiteral("Partir de l’autre bout"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
      const int idApercu2 = m_nouvelle ? m_piece.prochainId : f.id;
      for (const auto& ft : m_affiche.filetages)
        if (ft.fonction == idApercu2)
          aide(fl, QStringLiteral("%1 %2 sur %3 mm (Ø nominal %4, Ø %5 %6).")
                       .arg(qs(ft.designation), ft.exterieur ? QStringLiteral("extérieur") : QStringLiteral("intérieur"), QLocale().toString(ft.longueur, 'f', 2),
                            QLocale().toString(ft.nominal, 'f', 2), QLocale().toString(ft.mineur, 'f', 2), ft.exterieur ? QStringLiteral("à fond de filet") : QStringLiteral("de perçage")));
    } else if (f.type == TypeFonction::BordTombe) {
      auto* fl = groupe(QStringLiteral("Bord tombé"));
      choisir(fl, QStringLiteral("Arête"), f.references.empty() ? QString() : qs(f.references.front()), Champ::Aretes);
      if (m_champ == Champ::Aretes) aide(fl, QStringLiteral("Clique l’arête droite (ou la face mince) de la tôle le long de laquelle plier."));
      valeur(fl, QStringLiteral("Longueur"), f.longueur, false, 0.01, 1e5, [](FonctionPiece& g, double x) { g.longueur = x; });
      auto* mesure = new QComboBox;
      mesure->setObjectName(QStringLiteral("mesureBord"));
      mesure->addItem(QStringLiteral("Extérieure (au sommet virtuel)"), QStringLiteral("exterieure"));
      mesure->addItem(QStringLiteral("Intérieure"), QStringLiteral("interieure"));
      mesure->addItem(QStringLiteral("Depuis la fin du pli"), QStringLiteral("tangente"));
      mesure->setCurrentIndex(std::max(0, mesure->findData(qs(f.mesure))));
      connect(mesure, &QComboBox::activated, this, [this, mesure] { m_brouillon.mesure = mesure->currentData().toString().toStdString(); apercu(); });
      fl->addRow(QStringLiteral("Longueur mesurée"), mesure);
      valeur(fl, QStringLiteral("Angle"), f.angle, true, 1, 180, [](FonctionPiece& g, double x) { g.angle = x; });
      auto* rayonTole = caseACocher(fl, QStringLiteral("Rayon de la tôle"), f.rayon <= 0, [this](FonctionPiece& g, bool x) { g.rayon = x ? 0 : (m_res.tole.present ? m_res.tole.rayon : 1.5); });
      (void)rayonTole;
      if (f.rayon > 0) valeur(fl, QStringLiteral("Rayon"), f.rayon, false, 0.01, 1000, [](FonctionPiece& g, double x) { g.rayon = x; });
      auto* pos = new QComboBox;
      pos->setObjectName(QStringLiteral("positionPli"));
      pos->addItem(QStringLiteral("Matière à l’intérieur (cote extérieure gardée)"), QStringLiteral("matiere-interieure"));
      pos->addItem(QStringLiteral("Matière à l’extérieur"), QStringLiteral("matiere-exterieure"));
      pos->addItem(QStringLiteral("Pli à l’extérieur de l’arête"), QStringLiteral("pli-exterieur"));
      pos->setCurrentIndex(std::max(0, pos->findData(qs(f.positionPli))));
      connect(pos, &QComboBox::activated, this, [this, pos] { m_brouillon.positionPli = pos->currentData().toString().toStdString(); apercu(); });
      fl->addRow(QStringLiteral("Position"), pos);
      caseACocher(fl, QStringLiteral("Plier vers le dessous"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
      valeur(fl, QStringLiteral("Retrait au début"), f.retraitDebut, false, 0, 1e5, [](FonctionPiece& g, double x) { g.retraitDebut = x; });
      valeur(fl, QStringLiteral("Retrait à la fin"), f.retraitFin, false, 0, 1e5, [](FonctionPiece& g, double x) { g.retraitFin = x; });
      if (m_res.tole.present) {
        const double r = f.rayon > 0 ? f.rayon : m_res.tole.rayon;
        aide(fl, QStringLiteral("Pli de %1 mm développé (K = %2). Un bord voisin déjà plié laisse son coin libre.")
                     .arg(QLocale().toString(longueurDeveloppee(f.angle, r, m_res.tole.epaisseur, m_res.tole.facteurK), 'f', 3), QLocale().toString(m_res.tole.facteurK, 'f', 3)));
      }
    } else if (f.type == TypeFonction::Depouille) {
      auto* fl = groupe(QStringLiteral("Dépouille"));
      auto* neutre = new QComboBox;
      neutre->addItems({QStringLiteral("Plan de dessus (XY)"), QStringLiteral("Plan de face (XZ)"), QStringLiteral("Plan de droite (YZ)"), QStringLiteral("Plan ou face choisi dans la vue")});
      neutre->setCurrentIndex(f.planNeutre == "XY" ? 0 : f.planNeutre == "XZ" ? 1 : f.planNeutre == "YZ" ? 2 : 3);
      connect(neutre, &QComboBox::activated, this, [this](int i) {
        if (i < 3) m_brouillon.planNeutre = i == 0 ? "XY" : i == 1 ? "XZ" : "YZ";
        else activerChamp(Champ::Neutre);
        apercu();
        majProprietes();
      });
      fl->addRow(QStringLiteral("Plan neutre"), neutre);
      if (neutre->currentIndex() == 3) choisir(fl, QStringLiteral("Plan choisi"), qs(f.planNeutre), Champ::Neutre);
      choisir(fl, QStringLiteral("Faces à incliner"), QStringLiteral("%1 choisie(s)").arg(f.references.size()), Champ::Faces);
      valeur(fl, QStringLiteral("Angle"), f.angle, true, 0.01, 89, [](FonctionPiece& g, double x) { g.angle = x; });
      caseACocher(fl, QStringLiteral("Inverser le sens de démoulage"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
      aide(fl, QStringLiteral("Le plan neutre ne bouge pas ; les faces choisies s’inclinent de l’angle donné, pour faciliter le démoulage."));
    } else {
      const bool faces = f.type == TypeFonction::Coque;
      auto* fl = groupe(QString::fromUtf8(nomTypeFonction(f.type)));
      if (f.type == TypeFonction::Conge) valeur(fl, QStringLiteral("Rayon"), f.rayon, false, 0.001, 1e5, [](FonctionPiece& g, double x) { g.rayon = x; });
      if (f.type == TypeFonction::Chanfrein) {
        auto* type = new QComboBox;
        type->addItem(icone(QStringLiteral("chanfrein")), QStringLiteral("Distances égales"), QStringLiteral("egal"));
        type->addItem(icone(QStringLiteral("chanfrein")), QStringLiteral("Deux distances"), QStringLiteral("distances"));
        type->addItem(icone(QStringLiteral("chanfrein")), QStringLiteral("Distance et angle"), QStringLiteral("angle"));
        type->setCurrentIndex(std::max(0, type->findData(qs(f.typeChanfrein))));
        connect(type, &QComboBox::activated, this, [this, type] { m_brouillon.typeChanfrein = type->currentData().toString().toStdString(); apercu(); majProprietes(); });
        fl->addRow(QStringLiteral("Type"), type);
        valeur(fl, f.typeChanfrein == "egal" ? QStringLiteral("Distance") : QStringLiteral("Distance 1"), f.distance, false, 0.001, 1e5, [](FonctionPiece& g, double x) { g.distance = x; });
        if (f.typeChanfrein == "distances") valeur(fl, QStringLiteral("Distance 2"), f.distance2, false, 0.001, 1e5, [](FonctionPiece& g, double x) { g.distance2 = x; });
        if (f.typeChanfrein == "angle") valeur(fl, QStringLiteral("Angle"), f.angleChanfrein, true, 1, 89, [](FonctionPiece& g, double x) { g.angleChanfrein = x; });
        if (f.typeChanfrein != "egal") {
          choisir(fl, QStringLiteral("Face de la distance 1"), f.faceChanfrein.empty() ? QStringLiteral("par défaut (fonction la plus ancienne)") : qs(f.faceChanfrein), Champ::FaceChanfrein);
          if (m_champ == Champ::FaceChanfrein) aide(fl, QStringLiteral("Clique la face sur laquelle se mesure la première distance ; l’aperçu montre le résultat."));
          caseACocher(fl, QStringLiteral("Inverser le côté de la première distance"), f.inverse, [](FonctionPiece& g, bool x) { g.inverse = x; });
        }
      }
      if (faces) valeur(fl, QStringLiteral("Épaisseur"), f.epaisseur, false, 0.001, 1e5, [](FonctionPiece& g, double x) { g.epaisseur = x; });
      if (faces) {
        // Faces d'épaisseur différente : un clic les ajoute avec l'épaisseur indiquée, chacune garde la sienne et se
        // règle ensuite à part (AP-18). La valeur proposée vit dans l'atelier, pas dans un champ d'une autre fonction.
        if (m_epaisseurParticuliere <= 0) m_epaisseurParticuliere = f.epaisseursFaces.empty() ? f.epaisseur * 2 : f.epaisseursFaces.back().second;
        valeur(fl, QStringLiteral("Épaisseur des faces ajoutées"), m_epaisseurParticuliere, false, 0.001, 1e5, [this](FonctionPiece&, double x) { m_epaisseurParticuliere = x; });
        choisir(fl, QStringLiteral("Faces particulières"), QStringLiteral("%1 face(s)").arg(f.epaisseursFaces.size()), Champ::FacesEpaisseur);
        for (std::size_t k = 0; k < f.epaisseursFaces.size(); ++k) {
          const std::string& nom = f.epaisseursFaces[k].first;
          valeur(fl, QStringLiteral("  face %1").arg(qs(nom.size() > 18 ? nom.substr(0, 16) + "…" : nom)), f.epaisseursFaces[k].second, false, 0.001, 1e5,
                 [k](FonctionPiece& g, double x) { if (k < g.epaisseursFaces.size()) g.epaisseursFaces[k].second = x; });
        }
      }
      choisir(fl, faces ? QStringLiteral("Faces ouvertes") : QStringLiteral("Arêtes"), QStringLiteral("%1 choisie(s)").arg(f.references.size()), faces ? Champ::Faces : Champ::Aretes);
      aide(fl, faces ? QStringLiteral("Clique dans la vue les faces à ouvrir ; un second clic retire une face.")
                     : QStringLiteral("Clique dans la vue les arêtes, ou une face pour toutes ses arêtes ; un second clic retire."));
      auto* vider = new QPushButton(QStringLiteral("Vider la sélection"));
      connect(vider, &QPushButton::clicked, this, [this] { m_brouillon.references.clear(); apercu(); majProprietes(); });
      fl->addRow(QString(), vider);
    }
  } else if (m_indice < 0) {
    auto* l = new QLabel(QStringLiteral("<h3>Concevoir une pièce</h3><p>Une esquisse cotée et contrainte, puis des fonctions qui l’extrudent, la font tourner, l’arrondissent ou l’évident. "
                                        "Chaque étape reste modifiable dans l’arbre.</p>"));
    l->setWordWrap(true);
    col->addWidget(l);
    auto* b = new QPushButton(QStringLiteral("Nouvelle pièce"));
    b->setObjectName(QStringLiteral("principal"));
    connect(b, &QPushButton::clicked, this, [this] { nouvellePiece(); });
    col->addWidget(b);
  } else {
    const FonctionPiece* f = m_piece.fonction(fonctionChoisie());
    auto* l = new QLabel;
    l->setWordWrap(true);
    if (!f) {
      l->setText(QStringLiteral("<h3>%1</h3><p><b>Créer</b> : clique un plan ou une face plane, puis « Nouvelle esquisse » (onglet Esquisse) ; ensuite Extrusion, Enlèvement ou Révolution (onglet Fonctions).</p>"
                                "<p><b>Retoucher</b> : Congé, Chanfrein, Coque ; tu cliques ensuite les arêtes ou les faces dans la vue, avec aperçu en direct.</p>"
                                "<p><b>Modifier</b> : double-clic sur une fonction de l’arbre, ou clic droit pour la renommer, la déplacer, la désactiver ou la supprimer.</p>")
                     .arg(qs(m_piece.nom).toHtmlEscaped()));
      col->addWidget(l);
      auto* nom = new QLineEdit(qs(m_piece.nom));
      connect(nom, &QLineEdit::editingFinished, this, [this, nom] { m_piece.nom = nom->text().trimmed().toStdString(); enregistrer(); majChoixPieces(); });
      form->addRow(QStringLiteral("Nom de la pièce"), nom);
      col->addLayout(form);
    } else {
      // Fonction choisie dans l'arbre : ses réglages principaux, modifiables sur place (la pièce se reconstruit
      // aussitôt), son état, et les actions utiles. Pas de détour par un bouton « Modifier ».
      const int id = f->id;
      const std::string err = m_res.erreur(f->id);
      page->setObjectName(QStringLiteral("panneauProprietes"));
      page->setStyleSheet(QString::fromLatin1(STYLE_PANNEAU));
      {
        auto* haut = new QHBoxLayout;
        auto* ic = new QLabel;
        ic->setPixmap(icone(iconeFonction(*f)).pixmap(26, 26));
        haut->addWidget(ic);
        haut->addWidget(new QLabel(QStringLiteral("<span style='font-size:15px; font-weight:700; color:#10142A'>%1</span><br><span style='color:#6B7393'>%2</span>")
                                       .arg(qs(f->nom).toHtmlEscaped(), QString::fromUtf8(nomTypeFonction(f->type)))), 1);
        col->addLayout(haut);
      }
      if (!err.empty()) {
        auto* e = new QLabel(QStringLiteral("⚠ %1").arg(qs(err)));
        e->setWordWrap(true);
        e->setStyleSheet(QStringLiteral("color: white; background: #C2402E; padding: 7px; border-radius: 7px;"));
        col->addWidget(e);
      }
      const Unites u = unites();
      auto* g = new QGroupBox(QStringLiteral("Réglages"));
      auto* fl = new QFormLayout(g);
      // Champ appliqué directement à la fonction de la pièce, aux mêmes bornes que dans le gestionnaire (AP-32)
      auto valeurDirecte = [this, fl, &u, id](const QString& libelle, double v, bool angle, double mini, double maxi, std::function<void(FonctionPiece&, double)> ecrire) {
        auto* c = new ChampValeur(angle, u);
        c->setBornes(mini, maxi);
        c->setValeur(v);
        connect(c, &ChampValeur::valeurChangee, this, [this, id, ecrire](double x) {
          FonctionPiece* g2 = m_piece.fonction(id);
          if (!g2) return;
          ecrire(*g2, x);
          enregistrer();
          reconstruire(false);
        });
        fl->addRow(libelle, c);
      };
      int champs = 0;
      switch (f->type) {
        case TypeFonction::Extrusion:
          if (f->fin == "borgne" || f->fin == "milieu") { valeurDirecte(QStringLiteral("Profondeur"), f->longueur, false, 0.001, 1e5, [](FonctionPiece& x, double v) { x.longueur = v; }); ++champs; }
          if (f->mince) { valeurDirecte(QStringLiteral("Épaisseur"), f->epaisseurMince, false, 0.001, 1e5, [](FonctionPiece& x, double v) { x.epaisseurMince = v; }); ++champs; }
          break;
        case TypeFonction::Revolution: valeurDirecte(QStringLiteral("Angle"), f->angle, true, 0.01, 360, [](FonctionPiece& x, double v) { x.angle = v; }); ++champs; break;
        case TypeFonction::Conge: valeurDirecte(QStringLiteral("Rayon"), f->rayon, false, 0.001, 1e5, [](FonctionPiece& x, double v) { x.rayon = v; }); ++champs; break;
        case TypeFonction::Chanfrein:
          valeurDirecte(QStringLiteral("Distance"), f->distance, false, 0.001, 1e5, [](FonctionPiece& x, double v) { x.distance = v; }); ++champs;
          if (f->typeChanfrein == "distances") { valeurDirecte(QStringLiteral("Seconde distance"), f->distance2, false, 0.001, 1e5, [](FonctionPiece& x, double v) { x.distance2 = v; }); ++champs; }
          if (f->typeChanfrein == "angle") { valeurDirecte(QStringLiteral("Angle"), f->angleChanfrein, true, 1, 89, [](FonctionPiece& x, double v) { x.angleChanfrein = v; }); ++champs; }
          break;
        case TypeFonction::Coque: valeurDirecte(QStringLiteral("Épaisseur"), f->epaisseur, false, 0.001, 1e5, [](FonctionPiece& x, double v) { x.epaisseur = v; }); ++champs; break;
        case TypeFonction::Percage:
          if (f->typePercage == "simple") { valeurDirecte(QStringLiteral("Diamètre"), f->diametre, false, 0.01, 1e5, [](FonctionPiece& x, double v) { x.diametre = v; }); ++champs; }
          if (!f->traversant) { valeurDirecte(QStringLiteral("Profondeur"), f->profondeur, false, 0.01, 1e5, [](FonctionPiece& x, double v) { x.profondeur = v; }); ++champs; }
          break;
        case TypeFonction::Repetition:
          if (f->typeRepetition == "lineaire") { valeurDirecte(QStringLiteral("Pas"), f->pas1, false, -1e5, 1e5, [](FonctionPiece& x, double v) { x.pas1 = v; }); ++champs; }
          if (f->typeRepetition == "circulaire") { valeurDirecte(QStringLiteral("Angle total"), f->angle, true, 1, 360, [](FonctionPiece& x, double v) { x.angle = v; }); ++champs; }
          if (f->typeRepetition != "esquisse") {
            auto* n = new QSpinBox;
            n->setRange(2, 1000);
            n->setValue(f->nombre1);
            connect(n, &QSpinBox::valueChanged, this, [this, id](int v) { if (FonctionPiece* g2 = m_piece.fonction(id)) { g2->nombre1 = v; enregistrer(); reconstruire(false); } });
            fl->addRow(QStringLiteral("Nombre"), n);
            ++champs;
          }
          break;
        case TypeFonction::Depouille: valeurDirecte(QStringLiteral("Angle"), f->angle, true, 0.01, 89, [](FonctionPiece& x, double v) { x.angle = v; }); ++champs; break;
        case TypeFonction::Tole:
          valeurDirecte(QStringLiteral("Épaisseur"), f->epaisseur, false, 0.01, 100, [](FonctionPiece& x, double v) { x.epaisseur = v; }); ++champs;
          valeurDirecte(QStringLiteral("Rayon de pliage"), f->rayon, false, 0.01, 1000, [](FonctionPiece& x, double v) { x.rayon = v; }); ++champs;
          break;
        case TypeFonction::Nervure:
          valeurDirecte(QStringLiteral("Épaisseur"), f->epaisseur, false, 0.01, 1e4, [](FonctionPiece& x, double v) { x.epaisseur = v; }); ++champs;
          break;
        case TypeFonction::Filetage:
          valeurDirecte(QStringLiteral("Longueur (0 : toute la face)"), f->longueurFiletee, false, 0, 1e5, [](FonctionPiece& x, double v) { x.longueurFiletee = v; }); ++champs;
          break;
        case TypeFonction::BordTombe:
          valeurDirecte(QStringLiteral("Longueur"), f->longueur, false, 0.01, 1e5, [](FonctionPiece& x, double v) { x.longueur = v; }); ++champs;
          valeurDirecte(QStringLiteral("Angle"), f->angle, true, 1, 180, [](FonctionPiece& x, double v) { x.angle = v; }); ++champs;
          break;
        case TypeFonction::Balayage:
          if (f->tube) {
            valeurDirecte(QStringLiteral("Diamètre"), f->diametre, false, 0.01, 1e5, [](FonctionPiece& x, double v) { x.diametre = v; }); ++champs;
            valeurDirecte(QStringLiteral("Épaisseur (0 : plein)"), f->epaisseur, false, 0, 1e5, [](FonctionPiece& x, double v) { x.epaisseur = v; }); ++champs;
          }
          break;
        case TypeFonction::Plan:
          if (f->mode == "decale") { valeurDirecte(QStringLiteral("Distance"), f->distance, false, -1e5, 1e5, [](FonctionPiece& x, double v) { x.distance = v; }); ++champs; }
          if (f->mode == "angle") { valeurDirecte(QStringLiteral("Angle"), f->angle, true, -360, 360, [](FonctionPiece& x, double v) { x.angle = v; }); ++champs; }
          break;
        default: break;
      }
      if (f->type == TypeFonction::Esquisse) {
        // Esquisse : son état en une ligne, puis ce qu'on en fait
        Esquisse copie = f->esquisse;
        const EtatEsquisse etat = resoudre(copie);
        const int ferme = m_res.etatEsquisse(id);
        auto* l2 = new QLabel(QStringLiteral("%1<br>%2")
                                  .arg(ferme == 0 ? QStringLiteral("<span style='color:#2E8B57'>✓ Profils fermés</span>") : ferme == 1 ? QStringLiteral("<span style='color:#C77A1E'>⚠ Profil ouvert</span>") : QStringLiteral("<span style='color:#C2402E'>✖ Contour non valide</span>"),
                                       etat.degresLiberte == 0 ? QStringLiteral("<span style='color:#2E8B57'>✓ Entièrement contrainte</span>") : QStringLiteral("Partiellement contrainte — %1 degré(s) de liberté").arg(etat.degresLiberte)));
        fl->addRow(l2);
        ++champs;
      }
      if (champs) col->addWidget(g);
      else delete g;
      auto* actions = new QGroupBox(QStringLiteral("Actions"));
      auto* al = new QGridLayout(actions);
      int k = 0;
      auto action = [&](const QString& ic, const QString& t, std::function<void()> fn) {
        auto* b = new QPushButton(icone(ic), t);
        b->setObjectName(QStringLiteral("champ"));
        connect(b, &QPushButton::clicked, this, [fn] { fn(); });
        al->addWidget(b, k / 2, k % 2);
        ++k;
      };
      if (f->type == TypeFonction::Esquisse3D) {
        action(QStringLiteral("balayage"), QStringLiteral("Tube le long"), [this] { nouvelleFonction(TypeFonction::Balayage, false, "tube"); });
        action(QStringLiteral("balayage"), QStringLiteral("Balayage d’un profil"), [this, id] { nouvelleFonction(TypeFonction::Balayage, false, ""); m_brouillon.cheminId = id; apercu(); majProprietes(); });
        action(QStringLiteral("esquisse3d"), QStringLiteral("Modifier les points"), [this, id] { editerFonction(id); });
      } else if (f->type == TypeFonction::Esquisse) {
        action(QStringLiteral("extrusion"), QStringLiteral("Extrusion"), [this] { nouvelleFonction(TypeFonction::Extrusion, false, "borgne"); });
        action(QStringLiteral("enlevement"), QStringLiteral("Enlèvement"), [this] { nouvelleFonction(TypeFonction::Extrusion, true, "borgne"); });
        action(QStringLiteral("revolution"), QStringLiteral("Révolution"), [this] { nouvelleFonction(TypeFonction::Revolution, false, ""); });
        action(QStringLiteral("esquisse"), QStringLiteral("Modifier l’esquisse"), [this, id] { editerFonction(id); });
      } else {
        action(QStringLiteral("proprietes"), QStringLiteral("Tous les réglages"), [this, id] { editerFonction(id); });
      }
      action(QStringLiteral("repetition-lineaire"), QStringLiteral("Dupliquer"), [this, id] {
        const FonctionPiece* src = m_piece.fonction(id);
        if (!src) return;
        FonctionPiece copie = *src;
        copie.nom = src->nom + " (copie)";
        const int nouveau = m_piece.ajouter(copie);
        enregistrer();
        reconstruire(false);
        choisirFonction(nouveau);
      });
      action(QStringLiteral("supprimer"), QStringLiteral("Supprimer"), [this, id] { supprimerFonction(id); });
      col->addWidget(actions);
    }
  }
  col->addStretch(1);
  remplacerPage(m_props, page);
}

// Suppression d'une fonction, toujours confirmée (AP-29) : la question nomme les fonctions qui en dépendent (elles
// passeront en échec) et propose d'emporter l'esquisse rangée sous elle, si rien d'autre ne l'utilise.
void AtelierPiece::supprimerFonction(int id) {
  const FonctionPiece* f = m_piece.fonction(id);
  if (!f) return;
  QStringList dependants;
  for (const auto& g : m_piece.fonctions) {
    const auto parents = parentsDe(g);
    if (g.id != id && std::find(parents.begin(), parents.end(), id) != parents.end()) dependants << qs(g.nom);
  }
  // Esquisse absorbée : celle de la fonction, qu'aucune autre fonction n'utilise
  int esquisse = 0;
  if (const FonctionPiece* e = m_piece.fonction(f->esquisseId); e && e->type == TypeFonction::Esquisse) {
    bool autre = false;
    for (const auto& g : m_piece.fonctions) {
      if (g.id == id) continue;
      const auto parents = parentsDe(g);
      autre = autre || std::find(parents.begin(), parents.end(), e->id) != parents.end();
    }
    if (!autre) esquisse = e->id;
  }
  QMessageBox q(QMessageBox::Question, QStringLiteral("Supprimer"), QStringLiteral("Supprimer « %1 » ?").arg(qs(f->nom)), QMessageBox::Yes | QMessageBox::No, this);
  if (!dependants.isEmpty())
    q.setInformativeText(QStringLiteral("Ces fonctions en dépendent et passeront en échec : %1.").arg(dependants.join(QStringLiteral(", "))));
  QCheckBox* avecEsquisse = nullptr;
  if (esquisse) {
    avecEsquisse = new QCheckBox(QStringLiteral("Supprimer aussi son esquisse « %1 »").arg(qs(m_piece.fonction(esquisse)->nom)));
    avecEsquisse->setChecked(true);
    q.setCheckBox(avecEsquisse);
  }
  if (q.exec() != QMessageBox::Yes) return;
  const bool aussi = avecEsquisse && avecEsquisse->isChecked();
  auto& fs = m_piece.fonctions;
  fs.erase(std::remove_if(fs.begin(), fs.end(), [&](const FonctionPiece& g) { return g.id == id || (aussi && g.id == esquisse); }), fs.end());
  enregistrer();
  reconstruire(false);
}

void AtelierPiece::menuArbre(const QPoint& p) {
  QTreeWidgetItem* it = m_arbre->itemAt(p);
  if (!it || m_gestion) return;
  // Mise en plan : ouvrir, renommer, supprimer (AP-30)
  if (const QString cle = it->data(0, Navigateur::ROLE_CLE).toString(); cle.startsWith(QLatin1String("plan:"))) {
    const int k = cle.section(QLatin1Char(':'), 2, 2).toInt();
    QMenu menu(this);
    menu.addAction(icone(QStringLiteral("plan")), QStringLiteral("Ouvrir"), this, [this, k] { ouvrirMiseEnPlan(k); });
    menu.addAction(QStringLiteral("Renommer…"), this, [this, k] { renommerMiseEnPlan(k); });
    menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer…"), this, [this, k] { supprimerMiseEnPlan(k); });
    menu.exec(m_arbre->viewport()->mapToGlobal(p));
    return;
  }
  const int id = it->data(0, Qt::UserRole).toInt();
  FonctionPiece* f = m_piece.fonction(id);
  if (!f) return;
  QMenu menu(this);
  menu.addAction(f->type == TypeFonction::Esquisse ? QStringLiteral("Modifier l’esquisse") : QStringLiteral("Modifier"), this, [this, id] { editerFonction(id); });
  if (f->esquisseId && m_piece.fonction(f->esquisseId)) {
    const int e = f->esquisseId;
    menu.addAction(QStringLiteral("Modifier son esquisse"), this, [this, e] { editerEsquisse(e); });
  }
  if (f->type == TypeFonction::Esquisse) menu.addAction(icone(QStringLiteral("afficher")), QStringLiteral("Afficher / masquer (Espace)"), this, [this, id] { basculerVisibiliteEsquisse(id); });
  menu.addAction(icone(QStringLiteral("repetition-lineaire")), QStringLiteral("Dupliquer"), this, [this, id] {
    const FonctionPiece* src = m_piece.fonction(id);
    if (!src) return;
    FonctionPiece copie = *src;
    copie.nom = src->nom + " (copie)";
    const int nouveau = m_piece.ajouter(copie);
    enregistrer();
    reconstruire(false);
    choisirFonction(nouveau);
  });
  menu.addAction(QStringLiteral("Renommer…"), this, [this, id] {
    FonctionPiece* g = m_piece.fonction(id);
    bool ok = false;
    const QString n = QInputDialog::getText(this, QStringLiteral("Renommer"), QStringLiteral("Nom"), QLineEdit::Normal, qs(g->nom), &ok);
    if (ok && !n.trimmed().isEmpty()) { g->nom = n.trimmed().toStdString(); enregistrer(); majArbre(); }
  });
  menu.addAction(f->desactivee ? QStringLiteral("Réactiver") : QStringLiteral("Désactiver"), this, [this, id] {
    FonctionPiece* g = m_piece.fonction(id);
    g->desactivee = !g->desactivee;
    enregistrer();
    reconstruire();
  });
  menu.addSeparator();
  menu.addAction(QStringLiteral("Monter"), this, [this, id] { deplacerFonction(id, -1); });
  menu.addAction(QStringLiteral("Descendre"), this, [this, id] { deplacerFonction(id, 1); });
  menu.addSeparator();
  menu.addAction(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer…"), this, [this, id] { supprimerFonction(id); });
  menu.exec(m_arbre->viewport()->mapToGlobal(p));
}

void AtelierPiece::deplacerFonction(int id, int sens) {
  // La fonction se déplace avec son esquisse, comme dans l'arbre, et jamais avant une fonction dont elle dépend
  // (esquisse, plan, faces…) : FO-04, AP-06
  std::string raison;
  if (!deplacerBloc(m_piece, id, sens, &raison)) {
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(qs(raison).toHtmlEscaped()));
    return;
  }
  enregistrer();
  reconstruire();
  choisirFonction(id);
}

void AtelierPiece::nouvellePiece() {
  if (!terminerEdition()) return;
  // Identifiant et nom jamais repris d'une pièce existante, même après une suppression (AP-02)
  std::vector<std::string> ids, noms;
  for (const auto& j : m_doc->projet().pieces) {
    if (j.contains("id") && j["id"].is_string()) ids.push_back(j["id"].get<std::string>());
    if (j.contains("nom") && j["nom"].is_string()) noms.push_back(j["nom"].get<std::string>());
  }
  int n = static_cast<int>(m_doc->projet().pieces.size()) + 1;
  while (std::find(noms.begin(), noms.end(), "Pièce " + std::to_string(n)) != noms.end()) ++n;
  Piece p;
  p.id = nouvelId("piece", ids);
  p.nom = "Pièce " + std::to_string(n);
  const Json j = pieceVersJson(p);
  m_doc->modifier([&](Projet& pr) { pr.pieces.push_back(j); });
  m_indice = static_cast<int>(m_doc->projet().pieces.size()) - 1;
  charger();
  m_onglets->setCurrentIndex(0);
  m_etat->setText(QStringLiteral("<span style='color:#2F6BF5'><b>Pièce créée : clique un plan (dans la vue ou dans l’arbre), puis « Nouvelle esquisse ».</b></span>"));
}

Unites AtelierPiece::unites() const {
  Unites u;
  u.longueur = uniteLongueurDepuis(m_piece.uniteLongueur);
  u.angle = uniteAngleDepuis(m_piece.uniteAngle);
  return u;
}

// Comme dans les grands logiciels : on choisit le support (plan de base ou face plane), puis Esquisse ; ou
// Esquisse d'abord, puis un clic sur le support.
void AtelierPiece::commandeEsquisse() {
  if (!peutLancer()) return;
  if (m_indice < 0) { nouvellePiece(); return; }
  if (creerEsquisseDepuisSelection()) return;
  m_attenteEsquisse = true;
  majPlans();
  m_vue->setMode(VuePiece::Mode::Faces);
  majFiltre(1);
  m_etat->setText(QStringLiteral("<span style='color:#2F6BF5'><b>Nouvelle esquisse : clique un plan (dans la vue ou dans l’arbre) ou une face plane de la pièce.</b></span>"));
}

bool AtelierPiece::creerEsquisseDepuisSelection() {
  // Plan de base choisi dans l'arbre
  const int choisie = fonctionChoisie();
  if (choisie <= -1 && choisie >= -3) { m_attenteEsquisse = false; ajouterEsquisse(choisie == -1 ? PlanBase::XY : choisie == -2 ? PlanBase::XZ : PlanBase::YZ, false); return true; }
  // Plan de base cliqué dans la vue
  std::string refPlan = m_vue->referenceSelectionnee();
  if (const FonctionPiece* pc = m_piece.fonction(choisie); refPlan.empty() && pc && pc->type == TypeFonction::Plan) refPlan = "F:" + std::to_string(pc->id);
  if (refPlan.rfind("F:", 0) == 0) {
    if (const FonctionPiece* g = m_piece.fonction(std::atoi(refPlan.c_str() + 2)); g && g->type == TypeFonction::Plan) {
      FonctionPiece f;
      f.type = TypeFonction::Esquisse;
      f.face = refPlan;
      const int id = m_piece.ajouter(f);
      m_vue->viderSelection();
      enregistrer();
      reconstruire();
      m_prochaineNouvelle = true;
      editerEsquisse(id);
      return true;
    }
  }
  const std::string plan = m_vue->planSelectionne();
  if (!plan.empty()) { m_attenteEsquisse = false; ajouterEsquisse(plan == "XY" ? PlanBase::XY : plan == "XZ" ? PlanBase::XZ : PlanBase::YZ, false); return true; }
  // Face plane de la pièce
  for (const auto& s : m_vue->selection()) {
    if (s.ShapeType() != TopAbs_FACE) continue;
    gp_Ax3 ax;
    if (!repereFace(TopoDS::Face(s), ax, m_res.forme)) { m_etat->setText(QStringLiteral("<span style='color:#C2402E'><b>Cette face n’est pas plane : choisis une face plane ou un plan.</b></span>")); return false; }
    const std::string nom = m_res.nomFace(TopoDS::Face(s));
    if (nom.empty()) return false;
    m_attenteEsquisse = false;
    FonctionPiece f;
    f.type = TypeFonction::Esquisse;
    f.face = nom;
    // Axe X de l'esquisse mémorisé à la pose : il suit la face ensuite sans jamais basculer (FO-08)
    f.axeX = {ax.XDirection().X(), ax.XDirection().Y(), ax.XDirection().Z()};
    const int id = m_piece.ajouter(f);
    m_vue->viderSelection();
    m_vue->setMode(VuePiece::Mode::Tout);
    majFiltre(0);
    enregistrer();
    reconstruire();
    m_prochaineNouvelle = true;
    editerEsquisse(id);
    return true;
  }
  return false;
}

void AtelierPiece::ajouterEsquisse(PlanBase plan, bool surFace) {
  if (m_indice < 0) { nouvellePiece(); return; }
  FonctionPiece f;
  f.type = TypeFonction::Esquisse;
  f.plan = plan;
  if (surFace) {
    const auto noms = referencesSelection(true);
    gp_Ax3 ax;
    const auto fs = noms.empty() ? std::vector<TopoDS_Face>{} : m_affiche.facesNommees(noms.front());
    if (fs.empty() || !repereFace(fs.front(), ax, m_res.forme)) {
      m_vue->setMode(VuePiece::Mode::Faces);
      majFiltre(2);
      m_etat->setText(QStringLiteral("<span style='color:#2F6BF5'><b>Clique une face plane de la pièce, puis à nouveau Esquisse → Sur la face sélectionnée.</b></span>"));
      return;
    }
    f.face = noms.front();
    f.axeX = {ax.XDirection().X(), ax.XDirection().Y(), ax.XDirection().Z()};
  }
  commencerSessionEsquisse();  // la création de l'esquisse et ses tracés feront un seul pas (AP-04)
  const int id = m_piece.ajouter(f);
  m_vue->viderSelection();
  enregistrer();
  reconstruire();
  m_prochaineNouvelle = true;
  editerEsquisse(id);
}

// Tôlerie : une esquisse neuve sur le plan de dessus ; à sa validation, la tôle de base s'ouvre sur ce contour.
void AtelierPiece::esquisserTole() {
  if (!peutLancer()) return;
  if (m_indice < 0) nouvellePiece();
  if (m_indice < 0) return;
  ajouterEsquisse(PlanBase::XY, false);
  if (m_esquisseEditee) {
    m_toleApresEsquisse = m_esquisseEditee;
    m_etat->setText(QStringLiteral("<span style='color:#2F6BF5'><b>Dessine le contour de la tôle</b> (fermé : rectangle, polygone…), puis valide : la tôle de base s’ouvre.</span>"));
  }
}

void AtelierPiece::montrerDeplie(bool oui) {
  if (oui && !m_res.tole.present) {
    m_etat->setText(QStringLiteral("<span style='color:#C77A1E'>Pas de tôle dans cette pièce : commence par « Tôle de base ».</span>"));
    if (m_actionDeplie) { const QSignalBlocker b(m_actionDeplie); m_actionDeplie->setChecked(false); }
    return;
  }
  m_vueDepliee = oui;
  if (m_actionDeplie && m_actionDeplie->isChecked() != oui) { const QSignalBlocker b(m_actionDeplie); m_actionDeplie->setChecked(oui); }
  reconstruire(true);
  if (oui) {
    const DeplieTole& d = m_res.tole;
    QString t = QStringLiteral("<span style='color:#2F7D53'><b>Déplié</b></span> : %1 × %2 mm, %3 pli(s), épaisseur %4 mm, K %5")
                    .arg(QLocale().toString(d.xmax - d.xmin, 'f', 2), QLocale().toString(d.ymax - d.ymin, 'f', 2))
                    .arg(d.plis.size())
                    .arg(QLocale().toString(d.epaisseur, 'f', 2), QLocale().toString(d.facteurK, 'f', 3));
    for (const auto& a : d.alertes) t += QStringLiteral("<br><span style='color:#C77A1E'>%1</span>").arg(qs(a).toHtmlEscaped());
    m_etat->setText(t);
  }
}

bool AtelierPiece::exporterDeplie(const QString& format, const QString& cheminDonne) {
  if (!m_res.tole.present) {
    m_etat->setText(QStringLiteral("<span style='color:#C77A1E'>Pas de tôle dans cette pièce : rien à déplier.</span>"));
    return false;
  }
  QString chemin = cheminDonne;
  const bool svg = format == QLatin1String("svg");
  if (chemin.isEmpty())
    chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter le déplié"), qs(m_piece.nom) + QStringLiteral(" - déplié.") + (svg ? QStringLiteral("svg") : QStringLiteral("dxf")),
                                          svg ? QStringLiteral("SVG (*.svg)") : QStringLiteral("DXF (*.dxf)"));
  if (chemin.isEmpty()) return false;
  const std::string texte = svg ? svgDeplie(m_res.tole, m_piece.nom) : dxfDeplie(m_res.tole, m_piece.nom);
  QFile f(chemin);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(texte.data(), static_cast<qint64>(texte.size())) != static_cast<qint64>(texte.size())) {
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'>Écriture impossible : %1</span>").arg(chemin.toHtmlEscaped()));
    return false;
  }
  QString t = QStringLiteral("<span style='color:#2F7D53'><b>Déplié exporté</b></span> : %1 (%2 pli(s)).").arg(QFileInfo(chemin).fileName().toHtmlEscaped()).arg(m_res.tole.plis.size());
  if (!m_res.tole.alertes.empty()) t += QStringLiteral("<br><span style='color:#C77A1E'>%1</span>").arg(qs(m_res.tole.alertes.front()).toHtmlEscaped());
  m_etat->setText(t);
  return true;
}

void AtelierPiece::nouvelleFonction(TypeFonction type, bool enlever, const std::string& variante) {
  if (!peutLancer()) return;  // une esquisse ouverte est d'abord terminée (AP-01)
  if (m_indice < 0) { nouvellePiece(); return; }
  if (m_gestion) fermerGestionnaire(false);
  m_attenteEsquisse = false;  // une autre commande abandonne l'attente d'un support d'esquisse (AP-11)
  if (m_mesure) basculerMesure();  // et quitte la mesure (AP-36)
  FonctionPiece f;
  f.type = type;
  f.enlever = enlever;
  if (type == TypeFonction::Extrusion || type == TypeFonction::Revolution) {
    int esquisse = m_vue->esquisseSelectionnee();
    if (!esquisse)
      if (const FonctionPiece* c = m_piece.fonction(fonctionChoisie()); c && c->type == TypeFonction::Esquisse) esquisse = c->id;
    if (!esquisse) {
      // Dernière esquisse pas encore utilisée.
      for (auto it = m_piece.fonctions.rbegin(); it != m_piece.fonctions.rend() && !esquisse; ++it)
        if (it->type == TypeFonction::Esquisse &&
            std::none_of(m_piece.fonctions.begin(), m_piece.fonctions.end(), [&](const FonctionPiece& g) { return g.esquisseId == it->id; }))
          esquisse = it->id;
    }
    f.esquisseId = esquisse;
    const FonctionPiece* fe = m_piece.fonction(esquisse);
    if (type == TypeFonction::Revolution && fe)
      for (const auto& el : fe->esquisse.elements)
        if (el.type == TypeElement::Ligne && (el.construction || !f.axe)) { f.axe = el.id; if (el.construction) break; }
  } else if (type == TypeFonction::Tole) {
    if (std::any_of(m_piece.fonctions.begin(), m_piece.fonctions.end(), [](const FonctionPiece& g) { return g.type == TypeFonction::Tole && !g.desactivee; })) {
      m_etat->setText(QStringLiteral("<span style='color:#C77A1E'>Cette pièce a déjà sa tôle de base : ajoute des bords tombés (clique ensuite l’arête).</span>"));
      return;
    }
    int esquisse = m_vue->esquisseSelectionnee();
    if (!esquisse)
      if (const FonctionPiece* c = m_piece.fonction(fonctionChoisie()); c && c->type == TypeFonction::Esquisse) esquisse = c->id;
    if (!esquisse)
      for (auto it = m_piece.fonctions.rbegin(); it != m_piece.fonctions.rend() && !esquisse; ++it)
        if (it->type == TypeFonction::Esquisse && std::none_of(m_piece.fonctions.begin(), m_piece.fonctions.end(), [&](const FonctionPiece& g) { return g.esquisseId == it->id; }))
          esquisse = it->id;
    if (!esquisse) {
      // Pas encore de contour : on l'esquisse d'abord, la tôle s'ouvre à la validation
      esquisserTole();
      return;
    }
    f.esquisseId = esquisse;
    f.epaisseur = 1.5;
    f.rayon = 1.5;
    f.facteurK = 0;
  } else if (type == TypeFonction::Nervure) {
    if (m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("Il faut d’abord un solide : la nervure le raidit.")); return; }
    int esquisse = m_vue->esquisseSelectionnee();
    if (!esquisse)
      if (const FonctionPiece* c = m_piece.fonction(fonctionChoisie()); c && c->type == TypeFonction::Esquisse) esquisse = c->id;
    if (!esquisse)
      for (auto it = m_piece.fonctions.rbegin(); it != m_piece.fonctions.rend() && !esquisse; ++it)
        if (it->type == TypeFonction::Esquisse && std::none_of(m_piece.fonctions.begin(), m_piece.fonctions.end(), [&](const FonctionPiece& g) { return g.esquisseId == it->id; }))
          esquisse = it->id;
    f.esquisseId = esquisse;
    f.epaisseur = 3;
    f.mode = variante.empty() ? "parallele" : variante;
    f.sensMince = "milieu";
  } else if (type == TypeFonction::Filetage) {
    if (m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("Il faut d’abord un solide : le filetage se pose sur un trou ou un arbre.")); return; }
    for (const auto& sel : m_vue->selection())
      if (sel.ShapeType() == TopAbs_FACE) {
        const std::string n = nommerDansAvant(sel, true);
        if (!n.empty()) { f.references = {n}; break; }
      }
    f.norme = "ISO";
    f.taille.clear();
    f.longueurFiletee = 0;
  } else if (type == TypeFonction::BordTombe) {
    if (!m_res.tole.present) {
      m_etat->setText(QStringLiteral("<span style='color:#C77A1E'>Commence par une tôle de base (onglet Tôlerie) : le bord tombé se plie sur une de ses arêtes.</span>"));
      return;
    }
    for (const auto& sel : m_vue->selection())
      if (sel.ShapeType() == TopAbs_EDGE || sel.ShapeType() == TopAbs_FACE) {
        const std::string n = nommerDansAvant(sel, sel.ShapeType() == TopAbs_FACE);
        if (!n.empty()) { f.references = {n}; break; }
      }
    f.longueur = 20;
    f.angle = 90;
    f.rayon = 0;
  } else if (type == TypeFonction::Plan || type == TypeFonction::Axe || type == TypeFonction::Point || type == TypeFonction::Repere) {
    // Géométrie de référence : la variante donne le mode ; ce qui est sélectionné devient ses supports.
    f.mode = type == TypeFonction::Repere ? std::string() : variante.empty() ? (type == TypeFonction::Plan ? "decale" : type == TypeFonction::Axe ? "arete" : "coordonnees") : variante;
    std::string plan = m_vue->planSelectionne();
    if (plan.empty() && m_vue->selection().empty() && m_vue->referenceSelectionnee().empty()) plan = supportDepuisArbre();  // AP-15
    if (!plan.empty()) f.supports.push_back(plan);
    const std::string refSel = m_vue->referenceSelectionnee();
    if (!refSel.empty()) f.supports.push_back(refSel);
    for (const auto& s : m_vue->selection()) { const std::string n = nomReference(s); if (!n.empty()) f.supports.push_back(n); }
    if (type == TypeFonction::Plan && f.supports.empty() && f.mode != "troisPoints") f.supports.push_back("XY");
    f.distance = 10;
    f.angle = 30;
  } else if (type == TypeFonction::Percage) {
    int esquisse = m_vue->esquisseSelectionnee();
    if (!esquisse) if (const FonctionPiece* c = m_piece.fonction(fonctionChoisie()); c && c->type == TypeFonction::Esquisse) esquisse = c->id;
    if (!esquisse)
      for (auto it = m_piece.fonctions.rbegin(); it != m_piece.fonctions.rend() && !esquisse; ++it)
        if (it->type == TypeFonction::Esquisse && std::none_of(m_piece.fonctions.begin(), m_piece.fonctions.end(), [&](const FonctionPiece& g) { return g.esquisseId == it->id; })) esquisse = it->id;
    f.esquisseId = esquisse;
    f.typePercage = variante.empty() ? "simple" : variante;
    f.traversant = true;
  } else if (type == TypeFonction::Repetition || type == TypeFonction::Symetrie) {
    if (m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("Il faut d’abord un solide.")); return; }
    // Fonctions sélectionnées avant la commande (arbre), balayages et lissages compris (AP-08)
    for (QTreeWidgetItem* it : m_arbre->selectedItems())
      if (const FonctionPiece* c = m_piece.fonction(it->data(0, Qt::UserRole).toInt()); c && sourceRepetable(*c) && std::find(f.sources.begin(), f.sources.end(), c->id) == f.sources.end())
        f.sources.push_back(c->id);
    if (f.sources.empty())
      if (const FonctionPiece* c = m_piece.fonction(fonctionChoisie()); c && sourceRepetable(*c)) f.sources = {c->id};
    f.typeRepetition = variante.empty() ? "lineaire" : variante;
    f.direction1 = f.typeRepetition == "circulaire" ? "Z" : "X";
    f.nombre1 = f.typeRepetition == "circulaire" ? 6 : 3;
    f.angle = 360;
    if (type == TypeFonction::Symetrie) {
      // Plan cliqué dans la vue, sinon plan ou référence choisi dans l'arbre (AP-15), sinon YZ
      std::string plan = m_vue->planSelectionne();
      if (plan.empty()) plan = supportDepuisArbre();
      f.supports = {plan.empty() ? std::string("YZ") : plan};
    }
  } else if (type == TypeFonction::Esquisse3D) {
    for (const auto& s : m_vue->selection())
      if (s.ShapeType() == TopAbs_VERTEX) { const gp_Pnt q = BRep_Tool::Pnt(TopoDS::Vertex(s)); f.points3d.push_back({q.X(), q.Y(), q.Z()}); }
    // Sans sommet sélectionné, on part de l'origine ; les clics ajoutent la suite
  } else if (type == TypeFonction::Balayage && variante == "tube") {
    // Tube le long d'un chemin : l'esquisse choisie (3D ou 2D), sinon la dernière esquisse 3D
    const int choisie = m_vue->esquisseSelectionnee() ? m_vue->esquisseSelectionnee() : fonctionChoisie();
    if (const FonctionPiece* c = m_piece.fonction(choisie); c && (c->type == TypeFonction::Esquisse3D || c->type == TypeFonction::Esquisse)) f.cheminId = c->id;
    if (!f.cheminId)
      for (auto it = m_piece.fonctions.rbegin(); it != m_piece.fonctions.rend() && !f.cheminId; ++it)
        if (it->type == TypeFonction::Esquisse3D) f.cheminId = it->id;
    f.tube = true;
    // Diamètre de départ à l'échelle du chemin : un quart du plus court segment d'une esquisse 3D (au plus 10 mm),
    // pour que le tube passe les coudes sans se recouper
    f.diametre = 10;
    if (const FonctionPiece* c = m_piece.fonction(f.cheminId); c && c->type == TypeFonction::Esquisse3D && c->points3d.size() >= 2) {
      double plusCourt = 1e300;
      for (std::size_t i = 1; i < c->points3d.size(); ++i) {
        const auto& a = c->points3d[i - 1];
        const auto& b = c->points3d[i];
        const double l = std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
        if (l > 1e-6) plusCourt = std::min(plusCourt, l);
      }
      if (plusCourt < 40) f.diametre = std::max(0.1, std::round(plusCourt / 4 * 10) / 10);
    }
    f.epaisseur = 0;
    f.enlever = enlever;
  } else if (type == TypeFonction::Balayage || type == TypeFonction::Lissage) {
    std::vector<int> libres;
    for (const auto& g : m_piece.fonctions)
      if (g.type == TypeFonction::Esquisse && std::none_of(m_piece.fonctions.begin(), m_piece.fonctions.end(), [&](const FonctionPiece& h2) { return h2.esquisseId == g.id || h2.cheminId == g.id || std::count(h2.sources.begin(), h2.sources.end(), g.id); }))
        libres.push_back(g.id);
    const int choisie = m_vue->esquisseSelectionnee() ? m_vue->esquisseSelectionnee() : fonctionChoisie();
    if (type == TypeFonction::Balayage) {
      f.esquisseId = m_piece.fonction(choisie) && m_piece.fonction(choisie)->type == TypeFonction::Esquisse ? choisie : (libres.size() >= 2 ? libres[libres.size() - 2] : 0);
      for (auto it = libres.rbegin(); it != libres.rend(); ++it) if (*it != f.esquisseId) { f.cheminId = *it; break; }
    } else {
      f.sources = libres;
    }
    f.enlever = enlever;
  } else if (type == TypeFonction::Depouille) {
    if (m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("Il faut d’abord un solide.")); return; }
    f.references = referencesSelection(true);
    f.planNeutre = "XY";
    f.angle = 3;
  } else {
    if (m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("Il faut d’abord un solide (extrusion ou révolution).")); return; }
    f.references = referencesSelection(type == TypeFonction::Coque);
  }
  // Variante choisie dans le menu du bouton : conditions de départ du gestionnaire.
  if (type == TypeFonction::Extrusion && !variante.empty()) {
    f.traversant = false;
    f.symetrique = false;
    if (variante == "milieu") f.fin = "milieu";
    else if (variante == "traversant") f.fin = "traversant";
    else if (variante == "face") f.fin = "face";
    else if (variante == "mince") { f.mince = true; f.fin = "borgne"; }
    else {
      f.fin = "borgne";
      if (enlever) { f.longueur = 5; f.inverse = sensEnlevement(f); }
    }
    if (enlever && variante != "borgne" && variante != "mince") f.inverse = f.inverse || variante == "face";
  }
  arrondirDefauts(f);  // valeurs de départ rondes dans l'unité du document (AP-41)
  if (type == TypeFonction::Extrusion && enlever && variante.empty()) { f.fin = "borgne"; f.longueur = valeurRonde(5); f.inverse = sensEnlevement(f); }
  if (type == TypeFonction::Revolution && variante == "milieu") f.milieu = true;
  if (type == TypeFonction::Chanfrein && !variante.empty()) f.typeChanfrein = variante;
  ouvrirGestionnaire(f, true);
  if (type == TypeFonction::Extrusion && variante == "face") { activerChamp(Champ::Fin1); majProprietes(); }
}

void AtelierPiece::editerFonction(int id) {
  const FonctionPiece* f = m_piece.fonction(id);
  if (!f) return;
  if (f->type == TypeFonction::Esquisse) editerEsquisse(id);
  else ouvrirGestionnaire(*f, false);
}

// Valeur de départ ronde dans l'unité du document : 10 mm deviennent 1 cm, ou 0,5 in (série 1, 2, 5) ; en mm, rien ne
// change (AP-41)
double AtelierPiece::valeurRonde(double mm) const {
  const Unites u = unites();
  if (u.longueur == UniteLongueur::Millimetre || mm <= 0) return mm;
  const double v = depuisMillimetres(mm, u.longueur);
  const double p = std::pow(10.0, std::floor(std::log10(v)));
  double meilleur = p;
  for (double m : {1.0, 2.0, 5.0, 10.0})
    if (std::abs(std::log(m * p / v)) < std::abs(std::log(meilleur / v))) meilleur = m * p;
  return versMillimetres(meilleur, u.longueur);
}

void AtelierPiece::arrondirDefauts(FonctionPiece& f) const {
  if (unites().longueur == UniteLongueur::Millimetre) return;
  for (double* x : {&f.longueur, &f.longueur2, &f.rayon, &f.distance, &f.distance2, &f.epaisseur, &f.epaisseurMince, &f.profondeur, &f.pas1, &f.pas2})
    if (*x > 0) *x = valeurRonde(*x);
}

// Sens d'un enlèvement neuf : celui qui retire le plus de matière, essayé dans les deux sens sur la pièce (AP-20).
// Vrai : sens inverse de la normale de l'esquisse (vers la matière pour une esquisse posée sur une face).
bool AtelierPiece::sensEnlevement(const FonctionPiece& f) const {
  if (m_res.forme.IsNull() || !m_piece.fonction(f.esquisseId)) return true;
  const double v0 = volume(m_res.forme);
  double retire[2] = {0, 0};
  for (int k = 0; k < 2; ++k) {
    Piece temp = m_piece;
    FonctionPiece g = f;
    g.inverse = k == 1;
    g.id = temp.prochainId;
    temp.fonctions.push_back(g);
    const ResultatPiece r = construirePiece(temp);
    if (r.erreur(g.id).empty() && !r.forme.IsNull()) retire[k] = v0 - volume(r.forme);
  }
  return retire[1] >= retire[0] - 1e-9;
}

// Fonctions qu'une répétition ou une symétrie de fonctions peut recopier
bool AtelierPiece::sourceRepetable(const FonctionPiece& f) {
  return f.type == TypeFonction::Extrusion || f.type == TypeFonction::Revolution || f.type == TypeFonction::Percage || f.type == TypeFonction::Balayage ||
         f.type == TypeFonction::Lissage || f.type == TypeFonction::Import;
}

// Plan ou référence choisi dans l'arbre : plan de base (XY, XZ, YZ) ou fonction Plan, Axe, Point, Repère (« F:n »)
std::string AtelierPiece::supportDepuisArbre() const {
  const int c = fonctionChoisie();
  if (c == -1) return "XY";
  if (c == -2) return "XZ";
  if (c == -3) return "YZ";
  if (c == -5) return "X";
  if (c == -6) return "Y";
  if (c == -7) return "Z";
  if (c == -8) return "O";
  if (const FonctionPiece* g = m_piece.fonction(c); g && (g->type == TypeFonction::Plan || g->type == TypeFonction::Axe || g->type == TypeFonction::Point || g->type == TypeFonction::Repere))
    return "F:" + std::to_string(g->id);
  return {};
}

void AtelierPiece::ouvrirGestionnaire(const FonctionPiece& f, bool nouvelle) {
  m_symetrieParFonctions = !f.sources.empty();
  m_epaisseurParticuliere = 0;
  if (m_mesure) basculerMesure();  // la mesure se quitte quand un réglage s'ouvre (AP-36)
  m_attenteEsquisse = false;
  m_gestion = true;
  m_nouvelle = nouvelle;
  m_brouillon = f;
  // État d'avant : la pièce jusqu'à la fonction qui précède (ou entière pour une nouvelle fonction).
  m_avant = m_res;
  if (!nouvelle) {
    int precedente = -1;
    for (const auto& g : m_piece.fonctions) { if (g.id == f.id) break; precedente = g.id; }
    m_avant = precedente > 0 ? construirePiece(m_piece, precedente) : ResultatPiece();
  }
  // Champ de sélection actif à l'ouverture, selon le type de fonction
  switch (f.type) {
    case TypeFonction::Coque: activerChamp(Champ::Faces); break;
    case TypeFonction::Conge:
    case TypeFonction::Chanfrein: activerChamp(Champ::Aretes); break;
    case TypeFonction::Plan:
    case TypeFonction::Axe:
    case TypeFonction::Symetrie: activerChamp(f.supports.empty() || f.type == TypeFonction::Plan ? Champ::Supports : Champ::Aucun); break;
    case TypeFonction::Point: activerChamp(f.mode != "coordonnees" ? Champ::Supports : Champ::Aucun); break;
    case TypeFonction::Repere: activerChamp(Champ::Supports); break;
    case TypeFonction::Repetition: activerChamp(f.sources.empty() ? Champ::Sources : Champ::Aucun); break;
    case TypeFonction::Depouille: activerChamp(f.references.empty() ? Champ::Faces : Champ::Aucun); break;
    case TypeFonction::Percage: activerChamp(m_piece.fonction(f.esquisseId) ? Champ::Aucun : Champ::Esquisse); break;
    case TypeFonction::Balayage: activerChamp(!m_piece.fonction(f.cheminId) ? Champ::Chemin : (!f.tube && !m_piece.fonction(f.esquisseId)) ? Champ::Esquisse : Champ::Aucun); break;
    case TypeFonction::Lissage: activerChamp(f.sources.size() < 2 ? Champ::Profils : Champ::Aucun); break;
    case TypeFonction::Esquisse3D: activerChamp(Champ::Points3D); break;
    case TypeFonction::Import: activerChamp(Champ::Aucun); break;
    case TypeFonction::BordTombe: activerChamp(f.references.empty() ? Champ::Aretes : Champ::Aucun); break;
    case TypeFonction::Filetage: activerChamp(f.references.empty() ? Champ::Faces : Champ::Aucun); break;
    default:
      if (!m_piece.fonction(f.esquisseId)) activerChamp(Champ::Esquisse);
      else if (regionsEsquisse(*m_piece.fonction(f.esquisseId), m_res.repere(f.esquisseId)).size() > 1) activerChamp(Champ::Regions);
      else activerChamp(Champ::Aucun);
      break;
  }
  activerCommandes(false);
  apercu();
  majProprietes();
  majCoinValidation();  // coin ✔/✖ dans la vue aussi pour les fonctions (AP-14)
  emit historiqueChange();  // Annuler et Rétablir du projet sont suspendus pendant le réglage (AP-03)
}

// Filtre de sélection imposé par un réglage : liste du ruban et icônes de la bande suivent ensemble (AP-40)
void AtelierPiece::majFiltre(int index) {
  if (auto* c = m_outilsPiece->findChild<QComboBox*>(QStringLiteral("modeSelection"))) {
    const QSignalBlocker b(c);
    c->setCurrentIndex(index);
  }
  if (m_groupeFiltre)
    for (QAction* a : m_groupeFiltre->actions()) if (a->data().toInt() == index) a->setChecked(true);
}

void AtelierPiece::activerChamp(Champ c) {
  m_champ = c;
  majPlans();  // plans de base montrés quand le champ attend un plan (AP-15)
  VuePiece::Mode mode = VuePiece::Mode::Tout;
  if (c == Champ::Regions) mode = VuePiece::Mode::Regions;
  else if (c == Champ::Aretes) mode = VuePiece::Mode::Aretes;
  else if (c == Champ::Faces || c == Champ::Sources || c == Champ::Neutre || c == Champ::FacesEpaisseur) mode = VuePiece::Mode::Faces;
  else if (c == Champ::Direction) mode = VuePiece::Mode::Aretes;
  else if (c == Champ::AxeRevolution) mode = VuePiece::Mode::Tout;
  else if (c == Champ::Chemin || c == Champ::Profils) mode = VuePiece::Mode::Tout;
  else if (c == Champ::Sommet3D) mode = VuePiece::Mode::Sommets;
  else if (c == Champ::Points3D) mode = VuePiece::Mode::Tout;
  else if (c == Champ::Supports) mode = VuePiece::Mode::Tout;
  else if (c == Champ::Fin1 || c == Champ::Fin2) {
    const std::string& fin = c == Champ::Fin1 ? m_brouillon.fin : m_brouillon.fin2;
    mode = fin == "sommet" ? VuePiece::Mode::Sommets : VuePiece::Mode::Faces;
  }
  m_vue->setMode(mode);
  majFiltre(mode == VuePiece::Mode::Faces ? 1 : mode == VuePiece::Mode::Aretes ? 2 : mode == VuePiece::Mode::Sommets ? 3 : 0);
}

// OK du réglage : une fonction dont l'aperçu est en échec n'est validée qu'après confirmation, en disant que les
// fonctions qui suivent ne seront pas calculées (AP-09)
void AtelierPiece::validerReglage() {
  const int id = m_nouvelle ? m_piece.prochainId : m_brouillon.id;
  const std::string err = m_affiche.erreur(id);
  if (!err.empty()) {
    int aval = 0;
    bool apres = false;
    for (const auto& g : m_piece.fonctions) { if (apres && !g.desactivee) ++aval; if (g.id == id) apres = true; }
    const QString nom = m_brouillon.nom.empty() ? QString::fromUtf8(nomTypeFonction(m_brouillon.type)) : qs(m_brouillon.nom);
    QString texte = QStringLiteral("« %1 » est en échec : %2").arg(nom, qs(err));
    if (aval > 0) texte += QStringLiteral("\n\nLes %1 fonction(s) qui la suivent ne seront pas calculées tant qu’elle restera en échec.").arg(aval);
    texte += QStringLiteral("\n\nValider quand même ?");
    if (QMessageBox::question(this, QStringLiteral("Fonction en échec"), texte) != QMessageBox::Yes) return;
  }
  fermerGestionnaire(true);
}

void AtelierPiece::fermerGestionnaire(bool valider) {
  if (valider && m_brouillon.type == TypeFonction::Esquisse3D && m_brouillon.points3d.size() < 2) {
    // Une esquisse 3D demande au moins deux points : le réglage reste ouvert
    m_etat->setText(QStringLiteral("<span style='color:#C77A1E'><b>Place au moins deux points</b> : clique dans la vue (sur la pièce ou dans le vide), ou ajoute-les au tableau.</span>"));
    return;
  }
  m_vue->setPoignee(false);
  int id = m_brouillon.id;
  if (valider) {
    if (m_nouvelle) id = m_piece.ajouter(m_brouillon);
    else if (FonctionPiece* g = m_piece.fonction(m_brouillon.id)) *g = m_brouillon;
    enregistrer();
  }
  m_gestion = false;
  majCoinValidation();
  emit historiqueChange();
  m_champ = Champ::Aucun;
  // Fonction lancée depuis une esquisse puis annulée : retour dans l'esquisse pour la reprendre
  if (const int retour = std::exchange(m_retourEsquisse, 0); retour && !valider) {
    const bool nouvelle = m_retourNouvelle;
    const std::optional<FonctionPiece> avant = m_retourAvant;
    QTimer::singleShot(0, this, [this, retour, nouvelle, avant] {
      m_prochaineNouvelle = nouvelle;
      m_prochainAvant = avant;
      editerFonction(retour);
      m_prochainAvant.reset();
    });
  }
  m_retourAvant.reset();
  m_vue->afficherRegions({}, {});
  m_vue->setMode(VuePiece::Mode::Tout);
  majFiltre(0);
  activerCommandes(true);
  majChoixPieces();
  m_vue->viderSelection();
  reconstruire(false);
  if (valider) choisirFonction(id);
}

// Aperçu : la pièce rejouée avec la fonction en cours de réglage ; les références retenues sont surlignées.
// Poignée d'extrusion : au centre du profil, dans la direction de la fonction, au bout de la profondeur.
void AtelierPiece::majPoignee() {
  const bool ok = m_gestion && m_brouillon.type == TypeFonction::Extrusion && (m_brouillon.fin == "borgne" || m_brouillon.fin == "milieu") && m_piece.fonction(m_brouillon.esquisseId);
  if (!ok) { m_vue->setPoignee(false); return; }
  const FonctionPiece* e = m_piece.fonction(m_brouillon.esquisseId);
  double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
  for (const auto& el : e->esquisse.elements) {
    if (el.construction) continue;
    for (int pid : {el.p1, el.p2, el.centre})
      if (const PointEsquisse* p = e->esquisse.point(pid)) { u0 = std::min(u0, p->x); u1 = std::max(u1, p->x); v0 = std::min(v0, p->y); v1 = std::max(v1, p->y); }
  }
  if (u0 > u1) { m_vue->setPoignee(false); return; }
  const gp_Ax3 ax = m_res.repere(e->id);
  const gp_Pnt base = ax.Location().Translated(gp_Vec(ax.XDirection()) * ((u0 + u1) / 2) + gp_Vec(ax.YDirection()) * ((v0 + v1) / 2));
  gp_Dir dir = ax.Direction();
  if (m_brouillon.inverse) dir.Reverse();
  const double l = m_brouillon.fin == "milieu" ? m_brouillon.longueur / 2 : m_brouillon.longueur;
  m_vue->setPoignee(true, base, dir, l);
}

void AtelierPiece::apercu() {
  Piece temp = m_piece;
  if (m_nouvelle) {
    FonctionPiece f = m_brouillon;
    f.id = temp.prochainId;
    temp.fonctions.push_back(f);
  } else if (FonctionPiece* g = temp.fonction(m_brouillon.id)) {
    *g = m_brouillon;
  }
  m_affiche = construirePiece(temp);
  ResultatPiece affiche = m_affiche;
  masquerEsquissesUtilisees(affiche, temp);
  majVisibilite();  // l'esquisse, le chemin et les profils du réglage restent visibles (AP-34)
  // Pour une extrusion ou une révolution, l'esquisse choisie reste visible pendant le réglage.
  if (m_brouillon.type == TypeFonction::Extrusion || m_brouillon.type == TypeFonction::Revolution || m_brouillon.type == TypeFonction::Tole || m_brouillon.type == TypeFonction::Nervure)
    for (const auto& e : m_affiche.esquisses) if (e.first == m_brouillon.esquisseId) affiche.esquisses.push_back(e);
  colorerFaces(affiche);
  m_vue->afficher(affiche, false);
  std::vector<TopoDS_Shape> retenues;
  for (const auto& ref : m_brouillon.references) {
    for (const auto& ar : m_avant.aretes(ref)) retenues.push_back(ar);
    for (const auto& fc : m_avant.facesNommees(ref)) retenues.push_back(fc);
  }
  m_vue->surligner(retenues);
  // Régions de l'esquisse à choisir (plusieurs contours fermés) : faces translucides cliquables.
  std::vector<RegionEsquisse> regions;
  if ((m_brouillon.type == TypeFonction::Extrusion || m_brouillon.type == TypeFonction::Revolution || m_brouillon.type == TypeFonction::Tole || m_brouillon.type == TypeFonction::Nervure) && m_champ == Champ::Regions)
    if (const FonctionPiece* fe = m_piece.fonction(m_brouillon.esquisseId)) regions = regionsEsquisse(*fe, m_res.repere(fe->id));
  m_vue->afficherRegions(regions, m_brouillon.regions);
  majPoignee();
  const std::string err = m_affiche.erreur(m_nouvelle ? temp.prochainId : m_brouillon.id);
  m_etat->setText(err.empty() ? QStringLiteral("<span style='color:#2F6BF5'><b>Aperçu</b> : règle la fonction, puis ✔ OK.</span>")
                              : QStringLiteral("<span style='color:#C2402E'><b>%1</b></span>").arg(qs(err).toHtmlEscaped()));
}

// Clic dans la vue 3D : pendant un réglage, ajoute ou retire l'élément cliqué de la fonction en cours.
// Barre d'état : ce qui est sélectionné, avec sa mesure (aire d'une face, longueur d'une arête, distance et
// angle entre deux éléments).
void AtelierPiece::infoSelection() {
  const auto choisis = m_vue->selectionDetaillee();
  if (choisis.empty()) {
    if (m_mesure) { m_tracesMesure.clear(); majTracesMesure(); }
    return;
  }
  const std::vector<TopoDS_Shape> sel = [&] { std::vector<TopoDS_Shape> r; for (const auto& c : choisis) r.push_back(c.forme); return r; }();
  const Unites u = unites();
  auto L = [&](double mm) { return QString::fromStdString(ecrireValeur(std::round(mm * 1000) / 1000, false, u)); };
  // Géométrie mesurable de chaque élément : les plans de base et les plans de référence sont infinis, les axes des
  // droites, les points des points (AP-17)
  struct Mesurable {
    enum class Genre { Forme, Plan, Axe, Point } genre = Genre::Forme;
    TopoDS_Shape forme;
    gp_Pln plan;
    gp_Ax1 axe;
    gp_Pnt point;
    QString nom;
  };
  auto mesurable = [&](const VuePiece::ElementChoisi& c) {
    Mesurable m;
    m.forme = c.forme;
    using G = VuePiece::ElementChoisi::Genre;
    if (c.genre == G::PlanBase) {
      m.genre = Mesurable::Genre::Plan;
      m.plan = gp_Pln(c.plan == "XZ" ? gp_Ax3(gp::Origin(), gp::DY()) : c.plan == "YZ" ? gp_Ax3(gp::Origin(), gp::DX()) : gp_Ax3(gp::Origin(), gp::DZ()));
      m.nom = QStringLiteral("Plan de base %1 (infini)").arg(qs(c.plan));
    } else if (c.genre == G::Reference) {
      const FonctionPiece* g = m_piece.fonction(c.id);
      m.nom = g ? qs(g->nom) : QStringLiteral("Référence");
      for (const auto& [id, ax] : m_res.plansReference) if (id == c.id) { m.genre = Mesurable::Genre::Plan; m.plan = gp_Pln(ax); m.nom += QStringLiteral(" (plan infini)"); }
      for (const auto& [id, ax] : m_res.axesReference) if (id == c.id) { m.genre = Mesurable::Genre::Axe; m.axe = ax; m.nom += QStringLiteral(" (axe)"); }
      for (const auto& [id, pt] : m_res.pointsReference)
        if (id == c.id) {
          m.genre = Mesurable::Genre::Point;
          m.point = pt;
          m.nom += QStringLiteral(" · X %1 · Y %2 · Z %3").arg(L(pt.X()), L(pt.Y()), L(pt.Z()));
        }
      for (const auto& [id, ax] : m_res.reperesReference) if (id == c.id) { m.genre = Mesurable::Genre::Point; m.point = ax.Location(); m.nom += QStringLiteral(" (origine du repère)"); }
    } else if (c.genre == G::Esquisse) {
      const FonctionPiece* g = m_piece.fonction(c.id);
      m.nom = g ? QStringLiteral("Esquisse « %1 »").arg(qs(g->nom)) : QStringLiteral("Esquisse");
    }
    return m;
  };
  std::vector<Mesurable> mes;
  for (const auto& c : choisis) mes.push_back(mesurable(c));
  // Mesure complète (M11) : chaque élément décrit, puis distance minimale et ΔX ΔY ΔZ, centre à centre, entraxe,
  // écart de plans parallèles, angle vrai (0 à 180°) ; au-delà de deux éléments, les totaux
  std::vector<ElementMesure> elements;
  for (const auto& m : mes) {
    ElementMesure e;
    switch (m.genre) {
      case Mesurable::Genre::Plan: e.genre = ElementMesure::Genre::Plan; e.plan = m.plan; break;
      case Mesurable::Genre::Axe: e.genre = ElementMesure::Genre::Axe; e.axe = m.axe; break;
      case Mesurable::Genre::Point: e.genre = ElementMesure::Genre::Point; e.point = m.point; break;
      default: e.forme = m.forme; break;
    }
    e.nom = m.nom.toStdString();
    e.solide = m_res.forme;
    elements.push_back(e);
  }
  const ResultatMesure r = mesurer(elements);
  // Barre d'état : l'élément seul, ou l'essentiel entre deux, ou les totaux
  QString texte;
  {
    const auto lignes = lignesMesure(r, u);
    if (mes.size() == 1 && !lignes.empty()) texte = QString::fromStdString(lignes.front()).section(QStringLiteral(". "), 1);
    else if (mes.size() == 2) {
      texte = QStringLiteral("2 éléments");
      if (r.distance) texte += QStringLiteral(" · distance %1").arg(L(r.distanceMin));
      if (r.entraxe) texte += QStringLiteral(" · entraxe %1").arg(L(r.valeurEntraxe));
      if (r.angle) {
        // Deux faces : l'angle qu'elles font entre elles (parallèles : 0°, d'équerre : 90°)
        const double a = r.natureAngle == ResultatMesure::Angle::Normales ? 180 - r.angleDeg : r.angleDeg;
        texte += QStringLiteral(" · angle %1").arg(QString::fromStdString(texteGrandeur({GrandeurMesure::Nature::Angle, "", a}, u)));
      }
    } else if (!lignes.empty()) texte = QStringLiteral("%1 éléments · %2").arg(mes.size()).arg(QString::fromStdString(lignes.back()));
  }
  m_etat->setText(QStringLiteral("<b>%1</b>").arg(texte.toHtmlEscaped()));
  if (m_mesure) {
    const Unites um = unitesMesure();
    QString detail = QStringLiteral("<b>Mesurer</b>");
    const auto lignes = lignesMesure(r, um);
    for (std::size_t i = 0; i < lignes.size(); ++i)
      detail += (i < r.elements.size() ? QStringLiteral("<br>%1") : QStringLiteral("<br><b>%1</b>")).arg(QString::fromStdString(lignes[i]).toHtmlEscaped());
    detail += QStringLiteral("<br><span style='color:#6B7393'>Clique d’autres éléments pour les ajouter ; M ou Échap pour quitter.</span>");
    m_texteMesure->setText(detail);
    m_tracesMesure.clear();
    if (r.distance && r.distanceMin > 1e-9)
      m_tracesMesure.push_back({r.pa, r.pb, QString::fromStdString(texteGrandeur({GrandeurMesure::Nature::Longueur, "", r.distanceMin}, um))});
    if (r.centres && r.distanceCentres > 1e-9 && (r.ca.Distance(r.pa) > 1e-6 || r.cb.Distance(r.pb) > 1e-6))
      m_tracesMesure.push_back({r.ca, r.cb, QStringLiteral("⊙ ") + QString::fromStdString(texteGrandeur({GrandeurMesure::Nature::Longueur, "", r.distanceCentres}, um))});
    majTracesMesure();
    placerPanneauMesure();
  }
}

prom::Unites AtelierPiece::unitesMesure() const {
  Unites u = unites();
  if (m_uniteMesure && !m_uniteMesure->currentData().toString().isEmpty()) u.longueur = uniteLongueurDepuis(m_uniteMesure->currentData().toString().toStdString());
  return u;
}

void AtelierPiece::placerPanneauMesure() {
  m_panneauMesure->adjustSize();
  m_panneauMesure->move(m_panneauMesure->parentWidget()->width() - m_panneauMesure->width() - 12, m_panneauMesure->parentWidget()->height() - m_panneauMesure->height() - 12);
}

void AtelierPiece::majTracesMesure() {
  std::vector<VuePiece::TraitMesure> t = m_mesuresGardees;
  if (m_mesure) t.insert(t.end(), m_tracesMesure.begin(), m_tracesMesure.end());
  m_vue->setTracesMesure(t);
}

void AtelierPiece::garderMesure() {
  if (m_tracesMesure.empty()) { m_etat->setText(QStringLiteral("Rien à garder : mesure d’abord deux éléments.")); return; }
  m_mesuresGardees.insert(m_mesuresGardees.end(), m_tracesMesure.begin(), m_tracesMesure.end());
  m_tracesMesure.clear();
  m_vue->viderSelection();
  majTracesMesure();
  m_etat->setText(QStringLiteral("%1 mesure(s) gardée(s) dans la vue ; « Effacer » les retire.").arg(m_mesuresGardees.size()));
}

void AtelierPiece::effacerMesures() {
  m_mesuresGardees.clear();
  majTracesMesure();
}

// Barre flottante près du curseur : les commandes qui ont du sens pour ce qui vient d'être sélectionné.
// Une esquisse utilisée par une fonction se masque d'elle-même (comme dans SolidWorks) ; on peut la montrer ou la
// masquer à la demande (Espace, clic droit) ; celle qu'on choisit dans l'arbre reste toujours visible.
void AtelierPiece::basculerMesure() {
  if (!m_mesure && !peutLancer()) return;
  m_attenteEsquisse = false;  // AP-11
  m_mesure = !m_mesure;
  m_vue->setCumulatif(m_mesure);
  m_vue->viderSelection();
  m_barre3D->hide();
  m_tracesMesure.clear();
  if (m_mesure) {
    m_texteMesure->setText(QStringLiteral("<b>Mesurer</b><br>Clique les faces, arêtes, sommets, plans ou axes à mesurer.<br><span style='color:#6B7393'>M ou Échap pour quitter.</span>"));
    placerPanneauMesure();
    m_panneauMesure->show();
    m_panneauMesure->raise();
    m_etat->setText(QStringLiteral("<b>Mesurer</b> : clique les éléments un à un (ils s'ajoutent), Échap pour quitter."));
  } else {
    m_panneauMesure->hide();
    m_etat->setText(QString());
  }
  majTracesMesure();
}

// Vue en coupe : un plan de base qu'on déplace avec un curseur ; la matière coupée apparaît en couleur.
void AtelierPiece::vueEnCoupe() {
  if (!peutLancer()) return;
  if (m_panneauCoupe) {
    const bool montrer = !m_panneauCoupe->isVisible();
    m_panneauCoupe->setVisible(montrer);
    if (!montrer) m_vue->setCoupe(false);
    else if (auto* b = m_panneauCoupe->findChild<QSlider*>()) emit b->valueChanged(b->value());
    return;
  }
  if (m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("Il n’y a rien à couper.")); return; }
  m_panneauCoupe = new QFrame(m_barre3D->parentWidget());
  m_panneauCoupe->setObjectName(QStringLiteral("panneauCoupe"));
  m_panneauCoupe->setStyleSheet(QStringLiteral("#panneauCoupe { background: rgba(255,255,255,240); border: 1px solid #CDD3E4; border-radius: 9px; }"));
  auto* l = new QHBoxLayout(m_panneauCoupe);
  l->setContentsMargins(10, 6, 10, 6);
  auto* plan = new QComboBox;
  plan->addItems({QStringLiteral("Plan XY"), QStringLiteral("Plan XZ"), QStringLiteral("Plan YZ")});
  auto* curseur = new QSlider(Qt::Horizontal);
  curseur->setRange(0, 1000);
  curseur->setValue(500);
  curseur->setMinimumWidth(220);
  auto* inverser = new QCheckBox(QStringLiteral("Inverser"));
  auto* valeurTexte = new QLabel;
  auto* fermer = new QToolButton;
  fermer->setIcon(icone(QStringLiteral("annuler")));
  l->addWidget(new QLabel(QStringLiteral("<b>Coupe</b>")));
  l->addWidget(plan);
  l->addWidget(curseur);
  l->addWidget(valeurTexte);
  l->addWidget(inverser);
  l->addWidget(fermer);
  auto appliquer = [this, plan, curseur, inverser, valeurTexte] {
    Bnd_Box b;
    BRepBndLib::Add(m_res.forme, b);
    if (b.IsVoid()) return;
    double x0, y0, z0, x1, y1, z1;
    b.Get(x0, y0, z0, x1, y1, z1);
    const int i = plan->currentIndex();
    const double mn = i == 0 ? z0 : i == 1 ? y0 : x0, mx = i == 0 ? z1 : i == 1 ? y1 : x1;
    const double pos = mn + (mx - mn) * curseur->value() / 1000.0;
    gp_Dir n = i == 0 ? gp_Dir(0, 0, -1) : i == 1 ? gp_Dir(0, -1, 0) : gp_Dir(-1, 0, 0);
    if (inverser->isChecked()) n.Reverse();
    const gp_Pnt o = i == 0 ? gp_Pnt(0, 0, pos) : i == 1 ? gp_Pnt(0, pos, 0) : gp_Pnt(pos, 0, 0);
    m_vue->setCoupe(true, gp_Pln(o, n));
    valeurTexte->setText(QString::fromStdString(ecrireValeur(std::round(pos * 100) / 100, false, unites())));
  };
  connect(plan, &QComboBox::currentIndexChanged, this, appliquer);
  connect(curseur, &QSlider::valueChanged, this, appliquer);
  connect(inverser, &QCheckBox::toggled, this, appliquer);
  connect(fermer, &QToolButton::clicked, this, [this] { m_panneauCoupe->hide(); m_vue->setCoupe(false); });
  m_panneauCoupe->adjustSize();
  m_panneauCoupe->move(60, m_panneauCoupe->parentWidget()->height() - m_panneauCoupe->height() - 14);
  m_panneauCoupe->show();
  m_panneauCoupe->raise();
  appliquer();
}

QString AtelierPiece::premiereErreur() const {
  if (m_res.erreurs.empty()) return {};
  const auto& [id, msg] = m_res.erreurs.front();
  const FonctionPiece* f = m_piece.fonction(id);
  return (f ? qs(f->nom) + QStringLiteral(" : ") : QString()) + qs(msg);
}

void AtelierPiece::montrerPremiereErreur() {
  if (m_res.erreurs.empty()) return;
  choisirFonction(m_res.erreurs.front().first);
}

void AtelierPiece::verifierFabricationPiece() {
  if (m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("Il n’y a pas encore de volume à vérifier.")); return; }
  const auto avis = verifierFabrication(m_res.forme);
  // Panneau latéral (U1) : la vue reste manipulable, « Montrer » surligne dans la vue
  auto* d = new QWidget;
  d->setObjectName(QStringLiteral("verificationFabrication"));
  d->setStyleSheet(QString::fromLatin1(STYLE_PANNEAU));
  auto* col = new QVBoxLayout(d);
  col->setContentsMargins(0, 0, 0, 0);
  int aCorriger = 0;
  for (const auto& x : avis) aCorriger += x.niveau == 1;
  col->addWidget(new QLabel(avis.empty() ? QStringLiteral("<span style='color:#2E8B57; font-weight:700; font-size:14px'>✓ Rien à signaler</span><br>Pas de paroi mince, de petit rayon, de trou trop profond ni d’angle intérieur vif.")
                                         : QStringLiteral("<span style='font-weight:700; font-size:14px'>%1 point(s) à corriger, %2 à savoir</span>").arg(aCorriger).arg(avis.size() - aCorriger)));
  for (const auto& x : avis) {
    auto* g = new QGroupBox;
    auto* l = new QHBoxLayout(g);
    auto* t = new QLabel(QStringLiteral("<span style='color:%1'>%2</span> %3").arg(x.niveau ? QStringLiteral("#C2402E") : QStringLiteral("#A87410"), x.niveau ? QStringLiteral("✖") : QStringLiteral("ⓘ"), qs(x.message).toHtmlEscaped()));
    t->setWordWrap(true);
    l->addWidget(t, 1);
    auto* montrer = new QPushButton(icone(QStringLiteral("afficher")), QStringLiteral("Montrer"));
    montrer->setObjectName(QStringLiteral("champ"));
    const auto formes = x.formes;
    connect(montrer, &QPushButton::clicked, this, [this, f2 = formes] { m_vue->surligner(f2); });
    l->addWidget(montrer);
    col->addWidget(g);
  }
  col->addStretch(1);
  m_panneau->ouvrir(QStringLiteral("fabrication"), QStringLiteral("Vérification de fabrication"), QStringLiteral("percage"), d, PanneauLateral::Boutons::Fermer);
  m_etat->setText(avis.empty() ? QStringLiteral("<b>✓ Fabrication</b> : rien à signaler.") : QStringLiteral("<b>Fabrication</b> : %1 point(s) à corriger.").arg(aCorriger));
}

void AtelierPiece::afficherRetour(const QString& texte, std::function<void()> f) {
  if (!m_retour) {
    m_retour = new QPushButton(m_barre3D->parentWidget());
    m_retour->setObjectName(QStringLiteral("retour"));
    m_retour->setStyleSheet(QStringLiteral("#retour { background: #2F6BF5; color: white; border: none; border-radius: 8px; padding: 8px 14px; font-weight: 600; } #retour:hover { background: #2453D6; }"));
  }
  m_retour->disconnect();
  if (!f) { m_retour->hide(); return; }
  m_retour->setText(QStringLiteral("↩ ") + texte);
  connect(m_retour, &QPushButton::clicked, this, [f] { f(); });
  m_retour->adjustSize();
  m_retour->move((m_retour->parentWidget()->width() - m_retour->width()) / 2, 12);
  m_retour->show();
  m_retour->raise();
}

void AtelierPiece::fonctionDepuisEsquisse(TypeFonction type, bool enlever) {
  const int id = m_esquisseEditee;
  if (!id) return;
  // Au retour (fonction annulée), l'esquisse reprend son état d'ouverture : neuve, ✖ la supprime encore (EI-27)
  const bool nouvelle = m_esquisseNouvelle;
  const FonctionPiece avant = m_esquisseAvant;
  terminerEsquisse();
  if (m_esquisseEditee) return;  // l'esquisse n'a pas pu se terminer (message affiché)
  choisirFonction(id);
  nouvelleFonction(type, enlever, type == TypeFonction::Extrusion ? "borgne" : "");
  if (m_gestion) {
    m_brouillon.esquisseId = id;
    m_retourEsquisse = id;
    m_retourNouvelle = nouvelle;
    m_retourAvant = avant;
    apercu();
    majProprietes();
  }
}

// Commande « Mise en plan » : la dernière de la pièce, ou une nouvelle s'il n'y en a pas (AP-30)
void AtelierPiece::miseEnPlan() {
  if (!m_piece.plans.empty()) ouvrirMiseEnPlan(static_cast<int>(m_piece.plans.size()) - 1);
  else nouvelleMiseEnPlan();
}

// Nouvelle mise en plan : elle n'entre dans la pièce que si on la garde (AP-30)
void AtelierPiece::nouvelleMiseEnPlan() {
  if (!peutLancer()) return;
  if (m_indice < 0 || m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("Il faut une pièce avec du volume pour la mettre en plan.")); return; }
  PlanPiece pl;
  int n = static_cast<int>(m_piece.plans.size()) + 1;
  auto pris = [this](const std::string& nom) { return std::any_of(m_piece.plans.begin(), m_piece.plans.end(), [&](const PlanPiece& x) { return x.nom == nom; }); };
  while (pris("Mise en plan " + std::to_string(n))) ++n;
  pl.nom = "Mise en plan " + std::to_string(n);
  m_piece.plans.push_back(pl);
  if (!ouvrirMiseEnPlan(static_cast<int>(m_piece.plans.size()) - 1, true)) m_piece.plans.pop_back();
}

void AtelierPiece::renommerMiseEnPlan(int index) {
  if (index < 0 || index >= static_cast<int>(m_piece.plans.size())) return;
  bool ok = false;
  const QString n = QInputDialog::getText(this, QStringLiteral("Renommer la mise en plan"), QStringLiteral("Nom"), QLineEdit::Normal, qs(m_piece.plans[static_cast<std::size_t>(index)].nom), &ok);
  if (!ok || n.trimmed().isEmpty()) return;
  m_piece.plans[static_cast<std::size_t>(index)].nom = n.trimmed().toStdString();
  enregistrer();
  majArbre();
}

void AtelierPiece::supprimerMiseEnPlan(int index) {
  if (index < 0 || index >= static_cast<int>(m_piece.plans.size())) return;
  if (QMessageBox::question(this, QStringLiteral("Supprimer"), QStringLiteral("Supprimer « %1 » ?").arg(qs(m_piece.plans[static_cast<std::size_t>(index)].nom))) != QMessageBox::Yes) return;
  m_piece.plans.erase(m_piece.plans.begin() + index);
  enregistrer();
  majArbre();
}

// Filetages de la pièce pour la mise en plan (ISO 6410)
std::vector<OptionsMiseEnPlan::FiletageDessin> AtelierPiece::filetagesDessin(const ResultatPiece& r) {
  std::vector<OptionsMiseEnPlan::FiletageDessin> v;
  for (const auto& ft : r.filetages)
    for (const auto& axe : ft.axes) v.push_back({axe, ft.nominal, ft.mineur, ft.longueur, ft.exterieur, ft.designation});
  return v;
}

bool AtelierPiece::ouvrirMiseEnPlan(int index, bool creation) {
  if (index < 0 || index >= static_cast<int>(m_piece.plans.size())) return false;
  if (m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("<span style='color:#C2402E'>La pièce n’a pas encore de volume : rien à mettre en plan.</span>")); return false; }
  // Fonctions en échec : le plan montrerait la pièce sans elles (EX-13)
  if (!confirmerMalgreErreurs(QStringLiteral("Mise en plan"))) return false;
  // Cartouche : numéro de pièce et masse (provisoire si la matière n'est pas confirmée), MP-06
  std::string masse;
  if (const ProprietesMasse pm = proprietesMasse(m_res.forme, m_piece.densite); pm.valide)
    masse = (m_piece.densiteConfirmee ? std::string() : std::string("≈ ")) + QLocale().toString(pm.masse, 'f', pm.masse < 10 ? 2 : 1).toStdString() + " g";
  MiseEnPlan d(m_res.forme, m_piece.plans[static_cast<std::size_t>(index)], m_piece.nom, m_piece.materiau, this, m_piece.reference, masse);
  d.setFiletages(filetagesDessin(m_res));
  d.setProprietes(m_piece.proprietes);
  {
    std::vector<std::string> orphelines;
    auto cotes = cotesDuModele(m_piece, m_res, &orphelines);
    d.setCotesModele(std::move(cotes), std::move(orphelines));
  }
  if (creation) d.modeCreation();
  if (d.exec() != QDialog::Accepted && creation) return false;
  m_piece.plans[static_cast<std::size_t>(index)] = d.reglages();
  enregistrer();
  majArbre();
  return true;
}

// Variables : un tableau nom / expression ; les cotes qui les citent sont recalculées, la pièce reconstruite.
// Variables : un tableau nom / expression, dans une fenêtre d'outil non modale posée à côté de la vue (AP-37). Chaque
// valeur validée reconstruit la pièce, qu'on voit changer ; OK écrit, Annuler rend l'état d'avant. Les noms qu'une
// cote ne pourrait pas citer sont refusés (AP-23).
void AtelierPiece::variablesPiece() {
  if (!peutLancer() || m_indice < 0) return;
  // Dans le panneau latéral (U1), non modal : la vue reste manipulable, l'arbre attend la fin
  auto* d = new QWidget;
  d->setObjectName(QStringLiteral("variablesPiece"));
  auto* col = new QVBoxLayout(d);
  col->setContentsMargins(0, 0, 0, 0);
  auto* consigne = new QLabel(QStringLiteral("Une variable peut citer les précédentes (« e = L/4 »). Dans une cote, tape son nom ou une expression : la cote suivra. "
                                             "La pièce se met à jour à chaque valeur."));
  consigne->setWordWrap(true);
  col->addWidget(consigne);
  auto* table = new QTableWidget(0, 3);
  table->setHorizontalHeaderLabels({QStringLiteral("Nom"), QStringLiteral("Expression"), QStringLiteral("Valeur")});
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table->verticalHeader()->hide();
  auto* message = new QLabel;
  message->setWordWrap(true);
  message->setStyleSheet(QStringLiteral("color: #C2402E;"));
  const auto sauvegarde = std::make_shared<Piece>(m_piece);
  activerCommandes(false);
  m_arbre->setEnabled(false);
  auto lireTable = [table] {
    std::vector<std::pair<std::string, std::string>> vars;
    for (int r = 0; r < table->rowCount(); ++r) {
      const QString n = table->item(r, 0) ? table->item(r, 0)->text().trimmed() : QString();
      const QString x = table->item(r, 1) ? table->item(r, 1)->text().trimmed() : QString();
      if (!n.isEmpty()) vars.push_back({n.toStdString(), x.toStdString()});
    }
    return vars;
  };
  // Défaut de la liste (nom refusé, nom en double), vide si elle convient
  auto defaut = [](const std::vector<std::pair<std::string, std::string>>& vars) {
    std::set<std::string> vus;
    for (const auto& [n, x] : vars) {
      if (const std::string e = defautNomVariable(n); !e.empty()) return e;
      if (!vus.insert(n).second) return "« " + n + " » est déclarée deux fois.";
    }
    return std::string();
  };
  auto remplir = [this, table, message, defaut, sauvegarde](const std::vector<std::pair<std::string, std::string>>& vars) {
    const QSignalBlocker b(table);
    table->setRowCount(static_cast<int>(vars.size()));
    Piece essai = *sauvegarde;
    essai.variables = vars;
    const auto valeurs = evaluerGrandeurs(essai);
    for (size_t i = 0; i < vars.size(); ++i) {
      auto* nom = new QTableWidgetItem(qs(vars[i].first));
      if (const std::string e = defautNomVariable(vars[i].first); !e.empty()) { nom->setForeground(QColor(0xC2, 0x40, 0x2E)); nom->setToolTip(qs(e)); }
      table->setItem(static_cast<int>(i), 0, nom);
      table->setItem(static_cast<int>(i), 1, new QTableWidgetItem(qs(vars[i].second)));
      // Valeur dans sa dimension : nombre pur, longueur ou angle (ES-15)
      QString texte = QStringLiteral("⚠");
      if (const auto it = valeurs.find(vars[i].first); it != valeurs.end())
        texte = it->second.dimension == 0 ? QLocale().toString(it->second.valeur, 'g', 10)
                                          : QString::fromStdString(ecrireValeur(it->second.valeur, it->second.dimension == 2, unites()));
      auto* v = new QTableWidgetItem(texte);
      v->setFlags(v->flags() & ~Qt::ItemIsEditable);
      table->setItem(static_cast<int>(i), 2, v);
    }
    // Aperçu : la pièce reconstruite avec ces variables, sans rien écrire dans le projet
    const std::string e = defaut(vars);
    message->setText(qs(e));
    if (e.empty()) {
      m_piece = *sauvegarde;
      m_piece.variables = vars;
      std::string err;
      appliquerVariables(m_piece, &err);
      if (!err.empty()) message->setText(qs(err));
      reconstruire(false);
    }
  };
  remplir(m_piece.variables);
  connect(table, &QTableWidget::itemChanged, d, [remplir, lireTable] { remplir(lireTable()); });
  col->addWidget(table);
  col->addWidget(message);
  auto* ligne = new QHBoxLayout;
  auto* plus = new QPushButton(QStringLiteral("＋ Variable"));
  connect(plus, &QPushButton::clicked, d, [remplir, lireTable] {
    auto v = lireTable();
    int k = static_cast<int>(v.size()) + 1;
    auto pris = [&v](const std::string& n) { return std::any_of(v.begin(), v.end(), [&](const auto& x) { return x.first == n; }); };
    while (pris("v" + std::to_string(k))) ++k;
    v.push_back({"v" + std::to_string(k), "10"});
    remplir(v);
  });
  auto* moins = new QPushButton(QStringLiteral("Supprimer"));
  connect(moins, &QPushButton::clicked, d, [table, remplir, lireTable] {
    auto v = lireTable();
    const int r = table->currentRow();
    if (r >= 0 && r < static_cast<int>(v.size())) v.erase(v.begin() + r);
    remplir(v);
  });
  ligne->addWidget(plus);
  ligne->addWidget(moins);
  ligne->addStretch(1);
  col->addLayout(ligne);
  auto revenir = [this] {
    activerCommandes(true);
    m_arbre->setEnabled(true);
  };
  m_panneau->ouvrir(QStringLiteral("variables"), QStringLiteral("Variables de la pièce"), QStringLiteral("proprietes"), d, PanneauLateral::Boutons::ValiderAnnuler,
                    [this, lireTable, defaut, sauvegarde, revenir] {
                      const auto vars = lireTable();
                      if (const std::string e = defaut(vars); !e.empty()) { m_panneau->setMessage(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(qs(e).toHtmlEscaped())); return false; }
                      m_piece = *sauvegarde;
                      m_piece.variables = vars;
                      std::string err;
                      appliquerVariables(m_piece, &err);
                      revenir();
                      enregistrer();
                      reconstruire(false);
                      m_etat->setText(err.empty() ? QStringLiteral("Variables appliquées.") : QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(qs(err).toHtmlEscaped()));
                      return true;
                    },
                    // Fermeture sans Valider : la pièce revient à son état d'avant
                    [this, sauvegarde, revenir] {
                      revenir();
                      m_piece = *sauvegarde;
                      reconstruire(false);
                    });
}

void AtelierPiece::majVisibilite() {
  std::set<int> masquees;
  const std::set<int> utilisees = esquissesUtilisees(m_piece), reglage = esquissesDuReglage();
  const int choisie = fonctionChoisie();
  for (const auto& f : m_piece.fonctions) {
    if (f.type != TypeFonction::Esquisse || f.id == choisie || reglage.count(f.id)) continue;
    if (!esquisseVisible(f, utilisees)) masquees.insert(f.id);
  }
  m_vue->setEsquissesMasquees(masquees);
  m_vue->setApparence(QColor(qs(m_piece.couleur)), m_piece.transparence);
}

// Apparences par face (M12) : couleurs propres des faces nommées, posées sur le solide affiché
void AtelierPiece::colorerFaces(const ResultatPiece& r) {
  std::vector<std::pair<TopoDS_Shape, QColor>> c;
  for (const auto& [face, hexa] : facesColorees(m_piece.couleursFaces, r)) c.emplace_back(face, QColor(qs(hexa)));
  m_vue->setCouleursSousFormes(c);
}

void AtelierPiece::basculerVisibiliteEsquisse(int id) {
  FonctionPiece* f = m_piece.fonction(id);
  if (!f || f->type != TypeFonction::Esquisse) return;
  const bool visible = esquisseVisible(*f, esquissesUtilisees(m_piece));
  f->visibilite = visible ? 2 : 1;
  enregistrer();
  reconstruire(false);
  m_etat->setText(visible ? QStringLiteral("%1 masquée (Espace pour la montrer).").arg(qs(f->nom)) : QStringLiteral("%1 affichée.").arg(qs(f->nom)));
}

void AtelierPiece::majBarre3D(const QPoint& pres) {
  for (QWidget* w : m_barre3D->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) w->deleteLater();
  delete m_barre3D->layout();
  if (m_gestion || m_esquisseEditee || m_attenteEsquisse) { m_barre3D->hide(); return; }
  struct A { const char* icone; QString bulle; std::function<void()> f; };
  std::vector<A> actions;
  QString titre;
  const auto sel = m_vue->selection();
  const int esquisse = m_vue->esquisseSelectionnee();
  const FonctionPiece* fc = m_piece.fonction(fonctionChoisie());
  const FonctionPiece* e3 = m_piece.fonction(esquisse ? esquisse : (fc ? fc->id : 0));
  if (e3 && e3->type == TypeFonction::Esquisse3D) {
    titre = QStringLiteral("Esquisse 3D");
    const int id = e3->id;
    actions = {{"balayage", QStringLiteral("Tube le long de ce chemin"), [this] { nouvelleFonction(TypeFonction::Balayage, false, "tube"); }},
               {"esquisse3d", QStringLiteral("Modifier les points"), [this, id] { editerFonction(id); }}};
  } else if (esquisse || (sel.empty() && fc && fc->type == TypeFonction::Esquisse)) {
    titre = QStringLiteral("Esquisse");
    const int id = esquisse ? esquisse : fc->id;
    actions = {{"extrusion", QStringLiteral("Extrusion (E)"), [this] { nouvelleFonction(TypeFonction::Extrusion, false, "borgne"); }},
               {"enlevement", QStringLiteral("Enlèvement (X)"), [this] { nouvelleFonction(TypeFonction::Extrusion, true, "borgne"); }},
               {"revolution", QStringLiteral("Révolution (R)"), [this] { nouvelleFonction(TypeFonction::Revolution, false, ""); }},
               {"esquisse", QStringLiteral("Modifier l’esquisse"), [this, id] { editerFonction(id); }}};
  } else if (!sel.empty()) {
    int faces = 0, aretes = 0, sommets = 0;
    bool plane = false;
    for (const auto& s : sel) {
      if (s.ShapeType() == TopAbs_FACE) { ++faces; plane = BRepAdaptor_Surface(TopoDS::Face(s)).GetType() == GeomAbs_Plane; }
      if (s.ShapeType() == TopAbs_EDGE) ++aretes;
      if (s.ShapeType() == TopAbs_VERTEX) ++sommets;
    }
    if (faces == 1 && aretes == 0 && sommets == 0) {
      titre = plane ? QStringLiteral("Face plane") : QStringLiteral("Face");
      if (plane) actions.push_back({"esquisse", QStringLiteral("Esquisse sur cette face"), [this] { creerEsquisseDepuisSelection(); }});
      actions.push_back({"conge", QStringLiteral("Congé sur les arêtes de la face"), [this] { nouvelleFonction(TypeFonction::Conge, false, ""); }});
      actions.push_back({"chanfrein", QStringLiteral("Chanfrein sur les arêtes de la face"), [this] { nouvelleFonction(TypeFonction::Chanfrein, false, "egal"); }});
      actions.push_back({"coque", QStringLiteral("Coque en ouvrant cette face"), [this] { nouvelleFonction(TypeFonction::Coque, false, ""); }});
    } else if (aretes >= 1 && faces == 0) {
      titre = aretes == 1 ? QStringLiteral("Arête") : QStringLiteral("%1 arêtes").arg(aretes);
      actions.push_back({"conge", QStringLiteral("Congé"), [this] { nouvelleFonction(TypeFonction::Conge, false, ""); }});
      actions.push_back({"chanfrein", QStringLiteral("Chanfrein"), [this] { nouvelleFonction(TypeFonction::Chanfrein, false, "egal"); }});
    } else if (faces >= 2 && aretes == 0) {
      titre = QStringLiteral("%1 faces").arg(faces);
      actions.push_back({"conge", QStringLiteral("Congé sur leurs arêtes"), [this] { nouvelleFonction(TypeFonction::Conge, false, ""); }});
      actions.push_back({"coque", QStringLiteral("Coque en ouvrant ces faces"), [this] { nouvelleFonction(TypeFonction::Coque, false, ""); }});
    }
    if (faces >= 1 && aretes == 0 && sommets == 0) actions.push_back({"couleur-face", QStringLiteral("Couleur des faces…"), [this] { choisirCouleurFaces(); }});
  }
  if (actions.empty()) { m_barre3D->hide(); return; }
  auto* l = new QHBoxLayout(m_barre3D);
  l->setContentsMargins(6, 3, 6, 3);
  l->setSpacing(2);
  l->addWidget(new QLabel(titre));
  for (const auto& x : actions) {
    auto* b = new QToolButton(m_barre3D);
    b->setIcon(icone(QString::fromLatin1(x.icone)));
    b->setIconSize(QSize(24, 24));
    b->setToolTip(x.bulle);
    const auto f = x.f;
    connect(b, &QToolButton::clicked, this, [this, f] { m_barre3D->hide(); f(); });
    l->addWidget(b);
  }
  m_barre3D->adjustSize();
  QWidget* scene = m_barre3D->parentWidget();
  QPoint p = pres + QPoint(18, 18);
  p.setX(std::clamp(p.x(), 6, std::max(6, scene->width() - m_barre3D->width() - 6)));
  p.setY(std::clamp(p.y(), 6, std::max(6, scene->height() - m_barre3D->height() - 6)));
  m_barre3D->move(p);
  m_barre3D->show();
  m_barre3D->raise();
}

void AtelierPiece::selectionVue() {
  if (!m_gestion && !m_attenteEsquisse && !m_esquisseEditee) { infoSelection(); if (!m_mesure) majBarre3D(m_vue->derniereSouris()); }
  if (m_attenteEsquisse && !m_gestion) { if (creerEsquisseDepuisSelection()) return; }
  if (m_gestion && m_champ == Champ::Sommet3D) {
    for (const auto& s : m_vue->selection())
      if (s.ShapeType() == TopAbs_VERTEX) { const gp_Pnt q = BRep_Tool::Pnt(TopoDS::Vertex(s)); m_brouillon.points3d.push_back({q.X(), q.Y(), q.Z()}); }
    m_vue->viderSelection();
    apercu();
    majProprietes();
    return;
  }
  if (m_gestion && (m_champ == Champ::Chemin || m_champ == Champ::Profils || (m_champ == Champ::Esquisse && m_brouillon.type == TypeFonction::Balayage))) {
    int esq = m_vue->esquisseSelectionnee();
    if (!esq) return;
    if (m_champ == Champ::Profils) {
      auto& src = m_brouillon.sources;
      const auto it = std::find(src.begin(), src.end(), esq);
      if (it != src.end()) src.erase(it); else src.push_back(esq);
    } else if (m_champ == Champ::Chemin) {
      m_brouillon.cheminId = esq;
    } else {
      m_brouillon.esquisseId = esq;
      activerChamp(Champ::Chemin);
    }
    m_vue->viderSelection();
    apercu();
    majProprietes();
    return;
  }
  if (m_gestion && (m_champ == Champ::Supports || m_champ == Champ::Sources || m_champ == Champ::Direction || m_champ == Champ::Neutre)) {
    std::vector<std::string> noms;
    const std::string plan = m_vue->planSelectionne();
    if (!plan.empty()) noms.push_back(plan);
    const std::string refSel = m_vue->referenceSelectionnee();
    if (!refSel.empty()) noms.push_back(refSel);
    if (const std::string axeB = m_vue->axeSelectionne(); !axeB.empty()) noms.push_back(axeB);
    for (const auto& s : m_vue->selection()) { const std::string n = nomReference(s); if (!n.empty()) noms.push_back(n); }
    if (noms.empty()) return;
    if (m_champ == Champ::Supports) {
      auto& sup = m_brouillon.supports;
      for (const auto& n : noms) {
        const auto it = std::find(sup.begin(), sup.end(), n);
        if (it != sup.end()) sup.erase(it); else sup.push_back(n);
      }
    } else if (m_champ == Champ::Neutre) {
      m_brouillon.planNeutre = noms.front();
      activerChamp(Champ::Faces);
    } else if (m_champ == Champ::Direction) {
      m_brouillon.direction1 = noms.front();
    } else {
      // Une face cliquée désigne la fonction qui l'a créée : son numéro est le début du nom.
      for (const auto& n : noms) {
        const int fid = std::atoi(n.c_str());
        const FonctionPiece* g = m_piece.fonction(fid);
        if (!g || (g->type != TypeFonction::Extrusion && g->type != TypeFonction::Revolution && g->type != TypeFonction::Percage && g->type != TypeFonction::Balayage &&
                   g->type != TypeFonction::Lissage && g->type != TypeFonction::Import))
          continue;
        auto& src = m_brouillon.sources;
        const auto it = std::find(src.begin(), src.end(), fid);
        if (it != src.end()) src.erase(it); else src.push_back(fid);
      }
    }
    m_vue->viderSelection();
    apercu();
    majProprietes();
    return;
  }
  if (m_gestion && m_champ == Champ::AxeRevolution) {
    // Axe de révolution au clic : axe de base, axe de référence, arête droite ou circulaire, face cylindrique (M8)
    std::string n = m_vue->axeSelectionne();
    if (n == "O") n.clear();
    if (n.empty()) n = m_vue->referenceSelectionnee();
    if (n.empty())
      for (const auto& sel : m_vue->selection()) {
        if (sel.ShapeType() == TopAbs_EDGE) n = nommerDansAvant(sel, false);
        else if (sel.ShapeType() == TopAbs_FACE) {
          BRepAdaptor_Surface sf(TopoDS::Face(sel));
          if (sf.GetType() == GeomAbs_Cylinder || sf.GetType() == GeomAbs_Cone) n = nommerDansAvant(sel, true);
        }
        if (!n.empty()) break;
      }
    if (n.empty()) return;
    m_brouillon.axeBase = n;
    activerChamp(Champ::Aucun);
    m_vue->viderSelection();
    apercu();
    majProprietes();
    return;
  }
  if (m_gestion && (m_champ == Champ::Regions || m_champ == Champ::Fin1 || m_champ == Champ::Fin2 || m_champ == Champ::Esquisse)) {
    if (m_champ == Champ::Regions) {
      const std::string cle = m_vue->regionSelectionnee();
      if (cle.empty()) return;
      auto& r = m_brouillon.regions;
      // Première sélection : on part de « toutes » et on garde seulement celle-ci ; ensuite on ajoute ou retire.
      auto it = std::find(r.begin(), r.end(), cle);
      if (it != r.end()) {
        const auto k = static_cast<std::size_t>(it - r.begin());
        r.erase(it);
        if (k < m_brouillon.pointsRegions.size()) m_brouillon.pointsRegions.erase(m_brouillon.pointsRegions.begin() + static_cast<std::ptrdiff_t>(k));
      } else r.push_back(cle);
      synchroniserPointsRegions(m_brouillon);
    } else if (m_champ == Champ::Esquisse) {
      const int e = m_vue->esquisseSelectionnee();
      if (!e) return;
      m_brouillon.esquisseId = e;
      m_brouillon.regions.clear();
      m_brouillon.pointsRegions.clear();
    } else {
      std::string ref;
      for (const auto& s : m_vue->selection()) {
        if (s.ShapeType() == TopAbs_FACE || s.ShapeType() == TopAbs_VERTEX) ref = nommerDansAvant(s, true);
      }
      if (ref.empty()) return;
      (m_champ == Champ::Fin1 ? m_brouillon.refFin : m_brouillon.refFin2) = ref;
      activerChamp(Champ::Aucun);
    }
    m_vue->viderSelection();
    apercu();
    majProprietes();
    return;
  }
  if (!m_gestion) {
    const int e = m_vue->esquisseSelectionnee();
    if (e) { choisirFonction(e); m_etat->setText(QStringLiteral("Esquisse choisie : clique Extrusion, Enlèvement ou Révolution.")); }
    else {
      const auto s = m_vue->selection();
      if (!s.empty()) m_etat->setText(QStringLiteral("%1 élément(s) sélectionné(s) (Maj pour en ajouter).").arg(s.size()));
    }
    return;
  }
  if (m_brouillon.type == TypeFonction::Extrusion || m_brouillon.type == TypeFonction::Revolution || m_brouillon.type == TypeFonction::Tole || m_brouillon.type == TypeFonction::Nervure) {
    const int e = m_vue->esquisseSelectionnee();
    if (e && e != m_brouillon.esquisseId) { m_brouillon.esquisseId = e; m_vue->viderSelection(); apercu(); majProprietes(); }
    return;
  }
  // Faces entières pour la coque et la dépouille ; arêtes pour les congés et les chanfreins (AP-07)
  const bool faces = m_brouillon.type == TypeFonction::Coque || m_brouillon.type == TypeFonction::Depouille;
  if (m_champ == Champ::FaceChanfrein) {
    for (const auto& s : m_vue->selection())
      if (s.ShapeType() == TopAbs_FACE) {
        const std::string n = nommerDansAvant(s, true);
        if (n.empty()) continue;
        m_brouillon.faceChanfrein = m_brouillon.faceChanfrein == n ? std::string() : n;
        activerChamp(Champ::Aretes);
        m_vue->viderSelection();
        apercu();
        majProprietes();
        return;
      }
    return;
  }
  if (m_champ == Champ::FacesEpaisseur) {
    // Faces d'épaisseur particulière : ajoutées avec la valeur du champ, un second clic les retire
    bool change = false;
    for (const auto& s : m_vue->selection())
      if (s.ShapeType() == TopAbs_FACE) {
        const std::string n = nommerDansAvant(s, true);
        if (n.empty()) continue;
        auto& l = m_brouillon.epaisseursFaces;
        const auto it = std::find_if(l.begin(), l.end(), [&](const auto& x) { return x.first == n; });
        if (it != l.end()) l.erase(it);
        else l.push_back({n, m_epaisseurParticuliere > 0 ? m_epaisseurParticuliere : m_brouillon.epaisseur * 2});
        change = true;
      }
    if (!change) return;
    m_vue->viderSelection();
    apercu();
    majProprietes();
    return;
  }
  // Sans champ actif, ou hors des champs d'arêtes et de faces, un clic n'ajoute rien (AP-19)
  if (m_champ != Champ::Aretes && m_champ != Champ::Faces) return;
  if (m_brouillon.type == TypeFonction::Filetage) {
    for (const auto& sel : m_vue->selection())
      if (sel.ShapeType() == TopAbs_FACE) {
        const std::string n = nommerDansAvant(sel, true);
        if (n.empty()) continue;
        m_brouillon.references = {n};
        activerChamp(Champ::Aucun);
        m_vue->viderSelection();
        apercu();
        majProprietes();
        return;
      }
    return;
  }
  if (m_brouillon.type == TypeFonction::BordTombe) {
    // Bord tombé : une seule arête (ou la face mince), qui remplace la précédente
    for (const auto& sel : m_vue->selection())
      if (sel.ShapeType() == TopAbs_EDGE || sel.ShapeType() == TopAbs_FACE) {
        const std::string n = nommerDansAvant(sel, sel.ShapeType() == TopAbs_FACE);
        if (n.empty()) continue;
        m_brouillon.references = {n};
        activerChamp(Champ::Aucun);
        m_vue->viderSelection();
        apercu();
        majProprietes();
        return;
      }
    return;
  }
  // On nomme ce qui est cliqué dans l'état d'avant la fonction (même géométrie, sans la retouche en cours).
  auto nommer = [&](const TopoDS_Shape& s) -> std::vector<std::string> {
    std::vector<std::string> r;
    if (s.ShapeType() == TopAbs_EDGE) {
      if (faces) return r;  // une arête ne désigne pas une face
      const std::string n = nommerDansAvant(s, false);
      if (!n.empty()) r.push_back(n);
    } else if (s.ShapeType() == TopAbs_FACE) {
      if (faces) {
        const std::string n = nommerDansAvant(s, true);
        if (!n.empty()) r.push_back(n);
      } else {
        for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
          const std::string n = nommerDansAvant(ex.Current(), false);
          if (!n.empty()) r.push_back(n);
        }
      }
    }
    return r;
  };
  bool change = false;
  for (const auto& s : m_vue->selection())
    for (const auto& n : nommer(s)) {
      auto& refs = m_brouillon.references;
      auto it = std::find(refs.begin(), refs.end(), n);
      if (it != refs.end()) refs.erase(it); else refs.push_back(n);
      change = true;
    }
  if (!change) return;
  m_vue->viderSelection();
  apercu();
  majProprietes();
}

void AtelierPiece::synchroniserPointsRegions(FonctionPiece& f) const {
  const FonctionPiece* fe = m_piece.fonction(f.esquisseId);
  if (!fe) { f.pointsRegions.clear(); return; }
  const auto regions = regionsEsquisse(*fe, m_res.repere(fe->id));
  std::vector<std::array<double, 2>> points;
  for (std::size_t k = 0; k < f.regions.size(); ++k) {
    const auto it = std::find_if(regions.begin(), regions.end(), [&](const RegionEsquisse& rg) { return rg.cle == f.regions[k]; });
    if (it != regions.end()) points.push_back({it->u, it->v});
    else if (k < f.pointsRegions.size()) points.push_back(f.pointsRegions[k]);
    else { f.pointsRegions.clear(); return; }  // clé inconnue sans point : la clé seule fera foi
  }
  f.pointsRegions = points;
}

std::string AtelierPiece::nomReference(const TopoDS_Shape& s) const {
  if (s.ShapeType() == TopAbs_FACE) return nommerDansAvant(s, true).empty() ? m_res.nomFace(TopoDS::Face(s)) : nommerDansAvant(s, true);
  if (s.ShapeType() == TopAbs_EDGE) return nommerDansAvant(s, false).empty() ? m_res.nomArete(TopoDS::Edge(s)) : nommerDansAvant(s, false);
  if (s.ShapeType() == TopAbs_VERTEX) return nommerDansAvant(s, true).empty() ? m_res.nomSommet(TopoDS::Vertex(s)) : nommerDansAvant(s, true);
  return {};
}

std::vector<std::string> AtelierPiece::referencesSelection(bool faces) const {
  std::vector<std::string> refs;
  for (const auto& s : m_vue->selection()) {
    std::string r;
    if (!faces && s.ShapeType() == TopAbs_EDGE) r = m_res.nomArete(TopoDS::Edge(s));
    if (faces && s.ShapeType() == TopAbs_FACE) r = m_res.nomFace(TopoDS::Face(s));
    if (!faces && s.ShapeType() == TopAbs_FACE)
      for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) {
        const std::string n = m_res.nomArete(TopoDS::Edge(ex.Current()));
        if (!n.empty() && std::find(refs.begin(), refs.end(), n) == refs.end()) refs.push_back(n);
      }
    if (!r.empty() && std::find(refs.begin(), refs.end(), r) == refs.end()) refs.push_back(r);
  }
  return refs;
}

void AtelierPiece::editerEsquisse(int id) {
  if (m_gestion) fermerGestionnaire(false);
  if (m_mesure) basculerMesure();  // AP-36
  FonctionPiece* f = m_piece.fonction(id);
  if (!f || f->type != TypeFonction::Esquisse) { finirSessionEsquisse(); return; }
  commencerSessionEsquisse();
  m_esquisseEditee = id;
  m_esquisseAvant = m_prochainAvant ? *m_prochainAvant : *f;
  m_esq->setVariables(evaluerGrandeurs(m_piece));
  {
    std::vector<std::string> noms;
    const Esquisse ext = geometrieExterne(&noms);
    m_esq->setReferencesExternes(ext, noms);
  }
  m_esquisseNouvelle = m_prochaineNouvelle;
  m_prochaineNouvelle = false;
  m_vue->sauverCamera();
  const gp_Ax3 ax = m_res.repere(id);
  // Projection entre l'écran et le plan de l'esquisse, par la caméra de la vue 3D.
  Esquisseur::Projection proj;
  proj.versEcran = [this, ax](double u, double v) {
    return m_vue->projeter(ax.Location().Translated(gp_Vec(ax.XDirection()) * u + gp_Vec(ax.YDirection()) * v));
  };
  proj.versEsquisse = [this, ax](const QPointF& p, double& u, double& v) {
    gp_Pnt o;
    gp_Dir d;
    if (!m_vue->rayon(p, o, d)) return false;
    const double den = gp_Vec(d).Dot(gp_Vec(ax.Direction()));
    if (std::abs(den) < 1e-12) return false;
    const double t = gp_Vec(o, ax.Location()).Dot(gp_Vec(ax.Direction())) / den;
    const gp_Pnt q = o.Translated(gp_Vec(d) * t);
    const gp_Vec w(ax.Location(), q);
    u = w.Dot(gp_Vec(ax.XDirection()));
    v = w.Dot(gp_Vec(ax.YDirection()));
    return true;
  };
  proj.zoomer = [this](const QPointF& p, double c) { m_vue->zoomer(p, c); };
  proj.tourner = [this](double dx, double dy) { m_vue->tourner(dx, dy); };
  proj.deplacer = [this](double dx, double dy) { m_vue->deplacer(dx, dy); };
  auto cadreEsquisse = [this, ax, id]() {
    Bnd_Box b;
    const FonctionPiece* g = m_piece.fonction(id);
    if (g)
      for (const auto& p : g->esquisse.points) b.Add(ax.Location().Translated(gp_Vec(ax.XDirection()) * p.x + gp_Vec(ax.YDirection()) * p.y));
    if (!m_res.forme.IsNull()) BRepBndLib::Add(m_res.forme, b);
    if (b.IsVoid() || std::sqrt(b.SquareExtent()) < 20) {
      b.Add(ax.Location().Translated(gp_Vec(ax.XDirection()) * -20 + gp_Vec(ax.YDirection()) * -20));
      b.Add(ax.Location().Translated(gp_Vec(ax.XDirection()) * 60 + gp_Vec(ax.YDirection()) * 40));
    }
    return b;
  };
  proj.recadrer = [this, cadreEsquisse] { m_vue->cadrer(cadreEsquisse()); };
  m_esq->setProjection(proj);
  m_esq->setUnites(unites());
  // Esquisse à références externes : telle que la reconstruction l'a mise à jour (S3)
  m_esq->editer(m_res.esquisseSuivie(id) ? *m_res.esquisseSuivie(id) : f->esquisse);
  // La pièce reste visible, translucide ; l'esquisse éditée n'a plus de tracé 3D (elle est dessinée par-dessus).
  ResultatPiece affiche = m_res;
  masquerEsquissesUtilisees(affiche, m_piece);
  affiche.esquisses.erase(std::remove_if(affiche.esquisses.begin(), affiche.esquisses.end(), [id](const auto& e) { return e.first == id; }), affiche.esquisses.end());
  m_vue->setPlansVisibles(false);
  colorerFaces(affiche);
  m_vue->afficher(affiche, false);
  m_vue->setSolideTranslucide(!m_res.forme.IsNull());
  m_vue->orienterVers(ax, cadreEsquisse());
  m_esq->show();
  m_esq->raise();
  m_outilsEsquisse->show();
  m_outilsPiece->hide();
  if (m_bandeVue) m_bandeVue->hide();
  m_arbre->setEnabled(false);
  m_attenteEsquisse = false;
  m_esq->setOutil(f->esquisse.elements.empty() ? Esquisseur::Outil::Rectangle : Esquisseur::Outil::Selection);
  m_esq->setFocus();
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("panneauProprietes"));
  page->setStyleSheet(QString::fromLatin1(STYLE_PANNEAU));
  auto* col = new QVBoxLayout(page);
  {
    // En-tête : nom de l'esquisse et plan de travail (repris en bandeau dans la vue)
    const QString plan = !f->face.empty() ? (f->face.rfind("F:", 0) == 0 ? QStringLiteral("plan de référence") : QStringLiteral("sur une face"))
                                          : f->plan == PlanBase::XY ? QStringLiteral("Plan XY (dessus)") : f->plan == PlanBase::XZ ? QStringLiteral("Plan XZ (face)") : QStringLiteral("Plan YZ (droite)");
    auto* haut = new QHBoxLayout;
    auto* ic = new QLabel;
    ic->setPixmap(icone(QStringLiteral("esquisse")).pixmap(26, 26));
    haut->addWidget(ic);
    haut->addWidget(new QLabel(QStringLiteral("<span style='font-size:15px; font-weight:700; color:#10142A'>%1</span><br><span style='color:#6B7393'>%2</span>").arg(qs(f->nom).toHtmlEscaped(), plan)), 1);
    col->addLayout(haut);
    m_esq->setTitre(qs(f->nom) + QStringLiteral(" · ") + plan);
  }
  m_groupeEtat = new QGroupBox(QStringLiteral("État"));
  m_etatEsquisse = new QVBoxLayout(m_groupeEtat);
  col->addWidget(m_groupeEtat);
  m_groupeCotes = new QGroupBox(QStringLiteral("Cotes"));
  auto* colCotes = new QVBoxLayout(m_groupeCotes);
  m_barreContraintes = nullptr;
  m_resumeContraintes = nullptr;
  m_listeCalques = nullptr;
  m_tableCotes = new QTableWidget(0, 2);
  m_tableCotes->setHorizontalHeaderLabels({QStringLiteral("Cote"), QStringLiteral("Valeur")});
  m_tableCotes->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_tableCotes->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  m_tableCotes->setWordWrap(false);
  m_tableCotes->verticalHeader()->hide();
  m_tableCotes->setMinimumHeight(180);
  connect(m_tableCotes, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
    if (it->column() != 1) return;
    const int id = it->data(Qt::UserRole).toInt();
    // Même lecture que l'éditeur de la vue, sans lui prendre le focus : on enchaîne les lignes au clavier ; une
    // valeur refusée est remplacée par celle que la cote garde (EI-30)
    QString erreur;
    if (!m_esq->changerCote(id, it->text(), &erreur)) {
      m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(erreur.toHtmlEscaped()));
      QTimer::singleShot(0, this, [this] { majTableCotes(); });
    }
  });
  m_tableCotes->setMinimumHeight(0);
  colCotes->addWidget(m_tableCotes);
  col->addWidget(m_groupeCotes);
  {
    auto* g = new QGroupBox(QStringLiteral("Affichage"));
    auto* fl = new QFormLayout(g);
    auto* rel = new QComboBox;
    rel->addItems({QStringLiteral("Toutes"), QStringLiteral("Celles de la sélection"), QStringLiteral("Aucune")});
    rel->setCurrentIndex(m_esq->affichageRelations());
    connect(rel, &QComboBox::currentIndexChanged, this, [this](int i) { m_esq->setAffichageRelations(i); });
    fl->addRow(QStringLiteral("Relations"), rel);
    auto* legende = new QLabel(QStringLiteral(
        "<span style='color:#363D5C'>●</span> libre &nbsp; <span style='color:#2E8B57'>●</span> contraint &nbsp; <span style='color:#2F6BF5'>●</span> sélection<br>"
        "<span style='color:#E07B1E'>●</span> aperçu &nbsp; <span style='color:#6E9BFF'>┄</span> construction &nbsp; <span style='color:#C2402E'>●</span> conflit"));
    legende->setStyleSheet(QStringLiteral("color: #4A5270;"));
    fl->addRow(QStringLiteral("Couleurs"), legende);
    col->addWidget(g);
  }
  auto* raccourcis = new QLabel(QStringLiteral("<a href='aide'>Raccourcis clavier (F1)</a>"));
  connect(raccourcis, &QLabel::linkActivated, this, [this] { aideRaccourcis(); });
  col->addWidget(raccourcis);
  col->addStretch(1);
  m_pileEsquisse = new QStackedWidget;
  m_pileEsquisse->addWidget(page);
  m_pileEsquisse->addWidget(new QWidget);
  remplacerPage(m_props, m_pileEsquisse);
  m_props->show();  // panneau de l'esquisse (cotes, contraintes, calques, commandes) : toujours visible pendant l'esquisse
  majTableCotes();
  majCoinValidation();
}

// ---------- Commandes d'esquisse à panneau ----------

void AtelierPiece::ouvrirCommandeEsquisse(const QString& c) {
  if (!m_esquisseEditee || !m_pileEsquisse) return;
  m_commande = c;
  m_esq->setOutil(Esquisseur::Outil::Selection);
  m_esq->setCommandeActive(true);
  m_cmdElements = m_esq->elementsSelectionnes();
  m_cmdAxe = m_esq->ligneOuAxeSelectionne();
  if (c == QLatin1String("symetrie") && m_cmdAxe > 0) m_cmdElements.erase(std::remove(m_cmdElements.begin(), m_cmdElements.end(), m_cmdAxe), m_cmdElements.end());
  m_cmdPoint = m_esq->pointSelectionne();
  m_cmdChamp = m_cmdElements.empty() ? 0 : (c == QLatin1String("symetrie") && m_cmdAxe == 0) ? 1 : 0;
  m_cmdNombre = c == QLatin1String("repetition-circulaire") ? 6 : 3;
  m_cmdValeur = c == QLatin1String("repetition-circulaire") || c == QLatin1String("pivoter") ? (c == QLatin1String("pivoter") ? 90 : 360) : c == QLatin1String("echelle") ? 2 : c == QLatin1String("decaler") ? 2 : 20;
  m_cmdValeur2 = 0;
  m_cmdInverse = false;
  m_cmdCopie = false;
  m_cmdLiberer = false;
  m_cmdDirection = "X";
  majCommandeEsquisse();
}

std::optional<Esquisse> AtelierPiece::resultatCommande(double* ecart) const {
  if (ecart) *ecart = 0;
  if (m_cmdElements.empty() || m_commande == QLatin1String("relations")) return std::nullopt;
  Esquisse e = m_esq->esquisse();
  // Points emportés par la transformation (ceux des éléments choisis)
  std::set<int> bougent;
  for (int id : m_cmdElements)
    if (const ElementEsquisse* el = e.element(id))
      for (int q : {el->p1, el->p2, el->centre}) if (q > 0) bougent.insert(q);
  const QString& c = m_commande;
  Point centre{0, 0};
  if (m_cmdPoint > 0) if (const PointEsquisse* p = e.point(m_cmdPoint)) centre = {p->x, p->y};
  if (c == QLatin1String("symetrie")) {
    if (!m_cmdAxe) return std::nullopt;
    e.symetriser(m_cmdElements, m_cmdAxe);
  } else if (c == QLatin1String("repetition-lineaire")) {
    double dx = 1, dy = 0;
    if (m_cmdDirection == "Y") { dx = 0; dy = 1; }
    else if (m_cmdDirection == "ligne" && m_cmdAxe > 0)
      if (const ElementEsquisse* l = e.element(m_cmdAxe)) {
        const PointEsquisse *a = e.point(l->p1), *b = e.point(l->p2);
        const double n = std::max(1e-12, std::hypot(b->x - a->x, b->y - a->y));
        dx = (b->x - a->x) / n;
        dy = (b->y - a->y) / n;
      }
    const double s = m_cmdInverse ? -m_cmdValeur : m_cmdValeur;
    e.repeterLineaire(m_cmdElements, dx * s, dy * s, m_cmdNombre);
  } else if (c == QLatin1String("repetition-circulaire")) {
    e.repeterCirculaire(m_cmdElements, centre.x, centre.y, m_cmdNombre, m_cmdInverse ? -m_cmdValeur : m_cmdValeur);
  } else if (c == QLatin1String("decaler")) {
    e.decaler(m_cmdElements, m_cmdInverse ? -m_cmdValeur : m_cmdValeur);
  } else if (c == QLatin1String("deplacer")) {
    const double dx = m_cmdValeur, dy = m_cmdValeur2;
    e.transformer(m_cmdElements, [dx, dy](Point p) { return Point{p.x + dx, p.y + dy}; }, 1, m_cmdCopie);
  } else if (c == QLatin1String("pivoter")) {
    const double an = (m_cmdInverse ? -m_cmdValeur : m_cmdValeur) * std::numbers::pi / 180, co = std::cos(an), si = std::sin(an);
    e.transformer(m_cmdElements, [=](Point p) { return Point{centre.x + (p.x - centre.x) * co - (p.y - centre.y) * si, centre.y + (p.x - centre.x) * si + (p.y - centre.y) * co}; }, 1, m_cmdCopie);
  } else if (c == QLatin1String("echelle")) {
    if (m_cmdValeur <= 0) return std::nullopt;  // facteur refusé par le champ (EI-11)
    const double k = m_cmdValeur;
    e.transformer(m_cmdElements, [=](Point p) { return Point{centre.x + (p.x - centre.x) * k, centre.y + (p.y - centre.y) * k}; }, k, m_cmdCopie);
  }
  const bool deplacement = c == QLatin1String("deplacer") || c == QLatin1String("pivoter") || c == QLatin1String("echelle");
  if (deplacement && !m_cmdCopie && m_cmdLiberer) {
    // Relations entre ce qui bouge et ce qui reste (origine, axes, autres éléments) retirées sur demande
    std::set<int> elementsBougent(m_cmdElements.begin(), m_cmdElements.end());
    auto bouge = [&](int r) {
      if (r == 0 || r == AXE_H || r == AXE_V) return false;
      if (bougent.count(r) || elementsBougent.count(r)) return true;
      return false;
    };
    auto reste = [&](int r) {
      if (r == 0 || r == AXE_H || r == AXE_V) return true;
      if (e.element(r)) return !elementsBougent.count(r);
      return e.point(r) != nullptr && !bougent.count(r);
    };
    std::erase_if(e.contraintes, [&](const ContrainteEsquisse& k) {
      if (k.type == TypeContrainte::Fixe) return false;  // déjà transportée par la transformation
      return std::any_of(k.refs.begin(), k.refs.end(), bouge) && std::any_of(k.refs.begin(), k.refs.end(), reste);
    });
  }
  // L'aperçu montre le résultat réel, après résolution : ce que l'on voit est ce que OK appliquera (EI-10)
  const Esquisse brut = e;
  resoudre(e);
  if (ecart && deplacement)
    for (int q : bougent) {
      const PointEsquisse* a = e.point(q);
      const PointEsquisse* b = brut.point(q);
      if (a && b) *ecart = std::max(*ecart, std::hypot(a->x - b->x, a->y - b->y));
    }
  return e;
}

void AtelierPiece::majApercuCommande() {
  double ecart = 0;
  if (const auto r = resultatCommande(&ecart)) m_esq->setApercu(&*r);
  else m_esq->setApercu(nullptr);
  if (m_cmdAlerte) {
    const bool retenu = ecart > 1e-6;
    m_cmdAlerte->setVisible(retenu);
    if (retenu)
      m_cmdAlerte->setText(QStringLiteral("Des relations avec ce qui ne bouge pas (origine, axes, autres éléments) retiennent la sélection : le résultat, montré en aperçu, "
                                          "s’écarte de %1 de la transformation demandée. Coche « Libérer » pour les retirer.")
                               .arg(QString::fromStdString(ecrireValeur(ecart, false, unites()))));
  }
}

void AtelierPiece::fermerCommandeEsquisse(bool valider) {
  if (valider)
    if (const auto r = resultatCommande()) m_esq->appliquer(*r);
  m_commande.clear();
  m_esq->setApercu(nullptr);
  m_esq->setSelectionCumulative(false);
  m_esq->setCommandeActive(false);
  if (m_pileEsquisse) m_pileEsquisse->setCurrentIndex(0);
  majTableCotes();
}

void AtelierPiece::majCommandeEsquisse() {
  if (m_commande.isEmpty() || !m_pileEsquisse) return;
  struct Def { const char* cle; const char* titre; const char* icone; const char* aide; };
  static const Def defs[] = {
      {"symetrie", "Symétrie", "symetrie", "Les éléments sont recopiés de l'autre côté de l'axe ; les copies restent liées aux originaux."},
      {"repetition-lineaire", "Réseau linéaire", "repetition-lineaire", "Copies alignées, à pas constant ; chaque copie reste à son écart de l'original."},
      {"repetition-circulaire", "Réseau circulaire", "repetition-circulaire", "Copies réparties autour d'un centre, sur un tour complet ou un angle donné."},
      {"decaler", "Décaler", "decaler", "Cercles, ou chaîne de lignes et d'arcs : la chaîne décalée se raccorde aux coins."},
      {"deplacer", "Déplacer", "deplacer", "Translation des éléments, ou d'une copie."},
      {"pivoter", "Pivoter", "pivoter", "Rotation autour de l'origine ou d'un point choisi."},
      {"echelle", "Échelle", "echelle", "Agrandissement ou réduction autour d'un point ; les cotes suivent."},
      {"relations", "Relations", "contraintes", "Choisis des éléments dans l'esquisse : les relations possibles s'affichent, et celles qui existent déjà peuvent être retirées."}};
  const Def* d = nullptr;
  for (const auto& x : defs) if (m_commande == QLatin1String(x.cle)) d = &x;
  if (!d) return;
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("panneauProprietes"));
  page->setStyleSheet(QString::fromLatin1(STYLE_PANNEAU));
  auto* col = new QVBoxLayout(page);
  QPushButton* ok = nullptr;
  QPushButton* annuler = nullptr;
  const bool relations = m_commande == QLatin1String("relations");
  enTetePanneau(col, QString::fromLatin1(d->icone), QString::fromUtf8(d->titre), ok, annuler, !relations);
  if (ok) connect(ok, &QPushButton::clicked, this, [this] { fermerCommandeEsquisse(true); });
  connect(annuler, &QPushButton::clicked, this, [this] { fermerCommandeEsquisse(false); });
  auto* aideTexte = new QLabel(QString::fromUtf8(d->aide));
  aideTexte->setWordWrap(true);
  aideTexte->setStyleSheet(QStringLiteral("color: #4A5270;"));
  col->addWidget(aideTexte);
  m_esq->setSelectionCumulative(!relations && m_cmdChamp == 0);
  auto* form = new QFormLayout;
  // Champ de sélection : bouton coloré quand il est actif ; un clic l'active
  auto champ = [this, form](int numero, const QString& libelle, const QString& valeur) {
    auto* b = new QPushButton(icone(QStringLiteral("filtre-auto")), valeur.isEmpty() ? QStringLiteral("Cliquer dans l’esquisse…") : valeur);
    b->setObjectName(m_cmdChamp == numero ? QStringLiteral("champActif") : QStringLiteral("champ"));
    connect(b, &QPushButton::clicked, this, [this, numero] {
      m_cmdChamp = numero;
      if (numero == 0) m_esq->selectionnerElements(m_cmdElements);
      majCommandeEsquisse();
    });
    form->addRow(libelle, b);
  };
  auto valeur = [this, form](const QString& libelle, double v, bool angle, std::function<void(double)> f) {
    auto* ch = new ChampValeur(angle, unites());
    ch->setBornes(-1e6, 1e6);
    ch->setValeur(v);
    connect(ch, &ChampValeur::valeurChangee, this, [this, f](double x) { f(x); majApercuCommande(); });
    form->addRow(libelle, ch);
  };
  auto coche = [this, form](const QString& t, bool v, std::function<void(bool)> f) {
    auto* c = new QCheckBox(t);
    c->setChecked(v);
    connect(c, &QCheckBox::toggled, this, [this, f](bool on) { f(on); majApercuCommande(); });
    form->addRow(QString(), c);
  };
  if (m_commande == QLatin1String("relations")) {
    // Relations possibles pour la sélection, puis relations existantes avec leur bouton de retrait
    const auto ids = m_esq->elementsSelectionnes();
    col->addWidget(new QLabel(QStringLiteral("<b>%1 élément(s) sélectionné(s)</b>").arg(ids.size())));
    auto* grille = new QGridLayout;
    const struct { const char* icone; const char* t; TypeContrainte type; } rel[] = {
        {"horizontal", "Horizontale", TypeContrainte::Horizontal}, {"vertical", "Verticale", TypeContrainte::Vertical}, {"coincident", "Coïncidente", TypeContrainte::Coincidence},
        {"contraintes", "Parallèle", TypeContrainte::Parallele}, {"contraintes", "Perpendiculaire", TypeContrainte::Perpendiculaire}, {"contraintes", "Égale", TypeContrainte::Egal},
        {"arc-tangent", "Tangente", TypeContrainte::Tangente}, {"cercle", "Concentrique", TypeContrainte::Concentrique}, {"ligne", "Colinéaire", TypeContrainte::Colineaire},
        {"ligne-milieu", "Milieu", TypeContrainte::Milieu}, {"symetrie", "Symétrique", TypeContrainte::Symetrie}, {"point", "Fixe", TypeContrainte::Fixe}};
    int k = 0;
    for (const auto& r : rel) {
      auto* b = new QPushButton(icone(QString::fromLatin1(r.icone)), QString::fromUtf8(r.t));
      const TypeContrainte t = r.type;
      connect(b, &QPushButton::clicked, this, [this, t] { m_esq->contraindre(t); majCommandeEsquisse(); });
      grille->addWidget(b, k / 2, k % 2);
      ++k;
    }
    col->addLayout(grille);
    col->addWidget(new QLabel(QStringLiteral("<b>Relations et cotes existantes</b>")));
    const auto existantes = m_esq->contraintesDe(ids);
    if (existantes.empty()) col->addWidget(new QLabel(QStringLiteral("Aucune sur la sélection.")));
    for (int cid : existantes) {
      auto* ligne = new QHBoxLayout;
      ligne->addWidget(new QLabel(m_esq->nomContrainteLisible(cid)), 1);
      auto* x = new QToolButton;
      x->setIcon(icone(QStringLiteral("supprimer")));
      x->setToolTip(QStringLiteral("Retirer"));
      connect(x, &QToolButton::clicked, this, [this, cid] { m_esq->supprimerContrainte(cid); majCommandeEsquisse(); });
      ligne->addWidget(x);
      col->addLayout(ligne);
    }
  } else {
    {
      auto* g = new QGroupBox(QStringLiteral("Éléments (%1)").arg(m_cmdElements.size()));
      auto* gl = new QVBoxLayout(g);
      auto* liste = new QListWidget;
      liste->setObjectName(m_cmdChamp == 0 ? QString() : QStringLiteral("inactive"));
      liste->setMaximumHeight(118);
      for (int id : m_cmdElements) {
        const ElementEsquisse* el = m_esq->esquisse().element(id);
        if (!el) continue;
        auto* it = new QListWidgetItem(icone(el->type == TypeElement::Ligne ? QStringLiteral("ligne") : el->type == TypeElement::Cercle ? QStringLiteral("cercle") : QStringLiteral("arc")),
                                       QStringLiteral("%1 %2").arg(el->type == TypeElement::Ligne ? QStringLiteral("Ligne") : el->type == TypeElement::Cercle ? QStringLiteral("Cercle") : QStringLiteral("Arc")).arg(id));
        it->setData(Qt::UserRole, id);
        liste->addItem(it);
      }
      if (m_cmdElements.empty()) liste->addItem(QStringLiteral("Clique les éléments dans l’esquisse…"));
      connect(liste, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const int id = it->data(Qt::UserRole).toInt();
        m_cmdElements.erase(std::remove(m_cmdElements.begin(), m_cmdElements.end(), id), m_cmdElements.end());
        m_esq->selectionnerElements(m_cmdElements);
        majCommandeEsquisse();
      });
      liste->viewport()->installEventFilter(this);
      liste->setProperty("champCommande", 0);
      gl->addWidget(liste);
      auto* ligne = new QHBoxLayout;
      auto* activer = new QPushButton(m_cmdChamp == 0 ? QStringLiteral("Les clics ajoutent ou retirent") : QStringLiteral("Choisir les éléments"));
      activer->setObjectName(m_cmdChamp == 0 ? QStringLiteral("champActif") : QStringLiteral("champ"));
      connect(activer, &QPushButton::clicked, this, [this] { m_cmdChamp = 0; m_esq->selectionnerElements(m_cmdElements); majCommandeEsquisse(); });
      auto* vider = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Vider"));
      vider->setObjectName(QStringLiteral("champ"));
      connect(vider, &QPushButton::clicked, this, [this] { m_cmdElements.clear(); m_esq->selectionnerElements({}); majCommandeEsquisse(); });
      ligne->addWidget(activer, 1);
      ligne->addWidget(vider);
      gl->addLayout(ligne);
      col->addWidget(g);
    }
    if (m_commande == QLatin1String("symetrie"))
      champ(1, QStringLiteral("Axe de symétrie"), m_cmdAxe == AXE_H ? QStringLiteral("axe horizontal") : m_cmdAxe == AXE_V ? QStringLiteral("axe vertical") : m_cmdAxe > 0 ? QStringLiteral("ligne %1").arg(m_cmdAxe) : QString());
    if (m_commande == QLatin1String("repetition-lineaire")) {
      auto* dir = new QComboBox;
      dir->addItem(QStringLiteral("Selon X"), QStringLiteral("X"));
      dir->addItem(QStringLiteral("Selon Y"), QStringLiteral("Y"));
      dir->addItem(QStringLiteral("Le long d’une ligne"), QStringLiteral("ligne"));
      dir->setCurrentIndex(std::max(0, dir->findData(qs(m_cmdDirection))));
      connect(dir, &QComboBox::activated, this, [this, dir] { m_cmdDirection = dir->currentData().toString().toStdString(); if (m_cmdDirection == "ligne") m_cmdChamp = 1; majCommandeEsquisse(); });
      form->addRow(QStringLiteral("Direction"), dir);
      if (m_cmdDirection == "ligne") champ(1, QStringLiteral("Ligne de direction"), m_cmdAxe > 0 ? QStringLiteral("ligne %1").arg(m_cmdAxe) : QString());
    }
    if (m_commande == QLatin1String("repetition-circulaire") || m_commande == QLatin1String("pivoter") || m_commande == QLatin1String("echelle"))
      champ(1, QStringLiteral("Centre"), m_cmdPoint > 0 ? QStringLiteral("point %1").arg(m_cmdPoint) : QStringLiteral("origine"));
    if (m_commande.startsWith(QLatin1String("repetition"))) {
      auto* n = new QSpinBox;
      n->setRange(2, 1000);
      n->setValue(m_cmdNombre);
      connect(n, &QSpinBox::valueChanged, this, [this](int v) { m_cmdNombre = v; majApercuCommande(); });
      form->addRow(QStringLiteral("Nombre d’exemplaires"), n);
    }
    if (m_commande == QLatin1String("repetition-lineaire")) valeur(QStringLiteral("Pas"), m_cmdValeur, false, [this](double x) { m_cmdValeur = x; });
    if (m_commande == QLatin1String("repetition-circulaire")) valeur(QStringLiteral("Angle total"), m_cmdValeur, true, [this](double x) { m_cmdValeur = x; });
    if (m_commande == QLatin1String("decaler")) valeur(QStringLiteral("Distance"), m_cmdValeur, false, [this](double x) { m_cmdValeur = x; });
    if (m_commande == QLatin1String("deplacer")) {
      valeur(QStringLiteral("Décalage horizontal"), m_cmdValeur, false, [this](double x) { m_cmdValeur = x; });
      valeur(QStringLiteral("Décalage vertical"), m_cmdValeur2, false, [this](double x) { m_cmdValeur2 = x; });
    }
    if (m_commande == QLatin1String("pivoter")) valeur(QStringLiteral("Angle"), m_cmdValeur, true, [this](double x) { m_cmdValeur = x; });
    if (m_commande == QLatin1String("echelle")) {
      // Facteur sans unité, strictement positif (il n'est pas une longueur, EI-11)
      auto* k = new ChampNombre;
      k->setRange(0.0001, 10000);
      k->setDecimals(4);
      k->setValue(m_cmdValeur > 0 ? m_cmdValeur : 2);
      m_cmdValeur = k->value();
      connect(k, &QDoubleSpinBox::valueChanged, this, [this](double x) { m_cmdValeur = x; majApercuCommande(); });
      form->addRow(QStringLiteral("Facteur"), k);
    }
    if (m_commande == QLatin1String("decaler") || m_commande.startsWith(QLatin1String("repetition")) || m_commande == QLatin1String("pivoter"))
      coche(m_commande == QLatin1String("decaler") ? QStringLiteral("Vers l’intérieur") : QStringLiteral("Inverser le sens"), m_cmdInverse, [this](bool on) { m_cmdInverse = on; });
    if (m_commande == QLatin1String("deplacer") || m_commande == QLatin1String("pivoter") || m_commande == QLatin1String("echelle")) {
      coche(QStringLiteral("Garder l’original (copier)"), m_cmdCopie, [this](bool on) { m_cmdCopie = on; });
      coche(QStringLiteral("Libérer : retirer les relations avec ce qui ne bouge pas"), m_cmdLiberer, [this](bool on) { m_cmdLiberer = on; });
    }
    auto* reglages = new QGroupBox(QStringLiteral("Réglages"));
    reglages->setLayout(form);
    col->addWidget(reglages);
    m_cmdAlerte = new QLabel;
    m_cmdAlerte->setWordWrap(true);
    m_cmdAlerte->setStyleSheet(QStringLiteral("color: #B4652B;"));
    m_cmdAlerte->hide();
    col->addWidget(m_cmdAlerte);
    auto* consigne = new QLabel(m_cmdChamp == 0 ? QStringLiteral("Chaque clic dans l’esquisse ajoute ou retire un élément ; un cadre en prend plusieurs. Double-clic dans la liste pour retirer.")
                                 : m_commande == QLatin1String("symetrie") || m_cmdDirection == "ligne" ? QStringLiteral("Clique la ligne ou l’axe voulu dans l’esquisse.")
                                                                                                          : QStringLiteral("Clique le point voulu dans l’esquisse."));
    consigne->setWordWrap(true);
    consigne->setObjectName(QStringLiteral("consigne"));
    col->addWidget(consigne);
  }
  col->addStretch(1);
  QWidget* ancien = m_pileEsquisse->widget(1);
  m_pileEsquisse->insertWidget(1, page);
  m_pileEsquisse->removeWidget(ancien);
  ancien->deleteLater();
  m_pileEsquisse->setCurrentIndex(1);
  majApercuCommande();
}

void AtelierPiece::majTableCotes() {
  if (!m_tableCotes || !m_esquisseEditee) return;
  const QSignalBlocker b(m_tableCotes);
  const auto ids = m_esq->cotes();
  m_tableCotes->setRowCount(static_cast<int>(ids.size()));
  const Unites u = unites();
  int ligne = 0;
  // Cotes numérotées par type dans l'ordre d'affichage (« Longueur 1 », « Longueur 2 », « Rayon 1 »), et non par
  // leur identifiant interne (AP-38) ; l'expression d'une cote qui suit une variable est montrée
  std::map<TypeContrainte, int> rangs;
  for (int id : ids) {
    const ContrainteEsquisse* c = nullptr;
    for (const auto& k : m_esq->esquisse().contraintes) if (k.id == id) c = &k;
    if (!c) continue;
    const bool angle = c->type == TypeContrainte::Angle;
    QString libelle = QString::fromUtf8(nomContrainte(c->type)) + QStringLiteral(" %1").arg(++rangs[c->type]);
    if (c->reference) libelle += QStringLiteral(" (réf.)");
    auto* nom = new QTableWidgetItem(libelle);
    nom->setFlags(nom->flags() & ~Qt::ItemIsEditable);
    nom->setIcon(icone(QStringLiteral("cote")));
    const double v = c->reference ? mesurer(m_esq->esquisse(), *c) : c->valeur;
    auto* val = new QTableWidgetItem(c->expression.empty() || c->reference ? QString::fromStdString(ecrireValeur(std::abs(v), angle, u)) : qs(c->expression));
    if (!c->expression.empty() && !c->reference) val->setToolTip(QStringLiteral("= %1").arg(QString::fromStdString(ecrireValeur(std::abs(v), angle, u))));
    val->setData(Qt::UserRole, id);
    if (c->reference) val->setFlags(val->flags() & ~Qt::ItemIsEditable);
    m_tableCotes->setItem(ligne, 0, nom);
    m_tableCotes->setItem(ligne, 1, val);
    ++ligne;
  }
  m_tableCotes->setRowCount(ligne);
  // Bilan des contraintes
  if (m_barreContraintes && m_resumeContraintes) {
    Esquisse copie = m_esq->esquisse();
    const EtatEsquisse etat = resoudre(copie);
    const int variables = static_cast<int>(2 * (copie.points.size() - 1));
    int rayons = 0;
    for (const auto& el : copie.elements) if (el.type == TypeElement::Cercle) ++rayons;
    const int total = std::max(1, variables + rayons);
    m_barreContraintes->setValue(std::clamp(100 - 100 * etat.degresLiberte / total, 0, 100));
    m_barreContraintes->setStyleSheet(etat.degresLiberte == 0 ? QStringLiteral("QProgressBar::chunk { background: #2E8B57; }") : QStringLiteral("QProgressBar::chunk { background: #6E9BFF; }"));
    std::map<std::string, int> compte;
    for (const auto& k : copie.contraintes) ++compte[nomContrainte(k.type)];
    QStringList morceaux;
    for (const auto& [n, c] : compte) morceaux << QStringLiteral("%1 %2").arg(QString::fromUtf8(n.c_str())).arg(c);
    m_resumeContraintes->setText(QStringLiteral("<span style='color:%1'><b>%2</b></span><br><span style='color:#4A5270'>%3</span>")
                                     .arg(etat.degresLiberte == 0 ? QStringLiteral("#2E8B57") : QStringLiteral("#363D5C"),
                                          etat.degresLiberte == 0 ? QStringLiteral("Entièrement contrainte") : QStringLiteral("%1 degré(s) de liberté restant(s)").arg(etat.degresLiberte),
                                          morceaux.isEmpty() ? QStringLiteral("Aucune contrainte") : morceaux.join(QStringLiteral(" · "))));
  }
  majCalques();
  majDiagnostic();
  if (m_groupeCotes) { m_groupeCotes->setVisible(m_tableCotes->rowCount() > 0); m_groupeCotes->setTitle(QStringLiteral("Cotes (%1)").arg(m_tableCotes->rowCount())); }
}

void AtelierPiece::aideRaccourcis() {
  QMessageBox::information(this, QStringLiteral("Raccourcis clavier"), QStringLiteral(
      "<h3>Partout</h3><p><b>Ctrl+K</b> rechercher une commande · <b>Ctrl+Z / Ctrl+Y</b> annuler, rétablir · <b>Ctrl+S</b> enregistrer · <b>F1</b> cette aide</p>"
      "<h3>Vue 3D</h3><p><b>F</b> recadrer · <b>1 2 3 0</b> face, dessus, droite, isométrique · <b>E</b> extrusion · <b>X</b> enlèvement · <b>R</b> révolution · "
      "<b>M</b> mesurer · <b>Espace</b> montrer ou masquer l’esquisse choisie · <b>Échap</b> désélectionner</p>"
      "<h3>Esquisse</h3><p><b>L</b> ligne · <b>R</b> rectangle · <b>C</b> cercle · <b>A</b> arc · <b>O</b> rainure · <b>P</b> point · <b>J</b> congé · <b>D</b> cote · "
      "<b>H / V</b> horizontale, verticale · <b>X</b> mode construction · <b>Suppr</b> supprimer · <b>Espace</b> commande rapide (« c 50 », « r 40 30 », « ? ») · "
      "clic droit maintenu : menu radial · <b>Ctrl+Entrée</b> terminer</p>"
      "<h3>Souris</h3><p>Partout : molette = zoom · bouton du milieu glissé = rotation · clic droit glissé = déplacement. Dans la vue 3D, le clic gauche "
      "glissé fait aussi tourner la vue ; dans l’esquisse, il trace ou déplace. Double-clic sur une cote : la modifier.</p>"));
}

void AtelierPiece::majDiagnostic() {
  if (!m_etatEsquisse || !m_esquisseEditee) return;
  QLayoutItem* it;
  while ((it = m_etatEsquisse->takeAt(0)) != nullptr) { if (it->widget()) it->widget()->deleteLater(); delete it; }
  Esquisse copie = m_esq->esquisse();
  const EtatEsquisse etat = resoudre(copie);
  const DiagnosticEsquisse d = diagnostiquer(copie);
  auto ligne = [this](const QString& html) { auto* l = new QLabel(html); l->setWordWrap(true); m_etatEsquisse->addWidget(l); return l; };
  if (!etat.enConflit.empty()) ligne(QStringLiteral("<span style='color:#C2402E; font-weight:700'>✖ Contraintes en conflit</span><br>Retire l’une des contraintes en rouge."));
  else if (d.degresLiberte == 0) ligne(QStringLiteral("<span style='color:#2E8B57; font-weight:700'>✓ Entièrement contrainte</span>"));
  else ligne(QStringLiteral("<span style='color:#1B2F7A; font-weight:700'>Partiellement contrainte — %1 degré%2 de liberté</span>").arg(d.degresLiberte).arg(d.degresLiberte > 1 ? "s" : ""));
  if (!d.libertes.empty() && etat.enConflit.empty()) {
    auto* conseil = new QLabel;
    conseil->setWordWrap(true);
    conseil->setObjectName(QStringLiteral("consigne"));
    conseil->hide();
    std::vector<int> tousE, tousP;
    for (const auto& l : d.libertes) {
      auto* b = new QPushButton(QStringLiteral("↔ %1").arg(qs(l.description)));
      b->setObjectName(QStringLiteral("champ"));
      b->setToolTip(qs(l.suggestion));
      const auto el = l.elements;
      const auto pt = l.points;
      const QString sugg = qs(l.suggestion);
      connect(b, &QPushButton::clicked, this, [this, el, pt, sugg, conseil] { m_esq->setSurbrillance(el, pt); conseil->setText(sugg); conseil->show(); });
      m_etatEsquisse->addWidget(b);
      tousE.insert(tousE.end(), l.elements.begin(), l.elements.end());
      tousP.insert(tousP.end(), l.points.begin(), l.points.end());
    }
    m_etatEsquisse->addWidget(conseil);
    auto* tout = new QPushButton(icone(QStringLiteral("afficher")), QStringLiteral("Montrer tout ce qui peut bouger"));
    tout->setObjectName(QStringLiteral("champ"));
    connect(tout, &QPushButton::clicked, this, [this, tousE, tousP] { m_esq->setSurbrillance(tousE, tousP); });
    m_etatEsquisse->addWidget(tout);
  }
  if (!d.extremitesLibres.empty()) {
    ligne(QStringLiteral("<span style='color:#C77A1E; font-weight:700'>⚠ Profil ouvert — %1 extrémité%2 libre%2</span><br>Une extrusion pleine demande un contour fermé.").arg(d.extremitesLibres.size()).arg(d.extremitesLibres.size() > 1 ? "s" : ""));
    auto* montrer = new QPushButton(icone(QStringLiteral("afficher")), QStringLiteral("Afficher les extrémités"));
    montrer->setObjectName(QStringLiteral("champ"));
    const auto pts = d.extremitesLibres;
    connect(montrer, &QPushButton::clicked, this, [this, pts] { m_esq->setSurbrillance({}, pts); });
    m_etatEsquisse->addWidget(montrer);
  } else if (!copie.elements.empty()) {
    ligne(QStringLiteral("<span style='color:#2E8B57'>✓ Aucune extrémité libre</span>"));
  }
}

void AtelierPiece::majCalques() {
  if (!m_listeCalques || !m_esquisseEditee) return;
  const QSignalBlocker b(m_listeCalques);
  m_listeCalques->clear();
  auto pastille = [](const QColor& c) {
    QPixmap p(14, 14);
    p.fill(Qt::transparent);
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing);
    g.setBrush(c);
    g.setPen(c.darker(130));
    g.drawRoundedRect(QRectF(1, 1, 12, 12), 3, 3);
    return QIcon(p);
  };
  auto* defaut = new QListWidgetItem(pastille(theme().texte(QColor(0x36, 0x3D, 0x5C))), QStringLiteral("Calque par défaut"));
  defaut->setData(Qt::UserRole, QString());
  m_listeCalques->addItem(defaut);
  int actif = 0;
  for (const auto& k : m_esq->esquisse().calques) {
    auto* it = new QListWidgetItem(pastille(QColor(QString::fromStdString(k.couleur))), QString::fromStdString(k.nom));
    it->setData(Qt::UserRole, QString::fromStdString(k.nom));
    it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
    it->setCheckState(k.visible ? Qt::Checked : Qt::Unchecked);
    m_listeCalques->addItem(it);
    if (k.nom == m_esq->calqueActif()) actif = m_listeCalques->count() - 1;
  }
  m_listeCalques->setCurrentRow(actif);
}

// Convertir les entités : les arêtes de la pièce passent dans l'esquisse, fixées (contour de la face d'appui ;
// sur un plan de base, les arêtes de la pièce posées dans ce plan).
// Arêtes de la pièce ramenées dans le plan de l'esquisse : celles de la face d'appui et celles posées dans le plan ;
// sinon (esquisse sur un plan au-dessus ou au-dessous de la pièce) le contour projeté de toutes les arêtes.
// Géométrie de la pièce entière : ces copies sont fixes, elles ne créent pas de dépendance.
// Arêtes de la pièce proposées dans l'esquisse éditée (S3) : celles du corps tel qu'il est avant l'esquisse (pas de
// référence vers une fonction qui vient après), dans le plan de l'esquisse avec leur nom (reprises associatives) ;
// à défaut, les contours vus de la pièce projetés sur le plan (lignes cachées exclues), sans limite de nombre.
Esquisse AtelierPiece::geometrieExterne(std::vector<std::string>* noms) const {
  Esquisse ext;
  if (noms) noms->clear();
  const FonctionPiece* f = m_piece.fonction(m_esquisseEditee);
  if (!f) return ext;
  const gp_Ax3 ax = m_res.repere(m_esquisseEditee);
  // Corps avant l'esquisse : la fonction qui la précède dans l'arbre
  int precedente = 0;
  for (const auto& g : m_piece.fonctions) { if (g.id == m_esquisseEditee) break; precedente = g.id; }
  const ResultatPiece avant = precedente > 0 ? construirePiece(m_piece, precedente) : ResultatPiece();
  const TopoDS_Shape forme = avant.forme;
  if (forme.IsNull()) return ext;
  const gp_Pln plan(ax);
  TopTools_IndexedMapOfShape toutes;
  TopExp::MapShapes(forme, TopAbs_EDGE, toutes);
  std::vector<TopoDS_Edge> aretes;
  for (int i = 1; i <= toutes.Extent(); ++i) {
    const TopoDS_Edge ed = TopoDS::Edge(toutes(i));
    if (BRep_Tool::Degenerated(ed)) continue;
    BRepAdaptor_Curve c(ed);
    bool dans = true;
    for (double t : {0.0, 0.25, 0.5, 0.75, 1.0})
      if (plan.Distance(c.Value(c.FirstParameter() + t * (c.LastParameter() - c.FirstParameter()))) > 1e-6) dans = false;
    if (dans) aretes.push_back(ed);
  }
  const gp_Pnt O = ax.Location();
  const gp_Vec X(ax.XDirection()), Y(ax.YDirection()), N(ax.Direction());
  auto uv = [&](const gp_Pnt& p) { const gp_Vec w(O, p); return std::pair<double, double>{w.Dot(X), w.Dot(Y)}; };
  auto point = [&](double u, double v) {
    for (const auto& p : ext.points) if (std::hypot(p.x - u, p.y - v) < 1e-6) return p.id;
    return ext.ajouterPoint(u, v);
  };
  // Nom de l'arête d'où vient le dernier élément ajouté (vide : reprise sans lien)
  auto nommer = [&](const std::string& n) {
    if (!noms) return;
    noms->resize(ext.elements.size());
    noms->back() = n;
  };
  if (aretes.empty()) {
    // Contours vus projetés sur le plan (HLR) : polylignes reconnues en droites, cercles ou arcs
    const std::vector<Trait2D> traits = projeter(forme, gp_Ax2(O, ax.Direction(), ax.XDirection()), false, false, false);
    for (const auto& t : traits) {
      if (t.type != TypeTrait::Vu || t.points.size() < 2) continue;
      const auto& pts = t.points;
      // Origine de la projection : coordonnées dans le repère de la vue, qui est celui de l'esquisse
      auto P = [&](std::size_t k) { return pts[k]; };
      if (pts.size() == 2) {
        const int a = point(P(0).first, P(0).second), b = point(P(1).first, P(1).second);
        if (a != b) { ext.ajouterLigne(a, b); nommer({}); }
        continue;
      }
      // Cercle passant par le premier, le milieu et le dernier point : tous les points dessus ?
      const auto [x1, y1] = P(0);
      const auto [x2, y2] = P(pts.size() / 2);
      const auto [x3, y3] = P(pts.size() - 1);
      const bool ferme = std::hypot(x3 - x1, y3 - y1) < 1e-6;
      const auto [xb, yb] = ferme ? P(pts.size() / 4) : P(pts.size() / 2);
      const auto [xc, yc] = ferme ? P(pts.size() / 2) : P(pts.size() - 1);
      const double d = 2 * (x1 * (yb - yc) + xb * (yc - y1) + xc * (y1 - yb));
      bool cercle = std::abs(d) > 1e-12;
      double cx = 0, cy = 0, rr = 0;
      if (cercle) {
        cx = ((x1 * x1 + y1 * y1) * (yb - yc) + (xb * xb + yb * yb) * (yc - y1) + (xc * xc + yc * yc) * (y1 - yb)) / d;
        cy = ((x1 * x1 + y1 * y1) * (xc - xb) + (xb * xb + yb * yb) * (x1 - xc) + (xc * xc + yc * yc) * (xb - x1)) / d;
        rr = std::hypot(x1 - cx, y1 - cy);
        for (const auto& [x, y] : pts) cercle = cercle && std::abs(std::hypot(x - cx, y - cy) - rr) < 1e-3 * std::max(1.0, rr);
      }
      (void)x2;
      (void)y2;
      if (cercle && ferme) { ext.ajouterCercle(point(cx, cy), rr); nommer({}); continue; }
      if (cercle) {
        // Sens direct : le milieu du tracé est à gauche de la corde début → fin
        const bool direct = (xc - x1) * (y2 - y1) - (yc - y1) * (x2 - x1) < 0;
        const int a = point(x1, y1), b = point(x3, y3);
        ext.ajouterArc(point(cx, cy), direct ? a : b, direct ? b : a);
        nommer({});
        continue;
      }
      int prec = -1;
      for (const auto& [x, y] : pts) {
        const int q = point(x, y);
        if (prec >= 0 && prec != q) { ext.ajouterLigne(prec, q); nommer({}); }
        prec = q;
      }
    }
    return ext;
  }
  for (const auto& ed : aretes) {
    const std::string nom = avant.nomArete(ed);
    BRepAdaptor_Curve c(ed);
    const double t0 = c.FirstParameter(), t1 = c.LastParameter();
    gp_Pnt a0 = c.Value(t0), b0 = c.Value(t1);
    if (c.GetType() == GeomAbs_Line) {
      const auto [ua, va] = uv(a0);
      const auto [ub, vb] = uv(b0);
      if (std::hypot(ua - ub, va - vb) < 1e-6) continue;
      const int pa = point(ua, va), pb = point(ub, vb);
      bool deja = false;
      for (const auto& el : ext.elements) if (el.type == TypeElement::Ligne && ((el.p1 == pa && el.p2 == pb) || (el.p1 == pb && el.p2 == pa))) deja = true;
      if (!deja) { ext.ajouterLigne(pa, pb); nommer(nom); }
    } else if (c.GetType() == GeomAbs_Circle && std::abs(gp_Vec(c.Circle().Axis().Direction()).Dot(N)) > 0.999) {
      const gp_Circ ci = c.Circle();
      const auto [uc, vc] = uv(ci.Location());
      const int centre = point(uc, vc);
      if (std::abs(t1 - t0 - 2 * std::numbers::pi) < 1e-6) {
        bool deja = false;
        for (const auto& el : ext.elements) if (el.type == TypeElement::Cercle && el.centre == centre && std::abs(el.rayon - ci.Radius()) < 1e-6) deja = true;
        if (!deja) { ext.ajouterCercle(centre, ci.Radius()); nommer(nom); }
      } else {
        gp_Pnt dd = a0, fin = b0;
        if (ci.Axis().Direction().Dot(ax.Direction()) < 0) std::swap(dd, fin);
        const auto [ud, vd] = uv(dd);
        const auto [uf, vf] = uv(fin);
        ext.ajouterArc(centre, point(ud, vd), point(uf, vf));
        nommer(nom);
      }
    } else {
      // Courbe quelconque dans le plan : suite de segments (reprise fixe, sans lien)
      int prec = -1;
      for (int k = 0; k <= 24; ++k) {
        const auto [u, v] = uv(c.Value(t0 + (t1 - t0) * k / 24));
        const int q = point(u, v);
        if (prec >= 0 && prec != q) { ext.ajouterLigne(prec, q); nommer({}); }
        prec = q;
      }
    }
  }
  return ext;
}

// Insérer un fichier de CAO (M12) : corps importé, nommé et coloré comme dans le fichier. Un assemblage est posé à
// plat (ses instances à leur place) ; l'atelier d'assemblage, lui, en garde la structure.
bool AtelierPiece::insererFichierCAO(const QString& cheminDonne) {
  if (m_gestion) { m_etat->setText(QStringLiteral("Termine d’abord la fonction en cours (OK ou Annuler).")); return false; }
  if (!peutLancer()) return false;
  QString chemin = cheminDonne;
  if (chemin.isEmpty())
    chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Insérer un fichier CAO"), QString(), QStringLiteral("CAO (*.step *.stp *.igs *.iges *.brep *.brp *.stl)"));
  if (chemin.isEmpty()) return false;
  ImportCAO imp;
  try {
    imp = lireCAOStructure(chemin.toStdString());
  } catch (const std::exception& e) {
    const QString m = QStringLiteral("« %1 » : %2").arg(QFileInfo(chemin).fileName(), QString::fromUtf8(e.what()));
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(m.toHtmlEscaped()));
    if (cheminDonne.isEmpty()) QMessageBox::warning(this, QStringLiteral("Insérer un fichier CAO"), m);
    return false;
  }
  const ProduitImporte pr = imp.aplati(imp.produits.size() == 1 ? imp.produits.front().nom : imp.racine.nom);
  const bool premiere = std::none_of(m_piece.fonctions.begin(), m_piece.fonctions.end(), [](const FonctionPiece& g) { return g.type != TypeFonction::Esquisse; });
  FonctionPiece f;
  f.type = TypeFonction::Import;
  f.nom = pr.nom;
  f.donnees = deposerGeometrie(brepDepuisForme(pr.forme));
  f.source = QFileInfo(chemin).fileName().toStdString();
  const int id = m_piece.ajouter(f);
  // Couleurs : un premier corps donne sa couleur à la pièce ; un corps ajouté à une pièce déjà colorée garde la
  // sienne face par face
  const bool pieceGrise = m_piece.couleur == Piece{}.couleur;
  if (!pr.couleur.empty() && (premiere || pieceGrise)) m_piece.couleur = pr.couleur;
  int k = 0;
  for (TopExp_Explorer ex(pr.forme, TopAbs_FACE); ex.More(); ex.Next(), ++k) {
    std::string c;
    if (const auto it = pr.couleursFaces.find(k); it != pr.couleursFaces.end()) c = it->second;
    else if (!pr.couleur.empty() && pr.couleur != m_piece.couleur) c = pr.couleur;
    if (!c.empty() && c != m_piece.couleur) m_piece.couleursFaces[std::to_string(id) + ":import." + std::to_string(k)] = c;
  }
  // Pièce encore vierge au nom par défaut (« Pièce », « Pièce 6 ») : elle prend le nom du fichier
  if (premiere && !pr.nom.empty() && QRegularExpression(QStringLiteral("^(Pièce( \\d+)?)?$")).match(qs(m_piece.nom)).hasMatch()) m_piece.nom = pr.nom;
  enregistrer();
  reconstruire(true);
  QString m = QStringLiteral("« %1 » inséré : %2 solide(s), %3 face(s) colorée(s).").arg(qs(pr.nom).toHtmlEscaped()).arg(nombreSolides(pr.forme)).arg(pr.couleursFaces.size());
  if (imp.estAssemblage())
    m += QStringLiteral(" Assemblage de %1 instance(s) posé à plat ; inséré dans un assemblage, il garderait une pièce par produit.").arg(imp.instances());
  m_etat->setText(m);
  return true;
}

void AtelierPiece::colorerSelection(const QString& couleur) {
  if (m_gestion || m_esquisseEditee) return;
  int n = 0;
  for (const auto& s : m_vue->selection()) {
    if (s.ShapeType() != TopAbs_FACE) continue;
    const std::string nom = m_res.nomFace(TopoDS::Face(s));
    if (nom.empty()) continue;
    ++n;
    if (couleur.isEmpty()) {
      m_piece.couleursFaces.erase(nom);
      m_piece.couleursFaces.erase(nom.substr(0, nom.find('#')));  // la face entière, si elle était colorée en entier
    } else {
      m_piece.couleursFaces[nom] = couleur.toUpper().toStdString();
    }
  }
  if (!n) { m_etat->setText(QStringLiteral("Choisis d’abord une ou plusieurs faces dans la vue.")); return; }
  enregistrer();
  reconstruire(false);
  m_etat->setText(couleur.isEmpty() ? QStringLiteral("%1 face(s) reprennent la couleur de la pièce.").arg(n)
                                    : QStringLiteral("%1 face(s) en %2.").arg(n).arg(couleur.toUpper()));
}

void AtelierPiece::choisirCouleurFaces() {
  QString initiale = qs(m_piece.couleur);
  const std::vector<TopoDS_Shape> sel = m_vue->selection();
  for (const auto& s : sel)
    if (s.ShapeType() == TopAbs_FACE)
      if (const auto it = m_piece.couleursFaces.find(m_res.nomFace(TopoDS::Face(s))); it != m_piece.couleursFaces.end()) { initiale = qs(it->second); break; }
  if (std::none_of(sel.begin(), sel.end(), [](const TopoDS_Shape& x) { return x.ShapeType() == TopAbs_FACE; })) {
    m_etat->setText(QStringLiteral("Choisis d’abord une ou plusieurs faces dans la vue."));
    return;
  }
  const QColor c = QColorDialog::getColor(QColor(initiale), this, QStringLiteral("Couleur des faces"));
  if (c.isValid()) colorerSelection(c.name(QColor::HexRgb));
}

// Insérer un DXF (M1) : le fichier est lu, la fenêtre règle unité, placement, calques et relations, l'insertion
// fait un seul pas d'annulation de l'esquisse et laisse les éléments insérés sélectionnés.
bool AtelierPiece::insererDxf(const QString& cheminDonne, const prom::dxf::OptionsImportDxf* optionsDonnees) {
  if (!m_esquisseEditee) return false;
  QString chemin = cheminDonne;
  if (chemin.isEmpty())
    chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Insérer un DXF dans l’esquisse"), QString(), QStringLiteral("Dessins DXF (*.dxf *.DXF)"));
  if (chemin.isEmpty()) return false;
  QFile f(chemin);
  if (!f.open(QIODevice::ReadOnly)) {
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'>Impossible de lire « %1 ».</span>").arg(QFileInfo(chemin).fileName().toHtmlEscaped()));
    return false;
  }
  prom::dxf::DessinDxf d = prom::dxf::lireDessinDxf(f.readAll().toStdString());
  if (d.traits.empty()) {
    QString raison = d.alertes.empty() ? QStringLiteral("rien à reprendre.") : QString::fromStdString(d.alertes.front());
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'><b>DXF non inséré</b> : %1</span>").arg(raison.toHtmlEscaped()));
    if (!optionsDonnees) QMessageBox::warning(this, QStringLiteral("Insérer un DXF"), QStringLiteral("« %1 » : %2").arg(QFileInfo(chemin).fileName(), raison));
    return false;
  }
  prom::dxf::OptionsImportDxf o;
  if (optionsDonnees) o = *optionsDonnees;
  else {
    DialogueImportDxf dlg(d, QFileInfo(chemin).fileName(), this);
    if (dlg.exec() != QDialog::Accepted) return false;
    o = dlg.options();
  }
  Esquisse e = m_esq->esquisse();
  const prom::dxf::RapportImportDxf r = prom::dxf::importerDxf(e, d, o);
  if (r.total() == 0) {
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'>Aucun trait retenu : vérifie les calques cochés.</span>"));
    return false;
  }
  m_esq->appliquer(e);
  m_esq->selectionnerElements(r.elements);
  m_esq->recadrer();
  majCalques();
  QString t = QStringLiteral("<span style='color:#2E8B57'><b>DXF inséré</b></span> : %1 ligne(s), %2 arc(s), %3 cercle(s)").arg(r.lignes).arg(r.arcs).arg(r.cercles);
  if (r.approches) t += QStringLiteral(" (dont %1 pour suivre ellipses et splines)").arg(r.approches);
  t += o.fixer ? QStringLiteral(", fixés.") : QStringLiteral(" ; cote-les ou fixe-les pour contraindre le profil.");
  m_etat->setText(t);
  return true;
}

// Convertir les entités : toute la géométrie de référence entre dans l'esquisse, fixée.
void AtelierPiece::convertirEntites() {
  // Les arêtes choisies dans l'esquisse, sinon toutes celles encore proposées ; reprises en construction, liées à la
  // pièce quand elles sont dans le plan de l'esquisse (S3) ; une arête déjà reprise n'est pas dédoublée
  if (m_esq->nombreReferencesExternes() == 0) {
    m_etat->setText(QStringLiteral("<span style='color:#C77A1E'>Rien à reprendre : pas de volume avant cette esquisse, ou tout est déjà repris.</span>"));
    return;
  }
  const bool choix = !m_esq->externesChoisies().empty();
  const int nb = m_esq->convertirExternes();
  m_etat->setText(QStringLiteral("<span style='color:#2E8B57'><b>%1 entité(s) converties</b></span>%2 ; elles suivent la pièce quand elles en reprennent une arête.")
                      .arg(nb)
                      .arg(choix ? QStringLiteral(" (celles choisies)") : QString()));
}

void AtelierPiece::majCoinValidation() {
  if (m_barre3D && (m_esquisseEditee || m_gestion)) m_barre3D->hide();
  if (!m_coinValidation) return;
  const bool voir = m_esquisseEditee != 0 || m_gestion;
  m_coinValidation->setVisible(voir);
  if (voir) m_coinValidation->raise();
  if (auto* v = m_coinValidation->findChild<QToolButton*>(QStringLiteral("coinValider"))) v->setToolTip(m_esquisseEditee ? QStringLiteral("Terminer l’esquisse (Ctrl+Entrée)") : QStringLiteral("Valider la fonction"));
  if (auto* x = m_coinValidation->findChild<QToolButton*>(QStringLiteral("coinAbandonner"))) x->setToolTip(m_esquisseEditee ? QStringLiteral("Annuler l’esquisse") : QStringLiteral("Annuler la fonction"));
}

void AtelierPiece::annulerDansEsquisse() { m_esq->annulerEtape(); }
void AtelierPiece::retablirDansEsquisse() { m_esq->retablirEtape(); }
bool AtelierPiece::peutAnnulerDansEsquisse() const { return m_esq->peutAnnulerEtape(); }
bool AtelierPiece::peutRetablirDansEsquisse() const { return m_esq->peutRetablirEtape(); }

void AtelierPiece::terminerEsquisse() {
  m_esq->terminerSaisie(true);  // une valeur tapée sans Entrée compte (EI-06)
  if (FonctionPiece* f = m_piece.fonction(m_esquisseEditee)) {
    f->esquisse = m_esq->esquisse();
    enregistrer();
  }
  const int id = m_esquisseEditee;
  sortirEsquisse(m_esquisseEditee);
  m_etat->setText(QStringLiteral("<span style='color:#2F6BF5'><b>Esquisse terminée</b> : onglet Fonctions, puis Extrusion, Enlèvement ou Révolution.</span>"));
  if (std::exchange(m_toleApresEsquisse, 0) == id && id && !m_esquisseEditee) {
    choisirFonction(id);
    nouvelleFonction(TypeFonction::Tole, false, "");
    if (m_gestion) {
      m_brouillon.esquisseId = id;
      apercu();
      majProprietes();
    }
  }
}

// Annuler : l'esquisse revient à son état d'ouverture ; une esquisse neuve disparaît.
void AtelierPiece::annulerEsquisse() {
  m_esq->terminerSaisie(false);
  const int id = m_esquisseEditee;
  if (m_esquisseNouvelle) {
    auto& fs = m_piece.fonctions;
    fs.erase(std::remove_if(fs.begin(), fs.end(), [id](const FonctionPiece& g) { return g.id == id; }), fs.end());
  } else if (FonctionPiece* f = m_piece.fonction(id)) {
    *f = m_esquisseAvant;
  }
  enregistrer();
  m_toleApresEsquisse = 0;
  sortirEsquisse(m_esquisseNouvelle ? 0 : id);
  m_etat->setText(QStringLiteral("Esquisse annulée."));
}

void AtelierPiece::sortirEsquisse(int id) {
  if (!m_commande.isEmpty()) fermerCommandeEsquisse(false);
  finirSessionEsquisse();
  m_pileEsquisse = nullptr;
  m_esquisseEditee = 0;
  majChoixPieces();  // le ruban de la pièce redevient actif
  m_props->setVisible(m_gestion);
  if (m_bandeVue) m_bandeVue->show();
  m_tableCotes = nullptr;
  majCoinValidation();
  emit historiqueChange();
  m_esq->hide();
  m_esq->setProjection(std::nullopt);
  m_vue->setSolideTranslucide(false);
  m_outilsEsquisse->hide();
  m_outilsPiece->show();
  m_arbre->setEnabled(true);
  reconstruire(false);
  // La vue revient là où l'on était avant d'ouvrir l'esquisse.
  m_vue->restaurerCamera();
  if (id) choisirFonction(id);
  m_onglets->setCurrentIndex(1);
}

void AtelierPiece::afficherMasse() {
  if (m_res.forme.IsNull()) { m_etat->setText(QStringLiteral("La pièce n’a pas encore de volume.")); return; }
  if (!confirmerMalgreErreurs(QStringLiteral("Propriétés de masse"))) return;
  const ProprietesMasse pm = proprietesMasse(m_res.forme, m_piece.densite);
  const Unites u = unites();
  auto L = [&](double mm) { return QString::fromStdString(ecrireValeur(std::round(mm * 100) / 100, false, u)); };
  auto N = [](double x, int d) { return QLocale().toString(x, 'f', d); };
  const bool fiable = pm.valide;
  const QString approx = fiable ? QString() : QStringLiteral("≈ ");
  // Texte bâti par concaténation, un argument par marqueur : aucun « %17 » collé à un nombre (AP-13). Volume,
  // surface et inerties dans l'unité du document (cm³ et cm² en plus quand elle est le millimètre).
  const double k = depuisMillimetres(1, u.longueur);
  const QString sym = QString::fromUtf8(symbole(u.longueur));
  const bool mm = u.longueur == UniteLongueur::Millimetre;
  const QString volume = approx + N(pm.volume * k * k * k, mm ? 2 : 4) + QStringLiteral(" ") + sym + QStringLiteral("³") +
                         (mm ? QStringLiteral(" (") + N(pm.volume / 1000, 3) + QStringLiteral(" cm³)") : QString());
  const QString surface = approx + N(pm.aire * k * k, mm ? 2 : 4) + QStringLiteral(" ") + sym + QStringLiteral("²") +
                          (mm ? QStringLiteral(" (") + N(pm.aire / 100, 2) + QStringLiteral(" cm²)") : QString());
  const QString masse = m_piece.densiteConfirmee ? approx + N(pm.masse, 2) + QStringLiteral(" g") : QStringLiteral("≈ ") + N(pm.masse, 2) + QStringLiteral(" g (provisoire)");
  const QString uniteInertie = QStringLiteral("g·") + sym + QStringLiteral("²");
  auto I = [&](double gmm2) { return N(gmm2 * k * k, mm ? 1 : 4); };
  QString texte;
  texte += QStringLiteral("<h3>") + qs(m_piece.nom).toHtmlEscaped() + QStringLiteral("</h3>");
  texte += QStringLiteral("<p>Matériau : <b>") + qs(m_piece.materiau).toHtmlEscaped() + QStringLiteral("</b>, densité ") + N(m_piece.densite, 3) + QStringLiteral(" g/cm³");
  if (!m_piece.densiteConfirmee) texte += QStringLiteral(" — <b>provisoire</b> : matière non choisie, à confirmer dans Propriétés de la pièce");
  texte += QStringLiteral(".</p><table cellspacing='6'>");
  auto ligne = [&texte](const QString& nom, const QString& valeur) { texte += QStringLiteral("<tr><td>") + nom + QStringLiteral("</td><td>") + valeur + QStringLiteral("</td></tr>"); };
  ligne(QStringLiteral("Volume"), QStringLiteral("<b>") + volume + QStringLiteral("</b>"));
  ligne(QStringLiteral("Surface"), QStringLiteral("<b>") + surface + QStringLiteral("</b>"));
  ligne(QStringLiteral("Masse"), QStringLiteral("<b>") + masse + QStringLiteral("</b>"));
  ligne(QStringLiteral("Centre de gravité"), QStringLiteral("<b>X ") + L(pm.cx) + QStringLiteral(" · Y ") + L(pm.cy) + QStringLiteral(" · Z ") + L(pm.cz) + QStringLiteral("</b>"));
  ligne(QStringLiteral("Moments d’inertie au centre de gravité"),
        QStringLiteral("<b>Ixx ") + I(pm.ixx) + QStringLiteral(" · Iyy ") + I(pm.iyy) + QStringLiteral(" · Izz ") + I(pm.izz) + QStringLiteral("</b> ") + uniteInertie);
  ligne(QStringLiteral("Moments principaux (I1 ≤ I2 ≤ I3)"),
        QStringLiteral("<b>") + I(pm.i1) + QStringLiteral(" · ") + I(pm.i2) + QStringLiteral(" · ") + I(pm.i3) + QStringLiteral("</b> ") + uniteInertie);
  texte += QStringLiteral("</table>");
  // Produits d'inertie (convention écrite) et axes principaux (FO-23)
  auto axe = [&](int k) {
    const auto& a = pm.axesPrincipaux[static_cast<std::size_t>(k)];
    return QStringLiteral("(%1 ; %2 ; %3)").arg(N(a[0], 3), N(a[1], 3), N(a[2], 3));
  };
  texte += QStringLiteral("<table cellspacing='6'><tr><td>Produits d’inertie</td><td><b>Pxy ") + I(pm.ixy) + QStringLiteral(" · Pxz ") + I(pm.ixz) + QStringLiteral(" · Pyz ") +
           I(pm.iyz) + QStringLiteral("</b> ") + uniteInertie +
           QStringLiteral("<br><small>Pxy = ∫(x − x̄)(y − ȳ) dm ; le tenseur d’inertie porte −Pxy hors de sa diagonale.</small></td></tr><tr><td>Axes principaux</td><td>I1 ") + axe(0) +
           QStringLiteral(" · I2 ") + axe(1) + QStringLiteral(" · I3 ") + axe(2) + QStringLiteral("</td></tr></table>");
  texte += QStringLiteral("<p>Statut du calcul : <b>") + QString::fromStdString(libelleStatut(pm.statut)).toHtmlEscaped() +
           QStringLiteral("</b>.<br>Méthode : intégration exacte sur la géométrie B-Rep d’OpenCascade (pas de maillage), repère de la pièce.</p>");
  if (!fiable)
    texte += QStringLiteral("<p style='color:#C2402E'><b>Valeurs non fiables</b> : %1. Ne pas les utiliser pour un dimensionnement ; corriger la géométrie.</p>")
                 .arg(QString::fromStdString(pm.defaut).toHtmlEscaped());
  // Panneau latéral (U1) : le modèle reste manipulable pendant la lecture (AP-13)
  auto* boite = new QLabel(texte);
  boite->setObjectName(QStringLiteral("proprietesMasse"));
  boite->setWordWrap(true);
  boite->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  boite->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_panneau->ouvrir(QStringLiteral("masse"), QStringLiteral("Propriétés de masse"), QStringLiteral("masse"), boite, PanneauLateral::Boutons::Fermer);
}

// Ruban actif ou non ; le coin (Nouvelle pièce, propriétés) reste utilisable hors édition, même sans pièce (AP-28)
void AtelierPiece::activerRuban(bool oui) {
  if (!m_onglets) return;
  for (int i = 0; i < m_onglets->count(); ++i) m_onglets->widget(i)->setEnabled(oui);
  m_onglets->tabBar()->setEnabled(oui);
  if (QWidget* coin = m_onglets->cornerWidget(Qt::TopRightCorner)) coin->setEnabled(!m_gestion && m_esquisseEditee == 0);
}

void AtelierPiece::activerCommandes(bool oui) {
  activerRuban(oui);
}

// Plans de base : visibles quand la pièce est vide, quand on choisit le support d'une esquisse, ou à la demande.
void AtelierPiece::majPlans() {
  const bool planAttendu = m_gestion && (m_champ == Champ::Supports || m_champ == Champ::Neutre || m_champ == Champ::Direction || m_champ == Champ::AxeRevolution);
  const bool voir = m_indice >= 0 && (planAttendu || (!m_gestion && ((m_res.forme.IsNull() && m_piece.fonctions.empty()) || m_attenteEsquisse || m_plansDemandes)));
  if (voir != m_vue->plansVisibles()) m_vue->setPlansVisibles(voir);
}

// Nom, dans l'état d'avant la fonction en cours, de ce qui a été cliqué : même objet, sinon même géométrie
// (l'aperçu est un autre calcul de la même pièce).
std::string AtelierPiece::nommerDansAvant(const TopoDS_Shape& s, bool faces) const {
  if (m_avant.forme.IsNull()) return {};
  // Le nom topologique que lui donne l'aperçu, s'il existe déjà avant la fonction et ne dépend pas d'elle :
  // une arête raccourcie par un congé voisin garde le nom de ses deux faces, même si sa géométrie a changé.
  const std::string propre = std::to_string(m_brouillon.id) + ":";
  auto dependDeLaFonction = [&](const std::string& n) {
    if (n.rfind(propre, 0) == 0) return true;
    for (size_t p = n.find('|'); p != std::string::npos; p = n.find('|', p + 1))
      if (n.compare(p + 1, propre.size(), propre) == 0) return true;
    return n.find("S:" + propre) != std::string::npos;
  };
  if (!m_affiche.forme.IsNull()) {
    if (s.ShapeType() == TopAbs_EDGE) {
      const std::string n = m_affiche.nomArete(TopoDS::Edge(s));
      if (!n.empty() && !dependDeLaFonction(n) && !m_avant.aretes(n).empty()) return n;
    } else if (s.ShapeType() == TopAbs_FACE && faces) {
      const std::string n = m_affiche.nomFace(TopoDS::Face(s));
      if (!n.empty() && !dependDeLaFonction(n) && !m_avant.facesNommees(n).empty()) return n;
    }
  }
  const double tol = 1e-5 * std::max(1.0, std::sqrt([&] { Bnd_Box b; BRepBndLib::Add(m_avant.forme, b); return b.IsVoid() ? 1.0 : b.SquareExtent(); }()));
  if (s.ShapeType() == TopAbs_EDGE) {
    std::string n = m_avant.nomArete(TopoDS::Edge(s));
    if (!n.empty()) return n;
    BRepAdaptor_Curve c(TopoDS::Edge(s));
    const gp_Pnt m = c.Value((c.FirstParameter() + c.LastParameter()) / 2);
    for (TopExp_Explorer ex(m_avant.forme, TopAbs_EDGE); ex.More(); ex.Next()) {
      BRepAdaptor_Curve d(TopoDS::Edge(ex.Current()));
      if (d.Value((d.FirstParameter() + d.LastParameter()) / 2).Distance(m) < tol) return m_avant.nomArete(TopoDS::Edge(ex.Current()));
    }
    return {};
  }
  if (s.ShapeType() == TopAbs_FACE && faces) {
    std::string n = m_avant.nomFace(TopoDS::Face(s));
    if (!n.empty()) return n;
    GProp_GProps g;
    BRepGProp::SurfaceProperties(s, g);
    for (TopExp_Explorer ex(m_avant.forme, TopAbs_FACE); ex.More(); ex.Next()) {
      GProp_GProps h;
      BRepGProp::SurfaceProperties(ex.Current(), h);
      if (h.CentreOfMass().Distance(g.CentreOfMass()) < tol && std::abs(h.Mass() - g.Mass()) < tol * std::max(1.0, g.Mass())) return m_avant.nomFace(TopoDS::Face(ex.Current()));
    }
    return {};
  }
  if (s.ShapeType() == TopAbs_VERTEX) {
    std::string n = m_avant.nomSommet(TopoDS::Vertex(s));
    if (!n.empty()) return n;
    const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(s));
    for (TopExp_Explorer ex(m_avant.forme, TopAbs_VERTEX); ex.More(); ex.Next())
      if (BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())).Distance(p) < tol) return m_avant.nomSommet(TopoDS::Vertex(ex.Current()));
  }
  return {};
}

void AtelierPiece::proprietesPiece() {
  if (m_indice < 0) return;
  // Panneau latéral (U1) : la vue reste manipulable ; Valider applique, Annuler laisse la pièce telle quelle
  auto* page = new QWidget;
  auto* form = new QFormLayout(page);
  form->setContentsMargins(0, 0, 0, 0);
  auto* nom = new QLineEdit(qs(m_piece.nom));
  form->addRow(QStringLiteral("Nom"), nom);
  auto* reference = new QLineEdit(qs(m_piece.reference));
  reference->setPlaceholderText(QStringLiteral("numéro de pièce, par exemple SUP-001"));
  form->addRow(QStringLiteral("Référence"), reference);
  auto* fournisseur = new QLineEdit(qs(m_piece.fournisseur));
  fournisseur->setPlaceholderText(QStringLiteral("fabrication interne, ou nom du fournisseur"));
  form->addRow(QStringLiteral("Fournisseur"), fournisseur);
  // Propriétés personnalisées (M10) : reprises au cartouche (Révision, Désignation, Projet, Auteur) et proposées
  // comme colonnes de nomenclature
  auto* proprietes = new QTableWidget(0, 2);
  proprietes->setObjectName(QStringLiteral("proprietesPiece"));
  proprietes->setHorizontalHeaderLabels({QStringLiteral("Propriété"), QStringLiteral("Valeur")});
  proprietes->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  proprietes->verticalHeader()->hide();
  proprietes->setMinimumHeight(110);
  auto ajouterLigne = [proprietes](const QString& n, const QString& v) {
    const int r = proprietes->rowCount();
    proprietes->insertRow(r);
    proprietes->setItem(r, 0, new QTableWidgetItem(n));
    proprietes->setItem(r, 1, new QTableWidgetItem(v));
    return r;
  };
  for (const auto& [n, v] : m_piece.proprietes) ajouterLigne(qs(n), qs(v));
  auto* ajouter = new QToolButton;
  ajouter->setText(QStringLiteral("Ajouter une propriété"));
  ajouter->setPopupMode(QToolButton::InstantPopup);
  auto* menuProprietes = new QMenu(ajouter);
  for (const char* n : {"Désignation", "Révision", "Auteur", "Approuvé par", "Projet", "Propriétaire", "Type de document", "Traitement", "Finition", "Norme"})
    menuProprietes->addAction(QString::fromUtf8(n), proprietes, [proprietes, ajouterLigne, n] {
      const int r = ajouterLigne(QString::fromUtf8(n), QString());
      proprietes->setCurrentCell(r, 1);
      proprietes->editItem(proprietes->item(r, 1));
    });
  menuProprietes->addAction(QStringLiteral("Autre…"), proprietes, [proprietes, ajouterLigne] {
    const int r = ajouterLigne(QString(), QString());
    proprietes->setCurrentCell(r, 0);
    proprietes->editItem(proprietes->item(r, 0));
  });
  ajouter->setMenu(menuProprietes);
  auto* retirer = new QToolButton;
  retirer->setText(QStringLiteral("Retirer"));
  connect(retirer, &QToolButton::clicked, proprietes, [proprietes] { if (proprietes->currentRow() >= 0) proprietes->removeRow(proprietes->currentRow()); });
  auto* boutonsProprietes = new QHBoxLayout;
  boutonsProprietes->addWidget(ajouter);
  boutonsProprietes->addWidget(retirer);
  boutonsProprietes->addStretch(1);
  auto* blocProprietes = new QVBoxLayout;
  blocProprietes->addWidget(proprietes);
  blocProprietes->addLayout(boutonsProprietes);
  form->addRow(QStringLiteral("Propriétés"), blocProprietes);
  auto* longueur = new QComboBox;
  for (const auto& [code, titre] : {std::pair{"mm", "millimètre (mm)"}, std::pair{"cm", "centimètre (cm)"}, std::pair{"m", "mètre (m)"}, std::pair{"in", "pouce (in)"}, std::pair{"ft", "pied (ft)"}})
    longueur->addItem(QString::fromUtf8(titre), QString::fromLatin1(code));
  longueur->setCurrentIndex(std::max(0, longueur->findData(qs(m_piece.uniteLongueur))));
  form->addRow(QStringLiteral("Longueurs"), longueur);
  auto* angle = new QComboBox;
  angle->addItem(QStringLiteral("degré (°)"), QStringLiteral("deg"));
  angle->addItem(QStringLiteral("radian (rad)"), QStringLiteral("rad"));
  angle->setCurrentIndex(std::max(0, angle->findData(qs(m_piece.uniteAngle))));
  form->addRow(QStringLiteral("Angles"), angle);
  struct Mat { const char* nom; double densite; };
  static const Mat materiaux[] = {{"PLA", 1.24}, {"PETG", 1.27}, {"ABS", 1.04}, {"Nylon PA12", 1.01}, {"Polycarbonate", 1.20}, {"POM (Delrin)", 1.41},
                                  {"Aluminium 6061", 2.70}, {"Acier", 7.85}, {"Inox 304", 8.00}, {"Laiton", 8.50}, {"Cuivre", 8.96}, {"Titane", 4.43}, {"Chêne", 0.75}};
  auto* materiau = new QComboBox;
  for (const auto& m : materiaux) materiau->addItem(QString::fromUtf8(m.nom), m.densite);
  materiau->addItem(QStringLiteral("Autre (densité libre)"), 0.0);
  int im = materiau->findText(qs(m_piece.materiau));
  materiau->setCurrentIndex(im >= 0 ? im : materiau->count() - 1);
  form->addRow(QStringLiteral("Matériau"), materiau);
  auto* densite = new ChampNombre;
  densite->setRange(0.001, 100);
  densite->setDecimals(3);
  densite->setSuffix(QStringLiteral(" g/cm³"));
  densite->setValue(m_piece.densite);
  form->addRow(QStringLiteral("Densité"), densite);
  // Densité confirmée : sans elle, les masses restent affichées « provisoires ».
  auto* confirmee = new QCheckBox(QStringLiteral("Matière et densité vérifiées (sinon, masses provisoires)"));
  confirmee->setChecked(m_piece.densiteConfirmee);
  form->addRow(QString(), confirmee);
  connect(densite, &ChampNombre::valueChanged, page, [confirmee] { confirmee->setChecked(true); });
  const auto couleur = std::make_shared<QColor>(qs(m_piece.couleur));
  auto* teinte = new QPushButton;
  teinte->setObjectName(QStringLiteral("couleurPiece"));
  auto majTeinte = [teinte, couleur] { teinte->setText(couleur->name()); teinte->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border-radius: 6px; padding: 6px; }").arg(couleur->name(), couleur->lightness() > 140 ? QStringLiteral("#10142A") : QStringLiteral("white"))); };
  majTeinte();
  connect(teinte, &QPushButton::clicked, page, [couleur, majTeinte, page] { const QColor c = QColorDialog::getColor(*couleur, page, QStringLiteral("Couleur de la pièce")); if (c.isValid()) { *couleur = c; majTeinte(); } });
  form->addRow(QStringLiteral("Couleur"), teinte);
  auto* transparence = new QSlider(Qt::Horizontal);
  transparence->setRange(0, 90);
  transparence->setValue(static_cast<int>(m_piece.transparence * 100));
  form->addRow(QStringLiteral("Transparence"), transparence);
  connect(materiau, &QComboBox::activated, page, [materiau, densite, confirmee] {
    if (materiau->currentData().toDouble() > 0) densite->setValue(materiau->currentData().toDouble());
    confirmee->setChecked(true);
  });
  nom->setObjectName(QStringLiteral("nomPiece"));
  materiau->setObjectName(QStringLiteral("materiauPiece"));
  densite->setObjectName(QStringLiteral("densitePiece"));
  const int indice = m_indice;
  m_panneau->ouvrir(QStringLiteral("proprietesPiece"), QStringLiteral("Propriétés de la pièce"), QStringLiteral("proprietes"), page, PanneauLateral::Boutons::ValiderAnnuler,
                    [this, indice, nom, reference, fournisseur, proprietes, longueur, angle, materiau, densite, confirmee, couleur, transparence] {
                      if (m_indice != indice) return true;  // une autre pièce est ouverte : rien à écrire
                      appliquerProprietesPiece(nom->text(), reference->text(), fournisseur->text(), proprietes, longueur->currentData().toString(), angle->currentData().toString(),
                                               materiau->currentData().toDouble() > 0 ? materiau->currentText() : QStringLiteral("Autre"), densite->value(), confirmee->isChecked(),
                                               *couleur, transparence->value() / 100.0);
                      return true;
                    });
}

void AtelierPiece::appliquerProprietesPiece(const QString& nom, const QString& reference, const QString& fournisseur, const QTableWidget* proprietes, const QString& longueur,
                                            const QString& angle, const QString& materiau, double densite, bool confirmee, const QColor& couleur, double transparence) {
  m_piece.nom = nom.trimmed().isEmpty() ? m_piece.nom : nom.trimmed().toStdString();
  m_piece.reference = reference.trimmed().toStdString();
  m_piece.fournisseur = fournisseur.trimmed().toStdString();
  m_piece.proprietes.clear();
  for (int r = 0; r < proprietes->rowCount(); ++r) {
    const QString n = proprietes->item(r, 0) ? proprietes->item(r, 0)->text().trimmed() : QString();
    const QString v = proprietes->item(r, 1) ? proprietes->item(r, 1)->text().trimmed() : QString();
    if (!n.isEmpty()) m_piece.proprietes.emplace_back(n.toStdString(), v.toStdString());
  }
  m_piece.uniteLongueur = longueur.toStdString();
  m_piece.uniteAngle = angle.toStdString();
  m_piece.materiau = materiau.toStdString();
  m_piece.densite = densite;
  m_piece.densiteConfirmee = confirmee;
  m_piece.couleur = couleur.name().toStdString();
  m_piece.transparence = transparence;
  enregistrer();
  majChoixPieces();
  m_esq->setUnites(unites());
  majProprietes();
  reconstruire(false);
}

bool AtelierPiece::confirmerMalgreErreurs(const QString& action) {
  // Fonctions en échec : la forme est le dernier solide valide, privé d'elles et de leurs suites (EX-13)
  if (m_res.erreurs.empty()) return true;
  return QMessageBox::question(this, action,
                               QStringLiteral("%1 fonction(s) en échec ne sont pas appliquées : le résultat portera sur la pièce sans elles ni celles qui en dépendent. Continuer ?")
                                   .arg(m_res.erreurs.size())) == QMessageBox::Yes;
}

void AtelierPiece::exporter() {
  if (m_res.forme.IsNull()) { QMessageBox::information(this, QStringLiteral("Exporter la pièce"), QStringLiteral("La pièce n’a pas encore de volume.")); return; }
  if (!confirmerMalgreErreurs(QStringLiteral("Exporter la pièce"))) return;
  // Le format suit le filtre choisi ; le suffixe tapé ne l'emporte que s'il désigne un format du même filtre (EX-12)
  struct Format { QString filtre; QStringList suffixes; };
  const std::vector<Format> formats = {
      {QStringLiteral("STEP AP214 (*.step *.stp)"), {QStringLiteral("step"), QStringLiteral("stp")}},
      {QStringLiteral("STEP AP242 (*.step *.stp)"), {QStringLiteral("step"), QStringLiteral("stp")}},
      {QStringLiteral("STL (*.stl)"), {QStringLiteral("stl")}},
      {QStringLiteral("3MF (*.3mf)"), {QStringLiteral("3mf")}},
      {QStringLiteral("IGES, surfaces (*.igs *.iges)"), {QStringLiteral("igs"), QStringLiteral("iges")}},
      {QStringLiteral("IGES, solide B-rep (*.igs *.iges)"), {QStringLiteral("igs"), QStringLiteral("iges")}},
      {QStringLiteral("OBJ (*.obj)"), {QStringLiteral("obj")}},
      {QStringLiteral("PLY (*.ply)"), {QStringLiteral("ply")}},
      {QStringLiteral("BREP (*.brep)"), {QStringLiteral("brep")}}};
  QStringList filtres;
  for (const auto& f : formats) filtres << f.filtre;
  QString filtre = formats.front().filtre;
  QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter la pièce"), qs(m_piece.nom) + QStringLiteral(".step"), filtres.join(QStringLiteral(";;")), &filtre);
  if (chemin.isEmpty()) return;
  const QString suffixe = QFileInfo(chemin).suffix().toLower();
  int choix = -1;
  for (int i = 0; i < static_cast<int>(formats.size()); ++i)
    if (formats[static_cast<std::size_t>(i)].filtre == filtre) choix = i;
  if (choix < 0) {  // dialogue sans filtre rendu : le suffixe décide, STEP sinon
    choix = 0;
    for (int i = 0; i < static_cast<int>(formats.size()); ++i)
      if (formats[static_cast<std::size_t>(i)].suffixes.contains(suffixe)) { choix = i; break; }
  }
  const Format& f = formats[static_cast<std::size_t>(choix)];
  if (!f.suffixes.contains(suffixe)) chemin += QLatin1Char('.') + f.suffixes.front();
  try {
    const std::string c = chemin.toStdString();  // UTF-8, comme l'attendent OpenCascade et le noyau
    PieceNommee pn{m_piece.nom, m_res.forme};
    couleurDe(m_piece.couleur, pn.r, pn.g, pn.b);  // couleur de la pièce (EX-09)
    for (const auto& [face, hexa] : facesColorees(m_piece.couleursFaces, m_res)) {  // apparences par face (M12)
      std::array<double, 3> rgb{};
      couleurDe(hexa, rgb[0], rgb[1], rgb[2]);
      pn.couleursFaces.emplace_back(face, rgb);
    }
    const std::vector<PieceNommee> pieces = {pn};
    OptionsStep options;
    options.schema = choix == 1 ? SchemaStep::AP242 : SchemaStep::AP214;
    switch (choix) {
      case 2: exporterStl(m_res.forme, c); break;
      case 3: exporter3mf(pieces, c); break;
      case 4: exporterIges(pieces, c, false); break;
      case 5: exporterIges(pieces, c, true); break;
      case 6: exporterObj(pieces, c); break;
      case 7: exporterPly(pieces, c); break;
      case 8: exporterBrep(pieces, c); break;
      default: exporterStep(pieces, c, options); break;
    }
    m_etat->setText(QStringLiteral("Pièce exportée : %1").arg(chemin));
  } catch (const std::exception& e) {
    QMessageBox::warning(this, QStringLiteral("Export impossible"), QString::fromUtf8(e.what()));
  } catch (const Standard_Failure& e) {
    QMessageBox::warning(this, QStringLiteral("Export impossible"), QString::fromUtf8(e.GetMessageString()));
  }
}

void AtelierPiece::setEtatIntegre(bool oui) { m_etat->setVisible(!oui); }
