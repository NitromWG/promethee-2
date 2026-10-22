// Prométhée : atelier d'assemblage. Licence GPL-3.0-only.
#include "AtelierAssemblage.hpp"

#include <algorithm>
#include <memory>
#include <cmath>
#include <numbers>

#include <set>
#include <QApplication>
#include <QCheckBox>
#include <QTimer>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <GProp_GProps.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QListWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>

#include "ChampValeur.hpp"
#include "PanneauLateral.hpp"
#include "Document.hpp"
#include "EtiquetteEtat.hpp"
#include "Icones.hpp"
#include "DialogueNomenclature.hpp"
#include "MiseEnPlan.hpp"
#include "RemplacerPage.hpp"
#include "VuePiece.hpp"
#include "promethee/depot.hpp"
#include "promethee/structure.hpp"
#include "promethee/assemblage.hpp"
#include "promethee/construction.hpp"
#include "promethee/geometrie.hpp"
#include "promethee/import_cao.hpp"
#include "promethee/mesure.hpp"
#include "promethee/piece.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/nomenclature.hpp"
#include "promethee/nomenclature_meca.hpp"

using namespace prom;

namespace {
// Place une forme : simple déplacement, ou copie transformée quand la symétrie entre en jeu (OpenCascade refuse un
// déplacement « miroir » sur une forme)
TopoDS_Shape placer(const TopoDS_Shape& f, const ComposantAssemblage& c) {
  if (f.IsNull()) return f;
  const gp_Trsf t = placementComposant(c);
  if (c.miroir) return BRepBuilderAPI_Transform(f, t, true).Shape();
  return f.Moved(TopLoc_Location(t));
}
const char* STYLE =
    "#panneauAssemblage { background: #FAFBFE; }"
    "#panneauAssemblage QGroupBox { background: #F3F5FB; border: 1px solid #E1E5F0; border-radius: 9px; margin-top: 16px; padding: 12px 8px 8px 8px; }"
    "#panneauAssemblage QGroupBox::title { subcontrol-origin: margin; left: 10px; top: 2px; padding: 0 4px; color: #6B7393; font-weight: 600; }"
    "#panneauAssemblage QPushButton { text-align: left; padding: 6px 9px; border: 1px solid #CDD3E4; border-radius: 7px; background: white; color: #232A45; }"
    "#panneauAssemblage QPushButton:hover { border-color: #6E9BFF; }";
const char* STYLE_RUBAN =
    "QTabWidget::pane { border: none; }"
    "QTabBar::tab { padding: 6px 14px; font-weight: 600; color: #4A5270; border: none; }"
    "QTabBar::tab:selected { color: #10142A; border-bottom: 3px solid #5B4BF5; }"
    "QToolBar { border: none; spacing: 2px; background: #FAFBFE; }"
    "QToolButton { border-radius: 6px; padding: 3px; color: #10142A; }"
    "QToolButton:hover { background: #E9EEFF; }"
    "QToolButton:checked { background: #DDE5FF; }"
    "QToolButton::menu-button { border: none; background: transparent; width: 14px; }";
// Couleurs de repli des composants (quand la pièce n'en a pas de propre)
const QColor PALETTE[] = {QColor(0xC9, 0xCF, 0xDF), QColor(0xB8, 0xD0, 0xE8), QColor(0xE6, 0xC9, 0xA8), QColor(0xBF, 0xDB, 0xC4), QColor(0xD9, 0xC2, 0xDE), QColor(0xE3, 0xD7, 0xA4)};
QString nomType(const ContrainteAssemblage& c) {
  if (!typeContrainteAssemblageConnu(c.type)) return QStringLiteral("Type inconnu « %1 » (non résolu)").arg(QString::fromStdString(c.type));
  if (c.type == "concentrique") return QStringLiteral("Concentrique");
  if (c.type == "parallele") return QStringLiteral("Parallèle");
  if (c.type == "perpendiculaire") return QStringLiteral("Perpendiculaire");
  if (c.type == "angle") return QStringLiteral("Angle %1°").arg(c.valeur);
  if (c.type == "tangente") return QStringLiteral("Tangente");
  if (c.type == "pivot") return c.pilotee ? QStringLiteral("Pivot piloté à %1°").arg(c.valeur) : QStringLiteral("Pivot");
  if (c.type == "glissiere") return c.pilotee ? QStringLiteral("Glissière pilotée à %1 mm").arg(c.valeur) : QStringLiteral("Glissière");
  if (c.type == "rotule") return QStringLiteral("Rotule");
  if (c.type == "engrenage") return QStringLiteral("Engrenage ×%1").arg(c.valeur);
  if (c.type == "vis") return QStringLiteral("Vis-écrou");
  if (c.inverse) return QStringLiteral("Alignée");
  return std::abs(c.valeur) > 1e-12 ? QStringLiteral("Distance %1 mm").arg(c.valeur) : QStringLiteral("Coïncidente");
}
}  // namespace

AtelierAssemblage::AtelierAssemblage(Document* doc, QWidget* parent) : QWidget(parent), m_doc(doc) {
  auto* col = new QVBoxLayout(this);
  col->setContentsMargins(0, 0, 0, 0);
  col->setSpacing(0);
  m_ruban = new QTabWidget;
  m_ruban->setDocumentMode(true);
  m_ruban->setStyleSheet(QString::fromLatin1(STYLE_RUBAN));
  m_ruban->setMaximumHeight(108);
  col->addWidget(m_ruban);
  auto* corps = new QSplitter(Qt::Horizontal);
  auto* scene = new QWidget;
  auto* sl = new QVBoxLayout(scene);
  sl->setContentsMargins(0, 0, 0, 0);
  m_vue = new VuePiece;
  m_vue->setPlansVisibles(false);  // pas de grands plans de base dans un assemblage
  sl->addWidget(m_vue);
  corps->addWidget(scene);
  m_props = new QScrollArea;
  m_props->setWidgetResizable(true);
  m_props->setMinimumWidth(300);
  m_props->setFrameShape(QFrame::NoFrame);
  corps->addWidget(m_props);
  // Panneau latéral des outils (U1) : analyses, propriétés physiques, répétitions, remplacement, engrenage
  m_panneau = new PanneauLateral;
  corps->addWidget(m_panneau);
  corps->setStretchFactor(0, 1);
  corps->setSizes({1000, 340});
  col->addWidget(corps, 1);
  m_etat = new EtiquetteEtat;
  m_etat->setContentsMargins(10, 4, 10, 4);
  connect(m_etat, &EtiquetteEtat::texteChange, this, &AtelierAssemblage::messageChange);
  col->addWidget(m_etat);
  m_barre = new QFrame(scene);
  m_barre->setObjectName(QStringLiteral("barreAssemblage"));
  m_barre->setStyleSheet(QStringLiteral("#barreAssemblage { background: rgba(255,255,255,242); border: 1px solid #CDD3E4; border-radius: 10px; }"
                                        "QToolButton { border: none; border-radius: 6px; padding: 4px; } QToolButton:hover { background: #E9EEFF; }"
                                        "QLabel { color: #4A5270; font-size: 11px; padding: 0 4px; }"));
  m_barre->hide();
  construireRuban();
  // Clics dans la vue : composant choisi ; deux faces de composants différents : suggestions de contraintes
  connect(m_vue, &VuePiece::survol, this, [this](double x, double y, double z) { suivrePose(x, y, z); });
  connect(m_vue, &VuePiece::pointClique, this, [this](double, double, double, bool) { if (m_pose >= 0) finirPose(true); });
  connect(m_vue, &VuePiece::selectionChangee, this, [this] {
    if (m_pose >= 0) return;  // pendant la pose, le clic pose le composant
    const auto sel = m_vue->selection();
    if (sel.empty()) { if (!m_mesure) m_barre->hide(); else m_vue->setTracesMesure({}); return; }
    if (m_mesure) {
      // Mesure complète (M11), comme dans l'atelier Pièces : rayon, Ø, coordonnées, distance et ΔX ΔY ΔZ, centre à
      // centre, entraxe, écart de plans, angle vrai ; la distance est dessinée
      std::vector<ElementMesure> elements;
      for (const auto& x : sel) { ElementMesure e; e.forme = x; elements.push_back(e); }
      const ResultatMesure r = mesurer(elements);
      QString t = QStringLiteral("<b>Mesure</b>");
      for (const auto& l : lignesMesure(r, Unites{})) t += QStringLiteral("<br>") + QString::fromStdString(l).toHtmlEscaped();
      if (sel.size() == 1) t += QStringLiteral("<br>Clique un second élément.");
      m_etat->setText(t);
      std::vector<VuePiece::TraitMesure> traits;
      if (r.distance && r.distanceMin > 1e-9) traits.push_back({r.pa, r.pb, QString::fromStdString(texteGrandeur({GrandeurMesure::Nature::Longueur, "", r.distanceMin}, Unites{}))});
      if (r.centres && r.distanceCentres > 1e-9 && (r.ca.Distance(r.pa) > 1e-6 || r.cb.Distance(r.pb) > 1e-6))
        traits.push_back({r.ca, r.cb, QStringLiteral("⊙ ") + QString::fromStdString(texteGrandeur({GrandeurMesure::Nature::Longueur, "", r.distanceCentres}, Unites{}))});
      m_vue->setTracesMesure(traits);
      return;
    }
    const TopoDS_Shape s = sel.front();
    // Face, arête, sommet d'un composant, ou plan, axe, origine de l'assemblage (A2)
    int k = -1;
    std::string nom;
    if (!elementClique(s, k, nom)) return;
    if (!m_typeContrainte.empty()) { choisirReference(k, nom); return; }
    const bool assemblageClique = k == COMPOSANT_ASSEMBLAGE;
    // Second élément, d'un autre composant ou de l'assemblage : on garde le premier pour proposer les contraintes
    const bool premierAssemblage = m_faceComposant == COMPOSANT_ASSEMBLAGE;
    if (m_faceComposant != -1 && !m_faceNom.empty() && !nom.empty() && (assemblageClique != premierAssemblage || (!assemblageClique && k != m_faceComposant))) {
      m_secondComposant = k;
      m_secondNom = nom;
      majBarreFlottante(m_vue->derniereSouris());
      return;
    }
    if (assemblageClique) {
      m_faceComposant = k;
      m_faceNom = nom;
      m_faceForme.Nullify();
      m_barre->hide();
      m_etat->setText(QStringLiteral("%1 choisi : clique une face, une arête ou un sommet d’un composant pour le contraindre.").arg(libelleReference(k, nom)));
      return;
    }
    if (QApplication::keyboardModifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
      if (m_choisi >= 0) m_multi.insert(m_choisi);
      if (m_multi.count(k)) m_multi.erase(k); else m_multi.insert(k);
    } else m_multi.clear();
    choisirComposant(k);
    m_faceComposant = k;
    m_faceNom = nom;
    m_faceForme = s;
    majBarreFlottante(m_vue->derniereSouris());
  });
  // Glisser un composant à la souris : déplacer ou faire pivoter, dans le respect des contraintes (A4)
  connect(m_vue, &VuePiece::glisserDebut, this, [this](double x, double y, double z) { debutGlisser(x, y, z); });
  connect(m_vue, &VuePiece::glisserVers, this, [this](double dx, double dy, double dz) { glisserVers(dx, dy, dz); });
  connect(m_vue, &VuePiece::glisserFin, this, [this] { finGlisser(); });
  connect(m_doc, &Document::etatChange, this, [this] {
    revaliderSelection();
    if (!isVisible()) return;
    // Une pièce utilisée a changé (Modifier la pièce, Annuler…) : l'assemblage se résout à nouveau (AS-12)
    if (assemblageValide()) {
      if (m_signature.empty()) m_signature = signaturePieces();
      else if (!m_enResolution && signaturePieces() != m_signature) resoudre();
    }
    reconstruire(false);
    majProprietes();
  });
  // Échap abandonne l'outil en cours ; Ctrl+B reconstruit (résolution relancée)
  auto* echap = new QShortcut(QKeySequence(Qt::Key_Escape), this);
  echap->setContext(Qt::WidgetWithChildrenShortcut);
  connect(echap, &QShortcut::activated, this, [this] { abandonnerOutil(); });
  auto* reconstruireRaccourci = new QShortcut(QKeySequence(QStringLiteral("Ctrl+B")), this);
  reconstruireRaccourci->setContext(Qt::WidgetWithChildrenShortcut);
  connect(reconstruireRaccourci, &QShortcut::activated, this, [this] { resoudre(); reconstruire(false); majProprietes(); });
  // Annuler, Rétablir, ouverture : les rangs tenus ici ne désignent plus forcément les mêmes composants (AS-14, AS-27)
  connect(m_doc, &Document::projetRemplace, this, [this] {
    oublierSelection();
    m_signature.clear();  // projet remplacé (annulation, ouverture) : ses positions font foi
    m_dernierEtat.reset();
    m_problemesSous.clear();
    m_doc->definirProblemesExternes("assemblage", {});
  });
}

void AtelierAssemblage::setEtatIntegre(bool oui) { m_etat->setVisible(!oui); }

