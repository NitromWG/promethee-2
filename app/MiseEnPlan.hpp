// Prométhée : fenêtre de mise en plan d'une pièce (aperçu de la feuille, réglages, exports PDF, SVG, DXF).
// Licence GPL-3.0-only.
#pragma once

#include <QDialog>

#include <TopoDS_Shape.hxx>

#include "promethee/dessin.hpp"
#include "promethee/piece.hpp"

class MiseEnPlan : public QDialog {
  Q_OBJECT
public:
  // Cartouche (ISO 7200) : titre, numéro de pièce (référence), matière et masse (MP-06)
  MiseEnPlan(const TopoDS_Shape& forme, const prom::PlanPiece& reglages, const std::string& titre, const std::string& materiau, QWidget* parent = nullptr,
             const std::string& reference = {}, const std::string& masse = {});
  const prom::PlanPiece& reglages() const { return m_reglages; }
  // Mise en plan nouvelle : elle n'existe qu'une fois gardée ; « Abandonner » ne laisse rien dans la pièce (AP-30)
  void modeCreation();
  static bool exporterPdf(const prom::Feuille& f, const QString& chemin);
  // Plan d'assemblage : nomenclature au-dessus du cartouche et bulles de repère dans la vue isométrique
  void setAssemblage(std::vector<prom::OptionsMiseEnPlan::LigneNomenclature> nomenclature, std::vector<prom::OptionsMiseEnPlan::Bulle> bulles);
  const prom::Feuille& feuille() const { return m_feuille; }
  // Filetages de la pièce (taraudages, filetages cosmétiques), dessinés et indiqués selon l'ISO 6410
  void setFiletages(std::vector<prom::OptionsMiseEnPlan::FiletageDessin> filetages);
  // Propriétés personnalisées de la pièce : « Révision », « Désignation », « Projet », « Auteur » vont au cartouche
  void setProprietes(std::vector<std::pair<std::string, std::string>> proprietes);
  // Cotes des esquisses de la pièce (D3), montrées si « Cotes du modèle » est cochée ; orphelines signalées
  void setCotesModele(std::vector<prom::OptionsMiseEnPlan::CoteModele> cotes, std::vector<std::string> orphelines);

private:
  void recalculer();
  TopoDS_Shape m_forme;
  prom::PlanPiece m_reglages;
  std::string m_titre, m_materiau, m_reference, m_masse;
  prom::Feuille m_feuille;
  std::vector<prom::OptionsMiseEnPlan::LigneNomenclature> m_nomenclature;
  std::vector<prom::OptionsMiseEnPlan::Bulle> m_bulles;
  std::vector<prom::OptionsMiseEnPlan::FiletageDessin> m_filetages;
  std::vector<std::pair<std::string, std::string>> m_proprietes;
  std::vector<prom::OptionsMiseEnPlan::CoteModele> m_cotesModele;
  std::vector<std::string> m_cotesOrphelines;
  class QLabel* m_alertes = nullptr;
  class ApercuFeuille* m_apercu = nullptr;
  class QPushButton* m_fermer = nullptr;
};
