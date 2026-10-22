// Prométhée : interface entre processus de l'application (lot L13, 13.10). Licence GPL-3.0-only.
#include "ServeurApi.hpp"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>

#include "Document.hpp"
#include "Journal.hpp"
#include "promethee/api_commandes.hpp"
#include "promethee/commandes.hpp"
#include "promethee/modele.hpp"

ServeurApi::ServeurApi(Document* doc, QObject* parent) : QObject(parent), m_doc(doc) {}

ServeurApi::~ServeurApi() {
  if (m_serveur) m_serveur->close();
}

bool ServeurApi::demarrer(const QString& nomDemande) {
  QString nom = nomDemande;
  if (nom.isEmpty()) nom = qEnvironmentVariable("PROMETHEE_API");
  if (nom.isEmpty()) nom = QStringLiteral("promethee-") + Document::personne();
  m_serveur = new QLocalServer(this);
  m_serveur->setSocketOptions(QLocalServer::UserAccessOption);  // l'utilisateur seul
  if (!m_serveur->listen(nom)) {
    // Une autre fenêtre de Prométhée tient déjà ce nom : celui-ci, suivi du processus
    nom += QStringLiteral("-") + QString::number(QCoreApplication::applicationPid());
    QLocalServer::removeServer(nom);
    if (!m_serveur->listen(nom)) return false;
  }
  connect(m_serveur, &QLocalServer::newConnection, this, [this] {
    while (QLocalSocket* s = m_serveur->nextPendingConnection()) {
      connect(s, &QLocalSocket::readyRead, this, [this, s] { lire(s); });
      connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
    }
  });
  journal("interface JSON-RPC ouverte sur le canal local « " + m_serveur->fullServerName().toStdString() + " »");
  return true;
}

QString ServeurApi::nom() const { return m_serveur ? m_serveur->fullServerName() : QString(); }

void ServeurApi::lire(QLocalSocket* s) {
  while (s->canReadLine()) {
    const QString ligne = QString::fromUtf8(s->readLine()).trimmed();
    if (ligne.isEmpty()) continue;
    s->write(traiter(ligne).toUtf8() + '\n');
    s->flush();
  }
}

QString ServeurApi::traiter(const QString& ligne) {
  prom::Projet copie = m_doc->projet();
  prom::ReponseApi r = prom::traiterTexte(copie, ligne.toStdString());
  if (r.modifie && m_doc->consultation()) {
    r.reponse.erase("result");
    r.reponse["error"] = {{"code", -32004}, {"message", "projet ouvert en consultation : modification refusée"}};
    r.modifie = false;
  }
  if (r.modifie) {
    // Une seule conception changée : un pas qui ne garde qu'elle ; sinon le projet entier
    const auto& avant = m_doc->projet().electronique;
    std::vector<int> changees;
    if (avant.size() == copie.electronique.size())
      for (std::size_t i = 0; i < avant.size(); ++i)
        if (!prom::ecad::differencier(avant[i], copie.electronique[i]).vide()) changees.push_back(static_cast<int>(i));
    const QString nomPas = QString::fromStdString(r.nomPas);
    if (changees.size() == 1) {
      const int i = changees.front();
      m_doc->modifierConception(i, [&copie, i](prom::ecad::ConceptionElec& c) { c = copie.electronique[static_cast<std::size_t>(i)]; }, nomPas);
    } else
      m_doc->modifier([&copie](prom::Projet& p) { p = std::move(copie); }, nomPas);
  }
  emit requeteTraitee(r.modifie);
  return QString::fromStdString(r.reponse.dump());
}
