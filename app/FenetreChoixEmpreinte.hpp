// Prométhée : fenêtre « Choisir une empreinte » (lot L4, tâches 4.6, 4.7 ; EMP-01, EMP-02, EMP-04, EMP-14, P7, P8,
// P9, B12). Non modale, au centre : la liste d'empreintes devient lisible, filtrée et triable. Par défaut le filtre
// de compatibilité ne garde que les empreintes de la même sorte que celle pour laquelle le symbole est écrit, au bon
// nombre de pastilles (une inductance : les six empreintes d'inductance) ; décoché, toutes celles au bon nombre de
// pastilles. Filtre texte, tri par colonne, aperçu, application en série aux composants visés (la sélection),
// génération IPC-7351B depuis les cotes d'une fiche.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QDialog>
#include <QWidget>

#include "promethee/articles.hpp"

class AtelierElectronique;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class FenetreChoixEmpreinte : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreChoixEmpreinte(AtelierElectronique& a, QWidget* parent = nullptr);
  // Composants visés (uuid) : l'empreinte choisie leur est appliquée en série ; la liste suit le premier
  void viser(const std::vector<std::string>& composants);
  const std::vector<std::string>& vises() const { return m_vises; }
  void refaire();
  QCheckBox* filtreCompatibles() const { return m_compatibles; }
  QLineEdit* filtreTexte() const { return m_texte; }
  QTableWidget* table() const { return m_table; }
  QPushButton* boutonAppliquer() const { return m_appliquer; }
  std::vector<std::string> empreintesMontrees() const;
  // Applique l'empreinte de la ligne aux composants visés, en un pas ; rend le nombre de composants changés
  int appliquer(int ligne = -1);
  void ouvrirGenerateur();

 private:
  void montrerApercu();
  AtelierElectronique& m_atelier;
  std::vector<std::string> m_vises;
  QLabel* m_titre;
  QCheckBox* m_compatibles;
  QLineEdit* m_texte;
  QTableWidget* m_table;
  QLabel* m_apercu;
  QLabel* m_bilan;
  QPushButton* m_appliquer;
};

// Générateur IPC-7351B : cotes de la fiche (portée, patte, largeur, pas, corps), gabarit, densité ; pastilles
// calculées, noms IPC et KiCad, aperçu ; l'empreinte se range dans la bibliothèque personnelle
class DialogueIPC : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueIPC(QWidget* parent = nullptr);
  prom::ecad::CotesBoitier cotes() const;
  void setCotes(const prom::ecad::CotesBoitier& c);
  prom::ecad::DensiteIPC densite() const;
  prom::ecad::DefEmpreinte empreinte() const { return prom::ecad::empreinteIPC7351(cotes(), densite()); }
  QLabel* bilan() const { return m_bilan; }

 private:
  void majApercu();
  QComboBox *m_gabarit, *m_densite;
  QSpinBox* m_broches;
  QCheckBox* m_quatre;
  QDoubleSpinBox *m_pas, *m_lMin, *m_lMax, *m_tMin, *m_tMax, *m_wMin, *m_wMax, *m_corpsL, *m_corpsW, *m_hauteur, *m_plot;
  QLabel *m_bilan, *m_apercu;
};