// ---------- Ruban ----------
void AtelierAssemblage::construireRuban() {
  struct V { QString icone, texte, bulle; std::function<void()> f; };
  auto onglet = [this](const QString& titre) {
    auto* b = new QToolBar;
    b->setIconSize(QSize(30, 30));
    m_ruban->addTab(b, titre);
    return b;
  };
  auto gros = [this](QToolBar* b, const QString& titre, const std::vector<V>& vs) {
    auto* t = new QToolButton;
    t->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    t->setIconSize(QSize(30, 30));
    t->setAutoRaise(true);
    t->setMinimumWidth(std::max(62, QFontMetrics(t->font()).horizontalAdvance(titre) + (vs.size() > 1 ? 30 : 16)));
    std::vector<QAction*> acts;
    for (const auto& v : vs) {
      auto* a = new QAction(icone(v.icone), v.texte, t);
      a->setIconText(titre);
      a->setToolTip(v.bulle.isEmpty() ? v.texte : v.texte + QStringLiteral(" — ") + v.bulle);
      a->setStatusTip(a->toolTip());
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
  auto grille = [this](QToolBar* b, const QString& legende, const std::vector<std::vector<V>>& cases) {
    auto* boite = new QWidget;
    auto* v = new QVBoxLayout(boite);
    v->setContentsMargins(4, 0, 4, 0);
    v->setSpacing(1);
    auto* g = new QGridLayout;
    g->setSpacing(1);
    for (size_t k = 0; k < cases.size(); ++k) {
      auto* t = new QToolButton;
      t->setIconSize(QSize(20, 20));
      t->setAutoRaise(true);
      QMenu* m = cases[k].size() > 1 ? new QMenu(t) : nullptr;
      for (size_t j = 0; j < cases[k].size(); ++j) {
        const V& x = cases[k][j];
        auto* a = new QAction(icone(x.icone), x.texte, boite);
        a->setToolTip(x.bulle.isEmpty() ? x.texte : QStringLiteral("<b>%1</b><br>%2").arg(x.texte, x.bulle));
        a->setStatusTip(x.bulle.isEmpty() ? x.texte : x.texte + QStringLiteral(" — ") + x.bulle);
        const auto f = x.f;
        connect(a, &QAction::triggered, this, [f] { f(); });
        if (j == 0) t->setDefaultAction(a);
        if (m) m->addAction(a);
      }
      if (m) { t->setMenu(m); t->setPopupMode(QToolButton::MenuButtonPopup); }
      g->addWidget(t, static_cast<int>(k) % 2, static_cast<int>(k) / 2);
    }
    v->addLayout(g);
    auto* l = new QLabel(legende);
    l->setAlignment(Qt::AlignHCenter);
    l->setStyleSheet(QStringLiteral("color: #4A5270; font-size: 11px;"));
    v->addWidget(l);
    b->addWidget(boite);
  };
  // Onglet Assemblage : composants, contraintes, manipulation
  QToolBar* bA = onglet(QStringLiteral("Assemblage"));
  m_inserer = new QToolButton;
  m_inserer->setIcon(icone(QStringLiteral("piece")));
  m_inserer->setText(QStringLiteral("Composant"));
  m_inserer->setToolTip(QStringLiteral("Insérer un composant : pièce du projet, sous-assemblage, fichier STEP, IGES, BREP, STL, ou pièce d’un autre projet"));
  m_inserer->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  m_inserer->setIconSize(QSize(30, 30));
  m_inserer->setAutoRaise(true);
  m_inserer->setPopupMode(QToolButton::InstantPopup);
  m_inserer->setMenu(new QMenu(m_inserer));
  bA->addWidget(m_inserer);
  gros(bA, QStringLiteral("Nouvelle pièce"), {{QStringLiteral("nouvelle-piece"), QStringLiteral("Nouvelle pièce"), QStringLiteral("créée dans l’assemblage, puis modelée"), [this] { emit nouvellePieceDemandee(m_indice); }}});
  gros(bA, QStringLiteral("Fixer"), {{QStringLiteral("fixe"), QStringLiteral("Fixer ou libérer"), QStringLiteral("un composant fixé ne bouge plus"), [this] { basculerFixe(); }}});
  gros(bA, QStringLiteral("Reconstruire"), {{QStringLiteral("recadrer"), QStringLiteral("Reconstruire (Ctrl+B)"), QStringLiteral("relit les pièces et résout à nouveau les contraintes"), [this] {
         resoudre();
         reconstruire(false);
         majProprietes();
       }}});
  gros(bA, QStringLiteral("Sous-assemblage"), {{QStringLiteral("assemblage"), QStringLiteral("Créer un sous-assemblage"), QStringLiteral("avec les composants choisis (Maj + clic pour en prendre plusieurs)"), [this] { creerSousAssemblageDepuisSelection(); }}});
  bA->addSeparator();
  // Plans, axes et origine de l'assemblage, à contraindre comme une face (A2)
  {
    QToolButton* rep = gros(bA, QStringLiteral("Repère"), {{QStringLiteral("plans"), QStringLiteral("Plans et axes de l’assemblage"), QStringLiteral("montre les plans XY, XZ, YZ, les axes et l’origine, que les contraintes peuvent viser"), [] {}}});
    rep->setObjectName(QStringLiteral("repereAssemblage"));
    rep->defaultAction()->setCheckable(true);
    connect(rep->defaultAction(), &QAction::toggled, this, [this](bool on) { setRepereVisible(on); });
  }
  gros(bA, QStringLiteral("Contraindre"),
       {{QStringLiteral("coincident"), QStringLiteral("Coïncidente"), QStringLiteral("deux faces planes l’une contre l’autre ; un sommet ou une arête sur un plan, une arête ou un sommet ; plans de l’assemblage"), [this] { commencerContrainte("coincidence"); }},
        {QStringLiteral("colineaire"), QStringLiteral("Alignée"), QStringLiteral("deux faces planes dans le même plan"), [this] { commencerContrainte("aligne"); }},
        {QStringLiteral("cote"), QStringLiteral("Distance…"), QStringLiteral("deux faces planes face à face, à une distance ; un sommet ou une arête à une distance d’un plan"), [this] { commencerContrainte("distance"); }},
        {QStringLiteral("pivoter"), QStringLiteral("Angle…"), QStringLiteral("angle entre deux plans, arêtes ou axes"), [this] { commencerContrainte("angle"); }},
        {QStringLiteral("concentrique"), QStringLiteral("Concentrique"), QStringLiteral("deux axes confondus : faces cylindriques, arêtes circulaires ou droites, axes de l’assemblage"), [this] { commencerContrainte("concentrique"); }},
        {QStringLiteral("parallele"), QStringLiteral("Parallèle"), QStringLiteral("deux plans, arêtes ou axes"), [this] { commencerContrainte("parallele"); }},
        {QStringLiteral("perpendiculaire"), QStringLiteral("Perpendiculaire"), QStringLiteral("deux plans, arêtes ou axes"), [this] { commencerContrainte("perpendiculaire"); }},
        {QStringLiteral("arc-tangent"), QStringLiteral("Tangente"), QStringLiteral("face plane et face cylindrique"), [this] { commencerContrainte("tangente"); }}});
  grille(bA, QStringLiteral("Liaisons"),
         {{{QStringLiteral("pivoter"), QStringLiteral("Pivot"), QStringLiteral("deux faces cylindriques : ne reste que la rotation autour de l’axe"), [this] { commencerContrainte("pivot"); }}},
          {{QStringLiteral("deplacer"), QStringLiteral("Glissière"), QStringLiteral("deux faces cylindriques : ne reste que la translation le long de l’axe"), [this] { commencerContrainte("glissiere"); }}},
          {{QStringLiteral("concentrique"), QStringLiteral("Pivot glissant"), QStringLiteral("rotation et translation le long du même axe"), [this] { commencerContrainte("concentrique"); }}},
          {{QStringLiteral("point"), QStringLiteral("Rotule"), QStringLiteral("deux faces sphériques : centres confondus"), [this] { commencerContrainte("rotule"); }}},
          {{QStringLiteral("repetition-circulaire"), QStringLiteral("Engrenage…"), QStringLiteral("deux axes : la rotation de l’un entraîne l’autre selon le rapport"), [this] { commencerContrainte("engrenage"); }}},
          {{QStringLiteral("percage"), QStringLiteral("Vis-écrou…"), QStringLiteral("la rotation fait avancer selon le pas"), [this] { commencerContrainte("vis"); }}}});
  bA->addSeparator();
  grille(bA, QStringLiteral("Manipulation"),
         {{{QStringLiteral("deplacer"), QStringLiteral("Déplacer à la souris"), QStringLiteral("tire un composant : il suit dans le respect de ses contraintes (clic à nouveau pour finir)"), [this] { basculerDeplacerSouris(); }},
           {QStringLiteral("deplacer"), QStringLiteral("Placer par valeurs"), QStringLiteral("position, rotations, déplacement relatif du composant choisi, dans le panneau"), [this] { placerParValeurs(); }}},
          {{QStringLiteral("pivoter"), QStringLiteral("Faire pivoter à la souris (axe Z)"), QStringLiteral("tire le composant : il tourne autour de l’axe Z passant par son centre"), [this] { basculerPivoterSouris(2); }},
           {QStringLiteral("pivoter"), QStringLiteral("Faire pivoter à la souris (axe X)"), QString(), [this] { basculerPivoterSouris(0); }},
           {QStringLiteral("pivoter"), QStringLiteral("Faire pivoter à la souris (axe Y)"), QString(), [this] { basculerPivoterSouris(1); }},
           {QStringLiteral("pivoter"), QStringLiteral("Pivoter de 90° autour de Z"), QString(), [this] { pivoter(2, 90); }},
           {QStringLiteral("pivoter"), QStringLiteral("Pivoter de 90° autour de X"), QString(), [this] { pivoter(0, 90); }},
           {QStringLiteral("pivoter"), QStringLiteral("Pivoter de 90° autour de Y"), QString(), [this] { pivoter(1, 90); }},
           {QStringLiteral("pivoter"), QStringLiteral("Pivoter de 180° autour de Z"), QString(), [this] { pivoter(2, 180); }},
           {QStringLiteral("pivoter"), QStringLiteral("Pivoter d’un angle"), QStringLiteral("axe X, Y ou Z et angle, dans le panneau"), [this] { pivoterParValeur(); }}},
          {{QStringLiteral("repetition-lineaire"), QStringLiteral("Copier le composant"), QStringLiteral("même pièce, posée à côté"), [this] { copier(); }}},
          {{QStringLiteral("repetition-lineaire"), QStringLiteral("Répétition linéaire"), QStringLiteral("nombre, pas et direction dans le panneau"), [this] { panneauRepetition(false); }},
           {QStringLiteral("repetition-circulaire"), QStringLiteral("Répétition circulaire"), QStringLiteral("nombre, angle et axe dans le panneau"), [this] { panneauRepetition(true); }}},
          {{QStringLiteral("symetrie"), QStringLiteral("Symétrie par rapport à YZ"), QStringLiteral("copie symétrique du composant"), [this] { symetriser(3); }},
           {QStringLiteral("symetrie"), QStringLiteral("Symétrie par rapport à XZ"), QString(), [this] { symetriser(2); }},
           {QStringLiteral("symetrie"), QStringLiteral("Symétrie par rapport à XY"), QString(), [this] { symetriser(1); }}},
          {{QStringLiteral("convertir"), QStringLiteral("Remplacer la pièce…"), QStringLiteral("par une autre pièce du projet, contraintes gardées si possible"), [this] { remplacer(); }}}});
  // Onglet Mouvement : simulation des liaisons, animation de montage
  QToolBar* bM = onglet(QStringLiteral("Mouvement"));
  gros(bM, QStringLiteral("Simulation"), {{QStringLiteral("pivoter"), QStringLiteral("Simulation cinématique"), QStringLiteral("pilote une liaison (pivot, glissière, vis) et regarde tout bouger"), [this] { panneauSimulation(); }}});
  gros(bM, QStringLiteral("Animation"), {{QStringLiteral("deplacer"), QStringLiteral("Animation de montage"), QStringLiteral("les composants arrivent un à un à leur place"), [this] { animationMontage(); }}});
  // Onglet Vérifier
  QToolBar* bV = onglet(QStringLiteral("Vérifier"));
  gros(bV, QStringLiteral("Interférences"), {{QStringLiteral("coupe"), QStringLiteral("Interférences"), QStringLiteral("composants qui se pénètrent, volume en commun"), [this] { interferences(); }}});
  gros(bV, QStringLiteral("Jeux"), {{QStringLiteral("cote"), QStringLiteral("Analyse des jeux"), QStringLiteral("distance minimale entre composants"), [this] { jeux(); }}});
  gros(bV, QStringLiteral("Libertés"), {{QStringLiteral("gerer-relations"), QStringLiteral("Degrés de liberté"), QStringLiteral("mouvements encore libres, composant par composant"), [this] { libertes(); }}});
  gros(bV, QStringLiteral("Diagnostic"), {{QStringLiteral("contraintes"), QStringLiteral("Diagnostic des contraintes"), QStringLiteral("contradictions, faces disparues, degrés de liberté"), [this] { diagnostic(); }}});
  gros(bV, QStringLiteral("Mesurer"), {{QStringLiteral("cote"), QStringLiteral("Mesurer"), QStringLiteral("distance et ΔX ΔY ΔZ, centre à centre, entraxe, angle, rayon, coordonnées"), [this] { basculerMesure(); }}});
  gros(bV, QStringLiteral("Physique"), {{QStringLiteral("masse"), QStringLiteral("Propriétés physiques"), QStringLiteral("masse, volume, centre de gravité de l’ensemble"), [this] { proprietesPhysiques(); }}});
  // Onglet Documentation
  QToolBar* bD = onglet(QStringLiteral("Documentation"));
  gros(bD, QStringLiteral("Nomenclature"), {{QStringLiteral("composants"), QStringLiteral("Nomenclature de l’assemblage"), QStringLiteral("à niveaux ou pièces seules, repères stables, propriétés et masses ; XLSX et CSV"), [this] { nomenclature(); }}});
  gros(bD, QStringLiteral("Plan"), {{QStringLiteral("plan"), QStringLiteral("Mise en plan de l’assemblage"), QStringLiteral("vues, bulles de repère reliées aux pièces, nomenclature au-dessus du cartouche ; PDF, SVG, DXF"), [this] { miseEnPlan(); }}});
  gros(bD, QStringLiteral("Vue éclatée"), {{QStringLiteral("deplacer"), QStringLiteral("Vue éclatée"), QStringLiteral("écarte les composants du centre, réglable"), [this] { panneauEclate(); }}});
  gros(bD, QStringLiteral("STEP"), {{QStringLiteral("exporter"), QStringLiteral("Exporter en STEP"), QStringLiteral("l’ensemble, une pièce par composant"), [this] { exporterStep(); }}});
  // Onglet Affichage
  QToolBar* bF = onglet(QStringLiteral("Affichage"));
  gros(bF, QStringLiteral("Masquer"), {{QStringLiteral("afficher"), QStringLiteral("Masquer ou afficher"), QStringLiteral("le composant choisi"), [this] { basculerMasque(); }}});
  gros(bF, QStringLiteral("Isoler"), {{QStringLiteral("filtre-faces"), QStringLiteral("Isoler"), QStringLiteral("seulement le composant choisi"), [this] {
         if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
         m_isoles = {m_choisi};
         reconstruire(true);
       }}});
  gros(bF, QStringLiteral("Tout afficher"), {{QStringLiteral("recadrer"), QStringLiteral("Tout afficher"), QStringLiteral("fin de l’isolement, composants masqués compris"), [this] {
         m_isoles.clear();
         if (m_indice >= 0) { const int i = m_indice; m_doc->modifier([i](Projet& p) { for (auto& c : p.assemblages[i].composants) c.masque = false; }); }
         reconstruire(true);
       }}});
  gros(bF, QStringLiteral("Transparence"),
       {{QStringLiteral("afficher"), QStringLiteral("Opaque"), QString(), [this] { m_transparence = 0; reconstruire(false); }},
        {QStringLiteral("afficher"), QStringLiteral("Transparence 25 %"), QString(), [this] { m_transparence = 0.25; reconstruire(false); }},
        {QStringLiteral("afficher"), QStringLiteral("Transparence 50 %"), QString(), [this] { m_transparence = 0.5; reconstruire(false); }},
        {QStringLiteral("afficher"), QStringLiteral("Transparence 75 %"), QString(), [this] { m_transparence = 0.75; reconstruire(false); }}});
  gros(bF, QStringLiteral("Coupe"), {{QStringLiteral("coupe"), QStringLiteral("Vue en coupe"), QStringLiteral("plan de coupe réglable, matière coupée en couleur"), [this] { vueEnCoupe(); }}});
  gros(bF, QStringLiteral("Recadrer"), {{QStringLiteral("recadrer"), QStringLiteral("Recadrer"), QStringLiteral("F"), [this] { m_vue->recadrer(); }}});
}

// ---------- Données ----------
const ResultatPiece& AtelierAssemblage::resultatPiece(int indice) {
  static const ResultatPiece vide;
  const auto& ps = m_doc->projet().pieces;
  if (indice < 0 || indice >= static_cast<int>(ps.size())) return vide;
  // Clé : empreinte du JSON de la pièce, calculée sans le réécrire en texte (une pièce importée en pèse des dizaines
  // de mégaoctets, EX-06) ; les versions périmées sont retirées au début de chaque reconstruction
  const std::size_t cle = std::hash<prom::Json>{}(ps[static_cast<size_t>(indice)]);
  auto it = m_cache.find(cle);
  if (it == m_cache.end()) it = m_cache.emplace(cle, construirePiece(pieceDepuisJson(ps[static_cast<size_t>(indice)]))).first;
  return it->second;
}

const Assemblage& AtelierAssemblage::assemblageAffiche() const { return m_apercu ? *m_apercu : m_doc->projet().assemblages[static_cast<size_t>(m_indice)]; }

TopoDS_Shape AtelierAssemblage::formeAssemblage(int indice, std::vector<char>& enCours, int profondeur) {
  // Composé des composants d'un sous-assemblage, à leur place, sous-sous-assemblages compris (AS-17) ; masqués et
  // suspendus écartés à chaque niveau ; boucles et imbrications démesurées coupées (AS-02)
  const auto& p = m_doc->projet();
  TopoDS_Compound comp;
  BRep_Builder bb;
  bb.MakeCompound(comp);
  if (!rangAssemblageValide(p, indice) || profondeur > PROFONDEUR_ASSEMBLAGE_MAX || enCours[static_cast<size_t>(indice)]) return comp;
  enCours[static_cast<size_t>(indice)] = 1;
  for (const auto& sc : p.assemblages[static_cast<size_t>(indice)].composants) {
    if (sc.supprime || sc.masque) continue;
    TopoDS_Shape g;
    if (sc.assemblage >= 0) g = placer(formeAssemblage(sc.assemblage, enCours, profondeur + 1), sc);
    else if (rangPieceValide(p, sc.piece)) g = placer(resultatPiece(sc.piece).forme, sc);
    if (!g.IsNull()) bb.Add(comp, g);
  }
  enCours[static_cast<size_t>(indice)] = 0;
  return comp;
}

TopoDS_Shape AtelierAssemblage::formeComposant(int k) {
  if (!assemblageValide()) return {};
  return formeComposantDe(assemblageAffiche(), k);
}

TopoDS_Shape AtelierAssemblage::formeComposantDe(const Assemblage& a, int k, std::vector<std::pair<TopoDS_Shape, TopoDS_Shape>>* miroir) {
  const auto& p = m_doc->projet();
  if (!assemblageValide() || k < 0 || k >= static_cast<int>(a.composants.size())) return {};
  const auto& c = a.composants[static_cast<size_t>(k)];
  TopoDS_Shape f;
  if (c.assemblage >= 0) {
    std::vector<char> enCours(p.assemblages.size(), 0);
    enCours[static_cast<size_t>(m_indice)] = 1;  // l'assemblage ouvert ne peut pas se contenir
    f = formeAssemblage(c.assemblage, enCours, 1);
  } else if (rangPieceValide(p, c.piece)) {
    f = resultatPiece(c.piece).forme;
  }
  if (f.IsNull()) return f;
  if (!c.miroir) return f.Moved(TopLoc_Location(placementComposant(c)));
  // Copie symétrique : nouvelle géométrie, dont chaque face garde le lien vers la face nommée de la pièce
  BRepBuilderAPI_Transform tr(f, placementComposant(c), true);
  if (miroir)
    for (TopExp_Explorer ex(f, TopAbs_FACE); ex.More(); ex.Next()) {
      const TopoDS_Shape image = tr.ModifiedShape(ex.Current());
      if (!image.IsNull()) miroir->push_back({image, ex.Current()});
    }
  return tr.Shape();
}

std::string AtelierAssemblage::signaturePieces() const {
  const auto& p = m_doc->projet();
  if (!assemblageValide()) return {};
  std::size_t h = 1469598103934665603ULL;
  for (const auto& [ip, q] : quantitesPieces(p, m_indice)) {
    (void)q;
    if (!rangPieceValide(p, ip)) continue;
    h ^= std::hash<std::string>{}(p.pieces[static_cast<size_t>(ip)].dump()) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  }
  // Sous-assemblages utilisés (A3) : leurs composants et contraintes ; un sous-assemblage modifié dans son atelier
  // relance la résolution du parent, dont les contraintes visent ses faces
  for (int si : ordreAssemblages(p.assemblages, m_indice)) {
    if (si == m_indice) continue;
    std::string t;
    const auto& sa = p.assemblages[static_cast<size_t>(si)];
    for (const auto& c : sa.composants)
      t += std::to_string(c.piece) + ',' + std::to_string(c.assemblage) + ',' + std::to_string(c.x) + ',' + std::to_string(c.y) + ',' + std::to_string(c.z) + ',' + std::to_string(c.rx) + ',' +
           std::to_string(c.ry) + ',' + std::to_string(c.rz) + ',' + std::to_string(c.miroir) + ',' + std::to_string(c.supprime) + ';';
    for (const auto& c : sa.contraintes)
      t += c.type + ',' + std::to_string(c.a) + ',' + std::to_string(c.b) + ',' + c.refA + ',' + c.refB + ',' + std::to_string(c.valeur) + ',' + std::to_string(c.inverse) + ',' +
           std::to_string(c.suspendue) + ',' + std::to_string(c.alignement) + ';';
    h ^= std::hash<std::string>{}(t) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  }
  return std::to_string(h);
}

int AtelierAssemblage::composantSous(const TopoDS_Shape& s) const {
  for (size_t k = 0; k < m_formes.size(); ++k)
    for (TopExp_Explorer ex(m_formes[k], s.ShapeType()); ex.More(); ex.Next())
      if (ex.Current().IsSame(s)) return static_cast<int>(k);
  return -1;
}

std::string AtelierAssemblage::nomFaceSous(const TopoDS_Shape& face, int k) {
  if (!composantValide(k) || face.IsNull() || face.ShapeType() != TopAbs_FACE) return {};
  const auto& c = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(k)];
  if (c.assemblage >= 0) return cheminElementSous(face, c.assemblage, placementComposant(c), 0);  // AS-16
  if (!rangPieceValide(m_doc->projet(), c.piece)) return {};
  const ResultatPiece& r = resultatPiece(c.piece);
  // Composant symétrique : la face affichée est une copie, retrouvée par la correspondance tenue à l'affichage (AS-09)
  if (k < static_cast<int>(m_facesMiroir.size()) && !m_facesMiroir[static_cast<size_t>(k)].empty()) {
    for (const auto& [image, origine] : m_facesMiroir[static_cast<size_t>(k)])
      if (image.IsPartner(face)) return r.nomFace(TopoDS::Face(origine));
    return {};
  }
  // On retire seulement le placement d'affichage du composant : les placements propres au fichier importé (pièces
  // d'un STEP d'assemblage) restent, sans quoi aucune face n'est reconnue (AS-21)
  const TopLoc_Location affichage = k < static_cast<int>(m_locAffichage.size()) ? m_locAffichage[static_cast<size_t>(k)] : TopLoc_Location(placementComposant(c));
  const std::string nom = r.nomFace(TopoDS::Face(face.Located(affichage.Inverted() * face.Location())));
  return nom.empty() ? r.nomFace(TopoDS::Face(face.Located(TopLoc_Location()))) : nom;
}

// Arête ou sommet d'un composant (A2) : même retrait du placement d'affichage que pour une face
std::string AtelierAssemblage::nomElementSous(const TopoDS_Shape& s, int k) {
  if (s.IsNull() || !composantValide(k)) return {};
  if (s.ShapeType() == TopAbs_FACE) return nomFaceSous(s, k);
  if (s.ShapeType() != TopAbs_EDGE && s.ShapeType() != TopAbs_VERTEX) return {};
  const auto& c = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(k)];
  if (c.assemblage >= 0) return cheminElementSous(s, c.assemblage, placementComposant(c), 0);
  if (!rangPieceValide(m_doc->projet(), c.piece)) return {};
  // Composant symétrique : seules ses faces ont une correspondance tenue à l'affichage
  if (k < static_cast<int>(m_facesMiroir.size()) && !m_facesMiroir[static_cast<size_t>(k)].empty()) return {};
  const ResultatPiece& r = resultatPiece(c.piece);
  const TopLoc_Location affichage = k < static_cast<int>(m_locAffichage.size()) ? m_locAffichage[static_cast<size_t>(k)] : TopLoc_Location(placementComposant(c));
  auto nommer = [&](const TopoDS_Shape& x) { return x.ShapeType() == TopAbs_EDGE ? r.nomArete(TopoDS::Edge(x)) : r.nomSommet(TopoDS::Vertex(x)); };
  const std::string nom = nommer(s.Located(affichage.Inverted() * s.Location()));
  return nom.empty() ? nommer(s.Located(TopLoc_Location())) : nom;
}

namespace {
// Empreinte géométrique d'une face (aire, centre), d'une arête (longueur, centre) ou d'un sommet (point)
std::pair<double, gp_Pnt> empreinte(const TopoDS_Shape& s) {
  if (s.ShapeType() == TopAbs_VERTEX) return {0.0, BRep_Tool::Pnt(TopoDS::Vertex(s))};
  GProp_GProps g;
  if (s.ShapeType() == TopAbs_FACE) BRepGProp::SurfaceProperties(s, g);
  else BRepGProp::LinearProperties(s, g);
  return {g.Mass(), g.CentreOfMass()};
}
}  // namespace

// Élément cliqué dans un sous-assemblage : chemin « @k/…/nom » trouvé par la géométrie (aire ou longueur et centre,
// placements cumulés), qui reconnaît aussi les copies symétriques (faces) ; arêtes et sommets aussi (A2)
std::string AtelierAssemblage::cheminElementSous(const TopoDS_Shape& element, int sousAssemblage, const gp_Trsf& cumul, int profondeur) {
  const auto& p = m_doc->projet();
  if (!rangAssemblageValide(p, sousAssemblage) || profondeur > 8 || element.IsNull()) return {};
  const TopAbs_ShapeEnum genre = element.ShapeType();
  if (genre != TopAbs_FACE && genre != TopAbs_EDGE && genre != TopAbs_VERTEX) return {};
  const auto [mesure, centre] = empreinte(element);
  const auto& s = p.assemblages[static_cast<size_t>(sousAssemblage)];
  for (int k = 0; k < static_cast<int>(s.composants.size()); ++k) {
    const auto& sc = s.composants[static_cast<size_t>(k)];
    if (sc.supprime) continue;
    const gp_Trsf T = cumul * placementComposant(sc);
    if (sc.assemblage >= 0) {
      const std::string r = cheminElementSous(element, sc.assemblage, T, profondeur + 1);
      if (!r.empty()) return "@" + std::to_string(k) + "/" + r;
      continue;
    }
    if (!rangPieceValide(p, sc.piece)) continue;
    const ResultatPiece& r = resultatPiece(sc.piece);
    for (TopExp_Explorer ex(r.forme, genre); ex.More(); ex.Next()) {
      const auto [m, c] = empreinte(ex.Current());
      if (std::abs(m - mesure) > 1e-6 * std::max(1.0, mesure)) continue;
      if (c.Transformed(T).Distance(centre) > 1e-4) continue;
      const std::string nom = genre == TopAbs_FACE ? r.nomFace(TopoDS::Face(ex.Current())) : genre == TopAbs_EDGE ? r.nomArete(TopoDS::Edge(ex.Current())) : r.nomSommet(TopoDS::Vertex(ex.Current()));
      if (!nom.empty()) return "@" + std::to_string(k) + "/" + nom;
    }
  }
  return {};
}

bool AtelierAssemblage::elementClique(const TopoDS_Shape& s, int& k, std::string& nom) {
  if (const std::string plan = m_vue->planSelectionne(); !plan.empty()) {
    k = COMPOSANT_ASSEMBLAGE;
    nom = std::string(PREFIXE_REFERENCE_ASSEMBLAGE) + plan;
    return true;
  }
  if (const std::string axe = m_vue->axeSelectionne(); !axe.empty()) {
    k = COMPOSANT_ASSEMBLAGE;
    nom = std::string(PREFIXE_REFERENCE_ASSEMBLAGE) + axe;
    return true;
  }
  k = composantSous(s);
  if (k < 0) return false;
  nom = nomElementSous(s, k);
  return true;
}

QString AtelierAssemblage::nomCote(const ContrainteAssemblage& c, bool coteB) const {
  if (referenceAssemblage(coteB ? c.refB : c.refA)) return QStringLiteral("Assemblage");
  return nomComposant(coteB ? c.b : c.a);
}

QString AtelierAssemblage::libelleReference(int k, const std::string& ref) const {
  auto repere = [](const std::string& q, const QString& de) {
    if (q == "O") return QStringLiteral("Origine %1").arg(de);
    if (q.size() == 2) return QStringLiteral("Plan %1 %2").arg(QString::fromStdString(q), de);
    return QStringLiteral("Axe %1 %2").arg(QString::fromStdString(q), de);
  };
  if (referenceAssemblage(ref)) return repere(ref.substr(std::string(PREFIXE_REFERENCE_ASSEMBLAGE).size()), QStringLiteral("de l’assemblage"));
  const QString comp = nomComposant(k);
  // Chemin dans un sous-assemblage : on décrit l'élément visé
  std::string nom = ref;
  if (const auto barre = nom.rfind('/'); nom.rfind("@", 0) == 0 && barre != std::string::npos) nom = nom.substr(barre + 1);
  if (nom.rfind("base:", 0) == 0) return repere(nom.substr(5), QStringLiteral("de %1").arg(comp));
  if (nom.rfind("plan:", 0) == 0) return QStringLiteral("Plan de référence n° %1 de %2").arg(QString::fromStdString(nom.substr(5)), comp);
  if (nom.rfind("axe:", 0) == 0) return QStringLiteral("Axe de référence n° %1 de %2").arg(QString::fromStdString(nom.substr(4)), comp);
  if (nom.rfind("point:", 0) == 0) return QStringLiteral("Point de référence n° %1 de %2").arg(QString::fromStdString(nom.substr(6)), comp);
  if (nom.rfind("S:", 0) == 0) return QStringLiteral("Sommet de %1").arg(comp);
  if (nom.find('|') != std::string::npos) return QStringLiteral("Arête de %1").arg(comp);
  return QStringLiteral("Face de %1").arg(comp);
}

