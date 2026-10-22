// Prométhée : atelier électrique (armoires, machines, bâtiment). Un projet électrique, plusieurs vues : le ruban
// choisit le mode (Schéma, Armoire, Faisceau, Grafcet, Distribution, Photovoltaïque, Logement, Calculs, Rapports). À gauche, les folios et la palette
// des symboles CEI (avec recherche et potentiels prêts à poser) ; au centre le folio, l'armoire, la planche, un
// rapport ou un calcul ; à droite, les propriétés de ce qui est choisi et la liste des problèmes (un clic mène
// à l'endroit). Les contacts se rattachent à leur bobine à la pose (numéros de bornes CEI, renvois croisés),
// les bornes à leur bornier ; repérage, numéros de fils et vérification tournent à chaque modification.
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QPointer>
#include <QWidget>

#include "promethee/electrique.hpp"
#include "promethee/electrique_distribution.hpp"
#include "promethee/electrique_grafcet.hpp"
#include "promethee/electrique_pv.hpp"

class Document;
class EtiquetteEtat;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPoint;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QTimer;
class QTreeWidget;
class QVBoxLayout;
class VueArmoire;
class VueFaisceau;
class VueSchema;
class VueSchemaElectrique;

class AtelierElectrique : public QWidget {
  Q_OBJECT
 public:
  // Identifiants des modes (stables : le navigateur et les fichiers d'essais s'en servent) ; l'ordre des onglets est
  // celui du travail : dessiner (schéma, armoire, faisceau), concevoir l'installation, calculer, sortir les rapports
  enum Mode { Schema = 0, Armoire = 1, Faisceau = 2, Rapports = 3, Calculs = 4, Logement = 5, Distribution = 6, Photovoltaique = 7, Grafcet = 8 };
  explicit AtelierElectrique(Document* doc, QWidget* parent = nullptr);
  void ouvrir(int projet, int mode, int folio = 0);
  int projet() const { return m_projet; }
  int mode() const;
  void choisirMode(int mode);
  int folio() const;
  void setEtatIntegre(bool oui);
  VueSchema* vueSchema() const;
  VueArmoire* vueArmoire() const { return m_armoire; }
  VueFaisceau* vueFaisceau() const { return m_faisceau; }
  QTreeWidget* palette() const { return m_palette; }
  QTableWidget* tableRapport() const { return m_tableRapport; }
  const prom::elec::ProjetElectrique* projetCourant() const;
  const std::vector<prom::ecad::Violation>& violations() const { return m_violations; }
  // ----- Actions (aussi pour les essais) -----
  // Outil de pose d'un symbole de la bibliothèque (appareil, contact, borne, potentiel), « catalogue » : référence
  // proposée à la pose ; « parent » : appareil auquel un contact se rattache (vide : demandé à la pose)
  void choisirSymbole(const std::string& id, const std::string& catalogue = {}, const std::string& parent = {});
  void choisirPotentiel(const QString& nom);
  void poser(prom::ecad::Pt p, int rotation);  // appelée par la vue du schéma
  void numeroterFils(bool tout);
  void reperer(bool tout);
  void verifier();
  int ajouterFolio(prom::elec::TypeFolio t, const QString& titre = {});
  void allerAuFolio(int folio);
  void implanter(bool toutReplacer);
  int nouvelleArmoire();
  int nouveauFaisceau();
  // 0 appareils, 1 nomenclature (chiffrée), 2 borniers, 3 câbles, 4 carnet, 5 E/S, 6 folios, 7 bilan de puissance,
  // 8 liste de coupe, 9 chiffrage
  static constexpr int NB_RAPPORTS = 10;
  static QString nomRapport(int rapport);
  static QString iconeRapport(int rapport);
  void montrerRapport(int rapport);
  int rapport() const { return m_rapport; }
  bool exporterPdf(const QString& chemin);
  int exporterDxf(const QString& chemin, int folio);  // un folio en DXF ; nombre d'entités (−1 : échec)
  bool exporterCsv(int rapport, const QString& chemin);
  QStringList exporterDossier(const QString& dossier);  // folios en PDF et en DXF, rapports en CSV, plans de perçage DXF
  // Remplace le contenu du projet par le projet tiré du logement (unifilaire, coffret implanté), en un pas
  void appliquerLogement(const prom::elec::Logement& l);
  prom::elec::Logement logementCourant() const;
  void montrerAppareil(const std::string& composant);  // folio, sélection et vue centrée sur son symbole principal
  int ajouterCable();
  // Circuit type (démarrage moteur, inverseur, alimentation 24 V, télérupteur, va-et-vient, prises) posé en un pas
  // d'historique, à droite de ce qui est dessiné ; rend le folio montré (−1 si rien n'a été posé)
  int insererCircuit(int indice);
  // ----- Distribution (installation calculée) -----
  // Départ d'un type ajouté sous le tableau choisi (ou sous celui du départ choisi, sinon le tableau général) ;
  // rend son indice dans la liste des départs (−1 si rien)
  int ajouterDepart(prom::elec::TypeCharge t);
  void supprimerDepartChoisi();
  void appliquerModele(prom::elec::ModeleReseau m);  // remplace l'installation, en un pas d'historique
  void choisirDepart(const std::string& uuid);       // vide : la source
  const std::string& departChoisi() const { return m_departChoisi; }
  const prom::elec::ResultatReseauBT& resultatDistribution() const { return m_resultatDistribution; }
  QTreeWidget* arbreDistribution() const { return m_arbreDistribution; }
  QTableWidget* tableDistribution() const { return m_tableDistribution; }
  int genererUnifilaire();                            // folios tirés du calcul ; rend le premier (−1 sinon)
  // ----- Calculs -----
  void choisirCalcul(const std::string& id);          // fiche de la boîte à calculs montrée au centre
  const std::string& calculChoisi() const { return m_calculChoisi; }
  bool exporterNoteDeCalcul(const QString& chemin);   // PDF, HTML ou CSV selon l'extension
  // ----- Photovoltaïque -----
  void appliquerPV(double kWc);                       // installation type de cette puissance, en un pas d'historique
  int genererUnifilairePV();                          // folio tiré du calcul ; rend son indice (−1 sinon)
  bool exporterNotePV(const QString& chemin);         // PDF ou HTML selon l'extension
  const prom::elec::ResultatPV& resultatPV() const { return m_resultatPV; }
  // ----- Grafcet (NF EN CEI 60848) : dessin, édition dans le panneau, simulation, folio, texte structuré -----
  int nouveauGrafcet(const std::string& exemple = {});  // vide : une étape initiale ; rend son indice
  void choisirGrafcet(int indice);
  int grafcetCourant() const { return m_grafcet; }
  void choisirDansGrafcet(int etape, int transition);  // élément montré dans le panneau (−1, −1 : le grafcet)
  int etapeSuivanteGrafcet();                           // après l'étape ou la transition choisie ; rend son numéro
  void brancheGrafcet(bool et);                         // OU depuis l'étape choisie, ET depuis la transition choisie
  void bouclerGrafcet(int vers);                        // de l'étape choisie vers une étape existante
  void supprimerDansGrafcet();
  void simulerGrafcet(bool oui);
  prom::elec::SimulateurGrafcet* simulationGrafcet() const { return m_simulation.get(); }
  void entreeSimulation(const std::string& entree, bool valeur);
  void avancerSimulation(double secondes);
  int dessinerFolioGrafcet();                           // folio Grafcet du grafcet choisi (créé au besoin) ; son indice
  bool exporterTexteStructure(const QString& chemin);
  class VueGrafcet* vueGrafcet() const { return m_vueGrafcet; }
  // ----- Appareils personnels : formulaire du panneau, rangés dans le projet et dans la bibliothèque de l'utilisateur
  // (fichier « bibliotheque-electrique.json », ou celui que nomme PROMETHEE_BIBLIOTHEQUE_ELECTRIQUE) -----
  void nouvelAppareil();                                  // formulaire vierge dans le panneau
  void modifierAppareilPersonnel(const std::string& id);  // formulaire rempli (appareil du projet ou de la bibliothèque)
  // Range l'appareil (nouveau, ou modifié s'il porte un identifiant) ; rend son identifiant (vide si ses défauts
  // l'empêchent). Un appareil seulement dans la bibliothèque y est modifié sans toucher au projet.
  std::string enregistrerAppareilPersonnel(const prom::elec::SpecAppareil& a);
  bool formulaireAppareilOuvert() const { return m_formAppareil; }
  const std::vector<prom::ecad::DefSymbole>& mesAppareils() const { return m_mesAppareils; }
  static QString cheminMesAppareils();
  // ----- Apprendre : parcours guidé, carte en tête du panneau ; chaque étape se valide sur le projet -----
  void demarrerParcours(const std::string& id);
  void quitterParcours();
  void allerEtapeParcours(int etape);
  const std::string& parcours() const { return m_parcours; }
  int etapeParcours() const { return m_etapeParcours; }

