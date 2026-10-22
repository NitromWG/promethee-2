// Prométhée : interface entre processus de l'application (lot L13, 13.10 ; AUT-04, AUT-15). Un canal local du poste
// (socket de domaine Unix, tube nommé sous Windows), jamais un port réseau, réservé à l'utilisateur : un programme
// de n'importe quel langage y envoie des requêtes JSON-RPC 2.0, une par ligne (api_commandes.hpp), et reçoit une
// réponse par ligne. Chaque requête qui modifie le projet, ou chaque « lot », forme un seul pas d'annulation ; un
// projet en consultation les refuse. Licence GPL-3.0-only.
#pragma once

#include <QObject>
#include <QString>

class Document;
class QLocalServer;
class QLocalSocket;

class ServeurApi : public QObject {
  Q_OBJECT
 public:
  explicit ServeurApi(Document* doc, QObject* parent = nullptr);
  ~ServeurApi() override;
  // Ouvre le canal « nom » (vide : PROMETHEE_API, sinon « promethee-<utilisateur> », suivi du processus s'il est pris)
  bool demarrer(const QString& nom = {});
  QString nom() const;
  // Traite une ligne (utilisé par le canal et par les essais) ; rend la réponse JSON en une ligne
  QString traiter(const QString& ligne);

 signals:
  void requeteTraitee(bool modifie);

 private:
  void lire(QLocalSocket* s);
  Document* m_doc;
  QLocalServer* m_serveur = nullptr;
};
