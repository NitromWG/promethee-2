// Prométhée : barre de titre intégrée. Une seule bande en haut de la fenêtre : logo, menus, accès rapides
// (accueil, annuler, rétablir), nom du projet, recherche de commandes, et les boutons réduire, agrandir, fermer.
// La fenêtre se déplace en tirant la bande et se redimensionne par ses bords. Licence GPL-3.0-only.
#pragma once

#include <functional>

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QMenuBar;
class QToolButton;

class BarreTitre : public QWidget {
  Q_OBJECT
public:
  explicit BarreTitre(QWidget* fenetre);
  QMenuBar* menus() const { return m_menus; }
  void ajouterAction(class QAction* action);
  void ajouterSeparateur();
  void definirActions(const QList<class QAction*>& actions);  // remplace les accès rapides (onglet actif)
  void setBoutonsFenetre(bool visibles);
  // Agrandir et rétablir confiés à la fenêtre (fenêtre sans cadre : toute la surface utile de l'écran)
  void setGestionAgrandi(std::function<bool()> estAgrandi, std::function<void()> basculer) { m_estAgrandi = estAgrandi; m_basculerAgrandi = basculer; majTitre(); }
  void majTitre();

signals:
  void rechercheDemandee();

protected:
  void mousePressEvent(class QMouseEvent* e) override;
  void mouseDoubleClickEvent(class QMouseEvent* e) override;
  bool eventFilter(QObject* o, class QEvent* e) override;

private:
  void appliquerTheme();
  QWidget* m_fenetre;
  class QFrame* m_separateur = nullptr;
  QMenuBar* m_menus;
  QHBoxLayout* m_rapides;
  QLabel* m_titre;
  QToolButton* m_agrandir;
  QList<QWidget*> m_boutonsFenetre;
  std::function<bool()> m_estAgrandi;
  std::function<void()> m_basculerAgrandi;
  bool agrandie() const { return m_estAgrandi ? m_estAgrandi() : m_fenetre->isMaximized(); }
  void basculer() { if (m_basculerAgrandi) m_basculerAgrandi(); else m_fenetre->isMaximized() ? m_fenetre->showNormal() : m_fenetre->showMaximized(); }
};
