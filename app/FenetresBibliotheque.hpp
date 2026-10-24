// Prométhée : fenêtres de bibliothèque et d'approvisionnement de la 0.34 (lot L4) — reste à acheter pour N cartes
// d'après le stock de l'atelier et le catalogue de l'assembleur (4.18), échange d'empreintes en masse (4.20), gestion
// de bibliothèque en tableau : séries E, import CSV/XLSX, renommage par gabarit, copie avec dépendances (4.23), série
// d'empreintes par noms aux densités M, N, L (4.26), affectation des empreintes en tableau et par règles
// d'équivalence, non modale (4.28).
// Licence GPL-3.0-only.
#pragma once

#include <string>
#include <vector>

#include <QDialog>
#include <QWidget>

#include "promethee/approvisionnement.hpp"
#include "promethee/gestion_bibliotheque.hpp"
#include "promethee/remplacement.hpp"

class AtelierElectronique;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

// 4.18 : sources (stock de l'atelier, catalogues) importées, nombre de cartes ; le reste à acheter, par ligne
class DialogueAchats : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueAchats(AtelierElectronique& a, QWidget* parent = nullptr);
  // Source lue d'un fichier CSV ou XLSX ; faux si illisible (raison dans le bilan)
  bool ajouterSource(const QString& chemin, bool stockPropre);
  void ajouterSource(const prom::ecad::SourceStock& s);
  void reglerCartes(int n);
  void refaire();
  const std::vector<prom::ecad::BesoinAchat>& besoins() const { return m_besoins; }
  QTableWidget* table() const { return m_table; }
  QString csv() const;

 private:
  AtelierElectronique& m_atelier;
  std::vector<prom::ecad::SourceStock> m_sources;
  std::vector<prom::ecad::BesoinAchat> m_besoins;
  QSpinBox* m_cartes;
  QListWidget* m_liste;
  QTableWidget* m_table;
  QLabel* m_bilan;
};

// 4.20 : empreinte donnée en masse aux composants visés, remplacée en place sur la carte
class DialogueEchangeEmpreintes : public QDialog {
  Q_OBJECT
 public:
  DialogueEchangeEmpreintes(AtelierElectronique& a, std::vector<std::string> composants, QWidget* parent = nullptr);
  int viser(const QString& repere, const QString& valeur);
  void reglerEmpreinte(const QString& id);
  prom::ecad::RegleEchangeEmpreinte regle() const;
  QCheckBox* garderTextes() const { return m_textes; }
  QCheckBox* garderModele3D() const { return m_modele; }
  int appliquer();

 private:
  AtelierElectronique& m_atelier;
  std::vector<std::string> m_composants;
  QLineEdit *m_repere, *m_valeur, *m_empreinte;
  QCheckBox *m_textes, *m_attributs, *m_marges, *m_modele;
  QLabel* m_bilan;
};

// 4.23 : articles de la bibliothèque personnelle en tableau (édition en masse), séries E, import d'un tableau,
// renommage par gabarit, contrôles, copie vers une autre bibliothèque avec les dépendances
class DialogueGestionBibliotheque : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueGestionBibliotheque(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  QTableWidget* table() const { return m_table; }
  // Ajoute à la bibliothèque personnelle une série E sur le modèle donné ; rend le nombre d'articles créés
  int genererSerie(const prom::ecad::Article& modele, int serie, double min, double max, const prom::ecad::RenommageGabarit& g);
  int importerTableau(const QString& chemin);
  int renommer(const prom::ecad::RenommageGabarit& g, const std::vector<std::string>& ids = {});
  // Articles choisis (ou tous) copiés avec leurs dépendances dans le fichier de bibliothèque, créé s'il n'existe pas
  prom::ecad::RapportCopie copierVers(const QString& fichier, const std::vector<std::string>& ids, bool remplacer = false);
  QStringList controler() const;
  // Le tableau, édité à la main, appliqué aux articles (une révision de plus par article changé)
  int appliquerTableau();

 private:
  AtelierElectronique& m_atelier;
  std::vector<std::string> m_champs;
  QTableWidget* m_table;
  QLabel* m_bilan;
};

// 4.26 : liste de noms → empreintes aux densités choisies, contrôlées, rangées dans la bibliothèque personnelle
class DialogueSerieEmpreintes : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueSerieEmpreintes(AtelierElectronique& a, QWidget* parent = nullptr);
  void reglerNoms(const QStringList& noms);
  void reglerDensites(bool m, bool n, bool l);
  // Génère, contrôle et range ; rend le nombre d'empreintes rangées
  int generer();
  QStringList refus() const { return m_refus; }

 private:
  AtelierElectronique& m_atelier;
  QPlainTextEdit* m_noms;
  QCheckBox *m_m, *m_n, *m_l;
  QLabel* m_bilan;
  QStringList m_refus;
};

// 4.24 : piles de pastilles nommées du projet ; une cote changée, toutes les pastilles qui les citent suivent
class DialogueGabaritsPastilles : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueGabaritsPastilles(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  // Pastilles des empreintes du projet reliées au gabarit de mêmes cotes ; rend le nombre de pastilles reliées
  int relier();
  // Gabarit élargi (ou rétréci) de « dx », « dy » mm : ses pastilles suivent en une commande ; rend leur nombre
  int modifier(const std::string& nom, double dx, double dy);
  QTableWidget* table() const { return m_table; }
  int emplois(const std::string& nom) const;

 private:
  AtelierElectronique& m_atelier;
  QTableWidget* m_table;
  QLabel* m_bilan;
};

// 4.28 : composants en tableau (repère, valeur, boîtier, empreinte modifiable) et règles d'équivalence appliquées
// en une commande ; fenêtre non modale, qui suit la conception
class FenetreAffectation : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreAffectation(AtelierElectronique& a, QWidget* parent = nullptr);
  void refaire();
  void reglerRegles(const QString& texte);
  // Toutes les règles en un pas ; rend le nombre de composants affectés (bilan et journal dans la fenêtre)
  int appliquerRegles();
  // Empreinte donnée à la main dans la colonne, appliquée en un pas
  int appliquerTableau();
  QTableWidget* table() const { return m_table; }
  QCheckBox* sansEmpreinteSeuls() const { return m_sans; }

 private:
  AtelierElectronique& m_atelier;
  QPlainTextEdit* m_regles;
  QCheckBox* m_sans;
  QTableWidget* m_table;
  QLabel* m_bilan;
  std::vector<std::string> m_lignes;  // composant de chaque ligne
};
