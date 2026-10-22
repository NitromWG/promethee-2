// Prométhée : registre des actions de l'atelier électronique (lot L1, tâche 1.8, ERG-03, ERG-17). Les touches ne
// parlent plus aux outils : une frappe désigne une action du registre par son identifiant stable (« circuit.via »),
// que l'outil actif puis la vue exécutent. Chaque action accepte deux raccourcis ; F1 en montre la liste pour
// l'atelier et l'outil en cours. Un canevas ne garde que les touches d'une action qu'il peut faire maintenant : les
// touches de fonction et les combinaisons avec Alt remontent toujours à l'atelier et à la fenêtre (S22).
// L'éditeur de raccourcis (1.20) et l'essai « la souris suffit » (1.16) lisent ce même registre : chaque bouton du
// ruban et des bandeaux, chaque ligne des menus qui fait une action en porte l'identifiant (marquerAction), et une
// action sans bouton ni ligne déclare le geste qui la fait à la souris (ERG-17).
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <QKeySequence>
#include <QString>
#include <QStringList>

class QKeyEvent;
class QObject;

// Où l'action sert : partout (fenêtre), dans l'atelier hors des canevas (F1, F8), dans les deux canevas, au schéma
// seulement, au circuit seulement
enum class PorteeAction : std::uint8_t { Partout, Atelier, Canevas, Schema, Circuit };

struct ActionAtelier {
  std::string identifiant;  // stable : « circuit.via »
  QString libelle;          // « Via ; pendant le routage, via et changement de couche »
  PorteeAction portee = PorteeAction::Canevas;
  std::array<QKeySequence, 2> raccourcis;
  std::array<QKeySequence, 2> raccourcisOrigine{};  // ceux du registre, avant l'utilisateur (2.32)
  // Prise par l'atelier quand le focus est ailleurs qu'un champ de saisie (Échap, Suppr, R : S21)
  bool horsCanevas = false;
  // Touche de transition (F au schéma, qui ne recadre plus) : hors de la liste F1
  bool cachee = false;
  // Geste qui fait l'action à la souris quand aucun bouton ni aucune ligne de menu ne la porte : « glisser la
  // sélection », « molette », « clic droit sans glisser » (ERG-17) ; vide, un bouton ou une ligne la porte
  QString souris{};
  // Présentation au ruban et dans les menus (1.20) : nom court, icône, bulle ; vide, le libellé sert de nom
  QString nom{};
  QString icone{};
  QString bulle{};
  // Touche qui ouvre un menu de ces actions au curseur (ERG-03), plutôt que d'agir
  std::vector<std::string> menu{};
  // Avec le réglage « Premier appui : agir au curseur », la touche fait l'action là où est le curseur au lieu
  // d'armer l'outil (jonction, non-connexion, via)
  bool auCurseur = false;
  // Bascule (coude, mode) : faite au clavier, son nouvel état s'annonce près du curseur
  bool bascule = false;
  // Contrôle du ruban fabriqué par l'atelier (liste des couches, grille et unité) plutôt qu'un bouton
  bool controle = false;
  QString nomAffiche() const { return nom.isEmpty() ? libelle : nom; }
};

// Premier appui d'une touche d'outil (ERG-03) : armer l'outil, ou agir tout de suite au curseur
enum class PremierAppui : std::uint8_t { ArmerOutil, AgirAuCurseur };
PremierAppui premierAppui();
void setPremierAppui(PremierAppui p);

// Bouton, action de menu ou liste qui fait une action du registre : il en porte l'identifiant, que l'essai « la
// souris suffit » cherche dans l'interface réellement construite (ERG-17). Un porteur peut en faire plusieurs (la
// liste des couches fait « couche du dessus » et « couche du dessous »).
void marquerAction(QObject* porteur, std::string_view identifiant);
QStringList actionsMarquees(const QObject* porteur);

class RegistreActions {
 public:
  // Le registre des canevas électroniques (schéma et circuit, atelier électrique compris, qui partage le schéma)
  static RegistreActions& atelierElectronique();

  const std::vector<ActionAtelier>& actions() const { return m_actions; }
  const ActionAtelier* action(std::string_view identifiant) const;
  // Actions d'un canevas (portée Canevas et celle de la vue) que désigne cette frappe, dans l'ordre du registre
  std::vector<const ActionAtelier*> pour(PorteeAction vue, const QKeyEvent& e) const;
  // Suites de deux touches (ERG-03) : celles d'une vue qui commencent par cette frappe, et celles que complète la
  // seconde après la première
  std::vector<const ActionAtelier*> suites(PorteeAction vue, QKeyCombination premiere) const;
  std::vector<const ActionAtelier*> pourSuite(PorteeAction vue, QKeyCombination premiere, QKeyCombination seconde) const;
  // Action ajoutée par un script ou une extension (13.3) : faux si l'identifiant existe déjà
  bool ajouterAction(ActionAtelier a);
  // Remplace les deux raccourcis d'une action (éditeur de raccourcis, 1.20) ; faux si l'identifiant est inconnu
  // Ils sont gardés dans les réglages (« raccourcis/<identifiant> »), donc exportés, relus et remis à zéro avec eux
  bool setRaccourcis(std::string_view identifiant, const QKeySequence& premier, const QKeySequence& second = {});
  // Les raccourcis de l'utilisateur relus des réglages, sur ceux d'origine (démarrage, relecture, remise à zéro : 2.32)
  void relireRaccourcis();
  // Deux actions d'une même vue sur une même touche, hors des paires prévues où l'outil départage (Retour arrière :
  // supprimer hors tracé, retirer un coude pendant un tracé) : « Ctrl+D : edition.dupliquer et … »
  QStringList conflits() const;
  // Touches d'une action, écrites comme sur le clavier français : « Suppr, Retour arrière »
  QString touches(std::string_view identifiant) const;
  static QString nomTouche(const QKeySequence& k);
  // Frappe ramenée à sa forme de registre : Maj ne compte pas pour un symbole ou un chiffre (« / » et « + » se
  // tapent avec Maj en AZERTY), le pavé numérique ne compte pas, Entrée du pavé vaut Entrée
  static QKeyCombination normaliser(QKeyCombination k);

 private:
  RegistreActions();
  std::vector<ActionAtelier> m_actions;
};
