// Prométhée : gestionnaire de contraintes (lot L8, tâches 5.12 et 8.24 ; DRC-27 à DRC-30). Fenêtre non modale,
// commune au schéma et au circuit : les règles de la carte et de chaque classe de réseaux (largeur, isolement, via)
// en tableau, l'affectation des réseaux aux classes en dessous. Les deux pages lisent le même modèle : une règle
// changée depuis le schéma l'est au circuit, et l'inverse ; chaque changement est un seul pas d'annulation.
// Licence GPL-3.0-only.
#pragma once

#include <string>

#include <QWidget>

class AtelierElectronique;
class QLabel;
class QTableWidget;

class GestionnaireContraintes : public QWidget {
  Q_OBJECT
 public:
  enum Colonne { Largeur = 0, Isolement, ViaDiametre, ViaPercage, NbColonnes };
  explicit GestionnaireContraintes(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();  // relit les règles de la conception courante (sans rien changer)
  QTableWidget* regles() const { return m_regles; }
  QTableWidget* affectations() const { return m_affectations; }
  // Ligne 0 : toute la carte ; ligne i : classe i − 1. Valeur en mm. Faux si refusée (hors bornes, lecture seule)
  bool changer(int ligne, Colonne colonne, double mm);
  // Réseau rangé dans une classe (vide : retiré de toute classe), en un pas
  bool affecter(const std::string& reseau, const std::string& classe);
  bool ajouterClasse();
  QString origine() const;  // « schéma » ou « circuit » : la page d'où il a été ouvert

 private:
  void editee(int ligne, int colonne);
  AtelierElectronique& m_atelier;
  QTableWidget* m_regles;
  QTableWidget* m_affectations;
  QLabel* m_etat;
  bool m_refait = false;
};
