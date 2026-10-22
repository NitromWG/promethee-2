// Prométhée : fenêtre principale de l'application de bureau. Licence GPL-3.0-only.
#pragma once

#include <map>
#include <string>

#include <functional>

#include <QMainWindow>
#include <QPointer>

class AtelierPiece;
class Document;
class QDockWidget;
class QStackedWidget;
class Panneau;
class QLabel;
class Vue3D;
class VueCarte;

class FenetrePrincipale : public QMainWindow {
  Q_OBJECT
public:
  explicit FenetrePrincipale(bool dansCoquille = false);
  QList<class QMenu*> menusPrincipaux() const { return m_menus; }
  QList<class QAction*> actionsRapides() const { return m_actionsRapides; }
  bool confirmerAbandonPublic() { return confirmerAbandon(); }
  void paletteCommandesPublique() { paletteCommandes(); }
  void choixAccueilPublic(const QString& code) { choixAccueil(code); }
  QString texteEtat() const;   // message de l'atelier actif, puis validité du modèle
  void lienEtat(const QString& lien);
  void ouvrirFichier(const QString& chemin);
  // Copie de récupération rouverte : projet modifié, rattaché à son fichier d'origine.
  void restaurerCopie(const std::string& contenu, const QString& cheminOrigine);
  void importerFichierKicad(const QString& chemin, bool messages, bool garderPieces = false);
  // Fichier de CAO (STEP, IGES, BREP, STL) ajouté au projet : un assemblage (une pièce par produit, structure, noms et
  // couleurs du fichier) ouvert dans l'atelier d'assemblage, ou une pièce ouverte dans l'atelier Pièces. Vrai si lu.
  bool importerFichierCAO(const QString& chemin, bool messages = true);
  void ouvrirReglage(const QString& cle);  // la fenêtre des réglages, sur ce réglage surligné (2.32)
  // Session (2.32, ERG-35) : retenue à la fermeture, rouverte au lancement si le réglage le veut ; vrai si rouverte
  void retenirSession();
  bool rouvrirSession();
  Document* document() const { return m_doc; }
  VueCarte* vueCarte() const { return m_carte; }
  void montrerAtelierPiece(bool pieces);
  // Fichier › Enregistrer pour une version précédente… (1.22, ECH-11)
  void enregistrerPourVersionPrecedente();
  void montrerAccueil();
  void montrerProjet();
  void montrerAtelierSelonProjet();
  void montrerAssemblage(int assemblage, int composant = -1);
  void montrerSchema(int k);
  // Atelier électronique : conception d'indice « conception », dans le mode demandé (schéma, circuit, 3D, fabrication)
  void montrerElectronique(int conception, int mode, int feuille = 0);
  class AtelierElectronique* atelierElectronique() const { return m_electronique; }
  // Atelier électrique : projet électrique d'indice « projet », dans le mode demandé (schéma, armoire, faisceau,
  // rapports, calculs, logement)
  void montrerElectrique(int projet, int mode, int folio = 0);
  class AtelierElectrique* atelierElectrique() const { return m_electrique; }
  // Nouveau projet électrique (« schema », « logement », « armoire », « faisceau », « exemple »), dans un projet
  // neuf ou ajouté au projet ouvert
  bool nouveauProjetElectrique(const QString& genre, bool nouveauProjet);  // faux si l'abandon du projet ouvert est refusé
  void lancerParcours(const QString& id);  // projet neuf du bon genre, puis parcours d'apprentissage
  // Import d'un projet QElectroTech (.qet) en projet électrique de Prométhée ; faux si rien n'a été importé
  bool importerQetElectrique(const QString& chemin, bool nouveauProjet);
  AtelierPiece* atelierPiece() { if (!m_atelier) montrerAtelierPiece(true); return m_atelier; }
  Vue3D* vue3D() const { return m_vue3d; }

signals:
  void titreChange();
  void demandeNouvelOnglet(const QString& code);
  void etatTexteChange();
  void demandeOuvrirFichier(const QString& chemin);

protected:
  void closeEvent(QCloseEvent* e) override;

private:
  bool confirmerAbandon();
  bool enregistrer(bool sousNouveauNom);
  void exporterDossier();
  void importerKicad(bool garderPieces = false);
  void choixAccueil(const QString& code);
  void majNavigateur();
  // L'arbre du projet suit l'atelier électronique dans son panneau Projet, et revient à gauche ailleurs (1.21)
  void placerArbre();
  void retenirRecent();
  void majRecents();
  void paletteCommandes();
  class QStackedWidget* m_pages = nullptr;
  class Accueil* m_accueil = nullptr;
  class Navigateur* m_navigateur = nullptr;
  class QSplitter* m_zoneProjet = nullptr;  // l'arbre et les ateliers
  QList<int> m_taillesZoneProjet;
  class AtelierAssemblage* m_assemblage = nullptr;
  class PageSchema* m_pageSchema = nullptr;
  class PageNomenclature* m_nomenclature = nullptr;
  class PageIntegration* m_integration = nullptr;
  class AtelierElectronique* m_electronique = nullptr;
  class AtelierElectrique* m_electrique = nullptr;
  void nouvelleConceptionElec(bool nouveauProjet, bool schemaSeul);
  void suivreProjetsElectriques();
  void nouveauProjetDepuisMenu();
 public:
  // Import électrique d'une carte KiCad (EL-36) : empreintes, cuivre, réseaux, règles du .kicad_pro voisin ;
  // dans un nouveau projet, ou ajoutée au projet ouvert. Rend faux si rien n'a été importé.
  bool importerKicadElectronique(const QString& chemin, bool nouveauProjet);
 private:
  void suivreConceptionsElec();
  class BarreTitre* m_barreTitre = nullptr;
  bool m_cadreIntegre = false;
  bool m_dansCoquille = false;
  QString m_messageAtelier;
  QList<class QMenu*> m_menus;
  QList<class QAction*> m_actionsRapides;
  QList<class QAction*> m_raccourcisCarte;  // touches simples propres à la carte et au boîtier
  void majRaccourcisCarte();
  // Menus qui suivent l'atelier actif (G1, FenetreMenusAtelier.cpp) : les actions de la mécanique (boîtier, carte
  // mécanique, exemple) se cachent dans l'atelier électronique, qui montre les siennes dans Fichier, Édition, Ajouter
  // et Affichage ; Ctrl+I et Ctrl+E passent à l'import et au dossier électroniques, sans doublon de sens.
  void construireMenusElectronique(class QMenu* fichier, class QMenu* edition, class QMenu* affichage);
  void majMenusAtelier();
  bool atelierElectroniqueActif() const;
  QList<class QAction*> m_actionsMeca;  // cachées dans l'atelier électronique
  QList<class QAction*> m_actionsElec;  // montrées dans l'atelier électronique seulement
  class QAction* m_importKicadMeca = nullptr;
  class QAction* m_importKicadElec = nullptr;
  class QAction* m_dossierMeca = nullptr;
  class QAction* m_dossierElec = nullptr;
  class QMenu* m_menuAjouter = nullptr;
  class QMenu* m_menuAjouterElec = nullptr;
  // Réouverture sur le dernier atelier utilisé, retenu par fichier (G7)
  QString atelierCourant() const;  // « elec:2:1 », « electrique:0:3 », « pieces », « carte » ; vide sur l'accueil
  void retenirAtelier();
  bool rouvrirAtelier();
  // Accueil électronique ouvert par Ctrl+3 depuis un projet : la carte ou le schéma choisis s'y ajoutent (G7)
  bool m_accueilAjoutElec = false;
  QPointer<class FenetreReglages> m_reglages;  // fenêtre des réglages, créée à la première ouverture (1.19)
  Qt::Edges bordsSous(const QPoint& p) const;

protected:
  void mouseMoveEvent(class QMouseEvent* e) override;
  void mousePressEvent(class QMouseEvent* e) override;
  void changeEvent(class QEvent* e) override;
  void paintEvent(class QPaintEvent* e) override;

private:
  void ajouterSchemaQet(bool nouveauProjet);
  bool m_majNavigateurEnCours = false;
  // L'arbre n'est reconstruit que si ce qu'il montre a changé (A12) : projet modifié en bloc, chemin, ou une
  // conception dont la signature (Navigateur::signatureConception) n'est plus la même
  bool m_arbrePerime = true;
  QString m_cheminArbre;
  std::map<std::string, std::string> m_signaturesArbre;
  void exporterKicad();
  void majEtat();

  Document* m_doc;
  VueCarte* m_carte;
  Vue3D* m_vue3d;
  Panneau* m_panneau;
  AtelierPiece* m_atelier = nullptr;
  QStackedWidget* m_ateliers = nullptr;
  QDockWidget* m_dock = nullptr;
  std::function<void()> m_majHistorique;
  class QAction* m_actionBoitier = nullptr;
  class QAction* m_actionPieces = nullptr;
  QLabel* m_etat;
};