QString AtelierAssemblage::nomComposant(int k) const {
  const auto& p = m_doc->projet();
  if (!composantValide(k)) return QStringLiteral("?");
  const auto& c = p.assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(k)];
  if (c.assemblage >= 0) return rangAssemblageValide(p, c.assemblage) ? QString::fromStdString(p.assemblages[static_cast<size_t>(c.assemblage)].nom) : QStringLiteral("Sous-assemblage manquant");
  return rangPieceValide(p, c.piece) ? QString::fromStdString(p.pieces[static_cast<size_t>(c.piece)].value("nom", std::string("Pièce"))) : QStringLiteral("Pièce manquante");
}

void AtelierAssemblage::ouvrir(int a, int composant) {
  // Rien de l'assemblage précédent ne survit : aperçu et panneau de simulation, animation, éclatement,
  // sélections et contrainte commencée (AS-15, AS-26)
  arreterAnimation();
  if (m_panneauSimulation) { m_panneauSimulation->deleteLater(); m_panneauSimulation = nullptr; }
  m_apercu.reset();
  m_eclatement = 0;
  if (m_panneauEclate) m_panneauEclate->hide();
  m_indice = rangAssemblageValide(m_doc->projet(), a) ? a : -1;
  oublierSelection();
  m_barre->hide();
  m_dernierEtat.reset();
  m_problemesSous.clear();
  m_typeContrainte.clear();
  majRepere();
  // Pièces modifiées depuis la dernière résolution (retour de « Modifier la pièce ») : on résout d'abord (AS-12)
  if (assemblageValide()) {
    if (m_signature.empty()) m_signature = signaturePieces();  // première ouverture : rien n'a changé
    else if (signaturePieces() != m_signature) resoudre();
  }
  reconstruire(true);
  if (composant >= 0) choisirComposant(composant);
  else majProprietes();
}

void AtelierAssemblage::reconstruire(bool recadrer) {
  majMenuInserer();
  {
    // Cache : seules les versions actuelles des pièces restent (aucune référence n'est tenue entre deux reconstructions)
    std::set<std::size_t> actuelles;
    for (const auto& j : m_doc->projet().pieces) actuelles.insert(std::hash<prom::Json>{}(j));
    for (auto it = m_cache.begin(); it != m_cache.end();) it = actuelles.count(it->first) ? std::next(it) : m_cache.erase(it);
  }
  m_formes.clear();
  m_formesPlacees.clear();
  m_locAffichage.clear();
  m_facesMiroir.clear();
  const auto& p = m_doc->projet();
  if (m_indice < 0 || m_indice >= static_cast<int>(p.assemblages.size())) { m_etat->setText(QStringLiteral("Aucun assemblage ouvert.")); return; }
  const Assemblage& a = assemblageAffiche();
  const Assemblage& reel = p.assemblages[static_cast<size_t>(m_indice)];
  m_facesMiroir.resize(a.composants.size());
  // Vue éclatée : chaque composant s'écarte du centre de l'ensemble
  Bnd_Box tout;
  std::vector<TopoDS_Shape> brutes;
  for (int k = 0; k < static_cast<int>(a.composants.size()); ++k) {
    TopoDS_Shape f;
    try { f = formeComposantDe(a, k, &m_facesMiroir[static_cast<size_t>(k)]); } catch (...) { f.Nullify(); m_etat->setText(QStringLiteral("<span style='color:#C2402E'>Le composant n° %1 ne peut pas être placé.</span>").arg(k + 1)); }
    brutes.push_back(f);
    if (!f.IsNull()) BRepBndLib::Add(f, tout);
    // Forme à sa place réelle (sans aperçu de simulation ni éclatement), nulle pour un composant suspendu (AS-13)
    TopoDS_Shape placee;
    if (k < static_cast<int>(reel.composants.size()) && !reel.composants[static_cast<size_t>(k)].supprime) {
      if (!m_apercu) placee = f;
      else try { placee = formeComposantDe(reel, k); } catch (...) { placee.Nullify(); }
    }
    m_formesPlacees.push_back(placee);
  }
  gp_Pnt centre(0, 0, 0);
  if (!tout.IsVoid()) { double x0, y0, z0, x1, y1, z1; tout.Get(x0, y0, z0, x1, y1, z1); centre = gp_Pnt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2); }
  TopoDS_Compound ensemble;
  BRep_Builder bb;
  bb.MakeCompound(ensemble);
  std::vector<std::pair<TopoDS_Shape, QColor>> couleurs;
  int visibles = 0;
  for (int k = 0; k < static_cast<int>(a.composants.size()); ++k) {
    const auto& c = a.composants[static_cast<size_t>(k)];
    TopoDS_Shape f = brutes[static_cast<size_t>(k)];
    const double ecl = m_eclatement + (k < static_cast<int>(m_eclatementComposant.size()) ? m_eclatementComposant[static_cast<size_t>(k)] : 0);
    TopLoc_Location locAffichage(placementComposant(c));
    if (!f.IsNull() && ecl > 0) {
      Bnd_Box b;
      BRepBndLib::Add(f, b);
      double x0, y0, z0, x1, y1, z1;
      b.Get(x0, y0, z0, x1, y1, z1);
      const gp_Vec ecart(centre, gp_Pnt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2));
      gp_Trsf t;
      t.SetTranslation(ecart * ecl * 1.5);
      f = f.Moved(TopLoc_Location(t));
      locAffichage = TopLoc_Location(t) * locAffichage;
    }
    m_formes.push_back(f);
    m_locAffichage.push_back(locAffichage);
    const bool cache = c.masque || c.supprime || (!m_isoles.empty() && !m_isoles.count(k));
    if (f.IsNull() || cache) continue;
    bb.Add(ensemble, f);
    ++visibles;
    QColor col = PALETTE[k % 6];
    if (c.assemblage < 0 && c.piece >= 0 && c.piece < static_cast<int>(p.pieces.size())) {
      const std::string cp = p.pieces[static_cast<size_t>(c.piece)].value("couleur", std::string());
      if (!cp.empty() && cp != "#C9CFDF") col = QColor(QString::fromStdString(cp));
    }
    if (k == m_choisi || m_multi.count(k)) col = QColor(0x6E, 0x9B, 0xFF);
    couleurs.push_back({f, col});
    // Apparences par face de la pièce (pas sur le composant choisi, qui reste uni pour se voir)
    if (k != m_choisi && !m_multi.count(k) && c.assemblage < 0 && rangPieceValide(p, c.piece)) {
      const Json& jp = p.pieces[static_cast<size_t>(c.piece)];
      if (jp.contains("couleursFaces") && jp["couleursFaces"].is_object() && !jp["couleursFaces"].empty()) {
        std::map<std::string, std::string> parNom;
        for (const auto& [n, v] : jp["couleursFaces"].items()) if (v.is_string()) parNom[n] = v.get<std::string>();
        const ResultatPiece& rp = resultatPiece(c.piece);
        const TopoDS_Shape& brute = brutes[static_cast<size_t>(k)];
        for (const auto& [face, hexa] : facesColorees(parNom, rp)) {
          TopoDS_Shape image;
          if (!c.miroir) image = face.Moved(f.Location() * rp.forme.Location().Inverted());
          else
            for (const auto& [img, src] : m_facesMiroir[static_cast<size_t>(k)])
              if (src.IsSame(face)) { image = img.Moved(f.Location() * brute.Location().Inverted()); break; }
          if (!image.IsNull()) couleurs.push_back({image, QColor(QString::fromStdString(hexa))});
        }
      }
    }
  }
  m_vue->setCouleursSousFormes(couleurs);
  m_vue->setApparence(QColor(0xC9, 0xCF, 0xDF), m_transparence);
  ResultatPiece r;
  if (visibles) r.forme = ensemble;
  m_vue->afficher(r, recadrer);
  // L'échec de la dernière résolution reste affiché tant qu'il dure (AS-22)
  if (!m_mesure && !m_deplacer && m_typeContrainte.empty()) {
    if (m_dernierEtat && !m_dernierEtat->resolu && !m_dernierEtat->message.empty())
      m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(QString::fromStdString(m_dernierEtat->message).toHtmlEscaped()));
    else
      m_etat->setText(a.composants.empty() ? QStringLiteral("<b>%1</b> : vide. « Composant » insère une pièce, un sous-assemblage ou un fichier.").arg(QString::fromStdString(a.nom))
                                           : QStringLiteral("<b>%1</b> · %2 composant(s) · %3 contrainte(s)").arg(QString::fromStdString(a.nom)).arg(a.composants.size()).arg(a.contraintes.size()));
  }
}

void AtelierAssemblage::majMenuInserer() {
  QMenu* menu = m_inserer->menu();
  menu->clear();
  const auto& p = m_doc->projet();
  QMenu* pieces = menu->addMenu(icone(QStringLiteral("piece")), QStringLiteral("Pièce du projet"));
  if (p.pieces.empty()) pieces->addAction(QStringLiteral("Aucune pièce : « Nouvelle pièce » en crée une"))->setEnabled(false);
  for (size_t i = 0; i < p.pieces.size(); ++i)
    pieces->addAction(icone(QStringLiteral("piece")), QString::fromStdString(p.pieces[i].value("nom", std::string("Pièce"))), this, [this, i] { insererPiece(static_cast<int>(i), true); });
  QMenu* sous = menu->addMenu(icone(QStringLiteral("assemblage")), QStringLiteral("Sous-assemblage"));
  int nSous = 0;
  for (size_t a = 0; a < p.assemblages.size(); ++a) {
    if (static_cast<int>(a) == m_indice) continue;
    sous->addAction(icone(QStringLiteral("assemblage")), QString::fromStdString(p.assemblages[a].nom), this, [this, a] { insererSousAssemblage(static_cast<int>(a)); });
    ++nSous;
  }
  if (!nSous) sous->addAction(QStringLiteral("Aucun autre assemblage dans le projet"))->setEnabled(false);
  menu->addSeparator();
  menu->addAction(icone(QStringLiteral("ouvrir")), QStringLiteral("Depuis un fichier (STEP, IGES, BREP, STL)…"), this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Insérer une pièce"), QString(), QStringLiteral("Fichiers de CAO (*.step *.stp *.iges *.igs *.brep *.brp *.stl)"));
    if (chemin.isEmpty()) return;
    QString err;
    if (!insererFichier(chemin, &err)) QMessageBox::warning(this, QStringLiteral("Insertion impossible"), err);
  });
  menu->addAction(icone(QStringLiteral("ouvrir")), QStringLiteral("Depuis un autre projet Prométhée…"), this, [this] {
    const QString chemin = QFileDialog::getOpenFileName(this, QStringLiteral("Pièce d’un autre projet"), QString(), QStringLiteral("Fichiers Prométhée (*.prom *.json)"));
    if (chemin.isEmpty()) return;
    try {
      const Projet autre = lireProjet(chemin.toStdString());
      if (autre.pieces.empty()) { QMessageBox::information(this, QStringLiteral("Aucune pièce"), QStringLiteral("Ce projet ne contient aucune pièce.")); return; }
      QStringList noms;
      for (size_t i = 0; i < autre.pieces.size(); ++i) noms << QStringLiteral("%1 · %2").arg(i + 1).arg(QString::fromStdString(autre.pieces[i].value("nom", std::string("Pièce"))));
      auto inserer = [this, chemin](int n) {
        QString err;
        if (!insererDepuisProjet(chemin, n, &err)) m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(err.toHtmlEscaped()));
      };
      if (noms.size() == 1) { inserer(0); return; }
      // Choix dans le panneau latéral (U1)
      auto* liste = new QListWidget;
      liste->setObjectName(QStringLiteral("piecesAutreProjet"));
      liste->addItems(noms);
      liste->setCurrentRow(0);
      m_panneau->ouvrir(QStringLiteral("autreProjet"), QStringLiteral("Pièce de « %1 »").arg(QFileInfo(chemin).fileName()), QStringLiteral("ouvrir"), liste, PanneauLateral::Boutons::ValiderAnnuler,
                        [liste, inserer] {
                          if (liste->currentRow() < 0) return false;
                          inserer(liste->currentRow());
                          return true;
                        });
    } catch (const std::exception& e) {
      QMessageBox::warning(this, QStringLiteral("Lecture impossible"), QString::fromUtf8(e.what()));
    }
  });
}

// ---------- Insertion ----------
void AtelierAssemblage::insererPiece(int i, bool auCurseur) {
  if (!assemblageValide() || !rangPieceValide(m_doc->projet(), i)) return;
  double x = 0;
  for (const auto& f : m_formesPlacees) {
    Bnd_Box b;
    if (!f.IsNull()) BRepBndLib::Add(f, b);
    if (!b.IsVoid()) { double x0, y0, z0, x1, y1, z1; b.Get(x0, y0, z0, x1, y1, z1); x = std::max(x, x1 + 10); }
  }
  const int a = m_indice;
  auto ajouter = [a, i, x](Projet& p) { if (a >= 0 && a < static_cast<int>(p.assemblages.size())) p.assemblages[static_cast<size_t>(a)].composants.push_back({i, x, 0, 0, 0, 0, 0}); };
  // Au curseur : l'insertion et la pose ne font qu'un pas d'historique
  if (auCurseur) { m_doc->finGeste(); m_doc->debutGeste(); m_doc->pendantGeste(ajouter); }
  else m_doc->modifier(ajouter);
  reconstruire(!auCurseur);
  const int k = static_cast<int>(m_doc->projet().assemblages[static_cast<size_t>(a)].composants.size()) - 1;
  if (auCurseur) commencerPose(k);
  else choisirComposant(k);
}

// Insertion au curseur (M9) : le composant suit la souris, posé au sol (plan z = 0) par le centre du dessous de sa
// boîte ; un clic le pose, Échap le laisse à droite de l'ensemble
void AtelierAssemblage::commencerPose(int k) {
  if (!assemblageValide() || k < 0 || k >= static_cast<int>(m_formesPlacees.size()) || m_formesPlacees[static_cast<size_t>(k)].IsNull()) { m_doc->finGeste(); return; }
  const auto& c = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(k)];
  Bnd_Box b;
  BRepBndLib::Add(m_formesPlacees[static_cast<size_t>(k)], b);
  double x0, y0, z0, x1, y1, z1;
  b.Get(x0, y0, z0, x1, y1, z1);
  m_poseDepart = {c.x, c.y, c.z};
  m_poseDecalage = {(x0 + x1) / 2 - c.x, (y0 + y1) / 2 - c.y, z0 - c.z};
  m_pose = k;
  m_choisi = k;
  m_vue->setSuiviCurseur(true, 0);
  m_etat->setText(QStringLiteral("<b>Insertion</b> : le composant suit le curseur, posé au sol ; clique pour le poser, Échap pour le laisser à droite."));
}

void AtelierAssemblage::suivrePose(double x, double y, double z) {
  if (m_pose < 0 || !assemblageValide()) return;
  const int a = m_indice, k = m_pose;
  const std::array<double, 3> o = {x - m_poseDecalage[0], y - m_poseDecalage[1], z - m_poseDecalage[2]};
  m_doc->pendantGeste([a, k, o](Projet& p) {
    auto& v = p.assemblages[static_cast<size_t>(a)].composants;
    if (k < static_cast<int>(v.size())) { v[static_cast<size_t>(k)].x = o[0]; v[static_cast<size_t>(k)].y = o[1]; v[static_cast<size_t>(k)].z = o[2]; }
  });
  reconstruire(false);
}

void AtelierAssemblage::finirPose(bool poser) {
  if (m_pose < 0) return;
  const int k = m_pose;
  if (!poser) {
    const int a = m_indice;
    const auto d = m_poseDepart;
    m_doc->pendantGeste([a, k, d](Projet& p) {
      auto& v = p.assemblages[static_cast<size_t>(a)].composants;
      if (k < static_cast<int>(v.size())) { v[static_cast<size_t>(k)].x = d[0]; v[static_cast<size_t>(k)].y = d[1]; v[static_cast<size_t>(k)].z = d[2]; }
    });
  }
  m_pose = -1;
  m_vue->setSuiviCurseur(false);
  m_doc->finGeste();
  m_vue->viderSelection();
  choisirComposant(k);
  m_etat->setText(poser ? QStringLiteral("Composant posé ; contrains-le ou déplace-le.") : QStringLiteral("Composant laissé à droite de l’ensemble."));
}

void AtelierAssemblage::insererSousAssemblage(int s) {
  if (!assemblageValide() || !rangAssemblageValide(m_doc->projet(), s) || s == m_indice) return;
  // Pas de boucle : le sous-assemblage ne doit pas contenir l'assemblage courant (parcours sûr, AS-02)
  if (contientAssemblage(m_doc->projet(), s, m_indice)) { m_etat->setText(QStringLiteral("Impossible : cet assemblage contient déjà celui-ci.")); return; }
  double x = 0;
  for (const auto& f : m_formesPlacees) {
    Bnd_Box b;
    if (!f.IsNull()) BRepBndLib::Add(f, b);
    if (!b.IsVoid()) { double x0, y0, z0, x1, y1, z1; b.Get(x0, y0, z0, x1, y1, z1); x = std::max(x, x1 + 10); }
  }
  const int a = m_indice;
  m_doc->modifier([a, s, x](Projet& p) {
    ComposantAssemblage c{0, x, 0, 0, 0, 0, 0};
    c.assemblage = s;
    p.assemblages[static_cast<size_t>(a)].composants.push_back(c);
  });
  reconstruire(true);
}

bool AtelierAssemblage::insererFichier(const QString& chemin, QString* erreur, ModeImport mode) {
  if (!assemblageValide()) { if (erreur) *erreur = QStringLiteral("Aucun assemblage ouvert."); return false; }
  ImportCAO imp;
  try {
    imp = lireCAOStructure(chemin.toStdString());
  } catch (const std::exception& e) {
    if (erreur) *erreur = QString::fromUtf8(e.what());
    return false;
  }
  const std::string source = QFileInfo(chemin).fileName().toStdString();
  if (imp.estAssemblage() && mode == ModeImport::Demander) {
    QMessageBox q(QMessageBox::Question, QStringLiteral("Importer un assemblage"),
                  QStringLiteral("« %1 » contient %2 pièce(s) différente(s) en %3 instance(s), sur %4 niveau(x).")
                      .arg(QString::fromStdString(imp.racine.nom).toHtmlEscaped()).arg(imp.produits.size()).arg(imp.instances()).arg(imp.niveaux()),
                  QMessageBox::Cancel, this);
    q.setInformativeText(QStringLiteral("En assemblage, chaque pièce garde son nom et sa couleur, se met en plan et se compte dans la nomenclature ; "
                                        "en une seule pièce, tout devient un corps importé."));
    QPushButton* structure = q.addButton(QStringLiteral("Assemblage"), QMessageBox::AcceptRole);
    QPushButton* une = q.addButton(QStringLiteral("Une seule pièce"), QMessageBox::AcceptRole);
    q.setDefaultButton(structure);
    q.exec();
    if (q.clickedButton() == structure) mode = ModeImport::Structure;
    else if (q.clickedButton() == une) mode = ModeImport::UnePiece;
    else return true;  // abandon voulu : rien n'est inséré, ce n'est pas une erreur
  }
  // Une seule pièce : le fichier est posé à plat (instances placées), couleurs reportées face par face
  if (!imp.estAssemblage() || mode == ModeImport::UnePiece) {
    const std::string nom = imp.produits.size() == 1 ? imp.produits.front().nom : imp.racine.nom;
    const Piece pc = pieceDepuisProduit(imp.aplati(nom), source);
    int indice = 0;
    m_doc->modifier([&](Projet& p) { p.pieces.push_back(pieceVersJson(pc)); indice = static_cast<int>(p.pieces.size()) - 1; });
    insererPiece(indice);
    return true;
  }
  // Assemblage : une pièce par produit, un sous-assemblage par niveau du fichier. Dans un assemblage vide, le contenu
  // de la racine y est posé directement (et l'assemblage prend son nom s'il n'en avait pas) ; sinon la racine
  // devient un sous-assemblage, posé à côté de ce qui existe.
  const int a = m_indice;
  const bool vide = m_doc->projet().assemblages[static_cast<size_t>(a)].composants.empty();
  int racine = -1;
  m_doc->modifier([&](Projet& p) {
    racine = ajouterImportAuProjet(p, imp, source);
    if (vide && racine == static_cast<int>(p.assemblages.size()) - 1) {
      Assemblage& cible = p.assemblages[static_cast<size_t>(a)];
      cible.composants = p.assemblages[static_cast<size_t>(racine)].composants;
      if (cible.nom == "Assemblage" || cible.nom.empty()) cible.nom = p.assemblages[static_cast<size_t>(racine)].nom;
      p.assemblages.pop_back();
      racine = -1;
    }
  });
  if (racine >= 0) insererSousAssemblage(racine);
  else reconstruire(true);
  int sous = 0;
  std::function<void(const NoeudImporte&)> compter = [&](const NoeudImporte& n) { for (const auto& e : n.enfants) if (e.assemblage()) { ++sous; compter(e); } };
  compter(imp.racine);
  m_etat->setText(QStringLiteral("« %1 » importé : %2 pièce(s), %3 instance(s), %4 sous-assemblage(s).")
                      .arg(QString::fromStdString(imp.racine.nom).toHtmlEscaped()).arg(imp.produits.size()).arg(imp.instances()).arg(sous));
  return true;
}

