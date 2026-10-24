// Prométhée : « Remplacer… » un composant ou une sélection par un autre article (lot L3, tâche 3.16 ; SAI-42). On
// choisit l'article, ce qui l'emporte champ par champ (repère, valeur, empreinte, fabricant) et la correspondance des
// broches, par nom ou par numéro ; l'aperçu montre, avant de valider, chaque broche reliée et sa remplaçante, le
// nombre de fils qui restent accrochés, et les broches qui se perdraient (elles bloquent le remplacement). Le tout
// fait une seule commande ; l'empreinte se remplace en place sur le circuit.
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QDialog>
#include <QPixmap>
#include <QWidget>

#include "promethee/remplacement.hpp"

class AtelierElectronique;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QRadioButton;
class QTreeWidget;

class DialogueRemplacer : public QDialog {
  Q_OBJECT
 public:
  DialogueRemplacer(AtelierElectronique& a, std::vector<std::string> composants, QWidget* parent = nullptr);
  // L'article choisi, la règle, et l'aperçu du premier composant
  std::string article() const { return m_article; }
  void choisirArticle(const std::string& id);
  prom::ecad::RegleRemplacement regle() const;
  QTreeWidget* apercu() const { return m_apercu; }
  // Applique (rend le nombre de composants remplacés) ; l'erreur s'écrit dans la fenêtre
  int appliquer();
  // Portée de la mise à jour en masse (4.20) : motifs à jokers sur le repère et la valeur, dans toute la conception ;
  // rend le nombre de composants visés
  int appliquerPortee(const QString& repere, const QString& valeur);
  const std::vector<std::string>& composants() const { return m_composants; }
  // Comparaison en superposition : ancien symbole (A) et nouveau (B), transparence de 0 (A seul) à 100 (B seul)
  void reglerTransparence(int pourcent);
  int transparence() const;
  void basculerAB();
  class Superposition* superposition() const { return m_superposition; }

 private:
  void majApercu();
  void remplirArticles(const QString& filtre);
  AtelierElectronique& m_atelier;
  std::vector<std::string> m_composants;
  std::string m_article;
  QLineEdit* m_cherche;
  QListWidget* m_articles;
  QRadioButton* m_parNom;
  QRadioButton* m_parNumero;
  QCheckBox *m_repere, *m_valeur, *m_empreinte, *m_fabricant;
  QTreeWidget* m_apercu;
  QLabel* m_bilan;
  QPushButton* m_ok;
  QLineEdit *m_porteeRepere, *m_porteeValeur;
  QLabel* m_porteeBilan;
  class Superposition* m_superposition;
  class QSlider* m_transparence;
  QPushButton* m_ab;
};

// Deux images superposées, la seconde fondue sur la première selon la transparence (0 : A seule, 1 : B seule)
class Superposition : public QWidget {
  Q_OBJECT
 public:
  explicit Superposition(QWidget* parent = nullptr);
  void montrer(const QPixmap& a, const QPixmap& b);
  void reglerFondu(double t);
  double fondu() const { return m_t; }
  QImage image() { return grab().toImage(); }

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  QPixmap m_a, m_b;
  double m_t = 0.5;
};
