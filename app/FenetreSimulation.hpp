// Prométhée : fenêtre de simulation (lot L11 ; SIM-01 à SIM-04, SIM-06, SIM-12 à SIM-16, SIM-33, SIM-34). Non
// modale : l'analyse et ses réglages, les sondes (réseaux nommés du schéma), le calcul sur son fil avec Arrêter, les
// courbes avec deux curseurs, les mesures (fréquence, coupure à −3 dB, point de repos), la netlist modifiable pour un
// essai sans toucher au schéma et la console de ngspice. Un composant se règle pendant la simulation : la courbe
// précédente reste en teinte atténuée sous la nouvelle.
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QColor>
#include <QWidget>

#include "promethee/simulation.hpp"
#include "promethee/simulation_projet.hpp"

class AtelierElectronique;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTabWidget;

// Tracé de courbes : axes gradués, échelle logarithmique des fréquences, légende, deux curseurs à glisser
class TraceCourbes : public QWidget {
  Q_OBJECT
 public:
  struct Courbe {
    QString nom;
    std::vector<double> x, y;
    QColor couleur;
    bool attenuee = false;
    std::vector<std::pair<std::size_t, std::size_t>> surlignes;  // tronçons hors de la bande de la référence (11.15)
  };
  explicit TraceCourbes(QWidget* parent = nullptr);
  void setCourbes(std::vector<Courbe> c, const QString& axeX, const QString& axeY, bool logX);
  const std::vector<Courbe>& courbes() const { return m_courbes; }
  std::vector<Courbe>& courbes() { return m_courbes; }
  double curseur(int i) const { return m_curseurs[i]; }
  void setCurseur(int i, double x);
  // Axe vertical logarithmique, tracé XY (les courbes suivantes en fonction de la première) (11.13)
  void setLogY(bool v);
  void setXY(bool v);
  bool logY() const { return m_logY; }
  bool xy() const { return m_xy; }
  // Exports (11.13) : image PNG, dessin SVG écrit par Prométhée, CSV des courbes tracées
  bool exporterPng(const QString& chemin);
  std::string svg() const;
  std::string csv() const;
  // Point de l'écran d'une valeur, et courbe passant à moins de « tolerance » pixels d'un point (−1 : aucune)
  QPointF versEcran(double x, double y) const;
  int courbeA(const QPointF& p, double tolerance = 5) const;
 signals:
  void curseursChanges();
  void courbeCliquee(const QString& nom);  // un clic sur la courbe désigne son fil (11.14)

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent*) override;

 private:
  QRectF zone() const;
  double versEcranX(double x) const;
  double depuisEcranX(double px) const;
  void cadrer();
  std::vector<Courbe> m_courbes;
  QString m_axeX, m_axeY;
  bool m_logX = false, m_logY = false, m_xy = false;
  double m_x0 = 0, m_x1 = 1, m_y0 = 0, m_y1 = 1;
  double m_curseurs[2] = {0, 0};
  int m_tenu = -1;
};

