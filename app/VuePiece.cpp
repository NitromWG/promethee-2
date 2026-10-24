// Prométhée : vue 3D d'une pièce paramétrique. Licence GPL-3.0-only.
#include "VuePiece.hpp"

#include <QFontInfo>

#include <QMouseEvent>
#include <cstdio>
#include <QTimer>
#include <QWheelEvent>
#include <QVariantAnimation>

#include <AIS_AnimationCamera.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <AIS_TextLabel.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <TCollection_ExtendedString.hxx>
#include <AIS_ColoredShape.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <SelectMgr_Selection.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <Bnd_Box.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_NameOfMaterial.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Standard_Failure.hxx>
#include <gp_Pln.hxx>
#include <V3d_Trihedron.hxx>
#include <Prs3d_TextAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>

#include "OcctGlTools.hpp"
#include "Theme.hpp"

namespace {
Quantity_Color rgb(int r, int g, int b) { return Quantity_Color(r / 255.0, g / 255.0, b / 255.0, Quantity_TOC_sRGB); }
}  // namespace

VuePiece::VuePiece(QWidget* parent) : QOpenGLWidget(parent) {
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
  m_visu->SetDefaultLights();
  m_visu->SetLightOn();
  m_ctx = new AIS_InteractiveContext(m_visu);
  m_ctx->DefaultDrawer()->SetDeviationCoefficient(0.0003);
  m_ctx->DefaultDrawer()->SetDeviationAngle(3.0 * 3.14159265358979 / 180.0);
  m_ctx->SelectionStyle()->SetColor(rgb(0x1E, 0x7B, 0xE0));
  m_ctx->HighlightStyle()->SetColor(rgb(0x7F, 0xB8, 0xF5));
  m_vue = m_visu->CreateView();
  m_vue->SetImmediateUpdate(false);
#ifndef __APPLE__
  m_vue->ChangeRenderingParams().NbMsaaSamples = 4;
#endif
  appliquerTheme();
  connect(&Theme::instance(), &Theme::change, this, &VuePiece::appliquerTheme);
  m_vue->SetProj(V3d_XnegYnegZpos);
  auto& gestes = ChangeMouseGestureMap();
  gestes.Clear();
  // Même convention que dans l'esquisse (AP-39) : bouton du milieu glissé = rotation, clic droit glissé = déplacement ;
  // le clic gauche glissé fait aussi tourner la vue 3D, où il ne trace rien
  gestes.Bind(Aspect_VKeyMouse_RightButton, AIS_MouseGesture_Pan);
  gestes.Bind(Aspect_VKeyMouse_MiddleButton, AIS_MouseGesture_RotateOrbit);
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
  m_cube->TransformPersistence()->SetOffset2d(NCollection_Vec2<int>(70, 140));  // sous le coin de validation
  // Les axes sont donnés par le trièdre coloré en bas à gauche : le cube garde ses faces nommées, sans lettres jaunes.
  m_cube->SetDrawAxes(false);
  myViewAnimation->SetOwnDuration(0.35);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
}

VuePiece::~VuePiece() {
  Handle(Aspect_DisplayConnection) affichage = m_visu->Driver()->GetDisplayConnection();
  m_ctx->RemoveAll(false);
  m_ctx.Nullify();
  m_vue->Remove();
  m_vue.Nullify();
  m_visu.Nullify();
  makeCurrent();
  affichage.Nullify();
}

// Qt peut appeler initializeGL alors que la vue est encore cachée (onglet non affiché, capture d'écran) :
// OpenCascade a besoin d'une vraie fenêtre, on attend donc le premier affichage pour s'y attacher.
void VuePiece::initializeGL() {
  if (isVisible()) initialiserOcct();
}

bool VuePiece::initialiserOcct() {
  Handle(OpenGl_GraphicDriver) pilote = Handle(OpenGl_GraphicDriver)::DownCast(m_visu->Driver());
  pilote->ChangeOptions().contextCompatible = format().profile() != QSurfaceFormat::CoreProfile;
  const NCollection_Vec2<int> taille(rect().right() - rect().left(), rect().bottom() - rect().top());
  if (!occtqt::initialiserFenetre(m_vue, (Aspect_Drawable)effectiveWinId(), taille, devicePixelRatioF())) return false;
  makeCurrent();
  const bool premier = !m_pret;
  m_pret = true;
  if (premier) {
    m_ctx->Display(m_cube, 0, 0, false);
    // Trièdre rouge, vert, bleu en bas à gauche, posé une fois la vue attachée à sa fenêtre.
    try {
      m_vue->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_BLACK, 0.16, V3d_ZBUFFER);
      m_vue->Trihedron()->SetArrowsColor(rgb(0xD0, 0x2A, 0x2A), rgb(0x1E, 0x9A, 0x3C), rgb(0x1E, 0x5B, 0xD8));
      m_vue->Trihedron()->SetLabelsColor(rgb(0xD0, 0x2A, 0x2A), rgb(0x1E, 0x9A, 0x3C), rgb(0x1E, 0x5B, 0xD8));
      m_vue->Trihedron()->SetSizeRatio(0.9);
    } catch (const Standard_Failure&) {
      // Sans trièdre, la vue reste utilisable.
    }
    if (m_aAfficher) appliquer();
  }
  return true;
}

void VuePiece::paintGL() {
  if (!m_pret && (!isVisible() || !initialiserOcct())) return;
  if (m_vue->Window().IsNull()) return;
  if (devicePixelRatioF() != m_vue->Window()->DevicePixelRatio()) initialiserOcct();
  int largeur = 0, hauteur = 0, l = 0, h = 0;
  m_vue->Window()->Size(largeur, hauteur);
  if (!occtqt::initialiserTamponQt(m_vue)) return;
  m_vue->Window()->Size(l, h);
  if (m_recadrer && isVisible()) {
    m_recadrer = false;
    m_vue->FitAll(0.15, false);
    m_vue->Invalidate();
  }
  // Orientation demandée, ou vue qui a changé de taille pendant le cadrage automatique : cadrage pour la taille
  // actuelle, animé depuis la caméra du moment
  if (isVisible() && (m_orientationEnAttente || (m_cadrageAuto && (l != largeur || h != hauteur)))) {
    m_orientationEnAttente = false;
    animerVers(cameraCadrant(m_repereCadrage, m_boiteCadrage));
  }
  occtqt::etatGlAvantOcct(m_vue);
  m_vue->InvalidateImmediate();
  FlushViewEvents(m_ctx, m_vue, true);
  occtqt::etatGlApresOcct(m_vue);
  emit cameraChangee();
}

void VuePiece::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& vue) {
  AIS_ViewController::handleViewRedraw(ctx, vue);
  if (myToAskNextFrame) update();
}

void VuePiece::afficher(const prom::ResultatPiece& r, bool recadrer) {
  // Recadrage demandé, ou premier solide affiché : fait au prochain dessin, une fois la taille de la vue connue.
  if (recadrer || (m_solide.IsNull() && !r.forme.IsNull())) m_recadrer = true;
  m_attente = r;
  m_aAfficher = true;
  if (m_pret) appliquer();
}

