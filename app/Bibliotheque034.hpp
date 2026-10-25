// Prométhée : bibliothèque d'équipe dans l'atelier électronique (lot L19 de la 0.34 ; 19.4). Demandes d'articles
// manquants (formulaire, suivi par le bibliothécaire, notification au demandeur par la surveillance du dossier
// d'équipe), tableau de bord de santé calculé sur son fil et interruptible, comparaison côte à côte de deux
// candidats ; corriger, fusionner et supprimer sont des commandes annulables de la fenêtre. Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <memory>

#include <QFileSystemWatcher>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "promethee/sante_bibliotheque.hpp"

class AtelierElectronique;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QThread;
class QTreeWidget;
class QUndoStack;

class FenetreBibliothequeEquipe : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreBibliothequeEquipe(AtelierElectronique& a, QWidget* parent = nullptr);
  ~FenetreBibliothequeEquipe() override;
  // Dossier d'équipe (demandes, journal) et fichier de la bibliothèque d'équipe ; gardés dans les réglages
  void choisir(const QString& dossierEquipe, const QString& fichierBibliotheque);
  QString dossierEquipe() const { return m_dossier; }
  QString fichierBibliotheque() const { return m_fichier; }
  bool montrer(const QString& onglet);  // « demandes », « sante », « comparer »
  // ----- Demandes -----
  bool deposer(prom::ecad::DemandeArticle d, QString* erreur = nullptr);  // le formulaire rempli, puis déposé
  void relireDemandes();
  bool suivre(const QString& uuid, prom::ecad::EtatDemande etat, const QString& article = {}, const QString& raison = {});
  const std::vector<prom::ecad::DemandeArticle>& demandes() const { return m_demandes; }
  QStringList notifications() const { return m_notifications; }  // articles publiés apparus pour moi
  // ----- Santé -----
  void lancerSante();
  void interrompreSante();
  bool santeEnCours() const { return m_fil != nullptr; }
  const prom::ecad::SanteBibliotheque& sante() const { return m_sante; }
  // ----- Comparer, corriger, fusionner, supprimer (annulables) -----
  void comparer(const QString& a, const QString& b);
  QTableWidget* tableComparaison() const { return m_comparaison; }
  bool corriger(const QString& id, const QString& champ, const QString& valeur);
  bool fusionner(const QString& garde, const QString& supprime);
  bool supprimer(const QString& id);
  QUndoStack* pile() const { return m_pile; }
  QStringList projetsCitant(const QString& article) const;  // fichiers .prom du dossier d'équipe qui citent l'article

 signals:
  void santeTerminee();
  void notification(const QString& texte);

 private:
  void construireDemandes(QWidget* page);
  void construireSante(QWidget* page);
  void construireComparer(QWidget* page);
  void majChampsImposes();
  void majTableDemandes();
  void majSante();
  void majCandidats();
  bool executer(const prom::ecad::OperationBibliotheque& o, const QString& nom);
  std::vector<prom::ecad::Article> articles() const;

  AtelierElectronique& m_atelier;
  QString m_dossier, m_fichier;
  QTabWidget* m_onglets = nullptr;
  QLabel* m_lieu = nullptr;
  // Demandes
  QLineEdit *m_reference = nullptr, *m_fabricant = nullptr, *m_fiche = nullptr, *m_commentaire = nullptr;
  QWidget* m_champs = nullptr;  // champs imposés par l'équipe
  QTableWidget* m_tableDemandes = nullptr;
  QLabel* m_etatDemandes = nullptr;
  std::vector<prom::ecad::DemandeArticle> m_demandes;
  QStringList m_notifications, m_dejaNotifiees;
  QFileSystemWatcher m_veille;
  // Santé
  QProgressBar* m_progres = nullptr;
  QPushButton *m_lancer = nullptr, *m_arreter = nullptr;
  QTreeWidget* m_resultats = nullptr;
  prom::ecad::SanteBibliotheque m_sante;
  QThread* m_fil = nullptr;
  std::shared_ptr<std::atomic<bool>> m_arret;
  // Comparer
  QComboBox *m_a = nullptr, *m_b = nullptr;
  QTableWidget* m_comparaison = nullptr;
  QUndoStack* m_pile = nullptr;
};
