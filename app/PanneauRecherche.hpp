// Prométhée : panneau Chercher de l'atelier électronique (lot L2, tâches 2.12 et 2.27 ; SAI-17, SAI-40, STR-14).
// Ctrl+F y met le curseur. Il cherche dans toutes les feuilles de la conception ouverte : repères, valeurs, champs
// (fabricant, référence fabricant, fournisseur), étiquettes, noms d'alimentation, textes et noms de réseaux, avec
// casse, mot entier, jokers * et ?, ou expression régulière. La liste se refait à chaque frappe ; Entrée, ▲ et ▼
// mènent d'un résultat à l'autre, en boucle ; un clic mène à l'objet sur sa feuille, choisi et centré (STR-14), un
// réseau y est surligné. « Remplacer tout » change la seule partie trouvée, repères compris, en un pas ; un repère
// qui existe déjà est passé et nommé. Depuis 2.27 : un index commun au schéma et au circuit (zones, vias, pistes et
// textes de la carte compris), refait quand la conception change (Document::conceptionChangee) et seulement filtré
// pendant la frappe ; recherche approchée (distance d'édition bornée) ; genres d'objets et portée choisis ; tous
// les résultats choisis ensemble, pour l'inspecteur.
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <string>
#include <vector>

#include <QRegularExpression>
#include <QWidget>

#include "promethee/electronique.hpp"

class AtelierElectronique;
class QCheckBox;
class QLabel;
class QLineEdit;
class QTreeWidget;

class PanneauRecherche : public QWidget {
  Q_OBJECT
 public:
  enum class Genre { Repere, Valeur, Champ, Etiquette, Alimentation, Texte, Reseau, Zone, Vias, Pistes, TexteCarte };
  static constexpr int GENRES = 11;
  struct Trouve {
    Genre genre = Genre::Repere;
    int feuille = -1;    // −1 : le circuit
    std::string objet;   // UUID du composant (repère, valeur, champ), de l'objet de la feuille, ou nom du réseau
    std::string texte;   // le texte où la recherche a trouvé
    std::string champ;   // champ du composant (« fabricant »…), vide sinon
    prom::ecad::Pt position;
    std::vector<std::string> ids{};  // les objets du circuit qu'un résultat groupe (vias, pistes d'un réseau)
  };
  enum class Portee { Tout, Schema, Circuit, Feuille };
  void setPortee(Portee p);
  void setGenreCherche(Genre g, bool oui);
  void setApproche(bool oui);
  // Tous les résultats choisis ensemble (symboles, étiquettes, textes, zones, vias…) : l'inspecteur les montre
  int choisirTout();
  static int distanceEdition(const QString& a, const QString& b, int borne);  // au-delà de « borne » : borne + 1
  explicit PanneauRecherche(AtelierElectronique& a, QWidget* parent = nullptr);
  void focaliser();                    // Ctrl+F : le curseur dans le champ, son texte choisi
  void chercher(const QString& texte); // remplit le champ et cherche
  void majResultats();                 // la conception a changé
  bool aller(int i);                   // le i-ème résultat : sa feuille, l'objet choisi et centré
  bool suivant(int sens);              // ▼ (+1) et ▲ (−1), en boucle
  // Remplace la partie trouvée par « par » dans chaque résultat, en un pas ; rend le nombre d'objets changés
  int remplacerTout(const QString& par);
  const std::vector<Trouve>& resultats() const { return m_resultats; }
  QLineEdit* champ() const { return m_champ; }
  QTreeWidget* liste() const { return m_liste; }

 private:
  QRegularExpression motif() const;  // d'après le texte et les options ; invalide si le texte est vide ou faux
  void indexer();                     // l'index, refait quand la conception a changé
  void filtrer();                     // les résultats, tirés de l'index à chaque frappe
  AtelierElectronique& m_atelier;
  std::vector<Trouve> m_index;
  bool m_indexPerime = true;
  class QComboBox* m_portee = nullptr;
  class QToolButton* m_genres = nullptr;
  QCheckBox* m_approche = nullptr;
  std::array<bool, GENRES> m_genresCherches{};
  QLineEdit* m_champ;
  QLineEdit* m_remplacer;
  QCheckBox *m_casse, *m_motEntier, *m_jokers, *m_regex;
  QLabel* m_resume;
  QTreeWidget* m_liste;
  std::vector<Trouve> m_resultats;
  int m_courant = -1;
};