 signals:
  void messageChange(const QString& html);
  void parcoursDemande(const QString& id);  // un parcours choisi au ruban : il se suit dans un projet neuf

 private:
  void construireRuban();
  QWidget* construireGauche();
  QWidget* pageRapports();
  QWidget* pageCalculs();
  QWidget* pageLogement();
  void majFolios();
  void majPalette();
  void majPanneau();
  void majRapport();
  void majCalculs();
  void majLogement();
  void panneauSchema(QVBoxLayout* col);
  void panneauArmoire(QVBoxLayout* col);
  void panneauFaisceau(QVBoxLayout* col);
  void panneauRapport(QVBoxLayout* col);
  void panneauReglages(QVBoxLayout* col);
  void panneauDistribution(QVBoxLayout* col);
  void listeProblemes(QVBoxLayout* col);
  QWidget* pageDistribution();
  QWidget* gaucheDistribution();
  QWidget* gaucheCalculs();
  void majDistribution();
  void modifierReseau(const std::function<void(prom::elec::ReseauBT&)>& f);
  QWidget* pagePV();
  void majPV();
  void panneauPV(QVBoxLayout* col);
  void modifierPV(const std::function<void(prom::elec::InstallationPV&)>& f);
  QWidget* pageGrafcet();
  QWidget* gaucheGrafcet();
  void majGrafcet();
  void panneauGrafcet(QVBoxLayout* col);
  void modifierGrafcet(const std::function<void(prom::elec::Grafcet&)>& f);
  const prom::elec::Grafcet* grafcetChoisi() const;
  void carteParcours(QVBoxLayout* col);
  void suivreParcours();  // l'étape faite pendant qu'elle était montrée passe à la suivante
  int modeOnglet(int index) const;
  int ongletMode(int mode) const;
  void recalculer();
  void modifierProjet(const std::function<void(prom::elec::ProjetElectrique&)>& f);
  std::string choisirParent(prom::elec::RoleSymbole role, const std::string& symbole);
  // Définition d'un symbole : celle du projet, sinon la bibliothèque CEI, sinon mes appareils
  const prom::ecad::DefSymbole* definitionSymbole(const std::string& id) const;
  void panneauAppareil(QVBoxLayout* col);
  void chargerMesAppareils();
  void rangerAppareil(const prom::ecad::DefSymbole& d);
  void retirerAppareil(const std::string& id);
  void menuPalette(const QPoint& ou);
  std::string choisirBornier();
  void allerA(const prom::ecad::Violation& v);

