// Prométhée : vue 3D d'une pièce paramétrique, avec sélection d'arêtes ou de faces. Licence GPL-3.0-only.
#pragma once

#include <set>

#include <vector>

#include <QOpenGLWidget>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Shape.hxx>
#include <AIS_ViewController.hxx>
#include <AIS_ViewCube.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <gp_Pln.hxx>
#include <V3d_View.hxx>
#include <Bnd_Box.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pnt.hxx>

#include "promethee/construction.hpp"

class VuePiece : public QOpenGLWidget, public AIS_ViewController {
  Q_OBJECT
public:
  enum class Mode { Tout, Faces, Aretes, Sommets, Regions };  // filtre de sélection
  explicit VuePiece(QWidget* parent = nullptr);
  ~VuePiece() override;
  void afficher(const prom::ResultatPiece& r, bool recadrerSiNouveau = true);
  void setMode(Mode m);
  Mode mode() const { return m_mode; }
  std::vector<TopoDS_Shape> selection() const;
  // Sélection avec la nature de chaque élément : forme de la pièce, plan de base (infini), référence (plan, axe,
  // point de la pièce) ou esquisse ; les plans et points dessinés ne se mesurent pas comme des carrés ou des
  // sphères (AP-17)
  struct ElementChoisi {
    enum class Genre { Forme, PlanBase, Reference, Esquisse } genre = Genre::Forme;
    TopoDS_Shape forme;
    std::string plan;  // plan de base : « XY », « XZ », « YZ »
    int id = 0;        // référence ou esquisse
  };
  std::vector<ElementChoisi> selectionDetaillee() const;
  // Pas de la poignée d'extrusion, en mm : un pas rond dans l'unité du document (AP-41)
  void setPasPoignee(double mm) { m_pasPoignee = mm > 0 ? mm : 0.1; }
  void viderSelection();
  int esquisseSelectionnee() const;                         // esquisse cliquée dans la vue (0 sinon)
  std::string referenceSelectionnee() const;                // plan, axe ou point de référence cliqué (« F:12 »)
  void surligner(const std::vector<TopoDS_Shape>& formes);  // arêtes ou faces retenues par une fonction en cours
  void setSelectionCumulative(bool oui);                     // chaque clic ajoute ou retire, sans Maj
  std::string planSelectionne() const;                       // « XY », « XZ », « YZ » si un plan de base est choisi
  std::string axeSelectionne() const;                        // « X », « Y », « Z » (axes de base) ou « O » (origine)
  void setPlansVisibles(bool oui);
  bool plansVisibles() const { return m_plansVisibles; }
  void mettreEnEvidencePlan(const std::string& nom);       // « XY », « XZ », « YZ », ou vide
  void sauverCamera();
  void animerVers(const Handle(Graphic3d_Camera)& fin);  // transition douce de la caméra (300 ms)
  gp_Dir directionCamera() const { return m_vue->Camera()->Direction(); }
  QPoint derniereSouris() const { return m_derniereSouris; }
  // Affichage : esquisses, géométrie de construction, arêtes du solide, trièdre ; esquisses masquées ; apparence
  struct Affichage { bool esquisses = true, construction = true, aretes = true, triedre = true; };
  void setAffichage(const Affichage& a);
  const Affichage& affichage() const { return m_affichage; }
  void setEsquissesMasquees(const std::set<int>& ids) { m_masquees = ids; }
  void setApparence(const QColor& couleur, double transparence) { m_couleurSolide = couleur; m_transparenceSolide = transparence; }
  // Sélection cumulative (mesure) : chaque clic ajoute ou retire, sans Maj
  void setCumulatif(bool oui) { m_cumulatif = oui; }
  // Vue en coupe : plan de coupe avec remplissage de la matière coupée
  void setCoupe(bool active, const gp_Pln& plan = gp_Pln());
  // Plan de travail des clics dans le vide : passe par ce point, face à l'écran
  void setPlanTravail(const gp_Pnt& p) { m_planTravail = p; }
  // Couleur propre de certaines sous-formes (composants d'un assemblage)
  void setCouleursSousFormes(const std::vector<std::pair<TopoDS_Shape, QColor>>& c) { m_couleursSousFormes = c; }
  // Glisser des objets à la souris (déplacer un composant) au lieu de tourner la vue
  void setGlisserObjets(bool oui) { m_glisserObjets = oui; }
  // Suivi du curseur (insertion au curseur, M9) : chaque mouvement sans bouton émet survol() avec le point du sol
  // (plan horizontal z = zSol) sous le curseur ; vue de profil : plan face à l'écran passant par l'origine
  void setSuiviCurseur(bool oui, double zSol = 0) { m_suiviCurseur = oui; m_zSol = zSol; }
  gp_Pnt pointSol(const QPointF& ecran) const;
  // Poignée d'extrusion : flèche au bout de la fonction, qu'on tire pour changer la profondeur
  void setPoignee(bool visible, const gp_Pnt& base = gp_Pnt(), const gp_Dir& dir = gp_Dir(0, 0, 1), double longueur = 0);
  // Mesures dessinées dans la vue (M11) : segment entre deux points, extrémités marquées, valeur au milieu, devant
  // la pièce ; non cliquables. Liste vide : rien n'est dessiné.
  struct TraitMesure { gp_Pnt a, b; QString texte; };
  void setTracesMesure(const std::vector<TraitMesure>& traits);
  std::size_t nombreTracesMesure() const { return m_traces.size(); }
  double echelleCamera() const { return m_vue->Camera()->Scale(); }
  gp_Pnt centreCamera() const { return m_vue->Camera()->Center(); }
  std::string etatCamera() const;
  std::string diagnostic(const QPointF& ecran);
  void restaurerCamera();
  // Régions d'une esquisse proposées au choix (faces translucides cliquables), clé et face.
  void afficherRegions(const std::vector<prom::RegionEsquisse>& regions, const std::vector<std::string>& retenues);
  std::string regionSelectionnee() const;
  void setSolideTranslucide(bool oui);
  // Esquisse en contexte : projection entre l'écran et le plan d'esquisse, et pilotage de la caméra.
  QPointF projeter(const gp_Pnt& p) const;
  bool rayon(const QPointF& ecran, gp_Pnt& origine, gp_Dir& direction) const;
  // Face au plan du repère, la boîte cadrée ; cadrage refait si la vue change de taille (panneaux de l'esquisse
  // mis en place après coup), jusqu'à ce que l'utilisateur tourne, déplace ou zoome la vue
  void orienterVers(const gp_Ax3& repere, const Bnd_Box& cadre);
  void cadrer(const Bnd_Box& b);
  void tourner(double dx, double dy);
  void deplacer(double dx, double dy);
  void zoomer(const QPointF& ecran, double crans);
  void recadrer();
  void vueStandard(V3d_TypeOfOrientation o);
  QSize minimumSizeHint() const override { return {240, 200}; }

signals:
  void poigneeDeplacee(double longueur, bool fini);
  void pointClique(double x, double y, double z, bool surObjet);  // chaque clic gauche (sans glisser)
  void glisserDebut(double x, double y, double z);   // un objet est attrapé en ce point
  void glisserVers(double dx, double dy, double dz); // déplacement depuis le point de départ, face à l'écran
  void glisserFin();
  void selectionChangee();
  void cameraChangee();
  void survol(double x, double y, double z);  // suivi du curseur actif : point du sol sous le curseur

protected:
  void initializeGL() override;
  void paintGL() override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void OnSelectionChanged(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& vue) override;
  void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx, const Handle(V3d_View)& vue) override;

