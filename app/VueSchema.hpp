// Prométhée : canevas de schéma, socle commun aux domaines (A16) et éditeur du schéma électronique. Dessin de la
// feuille et de son cartouche, des symboles, fils, jonctions, étiquettes, alimentations et marques de
// non-connexion ; outils en classes (OutilsSchema.cpp) ; sélection partagée ; surlignage d'un réseau ; saisie sur
// place. Chaque modification passe par le document (annulable). Le domaine électrique (folios de l'électricien)
// est une classe à part, VueSchemaElectrique, qui ne remplace que ses points d'extension.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <QPixmap>
#include <QPointer>
#include <QWidget>

#include "OutilVue.hpp"
#include "promethee/electrique.hpp"
#include "promethee/electronique.hpp"

class AppuiLong;
class CadreVue;
class NavigationVue;
class BandeauOutil;
class Document;
class SelectionElectronique;
namespace prom {
struct Projet;
}

class VueSchema : public QWidget {
  Q_OBJECT
 public:
  enum class Outil { Selection, Fil, Etiquette, EtiquetteGlobale, Alimentation, Jonction, NonConnexion, Texte, Placer, Gomme };
  explicit VueSchema(Document* doc, QWidget* parent = nullptr);
  // Ce que montre la vue : une carte électronique (p.electronique) ou un projet électrique (VueSchemaElectrique)
  enum class Domaine { Electronique, Electrique };
  virtual Domaine domaine() const { return Domaine::Electronique; }
  // Pose confiée à l'atelier (appareil repéré, contact rattaché à sa bobine, borne de bornier…)
  void setPoseur(std::function<void(prom::ecad::Pt, int rotation)> f) { m_poseur = std::move(f); }
  void setConception(int conception, int feuille);
  int feuille() const { return m_feuille; }
  // Renvois des connecteurs hors feuille dessinés à la dernière image (2.20) : zone à l'écran, feuille ouverte d'un clic
  std::vector<std::pair<QRectF, int>> zonesRenvoi() const {
    std::vector<std::pair<QRectF, int>> r;
    for (const auto& z : m_zonesRenvoi) r.push_back({z.zone, z.feuille});
    return r;
  }
  void setConnectivite(const prom::ecad::Connectivite* k) {
    m_k = k;
    invaliderCache();
  }
  void setViolations(const std::vector<prom::ecad::Violation>* v) {
    m_violations = v;
    invaliderCache();
  }
  // ERC à trois niveaux (2.11, S9) : des violations données par setViolations, seules les immédiates prennent une
  // marque de couleur et celles du dessin un petit cercle gris ; celles du niveau complet viennent de la dernière
  // vérification (« Vérifier », F7), hachurées quand le schéma a changé depuis. Sans cet appel, toutes les violations
  // prennent leur marque (atelier électrique).
  void setVerification(const std::vector<prom::ecad::Violation>* complete, bool perimee) {
    m_niveauxErc = true;
    m_verification = complete;
    m_verificationPerimee = perimee;
    invaliderCache();
  }
  // Les marques de la feuille montrée, telles que la vue les peint et les explique au survol (S28 ; essais)
  enum class GenreMarque : std::uint8_t { Erreur, Avertissement, Dessin };
  struct MarqueErc {
    QPointF p;
    GenreMarque genre;
    bool perimee = false;
    const prom::ecad::Violation* violation = nullptr;
  };
  std::vector<MarqueErc> marquesErc() const;
  // Le dessin fixe est gardé en image (2.4, A10) : la souris ne redessine que le survol et l'outil armé. Ce qui
  // change le dessin sans changer le document (résultats de calcul, renvois, réglages d'affichage) l'invalide.
  void invaliderCache() {
    ++m_invalidations;
    update();
  }
  const std::string& objetSurvole() const { return m_survol; }  // essais
  std::string objetVise(QPointF q) const { return objetSous(q); }  // essais : l'objet qu'un clic ici prendrait
  // Menu contextuel (2.5, SAI-20, SAI-13, S7) : clic droit court sur un objet, une broche ou le vide ; avec un outil
  // armé, le menu de l'outil (Terminer, Annuler et ses boutons)
  void menuContextuel(QPointF q);
  // Champs d'un symbole (2.6, SAI-14) : le repère ou la valeur sous q ; édition sur place, Tab passe à l'autre,
  // Entrée valide ; F2 édite le texte de l'objet choisi (champ, étiquette, alimentation, texte libre)
  struct ChampVise {
    std::string symbole;
    bool valeur = false;
  };
  std::optional<ChampVise> champSous(QPointF q) const;
  QRectF zoneChamp(const std::string& symbole, bool valeur) const;  // à l'écran ; vide si le champ n'est pas dessiné
  void editerChamp(const std::string& symbole, bool valeur);
  bool modifierTexteChoisi();
  QString texteSurvol(QPointF q) const;  // infobulle de ce qui est sous q (SAI-19, S28)
  // Description d'un objet écrite une fois (2.30, DescriptionObjet.cpp) : l'infobulle et le lecteur d'écran la
  // lisent ; celle de l'objet tenu ; le nom du canevas ; le passage à l'objet relié suivant (Alt+→)
  QString descriptionObjet(const std::string& id) const;
  QString descriptionTenue() const;
  QString nomAccessible() const;
  bool objetRelieSuivant();
  // Arme un outil (1.6). Un outil armé vide la sélection (S11) ; la pose d'un composant est à un coup (S2).
  // Pendant un fil, un autre outil le suspend : sa commande faite, ou Échap, le fil reprend au dernier point fixé
  // (ERG-21, 1.17)
  void setOutil(Outil o, const std::string& parametre = {});
  bool traceSuspendu() const { return m_outils.suspendus(); }
  // Alt+clic ou appui long immobile (SAI-18) : la liste des objets sous le curseur, où l'on choisit le bon
  void listerObjetsSous(QPointF q);
  QString libelleObjet(const std::string& id) const;  // « Symbole R1 », « Fil GND », « Étiquette VCC »
  Outil outil() const;
  // Pose d'un composant dont la définition n'est pas encore dans le projet (bibliothèque personnelle) : elle y entre
  // avec la pose, en un seul pas d'annulation (S4)
  void armerPose(const prom::ecad::DefSymbole& d);
  // Pose d'un article réel (L4) : son symbole armé, l'article installé dans le projet avec la pose
  void armerArticle(const prom::ecad::Article& a);
  // Pose en série : l'outil reste armé, un clic par exemplaire, jusqu'à « Terminer » (bouton du bandeau)
  void setPoseEnSerie(bool oui);
  bool poseEnSerie() const;
  std::string symboleArme() const;  // article de la pose en cours (vide hors pose)
  // Glisser-déposer d'un article (2.2, SAI-01) : la pose s'arme quand le pointeur arrive sur la feuille, le fantôme le
  // suit, R, X et Y le tournent ou le retournent ; le lâcher pose, comme un clic. Sert au glisser de la bibliothèque
  // (GlisserArticle) et aux articles venus d'une autre instance (type MIME).
  void glisserArticle(const prom::ecad::DefSymbole& d, QPointF q);
  bool deposerArticle(QPointF q);  // vrai si l'article est posé
  void abandonnerArticle();        // le pointeur a quitté la feuille : la pose armée par le glisser est rendue
  // Plusieurs exemplaires (2.3, SAI-03) : Inser repose le dernier composant posé, même article, même valeur, même
  // orientation, décalé du dernier écart constaté entre deux poses de cet article (5,08 mm à droite à défaut), au
  // repère suivant ; « Poser une copie » arme la pose du composant choisi, avec sa valeur et son orientation
  bool repeterPose();
  bool poserCopie();
  // Repère donné à la pose (SAI-05, S15), réglable (Réglages) ; sinon « R? » jusqu'à l'annotation
  static bool annotationALaPose();
  static void setAnnotationALaPose(bool oui);
  // Coordonnées, réticule, grilles et saisie près du curseur (2.28, CoordonneesSchema.cpp) : le texte de la barre
  // d'état (point visé, et sa place relative à l'origine posée d'un clic ou au dernier point du tracé, en
  // cartésien ou en polaire) ; le réticule (petit, sur toute la vue, aucun) ; les grilles nommées à pas X et Y, la
  // grille active des fils et symboles, celle des textes, deux grilles rapides ; la longueur et l'angle tapés
  // pendant un tracé de fil
  QString texteCoordonnees() const;
  static QString texteRelatif(prom::ecad::Pt o, prom::ecad::Pt p);
  void armerOrigineRelative();
  void effacerOrigineRelative();
  bool origineRelativeArmee() const { return m_armeOrigine; }
  std::optional<prom::ecad::Pt> origineRelative() const { return m_origineRelative; }
  static bool coordonneesPolaires();
  static void setCoordonneesPolaires(bool oui);
  enum class Reticule { Petit, Grand, Aucun };
  static Reticule styleReticule();
  static void setStyleReticule(Reticule r);
  struct GrilleNommee {
    QString nom;
    double pasX = 1.27, pasY = 1.27;  // mm
  };
  static std::vector<GrilleNommee> grillesNommees();
  static void setGrillesNommees(const std::vector<GrilleNommee>& g);
  static GrilleNommee grilleNommee(const QString& nom);
  GrilleNommee grilleActive() const;
  GrilleNommee grilleTextes() const;
  void choisirGrille(const QString& nom);
  void choisirGrilleTextes(const QString& nom);
  static QString grilleRapide(int i);  // 0 ou 1
  static void setGrilleRapide(int i, const QString& nom);
  prom::ecad::Pt surGrilleActive(prom::ecad::Pt p) const;
  prom::ecad::Pt surGrilleTextes(prom::ecad::Pt p) const;
  void ajouterGrillesBandeau(struct ContenuBandeau& c);
  void dialogueGrilles();
  void ouvrirSaisieTrace(const QString& premier);
  class QFrame* saisieTrace() const;
  std::optional<prom::ecad::Pt> dernierPointTrace() const;  // OutilsSchema.cpp
  bool poserPointTrace(prom::ecad::Pt p);                    // OutilsSchema.cpp
  // Saut aux croisements de fils sans jonction (2.9, SAI-10), réglable
  static bool sautsAuxCroisements();
  static void setSautsAuxCroisements(bool oui);
  // Étiquette posée d'office au bout d'un fil laissé ouvert (2.21, SAI-13), réglable au bandeau du fil : son nom
  // demandé sur place, la série suivie
  static bool etiquetteAuBoutOuvert();
  static void setEtiquetteAuBoutOuvert(bool oui);
  void etiquetterBout(prom::ecad::Pt bout, prom::ecad::Pt avant);
  // Échap : la saisie sur place, puis le geste en cours, sinon l'état neutre (charte)
  void echap();
  BandeauOutil* bandeau() const { return m_bandeau; }
  // Calcul de fond qui dure (DRC, zones) : flèche avec sablier sur la vue, la saisie reste possible
  void setCalculLong(bool oui);
  // Sélection partagée avec le circuit et l'inspecteur (électronique) ; sans elle, la vue garde la sienne
  void setSelectionPartagee(SelectionElectronique* s);
  SelectionElectronique* selectionPartagee() const { return m_sel; }
  void setReseauSurligne(const std::string& nom);
  const std::string& reseauSurligne() const;
  void recadrer();  // le dessin (électricien : le folio entier)
  void ajusterAuDessin();  // le dessin du folio, aussi grand que possible
  void zoomer(double facteur);  // autour du centre de la vue
  double echelle() const { return m_echelle; }  // pixels par millimètre
  // Barres de défilement et vue d'ensemble (2.12, SAI-25, S14), au schéma électronique ; nul ailleurs
  CadreVue* cadre() const { return m_cadre; }
  // Grille de points qui suit l'accroche (S27) : 1,27 mm à partir de 8 px/mm, 2,54 mm en dessous, aucune sous
  // 2 px/mm ; rend le pas montré (mm), 0 sans grille
  double pasGrille() const;
  void zoomerSurSelection();  // menu à marques, vers le haut : les objets choisis cadrés, sinon tout (1.19)
  // Cadrages nommés (2.29, SAI-25) : la feuille, l'origine et l'échelle gardées sous un nom, par conception
  void enregistrerCadrage(const QString& nom);
  bool rappelerCadrage(const QString& nom);
  QStringList cadrages() const;
  void deplacerVue(QPointF origine, double echelle);  // essais et souris 3D
  QPointF origineVue() const { return m_origine; }
  bool menuMarquesVisible() const;  // essais
  QString annonceClavier() const;    // essais : l'annonce près du curseur (suite en attente, bascule)
  void centrerSur(prom::ecad::Pt p);
  // Objets choisis sur la feuille montrée, symboles des composants choisis depuis le circuit compris
  const std::set<std::string>& selection() const { return m_selection; }
  void selectionner(const std::set<std::string>& s);
  void toutChoisir();  // Ctrl+A : tout ce qui est sur la feuille (S23)
  // Sélections avancées (2.26, SelectionAvancee.cpp) : objets dans un polygone ou touchés par un trait ; tous les
  // objets de la feuille ; des identifiants appliqués selon le mode (filtre et masque respectés) ; sélection
  // inversée ; « Étendre » d'un palier de connectivité et « Réduire » ; « Choisir les semblables » sur des
  // propriétés, à la feuille ou au projet ; opacité d'un objet que l'estompage atteint
  enum Semblable : unsigned { SemblableArticle = 1, SemblableValeur = 2, SemblableEmpreinte = 4, SemblableFabricant = 8 };
  std::vector<std::string> objetsDansForme(const prom::ecad::Polygone& forme, bool trait) const;
  std::vector<std::string> tousLesObjets() const;
  void appliquerIds(const std::vector<std::string>& ids, int mode);
  void inverserSelection();
  bool etendreSelection();
  bool reduireSelection();
  int choisirSemblables(unsigned proprietes, bool projet);
  void dialogueSemblables();
  double opaciteObjet(const std::string& id) const;
  // Huit mémoires de sélection (ERG-29) : un petit panneau sur le canevas, montré ou caché
  void basculerMemoires();
  class PanneauMemoires* memoires() const { return m_memoires; }
  // « Relier à… » depuis une broche (2.18, SAI-30) : le clic suivant sur une autre broche les relie par un fil qui
  // contourne les symboles ; Échap renonce
  void relierDepuis(const std::string& composant, const std::string& numero);
  bool relierEnCours() const { return !m_relierDepuis.empty(); }
  // Flèches : la sélection d'un pas de grille, de dix avec Maj ; les appuis rapprochés font un seul pas (C18)
  void deplacerSelection(prom::ecad::Pt delta);
  void tournerSelection(bool inverse = false);  // R ; Maj+R dans l'autre sens, champs replacés (2.23)
  // Commandes de la feuille et de la sélection, chacune en un pas d'annulation (2.19 à 2.25) :
  // croisements à quatre branches décalés en deux T sur la sélection (0), la feuille (1) ou tout le schéma (2) ;
  // broches libres des composants choisis terminées par une étiquette à leur nom ou une marque de non-connexion ;
  // repère et valeur replacés autour du corps ; un champ bloqué (ou débloqué) ; alignement et répartition sur la
  // grille, fils élastiques ; deux symboles de même genre échangés. Chacune rend le nombre d'objets changés.
  int decalerCroisementsSur(int portee);
  int terminerBrochesChoisies(bool etiquettes);
  int replacerChampsChoisis();
  void bloquerChamp(const std::string& symbole, bool valeur, bool bloque);
  int alignerChoisis(prom::ecad::Alignement a);
  bool echangerChoisis();
  void sousMenuCroisements(class QMenu* menu);
  void miroirSelection(bool hautBas = false);  // X : gauche-droite, Y : haut-bas (EL-24)
  // Action du registre (RegistreActions, 1.8) : l'outil armé d'abord, puis la vue ; faux si aucun ne la fait.
  // Sert aux touches, aux raccourcis que l'atelier prend hors du canevas (S21) et aux boutons (1.16)
  bool executerAction(std::string_view action);
  // L'action se ferait-elle maintenant (avec l'outil armé) ; le canevas ne garde que ces touches (S22)
  bool actionPossible(std::string_view action);
  bool outilPrend(std::string_view action);  // l'outil armé la fait (liste F1 : « Outil en cours »)
  QString nomOutil() const;                  // « Fil », « Pose : Résistance », « Sélection »
  void supprimerSelection();
  // Presse-papiers (EL-22) : copier, couper, coller sous la souris, dupliquer à côté
  void copier();
  void couper();
  void coller();
  void dupliquer();
  bool peutColler() const;
  // Collages (2.24, CollageSchema.cpp) : en matrice (lignes × colonnes au pas donné, numérotée d'abord en ligne ou
  // d'abord en colonne), spécial (repères gardés, libres ou effacés), copie en texte ; tampon : le collage qui suit
  // la souris reste armé après chaque clic, jusqu'à Échap
  std::set<std::string> collerMatrice(int lignes, int colonnes, prom::ecad::Pt pas, bool ligneDabord);
  void dialogueMatrice();
  bool collageSpecial(prom::ecad::ReperesCollage reperes);
  void dialogueCollageSpecial();
  QString texteSelection() const;
  void copierCommeTexte();
  bool tampon() const { return m_tampon; }
  void setTampon(bool oui) { m_tampon = oui; }
  // Ce que Ctrl+V collerait : le presse-papiers du système s'il porte un bloc de schéma (un autre projet, une autre
  // fenêtre de Prométhée, 2.13, SAI-27), sinon celui de la session
  prom::ecad::PressePapiersSchema presseCourant() const;
  static constexpr const char* MIME_SCHEMA = "application/x-promethee-schema";
  void modifierTexte(const std::string& uuid);  // contenu d'un texte de la feuille (EL-21)
  // Saisie sur place (texte, nom de potentiel ou de renvoi) : un champ posé sur le dessin à côté du point, Entrée
  // ou un clic ailleurs valide, Échap renonce ; aucune fenêtre ne s'ouvre
  void saisirSurPlace(prom::ecad::Pt p, const QString& invite, const QString& valeur, std::function<void(const QString&)> valider);
  class QLineEdit* saisieEnCours() const;  // essais
  void equiperIncrement(class QLineEdit* e);  // flèches qui incrémentent la partie numérique (2.22)
  // Clic simulé (essais) : position en coordonnées de la feuille
  void cliquerEn(prom::ecad::Pt p, bool double_ = false);
  QPointF versEcran(prom::ecad::Pt p) const { return ecran(p); }  // essais : où cliquer pour atteindre ce point
  // Dessin électronique : toutes les feuilles, une page chacune, au format de la feuille, avec le cartouche
  bool exporterPdf(const QString& chemin);
  // Un folio en DXF (R12, millimètres) : le dessin même du PDF, calques CADRE, FILS, NUMEROS, SYMBOLES, REPERES,
  // RENVOIS, ETIQUETTES, TEXTES ; rend le nombre d'entités écrites (−1 : échec)
  int exporterDxf(const QString& chemin, int feuille);