class FenetreSimulation : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreSimulation(AtelierElectronique& a, QWidget* parent = nullptr);
  ~FenetreSimulation() override;
  void refaire();  // relit la conception : réseaux sondables, netlist
  void setAnalyse(const prom::ecad::AnalyseSpice& a);
  prom::ecad::AnalyseSpice analyse() const;
  void sonder(const std::vector<std::string>& reseaux);  // seules ces sondes cochées
  void lancer();                                         // calcul sur la conception (réglage compris)
  void lancerNetlist(const QString& texte);              // calcul de la netlist telle qu'écrite
  void arreter();
  bool enCours() const { return m_enCours; }
  const prom::ecad::ResultatSimulation& resultat() const { return m_resultat; }
  const prom::ecad::NetlistSpice& netlist() const { return m_netlist; }
  TraceCourbes* trace() const { return m_trace; }
  QString mesures() const;  // le texte de l'onglet Mesures
  // Réglage pendant la simulation (11.6) : la valeur du composant multipliée par « facteur », sans toucher au schéma
  void regler(const std::string& reference, double facteur);
  bool appliquerReglage();  // la valeur réglée écrite dans le schéma, en un pas
  // 0.34 (11.8, 11.9, 11.13 à 11.15)
  prom::ecad::OptionsSimulation& options() { return m_options; }
  void setCompatibilite(const std::string& c);
  void ajouterExpression(const QString& expression);  // courbe calculée, tracée à chaque calcul
  std::vector<prom::ecad::LignePreparation> preparer();  // remplit l'onglet Préparation
  void essayerRemedes();                                // après un échec, sur le fil de calcul
  const prom::ecad::DiagnosticEchec& diagnostic() const { return m_diagnostic; }
  QString remedeRetenu() const { return m_retenu; }
  // Classeur gardé dans la conception (une commande annulable par changement)
  prom::ecad::ClasseurSimulation classeur() const;
  bool enregistrerOnglet(const QString& nom);  // réglages, sondes, courbes calculées et mesures de l'analyse en cours
  bool ouvrirOnglet(const QString& nom);
  bool figerResultat(const QString& nom);      // le dernier résultat, figé dans le classeur
  bool prendreReference(const QString& fige, const QString& courbe, double bande);
  prom::ecad::Comparaison comparer();          // à la référence de l'onglet ouvert, verdict gardé
  bool poserCourbeSurFeuille();                // image du tracé posée sur la feuille, refaite à chaque calcul
  std::vector<std::string> console(const QString& commande);
  QString ongletOuvert() const { return m_onglet; }
  // Partie simulée seule (11.8) : 0 tout le schéma, 1 la feuille affichée, 2 la sélection ; les liaisons coupées sont
  // proposées (un réseau d'alimentation nommé prend sa tension) puis se règlent dans l'onglet Partie
  void setPartie(int genre);
  bool setBouclage(const std::string& reseau, prom::ecad::BouclageLiaison b, double valeur = 0);

 signals:
  void calculTermine();

 private:
  void majChamps();
  void afficher(bool refaireTrace = true);
  prom::ecad::ConceptionElec conceptionReglee() const;
  AtelierElectronique& m_atelier;
  QComboBox* m_type;
  QLineEdit *m_pas, *m_duree, *m_fDebut, *m_fFin, *m_source, *m_dcDebut, *m_dcFin, *m_dcPas, *m_points = nullptr;
  prom::ecad::AnalyseSpice m_analyse;  // dernière analyse posée : ses réglages sans champ y restent
  std::string m_conceptionVue;          // UUID de la conception dont les réglages sont ceux de la fenêtre
  QListWidget* m_sondes;
  QPushButton *m_lancer, *m_arreter;
  QLabel* m_etat;
  QTabWidget* m_onglets;
  TraceCourbes* m_trace;
  QPlainTextEdit *m_texteNetlist, *m_console;
  QTableWidget* m_tableMesures;
  QComboBox* m_regle;
  QDoubleSpinBox* m_facteur;
  void majPreparation(const std::vector<prom::ecad::LignePreparation>& l);
  void majImagesCourbes();
  void majLiaisons();
  QComboBox* m_compat = nullptr;
  QComboBox* m_partie = nullptr;
  QTableWidget* m_tableLiaisons = nullptr;
  QListWidget* m_expressions = nullptr;
  QTableWidget* m_tablePreparation = nullptr;
  QListWidget* m_listeClasseur = nullptr;
  QLabel* m_verdict = nullptr;
  QPushButton* m_boutonRemedes = nullptr;
  QLineEdit* m_ligneConsole = nullptr;
  prom::ecad::OptionsSimulation m_options;
  prom::ecad::DiagnosticEchec m_diagnostic;
  QString m_retenu, m_onglet;
  prom::ecad::NetlistSpice m_netlist;
  prom::ecad::ResultatSimulation m_resultat;
  std::vector<TraceCourbes::Courbe> m_precedentes;
  std::string m_reglee;
  double m_facteurRegle = 1;
  std::shared_ptr<std::atomic<bool>> m_arret;
  std::thread m_fil;
  bool m_enCours = false;
};
