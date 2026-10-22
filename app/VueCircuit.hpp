// Prométhée : éditeur de circuit imprimé (canevas). Contour de carte, cuivre par couche (zones remplies, pistes,
// pastilles, vias), sérigraphie, encombrements, chevelu et marques de la DRC ; placement des empreintes (glisser,
// tourner, retourner sur l'autre face), routage interactif à 45° avec vias et vérification d'isolement en direct
// (la piste qui heurterait un autre réseau s'affiche en rouge et ne se pose pas), zones de cuivre, mesure.
// Sélection multiple (Maj + clic, cadre : de gauche à droite, objets entiers ; de droite à gauche, objets touchés),
// déplacement des empreintes, pistes, vias et zones avec étirement des pistes accrochées (EL-19, EL-43), sommets
// de zone glissés, ajoutés ou retirés, contour libre et découpes dessinés à la souris (EL-44), grille et unité
// réglables (EL-37), presse-papiers du cuivre (EL-22).
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

#include <QPixmap>
#include <QWidget>

#include "OutilVue.hpp"
#include "promethee/electronique.hpp"

class AppuiLong;
class NavigationVue;
class BandeauOutil;
class Document;
class SelectionElectronique;

class VueCircuit : public QWidget {
  Q_OBJECT
 public:
  enum class Outil { Selection, Router, Via, Zone, Mesure, Contour, Decoupe, Texte };
  enum class Genre { Rien, Empreinte, Piste, Via, Zone, Texte };
  // Résultats des moteurs, calculés une fois par l'atelier à chaque modification
  struct Calculs {
    prom::ecad::Connectivite k;
    std::vector<prom::ecad::PastillePlacee> pastilles;
    prom::ecad::Remplissages zones;
    prom::ecad::EtatRoutage etat;
    std::vector<prom::ecad::Violation> drc;
  };
  explicit VueCircuit(Document* doc, QWidget* parent = nullptr);
  void setConception(int i);
  void setCalculs(const Calculs* c) {
    m_calc = c;
    ++m_generationCalculs;
    update();
  }
  // Arme un outil (1.6). Pendant une piste, un autre outil la suspend avec son tracé : sa commande faite, ou Échap,
  // la piste reprend au dernier coude (ERG-21, 1.17) ; Sélection ou Router la reprennent aussitôt
  void setOutil(Outil o);
  bool traceSuspendu() const { return m_outils.suspendus(); }
  // Paire de couches du routage (ROU-05) : la via passe de l'une à l'autre ; préréglages selon l'empilage, la
  // paire extérieure d'abord, puis les paires voisines
  std::pair<prom::ecad::Couche, prom::ecad::Couche> paireCouches() const { return m_paire; }
  void setPaireCouches(prom::ecad::Couche a, prom::ecad::Couche b);
  static std::vector<std::pair<prom::ecad::Couche, prom::ecad::Couche>> pairesPossibles(int couchesCuivre);
  // Couche d'arrivée de la prochaine via, choisie avant de la poser ; vide : l'autre couche de la paire
  std::optional<prom::ecad::Couche> arriveeVia() const { return m_arriveeVia; }
  void setArriveeVia(std::optional<prom::ecad::Couche> c);
  prom::ecad::Couche coucheApresVia() const;  // où mène la prochaine via depuis la couche active
  Outil outil() const;
  // Échap : le geste en cours (Échap garde ce qui est fixé), sinon l'état neutre (charte)
  void echap();
  BandeauOutil* bandeau() const { return m_bandeau; }
  // Calcul de fond qui dure (DRC, zones) : flèche avec sablier sur la vue, la saisie reste possible
  void setCalculLong(bool oui);
  void setCoucheActive(prom::ecad::Couche c);
  prom::ecad::Couche coucheActive() const { return m_coucheActive; }
  void setCoucheVisible(prom::ecad::Couche c, bool v);
  bool coucheVisible(prom::ecad::Couche c) const { return m_visibles[static_cast<std::size_t>(c)]; }
  // Sélection partagée avec le schéma et l'inspecteur ; sans elle, la vue garde la sienne
  void setSelectionPartagee(SelectionElectronique* s);
  SelectionElectronique* selectionPartagee() const { return m_sel; }
  void setReseauSurligne(const std::string& n);
  void setReseauZone(const std::string& n) { m_reseauZone = n; }
  void recadrer();
  void zoomer(double facteur);  // autour du centre de la vue (+ et −)
  void zoomerSurSelection();  // menu à marques, vers le haut : les objets choisis cadrés, sinon tout (1.19)
  bool menuMarquesVisible() const;  // essais
  QString annonceClavier() const;    // essais : l'annonce près du curseur (suite en attente, bascule)
  void centrerSur(prom::ecad::Pt p);
  Genre genreChoisi() const { return m_genre; }
  const std::string& choisi() const { return m_choisi; }
  void choisir(Genre g, const std::string& id);
  // Sélection multiple : l'objet « choisi » est le dernier cliqué, les autres l'accompagnent
  struct ObjetChoisi {
    Genre genre = Genre::Rien;
    std::string id;
    friend bool operator==(const ObjetChoisi&, const ObjetChoisi&) = default;
  };
  const std::vector<ObjetChoisi>& selection() const { return m_selection; }
  void setSelection(const std::vector<ObjetChoisi>& s);
  bool estChoisi(Genre g, const std::string& id) const;
  // Cadre : de gauche à droite, objets entiers ; de droite à gauche, objets touchés ; « mode » :
  // SelectionElectronique::Mode (remplacer, ajouter, basculer, retirer)
  void choisirDansCadre(prom::ecad::Pt a, prom::ecad::Pt b, bool touches, int mode = 0);
  void toutChoisir();  // Ctrl+A : tout ce qui est sur les couches visibles
  // Déplacement de la sélection (pavé, panneau, essais) : les pistes accrochées s'étirent
  void deplacerSelection(prom::ecad::Pt delta);
  // Presse-papiers du cuivre : pistes, vias et zones (les empreintes viennent du schéma)
  void copier();
  void coller();
  void dupliquer();
  bool peutColler() const { return !m_presse.pistes.empty() || !m_presse.vias.empty() || !m_presse.zones.empty() || !m_presse.textes.empty(); }
  // Texte de sérigraphie sur la carte (révision, nom, polarité…), sur la face de la couche active ; rend son UUID
  std::string poserTexte(prom::ecad::Pt p, const std::string& texte);
  prom::ecad::Boite boiteTexte(const prom::ecad::TexteCircuit& t) const;
  // Pas de la grille en cours (placement, routage) et écriture d'une longueur dans l'unité de la carte
  prom::ecad::Nm pasPlacement() const;
  prom::ecad::Nm pasRoutage() const;
  QString longueur(double nmv) const;
  void setOpaciteInactives(double o) { m_opacite = o; update(); }
  void tournerChoix(bool inverse = false);  // R ; Maj+R dans l'autre sens
  void retournerChoix();
  void supprimerChoix();
  // Réseau d'une nouvelle zone : celui qu'on a choisi d'avance, sinon il est demandé à la fermeture, la pastille
  // sous le premier sommet proposée d'abord (EL-20)
  std::string reseauPropose(const prom::ecad::Polygone& contour) const;
  // Routage piloté (essais) : départ sur une pastille, points de passage, fin
  bool router(const std::vector<prom::ecad::Pt>& points, bool forcer = false);
  // Plan de montage en PDF (A4 paysage) : faces dessus et dessous à l'échelle, contour coté, pastilles, sérigraphie,
  // repères des composants, cartouche. C'est le dessin que l'on garde à côté de soi pour câbler la carte.
  bool exporterPlanMontage(const QString& chemin) const;
  QPointF versEcran(prom::ecad::Pt p) const { return ecran(p); }  // essais : où cliquer pour atteindre ce point
  // Alt+clic (C10) : la liste de tous les objets sous le curseur, zones comprises, avec leur couche ; on y choisit
  void listerObjetsSous(QPointF q);
  QString libelleObjet(const ObjetChoisi& o) const;  // « Zone GND · cuivre dessous »
  // Action du registre (RegistreActions, 1.8) : l'outil armé d'abord, puis la vue ; faux si aucun ne la fait
  bool executerAction(std::string_view action);
  // L'action se ferait-elle maintenant (avec l'outil armé) ; le canevas ne garde que ces touches (S22)
  bool actionPossible(std::string_view action);
  bool outilPrend(std::string_view action);  // l'outil armé la fait (liste F1 : « Outil en cours »)
  QString nomOutil() const;                  // « Routage +5V », « Sélection »
  // Article mécanique glissé de la bibliothèque (trou de fixation, 2.2) : son empreinte suit le pointeur, R la
  // tourne d'un quart de tour, le lâcher la pose (le symbole entre au schéma, en un pas)
  void suivreArticle(const prom::ecad::DefSymbole& d, QPointF q);
  void tournerArticle(bool inverse = false);
  bool poserArticle(QPointF q);
  void abandonnerArticle();
  void refuserArticle(const prom::ecad::DefSymbole& d);  // un article qui n'est pas mécanique, lâché sur le circuit

