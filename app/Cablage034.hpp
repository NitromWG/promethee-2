// Prométhée : faisceau de câblage dans l'atelier électrique (lot L19 de la 0.34 ; 19.10 à 19.13). Schéma de câblage
// (connecteurs de carte aux réseaux de leurs broches, bornier et connecteurs volants, câbles de la bibliothèque,
// fils seuls, épissures, cavaliers, contacts d'après les sections), planche (tronc et branches, rangée ensuite à la
// souris sur la planche de l'atelier, imprimée à l'échelle 1), longueurs (3D, topologie) et circuits continus,
// fabrication (contrôles, classeur, plan, machines, devis). Chaque modification est un pas d'annulation du document.
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include <QWidget>

#include "promethee/cablage.hpp"

class AtelierElectrique;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTabWidget;
class QTableWidget;

class FenetreCablage : public QWidget {
  Q_OBJECT
 public:
  explicit FenetreCablage(AtelierElectrique& a, QWidget* parent = nullptr);
  void recharger();
  bool montrer(const QString& onglet);  // « cablage », « planche », « longueurs », « fabrication »
  // ----- Faisceau et bibliothèque -----
  int faisceau() const { return m_indice; }
  void choisirFaisceau(int i);
  int nouveauFaisceau(const QString& nom);
  std::vector<prom::elec::Cablage> faisceaux() const;
  std::optional<prom::elec::Cablage> courant() const;
  const prom::elec::BibliothequeCablage& bibliotheque() const { return m_bibliotheque; }
  // Fichier de données de la bibliothèque (version 1 migrée) ; « appliquer » : les câbles posés suivent, en un pas
  bool chargerBibliotheque(const QString& fichier, bool appliquer, QStringList* migrations = nullptr);
  // ----- Câblage (19.10) -----
  bool ajouterConnecteurCarte(int conception, const QString& repere, const QString& reference = {});
  bool ajouterConnecteurSchema(const QString& repere, int cavites = 0);
  bool ajouterConnecteurVolant(const QString& repere, const QString& reference, int cavites);
  QString poserCable(const QString& designation, const QString& de, const QString& vers);
  QString poserFil(double section, const QString& couleur, const QString& de, const QString& deCavite, const QString& vers, const QString& versCavite, const QString& surFil = {});
  QString poserCavalier(const QString& connecteur, const QString& a, const QString& b, double section, double longueur);
  int affecterContacts();
  int poserEpissures();
  int ajouterFolio();  // folio « Câblage de faisceau » de ce faisceau ; son indice
  QTableWidget* tableFils() const { return m_fils; }
  // ----- Planche (19.11) -----
  bool regler(double foisonnement, double surlongueur);
  bool dessinerPlanche();  // topologie au besoin, planche de l'atelier (mode Faisceau) remplie, longueurs des fils
  int reprendrePlanche();  // nœuds rangés à la souris et longueurs saisies sur la planche de l'atelier
  bool reglerTroncon(int i, double longueur, bool coupure);
  bool poserProtection(int troncon, const QString& reference, double debut, double fin, const QString& texte = {});
  bool exporterPlanche(const QString& chemin, double echelle = 1);
  int exporterPages(const QString& chemin, const QString& format = QStringLiteral("A4"));
  bool exporterCoupe(int troncon, const QString& chemin);
  // Le symbole du connecteur remplacé par la projection en lignes cachées de son modèle 3D (STEP), de face ou de côté,
  // tourné et répété, numéros de cavités compris
  bool dessinerConnecteur(const QString& repere, const QString& modele, const QString& vue = QStringLiteral("face"), double rotation = 0, int repetitions = 1);
  // ----- Longueurs et circuits (19.12) -----
  double routerEn3D(const QString& cable, const std::vector<std::array<double, 3>>& attaches, double rayon, QStringList* alertes = nullptr);
  bool routerTopologie(const QString& fichier, const QString& cable, const QString& de, const QString& vers, const QStringList& imposes, double* longueur = nullptr);
  // Faisceau tracé en 3D (fichier de branches) mis à plat en planche, topologie et longueurs gardées
  int mettreAPlat(const QString& fichierBranches, QStringList* alertes = nullptr);
  prom::elec::BilanContinu circuit(const prom::elec::CircuitContinu& c);
  // ----- Fabrication (19.13) -----
  std::vector<prom::elec::DefautFaisceau> verifier();
  bool exporterClasseur(const QString& chemin);
  bool exporterPlan(const QString& chemin, const QString& revision = QStringLiteral("A"));
  bool exporterMachine(int gabarit, const QString& chemin);
  prom::elec::DevisFaisceau devis(const QString& fichierTemps, const QString& pdf = {}, const QString& csv = {});

 private:
  void construireCablage(QWidget* page);
  void construirePlanche(QWidget* page);
  void construireLongueurs(QWidget* page);
  void construireFabrication(QWidget* page);
  bool modifier(const std::function<void(prom::elec::Cablage&, prom::elec::ProjetElectrique&)>& f, const QString& nom);
  void majListes();

  AtelierElectrique& m_atelier;
  int m_indice = 0;
  prom::elec::BibliothequeCablage m_bibliotheque;
  QTabWidget* m_onglets = nullptr;
  QComboBox* m_choix = nullptr;
  QLineEdit* m_nom = nullptr;
  QTableWidget* m_connecteurs = nullptr;
  QComboBox *m_designation = nullptr, *m_de = nullptr, *m_vers = nullptr;
  QTableWidget* m_fils = nullptr;
  QLabel* m_etat = nullptr;
  // Planche
  QDoubleSpinBox *m_foisonnement = nullptr, *m_surlongueur = nullptr;
  QTableWidget* m_troncons = nullptr;
  QComboBox *m_protection = nullptr, *m_tronconProtection = nullptr;
  // Longueurs
  class VueTopologieCablage* m_vueTopologie = nullptr;
  QComboBox* m_cable3D = nullptr;
  QLineEdit* m_points3D = nullptr;
  QLabel* m_resultat = nullptr;
  // Fabrication
  QListWidget* m_defauts = nullptr;
  QComboBox* m_gabarit = nullptr;
};
