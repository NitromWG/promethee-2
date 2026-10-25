// Prométhée : programme séparé d'accès en ligne (décision D13 ; 4.22, 19.14). Réglages (désactivés tant que
// l'utilisateur ne les active pas, fonction par fonction), journal de chaque connexion, adaptateurs décrits par des
// gabarits modifiables (PLM simulé, Windchill, Teamcenter, 3DEXPERIENCE, Agile ; distributeurs). Les opérations
// lisent et remplissent les fichiers du dossier d'équipe : dépôt « Vers le PLM », retour « Depuis le PLM ».
// Licence GPL-3.0-only.
#pragma once

#include <map>
#include <optional>

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace prom::enligne {

struct ReglageFonction {
  bool actif = false;
  QString adaptateur;           // nom du gabarit (« simule », « windchill »…)
  QString adresse;              // « https://plm.exemple.fr »
  QString utilisateur, jeton;   // jeton : porteur (Bearer) ou mot de passe (Basic) selon le gabarit
  int delai = 20;               // s par requête
};
struct Reglages {
  bool actif = false;                          // interrupteur général : rien ne part sans lui
  std::map<QString, ReglageFonction> fonctions;  // « plm », « distributeurs », « tickets », « erp »
  QString journal;                             // fichier du journal des connexions (CSV)
  QString chemin;                              // fichier lu
};
// Réglages par défaut : fichier « acces-en-ligne.json » du dossier de configuration (ou des données portables)
QString cheminReglagesParDefaut();
Reglages lireReglages(const QString& chemin, QString* erreur = nullptr);
QByteArray texteReglagesParDefaut();  // tout désactivé, avec les gabarits disponibles
// La fonction peut-elle ouvrir le réseau ? Sinon la raison, à afficher (« accès en ligne désactivé »)
QString refus(const Reglages& r, const QString& fonction);

// Gabarit d'un adaptateur : opérations (méthode, chemin, corps), authentification, champs de la réponse
struct Operation {
  QString methode = QStringLiteral("GET"), chemin, corps;
  QString champRevision = QStringLiteral("revision"), champEtat = QStringLiteral("etat");
  std::map<QString, QString> entetes;
};
struct Gabarit {
  QString nom, description, authentification = QStringLiteral("bearer");  // « bearer », « basic », « aucune »
  std::map<QString, Operation> operations;                                // « deposer », « reserver », « restituer », « consulter »
};
QStringList gabaritsIntegres();
std::optional<Gabarit> gabarit(const QString& nom, const QString& dossierReglages, QString* erreur = nullptr);
std::optional<Gabarit> lireGabarit(const QByteArray& json, QString* erreur = nullptr);

// Journal : une ligne par connexion tentée (date, fonction, méthode, adresse, statut, octets envoyés, détail)
void noter(const Reglages& r, const QString& fonction, const QString& methode, const QString& adresse, int statut, qint64 envoyes, const QString& detail);

// Réponse d'une opération PLM
struct ReponsePlm {
  bool ok = false;
  int statut = 0;
  QString erreur, revision, etat, reservePar;
  QJsonObject corps;
};
// Exécute une opération du gabarit ; « valeurs » remplit les champs entre accolades du chemin et du corps
ReponsePlm executer(const Reglages& r, const QString& fonction, const Gabarit& g, const QString& operation, const std::map<QString, QString>& valeurs);

}  // namespace prom::enligne
