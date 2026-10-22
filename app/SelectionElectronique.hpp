// Prométhée : sélection partagée d'une conception électronique (lot L1, tâche 1.5). Le schéma, le circuit, la 3D et
// l'inspecteur lisent et changent la même : des identifiants typés et, à part, le réseau surligné. Un composant
// choisi d'un côté l'est de l'autre (ses symboles au schéma, son empreinte au circuit). Choisir ne fait jamais de
// pas d'annulation ; après une annulation, la sélection d'avant la commande revient (S24).
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <QObject>

#include "promethee/electronique.hpp"

class SelectionElectronique : public QObject {
  Q_OBJECT
 public:
  enum class Genre : std::uint8_t {
    // Schéma
    Symbole,
    Fil,
    Etiquette,
    TexteSchema,
    Jonction,
    NonConnexion,
    // Circuit
    Empreinte,  // identifiant : celui du composant
    Piste,
    Via,
    Zone,
    TexteCarte,
  };
  static bool auSchema(Genre g) { return g <= Genre::NonConnexion; }

  // Objet choisi. « composant » : pour un symbole, le composant qu'il pose (une empreinte en porte l'identifiant)
  struct Ref {
    Genre genre = Genre::Symbole;
    std::string id;
    std::string composant;
    friend bool operator==(const Ref& a, const Ref& b) { return a.genre == b.genre && a.id == b.id; }
    friend bool operator<(const Ref& a, const Ref& b) { return a.genre != b.genre ? a.genre < b.genre : a.id < b.id; }
  };

  // Effet d'un clic ou d'un cadre : remplacer (clic simple), ajouter (Maj), basculer (Ctrl), retirer (Ctrl+Maj),
  // les mêmes dans les deux vues (S23) et en boutons dans le bandeau de sélection (1.18)
  enum class Mode { Remplacer, Ajouter, Basculer, Retirer };
  static Mode modePour(Qt::KeyboardModifiers m);
  // Mode des clics et des cadres sans touche, choisi dans le bandeau de sélection (SAI-18, ERG-17) ; Maj et Ctrl
  // l'emportent sur lui. L'état neutre le ramène à Remplacer.
  Mode modeBandeau() const { return m_modeBandeau; }
  void setModeBandeau(Mode m);
  Mode modeClic(Qt::KeyboardModifiers m) const { return (m & (Qt::ShiftModifier | Qt::ControlModifier)) ? modePour(m) : m_modeBandeau; }
  // Filtre de sélection (SAI-18) : les genres exclus ne se prennent ni au clic ni au cadre. Un clic qu'il bloque le
  // signale (filtreBloque), et le bouton du filtre clignote.
  bool admis(Genre g) const { return !m_exclus.count(g); }
  void setAdmis(Genre g, bool oui);
  const std::set<Genre>& exclus() const { return m_exclus; }
  void signalerBlocage() { emit filtreBloque(); }

  // Forme du cadre de sélection (2.26, SAI-38) : cadre (dedans ou touchés selon le sens), hors du cadre, polygone
  // fermé d'un double-clic, trait qui prend ce qu'il touche
  enum class Forme : std::uint8_t { Cadre, Exterieur, Polygone, Trait };
  Forme forme() const { return m_forme; }
  void setForme(Forme f);
  // Ce qui n'est pas choisi (ERG-28) : estompé au niveau réglé, masqué (estompé, et ni pris ni modifié), ou en
  // ombre (à peine visible, ni pris ni modifié) ; rien de cela quand rien n'est choisi
  enum class Estompage : std::uint8_t { Aucun, Estomper, Masquer, Ombre };
  Estompage estompage() const { return m_estompage; }
  double niveauEstompage() const { return m_niveau; }  // part d'opacité retirée, de 0,1 à 0,9
  void setEstompage(Estompage e, double niveau = -1);
  bool estompageActif() const { return m_estompage != Estompage::Aucun && !m_objets.empty(); }
  bool intouchable() const { return estompageActif() && m_estompage != Estompage::Estomper; }  // masque, ombre
  double opaciteNonChoisi() const { return m_estompage == Estompage::Ombre ? 0.12 : 1.0 - m_niveau; }
  // Huit mémoires de sélection (ERG-29) : la sélection gardée, rappelée (remplacée, ou ajoutée avec Maj)
  static constexpr int MEMOIRES = 8;
  void memoriser(int i);
  bool rappeler(int i, Mode mode = Mode::Remplacer);
  std::size_t tailleMemoire(int i) const { return i >= 0 && i < MEMOIRES ? m_memoires[static_cast<std::size_t>(i)].size() : 0; }
  // Paliers d'« Étendre » (SAI-39) : la sélection d'avant chaque palier, que « Réduire » rend ; un autre choix les oublie
  void choisirPalier(const std::vector<Ref>& refs);
  bool reduire();
  std::size_t paliers() const { return m_paliers.size(); }