  // Dessin d'un symbole, aussi pour les aperçus de la bibliothèque
  struct Repere {
    QPointF origine;
    double echelle = 4;  // pixels par millimètre
    QPointF ecran(prom::ecad::Pt p) const { return origine + QPointF(prom::ecad::mm(p.x) * echelle, prom::ecad::mm(p.y) * echelle); }
  };
  static void dessinerSymbole(class QPainter& g, const Repere& r, const prom::ecad::DefSymbole& d, const prom::ecad::SymbolePlace& s, const QString& reference,
                              const QString& valeur, bool fantome = false);
  // Aperçu d'un symbole seul, centré et à l'échelle (bibliothèque, création de composant)
  static QPixmap apercu(const prom::ecad::DefSymbole& d, QSize taille);

 signals:
  void coordonneesChangees();  // le point visé ou l'origine relative ont changé (2.28)
  void selectionChangee();
  void message(const QString& html);
  void proprietesDemandees();
  void inspecteurDemande();  // E : l'inspecteur, curseur dans son premier champ
  void actionDemandee(const QString& action);  // une ligne d'un menu ouvert au clavier : l'atelier fait l'action
  void bibliothequeDemandee();
  void outilChange();
  void vueChangee();  // échelle, origine ou feuille montrée ont changé (indicateur de zoom)
  // Un composant vient d'être posé (pose à un coup) : il est choisi, l'inspecteur le montre (S4, SAI-02)
  void composantPose(const QString& symbole);
  // Une proposition qu'un clic sur la bulle accepte (« Insérer dans le fil », 2.3)
  void proposition(const QString& html, std::function<void()> faire);
  void circuitDemande(const QString& composant);  // « Montrer sur le circuit » (menu contextuel)

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  bool event(QEvent* e) override;
  bool focusNextPrevChild(bool suivant) override;
  void tabletEvent(class QTabletEvent* e) override;  // stylet (2.29) : le bouton du fût ouvre le menu
  void resizeEvent(QResizeEvent* e) override;
  // Le canevas prend le focus quand la souris y entre, sauf pendant une saisie ; il le montre par un liseré (S21)
  void enterEvent(QEnterEvent* e) override;
  void focusInEvent(QFocusEvent* e) override;
  void focusOutEvent(QFocusEvent* e) override;
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dragMoveEvent(QDragMoveEvent* e) override;
  void dragLeaveEvent(QDragLeaveEvent* e) override;
  void dropEvent(QDropEvent* e) override;

