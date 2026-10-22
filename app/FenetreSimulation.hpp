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
  };
  explicit TraceCourbes(QWidget* parent = nullptr);
  void setCourbes(std::vector<Courbe> c, const QString& axeX, const QString& axeY, bool logX);
  const std::vector<Courbe>& courbes() const { return m_courbes; }
  double curseur(int i) const { return m_curseurs[i]; }
  void setCurseur(int i, double x);
 signals:
  void curseursChanges();

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* e) override;
  void mouseMoveEvent(QMouseEvent* e) override;
  void mouseReleaseEvent(QMouseEvent*) override;

 private:
  QRectF zone() const;
  double versEcranX(double x) const;
  double depuisEcranX(double px) const;
  std::vector<Courbe> m_courbes;
  QString m_axeX, m_axeY;
  bool m_logX = false;
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

 signals:
  void calculTermine();

 private:
  void majChamps();
  void afficher(bool refaireTrace = true);
  prom::ecad::ConceptionElec conceptionReglee() const;
  AtelierElectronique& m_atelier;
  QComboBox* m_type;
  QLineEdit *m_pas, *m_duree, *m_fDebut, *m_fFin, *m_source, *m_dcDebut, *m_dcFin, *m_dcPas;
  QListWidget* m_sondes;
  QPushButton *m_lancer, *m_arreter;
  QLabel* m_etat;
  QTabWidget* m_onglets;
  TraceCourbes* m_trace;
  QPlainTextEdit *m_texteNetlist, *m_console;
  QTableWidget* m_tableMesures;
  QComboBox* m_regle;
  QDoubleSpinBox* m_facteur;
  prom::ecad::NetlistSpice m_netlist;
  prom::ecad::ResultatSimulation m_resultat;
  std::vector<TraceCourbes::Courbe> m_precedentes;
  std::string m_reglee;
  double m_facteurRegle = 1;
  std::shared_ptr<std::atomic<bool>> m_arret;
  std::thread m_fil;
  bool m_enCours = false;
};
