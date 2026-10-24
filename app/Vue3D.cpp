// Prométhée : vue 3D (OpenCascade dans un QOpenGLWidget). Licence GPL-3.0-only.
#include "Vue3D.hpp"

#include <algorithm>
#include <sstream>

#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <AIS_AnimationCamera.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_NameOfMaterial.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <StdPrs_ToolTriangulatedShape.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Trsf.hxx>

#include "OcctGlTools.hpp"
#include "Theme.hpp"

// Dictionnaire de texte d'OpenCascade : le nom historique est obsolète depuis la 7.8.
#if OCC_VERSION_HEX >= 0x070800
#include <NCollection_IndexedDataMap.hxx>
#include <TCollection_AsciiString.hxx>
using DictionnaireTexte = NCollection_IndexedDataMap<TCollection_AsciiString, TCollection_AsciiString>;
#else
#include <TColStd_IndexedDataMapOfStringString.hxx>
using DictionnaireTexte = TColStd_IndexedDataMapOfStringString;
#endif
#include "promethee/geometrie.hpp"

using namespace prom;

namespace {
Quantity_Color rgb(int r, int g, int b) { return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB); }

Quantity_Color couleurComposant(const Composant& k) {
  if (k.type == "led") {
    std::string s = k.valeur;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (s.find("vert") != std::string::npos) return rgb(0x2E, 0x9F, 0x58);
    if (s.find("bleu") != std::string::npos) return rgb(0x2F, 0x6B, 0xD0);
    if (s.find("jaune") != std::string::npos) return rgb(0xE2, 0xB2, 0x2E);
    if (s.find("blanc") != std::string::npos) return rgb(0xEE, 0xEE, 0xE6);
    return rgb(0xD2, 0x3A, 0x2C);
  }
  if (k.type == "module") return rgb(0xB8, 0xBF, 0xC5);
  if (k.type == "capteur") return rgb(0xAE, 0xB4, 0xB9);
  if (k.type == "usbc") return rgb(0xC3, 0xC8, 0xCC);
  if (k.type == "jst") return rgb(0xE8, 0xDF, 0xC8);
  if (k.type == "condo") return rgb(0x22, 0x31, 0x5A);
  return rgb(0x25, 0x28, 0x2A);
}

void styliser(const Handle(AIS_Shape)& o, const Quantity_Color& c) {
  o->SetColor(c);
  o->SetMaterial(Graphic3d_NOM_PLASTIC);
  o->Attributes()->SetFaceBoundaryDraw(true);
  o->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(rgb(0x56, 0x61, 0x5B), Aspect_TOL_SOLID, 1.0));
}

template <typename... T> std::string signature(const T&... v) {
  std::ostringstream s;
  s.precision(10);
  ((s << v << '|'), ...);
  return s.str();
}
}  // namespace

