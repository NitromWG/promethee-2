// Prométhée : messages de l'atelier électronique (lot L1, tâche 1.11, ERG-07, S29, G8). Trois niveaux, dont aucun
// n'écrase les deux autres : le bilan permanent, à droite de la barre d'état de l'atelier, en compteurs cliquables
// (« ERC ✓ », « DRC 3 ● », « Routage 87 % »), qu'aucun message ne remplace ; les messages passagers, empilés en bas
// à gauche du canevas, trois au plus, chacun 4 s plus 60 ms par mot au-delà de dix, 12 s au plus, la souris posée
// dessus suspendant le compte, un clic menant à l'objet concerné ; le panneau Messages, ouvert depuis la barre
// d'état, qui garde les 500 derniers de la session, horodatés, filtrables par niveau et copiables ; l'atelier le pose
// dans un panneau ancrable (1.21), sinon il s'ouvre dans une fenêtre à part. La barre d'état reste à l'atelier, dans
// la fenêtre à onglets aussi : rien n'y est coupé sur une seule ligne (G8).
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>

#include <QDateTime>
#include <QLabel>
#include <QPointer>
#include <QString>
#include <QWidget>

class QComboBox;
class QTimer;
class QToolButton;
class QTreeWidget;

enum class NiveauMessage : std::uint8_t { Information, Succes, Avertissement, Erreur };

struct MessageAtelier {
  QDateTime quand;
  NiveauMessage niveau = NiveauMessage::Information;
  QString html;
  std::function<void()> aller;  // mène à l'objet concerné (vide : rien à montrer)
};

// Messages de la session, les 500 derniers
class JournalMessages : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;
  static constexpr std::size_t MAXIMUM = 500;
  void ajouter(MessageAtelier m);
  const std::deque<MessageAtelier>& messages() const { return m_messages; }

 signals:
  void ajoute(const MessageAtelier& m);

 private:
  std::deque<MessageAtelier> m_messages;
};

// Un message passager : il se ferme seul, la souris posée dessus suspend le compte, un clic mène à l'objet
class BulleMessage : public QLabel {
  Q_OBJECT
 public:
  BulleMessage(const MessageAtelier& m, QWidget* parent);
  static int duree(const QString& html);  // millisecondes : 4 s, plus 60 ms par mot au-delà de dix, 12 s au plus

 signals:
  void finie(BulleMessage* b);

 protected:
  void enterEvent(QEnterEvent* e) override;
  void leaveEvent(QEvent* e) override;
  void mousePressEvent(QMouseEvent* e) override;

 private:
  std::function<void()> m_aller;
  QTimer* m_minuterie;
  int m_reste;
  qint64 m_depart = 0;
};

// Pile de messages passagers, posée en bas à gauche d'un canevas (son hôte), trois au plus
class PileMessages : public QWidget {
  Q_OBJECT
 public:
  explicit PileMessages(QWidget* hote);
  void montrer(const MessageAtelier& m);
  int nombre() const { return static_cast<int>(m_bulles.size()); }
  QString texte(int i) const;  // essais
  // Toutes les bulles partent d'un coup (essais : un clic visé ne tombe pas sur une bulle) ; le panneau Messages les
  // garde. Cacher la pile ne suffirait pas : elle se replace, et se montre, à chaque mise en page de son hôte
  void effacer();

 protected:
  bool eventFilter(QObject* o, QEvent* e) override;

 private:
  void placer();
  std::deque<QPointer<BulleMessage>> m_bulles;
};

// Panneau Messages : la session, horodatée, filtrable par niveau, copiable
class PanneauMessages : public QWidget {
  Q_OBJECT
 public:
  explicit PanneauMessages(JournalMessages& journal, QWidget* parent = nullptr);
  int lignes() const;  // essais : lignes visibles

 protected:
  void showEvent(QShowEvent* e) override;

 private:
  void remplir();
  JournalMessages& m_journal;
  QComboBox* m_niveau;
  QTreeWidget* m_liste;
};

// Barre d'état de l'atelier : le bouton du panneau Messages à gauche, le bilan permanent à droite
class BarreEtatAtelier : public QWidget {
  Q_OBJECT
 public:
  BarreEtatAtelier(JournalMessages& journal, QWidget* parent = nullptr);
  void setBilan(const QString& html);  // jamais remplacé par un message passager
  void ajouter(QWidget* w);            // à gauche, après le bouton Messages (le filtre de sélection, 1.18)
  QString bilan() const;
  // Le panneau Messages : celui que l'hôte a posé (setPanneau), qu'il montre à la demande (messagesDemandes),
  // sinon une fenêtre à part
  void ouvrirMessages();
  void setPanneau(PanneauMessages* p);
  PanneauMessages* panneau() const { return m_panneau; }

 signals:
  void lienBilan(const QString& lien);  // « erc », « drc »…
  void messagesDemandes();

 private:
  JournalMessages& m_journal;
  QToolButton* m_messages;
  QLabel* m_bilan;
  QPointer<PanneauMessages> m_panneau;
  bool m_heberge = false;
};
