// Prométhée : le document ouvert, partagé par toutes les vues. Licence GPL-3.0-only.
#pragma once

#include <optional>

#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>

#include "promethee/commandes.hpp"
#include "promethee/derive.hpp"
#include "promethee/fichiers.hpp"
#include "promethee/modele.hpp"
#include "promethee/verifs.hpp"

class QLockFile;

// Le fichier est déjà ouvert dans une autre fenêtre de Prométhée (verrou tenu par le processus « processus »).
class FichierDejaOuvert : public std::runtime_error {
public:
  FichierDejaOuvert(const std::string& message, qint64 pid) : std::runtime_error(message), processus(pid) {}
  qint64 processus;
};
// Le fichier a changé sur le disque depuis son ouverture ou son dernier enregistrement (autre fenêtre, autre
// programme, synchronisation) : l'écraser perdrait ces changements.
class ConflitEnregistrement : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Élément désigné dans les vues (rien, la carte, un composant ou un trou).
struct Cible {
  enum Type { Aucune, Carte, Composant, Trou } type = Aucune;
  std::string id;
  bool operator==(const Cible& o) const { return type == o.type && id == o.id; }
  bool operator!=(const Cible& o) const { return !(*this == o); }
};

class Document : public QObject {
  Q_OBJECT
public:
  explicit Document(QObject* parent = nullptr);
  ~Document() override;

  const prom::Projet& projet() const { return m_projet; }
  const prom::Derive& derive() const { return m_derive; }
  const std::vector<prom::Probleme>& problemes() const { return m_problemes; }
  // Problèmes établis hors du modèle par un atelier (contraintes d'assemblage non satisfaites…), comptés avec les
  // autres dans la barre d'état ; une source vide les retire (AS-12)
  void definirProblemesExternes(const std::string& source, std::vector<prom::Probleme> problemes);
  int nombreErreurs() const;

  // Modification en un pas d'historique (« nom » : ce que disent le menu Édition et le panneau Historique).
  void modifier(const std::function<void(prom::Projet&)>& f, const QString& nom = {});
  void remplacer(const prom::Projet& p);
  // Modification d'une seule conception électronique (A2) : seule elle est photographiée, la mécanique n'est
  // recalculée que si elle pilote le boîtier, et le changement est décrit objet par objet (conceptionChangee, A12).
  // « fusion » : deux modifications de même clé à moins d'une seconde font un seul pas (appuis sur les flèches,
  // C18). Rend faux si rien n'a changé ou si la conception publiée l'a refusé.
  bool modifierConception(int indice, const std::function<void(prom::ecad::ConceptionElec&)>& f, const QString& nom = {}, const QString& fusion = {});
  // Clé de fusion du dernier pas, s'il peut encore en recevoir un autre (flèches) : l'atelier attend la fin des
  // appuis pour recalculer (C18) ; vide sinon
  QString fusionOuverte() const;
  // Commande typée (propriété d'un composant…) : appliquée, nommée, fusionnée avec la précédente quand elle le
  // permet (frappes d'un même champ pendant une saisie, P2)
  bool executer(int indice, std::unique_ptr<prom::ecad::Commande> commande);
  // Geste continu (glisser) : toutes les étapes comptent pour un seul pas d'historique.
  void debutGeste();
  // « electronique » : le geste ne touche qu'une conception électronique ; le projet mécanique (dérivés, lien
  // avec le boîtier, vérifications) n'est recalculé qu'à la fin du geste, plus à chaque mouvement (EL-13)
  void pendantGeste(const std::function<void(prom::Projet&)>& f, bool electronique = false);
  void finGeste(const QString& nom = {});
  bool enGeste() const { return m_geste; }
  // Mode consultation (2.29, ERG-33) : le projet se lit, se parcourt et se cherche ; toute modification est refusée
  // et dite (refus), jusqu'à ce que le mode soit quitté
  void setConsultation(bool oui);
  // Projet tenu par un autre poste à l'ouverture (12.10) : « Tenu par Lou sur ATELIER-2 depuis … », vide sinon
  QString tenuPar() const { return m_tenuPar; }
  static QString personne();  // nom de l'utilisateur, pour les verrous et les notes
  // Historique local du fichier ouvert (12.9) : un instantané à chaque enregistrement, rangé par contenu
  QString dossierHistorique() const;
  static QString poste();
  bool consultation() const { return m_consultation; }
  // Couche de l'enseignant (16.12) : ses parties verrouillées et la couche elle-même sont refusées avec la raison ;
  // la fenêtre de l'enseignant lève le refus le temps de poser et sceller la couche (le sceau trahit toute autre pose)
  void autoriserCoucheEnseignant(bool oui) { m_coucheAutorisee = oui; }
  // Geste sur une conception électronique (glisser) : seule elle est photographiée ; finGeste en fait un pas, et
  // Ctrl+Z pendant le geste ne fait que l'annuler, sans toucher à l'historique (A15)
  void debutGesteConception(int indice);
  void annulerGeste();
  // Modification qui ne s'annule pas (publication pour la fabrication) : l'historique repart de là.
  void modifierDefinitivement(const std::function<void(prom::Projet&)>& f);
  // Réglage de travail sans pas d'historique (grille, unité d'une carte) : le projet est seulement marqué modifié
  void reglerSansHistorique(const std::function<void(prom::Projet&)>& f);