bool AtelierAssemblage::insererDepuisProjet(const QString& chemin, int indicePiece, QString* erreur) {
  try {
    const Projet autre = lireProjet(chemin.toStdString());
    if (indicePiece < 0 || indicePiece >= static_cast<int>(autre.pieces.size())) { if (erreur) *erreur = QStringLiteral("Pièce introuvable dans ce projet."); return false; }
    int indice = 0;
    m_doc->modifier([&](Projet& p) { p.pieces.push_back(autre.pieces[static_cast<size_t>(indicePiece)]); indice = static_cast<int>(p.pieces.size()) - 1; });
    insererPiece(indice);
    return true;
  } catch (const std::exception& e) {
    if (erreur) *erreur = QString::fromUtf8(e.what());
    return false;
  }
}

// ---------- Choix et panneau ----------
void AtelierAssemblage::choisirComposant(int k) {
  m_choisi = k;
  m_contrainteChoisie = -1;
  reconstruire(false);
  majProprietes();
}

void AtelierAssemblage::choisirContrainte(int k) {
  m_contrainteChoisie = k;
  m_choisi = -1;
  majProprietes();
}

void AtelierAssemblage::modifierComposant(int k, const std::function<void(ComposantAssemblage&)>& f) {
  if (m_indice < 0 || k < 0) return;
  const int i = m_indice;
  m_doc->modifier([i, k, f](Projet& p) { if (k < static_cast<int>(p.assemblages[static_cast<size_t>(i)].composants.size())) f(p.assemblages[static_cast<size_t>(i)].composants[static_cast<size_t>(k)]); });
}

void AtelierAssemblage::majProprietes() {
  auto* page = new QWidget;
  page->setObjectName(QStringLiteral("panneauAssemblage"));
  page->setStyleSheet(QString::fromLatin1(STYLE));
  auto* col = new QVBoxLayout(page);
  const auto& p = m_doc->projet();
  auto entete = [&](const QString& ic, const QString& titre, const QString& sous) {
    auto* h = new QHBoxLayout;
    auto* l = new QLabel;
    l->setPixmap(icone(ic).pixmap(26, 26));
    h->addWidget(l);
    h->addWidget(new QLabel(QStringLiteral("<span style='font-size:15px; font-weight:700; color:#10142A'>%1</span><br><span style='color:#6B7393'>%2</span>").arg(titre.toHtmlEscaped(), sous)), 1);
    col->addLayout(h);
  };
  if (m_indice < 0 || m_indice >= static_cast<int>(p.assemblages.size())) { remplacerPage(m_props, page); return; }
  const Assemblage& as = p.assemblages[static_cast<size_t>(m_indice)];
  if (m_choisi >= 0 && m_choisi < static_cast<int>(as.composants.size())) {
    const auto& c = as.composants[static_cast<size_t>(m_choisi)];
    const bool fixe = c.fixe || m_choisi == 0;
    entete(c.assemblage >= 0 ? QStringLiteral("assemblage") : QStringLiteral("piece"), nomComposant(m_choisi), QStringLiteral("Composant n° %1").arg(m_choisi + 1));
    // État : fixé ou mobile, et les mouvements encore libres
    std::vector<ResultatPiece> pieces;
    for (size_t i = 0; i < p.pieces.size(); ++i) pieces.push_back(resultatPiece(static_cast<int>(i)));
    const AnalyseAssemblage an = analyserAssemblage(as, pieces, &m_doc->projet().assemblages);
    // Nombre de mouvements encore possibles du composant (rang du noyau restreint), chacun décrit comme un torseur
    // dans le repère de l'assemblage (AS-07)
    const int nLibres = m_choisi < static_cast<int>(an.libertes.size()) ? an.libertes[static_cast<size_t>(m_choisi)] : 0;
    QStringList mouvements;
    if (m_choisi < static_cast<int>(an.mouvements.size()))
      for (const auto& m : an.mouvements[static_cast<size_t>(m_choisi)]) mouvements << QString::fromStdString(m).toHtmlEscaped();
    auto* etat = new QLabel(fixe ? QStringLiteral("🔒 <b>Fixé</b> : ne bouge pas.")
                                 : nLibres == 0 ? QStringLiteral("🔓 Mobile, <span style='color:#2E8B57'><b>entièrement contraint</b></span>.")
                                                : QStringLiteral("🔓 Mobile — <b>%1 mouvement(s) encore possible(s)</b> :<br>· %2").arg(nLibres).arg(mouvements.join(QStringLiteral("<br>· "))));
    etat->setWordWrap(true);
    col->addWidget(etat);
    // Position, modifiable sur place
    auto* g = new QGroupBox(QStringLiteral("Position"));
    auto* fl = new QFormLayout(g);
    Unites u;
    const double v[6] = {c.x, c.y, c.z, c.rx, c.ry, c.rz};
    static const char* libelles[] = {"X", "Y", "Z", "Rotation X", "Rotation Y", "Rotation Z"};
    for (int d = 0; d < 6; ++d) {
      auto* ch = new ChampValeur(d >= 3, u);
      ch->setBornes(-1e6, 1e6);
      ch->setValeur(v[d]);
      ch->setEnabled(!fixe);
      static const char* const noms[] = {"positionX", "positionY", "positionZ", "rotationX", "rotationY", "rotationZ"};
      ch->setObjectName(QString::fromLatin1(noms[d]));
      const int k = m_choisi;
      connect(ch, &ChampValeur::valeurChangee, this, [this, k, d](double x) {
        if (!composantValide(k)) return;
        ComposantAssemblage c2 = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(k)];
        double* q[6] = {&c2.x, &c2.y, &c2.z, &c2.rx, &c2.ry, &c2.rz};
        *q[d] = x;
        placerComposant(k, c2);  // dans le respect des contraintes : les composants liés suivent (A4)
      });
      fl->addRow(QString::fromLatin1(libelles[d]), ch);
    }
    col->addWidget(g);
    // Déplacer de, pivoter d'un angle autour d'un axe de l'assemblage passant par le centre du composant (A4), sans
    // boîte modale
    if (!fixe) {
      auto* gd = new QGroupBox(QStringLiteral("Déplacer, pivoter"));
      auto* fd = new QFormLayout(gd);
      auto* ligneD = new QHBoxLayout;
      std::array<ChampValeur*, 3> deltas{};
      static const char* const nomsD[] = {"deltaX", "deltaY", "deltaZ"};
      for (int d = 0; d < 3; ++d) {
        deltas[static_cast<size_t>(d)] = new ChampValeur(false, u);
        deltas[static_cast<size_t>(d)]->setObjectName(QString::fromLatin1(nomsD[d]));
        deltas[static_cast<size_t>(d)]->setBornes(-1e6, 1e6);
        deltas[static_cast<size_t>(d)]->setValeur(0);
        deltas[static_cast<size_t>(d)]->setPlaceholderText(QString::fromLatin1(d == 0 ? "ΔX" : d == 1 ? "ΔY" : "ΔZ"));
        ligneD->addWidget(deltas[static_cast<size_t>(d)]);
      }
      auto* deplacer = new QPushButton(icone(QStringLiteral("deplacer")), QStringLiteral("Déplacer"));
      deplacer->setObjectName(QStringLiteral("translaterComposant"));
      connect(deplacer, &QPushButton::clicked, this, [this, deltas] { translater(deltas[0]->valeur(), deltas[1]->valeur(), deltas[2]->valeur()); });
      ligneD->addWidget(deplacer);
      fd->addRow(QStringLiteral("De"), ligneD);
      auto* ligneP = new QHBoxLayout;
      auto* axe = new QComboBox;
      axe->setObjectName(QStringLiteral("axePivot"));
      axe->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
      axe->setCurrentIndex(2);
      auto* angle = new ChampValeur(true, u);
      angle->setObjectName(QStringLiteral("anglePivot"));
      angle->setBornes(-36000, 36000);
      angle->setValeur(45);
      auto* tourner = new QPushButton(icone(QStringLiteral("pivoter")), QStringLiteral("Pivoter"));
      tourner->setObjectName(QStringLiteral("pivoterComposant"));
      connect(tourner, &QPushButton::clicked, this, [this, axe, angle] { pivoterAutourDuCentre(axe->currentIndex(), angle->valeur()); });
      ligneP->addWidget(axe);
      ligneP->addWidget(angle, 1);
      ligneP->addWidget(tourner);
      fd->addRow(QStringLiteral("Autour de"), ligneP);
      col->addWidget(gd);
    }
    // Actions
    auto* ga = new QGroupBox(QStringLiteral("Actions"));
    auto* gl = new QGridLayout(ga);
    int n = 0;
    auto action = [&](const QString& ic, const QString& t, std::function<void()> f) {
      auto* b = new QPushButton(icone(ic), t);
      connect(b, &QPushButton::clicked, this, [f] { f(); });
      gl->addWidget(b, n / 2, n % 2);
      ++n;
    };
    action(QStringLiteral("fixe"), fixe ? QStringLiteral("Libérer") : QStringLiteral("Fixer"), [this] { basculerFixe(); });
    action(QStringLiteral("afficher"), c.masque ? QStringLiteral("Afficher") : QStringLiteral("Masquer"), [this] { basculerMasque(); });
    action(QStringLiteral("contraintes"), c.supprime ? QStringLiteral("Réactiver") : QStringLiteral("Suspendre"), [this] { basculerSuppression(); });
    action(QStringLiteral("repetition-lineaire"), QStringLiteral("Copier"), [this] { copier(); });
    if (c.assemblage < 0) action(QStringLiteral("esquisse"), QStringLiteral("Modifier la pièce"), [this, c] { emit ouvrirPiece(c.piece, m_indice); });
    action(QStringLiteral("supprimer"), QStringLiteral("Retirer"), [this] { retirer(); });
    col->addWidget(ga);
    // Contraintes de ce composant
    auto* gc = new QGroupBox(QStringLiteral("Contraintes du composant"));
    auto* cl = new QVBoxLayout(gc);
    int nc = 0;
    for (size_t k = 0; k < as.contraintes.size(); ++k) {
      const auto& ct = as.contraintes[k];
      if (ct.a != m_choisi && ct.b != m_choisi) continue;
      ++nc;
      auto* ligne = new QHBoxLayout;
      auto* t = new QLabel(QStringLiteral("%1 %2 ↔ %3").arg(ct.suspendue ? QStringLiteral("⏸") : QStringLiteral("✓"), nomType(ct), contrainteVersAssemblage(ct) ? QStringLiteral("Assemblage") : nomComposant(ct.a == m_choisi ? ct.b : ct.a)));
      if (ct.suspendue) t->setStyleSheet(QStringLiteral("color: #9AA0B8;"));
      ligne->addWidget(t, 1);
      auto* sus = new QToolButton;
      sus->setText(ct.suspendue ? QStringLiteral("▶") : QStringLiteral("⏸"));
      sus->setToolTip(ct.suspendue ? QStringLiteral("Réactiver") : QStringLiteral("Suspendre"));
      const int ki = static_cast<int>(k), i = m_indice;
      connect(sus, &QToolButton::clicked, this, [this, ki, i] { m_doc->modifier([ki, i](Projet& pr) { auto& x = pr.assemblages[static_cast<size_t>(i)].contraintes[static_cast<size_t>(ki)]; x.suspendue = !x.suspendue; }); resoudre(); });
      auto* sup = new QToolButton;
      sup->setIcon(icone(QStringLiteral("supprimer")));
      sup->setToolTip(QStringLiteral("Supprimer"));
      connect(sup, &QToolButton::clicked, this, [this, ki, i] { m_doc->modifier([ki, i](Projet& pr) { auto& v2 = pr.assemblages[static_cast<size_t>(i)].contraintes; v2.erase(v2.begin() + ki); }); resoudre(); });
      ligne->addWidget(sus);
      ligne->addWidget(sup);
      cl->addLayout(ligne);
    }
    if (!nc) cl->addWidget(new QLabel(QStringLiteral("Aucune. Clique une face de ce composant puis une face d’un autre : les contraintes possibles s’affichent.")));
    col->addWidget(gc);
  } else if (m_contrainteChoisie >= 0 && m_contrainteChoisie < static_cast<int>(as.contraintes.size())) {
    const auto& ct = as.contraintes[static_cast<size_t>(m_contrainteChoisie)];
    entete(QStringLiteral("coincident"), nomType(ct), QStringLiteral("%1 ↔ %2").arg(nomCote(ct, false), nomCote(ct, true)));
    auto* g = new QGroupBox(QStringLiteral("Réglages"));
    auto* fl = new QFormLayout(g);
    const int ki = m_contrainteChoisie, i = m_indice;
    if (ct.type == "coincidence" || ct.type == "angle") {
      auto* ch = new ChampValeur(ct.type == "angle", Unites());
      ch->setObjectName(QStringLiteral("valeurContrainte"));
      ch->setBornes(-1e6, 1e6);
      ch->setValeur(ct.valeur);
      connect(ch, &ChampValeur::valeurChangee, this, [this, ki, i](double x) { m_doc->modifier([ki, i, x](Projet& pr) { pr.assemblages[static_cast<size_t>(i)].contraintes[static_cast<size_t>(ki)].valeur = x; }); resoudre(); });
      fl->addRow(ct.type == "angle" ? QStringLiteral("Angle") : QStringLiteral("Distance"), ch);
    }
    // Modifier une contrainte ou une liaison après coup (M9) : sens, rapport, pas, position axiale, pilotage
    auto modifierContrainte = [this, ki, i](const std::function<void(ContrainteAssemblage&)>& f) {
      m_doc->modifier([ki, i, f](Projet& pr) {
        auto& v = pr.assemblages[static_cast<size_t>(i)].contraintes;
        if (ki < static_cast<int>(v.size())) f(v[static_cast<size_t>(ki)]);
      });
      resoudre();
      reconstruire(false);
    };
    // Références des deux côtés (A2) : la face choisie, ou un plan, un axe, l'origine de la pièce, sa géométrie de
    // référence, ou ceux de l'assemblage ; seules les références que la contrainte admet sont proposées
    {
      const bool liaisonCt = ct.type == "pivot" || ct.type == "glissiere" || ct.type == "vis" || ct.type == "engrenage" || ct.type == "rotule";
      for (const bool coteB : {false, true}) {
        const std::string ref = coteB ? ct.refB : ct.refA, autre = coteB ? ct.refA : ct.refB;
        const int comp = coteB ? ct.b : ct.a, compAutre = coteB ? ct.a : ct.b;
        auto* choix = new QComboBox;
        choix->setObjectName(coteB ? QStringLiteral("referenceB") : QStringLiteral("referenceA"));
        choix->addItem(libelleReference(comp, ref), QString::fromStdString(ref));
        auto proposer = [&](const std::string& nom, bool versAssemblage) {
          if (nom == ref) return;
          const int c1 = versAssemblage ? compAutre : comp;
          const bool admis = coteB ? facesAcceptees(ct.type, compAutre, autre, c1, nom, nullptr) : facesAcceptees(ct.type, c1, nom, compAutre, autre, nullptr);
          if (admis) choix->addItem(libelleReference(c1, nom), QString::fromStdString(nom));
        };
        if (!liaisonCt) {
          static const char* const reperes[] = {"XY", "XZ", "YZ", "X", "Y", "Z", "O"};
          if (!referenceAssemblage(ref) && composantValide(comp)) {
            for (const char* q : reperes) proposer(std::string("base:") + q, false);
            const auto& cc = as.composants[static_cast<size_t>(comp)];
            if (cc.assemblage < 0 && rangPieceValide(m_doc->projet(), cc.piece)) {
              const ResultatPiece& rp = resultatPiece(cc.piece);
              for (const auto& [id, ax] : rp.plansReference) proposer("plan:" + std::to_string(id), false);
              for (const auto& [id, ax] : rp.axesReference) proposer("axe:" + std::to_string(id), false);
              for (const auto& [id, pt] : rp.pointsReference) proposer("point:" + std::to_string(id), false);
            }
          }
          if (!referenceAssemblage(autre))
            for (const char* q : reperes) proposer(std::string(PREFIXE_REFERENCE_ASSEMBLAGE) + q, true);
        }
        choix->setEnabled(choix->count() > 1);
        connect(choix, &QComboBox::activated, this, [this, choix, coteB, modifierContrainte](int) {
          const std::string nom = choix->currentData().toString().toStdString();
          modifierContrainte([nom, coteB](ContrainteAssemblage& c) {
            (coteB ? c.refB : c.refA) = nom;
            // Le côté porté par l'assemblage garde le composant de l'autre côté
            if (referenceAssemblage(nom)) (coteB ? c.b : c.a) = coteB ? c.a : c.b;
          });
          QTimer::singleShot(0, this, [this] { majProprietes(); });
        });
        fl->addRow(coteB ? QStringLiteral("Référence 2") : QStringLiteral("Référence 1"), choix);
      }
    }
    if (ct.type == "coincidence" || ct.type == "tangente") {
      auto* inv = new QCheckBox(ct.type == "coincidence" ? QStringLiteral("Faces alignées (même sens) au lieu de face à face") : QStringLiteral("Tangente de l’autre côté"));
      inv->setObjectName(QStringLiteral("inverserContrainte"));
      inv->setChecked(ct.inverse);
      connect(inv, &QCheckBox::toggled, this, [modifierContrainte](bool on) { modifierContrainte([on](ContrainteAssemblage& c) { c.inverse = on; }); });
      fl->addRow(inv);
    }
    // Sens d'une contrainte (A1) : parallèle et concentrique, même sens ou opposés ; angle orienté et son inversion
    if (ct.type == "parallele" || ct.type == "concentrique") {
      auto* sensContrainte = new QComboBox;
      sensContrainte->setObjectName(QStringLiteral("sensContrainte"));
      sensContrainte->addItem(QStringLiteral("Le plus proche"), 0);
      sensContrainte->addItem(QStringLiteral("Même sens (alignées)"), 1);
      sensContrainte->addItem(QStringLiteral("Sens opposés"), 2);
      sensContrainte->setCurrentIndex(std::clamp(ct.alignement, 0, 2));
      connect(sensContrainte, &QComboBox::activated, this, [modifierContrainte, sensContrainte](int) {
        const int v = sensContrainte->currentData().toInt();
        modifierContrainte([v](ContrainteAssemblage& c) { c.alignement = v; });
      });
      fl->addRow(QStringLiteral("Sens"), sensContrainte);
    }
    if (ct.type == "angle") {
      auto* oriente = new QCheckBox(QStringLiteral("Angle orienté (sens de rotation imposé)"));
      oriente->setObjectName(QStringLiteral("angleOriente"));
      oriente->setChecked(ct.alignement != 0);
      connect(oriente, &QCheckBox::toggled, this, [this, ki, i](bool on) {
        std::vector<ResultatPiece> pieces;
        for (size_t n = 0; n < m_doc->projet().pieces.size(); ++n) pieces.push_back(resultatPiece(static_cast<int>(n)));
        bool ok = true;
        m_doc->modifier([&](Projet& pr) {
          Assemblage& a = pr.assemblages[static_cast<size_t>(i)];
          if (ki >= static_cast<int>(a.contraintes.size())) return;
          if (on) ok = orienterAngle(a, ki, pieces, &pr.assemblages);
          else {
            ContrainteAssemblage& c = a.contraintes[static_cast<size_t>(ki)];
            c.alignement = 0;
            c.parametres.clear();
            c.valeur = std::abs(c.valeur);
          }
        });
        if (!ok) m_etat->setText(QStringLiteral("Faces parallèles : pas d’axe autour duquel orienter l’angle."));
        resoudre();
        reconstruire(false);
        // Le panneau est refait après le signal : la case qui l'émet ne doit pas être détruite pendant son émission
        QTimer::singleShot(0, this, [this] { majProprietes(); });
      });
      fl->addRow(oriente);
      if (ct.alignement != 0) {
        auto* inverser = new QPushButton(QStringLiteral("Inverser le sens de l’angle"));
        inverser->setObjectName(QStringLiteral("inverserAngle"));
        connect(inverser, &QPushButton::clicked, this, [modifierContrainte] { modifierContrainte([](ContrainteAssemblage& c) { c.alignement = c.alignement == 1 ? 2 : 1; }); });
        fl->addRow(inverser);
      }
    }
    if (ct.type == "engrenage") {
      auto* rapport = new QDoubleSpinBox;
      rapport->setObjectName(QStringLiteral("rapportEngrenage"));
      rapport->setRange(-1000, 1000);
      rapport->setDecimals(4);
      rapport->setSingleStep(0.1);
      rapport->setValue(ct.valeur == 0 ? 1 : ct.valeur);
      rapport->setToolTip(QStringLiteral("Nombre de dents de B / nombre de dents de A ; négatif pour une couronne à denture intérieure"));
      connect(rapport, &QDoubleSpinBox::editingFinished, this, [modifierContrainte, rapport] {
        const double x = std::abs(rapport->value()) < 1e-9 ? 1 : rapport->value();
        modifierContrainte([x](ContrainteAssemblage& c) { c.valeur = x; });
      });
      fl->addRow(QStringLiteral("Rapport zB / zA"), rapport);
    }
    const bool axiale = ct.type == "pivot" || ct.type == "glissiere" || ct.type == "vis";
    if (axiale && ct.parametres.size() >= 7) {
      auto* pos = new ChampValeur(false, Unites());
      pos->setObjectName(QStringLiteral("positionAxiale"));
      pos->setBornes(-1e6, 1e6);
      pos->setValeur(ct.parametres[6]);
      connect(pos, &ChampValeur::valeurChangee, this, [modifierContrainte](double x) { modifierContrainte([x](ContrainteAssemblage& c) { if (c.parametres.size() >= 7) c.parametres[6] = x; }); });
      fl->addRow(ct.type == "pivot" ? QStringLiteral("Position axiale") : QStringLiteral("Position axiale de départ"), pos);
    }
    if (ct.type == "vis") {
      auto* pas = new ChampValeur(false, Unites());
      pas->setObjectName(QStringLiteral("pasVis"));
      pas->setBornes(-1000, 1000);
      pas->setValeur(ct.parametres.size() >= 8 ? ct.parametres[7] : 1.0);
      pas->setToolTip(QStringLiteral("Avance par tour ; négatif pour un filet à gauche"));
      connect(pas, &ChampValeur::valeurChangee, this, [modifierContrainte](double x) {
        if (std::abs(x) < 1e-9) return;
        modifierContrainte([x](ContrainteAssemblage& c) { if (c.parametres.size() < 8) c.parametres.resize(8, 1.0); c.parametres[7] = x; });
      });
      fl->addRow(QStringLiteral("Pas (par tour)"), pas);
    }
    if (axiale) {
      // Pilotage gardé dans le modèle : l'angle (pivot, vis) ou la course (glissière) est imposé
      auto* pil = new QCheckBox(ct.type == "glissiere" ? QStringLiteral("Course imposée") : QStringLiteral("Angle imposé"));
      pil->setObjectName(QStringLiteral("piloterLiaison"));
      pil->setChecked(ct.pilotee);
      const bool glissiere = ct.type == "glissiere";
      connect(pil, &QCheckBox::toggled, this, [this, modifierContrainte, glissiere](bool on) {
        modifierContrainte([on, glissiere](ContrainteAssemblage& c) {
          c.pilotee = on;
          // L'état actuel devient la valeur de départ : rien ne bouge en cochant
          if (on) c.valeur = glissiere ? 0 : c.angleSuivi[0];
        });
        majProprietes();
      });
      fl->addRow(pil);
      if (ct.pilotee) {
        auto* val = new ChampValeur(!glissiere, Unites());
        val->setObjectName(QStringLiteral("valeurPilotee"));
        val->setBornes(-1e6, 1e6);
        val->setValeur(ct.valeur);
        connect(val, &ChampValeur::valeurChangee, this, [modifierContrainte](double x) { modifierContrainte([x](ContrainteAssemblage& c) { c.valeur = x; }); });
        fl->addRow(glissiere ? QStringLiteral("Course") : QStringLiteral("Angle"), val);
      }
    }
    auto* sus = new QCheckBox(QStringLiteral("Suspendue (gardée, non résolue)"));
    sus->setChecked(ct.suspendue);
    connect(sus, &QCheckBox::toggled, this, [this, ki, i](bool on) { m_doc->modifier([ki, i, on](Projet& pr) { pr.assemblages[static_cast<size_t>(i)].contraintes[static_cast<size_t>(ki)].suspendue = on; }); resoudre(); });
    fl->addRow(sus);
    auto* sup = new QPushButton(icone(QStringLiteral("supprimer")), QStringLiteral("Supprimer la contrainte"));
    connect(sup, &QPushButton::clicked, this, [this, ki, i] { m_doc->modifier([ki, i](Projet& pr) { auto& v2 = pr.assemblages[static_cast<size_t>(i)].contraintes; v2.erase(v2.begin() + ki); }); m_contrainteChoisie = -1; resoudre(); majProprietes(); });
    fl->addRow(sup);
    col->addWidget(g);
  } else {
    entete(QStringLiteral("assemblage"), QString::fromStdString(as.nom), QStringLiteral("Assemblage"));
    std::vector<ResultatPiece> pieces;
    for (size_t i = 0; i < p.pieces.size(); ++i) pieces.push_back(resultatPiece(static_cast<int>(i)));
    const AnalyseAssemblage an = analyserAssemblage(as, pieces, &m_doc->projet().assemblages);
    int suspendues = 0;
    for (const auto& c : as.contraintes) suspendues += c.suspendue;
    auto* l = new QLabel(QStringLiteral("%1 composant(s) · %2 contrainte(s)%3<br>%4%5")
                             .arg(as.composants.size()).arg(as.contraintes.size())
                             .arg(suspendues ? QStringLiteral(" dont %1 suspendue(s)").arg(suspendues) : QString())
                             .arg(an.degresLiberte == 0 ? QStringLiteral("<span style='color:#2E8B57'>✓ Tout est contraint</span>") : QStringLiteral("%1 degré(s) de liberté au total").arg(an.degresLiberte))
                             .arg(an.nonSatisfaites.empty() ? QString() : QStringLiteral("<br><span style='color:#C2402E'>✖ %1 contrainte(s) non satisfaite(s)</span>").arg(an.nonSatisfaites.size())));
    l->setWordWrap(true);
    col->addWidget(l);
    // Dernière résolution en échec : les contraintes en cause, chacune suspendable d'un clic (AS-20)
    if (m_dernierEtat && !m_dernierEtat->resolu && !m_dernierEtat->problemes.empty()) {
      auto* gp = new QGroupBox(QStringLiteral("Contraintes en cause"));
      auto* gl = new QVBoxLayout(gp);
      auto* expl = new QLabel(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(QString::fromStdString(m_dernierEtat->message).toHtmlEscaped()));
      expl->setWordWrap(true);
      gl->addWidget(expl);
      for (const auto& [ci, raison] : m_dernierEtat->problemes) {
        if (ci < 0 || ci >= static_cast<int>(as.contraintes.size())) continue;
        const auto& ct = as.contraintes[static_cast<size_t>(ci)];
        auto* ligne = new QHBoxLayout;
        auto* t = new QLabel(QStringLiteral("%1 : %2 ↔ %3 — %4").arg(nomType(ct), nomCote(ct, false), nomCote(ct, true), QString::fromStdString(raison).toHtmlEscaped()));
        t->setWordWrap(true);
        ligne->addWidget(t, 1);
        if (!ct.suspendue) {
          auto* b = new QPushButton(QStringLiteral("Suspendre"));
          const int ki = ci, i = m_indice;
          connect(b, &QPushButton::clicked, this, [this, ki, i] {
            m_doc->modifier([ki, i](Projet& pr) { pr.assemblages[static_cast<size_t>(i)].contraintes[static_cast<size_t>(ki)].suspendue = true; });
            resoudre();
            reconstruire(false);
            majProprietes();
          });
          ligne->addWidget(b);
        }
        gl->addLayout(ligne);
      }
      col->addWidget(gp);
    }
    auto* aide = new QLabel(QStringLiteral("Clique un composant pour le régler. Clique une face, puis une face d’un autre composant : les contraintes possibles s’affichent près du curseur."));
    aide->setWordWrap(true);
    aide->setStyleSheet(QStringLiteral("color: #4A5270;"));
    col->addWidget(aide);
  }
  col->addStretch(1);
  remplacerPage(m_props, page);
}