Vue3D::Vue3D(Document* doc, QWidget* parent) : QOpenGLWidget(parent), m_doc(doc) {
  m_navigation = std::make_unique<Navigation3D>([this] { return m_vue; }, [this] { update(); }, [this](const gp_Pnt& p) { m_pivot = p; });
  Handle(Aspect_DisplayConnection) affichage;
#ifdef PROMETHEE_X11
  affichage = new Xw_DisplayConnection();
#endif
  Handle(OpenGl_GraphicDriver) pilote = new OpenGl_GraphicDriver(affichage, false);
  pilote->ChangeOptions().buffersNoSwap = true;
  pilote->ChangeOptions().buffersOpaqueAlpha = true;
  pilote->ChangeOptions().useSystemBuffer = false;
  m_visu = new V3d_Viewer(pilote);
  m_visu->SetDefaultBackgroundColor(Quantity_NOC_WHITE);
  m_visu->SetDefaultLights();
  m_visu->SetLightOn();
  m_ctx = new AIS_InteractiveContext(m_visu);
  // Maillage d'affichage fin : arrondis lisses, sans facettes visibles.
  m_ctx->DefaultDrawer()->SetDeviationCoefficient(0.0003);
  m_ctx->DefaultDrawer()->SetDeviationAngle(3.0 * 3.14159265358979 / 180.0);
  // Sélection en bleu franc, survol en bleu clair : la pièce choisie se voit tout de suite.
  m_ctx->SelectionStyle()->SetColor(rgb(0x1E, 0x7B, 0xE0));
  m_ctx->SelectionStyle()->SetDisplayMode(AIS_Shaded);
  m_ctx->SelectionStyle()->SetTransparency(0.0f);
  m_ctx->HighlightStyle()->SetColor(rgb(0x7F, 0xB8, 0xF5));
  m_ctx->HighlightStyle()->SetDisplayMode(AIS_Shaded);
  m_vue = m_visu->CreateView();
  m_vue->SetImmediateUpdate(false);
#ifndef __APPLE__
  m_vue->ChangeRenderingParams().NbMsaaSamples = 4;
#endif
  appliquerTheme();
  connect(&Theme::instance(), &Theme::change, this, &Vue3D::appliquerTheme);
  m_vue->SetProj(V3d_XnegYnegZpos);
  // Gauche : tourner (géré par Vue3D::tourner) ; droit ou milieu : déplacer ; molette : zoomer.
  auto& gestes = ChangeMouseGestureMap();
  gestes.Clear();
  gestes.Bind(Aspect_VKeyMouse_RightButton, AIS_MouseGesture_Pan);
  gestes.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_Pan);
  // Cube de vue : un clic sur une face, une arête ou un coin amène la vue correspondante, en douceur.
  m_cube = new AIS_ViewCube();
  m_cube->SetViewAnimation(myViewAnimation);
  m_cube->SetFixedAnimationLoop(false);
  m_cube->SetAutoStartAnimation(true);
  m_cube->SetSize(66);
  m_cube->SetFontHeight(11);
  m_cube->SetBoxColor(rgb(0xE8, 0xEC, 0xE9));
  m_cube->SetBoxSideLabel(V3d_Xpos, "Droite");
  m_cube->SetBoxSideLabel(V3d_Xneg, "Gauche");
  m_cube->SetBoxSideLabel(V3d_Ypos, "Arrière");
  m_cube->SetBoxSideLabel(V3d_Yneg, "Face");
  m_cube->SetBoxSideLabel(V3d_Zpos, "Dessus");
  m_cube->SetBoxSideLabel(V3d_Zneg, "Dessous");
  m_cube->TransformPersistence()->SetCorner2d(Aspect_TOTP_RIGHT_UPPER);
  m_cube->TransformPersistence()->SetOffset2d(NCollection_Vec2<int>(80, 80));
  myViewAnimation->SetOwnDuration(0.35);

  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  m_minuteur.setSingleShot(true);
  m_minuteur.setInterval(15);
  connect(&m_minuteur, &QTimer::timeout, this, [this] { reconstruire(); });
  connect(doc, &Document::change, this, [this] { planifier(); });
  connect(doc, &Document::selectionChangee, this, [this] { afficherSelection(); });
}

Vue3D::~Vue3D() {
  if (m_ouvrier.joinable()) m_ouvrier.join();
  Handle(Aspect_DisplayConnection) affichage = m_visu->Driver()->GetDisplayConnection();
  m_ctx->RemoveAll(false);
  m_ctx.Nullify();
  m_vue->Remove();
  m_vue.Nullify();
  m_visu.Nullify();
  makeCurrent();
  affichage.Nullify();
}

// La vue peut être créée cachée (derrière l'accueil) : OpenCascade n'attache sa fenêtre qu'une fois visible.
void Vue3D::initializeGL() {
  if (isVisible()) initialiserOcct();
}

bool Vue3D::initialiserOcct() {
  Handle(OpenGl_GraphicDriver) pilote = Handle(OpenGl_GraphicDriver)::DownCast(m_visu->Driver());
  pilote->ChangeOptions().contextCompatible = format().profile() != QSurfaceFormat::CoreProfile;
  const NCollection_Vec2<int> taille(rect().right() - rect().left(), rect().bottom() - rect().top());
  if (!occtqt::initialiserFenetre(m_vue, (Aspect_Drawable)effectiveWinId(), taille, devicePixelRatioF())) {
    m_infoGl = QStringLiteral("OpenGL indisponible pour la vue 3D.");
    return false;
  }
  makeCurrent();
  DictionnaireTexte infos;
  m_vue->DiagnosticInformation(infos, Graphic3d_DiagnosticInfo_Basic);
  m_infoGl.clear();
  for (DictionnaireTexte::Iterator it(infos); it.More(); it.Next())
    m_infoGl += QString::fromUtf8(it.Key().ToCString()) + QStringLiteral(" : ") + QString::fromUtf8(it.Value().ToCString()) + QLatin1Char('\n');
  const bool premier = !m_pret;
  m_pret = true;
  if (premier) {
    m_ctx->Display(m_cube, 0, 0, false);
    reconstruire();
  }
  return true;
}

