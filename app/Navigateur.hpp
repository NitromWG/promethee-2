// Prométhée : navigateur du projet. Une seule arborescence : le projet, puis un dossier par métier présent
// (électronique, mécanique, électrique), chacun avec ses parties (cartes, boîtier, pièces…), qui se déplient
// jusqu'à leur contenu (fonctions d'une pièce, composants d'une carte, plans). Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <set>
#include <string>

#include <QMenu>
#include <QTreeWidget>

#include "promethee/modele.hpp"

class Navigateur : public QTreeWidget {
  Q_OBJECT
public:
  explicit Navigateur(QWidget* parent = nullptr);
  // Reconstruit l'arbre ; l'élément de la pièce active est rendu pour que l'atelier y range ses fonctions.
  void maj(const prom::Projet& p, const QString& nomProjet, int pieceActive, bool atelierPieces, int conceptionElec = -1, int projetElectrique = -1);
  QTreeWidgetItem* itemPieceActive() const { return m_pieceActive; }
  // Ce que l'arbre montre d'une conception électronique (nom, révision, feuilles, composants, carte, publications) :
  // une modification qui le laisse inchangé ne reconstruit pas l'arbre (A12)
  static std::string signatureConception(const prom::ecad::ConceptionElec& c);
  // Menu contextuel de l'élément de clé « cle » (vide : le fond de l'arbre), propre à ce qu'il désigne (G2)
  void remplirMenu(QMenu& menu, const QString& cle);
  // Renommer une carte sur place, sans fenêtre (G7) : l'élément de la carte passe en édition
  bool editerNomConception(int conception);
  static constexpr int ROLE_CLE = Qt::UserRole + 1;      // clé stable de l'élément (repli conservé)
  static constexpr int ROLE_COULEUR = Qt::UserRole + 2;  // couleur du métier (liseré à gauche)
  static constexpr int ROLE_DETAIL = Qt::UserRole + 3;   // précision discrète, à droite si la place le permet
  static constexpr int ROLE_RENOMMAGE = Qt::UserRole + 4;  // élément mis en édition par « Renommer » (G7)

signals:
  void ouvrirCarte();
  void ouvrirPiece(int index);
  void choisirFonctionPiece(int index, int id);
  void nouvellePiece();
  void importerKicad();             // carte KiCad pour le boîtier (mécanique)
  void importerKicadElectronique();  // carte KiCad vers l'atelier électronique (G4)
  void ouvrirDansKicad();
  void supprimerPiece(int index);
  void ouvrirPlan(int piece, int plan);
  void nouveauPlan(int piece);
  void renommerPlan(int piece, int plan);
  void supprimerPlan(int piece, int plan);
  void ouvrirAssemblage(int assemblage, int composant);
  void nouvelAssemblage();
  void supprimerAssemblage(int assemblage);
  void ouvrirSchema(int k);
  void ajouterSchema();
  void retirerSchema(int k);
  void ouvrirSchemaExterne(int k);
  void supprimerContrainteAssemblage(int assemblage, int contrainte);
  void ouvrirContrainteAssemblage(int assemblage, int contrainte);
  void basculerContrainteAssemblage(int assemblage, int contrainte);
  void ouvrirNomenclature();
  void ouvrirIntegration();
  void menuDansPieceActive(const QPoint& p);   // clic droit sur une fonction de la pièce active
  // Électronique : mode 0 schéma, 1 circuit imprimé, 2 3D, 3 fabrication
  void ouvrirElectronique(int conception, int mode, int feuille);
  void ouvrirComposantElec(int conception, const QString& uuid);
  void nouvelleConceptionElec();
  // Renommage sur place dans l'arbre (G7) : le nouveau nom, déjà tapé
  void renommerConceptionElec(int conception, const QString& nom);
  void supprimerConceptionElec(int conception);
  void supprimerComposantElec(int conception, const QString& uuid);
  void supprimerFeuilleElec(int conception, int feuille);
  // Électrique : mode 0 schéma, 1 armoire, 2 faisceau, 3 rapports, 4 calculs, 5 logement
  void ouvrirElectrique(int projet, int mode, int folio);
  void nouveauProjetElectrique();
  void importerQetElectrique();
  void renommerProjetElectrique(int projet);
  void supprimerProjetElectrique(int projet);

public:
  // La vignette d'une feuille au survol (2.30, ERG-37) : un chemin d'image PNG, donné par l'hôte qui tient le projet
  void setVignette(std::function<QString(int conception, int feuille)> f) { m_vignette = std::move(f); }

protected:
  bool viewportEvent(QEvent* e) override;

private:
  std::function<QString(int, int)> m_vignette;
  QTreeWidgetItem* m_pieceActive = nullptr;
  std::set<QString> m_ouverts;
  bool m_premiere = true;
  void retenirOuverts(QTreeWidgetItem* it);
};
