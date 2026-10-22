// Prométhée : atelier électronique. Un seul modèle, plusieurs vues sans changer de fichier : le ruban choisit le
// mode (Schéma, Circuit imprimé, 3D, Fabrication) et le canevas suit. Panneau de droite selon l'objet choisi
// (composant, étiquette, empreinte, piste, via, zone, carte et règles), bibliothèque de symboles avec aperçu,
// liste des problèmes (ERC, DRC) où un clic mène à l'endroit. Les moteurs (connectivité, ERC, zones, chevelu, DRC)
// tournent à chaque modification. Un clic sur un réseau le surligne dans le schéma et sur le circuit. Les panneaux
// (projet, bibliothèque, inspecteur, messages) sont posés sur une fenêtre principale imbriquée (1.21, ERG-23).
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QKeySequence>
#include <QPointer>
#include <QWidget>

#include "VueCircuit.hpp"
#include "ZoneMessages.hpp"
#include "promethee/bibliotheque.hpp"
#include "promethee/commandes.hpp"
#include "promethee/electronique.hpp"

class Document;
class DispositionsAtelier;
class QComboBox;
class QToolButton;
class QDockWidget;
class QMainWindow;
class QScrollArea;
class QStackedWidget;
class QAction;
class QTabWidget;
class QTimer;
class SelectionElectronique;
class VuePiece;
class VueSchema;

