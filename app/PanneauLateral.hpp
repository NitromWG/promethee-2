// Prométhée : panneau latéral d'outil (U1). Les réglages qui ouvraient une fenêtre modale (propriétés de la pièce,
// propriétés de masse, variables, propriétés physiques, répétitions et remplacement dans l'assemblage) s'ouvrent ici, à
// droite de la vue, qui reste manipulable : en-tête (icône, titre, ✕), contenu défilant, Valider et Annuler (ou
// Fermer). Une seule page à la fois : en ouvrir une autre annule la précédente. Échap annule. Licence GPL-3.0-only.
#pragma once

#include <functional>

#include <QFrame>
#include <QPointer>
#include <QString>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class PanneauLateral : public QFrame {
  Q_OBJECT
public:
  enum class Boutons { Fermer, ValiderAnnuler };
  explicit PanneauLateral(QWidget* parent = nullptr);
  // id : nom stable de la page (essais) ; surValider rend vrai pour fermer (faux : une erreur est montrée, on reste)
  void ouvrir(const QString& id, const QString& titre, const QString& icone, QWidget* contenu, Boutons boutons, std::function<bool()> surValider = {},
              std::function<void()> surAnnuler = {});
  void valider();
  void annuler();
  void fermer();  // sans rien rappeler
  QString page() const { return m_page; }
  QWidget* contenu() const { return m_contenu; }
  void setMessage(const QString& html);  // ligne d'erreur ou d'aide au-dessus des boutons

signals:
  void ouvert(const QString& page);
  void referme(const QString& page);

protected:
  void keyPressEvent(class QKeyEvent* e) override;

private:
  QString m_page;
  QLabel* m_icone;
  QLabel* m_titre;
  QLabel* m_message;
  QScrollArea* m_defilement;
  QPointer<QWidget> m_contenu;
  QPushButton* m_valider;
  QPushButton* m_annuler;
  std::function<bool()> m_surValider;
  std::function<void()> m_surAnnuler;
};