void VuePiece::appliquer() {
  m_aAfficher = false;
  if (!m_solide.IsNull()) { m_ctx->Remove(m_solide, false); m_solide.Nullify(); }
  for (const auto& e : m_esquisses) m_ctx->Remove(e.second, false);
  m_esquisses.clear();
  Bnd_Box boite;
  if (!m_attente.forme.IsNull()) {
    if (m_couleursSousFormes.empty()) m_solide = new AIS_Shape(m_attente.forme);
    else {
      Handle(AIS_ColoredShape) cs = new AIS_ColoredShape(m_attente.forme);
      for (const auto& [sous, couleur] : m_couleursSousFormes)
        if (!sous.IsNull()) cs->SetCustomColor(sous, rgb(couleur.red(), couleur.green(), couleur.blue()));
      m_solide = cs;
    }
    m_solide->SetColor(rgb(m_couleurSolide.red(), m_couleurSolide.green(), m_couleurSolide.blue()));
    m_solide->SetMaterial(Graphic3d_NOM_PLASTIC);
    m_solide->Attributes()->SetFaceBoundaryDraw(m_affichage.aretes);
    m_solide->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(rgb(0x3E, 0x48, 0x50), Aspect_TOL_SOLID, 1.2));
    m_ctx->Display(m_solide, AIS_Shaded, 0, false);  // chargé dans le sélecteur ; les modes sont ajustés ensuite
    if (m_translucide) m_ctx->SetTransparency(m_solide, 0.55, false);
    else if (m_transparenceSolide > 0) m_ctx->SetTransparency(m_solide, m_transparenceSolide, false);
    BRepBndLib::Add(m_attente.forme, boite);
  }
  for (const auto& [id, trace] : m_attente.esquisses) {
    if (trace.IsNull() || !m_affichage.esquisses || m_masquees.count(id)) continue;
    Handle(AIS_Shape) a = new AIS_Shape(trace);
    // Vert : profils fermés, prêts pour une extrusion ; orange : profil ouvert ; rouge : problème.
    const int etat = m_attente.etatEsquisse(id);
    a->SetColor(etat == 0 ? rgb(0x2E, 0x8B, 0x57) : etat == 1 ? rgb(0xE0, 0x8A, 0x1E) : rgb(0xC2, 0x40, 0x2E));
    a->SetWidth(2.5);
    m_ctx->Display(a, AIS_WireFrame, 0, false);
    m_esquisses.push_back({id, a});
    BRepBndLib::Add(trace, boite);
  }
  // Géométrie de référence : plans (carrés violets translucides), axes (trait mixte), points (petites sphères).
  for (const auto& [id, a] : m_references) m_ctx->Remove(a, false);
  m_references.clear();
  {
    double t = 30;
    if (!boite.IsVoid()) t = std::max(15.0, std::sqrt(boite.SquareExtent()) * 0.35);
    const Quantity_Color violet = rgb(0x7B, 0x5E, 0xA7);
    for (const auto& [id, ax] : m_attente.plansReference) {
      const gp_Pnt o = ax.Location();
      const gp_Vec x = gp_Vec(ax.XDirection()) * t, y = gp_Vec(ax.YDirection()) * t;
      BRepBuilderAPI_MakePolygon poly(o.Translated(-x - y), o.Translated(x - y), o.Translated(x + y), o.Translated(-x + y), true);
      Handle(AIS_Shape) s = new AIS_Shape(BRepBuilderAPI_MakeFace(poly.Wire(), true).Face());
      s->SetColor(violet);
      s->SetTransparency(0.8);
      s->Attributes()->SetFaceBoundaryDraw(true);
      s->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(violet, Aspect_TOL_SOLID, 1.4));
      m_ctx->Display(s, AIS_Shaded, 0, false);
      m_references.push_back({id, s});
    }
    for (const auto& [id, axe] : m_attente.axesReference) {
      const gp_Pnt o = axe.Location();
      Handle(AIS_Shape) s = new AIS_Shape(BRepBuilderAPI_MakeEdge(o.Translated(gp_Vec(axe.Direction()) * -t), o.Translated(gp_Vec(axe.Direction()) * t)).Edge());
      s->SetColor(violet);
      s->Attributes()->SetWireAspect(new Prs3d_LineAspect(violet, Aspect_TOL_DOTDASH, 2));
      m_ctx->Display(s, AIS_WireFrame, 0, false);
      m_references.push_back({id, s});
    }
    // Filetages (M4) : hélice cuivrée au pas du filet, non cliquable
    if (const TopoDS_Shape filets = traceFiletages(m_attente); !filets.IsNull()) {
      Handle(AIS_Shape) s = new AIS_Shape(filets);
      const Quantity_Color cuivre = rgb(0xB8, 0x6B, 0x2E);
      s->SetColor(cuivre);
      s->Attributes()->SetWireAspect(new Prs3d_LineAspect(cuivre, Aspect_TOL_SOLID, 1.2));
      m_ctx->Display(s, AIS_WireFrame, -1, false);
      m_references.push_back({-1, s});
    }
    for (const auto& [id, q] : m_attente.pointsReference) {
      Handle(AIS_Shape) s = new AIS_Shape(BRepPrimAPI_MakeSphere(q, std::max(0.4, t * 0.02)).Shape());
      s->SetColor(violet);
      m_ctx->Display(s, AIS_Shaded, 0, false);
      m_references.push_back({id, s});
    }
  }
  // Géométrie de construction : trait d'axe gris, fin, nettement distinct du profil.
  for (const auto& [id, trace] : m_attente.esquissesConstruction) {
    if (trace.IsNull() || !m_affichage.esquisses || !m_affichage.construction || m_masquees.count(id)) continue;
    Handle(AIS_Shape) a = new AIS_Shape(trace);
    a->Attributes()->SetWireAspect(new Prs3d_LineAspect(rgb(0x8A, 0x94, 0x9A), Aspect_TOL_DOTDASH, 1.2));
    a->Attributes()->SetLineAspect(new Prs3d_LineAspect(rgb(0x8A, 0x94, 0x9A), Aspect_TOL_DOTDASH, 1.2));
    a->Attributes()->SetFreeBoundaryAspect(new Prs3d_LineAspect(rgb(0x8A, 0x94, 0x9A), Aspect_TOL_DOTDASH, 1.2));
    a->Attributes()->SetUnFreeBoundaryAspect(new Prs3d_LineAspect(rgb(0x8A, 0x94, 0x9A), Aspect_TOL_DOTDASH, 1.2));
    m_ctx->Display(a, AIS_WireFrame, 0, false);
    m_esquisses.push_back({id, a});
  }
  majPlans(boite);
  if (!boite.IsVoid()) {
    double x0, y0, z0, x1, y1, z1;
    boite.Get(x0, y0, z0, x1, y1, z1);
    m_pivot = gp_Pnt((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    if (!m_cadre) { m_recadrer = true; m_cadre = true; }
  }
  activerSelection();
  ResetPreviousMoveTo();
  m_vue->Invalidate();
  update();
}

// Sélection à la manière des grands logiciels : sommets, arêtes, faces, esquisses et plans se cliquent
// directement ; le filtre limite à un seul type quand une fonction l'exige.
void VuePiece::activerSelection() {
  const bool tout = m_mode == Mode::Tout;
  for (const auto& [id, a] : m_esquisses) {
    (void)id;
    m_ctx->Deactivate(a);
    if (tout) m_ctx->Activate(a, 0);
  }
  for (const auto& [nom, a] : m_plans) {
    (void)nom;
    m_ctx->Deactivate(a);
    if (tout || m_mode == Mode::Faces) m_ctx->Activate(a, 0);
  }
  for (const auto& [nom, a] : m_axesBase) {
    m_ctx->Deactivate(a);
    if (tout || (nom == "O" ? m_mode == Mode::Sommets : m_mode == Mode::Aretes)) m_ctx->Activate(a, 0);
  }
  for (const auto& [cle, a] : m_regions) {
    (void)cle;
    m_ctx->Deactivate(a);
    if (m_mode == Mode::Regions) m_ctx->Activate(a, 0);
  }
  if (m_solide.IsNull()) return;
  m_ctx->Deactivate(m_solide);
  if (tout || m_mode == Mode::Sommets) m_ctx->Activate(m_solide, AIS_Shape::SelectionMode(TopAbs_VERTEX));
  if (tout || m_mode == Mode::Aretes) m_ctx->Activate(m_solide, AIS_Shape::SelectionMode(TopAbs_EDGE));
  if (tout || m_mode == Mode::Faces) m_ctx->Activate(m_solide, AIS_Shape::SelectionMode(TopAbs_FACE));
}

// Plans de base : un carré par plan, disposés en coin de pièce vu de l'intérieur depuis la vue par défaut
// (le sol XY devant, les murs XZ et YZ derrière) : on distingue les trois plans sous tous les angles, au lieu
// de trois plans croisés qui se confondent en étoile, et ils ne recouvrent pas une pièce dessinée du côté
// positif des axes. Leur nom est affiché ; survol et sélection les font nettement ressortir.
void VuePiece::majPlans(const Bnd_Box& boite) {
  for (const auto& [nom, a] : m_plans) m_ctx->Remove(a, false);
  m_plans.clear();
  for (const auto& [nom, a] : m_axesBase) m_ctx->Remove(a, false);
  m_axesBase.clear();
  for (const auto& a : m_etiquettesPlans) m_ctx->Remove(a, false);
  m_etiquettesPlans.clear();
  if (!m_plansVisibles) return;
  double t = 40;
  if (!boite.IsVoid()) t = std::max(25.0, std::sqrt(boite.SquareExtent()) * 0.55);
  struct DefPlan { const char* nom; const char* titre; gp_Pnt a, b, c, d; gp_Pnt etiquette; int r, g, b2; };
  const DefPlan plans[] = {
      {"XY", "Dessus (XY)", gp_Pnt(0, 0, 0), gp_Pnt(-t, 0, 0), gp_Pnt(-t, -t, 0), gp_Pnt(0, -t, 0), gp_Pnt(-t * 0.97, -t * 0.97, 0), 0x6F, 0x9F, 0xE8},
      {"XZ", "Face (XZ)", gp_Pnt(0, 0, 0), gp_Pnt(-t, 0, 0), gp_Pnt(-t, 0, t), gp_Pnt(0, 0, t), gp_Pnt(-t * 0.97, 0, t * 0.97), 0x7C, 0xC4, 0x8E},
      {"YZ", "Droite (YZ)", gp_Pnt(0, 0, 0), gp_Pnt(0, -t, 0), gp_Pnt(0, -t, t), gp_Pnt(0, 0, t), gp_Pnt(0, -t * 0.97, t * 0.97), 0xE8, 0x8F, 0x86}};
  for (const auto& dp : plans) {
    BRepBuilderAPI_MakePolygon poly(dp.a, dp.b, dp.c, dp.d, true);
    Handle(AIS_Shape) a = new AIS_Shape(BRepBuilderAPI_MakeFace(poly.Wire(), true).Face());
    const bool evidence = m_planEnEvidence == dp.nom;
    a->SetColor(evidence ? rgb(0x1E, 0x7B, 0xE0) : rgb(dp.r, dp.g, dp.b2));
    a->SetTransparency(evidence ? 0.55 : 0.82);
    a->Attributes()->SetFaceBoundaryDraw(true);
    a->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(rgb(0x2F, 0x6D, 0xB5), Aspect_TOL_SOLID, evidence ? 2.5 : 1.4));
    // Survol et sélection bien visibles (plus opaques que le plan au repos).
    Handle(Prs3d_Drawer) survol = new Prs3d_Drawer();
    survol->SetLink(m_ctx->HighlightStyle());
    survol->SetColor(rgb(0x5F, 0xA2, 0xF0));
    survol->SetTransparency(0.45f);
    survol->SetDisplayMode(AIS_Shaded);
    a->SetDynamicHilightAttributes(survol);
    Handle(Prs3d_Drawer) choisi = new Prs3d_Drawer();
    choisi->SetLink(m_ctx->SelectionStyle());
    choisi->SetColor(rgb(0x1E, 0x7B, 0xE0));
    choisi->SetTransparency(0.35f);
    choisi->SetDisplayMode(AIS_Shaded);
    a->SetHilightAttributes(choisi);
    m_ctx->Display(a, AIS_Shaded, 0, false);
    m_plans.push_back({dp.nom, a});
    Handle(AIS_TextLabel) l = new AIS_TextLabel();
    l->SetText(TCollection_ExtendedString(dp.titre, true));
    l->SetPosition(dp.etiquette);
    l->SetColor(rgb(0x2F, 0x6D, 0xB5));
    // Police de l'interface, sans empattements, à la taille du texte de l'interface sur cet écran (AP-42)
    const QByteArray famille = QFontInfo(font()).family().toUtf8();
    l->SetFont(famille.constData());
    l->SetHeight(15 * devicePixelRatioF());
    m_ctx->Display(l, 0, -1, false);
    m_etiquettesPlans.push_back(l);
  }
  // Axes de base (rouge X, vert Y, bleu Z) et origine, cliquables comme les plans (M7)
  struct DefAxe { const char* nom; const char* titre; gp_Dir d; int r, g, b; };
  const DefAxe axes[] = {{"X", "X", gp_Dir(1, 0, 0), 0xD0, 0x3B, 0x2E}, {"Y", "Y", gp_Dir(0, 1, 0), 0x2E, 0x9E, 0x4F}, {"Z", "Z", gp_Dir(0, 0, 1), 0x2F, 0x6B, 0xF5}};
  const double la = t * 0.8;
  for (const auto& da : axes) {
    Handle(AIS_Shape) a = new AIS_Shape(BRepBuilderAPI_MakeEdge(gp::Origin(), gp::Origin().Translated(gp_Vec(da.d) * la)).Edge());
    const Quantity_Color c = rgb(da.r, da.g, da.b);
    a->SetColor(c);
    a->Attributes()->SetWireAspect(new Prs3d_LineAspect(c, Aspect_TOL_DASH, 2.2));
    a->SetZLayer(Graphic3d_ZLayerId_Topmost);  // devant les plans translucides, et choisi avant eux au clic
    m_ctx->Display(a, AIS_WireFrame, 0, false);
    m_axesBase.push_back({da.nom, a});
    Handle(AIS_TextLabel) l = new AIS_TextLabel();
    l->SetText(TCollection_ExtendedString(da.titre, true));
    l->SetPosition(gp::Origin().Translated(gp_Vec(da.d) * (la * 1.05)));
    l->SetColor(c);
    const QByteArray famille = QFontInfo(font()).family().toUtf8();
    l->SetFont(famille.constData());
    l->SetHeight(14 * devicePixelRatioF());
    m_ctx->Display(l, 0, -1, false);
    m_etiquettesPlans.push_back(l);
  }
  Handle(AIS_Shape) o = new AIS_Shape(BRepPrimAPI_MakeSphere(gp::Origin(), std::max(0.6, t * 0.025)).Shape());
  o->SetColor(rgb(0xE0, 0x7B, 0x1E));
  o->SetZLayer(Graphic3d_ZLayerId_Topmost);
  m_ctx->Display(o, AIS_Shaded, 0, false);
  m_axesBase.push_back({"O", o});
}

