// Prométhée : panneau des écarts entre le schéma et le circuit (lot L3, tâches 3.8 et 3.19 ; A4, PLA-16, PLA-17).
// Un écart par ligne : sa case (appliqué ou non par « Appliquer les écarts cochés »), le composant, ce qui diffère, et
// deux boutons, Appliquer et Voir (l'objet choisi dans les deux vues et centré). « Tout appliquer » fait une seule
// commande ; un filtre trie par genre. Une pastille orangée « 3 écarts avec le schéma » reste dans la barre d'état
// tant qu'il en reste ; un clic ouvre le panneau. Le mode se règle ici : automatique pour les changements sans risque
// (par défaut) ou tout sur confirmation. F8 devient « Synchroniser maintenant » : le panneau s'ouvre, la liste se
// montre, et tout s'applique. Ce qui est appliqué est gardé dans le rapport du panneau et dans les messages.
// Licence GPL-3.0-only.
#pragma once

#include <set>
#include <string>
#include <vector>

#include <QWidget>

#include "promethee/synchronisation.hpp"

class AtelierElectronique;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QToolButton;
class QTreeWidget;

class PanneauEcarts : public QWidget {
  Q_OBJECT
 public:
  explicit PanneauEcarts(AtelierElectronique& a, QWidget* parent = nullptr);
  // La conception a changé : la liste et la pastille se refont (au retour à la boucle d'événements)
  void majPlusTard();
  void maj();
  const std::vector<prom::ecad::EcartSynchro>& ecarts() const { return m_ecarts; }
  // Applique les écarts cochés, ou tous, en une seule commande ; rend le nombre d'écarts appliqués
  int appliquer(bool tous);
  int appliquerUn(const prom::ecad::EcartSynchro& e);
  void voir(const prom::ecad::EcartSynchro& e);
  // F8 : le panneau ouvert, la liste montrée, puis tout appliqué
  int synchroniserMaintenant();
  QTreeWidget* liste() const { return m_liste; }
  QToolButton* pastille() const { return m_pastille; }  // dans la barre d'état
  QListWidget* rapport() const { return m_rapport; }

 signals:
  void ouvrirDemande();

 private:
  void remplir();
  void consigner(const std::vector<prom::ecad::EcartSynchro>& faits);
  AtelierElectronique& m_atelier;
  QTreeWidget* m_liste;
  QComboBox* m_filtre;
  QLabel* m_resume;
  QPushButton* m_cochees;
  QPushButton* m_tout;
  QRadioButton* m_auto;
  QRadioButton* m_confirmation;
  QListWidget* m_rapport;
  QToolButton* m_pastille;
  std::vector<prom::ecad::EcartSynchro> m_ecarts;
  std::set<std::string> m_decoches;  // clés des écarts décochés (gardées d'une mise à jour à l'autre)
  bool m_majPrevue = false;
};