// Barre flottante : actions du composant choisi, et contraintes possibles quand deux faces sont cliquées
void AtelierAssemblage::majBarreFlottante(const QPoint& pres) {
  for (QWidget* w : m_barre->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) w->deleteLater();
  delete m_barre->layout();
  struct A { const char* icone; QString bulle; std::function<void()> f; };
  std::vector<A> actions;
  QString titre;
  const auto sel = m_vue->selection();
  const TopoDS_Shape s = sel.empty() ? TopoDS_Shape() : sel.front();
  const int k = s.IsNull() ? -1 : composantSous(s);
  if (m_secondComposant != -1 && m_faceComposant != -1 && !m_faceNom.empty()) {
    // Deux éléments de composants différents, ou un composant et l'assemblage (A2) : contraintes possibles selon
    // leur nature ; le côté « assemblage » porte le composant de l'autre côté
    const std::string nomB = m_secondNom;
    const std::string nomA = m_faceNom;
    int a = m_faceComposant, k = m_secondComposant;
    if (a == COMPOSANT_ASSEMBLAGE) a = k;
    if (k == COMPOSANT_ASSEMBLAGE) k = a;
    m_secondComposant = -1;
    m_secondNom.clear();
    titre = QStringLiteral("Contraindre");
    // Seules les contraintes que le noyau accepte pour ces deux faces sont proposées (même table que la résolution)
    auto ajouter = [&](const char* ic, const QString& t, const std::string& type) {
      if (!facesAcceptees(type, a, nomA, k, nomB, nullptr)) return;
      actions.push_back({ic, t, [this, a, nomA, k, nomB, type] { creerContrainte(type, a, nomA, k, nomB); }});
    };
    ajouter("coincident", QStringLiteral("Coïncidente"), "coincidence");
    ajouter("colineaire", QStringLiteral("Alignée"), "aligne");
    ajouter("cote", QStringLiteral("Distance…"), "distance");
    ajouter("parallele", QStringLiteral("Parallèle"), "parallele");
    ajouter("perpendiculaire", QStringLiteral("Perpendiculaire"), "perpendiculaire");
    ajouter("pivoter", QStringLiteral("Angle…"), "angle");
    ajouter("concentrique", QStringLiteral("Concentrique (pivot glissant)"), "concentrique");
    ajouter("pivot", QStringLiteral("Pivot"), "pivot");
    ajouter("glissiere", QStringLiteral("Glissière"), "glissiere");
    ajouter("vis", QStringLiteral("Vis-écrou…"), "vis");
    ajouter("engrenage", QStringLiteral("Engrenage…"), "engrenage");
    ajouter("concentrique", QStringLiteral("Rotule"), "rotule");
    ajouter("arc-tangent", QStringLiteral("Tangente"), "tangente");
    m_faceComposant = -1;
    if (nomB.empty()) actions.clear();
  } else if (k >= 0) {
    titre = nomComposant(k);
    actions = {{"deplacer", QStringLiteral("Placer par valeurs"), [this] { placerParValeurs(); }},
               {"pivoter", QStringLiteral("Pivoter de 90° autour de Z"), [this] { pivoter(2, 90); }},
               {"repetition-lineaire", QStringLiteral("Copier"), [this] { copier(); }},
               {"fixe", QStringLiteral("Fixer ou libérer"), [this] { basculerFixe(); }},
               {"afficher", QStringLiteral("Masquer"), [this] { basculerMasque(); }}};
  }
  if (actions.empty()) { m_barre->hide(); return; }
  auto* l = new QHBoxLayout(m_barre);
  l->setContentsMargins(6, 3, 6, 3);
  l->setSpacing(2);
  l->addWidget(new QLabel(titre));
  for (const auto& x : actions) {
    auto* b = new QToolButton(m_barre);
    b->setIcon(icone(QString::fromLatin1(x.icone)));
    b->setIconSize(QSize(24, 24));
    b->setToolTip(x.bulle);
    const auto f = x.f;
    connect(b, &QToolButton::clicked, this, [this, f] { m_barre->hide(); f(); });
    l->addWidget(b);
  }
  m_barre->adjustSize();
  QWidget* scene = m_barre->parentWidget();
  QPoint q = pres + QPoint(18, 18);
  q.setX(std::clamp(q.x(), 6, std::max(6, scene->width() - m_barre->width() - 6)));
  q.setY(std::clamp(q.y(), 6, std::max(6, scene->height() - m_barre->height() - 6)));
  m_barre->move(q);
  m_barre->show();
  m_barre->raise();
}

// ---------- Contraintes ----------
void AtelierAssemblage::commencerContrainte(const std::string& type) {
  if (m_indice < 0) return;
  m_typeContrainte = type;
  m_premierComposant = -1;
  m_premiereFace.clear();
  m_barre->hide();
  m_vue->viderSelection();
  // Faces, arêtes et sommets (A2) ; plans, axes et origine de l'assemblage montrés le temps de l'outil
  m_vue->setMode(VuePiece::Mode::Tout);
  majRepere();
  m_etat->setText(QStringLiteral("<b>Contrainte</b> : clique une face, une arête, un sommet du premier composant, ou un plan, un axe, l’origine de l’assemblage (Échap ou un autre outil pour abandonner)."));
}

void AtelierAssemblage::choisirReference(int k, const std::string& nom) {
  if (m_typeContrainte.empty()) return;
  m_vue->viderSelection();
  const bool assemblageChoisi = k == COMPOSANT_ASSEMBLAGE;
  if (!assemblageChoisi && !composantValide(k)) return;
  if (nom.empty()) { m_etat->setText(QStringLiteral("Cet élément n’a pas de nom stable dans sa pièce : choisis-en un autre.")); return; }
  if (m_premierComposant == -1) {
    m_premierComposant = k;
    m_premiereFace = nom;
    m_etat->setText(QStringLiteral("<b>Contrainte</b> : %1 choisi ; clique un élément %2.")
                        .arg(libelleReference(k, nom), assemblageChoisi ? QStringLiteral("d’un composant") : QStringLiteral("d’un autre composant, ou un plan de l’assemblage")));
    return;
  }
  if (assemblageChoisi && m_premierComposant == COMPOSANT_ASSEMBLAGE) { m_etat->setText(QStringLiteral("Choisis cette fois un élément d’un composant.")); return; }
  if (!assemblageChoisi && k == m_premierComposant) { m_etat->setText(QStringLiteral("Choisis un élément d’un autre composant.")); return; }
  int a = m_premierComposant, b = k;
  if (a == COMPOSANT_ASSEMBLAGE) a = b;
  if (b == COMPOSANT_ASSEMBLAGE) b = a;
  // Le ruban filtre comme la barre flottante : un élément que la contrainte n'accepte pas est refusé (AS-08)
  if (QString raison; !facesAcceptees(m_typeContrainte, a, m_premiereFace, b, nom, &raison)) {
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span> Clique un autre élément, ou Échap pour abandonner.").arg(raison.toHtmlEscaped()));
    return;
  }
  const std::string type = m_typeContrainte, premiere = m_premiereFace;
  m_typeContrainte.clear();
  m_premierComposant = -1;
  m_premiereFace.clear();
  majRepere();
  creerContrainte(type, a, premiere, b, nom);
}

void AtelierAssemblage::setRepereVisible(bool oui) {
  m_repereVisible = oui;
  majRepere();
}

void AtelierAssemblage::majRepere() { m_vue->setPlansVisibles(m_repereVisible || !m_typeContrainte.empty()); }

void AtelierAssemblage::lier(const std::string& type, int ca, const std::string& refA, int cb, const std::string& refB, double valeur) {
  std::vector<ResultatPiece> pieces;
  for (size_t i = 0; i < m_doc->projet().pieces.size(); ++i) pieces.push_back(resultatPiece(static_cast<int>(i)));
  if (!assemblageValide()) return;
  ContrainteAssemblage c;
  try {
    c = creerLiaison(m_doc->projet().assemblages[static_cast<size_t>(m_indice)], pieces, type, ca, refA, cb, refB, valeur, &m_doc->projet().assemblages);
  } catch (const std::invalid_argument& e) {
    m_etat->setText(QString::fromStdString(e.what()));
    m_vue->setMode(VuePiece::Mode::Tout);
    m_vue->viderSelection();
    return;
  }
  const int i = m_indice;
  m_doc->modifier([i, c](Projet& p) { p.assemblages[static_cast<size_t>(i)].contraintes.push_back(c); });
  m_vue->setMode(VuePiece::Mode::Tout);
  m_vue->viderSelection();
  resoudre();
  reconstruire(false);
  majProprietes();
}

void AtelierAssemblage::creerContrainte(const std::string& type, int a, const std::string& refA, int b, const std::string& refB) {
  // Toute sortie remet la vue en sélection ordinaire (AS-35)
  auto finir = [this] {
    m_vue->setMode(VuePiece::Mode::Tout);
    m_vue->viderSelection();
  };
  if (QString raison; !facesAcceptees(type, a, refA, b, refB, &raison)) {
    m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(raison.toHtmlEscaped()));
    finir();
    return;
  }
  if (type == "vis" || type == "engrenage") {
    // Pas de la vis, dents des roues : dans le panneau latéral (U1), la vue reste visible ; Valider pose la liaison
    auto* page = new QWidget;
    auto* f = new QFormLayout(page);
    f->setContentsMargins(0, 0, 0, 0);
    std::function<double()> valeur;
    if (type == "vis") {
      auto* pas = new ChampValeur(false, Unites());
      pas->setObjectName(QStringLiteral("pasNouvelleVis"));
      pas->setBornes(-1e4, 1e4);
      pas->setValeur(1);
      f->addRow(QStringLiteral("Pas (par tour)"), pas);
      auto* aide = new QLabel(QStringLiteral("Avance de l’écrou à chaque tour ; négatif pour un filet à gauche."));
      aide->setWordWrap(true);
      f->addRow(aide);
      valeur = [pas] { return std::abs(pas->valeur()) < 1e-9 ? 1.0 : pas->valeur(); };
    } else {
      // Nombres de dents de chaque roue, nommées par leur composant (AS-10) : rapport = dents de B / dents de A
      auto* za = new QSpinBox;
      auto* zb = new QSpinBox;
      za->setObjectName(QStringLiteral("dentsA"));
      zb->setObjectName(QStringLiteral("dentsB"));
      za->setRange(1, 100000);
      zb->setRange(1, 100000);
      za->setValue(20);
      zb->setValue(40);
      f->addRow(QStringLiteral("Dents de « %1 »").arg(nomComposant(a)), za);
      f->addRow(QStringLiteral("Dents de « %1 »").arg(nomComposant(b)), zb);
      auto* interieur = new QCheckBox(QStringLiteral("« %1 » est une couronne (denture intérieure)").arg(nomComposant(b)));
      interieur->setObjectName(QStringLiteral("couronne"));
      f->addRow(interieur);
      auto* aideEngrenage = new QLabel(QStringLiteral("La roue qui a le plus de dents tourne moins vite ; une couronne tourne dans le même sens que son pignon."));
      aideEngrenage->setWordWrap(true);
      f->addRow(aideEngrenage);
      valeur = [za, zb, interieur] { return static_cast<double>(zb->value()) / za->value() * (interieur->isChecked() ? -1 : 1); };
    }
    m_panneau->ouvrir(type == "vis" ? QStringLiteral("vis") : QStringLiteral("engrenage"), type == "vis" ? QStringLiteral("Vis-écrou") : QStringLiteral("Engrenage"),
                      type == "vis" ? QStringLiteral("percage") : QStringLiteral("repetition-circulaire"), page, PanneauLateral::Boutons::ValiderAnnuler,
                      [this, type, a, refA, b, refB, valeur] {
                        lier(type, a, refA, b, refB, valeur());
                        return true;
                      },
                      finir);
    return;
  }
  if (type == "pivot" || type == "glissiere" || type == "rotule") {
    lier(type, a, refA, b, refB, 0);
    return;
  }
  ContrainteAssemblage c;
  c.type = type == "distance" || type == "aligne" ? "coincidence" : type;
  c.a = a;
  c.refA = refA;
  c.b = b;
  c.refB = refB;
  if (type == "aligne") c.inverse = true;
  const bool aRegler = type == "distance" || type == "angle";
  if (aRegler) {
    // Valeur de départ : celle d'aujourd'hui, si bien que rien ne bouge ; elle se règle ensuite dans le panneau (U1)
    std::vector<ResultatPiece> pieces;
    for (size_t n = 0; n < m_doc->projet().pieces.size(); ++n) pieces.push_back(resultatPiece(static_cast<int>(n)));
    const auto v = valeurMesuree(m_doc->projet().assemblages[static_cast<size_t>(m_indice)], c, pieces, &m_doc->projet().assemblages);
    c.valeur = v ? std::round(*v * 1000) / 1000 : (type == "angle" ? 90 : 10);
  }
  const int i = m_indice;
  m_doc->modifier([i, c](Projet& p) { p.assemblages[static_cast<size_t>(i)].contraintes.push_back(c); });
  m_vue->setMode(VuePiece::Mode::Tout);
  m_vue->viderSelection();
  resoudre();
  reconstruire(false);
  if (aRegler && assemblageValide()) {
    // La contrainte posée s'ouvre dans le panneau, valeur prête à être tapée
    choisirContrainte(static_cast<int>(m_doc->projet().assemblages[static_cast<size_t>(m_indice)].contraintes.size()) - 1);
    if (auto* champ = m_props->findChild<QLineEdit*>(QStringLiteral("valeurContrainte"))) { champ->setFocus(); champ->selectAll(); }
    m_etat->setText(type == "angle" ? QStringLiteral("<b>Angle</b> posé à sa valeur actuelle : tape l’angle voulu dans le panneau.")
                                    : QStringLiteral("<b>Distance</b> posée à sa valeur actuelle : tape la distance voulue dans le panneau."));
  } else {
    majProprietes();
  }
}

EtatAssemblage AtelierAssemblage::resoudre() {
  if (!assemblageValide()) return {};
  // Mise à jour de bas en haut (A3, AS-12) : les sous-assemblages utilisés d'abord, chacun avec ses propres
  // contraintes, puis l'assemblage ouvert ; une résolution en échec n'écrit rien
  std::vector<Assemblage> tous = m_doc->projet().assemblages;
  std::vector<ResultatPiece> pieces;
  for (size_t i = 0; i < m_doc->projet().pieces.size(); ++i) pieces.push_back(resultatPiece(static_cast<int>(i)));
  const std::vector<MiseAJourAssemblage> maj = mettreAJourAssemblages(tous, pieces, m_indice);
  EtatAssemblage e;
  std::vector<std::pair<int, EtatAssemblage>> sous;
  std::vector<std::pair<int, Assemblage>> ecrire;
  for (const auto& m : maj) {
    if (m.assemblage == m_indice) e = m.etat;
    else sous.push_back({m.assemblage, m.etat});
    if (m.deplace) ecrire.push_back({m.assemblage, tous[static_cast<size_t>(m.assemblage)]});
  }
  m_dernierEtat = e;
  m_problemesSous = sous;
  // Positions et angles suivis des liaisons, écrits seulement s'ils ont changé
  if (!ecrire.empty()) {
    m_enResolution = true;  // l'écriture ne relance pas la résolution (signature pas encore à jour)
    m_doc->modifier([ecrire](Projet& p) {
      for (const auto& [i, a] : ecrire) {
        if (!rangAssemblageValide(p, i)) continue;
        auto& as = p.assemblages[static_cast<size_t>(i)];
        as.composants = a.composants;
        for (size_t k = 0; k < a.contraintes.size() && k < as.contraintes.size(); ++k) as.contraintes[k].angleSuivi = a.contraintes[k].angleSuivi;
      }
    });
    m_enResolution = false;
  }
  m_signature = signaturePieces();  // après l'écriture : les sous-assemblages remis en place ne relancent rien
  signalerProblemes(e);
  if (!e.resolu && !e.message.empty()) m_etat->setText(QStringLiteral("<span style='color:#C2402E'>%1</span>").arg(QString::fromStdString(e.message).toHtmlEscaped()));
  return e;
}