std::string VuePiece::axeSelectionne() const {
  for (m_ctx->InitSelected(); m_ctx->MoreSelected(); m_ctx->NextSelected()) {
    const Handle(AIS_InteractiveObject) o = m_ctx->SelectedInteractive();
    for (const auto& [nom, a] : m_axesBase) if (o == a) return nom;
  }
  return {};
}

void VuePiece::mettreEnEvidencePlan(const std::string& nom) {
  if (nom == m_planEnEvidence) return;
  m_planEnEvidence = nom;
  appliquer();
}

void VuePiece::sauverCamera() {
  m_cameraSauvee = new Graphic3d_Camera();
  m_cameraSauvee->Copy(m_vue->Camera());
}

void VuePiece::restaurerCamera() {
  finCadrageAuto();
  if (m_cameraSauvee.IsNull()) return;
  const Handle(Graphic3d_Camera) fin = m_cameraSauvee;
  m_cameraSauvee.Nullify();
  animerVers(fin);
}

// Transition de caméra pilotée par Qt : l'utilisateur garde la main dès la fin (zoom, rotation, déplacement).
void VuePiece::animerVers(const Handle(Graphic3d_Camera)& fin) {
  if (m_animation) { m_animation->stop(); m_animation->deleteLater(); m_animation = nullptr; }
  Handle(Graphic3d_Camera) depart = new Graphic3d_Camera();
  depart->Copy(m_vue->Camera());
  Handle(Graphic3d_Camera) arrivee = new Graphic3d_Camera();
  arrivee->Copy(fin);
  auto* anim = new QVariantAnimation(this);
  m_animation = anim;
  anim->setStartValue(0.0);
  anim->setEndValue(1.0);
  anim->setDuration(300);
  anim->setEasingCurve(QEasingCurve::OutCubic);
  connect(anim, &QVariantAnimation::valueChanged, this, [this, depart, arrivee](const QVariant& t) {
    Handle(Graphic3d_Camera) c = new Graphic3d_Camera();
    NCollection_Lerp<Handle(Graphic3d_Camera)> lerp(depart, arrivee);
    lerp.Interpolate(t.toDouble(), c);
    m_vue->Camera()->Copy(c);
    m_vue->Invalidate();
    update();
  });
  connect(anim, &QVariantAnimation::finished, this, [this, arrivee, anim] {
    m_vue->Camera()->Copy(arrivee);
    m_vue->Invalidate();
    update();
    if (m_animation == anim) m_animation = nullptr;
    anim->deleteLater();
  });
  anim->start();
}