void Vue3D::paintGL() {
  if (!m_pret && (!isVisible() || !initialiserOcct())) return;
  if (m_vue->Window().IsNull()) return;
  if (devicePixelRatioF() != m_vue->Window()->DevicePixelRatio()) initialiserOcct();
  if (!occtqt::initialiserTamponQt(m_vue)) return;
  occtqt::etatGlAvantOcct(m_vue);
  m_vue->InvalidateImmediate();
  FlushViewEvents(m_ctx, m_vue, true);
  occtqt::etatGlApresOcct(m_vue);
}

void Vue3D::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& vue) {
  AIS_ViewController::handleViewRedraw(ctx, vue);
  if (myToAskNextFrame) update();
}

bool Vue3D::transmettreSouris(QMouseEvent* e) {
  if (m_vue.IsNull() || m_vue->Window().IsNull()) return false;
  const double r = devicePixelRatioF();
  const NCollection_Vec2<int> pt(static_cast<int>(e->position().x() * r + 0.5), static_cast<int>(e->position().y() * r + 0.5));
  Aspect_VKeyMouse boutons = Aspect_VKeyMouse_NONE;
  if (e->buttons() & Qt::LeftButton) boutons |= Aspect_VKeyMouse_LeftButton;
  if (e->buttons() & Qt::MiddleButton) boutons |= Aspect_VKeyMouse_MiddleButton;
  if (e->buttons() & Qt::RightButton) boutons |= Aspect_VKeyMouse_RightButton;
  Aspect_VKeyFlags drapeaux = Aspect_VKeyFlags_NONE;
  if (e->modifiers() & Qt::ShiftModifier) drapeaux |= Aspect_VKeyFlags_SHIFT;
  if (e->modifiers() & Qt::ControlModifier) drapeaux |= Aspect_VKeyFlags_CTRL;
  if (e->modifiers() & Qt::AltModifier) drapeaux |= Aspect_VKeyFlags_ALT;
  if (e->type() == QEvent::MouseMove) return UpdateMousePosition(pt, boutons, drapeaux, false);
  return UpdateMouseButtons(pt, boutons, drapeaux, false);
}

void Vue3D::mousePressEvent(QMouseEvent* e) {
  setFocus();
  if (e->button() == Qt::LeftButton) {
    // Pivot : centre du boîtier affiché, comme SolidWorks qui tourne autour du modèle.
    const Derive& d = m_doc->derive();
    const double leve = m_modeCouvercle == Couvercle::Souleve ? std::max(16.0, 0.42 * std::max(d.Lo, d.Wo)) : 0.0;
    m_pivot = gp_Pnt(d.cx, d.cy, (d.ztop + leve) / 2);
    m_rotation = {true, false, e->position(), e->position()};
    return;
  }
  if (e->button() == Qt::RightButton) m_pressionDroite = e->position();
  if (transmettreSouris(e)) update();
}

void Vue3D::mouseReleaseEvent(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton && m_rotation.actif) {
    m_rotation.actif = false;
    if (!m_rotation.bouge) cliquer(e->position(), e->modifiers());  // simple clic : sélection, ou cube de vue
    return;
  }
  if (transmettreSouris(e)) update();
  if (e->button() == Qt::RightButton && (e->position() - m_pressionDroite).manhattanLength() < 4) menuContextuel(e->position());
}

void Vue3D::mouseMoveEvent(QMouseEvent* e) {
  if (m_rotation.actif && (e->buttons() & Qt::LeftButton)) {
    if (!m_rotation.bouge && (e->position() - m_rotation.depart).manhattanLength() < 3) return;
    m_rotation.bouge = true;
    tourner(e->position().x() - m_rotation.dernier.x(), e->position().y() - m_rotation.dernier.y());
    m_rotation.dernier = e->position();
    return;
  }
  if (transmettreSouris(e)) update();
}

