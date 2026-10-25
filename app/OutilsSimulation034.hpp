// Prométhée : outils de simulation de l'atelier électronique (lot L11 de la 0.34) — calculateurs de l'électronique en
// fiches à champs bornés (11.7, 11.17 à 11.19) et modèle de simulation d'un composant : fiche de modèle intégré,
// modèle tiré de la fiche technique sur son fil, tampon IBIS, fichier de modèle d'un autre simulateur (11.5, 11.11,
// 11.12), rattaché à l'article du composant en une commande annulable. Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QDialog>
#include <QWidget>

#include "promethee/calculateurs_electronique.hpp"
#include "promethee/modeles_simulation.hpp"

class AtelierElectronique;
class QComboBox;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QTableWidget;
class QTreeWidget;

// Calculateurs de l'électronique : familles, fiche, résultat ; un champ hors bornes est refusé, pas ramené
class FenetreCalculateurs : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreCalculateurs(QWidget* parent = nullptr);
  bool choisir(const std::string& id);
  // Valeurs posées dans les champs (dans leur ordre), puis calcul ; rend faux si l'une est hors bornes
  bool regler(const std::vector<double>& valeurs);
  QString resultat() const;  // texte du résultat affiché
  std::string choisi() const { return m_id; }

 private:
  void calculer();
  QTreeWidget* m_liste = nullptr;
  QLabel *m_titre = nullptr, *m_resultat = nullptr;
  QWidget* m_form = nullptr;
  QFormLayout* m_champs = nullptr;
  std::vector<std::function<double()>> m_lire;
  std::vector<std::function<void(double)>> m_ecrire;
  std::string m_id;
};

// Modèle de simulation du composant choisi
class DialogueModeleSimulation : public QDialog {
  Q_OBJECT
 public:
  DialogueModeleSimulation(AtelierElectronique& a, std::string composant, QWidget* parent = nullptr);
  // Fiche de modèle intégré (« diode »…) aux valeurs données : le texte proposé, ou vide et la raison
  QString ecrireFiche(const std::string& fiche, const std::vector<double>& valeurs, QString* refus = nullptr);
  // Diode ajustée sur les points (courant, tension), sur un fil ; rend le modèle (attend la fin si « attendre »)
  prom::ecad::AjustementModele ajusterDiode(const std::vector<prom::ecad::PointFiche>& points, bool attendre);
  bool lireIbis(const QString& chemin);
  // Fichier de modèle d'un autre simulateur (« ps », « lt », « hs » ou vide pour ngspice) : proposé avec ses adaptations
  bool lireModele(const QString& chemin, const QString& compatibilite);
  prom::ecad::AjustementModele tamponIbis(const QString& modele, int coin);
  // Le modèle proposé devient celui de l'article du composant (créé dans le projet s'il n'en a pas)
  bool rattacher(const prom::ecad::ModeleSpice& m);
  const prom::ecad::ModeleSpice& propose() const { return m_propose; }

 private:
  void proposer(const prom::ecad::ModeleSpice& m, const QString& note);
  AtelierElectronique& m_atelier;
  std::string m_composant;
  prom::ecad::ModeleSpice m_propose;
  std::optional<prom::ecad::FichierIbis> m_ibis;
  QPlainTextEdit* m_texte = nullptr;
  QLabel* m_note = nullptr;
  QComboBox *m_fiche = nullptr, *m_modelesIbis = nullptr, *m_coin = nullptr;
  QFormLayout* m_champsFiche = nullptr;
  QTableWidget* m_points = nullptr;
  std::vector<std::function<double()>> m_lireFiche;
};