void VuePiece::setPlansVisibles(bool oui) {
  m_plansVisibles = oui;
  appliquer();
}

std::string VuePiece::planSelectionne() const {
  for (m_ctx->InitSelected(); m_ctx->MoreSelected(); m_ctx->NextSelected()) {
    const Handle(AIS_InteractiveObject) o = m_ctx->SelectedInteractive();
    for (const auto& [nom, a] : m_plans) if (o == a) return nom;
  }
  return {};
}

void VuePiece::afficherRegions(const std::vector<prom::RegionEsquisse>& regions, const std::vector<std::string>& retenues) {
  for (const auto& [cle, a] : m_regions) m_ctx->Remove(a, false);
  m_regions.clear();
  for (const auto& rg : regions) {
    const bool choisie = retenues.empty() ? rg.parDefaut : std::find(retenues.begin(), retenues.end(), rg.cle) != retenues.end();
    // Légèrement décalée vers la caméra pour rester cliquable par-dessus la pièce.
    Handle(AIS_Shape) a = new AIS_Shape(rg.face);
    a->SetColor(choisie ? rgb(0x1E, 0x7B, 0xE0) : rgb(0xC4, 0xCC, 0xD4));
    a->SetTransparency(choisie ? 0.35 : 0.6);
    a->Attributes()->SetFaceBoundaryDraw(true);
    a->Attributes()->SetFaceBoundaryAspect(new Prs3d_LineAspect(rgb(0x1E, 0x5B, 0xB0), Aspect_TOL_SOLID, 1.5));
    a->SetZLayer(Graphic3d_ZLayerId_Topmost);
    Handle(Prs3d_Drawer) survol = new Prs3d_Drawer();
    survol->SetLink(m_ctx->HighlightStyle());
    survol->SetColor(rgb(0xE0, 0x7B, 0x1E));
    survol->SetTransparency(0.3f);
    survol->SetDisplayMode(AIS_Shaded);
    survol->SetZLayer(Graphic3d_ZLayerId_Topmost);
    a->SetDynamicHilightAttributes(survol);
    m_ctx->Display(a, AIS_Shaded, 0, false);
    m_regions.push_back({rg.cle, a});
  }
  activerSelection();
  m_vue->Invalidate();
  update();
}

std::string VuePiece::regionSelectionnee() const {
  for (m_ctx->InitSelected(); m_ctx->MoreSelected(); m_ctx->NextSelected()) {
    const Handle(AIS_InteractiveObject) o = m_ctx->SelectedInteractive();
    for (const auto& [cle, a] : m_regions) if (o == a) return cle;
  }
  return {};
}

void VuePiece::setSolideTranslucide(bool oui) {
  m_translucide = oui;
  if (!m_solide.IsNull()) m_ctx->SetTransparency(m_solide, oui ? 0.55 : 0.0, false);
  m_vue->Invalidate();
  update();
}

QPointF VuePiece::projeter(const gp_Pnt& p) const {
  // Projection fine (sans arrondi au pixel) par la caméra.
  const gp_Pnt proj = m_vue->Camera()->Project(p);
  int w = 0, h = 0;
  m_vue->Window()->Size(w, h);
  const double xr = (proj.X() + 1) * 0.5 * w;
  const double yr = (1 - proj.Y()) * 0.5 * h;
  const double r = devicePixelRatioF();
  return {xr / r, yr / r};
}

bool VuePiece::rayon(const QPointF& ecran, gp_Pnt& origine, gp_Dir& direction) const {
  if (m_vue->Window().IsNull()) return false;
  const double r = devicePixelRatioF();
  double X, Y, Z, Dx, Dy, Dz;
  m_vue->ConvertWithProj(static_cast<int>(ecran.x() * r + 0.5), static_cast<int>(ecran.y() * r + 0.5), X, Y, Z, Dx, Dy, Dz);
  if (Dx * Dx + Dy * Dy + Dz * Dz < 1e-24) return false;
  origine = gp_Pnt(X, Y, Z);
  direction = gp_Dir(Dx, Dy, Dz);
  return true;
}

// Fait au dessin suivant, qui suit la mise en page : l'appelant ouvre souvent des panneaux juste après (esquisse :
// outils, propriétés) qui rétrécissent la vue. Si elle change encore de taille ensuite, le cadrage est refait.
void VuePiece::orienterVers(const gp_Ax3& repere, const Bnd_Box& cadre) {
  m_repereCadrage = repere;
  m_boiteCadrage = cadre;
  m_cadrageAuto = m_orientationEnAttente = true;
  update();
}