 signals:
  void selectionChangee();
  void message(const QString& html);
  void outilChange();
  void coucheActiveChangee();  // une via posée pendant le routage a fait changer de couche
  void inspecteurDemande();  // E : l'inspecteur, curseur dans son premier champ
  void actionDemandee(const QString& action);  // une ligne d'un menu ouvert au clavier : l'atelier fait l'action

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void wheelEvent(QWheelEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  bool event(QEvent* e) override;
  void leaveEvent(QEvent*) override;
  // Le canevas prend le focus quand la souris y entre, sauf pendant une saisie ; il le montre par un liseré (S21)
  void enterEvent(QEnterEvent* e) override;
  void focusInEvent(QFocusEvent* e) override;
  void focusOutEvent(QFocusEvent* e) override;
  // Article venu d'ailleurs par le glisser-déposer du système (type MIME de GlisserArticle)
  void dragEnterEvent(QDragEnterEvent* e) override;
  void dragMoveEvent(QDragMoveEvent* e) override;
  void dragLeaveEvent(QDragLeaveEvent* e) override;
  void dropEvent(QDropEvent* e) override;

 private:
  class OutilBase;
  class OutilSelection;
  class OutilRoutage;
  class OutilVia;
  class OutilPolygone;
  class OutilMesure;
  class OutilTexte;
  std::unique_ptr<OutilVue> creerOutil(Outil o);  // OutilsCircuit.cpp
  bool agirVue(std::string_view action, bool faire);  // actions de la vue elle-même (outils à armer, couche, vue)
  GesteVue geste(class QMouseEvent* e) const;
  void majBandeau();
  void appliquerCurseur();
  void reprendreTrace();  // la commande imbriquée finie : la piste suspendue reprend (1.17)
  void executerGeste(const std::string& action);  // commande du menu à marques (1.19)
  // Répartiteur des touches (ERG-03) : menus au curseur, outil, premier appui au curseur, vue ; bascules annoncées
  bool toucheAction(const std::vector<const struct ActionAtelier*>& actions);
  bool agirAuCurseur(std::string_view action);
  void annoncerBascule(const struct ActionAtelier& a);
  void ouvrirMenuTouche(const struct ActionAtelier& a);
  AppuiLong* m_appuiLong = nullptr;  // appui long immobile de l'outil Sélection : la liste des objets dessous (SAI-18)
  std::optional<prom::ecad::DefSymbole> m_article;  // article mécanique glissé (2.2)
  // Le dessin fixe gardé en image (2.4, A10) : la souris ne redessine que l'outil, l'article glissé et les repères
  void peindreFixe(class QPainter& g, const prom::ecad::ConceptionElec& c);
  void peindreReperes(class QPainter& g, const prom::ecad::ConceptionElec& c);
  struct CleCache {
    std::uint64_t generation = ~std::uint64_t{0}, calculs = 0;
    int theme = -1, conception = -1;
    double echelle = 0, opacite = 0;
    QPointF origine;
    QSize taille;
    qreal dpr = 0;
    prom::ecad::Couche active{};
    std::vector<bool> visibles;
    std::string reseau;
    std::vector<ObjetChoisi> selection;
    int estompage = 0;  // ce qui n'est pas choisi, estompé au niveau réglé (2.26)
    double niveau = 0;
    friend bool operator==(const CleCache&, const CleCache&) = default;
  };
  QPixmap m_cache;
  CleCache m_cleCache;
  std::uint64_t m_generationDoc = 0, m_generationCalculs = 0;
  prom::ecad::Pt m_posArticle{};
  double m_rotationArticle = 0;
  // Clic droit court pendant une piste : supprimer la piste gênante sous le curseur, via, coude, fin (ERG-21)
  void menuRoutage(QPointF q);
  bool m_calculLong = false;
  const prom::ecad::ConceptionElec* conception() const;
  bool refuseVerrou();
  // Un pas d'annulation ; « fusion » : des appuis rapprochés de même clé n'en font qu'un (flèches, C18)
  void modifier(const std::function<void(prom::ecad::ConceptionElec&)>& f, const QString& fusion = {});
  QPointF ecran(prom::ecad::Pt p) const;
  prom::ecad::Pt monde(QPointF q) const;
  prom::ecad::Pt surGrilleRoutage(prom::ecad::Pt p) const;
  int empreinteSous(prom::ecad::Pt p) const;
  int pastilleSous(prom::ecad::Pt p) const;
  ObjetChoisi objetSous(QPointF q, std::string* reseau = nullptr) const;
  // Objets sous le point, du plus précis au plus étendu : vias, pistes (couche active d'abord), textes, empreinte,
  // zones. Une zone ne se prend que près de son contour (C10) ; « interieurs » : aussi par l'intérieur (Alt+clic)
  std::vector<ObjetChoisi> objetsSous(QPointF q, bool interieurs, std::vector<std::string>* reseaux = nullptr) const;
  void choisirSous(QPointF q, Qt::KeyboardModifiers mods = Qt::NoModifier);
  QPointF m_dernierClic{-1e9, -1e9};  // clic répété au même endroit : l'objet suivant dessous
  qint64 m_quandDernierClic = 0;
  bool majSelectionVue();  // m_selection d'après la sélection partagée ; vrai si elle a changé
  void choisirChaine(const std::string& piste, bool reseauEntier);
  struct Accroche {  // bout de piste accroché à une pastille ou à un objet déplacé : il suit
    std::string piste;
    bool b = false;      // extrémité b (sinon a)
    int empreinte = -1;  // pastille d'une empreinte déplacée : coordonnées locales ; -1 : translation simple
    prom::ecad::Pt local, depart;
  };
  std::vector<Accroche> accroches(const std::vector<ObjetChoisi>& objets) const;
  void appliquerDeplacement(prom::ecad::ConceptionElec& c, prom::ecad::Pt d) const;
  void transformerEmpreintes(const std::function<void(prom::ecad::EmpreintePlacee&)>& f);
  int sommetSous(QPointF q) const;
  std::pair<int, prom::ecad::Pt> areteSous(QPointF q) const;
  void dessinerCouche(class QPainter& g, prom::ecad::Couche c, double opacite);

  Document* m_doc;
  int m_conception = -1;
  const Calculs* m_calc = nullptr;
  GestionnaireOutils m_outils;
  BandeauOutil* m_bandeau = nullptr;
  prom::ecad::Couche m_coucheActive = prom::ecad::Couche::CuivreDessus;
  std::pair<prom::ecad::Couche, prom::ecad::Couche> m_paire{prom::ecad::Couche::CuivreDessus, prom::ecad::Couche::CuivreDessous};
  std::optional<prom::ecad::Couche> m_arriveeVia;
  std::array<bool, prom::ecad::NB_COUCHES> m_visibles{};
  std::string m_reseau, m_reseauZone;
  SelectionElectronique* m_sel = nullptr;
  SelectionElectronique* m_selPropre = nullptr;
  // Ce que la vue montre choisi : ses objets, et les empreintes des composants choisis au schéma (1.5) ;
  // m_genre et m_choisi : l'objet tenu
  Genre m_genre = Genre::Rien;
  std::string m_choisi;
  std::vector<ObjetChoisi> m_selection;
  double m_opacite = 0.45;
  // Déplacement de la sélection (glisser, flèches) : état de départ, bouts de pistes qui suivent
  prom::ecad::Circuit m_circuitDepart;
  std::vector<Accroche> m_accroches;
  struct PressePapiers {
    std::vector<prom::ecad::Piste> pistes;
    std::vector<prom::ecad::Via> vias;
    std::vector<prom::ecad::Zone> zones;
    std::vector<prom::ecad::TexteCircuit> textes;
    prom::ecad::Pt origine;
  } m_presse;
  double m_echelle = 12;
  QPointF m_origine{40, 40};
  QPointF m_souris, m_pression;
  NavigationVue* m_nav = nullptr;  // molette, pavé, boutons du milieu et de droite, menu à marques (1.19)
  prom::ecad::Pt m_departGlisse, m_positionDepart;
};
