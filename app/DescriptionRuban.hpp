// Prométhée : description du ruban de l'atelier électronique (lot L1, tâche 1.20, ERG-22). Un fichier JSON ne cite
// que des identifiants du registre des actions : quatre onglets (schéma, circuit, 3D, fabrication), leurs groupes
// (« grand » : un gros bouton et la liste de ses actions ; « grille » : petits boutons sur deux rangs ; « contrôle » :
// liste ou réglage fabriqué par l'atelier ; « séparateur »). L'original est livré avec l'application ; la version de
// l'utilisateur, éditée par glisser dans les réglages, est gardée d'une session à l'autre. Les actions des scripts
// et des extensions (13.3), ajoutées au registre, s'y placent comme les autres.
// Licence GPL-3.0-only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>

class QJsonDocument;
class RegistreActions;

struct DescriptionRuban {
  enum class Forme : std::uint8_t { Grand, Grille, Controle, Separateur };
  struct Groupe {
    QString titre;
    Forme forme = Forme::Grand;
    std::vector<std::string> actions;
    friend bool operator==(const Groupe&, const Groupe&) = default;
  };
  struct Onglet {
    std::string id;  // « schema », « circuit », « 3d », « fabrication » : l'ordre des modes de l'atelier
    QString titre;
    std::vector<Groupe> groupes;
    friend bool operator==(const Onglet&, const Onglet&) = default;
  };
  std::vector<Onglet> onglets;
  friend bool operator==(const DescriptionRuban&, const DescriptionRuban&) = default;

  static DescriptionRuban depuisJson(const QByteArray& json, QString* erreur = nullptr);
  QByteArray versJson() const;
  // Identifiants que le registre ne connaît pas (fichier abîmé, action d'une extension retirée) : ils sont ignorés
  QStringList inconnues(const RegistreActions& r) const;
  bool contient(const std::string& id) const;
};

// Le ruban en cours : celui de l'utilisateur s'il en a un, sinon l'original ; l'atelier se reconstruit à chaque
// changement
class RubanEnCours : public QObject {
  Q_OBJECT
 public:
  static RubanEnCours& instance();
  const DescriptionRuban& description() const { return m_description; }
  static DescriptionRuban original();
  void remplacer(const DescriptionRuban& d);  // gardé dans les réglages de l'application
  void retablir();                              // l'original, la version de l'utilisateur oubliée
  void relire();                                // relit les réglages (au démarrage, essais du redémarrage)
  bool personnalise() const;

 signals:
  void change();

 private:
  RubanEnCours();
  DescriptionRuban m_description;
};
