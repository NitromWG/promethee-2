// Prométhée : laboratoire de simulation de l'atelier électronique (lot L14 de la 0.34). Simulation continue au rythme
// du temps réel, boutons, interrupteurs et générateurs réglés pendant le calcul, LED et afficheurs qui suivent,
// retour visuel sur le schéma (14.2 à 14.4) ; oscilloscope, multimètre ; instruments Python posés au schéma (14.5) ;
// analyseur logique et décodeurs (14.6) ; analyse symbolique et analyses natives (14.7) ; autres moteurs et mesures
// réelles (14.8) ; analyseur de réseau et Touchstone (14.10) ; abaisseur, boucle et arbre des rails (14.14).
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <complex>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QWidget>

#include "promethee/alimentations.hpp"
#include "promethee/analyses_natives.hpp"
#include "promethee/moteurs_tiers.hpp"
#include "promethee/parametres_s.hpp"
#include "promethee/simulation_continue.hpp"

class AtelierElectronique;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTimer;
class QVBoxLayout;
class TraceCourbes;

// Afficheur à sept segments dessiné (bits a à g)
class AfficheurSegments : public QWidget {
  Q_OBJECT
 public:
  explicit AfficheurSegments(QWidget* parent = nullptr);
  void setSegments(std::uint8_t s);
  std::uint8_t segments() const { return m_segments; }
  QSize sizeHint() const override { return {46, 70}; }

 protected:
  void paintEvent(class QPaintEvent*) override;

 private:
  std::uint8_t m_segments = 0;
};