 protected:
  // Points d'extension d'une classe de domaine (A16) ; le socle sert le schéma électronique
  virtual const prom::ecad::ConceptionElec* conception() const;
  virtual prom::ecad::ConceptionElec* cible(prom::Projet& p) const;  // conception à modifier dans le projet
  // Une modification, un pas d'annulation : de la seule conception (électronique, A2), sinon du projet
  virtual void modifier(const std::function<void(prom::ecad::Feuille&, prom::ecad::ConceptionElec&)>& f, const QString& fusion = {}, const QString& nom = {});
  virtual void commencerGeste();  // glisser : un geste sur la conception, ou sur le projet
  virtual void dessinerFond(class QPainter& g, const prom::ecad::Feuille& f, const prom::ecad::ConceptionElec& c);  // cadre et cartouche
  virtual void dessinerApresFils(class QPainter&, const prom::ecad::Feuille&) {}
  virtual void dessinerApresSymboles(class QPainter&, const prom::ecad::Feuille&) {}
  // Repère et valeur écrits à côté d'un symbole posé ; « ref » et « val » arrivent avec ceux du schéma électronique
  virtual void textesSymbole(const prom::ecad::Feuille&, const prom::ecad::SymbolePlace&, const prom::ecad::DefSymbole&, const prom::ecad::ComposantElec*, QString&, QString&) const {}
  virtual QString repereFantome(const prom::ecad::DefSymbole& d) const;  // repère du fantôme d'une pose (« R? »)
  virtual bool texteAncreAGauche() const { return false; }               // texte écrit depuis son point (électricien)
  virtual bool recadrerSurDessin() const { return true; }                // Origine cadre le dessin, sinon la feuille
  virtual int margeBas() const { return 0; }                             // place gardée en bas par des commandes de vue
  virtual QString aideOutil(Outil) const { return {}; }                  // aide de l'outil armé, s'il n'y a pas de bandeau
  virtual bool avecBandeau() const { return true; }
  virtual bool poseUnCoup() const { return true; }                       // la pose d'un composant rend la main (S2)
  virtual std::set<std::string> collerDans(const prom::ecad::PressePapiersSchema& pp, prom::ecad::Pt ou);
  virtual void armerAlimentation();                                      // touche P

