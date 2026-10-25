// Prométhée : enseignement dans l'atelier électronique (lot L16 de la 0.34 ; 16.10 à 16.14). Réglages de
// l'établissement lus au lancement et essais intégrés du poste ; consignes écrites sans programmer et vérifiées sur
// le projet de l'élève ; couche de l'enseignant (verrous, restrictions, pannes cachées) scellée et relevée ;
// évaluation par comparaison au montage de référence ; classe sans service en ligne et page HTML autonome.
// Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QStringList>
#include <QWidget>

#include "promethee/enseignement.hpp"

class AtelierElectronique;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTabWidget;
class QTableWidget;

namespace etablissement {
// Fichier « etablissement.json » posé à côté du programme (ou dans PROMETHEE_DOSSIER_PROGRAMME), sinon dans le
// dossier commun du poste (/etc/promethee, %PROGRAMDATA%\Promethee) ; le premier trouvé s'impose
std::optional<prom::ecad::ReglagesEtablissement> charger(QString* chemin = nullptr, QString* erreur = nullptr);
// Réglages imposés : dossier des bibliothèques, profil des cartes neuves, premier lancement passé, interface
bool appliquer(const prom::ecad::ReglagesEtablissement& r, const QString& dossierDuFichier);
const std::optional<prom::ecad::ReglagesEtablissement>& actifs();
bool actionMasquee(const std::string& id);
bool interfaceSimplifiee();
// Essais intégrés du poste (16.10) : lignes « OK … » ou « ÉCHEC … », dans l'ordre
QStringList verifierPoste();
}  // namespace etablissement

class FenetreEnseignement : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreEnseignement(AtelierElectronique& a, QWidget* parent = nullptr);
  bool montrer(const QString& onglet);  // « consignes », « enseignant », « evaluation », « classe », « diffusion »
  void recharger();

  // ---------- Consignes (16.11) ----------
  void nouvelleConsigne(const QString& id, const QString& titre);
  int ajouterEtape(const prom::ecad::EtapeConsigne& e);  // une ligne de plus dans le tableau des étapes
  prom::ecad::Consigne consigne() const;                 // telle que le tableau la montre
  bool enregistrerConsigne();                            // dans la conception ouverte, en un pas
  std::vector<prom::ecad::EtatEtape> verifierSur(int conception);
  bool exporterConsigne(const QString& chemin);
  bool importerConsigne(const QString& chemin);

  // ---------- Couche de l'enseignant (16.12) ----------
  void setSecret(const QString& s);
  bool verrouiller(const QString& reference, bool oui);
  void ajouterPanne(const prom::ecad::PanneCachee& p);
  void masquerAction(const QString& id, bool oui);
  bool sceller();  // couche appliquée à la conception ouverte et scellée, en un pas
  prom::ecad::EtatSceau relever();

  // ---------- Évaluation (16.13) ----------
  prom::ecad::ResultatEvaluation evaluer(int reference, bool complet);
  int controlesComptes() const { return m_comptes; }
  bool verifierFormule(const QString& eleve, const QString& reference);

  // ---------- Classe et diffusion (16.14) ----------
  void setDossierClasse(const QString& d);
  bool publier(const prom::ecad::Devoir& d);
  bool deposer(const QString& devoir, const QString& eleve);
  std::vector<prom::ecad::Rendu> relever(const QString& devoir);
  bool noter(const QString& devoir, const QString& eleve, double note, const QString& remarques);
  bool exporterNotes(const QString& devoir, const QString& chemin);
  bool exporterCartouche(const QString& devoir, const QString& chemin);
  bool exporterPage(const QString& chemin);

 private:
  void construireConsignes(QWidget* page);
  void construireEnseignant(QWidget* page);
  void construireEvaluation(QWidget* page);
  void construireClasse(QWidget* page);
  void construireDiffusion(QWidget* page);
  std::string projetEnTexte() const;
  AtelierElectronique& m_atelier;
  QTabWidget* m_onglets = nullptr;
  QLabel* m_etat = nullptr;
  // Consignes
  QLineEdit* m_idConsigne = nullptr;
  QLineEdit* m_titreConsigne = nullptr;
  QTableWidget* m_etapes = nullptr;
  QListWidget* m_resultats = nullptr;
  QComboBox* m_carteEleve = nullptr;
  // Enseignant
  QLineEdit* m_secret = nullptr;
  QListWidget* m_verrous = nullptr;
  QListWidget* m_actions = nullptr;
  QTableWidget* m_pannes = nullptr;
  QCheckBox* m_simplifiee = nullptr;
  QLabel* m_sceau = nullptr;
  // Évaluation
  QComboBox* m_reference = nullptr;
  QComboBox* m_mode = nullptr;
  QListWidget* m_ecarts = nullptr;
  int m_comptes = 0;
  // Classe
  QLineEdit* m_dossierClasse = nullptr;
  QTableWidget* m_rendus = nullptr;
};