class AtelierElectronique : public QWidget {
  Q_OBJECT
 public:
  enum Mode { Schema = 0, Circuit = 1, Vue3D = 2, Fabrication = 3 };
  explicit AtelierElectronique(Document* doc, QWidget* parent = nullptr);
  ~AtelierElectronique() override;
  void ouvrir(int conception, int mode, int feuille = 0);
  int conception() const { return m_conception; }
  int mode() const;
  void setEtatIntegre(bool oui);
  // Échap où que soit le focus dans l'atelier : champ de saisie quitté, puis état neutre (charte, S21)
  void echap();
  // F1 : les raccourcis de l'outil en cours, de la vue, de l'atelier et de Prométhée (registre des actions, 1.8)
  void montrerRaccourcis();
  class ListeRaccourcis* listeRaccourcis() const;  // essais
  // E : l'inspecteur, le curseur dans son premier champ
  void ouvrirInspecteur();
  VueSchema* vueSchema() const { return m_schema; }
  VueCircuit* vueCircuit() const { return m_circuit; }
  SelectionElectronique* selection() const { return m_selection; }
  const VueCircuit::Calculs& calculs() const { return m_calculs; }
  const std::vector<prom::ecad::Violation>& erc() const { return m_erc; }
  // Vérification complète (« Vérifier », F7 ; 2.11, S9) : ERC et DRC refaites sur-le-champ, panneau Vérifications
  // ouvert. Les résultats du niveau complet restent montrés jusqu'à la suivante et passent « à refaire » quand la
  // conception change ; le passage au circuit les refait aussi.
  void verifier();
  bool verificationFaite() const { return m_verificationFaite; }
  bool verificationPerimee() const { return m_verificationPerimee; }
  // Ce que montrent la vue et le panneau : l'ERC immédiate et du dessin, à jour, et le niveau complet de la
  // dernière vérification
  std::vector<prom::ecad::Violation> ercMontree() const;
  class PanneauVerifications* verifications() const { return m_verifications; }
  void montrerVerifications();
  // Chercher et remplacer (Ctrl+F, 2.12, SAI-17) : le panneau Chercher, le curseur dans son champ
  void chercher();
  class PanneauRecherche* recherche() const { return m_recherche; }
  // Indicateur de zoom de la barre d'état (2.12, S14) : « 85 % » de la taille réelle ; un clic offre Tout voir,
  // la taille réelle, la sélection et la vue d'ensemble
  QToolButton* indicateurZoom() const { return m_zoom; }
  // Apprendre (2.17) : le parcours « Ta première carte », le lexique ; au premier lancement, les préférences
  void montrerApprendre();
  class PanneauApprendre* apprendre() const { return m_apprendre; }
  // Nombre de calculs de connectivité lancés (essais : une valeur changée n'en relance aucun, A12)
  std::uint64_t generationCalcul() const { return m_generation; }
  // Calcul complet et immédiat (essais, changement de mode, boutons ERC et DRC)
  void recalculer();
  // Après une modification (EL-13) : au schéma, connectivité et ERC seulement ; pendant un glissement, chevelu
  // sur les zones déjà remplies ; sinon remplissage, état du routage et DRC sur un fil à part, l'interface
  // gardant le dernier résultat (« vérification en cours ») ; un résultat périmé est jeté et le calcul relancé
  void recalculerApresModification();
  bool calculEnCours() const { return m_calculEnCours; }
  struct Resultat;
  // Actions (aussi pour les essais)
  void annoter(bool tout);
  void mettreAJourCircuit();
  QStringList exporterFabrication(const QString& dossier);
  bool exporterDessin(const QString& chemin);
  bool exporterPlanMontage(const QString& chemin);
  // Typon de gravure maison : PDF à l'échelle 1 (ou SVG selon l'extension)
  bool exporterTypon(const QString& chemin, const prom::ecad::OptionsTypon& o);
  void exporterTyponInteractif();
  // La carte pilote le boîtier du projet ; « montrer » : l'atelier du boîtier s'ouvre (la case de l'inspecteur lie sans
  // quitter l'atelier, P22 : un lien du message y mène)
  void lierAuBoitier(bool montrer = true);
  // Bibliothèque, panneau ancré à gauche (1.10) : ouverte au schéma ; « chercher » (A) met le curseur dans sa
  // recherche, sinon elle ne prend pas le focus
  void montrerBibliotheque(bool chercher = false);
  void replierBibliotheque();
  class PanneauBibliotheque* bibliotheque() const { return m_panneauBibliotheque; }
  // Tableau des champs de tous les composants (valeurs, fabricants, références…), appliqué en un pas
  void ouvrirTableauChamps();
  // Rend le nombre de composants changés. « base » : les composants que le tableau montrait ; seuls les champs que
  // le tableau a changés sont alors écrits, ce qui a changé ailleurs entre-temps reste
  int appliquerChamps(const std::vector<prom::ecad::ComposantElec>& nouveaux, const std::vector<prom::ecad::ComposantElec>* base = nullptr);
  void montrerComposant(const std::string& uuid);  // feuille, sélection et vue centrée sur son symbole
  // Retire un composant : ses symboles de toutes les feuilles et son empreinte du circuit, en un pas (G2)
  void supprimerComposant(const std::string& uuid);
  void supprimerFeuille(int feuille);
  // Composants créés par l'utilisateur : bibliothèque personnelle (fichier JSON, réutilisée d'un projet à l'autre)
  static QString cheminBibliothequePerso();
  static std::vector<prom::ecad::DefSymbole> bibliothequePerso();
  static prom::ecad::LectureBibliotheque lectureBibliothequePerso();  // avec l'état du fichier (abîmé, cause)
  void ajouterSymbolePerso(const prom::ecad::DefSymbole& d);
  void ajouterSymbolesPerso(const std::vector<prom::ecad::DefSymbole>& ds);
  // Empreintes créées (tableau de pastilles) ou importées (.kicad_mod) : bibliothèque personnelle et projet ;
  // « composant » (UUID) reçoit l'empreinte s'il est donné
  static std::vector<prom::ecad::DefEmpreinte> empreintesPerso();
  void ajouterEmpreintePerso(const prom::ecad::DefEmpreinte& d, const std::string& composant = {});
  void creerEmpreinte(const std::string& composant);
  // Symbole à plusieurs unités : pose l'unité suivante non posée du composant de ce symbole, à côté de lui
  std::string poserUniteSuivante(const std::string& uuidSymbole, int feuille);
  void importerEmpreinteKicad(const std::string& composant);
  void importerSymbolesKicad();
  void creerComposant(const prom::ecad::DefSymbole* base = nullptr, bool remplacer = false);
  // Validation globale et publication pour la fabrication (révision verrouillée)
  prom::ecad::Validation validation() const;
  bool publier(const QString& dossierParent, const QString& auteur, const QString& derogation, QString* erreur = nullptr);
  void nouvelleRevision();
  void routerAutomatiquement();