  // Socle commun : document, conception, feuille, navigation, résultats des moteurs
  const prom::ecad::DefSymbole* definition(const std::string& id) const;  // projet, puis bibliothèques
  const prom::ecad::Feuille* feuilleCourante() const;
  bool refuseVerrou();  // conception publiée : rien ne se modifie, un message le dit
  QPointF ecran(prom::ecad::Pt p) const;
  prom::ecad::Pt monde(QPointF q) const;
  void cadrer(bool dessin);
  Document* m_doc;
  int m_conception = -1, m_feuille = 0;
  const prom::ecad::Connectivite* m_k = nullptr;
  bool m_impression = false;
  std::uint64_t m_generation = 0;  // change à chaque modification du projet (caches des classes de domaine)
  double m_echelle = 3;
  QPointF m_origine{20, 20};

 private:
  class OutilBase;
  class OutilSelection;
  class OutilPose;
  class OutilFil;
  class OutilEtiquette;
  class OutilPoint;
  class OutilTexte;
  class OutilGomme;
  std::unique_ptr<OutilVue> creerOutil(Outil o, const std::string& parametre);  // OutilsSchema.cpp
  bool agirVue(std::string_view action, bool faire);  // actions de la vue elle-même (outils à armer, vue, presse-papiers)
  bool m_astuceF = false;                             // « Tout voir : touche Origine » déjà dit (C13)
  GesteVue geste(class QMouseEvent* e) const;
  void majBandeau();
  void appliquerCurseur();
  void reprendreTrace();  // la commande imbriquée finie : le fil suspendu reprend (ERG-21, 1.17)
  void executerGeste(const std::string& action);  // commande du menu à marques (1.19)
  // Répartiteur des touches (ERG-03) : menus au curseur, outil, premier appui au curseur, vue ; bascules annoncées
  bool toucheAction(const std::vector<const struct ActionAtelier*>& actions);
  bool agirAuCurseur(std::string_view action);
  void annoncerBascule(const struct ActionAtelier& a);
  void ouvrirMenuTouche(const struct ActionAtelier& a);
  AppuiLong* m_appuiLong = nullptr;  // appui long immobile de l'outil Sélection : la liste des objets dessous (SAI-18)
  bool m_calculLong = false;
  bool m_poseGlissee = false;  // la pose armée vient d'un glisser d'article (2.2)
  int m_modeFil = 0;           // fil : équerre, 45°, libre, courbe (gardé d'un fil à l'autre)
  prom::ecad::Nm m_rayonFil = 2'540'000;  // rayon des coins en mode courbe
  bool m_coudeFil = true;      // coude horizontal d'abord
  // Dernière pose d'un composant (Inser, 2.3) : le symbole posé, et l'écart constaté entre les deux dernières poses
  // du même article
  void noterPose(const std::string& symbole);
  std::string m_dernierePose;
  std::optional<prom::ecad::Pt> m_ecartPose;
  prom::ecad::Pt accroche(QPointF q) const;
  // Le premier objet sous le point que le filtre de sélection admet (SAI-18) ; objetsSous : tous, du plus précis
  // au plus étendu (jonction, non-connexion, étiquette, texte, symboles du plus petit au plus grand, fils)
  std::string objetSous(QPointF q) const;
  std::vector<std::string> objetsSous(QPointF q) const;
  std::vector<std::string> objetsDansCadre(prom::ecad::Pt a, prom::ecad::Pt b, bool touches) const;
  bool majSelectionVue();  // m_selection d'après la sélection partagée ; vrai si elle a changé
  void cliquerSelection(QPointF q, Qt::KeyboardModifiers mods);  // clic de l'outil Sélection
  void choisirReseauDuFil(const std::string& fil);               // double-clic sur un fil
  void preparerDeplacement();                                    // la feuille et la sélection au départ du glisser
  // Les objets choisis, de d depuis leur départ : la feuille de départ, puis le déplacement à fils élastiques du
  // noyau (2.8, S16, S17) ; décrochés avec M
  void decaler(prom::ecad::ConceptionElec& cc, int feuille, prom::ecad::Pt d) const;
  std::string reseauDe(const std::string& objet) const;  // réseau d'un fil ou d'une étiquette, sinon vide
  std::vector<prom::ecad::Pt> pointsConnexion() const;
  bool estPointConnexion(prom::ecad::Pt p, bool interieurFil) const;