class FenetreLaboratoire : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreLaboratoire(AtelierElectronique& a, QWidget* parent = nullptr);
  ~FenetreLaboratoire() override;
  bool montrer(const QString& onglet);  // « continu », « logique », « symbolique », « reseau », « alimentation », « mesures »

  // ---------- Simulation continue ----------
  bool demarrer(QString* erreur = nullptr);
  void arreter();
  bool enCours() const;
  void setVitesse(double v);
  prom::ecad::SimulationContinue& session() { return *m_session; }
  QPushButton* commande(const QString& reference) const;      // bouton ou interrupteur du schéma
  QDoubleSpinBox* frequence(const QString& reference) const;  // générateur (source « externe »)
  int chiffre(const QString& reference) const;                // afficheur à sept segments (−1 éteint)
  bool ledAllumee(const QString& reference) const;
  TraceCourbes* oscilloscope() const { return m_oscillo; }
  void reglerOscilloscope(const QString& voie1, const QString& voie2, double secondesParDivision);
  void reglerMultimetre(const QString& reseau, int mode);  // 0 continu, 1 efficace alternatif, 2 fréquence
  double mesureMultimetre() const { return m_mesure; }
  void rafraichir();  // indicateurs, instruments et retour visuel tout de suite
  // Participants de la prochaine simulation continue (microcontrôleur émulé, composant HDL : 15.4, 15.7), le
  // composant « reference » co-simulé en « genre » (« micro », « hdl ») ; consommés par demarrer()
  void ajouterParticipant(std::shared_ptr<prom::ecad::ParticipantContinu> p, const std::string& reference, const std::string& genre);
  const prom::ecad::NetlistSpice& netlist() const { return m_netlist; }
  void setPas(double secondes);

  // ---------- Instruments posés au schéma (14.5) ----------
  std::map<QString, double> mesurerInstruments();

  // ---------- Analyseur logique (14.6) ----------
  QString decoderI2C(const QString& sda, const QString& scl);

  // ---------- Symbolique et analyses natives (14.7) ----------
  bool calculerSymbolique(const QString& entree, const QString& sortie);
  QString formule() const;
  QString latex() const;
  std::vector<std::complex<double>> poles() const { return m_poles; }
  QString analyseNative(int genre);  // 0 transfert, 1 pôles et zéros, 2 bruit, 3 Fourier

  // ---------- Analyseur de réseau (14.10) ----------
  bool analyserReseau(const QString& port1, const QString& port2, double fDebut = 1e3, double fFin = 1e9);
  const std::optional<prom::ecad::ParametresS>& parametresS() const { return m_s; }
  bool exporterTouchstone(const QString& chemin);

  // ---------- Alimentation (14.14) ----------
  bool concevoirAbaisseur(double vEntree, double vSortie, double courant);
  const prom::ecad::ConceptionAbaisseur& abaisseur() const { return m_abaisseur; }
  double margeSimulee() const { return m_marge; }
  bool poserAbaisseur();
  QString arbreRails(const std::map<std::string, double>& charges);

  // ---------- Mesures et autres moteurs (14.8) ----------
  bool importerMesure(const QString& chemin);
  int courbesMesurees() const { return static_cast<int>(m_mesures.size()); }
  QString moteurs() const;
  double ecartMesureSimulation(const QString& reseau);  // écart maximal relatif, après une simulation transitoire

 private:
  void construireContinu(QWidget* page);
  void construireCommandes();
  void construireLogique(QWidget* page);
  void construireSymbolique(QWidget* page);
  void construireReseau(QWidget* page);
  void construireAlimentation(QWidget* page);
  void construireMesures(QWidget* page);
  void majReseaux();
  std::string sonde(const QString& reseau) const;
  AtelierElectronique& m_atelier;
  QTabWidget* m_onglets = nullptr;
  std::unique_ptr<prom::ecad::SimulationContinue> m_session;
  std::vector<std::shared_ptr<prom::ecad::ParticipantContinu>> m_participants, m_actifs;
  std::map<std::string, std::string> m_cosimulation;
  prom::ecad::NetlistSpice m_netlist;
  QTimer* m_minuterie = nullptr;
  int m_tours = 0;
  QLabel *m_temps = nullptr, *m_multimetre = nullptr, *m_etat = nullptr;
  QDoubleSpinBox *m_vitesse = nullptr, *m_pas = nullptr, *m_secDiv = nullptr;
  QComboBox *m_voie1 = nullptr, *m_voie2 = nullptr, *m_reseauMulti = nullptr, *m_modeMulti = nullptr;
  QWidget* m_commandes = nullptr;
  QVBoxLayout* m_dispositionCommandes = nullptr;
  std::map<QString, QPushButton*> m_boutons;
  std::map<QString, QDoubleSpinBox*> m_frequences;
  std::map<QString, QComboBox*> m_formes;
  std::map<QString, QDoubleSpinBox*> m_amplitudes;
  std::map<QString, AfficheurSegments*> m_afficheurs;
  std::map<QString, QLabel*> m_leds;
  std::map<QString, std::array<std::string, 8>> m_vecteursAfficheurs;  // segments a à g puis commun
  std::map<QString, std::string> m_vecteursLed;                        // courant de la diode
  TraceCourbes* m_oscillo = nullptr;
  double m_mesure = 0;
  // Logique
  QComboBox *m_sda = nullptr, *m_scl = nullptr;
  QPlainTextEdit* m_trames = nullptr;
  // Symbolique
  QComboBox *m_entree = nullptr, *m_sortie = nullptr;
  QLabel* m_formule = nullptr;
  QLineEdit* m_latex = nullptr;
  QPlainTextEdit* m_natif = nullptr;
  std::optional<prom::ecad::FonctionSymbolique> m_fonction;
  std::vector<std::complex<double>> m_poles;
  // Réseau
  QComboBox *m_port1 = nullptr, *m_port2 = nullptr;
  TraceCourbes* m_traceS = nullptr;
  std::optional<prom::ecad::ParametresS> m_s;
  // Alimentation
  QDoubleSpinBox *m_vin = nullptr, *m_vout = nullptr, *m_iout = nullptr;
  QPlainTextEdit* m_rapportAlim = nullptr;
  prom::ecad::ConceptionAbaisseur m_abaisseur;
  prom::ecad::DemandeAbaisseur m_demande;
  double m_marge = 0;
  // Mesures
  QPlainTextEdit* m_rapportMesures = nullptr;
  TraceCourbes* m_traceMesures = nullptr;
  std::vector<prom::ecad::CourbeMesuree> m_mesures;
};