// Caméra face au plan du repère, cadrant la boîte pour la taille actuelle de la vue ; celle de la vue est rendue
// telle quelle
Handle(Graphic3d_Camera) VuePiece::cameraCadrant(const gp_Ax3& repere, const Bnd_Box& cadre) {
  Handle(Graphic3d_Camera) depart = new Graphic3d_Camera();
  depart->Copy(m_vue->Camera());
  const Handle(Graphic3d_Camera)& cam = m_vue->Camera();
  cam->SetDirection(repere.Direction().Reversed());
  cam->SetUp(repere.YDirection());
  // Une boîte plate (esquisse seule) ferait coïncider l'œil et le centre : on lui donne de l'épaisseur.
  Bnd_Box b = cadre;
  if (!b.IsVoid()) { b.Enlarge(std::max(1.0, 0.05 * std::sqrt(b.SquareExtent()))); m_vue->FitAll(b, 0.25, false); }
  else m_vue->FitAll(0.25, false);
  if (cam->Distance() < 1e-6) cam->SetEye(cam->Center().Translated(gp_Vec(repere.Direction()) * std::max(100.0, cam->Scale() * 2)));
  Handle(Graphic3d_Camera) arrivee = new Graphic3d_Camera();
  arrivee->Copy(cam);
  cam->Copy(depart);
  return arrivee;
}

void VuePiece::cadrer(const Bnd_Box& b0) {
  finCadrageAuto();
  if (b0.IsVoid()) return;
  Bnd_Box b = b0;
  b.Enlarge(std::max(1.0, 0.05 * std::sqrt(b.SquareExtent())));
  const gp_Dir d = m_vue->Camera()->Direction();
  m_vue->FitAll(b, 0.25);
  if (m_vue->Camera()->Distance() < 1e-6) m_vue->Camera()->SetEye(m_vue->Camera()->Center().Translated(gp_Vec(d) * -std::max(100.0, m_vue->Camera()->Scale() * 2)));
  m_vue->Invalidate();
  update();
}

void VuePiece::tourner(double dx, double dy) {
  finCadrageAuto();
  const Handle(Graphic3d_Camera)& cam = m_vue->Camera();
  const gp_Dir haut = cam->Up(), droite = cam->Direction().Crossed(haut);
  gp_Trsf a, b;
  a.SetRotation(gp_Ax1(m_pivot, haut), -dx * 0.0075);
  b.SetRotation(gp_Ax1(m_pivot, droite), -dy * 0.0075);
  const gp_Trsf t = b.Multiplied(a);
  cam->SetEyeAndCenter(cam->Eye().Transformed(t), cam->Center().Transformed(t));
  cam->SetUp(haut.Transformed(t));
  cam->OrthogonalizeUp();
  m_vue->Invalidate();
  update();
}

void VuePiece::deplacer(double dx, double dy) {
  finCadrageAuto();
  const Handle(Graphic3d_Camera)& cam = m_vue->Camera();
  int w = 0, h = 0;
  m_vue->Window()->Size(w, h);
  const double r = devicePixelRatioF();
  const gp_XYZ taille = cam->ViewDimensions();
  const double mmParPixel = taille.Y() / std::max(1, h) * r;
  const gp_Dir haut = cam->Up(), droite = cam->Direction().Crossed(haut);
  const gp_Vec v = gp_Vec(droite) * (-dx * mmParPixel) + gp_Vec(haut) * (dy * mmParPixel);
  cam->SetEyeAndCenter(cam->Eye().Translated(v), cam->Center().Translated(v));
  m_vue->Invalidate();
  update();
}

void VuePiece::zoomer(const QPointF& ecran, double crans) {
  finCadrageAuto();
  if (m_vue->Window().IsNull()) return;
  if (UpdateMouseScroll(Aspect_ScrollDelta(pixel(ecran), crans))) update();
}

void VuePiece::setMode(Mode m) {
  m_mode = m;
  m_ctx->ClearSelected(false);
  activerSelection();
  m_vue->Invalidate();
  update();
  emit selectionChangee();
}

std::vector<TopoDS_Shape> VuePiece::selection() const {
  std::vector<TopoDS_Shape> res;
  for (m_ctx->InitSelected(); m_ctx->MoreSelected(); m_ctx->NextSelected())
    if (m_ctx->HasSelectedShape()) res.push_back(m_ctx->SelectedShape());
  return res;
}

std::vector<VuePiece::ElementChoisi> VuePiece::selectionDetaillee() const {
  std::vector<ElementChoisi> res;
  for (m_ctx->InitSelected(); m_ctx->MoreSelected(); m_ctx->NextSelected()) {
    ElementChoisi e;
    if (m_ctx->HasSelectedShape()) e.forme = m_ctx->SelectedShape();
    const Handle(AIS_InteractiveObject) o = m_ctx->SelectedInteractive();
    for (const auto& [nom, a] : m_plans) if (o == a) { e.genre = ElementChoisi::Genre::PlanBase; e.plan = nom; }
    for (const auto& [id, a] : m_references) if (o == a) { e.genre = ElementChoisi::Genre::Reference; e.id = id; }
    for (const auto& [id, a] : m_esquisses) if (o == a) { e.genre = ElementChoisi::Genre::Esquisse; e.id = id; }
    res.push_back(e);
  }
  return res;
}

int VuePiece::esquisseSelectionnee() const {
  for (m_ctx->InitSelected(); m_ctx->MoreSelected(); m_ctx->NextSelected()) {
    const Handle(AIS_InteractiveObject) o = m_ctx->SelectedInteractive();
    for (const auto& [id, a] : m_esquisses) if (o == a) return id;
  }
  return 0;
}

void VuePiece::surligner(const std::vector<TopoDS_Shape>& formes) {
  if (!m_surlignage.IsNull()) { m_ctx->Remove(m_surlignage, false); m_surlignage.Nullify(); }
  if (!formes.empty()) {
    BRep_Builder b;
    TopoDS_Compound c;
    b.MakeCompound(c);
    for (const auto& f : formes) b.Add(c, f);
    m_surlignage = new AIS_Shape(c);
    m_surlignage->SetColor(rgb(0x1E, 0x7B, 0xE0));
    m_surlignage->SetWidth(4.0);
    m_surlignage->SetTransparency(0.35);
    m_ctx->Display(m_surlignage, AIS_Shaded, -1, false);
  }
  m_vue->Invalidate();
  update();
}

void VuePiece::setSelectionCumulative(bool oui) {
  auto& schemas = ChangeMouseSelectionSchemes();
  schemas.Bind(Aspect_VKeyMouse_LeftButton, oui ? AIS_SelectionScheme_XOR : AIS_SelectionScheme_Replace);
  schemas.Bind(static_cast<unsigned int>(Aspect_VKeyMouse_LeftButton) | static_cast<unsigned int>(Aspect_VKeyFlags_SHIFT), AIS_SelectionScheme_XOR);
}

void VuePiece::viderSelection() {
  m_ctx->ClearSelected(false);
  m_vue->Invalidate();
  update();
}

void VuePiece::OnSelectionChanged(const Handle(AIS_InteractiveContext)&, const Handle(V3d_View)&) {
  QTimer::singleShot(0, this, [this] { emit selectionChangee(); });
}