// Rotation libre : glisser horizontalement tourne autour de l'axe vertical de l'écran, verticalement autour
// de son axe horizontal. Le haut de la caméra tourne avec elle : aucune butée, on peut passer par-dessus le modèle.
void Vue3D::tourner(double dx, double dy) {
  if (m_vue.IsNull()) return;
  const Handle(Graphic3d_Camera)& cam = m_vue->Camera();
  const gp_Dir haut = cam->Up(), visee = cam->Direction();
  const gp_Dir droite = visee.Crossed(haut);
  const double k = 0.0075;  // radians par pixel
  gp_Trsf autourHaut, autourDroite;
  autourHaut.SetRotation(gp_Ax1(m_pivot, haut), -dx * k);
  autourDroite.SetRotation(gp_Ax1(m_pivot, droite), -dy * k);
  const gp_Trsf t = autourDroite.Multiplied(autourHaut);
  cam->SetEyeAndCenter(cam->Eye().Transformed(t), cam->Center().Transformed(t));
  cam->SetUp(haut.Transformed(t));
  cam->OrthogonalizeUp();
  m_vue->Invalidate();
  update();
}

void Vue3D::cliquer(const QPointF& position, Qt::KeyboardModifiers modificateurs) {
  if (m_vue.IsNull() || m_vue->Window().IsNull()) return;
  const double r = devicePixelRatioF();
  const NCollection_Vec2<int> pt(static_cast<int>(position.x() * r + 0.5), static_cast<int>(position.y() * r + 0.5));
  Aspect_VKeyFlags drapeaux = Aspect_VKeyFlags_NONE;
  if (modificateurs & Qt::ShiftModifier) drapeaux |= Aspect_VKeyFlags_SHIFT;
  if (modificateurs & Qt::ControlModifier) drapeaux |= Aspect_VKeyFlags_CTRL;
  UpdateMouseButtons(pt, Aspect_VKeyMouse_LeftButton, drapeaux, false);
  UpdateMouseButtons(pt, Aspect_VKeyMouse_NONE, drapeaux, false);
  update();
}
void Vue3D::wheelEvent(QWheelEvent* e) {
  if (m_vue.IsNull() || m_vue->Window().IsNull()) return;
  const double r = devicePixelRatioF();
  const NCollection_Vec2<int> pt(static_cast<int>(e->position().x() * r + 0.5), static_cast<int>(e->position().y() * r + 0.5));
  if (UpdateMouseScroll(Aspect_ScrollDelta(pt, e->angleDelta().y() / 120.0))) update();
}

void Vue3D::OnSelectionChanged(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)&) {
  if (m_synchro) return;
  Cible c;
  ctx->InitSelected();
  if (ctx->MoreSelected()) {
    const Handle(AIS_InteractiveObject) obj = ctx->SelectedInteractive();
    if (obj == m_cube) return;  // un clic sur le cube change la vue, pas la sélection
    if (obj == m_carte || obj == m_corps) c = {Cible::Carte, "carte"};
    for (const auto& [id, forme] : m_composants)
      if (obj == forme) c = {Cible::Composant, id};
  }
  // Différé : la sélection change pendant le traitement des événements de la vue.
  QTimer::singleShot(0, this, [this, c] { m_doc->selectionner(c); });
}

void Vue3D::planifier() {
  if (!m_minuteur.isActive()) m_minuteur.start();
}