void AtelierAssemblage::signalerProblemes(const EtatAssemblage& e) {
  if (!assemblageValide()) return;
  const Assemblage& as = m_doc->projet().assemblages[static_cast<size_t>(m_indice)];
  std::vector<Probleme> liste;
  for (const auto& [ci, raison] : e.problemes) {
    if (ci < 0 || ci >= static_cast<int>(as.contraintes.size())) continue;
    const auto& c = as.contraintes[static_cast<size_t>(ci)];
    liste.push_back({Severite::Erreur, "assemblage",
                     "Assemblage « " + as.nom + " » : contrainte n° " + std::to_string(ci + 1) + " (" + nomType(c).toStdString() + ", " + nomCote(c, false).toStdString() + " ↔ " +
                         nomCote(c, true).toStdString() + ") " + raison + ".",
                     {}, std::nullopt});
  }
  // Sous-assemblages résolus avec lui (A3) : leurs contraintes écartées ou violées comptent aussi
  for (const auto& [si, es] : m_problemesSous) {
    if (!rangAssemblageValide(m_doc->projet(), si)) continue;
    const Assemblage& sa = m_doc->projet().assemblages[static_cast<size_t>(si)];
    for (const auto& [ci, raison] : es.problemes) {
      if (ci < 0 || ci >= static_cast<int>(sa.contraintes.size())) continue;
      liste.push_back({Severite::Erreur, "assemblage", "Sous-assemblage « " + sa.nom + " » : contrainte n° " + std::to_string(ci + 1) + " (" + nomType(sa.contraintes[static_cast<size_t>(ci)]).toStdString() + ") " + raison + ".", {}, std::nullopt});
    }
    if (!es.resolu && es.problemes.empty() && !es.message.empty()) liste.push_back({Severite::Erreur, "assemblage", "Sous-assemblage « " + sa.nom + " » : " + es.message, {}, std::nullopt});
  }
  m_doc->definirProblemesExternes("assemblage", std::move(liste));
}

bool AtelierAssemblage::facesAcceptees(const std::string& type, int a, const std::string& refA, int b, const std::string& refB, QString* raison) {
  if (!composantValide(a) || !composantValide(b)) { if (raison) *raison = QStringLiteral("Composant introuvable."); return false; }
  // Repère de l'assemblage (A2) : un seul côté, et pas pour une liaison, qui relie deux composants
  const bool versAssemblage = referenceAssemblage(refA) || referenceAssemblage(refB);
  if (referenceAssemblage(refA) && referenceAssemblage(refB)) { if (raison) *raison = QStringLiteral("Choisis au moins un élément d’un composant."); return false; }
  if (versAssemblage && (type == "pivot" || type == "glissiere" || type == "vis" || type == "engrenage" || type == "rotule")) {
    if (raison) *raison = QStringLiteral("Une liaison relie deux composants : vers l’assemblage, utilise une contrainte simple (coïncidente, concentrique, parallèle…).");
    return false;
  }
  if (!versAssemblage && a == b) { if (raison) *raison = QStringLiteral("Les deux éléments appartiennent au même composant."); return false; }
  const auto& as = m_doc->projet().assemblages[static_cast<size_t>(m_indice)];
  const auto& ca = as.composants[static_cast<size_t>(a)];
  const auto& cb = as.composants[static_cast<size_t>(b)];
  // Pièce ou sous-assemblage (face désignée par son chemin « @k/face », AS-16)
  std::vector<ResultatPiece> pieces;
  for (size_t i = 0; i < m_doc->projet().pieces.size(); ++i) pieces.push_back(resultatPiece(static_cast<int>(i)));
  const std::string ga = genreFaceComposant(ca, refA, pieces, &m_doc->projet().assemblages), gb = genreFaceComposant(cb, refB, pieces, &m_doc->projet().assemblages);
  const std::string t = type == "distance" || type == "aligne" ? "coincidence" : type;
  const std::string refus = facesAdmises(t, ga, gb);
  if (!refus.empty() && raison) {
    QString r = QString::fromStdString(refus);
    if (!r.isEmpty()) r[0] = r[0].toUpper();
    *raison = r + QStringLiteral(" (éléments choisis : %1 et %2).").arg(QString::fromStdString(ga.empty() ? "inconnue" : ga), QString::fromStdString(gb.empty() ? "inconnue" : gb));
  }
  return refus.empty();
}

void AtelierAssemblage::abandonnerOutil() {
  if (m_pose >= 0) { finirPose(false); return; }
  m_typeContrainte.clear();
  m_premierComposant = -1;
  m_premiereFace.clear();
  m_faceComposant = -1;
  m_faceNom.clear();
  m_faceForme.Nullify();
  m_secondComposant = -1;
  m_secondNom.clear();
  majRepere();
  m_barre->hide();
  if (m_mesure) { m_mesure = false; m_vue->setCumulatif(false); m_vue->setTracesMesure({}); }
  if (m_deplacer) { m_deplacer = false; m_pivotSouris = -1; m_vue->setGlisserObjets(false); }
  m_vue->setMode(VuePiece::Mode::Tout);
  m_vue->viderSelection();
  m_etat->setText(QStringLiteral("Outil abandonné."));
}

// ---------- Manipulation ----------
void AtelierAssemblage::pivoter(int axe, double angle) {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  if (m_choisi == 0 || m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)].fixe) { m_etat->setText(QStringLiteral("Composant fixé : libère-le pour le faire pivoter.")); return; }
  // Autour de l'axe de l'assemblage, pas de l'axe propre du composant (AS-23)
  ComposantAssemblage c = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)];
  pivoterAutourAxeGlobal(c, axe, angle);
  placerComposant(m_choisi, c);
}

void AtelierAssemblage::copier() {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  ComposantAssemblage c = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)];
  Bnd_Box b;
  if (m_choisi < static_cast<int>(m_formesPlacees.size()) && !m_formesPlacees[static_cast<size_t>(m_choisi)].IsNull()) BRepBndLib::Add(m_formesPlacees[static_cast<size_t>(m_choisi)], b);
  double largeur = 50;
  if (!b.IsVoid()) { double x0, y0, z0, x1, y1, z1; b.Get(x0, y0, z0, x1, y1, z1); largeur = x1 - x0 + 10; }
  c.x += largeur;
  c.fixe = false;
  const int i = m_indice;
  m_doc->modifier([i, c](Projet& p) { p.assemblages[static_cast<size_t>(i)].composants.push_back(c); });
  choisirComposant(static_cast<int>(m_doc->projet().assemblages[static_cast<size_t>(i)].composants.size()) - 1);
}

void AtelierAssemblage::panneauRepetition(bool circulaire) {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  auto* page = new QWidget;
  auto* f = new QFormLayout(page);
  f->setContentsMargins(0, 0, 0, 0);
  auto* nombre = new QSpinBox;
  nombre->setObjectName(QStringLiteral("nombreRepetition"));
  nombre->setRange(2, 500);
  nombre->setValue(circulaire ? 4 : 3);
  f->addRow(QStringLiteral("Exemplaires (en tout)"), nombre);
  auto* pas = new ChampValeur(circulaire, Unites());
  pas->setObjectName(QStringLiteral("pasRepetition"));
  pas->setBornes(-1e5, 1e5);
  pas->setValeur(circulaire ? 90 : 50);
  f->addRow(circulaire ? QStringLiteral("Angle entre deux") : QStringLiteral("Pas"), pas);
  auto* axe = new QComboBox;
  axe->setObjectName(QStringLiteral("axeRepetition"));
  axe->addItems({QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
  axe->setCurrentIndex(circulaire ? 2 : 0);
  f->addRow(circulaire ? QStringLiteral("Autour de l’axe") : QStringLiteral("Selon"), axe);
  if (circulaire) {
    // Tour complet : l'angle suit le nombre d'exemplaires
    auto* complet = new QPushButton(QStringLiteral("Répartir sur 360°"));
    connect(complet, &QPushButton::clicked, page, [nombre, pas] { pas->setValeur(360.0 / nombre->value()); });
    f->addRow(complet);
  }
  const int k = m_choisi;
  m_panneau->ouvrir(circulaire ? QStringLiteral("repetitionCirculaire") : QStringLiteral("repetitionLineaire"), circulaire ? QStringLiteral("Répétition circulaire") : QStringLiteral("Répétition linéaire"),
                    circulaire ? QStringLiteral("repetition-circulaire") : QStringLiteral("repetition-lineaire"), page, PanneauLateral::Boutons::ValiderAnnuler,
                    [this, k, circulaire, nombre, pas, axe] {
                      if (!composantValide(k)) return true;
                      m_choisi = k;
                      repeter(circulaire, nombre->value(), pas->valeur(), axe->currentIndex());
                      return true;
                    });
}

void AtelierAssemblage::repeter(bool circulaire, int nombre, double pas, int axe) {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  const ComposantAssemblage base = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)];
  const int i = m_indice;
  m_doc->modifier([=](Projet& p) {
    for (int k = 1; k < nombre; ++k) {
      ComposantAssemblage c = base;
      c.fixe = false;
      if (circulaire) {
        // Rotation autour de l'axe X, Y ou Z de l'assemblage passant par l'origine (position et orientation)
        pivoterAutour(c, gp_Ax1(gp::Origin(), axe == 0 ? gp::DX() : axe == 1 ? gp::DY() : gp::DZ()), pas * k);
      } else {
        (axe == 0 ? c.x : axe == 1 ? c.y : c.z) += pas * k;
      }
      p.assemblages[static_cast<size_t>(i)].composants.push_back(c);
    }
  });
  reconstruire(false);
  majProprietes();
}

void AtelierAssemblage::symetriser(int plan) {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  // Symétrique par le plan de l'assemblage, composée avec le placement de l'original (AS-09)
  const ComposantAssemblage c = composantSymetrique(m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)], plan);
  const int i = m_indice;
  m_doc->modifier([i, c](Projet& p) { p.assemblages[static_cast<size_t>(i)].composants.push_back(c); });
  choisirComposant(static_cast<int>(m_doc->projet().assemblages[static_cast<size_t>(i)].composants.size()) - 1);
}

void AtelierAssemblage::basculerFixe() {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  if (m_choisi == 0) { m_etat->setText(QStringLiteral("Le premier composant sert de référence : il reste fixe.")); return; }
  modifierComposant(m_choisi, [](ComposantAssemblage& c) { c.fixe = !c.fixe; });
  majProprietes();
}

void AtelierAssemblage::basculerMasque() {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  modifierComposant(m_choisi, [](ComposantAssemblage& c) { c.masque = !c.masque; });
}

void AtelierAssemblage::basculerSuppression() {
  if (!composantValide(m_choisi)) return;
  modifierComposant(m_choisi, [](ComposantAssemblage& c) { c.supprime = !c.supprime; });
  resoudre();
}

void AtelierAssemblage::retirer() {
  if (!composantValide(m_choisi)) return;
  const int i = m_indice, k = m_choisi;
  // Les contraintes qui le visaient disparaissent ; les autres, et les sélections tenues ici, suivent la
  // renumérotation (AS-27)
  m_doc->modifier([i, k](Projet& p) { if (rangAssemblageValide(p, i)) retirerComposant(p.assemblages[static_cast<size_t>(i)], k); });
  auto decaler = [k](std::set<int>& e) {
    std::set<int> r;
    for (int x : e) if (x != k) r.insert(x > k ? x - 1 : x);
    e = std::move(r);
  };
  decaler(m_multi);
  decaler(m_isoles);
  if (m_premierComposant == k) { m_premierComposant = -1; m_premiereFace.clear(); } else if (m_premierComposant > k) --m_premierComposant;
  if (m_faceComposant == k) { m_faceComposant = -1; m_faceNom.clear(); m_faceForme.Nullify(); } else if (m_faceComposant > k) --m_faceComposant;
  m_choisi = -1;
  m_contrainteChoisie = -1;
  reconstruire(false);
  majProprietes();
}

// Placement par valeurs (A4) : le panneau du composant, sans boîte modale ; le champ X prend la main
void AtelierAssemblage::placerParValeurs() {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  // Même règle que le glisser et « Pivoter » : un composant fixe ne se déplace pas (AS-36)
  if (m_choisi == 0 || m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)].fixe) {
    m_etat->setText(QStringLiteral("Composant fixé : libère-le pour le déplacer."));
    return;
  }
  majProprietes();
  if (auto* x = m_props->findChild<QLineEdit*>(QStringLiteral("positionX"))) { x->setFocus(); x->selectAll(); }
  m_etat->setText(QStringLiteral("<b>Placer</b> : tape la position et les rotations dans le panneau (Entrée valide), ou un déplacement relatif."));
}

void AtelierAssemblage::pivoterParValeur() {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  if (m_choisi == 0 || m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)].fixe) {
    m_etat->setText(QStringLiteral("Composant fixé : libère-le pour le faire pivoter."));
    return;
  }
  majProprietes();
  if (auto* a = m_props->findChild<QLineEdit*>(QStringLiteral("anglePivot"))) { a->setFocus(); a->selectAll(); }
  m_etat->setText(QStringLiteral("<b>Pivoter</b> : choisis l’axe et l’angle dans le panneau, puis « Pivoter »."));
}

void AtelierAssemblage::translater(double dx, double dy, double dz) {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  if (m_choisi == 0 || m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)].fixe) {
    m_etat->setText(QStringLiteral("Composant fixé : libère-le pour le déplacer."));
    return;
  }
  ComposantAssemblage c = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)];
  c.x += dx;
  c.y += dy;
  c.z += dz;
  placerComposant(m_choisi, c);
}

void AtelierAssemblage::pivoterAutourDuCentre(int axe, double angle) {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord un composant.")); return; }
  if (m_choisi == 0 || m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)].fixe) {
    m_etat->setText(QStringLiteral("Composant fixé : libère-le pour le faire pivoter."));
    return;
  }
  // Axe de l'assemblage passant par le centre du composant : il tourne sur lui-même au lieu de partir en orbite
  gp_Pnt centre;
  if (m_choisi < static_cast<int>(m_formesPlacees.size()) && !m_formesPlacees[static_cast<size_t>(m_choisi)].IsNull()) {
    Bnd_Box b;
    BRepBndLib::Add(m_formesPlacees[static_cast<size_t>(m_choisi)], b);
    if (!b.IsVoid()) {
      double x0, y0, z0, x1, y1, z1;
      b.Get(x0, y0, z0, x1, y1, z1);
      centre = gp_Pnt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    }
  }
  const gp_Dir d = axe == 0 ? gp::DX() : axe == 1 ? gp::DY() : gp::DZ();
  ComposantAssemblage c = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)];
  pivoterAutour(c, gp_Ax1(centre, d), angle);
  placerComposant(m_choisi, c);
}

void AtelierAssemblage::basculerDeplacerSouris() {
  m_deplacer = !m_deplacer;
  m_pivotSouris = -1;
  m_vue->setGlisserObjets(m_deplacer);
  m_etat->setText(m_deplacer ? QStringLiteral("<b>Déplacer</b> : tire un composant ; il suit dans le respect de ses contraintes. Bouton à nouveau ou Échap pour finir.") : QString());
}

void AtelierAssemblage::basculerPivoterSouris(int axe) {
  const bool actif = m_deplacer && m_pivotSouris == axe;
  m_deplacer = !actif;
  m_pivotSouris = actif ? -1 : std::clamp(axe, 0, 2);
  m_vue->setGlisserObjets(m_deplacer);
  static const char* const noms[] = {"X", "Y", "Z"};
  m_etat->setText(m_deplacer ? QStringLiteral("<b>Pivoter</b> : tire un composant ; il tourne autour de l’axe %1 passant par son centre, dans le respect de ses contraintes. Échap pour finir.").arg(QString::fromLatin1(noms[m_pivotSouris]))
                             : QString());
}

void AtelierAssemblage::debutGlisser(double x, double y, double z) {
  if (!assemblageValide()) return;
  const auto sel = m_vue->selection();
  const int k = sel.empty() ? m_choisi : composantSous(sel.front());
  if (!composantValide(k)) return;
  choisirComposant(k);
  const auto& a = m_doc->projet().assemblages[static_cast<size_t>(m_indice)];
  m_avantGlisse = a.composants[static_cast<size_t>(k)];
  m_composantsGlisse = a.composants;
  m_pointGlisse = gp_Pnt(x, y, z);
  // Centre du composant (boîte de sa forme placée) : l'axe de rotation à la souris y passe
  m_centreGlisse = m_pointGlisse;
  if (k < static_cast<int>(m_formesPlacees.size()) && !m_formesPlacees[static_cast<size_t>(k)].IsNull()) {
    Bnd_Box b;
    BRepBndLib::Add(m_formesPlacees[static_cast<size_t>(k)], b);
    if (!b.IsVoid()) {
      double x0, y0, z0, x1, y1, z1;
      b.Get(x0, y0, z0, x1, y1, z1);
      m_centreGlisse = gp_Pnt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    }
  }
  m_piecesGlisse.clear();
  for (size_t i = 0; i < m_doc->projet().pieces.size(); ++i) m_piecesGlisse.push_back(resultatPiece(static_cast<int>(i)));
  m_doc->debutGeste();  // un seul pas d'annulation pour tout le glissement
}

void AtelierAssemblage::glisserVers(double dx, double dy, double dz) {
  if (!m_doc->enGeste() || !composantValide(m_choisi)) return;
  const auto& c = m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)];
  if (c.fixe || m_choisi == 0) { m_etat->setText(QStringLiteral("Composant fixé : libère-le pour le déplacer.")); return; }
  ComposantAssemblage n = m_avantGlisse;
  if (m_pivotSouris >= 0) {
    // Angle balayé par la souris autour de l'axe, vu dans le plan perpendiculaire à l'axe
    const gp_Dir axe = m_pivotSouris == 0 ? gp::DX() : m_pivotSouris == 1 ? gp::DY() : gp::DZ();
    auto dansPlan = [&](const gp_Pnt& q) { gp_Vec v(m_centreGlisse, q); return v - gp_Vec(axe) * v.Dot(gp_Vec(axe)); };
    const gp_Vec u = dansPlan(m_pointGlisse), w = dansPlan(m_pointGlisse.Translated(gp_Vec(dx, dy, dz)));
    if (u.Magnitude() < 1e-9 || w.Magnitude() < 1e-9) return;
    const double angle = std::atan2(u.Crossed(w).Dot(gp_Vec(axe)), u.Dot(w)) * 180 / std::numbers::pi;
    pivoterAutour(n, gp_Ax1(m_centreGlisse, axe), angle);
  } else {
    n.x += dx;
    n.y += dy;
    n.z += dz;
  }
  poserEnRespectant(m_choisi, n);
  reconstruire(false);
}

void AtelierAssemblage::finGlisser() {
  if (!m_doc->enGeste()) return;
  m_doc->finGeste();
  m_piecesGlisse.clear();
  resoudre();
  reconstruire(false);
  majProprietes();
}

void AtelierAssemblage::poserEnRespectant(int k, const ComposantAssemblage& cible) {
  if (!assemblageValide()) return;
  Assemblage a = m_doc->projet().assemblages[static_cast<size_t>(m_indice)];
  if (a.composants.size() != m_composantsGlisse.size()) return;
  // On repart de l'état du début du glisser : la solution ne dérive pas d'un pas de souris à l'autre
  a.composants = m_composantsGlisse;
  const auto r = deplacerDansContraintes(a, k, cible, m_piecesGlisse, &m_doc->projet().assemblages);
  if (!r) return;  // aucune pose possible : le composant reste où il était au pas précédent
  const std::vector<ComposantAssemblage> pose = *r;
  const int i = m_indice;
  m_doc->pendantGeste([i, pose](Projet& p) {
    if (!rangAssemblageValide(p, i) || p.assemblages[static_cast<size_t>(i)].composants.size() != pose.size()) return;
    p.assemblages[static_cast<size_t>(i)].composants = pose;
  });
}

void AtelierAssemblage::placerComposant(int k, const ComposantAssemblage& cible) {
  if (!composantValide(k)) return;
  const Assemblage& a = m_doc->projet().assemblages[static_cast<size_t>(m_indice)];
  std::vector<ResultatPiece> pieces;
  for (size_t i = 0; i < m_doc->projet().pieces.size(); ++i) pieces.push_back(resultatPiece(static_cast<int>(i)));
  const auto r = deplacerDansContraintes(a, k, cible, pieces, &m_doc->projet().assemblages);
  if (!r) {
    // Aucune pose qui respecte les contraintes : la demande est appliquée telle quelle, la résolution dira pourquoi
    modifierComposant(k, [cible](ComposantAssemblage& c) { c = cible; });
    resoudre();
    return;
  }
  const std::vector<ComposantAssemblage> pose = *r;
  const int i = m_indice;
  m_doc->modifier([i, pose](Projet& p) {
    if (rangAssemblageValide(p, i) && p.assemblages[static_cast<size_t>(i)].composants.size() == pose.size()) p.assemblages[static_cast<size_t>(i)].composants = pose;
  });
  resoudre();
}

