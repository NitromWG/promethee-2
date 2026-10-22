// Prométhée : atelier d'assemblage. Ruban organisé selon le travail du mécanicien : insérer les composants,
// les contraindre, les manipuler, vérifier l'ensemble, le documenter, régler l'affichage. Panneau de propriétés
// du composant ou de la contrainte choisis, suggestions de contraintes quand deux faces sont cliquées.
// Licence GPL-3.0-only.
#pragma once

#include <memory>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QWidget>

#include <TopoDS_Shape.hxx>

#include <TopLoc_Location.hxx>
#include <gp_Pnt.hxx>

#include "promethee/assemblage.hpp"
#include "promethee/construction.hpp"
#include "promethee/nomenclature_meca.hpp"

class Document;
class EtiquetteEtat;
class QFrame;
class QTimer;
class QScrollArea;
class QTabWidget;
class QToolButton;
class VuePiece;

class AtelierAssemblage : public QWidget {
  Q_OBJECT
public:
  explicit AtelierAssemblage(Document* doc, QWidget* parent = nullptr);
  void setEtatIntegre(bool oui);
  void ouvrir(int assemblage, int composant = -1);
  int assemblage() const { return m_indice; }
  void choisirComposant(int k);
  void choisirContrainte(int k);
  // STEP, IGES, BREP, STL. Un fichier d'assemblage devient, au choix, des pièces placées dans des sous-assemblages
  // (structure, noms et couleurs du fichier) ou une seule pièce ; « Demander » pose la question.
  enum class ModeImport { Demander, Structure, UnePiece };
  bool insererFichier(const QString& chemin, QString* erreur = nullptr, ModeImport mode = ModeImport::Demander);
  bool insererDepuisProjet(const QString& chemin, int indicePiece, QString* erreur = nullptr);
  // auCurseur : le composant suit la souris jusqu'au clic qui le pose (M9) ; sinon posé à droite de l'ensemble
  void insererPiece(int indice, bool auCurseur = false);
  void commencerPose(int composant);
  void suivrePose(double x, double y, double z);
  void finirPose(bool poser);  // faux : le composant revient à droite de l'ensemble
  bool enPose() const { return m_pose >= 0; }
  void abandonnerOutil();  // Échap : outil, mesure ou pose en cours abandonnés
  // Contraintes (A2) : une référence est une face, une arête, un sommet d'un composant, ou un plan, un axe, l'origine de
  // l'assemblage (composant COMPOSANT_ASSEMBLAGE, référence « assemblage:XY »…). Outil commencé au ruban, puis deux
  // références choisies (clics dans la vue, ou choisirReference pour les essais)
  static constexpr int COMPOSANT_ASSEMBLAGE = -2;
  void commencerContrainte(const std::string& type);
  void choisirReference(int composant, const std::string& reference);
  bool outilContrainte() const { return !m_typeContrainte.empty(); }
  // Plans, axes et origine de l'assemblage montrés dans la vue (aussi pendant un outil de contrainte)
  void setRepereVisible(bool oui);
  bool repereVisible() const { return m_repereVisible; }
  // Libellé lisible d'une référence (« Arête de Plaque », « Plan XY de l'assemblage »)
  QString libelleReference(int composant, const std::string& reference) const;
  // Plan d'assemblage (vues, bulles, nomenclature) prêt à montrer ; nul sans assemblage
  std::unique_ptr<class MiseEnPlan> creerMiseEnPlan();
  class DialogueNomenclature* nomenclature();  // ouvre la nomenclature (non modale) ; nulle sans assemblage
  prom::MassePiece massePiece(int piece);
  void basculerMesure();  // mesure dans la vue de l'assemblage (M11)
  QString texteEtat() const;
  void insererSousAssemblage(int indice);
  // Opérations sur le composant choisi (aussi utilisées par les essais)
  void pivoter(int axe, double angle);       // axe 0 X, 1 Y, 2 Z
  void translater(double dx, double dy, double dz);  // déplacement relatif du composant choisi (A4)
  void pivoterAutourDuCentre(int axe, double angle);  // autour d'un axe de l'assemblage passant par son centre (A4)
  // Manipulation à la souris (A4) : déplacer, ou faire pivoter autour d'un axe de l'assemblage (0 X, 1 Y, 2 Z) passant
  // par le centre du composant. Le composant suit la souris dans le respect de ses contraintes : les composants qui lui
  // sont liés suivent s'ils le peuvent, sinon il ne prend que les mouvements que ses contraintes lui laissent.
  void basculerDeplacerSouris();
  void basculerPivoterSouris(int axe);
  int pivotSouris() const { return m_pivotSouris; }  // −1 : pas de rotation à la souris
  void debutGlisser(double x, double y, double z);
  void glisserVers(double dx, double dy, double dz);
  void finGlisser();
  // Placement par valeurs et pivotement d'un angle : dans le panneau du composant, sans boîte modale (A4)
  void placerParValeurs();
  void pivoterParValeur();
  void copier();
  void repeter(bool circulaire, int nombre, double pas, int axe);  // circulaire : autour de l'axe X, Y ou Z de l'assemblage
  void panneauRepetition(bool circulaire);  // nombre, pas, axe dans le panneau latéral (U1)
  class PanneauLateral* panneauLateral() const { return m_panneau; }
  void symetriser(int plan);                 // 1 XY, 2 XZ, 3 YZ
  void basculerFixe();
  void basculerMasque();
  void basculerSuppression();
  void setEclatement(double facteur);        // vue éclatée (affichage seulement), 0 : assemblé
  int composantChoisi() const { return m_choisi; }
  // Liaisons, simulation, sous-assemblage depuis une sélection (aussi pour les essais)
  void lier(const std::string& type, int a, const std::string& refA, int b, const std::string& refB, double valeur);
  // Image de la simulation : la liaison pilotée à cette valeur, sans modifier le projet. Faux si le mécanisme ne peut
  // pas atteindre cette valeur : l'image précédente est gardée (AS-19)
  bool simuler(int contrainte, double valeur);
  void arreterSimulation();
  void creerSousAssemblage(const std::set<int>& composants);   // sans question (essais) ; les contraintes coupées sont comptées
  void creerSousAssemblageDepuisSelection();                   // commande du ruban : nomme d'abord les contraintes coupées
  const std::set<int>& selectionMultiple() const { return m_multi; }
  void animationMontage();

signals:
  void nouvellePieceDemandee(int assemblage);
  void ouvrirPiece(int piece, int assemblage);
  void messageChange(const QString& html);

private:
  void construireRuban();
  void reconstruire(bool recadrer);
  void majMenuInserer();
  void majProprietes();
  void majBarreFlottante(const QPoint& pres);
  void creerContrainte(const std::string& type, int a, const std::string& refA, int b, const std::string& refB);
  prom::EtatAssemblage resoudre();
                 // Échap : contrainte commencée, barre flottante, mesure, déplacement (AS-35, AS-37)
  void signalerProblemes(const prom::EtatAssemblage& e);  // contraintes écartées ou violées, comptées dans la barre d'état (AS-12)
  // Les deux faces conviennent-elles à ce type de contrainte ? Sinon, la raison (AS-08)
  bool facesAcceptees(const std::string& type, int a, const std::string& refA, int b, const std::string& refB, QString* raison);
  std::string signaturePieces() const;     // empreinte des pièces du projet : une pièce modifiée relance la résolution (AS-12)
  void remplacer();
  void remplacerPar(int piece);
  void retirer();
  void interferences();
  void jeux();
  void libertes();
  void diagnostic();
  void proprietesPhysiques();
  void miseEnPlan();  // plan d'assemblage : vues, bulles, nomenclature
  void exporterStep();
  void vueEnCoupe();
  void panneauEclate();
  void modifierComposant(int k, const std::function<void(prom::ComposantAssemblage&)>& f);
  const prom::ResultatPiece& resultatPiece(int indice);
  TopoDS_Shape formeComposant(int k);  // géométrie placée (pièce ou sous-assemblage) telle qu'affichée, nulle si le rang est invalide
  // Géométrie placée d'un composant d'un assemblage donné ; pour un composant symétrique, « miroir » reçoit la
  // correspondance face affichée → face de la pièce (AS-09)
  TopoDS_Shape formeComposantDe(const prom::Assemblage& a, int k, std::vector<std::pair<TopoDS_Shape, TopoDS_Shape>>* miroir = nullptr);
  TopoDS_Shape formeAssemblage(int indice, std::vector<char>& enCours, int profondeur);
  // Rangs tenus par l'atelier : validés avant toute lecture du projet, revalidés à chaque changement
  bool assemblageValide() const;
  bool composantValide(int k) const;
  void oublierSelection();
  void revaliderSelection();
  void arreterAnimation();
  const prom::Assemblage& assemblageAffiche() const;
  int composantSous(const TopoDS_Shape& s) const;
  std::string nomFaceSous(const TopoDS_Shape& face, int composant);
  // Face, arête ou sommet affiché : son nom stable dans sa pièce, ou son chemin « @k/… » dans un sous-assemblage (A2)
  std::string nomElementSous(const TopoDS_Shape& s, int composant);
  std::string cheminElementSous(const TopoDS_Shape& s, int sousAssemblage, const gp_Trsf& cumul, int profondeur);
  // Élément cliqué : plan, axe ou origine de l'assemblage, sinon élément d'un composant ; faux si rien d'utilisable
  bool elementClique(const TopoDS_Shape& s, int& composant, std::string& nom);
  QString nomCote(const prom::ContrainteAssemblage& c, bool coteB) const;  // composant, ou « Assemblage »
  void majRepere();
  QString nomComposant(int k) const;

