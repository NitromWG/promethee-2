// Prométhée : automatisation et assistant de l'atelier électronique (lot L13 de la 0.34). Ligne de commande sous le
// canevas, avec complétion, historique, commandes probables, macros enregistrées en direct, relues d'un fichier et
// rejouées en un pas, commande tapée affectée à une touche (13.8) ; console Python ancrée, éditeur, points d'arrêt et
// pas à pas, courbes tracées, script rangé sur un raccourci (13.3, 13.9) ; propositions d'un assistant extérieur
// reçues par le serveur MCP, acceptées ou refusées une à une (13.4) ; assistant de conception : moteur au choix,
// « Expliquer ce bloc », « Demander à l'assistant » sur une violation, mémoire montrée et suspendable, fiches
// techniques interrogées, simulation confiée (13.15 à 13.17) ; éditeur de requêtes qui compte les objets visés et
// fait une règle (13.11) ; conception guidée par modules et circuits types posés d'un clic (13.14).
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <QDialog>
#include <QStringList>
#include <QWidget>

#include "promethee/assistant.hpp"
#include "promethee/assistant_mcp.hpp"
#include "promethee/commandes_texte.hpp"
#include "promethee/conception_guidee.hpp"
#include "promethee/requetes_objets.hpp"
#include "promethee/scripts_python.hpp"

class AtelierElectronique;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFileSystemWatcher;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStringListModel;
class QTextBrowser;
class QToolButton;
class TraceCourbes;

// ---------- 13.8 · Ligne de commande sous le canevas ----------
class LigneCommandes : public QWidget {
  Q_OBJECT
 public:
  explicit LigneCommandes(AtelierElectronique& a, QWidget* parent = nullptr);
  // Une ligne : comprise, essayée sur une copie, appliquée en un pas ; « message » dit le résultat ou l'erreur
  bool executer(const QString& ligne, QString* message = nullptr);
  // Macro : enregistrement en direct des lignes réussies, ou rejouée d'un coup (un seul pas d'annulation)
  void setEnregistrement(bool oui);
  bool enregistrement() const { return m_enregistre; }
  const prom::Macro& macro() const { return m_macro; }
  bool rejouer(const prom::Macro& m, QString* message = nullptr);
  bool enregistrerMacro(const QString& chemin, const QString& nom);
  bool rejouerFichier(const QString& chemin, QString* message = nullptr);
  // Commande tapée affectée à une touche de l'atelier (« Ctrl+Alt+1 ») ; gardée dans les réglages du poste
  bool affecter(const QString& touche, const QString& ligne);
  QStringList propositions() const;  // commandes probables après la dernière
  QStringList historique() const { return m_historique; }
  QLineEdit* champ() const { return m_champ; }
  QString dernierMessage() const;
  void activer();  // le curseur dans le champ

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void majPropositions();
  void majCompletion(const QString& texte);
  void dire(const QString& m, bool erreur);
  void installerRaccourcis();
  AtelierElectronique& m_atelier;
  QLineEdit* m_champ = nullptr;
  QStringListModel* m_completions = nullptr;
  QLabel* m_message = nullptr;
  QToolButton* m_bouton = nullptr;  // enregistrement de la macro
  std::vector<QPushButton*> m_probables;
  QStringList m_historique;
  int m_rang = -1;  // place dans l'historique (flèches)
  prom::PredicteurCommandes m_predicteur;
  bool m_enregistre = false;
  prom::Macro m_macro;
};

// ---------- 13.3, 13.9 · Console Python ----------
class ConsolePython : public QWidget {
  Q_OBJECT
 public:
  explicit ConsolePython(AtelierElectronique& a, QWidget* parent = nullptr);
  ~ConsolePython() override;
  void setCode(const QString& code);
  QString code() const;
  void basculerPointArret(int ligne);
  std::set<int> pointsArret() const { return m_points; }
  void setPasAPas(bool oui);
  // Exécute le script sur une copie du projet ; ses modifications font un seul pas. « attendre » : la fin du script
  // est attendue (essais) ; sinon il tourne sur un fil, la console restant vivante
  bool executer(bool attendre = false);
  bool enCours() const { return m_enCours; }
  // Script arrêté (point d'arrêt ou pas) : « continuer », « pas » ou « arreter »
  void reprendre(const std::string& choix);
  int ligneArretee() const { return m_ligneArret; }
  const prom::ResultatScript& resultat() const { return m_resultat; }
  QString sortie() const;
  TraceCourbes* trace() const { return m_trace; }
  // Script rangé : nommé, gardé dans les réglages du poste, sur une touche s'il en a une
  bool ranger(const QString& nom, const QString& touche = {});
  QStringList scriptsRanges() const;

 private:
  void fini(prom::ResultatScript r, prom::Projet copie);
  void marquer();
  void arrete(int ligne, const nlohmann::ordered_json& variables);
  AtelierElectronique& m_atelier;
  QPlainTextEdit *m_editeur = nullptr, *m_sortie = nullptr;
  QLabel* m_etat = nullptr;
  QPushButton *m_lancer = nullptr, *m_continuer = nullptr, *m_pas = nullptr, *m_arreter = nullptr;
  QCheckBox* m_pasAPas = nullptr;
  QComboBox* m_ranges = nullptr;
  TraceCourbes* m_trace = nullptr;
  std::set<int> m_points;
  bool m_enCours = false;
  int m_ligneArret = 0;
  prom::ResultatScript m_resultat;
  std::thread m_fil;
  std::atomic<bool> m_arret{false};
  std::mutex m_verrou;
  std::condition_variable m_reveil;
  std::optional<std::string> m_choix;
};