void AtelierAssemblage::remplacer() {
  if (!composantValide(m_choisi)) { m_etat->setText(QStringLiteral("Clique d’abord le composant à remplacer.")); return; }
  const auto& p = m_doc->projet();
  // Rang affiché devant le nom : deux pièces de même nom restent distinctes (AS-18)
  QStringList noms;
  for (size_t i = 0; i < p.pieces.size(); ++i) noms << QStringLiteral("%1 · %2").arg(i + 1).arg(QString::fromStdString(p.pieces[i].value("nom", std::string("Pièce"))));
  if (noms.isEmpty()) return;
  // Choix dans le panneau latéral (U1) ; la pièce actuelle est présélectionnée
  auto* liste = new QListWidget;
  liste->setObjectName(QStringLiteral("piecesRemplacement"));
  liste->addItems(noms);
  const auto& actuel = p.assemblages[static_cast<size_t>(m_indice)].composants[static_cast<size_t>(m_choisi)];
  liste->setCurrentRow(actuel.assemblage < 0 && actuel.piece >= 0 && actuel.piece < noms.size() ? actuel.piece : 0);
  const int k = m_choisi;
  m_panneau->ouvrir(QStringLiteral("remplacer"), QStringLiteral("Remplacer « %1 »").arg(nomComposant(k)), QStringLiteral("piece"), liste, PanneauLateral::Boutons::ValiderAnnuler,
                    [this, liste, k] {
                      if (liste->currentRow() < 0) return false;
                      if (!composantValide(k)) return true;
                      m_choisi = k;
                      remplacerPar(liste->currentRow());
                      return true;
                    });
}

void AtelierAssemblage::remplacerPar(int nouvelle) {
  const auto& p = m_doc->projet();
  if (!composantValide(m_choisi) || !rangPieceValide(p, nouvelle)) return;
  // Une face homonyme n'est gardée que si elle a le même genre, le même rayon et la même orientation dans sa pièce ;
  // sinon la contrainte est suspendue, à rattacher (AS-18). Les liaisons sont recréées depuis l'état présent.
  const int k = m_choisi, i = m_indice;
  const Assemblage avant = p.assemblages[static_cast<size_t>(i)];
  const auto& ancien = avant.composants[static_cast<size_t>(k)];
  std::vector<ResultatPiece> pieces;
  for (size_t j = 0; j < p.pieces.size(); ++j) pieces.push_back(resultatPiece(static_cast<int>(j)));
  const ResultatPiece& rAncienne = ancien.assemblage < 0 && rangPieceValide(p, ancien.piece) ? pieces[static_cast<size_t>(ancien.piece)] : ResultatPiece();
  const ResultatPiece& rNouvelle = pieces[static_cast<size_t>(nouvelle)];
  Assemblage apres = avant;
  apres.composants[static_cast<size_t>(k)].piece = nouvelle;
  apres.composants[static_cast<size_t>(k)].assemblage = -1;
  QStringList aRattacher;
  for (size_t ci = 0; ci < apres.contraintes.size(); ++ci) {
    auto& c = apres.contraintes[ci];
    if (c.a != k && c.b != k) continue;
    // Contrainte vers l'assemblage (A2) : la référence à comparer est celle du composant
    const std::string& nom = referenceAssemblage(c.refA) ? c.refB : referenceAssemblage(c.refB) ? c.refA : c.a == k ? c.refA : c.refB;
    std::string raison;
    if (!facesCompatibles(rAncienne, rNouvelle, nom, &raison)) {
      c.suspendue = true;
      aRattacher << QStringLiteral("%1 (%2)").arg(nomType(c), QString::fromStdString(raison).toHtmlEscaped());
      continue;
    }
    if (c.type == "pivot" || c.type == "glissiere" || c.type == "vis" || c.type == "engrenage") {
      try {
        const double v = c.type == "engrenage" ? c.valeur : c.type == "vis" && c.parametres.size() >= 8 ? c.parametres[7] : 0;
        ContrainteAssemblage n = creerLiaison(apres, pieces, c.type, c.a, c.refA, c.b, c.refB, v, &m_doc->projet().assemblages);
        n.pilotee = c.pilotee;
        if (c.pilotee) n.valeur = c.valeur;
        n.suspendue = c.suspendue;
        c = n;
      } catch (const std::invalid_argument& e) {
        c.suspendue = true;
        aRattacher << QStringLiteral("%1 (%2)").arg(nomType(c), QString::fromUtf8(e.what()).toHtmlEscaped());
      }
    }
  }
  m_doc->modifier([i, apres](Projet& pr) { if (rangAssemblageValide(pr, i)) pr.assemblages[static_cast<size_t>(i)] = apres; });
  resoudre();
  m_etat->setText(aRattacher.isEmpty() ? QStringLiteral("Composant remplacé ; toutes ses contraintes ont retrouvé une face équivalente.")
                                       : QStringLiteral("<span style='color:#B7791F'>Composant remplacé ; %1 contrainte(s) suspendue(s), à rattacher : %2</span>")
                                             .arg(aRattacher.size()).arg(aRattacher.join(QStringLiteral(" ; "))));
}

// ---------- Vérifications ----------
namespace {
// Page d'analyse du panneau latéral (U1) : résumé, puis les lignes que l'appelant ajoute
QWidget* pageListe(const QString& resume) {
  auto* d = new QWidget;
  d->setObjectName(QStringLiteral("panneauAssemblage"));
  d->setStyleSheet(QString::fromLatin1(STYLE));
  auto* col = new QVBoxLayout(d);
  col->setContentsMargins(0, 0, 0, 0);
  auto* r = new QLabel(resume);
  r->setWordWrap(true);
  col->addWidget(r);
  return d;
}
}  // namespace

void AtelierAssemblage::interferences() {
  // Chaque paire de composants : interférence (volume commun), contact (à 1 µm près), jeu faible (< 0,1 mm) ou
  // indéterminé (géométrie invalide, calcul en échec), avec les tolérances documentées dans assemblage.hpp.
  // Les paires dont les boîtes sont trop loin l'une de l'autre sont écartées sans calcul exact (contactsEntre).
  std::vector<PaireContact> res = contactsEntre(m_formesPlacees);
  res.erase(std::remove_if(res.begin(), res.end(), [](const PaireContact& x) { return x.c.etat == EtatContact::JeuFaible; }), res.end());
  const int indetermines = static_cast<int>(std::count_if(res.begin(), res.end(), [](const PaireContact& x) { return x.c.etat == EtatContact::Indetermine; }));
  std::stable_sort(res.begin(), res.end(), [](const PaireContact& x, const PaireContact& y) { return static_cast<int>(x.c.etat) > static_cast<int>(y.c.etat); });
  const int nInter = static_cast<int>(std::count_if(res.begin(), res.end(), [](const PaireContact& x) { return x.c.etat == EtatContact::Interference; }));
  QString titre = nInter ? QStringLiteral("<b style='color:#C2402E'>%1 interférence(s)</b>").arg(nInter)
                         : indetermines ? QStringLiteral("<b style='color:#B7791F'>Analyse incomplète</b>")
                                        : QStringLiteral("<b style='color:#2E8B57'>✓ Aucune interférence</b> : les composants ne se pénètrent pas.");
  titre += QStringLiteral("<br><span style='color:#6B7393'>Contact : distance ≤ 0,001 mm et volume commun ≤ 0,001 mm³. Géométrie nominale, "
                          "sans tolérances de fabrication.</span>");
  QWidget* d = pageListe(titre);
  auto* col = qobject_cast<QVBoxLayout*>(d->layout());
  for (const auto& x : res) {
    QString t;
    if (x.c.etat == EtatContact::Interference) t = QStringLiteral("%1 ↔ %2 : interférence, %3 mm³ en commun").arg(nomComposant(x.a), nomComposant(x.b), QLocale().toString(x.c.volumeCommun, 'f', 3));
    else if (x.c.etat == EtatContact::Contact) t = QStringLiteral("%1 ↔ %2 : contact (surfaces qui se touchent)").arg(nomComposant(x.a), nomComposant(x.b));
    else t = QStringLiteral("%1 ↔ %2 : indéterminé, %3").arg(nomComposant(x.a), nomComposant(x.b), QString::fromStdString(x.c.detail));
    auto* b = new QPushButton(t);
    const auto fa = m_formesPlacees[static_cast<size_t>(x.a)], fb = m_formesPlacees[static_cast<size_t>(x.b)];
    connect(b, &QPushButton::clicked, this, [this, fa, fb] { m_vue->surligner({fa, fb}); });
    col->addWidget(b);
  }
  qobject_cast<QVBoxLayout*>(d->layout())->addStretch(1);
  m_panneau->ouvrir(QStringLiteral("interferences"), QStringLiteral("Interférences"), QStringLiteral("coupe"), d, PanneauLateral::Boutons::Fermer);
}

void AtelierAssemblage::jeux() {
  // Les trente plus petits jeux exacts entre composants séparés ; contacts, interférences et paires indéterminées
  // sont comptés à part, sans masquer les vrais jeux faibles (AS-29)
  constexpr std::size_t LISTE = 30;
  const BilanJeux bilan = plusPetitsJeux(m_formesPlacees, LISTE);
  const std::vector<PaireContact>& res = bilan.jeux;
  auto mm = [](double v) { return QLocale().toString(v, 'f', 3); };
  QString titre = res.empty() ? QStringLiteral("Aucun jeu entre composants séparés.")
                              : QStringLiteral("Jeu minimal : <b>%1 mm</b> (entre %2 et %3), au point (%4 ; %5 ; %6) mm.")
                                    .arg(mm(res.front().c.distance), nomComposant(res.front().a), nomComposant(res.front().b), mm(res.front().c.pointA.X()),
                                         mm(res.front().c.pointA.Y()), mm(res.front().c.pointA.Z()));
  if (bilan.contacts || bilan.interferences || bilan.indetermines)
    titre += QStringLiteral("<br>Hors jeux : %1 contact(s), %2 interférence(s), %3 paire(s) indéterminée(s) — voir « Interférences ».")
                 .arg(bilan.contacts).arg(bilan.interferences).arg(bilan.indetermines);
  if (!res.empty())
    titre += QStringLiteral("<br><span style='color:#6B7393'>Jeu faible : moins de 0,1 mm. Jeu nominal, sans tolérances de fabrication.%1</span>")
                 .arg(res.size() == LISTE ? QStringLiteral(" Les %1 plus petits jeux sont listés.").arg(LISTE) : QString());
  QWidget* d = pageListe(titre);
  auto* col = qobject_cast<QVBoxLayout*>(d->layout());
  for (const auto& x : res) {
    const QString etat = x.c.etat == EtatContact::Separees ? QString() : QStringLiteral("  ⚠ ") + QString::fromStdString(libelleContact(x.c.etat));
    auto* b = new QPushButton(QStringLiteral("%1 ↔ %2 : %3 mm%4").arg(nomComposant(x.a), nomComposant(x.b), mm(x.c.distance), etat));
    const auto fa = m_formesPlacees[static_cast<size_t>(x.a)], fb = m_formesPlacees[static_cast<size_t>(x.b)];
    connect(b, &QPushButton::clicked, this, [this, fa, fb] { m_vue->surligner({fa, fb}); });
    col->addWidget(b);
  }
  qobject_cast<QVBoxLayout*>(d->layout())->addStretch(1);
  m_panneau->ouvrir(QStringLiteral("jeux"), QStringLiteral("Analyse des jeux"), QStringLiteral("cote"), d, PanneauLateral::Boutons::Fermer);
}

void AtelierAssemblage::libertes() {
  if (m_indice < 0) return;
  const auto& p = m_doc->projet();
  const Assemblage& as = p.assemblages[static_cast<size_t>(m_indice)];
  std::vector<ResultatPiece> pieces;
  for (size_t i = 0; i < p.pieces.size(); ++i) pieces.push_back(resultatPiece(static_cast<int>(i)));
  const AnalyseAssemblage an = analyserAssemblage(as, pieces, &m_doc->projet().assemblages);
  QString resume = an.degresLiberte == 0 ? QStringLiteral("<b style='color:#2E8B57'>✓ Tout est contraint.</b>") : QStringLiteral("<b>%1 degré(s) de liberté</b> en tout.").arg(an.degresLiberte);
  resume += QStringLiteral("<br><span style='color:#6B7393'>Translations selon les axes X, Y, Z de l’assemblage ; rotations autour d’un axe parallèle à X, Y ou Z "
                           "(quel que soit le point par lequel il passe). Le détail de chaque mouvement suit le tableau.</span>");
  QWidget* d = pageListe(resume);
  auto* t = new QTableWidget(static_cast<int>(as.composants.size()), 7);
  t->setHorizontalHeaderLabels({QStringLiteral("Composant"), QStringLiteral("Tx"), QStringLiteral("Ty"), QStringLiteral("Tz"), QStringLiteral("R∥X"), QStringLiteral("R∥Y"), QStringLiteral("R∥Z")});
  t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  t->verticalHeader()->hide();
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  for (int k = 0; k < static_cast<int>(as.composants.size()); ++k) {
    const bool fixe = k == 0 || as.composants[static_cast<size_t>(k)].fixe;
    t->setItem(k, 0, new QTableWidgetItem(nomComposant(k) + (fixe ? QStringLiteral(" (fixe)") : QString())));
    for (int dd = 0; dd < 6; ++dd) {
      const bool libre = k < static_cast<int>(an.libres.size()) && an.libres[static_cast<size_t>(k)][static_cast<size_t>(dd)];
      auto* it = new QTableWidgetItem(libre ? QStringLiteral("🔓") : QStringLiteral("🔒"));
      it->setTextAlignment(Qt::AlignCenter);
      t->setItem(k, dd + 1, it);
    }
  }
  qobject_cast<QVBoxLayout*>(d->layout())->addWidget(t);
  QString detail;
  for (int k = 0; k < static_cast<int>(an.mouvements.size()); ++k)
    for (const auto& m : an.mouvements[static_cast<size_t>(k)]) detail += QStringLiteral("<br>· <b>%1</b> : %2").arg(nomComposant(k).toHtmlEscaped(), QString::fromStdString(m).toHtmlEscaped());
  if (!detail.isEmpty()) {
    auto* l = new QLabel(QStringLiteral("<b>Mouvements possibles</b>") + detail);
    l->setWordWrap(true);
    qobject_cast<QVBoxLayout*>(d->layout())->addWidget(l);
  }
  qobject_cast<QVBoxLayout*>(d->layout())->addStretch(1);
  m_panneau->ouvrir(QStringLiteral("libertes"), QStringLiteral("Degrés de liberté"), QStringLiteral("gerer-relations"), d, PanneauLateral::Boutons::Fermer);
}

void AtelierAssemblage::diagnostic() {
  if (m_indice < 0) return;
  const auto& p = m_doc->projet();
  const Assemblage& as = p.assemblages[static_cast<size_t>(m_indice)];
  std::vector<ResultatPiece> pieces;
  for (size_t i = 0; i < p.pieces.size(); ++i) pieces.push_back(resultatPiece(static_cast<int>(i)));
  const AnalyseAssemblage an = analyserAssemblage(as, pieces, &m_doc->projet().assemblages);
  QString t = QStringLiteral("<b>%1 contrainte(s)</b>, %2 degré(s) de liberté.<br>").arg(as.contraintes.size()).arg(an.degresLiberte);
  if (an.nonSatisfaites.empty()) t += QStringLiteral("<span style='color:#2E8B57'>✓ Toutes les contraintes actives sont satisfaites.</span>");
  else {
    t += QStringLiteral("<span style='color:#C2402E'>✖ Contraintes non satisfaites (contradictoires, ou face disparue) :</span>");
    for (int k : an.nonSatisfaites) t += QStringLiteral("<br>· %1 : %2 ↔ %3").arg(nomType(as.contraintes[static_cast<size_t>(k)]), nomComposant(as.contraintes[static_cast<size_t>(k)].a), nomComposant(as.contraintes[static_cast<size_t>(k)].b));
    t += QStringLiteral("<br>Suspends ou supprime l’une d’elles (clic droit dans l’arbre, ou panneau du composant).");
  }
  int suspendues = 0;
  for (const auto& c : as.contraintes) suspendues += c.suspendue;
  if (suspendues) t += QStringLiteral("<br>⏸ %1 contrainte(s) suspendue(s).").arg(suspendues);
  QWidget* d = pageListe(t);
  qobject_cast<QVBoxLayout*>(d->layout())->addStretch(1);
  m_panneau->ouvrir(QStringLiteral("diagnostic"), QStringLiteral("Diagnostic des contraintes"), QStringLiteral("contraintes"), d, PanneauLateral::Boutons::Fermer);
}

void AtelierAssemblage::proprietesPhysiques() {
  if (m_indice < 0) return;
  const auto& p = m_doc->projet();
  std::vector<Piece> definitions;
  std::vector<ResultatPiece> resultats;
  for (size_t i = 0; i < p.pieces.size(); ++i) {
    definitions.push_back(pieceDepuisJson(p.pieces[i]));
    resultats.push_back(resultatPiece(static_cast<int>(i)));
  }
  const ProprietesAssemblage pa = proprietesAssemblage(p.assemblages, m_indice, definitions, resultats);
  auto N = [](double v, int d) { return QLocale().toString(v, 'f', d); };
  QString texte = QStringLiteral("<b>Masse</b> : %1%2 g<br><b>Volume</b> : %3 cm³ (somme des %4 pièces, volumes en commun non retirés)<br>"
                                 "<b>Centre de gravité</b> : X %5 · Y %6 · Z %7 mm<br><span style='color:#6B7393'>Masse volumique de chaque pièce d’après sa matière ; "
                                 "sous-assemblages compris, composants suspendus exclus.</span>")
                      .arg(pa.fiable() ? QString() : QStringLiteral("≈ "), N(pa.masse, 1), N(pa.volume / 1000, 2), QString::number(pa.pieces), N(pa.centre.X(), 1),
                           N(pa.centre.Y(), 1), N(pa.centre.Z(), 1));
  if (pa.provisoires)
    texte += QStringLiteral("<p style='color:#B7791F'><b>Provisoire</b> : %1 pièce(s) sans matière confirmée (densité par défaut).</p>").arg(pa.provisoires);
  if (!pa.exclues.empty()) {
    texte += QStringLiteral("<p style='color:#C2402E'><b>Incomplet</b> : pièces écartées du calcul :</p><ul>");
    for (const auto& e : pa.exclues) texte += QStringLiteral("<li>%1</li>").arg(QString::fromStdString(e).toHtmlEscaped());
    texte += QStringLiteral("</ul>");
  }
  QWidget* d = pageListe(texte);
  qobject_cast<QVBoxLayout*>(d->layout())->addStretch(1);
  m_panneau->ouvrir(QStringLiteral("proprietesPhysiques"), QStringLiteral("Propriétés physiques"), QStringLiteral("masse"), d, PanneauLateral::Boutons::Fermer);
}

// ---------- Documentation ----------
// Plan d'assemblage (M6) : les composants placés, une bulle par pièce reliée au centre de sa face la plus tournée vers
// l'observateur de la vue isométrique, et la nomenclature (repères dans l'ordre de la nomenclature de l'assemblage).
void AtelierAssemblage::miseEnPlan() {
  if (auto dlg = creerMiseEnPlan()) dlg->exec();
}

std::unique_ptr<MiseEnPlan> AtelierAssemblage::creerMiseEnPlan() {
  if (!assemblageValide()) return nullptr;
  const auto& p = m_doc->projet();
  const Assemblage& a = p.assemblages[static_cast<size_t>(m_indice)];
  const auto quantites = nomenclatureMecanique(p, m_indice);
  std::vector<OptionsMiseEnPlan::LigneNomenclature> lignes;
  for (const auto& l : quantites) {
    if (l.piece < 0 || l.piece >= static_cast<int>(p.pieces.size())) continue;
    const Piece pc = pieceDepuisJson(p.pieces[static_cast<size_t>(l.piece)]);
    OptionsMiseEnPlan::LigneNomenclature x;
    x.repere = repereArticle(p, false, l.piece, l.symetrique);  // repère stable de la pièce (M10)
    if (x.repere <= 0) x.repere = static_cast<int>(lignes.size()) + 1;
    x.quantite = l.quantite;
    x.designation = pc.nom + (l.symetrique ? " (symétrique)" : "");
    x.materiau = pc.materiau;
    x.reference = (pc.reference.empty() ? "P" + std::to_string(l.piece + 1) : pc.reference) + (l.symetrique ? "-S" : "");
    lignes.push_back(x);
  }
  BRep_Builder bb;
  TopoDS_Compound ensemble;
  bb.MakeCompound(ensemble);
  std::vector<OptionsMiseEnPlan::Bulle> bulles;
  const gp_Vec observateur(-1 / std::sqrt(3.0), -1 / std::sqrt(3.0), 1 / std::sqrt(3.0));
  int sansBulle = 0;
  for (std::size_t k = 0; k < a.composants.size() && k < m_formesPlacees.size(); ++k) {
    const ComposantAssemblage& c = a.composants[k];
    const TopoDS_Shape& forme = m_formesPlacees[k];
    if (c.supprime || forme.IsNull()) continue;
    bb.Add(ensemble, forme);
    if (c.assemblage >= 0) { ++sansBulle; continue; }
    int repere = 0;
    for (std::size_t r = 0, i = 0; r < quantites.size(); ++r) {
      if (quantites[r].piece < 0 || quantites[r].piece >= static_cast<int>(p.pieces.size())) continue;
      if (quantites[r].piece == c.piece && quantites[r].symetrique == (c.miroir != 0)) repere = lignes[i].repere;
      ++i;
    }
    if (!repere) continue;
    // Ancre : centre de la face la plus en vue (aire × orientation vers l'observateur)
    gp_Pnt ancre;
    double meilleur = 0;
    for (TopExp_Explorer ex(forme, TopAbs_FACE); ex.More(); ex.Next()) {
      const TopoDS_Face f = TopoDS::Face(ex.Current());
      GProp_GProps g;
      BRepGProp::SurfaceProperties(f, g);
      const gp_Pnt centre = g.CentreOfMass();
      BRepAdaptor_Surface sf(f);
      if (sf.GetType() != GeomAbs_Plane) continue;
      gp_Dir n = sf.Plane().Axis().Direction();
      if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
      const double score = g.Mass() * gp_Vec(n).Dot(observateur);
      if (score > meilleur) { meilleur = score; ancre = centre; }
    }
    if (meilleur <= 0) {
      GProp_GProps g;
      BRepGProp::VolumeProperties(forme, g);
      ancre = g.CentreOfMass();
    }
    bulles.push_back({repere, ancre});
  }
  PlanPiece reglages;
  reglages.nom = "Plan d'assemblage";
  reglages.cachees = false;
  reglages.axes = false;
  auto dlg = std::make_unique<MiseEnPlan>(ensemble, reglages, a.nom, std::string(), this);
  dlg->setAssemblage(lignes, bulles);
  if (sansBulle) m_etat->setText(QStringLiteral("%1 sous-assemblage(s) sans bulle : leurs pièces sont dans la nomenclature.").arg(sansBulle));
  return dlg;
}

