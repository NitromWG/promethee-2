// Prométhée : vues de l'atelier en mosaïque (lot L3, tâche 3.21 ; ERG-40), à la place de la pile d'une vue à la fois.
// Une vue à la fois, comme avant : celle du mode du ruban. Côte à côte : le schéma et le circuit (et la 3D, le tableau
// des objets si on les ajoute) partagent la place dans des panneaux redimensionnables ; la vue du mode est entourée,
// un clic dans une autre la rend active (le ruban suit). Les vues sont liées par la sélection croisée : ce qui est
// choisi dans l'une l'est dans les autres, et l'atelier y cadre l'objet.
// Licence GPL-3.0-only.
#pragma once

#include <vector>

#include <QWidget>

class QSplitter;

class MosaiqueVues : public QWidget {
  Q_OBJECT
 public:
  explicit MosaiqueVues(QWidget* parent = nullptr);
  // « mode » : une vue d'un mode du ruban (schéma, circuit, 3D) ; un clic dedans la rend active
  void ajouter(QWidget* vue, bool mode = true);
  void setActive(QWidget* vue);
  QWidget* active() const { return m_active; }
  // Compatibilité avec la pile d'avant
  void setCurrentWidget(QWidget* vue) { setActive(vue); }
  QWidget* currentWidget() const { return m_active; }

  bool mosaique() const { return m_mosaique; }
  void setMosaique(bool oui);
  // Les vues montrées côte à côte, dans l'ordre (la vue active l'est toujours)
  void montrer(QWidget* vue, bool oui);
  bool montree(QWidget* vue) const;
  bool visibleDansMosaique(QWidget* vue) const;  // à l'écran en ce moment

 signals:
  void activeDemandee(QWidget* vue);  // un clic dans une vue de mode qui n'est pas active
  void dispositionChangee();

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  class Cadre;
  Cadre* cadreDe(QWidget* vue) const;
  void disposer();
  QSplitter* m_partage;
  std::vector<Cadre*> m_cadres;
  std::vector<QWidget*> m_montrees;
  QWidget* m_active = nullptr;
  bool m_mosaique = false;
};