  explicit SelectionElectronique(QObject* parent = nullptr) : QObject(parent) {}

  const std::vector<Ref>& objets() const { return m_objets; }  // dans l'ordre du choix
  bool vide() const { return m_objets.empty(); }
  std::size_t taille() const { return m_objets.size(); }
  bool contient(Genre g, const std::string& id) const;
  // L'objet tenu : le dernier choisi (celui que l'inspecteur détaille, celui qu'on saisit pour glisser)
  const Ref* tenu() const { return m_objets.empty() ? nullptr : &m_objets.back(); }
  // Composants choisis, par un symbole ou par leur empreinte
  const std::set<std::string>& composants() const { return m_composants; }
  bool composantChoisi(const std::string& composant) const { return m_composants.count(composant) > 0; }
  std::vector<std::string> ids(Genre g) const;

  // Applique « refs » selon le mode ; rend vrai si la sélection a changé
  bool appliquer(const std::vector<Ref>& refs, Mode mode);
  bool choisir(const std::vector<Ref>& refs) { return appliquer(refs, Mode::Remplacer); }
  bool vider() { return appliquer({}, Mode::Remplacer); }
  // L'objet passe en dernier (tenu) sans changer l'ensemble
  void tenir(const Ref& r);
  // Retire ce qui n'existe plus (annulation, autre vue, suppression)
  bool filtrer(const std::function<bool(const Ref&)>& existe);

  const std::string& reseau() const { return m_reseau; }
  void surligner(const std::string& reseau);
  // État neutre : rien de choisi, rien de surligné (Échap, clic dans le vide, S5)
  void neutre();

  // L'objet existe-t-il encore dans la conception (après une annulation, une suppression, une autre vue) ?
  static bool existe(const prom::ecad::ConceptionElec& c, const Ref& r);
  // Objet du schéma désigné par son identifiant sur une feuille : son genre, et son composant pour un symbole
  static bool refSchema(const prom::ecad::Feuille& f, const std::string& id, Ref& r);
  bool filtrer(const prom::ecad::ConceptionElec& c) {
    return filtrer([&c](const Ref& r) { return existe(c, r); });
  }

  // Forme gardée avec chaque pas d'historique, et relue après son annulation
  std::vector<std::string> instantane() const;
  void restaurer(const std::vector<std::string>& s, const std::function<bool(const Ref&)>& existe);

 signals:
  void changee();
  void surlignageChange();
  void modeBandeauChange();
  void filtreChange();
  void filtreBloque();
  void formeChange();
  void estompageChange();
  void memoiresChange();

 private:
  void majComposants();
  Mode m_modeBandeau = Mode::Remplacer;
  std::set<Genre> m_exclus;
  std::vector<Ref> m_objets;
  std::set<std::string> m_composants;
  std::string m_reseau;
  Forme m_forme = Forme::Cadre;
  Estompage m_estompage = Estompage::Aucun;
  double m_niveau = 0.6;
  std::array<std::vector<Ref>, MEMOIRES> m_memoires;
  std::vector<std::vector<Ref>> m_paliers;
  bool m_palier = false;  // le choix en cours est un palier : les paliers d'avant restent
};
