// Prométhée : apprendre l'atelier électronique (lot L2, tâche 2.17 ; AUT-11, ERG-13, ERG-16). Un panneau à deux
// onglets. « Parcours » : la carte de l'étape en cours du parcours « Ta première carte » (ce qu'on y apprend, où
// cliquer, le mot du lexique), qui passe d'elle-même à la suivante quand la conception montre l'étape faite, et
// ouvre l'onglet de l'atelier où elle se passe ; au premier lancement, les préférences des cartes neuves (unité,
// profil de fabricant, dossier de la bibliothèque personnelle). « Lexique » : les mots du métier, cherchés pendant
// la frappe.
// Licence GPL-3.0-only.
#pragma once

#include <string>

#include <QString>
#include <QWidget>

#include "promethee/electronique.hpp"

class AtelierElectronique;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTabWidget;

class PanneauApprendre : public QWidget {
  Q_OBJECT
 public:
  explicit PanneauApprendre(AtelierElectronique& a, QWidget* parent = nullptr);
  // Une carte neuve, ajoutée au projet ouvert (un pas d'annulation), et le parcours montré à sa première étape
  void commencer(const std::string& id);
  void quitter();
  void majEtape();  // la conception a changé : l'étape faite passe à la suivante
  const std::string& parcours() const { return m_parcours; }
  int etape() const { return m_etape; }
  QTabWidget* onglets() const { return m_onglets; }
  QLabel* titreEtape() const { return m_titre; }
  QLineEdit* rechercheLexique() const { return m_cherche; }
  QListWidget* lexique() const { return m_lexique; }
  // Premier lancement (ERG-16) : le groupe des préférences montré ; « Valider » les garde et le referme
  void montrerPremierLancement();
  static bool premierLancementFait();
  // Préférences des cartes neuves, réglées au premier lancement : unité d'affichage et profil de fabricant
  static void appliquerPreferences(prom::ecad::ConceptionElec& c);

 private:
  void montrer();
  void remplirLexique(const QString& filtre);
  AtelierElectronique& m_atelier;
  std::string m_parcours;
  std::string m_conception;  // UUID de la carte du parcours
  int m_etape = 0;
  QTabWidget* m_onglets;
  QGroupBox* m_premier;
  QComboBox *m_unite, *m_profil;
  QLineEdit* m_dossier;
  QWidget* m_carte;
  QLabel *m_avancement, *m_titre, *m_pourquoi, *m_comment, *m_mot, *m_etat;
  QProgressBar* m_barre;
  QPushButton *m_commencer, *m_precedent, *m_suivant, *m_quitter;
  QLineEdit* m_cherche;
  QListWidget* m_lexique;
};
