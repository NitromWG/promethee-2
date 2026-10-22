// Prométhée : copies de récupération des projets modifiés. Licence GPL-3.0-only.
#include "Recuperation.hpp"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QStandardPaths>
#include <QUuid>

#include "Document.hpp"
#include "Journal.hpp"
#include "promethee/fichiers.hpp"

namespace {
const QString VERROU = QStringLiteral("session.verrou");

QString nomCopie(const Document& d) { return d.identifiant() + QStringLiteral(".prom"); }
QString nomFiche(const Document& d) { return d.identifiant() + QStringLiteral(".json"); }

void ecrire(const QString& chemin, const std::string& contenu) { prom::ecrireFichierAtomique(prom::cheminFichier(chemin.toStdString()), contenu); }
}  // namespace

Recuperation& Recuperation::instance() {
  static Recuperation r;
  return r;
}

Recuperation::~Recuperation() = default;

QString Recuperation::dossierDonnees() {
  const QByteArray force = qgetenv("PROMETHEE_DOSSIER_DONNEES");
  if (!force.isEmpty()) return QString::fromLocal8Bit(force);
  return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("Prométhée"));
}

void Recuperation::demarrer() {
  if (m_verrou) return;
  const QString racine = QDir(dossierDonnees()).filePath(QStringLiteral("recuperation"));
  m_dossier = QDir(racine).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
  if (!QDir().mkpath(m_dossier)) {
    journal("récupération : dossier de session impossible à créer");
    return;
  }
  auto v = std::make_unique<QLockFile>(QDir(m_dossier).filePath(VERROU));
  v->setStaleLockTime(0);  // un verrou n'est périmé que si son processus n'existe plus
  if (!v->tryLock(0)) {
    journal("récupération : verrou de session indisponible");
    return;
  }
  m_verrou = std::move(v);
  journal("récupération : session démarrée");
}

void Recuperation::terminer() {
  if (!m_verrou) return;
  m_verrou->unlock();
  m_verrou.reset();
  QDir(m_dossier).removeRecursively();
  journal("récupération : session terminée normalement, copies effacées");
}

bool Recuperation::sauvegarder(const Document& d) {
  if (!m_verrou) return false;
  if (!d.modifie() || d.estVierge()) {
    oublier(d);
    return false;
  }
  try {
    const QDir dossier(m_dossier);
    ecrire(dossier.filePath(nomCopie(d)), d.contenu());
    QJsonObject fiche;
    fiche[QStringLiteral("origine")] = d.chemin();
    fiche[QStringLiteral("nom")] = QString::fromStdString(d.projet().nom);
    fiche[QStringLiteral("date")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    ecrire(dossier.filePath(nomFiche(d)), QJsonDocument(fiche).toJson().toStdString());
    return true;
  } catch (const std::exception& e) {
    journal(std::string("récupération : copie impossible : ") + e.what());
    return false;
  }
}

void Recuperation::oublier(const Document& d) {
  if (m_dossier.isEmpty()) return;
  const QDir dossier(m_dossier);
  QFile::remove(dossier.filePath(nomCopie(d)));
  QFile::remove(dossier.filePath(nomFiche(d)));
}

std::vector<Recuperation::Copie> Recuperation::orphelines() const {
  std::vector<Copie> r;
  const QDir racine(QDir(dossierDonnees()).filePath(QStringLiteral("recuperation")));
  for (const QString& session : racine.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString chemin = racine.filePath(session);
    if (chemin == m_dossier) continue;
    // Le verrou se prend si sa session ne tourne plus : ses copies sont orphelines.
    QLockFile essai(QDir(chemin).filePath(VERROU));
    essai.setStaleLockTime(0);
    if (!essai.tryLock(0)) continue;
    essai.unlock();
    const QDir d(chemin);
    for (const QString& f : d.entryList({QStringLiteral("*.json")}, QDir::Files)) {
      QFile fiche(d.filePath(f));
      if (!fiche.open(QIODevice::ReadOnly)) continue;
      const QJsonObject o = QJsonDocument::fromJson(fiche.readAll()).object();
      Copie c;
      c.fichier = d.filePath(QFileInfo(f).completeBaseName() + QStringLiteral(".prom"));
      if (!QFile::exists(c.fichier)) continue;
      c.cheminOrigine = o.value(QStringLiteral("origine")).toString();
      c.nom = o.value(QStringLiteral("nom")).toString();
      c.date = QDateTime::fromString(o.value(QStringLiteral("date")).toString(), Qt::ISODate).toLocalTime();
      r.push_back(c);
    }
    // Session interrompue sans copie : rien à proposer, on fait le ménage.
    if (d.entryList({QStringLiteral("*.prom")}, QDir::Files).isEmpty()) QDir(chemin).removeRecursively();
  }
  std::sort(r.begin(), r.end(), [](const Copie& a, const Copie& b) { return a.date > b.date; });
  return r;
}

void Recuperation::abandonner(const Copie& c) {
  const QFileInfo info(c.fichier);
  QFile::remove(c.fichier);
  QFile::remove(info.dir().filePath(info.completeBaseName() + QStringLiteral(".json")));
  QDir dossier = info.dir();
  if (dossier.entryList({QStringLiteral("*.prom")}, QDir::Files).isEmpty()) dossier.removeRecursively();
}
