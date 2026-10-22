// Prométhée : canevas des folios électriques (A16). La classe de domaine de l'atelier électrique : elle reprend le
// socle du schéma (navigation, grille, sélection, outils, saisie sur place) et n'en remplace que les points
// d'extension, pour suivre les usages de l'électricien : folio à colonnes et lignes repérées, cartouche pleine
// largeur, repères « -KM1 », renvois croisés entre bobines et contacts, numéros de fils, bornes « -X1:3 », folios
// dessinés d'office (borniers, nomenclature, grafcet), commandes de vue dans le coin.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "VueSchema.hpp"

class VueSchemaElectrique : public VueSchema {
  Q_OBJECT
 public:
  explicit VueSchemaElectrique(Document* doc, QWidget* parent = nullptr);
  Domaine domaine() const override { return Domaine::Electrique; }
  void setRenvois(const prom::elec::Renvois* r) {
    m_renvois = r;
    invaliderCache();
  }

 protected:
  const prom::ecad::ConceptionElec* conception() const override;
  prom::ecad::ConceptionElec* cible(prom::Projet& p) const override;
  void modifier(const std::function<void(prom::ecad::Feuille&, prom::ecad::ConceptionElec&)>& f, const QString& fusion = {}, const QString& nom = {}) override;
  void commencerGeste() override;
  void dessinerFond(class QPainter& g, const prom::ecad::Feuille& f, const prom::ecad::ConceptionElec& c) override;
  void dessinerApresFils(class QPainter& g, const prom::ecad::Feuille& f) override;
  void dessinerApresSymboles(class QPainter& g, const prom::ecad::Feuille& f) override;
  void textesSymbole(const prom::ecad::Feuille& f, const prom::ecad::SymbolePlace& s, const prom::ecad::DefSymbole& d, const prom::ecad::ComposantElec* x, QString& ref,
                     QString& val) const override;
  QString repereFantome(const prom::ecad::DefSymbole& d) const override;
  bool texteAncreAGauche() const override { return true; }
  bool recadrerSurDessin() const override { return false; }
  int margeBas() const override;
  QString aideOutil(Outil o) const override;
  bool avecBandeau() const override { return false; }
  bool poseUnCoup() const override { return false; }
  std::set<std::string> collerDans(const prom::ecad::PressePapiersSchema& pp, prom::ecad::Pt ou) override;
  void armerAlimentation() override { setOutil(Outil::EtiquetteGlobale); }
  void resizeEvent(QResizeEvent* e) override;

 private:
  const prom::elec::ProjetElectrique* projetElectrique() const;
  void dessinerCadreElectrique(class QPainter& g, const prom::ecad::Feuille& f, const prom::elec::ProjetElectrique& pe);
  void dessinerRenvois(class QPainter& g, const prom::ecad::Feuille& f, const prom::elec::ProjetElectrique& pe);
  void dessinerNumerosFils(class QPainter& g, const prom::ecad::Feuille& f);
  // Folios dessinés d'office (borniers, nomenclature) : rapports recalculés seulement quand le projet change
  void dessinerFolioDOffice(class QPainter& g, const prom::ecad::Feuille& f, const prom::elec::ProjetElectrique& pe, const QRectF& dessin);
  const prom::elec::Renvois* m_renvois = nullptr;
  class QWidget* m_commandesVue = nullptr;  // zoom à la souris pour qui ne connaît pas la molette
  std::uint64_t m_generationRapports = ~std::uint64_t{0};
  int m_projetRapports = -1;
  std::vector<prom::elec::LigneBorne> m_cacheBorniers;
  std::vector<prom::elec::LigneNomenclatureElec> m_cacheNomenclature;
};
