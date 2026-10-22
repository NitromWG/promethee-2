// Prométhée : écran d'accueil. Une recherche (« Que veux-tu créer ? »), quatre métiers (mécanique, électronique,
// électrique, plusieurs) dont un seul montre ses créations à la fois — celui de la dernière fois —, sinon un aperçu
// « Pour commencer » avec les exemples. À gauche : nouveau projet vide, ouvrir, importer, projets récents.
// Licence GPL-3.0-only.
#pragma once

#include <map>

#include <QString>
#include <QStringList>
#include <QWidget>

class QGridLayout;
class QLabel;
class QScrollArea;
class QVBoxLayout;

class Accueil : public QWidget {
  Q_OBJECT
public:
  explicit Accueil(QWidget* parent = nullptr);
  void setRecents(const QStringList& chemins);   // « chemin|métiers » (métiers : meca, elec, electrique)
  void choisirMetier(const QString& metier);

protected:
  void resizeEvent(QResizeEvent* e) override;

signals:
  void choisi(const QString& code);              // « piece », « kicad-ouvrir », « mixte-boitier », « mixte-vide », « ouvrir »
  void ouvrirRecent(const QString& chemin);

private:
  void majChoix();
  QString m_metier;
  int m_colonnes = 4;
  QWidget* m_tuiles = nullptr;
  QScrollArea* m_defile = nullptr;
  std::map<QString, QWidget*> m_sections;  // métier → section de ses choix (toutes visibles d'emblée)
  QLabel* m_consigne = nullptr;
  QVBoxLayout* m_recents = nullptr;
  class QLineEdit* m_recherche = nullptr;     // « Que veux-tu créer ? » : tous métiers confondus
  QWidget* m_commencer = nullptr;             // aperçu quand aucun métier n'est choisi
  QWidget* m_resultats = nullptr;             // résultats de la recherche
  QLabel* m_titreResultats = nullptr;
  QGridLayout* m_grilleResultats = nullptr;
  QString m_premierResultat;                  // ouvert par Entrée dans la recherche
};