  void dessinerFils(class QPainter& g, const prom::ecad::Feuille& f);
  // Broche sous la souris (actions depuis une broche, SAI-13) ; textes dessinés d'un symbole ; places de ses champs
  struct BrocheVisee {
    std::string symbole, composant, numero, nom;
    prom::ecad::Pt p;
    int direction = 0;
  };
  std::optional<BrocheVisee> brocheSous(QPointF q) const;
  // Fil commencé par l'outil Sélection sur le bout d'une broche libre (2.18, SAI-29) : aux gestes du Fil, sans
  // l'armer ; fini, la main revient à Sélection
  std::optional<BrocheVisee> brocheLibreSous(QPointF q) const;  // à moins de 8 px du bout, rien n'y est relié
  void commencerFilDepuis(const BrocheVisee& b);
  void finFilDepuisSelection();
  bool m_filDepuisSelection = false;
  // « Relier à… » (2.18) : la broche de départ (« U1:3 ») ; le clic suivant sur une broche trace le fil
  std::string m_relierDepuis;
  bool relierVers(QPointF q);
  // Renommer un réseau sur place (2.20) : portée 0 ce tronçon, 1 la feuille, 2 toutes les feuilles
  void renommerReseauSurPlace(const std::string& ancien, prom::ecad::Pt ou, int portee);
  std::string m_renommageConfirme;  // nom pris, annoncé une fois : la seconde Entrée relie les deux réseaux
  void renvoyerFil(const std::string& fil, const std::string& reseau, prom::ecad::Pt ou);
  // Renvois des connecteurs hors feuille dessinés (2.20) : leur zone à l'écran, la feuille qu'ils ouvrent
  struct ZoneRenvoi {
    QRectF zone;
    int feuille = -1;
    std::string nom;
  };
  std::vector<ZoneRenvoi> m_zonesRenvoi;
  bool ouvrirRenvoi(QPointF q);  // un clic sur un renvoi : la feuille partenaire, le connecteur choisi
  void textesDe(const prom::ecad::Feuille& f, const prom::ecad::SymbolePlace& s, const prom::ecad::DefSymbole& d, QString& ref, QString& val) const;
  struct PlacesChamps {
    prom::ecad::Pt ref, val;
    int alRef = 0, alVal = 0;
  };
  static PlacesChamps placesChamps(const prom::ecad::DefSymbole& d, const prom::ecad::SymbolePlace& s, const QString& reference, const QString& valeur);
  void menuOutil(QPointF q);
  std::optional<ChampVise> m_champGlisse;  // champ pris à la souris (2.6)
  // Survol et image du dessin fixe (2.4)
  struct CleCache {
    std::uint64_t generation = ~std::uint64_t{0}, selection = 0, invalidations = 0;
    int theme = -1, conception = -1, feuille = -1;
    double echelle = 0;
    QPointF origine;
    QSize taille;
    qreal dpr = 0;
    std::string reseau;
    bool focus = false;
    friend bool operator==(const CleCache&, const CleCache&) = default;
  };
  CleCache cleCache() const;
  void dessinerSurvol(class QPainter& g);
  QRect zoneSurvol(const std::string& objet) const;  // à l'écran, ce que couvre le contour de survol d'un objet
  QPixmap m_cache;
  CleCache m_cleCache;
  std::uint64_t m_generationSel = 0, m_invalidations = 0;
  std::string m_survol;  // objet sous la souris, avec l'outil Sélection : un contour fin (S6)
  void dessinerSurlignage(class QPainter& g, const prom::ecad::Feuille& f, const prom::ecad::ConceptionElec& c);
  void peindre(class QPainter& g);

