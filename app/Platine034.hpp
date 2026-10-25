// Prométhée : platine d'essai de l'atelier électronique (lot L16 de la 0.34 ; 16.2 à 16.4, 16.6). Vue de dessus
// synchronisée avec le schéma (fil tiré d'un trou à l'autre : la liaison passe au schéma et au chevelu en un pas),
// pattes vertes ou rouges, potentiel allumé au clic maintenu, écarts qui mènent au trou, montage posé de lui-même ;
// platine équipée en 3D dont les interrupteurs pilotent la simulation continue du laboratoire ; documents de
// câblage (plan de chargement, liste des fils, impression à l'échelle, plaque à bandes) et analyse d'une guitare.
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QWidget>

#include "promethee/platine.hpp"

class AtelierElectronique;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;
class QTableWidget;
class QTimer;

// Platine vue de dessus
class VuePlatineEssai : public QWidget {
  Q_OBJECT
 public:
  explicit VuePlatineEssai(QWidget* parent = nullptr);
  void setDonnees(const prom::ecad::ConceptionElec& c, const prom::ecad::VuePlatine& v);
  const prom::ecad::VuePlatine& vue() const { return m_v; }
  QPointF pointDuTrou(const prom::ecad::Trou& t) const;
  std::optional<prom::ecad::Trou> trouSous(QPointF p) const;
  // Potentiel allumé (clic maintenu) : trous du nœud, broches voulues et parasites
  const std::vector<prom::ecad::Trou>& potentiel() const { return m_potentiel; }
  const std::set<std::pair<std::string, std::string>>& parasites() const { return m_parasites; }
  void allumer(const std::optional<prom::ecad::Trou>& t);
  void signaler(const std::optional<prom::ecad::Trou>& t);  // trou d'un écart
  prom::ecad::EtatPatte etat(const std::string& composant, const std::string& broche) const;
  QSize sizeHint() const override { return {980, 340}; }

 signals:
  void filTire(prom::ecad::Trou a, prom::ecad::Trou b);

 protected:
  void paintEvent(class QPaintEvent*) override;
  void mousePressEvent(class QMouseEvent* e) override;
  void mouseMoveEvent(class QMouseEvent* e) override;
  void mouseReleaseEvent(class QMouseEvent* e) override;

 private:
  QPointF versEcran(double xmm, double ymm) const;
  double echelle() const;
  prom::ecad::ConceptionElec m_c;
  prom::ecad::VuePlatine m_v;
  prom::ecad::ConnectivitePlatine m_k;
  std::map<std::pair<std::string, std::string>, prom::ecad::EtatPatte> m_etats;
  std::optional<prom::ecad::Trou> m_appui, m_signale;
  QPointF m_curseur;
  bool m_tire = false;
  std::vector<prom::ecad::Trou> m_potentiel;
  std::set<std::pair<std::string, std::string>> m_voulues, m_parasites;
  int m_noeudAllume = -1;
};

// Platine équipée en 3D (projection axonométrique, corps en boîtes, pattes et fils en tubes)
class VuePlatine3D : public QWidget {
  Q_OBJECT
 public:
  explicit VuePlatine3D(QWidget* parent = nullptr);
  void setScene(std::vector<prom::ecad::Objet3DPlatine> s);
  void setEtats(const std::set<std::string>& ledsAllumees, const std::set<std::string>& fermes);
  const std::set<std::string>& ledsAllumees() const { return m_leds; }
  QPointF projeter(double x, double y, double z) const;
  // Composant visible sous ce point (le plus proche de l'œil) ; centre projeté du dessus d'un composant
  std::optional<prom::ecad::Objet3DPlatine> objetSous(QPointF p) const;
  std::optional<QPointF> centre(const std::string& reference) const;
  void tourner(double degres);
  QSize sizeHint() const override { return {980, 520}; }

 signals:
  void composantClique(QString uuid, QString reference, QString genre);

 protected:
  void paintEvent(class QPaintEvent*) override;
  void mousePressEvent(class QMouseEvent* e) override;
  void mouseMoveEvent(class QMouseEvent* e) override;
  void wheelEvent(class QWheelEvent* e) override;

 private:
  double profondeur(double x, double y, double z) const;
  std::vector<prom::ecad::Objet3DPlatine> m_scene;
  std::set<std::string> m_leds, m_fermes;
  double m_azimut = -28, m_elevation = 38, m_zoom = 1;
  QPointF m_glisse;
};

class FenetrePlatine : public QWidget {
  Q_OBJECT
 public:
  explicit FenetrePlatine(AtelierElectronique& a, QWidget* parent = nullptr);
  ~FenetrePlatine() override;
  bool montrer(const QString& onglet);  // « platine », « 3d », « documents », « guitare »
  void recharger();

  // ---------- Platine (16.2, 16.3) ----------
  VuePlatineEssai* vue() const { return m_vue; }
  bool monter();         // montage entier, en un pas
  bool etapeSuivante();  // un composant de plus
  int verifier();        // écarts listés ; rend leur nombre
  bool montrerEcart(int i);
  const std::vector<prom::ecad::EcartPlatine>& ecarts() const { return m_ecarts; }
  bool ajouterFil(const prom::ecad::Trou& a, const prom::ecad::Trou& b, QString* erreur = nullptr);
  bool deplacerPatte(const QString& reference, const QString& broche, const prom::ecad::Trou& t);
  bool deplacerComposant(const QString& reference, int colonnes, int rangees, QString* erreur = nullptr);
  prom::ecad::EtatPatte etatPatte(const QString& reference, const QString& broche) const;
  std::optional<prom::ecad::Trou> trouDe(const QString& reference, const QString& broche) const;
  std::optional<prom::ecad::Trou> trouLibre(const prom::ecad::Trou& memePotentiel) const;

  // ---------- 3D (16.4) ----------
  VuePlatine3D* vue3D() const { return m_vue3D; }
  void rafraichir3D();

  // ---------- Documents et guitare (16.6) ----------
  QString plan() const;
  bool exporter(const QString& chemin);  // .svg (plan), .pdf (à l'échelle, chemin du typon), .plt (HPGL), « coupures.svg »
  bool analyserGuitare(const QString& selecteur, const QString& sortie);
  const std::vector<prom::ecad::PositionGuitare>& positionsGuitare() const { return m_guitare; }

 private:
  void construirePlatine(QWidget* page);
  void construire3D(QWidget* page);
  void construireDocuments(QWidget* page);
  void construireGuitare(QWidget* page);
  void majDocuments();
  bool enregistrerVue(const prom::ecad::VuePlatine& v, const QString& nom);
  AtelierElectronique& m_atelier;
  QTabWidget* m_onglets = nullptr;
  QLabel* m_etat = nullptr;
  VuePlatineEssai* m_vue = nullptr;
  VuePlatine3D* m_vue3D = nullptr;
  QListWidget* m_listeEcarts = nullptr;
  QComboBox* m_couleur = nullptr;
  QPlainTextEdit* m_plan = nullptr;
  QTableWidget* m_fils = nullptr;
  QTableWidget* m_tableGuitare = nullptr;
  QComboBox* m_selecteur = nullptr;
  QComboBox* m_sortie = nullptr;
  QTimer* m_minuterie = nullptr;
  std::vector<prom::ecad::EcartPlatine> m_ecarts;
  std::vector<prom::ecad::PositionGuitare> m_guitare;
  int m_etapes = 0;
};
