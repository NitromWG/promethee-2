// Prométhée : rapport de vérification ouvert dans Prométhée (lot L5, tâche 5.29 ; DRC-18 à DRC-20). Le rapport HTML
// autonome du noyau, groupé par règle, exclusions à part ; chaque ligne mène à son objet d'un clic. Il s'enregistre
// en HTML ou en texte.
// Licence GPL-3.0-only.
#pragma once

#include <vector>

#include <QWidget>

#include "promethee/verification.hpp"

class AtelierElectronique;
class QTextBrowser;

class FenetreRapport : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreRapport(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  QTextBrowser* navigateur() const { return m_texte; }
  const std::vector<prom::ecad::Violation>& erc() const { return m_erc; }
  const prom::ecad::BilanVerification& drc() const { return m_drc; }
  QString html() const { return m_html; }
  void suivre(const QUrl& lien);  // « promethee:violation/<n> » : l'objet montré

 private:
  AtelierElectronique& m_atelier;
  QTextBrowser* m_texte;
  std::vector<prom::ecad::Violation> m_erc;
  prom::ecad::BilanVerification m_drc;
  QString m_html;
};