  // ---------- Pour l'inspecteur (InspecteurElectronique, 1.9) ----------
  Document* document() const { return m_doc; }
  const prom::ecad::ConceptionElec* conceptionCourante() const;
  // Une modification de la conception ouverte, en un pas qui ne photographie qu'elle (A2) ; « fusion » : voir
  // Document::modifierConception (« saisie:N » : un passage dans un champ, un pas)
  bool modifierConception(const std::function<void(prom::ecad::ConceptionElec&)>& f, const QString& nom = {}, const QString& fusion = {});
  class InspecteurElectronique* inspecteur() const { return m_inspecteur; }
  bool bibliothequeMontree() const;
  void montrerInspecteur();                 // la page qui convient à la vue et à la sélection
  // Message passager (1.11) : empilé en bas à gauche du canevas, gardé dans le panneau Messages ; « aller » mène à
  // l'objet concerné d'un clic sur le message
  void afficherMessage(const QString& html, NiveauMessage niveau = NiveauMessage::Information, std::function<void()> aller = {});
  BarreEtatAtelier* barreEtat() const { return m_barreEtat; }
  PileMessages* pileMessages() const { return m_pile; }
  JournalMessages* journal() const { return m_journal; }
  void setMode(int mode);                     // onglet du ruban : schéma, circuit, 3D, fabrication
  // Panneaux (1.21, ERG-23) : sur une fenêtre principale imbriquée, les vues au centre, ils flottent (un second
  // écran), s'ancrent, se groupent en onglets ou se replient ; leurs dispositions nommées se gardent pour soi, dans
  // le projet ou dans un fichier (DispositionsAtelier)
  enum class Panneau : std::uint8_t { Projet, Bibliotheque, Inspecteur, Messages, Verifications, Recherche, Apprendre, Ecarts, Champs };
  // Écarts entre le schéma et le circuit (3.8) : la liste, et sa pastille dans la barre d'état
  class PanneauEcarts* ecarts() const { return m_ecarts; }
  void montrerEcarts();
  // Tableau des champs (3.9, SAI-16) : panneau ancré sous le canevas, relu à chaque modification quand il est ouvert
  class TableauChamps* tableauChamps() const { return m_tableauChamps; }
  void majTableauChamps();
  // Vues en mosaïque (3.21, ERG-40) : le schéma et le circuit côte à côte, liés par la sélection croisée (ce qui est
  // choisi dans l'une est cadré dans l'autre) ; la 3D et le tableau de tous les objets s'y ajoutent comme des vues
  class MosaiqueVues* mosaique() const { return m_vues; }
  void setMosaique(bool oui);
  void montrerVue3DDansMosaique(bool oui);
  void montrerTableauObjets(bool oui);
  class TableauObjets* tableauObjets() const { return m_tableauObjets; }
  // Cadre l'objet tenu dans les vues montrées qui ne sont pas celle du mode
  void cadrerVuesPassives();
  // Pinceau de propriétés (3.15, SAI-41) : pris sur un composant, appliqué à chaque composant choisi ensuite, en un pas
  // par composant ; Échap ou « Terminer » le repose
  void armerPinceau(const std::string& modele);
  void terminerPinceau();
  bool pinceauArme() const { return !m_pinceau.empty(); }
  // Renuméroter les composants choisis dans l'ordre de lecture (feuille, haut en bas, gauche à droite), en un pas,
  // sans prendre le repère d'un composant qui n'est pas choisi
  void renumeroterSelection();
  // « Remplacer… » (3.16, SAI-42) : la fenêtre du remplacement, pour ces composants (la sélection si vide)
  void remplacerComposants(std::vector<std::string> composants = {});
  QDockWidget* panneau(Panneau p) const;
  QMainWindow* fenetrePanneaux() const { return m_fenetre; }
  DispositionsAtelier* dispositions() const { return m_dispositions; }
  // Projet et bibliothèque à gauche, côte à côte ; inspecteur seul à droite (B5) ; messages en bas, fermés
  void dispositionOrigine();
  // L'arbre du projet (le navigateur de la fenêtre) vient dans le panneau Projet tant que l'atelier est montré ;
  // rendu, il est détaché, sans parent (nul s'il n'y était pas)
  void accueillirArbre(QWidget* arbre);
  QWidget* rendreArbre();
  void ouvrirMessages();
  // Action du registre (RegistreActions) lancée depuis un menu de la fenêtre : la vue montrée la fait (G1)
  bool executerAction(std::string_view action);
  // L'action se ferait-elle maintenant : une commande de l'atelier, ou la vue montrée (palette Ctrl+K, 2.14)
  bool actionPossible(std::string_view action);
  // Le composant choisi : sa liste d'empreintes ouverte dans l'inspecteur (« Changer d'empreinte… »)
  void changerEmpreinte();
  // Une liste de paires de broches (« U1:PA9 → J2:3 », une par ligne) reliée en un pas (2.18, SAI-30) ; rend le
  // nombre de segments posés, −1 si une paire n'a pu l'être (elles sont nommées dans un message)
  void relierListe();
  int relierTexte(const QString& texte);
  void exporterFabricationInteractif();  // dossier de fabrication (brouillon), dossier demandé
  // Schéma seul (STR-13) : crée le circuit imprimé, en un pas, et y passe
  void creerCircuit();
  void surligner(const QString& reseau);
  void deplacerFeuille(int feuille, int sens);
  void importerContourDxf();
  // Profil de fabricant (EL-40) : ses minimums ne font que relever les règles qui sont en dessous, après confirmation
  void choisirProfil(std::size_t indice, QComboBox* liste);
  void majCouches();
  void publierInteractif();

