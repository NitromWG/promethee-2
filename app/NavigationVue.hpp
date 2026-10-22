// Prométhée : navigation commune aux canevas du schéma et du circuit (lot L1, tâche 1.19, ERG-19, ERG-20), tirée
// de leurs deux wheelEvent. La molette zoome d'un facteur réglable par cran, dans le sens voulu, autour du curseur
// ou du centre de la vue ; le pavé tactile, reconnu à son type d'appareil (QInputDevice::DeviceType::TouchPad),
// sinon à un défilement en pixels, zoome ou fait défiler à deux doigts selon le réglage, pincer zoomant toujours.
// Les boutons du milieu et de droite déplacent la vue ; pendant un glisser, la vue défile quand le pointeur est dans
// une bande de 24 px au bord. Menu à marques : le bouton des gestes (droit avec Ctrl, latéral ou milieu), tenu
// immobile 260 ms, montre le menu radial à huit directions de l'esquisseur, qui sert d'aide ; un tracé rapide,
// reconnu à sa direction dès 26 px, lance la commande sans lui. Le bouton droit tenu immobile le montre aussi ;
// relâché dans la zone morte, c'est le clic droit court. Les réglages sont ceux de la page « Souris et pavé
// tactile » de la fenêtre des réglages (FenetreReglages).
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QKeyCombination>
#include <QObject>
#include <QPointF>
#include <QString>

class QFont;
class QMouseEvent;
class QNativeGestureEvent;
class QPainter;
class QTimer;
class QWheelEvent;
class QWidget;

// Menu radial à huit directions (esquisseur, menu à marques des vues) : anneau sombre, secteur visé surligné, icône
// et libellé de chaque direction, de l'est dans le sens trigonométrique
struct EntreeRadiale {
  QString icone, libelle;
};
void dessinerMenuRadial(QPainter& g, QPointF centre, int surligne, const std::array<EntreeRadiale, 8>& entrees, const QFont& police);

struct ReglagesNavigation {
  enum class DeuxDoigts : std::uint8_t { Zoomer, Defiler };
  enum class BoutonGestes : std::uint8_t { DroitAvecCtrl, Lateral, Milieu };
  double zoomCran = 1.25;  // facteur d'un cran de molette, de 1,1 à 2 (comme + et −)
  bool moletteInverse = false;
  // Maj + molette fait défiler verticalement, Ctrl + molette horizontalement ; croisé : l'inverse (SAI-25)
  bool defilementCroise = false;
  bool zoomSurCurseur = true;  // sinon autour du centre de la vue
  bool defilementBord = true;  // la vue défile pendant un glisser, le pointeur au bord
  DeuxDoigts deuxDoigts = DeuxDoigts::Zoomer;
  BoutonGestes gestes = BoutonGestes::DroitAvecCtrl;
  friend bool operator==(const ReglagesNavigation&, const ReglagesNavigation&) = default;
};

// Les réglages en cours, lus et écrits dans les réglages de l'application ; les vues suivent leurs changements
class Navigation : public QObject {
  Q_OBJECT
 public:
  static Navigation& instance();
  const ReglagesNavigation& reglages() const { return m_r; }
  void setReglages(const ReglagesNavigation& r);

 signals:
  void change();

 private:
  Navigation();
  ReglagesNavigation m_r;
};

class NavigationVue : public QObject {
  Q_OBJECT
 public:
  static constexpr int BORD = 24;          // bande du défilement au bord, en pixels
  static constexpr int ATTENTE_MENU = 260;  // bouton tenu immobile avant que le menu radial paraisse, en ms
  static constexpr int TRAIT = 26;         // longueur d'un tracé rapide, et rayon de la zone morte, en pixels
  // Un secteur du menu à marques, dans l'ordre des directions : est, nord-est, nord, nord-ouest, ouest, sud-ouest,
  // sud, sud-est
  struct Secteur {
    std::string action;
    QString libelle;
    QString icone;
  };
  using Secteurs = std::array<Secteur, 8>;
  // Les secteurs d'origine (ERG-20) : gauche Annuler, droite Rétablir, haut Zoom sur la sélection, bas Tout voir, en
  // diagonale Tourner, Miroir, Supprimer, Dupliquer ; « miroir » : l'action du miroir dans cette vue
  static Secteurs secteursOrigine(const std::string& miroir);
  static int secteurVers(QPointF delta);  // direction d'un déplacement : 0 à 7, -1 dans la zone morte

  NavigationVue(QWidget* vue, Secteurs secteurs, std::function<void(const std::string&)> faire, std::function<void(QPointF)> decaler);

  enum class Fin { Rien, Pris, ClicDroitCourt };
  bool appui(QMouseEvent* e);  // vrai : la navigation prend l'appui (déplacer la vue, geste)
  bool deplacer(QMouseEvent* e);
  Fin relacher(QMouseEvent* e);
  // Molette et pavé tactile ; « origine » et « echelle » : la vue, l'échelle bornée par min et max
  void molette(QWheelEvent* e, QPointF& origine, double& echelle, double min, double max) const;
  bool pincer(QNativeGestureEvent* e, QPointF& origine, double& echelle, double min, double max) const;
  // Pendant un glisser du bouton gauche : la vue défile tant que le pointeur reste au bord
  void glisserGauche(QPointF souris, bool glisse);
  bool panoramique() const { return m_mode == Mode::Deplacer && m_bouge; }
  bool menuVisible() const { return m_menu; }
  bool annonceVisible() const { return !m_annonce.isEmpty(); }
  int secteurSurligne() const { return m_secteur; }
  // Le menu radial, s'il est montré, et l'annonce près du curseur
  void dessiner(QPainter& g, QPointF souris) const;
  // Retour du clavier (ERG-03) : une annonce près du curseur (bascule faite au clavier, suite en attente)
  void annoncer(const QString& texte, int duree = 1000);
  QString annonce() const { return m_annonce; }
  // Suite de deux touches : la première attend la seconde 1,5 s, ses suites possibles annoncées
  void attendreSuite(QKeyCombination premiere, const QString& suites);
  std::optional<QKeyCombination> prefixe() const { return m_prefixe; }
  void finirSuite();
  // « P… V : Poser une via » : les suites qu'une première touche ouvre
  static QString texteSuites(QKeyCombination premiere, const std::vector<const struct ActionAtelier*>& suites);
  const Secteurs& secteurs() const { return m_secteurs; }

 private:
  enum class Mode { Aucun, Deplacer, Gestes };
  bool boutonGestes(const QMouseEvent* e) const;
  void lancer(int secteur);
  QWidget* m_vue;
  Secteurs m_secteurs;
  std::function<void(const std::string&)> m_faire;
  std::function<void(QPointF)> m_decaler;
  QTimer* m_attente;
  QTimer* m_defilement;
  QTimer* m_finAnnonce;
  QString m_annonce;
  std::optional<QKeyCombination> m_prefixe;
  Mode m_mode = Mode::Aucun;
  Qt::MouseButton m_bouton = Qt::NoButton;
  QPointF m_depart, m_dernier, m_souris;
  bool m_bouge = false, m_menu = false, m_trace = false;
  int m_secteur = -1;
};