private:
  void appliquerTheme();
  void appliquer();
  bool initialiserOcct();
  Handle(Graphic3d_Camera) cameraCadrant(const gp_Ax3& repere, const Bnd_Box& cadre);
  void finCadrageAuto() { m_cadrageAuto = m_orientationEnAttente = false; }
  void activerSelection();
  NCollection_Vec2<int> pixel(const QPointF& p) const;

  Handle(V3d_Viewer) m_visu;
  Handle(V3d_View) m_vue;
  Handle(AIS_InteractiveContext) m_ctx;
  Handle(AIS_ViewCube) m_cube;
  Handle(AIS_Shape) m_solide;
  std::vector<std::pair<int, Handle(AIS_Shape)>> m_esquisses;
  Handle(AIS_Shape) m_surlignage;
  std::vector<std::pair<std::string, Handle(AIS_Shape)>> m_plans;
  std::vector<std::pair<std::string, Handle(AIS_Shape)>> m_axesBase;  // axes X, Y, Z et origine, cliquables (M7)
  std::vector<std::pair<std::string, Handle(AIS_Shape)>> m_regions;
  std::vector<Handle(AIS_InteractiveObject)> m_etiquettesPlans;
  std::vector<std::pair<int, Handle(AIS_InteractiveObject)>> m_references;
  Handle(Graphic3d_Camera) m_cameraSauvee;
  class QVariantAnimation* m_animation = nullptr;
  bool m_cadrageAuto = false, m_orientationEnAttente = false;
  double m_pasPoignee = 0.1;
  gp_Ax3 m_repereCadrage;
  Bnd_Box m_boiteCadrage;
  std::string m_planEnEvidence;
  bool m_plansVisibles = true, m_translucide = false;
  void majPlans(const Bnd_Box& b);
  prom::ResultatPiece m_attente;
  bool m_aAfficher = false, m_pret = false, m_cadre = false, m_recadrer = false;
  Mode m_mode = Mode::Tout;
  struct Rotation { bool actif = false, bouge = false; QPointF depart, dernier; };
  Rotation m_rotation;
  QPoint m_derniereSouris;
  Affichage m_affichage;
  bool m_cumulatif = false;
  gp_Pnt m_planTravail;
  std::vector<std::pair<TopoDS_Shape, QColor>> m_couleursSousFormes;
  bool m_glisserObjets = false, m_glisseObjet = false;
  gp_Pnt m_departGlisse;
  gp_Pnt pointSurPlan(const QPointF& pos, const gp_Pnt& origine) const;
  Handle(Graphic3d_ClipPlane) m_planCoupe;
  bool m_poigneeVisible = false, m_glissePoignee = false;
  gp_Pnt m_poigneeBase;
  gp_Dir m_poigneeDir = gp_Dir(0, 0, 1);
  double m_poigneeLongueur = 0, m_longueurDepart = 0;
  QPointF m_departPoignee, m_vecteurEcran;
  Handle(AIS_Shape) m_poignee;
  std::vector<TraitMesure> m_traces;
  bool m_suiviCurseur = false;
  double m_zSol = 0;
  std::vector<Handle(AIS_InteractiveObject)> m_aisTraces;
  void placerPoignee();
  std::set<int> m_masquees;
  QColor m_couleurSolide = QColor(0xC9, 0xCF, 0xDF);
  double m_transparenceSolide = 0;
  gp_Pnt m_pivot;
};
