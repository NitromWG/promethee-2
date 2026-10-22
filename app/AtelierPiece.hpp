// Prométhée : atelier de conception de pièces (arbre de construction, esquisseur, vue 3D, propriétés).
// Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <set>

#include <QPointer>
#include <QColor>
#include <QWidget>

#include "VuePiece.hpp"

#include "promethee/construction.hpp"
#include "promethee/dessin.hpp"
#include "promethee/dxf_dessin.hpp"
#include "promethee/piece.hpp"
#include "promethee/unites.hpp"

class Document;
class Esquisseur;
class QComboBox;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class QTabWidget;
class VuePiece;

class AtelierPiece : public QWidget {
  Q_OBJECT
public:
  void setEtatIntegre(bool oui);  // dans la fenêtre à onglets : la ligne d'état de l'atelier monte dans la barre du bas
  explicit AtelierPiece(Document* doc, QWidget* parent = nullptr);
  void nouvellePiece();
  void exporter();
  bool aUnePiece() const { return m_indice >= 0; }
  void editerEsquisse(int id);
  // Tôlerie : esquisse du contour puis tôle de base ; déplié montré à la place de la pièce pliée ; export du déplié
  void esquisserTole();
  void montrerDeplie(bool oui);
  bool vueDepliee() const { return m_vueDepliee; }
  bool exporterDeplie(const QString& format, const QString& chemin = {});
  static std::vector<prom::OptionsMiseEnPlan::FiletageDessin> filetagesDessin(const prom::ResultatPiece& r);
  // Insérer un DXF dans l'esquisse éditée ; chemin et options donnés : sans fenêtre (essais). Vrai si inséré.
  bool insererDxf(const QString& chemin = {}, const prom::dxf::OptionsImportDxf* options = nullptr);
  // Insérer un fichier de CAO (STEP, IGES, BREP, STL) comme corps importé, avec son nom, sa couleur et les couleurs
  // de ses faces ; un fichier d'assemblage est posé à plat. Chemin vide : fenêtre de choix. Vrai si inséré.
  bool insererFichierCAO(const QString& chemin = {});
  // Apparences par face (M12) : couleur « #RRGGBB » des faces choisies dans la vue ; vide : couleur de la pièce
  void colorerSelection(const QString& couleur);
  void choisirCouleurFaces();
  void editerFonction(int id);
  bool enEsquisse() const { return m_esquisseEditee != 0; }
  bool enEdition() const { return m_gestion || m_esquisseEditee != 0; }
  bool enReglage() const { return m_gestion && m_esquisseEditee == 0; }
  // Termine l'édition en cours (esquisse validée, réglage validé par OK) avant un changement de pièce ou de
  // document (AP-01, AP-02, EI-03). Faux si une édition reste ouverte (réglage incomplet) : l'action qui l'a
  // demandée doit alors être refusée.
  bool terminerEdition();
  // Navigateur du projet : l'atelier range les fonctions de la pièce active sous l'élément « racine ».
  void attacherArbre(QTreeWidget* arbre);
  void setRacineArbre(QTreeWidgetItem* racine);
  void menuArbreExterne(const QPoint& p) { menuArbre(p); }
  int pieceActive() const { return m_indice; }
  void activerPiece(int i);
  void choisirFonctionPublique(int id) { choisirFonction(id); }
  void nouvelleMiseEnPlan();
  // Bouton flottant de retour (par exemple vers l'assemblage d'où vient la pièce) ; sans fonction, il disparaît
  void afficherRetour(const QString& texte, std::function<void()> f);
  class QPushButton* m_retour = nullptr;
  class QSplitter* m_separation = nullptr;
  void verifierFabricationPiece();
  int nombreErreurs() const { return static_cast<int>(m_res.erreurs.size()); }
  QString premiereErreur() const;
  void montrerPremiereErreur();
  void variablesPiece();
  // Commandes d'esquisse à panneau (symétrie, réseaux, décaler, déplacer, pivoter, échelle, relations)
  void ouvrirCommandeEsquisse(const QString& c);
  void majCommandeEsquisse();
  // Résultat de la commande à panneau, résolu (tel que OK l'appliquera) ; ecart : distance entre la transformation
  // demandée et ce que les relations laissent faire
  std::optional<prom::Esquisse> resultatCommande(double* ecart = nullptr) const;
  void majApercuCommande();
  void fermerCommandeEsquisse(bool valider);
  // Ouvre une mise en plan ; creation : nouvelle, gardée seulement si l'utilisateur la garde (vrai alors)
  bool ouvrirMiseEnPlan(int index, bool creation = false);
  void miseEnPlan();
  void renommerMiseEnPlan(int index);
  void supprimerMiseEnPlan(int index);
  void annulerDansEsquisse();
  void retablirDansEsquisse();
  bool peutAnnulerDansEsquisse() const;
  bool peutRetablirDansEsquisse() const;

signals:
  void messageChange(const QString& html);
  void historiqueChange();

public:
  class PanneauLateral* panneauLateral() const { return m_panneau; }  // essais (U1)
  void ouvrirProprietesPiece() { proprietesPiece(); }
  void ouvrirVariables() { variablesPiece(); }
  void ouvrirMasse() { afficherMasse(); }

private:
  // Avertit que des fonctions en échec ne sont pas appliquées ; vrai pour poursuivre (EX-13)
  bool confirmerMalgreErreurs(const QString& action);
  void charger();
  void enregistrer();
  void reconstruire(bool recadrer = false);
  void majArbre();
  void majProprietes();
  void majChoixPieces();
  int fonctionChoisie() const;
  void choisirFonction(int id);
  void ajouterEsquisse(prom::PlanBase plan, bool surFace);
  void commandeEsquisse();                     // sur le plan ou la face choisis, sinon attend un clic
  bool creerEsquisseDepuisSelection();
  prom::Unites unites() const;
  void nouvelleFonction(prom::TypeFonction type, bool enlever, const std::string& variante = {});
  void afficherMasse();
  void terminerEsquisse();
  // Gestionnaire de fonction : réglages, sélection dans la vue, aperçu en direct, puis OK ou Annuler.
  void ouvrirGestionnaire(const prom::FonctionPiece& f, bool nouvelle);
  void fermerGestionnaire(bool valider);
  void apercu();
  void selectionVue();
  void menuArbre(const QPoint& p);
  void deplacerFonction(int id, int sens);
  std::vector<std::string> referencesSelection(bool faces) const;

