// Prométhée : fenêtres du schéma avancé de la 0.34 (lot L7) — graphe des réseaux rangé par ressorts (7.14), jeux de
// styles (7.25), assistant de symboles normalisés (7.20), générateurs de cuivre (7.24), tableau des pastilles d'une
// empreinte (7.9, 7.21) et import de formes DXF ou SVG sur la feuille (7.19).
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QDialog>
#include <QWidget>

#include "promethee/empreintes_edition.hpp"
#include "promethee/schema_avance.hpp"

class AtelierElectronique;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTableWidget;
class QTabWidget;

// 7.14 : composants et réseaux en nœuds, rangés par ressorts ; un clic sur un réseau le surligne au schéma
class VueGrapheReseaux : public QWidget {
  Q_OBJECT
 public:
  explicit VueGrapheReseaux(QWidget* parent = nullptr);
  void setGraphe(prom::ecad::GrapheReseaux g);
  const prom::ecad::GrapheReseaux& graphe() const { return m_g; }
  int noeudSous(QPointF q) const;  // −1 : aucun
 signals:
  void reseauChoisi(const QString& nom);

 protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(class QMouseEvent* e) override;

 private:
  QPointF ecran(const prom::ecad::NoeudGraphe& n) const;
  prom::ecad::GrapheReseaux m_g;
  double m_x0 = 0, m_y0 = 0, m_k = 1;
  int m_choisi = -1;
};

class DialogueGrapheReseaux : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueGrapheReseaux(AtelierElectronique& a, QWidget* parent = nullptr);
  void recalculer();  // graphe refait d'après la conception, rangé
  VueGrapheReseaux* vue() const { return m_vue; }

 private:
  AtelierElectronique& m_atelier;
  VueGrapheReseaux* m_vue;
  QCheckBox* m_sansAlim;
  QLabel* m_bilan;
};

// 7.25 : jeux intégrés, jeu de la conception enregistré, jeu relu d'un fichier
class DialogueJeuxStyles : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueJeuxStyles(AtelierElectronique& a, QWidget* parent = nullptr);
  bool appliquer(const QString& nom);  // un jeu de la liste
  bool charger(const QString& chemin);  // ajouté à la liste et appliqué
  bool enregistrer(const QString& chemin, const QString& nom);

 private:
  void remplir();
  AtelierElectronique& m_atelier;
  std::vector<prom::ecad::JeuStyles> m_jeux;
  QListWidget* m_liste;
  QLabel* m_detail;
};

// 7.20 : genre, empreinte et style ; le symbole dessiné et numéroté comme l'empreinte, posé au schéma
class DialogueAssistantSymbole : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueAssistantSymbole(AtelierElectronique& a, QWidget* parent = nullptr);
  void regler(const QString& genre, const QString& empreinte, const QString& style = QStringLiteral("CEI"));
  std::optional<prom::ecad::DefSymbole> symbole() const;
  bool poser();  // armé à la pose sur la feuille montrée

 private:
  void majApercu();
  AtelierElectronique& m_atelier;
  QComboBox *m_genre, *m_style;
  QLineEdit* m_empreinte;
  QLabel *m_apercu, *m_broches;
};

// 7.24 : bobine planaire (inductance de Wheeler modifiée), piste résistive, capteur capacitif
class DialogueGenerateurs : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueGenerateurs(AtelierElectronique& a, QWidget* parent = nullptr);
  prom::ecad::Bobine bobine() const;
  double inductanceNh() const;
  int poserBobine(const QString& reseau = {});      // au centre de la carte ; rend le nombre de pistes posées
  double poserPisteResistive(double ohms, double largeurMm, const QString& reseau);  // rend la longueur posée
  int poserCapteur(prom::ecad::GenreCapteur genre, int segments, double taille);
  QTabWidget* onglets() const { return m_onglets; }

 private:
  void majInductance();
  AtelierElectronique& m_atelier;
  QTabWidget* m_onglets;
  QComboBox* m_forme;
  QSpinBox* m_spires;
  QDoubleSpinBox *m_diametre, *m_largeur, *m_ecart;
  QLabel* m_inductance;
};

// 7.9, 7.21 : pastilles d'une empreinte en tableau (doublons signalés), renumérotées en série ou en matrice, lues d'un
// fichier de coordonnées, pastille modèle, cote réglée ; contrôlée puis enregistrée au projet, les composants posés
// mis à jour sur demande
class DialoguePastilles : public QDialog {
  Q_OBJECT
 public:
  DialoguePastilles(AtelierElectronique& a, const std::string& empreinte, QWidget* parent = nullptr);
  const prom::ecad::DefEmpreinte& empreinte() const { return m_def; }
  void setEmpreinte(const prom::ecad::DefEmpreinte& d);
  int renumeroterSerie();
  int renumeroterMatrice();
  bool lireCoordonnees(const QString& chemin);
  int appliquerModele(const std::vector<std::string>& numeros);  // la pastille de la première ligne choisie, sur les autres
  bool reglerCote(const std::string& a, const std::string& b, double mm);
  // 10.21 : forme libre des pastilles choisies (toutes si aucune) tirée d'un DXF ou d'un SVG, mise à « largeur » mm
  // (0 : à l'échelle du fichier)
  bool formeDepuisDessin(const QString& chemin, const std::vector<std::string>& numeros, double largeur = 0);
  QStringList controler() const;
  int doublons() const;
  // Rangée au projet ; « composants » : ceux à reprendre (vide : aucun) ; rend le nombre repris
  int enregistrer(bool mettreAJourComposants);
  QTableWidget* tableau() const { return m_tableau; }

 private:
  void remplir();
  AtelierElectronique& m_atelier;
  prom::ecad::DefEmpreinte m_def;
  QTableWidget* m_tableau;
  QLabel* m_bilan;
  QCheckBox* m_majComposants;
};

// 7.19 : DXF (calques à choisir) ou SVG en formes modifiables de la feuille montrée
class DialogueImportFormes : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueImportFormes(AtelierElectronique& a, QWidget* parent = nullptr);
  bool choisirFichier(const QString& chemin);
  std::set<std::string> calquesChoisis() const;
  void choisirCalques(const std::set<std::string>& calques);
  int importer();  // rend le nombre de formes posées

 private:
  AtelierElectronique& m_atelier;
  QString m_chemin;
  QByteArray m_contenu;
  QLabel* m_fichier;
  QListWidget* m_calques;
  QDoubleSpinBox* m_echelle;
  QSpinBox* m_calqueNotes;
  QLabel* m_bilan;
};
