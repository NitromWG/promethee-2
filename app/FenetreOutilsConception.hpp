// Prométhée : outils de conception (lots L12 à L19). Une fenêtre non modale, un onglet par discipline : variantes et
// coût, revue automatique, comparaison avec un autre fichier, schéma de système, notes, historique local, mise en
// service et fiabilité, intégrité, logique numérique, fabrication à l'atelier, équipe et exigences. Chaque onglet
// lit la conception courante et rend son rapport ; les actions qui modifient passent par le document (un pas
// d'annulation chacune). Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <map>

#include <QString>
#include <QWidget>

class AtelierElectronique;
class QComboBox;
class QSpinBox;
class QTabWidget;
class QTextBrowser;

class FenetreOutilsConception : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreOutilsConception(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();  // tous les rapports relus
  // Onglets (essais) : « variantes », « revue », « comparer », « systeme », « notes », « historique », « essais »,
  // « integrite », « logique », « atelier », « exigences »
  bool montrer(const QString& onglet);
  QString rapport(const QString& onglet) const;  // texte du rapport de l'onglet
  QComboBox* varianteActive() const { return m_variante; }
  QSpinBox* cartes() const { return m_cartes; }

 private:
  QWidget* onglet(const QString& id, const QString& titre, const std::function<QString()>& html, const std::vector<std::pair<QString, std::function<void()>>>& boutons = {});
  AtelierElectronique& m_atelier;
  QTabWidget* m_onglets;
  QComboBox* m_variante = nullptr;
  QSpinBox* m_cartes = nullptr;
  std::map<QString, QTextBrowser*> m_textes;
  std::map<QString, std::function<QString()>> m_rapports;
  std::map<QString, int> m_indices;
  QString m_comparaison;  // dernier rapport de comparaison
};
