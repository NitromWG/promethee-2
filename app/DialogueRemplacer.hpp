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
};