  // Sélection d'un atelier gardée avec chaque pas d'une conception : après l'annulation du pas, elle revient
  // (selectionRendue), et l'on voit ce qui a changé (S24)
  void setSuiviSelection(std::function<std::vector<std::string>(const std::string& conception)> f) { m_suiviSelection = std::move(f); }

  bool peutAnnuler() const { return !m_annuler.empty(); }
  bool peutRetablir() const { return !m_retablir.empty(); }
  void annuler();
  void retablir();
  // Historique nommé (SAI-24) : « Valeur de R3 : 10k → 4k7 »
  QString nomAnnuler() const;
  QString nomRetablir() const;
  QStringList historique() const;   // pas faits puis pas défaits, du plus ancien au plus récent
  int positionHistorique() const;   // nombre de pas faits : les suivants de historique() sont défaits
  void allerA(int position);         // annule ou rétablit jusqu'à cette position

  const Cible& selection() const { return m_selection; }
  void selectionner(const Cible& c);
  const prom::Composant* composantSelectionne() const;
  const prom::Trou* trouSelectionne() const;

  const QString& chemin() const { return m_chemin; }
  bool modifie() const;
  // Lève FichierDejaOuvert (sauf « ignorerVerrou »), prom::ErreurFichier (illisible, tronqué, format plus récent…).
  void ouvrir(const QString& chemin, bool ignorerVerrou = false);
  // Écriture atomique. Lève ConflitEnregistrement si le fichier a changé ailleurs (sauf « forcer »),
  // FichierDejaOuvert pour un nouveau nom déjà ouvert ailleurs (sauf « forcer »), prom::ErreurFichier.
  void enregistrer(const QString& chemin, bool forcer = false);
  // Ce que la dernière ouverture a ignoré, borné ou migré (vide si rien).
  const std::vector<std::string>& avertissementsLecture() const { return m_avertissements; }
  // Identifiant du document pour la durée de la session (copies de récupération).
  const QString& identifiant() const { return m_id; }
  std::string contenu() const;  // texte du fichier, tel qu'il serait enregistré
  // Reprend une copie de récupération : le projet s'ouvre modifié, rattaché à son fichier d'origine.
  void restaurerCopie(const std::string& contenu, const QString& cheminOrigine);
  void nouveau();
  void exemple();
  void creer(const prom::Projet& p) { charger(p); }  // nouveau projet déjà rempli, sans pas d'historique
  // Vierge : rien d'ouvert ni de créé (aucune pièce, carte, assemblage ou schéma, jamais enregistré, pas modifié).
  bool estVierge() const;

signals:
  void change();             // le modèle a changé, tout est recalculé
  void selectionChangee();
  void etatChange();         // historique, chemin ou état « modifié »
  // Le projet a été remplacé en bloc (Annuler, Rétablir, ouverture, nouveau projet, copie de récupération) :
  // les rangs que les ateliers tiennent (composant choisi, sélection multiple, contrainte commencée) ne
  // désignent plus forcément les mêmes objets. Émis avant change() et etatChange().
  void projetRemplace();
  // Un autre projet est chargé (ouverture, nouveau, exemple, copie de récupération) : sélections, outils et
  // cadrages des ateliers repartent de zéro (S24). Émis après projetRemplace().
  void projetCharge();
  // Après l'annulation d'un pas d'une conception : la sélection qu'avait l'atelier avant ce pas
  void selectionRendue(const std::string& conception, const std::vector<std::string>& selection);
  void refus(const QString& raison);  // modification refusée (conception publiée et verrouillée)
  // Une conception électronique a changé (A12) : les objets touchés et la nature du changement. Ni change() ni la
  // dérivation mécanique ne suivent, sauf si cette conception pilote le boîtier.
  void conceptionChangee(const prom::ecad::ChangementConception& changement);
  // Le geste en cours a été arrêté par le document (Ctrl+Z pendant un glisser, A15) : la vue qui le menait lâche
  // l'objet et ne poursuit pas le glissement
  void gesteInterrompu();

private:
  struct Pas;  // pas d'historique : instantané du projet, ou commande sur une conception
  const std::string& instantane() const;
  void empilerAnnulation(std::string etat, const QString& nom = {});
  void empiler(std::unique_ptr<Pas> pas);
  void depiler();  // retire le dernier pas, devenu sans effet (saisie revenue à sa valeur de départ)
  void apresConception(int indice, prom::ecad::ChangementConception ch);
  void terminerGesteInterrompu();
  int indiceConception(const std::string& uuid) const;
  std::uint64_t etatCourant() const;
  void oublierHistorique();
  void restaurer(const std::string& s);
  void recalculer();
  void charger(const prom::Projet& p);  // remplace le projet sans le marquer modifié (nouveau, exemple)
  void validerSelection();
  std::unique_ptr<QLockFile> verrouPour(const QString& chemin, bool ignorer) const;
  void lacherFichier();
  // Conceptions publiées (verrouillées) : contenu à garder tel quel, et clé de comparaison sans les réglages
  // d'affichage (grille, unité), qui restent libres sur une révision publiée
  struct Verrou {
    std::string uuid, json, cle;
  };
  std::vector<Verrou> verrous() const;
  void proteger(const std::vector<Verrou>& avant);
  // Après une annulation ou un rétablissement, les conceptions publiées reprennent leur état publié : seule la
  // publication est irréversible, le reste de l'historique se défait normalement (EL-18). Vrai si l'état restauré
  // ne diffère du précédent que par elles.
  void reappliquerVerrous(const std::vector<Verrou>& avant);