void Vue3D::reconstruire() {
  if (!m_pret) return;
  const Projet& p = m_doc->projet();
  const Derive& d = m_doc->derive();
  auto afficher = [this](Handle(AIS_Shape)& objet, const TopoDS_Shape& forme, const Quantity_Color& couleur) {
    if (objet.IsNull()) {
      objet = new AIS_Shape(forme);
      styliser(objet, couleur);
      m_ctx->Display(objet, AIS_Shaded, 0, false);
    } else {
      objet->SetShape(forme);
      m_ctx->Redisplay(objet, false);
    }
  };
  std::ostringstream valides, forme;
  for (const auto& k : p.carte.contours)
    for (const auto& e : k.elements) forme << e.debut.x << ',' << e.debut.y << (e.milieu ? 'a' : 'l') << ';';
  for (const auto& [mur, liste] : decoupesValides(d))
    for (const auto& o : liste) valides << lettre(mur) << o.s << ',' << o.t << ',' << o.w << ',' << o.h << ',' << o.r << ';';
  for (const auto& q : piliersValides(d)) valides << q.x << ',' << q.y << ',' << q.Rb << ',' << q.rp << ';';
  for (const auto& r : d.debords) forme << 'd' << r.x1 << ',' << r.y1 << ',' << r.x2 << ',' << r.y2 << ';';
  const std::string sc = signature(d.Lo, d.Wo, d.ro, d.Li, d.Wi, d.ri, d.zf, d.zt, d.zpb, d.Rb, d.rp, d.cx, d.cy, p.boitier.paroi, p.boitier.jeu, valides.str(), forme.str());
  std::ostringstream percages;
  for (const auto& o : percagesValides(d)) percages << o.x << ',' << o.y << ',' << o.d << ';';
  const std::string sl = signature(d.Lo, d.Wo, d.ro, d.lo.hx, d.lo.hy, d.li.hx, d.li.hy, d.zt, d.ztop, d.cx, d.cy, p.boitier.jeu, p.boitier.paroi, percages.str(), forme.str());
  // Boîtier et couvercle : calculés et maillés dans un fil à part, appliqués à leur arrivée.
  std::ostringstream trousCarte;
  for (const auto& t : d.trous) trousCarte << t.x << ',' << t.y << ',' << t.vis.trou << ';';
  const std::string sp = signature(p.carte.x0, p.carte.y0, p.carte.L, p.carte.W, p.carte.r, p.carte.t, d.zpb, d.vis.trou, trousCarte.str(), forme.str());
  // Boîtier, couvercle et carte : calculés et maillés dans un fil à part, appliqués à leur arrivée.
  if (sc != m_sigCorps || sl != m_sigCouvercle || sp != m_sigCarte) {
    if (m_enCours) {
      m_relancer = true;
    } else if (sc != m_sigCorpsDemande || sl != m_sigCouvercleDemande || sp != m_sigCarteDemande) {
      m_enCours = true;
      m_sigCorpsDemande = sc;
      m_sigCouvercleDemande = sl;
      m_sigCarteDemande = sp;
      const bool faireCorps = sc != m_sigCorps, faireCouvercle = sl != m_sigCouvercle, faireCarte = sp != m_sigCarte;
      const Handle(Prs3d_Drawer) dessin = m_ctx->DefaultDrawer();
      m_ouvrier = std::thread([this, p, d, sc, sl, sp, faireCorps, faireCouvercle, faireCarte, dessin] {
        TopoDS_Shape corps, couvercle, carte;
        QString erreur;
        try {
          if (faireCarte) { carte = construireCarte(p, d); StdPrs_ToolTriangulatedShape::Tessellate(carte, dessin); }
          if (faireCorps) { corps = construireCorps(p, d); StdPrs_ToolTriangulatedShape::Tessellate(corps, dessin); }
          if (faireCouvercle) { couvercle = construireCouvercle(p, d); StdPrs_ToolTriangulatedShape::Tessellate(couvercle, dessin); }
        } catch (const std::exception& e) {
          erreur = QString::fromUtf8(e.what());
        } catch (...) {
          erreur = QStringLiteral("Construction du boîtier impossible.");
        }
        QMetaObject::invokeMethod(this, [this, corps, couvercle, carte, sc, sl, sp, erreur] { appliquer(corps, couvercle, carte, sc, sl, sp, erreur); }, Qt::QueuedConnection);
      });
    }
  }
  placerCouvercle();
  std::map<std::string, Handle(AIS_Shape)> restants;
  for (const auto& g : d.comps) {
    const Composant* k = nullptr;
    for (const auto& q : p.composants) if (q.id == g.id) k = &q;
    if (!k) continue;
    Handle(AIS_Shape) objet;
    if (auto it = m_composants.find(g.id); it != m_composants.end()) objet = it->second;
    const std::string sg = signature(g.x, g.y, g.hx, g.hy, g.h, g.dessous, d.zpt, d.zpb, k->type, k->valeur);
    if (objet.IsNull() || m_sigComposants[g.id] != sg) {
      afficher(objet, construireComposant(d, g), couleurComposant(*k));
      objet->SetColor(couleurComposant(*k));
      m_sigComposants[g.id] = sg;
    }
    restants[g.id] = objet;
  }
  for (const auto& [id, objet] : m_composants)
    if (!restants.count(id)) { m_ctx->Remove(objet, false); m_sigComposants.erase(id); }
  m_composants = restants;
  appliquerVisibilite();
  afficherSelection();
  if (!m_cadre) {
    m_vue->FitAll(0.08, false);
    m_cadre = true;
  } else if (!m_doc->enGeste() && !m_vue->Window().IsNull()) {
    int w = 0, h = 0;
    m_vue->Window()->Size(w, h);
    const double leve = m_modeCouvercle == Couvercle::Souleve ? std::max(16.0, 0.42 * std::max(d.Lo, d.Wo)) : 0.0;
    bool deborde = false;
    for (double sx : {-1.0, 1.0})
      for (double sy : {-1.0, 1.0})
        for (double z : {0.0, d.ztop + leve}) {
          int px = 0, py = 0;
          m_vue->Convert(d.cx + sx * d.Lo / 2, d.cy + sy * d.Wo / 2, z, px, py);
          if (px < 0 || py < 0 || px > w || py > h) deborde = true;
        }
    if (deborde) m_vue->FitAll(0.08, false);
  }
  m_vue->Invalidate();
  update();
}

