// Prométhée : panneau Vérifications de l'atelier électronique (lot L2, tâches 2.11 et 2.33 ; S9, S28, P29, P30, E5).
// Les résultats de l'ERC et de la DRC, groupés par cause et dépliables ; un clic sur un problème mène à sa feuille
// ou au circuit, choisit le bon objet (symbole, fil, étiquette, piste, via, zone, empreinte) et centre la vue ;
// F4 mène au suivant. Au clic droit : la correction proposée quand il y en a une, l'exclusion justifiée (code,
// objets, raison, auteur, date, gardée dans le projet et rappelée à la publication), la gravité réglée du code pour
// ce projet, et le retour des exclus. « Vérifier » (F7) lance la vérification complète ; ses résultats restent
// jusqu'au calcul suivant et passent « à refaire » quand le schéma change.
// Licence GPL-3.0-only.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QWidget>

#include "promethee/electronique.hpp"

class AtelierElectronique;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class PanneauVerifications : public QWidget {
  Q_OBJECT
 public:
  explicit PanneauVerifications(AtelierElectronique& a, QWidget* parent = nullptr);
  // Les résultats ont changé (calcul refait, réglages de la conception) : la liste se refait, l'élément choisi reste
  void majResultats();
  // La même, au retour à la boucle d'événements : un calcul déclenché par un clic dans la liste ne la refait pas
  // sous ce clic
  void majPlusTard();
  // F4 : le problème suivant de la liste, choisi et montré ; faux s'il n'y en a pas
  bool suivant();
  // Aller au problème : sa feuille ou le circuit, ses objets choisis, la vue centrée (P29, P30)
  void aller(const prom::ecad::Violation& v, bool erc);
  QTreeWidget* liste() const { return m_liste; }

 private:
  enum class Filtre : int { Tout, Erreurs, Avertissements, Exclus };
  void menuElement(const QPoint& p);
  void exclure(const prom::ecad::Violation& v);
  void regler(const std::string& code, std::optional<prom::ecad::GraviteReglee> g);
  void corriger(const prom::ecad::Violation& v);
  void refaire();  // après un réglage du projet : la vérification complète refaite si elle l'était
  static QString correction(const prom::ecad::Violation& v);  // libellé de la correction proposée, vide sinon
  AtelierElectronique& m_atelier;
  QTreeWidget* m_liste;
  QComboBox* m_filtre;
  QLabel* m_resume;
  QWidget* m_exclusion;  // rangée de saisie de la raison d'une exclusion, sans fenêtre modale
  QLineEdit* m_raison;
  QPushButton* m_valider;
  std::vector<std::pair<prom::ecad::Violation, bool>> m_affichees;  // violation, vraie si ERC (sinon DRC)
  bool m_majPrevue = false;
};
