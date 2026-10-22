// Prométhée : aperçu d'un article de bibliothèque (lot L1, tâche 1.21, BIB-04). Quatre faces, choisies par une
// rangée de boutons : le symbole tel qu'il sera posé, l'empreinte (pastilles, perçages, sérigraphie, encombrement),
// une vue 3D schématique du boîtier posé sur sa carte (corps de l'empreinte : largeur, profondeur, hauteur) et la
// photo du composant réel (propriété « photo » du symbole : chemin d'une image) ou, faute de photo, une mention qui
// le dit. Une face sans contenu reste choisissable et dit ce qui manque. Widget autonome, sans lien avec l'atelier :
// le panneau de bibliothèque s'en sert, la fenêtre de choix des composants (4.5) le reprendra.
// Licence GPL-3.0-only.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <QPixmap>
#include <QWidget>

#include "promethee/circuit.hpp"
#include "promethee/schema.hpp"

class QLabel;
class QToolButton;

class ApercuArticle : public QWidget {
  Q_OBJECT
 public:
  enum class Face : std::uint8_t { Symbole, Empreinte, Vue3D, Photo };
  static constexpr int NOMBRE_FACES = 4;
  explicit ApercuArticle(QWidget* parent = nullptr);
  // L'article et, s'il en a une, son empreinte ; la face choisie reste d'un article à l'autre
  void montrer(const prom::ecad::DefSymbole& d, const prom::ecad::DefEmpreinte* empreinte);
  void vider();
  void setFace(Face f);
  Face face() const { return m_face; }
  // Essais : l'image de la face montrée (nulle si la face n'a rien à montrer) et la mention qui la remplace
  QPixmap image() const;
  QString mention() const;

  // Dessins seuls, réutilisables : empreinte vue de dessus, boîtier en perspective cavalière, photo à l'échelle
  static QPixmap dessinEmpreinte(const prom::ecad::DefEmpreinte& e, QSize taille);
  static QPixmap dessin3D(const prom::ecad::DefEmpreinte& e, QSize taille);
  static std::optional<QPixmap> photo(const prom::ecad::DefSymbole& d, QSize taille);

 signals:
  void faceChangee(ApercuArticle::Face f);

 protected:
  void resizeEvent(QResizeEvent* e) override;

 private:
  void dessiner();
  std::optional<prom::ecad::DefSymbole> m_symbole;
  std::optional<prom::ecad::DefEmpreinte> m_empreinte;
  Face m_face = Face::Symbole;
  std::array<QToolButton*, NOMBRE_FACES> m_boutons{};
  QLabel* m_image;
};