 signals:
  void messageChange(const QString& html);
  void boitierDemande();

 protected:
  // Un panneau flottant suit l'atelier : caché avec lui, rendu avec lui
  void hideEvent(QHideEvent* e) override;
  void showEvent(QShowEvent* e) override;
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  // Ruban (RubanElectronique.cpp, 1.20) : construit d'après sa description, qui ne cite que des identifiants du
  // registre ; les commandes de l'atelier y sont rangées par identifiant, les contrôles fabriqués à part
  void construireRuban();
  void ajusterRuban();  // replié selon la largeur (2.15, G12)
  void enregistrerCommandes();
  QWidget* controleCouches();
  QWidget* controleGrille();
  std::map<std::string, std::function<void()>, std::less<>> m_commandes;
  void majPanneau();
  // Raccourci pris par l'atelier quand le focus est hors du canevas (Suppr, R : S21) : l'action va à la vue
  // montrée ; un tableau qui a le focus reçoit la frappe à la place
  void actionHorsCanevas(const std::string& action, const QKeySequence& touche);
  QStringList ajouterDessins(const QString& dossier);
  void majVue3D();
  void annoncerSurlignage();
  void majSchemaSeul();     // onglets du circuit, de la 3D et de la fabrication selon la conception (STR-13)
  QAction* m_actionCreerCircuit = nullptr;     // « Circuit » du ruban Schéma, pour un schéma seul
  QAction* m_actionMajDepuisSchema = nullptr;  // « Mettre à jour » du ruban Schéma (F8), quand le circuit existe