void Vue3D::appliquer(const TopoDS_Shape& corps, const TopoDS_Shape& couvercle, const TopoDS_Shape& carte, const std::string& sc, const std::string& sl,
                      const std::string& sp, const QString& erreur) {
  if (m_ouvrier.joinable()) m_ouvrier.join();
  m_enCours = false;
  auto afficher = [this](Handle(AIS_Shape)& objet, const TopoDS_Shape& forme, const Quantity_Color& couleur) {
    if (objet.IsNull()) {
      objet = new AIS_Shape(forme);
      styliser(objet, couleur);
      m_ctx->Display(objet, AIS_Shaded, 0, false);
    } else {
      objet->SetShape(forme);
      m_ctx->Redisplay(objet, false);
    }
  };
  if (erreur.isEmpty()) {
    if (!carte.IsNull()) {
      afficher(m_carte, carte, rgb(0x2E, 0x6A, 0x50));
      m_sigCarte = sp;
    }
    if (!corps.IsNull()) {
      afficher(m_corps, corps, rgb(0xD3, 0xD8, 0xD4));
      m_sigCorps = sc;
    }
    if (!couvercle.IsNull()) {
      afficher(m_couvercle, couvercle, rgb(0xD9, 0xDD, 0xD9));
      m_ctx->Deactivate(m_couvercle);
      m_sigCouvercle = sl;
    }
    placerCouvercle();
    appliquerVisibilite();
    afficherSelection();
    if (!m_cadreCorps && !m_corps.IsNull()) {
      m_vue->FitAll(0.08, false);
      m_cadreCorps = true;
    }
  } else {
    // Échec : on garde l'affichage précédent ; la prochaine modification retentera.
    m_sigCorpsDemande.clear();
    m_sigCouvercleDemande.clear();
    m_sigCarteDemande.clear();
  }
  m_vue->Invalidate();
  update();
  if (m_relancer) {
    m_relancer = false;
    planifier();
  }
}

void Vue3D::setModeBoitier(Boitier mode) {
  m_modeBoitier = mode;
  placerCouvercle();
  appliquerVisibilite();
  emit modeBoitierChange(static_cast<int>(mode));
}

