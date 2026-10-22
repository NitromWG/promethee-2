// Prométhée : nomenclature commune aux métiers. Pièces mécaniques (matière, masse, quantité dans les
// assemblages), composants électroniques de la carte, appareils électriques des schémas QElectroTech ;
// export CSV. Licence GPL-3.0-only.
#pragma once

#include <QWidget>

class Document;
class QTableWidget;

class PageNomenclature : public QWidget {
  Q_OBJECT
public:
  explicit PageNomenclature(Document* doc, QWidget* parent = nullptr);
  void actualiser();

private:
  void exporterCsv();
  Document* m_doc;
  QTableWidget* m_table;
};

// Intégration mécatronique : la carte dans son boîtier. Encombrement, fixations, connecteurs de bord, vérifications.
class PageIntegration : public QWidget {
  Q_OBJECT
public:
  explicit PageIntegration(Document* doc, QWidget* parent = nullptr);
  void actualiser();

private:
  Document* m_doc;
  class QLabel* m_texte;
};
