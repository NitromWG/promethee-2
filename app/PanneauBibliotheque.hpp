// Prométhée : panneau de bibliothèque de l'atelier électronique (lot L1, tâche 1.10, BIB-04, B5, S3). Ancré à gauche
// du canevas, repliable, sa largeur et son état gardés d'une session à l'autre ; distinct de l'inspecteur, qui reste
// seul à droite : choisir un article ne touche jamais au panneau de droite, et un clic sur la feuille ne referme
// plus la bibliothèque. Il ne prend pas le focus à l'ouverture, sauf par A (« Choisir un composant »), qui met le
// curseur dans la recherche. Pendant une pose, l'encart « À poser » en tête montre l'article armé et rend la main
// d'un clic ; la bibliothèque reste ouverte pour poser autre chose (S3). Un clic arme la pose, Entrée aussi (dans la
// liste, ou dans la recherche pour le premier article trouvé) ; un article se glisse sur la feuille (GlisserArticle,
// 2.2) ; favoris, récents et vignettes : fenêtre de choix et moteur de recherche (lot L4).
// Posé dans un panneau ancrable (1.21) ; colonnes de champs choisies au menu de l'en-tête, tri par catégorie, par
// pertinence ou par ordre alphabétique, aperçu du symbole, de l'empreinte, de la 3D et de la photo (ApercuArticle),
// gardés d'une session à l'autre (BIB-04).
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QWidget>

#include "promethee/circuit.hpp"
#include "promethee/schema.hpp"

class ApercuArticle;
class AtelierElectronique;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class PanneauBibliotheque : public QWidget {
  Q_OBJECT
 public:
  explicit PanneauBibliotheque(AtelierElectronique& a, QWidget* parent = nullptr);
  // Relit les articles : bibliothèque intégrée, personnelle, symboles propres au projet ; la recherche et l'article
  // choisi restent
  void refaire();
  // A : le curseur dans la recherche, le texte choisi pour être remplacé
  void chercher();
  // L'outil du schéma a changé : l'encart « À poser » le suit
  void majAPoser();
  QTreeWidget* arbre() const { return m_arbre; }
  QLineEdit* recherche() const { return m_recherche; }
  std::string articleChoisi() const;
  ApercuArticle* apercu() const { return m_apercu; }
  // Tri : par catégorie (arbre), par pertinence pour la recherche, par ordre alphabétique (listes)
  enum class Tri : std::uint8_t { Categorie, Pertinence, Alphabetique };
  void setTri(Tri t);
  Tri tri() const { return m_triChoisi; }
  // Colonnes de champs après le nom, choisies au menu de l'en-tête : « description », « categorie », « empreinte »,
  // « broches », « fabricant », « mpn », « prefixe »
  static QStringList colonnesPossibles();
  static QString titreColonne(const QString& id);
  void setColonnes(const QStringList& ids);
  QStringList colonnes() const { return m_colonnes; }
  // Pertinence d'un article pour une recherche, accents et casse ignorés : 0 s'il ne répond pas à tous les mots ;
  // le nom exact d'abord, puis le début du nom, le nom, l'identifiant, le fabricant, les mots-clés, la description
  static int pertinence(const prom::ecad::DefSymbole& d, const QString& recherche);

 signals:
  void replier();

 private:
  const prom::ecad::DefSymbole* trouver(const std::string& id) const;
  std::optional<prom::ecad::DefEmpreinte> empreinteDe(const prom::ecad::DefSymbole& d) const;
  void filtrer(const QString& texte);
  void majEnTete();
  void menuColonnes(const QPoint& p);
  void montrerApercu(QTreeWidgetItem* it);
  void placer(QTreeWidgetItem* it, bool serie);
  AtelierElectronique& m_atelier;
  std::vector<prom::ecad::DefSymbole> m_perso;  // personnels et propres au projet
  QLineEdit* m_recherche;
  QTreeWidget* m_arbre;
  QComboBox* m_tri;
  Tri m_triChoisi = Tri::Categorie;
  QStringList m_colonnes;
  ApercuArticle* m_apercu;
  class GlisserArticle* m_glisser = nullptr;  // glisser-déposer des articles (2.2)
  QLabel* m_info;
  QWidget* m_aPoser;
  QLabel *m_aPoserApercu, *m_aPoserTexte, *m_alerte;
};