// ---------- 13.4 · Propositions d'un assistant (serveur MCP) ----------
class PanneauPropositions : public QWidget {
  Q_OBJECT
 public:
  explicit PanneauPropositions(AtelierElectronique& a, QWidget* parent = nullptr);
  void relire();  // le fichier des propositions, à côté du projet
  const std::vector<prom::PropositionAssistant>& propositions() const { return m_propositions; }
  bool accepter(int numero, QString* message = nullptr);  // en un pas, le fichier mis à jour
  bool refuser(int numero, const QString& raison = {});
  QString cheminFichier() const;

 private:
  void majListe();
  bool ecrire();
  AtelierElectronique& m_atelier;
  QFileSystemWatcher* m_veille = nullptr;
  QListWidget* m_liste = nullptr;
  QLabel* m_etat = nullptr;
  std::vector<prom::PropositionAssistant> m_propositions;
};

// ---------- 13.15 à 13.17 · Assistant de conception ----------
class PanneauAssistant : public QWidget {
  Q_OBJECT
 public:
  explicit PanneauAssistant(AtelierElectronique& a, QWidget* parent = nullptr);
  void setMoteur(const prom::MoteurAssistant& m);  // gardé dans les réglages du poste
  const prom::MoteurAssistant& moteur() const { return m_moteur; }
  prom::MemoireAssistant memoire() const;
  void setMemoire(const prom::MemoireAssistant& m);
  // Les réponses s'ajoutent à la conversation ; chaque demande rend la réponse (ok faux et l'erreur sinon)
  prom::ReponseAssistant expliquer(const std::set<std::string>& composants);  // vide : la sélection
  prom::ReponseAssistant demander(const prom::ecad::Violation& v);
  prom::ReponseAssistant poser(const QString& question);
  // Fiche technique (PDF) : lue une fois, puis interrogée
  bool lireFiche(const QString& pdf);
  prom::ReponseFiche questionFiche(const QString& question);
  // Simulation confiée : la courbe s'affiche sous la conversation
  prom::ReponseSimulation questionSimulation(const QString& question);
  QString conversation() const;
  TraceCourbes* trace() const { return m_trace; }

 private:
  void ajouter(const QString& qui, const QString& html);
  void lireReglages();
  AtelierElectronique& m_atelier;
  prom::MoteurAssistant m_moteur;
  std::optional<prom::FicheTechnique> m_fiche;
  QComboBox* m_genre = nullptr;
  QLineEdit *m_programme = nullptr, *m_modele = nullptr, *m_question = nullptr;
  QPlainTextEdit* m_memoire = nullptr;
  QCheckBox* m_suspendue = nullptr;
  QTextBrowser* m_conversation = nullptr;
  QLabel* m_ficheLue = nullptr;
  TraceCourbes* m_trace = nullptr;
  QString m_texte;  // la conversation, en HTML
};

// ---------- 13.11 · Éditeur de requêtes ----------
class DialogueRequetes : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueRequetes(AtelierElectronique& a, QWidget* parent = nullptr);
  void setRequete(const QString& r);  // le compte se refait
  QString requete() const;
  bool valide() const { return m_valide; }
  prom::ecad::ObjetsVises vises() const { return m_vises; }
  QString compte() const;
  // Règle d'isolement entre les objets de la requête et ceux de « autre » (vide : le reste), sur les couches
  // extérieures ou toutes ; ajoutée aux contraintes de la carte en un pas
  bool creerRegle(const QString& nom, const QString& autre, double minimumMm, bool exterieures);
  void selectionner();  // les objets visés deviennent la sélection

 private:
  void compter();
  void majListes();
  AtelierElectronique& m_atelier;
  QLineEdit* m_champ = nullptr;
  QLabel* m_compte = nullptr;
  QComboBox *m_historique = nullptr, *m_exemples = nullptr;
  QListWidget* m_favoris = nullptr;
  prom::ecad::HistoriqueRequetes m_memo;
  prom::ecad::ObjetsVises m_vises;
  bool m_valide = false;
};

// ---------- 13.14 · Conception guidée ----------
class DialogueConceptionGuidee : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueConceptionGuidee(AtelierElectronique& a, QWidget* parent = nullptr);
  void regler(double largeur, double hauteur, const std::string& module, const std::vector<std::string>& interfaces);
  // Conçoit, ajoute la conception au projet (un pas) et l'ouvre ; rend le résultat
  prom::ecad::ResultatGuide concevoir();
  QString journal() const;
  // Circuits types proposés pour la conception ouverte, posés d'un clic
  std::vector<prom::ecad::PropositionCircuitType> proposer();
  bool poser(std::size_t i);

 private:
  AtelierElectronique& m_atelier;
  QDoubleSpinBox *m_largeur = nullptr, *m_hauteur = nullptr;
  QComboBox* m_module = nullptr;
  std::vector<std::pair<std::string, QCheckBox*>> m_interfaces;
  QTextBrowser* m_journal = nullptr;
  QListWidget* m_circuits = nullptr;
  std::vector<prom::ecad::PropositionCircuitType> m_propositions;
};