NCollection_Vec2<int> VuePiece::pixel(const QPointF& p) const {
  const double r = devicePixelRatioF();
  return NCollection_Vec2<int>(static_cast<int>(p.x() * r + 0.5), static_cast<int>(p.y() * r + 0.5));
}

void VuePiece::mousePressEvent(QMouseEvent* e) {
  setFocus();
  finCadrageAuto();
  if (e->button() == Qt::LeftButton && m_glisserObjets && !m_vue->Window().IsNull()) {
    // Un objet sous le curseur : on l'attrape pour le déplacer (face à l'écran)
    const NCollection_Vec2<int> p = pixel(e->position());
    m_ctx->MoveTo(p.x(), p.y(), m_vue, false);
    if (m_ctx->HasDetected() && m_ctx->MainSelector()->NbPicked() > 0) {
      m_departGlisse = m_ctx->MainSelector()->PickedPoint(1);
      m_glisseObjet = true;
      UpdateMouseButtons(p, Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_NONE, false);
      UpdateMouseButtons(p, Aspect_VKeyMouse_NONE, Aspect_VKeyFlags_NONE, false);
      emit glisserDebut(m_departGlisse.X(), m_departGlisse.Y(), m_departGlisse.Z());
      return;
    }
  }
  if (e->button() == Qt::LeftButton && m_poigneeVisible && !m_poignee.IsNull() && !m_vue->Window().IsNull()) {
    // Clic sur la poignée d'extrusion : on la tire le long de la direction de la fonction.
    const NCollection_Vec2<int> p = pixel(e->position());
    m_ctx->MoveTo(p.x(), p.y(), m_vue, false);
    if (m_ctx->HasDetected() && m_ctx->DetectedInteractive() == m_poignee) {
      m_glissePoignee = true;
      m_longueurDepart = m_poigneeLongueur;
      m_departPoignee = e->position();
      const gp_Pnt bout = m_poigneeBase.Translated(gp_Vec(m_poigneeDir) * m_poigneeLongueur);
      const QPointF a = projeter(bout), b = projeter(bout.Translated(gp_Vec(m_poigneeDir) * 1.0));
      m_vecteurEcran = b - a;  // pixels par millimètre le long de la direction
      return;
    }
  }
  if (e->button() == Qt::LeftButton) {
    m_rotation = {true, false, e->position(), e->position()};
    return;
  }
  if (m_vue->Window().IsNull()) return;
  Aspect_VKeyMouse b = Aspect_VKeyMouse_NONE;
  if (e->buttons() & Qt::RightButton) b |= Aspect_VKeyMouse_RightButton;
  if (e->buttons() & Qt::MiddleButton) b |= Aspect_VKeyMouse_MiddleButton;
  if (UpdateMouseButtons(pixel(e->position()), b, Aspect_VKeyFlags_NONE, false)) update();
}