  Document* m_doc;
  int m_projet = -1;
  std::string m_uuidProjet;  // pour reconnaître un autre projet au même indice (nouveau document)
  QTabWidget* m_ruban = nullptr;
  std::vector<int> m_ordreModes;  // mode de chaque onglet du ruban
  QSplitter* m_corps = nullptr;
  QStackedWidget* m_gauche = nullptr;
  QStackedWidget* m_pileGauche = nullptr;
  std::map<int, QWidget*> m_pagesGauche;  // colonne de gauche de chaque mode qui en a une
  QStackedWidget* m_vues = nullptr;
  VueSchemaElectrique* m_schema = nullptr;
  VueArmoire* m_armoire = nullptr;
  VueFaisceau* m_faisceau = nullptr;
  QWidget* m_pageRapports = nullptr;
  QWidget* m_pageCalculs = nullptr;
  QWidget* m_pageLogement = nullptr;
  QScrollArea* m_panneau = nullptr;
  EtiquetteEtat* m_etat = nullptr;
  QTimer* m_minuterie = nullptr;
  // Gauche, mode schéma
  QListWidget* m_folios = nullptr;
  QLineEdit* m_recherche = nullptr;
  QTreeWidget* m_palette = nullptr;
  // Gauche, autres modes
  QListWidget* m_armoires = nullptr;
  QListWidget* m_faisceaux = nullptr;
  QListWidget* m_rapports = nullptr;
  // Rapports
  QLabel* m_titreRapport = nullptr;
  QTableWidget* m_tableRapport = nullptr;
  int m_rapport = 0;
  int m_cableChoisi = -1;
  // Calculs : la liste à gauche, la fiche du calcul choisi au centre
  QTreeWidget* m_listeCalculs = nullptr;
  QLineEdit* m_rechercheCalcul = nullptr;
  QWidget* m_formCalcul = nullptr;
  QLabel* m_titreCalcul = nullptr;
  QLabel* m_resultatCalcul = nullptr;
  std::string m_calculChoisi = "section";
  std::function<void()> m_calculer;
  // Logement
  QTableWidget* m_pieces = nullptr;
  QTableWidget* m_circuits = nullptr;
  QLabel* m_avisLogement = nullptr;
  std::function<prom::elec::Logement()> m_lireLogement;
  std::function<void(const prom::elec::Logement&)> m_ecrireLogement;
  std::function<void()> m_calculLogement;
  std::string m_logementPour;  // projet (UUID) dont le logement est chargé dans le formulaire
  // Distribution
  QWidget* m_pageDistribution = nullptr;
  QStackedWidget* m_pileDistribution = nullptr;  // installation vide (modèles) ou résultats
  QTreeWidget* m_arbreDistribution = nullptr;
  QTableWidget* m_tableDistribution = nullptr;
  QLabel* m_bandeauDistribution = nullptr;
  std::string m_departChoisi;
  prom::elec::ResultatReseauBT m_resultatDistribution;
  QPointer<QLabel> m_detailDistribution;  // résultat du départ choisi, dans le panneau : tenu à jour sans le reconstruire
  // Photovoltaïque
  QWidget* m_pagePV = nullptr;
  QStackedWidget* m_pilePV = nullptr;  // installations types ou résultats
  QLabel* m_bandeauPV = nullptr;
  QWidget* m_vueCalepinage = nullptr;
  QLabel* m_detailPV = nullptr;
  prom::elec::ResultatPV m_resultatPV;
  // Grafcet
  class VueGrafcet* m_vueGrafcet = nullptr;
  QWidget* m_pageGrafcet = nullptr;
  QLabel* m_bandeauGrafcet = nullptr;
  QListWidget* m_listeGrafcets = nullptr;
  int m_grafcet = -1;
  int m_etapeGrafcet = -1, m_transitionGrafcet = -1;
  std::unique_ptr<prom::elec::SimulateurGrafcet> m_simulation;
  std::string m_signatureSimulation;  // le grafcet simulé : la simulation repart s'il change
  // Apprendre
  std::string m_parcours;
  int m_etapeParcours = 0;
  bool m_etapeDejaFaite = false;  // faite dès son début : elle attend « Suivante »
  // Calculs du projet, gardés pour les vues
  prom::ecad::Connectivite m_k;
  prom::elec::Renvois m_renvois;
  std::vector<prom::ecad::Violation> m_violations;
  // Pose en cours
  std::string m_symbolePose, m_cataloguePose, m_parentPose;
  // Appareils personnels : saisie du formulaire (gardée quand le panneau se refait), bibliothèque de l'utilisateur
  bool m_formAppareil = false, m_modifAppareil = false;
  prom::elec::SpecAppareil m_specAppareil;
  std::vector<prom::ecad::DefSymbole> m_mesAppareils;
};