// Applique masquages et transparences : boîtier (visible, transparent, masqué), carte et composants un par un.
void Vue3D::appliquerVisibilite() {
  if (!m_pret) return;
  auto montrer = [this](const Handle(AIS_Shape)& o, bool visible, double transparence, bool selectionnable) {
    if (o.IsNull()) return;
    if (!visible) { if (m_ctx->IsDisplayed(o)) m_ctx->Erase(o, false); return; }
    if (!m_ctx->IsDisplayed(o)) m_ctx->Display(o, AIS_Shaded, 0, false);
    if (std::abs(o->Transparency() - transparence) > 1e-3) m_ctx->SetTransparency(o, transparence, false);
    if (selectionnable) m_ctx->Activate(o, 0);
    else m_ctx->Deactivate(o);
  };
  montrer(m_corps, m_modeBoitier != Boitier::Masque, m_modeBoitier == Boitier::Transparent ? 0.7 : 0.0, m_modeBoitier == Boitier::Visible);
  montrer(m_carte, !m_carteMasquee, m_carteTransparente ? 0.65 : 0.0, true);
  for (const auto& [id, o] : m_composants) montrer(o, !m_masques.count(id), m_transparents.count(id) ? 0.65 : 0.0, true);
  m_vue->Invalidate();
  update();
}

// Suppr dans la vue 3D : supprime le composant ou le trou sélectionné, comme dans la vue Carte.
void Vue3D::keyPressEvent(QKeyEvent* e) {
  const Cible s = m_doc->selection();
  if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && (s.type == Cible::Composant || s.type == Cible::Trou)) {
    m_doc->modifier([&](Projet& p) {
      if (s.type == Cible::Composant)
        p.composants.erase(std::remove_if(p.composants.begin(), p.composants.end(), [&](const Composant& k) { return k.id == s.id; }), p.composants.end());
      else
        p.trous.erase(std::remove_if(p.trous.begin(), p.trous.end(), [&](const Trou& t) { return t.id == s.id; }), p.trous.end());
    });
    return;
  }
  QOpenGLWidget::keyPressEvent(e);
}

void Vue3D::masquerSelection() {
  const Cible s = m_doc->selection();
  if (s.type == Cible::Composant) m_masques.insert(s.id);
  else if (s.type == Cible::Carte) m_carteMasquee = true;
  else return;
  m_doc->selectionner({});
  appliquerVisibilite();
}

void Vue3D::toutAfficher() {
  m_masques.clear();
  m_transparents.clear();
  m_carteMasquee = m_carteTransparente = false;
  m_modeBoitier = Boitier::Visible;
  placerCouvercle();
  appliquerVisibilite();
}

// Clic droit sans glisser : actions sur la pièce visée (masquer, transparence, supprimer).
void Vue3D::menuContextuel(const QPointF& position) {
  const double r = devicePixelRatioF();
  m_ctx->MoveTo(static_cast<int>(position.x() * r + 0.5), static_cast<int>(position.y() * r + 0.5), m_vue, true);
  Handle(AIS_InteractiveObject) vise = m_ctx->HasDetected() ? m_ctx->DetectedInteractive() : Handle(AIS_InteractiveObject)();
  std::string idComposant;
  for (const auto& [id, o] : m_composants) if (o == vise) idComposant = id;
  QMenu menu(this);
  if (!idComposant.empty()) {
    std::string ref;
    for (const auto& k : m_doc->projet().composants) if (k.id == idComposant) ref = k.ref;
    m_doc->selectionner({Cible::Composant, idComposant});
    menu.addSection(QString::fromStdString(ref));
    menu.addAction(QStringLiteral("Masquer"), this, [this, idComposant] { m_masques.insert(idComposant); m_doc->selectionner({}); appliquerVisibilite(); });
    const bool transp = m_transparents.count(idComposant) > 0;
    menu.addAction(transp ? QStringLiteral("Rendre opaque") : QStringLiteral("Rendre transparent"), this, [this, idComposant, transp] {
      if (transp) m_transparents.erase(idComposant); else m_transparents.insert(idComposant);
      appliquerVisibilite();
    });
    menu.addAction(QStringLiteral("Supprimer le composant"), this, [this, idComposant] {
      m_doc->modifier([&](Projet& p) { p.composants.erase(std::remove_if(p.composants.begin(), p.composants.end(), [&](const Composant& k) { return k.id == idComposant; }), p.composants.end()); });
    });
  } else if (!vise.IsNull() && vise == m_carte) {
    menu.addSection(QStringLiteral("Carte"));
    menu.addAction(QStringLiteral("Masquer la carte"), this, [this] { m_carteMasquee = true; appliquerVisibilite(); });
    menu.addAction(m_carteTransparente ? QStringLiteral("Carte opaque") : QStringLiteral("Carte transparente"), this, [this] { m_carteTransparente = !m_carteTransparente; appliquerVisibilite(); });
  } else if (!vise.IsNull() && vise == m_corps) {
    menu.addSection(QStringLiteral("Boîtier"));
    menu.addAction(QStringLiteral("Rendre transparent"), this, [this] { setModeBoitier(Boitier::Transparent); });
    menu.addAction(QStringLiteral("Masquer le boîtier"), this, [this] { setModeBoitier(Boitier::Masque); });
  }
  if (!menu.isEmpty()) menu.addSeparator();
  menu.addAction(QStringLiteral("Tout afficher"), this, [this] { toutAfficher(); });
  menu.addAction(QStringLiteral("Recadrer"), this, [this] { recadrer(); });
  menu.exec(mapToGlobal(position.toPoint()));
}