void VuePiece::mouseReleaseEvent(QMouseEvent* e) {
  m_derniereSouris = e->position().toPoint();
  if (m_glisseObjet && e->button() == Qt::LeftButton) {
    m_glisseObjet = false;
    emit glisserFin();
    return;
  }
  if (m_glissePoignee && e->button() == Qt::LeftButton) {
    m_glissePoignee = false;
    emit poigneeDeplacee(m_poigneeLongueur, true);
    return;
  }
  if (e->button() == Qt::LeftButton && m_rotation.actif) {
    m_rotation.actif = false;
    if (!m_rotation.bouge && !m_vue->Window().IsNull()) {
      // Les objets ont pu être refaits depuis le dernier survol (aperçu) : on redétecte sous le curseur.
      const NCollection_Vec2<int> p = pixel(e->position());
      m_ctx->MoveTo(p.x(), p.y(), m_vue, false);
      ResetPreviousMoveTo();
      Aspect_VKeyFlags f = Aspect_VKeyFlags_NONE;
      if (m_cumulatif || (e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) f |= Aspect_VKeyFlags_SHIFT;
      // Point 3D du clic : sur l'objet détecté, sinon sur le plan de travail face à l'écran
      gp_Pnt q;
      bool surObjet = false;
      if (m_ctx->HasDetected() && m_ctx->MainSelector()->NbPicked() > 0) { q = m_ctx->MainSelector()->PickedPoint(1); surObjet = true; }
      else {
        double X, Y, Z, Vx, Vy, Vz;
        m_vue->ConvertWithProj(p.x(), p.y(), X, Y, Z, Vx, Vy, Vz);
        const gp_Dir n = m_vue->Camera()->Direction();
        const gp_Vec d(Vx, Vy, Vz);
        const double den = d.Dot(gp_Vec(n));
        if (std::abs(den) > 1e-12) {
          const double t = gp_Vec(gp_Pnt(X, Y, Z), m_planTravail).Dot(gp_Vec(n)) / den;
          q = gp_Pnt(X, Y, Z).Translated(d * t);
        } else q = gp_Pnt(X, Y, Z);
      }
      UpdateMouseButtons(pixel(e->position()), Aspect_VKeyMouse_LeftButton, f, false);
      UpdateMouseButtons(pixel(e->position()), Aspect_VKeyMouse_NONE, f, false);
      update();
      emit pointClique(q.X(), q.Y(), q.Z(), surObjet);
    }
    return;
  }
  if (m_vue->Window().IsNull()) return;
  Aspect_VKeyMouse b = Aspect_VKeyMouse_NONE;
  if (e->buttons() & Qt::RightButton) b |= Aspect_VKeyMouse_RightButton;
  if (e->buttons() & Qt::MiddleButton) b |= Aspect_VKeyMouse_MiddleButton;
  if (UpdateMouseButtons(pixel(e->position()), b, Aspect_VKeyFlags_NONE, false)) update();
}

void VuePiece::mouseMoveEvent(QMouseEvent* e) {
  if (m_glisseObjet && (e->buttons() & Qt::LeftButton)) {
    const gp_Pnt q = pointSurPlan(e->position(), m_departGlisse);
    emit glisserVers(q.X() - m_departGlisse.X(), q.Y() - m_departGlisse.Y(), q.Z() - m_departGlisse.Z());
    return;
  }
  if (m_glissePoignee && (e->buttons() & Qt::LeftButton)) {
    const double l2 = m_vecteurEcran.x() * m_vecteurEcran.x() + m_vecteurEcran.y() * m_vecteurEcran.y();
    if (l2 < 1e-9) return;
    const QPointF d = e->position() - m_departPoignee;
    const double delta = (d.x() * m_vecteurEcran.x() + d.y() * m_vecteurEcran.y()) / l2;
    double l = std::max(m_pasPoignee, m_longueurDepart + delta);
    l = std::round(l / m_pasPoignee) * m_pasPoignee;  // au pas de l'unité du document (AP-41)
    if (std::abs(l - m_poigneeLongueur) > 1e-9) {
      m_poigneeLongueur = l;
      placerPoignee();
      emit poigneeDeplacee(l, false);
    }
    return;
  }
  if (m_rotation.actif && (e->buttons() & Qt::LeftButton)) {
    if (!m_rotation.bouge && (e->position() - m_rotation.depart).manhattanLength() < 3) return;
    m_rotation.bouge = true;
    const double dx = e->position().x() - m_rotation.dernier.x(), dy = e->position().y() - m_rotation.dernier.y();
    m_rotation.dernier = e->position();
    const Handle(Graphic3d_Camera)& cam = m_vue->Camera();
    const gp_Dir haut = cam->Up(), droite = cam->Direction().Crossed(haut);
    gp_Trsf a, b;
    a.SetRotation(gp_Ax1(m_pivot, haut), -dx * 0.0075);
    b.SetRotation(gp_Ax1(m_pivot, droite), -dy * 0.0075);
    const gp_Trsf t = b.Multiplied(a);
    cam->SetEyeAndCenter(cam->Eye().Transformed(t), cam->Center().Transformed(t));
    cam->SetUp(haut.Transformed(t));
    cam->OrthogonalizeUp();
    m_vue->Invalidate();
    update();
    return;
  }
  if (m_vue->Window().IsNull()) return;
  if (m_suiviCurseur && e->buttons() == Qt::NoButton) {
    const gp_Pnt q = pointSol(e->position());
    emit survol(q.X(), q.Y(), q.Z());
  }
  Aspect_VKeyMouse b = Aspect_VKeyMouse_NONE;
  if (e->buttons() & Qt::RightButton) b |= Aspect_VKeyMouse_RightButton;
  if (e->buttons() & Qt::MiddleButton) b |= Aspect_VKeyMouse_MiddleButton;
  if (UpdateMousePosition(pixel(e->position()), b, Aspect_VKeyFlags_NONE, false)) update();
}

gp_Pnt VuePiece::pointSol(const QPointF& ecran) const {
  const NCollection_Vec2<int> p = pixel(ecran);
  double X, Y, Z, Vx, Vy, Vz;
  m_vue->ConvertWithProj(p.x(), p.y(), X, Y, Z, Vx, Vy, Vz);
  const gp_Vec d(Vx, Vy, Vz);
  // Plan horizontal du sol, sauf vue rasante : plan face à l'écran par l'origine
  if (std::abs(d.Z()) > 0.08 * d.Magnitude()) {
    const double t = (m_zSol - Z) / d.Z();
    return gp_Pnt(X, Y, Z).Translated(d * t);
  }
  return pointSurPlan(ecran, gp_Pnt(0, 0, m_zSol));
}

void VuePiece::wheelEvent(QWheelEvent* e) {
  finCadrageAuto();
  if (m_vue->Window().IsNull()) return;
  if (UpdateMouseScroll(Aspect_ScrollDelta(pixel(e->position()), e->angleDelta().y() / 120.0))) update();
}

void VuePiece::recadrer() {
  finCadrageAuto();
  m_vue->FitAll(0.15, false);
  m_vue->Invalidate();
  update();
}

void VuePiece::vueStandard(V3d_TypeOfOrientation o) {
  finCadrageAuto();
  Handle(Graphic3d_Camera) depart = new Graphic3d_Camera();
  depart->Copy(m_vue->Camera());
  m_vue->SetProj(o);
  m_vue->FitAll(0.15, false);
  Handle(Graphic3d_Camera) arrivee = new Graphic3d_Camera();
  arrivee->Copy(m_vue->Camera());
  m_vue->Camera()->Copy(depart);
  animerVers(arrivee);
}

std::string VuePiece::diagnostic(const QPointF& ecran) {
  std::string s;
  const NCollection_Vec2<int> p = pixel(ecran);
  m_ctx->MoveTo(p.x(), p.y(), m_vue, true);
  s += "détecté=" + std::to_string(m_ctx->HasDetected());
  if (m_ctx->HasDetected()) {
    const Handle(AIS_InteractiveObject) o = m_ctx->DetectedInteractive();
    for (const auto& [nom, a] : m_plans) if (o == a) s += " plan " + nom;
    for (const auto& [cle, a] : m_regions) if (o == a) s += " région " + cle;
    for (const auto& [id, a] : m_esquisses) if (o == a) s += " esquisse " + std::to_string(id);
    if (o == m_solide) s += " solide";
  }
  s += " mode=" + std::to_string(static_cast<int>(m_mode)) + " régions=" + std::to_string(m_regions.size()) + " solide=" + std::to_string(!m_solide.IsNull());
  if (!m_solide.IsNull()) {
    NCollection_List<int> modes;
    m_ctx->ActivatedModes(m_solide, modes);
    s += " modesSolide=" + std::to_string(modes.Size()) + " affiché=" + std::to_string(m_ctx->IsDisplayed(m_solide));
    for (int md : modes) {
      const Handle(SelectMgr_Selection)& sel = m_solide->Selection(md);
      s += " sel" + std::to_string(md) + "=" + (sel.IsNull() ? std::string("nulle") : std::to_string(sel->Entities().Size()) + "/" + std::to_string(static_cast<int>(sel->GetSelectionState())));
    }
    Bnd_Box b;
    BRepBndLib::Add(m_attente.forme, b);
    if (!b.IsVoid()) {
      double x0, y0, z0, x1, y1, z1;
      b.Get(x0, y0, z0, x1, y1, z1);
      const QPointF a = projeter(gp_Pnt(x0, y0, z0)), c = projeter(gp_Pnt(x1, y1, z1));
      s += " boîte écran " + std::to_string(int(a.x())) + "," + std::to_string(int(a.y())) + " à " + std::to_string(int(c.x())) + "," + std::to_string(int(c.y()));
    }
  }
  s += " plans=" + std::to_string(m_plans.size());
  for (const auto& [nom, a] : m_plans) s += " " + nom + (m_ctx->IsDisplayed(a) ? "(affiché" : "(caché") + (m_ctx->IsSelected(a) ? ",choisi)" : ")");
  int w = 0, h = 0;
  m_vue->Window()->Size(w, h);
  s += " fenetre=" + std::to_string(w) + "x" + std::to_string(h) + " pixel=" + std::to_string(p.x()) + "," + std::to_string(p.y());
  for (const auto& [nom, a] : m_plans) {
    NCollection_List<int> modes;
    m_ctx->ActivatedModes(a, modes);
    s += " modes" + nom + "=" + std::to_string(modes.Size());
  }
  // Balayage : y a-t-il quelque chose de détectable quelque part ?
  int trouves = 0, premierX = -1, premierY = -1;
  for (int y = 10; y < h; y += 20)
    for (int x = 10; x < w; x += 20) {
      m_ctx->MoveTo(x, y, m_vue, false);
      if (m_ctx->HasDetected()) { ++trouves; if (premierX < 0) { premierX = x; premierY = y; } }
    }
  s += " détectables=" + std::to_string(trouves) + " premier=" + std::to_string(premierX) + "," + std::to_string(premierY);
  return s;
}

std::string VuePiece::etatCamera() const {
  const Handle(Graphic3d_Camera)& c = m_vue->Camera();
  char t[400];
  std::snprintf(t, sizeof t, "oeil (%.2f %.2f %.2f) centre (%.2f %.2f %.2f) distance %.3f haut (%.2f %.2f %.2f) pivot (%.2f %.2f %.2f) échelle %.3f", c->Eye().X(), c->Eye().Y(), c->Eye().Z(),
                c->Center().X(), c->Center().Y(), c->Center().Z(), c->Distance(), c->Up().X(), c->Up().Y(), c->Up().Z(), m_pivot.X(), m_pivot.Y(), m_pivot.Z(), c->Scale());
  return t;
}

void VuePiece::setAffichage(const Affichage& a) {
  const bool triedre = a.triedre != m_affichage.triedre;
  m_affichage = a;
  if (triedre && m_pret) {
    if (a.triedre) m_vue->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, Quantity_NOC_BLACK, 0.16, V3d_ZBUFFER);
    else m_vue->TriedronErase();
  }
  if (m_pret) appliquer();
}

