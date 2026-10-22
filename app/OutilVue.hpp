// Prométhée : outils des canevas de l'atelier électronique (lot L1, tâche 1.6, A5). Chaque outil est une classe :
// il reçoit le survol, l'appui, le glisser, le relâcher, le double-clic et les touches, dessine son aperçu, donne
// le contenu du bandeau d'outil et son curseur, et dit s'il est à un coup (il rend la main à l'outil Sélection après
// son action) ou permanent. Le gestionnaire d'une vue tient l'outil actif, et une pile d'outils suspendus (1.17).
// Échap suit la charte : un geste en cours est annulé d'abord (l'outil garde Échap), sinon l'atelier revient à
// l'état neutre.
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include <QCursor>
#include <QPointF>
#include <QString>

#include "RegistreActions.hpp"

class QPainter;

// Ce qu'un outil reçoit d'un geste de souris
struct GesteVue {
  QPointF ecran;
  Qt::MouseButton bouton = Qt::NoButton;
  Qt::MouseButtons boutons;
  Qt::KeyboardModifiers mods;
};

// Une ligne de la liste d'un bouton du bandeau (paire de couches, largeur, prochaine via) : cochée quand elle est
// le choix en cours, grisée quand elle n'est pas encore possible, sa raison en bulle
struct ChoixBandeau {
  QString libelle;
  std::function<void()> faire;
  bool coche = false;
  bool actif = true;
  QString bulle{};
  std::string action{};  // action du registre que fait la ligne (« circuit.coucheDessous »), vide sinon
  bool section = false;  // titre d'un groupe de lignes, sans action
};

// Une action du bandeau : un bouton, avec sa touche écrite à côté (ERG-17 : la souris suffit)
struct ActionBandeau {
  QString id;       // identifiant stable (« tourner », « serie », « terminer »…), nom d'objet du bouton
  QString libelle;  // « Tourner »
  QString touche;   // « R », vide s'il n'y en a pas
  std::function<void()> faire;
  bool sortie = false;  // « Terminer » ou « Annuler » : toujours le dernier bouton à droite
  bool coche = false;   // bouton à bascule enfoncé (« Poser en série », mode courant)
  bool cochable = false;
  std::string action{};  // action du registre que fait le bouton (« objet.tourner »), vide s'il n'y en a pas
  // Liste ouverte par la flèche du bouton (le bouton lui-même fait « faire ») ; sans « faire », la liste s'ouvre
  // d'un clic sur le bouton entier
  std::vector<ChoixBandeau> choix{};
  // Bouton d'une action du registre : sa touche est celle du registre, l'essai « la souris suffit » l'y trouve
  static ActionBandeau du(std::string_view action, const QString& id, const QString& libelle, std::function<void()> faire) {
    ActionBandeau a;
    a.id = id;
    a.libelle = libelle;
    a.touche = RegistreActions::atelierElectronique().touches(action).section(QStringLiteral(", "), 0, 0);
    a.faire = std::move(faire);
    a.action = std::string(action);
    return a;
  }
};

// Contenu du bandeau : outil et objet, ce qu'il y a à savoir, puis les actions ; vide, il se masque
struct ContenuBandeau {
  QString titre;   // « Pose : Résistance »
  QString detail;  // « clic pour poser »
  std::vector<ActionBandeau> actions;
  bool vide() const { return titre.isEmpty(); }
};

class OutilVue {
 public:
  virtual ~OutilVue() = default;
  virtual int genre() const = 0;  // valeur de l'énumération Outil de la vue
  virtual bool unCoup() const { return false; }
  virtual void survol(const GesteVue&) {}
  virtual void appui(const GesteVue&) {}
  virtual void glisser(const GesteVue&) {}
  virtual void relacher(const GesteVue&) {}
  virtual void doubleClic(const GesteVue&) {}
  // Action du registre (RegistreActions, 1.8) désignée par une frappe ou un bouton : vrai si l'outil la fait dans
  // son état présent ; avec faire à faux, il dit seulement s'il la ferait (le canevas ne garde que ces touches, S22)
  virtual bool agir(std::string_view /*action*/, bool /*faire*/) { return false; }
  // Échap : vrai si l'outil l'a gardé (geste en cours annulé, segment en aperçu retiré) ; faux : on en sort
  virtual bool echap() { return false; }
  // Ctrl+Z pendant un tracé : le tracé entier est retiré, sans toucher à l'historique ; vrai s'il y en avait un
  virtual bool annulerTrace() { return false; }
  virtual bool geste() const { return false; }  // un geste dure (glisser, tracé commencé)
  // Le document a arrêté le geste (Ctrl+Z pendant un glisser, A15) : l'outil lâche l'objet
  virtual void gesteInterrompu() {}
  virtual void dessiner(QPainter&) const {}
  virtual ContenuBandeau bandeau() const { return {}; }
  virtual QCursor curseur() const { return Qt::CrossCursor; }
  // Un autre outil va le remplacer : il pose ce qui est fixé (une piste en cours)
  virtual void quitter() {}
  // Suspendu par une commande lancée pendant son tracé, puis repris là où il était (1.17)
  virtual void suspendre() {}
  virtual void reprendre() {}
};

// Outil actif d'une vue, et outils suspendus sous lui
// L'outil remplacé n'est détruit qu'au geste suivant (recycler) : un outil peut en armer un autre depuis l'une de
// ses propres méthodes (la pose à un coup rend la main à l'outil Sélection) sans se détruire sous lui-même.
class GestionnaireOutils {
 public:
  OutilVue* actif() const { return m_actif.get(); }
  void armer(std::unique_ptr<OutilVue> o) {
    for (auto& s : m_suspendus) m_corbeille.push_back(std::move(s));
    m_suspendus.clear();
    if (m_actif) m_corbeille.push_back(std::move(m_actif));
    m_actif = std::move(o);
  }
  void recycler() { m_corbeille.clear(); }
  // Pose un outil par-dessus l'actif, qui attend (1.17)
  void empiler(std::unique_ptr<OutilVue> o) {
    if (m_actif) {
      m_actif->suspendre();
      m_suspendus.push_back(std::move(m_actif));
    }
    m_actif = std::move(o);
  }
  // L'outil actif se termine : le suspendu reprend ; rend faux s'il n'y en avait pas
  bool depiler() {
    if (m_suspendus.empty()) return false;
    if (m_actif) m_corbeille.push_back(std::move(m_actif));
    m_actif = std::move(m_suspendus.back());
    m_suspendus.pop_back();
    m_actif->reprendre();
    return true;
  }
  bool suspendus() const { return !m_suspendus.empty(); }
  const OutilVue* suspendu() const { return m_suspendus.empty() ? nullptr : m_suspendus.back().get(); }
  // Change l'outil actif sans toucher aux suspendus (une autre commande pendant une commande imbriquée)
  void remplacer(std::unique_ptr<OutilVue> o) {
    if (m_actif) m_corbeille.push_back(std::move(m_actif));
    m_actif = std::move(o);
  }

 private:
  std::unique_ptr<OutilVue> m_actif;
  std::vector<std::unique_ptr<OutilVue>> m_suspendus, m_corbeille;
};
