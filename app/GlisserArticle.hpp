// Prométhée : glisser-déposer d'un article de bibliothèque (lot L2, tâche 2.2, SAI-01, S1, B7). Un article pris
// dans une liste (panneau de bibliothèque, plus tard la fenêtre de choix) et tiré hors d'elle arme la pose sur le
// canevas que survole le pointeur : au schéma, le fantôme du symbole suit le pointeur et R, X, Y le tournent ou
// le retournent pendant le glisser ; au circuit, un article mécanique (trou de fixation) y montre son empreinte.
// Le lâcher pose ; Échap, ou un lâcher hors d'un canevas, renonce. Le glisser se fait dans Prométhée, sans le
// glisser-déposer du système, pour que les touches restent à l'utilisateur pendant le geste ; hors des fenêtres
// de Prométhée, il passe la main au système avec l'article en type MIME (application/x-promethee-article), que
// les canevas reçoivent aussi d'une autre instance.
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QObject>
#include <QPoint>
#include <QPointer>

#include "promethee/schema.hpp"

class QAbstractItemView;
class QLabel;
class QMimeData;
class QModelIndex;
class VueCircuit;
class VueSchema;

class GlisserArticle : public QObject {
  Q_OBJECT
 public:
  static constexpr const char* MIME = "application/x-promethee-article";
  static QMimeData* mime(const prom::ecad::DefSymbole& d);
  static std::optional<prom::ecad::DefSymbole> article(const QMimeData* m);

  // « source » : la liste d'où partent les articles ; « articleDe » rend l'article d'une ligne (rien pour une
  // catégorie)
  GlisserArticle(QAbstractItemView* source, std::function<std::optional<prom::ecad::DefSymbole>(const QModelIndex&)> articleDe, QObject* parent = nullptr);
  ~GlisserArticle() override;
  bool actif() const { return m_actif; }
  bool eventFilter(QObject* o, QEvent* e) override;

 signals:
  void depose(const QString& id);  // un article a été posé par un glisser

 private:
  void commencer();
  void suivre(QPoint global);
  void lacher(QPoint global);
  void annuler();
  void finir();
  bool touche(class QKeyEvent* k);
  void quitterCible();
  void versSysteme();
  QAbstractItemView* m_source;
  std::function<std::optional<prom::ecad::DefSymbole>(const QModelIndex&)> m_articleDe;
  std::optional<prom::ecad::DefSymbole> m_article;
  QPoint m_appui;
  bool m_appuye = false, m_actif = false;
  QPointer<VueSchema> m_schema;
  QPointer<VueCircuit> m_circuit;
  QPointer<VueCircuit> m_refus;  // circuit survolé par un article qui n'est pas mécanique
  std::vector<std::string> m_enAttente;  // R, X, Y tapés avant d'arriver sur un canevas : rejoués à l'arrivée
  QLabel* m_vignette = nullptr;          // l'article près du pointeur, hors des canevas
};