  Document* m_doc;
  int m_indice = -1, m_choisi = -1, m_contrainteChoisie = -1;
  VuePiece* m_vue = nullptr;
  QTabWidget* m_ruban = nullptr;
  QScrollArea* m_props = nullptr;
  EtiquetteEtat* m_etat;
  int m_pose = -1;                                  // composant en cours de pose au curseur
  std::array<double, 3> m_poseDepart{}, m_poseDecalage{};
  QToolButton* m_inserer = nullptr;
  QFrame* m_barre = nullptr;
  class PanneauLateral* m_panneau = nullptr;  // outils sans fenêtre modale (U1)
  QFrame* m_panneauCoupe = nullptr;
  QFrame* m_panneauEclate = nullptr;
  std::map<std::size_t, prom::ResultatPiece> m_cache;  // empreinte du JSON de la pièce → pièce construite
  std::vector<TopoDS_Shape> m_formes;      // par composant, telles qu'affichées (éclatement, aperçu de simulation)
  // Par composant, à leur place réelle dans le modèle, sans éclatement ni aperçu, nulles pour les suspendus : c'est
  // sur elles que travaillent interférences, jeux, coupe, insertion, copie et export (AS-13)
  std::vector<TopoDS_Shape> m_formesPlacees;
  std::vector<TopLoc_Location> m_locAffichage;  // placement d'affichage de chaque composant (retiré pour nommer une face, AS-21)
  std::vector<std::vector<std::pair<TopoDS_Shape, TopoDS_Shape>>> m_facesMiroir;  // composants symétriques
  std::vector<std::pair<int, prom::EtatAssemblage>> m_problemesSous;
  bool m_enResolution = false;  // sous-assemblages résolus avec lui (A3)
  std::optional<prom::EtatAssemblage> m_dernierEtat;  // dernière résolution, affichée tant qu'elle est en échec (AS-22)
  std::string m_signature;                 // empreinte des pièces à la dernière résolution
  std::set<int> m_isoles;                  // composants isolés (vide : tous)
  double m_eclatement = 0, m_transparence = 0;
  bool m_deplacer = false, m_mesure = false;
  // Contrainte en cours de pose, et dernière face cliquée (pour les suggestions)
  std::string m_typeContrainte;
  int m_premierComposant = -1;
  std::string m_premiereFace;
  int m_faceComposant = -1;
  std::string m_faceNom;
  int m_secondComposant = -1;  // second élément cliqué, pour la barre de suggestions
  std::string m_secondNom;
  bool m_repereVisible = false;
  TopoDS_Shape m_faceForme;
  prom::ComposantAssemblage m_avantGlisse;
  int m_pivotSouris = -1;                   // axe de la rotation à la souris (A4), −1 : aucune
  gp_Pnt m_pointGlisse, m_centreGlisse;     // point attrapé et centre du composant au début du glisser
  std::vector<prom::ComposantAssemblage> m_composantsGlisse;  // état de l'assemblage au début du glisser
  std::vector<prom::ResultatPiece> m_piecesGlisse;            // pièces construites, gardées le temps du glisser
  // Pose demandée par la souris, mise en accord avec les contraintes : composants écrits pendant le geste
  void poserEnRespectant(int k, const prom::ComposantAssemblage& cible);
  void placerComposant(int k, const prom::ComposantAssemblage& cible);  // même chose en un pas d'annulation
  std::optional<prom::Assemblage> m_apercu;        // simulation : assemblage affiché à la place de celui du projet
  std::set<int> m_multi;                           // composants sélectionnés avec Maj
  std::vector<double> m_eclatementComposant;       // animation de montage : écart propre à chaque composant
  QTimer* m_animation = nullptr;                   // une seule minuterie d'animation de montage
  int m_etapeAnimation = 0;
  QFrame* m_panneauSimulation = nullptr;
  void panneauSimulation();
};
