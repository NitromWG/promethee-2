// Prométhée : « Enregistrer pour une version précédente… » (lot L1, tâche 1.22, ECH-11). Pour un collègue qui n'a
// pas encore migré : la version choisie (la précédente d'abord), la liste de ce qui s'y perd, calculée par l'inverse
// de chaque étape de migration, puis un fichier neuf (« carte.pour-0-29.prom ») ; le projet ouvert, son chemin et
// son état ne changent pas.
// Licence GPL-3.0-only.
#pragma once

#include <optional>

#include <QDialog>
#include <QString>
#include <QStringList>

#include "promethee/migrations.hpp"

class QComboBox;
class QLabel;
class QListWidget;

class DialogueVersionPrecedente : public QDialog {
  Q_OBJECT
 public:
  DialogueVersionPrecedente(const prom::Projet& p, QString cheminProjet, QWidget* parent = nullptr);
  QString version() const;
  void setVersion(const QString& v);
  QStringList pertes() const;
  QString cheminPropose() const;  // à côté du projet, « .pour-0-29 » avant l'extension
  bool ecrire(const QString& chemin, QString* erreur = nullptr) const;

 private:
  void calculer();
  const prom::Projet& m_projet;
  QString m_chemin;
  QComboBox* m_version;
  QLabel* m_resume;
  QListWidget* m_pertes;
  std::optional<prom::FichierAnterieur> m_fichier;
};