  prom::Projet m_projet;
  prom::Derive m_derive;
  std::vector<prom::Probleme> m_problemes;
  std::map<std::string, std::vector<prom::Probleme>> m_problemesExternes;
  std::vector<std::unique_ptr<Pas>> m_annuler, m_retablir;
  std::string m_avantGeste;
  // Geste sur une conception : son indice et son état de départ (-1 : geste du projet entier)
  int m_gesteConception = -1;
  std::optional<prom::ecad::ConceptionElec> m_conceptionAvantGeste;
  // État enregistré (A2) : identifiant du dernier pas fait, sans re-sérialiser le projet à chaque modification ;
  // « m_horsHistorique » : réglage modifié hors historique (grille, unité) depuis l'enregistrement
  std::uint64_t m_prochainPas = 1, m_base = 0, m_enregistre = 0;
  bool m_horsHistorique = false;
  mutable std::optional<std::string> m_instantane;  // vidé à chaque modification du projet
  bool m_geste = false;
  bool m_consultation = false;
  bool m_coucheAutorisee = false;
  std::function<std::vector<std::string>(const std::string&)> m_suiviSelection;
  Cible m_selection;
  QString m_chemin;
  QString m_id;
  std::unique_ptr<QLockFile> m_verrou;     // tant que le fichier est ouvert ici
  // Verrou partagé, en petit fichier à côté du projet (12.10) : un autre poste qui le tient vivant met le projet en
  // consultation ; le nôtre est renouvelé chaque minute et levé à la fermeture
  QString m_cheminVerrouPartage, m_tenuPar;
  class QTimer* m_vieVerrou = nullptr;
  void poserVerrouPartage(const QString& chemin);
  void leverVerrouPartage();
  prom::EtatDisque m_disque;               // état du fichier à l'ouverture ou au dernier enregistrement
  std::vector<std::string> m_avertissements;
};
