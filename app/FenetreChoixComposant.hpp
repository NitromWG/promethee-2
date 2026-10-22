// Prométhée : fenêtre « Choisir un composant » (lot L4, tâches 4.4, 4.5, 4.15, 4.21, 4.22 ; BIB-05 à BIB-07,
// BIB-20, BIB-27 à BIB-29, ERG-06). Non modale : la feuille reste utilisable pendant qu'elle est ouverte. La
// recherche commune (IndexArticles) trouve les articles réels de la bibliothèque intégrée, de la bibliothèque
// personnelle et du projet ; « 10k 0603 » donne la résistance voulue, composée à la volée au besoin. Cases « avec
// empreinte », « avec modèle 3D », « avec modèle de simulation », « avec fiche », « avec référence » ; « Chercher des
// semblables » ; composant générique décrit par ses exigences. Une archive de SnapEDA, d'Ultra Librarian ou de
// SamacSys déposée sur la fenêtre devient un seul article contrôlé. L'onglet des portails et des distributeurs reste
// grisé, avec la raison, tant que l'accès en ligne n'est pas activé : Prométhée n'ouvre alors aucune connexion.
// Licence GPL-3.0-only.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QWidget>

#include "promethee/schema.hpp"

namespace prom::ecad {
class IndexArticles;
}
class AtelierElectronique;
class ApercuArticle;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;

class FenetreChoixComposant : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreChoixComposant(AtelierElectronique& a, QWidget* parent = nullptr);
  ~FenetreChoixComposant() override;
  // Relit les sources (intégrée, personnelle, projet) et refait la recherche en cours
  void refaire();
  void chercher(const QString& requete);
  // Articles de la liste, dans l'ordre affiché
  const std::vector<prom::ecad::Article>& resultats() const { return m_resultats; }
  QTableWidget* table() const { return m_table; }
  QLineEdit* recherche() const { return m_recherche; }
  QCheckBox* filtre(int i) const { return m_filtres[static_cast<std::size_t>(i)]; }  // 0 empreinte, 1 3D, 2 simulation, 3 fiche, 4 référence
  QTabWidget* onglets() const { return m_onglets; }
  QWidget* portails() const { return m_portails; }
  QLabel* raisonPortails() const { return m_raisonPortails; }
  QPushButton* boutonPoser() const { return m_poser; }
  // Arme la pose de l'article de la ligne (le premier sans ligne choisie) ; faux sans article
  bool poser(int ligne = -1);
  // « Chercher des semblables » à l'article de la ligne choisie
  void semblables();
  // Composant générique provisoire décrit par le texte de la recherche (« LDO 3,3 V 500 mA »), armé pour la pose
  bool poserGenerique();
  // Archive d'un portail : un seul article rangé dans la bibliothèque personnelle ; rend son identifiant, vide si refusée
  std::string importerArchive(const QString& chemin);
  std::string articleChoisi() const;

 protected:
  void dragEnterEvent(class QDragEnterEvent* e) override;
  void dropEvent(class QDropEvent* e) override;

 private:
  void remplir(const std::vector<prom::ecad::Article>& articles, const std::vector<bool>& composes);
  void montrerApercu();
  AtelierElectronique& m_atelier;
  QLineEdit* m_recherche;
  std::vector<QCheckBox*> m_filtres;
  QTableWidget* m_table;
  ApercuArticle* m_apercu;
  QLabel* m_details;
  QTabWidget* m_onglets;
  QWidget* m_portails;
  QLabel* m_raisonPortails;
  QPushButton *m_poser, *m_semblables, *m_generique;
  QLabel* m_bilan;
  std::vector<prom::ecad::Article> m_resultats;
  std::unique_ptr<prom::ecad::IndexArticles> m_index;
};
