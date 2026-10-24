// Prométhée : fenêtres mécatroniques de la 0.34 (lot L6) — boîtier du commerce importé en STEP (6.7), pièce montée
// sur la carte (6.8), apparence de la vue 3D et points de vue nommés (6.11), rendu de la carte à fond transparent et
// images clés (6.18).
// Licence GPL-3.0-only.
#pragma once

#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <QDialog>
#include <QImage>

#include "promethee/rendu_carte.hpp"

class AtelierElectronique;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;

// Ce que la vue 3D de la carte montre (6.11) : vernis du rendu (vide : celui de l'empilage), familles d'empreintes
// masquées, boîtes à la place des modèles, pièces montées et boîtier importé
struct ApparenceCarte3D {
  std::string vernis;
  std::set<std::string> famillesMasquees;
  bool boites = false, pieces = true, boitierImporte = false;
  friend bool operator==(const ApparenceCarte3D&, const ApparenceCarte3D&) = default;
};

// 6.7 : fichier STEP du boîtier, sa place autour de la carte et le jeu à tenir
class DialogueBoitierImporte : public QDialog {
  Q_OBJECT
 public:
  explicit DialogueBoitierImporte(AtelierElectronique& a, QWidget* parent = nullptr);
  // Lit le fichier et propose une place centrée sur la carte ; faux s'il est illisible
  bool choisirFichier(const QString& chemin);
  void reglerPlace(double x, double y, double z, double rotation, double jeu);
  // Range le boîtier dans la conception, en un pas ; rend le nombre de collisions trouvées aussitôt
  int appliquer();

 private:
  AtelierElectronique& m_atelier;
  std::optional<prom::ecad::Modele3D> m_modele;
  QLabel* m_fichier;
  QLineEdit *m_nom, *m_fabricant, *m_mpn;
  QDoubleSpinBox *m_x, *m_y, *m_z, *m_rotation, *m_jeu;
  QLabel* m_bilan;
};

// 6.8 : pièce mécanique montée sur la carte, d'un fichier STEP, posée sur un composant ou au centre de la carte
class DialoguePieceMontee : public QDialog {
  Q_OBJECT
 public:
  DialoguePieceMontee(AtelierElectronique& a, const std::string& composant, QWidget* parent = nullptr);
  bool choisirFichier(const QString& chemin);
  void reglerArticle(const QString& reference, const QString& nom, const QString& fabricant, const QString& mpn);
  std::string appliquer();  // rend l'UUID de la pièce posée, vide en cas d'échec

 private:
  AtelierElectronique& m_atelier;
  std::string m_composant;
  std::optional<prom::ecad::Modele3D> m_modele;
  QLabel* m_fichier;
  QLineEdit *m_reference, *m_nom, *m_fabricant, *m_mpn, *m_fournisseur;
  QDoubleSpinBox* m_z;
  QSpinBox* m_quantite;
  QLabel* m_bilan;
};

// 6.11 : apparence de la vue 3D de la carte
class DialogueApparence3D : public QDialog {
  Q_OBJECT
 public:
  DialogueApparence3D(AtelierElectronique& a, QWidget* parent = nullptr);
  void appliquer();

 private:
  AtelierElectronique& m_atelier;
  QComboBox* m_vernis;
  QListWidget* m_familles;
  QCheckBox *m_boites, *m_pieces, *m_boitier;
};

// 6.18 : rendu par lancer de rayons sur son fil, interruptible ; images clés et suite d'images
class FenetreRendu : public QDialog {
  Q_OBJECT
 public:
  explicit FenetreRendu(AtelierElectronique& a, QWidget* parent = nullptr);
  ~FenetreRendu() override;
  void reglerTaille(int largeur, int hauteur);
  void reglerFondTransparent(bool oui);
  // Lance le rendu sur son fil ; « fichier » non vide : l'image y est enregistrée en PNG à la fin
  void lancer(const QString& fichier = {});
  void arreter();
  bool enCours() const { return m_enCours; }
  const QImage& image() const { return m_image; }
  double progression() const { return m_progression; }
  // Images clés : la vue actuelle ajoutée à l'instant donné ; l'animation rendue en PNG numérotés dans le dossier
  void ajouterCle(double t, double leveeBoitier = 0);
  int nombreCles() const { return static_cast<int>(m_cles.size()); }
  int enregistrerAnimation(const QString& dossier, int imagesParSeconde = 10);

 signals:
  void termine(bool interrompu);

 private:
  void fin(const std::shared_ptr<prom::ImageRendu>& r);
  AtelierElectronique& m_atelier;
  QSpinBox *m_largeur, *m_hauteur, *m_echantillons;
  QCheckBox *m_transparent, *m_ombres, *m_reflets;
  QProgressBar* m_barre;
  QPushButton *m_lancer, *m_arreter;
  QLabel *m_apercu, *m_bilan;
  QListWidget* m_listeCles;
  std::thread m_fil;
  std::shared_ptr<std::atomic<bool>> m_arret;
  bool m_enCours = false;
  double m_progression = 0;
  QImage m_image;
  QString m_fichier;
  std::vector<prom::ImageCle> m_cles;
};
