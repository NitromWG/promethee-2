// Prométhée : imports des autres logiciels dans l'atelier électronique (lot L10 de la 0.34) — conceptions KiCad et
// EAGLE avec le tableau des couches et l'aperçu de la fusion dans la carte ouverte (10.5, 10.12), bibliothèques
// entières avec le rapport par objet (10.4, 10.18), nomenclature en entrée (10.6). Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <map>
#include <string>
#include <vector>

#include <QDialog>

#include "promethee/import_bibliotheques.hpp"
#include "promethee/idx.hpp"
#include "promethee/import_commun.hpp"
#include "promethee/nomenclature_entree.hpp"
#include "promethee/reprise_existant.hpp"

class AtelierElectronique;
class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QThread;

// Import d'une conception : cible (nouveau projet, projet ouvert, carte ouverte), tableau des couches proposé d'après
// leur nom et leur fonction et gardé par logiciel, aperçu des repères renumérotés et des réseaux homonymes
class DialogueImportConception : public QDialog {
  Q_OBJECT
 public:
  DialogueImportConception(prom::ecad::ImportTiers import, const prom::ecad::ConceptionElec* ouverte, QWidget* parent = nullptr);
  enum Cible { NouveauProjet = 0, Projet = 1, CarteOuverte = 2 };
  int cible() const;
  void reglerCible(int c);
  prom::ecad::AssociationCouches association() const;  // telle que réglée dans le tableau
  prom::ecad::OptionsFusion options() const;
  QTableWidget* tableCouches() const { return m_couches; }
  QLabel* apercu() const { return m_apercu; }
  const prom::ecad::ImportTiers& import() const { return m_import; }
  // Associations gardées par logiciel d'origine (réglages du poste) : nom d'origine → code de couche
  static std::map<std::string, std::string> associationGardee(const std::string& logiciel);
  static void garderAssociation(const prom::ecad::AssociationCouches& a);

 private:
  void refaireApercu();
  prom::ecad::ImportTiers m_import;
  const prom::ecad::ConceptionElec* m_ouverte;
  prom::ecad::AssociationCouches m_proposee;
  QTableWidget* m_couches = nullptr;
  QLabel* m_apercu = nullptr;
  QRadioButton* m_nouveau = nullptr;
  QRadioButton* m_projet = nullptr;
  QRadioButton* m_carte = nullptr;
  QCheckBox* m_reunir = nullptr;
};

// Bibliothèques d'autres logiciels (KiCad, EAGLE…) vers la bibliothèque personnelle ou une bibliothèque d'équipe
class DialogueImportBibliotheques : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueImportBibliotheques(AtelierElectronique& a, QWidget* parent = nullptr);
  // « destination » vide : bibliothèque personnelle ; sinon le fichier de la bibliothèque (d'équipe), créé s'il manque.
  // Rend le nombre d'articles importés ou remplacés, −1 en cas d'erreur.
  int importer(const QStringList& fichiers, const QString& destination, bool remplacer);
  const prom::ecad::RapportBibliotheque& rapport() const { return m_rapport; }
  QTableWidget* table() const { return m_table; }
  bool enregistrerRapport(const QString& chemin) const;

 private:
  void montrer();
  AtelierElectronique& m_atelier;
  prom::ecad::RapportBibliotheque m_rapport;
  QTableWidget* m_table = nullptr;
  QLabel* m_bilan = nullptr;
};

// Nomenclature en entrée : aperçu des changements composant par composant, puis une commande annulable
class DialogueNomenclatureEntree : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueNomenclatureEntree(AtelierElectronique& a, QWidget* parent = nullptr);
  bool lire(const QString& chemin);  // aperçu
  int appliquer();                   // rend le nombre de changements appliqués
  const prom::ecad::RapportNomenclatureEntree& apercu() const { return m_apercu; }
  QTableWidget* table() const { return m_table; }

 private:
  AtelierElectronique& m_atelier;
  std::vector<std::vector<std::string>> m_tableau;
  prom::ecad::RapportNomenclatureEntree m_apercu;
  QTableWidget* m_table = nullptr;
  QLabel* m_bilan = nullptr;
};

// Proposition IDX de la mécanique : un changement par ligne, coché pour l'accepter ; les acceptés s'appliquent en une
// commande, la réponse part à côté du fichier reçu, l'échange va à l'historique
class DialogueIdx : public QDialog {
  Q_OBJECT
 public:
  DialogueIdx(AtelierElectronique& a, prom::ecad::PropositionIdx p, QString fichier, QWidget* parent = nullptr);
  QTableWidget* table() const { return m_table; }
  std::vector<bool> acceptes() const;
  int appliquer();  // rend le nombre de changements appliqués

 private:
  AtelierElectronique& m_atelier;
  prom::ecad::PropositionIdx m_proposition;
  QString m_fichier;
  QTableWidget* m_table = nullptr;
};

// Conversion en série d'un dossier de projets (10.19) : projets trouvés dans le dossier et ses sous-dossiers, convertis
// sur un fil à part sans s'arrêter au premier échec ; chacun enregistré en projet Prométhée avec son rapport, les
// articles communs rangés une seule fois dans la bibliothèque d'équipe (ou personnelle), puis le guide de l'ancien outil
class DialogueConversionSerie : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueConversionSerie(AtelierElectronique& a, QWidget* parent = nullptr);
  ~DialogueConversionSerie() override;
  int choisirDossier(const QString& dossier);       // rend le nombre de projets trouvés
  void reglerDestination(const QString& dossier);   // dossier des projets convertis et des rapports
  void reglerBibliotheque(const QString& fichier);  // bibliothèque d'équipe ; vide : la bibliothèque personnelle
  // Lance la série sur son fil ; « attendre » rend la main une fois la série finie (essais)
  bool lancer(bool attendre = false);
  void arreter();
  bool enCours() const;
  const prom::ecad::BilanSerie& bilan() const { return m_bilan; }
  QTableWidget* table() const { return m_table; }
  QString logicielPrincipal() const;  // logiciel le plus fréquent de la série
  void montrerGuide();

 signals:
  void termine();

 private:
  void finir();
  AtelierElectronique& m_atelier;
  QString m_dossier, m_destination, m_bibliotheque;
  std::vector<prom::ecad::ProjetAConvertir> m_projets;
  prom::ecad::ContenuBibliotheque m_equipe;
  prom::ecad::BilanSerie m_bilan;
  std::atomic<bool> m_arret{false};
  QThread* m_fil = nullptr;
  bool m_fini = true;
  QTableWidget* m_table = nullptr;
  QProgressBar* m_barre = nullptr;
  QLabel *m_etat = nullptr, *m_source = nullptr;
  QLineEdit *m_dest = nullptr, *m_biblio = nullptr;
  QPushButton *m_lancer = nullptr, *m_stop = nullptr, *m_guide = nullptr;
};

// Guide des termes et des gestes de l'ancien outil, en page HTML
void montrerGuideAncienOutil(QWidget* parent, const QString& logiciel);
