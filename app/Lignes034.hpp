// Prométhée : schéma de lignes et topologie des réseaux routés (lot L18 de la 0.34 ; 18.4), schéma déduit d'une carte
// et retouches d'une révision fabriquée (lot L17 ; 17.6), dans l'atelier électronique. Licence GPL-3.0-only.
#pragma once

#include <QWidget>

#include "promethee/schema_deduit.hpp"
#include "promethee/topologie_lignes.hpp"

class AtelierElectronique;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;

// Topologie prévue et extraite, superposées : ligne principale à l'échelle, branches, vias et leurs moignons
class VueTopologie : public QWidget {
  Q_OBJECT
 public:
  explicit VueTopologie(QWidget* parent = nullptr);
  void setTopologies(const prom::ecad::SchemaLignes& prevue, const prom::ecad::SchemaLignes& extraite);
  QSize sizeHint() const override { return {720, 260}; }

 protected:
  void paintEvent(class QPaintEvent*) override;

 private:
  prom::ecad::SchemaLignes m_prevue, m_extraite;
};

class FenetreLignes : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreLignes(AtelierElectronique& a, QWidget* parent = nullptr);
  void recharger();
  void choisirReseau(const QString& reseau);
  prom::ecad::SchemaLignes prevue() const;          // telle que la fenêtre la montre
  void setPrevue(const prom::ecad::SchemaLignes& s);
  bool enregistrer();                              // dans la carte, en un pas
  const prom::ecad::SchemaLignes& extraite() const { return m_extraite; }
  int extraireEtComparer();                        // rend le nombre d'écarts listés
  const std::vector<prom::ecad::EcartTopologie>& ecarts() const { return m_ecarts; }
  double simuler(double moignon);                  // tension maximale au dernier récepteur (V), NaN sans ngspice
  bool versRegles();

 private:
  AtelierElectronique& m_atelier;
  QComboBox* m_reseau = nullptr;
  QComboBox* m_topologie = nullptr;
  QLineEdit* m_ordre = nullptr;
  QDoubleSpinBox* m_moignon = nullptr;
  QDoubleSpinBox* m_moignonVia = nullptr;
  QTableWidget* m_elements = nullptr;
  QListWidget* m_liste = nullptr;
  VueTopologie* m_vue = nullptr;
  QLabel* m_etat = nullptr;
  prom::ecad::SchemaLignes m_extraite;
  std::vector<prom::ecad::EcartTopologie> m_ecarts;
};

class FenetreRetouches : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreRetouches(AtelierElectronique& a, QWidget* parent = nullptr);
  void recharger();
  bool ajouter(const prom::ecad::Retouche& r);  // dans la carte, en un pas
  int porterAuSchema();
  QString liste(const QString& numeroSerie) const;
  std::vector<prom::ecad::Retouche> proposees(int revisionFabriquee) const;  // pour la conception ouverte
  bool accepter(int revisionFabriquee, int rang);

 private:
  AtelierElectronique& m_atelier;
  QListWidget* m_liste = nullptr;
  QComboBox* m_genre = nullptr;
  QLineEdit *m_a = nullptr, *m_b = nullptr, *m_valeur = nullptr, *m_series = nullptr;
  QComboBox* m_fabriquee = nullptr;
  QListWidget* m_proposees = nullptr;
  QLabel* m_etat = nullptr;
};
