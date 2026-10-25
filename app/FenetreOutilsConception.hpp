// Prométhée : outils de conception (lots L12 à L19). Une fenêtre non modale, un onglet par discipline : variantes et
// coût, revue automatique, comparaison avec un autre fichier, schéma de système, notes, historique local, mise en
// service et fiabilité, intégrité, logique numérique, fabrication à l'atelier, équipe et exigences. Chaque onglet
// lit la conception courante et rend son rapport ; les actions qui modifient passent par le document (un pas
// d'annulation chacune). Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <map>

#include <QString>
#include <QWidget>

#include "promethee/liste_achat.hpp"
#include "promethee/projet_partage.hpp"

class AtelierElectronique;
class QComboBox;
class QSpinBox;
class QTabWidget;
class QTextBrowser;

class FenetreOutilsConception : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreOutilsConception(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();  // tous les rapports relus
  // Onglets (essais) : « variantes », « revue », « comparer », « systeme », « notes », « historique », « essais »,
  // « integrite », « logique », « atelier », « exigences »
  bool montrer(const QString& onglet);
  QString rapport(const QString& onglet) const;  // texte du rapport de l'onglet
  QComboBox* varianteActive() const { return m_variante; }
  QSpinBox* cartes() const { return m_cartes; }
  // 0.34, lot L19 (OutilsConception034L19.cpp) : tableau de bord de la nomenclature, recalculé à chaque commande sur
  // les seules lignes touchées, et liste d'achat (19.7) ; dépôt vers le PLM et retour (19.14)
  const prom::ecad::TableauBordNomenclature& tableauBord() const { return m_tableauBord; }
  const prom::ecad::ListeAchat& listeAchat() const { return m_listeAchat; }
  const prom::ecad::ListeAchat& calculerAchats(int cartes, const QString& fichierFrais = {});
  bool exporterPanier(const QString& distributeur, const QString& chemin);
  bool exporterPageAcheteurs(const QString& chemin);
  QString deposerPlm(const QString& dossierEquipe, const QString& revision, bool modeles = true);  // dossier du dépôt, vide sinon
  // Programme séparé d'accès en ligne (décision D13) lancé sur le dernier dépôt : sortie et code (3 : désactivé)
  int synchroniserPlm(const QString& operation, QString* sortie = nullptr);
  // Constats ouverts des revues en tickets (« Vers le PLM »), synchronisés par le programme d'accès en ligne ; ceux
  // clos chez le gestionnaire se closent dans la conception, en un pas. Rend le nombre de constats clos (−1 : échec)
  int synchroniserTickets(QString* sortie = nullptr);

 private:
  QWidget* onglet(const QString& id, const QString& titre, const std::function<QString()>& html, const std::vector<std::pair<QString, std::function<void()>>>& boutons = {});
  AtelierElectronique& m_atelier;
  QTabWidget* m_onglets;
  QComboBox* m_variante = nullptr;
  QSpinBox* m_cartes = nullptr;
  std::map<QString, QTextBrowser*> m_textes;
  std::map<QString, std::function<QString()>> m_rapports;
  std::map<QString, int> m_indices;
  QString m_comparaison;  // dernier rapport de comparaison
  // 0.34 (OutilsConception034.cpp)
  void ajouterOnglets034();
  QString m_bilanReversement;                   // dernier reversement de la copie locale (12.15)
  std::vector<prom::DonneeAchat> m_donneesAchat;  // données d'achat lues (12.18)
  void ajouterOnglets034L19();
  void majTableauBord();
  int lancerEnLigne(const QStringList& args, QString* sortie);
  prom::ecad::TableauBordNomenclature m_tableauBord;
  prom::ecad::ListeAchat m_listeAchat;
  int m_cartesAchat = 0;
  QString m_dernierDepot, m_dossierEquipePlm, m_sortiePlm;
};