  void appliquerResultat(Resultat&& r);
  void retenirVerification();  // le niveau complet de l'ERC à jour devient celui que montre la vue
  void perimerVerification();  // la conception a changé : la vérification complète est « à refaire »
  void afficherEtat(int niveau);
  void allerAuProbleme(const QString& lien);  // compteur du bilan cliqué : la première violation
  void lancerCalcul();
  void majEtatCalcul();

  Document* m_doc;
  int m_conception = -1;
  std::uint64_t m_generation = 0;
  bool m_calculEnCours = false, m_circuitPerime = true;
  std::thread m_fil;
  QTabWidget* m_ruban = nullptr;
  class MosaiqueVues* m_vues = nullptr;
  class TableauObjets* m_tableauObjets = nullptr;
  QAction *m_actionMosaique = nullptr, *m_actionTableauObjets = nullptr;
  QMainWindow* m_fenetre = nullptr;
  QDockWidget *m_dockProjet = nullptr, *m_dockBibliotheque = nullptr, *m_dockInspecteur = nullptr, *m_dockMessages = nullptr, *m_dockVerifications = nullptr, *m_dockRecherche = nullptr, *m_dockApprendre = nullptr,
              *m_dockEcarts = nullptr, *m_dockChamps = nullptr;
  class PanneauEcarts* m_ecarts = nullptr;
  class TableauChamps* m_tableauChamps = nullptr;
  std::string m_pinceau;                         // composant modèle du pinceau
  prom::ecad::ComposantElec m_modelePinceau;
  QWidget* m_attenteProjet = nullptr;  // le panneau Projet sans l'arbre
  DispositionsAtelier* m_dispositions = nullptr;
  std::vector<QPointer<QDockWidget>> m_flottantsCaches;
  SelectionElectronique* m_selection = nullptr;
  QPointer<class ListeRaccourcis> m_raccourcis;  // F1
  VueSchema* m_schema = nullptr;
  VueCircuit* m_circuit = nullptr;
  VuePiece* m_3d = nullptr;
  class InspecteurElectronique* m_inspecteur = nullptr;
  class PanneauBibliotheque* m_panneauBibliotheque = nullptr;
  bool m_bibliothequeOuverte = true;  // choix de l'utilisateur, gardé (le panneau n'apparaît qu'au schéma)
  void majBibliotheque();             // visible au schéma si elle est ouverte
  BarreEtatAtelier* m_barreEtat = nullptr;
  PileMessages* m_pile = nullptr;
  JournalMessages* m_journal = nullptr;
  QComboBox* m_coucheActive = nullptr;
  QComboBox* m_grille = nullptr;
  QComboBox* m_unite = nullptr;
  QTimer* m_minuterie = nullptr;
  VueCircuit::Calculs m_calculs;
  std::vector<prom::ecad::Violation> m_erc;
  class PanneauVerifications* m_verifications = nullptr;
  class PanneauRecherche* m_recherche = nullptr;
  class PanneauApprendre* m_apprendre = nullptr;
  QToolButton* m_zoom = nullptr;
  void majZoom();
  QToolButton* m_coordonnees = nullptr;  // 2.28
 public:
  void majCoordonnees();
  static double pixelsParMm(const QWidget* w);  // taille réelle : la densité de l'écran, étalonnée (2.29)
  void etalonnerEcran();
  QToolButton* boutonCoordonnees() const { return m_coordonnees; }

 private:
  std::vector<prom::ecad::Violation> m_ercVerifiee;  // niveau complet, à la dernière vérification
  std::string m_conceptionVerifiee;                   // UUID de la conception vérifiée
  bool m_verificationFaite = false, m_verificationPerimee = false;
  // Changements reçus depuis le dernier calcul (A12) : ils disent quels moteurs refaire
  prom::ecad::ChangementConception m_enAttente;
  int m_niveauCalcul = 2;  // niveau du dernier résultat appliqué (2 : DRC comprise)
  bool m_3dAJour = false;
  std::string m_symboleApercu;
};
