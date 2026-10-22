// Prométhée : panneau latéral (propriétés, vérifications, nomenclature). Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <vector>

#include <QTabWidget>
#include <QTimer>

#include "Document.hpp"

class QDoubleSpinBox;
class QFormLayout;
class QListWidget;
class QPushButton;
class QScrollArea;
class QTableWidget;
class QVBoxLayout;

class Panneau : public QTabWidget {
  Q_OBJECT
public:
  explicit Panneau(Document* doc, QWidget* parent = nullptr);
  QSize sizeHint() const override { return {360, 600}; }

private:
  void construireProprietes();
  void synchroniser();
  void majVerifications();
  void majNomenclature();
  QFormLayout* groupe(QVBoxLayout* colonne, const QString& titre);
  QDoubleSpinBox* nombre(QFormLayout* f, const QString& libelle, double min, double max, std::function<double()> lire,
                         std::function<void(prom::Projet&, double)> ecrire);
  void texte(QFormLayout* f, const QString& libelle, std::function<std::string()> lire, std::function<void(prom::Projet&, const std::string&)> ecrire);
  void lecture(QVBoxLayout* colonne, std::function<QString()> lire);

  Document* m_doc;
  QScrollArea* m_zone = nullptr;
  QListWidget* m_verifs = nullptr;
  QPushButton* m_corriger = nullptr;
  QTableWidget* m_nomen = nullptr;
  std::vector<std::function<void()>> m_synchros;
  Cible m_cibleConstruite{Cible::Aucune, "?"};
  QTimer m_minuteurListes;
  std::string m_sigVerifs, m_sigNomen;
  bool m_carteLibre = false;
};
