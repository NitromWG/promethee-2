// Prométhée : gestes de la sélection communs au schéma et au circuit (lot L1, tâche 1.18, SAI-18, ERG-17). Le
// bandeau de sélection donne à la souris les quatre modes de SelectionElectronique (remplacer, ajouter, basculer,
// retirer), que Maj, Ctrl et Ctrl+Maj donnent au clavier ; un appui long immobile sur des objets superposés ouvre la
// liste où choisir le bon, comme Alt+clic (1.12).
// Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <utility>

#include <QPointF>
#include <QTimer>
#include <QWidget>

#include "OutilVue.hpp"
#include "SelectionElectronique.hpp"

// Bandeau de l'outil Sélection : paraît quand quelque chose est choisi ou qu'un mode autre que Remplacer est
// retenu ; la sortie (Échap) rend l'état neutre, qui ramène le mode à Remplacer
inline ContenuBandeau bandeauSelection(SelectionElectronique* sel, ActionBandeau sortie) {
  ContenuBandeau c;
  using M = SelectionElectronique::Mode;
  if (!sel || (sel->vide() && sel->modeBandeau() == M::Remplacer)) return c;
  c.titre = QStringLiteral("Sélection");
  c.detail = sel->vide() ? QStringLiteral("rien de choisi") : QStringLiteral("%1 objet(s)").arg(sel->taille());
  struct Bouton {
    M mode;
    const char* id;
    const char* libelle;
    const char* touche;
  };
  static constexpr Bouton boutons[] = {{M::Remplacer, "remplacer", "Remplacer", ""},
                                       {M::Ajouter, "ajouter", "Ajouter", "Maj"},
                                       {M::Basculer, "basculer", "Basculer", "Ctrl"},
                                       {M::Retirer, "retirer", "Retirer", "Ctrl+Maj"}};
  for (const auto& [mode, id, libelle, touche] : boutons) {
    ActionBandeau a;
    a.id = QStringLiteral("mode-") + QLatin1String(id);
    a.libelle = QString::fromUtf8(libelle);
    a.touche = QString::fromUtf8(touche);
    a.cochable = true;
    a.coche = sel->modeBandeau() == mode;
    a.faire = [sel, m = mode] { sel->setModeBandeau(m); };
    c.actions.push_back(std::move(a));
  }
  sortie.libelle = QStringLiteral("Tout désélectionner");
  c.actions.push_back(std::move(sortie));
  return c;
}

// Appui long immobile : le bouton tenu 600 ms sans bouger de plus de 3 pixels. Enfant de la vue, qui le détruit.
class AppuiLong : public QObject {
 public:
  static constexpr int DUREE = 600;
  AppuiLong(QWidget* vue, std::function<void(QPointF)> faire) : QObject(vue), m_minuterie(new QTimer(this)) {
    m_minuterie->setSingleShot(true);
    m_minuterie->setInterval(DUREE);
    QObject::connect(m_minuterie, &QTimer::timeout, this, [this, f = std::move(faire)] { f(m_point); });
  }
  AppuiLong(const AppuiLong&) = delete;
  AppuiLong& operator=(const AppuiLong&) = delete;
  void commencer(QPointF p) {
    m_point = p;
    m_minuterie->start();
  }
  void bouger(QPointF p) {
    if ((p - m_point).manhattanLength() > 3) m_minuterie->stop();
  }
  void arreter() { m_minuterie->stop(); }

 private:
  QTimer* m_minuterie;
  QPointF m_point;
};
