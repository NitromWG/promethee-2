// Prométhée : copies de récupération des projets modifiés. Licence GPL-3.0-only.
// Toutes les deux minutes, chaque projet modifié et pas encore enregistré est copié dans le dossier de la
// session (<données locales>/Prométhée/recuperation/<session>/). Un verrou tenu tant que le programme tourne
// distingue une session vivante d'une session interrompue (plantage, arrêt forcé, coupure) : au lancement
// suivant, les copies des sessions interrompues sont proposées à la récupération. Une fermeture normale
// efface le dossier de la session.
#pragma once

#include <memory>
#include <vector>

#include <QDateTime>
#include <QString>

class Document;
class QLockFile;

class Recuperation {
public:
  static Recuperation& instance();
  // Dossier racine des données locales de Prométhée (PROMETHEE_DOSSIER_DONNEES le remplace, pour les essais).
  static QString dossierDonnees();

  void demarrer();  // crée le dossier de la session et prend son verrou
  void terminer();  // fermeture normale : efface la session
  bool active() const { return m_verrou != nullptr; }
  QString dossierSession() const { return m_dossier; }

  // Écrit la copie du document s'il est modifié, l'efface sinon. Rend vrai si une copie a été écrite.
  bool sauvegarder(const Document& d);
  void oublier(const Document& d);  // document enregistré ou fermé

  struct Copie {
    QString fichier;        // copie du projet
    QString cheminOrigine;  // fichier du projet (vide : jamais enregistré)
    QString nom;
    QDateTime date;
  };
  // Copies laissées par les sessions qui ne tournent plus.
  std::vector<Copie> orphelines() const;
  void abandonner(const Copie& c);

private:
  Recuperation() = default;
  ~Recuperation();
  std::unique_ptr<QLockFile> m_verrou;
  QString m_dossier;
};