// Masse d'une pièce du projet pour la nomenclature : géométrie construite × densité (provisoire si non confirmée)
MassePiece AtelierAssemblage::massePiece(int i) {
  const auto& p = m_doc->projet();
  if (!rangPieceValide(p, i)) return {};
  const Piece pc = pieceDepuisJson(p.pieces[static_cast<size_t>(i)]);
  const ProprietesMasse pm = proprietesMasse(resultatPiece(i).forme, pc.densite);
  return {pm.masse, pm.statut != StatutMasse::Vide && pm.masse > 0, !pc.densiteConfirmee};
}

DialogueNomenclature* AtelierAssemblage::nomenclature() {
  if (!assemblageValide()) return nullptr;
  // Arbre à niveaux, premier niveau ou pièces seules ; repères stables, colonnes au choix, masses, CSV et XLSX (M10)
  auto* d = new DialogueNomenclature(m_doc, m_indice, [this](int i) { return massePiece(i); }, this);
  d->setAttribute(Qt::WA_DeleteOnClose);
  d->show();
  return d;
}

void AtelierAssemblage::exporterStep() {
  const auto& p = m_doc->projet();
  if (m_indice < 0 || m_formesPlacees.empty()) return;
  // AP214 (lu partout) ou AP242 (norme actuelle d'échange 3D) : le filtre choisi décide
  const QString ap214 = QStringLiteral("STEP AP214 (*.step *.stp)"), ap242 = QStringLiteral("STEP AP242 (*.step *.stp)");
  QString filtre = ap214;
  QString chemin = QFileDialog::getSaveFileName(this, QStringLiteral("Exporter l’assemblage"), QString::fromStdString(p.assemblages[static_cast<size_t>(m_indice)].nom) + QStringLiteral(".step"),
                                                ap214 + QStringLiteral(";;") + ap242, &filtre);
  if (chemin.isEmpty()) return;
  if (const QString suffixe = QFileInfo(chemin).suffix().toLower(); suffixe != QLatin1String("step") && suffixe != QLatin1String("stp")) chemin += QStringLiteral(".step");
  OptionsStep options;
  options.schema = filtre == ap242 ? SchemaStep::AP242 : SchemaStep::AP214;
  // Depuis le modèle : un vrai assemblage STEP (prototype par pièce, nommé et coloré ; une instance placée par
  // composant ; sous-assemblages imbriqués ; pièce symétrique distincte), EX-02, EX-07, EX-09
  try {
    const int manquants = prom::exporterStepAssemblage(p, m_indice, [this](int i) -> const ResultatPiece& { return resultatPiece(i); }, chemin.toStdString(), options);
    m_etat->setText(manquants ? QStringLiteral("<span style='color:#B7791F'>Assemblage exporté en STEP ; %1 composant(s) sans géométrie n’y figurent pas.</span>").arg(manquants)
                              : QStringLiteral("Assemblage exporté en STEP."));
  } catch (const std::exception& e) {
    // Une erreur d'écriture (dossier protégé, support retiré) se dit, elle n'arrête pas le logiciel (EX-03)
    QMessageBox::warning(this, QStringLiteral("Export STEP"), QString::fromUtf8(e.what()));
  } catch (const Standard_Failure& e) {
#if OCC_VERSION_HEX >= 0x080000
    QMessageBox::warning(this, QStringLiteral("Export STEP"), QString::fromUtf8(e.what()));
#else
    QMessageBox::warning(this, QStringLiteral("Export STEP"), QString::fromUtf8(e.GetMessageString()));
#endif
  }
}

QString AtelierAssemblage::texteEtat() const { return m_etat->text(); }

void AtelierAssemblage::basculerMesure() {
  m_mesure = !m_mesure;
  m_vue->setCumulatif(m_mesure);
  m_vue->viderSelection();
  m_vue->setTracesMesure({});
  m_etat->setText(m_mesure ? QStringLiteral("<b>Mesure</b> : clique les éléments un à un (ils s’ajoutent). Mesurer à nouveau pour finir.") : QString());
}

void AtelierAssemblage::setEclatement(double f) {
  m_eclatement = std::clamp(f, 0.0, 3.0);
  reconstruire(false);
}

void AtelierAssemblage::panneauEclate() {
  if (m_panneauEclate) { m_panneauEclate->setVisible(!m_panneauEclate->isVisible()); if (!m_panneauEclate->isVisible()) setEclatement(0); return; }
  m_panneauEclate = new QFrame(m_barre->parentWidget());
  m_panneauEclate->setObjectName(QStringLiteral("panneauEclate"));
  m_panneauEclate->setStyleSheet(QStringLiteral("#panneauEclate { background: rgba(255,255,255,240); border: 1px solid #CDD3E4; border-radius: 9px; }"));
  auto* l = new QHBoxLayout(m_panneauEclate);
  l->setContentsMargins(10, 6, 10, 6);
  l->addWidget(new QLabel(QStringLiteral("<b>Vue éclatée</b>")));
  auto* s = new QSlider(Qt::Horizontal);
  s->setRange(0, 100);
  s->setMinimumWidth(220);
  connect(s, &QSlider::valueChanged, this, [this](int v) { setEclatement(v / 100.0); });
  l->addWidget(s);
  auto* fermer = new QToolButton;
  fermer->setIcon(icone(QStringLiteral("annuler")));
  connect(fermer, &QToolButton::clicked, this, [this] { m_panneauEclate->hide(); setEclatement(0); });
  l->addWidget(fermer);
  m_panneauEclate->adjustSize();
  m_panneauEclate->move(14, m_panneauEclate->parentWidget()->height() - m_panneauEclate->height() - 14);
  m_panneauEclate->show();
  m_panneauEclate->raise();
}

void AtelierAssemblage::vueEnCoupe() {
  if (m_panneauCoupe) {
    const bool montrer = !m_panneauCoupe->isVisible();
    m_panneauCoupe->setVisible(montrer);
    if (!montrer) m_vue->setCoupe(false);
    return;
  }
  m_panneauCoupe = new QFrame(m_barre->parentWidget());
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
  auto* fermer = new QToolButton;
  fermer->setIcon(icone(QStringLiteral("annuler")));
  l->addWidget(new QLabel(QStringLiteral("<b>Coupe</b>")));
  l->addWidget(plan);
  l->addWidget(curseur);
  l->addWidget(inverser);
  l->addWidget(fermer);
  auto appliquer = [this, plan, curseur, inverser] {
    Bnd_Box b;
    for (const auto& f : m_formesPlacees) if (!f.IsNull()) BRepBndLib::Add(f, b);
    if (b.IsVoid()) return;
    double x0, y0, z0, x1, y1, z1;
    b.Get(x0, y0, z0, x1, y1, z1);
    const int i = plan->currentIndex();
    const double mn = i == 0 ? z0 : i == 1 ? y0 : x0, mx = i == 0 ? z1 : i == 1 ? y1 : x1;
    const double pos = mn + (mx - mn) * curseur->value() / 1000.0;
    gp_Dir n = i == 0 ? gp_Dir(0, 0, -1) : i == 1 ? gp_Dir(0, -1, 0) : gp_Dir(-1, 0, 0);
    if (inverser->isChecked()) n.Reverse();
    m_vue->setCoupe(true, gp_Pln(i == 0 ? gp_Pnt(0, 0, pos) : i == 1 ? gp_Pnt(0, pos, 0) : gp_Pnt(pos, 0, 0), n));
  };
  connect(plan, &QComboBox::currentIndexChanged, this, appliquer);
  connect(curseur, &QSlider::valueChanged, this, appliquer);
  connect(inverser, &QCheckBox::toggled, this, appliquer);
  connect(fermer, &QToolButton::clicked, this, [this] { m_panneauCoupe->hide(); m_vue->setCoupe(false); });
  m_panneauCoupe->adjustSize();
  m_panneauCoupe->move(14, 14);
  m_panneauCoupe->show();
  m_panneauCoupe->raise();
  appliquer();
}


// ---------- Simulation, animation, sous-assemblage ----------
bool AtelierAssemblage::simuler(int contrainte, double valeur) {
  if (!assemblageValide()) return false;
  Assemblage a = m_doc->projet().assemblages[static_cast<size_t>(m_indice)];
  if (contrainte < 0 || contrainte >= static_cast<int>(a.contraintes.size())) return false;
  // Départ de l'image précédente (positions et tours suivis) : mouvement continu
  if (m_apercu && m_apercu->composants.size() == a.composants.size() && m_apercu->contraintes.size() == a.contraintes.size()) {
    a.composants = m_apercu->composants;
    for (size_t k = 0; k < a.contraintes.size(); ++k) a.contraintes[k].angleSuivi = m_apercu->contraintes[k].angleSuivi;
  }
  a.contraintes[static_cast<size_t>(contrainte)].pilotee = true;
  a.contraintes[static_cast<size_t>(contrainte)].valeur = valeur;
  std::vector<ResultatPiece> pieces;
  for (size_t i = 0; i < m_doc->projet().pieces.size(); ++i) pieces.push_back(resultatPiece(static_cast<int>(i)));
  // Une position que le mécanisme n'atteint pas n'est pas montrée : l'image précédente reste (AS-19)
  if (!resoudreAssemblage(a, pieces, &m_doc->projet().assemblages).resolu) return false;
  m_apercu = a;
  reconstruire(false);
  return true;
}

void AtelierAssemblage::arreterSimulation() {
  m_apercu.reset();
  reconstruire(false);
}

void AtelierAssemblage::panneauSimulation() {
  if (!assemblageValide()) return;
  if (m_panneauSimulation) { m_panneauSimulation->deleteLater(); m_panneauSimulation = nullptr; arreterSimulation(); return; }
  const Assemblage& as = m_doc->projet().assemblages[static_cast<size_t>(m_indice)];
  std::vector<int> pilotables;
  for (size_t k = 0; k < as.contraintes.size(); ++k) {
    const auto& c = as.contraintes[k];
    if (!c.suspendue && (c.type == "pivot" || c.type == "glissiere" || c.type == "vis")) pilotables.push_back(static_cast<int>(k));
  }
  if (pilotables.empty()) { m_etat->setText(QStringLiteral("Pas de liaison à piloter : crée d’abord un pivot, une glissière ou une vis-écrou.")); return; }
  m_panneauSimulation = new QFrame(m_barre->parentWidget());
  m_panneauSimulation->setObjectName(QStringLiteral("panneauSimulation"));
  m_panneauSimulation->setStyleSheet(QStringLiteral("#panneauSimulation { background: rgba(255,255,255,242); border: 1px solid #CDD3E4; border-radius: 9px; }"));
  auto* l = new QHBoxLayout(m_panneauSimulation);
  l->setContentsMargins(10, 6, 10, 6);
  l->addWidget(new QLabel(QStringLiteral("<b>Simulation</b>")));
  auto* liaison = new QComboBox;
  for (int k : pilotables) {
    const auto& c = as.contraintes[static_cast<size_t>(k)];
    liaison->addItem(QStringLiteral("%1 : %2 ↔ %3").arg(c.type == "pivot" ? QStringLiteral("Pivot") : c.type == "glissiere" ? QStringLiteral("Glissière") : QStringLiteral("Vis-écrou"), nomComposant(c.a), nomComposant(c.b)), k);
  }
  l->addWidget(liaison);
  auto* de = new QSpinBox;
  auto* a2 = new QSpinBox;
  de->setRange(-100000, 100000);
  a2->setRange(-100000, 100000);
  de->setValue(0);
  a2->setValue(360);
  l->addWidget(new QLabel(QStringLiteral("de")));
  l->addWidget(de);
  l->addWidget(new QLabel(QStringLiteral("à")));
  l->addWidget(a2);
  auto* curseur = new QSlider(Qt::Horizontal);
  curseur->setRange(0, 200);
  curseur->setMinimumWidth(220);
  l->addWidget(curseur);
  auto* lecture = new QToolButton;
  lecture->setText(QStringLiteral("▶"));
  lecture->setCheckable(true);
  l->addWidget(lecture);
  auto* fermer = new QToolButton;
  fermer->setIcon(icone(QStringLiteral("annuler")));
  l->addWidget(fermer);
  auto unites = [liaison, as] {
    const int k = liaison->currentData().toInt();
    return as.contraintes[static_cast<size_t>(k)].type == "glissiere" ? QStringLiteral(" mm") : QStringLiteral(" °");
  };
  de->setSuffix(unites());
  a2->setSuffix(unites());
  connect(liaison, &QComboBox::currentIndexChanged, this, [=, this] { de->setSuffix(unites()); a2->setSuffix(unites()); a2->setValue(liaison->currentText().startsWith(QStringLiteral("Glissière")) ? 20 : 360); m_apercu.reset(); curseur->setValue(0); });
  connect(curseur, &QSlider::valueChanged, this, [=, this](int v) {
    const double val = de->value() + (a2->value() - de->value()) * v / 200.0;
    if (!simuler(liaison->currentData().toInt(), val)) {
      // Le mécanisme se bloque : lecture arrêtée, dernière position atteinte gardée à l'écran
      lecture->setChecked(false);
      m_etat->setText(QStringLiteral("<span style='color:#C2402E'><b>Simulation</b> : le mécanisme se bloque avant %1%2 (position impossible ou point mort).</span>")
                          .arg(QLocale().toString(val, 'f', 1), unites()));
      return;
    }
    m_etat->setText(QStringLiteral("<b>Simulation</b> : %1%2").arg(QLocale().toString(val, 'f', 1), unites()));
  });
  auto* minuterie = new QTimer(m_panneauSimulation);
  minuterie->setInterval(40);
  connect(minuterie, &QTimer::timeout, this, [curseur] { curseur->setValue((curseur->value() + 2) % 201); });
  connect(lecture, &QToolButton::toggled, this, [minuterie, lecture](bool on) { lecture->setText(on ? QStringLiteral("⏸") : QStringLiteral("▶")); on ? minuterie->start() : minuterie->stop(); });
  connect(fermer, &QToolButton::clicked, this, [this] { panneauSimulation(); });
  m_panneauSimulation->adjustSize();
  m_panneauSimulation->move(14, m_panneauSimulation->parentWidget()->height() - m_panneauSimulation->height() - 14);
  m_panneauSimulation->show();
  m_panneauSimulation->raise();
}

void AtelierAssemblage::arreterAnimation() {
  if (m_animation) m_animation->stop();
  m_etapeAnimation = 0;
  m_eclatementComposant.clear();
}

void AtelierAssemblage::animationMontage() {
  if (!assemblageValide()) return;
  const int n = static_cast<int>(assemblageAffiche().composants.size());
  if (n < 2) return;
  // Une seule minuterie, relancée depuis le début à chaque clic (AS-26)
  arreterAnimation();
  if (!m_animation) {
    m_animation = new QTimer(this);
    connect(m_animation, &QTimer::timeout, this, [this] {
      const int nb = static_cast<int>(m_eclatementComposant.size());
      const int k = 1 + m_etapeAnimation / 12, sous = m_etapeAnimation % 12;
      if (k >= nb || !assemblageValide()) { arreterAnimation(); reconstruire(false); return; }
      m_eclatementComposant[static_cast<size_t>(k)] = 1.2 * (1 - (sous + 1) / 12.0);
      ++m_etapeAnimation;
      reconstruire(false);
    });
  }
  // Tous les composants partent écartés ; ils rejoignent leur place l'un après l'autre
  m_eclatementComposant.assign(static_cast<size_t>(n), 1.2);
  m_eclatementComposant[0] = 0;
  reconstruire(false);
  m_animation->start(50);
}

void AtelierAssemblage::creerSousAssemblageDepuisSelection() {
  std::set<int> choisis = m_multi;
  if (m_choisi >= 0) choisis.insert(m_choisi);
  creerSousAssemblage(choisis);
}

void AtelierAssemblage::creerSousAssemblage(const std::set<int>& choisis) {
  if (!assemblageValide()) return;
  if (choisis.size() < 2) { m_etat->setText(QStringLiteral("Choisis au moins deux composants (Maj + clic), puis « Créer un sous-assemblage »."));
    return; }
  const int i = m_indice;
  const int gardees = static_cast<int>(contraintesCoupees(m_doc->projet().assemblages[static_cast<size_t>(i)], choisis).size());
  std::vector<ResultatPiece> pieces;
  for (size_t k = 0; k < m_doc->projet().pieces.size(); ++k) pieces.push_back(resultatPiece(static_cast<int>(k)));
  int rang = -1, liaisonsPerdues = 0;
  m_doc->modifier([i, choisis, &rang, &pieces, &liaisonsPerdues](Projet& p) {
    rang = prom::creerSousAssemblage(p, i, choisis);
    if (rang < 0) return;
    // Les contraintes qui reliaient la sélection au reste visent maintenant une face du sous-assemblage (AS-16) ;
    // les liaisons y retrouvent leur état de départ
    Assemblage& a = p.assemblages[static_cast<size_t>(i)];
    for (auto& c : a.contraintes) {
      const bool liaison = c.type == "pivot" || c.type == "glissiere" || c.type == "vis" || c.type == "engrenage";
      if (!liaison || !c.parametres.empty()) continue;
      try {
        const double v = c.type == "engrenage" ? c.valeur : 0;
        ContrainteAssemblage n = creerLiaison(a, pieces, c.type, c.a, c.refA, c.b, c.refB, v, &p.assemblages);
        n.pilotee = c.pilotee;
        if (c.pilotee) n.valeur = c.valeur;
        n.suspendue = c.suspendue;
        c = n;
      } catch (const std::exception&) {
        c.suspendue = true;
        ++liaisonsPerdues;
      }
    }
  });
  oublierSelection();
  if (rang >= 0) resoudre();
  reconstruire(false);
  majProprietes();
  if (rang < 0) { m_etat->setText(QStringLiteral("Sous-assemblage impossible : choisis au moins deux composants de cet assemblage.")); return; }
  QString t = QStringLiteral("Sous-assemblage créé ; il se range sous l’assemblage dans l’arbre.");
  if (gardees) t += QStringLiteral(" %1 contrainte(s) qui le reliaient au reste gardée(s) : elles visent ses faces, il reste en position.").arg(gardees);
  if (liaisonsPerdues) t += QStringLiteral(" <span style='color:#A8620E'>%1 liaison(s) suspendue(s) : à recréer.</span>").arg(liaisonsPerdues);
  m_etat->setText(t);
}

// ---------- Validité des rangs tenus par l'atelier ----------
bool AtelierAssemblage::assemblageValide() const { return rangAssemblageValide(m_doc->projet(), m_indice); }

bool AtelierAssemblage::composantValide(int k) const {
  return assemblageValide() && k >= 0 && k < static_cast<int>(m_doc->projet().assemblages[static_cast<size_t>(m_indice)].composants.size());
}

void AtelierAssemblage::oublierSelection() {
  m_choisi = -1;
  m_contrainteChoisie = -1;
  m_multi.clear();
  m_isoles.clear();
  m_premierComposant = -1;
  m_premiereFace.clear();
  m_faceComposant = -1;
  m_faceNom.clear();
  m_faceForme.Nullify();
  m_secondComposant = -1;
  m_secondNom.clear();
  if (m_vue) m_vue->viderSelection();
}

void AtelierAssemblage::revaliderSelection() {
  if (!assemblageValide()) {
    // Assemblage supprimé ou création annulée : l'atelier n'affiche plus rien
    if (m_indice >= 0) { m_indice = -1; arreterAnimation(); m_apercu.reset(); }
    oublierSelection();
    return;
  }
  const Assemblage& a = m_doc->projet().assemblages[static_cast<size_t>(m_indice)];
  const int n = static_cast<int>(a.composants.size());
  auto hors = [n](int k) { return k >= n; };
  if (hors(m_choisi)) m_choisi = -1;
  if (m_contrainteChoisie >= static_cast<int>(a.contraintes.size())) m_contrainteChoisie = -1;
  if (hors(m_premierComposant)) { m_premierComposant = -1; m_premiereFace.clear(); }
  if (hors(m_faceComposant)) { m_faceComposant = -1; m_faceNom.clear(); m_faceForme.Nullify(); }
  std::erase_if(m_multi, hors);
  std::erase_if(m_isoles, hors);
  if (m_apercu && m_apercu->composants.size() != a.composants.size()) m_apercu.reset();
  if (m_eclatementComposant.size() > a.composants.size()) arreterAnimation();
}
