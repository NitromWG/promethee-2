// Prométhée : réglages déclarés une fois (lot L2, tâche 2.32, ERG-38, ERG-39, ERG-35, ERG-16). Chaque réglage a sa
// clé, son libellé, ses mots voisins et sa page : la fenêtre des réglages en tire ses champs et sa recherche (le
// réglage trouvé est ouvert et surligné), la palette Ctrl+K en tire ses entrées. Les réglages s'exportent dans un
// fichier, se relisent et se remettent à zéro, raccourcis, ruban et thème compris ; un dossier synchronisé
// (nuage, partage) les garde à jour d'un poste à l'autre. Au premier lancement d'une version, ceux de la version
// d'avant sont repris, clés renommées comprises.
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>

struct Reglage {
  enum class Genre { Booleen, Entier, Reel, Choix, Texte };
  QString cle;          // clé des réglages (QSettings)
  QString libelle;      // « Repère donné à la pose »
  QString motsVoisins;  // ce que l'on pourrait taper pour le trouver : « annotation numérotation »
  QString page;         // « Schéma », « Apparence », « Session », « Souris et pavé tactile »
  Genre genre = Genre::Booleen;
  QVariant defaut;
  QStringList choix{};  // Choix : les libellés ; la valeur gardée est l'indice, ou « valeurs » à cet indice
  QStringList valeurs{};
  double min = 0, max = 100, pas = 1;
  QString suffixe{};
  QString bulle{};
  bool redemarrer = false;                            // ne vaut qu'au prochain lancement
  std::function<void(const QVariant&)> appliquer{};  // effet immédiat, après l'écriture
};

class RegistreReglages {
 public:
  static RegistreReglages& instance();
  const std::vector<Reglage>& tous() const { return m_reglages; }
  const Reglage* reglage(const QString& cle) const;
  QStringList pages() const;  // dans l'ordre de déclaration
  // Recherche sans accents ni casse, mots dans le désordre, dans le libellé, les mots voisins et la page
  std::vector<const Reglage*> chercher(const QString& texte) const;
  QVariant valeur(const QString& cle) const;
  void ecrire(const QString& cle, const QVariant& v);

  // Export, relecture, remise à zéro (ERG-39) : tous les réglages de l'utilisateur, raccourcis, ruban et thème compris
  QJsonObject exporter() const;
  bool relire(const QJsonObject& o, QString* erreur = nullptr);
  void remettreAZero();
  bool exporterFichier(const QString& chemin, QString* erreur = nullptr) const;
  bool relireFichier(const QString& chemin, QString* erreur = nullptr);
  // Dossier synchronisé : chaque changement y est écrit ; au démarrage, un fichier plus récent que les réglages
  // locaux est relu
  QString dossierSynchronise() const;
  void setDossierSynchronise(const QString& dossier);
  void synchroniser();       // écrit le fichier du dossier synchronisé (s'il y en a un)
  bool relireSynchronise();  // au démarrage
  // Version précédente (ERG-16) : rend la version d'où les réglages ont été repris, vide s'il n'y avait rien à reprendre
  QString reprendreVersionPrecedente(const QString& versionCourante);

 private:
  RegistreReglages();
  void declarer(Reglage r) { m_reglages.push_back(std::move(r)); }
  std::vector<Reglage> m_reglages;
};
