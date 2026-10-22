// Prométhée : création d'un composant électronique par l'utilisateur. Identité (nom, préfixe, fabricant,
// référence, fiche technique), empreinte du catalogue paramétrique, liste des broches (numéro, nom, type, côté),
// remplie à la main, collée depuis une fiche technique ou tirée de l'empreinte ; le symbole est dessiné en direct.
// Licence GPL-3.0-only.
#pragma once

#include <string>

#include <QDialog>

#include "promethee/schema.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

class DialogueComposant : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueComposant(QWidget* parent = nullptr);
  void setSpec(const prom::ecad::SpecSymbole& s);  // modification, variante, essais
  prom::ecad::SpecSymbole spec() const;
  prom::ecad::DefSymbole symbole() const { return prom::ecad::genererSymbole(spec()); }
  bool valide(QString* raison = nullptr) const;
  // Pour les essais et le collage : ajoute les broches lues dans un texte (« 1 VCC alim »)
  void collerBroches(const QString& texte);
  void brochesDepuisEmpreinte();
  // Unités semblables (portes, amplis : 2.23, SYM-04) : une broche modifiée (nom, type, côté) l'est aussi à la même
  // place dans chacune des autres unités de même taille, son numéro gardé ; rend le nombre de lignes changées
  int propager(int ligne);
  void setPropagation(bool oui);
  // Libellé d'une unité dans les listes : « A · Ampli 1 » quand elle est nommée, sa lettre sinon (2.23)
  QString libelleUnite(int u) const;

 private:
  void majApercu();
  void ajouterLigne(const prom::ecad::SpecBroche& b);
  int ligneDe(const QWidget* w) const;
  void majLibellesUnites();
  QLineEdit *m_nom, *m_prefixe, *m_description, *m_categorie, *m_fabricant, *m_mpn, *m_fiche, *m_motsCles;
  QComboBox* m_empreinte;
  QLineEdit* m_nomsUnites = nullptr;  // « Ampli 1, Ampli 2 » : un nom par unité, dans l'ordre
  QTableWidget* m_broches;
  QLabel *m_apercu, *m_bilan;
  QPushButton* m_ok;
  std::string m_id;
  int m_version = 1;
  bool m_remplissage = false;
  bool m_propage = false;  // pendant une propagation : les lignes copiées ne propagent pas à leur tour
  class QCheckBox* m_propagation = nullptr;
};