void VuePiece::setTracesMesure(const std::vector<TraitMesure>& traits) {
  m_traces = traits;
  for (const auto& o : m_aisTraces) m_ctx->Remove(o, false);
  m_aisTraces.clear();
  const Quantity_Color c = rgb(0xC2, 0x40, 0x2E);
  const QByteArray famille = QFontInfo(font()).family().toUtf8();
  Bnd_Box b;
  for (const auto& t : m_traces) { b.Add(t.a); b.Add(t.b); }
  const double rayon = b.IsVoid() ? 0.5 : std::max(0.15, 0.008 * std::sqrt(b.SquareExtent()));
  for (const auto& t : m_traces) {
    auto montrer = [&](const Handle(AIS_InteractiveObject)& o, int mode) {
      o->SetZLayer(Graphic3d_ZLayerId_Topmost);
      m_ctx->Display(o, mode, -1, false);  // -1 : jamais choisi au clic
      m_aisTraces.push_back(o);
    };
    if (t.a.Distance(t.b) > 1e-9) {
      Handle(AIS_Shape) l = new AIS_Shape(BRepBuilderAPI_MakeEdge(t.a, t.b).Edge());
      l->SetColor(c);
      l->Attributes()->SetWireAspect(new Prs3d_LineAspect(c, Aspect_TOL_SOLID, 2.0));
      montrer(l, AIS_WireFrame);
    }
    for (const gp_Pnt& p : {t.a, t.b}) {
      Handle(AIS_Shape) s = new AIS_Shape(BRepPrimAPI_MakeSphere(p, rayon).Shape());
      s->SetColor(c);
      montrer(s, AIS_Shaded);
      if (t.a.Distance(t.b) <= 1e-9) break;
    }
    if (!t.texte.isEmpty()) {
      Handle(AIS_TextLabel) l = new AIS_TextLabel();
      l->SetText(TCollection_ExtendedString(t.texte.toUtf8().constData(), true));
      l->SetPosition(gp_Pnt((t.a.XYZ() + t.b.XYZ()) / 2));
      l->SetColor(c);
      l->SetFont(famille.constData());
      l->SetHeight(14 * devicePixelRatioF());
      montrer(l, 0);
    }
  }
  update();
}

void VuePiece::setPoignee(bool visible, const gp_Pnt& base, const gp_Dir& dir, double longueur) {
  m_poigneeVisible = visible;
  if (!visible) {
    if (!m_poignee.IsNull()) { m_ctx->Remove(m_poignee, false); m_poignee.Nullify(); }
    update();
    return;
  }
  if (m_glissePoignee) return;  // pendant le glissement, c'est la souris qui commande
  m_poigneeBase = base;
  m_poigneeDir = dir;
  m_poigneeLongueur = longueur;
  placerPoignee();
}

// Flèche orange : tige et pointe, de taille proportionnée à la pièce, au bout de l'extrusion.
void VuePiece::placerPoignee() {
  if (!m_pret) return;
  Bnd_Box b;
  if (!m_attente.forme.IsNull()) BRepBndLib::Add(m_attente.forme, b);
  const double taille = b.IsVoid() ? 12.0 : std::clamp(std::sqrt(b.SquareExtent()) * 0.12, 4.0, 60.0);
  const gp_Pnt bout = m_poigneeBase.Translated(gp_Vec(m_poigneeDir) * m_poigneeLongueur);
  const gp_Ax2 axe(bout, m_poigneeDir);
  TopoDS_Shape tige = BRepPrimAPI_MakeCylinder(axe, taille * 0.06, taille * 0.7).Shape();
  const gp_Ax2 axePointe(bout.Translated(gp_Vec(m_poigneeDir) * taille * 0.7), m_poigneeDir);
  TopoDS_Shape pointe = BRepPrimAPI_MakeCone(axePointe, taille * 0.18, 0, taille * 0.35).Shape();
  TopoDS_Compound fleche;
  BRep_Builder bb;
  bb.MakeCompound(fleche);
  bb.Add(fleche, tige);
  bb.Add(fleche, pointe);
  if (!m_poignee.IsNull()) m_ctx->Remove(m_poignee, false);
  m_poignee = new AIS_Shape(fleche);
  m_poignee->SetColor(rgb(0xE0, 0x7B, 0x1E));
  m_poignee->SetZLayer(Graphic3d_ZLayerId_Topmost);
  m_ctx->Display(m_poignee, AIS_Shaded, 0, false);
  m_vue->Invalidate();
  update();
}

std::string VuePiece::referenceSelectionnee() const {
  for (m_ctx->InitSelected(); m_ctx->MoreSelected(); m_ctx->NextSelected()) {
    const Handle(AIS_InteractiveObject) o = m_ctx->SelectedInteractive();
    for (const auto& [id, a] : m_references) if (o == a) return "F:" + std::to_string(id);
  }
  return {};
}

void VuePiece::setCoupe(bool active, const gp_Pln& plan) {
  if (!m_pret) return;
  if (!active) {
    if (!m_planCoupe.IsNull()) { m_vue->RemoveClipPlane(m_planCoupe); m_planCoupe.Nullify(); }
  } else {
    if (m_planCoupe.IsNull()) {
      m_planCoupe = new Graphic3d_ClipPlane(plan);
      m_planCoupe->SetCapping(true);
      m_planCoupe->SetUseObjectMaterial(false);
      Graphic3d_MaterialAspect matiere(Graphic3d_NOM_PLASTIC);
      matiere.SetColor(rgb(0xE0, 0x7B, 0x1E));
      m_planCoupe->SetCappingMaterial(matiere);
      m_vue->AddClipPlane(m_planCoupe);
    } else {
      m_planCoupe->SetEquation(plan);
    }
  }
  m_vue->Invalidate();
  update();
}

// Point sous le curseur, sur le plan face à l'écran qui passe par « origine »
gp_Pnt VuePiece::pointSurPlan(const QPointF& pos, const gp_Pnt& origine) const {
  const NCollection_Vec2<int> p = pixel(pos);
  double X, Y, Z, Vx, Vy, Vz;
  m_vue->ConvertWithProj(p.x(), p.y(), X, Y, Z, Vx, Vy, Vz);
  const gp_Dir n = m_vue->Camera()->Direction();
  const gp_Vec d(Vx, Vy, Vz);
  const double den = d.Dot(gp_Vec(n));
  if (std::abs(den) < 1e-12) return gp_Pnt(X, Y, Z);
  const double t = gp_Vec(gp_Pnt(X, Y, Z), origine).Dot(gp_Vec(n)) / den;
  return gp_Pnt(X, Y, Z).Translated(d * t);
}

// Fond de la vue aux couleurs du thème (dégradé vertical)
void VuePiece::appliquerTheme() {
  const CouleursInterface& c = theme().c();
  const auto qc = [](const QColor& x) { return Quantity_Color(x.redF(), x.greenF(), x.blueF(), Quantity_TOC_sRGB); };
  m_vue->SetBgGradientColors(qc(c.vue3dHaut), qc(c.vue3dBas), Aspect_GFM_VER, false);
  m_vue->Invalidate();
  update();
}