void Vue3D::placerCouvercle() {
  if (m_couvercle.IsNull()) return;
  if (m_modeCouvercle == Couvercle::Masque || m_modeBoitier == Boitier::Masque) {
    m_ctx->Erase(m_couvercle, false);
    return;
  }
  const Derive& d = m_doc->derive();
  gp_Trsf t;
  t.SetTranslation(gp_Vec(0, 0, m_modeCouvercle == Couvercle::Souleve ? std::max(16.0, 0.42 * std::max(d.Lo, d.Wo)) : 0.0));
  m_ctx->SetLocation(m_couvercle, TopLoc_Location(t));
  if (!m_ctx->IsDisplayed(m_couvercle)) {
    m_ctx->Display(m_couvercle, AIS_Shaded, 0, false);
    m_ctx->Deactivate(m_couvercle);
  }
  m_ctx->SetTransparency(m_couvercle, m_modeCouvercle == Couvercle::Souleve ? 0.55 : 0.0, false);
}

void Vue3D::afficherSelection() {
  if (!m_pret) return;
  m_synchro = true;
  m_ctx->ClearSelected(false);
  const Cible& s = m_doc->selection();
  Handle(AIS_Shape) objet;
  if (s.type == Cible::Composant) {
    if (auto it = m_composants.find(s.id); it != m_composants.end()) objet = it->second;
  } else if (s.type == Cible::Carte) {
    objet = m_carte;
  }
  if (!objet.IsNull() && m_ctx->IsDisplayed(objet)) m_ctx->AddOrRemoveSelected(objet, false);
  m_synchro = false;
  m_vue->Invalidate();
  update();
}

void Vue3D::setCouvercle(Couvercle c) {
  m_modeCouvercle = c;
  placerCouvercle();
  m_vue->Invalidate();
  update();
}

void Vue3D::vueStandard(V3d_TypeOfOrientation orientation) {
  if (m_vue.IsNull()) return;
  Handle(Graphic3d_Camera) depart = new Graphic3d_Camera();
  depart->Copy(m_vue->Camera());
  m_vue->SetProj(orientation);
  m_vue->FitAll(0.08, false);
  Handle(Graphic3d_Camera) arrivee = new Graphic3d_Camera();
  arrivee->Copy(m_vue->Camera());
  m_vue->Camera()->Copy(depart);
  myViewAnimation->SetView(m_vue);
  myViewAnimation->SetCameraStart(depart);
  myViewAnimation->SetCameraEnd(arrivee);
  myViewAnimation->StartTimer(0.0, 1.0, true, true);  // le minuteur s'arrête à la fin : sinon l'animation reprend la main sur la caméra à chaque image
  m_vue->Invalidate();
  update();
}

void Vue3D::recadrer() {
  if (m_vue.IsNull()) return;
  m_vue->FitAll(0.08, false);
  update();
}

// Fond de la vue aux couleurs du thème (dégradé vertical)
void Vue3D::appliquerTheme() {
  const CouleursInterface& c = theme().c();
  const auto qc = [](const QColor& x) { return Quantity_Color(x.redF(), x.greenF(), x.blueF(), Quantity_TOC_sRGB); };
  m_vue->SetBgGradientColors(qc(c.vue3dHaut), qc(c.vue3dBas), Aspect_GFM_VER, false);
  m_vue->Invalidate();
  update();
}
