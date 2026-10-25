// Prométhée : microcontrôleurs, FPGA et logique dans l'atelier électronique (lot L15 de la 0.34). Onglet programme
// (sources rangées dans le projet, compilation, écarts avec le schéma qui mènent à la broche, initialisation écrite
// d'après le câblage) ; co-simulation du microcontrôleur avec le laboratoire, terminal série et mise au point ;
// modules et organigrammes ; composants décrits en HDL ; machines à états ; robot sur une piste ; logique
// programmable. Licence GPL-3.0-only.
#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QImage>
#include <QWidget>

#include "promethee/embarque.hpp"
#include "promethee/hdl.hpp"
#include "promethee/micro_simulation.hpp"

class AtelierElectronique;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;
class QTableWidget;
class QTimer;

// Piste et trajet du robot
class VueRobot : public QWidget {
  Q_OBJECT
 public:
  explicit VueRobot(QWidget* parent = nullptr);
  void setPiste(const prom::ecad::PisteRobot& p);
  void setTrajet(std::vector<std::pair<double, double>> t);
  const std::vector<std::pair<double, double>>& trajet() const { return m_trajet; }
  QSize sizeHint() const override { return {520, 300}; }

 protected:
  void paintEvent(class QPaintEvent*) override;

 private:
  QImage m_image;
  std::vector<std::pair<double, double>> m_trajet;
};

class FenetreEmbarque : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreEmbarque(AtelierElectronique& a, QWidget* parent = nullptr);
  ~FenetreEmbarque() override;
  bool montrer(const QString& onglet);  // « programme », « cosimulation », « organigramme », « hdl », « machine », « robot », « logique »

  // ---------- Programme (15.3) ----------
  QPlainTextEdit* editeur() const { return m_editeur; }
  void nouveauProgramme(const QString& nom, const QString& langage, const QString& cible);
  bool enregistrer(QString* erreur = nullptr);  // dans le dossier « programmes » du projet, cité par chemin et SHA-256
  bool compiler();
  const prom::ecad::ResultatCompilation& compilation() const { return m_compilation; }
  const std::vector<prom::ecad::EcartProgramme>& ecarts();  // relus d'après le texte de l'éditeur
  bool montrerEcart(int i);                                   // la broche montrée sur le schéma
  void insererInitialisation();
  QString langageCourant() const;
  QString cibleCourante() const;
  double frequence() const;  // Hz, celle du quartz du microcontrôleur
  void setFrequence(double hz);

  // ---------- Co-simulation, terminal et mise au point (15.4, 15.5) ----------
  bool demarrerCosimulation(QString* erreur = nullptr);
  void arreterCosimulation();
  prom::ecad::CosimulationMicro* micro() const { return m_micro.get(); }
  bool basculerPoint(int ligne);  // point d'arrêt sur une ligne de l'éditeur
  const std::set<int>& points() const { return m_points; }
  void continuer();
  void pasLigne();
  void pasInstruction();
  QString terminal() const;
  void reglerTerminal(int debit);
  void envoyerTerminal(const QString& texte);
  void rafraichir();  // terminal, état du débogueur, registres

  // ---------- Modules et organigrammes (15.6) ----------
  bool poserModule(const QString& id);
  void exempleOrganigramme();
  bool ajouterBloc(const QString& texte);  // un bloc proposé, d'après son texte
  QStringList propositions(const QString& debut) const;
  prom::ecad::CodeOrganigramme convertir(bool python);
  bool compilerOrganigramme();
  const prom::ecad::Organigramme& organigramme() const { return m_organigramme; }

  // ---------- Composants HDL (15.7) ----------
  bool poserHdl(const QString& code, const QString& langage);
  bool cosimulerHdl(const QString& reference, QString* erreur = nullptr);
  prom::ecad::CosimulationHdl* hdl() const { return m_hdl.get(); }

  // ---------- Machines à états (15.10) ----------
  void exempleFeux();
  // Machine, circuit à bascules et GHDL sur « periodes » : nombre de périodes où les trois sont d'accord (−1 :
  // GHDL absent, seules la machine et les bascules comparées)
  int comparerMachine(int periodes);
  bool poserBascules();
  const prom::ecad::MachineEtats& machine() const { return m_machine; }

  // ---------- Robot (15.8) ----------
  bool simulerRobot(double duree, const QString& attendu);
  const prom::ecad::ResultatRobot& resultatRobot() const { return m_robot; }
  VueRobot* vueRobot() const { return m_vueRobot; }

  // ---------- Logique programmable (15.12) ----------
  QString vhdlLogique();
  bool verifierTableGhdl(QString* detail = nullptr);
  bool exporterJedec(const QString& chemin);

 private:
  void construireProgramme(QWidget* page);
  void construireCosimulation(QWidget* page);
  void construireOrganigramme(QWidget* page);
  void construireHdl(QWidget* page);
  void construireMachine(QWidget* page);
  void construireRobot(QWidget* page);
  void construireLogique(QWidget* page);
  void majOrganigramme();
  void majMachine();
  QString dossierProgrammes() const;
  std::string nomFichierSource() const;
  AtelierElectronique& m_atelier;
  QTabWidget* m_onglets = nullptr;
  QLabel* m_etat = nullptr;
  // Programme
  QPlainTextEdit* m_editeur = nullptr;
  QComboBox *m_langage = nullptr, *m_cible = nullptr, *m_frequence = nullptr;
  QLineEdit* m_nom = nullptr;
  QListWidget *m_erreurs = nullptr, *m_listeEcarts = nullptr;
  prom::ecad::ResultatCompilation m_compilation;
  std::vector<prom::ecad::EcartProgramme> m_ecarts;
  // Co-simulation
  std::shared_ptr<prom::ecad::CosimulationMicro> m_micro;
  std::set<int> m_points;
  QPlainTextEdit *m_terminal = nullptr, *m_traces = nullptr;
  QComboBox* m_debit = nullptr;
  QLineEdit* m_envoi = nullptr;
  QLabel* m_etatDebogueur = nullptr;
  QTableWidget* m_registres = nullptr;
  QTimer* m_minuterie = nullptr;
  // Organigramme
  prom::ecad::Organigramme m_organigramme;
  QListWidget* m_blocs = nullptr;
  QLineEdit* m_ajoutBloc = nullptr;
  QComboBox* m_module = nullptr;
  QPlainTextEdit* m_codeOrganigramme = nullptr;
  // HDL
  QPlainTextEdit* m_codeHdl = nullptr;
  QComboBox* m_langageHdl = nullptr;
  std::shared_ptr<prom::ecad::CosimulationHdl> m_hdl;
  // Machine
  prom::ecad::MachineEtats m_machine;
  QTableWidget *m_tableTransitions = nullptr, *m_sequences = nullptr;
  QPlainTextEdit* m_equations = nullptr;
  // Robot
  VueRobot* m_vueRobot = nullptr;
  QLabel* m_resultatRobot = nullptr;
  prom::ecad::ResultatRobot m_robot;
  // Logique
  QPlainTextEdit* m_sortieLogique = nullptr;
};
