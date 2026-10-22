// Prométhée : éditeur d'esquisse 2D. Licence GPL-3.0-only.
// Tracé avec accroches (points, milieux, courbes, axes), contraintes, cotation intelligente à la manière de
// SolidWorks (on clique les éléments, on place la cote, on tape la valeur), glisser avec résolution en direct.
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <map>
#include <set>

#include <QWidget>

#include "promethee/esquisse.hpp"
#include "promethee/unites.hpp"

class QLineEdit;
class QPainter;

class Esquisseur : public QWidget {
  Q_OBJECT
public:
  enum class Outil { Selection, Ligne, LigneConstruction, Rectangle, Cercle, Arc, Arc3Points, Rainure, Polygone, Point, Cote, Conge,
                     RectangleCentre, Rectangle3Points, Parallelogramme, Cercle3Points, ArcTangent, LigneMilieu, Ajuster, Prolonger,
                     ChanfreinEsquisse, RainureArc };
  explicit Esquisseur(QWidget* parent = nullptr);

  void editer(const prom::Esquisse& e);
  const prom::Esquisse& esquisse() const { return m_e; }
  const prom::EtatEsquisse& etat() const { return m_etat; }
  void setOutil(Outil o);
  Outil outil() const { return m_outil; }
  // Réglages des outils (U1) : réglés avant les clics, dans le champ posé sur la vue
  double distanceChanfrein() const { return m_distanceChanfrein; }
  void setDistanceChanfrein(double d) { if (d > 0) m_distanceChanfrein = d; majReglageOutil(); }
  double largeurRainureArc() const { return m_largeurRainureArc; }
  void setLargeurRainureArc(double l) { if (l > 0) m_largeurRainureArc = l; majReglageOutil(); }
  void setCotesPolygone(int n) { m_cotesPolygone = n; }
  bool contraindre(prom::TypeContrainte t);  // sur la sélection courante ; faux (et message) si elle ne convient pas
  void basculerConstruction();
  void basculerReference();                   // cote pilotante ↔ cote de référence
  void supprimerSelection();
  void toutSelectionner();
  void recadrer();
  QSize sizeHint() const override { return {800, 600}; }
  // Historique propre à l'esquisse (Ctrl+Z, Ctrl+Y)
  void annulerEtape();
  void retablirEtape();
  bool peutAnnulerEtape() const { return !m_annulations.empty(); }
  bool peutRetablirEtape() const { return !m_retablissements.empty(); }
  // Mode construction : tout ce qui est tracé devient géométrie d'aide (touche X)
  void setModeConstruction(bool oui);
  bool modeConstruction() const { return m_modeConstruction; }
  bool aSelection() const { return !m_selection.empty(); }
  // Symétrie et répétitions de la sélection
  void symetriserSelection();
  void transformerSelection(int quoi);   // 0 décaler, 1 déplacer, 2 pivoter, 3 échelle
  void setPolygoneCirconscrit(bool oui) { m_polygoneCirconscrit = oui; }
  // Variables de la pièce, utilisables dans les cotes (« L/2 ») : la cote garde l'expression et suit la variable
  void setVariables(const std::map<std::string, prom::Grandeur>& v) { m_variables = v; }
  const std::map<std::string, prom::Grandeur>& variables() const { return m_variables; }
  void changerCoteTexte(int id, const QString& texte);
  // Tableau des cotes : même lecture que l'éditeur, sans éditeur visible ni changement de focus ; faux et
  // raison si la valeur est refusée (la cote garde alors sa valeur, EI-30)
  bool changerCote(int id, const QString& texte, QString* erreur = nullptr);
  // Saisie de cote en cours : validée (garder) ou abandonnée, avant de terminer, d'annuler ou de quitter
  // l'esquisse (EI-06)
  void terminerSaisie(bool garder);
  // Commandes à panneau (symétrie, réseaux, décaler, déplacer…) : aperçu en orange, sélection pilotée
  void setApercu(const prom::Esquisse* e);
  void setCommandeActive(bool oui) { m_commandeActive = oui; majBarreContexte(); }
  void setSelectionCumulative(bool oui) { m_selectionCumulative = oui; }
  // Diagnostic : éléments et points mis en évidence (ce qui peut bouger, extrémités libres)
  void setSurbrillance(const std::vector<int>& elements, const std::vector<int>& points);
  // Arêtes de la pièce projetées dans le plan (gris pointillé) : un clic de sélection ou de cotation les reprend dans
  // l'esquisse comme géométrie de construction fixe, pour s'y relier ou coter jusqu'à elles
  // Arêtes de la pièce proposées (S3) : « noms » donne, élément par élément, le nom de l'arête (reprise associative ;
  // vide : reprise fixe)
  void setReferencesExternes(const prom::Esquisse& ext, const std::vector<std::string>& noms = {}) {
    m_extTout = ext;
    m_extNomsTout = noms;
    m_externesChoisies.clear();
    majReferencesExternes();
    update();
  }
  int importerExterne(int indice);
  int nombreReferencesExternes() const { return static_cast<int>(m_ext.elements.size()); }
  // Convertir les entités : les arêtes choisies (clic, Ctrl pour en ajouter), sinon toutes celles encore proposées ;
  // rend le nombre d'éléments repris
  int convertirExternes();
  const std::set<int>& externesChoisies() const { return m_externesChoisies; }
  void choisirExterne(int indice, bool ajouter);
  // Bandeau du plan de travail, en haut à gauche de la vue (« Esquisse 2 · Plan XY »)
  void setTitre(const QString& t) { m_titre = t; update(); }
  // Relations à l'écran : 0 toutes, 1 celles de la sélection, 2 aucune
  void setAffichageRelations(int mode) { m_affichageRelations = mode; update(); }
  int affichageRelations() const { return m_affichageRelations; }
  void selectionnerElements(const std::vector<int>& ids);
  int ligneOuAxeSelectionne() const;   // dernière ligne ou axe sélectionné (0 si aucun)
  int pointSelectionne() const;        // dernier point sélectionné (-1 si aucun)
  void supprimerContrainte(int id);
  std::vector<int> contraintesDe(const std::vector<int>& refs) const;   // relations et cotes qui portent sur ces éléments ou points
  QString nomContrainteLisible(int id) const;
  void repeterSelection(bool circulaire);
  // Remplace l'esquisse en une étape d'historique (conversion d'entités, par exemple)
  void appliquer(const prom::Esquisse& e);
  // Calques : sans interface depuis la 0.17 ; ceux des anciens fichiers sont relus visibles (EI-12). Méthodes
  // gardées pour les essais : le calque actif reçoit ce qui est tracé, un calque masqué n'est ni dessiné ni pris
  const std::string& calqueActif() const { return m_calqueActif; }
  void setCalqueActif(const std::string& nom);
  void ajouterCalque(const std::string& nom, const std::string& couleur);
  void supprimerCalque(const std::string& nom);
  void setCalqueVisible(const std::string& nom, bool visible);
  void mettreSelectionSurCalque(const std::string& nom);
  std::vector<int> elementsSelectionnes() const;
  // Cotes de l'esquisse (pour l'éditeur de cotes) et changement de valeur
  std::vector<int> cotes() const;
  void changerValeurCote(int id, double valeur);
  // Ligne de commande (barre d'espace) : « c 50 », « r 40 30 », « d », « h »…
  bool executerCommande(const QString& texte, QString* message = nullptr);
  void ouvrirCommande();
  // Esquisse en contexte : le dessin se superpose à la vue 3D, et passe par sa caméra.
  struct Projection {
    std::function<QPointF(double u, double v)> versEcran;
    std::function<bool(const QPointF& ecran, double& u, double& v)> versEsquisse;
    std::function<void(const QPointF& ecran, double crans)> zoomer;
    std::function<void(double dx, double dy)> tourner;
    std::function<void(double dx, double dy)> deplacer;
    std::function<void()> recadrer;
  };
  void setProjection(std::optional<Projection> p);
  void setUnites(const prom::Unites& u) { m_unites = u; majReglageOutil(); update(); }

signals:
  void modifiee();
  void etatChange(const QString& message, int niveau);  // 0 : contrainte, 1 : libre, 2 : conflit ou erreur
  void outilChange(Esquisseur::Outil o);
  void historiqueChange();
  void selectionAChange();
  void demandeCommande(const QString& commande);
  void demandeTerminer();
  void demandeAnnuler();
  void modeConstructionChange(bool oui);

protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent* e) override;
  void mouseDoubleClickEvent(QMouseEvent* e) override;
  void wheelEvent(QWheelEvent* e) override;
  void keyPressEvent(QKeyEvent* e) override;
  bool focusNextPrevChild(bool suivant) override;
  void leaveEvent(QEvent*) override;
  // Les touches de l'esquisseur passent avant les raccourcis de l'atelier et de la fenêtre (EI-04, EI-05)
  bool event(QEvent* e) override;
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  struct Cible {
    enum Type { Rien, Point, Element, Axe, Contrainte, Externe } type = Rien;
    int id = 0;
    bool operator==(const Cible& o) const { return type == o.type && id == o.id; }
    bool operator!=(const Cible& o) const { return !(*this == o); }
  };
  struct Proposition {
    bool valide = false;
    prom::TypeContrainte type = prom::TypeContrainte::Distance;
    std::vector<int> refs;
    double valeur2 = 0;  // angle : secteur mesuré, choisi par la position de la souris (S2)
  };
  // Secteur d'une cote d'angle entre deux lignes qui contient le point w (repère de l'esquisse) : 0 à 3 (S2)
  int secteurAngle(int ligne1, int ligne2, const QPointF& w) const;

  QPointF ecran(double x, double y) const { return m_proj ? m_proj->versEcran(x, y) : QPointF(m_ox + x * m_s, m_oy - y * m_s); }
  QPointF ecran(const prom::Point& p) const { return ecran(p.x, p.y); }
  QPointF monde(const QPointF& p) const {
    if (m_proj) { double u = 0, v = 0; m_proj->versEsquisse(p, u, v); return {u, v}; }
    return {(p.x() - m_ox) / m_s, (m_oy - p.y()) / m_s};
  }
  void majEchelle() const;
  void cercle(QPainter& g, const prom::Point& c, double r) const;
  prom::Point pt(int id) const;
  Cible toucher(const QPointF& p, bool avecCotes = true) const;
  Cible toucherEsquisse(const QPointF& p, bool avecCotes = true) const;
  int pointSous(const QPointF& p, int exclu = -1) const;
  int obtenirPoint(const QPointF& p);
  // Inférence pendant le tracé d'une ligne (S1) : direction horizontale, verticale, parallèle ou perpendiculaire à
  // une ligne, tangente à l'arc de départ, alignement sur un point existant, longueur égale au trait précédent.
  // Le point d'arrivée est accroché, les relations sont posées au clic (refs : −1 la nouvelle ligne, −2 son point
  // d'arrivée), les lignes d'aide dessinées pendant le tracé. Alt : sans inférence.
  struct Inference {
    bool active = false;
    QPointF fin;  // point d'arrivée accroché (repère de l'esquisse)
    std::vector<std::pair<prom::TypeContrainte, std::vector<int>>> relations;
    std::vector<std::pair<QPointF, QPointF>> guides;  // lignes d'aide (repère de l'esquisse)
    QString symbole;
  };
  Inference inferer(int depart, const QPointF& souris, bool sansInference) const;
  void apresModification(bool signaler = true);
  bool selectionne(const Cible& c) const;
  Proposition proposerCote(const std::vector<Cible>& refs, const QPointF& souris) const;
  void dessinerCote(QPainter& g, const prom::ContrainteEsquisse& c, const QColor& couleur) const;
  QPointF positionEtiquette(const prom::ContrainteEsquisse& c) const;
  QString texteContrainte(const prom::ContrainteEsquisse& c) const;
  void ouvrirEditeur(int idContrainte);
  void validerEditeur();
  // Lecture d'une valeur de cote et application ; faux et raison si elle est refusée
  bool appliquerTexteCote(int id, const std::string& texte, QString* erreur);
  void finirOutil();
  // Points créés par l'outil en cours (premier clic, centre…) : retirés si la forme n'aboutit pas (EI-08)
  void retirerPointsOutil();
  // Points existants cliqués par un outil qui travaille sur des positions : la forme créée y est liée par
  // coïncidence (ou « sur la courbe »), pour qu'elle les suive (EI-09)
  void lierAuxPointsCliques(const std::set<int>& pointsAvant, const std::set<int>& elementsAvant, const std::vector<std::pair<QPointF, int>>& clics);
  // Vrai si l'esquisse diffère de son dernier état enregistré (pas d'étape vide, EI-16)
  bool aChange() const;
  bool toucheDeLEsquisse(const class QKeyEvent* e) const;
  // Saisie dynamique (M2) : pendant le tracé d'une ligne, d'un rectangle ou d'un cercle, les valeurs tapées
  // (longueur puis angle, largeur puis hauteur, diamètre) fixent la forme ; Tab passe au champ suivant, Entrée
  // pose la forme cotée, Échap abandonne la saisie. Unités et expressions comme dans les cotes.
  struct SaisieDynamique {
    bool active = false;
    int champ = 0;
    QString texte[2];
    bool fige[2] = {false, false};
    double valeur[2] = {0, 0};
  };
  SaisieDynamique m_dyn;
  bool saisieDynamiquePossible() const;
  int champsDynamiques() const;
  bool figerChampDynamique(int k);
  QPointF finDynamique() const;  // point d'arrivée (plan de l'esquisse) qui respecte les valeurs tapées
  void validerSaisieDynamique();

  prom::Esquisse m_e;
  prom::EtatEsquisse m_etat;
  mutable double m_s = 6;
  double m_ox = 0, m_oy = 0;
  bool m_cadre = false;
  Outil m_outil = Outil::Selection;
  int m_cotesPolygone = 6;
  std::vector<int> m_clics;
  std::vector<QPointF> m_libres;  // positions cliquées (dans le plan) pour les outils à plusieurs points
  std::vector<int> m_libresPoints;  // pour chaque position libre : point existant cliqué, ou -1
  std::vector<int> m_pointsOutil;   // points créés par l'outil en cours, pas encore utilisés par une forme
  std::string m_texteInitialEditeur;  // texte montré à l'ouverture de l'éditeur de cote (EI-07)
  std::vector<Cible> m_coteRefs;
  std::vector<Cible> m_selection;
  bool cibleExiste(const Cible& c) const;
  void purgerReferences();
  Cible m_survol;
  QPointF m_souris;
  enum class Geste { Aucun, Points, Vue, Cadre, Etiquette } m_geste = Geste::Aucun;
  std::vector<std::pair<int, prom::Point>> m_departPoints;
  int m_etiquette = 0;
  QPointF m_depart, m_souris0;
  Qt::MouseButton m_vueBouton = Qt::NoButton;
  double m_oxDepart = 0, m_oyDepart = 0;
  bool m_bouge = false;
  QLineEdit* m_editeur = nullptr;
  int m_coteEditee = 0;
  mutable std::vector<std::pair<QRectF, int>> m_zones;
  std::vector<prom::Esquisse> m_annulations, m_retablissements;
  prom::Esquisse m_stable;
  bool m_modeConstruction = false;
  bool m_polygoneCirconscrit = false;
  double m_largeurRainureArc = 4;
  double m_distanceChanfrein = 2;
  class QFrame* m_reglageOutil = nullptr;     // réglage de l'outil posé sur la vue (U1)
  class QLabel* m_libelleReglage = nullptr;
  class ChampValeur* m_champReglage = nullptr;
  void majReglageOutil();
  bool m_commandeActive = false;
  bool m_selectionCumulative = false;
  std::vector<int> m_surbrillanceElements, m_surbrillancePoints;
  QString m_titre;
  prom::Esquisse m_ext;      // arêtes de la pièce encore proposées (sans copie dans l'esquisse)
  prom::Esquisse m_extTout;  // toutes les arêtes de la pièce projetées dans le plan
  std::vector<std::string> m_extNomsTout, m_extNoms;  // noms des arêtes (alignés sur les éléments)
  std::set<int> m_externesChoisies;                    // indices dans m_ext
  int importerElementExterne(const prom::ElementEsquisse& src, const prom::Esquisse& source, const std::string& nom);
  // Arêtes proposées = toutes, moins celles déjà reprises : recalculé après chaque changement, annulation
  // comprise (EI-26)
  void majReferencesExternes();
  bool coteRedondante(const prom::Esquisse& avecCote, int id, bool resolvaitAvant) const;
  int m_affichageRelations = 0;
  std::optional<prom::Esquisse> m_apercu;
  std::map<std::string, prom::Grandeur> m_variables;  // variables de la pièce, avec leur dimension
  std::string m_calqueActif;
  bool calqueVisible(const std::string& nom) const;
  QColor couleurCalque(const std::string& nom) const;
  class QFrame* m_barreContexte = nullptr;
  void majBarreContexte();
  void coterSelection();
  QLineEdit* m_commande = nullptr;
  QPointF m_dernierClic;
  int m_dernierClicPoint = -1;  // point existant sous le dernier clic (base des commandes « c 50 »…, EI-19)
  int m_arcPrecedent = 0;  // arc tangent enchaîné : l'arc suivant lui est tangent
  bool m_aDernierClic = false;
  // Menu radial (clic droit maintenu)
  class QTimer* m_minuterieRadial = nullptr;
  bool m_radialVisible = false;
  QPointF m_centreRadial;
  int m_secteur = -1;
  void dessinerRadial(QPainter& g);
  int secteurSous(const QPointF& p) const;
  void dessinerInspecteur(QPainter& g);
  int coinProche(const QPointF& ecranPos) const;
  std::optional<Projection> m_proj;
  prom::Unites m_unites;
};
