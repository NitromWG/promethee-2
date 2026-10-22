// Prométhée : lecture d'un schéma QElectroTech (.qet) relié au projet : titre, folios, appareils (repère,
// désignation), nombre de conducteurs. Licence GPL-3.0-only.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct InfosQet {
  bool lu = false;
  QString titre, erreur;
  QStringList folios;
  struct Appareil { QString repere, designation, folio; };
  QList<Appareil> appareils;
  int conducteurs = 0;
};

InfosQet lireQet(const QString& chemin);

#include <QWidget>

class QLabel;
class QTableWidget;

// Page d'un schéma QElectroTech relié : résumé, appareils, ouverture dans QElectroTech.
class PageSchema : public QWidget {
  Q_OBJECT
public:
  explicit PageSchema(QWidget* parent = nullptr);
  void montrer(const QString& chemin);

private:
  QString m_chemin;
  QLabel* m_titre;
  QTableWidget* m_table;
};
