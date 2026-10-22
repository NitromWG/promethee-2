// Prométhée : fenêtre des réglages (lot L1, tâche 1.19 ; lot L2, tâches 2.31 et 2.32 : ERG-19, ERG-20, ERG-36, ERG-38,
// ERG-39). Une liste de pages à gauche, la page à droite ; les réglages valent aussitôt, sans bouton à valider. Les
// pages viennent des réglages déclarés une fois (RegistreReglages) : une recherche en tête trouve un réglage par son
// nom ou ses mots voisins et l’ouvre surligné. Le ruban a sa page ; Apparence règle chaque couleur des canevas et les
// fichiers de thème ; Session exporte, relit et remet à zéro tous les réglages.
// Licence GPL-3.0-only.
#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;

class QLineEdit;
class QVBoxLayout;
struct Reglage;

class FenetreReglages : public QDialog {
  Q_OBJECT
 public:
  explicit FenetreReglages(QWidget* parent = nullptr);
  void montrerPage(const QString& nom);  // nom d'objet de la page : « page:souris »
  // Le réglage de cette clé : sa page ouverte, son champ surligné et amené dans la vue (2.32, ERG-38)
  void montrerReglage(const QString& cle);
  QWidget* surligne() const { return m_surligne; }  // essais : le dernier champ surligné

 private:
  QWidget* pageRuban();
  QWidget* pageDeclaree(const QString& titre);  // les réglages déclarés de cette page (RegistreReglages)
  QWidget* champ(const Reglage& r);
  void ajouterCarteMarques(QVBoxLayout* col);
  void ajouterCouleurs(QVBoxLayout* col);
  void ajouterFichiers(QVBoxLayout* col);
  void ajouterPage(const QString& titre, QWidget* page);
  QListWidget* m_liste;
  QStackedWidget* m_pages;
  QLineEdit* m_recherche;
  QListWidget* m_trouves;
  QWidget* m_surligne = nullptr;
};