  Document* m_doc;
  int m_indice = -1;
  std::string m_idPiece;  // identifiant stable de la pièce éditée : m_indice est recalculé d'après lui (AP-02)
  int rangPiece(const std::string& id) const;  // rang de la pièce d'identifiant id dans le projet, −1 si absente
  // Avant une commande de pièce : une esquisse ouverte est terminée ; faux si elle reste ouverte
  bool peutLancer();
  prom::Piece m_piece;
  prom::ResultatPiece m_res;
  int m_esquisseEditee = 0;
  bool m_gestion = false, m_nouvelle = false;
  prom::FonctionPiece m_brouillon;
  prom::ResultatPiece m_affiche;
  prom::ResultatPiece m_avant;   // état de la pièce juste avant la fonction en cours de réglage
  enum class Champ { Aucun, Esquisse, Regions, Fin1, Fin2, Aretes, Faces, Supports, Sources, Direction, Neutre, FacesEpaisseur, Chemin, Profils, Sommet3D, Points3D, FaceChanfrein, AxeRevolution } m_champ = Champ::Aucun;
  // Points intérieurs des régions choisies, tenus à jour avec leurs clés (FO-06)
  void synchroniserPointsRegions(prom::FonctionPiece& f) const;
  void vueEnCoupe();
  class QFrame* m_panneauCoupe = nullptr;
  std::string nomReference(const TopoDS_Shape& s) const;   // face, arête, sommet, plan de base ou de référence
  bool m_attenteEsquisse = false;
  void activerChamp(Champ c);
  void masquerEsquissesUtilisees(prom::ResultatPiece& r, const prom::Piece& p) const;
  static std::set<int> esquissesUtilisees(const prom::Piece& p);
  static bool esquisseVisible(const prom::FonctionPiece& f, const std::set<int>& utilisees);
  std::set<int> esquissesDuReglage() const;
  std::string m_dernierJson;
  QComboBox* m_choixPiece;
  QTreeWidget* m_arbre;
  QStackedWidget* m_centre;
  VuePiece* m_vue;
  Esquisseur* m_esq;
  QScrollArea* m_props;
  QWidget* m_outilsPiece;          // zone de commandes : pièce, onglets par catégorie, filtre
  class QTabWidget* m_onglets = nullptr;
  void activerCommandes(bool oui);
  void proprietesPiece();  // panneau latéral (U1)
  void appliquerProprietesPiece(const QString& nom, const QString& reference, const QString& fournisseur, const class QTableWidget* proprietes, const QString& longueur,
                                const QString& angle, const QString& materiau, double densite, bool confirmee, const QColor& couleur, double transparence);
  void majPlans();
  class QWidget* m_coinValidation = nullptr;   // coche verte et croix rouge dans la vue
  QPointer<class QTableWidget> m_tableCotes;  // éditeur des cotes de l'esquisse
  void majTableCotes();
  void convertirEntites();
  prom::Esquisse geometrieExterne(std::vector<std::string>* noms = nullptr) const;
  void fonctionDepuisEsquisse(prom::TypeFonction type, bool enlever);
  int m_retourEsquisse = 0;
  // Session d'esquisse : un seul pas d'historique du projet, de l'ouverture à ✔ (ou aucun si ✖), AP-04
  bool m_sessionEsquisse = false;
  bool m_symetrieParFonctions = false;
  double m_epaisseurParticuliere = 0;
  class PanneauLateral* m_panneau = nullptr;  // outils dans un panneau latéral, sans fenêtre modale (U1) ; variables (AP-37)  // coque : épaisseur donnée aux faces particulières ajoutées (AP-18)  // symétrie : fonctions choisies plutôt que tout le corps (AP-08)
  static bool sourceRepetable(const prom::FonctionPiece& f);
  bool sensEnlevement(const prom::FonctionPiece& f) const;
  double valeurRonde(double mm) const;
  void arrondirDefauts(prom::FonctionPiece& f) const;
  std::string supportDepuisArbre() const;
  void clicArbreReglage(int id);
  void validerReglage();
  void supprimerFonction(int id);
  void activerRuban(bool oui);
  void majFiltre(int index);
  class QActionGroup* m_groupeFiltre = nullptr;
  class QAction* m_actionPlans = nullptr;
  void commencerSessionEsquisse();
  void finirSessionEsquisse();
  bool m_retourNouvelle = false;                      // esquisse neuve au départ de la fonction (EI-27)
  std::optional<prom::FonctionPiece> m_retourAvant;   // état d'ouverture de l'esquisse, rendu au retour
  std::optional<prom::FonctionPiece> m_prochainAvant;
  class QListWidget* m_listeCalques = nullptr;
  class QLabel* m_resumeContraintes = nullptr;
  class QProgressBar* m_barreContraintes = nullptr;
  void majCalques();
  void majDiagnostic();
  void aideRaccourcis();
  QPointer<class QGroupBox> m_groupeEtat;
  QPointer<class QGroupBox> m_groupeCotes;
  QPointer<class QVBoxLayout> m_etatEsquisse;
  void infoSelection();
  class QFrame* m_barre3D = nullptr;
  void majBarre3D(const QPoint& pres);
  void majVisibilite();
  void colorerFaces(const prom::ResultatPiece& r);
  void majPoignee();
  bool m_mesure = false;
  bool m_vueDepliee = false;
  int m_toleApresEsquisse = 0;  // esquisse dont la validation ouvre la tôle de base
  QAction* m_actionDeplie = nullptr;
  // Mesure (M11) : panneau (texte, unités, garder, effacer) ; mesures gardées dessinées jusqu'à leur effacement
  class QFrame* m_panneauMesure = nullptr;
  class QLabel* m_texteMesure = nullptr;
  class QComboBox* m_uniteMesure = nullptr;
  std::vector<VuePiece::TraitMesure> m_tracesMesure, m_mesuresGardees;
  void placerPanneauMesure();
  void majTracesMesure();
  prom::Unites unitesMesure() const;
public:
  void basculerMesure();
  void garderMesure();
  void effacerMesures();
  std::size_t nombreMesuresGardees() const { return m_mesuresGardees.size(); }
private:
  qint64 m_dernierApercu = 0;
  void basculerVisibiliteEsquisse(int id);
  class QFrame* m_bandeVue = nullptr;
  void majCoinValidation();
  std::string nommerDansAvant(const TopoDS_Shape& s, bool faces) const;
  bool m_plansDemandes = false;
  QString m_commande;
  std::vector<int> m_cmdElements;
  int m_cmdAxe = 0, m_cmdPoint = -1, m_cmdNombre = 3, m_cmdChamp = 0;
  double m_cmdValeur = 20, m_cmdValeur2 = 0;
  bool m_cmdInverse = false, m_cmdCopie = false, m_cmdLiberer = false;
  QPointer<QLabel> m_cmdAlerte;  // relations qui retiennent la sélection (EI-10)
  std::string m_cmdDirection = "X";
  QPointer<class QStackedWidget> m_pileEsquisse;
  QTreeWidget* m_arbrePropre = nullptr;
  QTreeWidgetItem* m_racine = nullptr;
  bool m_arbreExterne = false;
  std::set<int> m_fonctionsOuvertes;
  bool dansMaPiece(const QTreeWidgetItem* it) const;
  QTreeWidgetItem* cibleArbre() const;
  void brancherArbre(QTreeWidget* a, bool menu);
  // Session d'esquisse : état d'avant (pour annuler) et vue d'avant (rétablie en sortant)
  prom::FonctionPiece m_esquisseAvant;
  bool m_esquisseNouvelle = false, m_prochaineNouvelle = false;
  void annulerEsquisse();
  void sortirEsquisse(int id);
  QToolBar* m_outilsEsquisse;
  class EtiquetteEtat* m_etat;
};