  std::function<void(prom::ecad::Pt, int)> m_poseur;
  const std::vector<prom::ecad::Violation>* m_violations = nullptr;
  CadreVue* m_cadre = nullptr;
  // Ce que le cadre et l'indicateur de zoom ont vu en dernier : ils ne se refont que si la vue a changé
  QPointF m_origineVue;
  double m_echelleVue = 0;
  QSize m_tailleVue;
  int m_feuilleVue = -2;
  void majCadre();
  bool collerSousLaSouris(const prom::ecad::PressePapiersSchema& pp, std::optional<prom::ecad::ReperesCollage> reperes = std::nullopt);  // OutilsSchema.cpp
  const std::vector<prom::ecad::Violation>* m_verification = nullptr;  // niveau complet, au dernier « Vérifier »
  bool m_niveauxErc = false, m_verificationPerimee = false;
  GestionnaireOutils m_outils;
  BandeauOutil* m_bandeau = nullptr;
  std::string m_derniereEtiquette;  // dernier nom posé de la série en cours (vide : aucune, S20)
  // Réglages de l'outil Étiquette gardés d'une pose à l'autre (2.22) : le nom posé au prochain clic, le pas de la
  // série ; et les noms d'une liste collée, posés un par clic (2.20)
  std::string m_prochaineEtiquette;
  int m_pasEtiquette = 1;
  bool m_tampon = false;  // collage qui reste armé (2.24)
  std::optional<prom::ecad::Pt> m_origineRelative;  // 2.28
  bool m_armeOrigine = false;
  QString m_relatifFige;  // le segment tout juste posé par la saisie près du curseur, jusqu'au prochain mouvement
  QPointer<class QFrame> m_saisieTrace;
  QString m_grille, m_grilleTextes;
  class PanneauMemoires* m_memoires = nullptr;
  std::vector<std::string> m_nomsColles;
  std::string m_derniereAlim, m_styleAlim = "GND";  // alimentation : dernier nom et style posés (P les reprend)
  std::string m_reseau;  // réseau surligné dessiné (celui de la sélection partagée, vide à l'impression)
  SelectionElectronique* m_sel = nullptr;
  SelectionElectronique* m_selPropre = nullptr;  // celle de la vue, sans sélection partagée (atelier électrique)
  bool m_choixLocal = false;                     // selectionner() en cours : un seul signal selectionChangee
  std::set<std::string> m_selection;
  QPointF m_souris, m_pression;
  NavigationVue* m_nav = nullptr;  // molette, pavé, boutons du milieu et de droite, menu à marques (1.19)
  // Déplacement de la sélection (glisser, flèches) : la feuille et la sélection au départ ; fils élastiques (G) ou
  // décrochés (M)
  prom::ecad::Pt m_departGlisse;
  std::optional<prom::ecad::Feuille> m_feuilleDepart;
  std::set<std::string> m_selDepart;
  bool m_elastique = true;
  QPointer<class QLineEdit> m_saisie;
};
