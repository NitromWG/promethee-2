// Prométhée : formulaire de panneau lié au document. Chaque champ applique sa valeur à la fin de la saisie (une
// modification passe par l'historique ; une valeur inchangée n'en crée pas) ; les groupes s'empilent dans la
// colonne du panneau. Sert aux calculs de l'atelier électrique (installation, photovoltaïque). Licence GPL-3.0-only.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QObject;
class QSpinBox;
class QVBoxLayout;

namespace prom::elec {
struct AvisCalcul;
}

class FormulaireLie {
 public:
  FormulaireLie(QObject* contexte, QVBoxLayout* colonne) : m_contexte(contexte), m_col(colonne) {}
  QFormLayout* groupe(const QString& titre, const QString& nom = {}) const;
  // « auto0 » : texte montré à la valeur minimale (« auto », « aucun »…)
  QDoubleSpinBox* reel(QFormLayout* f, const QString& etiquette, double valeur, double min, double max, int decimales, const QString& suffixe, const QString& nom,
                       const std::function<void(double)>& appliquer, const QString& bulle = {}, const QString& auto0 = {}) const;
  QSpinBox* entier(QFormLayout* f, const QString& etiquette, int valeur, int min, int max, const QString& nom, const std::function<void(int)>& appliquer,
                   const QString& bulle = {}, const QString& auto0 = {}) const;
  QComboBox* choix(QFormLayout* f, const QString& etiquette, const QStringList& items, int courant, const QString& nom, const std::function<void(int)>& appliquer,
                   const QString& bulle = {}) const;
  QCheckBox* coche(QFormLayout* f, const QString& texte, bool valeur, const QString& nom, const std::function<void(bool)>& appliquer, const QString& bulle = {}) const;
  QLineEdit* texte(QFormLayout* f, const QString& etiquette, const std::string& valeur, const QString& nom, const std::function<void(const std::string&)>& appliquer) const;
  // Avis d'un calcul (erreurs, alertes, informations), chacun avec sa règle
  void avis(const std::vector<prom::elec::AvisCalcul>& liste, const QString& nom) const;

 private:
  QObject* m_contexte;
  QVBoxLayout* m_col;
};
