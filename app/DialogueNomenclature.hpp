// Prométhée : nomenclature d'un assemblage (M10) — pièces seules, premier niveau ou à niveaux, colonnes au choix,
// repères stables, propriétés personnalisées, masses unitaires et totales ; exports CSV et XLSX. Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QDialog>

#include "promethee/nomenclature_meca.hpp"

class Document;
class QComboBox;
class QLabel;
class QToolButton;
class QTreeWidget;

class DialogueNomenclature : public QDialog {
  Q_OBJECT
public:
  DialogueNomenclature(Document* doc, int assemblage, std::function<prom::MassePiece(int)> masse, QWidget* parent = nullptr);
  void setVue(prom::VueNomenclature vue);
  void setColonnes(const std::vector<std::string>& colonnes);
  const prom::TableauNomenclature& tableau() const { return m_tableau; }
  // CSV ou XLSX selon le suffixe ; faux (et message rendu) si l'écriture échoue
  bool exporter(const QString& chemin, QString* erreur = nullptr);

private:
  void actualiser();
  void majMenuColonnes();
  Document* m_doc;
  int m_assemblage;
  std::function<prom::MassePiece(int)> m_masse;
  prom::VueNomenclature m_vue = prom::VueNomenclature::Niveaux;
  std::vector<std::string> m_colonnes;
  prom::TableauNomenclature m_tableau;
  QComboBox* m_choixVue = nullptr;
  QToolButton* m_boutonColonnes = nullptr;
  QTreeWidget* m_arbre = nullptr;
  QLabel* m_total = nullptr;
};
